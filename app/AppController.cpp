#include "app/AppController.hpp"

#include "core/coordinates/Coordinates.hpp"
#include "core/pipeline/ImageProcessing.hpp"
#include "vision/recognition/UnavailableTextRecognizer.hpp"

#if defined(Q_OS_MACOS)
#include "capture/macos/MacScreenCapture.hpp"
#include "vision/recognition/VisionTextRecognizer.hpp"
#elif defined(Q_OS_WIN)
#include "capture/windows/DxgiDesktopCapture.hpp"
#include "vision/recognition/WindowsTextRecognizer.hpp"
#else
#include "capture/UnsupportedScreenCapture.hpp"
#endif

#include <QClipboard>
#include <QCursor>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QMetaObject>
#include <QScreen>
#include <QSettings>
#include <QUrl>
#include <QVariantMap>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>

using namespace std::chrono_literals;

namespace {
constexpr std::array<double, 23> zoomLevels{
    1.0, 1.25, 1.5, 2.0, 3.0, 4.0, 6.0, 8.0, 12.0,
    16.0, 24.0, 32.0, 48.0, 64.0, 96.0, 128.0, 256.0, 512.0,
    1024.0, 2048.0, 4096.0, 8192.0, 16384.0
};
static_assert(zoomLevels[9] == 16.0);
static_assert(zoomLevels.back() == 16384.0);

QString confidenceText(float value) {
    switch (loupe::confidenceBand(value)) {
    case loupe::ConfidenceBand::High: return QStringLiteral("High confidence");
    case loupe::ConfidenceBand::Medium: return QStringLiteral("Medium confidence");
    case loupe::ConfidenceBand::Low: return QStringLiteral("Low confidence — inspect pixels");
    case loupe::ConfidenceBand::Unknown: return {};
    }
    return {};
}

bool looksLikeMaskedContent(const loupe::PixelBuffer& pixels) {
    if (!pixels.valid()) return false;
    const auto totalPixels = static_cast<size_t>(pixels.width)
                           * static_cast<size_t>(pixels.height);
    const auto stride = std::max<size_t>(1, totalPixels / 4096U);
    size_t sampled = 0;
    size_t black = 0;
    for (size_t pixel = 0; pixel < totalPixels; pixel += stride) {
        const auto y = pixel / static_cast<size_t>(pixels.width);
        const auto x = pixel % static_cast<size_t>(pixels.width);
        const auto offset = y * static_cast<size_t>(pixels.bytesPerRow)
                          + x * static_cast<size_t>(pixels.bytesPerPixel());
        bool isBlack = false;
        if (pixels.format == loupe::PixelFormat::Gray8) {
            isBlack = std::to_integer<uint8_t>(pixels.bytes[offset]) < 4U;
        } else {
            isBlack = std::to_integer<uint8_t>(pixels.bytes[offset]) < 4U &&
                      std::to_integer<uint8_t>(pixels.bytes[offset + 1]) < 4U &&
                      std::to_integer<uint8_t>(pixels.bytes[offset + 2]) < 4U;
        }
        black += isBlack ? 1U : 0U;
        ++sampled;
    }
    return sampled > 0 && static_cast<double>(black) / sampled > 0.98;
}

int32_t boundedInteger(double value, bool roundUp) noexcept {
    if (!std::isfinite(value)) return 0;
    const auto minimum = static_cast<double>(std::numeric_limits<int32_t>::min());
    const auto maximum = static_cast<double>(std::numeric_limits<int32_t>::max());
    const auto bounded = std::clamp(value, minimum, maximum);
    return static_cast<int32_t>(roundUp ? std::ceil(bounded) : std::floor(bounded));
}

std::optional<loupe::PixelRect> logicalPixelRect(const QRectF& rect) noexcept {
    if (!std::isfinite(rect.x()) || !std::isfinite(rect.y()) ||
        !std::isfinite(rect.width()) || !std::isfinite(rect.height()) ||
        rect.width() <= 0.0 || rect.height() <= 0.0) return std::nullopt;
    return loupe::PixelRect{boundedInteger(rect.x(), false), boundedInteger(rect.y(), false),
                            boundedInteger(rect.width(), true), boundedInteger(rect.height(), true)};
}

float normalizedCoordinate(float value) noexcept {
    return std::isfinite(value) ? std::clamp(value, 0.0F, 1.0F) : 0.0F;
}
} // namespace

AppController::AppController(LoupeImageProvider* imageProvider, QObject* parent)
    : QObject(parent), imageProvider_(imageProvider) {
#if defined(Q_OS_MACOS)
    capture_ = std::make_unique<loupe::MacScreenCapture>();
    auto engine = std::make_unique<loupe::VisionTextRecognizer>();
#elif defined(Q_OS_WIN)
    capture_ = std::make_unique<loupe::DxgiDesktopCapture>();
    auto engine = std::make_unique<loupe::WindowsTextRecognizer>();
#else
    capture_ = std::make_unique<loupe::UnsupportedScreenCapture>();
    auto engine = std::make_unique<loupe::UnavailableTextRecognizer>();
#endif
    recognizer_ = std::make_unique<loupe::RecognitionScheduler>(
        std::move(engine), [this](loupe::OcrResult result) {
            QMetaObject::invokeMethod(this, [this, result = std::move(result)]() mutable {
                receiveRecognition(std::move(result));
            }, Qt::QueuedConnection);
        });

    pollTimer_.setInterval(16);
    pollTimer_.setTimerType(Qt::PreciseTimer);
    connect(&pollTimer_, &QTimer::timeout, this, &AppController::poll);
    connect(qApp, &QGuiApplication::screenAdded, this, [this](QScreen*) {
        restartCaptureForTopologyChange();
    });
    connect(qApp, &QGuiApplication::screenRemoved, this, [this](QScreen*) {
        restartCaptureForTopologyChange();
    });

    QSettings settings;
    autoEnabled_ = settings.value(QStringLiteral("capture/automatic"), true).toBool();
    setCaptureArea(settings.value(QStringLiteral("capture/area"), captureArea_).toString());
    setPointerResponse(settings.value(QStringLiteral("capture/response"), pointerResponse_).toString());
    setTextSize(settings.value(QStringLiteral("ocr/textSize"), textSize_).toString());
    setRecognition(settings.value(QStringLiteral("ocr/recognition"), recognition_).toString());
    setOcrDisplay(settings.value(QStringLiteral("ocr/display"), ocrDisplay_).toString());
    zoomIndex_ = std::clamp(settings.value(QStringLiteral("zoom/index"), zoomIndex_).toInt(),
                            0, static_cast<int>(zoomLevels.size()) - 1);
}

AppController::~AppController() {
    pollTimer_.stop();
    recognizer_.reset();
    if (capture_) capture_->stop();
}

QString AppController::sourceUrl() const {
    if (imageSerial_ == 0) return {};
    return QStringLiteral("image://loupe/original/%1").arg(imageSerial_);
}

QString AppController::documentUrl() const {
    if (imageSerial_ == 0) return {};
    return QStringLiteral("image://loupe/document/%1").arg(imageSerial_);
}

QRectF AppController::desktopGeometry() const {
    QRect combined;
    for (const auto* screen : QGuiApplication::screens()) combined = combined.united(screen->geometry());
    return combined;
}

double AppController::zoom() const noexcept { return zoomLevels[static_cast<size_t>(zoomIndex_)]; }

QString AppController::zoomLabel() const {
    const auto percent = zoom() * 100.0;
    if (percent >= 10000.0) return QStringLiteral("%1×").arg(zoom(), 0, 'f', 0);
    return QStringLiteral("%1%").arg(percent, 0, 'f', percent < 1000.0 ? 0 : 0);
}

void AppController::start() {
    const auto status = capture_->start();
    captureStatus_ = QString::fromStdString(status.message);
    coordinator_.setCaptureStatus(status.code);
    emit captureStatusChanged();
    pollTimer_.start();
}

void AppController::setPointerInside(bool inside) {
    pointerInside_ = inside;
    coordinator_.setPointerInsideUtility(inside);
    if (!inside) lastPointer_ = {INT_MIN, INT_MIN};
}

void AppController::selectArea() {
    adjusting_ = false;
    selectionActive_ = true;
    initialSelection_ = {};
    coordinator_.beginManualSelection();
    emit selectionChanged();
}

void AppController::adjustArea() {
    if (!coordinator_.region()) return;
    adjusting_ = true;
    selectionActive_ = true;
    initialSelection_ = currentRegionAsLogical();
    coordinator_.beginAdjusting();
    emit selectionChanged();
}

void AppController::cancelSelection() {
    selectionActive_ = false;
    adjusting_ = false;
    coordinator_.resumeAuto();
    emit selectionChanged();
}

void AppController::completeSelection(const QRectF& logicalDesktopRect) {
    if (logicalDesktopRect.width() < 2.0 || logicalDesktopRect.height() < 2.0) {
        cancelSelection();
        return;
    }
    const auto origin = adjusting_ ? loupe::CaptureOrigin::Adjusted : loupe::CaptureOrigin::ManualDrag;
    selectionActive_ = false;
    adjusting_ = false;
    emit selectionChanged();
    captureLogicalRect(logicalDesktopRect.normalized(), origin);
    scheduleRecognition(loupe::RecognitionQuality::Accurate);
}

void AppController::togglePin() {
    pinned_ = !pinned_;
    coordinator_.setPinned(pinned_);
    if (pinned_) scheduleRecognition(loupe::RecognitionQuality::Accurate);
    emit modeChanged();
}

void AppController::zoomIn() {
    if (zoomIndex_ < static_cast<int>(zoomLevels.size()) - 1) {
        ++zoomIndex_;
        QSettings().setValue(QStringLiteral("zoom/index"), zoomIndex_);
        emit zoomChanged();
    }
}

void AppController::zoomOut() {
    if (zoomIndex_ > 0) {
        --zoomIndex_;
        QSettings().setValue(QStringLiteral("zoom/index"), zoomIndex_);
        emit zoomChanged();
    }
}

void AppController::zoomByWheel(double angleDeltaY) {
    if (!std::isfinite(angleDeltaY)) return;
    constexpr double detent = 120.0;
    wheelAccumulator_ = std::clamp(wheelAccumulator_ + angleDeltaY,
                                   -detent * 4.0, detent * 4.0);
    while (wheelAccumulator_ >= detent) {
        zoomIn();
        wheelAccumulator_ -= detent;
    }
    while (wheelAccumulator_ <= -detent) {
        zoomOut();
        wheelAccumulator_ += detent;
    }
}

void AppController::beginPinchZoom() {
    pinchStartZoom_ = zoom();
}

void AppController::zoomByPinch(double scale) {
    if (!std::isfinite(scale) || scale <= 0.0) return;
    const auto target = std::clamp(pinchStartZoom_ * scale,
                                   zoomLevels.front(), zoomLevels.back());
    const auto closest = std::min_element(zoomLevels.begin(), zoomLevels.end(),
        [target](double left, double right) {
            return std::abs(std::log(left / target)) < std::abs(std::log(right / target));
        });
    const auto nextIndex = static_cast<int>(std::distance(zoomLevels.begin(), closest));
    if (nextIndex == zoomIndex_) return;
    zoomIndex_ = nextIndex;
    QSettings().setValue(QStringLiteral("zoom/index"), zoomIndex_);
    emit zoomChanged();
}

void AppController::copyText() {
    if (!ocrText_.isEmpty()) QGuiApplication::clipboard()->setText(ocrText_);
}

void AppController::copyNumber() {
    if (numericLike_ && !numericText_.isEmpty())
        QGuiApplication::clipboard()->setText(numericText_);
}

void AppController::copyImage() {
    const auto region = coordinator_.region();
    if (region && region->originalPixels)
        QGuiApplication::clipboard()->setImage(toQImage(*region->originalPixels));
}

void AppController::openScreenRecordingSettings() {
#if defined(Q_OS_MACOS)
    QDesktopServices::openUrl(QUrl(QStringLiteral(
        "x-apple.systempreferences:com.apple.preference.security?Privacy_ScreenCapture")));
#endif
}

void AppController::setAutoEnabled(bool enabled) {
    if (autoEnabled_ == enabled) return;
    autoEnabled_ = enabled;
    QSettings().setValue(QStringLiteral("capture/automatic"), enabled);
    if (enabled) coordinator_.resumeAuto();
    emit modeChanged();
}

void AppController::setViewMode(const QString& mode) {
    if (mode != QStringLiteral("pixels") && mode != QStringLiteral("document") &&
        mode != QStringLiteral("ai")) return;
    if (viewMode_ == mode) return;
    viewMode_ = mode;
    emit viewModeChanged();
}

void AppController::setOcrDisplay(const QString& display) {
    if (display != QStringLiteral("auto") && display != QStringLiteral("reader") &&
        display != QStringLiteral("overlay")) return;
    if (ocrDisplay_ == display) return;
    ocrDisplay_ = display;
    QSettings().setValue(QStringLiteral("ocr/display"), display);
    if (display != QStringLiteral("auto")) activeOcrDisplay_ = display;
    emit ocrDisplayChanged();
    emit ocrChanged();
}

void AppController::setCaptureArea(const QString& area) {
    if (area != QStringLiteral("tight") && area != QStringLiteral("normal") &&
        area != QStringLiteral("wide")) return;
    if (captureArea_ == area) return;
    captureArea_ = area;
    QSettings().setValue(QStringLiteral("capture/area"), area);
    lastPointer_ = {INT_MIN, INT_MIN};
    emit settingsChanged();
}

void AppController::setPointerResponse(const QString& response) {
    if (response != QStringLiteral("responsive") && response != QStringLiteral("normal") &&
        response != QStringLiteral("calm")) return;
    if (pointerResponse_ == response) return;
    pointerResponse_ = response;
    QSettings().setValue(QStringLiteral("capture/response"), response);
    emit settingsChanged();
}

void AppController::setTextSize(const QString& size) {
    if (size != QStringLiteral("large") && size != QStringLiteral("huge") &&
        size != QStringLiteral("maximum")) return;
    if (textSize_ == size) return;
    textSize_ = size;
    QSettings().setValue(QStringLiteral("ocr/textSize"), size);
    emit settingsChanged();
}

void AppController::setRecognition(const QString& recognition) {
    if (recognition != QStringLiteral("fast") && recognition != QStringLiteral("balanced") &&
        recognition != QStringLiteral("accurate")) return;
    if (recognition_ == recognition) return;
    recognition_ = recognition;
    QSettings().setValue(QStringLiteral("ocr/recognition"), recognition);
    emit settingsChanged();
}

void AppController::poll() {
    refreshCaptureStatus();
    if (!autoEnabled_ || pinned_ || pointerInside_ || selectionActive_) return;
    const auto position = QCursor::pos();
    const auto moved = lastPointer_.x() == INT_MIN || (position - lastPointer_).manhattanLength() > 2;
    if (moved) {
        lastPointer_ = position;
        lastPointerMove_ = std::chrono::steady_clock::now();
        scheduledGeneration_ = 0;
        capturePointerRegion(position);
        return;
    }
    const auto dwell = pointerResponse_ == QStringLiteral("responsive") ? 120ms
                     : pointerResponse_ == QStringLiteral("calm") ? 250ms : 160ms;
    if (coordinator_.generation() != 0 && scheduledGeneration_ != coordinator_.generation() &&
        std::chrono::steady_clock::now() - lastPointerMove_ >= dwell) {
        const auto quality = recognition_ == QStringLiteral("fast")
            ? loupe::RecognitionQuality::Fast
            : recognition_ == QStringLiteral("accurate")
                ? loupe::RecognitionQuality::Accurate : loupe::RecognitionQuality::Balanced;
        scheduleRecognition(quality);
    }
}

void AppController::refreshCaptureStatus() {
    auto status = capture_->status();
    const auto now = std::chrono::steady_clock::now();
    if ((status.code == loupe::CaptureStatusCode::TemporarilyUnavailable ||
         status.code == loupe::CaptureStatusCode::PermissionRequired) &&
        now - lastCaptureRestart_ >= 2s) {
        lastCaptureRestart_ = now;
        capture_->stop();
        status = capture_->start();
    }
    const auto next = QString::fromStdString(status.message);
    if (captureStatus_ != next) {
        captureStatus_ = next;
        coordinator_.setCaptureStatus(status.code);
        emit captureStatusChanged();
    }
}

void AppController::restartCaptureForTopologyChange() {
    emit desktopGeometryChanged();
    if (!pollTimer_.isActive()) return;
    capture_->stop();
    const auto status = capture_->start();
    coordinator_.setCaptureStatus(status.code);
    captureStatus_ = QString::fromStdString(status.message);
    lastPointer_ = {INT_MIN, INT_MIN};
    scheduledGeneration_ = 0;
    emit captureStatusChanged();
}

void AppController::capturePointerRegion(const QPoint& logicalGlobal) {
    const QSize size = captureArea_ == QStringLiteral("tight") ? QSize(300, 160)
                     : captureArea_ == QStringLiteral("wide") ? QSize(620, 360)
                                                               : QSize(420, 240);
    const QRectF rect(logicalGlobal.x() - size.width() / 2.0,
                      logicalGlobal.y() - size.height() / 2.0,
                      size.width(), size.height());
    captureLogicalRect(rect, loupe::CaptureOrigin::AutoPointer);
}

void AppController::captureLogicalRect(const QRectF& logicalGlobal, loupe::CaptureOrigin origin) {
    const auto logicalPixels = logicalPixelRect(logicalGlobal);
    if (!logicalPixels) return;
    const auto displays = capture_->displays();
    const auto screens = QGuiApplication::screens();
    if (displays.empty() || screens.empty()) return;
    const auto screenIndex = screenIndexForPoint(logicalGlobal.center());
    if (screenIndex < 0) return;
    const auto displayIndex = displayIndexForScreen(screenIndex, displays);
    if (displayIndex < 0) return;
    const auto* screen = screens[screenIndex];
    const auto& display = displays[static_cast<size_t>(displayIndex)];
    const auto logicalScreen = screen->geometry();
    const loupe::PixelRect logicalDisplay{logicalScreen.x(), logicalScreen.y(),
                                          logicalScreen.width(), logicalScreen.height()};
    const auto physical = loupe::coordinates::logicalToPhysical(*logicalPixels, logicalDisplay,
                                                                 display.physicalRect);
    const auto clipped = loupe::coordinates::intersect(physical, display.physicalRect);
    auto frame = capture_->latestFrame(display.id);
    if (!frame || frame->protectedContent) {
        captureStatus_ = frame && frame->protectedContent
            ? QStringLiteral("Protected content cannot be captured")
            : QStringLiteral("Waiting for the first desktop frame…");
        emit captureStatusChanged();
        return;
    }
    auto pixels = loupe::image::crop(*frame, clipped);
    if (!pixels) return;
    if (frame->maskedContentPresent && looksLikeMaskedContent(*pixels)) {
        captureStatus_ = QStringLiteral("Protected content cannot be captured");
        emit captureStatusChanged();
        return;
    }

    auto region = std::make_shared<loupe::RegionSnapshot>();
    region->frameId = frame->frameId;
    region->regionId = frame->frameId;
    region->origin = origin;
    region->userRect = clipped;
    region->analysisRect = loupe::coordinates::expandAndClamp(clipped, 12, frame->physicalRect);
    region->originalPixels = std::move(pixels);
    region->display = display.id;
    region->devicePixelRatio = display.devicePixelRatio;
    region->capturedAt = frame->capturedAt;
    publishRegion(std::move(region));
}

void AppController::publishRegion(std::shared_ptr<const loupe::RegionSnapshot> region) {
    coordinator_.publish(std::move(region));
    const auto current = coordinator_.region();
    if (!current || !current->originalPixels) return;
    recognizer_->cancelBefore(current->generation);
    auto document = loupe::image::documentEnhance(*current->originalPixels);
    if (imageProvider_) {
        imageProvider_->setImages(toQImage(*current->originalPixels),
                                  document ? toQImage(*document) : QImage{});
    }
    sourceSize_ = {current->originalPixels->width, current->originalPixels->height};
    ++imageSerial_;
    resetOcrForNewSource();
    emit sourceChanged();
}

void AppController::scheduleRecognition(loupe::RecognitionQuality quality) {
    const auto region = coordinator_.region();
    if (!region || !region->originalPixels) return;
    scheduledGeneration_ = region->generation;
    ocrStatus_ = QStringLiteral("Reading locally…");
    emit ocrChanged();
    recognizer_->submit({region->generation, region, {quality, true}});
}

void AppController::receiveRecognition(loupe::OcrResult result) {
    if (!coordinator_.publishOcrGeneration(result.generation)) return;
    if (!result.error.empty()) {
        ocrStatus_ = QString::fromStdString(result.error);
        confidence_.clear();
        emit ocrChanged();
        return;
    }
    accounting_.annotate(result);
    ocrText_ = QString::fromStdString(formatter_.format(result));
    ocrStatus_ = result.tokens.empty()
        ? QStringLiteral("No text found — magnifier remains available")
        : QStringLiteral("%1 · on-device").arg(QString::fromStdString(result.engine));
    confidence_ = confidenceText(result.overallConfidence);
    numericLike_ = std::any_of(result.tokens.begin(), result.tokens.end(),
                               [](const auto& token) { return token.numericLike; });
    numericText_.clear();
    const auto numericToken = std::find_if(result.tokens.begin(), result.tokens.end(),
                                           [](const auto& token) { return token.numericLike; });
    if (numericToken != result.tokens.end()) {
        const auto assessment = accounting_.assess(numericToken->text, numericToken->confidence);
        numericText_ = QString::fromStdString(assessment.normalized);
    }
    alternative_.clear();
    for (const auto& token : result.tokens) {
        const auto assessment = accounting_.assess(token.text, token.confidence);
        if (!assessment.alternatives.empty()) {
            alternative_ = QStringLiteral("Possible reading: %1")
                .arg(QString::fromStdString(assessment.alternatives.front().text));
            break;
        }
    }

    overlayItems_.clear();
    for (const auto& token : result.tokens) {
        const auto& q = token.sourceQuad;
        const auto left = std::min({normalizedCoordinate(q.x1), normalizedCoordinate(q.x2),
                                    normalizedCoordinate(q.x3), normalizedCoordinate(q.x4)});
        const auto right = std::max({normalizedCoordinate(q.x1), normalizedCoordinate(q.x2),
                                     normalizedCoordinate(q.x3), normalizedCoordinate(q.x4)});
        const auto top = std::min({normalizedCoordinate(q.y1), normalizedCoordinate(q.y2),
                                   normalizedCoordinate(q.y3), normalizedCoordinate(q.y4)});
        const auto bottom = std::max({normalizedCoordinate(q.y1), normalizedCoordinate(q.y2),
                                      normalizedCoordinate(q.y3), normalizedCoordinate(q.y4)});
        QVariantMap item;
        item.insert(QStringLiteral("text"), QString::fromStdString(token.text));
        item.insert(QStringLiteral("x"), left);
        item.insert(QStringLiteral("y"), top);
        item.insert(QStringLiteral("width"), right - left);
        item.insert(QStringLiteral("height"), bottom - top);
        item.insert(QStringLiteral("low"), token.confidence < 0.70F);
        overlayItems_.push_back(item);
    }
    activeOcrDisplay_ = ocrDisplay_ == QStringLiteral("auto")
        ? (formatter_.prefersOverlay(result) ? QStringLiteral("overlay") : QStringLiteral("reader"))
        : ocrDisplay_;
    emit ocrChanged();
}

int AppController::screenIndexForPoint(const QPointF& point) const {
    const auto screens = QGuiApplication::screens();
    for (int i = 0; i < screens.size(); ++i) {
        if (screens[i]->geometry().contains(point.toPoint())) return i;
    }
    if (QGuiApplication::primaryScreen() == nullptr) return -1;
    const auto primaryIndex = screens.indexOf(QGuiApplication::primaryScreen());
    return primaryIndex < 0 || primaryIndex > std::numeric_limits<int>::max()
        ? -1 : static_cast<int>(primaryIndex);
}

int AppController::displayIndexForScreen(int screenIndex,
                                         const std::vector<loupe::DisplayInfo>& displays) const {
    const auto screens = QGuiApplication::screens();
    if (screenIndex < 0 || screenIndex >= screens.size()) return -1;
    const auto screenName = screens[screenIndex]->name();
    const auto named = std::find_if(displays.begin(), displays.end(), [&screenName](const auto& display) {
        return QString::fromStdString(display.name).compare(screenName, Qt::CaseInsensitive) == 0;
    });
    if (named != displays.end()) return static_cast<int>(std::distance(displays.begin(), named));
    if (screens[screenIndex] == QGuiApplication::primaryScreen()) {
        const auto primary = std::find_if(displays.begin(), displays.end(),
                                          [](const auto& display) { return display.primary; });
        if (primary != displays.end()) return static_cast<int>(std::distance(displays.begin(), primary));
    }
    const auto expectedWidth = screens[screenIndex]->geometry().width()
                             * screens[screenIndex]->devicePixelRatio();
    const auto expectedHeight = screens[screenIndex]->geometry().height()
                              * screens[screenIndex]->devicePixelRatio();
    const auto closest = std::min_element(displays.begin(), displays.end(),
        [expectedWidth, expectedHeight](const auto& left, const auto& right) {
            const auto leftError = std::abs(left.physicalRect.width - expectedWidth)
                                 + std::abs(left.physicalRect.height - expectedHeight);
            const auto rightError = std::abs(right.physicalRect.width - expectedWidth)
                                  + std::abs(right.physicalRect.height - expectedHeight);
            return leftError < rightError;
        });
    return closest == displays.end() ? -1 : static_cast<int>(std::distance(displays.begin(), closest));
}

QImage AppController::toQImage(const loupe::PixelBuffer& pixels) {
    if (!pixels.valid()) return {};
    QImage::Format format = QImage::Format_ARGB32;
    if (pixels.format == loupe::PixelFormat::Rgba8) format = QImage::Format_RGBA8888;
    else if (pixels.format == loupe::PixelFormat::Gray8) format = QImage::Format_Grayscale8;
    return QImage(reinterpret_cast<const uchar*>(pixels.bytes.data()), pixels.width, pixels.height,
                  pixels.bytesPerRow, format).copy();
}

QRectF AppController::currentRegionAsLogical() const {
    const auto region = coordinator_.region();
    if (!region) return {};
    const auto displays = capture_->displays();
    const auto display = std::find_if(displays.begin(), displays.end(), [region](const auto& item) {
        return item.id == region->display;
    });
    if (display == displays.end()) return {};
    if (display->physicalRect.width <= 0 || display->physicalRect.height <= 0) return {};
    const auto screens = QGuiApplication::screens();
    const auto displayIndex = static_cast<int>(std::distance(displays.begin(), display));
    auto* screen = display->primary ? QGuiApplication::primaryScreen()
                                    : (displayIndex >= 0 && displayIndex < screens.size()
                                        ? screens[displayIndex] : nullptr);
    if (!screen) return {};
    const auto geometry = screen->geometry();
    const auto scaleX = static_cast<double>(geometry.width()) / display->physicalRect.width;
    const auto scaleY = static_cast<double>(geometry.height()) / display->physicalRect.height;
    return {
        geometry.x() + (region->userRect.x - display->physicalRect.x) * scaleX,
        geometry.y() + (region->userRect.y - display->physicalRect.y) * scaleY,
        region->userRect.width * scaleX,
        region->userRect.height * scaleY
    };
}

void AppController::resetOcrForNewSource() {
    ocrText_.clear();
    ocrStatus_ = QStringLiteral("Hold still to read");
    confidence_.clear();
    alternative_.clear();
    overlayItems_.clear();
    numericLike_ = false;
    numericText_.clear();
    emit ocrChanged();
}
