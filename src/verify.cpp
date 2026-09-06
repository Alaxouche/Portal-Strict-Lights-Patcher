#include "pch.h"

#include "verify.h"

#include "config.h"
#include "hooks.h"

namespace PortalLightsRuntimePatcher
{
	namespace
	{
		constexpr auto kPortalStrict = RE::TES_LIGHT_FLAGS::kPortalStrict;

		/// A BSLight does not point back at the form it came from, so the link is
		/// rebuilt through the scene graph: the NiLight is parented under the 3D of
		/// the reference, and NiAVObject::GetUserData() carries that reference.
		/// Walks up because GenDynamic attaches the NiLight to a child node, not to
		/// the node that holds the user data.
		RE::TESObjectREFR* OwnerOf(RE::BSLight* a_light)
		{
			const auto walk = [](RE::NiAVObject* a_node) -> RE::TESObjectREFR* {
				for (auto* node = a_node; node; node = node->parent) {
					if (auto* ref = node->GetUserData()) {
						return ref;
					}
				}
				return nullptr;
			};

			if (auto* ref = walk(a_light->light.get())) {
				return ref;
			}
			return walk(a_light->objectNode.get());
		}

		/// Non-null only for a placed light bulb. A carried torch resolves to the
		/// actor holding it and a spell light to its caster, so both come back null
		/// here -- which is the same split the hooked call sites make.
		RE::TESObjectLIGH* PlacedLightOf(RE::BSLight* a_light)
		{
			auto* ref = OwnerOf(a_light);
			if (!ref) {
				return nullptr;
			}
			auto* base = ref->GetBaseObject();
			return base ? base->As<RE::TESObjectLIGH>() : nullptr;
		}

		/// Everything the classification and the portalStrict read depend on, for one
		/// light. The neighbouring booleans are the point: pointLight, ambientLight and
		/// dynamic sit immediately before portalStrict, so if all four read 1 on every
		/// light the struct is misaligned on this build and the audit is measuring a
		/// byte that means nothing. A sane spread across them says the offset is right.
		void Diagnose(RE::BSLight* a_light, std::size_t a_index)
		{
			auto* niLight = a_light->light.get();
			auto* ref     = OwnerOf(a_light);
			auto* base    = ref ? ref->GetBaseObject() : nullptr;

			logger::info(
			    "  [{}] BSLight 0x{:X} shadow={} point={} ambient={} dynamic={} strict={} "
			    "lum={:.3f} rooms={} portals={} niLight={} node={} name={}",
			    a_index,
			    reinterpret_cast<std::uintptr_t>(a_light),
			    a_light->IsShadowLight() ? 1 : 0,
			    a_light->pointLight ? 1 : 0,
			    a_light->ambientLight ? 1 : 0,
			    a_light->dynamic ? 1 : 0,
			    a_light->portalStrict ? 1 : 0,
			    a_light->luminance,
			    a_light->rooms.size(),
			    a_light->portals.size(),
			    niLight ? "yes" : "null",
			    a_light->objectNode ? "yes" : "null",
			    niLight ? niLight->name.c_str() : "-");

			// The other half of the question: what the light is actually attached to. A
			// candelabra places its light on a STAT reference, not on a LIGH one, so a
			// classifier that only accepts a LIGH base object files real placed lights
			// under temporary ones and leaves the control group empty.
			if (!ref) {
				logger::info("       no owning reference found by the parent walk");
			} else {
				logger::info("       ref [{:08X}] {} -> base {} [{:08X}] {}",
				             ref->GetFormID(),
				             ref->GetFormType(),
				             base ? "is" : "none",
				             base ? base->GetFormID() : 0,
				             base ? std::format("{}", base->GetFormType()) : std::string{});
			}
		}
		void Inspect(RE::BSLight*             a_light,
		             SceneAudit&               a_audit,
		             std::vector<std::string>& a_failures,
		             std::size_t (&a_baseTypes)[256])
		{
			++a_audit.lights;

			auto* ref  = OwnerOf(a_light);
			auto* form = ref ? ref->GetBaseObject() : nullptr;
			auto* base = form ? form->As<RE::TESObjectLIGH>() : nullptr;

			if (!base) {
				++a_audit.otherLights;
				if (a_light->portalStrict) {
					++a_audit.otherStrict;
				}
				if (ref) {
					++a_audit.otherTraced;
					// Which kinds of form these lights hang off is the whole question: a
					// candelabra STAT reads very differently from an actor carrying a torch.
					++a_baseTypes[static_cast<std::uint8_t>(form ? form->GetFormType() : ref->GetFormType())];
				} else {
					++a_audit.otherUntraced;
				}
				return;
			}

			++a_audit.refLights;

			if (a_light->portalStrict) {
				++a_audit.refStrict;
				logger::debug("scene ok  : [{:08X}] portalStrict=1", base->GetFormID());
				return;
			}

			++a_audit.refNotStrict;

			if (a_failures.size() < 10) {
				// Whether the record itself carries the flag says which half failed:
				// with it set, the load order already wanted this and the light still
				// came out unrestricted; without it, the hook is what did not fire.
				a_failures.push_back(std::format("[{:08X}] record flag={}",
				                                 base->GetFormID(),
				                                 base->data.flags.all(kPortalStrict) ? "set" : "unset"));
			}
		}
	}

	SceneAudit AuditSceneLights()
	{
		SceneAudit audit{};

		auto& state = RE::BSShaderManager::State::GetSingleton();

		// [0] is the main world scene graph; [1..3] drive menus and the race-sex
		// preview, which carry their own lights and would only add noise.
		auto* ssn = state.shadowSceneNode[0];
		if (!ssn) {
			logger::error("[AUDIT] No ShadowSceneNode yet; load a save before auditing.");
			return audit;
		}

		audit.valid    = true;
		audit.interior = state.interior;

		auto& sceneData = ssn->GetRuntimeData();

		std::size_t baseTypes[256]{};

		std::vector<std::string> failures;
		failures.reserve(10);

		for (auto& light : sceneData.activeLights) {
			if (light) {
				Inspect(light.get(), audit, failures, baseTypes);
			}
		}
		// Shadow-casting lights live in their own array and never appear in the one
		// above, so both have to be walked or every shadow light is missed.
		for (auto& light : sceneData.activeShadowLights) {
			if (light) {
				Inspect(light.get(), audit, failures, baseTypes);
			}
		}

		if (Config::DIAGNOSE_LIGHTS > 0) {
			const auto wanted = static_cast<std::size_t>(Config::DIAGNOSE_LIGHTS);
			std::size_t shown = 0;
			logger::info("--- Per-light diagnostic ---");
			for (auto& light : sceneData.activeLights) {
				if (light && shown < wanted) {
					Diagnose(light.get(), shown++);
				}
			}
			for (auto& light : sceneData.activeShadowLights) {
				if (light && shown < wanted) {
					Diagnose(light.get(), shown++);
				}
			}
		}

		const auto hooks = GetHookStats();

		logger::info("");
		logger::info("--- Scene audit ({}) ---", audit.interior ? "interior" : "exterior");
		logger::info("Hooks installed            : {}", HooksInstalled() ? "yes" : "NO");
		logger::info("Ref lights built so far    : {} (flagged {}, already strict {})",
		             hooks.refLights, hooks.flagged, hooks.alreadyStrict);
		logger::info("  skipped by filters       : {} spot, {} magic",
		             hooks.spotSkipped, hooks.magicSkipped);
		logger::info("Lights live in the renderer: {}", audit.lights);
		logger::info("  placed light bulbs       : {}", audit.refLights);
		logger::info("    portal-strict          : {}", audit.refStrict);
		logger::info("    NOT portal-strict      : {}", audit.refNotStrict);
		logger::info("  everything else          : {} ({} on a non-LIGH reference, {} with "
		             "no reference found)",
		             audit.otherLights, audit.otherTraced, audit.otherUntraced);
		logger::info("    portal-strict          : {}", audit.otherStrict);

		for (std::size_t type = 0; type < std::size(baseTypes); ++type) {
			if (baseTypes[type] > 0) {
				logger::info("      base form type {:<12} : {}",
				             std::format("{}", static_cast<RE::FormType>(type)),
				             baseTypes[type]);
			}
		}

		if (!audit.interior) {
			logger::info("Exterior cell: portal culling does not run here, so this shows the flag "
			             "travelled, not that it does anything.");
		}

		if (!HooksInstalled()) {
			logger::error("[AUDIT] The hooks were never installed, so nothing could have changed.");
		} else if (audit.refLights == 0) {
			logger::warn("[AUDIT] No placed light bulb in this scene. Stand inside a lit interior "
			             "and try again.");
		} else if (audit.refNotStrict == 0) {
			logger::info("[AUDIT] PASS: all {} placed light bulb(s) reached the renderer with "
			             "portalStrict set.",
			             audit.refLights);
		} else {
			logger::warn("[AUDIT] FAIL: {} of {} placed light bulb(s) are not portal-strict in the "
			             "renderer.",
			             audit.refNotStrict, audit.refLights);
			for (const auto& failure : failures) {
				logger::warn("  {}", failure);
			}
		}

		// ExcludeMagicLights leans on po3_Tweaks for EditorIDs. Without it the filter
		// cannot test anything, and every light it was meant to protect went through
		// unfiltered. Reported as a number rather than assumed away.
		if (Config::EXCLUDE_MAGIC_LIGHTS && hooks.blindMagic > 0) {
			logger::warn("[AUDIT] ExcludeMagicLights is ON, but {} of {} reference light(s) had "
			             "no readable EditorID, so they could not be tested and were patched "
			             "anyway. Install powerofthree Tweaks or turn the filter off knowingly.",
			             hooks.blindMagic, hooks.refLights);
		}
		// A measurement that returns true for every light in the scene is not a pass,
		// it is a broken read: either the classification never populates a control
		// group, or portalStrict is not the byte being read on this build.
		if (audit.lights > 0 && audit.refStrict + audit.otherStrict == audit.lights) {
			logger::error("[AUDIT] SUSPECT: every one of the {} lights in this scene reads "
			              "portal-strict, including the {} this plugin never touches. Treat "
			              "the PASS above as unproven and compare against a run with "
			              "Debug/DisableHooks = true.",
			              audit.lights, audit.otherLights);
		}

		// The negative half of the test, and the reason the call-site hook exists at
		// all: temporary lights must stay unrestricted. A non-zero count here is only
		// expected for records that shipped portal-strict in the load order.
		if (audit.otherStrict > 0) {
			logger::info("[AUDIT] {} of {} other light(s) are portal-strict. This plugin never "
			             "sets it on those, so the number only means something next to the "
			             "same figure from a Debug/DisableHooks run: it has to be identical.",
			             audit.otherStrict, audit.otherLights);
		}

		// On screen too: the point of this whole exercise was not having to alt-tab
		// into a log to find out whether the run was good.
		std::string summary;
		if (!HooksInstalled()) {
			summary = "Portal audit: hooks not installed. Nothing was patched.";
		} else if (audit.refLights == 0) {
			summary = "Portal audit: no placed light bulb in this scene.";
		} else {
			summary = std::format("Portal audit: {}/{} placed lights portal-strict. {}"
			                      "  |  temporary lights left alone: {}/{}",
			                      audit.refStrict,
			                      audit.refLights,
			                      audit.refNotStrict == 0 ? "PASS" : "FAIL - see log",
			                      audit.otherLights - audit.otherStrict,
			                      audit.otherLights);
		}
		RE::DebugMessageBox(summary.c_str());

		return audit;
	}

	namespace
	{
		class AuditHotkeySink : public RE::BSTEventSink<RE::InputEvent*>
		{
		public:
			static AuditHotkeySink* GetSingleton()
			{
				static AuditHotkeySink singleton;
				return &singleton;
			}

			RE::BSEventNotifyControl ProcessEvent(
				RE::InputEvent* const*                a_event,
				RE::BSTEventSource<RE::InputEvent*>*) override
			{
				if (!a_event || Config::AUDIT_HOTKEY <= 0) {
					return RE::BSEventNotifyControl::kContinue;
				}

				const auto wanted = static_cast<std::uint32_t>(Config::AUDIT_HOTKEY);

				for (auto* event = *a_event; event; event = event->next) {
					auto* button = event->AsButtonEvent();
					// IsDown() rather than IsPressed(): held keys repeat every frame and
					// would queue one message box per frame.
					if (!button || !button->IsDown()) {
						continue;
					}
					if (button->GetDevice() != RE::INPUT_DEVICE::kKeyboard) {
						continue;
					}
					if (button->GetIDCode() == wanted) {
						AuditSceneLights();
					}
				}

				return RE::BSEventNotifyControl::kContinue;
			}

		private:
			AuditHotkeySink()                                  = default;
			AuditHotkeySink(const AuditHotkeySink&)            = delete;
			AuditHotkeySink& operator=(const AuditHotkeySink&) = delete;
		};
	}

	void RegisterAuditHotkey()
	{
		if (Config::AUDIT_HOTKEY <= 0) {
			logger::info("[AUDIT] Hotkey disabled (Debug/AuditHotkey = 0).");
			return;
		}

		auto* manager = RE::BSInputDeviceManager::GetSingleton();
		if (!manager) {
			logger::error("[AUDIT] BSInputDeviceManager unavailable; the audit hotkey is dead.");
			return;
		}

		manager->AddEventSink(AuditHotkeySink::GetSingleton());
		logger::info("[AUDIT] Press DX scan code 0x{:02X} in game to audit the scene.",
		             Config::AUDIT_HOTKEY);
	}
}
