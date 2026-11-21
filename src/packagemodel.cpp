#include "packagemodel.h"

#include <algorithm>

PackageModel::PackageModel(QObject *parent) : QAbstractListModel(parent) {}

int PackageModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : m_visible.size();
}

QVariant PackageModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_visible.size())
        return {};

    const PackageItem &pkg = m_all.at(m_visible.at(index.row()));
    switch (role) {
    case IdRole: return pkg.id;
    case NameRole: return pkg.name;
    case DisplayNameRole: return pkg.displayName;
    case VersionRole: return pkg.version;
    case AvailableVersionRole: return pkg.availableVersion;
    case SourceRole: return pkg.source;
    case RepositoryRole: return pkg.repository;
    case DescriptionRole: return pkg.description;
    case UrlRole: return pkg.url;
    case SizeBytesRole: return pkg.sizeBytes;
    case InstallDateRole: return pkg.installDate;
    case ExplicitInstallRole: return pkg.explicitInstall;
    case IsOrphanRole: return pkg.isOrphan;
    case IsOmarchyDefaultRole: return pkg.isOmarchyDefault;
    case HasUpdateRole: return pkg.hasUpdate;
    case IconNameRole: return pkg.iconName;
    case DesktopIdRole: return pkg.desktopId;
    default: return {};
    }
}

QHash<int, QByteArray> PackageModel::roleNames() const {
    return {
        {IdRole, "pkgId"},
        {NameRole, "name"},
        {DisplayNameRole, "displayName"},
        {VersionRole, "version"},
        {AvailableVersionRole, "availableVersion"},
        {SourceRole, "source"},
        {RepositoryRole, "repository"},
        {DescriptionRole, "description"},
        {UrlRole, "url"},
        {SizeBytesRole, "sizeBytes"},
        {InstallDateRole, "installDate"},
        {ExplicitInstallRole, "explicitInstall"},
        {IsOrphanRole, "isOrphan"},
        {IsOmarchyDefaultRole, "isOmarchyDefault"},
        {HasUpdateRole, "hasUpdate"},
        {IconNameRole, "iconName"},
        {DesktopIdRole, "desktopId"},
    };
}

void PackageModel::setPackages(QVector<PackageItem> packages) {
    m_all = std::move(packages);
    rebuild();
}

void PackageModel::applyUpdates(const QHash<QString, QString> &pacmanAndOmarchyUpdates,
                                const QHash<QString, QString> &aurUpdates,
                                const QHash<QString, QString> &flatpakUpdates,
                                const QHash<QString, QString> &pluginUpdates,
                                const QHash<QString, QString> &miseUpdates) {
    for (auto &pkg : m_all) {
        pkg.hasUpdate = false;
        pkg.availableVersion.clear();

        if ((pkg.source == "omarchy" || pkg.source == "pacman") && pacmanAndOmarchyUpdates.contains(pkg.name)) {
            pkg.hasUpdate = true;
            pkg.availableVersion = pacmanAndOmarchyUpdates.value(pkg.name);
        } else if (pkg.source == "aur" && aurUpdates.contains(pkg.name)) {
            pkg.hasUpdate = true;
            pkg.availableVersion = aurUpdates.value(pkg.name);
        } else if (pkg.source == "flatpak" && flatpakUpdates.contains(pkg.name)) {
            pkg.hasUpdate = true;
            pkg.availableVersion = flatpakUpdates.value(pkg.name);
        } else if (pkg.source == "omarchy-plugin" && pluginUpdates.contains(pkg.name)) {
            pkg.hasUpdate = true;
            pkg.availableVersion = pluginUpdates.value(pkg.name);
        } else if (pkg.source == "mise" && miseUpdates.contains(pkg.name)) {
            pkg.hasUpdate = true;
            pkg.availableVersion = miseUpdates.value(pkg.name);
        }
    }
    rebuild();
}

const PackageItem *PackageModel::findById(const QString &id) const {
    for (const auto &pkg : m_all)
        if (pkg.id == id)
            return &pkg;
    return nullptr;
}

void PackageModel::setFilterSource(const QString &source) {
    if (m_filterSource == source)
        return;
    m_filterSource = source;
    emit filterChanged();
    rebuild();
}

void PackageModel::setSearchQuery(const QString &query) {
    if (m_searchQuery == query)
        return;
    m_searchQuery = query;
    emit filterChanged();
    rebuild();
}

void PackageModel::setShowDependencies(bool show) {
    if (m_showDependencies == show)
        return;
    m_showDependencies = show;
    emit filterChanged();
    rebuild();
}

void PackageModel::setSortBy(const QString &sortBy) {
    if (m_sortBy == sortBy)
        return;
    m_sortBy = sortBy;
    emit filterChanged();
    rebuild();
}

QVariantMap PackageModel::toVariantMap(const PackageItem &pkg) {
    return {
        {"pkgId", pkg.id},
        {"name", pkg.name},
        {"displayName", pkg.displayName},
        {"version", pkg.version},
        {"availableVersion", pkg.availableVersion},
        {"source", pkg.source},
        {"repository", pkg.repository},
        {"description", pkg.description},
        {"url", pkg.url},
        {"sizeBytes", pkg.sizeBytes},
        {"installDate", pkg.installDate},
        {"explicitInstall", pkg.explicitInstall},
        {"isOrphan", pkg.isOrphan},
        {"isOmarchyDefault", pkg.isOmarchyDefault},
        {"hasUpdate", pkg.hasUpdate},
        {"iconName", pkg.iconName},
        {"desktopId", pkg.desktopId},
    };
}

QVariantMap PackageModel::get(int row) const {
    if (row < 0 || row >= m_visible.size())
        return {};
    return toVariantMap(m_all.at(m_visible.at(row)));
}

QVariantMap PackageModel::getById(const QString &id) const {
    const PackageItem *pkg = findById(id);
    return pkg ? toVariantMap(*pkg) : QVariantMap{};
}

int PackageModel::indexOfId(const QString &id) const {
    for (int i = 0; i < m_visible.size(); ++i)
        if (m_all.at(m_visible.at(i)).id == id)
            return i;
    return -1;
}

void PackageModel::rebuild() {
    beginResetModel();

    m_visible.clear();
    m_visibleBytes = 0;
    m_maxVisibleBytes = 0;
    m_scopeCount = 0;
    m_updatesCount = 0;
    m_omarchyCount = 0;
    m_pacmanCount = 0;
    m_aurCount = 0;
    m_flatpakCount = 0;
    m_pluginCount = 0;
    m_miseCount = 0;
    m_orphanCount = 0;
    m_totalBytes = 0;
    m_omarchyBytes = 0;
    m_pacmanBytes = 0;
    m_aurBytes = 0;
    m_flatpakBytes = 0;
    m_pluginBytes = 0;
    m_miseBytes = 0;
    m_orphanBytes = 0;

    const QString needle = m_searchQuery.trimmed().toLower();

    for (int i = 0; i < m_all.size(); ++i) {
        const PackageItem &pkg = m_all.at(i);

        // Updates and orphans always surface in their dedicated tabs regardless
        // of the explicit-only toggle, so important maintenance is never hidden.
        if (pkg.hasUpdate)
            ++m_updatesCount;
        if (pkg.isOrphan) {
            ++m_orphanCount;
            m_orphanBytes += pkg.sizeBytes;
        }

        const bool inScope = m_showDependencies || pkg.explicitInstall
                             || pkg.source == "flatpak" || pkg.source == "omarchy-plugin" || pkg.source == "mise";
        if (inScope) {
            ++m_scopeCount;
            m_totalBytes += pkg.sizeBytes;
            if (pkg.source == "omarchy") {
                ++m_omarchyCount;
                m_omarchyBytes += pkg.sizeBytes;
            } else if (pkg.source == "pacman") {
                ++m_pacmanCount;
                m_pacmanBytes += pkg.sizeBytes;
            } else if (pkg.source == "aur") {
                ++m_aurCount;
                m_aurBytes += pkg.sizeBytes;
            } else if (pkg.source == "flatpak") {
                ++m_flatpakCount;
                m_flatpakBytes += pkg.sizeBytes;
            } else if (pkg.source == "omarchy-plugin") {
                ++m_pluginCount;
                m_pluginBytes += pkg.sizeBytes;
            } else if (pkg.source == "mise") {
                ++m_miseCount;
                m_miseBytes += pkg.sizeBytes;
            }
        }

        bool matchesTab = false;
        if (m_filterSource == "updates")
            matchesTab = pkg.hasUpdate;
        else if (m_filterSource == "orphans")
            matchesTab = pkg.isOrphan;
        else if (m_filterSource == "all")
            matchesTab = inScope;
        else
            matchesTab = inScope && pkg.source == m_filterSource;

        if (!matchesTab)
            continue;

        if (!needle.isEmpty()) {
            const bool matched = pkg.name.toLower().contains(needle)
                              || pkg.displayName.toLower().contains(needle)
                              || pkg.description.toLower().contains(needle)
                              || pkg.repository.toLower().contains(needle);
            if (!matched)
                continue;
        }

        m_visible.append(i);
        m_visibleBytes += pkg.sizeBytes;
        if (pkg.sizeBytes > m_maxVisibleBytes)
            m_maxVisibleBytes = pkg.sizeBytes;
    }

    std::sort(m_visible.begin(), m_visible.end(), [this](int aIdx, int bIdx) {
        const PackageItem &a = m_all.at(aIdx);
        const PackageItem &b = m_all.at(bIdx);
        if (m_sortBy == "size") {
            if (a.sizeBytes != b.sizeBytes)
                return a.sizeBytes > b.sizeBytes;
        } else if (m_sortBy == "updates") {
            if (a.hasUpdate != b.hasUpdate)
                return a.hasUpdate > b.hasUpdate;
            if (a.sizeBytes != b.sizeBytes)
                return a.sizeBytes > b.sizeBytes;
        } else if (m_sortBy == "date") {
            if (a.installDate != b.installDate)
                return a.installDate > b.installDate;
        }
        return QString::compare(a.displayName, b.displayName, Qt::CaseInsensitive) < 0;
    });

    endResetModel();
    emit statsChanged();
}
