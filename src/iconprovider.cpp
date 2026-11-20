#include "iconprovider.h"

#include <QFileInfo>
#include <QIcon>
#include <QUrl>

IconProvider::IconProvider() : QQuickImageProvider(QQuickImageProvider::Pixmap) {}

QPixmap IconProvider::requestPixmap(const QString &id, QSize *size, const QSize &requestedSize) {
    const QString decoded = QUrl::fromPercentEncoding(id.toUtf8());
    const int side = requestedSize.isValid() && requestedSize.width() > 0 ? requestedSize.width() : 48;

    QIcon icon;
    if (!decoded.isEmpty()) {
        if (QFileInfo::exists(decoded))
            icon = QIcon(decoded);
        else
            icon = QIcon::fromTheme(decoded);
    }

    if (icon.isNull())
        return QPixmap();

    const QPixmap pixmap = icon.pixmap(QSize(side, side));
    if (size)
        *size = pixmap.size();
    return pixmap;
}
