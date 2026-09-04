#pragma once

#include <cstddef>

namespace PortalLightsRuntimePatcher
{
	struct PatchStats
	{
		std::size_t total          = 0;  ///< LIGH forms walked
		std::size_t alreadyStrict  = 0;  ///< both flags already set, untouched
		std::size_t patched        = 0;  ///< at least one flag newly set
		std::size_t skippedSpot    = 0;  ///< excluded by ExcludeSpotLights
		std::size_t skippedMagic   = 0;  ///< excluded by ExcludeMagicLights
		std::size_t missingEditorID = 0; ///< EditorID unreadable, magic filter blind
	};

	/// Sets Portal-strict on every TESObjectLIGH the data handler knows about.
	/// Call once, from kDataLoaded: every plugin's forms are merged and their
	/// winning values resolved by then, and nothing has been cloned into a scene
	/// yet, so editing the base form still reaches every future instance.
	PatchStats PatchAllLights();
}
