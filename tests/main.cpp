// Runs every suite; each lives in its own file so they can be written apart.

#include <QGuiApplication>

int runCaptureTests(int argc, char *argv[]);
int runDspTests(int argc, char *argv[]);
int runSettingsTests(int argc, char *argv[]);
int runPresetsTests(int argc, char *argv[]);
int runStreamsTests(int argc, char *argv[]);
int runWallpaperTests(int argc, char *argv[]);

int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);
    int failures = 0;
    failures += runCaptureTests(argc, argv);
    failures += runDspTests(argc, argv);
    failures += runSettingsTests(argc, argv);
    failures += runPresetsTests(argc, argv);
    failures += runStreamsTests(argc, argv);
    failures += runWallpaperTests(argc, argv);
    return failures == 0 ? 0 : 1;
}
