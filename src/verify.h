#pragma once

#include <cstddef>

namespace PortalLightsRuntimePatcher
{
	/// What the renderer actually holds, not what the hook believes it wrote.
	///
	/// The hook sets Portal-strict on the base form only for the duration of one
	/// Clone3D or LoadGraphics call. Somewhere under that call the engine reads
	/// the bit and stores it on the BSLight, which is what the portal graph culls
	/// with. Since the form is restored as soon as the call returns, the BSLight
	/// is the only place the result survives -- and it is also the place the
	/// engine actually reads. Nothing here can agree with the hook by
	/// construction.
	struct SceneAudit
	{
		bool valid    = false;  ///< the ShadowSceneNode was reachable
		bool interior = false;  ///< portal culling only ever runs in interiors

		std::size_t lights = 0;  ///< BSLight + BSShadowLight live in the scene

		/// Lights whose owning reference has a LIGH base object, i.e. placed light
		/// bulbs -- exactly the population the hooked call sites cover.
		std::size_t refLights    = 0;
		std::size_t refStrict    = 0;  ///< of those, portal-strict in the renderer
		std::size_t refNotStrict = 0;  ///< of those, not -- these are the failures

		/// Everything else: spell lights, equipped torches, hazards, sun, cloud
		/// light. The hook deliberately never touches these.
		std::size_t otherLights = 0;
		std::size_t otherStrict = 0;  ///< strict anyway; only the baseline run can say whether that is normal

		/// The second group split by why it landed there, because "not a placed bulb"
		/// covers two very different things and lumping them hid which one was in play.
		std::size_t otherTraced   = 0;  ///< an owning reference was found, its base is not a LIGH
		std::size_t otherUntraced = 0;  ///< the parent walk found no reference at all
	};

	/// Walks the active lights of the main ShadowSceneNode and reports.
	/// Safe to call at any time; returns valid == false if there is no scene yet.
	SceneAudit AuditSceneLights();

	/// Installs the input sink that runs AuditSceneLights on Config::AUDIT_HOTKEY.
	/// No-op when the hotkey is 0. Call from kInputLoaded.
	void RegisterAuditHotkey();
}
