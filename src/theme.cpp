// Theme watching adapted from Omacut (MIT, David Heinemeier Hansson).
#include "theme.h"

#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStandardPaths>
#include <cmath>

namespace {

QHash<QString, QString> parseColorsToml(const QString &path) {
    QHash<QString, QString> values;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return values;

    const QString text = QString::fromUtf8(file.readAll());
    static const QRegularExpression colorRx(
        R"re(^\s*([a-zA-Z0-9_]+)\s*=\s*["'](#[0-9a-fA-F]{6}|dark|light)["'])re",
        QRegularExpression::MultilineOption);
    auto it = colorRx.globalMatch(text);
    while (it.hasNext()) {
        const auto m = it.next();
        values.insert(m.captured(1), m.captured(2));
    }
    return values;
}

} // namespace

Theme::Theme(const QString &directory, QObject *parent, const QString &hyprctl)
    : QObject(parent),
      m_directory(directory.isEmpty() ? QDir::homePath() + "/.local/state/omarchy/current" : directory) {
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(80);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { m_debounce.start(); });
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this] { m_debounce.start(); });
    connect(&m_debounce, &QTimer::timeout, this, &Theme::reload);

    // Corners follow Hyprland's effective rounding, which themes and personal
    // overrides can both set, so ask hyprctl rather than reading its config.
    // Without Hyprland the corners stay sharp, like Omarchy's default.
    m_hyprctl = !hyprctl.isEmpty() ? hyprctl
              : directory.isEmpty() && !qEnvironmentVariableIsEmpty("HYPRLAND_INSTANCE_SIGNATURE")
                  ? QStandardPaths::findExecutable("hyprctl") : QString();
    m_roundingTimeout.setSingleShot(true);
    m_roundingTimeout.setInterval(500);
    connect(&m_roundingTimeout, &QTimer::timeout, &m_roundingProcess, &QProcess::kill);
    connect(&m_roundingProcess, &QProcess::errorOccurred, this, [this] { m_roundingTimeout.stop(); });
    connect(&m_roundingProcess, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        m_roundingTimeout.stop();
        const auto value = QJsonDocument::fromJson(m_roundingProcess.readAllStandardOutput()).object().value("int");
        if (code != 0 || status != QProcess::NormalExit || !value.isDouble() || value.toInt(-1) < 0)
            return;
        if (value.toInt() != m_radius) {
            m_radius = value.toInt();
            emit changed();
        }
    });
    m_roundingPoll.setInterval(1000);
    connect(&m_roundingPoll, &QTimer::timeout, this, &Theme::refreshRounding);
    if (!m_hyprctl.isEmpty())
        m_roundingPoll.start();
    reload();
}

Theme::~Theme() {
    if (m_roundingProcess.state() != QProcess::NotRunning) {
        m_roundingProcess.kill();
        m_roundingProcess.waitForFinished(1000);
    }
}

void Theme::refreshRounding() {
    if (m_hyprctl.isEmpty() || m_roundingProcess.state() != QProcess::NotRunning)
        return;
    m_roundingProcess.start(m_hyprctl, {"-j", "getoption", "decoration:rounding"});
    m_roundingTimeout.start();
}

QString Theme::readAccent(const QString &path) {
    const auto values = parseColorsToml(path);
    return values.value("accent", "#FFD60A");
}

// Black or white, whichever reads on the background color (WCAG relative luminance).
QString Theme::contrastForeground(const QString &hexColor) {
    const QColor c(hexColor);
    auto linear = [](double v) { return v <= .04045 ? v / 12.92 : std::pow((v + .055) / 1.055, 2.4); };
    const double luminance = .2126 * linear(c.redF()) + .7152 * linear(c.greenF()) + .0722 * linear(c.blueF());
    return luminance > .179 ? "black" : "white";
}

QString Theme::accentForeground() const {
    return contrastForeground(m_accent);
}

void Theme::reload() {
    refreshRounding();
    const auto paths = m_watcher.files() + m_watcher.directories();
    if (!paths.isEmpty())
        m_watcher.removePaths(paths);

    // A theme switch replaces symlinks and files, so watch the parents too and
    // re-arm after every change. Watching the nearest existing ancestor means
    // installing Omarchy later works without restarting the app.
    QString ancestor = m_directory;
    while (!QFileInfo::exists(ancestor) && ancestor != "/")
        ancestor = QFileInfo(ancestor).absolutePath();
    QStringList candidates{ancestor, QFileInfo(ancestor).absolutePath(), m_directory,
                           m_directory + "/theme", m_directory + "/theme/colors.toml"};
    candidates.removeDuplicates();
    for (const auto &path : candidates)
        if (QFileInfo::exists(path))
            m_watcher.addPath(path);

    const auto values = parseColorsToml(m_directory + "/theme/colors.toml");

    bool dirty = false;
    auto assign = [&](QString &member, const QString &key, const QString &fallback) {
        const QString next = values.value(key, fallback);
        if (next != member) {
            member = next;
            dirty = true;
        }
    };

    const bool nextLight = values.value("mode") == "light";
    if (nextLight != m_isLight) {
        m_isLight = nextLight;
        dirty = true;
    }

    assign(m_accent, "accent", "#FFD60A");
    assign(m_background, "background", nextLight ? "#EFF1F5" : "#0E0E10");
    assign(m_darkBackground, "dark_background", nextLight ? "#E3E4E8" : "#0A0A0C");
    assign(m_darkerBackground, "darker_background", nextLight ? "#D7D8DC" : "#070709");
    assign(m_lighterBackground, "lighter_background", nextLight ? "#DCE0E8" : "#19191E");
    assign(m_foreground, "foreground", nextLight ? "#4C4F69" : "#E8E8EC");
    assign(m_darkForeground, "dark_foreground", nextLight ? "#7C7F93" : "#8E8E98");
    assign(m_lightForeground, "light_foreground", nextLight ? "#5C5F77" : "#F2F2F6");
    assign(m_brightForeground, "bright_foreground", nextLight ? "#2E3040" : "#FFFFFF");
    assign(m_selection, "selection", nextLight ? "#CCD0DA" : "#282832");
    assign(m_muted, "muted", nextLight ? "#9CA0B0" : "#6E6E78");
    assign(m_red, "red", nextLight ? "#D20F39" : "#EF6B73");
    assign(m_yellow, "yellow", nextLight ? "#DF8E1D" : "#E5C07B");
    assign(m_orange, "orange", nextLight ? "#D84E2B" : "#F3A96B");
    assign(m_green, "green", nextLight ? "#40A02B" : "#8FBE7A");
    assign(m_cyan, "cyan", nextLight ? "#179299" : "#6EC2C8");
    assign(m_blue, "blue", nextLight ? "#1E66F5" : "#6DAEF0");
    assign(m_magenta, "magenta", nextLight ? "#EA76CB" : "#C39CE0");

    if (dirty)
        emit changed();
}
