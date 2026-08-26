#include "app/AppController.hpp"
#include "app/LoupeImageProvider.hpp"

#if defined(Q_OS_WIN)
#include "capture/windows/WindowCaptureExclusion.hpp"
#endif

#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QScreen>
#include <QQuickWindow>
#include <QTimer>

#include <algorithm>

namespace {

void constrainToAvailableScreen(QQuickWindow* window) {
    if (window == nullptr) return;
    auto* screen = window->screen();
    if (screen == nullptr) screen = QGuiApplication::primaryScreen();
    if (screen == nullptr) return;
    const auto available = screen->availableGeometry();
    if (available.isEmpty()) return;

    const auto maximumWidth = std::max(1, available.width());
    const auto maximumHeight = std::max(1, available.height());
    window->setMinimumWidth(std::min(680, maximumWidth));
    window->setMinimumHeight(std::min(620, maximumHeight));
    window->setMaximumWidth(maximumWidth);
    window->setMaximumHeight(maximumHeight);

    const auto width = std::clamp(window->width(), window->minimumWidth(), maximumWidth);
    const auto height = std::clamp(window->height(), window->minimumHeight(), maximumHeight);
    window->resize(width, height);
    window->setPosition(
        std::clamp(window->x(), available.x(), available.x() + available.width() - width),
        std::clamp(window->y(), available.y(), available.y() + available.height() - height));
}

void monitorScreenBounds(QQuickWindow* window, QScreen* screen) {
    if (window == nullptr || screen == nullptr) return;
    QObject::connect(screen, &QScreen::availableGeometryChanged, window,
                     [window](const QRect&) { constrainToAvailableScreen(window); });
}

} // namespace

int main(int argc, char* argv[]) {
    QGuiApplication application(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("Document Loupe"));
    QGuiApplication::setOrganizationName(QStringLiteral("OpenAI"));
    QGuiApplication::setOrganizationDomain(QStringLiteral("openai.com"));
    QGuiApplication::setWindowIcon(QIcon(QStringLiteral(
        ":/branding/document-loupe-icon-master.png")));

    auto* imageProvider = new LoupeImageProvider();
    // The controller must outlive the QML engine: context properties are read
    // while the engine destroys its object tree during application shutdown.
    AppController controller(imageProvider);
    QQmlApplicationEngine engine;
    engine.addImageProvider(QStringLiteral("loupe"), imageProvider);
    engine.rootContext()->setContextProperty(QStringLiteral("controller"), &controller);

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                     &application, [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule(QStringLiteral("DocumentLoupe"), QStringLiteral("Main"));

    if (!engine.rootObjects().isEmpty()) {
        if (auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().front())) {
            constrainToAvailableScreen(window);
            monitorScreenBounds(window, window->screen());
            QObject::connect(window, &QQuickWindow::screenChanged, window,
                             [window](QScreen* screen) {
                                 constrainToAvailableScreen(window);
                                 monitorScreenBounds(window, screen);
                             });
            window->show();
        }
    }

#if defined(Q_OS_WIN)
    QTimer::singleShot(0, &application, [] {
        for (auto* window : QGuiApplication::topLevelWindows())
            loupe::excludeWindowFromCapture(window->winId());
    });
#endif
    const auto arguments = QCoreApplication::arguments();
    if (!arguments.contains(QStringLiteral("--ui-test")))
        QTimer::singleShot(0, &controller, &AppController::start);
    if (arguments.contains(QStringLiteral("--smoke-test"))) {
        QTimer::singleShot(750, &application, &QCoreApplication::quit);
    }
    return application.exec();
}
