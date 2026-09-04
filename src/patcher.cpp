#include "pch.h"

#include "patcher.h"

#include "config.h"

namespace PortalLightsRuntimePatcher
{
	namespace
	{
		// Portal-strict lives in TWO places on a LIGH record, and the Creation Kit
		// writes both. Both are set here so the result is byte-identical to what
		// an xEdit override would produce:
		//
		//   DATA\Flags\Portal-strict                 -> TES_LIGHT_FLAGS::kPortalStrict          (1 << 13)
		//   Record Header\Record Flags\Portal-strict -> TESObjectLIGH::RecordFlags::kPortalStrict (1 << 17)
		//
		// The DATA flag is the functional one the engine reads when culling lights
		// against room bounds; the record-header flag is CK parity.

		constexpr auto kDataPortalStrict = RE::TES_LIGHT_FLAGS::kPortalStrict;
		constexpr auto kRecordPortalStrict =
			static_cast<std::uint32_t>(RE::TESObjectLIGH::RecordFlags::kPortalStrict);

		bool HasDataFlag(const RE::TESObjectLIGH* a_light)
		{
			return a_light->data.flags.all(kDataPortalStrict);
		}

		bool HasRecordFlag(const RE::TESObjectLIGH* a_light)
		{
			return (a_light->GetFormFlags() & kRecordPortalStrict) != 0;
		}

		/// Spot lights are identified by their own flags rather than by FOV: the
		/// engine picks the shadow class from these bits, and a record can carry
		/// Spot Shadow without Spot Light.
		bool IsSpotLight(const RE::TESObjectLIGH* a_light)
		{
			return a_light->data.flags.any(
				RE::TES_LIGHT_FLAGS::kSpotlight,
				RE::TES_LIGHT_FLAGS::kSpotShadow);
		}

		bool ShouldLog(spdlog::level::level_enum a_level)
		{
			auto* log = spdlog::default_logger_raw();
			return log && log->should_log(a_level);
		}
	}

	PatchStats PatchAllLights()
	{
		PatchStats stats{};

		// kDataLoaded fires once, but a second entry would double-count the stats
		// and re-report a pass that already happened.
		static bool alreadyRan = false;
		if (std::exchange(alreadyRan, true)) {
			logger::warn("PatchAllLights called twice; ignoring the second call.");
			return stats;
		}

		auto* dataHandler = RE::TESDataHandler::GetSingleton();
		if (!dataHandler) {
			logger::error("TESDataHandler is unavailable; no light was patched.");
			return stats;
		}

		const auto& lights = dataHandler->GetFormArray<RE::TESObjectLIGH>();

		// Hoisted out of the loop: these are globals in another translation unit,
		// so the compiler cannot prove the opaque calls below leave them alone and
		// would otherwise reload them on every iteration.
		const bool excludeMagic = Config::EXCLUDE_MAGIC_LIGHTS;
		const bool excludeSpot  = Config::EXCLUDE_SPOT_LIGHTS;

		// get_editorID goes through po3_Tweaks' exported lookup and returns a fresh
		// std::string, so it is only worth paying for when something actually reads
		// it. With both filters off and the log at info, that is never.
		const bool wantEditorID = excludeMagic || ShouldLog(spdlog::level::debug);

		logger::info("Walking {} LIGH form(s)...", lights.size());

		for (auto* light : lights) {
			if (!light) {
				continue;
			}

			++stats.total;

			const bool hasData   = HasDataFlag(light);
			const bool hasRecord = HasRecordFlag(light);

			if (hasData && hasRecord) {
				++stats.alreadyStrict;
				continue;
			}

			if (excludeSpot && IsSpotLight(light)) {
				++stats.skippedSpot;
				logger::trace("skip spot   : [{:08X}]", light->GetFormID());
				continue;
			}

			// Fetched at most once per light, and only when it will be read.
			const std::string editorID =
				wantEditorID ? clib_util::editorID::get_editorID(light) : std::string{};

			if (excludeMagic) {
				// Nothing on a LIGH record marks it as "magic", so this matches the
				// vanilla EditorID convention (MagicLightFireStormHand, ...), exactly
				// like the xEdit script. An empty EditorID means the filter could not
				// be evaluated at all -- counted and reported, never silently ignored.
				if (editorID.empty()) {
					++stats.missingEditorID;
				} else if (clib_util::string::icontains(editorID, "magic"sv)) {
					++stats.skippedMagic;
					logger::trace("skip magic  : [{:08X}] {}", light->GetFormID(), editorID);
					continue;
				}
			}

			if (!hasData) {
				light->data.flags.set(kDataPortalStrict);
			}
			if (!hasRecord) {
				light->formFlags |= kRecordPortalStrict;
			}

			++stats.patched;
			logger::debug("portal-strict: [{:08X}] {}", light->GetFormID(), editorID);
		}

		// Verification pass. The loop above trusts its own writes; this re-reads
		// every form and counts what is actually set now, so a write that did not
		// stick shows up as a number instead of being assumed away.
		std::size_t              verified = 0;
		std::vector<std::string> samples;
		samples.reserve(5);

		for (auto* light : lights) {
			if (!light) {
				continue;
			}
			if (HasDataFlag(light) && HasRecordFlag(light)) {
				++verified;
				if (samples.size() < 5) {
					auto editorID = clib_util::editorID::get_editorID(light);
					samples.push_back(std::format("[{:08X}]{}{}",
					                              light->GetFormID(),
					                              editorID.empty() ? "" : " ",
					                              editorID));
				}
			}
		}

		logger::info("--- Summary ---");
		logger::info("LIGH forms walked      : {}", stats.total);
		logger::info("Patched                : {}", stats.patched);
		logger::info("Already portal-strict  : {}", stats.alreadyStrict);
		logger::info("Spotlights skipped     : {}", stats.skippedSpot);
		logger::info("Magic lights skipped   : {}", stats.skippedMagic);
		logger::info("");

		const auto expected = stats.patched + stats.alreadyStrict;
		if (verified == expected) {
			logger::info("VERIFIED: {} light(s) now carry both Portal-strict flags.", verified);
		} else {
			logger::error("VERIFICATION MISMATCH: expected {} light(s) to carry both "
			              "Portal-strict flags, found {}. Some writes did not stick.",
			              expected, verified);
		}

		if (!samples.empty()) {
			std::string joined;
			for (const auto& sample : samples) {
				if (!joined.empty()) {
					joined += ", ";
				}
				joined += sample;
			}
			logger::info("Spot-check these in game: {}", joined);
		}

		if (excludeMagic && stats.missingEditorID > 0) {
			logger::warn("");
			logger::warn("ExcludeMagicLights is ON, but {} of {} lights had no readable "
			             "EditorID, so they could not be tested and were patched anyway.",
			             stats.missingEditorID, stats.total);
			logger::warn("Light EditorIDs only exist at runtime when powerofthree's Tweaks "
			             "(po3_Tweaks.dll) is installed. Without it this filter cannot work.");
		}

		return stats;
	}
}
