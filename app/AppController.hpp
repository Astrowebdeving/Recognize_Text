#pragma once

#include "app/LoupeImageProvider.hpp"
#include "capture/IScreenCapture.hpp"
#include "core/jobs/RecognitionScheduler.hpp"
#include "core/state/CaptureCoordinator.hpp"
#include "vision/accounting/AccountingValidator.hpp"
#include "vision/layout/ReaderFormatter.hpp"

#include <QObject>
#include <QPoint>
#include <QRectF>
#include <QSize>
#include <QString>
#include <QTimer>
#include <QVariantList>

#include <chrono>
#include <memory>

class AppController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString sourceUrl READ sourceUrl NOTIFY sourceChanged)
    Q_PROPERTY(QString documentUrl READ documentUrl NOTIFY sourceChanged)
    Q_PROPERTY(QSize sourceSize READ sourceSize NOTIFY sourceChanged)
    Q_PROPERTY(QString captureStatus READ captureStatus NOTIFY captureStatusChanged)
    Q_PROPERTY(QString ocrText READ ocrText NOTIFY ocrChanged)
    Q_PROPERTY(QString ocrStatus READ ocrStatus NOTIFY ocrChanged)
    Q_PROPERTY(QString confidence READ confidence NOTIFY ocrChanged)
    Q_PROPERTY(QString alternative READ alternative NOTIFY ocrChanged)
    Q_PROPERTY(QVariantList overlayItems READ overlayItems NOTIFY ocrChanged)
    Q_PROPERTY(bool numericLike READ numericLike NOTIFY ocrChanged)
    Q_PROPERTY(bool pinned READ pinned NOTIFY modeChanged)
    Q_PROPERTY(bool autoEnabled READ autoEnabled WRITE setAutoEnabled NOTIFY modeChanged)
    Q_PROPERTY(bool selectionActive READ selectionActive NOTIFY selectionChanged)
    Q_PROPERTY(bool adjusting READ adjusting NOTIFY selectionChanged)
    Q_PROPERTY(QRectF desktopGeometry READ desktopGeometry NOTIFY desktopGeometryChanged)
    Q_PROPERTY(QRectF initialSelection READ initialSelection NOTIFY selectionChanged)
    Q_PROPERTY(double zoom READ zoom NOTIFY zoomChanged)
    Q_PROPERTY(QString zoomLabel READ zoomLabel NOTIFY zoomChanged)
    Q_PROPERTY(QString viewMode READ viewMode WRITE setViewMode NOTIFY viewModeChanged)
    Q_PROPERTY(QString ocrDisplay READ ocrDisplay WRITE setOcrDisplay NOTIFY ocrDisplayChanged)
    Q_PROPERTY(QString activeOcrDisplay READ activeOcrDisplay NOTIFY ocrChanged)
    Q_PROPERTY(bool hasSource READ hasSource NOTIFY sourceChanged)
    Q_PROPERTY(QString captureArea READ captureArea WRITE setCaptureArea NOTIFY settingsChanged)
    Q_PROPERTY(QString pointerResponse READ pointerResponse WRITE setPointerResponse NOTIFY settingsChanged)
    Q_PROPERTY(QString textSize READ textSize WRITE setTextSize NOTIFY settingsChanged)
    Q_PROPERTY(QString recognition READ recognition WRITE setRecognition NOTIFY settingsChanged)

public:
    explicit AppController(LoupeImageProvider* imageProvider, QObject* parent = nullptr);
    ~AppController() override;

    [[nodiscard]] QString sourceUrl() const;
    [[nodiscard]] QString documentUrl() const;
    [[nodiscard]] QSize sourceSize() const noexcept { return sourceSize_; }
    [[nodiscard]] QString captureStatus() const { return captureStatus_; }
    [[nodiscard]] QString ocrText() const { return ocrText_; }
    [[nodiscard]] QString ocrStatus() const { return ocrStatus_; }
    [[nodiscard]] QString confidence() const { return confidence_; }
    [[nodiscard]] QString alternative() const { return alternative_; }
    [[nodiscard]] QVariantList overlayItems() const { return overlayItems_; }
    [[nodiscard]] bool numericLike() const noexcept { return numericLike_; }
    [[nodiscard]] bool pinned() const noexcept { return pinned_; }
    [[nodiscard]] bool autoEnabled() const noexcept { return autoEnabled_; }
    [[nodiscard]] bool selectionActive() const noexcept { return selectionActive_; }
    [[nodiscard]] bool adjusting() const noexcept { return adjusting_; }
    [[nodiscard]] QRectF desktopGeometry() const;
    [[nodiscard]] QRectF initialSelection() const { return initialSelection_; }
    [[nodiscard]] double zoom() const noexcept;
    [[nodiscard]] QString zoomLabel() const;
    [[nodiscard]] QString viewMode() const { return viewMode_; }
    [[nodiscard]] QString ocrDisplay() const { return ocrDisplay_; }
    [[nodiscard]] QString activeOcrDisplay() const { return activeOcrDisplay_; }
    [[nodiscard]] bool hasSource() const noexcept { return !sourceSize_.isEmpty(); }
    [[nodiscard]] QString captureArea() const { return captureArea_; }
    [[nodiscard]] QString pointerResponse() const { return pointerResponse_; }
    [[nodiscard]] QString textSize() const { return textSize_; }
    [[nodiscard]] QString recognition() const { return recognition_; }

    Q_INVOKABLE void start();
    Q_INVOKABLE void setPointerInside(bool inside);
    Q_INVOKABLE void selectArea();
    Q_INVOKABLE void adjustArea();
    Q_INVOKABLE void cancelSelection();
    Q_INVOKABLE void completeSelection(const QRectF& logicalDesktopRect);
    Q_INVOKABLE void togglePin();
    Q_INVOKABLE void zoomIn();
    Q_INVOKABLE void zoomOut();
    Q_INVOKABLE void zoomByWheel(double angleDeltaY);
    Q_INVOKABLE void beginPinchZoom();
    Q_INVOKABLE void zoomByPinch(double scale);
    Q_INVOKABLE void copyText();
    Q_INVOKABLE void copyNumber();
    Q_INVOKABLE void copyImage();
    Q_INVOKABLE void openScreenRecordingSettings();

    void setAutoEnabled(bool enabled);
    void setViewMode(const QString& mode);
    void setOcrDisplay(const QString& display);
    void setCaptureArea(const QString& area);
    void setPointerResponse(const QString& response);
    void setTextSize(const QString& size);
    void setRecognition(const QString& recognition);

signals:
    void sourceChanged();
    void captureStatusChanged();
    void ocrChanged();
    void modeChanged();
    void selectionChanged();
    void desktopGeometryChanged();
    void zoomChanged();
    void viewModeChanged();
    void ocrDisplayChanged();
    void settingsChanged();

private:
    void poll();
    void refreshCaptureStatus();
    void restartCaptureForTopologyChange();
    void capturePointerRegion(const QPoint& logicalGlobal);
    void captureLogicalRect(const QRectF& logicalGlobal, loupe::CaptureOrigin origin);
    void publishRegion(std::shared_ptr<const loupe::RegionSnapshot> region);
    void scheduleRecognition(loupe::RecognitionQuality quality);
    void receiveRecognition(loupe::OcrResult result);
    [[nodiscard]] int screenIndexForPoint(const QPointF& point) const;
    [[nodiscard]] int displayIndexForScreen(int screenIndex,
                                            const std::vector<loupe::DisplayInfo>& displays) const;
    [[nodiscard]] static QImage toQImage(const loupe::PixelBuffer& pixels);
    [[nodiscard]] QRectF currentRegionAsLogical() const;
    void resetOcrForNewSource();

    LoupeImageProvider* imageProvider_{};
    std::unique_ptr<loupe::IScreenCapture> capture_;
    std::unique_ptr<loupe::RecognitionScheduler> recognizer_;
    loupe::CaptureCoordinator coordinator_;
    loupe::AccountingValidator accounting_;
    loupe::ReaderFormatter formatter_;
    QTimer pollTimer_;
    QPoint lastPointer_{INT_MIN, INT_MIN};
    std::chrono::steady_clock::time_point lastPointerMove_{};
    std::chrono::steady_clock::time_point lastCaptureRestart_{};
    loupe::GenerationId scheduledGeneration_{};
    uint64_t imageSerial_{};
    int zoomIndex_{9};
    double pinchStartZoom_{16.0};
    double wheelAccumulator_{};
    bool pointerInside_{};
    bool pinned_{};
    bool autoEnabled_{true};
    bool selectionActive_{};
    bool adjusting_{};
    QString captureStatus_{QStringLiteral("Starting capture…")};
    QString ocrText_;
    QString ocrStatus_{QStringLiteral("OCR starting…")};
    QString confidence_;
    QString alternative_;
    QVariantList overlayItems_;
    bool numericLike_{};
    QString numericText_;
    QSize sourceSize_;
    QRectF initialSelection_;
    QString viewMode_{QStringLiteral("pixels")};
    QString ocrDisplay_{QStringLiteral("auto")};
    QString activeOcrDisplay_{QStringLiteral("reader")};
    QString captureArea_{QStringLiteral("normal")};
    QString pointerResponse_{QStringLiteral("normal")};
    QString textSize_{QStringLiteral("huge")};
    QString recognition_{QStringLiteral("balanced")};
};
