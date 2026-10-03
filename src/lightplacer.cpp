#include "pch.h"

#include "lightplacer.h"

#include "config.h"
#include "hooks.h"

#include <cctype>
#include <sstream>

namespace PortalLightsRuntimePatcher
{
	namespace
	{
		LightPlacerStats g_stats{};

		constexpr auto npos = std::string_view::npos;

		bool IsSpace(char a_c)
		{
			return a_c == ' ' || a_c == static_cast<char>(9) ||
			       a_c == static_cast<char>(10) || a_c == static_cast<char>(13);
		}

		std::string ReadFile(const std::filesystem::path& a_path)
		{
			std::ifstream in(a_path, std::ios::binary);
			if (!in) {
				return {};
			}
			std::ostringstream buffer;
			buffer << in.rdbuf();
			return buffer.str();
		}

		bool HasJsonExtension(const std::filesystem::path& a_path)
		{
			auto ext = a_path.extension().string();
			for (auto& c : ext) {
				c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			}
			return ext == ".json";
		}

		/// The string value of "a_key": "..." inside a_span, or empty. Light Placer
		/// configs are machine-written and every field this pass cares about is a
		/// plain string, so a scan for the quoted key followed by a colon and a
		/// quoted value is all it takes; no JSON library is linked for this.
		std::string_view StringValue(std::string_view a_span, std::string_view a_key)
		{
			std::string quoted;
			quoted += '"';
			quoted += a_key;
			quoted += '"';

			std::size_t pos = 0;
			while ((pos = a_span.find(quoted, pos)) != npos) {
				std::size_t i = pos + quoted.size();
				while (i < a_span.size() && IsSpace(a_span[i])) {
					++i;
				}
				if (i < a_span.size() && a_span[i] == ':') {
					++i;
					while (i < a_span.size() && IsSpace(a_span[i])) {
						++i;
					}
					if (i < a_span.size() && a_span[i] == '"') {
						const auto end = a_span.find('"', i + 1);
						if (end != npos) {
							return a_span.substr(i + 1, end - i - 1);
						}
					}
				}
				pos += quoted.size();
			}
			return {};
		}

		/// Calls a_fn with every innermost {...} object in a_text. A Light Placer
		/// light entry is one of those: its "light" and "flags" fields sit side by
		/// side in an object that holds no object of its own, so this is where the
		/// two can be read together without walking a real parse tree.
		template <class F>
		void ForEachInnermostObject(std::string_view a_text, F&& a_fn)
		{
			std::vector<std::size_t> open;
			bool                     inString  = false;
			bool                     haveClose = false;
			std::size_t              lastClose = 0;

			for (std::size_t i = 0; i < a_text.size(); ++i) {
				const char c = a_text[i];
				if (inString) {
					if (c == static_cast<char>(92)) {
						++i;  // escaped character, whatever it is
					} else if (c == '"') {
						inString = false;
					}
					continue;
				}
				if (c == '"') {
					inString = true;
				} else if (c == '{') {
					open.push_back(i);
				} else if (c == '}' && !open.empty()) {
					const auto start = open.back();
					open.pop_back();
					if (!haveClose || start > lastClose) {
						a_fn(a_text.substr(start, i - start + 1));
					}
					lastClose = i;
					haveClose = true;
				}
			}
		}

		/// Light Placer accepts "A|B|C" and takes the first EditorID that resolves.
		///
		/// Each candidate is copied into a std::string before the lookup. That is not
		/// cosmetic: LookupByEditorID builds a BSFixedString from the view, and the
		/// CommonLibSSE-NG constructor hands view.data() to the engine as a C string,
		/// ignoring the length. A view into the JSON text is not null-terminated, so
		/// the key the engine hashed was the EditorID plus the rest of the file, and
		/// every lookup came back empty. A std::string ends where it should.
		RE::TESObjectLIGH* ResolveLight(std::string_view a_editorIDs)
		{
			while (!a_editorIDs.empty()) {
				const auto        bar = a_editorIDs.find('|');
				const std::string one(a_editorIDs.substr(0, bar));
				if (auto* form = RE::TESForm::LookupByEditorID<RE::TESObjectLIGH>(one)) {
					return form;
				}
				if (bar == npos) {
					break;
				}
				a_editorIDs.remove_prefix(bar + 1);
			}
			return nullptr;
		}
	}

	LightPlacerStats PatchLightPlacerConfigs()
	{
		LightPlacerStats st{};
		std::error_code  ec;

		// Relative to the game root, which is the working directory SKSE plugins run
		// in. This is the same folder Light Placer itself reads, so a mod manager's
		// virtual file system presents the same files to both.
		const std::filesystem::path root{ "Data/LightPlacer" };
		if (!std::filesystem::is_directory(root, ec)) {
			logger::info("[LightPlacer] Data/LightPlacer not present; nothing to do.");
			g_stats = st;
			return st;
		}
		st.ran = true;

		// Each EditorID is decided once, however many entries name it: the flag
		// lives on the record, so a second look could not change anything.
		std::unordered_set<std::string> seen;

		auto it = std::filesystem::recursive_directory_iterator(root, ec);
		for (; !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
			if (!it->is_regular_file(ec) || !HasJsonExtension(it->path())) {
				continue;
			}
			const auto text = ReadFile(it->path());
			if (text.empty()) {
				continue;
			}
			++st.files;

			ForEachInnermostObject(text, [&](std::string_view a_entry) {
				const auto light = StringValue(a_entry, "light");
				if (light.empty()) {
					return;
				}
				++st.entries;

				// Light Placer ORs its own flag with the record's, so an entry that
				// already asks for PortalStrict needs nothing from this plugin.
				const auto flags = StringValue(a_entry, "flags");
				if (flags.find("PortalStrict") != npos) {
					++st.strictInJson;
					return;
				}

				if (!seen.insert(std::string(light)).second) {
					return;
				}
				++st.uniqueRecords;

				auto* form = ResolveLight(light);
				if (!form) {
					++st.unresolved;
					logger::debug("[LightPlacer] no LIGH form for EditorID {}", light);
					return;
				}

				// Light Placer casts shadows from its own Shadow flag, not from the
				// record, so the entry has to be consulted as well as the form.
				if (Config::EXCLUDE_SHADOW_LIGHTS && flags.find("Shadow") != npos) {
					++st.skippedShadow;
					return;
				}

				bool blind = false;
				switch (FilterLightForm(form, light, blind)) {
				case SkipReason::kCarried: ++st.skippedCarried; return;
				case SkipReason::kShadow:  ++st.skippedShadow;  return;
				case SkipReason::kSpot:    ++st.skippedSpot;    return;
				case SkipReason::kMagic:   ++st.skippedMagic;   return;
				case SkipReason::kNone:    break;
				}

				if (SetPortalStrict(form)) {
					++st.flagged;
					logger::debug("[LightPlacer] portal-strict: [{:08X}] {}", form->GetFormID(), light);
				} else {
					++st.alreadyStrict;
				}
			});
		}

		logger::info("[LightPlacer] {} file(s), {} light entrie(s): {} already PortalStrict in JSON, "
		             "{} distinct record(s) named by the rest, {} flagged, {} already strict, "
		             "{} unresolved, skipped {} carried / {} shadow / {} spot / {} magic",
		             st.files, st.entries, st.strictInJson, st.uniqueRecords, st.flagged,
		             st.alreadyStrict, st.unresolved, st.skippedCarried, st.skippedShadow,
		             st.skippedSpot, st.skippedMagic);

		if (st.unresolved > 0 && st.flagged == 0 && st.alreadyStrict == 0) {
			// LIGH records do not keep their EditorID in the engine table on their own.
			// powerofthree Tweaks puts them there during plugin load when its
			// bLoadEditorIDs fix is on, which is also what Light Placer relies on.
			logger::warn("[LightPlacer] No EditorID resolved to a LIGH form. LIGH records only have "
			             "a runtime EditorID when powerofthree Tweaks is installed with "
			             "bLoadEditorIDs = true in po3_Tweaks.ini; Light Placer needs the same "
			             "thing to find its lights.");
		}

		g_stats = st;
		return st;
	}

	const LightPlacerStats& GetLightPlacerStats()
	{
		return g_stats;
	}
}
