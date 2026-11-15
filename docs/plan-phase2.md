# Spectra phase 2: plan

Four features, built in parallel by four agents, each on its own branch and
PR. The scaffold branch (`phase2-scaffold`) lays down every shared piece
first, so the agents only touch files they own.

| Task | What it adds |
|---|---|
| presets | Named looks, a hover menu on the corner sparkle, number keys |
| wallpaper | `spectra --wallpaper`: the visualizer on a layer under every window |
| visuals | Layouts: mirrored, radial, stereo, waveform |
| capture | Pause when silent so the device can sleep, capture one app, auto Bluetooth delay |

Read `docs/plan.md` first for how the app works (data flow, conventions).

## What the scaffold already did

- **Settings**: every new key is parsed, validated and tested
  (`tests/settings_tests.cpp`):
  - `[look] layout` (bars, mirrored, radial, stereo, waveform) with a saving
    setter, `Settings::Layouts`.
  - `[analysis] delay=auto` (the new default): `autoDelay()` is true and
    `tuning().delaySeconds` is 0; `main.cpp` substitutes
    `Capture::outputLatency()`.
  - `[capture] target`, `sleep_after` → `config::Capture`.
  - `[wallpaper] height, opacity, transparent, layer, monitor` →
    `config::Wallpaper`.
- **Interfaces with stubs**: `Capture::setTarget/setSleepAfter/sleeping/
  outputLatency`, `Spectrum::levelsLeft/levelsRight/waveform/setLayout`,
  `Presets` (`src/presets.h`), `wallpaper::` (`src/wallpaper.h`),
  `src/streams.{h,cpp}` (empty home for the capture task).
- **main.cpp** wires all of it: settings changes flow to capture
  (target, sleep) and spectrum (tuning, layout, auto delay); `presets` and
  `wallpaperMode` are QML context properties; `--wallpaper` calls
  `wallpaper::prepareProcess()` before the app and `wallpaper::attach()`
  after QML loads.
- **QML**: `Main.qml` is a shell: a `scene` (glow + `Visualizer`) that in
  wallpaper mode fills `wallpaperHeight` of the screen at
  `wallpaperOpacity`, a `CornerMenu` hidden in wallpaper mode, the error
  plate, help, shortcuts. `Visualizer.qml` loads the layout file named by
  `settings.layout`. Layout files and `CornerMenu.qml` are stubs.
- **Keys**: Ctrl+Q quits (was Q), L cycles layouts, 1-9 call
  `presets.applyIndex(n - 1)`. Help overlay and README updated.
- **Build**: all new sources are in both `.pro` files, `-lLayerShellQtInterface`
  is linked, PKGBUILD depends on `layer-shell-qt`, test suites
  `presets_tests.cpp`, `streams_tests.cpp`, `wallpaper_tests.cpp` exist as
  placeholders and run from `tests/main.cpp`.

## Ownership

Public interfaces in headers are fixed. Add private members, private
helpers and new free functions freely; change a public signature only if you
must, and say so in the PR description.

| Task | Owns |
|---|---|
| presets | `src/presets.{h,cpp}`, `src/CornerMenu.qml`, `src/GlowButton.qml`, `tests/presets_tests.cpp`; may **add** to `src/settings.{h,cpp}` (a multi-key write, for example) without changing existing behavior; may add QML files to the "presets task" block of `resources.qrc` |
| wallpaper | `src/wallpaper.{h,cpp}`, `tests/wallpaper_tests.cpp`, `pkgbuild/` (a second desktop entry, PKGBUILD install lines) |
| visuals | `src/dsp.{h,cpp}`, `src/spectrum.{h,cpp}`, `src/Visualizer.qml`, `src/SpectrumBars.qml`, `src/{Mirrored,Radial,Stereo,Waveform}Layout.qml`, `tests/dsp_tests.cpp`; may add QML/JS files to the "visuals task" block of `resources.qrc` |
| capture | `src/capture.{h,cpp}`, `src/streams.{h,cpp}`, `tests/capture_tests.cpp`, `tests/streams_tests.cpp` |

Nobody edits `README.md`, `main.cpp`, `Main.qml`, `HelpOverlay.qml`, the
`.pro` files or `docs/`. If you need a change there, describe it in the PR
description and the lead makes it after merging. Put user-facing docs (new
keys, how to use the feature) in the PR description; the lead folds them
into the README.

## Task: presets

- **Built-ins**, compiled in, look keys only (never layout or tuning, so a
  preset never changes how the analysis behaves):
  - Rainbow: today's defaults.
  - Neon: magenta → violet → cyan, glow on.
  - Sunset: deep purple → magenta → orange → yellow.
  - Ocean: navy → teal → aqua, vertical mode.
  - Fire: dark red → orange → yellow, vertical mode, glow on.
  - Mono: greys to white, no peaks.
- **User presets**: `~/.config/spectra/presets/<name>.conf`, partial INI
  with `[look]` (minus `layout`) and `[analysis]`. `saveCurrent(name)`
  writes the current values atomically; names are filenames, so reject
  `/`, leading dots and empty names. A user preset can't take a built-in's
  name. `remove` deletes user presets only.
- **Applying** writes the preset's keys into `spectra.conf` in one atomic
  write (add a multi-key method to Settings for that, reusing its
  comment-preserving line editing), so the file always shows what's on
  screen. Keys the preset doesn't set stay as they are.
- **current** is the last applied preset, cleared when a settings change
  makes the config stop matching it.
- **Hover menu** (`CornerMenu.qml`): the sparkle stays where it is and
  still toggles glow on click. Hovering the corner (sparkle or panel) opens
  a small dark panel below it (`#e6101010`, small radius, Material dark):
  - a row of toggles: glow, peaks, color mode, layout (shows the current
    one; click cycles), each with its key in a tooltip;
  - preset swatches: small gradient chips from each preset's colors, the
    current one outlined, a 1-9 hint, tooltip with the name; a small x on
    hover for user presets;
  - "Save as preset…" opening an inline text field (Enter saves, Esc cancels).
  It closes ~300 ms after the pointer leaves, never while the text field
  has focus. Keyboard shortcuts for everything already exist in Main.qml.
- **Tests**: built-ins listed first and in order; apply writes keys and
  keeps other keys and comments; apply of a user preset with analysis
  keys; saveCurrent round trip; rejected names; remove; applyIndex out of
  range does nothing; current set, then cleared by an external edit.

## Task: wallpaper

- `spectra --wallpaper` runs the normal app as a layer-shell surface:
  - `prepareProcess()`: select the layer-shell integration before the
    platform plugin loads (LayerShellQt 6.8: `LayerShellQt::Shell::useLayerShell()`
    or `QT_WAYLAND_SHELL_INTEGRATION=layer-shell`, check which), and request
    an alpha channel in the default `QSurfaceFormat` so `transparent=true`
    shows the desktop wallpaper through.
  - `attach()`: `LayerShellQt::Window::get(window)`: layer from
    `options.layer` (bottom = above the wallpaper app, background = in its
    place), anchored to all four edges, exclusive zone so it doesn't push
    windows, no keyboard interactivity, a scope like `spectra`. Put it on
    the monitor named `options.monitor` (match `QScreen::name()`), else let
    the compositor choose. Make input pass through to the desktop (empty
    input region). Then show.
  - One wallpaper at a time: a `QLockFile` in `$XDG_RUNTIME_DIR`. Running
    `spectra --wallpaper` while one is running stops the running one and
    exits, so the same launcher entry toggles it.
- `pkgbuild/spectra-wallpaper.desktop` ("Spectra Wallpaper", toggles it)
  installed by the PKGBUILD.
- Verify on the real desktop: start it, `hyprctl layers` shows it on the
  right layer and monitor, a screenshot (`grim`) shows bars over the
  wallpaper at the configured height, clicks reach the desktop, the second
  launch stops it. Stop it when done. Note Hyprland here uses the Lua
  config: dispatch syntax is `hyprctl dispatch 'hl.dsp....'`.
- Don't edit the user's Hyprland config. Put an autostart snippet in the PR
  description instead.
- Tests: argument detection, lock-file toggle logic (factor it so it runs
  without a compositor), monitor matching by name.

## Task: visuals

- **Spectrum** (`spectrum.cpp`): honor `setLayout` so tick computes only
  what the layout reads:
  - `levelsLeft` / `levelsRight`: each channel's power through the same
    bands, normalize and smoothing as `levels` (stereo layout only).
  - `waveform`: about 512 points from the newest ~20 ms of audio, mono,
    aligned to a rising zero crossing so the trace stands still on steady
    tones, scaled to -1..1 without clipping on loud audio (gentle
    auto-gain is fine). Waveform layout only.
  - Pure helpers go in `dsp.{h,cpp}` with tests.
- **Layouts** (each fills its parent, reads `spectrum.*` and `settings.*`,
  follows the color mode and gap, and copies lists once per frame like
  `SpectrumBars` does):
  - **mirrored**: bars grow up and down from the horizontal center line.
  - **radial**: bars around a ring (inner radius about a quarter of the
    shorter side), growing outward; bass at the top, mirrored left and
    right so the ring is symmetric.
  - **stereo**: left channel grows up from the center line, right grows
    down.
  - **waveform**: a smooth line, 2-3 px, colored by the gradient across
    its width (Shapes can't stroke with a gradient; segmenting the line or
    a shader mask both work, pick one and say why). It should look good
    with glow.
  - Peaks where they read well (mirrored, stereo); skip where they don't.
- Glow comes free from Main.qml. Check every layout with glow on and off.
- GPU effects don't render on the offscreen platform: check visuals with a
  window on the real display (a quick harness that loads `qrc:/Main.qml`
  with fake context objects is fine; close it after).
- Tests: per-channel levels (a left-only sine shows in left only), waveform
  point count, range and zero-crossing alignment, layout switching stops
  computing unused outputs.

## Task: capture

- **Sleep when silent**: after `sleepAfter` seconds of digital silence,
  stop pw-record so the output can suspend, and set `sleeping`. Clear the
  ring so the bars fall to zero. Wake when something starts playing:
  watch streams (`pactl subscribe` for sink-input events, then
  `pactl -f json list sink-inputs`) and restart capture when an uncorked
  stream appears. EasyEffects is running here: work out whether its own
  streams are always present and uncorked, and ignore them so they don't
  keep capture awake. Then check that the sink actually reaches SUSPENDED
  (`pactl list sinks short`) while sleeping. If EasyEffects keeps the
  device awake regardless, say so plainly in the PR.
- **One app**: with `target` set, record only that app's playback stream
  (match application name or binary, case-insensitive). Find out how to
  point pw-record at a playback stream node and verify it. Follow the app
  when its stream is recreated (new song, new tab). Nothing playing from
  it means no audio, not an error. Note in the PR that this is pre-EQ.
- **Output latency**: when the default sink changes, read its latency from
  PipeWire (pw-dump / pactl JSON) and emit `outputLatencyChanged` if it
  moved. Bluetooth sinks should report their codec latency. Test the
  parsing on canned JSON captured from this machine. Don't connect or
  switch the user's devices; if no Bluetooth sink is available, say so.
- `setTarget` and `setSleepAfter` are called on every settings change:
  unchanged values must do nothing.
- Parsing goes in `streams.{h,cpp}` as pure functions tested on canned
  JSON; process handling stays in `capture.cpp`. Kill every child process
  on exit.

## Process (every task)

1. Work only in your worktree. `bin/build` and `bin/test` pass before you
   push.
2. Don't change the user's real `~/.config/spectra/spectra.conf` except
   briefly to test, and restore it exactly afterwards. Tests use temp dirs.
3. Commit with short imperative messages, push your branch, and open a PR
   against `phase2-scaffold` (not main). Don't merge.
4. The PR description covers: what changed, how it was verified (paste
   test totals; say what was checked by eye), user-facing docs for the
   README, anything the lead must change in files you don't own, and known
   gaps.

## After the PRs (lead)

Merge `phase2-scaffold` first; GitHub retargets the four PRs to main. Merge
them, resolve the small overlaps (only `resources.qrc` and `settings.*` are
shared), fold the docs into the README, then a GPT review pass and
`bin/install`.

## Later

Album-art colors (MPRIS), beat-reactive glow, trails, native PipeWire
capture.
