#include "pch.h"

#include "hooks.h"

#include "config.h"

namespace PortalLightsRuntimePatcher
{
	namespace
	{
		constexpr auto kPortalStrict = RE::TES_LIGHT_FLAGS::kPortalStrict;

		std::atomic<std::size_t> g_refLights{ 0 };
		std::atomic<std::size_t> g_flagged{ 0 };
		std::atomic<std::size_t> g_alreadyStrict{ 0 };
		std::atomic<std::size_t> g_spotSkipped{ 0 };
		std::atomic<std::size_t> g_magicSkipped{ 0 };
		std::atomic<std::size_t> g_blindMagic{ 0 };

		bool g_installed = false;

		/// Spot lights are identified by their own flags rather than by FOV: the
		/// engine picks the shadow class from these bits, and a record can carry
		/// Spot Shadow without Spot Light.
		bool IsSpotLight(const RE::TESObjectLIGH* a_light)
		{
			return a_light->data.flags.any(
				RE::TES_LIGHT_FLAGS::kSpotlight,
				RE::TES_LIGHT_FLAGS::kSpotShadow);
		}

		/// Nothing on a LIGH record marks it as magic, so this matches the vanilla
		/// EditorID convention (MagicLightFireStormHand, ...). Spell lights are not
		/// built through these entry points in the first place, so this only has to
		/// catch a magic record placed in a cell as an ordinary reference.
		///
		/// Returns false when the EditorID cannot be read, and says so through
		/// a_blind so the caller can report it instead of silently patching.
		bool IsMagicLight(const RE::TESObjectLIGH* a_light, bool& a_blind)
		{
			// Form EditorIDs are not kept in memory by the game; this goes through the
			// po3_Tweaks export and comes back empty when that DLL is absent.
			const auto editorID = clib_util::editorID::get_editorID(a_light);
			if (editorID.empty()) {
				a_blind = true;
				return false;
			}
			return clib_util::string::icontains(editorID, "magic"sv);
		}

		/// True when the flag was set for this call and has to be put back afterwards.
		/// Portal-strict is applied to the record for the duration of one engine call
		/// and removed again, so nothing this plugin does outlives the call: the same
		/// LIGH used as a torch or a spell light elsewhere is never affected.
		bool ApplyForCall(RE::TESObjectLIGH* a_light)
		{
			g_refLights.fetch_add(1, std::memory_order_relaxed);

			if (Config::EXCLUDE_SPOT_LIGHTS && IsSpotLight(a_light)) {
				g_spotSkipped.fetch_add(1, std::memory_order_relaxed);
				return false;
			}

			if (Config::EXCLUDE_MAGIC_LIGHTS) {
				bool blind = false;
				if (IsMagicLight(a_light, blind)) {
					g_magicSkipped.fetch_add(1, std::memory_order_relaxed);
					return false;
				}
				if (blind) {
					g_blindMagic.fetch_add(1, std::memory_order_relaxed);
				}
			}

			auto& flags = a_light->data.flags;
			if (flags.all(kPortalStrict)) {
				g_alreadyStrict.fetch_add(1, std::memory_order_relaxed);
				return false;
			}

			flags.set(kPortalStrict);
			g_flagged.fetch_add(1, std::memory_order_relaxed);
			return true;
		}

		void Revert(RE::TESObjectLIGH* a_light, bool a_applied)
		{
			if (a_applied) {
				a_light->data.flags.reset(kPortalStrict);
			}
		}

		// Both entry points below are virtual members of TESObjectLIGH, so `this` is
		// always the light record being built and the reference is always a placed
		// one. Equipped torches, spell lights and hazards never arrive here: those are
		// built by the magic and equip systems, which call the light generator
		// directly without going through the base object of a reference.
		//
		// Wrapping the outer call rather than the generator itself means the flag is
		// visible for the whole of the work the engine does underneath, at any call
		// depth -- which matters because the generator is reached indirectly.

		struct Clone3DHook
		{
			static RE::NiAVObject* Thunk(RE::TESObjectLIGH* a_this, RE::TESObjectREFR* a_ref)
			{
				if (!a_this) {
					return func(a_this, a_ref);
				}

				const bool applied = ApplyForCall(a_this);
				auto*      result  = func(a_this, a_ref);
				Revert(a_this, applied);

				return result;
			}

			static inline REL::Relocation<decltype(Thunk)> func;
		};

		struct LoadGraphicsHook
		{
			static RE::NiAVObject* Thunk(RE::TESObjectLIGH* a_this, RE::TESObjectREFR* a_ref)
			{
				if (!a_this) {
					return func(a_this, a_ref);
				}

				const bool applied = ApplyForCall(a_this);
				auto*      result  = func(a_this, a_ref);
				Revert(a_this, applied);

				return result;
			}

			static inline REL::Relocation<decltype(Thunk)> func;
		};
	}

	bool InstallLightHooks()
	{
		// Nothing is written into the executable. Swapping two entries of the
		// TESObjectLIGH vtable needs no address library entry, no call site and no
		// trampoline, so there is no offset here that can be right on one build and
		// wrong on the next -- which is exactly what went wrong with the call-site
		// approach this replaces.
		if (Config::DISABLE_HOOKS) {
			logger::warn("Debug/DisableHooks is ON: nothing is hooked. An audit now shows "
			             "what the load order already had, which is the baseline the real "
			             "run has to differ from.");
			return false;
		}

		REL::Relocation<std::uintptr_t> vtable{ RE::TESObjectLIGH::VTABLE[0] };

		Clone3DHook::func      = vtable.write_vfunc(0x4A, Clone3DHook::Thunk);
		LoadGraphicsHook::func = vtable.write_vfunc(0x47, LoadGraphicsHook::Thunk);

		g_installed = true;

		logger::info("Hooked TESObjectLIGH vtable at 0x{:X}: Clone3D (0x4A) -> 0x{:X}, "
		             "LoadGraphics (0x47) -> 0x{:X}",
		             vtable.address(),
		             Clone3DHook::func.address(),
		             LoadGraphicsHook::func.address());
		logger::info("Runtime {}", REL::Module::get().version().string());

		return true;
	}

	bool HooksInstalled()
	{
		return g_installed;
	}

	HookStats GetHookStats()
	{
		return HookStats{
			g_refLights.load(std::memory_order_relaxed),
			g_flagged.load(std::memory_order_relaxed),
			g_alreadyStrict.load(std::memory_order_relaxed),
			g_spotSkipped.load(std::memory_order_relaxed),
			g_magicSkipped.load(std::memory_order_relaxed),
			g_blindMagic.load(std::memory_order_relaxed),
		};
	}
}
