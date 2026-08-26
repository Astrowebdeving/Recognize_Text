# Document Loupe

Document Loupe is a capture-first desktop magnifier for difficult scans, remote-desktop documents, and accounting text. Point at a source to freeze and enlarge its pixels; hold still to run local OCR; or use **Select Area** for an exact manual capture.

The implementation follows one hard rule: capture and magnification never depend on OCR. Original pixels are immutable, every asynchronous result carries a generation ID, and stale OCR is discarded before publication.

## One-click macOS test

Double-click **Document Loupe.app** in the project folder. On the first real launch:

1. Approve the one-time Screen Recording request, or click **Open System Settings** when the permission explanation appears.
2. Allow Document Loupe under **Privacy & Security > Screen Recording**.
3. Quit and reopen the app if macOS requests it.
4. Move the pointer outside the loupe and hold still over text, or click **Select Area** and drag around a document region.
5. Pinch with two fingers over the loupe, use the mouse wheel/trackpad scroll, or press the −/+ buttons to zoom.

Use the permanent **×** button at the upper right of the app to quit. The macOS title-bar close control also works. Settings and selection have their own visible **×** controls and can also be dismissed with Escape.

The root app is a local one-click test launcher. It uses the Qt installation on this Mac; it is not yet a notarized, self-contained distribution package. Keep using that same root app after granting permission. macOS ties privacy grants to the app's code-signing identity, so replacing it with a newly rebuilt ad-hoc development bundle requires a new grant. A release signed with a stable Apple Development or Developer ID identity does not have that development-only limitation.

## Transferable packages

CMake/CPack now creates self-contained packages that include the Qt frameworks, QML imports, plugins, and runtime files:

```sh
cmake -S . -B build-package -DCMAKE_BUILD_TYPE=Release
cmake --build build-package --parallel
cpack --config build-package/CPackConfig.cmake -G DragNDrop -B artifacts
```

On macOS this produces a `.dmg`; on Windows the package workflow produces a one-click NSIS Setup `.exe`, a portable `.zip`, and an MSIX package. The Windows MSIX is the build that enables Microsoft's package-identity-gated local OCR API. See [packaging/windows/README.md](packaging/windows/README.md) for the exact install and launch choices.

Packaging is separate from publisher trust. Public macOS distribution still requires Developer ID signing and notarization; public Windows distribution requires Authenticode/MSIX signing or Microsoft Store submission. Unsigned or ad-hoc packages remain suitable for local testing but can trigger operating-system security warnings.

## What is implemented

- Qt Quick compact loupe with Auto, Select Area, Adjust Area, Pin, extreme stepped zoom, Pixels/Document modes, Reader/Overlay presentation, and clipboard actions.
- Native two-finger pinch zoom, accumulated wheel/trackpad zoom, and an original cross-platform application icon.
- ScreenCaptureKit capture on macOS with Screen Recording permission handling and self-application exclusion.
- DXGI Desktop Duplication (`DuplicateOutput1`) capture on Windows with finite frame acquisition, last-frame retention, and access-loss reporting.
- A common physical-pixel capture contract used by Auto, manual selection, and adjustment.
- Immutable original ROI storage and a separate deterministic grayscale/contrast/unsharp document branch.
- Bounded, latest-wins OCR scheduling with generation-checked publication.
- On-device Apple Vision OCR on macOS and Windows OCR for MSIX-installed Windows builds, behind the same recognizer interface.
- Accounting grammar, numeric-like classification, preserved punctuation, and explicit low-confidence alternatives.
- Geometry-backed Reader formatting and normalized Overlay geometry.

The PP-OCRv6 ONNX model manifest and interfaces are present, but model weights are intentionally not checked into the repository. Apple Vision keeps macOS development builds useful. Microsoft's built-in Windows OCR requires package identity, so it works only after MSIX installation; an unpackaged Windows executable remains a raw loupe until a bundled OCR backend is added. A distributable accuracy-targeted build must still bundle hash-verified PP-OCRv6 weights and connect the ONNX implementation. PP-StructureV3, neural restoration, and PaddleOCR-VL remain later gated modules rather than blockers for the core workflow.

## Build

Requirements for the desktop app:

- CMake 3.29+
- C++20 compiler
- Qt 6.10+ (`Core`, `Gui`, `Quick`, `QuickControls2`)
- macOS 13+ or Windows 10 1809+

```sh
cmake -S . -B build
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Run the high-volume stress suite with:

```sh
cmake -S . -B build-stress -DLOUPE_BUILD_APP=OFF -DLOUPE_BUILD_STRESS_TESTS=ON
cmake --build build-stress --parallel
./build-stress/loupe_stress_tests
```

If Qt is unavailable, CMake still builds and tests the dependency-free core:

```sh
cmake -S . -B build-core -DLOUPE_BUILD_APP=OFF
cmake --build build-core --parallel
ctest --test-dir build-core --output-on-failure
```

On first macOS launch, allow Screen Recording when prompted, then relaunch the app if required by the OS.

## Architecture

```text
native desktop capture
        ↓
immutable DesktopFrame
        ↓
unified RegionSnapshot + GenerationId
        ├── original pixels → Pixel magnifier (always available)
        ├── deterministic enhancement → Document view
        └── bounded local OCR → accounting validation → Reader / Overlay
```

No screenshot or OCR content is transmitted. There is no cloud path in the application.

## Current production gates

Before a signed v1 distribution, complete the repository's intentionally explicit external gates:

- bundle and hash PP-OCRv6 medium detector/recognizer ONNX weights;
- add the Windows ONNX/Windows ML recognizer backend and CPU fallback;
- run the capture/DPI matrix on physical Windows hardware;
- calibrate OCR confidence on the accounting regression dataset;
- code-sign/notarize packages.

See [docs/PORTABILITY.md](docs/PORTABILITY.md) for the platform-by-platform support matrix, completed portability work, and the remaining release gaps.
