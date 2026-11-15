# Spectra: plan

A resizable desktop window showing a live spectrum of whatever is playing.
Black background, RGB bars, colors and tuning in an editable config file.
Built the way Omarchy's own apps are: C++ and Qt Quick, qmake6, one binary,
a PKGBUILD so it lands in the launcher.

It replaces the small spectrum in EasyEffects 8.3, which can be made taller
but can't be recolored.

## Stages

1. **Working app (this plan).** Audio from `pw-record --raw`, FFT with fftw,
   bars drawn as QML Rectangles with gradients. Done when it runs from the
   launcher, tracks music smoothly, and colors change live from the config.
2. **Optional: native PipeWire capture.** Only if pw-record proves laggy or
   flaky when outputs switch. Swaps `capture.cpp` behind the same interface.
3. **Optional: GPU rendering.** A ShaderEffect fed the band levels, for glow,
   trails, color cycling. Swaps `SpectrumBars.qml`.

## Data flow

```text
pw-record --raw (f32, 48 kHz, stereo, default sink monitor)
   │ stdout, QProcess readyRead on the GUI thread
   ▼
Capture ── ring buffer, 3 s of interleaved stereo
   │ latestFrames(fftSize + delay)        ◄── called by Spectrum::tick
   ▼
Spectrum::tick(dt)  (QML FrameAnimation, once per vsync)
   │ dsp::stereoPower → bandDb → normalize → smooth → peaks
   ▼
levels / peaks (QList<qreal>, 0..1) ──► SpectrumBars.qml ──► screen
                                            ▲
Settings (~/.config/spectra/spectra.conf) ──┘ colors, mode, gap
   └── tuning() ──► Spectrum::setTuning (wired in main.cpp)
```

## Files and owners

The public interfaces in `src/*.h` are fixed. Tasks may add private members,
private helpers, and new files they own, but must not change a public
signature without saying so in their report.

| Area | Files | Task |
|---|---|---|
| Wiring | `src/main.cpp`, `spectra.pro`, `tests/spectra_tests.pro`, `tests/main.cpp`, `bin/*` | lead (integration) |
| Capture | `src/capture.{h,cpp}`, `tests/capture_tests.cpp` | capture |
| DSP | `src/dsp.{h,cpp}`, `src/spectrum.{h,cpp}`, `tests/dsp_tests.cpp` | dsp |
| UI | `src/settings.{h,cpp}`, `src/Main.qml`, `src/SpectrumBars.qml`, `src/resources.qrc`, `tests/settings_tests.cpp`, `pkgbuild/*`, `README.md`, `LICENSE` | ui |

If a task needs a new source file in the build, it says so in its report and
the lead adds it to the `.pro` files (to avoid merge conflicts). Exception:
the ui task owns `resources.qrc`.

## Task: capture

`Capture` runs the recorder and keeps a ring buffer.

- Default command: `pw-record --raw --format f32 --rate 48000 --channels 2
  -P stream.capture.sink=true -`. **`--raw` is required**: without it stdout
  starts with a `.snd` header (verified on this machine). With no `--target`,
  WirePlumber links it to the default sink's monitor; verify it follows a
  default-sink change (`pactl set-default-sink …`) and document what happens.
- Target the hardware sink monitor (post-EQ), not EasyEffects' virtual sink
  (pre-EQ). Check with `pw-link -l` / `pw-dump` while it runs.
- Read stdout on `readyRead`, keep sample-frame alignment across partial
  reads (a read can end mid-float or mid-frame; carry the remainder).
- Ring buffer of `BufferSeconds * SampleRate * Channels` floats. Preallocated,
  no per-read allocation. `latestFrames` zero-pads the front when short.
- Ask pw-record for low latency (`--latency` e.g. `256` samples or `10ms`;
  its default is 100 ms) and measure the effect.
- If the recorder is missing, fails to start, or exits: set `error` to a
  plain sentence ("pw-record isn't installed", "Audio capture stopped: …"),
  emit `statusChanged`, and retry with backoff (1 s, 2 s, 4 s, cap 10 s).
  Clear the error once audio flows again. `stop()` and the destructor end
  the process cleanly with no retry.
- Tests (`tests/capture_tests.cpp`): a fake recorder script in a temp dir
  that writes known floats in odd-sized chunks; check alignment, ordering,
  zero-padding, ring wraparound, error on a missing program, error and retry
  when the fake exits.

## Task: dsp

Pure functions in `dsp.cpp`, and `Spectrum` gluing them to a source.

- `stereoPower`: fftwf r2c per channel, plans cached per size (create plans
  once; fftw planning is slow and not thread-safe). Average the two power
  spectra.
- `logBands`: exactly `count` bands, ascending, non-overlapping, at least one
  bin each. At 4096/48 kHz bins are ~11.7 Hz apart, so the lowest bands run
  out of bins; widen them upward instead of duplicating bins.
- `bandDb`: mean power per band, scaled so a full-scale sine reads about
  0 dB (account for the Hann window's gain and the FFT size).
- `smooth`: `level += (target - level) * (1 - exp(-dt / tau))`, tau = attack
  when rising, decay when falling.
- `Spectrum::tick(dt)`: clamp dt to 0.1 s; when frozen, do nothing. Pull
  `fftSize + delayFrames` frames and analyze the oldest `fftSize` of them
  (that is the A/V delay). Recompute window and bands only when tuning
  changes. Emit `updated` every tick. `setTuning` validates (power-of-two
  fftSize in 512..16384, bands 8..256, minHz < maxHz ≤ Nyquist,
  floorDb < ceilingDb, positive time constants, delay 0..2 s) and clamps
  rather than rejecting.
- Tests (`tests/dsp_tests.cpp`): a 1 kHz sine lands in the band containing
  1 kHz and that band is the max; full-scale sine ≈ 0 dB (±1.5); silence is
  -120; out-of-phase stereo doesn't cancel; `logBands` invariants for several
  sizes including 512-point FFTs with 128 bands; smoothing attack vs decay
  direction and convergence; peaks hold then fall; Spectrum with a fake
  source returns `bands` levels in 0..1 and honors frozen and delay.

## Task: ui

Settings, the window, packaging.

- `Settings`: INI at `~/.config/spectra/spectra.conf` (QSettings IniFormat
  or a small parser). Write defaults atomically on first run (temp file then
  rename). Watch the file and its directory with QFileSystemWatcher, re-arm
  after every change (editors replace files), debounce ~100 ms, emit
  `changed`. Invalid values fall back to defaults and set `error` to a
  sentence naming the key. Defaults:

  ```ini
  [look]
  background=#000000
  colors=#ff0000, #ffff00, #00ff00, #00ffff, #0000ff, #ff00ff
  mode=spectrum        ; spectrum = hue across bands, vertical = bottom to top
  peaks=true
  gap=0.25

  [analysis]
  bands=96
  fft_size=4096
  min_hz=30
  max_hz=16000
  floor_db=-70
  ceiling_db=-10
  attack=0.02
  decay=0.25
  peak_fall=0.6
  delay=0
  ```

  `colorAt(t)` interpolates linearly in RGB between stops. Setters for mode
  and peaks write back to the file atomically, keeping the other keys.
- `Main.qml`: borderless-looking black window (`settings.background`), a
  `FrameAnimation` calling `spectrum.tick(frameTime)`, a `SpectrumBars` filling
  it. Shortcuts as `Shortcut { context: Qt.ApplicationShortcut }`:
  `?` help overlay, `Q` quit, `Space` freeze, `F` fullscreen, `P` peaks,
  `C` cycle color mode. Show `capture.error` and `settings.error` as small
  centered text when non-empty. No other chrome: the bars are the window.
- `SpectrumBars.qml` (not named after a Qt type): reads `spectrum.levels`
  and `spectrum.peaks` **once per frame** into a property, and a Repeater of
  bars binds to `bars.levels[index]`, so the C++ list isn't converted to JS
  once per bar. Bars grow from the bottom; width from window width, band
  count and gap. Spectrum mode: each bar a solid `settings.colorAt(i / (n-1))`,
  computed when colors change, not per frame. Vertical mode: a Gradient with
  stops from `settings.colors`, the gradient spanning the full window height
  so color reads as level. Peak caps: thin bars at the peak height.
- `pkgbuild/`: PKGBUILD (depends `qt6-base qt6-declarative qt6-wayland
  pipewire fftw`), `spectra.desktop`, `spectra.svg` (black rounded square with
  RGB bars). `LICENSE` MIT, Kevin Andrews, 2026. `README.md`: what it is,
  shortcuts table, config reference, build/install.
- Tests (`tests/settings_tests.cpp`): defaults written on first run; values
  read; bad value falls back with error; `colorAt` endpoints and midpoints;
  setters persist and keep other keys; external edit emits `changed`.

## Conventions

- Qt classes only where Qt needs them (QObjects exposed to QML). Everything
  else is free functions; prefer pure ones. Functions ≤ 20 lines, nesting
  ≤ 3. Comments explain why, in full sentences.
- No `qDebug` sprinkled around; errors surface through the `error`
  properties.
- Build with `bin/build`, test with `bin/test`. Both must pass before a task
  reports done. Commit on your branch with short imperative messages.

## Integration (lead)

Merge the three branches, wire anything new into the `.pro` files, build,
run against real music, screenshot, and tune defaults by eye. Then
`bin/install`.
