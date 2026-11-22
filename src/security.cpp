#include "security.h"
#include "backend.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QUrl>

#include <algorithm>

namespace Security {

namespace {

int compareVersionPart(const QString &a, const QString &b) {
    if (a == b)
        return 0;

    int i = 0;
    int j = 0;
    const int n = a.size();
    const int m = b.size();

    while (i < n && j < m) {
        // Skip non-alphanumeric separators, tracking if one side has more separators.
        while (i < n && !a[i].isLetterOrNumber())
            ++i;
        while (j < m && !b[j].isLetterOrNumber())
            ++j;
        if (i >= n || j >= m)
            break;

        const bool aNum = a[i].isDigit();
        const bool bNum = b[j].isDigit();

        // In ALPM vercmp, numeric segments are always newer than alpha segments.
        if (aNum != bNum)
            return aNum ? 1 : -1;

        const int startA = i;
        const int startB = j;
        if (aNum) {
            while (i < n && a[i].isDigit())
                ++i;
            while (j < m && b[j].isDigit())
                ++j;
            // Strip leading zeros before numeric comparison.
            int nzA = startA;
            while (nzA < i && a[nzA] == QLatin1Char('0'))
                ++nzA;
            int nzB = startB;
            while (nzB < j && b[nzB] == QLatin1Char('0'))
                ++nzB;
            const int lenA = i - nzA;
            const int lenB = j - nzB;
            if (lenA != lenB)
                return lenA > lenB ? 1 : -1;
            const int cmp = QStringView(a).mid(nzA, lenA).compare(QStringView(b).mid(nzB, lenB));
            if (cmp != 0)
                return cmp > 0 ? 1 : -1;
        } else {
            while (i < n && a[i].isLetter())
                ++i;
            while (j < m && b[j].isLetter())
                ++j;
            const int cmp = QStringView(a).mid(startA, i - startA).compare(QStringView(b).mid(startB, j - startB));
            if (cmp != 0)
                return cmp > 0 ? 1 : -1;
        }
    }

    // Handle trailing segments (ALPM: alpha suffix like "rc"/"beta" is older than no suffix;
    // numeric suffix is newer than no suffix).
    if (i == n && j == m)
        return 0;
    if (i < n) {
        int k = i;
        while (k < n && !a[k].isLetterOrNumber())
            ++k;
        if (k < n && a[k].isLetter())
            return -1;
        return 1;
    }
    int k = j;
    while (k < m && !b[k].isLetterOrNumber())
        ++k;
    if (k < m && b[k].isLetter())
        return 1;
    return -1;
}

struct ParsedArchVersion {
    QString epoch;
    QString pkgver;
    QString pkgrel;
};

ParsedArchVersion splitArchVersion(const QString &v) {
    ParsedArchVersion out;
    QString rest = v.trimmed();
    const int colon = rest.indexOf(':');
    if (colon >= 0) {
        out.epoch = rest.left(colon);
        rest = rest.mid(colon + 1);
    } else {
        out.epoch = QStringLiteral("0");
    }
    const int dash = rest.lastIndexOf('-');
    if (dash >= 0) {
        out.pkgver = rest.left(dash);
        out.pkgrel = rest.mid(dash + 1);
    } else {
        out.pkgver = rest;
    }
    return out;
}

} // namespace

int severityRank(const QString &severity) {
    const QString s = severity.trimmed().toLower();
    if (s == "critical")
        return 4;
    if (s == "high")
        return 3;
    if (s == "medium")
        return 2;
    if (s == "low")
        return 1;
    return 0;
}

QString normalizeSeverity(const QString &raw) {
    const QString s = raw.trimmed().toLower();
    if (s == "critical")
        return QStringLiteral("critical");
    if (s == "high")
        return QStringLiteral("high");
    if (s == "medium" || s == "moderate")
        return QStringLiteral("medium");
    if (s == "low")
        return QStringLiteral("low");
    return QStringLiteral("medium");
}

QString highestSeverity(const QVector<SecurityAdvisory> &advisories) {
    QString best;
    int bestRank = 0;
    for (const auto &adv : advisories) {
        const int r = severityRank(adv.severity);
        if (r > bestRank) {
            bestRank = r;
            best = adv.severity;
        }
    }
    return best;
}

QVariantList advisoriesToVariantList(const QVector<SecurityAdvisory> &advisories) {
    QVariantList list;
    list.reserve(advisories.size());
    for (const auto &adv : advisories) {
        list.append(QVariantMap{
            {"id", adv.id},
            {"cves", adv.cves},
            {"severity", adv.severity},
            {"summary", adv.summary},
            {"fixedVersion", adv.fixedVersion},
            {"url", sanitizeHttpUrl(adv.url)},
            {"source", adv.source},
        });
    }
    return list;
}

QString sanitizeHttpUrl(const QString &rawUrl) {
    const QString trimmed = rawUrl.trimmed();
    if (trimmed.isEmpty())
        return {};
    const QUrl url(trimmed, QUrl::StrictMode);
    if (!url.isValid())
        return {};
    const QString scheme = url.scheme().toLower();
    if ((scheme != "https" && scheme != "http") || url.host().isEmpty())
        return {};
    return url.toString();
}

QHash<QString, QString> parseMiseRegistry(const QString &registryOutput) {
    QHash<QString, QString> map;
    const QStringList lines = registryOutput.split('\n', Qt::SkipEmptyParts);
    static const QRegularExpression wsRx(R"(\s+)");
    for (const QString &rawLine : lines) {
        const QString line = rawLine.trimmed();
        if (line.isEmpty())
            continue;
        const auto m = wsRx.match(line);
        if (!m.hasMatch())
            continue;
        const QString name = line.left(m.capturedStart()).trimmed();
        const QString backends = line.mid(m.capturedEnd()).trimmed();
        if (!name.isEmpty() && !backends.isEmpty())
            map.insert(name, backends);
    }
    return map;
}

int compareArchVersions(const QString &a, const QString &b) {
    if (a == b)
        return 0;
    const auto pa = splitArchVersion(a);
    const auto pb = splitArchVersion(b);

    int cmp = compareVersionPart(pa.epoch, pb.epoch);
    if (cmp != 0)
        return cmp;
    cmp = compareVersionPart(pa.pkgver, pb.pkgver);
    if (cmp != 0)
        return cmp;
    if (!pa.pkgrel.isEmpty() && !pb.pkgrel.isEmpty())
        return compareVersionPart(pa.pkgrel, pb.pkgrel);
    return 0;
}

QHash<QString, QVector<SecurityAdvisory>> matchArchAdvisories(
    const QByteArray &archSecurityJson,
    const QVector<PackageItem> &installedPackages) {
    QHash<QString, QVector<SecurityAdvisory>> result;
    if (archSecurityJson.trimmed().isEmpty())
        return result;

    const QJsonDocument doc = QJsonDocument::fromJson(archSecurityJson);
    if (!doc.isArray())
        return result;

    // Index installed Pacman & Omarchy packages by name.
    QHash<QString, const PackageItem *> installedByName;
    for (const auto &pkg : installedPackages) {
        if (pkg.source == "pacman" || pkg.source == "omarchy")
            installedByName.insert(pkg.name, &pkg);
    }
    if (installedByName.isEmpty())
        return result;

    for (const QJsonValue &val : doc.array()) {
        if (!val.isObject())
            continue;
        const QJsonObject obj = val.toObject();
        const QString status = obj.value("status").toString().trimmed();
        if (status == "Not affected")
            continue;

        const QString avgName = obj.value("name").toString().trimmed();
        if (avgName.isEmpty())
            continue;
        const QString fixed = obj.value("fixed").toString().trimmed();
        const QString affected = obj.value("affected").toString().trimmed();
        const QString severity = normalizeSeverity(obj.value("severity").toString());
        const QString type = obj.value("type").toString().trimmed();

        QStringList cves;
        for (const QJsonValue &cv : obj.value("issues").toArray()) {
            const QString c = cv.toString().trimmed();
            if (!c.isEmpty())
                cves.append(c);
        }

        for (const QJsonValue &pv : obj.value("packages").toArray()) {
            const QString pkgName = pv.toString().trimmed();
            const PackageItem *pkg = installedByName.value(pkgName, nullptr);
            if (!pkg || pkg->version.isEmpty())
                continue;

            bool vulnerable = false;
            if (!fixed.isEmpty()) {
                vulnerable = compareArchVersions(pkg->version, fixed) < 0;
            } else if ((status == "Vulnerable" || status == "Testing") && !affected.isEmpty()) {
                vulnerable = compareArchVersions(pkg->version, affected) <= 0;
            }

            if (!vulnerable)
                continue;

            SecurityAdvisory adv;
            adv.id = avgName;
            adv.cves = cves;
            adv.severity = severity;
            adv.summary = (!type.isEmpty() && type.toLower() != "unknown")
                              ? type
                              : QStringLiteral("Known security vulnerability");
            adv.fixedVersion = fixed;
            adv.url = QStringLiteral("https://security.archlinux.org/") + avgName;
            adv.source = QStringLiteral("arch-security");
            result[pkg->id].append(std::move(adv));
        }
    }

    return result;
}

QHash<QString, QVector<SecurityAdvisory>> matchMarketplacePlugins(
    const QByteArray &registryJson,
    const QVector<PackageItem> &installedPackages,
    const QHash<QString, PluginMeta> &pluginMeta) {
    Q_UNUSED(pluginMeta);
    QHash<QString, QVector<SecurityAdvisory>> result;
    if (registryJson.trimmed().isEmpty())
        return result;

    const QJsonDocument doc = QJsonDocument::fromJson(registryJson);
    if (!doc.isObject())
        return result;

    QHash<QString, const PackageItem *> installedPlugins;
    for (const auto &pkg : installedPackages) {
        if (pkg.source == "omarchy-plugin")
            installedPlugins.insert(pkg.name, &pkg);
    }
    if (installedPlugins.isEmpty())
        return result;

    const QJsonObject root = doc.object();

    // 1. Flag plugins retired or removed from the Omarchy Plugin Marketplace.
    QSet<QString> retiredIds;
    for (const QJsonValue &rv : root.value("retiredPluginIds").toArray()) {
        const QString id = rv.toString().trimmed();
        if (!id.isEmpty())
            retiredIds.insert(id);
    }

    for (auto it = installedPlugins.constBegin(); it != installedPlugins.constEnd(); ++it) {
        if (retiredIds.contains(it.key())) {
            SecurityAdvisory adv;
            adv.id = QStringLiteral("OMARCHY-RETIRED:") + it.key();
            adv.severity = QStringLiteral("low");
            adv.summary = QStringLiteral("No longer listed (retired or renamed) in the Omarchy Plugin Marketplace — review upstream status");
            adv.url = QStringLiteral("https://github.com/omacom/omarchy-plugin-marketplace");
            adv.source = QStringLiteral("omarchy-marketplace");
            result[it.value()->id].append(std::move(adv));
        }
    }

    // 2. Inspect listed sources for Automated Security Baseline findings or revoked approvals.
    for (const QJsonValue &sv : root.value("sources").toArray()) {
        if (!sv.isObject())
            continue;
        const QJsonObject src = sv.toObject();
        const QString safeRepoUrl = sanitizeHttpUrl(src.value("repo").toString());

        QSet<QString> sourcePluginIds;
        if (src.value("catalog").isObject()) {
            const QString cid = src.value("catalog").toObject().value("id").toString().trimmed();
            if (!cid.isEmpty())
                sourcePluginIds.insert(cid);
        }
        if (src.value("plugins").isObject()) {
            const QJsonObject pObj = src.value("plugins").toObject();
            for (auto pit = pObj.constBegin(); pit != pObj.constEnd(); ++pit)
                sourcePluginIds.insert(pit.key().trimmed());
        } else if (src.value("plugins").isArray()) {
            for (const QJsonValue &pv : src.value("plugins").toArray()) {
                const QString pid = pv.toObject().value("id").toString().trimmed();
                if (!pid.isEmpty())
                    sourcePluginIds.insert(pid);
            }
        }
        if (src.value("automatedSecurityBaseline").isObject()) {
            for (const QJsonValue &pidVal :
                 src.value("automatedSecurityBaseline").toObject().value("pluginIds").toArray()) {
                const QString pid = pidVal.toString().trimmed();
                if (!pid.isEmpty())
                    sourcePluginIds.insert(pid);
            }
        }

        // Check baseline findings and revoked verification reviews.
        const QJsonObject baseline = src.value("automatedSecurityBaseline").toObject();
        const QString outcome = baseline.value("outcome").toString().trimmed().toLower();
        QStringList findings;
        for (const QJsonValue &fv : baseline.value("findings").toArray()) {
            const QString f = fv.isObject() ? fv.toObject().value("id").toString().trimmed()
                                            : fv.toString().trimmed();
            if (!f.isEmpty())
                findings.append(f);
        }

        const QJsonObject review = src.value("maintainerVerificationReview").toObject();
        const bool revoked = review.value("revoked").toBool(false)
                             || review.value("status").toString().trimmed().toLower() == "revoked"
                             || !review.value("revokedAt").toString().trimmed().isEmpty();

        for (const QString &pid : sourcePluginIds) {
            const PackageItem *pkg = installedPlugins.value(pid, nullptr);
            if (!pkg)
                continue;

            if (outcome == "needs-fixes" || !findings.isEmpty()) {
                SecurityAdvisory adv;
                adv.id = QStringLiteral("OMARCHY-BASELINE:") + pid;
                adv.severity = (outcome == "needs-fixes") ? QStringLiteral("critical") : QStringLiteral("high");
                adv.summary = findings.isEmpty()
                                  ? QStringLiteral("Plugin failed the Omarchy Marketplace security baseline")
                                  : QStringLiteral("Security baseline findings: ") + findings.join(", ");
                adv.url = !safeRepoUrl.isEmpty() ? safeRepoUrl
                                                 : QStringLiteral("https://github.com/omacom/omarchy-plugin-marketplace");
                adv.source = QStringLiteral("omarchy-marketplace");
                result[pkg->id].append(std::move(adv));
            }

            if (revoked) {
                SecurityAdvisory adv;
                adv.id = QStringLiteral("OMARCHY-REVOKED:") + pid;
                adv.severity = QStringLiteral("critical");
                adv.summary = QStringLiteral("Omarchy Marketplace verification approval was revoked");
                adv.url = !safeRepoUrl.isEmpty() ? safeRepoUrl
                                                 : QStringLiteral("https://github.com/omacom/omarchy-plugin-marketplace");
                adv.source = QStringLiteral("omarchy-marketplace");
                result[pkg->id].append(std::move(adv));
            }
        }
    }

    return result;
}

OsvQueryTarget resolveMiseOsvTarget(const PackageItem &pkg, const QString &registryLine) {
    OsvQueryTarget target;
    if (pkg.source != "mise" || pkg.version.isEmpty())
        return target;

    target.pkgId = pkg.id;
    target.toolName = pkg.name;
    target.version = pkg.version;

    // Parse backend tokens either from tool name itself (e.g. "npm:@openai/codex")
    // or from `mise registry <tool>` output (e.g. "aqua:openai/codex npm:@openai/codex").
    QStringList tokens;
    tokens.append(pkg.name.trimmed());
    const QStringList regTokens = registryLine.trimmed().split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    tokens.append(regTokens);

    for (const QString &tok : tokens) {
        if (tok.startsWith("npm:")) {
            target.ecosystem = QStringLiteral("npm");
            target.packageName = tok.mid(4).trimmed();
            return target;
        }
        if (tok.startsWith("pipx:")) {
            target.ecosystem = QStringLiteral("PyPI");
            target.packageName = tok.mid(5).trimmed();
            return target;
        }
        if (tok.startsWith("cargo:")) {
            target.ecosystem = QStringLiteral("crates.io");
            target.packageName = tok.mid(6).trimmed();
            return target;
        }
        if (tok.startsWith("gem:")) {
            target.ecosystem = QStringLiteral("RubyGems");
            target.packageName = tok.mid(4).trimmed();
            return target;
        }
        if (tok.startsWith("go:")) {
            target.ecosystem = QStringLiteral("Go");
            target.packageName = tok.mid(3).trimmed();
            return target;
        }
        if (tok == "core:go" || tok == "go") {
            target.ecosystem = QStringLiteral("Go");
            target.packageName = QStringLiteral("stdlib");
            return target;
        }
    }

    return {};
}

QByteArray buildOsvBatchRequest(const QVector<OsvQueryTarget> &targets) {
    QJsonArray queries;
    for (const auto &t : targets) {
        if (t.ecosystem.isEmpty() || t.packageName.isEmpty() || t.version.isEmpty())
            continue;
        queries.append(QJsonObject{
            {"version", t.version},
            {"package", QJsonObject{
                {"name", t.packageName},
                {"ecosystem", t.ecosystem},
            }},
        });
    }
    return QJsonDocument(QJsonObject{{"queries", queries}}).toJson(QJsonDocument::Compact);
}

QHash<QString, QVector<SecurityAdvisory>> parseOsvBatchResponse(
    const QByteArray &responseJson,
    const QVector<OsvQueryTarget> &targets) {
    QHash<QString, QVector<SecurityAdvisory>> result;
    if (responseJson.trimmed().isEmpty() || targets.isEmpty())
        return result;

    const QJsonDocument doc = QJsonDocument::fromJson(responseJson);
    if (!doc.isObject())
        return result;

    const QJsonArray results = doc.object().value("results").toArray();
    const int count = std::min(static_cast<int>(results.size()), static_cast<int>(targets.size()));
    for (int i = 0; i < count; ++i) {
        const QJsonArray vulns = results.at(i).toObject().value("vulns").toArray();
        if (vulns.isEmpty())
            continue;
        const auto &target = targets.at(i);
        for (const QJsonValue &vv : vulns) {
            const QJsonObject vObj = vv.toObject();
            const QString id = vObj.value("id").toString().trimmed();
            if (id.isEmpty())
                continue;

            SecurityAdvisory adv;
            adv.id = id;
            if (id.startsWith("CVE-"))
                adv.cves.append(id);
            for (const QJsonValue &av : vObj.value("aliases").toArray()) {
                const QString alias = av.toString().trimmed();
                if (alias.startsWith("CVE-") && !adv.cves.contains(alias))
                    adv.cves.append(alias);
            }

            const QString dbSeverity = vObj.value("database_specific").toObject().value("severity").toString();
            adv.severity = dbSeverity.isEmpty() ? QStringLiteral("medium") : normalizeSeverity(dbSeverity);
            const QString summary = vObj.value("summary").toString().trimmed();
            adv.summary = summary.isEmpty()
                              ? QStringLiteral("Vulnerability reported in OSV for %1 (%2)")
                                    .arg(target.packageName, target.ecosystem)
                              : summary;
            adv.url = QStringLiteral("https://osv.dev/vulnerability/") + id;
            adv.source = QStringLiteral("osv");
            result[target.pkgId].append(std::move(adv));
        }
    }

    return result;
}

void enrichOsvAdvisory(SecurityAdvisory &adv, const QByteArray &vulnJson) {
    if (vulnJson.trimmed().isEmpty())
        return;

    const QJsonDocument doc = QJsonDocument::fromJson(vulnJson);
    if (!doc.isObject())
        return;
    const QJsonObject obj = doc.object();

    for (const QJsonValue &av : obj.value("aliases").toArray()) {
        const QString alias = av.toString().trimmed();
        if (alias.startsWith("CVE-") && !adv.cves.contains(alias))
            adv.cves.append(alias);
    }

    const QString summary = obj.value("summary").toString().trimmed();
    if (!summary.isEmpty())
        adv.summary = summary;

    const QString dbSev = obj.value("database_specific").toObject().value("severity").toString().trimmed();
    if (!dbSev.isEmpty())
        adv.severity = normalizeSeverity(dbSev);

    // Extract earliest/first fixed version from affected[].ranges[].events[].fixed when available.
    if (adv.fixedVersion.isEmpty()) {
        for (const QJsonValue &affVal : obj.value("affected").toArray()) {
            const QJsonObject aff = affVal.toObject();
            for (const QJsonValue &rngVal : aff.value("ranges").toArray()) {
                for (const QJsonValue &evVal : rngVal.toObject().value("events").toArray()) {
                    const QString fixed = evVal.toObject().value("fixed").toString().trimmed();
                    if (!fixed.isEmpty()) {
                        adv.fixedVersion = fixed;
                        break;
                    }
                }
                if (!adv.fixedVersion.isEmpty())
                    break;
            }
            if (!adv.fixedVersion.isEmpty())
                break;
        }
    }
}

void sortAdvisoriesMap(QHash<QString, QVector<SecurityAdvisory>> &advisoriesByPkgId) {
    for (auto it = advisoriesByPkgId.begin(); it != advisoriesByPkgId.end(); ++it) {
        auto &vec = it.value();
        std::sort(vec.begin(), vec.end(), [](const SecurityAdvisory &a, const SecurityAdvisory &b) {
            const int ra = severityRank(a.severity);
            const int rb = severityRank(b.severity);
            if (ra != rb)
                return ra > rb;
            return a.id < b.id;
        });
    }
}

void mergeAdvisories(QHash<QString, QVector<SecurityAdvisory>> &dest,
                     const QHash<QString, QVector<SecurityAdvisory>> &src) {
    for (auto it = src.constBegin(); it != src.constEnd(); ++it) {
        auto &vec = dest[it.key()];
        QSet<QString> seen;
        for (const auto &existing : vec)
            seen.insert(existing.id);
        for (const auto &adv : it.value()) {
            if (!seen.contains(adv.id)) {
                seen.insert(adv.id);
                vec.append(adv);
            }
        }
    }
    sortAdvisoriesMap(dest);
}

QString cacheDirectory() {
    const QString xdgCache = qEnvironmentVariable("XDG_CACHE_HOME");
    const QString base = xdgCache.isEmpty() ? (QDir::homePath() + "/.cache") : xdgCache;
    return base + "/omapkg";
}

QString stateDirectory() {
    const QString xdgState = qEnvironmentVariable("XDG_STATE_HOME");
    const QString base = xdgState.isEmpty() ? (QDir::homePath() + "/.local/state") : xdgState;
    return base + "/omapkg";
}

bool readFreshCache(const QString &filePath, qint64 maxAgeSecs, QByteArray *outData) {
    const QFileInfo fi(filePath);
    if (!fi.exists() || !fi.isFile())
        return false;
    const qint64 ageSecs = fi.lastModified().secsTo(QDateTime::currentDateTime());
    if (maxAgeSecs >= 0 && ageSecs > maxAgeSecs)
        return false;
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    if (outData)
        *outData = f.readAll();
    return true;
}

bool writeCacheFile(const QString &filePath, const QByteArray &data) {
    const QFileInfo fi(filePath);
    QDir().mkpath(fi.absolutePath());
    QFile f(filePath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    return f.write(data) == data.size();
}

bool touchCacheFile(const QString &filePath) {
    QFile f(filePath);
    if (!f.exists() || !f.open(QIODevice::ReadWrite | QIODevice::Append))
        return false;
    return f.setFileTime(QDateTime::currentDateTime(), QFileDevice::FileModificationTime);
}

QByteArray readCacheEtag(const QString &filePath) {
    QFile f(filePath + ".etag");
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return f.readAll().trimmed();
}

void writeCacheEtag(const QString &filePath, const QByteArray &etag) {
    if (etag.trimmed().isEmpty())
        return;
    writeCacheFile(filePath + ".etag", etag.trimmed());
}

bool readFreshKeyedCache(const QString &filePath, const QString &expectedKey, qint64 maxAgeSecs, QByteArray *outData) {
    QByteArray raw;
    if (!readFreshCache(filePath, maxAgeSecs, &raw))
        return false;
    const QJsonDocument doc = QJsonDocument::fromJson(raw);
    if (!doc.isObject())
        return false;
    const QJsonObject obj = doc.object();
    if (obj.value("key").toString() != expectedKey)
        return false;
    if (outData)
        *outData = obj.value("payload").toString().toUtf8();
    return true;
}

bool writeKeyedCache(const QString &filePath, const QString &key, const QByteArray &data) {
    const QJsonObject obj{
        {"key", key},
        {"payload", QString::fromUtf8(data)},
    };
    return writeCacheFile(filePath, QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

int notifyNewAdvisories(const QVector<PackageItem> &packages,
                        const QHash<QString, QVector<SecurityAdvisory>> &advisoriesByPkgId,
                        const QString &stateFilePath,
                        const QString &notifySendPath,
                        bool dispatchDesktopNotification) {
    const QString path = stateFilePath.isEmpty()
                             ? (stateDirectory() + "/notified-advisories.json")
                             : stateFilePath;

    QSet<QString> previouslyNotified;
    {
        QFile f(path);
        if (f.open(QIODevice::ReadOnly)) {
            const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
            if (doc.isObject()) {
                for (const QJsonValue &v : doc.object().value("notified").toArray())
                    previouslyNotified.insert(v.toString());
            }
        }
    }

    QHash<QString, const PackageItem *> pkgById;
    for (const auto &pkg : packages)
        pkgById.insert(pkg.id, &pkg);

    QSet<QString> currentKeys;
    struct PendingNotice {
        QString displayName;
        QString severity;
        QString summary;
        QStringList ids;
    };
    QVector<PendingNotice> newlyFound;
    QString maxSeverity;
    int maxRank = 0;

    for (auto it = advisoriesByPkgId.constBegin(); it != advisoriesByPkgId.constEnd(); ++it) {
        const QString &pkgId = it.key();
        const auto &advs = it.value();
        if (advs.isEmpty())
            continue;

        PendingNotice notice;
        const PackageItem *pkg = pkgById.value(pkgId, nullptr);
        notice.displayName = pkg ? pkg->displayName : pkgId;
        bool hasNew = false;

        for (const auto &adv : advs) {
            const QString key = pkgId + "|" + adv.id;
            currentKeys.insert(key);
            // Suppress desktop popups for purely informational ("low") notices such as
            // renamed/retired plugins; keep them visible in the Security tab instead.
            if (severityRank(adv.severity) < 2)
                continue;
            if (!previouslyNotified.contains(key)) {
                hasNew = true;
                notice.ids.append(adv.cves.isEmpty() ? adv.id : adv.cves.first());
                if (severityRank(adv.severity) > severityRank(notice.severity)) {
                    notice.severity = adv.severity;
                    notice.summary = adv.summary;
                }
            }
        }

        if (hasNew) {
            if (severityRank(notice.severity) > maxRank) {
                maxRank = severityRank(notice.severity);
                maxSeverity = notice.severity;
            }
            newlyFound.append(std::move(notice));
        }
    }

    // Persist only currently active keys so resolved advisories are pruned cleanly.
    QJsonArray activeArr;
    QStringList sortedKeys = currentKeys.values();
    sortedKeys.sort();
    for (const QString &k : sortedKeys)
        activeArr.append(k);
    writeCacheFile(path, QJsonDocument(QJsonObject{{"notified", activeArr}}).toJson(QJsonDocument::Indented));

    if (newlyFound.isEmpty())
        return 0;

    if (dispatchDesktopNotification) {
        const QString bin = notifySendPath.isEmpty()
                                ? QStandardPaths::findExecutable("notify-send")
                                : notifySendPath;
        if (!bin.isEmpty()) {
            const QString urgency = (maxRank >= 3) ? QStringLiteral("critical") : QStringLiteral("normal");
            QString title;
            QString body;
            if (newlyFound.size() == 1) {
                const auto &n = newlyFound.first();
                title = QStringLiteral("Omapkg Security Alert: %1").arg(n.displayName);
                body = QStringLiteral("%1 (%2) — %3")
                           .arg(n.ids.mid(0, 3).join(", "), n.severity.toUpper(), n.summary);
            } else {
                title = QStringLiteral("Omapkg Security Alert (%1 packages)").arg(newlyFound.size());
                QStringList lines;
                for (int i = 0; i < std::min(3, static_cast<int>(newlyFound.size())); ++i) {
                    const auto &n = newlyFound.at(i);
                    lines.append(QStringLiteral("• %1: %2 (%3)")
                                     .arg(n.displayName, n.ids.value(0), n.severity.toUpper()));
                }
                if (newlyFound.size() > 3)
                    lines.append(QStringLiteral("+%1 more — open Omapkg to review").arg(newlyFound.size() - 3));
                body = lines.join("\n");
            }

            QProcess::startDetached(bin, {
                "-a", "Omapkg",
                "-i", "omapkg",
                "-u", urgency,
                title,
                body,
            });
        }
    }

    return newlyFound.size();
}

} // namespace Security
