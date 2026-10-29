#include "services.hpp"
#include "service_defaults.hpp"
#include "routed_audio.hpp"

#include "sdl_audio.hpp"
#include "portable_async.hpp"
#include "portable_runtime.hpp"
#include "sdl_ui.hpp"
#include "windows_runtime.hpp"

#include <algorithm>
#include <array>
#include <mutex>
#include <unordered_map>
#include <utility>

namespace cwxeditor::lib {
namespace {
template<class Function>
void for_each_service_member(Function&& apply) {
    apply(&Services::gui);
    apply(&Services::graphics);
    apply(&Services::image_codecs);
    apply(&Services::dialogs);
    apply(&Services::file_picker);
    apply(&Services::clipboard);
    apply(&Services::drag_drop);
    apply(&Services::audio);
    apply(&Services::filesystem);
    apply(&Services::processes);
    apply(&Services::shell);
    apply(&Services::ipc);
    apply(&Services::archives);
    apply(&Services::system);
    apply(&Services::text);
    apply(&Services::dispatcher);
    apply(&Services::tasks);
}
}

Services with_service_defaults(Services inherited, Services overrides) {
    bool complete = true;
    for_each_service_member([&](auto member) {
        if (overrides.*member) inherited.*member = std::move(overrides.*member);
        complete = complete && static_cast<bool>(inherited.*member);
    });
    // 未使用の SDL 画面基盤を起動して即座に破棄すると、入力機器の列挙と
    // 終了処理が衝突する。完全な指定には新しい基盤を追加しない。
    if (complete) return inherited;
    const auto fallback = make_platform_services();
    for_each_service_member([&](auto member) {
        if (!(inherited.*member)) inherited.*member = fallback.*member;
    });
    return inherited;
}

Services make_platform_services() {
    Services services = make_headless_services();

    // 製品の共通の表示処理は、すべてのOSでこのキューを使う。 ホスト
    // 固有のサービスで、UIイベント・入力の状態遷移を置き換えない。
    services.dispatcher = std::make_shared<EventLoopDispatcher>();
    services.tasks = make_portable_task_backend();

    // UIの実装は一つに統一し、
    // 別経路の実装を設けない。
    install_portable_runtime_services(services);
    // 共通のSDL表示処理を構築する前に、最終的なホストのシステム処理を設
    // 定する。 入力の分類と遅延編集が同じ時間間隔を参照できるようにする。
    if (auto backend = make_windows_system_backend()) {
        services.system = std::move(backend);
    }
    install_sdl_ui_services(services);

    if (auto backend = make_windows_filesystem_backend(services.dispatcher)) {
        services.filesystem = std::move(backend);
    }
    if (auto backend = make_windows_process_backend()) {
        services.processes = std::move(backend);
    }
    services.audio = std::make_shared<RoutedAudioBackend>(
        make_windows_audio_backend(), make_sdl_audio_backend());
    services.archives = make_archive_backend(services.filesystem);

    return services;
}

}  // namespace cwxeditor::lib
