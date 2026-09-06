# Portal Lights Runtime Patcher

SKSE plugin that gives **Portal-strict** to placed light bulbs, at runtime, at
the moment the engine builds each light. No ESP, no load-order slot, no
conflicts to resolve, and it picks up whatever the load order actually resolves
to on the machine it runs on.

## What it changes, and what it deliberately does not

Portal-strict restricts a light to the room it belongs to. That is what you
want for a candle on a table. It is not what you want for a light that travels
with whoever carries it.

| Light | Reaches the hook | Gets Portal-strict |
|---|---|---|
| Placed light reference (candle, sconce, brazier) | yes | yes |
| Equipped torch | no | no |
| Spell / magic light | no | no, and filtered by name if one ever does |
| Hazard light | no | no |
| Menu / inventory light | no | no |

The split is structural rather than a name filter. `Clone3D` and `LoadGraphics`
are virtual members of `TESObjectLIGH`, so `this` is always the light record
being built and the reference is always a placed one. Equipped torches, spell
lights and hazards never arrive there: those are built by the magic and equip
systems, which reach the light generator directly without going through the base
object of a reference.

| Hook | Slot | Entered for |
|---|---|---|
| `TESObjectLIGH::Clone3D` | vtable `0x4A` | a light reference entering a cell |
| `TESObjectLIGH::LoadGraphics` | vtable `0x47` | the 3D of that reference being built |

`Filters/ExcludeMagicLights` can additionally drop anything whose EditorID
contains *magic* before the flag is set. It is off by default because the entry
points already keep spell lights out; it only changes anything for a magic
record placed in a cell as an ordinary reference.

## Nothing is written into the executable

Two entries of the `TESObjectLIGH` vtable are swapped. No address library entry,
no call site, no trampoline, no byte patched — so there is no offset here that
can be right on one build and wrong on the next.

This matters more than it sounds. An earlier version of this plugin hooked two
hand-found call sites of `TESObjectLIGH::GenDynamic`. Both offsets were wrong on
Skyrim AE 1.6.1170: a scan of `.text` found fourteen direct callers of
`GenDynamic`, and not one of them was inside `Clone3D`, `LoadGraphics` or
`TESObjectREFR::AddLight`. Those functions reach the generator indirectly, at a
depth nobody had established. Wrapping the outer virtual call sidesteps the
question entirely: the flag is visible for the whole of the work the engine does
underneath, at any call depth.

## No record is permanently modified

The thunk sets `TES_LIGHT_FLAGS::kPortalStrict` on the base form, calls the
original, and immediately clears it again. The engine reads the bit while that
call runs, copies it into `LIGHT_CREATE_PARAMS::portalStrict`, and
`ShadowSceneNode::AddLight` stores it on the `BSLight`, where the portal graph
culls with it. The result lives on the scene light; the record goes back to
exactly what the load order defined.

This matters because the same `LIGH` form can be a placed candle in one cell and
an equipped or magic light elsewhere. A permanent write would reach those uses
too and re-introduce the exact problem the entry-point split exists to avoid.

Lights whose record already carries Portal-strict are counted and left alone.
## Verifying it actually worked

Set `Debug/AuditHotkey` to a scan code — `87` is F11 — and press it in game.
It ships disabled: the audit is a diagnostic, not something a player needs bound.

The audit walks `ShadowSceneNode::activeLights` and `activeShadowLights` and
reads `BSLight::portalStrict` on each one. That boolean belongs to the engine,
not to this plugin, and since the base form is restored after every call it is
the only place the result survives. Nothing here can agree with the hook by
construction.

Each `BSLight` is traced back to its form through the scene graph: up the parents
of the `NiLight` to the node whose `GetUserData()` gives the `TESObjectREFR`,
then its base object. A `LIGH` base means a placed light bulb; everything else is
reported separately, split by whether a reference was found at all and by the
base form types involved.

The result goes to the log and to an on-screen message box.

### The audit alone is not a result

A single run can only tell you what the scene looks like now, not what this
plugin changed. Set `Debug/DisableHooks = true`, load a save in a lit interior,
press the key; then set it back to `false`, load the same save at the same spot,
press again. Same scene, same lights, one variable.

What that looks like when it works — a real pair of runs on AE 1.6.1170, 195
lights in the scene both times:

| | placed bulbs strict | placed bulbs not strict | everything else strict |
|---|---|---|---|
| `DisableHooks = true` | 17 | **7** | 171 |
| `DisableHooks = false` | 24 | 0 | 171 |

and the hook reported `flagged 7` on the second run. Both halves have to hold:

- the **target moved**: the seven records the load order left unflagged are
  exactly the seven the hook flagged
- the **control group did not**: 171 either way, so nothing leaked outside the
  placed lights

A run where every light in the scene reads portal-strict is not a pass, it is a
broken measurement — either the classification never built a control group, or
the byte being read is not `portalStrict`. The audit says `SUSPECT` and refuses
to call that a success.

### When a number looks impossible

`Debug/DiagnoseLights = N` dumps the first N lights with everything the
classification and the `portalStrict` read depend on: the three booleans that sit
immediately before it in `BSLight`, luminance, room and portal counts, the owning
reference and its base form type.

It is how the offset was confirmed rather than assumed. On a correct read,
`pointLight`, `ambientLight` and `dynamic` show a sensible spread while
`portalStrict` varies independently — and `rooms`/`portals` correlate with it,
because a portal-strict light is precisely one the engine has associated with
rooms and portals. A misaligned struct produces noise across all four.
## Configuration

`Data/SKSE/Plugins/PortalLightsRuntimePatcher.ini`, rewritten on every start so
missing keys come back with their comments.

| Key | Default | Effect |
|---|---|---|
| `Filters/ExcludeMagicLights` | `false` | Skips lights whose EditorID contains `magic` |
| `Filters/ExcludeSpotLights` | `false` | Skips lights with `Spot Light` or `Spot Shadow` |
| `Log/EnableLogging` | `true` | Writes `PortalLightsRuntimePatcher.log` |
| `Log/LogLevel` | `3` | 1=error 2=warn 3=info 4=debug 5=trace |
| `Debug/AuditHotkey` | `0` (off) | Scan code that audits the live scene |
| `Debug/DisableHooks` | `false` | Installs nothing, so an audit measures the load order |
| `Debug/DiagnoseLights` | `0` | Dump this many lights per audit in full detail |

### ExcludeMagicLights needs powerofthree's Tweaks

Nothing on a `LIGH` record marks it as magic, so the filter matches the vanilla
EditorID convention (`MagicLightFireStormHand`, ...). Form EditorIDs are **not**
kept in memory by the game: without `po3_Tweaks.dll`, `get_editorID` returns
nothing and the filter cannot evaluate anything. The audit counts those cases
and says so at `warn` level rather than failing quietly:

```
[AUDIT] ExcludeMagicLights is ON, but 214 of 214 reference light(s) had no
readable EditorID, so they could not be tested and were patched anyway.
```

This is far less severe than when the plugin patched every `LIGH` base form:
spell lights are kept out by the call-site split whether or not the filter can
read a name. The filter only guards the `AddLight` path.

## Building

```bat
xmake f -y -m release --skyrim_se=y --skyrim_ae=y
xmake build
```

Both runtime options must be set: with neither, CommonLibSSE-NG compiles as
`UNKNOWN_RUNTIME` and its layout static-asserts fail. `se` + `ae` produces the
runtime-agnostic (NG) build. For VR, use `--skyrim_vr=y` alone — it cannot be
combined with SE/AE. VR is untested here: the vtable layout of `TESObjectLIGH`
diverges from the flat runtimes, so slots `0x4A` and `0x47` want checking before
trusting a VR build.

Output lands in `SKSE/Plugins/` under the configured build directory, `.pdb`
included so CrashLoggerSSE can resolve this plugin's frames.

## Layout

| File | Role |
|---|---|
| `src/main.cpp` | SKSE entry point, hook install, message listener |
| `src/hooks.*` | the two vtable hooks, the filters, the set/restore thunk |
| `src/config.*` | INI load/regenerate, global settings |
| `src/logger.*` | spdlog setup driven by the INI |
| `src/verify.*` | the scene audit and its hotkey |

## Credits

The central idea comes from **Truman**: only reference lights should be made
portal-strict, and patching every `LIGH` base form the way an xEdit override does
hits spell and equipped lights it has no business touching. That is what this
plugin is built around.

His implementation route — hooking two hand-found call sites of `GenDynamic` —
did not survive contact with AE 1.6.1170, where neither offset pointed at a call
to `GenDynamic` at all. The vtable entry points replace it and need no offsets.
