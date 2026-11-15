#pragma once

#include "settings.h"

#include <QString>
#include <QStringList>
#include <functional>

class QLockFile;
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
// It also makes the launcher entry a toggle: when another wallpaper is
// already running, this stops it and exits the process (status 0) instead
// of returning.
void prepareProcess();

// Puts the (still hidden) window on the layer and shows it. Returns false
// with nothing shown when the compositor can't do it; main exits then.
bool attach(QQuickWindow *window, const config::Wallpaper &options);

// The pieces below are split out so they run in tests without a compositor.

// Where the one-wallpaper-at-a-time lock lives, given $XDG_RUNTIME_DIR
// (empty falls back to the temp directory).
QString lockPath(const QString &runtimeDir);

enum class Claim {
    Owner,        // we hold the lock: run the wallpaper
    StoppedOther, // another wallpaper held it and was asked to stop: exit
    Unguarded,    // the lock can't be used at all: run without the guard
};

// Takes the lock, or, if a live process holds it, calls stop(pid) on it.
// The lock never goes stale by age, since a wallpaper runs for days.
Claim claimOrStop(QLockFile &lock, const std::function<bool(qint64)> &stop);

// The index of the output called `wanted` (case-insensitive) in `names`,
// or -1 when it's empty or not connected, which leaves the choice to the
// compositor.
int screenIndex(const QStringList &names, const QString &wanted);

} // namespace wallpaper
