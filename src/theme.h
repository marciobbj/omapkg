#pragma once

#include <QFileSystemWatcher>
#include <QObject>
#include <QProcess>
#include <QTimer>

// Follows the active Omarchy theme palette (~/.local/state/omarchy/current/theme/colors.toml)
// and Hyprland's effective window rounding live, falling back to dark defaults
// when Omarchy is not present.
class Theme : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString accent READ accent NOTIFY changed)
    Q_PROPERTY(QString accentForeground READ accentForeground NOTIFY changed)
    Q_PROPERTY(bool isLight READ isLight NOTIFY changed)
    Q_PROPERTY(QString background READ background NOTIFY changed)
    Q_PROPERTY(QString darkBackground READ darkBackground NOTIFY changed)
    Q_PROPERTY(QString darkerBackground READ darkerBackground NOTIFY changed)
    Q_PROPERTY(QString lighterBackground READ lighterBackground NOTIFY changed)
    Q_PROPERTY(QString foreground READ foreground NOTIFY changed)
    Q_PROPERTY(QString darkForeground READ darkForeground NOTIFY changed)
    Q_PROPERTY(QString lightForeground READ lightForeground NOTIFY changed)
    Q_PROPERTY(QString brightForeground READ brightForeground NOTIFY changed)
    Q_PROPERTY(QString selection READ selection NOTIFY changed)
    Q_PROPERTY(QString muted READ muted NOTIFY changed)
    Q_PROPERTY(QString red READ red NOTIFY changed)
    Q_PROPERTY(QString yellow READ yellow NOTIFY changed)
    Q_PROPERTY(QString orange READ orange NOTIFY changed)
    Q_PROPERTY(QString green READ green NOTIFY changed)
    Q_PROPERTY(QString cyan READ cyan NOTIFY changed)
    Q_PROPERTY(QString blue READ blue NOTIFY changed)
    Q_PROPERTY(QString magenta READ magenta NOTIFY changed)
    Q_PROPERTY(int radius READ radius NOTIFY changed)

public:
    explicit Theme(const QString &currentDirectory = {}, QObject *parent = nullptr, const QString &hyprctl = {});
    ~Theme() override;

    QString accent() const { return m_accent; }
    QString accentForeground() const;
    bool isLight() const { return m_isLight; }
    QString background() const { return m_background; }
    QString darkBackground() const { return m_darkBackground; }
    QString darkerBackground() const { return m_darkerBackground; }
    QString lighterBackground() const { return m_lighterBackground; }
    QString foreground() const { return m_foreground; }
    QString darkForeground() const { return m_darkForeground; }
    QString lightForeground() const { return m_lightForeground; }
    QString brightForeground() const { return m_brightForeground; }
    QString selection() const { return m_selection; }
    QString muted() const { return m_muted; }
    QString red() const { return m_red; }
    QString yellow() const { return m_yellow; }
    QString orange() const { return m_orange; }
    QString green() const { return m_green; }
    QString cyan() const { return m_cyan; }
    QString blue() const { return m_blue; }
    QString magenta() const { return m_magenta; }
    int radius() const { return m_radius; }

    static QString readAccent(const QString &path);
    static QString contrastForeground(const QString &hexColor);

signals:
    void changed();

private:
    void reload();
    void refreshRounding();

    QString m_directory;
    bool m_isLight = false;
    QString m_accent = "#FFD60A";
    QString m_background = "#0E0E10";
    QString m_darkBackground = "#0A0A0C";
    QString m_darkerBackground = "#070709";
    QString m_lighterBackground = "#19191E";
    QString m_foreground = "#E8E8EC";
    QString m_darkForeground = "#8E8E98";
    QString m_lightForeground = "#F2F2F6";
    QString m_brightForeground = "#FFFFFF";
    QString m_selection = "#282832";
    QString m_muted = "#6E6E78";
    QString m_red = "#EF6B73";
    QString m_yellow = "#E5C07B";
    QString m_orange = "#F3A96B";
    QString m_green = "#8FBE7A";
    QString m_cyan = "#6EC2C8";
    QString m_blue = "#6DAEF0";
    QString m_magenta = "#C39CE0";

    QFileSystemWatcher m_watcher;
    QTimer m_debounce;
    QString m_hyprctl;
    int m_radius = 0;
    QProcess m_roundingProcess;
    QTimer m_roundingPoll;
    QTimer m_roundingTimeout;
};
