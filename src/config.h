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

		/// DX scan code of the key that runs the scene audit in game. 0, the default,
		/// disables it: the audit is a diagnostic, not something a player needs bound.
		int auditHotkey = 0;

		/// Baseline switch: installs nothing, so an audit measures the load order
		/// rather than this plugin. The only way to tell a working patch from a
		/// broken measurement without rebuilding.
		bool disableHooks = false;

		/// Dump this many lights per audit with every field the classification and
		/// the portalStrict read depend on. 0 is off.
		int diagnoseLights = 0;

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
	extern int  AUDIT_HOTKEY;
	extern bool DISABLE_HOOKS;
	extern int  DIAGNOSE_LIGHTS;
}
