#include "backend.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTextStream>

namespace {

QStringList defaultDesktopDirs() {
    const QString home = QDir::homePath();
    return {
        "/usr/share/applications",
        home + "/.local/share/applications",
        "/var/lib/flatpak/exports/share/applications",
        home + "/.local/share/flatpak/exports/share/applications",
    };
}

QString miseInstallsDir() {
    const QString miseData = qEnvironmentVariable("MISE_DATA_DIR");
    if (!miseData.isEmpty())
        return QDir(miseData).filePath("installs");
    const QString xdgData = qEnvironmentVariable("XDG_DATA_HOME");
    if (!xdgData.isEmpty())
        return QDir(xdgData).filePath("mise/installs");
    return QDir::homePath() + "/.local/share/mise/installs";
}

QStringList collectDiskUsageDirs(const QString &pluginsDir) {
    QStringList dirs;
    QSet<QString> seen;
    auto addDir = [&](const QString &path) {
        const QFileInfo fi(path);
        if (!fi.exists() || !fi.isDir())
            return;
        const QString canonical = fi.canonicalFilePath();
        const QString target = canonical.isEmpty() ? fi.absoluteFilePath() : canonical;
        if (!seen.contains(target)) {
            seen.insert(target);
            dirs.append(target);
        }
    };

    if (!pluginsDir.isEmpty()) {
        QDir pdir(pluginsDir);
        if (pdir.exists()) {
            const auto entries = pdir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
            for (const QFileInfo &entry : entries) {
                if (!entry.fileName().startsWith('.'))
                    addDir(entry.filePath());
            }
        }
    }

    QDir mdir(miseInstallsDir());
    if (mdir.exists()) {
        const auto entries = mdir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QFileInfo &entry : entries) {
            if (!entry.fileName().startsWith('.'))
                addDir(entry.filePath());
        }
    }

    return dirs;
}

QString runQuickGit(const QString &gitPath, const QString &dirPath, const QStringList &args) {
    if (gitPath.isEmpty() || dirPath.isEmpty())
        return {};
    QProcess proc;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("LC_ALL", "C");
    env.insert("GIT_TERMINAL_PROMPT", "0");
    if (!env.contains("GIT_SSH_COMMAND"))
        env.insert("GIT_SSH_COMMAND", "ssh -oBatchMode=yes");
    proc.setProcessEnvironment(env);
    QStringList fullArgs{"-C", dirPath};
    fullArgs.append(args);
    proc.start(gitPath, fullArgs);
    if (!proc.waitForFinished(3000) || proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0)
        return {};
    return QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
}

bool isGitAncestor(const QString &gitPath, const QString &dirPath, const QString &commit) {
    if (gitPath.isEmpty() || dirPath.isEmpty() || commit.isEmpty())
        return false;
    QProcess proc;
    proc.start(gitPath, {"-C", dirPath, "merge-base", "--is-ancestor", commit, "HEAD"});
    return proc.waitForFinished(3000) && proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0;
}

QString normalizeLookupKey(const QString &value) {
    QString s = value.trimmed().toLower();
    static const QStringList suffixes{"-bin", "-git", "-appimage", "-nightly", "-beta", "-stable"};
    for (const auto &suffix : suffixes) {
        if (s.endsWith(suffix) && s.size() > suffix.size() + 1) {
            s.chop(suffix.size());
            break;
        }
    }
    return s;
}

QProcess *spawnAsync(QObject *parent, const QString &program, const QStringList &args,
                     bool forceCLocale = true, const QMap<QString, QString> &extraEnv = {},
                     const QString &workingDirectory = {}) {
    auto *proc = new QProcess(parent);
    if (forceCLocale || !extraEnv.isEmpty()) {
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        if (forceCLocale)
            env.insert("LC_ALL", "C");
        for (auto it = extraEnv.cbegin(); it != extraEnv.cend(); ++it)
            env.insert(it.key(), it.value());
        proc->setProcessEnvironment(env);
    }
    if (!workingDirectory.isEmpty())
        proc->setWorkingDirectory(workingDirectory);
    proc->start(program, args);
    return proc;
}

} // namespace

Backend::Backend(bool autoLoad, QObject *parent) : QObject(parent) {
    m_expacPath = QStandardPaths::findExecutable("expac");
    m_pacmanPath = QStandardPaths::findExecutable("pacman");
    m_checkupdatesPath = QStandardPaths::findExecutable("checkupdates");
    m_yayPath = QStandardPaths::findExecutable("yay");
    m_flatpakPath = QStandardPaths::findExecutable("flatpak");
    m_misePath = QStandardPaths::findExecutable("mise");
    m_gitPath = QStandardPaths::findExecutable("git");
    m_duPath = QStandardPaths::findExecutable("du");
    m_pkexecPath = QStandardPaths::findExecutable("pkexec");
    m_omarchyDefaultsPath = QStandardPaths::findExecutable("omarchy-pkg-defaults");
    m_omarchyUpdatePath = QStandardPaths::findExecutable("omarchy-update");
    m_omarchyTerminalPath = QStandardPaths::findExecutable("omarchy-launch-floating-terminal-with-presentation");
    m_xdgTerminalPath = QStandardPaths::findExecutable("xdg-terminal-exec");
    m_omarchyPluginListPath = QStandardPaths::findExecutable("omarchy-plugin-list");
    m_omarchyPluginUpdatePath = QStandardPaths::findExecutable("omarchy-plugin-update");
    m_omarchyPluginRemovePath = QStandardPaths::findExecutable("omarchy-plugin-remove");
    const QString defaultPluginsDir = QDir::homePath() + "/.config/omarchy/plugins";
    if (QDir(defaultPluginsDir).exists() || !m_omarchyPluginListPath.isEmpty())
        m_pluginsDir = defaultPluginsDir;
    m_gtkLaunchPath = QStandardPaths::findExecutable("gtk-launch");

    if (autoLoad)
        reloadPackages(true);
}

Backend::~Backend() {
    if (m_opProcess && m_opProcess->state() != QProcess::NotRunning) {
        m_opProcess->kill();
        m_opProcess->waitForFinished(1000);
    }
    if (m_detailsProcess && m_detailsProcess->state() != QProcess::NotRunning) {
        m_detailsProcess->kill();
        m_detailsProcess->waitForFinished(500);
    }
}

qint64 Backend::parseSizeBytes(const QString &humanSize) {
    // Handles both ASCII spaces and non-breaking spaces (\u00A0, \u202F) emitted by flatpak.
    QString normalized = humanSize;
    normalized.replace(QChar(0x00A0), QLatin1Char(' '));
    normalized.replace(QChar(0x202F), QLatin1Char(' '));
    static const QRegularExpression rx(R"(^\s*([0-9]+(?:[.,][0-9]+)?)\s*([a-zA-Z]+)?\s*$)");
    const auto m = rx.match(normalized);
    if (!m.hasMatch())
        return 0;

    QString numStr = m.captured(1);
    numStr.replace(',', '.');
    bool ok = false;
    const double value = numStr.toDouble(&ok);
    if (!ok)
        return 0;

    const QString unit = m.captured(2).toLower();
    if (unit == "b" || unit.isEmpty())
        return static_cast<qint64>(value);
    if (unit == "kb" || unit == "k")
        return static_cast<qint64>(value * 1000.0);
    if (unit == "kib")
        return static_cast<qint64>(value * 1024.0);
    if (unit == "mb" || unit == "m")
        return static_cast<qint64>(value * 1000.0 * 1000.0);
    if (unit == "mib")
        return static_cast<qint64>(value * 1024.0 * 1024.0);
    if (unit == "gb" || unit == "g")
        return static_cast<qint64>(value * 1000.0 * 1000.0 * 1000.0);
    if (unit == "gib")
        return static_cast<qint64>(value * 1024.0 * 1024.0 * 1024.0);
    if (unit == "tb" || unit == "t")
        return static_cast<qint64>(value * 1000.0 * 1000.0 * 1000.0 * 1000.0);
    if (unit == "tib")
        return static_cast<qint64>(value * 1024.0 * 1024.0 * 1024.0 * 1024.0);
    return static_cast<qint64>(value);
}

QHash<QString, QString> Backend::parseExpacRepos(const QString &output) {
    // pacman.conf lists [omarchy] ahead of [core]/[extra]/[multilib], so first-wins
    // ensures packages shipped by Omarchy are attributed to Omarchy.
    QHash<QString, QString> map;
    const QStringList lines = output.split('\n', Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const int sep = line.indexOf('\t');
        if (sep <= 0)
            continue;
        const QString repo = line.left(sep).trimmed();
        const QString name = line.mid(sep + 1).trimmed();
        if (!name.isEmpty() && !map.contains(name))
            map.insert(name, repo);
    }
    return map;
}

QVector<PackageItem> Backend::parseExpacInstalled(const QString &output,
                                                  const QHash<QString, QString> &repoByPkg,
                                                  const QSet<QString> &orphans,
                                                  const QSet<QString> &omarchyDefaults) {
    QVector<PackageItem> items;
    const QStringList lines = output.split('\n', Qt::SkipEmptyParts);
    items.reserve(lines.size());

    for (const QString &line : lines) {
        const QStringList parts = line.split('\t');
        if (parts.size() < 4)
            continue;

        PackageItem pkg;
        pkg.name = parts.at(0).trimmed();
        if (pkg.name.isEmpty())
            continue;
        pkg.displayName = pkg.name;
        pkg.version = parts.at(1).trimmed();
        pkg.sizeBytes = parts.at(2).trimmed().toLongLong();
        pkg.explicitInstall = parts.at(3).trimmed() == "explicit";
        pkg.installDate = parts.size() > 4 ? parts.at(4).trimmed().toLongLong() : 0;
        pkg.url = parts.size() > 5 ? parts.at(5).trimmed() : QString();
        pkg.description = parts.size() > 6 ? parts.mid(6).join('\t').trimmed() : QString();
        pkg.isOrphan = orphans.contains(pkg.name);
        pkg.isOmarchyDefault = omarchyDefaults.contains(pkg.name);

        if (repoByPkg.contains(pkg.name)) {
            pkg.repository = repoByPkg.value(pkg.name);
            pkg.source = (pkg.repository == "omarchy") ? "omarchy" : "pacman";
        } else {
            pkg.repository = "AUR";
            pkg.source = "aur";
        }
        pkg.id = pkg.source + ":" + pkg.name;
        items.append(std::move(pkg));
    }
    return items;
}

QHash<QString, QString> Backend::parseUpdatesOutput(const QString &output) {
    // Matches both `checkupdates --nocolor` ("pkg 1.0-1 -> 1.1-1") and
    // `yay -Qua` ("pkg 1.0-1 -> 1.1-1 [2d3h]") while ignoring warning lines.
    QHash<QString, QString> updates;
    static const QRegularExpression rx(R"(^([A-Za-z0-9@._+-]+)\s+\S+\s+->\s+(\S+))");
    const QStringList lines = output.split('\n', Qt::SkipEmptyParts);
    for (const QString &rawLine : lines) {
        const QString line = rawLine.trimmed();
        const auto m = rx.match(line);
        if (m.hasMatch())
            updates.insert(m.captured(1), m.captured(2));
    }
    return updates;
}

QVector<PackageItem> Backend::parseFlatpakList(const QString &output) {
    QVector<PackageItem> items;
    const QStringList lines = output.split('\n', Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const QStringList parts = line.split('\t');
        if (parts.size() < 2)
            continue;

        PackageItem pkg;
        pkg.name = parts.at(0).trimmed();
        if (pkg.name.isEmpty())
            continue;
        pkg.displayName = parts.at(1).trimmed().isEmpty() ? pkg.name : parts.at(1).trimmed();
        pkg.version = parts.size() > 2 ? parts.at(2).trimmed() : QString();
        pkg.sizeBytes = parts.size() > 3 ? parseSizeBytes(parts.at(3)) : 0;
        pkg.description = parts.size() > 4 ? parts.at(4).trimmed() : QString();
        pkg.repository = parts.size() > 5 && !parts.at(5).trimmed().isEmpty()
                             ? parts.at(5).trimmed()
                             : QStringLiteral("flatpak");
        pkg.source = "flatpak";
        pkg.explicitInstall = true;
        pkg.iconName = pkg.name;
        pkg.desktopId = pkg.name;
        pkg.id = "flatpak:" + pkg.name;
        items.append(std::move(pkg));
    }
    return items;
}

QHash<QString, QString> Backend::parseFlatpakUpdates(const QString &output) {
    QHash<QString, QString> updates;
    const QStringList lines = output.split('\n', Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const QStringList parts = line.split('\t');
        const QString appId = parts.at(0).trimmed();
        if (appId.isEmpty())
            continue;
        const QString nextVer = parts.size() > 1 ? parts.at(1).trimmed() : QString();
        updates.insert(appId, nextVer);
    }
    return updates;
}

QVariantMap Backend::parsePacmanDetails(const QString &output) {
    QVariantMap details;
    QString currentKey;
    QString currentValue;

    auto commit = [&]() {
        if (currentKey.isEmpty())
            return;
        const QString val = currentValue.trimmed();
        auto splitList = [&](const QString &raw) -> QStringList {
            if (raw.isEmpty() || raw == "None")
                return {};
            return raw.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        };

        if (currentKey == "Depends On")
            details.insert("dependsOn", splitList(val));
        else if (currentKey == "Required By")
            details.insert("requiredBy", splitList(val));
        else if (currentKey == "Optional For")
            details.insert("optionalFor", splitList(val));
        else if (currentKey == "Licenses")
            details.insert("licenses", val);
        else if (currentKey == "Architecture")
            details.insert("architecture", val);
        else if (currentKey == "Packager")
            details.insert("packager", val);
        else if (currentKey == "Build Date")
            details.insert("buildDate", val);
    };

    const QStringList lines = output.split('\n');
    for (const QString &line : lines) {
        if (line.trimmed().isEmpty())
            continue;
        if (!line.startsWith(' ')) {
            const int colon = line.indexOf(':');
            if (colon > 0) {
                commit();
                currentKey = line.left(colon).trimmed();
                currentValue = line.mid(colon + 1).trimmed();
                continue;
            }
        }
        if (!currentKey.isEmpty())
            currentValue += " " + line.trimmed();
    }
    commit();
    return details;
}

QVariantMap Backend::parseFlatpakDetails(const QString &output) {
    QVariantMap details;
    const QStringList lines = output.split('\n');
    for (const QString &line : lines) {
        const int colon = line.indexOf(':');
        if (colon <= 0)
            continue;
        const QString key = line.left(colon).trimmed();
        const QString val = line.mid(colon + 1).trimmed();
        if (key == "Origin")
            details.insert("packager", val);
        else if (key == "Arch")
            details.insert("architecture", val);
        else if (key == "License")
            details.insert("licenses", val);
        else if (key == "Date")
            details.insert("buildDate", val);
        else if (key == "Runtime")
            details.insert("dependsOn", QStringList{val});
    }
    details.insert("requiredBy", QStringList{});
    return details;
}

QHash<QString, qint64> Backend::parseDuSizes(const QString &output) {
    QHash<QString, qint64> sizes;
    const QStringList lines = output.split('\n', Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const int sep = line.indexOf('\t');
        if (sep <= 0)
            continue;
        bool ok = false;
        const qint64 bytes = line.left(sep).trimmed().toLongLong(&ok);
        if (!ok)
            continue;
        QString path = line.mid(sep + 1).trimmed();
        while (path.size() > 1 && path.endsWith('/'))
            path.chop(1);
        if (path.isEmpty())
            continue;
        sizes.insert(path, bytes);
        const QString canonical = QFileInfo(path).canonicalFilePath();
        if (!canonical.isEmpty())
            sizes.insert(canonical, bytes);
    }
    return sizes;
}

QString Backend::normalizeGitRemoteUrl(const QString &rawUrl) {
    QString url = rawUrl.trimmed();
    if (url.isEmpty())
        return {};
    static const QRegularExpression sshRx(R"(^git@([^:]+):(.+)$)");
    const auto m = sshRx.match(url);
    if (m.hasMatch()) {
        url = QStringLiteral("https://%1/%2").arg(m.captured(1), m.captured(2));
    } else if (url.startsWith("ssh://git@")) {
        url = "https://" + url.mid(QStringLiteral("ssh://git@").size());
    }
    if (url.endsWith(".git"))
        url.chop(4);
    if (url.startsWith("http://") || url.startsWith("https://"))
        return url;
    return {};
}

QString Backend::parsePluginRemoteHead(const QString &lsRemoteOutput) {
    const QStringList lines = lsRemoteOutput.split('\n', Qt::SkipEmptyParts);
    static const QRegularExpression shaRx(R"(^([0-9a-fA-F]{40})\s+)");
    for (const QString &rawLine : lines) {
        const auto m = shaRx.match(rawLine.trimmed());
        if (m.hasMatch())
            return m.captured(1).toLower();
    }
    return {};
}

QVector<PackageItem> Backend::parseOmarchyPlugins(const QString &pluginsDir,
                                                  const QString &listJson,
                                                  const QHash<QString, qint64> &dirSizes,
                                                  QHash<QString, PluginMeta> *metaOut) {
    QHash<QString, bool> enabledById;
    if (!listJson.trimmed().isEmpty()) {
        const QJsonDocument doc = QJsonDocument::fromJson(listJson.toUtf8());
        if (doc.isArray()) {
            for (const QJsonValue &val : doc.array()) {
                const QJsonObject obj = val.toObject();
                if (obj.value("firstParty").toBool(false))
                    continue;
                const QString id = obj.value("id").toString().trimmed();
                if (!id.isEmpty())
                    enabledById.insert(id, obj.value("enabled").toBool(false));
            }
        }
    }

    QVector<PackageItem> items;
    if (pluginsDir.isEmpty())
        return items;

    QDir dir(pluginsDir);
    if (!dir.exists())
        return items;

    const QString gitPath = QStandardPaths::findExecutable("git");
    const auto entries = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo &entry : entries) {
        if (entry.fileName().startsWith('.'))
            continue;
        const QString entryPath = entry.filePath();
        const QString manifestPath = QDir(entryPath).filePath("manifest.json");
        QFile manifestFile(manifestPath);
        if (!manifestFile.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;

        const QJsonDocument doc = QJsonDocument::fromJson(manifestFile.readAll());
        if (!doc.isObject())
            continue;

        const QJsonObject obj = doc.object();
        const QString pluginId = obj.value("id").toString(entry.fileName()).trimmed();
        if (pluginId.isEmpty() || pluginId.startsWith("omarchy."))
            continue;

        QStringList kinds;
        for (const QJsonValue &kv : obj.value("kinds").toArray()) {
            const QString k = kv.toString().trimmed();
            if (!k.isEmpty())
                kinds.append(k);
        }

        const QString canonicalDir = entry.canonicalFilePath().isEmpty()
                                         ? entry.absoluteFilePath()
                                         : entry.canonicalFilePath();
        const bool isGit = QFileInfo::exists(QDir(entryPath).filePath(".git"));

        PluginMeta meta;
        meta.dirPath = entryPath;
        meta.author = obj.value("author").toString().trimmed();
        meta.license = obj.value("license").toString().trimmed();
        meta.kinds = kinds;
        meta.enabled = enabledById.value(pluginId, false);
        meta.isGit = isGit;
        if (isGit && !gitPath.isEmpty()) {
            meta.localHead = runQuickGit(gitPath, entryPath, {"rev-parse", "HEAD"});
            meta.remoteUrl = runQuickGit(gitPath, entryPath, {"remote", "get-url", "origin"});
        }

        PackageItem pkg;
        pkg.id = "omarchy-plugin:" + pluginId;
        pkg.name = pluginId;
        pkg.displayName = obj.value("name").toString().trimmed();
        if (pkg.displayName.isEmpty())
            pkg.displayName = pluginId;
        pkg.version = obj.value("version").toString().trimmed();
        if (pkg.version.isEmpty() && !meta.localHead.isEmpty())
            pkg.version = meta.localHead.left(7);
        pkg.source = "omarchy-plugin";
        pkg.repository = kinds.isEmpty() ? QStringLiteral("plugin") : kinds.join(", ");
        pkg.description = obj.value("description").toString().trimmed();
        pkg.url = obj.value("homepage").toString().trimmed();
        if (pkg.url.isEmpty() && !meta.remoteUrl.isEmpty())
            pkg.url = normalizeGitRemoteUrl(meta.remoteUrl);
        pkg.sizeBytes = dirSizes.value(canonicalDir, dirSizes.value(entryPath, 0));
        pkg.installDate = QFileInfo(manifestPath).lastModified().toSecsSinceEpoch();
        pkg.explicitInstall = true;

        if (metaOut)
            metaOut->insert(pluginId, std::move(meta));
        items.append(std::move(pkg));
    }
    return items;
}

QVector<PackageItem> Backend::parseMiseInstalled(const QString &jsonOutput,
                                                 const QHash<QString, qint64> &dirSizes,
                                                 QHash<QString, MiseToolMeta> *metaOut) {
    QVector<PackageItem> items;
    if (jsonOutput.trimmed().isEmpty())
        return items;

    const QJsonDocument doc = QJsonDocument::fromJson(jsonOutput.toUtf8());
    if (!doc.isObject())
        return items;

    const QString home = QDir::homePath();
    const QJsonObject root = doc.object();
    for (auto it = root.constBegin(); it != root.constEnd(); ++it) {
        const QString toolName = it.key().trimmed();
        if (toolName.isEmpty() || !it.value().isArray())
            continue;

        const QJsonArray arr = it.value().toArray();
        QString activeVersion;
        QString activeInstallPath;
        QString requestedVersion;
        QString configPath;
        QStringList installedVersions;
        qint64 summedVersionBytes = 0;
        QString toolParentDir;

        for (const QJsonValue &entryVal : arr) {
            const QJsonObject entry = entryVal.toObject();
            if (!entry.value("installed").toBool(false))
                continue;

            const QString ver = entry.value("version").toString().trimmed();
            const QString installPath = entry.value("install_path").toString().trimmed();
            const bool active = entry.value("active").toBool(false);

            if (!ver.isEmpty())
                installedVersions.append(ver);

            if (!installPath.isEmpty()) {
                const QString canonical = QFileInfo(installPath).canonicalFilePath();
                const qint64 bytes = dirSizes.value(installPath, dirSizes.value(canonical, 0));
                summedVersionBytes += bytes;
                if (toolParentDir.isEmpty()) {
                    const QFileInfo fi(installPath);
                    toolParentDir = fi.dir().canonicalPath();
                    if (toolParentDir.isEmpty())
                        toolParentDir = fi.path();
                }
            }

            if (active || activeVersion.isEmpty()) {
                activeVersion = ver;
                activeInstallPath = installPath;
                const QString req = entry.value("requested_version").toString().trimmed();
                if (!req.isEmpty())
                    requestedVersion = req;
                const QJsonObject srcObj = entry.value("source").toObject();
                const QString srcPath = srcObj.value("path").toString().trimmed();
                if (!srcPath.isEmpty())
                    configPath = srcPath;
            }
        }

        if (installedVersions.isEmpty())
            continue;

        qint64 totalSize = summedVersionBytes;
        if (totalSize == 0 && !toolParentDir.isEmpty())
            totalSize = dirSizes.value(toolParentDir, 0);

        QString shortConfig = configPath;
        if (!home.isEmpty() && shortConfig.startsWith(home + "/"))
            shortConfig = "~" + shortConfig.mid(home.size());

        PackageItem pkg;
        pkg.id = "mise:" + toolName;
        pkg.name = toolName;
        pkg.displayName = toolName;
        pkg.version = activeVersion;
        pkg.source = "mise";
        pkg.repository = requestedVersion.isEmpty() ? QStringLiteral("mise") : requestedVersion;
        pkg.sizeBytes = totalSize;
        if (!activeInstallPath.isEmpty()) {
            const QFileInfo fi(activeInstallPath);
            if (fi.exists())
                pkg.installDate = fi.lastModified().toSecsSinceEpoch();
        }
        if (installedVersions.size() > 1) {
            pkg.description = QStringLiteral("%1 installed versions").arg(installedVersions.size());
            if (!shortConfig.isEmpty())
                pkg.description += QStringLiteral(" · ") + shortConfig;
        } else if (!shortConfig.isEmpty()) {
            pkg.description = shortConfig;
        } else {
            pkg.description = QStringLiteral("Managed by mise");
        }
        pkg.explicitInstall = true;

        if (metaOut) {
            MiseToolMeta meta;
            meta.configPath = configPath;
            meta.requestedVersion = requestedVersion;
            meta.activeInstallPath = activeInstallPath;
            meta.installedVersions = installedVersions;
            metaOut->insert(toolName, std::move(meta));
        }

        items.append(std::move(pkg));
    }

    return items;
}

QHash<QString, QString> Backend::parseMiseOutdated(const QString &jsonOutput) {
    QHash<QString, QString> updates;
    if (jsonOutput.trimmed().isEmpty())
        return updates;

    const QJsonDocument doc = QJsonDocument::fromJson(jsonOutput.toUtf8());
    if (!doc.isObject())
        return updates;

    const QJsonObject root = doc.object();
    for (auto it = root.constBegin(); it != root.constEnd(); ++it) {
        if (!it.value().isObject())
            continue;
        const QJsonObject obj = it.value().toObject();
        const QString name = obj.value("name").toString(it.key()).trimmed();
        const QString current = obj.value("current").toString().trimmed();
        QString latest = obj.value("latest").toString().trimmed();
        if (latest.isEmpty())
            latest = obj.value("bump").toString().trimmed();
        if (!name.isEmpty() && !latest.isEmpty() && latest != current)
            updates.insert(name, latest);
    }
    return updates;
}

QHash<QString, DesktopEntryInfo> Backend::scanDesktopEntries(const QStringList &directories) {
    QHash<QString, DesktopEntryInfo> index;
    for (const QString &dirPath : directories) {
        QDir dir(dirPath);
        if (!dir.exists())
            continue;
        const QStringList files = dir.entryList({"*.desktop"}, QDir::Files);
        for (const QString &fileName : files) {
            QFile file(dir.filePath(fileName));
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
                continue;

            bool inMainSection = false;
            bool noDisplay = false;
            bool hidden = false;
            DesktopEntryInfo info;
            info.desktopId = fileName.left(fileName.size() - 8); // strip ".desktop"

            QTextStream in(&file);
            while (!in.atEnd()) {
                const QString line = in.readLine().trimmed();
                if (line.isEmpty() || line.startsWith('#'))
                    continue;
                if (line.startsWith('[')) {
                    inMainSection = (line == "[Desktop Entry]");
                    continue;
                }
                if (!inMainSection)
                    continue;
                const int eq = line.indexOf('=');
                if (eq <= 0)
                    continue;
                const QString key = line.left(eq).trimmed();
                const QString val = line.mid(eq + 1).trimmed();
                if (key == "Name" && info.displayName.isEmpty())
                    info.displayName = val;
                else if (key == "Icon" && info.iconName.isEmpty())
                    info.iconName = val;
                else if (key == "Exec" && info.execBinary.isEmpty()) {
                    const QString firstToken = val.split(' ', Qt::SkipEmptyParts).value(0);
                    info.execBinary = QFileInfo(firstToken).fileName().toLower();
                } else if (key == "NoDisplay" && val == "true")
                    noDisplay = true;
                else if (key == "Hidden" && val == "true")
                    hidden = true;
            }

            if (hidden || noDisplay || info.displayName.isEmpty())
                continue;

            const QString idKey = info.desktopId.toLower();
            if (!index.contains(idKey))
                index.insert(idKey, info);
            if (!info.execBinary.isEmpty() && !index.contains(info.execBinary))
                index.insert(info.execBinary, info);
            const QString nameKey = info.displayName.toLower();
            if (!index.contains(nameKey))
                index.insert(nameKey, info);
        }
    }
    return index;
}

void Backend::enrichWithDesktopEntries(QVector<PackageItem> &items) const {
    const auto entries = scanDesktopEntries(defaultDesktopDirs());
    for (auto &pkg : items) {
        if (pkg.source == "omarchy-plugin" || pkg.source == "mise")
            continue;
        const QString lowerName = pkg.name.toLower();
        const QString normName = normalizeLookupKey(pkg.name);

        DesktopEntryInfo matched;
        if (entries.contains(lowerName))
            matched = entries.value(lowerName);
        else if (entries.contains(normName))
            matched = entries.value(normName);

        if (!matched.desktopId.isEmpty()) {
            if (pkg.source != "flatpak" && !matched.displayName.isEmpty())
                pkg.displayName = matched.displayName;
            if (!matched.iconName.isEmpty())
                pkg.iconName = matched.iconName;
            pkg.desktopId = matched.desktopId;
        }
    }
}

void Backend::reloadPackages(bool checkForUpdatesAfter) {
    if (m_loading)
        return;

    m_errorMessage.clear();
    if (m_pacmanPath.isEmpty() || m_expacPath.isEmpty()) {
        m_errorMessage = tr("Required tools (pacman and expac) were not found on this system.");
        emit stateChanged();
        return;
    }

    m_loading = true;
    m_checkAfterLoad = checkForUpdatesAfter;
    m_expacQOut.clear();
    m_expacSOut.clear();
    m_orphansOut.clear();
    m_defaultsOut.clear();
    m_flatpakListOut.clear();
    m_pluginListOut.clear();
    m_miseListOut.clear();
    m_duOut.clear();
    m_pendingLoadTasks = 0;
    emit stateChanged();

    auto launchTask = [&](const QString &program, const QStringList &args, QString *target,
                          const QMap<QString, QString> &extraEnv = {}, const QString &cwd = {}) {
        ++m_pendingLoadTasks;
        QProcess *proc = spawnAsync(this, program, args, true, extraEnv, cwd);
        connect(proc, &QProcess::finished, this, [this, proc, target](int, QProcess::ExitStatus) {
            *target = QString::fromUtf8(proc->readAllStandardOutput());
            proc->deleteLater();
            startReloadStepFinished();
        });
        connect(proc, &QProcess::errorOccurred, this, [this, proc](QProcess::ProcessError) {
            proc->deleteLater();
            startReloadStepFinished();
        });
    };

    launchTask(m_expacPath, {"-Q", "%n\t%v\t%m\t%w\t%l\t%u\t%d", "-t", "%s"}, &m_expacQOut);
    launchTask(m_expacPath, {"-S", "%r\t%n"}, &m_expacSOut);
    launchTask(m_pacmanPath, {"-Qdtq"}, &m_orphansOut);
    if (!m_omarchyDefaultsPath.isEmpty())
        launchTask(m_omarchyDefaultsPath, {}, &m_defaultsOut);
    if (!m_flatpakPath.isEmpty())
        launchTask(m_flatpakPath, {"list", "--app", "--columns=application,name,version,size,description,origin"},
                   &m_flatpakListOut);
    if (!m_omarchyPluginListPath.isEmpty())
        launchTask(m_omarchyPluginListPath, {"--json"}, &m_pluginListOut);
    if (!m_misePath.isEmpty())
        launchTask(m_misePath, {"-C", QDir::homePath(), "ls", "--json"}, &m_miseListOut);
    if (!m_duPath.isEmpty()) {
        const QStringList dirs = collectDiskUsageDirs(m_pluginsDir);
        if (!dirs.isEmpty()) {
            QStringList duArgs{"-sb"};
            duArgs.append(dirs);
            launchTask(m_duPath, duArgs, &m_duOut);
        }
    }
}

void Backend::startReloadStepFinished() {
    --m_pendingLoadTasks;
    if (m_pendingLoadTasks <= 0)
        finalizePackageLoad();
}

void Backend::finalizePackageLoad() {
    const auto repoMap = parseExpacRepos(m_expacSOut);
    QSet<QString> orphans;
    for (const QString &line : m_orphansOut.split('\n', Qt::SkipEmptyParts))
        orphans.insert(line.trimmed());

    QSet<QString> omarchyDefaults;
    for (const QString &line : m_defaultsOut.split('\n', Qt::SkipEmptyParts))
        omarchyDefaults.insert(line.trimmed());

    const auto dirSizes = parseDuSizes(m_duOut);
    m_pluginMeta.clear();
    m_miseMeta.clear();

    QVector<PackageItem> items = parseExpacInstalled(m_expacQOut, repoMap, orphans, omarchyDefaults);
    if (!m_flatpakListOut.isEmpty())
        items.append(parseFlatpakList(m_flatpakListOut));
    if (!m_pluginsDir.isEmpty())
        items.append(parseOmarchyPlugins(m_pluginsDir, m_pluginListOut, dirSizes, &m_pluginMeta));
    if (!m_miseListOut.isEmpty())
        items.append(parseMiseInstalled(m_miseListOut, dirSizes, &m_miseMeta));

    enrichWithDesktopEntries(items);
    m_model.setPackages(std::move(items));

    // Preserve any already-discovered update markers until the next update check completes.
    if (!m_pacmanUpdates.isEmpty() || !m_aurUpdates.isEmpty() || !m_flatpakUpdates.isEmpty()
        || !m_pluginUpdates.isEmpty() || !m_miseUpdates.isEmpty()) {
        m_model.applyUpdates(m_pacmanUpdates, m_aurUpdates, m_flatpakUpdates, m_pluginUpdates, m_miseUpdates);
    }

    m_loading = false;
    emit stateChanged();

    if (m_checkAfterLoad)
        checkUpdates();
}

void Backend::checkUpdates() {
    if (m_checkingUpdates)
        return;

    m_checkingUpdates = true;
    m_pendingUpdateTasks = 0;
    m_pacmanUpdates.clear();
    m_aurUpdates.clear();
    m_flatpakUpdates.clear();
    m_pluginUpdates.clear();
    m_miseUpdates.clear();
    emit stateChanged();

    auto launchCheck = [&](const QString &program, const QStringList &args,
                           std::function<void(const QString &)> handler,
                           const QMap<QString, QString> &extraEnv = {}) {
        ++m_pendingUpdateTasks;
        QProcess *proc = spawnAsync(this, program, args, true, extraEnv);
        connect(proc, &QProcess::finished, this, [this, proc, handler](int, QProcess::ExitStatus) {
            handler(QString::fromUtf8(proc->readAllStandardOutput()));
            proc->deleteLater();
            finishUpdateCheckStep();
        });
        connect(proc, &QProcess::errorOccurred, this, [this, proc](QProcess::ProcessError) {
            proc->deleteLater();
            finishUpdateCheckStep();
        });
    };

    if (!m_checkupdatesPath.isEmpty()) {
        launchCheck(m_checkupdatesPath, {"--nocolor"}, [this](const QString &out) {
            m_pacmanUpdates = parseUpdatesOutput(out);
        });
    }
    if (!m_yayPath.isEmpty()) {
        launchCheck(m_yayPath, {"-Qua"}, [this](const QString &out) {
            m_aurUpdates = parseUpdatesOutput(out);
        });
    }
    if (!m_flatpakPath.isEmpty()) {
        launchCheck(m_flatpakPath, {"remote-ls", "--updates", "--app", "--columns=application,version"},
                    [this](const QString &out) {
                        m_flatpakUpdates = parseFlatpakUpdates(out);
                    });
    }
    if (!m_misePath.isEmpty()) {
        launchCheck(m_misePath, {"-C", QDir::homePath(), "outdated", "--json"},
                    [this](const QString &out) {
                        m_miseUpdates = parseMiseOutdated(out);
                    },
                    {{"MISE_MINIMUM_RELEASE_AGE", "0"}});
    }
    if (!m_gitPath.isEmpty()) {
        const QMap<QString, QString> gitEnv{
            {"GIT_TERMINAL_PROMPT", "0"},
            {"GIT_SSH_COMMAND", "ssh -oBatchMode=yes"},
        };
        for (auto it = m_pluginMeta.cbegin(); it != m_pluginMeta.cend(); ++it) {
            const QString pluginId = it.key();
            const PluginMeta meta = it.value();
            if (!meta.isGit || meta.localHead.isEmpty() || meta.remoteUrl.isEmpty())
                continue;
            launchCheck(m_gitPath, {"-C", meta.dirPath, "ls-remote", "origin", "HEAD"},
                        [this, pluginId, meta](const QString &out) {
                            const QString remoteSha = parsePluginRemoteHead(out);
                            if (!remoteSha.isEmpty() && remoteSha != meta.localHead
                                && !isGitAncestor(m_gitPath, meta.dirPath, remoteSha)) {
                                m_pluginUpdates.insert(pluginId, remoteSha.left(7));
                            }
                        },
                        gitEnv);
        }
    }

    if (m_pendingUpdateTasks == 0)
        finishUpdateCheckStep();
}

void Backend::finishUpdateCheckStep() {
    --m_pendingUpdateTasks;
    if (m_pendingUpdateTasks > 0)
        return;

    m_model.applyUpdates(m_pacmanUpdates, m_aurUpdates, m_flatpakUpdates, m_pluginUpdates, m_miseUpdates);
    m_checkingUpdates = false;
    m_lastCheckedEpoch = QDateTime::currentSecsSinceEpoch();
    emit stateChanged();

    // Keep the Omarchy bar widget indicator in sync when updates finish or clear.
    const QString omarchyShell = QStandardPaths::findExecutable("omarchy-shell");
    if (!omarchyShell.isEmpty())
        QProcess::startDetached(omarchyShell, {"marciobbj.omapkg", "refresh"});
}

PackageUpdateStrategy Backend::planPackageUpdate(const QString &source, bool hasPendingSystemUpdates) {
    // Arch Linux does not support partial upgrades: every Omarchy or Pacman
    // package update must go through Omarchy's full system update flow.
    if (source == "omarchy" || source == "pacman")
        return PackageUpdateStrategy::OmarchyFullUpdate;
    if (source == "aur") {
        // AUR packages should be built against an upgraded system; when system
        // updates are pending, route through Omarchy's full update flow (which
        // upgrades system packages first and then runs omarchy-update-aur-pkgs).
        return hasPendingSystemUpdates
            ? PackageUpdateStrategy::OmarchyFullUpdate
            : PackageUpdateStrategy::AurPackageUpdate;
    }
    if (source == "flatpak")
        return PackageUpdateStrategy::FlatpakPackageUpdate;
    if (source == "omarchy-plugin")
        return PackageUpdateStrategy::PluginPackageUpdate;
    if (source == "mise")
        return PackageUpdateStrategy::MisePackageUpdate;
    return PackageUpdateStrategy::None;
}

SystemUpdateLaunch Backend::resolveOmarchyUpdateLaunch(const QString &omarchyTerminalPath,
                                                       const QString &omarchyUpdatePath,
                                                       const QString &xdgTerminalPath) {
    if (omarchyUpdatePath.isEmpty())
        return {};
    if (!omarchyTerminalPath.isEmpty())
        return {omarchyTerminalPath, {"omarchy-update"}};
    if (!xdgTerminalPath.isEmpty())
        return {xdgTerminalPath, {"-e", omarchyUpdatePath}};
    return {};
}

bool Backend::hasPendingSystemUpdates() const {
    for (const auto &pkg : m_model.allPackages()) {
        if (pkg.hasUpdate && (pkg.source == "omarchy" || pkg.source == "pacman"))
            return true;
    }
    return false;
}

bool Backend::launchOmarchySystemUpdate(const QString &title) {
    const auto launch = resolveOmarchyUpdateLaunch(
        m_omarchyTerminalPath, m_omarchyUpdatePath, m_xdgTerminalPath);
    if (launch.program.isEmpty()) {
        m_errorMessage = tr("omarchy-update is not available on this system.");
        emit stateChanged();
        return false;
    }

    m_errorMessage.clear();
    if (!m_logText.isEmpty())
        appendLog("\n");
    appendLog("==> " + title + "\n");
    appendLog("$ " + QFileInfo(launch.program).fileName() + " " + launch.arguments.join(' ') + "\n");

    if (!QProcess::startDetached(launch.program, launch.arguments)) {
        m_errorMessage = tr("Failed to launch omarchy-update.");
        appendLog(m_errorMessage + "\n");
        emit stateChanged();
        return false;
    }

    m_lastOperationSucceeded = true;
    m_statusMessage = tr("Updating Omarchy and Pacman packages");
    emit stateChanged();
    emit operationChanged();
    return true;
}

void Backend::updatePackage(const QString &pkgId) {
    if (m_busy)
        return;

    const PackageItem *pkg = m_model.findById(pkgId);
    if (!pkg)
        return;

    const PackageUpdateStrategy strategy = planPackageUpdate(pkg->source, hasPendingSystemUpdates());
    if (strategy == PackageUpdateStrategy::OmarchyFullUpdate) {
        launchOmarchySystemUpdate(tr("Updating Omarchy and Pacman packages"));
        return;
    }

    QList<QueuedCommand> commands;
    if (strategy == PackageUpdateStrategy::AurPackageUpdate) {
        if (m_yayPath.isEmpty() || m_pkexecPath.isEmpty()) {
            m_errorMessage = tr("yay and pkexec are required to update AUR packages.");
            emit stateChanged();
            return;
        }
        commands.append({
            m_yayPath,
            {"--sudo", m_pkexecPath, "--sudoflags", "", "-S", "--noconfirm", "--aur", "--cleanafter", pkg->name},
            tr("Updating %1 from AUR").arg(pkg->displayName),
            true,
            false
        });
    } else if (strategy == PackageUpdateStrategy::FlatpakPackageUpdate) {
        if (m_flatpakPath.isEmpty())
            return;
        commands.append({
            m_flatpakPath,
            {"update", "-y", pkg->name},
            tr("Updating %1").arg(pkg->displayName),
            false,
            false
        });
    } else if (strategy == PackageUpdateStrategy::PluginPackageUpdate) {
        if (m_omarchyPluginUpdatePath.isEmpty()) {
            m_errorMessage = tr("omarchy-plugin-update is not available on this system.");
            emit stateChanged();
            return;
        }
        commands.append({
            m_omarchyPluginUpdatePath,
            {pkg->name, "--yes"},
            tr("Updating %1").arg(pkg->displayName),
            false,
            false,
            {{"GIT_TERMINAL_PROMPT", "0"}, {"GIT_SSH_COMMAND", "ssh -oBatchMode=yes"}}
        });
    } else if (strategy == PackageUpdateStrategy::MisePackageUpdate) {
        if (m_misePath.isEmpty())
            return;
        commands.append({
            m_misePath,
            {"-C", QDir::homePath(), "upgrade", pkg->name},
            tr("Updating %1").arg(pkg->displayName),
            false,
            false,
            {{"MISE_MINIMUM_RELEASE_AGE", "0"}}
        });
    }

    if (!commands.isEmpty())
        enqueueOperation(tr("Updating %1").arg(pkg->displayName), std::move(commands));
}

void Backend::updateAll() {
    if (m_busy)
        return;

    bool needSystem = false;
    bool needAur = false;
    bool needFlatpak = false;
    bool needMise = false;
    QStringList outdatedPlugins;
    for (const auto &pkg : m_model.allPackages()) {
        if (!pkg.hasUpdate)
            continue;
        if (pkg.source == "omarchy" || pkg.source == "pacman")
            needSystem = true;
        else if (pkg.source == "aur")
            needAur = true;
        else if (pkg.source == "flatpak")
            needFlatpak = true;
        else if (pkg.source == "omarchy-plugin")
            outdatedPlugins.append(pkg.name);
        else if (pkg.source == "mise")
            needMise = true;
    }

    // Route system package updates through Omarchy's full update flow (`omarchy-update`),
    // which handles snapshots, keyrings, full `-Syu`, migrations, hooks, mise updates,
    // AUR rebuilds, and boot verification together.
    if (needSystem) {
        if (!launchOmarchySystemUpdate(tr("Updating all packages")))
            return;
        // omarchy-update runs omarchy-update-mise and omarchy-update-aur-pkgs after
        // upgrading system packages, so avoid invoking them concurrently.
        needAur = false;
        needMise = false;
    }

    QList<QueuedCommand> commands;
    if (needAur && !m_yayPath.isEmpty() && !m_pkexecPath.isEmpty()) {
        commands.append({
            m_yayPath,
            {"--sudo", m_pkexecPath, "--sudoflags", "", "-Sua", "--noconfirm", "--cleanafter",
             "--ignore", "gcc14,gcc14-libs"},
            tr("Updating AUR packages"),
            true,
            false
        });
    }
    if (needFlatpak && !m_flatpakPath.isEmpty()) {
        commands.append({
            m_flatpakPath,
            {"update", "-y"},
            tr("Updating Flatpak apps"),
            false,
            false
        });
    }
    if (!outdatedPlugins.isEmpty() && !m_omarchyPluginUpdatePath.isEmpty()) {
        for (const QString &pluginId : outdatedPlugins) {
            commands.append({
                m_omarchyPluginUpdatePath,
                {pluginId, "--yes"},
                tr("Updating Omarchy plugins"),
                false,
                false,
                {{"GIT_TERMINAL_PROMPT", "0"}, {"GIT_SSH_COMMAND", "ssh -oBatchMode=yes"}}
            });
        }
    }
    if (needMise && !m_misePath.isEmpty()) {
        commands.append({
            m_misePath,
            {"-C", QDir::homePath(), "upgrade"},
            tr("Updating Mise packages"),
            false,
            false,
            {{"MISE_MINIMUM_RELEASE_AGE", "0"}}
        });
    }

    if (!commands.isEmpty())
        enqueueOperation(tr("Updating all packages"), std::move(commands));
}

void Backend::removePackage(const QString &pkgId) {
    if (m_busy)
        return;

    const PackageItem *pkg = m_model.findById(pkgId);
    if (!pkg)
        return;

    QList<QueuedCommand> commands;
    if (pkg->source == "flatpak") {
        if (m_flatpakPath.isEmpty())
            return;
        commands.append({
            m_flatpakPath,
            {"uninstall", "-y", pkg->name},
            tr("Removing %1").arg(pkg->displayName),
            false,
            false
        });
    } else if (pkg->source == "omarchy-plugin") {
        if (m_omarchyPluginRemovePath.isEmpty()) {
            m_errorMessage = tr("omarchy-plugin-remove is not available on this system.");
            emit stateChanged();
            return;
        }
        commands.append({
            m_omarchyPluginRemovePath,
            {pkg->name, "--yes"},
            tr("Removing %1").arg(pkg->displayName),
            false,
            false
        });
    } else if (pkg->source == "mise") {
        if (m_misePath.isEmpty())
            return;
        const MiseToolMeta meta = m_miseMeta.value(pkg->name);
        if (!meta.configPath.isEmpty()) {
            commands.append({
                m_misePath,
                {"-C", QDir::homePath(), "unuse", "--path", meta.configPath, pkg->name},
                tr("Removing %1").arg(pkg->displayName),
                false,
                false
            });
        } else {
            commands.append({
                m_misePath,
                {"-C", QDir::homePath(), "uninstall", "--all", pkg->name},
                tr("Removing %1").arg(pkg->displayName),
                false,
                false
            });
        }
    } else {
        if (m_pkexecPath.isEmpty()) {
            m_errorMessage = tr("pkexec is not available for privileged package actions.");
            emit stateChanged();
            return;
        }
        commands.append({
            m_pkexecPath,
            {m_pacmanPath, "-Rns", "--noconfirm", pkg->name},
            tr("Removing %1").arg(pkg->displayName),
            false,
            false
        });
    }

    enqueueOperation(tr("Removing %1").arg(pkg->displayName), std::move(commands));
}

void Backend::cleanOrphans() {
    if (m_busy)
        return;

    QStringList orphanNames;
    for (const auto &pkg : m_model.allPackages())
        if (pkg.isOrphan)
            orphanNames.append(pkg.name);

    if (orphanNames.isEmpty())
        return;

    if (m_pkexecPath.isEmpty()) {
        m_errorMessage = tr("pkexec is not available for privileged package actions.");
        emit stateChanged();
        return;
    }

    QStringList args{m_pacmanPath, "-Rns", "--noconfirm"};
    args.append(orphanNames);
    enqueueOperation(tr("Cleaning unused orphans"), {{m_pkexecPath, args, tr("Removing orphaned packages"), false, false}});
}

void Backend::enqueueOperation(const QString &title, QList<QueuedCommand> commands) {
    m_errorMessage.clear();
    m_busy = true;
    m_operationFailed = false;
    m_operationCancelled = false;
    m_lastOperationSucceeded = true;
    m_operationTitle = title;
    m_statusMessage = title;
    m_commandQueue.clear();
    for (auto &cmd : commands)
        m_commandQueue.enqueue(std::move(cmd));

    if (!m_logText.isEmpty())
        appendLog("\n");
    appendLog("==> " + title + "\n");
    emit stateChanged();
    emit operationChanged();
    runNextCommand();
}

void Backend::runNextCommand() {
    if (m_commandQueue.isEmpty() || m_operationFailed || m_operationCancelled) {
        m_busy = false;
        m_lastOperationSucceeded = !m_operationFailed && !m_operationCancelled;
        if (m_operationCancelled)
            m_statusMessage = tr("Operation cancelled");
        else if (m_operationFailed)
            m_statusMessage = tr("Operation failed — inspect output for details");
        else
            m_statusMessage = tr("Completed successfully");
        emit operationChanged();
        reloadPackages(true);
        return;
    }

    const QueuedCommand cmd = m_commandQueue.dequeue();
    m_statusMessage = cmd.label;
    appendLog("$ " + QFileInfo(cmd.program).fileName() + " " + cmd.arguments.join(' ') + "\n");
    emit operationChanged();

    m_opProcess = new QProcess(this);
    m_opProcess->setProcessChannelMode(QProcess::MergedChannels);

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    if (cmd.stripGoEnv) {
        // Matches omarchy-update-aur-pkgs: mise exports GOROOT/GOBIN which breaks Go PKGBUILDs.
        env.remove("GOROOT");
        env.remove("GOBIN");
    }
    if (cmd.forceCLocale)
        env.insert("LC_ALL", "C");
    for (auto it = cmd.extraEnv.cbegin(); it != cmd.extraEnv.cend(); ++it)
        env.insert(it.key(), it.value());
    m_opProcess->setProcessEnvironment(env);
    if (!cmd.workingDirectory.isEmpty())
        m_opProcess->setWorkingDirectory(cmd.workingDirectory);

    connect(m_opProcess, &QProcess::readyReadStandardOutput, this, [this]() {
        if (m_opProcess)
            appendLog(QString::fromUtf8(m_opProcess->readAllStandardOutput()));
    });
    connect(m_opProcess, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus status) {
        if (m_opProcess) {
            const QByteArray trailing = m_opProcess->readAllStandardOutput();
            if (!trailing.isEmpty())
                appendLog(QString::fromUtf8(trailing));
            m_opProcess->deleteLater();
            m_opProcess = nullptr;
        }
        if (status != QProcess::NormalExit || exitCode != 0) {
            m_operationFailed = true;
            if (exitCode == 126 || exitCode == 127)
                appendLog(tr("Authentication was dismissed or the command could not be authorized.") + "\n");
            else
                appendLog(tr("Process exited with code %1.").arg(exitCode) + "\n");
        }
        runNextCommand();
    });
    connect(m_opProcess, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        if (m_opProcess) {
            appendLog(m_opProcess->errorString() + "\n");
            m_opProcess->deleteLater();
            m_opProcess = nullptr;
        }
        m_operationFailed = true;
        runNextCommand();
    });

    m_opProcess->start(cmd.program, cmd.arguments);
}

void Backend::cancelOperation() {
    if (!m_busy)
        return;
    m_operationCancelled = true;
    m_commandQueue.clear();
    if (m_opProcess && m_opProcess->state() != QProcess::NotRunning) {
        appendLog("\n" + tr("Cancelling...") + "\n");
        m_opProcess->terminate();
    }
}

void Backend::clearLog() {
    m_logText.clear();
    emit logChanged();
}

void Backend::appendLog(const QString &text) {
    m_logText.append(text);
    // Cap log buffer so long AUR builds stay snappy.
    constexpr int maxChars = 120000;
    if (m_logText.size() > maxChars)
        m_logText = m_logText.right(maxChars);
    emit logChanged();
}

void Backend::loadPackageDetails(const QString &pkgId) {
    if (m_detailsProcess) {
        m_detailsProcess->disconnect(this);
        if (m_detailsProcess->state() != QProcess::NotRunning) {
            m_detailsProcess->kill();
            m_detailsProcess->waitForFinished(300);
        }
        m_detailsProcess->deleteLater();
        m_detailsProcess = nullptr;
    }

    m_detailsPkgId = pkgId;
    m_selectedDetails.clear();

    const PackageItem *pkg = m_model.findById(pkgId);
    if (!pkg) {
        m_loadingDetails = false;
        emit detailsChanged();
        return;
    }

    if (pkg->source == "omarchy-plugin") {
        const PluginMeta meta = m_pluginMeta.value(pkg->name);
        if (!meta.license.isEmpty())
            m_selectedDetails.insert("licenses", meta.license);
        if (!meta.author.isEmpty())
            m_selectedDetails.insert("packager", meta.author);
        m_selectedDetails.insert("pluginEnabled", meta.enabled);
        m_selectedDetails.insert("pluginIsGit", meta.isGit);
        if (!meta.localHead.isEmpty())
            m_selectedDetails.insert("commit", meta.localHead.left(7));
        m_selectedDetails.insert("dependsOn", QStringList{});
        m_selectedDetails.insert("requiredBy", QStringList{});
        m_loadingDetails = false;
        emit detailsChanged();
        return;
    }

    if (pkg->source == "mise") {
        const MiseToolMeta meta = m_miseMeta.value(pkg->name);
        if (!meta.configPath.isEmpty()) {
            QString cfg = meta.configPath;
            const QString home = QDir::homePath();
            if (!home.isEmpty() && cfg.startsWith(home + "/"))
                cfg = "~" + cfg.mid(home.size());
            m_selectedDetails.insert("configPath", cfg);
        }
        if (!meta.requestedVersion.isEmpty())
            m_selectedDetails.insert("requestedVersion", meta.requestedVersion);
        m_selectedDetails.insert("installedVersions", meta.installedVersions);
        m_selectedDetails.insert("dependsOn", QStringList{});
        m_selectedDetails.insert("requiredBy", QStringList{});
        m_loadingDetails = false;
        emit detailsChanged();
        return;
    }

    m_loadingDetails = true;
    emit detailsChanged();

    const bool isFlatpak = (pkg->source == "flatpak");
    const QString program = isFlatpak ? m_flatpakPath : m_pacmanPath;
    const QStringList args = isFlatpak ? QStringList{"info", pkg->name} : QStringList{"-Qi", pkg->name};
    if (program.isEmpty()) {
        m_loadingDetails = false;
        emit detailsChanged();
        return;
    }

    m_detailsProcess = spawnAsync(this, program, args, true);
    connect(m_detailsProcess, &QProcess::finished, this, [this, pkgId, isFlatpak](int, QProcess::ExitStatus) {
        if (!m_detailsProcess)
            return;
        const QString out = QString::fromUtf8(m_detailsProcess->readAllStandardOutput());
        m_detailsProcess->deleteLater();
        m_detailsProcess = nullptr;
        if (pkgId != m_detailsPkgId)
            return;
        m_selectedDetails = isFlatpak ? parseFlatpakDetails(out) : parsePacmanDetails(out);
        m_loadingDetails = false;
        emit detailsChanged();
    });
    connect(m_detailsProcess, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        if (m_detailsProcess) {
            m_detailsProcess->deleteLater();
            m_detailsProcess = nullptr;
        }
        m_loadingDetails = false;
        emit detailsChanged();
    });
}

void Backend::launchApp(const QString &pkgId) {
    const PackageItem *pkg = m_model.findById(pkgId);
    if (!pkg)
        return;

    if (pkg->source == "flatpak" && !m_flatpakPath.isEmpty()) {
        QProcess::startDetached(m_flatpakPath, {"run", pkg->name});
    } else if (!pkg->desktopId.isEmpty() && !m_gtkLaunchPath.isEmpty()) {
        QProcess::startDetached(m_gtkLaunchPath, {pkg->desktopId});
    }
}

QJsonObject Backend::runCliSnapshot(bool includeUpdates) {
    auto runSync = [](const QString &program, const QStringList &args) -> QString {
        if (program.isEmpty())
            return {};
        QProcess proc;
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert("LC_ALL", "C");
        proc.setProcessEnvironment(env);
        proc.start(program, args);
        proc.waitForFinished(30000);
        return QString::fromUtf8(proc.readAllStandardOutput());
    };

    const auto repoMap = parseExpacRepos(runSync(m_expacPath, {"-S", "%r\t%n"}));
    QSet<QString> orphans;
    for (const QString &line : runSync(m_pacmanPath, {"-Qdtq"}).split('\n', Qt::SkipEmptyParts))
        orphans.insert(line.trimmed());
    QSet<QString> omarchyDefaults;
    if (!m_omarchyDefaultsPath.isEmpty())
        for (const QString &line : runSync(m_omarchyDefaultsPath, {}).split('\n', Qt::SkipEmptyParts))
            omarchyDefaults.insert(line.trimmed());

    QHash<QString, qint64> dirSizes;
    if (!m_duPath.isEmpty()) {
        const QStringList dirs = collectDiskUsageDirs(m_pluginsDir);
        if (!dirs.isEmpty()) {
            QStringList duArgs{"-sb"};
            duArgs.append(dirs);
            dirSizes = parseDuSizes(runSync(m_duPath, duArgs));
        }
    }

    m_pluginMeta.clear();
    m_miseMeta.clear();

    QVector<PackageItem> items = parseExpacInstalled(
        runSync(m_expacPath, {"-Q", "%n\t%v\t%m\t%w\t%l\t%u\t%d", "-t", "%s"}),
        repoMap, orphans, omarchyDefaults);
    if (!m_flatpakPath.isEmpty())
        items.append(parseFlatpakList(runSync(m_flatpakPath,
            {"list", "--app", "--columns=application,name,version,size,description,origin"})));
    if (!m_pluginsDir.isEmpty()) {
        const QString pluginListJson = m_omarchyPluginListPath.isEmpty()
                                           ? QString()
                                           : runSync(m_omarchyPluginListPath, {"--json"});
        items.append(parseOmarchyPlugins(m_pluginsDir, pluginListJson, dirSizes, &m_pluginMeta));
    }
    if (!m_misePath.isEmpty()) {
        items.append(parseMiseInstalled(
            runSync(m_misePath, {"-C", QDir::homePath(), "ls", "--json"}), dirSizes, &m_miseMeta));
    }
    enrichWithDesktopEntries(items);
    m_model.setPackages(std::move(items));

    if (includeUpdates) {
        auto startAsync = [](const QString &program, const QStringList &args,
                             const QMap<QString, QString> &extraEnv = {}) -> QProcess * {
            if (program.isEmpty())
                return nullptr;
            auto *proc = new QProcess();
            QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
            env.insert("LC_ALL", "C");
            for (auto it = extraEnv.cbegin(); it != extraEnv.cend(); ++it)
                env.insert(it.key(), it.value());
            proc->setProcessEnvironment(env);
            proc->start(program, args);
            return proc;
        };
        auto finishAsync = [](QProcess *proc) -> QString {
            if (!proc)
                return {};
            proc->waitForFinished(30000);
            const QString out = QString::fromUtf8(proc->readAllStandardOutput());
            delete proc;
            return out;
        };

        QProcess *pacProc = startAsync(m_checkupdatesPath, {"--nocolor"});
        QProcess *aurProc = startAsync(m_yayPath, {"-Qua"});
        QProcess *fpProc = startAsync(m_flatpakPath,
            {"remote-ls", "--updates", "--app", "--columns=application,version"});
        QProcess *miseProc = startAsync(m_misePath,
            {"-C", QDir::homePath(), "outdated", "--json"},
            {{"MISE_MINIMUM_RELEASE_AGE", "0"}});

        struct PluginCheckJob {
            QString id;
            PluginMeta meta;
            QProcess *proc = nullptr;
        };
        QVector<PluginCheckJob> pluginJobs;
        if (!m_gitPath.isEmpty()) {
            const QMap<QString, QString> gitEnv{
                {"GIT_TERMINAL_PROMPT", "0"},
                {"GIT_SSH_COMMAND", "ssh -oBatchMode=yes"},
            };
            for (auto it = m_pluginMeta.cbegin(); it != m_pluginMeta.cend(); ++it) {
                const PluginMeta &meta = it.value();
                if (!meta.isGit || meta.localHead.isEmpty() || meta.remoteUrl.isEmpty())
                    continue;
                pluginJobs.append({
                    it.key(),
                    meta,
                    startAsync(m_gitPath, {"-C", meta.dirPath, "ls-remote", "origin", "HEAD"}, gitEnv)
                });
            }
        }

        const auto pacUpdates = parseUpdatesOutput(finishAsync(pacProc));
        const auto aurUpdates = parseUpdatesOutput(finishAsync(aurProc));
        const auto fpUpdates = parseFlatpakUpdates(finishAsync(fpProc));
        const auto miseUpdates = parseMiseOutdated(finishAsync(miseProc));
        QHash<QString, QString> pluginUpdates;
        for (const auto &job : pluginJobs) {
            const QString remoteSha = parsePluginRemoteHead(finishAsync(job.proc));
            if (!remoteSha.isEmpty() && remoteSha != job.meta.localHead
                && !isGitAncestor(m_gitPath, job.meta.dirPath, remoteSha)) {
                pluginUpdates.insert(job.id, remoteSha.left(7));
            }
        }
        m_model.applyUpdates(pacUpdates, aurUpdates, fpUpdates, pluginUpdates, miseUpdates);
    }

    QJsonArray arr;
    for (const auto &pkg : m_model.allPackages()) {
        arr.append(QJsonObject{
            {"id", pkg.id},
            {"name", pkg.name},
            {"displayName", pkg.displayName},
            {"version", pkg.version},
            {"availableVersion", pkg.availableVersion},
            {"source", pkg.source},
            {"repository", pkg.repository},
            {"sizeBytes", pkg.sizeBytes},
            {"explicit", pkg.explicitInstall},
            {"orphan", pkg.isOrphan},
            {"hasUpdate", pkg.hasUpdate},
            {"description", pkg.description},
        });
    }

    return QJsonObject{
        {"totalPackages", m_model.totalCount()},
        {"updatesCount", m_model.updatesCount()},
        {"orphanCount", m_model.orphanCount()},
        {"totalBytes", m_model.totalBytes()},
        {"omarchyBytes", m_model.omarchyBytes()},
        {"pacmanBytes", m_model.pacmanBytes()},
        {"aurBytes", m_model.aurBytes()},
        {"flatpakBytes", m_model.flatpakBytes()},
        {"pluginBytes", m_model.pluginBytes()},
        {"miseBytes", m_model.miseBytes()},
        {"packages", arr},
    };
}
