// SKSE plugin entry point.
//
// src/pch.h is force-included by xmake (set_pcxxheader in xmake.lua), so RE/,
// REL/, SKSE/, the `logger` alias and Plugin::* are already visible here.

#include "config.h"
#include "hooks.h"
#include "logger.h"
#include "verify.h"

using namespace PortalLightsRuntimePatcher;

namespace
{
	void OnMessage(SKSE::MessagingInterface::Message* a_message)
	{
		if (!a_message) {
			return;
		}

		switch (a_message->type) {
		case SKSE::MessagingInterface::kInputLoaded:
			// The input device manager exists from here on, so this is the earliest
			// point the audit hotkey can be attached.
			RegisterAuditHotkey();
			break;

		case SKSE::MessagingInterface::kDataLoaded:
			// A plugin that quietly does nothing is worse than one that fails loudly,
			// so a refused install is put in front of the player once, on screen.
			if (!HooksInstalled() && !Config::DISABLE_HOOKS) {
				RE::DebugMessageBox(
					"Portal Lights Runtime Patcher could not verify its patch sites and "
					"installed nothing. Lights are unchanged. See the log.");
			}
			break;

		default:
			break;
		}
	}
}

extern "C" DLLEXPORT bool SKSEAPI SKSEPlugin_Load(const SKSE::LoadInterface* a_skse)
{
	REL::Module::reset();

	const RuntimeConfig cfg = LoadRuntimeConfig();
	ApplyConfig(cfg);
	SetupLog(cfg.enableLogging, cfg.logLevel);

	for (const auto& warning : cfg.parseWarnings) {
		logger::warn("{}", warning);
	}

	logger::info("{} v{} loading against runtime {}",
	             Plugin::NAME,
	             Plugin::VERSION.string(),
	             REL::Module::get().version().string());

	logger::info("[CONFIG] {} ({})",
	             cfg.iniFound ? "INI loaded" : "INI not found, defaults written",
	             cfg.iniPath.string());
	logger::info("[CONFIG] ExcludeMagicLights={}, ExcludeSpotLights={}, LogLevel={}, "
	             "AuditHotkey=0x{:02X}",
	             cfg.excludeMagicLights,
	             cfg.excludeSpotLights,
	             cfg.logLevel,
	             cfg.auditHotkey);

	SKSE::Init(a_skse);

	// Vtable swaps only: no trampoline, nothing written into the executable.
	InstallLightHooks();

	auto* messaging = SKSE::GetMessagingInterface();
	if (!messaging || !messaging->RegisterListener("SKSE", OnMessage)) {
		logger::critical("Failed to register the SKSE message listener.");
		return false;
	}

	return true;
}
