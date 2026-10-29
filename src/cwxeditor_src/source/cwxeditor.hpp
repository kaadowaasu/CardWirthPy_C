#pragma once

#include "cwxeditor_src/core/cwx/structs.hpp"
#include "cwxeditor_src/lib/components.hpp"
#include "cwxeditor_src/lib/runtime_context.hpp"
#include "cwxeditor_src/lib/services.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace cwxeditor::cwx::editor::gui::dwt::dprops {
class Props;
}
namespace cwxeditor::cwx::system {
class System;
}

namespace cwxeditor::cwxeditor {

namespace lib = ::cwxeditor::lib;
namespace structs = ::cwxeditor::cwx::structs;
namespace system = ::cwxeditor::cwx::system;

using String = lib::String;
using Arguments = lib::Array<String>;

/// Dランタイムとの境界用に保持する実行時オプション。
extern std::vector<String> rt_options;

/// 具体的なメインウィンドウモジュールが使う、
/// 共通の起動契約。コールバックの間、入口の処
/// 理がシステム、プロパティ、サービスを所有す
/// る。メインウィンドウ側はコールバック終了後
/// にこれらのポインターを保持してはならない。
struct MainWindowStartup {
    String appPath;
    system::System* system{};
    ::cwxeditor::cwx::editor::gui::dwt::dprops::Props* properties{};
    structs::LaunchOption options;
    lib::Services services;
    lib::EditorRuntimeContext* runtimeContext{};
    std::shared_ptr<lib::Window> window;
};

using MainWindowRunner = std::function<void(MainWindowStartup&)>;

/// GUIモジュールが提供するメインウィンドウの実行処理を設定する。
void setMainWindowRunner(MainWindowRunner runner);

/// 現在設定されているメインウィンドウの実行処理を取得する。
[[nodiscard]] MainWindowRunner mainWindowRunner();

/// Dのモジュールレベルのmain関数を、具体的なC++の引数型で提供する。
void main(const Arguments& args);

/// サービスを初期化し、オプションを解析してEditorを起動する。
void mainImpl(const Arguments& args);

/// argc/argvを渡すホスト向けのCランタイム用アダプター。
[[nodiscard]] int run(int argc, char* const* argv);

}  // namespace cwxeditor::cwxeditor
