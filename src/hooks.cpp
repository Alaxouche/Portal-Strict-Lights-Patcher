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
		std::atomic<std::size_t> g_shadowSkipped{ 0 };
		std::atomic<std::size_t> g_carriedSkipped{ 0 };
		std::atomic<std::size_t> g_exteriorSkipped{ 0 };
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

		/// Shadow-casting lights, which lighting overhauls use as long-range
		/// sources deliberately placed OUTSIDE the room bounds so they can throw
		/// window shadows inward. Confining one to a room is what breaks those
		/// shadows, so ELFX Shadows and Lux want these left alone.
		bool IsShadowLight(const RE::TESObjectLIGH* a_light)
		{
			return a_light->data.flags.any(
				RE::TES_LIGHT_FLAGS::kHemiShadow,
				RE::TES_LIGHT_FLAGS::kOmniShadow,
				RE::TES_LIGHT_FLAGS::kSpotShadow);
		}

		/// Nothing on a LIGH record marks it as magic, so this matches the vanilla
		/// EditorID convention (MagicLightFireStormHand, ...). Spell lights are not
		/// built through these entry points in the first place, so this only has to
		/// catch a magic record placed in a cell as an ordinary reference.
		bool IsMagicLight(const RE::TESObjectLIGH* a_light, std::string_view a_editorID, bool& a_blind)
		{
			// Form EditorIDs are not kept in memory by the game; when the caller does
			// not already know it, this goes through the po3_Tweaks export and comes
			// back empty when that DLL is absent.
			const std::string editorID = a_editorID.empty()
			                                 ? clib_util::editorID::get_editorID(a_light)
			                                 : std::string(a_editorID);
			if (editorID.empty()) {
				a_blind = true;
				return false;
			}
			return clib_util::string::icontains(editorID, "magic"sv);
		}

		/// Room bounds and portals only exist in interiors. Portal-strict on a light
		/// with no room to belong to is not merely useless: when the engine later
		/// tears that light down it walks the owning room/portal list to unlink it,
		/// and a list that never existed is a null dereference. That is the crash
		/// this filter exists to avoid, not a performance tweak.
		bool IsInsideRoomBounds(const RE::TESObjectREFR* a_ref)
		{
			const auto* cell = a_ref->GetParentCell();
			return cell && cell->IsInteriorCell();
		}

		/// A light the player or an NPC can pick up is, by definition, one that
		/// travels through doorways in someone's hand. Torches are the obvious case.
		/// Since the flag lives on the record, a torch lying on a dungeon floor is a
		/// placed reference whose record is the same DefaultTorch01 every carried
		/// torch uses: flag it there and every torch in the game stops lighting the
		/// next room. The record knows it is carriable, so the record is where to
		/// stop that.
		bool IsCarriedLight(const RE::TESObjectLIGH* a_light)
		{
			return a_light->CanBeCarried();
		}

		/// Per-call bookkeeping for the reference hooks. The actual decision is
		/// FilterLightForm, shared with the Light Placer pass.
		void ApplyForCall(RE::TESObjectLIGH* a_light, RE::TESObjectREFR* a_ref)
		{
			g_refLights.fetch_add(1, std::memory_order_relaxed);

			if (!Config::PATCH_EXTERIORS && !IsInsideRoomBounds(a_ref)) {
				g_exteriorSkipped.fetch_add(1, std::memory_order_relaxed);
				return;
			}

			bool blind = false;
			switch (FilterLightForm(a_light, {}, blind)) {
			case SkipReason::kCarried:
				g_carriedSkipped.fetch_add(1, std::memory_order_relaxed);
				return;
			case SkipReason::kShadow:
				g_shadowSkipped.fetch_add(1, std::memory_order_relaxed);
				return;
			case SkipReason::kSpot:
				g_spotSkipped.fetch_add(1, std::memory_order_relaxed);
				return;
			case SkipReason::kMagic:
				g_magicSkipped.fetch_add(1, std::memory_order_relaxed);
				return;
			case SkipReason::kNone:
				break;
			}
			if (blind) {
				g_blindMagic.fetch_add(1, std::memory_order_relaxed);
			}

			// The flag is NOT taken back off afterwards, and that is deliberate. An
			// earlier build set it, called the engine, then restored the record. The
			// light was built portal-strict but the record no longer said so, so the
			// teardown path and the creation path disagreed about what the light was.
			// Leaving it in place is what the xEdit script this plugin replaces has
			// always done, and that script does not crash.
			if (SetPortalStrict(a_light)) {
				g_flagged.fetch_add(1, std::memory_order_relaxed);
			} else {
				g_alreadyStrict.fetch_add(1, std::memory_order_relaxed);
			}
		}
		// Both entry points below are virtual members of TESObjectLIGH, so `this` is
		// always the light record being built and the reference is always a placed
		// one. Equipped torches, spell lights and hazards never arrive here: those
		// are built by the magic and equip systems, which reach the light generator
		// directly without going through the base object of a reference.
		//
		// The flag has to be set BEFORE the original call, because that call is what
		// generates the light and reads the flag on the way.

		struct Clone3DHook
		{
			static RE::NiAVObject* Thunk(RE::TESObjectLIGH* a_this, RE::TESObjectREFR* a_ref)
			{
				if (a_this && a_ref) {
					ApplyForCall(a_this, a_ref);
				}
				return func(a_this, a_ref);
			}

			static inline REL::Relocation<decltype(Thunk)> func;
		};

		struct LoadGraphicsHook
		{
			static RE::NiAVObject* Thunk(RE::TESObjectLIGH* a_this, RE::TESObjectREFR* a_ref)
			{
				if (a_this && a_ref) {
					ApplyForCall(a_this, a_ref);
				}
				return func(a_this, a_ref);
			}

			static inline REL::Relocation<decltype(Thunk)> func;
		};
	}

	SkipReason FilterLightForm(RE::TESObjectLIGH* a_light, std::string_view a_editorID, bool& a_blind)
	{
		if (Config::EXCLUDE_CARRIED_LIGHTS && IsCarriedLight(a_light)) {
			return SkipReason::kCarried;
		}
		if (Config::EXCLUDE_SHADOW_LIGHTS && IsShadowLight(a_light)) {
			return SkipReason::kShadow;
		}
		if (Config::EXCLUDE_SPOT_LIGHTS && IsSpotLight(a_light)) {
			return SkipReason::kSpot;
		}
		if (Config::EXCLUDE_MAGIC_LIGHTS && IsMagicLight(a_light, a_editorID, a_blind)) {
			return SkipReason::kMagic;
		}
		return SkipReason::kNone;
	}

	bool SetPortalStrict(RE::TESObjectLIGH* a_light)
	{
		auto& flags = a_light->data.flags;
		if (flags.all(kPortalStrict)) {
			return false;
		}
		flags.set(kPortalStrict);
		return true;
	}

	bool InstallLightHooks()
	{
		if (Config::DISABLE_HOOKS) {
			logger::warn("Debug/DisableHooks is ON: nothing is hooked. An audit now shows "
			             "what the load order already had, which is the baseline the real "
			             "run has to differ from.");
			return false;
		}

		// Nothing is written into the executable. Swapping two entries of the
		// TESObjectLIGH vtable needs no address library entry, no call site and no
		// trampoline, so there is no offset here that can be right on one build and
		// wrong on the next.
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
			g_shadowSkipped.load(std::memory_order_relaxed),
			g_carriedSkipped.load(std::memory_order_relaxed),
			g_exteriorSkipped.load(std::memory_order_relaxed),
			g_blindMagic.load(std::memory_order_relaxed),
		};
	}
}
