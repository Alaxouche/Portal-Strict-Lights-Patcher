// SKSE plugin entry point.
//
// src/pch.h is force-included by xmake (set_pcxxheader in xmake.lua), so RE/,
// REL/, SKSE/, the `logger` alias and Plugin::* are already visible here.

#include "config.h"
#include "logger.h"
#include "patcher.h"

using namespace PortalLightsRuntimePatcher;

namespace
{
	void OnMessage(SKSE::MessagingInterface::Message* a_message)
	{
		if (!a_message) {
			return;
		}

		switch (a_message->type) {
		case SKSE::MessagingInterface::kDataLoaded:
			// Every plugin's forms are merged and their winning values resolved by
			// now, and nothing has been cloned into a scene yet, so editing base
			// forms here reaches every instance the game will ever spawn.
			PatchAllLights();
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
	logger::info("[CONFIG] ExcludeMagicLights={}, ExcludeSpotLights={}, LogLevel={}",
	             cfg.excludeMagicLights,
	             cfg.excludeSpotLights,
	             cfg.logLevel);

	SKSE::Init(a_skse);

	auto* messaging = SKSE::GetMessagingInterface();
	if (!messaging || !messaging->RegisterListener("SKSE", OnMessage)) {
		logger::critical("Failed to register the SKSE message listener; nothing will be patched.");
		return false;
	}

	return true;
}
