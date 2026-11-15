# Spectra

![Spectra showing a live spectrum in rainbow colors on black](docs/screenshot.png)

A live spectrum of whatever your computer is playing, in a window of its own.
Black background, colored bars, nothing else. Resize it, make it full screen,
tile it next to your music player.

It listens to the default output's monitor through `pw-record`, so it shows
what you actually hear, after EasyEffects or any other filter. Colors and
tuning live in a plain config file that applies the moment you save it.

Built for [Omarchy](https://omarchy.org) (Arch, Hyprland, PipeWire), but any
Linux desktop with PipeWire and Qt 6 should do.

## Shortcuts

| Key   | Does                                         |
|-------|----------------------------------------------|
| `?`   | Show or hide the shortcut overlay (also F1)  |
| Space | Freeze or unfreeze the bars                  |
| `F`   | Full screen on or off                        |
| `P`   | Peak caps on or off                          |
| `G`   | Glow on or off (or click the corner sparkle) |
| `C`   | Switch color mode: spectrum or vertical      |
| `Q`   | Quit                                         |

`P`, `G` and `C` save their choice to the config file, so it sticks.

## Config

`~/.config/spectra/spectra.conf` is written with the defaults on first run.
Edit it in any editor; spectra picks up the change as soon as the file is
saved. If a value doesn't make sense, spectra uses the default for that key
and says which key in the middle of the window.

```ini
[look]
background=#000000
colors=#ff0000, #ffff00, #00ff00, #00ffff, #0000ff, #ff00ff
mode=spectrum
peaks=true
glow=false
gap=0.25

[analysis]
bands=96
fft_size=8192
min_hz=30
max_hz=16000
floor_db=-70
ceiling_db=-3
attack=0.02
decay=0.25
peak_fall=0.6
delay=0
```

### `[look]`

| Key          | Values                    | Meaning |
|--------------|---------------------------|---------|
| `background` | color                     | Window color behind the bars. |
| `colors`     | comma-separated colors    | Gradient stops from low to high. One color gives solid bars. |
| `mode`       | `spectrum` or `vertical`  | `spectrum` runs the gradient across the bands, bass to treble. `vertical` runs it up each bar, so color shows loudness. |
| `peaks`      | `true` or `false`         | Thin caps that mark each bar's recent peak and drift down. |
| `glow`       | `true` or `false`         | A soft halo around the bars, drawn on the GPU. |
| `gap`        | 0 to 0.9                  | Space between bars, as a fraction of a bar's width. |

Colors are anything Qt understands: `#rrggbb`, `#aarrggbb`, or names like
`orange`.

### `[analysis]`

| Key          | Values            | Meaning |
|--------------|-------------------|---------|
| `bands`      | 8 to 256          | Number of bars. |
| `fft_size`   | 512 to 16384, a power of two | Bigger resolves bass better but reacts a little later. |
| `min_hz`     | 1 to 24000        | Lowest frequency shown. Must be below `max_hz`. |
| `max_hz`     | 1 to 24000        | Highest frequency shown. |
| `floor_db`   | -120 to 20        | Level that draws an empty bar. Must be below `ceiling_db`. |
| `ceiling_db` | -120 to 20        | Level that draws a full bar. Lower it if the bars look short. |
| `attack`     | seconds           | How quickly a bar rises. |
| `decay`      | seconds           | How quickly a bar falls. |
| `peak_fall`  | per second        | How fast peak caps fall, as a fraction of the full height. |
| `delay`      | 0 to 2 seconds    | Holds the picture back to line up with Bluetooth headphones. |

Lines starting with `;` or `#` are comments, and so is anything after a `;`
that follows a space.

## Build and install

You need `qt6-base`, `qt6-declarative`, `qt6-wayland`, `pipewire` (for
`pw-record`) and `fftw`, plus `base-devel`.

```sh
bin/build      # builds build/spectra
bin/test       # runs the tests offscreen
bin/install    # makepkg -fsi: installs the binary, launcher entry and icon
```

After `bin/install`, Spectra shows up in the app launcher (Super + Space).

## License

MIT. See [LICENSE](LICENSE).
