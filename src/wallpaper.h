#pragma once

#include "settings.h"

class QQuickWindow;

// spectra --wallpaper: the same window, drawn on Hyprland's bottom or
// background layer (wlr-layer-shell, through LayerShellQt) so the spectrum
// sits under every window, ignores input, and fills the chosen monitor.
namespace wallpaper {

// Whether --wallpaper is on the command line. Checked before
// QGuiApplication exists, because the layer-shell integration has to be
// chosen before the platform plugin loads.
bool requested(int argc, char *argv[]);

// Process-wide setup that must run before QGuiApplication is constructed.
void prepareProcess();

// Puts the (still hidden) window on the layer and shows it. Returns false
// with nothing shown when the compositor can't do it; main exits then.
bool attach(QQuickWindow *window, const config::Wallpaper &options);

} // namespace wallpaper
