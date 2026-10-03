#pragma once

#include <cstddef>

namespace PortalLightsRuntimePatcher
{
	/// What the Light Placer pass did. Light Placer builds its lights from JSON
	/// configs under Data/LightPlacer, each entry naming a LIGH record by EditorID,
	/// and decides Portal-strict as (its own PortalStrict flag OR the record's
	/// kPortalStrict). The reference hooks never see those lights, so this pass
	/// flags the records those configs name instead, before any of them is built.
	struct LightPlacerStats
	{
		bool        ran            = false;  ///< Data/LightPlacer existed and was scanned
		std::size_t files          = 0;      ///< JSON files read
		std::size_t entries        = 0;      ///< light entries found across them
		std::size_t strictInJson   = 0;      ///< entries already carrying PortalStrict in their flags
		std::size_t uniqueRecords  = 0;      ///< distinct EditorIDs the remaining entries name
		std::size_t unresolved     = 0;      ///< EditorIDs no LIGH form answered to
		std::size_t flagged        = 0;      ///< records given Portal-strict here
		std::size_t alreadyStrict  = 0;      ///< records the load order had already flagged
		std::size_t skippedCarried = 0;
		std::size_t skippedShadow  = 0;      ///< form shadow flags, or Shadow in the entry's own flags
		std::size_t skippedSpot    = 0;
		std::size_t skippedMagic   = 0;
	};

	/// Call once, from kDataLoaded: forms exist, Light Placer has not built
	/// anything yet (it only does so when a reference loads its 3D).
	LightPlacerStats PatchLightPlacerConfigs();

	[[nodiscard]] const LightPlacerStats& GetLightPlacerStats();
}
