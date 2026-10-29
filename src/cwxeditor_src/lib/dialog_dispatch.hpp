#pragma once

#include "dialogs.hpp"
#include "threading.hpp"

#include <memory>

namespace cwxeditor::lib {

// OSの選択画面を含む、全ダイアログサービス共通のスレッド境界
// 。 UI所有者はイベントループの破棄前にディスパッチャーを
// 終了させる。 保留呼び出しはGUIへ触れず中止として戻る。
[[nodiscard]] std::shared_ptr<DialogBackend> make_dispatched_dialog_backend(
    std::shared_ptr<DialogBackend> backend, std::shared_ptr<MainDispatcher> dispatcher);

} // namespace cwxeditor::lib
