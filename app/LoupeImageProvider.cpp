#include "app/LoupeImageProvider.hpp"

#include <QMutexLocker>

LoupeImageProvider::LoupeImageProvider()
    : QQuickImageProvider(QQuickImageProvider::Image) {}

QImage LoupeImageProvider::requestImage(const QString& id, QSize* size,
                                        const QSize& requestedSize) {
    QMutexLocker lock(&mutex_);
    QImage image = id.startsWith(QStringLiteral("document")) ? document_ : original_;
    if (size) *size = image.size();
    if (requestedSize.isValid() && !image.isNull()) {
        image = image.scaled(requestedSize, Qt::KeepAspectRatio,
                             id.startsWith(QStringLiteral("document"))
                                ? Qt::SmoothTransformation : Qt::FastTransformation);
    }
    return image;
}

void LoupeImageProvider::setImages(QImage original, QImage document) {
    QMutexLocker lock(&mutex_);
    original_ = std::move(original);
    document_ = std::move(document);
}

