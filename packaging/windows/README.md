# Windows packages

The package workflow produces three Windows x64 artifacts (the computer brand,
including Lenovo, Dell, or HP, does not matter):

- `Document-Loupe-*-Windows-*.exe`: NSIS one-click Setup program with Start Menu shortcut and uninstaller.
- `Document-Loupe-*-Windows-*.zip`: portable self-contained folder.
- `Document-Loupe-x64.msix`: full-trust MSIX package with Windows package identity.

All formats contain the application, Qt runtime libraries, QML imports, plugins, compiler runtime, and icons. DXGI capture and magnification work in the NSIS and portable builds. The current native `Windows.Media.Ocr` fallback requires package identity, so OCR is enabled only when the MSIX build is installed. This is a Microsoft platform restriction, not an installer preference.

## Install and run

For the simplest capture-and-zoom installation, double-click the Setup `.exe`,
finish the short installer, and open **Document Loupe** from the Start Menu. The
installer also offers to launch the app immediately and adds a normal Windows
uninstaller under **Settings > Apps > Installed apps**.

For capture, zoom, and OCR, install the signed `.msix` instead. Double-click it,
choose **Install**, and launch **Document Loupe** from the Start Menu. A public
MSIX must be signed by a certificate Windows trusts; an unsigned development
MSIX is a build artifact, not a frictionless end-user installer.

The portable `.zip` is intended for testing without installation: extract the
whole folder and run `bin\DocumentLoupe.exe`. Do not copy only the `.exe`, because
its adjacent Qt libraries and plugins are required.

## Build locally on Windows

Use Visual Studio 2022, CMake 3.29+, Qt 6.10+, and NSIS 3.03+:

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
cpack --config build/CPackConfig.cmake -G NSIS -B artifacts
cpack --config build/CPackConfig.cmake -G ZIP -B artifacts
./packaging/windows/package-msix.ps1 `
  -BuildDirectory build `
  -StageDirectory msix-stage `
  -OutputDirectory artifacts
```

`package-msix.ps1` can sign the resulting MSIX when `-CertificatePath` and `-CertificatePassword` are supplied. The configured `LOUPE_MSIX_PUBLISHER` must exactly match the certificate subject. Do not distribute a fabricated or implicitly trusted certificate. For a public one-click MSIX, sign with a CA-trusted certificate/Azure Artifact Signing or submit the package to Microsoft Store. A self-signed testing certificate must be explicitly trusted on each test PC first.
