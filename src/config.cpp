#include "pch.h"

#include "config.h"

#include <Windows.h>

namespace PortalLightsRuntimePatcher::Config
{
	bool EXCLUDE_MAGIC_LIGHTS = false;
	bool EXCLUDE_SPOT_LIGHTS  = false;
	bool ENABLE_LOGGING       = true;
	int  LOG_LEVEL            = 3;
}

namespace PortalLightsRuntimePatcher
{
	namespace
	{
		extern "C" IMAGE_DOS_HEADER __ImageBase;

		/// The INI sits next to the DLL, i.e. in SKSE/Plugins/, so it travels with
		/// the mod and resolves correctly through a mod manager's virtual file
		/// system. Falls back to the literal Data path if the module path cannot
		/// be read.
		std::filesystem::path GetIniPath()
		{
			wchar_t    buffer[MAX_PATH]{};
			const auto len = ::GetModuleFileNameW(
				reinterpret_cast<HMODULE>(&__ImageBase),
				buffer,
				static_cast<DWORD>(std::size(buffer)));

			if (len == 0 || len >= std::size(buffer)) {
				return std::filesystem::path("Data/SKSE/Plugins") /
				       std::format("{}.ini", Plugin::NAME);
			}

			auto path = std::filesystem::path(buffer, buffer + len);
			path.replace_extension(".ini");
			return path;
		}

		// SimpleIni wants one comment block per key, every line prefixed with ';'.
		constexpr auto kCommentMagic =
			";OFF by default: every light is patched, magic ones included.\n"
			";Turn on to leave magic lights alone (EditorID contains \"magic\").\n"
			";Those follow actors across room bounds, which is the case where\n"
			";portal-strict is most debatable.\n"
			";REQUIRES powerofthree's Tweaks: without po3_Tweaks.dll no light has\n"
			";a readable EditorID at runtime and this filter silently does nothing.\n"
			";The log says so explicitly when that happens.";

		constexpr auto kCommentSpot =
			";OFF by default: spotlights are patched like everything else.\n"
			";Turn on to leave them alone (Spot Light / Spot Shadow flags).\n"
			";Portal-strict on a spot can drop culled-but-visible cones.";

		constexpr auto kCommentLogging =
			";Write PortalLightsRuntimePatcher.log to\n"
			";Documents/My Games/Skyrim Special Edition/SKSE/.";

		constexpr auto kCommentLevel =
			";1=error 2=warn 3=info 4=debug 5=trace.\n"
			";Level 4 logs one line per patched light, which is thousands of lines.";
	}

	RuntimeConfig LoadRuntimeConfig()
	{
		RuntimeConfig cfg{};
		cfg.iniPath = GetIniPath();

		const auto iniPathStr = cfg.iniPath.string();

		CSimpleIniA ini;
		ini.SetUnicode();

		const auto rc = ini.LoadFile(iniPathStr.c_str());
		cfg.iniFound  = rc >= 0;

		clib_util::ini::get_value(ini, cfg.excludeMagicLights, "Filters", "ExcludeMagicLights", kCommentMagic);
		clib_util::ini::get_value(ini, cfg.excludeSpotLights, "Filters", "ExcludeSpotLights", kCommentSpot);
		clib_util::ini::get_value(ini, cfg.enableLogging, "Log", "EnableLogging", kCommentLogging);
		clib_util::ini::get_value(ini, cfg.logLevel, "Log", "LogLevel", kCommentLevel);

		if (cfg.logLevel < 1 || cfg.logLevel > 5) {
			cfg.parseWarnings.push_back(
				std::format("[CONFIG] LogLevel {} out of range, clamped to 1-5.", cfg.logLevel));
			cfg.logLevel = std::clamp(cfg.logLevel, 1, 5);
		}

		// Written back unconditionally: a first run creates a fully commented INI,
		// and a partial one gains the keys it was missing.
		if (const auto saveRc = ini.SaveFile(iniPathStr.c_str()); saveRc < 0) {
			cfg.parseWarnings.push_back(
				std::format("[CONFIG] Could not write {} (error {}).", iniPathStr, saveRc));
		}

		return cfg;
	}

	void ApplyConfig(const RuntimeConfig& a_cfg)
	{
		Config::EXCLUDE_MAGIC_LIGHTS = a_cfg.excludeMagicLights;
		Config::EXCLUDE_SPOT_LIGHTS  = a_cfg.excludeSpotLights;
		Config::ENABLE_LOGGING       = a_cfg.enableLogging;
		Config::LOG_LEVEL            = a_cfg.logLevel;
	}
}
