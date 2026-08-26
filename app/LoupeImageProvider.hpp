#pragma once

#include <QImage>
#include <QMutex>
#include <QQuickImageProvider>

class LoupeImageProvider final : public QQuickImageProvider {
public:
    LoupeImageProvider();

    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;
    void setImages(QImage original, QImage document);

private:
    QMutex mutex_;
    QImage original_;
    QImage document_;
};

