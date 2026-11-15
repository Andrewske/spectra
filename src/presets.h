#pragma once

#include <QFileSystemWatcher>
#include <QList>
#include <QMap>
#include <QObject>
#include <QString>
#include <QVariantList>

class Settings;

// One named look: the "section/key" values it writes into spectra.conf.
struct Preset {
    QString name;
    bool builtIn = false;
    QMap<QString, QString> values;
};

// Named looks: colors, mode, glow, peaks and, for the user's own, tuning.
// Never the layout. Built-in presets are compiled in and set look keys only;
// the user's own live in ~/.config/spectra/presets/<name>.conf as partial INI
// files. Applying one writes its keys into spectra.conf, so the config always
// shows what's on screen and edits to it still apply live.
class Presets : public QObject {
    Q_OBJECT
    // [{name: string, colors: [string], builtIn: bool}], built-ins first.
    Q_PROPERTY(QVariantList list READ list NOTIFY listChanged)
    // The preset last applied, or empty once the config has been edited away from it.
    Q_PROPERTY(QString current READ current NOTIFY currentChanged)

public:
    // An empty directory means ~/.config/spectra/presets.
    explicit Presets(Settings *settings, const QString &directory = {}, QObject *parent = nullptr);

    QVariantList list() const;
    QString current() const;

    Q_INVOKABLE void apply(const QString &name);
    Q_INVOKABLE void applyIndex(int index); // 0-based position in list, for the number keys
    // Saves the current look and tuning as a user preset; false (and nothing
    // written) for an empty name or a built-in's name.
    Q_INVOKABLE bool saveCurrent(const QString &name);
    Q_INVOKABLE bool remove(const QString &name); // user presets only

signals:
    void listChanged();
    void currentChanged();

private:
    QList<Preset> all() const;
    void loadUserPresets();
    void watchDirectory();
    void followSettings();
    void setCurrent(const QString &name);

    Settings *m_settings;
    QString m_directory;
    QList<Preset> m_user; // sorted by name, case-insensitively
    QString m_current;
    QFileSystemWatcher m_watcher;
};
