#pragma once

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVector>

#include "packagemodel.h"

struct PluginMeta;
struct MiseToolMeta;

struct OsvQueryTarget {
    QString pkgId;
    QString toolName;
    QString ecosystem;
    QString packageName;
    QString version;
};

namespace Security {

// Severity helpers ("critical" > "high" > "medium" > "low").
int severityRank(const QString &severity);
QString normalizeSeverity(const QString &raw);
QString highestSeverity(const QVector<SecurityAdvisory> &advisories);
QVariantList advisoriesToVariantList(const QVector<SecurityAdvisory> &advisories);
QString sanitizeHttpUrl(const QString &rawUrl);
QHash<QString, QString> parseMiseRegistry(const QString &registryOutput);

// Pure Arch Linux alpm_pkg_vercmp-compatible version comparison.
// Returns < 0 if a < b, 0 if a == b, > 0 if a > b.
int compareArchVersions(const QString &a, const QString &b);

// Match installed Omarchy/Pacman packages against https://security.archlinux.org/json.
QHash<QString, QVector<SecurityAdvisory>> matchArchAdvisories(
    const QByteArray &archSecurityJson,
    const QVector<PackageItem> &installedPackages);

// Match installed Omarchy Plugins against omacom/omarchy-plugin-marketplace registry.json.
QHash<QString, QVector<SecurityAdvisory>> matchMarketplacePlugins(
    const QByteArray &registryJson,
    const QVector<PackageItem> &installedPackages,
    const QHash<QString, PluginMeta> &pluginMeta);

// Resolve a Mise tool + optional `mise registry <tool>` backend line into an OSV target.
OsvQueryTarget resolveMiseOsvTarget(const PackageItem &pkg, const QString &registryLine);

// Build the JSON payload for POST https://api.osv.dev/v1/querybatch.
QByteArray buildOsvBatchRequest(const QVector<OsvQueryTarget> &targets);

// Parse the response from POST https://api.osv.dev/v1/querybatch.
QHash<QString, QVector<SecurityAdvisory>> parseOsvBatchResponse(
    const QByteArray &responseJson,
    const QVector<OsvQueryTarget> &targets);

// Enrich a single OSV advisory using GET https://api.osv.dev/v1/vulns/<id> JSON.
void enrichOsvAdvisory(SecurityAdvisory &adv, const QByteArray &vulnJson);

// Merge advisory maps cleanly without duplicate advisory IDs per package.
void mergeAdvisories(QHash<QString, QVector<SecurityAdvisory>> &dest,
                     const QHash<QString, QVector<SecurityAdvisory>> &src);
void sortAdvisoriesMap(QHash<QString, QVector<SecurityAdvisory>> &advisoriesByPkgId);

// File cache & ETag helpers under ${XDG_CACHE_HOME:-~/.cache}/omapkg/.
QString cacheDirectory();
QString stateDirectory();
bool readFreshCache(const QString &filePath, qint64 maxAgeSecs, QByteArray *outData);
bool writeCacheFile(const QString &filePath, const QByteArray &data);
bool touchCacheFile(const QString &filePath);
QByteArray readCacheEtag(const QString &filePath);
void writeCacheEtag(const QString &filePath, const QByteArray &etag);
bool readFreshKeyedCache(const QString &filePath, const QString &expectedKey, qint64 maxAgeSecs, QByteArray *outData);
bool writeKeyedCache(const QString &filePath, const QString &key, const QByteArray &data);

// Desktop notification deduplication & dispatch via `notify-send`.
// Returns the number of newly alerted packages (or 0 if all were already notified).
int notifyNewAdvisories(const QVector<PackageItem> &packages,
                        const QHash<QString, QVector<SecurityAdvisory>> &advisoriesByPkgId,
                        const QString &stateFilePath = {},
                        const QString &notifySendPath = {},
                        bool dispatchDesktopNotification = true);

} // namespace Security
