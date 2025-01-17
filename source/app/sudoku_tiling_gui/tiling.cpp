///
/// Copyright Matus Chochlik.
/// Distributed under the GNU GENERAL PUBLIC LICENSE version 3.
/// See http://www.gnu.org/licenses/gpl-3.0.txt
///

import eagine.core;
import eagine.msgbus;
#include "PaintedImage.hpp"
#include "TilingBackend.hpp"
#include "TilingViewModel.hpp"
#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <clocale>

namespace eagine {
//------------------------------------------------------------------------------
static int argc_copy = 0;
static const char** argv_copy = nullptr;
//------------------------------------------------------------------------------
auto main(main_ctx& ctx) -> int {
    enable_message_bus(ctx);
    ctx.log().info("message bus tiling starting");

    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication app{argc_copy, const_cast<char**>(argv_copy)};
    app.setOrganizationName("EAGine");
    app.setOrganizationDomain("oglplus.org");
    app.setApplicationName("Tiling");

    const auto registerId = "com.github.matus_chochlik.eagine.msgbus.tiling";

    qmlRegisterUncreatableType<TilingTheme>(registerId, 1, 0, "TilingTheme", {});
    qmlRegisterUncreatableType<TilingViewModel>(
      registerId, 1, 0, "TilingViewModel", {});
    qmlRegisterType<PaintedImage>(registerId, 1, 0, "PaintedImage");
    qRegisterMetaType<const QImage*>("const QImage*");

    TilingBackend backend(ctx);
    QQmlApplicationEngine engine;

    engine.rootContext()->setContextProperty("backend", &backend);
    engine.load("qrc:///tiling.qml");

    return app.exec();
}
//------------------------------------------------------------------------------
} // namespace eagine

auto main(int argc, const char** argv) -> int {
    eagine::argc_copy = argc;
    eagine::argv_copy = argv;
    eagine::main_ctx_options options;
    options.app_id = "TilingExe";
    return eagine::main_impl(argc, argv, options, eagine::main);
}
