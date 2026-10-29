#pragma once

#include "cwxeditor_src/core/cwx/area.hpp"
#include "cwxeditor_src/core/cwx/msgs.hpp"
#include "cwxeditor_src/core/cwx/path.hpp"
#include "cwxeditor_src/core/cwx/summary.hpp"
#include "cwxeditor_src/core/cwx/types.hpp"
#include "cwxeditor_src/core/cwx/utils.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/absdialog.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/undo.hpp"
#include "cwxeditor_src/lib/components.hpp"
#include "cwxeditor_src/lib/events.hpp"
#include "cwxeditor_src/lib/image_import.hpp"
#include "cwxeditor_src/lib/layout.hpp"
#include "cwxeditor_src/lib/portable.hpp"
#include "cwxeditor_src/lib/services.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace cwxeditor::cwx::editor::gui::dwt::comment { class CommentDialog; }
namespace cwxeditor::cwx::card { class CastCard; }

namespace cwxeditor::cwx::editor::gui::dwt::areawindow {

namespace lib = ::cwxeditor::lib;
namespace area = ::cwxeditor::cwx::area;
namespace path = ::cwxeditor::cwx::path;
namespace summary = ::cwxeditor::cwx::summary;
namespace skin = ::cwxeditor::cwx::skin;
namespace types = ::cwxeditor::cwx::types;
namespace utils = ::cwxeditor::cwx::utils;
namespace absdialog = ::cwxeditor::cwx::editor::gui::dwt::absdialog;
namespace undo = ::cwxeditor::cwx::editor::gui::dwt::undo;

using Props = absdialog::Props;
using Summary = summary::Summary;
using UndoManager = undo::UndoManager;
using Panel = lib::Panel;
using Container = lib::Container;
using Window = lib::Window;
using ResourceId = lib::ResourceId;

/// 従来のSWT SelectionEventに代わ
/// り、表・クリップボード操作へ渡す共通イベント。
struct SelectionEvent {
    std::string command;
    bool handled{};
};

enum class SceneEditTargetKind {
    Card,
    Background,
};

/// エリアウィンドウが購読する、Editor全体の通知用
/// の小さなシグナル実装。 接続を明示的に管理し、ウィン
/// ドウ破棄後に無効なオブジェクトを指す通知を残さない。
template <typename... Args>
class Signal final {
public:
    using Handler = std::function<void(Args...)>;
    using Connection = std::uint64_t;

    Signal()=default;
    Signal(const Signal& other):next_id_(other.next_id_),handlers_(other.handlers_){}
    Signal& operator=(const Signal& other){if(this!=&other){lifetime_.reset();next_id_=other.next_id_;handlers_=other.handlers_;lifetime_=std::make_shared<int>(0);}return *this;}
    ~Signal(){lifetime_.reset();}

    [[nodiscard]] Connection connect(Handler handler) {
        if (!handler) return 0U;
        const auto id = next_id_++;
        handlers_.emplace(id, std::move(handler));
        return id;
    }

    void disconnect(Connection id) {
        if (id != 0U) handlers_.erase(id);
    }

    void emit(Args... args) const {
        const std::weak_ptr<void> life=lifetime_;
        const auto snapshot=handlers_;
        for (const auto& [id,handler] : snapshot) {
            // C++での寿命保護として、通知中に解除された登録は、
            // 開始時のスナップショットに含まれていても呼ばない。
            if(life.expired())return;
            if(handlers_.contains(id)&&handler)handler(args...);
        }
    }

private:
    Connection next_id_{1U};
    std::map<Connection, Handler> handlers_;
    std::shared_ptr<void> lifetime_{std::make_shared<int>(0)};
};

/// 具体的なエリアビューからホストウィンドウへ渡すコールバ
/// ック。 特定のウィジェットツールキットに依存させない。
struct AreaViewCallbacks {
    lib::Action refresh;
    std::function<bool()> can_up;
    std::function<bool()> can_down;
    lib::Action up;
    lib::Action down;
    std::function<void(bool)> open_event;
    lib::Action open_duplicate;
    lib::Action edit;
    // Dは一覧の選択を変える前に、クリックした画像をキャッシュ済みのモ
    // ードレス編集画面へ渡す。 ホストも対象を明示するこの経路を使う。
    std::function<void(SceneEditTargetKind, std::size_t)> edit_target;
    std::function<bool()> can_edit;
    std::function<bool()> can_select_connected_resource;
    lib::Action select_connected_resource;
    lib::Action write_comment;
    std::function<bool()> can_write_comment;
    std::function<bool()> can_do_tcpd;
    std::function<bool()> can_do_t;
    std::function<bool()> can_do_c;
    std::function<bool()> can_do_p;
    std::function<bool()> can_do_d;
    std::function<bool()> can_do_clone;
    std::function<void(SelectionEvent&)> cut;
    std::function<void(SelectionEvent&)> copy;
    std::function<void(SelectionEvent&)> paste;
    std::function<void(SelectionEvent&)> del;
    std::function<void(SelectionEvent&)> clone;
    std::function<bool(std::string_view, bool)> open_cwx_path;
    std::function<std::vector<std::string>()> opened_cwx_path;
};

/// 一つ以上のエリアウィンドウが必要とする通知とホストサービス
/// 。 DWTのCommonsに対応する、移植可能な具体型。
struct AreaWindowSignals {
    Signal<const area::Area*> del_area;
    Signal<const area::Area*> ref_area;
    Signal<const area::Battle*> del_battle;
    Signal<const area::Battle*> ref_battle;
    Signal<const area::Package*> del_package;
    Signal<const area::Package*> ref_package;
    Signal<const Summary*> close_adds;
    // DのEnemyCardの場面と参照カードのパネルは、Cast
    // の変更を購読する。 共有ホストのコンテキストに通知を置き、
    // 単独起動とALLの各SceneViewで同じ経路を使う。
    Signal<std::shared_ptr<::cwxeditor::cwx::card::CastCard>> ref_cast;
    Signal<std::shared_ptr<::cwxeditor::cwx::card::CastCard>> del_cast;
    Signal<> repl_text;
    Signal<> ref_preview_values;
    Signal<> ref_card_state;
    Signal<> ref_undo_max;
};

struct SceneViewRegistration {
    const area::AbstractArea* area{};
    const void* sender{};
    std::weak_ptr<void> lifetime;
    lib::Action refresh;
    lib::Action refresh_geometry;
    lib::Action refresh_replaced;
    std::function<void(const std::vector<std::size_t>&,
                       const std::vector<std::size_t>&)> restore_selection;
    // 追加通知では、共有モデルの挿入前に各ビュー独自のTable
    // 状態を取得し、 そのビューが行を追加した後に復元する。
    std::function<std::shared_ptr<void>()> capture_append_rows;
    std::function<void(const std::vector<std::size_t>&,
                       const std::vector<std::size_t>&, bool, bool,
                       bool,
                       const std::shared_ptr<void>&)> append_rows;
};

struct SceneViewAppendRowsSnapshot final {
    std::shared_ptr<SceneViewRegistration> view;
    std::shared_ptr<void> state;
};

class SceneViewRegistry final {
public:
    [[nodiscard]] std::shared_ptr<SceneViewRegistration> add(
        const area::AbstractArea* area_value, const void* sender,
        const std::shared_ptr<void>& lifetime,
        lib::Action refresh, lib::Action refresh_geometry,
        lib::Action refresh_replaced = {},
        std::function<void(const std::vector<std::size_t>&,
                           const std::vector<std::size_t>&)> restore_selection = {},
        std::function<std::shared_ptr<void>()> capture_append_rows = {},
        std::function<void(const std::vector<std::size_t>&,
                           const std::vector<std::size_t>&, bool, bool,
                           bool,
                           const std::shared_ptr<void>&)> append_rows = {}) {
        auto entry = std::make_shared<SceneViewRegistration>(SceneViewRegistration{
            area_value, sender, lifetime, std::move(refresh),
            std::move(refresh_geometry), std::move(refresh_replaced),
            std::move(restore_selection), std::move(capture_append_rows),
            std::move(append_rows)});
        entries_.push_back(entry);
        prune();
        return entry;
    }

    void publish(const area::AbstractArea* area_value, const void* sender,
                  const bool geometry_only = false,
                  const bool model_replaced = false) {
        prune();
        std::vector<std::shared_ptr<SceneViewRegistration>> peers;
        for (const auto& weak : entries_) {
            if (auto entry = weak.lock(); entry && entry->area == area_value &&
                entry->sender != sender && !entry->lifetime.expired()) {
                peers.push_back(std::move(entry));
            }
        }
        for (const auto& peer : peers) {
            if (peer->lifetime.expired()) continue;
            const auto& action = model_replaced && peer->refresh_replaced
                ? peer->refresh_replaced
                : geometry_only ? peer->refresh_geometry : peer->refresh;
            if (action) action();
        }
        prune();
    }

    // 所有者の配列を変える前に、他のTableの選択を取得する
    // 。 新しい行ができたら、取得元と同じ登録先へ状態を返す。
    [[nodiscard]] std::vector<SceneViewAppendRowsSnapshot> captureAppendRows(
        const area::AbstractArea* area_value, const void* sender) {
        prune();
        std::vector<SceneViewAppendRowsSnapshot> snapshots;
        for (const auto& weak : entries_) {
            auto entry = weak.lock();
            if (!entry || entry->area != area_value || entry->sender == sender ||
                entry->lifetime.expired() || !entry->capture_append_rows ||
                !entry->append_rows) continue;
            auto state = entry->capture_append_rows();
            if (state && !entry->lifetime.expired())
                snapshots.push_back({std::move(entry), std::move(state)});
        }
        return snapshots;
    }

    void appendRows(const std::vector<SceneViewAppendRowsSnapshot>& snapshots,
                    const std::vector<std::size_t>& cards,
                    const std::vector<std::size_t>& backs,
                    const bool select, const bool do_refresh,
                    const bool add_to_peer_selection = false) {
        for (const auto& snapshot : snapshots) {
            const auto& entry = snapshot.view;
            if (!entry || entry->lifetime.expired() || !snapshot.state ||
                !entry->append_rows) continue;
            entry->append_rows(cards, backs, select, do_refresh,
                               add_to_peer_selection, snapshot.state);
        }
        prune();
    }

    // DのReselectは、同じ所有者の全場面へUndo/Redoの選択状態を適用する。
    void restoreSelection(const area::AbstractArea* area_value, const void* sender,
                          const std::vector<std::size_t>& cards,
                          const std::vector<std::size_t>& backs) {
        prune();
        std::vector<std::shared_ptr<SceneViewRegistration>> peers;
        for (const auto& weak : entries_)
            if (auto entry = weak.lock(); entry && entry->area == area_value &&
                entry->sender != sender && !entry->lifetime.expired())
                peers.push_back(std::move(entry));
        for (const auto& peer : peers) {
            if (peer->lifetime.expired()) continue;
            const auto action = peer->restore_selection;
            if (action) action(cards, backs);
        }
    }

private:
    void prune() {
        std::erase_if(entries_, [](const auto& weak) {
            const auto entry = weak.lock();
            return !entry || entry->lifetime.expired();
        });
    }
    std::vector<std::weak_ptr<SceneViewRegistration>> entries_;
};

class MenuActionRegistry final {
public:
    struct Entry {
        lib::Action action;
        std::function<bool()> enabled;
        std::function<bool()> checked;
        std::uint64_t generation{};
    };

    MenuActionRegistry()=default;
    MenuActionRegistry(const MenuActionRegistry& other):entries_(other.entries_),next_generation_(other.next_generation_){}
    MenuActionRegistry& operator=(const MenuActionRegistry& other){if(this!=&other){lifetime_.reset();entries_=other.entries_;next_generation_=other.next_generation_;lifetime_=std::make_shared<int>(0);}return *this;}
    ~MenuActionRegistry(){lifetime_.reset();}

    std::uint64_t put(types::MenuID id, lib::Action action,
             std::function<bool()> enabled = {},
             std::function<bool()> checked = {});
    void remove(types::MenuID id);
    void remove(types::MenuID id,std::uint64_t expected_generation);
    [[nodiscard]] bool dispatch(types::MenuID id);
    [[nodiscard]] bool enabled(types::MenuID id) const;
    [[nodiscard]] bool checked(types::MenuID id) const;
    [[nodiscard]] const std::map<types::MenuID, Entry>& entries() const noexcept {
        return entries_;
    }

private:
    std::map<types::MenuID, Entry> entries_;
    std::shared_ptr<void> lifetime_{std::make_shared<int>(0)};
    std::uint64_t next_generation_{1};
};

/// 場面ウィンドウとホストの接続。シグナルでEditor
/// 全体の更新・削除通知を再現でき、 独立したヘ
/// ッドレスウィンドウでは空のままにもできる。
struct AreaWindowContext {
    lib::Services services{};
    AreaWindowSignals signals{};
    AreaViewCallbacks view_callbacks{};
    std::shared_ptr<SceneViewRegistry> scene_view_registry{
        std::make_shared<SceneViewRegistry>()};
    // ホストがイベントペインを接続して有効化した後、場面からイベント
    // への移動を完了する。 ビューは選択された所有者・イベントのCWXPath
    // を渡し、タブの選択とペインの検索はホストが行う。
    std::function<void(std::string_view, bool)> open_event_path;
    // 参照先のArea/Battle場面を開く（DのOpenAtTableView
    // ）。 背景参照の選択ではイベント編集でなく場面編集を
    // 選ぶため、open_event_pathとは分ける。
    std::function<void(area::AbstractArea*)> open_area_scene;
    // 登録済みの場面ペインを有効にするのではなく、同
    // じArea/Battleの追加ビューを開く。
    std::function<void(area::AbstractArea*)> open_duplicate;
    // 場面の参照フィルター表で選ばれたフラ
    // グなど、場面編集の外側の資源を開く。
    std::function<bool(std::string_view, bool)> open_cwx_path;
    std::function<bool(std::string_view,bool,bool)> open_file_path;
    std::function<std::shared_ptr<comment::CommentDialog>(std::string_view)> make_scene_comment_dialog;
    std::function<lib::Action(lib::Action)> subscribe_scene_comment_updates;
    std::function<lib::Action(std::function<void(types::MenuID)>)> subscribe_menu_updates;
    std::function<lib::Action(std::function<void(skin::Skin::Ptr)>)> subscribe_skin_updates;
    std::function<bool()> stop_open;
    // OS・文書提供元の画像を現在のシナリオへ取り込む。D
    // のリンク・何もしない操作を選んだ場合は外部パスを残
    // す。 保存先の確認とコピーはホストが行い、場面ビュ
    // ーは返されたモデルのパスと画像寸法だけを適用する。
    std::function<lib::ImageImportResult(const std::filesystem::path&,
                                         lib::ImageImportKind)> import_scene_image;
    // Dと同様、場面へのファイルドロップ後にシナリオの素材ディレクトリを更新する。
    std::function<void(std::string_view)> ref_paths;
    std::function<void(std::string_view)> ref_bg_image;
    std::function<void(std::string_view)> ref_menu_card;
    std::function<void(std::string_view)> add_menu_card, del_menu_card;
    std::function<void(std::string_view)> add_bg_image, del_bg_image;
    std::function<void()> ref_card_groups, ref_cell_names;
    std::function<void(area::AbstractArea*,const std::vector<std::pair<std::size_t,std::size_t>>&)> ref_menu_card_indices;
    std::function<void(std::string,bool)> find_named_id;
    std::function<void()> ref_use_count;
    // モデルだけの履歴に保持できるよう、実装側でホストを弱参照
    // で保護し、 外部へ通知するたびに有効性を確認すること。
    std::function<void(const std::vector<std::string>&,
                       const std::vector<std::string>&)> history_resource_refresh;
    // モデル履歴に保持できる通知処理。編集元のビューを保持してはならない。
    std::function<void(area::AbstractArea*,
                       const std::vector<std::pair<std::size_t,std::size_t>>&)> history_card_indices_refresh;
    MenuActionRegistry* menu_registry{};
    std::function<void(const std::shared_ptr<Panel>&)> close_panel;
    std::function<void(const std::shared_ptr<Panel>&, std::string_view)> set_title;
    std::function<bool(const std::shared_ptr<Panel>&)> has_focus;
    lib::Action refresh_toolbar;
    ResourceId area_image{};
    ResourceId battle_image{};
    skin::Skin::Ptr skin_owner{};
    const skin::Skin* skin{};
    bool show_inherit_background{true};
    // 任意のホスト側の壁紙描画。描画時に現在のホスト設定
    // を読み、 画像や資源の寿命をビューへ移管しない。
    std::function<void(lib::DrawingContext&, lib::Rect)> paint_wallpaper;
};

}  // namespace cwxeditor::cwx::editor::gui::dwt::areawindow
