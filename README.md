# Portal Lights Runtime Patcher

SKSE plugin that sets **Portal-strict** on every `LIGH` form in the load order,
at runtime, once per session. Same result as an xEdit override plugin — no ESP,
no load-order slot, no conflicts to resolve, and it picks up whatever the load
order actually resolves to on the machine it runs on.

## What it changes

A LIGH record carries Portal-strict in two places, and the Creation Kit writes
both. The plugin sets both, so the result matches a hand-made override exactly:

| Location | Constant | Bit |
|---|---|---|
| `DATA\Flags\Portal-strict` | `RE::TES_LIGHT_FLAGS::kPortalStrict` | `1 << 13` |
| `Record Header\Record Flags\Portal-strict` | `RE::TESObjectLIGH::RecordFlags::kPortalStrict` | `1 << 17` |

The DATA flag is the functional one the engine reads when culling lights against
room bounds. The record-header flag is CK parity.

Lights that already have both are left untouched.

## When it runs

On `SKSE::MessagingInterface::kDataLoaded`. Every plugin's forms are merged and
their winning values resolved by then, and nothing has been cloned into a scene
yet, so editing the base forms reaches every instance the game will ever spawn.

## Configuration

`Data/SKSE/Plugins/PortalLightsRuntimePatcher.ini`, rewritten on every start so
missing keys come back with their comments.

Nothing is excluded out of the box: every light gets the flag. The two filters
exist for tuning if a specific light type misbehaves.

| Key | Default | Effect |
|---|---|---|
| `Filters/ExcludeMagicLights` | `false` | Skips lights whose EditorID contains `magic` |
| `Filters/ExcludeSpotLights` | `false` | Skips lights with `Spot Light` or `Spot Shadow` |
| `Log/EnableLogging` | `true` | Writes `PortalLightsRuntimePatcher.log` |
| `Log/LogLevel` | `3` | 1=error 2=warn 3=info 4=debug 5=trace |

### ExcludeMagicLights needs powerofthree's Tweaks

Nothing on a LIGH record marks it as "magic", so the filter matches the vanilla
EditorID convention (`MagicLightFireStormHand`, `MagicLightShockHand`, …).

Form EditorIDs are **not** kept in memory by the game. Without `po3_Tweaks.dll`
installed, `get_editorID` returns nothing for lights and this filter cannot
evaluate anything — every magic light would be patched. The plugin counts those
cases and says so explicitly at `warn` level rather than failing quietly:

```
ExcludeMagicLights is ON, but 1423 of 1517 lights had no readable EditorID,
so they could not be tested and were patched anyway.
```

If you see that line, either install po3_Tweaks or set `ExcludeMagicLights = false`
knowingly.

## Building

```bat
xmake f -y -m release --skyrim_se=y --skyrim_ae=y
xmake build
```

Both runtime options must be set: with neither, CommonLibSSE-NG compiles as
`UNKNOWN_RUNTIME` and its layout static-asserts fail. `se` + `ae` produces the
runtime-agnostic (NG) build. For VR, use `--skyrim_vr=y` alone — it cannot be
combined with SE/AE.

Output lands in `SKSE/Plugins/` under the configured build directory, `.pdb`
included so CrashLoggerSSE can resolve this plugin's frames.

## Layout

| File | Role |
|---|---|
| `src/main.cpp` | SKSE entry point, message listener |
| `src/config.*` | INI load/regenerate, global settings |
| `src/logger.*` | spdlog setup driven by the INI |
| `src/patcher.*` | the form walk and the flag writes |
| `src/keyhandler/` | unused leftover from the project template |
