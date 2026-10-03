#pragma once

#include <cstddef>
#include <string_view>

namespace PortalLightsRuntimePatcher
{
	/// Why a light record was left alone. kNone means it was flagged (or already was).
	enum class SkipReason
	{
		kNone,
		kCarried,
		kShadow,
		kSpot,
		kMagic,
	};

	/// The form-level filters, shared by the reference hooks and the Light Placer
	/// pass so both paths make the same decision for the same record.
	/// a_editorID may be empty: the magic test then goes through po3_Tweaks and
	/// reports through a_blind when nothing could be read.
	SkipReason FilterLightForm(RE::TESObjectLIGH* a_light, std::string_view a_editorID, bool& a_blind);

	/// Sets Portal-strict on the record. Returns false when it was already there.
	bool SetPortalStrict(RE::TESObjectLIGH* a_light);

	struct HookStats
	{
		std::size_t refLights        = 0;  ///< placed-light entry points entered so far
		std::size_t flagged          = 0;  ///< of those, given Portal-strict
		std::size_t alreadyStrict    = 0;  ///< of those, the flag was already there
		std::size_t spotSkipped      = 0;  ///< left alone by ExcludeSpotLights
		std::size_t magicSkipped     = 0;  ///< left alone by ExcludeMagicLights
		std::size_t shadowSkipped    = 0;  ///< left alone by ExcludeShadowLights
		std::size_t carriedSkipped   = 0;  ///< left alone by ExcludeCarriedLights
		std::size_t exteriorSkipped  = 0;  ///< outside an interior, so outside any room bound
		std::size_t blindMagic       = 0;  ///< ExcludeMagicLights on, EditorID unreadable
	};

	/// Swaps the TESObjectLIGH vtable entries for Clone3D and LoadGraphics, the
	/// two virtual entry points only reached when the engine builds the 3D of a
	/// placed light reference. Nothing is written into the executable, so there
	/// is no patch site to validate and no trampoline to allocate.
	/// Returns false when Debug/DisableHooks asked for a baseline run.
	bool InstallLightHooks();

	[[nodiscard]] bool      HooksInstalled();
	[[nodiscard]] HookStats GetHookStats();
}
