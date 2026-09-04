#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace PortalLightsRuntimePatcher
{
	struct RuntimeConfig
	{
		bool excludeMagicLights = false;
		bool excludeSpotLights  = false;

		bool enableLogging = true;
		int  logLevel      = 3;

		bool                     iniFound = false;
		std::filesystem::path    iniPath;
		std::vector<std::string> parseWarnings;
	};

	/// Reads <dll folder>/PortalLightsRuntimePatcher.ini, then writes it back so a
	/// missing or partial file is regenerated complete and commented.
	RuntimeConfig LoadRuntimeConfig();

	void ApplyConfig(const RuntimeConfig& a_cfg);
}

namespace PortalLightsRuntimePatcher::Config
{
	extern bool EXCLUDE_MAGIC_LIGHTS;
	extern bool EXCLUDE_SPOT_LIGHTS;
	extern bool ENABLE_LOGGING;
	extern int  LOG_LEVEL;
}
