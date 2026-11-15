// Runs every suite; each lives in its own file so they can be written apart.

#include <QGuiApplication>

int runCaptureTests(int argc, char *argv[]);
int runDspTests(int argc, char *argv[]);
int runSettingsTests(int argc, char *argv[]);

int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);
    int failures = 0;
    failures += runCaptureTests(argc, argv);
    failures += runDspTests(argc, argv);
    failures += runSettingsTests(argc, argv);
    return failures == 0 ? 0 : 1;
}
