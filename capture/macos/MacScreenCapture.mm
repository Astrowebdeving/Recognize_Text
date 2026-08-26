#include "capture/macos/MacScreenCapture.hpp"

#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>

#include <atomic>
#include <algorithm>
#include <cstring>
#include <limits>
#include <mutex>
#include <new>
#include <unordered_map>
#include <utility>

@interface LoupeStreamOutput : NSObject <SCStreamOutput>
@property(nonatomic, copy) void (^frameHandler)(CMSampleBufferRef sampleBuffer);
@end

@implementation LoupeStreamOutput
- (void)stream:(SCStream*)stream
    didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer
                  ofType:(SCStreamOutputType)type {
    (void)stream;
    if (type == SCStreamOutputTypeScreen && self.frameHandler != nil) {
        self.frameHandler(sampleBuffer);
    }
}
@end

namespace loupe {

namespace {

struct StreamState {
    DisplayInfo info;
    SCStream* stream{nil};
    LoupeStreamOutput* output{nil};
    dispatch_queue_t queue{nil};
    mutable std::mutex frameMutex;
    std::shared_ptr<const DesktopFrame> latest;
};

PixelRect displayPhysicalRect(SCDisplay* display) {
    const auto bounds = CGDisplayBounds(display.displayID);
    const auto scaleX = bounds.size.width > 0.0
        ? static_cast<double>(display.width) / bounds.size.width : 1.0;
    const auto scaleY = bounds.size.height > 0.0
        ? static_cast<double>(display.height) / bounds.size.height : 1.0;
    return {
        static_cast<int32_t>(std::llround(bounds.origin.x * scaleX)),
        static_cast<int32_t>(std::llround(bounds.origin.y * scaleY)),
        static_cast<int32_t>(display.width),
        static_cast<int32_t>(display.height)
    };
}

std::string errorMessage(NSError* error, const char* fallback) {
    if (error == nil) return fallback;
    const char* utf8 = error.localizedDescription.UTF8String;
    return utf8 == nullptr ? std::string(fallback) : std::string(utf8);
}

} // namespace

struct MacScreenCapture::Impl {
    mutable std::mutex mutex;
    std::unordered_map<DisplayId, std::shared_ptr<StreamState>> streams;
    CaptureStatus current{CaptureStatusCode::Stopped, "Capture stopped"};
    std::atomic<FrameId> nextFrame{1};
    bool stopping{};

    void setStatus(CaptureStatus status) {
        std::scoped_lock lock(mutex);
        current = std::move(status);
    }
};

MacScreenCapture::MacScreenCapture() : impl_(std::make_shared<Impl>()) {}

MacScreenCapture::~MacScreenCapture() { stop(); }

CaptureStatus MacScreenCapture::start() {
    if (!CGPreflightScreenCaptureAccess()) {
        NSUserDefaults* defaults = NSUserDefaults.standardUserDefaults;
        // V2 intentionally follows the final bundle/executable identity fix.
        // It gives that identity one native request, while still preventing
        // repeated prompts on ordinary relaunches.
        NSString* const requestKey = @"DocumentLoupeScreenCapturePermissionPromptedV2";
        const bool alreadyRequested = [defaults boolForKey:requestKey];
        if (!alreadyRequested) {
            // TCC owns the permission grant. We persist only that the native
            // request was shown, preventing a prompt loop on later launches.
            [defaults setBool:YES forKey:requestKey];
            [defaults synchronize];
            (void)CGRequestScreenCaptureAccess();
        }
        if (!CGPreflightScreenCaptureAccess()) {
            impl_->setStatus({CaptureStatusCode::PermissionRequired,
                              alreadyRequested
                                ? "Screen Recording permission is not active. Enable Document Loupe once in System Settings > Privacy & Security > Screen Recording."
                                : "Screen Recording permission was requested once. Enable Document Loupe in System Settings, then return here or reopen the app."});
            return status();
        }
    }

    {
        std::scoped_lock lock(impl_->mutex);
        if (!impl_->streams.empty()) return impl_->current;
        impl_->stopping = false;
        impl_->current = {CaptureStatusCode::Starting, "Starting screen capture…"};
    }

    std::weak_ptr<Impl> weakImplementation = impl_;
    [SCShareableContent getShareableContentExcludingDesktopWindows:NO
                                              onScreenWindowsOnly:YES
                                               completionHandler:^(SCShareableContent* content, NSError* error) {
        auto implementation = weakImplementation.lock();
        if (!implementation) return;
        if (error != nil || content.displays.count == 0) {
            const auto message = errorMessage(error, "No active display is available");
            implementation->setStatus({CaptureStatusCode::TemporarilyUnavailable, message});
            return;
        }

        SCRunningApplication* ownApplication = nil;
        const auto pid = NSProcessInfo.processInfo.processIdentifier;
        for (SCRunningApplication* application in content.applications) {
            if (application.processID == pid) {
                ownApplication = application;
                break;
            }
        }
        NSArray<SCRunningApplication*>* exclusions = ownApplication == nil
            ? @[] : @[ ownApplication ];

        for (SCDisplay* display in content.displays) {
            if (display.width <= 0 || display.height <= 0) continue;
            auto state = std::make_shared<StreamState>();
            state->info.id = static_cast<DisplayId>(display.displayID);
            state->info.physicalRect = displayPhysicalRect(display);
            const auto bounds = CGDisplayBounds(display.displayID);
            state->info.devicePixelRatio = bounds.size.width > 0.0
                ? static_cast<double>(display.width) / bounds.size.width : 1.0;
            state->info.name = "Display " + std::to_string(display.displayID);
            state->info.primary = CGDisplayIsMain(display.displayID) != 0;
            state->queue = dispatch_queue_create("com.openai.documentloupe.capture", DISPATCH_QUEUE_SERIAL);
            state->output = [LoupeStreamOutput new];

            std::weak_ptr<StreamState> weakState = state;
            std::weak_ptr<Impl> frameImplementation = implementation;
            state->output.frameHandler = ^(CMSampleBufferRef sampleBuffer) {
                auto locked = weakState.lock();
                auto implementationForFrame = frameImplementation.lock();
                if (!locked || !implementationForFrame || !CMSampleBufferIsValid(sampleBuffer)) return;
                CVImageBufferRef imageBuffer = CMSampleBufferGetImageBuffer(sampleBuffer);
                if (imageBuffer == nullptr) return;
                CVPixelBufferRef pixelBuffer = static_cast<CVPixelBufferRef>(imageBuffer);
                if (CVPixelBufferLockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly)
                    != kCVReturnSuccess) return;
                struct UnlockGuard {
                    CVPixelBufferRef buffer;
                    ~UnlockGuard() { CVPixelBufferUnlockBaseAddress(buffer, kCVPixelBufferLock_ReadOnly); }
                } unlock{pixelBuffer};
                const auto rawWidth = CVPixelBufferGetWidth(pixelBuffer);
                const auto rawHeight = CVPixelBufferGetHeight(pixelBuffer);
                const auto stride = CVPixelBufferGetBytesPerRow(pixelBuffer);
                const auto* base = static_cast<const std::byte*>(CVPixelBufferGetBaseAddress(pixelBuffer));
                if (base == nullptr || rawWidth == 0 || rawHeight == 0 ||
                    rawWidth > static_cast<size_t>(std::numeric_limits<int32_t>::max() / 4) ||
                    rawHeight > static_cast<size_t>(std::numeric_limits<int32_t>::max())) return;
                const auto rowBytes = rawWidth * 4U;
                if (stride < rowBytes || rowBytes > std::numeric_limits<size_t>::max() / rawHeight)
                    return;

                std::shared_ptr<PixelBuffer> pixels;
                try {
                    pixels = std::make_shared<PixelBuffer>();
                    pixels->width = static_cast<int32_t>(rawWidth);
                    pixels->height = static_cast<int32_t>(rawHeight);
                    pixels->bytesPerRow = static_cast<int32_t>(rowBytes);
                    pixels->format = PixelFormat::Bgra8;
                    pixels->bytes.resize(rowBytes * rawHeight);
                } catch (const std::bad_alloc&) {
                    implementationForFrame->setStatus({CaptureStatusCode::TemporarilyUnavailable,
                                                       "Not enough memory for a desktop frame"});
                    return;
                }
                for (size_t y = 0; y < rawHeight; ++y) {
                    std::memcpy(pixels->bytes.data() + y * rowBytes,
                                base + y * stride, rowBytes);
                }

                auto frame = std::make_shared<DesktopFrame>();
                frame->frameId = implementationForFrame->nextFrame.fetch_add(1, std::memory_order_relaxed);
                frame->display = locked->info.id;
                frame->physicalRect = locked->info.physicalRect;
                frame->pixels = std::move(pixels);
                frame->capturedAt = std::chrono::steady_clock::now();
                {
                    std::scoped_lock frameLock(locked->frameMutex);
                    locked->latest = std::move(frame);
                }
            };

            SCContentFilter* filter = [[SCContentFilter alloc] initWithDisplay:display
                                                        excludingApplications:exclusions
                                                           exceptingWindows:@[]];
            SCStreamConfiguration* configuration = [SCStreamConfiguration new];
            configuration.width = static_cast<size_t>(display.width);
            configuration.height = static_cast<size_t>(display.height);
            configuration.minimumFrameInterval = CMTimeMake(1, 60);
            configuration.queueDepth = 3;
            configuration.pixelFormat = kCVPixelFormatType_32BGRA;
            configuration.showsCursor = NO;
            configuration.capturesAudio = NO;

            state->stream = [[SCStream alloc] initWithFilter:filter
                                              configuration:configuration
                                                   delegate:nil];
            NSError* outputError = nil;
            if (![state->stream addStreamOutput:state->output
                                           type:SCStreamOutputTypeScreen
                             sampleHandlerQueue:state->queue
                                          error:&outputError]) {
                continue;
            }

            {
                std::scoped_lock lock(implementation->mutex);
                if (implementation->stopping) return;
                implementation->streams[state->info.id] = state;
            }
            std::weak_ptr<Impl> startImplementation = implementation;
            std::weak_ptr<StreamState> startState = state;
            [state->stream startCaptureWithCompletionHandler:^(NSError* startError) {
                auto implementationForStart = startImplementation.lock();
                auto stateForStart = startState.lock();
                if (!implementationForStart || !stateForStart) return;
                bool stopOrphan = false;
                {
                    std::scoped_lock lock(implementationForStart->mutex);
                    const auto found = implementationForStart->streams.find(stateForStart->info.id);
                    stopOrphan = implementationForStart->stopping ||
                                 found == implementationForStart->streams.end() ||
                                 found->second.get() != stateForStart.get();
                    if (!stopOrphan) {
                        implementationForStart->current = startError == nil
                            ? CaptureStatus{CaptureStatusCode::Ready, "Capture ready"}
                            : CaptureStatus{CaptureStatusCode::TemporarilyUnavailable,
                                            errorMessage(startError, "Screen capture failed to start")};
                    }
                }
                if (stopOrphan)
                    [stateForStart->stream stopCaptureWithCompletionHandler:nil];
            }];
        }
        {
            std::scoped_lock lock(implementation->mutex);
            if (!implementation->stopping && implementation->streams.empty()) {
                implementation->current = {CaptureStatusCode::TemporarilyUnavailable,
                                           "No display capture stream could be created"};
            }
        }
    }];
    return status();
}

void MacScreenCapture::stop() noexcept {
    std::unordered_map<DisplayId, std::shared_ptr<StreamState>> streams;
    {
        std::scoped_lock lock(impl_->mutex);
        impl_->stopping = true;
        streams.swap(impl_->streams);
        impl_->current = {CaptureStatusCode::Stopped, "Capture stopped"};
    }
    for (const auto& [id, state] : streams) {
        (void)id;
        state->output.frameHandler = nil;
        [state->stream stopCaptureWithCompletionHandler:nil];
    }
}

std::shared_ptr<const DesktopFrame> MacScreenCapture::latestFrame(DisplayId display) {
    std::shared_ptr<StreamState> stream;
    {
        std::scoped_lock lock(impl_->mutex);
        const auto found = impl_->streams.find(display);
        if (found == impl_->streams.end()) return {};
        stream = found->second;
    }
    std::scoped_lock lock(stream->frameMutex);
    return stream->latest;
}

std::vector<DisplayInfo> MacScreenCapture::displays() const {
    std::vector<DisplayInfo> result;
    std::scoped_lock lock(impl_->mutex);
    result.reserve(impl_->streams.size());
    for (const auto& [id, stream] : impl_->streams) {
        (void)id;
        result.push_back(stream->info);
    }
    std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
        if (left.primary != right.primary) return left.primary;
        if (left.physicalRect.x != right.physicalRect.x)
            return left.physicalRect.x < right.physicalRect.x;
        return left.physicalRect.y < right.physicalRect.y;
    });
    return result;
}

CaptureStatus MacScreenCapture::status() const {
    std::scoped_lock lock(impl_->mutex);
    return impl_->current;
}

} // namespace loupe
