#include "pch.h"

#include "config.h"

#include <Windows.h>

namespace PortalLightsRuntimePatcher::Config
{
	bool EXCLUDE_MAGIC_LIGHTS = true;
	bool EXCLUDE_SPOT_LIGHTS  = true;
	bool EXCLUDE_SHADOW_LIGHTS = true;
	bool EXCLUDE_CARRIED_LIGHTS = true;
	bool PATCH_EXTERIORS       = true;
	bool PATCH_LIGHT_PLACER    = true;
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
			";ON by default: any light whose EditorID contains the word magic is left alone.\n"
			";Real spell lights never reach this plugin anyway, so this only covers a magic\n"
			";record that was placed in a cell as an ordinary reference.\n"
			";Turn off to treat those like any other placed light.\n"
			";REQUIRES powerofthree Tweaks: without po3_Tweaks.dll no light has a readable\n"
			";EditorID at runtime and this filter cannot test anything, so nothing gets\n"
			";excluded. The audit says so with a count rather than failing quietly.";

		constexpr auto kCommentSpot =
			";ON by default: spotlights are left alone (Spot Light / Spot Shadow flags).\n"
			";Portal-strict on a spot can drop a cone that should still be visible.\n"
			";Turn off to give them the flag like every other placed light.";

		constexpr auto kCommentShadow =
			";ON by default: shadow-casting lights are left alone (Hemi/Omni/Spot Shadow).\n"
			";Lighting overhauls such as ELFX Shadows and Lux use long-range shadow lights\n"
			";placed OUTSIDE the room bounds on purpose, so they can throw window shadows\n"
			";inward. Confining one to a room stops those shadows working.\n"
			";Reported by nicola89b, co-author of ELFX Shadows. Turn off only if you know\n"
			";your lighting mod does not rely on them.";

		constexpr auto kCommentCarried =
			";ON by default: lights the player or an NPC can pick up are left alone.\n"
			";A carriable light travels through doorways in someone hand, torches above\n"
			";all. The flag lives on the record, and a torch lying on a dungeon floor uses\n"
			";the same DefaultTorch01 record as every carried torch: flag it there and every\n"
			";torch in the game stops lighting the next room. Turn off only if you know\n"
			";what you are doing.";

		constexpr auto kCommentExterior =
			";ON by default: exterior cells are patched like interiors.\n"
			";Room bounds and portals only exist indoors, so the flag has nothing to cull\n"
			";against outside one. It is set anyway for two reasons: the xEdit script this\n"
			";plugin replaces has always flagged every record regardless of where it is used,\n"
			";and the flag lives on the record, so a form first seen in an interior carries it\n"
			";outdoors whatever this setting says.\n"
			";Turn off to skip lights whose reference sits in an exterior cell. That narrows\n"
			";exposure but cannot unflag a record already caught indoors.";

		constexpr auto kCommentLightPlacer =
			";ON by default: records named by Light Placer configs get the flag too.\n"
			";Light Placer builds its own lights from the JSON files under Data/LightPlacer,\n"
			";each entry naming a LIGH record by EditorID, and makes a light portal-strict\n"
			";when its entry says PortalStrict OR the record carries the flag. Those lights\n"
			";never pass through the engine entry points this plugin hooks, so this pass\n"
			";reads the configs at load and flags the records they name, before any light\n"
			";is built. Entries already marked PortalStrict are left to Light Placer.\n"
			";The same filters apply: carried, shadow (form flags or Shadow in the entry),\n"
			";spot and magic. Note that many configs reuse MagicLightWhite01 and similar\n"
			";records as generic bulbs: with ExcludeMagicLights on those are skipped here.";

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
		clib_util::ini::get_value(ini, cfg.excludeShadowLights, "Filters", "ExcludeShadowLights", kCommentShadow);
		clib_util::ini::get_value(ini, cfg.excludeCarriedLights, "Filters", "ExcludeCarriedLights", kCommentCarried);
		clib_util::ini::get_value(ini, cfg.patchExteriors, "Filters", "PatchExteriors", kCommentExterior);
		clib_util::ini::get_value(ini, cfg.patchLightPlacer, "LightPlacer", "PatchLightPlacer", kCommentLightPlacer);
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
		Config::EXCLUDE_SHADOW_LIGHTS = a_cfg.excludeShadowLights;
		Config::EXCLUDE_CARRIED_LIGHTS = a_cfg.excludeCarriedLights;
		Config::PATCH_EXTERIORS       = a_cfg.patchExteriors;
		Config::PATCH_LIGHT_PLACER    = a_cfg.patchLightPlacer;
		Config::ENABLE_LOGGING       = a_cfg.enableLogging;
		Config::LOG_LEVEL            = a_cfg.logLevel;
		Config::AUDIT_HOTKEY         = a_cfg.auditHotkey;
		Config::DISABLE_HOOKS        = a_cfg.disableHooks;
		Config::DIAGNOSE_LIGHTS      = a_cfg.diagnoseLights;
	}
}
