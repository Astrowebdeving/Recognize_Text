cmake_minimum_required(VERSION 3.29)

# Set the actual minimum supported macOS version before project() runs so clean
# CI machines use the same SDK availability and ABI settings as release builds.
if(APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING
        "Minimum macOS version supported by Document Loupe")
endif()

project(DocumentLoupe VERSION 0.1.3 LANGUAGES CXX)

include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(LOUPE_BUILD_APP "Build the Qt Quick desktop application" ON)
option(LOUPE_BUILD_TESTS "Build unit tests" ON)
option(LOUPE_BUILD_STRESS_TESTS "Build randomized and concurrent stress tests" OFF)
set(LOUPE_MACOS_SIGN_IDENTITY "" CACHE STRING
    "Optional Apple code-signing identity used while creating a deployed macOS bundle")
set(LOUPE_MSIX_IDENTITY_NAME "DocumentLoupe" CACHE STRING
    "Windows package identity name assigned by the Store or publisher")
set(LOUPE_MSIX_PUBLISHER "CN=Document Loupe Project" CACHE STRING
    "Windows MSIX publisher; must exactly match the package-signing certificate subject")

add_library(loupe_core
    core/coordinates/Coordinates.cpp
    core/jobs/RecognitionScheduler.cpp
    core/pipeline/ImageProcessing.cpp
    core/state/CaptureCoordinator.cpp
    vision/accounting/AccountingValidator.cpp
    vision/layout/ReaderFormatter.cpp
)
target_include_directories(loupe_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(loupe_core PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(loupe_core PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(loupe_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(LOUPE_BUILD_APP)
    find_package(Qt6 6.10 QUIET COMPONENTS Core Gui Quick QuickControls2)
    if(Qt6_FOUND)
        qt_standard_project_setup(REQUIRES 6.10)
        qt_policy(SET QTP0004 NEW)

        set(LOUPE_PLATFORM_SOURCES)
        set(LOUPE_PLATFORM_LIBRARIES)

        if(APPLE)
            enable_language(OBJCXX)
            set(LOUPE_MACOS_ICON "${CMAKE_CURRENT_SOURCE_DIR}/assets/DocumentLoupe.icns")
            set_source_files_properties(${LOUPE_MACOS_ICON} PROPERTIES
                MACOSX_PACKAGE_LOCATION "Resources"
            )
            list(APPEND LOUPE_PLATFORM_SOURCES
                capture/macos/MacScreenCapture.mm
                vision/recognition/VisionTextRecognizer.mm
                ${LOUPE_MACOS_ICON}
            )
            set_source_files_properties(
                capture/macos/MacScreenCapture.mm
                vision/recognition/VisionTextRecognizer.mm
                PROPERTIES COMPILE_FLAGS "-fobjc-arc"
            )
            list(APPEND LOUPE_PLATFORM_LIBRARIES
                "-framework AppKit"
                "-framework CoreGraphics"
                "-framework CoreMedia"
                "-framework CoreVideo"
                "-framework ScreenCaptureKit"
                "-framework Vision"
            )
        elseif(WIN32)
            set(LOUPE_WINDOWS_ICON "${CMAKE_CURRENT_SOURCE_DIR}/assets/DocumentLoupe.ico")
            configure_file(packaging/windows/DocumentLoupe.rc.in
                           ${CMAKE_CURRENT_BINARY_DIR}/DocumentLoupe.rc @ONLY)
            list(APPEND LOUPE_PLATFORM_SOURCES
                capture/windows/DxgiDesktopCapture.cpp
                capture/windows/WindowCaptureExclusion.cpp
                vision/recognition/WindowsTextRecognizer.cpp
                ${CMAKE_CURRENT_BINARY_DIR}/DocumentLoupe.rc
            )
            list(APPEND LOUPE_PLATFORM_LIBRARIES d3d11 dxgi shcore user32 windowsapp)
        else()
            list(APPEND LOUPE_PLATFORM_SOURCES capture/UnsupportedScreenCapture.cpp)
        endif()

        qt_add_executable(document-loupe
            app/main.cpp
            app/AppController.cpp
            app/LoupeImageProvider.cpp
            ${LOUPE_PLATFORM_SOURCES}
        )

        qt_add_qml_module(document-loupe
            URI DocumentLoupe
            VERSION 1.0
            QML_FILES
                ui/Main.qml
                ui/Loupe.qml
                ui/Reader.qml
                ui/OcrOverlay.qml
                ui/ManualSelector.qml
                ui/Settings.qml
        )

        qt_add_resources(document-loupe loupe_brand
            PREFIX "/branding"
            BASE "${CMAKE_CURRENT_SOURCE_DIR}/assets"
            FILES "${CMAKE_CURRENT_SOURCE_DIR}/assets/document-loupe-icon-master.png"
        )

        target_link_libraries(document-loupe PRIVATE
            loupe_core
            Qt6::Core
            Qt6::Gui
            Qt6::Quick
            Qt6::QuickControls2
            ${LOUPE_PLATFORM_LIBRARIES}
        )
        target_compile_definitions(document-loupe PRIVATE
            QT_NO_CAST_FROM_ASCII
            QT_NO_CAST_TO_ASCII
        )
        if(MSVC)
            target_compile_options(document-loupe PRIVATE /W4 /permissive- /utf-8)
            target_compile_definitions(document-loupe PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
        else()
            target_compile_options(document-loupe PRIVATE -Wall -Wextra -Wpedantic)
        endif()
        set_target_properties(document-loupe PROPERTIES
            # Keep a stable, conventional internal executable name. The
            # user-facing bundle/shortcut remains "Document Loupe". Avoiding
            # spaces here is required because macdeployqt passes QML plug-in
            # paths through command-line deployment tools.
            OUTPUT_NAME "DocumentLoupe"
            MACOSX_BUNDLE TRUE
            MACOSX_BUNDLE_BUNDLE_NAME "Document Loupe"
            MACOSX_BUNDLE_GUI_IDENTIFIER "com.openai.documentloupe"
            WIN32_EXECUTABLE TRUE
        )
        if(APPLE)
            set_target_properties(document-loupe PROPERTIES
                MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/packaging/macos/Info.plist.in"
                MACOSX_BUNDLE_ICON_FILE "DocumentLoupe.icns"
            )
        endif()

        install(TARGETS document-loupe
            BUNDLE DESTINATION .
            RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
        )

        if(APPLE)
            # Qt's generated QML deployment script passes every copied QML
            # plug-in back to macdeployqt as an additional executable. Some
            # Homebrew Qt builds mishandle those paths. The documented
            # macdeployqt -qmldir flow performs the same scan atomically.
            find_program(LOUPE_MACDEPLOYQT_EXECUTABLE
                NAMES macdeployqt
                HINTS "${Qt6_DIR}/../../../bin"
                REQUIRED
            )
            configure_file(
                packaging/macos/DeployMac.cmake.in
                "${CMAKE_CURRENT_BINARY_DIR}/DeployMac.cmake"
                @ONLY
            )
            install(SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/DeployMac.cmake")
        else()
            qt_generate_deploy_qml_app_script(
                TARGET document-loupe
                OUTPUT_SCRIPT LOUPE_QT_DEPLOY_SCRIPT
            )
            install(SCRIPT "${LOUPE_QT_DEPLOY_SCRIPT}")
        endif()

        set(CPACK_PACKAGE_NAME "Document Loupe")
        set(CPACK_PACKAGE_VENDOR "Document Loupe Project")
        set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
            "Local screen magnifier and document OCR utility")
        set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
        set(CPACK_PACKAGE_INSTALL_DIRECTORY "Document Loupe")
        set(CPACK_PACKAGE_FILE_NAME
            "Document-Loupe-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")

        if(APPLE)
            set(CPACK_GENERATOR "DragNDrop;ZIP")
            set(CPACK_DMG_VOLUME_NAME "Document Loupe")
            set(CPACK_DMG_FORMAT "UDZO")
        elseif(WIN32)
            set(CPACK_GENERATOR "NSIS;ZIP")
            set(CPACK_PACKAGE_EXECUTABLES "DocumentLoupe" "Document Loupe")
            set(CPACK_NSIS_PACKAGE_NAME "Document Loupe")
            set(CPACK_NSIS_DISPLAY_NAME "Document Loupe")
            set(CPACK_NSIS_MUI_ICON "${CMAKE_CURRENT_SOURCE_DIR}/assets/DocumentLoupe.ico")
            set(CPACK_NSIS_MUI_UNIICON "${CMAKE_CURRENT_SOURCE_DIR}/assets/DocumentLoupe.ico")
            set(CPACK_NSIS_INSTALLED_ICON_NAME "bin\\\\DocumentLoupe.exe")
            set(CPACK_NSIS_MUI_FINISHPAGE_RUN "bin\\\\DocumentLoupe.exe")
            set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
            set(CPACK_NSIS_MANIFEST_DPI_AWARE ON)
            set(CPACK_NSIS_MODIFY_PATH OFF)

            set(LOUPE_MSIX_VERSION "${PROJECT_VERSION}.0")
            set(LOUPE_MSIX_ARCHITECTURE "x64")
            if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(ARM64|arm64|aarch64)$")
                set(LOUPE_MSIX_ARCHITECTURE "arm64")
            endif()
            configure_file(
                packaging/windows/AppxManifest.xml.in
                "${CMAKE_CURRENT_BINARY_DIR}/AppxManifest.xml"
                @ONLY
            )
        endif()

        include(CPack)
    else()
        message(WARNING "Qt 6.10+ was not found; the desktop app target is skipped. Core and tests remain available.")
    endif()
endif()

if(LOUPE_BUILD_TESTS)
    enable_testing()
    add_executable(loupe_tests tests/unit/CoreTests.cpp)
    target_link_libraries(loupe_tests PRIVATE loupe_core)
    add_test(NAME loupe_tests COMMAND loupe_tests)
endif()

if(LOUPE_BUILD_STRESS_TESTS)
    add_executable(loupe_stress_tests tests/stress/StressTests.cpp)
    target_link_libraries(loupe_stress_tests PRIVATE loupe_core)
    if(MSVC)
        target_compile_options(loupe_stress_tests PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(loupe_stress_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()
