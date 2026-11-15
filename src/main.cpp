// spectra: a live audio spectrum of whatever is playing, in its own window.

#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QUrl>

#include "capture.h"
#include "settings.h"
#include "spectrum.h"

int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);
    app.setApplicationName("spectra");
    app.setApplicationVersion("0.1.0");

    // Matches the window to spectra.desktop, so the compositor (Wayland
    // app_id) and the launcher pick up the installed icon.
    app.setDesktopFileName("spectra");
    app.setWindowIcon(QIcon::fromTheme("spectra"));
    QQuickStyle::setStyle("Material");

    Settings settings;
    Capture capture;
    Spectrum spectrum([&capture](int frames) { return capture.latestFrames(frames); }, Capture::SampleRate);

    // Tuning edits in the config file apply live, like the colors.
    spectrum.setTuning(settings.tuning());
    QObject::connect(&settings, &Settings::changed, &spectrum,
                     [&] { spectrum.setTuning(settings.tuning()); });

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("settings", &settings);
    engine.rootContext()->setContextProperty("capture", &capture);
    engine.rootContext()->setContextProperty("spectrum", &spectrum);
    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;

    capture.start();
    return app.exec();
}
