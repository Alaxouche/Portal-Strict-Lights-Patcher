#pragma once

#include <cstddef>

namespace PortalLightsRuntimePatcher
{
	struct HookStats
	{
		std::size_t refLights     = 0;  ///< placed-light entry points entered so far
		std::size_t flagged       = 0;  ///< of those, given Portal-strict for the call
		std::size_t alreadyStrict = 0;  ///< of those, the load order had already set it
		std::size_t spotSkipped   = 0;  ///< left alone by ExcludeSpotLights
		std::size_t magicSkipped  = 0;  ///< left alone by ExcludeMagicLights
		std::size_t blindMagic    = 0;  ///< ExcludeMagicLights on, EditorID unreadable
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
