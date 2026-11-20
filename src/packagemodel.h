#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QString>
#include <QVariantMap>
#include <QVector>

struct PackageItem {
    QString id;
    QString name;
    QString displayName;
    QString version;
    QString availableVersion;
    QString source;       // "omarchy", "pacman", "aur", "flatpak"
    QString repository;   // "omarchy", "core", "extra", "multilib", "AUR", "flathub", ...
    QString description;
    QString url;
    qint64 sizeBytes = 0;
    qint64 installDate = 0;
    bool explicitInstall = true;
    bool isOrphan = false;
    bool isOmarchyDefault = false;
    bool hasUpdate = false;
    QString iconName;
    QString desktopId;
};

class PackageModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QString filterSource READ filterSource WRITE setFilterSource NOTIFY filterChanged)
    Q_PROPERTY(QString searchQuery READ searchQuery WRITE setSearchQuery NOTIFY filterChanged)
    Q_PROPERTY(bool showDependencies READ showDependencies WRITE setShowDependencies NOTIFY filterChanged)
    Q_PROPERTY(QString sortBy READ sortBy WRITE setSortBy NOTIFY filterChanged)

    Q_PROPERTY(int visibleCount READ visibleCount NOTIFY statsChanged)
    Q_PROPERTY(qint64 visibleBytes READ visibleBytes NOTIFY statsChanged)
    Q_PROPERTY(qint64 maxVisibleBytes READ maxVisibleBytes NOTIFY statsChanged)
    Q_PROPERTY(int totalCount READ totalCount NOTIFY statsChanged)
    Q_PROPERTY(int scopeCount READ scopeCount NOTIFY statsChanged)
    Q_PROPERTY(int updatesCount READ updatesCount NOTIFY statsChanged)
    Q_PROPERTY(int omarchyCount READ omarchyCount NOTIFY statsChanged)
    Q_PROPERTY(int pacmanCount READ pacmanCount NOTIFY statsChanged)
    Q_PROPERTY(int aurCount READ aurCount NOTIFY statsChanged)
    Q_PROPERTY(int flatpakCount READ flatpakCount NOTIFY statsChanged)
    Q_PROPERTY(int orphanCount READ orphanCount NOTIFY statsChanged)

    Q_PROPERTY(qint64 totalBytes READ totalBytes NOTIFY statsChanged)
    Q_PROPERTY(qint64 omarchyBytes READ omarchyBytes NOTIFY statsChanged)
    Q_PROPERTY(qint64 pacmanBytes READ pacmanBytes NOTIFY statsChanged)
    Q_PROPERTY(qint64 aurBytes READ aurBytes NOTIFY statsChanged)
    Q_PROPERTY(qint64 flatpakBytes READ flatpakBytes NOTIFY statsChanged)
    Q_PROPERTY(qint64 orphanBytes READ orphanBytes NOTIFY statsChanged)

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        NameRole,
        DisplayNameRole,
        VersionRole,
        AvailableVersionRole,
        SourceRole,
        RepositoryRole,
        DescriptionRole,
        UrlRole,
        SizeBytesRole,
        InstallDateRole,
        ExplicitInstallRole,
        IsOrphanRole,
        IsOmarchyDefaultRole,
        HasUpdateRole,
        IconNameRole,
        DesktopIdRole
    };

    explicit PackageModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setPackages(QVector<PackageItem> packages);
    void applyUpdates(const QHash<QString, QString> &pacmanAndOmarchyUpdates,
                      const QHash<QString, QString> &aurUpdates,
                      const QHash<QString, QString> &flatpakUpdates);

    const QVector<PackageItem> &allPackages() const { return m_all; }
    const PackageItem *findById(const QString &id) const;

    QString filterSource() const { return m_filterSource; }
    void setFilterSource(const QString &source);

    QString searchQuery() const { return m_searchQuery; }
    void setSearchQuery(const QString &query);

    bool showDependencies() const { return m_showDependencies; }
    void setShowDependencies(bool show);

    QString sortBy() const { return m_sortBy; }
    void setSortBy(const QString &sortBy);

    int visibleCount() const { return m_visible.size(); }
    qint64 visibleBytes() const { return m_visibleBytes; }
    qint64 maxVisibleBytes() const { return m_maxVisibleBytes; }
    int totalCount() const { return m_all.size(); }
    int scopeCount() const { return m_scopeCount; }
    int updatesCount() const { return m_updatesCount; }
    int omarchyCount() const { return m_omarchyCount; }
    int pacmanCount() const { return m_pacmanCount; }
    int aurCount() const { return m_aurCount; }
    int flatpakCount() const { return m_flatpakCount; }
    int orphanCount() const { return m_orphanCount; }

    qint64 totalBytes() const { return m_totalBytes; }
    qint64 omarchyBytes() const { return m_omarchyBytes; }
    qint64 pacmanBytes() const { return m_pacmanBytes; }
    qint64 aurBytes() const { return m_aurBytes; }
    qint64 flatpakBytes() const { return m_flatpakBytes; }
    qint64 orphanBytes() const { return m_orphanBytes; }

    Q_INVOKABLE QVariantMap get(int row) const;
    Q_INVOKABLE QVariantMap getById(const QString &id) const;
    Q_INVOKABLE int indexOfId(const QString &id) const;

signals:
    void filterChanged();
    void statsChanged();

private:
    static QVariantMap toVariantMap(const PackageItem &pkg);
    void rebuild();

    QVector<PackageItem> m_all;
    QVector<int> m_visible;

    QString m_filterSource = "all";
    QString m_searchQuery;
    bool m_showDependencies = false;
    QString m_sortBy = "size";

    qint64 m_visibleBytes = 0;
    qint64 m_maxVisibleBytes = 0;
    int m_scopeCount = 0;
    int m_updatesCount = 0;
    int m_omarchyCount = 0;
    int m_pacmanCount = 0;
    int m_aurCount = 0;
    int m_flatpakCount = 0;
    int m_orphanCount = 0;

    qint64 m_totalBytes = 0;
    qint64 m_omarchyBytes = 0;
    qint64 m_pacmanBytes = 0;
    qint64 m_aurBytes = 0;
    qint64 m_flatpakBytes = 0;
    qint64 m_orphanBytes = 0;
};
