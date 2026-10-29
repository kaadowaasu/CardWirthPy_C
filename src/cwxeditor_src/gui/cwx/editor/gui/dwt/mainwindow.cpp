#include "cwxeditor_src/lib/ui_metrics.hpp"
#include "cwxeditor_src/lib/ui_edit_preferences.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/mainwindow.hpp"
#include "cwxeditor_src/lib/editor_diagnostics.hpp"
#include "cwxeditor_src/lib/file_save_transaction.hpp"
#include "cwxeditor_src/lib/runtime_context.hpp"
#include "cwxeditor_src/lib/ui_actions.hpp"
#include "cwxeditor_src/lib/ui_window_stack.hpp"
#include "cwxeditor_src/lib/ui_value_dialogs.hpp"
#include "cwxeditor_src/lib/window_host.hpp"
#include "cwxeditor_src/lib/windows_runtime.hpp"
#include "platform/storage/storage.hpp"


#include "cwxeditor_src/core/cwx/enumutils.hpp"
#include "cwxeditor_src/core/cwx/binary.hpp"
#include "cwxeditor_src/core/cwx/utils.hpp"
#include "cwxeditor_src/core/cwx/cab.hpp"
#include "cwxeditor_src/core/cwx/cwl.hpp"
#include "cwxeditor_src/core/cwx/menu.hpp"
#include "cwxeditor_src/core/cwx/path.hpp"
#include "cwxeditor_src/core/cwx/structs.hpp"
#include "cwxeditor_src/core/cwx/versioninfo.hpp"
#include "cwxeditor_src/core/cwx/warning.hpp"
#include "cwxeditor_src/core/cwx/area.hpp"
#include "cwxeditor_src/core/cwx/background.hpp"
#include "cwxeditor_src/core/cwx/card.hpp"
#include "cwxeditor_src/core/cwx/event.hpp"
#include "cwxeditor_src/core/cwx/expression.hpp"
#include "cwxeditor_src/core/cwx/flag.hpp"
#include "cwxeditor_src/core/cwx/jpy.hpp"
#include "cwxeditor_src/core/cwx/motion.hpp"
#include "cwxeditor_src/core/cwx/textholder.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/customtoolbar.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dockingfolder.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dutils.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/areawindow.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/areaview.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/scenehost.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/bgimagedialog.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/castcarddialog.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/effectcarddialog.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/eventdialog.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/eventwindow.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/eventview.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/image.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dskin.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/infocarddialog.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/history.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/partyhistory.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/loader.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/summarydialog.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/summaryskinbinding.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/smalldialogs.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/scripterrordialog.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/replacedialog.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/replace_error_provider.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/replace_grep_provider.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/replace_unused_provider.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/settingsdialog.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/spcarddialog.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/xmlbytestransfer.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/sound.hpp"

#if defined(_MSC_VER)
#pragma warning(disable : 4834)
#pragma warning(disable : 4100)
#endif

#include <algorithm>
#include <iterator>
#include <array>
#include <unordered_set>
#include <cassert>
#include <atomic>
#include <chrono>
#include <future>
#include <deque>
#include <mutex>
#include <thread>
#include <cmath>
#include <limits>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <numeric>
#include <span>
#include <regex>
#include <sstream>
#include <system_error>
#include <type_traits>
#include <utility>


namespace cwxeditor::cwx::editor::gui::dwt::mainwindow {

bool handleWarningState(const bool current, const std::uint32_t count,
                        const std::uint32_t warning,
                        const std::uint32_t lift) noexcept {
    if (warning == 0U || lift > warning) return current;
    if (!current && count > warning) return true;
    if (current && count <= lift) return false;
    return current;
}

namespace {

void showWarningDialog(const std::shared_ptr<lib::DialogBackend>& dialogs,
                       String title, String message) {
    if (!dialogs) return;
    lib::MessageDialogRequest request;
    request.title = std::move(title);
    request.message = std::move(message);
    request.severity = lib::MessageSeverity::Warning;
    request.buttons = lib::MessageButtons::OK;
    (void)dialogs->message(request);
}

// D版の editBeast と同様、内側の該当カードを正確に選び、残りのCWX接
// 尾辞をその子ダイアログへ渡す。 すべてのコンテントのホストでこの経路を共有する。
[[nodiscard]] bool openBeastCardDialog(
    cardwindow_ns::Commons& common, Summary* source,
    const cardwindow_ns::CWXPathPtr& owner,
    const cardwindow_ns::BeastCardPtr& card, const bool readOnly,
    const StringView nestedPath, const bool shellActivate,
    lib::Action applying, lib::Action applied, lib::Action closed) {
    const auto holder = source ? source->weak_from_this().lock() : SummaryPtr{};
    if (!holder || !card || !common.editCard) return false;
    common.editCard(cardwindow_ns::CardType::Beast, holder, owner, card, readOnly,
                    std::move(applying), std::move(applied), std::move(closed), false);
    if (nestedPath.empty()) return true;
    return common.openCardDialogPath &&
        common.openCardDialogPath(card, nestedPath, shellActivate);
}

// D版の sendToPipe と同様、同期された先入れ先出しの待ち行列に、すべての要求を保持する
// 。 ワーカーの状態を MainWindow から独立させ、未完了のタスクハンドルを保持する。 PortableTaskHandle
// を破棄すると、待機中の要求までキャンセルされてしまうため。
class SettingsReloadQueue final : public lib::TaskHandle {
    struct State {
        std::mutex mutex;
        std::deque<std::function<void()>> pending;
        bool running{};
        bool stopping{};
    };
    std::shared_ptr<State> state_{std::make_shared<State>()};
    std::mutex handles_mutex_;
    std::vector<std::shared_ptr<lib::TaskHandle>> handles_;
public:
    ~SettingsReloadQueue() override { cancel(); }
    void enqueue(const std::shared_ptr<lib::TaskBackend>& backend,
                 std::function<void()> notify) {
        const auto state = state_;
        {
            std::scoped_lock lock(state->mutex);
            if (state->stopping) return;
            state->pending.push_back(std::move(notify));
            if (state->running) return;
            state->running = true;
        }
        std::shared_ptr<lib::TaskHandle> handle;
        try {
            handle = backend->run([state] {
                for (;;) {
                    std::function<void()> job;
                    {
                        std::scoped_lock lock(state->mutex);
                        if (state->stopping || state->pending.empty()) {
                            state->running = false;
                            return;
                        }
                        job = std::move(state->pending.front());
                        state->pending.pop_front();
                    }
                    // 共通バックエンドと同様、バックグラウンドの失敗を個別に
                    // 扱う。 一回の一斉通知の失敗で、後続の要求を捨てない。
                    try { job(); } catch (...) {}
                }
            });
        } catch (...) {
            std::scoped_lock lock(state->mutex);
            state->running = false;
            throw;
        }
        // run() がその場で実行し、再入して要求を追加する場合がある。 どちらのミュー
        // テックスも run() をまたいで保持せず、以前の未完了ハンドルを上書きしない。
        std::scoped_lock lock(handles_mutex_);
        std::erase_if(handles_, [](const auto& old) { return old->finished(); });
        if (handle) {
            bool stopping;
            { std::scoped_lock state_lock(state->mutex); stopping = state->stopping; }
            if (stopping) handle->cancel();
            handles_.push_back(std::move(handle));
        }
    }
    void cancel() noexcept override {
        {
            std::scoped_lock lock(state_->mutex);
            state_->stopping = true;
            state_->pending.clear();
            state_->running = false;
        }
        std::scoped_lock lock(handles_mutex_);
        for (const auto& handle : handles_) handle->cancel();
    }
    bool finished() const noexcept override {
        std::scoped_lock lock(state_->mutex);
        return !state_->running && state_->pending.empty();
    }
    void wait() override {
        std::vector<std::shared_ptr<lib::TaskHandle>> handles;
        { std::scoped_lock lock(handles_mutex_); handles = handles_; }
        for (const auto& handle : handles) handle->wait();
    }
};

class SaveActivity final {
public:
    explicit SaveActivity(bool& active) : active_(&active) { *active_ = true; }
    ~SaveActivity() { release(); }
    void release() noexcept {
        if (!active_) return;
        *active_ = false;
        active_ = nullptr;
    }
    // 永続化は、開始した呼び出しの終了後も続く場合がある。 pollStoragePersistence
    // が同じ操作を完了するまで共有の文書ロックを維持し、 再
    // 読み込み・終了・二度目の保存で置き換わらないようにする。
    void keepActive() noexcept { active_ = nullptr; }
private:
    bool* active_{};
};

// ユーザーのシーン表示の配置を復元する。不正・旧式の配置は mountDefaultDock()
// が拒否し、 不完全なドッキングツリーを残す代わりに、D版の
// 標準の data/data_2/work 配置を再構築する。
constexpr bool kEnableSavedDockLayout = true;

// D版の Commons.show と同様、固定の data/data_2 コ
// ントロールも動的に開くパネルも、 主作業ペインを対になるペインとして使う。
[[nodiscard]] String defaultDockPairPane(
    const dockingfolder::DockingFolder& folder) {
    auto workPanes = folder.findPane("work", false);
    if (workPanes.empty()) workPanes = folder.findPane("work", true);
    return workPanes.empty() ? folder.firstKey() : workPanes.front();
}

[[nodiscard]] std::function<lib::Rect()> dockDisplayClientArea(
    std::shared_ptr<lib::GuiBackend> gui) {
    // WindowsのD版を基準にした方針を、すべてのバックエンドで共有する。 screens()
    // はネイティブの画面配置を提供し、別のUI方針は持たない。
    return [gui = std::move(gui)] {
        if (!gui) return lib::Rect{};
        const auto screens = gui->screens();
        if (screens.empty()) return lib::Rect{};
        if (screens.size() == 1U) {
            const auto& screen = screens.front();
            return screen.work_area.width > 0.0F &&
                           screen.work_area.height > 0.0F
                ? screen.work_area : screen.bounds;
        }
        // DWT Win32 の Display.getClientArea()
        // と同様、 複数モニターがある場合は仮想画面全体の境界を返す。
        const auto first = std::find_if(
            screens.begin(), screens.end(), [](const lib::ScreenInfo& screen) {
                return screen.bounds.width > 0.0F &&
                       screen.bounds.height > 0.0F;
            });
        if (first == screens.end()) return lib::Rect{};
        float left = first->bounds.x;
        float top = first->bounds.y;
        float right = first->bounds.x + first->bounds.width;
        float bottom = first->bounds.y + first->bounds.height;
        for (const auto& screen : screens) {
            if (screen.bounds.width <= 0.0F ||
                screen.bounds.height <= 0.0F)
                continue;
            left = std::min(left, screen.bounds.x);
            top = std::min(top, screen.bounds.y);
            right = std::max(right, screen.bounds.x + screen.bounds.width);
            bottom = std::max(bottom, screen.bounds.y + screen.bounds.height);
        }
        return lib::Rect{left, top, right - left, bottom - top};
    };
}

// エリア・戦闘エディタは、コールバックの Context への生ポインターを保持する
// 。 同じ寿命管理用の記録に両者をまとめ、必ずエディタ、Context の順で破棄す
// る。 二つの shared_ptr を単純なベクターに入れると、標準ライブラリに
// よっては vector::clear が先に Context を解放してしまう。
struct DynamicSceneLifetime final {
    ~DynamicSceneLifetime() {
        for (auto it = disconnect.rbegin(); it != disconnect.rend(); ++it) {
            if (*it) (*it)();
        }
        disconnect.clear();
        editor.reset();
        context.reset();
        menu_registry.reset();
    }

    std::vector<lib::Action> disconnect;
    std::shared_ptr<void> menu_registry;
    std::shared_ptr<void> context;
    std::shared_ptr<void> editor;
};

template <typename T>
const T& valueOf(const T& value) { return value; }

template <typename T, bool R, bool L>
const T& valueOf(const ::cwxeditor::cwx::settings::Prop<T, R, L>& value) {
    return value.value;
}

bool equalOpenHistory(const OpenHistory& left, const OpenHistory& right) {
    return left.path == right.path && left.skinType == right.skinType &&
           left.skinName == right.skinName && left.skinEngine == right.skinEngine;
}

bool equalOpenHistories(const std::vector<OpenHistory>& left,
                        const std::vector<OpenHistory>& right) {
    return left.size() == right.size() &&
           std::equal(left.begin(), left.end(), right.begin(), equalOpenHistory);
}

bool requiresMainToolBarSeparator(const MenuID left, const MenuID right) {
    return (left == MenuID::NewPackage && right == MenuID::NewFlagDir) ||
           left == MenuID::OpenImportSource || right == MenuID::OpenImportSource;
}

String persistentDockControlKey(const StringView runtimeKey) {
    String result(runtimeKey);
    // Commons は、取り込み元のペインを実行時に区別するため、現在の Summary
    // のアドレスを加える。 アドレスは再起動後には安定せず意味もないた
    // め、 リソースパスと、その後の重複識別用の接尾辞だけを保持する。
    for (const auto marker : {StringView("#source"), StringView("#owner")}) {
        if (const auto source = result.find(marker); source != String::npos) {
            const auto suffix = result.find('#', source + 1U);
            result.erase(source, suffix == String::npos ? String::npos : suffix - source);
        }
    }
    return result;
}

bool isCardGroupDockKey(const StringView key) {
    String normalized(key);
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](const unsigned char value) {
                       return static_cast<char>(std::tolower(value));
                   });
    const auto matches = [&normalized](const std::string_view base) {
        if (normalized == base) return true;
        if (normalized.size() <= base.size() ||
            normalized.compare(0, base.size(), base) != 0) return false;
        const char delimiter = normalized[base.size()];
        return delimiter == '#' || delimiter == ':' || delimiter == '/' ||
               delimiter == '-' || delimiter == '_';
    };
    return matches("cardgroup") || matches("cardgroupview");
}

bool isAllMode() {
    return lib::editor_window_host_options().all_mode;
}

// D版と同様、すべての単独版ホストでシナリオフォルダーを開く操作を提供する。 OS
// 固有のオープン処理は ShellBackend::open_path に隠す。
bool showScenarioFolderCommand() {
    return !isAllMode();
}

void activateHistoryWindow(std::shared_ptr<lib::GuiBackend> gui,
                           std::shared_ptr<lib::Dialog> dialog) {
    if (!gui || !dialog) return;
    // D版の Dialog.active() と同様、ウィンドウを前面に出す。キーボードフォーカ
    // スだけでは ネイティブウィンドウは前面に出ず、受動的な Window はフォーカス先にもで
    // きない。 共通の表示側の有効化処理を使い、既存または最初のコントロールへフォーカスを移す。
    gui->show(dialog);
    if (!dialog->visible()) return;
    if (const auto target = lib::focus_target_in_scope(
            dialog, gui->focused_widget(), true, true))
        gui->focus(target);
}

bool hiddenToolBarCommand(const MenuID id) {
    // ALL では外部ツールを省略する。単独版では、D版と同
    // 様に設定可能な外部ツールのドロップダウンを維持する。
    return (isAllMode() && id == MenuID::OuterTools) ||
           (!showScenarioFolderCommand() && id == MenuID::OpenDir);
}

bool containsDockWidget(const lib::WidgetPtr& root,
                        const lib::WidgetPtr& target) {
    if (!root || !target) return false;
    if (root == target) return true;
    const auto container = std::dynamic_pointer_cast<lib::Container>(root);
    if (!container) return false;
    for (const auto& child : container->children())
        if (containsDockWidget(child, target)) return true;
    return false;
}

struct WebAllUpperDataBranch final {
    std::shared_ptr<lib::SplitView> split;
    lib::WidgetPtr widget;
    std::size_t index{};
};

WebAllUpperDataBranch findWebAllUpperDataBranch(
    const lib::WidgetPtr& root, const lib::WidgetPtr& data_pane,
    const lib::WidgetPtr& cast_pane, const lib::WidgetPtr& work_pane) {
    if (!root || !containsDockWidget(root, data_pane) ||
        !containsDockWidget(root, cast_pane)) return {};

    if (const auto split = std::dynamic_pointer_cast<lib::SplitView>(root);
        split && split->orientation == lib::Orientation::Vertical) {
        const auto& children = split->children();
        for (std::size_t index = 0; index < children.size(); ++index) {
            const auto& child = children[index];
            if (containsDockWidget(child, data_pane) &&
                containsDockWidget(child, cast_pane) &&
                !containsDockWidget(child, work_pane))
                return {split, child, index};
        }
    }

    if (const auto container = std::dynamic_pointer_cast<lib::Container>(root)) {
        for (const auto& child : container->children()) {
            if (!containsDockWidget(child, data_pane) ||
                !containsDockWidget(child, cast_pane)) continue;
            auto nested = findWebAllUpperDataBranch(
                child, data_pane, cast_pane, work_pane);
            if (nested.split && nested.widget) return nested;
        }
    }
    return {};
}

structs::ToolBarSettings withRequiredMainToolBarSeparators(
    const structs::ToolBarSettings& source) {
    auto result = source.dup();
    for (auto& group : result.tools) {
        lib::Array<structs::Tool> normalized;
        normalized.reserve(group.size() + 2U);
        for (std::size_t index = 0; index < group.size(); ++index) {
            auto tool = group[index];
            // 旧式・独自の配置には、区切りフラグのない MenuID::None
            // が含まれる場合がある。 コマンドとして描くと、D版の細
            // い区切り線ではなく空のツールバーボタンになってしまう。
            if (tool.menu == MenuID::None) tool.separator = true;
            // 以前の既定配置には、これら二つのカード操作の
            // 間に区切りがあった。 現在の既定値だけでなく
            // 、保存済みの配置からも不要な空き枠を除く。
            if (tool.separator && index > 0U && index + 1U < group.size() &&
                !group[index - 1U].separator && !group[index + 1U].separator &&
                group[index - 1U].menu == MenuID::ShowCardDetail &&
                group[index + 1U].menu == MenuID::NewCast)
                continue;
            normalized.push_back(tool);
            if (index + 1U >= group.size() || tool.separator ||
                group[index + 1U].separator || group[index + 1U].menu == MenuID::None)
                continue;
            if (requiresMainToolBarSeparator(tool.menu,
                                             group[index + 1U].menu))
                normalized.push_back(structs::Tool::opCall());
        }
        group = std::move(normalized);
    }
    return result;
}

String pathString(StringView value) { return String(value); }

String lower(String value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::vector<String> splitCommandLine(StringView command) {
    std::vector<String> result;
    String token;
    bool quoted = false;
    for (std::size_t index = 0; index < command.size(); ++index) {
        const char character = command[index];
        if (character == '"') {
            quoted = !quoted;
            continue;
        }
        if (!quoted && std::isspace(static_cast<unsigned char>(character))) {
            if (!token.empty()) {
                result.push_back(std::move(token));
                token.clear();
            }
            continue;
        }
        if (character == '\\' && index + 1U < command.size() &&
            command[index + 1U] == '"') {
            token.push_back('"');
            ++index;
            continue;
        }
        token.push_back(character);
    }
    if (!token.empty()) result.push_back(std::move(token));
    return result;
}

using ::cwxeditor::cwx::structs::pathFromUtf8;
using ::cwxeditor::cwx::structs::pathToUtf8;

String basename(StringView value) {
    if (value.empty()) return {};
    return pathToUtf8(pathFromUtf8(value).filename());
}

String dirname(StringView value) {
    if (value.empty()) return {};
    return pathToUtf8(pathFromUtf8(value).parent_path());
}

void importPortableMenuItems(const std::vector<lib::MenuItem>& source,
                             const std::shared_ptr<dmenu::Menu>& destination,
                             const MenuID id) {
    if (!destination) return;
    for (const auto& source_item : source) {
        if (source_item.separator) {
            (void)destination->appendSeparator();
            continue;
        }
        int style = dmenu::MenuPush;
        if (source_item.radio) style |= dmenu::MenuRadio;
        else if (source_item.checked) style |= dmenu::MenuCheck;
        const auto item = destination->append(
            source_item.text, source_item.icon, style,
            dmenu::MenuData{id, {},
                [enabled = source_item.enabled] { return enabled; }}, {},
            source_item.action);
        if (!item) continue;
        item.native().shortcut = source_item.shortcut;
        item.native().action_with_gesture = source_item.action_with_gesture;
        item.native().open_children = source_item.open_children;
        item.setEnabled(source_item.enabled);
        item.setChecked(source_item.checked);
        if (!source_item.open_children && !source_item.children.empty()) {
            auto child = std::make_shared<dmenu::Menu>();
            importPortableMenuItems(source_item.children, child, id);
            destination->setSubmenu(item, std::move(child));
        }
    }
}

struct PortableIconRefreshBridge {
    std::weak_ptr<dmenu::Menu> destination;
    std::size_t first_native_item{};
};

void bindPortableIconRefresh(const std::shared_ptr<lib::Menu>& source,
                             const std::shared_ptr<dmenu::Menu>& destination,
                             const std::size_t first_native_item) {
    if (!source || !destination) return;
    const std::weak_ptr<dmenu::Menu> weak_destination = destination;
    destination->on_open = [source, weak_destination, first_native_item] {
        if (source->on_open) source->on_open();
        const auto current = weak_destination.lock();
        if (!current) return;
        for (std::size_t index = 0; index < source->items.size(); ++index) {
            const auto native_index = first_native_item + index;
            if (native_index >= current->size()) break;
            const auto icon = source->items[index].icon;
            if (current->items[native_index].icon != icon)
                dmenu::MenuItemRef(current.get(), native_index).setImage(icon);
        }
    };
}

constexpr std::optional<eventwindow::Command> eventCommandForMenu(const MenuID id) {
    using C = eventwindow::Command;
    switch (id) {
    case MenuID::EditScene: return C::EditScene;
    case MenuID::EditSceneDup: return C::EditSceneDuplicate;
    case MenuID::EditEventDup: return C::EditEventDuplicate;
    case MenuID::Undo: return C::Undo;
    case MenuID::Redo: return C::Redo;
    case MenuID::Up: return C::Up;
    case MenuID::Down: return C::Down;
    case MenuID::Comment: return C::Comment;
    case MenuID::ToScript: return C::ToScript;
    case MenuID::ToScript1Content: return C::ToScriptOne;
    case MenuID::ToScriptAll: return C::ToScriptAll;
    case MenuID::EventToPackage: return C::EventToPackage;
    case MenuID::StartToPackage: return C::StartToPackage;
    case MenuID::WrapTree: return C::WrapTree;
    case MenuID::SelectConnectedResource: return C::SelectConnectedResource;
    case MenuID::FindID: return C::FindId;
    case MenuID::EditProp: case MenuID::ShowProp: return C::EditProperty;
    case MenuID::Cut1Content: return C::CutOne;
    case MenuID::Copy1Content: return C::CopyOne;
    case MenuID::Delete1Content: return C::DeleteOne;
    case MenuID::PasteInsert: return C::PasteInsert;
    case MenuID::SwapToParent: return C::SwapToParent;
    case MenuID::SwapToChild: return C::SwapToChild;
    case MenuID::SelectCurrentEvent: return C::SelectCurrentEvent;
    default: return {};
    }
}

static_assert(eventCommandForMenu(MenuID::ToScript1Content) ==
              eventwindow::Command::ToScriptOne,
              "D's one-content script command must remain connected");

bool samePath(StringView left, StringView right,
              const lib::FileSystemBackend* filesystem) {
    if (left.empty() || right.empty()) return false;
    const auto left_path = pathFromUtf8(left);
    const auto right_path = pathFromUtf8(right);
    // 存在するネイティブパスでは、ジャンクション・シンボリックリンク・別表記も含め
    // 、実際のオブジェクトの同一性を優先する。 未存在のパスや提供側固有のパスは、
    // 共通のファイルシステムAPIに委ねる。 大文字小文字を区別するバックエンドに
    // は Quest と quest が共存できるため、ここでは文字種を揃えない。
    std::error_code ec;
    const bool equivalent = std::filesystem::equivalent(left_path, right_path, ec);
    if (!ec && equivalent) return true;
    ec.clear();
    if (filesystem) return filesystem->filenames_equal(left_path, right_path);
    return left_path.lexically_normal() == right_path.lexically_normal();
}

bool sameHistoryPath(const StringView left, const StringView right,
                     const lib::FileSystemBackend* filesystem) {
    return samePath(history::fullHistToHist(left), history::fullHistToHist(right),
                    filesystem);
}

bool sameParty(const ExecutionParty& left, const ExecutionParty& right) {
    return left.engineName == right.engineName && left.enginePath == right.enginePath &&
           left.isClassic == right.isClassic && left.yadoName == right.yadoName &&
           left.yadoPath == right.yadoPath && left.partyName == right.partyName &&
           left.partyPath == right.partyPath;
}

String timeName(const lib::Services& services) {
    static std::atomic<std::uint64_t> sequence{1};
    const auto now = std::chrono::system_clock::now();
    const auto timestamp = services.system
        ? services.system->format_local_time(now, "%Y%m%d%H%M%S")
        : std::nullopt;
    std::ostringstream out;
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count() % 1000;
    out << timestamp.value_or("00000000000000") << '-'
        << std::setw(3) << std::setfill('0') << milliseconds << '-'
        << sequence.fetch_add(1, std::memory_order_relaxed);
    return out.str();
}

String fingerprint(const std::filesystem::path& root) {
    std::error_code ec;
    if (std::filesystem::is_directory(root, ec) && !ec) {
        // D版の filesMD5 と同様、ルートとすべての子孫の更新日時をハッシ
        // ュ化する。 共通のリトルエンディアンのデータ列とMD5実装を再利用する。
        ::cwxeditor::cwx::binary::ByteIO timestamps;
        const auto append_modified = [&timestamps](const std::filesystem::path& path) {
            std::error_code error;
            const auto modified = std::filesystem::last_write_time(path, error);
            if (error) return false;
            timestamps.writeL(static_cast<std::int64_t>(
                modified.time_since_epoch().count()));
            return true;
        };
        if (!append_modified(root)) return {};

        std::vector<std::filesystem::path> entries;
        std::filesystem::recursive_directory_iterator iterator(root, ec), end;
        while (!ec && iterator != end) {
            entries.push_back(iterator->path());
            iterator.increment(ec);
        }
        if (ec) return {};
        // 走査順を固定し、ファイルシステムが同じディレクトリ構造を別
        // の順で列挙しても、 更新日時の列を再現できるようにする。
        std::sort(entries.begin(), entries.end(), [&root](const auto& left,
                                                          const auto& right) {
            return pathToUtf8(left.lexically_relative(root)) <
                   pathToUtf8(right.lexically_relative(root));
        });
        for (const auto& entry : entries) {
            if (!append_modified(entry)) return {};
        }
        return ::cwxeditor::cwx::utils::md5Digest(
            static_cast<const ::cwxeditor::cwx::binary::ByteIO&>(timestamps).bytes());
    }

    ec.clear();
    if (!std::filesystem::is_regular_file(root, ec) || ec) return {};
    std::uint64_t total = 0;
    std::uint64_t count = 0;
    std::uint64_t hash = 1469598103934665603ULL;
    const auto mix = [&hash](const auto value) {
        const auto* first = reinterpret_cast<const unsigned char*>(&value);
        for (std::size_t index = 0; index < sizeof(value); ++index) {
            hash ^= first[index];
            hash *= 1099511628211ULL;
        }
    };
    const auto mixPath = [&hash](const std::filesystem::path& value) {
        const auto text = pathToUtf8(value);
        for (const auto character : text) {
            hash ^= static_cast<unsigned char>(character);
            hash *= 1099511628211ULL;
        }
    };
    const auto size = std::filesystem::file_size(root, ec);
    if (ec) return {};
    const auto modified = std::filesystem::last_write_time(root, ec)
                              .time_since_epoch().count();
    if (ec) return {};
    total = size;
    ++count;
    mixPath(root.filename());
    mix(size);
    mix(modified);
    std::ostringstream out;
    out << count << ':' << total << ':' << std::hex << hash;
    return out.str();
}

// 日時による元の命名形式と、作成済みのC++版の一意な名前の両方を受け付ける。
// 接頭辞だけが似たものや一時作業ディレクトリは、バックアップの保持件数管理の対象にしない。
[[nodiscard]] bool ownedBackup(const std::filesystem::directory_entry& entry) {
    const auto name = pathToUtf8(entry.path().filename());
    static const std::regex directory_name(
        R"(^cwxeditor_backup_[0-9]{14}(-[0-9]{3}-[0-9]+)?\[.+\]$)");
    static const std::regex archive_name(
        R"(^cwxeditor_backup_[0-9]{14}(-[0-9]{3}-[0-9]+)?\[.+\]\.(zip|wsn)$)");
    std::error_code error;
    // 後処理でシンボリックリンクをたどり、無関係な対象へ入らない。
    if (entry.is_symlink(error) || error) return false;
    if (entry.is_directory(error) && !error) return std::regex_match(name, directory_name);
    return entry.is_regular_file(error) && !error && std::regex_match(name, archive_name);
}

// 初期化とバックアップの保持件数管理で、認識したバックアップの一つの整列済み一覧を共有する。
[[nodiscard]] std::vector<std::filesystem::directory_entry> ownedBackups(
    const std::filesystem::path& parent, std::error_code& error) {
    std::vector<std::filesystem::directory_entry> backups;
    std::filesystem::directory_iterator iterator(parent, error), end;
    if (error == std::errc::no_such_file_or_directory) {
        error.clear();
        return backups;
    }
    while (!error && iterator != end) {
        if (ownedBackup(*iterator)) backups.push_back(*iterator);
        iterator.increment(error);
    }
    if (error) return {};
    std::sort(backups.begin(), backups.end(), [](const auto& left, const auto& right) {
        return left.path().filename() < right.path().filename();
    });
    return backups;
}

[[nodiscard]] bool copyModificationTimes(const std::filesystem::path& source,
                                         const std::filesystem::path& destination,
                                         std::error_code& error) {
    error.clear();
    const auto copy_time = [&error](const std::filesystem::path& from,
                                    const std::filesystem::path& to) {
        const auto modified = std::filesystem::last_write_time(from, error);
        if (error) return false;
        std::filesystem::last_write_time(to, modified, error);
        return !error;
    };
    std::error_code status_error;
    if (!std::filesystem::is_directory(source, status_error)) {
        if (status_error) error = status_error;
        return !status_error && copy_time(source, destination);
    }
    if (status_error) { error = status_error; return false; }

    struct DirectoryTime {
        std::filesystem::path source;
        std::filesystem::path destination;
        std::size_t depth;
    };
    std::vector<DirectoryTime> directories{{source, destination, 0U}};
    std::filesystem::recursive_directory_iterator iterator(source, error), end;
    while (!error && iterator != end) {
        const auto entry = iterator->path();
        const auto relative = entry.lexically_relative(source);
        std::error_code entry_error;
        if (iterator->is_regular_file(entry_error)) {
            if (!copy_time(entry, destination / relative)) return false;
        } else if (!entry_error && iterator->is_directory(entry_error)) {
            directories.push_back({entry, destination / relative,
                static_cast<std::size_t>(std::distance(relative.begin(), relative.end()))});
        }
        if (entry_error) { error = entry_error; return false; }
        iterator.increment(error);
    }
    if (error) return false;
    std::sort(directories.begin(), directories.end(), [](const auto& left,
                                                         const auto& right) {
        return left.depth > right.depth;
    });
    for (const auto& directory : directories) {
        if (!copy_time(directory.source, directory.destination)) return false;
    }
    return true;
}

// D版の copyAll はファイルの更新日時を保持するが、ディレクトリは
// mkdir で作るだけで日時を戻さない。 C++版では日時のツリー全体を保
// 持し、再起動後も再帰的な更新日時のダイジェストが一致するようにする。 これ
// は初期化処理のために、D版の copyAll の制約を意図的に補うもの。
[[nodiscard]] bool copyScenario(const std::filesystem::path& source,
                                const std::filesystem::path& destination,
                                String* failure_reason = nullptr) {
    std::error_code ec;
    const auto failed = [&](const char* stage) {
        if (failure_reason) *failure_reason = String(stage) + ": " + ec.message() +
            " (" + std::to_string(ec.value()) + ")";
        return false;
    };
    auto staging = destination;
    staging += std::filesystem::path(".cwxeditor_temp");
    std::filesystem::remove_all(staging, ec);
    ec.clear();
    std::filesystem::create_directories(destination.parent_path(), ec);
    if (ec) return failed("create backup parent");
    if (std::filesystem::is_directory(source, ec)) {
        std::filesystem::create_directories(staging, ec);
        if (!ec) std::filesystem::copy(source, staging,
                              std::filesystem::copy_options::recursive |
                                  std::filesystem::copy_options::overwrite_existing,
                              ec);
    } else {
        std::filesystem::copy_file(source, staging,
                                    std::filesystem::copy_options::overwrite_existing,
                                    ec);
    }
    if (!ec) (void)copyModificationTimes(source, staging, ec);
    if (ec) {
        (void)failed("copy snapshot and modification times");
        std::error_code cleanup;
        std::filesystem::remove_all(staging, cleanup);
        return false;
    }
    // ファイルシステムの保存処理と、既存の対象を置換しないプラットフォームの公開処理を
    // 共有する。 Windows側の境界で、一時的に削除共有を許さないロックを扱う。
    (void)cardwirth::platform::rename_storage_path_no_replace(staging, destination, ec);
    if (ec) {
        (void)failed("publish staged snapshot");
        std::error_code cleanup;
        std::filesystem::remove_all(staging, cleanup);
        return false;
    }
    return true;
}

[[nodiscard]] bool forwardDuplicateScenarioStartup(
    const lib::Services& services, const LaunchOption& options,
    const bool eventPriority) {
    if (!services.ipc || options.scenario.empty()) return false;
    // ALL はエンジンのプロセス内にエディタを埋め込む。 D版の外部
    // インスタンスへの起動引き渡しは、単独エディタの起動に適用する。
    if (lib::editor_window_host_options().all_mode) return false;

    auto launch = options;
    ::cwxeditor::cwx::summary::decScenarioPath(
        launch.scenario, launch.openPaths, eventPriority);
    if (launch.scenario.empty()) return false;
    const auto input_path = structs::pathFromUtf8(launch.scenario);
    std::error_code path_error;
    if (!std::filesystem::exists(input_path, path_error) || path_error) return false;

    String scenario_path = utils::nabs(StringView{launch.scenario});
    const bool scenario_is_directory =
        std::filesystem::is_directory(input_path, path_error);
    if (path_error) return false;
    if (!scenario_is_directory) {
        const auto extension = utils::pathExtension(scenario_path);
        if (utils::cfnmatch(extension, ".xml") ||
            utils::cfnmatch(extension, ".wsm") ||
            utils::cfnmatch(extension, ".wid") ||
            utils::cfnmatch(extension, ".wex")) {
            const auto scenario_parent =
                structs::pathToUtf8(input_path.parent_path());
            scenario_path = utils::nabs(StringView{scenario_parent});
        }
    }

    std::string enumeration_error;
    const auto peers = services.ipc->enumerate_endpoints(
        "cwxeditor", {}, enumeration_error);
    for (const auto& endpoint : peers) {
        std::string request_error;
        const auto response = services.ipc->request(
            endpoint, "get opened scenario", std::chrono::milliseconds(250),
            request_error);
        constexpr std::string_view opened_prefix{"opened scenario "};
        if (!request_error.empty() || !response.starts_with(opened_prefix)) continue;
        const auto opened_path = utils::nabs(
            std::string_view(response).substr(opened_prefix.size()));
        if (!utils::cfnmatch(scenario_path, opened_path)) continue;

        String open_path_message{"open cwxpath "};
        for (std::size_t index = 0; index < launch.openPaths.size(); ++index) {
            if (index != 0U) open_path_message += path::CWXPATH_SEP;
            open_path_message += launch.openPaths[index];
        }
        request_error.clear();
        const auto open_reply = services.ipc->request(
            endpoint, open_path_message, std::chrono::milliseconds(250),
            request_error);
        if (request_error.empty() && open_reply.starts_with("opened cwxpath") &&
            !launch.selectfile.empty()) {
            request_error.clear();
            (void)services.ipc->request(
                endpoint, String("select file ") + launch.selectfile,
                std::chrono::milliseconds(250), request_error);
        }
        return true;
    }
    return false;
}

}  // namespace

MainWindow::MainWindow(String appPath, const system_ns::System& sys, Props& prop,
                       LaunchOption opt, lib::Services services,
                       MainWindowContext context)
    : commons::TopLevelPanel(), app_path_(std::move(appPath)), system_(&sys),
      prop_(&prop), prop_lifetime_(prop.lifetimeToken()),
      services_(std::move(services)), context_(std::move(context)),
       opt_(std::move(opt)),
      sync_(context_.runtimeContext ? context_.runtimeContext->storage_ptr() : nullptr),
       refresh_title_(this), drop_listener_(this) {
    if (!services_.graphics && !services_.dialogs && !services_.processes &&
        !services_.shell && !services_.ipc)
        services_ = prop.images().services();
    else if (!services_.audio)
        services_.audio = prop.images().services().audio;
    startup_forwarded_ = forwardDuplicateScenarioStartup(
        services_, opt_, valueOf(prop.var().clickIsOpenEvent));
    if (startup_forwarded_) {
        quit_ = true;
        session_state_persistence_suppressed_ = true;
        return;
    }
    lib::set_number_edit_wheel_enabled(
        prop.var().spinnerUpDownWithWheel.value);
    lib::set_tab_wheel_switch_enabled(prop.var().switchTabWheel.value);
    lib::set_editor_ime_mode(prop.var().imeMode.value);
    lib::set_combo_edit_popup_enabled(prop.var().comboListVisible.value);
    // D版と同様、MainWindow の起動時に保存済みの二つの全体音量を
    // 読み込む。 共有コントローラーと MaterialSelect の直接
    // プレビューに、同じ方針を適用する。 不正な負値は意図的にゼロとする。
    // D版の符号付きから符号なしへの変換では、最大音量になってしまうため。
    ::cwxeditor::cwx::editor::gui::sound::setServices(
        services_,
        static_cast<std::uint32_t>(
            std::clamp(prop.var().bgmVolume.value, 0, 100)),
        static_cast<std::uint32_t>(
            std::clamp(prop.var().seVolume.value, 0, 100)));

    images_ = std::make_unique<image_ns::Images>(app_path_, services_);
    // Web/Android の保存済みファイルのダイアログでは、フォルダー行に folder.png を使う。
    lib::set_stored_file_dialog_folder_icon(images_->folder());
    lib::editor_action_log("MainWindow construct stage=images");

    // D版と同様、Commons を構築する直前に FileSync を明示的に開始する。
    sync_.start();
    owned_comm_ = std::make_unique<commons::Commons>(*prop_, sync_);
    comm_ = owned_comm_.get();
    // D版と同様、作業ペインを配置する前に保存済みの壁紙を
    // 読み込む。 起動時と設定適用時の両方で、Commons
    // が共通の検索と画像キャッシュの通知を管理する。
    comm_->refreshWallpaper(*prop_);
    comm_->mainStatusPublisher(
        [this, alive = std::weak_ptr<std::atomic_bool>(lifecycle_alive_)](String status) {
            const auto current = alive.lock();
            if (current && current->load(std::memory_order_acquire))
                setStatusLine(std::move(status));
        });
    if (services_.gui) {
        const std::weak_ptr<std::atomic_bool> alive = lifecycle_alive_;
        const bool stop_windows_open =
            !lib::editor_window_host_options().all_mode &&
            services_.gui->platform() == lib::Platform::Windows;
        auto* const stop_comm = stop_windows_open ? comm_ : nullptr;
        unsubscribe_successful_native_event_dispatch_ =
            services_.gui->subscribe_successful_native_event_dispatch(
                [this, alive, stop_comm] {
                    const auto life = alive.lock();
                    if (!life || !life->load(std::memory_order_acquire)) return;
                    uiDispatchComplete();
                    if (stop_comm) stop_comm->stopOpen(false);
                });
        // D の独立バックアップタイマーと同様、入れ子のモーダル中も期限を確認する。
        // UI ディスパッチ完了通知とは分け、編集回数の境界は変えない。
        unsubscribe_modal_pump_iteration_ =
            services_.gui->subscribe_modal_pump_iteration([this, alive] {
                const auto life = alive.lock();
                if (life && life->load(std::memory_order_acquire)) backupThr();
            });
    }
    if (services_.dispatcher) {
        const std::weak_ptr<std::atomic_bool> alive = lifecycle_alive_;
        unsubscribe_successful_task_dispatch_ =
            services_.dispatcher->subscribe_successful_task_dispatch(
                [this, alive] {
                    const auto life = alive.lock();
                    if (life && life->load(std::memory_order_acquire))
                        uiDispatchComplete();
                });
    }
    window_ = services_.gui ? services_.gui->create_window()
                            : std::make_shared<lib::Window>();
    root_ = window_;
    commons::TopLevelPanel::shell(window_);
    commons::TopLevelPanel::title("CWXEditor_C");
    lib::editor_action_log("MainWindow construct stage=window");
    window_->icon = images_->app();
    auto& main_window = prop.flex().mainWin;
    const float main_width = static_cast<float>(std::max(320, main_window.width()));
    const float main_height = static_cast<float>(std::max(240, main_window.height()));
    const bool saved_x = main_window.x() != properties::DEFAULT;
    const bool saved_y = main_window.y() != properties::DEFAULT;
    int main_x = saved_x ? main_window.x() : 0;
    int main_y = saved_y ? main_window.y() : 0;
    const auto screens = services_.gui ? services_.gui->screens()
                                       : std::vector<lib::ScreenInfo>{};
    if (!screens.empty()) {
        const auto selected = [&]() -> const lib::ScreenInfo& {
            if (saved_x && saved_y) {
                const float center_x = static_cast<float>(main_x) + main_width / 2.0F;
                const float center_y = static_cast<float>(main_y) + main_height / 2.0F;
                if (const auto found = std::find_if(screens.begin(), screens.end(),
                        [center_x, center_y](const lib::ScreenInfo& screen) {
                            const auto& area = screen.work_area;
                            return center_x >= area.x && center_x < area.x + area.width &&
                                   center_y >= area.y && center_y < area.y + area.height;
                        }); found != screens.end()) return *found;
            }
            if (const auto primary = std::find_if(screens.begin(), screens.end(),
                    [](const lib::ScreenInfo& screen) { return screen.primary; });
                primary != screens.end()) return *primary;
            return screens.front();
        }();
        const auto& work = selected.work_area;
        if (!saved_x)
            main_x = static_cast<int>(std::lround(
                work.x + std::max(0.0F, work.width - main_width) / 2.0F));
        if (!saved_y)
            main_y = static_cast<int>(std::lround(
                work.y + std::max(0.0F, work.height - main_height) / 2.0F));
        dutils::intoDisplay(main_x, main_y, static_cast<int>(main_width),
                            static_cast<int>(main_height), work);
    } else {
        dutils::intoDisplay(main_x, main_y, static_cast<int>(main_width),
                            static_cast<int>(main_height));
    }
    // 実際のデスクトップバックエンドが、DPIを考慮した最終的な中央配置を行う。
    // 座標が保存されている場合は中央配置を無効にし、そのモニター上に復元する。
    window_->center_on_owner = !saved_x && !saved_y;
    window_->set_bounds({static_cast<float>(main_x), static_cast<float>(main_y),
                         main_width, main_height});
    window_->maximized = main_window.maximized();
    comm_->baseShell(window_);
    // D版と同様、主ツールバーは登録済みのペインのコントロールとは別に列挙する。 その
    // 都度取得することで、再構築済みの古い主ツールバーをペインの登録先に保持しない。
    comm_->mainToolBars([this, weak = std::weak_ptr<std::atomic_bool>(lifecycle_alive_)] {
        std::vector<commons::ToolBarPtr> bars;
        const auto life = weak.lock();
        if (!life || !life->load(std::memory_order_acquire)) return bars;
        for (const auto& bar : tool_bars_) bars.push_back(bar);
        return bars;
    });
    comm_->toolBarsRefreshed([this, weak = std::weak_ptr<std::atomic_bool>(lifecycle_alive_)] {
        const auto life = weak.lock();
        if (life && life->load(std::memory_order_acquire)) refreshToolBarHostState();
    });
    window_->on_close_requested = [this] { exitAll(); };
    if (services_.drag_drop) {
        lib::DropTargetBinding target;
        target.target = window_;
        target.accepted_formats = {"text/uri-list"};
        const auto weak = std::weak_ptr<std::atomic_bool>(lifecycle_alive_);
        const auto negotiate = [weak](lib::DragEvent& event) {
            const auto life = weak.lock();
            event.selected = life && life->load(std::memory_order_acquire) &&
                (static_cast<std::uint32_t>(event.allowed) &
                 static_cast<std::uint32_t>(lib::DropOperation::Link))
                ? lib::DropOperation::Link : lib::DropOperation::None;
        };
        target.on_enter = negotiate;
        target.on_over = negotiate;
        target.on_drop = [this, weak, negotiate](lib::DragEvent& event) {
            negotiate(event);
            if (event.selected != lib::DropOperation::Link || event.data.files.empty()) return;
            // UIの通知処理へ渡す前に、ネイティブのデータを複製する。 D
            // 版と同様、通常の保存確認を通して最初のファイルだけを開く。
            auto apply = [this, weak, paths = event.data.files]() {
                const auto life = weak.lock();
                if (!life || !life->load(std::memory_order_acquire))
                    return lib::DropOperation::None;
                std::vector<String> native_paths;
                native_paths.reserve(paths.size());
                for (const auto& path : paths) native_paths.push_back(pathToUtf8(path));
                drop_listener_.drop(native_paths);
                return lib::DropOperation::Link;
            };
            if (event.defer && event.defer(apply)) { event.handled = true; return; }
            event.selected = apply();
            event.handled = event.selected != lib::DropOperation::None;
        };
        services_.drag_drop->bind_target(std::move(target));
    }

    docking_folder_ = std::make_shared<dockingfolder::DockingFolder>(
        window_, 0, String{"work"});
    lib::editor_action_log("MainWindow construct stage=docking-created");
    configureDockingFolder();
    lib::editor_action_log("MainWindow construct stage=docking-configured");
    comm_->panePresenter(
        [this](const StringView key,
               const std::shared_ptr<commons::TopLevelPanel>& panel,
               const StringView paneFamily, const bool select) {
            presentDockPanel(key, panel, paneFamily, select);
        });
    const std::weak_ptr<std::atomic_bool> path_route_life = lifecycle_alive_;
    comm_->pathRouter([this, path_route_life](String path, const bool activate)
                          -> std::optional<bool> {
        const auto alive = path_route_life.lock();
        if (!alive || !alive->load(std::memory_order_acquire)) return false;
        return routeCWXPath(std::move(path), activate);
    });
    initializeSignals();
    lib::editor_action_log("MainWindow construct stage=signals");

    skin_ = dskin::findSkin2(*prop_, valueOf(prop_->var().defaultSkin),
                             valueOf(prop_->var().defaultSkinName));
    if (skin_) comm_->skin(skin_);
    lib::editor_action_log("MainWindow construct stage=skin");
    pipe_name_ = createPipeName();
    if (!prop_->configFilePath().empty()) {
        dock_state_file_ = prop_->configFilePath() + ".dock";
    } else if (prop_->storage()) {
        dock_state_file_ = lib::editor_window_host_options().all_mode ||
                                   lib::is_managed_user_data_session(services_)
            ? String("editor/Config/cwxeditor.dock")
            : String("Config/cwxeditor.dock");
    } else {
        dock_state_file_ = app_path_.empty() ? String("cwxeditor.dock")
                                             : app_path_ + ".dock";
    }
    loadDockState();
    lib::editor_action_log("MainWindow construct stage=dock-loaded");
    initializeTypedPanes();
    lib::editor_action_log("MainWindow construct stage=typed-panes");

    no_summary_menu_ = {
        MenuID::New, MenuID::Open, MenuID::NewAtNewWindow,
        MenuID::OpenAtNewWindow, MenuID::Close, MenuID::Find,
        MenuID::ToXMLText, MenuID::TableView, MenuID::VarView,
        MenuID::EditScenarioHistory, // D版と同様、シナリオがなくても保存済みの履歴を表示する。
        MenuID::CastView, MenuID::SkillView, MenuID::ItemView,
        MenuID::BeastView, MenuID::InfoView, MenuID::FileView,
        MenuID::CouponView, MenuID::GossipView, MenuID::CompleteStampView,
        MenuID::KeyCodeView, MenuID::CellNameView, MenuID::CardGroupView,
        MenuID::ChangeVH, MenuID::ShowCardProp, MenuID::ShowCardImage,
        MenuID::ShowCardDetail, MenuID::ExecEngine, MenuID::OuterTools, MenuID::Settings,
        MenuID::VersionInfo, MenuID::CustomizeToolBar, MenuID::OpenBackupDir,
        // D版と同様、Summary がなくても、整理処理の可否はパーティの履歴で判定する。
        MenuID::DeleteNotExistsParties,
    };
    main_menu_ = {MenuID::File, MenuID::Edit, MenuID::View, MenuID::Tool,
                  MenuID::Table, MenuID::Variable, MenuID::Help, MenuID::Card};
    mountDefaultDock();
    if (docking_folder_)
        for (const auto& pane_key : docking_folder_->paneKeys())
            configureWallpaperForPane(pane_key); // フックの設定前に復元されたペインも対象にする。
    lib::editor_action_log("MainWindow construct stage=dock-mounted");
    rebuildMenus();
    lib::editor_action_log("MainWindow construct stage=menus");
    if (services_.gui) {
        const std::weak_ptr<std::atomic_bool> alive = lifecycle_alive_;
        services_.gui->set_application_key_filter(
            [this, alive](const lib::Key key, const lib::Modifiers modifiers) {
                const auto lifetime = alive.lock();
                if (!lifetime || !lifetime->load(std::memory_order_acquire))
                    return false;
                lib::KeyEvent event{key, modifiers};
                KeyDownFilter(this).handleEvent(event);
                return event.handled;
            });
    }
    {
        // D版と同様、DockingFolder.FocusL を表示全体の SWT.FocusIn/MouseDown
        // に登録する。 ドッキングされたペイン内の任意のコントロールにフォー
        // カスが移ると、そのペインを選択し、 編集メニューの取り消し・やり直し
        // やツールバーが、実際に作業したビューを対象にする。 ペインの外のツー
        // ルバーや ALL の取り消し・やり直しを押しても、対象は維持する。
        const std::weak_ptr<std::atomic_bool> alive = lifecycle_alive_;
        focus_observer_id_ = lib::add_focus_observer(
            [this, alive](const lib::WidgetPtr& focused) {
                const auto lifetime = alive.lock();
                if (!lifetime || !lifetime->load(std::memory_order_acquire))
                    return;
                trackFocusForMenus(focused);
            });
    }
    {
        // D版と同様、各ダイアログの編集可能な Text/Combo に
        // 、独自の取り消し履歴を持つ createTextMenu を付
        // ける。 表示するダイアログで未設定の入力欄に標準テキストメニュ
        // ーを与え、 Ctrl+Z/Ctrl+Y や ALL の見出しの
        // 取り消し・やり直しで、カード名などの編集を戻せるようにする。
        // すでに独自のテキストメニューがある場合は、そのまま維持する。
        const std::weak_ptr<std::atomic_bool> alive = lifecycle_alive_;
        lib::set_window_shown_hook(
            [this, alive](const std::shared_ptr<lib::Window>& window) {
                const auto lifetime = alive.lock();
                if (!lifetime || !lifetime->load(std::memory_order_acquire))
                    return;
                if (!std::dynamic_pointer_cast<lib::Dialog>(window)) return;
                dmenu::Commons comm;
                comm.services = services_;
                if (prop_) {
                    comm.undoMaxEtc = static_cast<std::size_t>(
                        std::max<std::int32_t>(0, prop_->var().undoMaxEtc));
                    comm.presentation.textTabs = static_cast<std::size_t>(
                        std::max<std::int32_t>(0, prop_->var().textTabs.value));
                }
                comm.undoLimit = [this, alive] {
                    const auto life = alive.lock();
                    if (!life || !life->load(std::memory_order_acquire) || !prop_) return std::size_t{};
                    return static_cast<std::size_t>(std::max<std::int32_t>(0, prop_->var().undoMaxEtc.value));
                };
                const auto commonsLife = comm_->lifetimeToken();
                comm.subscribeUndoMax = [commons = comm_, commonsLife](lib::Action handler) {
                    if (!commons || commonsLife.expired()) return lib::Action{};
                    const auto token = commons->refUndoMax.add(std::move(handler));
                    // 保持された共通の Text は Main/Commons の所有者より長
                    // く存続する場合がある。 SWTではウィンドウと同時に破棄されるが、ここで
                    // は後のRAIIによる接続解除で 借用したホストを参照しないようにする。
                    return lib::Action([commons, commonsLife, token] {
                        if (!commonsLife.expired()) commons->refUndoMax.remove(token);
                    });
                };
                comm.presentation.menuText = [this, alive](MenuID id) {
                    const auto life = alive.lock();
                    return life && life->load(std::memory_order_acquire) ? menuText(id) : String{};
                };
                comm.presentation.menuImage = [this, alive](MenuID id) {
                    const auto life = alive.lock();
                    return life && life->load(std::memory_order_acquire) ? menuImage(id) : lib::ResourceId{};
                };
                dmenu::installMissingTextMenus(window, comm);
            });
    }
    refreshExecEngine();
    refreshOuterTools();
    refreshTitle();
}

std::shared_ptr<undo::UndoManager> MainWindow::sharedSceneHistory(
    const std::shared_ptr<Summary>& summary, const std::shared_ptr<void>& owner) {
    std::erase_if(scene_histories_, [](const SceneHistoryEntry& entry) {
        return entry.summary.expired() || entry.owner.expired() || entry.manager.expired();
    });
    for (const auto& entry : scene_histories_) {
        // シーンのペインは Area/Battle のポインターを持ち、EventWindow
        // は仮想基底 CWXPath を通じて同じモデルを解決する。 共有所有権を比較し、異な
        // るポインターで同じ対象を指すビューが、D版互換の一つの履歴位置を共有するようにする。
        const bool same_owner = !entry.owner.owner_before(owner) &&
                                !owner.owner_before(entry.owner);
        if (entry.summary.lock() == summary && same_owner)
            if (auto manager = entry.manager.lock()) return manager;
    }
    const auto configured = prop_ ? prop_->var().undoMaxEvent.value : 0;
    auto manager = std::make_shared<undo::UndoManager>(
        configured > 0 ? static_cast<std::size_t>(configured) : 0U);
    scene_histories_.push_back({summary, owner, manager});
    return manager;
}

void MainWindow::retireEventHost(const std::shared_ptr<eventwindow::EventWindow>& current,
                                  const bool discardDock) {
    if (!current || !comm_) return;
    // カードの種類に応じた無効化と、通常のドッキング終了を、同じ終了確定
    // の境界で扱う。 同期コールバックの間はコントローラーを保持する。
    current->closeActiveDialog();
    std::vector<String> retired_keys;
    for (auto it = event_windows_.begin(); it != event_windows_.end();) {
        if (it->second.lock() == current) {
            retired_keys.push_back(it->first);
            it = event_windows_.erase(it);
        } else ++it;
    }
    for (const auto& retired_key : retired_keys) {
        all_view_toolbar_factories_.erase(retired_key);
        // ユーザー操作による DockingFolder の終了では、許可フックが戻った
        // 後で自身のコントロールを外す。 所有者の削除には、その外側の終了処理がない。
        if (discardDock && docking_folder_) (void)docking_folder_->discard(retired_key);
        dock_.close(retired_key);
    }
    comm_->close(current->shell());
    std::erase_if(scenario_panes_, [&current](const auto& candidate) {
        return candidate.get() == current.get();
    });
    refreshAllCompactSelectors();
    comm_->refreshToolBar();
}

void MainWindow::configureDockingFolder() {
    if (!docking_folder_) return;
    docking_folder_->selectEvent.clear();
    docking_folder_->displayClientArea =
        dockDisplayClientArea(services_.gui);
    docking_folder_->closeCtrlEvent.clear();
    docking_folder_->movingShellEvent.clear();
    docking_folder_->moveShellEvent.clear();
    docking_folder_->selectEvent.push_back([this](StringView key) {
        dock_.select(String(key));
        refreshSelectionChrome(key);
    });
    docking_folder_->selectFocusEvent = [this,
        weak = std::weak_ptr<std::atomic_bool>(lifecycle_alive_),
        gui = services_.gui](const WidgetPtr& widget) {
        const auto alive = weak.lock();
        if (!alive || !alive->load(std::memory_order_acquire) || !widget) return;
        const String selected_key = dock_.selected;
        WidgetPtr target = widget;
        const auto selected_control = docking_folder_
            ? docking_folder_->control(selected_key) : WidgetPtr{};
        // D版の Commons.show と同様、ペインのウィンドウより TopLevelPanel.focusControl を優先する。
        if (selected_control) {
            if (const auto panel = commons::getTopLevelPanel(widget)) {
                const auto panel_focus = panel->focusControl();
                if (panel_focus) {
                    if (const auto panel_target = lib::focus_target_in_scope(
                            selected_control, panel_focus, false, true))
                        target = panel_target;
                }
            }
        }
        if (gui && docking_folder_) {
            // D版の forceFocus と同様、ペインのコントロールにフォー
            // カスを移す前に、そのウィンドウを有効化する。 切り離されたペイン
            // は独自の共通 Window モデルを持つため、 ここにOS別の方
            // 針を追加せず、GuiBackend を通じてその画面を復元する。
            for (const auto& shell : docking_folder_->subWindows()) {
                if (!shell || !lib::contains_widget(shell, target.get())) continue;
                gui->restore_window(shell);
                if (!alive->load(std::memory_order_acquire)) return;
                break;
            }
        }
        if (gui) gui->focus(target);
        // フォーカス処理が同期的に選択を変更したり、ホストを無効化したりする場合がある
        // 。 コールバック後も選択されている EventView だけを取得し直す。
        const auto current = weak.lock();
        if (current && current->load(std::memory_order_acquire))
            refreshSelectedEventContentsToolBox(selected_key);
    };
    docking_folder_->showContextMenu =
        [this](const std::shared_ptr<lib::Menu>& menu,
               const WidgetPtr& owner, const lib::Point position) {
            if (services_.gui && menu && owner)
                services_.gui->show_context_menu(menu, owner, position);
        };
    docking_folder_->closeCtrlEvent.push_back([this](StringView key) {
        if (const auto found = dynamic_card_windows_.find(String(key));
            found != dynamic_card_windows_.end()) {
            if (const auto editor = found->second.lock()) editor->onHostClose();
        }
        if (const auto found = event_windows_.find(String(key)); found != event_windows_.end())
            if (const auto editor = found->second.lock()) retireEventHost(editor, false);
        // SWT と同様、シーンタブの終了が確定したら、ホストのラッパーが
        // 残っていてもエディタを破棄する。 Commons の登録を外し、
        // 無効になった元のビューなしで共有履歴を再適用できるようにする。
        if (comm_ && docking_folder_) {
            const auto data = commons::tlpData(docking_folder_->control(key));
            if (data && data->tlp) {
                const auto kind = data->tlp->viewProjection().kind;
                if (kind == commons::ViewProjection::Kind::Area ||
                    kind == commons::ViewProjection::Kind::Battle)
                    comm_->close(data->tlp->shell());
            }
        }
        dock_.close(key);
        return true;
    });
    docking_folder_->canMove = [this](StringView control, StringView pane) {
        return dockCanMove(control, pane);
    };
    docking_folder_->canVanish = [this](const dockingfolder::DockingFolder&,
                                        StringView key) {
        return dockCanVanish(key);
    };
    docking_folder_->hasCloseButton = [this](StringView key) {
        // 単独版はD版のドッキングタブに合わせ、タブ内の閉じる印を付けない。
        // コンテキストメニューと、中クリックで閉じる設定は引き続き利用できる。
        if (!lib::editor_window_host_options().all_mode) return false;
        return (prop_ && valueOf(prop_->var().showCloseButtonAllTab)) ||
               key.starts_with("work") || key.starts_with("side") ||
               key.starts_with("area.scene:") || key.starts_with("area.event:") ||
               key.starts_with("battle.scene:") || key.starts_with("battle.event:") ||
               key.starts_with("package:") || key.starts_with("use:");
    };
    docking_folder_->closeTabWithMiddleClick = [this](StringView) {
        return !prop_ || valueOf(prop_->var().closeTabWithMiddleClick);
    };
    docking_folder_->newPaneName = [this](StringView control, StringView pane,
                                           dockingfolder::Dir direction) {
        const auto translated = direction == dockingfolder::Dir::E ? DockDirection::east :
                                direction == dockingfolder::Dir::W ? DockDirection::west :
                                direction == dockingfolder::Dir::S ? DockDirection::south :
                                                                     DockDirection::north;
        return dockNewPaneName(control, pane, translated);
    };
    // D版の initDock と同様、システム用ペイン・コントロールが消えるときに配置を記憶する。
    docking_folder_->memoryPane = [](StringView key) {
        return key.starts_with("data") || key.starts_with("side");
    };
    docking_folder_->memoryControl =
        [weak = std::weak_ptr<dockingfolder::DockingFolder>(docking_folder_)](StringView key) {
            if (key.starts_with("data") || key.starts_with("flag") ||
                key.starts_with("card") || key.starts_with("castCard") ||
                key.starts_with("skillCard") || key.starts_with("itemCard") ||
                key.starts_with("beastCard") || key.starts_with("infoCard") ||
                key.starts_with("file")) return true;
            const auto folder = weak.lock();
            return key.starts_with("side") && folder &&
                   folder->findCtrl("side", true).size() == 1U;
        };
    docking_folder_->firstResize = [this](StringView key) {
        return dockFirstResize(key);
    };
    docking_folder_->movingShellEvent.push_back([this](StringView key) {
        return dockMovingShellEvent(String(key));
    });
    docking_folder_->moveShellEvent.push_back(
        [this](WindowPtr before, StringView key) {
            dockMoveShellEvent(std::move(before), String(key));
        });
    docking_folder_->addCreatePaneEvent(
        [this](const ContainerPtr& parent, StringView key) {
            createPaneEvent(parent, String(key));
        });
    // fromNode() は、この方針を設定する前にページを復元する。 復元済
    // みのページにも、単独版の閉じる印なしの規則を適用し直す。 新規に配置するペ
    // ージは addImpl() で hasCloseButton を参照する。
    if (!lib::editor_window_host_options().all_mode)
        docking_folder_->updateCloseButtons();
}

MainWindow::MainWindow(Props& prop, LaunchOption opt, lib::Services services,
                       MainWindowContext context)
    : MainWindow(prop.parent().appPath(), prop.sys(), prop, std::move(opt),
                 std::move(services), std::move(context)) {}

MainWindow::~MainWindow() {
    if (prop_lifetime_.expired()) prop_ = nullptr;
    if (startup_forwarded_) {
        if (lifecycle_alive_) lifecycle_alive_->store(false, std::memory_order_release);
        return;
    }
    // D版と同様、ペインの破棄前にドッキング履歴を保存する。提供側が有
    // 効なうちにスナップショットを取得し、 その後は下の既存のキュー・
    // ワーカーの終了順序を維持する。 失敗・再入による取得中断は、有効
    // な空のタブ一覧とは区別し、最後の完全な履歴を空で上書きしない。
    std::optional<String> shutdownHistory;
    const auto historyLife = std::weak_ptr<std::atomic_bool>(lifecycle_alive_);
    try {
        const auto historySummary = summary_;
        auto* const historyCommons = comm_;
        const auto stillCurrent = [&] {
            const auto alive = historyLife.lock();
            return alive && alive->load(std::memory_order_acquire) &&
                summary_ == historySummary && comm_ == historyCommons &&
                historyCommons && historyCommons->summary() == historySummary;
        };
        if (!session_state_persistence_suppressed_ && !main_window_state_saved_ &&
            historySummary && stillCurrent()) {
            // isChanged() は Summary のフラグだけでなく外部
            // の提供処理も呼ぶため、 問い合わせごとに寿命と接続先を検証する。
            const bool dirtyBefore = historyCommons->isChanged();
            if (!dirtyBefore && stillCurrent()) {
                const auto snapshot = createFullHistString();
                if (stillCurrent()) {
                    const bool dirtyAfter = historyCommons->isChanged();
                    if (!dirtyAfter && stillCurrent()) shutdownHistory = snapshot;
                }
            }
        }
    } catch (...) {
        // 最後の完全な履歴を保持する。他の設定は引き続き保存できる。
    }
    // UIや Summary の状態を変える前に、待機中の受信コールバックを無効化し
    // てワーカーの終了を待つ。 保留中の問い合わせはこのフラグを見て待機をやめる。
    if (lifecycle_alive_) lifecycle_alive_->store(false, std::memory_order_release);
    if (unsubscribe_successful_native_event_dispatch_) {
        auto unsubscribe = std::move(unsubscribe_successful_native_event_dispatch_);
        unsubscribe_successful_native_event_dispatch_ = {};
        unsubscribe();
    }
    if (unsubscribe_modal_pump_iteration_) {
        auto unsubscribe = std::move(unsubscribe_modal_pump_iteration_);
        unsubscribe_modal_pump_iteration_ = {};
        unsubscribe();
    }
    if (unsubscribe_successful_task_dispatch_) {
        auto unsubscribe = std::move(unsubscribe_successful_task_dispatch_);
        unsubscribe_successful_task_dispatch_ = {};
        unsubscribe();
    }
    if (directory_window_) directory_window_->retireHost();
    if (services_.drag_drop && window_) services_.drag_drop->unbind(window_);
    if (ipc_server_) ipc_server_->stop();
    if (comm_) comm_->summary({});
    // D版の DListener は widgetDisposed で最終の境界を
    // 保存する。 共通のイベントループは exitAll() を通らず終了する場
    // 合もあるため、 終了要求の処理だけで保存すると、その経路で位置が失われる。
    const bool shutdown_state_saved =
        saveSessionStateForShutdownImpl(&shutdownHistory);
    quit_ = true;
    send_pipe_quit_ = true;
    retireOwnedMenuBindings();
    cancelScenarioLoadTimeout();
    if (focus_observer_id_) lib::remove_focus_observer(focus_observer_id_);
    lib::set_window_shown_hook({});
    lib::set_stored_file_dialog_folder_icon(0U);
    for (const auto& load : background_loads_) load->cancel();
    // 異常終了・強制終了時の代替処理。通常の終了では、借用データを使うすべてのワ
    // ーカーが終わるまで イベント処理を続け、その後でこのデストラクターに入る。
    for (const auto& load : background_loads_) (void)load->cancelAndWait();
    background_loads_.clear();
    scenario_load_control_.reset();
    dismissPreviousScenarioLoading();
    if (create_scenario_dialog_) {
        create_scenario_dialog_->forceCancel();
        create_scenario_dialog_.reset();
    }
    if (overwrite_save_dialog_ && services_.gui) services_.gui->close(overwrite_save_dialog_);
    overwrite_save_dialog_.reset();
    overwrite_save_message_.reset();
    overwrite_save_close_.reset();
    for (auto it = name_signal_cleanup_.rbegin();
         it != name_signal_cleanup_.rend(); ++it) {
        if (*it) (*it)();
    }
    name_signal_cleanup_.clear();
    // ドッキングしたコントロールや主 HWND を破棄する前に、ネイティブの子
    // ダイアログを閉じる。 そうしないと、親の終了処理中もモーダルの EventDialog
    // が UIの通知処理を入れ子のループ内に留めてしまう。
    for (auto& [key, weak] : event_windows_) {
        (void)key;
        if (const auto editor = weak.lock()) editor->closeActiveDialog();
    }
    if (scenario_history_dialog_) {
        scenario_history_dialog_->close();
        scenario_history_dialog_.reset();
    }
    if (import_history_dialog_) {
        import_history_dialog_->close();
        import_history_dialog_.reset();
    }
    if (executed_party_history_dialog_) {
        executed_party_history_dialog_->close();
        executed_party_history_dialog_.reset();
    }
    if (actual_replace_dialog_) {
        actual_replace_dialog_->close();
        actual_replace_dialog_.reset();
    }
    if (summary_dialog_) {
        summary_dialog_->forceCancel();
        summary_dialog_.reset();
    }
    if (toolbar_custom_dialog_) {
        toolbar_custom_dialog_->cancel();
        toolbar_custom_dialog_->close();
        toolbar_custom_dialog_.reset();
    }
    if (settings_dialog_) {
        settings_dialog_->forceCancel();
        settings_dialog_.reset();
    }
    if (debug_dialog_window_ && services_.gui)
        services_.gui->close(debug_dialog_window_);
    debug_dialog_window_.reset();
    if (ui_dispatch_error_dialog_) {
        const auto error_dialog = ui_dispatch_error_dialog_;
        if (services_.gui) services_.gui->close(error_dialog->dialog());
    }
    ui_dispatch_error_dialog_.reset();
    // 最終のドッキング情報の保存は、上の共通の終了スナップショットが担当する
    // 。 ホストが永続化を確認した後に、別の書き込みを追加しない。 音声のフ
    // ァサードはプロセス内で静的に存続するが、サービスへの参照はこのエディタ
    // のセッションから借用している。 ホストがGUI・音声の構成を破棄する
    // 前に、このセッションの接続だけを外し、新しいセッションには触れない。
    ::cwxeditor::cwx::editor::gui::sound::clearServices(services_);

    // DockingFolder は配置した全ページを共有所有する。
    // 個別パスのエリア・戦闘・イベントエディタの コールバックは MainWindow
    // を参照するため、そのエディタとコントローラーが有効なうち
    // にドッキングツリーを解体する。 メンバーの破棄順に任せると
    // 、dynamic_panes_ が docking_folder_
    // より後に宣言されているため、 コールバックの所有者
    // を先に破棄し、その後でコントロールを解放してしまう。
    if (docking_folder_) {
        if (window_ && docking_folder_->component())
            window_->remove(docking_folder_->component());
        docking_folder_.reset();
    }
    // 再入したパッケージのコールバックが、ホストの終了後まで共有の AreaTable を保持する
    // 場合がある。 AreaTable は MainWindow の area_table_context_
    // を借用するため、その有効なうちに接続を外す。 後で共有所有者が解放したときは、古い
    // Context ではなく、多重実行を防ぐ disposed_ の検査に入るようにする。
    if (area_table_) area_table_->dispose();
    if (comm_) {
        comm_->panePresenter({});
        comm_->pathRouter({});
        comm_->closeAll();
    }
    event_windows_.clear();
    all_view_toolbar_factories_.clear();
    all_view_toolbar_active_.reset();
    dynamic_card_windows_.clear();
    scenario_panes_.clear();
    debug_scene_create_card_.clear();
    scene_open_event_.clear();
    scene_scale_refresh_.clear();
    debug_area_scene_windows_.clear();
    debug_battle_scene_windows_.clear();
    dynamic_panes_.clear();
    // エリア・戦闘ビューは、操作の登録先への生ポインターを保持す
    // る。 登録先の所有者を解放する前に、上でビューを解放する。
    scene_menu_registries_.clear();
    scene_view_registries_.clear();
    scene_histories_.clear();
    // D版と同様、保存処理の完了を待ってから展開先のツリーを削除す
    // る。 共通の FileSync の処理を先に完了させ、待機
    // 中の書き込みが削除済みディレクトリへ到達しないようにする。
    bool storage_sync_succeeded = true;
    try {
        sync_.sync();
    } catch (const std::exception& error) {
        storage_sync_succeeded = false;
        utils::debugln("shutdown storage sync: ", error.what());
    } catch (...) {
        storage_sync_succeeded = false;
        utils::debugln("shutdown storage sync failed");
    }
    // 文書に結び付いたすべてのビューは、上でモデルを解放済み。
    // 共有の寿命管理は、新規読み込みが拒否された場合の代替処理で
    // あり、 通常の終了ではD版互換の明示的な後処理を維持する。
    if (summary_ && summary_->useTemp()) {
        try {
            summary_->delTemp();
        } catch (const std::exception& error) {
            utils::debugln("shutdown temporary scenario cleanup: ", error.what());
        } catch (...) {
            utils::debugln("shutdown temporary scenario cleanup failed");
        }
    }
    // D版と同様、最終のウィンドウ破棄時に FlexProps のファイル未指定用の作業領域を削
    // 除する。 セッション保存や保留中の書き込みが失敗した場合は、復旧データを残すため保持する
    // 。 標準・ALL の設定保存先では FlexProps::cleanup は何もしない。
    if (shutdown_state_saved && !session_state_persistence_suppressed_ &&
        storage_sync_succeeded) {
        if (const auto prop_lifetime = prop_lifetime_.lock(); prop_lifetime && prop_)
            prop_->flex().cleanup();
    }
    sync_.quit();
}

void MainWindow::initializeSignals() {
    comm_->save.add([this](WindowPtr shell) { savec(std::move(shell)); });
    comm_->refScenarioName.add([this] {
        refreshTitle();
        // Summary の名前だけを編集した場合も、データの表とツリーの両方の見出しを更新する。
        if (area_table_) area_table_->refresh();
        card_comm_.refScenarioName.call();
    });
    comm_->refScenarioPath.add([this] {
        refreshTitle();
        card_comm_.refScenarioPath.call();
    });
    comm_->refTableViewStyle.add([this, weak = std::weak_ptr<std::atomic_bool>(lifecycle_alive_)] {
        const auto alive = weak.lock();
        if (!alive || !alive->load(std::memory_order_acquire) || !prop_ ||
            lib::editor_window_host_options().all_mode) return;
        // D版の DataWindow.refTableViewStyle と同様、既存の
        // エリアペインを再構成する。 保持中のC++版ホストの公開スタイル設定関数を使う。
        const bool show_tree = prop_->var().showAreaDirTree.value;
        data_prop_.showAreaDirectoryTree = show_tree;
        if (area_table_) area_table_->setDirectoryMode(show_tree);
    });
    // D版の AreaTable と同様、置換文字列と Summary 行の設定を購読する
    // 。 同じ Summary の再設定は何もしないため、既存の表を保持したまま更新する。
    comm_->replText.add([this, weak = std::weak_ptr<std::atomic_bool>(lifecycle_alive_)] {
        const auto alive = weak.lock();
        if (!alive || !alive->load(std::memory_order_acquire)) return;
        const auto table = area_table_;
        if (!table) return;
        table->replText();
    });
    comm_->refAreaTable.add([this, weak = std::weak_ptr<std::atomic_bool>(lifecycle_alive_)] {
        const auto alive = weak.lock();
        if (!alive || !alive->load(std::memory_order_acquire)) return;
        const auto table = area_table_;
        if (table) table->refresh();
    });
    comm_->refWallpaper.add([this, life = std::weak_ptr<std::atomic_bool>(lifecycle_alive_)] {
        const auto alive = life.lock();
        if (!alive || !alive->load(std::memory_order_acquire)) return;
        redrawAll();
        if (!alive->load(std::memory_order_acquire) ||
            lib::editor_window_host_options().all_mode || !services_.gui ||
            !docking_folder_) return;
        const auto gui = services_.gui;
        std::vector<dockingfolder::TabF> targets;
        for (const auto& key : docking_folder_->paneKeys()) {
            if (!StringView(key).starts_with("work")) continue;
            if (const auto tabs = docking_folder_->tabView(key))
                targets.push_back(tabs);
        }
        for (const auto& tabs : targets) {
            if (!alive->load(std::memory_order_acquire)) return;
            gui->request_redraw(tabs);
        }
    });
    comm_->refClassicSkin.add([this] {
        const std::weak_ptr<std::atomic_bool> life = lifecycle_alive_;
        refreshExecEngine();
        const auto alive = life.lock();
        if (!alive || !alive->load()) return;
        if (!lib::editor_window_host_options().all_mode) {
            const auto history = executed_party_history_dialog_;
            if (history) history->ref_classic_skin(false);
        }
    });
    comm_->refOuterTools.add([this] { refreshOuterTools(); });
    comm_->refHistories.add([this] { createFileMenu(); });
    comm_->refSkin.add([this] { refSkin(); });
    comm_->refImageScale.add([this] {
        for (const auto& [key, refresh] : scene_scale_refresh_) {
            (void)key;
            if (refresh) refresh();
        }
        // CardList はラスターのキャッシュと固定のセル寸法を持つ。 表示
        // 倍率の変更時は、開いている外部のカードペインも含めて両方を更新する。
        for (const auto& window : {cast_window_, skill_window_, item_window_,
                                   beast_window_, info_window_})
            if (window) window->refreshM();
        std::vector<const cardwindow_ns::CardWindow*> refreshed;
        for (const auto& [key, weak] : dynamic_card_windows_) {
            (void)key;
            if (const auto window = weak.lock(); window &&
                std::find(refreshed.begin(), refreshed.end(), window.get()) == refreshed.end()) {
                refreshed.push_back(window.get());
                window->refreshM();
            }
        }
    });
    comm_->refScenario.add([this](SummaryPtr summ) { refScenario(std::move(summ)); });
    // CardWindow の Commons は、主エディタの Commons
    // より意図的に機能を絞っている。 ここで共通のモデル通知を橋渡し
    // し、終了・更新リスナーが、 製品から一度も通知されない独立したシ
    // グナルではなく、D版と同じ発生元のイベントを受けるようにする。
    comm_->refCast.add([this](const auto& cast) { card_comm_.refCast.call(cast); });
    comm_->delCast.add([this](const auto& cast) { card_comm_.delCast.call(cast); });
    // 取り込み用 CardWindow は独自の共通 Commons に通知す
    // る。読み取り専用のイベント・シーンのホストにも D版の一つのモデル通知が
    // 届くよう、取り込み元の終了を双方向に伝える。 Summary ごとに保護
    // し、別の取り込み元の入れ子の終了は実行するが、同じ通知の往復は止める。
    const auto closing_sources=std::make_shared<std::unordered_set<const Summary*>>();
    const auto forward_source_close=[closing_sources](const SummaryPtr& summary, const auto& publish) {
        if(!summary || !closing_sources->insert(summary.get()).second) return;
        struct Release final {
            std::shared_ptr<std::unordered_set<const Summary*>> active;
            const Summary* value;
            ~Release(){active->erase(value);}
        } release{closing_sources,summary.get()};
        publish(summary);
    };
    comm_->closeAdds.add([this,forward_source_close](const auto& summary) {
        forward_source_close(summary,[this](const auto& value){card_comm_.closeAdds.call(value);});
    });
    card_comm_.closeAdds.add([this,forward_source_close,
                             life=std::weak_ptr<std::atomic_bool>(lifecycle_alive_)](const auto& summary) {
        const auto alive=life.lock();
        if(!alive || !alive->load(std::memory_order_acquire) || !comm_) return;
        forward_source_close(summary,[this](const auto& value){comm_->closeAdds.call(value);});
    });
    comm_->refSoundType.add([this] { refSoundType(); });
    comm_->refExecutedParties.add([this] { refExecutedParties(); });
    comm_->refImportHistory.add([this] { refImportHistory(); });
    comm_->changed.add([this, lifetime = std::weak_ptr<std::atomic_bool>(lifecycle_alive_)] {
        const auto alive = lifetime.lock();
        if (!alive || !alive->load(std::memory_order_acquire)) return;
        ++edit_generation_;
        updateChangeCount();
        refreshTitle();
    });
    comm_->replText.add([this] { refreshTitle(); });
    comm_->replText.add([signal = flag_comm_.refreshText] { signal.call(); });
    const auto refreshDirectorySettings = [this, life = std::weak_ptr(lifecycle_alive_)] {
        const auto alive = life.lock();
        if (!alive || !alive->load() || isAllMode() || !prop_) return;
        const auto view = directory_window_;
        if (view) view->updateDisplaySettings(prop_->var().logicalSort.value,
                                             valueOf(prop_->var().ignorePaths));
    };
    comm_->refSortCondition.add(refreshDirectorySettings);
    comm_->refIgnorePaths.add(refreshDirectorySettings);
    // D版の監視処理と同様、現在の設定を参照する。 対象外の ALL
    // ペインを有効化せず、同じ確定の境界で承認済みの設定を適用する。
    comm_->saved.add([this, life = std::weak_ptr(lifecycle_alive_)] {
        const auto alive = life.lock();
        if (!alive || !alive->load() || isAllMode() || !prop_) return;
        const bool enabled = prop_->var().traceDirectories.value;
        directory_prop_.traceChanges = enabled;
        const auto view = directory_window_;
        if (view) view->setTraceEnabled(enabled);
    });
    comm_->refDataVersion.add([signal = flag_comm_.refDataVersion] { signal.call(); });
    comm_->refPreviewValues.add([signal = flag_comm_.refSpecialPreview] { signal.call(); });
    comm_->refFlagAndStep.add([signal = flag_comm_.refSpecialPreview](const auto&, const auto&, const auto&) { signal.call(); });
    comm_->delFlagAndStep.add([signal = flag_comm_.refSpecialPreview](const auto&, const auto&, const auto&) { signal.call(); });
    comm_->refPath.add([signal = flag_comm_.refSpecialPreview](String, String, bool) { signal.call(); });
    comm_->refPaths.add([signal = flag_comm_.refSpecialPreview](String) { signal.call(); });
}

void MainWindow::initializeTypedPanes() {
    if (!comm_ || !prop_) return;
    const bool all_mode = isAllMode();

    // 具体的なウィンドウのモジュールは、それぞれ小さなツールキット非
    // 依存のプロパティ・共有情報を持つ。 MainWindow がそ
    // れらを所有し、コールバックをエディタ全体の調整処理に接続する。
    typed_host_ = std::make_shared<lib::Panel>();

    const auto& messages = prop_->requiredMsgs();
    comment_comm_.services = services_;
    comment_comm_.prop.services = services_;
    const auto edit_summary = [this] {
        if (!summary_ || !prop_) return;
        if (summary_dialog_ && summary_dialog_->dialog() &&
            summary_dialog_->dialog()->visible()) {
            summary_dialog_->active();
            return;
        }
        const auto data_dialog_owner = services_.gui && area_table_ &&
                area_table_->table()
            ? services_.gui->window_owner(area_table_->table()) : WindowPtr{};
        summary_dialog_ = std::make_shared<summarydialog::SummaryDialog>(
            *prop_, data_dialog_owner ? data_dialog_owner : window_, summary_.get(),
            !summary_->readOnlyPath().empty(),
            services_, comm_);
        // D版のデリゲートはコンテキストを共有する。std::function
        // の複製もこのトランザクションを共有し、 素材の準備後、実際の書き込み
        // 前に現在値と比較する。 別のエディタや素材の確認画面で変更されている
        // 場合があるため。 D版の AreaTable::UndoEdit::SummData
        // と同様、保存するのはこのプロパティ群とスキンの識別情報だけ
        // で、 キャストや Summary の全フィールドは含めない。
        struct SummaryUndoState {
            String name, author, description, type, skin_name;
            lib::Array<lib::Owner<card::CardImage>> images;
            lib::Array<String> coupons;
            std::uint32_t level_min{}, level_max{}, coupon_number{};
            std::uint64_t start_area{};
            SkinPtr skin;
            SummaryUndoState(const Summary& model, SkinPtr current_skin)
                : name(model.scenarioName()), author(model.author()), description(model.desc()),
                  type(model.type()), skin_name(model.skinName()), images(model.imagePaths()),
                  coupons(model.rCoupons()), level_min(model.levelMin()), level_max(model.levelMax()),
                  coupon_number(model.rCouponNum()), start_area(model.startArea()), skin(std::move(current_skin)) {}
            void restore(Summary& model) const {
                model.scenarioName(name); model.imagePaths(images); model.author(author);
                model.levelMin(level_min); model.levelMax(level_max); model.desc(description);
                model.type(type); model.skinName(skin_name); model.rCoupons(coupons);
                model.rCouponNum(coupon_number); model.startArea(start_area);
            }
            bool same(const Summary& model, const SkinPtr& current_skin) const {
                const auto current_images = model.imagePaths();
                if (images.size() != current_images.size()) return false;
                for (std::size_t i=0; i<images.size(); ++i) {
                    if (static_cast<bool>(images[i]) != static_cast<bool>(current_images[i])) return false;
                    if (images[i] && !(*images[i] == *current_images[i])) return false;
                }
                return name==model.scenarioName() && author==model.author() &&
                    description==model.desc() && type==model.type() && skin_name==model.skinName() &&
                    coupons==model.rCoupons() && level_min==model.levelMin() && level_max==model.levelMax() &&
                    coupon_number==model.rCouponNum() && start_area==model.startArea() && skin==current_skin;
            }
        };
        struct SummaryApplyState {
            std::shared_ptr<SummaryUndoState> undo_state;
            String name;
            String data_version;
            bool image_scale{};
            std::uint64_t start_area{};
            bool ready{};
            std::uint64_t generation{};
        };
        const auto previous = std::make_shared<SummaryApplyState>();
        const auto owner_life = std::weak_ptr<std::atomic_bool>(lifecycle_alive_);
        const auto dialog_life = std::weak_ptr<summarydialog::SummaryDialog>(summary_dialog_);
        const auto model_life = std::weak_ptr<Summary>(summary_);
        summary_dialog_->on_applying = [this, previous, owner_life, dialog_life, model_life] {
            previous->ready = false;
            ++previous->generation;
            const auto alive = owner_life.lock();
            if (!alive || !alive->load()) return;
            const auto dialog = dialog_life.lock();
            const auto model = model_life.lock();
            if (!dialog || dialog != summary_dialog_ || !model || model != summary_) return;
            previous->undo_state = std::make_shared<SummaryUndoState>(*model, skin_);
            previous->name = model->scenarioName();
            previous->data_version = model->dataVersion();
            previous->image_scale = model->loadScaledImage();
            previous->start_area = model->startArea();
            previous->ready = true;
        };
        summary_dialog_->appliedEvent.push_back(
            [this, previous, owner_life, dialog_life, model_life] {
            const auto alive = owner_life.lock();
            if (!alive || !alive->load()) return;
            const auto dialog = dialog_life.lock();
            const auto model = model_life.lock();
            if (!comm_ || !dialog || dialog != summary_dialog_ || !model || model != summary_ ||
                !std::exchange(previous->ready, false)) return;
            // 購読先を呼ぶ前に消費し、無効化された操作や入れ子の操作が、
            // このトランザクションの適用前の値を再利用しないようにする。
            const auto before = *previous;
            const auto current = [&] {
                return alive->load() && previous->generation == before.generation &&
                    dialog == summary_dialog_ && model == summary_;
            };
            const auto selected_skin = summary_dialog_->selectedSkin();
            if (selected_skin && selected_skin != skin_) {
                const auto old_skin = skin_;
                // Classic の選択では新しいオブジェクトが生成される場合がある。
                // D版と同様、同じ選択スキンを表す別オブジェクトになっただけでは通知せず
                // 、 素材ディレクトリが変わった場合だけ refSkin を通知する。
                const bool material_directory_changed = !old_skin ||
                    !utils::cfnmatch(utils::nabs(StringView(old_skin->resDir())),
                                    utils::nabs(StringView(selected_skin->resDir())));
                const bool old_uses_non_classic_materials = old_skin &&
                    !old_skin->legacy() &&
                    !old_skin->sourceOfMaterialsIsClassicEngine();
                const bool new_uses_non_classic_materials =
                    !selected_skin->legacy() &&
                    !selected_skin->sourceOfMaterialsIsClassicEngine();
                skin_ = selected_skin;
                comm_->skin(skin_);
                comm_->updateSkinMaterialsExtension(
                    summary_->useCounter(), old_skin, selected_skin);
                if (!current()) return;
                area_table_context_.skin = skin_.get();
                if (area_table_) area_table_->skin(skin_.get());
                if (!current()) return;
                if (material_directory_changed) comm_->refSkin.call();
                if (!current()) return;
                // D版と同様、旧形式のシナリオがClassic素材の供給条件
                // をまたぐ場合は、全子文書を変更済みにする。 そうしないと、
                // 未変更の子XMLで拡張子の書き換えが省略される場合がある。
                if (summary_->legacy() &&
                    valueOf(prop_->var().replaceClassicResourceExtension) &&
                    old_skin &&
                    old_uses_non_classic_materials !=
                        new_uses_non_classic_materials)
                    summary_->changedAll();
            }
            // D版は後のバージョン・倍率の書き込みより前に refCoupons
            // を通知し、倍率変更で参照回数を二度更新する場合がある。 このC++
            // 版のアダプターは適用成功後に通知し、各監視先が確定済みの Summary
            // を参照できるようにする。 参照回数の更新は一度だけ行う。
            if (summary_->scenarioName() != before.name)
                comm_->refScenarioName.call();
            if (!current()) return;
            if (!summary_->legacy() && summary_->dataVersion() != before.data_version)
                comm_->refDataVersion.call();
            if (!current()) return;
            if (summary_->loadScaledImage() != before.image_scale)
                comm_->refImageScale.call();
            if (!current()) return;
            comm_->refCoupons.call();
            if (!current()) return;
            comm_->refUseCount.call();
            if (!current()) return;
            // D版の SummaryDialog.apply と同様、対象を絞った通知
            // を行う。 適用のたびに全ペインを開き直したり、音声を再設定したりしない。
            refreshTitle();
            if (!current()) return;
            if (services_.gui && window_) services_.gui->request_redraw(window_);
            if (!current()) return;
            // 左上のエリア一覧の開始エリアの印は、このダイ
            // アログで選んだ開始エリアに直ちに追従させる。
            if (summary_->startArea() != before.start_area && area_table_)
                area_table_->refresh();
            if (!current()) return;
            if (area_table_ && before.undo_state && !before.undo_state->same(*model, skin_)) {
                const auto saved = before.undo_state;
                area_table_->storeSummaryEdit([this, saved, owner_life, model_life] {
                    const auto life = owner_life.lock();
                    const auto model = model_life.lock();
                    if (!life || !life->load() || !model || model != summary_ || !comm_ || !area_table_ ||
                        area_table_->summary() != model.get()) return;
                    SummaryUndoState replaced(*model, skin_);
                    const bool skin_changed = skin_ != saved->skin;
                    saved->restore(*model);
                    skin_ = saved->skin;
                    comm_->skin(skin_);
                    area_table_context_.skin = skin_.get();
                    area_table_->skin(skin_.get());
                    *saved = std::move(replaced);
                    const auto live = [&] { return life->load() && summary_ == model && comm_ && area_table_; };
                    comm_->refScenarioName.call();
                    if (!live()) return;
                    // 取り消しでは、適用時の素材ディレクトリによる条件とは異なり、オブジェ
                    // クトの同一性を使う。 共通ハンドラーは現在の称号から係数を求める。
                    if (skin_changed) comm_->refSkin.call();
                    if (!live()) return;
                    comm_->refUseCount.call();
                    if (!live()) return;
                    area_table_->refresh();
                });
            }
        });
        (void)summary_dialog_->open();
    };
    const auto open_replace_id =
        [this,weak=std::weak_ptr<std::atomic_bool>(lifecycle_alive_)](String from, const replacedialog::IDKind kind, const bool start) {
            const auto life=weak.lock();if(!life || !life->load())return;
            if (!summary_) return;
            configureReplaceDialog([from=std::move(from),kind,start](auto& dialog) {
                dialog.replaceID(from,kind,start);
            });
        };

    data_prop_.scenarioView = messages.scenarioView.value;
    data_prop_.variableView = messages.variableView.value;
    data_prop_.dataTabName = messages.areasTabName.value;
    data_prop_.areasTabName = messages.areasTabName.value;
    data_prop_.flagTabName = messages.flagTabName.value;
    data_prop_.areaId = messages.areaId.value;
    data_prop_.areaName = messages.areaName.value;
    data_prop_.areaCount = all_mode ? "利用" : messages.areaCount.value;
    data_prop_.flagName = messages.flagName.value;
    data_prop_.flagInitialValue = messages.flagInit.value;
    data_prop_.flagInitialization = messages.variableInitializationShort.value;
    data_prop_.showAreaDirectoryTree = valueOf(prop_->var().showAreaDirTree);
    data_prop_.tableViewImage = menuImage(MenuID::TableView);
    data_prop_.variableViewImage = menuImage(MenuID::VarView);
    data_prop_.readOnly = summary_ && !summary_->readOnlyPath().empty();
    area_table_context_.services = services_;
    area_table_context_.dialog_owner = window_;
    area_table_context_.editor_commons = comm_;
    area_table_context_.editor_commons_lifetime =
        comm_ ? comm_->lifetimeToken() : std::weak_ptr<const int>{};
    area_table_context_.menu_text = [this](const MenuID id) {
        return menuText(id);
    };
    area_table_context_.menu_action = [this](const MenuID id) {
        raiseEvent(id, SelectionEvent{});
    };
    area_table_context_.menu_enabled = [this](const MenuID id) {
        return canDoMenu(id);
    };
    area_table_context_.summary_image = images_ ? images_->summary() : 0U;
    area_table_context_.area_dir_image = images_ ? images_->areaDir() : 0U;
    area_table_context_.area_image = images_ ? images_->area() : 0U;
    area_table_context_.start_area_image = images_ ? images_->startArea() : 0U;
    area_table_context_.battle_image = images_ ? images_->battle() : 0U;
    area_table_context_.package_image = images_ ? images_->packages() : 0U;
    area_table_context_.skin = skin_.get();
    area_table_context_.callbacks.ref_paths = [this](std::string_view path) {
        if (comm_) comm_->refPaths.call(String(path));
    };
    area_table_context_.callbacks.ref_area = [this](areatable_ns::AbstractArea* value) {
        if (!comm_ || !summary_ || !value) return;
        if (const auto area = dynamic_cast<::cwxeditor::cwx::area::Area*>(value))
            comm_->refArea.call(summary_->area(area->id()));
        else if (const auto battle = dynamic_cast<::cwxeditor::cwx::area::Battle*>(value))
            comm_->refBattle.call(summary_->battle(battle->id()));
        else if (const auto package = dynamic_cast<::cwxeditor::cwx::area::Package*>(value))
            comm_->refPackage.call(summary_->cwPackage(package->id()));
    };
    area_table_context_.callbacks.del_area = [this](areatable_ns::AbstractArea* value) {
        if (!comm_ || !value) return;
        const auto shared = value->shared_from_this();
        if (const auto area = std::dynamic_pointer_cast<::cwxeditor::cwx::area::Area>(shared))
            comm_->delArea.call(area);
        else if (const auto battle = std::dynamic_pointer_cast<::cwxeditor::cwx::area::Battle>(shared))
            comm_->delBattle.call(battle);
        else if (const auto package = std::dynamic_pointer_cast<::cwxeditor::cwx::area::Package>(shared))
            comm_->delPackage.call(package);
    };
    area_table_context_.callbacks.refresh_toolbar = [this] {
        if (comm_) comm_->refreshToolBar();
    };
    area_table_context_.callbacks.status = [this](StringView value) {
        setStatusLine(String(value));
    };
    area_table_context_.callbacks.ref_scenario = [this](Summary*) {
        if (comm_ && summary_) comm_->refScenario.call(summary_);
    };
    area_table_context_.callbacks.ref_scenario_name = [this] { refreshTitle(); };
    area_table_context_.callbacks.refresh_flags = [this] {
        if (!flags_pane_) return;
        // AreaTable からの Summary 編集は、取り消し・
        // やり直しも含め、Summary 自体を置き換えずに 変数ディ
        // レクトリや参照回数を変える場合がある。D版と同様、後のシナリ
        // オ切り替えを待たず、 ここで状態変数ペインの両側を更新する。
        flags_pane_->dirs().refresh();
        flags_pane_->flags().refresh();
    };
    area_table_context_.callbacks.edit_summary = edit_summary;
    area_table_context_.callbacks.comment_title = [this](
        const areatable_ns::AbstractAreaPtr& value) {
        if (!value || !prop_) return String{};
        return ::cwxeditor::cwx::utils::tryFormat(
            prop_->requiredMsgs().dlgTitCommentWith.value, value->name());
    };
    area_table_context_.callbacks.make_comment_dialog = [this](
        const areatable_ns::AbstractAreaPtr& value, const StringView old) {
        auto dialog = createCommentDialog(old);
        if (area_table_context_.callbacks.comment_title)
            dialog->title = area_table_context_.callbacks.comment_title(value);
        return dialog;
    };
    area_table_context_.callbacks.replace_id =
        [open_replace_id](const std::uint64_t id,
                          const areatable_ns::AreaTableType type) {
            auto kind = replacedialog::IDKind::Area;
            if (type == areatable_ns::AreaTableType::Battle)
                kind = replacedialog::IDKind::Battle;
            else if (type == areatable_ns::AreaTableType::Package)
                kind = replacedialog::IDKind::Package;
            open_replace_id(std::to_string(id), kind, true);
        };
    area_table_context_.callbacks.do_import =
        [this](Summary* target, Summary* source,
               const std::vector<String>& paths) {
            if (comm_ && target && source && !paths.empty())
                comm_->doImport(target, source, paths);
        };
    area_table_context_.callbacks.use_count = [this](const areatable_ns::AbstractArea* value) {
        if (!value || !summary_ || !summary_->useCounter()) return std::size_t{};
        const auto counter = summary_->useCounter();
        if (const auto area = dynamic_cast<const ::cwxeditor::cwx::area::Area*>(value))
            return static_cast<std::size_t>(
                counter->get(::cwxeditor::cwx::usecounter::AreaId{area->id()}));
        if (const auto battle = dynamic_cast<const ::cwxeditor::cwx::area::Battle*>(value))
            return static_cast<std::size_t>(
                counter->get(::cwxeditor::cwx::usecounter::BattleId{battle->id()}));
        if (const auto package = dynamic_cast<const ::cwxeditor::cwx::area::Package*>(value))
            return static_cast<std::size_t>(
                counter->get(::cwxeditor::cwx::usecounter::PackageId{package->id()}));
        return std::size_t{};
    };
    area_table_context_.callbacks.open_scene = [this](
        areatable_ns::AbstractArea* value, bool activate, bool duplicate) {
        if (!comm_ || !prop_ || !summary_ || !value) return;
        const auto duplicated = duplicate ? std::make_shared<commons::TopLevelPanel>() : nullptr;
        if (const auto area = dynamic_cast<::cwxeditor::cwx::area::Area*>(value))
            (void)comm_->openAreaScene(*prop_, summary_.get(), summary_->area(area->id()),
                                       activate, duplicated);
        else if (const auto battle = dynamic_cast<::cwxeditor::cwx::area::Battle*>(value))
            (void)comm_->openAreaScene(*prop_, summary_.get(), summary_->battle(battle->id()),
                                       activate, duplicated);
    };
    area_table_context_.callbacks.open_event = [this](
        areatable_ns::AbstractArea* value, bool activate, bool duplicate) {
        if (!comm_ || !prop_ || !summary_ || !value) return;
        const auto duplicated = duplicate
            ? comm_->eventWindowFrom(summary_.get(), value) : nullptr;
        if (const auto area = dynamic_cast<::cwxeditor::cwx::area::Area*>(value))
            (void)comm_->openAreaEvent(*prop_, summary_.get(), summary_->area(area->id()),
                                       activate, duplicated);
        else if (const auto battle = dynamic_cast<::cwxeditor::cwx::area::Battle*>(value))
            (void)comm_->openAreaEvent(*prop_, summary_.get(), summary_->battle(battle->id()),
                                       activate, duplicated);
        else if (const auto package = dynamic_cast<::cwxeditor::cwx::area::Package*>(value))
            (void)comm_->openArea(*prop_, summary_.get(), summary_->cwPackage(package->id()),
                                  activate, duplicated);
    };
    area_table_context_.callbacks.open_package = area_table_context_.callbacks.open_event;
    auto* const area_table_bus = comm_;
    const auto area_table_bus_life = area_table_bus->lifetimeToken();
    area_table_context_.callbacks.publish_import_sort =
        [area_table_bus, area_table_bus_life](int column, int direction) {
            if (!area_table_bus_life.expired()) area_table_bus->refImportAreasSort.call(column, direction);
        };
    area_table_context_.callbacks.subscribe_import_sort =
        [area_table_bus, area_table_bus_life](std::function<void(int, int)> notify) -> lib::Action {
            if (area_table_bus_life.expired()) return {};
            const auto token = area_table_bus->refImportAreasSort.add(std::move(notify));
            return [area_table_bus, area_table_bus_life, token] {
                if (!area_table_bus_life.expired()) area_table_bus->refImportAreasSort.remove(token);
            };
        };
    area_table_context_.callbacks.subscribe_use_count =
        [area_table_bus, area_table_bus_life](lib::Action notify) -> lib::Action {
            if (area_table_bus_life.expired()) return {};
            const auto token = area_table_bus->refUseCount.add(std::move(notify));
            return [area_table_bus, area_table_bus_life, token] {
                if (!area_table_bus_life.expired()) area_table_bus->refUseCount.remove(token);
            };
        };
    area_table_context_.callbacks.publish_use_count =
        [area_table_bus, area_table_bus_life] {
            if (!area_table_bus_life.expired()) area_table_bus->refUseCount.call();
        };
    area_table_context_.callbacks.use_count_from = [](Summary* source, const areatable_ns::AbstractArea* value) {
        if (!source || !value || !source->useCounter()) return std::size_t{};
        const auto counter = source->useCounter();
        if (const auto area = dynamic_cast<const ::cwxeditor::cwx::area::Area*>(value))
            return static_cast<std::size_t>(counter->get(::cwxeditor::cwx::usecounter::AreaId{area->id()}));
        if (const auto battle = dynamic_cast<const ::cwxeditor::cwx::area::Battle*>(value))
            return static_cast<std::size_t>(counter->get(::cwxeditor::cwx::usecounter::BattleId{battle->id()}));
        if (const auto package = dynamic_cast<const ::cwxeditor::cwx::area::Package*>(value))
            return static_cast<std::size_t>(counter->get(::cwxeditor::cwx::usecounter::PackageId{package->id()}));
        return std::size_t{};
    };
    area_table_context_.callbacks.stop_open = [this, weak = std::weak_ptr<std::atomic_bool>(lifecycle_alive_)] {
        const auto alive = weak.lock();
        return !alive || !alive->load(std::memory_order_acquire) || !comm_ || comm_->stopOpen();
    };
    area_table_context_.callbacks.open_scene_from = [this](
        Summary* source, areatable_ns::AbstractArea* value,
        const bool activate, const bool duplicate) {
        if (!comm_ || !prop_ || !source || !value) return;
        const auto duplicated = duplicate
            ? std::make_shared<commons::TopLevelPanel>() : nullptr;
        if (const auto area = dynamic_cast<::cwxeditor::cwx::area::Area*>(value))
            (void)comm_->openAreaScene(*prop_, source, source->area(area->id()),
                                       activate, duplicated);
        else if (const auto battle =
                     dynamic_cast<::cwxeditor::cwx::area::Battle*>(value))
            (void)comm_->openAreaScene(*prop_, source, source->battle(battle->id()),
                                       activate, duplicated);
    };
    area_table_context_.callbacks.open_event_from = [this](
        Summary* source, areatable_ns::AbstractArea* value,
        const bool activate, const bool duplicate) {
        if (!comm_ || !prop_ || !source || !value) return;
        const auto duplicated = duplicate
            ? comm_->eventWindowFrom(source, value) : nullptr;
        if (const auto area = dynamic_cast<::cwxeditor::cwx::area::Area*>(value))
            (void)comm_->openAreaEvent(*prop_, source, source->area(area->id()),
                                       activate, duplicated);
        else if (const auto battle =
                     dynamic_cast<::cwxeditor::cwx::area::Battle*>(value))
            (void)comm_->openAreaEvent(*prop_, source, source->battle(battle->id()),
                                       activate, duplicated);
        else if (const auto package =
                     dynamic_cast<::cwxeditor::cwx::area::Package*>(value))
            (void)comm_->openArea(*prop_, source, source->cwPackage(package->id()),
                                  activate, duplicated);
    };
    area_table_context_.callbacks.open_package_from =
        area_table_context_.callbacks.open_event_from;
    area_table_ = std::make_shared<areatable_ns::AreaTable>(
        area_table_context_, *prop_, false, nullptr);
    area_table_->construct(typed_host_);
    comm_->packageCreators(
        [this, life = std::weak_ptr<std::atomic_bool>(lifecycle_alive_)](
            const ::cwxeditor::cwx::event::EventTreePtr& tree, String name, const bool) -> std::uint64_t {
            const auto current = life.lock();
            if (!current || !current->load(std::memory_order_acquire)) return 0U;
            const auto table_owner = area_table_;
            const auto table = table_owner ? table_owner->table() : WidgetPtr{};
            const auto services = services_;
            if (!table_owner || !table) return 0U;
            dutils::forceFocus(table, false, services);
            if (!current->load(std::memory_order_acquire)) return 0U;
            return table_owner->createPackage(tree, std::move(name));
        },
        [this, life = std::weak_ptr<std::atomic_bool>(lifecycle_alive_)](
            const ::cwxeditor::cwx::event::ContentPtr& start, String name, const bool) -> std::uint64_t {
            const auto current = life.lock();
            if (!current || !current->load(std::memory_order_acquire)) return 0U;
            const auto table_owner = area_table_;
            const auto table = table_owner ? table_owner->table() : WidgetPtr{};
            const auto services = services_;
            if (!table_owner || !table) return 0U;
            dutils::forceFocus(table, false, services);
            if (!current->load(std::memory_order_acquire)) return 0U;
            return table_owner->createPackage(start, std::move(name));
        });

    flag_comm_.services = services_;
    if (comm_) {
        flag_comm_.subscribeVariableDeletion =
            [bus = comm_, life = comm_->lifetimeToken()](
                flagspane_ns::Commons::VariableDeletionCallback callback) -> lib::Action {
                if (life.expired()) return {};
                const auto token = bus->delFlagAndStep.add(std::move(callback));
                return [bus, life, token] {
                    if (!life.expired()) bus->delFlagAndStep.remove(token);
                };
            };
    }
    flag_comm_.publishUseCount = [this, life = std::weak_ptr<std::atomic_bool>(lifecycle_alive_)] {
        const auto alive = life.lock();
        if (alive && alive->load(std::memory_order_acquire) && comm_)
            comm_->refUseCount.call();
    };
    flag_comm_.stopOpen = [this, weak = std::weak_ptr<std::atomic_bool>(lifecycle_alive_)] {
        const auto alive = weak.lock();
        return !alive || !alive->load(std::memory_order_acquire) ||
               !comm_ || comm_->stopOpen();
    };
    flag_comm_.editorProps = [properties = prop_, life = std::weak_ptr(lifecycle_alive_)] {
        const auto alive = life.lock();
        return alive && alive->load() ? properties : nullptr;
    };
    flag_comm_.setStatusLine = [this, weak = std::weak_ptr(lifecycle_alive_)](
        const lib::WidgetPtr& owner, StringView value) {
        const auto alive = weak.lock();
        if (!alive || !alive->load() || !comm_) return;
        publishPaneStatusLine(owner, String(value));
    };
    flag_comm_.xmlCopyProvider = [properties = prop_, life = std::weak_ptr(lifecycle_alive_)] {
        const auto alive = life.lock();
        return alive && alive->load() && properties && properties->var().xmlCopy.value;
    };
    flag_comm_.commentTitle = [prop = prop_, life = std::weak_ptr(lifecycle_alive_)](StringView name) {
        const auto alive = life.lock();
        if (!alive || !alive->load() || !prop) return String{};
        return ::cwxeditor::cwx::utils::tryFormat(prop->requiredMsgs().dlgTitCommentWith.value, String(name));
    };
    flag_comm_.warningImage = prop_->images().load(StringView{"warning.png"});
    flag_comm_.warnings = [this](const ::cwxeditor::cwx::event::CWXPath& value) {
        if (!prop_ || !skin_) return std::vector<String>{};
        return ::cwxeditor::cwx::warning::warnings(
            prop_->parent(), *skin_, summary_.get(), value,
            summary_ && summary_->legacy(),
            summary_ ? summary_->dataVersion()
                     : ::cwxeditor::cwx::versioninfo::LATEST_VERSION,
            prop_->var().targetVersion.value);
    };
    flag_comm_.refreshToolBar = [this] { if (comm_) comm_->refreshToolBar(); };
    flag_comm_.menuText = [this](const MenuID id) { return menuText(id); };
    flag_comm_.menuImage = [this](const MenuID id) { return menuImage(id); };
    flag_comm_.menuAction = [this](const MenuID id) {
        raiseEvent(id, SelectionEvent{});
    };
    flag_comm_.menuEnabled = [this](const MenuID id) {
        return canDoMenu(id);
    };
    flag_comm_.typedReplaceIDRequest.add(
        [open_replace_id](String value, const flagspane_ns::VariableKind type, const bool) {
            auto kind = replacedialog::IDKind::Variant;
            if (type == flagspane_ns::VariableKind::Flag) kind = replacedialog::IDKind::Flag;
            else if (type == flagspane_ns::VariableKind::Step) kind = replacedialog::IDKind::Step;
            open_replace_id(std::move(value), kind, true);
        });
    flag_comm_.refScenario.add([this] {
        if (comm_ && summary_) comm_->refScenario.call(summary_);
    });
    const auto notify_variables = [this] {
        if (summary_) summary_->changed();
        if (comm_) {
            comm_->refUseCount.call();
            comm_->refreshToolBar();
        }
    };
    flag_comm_.refFlagAndStep.add(
        [this, notify_variables](const flagspane_ns::FlagList& flags, const flagspane_ns::StepList& steps,
                           const flagspane_ns::VariantList& variants) {
            notify_variables();
            if (comm_) comm_->refFlagAndStep.call(flags, steps, variants);
        });
    // 所有者は表の更新より前に削除を通知するため、 参照回数とツ
    // ールバーの更新は、呼び出し元の通知後の処理に遅延させる。
    flag_comm_.delFlagAndStep.add(
        [this, life = std::weak_ptr<std::atomic_bool>(lifecycle_alive_),
         dispatching = std::make_shared<bool>(false)](
            const flagspane_ns::FlagList& flags,
            const flagspane_ns::StepList& steps,
            const flagspane_ns::VariantList& variants) {
            const auto alive = life.lock();
            if (!alive || !alive->load(std::memory_order_acquire) ||
                *dispatching)
                return;
            *dispatching = true;
            struct DispatchGuard final {
                std::shared_ptr<bool> state;
                ~DispatchGuard() { if (state) *state = false; }
            } dispatch_guard{dispatching};
            if (comm_) comm_->delFlagAndStep.call(flags, steps, variants);
            if (!alive->load(std::memory_order_acquire)) return;
            if (summary_) summary_->changed();
        });
    flag_comm_.refFlagDir.add(
        [notify_variables](const flagspane_ns::FlagDirPtr&) { notify_variables(); });
    flag_comm_.delFlagDir.add(
        [notify_variables](const flagspane_ns::FlagDirPtr&) { notify_variables(); });
    flag_props_.etc.startIncrementalSearchWhenKeyDown = prop_->var().startIncrementalSearchWhenKeyDown.value;
    flag_props_.etc.startIncrementalSearchWhenKeyDownProvider =
        [prop = prop_, life = std::weak_ptr<std::atomic_bool>(lifecycle_alive_)] {
            const auto alive = life.lock();
            return alive && alive->load(std::memory_order_acquire) && prop &&
                prop->var().startIncrementalSearchWhenKeyDown.value;
        };
    flag_props_.etc.incrementalSearchBoxWidth = prop_->var().incrementalSearchBoxWidth.value;
    flag_props_.etc.incrementalSearchType = prop_->var().incrementalSearchType.value;
    flag_props_.etc.storeIncrementalSearchType =
        [properties = prop_, weak = std::weak_ptr(lifecycle_alive_)](std::int32_t type) {
            const auto alive = weak.lock();
            if (alive && alive->load() && properties)
                properties->var().incrementalSearchType.value = type;
        };
    flag_props_.msgs.flag = messages.flag.value;
    flag_props_.msgs.statusFormat = messages.flagStatus.value;
    flag_props_.msgs.statusSelectedFormat = messages.flagStatusSel.value;
    flag_props_.msgs.step = messages.step.value;
    flag_props_.msgs.variant = messages.variant.value;
    flag_props_.msgs.variantValueHint = messages.variantValueHint.value;
    flag_props_.msgs.warningInvalidVariantValue = messages.warningInvalidVariantValue.value;
    flag_props_.msgs.flagName = messages.flagName.value;
    flag_props_.msgs.flagInit = messages.flagInit.value;
    flag_props_.msgs.variableInitializationShort = messages.variableInitializationShort.value;
    flag_props_.msgs.variableInitializationNameLeave = messages.variableInitializationNameLeave.value;
    flag_props_.msgs.variableInitializationNameEventExit = messages.variableInitializationNameEventExit.value;
    flag_props_.msgs.variableInitializationNameBattleStart = messages.variableInitializationNameBattleStart.value;
    flag_props_.msgs.variableInitializationNameBattleEnd = messages.variableInitializationNameBattleEnd.value;
    flag_props_.msgs.variableInitializationNameNone = messages.variableInitializationNameNone.value;

    // ALL仕様: 左上の状態変数表とその選択肢には省略した初期化名を共通で使う。
    flag_props_.msgs.variableInitializationShortNameLeave =
        all_mode ? "シナ終" : messages.variableInitializationShortNameLeave.value;
    flag_props_.msgs.variableInitializationShortNameEventExit =
        all_mode ? "イベ終" : messages.variableInitializationShortNameEventExit.value;
    flag_props_.msgs.variableInitializationShortNameBattleStart =
        all_mode ? "戦開" : messages.variableInitializationShortNameBattleStart.value;
    flag_props_.msgs.variableInitializationShortNameBattleEnd =
        all_mode ? "戦終" : messages.variableInitializationShortNameBattleEnd.value;
    flag_props_.msgs.variableInitializationShortNameNone =
        all_mode ? "しない" : messages.variableInitializationShortNameNone.value;
    flag_props_.msgs.flagCount = all_mode ? "利用" : messages.flagCount.value;
    flag_props_.msgs.flagDirRoot = messages.flagDirRoot.value;
    flag_props_.msgs.flagDirNew = messages.flagDirNew.value;
    flag_props_.msgs.newFlag = messages.flag.value;
    flag_props_.msgs.newStep = messages.step.value;
    flag_props_.msgs.newVariant = messages.variant.value;
    flag_props_.msgs.flagTrue = messages.dlgLblFlagTrue.value;
    flag_props_.msgs.flagFalse = messages.dlgLblFlagFalse.value;
    flag_props_.msgs.setFlagTrue = messages.setFlagTrue.value;
    flag_props_.msgs.setFlagFalse = messages.setFlagFalse.value;
    flag_props_.msgs.stepName = messages.dlgLblStepName.value;
    flag_props_.msgs.stepInitial = messages.dlgLblStepInit.value;
    flag_props_.msgs.stepCount = messages.stepCount.value;
    flag_props_.msgs.flagEditName = messages.dlgLblFlagName.value;
    flag_props_.msgs.flagInitial = messages.dlgLblFlagInit.value;
    flag_props_.msgs.variantName = messages.variantName.value;
    flag_props_.msgs.numberValue = messages.numberValue.value;
    flag_props_.msgs.stringValue = messages.stringValue.value;
    flag_props_.msgs.booleanValue = messages.booleanValue.value;
    flag_props_.msgs.warningLimitForNumberOfVariant =
        messages.warningLimitForNumberOfVariant.value;
    flag_props_.msgs.variableInitialization = messages.variableInitialization.value;
    flag_props_.msgs.expandSpecialCharacters = messages.expandSPChars.value;
    flag_props_.msgs.setStepValue = messages.setStepValue.value;
    flag_props_.etc.flagSashV = prop_->var().flagSashV.value;
    flag_props_.etc.flagSashL = prop_->var().flagSashL.value;
    flag_props_.etc.flagSashR = prop_->var().flagSashR.value;
    flag_props_.etc.storeFlagSash = [this, life = std::weak_ptr(lifecycle_alive_)](
        bool vertical, std::int32_t left, std::int32_t right) {
        const auto alive = life.lock();
        if (!alive || !alive->load() || !prop_) return;
        prop_->var().flagSashV.value = vertical;
        prop_->var().flagSashL.value = left;
        prop_->var().flagSashR.value = right;
        (void)prop_->apply();
    };
    // ALL仕様: 既存の名称112px・初期化105pxから50pxを名称へ移す。利用 -22pxは維持。
    flag_props_.etc.flagNameColumn = all_mode ? 162.0F
        : static_cast<float>(prop_->var().flagNameColumn.value);
    flag_props_.etc.flagInitColumn = all_mode ? 140.0F
        : static_cast<float>(prop_->var().flagInitColumn.value);
    flag_props_.etc.variableInitializationColumn = all_mode ? 55.0F
        : static_cast<float>(prop_->var().variableInitializationColumn.value);
    flag_props_.etc.flagCountColumn = all_mode ? 28.0F
        : static_cast<float>(prop_->var().flagCountColumn.value);
    flag_props_.etc.localFlagNameColumn = all_mode ? 162.0F
        : static_cast<float>(prop_->var().localFlagNameColumn.value);
    flag_props_.etc.localFlagInitColumn = all_mode ? 140.0F
        : static_cast<float>(prop_->var().localFlagInitColumn.value);
    flag_props_.etc.localVariableInitializationColumn = all_mode ? 55.0F
        : static_cast<float>(prop_->var().localVariableInitializationColumn.value);
    flag_props_.etc.localFlagCountColumn = all_mode ? 28.0F
        : static_cast<float>(prop_->var().localFlagCountColumn.value);
    flag_props_.etc.flagNameWidth = static_cast<float>(
        prop_->var().flagNameWidth.value);
    flag_props_.etc.flagInitWidth = static_cast<float>(
        prop_->var().flagInitWidth.value);
    flag_props_.etc.flagValueWidth = static_cast<float>(
        prop_->var().flagValueWidth.value);
    flag_props_.etc.firesWidth = static_cast<float>(
        prop_->var().firesWidth.value);
    flag_props_.etc.storeFlagColumn = [this](const bool local,
                                             const std::size_t column,
                                             const float width) {
        if (!prop_ || column >= 4U || !std::isfinite(width) || width <= 0.0F)
            return;
        auto& vars = prop_->var();
        const auto saved = static_cast<std::int32_t>(std::lround(width));
        if (local) {
            switch (column) {
            case 0U: vars.localFlagNameColumn.value = saved; break;
            case 1U: vars.localFlagInitColumn.value = saved; break;
            case 2U: vars.localVariableInitializationColumn.value = saved; break;
            case 3U: vars.localFlagCountColumn.value = saved; break;
            default: break;
            }
        } else {
            switch (column) {
            case 0U: vars.flagNameColumn.value = saved; break;
            case 1U: vars.flagInitColumn.value = saved; break;
            case 2U: vars.variableInitializationColumn.value = saved; break;
            case 3U: vars.flagCountColumn.value = saved; break;
            default: break;
            }
        }
        (void)prop_->apply();
    };
    flag_props_.etc.flagInitValue = prop_->var().flagInitValue.value;
    flag_props_.etc.flagTrues = prop_->var().flagTrues.value;
    flag_props_.etc.flagFalses = prop_->var().flagFalses.value;
    // ペインの Props は複製される。新しいエディタは現在の既定値を読み、
    // 設定変更で既存のフラグや開いている未確定の編集を変更しないようにする。
    flag_props_.etc.flagDefaultsProvider =
        [prop = prop_, weak = std::weak_ptr<std::atomic_bool>(lifecycle_alive_),
         fallback = flagspane_ns::FlagCreationDefaults{flag_props_.etc.flagInitValue,
             flag_props_.etc.flagTrues, flag_props_.etc.flagFalses}] {
        const auto alive = weak.lock();
        if (!alive || !alive->load(std::memory_order_acquire)) return fallback;
        return flagspane_ns::FlagCreationDefaults{prop->var().flagInitValue.value,
            prop->var().flagTrues.value, prop->var().flagFalses.value};
    };
    flag_props_.etc.stepValueName = prop_->var().stepValueName.value;
    flag_props_.etc.limitForNumberOfVariant =
        prop_->var().limitForNumberOfVariant.value;
    flag_props_.etc.undoMaxMainView = static_cast<std::size_t>(
        std::max<std::int32_t>(0, valueOf(prop_->var().undoMaxMainView)));
    flag_props_.etc.undoMaxEtcProvider =
        [prop = prop_, weak = std::weak_ptr<std::atomic_bool>(lifecycle_alive_)] {
        const auto alive = weak.lock();
        if (!alive || !alive->load(std::memory_order_acquire)) return std::size_t{0};
        return static_cast<std::size_t>(
            std::max<std::int32_t>(0, valueOf(prop->var().undoMaxEtc)));
    };
    flag_props_.etc.undoMaxEtc = flag_props_.etc.undoMaxEtcProvider();
    flag_props_.looks.stepMaxCount = static_cast<std::size_t>(
        std::max(1, prop_->looks().stepMaxCount()));
    flag_props_.flagDirImage = images_ ? images_->flagDir() : 0U;
    flag_props_.flagImage = images_ ? images_->flag() : 0U;
    flag_props_.stepImage = images_ ? images_->step() : 0U;
    flag_props_.variantImage = images_ ? images_->variant() : 0U;
    flags_pane_ = std::make_shared<flagspane_ns::FlagsPane>(flag_comm_, flag_props_);
    (void)flags_pane_->construct(typed_host_);
    // 変数ペインは内部の Panel に配置されるため、構築時の親から
    // 最上位の所有者を推定できない。 フラグ・ステップのプロパティダイ
    // アログは、主ウィンドウを明示的な所有者・中央配置の基準にする。
    flags_pane_->dialogContext(summary_, window_);

    card_comm_.services = services_;
    card_comm_.warningImage = prop_->images().load(StringView{"warning.png"});
    card_comm_.materialPath = [this](const cardwindow_ns::SummaryPtr& target) {
        if (skin_) return skin_->materialPath();
        return target && target->legacy() ? String{} : String("Material");
    };
    card_comm_.refPaths = [this](const StringView path) {
        if (comm_) comm_->refPaths.call(String(path));
    };
    card_comm_.warnings = [this](const cardwindow_ns::SummaryPtr& source,
                                 const ::cwxeditor::cwx::path::CWXPath& value) {
        // D版の取り込み元 CardPane と同様、その Summary 用に解決したス
        // キンを保持する。 転送先にだけある素材によって、取り込み元の素材不足を隠さない。
        auto warning_skin = skin_;
        if (source && source != summary_ && comm_)
            warning_skin = comm_->findSkinFromHistory(source.get());
        if (!prop_ || !warning_skin) return std::vector<String>{};
        return ::cwxeditor::cwx::warning::warnings(
            prop_->parent(), *warning_skin, source.get(), value,
            source && source->legacy(),
            source ? source->dataVersion()
                     : ::cwxeditor::cwx::versioninfo::LATEST_VERSION,
            prop_->var().targetVersion.value);
    };
    card_comm_.refreshToolBar = [this] { if (comm_) comm_->refreshToolBar(); };
    card_comm_.refUseCount = [this] { if (comm_) comm_->refUseCount.call(); };
    card_comm_.menuText = [this](const MenuID id) { return menuText(id); };
    card_comm_.replaceID =
        [open_replace_id](const cardwindow_ns::CardType type,
                          const std::uint64_t id, const bool start) {
            auto kind = replacedialog::IDKind::Cast;
            switch (type) {
            case cardwindow_ns::CardType::Cast:
                kind = replacedialog::IDKind::Cast;
                break;
            case cardwindow_ns::CardType::Skill:
                kind = replacedialog::IDKind::Skill;
                break;
            case cardwindow_ns::CardType::Item:
                kind = replacedialog::IDKind::Item;
                break;
            case cardwindow_ns::CardType::Beast:
                kind = replacedialog::IDKind::Beast;
                break;
            case cardwindow_ns::CardType::Info:
                kind = replacedialog::IDKind::Info;
                break;
            }
            open_replace_id(std::to_string(id), kind, start);
        };
    card_comm_.addScenario = [this] { addScenario(); };
    card_comm_.addScenarioFiles = [this](const std::vector<String>& paths) {
        addScenario(paths);
    };
    card_comm_.openCardPane = [this, weak = std::weak_ptr<std::atomic_bool>(lifecycle_alive_)](cardwindow_ns::CardType type, bool activate) {
        const auto alive = weak.lock();
        if (!alive || !alive->load(std::memory_order_acquire) || !comm_)
            return cardwindow_ns::CardPanePtr{};
        // 有効化する要求も、しない要求も Commons を通す。 呼び出し元がフォ
        // ーカスを別の場所に残す場合でも、ホストが対象ペインを配置できるようにする。
        if (!comm_->openCardPane(static_cast<std::int32_t>(type), activate) ||
            !alive->load(std::memory_order_acquire))
            return cardwindow_ns::CardPanePtr{};
        switch (type) {
        case cardwindow_ns::CardType::Cast:
            return cast_window_ ? cast_window_->paneCast() : cardwindow_ns::CardPanePtr{};
        case cardwindow_ns::CardType::Skill:
            return skill_window_ ? skill_window_->paneSkill() : cardwindow_ns::CardPanePtr{};
        case cardwindow_ns::CardType::Item:
            return item_window_ ? item_window_->paneItem() : cardwindow_ns::CardPanePtr{};
        case cardwindow_ns::CardType::Beast:
            return beast_window_ ? beast_window_->paneBeast() : cardwindow_ns::CardPanePtr{};
        case cardwindow_ns::CardType::Info:
            return info_window_ ? info_window_->paneInfo() : cardwindow_ns::CardPanePtr{};
        }
        return cardwindow_ns::CardPanePtr{};
    };
    card_comm_.stopOpen = [this, weak = std::weak_ptr<std::atomic_bool>(lifecycle_alive_)] {
        const auto alive = weak.lock();
        return !alive || !alive->load(std::memory_order_acquire) || !comm_ || comm_->stopOpen();
    };
    card_comm_.openHands = [this](
        const cardwindow_ns::Props&, const SummaryPtr& summ,
        const cardwindow_ns::CastCardPtr& card, const SummaryPtr& toc, bool activate) {
        if (!comm_ || !prop_ || !summ || !card) return;
        if (toc) (void)comm_->openAddHands(*prop_, summ.get(), card, toc.get(), activate);
        else (void)comm_->openHands(*prop_, summ.get(), card, activate);
    };
    card_comm_.openHandsPane = [this](
        const cardwindow_ns::Props&, const SummaryPtr& summ,
        const cardwindow_ns::CastCardPtr& card, const SummaryPtr& toc,
        const cardwindow_ns::CardType type, const bool activate) {
        if (!comm_ || !prop_ || !summ || !card) return cardwindow_ns::CardPanePtr{};
        const auto alive = std::weak_ptr<std::atomic_bool>(lifecycle_alive_);
        const auto panel = toc
            ? comm_->openAddHands(*prop_, summ.get(), card, toc.get(), activate)
            : comm_->openHands(*prop_, summ.get(), card, activate);
        const auto flag = alive.lock();
        if (!flag || !flag->load(std::memory_order_acquire) || !panel)
            return cardwindow_ns::CardPanePtr{};
        // Commons の返すものはドッキングのホストであり、別の手札エディタではない。 こ
        // のクライアントパネルそのものに結び付いた、既存の CardWindow を取得する。
        for (const auto& [key, weak] : dynamic_card_windows_) {
            (void)key;
            const auto editor = weak.lock();
            if (editor && editor->panel() == panel->shell() &&
                editor->summary() == summ && editor->owner() == card)
                return editor->openPane(type, activate);
        }
        return cardwindow_ns::CardPanePtr{};
    };
    card_comm_.openHandsPath = [this](const SummaryPtr& summ,
        const cardwindow_ns::CastCardPtr& card, const SummaryPtr& toc,
        const StringView path, const bool activate) {
        if (!comm_ || !prop_ || !summ || !card) return false;
        const bool focus = activate && !::cwxeditor::cwx::path::cphasattr(path, "nofocus");
        const auto panel = toc
            ? comm_->openAddHands(*prop_, summ.get(), card, toc.get(), focus)
            : comm_->openHands(*prop_, summ.get(), card, focus);
        return panel && panel->openCWXPath(String(path), activate);
    };
    card_comm_.openUseEventsPath = [this](const SummaryPtr& summ,
        const cardwindow_ns::EffectCardPtr& card, const StringView path,
        const bool activate) {
        if (!comm_ || !prop_ || !summ || !card) return false;
        const auto panel = comm_->openUseEvents(*prop_, summ.get(), card,
            activate && !::cwxeditor::cwx::path::cphasattr(path, "nofocus"));
        return panel && panel->openCWXPath(String(path), activate);
    };
    card_comm_.refCard = [this](const cardwindow_ns::CardPtr& value) {
        if (!comm_ || !value) return;
        if (const auto card = std::dynamic_pointer_cast<::cwxeditor::cwx::card::CastCard>(value)) comm_->refCast.call(card);
        else if (const auto card = std::dynamic_pointer_cast<::cwxeditor::cwx::card::SkillCard>(value)) comm_->refSkill.call(card);
        else if (const auto card = std::dynamic_pointer_cast<::cwxeditor::cwx::card::ItemCard>(value)) comm_->refItem.call(card);
        else if (const auto card = std::dynamic_pointer_cast<::cwxeditor::cwx::card::BeastCard>(value)) comm_->refBeast.call(card);
        else if (const auto card = std::dynamic_pointer_cast<::cwxeditor::cwx::card::InfoCard>(value)) comm_->refInfo.call(card);
    };
    card_comm_.delCard = [this](const cardwindow_ns::CWXPathPtr& owner,
                                 const cardwindow_ns::CardPtr& value) {
        if (!comm_ || !value) return;
        // D版の cardpane.delCard と同様、先に所属を外し
        // 、Cast より前に子の削除を通知する。 子のイベントには内包す
        // る所有者の引数を維持し、切り離した Cast に置き換えない。
        if (const auto cast = std::dynamic_pointer_cast<::cwxeditor::cwx::card::CastCard>(value)) {
            for (const auto& child : cast->skills()) comm_->delSkill.call(owner, child);
            for (const auto& child : cast->items()) comm_->delItem.call(owner, child);
            for (const auto& child : cast->beasts()) comm_->delBeast.call(owner, child);
            comm_->delCast.call(cast);
        } else if (const auto card = std::dynamic_pointer_cast<::cwxeditor::cwx::card::SkillCard>(value)) {
            comm_->delSkill.call(owner, card);
        } else if (const auto card = std::dynamic_pointer_cast<::cwxeditor::cwx::card::ItemCard>(value)) {
            comm_->delItem.call(owner, card);
        } else if (const auto card = std::dynamic_pointer_cast<::cwxeditor::cwx::card::BeastCard>(value)) {
            comm_->delBeast.call(owner, card);
        } else if (const auto card = std::dynamic_pointer_cast<::cwxeditor::cwx::card::InfoCard>(value)) {
            comm_->delInfo.call(card);
        }
        comm_->refUseCount.call();
        if (const auto cast = std::dynamic_pointer_cast<::cwxeditor::cwx::card::CastCard>(owner))
            comm_->refCast.call(cast);
    };
    card_comm_.openUseEvents = [this](
        const cardwindow_ns::Props&, const SummaryPtr& summ,
        const cardwindow_ns::EffectCardPtr& card, bool activate, bool duplicate) {
        if (!comm_ || !prop_ || !summ || !card) return;
        (void)comm_->openUseEvents(
            *prop_, summ.get(), card, activate,
            duplicate ? std::make_shared<commons::TopLevelPanel>() : nullptr);
    };
    card_comm_.commentTitle = [this](const cardwindow_ns::CardPtr& card) {
        if (!card || !prop_) return String{};
        return ::cwxeditor::cwx::utils::tryFormat(
            prop_->requiredMsgs().dlgTitCommentWith.value, card->name());
    };
    card_comm_.makeCommentDialog = [this](const SummaryPtr&,
                                          const cardwindow_ns::CardPtr& card,
                                          const StringView old) {
        auto dialog = createCommentDialog(old);
        if (card_comm_.commentTitle) dialog->title = card_comm_.commentTitle(card);
        return dialog;
    };
    card_comm_.openCardDialogPath = [this, alive = std::weak_ptr<std::atomic_bool>(lifecycle_alive_)](
        const cardwindow_ns::CardPtr& card, const StringView path, const bool activate) {
        const auto token = alive.lock();
        if (!token || !token->load(std::memory_order_acquire) || !card) return false;
        const auto found = active_card_dialogs_.find(card.get());
        if (found == active_card_dialogs_.end()) return false;
        const auto keep_alive = found->second.owner;
        const auto open = found->second.openPath;
        return open && open(path, activate);
    };
    card_comm_.editCard = [this](const cardwindow_ns::CardType type,
                                 const SummaryPtr& summ,
                                 const cardwindow_ns::CWXPathPtr& owner,
                                 const cardwindow_ns::CardPtr& card,
                                 const bool readOnly,
                                 lib::Action applying,
                                 lib::Action applied,
                                 lib::Action closed, const bool creationMode) {
        if (!prop_ || !summ || !card) return;
        if (const auto found = active_card_dialogs_.find(card.get());
            found != active_card_dialogs_.end()) {
            if (found->second.window && found->second.window->visible()) {
                if (services_.gui) {
                    services_.gui->show(found->second.window);
                    services_.gui->focus(found->second.window);
                }
                return;
            }
            active_card_dialogs_.erase(found);
        }
        const auto property_main_alive = std::weak_ptr<std::atomic_bool>(lifecycle_alive_);
        const auto property_applying = [property_main_alive, applying = std::move(applying)] {
            const auto flag = property_main_alive.lock();
            if (flag && flag->load(std::memory_order_acquire) && applying) applying();
        };
        const auto changed = [this](bool mark_dirty = true) {
            if (mark_dirty && summary_) summary_->changed();
            if (comm_) {
                comm_->refScenario.call(summary_);
                comm_->refreshToolBar();
            }
        };
        if (type == cardwindow_ns::CardType::Cast) {
            const auto value = std::dynamic_pointer_cast<cardwindow_ns::CastCard>(card);
            if (!value) return;
            castcarddialog::CastCardDialogContext context(*prop_);
            context.editor_commons = comm_;
            context.services = services_;
            context.common.skin = skin_.get();
            const auto main_alive = std::weak_ptr<std::atomic_bool>(lifecycle_alive_);
            const auto alive = [main_alive] {
                const auto flag = main_alive.lock();
                return flag && flag->load(std::memory_order_acquire);
            };
            context.resolve_source_skin = [this, alive](const Summary* source) {
                if (!alive() || !comm_ || !prop_) return commons::SkinPtr{};
                return commons::findSkin(*comm_, *prop_, source);
            };
            // CastCardDialog は、AbsDialog が appliedEvent
            // を通知する前にモデルを変更する。 そこでシナリオ全体を更新す
            // ると、元の CardPane が再構築されてダイアログへの弱
            // 参照の連携が失われ、 新規Castが CardPane::addCard
            // に届かなくなる。ツールバーの再描画は局所的に行い、
            // 下の連携処理が挿入を確定した後で、シナリオを通知する。
            context.refresh_toolbar = [this, alive] {
                if (alive() && comm_) comm_->refreshToolBar();
            };
            context.refresh_coupons = [this, alive] {
                if (alive() && comm_) comm_->refCoupons.call();
            };
            context.changed_card = [](cardwindow_ns::CastCard*) {};
            context.action_card_image = [this, alive](const types::ActionCardType type) {
                if (!alive()) return lib::ResourceId{};
                return images_ ? images_->actionCard(type) : lib::ResourceId{};
            };
            context.open_cwx_path = [this, alive](const StringView valuePath, const bool activate) {
                if (!alive()) return false;
                return openCWXPath(String(valuePath), activate);
            };
            auto dialog = std::make_shared<castcarddialog::CastCardDialog>(
                std::move(context), *prop_, window_, summ.get(), value, readOnly,
                summ->useCounter(), creationMode);
            if (!alive()) return;
            dialog->on_applying = property_applying;
            dialog->closeEvent.push_back(
                [this, alive, key = card.get(), closed = std::move(closed)] {
                    if (!alive()) return;
                    active_card_dialogs_.erase(key);
                    debug_cast_dialog_.reset();
                    if (closed) closed();
                });
            dialog->appliedEvent.push_back(
                [changed, alive, applied = std::move(applied)] {
                    if (!alive()) return;
                    // CardPane との連携処理は、ここで新規カードを
                    // 挿入する。 既存のカードではペインを更新するだけだが
                    // 、どちらもその後に確定済みのモデル状態を通知する。
                    if (applied) applied();
                    if (alive()) changed();
                });
            (void)dialog->open();
            if (!alive()) return;
            debug_cast_dialog_ = dialog;
            active_card_dialogs_[card.get()] = {
                dialog, dialog->dialog(), cardwindow_ns::CardType::Cast,
                [weak = std::weak_ptr<castcarddialog_ns::CastCardDialog>(dialog)] {
                    if (const auto value = weak.lock()) value->forceApply();
                }};
            dynamic_panes_.push_back(std::move(dialog));
            return;
        }
        if (type == cardwindow_ns::CardType::Info) {
            const auto value = std::dynamic_pointer_cast<cardwindow_ns::InfoCard>(card);
            if (!value) return;
            infocarddialog::Options options;
            options.creationMode = creationMode;
            options.graphics = services_.graphics.get();
            options.owner = window_;
            options.editor_props = prop_;
            options.editor_commons = comm_;
            options.skin_resolver = [this](const auto&, const Summary* source) {
                return comm_ ? comm_->findSkinFromHistory(source)
                             : std::shared_ptr<infocarddialog::Skin>{};
            };
            options.services = services_;
            auto dialog = std::make_shared<infocarddialog::InfoCardDialog>(
                prop_->parent(), summ, value, readOnly, std::move(options));
            dialog->on_applying = property_applying;
            dialog->on_applied = [changed, applied = std::move(applied)](
                                     const cardwindow_ns::InfoCard&) {
                changed();
                if (applied) applied();
            };
            dialog->on_closed = [this, key = card.get(), gui = services_.gui,
                                  weak = std::weak_ptr<lib::Dialog>(dialog),
                                  closed = std::move(closed)] {
                // このコールバックの間はダイアログを保持する。close()
                // の実行中に、 有効なダイアログのマップと動的所有者
                // の参照を外して、受け手を早く破棄しないようにする。
                const auto closing = weak.lock();
                if (gui && closing) gui->close(closing);
                releaseClosedCardDialog(key, closing);
                if (closed) closed();
            };
            (void)dialog->open();
            if (services_.gui) services_.gui->show(dialog);
            active_card_dialogs_[card.get()] = {
                dialog, dialog, cardwindow_ns::CardType::Info,
                [weak = std::weak_ptr<infocarddialog::InfoCardDialog>(dialog)] {
                    if (const auto value = weak.lock()) (void)value->apply();
                }};
            dynamic_panes_.push_back(std::move(dialog));
            return;
        }
        contentinitializer::ContentInitializerContext context(
            const_cast<::cwxeditor::cwx::props::CProps&>(prop_->parent()),
            prop_->var(), services_);
        context.editor_props = prop_;
        context.editor_commons = comm_;
        context.openCWXPath = [this, weak=std::weak_ptr<std::atomic_bool>(lifecycle_alive_)](
            const StringView path, const bool activate) {
            const auto life=weak.lock();
            return life && life->load(std::memory_order_acquire) &&
                openCWXPath(String(path),activate);
        };
        context.dialog_owner = window_;
        context.editBeastCard = [this](
            Summary* source, const auto& owner, const auto& card,
            const bool read_only, const StringView nested_path,
            const bool shell_activate, lib::Action applying, lib::Action applied,
            lib::Action closed) {
            return openBeastCardDialog(card_comm_, source, owner, card, read_only,
                nested_path, shell_activate, std::move(applying),
                std::move(applied), std::move(closed));
        };
        context.skinResolver = [this](const Summary* source, StringView, StringView) {
            return comm_ ? comm_->findSkinFromHistory(source) : skin_;
        };
        // MotionView はダイアログ構築中や選択変更時に
        // コマンド状態を更新する。 これはツールバーの表示更新で
        // あり、カードの編集ではない。changed を通すと、
        // スキル・アイテム・召喚獣の作成ダイアログを開くだけで
        // シナリオが変更済みになる。 D版のダイアログの寿命に
        // 合わせ、下の適用コールバックだけを確定の境界とする。
        context.refreshToolBar = [this] {
            if (comm_) comm_->refreshToolBar();
        };
        const auto parent = std::static_pointer_cast<lib::Container>(window_);
        auto show = [this, changed, property_applying, key = card.get(), type,
                     applied = std::move(applied),
                     closed = std::move(closed)](const auto& dialog) mutable {
            ::cwxeditor::cwx::editor::gui::dwt::eventdialog::DialogCallbacks callbacks;
            callbacks.applied = [changed, applied = std::move(applied)] {
                // Effect モデルの設定処理は、実際の変更をすでに
                // 通知する。 UI更新と CardPane の新規作成
                // 連携は残すが、適用時に無条件の変更通知を重ねない。
                changed(false);
                if (applied) applied();
            };
            callbacks.closed = [this, key, closed = std::move(closed)] {
                active_card_dialogs_.erase(key);
                if (closed) closed();
            };
            dialog->on_applying = property_applying;
            dialog->setCallbacks(std::move(callbacks));
            dialog->open();
            active_card_dialogs_[key] = {
                dialog, dialog->widget(), type,
                [weak = std::weak_ptr<std::remove_reference_t<decltype(*dialog)>>(dialog)] {
                    if (const auto value = weak.lock()) (void)value->apply();
                },
                [weak = std::weak_ptr<std::remove_reference_t<decltype(*dialog)>>(dialog)](
                    const StringView path, const bool activate) {
                    const auto current = weak.lock();
                    return current && current->openCWXPath(String(path), activate);
                }};
            dynamic_panes_.push_back(dialog);
        };
        if (type == cardwindow_ns::CardType::Skill) {
            const auto value = std::dynamic_pointer_cast<cardwindow_ns::SkillCard>(card);
            if (value) show(std::make_shared<effectcarddialog::SkillCardDialog>(
                context, summ, parent, owner, summ->useCounter(), value, readOnly, creationMode));
        } else if (type == cardwindow_ns::CardType::Item) {
            const auto value = std::dynamic_pointer_cast<cardwindow_ns::ItemCard>(card);
            if (value) show(std::make_shared<effectcarddialog::ItemCardDialog>(
                context, summ, parent, owner, summ->useCounter(), value, readOnly, creationMode));
        } else if (type == cardwindow_ns::CardType::Beast) {
            const auto value = std::dynamic_pointer_cast<cardwindow_ns::BeastCard>(card);
            if (value) show(std::make_shared<effectcarddialog::BeastCardDialog>(
                context, summ, parent, owner, summ->useCounter(), value, readOnly, creationMode));
        }
    };
    card_comm_.editEvent = [this](const cardwindow_ns::CardPtr& card, bool duplicate) {
        const auto effect = std::dynamic_pointer_cast<cardwindow_ns::EffectCard>(card);
        if (!effect || !comm_ || !prop_ || !summary_) return;
        (void)comm_->openUseEvents(
            *prop_, summary_.get(), effect, true,
            duplicate ? std::make_shared<commons::TopLevelPanel>() : nullptr);
    };
    card_comm_.importCards = [this](const std::vector<cardwindow_ns::CardPtr>& cards) {
        if (!summary_) return;
        for (const auto& card : cards) {
            const auto copy = card ? card->dup() : cardwindow_ns::CardPtr{};
            if (const auto cast = std::dynamic_pointer_cast<cardwindow_ns::CastCard>(copy))
                (void)summary_->add(cast);
            else if (const auto skill = std::dynamic_pointer_cast<cardwindow_ns::SkillCard>(copy))
                (void)summary_->add(skill);
            else if (const auto item = std::dynamic_pointer_cast<cardwindow_ns::ItemCard>(copy))
                (void)summary_->add(item);
            else if (const auto beast = std::dynamic_pointer_cast<cardwindow_ns::BeastCard>(copy))
                (void)summary_->add(beast);
            else if (const auto info = std::dynamic_pointer_cast<cardwindow_ns::InfoCard>(copy))
                (void)summary_->add(info);
        }
        if (comm_) comm_->refScenario.call(summary_);
    };
    card_comm_.doImport = [this](const SummaryPtr& target, const SummaryPtr& source,
                                 const std::vector<String>& paths) {
        if (comm_) comm_->doImport(target.get(), source.get(), paths);
    };
    const auto card_captions = std::make_shared<std::unordered_map<const cardwindow_ns::Card*, bool>>();
    card_comm_.cardImageData = [this, card_captions](const cardwindow_ns::CardPtr& card) {
        bool caption{};
        auto image = renderCardImage(summary_, card, false, &caption);
        (*card_captions)[card.get()] = caption;
        return image;
    };
    card_comm_.cardImageHasCaption = [card_captions](const cardwindow_ns::CardPtr& card) {
        const auto found = card_captions->find(card.get());
        return found != card_captions->end() && found->second;
    };
    card_comm_.cardLifeImageData = [this, card_captions](const cardwindow_ns::CardPtr& card) {
        bool caption{};
        auto image = renderCardImage(summary_, card, true, &caption);
        (*card_captions)[card.get()] = caption;
        return image;
    };
    card_comm_.forceFocus = [this](const WidgetPtr& widget, bool activate) {
        dutils::forceFocus(widget, activate, services_);
    };
    card_comm_.openCWXPath = [this](StringView path, bool activate) {
        return openCWXPath(String(path), activate);
    };
    card_comm_.openFilePath = [this, weak = std::weak_ptr<std::atomic_bool>(lifecycle_alive_)](
                                  StringView path, bool activate, bool onlySelect) {
        const auto alive = weak.lock();
        if (!alive || !alive->load(std::memory_order_acquire) || !comm_) return false;
        return comm_->openFilePath(String(path), activate, onlySelect);
    };
    card_comm_.setStatusLine = [this,
        alive = std::weak_ptr<std::atomic_bool>(lifecycle_alive_)](
        const WidgetPtr& owner, String value) {
        const auto current = alive.lock();
        if (!current || !current->load(std::memory_order_acquire)) return;
        publishPaneStatusLine(owner, std::move(value));
    };
    card_comm_.setTitle = [this](const WidgetPtr& widget, String text) {
        if (!widget) return;

        // 切り離された CardWindow はネイティブのウィンドウであり、ド
        // ッキングされたカードエディタは 同じコントロールを所有する DockingFolder
        // のタブで表される。D版の Commons.setTitle と同
        // 様に両方を扱う。 コールバックが空だと、初期の TopLevelPanel
        // のタイトルが正しくても、その後の更新を反映できない。
        if (const auto window = std::dynamic_pointer_cast<lib::Window>(widget))
            window->title = text;
        if (!docking_folder_) return;

        String key = docking_folder_->keyFromCtrl(widget);
        if (key.empty()) {
            for (const auto& [candidate, weakEditor] : dynamic_card_windows_) {
                const auto editor = weakEditor.lock();
                if (editor && editor->shell().get() == widget.get() &&
                    docking_folder_->control(candidate)) {
                    key = candidate;
                    break;
                }
            }
        }
        if (!key.empty() && docking_folder_->setTabText(key, std::move(text)) &&
            services_.gui) {
            refreshAllCompactSelectors();
            services_.gui->request_redraw(docking_folder_->component());
        }
    };
    card_comm_.close = [this](const WidgetPtr& widget) {
        if (!widget) return;
        // Cast の終了処理では、手札コントローラーへのすべての別参照も無効化する。 登
        // 録されているのは CardWindow::shell() ではなくクライアント
        // パネル。 ネイティブの画面の登録を外す間、コントローラーをローカルで保持する。
        std::shared_ptr<cardwindow_ns::CardWindow> retiring;
        for (const auto& [key, weak] : dynamic_card_windows_) {
            if (const auto current = weak.lock(); current && current->shell().get() == widget.get()) {
                retiring = current;
                break;
            }
        }
        if (retiring) {
            std::vector<String> keys;
            for (auto it = dynamic_card_windows_.begin(); it != dynamic_card_windows_.end();) {
                if (it->second.lock() == retiring) {
                    keys.push_back(it->first);
                    it = dynamic_card_windows_.erase(it);
                } else ++it;
            }
            for (const auto& key : keys) {
                if (docking_folder_) (void)docking_folder_->discard(key);
                dock_.close(key);
            }
            if (comm_) comm_->close(std::static_pointer_cast<lib::Container>(retiring->panel()));
            widget->set_visible(false);
            refreshAllCompactSelectors();
            return;
        }
        if (docking_folder_) {
            String key = docking_folder_->keyFromCtrl(widget);
            if (key.empty()) {
                for (const auto& [candidate, weakEditor] : dynamic_card_windows_) {
                    const auto editor = weakEditor.lock();
                    if (editor && editor->shell().get() == widget.get() &&
                        docking_folder_->control(candidate)) {
                        key = candidate;
                        break;
                    }
                }
            }
            if (!key.empty() && dockCloseCtrl(key)) return;
        }
        widget->set_visible(false);
    };
    card_comm_.changeVHSide = [this] { if (comm_) comm_->refScenario.call(summary_); };
    card_comm_.editSummary = edit_summary;
    card_comm_.storeSort = [this](const bool edit_mode, const bool summary_owner,
                                  const int column, const int direction) {
        auto& etc = prop_->var();
        if (edit_mode) {
            if (summary_owner) {
                etc.mainCardsSortColumn.value = column;
                etc.mainCardsSortDirection.value = direction;
            } else {
                etc.handCardsSortColumn.value = column;
                etc.handCardsSortDirection.value = direction;
            }
        } else if (summary_owner) {
            etc.importCardsSortColumn.value = column;
            etc.importCardsSortDirection.value = direction;
        } else {
            etc.importHandCardsSortColumn.value = column;
            etc.importHandCardsSortDirection.value = direction;
        }
        if (comm_) {
            if (edit_mode && summary_owner) comm_->refMainCardsSort.call(column, direction);
            else if (edit_mode) comm_->refHandCardsSort.call(column, direction);
            else if (summary_owner) comm_->refImportCardsSort.call(column, direction);
            else comm_->refImportHandCardsSort.call(column, direction);
        }
    };
    card_comm_.storeColumnWidth = [this, weak = std::weak_ptr<std::atomic_bool>(lifecycle_alive_)](
        const bool edit_mode, const bool summary_owner, const int column, const float width) {
        const auto alive = weak.lock();
        if (!alive || !alive->load(std::memory_order_acquire)) return;
        auto& etc = prop_->var();
        const int pixels = static_cast<int>(width);
        if (edit_mode && summary_owner) {
            switch (column) {
            case 0: etc.cardIdColumn.value = pixels; card_prop_.cardIdColumn = width; break;
            case 1: etc.cardNameColumn.value = pixels; card_prop_.cardNameColumn = width; break;
            case 2: etc.cardDescriptionColumn.value = pixels; card_prop_.cardDescriptionColumn = width; break;
            case 3: etc.cardCountColumn.value = pixels; card_prop_.cardCountColumn = width; break;
            case 4: etc.cardNumberColumn.value = pixels; card_prop_.cardNumberColumn = width; break;
            default: break;
            }
        }
        else if (edit_mode && !summary_owner) {
            switch (column) {
            case 0: etc.handCardIdColumn.value = pixels; card_prop_.handCardIdColumn = width; break;
            case 1: etc.handCardNameColumn.value = pixels; card_prop_.handCardNameColumn = width; break;
            case 2: etc.handCardDescriptionColumn.value = pixels; card_prop_.handCardDescriptionColumn = width; break;
            case 4: etc.handCardNumberColumn.value = pixels; card_prop_.handCardNumberColumn = width; break;
            default: break;
            }
        }
        else if (!edit_mode && summary_owner) {
            switch (column) {
            case 0: etc.importCardIdColumn.value = pixels; card_prop_.importCardIdColumn = width; break;
            case 1: etc.importCardNameColumn.value = pixels; card_prop_.importCardNameColumn = width; break;
            case 2: etc.importCardDescriptionColumn.value = pixels; card_prop_.importCardDescriptionColumn = width; break;
            case 3: etc.importCardCountColumn.value = pixels; card_prop_.importCardCountColumn = width; break;
            case 4: etc.importCardNumberColumn.value = pixels; card_prop_.importCardNumberColumn = width; break;
            default: break;
            }
        }
        else if (!edit_mode && !summary_owner) {
            switch (column) {
            case 0: etc.importHandCardIdColumn.value = pixels; card_prop_.importHandCardIdColumn = width; break;
            case 1: etc.importHandCardNameColumn.value = pixels; card_prop_.importHandCardNameColumn = width; break;
            case 2: etc.importHandCardDescriptionColumn.value = pixels; card_prop_.importHandCardDescriptionColumn = width; break;
            case 4: etc.importHandCardNumberColumn.value = pixels; card_prop_.importHandCardNumberColumn = width; break;
            default: break;
            }
        }
    };
    card_prop_.cardTabName = messages.cardTabName.value;
    card_prop_.handCardTabName = messages.handCardTabName.value;
    card_prop_.importSourceTabName = messages.importSourceTabName.value;
    card_prop_.areasTabName = messages.areasTabName.value;
    card_prop_.areaStatus = messages.areaStatus.value;
    card_prop_.areaIdLabel = messages.areaId.value;
    card_prop_.areaNameLabel = messages.areaName.value;
    card_prop_.areaCountLabel = all_mode ? "利用" : messages.areaCount.value;
    card_prop_.castName = messages.cwCast.value;
    card_prop_.skillName = messages.skill.value;
    card_prop_.itemName = messages.item.value;
    card_prop_.beastName = messages.beast.value;
    card_prop_.infoName = messages.info.value;
    card_prop_.idLabel = "ID";
    card_prop_.nameLabel = messages.cardName.value;
    card_prop_.levelLabel = messages.level.value;
    card_prop_.descriptionLabel = messages.cardDesc.value;
    card_prop_.useCountLabel = messages.cardCount.value;
    card_prop_.useNumberLabel = messages.useCount.value;
    if (prop_) {
        const auto& etc = prop_->var();
        card_prop_.cardIdColumn = all_mode ? 50.0F
            : static_cast<float>(valueOf(etc.cardIdColumn));
        // ALL の右上のカード表には、名前用にさらに30pxを確保する。
        card_prop_.cardNameColumn = all_mode ? 175.0F
            : static_cast<float>(valueOf(etc.cardNameColumn));
        card_prop_.cardNumberColumn = all_mode ? 45.0F
            : static_cast<float>(valueOf(etc.cardNumberColumn));
        card_prop_.cardDescriptionColumn = static_cast<float>(valueOf(etc.cardDescriptionColumn));
        card_prop_.cardCountColumn = all_mode ? 40.0F
            : static_cast<float>(valueOf(etc.cardCountColumn));
        card_prop_.handCardIdColumn = all_mode ? 50.0F
            : static_cast<float>(valueOf(etc.handCardIdColumn));
        card_prop_.handCardNameColumn = all_mode ? 175.0F
            : static_cast<float>(valueOf(etc.handCardNameColumn));
        card_prop_.handCardNumberColumn = all_mode ? 45.0F
            : static_cast<float>(valueOf(etc.handCardNumberColumn));
        card_prop_.handCardDescriptionColumn = static_cast<float>(valueOf(etc.handCardDescriptionColumn));
        card_prop_.importCardIdColumn = all_mode ? 50.0F
            : static_cast<float>(valueOf(etc.importCardIdColumn));
        card_prop_.importCardNameColumn = all_mode ? 175.0F
            : static_cast<float>(valueOf(etc.importCardNameColumn));
        card_prop_.importCardNumberColumn = all_mode ? 45.0F
            : static_cast<float>(valueOf(etc.importCardNumberColumn));
        card_prop_.importCardDescriptionColumn = static_cast<float>(valueOf(etc.importCardDescriptionColumn));
        card_prop_.importCardCountColumn = all_mode ? 40.0F
            : static_cast<float>(valueOf(etc.importCardCountColumn));
        card_prop_.importHandCardIdColumn = all_mode ? 50.0F
            : static_cast<float>(valueOf(etc.importHandCardIdColumn));
        card_prop_.importHandCardNameColumn = all_mode ? 175.0F
            : static_cast<float>(valueOf(etc.importHandCardNameColumn));
        card_prop_.importHandCardNumberColumn = all_mode ? 45.0F
            : static_cast<float>(valueOf(etc.importHandCardNumberColumn));
        card_prop_.importHandCardDescriptionColumn = static_cast<float>(valueOf(etc.importHandCardDescriptionColumn));
        card_prop_.mainCardsSortColumn = valueOf(etc.mainCardsSortColumn);
        card_prop_.mainCardsSortDirection = valueOf(etc.mainCardsSortDirection);
        card_prop_.handCardsSortColumn = valueOf(etc.handCardsSortColumn);
        card_prop_.handCardsSortDirection = valueOf(etc.handCardsSortDirection);
        card_prop_.importCardsSortColumn = valueOf(etc.importCardsSortColumn);
        card_prop_.importCardsSortDirection = valueOf(etc.importCardsSortDirection);
        card_prop_.importHandCardsSortColumn = valueOf(etc.importHandCardsSortColumn);
        card_prop_.importHandCardsSortDirection = valueOf(etc.importHandCardsSortDirection);
        card_prop_.cardsMarginX = valueOf(etc.cardsMarginX);
        card_prop_.cardsSpaceX = valueOf(etc.cardsSpaceX);
        card_prop_.cardsMarginY = valueOf(etc.cardsMarginY);
        card_prop_.cardsSpaceY = valueOf(etc.cardsSpaceY);
        card_prop_.cardsTitleSpace = valueOf(etc.cardsTitleSpace);
        card_prop_.cardsFocusLinePadding = valueOf(etc.cardsFocusLinePadding);
        card_prop_.cardsDefaultWrap = valueOf(etc.cardsDefaultWrap);
    }
    card_prop_.cardLifeStatus = messages.cardStatus.value;
    card_prop_.cardSelectedStatus = messages.cardStatusSelOne.value;
    card_prop_.cardListStatus = messages.cardStatusSelMulti.value;
    card_prop_.cardTableStatus = messages.cardStatusSelMulti.value;
    const auto cardSize = prop_->looks().cardSize();
    const auto cardInsets = prop_->looks().cardInsets();
    const auto castInsets = prop_->looks().castCardInsets();
    card_prop_.cardWidth = std::max(1, static_cast<int>(cardSize.width) +
                                      cardInsets.e + cardInsets.w);
    card_prop_.cardHeight = std::max(1, static_cast<int>(cardSize.height) +
                                       cardInsets.n + cardInsets.s);
    card_prop_.castCardWidth = std::max(1, static_cast<int>(cardSize.width) +
                                          castInsets.e + castInsets.w);
    card_prop_.castCardHeight = std::max(1, static_cast<int>(cardSize.height) +
                                           castInsets.n + castInsets.s);
    card_prop_.castImage = menuImage(MenuID::CastView);
    card_prop_.skillImage = menuImage(MenuID::SkillView);
    card_prop_.itemImage = menuImage(MenuID::ItemView);
    card_prop_.beastImage = menuImage(MenuID::BeastView);
    card_prop_.infoImage = menuImage(MenuID::InfoView);
    // D版と同様、手札と取り込み用 CardWindow のホストタブに、これらのメニュー画像を使う。
    card_prop_.openHandImage = menuImage(MenuID::OpenHand);
    card_prop_.importSourceImage = menuImage(MenuID::OpenImportSource);
    if (images_) {
        card_prop_.handImage = images_->hand();
        card_prop_.handEmptyImage = images_->handEmpty();
        card_prop_.eventTreeImage = images_->eventTree();
        card_prop_.eventTreeEmptyImage = images_->eventTreeEmpty();
    }
    card_prop_.tableViewImage = menuImage(MenuID::ShowCardDetail);
    card_prop_.commentImage = menuImage(MenuID::Comment);
    card_prop_.cardLife = prop_ && valueOf(prop_->var().cardLife);
    card_prop_.cardDetails = prop_ && valueOf(prop_->var().cardDetails);
    card_prop_.showHandMark = prop_ && valueOf(prop_->var().showHandMark);
    card_prop_.showHandMarkAlways = prop_ && valueOf(prop_->var().showHandMarkAlways);
    card_prop_.showEventTreeMark = prop_ && valueOf(prop_->var().showEventTreeMark);
    card_prop_.showEventTreeMarkAlways = prop_ && valueOf(prop_->var().showEventTreeMarkAlways);
    card_prop_.ignoreEmptyStart = prop_ && valueOf(prop_->var().ignoreEmptyStart);
    card_prop_.clickIsOpenEvent = prop_ && valueOf(prop_->var().clickIsOpenEvent);
    card_prop_.readOnly = summary_ && !summary_->readOnlyPath().empty();
    card_prop_.undoMax = static_cast<std::size_t>(
        std::max<std::int32_t>(0, valueOf(prop_->var().undoMaxMainView)));
    // 取り込み元の表タブにも主エディタと同じ完全な AreaTable を
    // 使う。 D版と同様、三列の一覧の上にディレクトリツリーを表示する。
    card_prop_.areaTableContext = &area_table_context_;
    card_prop_.editorProps = prop_;
    cast_window_ = std::make_shared<cardwindow_ns::CardWindow>(
        card_comm_, card_prop_, cardwindow_ns::CardWindowKind::Cast,
        typed_host_);
    skill_window_ = std::make_shared<cardwindow_ns::CardWindow>(
        card_comm_, card_prop_, cardwindow_ns::CardWindowKind::Skill,
        typed_host_);
    item_window_ = std::make_shared<cardwindow_ns::CardWindow>(
        card_comm_, card_prop_, cardwindow_ns::CardWindowKind::Item,
        typed_host_);
    beast_window_ = std::make_shared<cardwindow_ns::CardWindow>(
        card_comm_, card_prop_, cardwindow_ns::CardWindowKind::Beast,
        typed_host_);
    info_window_ = std::make_shared<cardwindow_ns::CardWindow>(
        card_comm_, card_prop_, cardwindow_ns::CardWindowKind::Info,
        typed_host_);

    name_comm_.services = services_;
    name_comm_.clipboard = services_.clipboard;
    name_comm_.messages.idName = messages.idName.value;
    name_comm_.messages.idCount = all_mode ? "利用" : messages.idCount.value;
    name_comm_.messages.coupon = messages.couponTabName.value;
    name_comm_.messages.gossip = messages.gossipTabName.value;
    name_comm_.messages.completeStamp = messages.completeStampTabName.value;
    name_comm_.messages.keyCode = messages.keyCodeTabName.value;
    name_comm_.messages.cellName = messages.cellNameTabName.value;
    name_comm_.messages.cardGroup = messages.cardGroupTabName.value;
    name_comm_.messages.idStatus = messages.idStatus.value;
    name_comm_.messages.idStatusSelected = messages.idStatusSel.value;
    name_comm_.images.couponNormal = images_ ? images_->couponNormal() : 0U;
    name_comm_.images.gossip = menuImage(MenuID::GossipView);
    name_comm_.images.completeStamp = menuImage(MenuID::CompleteStampView);
    name_comm_.images.keyCode = menuImage(MenuID::KeyCodeView);
    name_comm_.images.backs = menuImage(MenuID::CellNameView);
    name_comm_.images.cards = menuImage(MenuID::CardGroupView);
    name_comm_.images.couponView = menuImage(MenuID::CouponView);
    name_comm_.images.gossipView = menuImage(MenuID::GossipView);
    name_comm_.images.completeStampView = menuImage(MenuID::CompleteStampView);
    name_comm_.images.keyCodeView = menuImage(MenuID::KeyCodeView);
    name_comm_.images.cellNameView = menuImage(MenuID::CellNameView);
    name_comm_.images.cardGroupView = menuImage(MenuID::CardGroupView);
    const auto name_host_live = [weak=std::weak_ptr<std::atomic_bool>(lifecycle_alive_)] {
        const auto life=weak.lock(); return life && life->load(std::memory_order_acquire);
    };
    name_comm_.menuText = [this,name_host_live](const MenuID id) { return name_host_live() ? menuText(id) : String{}; };
    name_comm_.menuImage = [this,name_host_live](const MenuID id) { return name_host_live() ? menuImage(id) : lib::ResourceId{}; };
    name_comm_.quickEditProvider = [this, name_host_live] {
        return name_host_live() && prop_ && prop_->var().editTriggerType.value ==
            static_cast<std::int32_t>(::cwxeditor::cwx::structs::EditTrigger::Quick);
    };
    name_comm_.undoMaxReplaceProvider = [this, name_host_live] {
        return name_host_live() ? static_cast<std::size_t>(
            std::max<std::int32_t>(0, valueOf(prop_->var().undoMaxReplace))) : 0U;
    };
    name_comm_.copyConverted = [this, name_host_live](const std::vector<String>& values,
                                                      const StringView kind) {
        if (!name_host_live()) return false;
        const auto clipboard = services_.clipboard;
        const auto xml_copy = prop_->var().xmlCopy.value;
        return namewindow_ns::writeConvertedNames(clipboard, xml_copy, values, kind);
    };
    name_comm_.convertCoupon = [this](const StringView value,
                                      const namewindow_ns::CouponType type,
                                      const bool ignore_system) {
        return prop_->sys().convCoupon(value, type, ignore_system);
    };
    name_comm_.convertKeyCode = [this](const StringView value,
                                       const namewindow_ns::FKCKind kind) {
        return prop_->sys().convFireKeyCode(value, kind);
    };
    name_comm_.refreshToolBar = [this,name_host_live] { if (name_host_live() && comm_) comm_->refreshToolBar(); };
    name_comm_.replText = [this] { refreshTitle(); };
    name_comm_.refUseCount = [this] { if (comm_) comm_->refUseCount.call(); };
    name_comm_.refIds = [this] { if (comm_) comm_->refCardGroups.call(); };
    name_comm_.refNameID = [this](const namewindow_ns::NameIDKind kind) {
        if (!comm_) return;
        switch (kind) {
        case namewindow_ns::NameIDKind::Coupon: comm_->refCoupons.call(); break;
        case namewindow_ns::NameIDKind::Gossip: comm_->refGossips.call(); break;
        case namewindow_ns::NameIDKind::CompleteStamp: comm_->refCompleteStamps.call(); break;
        case namewindow_ns::NameIDKind::KeyCode: comm_->refKeyCodes.call(); break;
        case namewindow_ns::NameIDKind::CellName: comm_->refCellNames.call(); break;
        case namewindow_ns::NameIDKind::CardGroup: comm_->refCardGroups.call(); break;
        }
    };
    name_comm_.setStatusLine = [this,name_host_live](lib::Widget*, StringView value) {
        if(name_host_live())setStatusLine(String(value));
    };
    name_comm_.replaceID = [this,name_host_live](const namewindow_ns::NameIDKind source_kind,
                                  StringView path, const bool start) {
        if (!name_host_live() || !summary_ || path.empty()) return;
        replacedialog::IDKind kind = replacedialog::IDKind::Coupon;
        switch (source_kind) {
        case namewindow_ns::NameIDKind::Coupon:
            kind = replacedialog::IDKind::Coupon;
            break;
        case namewindow_ns::NameIDKind::Gossip:
            kind = replacedialog::IDKind::Gossip;
            break;
        case namewindow_ns::NameIDKind::CompleteStamp:
            kind = replacedialog::IDKind::CompleteStamp;
            break;
        case namewindow_ns::NameIDKind::KeyCode:
            kind = replacedialog::IDKind::KeyCode;
            break;
        case namewindow_ns::NameIDKind::CellName:
            kind = replacedialog::IDKind::CellName;
            break;
        case namewindow_ns::NameIDKind::CardGroup:
            kind = replacedialog::IDKind::CardGroup;
            break;
        }
        configureReplaceDialog([from=String(path),kind,start](auto& dialog) {
            dialog.replaceID(from,kind,start);
        });
    };
    name_comm_.logicalSort = prop_->var().logicalSort.value;
    name_comm_.logicalSortProvider = [this, name_host_live] {
        return name_host_live() && prop_ && prop_->var().logicalSort.value;
    };
    name_comm_.undoMaxEvent = static_cast<std::size_t>(
        std::max<std::int32_t>(0, valueOf(prop_->var().undoMaxEvent)));
    name_comm_.undoMaxReplace = static_cast<std::size_t>(
        std::max<std::int32_t>(0, valueOf(prop_->var().undoMaxReplace)));
    name_comm_.nameColumnWidth = all_mode ? 295.0F
        : static_cast<float>(prop_->var().idNameColumn.value);
    // ALL仕様: 左上の各表で「利用数」を「利用」にそろえ、利用列を22px縮める。
    name_comm_.useCountColumnWidth = all_mode ? 33.0F
        : static_cast<float>(prop_->var().idCountColumn.value);
    // ALL 独自の固定幅の方針を維持し、単独版はD版の Props に従う。
    if (!all_mode) {
        name_comm_.loadColumnWidths = [this, name_host_live] {
            if (!name_host_live()) return std::pair<float, float>{330.0F, 60.0F};
            return std::pair<float, float>{
                static_cast<float>(prop_->var().idNameColumn.value),
                static_cast<float>(prop_->var().idCountColumn.value)};
        };
        name_comm_.storeColumnWidth = [this, name_host_live](std::size_t column, float width) {
            if (!name_host_live() || !std::isfinite(width) || width < 0.0F ||
                width > static_cast<float>(std::numeric_limits<std::int32_t>::max() - 1024)) return;
            const auto value = static_cast<std::int32_t>(std::lround(width));
            if (column == 0U) prop_->var().idNameColumn.value = value;
            else if (column == 1U) prop_->var().idCountColumn.value = value;
        };
    }
    name_comm_.loadSort = [this](const namewindow_ns::NameIDKind kind) {
        const auto& etc = prop_->var();
        std::int32_t column = 0;
        std::int32_t direction = static_cast<std::int32_t>(structs::SortDir::Up);
        switch (kind) {
        case namewindow_ns::NameIDKind::Coupon:
            column = valueOf(etc.couponSortColumn);
            direction = valueOf(etc.couponSortDirection);
            break;
        case namewindow_ns::NameIDKind::Gossip:
            column = valueOf(etc.gossipSortColumn);
            direction = valueOf(etc.gossipSortDirection);
            break;
        case namewindow_ns::NameIDKind::CompleteStamp:
            column = valueOf(etc.completeStampSortColumn);
            direction = valueOf(etc.completeStampSortDirection);
            break;
        case namewindow_ns::NameIDKind::KeyCode:
            column = valueOf(etc.keyCodeSortColumn);
            direction = valueOf(etc.keyCodeSortDirection);
            break;
        case namewindow_ns::NameIDKind::CellName:
            column = valueOf(etc.cellNameSortColumn);
            direction = valueOf(etc.cellNameSortDirection);
            break;
        case namewindow_ns::NameIDKind::CardGroup:
            column = valueOf(etc.cardGroupSortColumn);
            direction = valueOf(etc.cardGroupSortDirection);
            break;
        }
        return std::pair{
            column == 1 ? namewindow_ns::SortColumn::UseCount
                        : namewindow_ns::SortColumn::Name,
            direction == static_cast<std::int32_t>(structs::SortDir::Down)
                ? namewindow_ns::SortDirection::Down
                : namewindow_ns::SortDirection::Up};
    };
    name_comm_.storeTypedSort = [this](const namewindow_ns::NameIDKind kind,
                                       const namewindow_ns::SortColumn column,
                                       const namewindow_ns::SortDirection direction) {
        auto& etc = prop_->var();
        const auto column_value = column == namewindow_ns::SortColumn::UseCount ? 1 : 0;
        const auto direction_value = direction == namewindow_ns::SortDirection::Down
            ? static_cast<std::int32_t>(structs::SortDir::Down)
            : static_cast<std::int32_t>(structs::SortDir::Up);
        switch (kind) {
        case namewindow_ns::NameIDKind::Coupon:
            etc.couponSortColumn.value = column_value;
            etc.couponSortDirection.value = direction_value;
            break;
        case namewindow_ns::NameIDKind::Gossip:
            etc.gossipSortColumn.value = column_value;
            etc.gossipSortDirection.value = direction_value;
            break;
        case namewindow_ns::NameIDKind::CompleteStamp:
            etc.completeStampSortColumn.value = column_value;
            etc.completeStampSortDirection.value = direction_value;
            break;
        case namewindow_ns::NameIDKind::KeyCode:
            etc.keyCodeSortColumn.value = column_value;
            etc.keyCodeSortDirection.value = direction_value;
            break;
        case namewindow_ns::NameIDKind::CellName:
            etc.cellNameSortColumn.value = column_value;
            etc.cellNameSortDirection.value = direction_value;
            break;
        case namewindow_ns::NameIDKind::CardGroup:
            etc.cardGroupSortColumn.value = column_value;
            etc.cardGroupSortDirection.value = direction_value;
            break;
        }
    };

    directory_prop_.appPath = app_path_;
    directory_prop_.materialPath = "Material";
    directory_prop_.editorProps = prop_;
    directory_prop_.maskCardImagePreview = prop_->var().maskCardImagePreview.value;
    // D版と同様、この設定は復号した寸法が現在のスキンのカードサイズに一致する画
    // 像に適用する。 DirectoryWindow は .bmp 拡張子だけで
    // は判定できないため、 現在のシナリオのスキンに結び付いた判定処理を使う。
    directory_prop_.isCard = [this](const StringView path) {
        return skin_ && skin_->isCardImage(path, false, false);
    };
    directory_prop_.isMaterial = [this](StringView path) {
        return skin_ && skin_->isMaterial(path);
    };
    directory_prop_.notUsedFiles = [this] {
        return summary_ && skin_
            ? summary_->notUsedFiles(*skin_, valueOf(prop_->var().ignorePaths),
                                     directory_prop_.logicalSort)
            : std::vector<String>{};
    };
    directory_prop_.traceChanges = !all_mode && prop_->var().traceDirectories.value;
    directory_prop_.fileSync = &sync_;
    directory_prop_.canTraceRefresh = [this] { return !in_saving_; };
    directory_prop_.autoUpdateJpy1File = prop_->var().autoUpdateJpy1File.value;
    directory_prop_.logicalSort = true;
    // 単独エディタのファイルペインは現在の設定とスキンを使う
    // 。 ALL は既存のコンパクトなペイン構成を維持する。
    if (!all_mode) {
        directory_prop_.logicalSort = prop_->var().logicalSort.value;
        directory_prop_.ignorePaths = valueOf(prop_->var().ignorePaths);
        directory_prop_.isBGM = [this](StringView path) { return skin_ && skin_->isBGM(path); };
        directory_prop_.isSE = [this](StringView path) { return skin_ && skin_->isSE(path); };
    }
    directory_prop_.directorySashVertical = prop_->var().directorySashV.value;
    directory_prop_.directorySashLeft = static_cast<float>(prop_->var().directorySashL.value);
    directory_prop_.directorySashRight = static_cast<float>(prop_->var().directorySashR.value);
    directory_prop_.fileNameColumn = all_mode ? 230.0F
        : static_cast<float>(prop_->var().fileNameColumn.value);
    // ALL のコンパクトなファイルペインは、拡張子の幅を意図的に8px狭くする。
    directory_prop_.fileExtColumn = all_mode ? 72.0F
        : static_cast<float>(prop_->var().fileExtColumn.value);
    directory_prop_.fileCountColumn = all_mode ? 28.0F
        : static_cast<float>(prop_->var().fileCountColumn.value);
    const std::weak_ptr<std::atomic_bool> directory_host = lifecycle_alive_;
    directory_prop_.storeFileColumnWidth = [this, directory_host](
        const std::size_t index, const float width) {
        const auto alive = directory_host.lock();
        if (!alive || !alive->load(std::memory_order_acquire) ||
            !std::isfinite(width) || width < 0.0F) return;
        if (index == 0U) directory_prop_.fileNameColumn = width;
        else if (index == 1U) directory_prop_.fileExtColumn = width;
        else if (index == 2U) directory_prop_.fileCountColumn = width;
    };
    directory_prop_.archivePath = prop_->var().archivePath.value;
    directory_prop_.selectedArchiveFilter = prop_->var().selectedArchiveFilter.value;
    directory_prop_.filesSortColumn = prop_->var().filesSortColumn.value;
    directory_prop_.filesSortDirection = prop_->var().filesSortDirection.value;
    directory_prop_.images.window = menuImage(MenuID::FileView);
    directory_prop_.messages.title = messages.dirTabName.value;
    directory_prop_.messages.fileName = messages.fileName.value;
    directory_prop_.messages.fileExt = messages.fileExt.value;
    directory_prop_.messages.fileCount = all_mode ? "利用" : messages.fileCount.value;
    directory_prop_.messages.dirStatus = messages.dirStatus.value;
    directory_prop_.messages.dirStatusSelected = messages.dirStatusSel.value;
    if (images_) {
        directory_prop_.images.warning = images_->warning();
        directory_prop_.images.folder = images_->folder();
        directory_prop_.images.cards = images_->cards();
        directory_prop_.images.backs = images_->backs();
        directory_prop_.images.bgm = images_->bgm();
        directory_prop_.images.se = images_->sound();
        directory_prop_.images.text = images_->text();
        directory_prop_.images.unknown = images_->unknown();
        directory_prop_.images.skeletonFolder = images_->skeleton(directory_prop_.images.folder);
        directory_prop_.images.skeletonCards = images_->skeleton(directory_prop_.images.cards);
        directory_prop_.images.skeletonBacks = images_->skeleton(directory_prop_.images.backs);
        directory_prop_.images.skeletonBgm = images_->skeleton(directory_prop_.images.bgm);
        directory_prop_.images.skeletonSe = images_->skeleton(directory_prop_.images.se);
        directory_prop_.images.skeletonText = images_->skeleton(directory_prop_.images.text);
        directory_prop_.images.skeletonUnknown = images_->skeleton(directory_prop_.images.unknown);
    }
    directory_prop_.warnings = [this, weak = std::weak_ptr<std::atomic_bool>(lifecycle_alive_)](StringView full_path) -> std::vector<String> {
        const auto alive = weak.lock();
        if (!alive || !alive->load(std::memory_order_acquire)) return {};
        const auto file = std::filesystem::u8path(String(full_path));
        const auto base = file.filename().u8string();
        const String name(reinterpret_cast<const char*>(base.data()), base.size());
        auto result = ::cwxeditor::cwx::warning::sjisWarnings(
            prop_->parent(), summary_, name, StringView{});
        std::error_code error;
        if (std::filesystem::is_regular_file(file, error) && !error) {
            const auto extension = ::cwxeditor::cwx::warning::fileExtensionWarnings(
                prop_->parent(), full_path);
            result.insert(result.end(), extension.begin(), extension.end());
        }
        return result;
    };
    directory_prop_.openDirectoryWindow = [this] { openDirWindow(); };
    directory_prop_.saveScenario = [this] { return save(window_); };
    directory_prop_.createArchive = [this](StringView path, StringView filter) {
        if (!summary_ || in_saving_) return false;
        SaveActivity saving(in_saving_);
        try {
            const auto option = createSaveOpt(summary_->legacy(), false);
            option.runIo([sync = &sync_] { sync->sync(); });
            if (utils::incmp(filter, ".cab") == 0) {
                summary_->createCab(path, valueOf(prop_->var().ignorePaths), option);
            } else {
                // D版と同様、通常のZIPはシステムの文字コード、WSNはUTF-8
                // で書く。 ディレクトリビューは選択と通知だけを担当する。
                summary_->createZip(path, valueOf(prop_->var().ignorePaths),
                                    utils::incmp(filter, ".wsn") != 0, sync_, option);
            }
            return true;
        } catch (const std::exception& error) {
            setStatusLine("アーカイブを作成できません: " + String(error.what()));
            return false;
        }
    };
    directory_prop_.replacePath = [this](StringView path, const bool start) {
        if (comm_) comm_->replacePath(String(path), start);
    };
    directory_prop_.refPath = [this](const StringView old_path,
                                     const StringView new_path,
                                     const bool directory) {
        if (comm_) comm_->refPath.call(String(old_path), String(new_path), directory);
    };
    directory_prop_.deletePathReferences = [this] {
        if (comm_) comm_->delPaths.call();
    };
    directory_prop_.refPaths = [this](StringView parent) {
        if (comm_) comm_->refPaths.call(String(parent));
    };
    directory_prop_.statusMessage = [this](StringView value) { setStatusLine(String(value)); };
    directory_prop_.openFolder = [this](StringView path) {
        if (services_.shell) {
            std::string error;
            (void)services_.shell->open_path(pathFromUtf8(path), error);
            if (!error.empty()) setStatusLine(error);
        }
    };
    if (services_.shell) {
        directory_prop_.launch = [this, weak = std::weak_ptr<std::atomic_bool>(lifecycle_alive_)](StringView path) {
            const auto alive = weak.lock();
            if (!alive || !alive->load(std::memory_order_acquire)) return;
            // D版と同様、素材ファイルは実行コマンドではなくOS
            // の関連付けで開く。 外部ツールの起動とは分ける。
            const auto shell = services_.shell;
            std::string error;
            const bool opened = shell->open_path(pathFromUtf8(path), error);
            if (!opened && alive->load(std::memory_order_acquire) && !error.empty())
                setStatusLine(error);
        };
    }
    directory_window_ = std::make_shared<directorywindow_ns::DirectoryWindow>(
        name_comm_, directory_prop_, typed_host_.get());
    coupon_window_ = std::make_shared<namewindow_ns::CouponWindow>(
        name_comm_, window_.get(), typed_host_.get(), nullptr, false);
    gossip_window_ = std::make_shared<namewindow_ns::GossipWindow>(
        name_comm_, window_.get(), typed_host_.get(), nullptr, false);
    complete_stamp_window_ = std::make_shared<namewindow_ns::CompleteStampWindow>(
        name_comm_, window_.get(), typed_host_.get(), nullptr, false);
    key_code_window_ = std::make_shared<namewindow_ns::KeyCodeWindow>(
        name_comm_, window_.get(), typed_host_.get(), nullptr, false);
    cell_name_window_ = std::make_shared<namewindow_ns::CellNameWindow>(
        name_comm_, window_.get(), typed_host_.get(), nullptr, false);
    // すべてのホストで、同じ六つの型付き関連名の経路を提供する。 画面部品の配置が
    // 異なっても、共通エディタから CardGroup の選択機能を取り除かない。
    card_group_window_ = std::make_shared<namewindow_ns::CardGroupWindow>(
        name_comm_, window_.get(), typed_host_.get(), nullptr, false);
    comm_->pathReplacement([this,name_host_live](String from,const bool start) {
        if (!name_host_live()) return;
        configureReplaceDialog([from=std::move(from),start](auto& dialog) {
            dialog.replacePath(from,start);
        });
    });
    comm_->idReplacement([this,name_host_live](commons::Commons::ReplaceIDRequest request) {
        if (name_host_live()) configureReplaceDialog(std::move(request));
    });
    comm_->fileSelection([this,name_host_live](String path,const bool deselect) {
        if(!name_host_live())return false;
        const auto directory=directory_window_;
        return directory && directory->select(std::move(path),deselect);
    });
    comm_->nameSelection([this,name_host_live](String key,std::vector<String> ids) {
        if(!name_host_live())return;
        const auto select=[&](const auto& member) {
            const auto window=member;
            if(window)window->view().selectStrings(ids);
        };
        if(key=="coupon")select(coupon_window_);
        else if(key=="gossip")select(gossip_window_);
        else if(key=="completeStamp")select(complete_stamp_window_);
        else if(key=="keyCode")select(key_code_window_);
        else if(key=="cellName")select(cell_name_window_);
        else if(key=="cardGroup")select(card_group_window_);
    });
    const auto bind_name_signal = [this](auto& signal, auto handler) {
        const auto token = signal.add(std::move(handler));
        auto* const source = &signal;
        name_signal_cleanup_.emplace_back([source, token] {
            source->remove(token);
        });
    };
    // 一つの共通の連携処理から、有効な各カードペインへ外観の変更を伝える。
    const auto appearance_bus = card_comm_.appearanceEvent;
    const auto refresh_appearance = [appearance_bus] {
        if (appearance_bus) appearance_bus->call();
    };
    bind_name_signal(comm_->refShowCardListTitle, refresh_appearance);
    bind_name_signal(comm_->refCardImageStatus, refresh_appearance);
    const auto sort_bus = card_comm_.refSortParamsEvent;
    const auto relay_sort = [sort_bus](bool edit, bool summary) {
        return [sort_bus, edit, summary](int column, int direction) {
            if (sort_bus && column >= 0 && column <= 4)
                sort_bus->call(edit, summary, static_cast<::cwxeditor::cwx::editor::gui::dwt::cardpane::CardTableColumn>(column), direction);
        };
    };
    bind_name_signal(comm_->refMainCardsSort, relay_sort(true, true));
    bind_name_signal(comm_->refHandCardsSort, relay_sort(true, false));
    bind_name_signal(comm_->refImportCardsSort, relay_sort(false, true));
    bind_name_signal(comm_->refImportHandCardsSort, relay_sort(false, false));
    const auto card_bus = card_comm_.cardChangedEvent;
    const auto refresh_card = [card_bus](const auto& card) {
        if (card_bus) card_bus->call(card, false);
    };
    const auto delete_card = [card_bus](const auto& card) {
        if (card_bus) card_bus->call(card, true);
    };
    const auto delete_owned_card = [card_bus](const auto& owner, const auto& card) {
        // D版の delCardCallback と同様、Summary 配下の元カードを削除する場合だ
        // けリンクを無効化する。 Cast の手札の削除で、同じIDを持つ無関係なリンクを無効化しない。
        if (card_bus && std::dynamic_pointer_cast<summary::Summary>(owner))
            card_bus->call(card, true);
    };
    bind_name_signal(comm_->refCast, refresh_card);
    bind_name_signal(comm_->refSkill, refresh_card);
    bind_name_signal(comm_->refItem, refresh_card);
    bind_name_signal(comm_->refBeast, refresh_card);
    bind_name_signal(comm_->refInfo, refresh_card);
    bind_name_signal(comm_->delCast, delete_card);
    bind_name_signal(comm_->delSkill, delete_owned_card);
    bind_name_signal(comm_->delItem, delete_owned_card);
    bind_name_signal(comm_->delBeast, delete_owned_card);
    bind_name_signal(comm_->delInfo, delete_card);
    const auto changed_names = [this] {
        if (coupon_window_) coupon_window_->changed();
        if (gossip_window_) gossip_window_->changed();
        if (complete_stamp_window_) complete_stamp_window_->changed();
        if (key_code_window_) key_code_window_->changed();
        if (cell_name_window_) cell_name_window_->changed();
        if (card_group_window_) card_group_window_->changed();
    };
    const auto ref_all_names = [this] {
        if (coupon_window_) coupon_window_->refID();
        if (gossip_window_) gossip_window_->refID();
        if (complete_stamp_window_) complete_stamp_window_->refID();
        if (key_code_window_) key_code_window_->refID();
        if (cell_name_window_) cell_name_window_->refID();
        if (card_group_window_) card_group_window_->refID();
    };
    bind_name_signal(comm_->changed, changed_names);
    bind_name_signal(comm_->replText, ref_all_names);
    bind_name_signal(comm_->replText, [this, name_host_live] {
        if (!name_host_live()) return;
        std::vector<std::shared_ptr<cardwindow_ns::CardWindow>> hosts;
        const auto append = [&hosts](const auto& host) {
            if (host && std::ranges::find(hosts, host) == hosts.end()) hosts.push_back(host);
        };
        append(cast_window_); append(skill_window_); append(item_window_);
        append(beast_window_); append(info_window_);
        for (const auto& [key, weak] : dynamic_card_windows_) append(weak.lock());
        for (const auto& host : hosts) {
            if (!name_host_live()) return;
            const std::array panes{host->paneCast(), host->paneSkill(), host->paneItem(),
                                   host->paneBeast(), host->paneInfo()};
            for (const auto& pane : panes) {
                if (!name_host_live()) return;
                if (pane) {
                    const auto canonical = pane->canonical();
                    if (canonical) canonical->refreshCommentTitles();
                }
            }
        }
    });
    bind_name_signal(comm_->refPaths, [this, name_host_live](const String& parent) {
        if (!name_host_live()) return;
        const auto directory = directory_window_;
        if (directory) directory->refPaths(nullptr, parent);
    });
    bind_name_signal(comm_->refUseCount, [this, name_host_live] {
        if (!name_host_live()) return;
        flag_comm_.refUseCount.call();
        if (!name_host_live()) return;
        const auto directory = directory_window_;
        if (directory) directory->refreshUseCount();
        if (!name_host_live()) return;
        if (coupon_window_) coupon_window_->refUseCount();
        if (gossip_window_) gossip_window_->refUseCount();
        if (complete_stamp_window_) complete_stamp_window_->refUseCount();
        if (key_code_window_) key_code_window_->refUseCount();
        if (cell_name_window_) cell_name_window_->refUseCount();
        if (card_group_window_) card_group_window_->refUseCount();
    });
    bind_name_signal(comm_->refUseCount, [this, name_host_live] {
        if (!name_host_live()) return;
        // D版は再構築した各 CardPane を購読させる。C++版では MainWindow が
        // 一つのシグナルを持つため、 同じ固定・動的の各 CardWindow へ通知を振り分ける。
        std::vector<std::shared_ptr<cardwindow_ns::CardWindow>> hosts;
        const auto append = [&hosts](const auto& host) {
            if (host && std::ranges::find(hosts, host) == hosts.end())
                hosts.push_back(host);
        };
        append(cast_window_); append(skill_window_); append(item_window_);
        append(beast_window_); append(info_window_);
        for (const auto& [key, weak] : dynamic_card_windows_)
            append(weak.lock());
        for (const auto& host : hosts) {
            if (!name_host_live()) return;
            host->refreshUseCount();
        }
    });
    const auto refresh_history_settings = [this] {
        if (!prop_) return;
        const auto& v = prop_->var();
        const auto maximum = static_cast<std::size_t>(std::max(0, v.historyMax.value));
        const auto undoMaximum = static_cast<std::size_t>(std::max(0, v.undoMaxEtc.value));
        for (const auto& dialog : {scenario_history_dialog_, import_history_dialog_})
            if (dialog) dialog->update_live_settings(maximum,
                v.removeScenarioHistoryNonExistingWithPriority.value, undoMaximum);
        if (executed_party_history_dialog_)
            executed_party_history_dialog_->update_live_settings(v.executedPartiesMax.value,
                v.removePartyHistoryNonExistingWithPriority.value, undoMaximum, v.executedParties.value.size());
    };
    bind_name_signal(comm_->refUndoMax, refresh_history_settings);
    bind_name_signal(comm_->refHistoryMax, refresh_history_settings);
    bind_name_signal(comm_->refPartyHistoryMax, refresh_history_settings);
    bind_name_signal(comm_->saved, refresh_history_settings);
    bind_name_signal(comm_->refUndoMax, [this] {
        const auto main_limit = static_cast<std::size_t>(
            std::max<std::int32_t>(0, valueOf(prop_->var().undoMaxMainView)));
        flag_props_.etc.undoMaxMainView = main_limit;
        flag_props_.etc.undoMaxEtc = flag_props_.etc.undoMaxEtcProvider();
        if (flags_pane_) flags_pane_->setUndoLimit(main_limit);
        flag_comm_.refUndoMax.call();
        // すべての有効なイベントペインを一か所で更新する。D版は取り
        // 消し管理を所有するペインだけが 上限を変更するため、元の
        // ペインが閉じると、複製側が更新を受けなくなる場合がある。
        std::vector<std::shared_ptr<eventwindow::EventWindow>> event_hosts;
        for (const auto& [key, weak] : event_windows_)
            if (const auto host=weak.lock(); host && std::ranges::find(event_hosts,host)==event_hosts.end())
                event_hosts.push_back(host);
        for (const auto& host : event_hosts) host->refUndoMax();

        if (area_table_) area_table_->setUndoLimit(main_limit);
        card_prop_.undoMax = main_limit;
        if (cast_window_) cast_window_->setUndoLimit(main_limit);
        if (skill_window_) skill_window_->setUndoLimit(main_limit);
        if (item_window_) item_window_->setUndoLimit(main_limit);
        if (beast_window_) beast_window_->setUndoLimit(main_limit);
        if (info_window_) info_window_->setUndoLimit(main_limit);
        for (auto it = dynamic_card_windows_.begin(); it != dynamic_card_windows_.end();) {
            if (const auto window = it->second.lock()) {
                window->setUndoLimit(main_limit);
                ++it;
            } else {
                it = dynamic_card_windows_.erase(it);
            }
        }
        name_comm_.undoMaxReplace = static_cast<std::size_t>(
            std::max<std::int32_t>(0, valueOf(prop_->var().undoMaxReplace)));
        if (coupon_window_) coupon_window_->refUndoMax();
        if (gossip_window_) gossip_window_->refUndoMax();
        if (complete_stamp_window_) complete_stamp_window_->refUndoMax();
        if (key_code_window_) key_code_window_->refUndoMax();
        if (cell_name_window_) cell_name_window_->refUndoMax();
        if (card_group_window_) card_group_window_->refUndoMax();
    });
    bind_name_signal(comm_->refCoupons, [this] {
        if (coupon_window_) coupon_window_->refID();
    });
    bind_name_signal(comm_->refGossips, [this] {
        if (gossip_window_) gossip_window_->refID();
    });
    bind_name_signal(comm_->refCompleteStamps, [this] {
        if (complete_stamp_window_) complete_stamp_window_->refID();
    });
    bind_name_signal(comm_->refKeyCodes, [this] {
        if (key_code_window_) key_code_window_->refID();
    });
    bind_name_signal(comm_->refCellNames, [this] {
        if (cell_name_window_) cell_name_window_->refID();
    });
    bind_name_signal(comm_->refCardGroups, [this] {
        if (card_group_window_) card_group_window_->refID();
    });
    bind_name_signal(comm_->refFlagAndStep, [this](const auto& flags, const auto& steps,
                                                   const auto& variants) {
        if (coupon_window_) coupon_window_->refIDFS(flags, steps, variants);
        if (gossip_window_) gossip_window_->refIDFS(flags, steps, variants);
    });
    // このホストは構築時の親にすぎず、実際のページはドッキング側が所有する。
    if (typed_host_) typed_host_->clear();

    const auto registerWidget = [this](String key, WidgetPtr widget,
                                       String title, lib::ResourceId image,
                                       std::function<std::vector<String>()> paths = {}) {
        if (!comm_ || !widget) return;
        dock_.add(key);
        auto panel = std::make_shared<commons::TopLevelPanel>(
            std::dynamic_pointer_cast<lib::Container>(widget));
        panel->title(std::move(title));
        panel->image(image);
        if (paths) panel->openedCWXPathProvider(std::move(paths));
        comm_->registerPane(std::move(key), std::move(panel));
    };
    registerWidget("data", area_table_ ? area_table_->panel() : WidgetPtr{},
                   data_prop_.areasTabName, data_prop_.tableViewImage,
                   [weak = std::weak_ptr(area_table_)] {
                       const auto current = weak.lock();
                       if (!current) return std::vector<String>{};
                       auto paths = current->openedCWXPath();
                       if (paths.empty()) paths.push_back("tableview");
                       return paths;
                   });
    registerWidget("flag", flags_pane_ ? flags_pane_->widget() : WidgetPtr{},
                   data_prop_.flagTabName, data_prop_.variableViewImage,
                   [weak = std::weak_ptr(flags_pane_)] {
                       const auto current = weak.lock();
                       if (!current) return std::vector<String>{};
                       auto paths = current->openedCWXPath();
                       if (paths.empty()) paths.push_back("variableview");
                       return paths;
                   });
    registerWidget("castCard", cast_window_ ? cast_window_->panel() : WidgetPtr{},
                   card_prop_.castName, card_prop_.castImage,
                   [weak = std::weak_ptr(cast_window_)] {
                       const auto current = weak.lock();
                       return current ? current->openedCWXPath() : std::vector<String>{};
                   });
    registerWidget("skillCard", skill_window_ ? skill_window_->panel() : WidgetPtr{},
                   card_prop_.skillName, card_prop_.skillImage,
                   [weak = std::weak_ptr(skill_window_)] {
                       const auto current = weak.lock();
                       return current ? current->openedCWXPath() : std::vector<String>{};
                   });
    registerWidget("itemCard", item_window_ ? item_window_->panel() : WidgetPtr{},
                   card_prop_.itemName, card_prop_.itemImage,
                   [weak = std::weak_ptr(item_window_)] {
                       const auto current = weak.lock();
                       return current ? current->openedCWXPath() : std::vector<String>{};
                   });
    registerWidget("beastCard", beast_window_ ? beast_window_->panel() : WidgetPtr{},
                   card_prop_.beastName, card_prop_.beastImage,
                   [weak = std::weak_ptr(beast_window_)] {
                       const auto current = weak.lock();
                       return current ? current->openedCWXPath() : std::vector<String>{};
                   });
    registerWidget("infoCard", info_window_ ? info_window_->panel() : WidgetPtr{},
                   card_prop_.infoName, card_prop_.infoImage,
                   [weak = std::weak_ptr(info_window_)] {
                       const auto current = weak.lock();
                       return current ? current->openedCWXPath() : std::vector<String>{};
                   });
    registerWidget("file", directory_window_ ? directory_window_->shell() : WidgetPtr{},
                   directory_prop_.messages.title, directory_prop_.images.window,
                   [weak = std::weak_ptr(directory_window_)] {
                       const auto current = weak.lock();
                       return current ? current->openedCWXPath() : std::vector<String>{};
                   });
    registerWidget("coupon", coupon_window_ ? coupon_window_->shell() : WidgetPtr{},
                   coupon_window_ ? coupon_window_->title() : String{}, coupon_window_ ? coupon_window_->image() : 0,
                   [weak = std::weak_ptr(coupon_window_)] {
                       const auto current = weak.lock();
                       return current ? current->openedCWXPath() : std::vector<String>{};
                   });
    registerWidget("gossip", gossip_window_ ? gossip_window_->shell() : WidgetPtr{},
                   gossip_window_ ? gossip_window_->title() : String{}, gossip_window_ ? gossip_window_->image() : 0,
                   [weak = std::weak_ptr(gossip_window_)] {
                       const auto current = weak.lock();
                       return current ? current->openedCWXPath() : std::vector<String>{};
                   });
    registerWidget("completeStamp", complete_stamp_window_ ? complete_stamp_window_->shell() : WidgetPtr{},
                   complete_stamp_window_ ? complete_stamp_window_->title() : String{}, complete_stamp_window_ ? complete_stamp_window_->image() : 0,
                   [weak = std::weak_ptr(complete_stamp_window_)] {
                       const auto current = weak.lock();
                       return current ? current->openedCWXPath() : std::vector<String>{};
                   });
    registerWidget("keyCode", key_code_window_ ? key_code_window_->shell() : WidgetPtr{},
                   key_code_window_ ? key_code_window_->title() : String{}, key_code_window_ ? key_code_window_->image() : 0,
                   [weak = std::weak_ptr(key_code_window_)] {
                       const auto current = weak.lock();
                       return current ? current->openedCWXPath() : std::vector<String>{};
                   });
    registerWidget("cellName", cell_name_window_ ? cell_name_window_->shell() : WidgetPtr{},
                   cell_name_window_ ? cell_name_window_->title() : String{}, cell_name_window_ ? cell_name_window_->image() : 0,
                   [weak = std::weak_ptr(cell_name_window_)] {
                       const auto current = weak.lock();
                       return current ? current->openedCWXPath() : std::vector<String>{};
                   });
    registerWidget("cardGroup", card_group_window_ ? card_group_window_->shell() : WidgetPtr{},
                   card_group_window_ ? card_group_window_->title() : String{}, card_group_window_ ? card_group_window_->image() : 0,
                   [weak = std::weak_ptr(card_group_window_)] {
                       const auto current = weak.lock();
                       return current ? current->openedCWXPath() : std::vector<String>{};
                   });

    const auto host_alive = [weak = std::weak_ptr<std::atomic_bool>(lifecycle_alive_)] {
        const auto state = weak.lock();
        return state && state->load(std::memory_order_acquire);
    };
    comm_->externalChanges([this, host_alive] {
        return host_alive() && directory_window_ && directory_window_->isChanged();
    });
    comm_->importPublication([this, host_alive](Summary* target,
                                                const importutils::ImportResult& imported) {
        const auto current = [this, host_alive, target] {
            return host_alive() && summary_.get() == target && comm_;
        };
        if (!current()) return;
        // D版のGCによるデリゲートは、追加中に受け手を無効化しない。 C++版のホ
        // ストコールバックでは、ホストや文書が無効になったら残りの処理を中止する。
        if (!imported.materials.empty() && directory_window_) directory_window_->refresh();
        if (!current()) return;
        if (!imported.flags.empty() || !imported.steps.empty() || !imported.variants.empty()) {
            (void)comm_->openFlagWin(false);
            if (!current()) return;
            if (flags_pane_) flags_pane_->addFlagsAndSteps(imported.flags, imported.steps, imported.variants);
        }
        const auto add_cards = [this, &current](const auto& entries, const auto& window,
                                               const auto kind) {
            if (entries.empty() || !current() || !window) return;
            switch (kind) {
            case cardwindow_ns::CardType::Cast: (void)comm_->openCastWin(false); break;
            case cardwindow_ns::CardType::Skill: (void)comm_->openSkillWin(false); break;
            case cardwindow_ns::CardType::Item: (void)comm_->openItemWin(false); break;
            case cardwindow_ns::CardType::Beast: (void)comm_->openBeastWin(false); break;
            case cardwindow_ns::CardType::Info: (void)comm_->openInfoWin(false); break;
            }
            if (!current()) return;
            auto pane = window->openPane(kind, false);
            if (!current() || !pane || !pane->canonical()) return;
            using Card = cardwindow_ns::CardPtr;
            std::vector<std::pair<std::uint64_t, Card>> ordered;
            for (const auto& [id, value] : entries) if (value) ordered.emplace_back(id, value);
            std::sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
            std::vector<Card> cards;
            for (const auto& [id, value] : ordered) cards.push_back(value);
            pane->canonical()->addCards(cards);
        };
        add_cards(imported.casts, cast_window_, cardwindow_ns::CardType::Cast);
        add_cards(imported.skills, skill_window_, cardwindow_ns::CardType::Skill);
        add_cards(imported.items, item_window_, cardwindow_ns::CardType::Item);
        add_cards(imported.beasts, beast_window_, cardwindow_ns::CardType::Beast);
        add_cards(imported.infos, info_window_, cardwindow_ns::CardType::Info);
        if (!current()) return;
        std::vector<::cwxeditor::cwx::area::AbstractAreaPtr> areas;
        const auto append_areas = [&areas](const auto& entries) {
            std::vector<std::pair<std::uint64_t, ::cwxeditor::cwx::area::AbstractAreaPtr>> ordered;
            for (const auto& [id, value] : entries) if (value) ordered.emplace_back(id, value);
            std::sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
            for (const auto& [id, value] : ordered) areas.push_back(value);
        };
        append_areas(imported.areas);
        append_areas(imported.battles);
        append_areas(imported.packages);
        if (!areas.empty()) {
            (void)comm_->openDataWin(false);
            if (current() && area_table_) area_table_->addAreas(areas);
        }
    });

    const auto pathFromKey = [](String key) {
        if (const auto duplicate = key.find('#'); duplicate != String::npos)
            key.erase(duplicate);
        if (const auto separator = key.find(':'); separator != String::npos)
            return key.substr(separator + 1U);
        return String{};
    };
    using SceneBackEdit = std::function<void(const std::vector<std::size_t>&)>;
    const auto configureBackgroundDialogs = [this](const auto& editor,
                                                    const SummaryPtr& scene_summary) -> SceneBackEdit {
        if (!editor || !editor->areaView() || !prop_) return {};
        auto* view = editor->areaView();
        using View = std::remove_pointer_t<decltype(view)>;
        if constexpr (!std::is_same_v<typename View::AreaType,
                                      ::cwxeditor::cwx::area::Area>) {
            return {};
        } else {
            const auto view_life=view->sceneLifetime();
            const auto main_live=[main_life=std::weak_ptr<std::atomic_bool>(lifecycle_alive_)] {
                const auto state=main_life.lock();
                return state && state->load(std::memory_order_acquire);
            };
            const auto live=[view_life,main_live]{return !view_life.expired() && main_live();};
            // D版と同様、取り込み元のタブも含め、このシーンから _forceSkin を渡す。
            const auto openBackground = [this, view, scene_summary,live,view_life,main_live](
                                            ::cwxeditor::cwx::background::BgImagePtr back,
                                            const bool create) {
                if (!live() || !back || !prop_ || view->readOnly()) return;
                // AreaScene は構築時に選んだシナリオのスキンより長く存続
                // する場合があるため、 ダイアログを開く時点でスキンを解決する。
                const auto dialog_skin = view->context().skin_owner
                    ? view->context().skin_owner : skin_;
                // 通常のシナリオから解決したスキンは、D版の明示的な forceSkin とは区別する。
                const auto* dialog_force_skin = view->summSkin() ? view->summSkin()
                    : (scene_summary != summary_ ? dialog_skin.get() : nullptr);
                const SceneDialogKey key{view, back.get()};
                if (const auto found = active_background_dialogs_.find(key);
                    found != active_background_dialogs_.end()) {
                    if (found->second.window && found->second.window->visible()) {
                        if (services_.gui) {
                            const auto window=found->second.window;
                            const auto gui=services_.gui;
                            gui->show(window);if(!live())return;
                            gui->focus(window);
                        }
                        return;
                    }
                    active_background_dialogs_.erase(found);
                }

                if (!create && !lib::editor_window_host_options().all_mode) {
                    const auto backs = view->area().backs();
                    const auto found = std::find(backs.begin(), backs.end(), back);
                    if (found == backs.end() || !scenehost::projectEditSelection(
                            view, false, static_cast<std::size_t>(found - backs.begin()), live)) return;
                }
                bgimagedialog::BgImageDialogContext context(*prop_);
                context.services = services_;
                context.common.services = services_;
                context.common.skin = dialog_skin.get();
                context.skin_owner = dialog_skin;
                context.common.summary = scene_summary.get();
                context.editor_commons = comm_;
                // D版の selectEasySetting と同様、元サイズの設定変更後に主ツールバーを更新する。
                context.refresh_toolbar = [this,live] {
                    if (!live()) return;
                    if (comm_) comm_->refreshToolBar();
                };
                context.refresh_cell_names = [this,live] {
                    if(!live())return;
                    if (comm_) comm_->refCellNames.call();
                };
                context.settings = [this,dialog_skin,live] {
                    if (!live() || !prop_) return std::vector<structs::BgImageSetting>{};
                    // D版と同様、refSkin で現在の Commons のスキンから設定を組み直す。
                    const auto current_skin = comm_ ? comm_->skin() : dialog_skin;
                    const String type = current_skin ? current_skin->type() : String{};
                    auto result = ::cwxeditor::cwx::editor::gui::dwt::dprops::arraySnapshot(
                        prop_->bgImageSettings(type));
                    if (result.empty())
                        result = ::cwxeditor::cwx::editor::gui::dwt::dprops::arraySnapshot(
                            prop_->bgImageSettings({}));
                    return result;
                };
                std::shared_ptr<bgimagedialog::BgImageDialog> dialog;
                const bgimagedialog::UseCounterPtr counter =
                    scene_summary ? scene_summary->useCounter()
                             : bgimagedialog::UseCounterPtr{};
                const auto used_names = [this, counter,live]<typename Id> {
                    std::vector<String> result;
                    if (!live() || !counter) return result;
                    for (const auto& key : counter->template keys<Id>()) {
                        const String value = static_cast<String>(key);
                        if (!value.empty() &&
                            std::find(result.begin(), result.end(), value) == result.end())
                            result.push_back(value);
                    }
                    const bool logical = prop_ && valueOf(prop_->var().logicalSort);
                    std::stable_sort(result.begin(), result.end(), [logical](
                                         const String& left, const String& right) {
                        return logical ? ::cwxeditor::cwx::utils::ncmp(left, right) < 0
                                       : left < right;
                    });
                    return result;
                };
                context.flags = used_names.template operator()<usecounter::FlagId>();
                context.cell_names =
                    used_names.template operator()<usecounter::CellNameId>();
                context.cell_names_provider = [used_names] {
                    return used_names.template operator()<usecounter::CellNameId>();
                };
                if (auto value = std::dynamic_pointer_cast<
                        ::cwxeditor::cwx::background::ImageCell>(back)) {
                    dialog = std::make_shared<bgimagedialog::ImageCellDialog>(
                        context, *prop_, scene_summary.get(), dialog_force_skin, value,
                        create, window_, counter);
                } else if (auto value =
                               std::dynamic_pointer_cast<
                                   ::cwxeditor::cwx::background::TextCell>(back)) {
                    dialog = std::make_shared<bgimagedialog::TextCellDialog>(
                        context, *prop_, scene_summary.get(), dialog_force_skin, value,
                        create, window_, counter);
                } else if (auto value =
                               std::dynamic_pointer_cast<
                                   ::cwxeditor::cwx::background::ColorCell>(back)) {
                    dialog = std::make_shared<bgimagedialog::ColorCellDialog>(
                        context, *prop_, scene_summary.get(), value, create, window_,
                        counter);
                } else if (auto value =
                               std::dynamic_pointer_cast<
                                   ::cwxeditor::cwx::background::PCCell>(back)) {
                    dialog = std::make_shared<bgimagedialog::PCCellDialog>(
                        context, *prop_, scene_summary.get(), dialog_force_skin, value,
                        create, window_, counter);
                }
                if (!dialog) return;

                const auto transaction = std::make_shared<typename View::ExternalBackEditPtr>();

                const auto inserted = std::make_shared<bool>(!create);
                const auto weak_dialog =
                    std::weak_ptr<bgimagedialog::BgImageDialog>(dialog);
                // D版と同様、取り消しの基準値は applyEvent 時にそのセルから
                // 取得する。 他の編集より前にモードレス画面を開いた時点の値は使わない。
                dialog->applyEvent.push_back([view,weak_dialog,inserted,transaction,live] {
                    if(!live() || !*inserted)return;
                    if(const auto current=weak_dialog.lock())*transaction=view->beginExternalBackEdit(current->back());
                });
                dialog->appliedEvent.push_back(
                    [this, view, scene_summary,weak_dialog, inserted, transaction,live] {
                        if(!live())return;
                        const auto current = weak_dialog.lock();
                        if (!current) return;
                        const auto expected_revision=view->sceneRevision()+1U;
                        if (!*inserted) {
                            // 適用後もモードレスダイアログが編集し
                            // 続ける、同じオブジェクトを保持する。
                            const auto index=view->insertCreatedBgImage(current->back());
                            // ホストへの通知が新しいシーン編集を始め
                            // ても、挿入自体は完了済みとして扱う。
                            if(index>=0)*inserted=true;
                            if(!live() || view->sceneRevision()!=expected_revision)return;
                            if (index >= 0) {
                                *inserted = true;
                                *transaction =
                                    view->beginExternalBackEdit(current->back());
                            }
                        } else {
                            const bool applied=view->commitExternalBackEdit(*transaction);
                            if(!live() || !applied || view->sceneRevision()!=expected_revision)return;
                        }
                        if(!live() || view->sceneRevision()!=expected_revision)return;
                        if (scene_summary) scene_summary->changed();
                        if(!live() || view->sceneRevision()!=expected_revision)return;
                        if (comm_) {
                            comm_->refCellNames.call();
                            if(!live() || view->sceneRevision()!=expected_revision)return;
                            comm_->refreshToolBar();
                        }
                    });
                dialog->closeEvent.push_back(
                    [this, view, key, transaction,weak_dialog,view_life,main_live] {
                        if(!main_live())return;
                        if(!view_life.expired())view->endExternalBackEdit(*transaction);
                        transaction->reset();
                        const auto current=weak_dialog.lock();
                        const auto found=active_background_dialogs_.find(key);
                        if(current && found!=active_background_dialogs_.end() && found->second.owner.get()==current.get())active_background_dialogs_.erase(found);
                    });
                // C++版では、借用するシーンのコールバックを明示的に無効化する必要がある
                // 。 D版のモードレス画面のマップは、代わりにGC管理の受け手を保持する。
                view->onSceneRetired([weak_dialog]{if(const auto current=weak_dialog.lock())current->forceCancel();});
                active_background_dialogs_[key] = {dialog, dialog->dialog(),cardwindow_ns::CardType::Cast,
                    [weak_dialog]{if(const auto current=weak_dialog.lock())current->forceApply();}};
                dynamic_panes_.push_back(dialog);
                dialog->modal(false);
                (void)dialog->open();
            };

            view->context().create_background =
                [this, openBackground,live](const int type) {
                    if(!live())return;
                    ::cwxeditor::cwx::background::BgImagePtr value;
                    if (type == 1) {
                        auto cell = std::make_shared<
                            ::cwxeditor::cwx::background::TextCell>();
                        cell->width(valueOf(prop_->var().textCellDefaultWidth));
                        cell->height(valueOf(prop_->var().textCellDefaultHeight));
                        value = std::move(cell);
                    } else if (type == 2) {
                        auto cell = std::make_shared<
                            ::cwxeditor::cwx::background::ColorCell>();
                        cell->width(valueOf(prop_->var().colorCellDefaultWidth));
                        cell->height(valueOf(prop_->var().colorCellDefaultHeight));
                        value = std::move(cell);
                    } else if (type == 3) {
                        auto cell = std::make_shared<
                            ::cwxeditor::cwx::background::PCCell>();
                        const auto size = prop_->looks().cardSize();
                        cell->width(static_cast<std::int32_t>(size.width));
                        cell->height(static_cast<std::int32_t>(size.height));
                        value = std::move(cell);
                    } else {
                        value = std::make_shared<
                            ::cwxeditor::cwx::background::ImageCell>();
                    }
                    openBackground(std::move(value), true);
                };
            const auto previous_edit = view->context().callbacks.edit;
            SceneBackEdit editBackgrounds =
                [view, openBackground, previous_edit,live](const std::vector<std::size_t>& selection) {
                    if(!live())return;
                    const auto backs=view->area().backs();
                    const auto stop_open=view->context().stop_open;
                    bool opened = false;
                    for (const auto index : selection) {
                        if(stop_open && stop_open())return;
                        if(!live())return;
                        if (index >= backs.size()) continue;
                        openBackground(backs[index], false);
                        if(!live())return;
                        opened = true;
                    }
                    if (!opened && previous_edit) previous_edit();
                };
            view->context().callbacks.edit = [view, editBackgrounds, live] {
                if (!live()) return;
                const auto selected = view->backList().selection;
                editBackgrounds(selected);
            };
            return editBackgrounds;
        }
    };
    const auto configureSceneCardDialogs = [this](const auto& editor,
                                                  const SummaryPtr& scene_summary,
                                                  SceneBackEdit editBackgrounds) {
        if (!editor || !editor->areaView() || !prop_ || !scene_summary) return;
        auto* view = editor->areaView();
        using View = std::remove_pointer_t<decltype(view)>;
        using Card = typename View::CardType;
        const auto view_life=view->sceneLifetime();
        const auto main_live=[main_life=std::weak_ptr<std::atomic_bool>(lifecycle_alive_)] {
            const auto state=main_life.lock();
            return state && state->load(std::memory_order_acquire);
        };
        const auto live=[view_life,main_live]{return !view_life.expired() && main_live();};
        // 取り込んだシーンのタブは、取り込み元のシナリオのスキンを持つ。
        // ここで主シナリオのスキンを使うと、カードの素材選択・プレビュ
        // ーが シーンのキャンバスとは別の素材ツリーを参照してしまう。
        const auto dialog_skin = view->context().skin_owner
            ? view->context().skin_owner : skin_;
        // 通常のシナリオから解決したスナップショットは、D版の明示的な forceSkin とは区別する。
        const auto* dialog_force_skin = view->summSkin() ? view->summSkin()
            : (scene_summary != summary_ ? dialog_skin.get() : nullptr);

        const auto openCard = [this, view, scene_summary, dialog_skin,dialog_force_skin,live,view_life,main_live](const std::shared_ptr<Card>& card,
                                           const bool create = false) {
            if (!live() || !card || !prop_ || !scene_summary || view->readOnly()) return;
            if constexpr (std::is_same_v<Card,
                                         ::cwxeditor::cwx::area::EnemyCard>) {
                // D版と同様、敵カードの参照先を編集するには、
                // 選択可能なキャスト定義が少なくとも一つ必要。
                if (scene_summary->casts().empty()) return;
            }

            const SceneDialogKey key{view, card.get()};
            if (const auto found = active_scene_card_dialogs_.find(key);
                found != active_scene_card_dialogs_.end()) {
                if (found->second.window && found->second.window->visible()) {
                    if (services_.gui) {
                        const auto window=found->second.window;
                        const auto gui=services_.gui;
                        gui->show(window);if(!live())return;
                        gui->focus(window);
                    }
                    return;
                }
                active_scene_card_dialogs_.erase(found);
            }

            if (!create && !lib::editor_window_host_options().all_mode) {
                const auto cards = view->area().cards();
                const auto found = std::find(cards.begin(), cards.end(), card);
                if (found == cards.end() || !scenehost::projectEditSelection(
                        view, true, static_cast<std::size_t>(found - cards.begin()), live)) return;
            }
            const auto inserted = std::make_shared<bool>(!create);
            const auto active_key = std::make_shared<SceneDialogKey>(key);
            spcarddialog::SpCardDialogContext context(*prop_);
            context.services = services_;
            context.common.services = services_;
            context.common.skin = dialog_skin.get();
            context.common.summary = scene_summary.get();
            context.editor_commons = comm_;
            const auto used_names = [this, scene_summary,live]<typename Id> {
                std::vector<String> result;
                if(!live())return result;
                const auto counter = scene_summary->useCounter();
                if (!counter) return result;
                for (const auto& key : counter->template keys<Id>()) {
                    const String name = static_cast<String>(key);
                    if (!name.empty() &&
                        std::find(result.begin(), result.end(), name) == result.end())
                        result.push_back(name);
                }
                const bool logical = prop_ && valueOf(prop_->var().logicalSort);
                std::stable_sort(result.begin(), result.end(), [logical](
                                     const String& left, const String& right) {
                    return logical ? ::cwxeditor::cwx::utils::ncmp(left, right) < 0
                                   : left < right;
                });
                return result;
            };
            context.flags = used_names.template operator()<usecounter::FlagId>();
            context.card_groups =
                used_names.template operator()<usecounter::CardGroupId>();
            context.refresh_toolbar = [this,live] {
                if(!live())return;
                if (comm_) comm_->refreshToolBar();
            };
            context.refresh_card_groups = [this,live] {
                if(!live())return;
                if (comm_) comm_->refCardGroups.call();
            };
            // SpCardDialog は自身の警告欄を更新する
            // 。 途中の編集、特にアニメーション速度の変更のたび
            // にシーン全体を更新すると、主画面が点滅し続ける。
            context.refresh_warnings = {};
            context.casts = [summary = scene_summary]() {
                std::vector<spcarddialog::ConstCastCardPtr> values;
                if (summary) {
                    const auto source = std::as_const(*summary).casts();
                    values.reserve(source.size());
                    for (const auto& cast : source) values.push_back(cast);
                }
                return values;
            };
            context.open_cast_view = [this, scene_summary, live](std::uint64_t id) {
                if (!live() || summary_ != scene_summary || !comm_) return;
                const auto cast = scene_summary->cwCast(id);
                if (!cast) return;
                const auto path = ::cwxeditor::cwx::path::cpaddattr(cast->cwxPath(true), "shallow");
                // 型付きの通常の Cast 用ホストを再利用する。 Commons
                // の path: 検索では、この固定リソースのペインを生成できない。
                const auto window = cast_window_;
                const auto expected = window ? window->paneCast() : cardwindow_ns::CardPanePtr{};
                const auto open_pane = card_comm_.openCardPane;
                if (!window || !expected || !open_pane) return;
                const auto pane = open_pane(cardwindow_ns::CardType::Cast, true);
                if (!live() || summary_ != scene_summary ||
                    scene_summary->cwCast(id) != cast || pane != expected ||
                    window->summary() != scene_summary ||
                    pane->summary() != scene_summary || pane->cardById(id) != cast) return;
                (void)pane->openCWXPath(path, false);
            };
            context.add_card = [view, inserted,live](
                                   std::shared_ptr<spcarddialog::AbstractSpCard>
                                       candidate) {
                if(!live())return;
                const auto typed = std::dynamic_pointer_cast<Card>(candidate);
                if (!typed) return;
                *inserted = view->insertCreatedCard(typed) >= 0;
            };

            std::shared_ptr<spcarddialog::SpCardDialog<Card>> dialog;
            if constexpr (std::is_same_v<Card,
                                         ::cwxeditor::cwx::area::MenuCard>) {
                dialog = std::make_shared<spcarddialog::MenuCardDialog>(
                    context, *prop_, scene_summary.get(), dialog_force_skin, card, create,
                    window_, scene_summary->useCounter());
            } else if constexpr (std::is_same_v<
                                     Card,
                                     ::cwxeditor::cwx::area::EnemyCard>) {
                dialog = std::make_shared<spcarddialog::EnemyCardDialog>(
                    context, *prop_, scene_summary.get(), dialog_force_skin, card, create,
                    window_, scene_summary->useCounter());
            } else {
                return;
            }

            const auto weak_dialog =
                std::weak_ptr<spcarddialog::SpCardDialog<Card>>(dialog);
            // D版と同様、取り消しの基準値はモードレス画面のカードごとに保持する
            // 。 選択の変更や別ダイアログの適用で、その基準値を入れ替えない。
            const auto transaction=std::make_shared<typename View::ExternalCardEditPtr>();
            dialog->applyEvent.push_back([view, inserted,weak_dialog,transaction,live] {
                if(!live() || !*inserted)return;
                if(const auto current=weak_dialog.lock())*transaction=view->beginExternalCardEdit(current->card());
            });
            dialog->appliedEvent.push_back(
                [this, view, weak_dialog, inserted, active_key,transaction,live] {
                if(!live())return;
                const auto current = weak_dialog.lock();
                if (!current || !*inserted) return;
                const auto before_commit=view->sceneRevision();
                const auto expected_revision=before_commit+(*transaction?1U:0U);
                if(*transaction) {
                    const bool applied=view->commitExternalCardEdit(*transaction);
                    if(!live() || !applied || view->sceneRevision()!=expected_revision)return;
                }
                if(!live())return;
                view->editCardApply(current->card());
                if(!live() || view->sceneRevision()!=expected_revision)return;
                if (summary_) summary_->changed();
                if(!live() || view->sceneRevision()!=expected_revision)return;
                if (comm_) {
                    comm_->refMenuCardAndBgImageList.call();if(!live() || view->sceneRevision()!=expected_revision)return;
                    comm_->refUseCount.call();if(!live() || view->sceneRevision()!=expected_revision)return;
                    comm_->refCardGroups.call();if(!live() || view->sceneRevision()!=expected_revision)return;
                    comm_->refreshToolBar();if(!live() || view->sceneRevision()!=expected_revision)return;
                }
                const auto current_target=static_cast<const void*>(current->card().get());
                if(current_target!=active_key->target) {
                    const auto found=active_scene_card_dialogs_.find(*active_key);
                    if(found!=active_scene_card_dialogs_.end() && found->second.owner.get()==current.get()) {
                        auto node=active_scene_card_dialogs_.extract(found);
                        node.key()=SceneDialogKey{view,current_target};
                        active_key->target=current_target;
                        active_scene_card_dialogs_.insert(std::move(node));
                    }
                }
            });
            dialog->closeEvent.push_back([this,view,active_key,transaction,weak_dialog,view_life,main_live] {
                if(!main_live())return;
                if(!view_life.expired())view->endExternalCardEdit(*transaction);
                transaction->reset();
                if(!main_live())return;
                const auto current=weak_dialog.lock();
                const auto found=active_scene_card_dialogs_.find(*active_key);
                if(current && found!=active_scene_card_dialogs_.end() && found->second.owner.get()==current.get())active_scene_card_dialogs_.erase(found);
            });
            // シーンのモードレスエディタ自身を保持したまま無効化する。 そのコー
            // ルバックからは、シーンのトークンが直ちに失効して見えるようにする。
            view->onSceneRetired([weak_dialog]{if(const auto current=weak_dialog.lock())current->forceCancel();});
            active_scene_card_dialogs_[key] = {
                dialog, dialog->dialog(), cardwindow_ns::CardType::Cast,
                [weak_dialog] {
                    if (const auto current = weak_dialog.lock()) current->forceApply();
                }};
            dynamic_panes_.push_back(dialog);
            // シーンのカードは、D版のモードレスエディタと同様、カードごとに保持し
            // たダイアログのマップを使う。 特にWebでは、ホストされたキャンバ
            // スの有効化コールバックから 入れ子の同期モーダルループに入らない。
            dialog->modal(false);
            (void)dialog->open();
        };

        // 左側一覧とシーンのキャンバスは、どちらも edit() を呼ぶ前にこの
        // 選択を同期する。 ここで選択モデルを解決し、D版の _editDlgsC
        // と同様、オブジェクトごとに一つのモードレスエディタを保持する。
        const auto previous_edit = view->context().callbacks.edit;
        view->context().callbacks.edit_target =
            [view, openCard, editBackgrounds, previous_edit, live](
                const areawindow::SceneEditTargetKind kind,
                const std::size_t index) {
                if (!live()) return;
                if (kind == areawindow::SceneEditTargetKind::Card) {
                    const auto cards = view->area().cards();
                    if (index < cards.size()) openCard(cards[index]);
                } else if (editBackgrounds) {
                    const std::vector<std::size_t> target{index};
                    editBackgrounds(target);
                } else if (previous_edit) previous_edit();
            };
        view->context().callbacks.edit = scenehost::cardEditCallback(
            view, openCard, previous_edit, live, std::move(editBackgrounds));
        view->context().create_card = [this, view, openCard, scene_summary,live] {
            if (!live() || !scene_summary || view->readOnly()) return;
            std::shared_ptr<Card> card;
            if constexpr (std::is_same_v<
                              Card, ::cwxeditor::cwx::area::MenuCard>) {
                card = std::make_shared<Card>(
                    "", false, ::cwxeditor::cwx::area::CardImageArray{}, "", "",
                    0, 0, 100U, ::cwxeditor::cwx::types::LAYER_MENU_CARD, "", -1);
            } else if constexpr (std::is_same_v<
                                     Card,
                                     ::cwxeditor::cwx::area::EnemyCard>) {
                if (scene_summary->casts().empty()) return;
                card = std::make_shared<Card>(
                    0U, ::cwxeditor::cwx::area::ActionMap{}, "", 0, 0, 100U,
                    ::cwxeditor::cwx::types::LAYER_MENU_CARD, "", -1, false,
                    "", false,
                    ::cwxeditor::cwx::area::CardImageArray{});
            }
            openCard(card, true);
        };
    };
    const auto registerScene =
        [this, pathFromKey, configureBackgroundDialogs,
         configureSceneCardDialogs,open_replace_id](String family, bool battle) {
        comm_->registerPaneFactory(
            std::move(family),
            [this, pathFromKey, battle, configureBackgroundDialogs,
             configureSceneCardDialogs,open_replace_id](String key, bool, const std::shared_ptr<void>& source_owner)
                -> std::shared_ptr<commons::TopLevelPanel> {
                const auto source_summary = source_owner
                    ? std::static_pointer_cast<Summary>(source_owner) : summary_;
                if (!source_summary || !prop_) return {};
                const bool read_only = source_summary != summary_;
                const String path = pathFromKey(key);
                const auto resource = source_summary->findCWXPath(path);
                auto scene_skin = comm_ ? comm_->findSkinFromHistory(source_summary.get())
                                        : dskin::SkinPtr{};
                if (!scene_skin) scene_skin = skin_;
                auto context = std::make_shared<areawindow::AreaWindowContext>();
                // D版と同様、編集中の同じ Summary に属する全エリア・戦
                // 闘ビュー間で更新を伝える。 ペインごとの登録先では、複製したシ
                // ーンが、他のビューの編集や取り消し・やり直しに追従できない。
                auto& shared_scene_registry =
                    scene_view_registries_[source_summary.get()];
                if (!shared_scene_registry)
                    shared_scene_registry =
                        std::make_shared<areawindow::SceneViewRegistry>();
                context->scene_view_registry = shared_scene_registry;
                auto lifetime = std::make_shared<DynamicSceneLifetime>();
                lifetime->context = context;
                context->services = services_;
                const std::weak_ptr<std::atomic_bool> wallpaper_life = lifecycle_alive_;
                context->paint_wallpaper = [this, wallpaper_life](lib::DrawingContext& drawing,
                                                                   lib::Rect rect) {
                    const auto alive = wallpaper_life.lock();
                    if (!alive || !alive->load(std::memory_order_acquire) ||
                        lib::editor_window_host_options().all_mode || !comm_ || !prop_) return;
                    const auto image = comm_->wallpaper();
                    if (!image) return;
                    auto style_value = prop_->var().wallpaperStyle.value;
                    if (style_value < static_cast<int>(structs::WallpaperStyle::Center) ||
                        style_value > static_cast<int>(structs::WallpaperStyle::ExpandFull)) {
                        style_value = static_cast<int>(structs::WallpaperStyle::Tile);
                        prop_->var().wallpaperStyle.value = style_value;
                    }
                    dutils::drawWallpaper(drawing, image, rect,
                        static_cast<structs::WallpaperStyle>(style_value),
                        prop_->images().imageSize(image));
                };
                context->skin_owner = scene_skin;
                context->skin = scene_skin.get();
                // シーンのコンストラクター設定とペイン画像の扱いをD版に合わせる。
                context->show_inherit_background = prop_->var().showInheritBackground.value;
                context->area_image = images_ ? images_->areaSceneView() : 0U;
                context->battle_image = images_ ? images_->battleSceneView() : 0U;
                context->open_area_scene = [this, source_summary](
                    ::cwxeditor::cwx::area::AbstractArea* target) {
                    if (!target || !comm_ || !prop_) return;
                    if (const auto area =
                            dynamic_cast<::cwxeditor::cwx::area::Area*>(target))
                        (void)comm_->openAreaScene(
                            *prop_, source_summary.get(), source_summary->area(area->id()),
                            true, nullptr);
                    else if (const auto battle =
                                 dynamic_cast<::cwxeditor::cwx::area::Battle*>(target))
                        (void)comm_->openAreaScene(
                            *prop_, source_summary.get(), source_summary->battle(battle->id()),
                             true, nullptr);
                };
                const auto duplicate_base = std::make_shared<std::weak_ptr<commons::TopLevelPanel>>();
                context->open_duplicate = [this, duplicate_base,
                    weak = std::weak_ptr<std::atomic_bool>(lifecycle_alive_)](
                    ::cwxeditor::cwx::area::AbstractArea*) {
                    const auto alive = weak.lock();
                    if (!alive || !alive->load(std::memory_order_acquire) || !comm_) return;
                    if (const auto base = duplicate_base->lock())
                        (void)comm_->duplicatePanel(base, true);
                };
                context->open_cwx_path = [this](const StringView path,
                                                const bool activate) {
                    return openCWXPath(String(path), activate);
                };
                context->open_file_path=[this,weak=std::weak_ptr<std::atomic_bool>(lifecycle_alive_)](const StringView file,bool activate,bool only){
                    const auto alive=weak.lock();if(!alive || !alive->load(std::memory_order_acquire) || !comm_)return false;
                    return comm_->openFilePath(String(file),activate,only);
                };
                const auto scene_comments = sceneCommentCallbacks();
                context->make_scene_comment_dialog = scene_comments.make_dialog;
                context->stop_open=[this,weak=std::weak_ptr<std::atomic_bool>(lifecycle_alive_)]{
                    const auto alive=weak.lock();return !alive || !alive->load(std::memory_order_acquire) || !comm_ || comm_->stopOpen();
                };
                context->subscribe_scene_comment_updates = scene_comments.subscribe_updates;
                context->subscribe_menu_updates = [this, weak=std::weak_ptr<std::atomic_bool>(lifecycle_alive_)](
                        std::function<void(types::MenuID)> callback) {
                    const auto alive = weak.lock();
                    if (!alive || !alive->load(std::memory_order_acquire) || !comm_)
                        return lib::Action{};
                    const auto token = comm_->refMenu.add(std::move(callback));
                    return lib::Action([this, weak, token] {
                        const auto alive = weak.lock();
                        if (!alive || !alive->load(std::memory_order_acquire) || !comm_) return;
                        comm_->refMenu.remove(token);
                    });
                };
                context->subscribe_skin_updates = [this, weak=std::weak_ptr<std::atomic_bool>(lifecycle_alive_)](
                        std::function<void(skin::Skin::Ptr)> callback) {
                    const auto alive = weak.lock();
                    if (!alive || !alive->load(std::memory_order_acquire) || !comm_)
                        return lib::Action{};
                    const auto token = comm_->refSkin.add([this, weak, callback=std::move(callback)] {
                        const auto alive = weak.lock();
                        if (!alive || !alive->load(std::memory_order_acquire) || !comm_ || !callback) return;
                        callback(comm_->skin());
                    });
                    return lib::Action([this, weak, token] {
                        const auto alive = weak.lock();
                        if (!alive || !alive->load(std::memory_order_acquire) || !comm_) return;
                        comm_->refSkin.remove(token);
                    });
                };
                context->import_scene_image = scenehost::imageImporter(
                    prop_, services_, source_summary, scene_skin);
                context->ref_paths = [this,weak=std::weak_ptr<std::atomic_bool>(lifecycle_alive_),
                    summary=std::weak_ptr<::cwxeditor::cwx::summary::Summary>(source_summary)](
                        const std::string_view path) {
                    const auto alive=weak.lock();
                    const auto source=summary.lock();
                    if (alive && alive->load(std::memory_order_acquire) && source &&
                        summary_==source && comm_)
                        comm_->refPaths.call(String(path));
                };
                context->ref_bg_image = [this,weak=std::weak_ptr<std::atomic_bool>(lifecycle_alive_)](const StringView path) {
                    const auto alive=weak.lock();
                    if (alive && alive->load(std::memory_order_acquire) && comm_)
                        comm_->refBgImage.call(String(path));
                };
                context->ref_menu_card = [this,weak=std::weak_ptr<std::atomic_bool>(lifecycle_alive_)](const StringView path) {
                    const auto alive=weak.lock();
                    if (alive && alive->load(std::memory_order_acquire) && comm_)
                        comm_->refMenuCard.call(String(path));
                };
                const auto typed_path=[this,weak=std::weak_ptr<std::atomic_bool>(lifecycle_alive_)](auto member) {
                    return [this,weak,member](const StringView path) {
                        const auto alive=weak.lock();
                        if(alive && alive->load(std::memory_order_acquire) && comm_)
                            (comm_->*member).call(String(path));
                    };
                };
                context->add_menu_card=typed_path(&commons::Commons::addMenuCard);
                context->del_menu_card=typed_path(&commons::Commons::delMenuCard);
                context->add_bg_image=typed_path(&commons::Commons::addBgImage);
                context->del_bg_image=typed_path(&commons::Commons::delBgImage);
                context->ref_card_groups=[this,weak=std::weak_ptr<std::atomic_bool>(lifecycle_alive_)] {
                    const auto alive=weak.lock();
                    if(alive && alive->load(std::memory_order_acquire) && comm_)comm_->refCardGroups.call();
                };
                context->ref_cell_names=[this,weak=std::weak_ptr<std::atomic_bool>(lifecycle_alive_)] {
                    const auto alive=weak.lock();
                    if(alive && alive->load(std::memory_order_acquire) && comm_)comm_->refCellNames.call();
                };
                context->ref_menu_card_indices = [this,weak=std::weak_ptr<std::atomic_bool>(lifecycle_alive_)](
                    ::cwxeditor::cwx::area::AbstractArea* area,
                    const std::vector<std::pair<std::size_t,std::size_t>>& indices) {
                    const auto life=weak.lock();if(!life || !life->load() || !area || !comm_)return;
                    if(const auto owner=area->weak_from_this().lock())comm_->refMenuCardIndices.call(owner,indices);
                };
                context->history_card_indices_refresh =
                    [this,weak=std::weak_ptr<std::atomic_bool>(lifecycle_alive_),
                     summary=std::weak_ptr<::cwxeditor::cwx::summary::Summary>(source_summary)](
                        ::cwxeditor::cwx::area::AbstractArea* area,
                        const std::vector<std::pair<std::size_t,std::size_t>>& indices) {
                        const auto alive=weak.lock(); const auto source=summary.lock();
                        if (!alive || !alive->load(std::memory_order_acquire) ||
                            !source || summary_!=source || !area || !comm_) return;
                        if (const auto owner=area->weak_from_this().lock())
                            comm_->refMenuCardIndices.call(owner,indices);
                    };
                context->find_named_id = [open_replace_id](String value,bool cell) {
                    open_replace_id(std::move(value),cell?replacedialog::IDKind::CellName:
                        replacedialog::IDKind::CardGroup,true);
                };
                context->ref_use_count = [this,weak=std::weak_ptr<std::atomic_bool>(lifecycle_alive_)] {
                    const auto alive=weak.lock();
                    if (alive && alive->load(std::memory_order_acquire) && comm_) comm_->refUseCount.call();
                };
                context->history_resource_refresh =
                    [this,weak=std::weak_ptr<std::atomic_bool>(lifecycle_alive_),
                     summary=std::weak_ptr<::cwxeditor::cwx::summary::Summary>(source_summary)](
                        const std::vector<std::string>& cards,const std::vector<std::string>& backs) {
                        const auto current=[&] {
                            const auto alive=weak.lock();
                            return alive && alive->load(std::memory_order_acquire) &&
                                comm_ && summary_==summary.lock();
                        };
                        for (const auto& path:cards) { if (!current()) return; comm_->refMenuCard.call(path); }
                        for (const auto& path:backs) { if (!current()) return; comm_->refBgImage.call(path); }
                        if (!current()) return;
                        comm_->refUseCount.call();
                        if (!current()) return;
                        comm_->refreshToolBar();
                    };
                auto menu_registry =
                    std::make_shared<areawindow::MenuActionRegistry>();
                context->menu_registry = menu_registry.get();
                lifetime->menu_registry = menu_registry;
                scene_menu_registries_[key] = menu_registry;
                lifetime->disconnect.emplace_back([this, key, menu_registry] {
                    const auto found = scene_menu_registries_.find(key);
                    if (found != scene_menu_registries_.end() &&
                        found->second == menu_registry)
                        scene_menu_registries_.erase(found);
                });
                context->refresh_toolbar = [this,weak=std::weak_ptr<std::atomic_bool>(lifecycle_alive_)] {
                    const auto alive=weak.lock();
                    if (alive && alive->load(std::memory_order_acquire) && comm_) comm_->refreshToolBar();
                };
                context->close_panel = [this](const WidgetPtr& panel) {
                    if (!docking_folder_ || !panel) return;
                    const String control_key = docking_folder_->keyFromCtrl(panel);
                    if (!control_key.empty()) (void)dockCloseCtrl(control_key);
                };
                context->set_title = [this](const WidgetPtr& panel,
                                            const StringView title) {
                    if (!docking_folder_ || !panel) return;
                    const String control_key = docking_folder_->keyFromCtrl(panel);
                    if (control_key.empty()) return;
                    (void)docking_folder_->setTabText(control_key, String(title));
                    refreshAllCompactSelectors();
                    if (services_.gui) services_.gui->request_redraw(panel);
                };
                const std::weak_ptr<areawindow::AreaWindowContext> weak_context = context;
                const auto bind = [&lifetime](auto& source, auto handler) {
                    const auto token = source.add(std::move(handler));
                    auto* const source_ptr = &source;
                    lifetime->disconnect.emplace_back([source_ptr, token] {
                        source_ptr->remove(token);
                    });
                };
                bind(comm_->refArea, [weak_context](const commons::AreaPtr value) {
                    if (const auto target = weak_context.lock())
                        target->signals.ref_area.emit(value.get());
                });
                bind(comm_->delArea, [weak_context](const commons::AreaPtr value) {
                    if (const auto target = weak_context.lock())
                        target->signals.del_area.emit(value.get());
                });
                bind(comm_->refBattle, [weak_context](const commons::BattlePtr value) {
                    if (const auto target = weak_context.lock())
                        target->signals.ref_battle.emit(value.get());
                });
                bind(comm_->delBattle, [weak_context](const commons::BattlePtr value) {
                    if (const auto target = weak_context.lock())
                        target->signals.del_battle.emit(value.get());
                });
                bind(comm_->refPackage, [weak_context](const commons::PackagePtr value) {
                    if (const auto target = weak_context.lock())
                        target->signals.ref_package.emit(value.get());
                });
                bind(comm_->delPackage, [weak_context](const commons::PackagePtr value) {
                    if (const auto target = weak_context.lock())
                        target->signals.del_package.emit(value.get());
                });
                // D版と同様、編集可能な EnemyCard/RefCards の SceneView
                // は、CardPane と同じ Cast 通知を購読する。 共通のコン
                // テキストを通じて橋渡しし、単独版と ALL で同じビューの経路を使う。
                bind(comm_->refCast, [weak_context](const auto& cast) {
                    if (const auto target = weak_context.lock())
                        target->signals.ref_cast.emit(cast);
                });
                bind(comm_->delCast, [weak_context](const auto& cast) {
                    if (const auto target = weak_context.lock())
                        target->signals.del_cast.emit(cast);
                });
                bind(comm_->closeAdds, [weak_context](const commons::SummaryPtr value) {
                    if (const auto target = weak_context.lock())
                        target->signals.close_adds.emit(value.get());
                });
                bind(comm_->replText, [weak_context] {
                    if (const auto target = weak_context.lock())
                        target->signals.repl_text.emit();
                });
                bind(comm_->refPreviewValues, [weak_context] {
                    if (const auto target = weak_context.lock()) target->signals.ref_preview_values.emit();
                });
                bind(comm_->refFlagAndStep, [weak_context](const auto&, const auto&, const auto&) {
                    if (const auto target = weak_context.lock()) target->signals.ref_preview_values.emit();
                });
                // D版と同様、変数の削除後にもシーンの参照警告を更新する。
                if (!lib::editor_window_host_options().all_mode) {
                    bind(comm_->delFlagAndStep, [weak_context](const auto&, const auto&, const auto&) {
                        if (const auto target = weak_context.lock()) target->signals.ref_preview_values.emit();
                    });
                }
                // D版と同様、バージョン変更後は編集可能なシナリオの参照警
                // 告を再描画する。 データバージョンの変更では、すべてのエ
                // ディタホストで参照プレビューを更新する。 ALL が変え
                // るのは配置だけであり、プレビュー更新の動作は共通に保つ。
                if (!read_only && source_summary &&
                    !source_summary->scenarioPath().empty()) {
                    const auto refresh_versions = [weak_context] {
                        if (const auto target = weak_context.lock()) target->signals.ref_preview_values.emit();
                    };
                    bind(comm_->refDataVersion, refresh_versions);
                    bind(comm_->refTargetVersion, refresh_versions);
                }
                bind(comm_->refCardState, [weak_context] {
                    if (const auto target = weak_context.lock()) target->signals.ref_card_state.emit();
                });
                bind(comm_->refUndoMax, [weak_context] {
                    if (const auto target = weak_context.lock())
                        target->signals.ref_undo_max.emit();
                });
                if (battle) {
                    const auto owner = std::dynamic_pointer_cast<::cwxeditor::cwx::area::Battle>(resource);
                    if (!owner) return {};
                    scene_open_event_[key] = [this,
                        weak_summary = std::weak_ptr<Summary>(source_summary),
                        weak_owner = std::weak_ptr<::cwxeditor::cwx::area::Battle>(owner)] {
                        const auto source = weak_summary.lock();
                        const auto area = weak_owner.lock();
                        if (!source || !area || !comm_ || !prop_) return;
                        (void)comm_->openAreaEvent(*prop_, source.get(), area, true);
                    };
                    auto opened_event_panel = std::make_shared<std::weak_ptr<commons::TopLevelPanel>>();
                    context->view_callbacks.open_event = [this, owner, source_summary, opened_event_panel](const bool duplicate) {
                        if (!comm_ || !prop_) return;
                        const auto duplicate_base = duplicate
                            ? std::make_shared<commons::TopLevelPanel>()
                            : nullptr;
                        *opened_event_panel = comm_->openAreaEvent(*prop_, source_summary.get(), owner, true,
                                                   duplicate_base);
                    };
                    context->open_event_path = [this, owner, source_summary, opened_event_panel](
                                                   const StringView event_path,
                                                   const bool alternate) {
                        if (!comm_ || !prop_) return;
                        auto panel = opened_event_panel->lock();
                        opened_event_panel->reset();
                        if (!panel) panel = comm_->openAreaEvent(
                            *prop_, source_summary.get(), owner, true);
                        if (panel) (void)panel->openCWXPath(String(event_path), alternate);
                    };
                    using RichBattleWindow = areawindow::BattleSceneWindow;
                    auto editor = std::make_shared<RichBattleWindow>(
                        *context, *prop_, source_summary.get(), ContainerPtr{}, window_, owner,
                        sharedSceneHistory(source_summary, owner), read_only);
                    // チェックボックスでカード・背景セルの固定状態を切り替える設定が変わったら、開いているシーンを更新する。
                    bind(comm_->refSwitchFixedWithCheckBox,
                        [weak = std::weak_ptr<RichBattleWindow>(editor)] {
                            const auto current = weak.lock();
                            if (!current || !current->areaView()) return;
                            auto* view = current->areaView();
                            if (!view->sceneLifetime().expired()) view->refSwitchFixedWithCheckBox();
                        });
                    bind(comm_->refImagePaneSelectionFilter,
                        [weak = std::weak_ptr<RichBattleWindow>(editor)] {
                            const auto current = weak.lock();
                            if (!current || !current->areaView()) return;
                            auto* view = current->areaView();
                            if (!view->sceneLifetime().expired()) view->refImagePaneSelectionFilter();
                        });
                    bind(comm_->refWallpaper, [weak = std::weak_ptr<RichBattleWindow>(editor)] {
                        const auto current = weak.lock();
                        if (!current || !current->areaView()) return;
                        auto* view = current->areaView();
                        if (!view->sceneLifetime().expired()) view->refreshWallpaper();
                    });
                    // D版と同様、有効で編集可能なシーンをファイル改名の通知に登録する。
                    if (!read_only && source_summary && !source_summary->scenarioPath().empty()) {
                        bind(comm_->refPath, [weak=std::weak_ptr<RichBattleWindow>(editor)](
                                const String& oldPath, const String& newPath, const bool directory) {
                            const auto current = weak.lock();
                            if (!current || !current->areaView()) return;
                            auto* view = current->areaView();
                            if (!view->sceneLifetime().expired()) view->refPath(oldPath, newPath, directory);
                        });
                    }
                    debug_battle_scene_windows_[key] = editor;
                    scene_scale_refresh_[key] =
                        [weak = std::weak_ptr<RichBattleWindow>(editor)] {
                            if (const auto current = weak.lock(); current && current->areaView())
                                current->areaView()->refImageScale();
                        };
                    auto editBackgrounds = configureBackgroundDialogs(editor, source_summary);
                    configureSceneCardDialogs(editor, source_summary, std::move(editBackgrounds));
                    debug_scene_create_card_[key] =
                        [weak = std::weak_ptr<RichBattleWindow>(editor)] {
                            if (const auto current = weak.lock(); current && current->areaView())
                                current->areaView()->createCard();
                        };
                     auto panel = std::make_shared<commons::TopLevelPanel>(editor->shell());
                    *duplicate_base = panel;
                    panel->hostRetirementHandler([weak = std::weak_ptr<RichBattleWindow>(editor)] {
                        if (const auto current = weak.lock()) current->dispose();
                    });
                    panel->openedCWXPathProvider([weak = std::weak_ptr(editor)] {
                        const auto current = weak.lock();
                        return current ? current->openedCWXPath() : std::vector<String>{};
                    });
                     panel->title(editor->title());
                     panel->image(editor->image());
                     panel->rememberCWXPath(owner->cwxPath(true));
                    commons::ViewProjection projection;
                    projection.kind = commons::ViewProjection::Kind::Battle;
                    projection.debugMode = [weak = std::weak_ptr<RichBattleWindow>(editor)] {
                        const auto current = weak.lock();
                        return current && current->areaView() && current->areaView()->debugMode();
                    };
                    projection.owner = resource;
                    projection.view = editor->areaView()->root();
                    projection.logicalShell = window_;
                    projection.useCards = true;
                    projection.useBacks = false;
                    projection.readOnly = read_only;
                    panel->viewProjection(std::move(projection));
                    panel->openCWXPathHandler(
                        [editor](String path, const bool activate) {
                            return editor->openCWXPath(path, activate);
                        });
                    lifetime->editor = editor;
                    scenario_panes_.push_back(lifetime);
                    if (lib::editor_window_host_options().all_mode) {
                        all_view_toolbar_factories_[key] =
                            [weak = std::weak_ptr<RichBattleWindow>(editor)]() -> WidgetPtr {
                                if (const auto current = weak.lock(); current && current->areaView())
                                    return current->areaView()->allCompactToolbar();
                                return {};
                            };
                    }
                    return panel;
                }
                const auto owner = std::dynamic_pointer_cast<::cwxeditor::cwx::area::Area>(resource);
                if (!owner) return {};
                scene_open_event_[key] = [this,
                    weak_summary = std::weak_ptr<Summary>(source_summary),
                    weak_owner = std::weak_ptr<::cwxeditor::cwx::area::Area>(owner)] {
                    const auto source = weak_summary.lock();
                    const auto area = weak_owner.lock();
                    if (!source || !area || !comm_ || !prop_) return;
                    (void)comm_->openAreaEvent(*prop_, source.get(), area, true);
                };
                auto opened_event_panel = std::make_shared<std::weak_ptr<commons::TopLevelPanel>>();
                context->view_callbacks.open_event = [this, owner, source_summary, opened_event_panel](const bool duplicate) {
                    if (!comm_ || !prop_) return;
                    const auto duplicate_base = duplicate
                        ? std::make_shared<commons::TopLevelPanel>()
                        : nullptr;
                    *opened_event_panel = comm_->openAreaEvent(*prop_, source_summary.get(), owner, true,
                                               duplicate_base);
                };
                context->open_event_path = [this, owner, source_summary, opened_event_panel](
                                               const StringView event_path,
                                               const bool alternate) {
                    if (!comm_ || !prop_) return;
                    auto panel = opened_event_panel->lock();
                    opened_event_panel->reset();
                    if (!panel) panel = comm_->openAreaEvent(
                        *prop_, source_summary.get(), owner, true);
                    if (panel) (void)panel->openCWXPath(String(event_path), alternate);
                };
                using RichAreaWindow = areawindow::AreaSceneWindow;
                auto editor = std::make_shared<RichAreaWindow>(
                    *context, *prop_, source_summary.get(), ContainerPtr{}, window_, owner,
                    sharedSceneHistory(source_summary, owner), read_only);
                // チェックボックスでカード・背景セルの固定状態を切り替える設定が変わったら、開いているシーンを更新する。
                bind(comm_->refSwitchFixedWithCheckBox,
                    [weak = std::weak_ptr<RichAreaWindow>(editor)] {
                        const auto current = weak.lock();
                        if (!current || !current->areaView()) return;
                        auto* view = current->areaView();
                        if (!view->sceneLifetime().expired()) view->refSwitchFixedWithCheckBox();
                    });
                bind(comm_->refImagePaneSelectionFilter,
                    [weak = std::weak_ptr<RichAreaWindow>(editor)] {
                        const auto current = weak.lock();
                        if (!current || !current->areaView()) return;
                        auto* view = current->areaView();
                        if (!view->sceneLifetime().expired()) view->refImagePaneSelectionFilter();
                    });
                bind(comm_->refWallpaper, [weak = std::weak_ptr<RichAreaWindow>(editor)] {
                    const auto current = weak.lock();
                    if (!current || !current->areaView()) return;
                    auto* view = current->areaView();
                    if (!view->sceneLifetime().expired()) view->refreshWallpaper();
                });
                // D版と同様、有効で編集可能なシーンをファイル改名の通知に登録する。
                if (!read_only && source_summary && !source_summary->scenarioPath().empty()) {
                    bind(comm_->refPath, [weak=std::weak_ptr<RichAreaWindow>(editor)](
                            const String& oldPath, const String& newPath, const bool directory) {
                        const auto current = weak.lock();
                        if (!current || !current->areaView()) return;
                        auto* view = current->areaView();
                        if (!view->sceneLifetime().expired()) view->refPath(oldPath, newPath, directory);
                    });
                }
                debug_area_scene_windows_[key]=editor;
                scene_scale_refresh_[key] =
                    [weak = std::weak_ptr<RichAreaWindow>(editor)] {
                        if (const auto current = weak.lock(); current && current->areaView())
                            current->areaView()->refImageScale();
                    };
                auto editBackgrounds = configureBackgroundDialogs(editor, source_summary);
                configureSceneCardDialogs(editor, source_summary, std::move(editBackgrounds));
                debug_scene_create_card_[key] =
                    [weak = std::weak_ptr<RichAreaWindow>(editor)] {
                        if (const auto current = weak.lock(); current && current->areaView())
                            current->areaView()->createCard();
                    };
                 auto panel = std::make_shared<commons::TopLevelPanel>(editor->shell());
                    *duplicate_base = panel;
                    panel->hostRetirementHandler([weak = std::weak_ptr<RichAreaWindow>(editor)] {
                        if (const auto current = weak.lock()) current->dispose();
                    });
                    panel->openedCWXPathProvider([weak = std::weak_ptr(editor)] {
                        const auto current = weak.lock();
                        return current ? current->openedCWXPath() : std::vector<String>{};
                    });
                 panel->title(editor->title());
                 panel->image(editor->image());
                 panel->rememberCWXPath(owner->cwxPath(true));
                commons::ViewProjection projection;
                projection.kind = commons::ViewProjection::Kind::Area;
                projection.owner = resource;
                projection.view = editor->areaView()->root();
                projection.logicalShell = window_;
                projection.useCards = true;
                projection.useBacks = true;
                projection.readOnly = read_only;
                panel->viewProjection(std::move(projection));
                panel->openCWXPathHandler(
                    [editor](String path, const bool activate) {
                        return editor->openCWXPath(path, activate);
                    });
                lifetime->editor = editor;
                scenario_panes_.push_back(lifetime);
                if (lib::editor_window_host_options().all_mode) {
                    all_view_toolbar_factories_[key] =
                        [weak = std::weak_ptr<RichAreaWindow>(editor)]() -> WidgetPtr {
                            if (const auto current = weak.lock(); current && current->areaView())
                                return current->areaView()->allCompactToolbar();
                            return {};
                        };
                }
                return panel;
            });
    };
    const auto registerEvent = [this, pathFromKey](String family) {
        const bool source_aware = family != "use";
        comm_->registerPaneFactory(
            std::move(family),
            [this, pathFromKey, source_aware](String key, bool,
                                              const std::shared_ptr<void>& source_owner)
                -> std::shared_ptr<commons::TopLevelPanel> {
                const auto use_request = !source_aware && source_owner
                    ? std::static_pointer_cast<commons::UseEventRequest>(source_owner)
                    : std::shared_ptr<commons::UseEventRequest>{};
                const auto source_summary = use_request ? use_request->source
                    : source_aware && source_owner
                        ? std::static_pointer_cast<Summary>(source_owner) : summary_;
                if (!source_summary || !prop_) return {};
                const bool read_only = source_summary != summary_;
                auto resource = use_request
                    ? std::static_pointer_cast<::cwxeditor::cwx::path::CWXPath>(use_request->card)
                    : source_summary->findCWXPath(pathFromKey(key));
                eventwindow::EventOwnerHandle owner;
                if (const auto areaOwner = std::dynamic_pointer_cast<::cwxeditor::cwx::area::Area>(resource))
                    owner = eventwindow::EventOwnerHandle::from(areaOwner);
                else if (const auto battleOwner = std::dynamic_pointer_cast<::cwxeditor::cwx::area::Battle>(resource))
                    owner = eventwindow::EventOwnerHandle::from(battleOwner);
                else if (const auto packageOwner = std::dynamic_pointer_cast<::cwxeditor::cwx::area::Package>(resource))
                    owner = eventwindow::EventOwnerHandle::from(packageOwner);
                else if (const auto skillOwner = std::dynamic_pointer_cast<::cwxeditor::cwx::card::SkillCard>(resource))
                    owner = eventwindow::EventOwnerHandle::from(skillOwner);
                else if (const auto itemOwner = std::dynamic_pointer_cast<::cwxeditor::cwx::card::ItemCard>(resource))
                    owner = eventwindow::EventOwnerHandle::from(itemOwner);
                else if (const auto beastOwner = std::dynamic_pointer_cast<::cwxeditor::cwx::card::BeastCard>(resource))
                    owner = eventwindow::EventOwnerHandle::from(beastOwner);
                if (owner.empty()) return {};
                eventwindow::ContentInitializerContext context(
                    const_cast<::cwxeditor::cwx::props::CProps&>(prop_->parent()),
                    prop_->var(), services_);
                context.editor_props = prop_;
                context.editor_commons = comm_;
                context.dialog_owner = window_;
                auto script_diagnostics = std::make_shared<std::vector<std::shared_ptr<
                    scripterrordialog::ScriptErrorDialog>>>();
                const auto props_lifetime = prop_ ? prop_->lifetimeToken()
                                                  : std::weak_ptr<void>{};
                const std::weak_ptr<std::atomic_bool> host_lifetime = lifecycle_alive_;
                context.scriptErrorReporter = [prop = prop_, props_lifetime,
                    host_lifetime, owner = std::weak_ptr<lib::Window>(window_),
                    services = services_, script_diagnostics](
                        const script::CWXScriptException& error,
                        const StringView base,
                        const script::CompileOption& option) {
                    const auto props_alive = props_lifetime.lock();
                    const auto host_alive = host_lifetime.lock();
                    const auto host = owner.lock();
                    if (!prop || !props_alive || !host || !host_alive ||
                        !host_alive->load(std::memory_order_acquire)) return;
                    std::erase_if(*script_diagnostics,
                        [](const auto& entry) { return !entry || entry->closed(); });
                    auto diagnostic = std::make_shared<
                        scripterrordialog::ScriptErrorDialog>(services, host, error,
                            String(base), option,
                            scripterrordialog::presentationFromProps(*prop));
                    script_diagnostics->push_back(diagnostic);
                    (void)diagnostic->open();
                };
                context.refreshToolBar = [this,
                    weak = std::weak_ptr<std::atomic_bool>(lifecycle_alive_)] {
                    const auto life = weak.lock();
                    if (life && life->load(std::memory_order_acquire) && comm_)
                        comm_->refreshToolBar();
                };
                const auto scene_comments = sceneCommentCallbacks();
                context.make_scene_comment_dialog = scene_comments.make_dialog;
                context.subscribe_scene_comment_updates = scene_comments.subscribe_updates;
                context.editBeastCard = [this](
                    Summary* source, const auto& child_owner, const auto& card,
                    const bool child_read_only, const StringView nested_path,
                    const bool shell_activate, lib::Action applying, lib::Action applied,
                    lib::Action closed) {
                    return openBeastCardDialog(card_comm_, source, child_owner, card,
                        child_read_only, nested_path, shell_activate,
                        std::move(applying), std::move(applied), std::move(closed));
                };
                const bool pin_summary_skin = summaryskinbinding::pinsSummarySkin(
                    read_only, source_summary.get());
                const auto pinned_summary_skin = pin_summary_skin && comm_
                    ? comm_->findSkinFromHistory(source_summary.get()) : SkinPtr{};
                const auto pinned_fallback_skin = comm_ ? comm_->skin() : skin_;
                context.skinResolver = [this, source_summary, pin_summary_skin,
                    pinned_summary_skin, pinned_fallback_skin](const Summary* source,
                                                               StringView, StringView) {
                    if (pin_summary_skin && source == source_summary.get())
                        return pinned_summary_skin ? pinned_summary_skin : pinned_fallback_skin;
                    return comm_ ? comm_->findSkinFromHistory(source) : skin_;
                };
                // EventView のDWTパレットは画像を使う。共通の EventWindow
                // は Props から名前・テキストを求められるが、 エディタ
                // が読み込んだ CType の画像一覧を自力では取得できない。
                context.contentImage = [this](const types::CType type) {
                    return images_ ? images_->content(type) : 0U;
                };
                const auto owner_kind = owner.kind();
                // D版形式の単独版ツールバーと ALL のコンパクトな操作欄は、
                // どちらも通常のシーン経路で所有エリア・戦闘を開く。 取り込み元
                // ・読み取り専用のシーンでは、元の Summary を保持する。
                if ((owner_kind == eventwindow::OwnerKind::Area ||
                     owner_kind == eventwindow::OwnerKind::Battle)) {
                    context.returnToScene = [this, source_summary, resource] {
                        if (!comm_ || !prop_) return;
                        if (const auto area = std::dynamic_pointer_cast<
                                ::cwxeditor::cwx::area::Area>(resource))
                            (void)comm_->openAreaScene(*prop_, source_summary.get(),
                                area, true, nullptr);
                        else if (const auto battle = std::dynamic_pointer_cast<
                                     ::cwxeditor::cwx::area::Battle>(resource))
                            (void)comm_->openAreaScene(*prop_, source_summary.get(),
                                battle, true, nullptr);
                    };
                }
                context.openCWXPath = [this,
                    weak = std::weak_ptr<std::atomic_bool>(lifecycle_alive_)](
                    const StringView path, const bool activate) {
                    const auto life = weak.lock();
                    return life && life->load(std::memory_order_acquire) &&
                        openCWXPath(String(path), activate);
                };
                context.findLocalVariableUsers = [this,
                    weak = std::weak_ptr<std::atomic_bool>(lifecycle_alive_)](
                    const std::shared_ptr<::cwxeditor::cwx::path::CWXPath>& object) {
                    const auto life = weak.lock();
                    if (!life || !life->load(std::memory_order_acquire) || !object) return false;
                    namespace core = ::cwxeditor::cwx;
                    const auto find = [this]<typename T, typename ID>(
                        const std::shared_ptr<T>& value, const ID& id) {
                        if (!value) return false;
                        const auto counter = value->useCounter();
                        if (!counter || !std::dynamic_pointer_cast<core::flag::LocalVariableOwner>(counter->owner()))
                            return false;
                        std::vector<replacedialog::CWXPathPtr> paths, sorted;
                        for (const auto& user : counter->values(id))
                            if (user && user->owner()) paths.push_back(user->owner());
                        replacedialog::SortableCWXPath<replacedialog::CWXPathPtr>::sortedPaths(
                            paths, [&](const auto& entry) { sorted.push_back(entry.obj); });
                        const auto place = core::summary::cwxPlace(counter->owner());
                        const String kind = prop_ ? prop_->requiredMsgs().replLocalVariables.value : String{};
                        configureReplaceDialog([place, sorted = std::move(sorted), kind](auto& dialog) {
                            dialog.setFindResult(place, sorted, kind);
                        });
                        return true;
                    };
                    if (const auto value = std::dynamic_pointer_cast<core::flag::Flag>(object))
                        return find(value, core::usecounter::FlagId(value->path()));
                    if (const auto value = std::dynamic_pointer_cast<core::flag::Step>(object))
                        return find(value, core::usecounter::StepId(value->path()));
                    if (const auto value = std::dynamic_pointer_cast<core::flag::Variant>(object))
                        return find(value, core::usecounter::VariantId(value->path()));
                    return false;
                };
                context.findStartUsers = [this,
                    weak = std::weak_ptr<std::atomic_bool>(lifecycle_alive_)](
                    const ::cwxeditor::cwx::event::ContentPtr& start) {
                    const auto life = weak.lock();
                    if (!life || !life->load(std::memory_order_acquire) ||
                        !start || start->type() != types::CType::Start || !start->tree())
                        return false;
                    const auto tree = start->tree();
                    auto sorted = replacedialog::startUserPaths(start);
                    const auto place = ::cwxeditor::cwx::summary::cwxPlace(tree);
                    const String kind = prop_ ? prop_->requiredMsgs().replStartUsers.value : String{};
                    configureReplaceDialog([place, sorted = std::move(sorted), kind](auto& dialog) {
                        dialog.setFindResult(place, sorted, kind);
                    });
                    return true;
                };
                context.eventViewImage = [this, owner_kind](const StringView name)
                    -> lib::ResourceId {
                    if (!images_) return {};
                    if (name == "return_scene")
                        return owner_kind == eventwindow::OwnerKind::Battle
                            ? images_->battleSceneView() : images_->areaSceneView();
                    if (name == "duplicate_event") {
                        switch (owner_kind) {
                        case eventwindow::OwnerKind::Area: return images_->areaEventTreeViewDup();
                        case eventwindow::OwnerKind::Battle: return images_->battleEventTreeViewDup();
                        case eventwindow::OwnerKind::Package: return images_->packageDup();
                        case eventwindow::OwnerKind::SkillCard: return images_->skillDup();
                        case eventwindow::OwnerKind::ItemCard: return images_->itemDup();
                        case eventwindow::OwnerKind::BeastCard: return images_->beastDup();
                        case eventwindow::OwnerKind::Unknown: return {};
                        }
                    }
                    if (name == "undo") return images_->menu(types::MenuID::Undo);
                    if (name == "redo") return images_->menu(types::MenuID::Redo);
                    if (name == "up") return images_->menu(types::MenuID::Up);
                    if (name == "down") return images_->menu(types::MenuID::Down);
                    if (name == "create") return images_->menu(types::MenuID::NewEvent);
                    if (name == "add_fire") return images_->newIgnition();
                    if (name == "edit") return images_->menu(types::MenuID::EditSelection);
                    if (name == "expand") return images_->expandTree();
                    if (name == "collapse") return images_->foldTree();
                    if (name == "detail") return images_->showEventTreeDetail();
                    if (name == "line") return images_->showEventTreeLineNumber();
                    if (name == "select_current")
                        return images_->menu(types::MenuID::SelectCurrentEvent);
                    if (name == "arrow") return images_->evtArrow();
                    if (name == "quick") return images_->menu(types::MenuID::PutQuick);
                    if (name == "select") return images_->menu(types::MenuID::PutSelect);
                    if (name == "continue")
                        return images_->menu(types::MenuID::PutContinue);
                    if (name == "auto_edit") return images_->evtAutoOpen();
                    if (name == "insert_first") return images_->evtInsertFirst();
                    if (name == "templates") return images_->menu(types::MenuID::EvTemplates);
                    if (name == "event_tree") return images_->eventTree();
                    if (name == "event_tree_and") return images_->eventTreeAnd();
                    if (name == "event_tree_selected") return images_->eventTreeSelected();
                    if (name == "event_tree_and_selected")
                        return images_->eventTreeAndSelected();
                    if (name == "player_card") return images_->playerCard();
                    if (name == "menu_card" || name == "enemy_card")
                        return images_->cards();
                    if (name == "card_flag") return images_->cardsWithFlag();
                    if (name == "enemy_escape") return images_->runAwayableEnemyCard();
                    if (name == "enemy_escape_flag")
                        return images_->runAwayableEnemyCardWithFlag();
                    if (name == "fire_default" || name == "start_reference")
                        return images_->defStart();
                    if (name == "fire_keycode") return images_->keyCode();
                    if (name == "fire_keycode_success")
                        return images_->menu(types::MenuID::KeyCodeTimingSuccess);
                    if (name == "fire_keycode_failure")
                        return images_->menu(types::MenuID::KeyCodeTimingFailure);
                    if (name == "fire_keycode_hasnot")
                        return images_->menu(types::MenuID::KeyCodeTimingHasNot);
                    if (name == "fire_round") return images_->round();
                    if (name == "owner") {
                        switch (owner_kind) {
                        case eventwindow::OwnerKind::Area: return images_->area();
                        case eventwindow::OwnerKind::Battle: return images_->battle();
                        case eventwindow::OwnerKind::Package: return images_->packages();
                        case eventwindow::OwnerKind::SkillCard: return images_->skill();
                        case eventwindow::OwnerKind::ItemCard: return images_->item();
                        case eventwindow::OwnerKind::BeastCard: return images_->beast();
                        case eventwindow::OwnerKind::Unknown: break;
                        }
                    }
                    return {};
                };
                const auto duplicate_base = std::make_shared<std::weak_ptr<commons::TopLevelPanel>>();
                auto callbacks = eventwindow::EventWindow::defaultCallbacks();
                const auto ownerImage = context.eventViewImage;
                callbacks.image = [ownerImage](eventwindow::OwnerKind) {
                    return ownerImage ? ownerImage("owner") : lib::ResourceId{};
                };
                callbacks.openScene = [this,
                    weak = std::weak_ptr<std::atomic_bool>(lifecycle_alive_)](
                    const eventwindow::EventOwnerHandle& handle,
                    const SummaryPtr& source, const bool duplicate) {
                    const auto life = weak.lock();
                    if (!life || !life->load(std::memory_order_acquire) ||
                        !comm_ || !prop_ || !source || !handle.resource()) return;
                    const auto base = duplicate
                        ? comm_->areaWindowFrom(handle.resource()->cwxPath(true), false)
                        : std::shared_ptr<commons::TopLevelPanel>{};
                    if (const auto area = std::dynamic_pointer_cast<
                            ::cwxeditor::cwx::area::Area>(handle.resource()))
                        (void)comm_->openAreaScene(*prop_, source.get(), area, true, base);
                    else if (const auto battle = std::dynamic_pointer_cast<
                            ::cwxeditor::cwx::area::Battle>(handle.resource()))
                        (void)comm_->openAreaScene(*prop_, source.get(), battle, true, base);
                };

                // タイトルの更新方針は EventWindow が管理する。 その結
                // 果をローカルのモデルだけでなく、実際のホストのタブにも反映する。
                callbacks.titleChanged = [this, duplicate_base,
                    weak = std::weak_ptr<std::atomic_bool>(lifecycle_alive_)](const StringView text) {
                    const auto alive = weak.lock();
                    const auto panel = duplicate_base->lock();
                    if (!alive || !alive->load(std::memory_order_acquire) || !panel) return;
                    panel->title(String(text));
                    const auto folder = docking_folder_;
                    if (!folder) return;
                    const auto key = folder->keyFromCtrl(panel->shell());
                    if (!key.empty()) (void)folder->setTabText(key, String(text));
                };
                callbacks.view.openDuplicate = [this, duplicate_base,
                    weak = std::weak_ptr<std::atomic_bool>(lifecycle_alive_)] {
                    const auto alive = weak.lock();
                    if (!alive || !alive->load(std::memory_order_acquire) || !comm_) return;
                    if (const auto base = duplicate_base->lock())
                        (void)comm_->duplicatePanel(base, true);
                };
                callbacks.view.statusChanged = [this, duplicate_base,
                    weak = std::weak_ptr<std::atomic_bool>(lifecycle_alive_)](String text) {
                    const auto alive = weak.lock();
                    const auto panel = duplicate_base->lock();
                    if (!alive || !alive->load(std::memory_order_acquire) || !panel || !comm_) return;
                    const auto shell = panel->shell();
                    panel->statusLine(text);
                    publishPaneStatusLine(shell, std::move(text));
                };
                eventwindow::OperationalHistory operational_history;
                // D版と同様、複製した EventWindow は、使用時イベン
                // トだけでなくエリア・戦闘でも 同じ取り込み元・所有者の履歴を
                // 共有する。ただし、パッケージの履歴は意図的に新しく開始する。
                if (!source_aware || owner_kind == eventwindow::OwnerKind::Area ||
                    owner_kind == eventwindow::OwnerKind::Battle) {
                    for (const auto& [existing_key, weak] : event_windows_) {
                        if (const auto existing = weak.lock(); existing &&
                            existing->ownerHandle().resource() == resource &&
                            existing->eventView().summary() == source_summary) {
                            operational_history = existing->operationalHistory();
                            if (operational_history) break;
                        }
                    }
                }
                if (owner_kind == eventwindow::OwnerKind::Area ||
                    owner_kind == eventwindow::OwnerKind::Battle) {
                    if (!operational_history)
                        operational_history = std::make_shared<eventwindow::OperationalHistoryState>();
                    operational_history->core = sharedSceneHistory(source_summary, resource);
                    if (!operational_history->manager)
                        operational_history->manager = std::make_shared<flagspane::UndoManager>(
                            operational_history->core);
                }
                auto editor = std::make_shared<eventwindow::EventWindow>(
                    std::move(context), source_summary, owner, ContainerPtr{}, nullptr,
                    read_only, std::move(callbacks), std::move(operational_history));
                 auto panel = std::make_shared<commons::TopLevelPanel>(editor->shell());
                *duplicate_base = panel;
                const auto disconnects = std::make_shared<std::vector<lib::Action>>();
                panel->hostRetirementHandler([weak = std::weak_ptr<eventwindow::EventWindow>(editor), disconnects] {
                    for (const auto& disconnect : *disconnects) if (disconnect) disconnect();
                    disconnects->clear();
                    if (const auto current = weak.lock()) current->retireHostBindings();
                });
                {
                    const auto comm_life = comm_->lifetimeToken();
                    const auto host_life = std::weak_ptr<std::atomic_bool>(lifecycle_alive_);
                    const auto weak_editor = std::weak_ptr<eventwindow::EventWindow>(editor);
                    const auto weak_panel = std::weak_ptr<commons::TopLevelPanel>(panel);
                    const auto retire = [this, host_life, weak_editor] {
                        const auto alive = host_life.lock();
                        const auto current = weak_editor.lock();
                        if (!alive || !alive->load(std::memory_order_acquire) || !current) return;
                        retireEventHost(current, true);
                    };
                    const auto subscribe = [disconnects, comm_life](auto& signal, auto handler) {
                        const auto connection = signal.add(std::move(handler));
                        disconnects->push_back([comm_life, signal = &signal, connection] {
                            if (!comm_life.expired()) signal->remove(connection);
                        });
                    };
                    if (read_only) {
                        subscribe(comm_->closeAdds, [source_summary, retire](const auto& closed) {
                            if (closed == source_summary) retire();
                        });
                    } else if (source_aware) {
                        const auto deleted = [resource, retire](const auto& value) {
                            if (value && resource.get() == static_cast<const path::CWXPath*>(
                                    static_cast<const event::EventTreeOwner*>(value.get()))) retire();
                        };
                        const auto refreshed = [this, resource, host_life, weak_editor, weak_panel](const auto& value) {
                            const auto alive = host_life.lock();
                            const auto current = weak_editor.lock();
                            if (!alive || !alive->load(std::memory_order_acquire) || !value ||
                                resource.get() != static_cast<const path::CWXPath*>(
                                    static_cast<const event::EventTreeOwner*>(value.get())) ||
                                !current || current->eventView().hostRetired()) return;
                            const auto folder = docking_folder_;
                            current->refreshTitle();
                            if (!alive->load(std::memory_order_acquire) || current->eventView().hostRetired()) return;
                            if (const auto panel = weak_panel.lock()) panel->title(current->title());
                            if (!folder) return;
                            const auto control_key = folder->keyFromCtrl(current->shell());
                            if (control_key.empty()) return;
                            (void)folder->setTabText(control_key, current->title());
                            if (!alive->load(std::memory_order_acquire) || current->eventView().hostRetired()) return;
                            const auto* tab = folder->tab(control_key);
                            const auto old_restore = tab ? tab->restoreKey : String{};
                            const auto colon = old_restore.find(':');
                            const auto suffix = old_restore.find('#');
                            if (colon != String::npos)
                                (void)folder->setRestoreKey(control_key,
                                    old_restore.substr(0, colon + 1) + resource->cwxPath(true) +
                                    (suffix == String::npos ? String{} : old_restore.substr(suffix)));
                        };
                        if (owner.kind() == eventwindow::OwnerKind::Area) {
                            subscribe(comm_->refArea, refreshed);subscribe(comm_->delArea, deleted);
                        } else if (owner.kind() == eventwindow::OwnerKind::Battle) {
                            subscribe(comm_->refBattle, refreshed);subscribe(comm_->delBattle, deleted);
                        } else if (owner.kind() == eventwindow::OwnerKind::Package) {
                            subscribe(comm_->refPackage, refreshed);subscribe(comm_->delPackage, deleted);
                        }
                    } else {
                        const auto deleted = [resource, retire](const auto&, const auto& deleted_card) {
                            if (resource == deleted_card) retire();
                        };
                        const auto refreshed = [this, resource, host_life, weak_editor, weak_panel](const auto& card) {
                            const auto alive = host_life.lock();
                            if (!alive || !alive->load(std::memory_order_acquire) || resource != card) return;
                            if (const auto current = weak_editor.lock()) {
                                current->refreshTitle();
                                if (const auto panel = weak_panel.lock()) panel->title(current->title());
                                if (docking_folder_) {
                                    const auto control_key = docking_folder_->keyFromCtrl(current->shell());
                                    if (!control_key.empty()) {
                                        (void)docking_folder_->setTabText(control_key, current->title());
                                        // 重複識別用の接尾辞を保ったまま、新しいリソースパスを保存する。 実行
                                        // 時の検索には、引き続き有効なオブジェクトそのものの識別情報を使う。
                                        const auto* tab = docking_folder_->tab(control_key);
                                        const auto old_restore = tab ? tab->restoreKey : String{};
                                        const auto suffix = old_restore.find('#');
                                        (void)docking_folder_->setRestoreKey(control_key,
                                            "use:" + resource->cwxPath(true) +
                                            (suffix == String::npos ? String{} : old_restore.substr(suffix)));
                                    }
                                }
                            }
                        };
                        if (owner.kind() == eventwindow::OwnerKind::SkillCard) {
                            subscribe(comm_->delSkill, deleted);
                            subscribe(comm_->refSkill, refreshed);
                        } else if (owner.kind() == eventwindow::OwnerKind::ItemCard) {
                            subscribe(comm_->delItem, deleted);
                            subscribe(comm_->refItem, refreshed);
                        } else if (owner.kind() == eventwindow::OwnerKind::BeastCard) {
                            subscribe(comm_->delBeast, deleted);
                            subscribe(comm_->refBeast, refreshed);
                        }
                    }
                }
                    panel->openedCWXPathProvider([weak = std::weak_ptr<eventwindow::EventWindow>(editor)] {
                    const auto current = weak.lock();
                    return current ? current->openedCWXPath() : std::vector<String>{};
                });
                 panel->title(editor->title());
                 panel->image(editor->image());
                 panel->openCWXPathHandler(
                    [editor](String path, const bool activate) {
                        return editor->openCWXPath(path, activate);
                    });
                if (auto* rich = editor->eventView().richView()) {
                    commons::ViewProjection projection;
                    projection.kind = commons::ViewProjection::Kind::Event;
                    projection.owner = resource;
                    projection.view = rich->control();
                    projection.treeView = rich->eventTreeControl();
                    // 共通のドッキング処理は、論理的なホストを明示的に記録する。 OS
                    // 固有の方針を設けず、SWT の getShell を置き換える。
                    projection.logicalShell = window_;
                    projection.readOnly = read_only;
                    const auto weak = std::weak_ptr<eventwindow::EventWindow>(editor);
                    projection.selectedTree = [weak]() -> commons::EventTreePtr {
                        const auto current = weak.lock();
                        const auto* view = current ? current->eventView().richView() : nullptr;
                        return view ? view->displayedEventTree() : commons::EventTreePtr{};
                    };
                    projection.refreshEventTreeView = [weak] {
                        const auto current = weak.lock();
                        if (current && !current->eventView().hostRetired())
                            current->eventView().refresh();
                    };
                    projection.finishEventTreeEdit = [weak](
                        const commons::EventTreePtr& expectedTree) {
                        const auto current = weak.lock();
                        if (!current || current->eventView().hostRetired()) return false;
                        auto* const view = current->eventView().richView();
                        if (!view || !commons::Commons::sameEventTreeIdentity(
                                         view->displayedEventTree(), expectedTree)) return false;
                        view->editEnter();
                        return !current->eventView().hostRetired() &&
                               current->eventView().richView() == view &&
                               commons::Commons::sameEventTreeIdentity(
                                   view->displayedEventTree(), expectedTree);
                    };
                    projection.toolbox = [weak]() -> PanelPtr {
                        const auto current = weak.lock();
                        const auto* view = current ? current->eventView().richView() : nullptr;
                        return view ? view->contentsPaletteControl() : PanelPtr{};
                    };
                    projection.toolboxSingleton = [weak] {
                        const auto current = weak.lock();
                        if (current) current->eventView().prepareContentsPalette();
                        const auto* view = current ? current->eventView().richView() : nullptr;
                        return view && view->contentsPaletteSingleton();
                    };
                    projection.ensureToolbox = [weak]() -> PanelPtr {
                        const auto current = weak.lock();
                        if (current) current->eventView().prepareContentsPalette();
                        auto* view = current ? current->eventView().richView() : nullptr;
                        return view ? view->ensureContentsPalette() : PanelPtr{};
                    };
                    projection.releaseToolbox = [weak]() -> std::shared_ptr<void> {
                        const auto current = weak.lock();
                        auto* view = current ? current->eventView().richView() : nullptr;
                        return view ? view->releaseContentsPalette() : std::shared_ptr<void>{};
                    };
                    projection.adoptToolbox = [weak](std::shared_ptr<void> value) -> PanelPtr {
                        const auto current = weak.lock();
                        auto* view = current ? current->eventView().richView() : nullptr;
                        return view ? view->adoptContentsPalette(std::move(value)) : PanelPtr{};
                    };
                    projection.rebuildToolbox = [weak] {
                        const auto current = weak.lock();
                        if (current) current->eventView().prepareContentsPalette();
                        auto* view = current ? current->eventView().richView() : nullptr;
                        if (view) view->rebuildContentsPalette();
                    };
                    panel->viewProjection(std::move(projection));
                }
                event_windows_[key] = editor;
                if (auto* rich = editor->eventView().richView()) {
                    const auto last_tree = std::make_shared<std::pair<std::weak_ptr<event::EventTree>, bool>>(
                        rich->displayedEventTree(), static_cast<bool>(rich->displayedEventTree()));
                    rich->selectionChanged.add([this,
                        alive = std::weak_ptr<std::atomic_bool>(lifecycle_alive_),
                        weak = std::weak_ptr<eventwindow::EventWindow>(editor), last_tree] {
                        const auto life = alive.lock();
                        if (!life || !life->load(std::memory_order_acquire) || !comm_) return;
                        const auto current = weak.lock();
                        auto* view = current ? current->eventView().richView() : nullptr;
                        if (!view) return;
                        const auto tree = view->displayedEventTree();
                        if (last_tree->first.lock() == tree && last_tree->second == static_cast<bool>(tree)) return;
                        last_tree->first = tree;
                        last_tree->second = static_cast<bool>(tree);
                        if (!tree) { view->closeToolWindow(); return; }
                        // D版と同様、イベントツリーを設定するとパレットを開く。 ドッキ
                        // ングによってキーが変わる場合があるため、現在のキーを取得する。
                        const auto selected = event_windows_.find(dock_.selected);
                        if (dock_.moving || selected == event_windows_.end() || selected->second.lock() != current) return;
                        (void)comm_->getContentsToolBox(view->eventTreeControl());
                    });
                }
                scenario_panes_.push_back(editor);
                if (lib::editor_window_host_options().all_mode) {
                    all_view_toolbar_factories_[key] =
                        [weak = std::weak_ptr<eventwindow::EventWindow>(editor)]() -> WidgetPtr {
                            if (const auto current = weak.lock())
                                return current->eventView().allCompactToolbar();
                            return {};
                        };
                }
                return panel;
            });
    };
    registerScene("area.scene", false);
    registerScene("battle.scene", true);
    registerEvent("area.event");
    registerEvent("battle.event");
    registerEvent("package");
    comm_->registerPaneFactory(
        "hands",
        [this, pathFromKey](String key, bool, const std::shared_ptr<void>& owner)
            -> std::shared_ptr<commons::TopLevelPanel> {
            auto request = std::static_pointer_cast<commons::HandPaneRequest>(owner);
            // ドッキング復元には保存済みのキーだけがあり、開く要求はない
            // 。 通常の手札は読み込んだシナリオ内で解決し、 取り込み
            // 元の手札を別シナリオの編集可能なデータとして復元しない。
            if (!request && summary_ && key.find("#import:") == String::npos) {
                request = std::make_shared<commons::HandPaneRequest>();
                request->source = summary_;
                request->card = std::dynamic_pointer_cast<::cwxeditor::cwx::card::CastCard>(
                    summary_->findCWXPath(pathFromKey(key)));
            }
            if (!request || !request->source || !request->card) return {};
            auto editor = std::make_shared<cardwindow_ns::CardWindow>(
                card_comm_, card_prop_, request->destination
                    ? cardwindow_ns::CardWindowKind::ImportSourceHand
                    : cardwindow_ns::CardWindowKind::Hand,
                ContainerPtr{}, request->source,
                std::static_pointer_cast<cardwindow_ns::CWXPath>(request->card),
                request->destination);
            // 親のない CardWindow は遅延構築する。 どのホストでも、
            // ドッキング用のコントロールを公開する前に手札のペインを構築する。
            editor->reconstruct();
            dynamic_card_windows_[key] = editor;
            // 最上位の Window ではなく、クライアントパネルをドッキングする。 共通
            // の配置処理は、子の Window を通常のペイン描画から意図的に除外する。
            auto panel = std::make_shared<commons::TopLevelPanel>(
                std::static_pointer_cast<lib::Container>(editor->panel()));
            commons::ViewProjection projection;
            projection.owner = request->card;
            panel->viewProjection(std::move(projection));
            panel->title(editor->title());
            // 動的な手札のドッキングタブには、D版の CardWindow.image のリソースを使う。
            panel->image(editor->image());
            panel->openCWXPathHandler(
                [editor](String path, const bool activate) {
                    return editor->openCWXPath(path, activate);
                });
            // ドッキング履歴は、記憶した所有者パスだけでなく、現在の手札用 CardWindow を読む。
            panel->openedCWXPathProvider(
                [weak = std::weak_ptr(editor)] {
                    const auto current = weak.lock();
                    return current ? current->openedCWXPath() : std::vector<String>{};
                });
            scenario_panes_.push_back(editor);
            return panel;
        });
    registerEvent("use");
}

void MainWindow::loadDockState() {
    dock_.panes.clear();
    dock_.selected.clear();
    pending_dock_state_.clear();
    // ALL は単独エディタと同じ保存済みペイン配置を使う。 復元後にコ
    // ンパクトな選択欄の設定を再適用する。 不正・旧式の配置は、引き続き
    // mountDefaultDock() で標準のペイン構成に戻す。
    if constexpr (!kEnableSavedDockLayout) {
        lib::editor_action_log(
            "Dock layout restore disabled: using canonical data/data_2/work layout");
        return;
    }
    if (dock_state_file_.empty()) return;
    String serialized;
    if (prop_ && prop_->storage()) {
        const auto data = prop_->storage()->read(pathFromUtf8(dock_state_file_));
        if (!data) return;
        serialized.assign(reinterpret_cast<const char*>(data->data()), data->size());
    } else {
        std::ifstream input{pathFromUtf8(dock_state_file_), std::ios::binary};
        if (!input) return;
        std::ostringstream contents;
        contents << input.rdbuf();
        serialized = contents.str();
    }
    const auto first = serialized.find_first_not_of(" \t\r\n");
    if (first != String::npos && serialized[first] == '<') {
        if (isAllMode()) {
            try {
                const auto node = ::cwxeditor::cwx::xml::XNode::parse(serialized);
                const int saved_width = node.attr<int>("allEditorWidth", false, 0);
                const int current_width = static_cast<int>(std::lround(
                    lib::editor_window_host_options().hosted_canvas_size.width));
                // 横幅の異なる画面の分割比率を使うと、ALL のコン
                // パクトな列が使いにくくなる。 付加情報がない場合は
                // 古い保存形式とみなし、 D版の標準の data/data_2/work
                // のサイズを意図的に選ぶ。
                if (saved_width <= 0 || current_width <= 0 ||
                    saved_width != current_width) {
                    lib::editor_action_log(
                        "ALL dock width changed: using canonical data/data_2/work layout");
                    return;
                }
            } catch (const std::exception& exception) {
                utils::debugln("read ALL docking width failed: ", exception.what());
                return;
            }
        }
        pending_dock_state_ = std::move(serialized);
        return;
    }

    // 初期のC++版が使った行単位の形式も読み
    // 、既存の設定を捨てずに新形式へ移行する。
    std::istringstream legacy(serialized);
    std::getline(legacy, dock_.selected);
    String pane;
    while (std::getline(legacy, pane)) {
        if (!pane.empty() && !(isAllMode() && isCardGroupDockKey(pane)))
            dock_.add(std::move(pane));
    }
    if (isAllMode() && isCardGroupDockKey(dock_.selected))
        dock_.selected.clear();
}

bool MainWindow::saveDockState() const {
    if (dock_state_file_.empty()) return true;
    String serialized;
    try {
        // D版と同様、ドッキングの枠組みは保存するが、work
        // 系列からリソースのコントロールを意図的に除く。 エ
        // リア・戦闘・イベントのタブは現在のシナリオに属し、OpenHistory
        // から再構築するため、 ここでも保存すると次回起
        // 動時に二重に開いてしまう。 Summary の
        // 読み込み前の標準ペインは仮表示なので、そこで終
        // 了しても復元待ちの保存済み配置を上書きしない。
        serialized = !pending_dock_state_.empty() ? pending_dock_state_
            : docking_folder_ ? docking_folder_->toNode({String{"work"}}).text()
                              : String{};
        if (isAllMode() && !serialized.empty()) {
            auto node = ::cwxeditor::cwx::xml::XNode::parse(serialized);
            const int current_width = std::max(1, static_cast<int>(std::lround(
                lib::editor_window_host_options().hosted_canvas_size.width)));
            // ALL はD版のドッキング配置に加えてキャンバス幅も保
            // 存する。 保持・復元された配置にはすでにこの属性がある
            // ため、 後の保存では置き換え、終了処理を拒否しない。
            node.remove_attr("allEditorWidth");
            node.new_attr("allEditorWidth", current_width);
            serialized = node.text();
        }
    } catch (const std::exception& exception) {
        utils::debugln("serialize docking layout failed: ", exception.what());
        return false;
    } catch (...) {
        utils::debugln("serialize docking layout failed");
        return false;
    }
    if (prop_ && prop_->storage()) {
        const auto* first = reinterpret_cast<const std::byte*>(serialized.data());
        const std::span<const std::byte> data(first, serialized.size());
        if (!prop_->storage()->write_atomic(
                pathFromUtf8(dock_state_file_), data, true)) {
            utils::debugln("save docking layout through Storage failed");
            return false;
        }
        return true;
    }
    std::error_code error;
    const auto path = pathFromUtf8(dock_state_file_);
    if (!path.parent_path().empty())
        std::filesystem::create_directories(path.parent_path(), error);
    if (error) return false;
    auto temporary = path;
    temporary += std::filesystem::path(".tmp");
    std::ofstream output(temporary,
                         std::ios::binary | std::ios::trunc);
    if (!output) return false;
    output << serialized;
    output.flush();
    if (!output) return false;
    output.close();
    if (!output) return false;
    std::filesystem::rename(temporary, path, error);
    if (error) {
        error.clear();
        std::filesystem::copy_file(temporary, path,
                                   std::filesystem::copy_options::overwrite_existing,
                                   error);
        std::error_code cleanup;
        std::filesystem::remove(temporary, cleanup);
    }
    return !error;
}

void MainWindow::mountPaneControls(const StringView paneKey) {
    if (!docking_folder_ || paneKey.empty()) return;
    const auto pane = docking_folder_->pane(paneKey);
    if (!pane) return;
    const String pairPane = defaultDockPairPane(*docking_folder_);

    const auto add = [this, paneKey, pairPane](String key, WidgetPtr widget,
                                     String title, lib::ResourceId image,
                                     bool select) {
        if (!docking_folder_ || !widget || key.empty() ||
            docking_folder_->control(key)) return;
        try {
            docking_folder_->addFromMemory(
                [widget](const ContainerPtr&, StringView) { return widget; },
                std::move(title), image, std::move(key), String(paneKey),
                pairPane, dockingfolder::Dir::N, select);
        } catch (const std::exception& error) {
            utils::debugln("mount docking pane failed: ", error.what());
        }
    };

    if (paneKey == "data") {
        add("data", area_table_ ? area_table_->panel() : WidgetPtr{},
            data_prop_.areasTabName, data_prop_.tableViewImage, true);
        add("flag", flags_pane_ ? flags_pane_->widget() : WidgetPtr{},
            data_prop_.flagTabName, data_prop_.variableViewImage, false);
        add("file", directory_window_ ? directory_window_->shell() : WidgetPtr{},
            directory_prop_.messages.title, directory_prop_.images.window, false);
        add("coupon", coupon_window_ ? coupon_window_->shell() : WidgetPtr{},
            coupon_window_ ? coupon_window_->title() : String{},
            coupon_window_ ? coupon_window_->image() : 0, false);
    } else if (paneKey == "data_2") {
        add("castCard", cast_window_ ? cast_window_->panel() : WidgetPtr{},
            card_prop_.castName, card_prop_.castImage, true);
        add("skillCard", skill_window_ ? skill_window_->panel() : WidgetPtr{},
            card_prop_.skillName, card_prop_.skillImage, false);
        add("itemCard", item_window_ ? item_window_->panel() : WidgetPtr{},
            card_prop_.itemName, card_prop_.itemImage, false);
        add("beastCard", beast_window_ ? beast_window_->panel() : WidgetPtr{},
            card_prop_.beastName, card_prop_.beastImage, false);
        add("infoCard", info_window_ ? info_window_->panel() : WidgetPtr{},
            card_prop_.infoName, card_prop_.infoImage, false);
    }
}

void MainWindow::presentDockPanel(
    const StringView resourceKey,
    const std::shared_ptr<commons::TopLevelPanel>& panel,
    const StringView paneFamily, const bool select) {
    if (!docking_folder_ || !panel || !panel->shell() ||
        resourceKey.empty() || paneFamily.empty()) {
        return;
    }

    // 配置済みのエディタを開き直す場合は、既存のタブを選択する。 Commons は CWXPath
    // ごとに TopLevelPanel をキャッシュするため、 D版互換のドッキングキーが
    // work/work_1/... でも、ウィジェット自身の識別情報が安定した対応関係となる。
    if (const String existing = docking_folder_->keyFromCtrl(panel->shell());
        !existing.empty()) {
        if (const auto event = event_windows_.find(String(resourceKey));
            event != event_windows_.end())
            event_windows_[existing] = event->second;
        if (const auto scene = scene_menu_registries_.find(String(resourceKey));
            scene != scene_menu_registries_.end())
            scene_menu_registries_[existing] = scene->second;
        if (const auto toolbar = all_view_toolbar_factories_.find(String(resourceKey));
            toolbar != all_view_toolbar_factories_.end())
            all_view_toolbar_factories_[existing] = toolbar->second;
        if (const auto create = debug_scene_create_card_.find(String(resourceKey));
            create != debug_scene_create_card_.end())
            debug_scene_create_card_[existing] = create->second;
        if (const auto open_event = scene_open_event_.find(String(resourceKey));
            open_event != scene_open_event_.end())
            scene_open_event_[existing] = open_event->second;
        if (const auto cards = dynamic_card_windows_.find(String(resourceKey));
            cards != dynamic_card_windows_.end())
            dynamic_card_windows_[existing] = cards->second;
        // D版の Commons.show/rOpen と同様、開くペインのタブは必
        // ず前面に出す。 shellActivate はキーボードフォーカスとウィ
        // ンドウの有効化を移すかだけを決める。 検索結果は shellActivate=false
        // で開くが、主ウィンドウ内には結果を表示する必要がある。
        (void)select;
        dock_.select(existing);
        (void)docking_folder_->select(existing);
        return;
    }

    String targetPane;
    // ALL は意図した独自の配置として、既存の挿入先の方針を維持する。 単独版のD
    // 版互換経路は、フォーカスのあるウィンドウと、移動済みの有効なサイドタブに従う。
    if (!isAllMode()) {
        if (paneFamily == "work" && services_.gui) {
            const auto focused = services_.gui->focused_widget();
            for (const auto& shell : docking_folder_->subWindows()) {
                if (!focused || !lib::window_contains_widget(shell, focused.get())) continue;
                if (const auto pane = docking_folder_->pane(paneFamily, shell))
                    targetPane = docking_folder_->key(pane);
                break;
            }
        } else if (paneFamily == "side") {
            // D版の addPaneFromCtrlMemory と同様、side 系列の既存コントロ
            // ールの親を先に再利用する。 そのコントロールが data ペインへ移動済みの場合も同様。
            const auto controls = docking_folder_->findCtrl(paneFamily, true);
            if (!controls.empty())
                targetPane = docking_folder_->paneKeyFromCtrlKey(controls.front());
        }
    }
    const auto mainPanes = docking_folder_->findPane(paneFamily, false);
    if (targetPane.empty() && !mainPanes.empty()) targetPane = mainPanes.front();
    if (targetPane.empty()) {
        try {
            if (paneFamily == "work") {
                (void)docking_folder_->addPaneAtRoot(
                    dockingfolder::Dir::S, 3, 10, String(paneFamily));
                targetPane = String(paneFamily);
            } else if (paneFamily == "data" && !isAllMode()) {
                // D版の Commons.show と同様、見つからない data 系列のペインは addFromMemory
                // で再作成する。 通常は work ペインの上に配置する。
                const auto works = docking_folder_->findPane("work", false);
                const String base = !works.empty() ? works.front() : docking_folder_->firstKey();
                if (!base.empty()) {
                    const auto created = docking_folder_->addPaneFromMemory(
                        base, base, dockingfolder::Dir::N, 1, 3, String(paneFamily));
                    if (created) targetPane = docking_folder_->key(created);
                }
            } else if (paneFamily == "side") {
                // D版と同様、取り込み元のシナリオ・カードの参照ウィンド
                // ウは作業領域のサイドペインに置く。 初期配置にはサイド
                // ペインがないため、初回使用時に作成する。 シナリオを読
                // み込んだ後でコントロールを黙って捨てないようにする。
                const auto works = docking_folder_->findPane("work", false);
                const String base = !works.empty()
                    ? works.front()
                    : docking_folder_->firstKey();
                if (!base.empty()) {
                    // ALL は意図した配置として従来の5:1の左右分割を維持し、単
                    // 独版はD版の3:1に従う。 両方とも同じドッキング経路を使う。
                    const int sideMainWeight = isAllMode() ? 5 : 3;
                    const auto created = docking_folder_->addPaneFromMemory(
                        base, base, dockingfolder::Dir::E, sideMainWeight, 1,
                        String(paneFamily));
                    if (created) targetPane = docking_folder_->key(created);
                }
            }
        } catch (const std::exception& error) {
            utils::debugln("create dynamic docking pane failed: ", error.what());
        }
    }
    if (targetPane.empty() || !docking_folder_->pane(targetPane)) return;
    // コマンドとタブメニュー用に、D版互換の work/work_1 のコントロー
    // ル識別情報を維持する。 再起動時の復元キーとなるリソースパスは別に保存する。
    String controlKey = paneFamily == "data" ? String(resourceKey)
                                               : docking_folder_->newCtrlKey(String(paneFamily));
    if (docking_folder_->control(controlKey))
        controlKey = docking_folder_->newCtrlKey(String(paneFamily));
    if (const auto event = event_windows_.find(String(resourceKey));
        event != event_windows_.end())
        event_windows_[controlKey] = event->second;
    if (const auto scene = scene_menu_registries_.find(String(resourceKey));
        scene != scene_menu_registries_.end())
        scene_menu_registries_[controlKey] = scene->second;
    if (const auto create = debug_scene_create_card_.find(String(resourceKey));
        create != debug_scene_create_card_.end())
        debug_scene_create_card_[controlKey] = create->second;
    if (const auto open_event = scene_open_event_.find(String(resourceKey));
        open_event != scene_open_event_.end())
        scene_open_event_[controlKey] = open_event->second;
    if (const auto cards = dynamic_card_windows_.find(String(resourceKey));
        cards != dynamic_card_windows_.end())
        dynamic_card_windows_[controlKey] = cards->second;
    if (const auto toolbar = all_view_toolbar_factories_.find(String(resourceKey));
        toolbar != all_view_toolbar_factories_.end())
        all_view_toolbar_factories_[controlKey] = toolbar->second;
    const auto host_lifetime = lifecycle_alive_;
    const auto host_current = [host_lifetime] {
        return host_lifetime && host_lifetime->load(std::memory_order_acquire);
    };
    try {
        const auto shell = panel->shell();
        docking_folder_->addFromMemory(
            [shell](const ContainerPtr&, StringView) { return shell; },
            panel->title().empty() ? String(resourceKey) : panel->title(),
            panel->image(), controlKey, targetPane,
            defaultDockPairPane(*docking_folder_),
            dockingfolder::Dir::N, true,
            prop_ && prop_->var().openTabAtRightOfCurrentTab.value
                ? dockingfolder::NewCtrlLocation::Right
                : dockingfolder::NewCtrlLocation::Last);
        if (!host_current()) return;
        (void)docking_folder_->setRestoreKey(
            controlKey, persistentDockControlKey(resourceKey));
        dock_.add(controlKey);
        dock_.select(controlKey);
        refreshAllCompactSelectors();
        if (services_.gui)
            services_.gui->request_redraw(docking_folder_->component());
    } catch (const std::exception& error) {
        utils::debugln("mount dynamic docking panel failed: ", error.what());
    }
}

void MainWindow::mountDefaultDock() {
    if (!docking_folder_) return;
    const bool all_mode = isAllMode();
    // リソースに結び付いた work タブには、読み込み済みの Summary が
    // 必要。 起動時は標準の固定ペインを先に配置し、保存されたツリーを保持する。
    // openScenario() が Summary の反映後に再試行する。
    if (!pending_dock_state_.empty() && summary_) {
        try {
            const auto node = ::cwxeditor::cwx::xml::XNode::parse(pending_dock_state_);
            auto restored = dockingfolder::DockingFolder::fromNode(
                node, window_, 0,
                 [this, all_mode](StringView key) {
                     return (all_mode && isCardGroupDockKey(key)) ||
                            dockCanVanish(key);
                 },
                 [this, all_mode](const dockingfolder::DockingFolder& restoring, StringView key) {
                     if (all_mode)
                         return isCardGroupDockKey(key) || dockCanVanish(key);
                     if (!dock_.canClose || key.empty()) return false;
                     // D版と同様、canVanish は docking_folder_
                     // に残る古い標準ツリーではなく、 復元中のツリーに対して判定する。
                     const auto main_works = restoring.findPane("work", false).size();
                     if ((prop_ && valueOf(prop_->var().canVanishWorkAreaInMainWindow)) ||
                         main_works == 0U)
                         return !key.starts_with("work") || restoring.findPane("work", true).size() > 1U;
                     if (restoring.isSubWindowPane(key)) return true;
                     return !key.starts_with("work") || main_works > 1U;
                 },
                 [this, all_mode](const ContainerPtr&, StringView key) -> WidgetPtr {
                     if (all_mode && isCardGroupDockKey(key)) return {};
                     if (!comm_) return {};
                     const auto panel = comm_->restoreRegisteredPane(String(key));
                     return panel ? panel->shell() : WidgetPtr{};
                 });
            if (restored) {
                if (all_mode) {
                    // 保存済みの ALL 配置に、デスクトップ版の
                    // cardGroup タブが残っている場合がある
                    // 。 復元ツリーを設定する前に実行時キーと保存済
                    // みの復元キーの両方を除き、 更新後のセッショ
                    // ンに古いペインや見出しが残らないようにする。
                    for (const auto& key : restored->controlKeys()) {
                        const auto* tab = restored->tab(key);
                        if (isCardGroupDockKey(key) ||
                            (tab && isCardGroupDockKey(tab->restoreKey)))
                            (void)restored->discard(key);
                    }
                }
                const auto paneKeys = restored->paneKeys();
                const bool hasWorkPane = std::any_of(
                    paneKeys.begin(), paneKeys.end(), [](const String& key) {
                        return StringView(key).starts_with("work");
                    });
                if (hasWorkPane) {
                    window_->remove(docking_folder_->component());
                    docking_folder_ = std::move(restored);
                    configureDockingFolder();
                    for (const auto& key : docking_folder_->paneKeys())
                        if (!(all_mode && isCardGroupDockKey(key))) dock_.add(key);
                } else {
                    // 初期のC++版の配置には必須の work ペインがなく、
                    // itemCard などのシステムタブが 切り離された子
                    // ウィンドウに残る場合があった。D版は work ペイン
                    // を必ず確保するため、 これは有効なユーザー設定としては
                    // 扱わず、壊れた配置を残す代わりに標準配置を再構築する。
                    utils::debugln("discard obsolete dock layout without work pane");
                }
            }
        } catch (const std::exception& error) {
            utils::debugln("restore docking layout failed: ", error.what());
        }
        pending_dock_state_.clear();
    }
    try {
        const auto paneKeys = docking_folder_->paneKeys();
        const bool hasWorkPane = std::any_of(
            paneKeys.begin(), paneKeys.end(), [](const String& key) {
                return StringView(key).starts_with("work");
            });
        if (!hasWorkPane)
            (void)docking_folder_->addPaneAtRoot(dockingfolder::Dir::S,
                                                  3, 10, "work");
        const auto first = docking_folder_->firstKey();
        if (first.empty()) return;
        if (!docking_folder_->pane("data")) {
            const StringView base = docking_folder_->pane("work")
                                        ? StringView{"work"}
                                        : StringView{first};
            const bool all_mode = lib::editor_window_host_options().all_mode;
            (void)docking_folder_->addPane(base, dockingfolder::Dir::N,
                                             all_mode ? 26 : 3,
                                             all_mode ? 74 : 10, "data");
        }
        if (!docking_folder_->pane("data_2")) {
            (void)docking_folder_->addPane("data", dockingfolder::Dir::E,
                                            5, 7, "data_2");
            if (all_mode) {
                // ALL では、新規配置の上側の仕切りだけを右へ50px移す。
                // 保存済みの配置と、その後のユーザーのドラッグを優先する。
                const auto left = docking_folder_->pane("data");
                const auto right = docking_folder_->pane("data_2");
                const auto adjust = [&](auto&& self, const WidgetPtr& widget) -> bool {
                    if (const auto split = std::dynamic_pointer_cast<lib::SplitView>(widget)) {
                        const auto& children = split->children();
                        if (split->orientation == lib::Orientation::Horizontal &&
                            children.size() == 2U && children[0] == left && children[1] == right) {
                            split->initial_first_extent_offset = 50.0F;
                            return true;
                        }
                    }
                    if (const auto container = std::dynamic_pointer_cast<lib::Container>(widget))
                        for (const auto& child : container->children())
                            if (self(self, child)) return true;
                    return false;
                };
                (void)adjust(adjust, docking_folder_->component());
            }
        }
    } catch (const std::exception& error) {
        utils::debugln("create default docking layout failed: ", error.what());
    }
    for (const auto& key : docking_folder_->paneKeys())
        if (!(all_mode && isCardGroupDockKey(key))) dock_.add(key);
    // 選択リスナーはメニューを再構築する。初期選択は MainWindow
    // のメニュー表の完成前に行われるため、 モデルの
    // 選択が安定するまでリスナーへの通知を遅延させる。
    auto selectionHandlers = std::move(docking_folder_->selectEvent);
    docking_folder_->selectEvent.clear();
    if (!dock_.selected.empty() && docking_folder_->pane(dock_.selected)) {
        (void)docking_folder_->select(dock_.selected);
    } else {
        dock_.select("data");
        (void)docking_folder_->select("data");
    }
    docking_folder_->selectEvent = std::move(selectionHandlers);
    mountPaneControls("data");
    mountPaneControls("data_2");
    configureMainDockLayout();
}

void MainWindow::dispatchMainThread(std::function<void()> action) const {
    if (!action) return;
    if (services_.dispatcher) {
        services_.dispatcher->post(std::move(action));
        return;
    }
    action();
}

void MainWindow::releaseClosedCardDialog(
    const cardwindow_ns::Card* key, const std::shared_ptr<void>& owner) {
    if (!owner) return;

    // 古いダイアログから遅れて来た終了通知で、同じカード用に
    // 新しく開いたダイアログを外さない。 動的な所有者はオブ
    // ジェクトの同一性に基づいて無効化する。 終了コールバッ
    // クが戻るまで、呼び出し元が owner を保持する。
    if (const auto found = active_card_dialogs_.find(key);
        found != active_card_dialogs_.end() && found->second.owner == owner) {
        active_card_dialogs_.erase(found);
    }
    dynamic_panes_.erase(
        std::remove_if(dynamic_panes_.begin(), dynamic_panes_.end(),
                       [&owner](const std::shared_ptr<void>& value) {
                           return value == owner;
                       }),
        dynamic_panes_.end());
}

bool MainWindow::pathExists(StringView path) const {
    if (path.empty()) return false;
    std::error_code ec;
    return std::filesystem::exists(pathFromUtf8(path), ec);
}

String MainWindow::menuText(MenuID id) const {
    if (!prop_) return {};
    return prop_->buildMenu(id);
}

String MainWindow::toolText(MenuID id) const {
    if (!prop_) return {};
    auto text = prop_->buildTool(id);
    return text.empty() ? prop_->buildMenu(id) : text;
}

lib::ResourceId MainWindow::menuImage(const MenuID id) const {
    try {
        return images_ ? images_->menu(id) : 0;
    } catch (const std::exception& error) {
        utils::debugln("menu image fallback: ", error.what());
        return 0;
    }
}

lib::ResourceId MainWindow::toolImage(const MenuID id) const {
    return menuImage(id);
}

String MainWindow::scenarioExtension() const {
    if (!summary_) return {};
    return summary_->legacy() ? ".wsm" : ".wsn";
}

lib::ImageData MainWindow::renderCardImage(
    const cardwindow_ns::SummaryPtr& source,
    const cardwindow_ns::CardPtr& card,
    const bool show_life, bool* has_caption) const {
    if(has_caption) *has_caption=false;
    if (!card || !prop_) return dutils::blankImage(64, 96);

    auto source_skin = skin_;
    if (source && source != summary_ && comm_) {
        if (auto resolved = comm_->findSkinFromHistory(source.get()))
            source_skin = std::move(resolved);
    }
    const auto* skin = source_skin.get();
    dutils::ImageDataWithScale rendered;
    if (const auto value = std::dynamic_pointer_cast<cardwindow_ns::CastCard>(card)) {
        // シナリオのスキン名が空なら、カード枠のスキンは未選択。 D
        // 版と同様、その場合はカード自身の画像レイヤーだけを描く。
        const auto* cast_skin = source && !source->skinName().empty() ? skin : nullptr;
        rendered = dutils::castCardImage(prop_, cast_skin, source.get(), value.get(),
                                         show_life, false, {}, false, {}, services_);
        if(has_caption) *has_caption=cast_skin && rendered.valid();
    } else if (const auto value = std::dynamic_pointer_cast<cardwindow_ns::SkillCard>(card))
        rendered = dutils::skillCardImage(prop_, skin, source.get(), *value, services_);
    else if (const auto value = std::dynamic_pointer_cast<cardwindow_ns::ItemCard>(card))
        rendered = dutils::itemCardImage(prop_, skin, source.get(), *value, services_);
    else if (const auto value = std::dynamic_pointer_cast<cardwindow_ns::BeastCard>(card))
        rendered = dutils::beastCardImage(prop_, skin, source.get(), *value, services_);
    else if (const auto value = std::dynamic_pointer_cast<cardwindow_ns::InfoCard>(card))
        rendered = dutils::infoCardImage(prop_, skin, source.get(), *value, services_);

    // D版の PileImage::setTitle と同様、通常の名前
    // は合成画像に含める。 共通の方針では、旧形式以外の名前全体を横方向
    // に圧縮し、旧形式の見出しは自然な幅を保つ。 名前の末尾を捨てない。
    if (rendered.valid() && !std::dynamic_pointer_cast<cardwindow_ns::CastCard>(card)) {
        const bool caption = dutils::addCardName(rendered, prop_, skin, *card, services_);
        if (has_caption) *has_caption = caption;
    }
    if (rendered.valid()) {
        // D版の CardList と同様、作成したカードの全画素を選択された表示
        // 倍率で描く。 高密度画像の論理サイズ（190x260の画像に対する95x130
        // ）を使うと、 一覧の元画像の矩形が左上だけに切り詰められてしまう。
        auto image = rendered.scaledPercent(prop_->imageScalePercent());
        for (auto& frame : image.frames)
            frame.logical_size = {static_cast<float>(frame.pixel_width),
                                  static_cast<float>(frame.pixel_height)};
        return image;
    }
    return dutils::blankImage(static_cast<int>(prop_->looks().cardSize().width),
                              static_cast<int>(prop_->looks().cardSize().height));
}

void MainWindow::refreshExecEngine() {
    refreshExecEngineImpl(menu_exec_engine_, menu_exec_party_, true);
    const auto auto_path = refreshExecEngineImpl(
        tool_exec_engine_, tool_exec_party_, false);
    const auto icon_item = tool_items_.find(MenuID::ExecEngine);
    // D版と同様、Pythonスクリプトでは実行ファイルのアイコン抽
    // 出を省略し、 その選択でもツールバーの現在の画像を維持する。
    if (icon_item != tool_items_.end() && prop_ && !auto_path.empty() &&
        lower(pathFromUtf8(auto_path).extension().string()) != ".py") {
        float scale = 1.0F;
        try {
            if (services_.system) scale = services_.system->scale_factor();
        } catch (...) {
        }
        if (!std::isfinite(scale) || scale <= 0.0F) scale = 1.0F;
        const auto edge = static_cast<int>(std::clamp(
            std::lround(16.0F * scale), 1L, 4096L));
        const auto item = icon_item->second;
        const std::weak_ptr<std::atomic_bool> lifecycle = lifecycle_alive_;
        dskin::loadIconAsync(prop_->images(), auto_path, edge, edge, services_,
            [item, lifecycle](const lib::ResourceId image) {
                const auto alive = lifecycle.lock();
                if (alive && alive->load(std::memory_order_acquire) && image &&
                    item.valid())
                    item.setImage(image);
            },
            [this, item, lifecycle, auto_path] {
                const auto alive = lifecycle.lock();
                // 共通のアイコン読み込みは非同期なので、完了前に
                // 選択エンジンが変わった場合は古い要求を捨てる。
                return alive && alive->load(std::memory_order_acquire) &&
                       item.valid() && execEnginePath() == auto_path;
            });
    }
    // 自動選択は現在のシナリオのスキンに属する。 メニュー更新で、
    // ユーザーの通常のエンジン設定を自動選択の値に置き換えない。
    updateExecEngineWithPartyName();
    refreshToolBar(MenuTable{});
}

String MainWindow::origScenarioPath() const {
    if (!summary_) return {};
    if (!summary_->readOnlyPath().empty()) return summary_->readOnlyPath();
    if (summary_->useTemp() && !summary_->origZipName().empty())
        return summary_->origZipName();
    return summary_->scenarioPath();
}

String MainWindow::refreshExecEngineImpl(const MenuPtr& menu,
                                         const MenuPtr& withParty,
                                         bool autoSelect) {
    if (menu) {
        menu->clearItems();
        menu->on_open = {}; // 下で再構築するため、以前の共通スナップショットは保持しない。
    }
    if (withParty) {
        withParty->clearItems();
        withParty->on_open = {};
    }
    execution_entries_.clear();
    if (!prop_) return {};

    const String autoPath = execEnginePath();
    dmenu::MenuItemRef autoItem;
    if (autoSelect) {
        autoItem = menu ? menu->append(menuText(MenuID::ExecEngineAuto),
            menuImage(MenuID::ExecEngineAuto), dmenu::MenuPush,
            dmenu::MenuData{MenuID::ExecEngineAuto, {},
                [this] { return canExecEngine(); }}, {},
            [this] { execEngine(); }) : dmenu::MenuItemRef{};
        if (autoItem) menu_items_[MenuID::ExecEngineAuto] = autoItem;
        if (withParty) {
            const auto partyItem = withParty->append(execEngineWithLastPartyText(),
                menuImage(MenuID::ExecEngineWithLastParty), dmenu::MenuPush,
                dmenu::MenuData{MenuID::ExecEngineWithLastParty, {},
                    [this] { return canExecEngineWithLastParty2(); }}, {},
                [this] { execEngineWithLastParty(); });
            menu_items_[MenuID::ExecEngineWithLastParty] = partyItem;
        }
    }

    auto portable_menu = menu ? std::make_shared<lib::Menu>() : nullptr;
    auto portable_party = withParty ? std::make_shared<lib::Menu>() : nullptr;
    partyhistory_ns::ExecEngineMenuOptions options;
    options.filesystem = services_.filesystem.get();
    options.can_exec_with_party = [this] { return canExecEngineWithParty(); };
    options.orig_scenario_path = [this] { return origScenarioPath(); };
    options.selectable_party = [](const partyhistory_ns::EPKey&) { return true; };
    const auto direct_icon_bridge = std::make_shared<PortableIconRefreshBridge>();
    const auto party_icon_bridge = std::make_shared<PortableIconRefreshBridge>();
    const std::weak_ptr<std::atomic_bool> lifecycle = lifecycle_alive_;
    options.application_icon = [this, lifecycle](
        const StringView executable, std::function<void(lib::ResourceId)> completed,
        std::function<bool()> still_needed) {
        const auto alive = lifecycle.lock();
        if (!alive || !alive->load(std::memory_order_acquire) || !prop_) return;
        float scale = 1.0F;
        try {
            if (services_.system) scale = services_.system->scale_factor();
        } catch (...) {
        }
        if (!std::isfinite(scale) || scale <= 0.0F) scale = 1.0F;
        const auto edge = static_cast<int>(std::clamp(
            std::lround(16.0F * scale), 1L, 4096L));
        dskin::loadIconAsync(prop_->images(), executable, edge, edge, services_,
                             std::move(completed), std::move(still_needed));
    };
    options.application_icon_loaded = [direct_icon_bridge, party_icon_bridge](
        const bool with_party_items, const std::size_t index,
        const lib::ResourceId image) {
        const auto& bridge = with_party_items ? party_icon_bridge : direct_icon_bridge;
        const auto destination = bridge->destination.lock();
        if (!destination) return;
        const auto native_index = bridge->first_native_item + index;
        if (native_index >= destination->size()) return;
        dmenu::MenuItemRef item(destination.get(), native_index);
        if (item.valid() && item.native().icon != image) item.setImage(image);
    };
    options.exec_engine = [this](const StringView path, const StringView scenario,
                                 const ExecutionParty& party) {
        execEngineP(String(path), String(scenario), party);
    };
    options.can_exec_classic = [this] { return canExecClassic(); };
    options.app_path = app_path_;
    options.engine_scan_path = valueOf(prop_->var().enginePath);
    options.engine_launch_path = prop_->enginePath();
    options.engine_version_path = prop_->enginePath();
    options.engine_name = prop_->requiredMsgs().menuText(MenuID::ExecEngineMain);
    options.engine_display_name = menuText(MenuID::ExecEngineMain);
    options.classic_engines = valueOf(prop_->var().classicEngines);
    options.read_classic_party_name = [properties = prop_](const StringView path) {
        return ::cwxeditor::cwx::cwl::readPartyName(properties->parent(), path);
    };
    options.classic_menu_text = [](const structs::ClassicEngine& engine) {
        return ::cwxeditor::cwx::menu::MenuProps::buildMenu(
            engine.name, engine.mnemonic, engine.hotkey, false);
    };
    options.is_debug_yado = [properties = prop_](const StringView path) {
        return ::cwxeditor::cwx::cwl::isDebugYado(properties->parent(), path);
    };
    if (images_) {
        options.images = {images_->menu(MenuID::ExecEngine), images_->warning(),
                          images_->team(), images_->yado(), images_->debugYado(),
                          images_->classicEngine()};
    }
    partyhistory_ns::createExecEngineMenu(options, portable_menu, portable_party);
    if (menu && portable_menu && !portable_menu->items.empty()) {
        if (menu->size() != 0U) (void)menu->appendSeparator();
        const auto first = menu->size();
        importPortableMenuItems(portable_menu->items, menu, MenuID::ExecEngineMain);
        direct_icon_bridge->destination = menu;
        direct_icon_bridge->first_native_item = first;
        bindPortableIconRefresh(portable_menu, menu, first);
    }
    if (withParty && portable_party && !portable_party->items.empty()) {
        if (withParty->size() != 0U) (void)withParty->appendSeparator();
        const auto first = withParty->size();
        importPortableMenuItems(portable_party->items, withParty,
                                MenuID::ExecEngineWithParty);
        party_icon_bridge->destination = withParty;
        party_icon_bridge->first_native_item = first;
        bindPortableIconRefresh(portable_party, withParty, first);
    }

    const auto& bookmarks = valueOf(prop_->var().executedPartyBookmarks);
    const auto& history = valueOf(prop_->var().executedParties);
    std::size_t number{};
    const auto addParty = [this, withParty, &number](const ExecutionParty& party) {
        execution_entries_.push_back(party);
        if (!withParty) return;
        const auto number_text = number < 10U ? "&" + std::to_string(number)
                                             : std::to_string(number);
        const auto description = ::cwxeditor::cwx::utils::tryFormat(
            prop_->requiredMsgs().execEngineWithParty.value, party.engineName,
            party.yadoName, party.partyName);
        const bool warning = !existsParty(party);
        const auto warning_image =
            images_ ? images_->warning() : lib::ResourceId{};
        const auto normal_image = party.isClassic
                                      ? (images_ ? images_->classicEngine()
                                                 : lib::ResourceId{})
                                      : menuImage(MenuID::ExecEngineMain);
        const auto image = warning ? warning_image : normal_image;
        const auto item = withParty->append(
            number_text + " " + description, image, dmenu::MenuPush,
            dmenu::MenuData{MenuID::ExecEngineWithParty, {},
                [this, party] { return canExecEngineWithParty() &&
                    (!party.isClassic || canExecClassic()); }}, {},
            [this, party] { execEngineP(party.enginePath, origScenarioPath(), party); });
        if (!warning) {
            // D版と同様、行がエンジンのアイコンを持つ場合だけ、メニュー表示時の警告
            // への切り替えを追加する。 ここでは共通の Menu の更新境界を使う。
            if (auto* entry = withParty->entry(item.index())) {
                const std::weak_ptr<std::atomic_bool> lifetime = lifecycle_alive_;
                entry->refresh = [this, lifetime, item, party, normal_image,
                                  warning_image] {
                    const auto alive = lifetime.lock();
                    if (!alive || !alive->load(std::memory_order_acquire) ||
                        !item.valid()) return;
                    const auto icon = existsParty(party) ? normal_image
                                                         : warning_image;
                    if (!item.valid()) return;
                    item.setImage(icon);
                };
            }
        }
        menu_items_[MenuID::ExecEngineWithParty] = item;
        ++number;
    };
    if (withParty && !bookmarks.empty()) {
        if (withParty->size() != 0U) (void)withParty->appendSeparator();
        for (const auto& party : bookmarks) addParty(party);
    }
    if (withParty && !history.empty()) {
        if (withParty->size() != 0U) (void)withParty->appendSeparator();
        for (const auto& party : history) addParty(party);
    }
    if (withParty && valueOf(prop_->var().executedPartiesMax) > 0) {
        if (withParty->size() != 0U) (void)withParty->appendSeparator();
        const auto item = withParty->append(
            menuText(MenuID::EditExecutedPartyHistory),
            menuImage(MenuID::EditExecutedPartyHistory), dmenu::MenuPush,
            dmenu::MenuData{MenuID::EditExecutedPartyHistory, {},
                [this] { return canEditExecutedPartyHistory(); }}, {},
            [this] { editExecutedPartyHistory(); });
        menu_items_[MenuID::EditExecutedPartyHistory] = item;
    }

    // D版と同様、自動選択メニューには選択された実行ファイルのアイコンを
    // 使う。 Classic エンジンに別の起動用実行ファイルがある場合
    // は、 ツールバーのパスは変えず、エンジン本体のアイコンを表示する。
    if (autoItem && !autoPath.empty()) {
        auto icon_path = utils::nabs(StringView{autoPath});
        for (const auto& classic : options.classic_engines) {
            const auto alternate = utils::nabs(StringView{
                classic.executePath(options.app_path, false)});
            if (!utils::cfnmatch(alternate, icon_path)) continue;
            icon_path = classic.executePath(options.app_path, true);
            break;
        }
        if (menu && !icon_path.empty() &&
            lower(pathFromUtf8(icon_path).extension().string()) != ".py") {
            float scale = 1.0F;
            try {
                if (services_.system) scale = services_.system->scale_factor();
            } catch (...) {
            }
            if (!std::isfinite(scale) || scale <= 0.0F) scale = 1.0F;
            const auto edge = static_cast<int>(std::clamp(
                std::lround(16.0F * scale), 1L, 4096L));
            const std::weak_ptr<std::atomic_bool> lifecycle = lifecycle_alive_;
            const auto requested = std::make_shared<std::atomic_bool>(false);
            const auto request_icon = [this, autoItem, lifecycle, autoPath,
                                       icon_path, edge, requested] {
                const auto alive = lifecycle.lock();
                if (!alive || !alive->load(std::memory_order_acquire) ||
                    !autoItem.valid() ||
                    requested->exchange(true, std::memory_order_acq_rel)) return;
                dskin::loadIconAsync(prop_->images(), icon_path, edge, edge, services_,
                    [autoItem, lifecycle](const lib::ResourceId image) {
                        const auto current = lifecycle.lock();
                        if (current && current->load(std::memory_order_acquire) && image &&
                            autoItem.valid())
                            autoItem.setImage(image);
                    },
                    [this, autoItem, lifecycle, autoPath] {
                        const auto current = lifecycle.lock();
                        return current && current->load(std::memory_order_acquire) &&
                               autoItem.valid() && execEnginePath() == autoPath;
                    });
            };
            auto previous_open = std::move(menu->on_open);
            menu->on_open = [previous_open = std::move(previous_open), request_icon] {
                if (previous_open) previous_open();
                request_icon();
            };
        }
    }
    return autoPath;
}

bool MainWindow::existsParty(const ExecutionParty& party) const {
    if (!prop_) return false;
    return partyhistory_ns::existsParty(
        partyhistory_ns::partyHistorySettings(prop_->var(), &prop_->sys()), party);
}

bool MainWindow::canDelNotExistsParty() const {
    return prop_ && !valueOf(prop_->var().executedParties).empty();
}

void MainWindow::delNotExistsParty() {
    if (!prop_) return;
    auto& entries = prop_->var().executedParties.value;
    const auto previous_size = entries.size();
    entries.erase(std::remove_if(entries.begin(), entries.end(),
                                  [this](const auto& party) { return !existsParty(party); }),
                  entries.end());
    if (entries.size() == previous_size) return;
    comm_->refExecutedParties.call();
    comm_->refreshToolBar();
}

void MainWindow::refExecutedParties() {
    refreshExecEngine();
    // D版と同様、別のエディタに再読み込みを要求する前に現在値を保存する。
    // 解決済みの保存先と現在のドッキング状態を使い、無関係な SettingsDialog
    // の接続は取り込まない。 保存が失敗した場合、設定したパー
    // ティの値はメモリに残し、再読み込み通知の前に失敗を伝える。
    if (prop_) {
        // 現在の DockingFolder と保存用の DockingFolderCTC
        // は別のアダプター。 通常の保存と同じ保持中・実行中の配置を
        // 取得し、シナリオが所有する work コントロールは除く。
        std::shared_ptr<properties::DockingFolderCTC> dock;
        if (!pending_dock_state_.empty())
            dock = std::make_shared<properties::DockingFolderCTC>(
                ::cwxeditor::cwx::xml::XNode::parse(pending_dock_state_));
        else if (docking_folder_)
            dock = std::make_shared<properties::DockingFolderCTC>(
                docking_folder_->toNode({String{"work"}}));
        prop_->flex().save(dock);
    }
    sendReloadProps();
}

void MainWindow::refImportHistory() {
    // このシグナルは汎用のメニュー更新にも使う。 永続化は通知元
    // の取り込み履歴の変更処理でだけ行い、購読側では行わない。
    createImportHistoryMenu(menu_open_import_, true);
    createImportHistoryMenu(tool_open_import_, false);
}

void MainWindow::refreshOuterTools() {
    // D版ではGCがメニューのコールバック中もホストを保持する。 共通のコールバッ
    // クは同期的に破棄できるため、コントロールを保持し、ホストの無効化時に止める。
    const auto alive = lifecycle_alive_;
    const auto menu = menu_bar_;
    const auto toolbars = tool_bars_;
    refreshOuterToolsImpl(menu_outer_tools_);
    refreshOuterToolsImpl(tool_outer_tools_);
    if (menu) menu->refreshItems();
    if (!alive || !alive->load()) return;
    for (const auto& toolbar : toolbars) {
        if (toolbar) toolbar->refreshItems();
        if (!alive->load()) return;
    }
    createFileMenu();
}

void MainWindow::refreshOuterToolsImpl(const MenuPtr& menu) {
    if (!menu || !prop_) return;
    menu->clearItems();
    const auto& tools = valueOf(prop_->var().outerTools);
    for (const auto& tool : tools) {
        const auto label = ::cwxeditor::cwx::menu::MenuProps::buildMenu(
            tool.name.empty() ? tool.command : tool.name,
            tool.mnemonic, tool.hotkey, false);
        menu->append(label, toolImage(MenuID::OuterTools),
                     dmenu::MenuPush,
                     dmenu::MenuData{MenuID::OuterTools, {},
                         [this] { return services_.processes != nullptr; }}, {},
                     [this, tool, weak = std::weak_ptr<std::atomic_bool>(lifecycle_alive_)] {
                         const auto alive = weak.lock();
                         if (!alive || !alive->load(std::memory_order_acquire)) return;
                         String files;
                         if (directory_window_) {
                             const auto selected = directory_window_->selFiles();
                             for (std::size_t index = 0; index < selected.size(); ++index) {
                                 if (index != 0U) files.push_back(' ');
                                 files.push_back('"');
                                 files += selected[index];
                                 files.push_back('"');
                             }
                         }
                         const String scenario = summary_
                             ? summary_->scenarioPath()
                             : pathToUtf8(std::filesystem::current_path());
                         const String command = structs::OuterTool::parse(
                             tool.command, files, scenario);
                         auto work_dir = structs::OuterTool::parse(
                             tool.workDir, files, scenario);
                         const auto app_dir = dirname(app_path_);
                         if (work_dir.empty()) work_dir = app_dir;
                         else if (!pathFromUtf8(work_dir).is_absolute() &&
                                  !app_dir.empty())
                             work_dir = pathToUtf8(pathFromUtf8(app_dir) /
                                                  pathFromUtf8(work_dir));
                         const auto arguments = splitCommandLine(command);
                         if (arguments.empty()) return;
                         const auto processes = services_.processes;
                         if (!processes) return;
                         const auto dialogs = services_.dialogs;
                         const auto title = prop_->requiredMsgs().dlgTitWarning.value;
                         const auto message = ::cwxeditor::cwx::utils::tryFormat(
                             prop_->requiredMsgs().errorExec.value, tool.name);
                         lib::ProcessLaunchRequest request;
                         request.executable = pathFromUtf8(arguments.front());
                         request.arguments.assign(arguments.begin() + 1,
                                                  arguments.end());
                         request.working_directory = pathFromUtf8(work_dir);
                         request.terminate_on_handle_release = false;
                         std::string error;
                         const auto launched = static_cast<bool>(
                             processes->launch(request, error));
                         if (launched || !alive->load(std::memory_order_acquire)) return;
                         if (!error.empty()) setStatusLine(error);
                         if (!alive->load(std::memory_order_acquire)) return;
                         showWarningDialog(dialogs, title, message);
                     });
    }
}

void MainWindow::RefreshTitle::run() {
    if (!owner_) return;
    String value;
    if (owner_->summary_ && owner_->prop_) {
        const auto& messages = owner_->prop_->requiredMsgs();
        value = ::cwxeditor::cwx::utils::tryFormat(
            owner_->summary_->isChanged() ? messages.mainWindowNameChanged.value
                                          : messages.mainWindowName.value,
            owner_->summary_->scenarioName(), owner_->origScenarioPath());
    } else if (owner_->prop_) {
        value = owner_->prop_->requiredMsgs().mainWindowNameEmpty.value;
    } else {
        value = "CWXEditor_C";
    }
    owner_->is_changed_ = owner_->summary_ && owner_->summary_->isChanged();
    static_cast<commons::TopLevelPanel&>(*owner_).title(value);
    if (owner_->window_) owner_->window_->title = value;
}

void MainWindow::refreshTitle() {
    const auto weak = std::weak_ptr<std::atomic_bool>(lifecycle_alive_);
    dispatchMainThread([this, weak] {
        const auto alive = weak.lock();
        if (!alive || !alive->load(std::memory_order_acquire)) return;
        refresh_title_.run();
    });
}

void MainWindow::updateChangeCount() {
    if (!summary_ || !prop_) return;
    const auto mode = valueOf(prop_->var().backupIntervalType);
    if (mode == 1) {
        // D版と同様、数え終えた編集のまとまりは次のUI処理
        // まで保持し、 同じUI処理内の追加変更は数えない。
        if (caught_changing_) return;
        caught_changing_ = true;
        ++change_count_;
    } else {
        // D版と同様、不明な保存値も含め、編集回数以外のすべて
        // のモードで編集カウンターを消す。 後で編集回数による
        // バックアップに戻しても、古い編集回数を持ち越さない。
        change_count_ = 0;
    }
    is_changed_ = summary_->isChanged();
    refreshTitle();
}

void MainWindow::backupThr() {
    const auto previous = last_backup_;
    backupThr(std::chrono::steady_clock::now());
    // 実際の入出力が間隔より長くかかる場合があるため、完了時点から次の間隔を測
    // る。 明示的な時計を渡すオーバーロードでは、引き続き一定した結果を保つ。
    if (last_backup_ != previous) last_backup_ = std::chrono::steady_clock::now();
}

void MainWindow::backupThr(const std::chrono::steady_clock::time_point now) {
    if (quit_ || !prop_ || backup_active_) return;
    if (last_backup_summary_.lock() != summary_) {
        last_backup_summary_ = summary_;
        last_backup_ = now;
        change_count_ = 0;
        caught_changing_ = false;
        if (backup_after_save_summary_.lock() != summary_) {
            backup_after_save_ = false;
            backup_after_save_summary_.reset();
        }
    }
    if (!summary_ || in_saving_) return;
    // ブラウザーの保存は、作業ツリーのスナップショット取得前に決着させる
    // 。 保存失敗後の継続は試行であり、永続化できたことは意味しない。
    if (backup_after_save_) {
        (void)createBackup();
        last_backup_ = now;
        change_count_ = 0;
        return;
    }
    const auto mode = valueOf(prop_->var().backupIntervalType);
    const auto minutes = valueOf(prop_->var().backupInterval);
    const auto edits = valueOf(prop_->var().backupIntervalEdit);
    bool due{};
    if (mode == 0) {
        change_count_ = 0;
        due = minutes > 0 && now - last_backup_ >= std::chrono::minutes(minutes);
    } else if (mode == 1) {
        last_backup_ = now;
        due = edits > 0 && change_count_ >= static_cast<std::uint64_t>(edits);
    }
    if (!due) return;
    // D版と同様、コピー失敗を含め、通常の戻り方なら試行時刻を
    // 進める。 保持件数の管理の失敗は、次の間隔で再試行する。
    (void)createBackup();
    last_backup_ = now;
    change_count_ = 0;
}

bool MainWindow::createBackup() {
    if (in_saving_ || backup_active_) return false;
    if (quit_ || !prop_ || !summary_ || !valueOf(prop_->var().backupEnabled)) return true;
    if (valueOf(prop_->var().backupRefAuthor) &&
        summary_->author() != valueOf(prop_->var().defaultAuthor)) return true;
    backup_active_ = true;
    struct BackupGuard { bool& active; ~BackupGuard() { active = false; } } guard{backup_active_};
    const auto snapshot_summary = summary_;
    const bool resumed = std::exchange(backup_after_save_, false) &&
        backup_after_save_summary_.lock() == snapshot_summary;
    backup_after_save_summary_.reset();
    try {
        if (!resumed && valueOf(prop_->var().autoSave) && summary_->isChanged()) {
            (void)save(window_, true);
            if (summary_ != snapshot_summary || quit_) return false;
            if (in_saving_) {
                backup_after_save_ = true;
                backup_after_save_summary_ = snapshot_summary;
                return false;
            }
        }
        const auto source = pathFromUtf8(snapshot_summary->scenarioPath());
        const auto parent = pathFromUtf8(prop_->backupPath());
        if (source.empty() || parent.empty() || !std::filesystem::exists(source)) return false;
        SaveActivity saving(in_saving_);
        const auto option = createSaveOpt(snapshot_summary->legacy(), false);
        const auto count = valueOf(prop_->var().backupCount);
        const bool archived = valueOf(prop_->var().backupArchived);
        String current;
        bool copied = false;
        bool cleaned = true;
        String previous = old_backup_fingerprint_;
        if (count > 0 && previous.empty()) {
            option.runIo([parent, &previous] {
                std::error_code error;
                const auto backups = ownedBackups(parent, error);
                if (error)
                    throw std::runtime_error("Unable to list existing backups: " +
                                             error.message());
                if (backups.empty()) return;
                const auto& latest = backups.back();
                if (latest.is_directory(error) && !error) {
                    previous = fingerprint(latest.path());
                } else if (latest.is_regular_file(error) && !error) {
                    previous = utils::fileToMD5Digest(latest.path());
                }
                if (error || previous.empty())
                    throw std::runtime_error("Unable to fingerprint latest existing backup");
            });
            if (!previous.empty()) old_backup_fingerprint_ = previous;
        }
        const auto suffix = archived ? (snapshot_summary->legacy() ? ".zip" : ".wsn") : "";
        const auto name = String("cwxeditor_backup_") + timeName(services_) +
            "[" + basename(pathToUtf8(source)) + "]";
        const auto destination = parent / pathFromUtf8(name + suffix);
        bool parent_created{};
        // D版と同様、バックアップの親は存在しない場合だけ作成する。 既存デ
        // ィレクトリを作り直すためだけに、モーダルの入出力段階を増やさない。
        if (count > 0 && !std::filesystem::exists(parent)) {
            option.runIo([parent, &parent_created, sync = &sync_] {
                sync->sync();
                parent_created = std::filesystem::create_directories(parent);
            });
            if (parent_created && comm_) comm_->refreshToolBar();
        }
        if (count > 0 && archived) {
            // 一時保存中も選択された拡張子を保ち、createZip が意
            // 図したルート形式を出力するようにする。 固定した取得データに
            // は保存済みXMLを含め、変更中のモデルを新たに直列化しない。
            const auto staging = parent / pathFromUtf8(name + ".cwxeditor_temp" + suffix);
            struct RemoveStaging {
                std::filesystem::path path;
                ~RemoveStaging() { std::error_code error; std::filesystem::remove(path, error); }
            } remove_staging{staging};
            snapshot_summary->createZip(pathToUtf8(staging), {}, true, sync_, option);
            option.runIo([staging, destination, previous, &current, &copied] {
                current = utils::fileToMD5Digest(staging);
                if (current.empty()) throw std::runtime_error("Unable to fingerprint backup archive");
                if (previous.empty() || current != previous) {
                    std::error_code error;
                    if (!cardwirth::platform::rename_storage_path_no_replace(staging, destination, error))
                        throw std::filesystem::filesystem_error(
                            "Unable to publish backup archive", staging, destination, error);
                    copied = true;
                }
            });
        } else if (count > 0) {
            option.runIo([source, destination, previous, &current, &copied, sync = &sync_] {
                // mkdir の段階が不要でも、コピー前の書き込み完了待ちは維持する。 アー
                // カイブの経路では、Summary::createZip を通じて実施済み。
                sync->sync();
                current = fingerprint(source);
                if (current.empty()) throw std::runtime_error("Unable to fingerprint backup directory");
                if (previous.empty() || current != previous) {
                    String copy_error;
                    copied = copyScenario(source, destination, &copy_error);
                    if (!copied) throw std::runtime_error("Unable to copy backup directory: " + copy_error);
                }
            });
        }
        // 後続の古い項目の削除に失敗しても、スナップショットの成功は記
        // 憶する。 次回はコピーではなく、保持件数の管理を再試行する。
        if (copied) old_backup_fingerprint_ = current;
        option.runIo([parent, count, &cleaned] {
            std::error_code ec;
            const auto backups = ownedBackups(parent, ec);
            if (ec) { cleaned = false; return; }
            const auto remove_count = count >= 0 && backups.size() > static_cast<std::size_t>(count)
                ? backups.size() - static_cast<std::size_t>(count) : 0U;
            for (std::size_t i = 0; i < remove_count; ++i) {
                ec.clear();
                std::filesystem::remove_all(backups[i].path(), ec);
                if (ec) cleaned = false; // 処理を続け、次に条件を満たす試行で再試行する。
            }
        });
        return cleaned;
    } catch (const std::exception& error) {
        setStatusLine("バックアップを作成できません: " + String(error.what()));
        return false;
    }
}

String MainWindow::preserveFatalRecoverySnapshot() noexcept {
    if (!prop_ || !summary_ || !summary_->isChanged() || !system_) return {};
    std::filesystem::path destination;
    try {
        auto parent = pathFromUtf8(prop_->backupPath());
        if (parent.empty() && context_.runtimeContext) {
            parent = context_.runtimeContext->storage_path(
                lib::LogicalRoot::Backup, pathFromUtf8("recovery"));
        }
        if (parent.empty()) return {};
        const auto source = pathFromUtf8(summary_->scenarioPath());
        destination = parent /
            pathFromUtf8(String("cwxeditor_recovery_") + timeName(services_) +
                         "[" + basename(summary_->scenarioPath()) + "]");

        std::error_code error;
        std::filesystem::create_directories(parent, error);
        if (error) throw std::runtime_error(error.message());

        // 素材と未認識のファイルは保持するが、古い管理対象XML群を新し
        // い復旧ディレクトリへコピーしない。 元の全コピーに現在のXML
        // を重ねると、削除済みのエリア・カードXMLが残ってしまう。
        if (!source.empty() && std::filesystem::exists(source, error)) {
            constexpr std::array<StringView, 8> managed_directories{
                "Area", "Battle", "Package", "CastCard",
                "SkillCard", "ItemCard", "BeastCard", "InfoCard"};
            const auto managed_xml = [&](const std::filesystem::path& path) {
                const auto relative = path.lexically_relative(source);
                if (relative.empty()) return false;
                const auto name = lower(pathToUtf8(relative.filename()));
                if (relative.parent_path().empty()) return name == "summary.xml";
                if (name.size() < 4U || name.substr(name.size() - 4U) != ".xml")
                    return false;
                const auto first = pathToUtf8(*relative.begin());
                return std::find(managed_directories.begin(),
                                 managed_directories.end(), first) !=
                       managed_directories.end();
            };
            std::filesystem::create_directories(destination, error);
            if (error) throw std::runtime_error(error.message());
            for (std::filesystem::recursive_directory_iterator current(
                     source, std::filesystem::directory_options::skip_permission_denied,
                     error), end;
                 current != end; current.increment(error)) {
                if (error) throw std::runtime_error(error.message());
                if (!current->is_regular_file(error)) {
                    if (error) throw std::runtime_error(error.message());
                    continue;
                }
                if (managed_xml(current->path())) continue;
                const auto target = destination /
                    current->path().lexically_relative(source);
                std::filesystem::create_directories(target.parent_path(), error);
                if (error) throw std::runtime_error(error.message());
                std::filesystem::copy_file(
                    current->path(), target,
                    std::filesystem::copy_options::overwrite_existing, error);
                if (error) throw std::runtime_error(error.message());
            }
            if (error) throw std::runtime_error(error.message());
        } else {
            std::filesystem::create_directories(destination, error);
            if (error) throw std::runtime_error(error.message());
        }
        std::filesystem::create_directories(destination / "Material", error);
        if (error) throw std::runtime_error(error.message());

        SaveOption recovery;
        recovery.saveSkinName = true;
        recovery.logicalSort = true;
        const auto xmls = summary_->toXMLs(*system_, recovery);
        for (const auto& [directory, files] : xmls) {
            const auto output = directory.empty()
                ? destination : destination / pathFromUtf8(directory);
            std::filesystem::create_directories(output, error);
            if (error) throw std::runtime_error(error.message());
            for (const auto& [name, xml] : files) {
                const auto* bytes = reinterpret_cast<const std::uint8_t*>(xml.data());
                if (!filesync::writeFileAndSync(
                        output / pathFromUtf8(name),
                        lib::ByteView(bytes, xml.size()), sync_.storage()))
                    throw std::runtime_error("Unable to write recovery XML: " + name);
            }
        }
        const String notice =
            "CWXEditor emergency recovery snapshot.\n"
            "The original scenario was not overwritten.\n"
            "Original: " + summary_->scenarioPath() + "\n";
        const auto* noticeBytes =
            reinterpret_cast<const std::uint8_t*>(notice.data());
        if (!filesync::writeFileAndSync(
                destination / "RECOVERY_README.txt",
                lib::ByteView(noticeBytes, notice.size()), sync_.storage()))
            throw std::runtime_error("Unable to write the recovery notice");
        return pathToUtf8(destination);
    } catch (const std::exception& error) {
        utils::debugln("fatal recovery snapshot: ", error.what(),
                       destination.empty() ? String{} :
                           String(" path=") + pathToUtf8(destination));
    } catch (...) {
        utils::debugln("fatal recovery snapshot failed",
                       destination.empty() ? String{} :
                           String(" path=") + pathToUtf8(destination));
    }
    // D版はUI例外後もエディタを動かし続けるが、C++版のイベン
    // トループは意図的に終了する。 別途作る復旧コピーが失敗した
    // 場合は、通常終了の後処理を適用せず元のアーカイブ作業ツリー
    // を保持し、 手動または次回起動時の復旧に使えるようにする。
    if (summary_) {
        const auto retained = summary_->retainTempForRecovery();
        if (!retained.empty())
            utils::fdebugln("Retained temporary scenario after failed fatal recovery: " +
                            retained);
    }
    // 書き込み途中のディレクトリは、診断用に意図的に残す
    // 。 空のパスを返し、完成したものとして通知しない。
    return {};
}

void MainWindow::openScenarioNewWin() {
    if (!prop_) return;
    const auto lifetime = std::weak_ptr<std::atomic_bool>(lifecycle_alive_);
    const auto app_path = app_path_;
    const auto processes = services_.processes;
    const auto dialogs = services_.dialogs;
    const auto warning_title = prop_->requiredMsgs().dlgTitWarning.value;
    const auto warning_format = prop_->requiredMsgs().errorExec.value;
    const auto title = prop_->requiredMsgs().dlgTitOpenScenarioAtNewWin;
    const auto launch = [this, lifetime, app_path, processes, dialogs,
                         warning_title, warning_format](std::optional<String> selected) {
        const auto alive = lifetime.lock();
        if (!alive || !alive->load(std::memory_order_acquire) ||
            !selected || selected->empty()) return;
        const auto executable_name = app_path.empty()
            ? String("CWXEditor_C") : basename(app_path);
        const auto warning = ::cwxeditor::cwx::utils::tryFormat(
            warning_format, executable_name);
        String process_error;
        bool launched = false;
        if (!app_path.empty() && processes) {
            lib::ProcessLaunchRequest request;
            request.executable = pathFromUtf8(app_path);
            request.arguments = {*selected};
            request.working_directory = pathFromUtf8(dirname(app_path));
            request.terminate_on_handle_release = false;
            std::string error;
            launched = static_cast<bool>(processes->launch(request, error));
            process_error = std::move(error);
        }
        if (launched || !alive->load(std::memory_order_acquire)) return;
        String status = warning;
        if (!process_error.empty()) status += "\n" + process_error;
        setStatusLine(std::move(status));
        if (!alive->load(std::memory_order_acquire)) return;
        showWarningDialog(dialogs, warning_title, warning);
    };
    if (isAllMode()) {
        const bool started = loader::selectScenarioAsync(
            *prop_, window_, title, [lifetime, launch, this](
                std::optional<String> selected, String error) mutable {
                const auto alive = lifetime.lock();
                if (!alive || !alive->load(std::memory_order_acquire)) return;
                if (!error.empty()) {
                    setStatusLine("シナリオ選択ダイアログを開けませんでした: " + error);
                    return;
                }
                launch(std::move(selected));
            }, services_);
        const auto alive = lifetime.lock();
        if (!started && alive && alive->load(std::memory_order_acquire))
            setStatusLine("シナリオ選択ダイアログを開けませんでした。");
        return;
    }
    auto selected = loader::selectScenario(*prop_, window_, title, services_);
    const auto alive = lifetime.lock();
    if (!alive || !alive->load(std::memory_order_acquire)) return;
    launch(std::move(selected));
}

void MainWindow::createScenarioNewWin() {
    if (!prop_) return;
    const auto lifetime = std::weak_ptr<std::atomic_bool>(lifecycle_alive_);
    const auto app_path = app_path_;
    const auto processes = services_.processes;
    const auto dialogs = services_.dialogs;
    const auto warning_title = prop_->requiredMsgs().dlgTitWarning.value;
    const auto warning_format = prop_->requiredMsgs().errorExec.value;
    smalldialogs::CreateScenarioDialog dialog(*prop_, window_, false, {}, services_);
    if (!dialog.open()) return;
    const auto alive = lifetime.lock();
    if (!alive || !alive->load(std::memory_order_acquire)) return;
    const auto executable_name = app_path.empty()
        ? String("CWXEditor_C") : basename(app_path);
    const auto warning = ::cwxeditor::cwx::utils::tryFormat(
        warning_format, executable_name);
    String process_error;
    bool launched = false;
    if (!app_path.empty() && processes) {
        lib::ProcessLaunchRequest request;
        request.executable = pathFromUtf8(app_path);
        if (dialog.legacy()) {
            request.arguments = {"-createclassic", dialog.name(), dialog.classicDir()};
        } else {
            request.arguments = {"-create", dialog.name(), dialog.skinType(), dialog.skinName()};
        }
        request.working_directory = pathFromUtf8(dirname(app_path));
        request.terminate_on_handle_release = false;
        std::string error;
        launched = static_cast<bool>(processes->launch(request, error));
        process_error = std::move(error);
    }
    if (launched || !alive->load(std::memory_order_acquire)) return;
    String status = warning;
    if (!process_error.empty()) status += "\n" + process_error;
    setStatusLine(std::move(status));
    if (!alive->load(std::memory_order_acquire)) return;
    showWarningDialog(dialogs, warning_title, warning);
}

void MainWindow::createScenario() {
    if (!prop_) return;
    const bool web = lib::editor_uses_async_dialog_flow(
        services_.gui ? services_.gui->platform() : lib::Platform::Headless);
    if (web && create_scenario_dialog_) {
        if (create_scenario_dialog_->dialog() &&
            create_scenario_dialog_->dialog()->visible()) {
            create_scenario_dialog_->active();
            return;
        }
        if (services_.gui && create_scenario_dialog_->dialog())
            services_.gui->end_modal(create_scenario_dialog_->dialog());
        create_scenario_dialog_.reset();
    }
    if (web) {
        if (!qSaveAsync(QSaveType::create, [this](const bool accepted) {
                if (!accepted) return;
                smalldialogs::Callbacks callbacks;
                callbacks.sync = &sync_;
                auto dialog = std::make_shared<smalldialogs::CreateScenarioDialog>(
                    *prop_, window_, true, std::move(callbacks), services_);
                create_scenario_dialog_ = dialog;
                const std::weak_ptr<smalldialogs::CreateScenarioDialog> weak_dialog =
                    dialog;
                const auto lifetime = lifecycle_alive_;
                const bool started = dialog->openAsync(
                    [this, weak_dialog, lifetime](bool accepted) {
                        if (!lifetime || !lifetime->load(std::memory_order_acquire))
                            return;
                        const auto complete = [this, weak_dialog, lifetime, accepted] {
                            if (!lifetime ||
                                !lifetime->load(std::memory_order_acquire))
                                return;
                            const auto current = weak_dialog.lock();
                            if (!current) return;
                            const auto release = [this, weak_dialog, lifetime] {
                                if (!lifetime ||
                                    !lifetime->load(std::memory_order_acquire))
                                    return;
                                const auto current = weak_dialog.lock();
                                if (current && create_scenario_dialog_ == current)
                                    create_scenario_dialog_.reset();
                            };
                            try {
                                if (accepted) finishCreateScenario(*current);
                            } catch (...) {
                                if (services_.dispatcher)
                                    dispatchMainThread(release);
                                else
                                    release();
                                throw;
                            }
                            if (services_.dispatcher) dispatchMainThread(release);
                            else release();
                        };
                        if (services_.dispatcher)
                            dispatchMainThread(complete);
                        else
                            complete();
                    });
                if (!started && create_scenario_dialog_ == dialog)
                    create_scenario_dialog_.reset();
            }))
            setStatusLine("変更内容の確認ダイアログを開けませんでした。");
        return;
    }

    if (!qSave(QSaveType::create)) return;

    smalldialogs::Callbacks callbacks;
    callbacks.sync = &sync_;
    smalldialogs::CreateScenarioDialog dialog(
        *prop_, window_, true, std::move(callbacks), services_);
    if (!dialog.open()) return;
    finishCreateScenario(dialog);
}

void MainWindow::finishCreateScenario(smalldialogs::CreateScenarioDialog& dialog) {
    SummaryPtr next = dialog.fromTemplate();
    SkinPtr selectedSkin;
    if (next) {
        selectedSkin = skin::Skin::find(
            prop_->parent(), prop_->enginePath(), next->type(),
            next->scenarioPath(), next->legacy(),
            valueOf(prop_->var().classicEngineRegex),
            valueOf(prop_->var().classicDataDirRegex),
            valueOf(prop_->var().classicMatchKey),
            valueOf(prop_->var().classicEngines),
            valueOf(prop_->var().defaultSkin));
        if (!selectedSkin)
            selectedSkin = dskin::findSkin2(*prop_, next->type(), next->skinName());
    } else if (dialog.legacy()) {
        const auto directory = dialog.classicDir();
        std::error_code error;
        std::filesystem::create_directories(pathFromUtf8(directory), error);
        if (error) {
            setStatusLine(::cwxeditor::cwx::utils::tryFormat(prop_->requiredMsgs().saveError.value, directory));
            return;
        }
        next = std::make_shared<Summary>(dialog.name(), dialog.skinType(),
                                         dialog.skinName(), directory, false, true,
                                         prop_->storage());
        selectedSkin = skin::Skin::find(
            prop_->parent(), prop_->enginePath(), next->type(), directory, true,
            valueOf(prop_->var().classicEngineRegex),
            valueOf(prop_->var().classicDataDirRegex),
            valueOf(prop_->var().classicMatchKey),
            valueOf(prop_->var().classicEngines),
            valueOf(prop_->var().defaultSkin));
        if (!selectedSkin)
            selectedSkin = dskin::findSkin2(*prop_, dialog.skinType(), dialog.skinName());
        if (selectedSkin) {
            const auto& areaName = valueOf(prop_->var().newAreaName);
            Summary::createStartArea(
                *next, prop_->parent(), !areaName.empty(), areaName,
                ::cwxeditor::cwx::editor::gui::dwt::dprops::arraySnapshot(
                    prop_->bgImagesDefault(selectedSkin->type())), nullptr,
                prop_->enginePath(), valueOf(prop_->var().classicEngineRegex),
                valueOf(prop_->var().classicDataDirRegex),
                valueOf(prop_->var().classicMatchKey),
                valueOf(prop_->var().classicEngines),
                valueOf(prop_->var().defaultSkin));
            next->saveOverwrite(prop_->parent(), *selectedSkin,
                                createSaveOpt(true, true), sync_);
        }
    } else {
        selectedSkin = dskin::findSkin2(*prop_, dialog.skinType(), dialog.skinName());
        if (!selectedSkin) {
            setStatusLine(::cwxeditor::cwx::utils::tryFormat(
                prop_->requiredMsgs().loadError.value, dialog.skinType() + "/" + dialog.skinName()));
            return;
        }
        const auto& areaName = valueOf(prop_->var().newAreaName);
        next = Summary::createScenario(
            prop_->parent(), prop_->tempPath(), dialog.name(), *selectedSkin,
            !areaName.empty(), areaName,
            ::cwxeditor::cwx::editor::gui::dwt::dprops::arraySnapshot(
                prop_->bgImagesDefault(selectedSkin->type())),
            valueOf(prop_->var().saveSkinName), sync_);
    }
    if (!next || !selectedSkin) {
        setStatusLine(::cwxeditor::cwx::utils::tryFormat(prop_->requiredMsgs().saveError.value, dialog.name()));
        return;
    }
    // D版と同様、Classic やテンプレートからの作成も
    // 含め、すべての作成経路で設定された作者名を適用する。
    next->author(valueOf(prop_->var().defaultAuthor));
    skin_ = std::move(selectedSkin);
    if (comm_) comm_->skin(skin_);
    openScenario(std::move(next), {});
    // D版の MainWindow.createScenario と同様
    // 、新しいシナリオを開けたら、 以前の文書の進捗・エラー表示を消す。
    setStatusLine({});
}

bool MainWindow::createScenarioFromLaunchOption() {
    if (!prop_ || (!opt_.create && !opt_.createclassic)) return false;

    try {
        SummaryPtr next;
        const String name = opt_.createName.empty()
                                ? String(valueOf(prop_->var().newScenarioName))
                                : opt_.createName;
        if (opt_.createclassic) {
            String directory = opt_.createclassicPath;
            if (directory.empty()) {
                smalldialogs::Callbacks callbacks;
                callbacks.services = services_;
                callbacks.sync = &sync_;
                directory = smalldialogs::CreateScenarioDialog::createClassicDir(
                    *prop_, window_, std::move(callbacks));
            }
            // D版と同様、ディレクトリ選択のキャンセルは作成のキャンセル
            // として扱う。 エラーにも、空パスのシナリオにも変換しない。
            if (directory.empty()) return false;

            std::error_code error;
            std::filesystem::create_directories(
                pathFromUtf8(directory), error);
            if (error) {
                setStatusLine(::cwxeditor::cwx::utils::tryFormat(prop_->requiredMsgs().saveError.value, directory));
                return false;
            }
            next = std::make_shared<Summary>(
                name, String{}, String{}, directory, false, true,
                prop_->storage());
            const auto& area_name = valueOf(prop_->var().newAreaName);
            Summary::createStartArea(
                *next, prop_->parent(), !area_name.empty(), area_name,
                ::cwxeditor::cwx::editor::gui::dwt::dprops::arraySnapshot(
                    prop_->bgImagesDefault(valueOf(prop_->var().defaultSkin))), nullptr,
                prop_->enginePath(), valueOf(prop_->var().classicEngineRegex),
                valueOf(prop_->var().classicDataDirRegex),
                valueOf(prop_->var().classicMatchKey),
                valueOf(prop_->var().classicEngines),
                valueOf(prop_->var().defaultSkin));
            if (!comm_) {
                setStatusLine(::cwxeditor::cwx::utils::tryFormat(prop_->requiredMsgs().saveError.value, name));
                return false;
            }
            const auto save_skin = commons::findSkin(*comm_, *prop_, next.get());
            if (!save_skin) {
                setStatusLine(::cwxeditor::cwx::utils::tryFormat(prop_->requiredMsgs().loadError.value,
                    String(valueOf(prop_->var().defaultSkin))));
                return false;
            }
            // D版のClassic起動経路と同様、初回の保存で作者名を保存する。 現在
            // のウィンドウでの新規作成は、意図的に後から設定する従来の順序を維持する。
            next->author(valueOf(prop_->var().defaultAuthor));
            next->saveOverwrite(prop_->parent(), *save_skin,
                                createSaveOpt(true, true), sync_);
        } else {
            const String skin_type = opt_.createSkinType.empty()
                                         ? String(valueOf(prop_->var().defaultSkin))
                                         : opt_.createSkinType;
            const String skin_name = opt_.createSkinName.empty()
                                         ? String(valueOf(prop_->var().defaultSkinName))
                                         : opt_.createSkinName;
            const auto selected_skin = dskin::findSkin2(
                *prop_, skin_type, skin_name);
            if (!selected_skin) {
                setStatusLine(::cwxeditor::cwx::utils::tryFormat(
                    prop_->requiredMsgs().loadError.value, skin_type + "/" + skin_name));
                return false;
            }
            const auto& area_name = valueOf(prop_->var().newAreaName);
            next = Summary::createScenario(
                prop_->parent(), prop_->tempPath(), name,
                *selected_skin, !area_name.empty(), area_name,
                ::cwxeditor::cwx::editor::gui::dwt::dprops::arraySnapshot(
                    prop_->bgImagesDefault(selected_skin->type())),
                valueOf(prop_->var().saveSkinName), sync_);
        }
        if (!next) {
            setStatusLine(::cwxeditor::cwx::utils::tryFormat(prop_->requiredMsgs().saveError.value, name));
            return false;
        }
        next->author(valueOf(prop_->var().defaultAuthor));
        openScenario(std::move(next), {});
        setStatusLine({});
        return static_cast<bool>(summary_);
    } catch (const std::exception& error) {
        setStatusLine(::cwxeditor::cwx::utils::tryFormat(prop_->requiredMsgs().saveError.value,
            opt_.createName.empty() ? String(valueOf(prop_->var().newScenarioName)) : opt_.createName) +
            "\n" + error.what());
    } catch (...) {
        setStatusLine(::cwxeditor::cwx::utils::tryFormat(prop_->requiredMsgs().saveError.value,
            opt_.createName.empty() ? String(valueOf(prop_->var().newScenarioName)) : opt_.createName));
    }
    return false;
}

void MainWindow::DTListener::dragEnter(const std::vector<String>& paths) {
    if (owner_ && !paths.empty()) owner_->selected_files_ = paths;
}

void MainWindow::DTListener::drop(const std::vector<String>& paths) {
    if (owner_) owner_->dropFiles(paths);
}

void MainWindow::dropFiles(const std::vector<String>& paths) {
    if (paths.empty() || !qSave(QSaveType::open)) return;
    selected_files_ = paths;
    openScenario(paths.front(), [this] { resetOpt(); });
}

LoadOption MainWindow::loadOption(const Summary* old) const {
    LoadOption option;
    // D版と同様、単独版の新規・再表示のどちらでも現在の分割入出力設定を読む。
    // 以前の Summary は、下の expandXMLs だけに使う。
    option.doubleIO = lib::editor_window_host_options().all_mode
        ? old && old->useTemp() : valueOf(prop_->var().doubleIO);
    option.expandXMLs = old ? old->expandXMLs() : valueOf(prop_->var().expandXMLs);
    option.numberStepToVariantThreshold =
        valueOf(prop_->var().numberStepToVariantThreshold);
    option.textOnly = false;
    option.processFunc = [this, generation = load_generation_,
                          alive = std::weak_ptr<std::atomic_bool>(lifecycle_alive_)](String name, String value) {
        const auto gate = alive.lock();
        if (gate && gate->load(std::memory_order_acquire) && generation == load_generation_ && !close_pending_) {
            auto* window = const_cast<MainWindow*>(this);
            // D版と同様、翻訳済みの loadingWithFile メッセージでこの進捗行を整形する。
            window->setStatusLine(::cwxeditor::cwx::utils::tryFormat(
                window->prop_->requiredMsgs().loadingWithFile.value, name, value));

        }
    };
    option.progressDetail = [this, generation = load_generation_,
        alive = std::weak_ptr<std::atomic_bool>(lifecycle_alive_)](String stage, String path) {
        const auto gate = alive.lock();
        if (!gate || !gate->load(std::memory_order_acquire) || generation != load_generation_ || close_pending_) return;
        auto* window = const_cast<MainWindow*>(this);
        window->setScenarioLoadingPhase({}, stage + "\n対象: " + path, {});
    };
    return option;
}

void MainWindow::reload() {
    if (!summary_ || !qSave(QSaveType::reload) || !prop_) return;
    reloadCurrentScenario();
}

void MainWindow::reloadCurrentScenario() {
    if (!summary_ || !prop_) return;
    const auto old = summary_;
    const bool read_only = (old->useTemp() && old->origZipName().empty()) ||
                           !old->readOnlyPath().empty();
    const auto reload_from_file = [this, old](String path) {
        if (path.empty()) {
            setStatusLine(::cwxeditor::cwx::utils::tryFormat(
                prop_->requiredMsgs().reloadError.value, summary_->scenarioPath()));
            return;
        }
        // 元のアーカイブ・読み取り専用の取り込み元の読み込みが失敗・キャ
        // ンセルされた場合、 現在表示中の Summary を保持する。
        openScenario(std::move(path), [this] { resetOpt(); }, {}, loadOption(old.get()));
    };

    // D版と同様、圧縮済み・読み取り専用のシナリオは、一時展開先の再解析で
    // はなく元の読み込み元から再読み込みする。 編集可能な展開済みシナリ
    // オと、一時的なClassic変換だけが作業ツリーを再読み込みする。
    if ((old->legacy() || read_only) && !(!read_only && old->useTemp())) {
        String source = old->readOnlyPath();
        if (source.empty()) {
            const auto summary_file = pathFromUtf8(old->scenarioPath()) / "Summary.wsm";
            std::error_code error;
            source = std::filesystem::exists(summary_file, error)
                ? pathToUtf8(summary_file) : old->scenarioPath();
        }
        reload_from_file(std::move(source));
        return;
    }
    if (!old->legacy() && !old->expandXMLs()) {
        reload_from_file(old->origZipName().empty() ? old->readOnlyPath()
                                                     : old->origZipName());
        return;
    }

    openScenario(old->scenarioPath(), [this] { resetOpt(); }, old->prepareReload());
}

void MainWindow::openScenarioM() {
    if (!prop_) return;
    if (!isAllMode()) {
        if (!qSave(QSaveType::open)) return;
        const auto selected = loader::selectScenario(
            *prop_, window_, menuText(MenuID::Open), services_);
        if (!selected || selected->empty()) return;
        openScenario(*selected, {});
        return;
    }

    const std::weak_ptr<std::atomic_bool> lifetime = lifecycle_alive_;
    const auto select_scenario = [this, lifetime] {
        const auto alive = lifetime.lock();
        if (!alive || !alive->load(std::memory_order_acquire) || !prop_) return;
        const bool started = loader::selectScenarioAsync(
            *prop_, window_, menuText(MenuID::Open),
            [this, lifetime](std::optional<String> selected, String error) {
                const auto current = lifetime.lock();
                if (!current || !current->load(std::memory_order_acquire)) return;
                if (!error.empty()) {
                    setStatusLine("シナリオ選択ダイアログを開けませんでした: " + error);
                    return;
                }
                if (!selected || selected->empty()) return;
                openScenario(*selected, {});
            }, services_);
        if (!started)
            setStatusLine("シナリオ選択ダイアログを開けませんでした。");
    };
    if (!qSaveAsync(QSaveType::open, [select_scenario](const bool accepted) {
            if (accepted) select_scenario();
        }))
        setStatusLine("変更内容の確認ダイアログを開けませんでした。");
}

int MainWindow::cmp(StringView a, StringView b) {
    return a == b ? 0 : (a < b ? -1 : 1);
}

int MainWindow::ncmp(StringView a, StringView b) {
    return cmp(lower(String(a)), lower(String(b)));
}

void MainWindow::openScenario(
    SummaryPtr summ, const std::vector<String>& errorFiles,
    const std::vector<loader::ScenarioLoadIssue>& issues) {
    if (!summ || in_saving_) return;
    // 読み込んだシナリオを反映する処理を、段階ごと
    // に1行ずつ記録する。 処理が止まった場合、最
    // 後の行から戻ってこなかった段階を特定できる。
    const auto stage = [](const char* name) {
        lib::editor_action_log(String("Scenario apply stage=") + name);
    };
    stage("begin");
    if (!errorFiles.empty() && prop_ &&
        static_cast<bool>(valueOf(prop_->var().cautionToScenarioLoadErrors))) {
        loader::ScenarioErrorFilesDialog warning(
            *prop_, window_, *summ, errorFiles, issues, services_,
            [this] { if (comm_) comm_->refreshToolBar(); });
        if (!warning.open()) return;
    }

    const auto oldSummary = summary_;

    // D版と同様、文書を置き換える境界で、モードレスの Summary ダイアログ
    // を閉じる。 summary_ の代入後では refScenario() が置
    // 換を検出できず、 残したダイアログの適用操作が以前の文書を変更してしまう。
    if (oldSummary && oldSummary != summ && summary_dialog_) {
        summary_dialog_->forceCancel();
        summary_dialog_.reset();
    }

    // 現在のエディタのスキンはプロセスではなく、開いたシナリオに属する
    // 。 D版の MainWindow::openScenario
    // と同様、エリア・カードのビューを作る前に、 シナリオと、旧形式
    // ならその履歴からスキンを解決する。 起動時・既定のスキンを使う
    // と、MapOfWirth などの素材が誤ったディレクトリを参照
    // し、 XMLや一覧は読み込めてもシーンのプレビューが空になる。
    SkinPtr openedSkin;
    OpenHistory openedHistory;
    if (comm_ && prop_) openedSkin = comm_->findSkinFromHistory(summ.get(), openedHistory);
    if (!openedSkin && prop_)
        openedSkin = dskin::findSkin2(*prop_, summ->type(), summ->skinName());
    if (!openedSkin) openedSkin = skin_;

    // D版と同様、先にスキンを解決し、旧形式の付加情報を取り込み、新形式の空のスキ
    // ン項目を補う。 その後で既定の種類についての警告と、ペインへの接続を行う。
    if (!lib::editor_window_host_options().all_mode && summ->legacy()) {
        if (!openedHistory.path.empty() &&
            (!openedHistory.skinType.empty() || !openedHistory.skinEngine.empty()))
            summ->type(openedHistory.skinType);
        if (!openedHistory.skinName.empty()) summ->skinName(openedHistory.skinName);
    }
    // ペインの再接続と履歴記録の前に、保存する付加情報をD版に合わせる。
    if (prop_ && !summ->legacy()) {
        if (summ->type().empty())
            summ->type(valueOf(prop_->var().defaultSkin));
        if (summ->skinName().empty())
            summ->skinName(valueOf(prop_->var().defaultSkinName));
    }

    // D版と同様、空ではない未知のシナリオのスキン種別は一度警告して正規化
    // する。 現在のスキンの解決後、リソースのビューを構築する前に行う。
    if (prop_ && !summ->type().empty() &&
        summ->type()!=prop_->var().defaultSkin.value && !dskin::hasSkin(*prop_,summ->type())) {
        const String fallback=prop_->var().defaultSkin.value;
        const std::weak_ptr<std::atomic_bool> life=lifecycle_alive_;
        if (services_.dialogs) {
            lib::MessageDialogRequest warning;
            warning.title=prop_->requiredMsgs().dlgTitWarning.value;
            warning.message=utils::tryFormat(prop_->requiredMsgs().useDefaultSkin.value,summ->type(),fallback);
            warning.severity=lib::MessageSeverity::Warning;
            warning.buttons=lib::MessageButtons::OK;
            (void)services_.dialogs->message(warning);
        }
        const auto alive=life.lock();
        if (!alive || !alive->load()) return;
        summ->type(fallback);
    }


    // D版の mainwindow.d と同様、旧形式データに対するClassic 1.50の Exchange
    // 補正を resetChanged() より前に行い、この互換補正だけで変更済みにしない。
    if (openedSkin && prop_ && (summ->legacy() || summ->toWsnProcessing())) {
        const auto exchange_name = openedSkin->actionCardName(
            prop_->sys(), ::cwxeditor::cwx::types::ActionCardType::Exchange);
        const auto use_counter = summ->useCounter();
        if (use_counter) {
            for (const auto& cast : summ->casts()) {
                if (!cast) continue;
                const auto items = cast->items();
                if (items.empty()) continue;
                const auto first_item = summ->baseCard(items.front());
                if (!first_item || first_item->name() != exchange_name) continue;
                for (const auto& user : use_counter->values(
                         ::cwxeditor::cwx::usecounter::toCastId(cast->id()))) {
                    const auto enemy =
                        std::dynamic_pointer_cast<::cwxeditor::cwx::area::EnemyCard>(
                            user->owner());
                    if (enemy)
                        enemy->action(
                            ::cwxeditor::cwx::types::ActionCardType::Exchange, false);
                }
            }
        }
    }

    if (prop_) {
        String menu_card_resource;
        if (openedSkin) {
            ::cwxeditor::cwx::skin::MaskType mask =
                ::cwxeditor::cwx::skin::MaskType::NoMask;
            menu_card_resource = openedSkin->resMenuCard(mask);
        }
        lib::editor_action_log(
            "Scenario skin resolved type=\"" + summ->type() + "\" name=\"" +
            summ->skinName() + "\" engine=\"" + prop_->enginePath() +
            "\" menu_card=\"" + menu_card_resource + "\"");
    }

    // D版と同様、スキン解決後、常設ペインへの反映前に、読み
    // 込んだモデル全体の変更状態を解除する。 新規作成時の
    // 作者設定、ローダーの正規化、旧形式の履歴からのスキン
    // 補正だけで、 開いたばかりの文書を変更済みにしない。
    summ->resetChanged();
    stage("skin-resolved");

    // D版と同様、置換前に現在の文書を記録し、下で新しく
    // 開いた文書を記録する。 最初に開いてから履歴の付加
    // 情報が変わった文書も、その現在の情報を保持する。
    if (summary_) addHistory();
    stage("old-history-added");
    // Commons はパスのキャッシュを消すが、DockingFolder
    // も対応するエリア・戦闘・イベントエディタのタブを所有する。
    // D版と同様、常設ペインを新しい Summary に接続する前
    // に、それらのリソースビューを閉じる。 新しいシナリオへ空のタ
    // ブや古いモデルの作業タブを引き継がないようにする。 ローダー
    // の完了経由ではなく、新規作成・再読み込みで直接文書を置き換え
    // る場合も、 保留中のファイル・取り込み読み込みを無効化する。
    (void)stopBackgroundLoads();
    stage("background-loads-stopped");
    clearScenarioPanes();
    stage("old-panes-cleared");
    summary_ = std::move(summ);
    comm_->summary(summary_);
    stage("summary-published");
    skin_ = std::move(openedSkin);
    // AreaTable は AreaTableContext の複製を保持
    // する。シナリオを開いて skin_ が置き換わるたびに、 スキンへの
    // 生ポインターを更新する。古いままだと、新規エリア作成時に設定された背
    // 景セルを解決する際、 解放済みの起動時・既定スキンを参照してしまう。
    area_table_context_.skin = skin_.get();
    if (area_table_) area_table_->skin(skin_.get());
    if (skin_) {
        comm_->skin(skin_);
    }
    opened_paths_.clear();
    commons::TopLevelPanel::setOpenedCWXPath({});
    stage("ref-scenario-begin");
    comm_->refScenario.call(summary_);
    stage("ref-scenario-end");
    if (!pending_dock_state_.empty()) {
        mountDefaultDock();
        // 復元はドッキング部品を置き換え、Window の末尾
        // に追加する。 伸縮の重みを再設定してステータスバー
        // の前に置き直し、自然な高さで止まらないようにする。
        composeRoot();
    }
    stage("dock-mounted");
    refreshTitle();
    stage("title-refreshed");
    refreshExecEngine();
    stage("exec-engine-refreshed");
    addHistory();
    stage("history-added");
    is_changed_ = summary_->isChanged();
    // D版の MainWindow.openScenario と同様、新しいモデ
    // ル全体と各ペインが有効になってから、 以前の別のアーカイブ作業ツリーを削除
    // する。 同じツリーを意図的に再利用する再読み込みでは、そのまま保持する。
    if (oldSummary && oldSummary != summary_ && oldSummary->useTemp() &&
        !utils::cfnmatch(utils::nabs(StringView{oldSummary->scenarioPath()}),
                         utils::nabs(StringView{summary_->scenarioPath()}))) {
        try {
            oldSummary->delTemp();
        } catch (const std::exception& error) {
            utils::debugln("replaced scenario cleanup: ", error.what());
        } catch (...) {
            utils::debugln("replaced scenario cleanup failed");
        }
    }
    stage("end");
}

void MainWindow::clearScenarioPanes() {
    std::vector<String> keys;
    const auto append = [&keys](const auto& map) {
        for (const auto& [key, value] : map) {
            static_cast<void>(value);
            if (std::find(keys.begin(), keys.end(), key) == keys.end())
                keys.push_back(key);
        }
    };
    append(event_windows_);
    append(dynamic_card_windows_);
    append(scene_menu_registries_);

    // D版と同じ文書置換の境界で、文書に結び付いたビューを無効化する
    // 。 上のマップに専用コントローラーを登録する前に、復元や登録済
    // みの生成処理から 作業ペインが開かれている場合があるため、ドッ
    // キングの全 work ページも対象に含める。 新しい Summary
    // に、古い所有者を持つシーン・イベントのタブを残さない。
    if (docking_folder_) {
        for (const auto& key : docking_folder_->findCtrl("work", true)) {
            if (std::find(keys.begin(), keys.end(), key) == keys.end())
                keys.push_back(key);
        }
    }

    // イベントダイアログは、パスに結び付いたペインの子。 MainWindow
    // のデストラクターと同じ順序で、エディタが存続している間に閉じる。
    for (auto& [key, weak] : event_windows_) {
        static_cast<void>(key);
        if (const auto editor = weak.lock()) editor->closeActiveDialog();
    }
    for (auto& [key, weak] : dynamic_card_windows_) {
        static_cast<void>(key);
        if (const auto editor = weak.lock()) editor->onHostClose();
    }

    if (docking_folder_) {
        for (const String& key : keys) {
            // これは対話的なタブ終了ではなく文書の置換なので、 work
            // ペインの終了方針によって古いリソースを残さない。
            (void)docking_folder_->discard(key);
            dock_.close(key);
        }
    }
    // Commons は、破棄したドッキングコントロールの TopLevelPanel
    // ラッパーをまだ所有する。 scenario_panes_ のイベ
    // ント・シーンのコントローラーが有効なうちにラッパーを解放する。
    // 逆順ではウィジェットのコールバックが破棄済みのコントローラーを指
    // してしまう。 固定のデータ・カード・変数ペインは残し、下で接続し
    // 直す。 closeAll() はエディタ全体の終了時だけに使う。
    if (comm_) comm_->closeScenarioPanes();
    event_windows_.clear();
    dynamic_card_windows_.clear();
    // 動的なシーンビューは、Context から操作の登録先のポインターを借
    // 用する。 その寿命は DynamicSceneLifetime が保持
    // するため、新しい Summary の設定前に古いコマンド経路を外せる。
    scene_menu_registries_.clear();
    scene_view_registries_.clear();
    scene_histories_.clear();
    scenario_panes_.clear();
    debug_scene_create_card_.clear();
    scene_open_event_.clear();
    scene_scale_refresh_.clear();
    debug_area_scene_windows_.clear();
    debug_battle_scene_windows_.clear();
}

void MainWindow::resetOpt() { opt_ = LaunchOption{}; }

void MainWindow::openScenarioImpl(SummaryPtr summ,
                                  const std::vector<String>& errorFiles,
                                  std::vector<String> openPaths,
                                  String selectFile,
                                  const std::vector<loader::ScenarioLoadIssue>& issues) {
    // D版と同様、各シナリオで開いているエディタのパスを OpenHistory
    // の項目に保存する。 openScenario() が履歴を更新する前
    // に取得し、Summary と全ペインの生成処理を登録してから復元する。
    std::vector<String> reconstructionPaths;
    if (summ && comm_ && prop_ && valueOf(prop_->var().reconstruction) &&
        !opt_.noload) {
        OpenHistory historyEntry;
        (void)comm_->findSkinFromHistory(summ.get(), historyEntry);
        reconstructionPaths = history::fullHistToCWXPaths(historyEntry.path);
    }
    const auto requestedSummary = summ;
    openScenario(std::move(summ), errorFiles, issues);
    // 一部だけ読み込んだシナリオをキャンセルした後の
    // 事後条件の検査は、D版にはない安全策として維持
    // する。 読み込み警告ダイアログがそのシナリオを
    // 拒否した場合、openScenario は現在
    // の文書を残す。 拒否した文書のCWXパスやファ
    // イル選択を、残っている元のモデルに適用しない。
    if (summary_ != requestedSummary) return;
    setScenarioLoadingPhase("画面への反映", "前回開いていたタブの復元",
                            "完了（この表示を閉じます）");
    reconstructionPaths.insert(reconstructionPaths.end(), openPaths.begin(),
                               openPaths.end());
    String reconstructionOwner;
    for (const auto& path : reconstructionPaths) {
        // 旧形式の空の履歴欄は、復元するリソースを指さない。 特に、引
        // 用したシナリオにタブがない場合、[] を警告対象にしない。
        if (path.empty()) continue;
        String effectivePath = path;
        const auto category = lower(::cwxeditor::cwx::path::cpcategory(path));
        const bool ownerPath = category == "area" || category == "area:id" ||
                               category == "battle" || category == "battle:id" ||
                               category == "package" || category == "package:id";
        if (ownerPath) {
            reconstructionOwner = ::cwxeditor::cwx::path::cpbody(
                ::cwxeditor::cwx::path::cptop(path));
        } else if (!reconstructionOwner.empty()) {
            const bool relativeChild = category == "event" || category == "playercard" ||
                                       category == "menucard" ||
                                       category == "enemycard" ||
                                       category == "background";
            if (relativeChild) {
                // 相対 EventView パスを修正する前のC++版の履歴
                // との互換処理。 D版は常にシナリオ内の完全な CWXPath
                // を保存するが、古い menucard:N/event:N;nofocus
                // は 直前の所有者の項目を使って復元し、存在し
                // ないファイルパスとしてユーザーに提示しない。
                effectivePath = reconstructionOwner + "/" + path;
            }
        }
        bool opened = false;
        try {
            opened = openCWXPath(effectivePath, true);
        } catch (const std::exception&) {
            opened = false;
        }
        if (opened) continue;
        const auto message = prop_
            ? ::cwxeditor::cwx::utils::tryFormat(
                  prop_->requiredMsgs().cwxPathOpenError.value, path)
            : ::cwxeditor::cwx::utils::tryFormat("パス [%1$s] を開けません。", path);
        setStatusLine(message);
        if (services_.dialogs) {
            lib::MessageDialogRequest warning;
            warning.title = prop_ ? prop_->requiredMsgs().dlgTitWarning.value
                                  : String{"警告 - CWXEditor_C"};
            warning.message = message;
            warning.severity = lib::MessageSeverity::Warning;
            warning.buttons = lib::MessageButtons::OK;
            (void)services_.dialogs->message(warning);
        }
    }
    // openScenario() はリソースのペインがまだない
    // 再構築前に、新しい文書を履歴に記録する。 通常終了後の再起
    // 動でも同じタブが残るよう、復元後に履歴をもう一度更新する。
    if (summary_ && prop_ && valueOf(prop_->var().reconstruction)) addHistory();
    if (!selectFile.empty()) {
        if (context_.selectFile) {
            context_.selectFile(selectFile);
        } else if (!openCWXPath(selectFile, true) && services_.shell) {
            std::string error;
            (void)services_.shell->reveal_path(pathFromUtf8(selectFile), error);
            if (!error.empty()) setStatusLine(error);
        }
    }
    if (comm_) comm_->refreshToolBar();
}

void MainWindow::openScenario() {
    if (!opt_.scenario.empty()) openScenario(opt_.scenario, [this] { resetOpt(); });
}

void MainWindow::openScenario(String fname, std::function<void()> failure, Summary::ReloadOperation reload,
                              std::optional<LoadOption> options) {
    if (in_saving_) {
        if (failure) failure();
        return;
    }
    if (!prop_ || fname.empty() || close_pending_) {
        dismissPreviousScenarioLoading();
        if (failure) failure();
        return;
    }
    // D版と同様、履歴が指す Summary.wsm が存在しない場合はシナリオフォルダーへ切り替え、
    // 現在の Summary.xml や置換後の Summary.wsm を読み込めるようにする。
    const auto requested_summary_path = pathFromUtf8(fname);
    if (lower(pathToUtf8(requested_summary_path.extension())) == ".wsm" &&
        !pathExists(fname)) {
        const auto parent = requested_summary_path.parent_path();
        fname = parent.empty() ? String{"."} : pathToUtf8(parent);
    }
    cancelScenarioLoadTimeout();
    lib::editor_action_log("Scenario open requested path=\"" + fname + "\"");
    // D版と同様、リソースの移動先と、すでに開いているビューの一覧は分ける。
    // .wid/.wex の選択では、先にシナリオディレクトリを解決し、 新し
    // い Summary の読み込み・配置後に、復号したエリア・カードのパスを
    // 復元する。 openScenario(Summary) より前に opened_paths_
    // へ書くと、古いペインの破棄時に失われてしまう。
    auto pending_open_paths = std::move(opt_.openPaths);
    opt_.openPaths.clear();
    if (valueOf(prop_->var().reconstruction) && !opt_.noload) {
        const auto remembered = history::fullHistToCWXPaths(fname);
        pending_open_paths.insert(pending_open_paths.end(), remembered.begin(),
                                  remembered.end());
        fname = history::fullHistToHist(fname);
    }
    summary::decScenarioPath(
        fname, pending_open_paths, valueOf(prop_->var().clickIsOpenEvent));
    auto pending_select_file = std::move(opt_.selectfile);
    opt_.selectfile.clear();
    for (const auto& load : background_loads_) load->cancel();
    pollBackgroundLoads();
    const auto generation = ++load_generation_;
    // 単独起動・ALLとも、起動時の前回シナリオだけでなく、開く・履歴・再読
    // み込みなど 通常の読み込みでも進捗を表示する。起動時や外部シナリオ用の
    // 表示が既にあれば再利用する。 前の読み込みの結果配信（pollBackgroundLoads
    // ）で閉じられないよう、世代を進めてから表示する。
    if (!previous_scenario_loading_)
        showScenarioLoading("シナリオを読み込み中です。\n成功またはエラーが確定するまでお待ちください。");
    const auto edit_generation = edit_generation_;
    const auto control = std::make_shared<loader::LoadControl>();
    control->submission_finished.store(false, std::memory_order_release);
    struct Submission {
        std::shared_ptr<loader::LoadControl> control;
        ~Submission() { control->submission_finished.store(true, std::memory_order_release); }
    } submission{control};
    background_loads_.push_back(control);
    scenario_load_control_ = control;
    const std::weak_ptr<std::atomic_bool> alive = lifecycle_alive_;
    auto status = [alive, this, generation](String value) {
        const auto gate = alive.lock();
        if (gate && gate->load(std::memory_order_acquire) &&
            generation == load_generation_ && !close_pending_)
        {
            setStatusLine(value);
            // 展開の数値進捗はステータス行に表示し、 ダイ
            // アログの現在の操作名と対象を上書きしない。
        }
    };
    auto completed = [alive, this, generation, edit_generation, control, failure = std::move(failure),
                      pending_open_paths = std::move(pending_open_paths),
                      pending_select_file = std::move(pending_select_file)](
                         loader::ScenarioLoadOutcome outcome) mutable {
        const auto gate = alive.lock();
        if (!gate || !gate->load(std::memory_order_acquire)) return;
        if (generation == load_generation_)
            setScenarioLoadingPhase("シナリオファイルの読み込み",
                in_saving_ ? "保存処理の完了待ち（読み込み結果は保存後に反映）"
                           : "読み込み結果の受け取り",
                "画面への反映");
        deliverLoadResult([alive, this, generation, edit_generation, control,
                           failure = std::move(failure), outcome = std::move(outcome),
                           pending_open_paths = std::move(pending_open_paths),
                           pending_select_file = std::move(pending_select_file)]() mutable {
            const auto gate = alive.lock();
            if (!gate || !gate->load(std::memory_order_acquire) || close_pending_ ||
                generation != load_generation_ || scenario_load_control_ != control) return;
            cancelScenarioLoadTimeout();
            scenario_load_control_.reset();
            lib::editor_action_log("Scenario load completed status=" +
                std::to_string(static_cast<int>(outcome.status)) +
                " issues=" + std::to_string(outcome.issues.size()) +
                " reason=\"" + outcome.message + "\"");
            if (outcome.succeeded() && edit_generation != edit_generation_) {
                dismissPreviousScenarioLoading();
                // 途中に編集が入った場合は、安全のため中止する。 保存、再
                // 入する確認、文書の置換、ユーザーの作業の破棄は行わない。
                setStatusLine("読み込み中に編集されたため切り替えを中止しました。現在の編集内容は保持されています。");
                return;
            }
            if (!outcome.succeeded()) {
                dismissPreviousScenarioLoading();
                const auto failure_gate = alive.lock();
                if (!failure_gate || !failure_gate->load(std::memory_order_acquire) ||
                    close_pending_ || generation != load_generation_) return;
                if (!outcome.cancelled()) {
                    if (!isAllMode()) {
                        auto after_warning = [alive, this, generation, owner = window_, gui = services_.gui,
                            message = outcome.message, failure = std::move(failure)]() mutable {
                            const auto callback_gate = alive.lock();
                            if (!callback_gate || !callback_gate->load(std::memory_order_acquire) ||
                                close_pending_ || generation != load_generation_ ||
                                window_ != owner || services_.gui != gui) return;
                            setStatusLine(message);
                            const auto after_status_gate = alive.lock();
                            if (!after_status_gate || !after_status_gate->load(std::memory_order_acquire) ||
                                close_pending_ || generation != load_generation_ ||
                                window_ != owner || services_.gui != gui) return;
                            if (failure) failure(); // MainWindow が無効化される場合があるため、この後はアクセスしない。
                        };
                        showScenarioLoadError(outcome.message, std::move(after_warning));
                        return;
                    }
                    setStatusLine(outcome.message);
                    const auto after_status_gate = alive.lock();
                    if (!after_status_gate || !after_status_gate->load(std::memory_order_acquire) ||
                        close_pending_ || generation != load_generation_) return;
                    if (failure) failure();
                    const auto after_failure_gate = alive.lock();
                    if (!after_failure_gate || !after_failure_gate->load(std::memory_order_acquire) ||
                        close_pending_ || generation != load_generation_) return;
                    // ALL では、既存のモードレスの警告表示を維持する。
                    showScenarioLoadError(outcome.message);
                }
                return;
            }
            setScenarioLoadingPhase("読み込み結果の受け取り",
                "画面への反映（エラーファイルの確認・スキン・各パネル）",
                "前回開いていたタブの復元");
            openScenarioImpl(std::move(outcome.summary), outcome.errorFiles,
                             std::move(pending_open_paths),
                             std::move(pending_select_file), outcome.issues);
            dismissPreviousScenarioLoading();
        });
    };
    const String requested_path = fname;
    setScenarioLoadingPhase("読み込みの準備",
        "シナリオファイルの読み込み（バックグラウンド処理）", "読み込み結果の受け取り");
    auto option = loadOption(nullptr);
    if (options) {
        option.expandXMLs = options->expandXMLs;
        option.doubleIO = options->doubleIO;
    }
    (void)loader::loadScenarioOutcomeFromFileAsync(
        *prop_, std::move(option), window_, sync_, std::move(status),
        std::move(fname), std::move(completed), services_, control, std::move(reload), false);
    if (scenario_load_control_ == control && services_.dispatcher) {
        const std::weak_ptr<std::atomic_bool> timeout_alive = lifecycle_alive_;
        const auto fail_unresponsive = [timeout_alive, this, control, generation](String message) {
            const auto gate = timeout_alive.lock();
            if (!gate || !gate->load(std::memory_order_acquire) ||
                scenario_load_control_ != control || generation != load_generation_) return;
            scenario_load_timeout_timer_ = {};
            ++load_generation_;
            control->cancel();
            scenario_load_control_.reset();
            dismissPreviousScenarioLoading();
            setStatusLine(message);
            showScenarioLoadError(message);
        };
        const auto task = control->taskSnapshot();
        if (task && task->finished() && !control->completed.load(std::memory_order_acquire)) {
            fail_unresponsive("シナリオの読み込み処理を開始できませんでした: " + requested_path);
            return;
        }
        scenario_load_timeout_timer_ = services_.dispatcher->post_delayed(
            std::chrono::seconds(60), [fail_unresponsive, requested_path] {
                fail_unresponsive("シナリオの読み込みが60秒以内に完了しませんでした。"
                    "読み込みを中止しました: " + requested_path);
            });
    }
}

bool MainWindow::hasBackgroundLoads() const {
    // ワーカーが完了しても、UIへの結果通知が待機中の場合がある。 pollBackgroundLoads()
    // がその通知を反映・無効化するまで、読み込みを有効なままにする。
    return !background_loads_.empty() || !pending_load_deliveries_.empty();
}

bool MainWindow::stopBackgroundLoads() {
    cancelScenarioLoadTimeout();
    dismissPreviousScenarioLoading();
    ++load_generation_;
    for (const auto& load : background_loads_) load->cancel();
    scenario_load_control_.reset();
    pending_load_deliveries_.clear();
    return !hasBackgroundLoads();
}

void MainWindow::deliverLoadResult(lib::Action delivery) {
    // 保存時のモーダルな入出力待ちは、UIの通知処理を進める。 結果の所有権は保持す
    // るが、保存中に現在のモデルを置き換えたり取り込みダイアログを開いたりしない。
    if (in_saving_) pending_load_deliveries_.push_back(std::move(delivery));
    else if (delivery) delivery();
}

void MainWindow::pollBackgroundLoads() {
    // D版と同様、保存・別名保存中はディレクトリ追跡を停止し、通知は消
    // 費しない。 保存の制限が解除されたら、保留した通知を処理する。
    const std::weak_ptr<std::atomic_bool> life = lifecycle_alive_;
    if (!in_saving_) {
        if (const auto view = directory_window_) view->pollTraceRefresh();
        const auto alive = life.lock();
        if (!alive || !alive->load(std::memory_order_acquire)) return;
    }
    if (in_saving_) return;
    auto deliveries = std::exchange(pending_load_deliveries_, {});
    for (std::size_t index = 0; index < deliveries.size(); ++index) {
        if (in_saving_) {
            // 先のコールバックが保存時の入れ子の通知ループを開いた場合、
            // 新しく追加された処理より前に、元の通知順序を維持する。
            auto newly_queued = std::exchange(pending_load_deliveries_, {});
            for (; index < deliveries.size(); ++index)
                pending_load_deliveries_.push_back(std::move(deliveries[index]));
            for (auto& delivery : newly_queued)
                pending_load_deliveries_.push_back(std::move(delivery));
            return;
        }
        if (deliveries[index]) deliveries[index]();
        const auto alive = life.lock();
        if (!alive || !alive->load(std::memory_order_acquire)) return;
    }
    // 結果通知が同期的に保存処理へ入る場合がある。 保存の制限が解除
    // されるまで、完了済みの制御オブジェクトと進捗記録を保持する。
    if (in_saving_) return;

    // 先にタスクハンドルを無効化する。進捗ダイアログの終了は通知処理
    // へ再入する場合があるため、 削除条件の判定関数から呼ばない。
    std::vector<std::shared_ptr<loader::LoadControl>> finished_loads;
    finished_loads.reserve(background_loads_.size());
    for (const auto& load : background_loads_) {
        if (!load->submission_finished.load(std::memory_order_acquire)) continue;
        const auto task = load->taskSnapshot();
        if (task && !task->finished()) continue;
        load->releaseTask();
        finished_loads.push_back(load);
    }
    if (!finished_loads.empty()) {
        std::erase_if(background_loads_, [&finished_loads](const auto& load) {
            return std::find(finished_loads.begin(), finished_loads.end(), load) !=
                   finished_loads.end();
        });
    }

    // 空の配列取り込みは、D版と同様に成功・失敗のコールバックを意図的に
    // 呼ばない。 その仕様は維持し、残ってしまうC++版の進捗通知のうち
    // 、該当する所有者のものだけを終了する。 終了処理がタスクハンドルの
    // 削除後に保存へ入る場合があるため、 background_loads_
    // とは独立して記録を走査し、残ったものは後の確認で処理する。
    while (!standalone_array_import_progress_.empty()) {
        if (in_saving_) return;
        const auto progress = std::find_if(standalone_array_import_progress_.begin(),
            standalone_array_import_progress_.end(), [](const auto& entry) {
                if (!entry.control->submission_finished.load(std::memory_order_acquire))
                    return false;
                const auto task = entry.control->taskSnapshot();
                return !task || task->finished();
            });
        if (progress == standalone_array_import_progress_.end()) break;
        auto record = std::move(*progress);
        standalone_array_import_progress_.erase(progress);
        const auto load = record.control;
        const auto alive = life.lock();
        if (!alive || !alive->load(std::memory_order_acquire)) return;
        const auto dialog = record.dialog.lock();
        const auto expected_summary = record.expected_summary.lock();
        if (!dialog || !expected_summary || load->isCancelled() || close_pending_ ||
            load_generation_ != record.scenario_generation || scenario_load_control_ ||
            summary_ != expected_summary || previous_scenario_loading_ != dialog)
            continue;

        const auto owner = window_;
        const auto gui = services_.gui;
        dismissPreviousScenarioLoading();
        const auto after_dismiss = life.lock();
        if (!after_dismiss || !after_dismiss->load(std::memory_order_acquire) ||
            window_ != owner || services_.gui != gui || in_saving_) return;
    }
    if (close_pending_ && background_loads_.empty()) {
        finishCloseAfterLoads();
    }
}

void MainWindow::pollStoragePersistence() {
    if (pending_save_as_) {
        auto* runtime = context_.runtimeContext;
        if (!runtime) return;
        if (runtime->state() == lib::RuntimeState::StorageError) {
            auto pending = std::move(pending_save_as_);
            pending->destination->abort();
            if (!pending->exportScenario && pending->summary)
                pending->summary->rollbackSaveAs(pending->checkpoint);
            if (!pending->exportScenario && pending->summary == summary_ &&
                pending->oldSkin && pending->saveSkin && skin_ != pending->oldSkin) {
                skin_ = pending->oldSkin;
                comm_->skin(skin_);
                comm_->restoreSkinMaterials(pending->skinJournal);
                area_table_context_.skin = skin_.get();
                if (area_table_) area_table_->skin(skin_.get());
            }
            setStatusLine(runtime->last_error().empty()
                ? "保存データを永続化できませんでした。"
                : runtime->last_error());
            in_saving_ = false;
            return;
        }
        if (runtime->state() != lib::RuntimeState::Ready) return;

        auto pending = std::move(pending_save_as_);
        std::string publish_error;
        bool published{};
        try {
            published = pending->destination->commit(publish_error);
        } catch (const std::exception& error) {
            publish_error = error.what();
        } catch (...) {
            publish_error = "保存先の確定中に不明なエラーが発生しました。";
        }
        if (!published) {
            pending->destination->abort();
            if (!pending->exportScenario && pending->summary)
                pending->summary->rollbackSaveAs(pending->checkpoint);
            if (!pending->exportScenario && pending->summary == summary_ &&
                pending->oldSkin && pending->saveSkin && skin_ != pending->oldSkin) {
                skin_ = pending->oldSkin;
                comm_->skin(skin_);
                comm_->restoreSkinMaterials(pending->skinJournal);
                area_table_context_.skin = skin_.get();
                if (area_table_) area_table_->skin(skin_.get());
            }
            setStatusLine(publish_error.empty()
                ? "選択した保存先を確定できませんでした。" : publish_error);
            in_saving_ = false;
            return;
        }
        if (!pending->exportScenario && pending->summary) {
            pending->summary->commitSaveAs(pending->checkpoint);
            if (pending->summary == summary_ &&
                pending->generation == edit_generation_)
                pending->summary->resetChanged();
        }
        if (pending->summary == summary_) {
            refreshTitle();
            if (comm_) {
                comm_->saved.call();
                if (!pending->exportScenario) {
                    comm_->refScenarioPath.call();
                    comm_->refSkin.call();
                    if (pending->oldClassic != summary_->legacy())
                        comm_->refDataVersion.call();
                    comm_->refPaths.call({});
                    addHistory();
                }
            }
        }
        // 保存音は永続化の完了後に鳴らし、書き込み完了待ちの間は鳴らさない。
        playSavedSound();
        setStatusLine("保存データの永続化が完了しました。");
        in_saving_ = false;
    }

    auto pending = pending_persist_summary_.lock();
    if (!pending) {
        pending_persist_generation_ = 0;
        if (!pending_save_as_) in_saving_ = false;
        notifyPendingPersistWaiters(false);
        return;
    }
    const auto* runtime = context_.runtimeContext;
    if (!runtime) return;
    if (runtime->state() == lib::RuntimeState::StorageError) {
        pending_persist_summary_.reset();
        pending_persist_generation_ = 0;
        in_saving_ = false;
        const auto message = runtime->last_error().empty()
            ? String{"保存データを永続化できませんでした。"} : runtime->last_error();
        setStatusLine(message);
        if (std::exchange(pending_overwrite_notification_, false))
            finishOverwriteSaveProgress(false, message);
        lib::editor_action_log("Save end result=persist-error message=\"" + message + "\"");
        pending_persist_backup_ = false;
        notifyPendingPersistWaiters(false);
        return;
    }
    if (runtime->state() != lib::RuntimeState::Ready) return;

    const bool unchanged = pending == summary_ &&
        pending_persist_generation_ == edit_generation_;
    pending_persist_summary_.reset();
    pending_persist_generation_ = 0;
    in_saving_ = false;
    pending_persist_backup_ = false;
    if (std::exchange(pending_overwrite_notification_, false))
        finishOverwriteSaveProgress(true, unchanged ? String{} :
            String{"保存開始後に加えた変更は、まだ保存されていません。"});
    lib::editor_action_log("Save end result=persisted unchanged=" + String(unchanged ? "1" : "0"));
    playSavedSound();
    if (!unchanged) {
        notifyPendingPersistWaiters(false);
        return;
    }

    pending->resetChanged();
    refreshTitle();
    if (comm_) {
        comm_->saved.call();
        addHistory();
    }
    setStatusLine("保存データの永続化が完了しました。");
    notifyPendingPersistWaiters(true);
}

void MainWindow::notifyPendingPersistWaiters(const bool saved) {
    auto waiters = std::exchange(pending_persist_waiters_, {});
    for (auto& waiter : waiters)
        if (waiter) waiter(saved);
}

void MainWindow::playSavedSound() {
    if (!prop_ || !services_.audio) return;
    const auto file = valueOf(prop_->var().savedSound);
    if (file.empty() || !std::filesystem::exists(pathFromUtf8(file))) return;
    std::string error;
    if (!services_.audio->initialize(error)) {
        if (!error.empty()) setStatusLine(error);
        return;
    }
    lib::AudioPlayOptions options;
    options.bus = lib::AudioBus::SoundEffect;
    options.volume = 1.0F;
    if (services_.audio->play({pathFromUtf8(file)}, options) == 0U && !error.empty())
        setStatusLine(error);
}

void MainWindow::saveScenario() { save(window_); }
void MainWindow::savec(WindowPtr shell) { save(std::move(shell)); }

void MainWindow::showOverwriteSaveProgress() {
    if (overwrite_save_dialog_ && services_.gui) services_.gui->close(overwrite_save_dialog_);
    auto dialog = std::make_shared<lib::Dialog>();
    dialog->title = "上書き保存 — 処理中";
    dialog->owner = window_;
    dialog->center_on_owner = true;
    // 保持したダイアログで非同期操作の状態を伝え、 保存・
    // 永続化完了処理の中で、別のモーダル待ちを始めない。
    dialog->modal = false;
    dialog->minimizable = false;
    dialog->maximizable = false;
    // メッセージ二行とOKボタンの行でコンパクトに構
    // 成する。 初期配置時は、失敗の詳細で行が増えた
    // 場合も、実測した内容が収まる大きさに広げる。
    dialog->set_bounds({0, 0, 560, 120});
    auto layout = std::make_shared<lib::LinearLayout>(lib::Orientation::Vertical);
    layout->padding = {16, 12, 16, 12};
    dialog->set_layout(layout);
    overwrite_save_message_ = std::make_shared<lib::Label>(
        "上書き保存を処理中です。\n編集内容をファイルに書き込んでいます。");
    overwrite_save_message_->wrap = true;
    overwrite_save_message_->set_layout_item({.horizontal = lib::Align::Stretch, .grow = 1.0F});
    dialog->add(overwrite_save_message_);
    overwrite_save_close_ = std::make_shared<lib::Button>("OK");
    overwrite_save_close_->default_button = true;
    overwrite_save_close_->set_enabled(false);
    const std::weak_ptr<lib::Dialog> weak = dialog;
    const std::weak_ptr<std::atomic_bool> lifetime = lifecycle_alive_;
    const auto gui = services_.gui;
    const auto close = [this, weak, lifetime, gui] {
        if (const auto current = weak.lock()) {
            if (gui) gui->close(current); else current->set_visible(false);
        }
        const auto alive = lifetime.lock();
        if (!alive || !alive->load(std::memory_order_acquire)) return;
        auto waiters = std::exchange(overwrite_save_ack_waiters_, {});
        for (auto& waiter : waiters)
            if (waiter) waiter();
    };
    overwrite_save_close_->on_click = close;
    // 中央に配置し、幅は従来の96pxの二倍にする。
    overwrite_save_close_->set_layout_item({.horizontal = lib::Align::Center, .preferred = {192, 28}});
    dialog->add(overwrite_save_close_);
    const std::weak_ptr<lib::Button> button = overwrite_save_close_;
    dialog->on_close_requested = [button, close] {
        if (const auto control = button.lock(); control && control->enabled()) close();
    };
    overwrite_save_dialog_ = dialog;
    if (gui) gui->show(dialog);
}

void MainWindow::finishOverwriteSaveProgress(bool success, const String& detail) {
    if (!overwrite_save_dialog_ || !overwrite_save_message_ || !overwrite_save_close_) return;
    overwrite_save_dialog_->title = success ? "上書き保存 — 終了" : "上書き保存 — 失敗";
    overwrite_save_message_->text = success ? "上書き保存が完了しました。" : "上書き保存に失敗しました。";
    if (!detail.empty()) overwrite_save_message_->text += "\n" + detail;
    overwrite_save_close_->set_enabled(true);
    if (services_.gui) services_.gui->show(overwrite_save_dialog_);
}

void MainWindow::afterOverwriteSaveAcknowledged(std::function<void()> completed) {
    if (!completed) return;
    // ALLではOKが押されるまで保存結果を表示する。 保存
    // 確認から要求された画面遷移は、その確認後に再開する。
    if (isAllMode() && overwrite_save_dialog_ && overwrite_save_close_ &&
        overwrite_save_dialog_->visible()) {
        overwrite_save_ack_waiters_.push_back(std::move(completed));
        return;
    }
    completed();
}

SaveOption MainWindow::createSaveOpt(bool isClassic, bool initial) const {
    SaveOption option;
    option.doubleIO = valueOf(prop_->var().doubleIO);
    option.saveInnerImagePath = valueOf(prop_->var().saveInnerImagePath);
    option.logicalSort = valueOf(prop_->var().logicalSort);
    // D版と同様、Classic と WSN の上書きには別々の設定を
    // 使う。 シナリオの初回保存を、変更箇所だけの書き込みにはしない。
    option.saveChangedOnly = !initial && (isClassic
        ? valueOf(prop_->var().saveChangedOnly)
        : valueOf(prop_->var().saveChangedOnlyWsn));
    option.backup = !initial && valueOf(prop_->var().backupBeforeSaveEnabled);
    const auto backup_root = prop_->backupBeforeSavePath();
    option.backupDir = pathToUtf8(
        pathFromUtf8(backup_root) / valueOf(prop_->var().backupBeforeSaveDir));
    option.archiveInNewThread = !initial && summary_ && summary_->useTemp() &&
        valueOf(prop_->var().archiveInNewThread);
    option.xmlFileNameIsIDOnly = valueOf(prop_->var().xmlFileNameIsIDOnly);
    option.autoUpdateJpy1File = valueOf(prop_->var().autoUpdateJpy1File);
    option.saveSkinName = valueOf(prop_->var().saveSkinName);
    option.replaceClassicResourceExtension =
        valueOf(prop_->var().replaceClassicResourceExtension);
    option.tableExtension = valueOf(prop_->var().tableExtension);
    option.midiExtension = valueOf(prop_->var().midiExtension);
    option.waveExtension = valueOf(prop_->var().waveExtension);
    if (in_saving_) {
        option.ioExecutor = [gui = services_.gui, owner = window_](lib::Action work) {
            if (gui) gui->run_background_io(owner, std::move(work));
            else if (work) work();
        };
    }
    return option;
}

bool MainWindow::beforeSave() {
    const auto option = createSaveOpt(summary_ && summary_->legacy(), false);
    option.runIo([sync = &sync_] { sync->sync(); });
    // D版と同様、対応する設定が有効なら、直列化前に開いているシナリオ依存のダ
    // イアログをすべて適用する。 非表示で保持されているC++版のモデルはSWT
    // ウィンドウとは異なるため、 実際に有効な画面だけを意図的に対象とする。
    if (prop_ && valueOf(prop_->var().applyDialogsBeforeSave)) {
        for (auto* dialog : editdialogsession::EditDialogSession::liveSessions()) {
            const auto window = dialog ? dialog->editDialogWindow() : nullptr;
            if (!dialog || dialog->noScenario() || !dialog->editDialogPending() ||
                !window || !window->visible()) continue;
            // C++版では、編集内容の適用が拒否されたら意図的に保存を中止
            // する。 D版はこの場合も、確定済みのモデルの保存を続ける。
            if (!dialog->applyEditDialog()) return false;
        }
    }
    updateChangeCount();
    if (!summary_ || !prop_ || !summary_->legacy() ||
        !valueOf(prop_->var().backupBeforeSaveEnabled)) return true;
    const auto source = pathFromUtf8(summary_->scenarioPath());
    if (source.empty() || !std::filesystem::exists(source)) return true;
    const auto parent = pathFromUtf8(prop_->backupBeforeSavePath());
    const auto destination = parent /
        (String("cwxeditor_presave_") + timeName(services_) + "[" +
         basename(pathToUtf8(source)) + "]");
    option.runIo([source, destination] {
        if (!copyScenario(source, destination))
            throw std::runtime_error("Unable to create the pre-save backup");
    });
    return true;
}

bool MainWindow::save(WindowPtr shell, bool backupSave) {
    static_cast<void>(shell);
    if (!summary_ || !prop_ || in_saving_) return false;
    const auto saveStarted = std::chrono::steady_clock::now();
    lib::editor_action_log(
        "Save begin scenario=\"" + summary_->scenarioName() + "\" path=\"" +
        summary_->scenarioPath() + "\" backup=" + (backupSave ? "1" : "0"));
    const auto saveElapsed = [&saveStarted] {
        return std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - saveStarted).count();
    };
    if (!summary_->isSaved()) return backupSave ? false : saveScenarioAImpl(window_);
    if (!skin_) skin_ = dskin::findSkin2(*prop_, valueOf(prop_->var().defaultSkin),
                                          valueOf(prop_->var().defaultSkinName));
    if (!skin_) return false;
    SaveActivity saving(in_saving_);
    // D版の MainWindow.save と同様、上書き保存の間は setWaitCursors(shell) を適用する。
    dutils::WaitCursorScope wait_cursors(shell ? shell : window_);
    sync_.clearError();
    const auto release = [&saving] { saving.release(); };
    auto* runtime = context_.runtimeContext;
    bool runtimeSave = false;
    try {
        if (!backupSave) showOverwriteSaveProgress();
        if (runtime && runtime->state() == lib::RuntimeState::StorageError &&
            !runtime->retry_storage())
            throw std::runtime_error(runtime->last_error().empty()
                ? "保存領域を再開できませんでした。" : runtime->last_error());
        runtimeSave = runtime && runtime->begin_storage_save();
        if (runtime && !runtimeSave) {
            throw std::runtime_error(runtime->last_error().empty()
                ? "Editor storage is not ready" : runtime->last_error());
        }
        if (!beforeSave())
            throw std::runtime_error(
                "An open editor dialog could not apply its changes");
        lib::editor_action_log("Save phase=write begin elapsed_us=" +
                               std::to_string(saveElapsed()));
        const auto serialized_summary = summary_;
        const auto serialized_generation = edit_generation_;
        auto save_option = createSaveOpt(summary_->legacy(), false);
        save_option.deferChangedReset = true;
        serialized_summary->saveOverwrite(prop_->parent(), *skin_, save_option, sync_);
        lib::editor_action_log("Save phase=write end elapsed_us=" +
                               std::to_string(saveElapsed()));
        if (runtimeSave && !runtime->complete_storage_save(true)) {
            throw std::runtime_error("Unable to complete the storage save transaction");
        }
        const auto persistence = runtime
            ? runtime->persist_storage_result()
            : lib::StoragePersistResult::Completed;
        if (persistence == lib::StoragePersistResult::Failed) {
            throw std::runtime_error(runtime->last_error().empty()
                ? "Unable to persist editor storage" : runtime->last_error());
        }
        if (persistence == lib::StoragePersistResult::Pending) {
            // ブラウザーの保存領域は書き込み要求を受け付けたが、まだ永続化
            // は完了していない。 イベントループがこの操作の結果を確認する
            // まで、文書を変更済みのままにし、共通の保存制限も維持する。
            pending_persist_summary_ = serialized_summary;
            pending_persist_generation_ = serialized_generation;
            pending_persist_backup_ = backupSave;
            pending_overwrite_notification_ = !backupSave;
            if (!backupSave && overwrite_save_message_)
                overwrite_save_message_->text =
                    "上書き保存を処理中です。\n保存領域への反映を待っています。";
            setStatusLine("保存データの永続化を待っています。");
            saving.keepActive();
            return false;
        }
        // 対象となるバックグラウンド保存でも、前面の上書き保存と同
        // じく、 永続化成功後に世代を確認して保存点を設定する。
        const bool unchanged = summary_ == serialized_summary &&
            edit_generation_ == serialized_generation;
        if (unchanged) serialized_summary->resetChanged();
        release();
        refreshTitle();
        comm_->saved.call();
        addHistory();
        playSavedSound();
        lib::editor_action_log("Save end result=ok elapsed_us=" +
                               std::to_string(saveElapsed()));
        if (!backupSave) finishOverwriteSaveProgress(true, unchanged ? String{} :
            String{"保存開始後に加えた変更は、まだ保存されていません。"});
        return unchanged;
    } catch (const std::exception& error) {
        if (runtimeSave && runtime && runtime->state() == lib::RuntimeState::SavingStorage)
            (void)runtime->complete_storage_save(false, error.what());
        release();
        lib::editor_action_log("Save end result=error elapsed_us=" +
                               std::to_string(saveElapsed()) + " message=\"" + error.what() + "\"");
        setStatusLine(::cwxeditor::cwx::utils::tryFormat(prop_->requiredMsgs().saveError.value,
            summary_->scenarioPath()) + "\n" + error.what());
        if (!backupSave) finishOverwriteSaveProgress(false, error.what());
        return false;
    } catch (...) {
        if (runtimeSave && runtime && runtime->state() == lib::RuntimeState::SavingStorage)
            (void)runtime->complete_storage_save(false, "Unknown save error");
        release();
        lib::editor_action_log("Save end result=error elapsed_us=" +
                               std::to_string(saveElapsed()) + " message=unknown");
        setStatusLine(::cwxeditor::cwx::utils::tryFormat(
            prop_->requiredMsgs().saveError.value, summary_->scenarioPath()));
        if (!backupSave) finishOverwriteSaveProgress(false, "不明な保存エラーです。");
        return false;
    }
}

void MainWindow::saveScenarioA() { saveScenarioAImpl(window_); }

bool MainWindow::saveScenarioAImpl(WindowPtr shell) {
    (void)shell;
    if (!summary_ || !skin_ || !services_.dialogs || in_saving_) return false;
    static constexpr std::size_t FilterXmlNewDir = 0U;
    static constexpr std::size_t FilterWsn = 1U;
    static constexpr std::size_t FilterXml = 2U;
    static constexpr std::size_t FilterWsm = 3U;
    static constexpr std::size_t FilterZip = 4U;
    const bool old_classic = summary_->legacy();
    lib::FileDialogRequest request;
    // これは文書形式の選択画面であり、ファイルメニューの操作ラベルではない
    // 。 D版と同様、専用の翻訳可能な「シナリオの保存」のタイトルを使う。
    request.title = prop_->requiredMsgs().dlgTitSaveScenario.value;
    request.selected_filter = old_classic ? FilterWsm : FilterXmlNewDir;
    const auto& messages = prop_->requiredMsgs();
    request.filters = {
        {messages.filterScenarioSaveNewDir.value, {"*"}},
        {messages.filterScenarioSave.value, {"*.wsn"}},
        {messages.filterScenarioSaveDir.value, {"Summary.xml"}},
        {messages.filterScenarioSaveClassic.value, {"Summary.wsm"}},
        {messages.filterScenarioSaveZip.value, {"*.zip"}},
    };
    std::optional<std::size_t> filter_cab;
    if (cab::canUncab()) {
        filter_cab = request.filters.size();
        request.filters.push_back(
            {messages.filterScenarioSaveCab.value, {"*.cab"}});
    }
    const auto filter_classic7 = request.filters.size();
    request.filters.push_back(
        {messages.filterScenarioSaveClassic7.value, {"*"}});
    const auto filter_zip7 = request.filters.size();
    request.filters.push_back(
        {messages.filterScenarioSaveZip7.value, {"*.zip"}});
    std::optional<std::size_t> filter_cab7;
    if (cab::canUncab()) {
        filter_cab7 = request.filters.size();
        request.filters.push_back(
            {messages.filterScenarioSaveCab7.value, {"*.cab"}});
    }

    const auto suggested_source = !summary_->readOnlyPath().empty()
        ? pathFromUtf8(summary_->readOnlyPath())
        : (!summary_->origZipName().empty()
            ? pathFromUtf8(summary_->origZipName())
            : pathFromUtf8(summary_->scenarioName()));
    auto suggested = suggested_source.filename();
    if (*request.selected_filter == FilterWsm) {
        suggested = "Summary.wsm";
    } else if (*request.selected_filter == FilterXmlNewDir) {
        suggested = pathFromUtf8(summary_->scenarioName()).filename();
    } else {
        suggested.replace_extension(".wsn");
    }
    request.initial_name = pathToUtf8(suggested);
    // D版と同様、別名保存は常に scenarioFilterPath から
    // 始める。 未保存文書で、シナリオ名にディレクトリ部分がない場合も同様。
    const auto filter_path = loader::scenarioFilterPath(*prop_);
    request.initial_directory = filter_path.empty() ? std::filesystem::path{}
                                                    : pathFromUtf8(filter_path);

    const auto dialogs = services_.dialogs;
    struct SaveAsTarget {
        std::filesystem::path destination;
        std::filesystem::path directory;
        bool classic{};
        bool export_sc{};
        bool nonempty_directory{};
    };
    // 一回の選択結果を、D版の保存形式と保存先へ変換する。 デスクトッ
    // プの同期ループと、Web ALL の非同期の処理列で共有する。
    const auto resolve = [filter_classic7, filter_zip7, filter_cab, filter_cab7](
                             lib::FileDialogRequest& request,
                             const lib::FileDialogSaveResult& selection)
        -> std::optional<SaveAsTarget> {
        if (!selection.path || selection.path->empty() || !selection.selected_filter ||
            *selection.selected_filter >= request.filters.size()) return std::nullopt;
        bool export_sc{};
        bool classic{};
        std::filesystem::path destination;
        const auto selected_filter = *selection.selected_filter;
        request.selected_filter = selected_filter;
        request.initial_directory = selection.path->parent_path();
        request.initial_name = pathToUtf8(selection.path->filename());
        const auto equals_optional = [selected_filter](const auto& value) {
            return value && selected_filter == *value;
        };
        export_sc = selected_filter == filter_classic7 ||
            selected_filter == filter_zip7 || equals_optional(filter_cab7);
        classic = selected_filter == FilterWsm ||
            selected_filter == FilterZip || equals_optional(filter_cab) || export_sc;
        destination = *selection.path;
        std::filesystem::path directory = destination.parent_path();
        bool check_directory{};
        if (selected_filter == FilterXmlNewDir || selected_filter == filter_classic7) {
            // 展開形式では Summary のファイル名ではなく、新し
            // いシナリオディレクトリの名前を受け取る。 Next のバ
            // ージョン7の出力は、親ではなくそのディレクトリ内に書く。
            directory = destination;
            destination /= selected_filter == filter_classic7 ? "Summary.wsm" : "Summary.xml";
            check_directory = std::filesystem::exists(directory);
        } else if (selected_filter == FilterXml) {
            destination = directory / "Summary.xml";
            check_directory = true;
        } else if (selected_filter == FilterWsm) {
            destination = directory / "Summary.wsm";
            check_directory = true;
        } else if (selected_filter != FilterWsn && selected_filter != FilterZip &&
                   selected_filter != filter_zip7 && !equals_optional(filter_cab) &&
                   !equals_optional(filter_cab7)) {
            return std::nullopt;
        }
        // Windowsのネイティブ選択画面は、バックエンドが最終パス
        // を別に返す場合、 選択フィルターの拡張子を必ずしも追加しない
        // 。D版の FileDialog と同様の既定拡張子の動作を
        // 、 すべてのデスクトップバックエンドの共通境界で維持する。
        if (!destination.has_extension()) {
            if (selected_filter == FilterWsn)
                destination += ".wsn";
            else if (selected_filter == FilterZip || selected_filter == filter_zip7)
                destination += ".zip";
            else if (equals_optional(filter_cab) || equals_optional(filter_cab7))
                destination += ".cab";
            // 保存先を拒否した場合は、同じネイティブの選択画
            // 面を開き直す。 最終的な保存パスだけでなく入力
            // 名も選択したD版形式に合わせ、 最初の選択で
            // 付いた拡張子が再試行で失われないようにする。
            request.initial_name = pathToUtf8(destination.filename());
        }

        std::error_code directory_error;
        bool nonempty_directory = check_directory &&
            std::filesystem::is_directory(directory, directory_error) &&
            std::filesystem::directory_iterator(directory, directory_error) !=
                std::filesystem::directory_iterator{};
        if (directory_error) nonempty_directory = false;
        return SaveAsTarget{destination, directory, classic, export_sc,
                            nonempty_directory};
    };
    // 非同期の完了処理へ複製されるため、ローカル変数ではなく所有者を取得する。
    const auto question = [prop = prop_](String text) {
        lib::MessageDialogRequest warning;
        warning.title = prop->requiredMsgs().dlgTitQuestion.value;
        warning.message = std::move(text);
        warning.severity = lib::MessageSeverity::Question;
        warning.buttons = lib::MessageButtons::YesNo;
        return warning;
    };
    const auto nonempty_text = [prop = prop_](const std::filesystem::path& directory) {
        return String(::cwxeditor::cwx::utils::tryFormat(
            prop->requiredMsgs().saveToNotEmptyDir.value, pathToUtf8(directory)));
    };

    if (isAllMode()) {
        // Web ALL では、同期のファイル選択・確認がブラウザーのフレーム
        // 内で入れ子のイベントループに入り、停止の原因になる。 同じ手順を完了
        // コールバックの連鎖として実行し、確認を拒否したら選択画面を開き直す。
        const std::weak_ptr<std::atomic_bool> lifetime = lifecycle_alive_;
        const auto alive = [lifetime] {
            const auto value = lifetime.lock();
            return value && value->load(std::memory_order_acquire);
        };
        auto shared_request = std::make_shared<lib::FileDialogRequest>(request);
        auto step = std::make_shared<std::function<void()>>();
        const auto ask = [dialogs](lib::MessageDialogRequest warning,
                                   std::function<void(bool)> answered) {
            if (dialogs->supports_message_async() &&
                dialogs->message_async(warning, [answered](lib::MessageResult result) {
                    answered(result == lib::MessageResult::Yes);
                })) return;
            answered(dialogs->message(warning) == lib::MessageResult::Yes);
        };
        const std::weak_ptr<std::function<void()>> weak_step = step;
        *step = [this, dialogs, shared_request, weak_step, resolve, alive, ask,
                 question, nonempty_text, old_classic] {
            const auto strong = weak_step.lock();
            if (!strong || !alive()) return;
            const bool started = dialogs->save_file_result_async(*shared_request,
                [this, strong, shared_request, resolve, alive, ask, question,
                 nonempty_text, old_classic](lib::FileDialogSaveResult selection) {
                    if (!alive() || in_saving_) return;
                    const auto target = resolve(*shared_request, selection);
                    if (!target) return;
                    const auto proceed = [this, target, old_classic, alive] {
                        if (!alive() || in_saving_) return;
                        (void)performSaveAs(target->destination, target->classic,
                                            target->export_sc, old_classic);
                    };
                    const auto after_directory =
                        [this, strong, target, proceed, ask, question, old_classic] {
                            if (!old_classic && target->classic) {
                                ask(question(prop_->requiredMsgs().warningXToClassic.value),
                                    [strong, proceed](const bool yes) {
                                        if (yes) proceed(); else (*strong)();
                                    });
                                return;
                            }
                            proceed();
                        };
                    if (target->nonempty_directory) {
                        ask(question(nonempty_text(target->directory)),
                            [strong, after_directory](const bool yes) {
                                if (yes) after_directory(); else (*strong)();
                            });
                        return;
                    }
                    after_directory();
                });
            if (!started) setStatusLine("保存先の選択ダイアログを開けませんでした。");
        };
        (*step)();
        return false;
    }

    for (;;) {
        lib::FileDialogSaveResult selection;
        try {
            selection = dialogs->save_file_result(request);
        } catch (const std::exception& error) {
            setStatusLine(::cwxeditor::cwx::utils::tryFormat(prop_->requiredMsgs().saveError.value,
                summary_->scenarioPath()) + "\n" + error.what());
            return false;
        } catch (...) {
            setStatusLine(::cwxeditor::cwx::utils::tryFormat(
                prop_->requiredMsgs().saveError.value, summary_->scenarioPath()));
            return false;
        }
        const auto target = resolve(request, selection);
        if (!target) return false;
        if (target->nonempty_directory &&
            dialogs->message(question(nonempty_text(target->directory))) != lib::MessageResult::Yes)
            continue;
        if (!old_classic && target->classic &&
            dialogs->message(question(messages.warningXToClassic.value)) != lib::MessageResult::Yes)
            continue;
        return performSaveAs(target->destination, target->classic, target->export_sc,
                             old_classic);
    }
}

bool MainWindow::performSaveAs(const std::filesystem::path destination,
                               const bool classic, const bool export_sc,
                               const bool old_classic) {
    const auto dialogs = services_.dialogs;
    if (!dialogs || !summary_ || !prop_) return false;
    auto save_path = std::make_unique<lib::FileSaveTransaction>(*dialogs, destination);
    std::optional<summary_ns::Summary::SaveAsCheckpoint> save_checkpoint;
    SaveActivity saving(in_saving_);
    // D版の saveScenarioAImpl と同様、保存先が決まったら setWaitCursors(shell) を設定する。
    dutils::WaitCursorScope wait_cursors(window_);
    sync_.clearError();
    const auto release = [&saving] { saving.release(); };
    auto* runtime = context_.runtimeContext;
    bool runtimeSave{};
    auto old_skin = skin_;
    SkinPtr save_skin;
    commons::Commons::SkinMaterialJournal skin_journal;
    const auto default_skin = dskin::findSkin2(
        *prop_, valueOf(prop_->var().defaultSkin), valueOf(prop_->var().defaultSkinName));
    const auto restore_skin = [this, &old_skin, &save_skin, &skin_journal, export_sc] {
        if (export_sc || !old_skin || !save_skin || skin_ == old_skin) return;
        skin_ = old_skin;
        comm_->skin(skin_);
        comm_->restoreSkinMaterials(skin_journal);
        area_table_context_.skin = skin_.get();
        if (area_table_) area_table_->skin(skin_.get());
    };
    try {
        if (runtime && runtime->state() == lib::RuntimeState::StorageError &&
            !runtime->retry_storage()) {
            throw std::runtime_error(runtime->last_error().empty()
                ? "Unable to restart editor storage" : runtime->last_error());
        }
        runtimeSave = runtime && runtime->begin_storage_save();
        if (runtime && !runtimeSave) {
            throw std::runtime_error(runtime->last_error().empty()
                ? "Editor storage is not ready" : runtime->last_error());
        }
        if (!beforeSave())
            throw std::runtime_error(
                "An open editor dialog could not apply its changes");
        // 確定したダイアログの編集は、作成した内容として扱い、別名保存の取り消し対象にはしない。
        old_skin = skin_;
        save_checkpoint = summary_->checkpointSaveAs();
        save_skin = commons::findSkin(
            *comm_, *prop_, summary_.get(), classic, pathToUtf8(destination),
            old_skin ? old_skin->type() : String{});
        if (!save_skin) save_skin = default_skin;
        if (!save_skin) throw std::runtime_error("Unable to resolve a skin for the selected save destination");
        if (!export_sc && save_skin != old_skin) {
            skin_ = save_skin;
            comm_->skin(skin_);
            comm_->updateSkinMaterialsExtension(summary_->useCounter(), old_skin, save_skin, &skin_journal);
            area_table_context_.skin = skin_.get();
            if (area_table_) area_table_->skin(skin_.get());
        }
        const auto serialized_summary = summary_;
        auto serialized_generation = edit_generation_;
        auto save_option = createSaveOpt(summary_->legacy(), false);
        save_option.deferChangedReset = true;
        save_option.beforeSerialization = [this, &serialized_generation] {
            serialized_generation = edit_generation_;
        };
        if (export_sc) {
            auto option = createSaveOpt(summary_->legacy(), false);
            option.dataVersion = 7U;
            summary_->exportClassicScenario(
                prop_->parent(), *save_skin, option, pathToUtf8(destination),
                prop_->tempPath(),
                [this](String warning) { setStatusLine(std::move(warning)); }, sync_);
        } else {
            summary_->saveWithName(
                prop_->parent(), *save_skin, save_option,
                pathToUtf8(destination), prop_->tempPath(),
                valueOf(prop_->var().expandXMLs), default_skin.get(),
                [this](String warning) { setStatusLine(std::move(warning)); },
                classic, sync_);
        }
        if (runtimeSave && !runtime->complete_storage_save(true)) {
            throw std::runtime_error("Unable to complete the storage save transaction");
        }
        const auto persistence = runtime
            ? runtime->persist_storage_result()
            : lib::StoragePersistResult::Completed;
        if (persistence == lib::StoragePersistResult::Failed) {
            throw std::runtime_error(runtime->last_error().empty()
                ? "Unable to persist editor storage" : runtime->last_error());
        }
        if (persistence == lib::StoragePersistResult::Pending) {
            // ブラウザーの永続化処理は書き込みを受け付けたが、まだ永続化は完
            // 了していない。 pollStoragePersistence
            // が完了を確認するまで、提供側の保存先の所有権と別名保存の復元
            // 点を保持する。 ここで中止すると、選択した保存先が失われる。
            pending_save_as_ = std::make_unique<PendingSaveAs>(PendingSaveAs{
                serialized_summary, *save_checkpoint, std::move(save_path), old_skin,
                save_skin, serialized_generation, export_sc, old_classic, std::move(skin_journal)});
            setStatusLine("保存データの永続化を待っています。");
            saving.keepActive();
            return false;
        }
        std::string publish_error;
        if (!save_path->commit(publish_error)) {
            throw std::runtime_error(publish_error.empty()
                ? "Unable to publish the selected save destination"
                : publish_error);
        }
        if (!export_sc) {
            summary_->commitSaveAs(*save_checkpoint);
            if (summary_ == serialized_summary && edit_generation_ == serialized_generation)
                summary_->resetChanged();
        }
        release();
        refreshTitle();
        comm_->saved.call();
        // saveWithName はシナリオのパスと表現形式を置き換える場
        // 合がある。 D版と同様、別名保存の成功後は依存する全ペインへ通知
        // し、 素材・ファイルのペインに以前の保存先が残らないようにする。
        if (!export_sc) {
            comm_->refScenarioPath.call();
            comm_->refSkin.call();
            if (old_classic != summary_->legacy()) comm_->refDataVersion.call();
            comm_->refPaths.call({});
            addHistory();
        }
        playSavedSound();
        return true;
    } catch (const std::exception& error) {
        save_path->abort();
        if (!export_sc && save_checkpoint) summary_->rollbackSaveAs(*save_checkpoint);
        restore_skin();
        if (runtimeSave && runtime && runtime->state() == lib::RuntimeState::SavingStorage)
            (void)runtime->complete_storage_save(false, error.what());
        const auto dialogs = services_.dialogs;
        const auto warning_title = prop_->requiredMsgs().dlgTitWarning.value;
        // D版の saveScenarioAImpl と同様、SummaryException.msg
        // はその内容をそのまま表示する。 すでにユーザー向けの保存失敗を含むメッセージは
        // 維持し、 それを持たない低水準の例外だけに、一般的な保存状況の説明を追加する。
        const auto warning = dynamic_cast<const ::cwxeditor::cwx::summary::SummaryException*>(
                                 &error)
            ? String(error.what())
            : ::cwxeditor::cwx::utils::tryFormat(
                  prop_->requiredMsgs().saveError.value, pathToUtf8(destination)) +
                  "\n" + error.what();
        const auto lifetime = lifecycle_alive_;
        release();
        if (lifetime && lifetime->load(std::memory_order_acquire)) {
            setStatusLine(warning);
            if (lifetime->load(std::memory_order_acquire))
                showWarningDialog(dialogs, warning_title, warning);
        }
        return false;
    } catch (...) {
        save_path->abort();
        if (!export_sc && save_checkpoint) summary_->rollbackSaveAs(*save_checkpoint);
        restore_skin();
        if (runtimeSave && runtime && runtime->state() == lib::RuntimeState::SavingStorage)
            (void)runtime->complete_storage_save(false, "Unknown save error");
        const auto dialogs = services_.dialogs;
        const auto warning_title = prop_->requiredMsgs().dlgTitWarning.value;
        const auto warning = ::cwxeditor::cwx::utils::tryFormat(
            prop_->requiredMsgs().saveError.value, pathToUtf8(destination));
        const auto lifetime = lifecycle_alive_;
        release();
        if (lifetime && lifetime->load(std::memory_order_acquire)) {
            setStatusLine(warning);
            if (lifetime->load(std::memory_order_acquire))
                showWarningDialog(dialogs, warning_title, warning);
        }
        return false;
    }
}

void MainWindow::updateExecEngineWithPartyName() {
    updateExecEngineWithPartyNameMI();
    updateExecEngineWithPartyNameTI();
}
void MainWindow::updateExecEngineWithPartyNameMI() {
    if (!menu_exec_party_ || !prop_) return;
    for (std::size_t index = 0; index < menu_exec_party_->size(); ++index) {
        const auto* entry = menu_exec_party_->entry(index);
        if (entry && entry->data.id == MenuID::ExecEngineWithLastParty) {
            const auto id = MenuID::ExecEngineWithLastParty;
            const auto& party = valueOf(prop_->var().lastExecutedParty);
            const auto text = party.enginePath.empty()
                ? prop_->buildMenu(id)
                : ::cwxeditor::cwx::menu::MenuProps::buildMenu(
                    execEngineWithLastPartyText(),
                    prop_->flex().menu.mnemonic(id),
                    prop_->flex().menu.hotkey(id), false);
            dmenu::MenuItemRef(menu_exec_party_.get(), index)
                .setText(text);
            break;
        }
    }
}
void MainWindow::updateExecEngineWithPartyNameTI() {
    if (!prop_) return;
    const auto item = tool_items_.find(MenuID::ExecEngineWithParty);
    if (item == tool_items_.end()) return;
    if (canExecEngineWithLastParty2()) {
        const auto& menu = prop_->flex().menu;
        item->second.setTooltip(::cwxeditor::cwx::menu::MenuProps::buildTool(
            execEngineWithLastPartyText(),
            menu.mnemonic(MenuID::ExecEngineWithLastParty),
            menu.hotkey(MenuID::ExecEngineWithLastParty), false));
    } else {
        item->second.setTooltip(prop_->buildTool(MenuID::ExecEngineWithParty));
    }
}

String MainWindow::execEngineWithLastPartyText() const {
    if (!prop_) return menuText(MenuID::ExecEngineWithLastParty);
    const auto& party = valueOf(prop_->var().lastExecutedParty);
    if (party.enginePath.empty()) return menuText(MenuID::ExecEngineWithLastParty);
    return ::cwxeditor::cwx::utils::tryFormat(
        prop_->requiredMsgs().execEngineWithLastParty.value, party.engineName,
        party.yadoName, party.partyName);
}

void MainWindow::execEngineP(String path, String scenario, ExecutionParty ep) {
    if (path.empty()) return;
    const auto original_path = path;
    String executable = path;
    std::vector<String> args;
    if (lower(pathFromUtf8(path).extension().string()) == ".py" && prop_) {
        const auto python = splitCommandLine(valueOf(prop_->var().pythonCommand));
        if (!python.empty()) {
            executable = python.front();
            args.assign(python.begin() + 1, python.end());
            args.push_back(path);
        }
    }
    if (!scenario.empty()) {
        args.insert(args.end(), {"-debug", "-yado", ep.yadoPath,
                                 "-party", ep.partyPath, "-scenario", scenario});
    }
    sync_.sync();
    const auto working_directory =
        pathFromUtf8(dirname(::cwxeditor::cwx::utils::nabs(
            StringView(original_path))));
    bool launched = false;
    if (!ep.isClassic && context_.executeEngine)
        launched = context_.executeEngine(executable, args, working_directory);
    if (!launched && services_.processes) {
        lib::ProcessLaunchRequest request;
        request.executable = pathFromUtf8(executable);
        request.arguments = args;
        request.working_directory = working_directory;
        request.terminate_on_handle_release = false;
        std::string error;
        launched = static_cast<bool>(services_.processes->launch(request, error));
        if (!launched && !error.empty()) setStatusLine(error);
    }
    if (!launched) {
        if (prop_) {
            const auto message = ::cwxeditor::cwx::utils::tryFormat(
                prop_->requiredMsgs().errorExecEngine.value, basename(original_path));
            setStatusLine(message);
            if (services_.dialogs) {
                lib::MessageDialogRequest warning;
                warning.title = prop_->requiredMsgs().dlgTitWarning.value;
                warning.message = message;
                warning.severity = lib::MessageSeverity::Warning;
                warning.buttons = lib::MessageButtons::OK;
                (void)services_.dialogs->message(warning);
            }
        }
        return;
    }
    if (!scenario.empty() && prop_) {
        prop_->var().lastExecutedParty = ep;
        if (valueOf(prop_->var().executedPartiesMax) > 0) {
            auto& history = prop_->var().executedParties.value;
            history.erase(std::remove_if(history.begin(), history.end(),
                                         [&ep](const auto& item) { return sameParty(item, ep); }),
                           history.end());
            history.insert(history.begin(), ep);
            const auto settings = partyhistory_ns::partyHistorySettings(
                prop_->var(), &prop_->sys());
            history = partyhistory_ns::removePartyHistoryNonExisting(
                settings,
                static_cast<bool>(valueOf(
                    prop_->var().removePartyHistoryNonExistingWithPriority)),
                std::move(history),
                static_cast<std::size_t>(valueOf(prop_->var().executedPartiesMax)));
        } else {
            prop_->var().executedParties.value.clear();
        }
        if (comm_) {
            comm_->refExecutedParties.call();
            comm_->refreshToolBar();
        }
    }
}

bool MainWindow::canExecEngine() const {
    return !execEnginePath().empty() &&
           (static_cast<bool>(context_.executeEngine) || services_.processes != nullptr);
}

bool MainWindow::canExecEngineWithLastParty() const {
    if (!prop_ || !canExecEngineWithParty()) return false;
    const auto& party = valueOf(prop_->var().lastExecutedParty);
    const auto& path = party.enginePath;
    if (path.empty()) return false;
    if (context_.executeEngine && !party.isClassic) return true;
    std::error_code error;
    return std::filesystem::is_regular_file(pathFromUtf8(path), error) && !error;
}

bool MainWindow::canExecEngineWithLastParty2() const {
    return canExecEngineWithLastParty() &&
           (!valueOf(prop_->var().lastExecutedParty).isClassic || canExecClassic());
}

bool MainWindow::canExecEngineWithPartyMenu() const {
    // D版と同様、保存済みのパーティのブックマーク・履歴用にドロップ
    // ダウンを利用可能にする。 各行の起動条件は、それぞれ維持する。
    const bool saved_party_entries = prop_ &&
        (!valueOf(prop_->var().executedPartyBookmarks).empty() ||
         !valueOf(prop_->var().executedParties).empty());
    return (!lib::editor_window_host_options().all_mode && canExecEngine()) ||
           canExecEngineWithParty() ||
           (prop_ && services_.processes &&
            !valueOf(prop_->var().classicEngines).empty()) ||
           saved_party_entries;
}

bool MainWindow::canExecEngineWithParty() const {
    // D版と同様、一時的なWSNに保存済みの元アーカイブパスができるまではパーティ起
    // 動を許可しない。 展開先ディレクトリは、保存済みのシナリオの指定先ではない。
    if (!lib::editor_window_host_options().all_mode && summary_ &&
        summary_->useTemp() && summary_->origZipName().empty()) return false;
    return summary_ && summary_->readOnlyPath().empty() && !origScenarioPath().empty() &&
           (static_cast<bool>(context_.executeEngine) || services_.processes != nullptr);
}

void MainWindow::execEngine() {
    const auto path = execEnginePath();
    // シナリオを開いただけでは、自動実行をパーティ起動に切り替えない。
    if (!path.empty()) execEngineP(path);
}

void MainWindow::execEngineWithLastParty() {
    if (!summary_ || !prop_) return;
    if (canExecEngineWithLastParty2()) {
        const auto party = valueOf(prop_->var().lastExecutedParty);
        execEngineP(party.enginePath, origScenarioPath(), party);
        return;
    }
    // D版と同様、使用可能な前回のパーティがない場合、ボタン本体を選択画面の起動に使う。
    const auto found = tool_items_.find(MenuID::ExecEngineWithParty);
    if (found != tool_items_.end()) dmenu::showDropDownMenu(found->second, tool_exec_party_);
}

bool MainWindow::canExecClassic() const {
    return services_.processes && summary_ && summary_->legacy() &&
           !summary_->useTemp();
}

String MainWindow::execEnginePath() const {
    if (!prop_) return {};
    // 組み込み実行では、古い設定から単独版の実行ファイルを選ばない。
    if (context_.executeEngine && !app_path_.empty()) return app_path_;
    auto path = summary_ && skin_ ? skin_->executeEngine() : prop_->enginePath();
    const bool selected_engine_path = !path.empty();
    // 既存の追加機能: シナリオ未読込で起動パスが空なら、設定の実行ファイル名を
    // 起動先の候補にする。D版の空パスを返す動作との差を残している。
    if (path.empty() && !summary_) path = valueOf(prop_->var().engine);
    if (!path.empty()) path = prop_->parent().toAppAbs(path);
    // D版と同様、選択したスキン・設定済みエンジンのパスは、存在し
    // なくても返す。 自動検出によって明示的な選択を消さない。 C++
    // 版の空設定時の代替処理と、組み込み実行の方針は維持する。
    if (!lib::editor_window_host_options().all_mode && selected_engine_path) return path;
    if (!path.empty() && pathExists(path)) return path;
    if (!path.empty() && services_.shell && !valueOf(prop_->var().findEnginePath)) return path;
    return {};
}

void MainWindow::openDataWindow() {
    if (docking_folder_ && docking_folder_->control("data")) {
        dock_.select("data");
        (void)docking_folder_->select("data", true);
    } else if (comm_) {
        comm_->openDataWin(true);
    }
}
void MainWindow::openFlagWindow() {
    if (docking_folder_ && docking_folder_->control("flag")) {
        dock_.select("flag");
        (void)docking_folder_->select("flag", true);
    } else if (comm_) {
        comm_->openFlagWin(true);
    }
}
void MainWindow::openDirWindow() {
    if (docking_folder_ && docking_folder_->control("file")) {
        dock_.select("file");
        (void)docking_folder_->select("file", true);
    } else if (comm_) {
        comm_->openDirWin(true);
    }
}
void MainWindow::openCouponWindow() {
    if (docking_folder_ && docking_folder_->control("coupon")) {
        dock_.select("coupon");
        (void)docking_folder_->select("coupon", true);
    } else if (comm_) {
        comm_->openCouponWin(true);
    }
}
void MainWindow::openGossipWindow() { if (comm_) comm_->openGossipWin(true); }
void MainWindow::openCompleteStampWindow() { if (comm_) comm_->openCompleteStampWin(true); }
void MainWindow::openKeyCodeWindow() { if (comm_) comm_->openKeyCodeWin(true); }
void MainWindow::openCellNameWindow() { if (comm_) comm_->openCellNameWin(true); }
void MainWindow::openCardGroupWindow() {
    if (isAllMode()) return;
    if (comm_) comm_->openCardGroupWin(true);
}

void MainWindow::exitAll() {
    if (close_pending_) return;
    lib::editor_action_log(
        "Exit requested source=MainWindow::exitAll visible=" +
        std::to_string(window_ && window_->visible() ? 1 : 0) +
        " quit_before=" + std::to_string(quit_ ? 1 : 0));
    if (!qSave(QSaveType::close)) return;
    // D版の ShellListener と同様、子ウィンドウを閉じる前に、現在
    // のシナリオのドッキング・履歴表記を確定する。 その後、DisposeListener
    // に相当する処理で lastScenario と主ウィンドウの境界を
    // 記録し、 イベントループの終了処理でプロパティツリー全体を保存する。
    // 下の未使用の互換リスナークラスに任せず、ここでその順序を維持する。
    if (summary_ && comm_ && !comm_->isChanged()) writeDock();
    if (!saveDockState()) {
        setStatusLine("ウィンドウ配置を保存できませんでした。");
        return;
    }
    if (settings_dialog_) {
        settings_dialog_->forceCancel();
        settings_dialog_.reset();
    }
    for (auto& [key, weak] : event_windows_) {
        (void)key;
        if (const auto editor = weak.lock()) editor->closeActiveDialog();
    }
    if (!stopBackgroundLoads()) {
        close_pending_ = true;
        if (window_) window_->set_enabled(false);
        setStatusLine("読み込みを中断して終了しています…");
        return;
    }
    finishCloseAfterLoads();
}

void MainWindow::finishCloseAfterLoads() {
    close_pending_ = false;
    // C++版ではD版の破棄経路と異なり、設定を保存できない場合は意図的に
    // 終了を拒否し、再試行できる画面を残す。 ワーカーのキャンセル後も、
    // qSave を繰り返さず同じ方針を適用する。 SDLバックエンドは
    // close() でネイティブハンドルを破棄するため、その前に最終
    // のデスクトップ座標を取得する。 その後の Window::bounds
    // には、クライアント領域の配置原点 (0,0) しか残らない。
    persistMainWindowState();
    if (!main_window_state_saved_ && !session_state_persistence_suppressed_) {
        quit_ = false;
        home_return_confirmed_ = false;
        if (window_) window_->set_enabled(true);
        setStatusLine("ウィンドウ情報を保存できませんでした。");
        return;
    }
    quit_ = true;
    if (services_.gui && window_) services_.gui->close(window_);
}

bool MainWindow::saveSessionStateForShutdown() noexcept {
    return saveSessionStateForShutdownImpl(nullptr);
}

bool MainWindow::saveSessionStateForShutdownImpl(
    const std::optional<String>* frozenHistory) noexcept {
    if (session_state_persistence_suppressed_) return true;
    if (main_window_state_saved_) return true;
    try {
        // ホストが exitAll() を呼ばずにエディタを閉
        // じる場合も、同じ履歴・設定の保存方針を使う。 シナリ
        // オ内容の保存や、追加の終了確認ダイアログは行わない。
        if (frozenHistory) {
            if (*frozenHistory && !(*frozenHistory)->empty() && prop_) {
                const auto scenario = history::fullHistToHist(**frozenHistory);
                for (auto* entries : {&prop_->var().scenarioBookmarks.value,
                                      &prop_->var().openHistories.value})
                    for (auto& entry : *entries)
                        if (sameHistoryPath(entry.path, scenario, services_.filesystem.get()))
                            entry.path = **frozenHistory;
            }
        } else if (summary_ && comm_ && !comm_->isChanged()) {
            writeDock();
        }
        if (settings_dialog_) {
            settings_dialog_->forceCancel();
            settings_dialog_.reset();
        }
        if (prop_) prop_->var().imeMode.value = lib::editor_ime_mode();
        if (!saveDockState()) return false;
        persistMainWindowState();
    } catch (const std::exception& error) {
        utils::debugln("save editor session settings: ", error.what());
    } catch (...) {
        utils::debugln("save editor session settings failed");
    }
    return main_window_state_saved_;
}

void MainWindow::cancelShutdownAfterStorageFailure() {
    main_window_state_saved_ = false;
    quit_ = false;
    close_pending_ = false;
    home_return_confirmed_ = false;
    if (window_) {
        window_->set_enabled(true);
        if (!window_->visible() && services_.gui) services_.gui->show(window_);
    }
}

void MainWindow::suppressSessionStatePersistence() noexcept {
    session_state_persistence_suppressed_ = true;
}

void MainWindow::persistMainWindowState() noexcept {
    if (main_window_state_saved_ || session_state_persistence_suppressed_ ||
        !prop_ || !window_) return;
    rememberToolbarWraps();
    // AbsDialog は初期設定時のウィンドウへの接続を解除するときに、最
    // 終の DSize/WSize を通知する。 モードレスダイアログの存続中
    // に MainWindow の終了が要求される場合もあるため、 FlexProps
    // 全体の保存前に、有効な全ダイアログの状態を取得する。 主ウィンドウ
    // だけが保存され、最後のダイアログのサイズ変更が失われることを防ぐ。
    for (auto* dialog : absdialog::AbsDialog::liveDialogs()) {
        if (!dialog) continue;
        try {
            dialog->saveWin();
        } catch (const std::exception& error) {
            utils::debugln("shutdown dialog geometry: ", error.what());
        } catch (...) {
            utils::debugln("shutdown dialog geometry failed");
        }
    }
    // Window::bounds はクライアント座標で配置され、通常のル
    // ート配置で原点が (0,0) になる。 D版が破棄時にSWTウィンド
    // ウの境界を記録するのと同様、ネイティブのデスクトップ座標を保存する。
    const auto bounds = services_.gui ? services_.gui->window_bounds(window_)
                                      : window_->bounds();
    auto& state = prop_->flex().mainWin;
    // D版と同様、x/y は常に保存する。最大化中は、直前の通常サイズを保持する。
    state.x(static_cast<std::int32_t>(std::lround(bounds.x)));
    state.y(static_cast<std::int32_t>(std::lround(bounds.y)));
    if (!window_->maximized) {
        state.width(static_cast<std::int32_t>(std::lround(bounds.width)));
        state.height(static_cast<std::int32_t>(std::lround(bounds.height)));
    }
    state.maximized(window_->maximized);
    // D版の DisposeListener と同様、常にこの値を更
    // 新する。 シナリオを開かずに終了した場合は、古いパスも消す。
    prop_->var().lastScenario = summary_
        ? history::createHistString(summary_.get())
        : String{};
    main_window_state_saved_ = prop_->apply();
}

void MainWindow::configureReplaceDialog(
    std::function<void(replacedialog_ns::ReplaceDialog&)> request) {
    const auto live = [weak=std::weak_ptr<std::atomic_bool>(lifecycle_alive_)] {
        const auto life=weak.lock();return life && life->load(std::memory_order_acquire);
    };
    if (!request || !live()) return;
    ensureReplaceDialog();
    if (!live()) return;
    const auto dialog=actual_replace_dialog_;
    if (!dialog) return;
    dialog->open();
    if (!live()) return;
    request(*dialog);
    if (!live()) return;
    if (auto state=openReplWin()) state->show();
}

void MainWindow::ensureReplaceDialog() {
    if (!actual_replace_dialog_) {
        replacedialog::Commons replacement_comm;
        replacement_comm.services = services_;
        replacement_comm.clipboard = services_.clipboard;
        replacement_comm.menuText = name_comm_.menuText;
        replacement_comm.menuImage = name_comm_.menuImage;
        replacement_comm.convertCoupon = name_comm_.convertCoupon;
        replacement_comm.convertKeyCode = name_comm_.convertKeyCode;
        replacement_comm.copyConverted = name_comm_.copyConverted;
        replacement_comm.refreshToolBar = [this] {
            if (comm_) comm_->refreshToolBar();
        };
        replacement_comm.refIds = [this] {
            if (comm_) comm_->replID.call();
            refreshAll({});
        };
        replacement_comm.refUseCount = [this] {
            if (comm_) comm_->refUseCount.call();
        };
        replacement_comm.replText = [this] {
            // 共通の通知で購読中の全エディタは更新済み。 さらに refreshAll
            // を呼ぶと、シーンのフラグ行を二度再構築してしまう。
            if (comm_) comm_->replText.call();
        };
        replacement_comm.refNameID = [this](const namewindow_ns::NameIDKind kind) {
            if (!comm_) return;
            switch (kind) {
            case namewindow_ns::NameIDKind::Coupon: comm_->refCoupons.call(); break;
            case namewindow_ns::NameIDKind::Gossip: comm_->refGossips.call(); break;
            case namewindow_ns::NameIDKind::CompleteStamp: comm_->refCompleteStamps.call(); break;
            case namewindow_ns::NameIDKind::KeyCode: comm_->refKeyCodes.call(); break;
            case namewindow_ns::NameIDKind::CellName: comm_->refCellNames.call(); break;
            case namewindow_ns::NameIDKind::CardGroup: comm_->refCardGroups.call(); break;
            }
        };
        replacement_comm.setStatusLine = [this](lib::Widget*, StringView status) {
            setStatusLine(String(status));
        };
        replacedialog::ReplaceProperties properties;
        properties.editor_commons = comm_;
        properties.textUndoLimit = [this, weak = std::weak_ptr<std::atomic_bool>(lifecycle_alive_)] {
            const auto life = weak.lock();
            if (!life || !life->load(std::memory_order_acquire)) return std::size_t{};
            return static_cast<std::size_t>(std::max<std::int32_t>(0, prop_->var().undoMaxEtc.value));
        };
        properties.webAllScreenFit = isAllMode();
        auto& options = properties.options;
        const auto& variables = prop_->var();
        options.notIgnoreCase = valueOf(variables.replaceTextNotIgnoreCase);
        options.regex = valueOf(variables.replaceTextRegExp);
        options.wildcard = valueOf(variables.replaceTextWildcard);
        options.exact = valueOf(variables.replaceTextExactMatch);
        options.ignoreReturnCode = valueOf(variables.replaceTextIgnoreReturnCode);
        options.cautionBeforeReplace = valueOf(variables.cautionBeforeReplace);
        options.summary = valueOf(variables.replaceTextSummary);
        options.scenario = valueOf(variables.replaceTextScenario);
        options.author = valueOf(variables.replaceTextAuthor);
        options.message = valueOf(variables.replaceTextMessage);
        options.cardName = valueOf(variables.replaceTextCardName);
        options.cardDescription = valueOf(variables.replaceTextCardDescription);
        options.eventText = valueOf(variables.replaceTextEventText);
        options.start = valueOf(variables.replaceTextStart);
        options.variableName = valueOf(variables.replaceTextVariableName);
        options.variableValue = valueOf(variables.replaceTextVariableValue);
        options.expression = valueOf(variables.replaceTextExpression);
        options.coupon = valueOf(variables.replaceTextCoupon);
        options.gossip = valueOf(variables.replaceTextGossip);
        options.endScenario = valueOf(variables.replaceTextEndScenario);
        options.areaName = valueOf(variables.replaceTextAreaName);
        options.keyCode = valueOf(variables.replaceTextKeyCode);
        options.cellName = valueOf(variables.replaceTextCellName);
        options.cardGroup = valueOf(variables.replaceTextCardGroup);
        options.file = valueOf(variables.replaceTextFile);
        options.comment = valueOf(variables.replaceTextComment);
        options.jptx = valueOf(variables.replaceTextJptx);
        options.textFile = valueOf(variables.replaceTextTextFile);
        for (std::size_t index = 0; index < options.contents.size(); ++index)
            options.contents[index] = variables.searchContents(
                static_cast<types::CType>(index));
        options.replaceNameCoupon = valueOf(variables.replaceNameCoupon);
        options.replaceNameGossip = valueOf(variables.replaceNameGossip);
        options.replaceNameEndScenario = valueOf(variables.replaceNameEndScenario);
        options.replaceNameKeyCode = valueOf(variables.replaceNameKeyCode);
        options.replaceNameCellName = valueOf(variables.replaceNameCellName);
        options.replaceNameCardGroup = valueOf(variables.replaceNameCardGroup);
        options.unusedFlag = valueOf(variables.searchUnusedFlag);
        options.unusedStep = valueOf(variables.searchUnusedStep);
        options.unusedVariant = valueOf(variables.searchUnusedVariant);
        options.unusedArea = valueOf(variables.searchUnusedArea);
        options.unusedBattle = valueOf(variables.searchUnusedBattle);
        options.unusedPackage = valueOf(variables.searchUnusedPackage);
        options.unusedCast = valueOf(variables.searchUnusedCast);
        options.unusedSkill = valueOf(variables.searchUnusedSkill);
        options.unusedItem = valueOf(variables.searchUnusedItem);
        options.unusedBeast = valueOf(variables.searchUnusedBeast);
        options.unusedInfo = valueOf(variables.searchUnusedInfo);
        options.unusedStart = valueOf(variables.searchUnusedStart);
        options.unusedPath = valueOf(variables.searchUnusedPath);
        options.grepSubDirectory = valueOf(variables.grepSubDir);
        options.searchResultRealtime = valueOf(variables.searchResultRealtime);
        options.searchOpenDialog = valueOf(variables.searchOpenDialog);
        options.showAreaDirectoryTree = valueOf(variables.showAreaDirTree);
        options.showRouteOfSearchResult =
            valueOf(variables.showRouteOfSearchResult);
        options.logicalSort = valueOf(variables.logicalSort);
        options.searchPlan = static_cast<std::size_t>(
            std::max<std::int32_t>(0, valueOf(variables.searchPlan)));
        options.searchIDKind = static_cast<std::size_t>(
            std::max<std::int32_t>(0, valueOf(variables.searchIDKind)));
        options.replaceRangeSashLeft = static_cast<float>(
            valueOf(variables.replaceRangeSashL));
        options.replaceRangeSashRight = static_cast<float>(
            valueOf(variables.replaceRangeSashR));
        options.searchResultRefreshCount = static_cast<std::size_t>(
            std::max<std::int32_t>(0, valueOf(variables.searchResultRefreshCount)));
        options.searchHistoryMax = static_cast<std::size_t>(
            std::max<std::int32_t>(0, valueOf(variables.searchHistoryMax)));
        options.undoMaxReplace = static_cast<std::size_t>(
            std::max<std::int32_t>(0, valueOf(variables.undoMaxReplace)));
        options.searchHistories = valueOf(variables.searchHistories);
        options.replaceHistories = valueOf(variables.replaceHistories);
        options.grepDirectoryHistories = valueOf(variables.grepDirHistories);
        options.grepDirectory = valueOf(variables.grepDir);
        options.searchErrorTargetVersion = valueOf(variables.searchErrorTargetVersion);
        properties.plainTextFileExtensions =
            valueOf(variables.plainTextFileExtensions);
        properties.resultTableSize = {
            static_cast<float>(valueOf(variables.searchResultTableWidth)),
            static_cast<float>(valueOf(variables.searchResultTableHeight))};
        properties.resultColumnMain = static_cast<float>(
            valueOf(variables.searchResultColumnMain));
        properties.resultColumnParent = static_cast<float>(
            valueOf(variables.searchResultColumnParent));
        properties.resultColumnCouponCount = static_cast<float>(
            valueOf(variables.searchResultColumnCouponCount));
        properties.resultColumnErrorDescription = static_cast<float>(
            valueOf(variables.searchResultColumnErrorDesc));
        properties.resultColumnScenario = static_cast<float>(
            valueOf(variables.searchResultColumnScenario));
        properties.storeProperties = [this](const replacedialog::ReplaceProperties& saved) {
            if (!prop_) return;
            const auto& source = saved.options;
            auto& target = prop_->var();
            target.replaceTextNotIgnoreCase.value = source.notIgnoreCase;
            target.replaceTextRegExp.value = source.regex;
            target.replaceTextWildcard.value = source.wildcard;
            target.replaceTextExactMatch.value = source.exact;
            target.replaceTextIgnoreReturnCode.value = source.ignoreReturnCode;
            target.cautionBeforeReplace.value = source.cautionBeforeReplace;
            target.replaceTextSummary.value = source.summary;
            target.replaceTextScenario.value = source.scenario;
            target.replaceTextAuthor.value = source.author;
            target.replaceTextMessage.value = source.message;
            target.replaceTextCardName.value = source.cardName;
            target.replaceTextCardDescription.value = source.cardDescription;
            target.replaceTextEventText.value = source.eventText;
            target.replaceTextStart.value = source.start;
            target.replaceTextVariableName.value = source.variableName;
            target.replaceTextVariableValue.value = source.variableValue;
            target.replaceTextExpression.value = source.expression;
            target.replaceTextCoupon.value = source.coupon;
            target.replaceTextGossip.value = source.gossip;
            target.replaceTextEndScenario.value = source.endScenario;
            target.replaceTextAreaName.value = source.areaName;
            target.replaceTextKeyCode.value = source.keyCode;
            target.replaceTextCellName.value = source.cellName;
            target.replaceTextCardGroup.value = source.cardGroup;
            target.replaceTextFile.value = source.file;
            target.replaceTextComment.value = source.comment;
            target.replaceTextJptx.value = source.jptx;
            target.replaceTextTextFile.value = source.textFile;
            for (std::size_t index = 0; index < source.contents.size(); ++index)
                target.searchContents(static_cast<types::CType>(index),
                                      source.contents[index]);
            target.replaceNameCoupon.value = source.replaceNameCoupon;
            target.replaceNameGossip.value = source.replaceNameGossip;
            target.replaceNameEndScenario.value = source.replaceNameEndScenario;
            target.replaceNameKeyCode.value = source.replaceNameKeyCode;
            target.replaceNameCellName.value = source.replaceNameCellName;
            target.replaceNameCardGroup.value = source.replaceNameCardGroup;
            target.searchUnusedFlag.value = source.unusedFlag;
            target.searchUnusedStep.value = source.unusedStep;
            target.searchUnusedVariant.value = source.unusedVariant;
            target.searchUnusedArea.value = source.unusedArea;
            target.searchUnusedBattle.value = source.unusedBattle;
            target.searchUnusedPackage.value = source.unusedPackage;
            target.searchUnusedCast.value = source.unusedCast;
            target.searchUnusedSkill.value = source.unusedSkill;
            target.searchUnusedItem.value = source.unusedItem;
            target.searchUnusedBeast.value = source.unusedBeast;
            target.searchUnusedInfo.value = source.unusedInfo;
            target.searchUnusedStart.value = source.unusedStart;
            target.searchUnusedPath.value = source.unusedPath;
            target.grepSubDir.value = source.grepSubDirectory;
            target.searchResultRealtime.value = source.searchResultRealtime;
            target.searchOpenDialog.value = source.searchOpenDialog;
            target.searchPlan.value = static_cast<std::int32_t>(source.searchPlan);
            target.searchIDKind.value = static_cast<std::int32_t>(source.searchIDKind);
            target.replaceRangeSashL.value = static_cast<std::int32_t>(
                std::lround(source.replaceRangeSashLeft));
            target.replaceRangeSashR.value = static_cast<std::int32_t>(
                std::lround(source.replaceRangeSashRight));
            target.searchResultRefreshCount.value = static_cast<std::int32_t>(
                source.searchResultRefreshCount);
            target.searchHistories.value = source.searchHistories;
            target.replaceHistories.value = source.replaceHistories;
            target.grepDirHistories.value = source.grepDirectoryHistories;
            target.grepDir.value = source.grepDirectory;
            target.searchErrorTargetVersion.value = source.searchErrorTargetVersion;
            target.searchResultColumnMain.value = static_cast<std::int32_t>(
                std::lround(saved.resultColumnMain));
            target.searchResultColumnParent.value = static_cast<std::int32_t>(
                std::lround(saved.resultColumnParent));
            target.searchResultColumnCouponCount.value = static_cast<std::int32_t>(
                std::lround(saved.resultColumnCouponCount));
            target.searchResultColumnErrorDesc.value = static_cast<std::int32_t>(
                std::lround(saved.resultColumnErrorDescription));
            target.searchResultColumnScenario.value = static_cast<std::int32_t>(
                std::lround(saved.resultColumnScenario));
            (void)prop_->apply();
        };
        // 検索ダイアログ全体に、選択されたメッセージカタログを使う。
        {
            const auto& m = prop_->requiredMsgs();
            auto& rm = properties.messages;
            rm.dialogTitle = m.dlgTitReplaceText.value;
            rm.search = m.search.value;
            rm.replace = m.replace.value;
            rm.close = m.dlgTextClose.value;
            rm.cancel = m.searchCancel.value;
            rm.searchResultEmpty = m.searchResultEmpty.value;
            rm.replaceResultEmpty = m.replResultEmpty.value;
            rm.searchResult = m.searchResult.value;
            rm.searchResultGrep1 = m.searchResultGrep1.value;
            rm.searchResultGrep2 = m.searchResultGrep2.value;
            rm.searchResultGrep3 = m.searchResultGrep3.value;
            rm.replaceResult = m.replResult.value;
            rm.cautionOfReplace = m.cautionOfReplace.value;
            rm.searchRange = m.searchRange.value;
            rm.localVariableUsers = m.replLocalVariables.value;
            rm.startUsers = m.replStartUsers.value;
            rm.rangeSummary = m.summary.value;
            rm.rangeVariables = m.flagsAndSteps.value;
            rm.allCheckRange = m.allCheckRange.value;
            rm.allCheck = m.allCheck.value;
            rm.allSelect = m.allSelect.value;
            rm.searchResultRealtime = m.searchResultRealtime.value;
            rm.searchOpenDialog = m.searchOpenDialog.value;
            rm.searchOpenDialogHint = m.searchOpenDialogHint.value;
            rm.searchResultColumnMain = m.searchResultColumnMain.value;
            rm.searchResultColumnParent = m.searchResultColumnParent.value;
            rm.searchResultColumnDescription = m.searchResultColumnErrorDesc.value;
            rm.searchResultColumnCoupon = m.searchResultColumnCoupon.value;
            rm.searchResultColumnCouponCount = m.searchResultColumnCouponCount.value;
            rm.searchResultColumnError = m.searchResultColumnError.value;
            rm.searchResultColumnScenario = m.searchResultColumnScenario.value;
            rm.tabText = m.replForText.value;
            rm.tabID = m.replForID.value;
            rm.tabPath = m.replForPath.value;
            rm.tabContents = m.replContents.value;
            rm.tabCoupon = m.replForCoupon.value;
            rm.tabUnused = m.replForUnuse.value;
            rm.tabError = m.replForError.value;
            rm.tabGrep = m.replGrep.value;
            rm.from = m.replFrom.value;
            rm.to = m.replTo.value;
            rm.textGroup = m.replText.value;
            rm.textTargetGroup = m.replTextTarget.value;
            rm.conditionGroup = m.replCond.value;
            rm.notIgnoreCase = m.replNotIgnoreCase.value;
            rm.exact = m.replExactMatch.value;
            rm.ignoreReturnCode = m.replIgnoreReturnCode.value;
            rm.wildcard = m.replWildcard.value;
            rm.regex = m.replRegExp.value;
            rm.wildcardDescription = m.wildcardDesc.value;
            rm.incrementalSearch = m.menuText(types::MenuID::IncSearch);
            rm.idGroup = m.replID.value;
            rm.idKind = m.replIDKind.value;
            rm.directID = m.replSetID.value;
            rm.pathGroup = m.replPath.value;
            rm.grepTextGroup = m.grepText.value;
            rm.grepFrom = m.grepFrom.value;
            rm.grepTargetGroup = m.grepTarget.value;
            rm.grepDirectoryTitle = m.grepDir.value;
            rm.grepDirectoryDescription = m.grepDirDesc.value;
            rm.grepBrowse = m.reference.value;
            rm.grepOpen = m.menuText(types::MenuID::Open);
            rm.grepCurrent = m.grepCurrent.value;
            rm.grepSubDirectory = m.grepSubDir.value;
            rm.errorDescription = m.replError.value;
            rm.errorConditionGroup = m.searchErrorCondition.value;
            rm.errorTargetVersion = m.searchErrorTargetVersion.value;
            rm.errorTargetVersionNotSet = m.searchErrorTargetVersionNotSet.value;
            rm.errorTargetVersionHint = m.searchErrorTargetVersionHint.value;
            rm.unusedHint = m.findUnusedHint.value;
            rm.idKinds = {m.replIDArea.value, m.replIDBattle.value, m.replIDPackage.value, m.replIDCast.value, m.replIDSkill.value, m.replIDItem.value, m.replIDBeast.value, m.replIDInfo.value, m.replIDFlag.value, m.replIDStep.value, m.replIDVariant.value, m.replIDCoupon.value, m.replIDGossip.value, m.replIDCompleteStamp.value, m.replIDKeyCode.value, m.replIDCellName.value, m.replIDCardGroup.value};
            rm.textTargets = {m.replTextSummary.value + "(&1)", m.replTextScenario.value + "(&2)", m.replTextAuthor.value + "(&3)", m.replTextMessage.value + "(&4)", m.replTextCardName.value + "(&5)", m.replTextCardDesc.value + "(&6)", m.replTextEventText.value + "(&7)", m.replTextStart.value + "(&8)", m.replTextVariableName.value + "(&9)", m.replTextVariableValue.value + "(&A)", m.replTextExpression.value + "(&B)", m.replTextCoupon.value + "(&D)", m.replTextGossip.value + "(&E)", m.replTextEndScenario.value + "(&G)", m.replTextAreaName.value + "(&H)", m.replTextKeyCode.value + "(&I)", m.replTextCellName.value + "(&K)", m.replTextCardGroup.value + "(&L)", m.replTextFile.value + "(&M)", m.replTextComment.value + "(&N)", m.replTextJptx.value + "(&O)", m.replTextTextFile.value + "(&P)"};
            rm.nameTargets = {m.replTextCoupon.value + "(&1)", m.replTextGossip.value + "(&2)", m.replTextEndScenario.value + "(&3)", m.replTextKeyCode.value + "(&4)", m.replTextCellName.value + "(&5)", m.replTextCardGroup.value + "(&6)"};
            rm.unusedTargets = {m.replUnuseFlag.value + "(&1)", m.replUnuseStep.value + "(&2)", m.replUnuseVariant.value + "(&3)", m.replUnuseArea.value + "(&4)", m.replUnuseBattle.value + "(&5)", m.replUnusePackage.value + "(&6)", m.replUnuseCast.value + "(&7)", m.replUnuseSkill.value + "(&8)", m.replUnuseItem.value + "(&9)", m.replUnuseBeast.value + "(&A)", m.replUnuseInfo.value + "(&B)", m.replUnuseStart.value + "(&C)", m.replUnusePath.value + "(&D)"};
        }
        if (images_) {
            for (std::size_t index = 0U; index < properties.contentImages.size(); ++index) {
                try {
                    properties.contentImages[index] =
                        images_->content(static_cast<types::CType>(index));
                } catch (...) {
                    properties.contentImages[index] = 0U;
                }
            }
        }
        properties.rangeDirectoryImage = images_ ? images_->areaDir() : 0U;
        properties.openDirectoryImage = menuImage(MenuID::OpenDir);
        properties.rangeImage = [this](const replacedialog::CWXPath* path) -> lib::ResourceId {
            if (dynamic_cast<const ::cwxeditor::cwx::card::CastCard*>(path)) return menuImage(MenuID::CastView);
            if (dynamic_cast<const ::cwxeditor::cwx::card::SkillCard*>(path)) return menuImage(MenuID::SkillView);
            if (dynamic_cast<const ::cwxeditor::cwx::card::ItemCard*>(path)) return menuImage(MenuID::ItemView);
            if (dynamic_cast<const ::cwxeditor::cwx::card::BeastCard*>(path)) return menuImage(MenuID::BeastView);
            if (dynamic_cast<const ::cwxeditor::cwx::card::InfoCard*>(path)) return menuImage(MenuID::InfoView);
            if (!images_) return 0U;
            if (dynamic_cast<const ::cwxeditor::cwx::area::Area*>(path)) return images_->area();
            if (dynamic_cast<const ::cwxeditor::cwx::area::Battle*>(path)) return images_->battle();
            if (dynamic_cast<const ::cwxeditor::cwx::area::Package*>(path)) return images_->packages();
            if (dynamic_cast<const ::cwxeditor::cwx::summary::Summary*>(path)) return images_->summary();
            return menuImage(MenuID::VarView);
        };
        properties.source.openCWXPath = [this](StringView path, bool alternate) {
            return openCWXPath(String(path), alternate);
        };
        properties.source.symbols = [this](const replacedialog::CWXPath& object,
                                           const bool desc, String& text,
                                           lib::ResourceId& image) {
            return searchResultSymbols(object, desc, text, image);
        };
        properties.source.unusedBatchTargets =
            [this](replacedialog::ReplaceQueryContext& context) {
                auto icon_context = context;
                icon_context.publishBatch = [this, publish = context.publishBatch,
                                             summary = context.summary,
                                             extensions = context.plainTextFileExtensions]
                    (std::vector<replacedialog::TextTarget> targets) {
                    for (auto& target : targets) {
                        if (target.kind != "unusedpath") continue;
                        if (!summary || !skin_) {
                            target.image = directory_prop_.images.unknown;
                            continue;
                        }
                        const auto full_path = pathFromUtf8(summary->scenarioPath()) /
                                               pathFromUtf8(target.path);
                        target.image = dutils::fimage(
                            prop_, pathToUtf8(full_path), skin_.get(), {}, &extensions);
                    }
                    return !publish || publish(std::move(targets));
                };
                return replacedialog::UnusedListingProvider::run(
                    icon_context,
                    {skin_.get(), prop_ ? &prop_->var().ignorePaths.value : nullptr});
            };
        properties.source.errorBatchTargets =
            [this](replacedialog::ReplaceQueryContext& context) {
                replaceerror::ProviderConfig config;
                if (prop_) {
                    config.properties = &prop_->parent();
                    config.defaultTargetVersion = prop_->var().targetVersion.value;
                    config.ignorePaths = prop_->var().ignorePaths.value;
                }
                config.skin = skin_;
                return replaceerror::Provider{std::move(config)}(context);
            };
        properties.errorVersions = replaceerror::versionChoices();
        replacegrep::ProviderOptions grep_options;
        grep_options.loadScenario = replacegrep::makeStandardScenarioLoader(prop_, &sync_,
            [this, weak = std::weak_ptr<std::atomic_bool>(lifecycle_alive_)](
                const replacedialog::Summary* owner, const replacedialog::CWXPath& object,
                const bool desc, String& text, lib::ResourceId& image) {
                const auto life = weak.lock();
                return life && life->load(std::memory_order_acquire) &&
                    searchResultSymbols(object, desc, text, image, owner);
            });
        grep_options.archives = services_.archives;
        properties.source.grepBatchTargets = replacegrep::Provider{
            std::move(grep_options)};
        properties.source.openExternal =
            [this](const replacedialog::ExternalOpenTarget& target,
                   const bool open_dialog) {
                const bool local = summary_ &&
                    (target.scenarioPath.empty() ||
                     samePath(target.scenarioPath, summary_->scenarioPath(),
                              services_.filesystem.get()));
                if (target.kind == replacedialog::ExternalOpenKind::ScenarioCWXPath) {
                    if (target.cwxPath.empty()) return false;
                    String path_value = target.cwxPath;
                    if (open_dialog)
                        path_value = path::cpaddattr(path_value, "opendialog");
                    path_value = path::cpaddattr(path_value, "only");
                    if (local) return openCWXPath(std::move(path_value), false);
                    if (!services_.processes || app_path_.empty() ||
                        target.scenarioPath.empty()) return false;
                    lib::ProcessLaunchRequest request;
                    request.executable = pathFromUtf8(app_path_);
                    request.arguments = {target.scenarioPath, std::move(path_value)};
                    request.working_directory = pathFromUtf8(dirname(app_path_));
                    request.terminate_on_handle_release = false;
                    std::string error;
                    const bool launched = static_cast<bool>(
                        services_.processes->launch(request, error));
                    if (!launched && !error.empty()) setStatusLine(error);
                    return launched;
                }

                if (target.relativePath.empty()) return false;
                if (local)
                    return comm_ && comm_->openFilePath(target.relativePath,
                                                        false, true);
                if (!services_.processes || app_path_.empty() ||
                    target.scenarioPath.empty()) return false;
                lib::ProcessLaunchRequest request;
                request.executable = pathFromUtf8(app_path_);
                request.arguments = {"-selectfile", target.relativePath,
                                     target.scenarioPath};
                request.working_directory = pathFromUtf8(dirname(app_path_));
                request.terminate_on_handle_release = false;
                std::string error;
                const bool launched = static_cast<bool>(
                    services_.processes->launch(request, error));
                if (!launched && !error.empty()) setStatusLine(error);
                return launched;
            };
        actual_replace_dialog_ = std::make_shared<replacedialog::ReplaceDialog>(
            std::move(replacement_comm), std::move(properties),
            window_.get(), summary_, [this] { sendReloadProps(); });
    } else if (actual_replace_dialog_->summary() != summary_) {
        actual_replace_dialog_->summary(summary_);
    }
}

void MainWindow::replaceText() {
    const auto live = [weak=std::weak_ptr<std::atomic_bool>(lifecycle_alive_)] {
        const auto value=weak.lock(); return value && value->load(std::memory_order_acquire);
    };
    const bool existing = static_cast<bool>(actual_replace_dialog_);
    ensureReplaceDialog();
    if (!live()) return;
    const auto dialog = actual_replace_dialog_;
    const auto window = dialog->window();
    const auto gui = services_.gui;
    // D版の openReplWin と同様、既存の検索を再び有効化し
    // 、 現在の結果・選択行・置換の取り消し履歴を初期化し直さない。
    if (existing && window && (window->visible() || window->minimized)) {
        if (gui) {
            // ウィンドウを復元・前面化するが、フォーカスを受け取れない Window 自体には
            // フォーカスを移さない。 移すと現在の編集可能な子のフォーカスが解除されるため。
            gui->restore_window(window);
            if (!live()) return;
        }
    } else dialog->open();
    if (!live()) return;
    auto state = openReplWin();
    if (state) state->show();
}

void MainWindow::clipboardToXML() {
    if (!services_.clipboard) return;
    auto data = services_.clipboard->read();
    if (!xmlbytestransfer::convertXMLBytesToText(data)) {
        return;
    }
    services_.clipboard->write(data);
    if (comm_) comm_->refreshToolBar();
}

bool MainWindow::prepareExternalNavigation() {
    if (!qSave(QSaveType::close)) return false;

    // 「保存しない」の選択で、この文書の破棄を許可する。 エ
    // ディタを再表示する前にモデルを置き換え、その間に Explorer
    // 側の版へ上書き保存できないようにする。
    external_navigation_discarded_ = summary_ && summary_->isChanged();
    if (external_navigation_discarded_) summary_->resetChanged();
    external_navigation_fingerprint_ = scenarioFilesFingerprint();
    return true;
}

bool MainWindow::prepareHomeReturnAsync(std::function<void(bool)> completed) {
    return qSaveAsync(QSaveType::home, std::move(completed));
}

bool MainWindow::prepareExternalNavigationAsync(
    std::function<void(bool)> completed) {
    if (!completed) return false;
    return qSaveAsync(QSaveType::close,
        [this, completed = std::move(completed)](const bool accepted) mutable {
            if (accepted) {
                external_navigation_discarded_ = summary_ && summary_->isChanged();
                try {
                    external_navigation_fingerprint_ = scenarioFilesFingerprint();
                } catch (...) {
                    external_navigation_fingerprint_.reset();
                }
            }
            if (accepted && summary_ && summary_->isChanged()) {
                try {
                    summary_->resetChanged();
                } catch (const std::exception& error) {
                    setStatusLine(std::string("データ画面へ移る前の確認に失敗しました: ") +
                                  error.what());
                    completed(false);
                    return;
                } catch (...) {
                    setStatusLine("データ画面へ移る前の確認に失敗しました。");
                    completed(false);
                    return;
                }
            }
            completed(accepted);
        });
}

bool MainWindow::qSaveAsync(QSaveType type,
                            std::function<void(bool)> completed) {
    if (!completed || !services_.dialogs ||
        !services_.dialogs->supports_message_async() ||
        (type != QSaveType::close && type != QSaveType::home &&
         type != QSaveType::open && type != QSaveType::create))
        return false;

    const std::weak_ptr<std::atomic_bool> lifetime = lifecycle_alive_;
    const auto completed_once = std::make_shared<std::atomic_bool>(false);
    const auto completion = std::make_shared<std::function<void(bool)>>(
        std::move(completed));
    const auto finish = [lifetime, completed_once, completion](const bool accepted) {
        if (completed_once->exchange(true, std::memory_order_acq_rel)) return;
        const auto alive = lifetime.lock();
        if (!alive || !alive->load(std::memory_order_acquire)) return;
        if (*completion) (*completion)(accepted);
    };
    if (in_saving_) {
        finish(false);
        return true;
    }
    if (type == QSaveType::close && home_return_confirmed_) {
        finish(true);
        return true;
    }
    const bool closing = type == QSaveType::close || type == QSaveType::home;

    const auto ask = [this, lifetime, finish](
        lib::MessageDialogRequest request,
        std::function<void(lib::MessageResult)> decided) {
        const auto alive = lifetime.lock();
        if (!alive || !alive->load(std::memory_order_acquire) || !services_.dialogs) {
            finish(false);
            return;
        }
        try {
            if (services_.dialogs->message_async(
                    request, [lifetime, decided = std::move(decided)](
                                 const lib::MessageResult result) mutable {
                        const auto current = lifetime.lock();
                        if (!current || !current->load(std::memory_order_acquire)) return;
                        if (decided) decided(result);
                    }))
                return;
        } catch (const std::exception& error) {
            setStatusLine(std::string("確認ダイアログを開けませんでした: ") + error.what());
            finish(false);
            return;
        } catch (...) {
            setStatusLine("確認ダイアログを開けませんでした。");
            finish(false);
            return;
        }
        setStatusLine("確認ダイアログを開けませんでした。");
        finish(false);
    };

    const std::function<void()> continue_save_decision =
        [this, lifetime, finish, ask, type] {
            const auto alive = lifetime.lock();
            if (!alive || !alive->load(std::memory_order_acquire)) return;
            if (comm_) (void)comm_->isChanged();
            if (!alive->load(std::memory_order_acquire)) return;
            if (in_saving_) { finish(false); return; }
            if (!summary_ || !summary_->isChanged()) {
                const bool accepted = type != QSaveType::close ||
                    !context_.confirmClose || context_.confirmClose();
                if (type == QSaveType::home) home_return_confirmed_ = accepted;
                finish(accepted);
                return;
            }

            if (type == QSaveType::home) {
                lib::MessageDialogRequest request;
                request.title = "ホームに戻る";
                request.message = "未保存の変更を破棄してホームに戻りますか？ シナリオは上書き保存しません。";
                request.severity = lib::MessageSeverity::Question;
                request.buttons = lib::MessageButtons::OKCancel;
                ask(std::move(request), [this, lifetime, finish](
                        const lib::MessageResult result) {
                    const auto current = lifetime.lock();
                    if (!current || !current->load(std::memory_order_acquire)) return;
                    const bool accepted = result == lib::MessageResult::OK;
                    home_return_confirmed_ = accepted;
                    finish(accepted);
                });
                return;
            }

            if (context_.confirmClose && type == QSaveType::close &&
                !context_.confirmClose()) {
                finish(false);
                return;
            }
            lib::MessageDialogRequest request;
            request.title = prop_ ? prop_->requiredMsgs().dlgTitQuestion.value
                                  : "確認 - CWXEditor_C";
            const auto format = prop_
                ? prop_->requiredMsgs().dlgMsgIsSaveBeforeExit.value
                : StringView{"「%1$s」は変更されています。保存しますか？"};
            request.message = ::cwxeditor::cwx::utils::tryFormat(
                format, summary_->scenarioName());
            request.severity = lib::MessageSeverity::Question;
            request.buttons = lib::MessageButtons::YesNoCancel;
            ask(std::move(request), [this, finish, type](const lib::MessageResult result) {
                if (result == lib::MessageResult::Cancel ||
                    result == lib::MessageResult::None) {
                    finish(false);
                } else if (result == lib::MessageResult::Yes) {
                    // Web ALL では、ブラウザーの永続化が保留中でも
                    // 上書き保存が false を返す場合がある。 保存
                    // が完了してから、データ画面への移動などを続行する。
                    const auto expected_summary = summary_;
                    const bool saved = save(window_);
                    const auto saved_generation = edit_generation_;
                    const auto admitted = [this, finish, type, expected_summary, saved_generation] {
                        // 保存成功後もモードレスの結果ウィンドウが残る場合が
                        // ある。 その後のモデル編集や未適用のダイアログ入力
                        // があれば、以前の終了・表示への同意を無効にする。
                        const auto still_saved = [this, expected_summary, saved_generation] {
                            return summary_ == expected_summary &&
                                edit_generation_ == saved_generation &&
                                summary_ && !summary_->isChanged();
                        };
                        if (!still_saved()) { finish(false); return; }
                        bool pending_dialog{};
                        for (auto* dialog : editdialogsession::EditDialogSession::liveSessions()) {
                            const auto window = dialog ? dialog->editDialogWindow() : nullptr;
                            if (!dialog || !window || !window->visible()) continue;
                            if (type != QSaveType::close && type != QSaveType::home &&
                                dynamic_cast<settingsdialog::SettingsDialog*>(dialog)) continue;
                            if (dialog->editDialogPending()) pending_dialog = true;
                        }
                        if (!pending_dialog) { finish(true); return; }
                        // 登録側には未確定入力の変更番号がないため、以前
                        // からある入力でも通常の破棄確認を取り直す。 保
                        // 存済みモデルが未変更なら、未確定ダイアログの確
                        // 認だけを行い、保存・完了確認を繰り返さない。
                        if (!qSaveAsync(type, [finish, still_saved](bool accepted) {
                                finish(accepted && still_saved());
                            })) finish(false);
                    };
                    if (saved) afterOverwriteSaveAcknowledged(admitted);
                    else if (pending_persist_summary_.lock())
                        pending_persist_waiters_.push_back(
                            [this, finish, admitted](const bool persisted) {
                                if (persisted) afterOverwriteSaveAcknowledged(admitted);
                                else finish(false);
                            });
                    else finish(false);
                } else {
                    finish(true);
                }
            });
        };

    std::size_t unapplied_dialogs{};
    for (auto* dialog : editdialogsession::EditDialogSession::liveSessions()) {
        const auto window = dialog ? dialog->editDialogWindow() : nullptr;
        if (!dialog || !window || !window->visible()) continue;
        if (!closing &&
            dynamic_cast<settingsdialog::SettingsDialog*>(dialog))
            continue;
        if (dialog->editDialogPending()) ++unapplied_dialogs;
    }
    if (unapplied_dialogs == 0U) {
        continue_save_decision();
        return true;
    }

    lib::MessageDialogRequest request;
    request.title = prop_ ? prop_->requiredMsgs().dlgTitQuestion.value
                          : "確認 - CWXEditor_C";
    const auto format = prop_
        ? (closing
               ? prop_->requiredMsgs().dlgMsgForceCancelDialogsQuit.value
               : prop_->requiredMsgs().dlgMsgForceCancelDialogs.value)
        : StringView{"%1$s件のダイアログに未適用の変更があります。続行しますか？"};
    request.message = ::cwxeditor::cwx::utils::tryFormat(
        format, unapplied_dialogs);
    request.severity = lib::MessageSeverity::Question;
    request.buttons = lib::MessageButtons::YesNo;
    ask(std::move(request), [continue_save_decision, finish](
            const lib::MessageResult result) {
        if (result == lib::MessageResult::Yes) continue_save_decision();
        else finish(false);
    });
    return true;
}

String MainWindow::scenarioFilesFingerprint() const {
    if (!summary_) return {};
    String result;
    const auto stamp = [&result](const std::filesystem::path& path,
                                 const std::filesystem::path& base) {
        std::error_code error;
        const auto size = std::filesystem::is_regular_file(path, error)
            ? std::filesystem::file_size(path, error) : 0U;
        const auto time = std::filesystem::last_write_time(path, error);
        result += pathToUtf8(base.empty() ? path : path.lexically_relative(base));
        result += '\t';
        result += std::to_string(static_cast<unsigned long long>(size));
        result += '\t';
        result += std::to_string(static_cast<long long>(time.time_since_epoch().count()));
        result += '\n';
    };
    for (const auto& value : {summary_->origZipName(), summary_->readOnlyPath()})
        if (!value.empty()) stamp(pathFromUtf8(value), {});
    const auto root = pathFromUtf8(summary_->scenarioPath());
    std::error_code error;
    if (!root.empty() && std::filesystem::is_directory(root, error)) {
        std::vector<std::filesystem::path> files;
        for (std::filesystem::recursive_directory_iterator it(root, error), end;
             !error && it != end; it.increment(error))
            files.push_back(it->path());
        std::sort(files.begin(), files.end());
        for (const auto& file : files) stamp(file, root);
    }
    return result;
}

void MainWindow::reloadAfterExternalNavigation() {
    // prepareExternalNavigation
    // で保存・破棄・キャンセルの判断を取得済みなので、
    // 再確認しない。 再読み込みはモデル全体を置き換え、
    // 開いているシーン・イベントのビューを閉じて保存済み
    // ファイルへ戻す。 Data Explorer の表
    // 示中にディスク上の変更がなく、編集の破棄も選ばれ
    // ていなければ、エディタの状態をそのまま維持する。
    const auto before = std::exchange(external_navigation_fingerprint_, std::nullopt);
    const bool discarded = std::exchange(external_navigation_discarded_, false);
    if (before && !discarded && *before == scenarioFilesFingerprint()) return;
    if (summary_ && summary_->isChanged()) summary_->resetChanged();
    reloadCurrentScenario();
}

bool MainWindow::qSave(QSaveType type) {
    if (in_saving_) return false;
    // ホームへ戻る操作では、破棄の確認を済ませている。ホストがモー
    // ドレスのエディタを閉じる際も、 通常の終了コールバックはその
    // 判断を再利用し、ホームの上に二度目のモーダル確認を開かない。
    if (type == QSaveType::close && home_return_confirmed_) return true;
    const bool closing = type == QSaveType::close || type == QSaveType::home;
    std::size_t unapplied_dialogs{};
    for (auto* dialog : editdialogsession::EditDialogSession::liveSessions()) {
        const auto window = dialog ? dialog->editDialogWindow() : nullptr;
        if (!dialog || !window || !window->visible()) continue;
        if (!closing &&
            dynamic_cast<settingsdialog::SettingsDialog*>(dialog))
            continue;
        if (dialog->editDialogPending()) ++unapplied_dialogs;
    }

    if (unapplied_dialogs != 0U) {
        if (!services_.dialogs) return false;
        lib::MessageDialogRequest request;
        request.title = prop_ ? prop_->requiredMsgs().dlgTitQuestion.value
                              : "確認 - CWXEditor_C";
        const auto format = prop_
            ? (closing
                   ? prop_->requiredMsgs().dlgMsgForceCancelDialogsQuit.value
                   : prop_->requiredMsgs().dlgMsgForceCancelDialogs.value)
            : StringView{"%1$s件のダイアログに未適用の変更があります。続行しますか？"};
        request.message = ::cwxeditor::cwx::utils::tryFormat(
            format, unapplied_dialogs);
        request.severity = lib::MessageSeverity::Question;
        request.buttons = lib::MessageButtons::YesNo;
        if (services_.dialogs->message(request) != lib::MessageResult::Yes)
            return false;
    }

    if (comm_) (void)comm_->isChanged();
    if (!summary_ || !summary_->isChanged()) {
        const bool accepted = type != QSaveType::close || !context_.confirmClose ||
                              context_.confirmClose();
        if (accepted && type == QSaveType::home) home_return_confirmed_ = true;
        return accepted;
    }
    if (type == QSaveType::home) {
        // ホームへ戻る操作を暗黙の保存として扱わない。 キャンセル時は
        // モデルを変更せず、ホストが実際に終了するときだけ破棄する。
        if (!services_.dialogs) return false;
        lib::MessageDialogRequest request;
        request.title = "ホームに戻る";
        request.message = "未保存の変更を破棄してホームに戻りますか？ シナリオは上書き保存しません。";
        request.severity = lib::MessageSeverity::Question;
        request.buttons = lib::MessageButtons::OKCancel;
        home_return_confirmed_ =
            services_.dialogs->message(request) == lib::MessageResult::OK;
        return home_return_confirmed_;
    }
    if (context_.confirmClose && type == QSaveType::close &&
        !context_.confirmClose())
        return false;
    if (services_.dialogs) {
        lib::MessageDialogRequest request;
        request.title = prop_ ? prop_->requiredMsgs().dlgTitQuestion.value
                              : "確認 - CWXEditor_C";
        const auto format = prop_
            ? (type == QSaveType::reload
                   ? prop_->requiredMsgs().dlgMsgIsSaveBeforeReload.value
                   : prop_->requiredMsgs().dlgMsgIsSaveBeforeExit.value)
            : StringView{"「%1$s」は変更されています。保存しますか？"};
        request.message = ::cwxeditor::cwx::utils::tryFormat(
            format, summary_->scenarioName());
        request.severity = lib::MessageSeverity::Question;
        request.buttons = type == QSaveType::reload
            ? lib::MessageButtons::OKCancel
            : lib::MessageButtons::YesNoCancel;
        const auto result = services_.dialogs->message(request);
        if (result == lib::MessageResult::Cancel || result == lib::MessageResult::None) return false;
        if (type == QSaveType::reload) return result == lib::MessageResult::OK;
        if (result == lib::MessageResult::Yes) return save(window_);
        return true;
    }
    return false;
}

void MainWindow::refShowMainToolBar() {
    const bool visible = prop_ && valueOf(prop_->var().showMainToolBar);
    for (const auto& bar : tool_bars_) if (bar) bar->set_visible(visible);
    if (toolbar_surface_)
        toolbar_surface_->set_visible(
            visible && !lib::editor_window_host_options().all_mode);
}

void MainWindow::SListener::shellClosed(bool& doit) {
    if (!owner_) return;
    doit = owner_->qSave(QSaveType::close);
    if (doit) {
        owner_->writeDock();
        owner_->saveDockState();
    }
}

void MainWindow::DListener::widgetDisposed() {
    if (!owner_) return;
    owner_->persistMainWindowState();
    owner_->quit_ = true;
    owner_->comm_->closeAll();
}

bool MainWindow::sendReloadPropsAndSave() {
    // D版と同様、更新した Properties 文書を書いてから再読み込みを通知する。 syncBindings/notifyChanged
    // が同期的にホストへ再入して MainWindow を破棄する場合
    // があるため、 適用をまたいで保持するのは寿命のトークンだけとする。
    const auto alive = lifecycle_alive_;
    if (!alive || !alive->load(std::memory_order_acquire) || !prop_ || !prop_->apply())
        return false;
    if (!alive->load(std::memory_order_acquire)) return false;
    if (!lib::editor_window_host_options().all_mode && !saveDockState()) return false;
    if (!alive->load(std::memory_order_acquire)) return false;
    sendReloadProps();
    if (!alive->load(std::memory_order_acquire)) return false;
    if (comm_) comm_->refSettingsWithSkinTypes.call();
    return alive->load(std::memory_order_acquire);
}

void MainWindow::sendReloadProps() {
    if (!services_.ipc || pipe_name_.empty()) return;
    // D版と同様、他のエディタの接続先を列挙し、自身のパイプを除外する。 共通の
    // IPCサービスで振り分け、Windows/macOS/Linuxで同じプ
    // ロトコルを使う。 自身の成功応答にUIが反応して繰り返さないようにする。
    const bool all_mode = lib::editor_window_host_options().all_mode;
    if (all_mode && ipc_broadcast_task_ && !ipc_broadcast_task_->finished()) return;
    const auto ipc = services_.ipc;
    const auto endpoint = pipe_name_;
    const auto notify = [ipc, endpoint] {
        std::string error;
        (void)ipc->broadcast(endpoint, "reload settings",
                             std::chrono::milliseconds(250), error);
    };
    if (services_.tasks && !all_mode) {
        auto queue = std::dynamic_pointer_cast<SettingsReloadQueue>(ipc_broadcast_task_);
        if (!queue) {
            queue = std::make_shared<SettingsReloadQueue>();
            ipc_broadcast_task_ = queue; // その場での実行や再入が起こる前に反映する。
        }
        queue->enqueue(services_.tasks, notify);
    } else if (services_.tasks) ipc_broadcast_task_ = services_.tasks->run(notify);
    else notify();
}

void MainWindow::reloadProps() {
    if (!prop_) return;
    // D版と同様、コントロールへ通知する前に設定ファイルを読
    // み直す。 メモリ内の既存値を通知するだけでは、エディタ
    // 間の再読み込みが成功したように見えても何も変わらない。
    settingsdialog::OldSettings old(*prop_, prop_->flex().menu);
    if (!prop_->reload()) {
        // D版の XMLFuncs は項目ごとの変換エラーを捕捉し、
        // ホイールのフィルターは復旧できた設定を直接読む。 C++
        // 版の診断は残すが、別の項目が失敗しても、単独版の NumberEdit
        // の方針には 同じ現在の設定値を反映する。
        if (!lib::editor_window_host_options().all_mode)
            lib::set_number_edit_wheel_enabled(prop_->var().spinnerUpDownWithWheel.value);
        setStatusLine("エディタ設定を再読み込みできませんでした。");
        return;
    }
    lib::set_number_edit_wheel_enabled(
        prop_->var().spinnerUpDownWithWheel.value);
    lib::set_tab_wheel_switch_enabled(prop_->var().switchTabWheel.value);
    lib::set_editor_ime_mode(prop_->var().imeMode.value);
    lib::set_combo_edit_popup_enabled(prop_->var().comboListVisible.value);
    old.raiseEvent(settingsCallbacks());
    if (comm_) comm_->saved.call();
    refreshExecEngine();
    refreshOuterTools();
    refreshTitle();
}

void MainWindow::settings() {
    if (context_.settings) {
        context_.settings();
        return;
    }
    openSettings(false);
}

settingsdialog::Callbacks MainWindow::settingsCallbacks() {
    settingsdialog::Callbacks callbacks;
    callbacks.editorCommons = comm_;
    if (comm_) {
        callbacks.refImageScale = [this] { comm_->refImageScale.call(); };
        callbacks.refTargetVersion = [this] { comm_->refTargetVersion.call(); };
        callbacks.refWallpaper = [this] {
            if (comm_ && prop_) comm_->refreshWallpaper(*prop_);
        };
        callbacks.refOuterTools = [this] { comm_->refOuterTools.call(); };
        callbacks.refEventTemplates = [this] { comm_->refEventTemplates.call(); };
        callbacks.refIgnorePaths = [this] { comm_->refIgnorePaths.call(); };
        callbacks.refCardState = [this] { comm_->refCardState.call(); };
        callbacks.refSwitchFixedWithCheckBox = [this] {
            comm_->refSwitchFixedWithCheckBox.call();
        };
        callbacks.refImagePaneSelectionFilter = [this] {
            comm_->refImagePaneSelectionFilter.call();
        };
        callbacks.refSortCondition = [this] { comm_->refSortCondition.call(); };
        callbacks.refSkin = [this] { comm_->refSkin.call(); };
        callbacks.refClassicSkin = [this] { comm_->refClassicSkin.call(); };
        callbacks.refHistories = [this] { comm_->refHistories.call(); };
        callbacks.refImportHistory = [this] { comm_->refImportHistory.call(); };
        callbacks.refHistoryMax = [this] { comm_->refHistoryMax.call(); };
        callbacks.refSearchHistories = [this] { comm_->refSearchHistories.call(); };
        callbacks.refUndoMax = [this] { comm_->refUndoMax.call(); };
        callbacks.refContentText = [this] { comm_->refContentText.call(); };
        callbacks.refMenu = [this](const MenuID id) { comm_->refMenu.call(id); };
        // D版と同様、既存のツールバーのラベルを更新する。 ここで再構築すると、Commons
        // が登録した EventView の操作可否判定が無効になる。
        callbacks.updateToolBarText = [this, alive=std::weak_ptr<std::atomic_bool>(lifecycle_alive_)] {
            const auto life = alive.lock();
            if (life && life->load(std::memory_order_acquire) && comm_)
                comm_->updateToolBarText();
        };
        callbacks.refFloatMessagePreview = [this] {
            comm_->refFloatMessagePreview.call();
        };
        callbacks.refUseMessageWindowColorInTextContentDialog = [this] {
            comm_->refUseMessageWindowColorInTextContentDialog.call();
        };
        callbacks.refSoundType = [this] { comm_->refSoundType.call(); };
        callbacks.refCardImageStatus = [this] { comm_->refCardImageStatus.call(); };
        callbacks.refEventTreeStyle = [this] { comm_->refEventTreeStyle.call(); };
        callbacks.refRadarStyle = [this] { comm_->refRadarStyle.call(); };
        callbacks.refShowAptitudes = [this] { comm_->refShowAptitudes.call(); };
        callbacks.refCastCardParameterEditStyle = [this] {
            comm_->refCastCardParameterEditStyle.call();
        };
        callbacks.refCoupons = [this] { comm_->refCoupons.call(); };
        callbacks.refGossips = [this] { comm_->refGossips.call(); };
        callbacks.refCompleteStamps = [this] { comm_->refCompleteStamps.call(); };
        callbacks.refKeyCodes = [this] { comm_->refKeyCodes.call(); };
        callbacks.refCellNames = [this] { comm_->refCellNames.call(); };
        callbacks.refPreviewValues = [this] { comm_->refPreviewValues.call(); };
        callbacks.refShowCardListHeader = [this] { comm_->refShowCardListHeader.call(); };
        callbacks.refShowCardListTitle = [this] { comm_->refShowCardListTitle.call(); };
        callbacks.refVarSelectStyle = [this] { comm_->refVarSelectStyle.call(); };
        callbacks.refEventTreeViewStyle = [this] { comm_->refEventTreeViewStyle.call(); };
        callbacks.refEventEditorStyle = [this] { comm_->refEventEditorStyle.call(); };
        callbacks.refAreaTable = [this] { comm_->refAreaTable.call(); };
        callbacks.refTableViewStyle = [this] { comm_->refTableViewStyle.call(); };
        callbacks.refContentsToolBoxStyle = [this] {
            comm_->refContentsToolBoxStyle.call();
        };
        callbacks.refUpdateMotionBarStyle = [this] {
            comm_->refUpdateMotionBarStyle.call();
        };
        callbacks.refMenuCardAndBgImageList = [this] {
            comm_->refMenuCardAndBgImageList.call();
        };
        callbacks.refExecutedParties = [this] { comm_->refExecutedParties.call(); };
        callbacks.refPartyHistoryMax = [this] { comm_->refPartyHistoryMax.call(); };
        callbacks.refBgImageSettings = [this] { comm_->refBgImageSettings.call(); };
        callbacks.refStandardSelections = [this] { comm_->refStandardSelections.call(); };
        callbacks.refStandardKeyCodes = [this] { comm_->refStandardKeyCodes.call(); };
        callbacks.refKeyCodesByFeatures = [this] { comm_->refKeyCodesByFeatures.call(); };
        callbacks.refSettingsWithSkinTypes = [this] {
            comm_->refSettingsWithSkinTypes.call();
        };
        callbacks.refElementOverrides = [this] { comm_->refElementOverrides.call(); };
        callbacks.updateCloseButtons = [this] {
            if (docking_folder_) docking_folder_->updateCloseButtons();
        };
        callbacks.updateMainToolBar = [this, weak=std::weak_ptr<std::atomic_bool>(lifecycle_alive_)] {
            const auto alive = weak.lock();
            if (!alive || !alive->load(std::memory_order_acquire)) return;
            (void)updateMainToolBar(false);
            if (!alive->load(std::memory_order_acquire)) return;
            // D版と同様、useCoolBar が変わったらツールバーのホストを再作成
            // する。 コマンドの再構築だけでは、無効になった空のバーがルートに残る。
            if (!lib::editor_window_host_options().all_mode) composeRoot();
        };
        callbacks.markSummaryChanged = [this](const settingsdialog::SummaryDirtyKind kind) {
            if (!summary_) return;
            bool changed{};
            bool all{};
            switch (kind) {
            case settingsdialog::SummaryDirtyKind::XmlFileName:
                if (!summary_->legacy()) {
                    changed = true;
                    all = summary_->expandXMLs();
                }
                break;
            case settingsdialog::SummaryDirtyKind::SaveSkinName:
                changed = true;
                break;
            case settingsdialog::SummaryDirtyKind::LegacyInnerImagePath:
                if (summary_->legacy()) {
                    changed = true;
                    all = true;
                }
                break;
            case settingsdialog::SummaryDirtyKind::LegacyResourceExtension:
                if (summary_->legacy() && skin_ && !skin_->legacy() &&
                    !skin_->sourceOfMaterialsIsClassicEngine()) {
                    changed = true;
                    all = true;
                }
                break;
            }
            if (!changed) return;
            if (all) summary_->changedAll(); else summary_->changed();
            is_changed_ = summary_->isChanged();
            refreshTitle();
            comm_->refScenario.call(summary_);
            comm_->refreshToolBar();
        };
        callbacks.saved = [this] { comm_->saved.call(); };
        callbacks.menuImage = [this](const MenuID id) { return menuImage(id); };
        callbacks.contentImage = [this](const types::CType type) {
            return images_ ? images_->content(type) : 0U;
        };
    }
    return callbacks;
}

void MainWindow::openSettings(const bool automation_hidden) {
    if (!prop_) return;
    if (settings_dialog_ && settings_dialog_->dialog() &&
        settings_dialog_->dialog()->visible()) {
        settings_dialog_->active();
        return;
    }
    const auto settings_started = std::chrono::steady_clock::now();
    lib::editor_action_log("Settings dialog begin phase=callbacks");

    auto callbacks = settingsCallbacks();
    settings_dialog_ = std::make_shared<settingsdialog::SettingsDialog>(
        *prop_, window_, std::shared_ptr<properties::DockingFolderCTC>{},
        summary_.get(), [this] {
            // SettingsDialog は設定変更のコールバックを通知済み。
            // それらを二重に通知せず、IPCの前に現在のドッキング状態を保存する。
            if (lib::editor_window_host_options().all_mode || saveDockState())
                sendReloadProps();
        },
        std::move(callbacks), services_);
    lib::editor_action_log(
        "Settings dialog phase=constructed elapsed_us=" + std::to_string(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - settings_started).count()));
    if (const auto dialog = settings_dialog_->dialog()) {
        dialog->initially_hidden = automation_hidden;
        dialog->activate_on_show = !automation_hidden;
    }
    (void)settings_dialog_->open();
    lib::editor_action_log(
        "Settings dialog end phase=opened elapsed_us=" + std::to_string(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - settings_started).count()));
}

bool MainWindow::canEditScenarioHistory() const { return true; }
void MainWindow::editScenarioHistory() {
    if (!prop_ || !images_) return;
    if (scenario_history_dialog_ && scenario_history_dialog_->dialog() &&
        scenario_history_dialog_->dialog()->visible()) {
        activateHistoryWindow(services_.gui, scenario_history_dialog_->dialog());
        return;
    }
    scenario_history_dialog_.reset();

    auto settings = history::historySettings(prop_->var());
    settings.context_menu_items = history::historyContextMenuItems(
        [this](const MenuID id) { return prop_->buildMenu(id); },
        [this](const MenuID id) { return menuImage(id); });
    // 共通の設定生成処理は値のスナップショットを使う。 現在値を取得する処理は
    // この所有ホストだけが登録し、Props を参照する前に有効性を確認する。
    settings.xmlCopyProvider = [this,
        weak = std::weak_ptr<std::atomic_bool>(lifecycle_alive_),
        fallback = settings.xmlCopy] {
        const auto alive = weak.lock();
        if (!alive || !alive->load(std::memory_order_acquire) || !prop_) return fallback;
        return prop_->var().xmlCopy.value;
    };
    const auto width = prop_->flex().scenarioHistoryDlg.width();
    const auto height = prop_->flex().scenarioHistoryDlg.height();
    settings.dialog_size = {
        width > 0 ? static_cast<float>(width) : 500.0F,
        height > 0 ? static_cast<float>(height) : 350.0F};
    settings.scenario_bookmark_hint = prop_->requiredMsgs().scenarioBookmarkHint.value;
    settings.history_scenario_file_name = prop_->requiredMsgs().historyScenarioFileName.value;
    settings.history_scenario_path = prop_->requiredMsgs().historyScenarioPath.value;
    settings.warning_history_too_many = prop_->requiredMsgs().warningHistoryTooMany.value;
    settings.ok_text = prop_->requiredMsgs().dlgTextOK.value;
    settings.cancel_text = prop_->requiredMsgs().dlgTextCancel.value;
    settings.apply_text = prop_->requiredMsgs().dlgTextApply.value;
    settings.images = {
        images_->warning(), images_->summaryFile(), images_->scenarioArchive(),
        images_->classic(), images_->unknown()};

    history::HistoryCallbacks callbacks;
    callbacks.toolbar_refresh = [this] {
        if (comm_) comm_->refreshToolBar();
    };
    const auto owner_lifetime = std::weak_ptr<std::atomic_bool>(lifecycle_alive_);
    callbacks.persisted = [this, owner_lifetime](const std::vector<OpenHistory>& histories,
                                                const std::vector<OpenHistory>& bookmarks) {
        const auto alive = owner_lifetime.lock();
        if (!alive || !alive->load(std::memory_order_acquire) || !prop_) return;
        if (equalOpenHistories(valueOf(prop_->var().openHistories), histories) &&
            equalOpenHistories(valueOf(prop_->var().scenarioBookmarks), bookmarks))
            return;
        prop_->var().scenarioBookmarks = bookmarks;
        prop_->var().openHistories = histories;
        if (!sendReloadPropsAndSave() || !alive->load(std::memory_order_acquire)) return;
        if (comm_) comm_->refHistories.call();
    };
    scenario_history_dialog_ = std::make_shared<history::ScenarioHistoryDialog>(
        services_, window_, prop_->requiredMsgs().editScenarioHistory.value,
        menuImage(MenuID::EditScenarioHistory),
        valueOf(prop_->var().openHistories),
        valueOf(prop_->var().scenarioBookmarks), std::move(settings),
        std::move(callbacks));
    const auto history_dialog = scenario_history_dialog_;
    const auto life = std::weak_ptr<std::atomic_bool>(lifecycle_alive_);
    history_dialog->show();
    const auto alive = life.lock();
    if (lib::editor_window_host_options().all_mode || !alive ||
        !alive->load(std::memory_order_acquire) || scenario_history_dialog_ != history_dialog) return;
    const auto window = history_dialog->dialog();
    if (!window) return;
    const auto prior = window->on_bounds_changed;
    window->on_bounds_changed = [prior, life, properties = prop_,
            model = std::weak_ptr<history::ScenarioHistoryDialog>(history_dialog),
            weak_window = std::weak_ptr<lib::Window>(window)](lib::Rect bounds) {
        if (prior) prior(bounds);
        const auto alive = life.lock();
        const auto current = weak_window.lock();
        if (!alive || !alive->load(std::memory_order_acquire) || model.expired() ||
            !properties || !current || current->maximized || current->minimized) return;
        const double scale = std::max(1.0F, current->bounds_ui_text_scale);
        // History::setup は、保存されたSWTのウィンドウ寸法をクライ
        // アント寸法に変換する。 通常時の境界を保存するときは、その逆変換を行う。
        const double width = current->bounds().width / scale + 16.0;
        const double height = current->bounds().height / scale + 39.0;
        if (!std::isfinite(width) || !std::isfinite(height) || width <= 0 || height <= 0 ||
            width > std::numeric_limits<int>::max() || height > std::numeric_limits<int>::max()) return;
        properties->flex().scenarioHistoryDlg.width(static_cast<int>(std::lround(width)));
        properties->flex().scenarioHistoryDlg.height(static_cast<int>(std::lround(height)));
    };
}
bool MainWindow::canEditImportHistory() const { return true; }
void MainWindow::editImportHistory() {
    if (!prop_ || !images_) return;
    if (import_history_dialog_ && import_history_dialog_->dialog() &&
        import_history_dialog_->dialog()->visible()) {
        activateHistoryWindow(services_.gui, import_history_dialog_->dialog());
        return;
    }
    import_history_dialog_.reset();

    auto settings = history::historySettings(prop_->var());
    settings.context_menu_items = history::historyContextMenuItems(
        [this](const MenuID id) { return prop_->buildMenu(id); },
        [this](const MenuID id) { return menuImage(id); });
    // 共通の設定生成処理は値のスナップショットを使う。 現在値を取得する処理は
    // この所有ホストだけが登録し、Props を参照する前に有効性を確認する。
    settings.xmlCopyProvider = [this,
        weak = std::weak_ptr<std::atomic_bool>(lifecycle_alive_),
        fallback = settings.xmlCopy] {
        const auto alive = weak.lock();
        if (!alive || !alive->load(std::memory_order_acquire) || !prop_) return fallback;
        return prop_->var().xmlCopy.value;
    };
    const auto width = prop_->flex().scenarioHistoryDlg.width();
    const auto height = prop_->flex().scenarioHistoryDlg.height();
    settings.dialog_size = {
        width > 0 ? static_cast<float>(width) : 500.0F,
        height > 0 ? static_cast<float>(height) : 350.0F};
    settings.scenario_bookmark_hint = prop_->requiredMsgs().scenarioBookmarkHint.value;
    settings.history_scenario_file_name = prop_->requiredMsgs().historyScenarioFileName.value;
    settings.history_scenario_path = prop_->requiredMsgs().historyScenarioPath.value;
    settings.warning_history_too_many = prop_->requiredMsgs().warningHistoryTooMany.value;
    settings.ok_text = prop_->requiredMsgs().dlgTextOK.value;
    settings.cancel_text = prop_->requiredMsgs().dlgTextCancel.value;
    settings.apply_text = prop_->requiredMsgs().dlgTextApply.value;
    settings.images = {
        images_->warning(), images_->summaryFile(), images_->scenarioArchive(),
        images_->classic(), images_->unknown()};

    history::HistoryCallbacks callbacks;
    callbacks.toolbar_refresh = [this] {
        if (comm_) comm_->refreshToolBar();
    };
    const auto owner_lifetime = std::weak_ptr<std::atomic_bool>(lifecycle_alive_);
    callbacks.persisted = [this, owner_lifetime](const std::vector<OpenHistory>& histories,
                                                  const std::vector<OpenHistory>& bookmarks) {
        const auto alive = owner_lifetime.lock();
        if (!alive || !alive->load(std::memory_order_acquire) || !prop_) return;
        if (equalOpenHistories(valueOf(prop_->var().importHistory), histories) &&
            equalOpenHistories(valueOf(prop_->var().importBookmarks), bookmarks))
            return;
        prop_->var().importBookmarks = bookmarks;
        prop_->var().importHistory = histories;
        if (!sendReloadPropsAndSave() || !alive->load(std::memory_order_acquire)) return;
        if (comm_) comm_->refImportHistory.call();
    };
    import_history_dialog_ = std::make_shared<history::ScenarioHistoryDialog>(
        services_, window_, prop_->requiredMsgs().editImportHistory.value,
        menuImage(MenuID::EditImportHistory),
        valueOf(prop_->var().importHistory),
        valueOf(prop_->var().importBookmarks), std::move(settings),
        std::move(callbacks));
    import_history_dialog_->show();
}

bool MainWindow::canDelNotExistsScenario() const {
    return prop_ && (!valueOf(prop_->var().scenarioBookmarks).empty() ||
                     !valueOf(prop_->var().openHistories).empty() ||
                     !valueOf(prop_->var().importBookmarks).empty() ||
                     !valueOf(prop_->var().importHistory).empty());
}

void MainWindow::delNotExistsScenario() {
    if (!prop_) return;
    const auto properties = prop_;
    auto* const communications = comm_;
    const auto alive = lifecycle_alive_;
    const auto still_current = [&] {
        return alive && alive->load(std::memory_order_acquire) &&
            prop_ == properties && comm_ == communications;
    };
    const auto filter = [this](const std::vector<OpenHistory>& source) {
        std::vector<OpenHistory> result;
        // 復元用の履歴は、シナリオパスに続けて復元する CWXPath
        // の一覧を保存する。 元の付加情報付きの項目は維持し、
        // 存在確認だけはD版と同様に復号したシナリオパスで行う。
        for (const auto& item : source)
            if (pathExists(history::fullHistToHist(item.path))) result.push_back(item);
        return result;
    };
    bool updated = false;
    const auto scenario_bookmarks = filter(valueOf(properties->var().scenarioBookmarks));
    const auto scenario_history = filter(valueOf(properties->var().openHistories));
    const bool scenario_changed =
        scenario_bookmarks.size() != valueOf(properties->var().scenarioBookmarks).size() ||
        scenario_history.size() != valueOf(properties->var().openHistories).size();
    if (scenario_changed) {
        properties->var().scenarioBookmarks = scenario_bookmarks;
        properties->var().openHistories = scenario_history;
        if (!still_current()) return;
        // D版と同様、この履歴群は取り込み処理と保存より前に通知する。
        if (communications) communications->refHistories.call();
        updated = true;
        if (!still_current()) return;
    }

    // D版の順次更新に合わせ、シナリオのコールバック後にこの履歴
    // 群を読む。 所有者の無効化やプロパティの再接続も考慮する。
    const auto import_bookmarks = filter(valueOf(properties->var().importBookmarks));
    const auto import_history = filter(valueOf(properties->var().importHistory));
    const bool import_changed =
        import_bookmarks.size() != valueOf(properties->var().importBookmarks).size() ||
        import_history.size() != valueOf(properties->var().importHistory).size();
    if (import_changed) {
        properties->var().importBookmarks = import_bookmarks;
        properties->var().importHistory = import_history;
        if (!still_current()) return;
        // D版の履歴群の順序を維持し、無効化・再接続されたホストでは処理しない。
        if (communications) communications->refImportHistory.call();
        updated = true;
        if (!still_current()) return;
    }
    if (!updated || !still_current()) return;
    // D版と同様、変更された各履歴群が購読先を更新してから永続化する。
    (void)sendReloadPropsAndSave();
}

bool MainWindow::canEditExecutedPartyHistory() const {
    return prop_ && (!valueOf(prop_->var().executedPartyBookmarks).empty() ||
                     !valueOf(prop_->var().executedParties).empty() ||
                     !valueOf(prop_->var().classicEngines).empty());
}
void MainWindow::editExecutedPartyHistory() {
    if (!prop_ || !images_) return;
    if (executed_party_history_dialog_ &&
        executed_party_history_dialog_->dialog() &&
        executed_party_history_dialog_->dialog()->visible()) {
        activateHistoryWindow(services_.gui, executed_party_history_dialog_->dialog());
        return;
    }
    executed_party_history_dialog_.reset();

    auto settings = partyhistory_ns::partyHistorySettings(prop_->var(), &prop_->sys());
    settings.xmlCopyProvider = [properties = prop_,
        alive = std::weak_ptr<std::atomic_bool>(lifecycle_alive_), fallback = settings.xmlCopy] {
        const auto gate = alive.lock();
        return gate && gate->load(std::memory_order_acquire)
            ? properties->var().xmlCopy.value : fallback;
    };
    const auto width = prop_->flex().executedPartyHistoryDlg.width();
    const auto height = prop_->flex().executedPartyHistoryDlg.height();
    settings.dialog_size = {
        width > 0 ? static_cast<float>(width) : 500.0F,
        height > 0 ? static_cast<float>(height) : 350.0F};
    settings.bookmark_hint = prop_->requiredMsgs().executionPartyBookmarkHint.value;
    settings.party_name = prop_->requiredMsgs().executionPartyName.value;
    settings.yado_name_label = prop_->requiredMsgs().executionPartyYado.value;
    settings.engine_name_label = prop_->requiredMsgs().executionPartyEngine.value;
    settings.warning_history_too_many = prop_->requiredMsgs().warningHistoryTooMany.value;
    settings.ok_text = prop_->requiredMsgs().dlgTextOK.value;
    settings.cancel_text = prop_->requiredMsgs().dlgTextCancel.value;
    settings.apply_text = prop_->requiredMsgs().dlgTextApply.value;
    settings.app_path = prop_->parent().appPath();
    settings.read_classic_party_name = [properties = prop_](StringView path) {
        return ::cwxeditor::cwx::cwl::readPartyName(properties->parent(), path);
    };
    settings.images = {
        images_->menu(MenuID::ExecEngine), images_->warning(), images_->team(),
        images_->yado(), images_->debugYado(), images_->classicEngine()};

    partyhistory_ns::PartyHistoryCallbacks callbacks;
    callbacks.toolbar_refresh = [this] {
        if (comm_) comm_->refreshToolBar();
    };
    callbacks.classic_skin_changed = [this] {
        if (comm_) comm_->refClassicSkin.call();
    };
    callbacks.persisted = [this](const std::vector<ExecutionParty>& histories,
                                 const std::vector<ExecutionParty>& bookmarks) {
        if (!prop_) return;
        prop_->var().executedPartyBookmarks = bookmarks;
        prop_->var().executedParties = histories;
        if (comm_) comm_->refExecutedParties.call();
    };
    // 既存の追加機能: 履歴ダイアログから選択したパーティでエンジンを起動できる。
    // D版の履歴編集に加え、C++版ではダブルクリックからもこの起動処理へ進む。
    callbacks.execute_engine = [this](const StringView scenario_path,
                                      const ExecutionParty& party) {
        if (party.enginePath.empty()) return;
        execEngineP(party.enginePath, String(scenario_path), party);
    };

    executed_party_history_dialog_ =
        std::make_shared<partyhistory_ns::ExecutedPartyHistoryDialog>(
            services_, window_, prop_->requiredMsgs().editExecutedPartyHistory.value,
            menuImage(MenuID::EditExecutedPartyHistory),
            valueOf(prop_->var().executedParties),
            valueOf(prop_->var().executedPartyBookmarks), std::move(settings),
            std::move(callbacks));
    const auto party_history_dialog = executed_party_history_dialog_;
    const auto life = std::weak_ptr<std::atomic_bool>(lifecycle_alive_);
    party_history_dialog->show();
    const auto alive = life.lock();
    if (lib::editor_window_host_options().all_mode || !alive ||
        !alive->load(std::memory_order_acquire) ||
        executed_party_history_dialog_ != party_history_dialog) return;
    const auto window = party_history_dialog->dialog();
    if (!window) return;
    const auto prior = window->on_bounds_changed;
    window->on_bounds_changed = [prior, life, properties = prop_,
            model = std::weak_ptr<partyhistory_ns::ExecutedPartyHistoryDialog>(
                party_history_dialog),
            weak_window = std::weak_ptr<lib::Window>(window)](lib::Rect bounds) {
        if (prior) prior(bounds);
        const auto alive = life.lock();
        const auto current = weak_window.lock();
        if (!alive || !alive->load(std::memory_order_acquire) || model.expired() ||
            !properties || !current || current->maximized || current->minimized) return;
        const double scale = std::max(1.0F, current->bounds_ui_text_scale);
        // ExecutedPartyHistoryDialog::setup は、ScenarioHistoryDialog::setup
        // と同じ標準枠を差し引いてから、 Window に境界を渡す。
        const double width = bounds.width / scale + 16.0;
        const double height = bounds.height / scale + 39.0;
        if (!std::isfinite(width) || !std::isfinite(height) || width <= 0 || height <= 0 ||
            width > std::numeric_limits<int>::max() || height > std::numeric_limits<int>::max()) return;
        properties->flex().executedPartyHistoryDlg.width(
            static_cast<int>(std::lround(width)));
        properties->flex().executedPartyHistoryDlg.height(
            static_cast<int>(std::lround(height)));
    };
}

void MainWindow::setHistSkin() {
    if (!prop_ || !summary_) return;
    setHistSkin(prop_->var().scenarioBookmarks.value);
    setHistSkin(prop_->var().openHistories.value);
}

void MainWindow::setHistSkin(std::vector<OpenHistory>& history) {
    if (!summary_) return;
    const auto scenario_history = history::createHistString(summary_.get());
    for (auto& item : history) {
        if (sameHistoryPath(item.path, scenario_history, services_.filesystem.get())) {
            item.skinType = summary_->type();
            item.skinName = summary_->skinName();
            item.skinEngine = skin_ ? String(skin_->legacyEngine()) : String{};
        }
    }
}

String MainWindow::findFullHist(StringView hist) const {
    if (!prop_) return {};
    for (const auto& item : valueOf(prop_->var().scenarioBookmarks))
        if (sameHistoryPath(item.path, hist, services_.filesystem.get()) || item.path == hist) return item.path;
    for (const auto& item : valueOf(prop_->var().openHistories))
        if (sameHistoryPath(item.path, hist, services_.filesystem.get()) || item.path == hist) return item.path;
    return {};
}

String MainWindow::createFullHistString() const {
    if (!summary_) return {};
    // D版と同様、シナリオを含むディレクトリではなく、Summary.xml/Summary.wsm
    // またはアーカイブのパスを保存する。 再起動時の findHistory()
    // も同じ表記を使うため、両者を混ぜると、 正しく保存し
    // た SceneView の履歴でも見つけられなくなる。
    const String scenarioPath = history::createHistString(summary_.get());
    if (scenarioPath.empty()) return {};
    if (!prop_ || !valueOf(prop_->var().reconstruction)) return scenarioPath;

    const auto paths = openedCWXPath();
    String result = "\"" + scenarioPath + "\" ";
    for (std::size_t index = 0; index < paths.size(); ++index) {
        if (index != 0U) result += path::CWXPATH_SEP;
        result += paths[index];
    }
    return result;
}

void MainWindow::writeDock() {
    if (!prop_) return;
    writeDock(prop_->var().scenarioBookmarks.value);
    writeDock(prop_->var().openHistories.value);
}

void MainWindow::writeDock(std::vector<OpenHistory>& history) {
    if (!summary_) return;
    const auto scenario_history = history::createHistString(summary_.get());
    if (scenario_history.empty()) return;
    const auto path = createFullHistString();
    for (auto& item : history)
        if (sameHistoryPath(item.path, scenario_history, services_.filesystem.get())) item.path = path;
}

void MainWindow::delHist(StringView fullHist) {
    if (!prop_) return;
    // 呼び出し元がどちらかの履歴ベクター内の文字列を参照している場合がある。 erase/remove
    // がその文字列を移動・破棄する前に、キーを保持する。
    const String stable_key(fullHist);
    const auto alive = lifecycle_alive_;
    auto& bookmarks = prop_->var().scenarioBookmarks.value;
    auto& histories = prop_->var().openHistories.value;
    const auto old_bookmark_count = bookmarks.size();
    const auto old_history_count = histories.size();
    delHist(stable_key, bookmarks);
    delHist(stable_key, histories);
    if (bookmarks.size() == old_bookmark_count && histories.size() == old_history_count)
        return;
    if (!sendReloadPropsAndSave() || !alive ||
        !alive->load(std::memory_order_acquire)) return;
    if (comm_) comm_->refHistories.call();
}

void MainWindow::delHist(StringView fullHist, std::vector<OpenHistory>& history) {
    history.erase(std::remove_if(history.begin(), history.end(),
                                 [this, fullHist](const auto& item) {
                                     return sameHistoryPath(item.path, fullHist,
                                                            services_.filesystem.get()) ||
                                            item.path == fullHist;
                                 }), history.end());
}

void MainWindow::updateSaveAsPickerDirectory() {
    if (!summary_ || !prop_ || lib::editor_window_host_options().all_mode ||
        lib::is_managed_user_data_session(services_) ||
        !prop_->var().enginePath.value.empty()) return;
    if (!summary_->readOnlyPath().empty()) {
        // D版の addHistory と同様、別名保存の開始フォルダーには、
        // 変換後の一時作業領域ではなく、元の読み取り専用の読み込み元を使う。
        const auto read_only = pathFromUtf8(
            ::cwxeditor::cwx::utils::nabs(StringView{summary_->readOnlyPath()}));
        prop_->var().scenarioPath = pathToUtf8(read_only.parent_path());
        return;
    }
    // D版の addHistory と同様、Summary.xml のあるディレクトリでは
    // なく、 シナリオを含むディレクトリを選ぶ。管理対象ルートでは、その方針を維持する。
    const auto saved = history::fullHistToHist(createFullHistString());
    if (saved.empty()) return;
    const auto target = pathFromUtf8(saved);
    prop_->var().scenarioPath = pathToUtf8(
        summary_->useTemp() ? target.parent_path() : target.parent_path().parent_path());
}

void MainWindow::addHistory() {
    if (!summary_ || !prop_) return;
    const auto path = createFullHistString();
    if (path.empty()) return;
    updateSaveAsPickerDirectory();
    auto& open_histories = prop_->var().openHistories.value;
    // D版の履歴の制約を一か所にまとめる。ブックマーク済みのシナリオを最
    // 近使った項目に重複させず、 既存の最近使った項目は先頭へ移す。上限
    // で切り詰める前に、設定された未存在ファイルの除去方針を適用する。
    history::addHistory(history::historySettings(prop_->var()),
                        OpenHistory{path, summary_->type(), summary_->skinName(), {}},
                        open_histories, prop_->var().scenarioBookmarks.value,
                        services_.filesystem.get());
    if (comm_ && !comm_->isChanged()) writeDock();
    prop_->var().lastScenario = history::fullHistToHist(path);
    // D版と同様、他のエディタへ再読み込みを通知する前に、履歴とドッキングを保
    // 存する。 実行パーティの保存と同じ、現在の配置を扱うアダプターを使う。
    std::shared_ptr<properties::DockingFolderCTC> dock;
    if (!pending_dock_state_.empty())
        dock = std::make_shared<properties::DockingFolderCTC>(
            ::cwxeditor::cwx::xml::XNode::parse(pending_dock_state_));
    else if (docking_folder_)
        dock = std::make_shared<properties::DockingFolderCTC>(
            docking_folder_->toNode({String{"work"}}));
    prop_->flex().save(dock);
    sendReloadProps();
    if (comm_) comm_->refHistories.call();
}

MainWindow::Hist::Hist(MainWindow* owner, Type type, int number, String hist)
    // D版の Hist.this と同様、保存済みのビュー接尾辞を復号してから、読み込みパスを保存する。
    : owner_(owner), type_(type), hist_(history::fullHistToHist(hist)), number_(number),
      owner_lifetime_(owner ? owner->lifecycle_alive_ : std::weak_ptr<std::atomic_bool>{}) {}

bool MainWindow::Hist::ownerAlive() const noexcept {
    const auto alive=owner_lifetime_.lock();
    return owner_ && alive && alive->load(std::memory_order_acquire);
}

void MainWindow::Hist::append(const MenuPtr& menu) {
    if (!menu) return;
    const auto self = shared_from_this();
    const lib::Action action = [self] { self->run(); };
    auto display = history::fullHistToHist(hist_);
    const auto name = lower(basename(display));
    if (name == "summary.xml" || name == "summary.wsm") display = dirname(display);
    const auto prefix = (number_ >= 0 && number_ < 10 ? String{"&"} : String{}) +
                        std::to_string(number_) + " ";
    const auto allowed = [self] {
        return self->ownerAlive() &&
            (self->type_ == Type::Open || static_cast<bool>(self->owner_->summary_));
    };
    // D版の Hist と同様、初期化時と MenuShown 時の両方で historyImage
    // を設定する。 履歴行を再構築しなくても、存在しないファイルに警告を表示できるようにする。
    const auto history_image = [self]() -> lib::ResourceId {
        if (!self->ownerAlive() || !self->owner_->images_) return {};
        auto* images = self->owner_->images_.get();
        return history::historyImage(
            {images->warning(), images->summaryFile(), images->scenarioArchive(),
             images->classic(), images->unknown()}, self->hist_,
            self->owner_->services_.filesystem.get());
    };
    const auto item = menu->append(
        prefix + cuthist(display, ownerAlive() && owner_->prop_
            ? valueOf(owner_->prop_->var().historySnipLength) : 30),
        history_image(), dmenu::MenuPush,
        dmenu::MenuData{MenuID::None, {}, allowed}, {}, action);
    if (item) {
        item.setEnabled(allowed());
        if (auto* entry = menu->entry(item.index()))
            entry->refresh = [item, history_image] {
                if (!item.valid()) return;
                const auto image = history_image();
                if (item.valid()) item.setImage(image);
            };
    }
    // dmenu は通常のネイティブ通知用の操作を Entry に保持
    // する。 動的な履歴行は、コンテキスト・ツール用バックエンドにも
    // lib::MenuItem として渡すため、 同じ所有権付きの操
    // 作をそちらにも設定する。 メニューの再構築をまたいでリスナーを
    // 保持し、どちらの表現からも表示中の履歴を実行できるようにする。
    if (item) item.native().action = action;
}

void MainWindow::Hist::run() {
    if (!ownerAlive() || (type_ == Type::Import && !owner_->summary_)) return;
    // ファイル・取り込みメニューの再構築後に、読み込みが完了する場
    // 合がある。 メニュー項目に取り込まれた古い生の this で
    // はなく、 このリスナーの状態を失敗時の継続処理まで保持する。
    const auto self = shared_from_this();
    if (type_ == Type::Open) {
        if (owner_->qSave(QSaveType::open) && ownerAlive()) {
            owner_->openScenario(hist_, [self] {
                self->delHist();
                if (self->ownerAlive()) self->owner_->resetOpt();
            });
        }
    } else {
        owner_->addScenario({hist_}, [self] { self->delHist(); });
    }
}

void MainWindow::Hist::delHist() {
    if (!ownerAlive() || !owner_->prop_ || !owner_->services_.dialogs) return;
    const String history_path = history::fullHistToHist(hist_);
    String display = history_path;
    const auto extension = lower(pathToUtf8(pathFromUtf8(display).extension()));
    if (extension == ".xml" || extension == ".wsm" || extension == ".wid" ||
        extension == ".wex") {
        display = dirname(display);
    }
    const bool bookmarked = std::any_of(
        owner_->prop_->var().scenarioBookmarks.value.begin(),
        owner_->prop_->var().scenarioBookmarks.value.end(),
        [this, &history_path](const OpenHistory& value) {
            return samePath(history::fullHistToHist(value.path), history_path,
                            owner_->services_.filesystem.get());
        });
    lib::MessageDialogRequest request;
    request.title = owner_->prop_->requiredMsgs().dlgTitQuestion.value;
    request.message = ::cwxeditor::cwx::utils::tryFormat(
        bookmarked ? owner_->prop_->requiredMsgs().scenarioNotFoundForBookmark.value
                   : owner_->prop_->requiredMsgs().scenarioNotFound.value,
        display);
    request.severity = lib::MessageSeverity::Question;
    request.buttons = lib::MessageButtons::YesNo;
    const auto dialogs = owner_->services_.dialogs;
    if (dialogs->message(request) == lib::MessageResult::Yes && ownerAlive())
        owner_->delHist(hist_);
}

String MainWindow::Hist::cuthist(String hist, int cut) {
    // D版のメニューラベルの規則を維持する。cut は先頭のディレクトリ
    // 部分とファイル名に対する Unicodeコードポイント数の上限で、
    // 省略記号は別に追加する。 バイト単位で切ると非ASCIIパスが壊れ
    // 、似た最近使った項目を区別するドライブ接頭辞も失われる。 単独版
    // はD版と同様、接頭辞の上限がゼロでもルートとファイル名を残す。
    // ALL の既存のゼロ上限時の表示と、不正な負値の扱いは維持する。
    if (cut < 0 || (cut == 0 && lib::editor_window_host_options().all_mode)) return hist;
    // D版と同様、ラベル計算前に nabs() を適用する。古い履歴から読
    // んだ相対パスも対象にするが、 保存されているパス自体は変更しない。
    hist = utils::nabs(StringView{hist});
    const auto length = lib::utf8_codepoint_count(hist);
    if (length <= static_cast<std::size_t>(cut) + 3U) return hist;

    const auto take_left = [](StringView value, const std::size_t count) {
        std::size_t index{};
        for (std::size_t i{}; i < count && index < value.size(); ++i)
            index = lib::next_utf8_boundary(value, index);
        return String(value.substr(0, index));
    };
    const auto take_right = [](StringView value, const std::size_t count) {
        std::size_t index = value.size();
        for (std::size_t i{}; i < count && index != 0U; ++i)
            index = lib::previous_utf8_boundary(value, index);
        return String(value.substr(index));
    };

    const auto path = pathFromUtf8(hist);
    const auto drive = pathToUtf8(path.root_name());
    const auto name = basename(hist);
    const auto root_length = drive.empty() ? 1U : lib::utf8_codepoint_count(drive) + 1U;
    const auto filename_length = lib::utf8_codepoint_count(name) + 1U;
    std::size_t prefix_length = filename_length <= static_cast<std::size_t>(cut)
        ? static_cast<std::size_t>(cut) - filename_length : 0U;
    prefix_length = std::max(prefix_length, root_length);
    if (length <= prefix_length + filename_length + 3U) return hist;
    return take_left(hist, prefix_length) + "..." + take_right(hist, filename_length);
}

bool MainWindow::Hist::unittest() const {
    return cuthist("abcdef", 5) == "abcdef" &&
           cuthist("C:\\Documents and Settings\\aaaaaaaaaaaa\\bbbbbbbbbb\\test.file", 30) ==
               "C:\\Documents and Set...\\test.file";
}

bool MainWindow::canSaveOverwrite() const {
    if (!summary_ || in_saving_) return false;
    // D版の canSaveOverwrite と同様、applyDialogsBeforeSave
    // が書き込み直前にダイアログを確定する場合は、 Summary
    // 自体が未変更でも、未適用のダイアログがあれば保存を有効にする。
    if (prop_ && valueOf(prop_->var().applyDialogsBeforeSave)) {
        for (auto* dialog : editdialogsession::EditDialogSession::liveSessions()) {
            const auto window = dialog ? dialog->editDialogWindow() : nullptr;
            if (dialog && !dialog->noScenario() && dialog->editDialogPending() &&
                window && window->visible()) return true;
        }
    }
    // 変更状態の判断は Summary を正とする。RefreshTitle
    // の is_changed_ は表示用のキャッシュにすぎず、 メ
    // ニュー・コマンドの有効化判定より後に更新される場合がある。 こ
    // こで参照すると、編集直後でも保存が無効のままになることがある。
    return !valueOf(prop_->var().saveNeedChanged) || summary_->isChanged();
}

void MainWindow::createFileMenu() {
    // dmenu が別の通知先一覧も保持するため、メニューを再作成する。
    if (menu_file_) menu_file_->retireBindings();
    menu_file_ = std::make_shared<dmenu::Menu>(true);
    if (menu_bar_) {
        for (std::size_t index = 0; index < menu_bar_->items.size(); ++index) {
            const auto* entry = menu_bar_->entry(index);
            if (entry && entry->data.id == MenuID::File) {
                menu_bar_->setSubmenu(dmenu::MenuItemRef(menu_bar_.get(), index), menu_file_);
                break;
            }
        }
    }
    const auto add = [this](MenuID id, std::function<bool()> enabled = {}) {
        const auto item = menu_file_->append(menuText(id), menuImage(id), dmenu::MenuPush,
            dmenu::MenuData{id, {}, [this, id, enabled] {
                return !enabled || enabled();
            }}, [this, id](SelectionEvent& event) { raiseEvent(id, event); });
        menu_items_[id] = item;
    };
    add(MenuID::New);
    add(MenuID::Open);
    add(MenuID::Save, [this] { return canSaveOverwrite(); });
    add(MenuID::SaveAs, [this] { return summary_ && !in_saving_; });
    menu_file_->appendSeparator();
    // ALL はホスト所有の埋め込みエディタ画面を一つだけ持つ。 単独版
    // のウィンドウ作成コマンドは表示するが、意図的に無効化する。 すべ
    // ての ALL ホストで共通とし、Web専用のメニューにはしない。
    add(MenuID::NewAtNewWindow, [this] { return !lib::editor_window_host_options().all_mode &&
                        services_.processes && !app_path_.empty(); });
    add(MenuID::OpenAtNewWindow, [this] { return !lib::editor_window_host_options().all_mode &&
                        services_.processes && !app_path_.empty(); });
    menu_file_->appendSeparator();
    add(MenuID::CreateArchive, [this] {
        return directory_window_ && directory_window_->canCreateArchive();
    });
    menu_file_->appendSeparator();
    add(MenuID::Reload, [this] { return summary_ != nullptr; });
    int historyNumber = 0;
    if (prop_ && !valueOf(prop_->var().scenarioBookmarks).empty()) {
        menu_file_->appendSeparator();
        for (const auto& item : valueOf(prop_->var().scenarioBookmarks))
            std::make_shared<Hist>(this, Hist::Type::Open, historyNumber++, item.path)->append(menu_file_);
    }
    if (prop_ && !valueOf(prop_->var().openHistories).empty()) {
        menu_file_->appendSeparator();
        for (const auto& item : valueOf(prop_->var().openHistories))
            std::make_shared<Hist>(this, Hist::Type::Open, historyNumber++, item.path)->append(menu_file_);
    }
    menu_file_->appendSeparator();
    add(MenuID::EditScenarioHistory);
    if (isAllMode()) {
        menu_items_.erase(MenuID::Close);
    } else {
        menu_file_->appendSeparator();
        add(MenuID::Close);
    }
}

void MainWindow::createImportHistoryMenu(const MenuPtr& menu, bool withSelectScenario) {
    if (!menu) return;
    menu->clearItems();
    if (withSelectScenario) {
        const auto select = menu->append(
            menuText(MenuID::SelectImportSource), menuImage(MenuID::SelectImportSource),
            dmenu::MenuPush,
            dmenu::MenuData{MenuID::SelectImportSource, {}, [this] { return canAddScenario(); }}, {},
            [this] { addScenario(); });
        menu_items_[MenuID::SelectImportSource] = select;
    }
    if (!prop_) return;
    int number = 0;
    const auto addSeparator = [&] {
        if (menu->size() != 0U) (void)menu->appendSeparator();
    };
    if (!valueOf(prop_->var().importBookmarks).empty()) addSeparator();
    for (const auto& item : valueOf(prop_->var().importBookmarks)) {
        std::make_shared<Hist>(this, Hist::Type::Import, number++, item.path)->append(menu);
    }
    if (!valueOf(prop_->var().importHistory).empty()) addSeparator();
    for (const auto& item : valueOf(prop_->var().importHistory)) {
        std::make_shared<Hist>(this, Hist::Type::Import, number++, item.path)->append(menu);
    }
    addSeparator();
    const auto edit = menu->append(
        menuText(MenuID::EditImportHistory), menuImage(MenuID::EditImportHistory),
        dmenu::MenuPush,
        dmenu::MenuData{MenuID::EditImportHistory, {},
            [this] { return canEditImportHistory(); }}, {},
        [this] { editImportHistory(); });
    menu_items_[MenuID::EditImportHistory] = edit;
}

void MainWindow::pipeThr() {
    if (!services_.ipc || pipe_name_.empty() || ipc_server_) return;
    const auto alive = lifecycle_alive_;
    const auto accepting = std::make_shared<std::atomic_bool>(true);
    const auto dispatcher = services_.dispatcher;
    const auto owner_thread = std::this_thread::get_id();
    const auto enqueue = [alive, dispatcher, owner_thread](std::function<void()> action) {
        auto guarded = [alive, action = std::move(action)] {
            if (alive && alive->load(std::memory_order_acquire)) action();
        };
        try {
            if (dispatcher) { dispatcher->post(std::move(guarded)); return true; }
            if (std::this_thread::get_id() == owner_thread) { guarded(); return true; }
        } catch (const std::exception&) {
            // 通知処理の終了中は、新しいUI処理を拒否す
            // る。 代わりに受信スレッド上で実行しない。
        }
        return false;
    };
    std::string error;
    auto server = services_.ipc->listen(pipe_name_,
        [this, alive, accepting, dispatcher, owner_thread, enqueue](StringView message) -> String {
        if (!alive || !alive->load(std::memory_order_acquire) ||
            !accepting->load(std::memory_order_acquire)) return {};
        if (message == "quit") {
            // D版と同様、quit メッセージは受信側を閉じるものであり、エディタ自身は終了しない。
            accepting->store(false, std::memory_order_release);
            (void)enqueue([this] { if (ipc_server_) ipc_server_->stop(); });
            return {};
        }
        if (message == "get opened scenario") {
            const auto query = [this]() -> String {
                return summary_ ? String("opened scenario ") + origScenarioPath() : String{};
            };
            if ((dispatcher && dispatcher->is_main_thread()) ||
                (!dispatcher && std::this_thread::get_id() == owner_thread)) return query();
            const auto result = std::make_shared<std::promise<String>>();
            auto ready = result->get_future();
            if (!enqueue([result, query] {
                    try { result->set_value(query()); }
                    catch (...) { result->set_exception(std::current_exception()); }
                })) return {};
            // 無期限の invoke と異なり、UIがキューを処理しなくなっても
            // 、 終了処理とワーカーの終了待ちがデッドロックしないようにする。
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
            while (alive->load(std::memory_order_acquire) &&
                   std::chrono::steady_clock::now() < deadline) {
                if (ready.wait_for(std::chrono::milliseconds(10)) == std::future_status::ready) {
                    try { return ready.get(); } catch (...) { return {}; }
                }
            }
            return {};
        }
        if (message.rfind("open cwxpath ", 0) == 0) {
            if (!enqueue([this, path = String(message.substr(13))] { openCWXPath(path, true); })) return {};
            return "opened cwxpath";
        }
        if (message == "reload settings") { (void)enqueue([this] { reloadProps(); }); return {}; }
        if (message.rfind("select file ", 0) == 0) {
            if (!enqueue([this, path = String(message.substr(12))] {
                    if (!summary_) return;
                    if (context_.selectFile) context_.selectFile(path);
                    else {
                        openDirWindow();
                        if (directory_window_) (void)directory_window_->select(path, true);
                    }
                })) return {};
            return "selected file";
        }
        return {};
    }, error);
    if (server) ipc_server_ = std::move(server);
    else if (!error.empty()) utils::debugln("Editor IPC receiver: ", error);
}

String MainWindow::createPipeName() {
    if (services_.ipc) return services_.ipc->allocate_endpoint("cwxeditor");
    return app_path_.empty() ? String("cwxeditor") : app_path_ + ".ipc";
}

void MainWindow::sendToPipe(MessageHandler sendRecv, ContinueHandler next) {
    if (!sendRecv || !next) return;
    {
        std::lock_guard lock(send_mutex_);
        send_queue_.emplace_back(std::move(sendRecv), std::move(next));
    }
    sendPipeThr();
}

void MainWindow::sendPipeThr() {
    std::vector<std::pair<MessageHandler, ContinueHandler>> queue;
    {
        std::lock_guard lock(send_mutex_);
        queue.swap(send_queue_);
    }
    for (auto& item : queue) sendToPipeImpl(std::move(item.first), std::move(item.second));
}

void MainWindow::sendToPipeImpl(MessageHandler sendRecv, ContinueHandler next) {
    if (!services_.ipc || pipe_name_.empty()) return;
    String received;
    std::string error;
    while (next() && !send_pipe_quit_) {
        const auto message = sendRecv(received);
        if (message.empty()) break;
        received = services_.ipc->request(pipe_name_, message, std::chrono::milliseconds(250), error);
        if (!error.empty()) break;
    }
}

void MainWindow::refSkin() {
    if (prop_ && summary_ && !summary_->legacy() && comm_ && comm_->skin()) {
        areaviewutils::Commons common(*prop_, *comm_->skin());
        common.summary = summary_.get();
        castcarddialog::updateCoefficients(common, summary_->casts());
    }
    setHistSkin();
    refSoundType();
    refreshExecEngine();
}

void MainWindow::refScenario(SummaryPtr summ) {
    if (summ && summary_ != summ) {
        if (summary_dialog_) summary_dialog_->forceCancel();
        summary_dialog_.reset();
        summary_ = std::move(summ);
    }
    if (summary_) comm_->summary(summary_);
    if (area_table_) area_table_->summary(summary_.get());
    flag_comm_.useCounter = summary_ ? summary_->useCounter() : nullptr;
    if (flags_pane_)
        flags_pane_->setFlagDirTree(summary_ ? summary_->flagDirRoot() : nullptr,
                                    summary_ ? summary_->useCounter() : nullptr);
    if (cast_window_) cast_window_->refresh(summary_);
    if (skill_window_) skill_window_->refresh(summary_);
    if (item_window_) item_window_->refresh(summary_);
    if (beast_window_) beast_window_->refresh(summary_);
    if (info_window_) info_window_->refresh(summary_);
    if (directory_window_) directory_window_->refresh(summary_);
    if (coupon_window_) coupon_window_->summary(summary_);
    if (gossip_window_) gossip_window_->summary(summary_);
    if (complete_stamp_window_) complete_stamp_window_->summary(summary_);
    if (key_code_window_) key_code_window_->summary(summary_);
    if (cell_name_window_) cell_name_window_->summary(summary_);
    if (card_group_window_) card_group_window_->summary(summary_);
    refSoundType();
    refreshTitle();
    if (services_.gui && window_) services_.gui->request_redraw(window_);
}

namespace {
[[nodiscard]] String trimSoundFontSetting(StringView value) {
    // D版の std.string.strip と同様、Unicode の White_Space
    // を使う。 UTF-8の列全体で照合し、継続バイト単位の判定やロケール依存
    // の isspace は使わない。 途中の空白と不正なバイト列は保持する。コ
    // ードポイントの集合は core/cwx/msgutils.cpp と共通。
    static constexpr StringView whitespace[] = {
        "\x09", "\x0A", "\x0B", "\x0C", "\x0D", "\x20",
        "\xC2\x85", "\xC2\xA0", "\xE1\x9A\x80",
        "\xE2\x80\x80", "\xE2\x80\x81", "\xE2\x80\x82",
        "\xE2\x80\x83", "\xE2\x80\x84", "\xE2\x80\x85",
        "\xE2\x80\x86", "\xE2\x80\x87", "\xE2\x80\x88",
        "\xE2\x80\x89", "\xE2\x80\x8A", "\xE2\x80\xA8",
        "\xE2\x80\xA9", "\xE2\x80\xAF", "\xE2\x81\x9F",
        "\xE3\x80\x80"
    };
    for (;;) {
        const auto found = std::find_if(std::begin(whitespace), std::end(whitespace),
            [value](StringView space) { return value.starts_with(space); });
        if (found == std::end(whitespace)) break;
        value.remove_prefix(found->size());
    }
    for (;;) {
        const auto found = std::find_if(std::begin(whitespace), std::end(whitespace),
            [value](StringView space) { return value.ends_with(space); });
        if (found == std::end(whitespace)) break;
        value.remove_suffix(found->size());
    }
    return String(value);
}

// D版の csvReader!string と同様、フィールドを保持し、二重の引用符を
// 復号する。 後の不正なフィールドで走査が止まっても、それまでに完成した部分は残す。
lib::Array<structs::SoundFontWithVolume> parseLegacySoundFonts(StringView setting) {
    const String text = trimSoundFontSetting(setting);
    lib::Array<structs::SoundFontWithVolume> fonts;
    if (text.empty()) return fonts;
    std::size_t i = 0;
    for (;;) {
        String field;
        if (i < text.size() && text[i] == '"') {
            ++i;
            bool closed = false;
            while (i < text.size()) {
                if (text[i] != '"') { field += text[i++]; continue; }
                ++i;
                if (i < text.size() && text[i] == '"') { field += '"'; ++i; continue; }
                closed = true;
                break;
            }
            if (!closed) return fonts;
            if (i < text.size() && text[i] != ',' && text[i] != '\r' && text[i] != '\n') return fonts;
        } else {
            while (i < text.size() && text[i] != ',' && text[i] != '\r' && text[i] != '\n') {
                if (text[i] == '"') return fonts;
                field += text[i++];
            }
        }
        fonts.emplace_back(std::move(field), 100U);
        if (i == text.size()) break;
        const auto delimiter = text[i++];
        if (delimiter == '\r' && i < text.size() && text[i] == '\n') ++i;
        if (delimiter != ',' && i == text.size()) break;
    }
    return fonts;
}

} // namespace

void MainWindow::refSoundType() {
    namespace audio = ::cwxeditor::cwx::editor::gui::sound;
    audio::stopBGM();
    audio::stopSE();
    if (!summary_ || !skin_ || !prop_) {
        audio::disposeSound();
        return;
    }

    int bgmType = valueOf(prop_->var().soundPlayType);
    int seType = valueOf(prop_->var().soundEffectPlayType);
    if (seType == audio::SOUND_TYPE_SAME_BGM) seType = bgmType;
    int engineBgm = summary_->legacy() ? audio::SOUND_TYPE_MCI : audio::SOUND_TYPE_SDL;
    int engineSe = engineBgm;
    lib::Array<structs::SoundFontWithVolume> soundFonts;
    auto soundFontDirectory = pathFromUtf8(skin_->engine()).parent_path();

    if (bgmType == audio::SOUND_TYPE_AUTO || seType == audio::SOUND_TYPE_AUTO) {
        if (skin_->legacy()) {
            const auto settings = skin_->loadEngineSettings();
            const auto setting = [&settings](StringView primary, StringView fallback) {
                if (const auto found = settings.find(String(primary)); found != settings.end())
                    return lower(found->second);
                if (const auto found = settings.find(String(fallback)); found != settings.end())
                    return lower(found->second);
                return String{};
            };
            const auto bgmApi = setting("musicapi", "soundapibgm");
            const auto seApi = setting("soundapi", "soundapise");
            if (bgmApi == "winmm") engineBgm = audio::SOUND_TYPE_MCI;
            else if (bgmApi == "bass") engineBgm = audio::SOUND_TYPE_BASS;
            if (seApi == "winmm") engineSe = audio::SOUND_TYPE_MCI;
            else if (seApi == "bass") engineSe = audio::SOUND_TYPE_BASS;
            if (engineBgm == audio::SOUND_TYPE_BASS || engineSe == audio::SOUND_TYPE_BASS) {
                if (const auto found = settings.find("soundfont"); found != settings.end()) {
                    soundFonts = parseLegacySoundFonts(found->second);
                }
            }
        } else {
            soundFonts = skin_->loadSoundFonts(
                valueOf(prop_->var().cardWirthPyDefaultSoundFont));
            if (!soundFonts.empty()) {
                engineBgm = audio::SOUND_TYPE_BASS;
                engineSe = audio::SOUND_TYPE_BASS;
            }
        }
    }
    if (bgmType == audio::SOUND_TYPE_AUTO) bgmType = engineBgm;
    if (seType == audio::SOUND_TYPE_AUTO) seType = engineSe;

    lib::AudioConfiguration audioRequest;
    audioRequest.preference =
        bgmType == audio::SOUND_TYPE_BASS || seType == audio::SOUND_TYPE_BASS
            ? lib::AudioBackendPreference::Bass
            : lib::AudioBackendPreference::Automatic;
    audioRequest.allow_fallback = true;
    audioRequest.library_search_roots = {
        pathFromUtf8(prop_->enginePath()).parent_path(), soundFontDirectory};
    for (const auto& font : soundFonts)
        audioRequest.sound_fonts.push_back(
            {pathFromUtf8(font.path), font.volume});
    // D版のWindows用BASSキャッシュはDLLディレクトリをキーにす
    // るが、共通のC++サービスはサウンドフォントを含む要求全体を使う。 パ
    // スが同じでもサウンドフォントの変更が無視されないよう、要求を渡し直す。
    const auto configured = audio::configureAudio(audioRequest);
    if (bgmType == audio::SOUND_TYPE_BASS || seType == audio::SOUND_TYPE_BASS) {
        if (!lib::has_audio_capability(configured.capabilities,
                                       lib::AudioCapability::Bass)) {
            const int fallback = audio::soundType(configured.effective);
            if (bgmType == audio::SOUND_TYPE_BASS) bgmType = fallback;
            if (seType == audio::SOUND_TYPE_BASS) seType = fallback;
        }
    }
    audio::lastSoundType = bgmType;
}

bool MainWindow::createMainToolBar(bool saveOrder) {
    // mainToolBars の提供処理は、現在の主ツールバーの世代だけ
    // を返す。 再構築をまたいでも Commons のペイン・ダイアログの
    // 登録は維持し、 各所有者が終了時に自身のコントロールを無効化する。
    for (const auto& bar : tool_bars_) if (bar) bar->retireBindings();
    tool_bars_.clear();
    tool_bar_group_order_.clear();
    tool_items_.clear();
    if (!prop_) return false;

    if (!lib::editor_window_host_options().all_mode) {
        // D版の arrangeOrder と同様、重複したグループも含め、保存された各出
        // 現箇所を実体化する。 その後、次回以降の再構築用に標準のインデックスを記録する。
        auto& settings = prop_->var().mainToolBar.value;
        auto& saved_order = prop_->var().toolsOrder.value;
        decltype(settings.tools) arranged;
        std::vector<bool> present(settings.tools.size(), false);
        for (const auto index : saved_order) {
            if (index < 0 || static_cast<std::size_t>(index) >= settings.tools.size()) continue;
            arranged.push_back(settings.tools[static_cast<std::size_t>(index)]);
            present[static_cast<std::size_t>(index)] = true;
        }
        for (std::size_t index = 0; index < settings.tools.size(); ++index)
            if (!present[index]) arranged.push_back(settings.tools[index]);
        settings.tools = std::move(arranged);
        saved_order.clear();
        for (std::size_t index = 0; index < settings.tools.size(); ++index)
            saved_order.push_back(static_cast<std::int32_t>(index));
    }

    // 古いツールバー配置には、標準配置の区切りがまだ含まれてい
    // ない場合がある。 ユーザーの項目順と明示的に保存した全
    // 区切りを保ちつつ、D版の三つのグループ境界を復元する。
    const auto normalized = withRequiredMainToolBarSeparators(
        valueOf(prop_->var().mainToolBar));
    const auto& groups = normalized.tools;
    std::vector<std::size_t> order;
    const auto appendOrder = [&order, &groups](std::int32_t value) {
        if (value < 0 || static_cast<std::size_t>(value) >= groups.size()) return;
        const auto index = static_cast<std::size_t>(value);
        if (std::find(order.begin(), order.end(), index) == order.end())
            order.push_back(index);
    };
    for (const auto value : valueOf(prop_->var().toolsOrder)) appendOrder(value);
    for (std::size_t index = 0; index < groups.size(); ++index)
        appendOrder(static_cast<std::int32_t>(index));
    if (saveOrder) {
        prop_->var().toolsOrder.value.clear();
        for (const auto index : order)
            prop_->var().toolsOrder.value.push_back(static_cast<std::int32_t>(index));
    }

    for (const auto groupIndex : order) {
        const auto& group = groups[groupIndex];
        if (std::ranges::none_of(group, [](const structs::Tool& tool) {
                return !tool.separator && !hiddenToolBarCommand(tool.menu);
            }))
            continue;
        auto bar = std::make_shared<dmenu::ToolBar>();
        tool_bars_.push_back(bar);
        tool_bar_group_order_.push_back(groupIndex);
        for (const auto& tool : group) {
            if (tool.separator) {
                (void)bar->append({}, {}, {}, dmenu::MenuSeparator, {}, {});
                continue;
            }
            const auto id = tool.menu;
            // 外部ツールの単独版の全入口を一時的に非表示にす
            // る。 保存済みのツールバー設定は削除しない。
            if (hiddenToolBarCommand(id)) continue;
            // 四つのドロップダウンツールはメニューを所有し、追加
            // の有効化動作も持つため、専用の生成処理を維持する。
            if (id == MenuID::ExecEngine) {
                createExecEngineTI(bar);
                if (!bar->items.empty())
                    tool_items_[id] = dmenu::ToolItemRef(bar.get(), bar->items.size() - 1U);
                continue;
            }
            if (id == MenuID::ExecEngineWithParty) {
                createExecEngineWithPartyTI(bar);
                if (!bar->items.empty())
                    tool_items_[id] = dmenu::ToolItemRef(bar.get(), bar->items.size() - 1U);
                continue;
            }
            if (id == MenuID::OuterTools) {
                createOuterToolsTI(bar);
                if (!bar->items.empty())
                    tool_items_[id] = dmenu::ToolItemRef(bar.get(), bar->items.size() - 1U);
                continue;
            }
            if (id == MenuID::OpenImportSource) {
                createOpenImportSourceTI(bar);
                if (!bar->items.empty())
                    tool_items_[id] = dmenu::ToolItemRef(bar.get(), bar->items.size() - 1U);
                continue;
            }
            const int style = (id == MenuID::ShowCardProp || id == MenuID::ShowCardImage ||
                               id == MenuID::ShowCardDetail) ? dmenu::MenuRadio : dmenu::MenuPush;
            const auto item = bar->append(
                toolText(id), toolText(id), toolImage(id), style,
                dmenu::MenuData{id, {}, [this, id] { return canDoMenu(id); }}, {},
                [this, id] { raiseEvent(id, SelectionEvent{}); });
            tool_items_[id] = item;
            if (const auto checked = menu_checked_.find(id); checked != menu_checked_.end())
                item.setChecked(checked->second());
        }
    }
    refShowMainToolBar();
    return !tool_bars_.empty();
}

void MainWindow::createExecEngineTI(const ToolBarPtr& bar) {
    if (!bar) return;
    const auto item = bar->append(toolText(MenuID::ExecEngine), toolText(MenuID::ExecEngine),
        toolImage(MenuID::ExecEngine), dmenu::ToolDropDown,
        dmenu::MenuData{MenuID::ExecEngine, {}, [this] {
            return canExecEngine() ||
                   (prop_ && !valueOf(prop_->var().classicEngines).empty());
        }}, {},
        [this] { execEngine(); });
    tool_items_[MenuID::ExecEngine] = item;
    tool_exec_engine_ = std::make_shared<dmenu::Menu>(true);
    if (auto* entry = bar->entry(item.index())) entry->menu = tool_exec_engine_;
}

void MainWindow::createExecEngineWithPartyTI(const ToolBarPtr& bar) {
    if (!bar) return;
    const auto item = bar->append(toolText(MenuID::ExecEngineWithParty),
        toolText(MenuID::ExecEngineWithParty), toolImage(MenuID::ExecEngineWithParty),
        dmenu::ToolDropDown,
        dmenu::MenuData{MenuID::ExecEngineWithParty, {}, [this] {
            return canExecEngineWithLastParty2() || canExecEngineWithPartyMenu();
        }},
        {}, [this] { execEngineWithLastParty(); });
    tool_items_[MenuID::ExecEngineWithParty] = item;
    tool_exec_party_ = std::make_shared<dmenu::Menu>(true);
    if (auto* entry = bar->entry(item.index())) entry->menu = tool_exec_party_;
}

void MainWindow::createOuterToolsTI(const ToolBarPtr& bar) {
    if (!bar) return;
    const auto item = bar->append(
        toolText(MenuID::OuterTools), toolText(MenuID::OuterTools),
        toolImage(MenuID::OuterTools), dmenu::ToolDropDown,
        dmenu::MenuData{MenuID::OuterTools, {}, [this] {
            return canDoMenu(MenuID::OuterTools);
        }}, {},
        // D版と同様、ボタン本体の操作は空にし、本体クリックで設定済みのドロップダウンが開くようにする。
        {});
    tool_items_[MenuID::OuterTools] = item;
    tool_outer_tools_ = std::make_shared<dmenu::Menu>(true);
    if (auto* entry = bar->entry(item.index())) entry->menu = tool_outer_tools_;
}

void MainWindow::createOpenImportSourceTI(const ToolBarPtr& bar) {
    if (!bar) return;
    const auto item = bar->append(
        toolText(MenuID::OpenImportSource), toolText(MenuID::OpenImportSource),
        toolImage(MenuID::OpenImportSource), dmenu::ToolDropDown,
        dmenu::MenuData{MenuID::OpenImportSource, {}, [this] {
            return canAddScenario() || canEditImportHistory();
        }}, [this, weak = std::weak_ptr<std::atomic_bool>(lifecycle_alive_),
             weak_bar = std::weak_ptr<dmenu::ToolBar>(bar)](dmenu::SelectionEvent& event) {
            const auto alive = weak.lock();
            if (!alive || !alive->load(std::memory_order_acquire)) return;
            if (canAddScenario()) {
                addScenario();
                return;
            }
            // D版と同様、シナリオがない場合はボタン本体からも取り込み履歴を開く。
            const auto owner = weak_bar.lock();
            const auto menu = tool_open_import_;
            if (!owner || !menu || !services_.gui) return;
            menu->refreshItems();
            if (!alive->load(std::memory_order_acquire)) return;
            const auto geometry = lib::tool_bar_item_geometry(*owner);
            if (event.itemIndex >= geometry.size()) return;
            services_.gui->show_context_menu(menu, owner,
                {geometry[event.itemIndex].bounds.x,
                 owner->bounds().y + owner->bounds().height});
        });
    tool_open_import_ = std::make_shared<dmenu::Menu>(true);
    if (auto* entry = bar->entry(item.index())) entry->menu = tool_open_import_;
    createImportHistoryMenu(tool_open_import_, false);
}

void MainWindow::customizeToolBar() {
    if (context_.customizeToolBar) {
        context_.customizeToolBar();
        updateMainToolBar(false);
        return;
    }
    if (toolbar_custom_dialog_ && toolbar_custom_dialog_->dialog() &&
        toolbar_custom_dialog_->dialog()->visible()) {
        toolbar_custom_dialog_->show();
        return;
    }
    // 実体化したバーだけでなく、保存されたグループ一覧を読む。 空のグルー
    // プには実行時の ToolBar がないが、D版の設定には含まれる。
    customtoolbar::ToolBarSettings current = prop_->var().mainToolBar.value;
    for (auto& group : current.tools)
        std::erase_if(group, [this](const structs::Tool& tool) {
            return !tool.separator && hiddenToolBarCommand(tool.menu);
        });
    const auto bindOwner=[weak=std::weak_ptr<std::atomic_bool>(lifecycle_alive_)](auto action) {
        return [weak,action=std::move(action)](auto&&... args)
            -> std::invoke_result_t<decltype(action)&,decltype(args)...> {
            using Result=std::invoke_result_t<decltype(action)&,decltype(args)...>;
            const auto life=weak.lock();
            if (!life || !life->load(std::memory_order_acquire)) {
                if constexpr (!std::is_void_v<Result>) return Result{};
                else return;
            }
            return action(std::forward<decltype(args)>(args)...);
        };
    };
    customtoolbar::Presentation presentation;
    presentation.undoMax=static_cast<std::size_t>(std::max<std::int32_t>(0,prop_->var().undoMaxEtc.value));
    presentation.undoLimit=bindOwner([this] { return static_cast<std::size_t>(std::max<std::int32_t>(0,prop_->var().undoMaxEtc.value)); });
    presentation.xmlCopy=prop_->var().xmlCopy.value;
    presentation.toolBarName=prop_->requiredMsgs().toolBarName.value;
    presentation.toolGroupName=prop_->requiredMsgs().toolGroupName.value;
    presentation.toolBarImage=images_ ? images_->toolBar() : 0;
    presentation.toolGroupImage=images_ ? images_->toolGroup() : 0;
    presentation.ok=prop_->requiredMsgs().dlgTextOK.value;
    presentation.cancel=prop_->requiredMsgs().dlgTextCancel.value;
    presentation.apply=prop_->requiredMsgs().dlgTextApply.value;
    presentation.upSelection=prop_->requiredMsgs().upSelection.value;
    presentation.downSelection=prop_->requiredMsgs().downSelection.value;
    presentation.addTool=prop_->requiredMsgs().menuText(MenuID::AddTool);
    presentation.removeTool=prop_->requiredMsgs().menuText(MenuID::Delete);
    presentation.addToolBar=prop_->requiredMsgs().menuText(MenuID::AddToolBar);
    presentation.addToolGroup=prop_->requiredMsgs().menuText(MenuID::AddToolGroup);
    presentation.incSearch=prop_->requiredMsgs().menuText(MenuID::IncSearch);
    presentation.undo=prop_->requiredMsgs().menuText(MenuID::Undo);
    presentation.redo=prop_->requiredMsgs().menuText(MenuID::Redo);
    presentation.copy=prop_->requiredMsgs().menuText(MenuID::Copy);
    presentation.cut=prop_->requiredMsgs().menuText(MenuID::Cut);
    presentation.paste=prop_->requiredMsgs().menuText(MenuID::Paste);
    presentation.deleteText=prop_->requiredMsgs().menuText(MenuID::Delete);
    presentation.resetToolBarSettings=prop_->requiredMsgs().menuText(MenuID::ResetToolBarSettings);
    presentation.sashLeft = prop_->var().mainToolBarCustomSashL.value;
    presentation.sashRight = prop_->var().mainToolBarCustomSashR.value;
    presentation.menuText = bindOwner([this](MenuID id) { return menuText(id); });
    presentation.menuImage = bindOwner([this](MenuID id) {
        // ゼロを指定すると、このコマンドをカスタマイズ画面の利用可能な項目から除外する。
        return hiddenToolBarCommand(id) ? 0U : menuImage(id);
    });
    presentation.menuTooltip = bindOwner([this](MenuID id) { return toolText(id); });
    presentation.menuShortcut=bindOwner([this](MenuID id) {
        const auto text=menuText(id);const auto tab=text.find('\t');
        return tab==String::npos ? String{} : text.substr(tab+1);
    });
    presentation.menuEnabled = bindOwner([this](MenuID id) { return canDoMenu(id); });
    presentation.menuChecked = bindOwner([this](MenuID id) {
        const auto found = menu_checked_.find(id);
        return found != menu_checked_.end() && found->second();
    });
    presentation.menuCommand = bindOwner([this](MenuID id) {
        raiseEvent(id, SelectionEvent{});
    });
    customtoolbar::Callbacks callbacks;
    // DSize は幅と高さだけを保存し、共通の配置処理はホストに任せる。
    callbacks.loadGeometry=bindOwner([this]() -> std::optional<lib::Rect> {
        const auto& size=prop_->flex().toolBarCustomDlg;
        return lib::Rect{0,0,static_cast<float>(size.width()),static_cast<float>(size.height())};
    });
    callbacks.saveGeometry=bindOwner([this](const lib::Rect& bounds) {
        auto& size=prop_->flex().toolBarCustomDlg;
        size.width(static_cast<std::int32_t>(bounds.width));
        size.height(static_cast<std::int32_t>(bounds.height));
        (void)prop_->apply();
    });
    callbacks.saveSashWeights = bindOwner([this](const std::int32_t left,
                                       const std::int32_t right) {
        if (!prop_) return;
        prop_->var().mainToolBarCustomSashL.value = left;
        prop_->var().mainToolBarCustomSashR.value = right;
        (void)prop_->apply();
    });
    callbacks.refreshToolBar = bindOwner([this] {
        if (comm_) comm_->refreshToolBar();
    });
    callbacks.subscribeUndoMax = [comm = comm_](lib::Action handler) {
        if (!comm || !handler) return lib::Action{};
        const auto token = comm->refUndoMax.add(std::move(handler));
        return lib::Action([comm, token] { comm->refUndoMax.remove(token); });
    };
    callbacks.applied = bindOwner([this](const customtoolbar::ToolBarSettings& tools) {
        const auto weak=std::weak_ptr<std::atomic_bool>(lifecycle_alive_);
        const auto live=[weak] {const auto life=weak.lock();return life && life->load(std::memory_order_acquire);};
        if (!prop_) return;
        auto updated = tools;
        {
            // カスタマイズ画面には、一時的に非表示のコマンドを出せな
            // い。 別のツールバー編集によって外部ツール・フォルダー
            // の設定が黙って消えないよう、保存済みの項目は保持する。
            const auto& saved = prop_->var().mainToolBar.value.tools;
            for (std::size_t group_index = 0; group_index < saved.size(); ++group_index) {
                for (std::size_t item_index = 0; item_index < saved[group_index].size(); ++item_index) {
                    const auto& hidden = saved[group_index][item_index];
                    if (!hiddenToolBarCommand(hidden.menu)) continue;
                    if (updated.tools.empty()) updated.tools.emplace_back();
                    auto& group = updated.tools[std::min(group_index, updated.tools.size() - 1U)];
                    group.insert(group.begin() + static_cast<std::ptrdiff_t>(
                        std::min(item_index, group.size())), hidden);
                }
            }
        }
        prop_->var().mainToolBar.value = std::move(updated);
        auto& order = prop_->var().toolsOrder.value;
        order.clear();
        order.reserve(tools.tools.size());
        for (std::size_t index = 0; index < tools.tools.size(); ++index)
            order.push_back(static_cast<std::int32_t>(index));
        (void)updateMainToolBar(false);
        if (!live()) return;
        composeRoot();
        if (!live()) return;
        const bool saved = prop_->apply();
        if (!live()) return;
        // D版と同様、他のエディタへの通知より先に保存する。 永続化に失敗したら古
        // い設定の再読み込みを要求せず、ALL のローカルだけの方針も維持する。
        if (saved && !lib::editor_window_host_options().all_mode && saveDockState())
            sendReloadProps();
    });
    callbacks.closed = bindOwner([this] {
        if (comm_) comm_->refreshToolBar();
    });
    auto initial = withRequiredMainToolBarSeparators(
        prop_->var().mainToolBar.INIT);
    {
        for (auto& group : initial.tools)
            std::erase_if(group, [](const structs::Tool& tool) {
                return hiddenToolBarCommand(tool.menu);
            });
    }
    const auto weak=std::weak_ptr<std::atomic_bool>(lifecycle_alive_);
    const auto dialog = std::make_shared<customtoolbar::ToolBarCustomDialog>(
        services_, window_, prop_->requiredMsgs().dlgTitCustomizeToolBar.value, menuImage(MenuID::CustomizeToolBar), current, initial,
        std::move(presentation), std::move(callbacks));
    const auto life=weak.lock();
    if (!life || !life->load(std::memory_order_acquire)) return;
    toolbar_custom_dialog_=dialog;
    dialog->show();
}

bool MainWindow::updateMainToolBar(bool saveOrder) {
    const auto result = createMainToolBar(saveOrder);
    refreshExecEngine();
    refreshOuterTools();
    return result;
}

dmenu::MenuItemRef MainWindow::getMenu(MenuID id) const {
    if (const auto found = menu_items_.find(id); found != menu_items_.end()) return found->second;
    return {};
}

bool MainWindow::canDoMenu(MenuID id) const {
    if (in_saving_) return false;
    if (!summary_ && !no_summary_menu_.contains(id)) return false;
    if (const auto found = menu_enabled_.find(id); found != menu_enabled_.end() && found->second)
        return found->second();
    return true;
}

void MainWindow::raiseEvent(MenuID id, const SelectionEvent& event) {
    std::optional<std::uint32_t> accelerator;
    if (event.is_accelerator) {
        const auto configured = prop_
            ? dmenu::convertAccelerator(prop_->buildMenu(id)) : 0U;
        accelerator = configured != 0U
            ? configured : dmenu::acceleratorModifierMask(event.accelerator_modifiers);
    }
    static_cast<void>(raiseEventImpl(id, event, {}, accelerator));
}

bool MainWindow::raiseEventImpl(const MenuID id, const SelectionEvent& event,
                                const StringView controlKey,
                                const std::optional<std::uint32_t> accelerator) {
    const auto lifetime = lifecycle_alive_;
    if (!lifetime || !lifetime->load(std::memory_order_acquire)) return false;

    // 有効化条件のコールバックを呼ぶ前に、操作を複製する。 コールバックが同
    // 期的にメニューのマップを再構築し、イテレーターを無効化する場合がある。
    SelectionHandler action;
    {
        const auto found = menu_actions_.find(id);
        if (found != menu_actions_.end()) action = found->second;
    }

    String action_name;
    try {
        action_name = enumutils::enumToString(id);
    } catch (...) {
        action_name = std::to_string(static_cast<int>(id));
    }
    const lib::EditorPerformanceScope performance(action_name, 0);
    const String source = event.toolBar ? "toolbar" : event.widget ? "widget" : "menu";
    const auto log_ignored = [&] {
        lib::editor_action_log("Action ignored source=" + source + " id=" + action_name);
    };
    if (!action) {
        log_ignored();
        return false;
    }

    const bool pane_route = !controlKey.empty();
    auto* const comm = comm_;
    const auto commLife = comm ? comm->lifetimeToken() : std::weak_ptr<const int>{};
    if ((accelerator && (!comm || commLife.expired())) ||
        (pane_route && !accelerator)) {
        log_ignored();
        return false;
    }

    bool enabled = false;
    if (pane_route) {
        if (in_saving_ || (!summary_ && !no_summary_menu_.contains(id))) {
            log_ignored();
            return false;
        }
        enabled = canDispatchSelectedMenu(id, controlKey);
    } else {
        enabled = canDoMenu(id);
    }
    if (!lifetime->load(std::memory_order_acquire)) return false;
    if (!enabled) {
        log_ignored();
        return false;
    }

    if (pane_route) {
        const auto gui = services_.gui;
        const auto* const folder = docking_folder_.get();
        if (!gui || !folder) {
            log_ignored();
            return false;
        }
        const auto currentFocus = gui->focused_widget();
        if (!lifetime->load(std::memory_order_acquire)) return false;
        if (currentFocus != event.widget || docking_folder_.get() != folder ||
            focusedControlKey(currentFocus) != controlKey || in_saving_ ||
            (!summary_ && !no_summary_menu_.contains(id)) || commLife.expired()) {
            log_ignored();
            return false;
        }
        bool action_still_registered{};
        {
            const auto found = menu_actions_.find(id);
            action_still_registered =
                found != menu_actions_.end() && bool(found->second);
        }
        if (!action_still_registered) {
            log_ignored();
            return false;
        }
    }
    if (accelerator && commLife.expired()) {
        log_ignored();
        return false;
    }

    auto action_event = event;
    if (accelerator) {
        action_event.is_accelerator = true;
        action_event.accelerator_modifiers =
            dmenu::acceleratorModifiers(*accelerator);
    }

    lib::editor_action_log("Action begin source=" + source + " id=" + action_name);
    const auto dispatch_action = [&]() {
        if (pane_route) {
            // D版のペインの代替処理と同様、登録済みの操作を呼ぶ。 C++版の製品ペインでは、フォー
            // カスのあるペインのキーを明示し、dispatchSelectedMenu を共有する。
            return dispatchSelectedMenu(id, action_event, controlKey);
        }
        // 有効化判定がコマンドの接続を再構築する場合がある。 無効
        // な古い接続ではなく、判定後に複製した現在の操作を呼ぶ。
        {
            const auto current = menu_actions_.find(id);
            action = current != menu_actions_.end() ? current->second : SelectionHandler{};
        }
        if (!action) {
            log_ignored();
            return false;
        }
        action(action_event);
        return true;
    };
    bool handled = true;
    if (accelerator) {
        comm->isMenuAccelerating(static_cast<int>(*accelerator));
        struct ResetMenuAcceleration {
            commons::Commons* comm;
            std::weak_ptr<const int> life;
            ~ResetMenuAcceleration() {
                if (!life.expired()) comm->isMenuAccelerating(0);
            }
        } reset{comm, commLife};
        handled = dispatch_action();
    } else {
        // 入れ子の通常メニュー操作でも、外側のショートカットの有効範囲を維持する。
        handled = dispatch_action();
    }

    if (!lifetime->load(std::memory_order_acquire)) return handled;
    if (!handled) {
        log_ignored();
        return false;
    }

    // メニュー操作とフォーカスのあるペインのショートカットで、
    // チェック状態、ステータス、ツールバーの更新を同じにする。
    menuActionAfter(id);
    if (!lifetime->load(std::memory_order_acquire)) return true;
    lib::editor_action_log("Action end source=" + source + " id=" + action_name);
    return true;
}

void MainWindow::menuAction(MenuID id, const SelectionEvent& event) { raiseEvent(id, event); }

MainWindow::SelectionHandler MainWindow::menuActionDlg(MenuID id) {
    return [this, id](const SelectionEvent& event) { menuAction(id, event); };
}

void MainWindow::menuActionAfter(MenuID id) {
    for (const auto& [checked_id, checked] : menu_checked_) {
        const bool value = checked && checked();
        if (const auto menu = menu_items_.find(checked_id); menu != menu_items_.end())
            menu->second.setChecked(value);
        if (const auto tool = tool_items_.find(checked_id); tool != tool_items_.end())
            tool->second.setChecked(value);
    }
    if (id == MenuID::ShowCardProp || id == MenuID::ShowCardImage || id == MenuID::ShowCardDetail)
        comm_->refCardImageStatus.call();
    comm_->refreshToolBar();
}

std::shared_ptr<cardwindow_ns::CardWindow>
MainWindow::selectedCardWindow(const StringView controlKey) const {
    const String selected = controlKey.empty() ? dock_.selected : String(controlKey);
    if (selected == "castCard") return cast_window_;
    if (selected == "skillCard") return skill_window_;
    if (selected == "itemCard") return item_window_;
    if (selected == "beastCard") return beast_window_;
    if (selected == "infoCard") return info_window_;
    if (const auto found = dynamic_card_windows_.find(selected);
        found != dynamic_card_windows_.end())
        return found->second.lock();
    // DockingFolder の work/work_1、side/side_1
    // は、保存済み CWXPath の別名。 ホストの履歴ボタ
    // ンに対象カードがないと判断する前に、その別名を解決する。
    if (docking_folder_) {
        if (const auto* tab = docking_folder_->tab(selected); tab &&
            !tab->restoreKey.empty()) {
            if (const auto found = dynamic_card_windows_.find(tab->restoreKey);
                found != dynamic_card_windows_.end())
                return found->second.lock();
        }
    }
    return {};
}

std::shared_ptr<eventwindow::EventWindow>
MainWindow::selectedEventWindow(const StringView controlKey) const {
    const String selected = controlKey.empty() ? dock_.selected : String(controlKey);
    if (const auto found = event_windows_.find(selected);
        found != event_windows_.end())
        return found->second.lock();
    if (docking_folder_) {
        if (const auto* tab = docking_folder_->tab(selected); tab &&
            !tab->restoreKey.empty()) {
            if (const auto found = event_windows_.find(tab->restoreKey);
                found != event_windows_.end())
                return found->second.lock();
        }
    }
    return {};
}

std::shared_ptr<areawindow::MenuActionRegistry>
MainWindow::selectedSceneMenuRegistry(const StringView controlKey) const {
    const String selected = controlKey.empty() ? dock_.selected : String(controlKey);
    if (const auto found = scene_menu_registries_.find(selected);
        found != scene_menu_registries_.end())
        return found->second;
    if (docking_folder_) {
        if (const auto* tab = docking_folder_->tab(selected); tab &&
            !tab->restoreKey.empty()) {
            if (const auto found = scene_menu_registries_.find(tab->restoreKey);
                found != scene_menu_registries_.end())
                return found->second;
        }
    }
    return {};
}

// D版と同様、現在フォーカスのあるコントロールのメニューツリーからツールバーの操作を解決
// する。 そのコントロールのドッキングキーを一度取得し、通常の選択ペイン操作と同じ実行・
// 有効化条件の表を使う。 focused_widget() は外部のコールバックを呼ぶ。
String MainWindow::focusedControlKey() const {
    const auto life = lifecycle_alive_;
    const auto gui = services_.gui;
    if (!life || !life->load(std::memory_order_acquire) || !gui) return {};
    const auto focused = gui->focused_widget();
    if (!life->load(std::memory_order_acquire) || !focused) return {};
    return focusedControlKey(focused);
}

String MainWindow::focusedControlKey(const lib::WidgetPtr& focused) const {
    const auto life = lifecycle_alive_;
    const auto* const folder = docking_folder_.get();
    if (!life || !life->load(std::memory_order_acquire) || !focused || !folder)
        return {};
    const auto controls = folder->allControls();
    for (const auto& control : controls) {
        if (!control || !lib::contains_widget(control, focused.get())) continue;
        const auto key = folder->keyFromCtrl(control);
        return life->load(std::memory_order_acquire) &&
                       docking_folder_.get() == folder
                   ? key : String{};
    }
    return {};
}

bool MainWindow::canDispatchFocusedIncSearch() const {
    const auto life = lifecycle_alive_;
    if (!life || !life->load(std::memory_order_acquire)) return false;
    const auto controlKey = focusedControlKey();
    if (!life->load(std::memory_order_acquire) || controlKey.empty()) return false;
    const bool enabled = canDispatchSelectedMenu(MenuID::IncSearch, controlKey);
    return life->load(std::memory_order_acquire) && enabled;
}

bool MainWindow::dispatchFocusedIncSearch(const SelectionEvent& event) {
    const auto life = lifecycle_alive_;
    if (!life || !life->load(std::memory_order_acquire)) return false;
    const auto controlKey = focusedControlKey();
    if (!life->load(std::memory_order_acquire) || controlKey.empty() ||
        !canDispatchSelectedMenu(MenuID::IncSearch, controlKey) ||
        !life->load(std::memory_order_acquire))
        return false;
    return dispatchSelectedMenu(MenuID::IncSearch, event, controlKey);
}

bool MainWindow::dispatchConfiguredPaneShortcut(const lib::KeyEvent& event) {
    const auto life = lifecycle_alive_;
    const auto gui = services_.gui;
    const auto* const folder = docking_folder_.get();
    const auto* const properties = prop_;
    if (!life || !life->load(std::memory_order_acquire) || !gui || !folder ||
        !properties)
        return false;

    const auto focused = gui->focused_widget();
    if (!life->load(std::memory_order_acquire) || !focused ||
        docking_folder_.get() != folder)
        return false;
    // D版の TopLevelPanel::doMenu と同様、フォーカスのあるペインだけを対象にする。 C++
    // 版のペイン操作は MainWindow の選択ペインの振り分け処理にあるため、その共通経路を維持する。
    const auto pane = commons::getTopLevelPanel(focused);
    if (!pane || !life->load(std::memory_order_acquire)) return false;
    const auto controlKey = focusedControlKey(focused);
    if (!life->load(std::memory_order_acquire) || controlKey.empty() ||
        docking_folder_.get() != folder)
        return false;

    // D版と同様、各ペインの操作登録一覧のスナップショットを走査する。 下の有効化判定が menu_actions_
    // を再構築する場合があるため、ここでもコマンドIDを複製する。
    std::vector<MenuID> actionIds;
    actionIds.reserve(menu_actions_.size());
    for (const auto& [id, action] : menu_actions_)
        if (id != MenuID::None && action) actionIds.push_back(id);

    const auto text = std::dynamic_pointer_cast<lib::TextEdit>(focused);
    const auto combo = std::dynamic_pointer_cast<lib::ComboBox>(focused);
    const auto number = std::dynamic_pointer_cast<lib::NumberEdit>(focused);
    const bool editableControlOwnsKey =
        (text && !text->read_only) || (combo && combo->editable) ||
        (number && !number->read_only);
    // C++版はペイン固有の操作も MainWindow に登録する。 先
    // に共有操作を試し、その後で固有の操作を同じフォーカス・有効化検査へ通
    // す。 編集可能なコントロールのキー入力は、そのコントロールに任せる。
    for (int pass = 0; pass != 2; ++pass) {
        const bool localPass = pass != 0;
        for (const auto id : actionIds) {
            if (!life->load(std::memory_order_acquire) ||
                docking_folder_.get() != folder)
                return false;
            const bool local = ::cwxeditor::cwx::menu::isLocalMenu(id);
            if (local != localPass || (local && editableControlOwnsKey)) continue;
            const String shortcut = properties->flex().menu.hotkey(id);
            if (!lib::menu_shortcut_matches(shortcut, event.key, event.modifiers,
                                            event.printable))
                continue;

            SelectionEvent selection;
            selection.widget = focused;
            selection.modifiers = event.modifiers;
            selection.physical_control = event.physical_control;
            const auto accelerator = dmenu::convertAccelerator(properties->buildMenu(id));
            if (!life->load(std::memory_order_acquire)) return false;
            if (raiseEventImpl(id, selection, controlKey, accelerator)) return true;
            if (!life->load(std::memory_order_acquire)) return false;
        }
    }
    return false;
}

bool MainWindow::dispatchSelectedMenu(const MenuID id,
                                      const SelectionEvent& event,
                                      const StringView controlKey) {
    const String selectedControl = controlKey.empty() ? dock_.selected : String(controlKey);
    if (const auto editor = selectedEventWindow(selectedControl)) {
        switch (id) {
        case MenuID::Cut: editor->cut(); return true;
        case MenuID::Copy: editor->copy(); return true;
        case MenuID::Paste: editor->paste(); return true;
        case MenuID::Delete: editor->del(); return true;
        case MenuID::Clone: editor->clone(); return true;
        default: break;
        }
        const auto command = eventCommandForMenu(id);
        if (command) {
            const lib::CommandGesture gesture{
                event.modifiers, lib::PointerButton::None,
                event.physical_control, event.accelerator_modifiers};
            return editor->invoke(*command, gesture);
        }
    }

    if (const auto registry = selectedSceneMenuRegistry(selectedControl); registry &&
        registry->dispatch(id))
        return true;

    if (selectedControl == "data" && area_table_) {
        areatable_ns::AreaTableSelectionEvent selection;
        switch (id) {
        case MenuID::Undo: area_table_->undo(); return true;
        case MenuID::Redo: area_table_->redo(); return true;
        case MenuID::Cut: area_table_->cut(selection); return true;
        case MenuID::Copy: area_table_->copy(selection); return true;
        case MenuID::Paste: area_table_->paste(selection); return true;
        case MenuID::Delete: area_table_->del(selection); return true;
        case MenuID::Clone: area_table_->clone(selection); return true;
        case MenuID::SelectAll: area_table_->selectAll(); return true;
        case MenuID::IncSearch: area_table_->incSearch(); return true;
        case MenuID::Comment: area_table_->writeComment(); return true;
        case MenuID::Up: area_table_->up(); return true;
        case MenuID::Down: area_table_->down(); return true;
        case MenuID::FindID: area_table_->replaceID(); return true;
        case MenuID::EditProp: case MenuID::ShowProp: area_table_->edit(); return true;
        case MenuID::EditScene: area_table_->openAreaScene(true, false); return true;
        case MenuID::EditSceneDup: area_table_->openAreaScene(true, true); return true;
        case MenuID::EditEvent: area_table_->openAreaEvent(true, false); return true;
        case MenuID::EditEventDup: area_table_->openAreaEvent(true, true); return true;
        case MenuID::SetStartArea: area_table_->setStartArea(); return true;
        case MenuID::Import: area_table_->doImport(); return true;
        case MenuID::ChangeVH: area_table_->changeVHSide(); return true;
        case MenuID::ReNumbering: area_table_->reNumbering(); return true;
        default: break;
        }
    }

    if (selectedControl == "flag" && flags_pane_) {
        flagspane_ns::SelectionEvent selection;
        switch (id) {
        case MenuID::Undo: flags_pane_->undo(); return true;
        case MenuID::Redo: flags_pane_->redo(); return true;
        case MenuID::Cut: flags_pane_->cut(selection); return true;
        case MenuID::Copy: flags_pane_->copy(selection); return true;
        case MenuID::Paste: flags_pane_->paste(selection); return true;
        case MenuID::Delete: flags_pane_->del(selection); return true;
        case MenuID::Clone: flags_pane_->clone(selection); return true;
        case MenuID::EditProp: case MenuID::ShowProp: flags_pane_->edit(); return true;
        case MenuID::IncSearch: flags_pane_->flags().incSearch(); return true;
        case MenuID::FindID: flags_pane_->replaceID(); return true;
        case MenuID::CopyVariablePath: flags_pane_->copyVariablePath(); return true;
        case MenuID::ChangeVH: flags_pane_->changeVHSide(); return true;
        default: break;
        }
    }

    if (const auto cards = selectedCardWindow(selectedControl)) {
        cardwindow_ns::SelectionEvent selection{event.modifiers};
        switch (id) {
        case MenuID::Undo: cards->undo(); return true;
        case MenuID::Redo: cards->redo(); return true;
        case MenuID::Cut: cards->cut(selection); return true;
        case MenuID::Copy: cards->copy(selection); return true;
        case MenuID::Paste: cards->paste(selection); return true;
        case MenuID::Delete: cards->del(selection); return true;
        case MenuID::Clone: cards->clone(selection); return true;
        case MenuID::SelectAll: cards->selectAll(); return true;
        case MenuID::IncSearch: cards->incSearch(); return true;
        case MenuID::Comment: cards->writeComment(); return true;
        case MenuID::Up: cards->up(); return true;
        case MenuID::Down: cards->down(); return true;
        case MenuID::FindID: cards->replaceID(); return true;
        case MenuID::EditProp: case MenuID::ShowProp: cards->edit(); return true;
        case MenuID::SelectConnectedResource: cards->selectConnectedResource(); return true;
        case MenuID::Import: cards->doImport(); return true;
        case MenuID::OpenHand: cards->editHand(); return true;
        case MenuID::ReNumbering: cards->reNumbering(); return true;
        case MenuID::RemoveRef: cards->removeRef(); return true;
        case MenuID::EditEvent: cards->editEvent(false); return true;
        case MenuID::EditEventDup: cards->editEvent(true); return true;
        case MenuID::EditEventAtTimeOfUsing: cards->editUseEvent(false); return true;
        case MenuID::ChangeVH: cards->changeVHSide(); return true;
        default: break;
        }
    }

    if (selectedControl == "file" && directory_window_) {
        switch (id) {
        case MenuID::Cut: directory_window_->cut(); return true;
        case MenuID::Copy: directory_window_->copy(); return true;
        case MenuID::Paste: directory_window_->paste(); return true;
        case MenuID::Delete: directory_window_->del(event.modifiers); return true;
        case MenuID::Clone: directory_window_->clone(); return true;
        case MenuID::SelectAll: directory_window_->selectAll(); return true;
        case MenuID::IncSearch: directory_window_->incSearch(); return true;
        case MenuID::FindID: directory_window_->replaceID(); return true;
        case MenuID::CopyFilePath: directory_window_->copyFilePath(); return true;
        case MenuID::CreateArchive: directory_window_->createArchive(); return true;
        case MenuID::ChangeVH: directory_window_->changeVHSide(); return true;
        default: break;
        }
    }

    const auto invoke_name = [id](const auto& value) {
        if (!value) return false;
        switch (id) {
        case MenuID::Undo: case MenuID::Redo: case MenuID::Copy:
        case MenuID::SelectAll: case MenuID::FindID: case MenuID::EditProp:
        case MenuID::IncSearch: case MenuID::CopyAsText:
            value->invoke(id); return true;
        default: return false;
        }
    };
    if (selectedControl == "coupon" && invoke_name(coupon_window_)) return true;
    if (selectedControl == "gossip" && invoke_name(gossip_window_)) return true;
    if (selectedControl == "completeStamp" && invoke_name(complete_stamp_window_)) return true;
    if (selectedControl == "keyCode" && invoke_name(key_code_window_)) return true;
    if (selectedControl == "cellName" && invoke_name(cell_name_window_)) return true;
    if (selectedControl == "cardGroup" && invoke_name(card_group_window_)) return true;
    return false;
}

bool MainWindow::canDispatchSelectedMenu(const MenuID id,
                                         const StringView controlKey) const {
    const String selectedControl = controlKey.empty() ? dock_.selected : String(controlKey);
    if (const auto editor = selectedEventWindow(selectedControl)) {
        switch (id) {
        case MenuID::Cut: return editor->canDoT();
        case MenuID::Copy: return editor->canDoC();
        case MenuID::Paste: return editor->canDoP();
        case MenuID::Delete: return editor->canDoD();
        case MenuID::Clone: return editor->canDoClone();
        default: break;
        }
        const auto command = eventCommandForMenu(id);
        if (!command) return false;
        const auto found = editor->commands().find(*command);
        return found != editor->commands().end() &&
               (!found->second.enabled || found->second.enabled());
    }
    if (const auto registry = selectedSceneMenuRegistry(selectedControl); registry &&
        registry->entries().contains(id))
        return registry->enabled(id);
    if (selectedControl == "data" && area_table_) {
        switch (id) {
        case MenuID::Undo: return area_table_->canUndo();
        case MenuID::Redo: return area_table_->canRedo();
        case MenuID::Cut: return area_table_->canDoT();
        case MenuID::Copy: return area_table_->canDoC();
        case MenuID::Paste: return area_table_->canDoP();
        case MenuID::Delete: return area_table_->canDoD();
        case MenuID::Clone: return area_table_->canDoClone();
        case MenuID::SelectAll: return area_table_->canSelectAll();
        case MenuID::IncSearch: return !area_table_->rows().empty();
        case MenuID::Comment: return area_table_->canWriteComment();
        case MenuID::Up: return area_table_->canUp();
        case MenuID::Down: return area_table_->canDown();
        case MenuID::FindID: return area_table_->canReplaceID();
        case MenuID::EditProp: case MenuID::ShowProp: return area_table_->canEdit();
        case MenuID::EditScene: case MenuID::EditSceneDup: return area_table_->canOpenAreaScene();
        case MenuID::EditEvent: case MenuID::EditEventDup: return area_table_->canOpenAreaEvent();
        case MenuID::SetStartArea: return area_table_->canSetStartArea();
        case MenuID::Import: return area_table_->canDoImport();
        case MenuID::ChangeVH: return area_table_->canChangeVH();
        case MenuID::ReNumbering: return area_table_->canReNumbering();
        default: return false;
        }
    }
    if (selectedControl == "flag" && flags_pane_) {
        switch (id) {
        case MenuID::Undo: return flags_pane_->canUndo();
        case MenuID::Redo: return flags_pane_->canRedo();
        case MenuID::Cut: return flags_pane_->canDoT();
        case MenuID::Copy: return flags_pane_->canDoC();
        case MenuID::Paste: return flags_pane_->canDoP();
        case MenuID::Delete: return flags_pane_->canDoD();
        case MenuID::Clone: return flags_pane_->canDoClone();
        case MenuID::EditProp: case MenuID::ShowProp: return flags_pane_->canEdit();
        case MenuID::IncSearch: return !flags_pane_->flags().rows().empty();
        case MenuID::FindID: return flags_pane_->canReplaceID();
        case MenuID::CopyVariablePath: return flags_pane_->canCopyVariablePath();
        case MenuID::ChangeVH: return true;
        default: return false;
        }
    }
    if (const auto cards = selectedCardWindow(selectedControl)) {
        switch (id) {
        case MenuID::Undo: return cards->canUndo();
        case MenuID::Redo: return cards->canRedo();
        case MenuID::Cut: return cards->canDoT();
        case MenuID::Copy: return cards->canDoC();
        case MenuID::Paste: return cards->canDoP();
        case MenuID::Delete: return cards->canDoD();
        case MenuID::Clone: return cards->canDoClone();
        case MenuID::SelectAll: return cards->canSelectAll();
        case MenuID::IncSearch: return cards->canIncSearch();
        case MenuID::Comment: return cards->canWriteComment();
        case MenuID::Up: return cards->canUp();
        case MenuID::Down: return cards->canDown();
        case MenuID::FindID: return cards->canReplaceID();
        case MenuID::EditProp: case MenuID::ShowProp: return cards->canEdit();
        case MenuID::SelectConnectedResource: return cards->canSelectConnectedResource();
        case MenuID::Import: return cards->canDoImport();
        case MenuID::OpenHand: return cards->canEditHand();
        case MenuID::ReNumbering: return cards->canReNumbering();
        case MenuID::RemoveRef: return cards->canRemoveRef();
        case MenuID::EditEvent: case MenuID::EditEventDup: return cards->canEditEvent();
        case MenuID::EditEventAtTimeOfUsing: return cards->canEditUseEvent();
        case MenuID::ChangeVH: return cards->canChangeVH();
        default: return false;
        }
    }
    if (selectedControl == "file" && directory_window_) {
        switch (id) {
        case MenuID::Cut: return directory_window_->canDoT();
        case MenuID::Copy: return directory_window_->canDoC();
        case MenuID::Paste: return directory_window_->canDoP();
        case MenuID::Delete: return directory_window_->canDoD();
        case MenuID::Clone: return directory_window_->canDoClone();
        case MenuID::SelectAll:
            return directory_window_->summary() && directory_window_->files() &&
                   !directory_window_->files()->rows.empty();
        case MenuID::IncSearch:
            return directory_window_->files() && !directory_window_->files()->rows.empty();
        case MenuID::FindID: return directory_window_->canReplaceID();
        case MenuID::CopyFilePath: return directory_window_->canDoC();
        case MenuID::CreateArchive: return directory_window_->canCreateArchive();
        case MenuID::ChangeVH: return true;
        default: return false;
        }
    }
    const auto can_name = [id](const auto& value) {
        if (!value) return std::optional<bool>{};
        switch (id) {
        case MenuID::Undo: return std::optional<bool>{value->view().canUndo()};
        case MenuID::Redo: return std::optional<bool>{value->view().canRedo()};
        case MenuID::Copy: case MenuID::CopyAsText:
            return std::optional<bool>{value->canDoC()};
        case MenuID::SelectAll:
            return std::optional<bool>{value->view().canSelectAll()};
        case MenuID::FindID:
            return std::optional<bool>{value->view().canFindID()};
        case MenuID::EditProp:
            return std::optional<bool>{value->view().canEditName()};
        case MenuID::IncSearch:
            return std::optional<bool>{value->view().canIncSearch()};
        default: return std::optional<bool>{false};
        }
    };
    if (selectedControl == "coupon") return can_name(coupon_window_).value_or(false);
    if (selectedControl == "gossip") return can_name(gossip_window_).value_or(false);
    if (selectedControl == "completeStamp")
        return can_name(complete_stamp_window_).value_or(false);
    if (selectedControl == "keyCode") return can_name(key_code_window_).value_or(false);
    if (selectedControl == "cellName") return can_name(cell_name_window_).value_or(false);
    if (selectedControl == "cardGroup") return can_name(card_group_window_).value_or(false);
    return false;
}

void MainWindow::refreshAllDialogSelector() {
    if (!all_dialog_selector_) return;
    all_dialog_selector_items_.clear();
    std::vector<lib::ChoiceItem> items;
    // 0行目はメニュー起動欄と同様の見出し。 その後に
    // 最小化したダイアログを、最前面のものから並べる。
    items.push_back({"ダイアログ", ""});
    if (services_.gui) {
        const auto dialogs = services_.gui->hosted_dialogs();
        for (auto it = dialogs.rbegin(); it != dialogs.rend(); ++it) {
            const auto& dialog = *it;
            if (!dialog || !dialog->minimized) continue;
            items.push_back({dialog->title.empty() ? String("(無題)") : String(dialog->title),
                             std::to_string(all_dialog_selector_items_.size() + 1U),
                             dialog->icon, true});
            all_dialog_selector_items_.push_back(dialog);
        }
    }
    (void)lib::replace_combo_items(all_dialog_selector_, std::move(items), 0U);
    all_dialog_selector_->set_enabled(!all_dialog_selector_items_.empty());
    if (services_.gui) services_.gui->request_redraw(all_dialog_selector_);
}

void MainWindow::setupMenuListener(const MenuPtr& menu) {
    if (!menu) return;
    const auto weak = std::weak_ptr<dmenu::Menu>(menu);
    const auto host = std::weak_ptr<std::atomic_bool>(lifecycle_alive_);
    // メニューに保存するコールバックは、同じメニューを強参照で所有しない。 D版はウィ
    // ンドウの破棄でリスナーを外すが、C++版ではコントロールが保持される場合もある。
    menu->on_command = [weak, host](std::string_view command) {
        const auto alive = host.lock();
        if (!alive || !alive->load(std::memory_order_acquire)) return;
        if (const auto current = weak.lock()) current->dispatch(command, SelectionEvent{});
    };
    menu->on_open = [weak, host] {
        const auto alive = host.lock();
        if (!alive || !alive->load(std::memory_order_acquire)) return;
        lib::editor_action_log("Menu open begin");
        const lib::EditorPerformanceScope performance("menu.open", 0);
        if (const auto current = weak.lock()) current->refreshItems();
    };
}

void MainWindow::retireOwnedMenuBindings() {
    // すべてのホストで、同じSWT互換の所有権の境界を使う。 外部の shared_ptr
    // はモデルだけを保持し、古い Main のコールバックは保持しない。
    for (const auto& menu : {menu_bar_,menu_file_,menu_exec_engine_,menu_exec_party_,
                            tool_exec_engine_,tool_exec_party_,menu_outer_tools_,
                            tool_outer_tools_,menu_open_import_,tool_open_import_})
        if (menu) menu->retireBindings();
    for (const auto& toolbar : tool_bars_) if (toolbar) toolbar->retireBindings();
}

void MainWindow::setupMenu(const std::map<MenuID, dmenu::MenuItemRef*>& menus) {
    for (const auto& [id, item] : menus) {
        if (!item || !*item) continue;
        item->setEnabled(canDoMenu(id));
        if (const auto found = menu_checked_.find(id); found != menu_checked_.end())
            item->setChecked(found->second());
    }
}

void MainWindow::rebuildMenus() {
    retireOwnedMenuBindings();
    const bool all_mode = isAllMode();
    menu_bar_ = std::make_shared<dmenu::Menu>();
    menu_file_ = std::make_shared<dmenu::Menu>(true);
    menu_exec_engine_ = std::make_shared<dmenu::Menu>(true);
    menu_exec_party_ = std::make_shared<dmenu::Menu>(true);
    menu_outer_tools_ = std::make_shared<dmenu::Menu>(true);
    menu_items_.clear();
    menu_actions_.clear();
    menu_enabled_.clear();
    menu_checked_.clear();
    tool_items_.clear();
    setupMenuListener(menu_bar_);
    const auto bind = [this](MenuID id, SelectionHandler action,
                             std::function<bool()> enabled = {}) {
        menu_actions_[id] = std::move(action);
        if (enabled) menu_enabled_[id] = std::move(enabled);
    };
    const auto fallback = [this](const SelectionEvent&, const MenuID id) {
        if (comm_) comm_->refMenu.call(id);
    };
    bind(MenuID::New, [this](const SelectionEvent&) { createScenario(); });
    bind(MenuID::Open, [this](const SelectionEvent&) { openScenarioM(); });
    bind(MenuID::Save, [this](const SelectionEvent&) { saveScenario(); },
         [this] { return canSaveOverwrite(); });
    bind(MenuID::SaveAs, [this](const SelectionEvent&) { saveScenarioA(); },
         [this] { return summary_ && !in_saving_; });
    bind(MenuID::Reload, [this](const SelectionEvent&) { reload(); },
         [this] { return summary_ != nullptr; });
    bind(MenuID::Close, [this](const SelectionEvent&) { exitAll(); });
    bind(MenuID::Find, [this](const SelectionEvent&) { replaceText(); });
    bind(MenuID::IncSearch, [this](const SelectionEvent& event) {
        (void)dispatchFocusedIncSearch(event);
    }, [this] { return canDispatchFocusedIncSearch(); });
    bind(MenuID::ReNumberingAll,
         [this](const SelectionEvent&) { reNumberingAll(); },
         [this] { return summary_ != nullptr; });
    bind(MenuID::ToXMLText, [this](const SelectionEvent&) { clipboardToXML(); },
         [this] {
             return services_.clipboard &&
                    xmlbytestransfer::CBisXMLOnly(*services_.clipboard);
         });
    bind(MenuID::TableView, [this](const SelectionEvent&) { openDataWindow(); });
    bind(MenuID::VarView, [this](const SelectionEvent&) { openFlagWindow(); });
    bind(MenuID::CastView, [this](const SelectionEvent&) { openCast(); });
    bind(MenuID::SkillView, [this](const SelectionEvent&) { openSkill(); });
    bind(MenuID::ItemView, [this](const SelectionEvent&) { openItem(); });
    bind(MenuID::BeastView, [this](const SelectionEvent&) { openBeast(); });
    bind(MenuID::InfoView, [this](const SelectionEvent&) { openInfo(); });
    bind(MenuID::FileView, [this](const SelectionEvent&) { openDirWindow(); });
    bind(MenuID::CouponView, [this](const SelectionEvent&) { openCouponWindow(); });
    bind(MenuID::GossipView, [this](const SelectionEvent&) { openGossipWindow(); });
    bind(MenuID::CompleteStampView, [this](const SelectionEvent&) { openCompleteStampWindow(); });
    bind(MenuID::KeyCodeView, [this](const SelectionEvent&) { openKeyCodeWindow(); });
    bind(MenuID::CellNameView, [this](const SelectionEvent&) { openCellNameWindow(); });
    bind(MenuID::CardGroupView, [this](const SelectionEvent&) { openCardGroupWindow(); });
    bind(MenuID::ExecEngine, [this](const SelectionEvent&) { execEngine(); },
         [this] {
             return canExecEngine() ||
                    (prop_ && !valueOf(prop_->var().classicEngines).empty());
         });
    bind(MenuID::ExecEngineWithLastParty, [this](const SelectionEvent&) {
        execEngineWithLastParty();
    }, [this] { return canExecEngineWithLastParty2(); });
    bind(MenuID::OpenDir, [this](const SelectionEvent&) { openDirectory(); },
         [this] { return canOpenDirectory(); });
    bind(MenuID::OpenBackupDir, [this](const SelectionEvent&) { openBackupDirectory(); },
         [this] { return canOpenBackupDirectory(); });
    bind(MenuID::OpenImportSource, [this](const SelectionEvent&) { addScenario(); },
         [this] { return canAddScenario() || canEditImportHistory(); });
    bind(MenuID::SelectImportSource, [this](const SelectionEvent&) { addScenario(); },
         [this] { return canAddScenario(); });
    // 操作の振り分けと、表示するファイルメニューの有効化条件を、 単独ウィ
    // ンドウを持たない ALL の一つの埋め込みエディタ画面に合わせる。
    bind(MenuID::NewAtNewWindow, [this](const SelectionEvent&) { createScenarioNewWin(); },
         [this] { return !lib::editor_window_host_options().all_mode &&
                         services_.processes && !app_path_.empty(); });
    bind(MenuID::OpenAtNewWindow, [this](const SelectionEvent&) { openScenarioNewWin(); },
         [this] { return !lib::editor_window_host_options().all_mode &&
                         services_.processes && !app_path_.empty(); });
    bind(MenuID::CreateArchive, [this](const SelectionEvent&) {
        if (directory_window_) directory_window_->createArchive();
    }, [this] {
        return directory_window_ && directory_window_->canCreateArchive();
    });
    bind(MenuID::EditScenarioHistory, [this](const SelectionEvent&) { editScenarioHistory(); },
         [this] { return canEditScenarioHistory(); });
    bind(MenuID::EditImportHistory, [this](const SelectionEvent&) { editImportHistory(); },
         [this] { return canEditImportHistory(); });
    bind(MenuID::EditExecutedPartyHistory,
         [this](const SelectionEvent&) { editExecutedPartyHistory(); },
         [this] { return canEditExecutedPartyHistory(); });
    bind(MenuID::DeleteNotExistsHistory,
         [this](const SelectionEvent&) { delNotExistsScenario(); },
         [this] { return canDelNotExistsScenario(); });
    bind(MenuID::DeleteNotExistsParties,
         [this](const SelectionEvent&) { delNotExistsParty(); },
         [this] { return canDelNotExistsParty(); });
    // ALL のツールバーはホストが提供するため、 埋め込みエディ
    // タでは単独版のカスタマイズダイアログを意図的に使えなくする。
    bind(MenuID::CustomizeToolBar,
         [this](const SelectionEvent&) { customizeToolBar(); },
         [this] { return !lib::editor_window_host_options().all_mode; });
    bind(MenuID::NewDir, [this](const SelectionEvent&) {
        if (directory_window_) directory_window_->createNewFolder();
    }, [this] { return directory_window_ && directory_window_->canCreateNewFolder(); });
    bind(MenuID::DelNotUsedFile, [this](const SelectionEvent& event) {
        // 全体のコマンドと局所的なコマンドを、同じ修飾キーを考慮した経路へ渡す。
        if (directory_window_) directory_window_->deleteUnuse(event.modifiers);
    }, [this] { return directory_window_ && directory_window_->canDeleteUnuse(); });
    bind(MenuID::EditSummary, [this](const SelectionEvent&) {
        if (area_table_) area_table_->editSummary();
    }, [this] { return area_table_ && area_table_->summary(); });
    const auto prepare_new_scene = [this](const bool focus_table) -> areatable_ns::AreaTable* {
        if (!area_table_ || !area_table_->summary()) return nullptr;
        const auto life = lifecycle_alive_;
        auto* const table = area_table_.get();
        const auto summary = table->summary();
        // D版と同様、三つの操作はいずれもウィンドウを有効化せずに Data を開く。
        // NewAreaDir はディレクトリツリーにフォーカスを残し、 NewArea/NewBattle
        // はインラインの名前編集に入る前に表へフォーカスを移す。
        if (comm_) comm_->openDataWin(false);
        else openDataWindow();
        const auto current = [&] {
            return life && life->load(std::memory_order_acquire) &&
                   area_table_.get() == table && table->summary() == summary;
        };
        if (!current()) return nullptr;
        if (focus_table && services_.gui && table->table())
            services_.gui->focus(table->table());
        return current() ? table : nullptr;
    };
    bind(MenuID::NewAreaDir, [prepare_new_scene](const SelectionEvent&) {
        if (auto* table = prepare_new_scene(false)) table->createDir();
    }, [this] { return area_table_ && area_table_->canCreateDir(); });
    bind(MenuID::NewArea, [prepare_new_scene](const SelectionEvent&) {
        if (auto* table = prepare_new_scene(true)) table->createArea();
    }, [this] { return area_table_ && area_table_->summary(); });
    bind(MenuID::NewBattle, [prepare_new_scene](const SelectionEvent&) {
        if (auto* table = prepare_new_scene(true)) table->createBattle();
    }, [this] { return area_table_ && area_table_->summary(); });
    bind(MenuID::NewPackage, [this](const SelectionEvent&) {
        if (!area_table_) return;
        // パッケージ作成は AreaTable でインラインの名前編集を開始
        // する。 左上で別のペインを選んでいる場合は、先にその表を表示する。
        openDataWindow();
        area_table_->createPackage();
    }, [this] { return area_table_ && area_table_->summary(); });
    bind(MenuID::NewFlagDir, [this](const SelectionEvent&) {
        if (!flags_pane_) return;
        // フォルダー作成は、左上の変数ツリーでインラインの名前編集を開
        // 始する。 どのホストでも、編集開始前にそのペインを表示する。
        openFlagWindow();
        flags_pane_->dirs().createDir();
    }, [this] { return flags_pane_ && flags_pane_->dirs().current(); });
    bind(MenuID::NewFlag, [this](const SelectionEvent&) {
        if (flags_pane_) flags_pane_->flags().createFlag();
    }, [this] { return flags_pane_ && flags_pane_->dirs().current(); });
    bind(MenuID::NewStep, [this](const SelectionEvent&) {
        if (flags_pane_) flags_pane_->flags().createStep();
    }, [this] { return flags_pane_ && flags_pane_->dirs().current(); });
    bind(MenuID::NewVariant, [this](const SelectionEvent&) {
        if (flags_pane_) flags_pane_->flags().createVariant();
    }, [this] { return summary_ && flags_pane_ && flags_pane_->flags().canCreateVariant(); });
    // D版の NewCard テンプレートと同様、現在選択されている CardWindow
    // を最初の対象とする。 複製・分離したカードビューでも、カードメニュー
    // からの作成で、 そのビューの現在のディレクトリと選択状態を維持する。
    bind(MenuID::NewCast, [this](const SelectionEvent&) { newCast(); },
         [this] { return canNewCard(); });
    bind(MenuID::NewSkill, [this](const SelectionEvent&) { newSkill(); },
         [this] { return canNewCard(); });
    bind(MenuID::NewItem, [this](const SelectionEvent&) { newItem(); },
         [this] { return canNewCard(); });
    bind(MenuID::NewBeast, [this](const SelectionEvent&) { newBeast(); },
         [this] { return canNewCard(); });
    bind(MenuID::NewInfo, [this](const SelectionEvent&) { newInfo(); },
         [this] { return canNewCard(); });
    bind(MenuID::OuterTools, {}, [this] {
        return prop_ && !valueOf(prop_->var().outerTools).empty();
    });
    bind(MenuID::Settings, [this](const SelectionEvent&) { settings(); });
    bind(MenuID::VersionInfo, [this](const SelectionEvent&) { versionInfo(); });
    bind(MenuID::Refresh, [this](const SelectionEvent& event) { refreshAll(event); },
         [this] { return summary_ != nullptr; });
    bind(MenuID::ShowCardProp, [this](const SelectionEvent& event) { showCardLife(event); });
    bind(MenuID::ShowCardImage, [this](const SelectionEvent& event) { showCardList(event); });
    bind(MenuID::ShowCardDetail, [this](const SelectionEvent& event) { showCardTable(event); });

    // コマンドは、まず具体的な選択ペインへ渡す。 refMenu は
    // 、専用の表示のみのコマンドのための互換用代替経路として残す。
    const std::array forwarded = {
        MenuID::Undo, MenuID::Redo, MenuID::Cut, MenuID::Copy, MenuID::Paste,
        MenuID::Delete, MenuID::Clone, MenuID::SelectAll, MenuID::Comment,
        MenuID::ToScript, MenuID::ToScript1Content, MenuID::ToScriptAll,
        MenuID::Up, MenuID::Down,
        MenuID::SelectConnectedResource, MenuID::FindID, MenuID::EditScene,
        MenuID::EditSceneDup, MenuID::EditEvent, MenuID::EditEventDup,
        MenuID::EditProp, MenuID::ShowProp, MenuID::Cut1Content,
        MenuID::Copy1Content, MenuID::Delete1Content, MenuID::PasteInsert,
        MenuID::SetStartArea, MenuID::CopyVariablePath, MenuID::SwapToParent,
        MenuID::SwapToChild, MenuID::Import, MenuID::OpenHand, MenuID::AddHand,
        MenuID::RemoveRef, MenuID::EditEventAtTimeOfUsing, MenuID::Hold,
        MenuID::CopyFilePath, MenuID::ChangeVH, MenuID::ReNumbering,
        MenuID::EventToPackage, MenuID::StartToPackage, MenuID::WrapTree,
        MenuID::CopyAll, MenuID::AddItem, MenuID::DelItem,
        MenuID::ConvertCouponType, MenuID::ConvertCouponTypeNormal,
        MenuID::ConvertCouponTypeHide, MenuID::ConvertCouponTypeSystem,
        MenuID::ConvertCouponTypeDur, MenuID::ConvertCouponTypeDurBattle,
        MenuID::CopyTypeConvertedCoupon, MenuID::CopyTypeConvertedCouponNormal,
        MenuID::CopyTypeConvertedCouponHide, MenuID::CopyTypeConvertedCouponSystem,
        MenuID::CopyTypeConvertedCouponDur, MenuID::CopyTypeConvertedCouponDurBattle,
        MenuID::AddInitialCoupons, MenuID::ReverseSignOfValues, MenuID::Reverse,
        MenuID::OverDialog, MenuID::UnderDialog, MenuID::CreateDialog,
        MenuID::DeleteDialog, MenuID::CopyToAllDialogs, MenuID::CopyToUpperDialogs,
        MenuID::CopyToLowerDialogs, MenuID::ShowParty, MenuID::ShowMsg,
        MenuID::ShowRefCards, MenuID::FixedCards, MenuID::FixedCells,
        MenuID::FixedBackground, MenuID::ShowGrid, MenuID::ShowEnemyCardProp,
        MenuID::ShowCard, MenuID::ShowBack, MenuID::NewMenuCard,
        MenuID::NewEnemyCard, MenuID::NewBack, MenuID::NewTextCell,
        MenuID::NewColorCell, MenuID::NewPCCell, MenuID::AutoArrange,
        MenuID::ManualArrange, MenuID::PossibleToRunAway, MenuID::SortWithPosition,
        MenuID::Mask, MenuID::Escape, MenuID::ChangePos, MenuID::PosTop,
        MenuID::PosBottom, MenuID::PosLeft, MenuID::PosRight, MenuID::PosEven,
        MenuID::NearTop, MenuID::NearBottom, MenuID::NearLeft, MenuID::NearRight,
        MenuID::NearCenterH, MenuID::NearCenterV, MenuID::NearCenter,
        MenuID::ScaleMin, MenuID::ScaleMiddle, MenuID::ScaleMax, MenuID::ScaleBig,
        MenuID::ScaleSmall, MenuID::ExpandBack, MenuID::CopyColor1ToColor2,
        MenuID::CopyColor2ToColor1, MenuID::ExchangeColors, MenuID::StopBGM,
        MenuID::PlayBGM, MenuID::NewEvent, MenuID::NewEventWithDialog,
        MenuID::KeyCodeTiming, MenuID::KeyCodeTimingUse,
        MenuID::KeyCodeTimingSuccess, MenuID::KeyCodeTimingFailure,
        MenuID::KeyCodeTimingHasNot, MenuID::CopyTimingConvertedKeyCode,
        MenuID::CopyTimingConvertedKeyCodeUse,
        MenuID::CopyTimingConvertedKeyCodeSuccess,
        MenuID::CopyTimingConvertedKeyCodeFailure,
        MenuID::CopyTimingConvertedKeyCodeHasNot, MenuID::AddKeyCodesByFeatures,
        MenuID::KeyCodeCond, MenuID::KeyCodeCondOr, MenuID::KeyCodeCondAnd,
        MenuID::AddRangeOfRound, MenuID::PlaySE, MenuID::StopSE, MenuID::PutQuick,
        MenuID::PutSelect, MenuID::PutContinue, MenuID::EvTemplates,
        MenuID::EvTemplatesOfScenario, MenuID::EditSelection, MenuID::Expand,
        MenuID::Collapse, MenuID::SelectCurrentEvent, MenuID::ResetValues,
        MenuID::ResetValuesAll, MenuID::CopyAsText, MenuID::OpenAtView,
        MenuID::OpenAtTableView, MenuID::OpenAtVarView, MenuID::OpenAtCardView,
        MenuID::OpenAtFileView, MenuID::OpenAtEventView};
    // D版の menuActionDlg と同様、最後にフォーカスのあったテキスト編集欄
    // のメニューで有効な項目を優先する。 それ以外は選択ペイン（_tlp）が処理する。
    // 主ウィンドウの IncSearch も同じ振り分け処理に参加し、経路を揃える。
    for (const auto id : forwarded)
        bind(id, [this, id, fallback](const SelectionEvent& event) {
            if (doTextMenu(id)) return;
            if (!dispatchSelectedMenu(id, event)) fallback(event, id);
        }, [this, id] { return canDoTextMenu(id) || canDispatchSelectedMenu(id); });

    menu_checked_[MenuID::ShowCardProp] = [this] {
        const auto selected = selectedCardWindow();
        if (selected && selected != cast_window_ && selected != skill_window_ &&
            selected != item_window_ && selected != beast_window_ &&
            selected != info_window_)
            return selected->isViewLife();
        return prop_ && valueOf(prop_->var().cardLife);
    };
    menu_checked_[MenuID::ShowCardDetail] = [this] {
        const auto selected = selectedCardWindow();
        if (selected && selected != cast_window_ && selected != skill_window_ &&
            selected != item_window_ && selected != beast_window_ &&
            selected != info_window_)
            return selected->isViewTable();
        return prop_ && valueOf(prop_->var().cardDetails);
    };
    menu_checked_[MenuID::ShowCardImage] = [this] {
        const auto selected = selectedCardWindow();
        if (selected && selected != cast_window_ && selected != skill_window_ &&
            selected != item_window_ && selected != beast_window_ &&
            selected != info_window_)
            return selected->isViewList();
        return prop_ && !valueOf(prop_->var().cardLife) && !valueOf(prop_->var().cardDetails);
    };
    // D版の setupTLP と同様、シーンの表示・配置コマンド
    // にチェック状態の取得処理を用意する。 メニューと独自ツールバ
    // ーのチェックも、実行処理と同じ選択ビューの登録経路を使う。
    const std::array scene_checked_ids = {
        MenuID::ShowParty, MenuID::ShowCard, MenuID::ShowBack,
        MenuID::AutoArrange, MenuID::ManualArrange, MenuID::FixedCards,
        MenuID::FixedCells, MenuID::FixedBackground, MenuID::ShowGrid,
        MenuID::PossibleToRunAway};
    for (const auto id : scene_checked_ids)
        menu_checked_[id] = [this, id] {
            const auto registry = selectedSceneMenuRegistry();
            return registry && registry->checked(id);
        };

    const auto addItem = [this](const MenuPtr& menu, MenuID id,
                                int style = dmenu::MenuPush) {
        if (!menu) return dmenu::MenuItemRef{};
        const auto item = menu->append(
            menuText(id), menuImage(id), style,
            dmenu::MenuData{id, {}, [this, id] { return canDoMenu(id); }},
            [this, id](SelectionEvent& event) { raiseEvent(id, event); });
        menu_items_[id] = item;
        if (const auto checked = menu_checked_.find(id); checked != menu_checked_.end())
            item.setChecked(checked->second());
        return item;
    };
    const auto addSep = [](const MenuPtr& menu) {
        if (menu) (void)menu->appendSeparator();
    };
    const auto addCascade = [this](const MenuPtr& parent, MenuID id) -> MenuPtr {
        if (!parent) return {};
        auto child = std::make_shared<dmenu::Menu>(true);
        const auto item = parent->append(menuText(id), menuImage(id),
                                         dmenu::MenuCascade, dmenu::MenuData{id});
        // setSubmenu は親への接続も設定し、子ツリー全体を lib::MenuItem::children
        // に同期する。 Entry のメンバーに直接代入すると
        // 、ネイティブメニューでは最上位の末端項目のままになる。
        parent->setSubmenu(item, child);
        menu_items_[id] = item;
        return child;
    };

    (void)addCascade(menu_bar_, MenuID::File);
    createFileMenu();
    if (menu_file_) {
        menu_file_->on_command = [menu_file = menu_file_](std::string_view command) {
            menu_file->dispatch(command, SelectionEvent{});
        };
    }

    const auto edit = addCascade(menu_bar_, MenuID::Edit);
    if (edit) {
        addItem(edit, MenuID::Undo); addItem(edit, MenuID::Redo); addSep(edit);
        addItem(edit, MenuID::Cut); addItem(edit, MenuID::Copy);
        addItem(edit, MenuID::Paste); addItem(edit, MenuID::Delete); addSep(edit);
        addItem(edit, MenuID::Clone); addSep(edit); addItem(edit, MenuID::SelectAll);
        addSep(edit); addItem(edit, MenuID::Comment); addSep(edit);
        addItem(edit, MenuID::ToScript); addItem(edit, MenuID::ToScript1Content);
        addItem(edit, MenuID::ToScriptAll); addSep(edit);
        addItem(edit, MenuID::Up); addItem(edit, MenuID::Down); addSep(edit);
        addItem(edit, MenuID::SelectConnectedResource); addSep(edit);
        addItem(edit, MenuID::Find); addItem(edit, MenuID::FindID); addSep(edit);
        addItem(edit, MenuID::ReNumberingAll); addSep(edit); addItem(edit, MenuID::ToXMLText);
        addSep(edit); addItem(edit, MenuID::NewDir); addSep(edit); addItem(edit, MenuID::DelNotUsedFile);
    }

    const auto view = addCascade(menu_bar_, MenuID::View);
    if (view) {
        addItem(view, MenuID::TableView); addItem(view, MenuID::VarView); addSep(view);
        addItem(view, MenuID::CastView); addItem(view, MenuID::SkillView);
        addItem(view, MenuID::ItemView); addItem(view, MenuID::BeastView);
        addItem(view, MenuID::InfoView); addSep(view); addItem(view, MenuID::FileView);
        addSep(view); addItem(view, MenuID::CouponView); addItem(view, MenuID::GossipView);
        addItem(view, MenuID::CompleteStampView); addItem(view, MenuID::KeyCodeView);
        addItem(view, MenuID::CellNameView);
        if (!all_mode) addItem(view, MenuID::CardGroupView);
        addSep(view); addItem(view, MenuID::Refresh);
        if (!prop_ || !valueOf(prop_->var().showDuplicateViewInToolBar)) {
            addSep(view); addItem(view, MenuID::EditSceneDup); addItem(view, MenuID::EditEventDup);
        }
        addSep(view); addItem(view, MenuID::ChangeVH);
    }

    const auto table = addCascade(menu_bar_, MenuID::Table);
    if (table) {
        addItem(table, MenuID::EditSummary); addSep(table); addItem(table, MenuID::EditScene);
        addItem(table, MenuID::EditEvent); addSep(table); addItem(table, MenuID::NewAreaDir);
        addItem(table, MenuID::NewArea); addItem(table, MenuID::NewBattle);
        addItem(table, MenuID::NewPackage);
    }
    const auto variable = addCascade(menu_bar_, MenuID::Variable);
    if (variable) {
        addItem(variable, MenuID::NewFlagDir); addItem(variable, MenuID::NewFlag);
        addItem(variable, MenuID::NewStep); addItem(variable, MenuID::NewVariant);
    }
    const auto card = addCascade(menu_bar_, MenuID::Card);
    if (card) {
        addItem(card, MenuID::ShowCardProp, dmenu::MenuRadio);
        addItem(card, MenuID::ShowCardImage, dmenu::MenuRadio);
        addItem(card, MenuID::ShowCardDetail, dmenu::MenuRadio); addSep(card);
        addItem(card, MenuID::NewCast); addItem(card, MenuID::NewSkill);
        addItem(card, MenuID::NewItem); addItem(card, MenuID::NewBeast);
        addItem(card, MenuID::NewInfo);
    }
    const auto tool = addCascade(menu_bar_, MenuID::Tool);
    if (tool) {
        if (!all_mode) {
            const auto engineItem = tool->append(
                menuText(MenuID::ExecEngine), menuImage(MenuID::ExecEngine),
                dmenu::MenuCascade,
                dmenu::MenuData{MenuID::ExecEngine, {}, [this] {
                    return canExecEngine() ||
                           (prop_ && !valueOf(prop_->var().classicEngines).empty());
                }});
            engineItem.native().radio = false;
            tool->setSubmenu(engineItem, menu_exec_engine_);
            menu_items_[MenuID::ExecEngine] = engineItem;
            const auto partyItem = tool->append(
                menuText(MenuID::ExecEngineWithParty),
                menuImage(MenuID::ExecEngineWithParty), dmenu::MenuCascade,
                dmenu::MenuData{MenuID::ExecEngineWithParty, {},
                    [this] { return canExecEngineWithPartyMenu(); }});
            tool->setSubmenu(partyItem, menu_exec_party_);
            menu_items_[MenuID::ExecEngineWithParty] = partyItem;
            if (showScenarioFolderCommand()) {
                addSep(tool);
                addItem(tool, MenuID::OpenDir);
            }
            addSep(tool);
            const auto outerItem = tool->append(
                menuText(MenuID::OuterTools), menuImage(MenuID::OuterTools),
                dmenu::MenuCascade,
                dmenu::MenuData{MenuID::OuterTools, {}, [this] {
                    return canDoMenu(MenuID::OuterTools);
                }});
            tool->setSubmenu(outerItem, menu_outer_tools_);
            menu_items_[MenuID::OuterTools] = outerItem;
            addSep(tool);
        }
        const auto importItem = tool->append(menuText(MenuID::OpenImportSource),
                                             menuImage(MenuID::OpenImportSource), dmenu::MenuCascade,
                                             dmenu::MenuData{MenuID::OpenImportSource, {}, [this] {
                                                 return canAddScenario() ||
                                                        canEditImportHistory();
                                             }});
        menu_items_[MenuID::OpenImportSource] = importItem;
        menu_open_import_ = std::make_shared<dmenu::Menu>(true);
        tool->setSubmenu(importItem, menu_open_import_);
        if (!all_mode) {
            addSep(tool);
            addItem(tool, MenuID::CustomizeToolBar);
            addSep(tool);
            addItem(tool, MenuID::OpenBackupDir);
        }
        addSep(tool);
        addItem(tool, MenuID::Settings);
    }
    if (!all_mode) {
        const auto help = addCascade(menu_bar_, MenuID::Help);
        if (help) addItem(help, MenuID::VersionInfo);
    }

    createImportHistoryMenu(menu_open_import_, true);
    createMainToolBar(false);
    if (all_mode) {
        // ALL には、単独版の下部ステータス部品を残さない。
        status_bar_.reset();
    } else if (!status_bar_) {
        status_bar_ = std::make_shared<lib::StatusBar>();
    }
    composeRoot();
}

void MainWindow::configureMainDockLayout() {
    if (!docking_folder_) return;
    // 主ドックの仕切り位置は、ユーザー・モデルが決める配
    // 置であり、選択シーンから決めない。 エリアと戦闘
    // の自然な最小高さは異なるため、毎回の配置に取り込
    // むと、 タブを選ぶたびに上下の枠が動いてしまう。
    const auto stabilize_splits = [&](const auto& self, const lib::WidgetPtr& widget) -> void {
        if (!widget) return;
        if (const auto split = std::dynamic_pointer_cast<lib::SplitView>(widget))
            split->constrain_to_child_minimums = false;
        if (const auto container = std::dynamic_pointer_cast<lib::Container>(widget))
            for (const auto& child : container->children()) self(self, child);
    };
    stabilize_splits(stabilize_splits, docking_folder_->component());
    if (lib::editor_window_host_options().all_mode) {
        const auto host_options = lib::editor_window_host_options();
        if (!web_all_last_canvas_width_ &&
            host_options.hosted_canvas_size.width > 0.0F)
            web_all_last_canvas_width_ = host_options.hosted_canvas_size.width;
        if (!web_all_last_canvas_height_ &&
            host_options.hosted_canvas_size.height > 0.0F)
            web_all_last_canvas_height_ = host_options.hosted_canvas_size.height;
        const auto data_pane = docking_folder_->pane("data");
        const auto cast_pane = docking_folder_->pane("data_2");
        const auto work_pane = docking_folder_->pane("work");
        const auto upper_data = findWebAllUpperDataBranch(
            docking_folder_->component(), data_pane, cast_pane, work_pane);
        if (upper_data.split && upper_data.widget) {
            const auto& children = upper_data.split->children();
            const auto& weights = upper_data.split->weights;
            float total_weight = 0.0F;
            float upper_weight = 0.0F;
            for (std::size_t index = 0; index < children.size(); ++index) {
                const float weight = index < weights.size()
                    ? std::max(0.0F, weights[index])
                    : std::max(0.0F, children[index]->layout_item().grow);
                total_weight += weight;
                if (index == upper_data.index) upper_weight = weight;
            }
            if (total_weight > 0.0F && upper_weight > 0.0F) {
                const float current_ratio = upper_weight / total_weight;
                if (!web_all_upper_pane_baseline_ratio_)
                    web_all_upper_pane_baseline_ratio_ = current_ratio;
                // コンパクトな選択行は約30pxを占める。保存済みの仕切
                // り比率も含め、 高さ710pxのエディタでの上側のデー
                // タ・キャスト領域の配分を維持する。 ALL はドック
                // を再構築せずにサイズ変更できるため、構築時に下限を設
                // 定し、 710から680への変化分は下側で吸収する。
                const float baseline_canvas_height = 710.0F - 30.0F;
                const float upper_data_baseline = std::max(
                    0.0F, std::ceil(baseline_canvas_height *
                                    *web_all_upper_pane_baseline_ratio_));
                auto item = upper_data.widget->layout_item();
                // 実測した高さ710pxの配置を基準として
                // 維持する。 分割の既存の比例配分により、
                // 残りの画素は上側の枝より下に割り当てる。
                item.minimum.height = upper_data_baseline;
                upper_data.widget->set_layout_item(item);
            }
        }
    }
    // 仕切りの安定性は単独版と ALL で共通にする。 主ドックの全
    // タブ見出しをコンパクトな選択欄に置き換えるのは ALL だけ。
    if (!lib::editor_window_host_options().all_mode) return;
    // ALL仕様差分: Dの個別タブ列は表示しない。固定ペインだけでなく、
    // Area/Battle/Package/Eventなど後から開いた各ビューも上段の選択欄で
    // 切り替えるため、全ペインを同じ規則で処理する。
    for (const auto& pane_key : docking_folder_->paneKeys()) {
        const auto controls = docking_folder_->controlKeys(pane_key);
        if (controls.empty()) continue;
        if (const auto tabs = docking_folder_->tabView(controls.front()))
            tabs->header_visible = false;
    }
}

void MainWindow::hostedCanvasResized() {
    if (!lib::editor_window_host_options().all_mode) return;
    const float next_canvas_width =
        lib::editor_window_host_options().hosted_canvas_size.width;
    const float next_canvas_height =
        lib::editor_window_host_options().hosted_canvas_size.height;
    const bool width_changed = web_all_last_canvas_width_ && next_canvas_width > 0.0F &&
        next_canvas_width != *web_all_last_canvas_width_;
    const bool height_changed = web_all_last_canvas_height_ && next_canvas_height > 0.0F &&
        next_canvas_height != *web_all_last_canvas_height_;
    if ((width_changed || height_changed) && docking_folder_) {
        const auto data_pane = docking_folder_->pane("data");
        const auto cast_pane = docking_folder_->pane("data_2");
        const auto work_pane = docking_folder_->pane("work");
        const auto upper_data = findWebAllUpperDataBranch(
            docking_folder_->component(), data_pane, cast_pane, work_pane);
        if (upper_data.split && upper_data.widget &&
            upper_data.split->children().size() == 2U) {
            const auto& children = upper_data.split->children();
            float current_content_height{};
            for (const auto& child : children)
                if (child) current_content_height += child->bounds().height;
            const float next_content_height = std::max(
                0.0F, current_content_height +
                          (height_changed ? next_canvas_height - *web_all_last_canvas_height_ : 0.0F));
            const float current_upper_height = upper_data.widget->bounds().height;
            if (current_content_height > 0.0F && next_content_height > 0.0F) {
                const float next_upper_height = std::clamp(
                    current_upper_height, 0.0F, next_content_height);
                upper_data.split->weights = {
                    next_upper_height,
                    std::max(0.0F, next_content_height - next_upper_height)};
            }
        }
    }
    if (next_canvas_width > 0.0F) web_all_last_canvas_width_ = next_canvas_width;
    if (next_canvas_height > 0.0F) web_all_last_canvas_height_ = next_canvas_height;
    configureMainDockLayout();
    if (services_.gui && window_) services_.gui->request_redraw(window_);
}

// ALL仕様: Dでは別ビューであるシーン/イベントを、選択欄では同じ原本として扱う。
static String compactSceneIdentity(String key) {
    const bool area = StringView(key).starts_with("area.scene:") || StringView(key).starts_with("area.event:");
    const bool battle = StringView(key).starts_with("battle.scene:") || StringView(key).starts_with("battle.event:");
    if (!area && !battle) return key;
    key = (area ? "area:" : "battle:") + key.substr(key.find(':') + 1U);
    // 所有する Summary を表す #source は残し、その後の明示
    // 的な重複ビュー接尾辞だけを除く。 取り込み元のアドレスも数値だが識別
    // 情報の一部なので、除くと取り込み元と編集中のデータが混ざってしまう。
    if (const auto source = key.find("#source"); source != String::npos) {
        if (const auto suffix = key.find('#', source + 7U); suffix != String::npos)
            key.erase(suffix);
    } else if (const auto suffix = key.rfind('#');
               suffix != String::npos && suffix + 1U < key.size() &&
               std::all_of(key.begin() + suffix + 1U, key.end(),
                           [](char c) { return c >= '0' && c <= '9'; })) {
        key.erase(suffix);
    }
    return key;
}

void MainWindow::refreshAllCompactSelectors(const StringView key) {
    if (!lib::editor_window_host_options().all_mode) return;
    const String selected = key.empty() ? dock_.selected : String(key);
    const auto select_key = [&selected](const std::shared_ptr<lib::ComboBox>& combo,
                                        const std::vector<String>& keys) {
        if (!combo) return;
        const auto found = std::find(keys.begin(), keys.end(), selected);
        if (found != keys.end())
            combo->selected_index = static_cast<std::size_t>(found - keys.begin());
    };
    select_key(all_left_selector_, all_left_selector_keys_);
    select_key(all_right_selector_, all_right_selector_keys_);
    if (all_scene_selector_ && docking_folder_) {
        String previous;
        if (all_scene_selector_->selected_index &&
            *all_scene_selector_->selected_index < all_scene_selector_keys_.size())
            previous = all_scene_selector_keys_[*all_scene_selector_->selected_index];
        std::vector<String> keys;
        std::vector<lib::ChoiceItem> items;
        std::map<String, std::size_t> identities;
        std::map<String, String> aliases;
        String current;
        for (const auto& pane : docking_folder_->paneKeys()) {
            if (!StringView(pane).starts_with("work")) continue;
            if (current.empty()) current = docking_folder_->selectedCtrl(pane);
            for (const auto& control : docking_folder_->controlKeys(pane)) {
                const auto* tab = docking_folder_->tab(control);
                const auto identity = compactSceneIdentity(tab ? tab->restoreKey : control);
                aliases[control] = identity;
                if (const auto found = identities.find(identity); found != identities.end()) {
                    // 行の主操作はシーンを優先し、イベントボタンは独立したままにする。
                    if (tab && (StringView(tab->restoreKey).starts_with("area.scene:") ||
                                StringView(tab->restoreKey).starts_with("battle.scene:"))) {
                        keys[found->second] = control;
                        items[found->second] = {docking_folder_->getTabText(control), control,
                                                tab->image, true};
                    }
                    continue;
                }
                identities.emplace(identity, keys.size());
                keys.push_back(control);
                items.push_back({docking_folder_->getTabText(control), control,
                                 tab ? tab->image : lib::ResourceId{}, true});
            }
        }
        std::optional<std::size_t> index;
        for (const auto& candidate : {selected, previous, current}) {
            const auto alias = aliases.find(candidate);
            if (alias == aliases.end()) continue;
            index = identities.at(alias->second);
            break;
        }
        if (items.empty()) items.push_back({"シーン", "", 0, false});
        all_scene_selector_keys_ = std::move(keys);
        (void)lib::replace_combo_items(all_scene_selector_, std::move(items),
                                      index.value_or(0U));
        all_scene_selector_->set_enabled(!all_scene_selector_keys_.empty());
        configureMainDockLayout();
        if (services_.gui) services_.gui->request_redraw(all_scene_selector_);
    }
    if (all_side_selector_ && docking_folder_) {
        std::vector<String> keys;
        std::vector<lib::ChoiceItem> items;
        const auto pane_keys = docking_folder_->paneKeys();
        String current;
        for (const auto& pane : pane_keys) {
            if (!StringView(pane).starts_with("side")) continue;
            const auto selected_control = docking_folder_->selectedCtrl(pane);
            if (current.empty() && !selected_control.empty()) current = selected_control;
            for (const auto& control : docking_folder_->controlKeys(pane)) {
                if (std::find(keys.begin(), keys.end(), control) != keys.end()) continue;
                String title = docking_folder_->getTabText(control);
                if (title.empty()) title = control;
                keys.push_back(control);
                items.push_back({std::move(title), control});
            }
        }
        if (const auto selected_key = std::find(keys.begin(), keys.end(), selected);
            selected_key != keys.end())
            current = *selected_key;
        std::optional<std::size_t> index;
        if (!current.empty()) {
            const auto found = std::find(keys.begin(), keys.end(), current);
            if (found != keys.end())
                index = static_cast<std::size_t>(found - keys.begin());
        }
        if (!index && !keys.empty()) index = 0U;
        if (items.empty()) items.push_back({"外部シナリオ・手札", ""});
        all_side_selector_keys_ = std::move(keys);
        (void)lib::replace_combo_items(all_side_selector_, std::move(items),
                                      index.value_or(0U));
        all_side_selector_->set_enabled(!all_side_selector_keys_.empty());
        if (services_.gui) services_.gui->request_redraw(all_side_selector_);
    }
}

void MainWindow::refreshAllViewToolbar(const StringView key) {
    if (!lib::editor_window_host_options().all_mode || !all_view_toolbar_host_)
        return;
    const String selected = key.empty() ? dock_.selected : String(key);
    WidgetPtr next;
    String next_key;
    const auto create_for = [this, &next, &next_key](const StringView candidate) {
        if (candidate.empty()) return false;
        const auto found = all_view_toolbar_factories_.find(String(candidate));
        if (found == all_view_toolbar_factories_.end() || !found->second) return false;
        next = found->second();
        if (!next) {
            all_view_toolbar_factories_.erase(found);
            return false;
        }
        next_key = String(candidate);
        return true;
    };
    // 選択中のシーン・イベント自身を優先する。データ・カード・サイドペインでは、 現
    // 在表示中のシーンツールバーを維持するか、選択タブから同じコンパクトなシーンの識
    // 別情報を求める。 それでも見つからない場合だけ、最初の開いているシーンを使う。
    if (!create_for(selected) && docking_folder_) {
        const auto* selected_tab = docking_folder_->tab(selected);
        const auto selected_identity = selected_tab
            ? compactSceneIdentity(selected_tab->restoreKey)
            : compactSceneIdentity(selected);
        if (!selected_identity.empty()) {
            for (const auto& [candidate, factory] : all_view_toolbar_factories_) {
                const auto* tab = docking_folder_->tab(candidate);
                if (tab && compactSceneIdentity(tab->restoreKey) == selected_identity &&
                    create_for(candidate))
                    break;
            }
        }
    }
    if (next_key.empty() && docking_folder_ &&
        !all_view_toolbar_key_.empty() &&
        docking_folder_->tab(all_view_toolbar_key_))
        (void)create_for(all_view_toolbar_key_);
    if (next_key.empty()) {
        for (const auto& candidate : all_scene_selector_keys_)
            if (create_for(candidate)) break;
    }
    if (next != all_view_toolbar_active_) {
        if (all_view_toolbar_active_) all_view_toolbar_host_->remove(all_view_toolbar_active_);
        all_view_toolbar_active_ = next;
        if (all_view_toolbar_active_) all_view_toolbar_host_->add(all_view_toolbar_active_);
    }
    all_view_toolbar_key_ = next_key;
    all_view_toolbar_host_->set_visible(static_cast<bool>(all_view_toolbar_active_));
    if (services_.gui) services_.gui->request_redraw(all_view_toolbar_host_);
}

void MainWindow::trackFocusForMenus(const lib::WidgetPtr& focused) {
    if (!focused) return;
    // D版の DockingFolder.FocusL と同様、ドッキングされたペイン内のコントロールにフォーカスが移ると、そのペインを選択する。
    if (docking_folder_) docking_folder_->focusIn(focused);
    // ALL の側面の操作欄は、D版の主 ToolBar に相当する。SWTではツールバーのク
    // リックでフォーカスを奪わないため、 lastFocusEditor もリセットしない。
    if (all_compact_navigation_ &&
        lib::contains_widget(all_compact_navigation_, focused.get()))
        return;
    // D版の Commons.refreshToolBarImpl と同様、主ウィンドウの Text/Combo のう
    // ち、 テキストメニューを持つものだけを lastFocusEditor とし、それ以外のフォーカスでは解除する。
    const bool in_main = services_.gui && window_ &&
                         services_.gui->window_owner(focused) == window_;
    if (in_main && dmenu::textMenuOf(focused)) last_focus_editor_ = focused;
    else last_focus_editor_.reset();
    refreshAllHistory();
}

dmenu::TextMenuModifyPtr MainWindow::lastFocusTextMenu() const {
    const auto editor = last_focus_editor_.lock();
    // D版の Commons::lastFocusEditor と同様、有効な Control を
    // 保持する。 SWTの表示・非表示では、そのコントロールやネイティブメニューは破棄されない。
    if (!editor) return {};
    if (services_.gui && window_ && services_.gui->window_owner(editor) != window_)
        return {};
    return dmenu::textMenuOf(editor);
}

bool MainWindow::canDoTextMenu(const MenuID id) const {
    const auto menu = lastFocusTextMenu();
    if (!menu) return false;
    switch (id) {
    case MenuID::Undo: return menu->canUndo();
    case MenuID::Redo: return menu->canRedo();
    case MenuID::Cut: return menu->canCut();
    case MenuID::Copy: return menu->canCopy();
    case MenuID::Paste: return menu->canPaste();
    case MenuID::Delete: return menu->canDelete();
    case MenuID::SelectAll: return true;
    default: return false;
    }
}

bool MainWindow::doTextMenu(const MenuID id) {
    if (!canDoTextMenu(id)) return false;
    const auto menu = lastFocusTextMenu();
    switch (id) {
    case MenuID::Undo: (void)menu->undo(); break;
    case MenuID::Redo: (void)menu->redo(); break;
    case MenuID::Cut: menu->cut(); break;
    case MenuID::Copy: menu->copy(); break;
    case MenuID::Paste: menu->paste(); break;
    case MenuID::Delete: menu->erase(); break;
    case MenuID::SelectAll: menu->selectAll(); break;
    default: return false;
    }
    if (comm_) comm_->refreshToolBar();
    return true;
}

void MainWindow::refreshAllHistory() {
    if (!lib::editor_window_host_options().all_mode ||
        !all_undo_button_ || !all_redo_button_)
        return;
    // ALL仕様: Undo/Redoボタンは常に有効。押下時に編集メニューの
    // 元に戻す/やり直しと同じ経路で実行し、対象が無ければ何もしない。
    all_undo_button_->set_enabled(true);
    all_redo_button_->set_enabled(true);
    if (services_.gui) {
        services_.gui->request_redraw(all_undo_button_);
        services_.gui->request_redraw(all_redo_button_);
    }
}

std::shared_ptr<lib::Panel> MainWindow::createAllCompactNavigation() {
    auto row = std::make_shared<lib::Panel>();
    row->accessibility_name = "ALL left navigation rail";
    auto layout = std::make_shared<lib::LinearLayout>(lib::Orientation::Horizontal);
    const bool all_mode = lib::editor_window_host_options().all_mode;
    // 余白は上・右・下・左の順に指定する。ALL は水平方向の
    // 余白を意図的に設けず、 115pxの操作欄自体をボタン領
    // 域とする。選択欄とエディタのドックの間に1～2pxの帯
    // を挟まない。 単独版は設計上の2pxの余白を維持する。
    layout->padding = {all_mode ? 0.0F : 2.0F, all_mode ? 0.0F : 2.0F,
                       all_mode ? 0.0F : 2.0F,
                       all_mode ? 0.0F : 2.0F};
    layout->orientation = lib::Orientation::Vertical;
    layout->spacing = 2.0F;
    layout->cross_axis = lib::Align::Stretch;
    row->set_layout(layout);

    const auto add_selector = [&row](const std::shared_ptr<lib::ComboBox>& combo) {
        // ALL の選択欄は元のタッチ領域を維持する。 左側
        // のドロップダウンが小さくなりすぎないよう、ポップ
        // アップ行の倍率と設計上のコントロール高さを保つ。
        combo->row_height_scale = 1.25F;
        lib::LayoutItem item;
        // 六つのドロップダウン（六つ目は最小化したダイアログの一覧）を、従来の五つと同じ
        // 高さに収める。 5×40 + 4×2 = 208 = 6×33 + 5×2。
        item.minimum = {105.0F, 30.0F};
        item.preferred = {105.0F, 33.0F};
        // 固定の操作欄のセルを埋める。選
        // 択肢によって自然幅が大きくなっ
        // ても、コントロールをセル幅へ伸
        // ばし、ドック横の空白帯を防ぐ。
        item.horizontal = lib::Align::Stretch;
        combo->set_layout_item(item);
        row->add(combo);
    };

    all_menu_selector_ = std::make_shared<lib::ComboBox>();
    all_menu_selector_->accessibility_name = "メニュー";
    // メニューの分類、空の区切り行、上書き保存の順に並べる。
    all_menu_selector_->visible_item_count = 11U;
    all_menu_selector_->items.push_back({"メニュー", ""});
    if (menu_bar_) {
        for (const auto& item : menu_bar_->items)
            all_menu_selector_->items.push_back(
                {lib::display_text_without_mnemonic(item.text), item.command,
                 item.icon, item.enabled});
    }
    // ALL ではツールの下に無効な空白行を置き、その次に上書き保存を置く。
    all_menu_selector_->items.push_back({"", "", {}, false});
    {
        // 見出しだけを使い、アクセスキーとタブ以降のショートカット表記を除く。
        String save_text = prop_->requiredMsgs().menuText(MenuID::Save);
        if (const auto tab = save_text.find('\t'); tab != String::npos)
            save_text.resize(tab);
        all_menu_selector_->items.push_back(
            {lib::display_text_without_mnemonic(save_text), "save-overwrite"});
    }
    all_menu_selector_->selected_index = 0U;
    all_menu_selector_->on_selection_changed = [this](const std::optional<std::size_t> selected) {
        if (selected && all_menu_selector_ && *selected < all_menu_selector_->items.size() &&
            all_menu_selector_->items[*selected].value == "save-overwrite") {
            all_menu_selector_->selected_index = 0U;
            if (services_.gui) services_.gui->request_redraw(all_menu_selector_);
            if (canSaveOverwrite()) saveScenario();
            return;
        }
        if (!selected || *selected == 0U || !menu_bar_ ||
            *selected > menu_bar_->items.size()) {
            if (all_menu_selector_) all_menu_selector_->selected_index = 0U;
            return;
        }
        const std::size_t menu_index = *selected - 1U;
        const auto* entry = menu_bar_->entry(menu_index);
        const auto menu = entry ? entry->submenu : MenuPtr{};
        if (menu && services_.gui && all_menu_selector_) {
            menu->refreshItems();
            const auto bounds = all_menu_selector_->bounds();
            services_.gui->show_context_menu(menu, all_menu_selector_,
                                              {bounds.x, bounds.y + bounds.height});
        }
        // このコントロールはメニューを開くためのもので、状態を保持する選択欄で
        // はない。 見出しを戻すことで、同じ分類を続けて二度開けるようにする。
        if (all_menu_selector_) all_menu_selector_->selected_index = 0U;
        if (services_.gui && all_menu_selector_)
            services_.gui->request_redraw(all_menu_selector_);
    };
    add_selector(all_menu_selector_);

    const auto create_pane_selector =
        [this, &add_selector](const StringView pane_key, const StringView label,
                             std::shared_ptr<lib::ComboBox>& selector,
                             std::vector<String>& keys) {
            selector = std::make_shared<lib::ComboBox>();
            selector->accessibility_name = String(label);
            keys = docking_folder_ ? docking_folder_->controlKeys(pane_key)
                                   : std::vector<String>{};
            if (keys.empty())
                selector->items.push_back({String(label), ""});
            for (const auto& control_key : keys) {
                String title = docking_folder_->getTabText(control_key);
                if (title.empty()) title = control_key;
                selector->items.push_back({std::move(title), control_key});
            }
            if (docking_folder_) {
                const String current = docking_folder_->selectedCtrl(pane_key);
                if (const auto found = std::find(keys.begin(), keys.end(), current);
                    found != keys.end())
                    selector->selected_index =
                        static_cast<std::size_t>(found - keys.begin());
            }
            if (selector->items.size() == 1U && keys.empty())
                selector->selected_index = 0U;
            selector->set_enabled(!keys.empty());
            auto* key_list = &keys;
            selector->on_selection_changed =
                [this, key_list](const std::optional<std::size_t> selected) {
                    if (!selected || *selected >= key_list->size() || !docking_folder_)
                        return;
                    (void)docking_folder_->select((*key_list)[*selected]);
                };
            add_selector(selector);
        };

    create_pane_selector("data", "テーブル", all_left_selector_,
                         all_left_selector_keys_);
    create_pane_selector("data_2", "キャスト", all_right_selector_,
                         all_right_selector_keys_);
    // ALL ではエリア・戦闘のシーンとイベントの両方を開いて
    // いても、コンパクトな行は一つにまとめる。 行の二つの操作
    // は、イベントビューを開く・選択する操作と、 シナリオのリ
    // ソースを残したまま両方の表示を閉じる操作に対応させる。
    all_scene_selector_ = std::make_shared<lib::ComboBox>();
    all_scene_selector_->accessibility_name = "シーン";
    all_scene_selector_->popup_actions = {"イベント", "閉じる"};
    all_scene_selector_->on_selection_changed = [this](const std::optional<std::size_t> selected) {
        if (!selected || *selected >= all_scene_selector_keys_.size() || !docking_folder_)
            return;
        (void)docking_folder_->select(all_scene_selector_keys_[*selected]);
    };
    all_scene_selector_->on_popup_action = [this](String control_key,
                                                   const std::size_t action) {
        if (!docking_folder_) return;
        const auto* source = docking_folder_->tab(control_key);
        const auto identity = compactSceneIdentity(source ? source->restoreKey : control_key);
        std::vector<String> matching;
        String event_key;
        for (const auto& pane : docking_folder_->paneKeys()) {
            if (!StringView(pane).starts_with("work")) continue;
            for (const auto& candidate : docking_folder_->controlKeys(pane)) {
                const auto* tab = docking_folder_->tab(candidate);
                if (!tab || compactSceneIdentity(tab->restoreKey) != identity) continue;
                matching.push_back(candidate);
                if (StringView(tab->restoreKey).starts_with("area.event:") ||
                    StringView(tab->restoreKey).starts_with("battle.event:"))
                    event_key = candidate;
            }
        }
        if (action == 0U) {
            if (!event_key.empty()) {
                (void)docking_folder_->select(event_key);
            } else if (const auto open_event = scene_open_event_.find(control_key);
                       open_event != scene_open_event_.end() && open_event->second) {
                open_event->second();
            }
        } else if (action == 1U) {
            for (const auto& candidate : matching)
                (void)docking_folder_->close(candidate);
        }
        refreshAllCompactSelectors();
    };
    all_scene_selector_keys_.clear();
    add_selector(all_scene_selector_);
    all_side_selector_ = std::make_shared<lib::ComboBox>();
    all_side_selector_->accessibility_name = "外部シナリオ・手札";
    all_side_selector_->popup_actions = {"閉じる"};
    all_side_selector_->on_selection_changed =
        [this](const std::optional<std::size_t> selected) {
            if (!selected || *selected >= all_side_selector_keys_.size() ||
                !docking_folder_) return;
            (void)docking_folder_->select(all_side_selector_keys_[*selected]);
        };
    all_side_selector_->on_popup_action =
        [this](String control_key, const std::size_t action) {
            if (action != 0U || control_key.empty() || !docking_folder_) return;
            (void)docking_folder_->close(control_key);
            refreshAllCompactSelectors();
        };
    all_side_selector_keys_.clear();
    add_selector(all_side_selector_);
    // ALL では、最小化したダイアログを外部シナリオの
    // 下に一覧表示し、 選択すると復元して前面に出す。
    all_dialog_selector_ = std::make_shared<lib::ComboBox>();
    all_dialog_selector_->accessibility_name = "ダイアログ";
    all_dialog_selector_->on_selection_changed =
        [this](const std::optional<std::size_t> selected) {
            if (!selected || *selected == 0U ||
                *selected > all_dialog_selector_items_.size()) return;
            const auto target = all_dialog_selector_items_[*selected - 1U].lock();
            if (target && services_.gui) services_.gui->restore_window(target);
            refreshAllDialogSelector();
        };
    add_selector(all_dialog_selector_);
    if (services_.gui)
        services_.gui->on_minimized_changed =
            [this, alive = std::weak_ptr<std::atomic_bool>(lifecycle_alive_)] {
                const auto current = alive.lock();
                if (!current || !current->load(std::memory_order_acquire)) return;
                refreshAllDialogSelector();
            };
    refreshAllDialogSelector();
    const auto add_action = [](const std::shared_ptr<lib::Container>& parent,
                               String title, const std::function<void()>& action) {
        auto button = std::make_shared<lib::Button>(std::move(title));
        button->accessibility_name = button->text;
        // ホストの二つの操作は、選択欄の下の一行にまとめる。
        button->text_horizontal_padding = 2.0F;
        lib::LayoutItem item;
        item.minimum = {58.0F, 35.0F};
        item.preferred = {60.0F, 38.0F};
        item.horizontal = lib::Align::Stretch;
        button->set_layout_item(item);
        button->set_enabled(static_cast<bool>(action));
        button->on_click = action;
        parent->add(button);
    };
    // これらはホストの画面移動であり、エディタの保存・テストプレイではない。 別画面
    // に覆われている間も、ホストは MainWindow と未保存の文書を保持する。
    auto host_actions = std::make_shared<lib::Panel>();
    host_actions->accessibility_name = "ALL host actions row";
    auto host_actions_layout = std::make_shared<lib::LinearLayout>(
        lib::Orientation::Horizontal);
    host_actions_layout->spacing = 2.0F;
    host_actions_layout->cross_axis = lib::Align::Stretch;
    host_actions->set_layout(host_actions_layout);
    host_actions->set_layout_item({.horizontal = lib::Align::Stretch,
                                   .minimum = {0.0F, 35.0F},
                                   .preferred = {0.0F, 38.0F}});
    add_action(host_actions, "エンジン", context_.requestEngine);
    add_action(host_actions, "データ", context_.requestData);
    row->add(host_actions);

    auto history_gap = std::make_shared<lib::Panel>();
    history_gap->accessibility_name = "ALL history gap";
    history_gap->set_layout_item({.horizontal = lib::Align::Stretch,
                                  .minimum = {0.0F, 8.0F},
                                  .preferred = {0.0F, 8.0F}});
    row->add(history_gap);

    // ALL の取り消し・やり直しはホスト側の操作部品とする。 シー
    // ン・イベントのタブが未選択でもエンジン・データの下に置き、
    // 編集メニューと同じ選択ペインへのコマンド経路を使う。 タッチ
    // 領域は45pxで、コンパクトな操作の高さ30pxの1.5倍。
    all_history_toolbar_ = std::make_shared<lib::Panel>();
    all_history_toolbar_->accessibility_name = "ALL history toolbar";
    auto history_layout = std::make_shared<lib::LinearLayout>(
        lib::Orientation::Horizontal);
    history_layout->spacing = 5.0F;
    history_layout->cross_axis = lib::Align::Stretch;
    history_layout->padding = {};
    all_history_toolbar_->set_layout(history_layout);
    all_history_toolbar_->set_layout_item({.horizontal = lib::Align::Stretch,
                                           .minimum = {105.0F, 45.0F},
                                           .preferred = {105.0F, 45.0F}});
    const auto add_history_button = [this](const MenuID id, const char* name,
                                           const std::function<void()>& action) {
        auto button = std::make_shared<lib::Button>();
        button->accessibility_name = name;
        button->icon = menuImage(id);
        button->set_tooltip(toolText(id));
        button->text_horizontal_padding = 0.0F;
        button->set_layout_item({.horizontal = lib::Align::Stretch,
                                 .vertical = lib::Align::Stretch,
                                 .minimum = {50.0F, 45.0F},
                                 .preferred = {50.0F, 45.0F},
                                 .grow = 1.0F});
        button->on_click = [this, action] {
            if (action) action();
            refreshAllHistory();
        };
        all_history_toolbar_->add(button);
        return button;
    };
    // 編集メニューの取り消し・やり直しと同じ経路を使う（raiseEvent
    // 経由のD版の menuActionDlg に相当）。 結び付いた操作
    // が選択ペインへ振り分け、代替処理もメニューと同じにする。 取り消し・
    // やり直しができない場合は raiseEvent 自体が無視する。 ダ
    // イアログの見出しの取り消し・やり直しボタンにも、同じアイコンを使う。
    lib::set_hosted_caption_history_icons(menuImage(MenuID::Undo), menuImage(MenuID::Redo));
    all_undo_button_ = add_history_button(MenuID::Undo, "ALL Undo", [this] {
        raiseEvent(MenuID::Undo, SelectionEvent{});
    });
    all_redo_button_ = add_history_button(MenuID::Redo, "ALL Redo", [this] {
        raiseEvent(MenuID::Redo, SelectionEvent{});
    });
    row->add(all_history_toolbar_);

    // 伸縮する空白は、現在のシーン・イベントの操作欄より
    // 前に置く。 操作欄を左下へ寄せ、ビューのツールバ
    // ーがなくても、ホームを最後の最下部の部品に保つ。
    auto toolbar_spacer = std::make_shared<lib::Panel>();
    toolbar_spacer->accessibility_name = "ALL view toolbar spacer";
    toolbar_spacer->set_layout_item({.horizontal = lib::Align::Stretch,
                                     .minimum = {0.0F, 3.0F},
                                     .preferred = {0.0F, 3.0F},
                                     .grow = 1.0F});
    row->add(toolbar_spacer);

    all_view_toolbar_host_ = std::make_shared<lib::Panel>();
    all_view_toolbar_host_->accessibility_name = "ALL active view toolbar host";
    auto view_toolbar_layout = std::make_shared<lib::LinearLayout>(
        lib::Orientation::Vertical);
    view_toolbar_layout->spacing = 1.0F;
    view_toolbar_layout->padding = {};
    all_view_toolbar_host_->set_layout(view_toolbar_layout);
    all_view_toolbar_host_->set_layout_item({.horizontal = lib::Align::Stretch,
                                             .minimum = {105.0F, 0.0F},
                                             .preferred = {105.0F, 0.0F}});
    all_view_toolbar_host_->set_visible(false);
    row->add(all_view_toolbar_host_);

    // ホームと現在のシーン・イベントの操作欄を見た目で区切
    // る。 ホストの上の伸縮空白で操作群を左下に寄せつつ、
    // 最後の操作の前には意図的に大きな固定間隔を置く。
    auto home_gap = std::make_shared<lib::Panel>();
    home_gap->accessibility_name = "ALL Home gap";
    home_gap->set_layout_item({.horizontal = lib::Align::Stretch,
                               .minimum = {0.0F, 8.0F},
                               .preferred = {0.0F, 8.0F}});
    row->add(home_gap);

    auto home = std::make_shared<lib::Button>("ホーム");
    home->accessibility_name = home->text;
    home->text_horizontal_padding = 2.0F;
    home->set_layout_item({.horizontal = lib::Align::Stretch,
                           .minimum = {105.0F, 35.0F},
                           .preferred = {105.0F, 38.0F}});
    home->set_enabled(static_cast<bool>(context_.requestHome));
    home->on_click = context_.requestHome;
    row->add(home);
    refreshAllCompactSelectors();
    refreshAllHistory();
    refreshAllViewToolbar();
    all_compact_navigation_ = row;
    return row;
}

void MainWindow::rememberToolbarWraps() {
    if (!prop_ || !toolbar_host_ || lib::editor_window_host_options().all_mode) return;
    std::vector<std::int32_t> wraps;
    float previous_y = -std::numeric_limits<float>::infinity();
    const auto& bands = toolbar_host_->children();
    for (std::size_t i = 0; i < bands.size(); ++i) {
        if (!bands[i]) continue;
        const auto y = bands[i]->bounds().y;
        if (y > previous_y) {
            wraps.push_back(static_cast<std::int32_t>(i));
            previous_y = y;
        }
    }
    prop_->var().toolsWrapIndices.value = std::move(wraps);
}

void MainWindow::composeRoot(const bool preserve_toolbar_wraps) {
    if (!window_) return;
    if (preserve_toolbar_wraps) rememberToolbarWraps();

    if (tool_bar_group_order_.size() != tool_bars_.size()) {
        tool_bar_group_order_.resize(tool_bars_.size());
        std::iota(tool_bar_group_order_.begin(), tool_bar_group_order_.end(),
                  std::size_t{0});
    }

    const bool all_mode = lib::editor_window_host_options().all_mode;
    // ALL は専用の左側操作欄に選択欄を置く。単
    // 独版は従来の上から下への画面構成を維持する。
    auto root_layout = std::make_shared<lib::LinearLayout>(
        all_mode ? lib::Orientation::Horizontal : lib::Orientation::Vertical);
    root_layout->padding = {};
    root_layout->spacing = 0.0F;
    window_->set_layout(root_layout);
    window_->clear();
    toolbar_surface_.reset();

    configureMainDockLayout();
    if (menu_bar_ && !all_mode) {
        // ネイティブのタイトルバー直下に余白を設けな
        // い。 メニューの操作領域に6pxを追加す
        // ると、メニューの下に不要な隙間が生じる。
        menu_bar_->bar_padding = 0.0F;
        menu_bar_->bar_extra_height = 0.0F;
        window_->add(menu_bar_);
    }
    if (all_mode) {
        const auto navigation = createAllCompactNavigation();
        lib::LayoutItem rail_item;
        // コンパクトな操作欄は、Web・単独版の設計幅よ
        // り15px狭くする。 水平余白を除くことで115px
        // がボタンセルの固定幅となり、 縦向きAndroid
        // 画面でドックの領域を広く取れる。
        rail_item.minimum = {115.0F, 0.0F};
        rail_item.preferred = rail_item.minimum;
        // 翻訳したメニュー項目の計測で、側面の操作欄が
        // 大きい自然幅を返す場合がある。 ALL では
        // 固定幅のホスト用の列なので、指定した推奨幅で
        // 最小幅に上限を設ける。 長いラベルがドックを
        // 押しのけ、間に空白帯を作らないようにする。
        rail_item.constrain_minimum_to_preferred = true;
        rail_item.vertical = lib::Align::Stretch;
        navigation->set_layout_item(rail_item);
        window_->add(navigation);
    }

    if (!all_mode && prop_ && !valueOf(prop_->var().useCoolBar)) {
        // D版の createCoolBar と同様、このモードでは折り返す通常の横並
        // びのホストを使う。 ALL の既存の切り離されたツールバーモデルは変えない。
        toolbar_host_.reset();
        dmenu::Commons toolbar_comm;
        toolbar_comm.services = services_;
        toolbar_comm.presentation.useCoolBar = false;
        toolbar_surface_ = dmenu::createCoolBar(toolbar_comm, *window_, false,
            [this](lib::Container& host) {
                for (const auto& toolbar : tool_bars_) {
                    if (!toolbar) continue;
                    toolbar->set_layout_item({});
                    toolbar->on_pointer = {};
                    host.add(toolbar);
                }
            });
        toolbar_surface_->set_visible(prop_ && valueOf(prop_->var().showMainToolBar));
    } else {
    toolbar_host_ = std::make_shared<lib::CoolBar>();
    toolbar_surface_ = toolbar_host_;
    toolbar_host_->locked = prop_ && valueOf(prop_->var().toolsLock);
    auto toolbar_layout = std::make_shared<lib::LinearLayout>(lib::Orientation::Horizontal);
    toolbar_layout->padding = {1.0F, 2.0F, 1.0F, 2.0F};
    toolbar_layout->spacing = 2.0F;
    toolbar_layout->cross_axis = lib::Align::Stretch;
    toolbar_host_->set_layout(toolbar_layout);
    if (prop_) {
        for (const auto index : valueOf(prop_->var().toolsWrapIndices))
            if (index > 0) toolbar_host_->wrap_indices.push_back(static_cast<std::size_t>(index));
    }

    toolbar_host_->on_item_moved = [this](const std::size_t from,
                                           const std::size_t to) {
        const auto move = [from, to](auto& values) {
            if (from >= values.size() || to >= values.size() || from == to)
                return;
            auto value = std::move(values[from]);
            values.erase(values.begin() + static_cast<std::ptrdiff_t>(from));
            values.insert(values.begin() + static_cast<std::ptrdiff_t>(to),
                          std::move(value));
        };
        move(tool_bars_);
        move(tool_bar_group_order_);
        if (!prop_) return;
        auto& saved = prop_->var().toolsOrder.value;
        saved.clear();
        for (const auto index : tool_bar_group_order_)
            saved.push_back(static_cast<std::int32_t>(index));
    };

    const auto toolbar_menu = std::make_shared<lib::Menu>(true);
    toolbar_menu->items = {
        {menuText(MenuID::LockToolBar), "lock-toolbar", {},
         menuImage(MenuID::LockToolBar), true, toolbar_host_->locked},
        {{}, {}, {}, 0, true, false, false, true},
        {menuText(MenuID::ResetToolBar), "reset-toolbar", {},
         menuImage(MenuID::ResetToolBar)},
        {{}, {}, {}, 0, true, false, false, true},
        {menuText(MenuID::CustomizeToolBar), "customize-toolbar", {},
         menuImage(MenuID::CustomizeToolBar)},
    };
    toolbar_menu->on_open = [this, weak_menu = std::weak_ptr<lib::Menu>(toolbar_menu)] {
        const auto menu = weak_menu.lock();
        if (!menu || menu->items.empty()) return;
        auto items = menu->items;
        items[0].checked = toolbar_host_ && toolbar_host_->locked;
        (void)lib::replace_menu_items(menu, std::move(items));
    };
    toolbar_menu->on_command = [this](const std::string_view command) {
        if (!prop_) return;
        if (command == "customize-toolbar") {
            // D版と同様、ドラッグ後の CoolBar の順序を取得し、保
            // 存するグループを並べてから、 同じカスタマイズ操作を開く。
            const auto life = lifecycle_alive_;
            if (!updateMainToolBar(true) ||
                !life->load(std::memory_order_acquire)) return;
            composeRoot(false);
            if (!life->load(std::memory_order_acquire)) return;
            raiseEvent(MenuID::CustomizeToolBar, SelectionEvent{});
            return;
        }
        if (command == "lock-toolbar") {
            const bool locked = !(toolbar_host_ && toolbar_host_->locked);
            prop_->var().toolsLock.value = locked;
            if (toolbar_host_) toolbar_host_->locked = locked;
            if (services_.gui && toolbar_host_)
                services_.gui->request_redraw(toolbar_host_);
            return;
        }
        if (command == "reset-toolbar") {
            prop_->var().toolsOrder.value = prop_->var().toolsOrder.INIT;
            prop_->var().toolsWrapIndices.value =
                prop_->var().toolsWrapIndices.INIT;
            createMainToolBar(false);
            composeRoot(false);
            if (services_.gui && window_)
                services_.gui->request_redraw(window_);
        }
    };
    const auto show_toolbar_menu =
        [this, toolbar_menu](lib::PointerEvent& event) {
            if (event.action != lib::PointerAction::Up ||
                event.button != lib::PointerButton::Secondary ||
                !services_.gui || !toolbar_host_) {
                return;
            }
            services_.gui->show_context_menu(toolbar_menu, toolbar_host_,
                                              event.position);
            event.handled = true;
        };
    toolbar_host_->on_pointer = show_toolbar_menu;
    // ALL は周囲のアプリ操作をホストが提供する。 エ
    // ディタのツールバーモデルはコマンド・設定用に残すが
    // 、 埋め込みの主画面から表示行とその高さを除く。
    toolbar_host_->set_visible(
        !all_mode && prop_ && valueOf(prop_->var().showMainToolBar));

    for (const auto& toolbar : tool_bars_) {
        if (!toolbar) continue;
        toolbar->set_layout_item({});
        toolbar->on_pointer = show_toolbar_menu;
        toolbar_host_->add(toolbar);
    }
    // ALL の配置には主ツールバーの行を接続しない。CoolBar
    // を隠すだけでは、 エリア・戦闘のメニュー更新から到
    // 達でき、後の表示・再計測でシーン上部に高さが戻る場合
    // がある。 切り離したコマンドモデルは利用可能なまま、
    // シーン選択によらず ALL の主領域の形状を保つ。
    if (!all_mode)
        window_->add(toolbar_host_);
    }

    if (docking_folder_) {
        const auto dock = docking_folder_->component();
        if (dock) {
            lib::LayoutItem item;
            item.minimum = {all_mode ? 64.0F : 200.0F, 120.0F};
            // ドックは表示領域として扱う。選択ページの最小サイズで主ウィンド
            // ウ内のドックを広げ、 すべての仕切りを動かさないようにする。
            item.preferred = item.minimum;
            item.constrain_minimum_to_preferred = true;
            item.grow = 1.0F;
            dock->set_layout_item(item);
            window_->add(dock);
        }
    }

    // ALL は単独版のステータスバー部品を意図的に完全に省略する。
    if (all_mode) status_bar_.reset();
    if (status_bar_) {
        status_bar_->set_layout_item({});
        window_->add(status_bar_);
    }
}

std::vector<ToolBarPtr> MainWindow::toolBars() const { return tool_bars_; }

void MainWindow::refreshToolBarHostState() {
    const auto now = std::chrono::steady_clock::now();
    if (last_handle_check_.time_since_epoch().count() == 0 ||
        now - last_handle_check_ >= std::chrono::seconds(8)) {
        last_handle_check_ = now;
        if (const auto handles = lib::windows_runtime::process_handle_count();
            prop_ && comm_ && handles) {
            const auto warning = prop_->var().warningWindowsHandleCount.value;
            const auto lift = prop_->var().liftWarningWindowsHandleCount.value;
            comm_->warningHandleCount(handleWarningState(
                comm_->warningHandleCount(), *handles, warning, lift));
        }
    }
    refreshAllHistory();
}

void MainWindow::refreshToolBar(const MenuTable& cMenuTbl) {
    // ネイティブのコマンド文字列は振り分け用のキーであり、MenuID の番号ではない。 Commons
    // の調整処理とすべての構成で、型付きの MenuData 更新を共有する。
    if (comm_) comm_->refreshToolBarItems(cMenuTbl);
}

void MainWindow::refreshSelectionChrome(const StringView key) {
    const String requested_selection(key);
    const auto selection_life = std::weak_ptr<std::atomic_bool>(lifecycle_alive_);
    refreshAllCompactSelectors(key);
    refreshAllHistory();
    refreshAllViewToolbar(key);
    for (const auto& [checked_id, checked] : menu_checked_) {
        const bool value = checked && checked();
        if (const auto menu = menu_items_.find(checked_id); menu != menu_items_.end())
            menu->second.setChecked(value);
        if (const auto tool = tool_items_.find(checked_id); tool != tool_items_.end())
            tool->second.setChecked(value);
    }
    if (services_.gui && menu_bar_) services_.gui->request_redraw(menu_bar_);
    refreshToolBar(MenuTable{});
    if (services_.gui) {
        for (const auto& bar : tool_bars_)
            if (bar) services_.gui->request_redraw(bar);
        if (docking_folder_ && !key.empty()) {
            if (const auto tabs = docking_folder_->tabView(key))
                services_.gui->request_redraw(tabs);
        }
        if (all_left_selector_) services_.gui->request_redraw(all_left_selector_);
        if (all_right_selector_) services_.gui->request_redraw(all_right_selector_);
    }
    // D版と同様、イベントエディタが選択されたら共通のコンテントパレットを取得す
    // る。 新規に開いた場合だけでなく、既存のドッキングタブの選択も対象にする。
    const auto alive = selection_life.lock();
    if (!alive || !alive->load(std::memory_order_acquire) || !comm_ ||
        dock_.moving || dock_.selected != requested_selection) return;
    // D版の dockSelect と同様、新しい行選択なしで、選択ペインが保持するステータスを復元する。
    if (!isAllMode() && docking_folder_) {
        const auto panel = commons::getTopLevelPanel(docking_folder_->control(requested_selection));
        if (panel) setStatusLine(panel->statusLine());
        // ステータスのコールバックが、所有ウィンドウを閉じたり切り替えたりする場合がある。
        const auto still_alive = selection_life.lock();
        if (!still_alive || !still_alive->load(std::memory_order_acquire) ||
            !comm_ || dock_.moving || dock_.selected != requested_selection) return;
    }
    refreshSelectedEventContentsToolBox(requested_selection);
}

void MainWindow::refreshSelectedEventContentsToolBox(const StringView key) {
    if (key.empty() || !comm_ || dock_.moving || dock_.selected != key) return;
    const auto found = event_windows_.find(String(key));
    if (found == event_windows_.end()) return;
    const auto editor = found->second.lock();
    const auto* view = editor ? editor->eventView().richView() : nullptr;
    // D版の EventWindow の FocusIn 経路と同様、選択 EventView のパレットを開き直す。
    if (view) (void)comm_->getContentsToolBox(view->eventTreeControl());
}

void MainWindow::MenuShown::menuShown() { if (owner_) owner_->refreshToolBar(MenuTable{}); }

std::vector<String> MainWindow::dockSelect(String key) {
    dock_.select(key);
    if (docking_folder_) (void)docking_folder_->select(key);
    else refreshSelectionChrome(key);
    return {std::move(key)};
}

void MainWindow::dockShellClosedEvent() { refreshSelectionChrome(dock_.selected); }

bool MainWindow::dockCanMove(StringView ctrlKey, StringView dropPaneKey) const {
    if (ctrlKey.empty()) return false;
    if (dropPaneKey.empty()) return true;
    const bool destination_is_work = dropPaneKey.starts_with("work");
    return ctrlKey.starts_with("work") ? destination_is_work
                                        : !destination_is_work;
}

bool MainWindow::dockCanVanish(const DockState* dock, StringView key) const {
    if (!dock || !dock->canClose || key.empty()) return false;
    if (!docking_folder_) return !key.starts_with("work") ||
        std::count_if(dock->panes.begin(), dock->panes.end(), [](const String& pane) {
            return pane.starts_with("work");
        }) > 1;
    const auto main_works = docking_folder_->findPane("work", false).size();
    const bool can_vanish_main = prop_ &&
        valueOf(prop_->var().canVanishWorkAreaInMainWindow);
    if (can_vanish_main || main_works == 0U) {
        return !key.starts_with("work") ||
               docking_folder_->findPane("work", true).size() > 1U;
    }
    if (docking_folder_->isSubWindowPane(key)) return true;
    return !key.starts_with("work") || main_works > 1U;
}

bool MainWindow::dockCanVanish(StringView key) const { return dockCanVanish(&dock_, key); }

bool MainWindow::dockCloseCtrl(StringView key) {
    if (key.empty()) return false;
    if (docking_folder_ && !docking_folder_->control(key)) return false;
    dock_.close(key);
    if (docking_folder_) (void)docking_folder_->close(key);
    rebuildMenus();
    return true;
}

std::vector<String> MainWindow::dockMovingShellEvent(String key) {
    const auto alive = std::weak_ptr<std::atomic_bool>(lifecycle_alive_);
    dock_.moving = true;
    if (const auto found = event_windows_.find(key); found != event_windows_.end()) {
        if (const auto editor = found->second.lock()) {
            editor->eventView().prepareContentsPalette();
            if (auto* view = editor->eventView().richView(); view && view->contentsPaletteSingleton()) {
                const auto box = view->contentsPaletteControl();
                const bool pooled = box && comm_ && comm_->poolContentsToolBox(box);
                const auto life = alive.lock();
                if (!life || !life->load(std::memory_order_acquire)) return {std::move(key)};
                // D版の movingShell と同様、コントロールが古いウィンドウから離れる前にパレ
                // ットを退避する。 保持できる関連ビューがなければ、古いネイティブのパレットを破棄する。
                view = editor->eventView().richView();
                if (!pooled && view) (void)view->releaseContentsPalette();
            }
            const auto life = alive.lock();
            if (!life || !life->load(std::memory_order_acquire)) return {std::move(key)};
            // D版はここでネイティブのツールバーを保存・再構築する。C++版は共通の EventView
            // のウィジェットを保持するため、 DockingFolder が同じ
            // ツリーの親を変える前に、このホストで独立表示のパレットを準備する。
            editor->movingWindow();
        } else event_windows_.erase(found);
    }
    return {std::move(key)};
}

void MainWindow::dockMoveShellEvent(WindowPtr before, String key) {
    const auto alive = std::weak_ptr<std::atomic_bool>(lifecycle_alive_);
    const auto gui = services_.gui;
    if (!lib::editor_window_host_options().all_mode && gui && docking_folder_) {
        const auto platform = gui->platform();
        if (platform != lib::Platform::Android && platform != lib::Platform::IOS &&
            platform != lib::Platform::Web) {
            const auto control = docking_folder_->control(key);
            WindowPtr detached;
            if (control) for (const auto& shell : docking_folder_->subWindows()) {
                if (shell && !lib::input_scope_path(shell, control).empty()) {
                    detached = shell;
                    break;
                }
            }
            // Window モデルを表示状態にするだけでは、SDLウィンドウは作成・登録されない。
            // このホストイベントより前に、DockingFolder がタイトルと境界を確定する。
            if (detached && detached != before) gui->show(detached);
            const auto life = alive.lock();
            if (!life || !life->load(std::memory_order_acquire)) return;
        }
    }
    const auto initial = event_windows_.find(key);
    if (initial == event_windows_.end()) {
        // イベント以外のペインでは、通常のドッキング選択の動作を維持する。
        dock_.moving = false;
        if (docking_folder_ && docking_folder_->control(key)) {
            dock_.select(std::move(key));
            (void)docking_folder_->select(dock_.selected);
        }
        return;
    }
    const auto moved_editor = initial->second.lock();
    const auto stillMounted = [&] {
        const auto found = event_windows_.find(key);
        return moved_editor && found != event_windows_.end() && found->second.lock() == moved_editor &&
            docking_folder_ && docking_folder_->control(key);
    };
    if (!stillMounted()) { dock_.moving = false; return; }
    if (const auto found = event_windows_.find(key); found != event_windows_.end()) {
        if (const auto editor = found->second.lock()) {
            auto* view = editor->eventView().richView();
            if (view && docking_folder_ && comm_) {
                const auto control = docking_folder_->control(key);
                auto shell = services_.gui ? services_.gui->window_owner(control) : WindowPtr{};
                const auto life = alive.lock();
                if (!life || !life->load(std::memory_order_acquire)) return;
                if (!stillMounted()) { dock_.moving = false; return; }
                if (!shell) for (const auto& candidate : docking_folder_->subWindows())
                    if (candidate && !lib::input_scope_path(candidate, control).empty()) { shell = candidate; break; }
                if (!shell && !lib::input_scope_path(window_, control).empty()) shell = window_;
                view = editor->eventView().richView();
                if (shell && view) {
                    comm_->eventTreeHost(view->eventTreeControl(), shell);
                    view->contentsPaletteOwner(shell);
                }
            }
            const auto life = alive.lock();
            if (!life || !life->load(std::memory_order_acquire)) return;
            // D版はここで新しいコントロールにスナップショットを復元する。C++
            // 版はウィジェットを保持し、 下で通常のドッキング選択を取得
            // し直す前に、新しいウィンドウ・パレットの所有者を反映する。
            editor->moveWindow();
        } else event_windows_.erase(found);
    }
    const auto life = alive.lock();
    if (!life || !life->load(std::memory_order_acquire)) return;
    dock_.moving = false;
    if (!stillMounted()) return;
    dock_.select(std::move(key));
    if (docking_folder_) (void)docking_folder_->select(dock_.selected);
    const auto selected_life = alive.lock();
    if (!selected_life || !selected_life->load(std::memory_order_acquire)) return;
    // 取得の停止中に newTab がこのページを選択済みの場合があ
    // る。 新しい論理的・ネイティブの所有者を反映してから取得する。
    refreshSelectionChrome(dock_.selected);
}

String MainWindow::dockNewPaneName(StringView ctrlKey, StringView basePane,
                                    DockDirection direction) const {
    if (!docking_folder_) return {};
    if (ctrlKey.starts_with("work")) return docking_folder_->newPaneKey("work");
    if (basePane.starts_with("work")) {
        if (direction == DockDirection::east || direction == DockDirection::west)
            return docking_folder_->newPaneKey("side");
        return docking_folder_->newPaneKey("data");
    }
    return {};
}

bool MainWindow::dockFirstResize(StringView paneKey) {
    return paneKey.starts_with("work");
}

void MainWindow::publishPaneStatusLine(const WidgetPtr& owner, String status) {
    const auto alive = lifecycle_alive_;
    if (!alive || !alive->load(std::memory_order_acquire) || !comm_) return;
    comm_->setStatusLine(owner, std::move(status), true);
}

void MainWindow::setStatusLine(String status) {
    commons::TopLevelPanel::statusLine(status);
    if (status_bar_) status_bar_->text = status;
    if (context_.status) context_.status(status);
    // 保留されたステータス更新の後に、入力イベントが来るとは限らない。
    redrawAll();
}

String MainWindow::title() const { return commons::TopLevelPanel::title(); }
lib::ResourceId MainWindow::image() const noexcept { return commons::TopLevelPanel::image(); }
ContainerPtr MainWindow::shell() const noexcept { return commons::TopLevelPanel::shell(); }

std::function<void(String)> MainWindow::statusText() const {
    return [this](String status) { const_cast<MainWindow*>(this)->setStatusLine(std::move(status)); };
}

DockState* MainWindow::dock() noexcept { return &dock_; }
SummaryPtr MainWindow::summary() const noexcept { return summary_; }
std::vector<lib::MenuItem> MainWindow::debugMainMenuItems() const {
    return menu_bar_ ? menu_bar_->items : std::vector<lib::MenuItem>{};
}

bool MainWindow::debugInvokeMenuAction(const MenuID id, const SelectionEvent& event) {
    const auto action = menu_actions_.find(id);
    if (action == menu_actions_.end()) return false;
    const auto enabled = menu_enabled_.find(id);
    if (enabled != menu_enabled_.end() && !enabled->second()) return false;
    const auto invoke = action->second;
    invoke(event);
    return true;
}

bool MainWindow::debugSelectCard(const StringView type, const std::size_t index) {
    const auto normalized = lower(String(type));
    std::shared_ptr<cardwindow_ns::CardWindow> window;
    cardwindow_ns::CardPanePtr pane;
    if (normalized == "cast") {
        dock_.selected = "castCard";
        window = cast_window_;
        pane = window ? window->paneCast() : cardwindow_ns::CardPanePtr{};
    } else if (normalized == "skill") {
        dock_.selected = "skillCard";
        window = skill_window_;
        pane = window ? window->paneSkill() : cardwindow_ns::CardPanePtr{};
    } else if (normalized == "item") {
        dock_.selected = "itemCard";
        window = item_window_;
        pane = window ? window->paneItem() : cardwindow_ns::CardPanePtr{};
    } else if (normalized == "beast") {
        dock_.selected = "beastCard";
        window = beast_window_;
        pane = window ? window->paneBeast() : cardwindow_ns::CardPanePtr{};
    } else if (normalized == "info") {
        dock_.selected = "infoCard";
        window = info_window_;
        pane = window ? window->paneInfo() : cardwindow_ns::CardPanePtr{};
    } else {
        return false;
    }
    if (!pane || index >= pane->cards().size()) return false;
    (void)pane->select(static_cast<std::ptrdiff_t>(index));
    if (services_.gui && pane->canonical())
        services_.gui->focus(pane->canonical()->inputWidget());
    return pane->isSelected();
}

bool MainWindow::debugSelectVariable(const StringView type, const std::size_t index) {
    if (!flags_pane_) return false;
    const auto normalized = lower(String(type));
    const auto root = flags_pane_->dirs().rootDir();
    if (!root) return false;
    flagspane_ns::VariableObject selected;
    flagspane_ns::FlagDirPtr parent;
    if (normalized == "flag") {
        const auto values = root->allFlags();
        if (index >= values.size() || !values[index]) return false;
        selected = values[index];
        parent = values[index]->parent();
    } else if (normalized == "step") {
        const auto values = root->allSteps();
        if (index >= values.size() || !values[index]) return false;
        selected = values[index];
        parent = values[index]->parent();
    } else if (normalized == "variant") {
        const auto values = root->allVariants();
        if (index >= values.size() || !values[index]) return false;
        selected = values[index];
        parent = values[index]->parent();
    } else {
        return false;
    }
    flags_pane_->dirs().current(parent);
    flags_pane_->flags().deselectAll();
    if (const auto* value = std::get_if<flagspane_ns::FlagPtr>(&selected))
        flags_pane_->flags().select(*value);
    else if (const auto* value = std::get_if<flagspane_ns::StepPtr>(&selected))
        flags_pane_->flags().select(*value);
    else if (const auto* value = std::get_if<flagspane_ns::VariantPtr>(&selected))
        flags_pane_->flags().select(*value);
    else
        return false;
    flags_pane_->debugFocusVariables();
    return true;
}

bool MainWindow::debugTouchCard(const StringView type, const std::size_t index) {
    const auto normalized = lower(String(type));
    cardwindow_ns::CardPanePtr pane;
    if (normalized == "cast" && cast_window_) pane = cast_window_->paneCast();
    else if (normalized == "skill" && skill_window_) pane = skill_window_->paneSkill();
    else if (normalized == "item" && item_window_) pane = item_window_->paneItem();
    else if (normalized == "beast" && beast_window_) pane = beast_window_->paneBeast();
    else if (normalized == "info" && info_window_) pane = info_window_->paneInfo();
    return pane && index < pane->cards().size() && pane->debugTouch(index);
}

bool MainWindow::debugOpenCard(const StringView type, const std::size_t index) {
    const auto normalized = lower(String(type));
    cardwindow_ns::CardPanePtr pane;
    if (normalized == "cast" && cast_window_) pane = cast_window_->paneCast();
    else if (normalized == "skill" && skill_window_) pane = skill_window_->paneSkill();
    else if (normalized == "item" && item_window_) pane = item_window_->paneItem();
    else if (normalized == "beast" && beast_window_) pane = beast_window_->paneBeast();
    else if (normalized == "info" && info_window_) pane = info_window_->paneInfo();
    if (!pane || index >= pane->cards().size() ||
        !pane->select(static_cast<std::ptrdiff_t>(index))) return false;
    const auto card = pane->cards()[index];
    pane->edit();
    const auto found = active_card_dialogs_.find(card.get());
    if (found == active_card_dialogs_.end() || !found->second.window ||
        !found->second.window->visible()) return false;
    debug_dialog_window_ = found->second.window;
    return true;
}

bool MainWindow::debugOpenArea(const std::size_t index) {
    if (!area_table_ || !summary_ || index >= summary_->areas().size() ||
        !summary_->areas()[index]) {
        return false;
    }
    area_table_->openAreaScene(summary_->areas()[index].get(), true, false);
    return docking_folder_ && dock_.selected.starts_with("work") &&
           static_cast<bool>(docking_folder_->control(dock_.selected));
}

bool MainWindow::debugOpenBattle(const std::size_t index) {
    if (!area_table_ || !summary_ || index >= summary_->battles().size() ||
        !summary_->battles()[index]) {
        return false;
    }
    area_table_->openAreaScene(summary_->battles()[index].get(), true, false);
    return docking_folder_ && dock_.selected.starts_with("work") &&
           static_cast<bool>(docking_folder_->control(dock_.selected));
}

bool MainWindow::debugOpenAreaEvent(const std::size_t index) {
    if (!area_table_ || !summary_ || index >= summary_->areas().size() ||
        !summary_->areas()[index]) {
        return false;
    }
    area_table_->openAreaEvent(summary_->areas()[index].get(), true, false);
    return docking_folder_ && dock_.selected.starts_with("work") &&
           static_cast<bool>(docking_folder_->control(dock_.selected));
}

bool MainWindow::debugOpenBattleEvent(const std::size_t index) {
    if (!area_table_ || !summary_ || index >= summary_->battles().size() ||
        !summary_->battles()[index]) {
        return false;
    }
    area_table_->openAreaEvent(summary_->battles()[index].get(), true, false);
    return docking_folder_ && dock_.selected.starts_with("work") &&
           static_cast<bool>(docking_folder_->control(dock_.selected));
}

bool MainWindow::debugOpenUseEvent(const StringView type,
                                   const std::size_t index) {
    if (!comm_ || !prop_ || !summary_) return false;
    const auto normalized = lower(String(type));
    std::shared_ptr<::cwxeditor::cwx::card::EffectCard> card;
    if (normalized == "skill") {
        const auto values = summary_->skills();
        if (index >= values.size()) return false;
        card = values[index];
    } else if (normalized == "item") {
        const auto values = summary_->items();
        if (index >= values.size()) return false;
        card = values[index];
    } else if (normalized == "beast") {
        const auto values = summary_->beasts();
        if (index >= values.size()) return false;
        card = values[index];
    } else {
        return false;
    }
    if (!card || !comm_->openUseEvents(*prop_, summary_.get(), card, true))
        return false;
    return docking_folder_ && dock_.selected.starts_with("work") &&
           static_cast<bool>(docking_folder_->control(dock_.selected));
}

bool MainWindow::debugOpenPackage(const std::size_t index) {
    if (!area_table_ || !summary_ || index >= summary_->packages().size() ||
        !summary_->packages()[index]) {
        return false;
    }
    area_table_->openAreaEvent(summary_->packages()[index].get(), true, false);
    return docking_folder_ && dock_.selected.starts_with("work") &&
           static_cast<bool>(docking_folder_->control(dock_.selected));
}

void MainWindow::debugOpenScenario(SummaryPtr summary) {
    openScenario(std::move(summary), {});
}

bool MainWindow::debugImportScenario(const StringView path) {
    if (!canAddScenario() || !prop_ || path.empty()) return false;
    LoadOption option;
    option.cardOnly = false;
    option.textOnly = false;
    option.doubleIO = valueOf(prop_->var().doubleIO);
    option.expandXMLs = false;
    option.numberStepToVariantThreshold =
        valueOf(prop_->var().numberStepToVariantThreshold);
    const auto results = loader::loadScenariosFromFile(
        *prop_, option, window_, sync_, statusText(), {String(path)}, {}, {},
        false, services_);
    if (results.empty()) return false;
    openImportSources(results);
    return true;
}

bool MainWindow::debugOpenEventContent(const std::size_t visibleIndex) {
    if (const auto found = event_windows_.find(dock_.selected);
        found != event_windows_.end()) {
        if (const auto editor = found->second.lock())
            return editor->debugOpenContent(visibleIndex);
    }
    for (auto it = event_windows_.begin(); it != event_windows_.end();) {
        if (const auto editor = it->second.lock()) {
            // 無関係な有効なイベントペインを見つけただけでは止めな
            // い。 複数のシーン・パッケージタブが共存でき、unordered_map
            // の走査順は決まっていない。
            if (editor->debugOpenContent(visibleIndex)) return true;
            ++it;
            continue;
        }
        it = event_windows_.erase(it);
    }
    return false;
}

bool MainWindow::debugPlaceEventContent(const types::CType type,
                                        const bool autoOpen,
                                        const std::size_t visibleIndex) {
    if (const auto found = event_windows_.find(dock_.selected);
        found != event_windows_.end()) {
        if (const auto editor = found->second.lock())
            return editor->debugPlaceContent(type, autoOpen, visibleIndex);
    }
    for (auto it = event_windows_.begin(); it != event_windows_.end();) {
        if (const auto editor = it->second.lock()) {
            if (editor->debugPlaceContent(type, autoOpen, visibleIndex)) return true;
            ++it;
        } else {
            it = event_windows_.erase(it);
        }
    }
    return false;
}

bool MainWindow::debugApplyEventContentDialog() {
    contentinitializer::EventDialogPtr target;
    for (const auto& [key, weak] : event_windows_) {
        (void)key;
        const auto editor = weak.lock();
        const auto active = editor ? editor->debugActiveDialog()
                                   : contentinitializer::EventDialogPtr{};
        if (!active || !active->widget() || !active->widget()->visible()) continue;
        if (target && target != active) return false;
        target = active;
    }
    if (!target) return false;
    return target->applyAndClose();
}

bool MainWindow::debugCreateSceneCard() {
    const auto found = debug_scene_create_card_.find(dock_.selected);
    if (found == debug_scene_create_card_.end() || !found->second) return false;
    // カード作成はダイアログの入れ子のネイティブループに
    // 入る。 メニューからの実行と同様に予約し、そのルー
    // プの開始前に通信コマンドが応答できるようにする。
    if (services_.dispatcher) services_.dispatcher->post(found->second);
    else return false;
    return true;
}

std::shared_ptr<areawindow::AreaSceneWindow> MainWindow::debugAreaSceneWindow(const WidgetPtr& shell) const {
    for(const auto& [key,weak]:debug_area_scene_windows_) {
        (void)key;
        if(const auto editor=weak.lock();editor && editor->shell()==shell)return editor;
    }
    return {};
}

std::shared_ptr<areawindow::BattleSceneWindow>
MainWindow::debugBattleSceneWindow(const WidgetPtr& shell) const {
    for (const auto& [key, weak] : debug_battle_scene_windows_) {
        (void)key;
        if (const auto editor = weak.lock(); editor && editor->shell() == shell) return editor;
    }
    return {};
}

bool MainWindow::debugApplySceneCardDialog() {
    lib::Action apply;
    for (const auto& [key, entry] : active_scene_card_dialogs_) {
        (void)key;
        if (!entry.window || !entry.window->visible() || !entry.apply) continue;
        if (apply) return false;
        apply = entry.apply;
    }
    if (!apply) return false;
    apply();
    return true;
}

String MainWindow::debugCardState(const StringView type) const {
    const auto normalized = lower(String(type));
    cardwindow_ns::CardPanePtr pane;
    if (normalized == "cast" && cast_window_) pane = cast_window_->paneCast();
    else if (normalized == "skill" && skill_window_) pane = skill_window_->paneSkill();
    else if (normalized == "item" && item_window_) pane = item_window_->paneItem();
    else if (normalized == "beast" && beast_window_) pane = beast_window_->paneBeast();
    else if (normalized == "info" && info_window_) pane = info_window_->paneInfo();
    if (!pane) return "unavailable";
    const auto mode = pane->viewMode() == cardwindow_ns::CViewMode::LIFE ? "life"
                    : pane->viewMode() == cardwindow_ns::CViewMode::CARD ? "image"
                    : pane->viewMode() == cardwindow_ns::CViewMode::TABLE ? "detail"
                    : "initial";
    String result = "cards=" + std::to_string(pane->cards().size()) +
                    " mode=" + mode +
                    " undo=" + std::to_string(pane->canUndo() ? 1 : 0) +
                    " redo=" + std::to_string(pane->canRedo() ? 1 : 0) +
                    " selected=";
    const auto selection = pane->canonical()
        ? pane->canonical()->selectionIndices()
        : pane->selectedIndices();
    if (selection.empty()) return result + "none";
    for (std::size_t i = 0; i < selection.size(); ++i) {
        if (i != 0U) result += ',';
        result += std::to_string(selection[i]);
    }
    return result;
}

String MainWindow::debugVariableState() const {
    if (!flags_pane_) return "unavailable";
    const auto root = flags_pane_->dirs().rootDir();
    if (!root) return "unavailable";
    const auto flags = root->allFlags().size();
    const auto steps = root->allSteps().size();
    const auto variants = root->allVariants().size();
    String selected;
    for (const auto index : flags_pane_->flags().selected()) {
        if (!selected.empty()) selected += ',';
        selected += std::to_string(index);
    }
    if (selected.empty()) selected = "none";
    return "flags=" + std::to_string(flags) + " steps=" + std::to_string(steps) +
           " variants=" + std::to_string(variants) + " selected=" + selected +
           " undo=" + (flags_pane_->canUndo() ? "1" : "0") +
           " redo=" + (flags_pane_->canRedo() ? "1" : "0");
}

void MainWindow::debugOpenSettings() {
    openSettings(true);
    if (!settings_dialog_ || !settings_dialog_->dialog() || !services_.gui) return;
    const auto dialog = settings_dialog_->dialog();
    dialog->activate_on_show = false;
    services_.gui->show(dialog);
}

String MainWindow::debugSettingsState() const {
    if (!settings_dialog_ || !settings_dialog_->dialog()) return "unavailable";
    const auto dialog = settings_dialog_->dialog();
    std::size_t widgets{};
    std::vector<String> tabs;
    const auto inspect = [&](const lib::WidgetPtr& widget, const auto& self) -> void {
        if (!widget) return;
        ++widgets;
        if (const auto tab = std::dynamic_pointer_cast<lib::TabView>(widget)) {
            for (const auto& page : tab->pages) {
                tabs.push_back(page.title);
                self(page.content, self);
            }
        }
        if (const auto container = std::dynamic_pointer_cast<lib::Container>(widget))
            for (const auto& child : container->children()) self(child, self);
    };
    inspect(dialog, inspect);
    std::size_t selected{};
    if (settings_dialog_->tabFolder() && settings_dialog_->tabFolder()->selected_index)
        selected = *settings_dialog_->tabFolder()->selected_index;
    String result = String("visible=") + (dialog->visible() ? "1" : "0") +
                    " widgets=" + std::to_string(widgets) + " tabs=" +
                    std::to_string(tabs.size()) + " selected=" +
                    std::to_string(selected);
    const auto dialog_bounds = dialog->bounds();
    result += " dialog=" + std::to_string(static_cast<int>(dialog_bounds.x)) + "," +
              std::to_string(static_cast<int>(dialog_bounds.y)) + "," +
              std::to_string(static_cast<int>(dialog_bounds.width)) + "x" +
              std::to_string(static_cast<int>(dialog_bounds.height));
    const auto append_bounds = [&result](const char* name,
                                         const lib::WidgetPtr& widget) {
        if (!widget) return;
        const auto bounds = widget->bounds();
        result += " " + String(name) + "=" +
                  std::to_string(static_cast<int>(bounds.x)) + "," +
                  std::to_string(static_cast<int>(bounds.y)) + "," +
                  std::to_string(static_cast<int>(bounds.width)) + "x" +
                  std::to_string(static_cast<int>(bounds.height));
    };
    append_bounds("main-window", window_);
    append_bounds("main-panel", settings_dialog_->mainPanel());
    append_bounds("content", settings_dialog_->content());
    append_bounds("addition", settings_dialog_->addition());
    append_bounds("buttons", settings_dialog_->buttonsPanel());
    if (prop_ && !prop_->var().outerTools.value.empty()) {
        result += " tool0=\"" + prop_->var().outerTools.value.front().name + "\"";
    }
    if (prop_) {
        result += " defaults=selections:" +
                  std::to_string(prop_->var().standardSelections.value.size()) +
                  ",keycodes:" +
                  std::to_string(prop_->var().standardKeyCodes.value.size());
    }
    result += " published-keycode-rows=features:" +
              std::to_string(settings_dialog_->publishedFeatureKeyCodeRows()) +
              ",motions:" +
              std::to_string(settings_dialog_->publishedMotionKeyCodeRows());
    result += " keycode-values=features:" +
              std::to_string(settings_dialog_->featureKeyCodeValueCount()) +
              ",motions:" +
              std::to_string(settings_dialog_->motionKeyCodeValueCount()) +
              " skin-settings-index:" +
              std::to_string(settings_dialog_->selectedSkinSettingsIndex());
    if (settings_dialog_->tabFolder()) {
        const auto tab_bounds = settings_dialog_->tabFolder()->bounds();
        result += " tab=" + std::to_string(static_cast<int>(tab_bounds.x)) + "," +
                  std::to_string(static_cast<int>(tab_bounds.y)) + "," +
                  std::to_string(static_cast<int>(tab_bounds.width)) + "x" +
                  std::to_string(static_cast<int>(tab_bounds.height));
        if (selected < settings_dialog_->tabFolder()->pages.size() &&
            settings_dialog_->tabFolder()->pages[selected].content) {
            const auto page_content = settings_dialog_->tabFolder()->pages[selected].content;
            const auto page_bounds = page_content->bounds();
            result += " page=" + std::to_string(static_cast<int>(page_bounds.x)) + "," +
                      std::to_string(static_cast<int>(page_bounds.y)) + "," +
                      std::to_string(static_cast<int>(page_bounds.width)) + "x" +
                      std::to_string(static_cast<int>(page_bounds.height));
            std::size_t page_lists{}, page_list_items{}, page_tables{}, page_table_rows{};
            std::size_t multiline_edits{}, multiline_chars{};
            String first_table_text;
            const auto inspect_page = [&](const lib::WidgetPtr& widget,
                                          const auto& self) -> void {
                if (!widget) return;
                if (const auto list = std::dynamic_pointer_cast<lib::ListView>(widget)) {
                    ++page_lists;
                    page_list_items += list->items.size();
                }
                if (const auto table = std::dynamic_pointer_cast<lib::TableView>(widget)) {
                    ++page_tables;
                    page_table_rows += table->rows.size();
                    if (first_table_text.empty() && !table->rows.empty() &&
                        !table->rows.front().cells.empty())
                        first_table_text = table->rows.front().cells.front().text;
                }
                if (const auto edit = std::dynamic_pointer_cast<lib::TextEdit>(widget);
                    edit && edit->multiline) {
                    ++multiline_edits;
                    multiline_chars += edit->text.size();
                }
                if (const auto container = std::dynamic_pointer_cast<lib::Container>(widget))
                    for (const auto& child : container->children()) self(child, self);
            };
            inspect_page(page_content, inspect_page);
            result += " data=lists:" + std::to_string(page_lists) + "/" +
                      std::to_string(page_list_items) + ",tables:" +
                      std::to_string(page_tables) + "/" +
                      std::to_string(page_table_rows) + ",multiline:" +
                      std::to_string(multiline_edits) + "/" +
                      std::to_string(multiline_chars) + ",first-table=\"" +
                      first_table_text + "\"";
        }
    }
    for (std::size_t index = 0; index < tabs.size(); ++index)
        result += ";" + std::to_string(index) + "=\"" + tabs[index] + "\"";
    return result;
}

bool MainWindow::debugSelectSettingsTab(const std::size_t index) {
    if (!settings_dialog_ || !settings_dialog_->tabFolder()) return false;
    const auto tabs = settings_dialog_->tabFolder();
    if (index >= tabs->pages.size()) return false;
    if (tabs->selected_index == index) return true;
    if (!lib::select_tab_page(*tabs, index)) return false;
    if (services_.gui) services_.gui->request_redraw(tabs);
    return true;
}

bool MainWindow::debugCloseSettings() {
    if (!settings_dialog_) return false;
    const auto dialog = settings_dialog_->dialog();
    if (dialog && dialog->visible()) settings_dialog_->forceCancel();
    else if (dialog && services_.gui) services_.gui->close(dialog);
    const bool closed = !dialog || !dialog->visible();
    if (closed) settings_dialog_.reset();
    return closed;
}

bool MainWindow::debugOpenDialog(const StringView kind) {
    if (!prop_ || !window_) return false;
    if (debug_dialog_window_ && debug_dialog_window_->visible()) return false;
    const auto normalized = lower(String(kind));
    if (normalized == "scenario" || normalized == "newscenario") {
        auto dialog = std::make_shared<smalldialogs::CreateScenarioDialog>(
            *prop_, window_, true, smalldialogs::Callbacks{}, services_);
        dialog->modal(false);
        dialog->dialog()->activate_on_show = false;
        debug_dialog_window_ = dialog->dialog();
        dynamic_panes_.push_back(dialog);
        (void)dialog->open();
        return debug_dialog_window_ && debug_dialog_window_->visible();
    }

    types::CType type{};
    if (normalized == "effect") type = types::CType::Effect;
    else if (normalized == "message") type = types::CType::TalkMessage;
    else if (normalized.starts_with("event:")) {
        const auto value_text = normalized.substr(6);
        std::size_t consumed{};
        try {
            const auto value = std::stol(value_text, &consumed, 10);
            if (consumed != value_text.size() ||
                value < static_cast<long>(types::CType::Start) ||
                value > static_cast<long>(types::CType::CustomXmlContent))
                return false;
            type = static_cast<types::CType>(value);
        } catch (const std::exception&) {
            return false;
        }
    } else return false;
    contentinitializer::ContentInitializerContext context(
        const_cast<::cwxeditor::cwx::props::CProps&>(prop_->parent()),
        prop_->var(), services_);
    context.editor_props = prop_;
    context.editor_commons = comm_;
    context.openCWXPath = [this, weak=std::weak_ptr<std::atomic_bool>(lifecycle_alive_)](
        const StringView path, const bool activate) {
        const auto life=weak.lock();
        return life && life->load(std::memory_order_acquire) &&
            openCWXPath(String(path),activate);
    };
    context.dialog_owner = window_;
    context.editBeastCard = [this](
        Summary* source, const auto& owner, const auto& card,
        const bool read_only, const StringView nested_path,
        const bool shell_activate, lib::Action applying, lib::Action applied,
        lib::Action closed) {
        return openBeastCardDialog(card_comm_, source, owner, card, read_only,
            nested_path, shell_activate, std::move(applying),
            std::move(applied), std::move(closed));
    };
    context.skinResolver = [this](const Summary* source, StringView, StringView) {
        return comm_ ? comm_->findSkinFromHistory(source) : skin_;
    };
    auto content = std::make_shared<::cwxeditor::cwx::event::Content>(type, "");
    if (type == types::CType::TalkMessage) content->boundaryCheck(true);
    auto dialog = eventdialog::createEventDialog(
        context, summary_, {}, summary_ ? summary_->useCounter()
                                        : eventdialog::UseCounterPtr{},
        window_, content, {}, false);
    if (!dialog) return false;
    dialog->widget()->activate_on_show = false;
    debug_dialog_window_ = dialog->widget();
    dialog->closeEvent.push_back([this] { debug_dialog_window_.reset(); });
    dynamic_panes_.push_back(dialog);
    dialog->open();
    return debug_dialog_window_ && debug_dialog_window_->visible();
}

std::shared_ptr<lib::Window> MainWindow::debugSettingsWindow() const {
    return settings_dialog_ && settings_dialog_->dialog() &&
           settings_dialog_->dialog()->visible() ? settings_dialog_->dialog() : nullptr;
}

MainWindow::SceneCommentCallbacks MainWindow::sceneCommentCallbacks() {
    const auto weak = std::weak_ptr<std::atomic_bool>(lifecycle_alive_);
    return {
        [this, weak](const StringView text) {
            const auto alive = weak.lock();
            if (!alive || !alive->load(std::memory_order_acquire))
                return std::shared_ptr<comment_ns::CommentDialog>{};
            return createCommentDialog(text);
        },
        [this, weak](lib::Action callback) {
            const auto alive = weak.lock();
            if (!alive || !alive->load(std::memory_order_acquire) || !comm_)
                return lib::Action{};
            const auto ref = [callback](String) { if (callback) callback(); };
            const auto menu_card_ref = comm_->refMenuCard.add(ref);
            const auto menu_card_del = comm_->delMenuCard.add(ref);
            const auto bg_image_ref = comm_->refBgImage.add(ref);
            const auto bg_image_del = comm_->delBgImage.add(ref);
            const auto text_ref = comm_->replText.add(callback);
            return lib::Action([this, weak, menu_card_ref, menu_card_del,
                                bg_image_ref, bg_image_del, text_ref] {
                const auto alive = weak.lock();
                if (!alive || !alive->load(std::memory_order_acquire) || !comm_) return;
                comm_->refMenuCard.remove(menu_card_ref);
                comm_->delMenuCard.remove(menu_card_del);
                comm_->refBgImage.remove(bg_image_ref);
                comm_->delBgImage.remove(bg_image_del);
                comm_->replText.remove(text_ref);
            });
        }
    };
}

std::shared_ptr<comment_ns::CommentDialog> MainWindow::createCommentDialog(const StringView text){
    // D版と同様、各モードレスエディタの構築時に現在のタブ設定を読む。 シーン・カード・エリ
    // アのすべての呼び出し元で、同じ生成処理と CommentDialog API を使う。
    if(prop_) {
        comment_comm_.prop.var.etc.textTabs=prop_->var().tabs.value;
        comment_ns::bindEditorImages(comment_comm_.prop, *prop_);
    }
    comment_comm_.prop.bindDialogSize = [properties=prop_, life=std::weak_ptr(lifecycle_alive_)](const std::shared_ptr<lib::Window>& window) -> std::function<void()> {
        return !properties || life.expired() ? std::function<void()>{} : properties->bindDialogSize(window, properties->flex().commentDlg);
    };
    return std::make_shared<comment_ns::CommentDialog>(comment_comm_,window_,String(text));
}

std::shared_ptr<lib::Window> MainWindow::debugUiDispatchErrorWindow() const {
    return ui_dispatch_error_dialog_
        ? ui_dispatch_error_dialog_->dialog() : std::shared_ptr<lib::Window>{};
}

std::shared_ptr<lib::Window> MainWindow::debugDialogWindow() const {
    if (debug_dialog_window_ && debug_dialog_window_->visible()) return debug_dialog_window_;
    std::shared_ptr<lib::Window> target;
    for (const auto& [key, entry] : active_card_dialogs_) {
        (void)key;
        if (!entry.window || !entry.window->visible()) continue;
        // 複数のエディタが開いているとき、unordered_map の任意の項目を選
        // ばない。 OPENCARD は要求された製品のダイアログを正確に識別する。
        if (target && target != entry.window) return {};
        target = entry.window;
    }
    for (const auto& [key, entry] : active_background_dialogs_) {
        (void)key;
        if (!entry.window || !entry.window->visible()) continue;
        if (target && target != entry.window) return {};
        target = entry.window;
    }
    for (const auto& [key, entry] : active_scene_card_dialogs_) {
        (void)key;
        if (!entry.window || !entry.window->visible()) continue;
        if (target && target != entry.window) return {};
        target = entry.window;
    }
    for (const auto& [key, weak] : event_windows_) {
        (void)key;
        const auto editor = weak.lock();
        const auto active = editor ? editor->debugActiveDialog()
                                   : contentinitializer::EventDialogPtr{};
        const auto window = active ? active->widget() : nullptr;
        if (!window || !window->visible()) continue;
        if (target && target != window) return {};
        target = window;
    }
    return target;
}

std::shared_ptr<lib::Window> MainWindow::debugReplaceDialogWindow() const {
    if (!actual_replace_dialog_) return {};
    const auto window = actual_replace_dialog_->window();
    return window && window->visible() ? window : nullptr;
}

bool MainWindow::debugCloseReplaceDialog() {
    if (!actual_replace_dialog_ || !actual_replace_dialog_->window() ||
        !actual_replace_dialog_->window()->visible()) return false;
    actual_replace_dialog_->close();
    return true;
}

std::shared_ptr<lib::Window> MainWindow::debugHistoryDialogWindow() const {
    if (!scenario_history_dialog_) return {};
    const auto window = scenario_history_dialog_->dialog();
    return window && window->visible() ? window : nullptr;
}

bool MainWindow::debugCloseHistoryDialog() {
    const auto window = debugHistoryDialogWindow();
    if (!window) return false;
    scenario_history_dialog_->close();
    return true;
}

std::shared_ptr<lib::Window> MainWindow::debugImportHistoryDialogWindow() const {
    if (!import_history_dialog_) return {};
    const auto window = import_history_dialog_->dialog();
    return window && window->visible() ? window : nullptr;
}

bool MainWindow::debugCloseImportHistoryDialog() {
    const auto window = debugImportHistoryDialogWindow();
    if (!window) return false;
    import_history_dialog_->close();
    return true;
}

bool MainWindow::debugHistoryDialogAction(const bool importHistory,
                                          const StringView action,
                                          const std::size_t index,
                                          const bool checked) {
    // Props::apply がこの所有者を無効化する場合があるため、呼び出し中は有効なダイアログを保持する。
    const auto value = importHistory ? import_history_dialog_
                                     : scenario_history_dialog_;
    if (!value || !value->dialog() || !value->dialog()->visible()) return false;
    if (action == "select") {
        value->select({index});
        return true;
    }
    if (action == "bookmark") {
        value->select({index});
        value->set_bookmark(index, checked);
        return true;
    }
    if (action == "up") {
        value->select({index});
        if (!value->can_up()) return false;
        value->up();
        return true;
    }
    if (action == "down") {
        value->select({index});
        if (!value->can_down()) return false;
        value->down();
        return true;
    }
    if (action == "apply") return value->apply();
    if (action == "cancel") {
        const auto dialog = value->dialog();
        if (dialog->on_close_requested) dialog->on_close_requested();
        else value->close();
        return true;
    }
    if (action == "close") {
        value->close();
        return true;
    }
    return false;
}

String MainWindow::debugDialogState() const {
    const auto target = debugDialogWindow();
    if (!target) return "unavailable";
    std::size_t widgets{};
    std::size_t tabs{};
    std::size_t pages{};
    std::size_t selected{};
    String radar_layout;
    String control_layout;
    const auto inspect = [&](const lib::WidgetPtr& widget, const auto& self) -> void {
        if (!widget) return;
        ++widgets;
        if (widgets <= 80U) {
            const auto child_bounds = widget->bounds();
            control_layout += " " + std::to_string(static_cast<int>(widget->kind())) + "=" +
                std::to_string(static_cast<int>(child_bounds.x)) + "," +
                std::to_string(static_cast<int>(child_bounds.y)) + "," +
                std::to_string(static_cast<int>(child_bounds.width)) + "x" +
                std::to_string(static_cast<int>(child_bounds.height));
        }
        if (const auto tab = std::dynamic_pointer_cast<lib::TabView>(widget)) {
            if (tabs == 0) {
                selected = tab->selected_index.value_or(0U);
                pages = tab->pages.size();
            }
            ++tabs;
            for (const auto& page : tab->pages) self(page.content, self);
        }
        if (const auto container = std::dynamic_pointer_cast<lib::Container>(widget)) {
            if (widget->accessibility_name == "RadarSpinner") {
                radar_layout += " radar=";
                for (const auto& child : container->children()) {
                    if (!child || std::dynamic_pointer_cast<lib::Canvas>(child)) continue;
                    const auto child_bounds = child->bounds();
                    radar_layout += std::to_string(static_cast<int>(child_bounds.x)) + "," +
                                    std::to_string(static_cast<int>(child_bounds.y)) + "," +
                                    std::to_string(static_cast<int>(child_bounds.width)) + "x" +
                                    std::to_string(static_cast<int>(child_bounds.height)) + ";";
                }
            }
            for (const auto& child : container->children()) self(child, self);
        }
    };
    inspect(target, inspect);
    const auto bounds = target->bounds();
    return "visible=" + String(target->visible() ? "1" : "0") +
           " title=\"" + target->title + "\" widgets=" +
           std::to_string(widgets) + " tabs=" + std::to_string(tabs) +
           " pages=" + std::to_string(pages) + " selected=" + std::to_string(selected) + " dialog=" +
           std::to_string(static_cast<int>(bounds.x)) + "," +
           std::to_string(static_cast<int>(bounds.y)) + "," +
           std::to_string(static_cast<int>(bounds.width)) + "x" +
           std::to_string(static_cast<int>(bounds.height)) + radar_layout +
           " controls={" + control_layout + " }";
}

bool MainWindow::debugSelectDialogTab(const std::size_t index) {
    const auto target = debugDialogWindow();
    std::shared_ptr<lib::TabView> first;
    const auto find = [&](const lib::WidgetPtr& widget, const auto& self) -> void {
        if (!widget || first) return;
        if (const auto tab = std::dynamic_pointer_cast<lib::TabView>(widget)) {
            first = tab;
            return;
        }
        if (const auto container = std::dynamic_pointer_cast<lib::Container>(widget))
            for (const auto& child : container->children()) self(child, self);
    };
    if (target) find(target, find);
    if (!first || index >= first->pages.size()) return false;
    if (first->selected_index == index) return true;
    if (!lib::select_tab_page(*first, index)) return false;
    if (services_.gui) services_.gui->request_redraw(first);
    return true;
}

bool MainWindow::debugSetCastField(const StringView field, const StringView value) {
    const auto dialog = debug_cast_dialog_.lock();
    if (!dialog) return false;
    const auto normalized = lower(String(field));
    if (normalized == "name") {
        if (!dialog->nameControl()) return false;
        dialog->nameControl()->text = String(value);
    } else if (normalized == "desc" || normalized == "description") {
        if (!dialog->descriptionControl()) return false;
        dialog->descriptionControl()->text = String(value);
    } else if (normalized == "level" || normalized == "lifemax") {
        const auto control = normalized == "level" ? dialog->levelControl()
                                                    : dialog->lifeMaxControl();
        if (!control) return false;
        try {
            std::size_t consumed{};
            const auto number = std::stod(String(value), &consumed);
            if (consumed != value.size()) return false;
            control->value = number;
        } catch (...) {
            return false;
        }
    } else {
        return false;
    }
    if (services_.gui) services_.gui->request_redraw(dialog->dialog());
    return true;
}

bool MainWindow::debugApplyCastDialog() {
    const auto dialog = debug_cast_dialog_.lock();
    if (!dialog) return false;
    dialog->forceApply();
    return true;
}

bool MainWindow::debugApplyCardDialog(const StringView type) {
    const auto normalized = lower(String(type));
    std::optional<cardwindow_ns::CardType> expected;
    if (normalized == "cast") expected = cardwindow_ns::CardType::Cast;
    else if (normalized == "skill") expected = cardwindow_ns::CardType::Skill;
    else if (normalized == "item") expected = cardwindow_ns::CardType::Item;
    else if (normalized == "beast") expected = cardwindow_ns::CardType::Beast;
    else if (normalized == "info") expected = cardwindow_ns::CardType::Info;
    if (!expected) return false;
    for (const auto& [key, entry] : active_card_dialogs_) {
        (void)key;
        if (entry.type != *expected || !entry.apply) continue;
        entry.apply();
        return true;
    }
    return false;
}

std::size_t MainWindow::debugAreaDirectoryCount() const {
    if (!area_table_) return 0;
    const auto root = area_table_->directoryTree();
    const auto count = [](const std::shared_ptr<areatable_ns::AreaTable::DirTree>& node,
                          const auto& self) -> std::size_t {
        if (!node) return 0;
        std::size_t result = node->parent ? 1U : 0U;
        for (const auto& child : node->subDirs) result += self(child, self);
        return result;
    };
    return count(root, count);
}

bool MainWindow::debugModifyAndApplyToolbarCustomizer() {
    if (!toolbar_custom_dialog_ || !toolbar_custom_dialog_->customizer()) return false;
    const auto before = toolbar_custom_dialog_->customizer()->tools().tools.size();
    toolbar_custom_dialog_->customizer()->addBar();
    if (!toolbar_custom_dialog_->apply()) return false;
    return prop_ && prop_->var().mainToolBar.value.tools.size() == before + 1U;
}

std::size_t MainWindow::debugImportSourceCount() const {
    return static_cast<std::size_t>(std::count_if(
        dynamic_card_windows_.begin(), dynamic_card_windows_.end(),
        [](const auto& entry) { return entry.first.starts_with("import:"); }));
}

bool MainWindow::debugCloseDialog() {
    const auto value = debugDialogWindow();
    if (!value) return false;
    if (value->on_close_requested) value->on_close_requested();
    else if (services_.gui) services_.gui->close(value);
    const bool closed = !value->visible();
    if (closed) debug_dialog_window_.reset();
    return closed;
}

void MainWindow::redrawAll() {
    if (context_.redraw) context_.redraw();
    else if (services_.gui && window_) services_.gui->request_redraw(window_);
}

bool MainWindow::canAddScenario() const { return summary_ != nullptr; }

namespace {

LoadOption importLoadOption(const Props& prop) {
    LoadOption option;
    option.cardOnly = false;
    option.textOnly = false;
    option.doubleIO = valueOf(prop.var().doubleIO);
    option.expandXMLs = false;
    option.numberStepToVariantThreshold =
        valueOf(prop.var().numberStepToVariantThreshold);
    return option;
}

}  // namespace

void MainWindow::addScenario() {
    addScenarioImpl({}, {}, true);
}

void MainWindow::addScenario(const std::vector<String>& paths,
                             lib::Action failure) {
    addScenarioImpl(paths, std::move(failure), false);
}

void MainWindow::addScenarioImpl(const std::vector<String>& paths,
                                 lib::Action failure, bool choose_files) {
    if (!canAddScenario() || !prop_ || in_saving_ || close_pending_ || (!choose_files && paths.empty())) {
        if (failure) failure();
        return;
    }
    pollBackgroundLoads();
    const auto control = std::make_shared<loader::LoadControl>();
    control->submission_finished.store(false, std::memory_order_release);
    background_loads_.push_back(control);
    const std::weak_ptr<std::atomic_bool> alive = lifecycle_alive_;
    const auto expected_summary = summary_;
    const auto generation = load_generation_;
    if (!choose_files && !paths.empty()) {
        const auto previous_dialog = previous_scenario_loading_;
        std::shared_ptr<lib::Dialog> created_dialog;
        showScenarioLoading("外部シナリオを読み込み中です。\n成功またはエラーが確定するまでお待ちください。",
                            &created_dialog);
        const auto progress_gate = alive.lock();
        if (!progress_gate || !progress_gate->load(std::memory_order_acquire)) {
            control->submission_finished.store(true, std::memory_order_release);
            return;
        }
        if (created_dialog && previous_scenario_loading_ == created_dialog &&
            !isAllMode() && created_dialog != previous_dialog &&
            !scenario_load_control_ && load_generation_ == generation &&
            summary_ == expected_summary) {
            standalone_array_import_progress_.push_back(
                {control, created_dialog, generation, expected_summary});
        }
    }
    const auto valid = [this, alive, control, expected_summary] {
        const auto gate = alive.lock();
        return gate && gate->load(std::memory_order_acquire) &&
            !control->isCancelled() && !close_pending_ && summary_ == expected_summary;
    };
    auto status = [this, valid](String text) { if (valid()) setStatusLine(std::move(text)); };
    auto complete = [this, valid](const loader::LoadResults& results) {
        if (valid()) deliverLoadResult([this, valid, results] {
            if (!valid()) return;
            openImportSources(results);
            dismissPreviousScenarioLoading();
        });
    };
    auto failed = [this, valid, failure = std::move(failure)] {
        if (valid()) deliverLoadResult([this, valid, failure] {
            if (!valid()) return;
            dismissPreviousScenarioLoading();
            if (failure) failure();
        });
    };
    if (choose_files && isAllMode()) {
        const bool started = loader::loadScenariosAsync(
            *prop_, importLoadOption(*prop_), window_, sync_, status,
            prop_->requiredMsgs().dlgTitAddScenario.value, complete, failed, true,
            services_, control);
        if (!started) {
            control->submission_finished.store(true, std::memory_order_release);
            status("シナリオ選択ダイアログを開けませんでした。");
            failed();
        }
        return;
    }
    struct Submission {
        std::shared_ptr<loader::LoadControl> control;
        ~Submission() { control->submission_finished.store(true, std::memory_order_release); }
    } submission{control};
    if (choose_files) {
        (void)loader::loadScenarios(*prop_, importLoadOption(*prop_), window_, sync_,
            std::move(status), prop_->requiredMsgs().dlgTitAddScenario.value,
            std::move(complete), std::move(failed), true, services_, control);
    } else {
        (void)loader::loadScenariosFromFile(*prop_, importLoadOption(*prop_), window_, sync_,
            std::move(status), paths, std::move(complete), std::move(failed),
            true, services_, control);
    }
}

void MainWindow::openImportSources(const loader::LoadResults& results) {
    if (!summary_ || !prop_ || !comm_) return;
    const auto initial_import_history = valueOf(prop_->var().importHistory);
    const auto initial_import_bookmarks = valueOf(prop_->var().importBookmarks);
    const auto owner_lifetime = lifecycle_alive_;
    for (const auto& result : results) {
        const auto& source = result.summary;
        if (!source) continue;
        if (!lib::editor_window_host_options().all_mode) {
            // D版と同様、履歴を解決してから取り込み元の不足したスキン情報を
            // 補い、 その情報を使って取り込み元のカードビューを構築する。
            const std::weak_ptr<std::atomic_bool> life = lifecycle_alive_;
            const auto defaultType = prop_->var().defaultSkin.value;
            const auto defaultName = prop_->var().defaultSkinName.value;
            OpenHistory sourceHistory;
            (void)comm_->findSkinFromHistory(source.get(), sourceHistory);
            const auto alive = life.lock();
            if (!alive || !alive->load()) return;
            if (source->legacy()) {
                if (!sourceHistory.path.empty() &&
                    (!sourceHistory.skinType.empty() || !sourceHistory.skinEngine.empty()))
                    source->type(sourceHistory.skinType);
                if (!sourceHistory.skinName.empty()) source->skinName(sourceHistory.skinName);
            } else {
                if (source->type().empty()) source->type(defaultType);
                if (source->skinName().empty()) source->skinName(defaultName);
            }
        }

        // D版の AddCard.openScenario と同様、取り込み元は読
        // み取り専用の CardWindow とし、 目次には現在編集中のシナリオ
        // を使う。専用のモデル上の親により、 DockingFolder の外で主
        // ウィンドウへ挿入せずに、CardWindow の部品を完全に構築する。
        auto modelParent = std::make_shared<lib::Panel>();
        auto import_comm = card_comm_;
        // カード枠と画像パスは、編集中のシナリオではなく取
        // り込み元のシナリオに対する相対値。 主ウィンドウ
        // の summary_ を参照すると、有効な外部カ
        // ードでも画像が空になるため、取り込み元を使う。
        auto captions=std::make_shared<std::unordered_map<const cardwindow_ns::Card*,bool>>();
        import_comm.cardImageData = [this, source, captions](const cardwindow_ns::CardPtr& card) {
            bool caption{};
            auto result=renderCardImage(source,card,false,&caption);
            (*captions)[card.get()]=caption;
            return result;
        };
        // 生成画像とともに見出しの所有関係を反映する。 再描画でス
        // キンを探し直したり、画像と異なるスキンを使ったりしない。
        import_comm.cardImageHasCaption = [captions](const cardwindow_ns::CardPtr& card) {
            const auto found=captions->find(card.get());
            return found!=captions->end() && found->second;
        };
        import_comm.cardLifeImageData = [this, source, captions](const cardwindow_ns::CardPtr& card) {
            bool caption{};
            auto image = renderCardImage(source, card, true, &caption);
            (*captions)[card.get()] = caption;
            return image;
        };
        // 取り込み用の表が所有するのは、編集中の Summary ではなく source
        // のオブジェクト。 エリア・戦闘行は既存の取り込み元対応のシーン・イベントのコール
        // バックへ渡す。 主シナリオ内の同じ CWXPath を解決するのではなく、Commons
        // が #source のキーを作り、対応する作業タブを選ぶようにする。
        import_comm.openAreaFrom = [this](
            const cardwindow_ns::SummaryPtr& source_summary,
            const ::cwxeditor::cwx::area::AbstractAreaPtr& value,
            const bool scene, const bool activate, const bool duplicate) {
            if (!source_summary || !value) return;
            const bool package = dynamic_cast<::cwxeditor::cwx::area::Package*>(
                value.get()) != nullptr;
            if (scene && !package) {
                if (area_table_context_.callbacks.open_scene_from) {
                    area_table_context_.callbacks.open_scene_from(
                        source_summary.get(), value.get(), activate, duplicate);
                }
            } else if (area_table_context_.callbacks.open_event_from) {
                area_table_context_.callbacks.open_event_from(
                    source_summary.get(), value.get(), activate, duplicate);
            }
        };
        auto editor = std::make_shared<cardwindow_ns::CardWindow>(
            std::move(import_comm), card_prop_, cardwindow_ns::CardWindowKind::ImportSource,
            modelParent, source,
            std::static_pointer_cast<::cwxeditor::cwx::path::CWXPath>(source),
            summary_);
        auto panel = std::make_shared<commons::TopLevelPanel>(editor->panel());
        panel->title(editor->title());
        panel->image(editor->image());
        panel->focusControl(editor->focusControl());
        panel->statusText(editor->statusText());

        String historyPath = history_ns::createHistString(source.get());
        if (historyPath.empty()) historyPath = source->scenarioPath();
        String resourceKey = "import:" + historyPath;
        if (historyPath.empty()) {
            resourceKey += source->scenarioName() + ":" +
                           std::to_string(dynamic_panes_.size());
        }
        dynamic_card_windows_[resourceKey] = editor;

        // 移植したエディタと、その TopLevelPanel の付加情報の両方を保持する。
        dynamic_panes_.push_back(modelParent);
        dynamic_panes_.push_back(editor);
        dynamic_panes_.push_back(panel);
        auto metadata = std::make_shared<commons::TLPData>(panel);
        metadata->main = editor;
        // SWT の Control.setData は TLPData を保持する。共通の
        // 登録先は弱参照なので、 ホスト所有の同じ動的エディタとともに付加情報を保持する。
        dynamic_panes_.push_back(metadata);
        commons::attachTLPData(editor->panel(), std::move(metadata));
        presentDockPanel(resourceKey, panel, "side", true);

        if (!historyPath.empty()) {
            history::addHistory(history::historySettings(prop_->var()),
                                OpenHistory{historyPath, source->type(),
                                            source->skinName(), {}},
                                prop_->var().importHistory.value,
                                prop_->var().importBookmarks.value,
                                services_.filesystem.get());
        }
    }
    const bool import_history_changed =
        !equalOpenHistories(initial_import_history, valueOf(prop_->var().importHistory)) ||
        !equalOpenHistories(initial_import_bookmarks, valueOf(prop_->var().importBookmarks));
    if (!owner_lifetime || !owner_lifetime->load(std::memory_order_acquire)) return;
    if (import_history_changed &&
        (!sendReloadPropsAndSave() || !owner_lifetime->load(std::memory_order_acquire))) return;
    if (comm_) comm_->refImportHistory.call();
    if (!owner_lifetime || !owner_lifetime->load(std::memory_order_acquire)) return;
    rebuildMenus();
}
void MainWindow::openCast() {
    if (docking_folder_ && docking_folder_->control("castCard")) {
        dock_.select("castCard");
        (void)docking_folder_->select("castCard", true);
    } else if (comm_) comm_->openCastWin(true);
}
void MainWindow::openSkill() {
    if (docking_folder_ && docking_folder_->control("skillCard")) {
        dock_.select("skillCard");
        (void)docking_folder_->select("skillCard", true);
    } else if (comm_) comm_->openSkillWin(true);
}
void MainWindow::openItem() {
    if (docking_folder_ && docking_folder_->control("itemCard")) {
        dock_.select("itemCard");
        (void)docking_folder_->select("itemCard", true);
    } else if (comm_) comm_->openItemWin(true);
}
void MainWindow::openBeast() {
    if (docking_folder_ && docking_folder_->control("beastCard")) {
        dock_.select("beastCard");
        (void)docking_folder_->select("beastCard", true);
    } else if (comm_) comm_->openBeastWin(true);
}
void MainWindow::openInfo() {
    if (docking_folder_ && docking_folder_->control("infoCard")) {
        dock_.select("infoCard");
        (void)docking_folder_->select("infoCard", true);
    } else if (comm_) comm_->openInfoWin(true);
}
void MainWindow::refreshAll(const SelectionEvent&) {
    const auto lifetime = lifecycle_alive_;
    const auto expected_summary = summary_;
    const auto expected_commons = comm_;
    const auto expected_properties = prop_;
    const auto expected_folder = docking_folder_;
    const auto host_current = [this, lifetime, expected_summary, expected_commons,
                               expected_properties, expected_folder] {
        return lifetime && lifetime->load(std::memory_order_acquire) &&
               summary_ == expected_summary && comm_ == expected_commons &&
               prop_ == expected_properties && docking_folder_ == expected_folder;
    };
    if (!host_current() || !expected_summary || !expected_commons) return;
    // D版の全体更新は、表示中の各ペインの引数なし操作を呼び、DirectoryWindow::refresh()
    // が JPY1/JPDC を再読み込みする。 C++版は refresh(SummaryPtr)
    // を経由するため、通知を振り分ける前にブースター一覧を再読み込みする。
    if (directory_window_ && expected_properties &&
        directory_window_->summary() == expected_summary)
        expected_summary->updateJpy1List(expected_properties->parent());
    if (!host_current()) return;
    expected_commons->refScenario.call(expected_summary);
    if (!host_current()) return;

    struct RefreshTarget {
        String pane_key;
        String control_key;
        std::shared_ptr<areawindow::MenuActionRegistry> registry;
        std::uint64_t generation{};
    };
    std::vector<RefreshTarget> targets;
    if (expected_folder) {
        for (const auto& pane_key : expected_folder->paneKeys()) {
            if (!host_current()) return;
            const auto control_key = expected_folder->selectedCtrl(pane_key);
            if (control_key.empty()) continue;
            const auto registry = selectedSceneMenuRegistry(control_key);
            if (!registry) continue;
            const auto entry = registry->entries().find(MenuID::Refresh);
            if (entry == registry->entries().end()) continue;
            const auto duplicate = std::find_if(targets.begin(), targets.end(),
                [&](const auto& target) { return target.registry == registry; });
            if (duplicate == targets.end())
                targets.push_back({pane_key, control_key, registry,
                                   entry->second.generation});
        }
    }
    for (const auto& target : targets) {
        if (!host_current()) return;
        if (!expected_folder ||
            expected_folder->selectedCtrl(target.pane_key) != target.control_key ||
            selectedSceneMenuRegistry(target.control_key) != target.registry)
            continue;
        const auto before = target.registry->entries().find(MenuID::Refresh);
        if (before == target.registry->entries().end() ||
            before->second.generation != target.generation)
            continue;
        (void)target.registry->dispatch(MenuID::Refresh);
        if (!host_current()) return;
        // 更新コールバックが同期的にシーンを無効化・置換する場合がある。 新しい
        // 接続先のペインや操作に対して、古いスナップショットの処理を続けない。
        if (expected_folder->selectedCtrl(target.pane_key) != target.control_key ||
            selectedSceneMenuRegistry(target.control_key) != target.registry)
            return;
        const auto after = target.registry->entries().find(MenuID::Refresh);
        if (after == target.registry->entries().end() ||
            after->second.generation != target.generation)
            return;
    }
    if (!host_current()) return;
    redrawAll();
}

void MainWindow::createPaneEvent(const ContainerPtr& parent, String paneKey) {
    static_cast<void>(parent);
    configureWallpaperForPane(paneKey);
    dock_.add(paneKey);
    tab_menus_.push_back(std::make_unique<TabMenu>(this, std::move(paneKey)));
    if (!tab_menus_.empty()) mountPaneControls(tab_menus_.back()->paneKey_);
    // ALL の初期配置後も、ビューの復元・移動などでペイ
    // ンが作られる場合がある。 同じ ALL の設定を再適
    // 用し、後から個別のタブ見出しが現れないようにする。
    configureMainDockLayout();
}

void MainWindow::configureWallpaperForPane(const StringView paneKey) {
    if (lib::editor_window_host_options().all_mode ||
        !paneKey.starts_with("work") || !docking_folder_) return;
    // コントロールのキーで tabView(paneKey) を探すと、空の
    // work ペインに壁紙の描画処理が設定されない。 ペイン自身の TabView
    // を取得し、D版と同様、空のクライアント領域を壁紙の描画先にする。
    const auto pane = docking_folder_->pane(paneKey);
    if (!pane) return;
    std::shared_ptr<lib::TabView> tabs;
    for (const auto& child : pane->children()) {
        if (auto candidate = std::dynamic_pointer_cast<lib::TabView>(child)) {
            tabs = std::move(candidate);
            break;
        }
    }
    if (!tabs) return;
    const std::weak_ptr<std::atomic_bool> life = lifecycle_alive_;
    tabs->on_empty_client_paint = [this, life](lib::DrawingContext& drawing,
                                               const lib::Rect client) {
        const auto alive = life.lock();
        if (!alive || !alive->load(std::memory_order_acquire) ||
            lib::editor_window_host_options().all_mode || !comm_ || !prop_) return;
        const auto image = comm_->wallpaper();
        if (!image) return;
        auto style_value = prop_->var().wallpaperStyle.value;
        if (style_value < static_cast<int>(structs::WallpaperStyle::Center) ||
            style_value > static_cast<int>(structs::WallpaperStyle::ExpandFull)) {
            style_value = static_cast<int>(structs::WallpaperStyle::Tile);
            prop_->var().wallpaperStyle.value = style_value;
        }
        dutils::drawWallpaper(drawing, image, client,
            static_cast<structs::WallpaperStyle>(style_value),
            prop_->images().imageSize(image));
    };
}

MainWindow::TabMenu* MainWindow::tabMenu(String paneKey) {
    for (const auto& item : tab_menus_)
        if (item && item->paneKey_ == paneKey) return item.get();
    return nullptr;
}

bool MainWindow::canOpenDirectory() const {
    if (!showScenarioFolderCommand() || !summary_ || !services_.shell) return false;
    // D版と同様、読み込み済みのすべての Summary で
    // OpenDir を有効にする。 読み込み後にパスが消
    // えても操作は利用可能にし、実行時の失敗は共通のシェルサ
    // ービスで伝える。 単独版コマンドとシェルサービスの検査
    // により、組み込みUIの方針と共通のOS境界を維持する。
    return true;
}
void MainWindow::openDirectory() {
    if (!canOpenDirectory()) return;
    if (!isAllMode() && docking_folder_ && directory_window_) {
        const auto panel = commons::getTopLevelPanel(docking_folder_->control(dock_.selected));
        if (panel && panel->shell() == directory_window_->shell()) {
            directory_window_->openDirectory();
            return;
        }
    }
    auto directory = pathFromUtf8(summary_->scenarioPath());
    std::error_code filesystem_error;
    if (std::filesystem::is_regular_file(directory, filesystem_error))
        directory = directory.parent_path();
    std::string error;
    if (!services_.shell->open_path(directory, error) && !error.empty()) {
        setStatusLine(error);
    }
}

bool MainWindow::canOpenBackupDirectory() const {
    if (!services_.shell || !prop_ || prop_->backupPath().empty()) return false;
    std::error_code error;
    return std::filesystem::is_directory(pathFromUtf8(prop_->backupPath()), error) && !error;
}
void MainWindow::openBackupDirectory() {
    if (!canOpenBackupDirectory()) return;
    std::string error;
    // D版は起動結果を捨てるが、C++版のステータス通知は維持する。 フ
    // ォルダーを開けなかった場合に、対処に使える理由をユーザーへ示す。
    if (!services_.shell->open_path(pathFromUtf8(prop_->backupPath()), error) && !error.empty())
        setStatusLine(error);
}

bool MainWindow::isMainCardWin(const PanelPtr& panel) const {
    return panel && comm_ && panel == comm_->openCardPane(0, false);
}

void MainWindow::showCardLife(const SelectionEvent&) {
    const auto selected = selectedCardWindow();
    if (selected && selected != cast_window_ && selected != skill_window_ &&
        selected != item_window_ && selected != beast_window_ &&
        selected != info_window_) {
        selected->showCardLife();
        refreshSelectionChrome(dock_.selected);
        return;
    }
    if (prop_) { prop_->var().cardLife = true; prop_->var().cardDetails = false; }
    for (const auto& window : {cast_window_, skill_window_, item_window_, beast_window_, info_window_})
        if (window) window->showCardLife();
    refreshSelectionChrome(dock_.selected);
}
void MainWindow::showCardList(const SelectionEvent&) {
    const auto selected = selectedCardWindow();
    if (selected && selected != cast_window_ && selected != skill_window_ &&
        selected != item_window_ && selected != beast_window_ &&
        selected != info_window_) {
        selected->showCardList();
        refreshSelectionChrome(dock_.selected);
        return;
    }
    if (prop_) { prop_->var().cardLife = false; prop_->var().cardDetails = false; }
    for (const auto& window : {cast_window_, skill_window_, item_window_, beast_window_, info_window_})
        if (window) window->showCardList();
    refreshSelectionChrome(dock_.selected);
}
void MainWindow::showCardTable(const SelectionEvent&) {
    const auto selected = selectedCardWindow();
    if (selected && selected != cast_window_ && selected != skill_window_ &&
        selected != item_window_ && selected != beast_window_ &&
        selected != info_window_) {
        selected->showCardTable();
        refreshSelectionChrome(dock_.selected);
        return;
    }
    if (prop_) { prop_->var().cardLife = false; prop_->var().cardDetails = true; }
    for (const auto& window : {cast_window_, skill_window_, item_window_, beast_window_, info_window_})
        if (window) window->showCardTable();
    refreshSelectionChrome(dock_.selected);
}

bool MainWindow::canNewCard() const { return summary_ != nullptr; }

String MainWindow::debugToolbarCustomizerState() const {
    const auto dialog = toolbar_custom_dialog_;
    const auto model = dialog ? dialog->dialog() : nullptr;
    return String("created=") + (dialog ? "1" : "0") +
           " visible=" + (model && model->visible() ? "1" : "0") +
           " controls=" +
           (dialog && dialog->customizer() && dialog->customizer()->widget()
                ? "1"
                : "0");
}

void MainWindow::newCast() {
    if (!canNewCard()) return;
    const auto selected = selectedCardWindow();
    if (selected && selected->canCreateCast()) selected->createCast();
    else if (cast_window_) cast_window_->createCast();
}
void MainWindow::newSkill() {
    if (!canNewCard()) return;
    const auto selected = selectedCardWindow();
    if (selected && selected->canCreateSkill()) selected->createSkill();
    else if (skill_window_) skill_window_->createSkill();
}
void MainWindow::newItem() {
    if (!canNewCard()) return;
    const auto selected = selectedCardWindow();
    if (selected && selected->canCreateItem()) selected->createItem();
    else if (item_window_) item_window_->createItem();
}
void MainWindow::newBeast() {
    if (!canNewCard()) return;
    const auto selected = selectedCardWindow();
    if (selected && selected->canCreateBeast()) selected->createBeast();
    else if (beast_window_) beast_window_->createBeast();
}
void MainWindow::newInfo() {
    if (!canNewCard()) return;
    const auto selected = selectedCardWindow();
    if (selected && selected->canCreateInfo()) selected->createInfo();
    else if (info_window_) info_window_->createInfo();
}
void MainWindow::versionInfo() {
    if (context_.versionInfo) {
        context_.versionInfo();
        return;
    }
    if (!prop_) return;
    smalldialogs::Callbacks callbacks;
    callbacks.openPath = [this](StringView path) {
        if (!services_.shell) return;
        std::string error;
        if (!services_.shell->open_path(structs::pathFromUtf8(path), error) && !error.empty())
            setStatusLine(error);
    };
    smalldialogs::VersionDialog dialog(*prop_, window_, std::move(callbacks),
                                       services_);
    (void)dialog.open();
}

void MainWindow::KeyDownFilter::handleEvent(lib::KeyEvent& event) {
    if (!owner_ || event.handled) return;
    // 単体版では編集可能な数値入力にフォーカスがある
    // 間、Ctrl+Sを含むキー処理をその入力欄に任
    // せ、主メニューのショートカットは実行しない。
    const auto focused = owner_->services_.gui
        ? owner_->services_.gui->focused_widget() : lib::WidgetPtr{};
    if (!lib::editor_window_host_options().all_mode) {
        const auto number = std::dynamic_pointer_cast<lib::NumberEdit>(focused);
        if (number && !number->read_only) return;
    }
    // D版の KeyDownFilter と同様、通常のキーは編集可能なテキストコントロール
    // のローカル処理に任せる。 セル編集中のEnterで EditProp を起動しない。
    const auto key_bits = static_cast<std::uint32_t>(event.key);
    const bool function_key = key_bits >= static_cast<std::uint32_t>(lib::Key::F1) &&
        key_bits <= static_cast<std::uint32_t>(lib::Key::F15);
    const bool control =
        (static_cast<std::uint32_t>(event.modifiers) &
         static_cast<std::uint32_t>(lib::Modifiers::Control)) != 0U;
    if (!control && !function_key) {
        if (const auto text = std::dynamic_pointer_cast<lib::TextEdit>(focused);
            text && !text->read_only) return;
        if (const auto combo = std::dynamic_pointer_cast<lib::ComboBox>(focused);
            combo && combo->editable) return;
    }
    const auto active = owner_->services_.gui
        ? owner_->services_.gui->active_input_window() : nullptr;
    bool detached = false;
    if (active && owner_->docking_folder_) {
        const auto shells = owner_->docking_folder_->subWindows();
        detached = std::find(shells.begin(), shells.end(), active) != shells.end();
    }
    const bool other_shell = active && active != owner_->window_;
    if (other_shell) {
        // D版の mainwindow.d の KeyDownFilter と同様、どのホストモ
        // ードでもプロパティのウィンドウが自身のコマンドを管理する。 ウィンドウ境界を越えられる
        // のは、所有者の全体コマンドと実際の DockingFolder のウィンドウだけ。 共
        // 通の切り離されたウィンドウには所有者への接続がないため、ドッキングモデルで識別する。
        bool modal_boundary = false;
        if (!detached) {
            auto shell = active;
            while (shell && shell != owner_->window_) {
                if (const auto dialog = std::dynamic_pointer_cast<lib::Dialog>(shell);
                    dialog && dialog->modal) modal_boundary = true;
                shell = shell->owner.lock();
            }
            if (shell != owner_->window_) return;
        }
        // フォーカスのあるコントロールと祖先の処理は、共通の表示側で実行
        // 済み。 次は、そのウィンドウ自身のメニューに処理の機会を渡す。
        if (lib::dispatch_menu_shortcuts(active, event.key, event.modifiers,
                                         event.printable, event.physical_control)) {
            event.handled = true;
            return;
        }
        if (modal_boundary) return;
        if (!detached) {
            const auto menu = owner_->menu_bar_;
            if (!menu) {
                event.handled = owner_->dispatchConfiguredPaneShortcut(event);
                return;
            }
            const auto item = dmenu::findMenu(*menu, static_cast<int>(event.key),
                0U, static_cast<std::uint32_t>(event.modifiers));
            if (!item) {
                event.handled = owner_->dispatchConfiguredPaneShortcut(event);
                return;
            }
            if (!::cwxeditor::cwx::menu::isGlobalMenu(item->data().id))
                return;
            SelectionEvent selection;
            selection.modifiers = event.modifiers;
            selection.physical_control = event.physical_control;
            selection.accelerator_modifiers = event.modifiers;
            selection.is_accelerator = true;
            item->trigger(selection);
            event.handled = true;
            return;
        }
    }
    // 主ウィンドウと分離したドッキング画面の入力は、既存の動的メニュー操作を
    // 使う。 入力の所有者がない場合は、独自描画の主エディタ画面として扱う。
    event.handled = lib::dispatch_menu_shortcuts(
        owner_->window_, event.key, event.modifiers, event.printable,
        event.physical_control);
    // ALL は composeRoot で表示用メニューツリーを意図的
    // に切り離すが、異なるのは表示だけ。 上の所有者の判断は共通にする。
    if (!event.handled && lib::editor_window_host_options().all_mode && owner_->menu_bar_)
        event.handled = lib::dispatch_menu_shortcut(
            owner_->menu_bar_, event.key, event.modifiers, event.printable,
            event.physical_control);
    if (!event.handled)
        event.handled = owner_->dispatchConfiguredPaneShortcut(event);
    // 代替経路も現在の設定済みキーに従う。 コマンドの割り当て変更では古い
    // キーを無効化し、Command/Ctrl の別名を勝手に追加しない。
    if (!event.handled && owner_->prop_) {
        for (const auto id : {MenuID::Copy, MenuID::Paste, MenuID::Cut,
                              MenuID::EditProp}) {
            if (!lib::menu_shortcut_matches(owner_->prop_->flex().menu.hotkey(id),
                                            event.key, event.modifiers, event.printable)) continue;
            if (owner_->canDispatchSelectedMenu(id)) {
                SelectionEvent selection;
                selection.modifiers = event.modifiers;
                event.handled = owner_->dispatchSelectedMenu(id, selection);
            }
            break;
        }
    }
}

void MainWindow::SwitchTab::spinUpDown(const lib::WheelEvent& event,
                                       const WidgetPtr& spinner) {
    if (!spinner) return;
    if (auto number = std::dynamic_pointer_cast<lib::NumberEdit>(spinner))
        number->value += event.delta_y < 0 ? number->step : -number->step;
}

void MainWindow::SwitchTab::handleEvent(lib::WheelEvent& event) {
    static_cast<void>(event);
    if (owner_) owner_->redrawAll();
}

void MainWindow::TabfPaint::paintControl(lib::Rect bounds) {
    if (owner_ && owner_->root_) owner_->root_->set_bounds(bounds);
}

MainWindow::TabMenu::TabMenu(MainWindow* owner, String paneKey)
    : owner_(owner), paneKey_(std::move(paneKey)),
      menu_(std::make_shared<lib::Menu>(true)) {
    menu_->on_open = [this] { rebuildMenu(); };
    rebuildMenu();
    if (owner_ && owner_->docking_folder_)
        owner_->docking_folder_->setMenu(paneKey_, menu_);
}

String MainWindow::TabMenu::controlKey() const {
    return owner_ && owner_->docking_folder_
        ? owner_->docking_folder_->selectedCtrl(paneKey_) : String{};
}

void MainWindow::TabMenu::rebuildMenu() {
    if (!menu_) return;
    menu_->items.clear();
    const auto add = [this](const MenuID id, lib::Action action,
                            const bool enabled) {
        lib::MenuItem item{};
        item.text = owner_ ? owner_->menuText(id) : String{};
        item.command = std::to_string(static_cast<std::uint32_t>(id));
        item.enabled = enabled;
        item.action = std::move(action);
        menu_->items.push_back(std::move(item));
    };
    const auto separator = [this] {
        lib::MenuItem item{};
        item.separator = true;
        menu_->items.push_back(std::move(item));
    };
    add(MenuID::CreateSubWindow,
        [this] { createSubWindow(SelectionEvent{}); }, canCreateSubWindow());
    separator();
    add(MenuID::LeftPane, [this] { leftTab(SelectionEvent{}); }, canWalkTab());
    add(MenuID::RightPane, [this] { rightTab(SelectionEvent{}); }, canWalkTab());
    separator();
    add(MenuID::ClosePane, [this] { close(SelectionEvent{}); }, canClose());
    separator();
    add(MenuID::ClosePaneExcept,
        [this] { closeEtc(SelectionEvent{}); }, canCloseEtc());
    add(MenuID::ClosePaneLeft,
        [this] { closeLeft(SelectionEvent{}); }, canCloseLeft());
    add(MenuID::ClosePaneRight,
        [this] { closeRight(SelectionEvent{}); }, canCloseRight());
    separator();
    add(MenuID::ClosePaneAll,
        [this] { closeAll(SelectionEvent{}); }, canClose());
    ++menu_->item_update_revision;
    ++menu_->item_order_revision;
}

bool MainWindow::TabMenu::canCreateSubWindow() const {
    return owner_ && owner_->docking_folder_ && !controlKey().empty() &&
           !owner_->docking_folder_->isSinglePane(paneKey_);
}
bool MainWindow::TabMenu::canWalkTab() const {
    return owner_ && owner_->docking_folder_ && !controlKey().empty() &&
           owner_->docking_folder_->controlCount(paneKey_) > 1;
}
bool MainWindow::TabMenu::canClose() const { return !controlKey().empty(); }
bool MainWindow::TabMenu::canCloseEtc() const {
    const auto key = controlKey();
    return owner_ && owner_->docking_folder_ && !key.empty() &&
           owner_->docking_folder_->hasEtc(key);
}
bool MainWindow::TabMenu::canCloseLeft() const {
    const auto key = controlKey();
    return owner_ && owner_->docking_folder_ && !key.empty() &&
           owner_->docking_folder_->hasLeft(key);
}
bool MainWindow::TabMenu::canCloseRight() const {
    const auto key = controlKey();
    return owner_ && owner_->docking_folder_ && !key.empty() &&
           owner_->docking_folder_->hasRight(key);
}
void MainWindow::TabMenu::createSubWindow(const SelectionEvent&) {
    const auto key = controlKey();
    if (owner_ && owner_->docking_folder_ && !key.empty())
        owner_->docking_folder_->createSubWindow(key);
}
void MainWindow::TabMenu::leftTab(const SelectionEvent&) {
    const auto key = controlKey();
    if (owner_ && owner_->docking_folder_ && !key.empty())
        (void)owner_->docking_folder_->walkLeft(key);
}
void MainWindow::TabMenu::rightTab(const SelectionEvent&) {
    const auto key = controlKey();
    if (owner_ && owner_->docking_folder_ && !key.empty())
        (void)owner_->docking_folder_->walkRight(key);
}
void MainWindow::TabMenu::close(const SelectionEvent&) {
    const auto key = controlKey();
    if (owner_ && !key.empty()) (void)owner_->dockCloseCtrl(key);
}
void MainWindow::TabMenu::closeEtc(const SelectionEvent&) {
    const auto key = controlKey();
    if (owner_ && owner_->docking_folder_ && !key.empty())
        owner_->docking_folder_->closeEtc(key);
}
void MainWindow::TabMenu::closeLeft(const SelectionEvent&) {
    const auto key = controlKey();
    if (owner_ && owner_->docking_folder_ && !key.empty())
        owner_->docking_folder_->closeLeft(key);
}
void MainWindow::TabMenu::closeRight(const SelectionEvent&) {
    const auto key = controlKey();
    if (owner_ && owner_->docking_folder_ && !key.empty())
        owner_->docking_folder_->closeRight(key);
}
void MainWindow::TabMenu::closeAll(const SelectionEvent&) {
    const auto key = controlKey();
    if (owner_ && owner_->docking_folder_ && !key.empty())
        owner_->docking_folder_->closeAll(key);
}

// D版の dutils.getSymbols() に相当する、検索結果オブジェクトの文字列とアイコン。
bool MainWindow::searchResultSymbols(const ::cwxeditor::cwx::path::CWXPath& source,
                                     const bool desc, String& text,
                                     lib::ResourceId& image,
                                     const ::cwxeditor::cwx::summary::Summary* symbols_summary) {
    dutils::EditorContext context;
    context.prop = prop_;
    context.skin = skin_.get();
    context.summary = symbols_summary ? symbols_summary : summary_.get();
    context.services = services_;
    return dutils::getSymbols(context, context.summary, &source, desc, text, image);
}

bool MainWindow::openCWXPath(String path, const bool shellActivate) {
    const auto routed = routeCWXPath(path, shellActivate);
    if (routed) return *routed;
    if (!comm_) return false;
    const bool activate = shellActivate &&
        !::cwxeditor::cwx::path::cphasattr(path, "nofocus");
    return comm_->openCWXPathFallback(std::move(path), activate);
}

std::optional<bool> MainWindow::routeCWXPath(String path,
                                             const bool shellActivate) {
    if (!summary_ || path.empty()) return false;
    const auto host_lifetime = lifecycle_alive_;
    const auto host_current = [host_lifetime] {
        return host_lifetime && host_lifetime->load(std::memory_order_acquire);
    };
    const auto first_separator = path.find(::cwxeditor::cwx::path::CWXPATH_SEP);
    if (first_separator != String::npos) {
        bool opened_all = true;
        std::size_t begin{};
        while (begin <= path.size()) {
            const auto end = path.find(::cwxeditor::cwx::path::CWXPATH_SEP, begin);
            const auto part = path.substr(begin, end == String::npos ? String::npos : end - begin);
            const bool opened = openCWXPath(part, shellActivate);
            // パスを開く処理が、このホストを同期的に閉じる場合がある。 無効な
            // MainWindow を通じて次の要素を処理する前に止める。
            if (!host_current()) return false;
            if (!opened) opened_all = false;
            if (end == String::npos) break;
            begin = end + ::cwxeditor::cwx::path::CWXPATH_SEP.size();
        }
        return opened_all;
    }
    bool opened = false;
    bool routed = false;
    std::shared_ptr<commons::TopLevelPanel> openedPanel;
    const bool activate = shellActivate &&
        !::cwxeditor::cwx::path::cphasattr(path, "nofocus");

    // D版と同様、エリア・戦闘・パッケージのパスはデータビューを経由する。
    // 対応するシーン・イベントのペインを作ってから、所有者より下のパス
    // をそのペインへ渡す。 Commons::openCWXPath は
    // 既存のペインを有効化できるが、パス固有のペインを単独では作れない。
    if (summary_ && comm_ && prop_) {
        const auto category = lower(::cwxeditor::cwx::path::cpcategory(path));
        const auto index = ::cwxeditor::cwx::path::cpindex(path);
        const bool by_id = category.ends_with(":id");
        String child = ::cwxeditor::cwx::path::cpbottom(path);
        const auto child_category =
            lower(::cwxeditor::cwx::path::cpcategory(child));
        bool scene = ::cwxeditor::cwx::path::cpempty(child) ||
                     (((child_category == "menucard") ||
                       (child_category == "enemycard")) &&
                      ::cwxeditor::cwx::path::cpempty(
                          ::cwxeditor::cwx::path::cpbottom(child))) ||
                     (child_category == "menucard" &&
                      ::cwxeditor::cwx::path::cpcategory(
                          ::cwxeditor::cwx::path::cpbottom(child)) == "name") ||
                     child_category == "background";
        if (scene && ::cwxeditor::cwx::path::cphasattr(child, "eventview"))
            scene = false;

        // D版と同様、名前一覧の履歴ルートを、対応する具体的な NameWindow
        // へ渡す。 単独版の経路と、対象外の ALL ホストを分ける。
        const std::weak_ptr<std::atomic_bool> name_route_life = lifecycle_alive_;
        const auto name_route_current = [name_route_life] {
            const auto alive = name_route_life.lock();
            return alive && alive->load();
        };
        const auto route_name = [&](const auto& member, auto open_window) {
            routed = true;
            const auto name_window = member;
            if (!name_window) return;
            (comm_->*open_window)(shellActivate);
            if (!name_route_current()) return;
            openedPanel = commons::getTopLevelPanel(name_window->shell());
            // D版の名前の経路と同様、shellActivate をそのまま渡す。 パス
            // 属性で、具体的な NameWindow のフォーカス要求を上書きしない。
            opened = name_window->openCWXPath(category, shellActivate);
        };
        if (!lib::editor_window_host_options().all_mode) {
            if (category == "couponview") route_name(coupon_window_, &commons::Commons::openCouponWin);
            else if (category == "gossipview") route_name(gossip_window_, &commons::Commons::openGossipWin);
            else if (category == "completestampview") route_name(complete_stamp_window_, &commons::Commons::openCompleteStampWin);
            else if (category == "keycodeview") route_name(key_code_window_, &commons::Commons::openKeyCodeWin);
            else if (category == "cellnameview") route_name(cell_name_window_, &commons::Commons::openCellNameWin);
            else if (category == "cardgroupview") route_name(card_group_window_, &commons::Commons::openCardGroupWin);
            if (!name_route_current()) return false;
        }
        std::shared_ptr<cardwindow_ns::CardWindow> cards;
        if (category == "fileview") {
            // D版の MainWindow と同様、この公開パスは DirectoryWindow
            // へ渡し、openCWXPath でファイルのコントロールを選ぶ。 その共
            // 通経路の前に共通のペイン表示処理を通し、単独版と組み込みUIで共有する。
            routed = true;
            if (directory_window_) {
                comm_->openDirWin(activate);
                if (!host_current()) return false;
                openedPanel = commons::getTopLevelPanel(directory_window_->shell());
                opened = directory_window_->openCWXPath(path, shellActivate);
                if (!host_current()) return false;
            }
        } else if (category == "tableview") {
            // D版の tableview の移動と同様、現在のディレクトリ・選
            // 択を保持し、既存の表へフォーカスを移すだけにする。 共通の Data
            // ペインを開き、状態を書き換えずに同じフォーカス処理を使う。
            routed = true;
            if (area_table_) {
                comm_->openDataWin(activate);
                if (!host_current()) return false;
                openedPanel = commons::getTopLevelPanel(area_table_->panel());
                const auto table = area_table_->table();
                opened = static_cast<bool>(table);
                if (opened) dutils::forceFocus(table, shellActivate, services_);
                if (!host_current()) return false;
            }
        } else if (category == "summary" ||
            ::cwxeditor::cwx::path::cpempty(path)) {
            // D版の Summary のルートは空。検索結果の受け渡しには空でない対象
            // が必要なので、 共通APIでは summary を明示的な別名とする。
            routed = true;
            if (::cwxeditor::cwx::path::cpempty(child) && area_table_) {
                comm_->openDataWin(activate);
                if (!host_current()) return false;
                openedPanel = commons::getTopLevelPanel(area_table_->panel());
                area_table_->setDirectory({});
                area_table_->selectSummary();
                opened = openedPanel && area_table_->summarySelected();
                if (opened && !::cwxeditor::cwx::path::cphasattr(path, "nofocus") && services_.gui)
                    services_.gui->focus(area_table_->summarySelectionControl());
                if (!host_current()) return false;
                if (opened && ::cwxeditor::cwx::path::cphasattr(path, "opendialog") &&
                    card_comm_.editSummary) card_comm_.editSummary();
                if (!host_current()) return false;
            }
        } else if (category == "castcard" || category == "castcard:id" || category == "castcardview") {
            cards = cast_window_; openedPanel = commons::getTopLevelPanel(comm_->openCastWin(activate)); routed = true;
        } else if (category == "skillcard" || category == "skillcard:id" || category == "skillcardview") {
            cards = skill_window_; openedPanel = commons::getTopLevelPanel(comm_->openSkillWin(activate)); routed = true;
        } else if (category == "itemcard" || category == "itemcard:id" || category == "itemcardview") {
            cards = item_window_; openedPanel = commons::getTopLevelPanel(comm_->openItemWin(activate)); routed = true;
        } else if (category == "beastcard" || category == "beastcard:id" || category == "beastcardview") {
            cards = beast_window_; openedPanel = commons::getTopLevelPanel(comm_->openBeastWin(activate)); routed = true;
        } else if (category == "infocard" || category == "infocard:id" || category == "infocardview") {
            cards = info_window_; openedPanel = commons::getTopLevelPanel(comm_->openInfoWin(activate)); routed = true;
        } else if (category == "variable" || category == "variableview") {
            routed = true;
            openedPanel = commons::getTopLevelPanel(comm_->openFlagWin(activate));
            if (!host_current()) return false;
            opened = openedPanel && flags_pane_ &&
                flags_pane_->openCWXPath(child, shellActivate);
            if (!host_current()) return false;
        }
        if (!host_current()) return false;
        if (cards) {
            opened = openedPanel && cards->openCWXPath(path, shellActivate);
            if (!host_current()) return false;
        }

        const auto select_shallow_owner = [&](const auto& owner) {
            if (!owner || !area_table_ || !::cwxeditor::cwx::path::cpempty(child) ||
                !::cwxeditor::cwx::path::cphasattr(child, "shallow")) return false;
            comm_->openDataWin(activate);
            // データペインの表示処理が MainWindow を同期的に無効化
            // する場合がある。 この浅い経路は処理済みとして扱い、呼び出し元
            // が破棄済みの Commons へ代替処理を行わないようにする。
            if (!host_current()) return true;
            openedPanel = commons::getTopLevelPanel(area_table_->panel());
            opened = openedPanel && area_table_->select(owner.get());
            if (!host_current()) return true;
            if (opened && !::cwxeditor::cwx::path::cphasattr(child, "nofocus") && services_.gui)
                services_.gui->focus(area_table_->table());
            if (!host_current()) return true;
            if (opened) comm_->refreshToolBar();
            return true;
        };
        if (category == "area" || category == "area:id") {
            routed = true;
            const auto owner = by_id
                                   ? summary_->area(index)
                                   : index < summary_->areas().size()
                                         ? summary_->areas()[index]
                                         : ::cwxeditor::cwx::area::AreaPtr{};
            if (owner && !select_shallow_owner(owner)) {
                const auto panel = scene
                    ? comm_->openAreaScene(*prop_, summary_.get(), owner,
                                           activate, nullptr)
                    : comm_->openAreaEvent(*prop_, summary_.get(), owner,
                                           activate, nullptr);
                if (!host_current()) return false;
                openedPanel = panel;
                opened = panel &&
                         (::cwxeditor::cwx::path::cpempty(child) ||
                          panel->openCWXPath(child, shellActivate));
                if (!host_current()) return false;
            }
        } else if (category == "battle" || category == "battle:id") {
            routed = true;
            const auto owner = by_id
                                   ? summary_->battle(index)
                                   : index < summary_->battles().size()
                                         ? summary_->battles()[index]
                                         : ::cwxeditor::cwx::area::BattlePtr{};
            if (owner && !select_shallow_owner(owner)) {
                const auto panel = scene
                    ? comm_->openAreaScene(*prop_, summary_.get(), owner,
                                           activate, nullptr)
                    : comm_->openAreaEvent(*prop_, summary_.get(), owner,
                                           activate, nullptr);
                if (!host_current()) return false;
                openedPanel = panel;
                opened = panel &&
                         (::cwxeditor::cwx::path::cpempty(child) ||
                          panel->openCWXPath(child, shellActivate));
                if (!host_current()) return false;
            }
        } else if (category == "package" || category == "package:id") {
            routed = true;
            const auto owner = by_id
                                   ? summary_->cwPackage(index)
                                   : index < summary_->packages().size()
                                         ? summary_->packages()[index]
                                         : ::cwxeditor::cwx::area::PackagePtr{};
            if (owner && !select_shallow_owner(owner)) {
                const auto panel = comm_->openArea(
                    *prop_, summary_.get(), owner, activate, nullptr);
                if (!host_current()) return false;
                openedPanel = panel;
                opened = panel &&
                         (::cwxeditor::cwx::path::cpempty(child) ||
                          panel->openCWXPath(child, shellActivate));
                if (!host_current()) return false;
            }
        }
    }

    if (!host_current()) return false;
    if (!routed) return std::nullopt;
    if (opened) {
        // シーンとイベントを選ぶための所有者・子の分割は実装上の
        // 都合。 D版と同様、eventview などの属性を含
        // む元の完全なパスを復元できるよう、 所有者のルートだ
        // けでなく、実際に開いたペインに完全なパスを保持する。
        String rememberedPath = ::cwxeditor::cwx::path::cpbody(path);
        const auto attributes = ::cwxeditor::cwx::path::cpattr(path);
        for (auto it = attributes.rbegin(); it != attributes.rend(); ++it) {
            if (*it != "nofocus")
                rememberedPath = ::cwxeditor::cwx::path::cpaddattr(
                    rememberedPath, *it);
        }
        if (openedPanel) openedPanel->rememberCWXPath(rememberedPath);
        if (std::find(opened_paths_.begin(), opened_paths_.end(), rememberedPath) ==
            opened_paths_.end())
            opened_paths_.push_back(rememberedPath);
        commons::TopLevelPanel::setOpenedCWXPath(opened_paths_);
        return true;
    }
    return false;
}

std::vector<String> MainWindow::openedCWXPath() const {
    const std::weak_ptr<std::atomic_bool> life = lifecycle_alive_;
    // 外部コールバックである子のパス取得処理を呼ぶ前に、メンバーを複製する。
    const auto gui = services_.gui;
    const auto selfIdentity =
        static_cast<const commons::TopLevelPanel*>(this);
    std::vector<std::pair<std::shared_ptr<commons::TopLevelPanel>, bool>> panels;
    if (docking_folder_) {
        for (const auto& paneKey : docking_folder_->paneKeys()) {
            for (const auto& control : docking_folder_->controls(paneKey))
                panels.emplace_back(commons::getTopLevelPanel(control), false);
            panels.emplace_back(commons::getTopLevelPanel(docking_folder_->control(
                                    docking_folder_->selectedCtrl(paneKey))), true);
        }
    }
    std::vector<String> result;
    const auto append = [&life, selfIdentity](
        std::vector<String>& target,
        const std::shared_ptr<commons::TopLevelPanel>& panel,
        const bool selected) {
        // D版と同様、タブとフォーカスの両方の走査で自身の TopLevelPanel を除
        // 外する。 祖先の付加情報により、未登録の子が Main に解決される場合がある。
        if (!panel || panel.get() == selfIdentity) return true;
        // SWTの取得処理にはコールバックの境界がないが、共通の提供処理は Main のホストを無
        // 効化する場合がある。 その一回の呼び出しをまたいで使うのは、スナップショットだけとする。
        const auto paths = panel->openedCWXPath();
        const auto alive = life.lock();
        if (!alive || !alive->load()) return false;
        String owner;
        for (const auto& source : paths) {
            const auto category = lower(path::cpcategory(source));
            if (category == "area" || category == "area:id" ||
                category == "battle" || category == "battle:id" ||
                category == "package" || category == "package:id") {
                owner = path::cpbody(path::cptop(source));
                break;
            }
        }
        for (const auto& source : paths) {
            if (source.empty()) continue;
            String normalized = source;
            const auto category = lower(path::cpcategory(source));
            if (!owner.empty() &&
                (category == "event" || category == "playercard" ||
                 category == "menucard" || category == "enemycard" ||
                 category == "background")) {
                normalized = owner + "/" + source;
            }
            // ペインには修正済みの絶対パスと古い相対の子パスが両方残
            // る場合がある。 完全なパスだけを保存し、フォーカス属
            // 性を一度だけ組み直して、 再起動の繰り返しで ;nofocus;nofocus
            // と重ならないようにする。
            String value = path::cpbody(normalized);
            const auto attributes = path::cpattr(normalized);
            for (auto it = attributes.rbegin(); it != attributes.rend(); ++it)
                if (*it != "nofocus") value = path::cpaddattr(value, *it);
            if (!selected) value = path::cpaddattr(value, "nofocus");
            if (std::find(target.begin(), target.end(), value) == target.end())
                target.push_back(value);
        }
        return true;
    };

    // D版の MainWindow.openedCWXPath と同
    // 様、配置済みの各タブのフォーカスなしのパスを追加し、 各ペイン
    // の選択タブは通常のパスも追加する。これによりタブ集合とペインの
    // 選択を復元する。 Commons のキャッシュではなく現在のド
    // ックを走査し、閉じたタブが次の保存履歴から消えるようにする。
    for (const auto& [panel, selected] : panels)
        if (!append(result, panel, selected)) return {};

    // D版と同様、各ペインの選択パスの後に、表示全体でフォーカスのあるコントロールを
    // 含む TopLevelPanel を追加する。 最後の入力対象を保持するため。
    if (!lib::editor_window_host_options().all_mode && gui) {
        const auto focused = gui->focused_widget();
        const auto alive = life.lock();
        if (!alive || !alive->load()) return {};
        const auto focusedPanel = commons::getTopLevelPanel(focused);
        if (focusedPanel && focusedPanel.get() != selfIdentity) {
            std::vector<String> focusedPaths;
            if (!append(focusedPaths, focusedPanel, true)) return {};
            // append() は履歴全体の重複を除くため、すでにある
            // 通常のフォーカスパスは末尾へ移す。 重複除去によって、D
            // 版の最後のフォーカスペインの順序が失われないようにする。
            for (auto& pathValue : focusedPaths) {
                result.erase(std::remove(result.begin(), result.end(), pathValue),
                             result.end());
                result.push_back(std::move(pathValue));
            }
        }
    }
    return result;
}

std::shared_ptr<ReplaceDialogState> MainWindow::openReplWin() {
    if (!replace_dialog_) replace_dialog_ = std::make_shared<ReplaceDialogState>();
    return replace_dialog_;
}

void MainWindow::reNumberingAll() {
    if (!summary_) return;
    if (services_.dialogs && prop_) {
        lib::MessageDialogRequest request;
        request.title = prop_->requiredMsgs().dlgTitQuestion.value;
        request.message = prop_->requiredMsgs().reNumberingAll.value;
        request.severity = lib::MessageSeverity::Question;
        request.buttons = lib::MessageButtons::OKCancel;
        if (services_.dialogs->message(request) != lib::MessageResult::OK) return;
    }

    if (area_table_) area_table_->reNumberingAll();
    for (const auto& window :
         {cast_window_, skill_window_, item_window_, beast_window_, info_window_}) {
        if (window) window->reNumberingAll();
    }
    for (const auto& cast : summary_->casts()) {
        if (!cast) continue;
        // D版と同様、開いている編集可能な手札ウィンドウに委譲し、番号変更を
        // そのペインの履歴に記録する。 手札が閉じている場合は直接処理する。
        std::shared_ptr<cardwindow_ns::CardWindow> hand;
        for (const auto& [key, weak] : dynamic_card_windows_) {
            auto current = weak.lock();
            if (current && current->kind() == cardwindow_ns::CardWindowKind::Hand &&
                current->summary() == summary_ && current->owner().get() == cast.get()) {
                hand = std::move(current);
                break;
            }
        }
        if (hand) { hand->reNumberingAll(); continue; }
        auto skills = cast->skills();
        auto items = cast->items();
        auto beasts = cast->beasts();
        reNumberingHands(skills);
        reNumberingHands(items);
        reNumberingHands(beasts);
    }
    if (comm_) comm_->refreshToolBar();
}

void MainWindow::dismissPreviousScenarioLoading() {
    previous_scenario_loading_label_.reset();
    auto dialog = std::exchange(previous_scenario_loading_, {});
    const bool modal = std::exchange(scenario_loading_modal_, false);
    if (!dialog) return;
    if (services_.gui) {
        if (modal) services_.gui->end_modal(dialog);
        else services_.gui->close(dialog);
    }
    else dialog->set_visible(false);
}

void MainWindow::cancelScenarioLoadTimeout() {
    const auto timer = std::exchange(scenario_load_timeout_timer_, {});
    if (timer && services_.dispatcher) services_.dispatcher->cancel_timer(timer);
}

void MainWindow::cancelScenarioLoading() {
    stopBackgroundLoads();
    setStatusLine("シナリオの読み込みを中止しました。");
}

void MainWindow::showScenarioLoading(
    String message, std::shared_ptr<lib::Dialog>* created_dialog) {
    if (created_dialog) created_dialog->reset();
    dismissPreviousScenarioLoading();
    auto dialog = std::make_shared<lib::Dialog>();
    dialog->title = "シナリオ読み込み";
    dialog->owner = window_;
    dialog->center_on_owner = true;
    dialog->modal = false;
    dialog->minimizable = false;
    dialog->maximizable = false;
    // ALL の読み込み通知の見出しには、取り消し・やり直し、
    // 最小化、閉じるボタンを付けない。 通知は完了時に閉じる。
    dialog->caption_buttons = false;
    dialog->set_bounds({0, 0, 640, 360});
    auto layout = std::make_shared<lib::LinearLayout>(lib::Orientation::Vertical);
    layout->padding = {16, 16, 16, 16};
    dialog->set_layout(layout);
    previous_scenario_loading_message_ = message;
    auto label = std::make_shared<lib::Label>(std::move(message));
    label->wrap = true;
    label->set_layout_item({.horizontal = lib::Align::Stretch, .grow = 1.0F});
    dialog->add(label);
    const std::weak_ptr<lib::Dialog> weak = dialog;
    const auto cancel = [this, weak] {
        if (const auto current = weak.lock(); current && current == previous_scenario_loading_)
            cancelScenarioLoading();
    };
    dialog->on_close_requested = cancel;
    auto button = std::make_shared<lib::Button>("中止");
    button->cancel_button = true;
    button->on_click = cancel;
    button->set_layout_item({.horizontal = lib::Align::End, .preferred = {96, 28}});
    dialog->add(button);
    previous_scenario_loading_ = dialog;
    previous_scenario_loading_label_ = label;
    // 再描画・表示側のコールバックが再入して新しい読み込み通知
    // を設定する前に、 このインスタンスそのものを公開する。
    if (created_dialog) *created_dialog = dialog;
    setScenarioLoadingPhase("なし", "読み込みの準備", "シナリオファイルの読み込み");
    if (services_.gui) {
        // 進捗・中止ダイアログは同期的なモーダル待ちを追加せず、 共通
        // の非同期モーダル処理で編集中の画面だけを入力対象から外す。
        // ホスト全体で入力を遮断すると、中止ボタンも操作できなくなる。
        scenario_loading_modal_ = services_.gui->begin_modal(dialog, window_);
        if (!scenario_loading_modal_) services_.gui->show(dialog);
    }
}

void MainWindow::setScenarioLoadingPhase(String finished, String current, String next) {
    if (!previous_scenario_loading_ || !previous_scenario_loading_label_) return;
    previous_scenario_loading_label_->text = previous_scenario_loading_message_ +
        (finished.empty() ? String{} : "\n\n終了したフェーズ: " + finished) +
        "\n現在の処理: " + current +
        (next.empty() ? String{} : "\n次のフェーズ: " + next);
    if (services_.gui) services_.gui->request_redraw(previous_scenario_loading_);
    lib::editor_action_log("Scenario loading phase finished=\"" + finished +
        "\" current=\"" + current + "\" next=\"" + next + "\"");
}

void MainWindow::showScenarioLoadError(const String& message, lib::Action after_dismiss) {
    // 単独版の読み込み失敗は、通知処理をブロックしないモーダル表示を行う。
    // ALL とタイムアウトの通知は、既存のモードレス経路を維持する。
    const std::weak_ptr<std::atomic_bool> alive = lifecycle_alive_;
    const auto generation = load_generation_;
    const auto gui = services_.gui;
    const auto owner = window_;
    dismissPreviousScenarioLoading();
    const auto gate = alive.lock();
    if (!gate || !gate->load(std::memory_order_acquire) ||
        close_pending_ || generation != load_generation_ ||
        window_ != owner || services_.gui != gui) return;

    const bool request_modal = static_cast<bool>(after_dismiss) && !isAllMode() && gui;
    auto dialog = std::make_shared<lib::Dialog>();
    dialog->defer_modal_end_until_top = request_modal;
    dialog->title = "シナリオの読み込みに失敗しました";
    dialog->owner = owner;
    dialog->center_on_owner = true;
    dialog->modal = request_modal;
    dialog->minimizable = false;
    dialog->maximizable = false;
    dialog->set_bounds({0, 0, 560, 200});
    auto layout = std::make_shared<lib::LinearLayout>(lib::Orientation::Vertical);
    layout->padding = {16, 16, 16, 16};
    layout->spacing = 12;
    dialog->set_layout(layout);
    auto label = std::make_shared<lib::Label>(message.empty()
        ? "シナリオを読み込めませんでした。" : message);
    label->wrap = true;
    label->set_layout_item({.horizontal = lib::Align::Stretch, .grow = 1.0F});
    dialog->add(label);

    struct WarningState {
        bool beginning{};
        bool modal{};
        bool close_requested{};
        bool closed{};
        lib::Action after_dismiss;
        std::shared_ptr<lib::GuiBackend> gui;
    };
    const auto state = std::make_shared<WarningState>();
    state->beginning = request_modal;
    state->after_dismiss = std::move(after_dismiss);
    state->gui = gui;
    const std::weak_ptr<lib::Dialog> weak = dialog;
    const auto clear_current = [alive, this, generation, weak] {
        const auto current_gate = alive.lock();
        if (!current_gate || !current_gate->load(std::memory_order_acquire) ||
            close_pending_ || generation != load_generation_) return false;
        const auto current_dialog = weak.lock();
        if (!current_dialog || previous_scenario_loading_ != current_dialog) return false;
        previous_scenario_loading_.reset();
        previous_scenario_loading_label_.reset();
        scenario_loading_modal_ = false;
        return true;
    };
    const auto close = [state, weak, clear_current] {
        if (state->closed) return;
        if (state->beginning) {
            // begin_modal が同期的に表示・終了する場合がある。
            // 結果が分かるまで、セッションの終了処理と後続処理を遅らせる。
            state->close_requested = true;
            return;
        }
        state->closed = true;
        const bool was_current = clear_current();
        auto after = std::exchange(state->after_dismiss, {});
        if (const auto current = weak.lock()) {
            if (state->gui) {
                if (state->modal) {
                    if (was_current) state->gui->end_modal(current, std::move(after));
                    else state->gui->end_modal(current);
                    return;
                }
                state->gui->close(current);
            } else {
                current->set_visible(false);
            }
        }
        if (was_current && after) after();
    };
    dialog->on_close_requested = close;
    auto button = std::make_shared<lib::Button>("閉じる");
    button->default_button = true;
    button->cancel_button = true;
    button->on_click = close;
    button->set_layout_item({.horizontal = lib::Align::End, .preferred = {96, 28}});
    dialog->add(button);
    previous_scenario_loading_ = dialog;
    if (!gui) {
        state->beginning = false;
        state->closed = true;
        if (auto after = std::exchange(state->after_dismiss, {})) after();
        return;
    }
    if (!request_modal) {
        gui->show(dialog);
        return;
    }

    bool presented{};
    try {
        presented = gui->begin_modal(dialog, owner);
    } catch (...) {
        // 開始途中のモーダルセッションは、遅延された最上位スタックの後処理も含め
        // てバックエンドが管理する。 新しいダイアログや無効な MainWindow
        // に触れず、このローカルの継続処理だけを無効化してから再送出する。
        state->beginning = false;
        state->closed = true;
        state->after_dismiss = {};
        (void)clear_current();
        throw;
    }
    state->beginning = false;
    state->modal = presented;
    const auto after_begin_gate = alive.lock();
    if (!after_begin_gate || !after_begin_gate->load(std::memory_order_acquire) ||
        close_pending_ || generation != load_generation_ ||
        window_ != owner || services_.gui != gui) {
        state->closed = true;
        state->after_dismiss = {};
        if (presented) gui->end_modal(dialog);
        else gui->close(dialog);
        return;
    }
    if (previous_scenario_loading_ != dialog || window_ != owner || services_.gui != gui) {
        state->closed = true;
        state->after_dismiss = {};
        if (presented) gui->end_modal(dialog);
        else gui->close(dialog);
        return;
    }
    scenario_loading_modal_ = presented;
    if (state->close_requested) {
        close();
        return;
    }
    if (!presented) {
        // 非同期モーダルに未対応のバックエンドでも警告を表示し
        // 、 警告の後にステータスを更新する順序を維持する。
        dialog->modal = false;
        gui->show(dialog);
    }
}

void MainWindow::dispatchUiEvent(const lib::Action& event) {
    const auto lifetime = lifecycle_alive_;
    if (!event || !lifetime || !lifetime->load(std::memory_order_acquire)) return;
    try {
        event();
    } catch (const lib::RecoverableUiError& error) {
        if (!lifetime->load(std::memory_order_acquire)) return;
        lib::editor_action_log(String("Editor operation rolled back: ") + error.what());
        try { setStatusLine(String("操作を取り消しました: ") + error.what()); }
        catch (...) {}
    } catch (const std::exception& error) {
        if (!lifetime->load(std::memory_order_acquire)) return;
        reportUiDispatchError(error.what());
    } catch (...) {
        if (!lifetime->load(std::memory_order_acquire)) return;
        reportUiDispatchError("unknown exception");
    }
}

void MainWindow::reportUiDispatchError(String message) {
    const auto lifetime = lifecycle_alive_;
    assert(lifetime && lifetime->load(std::memory_order_acquire) &&
           "UI dispatch diagnostics must not run during MainWindow teardown");
    if (!lifetime || !lifetime->load(std::memory_order_acquire)) return;
    if (ui_dispatch_error_dialog_) {
        // D版と同様、一つのモードレス報告を表示中は、繰り返すイベント処理の失敗通知を抑制する。
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        return;
    }
    if (!prop_) throw std::logic_error("Editor properties are unavailable after a UI failure");
    const auto gui = services_.gui;
    const auto owner = window_;
    const auto shell = services_.shell;
    const auto services = services_;
    auto* const properties = prop_;
    lib::editor_action_log(String("Editor UI dispatch exception: ") + message);
    if (!lifetime->load(std::memory_order_acquire)) return;
    String details = ::cwxeditor::cwx::utils::printStackTrace();
    if (!lifetime->load(std::memory_order_acquire)) return;
    details += "\n--------\n" +
        ::cwxeditor::cwx::utils::createDebugln(message);
    if (!lifetime->load(std::memory_order_acquire)) return;
    smalldialogs_ns::Callbacks callbacks;
    // D版はこのボタンでプロセスを終了する。C++版は共通の終了処理を
    // 使い、 シナリオやセッションの保存失敗に対処できるようにする。
    callbacks.shutdown = [this, lifetime] {
        if (lifetime->load(std::memory_order_acquire)) exitAll();
    };
    callbacks.openPath = [lifetime, shell](StringView path) {
        if (!lifetime->load(std::memory_order_acquire) || !shell) return;
        const auto log_path = pathFromUtf8(path);
        std::string error;
        if (!shell->open_path(log_path, error) &&
            lifetime->load(std::memory_order_acquire)) {
            error.clear();
            (void)shell->reveal_path(log_path.parent_path(), error);
        }
    };
    auto dialog = std::make_shared<smalldialogs_ns::ErrorDialog>(
        *properties, owner, std::move(details), std::move(callbacks), services);
    if (!lifetime->load(std::memory_order_acquire)) return;
    ui_dispatch_error_dialog_ = dialog;
    const std::weak_ptr<smalldialogs_ns::ErrorDialog> weak_dialog = dialog;
    dialog->closeEvent.push_back([this, lifetime, weak_dialog] {
        const auto current = weak_dialog.lock();
        if (!current || !lifetime->load(std::memory_order_acquire)) return;
        if (ui_dispatch_error_dialog_ == current) ui_dispatch_error_dialog_.reset();
    });
    if (owner && gui) gui->show(owner);
    if (!lifetime->load(std::memory_order_acquire) ||
        ui_dispatch_error_dialog_ != dialog) return;
    (void)dialog->open();
}

void MainWindow::doCWX() {
    if (opt_.create || opt_.createclassic) {
        (void)createScenarioFromLaunchOption();
    } else if (!opt_.scenario.empty()) {
        openScenario(opt_.scenario, [this] { resetOpt(); });
    } else if (!opt_.noload && valueOf(prop_->var().openLastScenario) &&
               !valueOf(prop_->var().lastScenario).empty()) {
        // 非同期の読み込み中はモードレスの通知を表示し続け、起
        // 動状況を伝える。 二つ目のモーダル待ちは追加しない。
        showScenarioLoading("前回のシナリオを読み込み中です。\n成功またはエラーが確定するまでお待ちください。");
        try {
            openScenario(valueOf(prop_->var().lastScenario), [this] {
                dismissPreviousScenarioLoading();
                resetOpt();
            });
        } catch (...) {
            dismissPreviousScenarioLoading();
            throw;
        }
    }
    // D版はSWTループとともにIPC・バックアップのワーカーを起動する。
    // C++版は外側のループでアプリのホストがイベント処理とバックアッ
    // プの時刻管理を行うため、 ここでは共通の受信側を一度だけ登録する。
    pipeThr();
}

}  // namespace cwxeditor::cwx::editor::gui::dwt::mainwindow
