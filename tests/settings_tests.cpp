#include <QtTest>

// TODO: owned by the ui task.
class SettingsTests : public QObject {
    Q_OBJECT

private slots:
    void placeholder() { QVERIFY(true); }
};

int runSettingsTests(int argc, char *argv[]) {
    SettingsTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "settings_tests.moc"
