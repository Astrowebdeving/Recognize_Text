# Portability and release-readiness review

## Platform status

| Target | Current implementation | Verification status |
| --- | --- | --- |
| macOS 13+ on Apple silicon | ScreenCaptureKit capture, own-app exclusion, Screen Recording permission UI, Apple Vision local OCR | Built and smoke-tested on arm64 macOS; native bridges pass strict syntax checks |
| Windows 11 x64/ARM64 | DXGI 1.5 Desktop Duplication, per-output D3D11 devices, capture-affinity exclusion, Windows local OCR when MSIX-installed | Source and CI target are present; physical Windows validation is still mandatory |
| Windows 10 1809+ | Same DXGI path with feature-level retry; Windows OCR requires package identity | Expected to compile under MSVC 2022; hardware and RDP test matrix remains open |
| Lenovo laptops | Lenovo requires no special backend; support depends on the installed Windows version, display driver, and CPU/GPU architecture | Intel/AMD/NVIDIA Lenovo hardware must be included in release qualification |
| Linux | No capture backend | Explicitly unsupported by this specification |

“Lenovo” is a hardware vendor rather than a separate platform. A ThinkPad running supported Windows uses the Windows backend; graphics-driver behavior and mixed-DPI layouts are the important variables.

## Portability work already present

- Platform APIs are isolated behind `IScreenCapture` and `ITextRecognizer`.
- Objective-C and ScreenCaptureKit types do not leak out of `.mm` files.
- UI, accounting validation, image processing, state coordination, and asynchronous scheduling are shared C++20 code.
- Windows creates one duplication pipeline per active adapter/output, retries D3D feature-level negotiation, uses finite frame acquisition, retains the last valid frame on timeout, and recreates capture after access loss or topology changes.
- Coordinates are converted at the Qt/native boundary and OCR geometry remains normalized to the immutable source ROI.
- Native OCR failures cannot disable raw capture or magnification.
- CI compiles the dependency-free core and Qt application on macOS and Windows.

## Known release gaps

This repository is a working MVP, not the specification’s release-ready v1.0 yet:

1. PP-OCRv6 ONNX sessions, Windows ML provider selection, model downloads, and production hashes are not integrated. Apple Vision is a local macOS fallback. Windows OCR is available only to an MSIX-installed build because Microsoft requires package identity.
2. PP-StructureV3, PaddleOCR-VL, and a validated neural restoration model are not implemented.
3. Auto capture currently uses a stable pointer-centered ROI; semantic detector-driven snapping and hysteresis scoring remain to be added.
4. A manual selection spanning two displays is currently limited to the display containing its center. Exact mixed-DPI, cross-display composition needs a dedicated multi-surface representation.
5. Adjust Area supports redraw/reselection but not draggable edge/corner handles.
6. ScreenCaptureKit currently materializes complete frames into CPU memory. Production performance work should retain IOSurfaces and crop before CPU transfer.
7. The Windows backend must be compiled and exercised on real Intel, AMD, and NVIDIA laptops, including RDP, mixed DPI, sleep/wake, hot-plug, HDR, rotation, and graphics reset.
8. Installers, code signing, notarization, model licensing review, and calibrated accounting OCR benchmarks remain external release gates.

These gaps are intentionally surfaced because silently describing native fallback OCR as PP-OCRv6—or claiming untested Lenovo hardware support—would be misleading.
