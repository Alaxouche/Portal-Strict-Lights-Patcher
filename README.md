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

Both filters ship **on**. `ExcludeSpotLights` leaves spot and spot-shadow lights
alone, because portal-strict on a cone can cull something that should still be
visible. `ExcludeMagicLights` drops anything whose EditorID contains *magic*; the
entry points already keep real spell lights out, so it only covers a magic record
placed in a cell as an ordinary reference.

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

## The flag is left in place

The thunk sets `TES_LIGHT_FLAGS::kPortalStrict` on the base form just before the
original call, because that call is what generates the light and reads the flag
on the way. The engine copies it into `LIGHT_CREATE_PARAMS::portalStrict` and
`ShadowSceneNode::AddLight` stores it on the `BSLight`, where the portal graph
culls with it.

The flag is **not** taken back off. An earlier build set it, called the engine,
then restored the record. The light had been built portal-strict but the record
no longer said so, and the two disagreed: when the engine tore that light down it
walked the owning room/portal list to unlink it and dereferenced a list the record
claimed did not exist. Leaving the flag in place is what the xEdit script this
plugin replaces has always done, and that script does not crash.

## Lights it refuses to touch

Three filters exist because portal-strict is actively harmful on some lights, not
merely useless:

- **Shadow lights** (`ExcludeShadowLights`, on). ELFX Shadows and Lux place
  long-range shadow casters *outside* the room bounds so they can throw window
  shadows inward. Confining one to a room stops those shadows working.
- **Exteriors** (`PatchExteriors`, on). Room bounds only exist indoors, so the flag
  has nothing to cull against outside one. It is set anyway: the xEdit script this
  replaces flags every record regardless of where it is used, and since the flag
  lives on the record, a form first seen in an interior carries it outdoors whatever
  this setting says. Turning it off narrows exposure without undoing that.
- **Carriable lights** (`ExcludeCarriedLights`, on). A torch lying on a dungeon
  floor is a placed reference whose record is the same `DefaultTorch01` every
  carried torch uses. The flag lives on the record, so flagging it there would
  stop every torch in the game lighting the next room. `kCanCarry` on the record
  is how the engine says a light travels in someone's hand, and that is the test.
- **Spot and magic lights** (`ExcludeSpotLights`, `ExcludeMagicLights`, both on).
  Portal-strict on a cone can cull light you should still see; the magic filter
  covers a magic record placed in a cell as an ordinary reference.
Lights whose record already carries Portal-strict are counted and left alone.
## Light Placer

Light Placer builds its own lights from the JSON configs under `Data/LightPlacer`.
Each entry names a `LIGH` record by EditorID and, in `LightData::GetPortalStrict()`,
decides portal-strict as *its own `PortalStrict` flag OR the record's
`kPortalStrict`*. Those lights never pass through `Clone3D` or `LoadGraphics`, so
the reference hooks never see them.

The second half of that OR is the way in. On `kDataLoaded`, before any light
exists, the plugin scans the configs, resolves the EditorIDs the same way Light
Placer does (`TESForm::LookupByEditorID`, with the same `A|B|C` fallback), runs the
same filters, and flags the records. Entries that already carry `PortalStrict`
are left to Light Placer. No JSON library is linked: every field this pass needs
is a quoted string sitting in a flat object, and a brace scanner finds them.

Two things to know. Light Placer casts shadows from its own `Shadow` flag, not
from the record, so `ExcludeShadowLights` also reads the entry. And config packs
reuse a handful of records as generic bulbs, `MagicLightWhite01` and
`MagicLightWardHand01` above all; with `ExcludeMagicLights` on, those stay
untouched here, which is the safer default since a record-level flag cannot
honour per-entry choices on a record used a thousand different ways.

## Verifying it actually worked

Set `Debug/AuditHotkey` to a scan code — `87` is F11 — and press it in game.
It ships disabled: the audit is a diagnostic, not something a player needs bound.

The audit walks `ShadowSceneNode::activeLights` and `activeShadowLights` and
reads `BSLight::portalStrict` on each one. That boolean belongs to the engine,
not to this plugin: it is what the portal graph culls with, written by the engine
well downstream of the record the hook touched. Reading it there can disagree with
the hook rather than echo it, which is the whole point.

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
| `Filters/ExcludeMagicLights` | `true` | Skips lights whose EditorID contains `magic` |
| `Filters/ExcludeSpotLights` | `true` | Skips lights with `Spot Light` or `Spot Shadow` |
| `Filters/ExcludeShadowLights` | `true` | Skips shadow casters; they live outside room bounds |
| `Filters/ExcludeCarriedLights` | `true` | Skips lights the player or an NPC can pick up (torches) |
| `Filters/PatchExteriors` | `true` | Set to `false` to skip lights placed in exterior cells |
| `LightPlacer/PatchLightPlacer` | `true` | Flags the records named by Light Placer configs at load |
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
| `src/hooks.*` | the two vtable hooks, the filters, the flag write |
| `src/lightplacer.*` | the Light Placer config scan at load |
| `src/config.*` | INI load/regenerate, global settings |
| `src/logger.*` | spdlog setup driven by the INI |
| `src/verify.*` | the scene audit and its hotkey |

## Credits

The central idea comes from **Truman**: only reference lights should be made
portal-strict, and patching every `LIGH` base form the way an xEdit override does
hits spell and equipped lights it has no business touching.

He also traced the crash that shaped the current design. In Ghidra, the faulting
code at `0x1414A170F` on AE reaches `FUN_141509a00`, which unlinks a light from
its owning room/portal list -- on a node that has already been freed. His reading:
set Portal-strict only on lights that actually belong to a portal, or the cleanup
walks a list that was never there. That is where `PatchExteriors` comes from.

**nicola89b**, co-author of ELFX Shadows, reported that lighting overhauls place
long-range shadow lights outside the room bounds on purpose, and that confining
them breaks window shadows. That is where `ExcludeShadowLights` comes from.

**Quantumyilmaz** questioned whether restoring the flag after the original call
was safe. It was not.

Thanks also to the SKSE team, Ryan McKenzie for CommonLibSSE, CharmedBaryon for
CommonLibSSE-NG, and powerofthree for po3_Tweaks and ClibUtil.
