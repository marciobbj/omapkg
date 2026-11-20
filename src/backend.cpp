#include "backend.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
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
                     bool forceCLocale = true) {
    auto *proc = new QProcess(parent);
    if (forceCLocale) {
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert("LC_ALL", "C");
        proc->setProcessEnvironment(env);
    }
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
    m_pkexecPath = QStandardPaths::findExecutable("pkexec");
    m_omarchyDefaultsPath = QStandardPaths::findExecutable("omarchy-pkg-defaults");
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
    m_pendingLoadTasks = 0;
    emit stateChanged();

    auto launchTask = [&](const QString &program, const QStringList &args, QString *target) {
        ++m_pendingLoadTasks;
        QProcess *proc = spawnAsync(this, program, args, true);
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

    QVector<PackageItem> items = parseExpacInstalled(m_expacQOut, repoMap, orphans, omarchyDefaults);
    if (!m_flatpakListOut.isEmpty())
        items.append(parseFlatpakList(m_flatpakListOut));

    enrichWithDesktopEntries(items);
    m_model.setPackages(std::move(items));

    // Preserve any already-discovered update markers until the next update check completes.
    if (!m_pacmanUpdates.isEmpty() || !m_aurUpdates.isEmpty() || !m_flatpakUpdates.isEmpty())
        m_model.applyUpdates(m_pacmanUpdates, m_aurUpdates, m_flatpakUpdates);

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
    emit stateChanged();

    auto launchCheck = [&](const QString &program, const QStringList &args,
                           std::function<void(const QString &)> handler) {
        ++m_pendingUpdateTasks;
        QProcess *proc = spawnAsync(this, program, args, true);
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

    if (m_pendingUpdateTasks == 0)
        finishUpdateCheckStep();
}

void Backend::finishUpdateCheckStep() {
    --m_pendingUpdateTasks;
    if (m_pendingUpdateTasks > 0)
        return;

    m_model.applyUpdates(m_pacmanUpdates, m_aurUpdates, m_flatpakUpdates);
    m_checkingUpdates = false;
    m_lastCheckedEpoch = QDateTime::currentSecsSinceEpoch();
    emit stateChanged();

    // Keep the Omarchy bar widget indicator in sync when updates finish or clear.
    const QString omarchyShell = QStandardPaths::findExecutable("omarchy-shell");
    if (!omarchyShell.isEmpty())
        QProcess::startDetached(omarchyShell, {"marciobbj.omapkg", "refresh"});
}

void Backend::updatePackage(const QString &pkgId) {
    if (m_busy)
        return;

    const PackageItem *pkg = m_model.findById(pkgId);
    if (!pkg)
        return;

    QList<QueuedCommand> commands;
    if (pkg->source == "omarchy" || pkg->source == "pacman") {
        if (m_pkexecPath.isEmpty()) {
            m_errorMessage = tr("pkexec is not available for privileged package actions.");
            emit stateChanged();
            return;
        }
        commands.append({
            m_pkexecPath,
            {m_pacmanPath, "-Sy", "--noconfirm", "--overwrite", "/usr/share/omarchy/*", pkg->name},
            tr("Updating %1").arg(pkg->displayName),
            false,
            false
        });
    } else if (pkg->source == "aur") {
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
    } else if (pkg->source == "flatpak") {
        if (m_flatpakPath.isEmpty())
            return;
        commands.append({
            m_flatpakPath,
            {"update", "-y", pkg->name},
            tr("Updating %1").arg(pkg->displayName),
            false,
            false
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
    for (const auto &pkg : m_model.allPackages()) {
        if (!pkg.hasUpdate)
            continue;
        if (pkg.source == "omarchy" || pkg.source == "pacman")
            needSystem = true;
        else if (pkg.source == "aur")
            needAur = true;
        else if (pkg.source == "flatpak")
            needFlatpak = true;
    }

    QList<QueuedCommand> commands;
    if (needSystem) {
        if (m_pkexecPath.isEmpty()) {
            m_errorMessage = tr("pkexec is not available for privileged package actions.");
            emit stateChanged();
            return;
        }
        commands.append({
            m_pkexecPath,
            {m_pacmanPath, "-Syu", "--noconfirm", "--overwrite", "/usr/share/omarchy/*"},
            tr("Updating Omarchy and Pacman packages"),
            false,
            false
        });
    }
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
    m_opProcess->setProcessEnvironment(env);

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

    QVector<PackageItem> items = parseExpacInstalled(
        runSync(m_expacPath, {"-Q", "%n\t%v\t%m\t%w\t%l\t%u\t%d", "-t", "%s"}),
        repoMap, orphans, omarchyDefaults);
    if (!m_flatpakPath.isEmpty())
        items.append(parseFlatpakList(runSync(m_flatpakPath,
            {"list", "--app", "--columns=application,name,version,size,description,origin"})));
    enrichWithDesktopEntries(items);
    m_model.setPackages(std::move(items));

    if (includeUpdates) {
        auto startAsync = [](const QString &program, const QStringList &args) -> QProcess * {
            if (program.isEmpty())
                return nullptr;
            auto *proc = new QProcess();
            QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
            env.insert("LC_ALL", "C");
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

        const auto pacUpdates = parseUpdatesOutput(finishAsync(pacProc));
        const auto aurUpdates = parseUpdatesOutput(finishAsync(aurProc));
        const auto fpUpdates = parseFlatpakUpdates(finishAsync(fpProc));
        m_model.applyUpdates(pacUpdates, aurUpdates, fpUpdates);
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
        {"packages", arr},
    };
}
