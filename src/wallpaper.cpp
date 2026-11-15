#include "wallpaper.h"

#include <LayerShellQt/Window>
#include <QDir>
#include <QGuiApplication>
#include <QLockFile>
#include <QQuickWindow>
#include <QScreen>
#include <QSocketNotifier>
#include <QSurfaceFormat>

#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

namespace wallpaper {

namespace {

// Long enough for the old wallpaper to stop capture and leave, short enough
// that a launcher toggle never feels stuck.
constexpr int StopWaitMs = 3000;

// SIGTERM and SIGINT land here as a byte, because a signal handler can't
// safely call into Qt. The event loop reads the other end and quits
// normally, so the capture child processes are stopped on the way out.
int signalPipe[2] = {-1, -1};

void onSignal(int) {
    const char byte = 1;
    [[maybe_unused]] const auto written = ::write(signalPipe[1], &byte, 1);
}

void catchTermination() {
    if (::pipe2(signalPipe, O_CLOEXEC | O_NONBLOCK) != 0)
        return; // Without the pipe the default handlers still end the process.
    struct sigaction action {};
    action.sa_handler = onSignal;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_RESTART;
    sigaction(SIGTERM, &action, nullptr);
    sigaction(SIGINT, &action, nullptr);
}

void quitOnSignal(QObject *owner) {
    if (signalPipe[0] < 0)
        return;
    auto *notifier = new QSocketNotifier(signalPipe[0], QSocketNotifier::Read, owner);
    QObject::connect(notifier, &QSocketNotifier::activated, owner, [] {
        char buffer[16];
        while (::read(signalPipe[0], buffer, sizeof buffer) > 0) {}
        QCoreApplication::quit();
    });
}

// Without an alpha channel a "transparent" window color is drawn black,
// hiding the desktop wallpaper behind the bars.
void requestAlpha() {
    QSurfaceFormat format = QSurfaceFormat::defaultFormat();
    format.setAlphaBufferSize(8);
    QSurfaceFormat::setDefaultFormat(format);
}

// Held for the life of the process; the kernel drops it if we crash, and
// QLockFile then sees the dead PID and takes the file over.
QLockFile &instanceLock() {
    static QLockFile lock(lockPath(qEnvironmentVariable("XDG_RUNTIME_DIR")));
    return lock;
}

bool stopProcess(qint64 pid) {
    return ::kill(pid_t(pid), SIGTERM) == 0;
}

// Waiting for the old one to let go means a quick second toggle starts a
// fresh wallpaper instead of stopping the one that is already leaving.
void waitForStop(QLockFile &lock) {
    if (lock.tryLock(StopWaitMs))
        lock.unlock();
}

LayerShellQt::Window::Layer layerFor(const QString &name) {
    return name == QLatin1String("background") ? LayerShellQt::Window::LayerBackground
                                               : LayerShellQt::Window::LayerBottom;
}

// Anchored to every edge, the compositor sizes the surface to the whole
// output. An exclusive zone of -1 also runs it under bars that reserve
// space, and pushes nothing aside, the way a wallpaper should.
void configureLayer(LayerShellQt::Window &layer, const config::Wallpaper &options) {
    using LS = LayerShellQt::Window;
    layer.setLayer(layerFor(options.layer));
    layer.setAnchors(LS::Anchors(LS::AnchorTop | LS::AnchorBottom | LS::AnchorLeft | LS::AnchorRight));
    layer.setExclusiveZone(-1);
    layer.setKeyboardInteractivity(LS::KeyboardInteractivityNone);
    layer.setActivateOnShow(false);
    layer.setScope(QStringLiteral("spectra"));
}

void placeOnMonitor(QQuickWindow &window, LayerShellQt::Window &layer, const QString &monitor) {
    const QList<QScreen *> screens = QGuiApplication::screens();
    QStringList names;
    for (const QScreen *screen : screens)
        names << screen->name();
    const int index = screenIndex(names, monitor);
    if (index < 0) {
        layer.setWantsToBeOnActiveScreen(true);
        return;
    }
    window.setScreen(screens[index]);
    layer.setScreen(screens[index]);
}

} // namespace

bool requested(int argc, char *argv[]) {
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--wallpaper") == 0)
            return true;
    return false;
}

// Since Qt 6.5 LayerShellQt picks the layer-shell integration per window in
// attach(), so the deprecated Shell::useLayerShell() (which sets
// QT_WAYLAND_SHELL_INTEGRATION for every window) isn't needed here.
void prepareProcess() {
    requestAlpha();
    QLockFile &lock = instanceLock();
    if (claimOrStop(lock, stopProcess) == Claim::StoppedOther) {
        waitForStop(lock);
        std::exit(0);
    }
    catchTermination();
}

bool attach(QQuickWindow *window, const config::Wallpaper &options) {
    if (QGuiApplication::platformName() != QLatin1String("wayland")) {
        qWarning("spectra --wallpaper needs a Wayland compositor with wlr-layer-shell");
        return false;
    }
    LayerShellQt::Window *layer = LayerShellQt::Window::get(window);
    if (!layer)
        return false;
    configureLayer(*layer, options);
    placeOnMonitor(*window, *layer, options.monitor);
    // Qt turns this flag into an empty Wayland input region, so clicks and
    // scrolling fall through to whatever is below.
    window->setFlag(Qt::WindowTransparentForInput);
    quitOnSignal(window);
    window->show();
    return true;
}

QString lockPath(const QString &runtimeDir) {
    const QString dir = runtimeDir.isEmpty() ? QDir::tempPath() : runtimeDir;
    return QDir(dir).filePath(QStringLiteral("spectra-wallpaper.lock"));
}

Claim claimOrStop(QLockFile &lock, const std::function<bool(qint64)> &stop) {
    lock.setStaleLockTime(0);
    if (lock.tryLock(0))
        return Claim::Owner;
    qint64 pid = 0;
    QString host, app;
    if (lock.error() != QLockFile::LockFailedError || !lock.getLockInfo(&pid, &host, &app))
        return Claim::Unguarded;
    return pid > 0 && stop(pid) ? Claim::StoppedOther : Claim::Unguarded;
}

int screenIndex(const QStringList &names, const QString &wanted) {
    if (wanted.isEmpty())
        return -1;
    for (int i = 0; i < names.size(); ++i)
        if (names[i].compare(wanted, Qt::CaseInsensitive) == 0)
            return i;
    return -1;
}

} // namespace wallpaper
