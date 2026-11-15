// spectra: a live audio spectrum of whatever is playing, in its own window
// or, with --wallpaper, under every window on the desktop.

#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QUrl>

#include "capture.h"
#include "presets.h"
#include "settings.h"
#include "spectrum.h"
#include "wallpaper.h"

namespace {

void configureApp(QGuiApplication &app) {
    app.setApplicationName("spectra");
    app.setApplicationVersion("0.2.0");
    // Matches the window to spectra.desktop, so the compositor (Wayland
    // app_id) and the launcher pick up the installed icon.
    app.setDesktopFileName("spectra");
    app.setWindowIcon(QIcon::fromTheme("spectra"));
    QQuickStyle::setStyle("Material");
}

// Everything that follows the config file does so live, like the colors.
// With delay=auto the delay comes from the output's latency instead.
void followSettings(const Settings &settings, Capture &capture, Spectrum &spectrum) {
    auto apply = [s = &settings, c = &capture, sp = &spectrum] {
        dsp::Tuning tuning = s->tuning();
        if (s->autoDelay())
            tuning.delaySeconds = float(c->outputLatency());
        sp->setTuning(tuning);
        sp->setLayout(s->layout());
        c->setTarget(s->captureTarget());
        c->setSleepAfter(s->sleepAfter());
    };
    apply();
    QObject::connect(&settings, &Settings::changed, &spectrum, apply);
    QObject::connect(&capture, &Capture::outputLatencyChanged, &spectrum, apply);
}

} // namespace

int main(int argc, char *argv[]) {
    const bool wallpaperMode = wallpaper::requested(argc, argv);
    if (wallpaperMode)
        wallpaper::prepareProcess();
    QGuiApplication app(argc, argv);
    configureApp(app);

    Settings settings;
    Capture capture;
    Spectrum spectrum([&capture](int frames) { return capture.latestFrames(frames); }, Capture::SampleRate);
    Presets presets(&settings);
    followSettings(settings, capture, spectrum);

    QQmlApplicationEngine engine;
    QQmlContext *context = engine.rootContext();
    context->setContextProperty("settings", &settings);
    context->setContextProperty("capture", &capture);
    context->setContextProperty("spectrum", &spectrum);
    context->setContextProperty("presets", &presets);
    context->setContextProperty("wallpaperMode", wallpaperMode);
    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    if (wallpaperMode && !wallpaper::attach(window, settings.wallpaper()))
        return 1;

    capture.start();
    return app.exec();
}
