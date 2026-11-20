#pragma once

#include <QQuickImageProvider>

// Resolves freedesktop icon names or absolute icon file paths for QML Image
// elements via image://themeicon/<name>.
class IconProvider : public QQuickImageProvider {
public:
    IconProvider();
    QPixmap requestPixmap(const QString &id, QSize *size, const QSize &requestedSize) override;
};
