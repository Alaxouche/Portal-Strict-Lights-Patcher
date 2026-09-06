#include "pch.h"

#include "config.h"

#include <Windows.h>

namespace PortalLightsRuntimePatcher::Config
{
	bool EXCLUDE_MAGIC_LIGHTS = false;
	bool EXCLUDE_SPOT_LIGHTS  = false;
	bool ENABLE_LOGGING       = true;
	int  LOG_LEVEL            = 3;
	int  AUDIT_HOTKEY         = 0;
	bool DISABLE_HOOKS        = false;
	int  DIAGNOSE_LIGHTS      = 0;
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

		// SimpleIni wants one comment block per key, every line prefixed with ";".

		constexpr auto kCommentMagic =
			";OFF by default: the entry points this plugin hooks are only reached for placed\n"
			";references, so spell lights, equipped torches and hazards are already excluded\n"
			";without any name matching.\n"
			";Turn on to additionally drop any light whose EditorID contains the word magic.\n"
			";That only changes anything for a magic record placed in a cell as an ordinary\n"
			";reference, which is rare.\n"
			";REQUIRES powerofthree Tweaks: without po3_Tweaks.dll no light has a readable\n"
			";EditorID at runtime and this filter cannot test anything. The log says so once,\n"
			";with a count, rather than failing quietly.";

		constexpr auto kCommentSpot =
			";OFF by default: spotlights get Portal-strict like every other reference light.\n"
			";Turn on to leave them alone (Spot Light / Spot Shadow flags).\n"
			";Portal-strict on a spot can drop culled-but-visible cones.";

		constexpr auto kCommentLogging =
			";Write PortalLightsRuntimePatcher.log to\n"
			";Documents/My Games/Skyrim Special Edition/SKSE/.";

		constexpr auto kCommentLevel =
			";1=error 2=warn 3=info 4=debug 5=trace.\n"
			";Level 4 logs one line per light the audit walks.";

		constexpr auto kCommentHotkey =
			";DISABLED by default (0). Set a DirectX scan code to audit the lights currently\n"
			";live in the renderer and show the result on screen.\n"
			";87 = 0x57 = F11 is a good choice. Alternatives: 88 (F12), 68 (F10),\n"
			";210 (Insert), 209 (Page Down).\n"
			";This reads BSLight::portalStrict inside the ShadowSceneNode, i.e. the value the\n"
			";portal culling actually uses -- not anything this plugin wrote. Stand in a lit\n"
			";interior and press it. See DisableHooks below: one run on its own proves\n"
			";nothing, it has to be compared against a baseline.";

		constexpr auto kCommentDisable =
			";Baseline switch. ON installs nothing at all, so an audit shows what the load\n"
			";order already had. Run once with this ON and once OFF from the same save: if\n"
			";the numbers are identical, the audit is measuring nothing and the result is\n"
			";worthless either way.";

		constexpr auto kCommentDiagnose =
			";Dump this many lights per audit with every field the classification and the\n"
			";portalStrict read depend on: the neighbouring booleans, luminance, room and\n"
			";portal counts, the owning reference and its base form type. 0 is off.\n"
			";Use it when a count looks impossible rather than trusting it.";
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
		clib_util::ini::get_value(ini, cfg.auditHotkey, "Debug", "AuditHotkey", kCommentHotkey);
		clib_util::ini::get_value(ini, cfg.disableHooks, "Debug", "DisableHooks", kCommentDisable);
		clib_util::ini::get_value(ini, cfg.diagnoseLights, "Debug", "DiagnoseLights", kCommentDiagnose);

		// DirectX scan codes stop at 0xFF; anything else could never fire and would
		// otherwise look like a working hotkey that simply never triggers.
		if (cfg.auditHotkey < 0 || cfg.auditHotkey > 0xFF) {
			cfg.parseWarnings.push_back(std::format(
				"[CONFIG] AuditHotkey {} is not a DirectX scan code (0-255); audit disabled.",
				cfg.auditHotkey));
			cfg.auditHotkey = 0;
		}

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
		Config::AUDIT_HOTKEY         = a_cfg.auditHotkey;
		Config::DISABLE_HOOKS        = a_cfg.disableHooks;
		Config::DIAGNOSE_LIGHTS      = a_cfg.diagnoseLights;
	}
}
