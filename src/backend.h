#pragma once

#include <QHash>
#include <QJsonObject>
#include <QMap>
#include <QNetworkAccessManager>
#include <QObject>
#include <QProcess>
#include <QQueue>
#include <QSet>
#include <QStringList>
#include <QVariantMap>

#include "packagemodel.h"

struct DesktopEntryInfo {
    QString desktopId;
    QString displayName;
    QString iconName;
    QString execBinary;
};

struct QueuedCommand {
    QString program;
    QStringList arguments;
    QString label;
    bool stripGoEnv = false;
    bool forceCLocale = false;
    QMap<QString, QString> extraEnv = {};
    QString workingDirectory = {};

    QueuedCommand() = default;
    QueuedCommand(QString prog, QStringList args, QString lbl,
                  bool stripGo = false, bool forceC = false,
                  QMap<QString, QString> env = {}, QString cwd = {})
        : program(std::move(prog)),
          arguments(std::move(args)),
          label(std::move(lbl)),
          stripGoEnv(stripGo),
          forceCLocale(forceC),
          extraEnv(std::move(env)),
          workingDirectory(std::move(cwd)) {}
};

struct PluginMeta {
    QString dirPath;
    QString author;
    QString license;
    QStringList kinds;
    bool enabled = false;
    bool isGit = false;
    QString localHead;
    QString remoteUrl;
};

struct MiseToolMeta {
    QString configPath;
    QString requestedVersion;
    QString activeInstallPath;
    QStringList installedVersions;
};

struct SystemUpdateLaunch {
    QString program;
    QStringList arguments;
};

enum class PackageUpdateStrategy {
    None,
    OmarchyFullUpdate,
    AurPackageUpdate,
    FlatpakPackageUpdate,
    PluginPackageUpdate,
    MisePackageUpdate,
};

class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(QObject *packages READ packages CONSTANT)
    Q_PROPERTY(bool loading READ loading NOTIFY stateChanged)
    Q_PROPERTY(bool checkingUpdates READ checkingUpdates NOTIFY stateChanged)
    Q_PROPERTY(bool checkingSecurity READ checkingSecurity NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY operationChanged)
    Q_PROPERTY(QString operationTitle READ operationTitle NOTIFY operationChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY operationChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY stateChanged)
    Q_PROPERTY(QString logText READ logText NOTIFY logChanged)
    Q_PROPERTY(bool lastOperationSucceeded READ lastOperationSucceeded NOTIFY operationChanged)
    Q_PROPERTY(qint64 lastCheckedEpoch READ lastCheckedEpoch NOTIFY stateChanged)
    Q_PROPERTY(bool hasYay READ hasYay CONSTANT)
    Q_PROPERTY(bool hasFlatpak READ hasFlatpak CONSTANT)
    Q_PROPERTY(bool hasPlugins READ hasPlugins CONSTANT)
    Q_PROPERTY(bool hasMise READ hasMise CONSTANT)
    Q_PROPERTY(bool hasCheckupdates READ hasCheckupdates CONSTANT)
    Q_PROPERTY(QVariantMap selectedDetails READ selectedDetails NOTIFY detailsChanged)
    Q_PROPERTY(bool loadingDetails READ loadingDetails NOTIFY detailsChanged)

public:
    explicit Backend(bool autoLoad = true, QObject *parent = nullptr);
    ~Backend() override;

    PackageModel *packages() { return &m_model; }
    bool loading() const { return m_loading; }
    bool checkingUpdates() const { return m_checkingUpdates; }
    bool checkingSecurity() const { return m_checkingSecurity; }
    bool busy() const { return m_busy; }
    QString operationTitle() const { return m_operationTitle; }
    QString statusMessage() const { return m_statusMessage; }
    QString errorMessage() const { return m_errorMessage; }
    QString logText() const { return m_logText; }
    bool lastOperationSucceeded() const { return m_lastOperationSucceeded; }
    qint64 lastCheckedEpoch() const { return m_lastCheckedEpoch; }
    bool hasYay() const { return !m_yayPath.isEmpty(); }
    bool hasFlatpak() const { return !m_flatpakPath.isEmpty(); }
    bool hasPlugins() const { return !m_pluginsDir.isEmpty(); }
    bool hasMise() const { return !m_misePath.isEmpty(); }
    bool hasCheckupdates() const { return !m_checkupdatesPath.isEmpty(); }
    QVariantMap selectedDetails() const { return m_selectedDetails; }
    bool loadingDetails() const { return m_loadingDetails; }

    Q_INVOKABLE void reloadPackages(bool checkForUpdatesAfter = true);
    Q_INVOKABLE void checkUpdates();
    Q_INVOKABLE void checkSecurity(bool notifyOnNew = true);
    Q_INVOKABLE void updatePackage(const QString &pkgId);
    Q_INVOKABLE void updateAll();
    Q_INVOKABLE void removePackage(const QString &pkgId);
    Q_INVOKABLE void cleanOrphans();
    Q_INVOKABLE void cancelOperation();
    Q_INVOKABLE void clearLog();
    Q_INVOKABLE void loadPackageDetails(const QString &pkgId);
    Q_INVOKABLE void launchApp(const QString &pkgId);

    // Synchronous snapshot helper used by the headless CLI commands.
    QJsonObject runCliSnapshot(bool includeUpdates, bool includeSecurity = false, bool notifySecurity = false);

    // Pure parsing and update-routing helpers exposed for unit testing.
    static QVector<PackageItem> parseExpacInstalled(const QString &output,
                                                    const QHash<QString, QString> &repoByPkg,
                                                    const QSet<QString> &orphans,
                                                    const QSet<QString> &omarchyDefaults);
    static QHash<QString, QString> parseExpacRepos(const QString &output);
    static QHash<QString, QString> parseUpdatesOutput(const QString &output);
    static QVector<PackageItem> parseFlatpakList(const QString &output);
    static QHash<QString, QString> parseFlatpakUpdates(const QString &output);
    static qint64 parseSizeBytes(const QString &humanSize);
    static QVariantMap parsePacmanDetails(const QString &output);
    static QVariantMap parseFlatpakDetails(const QString &output);
    static QHash<QString, qint64> parseDuSizes(const QString &output);
    static QString normalizeGitRemoteUrl(const QString &rawUrl);
    static QString parsePluginRemoteHead(const QString &lsRemoteOutput);
    static QVector<PackageItem> parseOmarchyPlugins(const QString &pluginsDir,
                                                    const QString &listJson,
                                                    const QHash<QString, qint64> &dirSizes = {},
                                                    QHash<QString, PluginMeta> *metaOut = nullptr);
    static QVector<PackageItem> parseMiseInstalled(const QString &jsonOutput,
                                                   const QHash<QString, qint64> &dirSizes = {},
                                                   QHash<QString, MiseToolMeta> *metaOut = nullptr);
    static QHash<QString, QString> parseMiseOutdated(const QString &jsonOutput);
    static QHash<QString, DesktopEntryInfo> scanDesktopEntries(const QStringList &directories);
    static PackageUpdateStrategy planPackageUpdate(const QString &source, bool hasPendingSystemUpdates);
    static SystemUpdateLaunch resolveOmarchyUpdateLaunch(const QString &omarchyTerminalPath,
                                                         const QString &omarchyUpdatePath,
                                                         const QString &xdgTerminalPath);
    static SystemUpdateLaunch resolvePluginUpdateLaunch(const QString &omarchyTerminalPath,
                                                        const QString &omarchyPluginUpdatePath,
                                                        const QString &xdgTerminalPath,
                                                        const QString &pluginId = {});

signals:
    void stateChanged();
    void operationChanged();
    void logChanged();
    void detailsChanged();

private:
    void startReloadStepFinished();
    void finalizePackageLoad();
    void finishUpdateCheckStep();
    void finishSecurityCheckStep();
    bool hasPendingSystemUpdates() const;
    bool launchOmarchySystemUpdate(const QString &title);
    bool launchOmarchyPluginUpdate(const QString &title, const QString &pluginId = {});
    void enqueueOperation(const QString &title, QList<QueuedCommand> commands);
    void runNextCommand();
    void appendLog(const QString &text);
    void enrichWithDesktopEntries(QVector<PackageItem> &items) const;

    PackageModel m_model;

    QString m_expacPath;
    QString m_pacmanPath;
    QString m_checkupdatesPath;
    QString m_yayPath;
    QString m_flatpakPath;
    QString m_misePath;
    QString m_gitPath;
    QString m_duPath;
    QString m_pkexecPath;
    QString m_omarchyDefaultsPath;
    QString m_omarchyUpdatePath;
    QString m_omarchyTerminalPath;
    QString m_xdgTerminalPath;
    QString m_omarchyPluginListPath;
    QString m_omarchyPluginUpdatePath;
    QString m_omarchyPluginRemovePath;
    QString m_pluginsDir;
    QString m_gtkLaunchPath;

    bool m_loading = false;
    bool m_checkAfterLoad = false;
    int m_pendingLoadTasks = 0;
    QString m_expacQOut;
    QString m_expacSOut;
    QString m_orphansOut;
    QString m_defaultsOut;
    QString m_flatpakListOut;
    QString m_pluginListOut;
    QString m_miseListOut;
    QString m_miseRegistryOut;
    QString m_duOut;

    bool m_checkingUpdates = false;
    int m_pendingUpdateTasks = 0;
    QHash<QString, QString> m_pacmanUpdates;
    QHash<QString, QString> m_aurUpdates;
    QHash<QString, QString> m_flatpakUpdates;
    QHash<QString, QString> m_pluginUpdates;
    QHash<QString, QString> m_miseUpdates;
    qint64 m_lastCheckedEpoch = 0;

    bool m_checkingSecurity = false;
    bool m_notifySecurityAfterCheck = false;
    int m_pendingSecurityTasks = 0;
    QHash<QString, QVector<SecurityAdvisory>> m_securityAdvisories;
    QNetworkAccessManager m_nam;

    QHash<QString, PluginMeta> m_pluginMeta;
    QHash<QString, MiseToolMeta> m_miseMeta;
    QHash<QString, QString> m_miseRegistry;

    bool m_busy = false;
    bool m_lastOperationSucceeded = true;
    bool m_operationFailed = false;
    bool m_operationCancelled = false;
    QString m_operationTitle;
    QString m_statusMessage;
    QString m_errorMessage;
    QString m_logText;
    QQueue<QueuedCommand> m_commandQueue;
    QProcess *m_opProcess = nullptr;

    QProcess *m_detailsProcess = nullptr;
    QString m_detailsPkgId;
    QVariantMap m_selectedDetails;
    bool m_loadingDetails = false;
};
