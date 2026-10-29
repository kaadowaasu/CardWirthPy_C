#pragma once
#include "cwxeditor_src/lib/image_mask.hpp"

// ツールキットに依存しないエリアの場面エディター。モデ
// ルはSummary・Areaの構造が所有し、 この
// クラスは共通部品のツリーと選択状態だけを所有する。

#include "cwxeditor_src/core/cwx/area.hpp"
#include "cwxeditor_src/core/cwx/enumutils.hpp"
#include "cwxeditor_src/core/cwx/background.hpp"
#include "cwxeditor_src/core/cwx/card.hpp"
#include "cwxeditor_src/core/cwx/path.hpp"
#include "cwxeditor_src/core/cwx/skin.hpp"
#include "cwxeditor_src/core/cwx/summary.hpp"
#include "cwxeditor_src/core/cwx/types.hpp"
#include "cwxeditor_src/core/cwx/utils.hpp"
#include "cwxeditor_src/core/cwx/versioninfo.hpp"
#include "cwxeditor_src/core/cwx/warning.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/absdialog.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/comment.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/areaviewutils.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/cellrender.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/area_context.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dprops.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dutils.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/incsearch.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/scene_flag_compare.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/materialselect.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/messageutils.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/undo.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/xmlbytestransfer.hpp"
#include "cwxeditor_src/lib/components.hpp"
#include "cwxeditor_src/lib/events.hpp"
#include "cwxeditor_src/lib/layout.hpp"
#include "cwxeditor_src/lib/services.hpp"
#include "cwxeditor_src/lib/ui_actions.hpp"
#include "cwxeditor_src/lib/ui_table_selection.hpp"
#include "cwxeditor_src/lib/ui_metrics.hpp"
#include "cwxeditor_src/lib/window_host.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <initializer_list>
#include <memory>
#include <map>
#include <limits>
#include <numeric>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cwxeditor::cwx::editor::gui::dwt::areaview {

namespace absdialog = ::cwxeditor::cwx::editor::gui::dwt::absdialog;
namespace area = ::cwxeditor::cwx::area;
namespace areaviewutils = ::cwxeditor::cwx::editor::gui::dwt::areaviewutils;
namespace areawindow = ::cwxeditor::cwx::editor::gui::dwt::areawindow;
namespace background = ::cwxeditor::cwx::background;
namespace card = ::cwxeditor::cwx::card;
namespace dprops = ::cwxeditor::cwx::editor::gui::dwt::dprops;
namespace dutils = ::cwxeditor::cwx::editor::gui::dwt::dutils;
namespace lib = ::cwxeditor::lib;
namespace incsearch = ::cwxeditor::cwx::editor::gui::dwt::incsearch;
namespace materialselect = ::cwxeditor::cwx::editor::gui::dwt::materialselect;
namespace skin = ::cwxeditor::cwx::skin;
namespace structs = ::cwxeditor::cwx::structs;
namespace summary = ::cwxeditor::cwx::summary;
namespace types = ::cwxeditor::cwx::types;
namespace undo = ::cwxeditor::cwx::editor::gui::dwt::undo;
namespace xmlbytestransfer = ::cwxeditor::cwx::editor::gui::dwt::xmlbytestransfer;

using Props = dprops::Props;
using Summary = summary::Summary;
using AbstractArea = area::AbstractArea;
using Area = area::Area;
using Battle = area::Battle;
using MenuCard = area::MenuCard;
using EnemyCard = area::EnemyCard;
using BgImage = background::BgImage;
using BgImageContainer = background::BgImageContainer;
using Skin = skin::Skin;
using FlexImage = areaviewutils::FlexImage;
using FlexImagePtr = areaviewutils::FlexImagePtr;
using ImagePane = lib::Canvas;
using Table = lib::TableView;
using SelectionEvent = areawindow::SelectionEvent;
using ResourceId = lib::ResourceId;

/// ホストのEditorが渡す値。メニュー・フォーカス・ツールバー・全体更
/// 新は、 引き続き共通のAreaWindowContextへ接続する。
struct AreaViewContext {
    lib::Services services{};
    std::function<void(lib::DrawingContext&, lib::Rect)> paint_wallpaper;
    usecounter::CounterOwner use_counter_owner;
    areawindow::AreaWindowContext* area_window{};
    areawindow::AreaViewCallbacks callbacks{};
    areawindow::MenuActionRegistry* menu_registry{};
    std::shared_ptr<areawindow::SceneViewRegistry> scene_view_registry{
        std::make_shared<areawindow::SceneViewRegistry>()};
    ResourceId card_image{};
    ResourceId enemy_image{};
    ResourceId background_image{};
    skin::Skin::Ptr skin_owner{};
    bool show_inherit_background{true};
    std::function<void(AbstractArea*, bool)> open_event;
    // ホストがイベント編集タブを接続・選択してから、場面からイ
    // ベントへの遷移を完了する。 ビューはD互換の選択カード
    // ・最初のツリーのパスを渡し、ドック実装には依存しない。
    std::function<void(std::string_view, bool)> open_event_path;
    // DのOpenAtTableViewは参照先のArea場面を開く。
    // Area/Battleのイベント編集を開く操作とは区別する。
    std::function<void(AbstractArea*)> open_area_scene;
    std::function<bool(std::string_view, bool)> open_cwx_path;
    std::function<bool(std::string_view,bool,bool)> open_file_path;
    std::function<std::shared_ptr<comment::CommentDialog>(std::string_view)> make_scene_comment_dialog;
    std::function<lib::Action(lib::Action)> subscribe_scene_comment_updates;
    std::function<lib::Action(std::function<void(types::MenuID)>)> subscribe_menu_updates;
    std::function<lib::Action(std::function<void(skin::Skin::Ptr)>)> subscribe_skin_updates;
    std::function<bool()> stop_open;
    // Dの資源ビューを更新するのは、資源を持つカード・背景を変えたSceneView
    // 操作と、 そのUndoのスナップショットの場合だけ。
    std::function<void(std::string_view)> ref_bg_image;
    std::function<void(std::string_view)> ref_menu_card;
    std::function<void(std::string_view)> add_menu_card, del_menu_card;
    std::function<void(std::string_view)> add_bg_image, del_bg_image;
    // 所有者ごとの子編集画面は、シナリオ単位のdel_bg_image
    // 通知とは独立して終了する。
    std::function<void(std::string_view)> retire_local_bg_image;
    std::function<void()> ref_card_groups, ref_cell_names;
    std::function<void(AbstractArea*,const std::vector<std::pair<std::size_t,std::size_t>>&)> ref_menu_card_indices;
    std::function<void(std::string,bool)> find_named_id;
    std::function<void()> ref_use_count;
    // モデルだけの履歴に保持できるよう、実装側でホストを弱参照
    // で保護し、 外部へ通知するたびに有効性を確認すること。
    std::function<void(const std::vector<std::string>&,
                       const std::vector<std::string>&)> history_resource_refresh;
    std::function<void(AbstractArea*,
                       const std::vector<std::pair<std::size_t,std::size_t>>&)> history_card_indices_refresh;
    std::function<lib::ImageImportResult(const std::filesystem::path&,
                                         lib::ImageImportKind)> import_scene_image;
    std::function<void(std::string_view)> ref_paths;
    std::function<void(AbstractArea*)> open_duplicate;
    std::function<void(std::string_view)> status;
    std::function<void()> refresh_flags;
    std::function<void()> refresh_toolbar;
    std::function<void()> write_comment;
    std::function<void()> create_card;
    std::function<void(int)> create_background;

    AreaViewContext() = default;
    explicit AreaViewContext(areawindow::AreaWindowContext& host) noexcept
        : services(host.services), paint_wallpaper(host.paint_wallpaper),
          area_window(&host), callbacks(host.view_callbacks),
          menu_registry(host.menu_registry),
          scene_view_registry(host.scene_view_registry),
          card_image(host.area_image),
          enemy_image(host.battle_image),
          skin_owner(host.skin_owner),
          show_inherit_background(host.show_inherit_background),
          open_event_path(host.open_event_path),
          open_area_scene(host.open_area_scene), open_cwx_path(host.open_cwx_path),
          open_file_path(host.open_file_path), make_scene_comment_dialog(host.make_scene_comment_dialog),
          subscribe_scene_comment_updates(host.subscribe_scene_comment_updates),
          subscribe_menu_updates(host.subscribe_menu_updates),
          subscribe_skin_updates(host.subscribe_skin_updates), stop_open(host.stop_open),
          ref_bg_image(host.ref_bg_image),
          ref_menu_card(host.ref_menu_card),
          add_menu_card(host.add_menu_card),del_menu_card(host.del_menu_card),
          add_bg_image(host.add_bg_image),del_bg_image(host.del_bg_image),
          ref_card_groups(host.ref_card_groups),ref_cell_names(host.ref_cell_names),
          ref_menu_card_indices(host.ref_menu_card_indices),find_named_id(host.find_named_id),
          ref_use_count(host.ref_use_count),
          history_resource_refresh(host.history_resource_refresh),
          history_card_indices_refresh(host.history_card_indices_refresh),
          import_scene_image(host.import_scene_image),
          ref_paths(host.ref_paths),
          open_duplicate(host.open_duplicate),
          refresh_toolbar(host.refresh_toolbar) {}
};

/// DのspnValue相当。選択項目で共通の値、または混在・未選択時の既定値を返す。
[[nodiscard]] int spnValue(std::span<const int> keys, int value) noexcept;

/// バトルの敗北時の遷移を設定する編集画面。
class BattleOtherSettingsDialog final : public absdialog::AbsDialog {
public:
    BattleOtherSettingsDialog(Props& prop, Battle& battle,
                              std::shared_ptr<lib::Window> parent = {},
                              lib::Services services = {});
    BattleOtherSettingsDialog(AreaViewContext& context, Props& prop,
                              Battle& battle,
                              std::shared_ptr<lib::Window> parent = {});
    BattleOtherSettingsDialog(const BattleOtherSettingsDialog&) = delete;
    BattleOtherSettingsDialog& operator=(const BattleOtherSettingsDialog&) = delete;

    [[nodiscard]] types::Transition transition() const noexcept { return result_transition_; }
    [[nodiscard]] int transitionSpeed() const noexcept { return result_speed_; }
    [[nodiscard]] bool doAnime() const noexcept { return result_do_anime_; }
    [[nodiscard]] bool ignoreEffectBooster() const noexcept {
        return result_ignore_effect_booster_;
    }
    [[nodiscard]] Battle& battle() const noexcept { return *battle_; }
    [[nodiscard]] bool commit();

protected:
    void setup(Container& content) override;
    bool apply() override;
    bool close(bool ok) override;

private:
    void storeResult();
    Props* prop_{};
    Battle* battle_{};
    std::shared_ptr<lib::ComboBox> transition_control_;
    std::shared_ptr<lib::NumberEdit> speed_control_;
    std::shared_ptr<lib::CheckBox> anime_control_;
    std::shared_ptr<lib::CheckBox> booster_control_;
    types::Transition result_transition_{types::Transition::None};
    int result_speed_{5};
    bool result_do_anime_{};
    bool result_ignore_effect_booster_{true};
};

// ALLではバトルの音声を側面欄でなく、モーダル画面の作業コピーで編集する。 MaterialSelect
// の検証・バージョン制限は、埋め込みの操作部品と共通にする。
class BattleBGMSettingsDialog final : public absdialog::AbsDialog {
public:
    BattleBGMSettingsDialog(AreaViewContext& context, Props& prop,
                            Battle& battle, Summary* summary);
    [[nodiscard]] int volume() const noexcept { return selector_->volume(); }
    [[nodiscard]] int loopCount() const noexcept { return selector_->loopCount(); }
    [[nodiscard]] int fadeIn() const noexcept { return selector_->fadeIn(); }
protected:
    void setup(Container& content) override;
private:
    Props* prop_{};
    std::unique_ptr<materialselect::MaterialSelect<materialselect::MtType::BGM>> selector_;
};

enum class DropTarg { ImagePane, Card, Back, Clipboard };

namespace detail {
// Dは場面ビューごとに一つのオブジェクト識別子を使う。 単調増加するIDを
// 使い、C++のアドレス再利用で古いドラッグデータと一致しないようにする。
inline std::atomic<std::uint64_t> next_scene_drag_identity{};
template<class Range,class T>void synchronizeChecks(const Range& current,
    std::vector<std::shared_ptr<T>>& old,std::vector<bool>& checked,std::vector<bool>& grayed){
    if(std::equal(current.begin(),current.end(),old.begin(),old.end()) && checked.size()==current.size() && grayed.size()==current.size())return;
    std::unordered_map<const T*,std::pair<bool,bool>> previous;
    for(std::size_t i=0;i<old.size() && i<checked.size() && i<grayed.size();++i)
        if(old[i])previous.emplace(old[i].get(),std::pair{checked[i],grayed[i]});
    std::vector<bool> next_checked,next_grayed;next_checked.reserve(current.size());next_grayed.reserve(current.size());
    for(const auto& value:current){const auto found=previous.find(value.get());next_checked.push_back(found==previous.end()?true:found->second.first);next_grayed.push_back(found==previous.end()?false:found->second.second);}
    old.assign(current.begin(),current.end());checked=std::move(next_checked);grayed=std::move(next_grayed);
}

enum class GeometryCommand {
    Top, Bottom, Left, Right, Even, NearTop, NearBottom, NearLeft, NearRight,
    CenterH, CenterV, Center, Big, Small, Expand
};
struct Placement {
    std::size_t index{};
    int x{}, y{}, width{}, height{}, scale{};
    bool operator==(const Placement&) const = default;
};
// 実際のカード・背景とすべてのホストで、同じDの配置規則を使う。
[[nodiscard]] std::vector<Placement> placeSelection(std::vector<Placement> selected,
    GeometryCommand command, bool cards, int canvasWidth, int canvasHeight);

class LambdaUndo final : public undo::Undo {
public:
    LambdaUndo(std::function<void()> undo_action, std::function<void()> redo_action)
        : undo_(std::move(undo_action)), redo_(std::move(redo_action)) {}
    void undo() override { if (undo_) undo_(); }
    void redo() override { if (redo_) redo_(); }
    void dispose() override { undo_ = {}; redo_ = {}; }

private:
    std::function<void()> undo_;
    std::function<void()> redo_;
};

template <typename T>
[[nodiscard]] std::shared_ptr<T> cloneObject(const std::shared_ptr<T>& object) {
    return object ? std::dynamic_pointer_cast<T>(object->dup()) : nullptr;
}

template <typename T>
[[nodiscard]] std::vector<std::size_t> normalizedSelection(
    const std::vector<std::size_t>& selection, std::size_t size) {
    std::vector<std::size_t> result;
    for (const auto index : selection) {
        if (index < size && std::find(result.begin(), result.end(), index) == result.end())
            result.push_back(index);
    }
    return result;
}

template <typename T>
[[nodiscard]] std::string cardName(const T& card) {
    if constexpr (std::is_same_v<T, MenuCard>) return card.name();
    else if constexpr (std::is_same_v<T, EnemyCard>) return std::to_string(card.id());
    else return {};
}

template <typename T>
[[nodiscard]] bool hasSpAuto(const T& value) {
    if constexpr (requires { value.spAuto(); }) return value.spAuto();
    else return false;
}

template <typename T>
void setSpAuto(T& value, bool enabled) {
    if constexpr (requires { value.spAuto(enabled); }) value.spAuto(enabled);
}

}  // namespace detail

/// Area・Battle・背景セルのコンテナーを編集する具体的なビュー。
template <typename A, typename C, bool UseCards, bool UseBacks>
class AbstractAreaView {
    enum class SceneEntryKind;
public:
    using AreaType = A;
    using CardType = C;
    using CardPtr = std::conditional_t<std::is_same_v<C, MenuCard>,
                                       area::MenuCardPtr, area::EnemyCardPtr>;

private:
    enum class ContextMenuTarget { Scene, Cards, Backs };

public:

    struct Snapshot {
        // イベント構造を固定して保持するのは削除履歴だけとし、通常編集では現在のモデルの同一性を保つ。
        std::vector<bool> deep_deleted_cards;
        area::MenuCardArray menu_cards;
        area::EnemyCardArray enemy_cards;
        area::MenuCardArray menu_identities;
        area::EnemyCardArray enemy_identities;
        std::shared_ptr<void> model_owner;
        std::vector<bool> card_checked,card_grayed,back_checked,back_grayed;
        background::BgImageArray backs;
        // Undo/Redoでもモデルの同一性を保ち、開いたモードレスのセ
        // ル編集画面が、 復元されたセルから切り離されないようにする。 Undo
        // には共有モデル配列でなく、独立した識別子の一覧を取得する。
        std::vector<background::BgImagePtr> back_identities;
        bool sp_auto{};
        bool possible_to_run_away{true};
        types::Transition transition{types::Transition::None};
        int transition_speed{5};
        bool do_anime{};
        bool ignore_effect_booster{true};
        std::string music;
        // DのUndoMusicは従属するPathUserを保持する。 素材の改
        // 名時に履歴だけが持つパスも更新し、現在の所有者を変更済みにはしない。
        std::shared_ptr<usecounter::PathUser> music_reference;
        std::uint32_t music_volume{};
        std::uint32_t music_loop_count{};
        std::uint32_t music_fade_in{};
        bool continue_bgm{};
        // DのUndoのReselectは、モデルの交換ごとに表・
        // 画像の選択を復元する。 添字はスナップショット内だけで有
        // 効なので、対応するモデル順を再構成してから表へ反映する。
        std::vector<std::size_t> selected_cards;
        std::vector<std::size_t> selected_backs;
    };

    struct BattleBGMSettings {
        std::string music;
        std::uint32_t volume{};
        std::uint32_t loop_count{};
        std::uint32_t fade_in{};
        bool continue_bgm{};
    };

    struct ExternalCardEdit {
        std::vector<std::pair<std::weak_ptr<C>,std::shared_ptr<C>>> targets;
        std::shared_ptr<void> model_owner;
    };
    using ExternalCardEditPtr=std::shared_ptr<ExternalCardEdit>;
    struct ExternalBackEdit {
        std::weak_ptr<BgImage> target;
        std::shared_ptr<BgImage> before;
    };
    using ExternalBackEditPtr = std::shared_ptr<ExternalBackEdit>;

    std::vector<lib::Action> modEvent;

    AbstractAreaView(AreaViewContext& context, Props& prop, Summary* summ,
                     const Skin* force_skin, A& area,
                     std::shared_ptr<lib::Container> parent,
                     std::shared_ptr<undo::UndoManager> undo_manager,
                     bool show_inherit_backs, bool read_only)
        : context_(&context), prop_(&prop), summary_(summ), force_skin_(force_skin),
          area_(&area), undo_owner_(std::move(undo_manager)),
          undo_(undo_owner_.get()),
          show_inherit_backs_(show_inherit_backs &&
                              !lib::editor_window_host_options().all_mode),
          read_only_(read_only) {
        construct(parent);
    }

    AbstractAreaView(AreaViewContext& context, Props& prop, Summary* summ,
                     const Skin* force_skin, A& area,
                     std::shared_ptr<lib::Container> parent,
                     undo::UndoManager* undo_manager,
                     bool show_inherit_backs, bool read_only)
        : context_(&context), prop_(&prop), summary_(summ), force_skin_(force_skin),
          area_(&area), undo_(undo_manager),
          show_inherit_backs_(show_inherit_backs &&
                              !lib::editor_window_host_options().all_mode),
          read_only_(read_only) {
        construct(parent);
    }

    AbstractAreaView(AreaViewContext&& context, Props& prop, Summary* summ,
                     const Skin* force_skin, A& area,
                     std::shared_ptr<lib::Container> parent,
                     std::shared_ptr<undo::UndoManager> undo_manager,
                     bool show_inherit_backs, bool read_only)
        : owned_context_(std::move(context)), context_(&owned_context_), prop_(&prop),
          summary_(summ), force_skin_(force_skin), area_(&area),
          undo_owner_(std::move(undo_manager)), undo_(undo_owner_.get()),
          show_inherit_backs_(show_inherit_backs &&
                              !lib::editor_window_host_options().all_mode),
          read_only_(read_only) {
        construct(parent);
    }

    AbstractAreaView(AreaViewContext&& context, Props& prop, Summary* summ,
                     const Skin* force_skin, A& area,
                     std::shared_ptr<lib::Container> parent,
                     undo::UndoManager* undo_manager,
                     bool show_inherit_backs, bool read_only)
        : owned_context_(std::move(context)), context_(&owned_context_), prop_(&prop),
          summary_(summ), force_skin_(force_skin), area_(&area), undo_(undo_manager),
          show_inherit_backs_(show_inherit_backs &&
                              !lib::editor_window_host_options().all_mode),
          read_only_(read_only) {
        construct(parent);
    }

    AbstractAreaView(areawindow::AreaWindowContext& context, Props& prop,
                     Summary* summ, const Skin* force_skin, A& area,
                     std::shared_ptr<lib::Container> parent,
                     undo::UndoManager* undo_manager = nullptr,
                     bool show_inherit_backs = true, bool read_only = false)
        : AbstractAreaView(makeContext(context), prop, summ, force_skin, area,
                           std::move(parent), undo_manager, show_inherit_backs,
                           read_only) {}

    // TAreaWindowの汎用の製品用ファクトリーが使うコンストラクター。
    AbstractAreaView(areawindow::AreaWindowContext& context, Props& prop,
                     Summary* summ, std::shared_ptr<lib::Container> parent,
                     A* area, undo::UndoManager* undo_manager,
                     bool show_inherit_backs, bool read_only)
        : AbstractAreaView(makeContext(context), prop, summ, nullptr, *requireArea(area),
                           std::move(parent), undo_manager, show_inherit_backs,
                           read_only) {}

    // 共有履歴の製品用入口では、編集元ペインを同
    // 期的に閉じ得る通知中もカーソルを保持する。
    AbstractAreaView(areawindow::AreaWindowContext& context, Props& prop,
                     Summary* summ, std::shared_ptr<lib::Container> parent,
                     A* area, std::shared_ptr<undo::UndoManager> undo_manager,
                     bool show_inherit_backs, bool read_only)
        : AbstractAreaView(makeContext(context), prop, summ, nullptr, *requireArea(area),
                           std::move(parent), std::move(undo_manager),
                           show_inherit_backs, read_only) {}

    AbstractAreaView(const AbstractAreaView&) = delete;
    AbstractAreaView& operator=(const AbstractAreaView&) = delete;
    virtual ~AbstractAreaView() {
        const auto drag_drop=context_?context_->services.drag_drop:decltype(context_->services.drag_drop){};
        retireSceneCallbacks();
        scene_view_registration_.reset();
        if(drag_drop) {drag_drop->unbind(cards_);drag_drop->unbind(backs_);drag_drop->unbind(canvas_);}
    }

    [[nodiscard]] std::weak_ptr<void> sceneLifetime()const noexcept{return scene_view_lifetime_;}
    [[nodiscard]] std::uint64_t sceneRevision()const noexcept{return geometry_revision_;}
    void onSceneRetired(lib::Action callback){if(callback)scene_retirement_callbacks_.push_back(std::move(callback));}
    [[nodiscard]] const std::shared_ptr<lib::Panel>& root() const noexcept { return root_; }
    [[nodiscard]] const std::shared_ptr<lib::Menu>& menu() const noexcept { return menu_; }
    [[nodiscard]] const std::shared_ptr<lib::Canvas>& canvas() const noexcept { return canvas_; }
    // ALLはホストのホーム操作の下へ場面の操作部品を埋め込む。 小型
    // パネルでも単独起動のツールバーと同じ部品を使い、 コマンドとNumberEdit
    // の通知は通常のAreaViewの動作を保つ。
    [[nodiscard]] std::shared_ptr<lib::Panel> allCompactToolbar() {
        if (!lib::editor_window_host_options().all_mode || !toolbar_row_)
            return {};
        if (all_compact_toolbar_) return all_compact_toolbar_;

        all_compact_toolbar_ = std::make_shared<lib::Panel>();
        all_compact_toolbar_->accessibility_name = "ALL SceneView compact toolbar";
        auto layout = std::make_shared<lib::LinearLayout>(lib::Orientation::Vertical);
        // 小型の操作欄はタッチ向けの縦積みにする。Undo/RedoはMainWindow
        // が Engine・Dataの直下に配置するため、このビ
        // ューには場面・イベントと位置・大きさの操作だけを置く。 contains only Scene/Event controls and geometry
        layout->spacing = 0.0F;
        layout->padding = {};
        all_compact_toolbar_->set_layout(layout);
        all_compact_toolbar_->set_layout_item({
            .horizontal = lib::Align::Stretch,
            .minimum = {115.0F, 0.0F},
            .preferred = {115.0F, 0.0F}});

        toolbar_row_->remove(toolbar_);
        if (toolbar_) {
            // ALLのイベント・上下移動の操作は、イベントビュー左下の作成・発火
            // 条件追加と同じ1.3倍にする。 24pxの部品は約32pxになる。
            toolbar_->icon_scale = kAllListToolScale;
            const float tool_height = std::ceil(24.0F * kAllListToolScale) + 1.0F;
            auto item = toolbar_->layout_item();
            item.minimum = {115.0F, tool_height};
            item.preferred = {115.0F, tool_height};
            item.horizontal = lib::Align::Stretch;
            item.vertical = lib::Align::Start;
            toolbar_->set_layout_item(item);
            all_compact_toolbar_->add(toolbar_);
        }
        // ホストの操作欄ではX・Y・幅・高さを編集できるようにする。 倍
        // 率・レイヤー・マスク・更新の操作はALL側の部品には含めない。
        for (std::size_t i = 0; i < 4U; ++i) {
            const auto& field = geometry_fields_[i];
            if (!field) continue;
            toolbar_row_->remove(field);
            all_compact_toolbar_->add(field);
        }
        all_compact_toolbar_->set_visible(prop_->var().showSceneToolBar.value);
        return all_compact_toolbar_;
    }
    [[nodiscard]] AreaViewContext& context() const noexcept { return *context_; }
    [[nodiscard]] Props& prop() const noexcept { return *prop_; }
    [[nodiscard]] Summary* summ() const noexcept { return summary_; }
    [[nodiscard]] Summary* summary() const noexcept { return summary_; }
    [[nodiscard]] A& area() const noexcept { return *area_; }
    [[nodiscard]] bool readOnly() const noexcept { return read_only_; }
    [[nodiscard]] const Skin* summSkin() const noexcept { return force_skin_; }
    [[nodiscard]] ImagePane& imagePane() const noexcept { return *canvas_; }
    [[nodiscard]] Table& cardList() const noexcept { return *cards_; }
    [[nodiscard]] Table& backList() const noexcept { return *backs_; }
    [[nodiscard]] Table& flagList() const noexcept { return *flags_; }

    void callModEvent() {
        const std::weak_ptr<void> life=scene_view_lifetime_;
        const auto revision=geometry_revision_;
        const auto handlers=modEvent;
        for (const auto& action : handlers) {
            if(action)action();
            // DのGCは受信先を保持するが、C++の通知は受信先を終了したり新
            // しい配置編集を始めたりできる。 その場合は古い編集を再開しない。
            if(life.expired() || geometry_revision_!=revision)return;
        }
    }
    void previewTrigger(Table& list, int x, int y, lib::Point screen_position,
                        std::int64_t hovered_row = -1,
                        bool exact_hit_test = false) {
        if (!preview_) return;
        if (list.rows.empty() || y < 0) {
            preview_->close();
            return;
        }
        // ネイティブ側が当たり判定を行った場合、-1は見出し・空
        // 白を明示する。 その結果を無視して推測した見出し寸法へ
        // 切り替えると、Windowsでプレビューが1行ずれる。
        if (exact_hit_test && hovered_row < 0) {
            preview_->close();
            return;
        }
        std::size_t index = hovered_row >= 0
                                ? static_cast<std::size_t>(hovered_row)
                                : list.rows.size();
        // Dはポインター直下の行をネイティブTableに問い合
        // わせ、選択行で代替しない。 共通の表ではDWTの標準
        // の見出し・行寸法を使い、別の素材へホバーしても 現
        // 在の選択を変更・解除せず、その行をプレビューする。
        constexpr float header_height = 22.0F;
        constexpr float row_height = 20.0F;
        if (index >= list.rows.size() && static_cast<float>(y) >= header_height) {
            index = std::min(
                list.rows.size() - 1U,
                static_cast<std::size_t>((static_cast<float>(y) - header_height) /
                                         row_height));
        }
        FlexImagePtr image;
        // カード行のプレビューは場面と同じ合成画像を使い、カ
        // ードの背景・素材レイヤー・名前を含める。 素材だけ
        // では、DのAreaViewで見える情報が失われる。
        if (&list == cards_.get() && index < card_images_.size())
            image = card_images_[index];
        else if (&list == backs_.get() && index < back_images_.size()) image = back_images_[index];
        if (!image) {
            preview_->close();
            return;
        }
        preview_->image(image, static_cast<int>(screen_position.x),
                        static_cast<int>(screen_position.y + row_height),
                        static_cast<int>(row_height));
        preview_->show();
    }
    void refreshHoverStatus(Table& list, const lib::PointerEvent& event) {
        if (!prop_->var().showCardStatusUnderPointer.value) return;
        std::optional<std::size_t> row;
        if (event.item_hit_test_available) {
            if (event.item_index >= 0) row = static_cast<std::size_t>(event.item_index);
        } else row = lib::hit_test_table_row(list, event.position,
            lib::scaled_ui_metric(lib::kEditorUiMetrics.row_height));
        const auto* skin_value = activeSkin();
        if (row && skin_value) {
            areaviewutils::Commons common(*prop_, *skin_value);
            common.summary = summary_;
            if constexpr (UseCards) if (&list == cards_.get() && *row < cardCount()) {
                statusLine(areaviewutils::createAreaViewStatusLine(common, summary_, *skin_value, areaCard(*row)));
                return;
            }
            if constexpr (UseBacks) if (&list == backs_.get() && *row < backCount()) {
                statusLine(areaviewutils::createAreaViewStatusLine(common, summary_, *skin_value, back(*row)));
                return;
            }
        }
        refreshStatusLine();
    }
    [[nodiscard]] std::size_t messageIndex() const noexcept {
        return UseCards ? cardCount() : 0U;
    }
    [[nodiscard]] std::size_t partyIndex() const noexcept { return messageIndex() + 1U; }

    void refreshRefAreas() {
        if constexpr (UseCards || !UseBacks) return;
        const AbstractArea* selected = ref_card_selected_ && ref_card_index_ < ref_areas_.size()
            ? ref_areas_[ref_card_index_] : nullptr;
        ref_areas_.clear();
        ref_area_names_.clear();
        if (!summary_) {
            ref_card_index_ = 0U;
            ref_card_selected_ = false;
            return;
        }
        bool selected_found = false;
        ref_areas_.reserve(summary_->areas().size() + summary_->battles().size());
        ref_area_names_.reserve(summary_->areas().size() + summary_->battles().size());
        const auto append = [this, selected, &selected_found](const auto& value) {
            if (!value) return;
            if (value.get() == selected) {
                ref_card_index_ = ref_areas_.size();
                selected_found = true;
            }
            ref_areas_.push_back(value.get());
            ref_area_names_.push_back(std::to_string(value->id()) + "." + value->name());
        };
        for (const auto& value : summary_->areas()) append(value);
        for (const auto& value : summary_->battles()) append(value);
        const bool select_no_reference = selected != nullptr && !selected_found;
        if (!selected || !selected_found) {
            ref_card_index_ = 0U;
            ref_card_selected_ = false;
        }
        refreshReferenceAreaCombo(select_no_reference);
    }
    void refreshRefAreasA(const Area&) { refreshRefAreas(); }
    void refreshRefAreasB(const Battle&) { refreshRefAreas(); }
    [[nodiscard]] std::size_t refCardIndex() const noexcept { return ref_card_index_; }
    void refCardIndex(std::size_t value) {
        if constexpr (UseCards || !UseBacks) return;
        if (value >= ref_areas_.size()) return;
        ref_card_index_ = value;
        ref_card_selected_ = true;
        refresh();
    }
    void refAreaTarget(const AbstractArea* value) {
        if constexpr (!UseCards && UseBacks) {
            if (ref_areas_.empty() && summary_) refreshRefAreas();
            ref_card_selected_ = false;
            ref_card_index_ = 0U;
            for (std::size_t index = 0U; index < ref_areas_.size(); ++index) {
                if (ref_areas_[index] != value) continue;
                ref_card_index_ = index;
                ref_card_selected_ = true;
                break;
            }
            refreshReferenceAreaCombo(value != nullptr && !ref_card_selected_);
            refresh();
        }
    }
    // 選択した参照場面の通知では、Dと同じく装飾画像だけを更
    // 新する。 背景モデルの編集として通知してはならない。
    bool refreshNotifiedReferenceCards(const std::string_view scene_path) {
        if constexpr (UseCards || !UseBacks) return false;
        if (lib::editor_window_host_options().all_mode) return false;
        if (ref_card_selected_ && ref_card_index_ < ref_areas_.size()) {
            auto* target = const_cast<AbstractArea*>(ref_areas_[ref_card_index_]);
            if (target && ::cwxeditor::cwx::path::cpeq(target->cwxPath(true), scene_path))
                refreshPresentation();
        }
        return true;
    }
    void refRefMenuCard(std::string value) {
        if (refreshNotifiedReferenceCards(::cwxeditor::cwx::path::cpparent(value))) return;
        ref_card_path_ = std::move(value); refresh();
    }
    void addRefMenuCard(std::string value) {
        if (refreshNotifiedReferenceCards(::cwxeditor::cwx::path::cpparent(value))) return;
        if (!value.empty()) ref_card_paths_.push_back(std::move(value));
    }
    void delRefMenuCard(std::string value) {
        if (refreshNotifiedReferenceCards(::cwxeditor::cwx::path::cpparent(value))) return;
        std::erase(ref_card_paths_, value);
    }
    void upRefMenuCards(std::string scene_path, std::vector<int> indices, int count) {
        if (!indices.empty() && refreshNotifiedReferenceCards(scene_path)) return;
        moveRefCards(indices, -std::max(count, 1));
    }
    void downRefMenuCards(std::string scene_path, std::vector<int> indices, int count) {
        if (!indices.empty() && refreshNotifiedReferenceCards(scene_path)) return;
        moveRefCards(indices, std::max(count, 1));
    }
    void openRefAreaView() {
        if (!ref_card_selected_ || ref_card_index_ >= ref_areas_.size()) return;
        auto* const target = const_cast<AbstractArea*>(ref_areas_[ref_card_index_]);
        if (!target) return;
        if (context_->open_area_scene) context_->open_area_scene(target);
        else if (context_->open_event) context_->open_event(target, false);
    }

    [[nodiscard]] std::vector<AbstractAreaView*> views() noexcept { return {this}; }
    [[nodiscard]] const std::vector<AbstractAreaView*> views() const noexcept { return {const_cast<AbstractAreaView*>(this)}; }
    void statusLine(std::string value) {
        status_line_=value;
        const auto status=status_;const auto notify=context_->status;
        if(status)status->text=value;
        if(notify)notify(value); // ホストのコールバックから戻った後は、受信先へ触れない。
    }
    [[nodiscard]] const std::string& statusLine() const noexcept { return status_line_; }

    [[nodiscard]] bool debugMode() const noexcept { return debug_mode_; }
    void reverseDebugMode() noexcept {
        debug_mode_ = !debug_mode_;
        prop_->var().viewEnemyCardDebug.value = debug_mode_;
        refresh();
    }
    [[nodiscard]] bool selectedEnemiesCanRunAway() const noexcept {
        if constexpr (!std::is_same_v<A, Battle> || !std::is_same_v<C, EnemyCard>) {
            return false;
        } else {
            if (selected_cards_.empty()) return false;
            return std::all_of(selected_cards_.begin(), selected_cards_.end(), [this](const auto index) {
                return index < cardCount() && areaCard(index).action(
                    types::ActionCardType::RunAway);
            });
        }
    }
    void setEscape() {
        if constexpr (std::is_same_v<A, Battle> && std::is_same_v<C, EnemyCard>) {
            if (read_only_ || selected_cards_.empty()) return;
            const bool enabled = !selectedEnemiesCanRunAway();
            bool changed = false;
            for (const auto index : selected_cards_)
                if (index < cardCount() && areaCard(index).action(
                    types::ActionCardType::RunAway) != enabled) {
                    changed = true;
                    break;
                }
            if (!changed) return;
            if(!mutateWithReferenceNotifications(selected_cards_, {}, [&] {
                for (const auto index : selected_cards_)
                    if (index < cardCount()) areaCard(index).action(
                        types::ActionCardType::RunAway, enabled);
            }))return;
            // Dと同じく選択行の画像をその場で更新し、Table
            // のフォーカスと選択印は変えない。
            refreshSelected(true, true);
            callModEvent();
        }
    }
    [[nodiscard]] bool applyBattleBGM(const BattleBGMSettings& value) {
        if constexpr (!std::is_same_v<A, Battle>) {
            return false;
        } else {
            if (read_only_ || (area_->music() == value.music &&
                area_->volume() == value.volume &&
                area_->loopCount() == value.loop_count &&
                area_->fadeIn() == value.fade_in &&
                area_->continueBGM() == value.continue_bgm)) return false;
            // Battleの設定はモデル変更を同期的に通
            // 知する。五つの項目をまとめて更新する間は
            // 選択部品の通知を抑止し、途中で古い操作部品
            // が既定のBGM値を書き戻さないようにする。
            const std::weak_ptr<void> life=scene_view_lifetime_;
            const auto revision=geometry_revision_+1U;
            const auto valid=[&]{return !life.expired() && geometry_revision_==revision;};
            const bool was_syncing = syncing_bgm_;
            syncing_bgm_ = true;
            bool continuation=false;
            try {
                continuation=mutateWithReferenceNotifications({}, {}, [&] {
                area_->music(value.music);
                if(!valid())return;
                area_->volume(value.volume);
                if(!valid())return;
                area_->loopCount(value.loop_count);
                if(!valid())return;
                area_->fadeIn(value.fade_in);
                if(!valid())return;
                area_->continueBGM(value.continue_bgm);
                if(!valid())return;
                }, true);
            } catch (...) {
                if (!life.expired()) syncing_bgm_ = was_syncing;
                throw;
            }
            if(life.expired())return true;
            syncing_bgm_ = was_syncing;
            if(!continuation)return true;
            refreshAfterModelChange();
            if(!valid())return true;
            // ネイティブの選択部品が更新中に、保留中のディレクトリ・
            // BGM継続の通知を完了する場合がある。 これは上のビュ
            // ー同期に属するので、五項目をまとめたコマンドより優先さ
            // せない。 操作部品は既にvalueを表示しているため、
            // Undoを増やさずコマンド後の状態だけを再設定する。
            if (area_->music() != value.music || area_->volume() != value.volume ||
                area_->loopCount() != value.loop_count ||
                area_->fadeIn() != value.fade_in ||
                area_->continueBGM() != value.continue_bgm) {
                const bool post_syncing = syncing_bgm_;
                syncing_bgm_ = true;
                area_->music(value.music);
                if(!valid()){if(!life.expired())syncing_bgm_=post_syncing;return true;}
                area_->volume(value.volume);
                if(!valid()){if(!life.expired())syncing_bgm_=post_syncing;return true;}
                area_->loopCount(value.loop_count);
                if(!valid()){if(!life.expired())syncing_bgm_=post_syncing;return true;}
                area_->fadeIn(value.fade_in);
                if(!valid()){if(!life.expired())syncing_bgm_=post_syncing;return true;}
                area_->continueBGM(value.continue_bgm);
                if(!valid()){if(!life.expired())syncing_bgm_=post_syncing;return true;}
                syncing_bgm_ = post_syncing;
            }
            return true;
        }
    }
    // MaterialSelectは値が確定してから通知する。ここで
    // 適用し、 Battleモデル・Undoの記録・共通部品を一つの
    // 操作で更新する。 無関係な従来の資源コールバックは開かない。
    void selectBGM() {
        if constexpr (std::is_same_v<A, Battle>) {
            if (!bgm_selector_ || syncing_bgm_) return;
            (void)applyBattleBGM({bgm_selector_->path(),
                                  static_cast<std::uint32_t>(std::max(0, bgm_selector_->volume())),
                                  static_cast<std::uint32_t>(std::max(0, bgm_selector_->loopCount())),
                                  static_cast<std::uint32_t>(std::max(0, bgm_selector_->fadeInModelValue())),
                                  bgm_selector_->continueBGM()});
        }
    }

    [[nodiscard]] bool isViewCards() const noexcept { return view_cards_; }
    [[nodiscard]] bool isViewBacks() const noexcept { return view_backs_; }
    [[nodiscard]] bool isViewMsg() const noexcept { return view_message_; }
    [[nodiscard]] bool isViewParty() const noexcept { return view_party_; }
    [[nodiscard]] bool isViewRefCards() const noexcept { return show_ref_cards_; }
    [[nodiscard]] bool isFixedCards() const noexcept { return read_only_ || fixed_cards_; }
    [[nodiscard]] bool isFixedCells() const noexcept { return read_only_ || fixed_cells_; }
    [[nodiscard]] bool isFixedBackground() const noexcept { return read_only_ || fixed_background_; }
    [[nodiscard]] bool spCustom() const noexcept { return !detail::hasSpAuto(*area_); }
    [[nodiscard]] bool isShowGrid() const noexcept { return show_grid_; }
    void reverseViewCards() {
        if constexpr (UseCards) {
            if (!focusCanvasBeforeImageVisibilityToggle(view_cards_, view_backs_)) return;
            view_cards_ = !view_cards_;
            prop_->var().viewCards.value = view_cards_;
            // DのreverseViewは非表示にした種類の編集対象を外
            // す。 後の一覧再構成で戻らないよう、固定キーも消去する。
            if (!view_cards_) {
                selected_cards_.clear();
                selected_card_keys_.clear();
            }
            refreshTypeVisibility();
        }
    }
    void reverseViewBacks() {
        if constexpr (UseBacks) {
            if (!focusCanvasBeforeImageVisibilityToggle(view_cards_, view_backs_)) return;
            view_backs_ = !view_backs_;
            prop_->var().viewBgImages.value = view_backs_;
            // DのreverseViewは非表示にした種類の編集対象を外
            // す。 後の一覧再構成で戻らないよう、固定キーも消去する。
            if (!view_backs_) {
                selected_backs_.clear();
                selected_back_keys_.clear();
            }
            refreshTypeVisibility();
        }
    }
    void reverseViewParty() {
        view_party_ = !view_party_;
        if constexpr (std::is_same_v<A, Area>)
            prop_->var().viewPartyCardsArea.value = view_party_;
        else if constexpr (std::is_same_v<A, Battle>)
            prop_->var().viewPartyCardsBattle.value = view_party_;
        else
            prop_->var().viewPartyCardsEvent.value = view_party_;
        // Dは既存の装飾画像とツールバーのチェック状態だけを変
        // える。 ここで全一覧を再構成すると、手動スクロール
        // で隠した無関係な背景の選択行まで表示されてしまう。
        refreshDecorationPresentation();
    }
    void reverseViewMsg() {
        view_message_ = !view_message_;
        if constexpr (std::is_same_v<A, Area>)
            prop_->var().viewMessageArea.value = view_message_;
        else if constexpr (std::is_same_v<A, Battle>)
            prop_->var().viewMessageBattle.value = view_message_;
        else
            prop_->var().viewMessageEvent.value = view_message_;
        // カード・背景の表を再構成せず、Dと同じくメッ
        // セージ画像とツールバーをその場で更新する。
        refreshDecorationPresentation();
    }
    void reverseViewRefCards() {
        if constexpr (!UseCards && UseBacks) {
            show_ref_cards_ = !show_ref_cards_;
            prop_->var().viewReferenceCards.value = show_ref_cards_;
            // 参照カードは装飾画像なので、Dと同じく表
            // 示切り替えで編集可能な一覧を変えない。
            refreshDecorationPresentation();
        }
    }
    void reverseFixedCards() {
        if (read_only_) return;
        fixed_cards_ = !fixed_cards_;
        if constexpr (std::is_same_v<C, MenuCard>)
            prop_->var().fixedImagesMenuCards.value = fixed_cards_;
        else if constexpr (std::is_same_v<C, EnemyCard>)
            prop_->var().fixedImagesBattle.value = fixed_cards_;
        else
            prop_->var().fixedImagesEvent.value = fixed_cards_;
        if (fixed_cards_) {
            discardKeyboardDraftsForFixedImages(true, false, false);
            discardPointerDraftsForFixedImages(true, false, false);
        }
        // Dのchecked()は場面の表示状態と選択操作を更新
        // するが、どちらの一覧も再構成・表示位置の変更はしない。
        refreshTypeVisibility();
    }
    void reverseFixedCells() {
        if (read_only_) return;
        fixed_cells_ = !fixed_cells_;
        prop_->var().fixedImagesCells.value = fixed_cells_;
        if (fixed_cells_) {
            discardKeyboardDraftsForFixedImages(false, true, false);
            discardPointerDraftsForFixedImages(false, true, false);
        }
        // Dのchecked()は場面の表示状態と選択操作を更新
        // するが、どちらの一覧も再構成・表示位置の変更はしない。
        refreshTypeVisibility();
    }
    void reverseFixedBackground() {
        if (read_only_) return;
        fixed_background_ = !fixed_background_;
        if constexpr (std::is_same_v<A, Area>)
            prop_->var().fixedImagesBackground.value = fixed_background_;
        else if constexpr (std::is_same_v<A, BgImageContainer>) {
            if (summary_) prop_->var().fixedImagesEventBackground.value = fixed_background_;
            else prop_->var().fixedImagesDefaultBackground.value = fixed_background_;
        }
        if (fixed_background_) {
            discardKeyboardDraftsForFixedImages(false, false, true);
            discardPointerDraftsForFixedImages(false, false, true);
        }
        // Dのchecked()は場面の表示状態と選択操作を更新
        // するが、どちらの一覧も再構成・表示位置の変更はしない。
        refreshTypeVisibility();
    }
    void reverseShowGrid() {
        show_grid_ = !show_grid_;
        prop_->var().showGrid.value = show_grid_;
        setupToolBar0(toolbar_);
        refreshGrid();
    }
    void refreshGrid() { requestSceneRedraw(); }

    void selectAllC() { if constexpr (UseCards) { selected_cards_.resize(cardCount()); std::iota(selected_cards_.begin(), selected_cards_.end(), 0U); refreshSelected(true, true); } }
    void selectAllB() { if constexpr (UseBacks) { selected_backs_.resize(backCount()); std::iota(selected_backs_.begin(), selected_backs_.end(), 0U); refreshSelected(true, true); } }
    void listSelectC() { if constexpr (UseCards) { selected_cards_ = cards_->selection; refreshSelected(true, true); } }
    void listSelectB() { if constexpr (UseBacks) { selected_backs_ = backs_->selection; refreshSelected(true, true); } }
    void refreshSelected(const bool reveal_rows = true,
                         const bool preserve_focus_row = false) {
        const std::weak_ptr<void> life=scene_view_lifetime_;
        const auto revision=geometry_revision_;
        const auto valid=[&]{return !life.expired() && geometry_revision_==revision;};
        if(numeric_preview_ && !numericPreviewCurrent(numeric_preview_->field))resetNumericPreviewImages();
        // SWTのTable.selectと同じく、モデルからの選択同期で新
        // しいSelectionEventを送らず、 他方の種類の選択を消さ
        // ない。DWTの空でないsetSelectionはsetFocusIndex(indices[0])
        // を呼び、 フォーカスと選択印を更新する。空の場合は
        // 選択解除だけなので、以前のフォーカスと印を残す。
        if (cards_) {
            (void)lib::select_table_rows(*cards_, selected_cards_, true, false);
            if (!preserve_focus_row && !cards_->selection.empty()) {
                cards_->selection_anchor = cards_->selection.front();
                cards_->selection_cursor = cards_->selection.front();
            }
        }
        if(!valid())return;
        if (backs_) {
            (void)lib::select_table_rows(*backs_, selected_backs_, true, false);
            if (!preserve_focus_row && !backs_->selection.empty()) {
                backs_->selection_anchor = backs_->selection.front();
                backs_->selection_cursor = backs_->selection.front();
            }
        }
        if(!valid())return;
        // DのTable#showSelectionは、一覧だけでなくImagePane
        // の選択にも追従する。 キャンバスのクリック・外部CWXPath
        // からの移動・再構成後も、選択モデルの行を見える位置に保つ。
        if (reveal_rows) {
            if (cards_ && !cards_->selection.empty())
                (void)lib::reveal_table_row(*cards_, cards_->selection.front());
            if (backs_ && !backs_->selection.empty())
                (void)lib::reveal_table_row(*backs_, backs_->selection.front());
        }
        syncGeometryControls();
        if(!valid())return;
        refreshFlagAssignCombo();
        if(!valid())return;
        refreshStatusLine();
        if(!valid())return;
        rememberSceneSelectionKeys();
        refreshPrimaryToolBarEnablement();
        if(!valid())return;
        requestSceneRedraw();
    }
    void refreshCard(std::size_t) { refreshCards(); }
    // 状態表示の設定が変わったら、表の行だけでなくカード画像も作り直す。
    void refreshCardState() { refreshPresentation(); }
    void refreshCardNamePreview() { refreshCards(); }
    void refreshCardNamePreviewF(const std::vector<std::string>&, const std::vector<std::string>&, const std::vector<std::string>&) { refreshCards(); }

    void editSpnCard(int value) { editSelectedCardPosition(value, true); }
    void enterSpnCard(int value) { editSelectedCardPosition(value, false); }
    [[nodiscard]] int cancelSpnCard(int old_value) const noexcept { return old_value; }
    void editSpnBack(int value) { editSelectedBackSize(value, true); }
    void enterSpnBack(int value) { editSelectedBackSize(value, false); }
    [[nodiscard]] int cancelSpnBack(int old_value) const noexcept { return old_value; }
    void editSpn(int value) { editSpnCard(value); }
    void enterSpn(int value) { enterSpnCard(value); }
    [[nodiscard]] int cancelSpn(int old_value) const noexcept { return cancelSpnCard(old_value); }

    void resizeImageC(FlexImage& image, int x, int y, std::uint32_t scale) {
        if constexpr (UseCards) {
            for (std::size_t i = 0; i < card_images_.size(); ++i) if (card_images_[i].get() == &image) {
                mutateCardGeometry([&] { areaCard(i).x(x); areaCard(i).y(y); areaCard(i).scale(scale); }); return;
            }
        }
    }
    void resizeImageB(FlexImage& image, int x, int y, int width, int height) {
        if constexpr (UseBacks) {
            for (std::size_t i = 0; i < back_images_.size(); ++i) if (back_images_[i].get() == &image) {
                mutateBackGeometry([&] { auto& value = back(i); value.x(x); value.y(y); value.width(width); value.height(height); }); return;
            }
        }
    }
    void selectImageC(FlexImage& image) { selectImage(image, selected_cards_, card_images_); }
    void selectImageB(FlexImage& image) { selectImage(image, selected_backs_, back_images_); }
    // DのPaintはTable.select/deselect後に部品を描き直すだけ
    // で、 Table.setSelectionやフォーカス・選択印の移動は行わない。
    void selectImageCImpl() { refreshSelected(true, true); }
    void selectImageBImpl() { refreshSelected(true, true); }
    void setAuto() { setAutoImpl(true); }
    void setCustom() { setAutoImpl(false); }
    [[nodiscard]] FlexImagePtr cardImage(int index) const noexcept {
        if (index < 0 || static_cast<std::size_t>(index) >= card_images_.size()) return {};
        return card_images_[static_cast<std::size_t>(index)];
    }
    [[nodiscard]] FlexImagePtr backImage(int index) const noexcept {
        if (index < 0 || static_cast<std::size_t>(index) >= back_images_.size()) return {};
        return back_images_[static_cast<std::size_t>(index)];
    }
    void setMask() {
        if constexpr (UseBacks) {
            if (read_only_) return;
            // Dの複数選択更新には、自身を追加する誤記（ics ~= ics）がある。 共
            // 通モデルの選択を読み、すべてマスクされた組をまとめて解除できるようにする。
            bool has_image{};
            bool all_masked{true};
            for (const auto index : selected_backs_) {
                if (index >= backCount()) continue;
                const auto* image = dynamic_cast<const background::ImageCell*>(&back(index));
                if (!image) continue;
                has_image = true;
                all_masked = all_masked && image->mask();
            }
            if (!has_image) return;
            const bool next_mask = !all_masked;
            std::vector<std::size_t> changed_backs;
            for (const auto index : selected_backs_)
                if (index < backCount() &&
                    dynamic_cast<const background::ImageCell*>(&back(index)))
                    changed_backs.push_back(index);
            // DのsetMaskはチェック欄の次の値を、選択した
            // ImageCellへ一律に適用する。 セルごとに反
            // 転したり、テキスト・色・PC背景を変えたりしない。
            if(!mutateWithReferenceNotifications({}, changed_backs, [&] {
                for (const auto index : selected_backs_)
                    if (index < backCount())
                        if (auto* image = dynamic_cast<background::ImageCell*>(&back(index)))
                            image->mask(next_mask);
            }))return;
            refreshAfterModelChange();
        }
    }
    void updateInheritBacks() {
        if (!inherit_background_label_) return;
        bool inherits = false;
        if constexpr (UseBacks && requires(A& owner) { owner.backs(); })
            inherits = background::isInheritBackground(area_->backs());
        inherit_background_label_->text = inherits
            ? prop_->requiredMsgs().inheritBackground.value
            : prop_->requiredMsgs().notInheritBackground.value;
        if (inherit_background_icon_)
            inherit_background_icon_->image = inherits
                ? prop_->images().load(dprops::StringView{"inherit_backs.png"})
                : prop_->images().load(
                      dprops::StringView{"not_inherit_backs.png"});
        inherit_background_label_->set_tooltip(inherits
            ? prop_->requiredMsgs().inheritBackgroundHint.value
            : prop_->requiredMsgs().notInheritBackgroundHint.value);
    }

    [[nodiscard]] bool canScaleCAny(int scale) const noexcept {
        if constexpr (UseCards) {
            if(read_only_ || scale<1)return false;
            for(const auto index:selected_cards_)
                if(index<cardCount() && areaCard(index).scale()!=static_cast<std::uint32_t>(scale))return true;
        }
        return false;
    }
    void scaleC(std::uint32_t scale) {
        if constexpr (UseCards) {
            if (!canScaleCAny(static_cast<int>(scale))) return;
            mutateCardGeometry([&] {
                for (const auto i : selected_cards_)
                    if (i < cardCount()) areaCard(i).scale(scale);
            });
        }
    }
    void scaleCMax() { scaleC(static_cast<std::uint32_t>(std::max(1,prop_->var().cardScaleMax.value))); }
    void scaleCMiddle() { scaleC(100U); }
    void scaleCMin() { scaleC(static_cast<std::uint32_t>(std::max(1,prop_->var().cardScaleMin.value))); }
    [[nodiscard]] bool canPosTop() const { return canPlaceSelection(detail::GeometryCommand::Top); }
    [[nodiscard]] bool canPosBottom() const { return canPlaceSelection(detail::GeometryCommand::Bottom); }
    [[nodiscard]] bool canPosLeft() const { return canPlaceSelection(detail::GeometryCommand::Left); }
    [[nodiscard]] bool canPosRight() const { return canPlaceSelection(detail::GeometryCommand::Right); }
    [[nodiscard]] bool canChangePos() const noexcept { return canMoveSelection(); }
    void posTop() { placeSelection(detail::GeometryCommand::Top); }
    void posBottom() { placeSelection(detail::GeometryCommand::Bottom); }
    void posLeft() { placeSelection(detail::GeometryCommand::Left); }
    void posRight() { placeSelection(detail::GeometryCommand::Right); }
    void posEven() { placeSelection(detail::GeometryCommand::Even); }
    [[nodiscard]] bool canPosEven() const { return canPlaceSelection(detail::GeometryCommand::Even); }
    void nearTop() { placeSelection(detail::GeometryCommand::NearTop); }
    void nearBottom() { placeSelection(detail::GeometryCommand::NearBottom); }
    void nearLeft() { placeSelection(detail::GeometryCommand::NearLeft); }
    void nearRight() { placeSelection(detail::GeometryCommand::NearRight); }
    [[nodiscard]] bool canNearTop() const { return canPlaceSelection(detail::GeometryCommand::NearTop); }
    [[nodiscard]] bool canNearBottom() const { return canPlaceSelection(detail::GeometryCommand::NearBottom); }
    [[nodiscard]] bool canNearLeft() const { return canPlaceSelection(detail::GeometryCommand::NearLeft); }
    [[nodiscard]] bool canNearRight() const { return canPlaceSelection(detail::GeometryCommand::NearRight); }
    void nearCenterH() { placeSelection(detail::GeometryCommand::CenterH); }
    void nearCenterV() { placeSelection(detail::GeometryCommand::CenterV); }
    void nearCenter() { placeSelection(detail::GeometryCommand::Center); }
    [[nodiscard]] bool canNearCenter(bool h, bool v) const {
        return (h || v) && canPlaceSelection(h&&v?detail::GeometryCommand::Center:
            h?detail::GeometryCommand::CenterH:detail::GeometryCommand::CenterV);
    }
    [[nodiscard]] bool canScaleEvenAny() const {
        if(read_only_)return false;
        for(const bool cards:{true,false}) {
            const auto values=selectedPlacement(cards);
            if(!values.empty() && std::any_of(values.begin(),values.end(),[first=values.front().scale](const auto& item){return item.scale!=first;}))return true;
        }
        return false;
    }
    void scaleEvenBig() { placeSelection(detail::GeometryCommand::Big); }
    void scaleEvenSmall() { placeSelection(detail::GeometryCommand::Small); }
    [[nodiscard]] bool canExpandBacks() const { return canPlaceSelection(detail::GeometryCommand::Expand); }
    void expandBacks() { placeSelection(detail::GeometryCommand::Expand); }

    void refreshWallpaper() {
        const std::weak_ptr<void> life=scene_view_lifetime_;
        // DのrefreshWallpaperは場面背景だけを更新する。 選択中の背
        // 景一覧を再構成すると選択行が表示され、ユーザーの表示位置が動いてしまう。
        if (!life.expired()) requestSceneRedraw();
    }
    void refreshFlags() {
        const std::weak_ptr<void> life = scene_view_lifetime_;
        const auto notify = context_->refresh_flags;
        // D版と同じく、通知先の有無にかかわらず参照Flag一覧自体を更新する。
        refreshFlagRows();
        if (life.expired()) return;
        if (notify) notify();
    }
    void refFlags(const std::vector<std::string>&, const std::vector<std::string>&, const std::vector<std::string>&) { refreshFlags(); }
    void delFlags(const std::vector<std::string>& flags,
                  const std::vector<std::string>&,
                  const std::vector<std::string>&) {
        // DはFlag自身が削除された場合だけ、参照フラグ表
        // を更新する。 Step/Variantだけの通知で
        // は、選択Flag行が画面外でも表示位置を変えない。
        if (read_only_ || !summary_ || flags.empty()) return;
        refreshFlags();
    }
    void flagIncSearch() {
        const std::weak_ptr<void> life = scene_view_lifetime_;
        if (life.expired()) return;
        const auto* const context = context_;
        const auto root = root_;
        const auto flags = flags_;
        const auto* const owner = area_;
        const auto services = context ? context->services : lib::Services{};
        const auto gui = services.gui;
        auto* const search = flag_inc_search_.get();
        // Dと同じく、検索画面を開く前にforceFocus(_flagList, true)を呼ぶ。
        if (gui && flags) dutils::forceFocus(flags, true, services);

        // フォーカスの変更で、このビューが同期的に終了・再接続される場合
        // がある。 通知へ進む前に、表と検索の正確な接続先を再確認する。
        if (life.expired()) return;
        const auto still_current = [&] {
            return context_ == context && root_ == root && flags_ == flags &&
                   area_ == owner && context && context_->services.gui == gui &&
                   flag_inc_search_.get() == search;
        };
        if (!still_current()) return;
        if (search) search->startIncSearch();
    }
    void updateFlagChecks() { syncFlagChecks(); }
    void changingImages() { refresh(); }
    // DのrefreshControlsはツールバーの描き直しだけを行う。
    // 共通部品の更新中も、Tableの現在のフォーカスと選択印を保つ。
    void refreshControls() { refreshSelected(true, true); }
    void updateFlagCombo() { refreshFlags(); }
    void refreshControlsImpl() { refreshControls(); }
    void refDataVersion() { refresh(); }
    void refreshStatusLine() {
        // DのAreaViewは、更新のたびに現在のモデル選択
        // からステータス文言を求める。 Undo/Redo後
        // の表示を正しく保ち、一覧・キャンバスの選択が変わっ
        // た後に 削除済みカードのパスが残らないようにする。
        std::string line;
        const auto* skin_value = activeSkin();
        if (prop_ && skin_value) {
            areaviewutils::Commons common(*prop_, *skin_value);
            common.summary = summary_;
            if constexpr (UseCards && UseBacks) {
                if (selected_cards_.size() == 1U && selected_backs_.empty()) {
                    const auto index = selected_cards_.front();
                    if (index < cardCount())
                        line = areaviewutils::createAreaViewStatusLine(
                            common, summary_, *skin_value, areaCard(index));
                } else if (selected_cards_.empty() && selected_backs_.size() == 1U) {
                    const auto index = selected_backs_.front();
                    if (index < backCount())
                        line = areaviewutils::createAreaViewStatusLine(
                            common, summary_, *skin_value, back(index));
                } else if (!selected_cards_.empty() || !selected_backs_.empty()) {
                    if (!selected_cards_.empty())
                        line = ::cwxeditor::cwx::utils::tryFormat(
                            prop_->requiredMsgs().areaViewStatusSelCard.value,
                            selected_cards_.size());
                    if (!selected_backs_.empty()) {
                        if (!line.empty()) line.push_back(' ');
                        line += ::cwxeditor::cwx::utils::tryFormat(
                            prop_->requiredMsgs().areaViewStatusSelBack.value,
                            selected_backs_.size());
                    }
                }
            } else if constexpr (UseCards) {
                if (selected_cards_.size() == 1U) {
                    const auto index = selected_cards_.front();
                    if (index < cardCount())
                        line = areaviewutils::createAreaViewStatusLine(
                            common, summary_, *skin_value, areaCard(index));
                } else if (selected_cards_.size() > 1U) {
                    line = ::cwxeditor::cwx::utils::tryFormat(
                        prop_->requiredMsgs().areaViewStatusSelCard.value,
                        selected_cards_.size());
                }
            } else if constexpr (UseBacks) {
                if (selected_backs_.size() == 1U) {
                    const auto index = selected_backs_.front();
                    if (index < backCount())
                        line = areaviewutils::createAreaViewStatusLine(
                            common, summary_, *skin_value, back(index));
                } else if (selected_backs_.size() > 1U) {
                    line = ::cwxeditor::cwx::utils::tryFormat(
                        prop_->requiredMsgs().areaViewStatusSelBack.value,
                        selected_backs_.size());
                }
            }
        }
        statusLine(std::move(line));
    }
    void refreshPanel() { refresh(); }
    void refImageScale() {
        if (!prop_ || !canvas_) return;
        scene_zoom_ = prop_->imageScaleFactor();
        const auto scene_size = prop_->looks().viewSize();
        const float authored_zoom = scene_zoom_ /
            std::max(0.1F, lib::editor_ui_text_scale());
        auto item = canvas_->layout_item();
        item.minimum = {static_cast<float>(scene_size.width) * authored_zoom,
                        static_cast<float>(scene_size.height) * authored_zoom};
        item.preferred = item.minimum;
        canvas_->set_layout_item(item);
        refresh();
    }
    void refreshR(std::string, std::string) { refresh(); }
    void replText() {
        // Dは置換通知でコピー元のプレビューを更新しない。 replText
        // は書き込み可能なシナリオビューだけを対象にする。
        if (read_only_ || !summary_ || summary_->scenarioPath().empty()) return;
        refresh();
    }
    void refMenuCardIndices(const AbstractArea&, const std::vector<std::tuple<std::size_t, std::size_t>>&) { refresh(); }

    [[nodiscard]] int cardsIndex() const noexcept { return static_cast<int>(messageIndex()); }
    static int staticCardsIndex(const A&) noexcept { return UseCards ? 0 : -1; }

    [[nodiscard]] std::string selectedEventPath() {
        if constexpr (UseCards) {
            // DはTable.getSelectionIndex()
            // で最初のモデル行を選び、Ctrlクリックの順序には依存
            // しない。 C++でも同期したTableの選択順を使う。
            const auto& table_selection = cards_ && !cards_->selection.empty()
                ? cards_->selection : selected_cards_;
            if (!table_selection.empty() && table_selection.front() < cardCount()) {
                auto& card = areaCard(table_selection.front());
                namespace cpath = ::cwxeditor::cwx::path;
                // DのopenEventはカード自身のパスを先に解決し、
                // 存在する場合だけ最初のイベントツリーを加える。 ツリ
                // ーがないカードでも、イベントのルートタブは選択する。
                std::string path = cpath::cplast(card.cwxPath(true));
                const auto& trees = card.trees();
                if (!trees.empty() && trees.front()) {
                    path = cpath::cpjoin(std::move(path),
                                         cpath::cplast(trees.front()->cwxPath(true)));
                }
                return path;
            }
        }
        return {};
    }

    void openEvent(bool can_duplicate = false) {
        if constexpr (std::is_same_v<A, Area> || std::is_same_v<A, Battle>) {
            // シナリオのSummaryがなければ移動先でパスを解決できないため、
            // Dと同じくArea/Battleのイベントへの遷移を拒否する。
            if (!summary_) return;
            bool opened{};
            if (context_->open_event) {
                context_->open_event(area_, can_duplicate);
                opened = true;
            } else if (context_->callbacks.open_event) {
                context_->callbacks.open_event(can_duplicate);
                opened = true;
            }
            // MainWindowは通常、旧open_eventではなくAreaWindowContext::view_callbacks
            // を渡す。 どちらの起動経路でもDと同じくエリアのイ
            // ベントを先に開き、 そのタブで最初の選択MenuCard/EnemyCard
            // のイベントツリーを選ぶ。
            if (opened) {
                if (const auto path = selectedEventPath(); !path.empty() &&
                    context_->open_event_path)
                    context_->open_event_path(path, false);
                return;
            }
        }
        if (context_->callbacks.open_event) context_->callbacks.open_event(can_duplicate);
    }
    void openDup() {
        if constexpr (std::is_same_v<A, Area> || std::is_same_v<A, Battle>) {
            if (context_->open_duplicate) {
                context_->open_duplicate(area_);
                return;
            }
        }
        if (context_->callbacks.open_duplicate) context_->callbacks.open_duplicate();
    }
    // Dの画像ペインのダブルクリックはcanEditを通す。
    // 読み取り専用のSceneViewや移動中の画像では、
    // バックエンドが実行通知を出しても編集画面を開かない。
    void edit() {
        if (canEdit() && context_->callbacks.edit) context_->callbacks.edit();
    }
    void editTarget(const SceneEntryKind kind, const std::size_t index) {
        if (!canEdit()) return;
        if (context_->callbacks.edit_target) {
            // クリックした対象は選択状態と分けて保持し、 新しい対象を反映す
            // る前に、ホストが既存のダイアログを再び有効にできるようにする。
            const auto target = kind == SceneEntryKind::Card
                ? areawindow::SceneEditTargetKind::Card
                : areawindow::SceneEditTargetKind::Background;
            context_->callbacks.edit_target(target, index);
        } else {
            edit();
        }
    }
    [[nodiscard]] bool canEdit() const noexcept {
        // ホストの判定で編集条件を厳しくすることはできるが、
        // このビューのD形式の読み取り専用設定は解除しない。
        return !read_only_ && !sceneGeometryMoving() &&
               (lib::editor_window_host_options().all_mode || !scene_keyboard_editing_) &&
               (!selected_cards_.empty() || !selected_backs_.empty()) &&
               (!context_->callbacks.can_edit ||
                               context_->callbacks.can_edit());
    }
    [[nodiscard]] bool isFocusOnListOrPane() const noexcept {
        // DはAreaViewルート内で最後にFocusInした部
        // 品を記憶する。 別ウィンドウへ移っても、同じ場面内の別の
        // 部品がフォーカスを得るまではその部品を操作対象にする。
        const auto gui = context_->services.gui;
        if (!gui) return last_focus_on_list_or_pane_;
        try {
            const auto focus = gui->focused_widget();
            if (focus && lib::focus_target_in_scope(root_, focus, false) == focus)
                return focus == cards_ || focus == backs_ || focus == canvas_;
            return last_focus_on_list_or_pane_;
        } catch (...) { return last_focus_on_list_or_pane_; }
    }

    [[nodiscard]] bool canUp() const noexcept { return isFocusOnListOrPane() && canMoveOrder(-1); }
    [[nodiscard]] bool canDown() const noexcept { return isFocusOnListOrPane() && canMoveOrder(1); }
    void up() { up(1, true, true); }
    void down() { down(1, true, true); }
    void up(int count, bool cards_value, bool backs_value) {
        if (isFocusOnListOrPane()) moveOrder(-std::max(count, 1), cards_value, backs_value);
    }
    void down(int count, bool cards_value, bool backs_value) {
        if (isFocusOnListOrPane()) moveOrder(std::max(count, 1), cards_value, backs_value);
    }

    [[nodiscard]] bool canCreateCard() const noexcept {
        if (read_only_) return false;
        // Dと同じく、選択できるCast定義がなければEnemyCard
        // を作れない。 ツールバー・メニュー・実行処理で同じ判定を使う。
        if constexpr (std::is_same_v<C, EnemyCard>)
            return summary_ && !summary_->casts().empty();
        return true;
    }
    void createCard() {
        if (!canCreateCard()) return;
        if (context_->create_card) context_->create_card();
    }
    void editCard(const std::vector<int>& indices) {
        if constexpr (UseCards) {
            if (read_only_) return;
            selected_cards_.clear();
            for (const int index : indices)
                if (index >= 0 && static_cast<std::size_t>(index) < cardCount())
                    selected_cards_.push_back(static_cast<std::size_t>(index));
            const auto targets = detail::normalizedSelection<C>(selected_cards_, cardCount());
            const std::weak_ptr<void> life = scene_view_lifetime_;
            if (context_->callbacks.edit_target) {
                refreshSelected();
                if (life.expired()) return;
                for (const auto index : targets) {
                    if (life.expired()) return;
                    editTarget(SceneEntryKind::Card, index);
                }
            } else {
                selected_backs_.clear();
                refreshSelected();
                if (life.expired()) return;
                edit();
            }
        }
    }
    void editCard(const std::shared_ptr<C>& target) {
        if constexpr (UseCards) {
            if (read_only_ || !target) return;
            if constexpr (std::is_same_v<C, EnemyCard>)
                if (!summary_ || summary_->casts().empty()) return;
            const auto found = std::find(areaCardArray().begin(),
                                         areaCardArray().end(), target);
            if (found == areaCardArray().end()) return;
            const auto index = static_cast<std::size_t>(
                found - areaCardArray().begin());
            if (context_->callbacks.edit_target) {
                // Dは場面の選択を変える前に既存の編集画面を有効にする。
                // ホストがキャッシュを先に検索して、この順序を守る。
                context_->callbacks.edit_target(
                    areawindow::SceneEditTargetKind::Card, index);
                return;
            }
            selected_cards_ = {index};
            selected_backs_.clear();
            const std::weak_ptr<void> life = scene_view_lifetime_;
            refreshSelected();
            if (!life.expired()) edit();
        }
    }
    void editCardApply(const std::shared_ptr<C>&) { refreshAfterModelChange(); }
    [[nodiscard]] ExternalCardEditPtr beginExternalCardEdit(const std::shared_ptr<C>& target) {
        if constexpr(!UseCards)return {};
        else {
            if(read_only_ || !target || std::find(areaCardArray().begin(),areaCardArray().end(),target)==areaCardArray().end())return {};
            auto transaction=std::make_shared<ExternalCardEdit>();
            transaction->targets.emplace_back(target,shallowCardSnapshot(target));
            if constexpr(requires(A& owner){owner.weak_from_this().lock();})transaction->model_owner=area_->weak_from_this().lock();
            return transaction;
        }
    }
    void beginExternalCardEdit() {
        if constexpr(UseCards) {
            external_card_edit_before_.reset();
            auto indices=detail::normalizedSelection<C>(selected_cards_,cardCount());
            // 従来の対象を渡さないAPI向けの互換処理。 通常のモー
            // ドレス画面は、表の選択によらず正確な対象カードを渡す。
            if(indices.empty()){indices.resize(cardCount());std::iota(indices.begin(),indices.end(),0U);}
            for(const auto index:indices) {
                if(!external_card_edit_before_)external_card_edit_before_=beginExternalCardEdit(areaCardPtr(index));
                else external_card_edit_before_->targets.emplace_back(areaCardPtr(index),shallowCardSnapshot(areaCardPtr(index)));
            }
        }
    }
    [[nodiscard]] bool commitExternalCardEdit(const ExternalCardEditPtr& transaction) {
        if constexpr(!UseCards)return false;
        else {
            if(read_only_ || !transaction || transaction->targets.empty())return false;
            const std::weak_ptr<void> life=scene_view_lifetime_;
            const auto revision=++geometry_revision_;
            std::vector<std::pair<std::shared_ptr<C>,std::shared_ptr<C>>> before,after;
            for(const auto& [weak,saved]:transaction->targets) {
                const auto target=weak.lock();
                if(!target || std::find(areaCardArray().begin(),areaCardArray().end(),target)==areaCardArray().end())continue;
                before.emplace_back(target,saved);after.emplace_back(target,shallowCardSnapshot(target));
            }
            if(before.empty())return false;
            const auto restore=[this,life,owner=transaction->model_owner](const auto& values) {
                if(life.expired())return;
                const auto revision=++geometry_revision_;
                std::vector<std::size_t> indices;
                for(const auto& [target,saved]:values) {
                    const auto found=std::find(areaCardArray().begin(),areaCardArray().end(),target);
                    if(found==areaCardArray().end())continue;
                    indices.push_back(static_cast<std::size_t>(found-areaCardArray().begin()));
                    target->shallowCopyFrom(*saved);
                    if(life.expired() || geometry_revision_!=revision)return;
                }
                refreshAfterModelChange();
                if(life.expired() || geometry_revision_!=revision)return;
                // DのUndoEditは対象モデルの値を更新するが、 ユーザ
                // ーの現在のTable/ImagePaneの選択は変えない。
                notifyReferencedResources(indices,{},true);
                if(life.expired() || geometry_revision_!=revision)return;
                publishSceneChange();
            };
            const auto history_owner=undo_owner_;const auto history=undo_;
            if(history)history->add(std::make_shared<detail::LambdaUndo>([restore,before]{restore(before);},[restore,after]{restore(after);}));
            if(life.expired() || geometry_revision_!=revision)return true;
            // このダイアログの基準状態だけを進め、 他の開いているダイ
            // アログの操作状態や、無関係なカードの属性には触れない。
            transaction->targets.clear();std::vector<std::size_t> indices;
            for(const auto& [target,saved]:after) {
                transaction->targets.emplace_back(target,saved);
                const auto found=std::find(areaCardArray().begin(),areaCardArray().end(),target);
                if(found!=areaCardArray().end())indices.push_back(static_cast<std::size_t>(found-areaCardArray().begin()));
            }
            refreshAfterModelChange();
            if(life.expired() || geometry_revision_!=revision)return true;
            notifyReferencedResources(indices,{},true);
            if(life.expired() || geometry_revision_!=revision)return true;
            publishSceneChange();return true;
        }
    }
    [[nodiscard]] bool commitExternalCardEdit() {
        auto transaction=std::move(external_card_edit_before_);
        return commitExternalCardEdit(transaction);
    }
    void endExternalCardEdit(const ExternalCardEditPtr& transaction)noexcept {if(transaction)transaction->targets.clear();}
    void endExternalCardEdit()noexcept {external_card_edit_before_.reset();}
    // 各モードレスのセル編集画面が、独立した操作の記録を持つ。 ビュ
    // ー全体で一つのスナップショットを使うと、別画面が先のUndoの
    // 基準を上書き・消去し、 無関係なセルまで履歴へ含めてしまう。
    [[nodiscard]] ExternalBackEditPtr beginExternalBackEdit(
        const std::shared_ptr<BgImage>& target) {
        if constexpr (!UseBacks) return {};
        if (read_only_ || !target) return {};
        return std::make_shared<ExternalBackEdit>(
            ExternalBackEdit{target, detail::cloneObject(target)});
    }
    [[nodiscard]] bool commitExternalBackEdit(
        const ExternalBackEditPtr& transaction) {
        if constexpr (!UseBacks) return false;
        if (read_only_ || !transaction || !transaction->before) return false;
        const auto target = transaction->target.lock();
        if (!target || !backIndex(target)) return false;
        const auto before = transaction->before;
        const auto after = detail::cloneObject(target);
        if (!after || *before == *after) return false;
        const std::weak_ptr<void> life=scene_view_lifetime_;
        const auto revision=++geometry_revision_;
        const auto history_owner=undo_owner_; // 共有所有のエントリーは保持し、生ポインターのエントリーは借用のままとする。
        const auto history=undo_;
        const auto detached_restore = detachedBackReplay(target);
        if (history)
            history->add(std::make_shared<detail::LambdaUndo>(
                [this, life, target, before, detached_restore] {
                    if (life.expired()) {
                        if (detached_restore) detached_restore(before);
                        return;
                    }
                    restoreBackValue(target, before);
                },
                [this, life, target, after, detached_restore] {
                    if (life.expired()) {
                        if (detached_restore) detached_restore(after);
                        return;
                    }
                    restoreBackValue(target, after);
                }));
        // 公開Undoコマンドは、このビューを終了したりdispose()から新しい編集を
        // 始めたりする場合がある。 管理オブジェクトを保持し、古い処理の継続は打ち切る。
        if(life.expired() || geometry_revision_!=revision)return true;
        transaction->before = after;
        refreshAfterModelChange();
        if(life.expired() || geometry_revision_!=revision)return true;
        if (const auto index = backIndex(target))
            notifyReferencedResources({}, {*index}, true);
        if(life.expired() || geometry_revision_!=revision)return true;
        publishSceneChange();
        return true;
    }
    void endExternalBackEdit(const ExternalBackEditPtr& transaction) noexcept {
        if (transaction) {
            transaction->target.reset();
            transaction->before.reset();
        }
    }
    [[nodiscard]] bool canWriteCommentC() const noexcept { return UseCards && !selected_cards_.empty() && !read_only_; }
    void writeCommentC(){
        if constexpr(UseCards)if(canWriteCommentC()){
            const auto selected=selected_cards_;const std::weak_ptr<void> life=scene_view_lifetime_;
            std::vector<std::shared_ptr<C>> targets;for(const auto index:selected)if(index<cardCount())targets.push_back(areaCardPtr(index));
            const auto gui=context_->services.gui;const auto owner=cards_;const auto stopped=context_->stop_open;
            // DのTableEdit.enterは、選択されたモードレス画面を開く前に編集を確定す
            // る。 モデルの識別子を取得し、確定処理の再入で別の行へ対象が変わらないようにする。
            if(gui)(void)gui->finish_programmatic_inline_edit(owner,true);
            for(const auto& target:targets){
                if(life.expired())return;const bool stop=stopped && stopped();
                if(life.expired() || stop)return;openSceneComment(target);
            }
        }
    }
    void writeCommentCImpl(int index){if constexpr(UseCards)if(!read_only_ && index>=0 && static_cast<std::size_t>(index)<cardCount())openSceneComment(areaCardPtr(index));}
    [[nodiscard]] bool canWriteCommentB() const noexcept { return UseBacks && !selected_backs_.empty() && !read_only_; }
    void writeCommentB(){
        if constexpr(UseBacks)if(canWriteCommentB()){
            const auto selected=selected_backs_;const std::weak_ptr<void> life=scene_view_lifetime_;
            std::vector<std::shared_ptr<BgImage>> targets;for(const auto index:selected)if(index<backCount())targets.push_back(backPtr(index));
            const auto gui=context_->services.gui;const auto owner=backs_;const auto stopped=context_->stop_open;
            if(gui)(void)gui->finish_programmatic_inline_edit(owner,true);
            for(const auto& target:targets){
                if(life.expired())return;const bool stop=stopped && stopped();
                if(life.expired() || stop)return;openSceneComment(target);
            }
        }
    }
    void writeCommentBImpl(int index){if constexpr(UseBacks)if(!read_only_ && index>=0 && static_cast<std::size_t>(index)<backCount())openSceneComment(backPtr(index));}
    [[nodiscard]] bool canWriteComment()const noexcept{
        const std::weak_ptr<void> life=scene_view_lifetime_;const auto gui=context_->services.gui;
        try{const auto focus=gui ? gui->focused_widget():lib::WidgetPtr{};if(life.expired())return false;
            if(focus && lib::focus_target_in_scope(cards_,focus,false)==focus)return canWriteCommentC();
            if(focus && lib::focus_target_in_scope(backs_,focus,false)==focus)return canWriteCommentB();
        }catch(...){return false;}return true;
    }
    void writeComment(){
        if(!canWriteComment())return;
        const std::weak_ptr<void> life=scene_view_lifetime_;
        const auto gui=context_->services.gui;const auto focus=gui ? gui->focused_widget():lib::WidgetPtr{};
        if(life.expired())return;
        if(focus && lib::focus_target_in_scope(backs_,focus,false)==focus && canWriteCommentB()){writeCommentB();return;}
        if(focus && lib::focus_target_in_scope(cards_,focus,false)==focus && canWriteCommentC()){writeCommentC();return;}
        // Dの全体のコメント操作は、実際にフォーカスがある一覧だけを対象
        // にする。 ホストの上書きは、明示的な追加の注入先として残す。
        const auto fallback=context_->write_comment ? context_->write_comment:context_->callbacks.write_comment;if(fallback)fallback();
    }
    [[nodiscard]] std::shared_ptr<comment::CommentDialog> sceneCommentDialog(const void* target)const{
        const auto found=scene_comment_dialogs_.find(target);return found==scene_comment_dialogs_.end()?nullptr:found->second.dialog;
    }
    [[nodiscard]] std::string previewText(
        std::string base, const usecounter::UseCounter* counter = nullptr) const {
        if (!prop_) return base;
        if (!lib::editor_window_host_options().all_mode) {
            // Dの場面内のTextCellには、簡易書式とグローバルなプレビュー値（null
            // カウンター）を使う。 メッセージ・ダイアログのプレビューとは分ける。
            messageutils::NameMap names;
            ::cwxeditor::cwx::msgutils::VarGetter flags, steps, variants, systems;
            messageutils::getPreviewValues(*prop_, summary_, nullptr,
                messageutils::SPCHAR_TEXT, names, flags, steps, variants, systems);
            return ::cwxeditor::cwx::msgutils::simpleFormatMsg(
                base, flags, steps, variants, systems, names,
                [this](const std::string& version) {
                    return prop_->isTargetVersion(summary_, version);
                }, prop_->sys().prefixSystemVarName());
        }
        Skin::Ptr skin_owner;
        if (const auto* value = activeSkin())
            skin_owner = Skin::Ptr(const_cast<Skin*>(value), [](Skin*) {});
        return messageutils::createSPCharPreview(
            *prop_, summary_, skin_owner, counter, std::move(base), true);
    }

    [[nodiscard]] bool canReversePossibleToRunAway() const noexcept {
        if constexpr (std::is_same_v<A, Battle>)
            return !read_only_ && (!summary_ || !summary_->legacy() ||
                                  !area_->possibleToRunAway());
        else return false;
    }
    [[nodiscard]] bool isPossibleToRunAway() const noexcept {
        if constexpr (std::is_same_v<A, Battle>) return area_->possibleToRunAway();
        else return false;
    }
    void reversePossibleToRunAway() {
        if constexpr (std::is_same_v<A, Battle>) {
            if (!canReversePossibleToRunAway()) return;
            // Battle全体の切り替えはカード自身の属性ではな
            // いが、DWTでは選択中のEnemyCardの資源表
            // 示も無効化する。 参照を考慮した処理を使い、直接
            // の変更でパスを一度通知し、 Undo/Redoの
            // スナップショットでは参照数を一度だけ再計算する。
            if(!mutateSettingWithReferenceNotifications(selected_cards_, {}, [&] {
                area_->possibleToRunAway(!area_->possibleToRunAway());
            }))return;
            refreshAfterModelChange();
        }
    }
    [[nodiscard]] bool canOpenBattleOtherSettings() const noexcept { return std::is_same_v<A, Battle> && !read_only_; }
    void openBattleOtherSettings() {
        if constexpr (std::is_same_v<A, Battle>) {
            BattleOtherSettingsDialog dialog(*context_, *prop_, *area_);
            if (!dialog.open()) return;
            const auto transition = dialog.transition();
            const auto speed = dialog.transitionSpeed();
            const auto anime = dialog.doAnime();
            const auto booster = dialog.ignoreEffectBooster();
            if (area_->battleEndLoseBgTransition() == transition &&
                area_->battleEndLoseBgTransitionSpeed() == speed &&
                area_->battleEndLoseBgDoAnime() == anime &&
                area_->battleEndLoseBgIgnoreEffectBooster() == booster) return;
            if(!mutateSettingWithReferenceNotifications({}, {}, [&] {
                area_->battleEndLoseBgTransition(transition);
                area_->battleEndLoseBgTransitionSpeed(speed);
                area_->battleEndLoseBgDoAnime(anime);
                area_->battleEndLoseBgIgnoreEffectBooster(booster);
            }))return;
            refreshAfterModelChange();
        }
    }

    void setupMenu(const std::shared_ptr<lib::Menu>& menu) {
        if (!menu) return;
        menu_ = menu;
        rebuildSceneMenu();
        const std::weak_ptr<void> life=scene_view_lifetime_;
        menu_->on_open = [this,life] {
            if(life.expired())return;
            switch (context_menu_target_) {
            case ContextMenuTarget::Scene: rebuildSceneMenu(); break;
            case ContextMenuTarget::Cards: rebuildListMenu(true); break;
            case ContextMenuTarget::Backs: rebuildListMenu(false); break;
            }
        };
        menu_->on_command = [this,life](std::string_view command) {
            if(life.expired())return;
            const auto source = context_menu_target_ == ContextMenuTarget::Cards
                ? std::optional<DropTarg>{DropTarg::Card}
                : context_menu_target_ == ContextMenuTarget::Backs
                    ? std::optional<DropTarg>{DropTarg::Back} : std::nullopt;
            if (command == "undo") undo();
            else if (command == "redo") redo();
            else if (command == "up") up();
            else if (command == "down") down();
            else if (command == "refresh") refreshFromMenu();
            else if (command == "edit") edit();
            else if (command == "create_card") createCard();
            else if (command == "create_back") createBackground();
            else if (command == "create_text") createTextCell();
            else if (command == "create_color") createColorCell();
            else if (command == "create_pc") createPCCell();
            else if (command == "cut") { SelectionEvent event{"cut"}; cut(event, source); }
            else if (command == "copy") { SelectionEvent event{"copy"}; copy(event, source); }
            else if (command == "paste") { SelectionEvent event{"paste"}; paste(event); }
            else if (command == "delete") { SelectionEvent event{"delete"}; del(event, source); }
            else if (command == "clone") { SelectionEvent event{"clone"}; clone(event, source); }
            else if (command == "open_event") openEvent(false);
            else if (command == "show_grid") reverseShowGrid();
            else if (command == "show_party") reverseViewParty();
            else if (command == "show_message") reverseViewMsg();
            else if (command == "show_reference_cards") reverseViewRefCards();
            else if (command == "show_debug") reverseDebugMode();
            else if (command == "fixed_cards") reverseFixedCards();
            else if (command == "fixed_cells") reverseFixedCells();
            else if (command == "fixed_background") reverseFixedBackground();
            else if (command == "show_cards") reverseViewCards();
            else if (command == "show_backs") reverseViewBacks();
            else if (command == "auto") setAuto();
            else if (command == "custom") setCustom();
            else if (command == "possible_to_run_away") reversePossibleToRunAway();
            else if (command == "near_top") nearTop();
            else if (command == "near_bottom") nearBottom();
            else if (command == "near_left") nearLeft();
            else if (command == "near_right") nearRight();
            else if (command == "near_center_h") nearCenterH();
            else if (command == "near_center_v") nearCenterV();
            else if (command == "near_center") nearCenter();
            else if (command == "pos_top") posTop();
            else if (command == "pos_bottom") posBottom();
            else if (command == "pos_left") posLeft();
            else if (command == "pos_right") posRight();
            else if (command == "pos_even") posEven();
            else if (command == "scale_min") scaleCMin();
            else if (command == "scale_middle") scaleCMiddle();
            else if (command == "scale_max") scaleCMax();
            else if (command == "scale_big") scaleEvenBig();
            else if (command == "expand_backs") expandBacks();
            else if (command == "scale_small") scaleEvenSmall();
            else if (command == "comment") {
                // Dは一覧の各ポップアップを固有のコメント操作へ結び付ける。 ポップア
                // ップを開いたことで、キーボードフォーカスが一覧から外れる場合がある。
                if (context_menu_target_ == ContextMenuTarget::Cards) {
                    if constexpr (UseCards) writeCommentC();
                    else writeComment();
                } else if (context_menu_target_ == ContextMenuTarget::Backs) {
                    if constexpr (UseBacks) writeCommentB();
                    else writeComment();
                } else {
                    writeComment();
                }
            }
            else if (command == "select_all_cards") selectAllC();
            else if (command == "select_all_backs") selectAllB();
            else if (command == "connected") selectConnectedResource();
            else if (command == "find_card_group") findCardGroup();
            else if (command == "find_cell_name") findCellName();
            else if (command == "sort_with_position") sortWithPosition();
        };
    }
    void setupToolBar0(const std::shared_ptr<lib::ToolBar>& bar) {
        if (!bar) return;
        toolbar_ = bar;
        const auto image = [this](const char* name) {
            return prop_->images().load(dprops::StringView{name});
        };
        const auto separator = lib::ToolItem{"", "", {}, true, false, true};
        const auto tool = [this](const types::MenuID id, std::string command,
                                 const lib::ResourceId icon, const bool enabled = true,
                                 const bool checked = false) {
            lib::ToolItem item;
            item.command = std::move(command);
            item.icon = icon;
            item.enabled = enabled;
            item.checked = checked;
            item.tooltip = prop_->buildTool(id);
            return item;
        };
        const bool all_mode = lib::editor_window_host_options().all_mode;
        toolbar_->single_row = !all_mode;
        std::vector<lib::ToolItem> items;
        if constexpr (std::is_same_v<A, Area> || std::is_same_v<A, Battle>) {
            items.push_back(tool(types::MenuID::EditEvent, "open_event",
                image(std::is_same_v<A, Battle> ? "battle_event.png" : "area_event.png")));
            if (!all_mode && prop_->var().showDuplicateViewInToolBar.value) {
                items.push_back(tool(types::MenuID::EditSceneDup, "open_duplicate",
                    image(std::is_same_v<A, Battle> ? "battle_event_dup.png"
                                                    : "area_event_dup.png")));
            }
            items.push_back(separator);
        }
        if (!all_mode) {
            items.push_back(tool(types::MenuID::Refresh, "refresh", image("refresh.png")));
            items.push_back(separator);
            items.push_back(tool(types::MenuID::Undo, "undo", image("undo.png"), canUndo()));
            items.push_back(tool(types::MenuID::Redo, "redo", image("redo.png"), canRedo()));
            items.push_back(separator);
            items.push_back(tool(types::MenuID::Up, "up", image("up.png"), canUp()));
            items.push_back(tool(types::MenuID::Down, "down", image("down.png"), canDown()));
        } else {
            // ALLでは場面・イベントへの入口と上下移動を先に置き
            // 、 履歴操作は小型の操作欄の別グループとして描く。
            items.push_back(tool(types::MenuID::Up, "up", image("up.png"), canUp()));
            items.push_back(tool(types::MenuID::Down, "down", image("down.png"), canDown()));
        }
        if (!all_mode) {
            items.push_back(separator);
            items.push_back(tool(types::MenuID::ShowParty, "view_party",
                                 image("party_cards.png"), true, view_party_));
            items.push_back(tool(types::MenuID::ShowMsg, "view_message",
                                 image("evt_message.png"), true, view_message_));
            // これらのDの部品は単独起動の場面ツールバー用。
            // ALLには含めず、定めた部品の順序を保つ。
            if constexpr (!UseCards && UseBacks) {
                items.push_back(tool(types::MenuID::ShowRefCards,
                                     "show_reference_cards",
                                     image("view_ref.png"), true,
                                     show_ref_cards_));
            }
            if constexpr (std::is_same_v<C, EnemyCard> ||
                          (!UseCards && UseBacks)) {
                if (summary_)
                    items.push_back(tool(types::MenuID::ShowEnemyCardProp,
                                         "show_debug",
                                         image("card_life.png"), true,
                                         debug_mode_));
            }
            if constexpr (UseCards) {
                items.push_back(separator);
                items.push_back(tool(types::MenuID::AutoArrange, "auto", image("auto.png"),
                                     !read_only_, !spCustom()));
                items.push_back(tool(types::MenuID::ManualArrange, "custom",
                                     image("custom.png"), !read_only_, spCustom()));
            }
        }
        (void)lib::replace_tool_bar_items(toolbar_, std::move(items));
        if (!all_mode) {
            // D はシーンのコマンドを一行に並べる。固定の
            // 188px 幅では、広い単体ウィンドウでもツー
            // ルバーが二行に折り返すため、内容から幅を計測
            // する。setupToolBar0 はグリッド
            // 切り替えなどでも再実行される。layout_item
            // の幅は倍率適用前で、計測時に ui_scaled
            // が掛かるため、前の幅指定を外してから計
            // 測し、文字倍率で割った値を保存する。端数
            // で二行目へ折り返さないよう切り上げる。
            auto item = toolbar_->layout_item();
            item.minimum.width = 0.0F;
            item.preferred.width = 0.0F;
            toolbar_->set_layout_item(item);
            const float natural_width = std::ceil(lib::preferred_ui_size(toolbar_).width /
                                        std::max(0.01F, lib::editor_ui_text_scale()));
            item.minimum.width = natural_width;
            item.preferred.width = natural_width;
            toolbar_->set_layout_item(item);
        }
        toolbar_->on_command = [this,life=std::weak_ptr<void>(scene_view_lifetime_)](std::string_view command) {
                if(life.expired())return;
            if (command == "open_event") {
                if constexpr (std::is_same_v<A, Area> || std::is_same_v<A, Battle>)
                    openEvent(false);
            } else if (command == "open_duplicate") {
                if constexpr (std::is_same_v<A, Area> || std::is_same_v<A, Battle>)
                    openDup();
            } else if (command == "refresh") refreshFromMenu();
            else if (command == "undo") undo();
            else if (command == "redo") redo();
            else if (command == "up") up();
            else if (command == "down") down();
            else if (command == "view_party") reverseViewParty();
            else if (command == "view_message") reverseViewMsg();
            else if (command == "show_reference_cards") reverseViewRefCards();
            else if (command == "show_debug") reverseDebugMode();
            else if (command == "auto") setAuto();
            else if (command == "custom") setCustom();
        };
        setupListToolBars();
        setupSelectionToolBars();
        if (all_mode) {
            if (!all_history_toolbar_)
                all_history_toolbar_ = std::make_shared<lib::ToolBar>();
            lib::ToolItem undo_item = tool(types::MenuID::Undo, "undo",
                                           image("undo.png"), canUndo());
            lib::ToolItem redo_item = tool(types::MenuID::Redo, "redo",
                                           image("redo.png"), canRedo());
            (void)lib::replace_tool_bar_items(all_history_toolbar_,
                                              {std::move(undo_item), separator,
                                               std::move(redo_item)});
            all_history_toolbar_->on_command = [this,life=std::weak_ptr<void>(scene_view_lifetime_)](const std::string_view command) {
                if(life.expired())return;
                if (command == "undo") undo();
                else if (command == "redo") redo();
            };
        }
    }
    [[nodiscard]] std::shared_ptr<lib::Menu> createMenuBar() {
        auto bar = std::make_shared<lib::Menu>(false);
        const std::weak_ptr<void> life = scene_view_lifetime_;
        const std::weak_ptr<lib::Menu> weak_bar = bar;
        // DのcreateBgImagesViewAndMenuと同じく、両方の背
        // 景表示で ポップアップ・ツールバーと同じメニューと場面コマンドを使う。
        bar->on_open = [this, life, weak_bar] {
            const auto target = weak_bar.lock();
            if (life.expired() || !target) return;
            const auto item = [this](types::MenuID id, const char* command, bool enabled) {
                lib::MenuItem result;
                result.text = prop_->buildMenu(id);
                result.icon = prop_->images().menu(id);
                result.command = command;
                result.enabled = enabled;
                return result;
            };
            lib::MenuItem edit;
            edit.text = prop_->buildMenu(types::MenuID::Edit);
            edit.children = {
                item(types::MenuID::Undo, "undo", canUndo()),
                item(types::MenuID::Redo, "redo", canRedo()), {.separator=true},
                item(types::MenuID::Up, "up", canUp()),
                item(types::MenuID::Down, "down", canDown()), {.separator=true},
                item(types::MenuID::Cut, "cut", canDoT()),
                item(types::MenuID::Copy, "copy", canDoC()),
                item(types::MenuID::Paste, "paste", canDoP()),
                item(types::MenuID::Delete, "delete", canDoD()),
                item(types::MenuID::Clone, "clone", canDoClone())};
            lib::MenuItem view;
            view.text = prop_->buildMenu(types::MenuID::View);
            view.children = {item(types::MenuID::Refresh, "refresh", true)};
            lib::MenuItem scene;
            scene.text = prop_->buildMenu(types::MenuID::CardsAndBacks);
            rebuildSceneMenu();
            // どちらのホストでも、既存の三つの重ね表示項目を先頭に保つ。
            for (const auto command : {"show_party", "show_message", "show_reference_cards"})
                for (const auto& child : menu_->items)
                    if (child.command == command) scene.children.push_back(child);
            scene.children.push_back({.separator=true});
            for (const auto& child : menu_->items)
                if (child.command != "show_party" && child.command != "show_message" &&
                    child.command != "show_reference_cards") scene.children.push_back(child);
            const std::function<void(std::vector<lib::MenuItem>&)> bind_actions =
                [&](auto& children) {
                    for (auto& child : children) {
                        if (!child.command.empty()) {
                            child.action = [weak_bar, command=child.command] {
                                if (const auto menu = weak_bar.lock(); menu && menu->on_command)
                                    menu->on_command(command);
                            };
                        }
                        bind_actions(child.children);
                    }
                };
            bind_actions(edit.children);
            bind_actions(view.children);
            bind_actions(scene.children);
            (void)lib::replace_menu_items(target,
                {std::move(edit), std::move(view), std::move(scene)});
        };
        bar->on_command = [this, life](std::string_view command) {
            if (life.expired() || !menu_ || !menu_->on_command) return;
            const auto previous = context_menu_target_;
            context_menu_target_ = ContextMenuTarget::Scene;
            const auto dispatch = menu_->on_command;
            try { dispatch(command); }
            catch (...) {
                if (!life.expired()) context_menu_target_ = previous;
                throw;
            }
            if (!life.expired()) context_menu_target_ = previous;
        };
        bar->on_open();
        return bar;
    }
    [[nodiscard]] bool setupToolBar(const std::shared_ptr<lib::ToolBar>& bar) {
        if (!bar || toolbar_setup_) return false;
        setupToolBar0(bar);
        toolbar_setup_ = true;
        return true;
    }
    void setupTLP(const std::shared_ptr<lib::Container>& parent) {
        // ホストがsetupTLPを繰り返しても、共通ルートは一度だ
        // け接続する。 このビューのメニューの接続は下で更新する。
        if (parent && root_) {
            const auto* const root_widget =
                static_cast<const lib::Widget*>(root_.get());
            const bool already_mounted = std::ranges::any_of(
                parent->children(), [root_widget](const auto& child) {
                    return child.get() == root_widget;
                });
            if (!already_mounted) parent->add(root_);
        }
        registerTopLevelMenuActions();
    }

    int insertIndex(const Table& list) const noexcept {
        // DのAreaView#insertIndexは選択行全体を
        // 並べ、最後の行の直後へ新しいセルを置く。 最初の選択行へ
        // 挿入すると、適用・ドロップの繰り返しで順序が逆転し、 選
        // 択行が上書きされたように見えるため、同じ挿入位置を使う。
        if (list.selection.empty()) return static_cast<int>(list.rows.size());
        const auto last = *std::max_element(list.selection.begin(),
                                            list.selection.end());
        return static_cast<int>(std::min(last + 1U, list.rows.size()));
    }

    int appendCard(const std::shared_ptr<C>& card, bool select = true,
                   bool do_refresh = true, bool from_image_pane = false) {
        if constexpr (!UseCards) return -1;
        if (!card || read_only_) return -1;
        const int index = from_image_pane ? static_cast<int>(cardCount())
                                          : insertIndex(*cards_);
        // Dの値を渡す版は挿入位置を選んでから、ドロップ・取り込
        // みと同じ添字付きの追加処理へ委譲する。 C++の補助オ
        // ーバーロードも、ビューごとの選択を使う同じ経路へ通す。
        appendCard(index, card, select, do_refresh, true);
        return index;
    }
    int insertCreatedCard(const std::shared_ptr<C>& card) {
        if constexpr (!UseCards) return -1;
        if (!card || read_only_) return -1;
        const auto index = static_cast<std::size_t>(
            std::max(insertIndex(*cards_), 0));
        if(!mutateWithReferenceNotifications(
            {index}, {},
            [&] {
                insertCard(index, card);
                selected_cards_ = {index};
                selected_backs_.clear();
            },true,true,false))return static_cast<int>(index);
        refreshAfterModelChange();
        return static_cast<int>(index);
    }
    int appendCard(const C& card, bool select = true, bool do_refresh = true, bool from_image_pane = false) {
        return appendCard(std::dynamic_pointer_cast<C>(card.dup()), select,
                          do_refresh, from_image_pane);
    }
    void appendCard(int index, const std::shared_ptr<C>& card, bool select = true, bool do_refresh = true, bool = true) {
        if constexpr (UseCards) {
            if (!card || read_only_) return;
            const auto copy = detail::cloneObject(card);
            const auto inserted = index < 0 ? cardCount()
                                             : static_cast<std::size_t>(index);
            auto append = beginAppendedRows();
            const std::weak_ptr<void> life = scene_view_lifetime_;
            const auto redo_selection = [this, life, inserted] {
                if (!life.expired()) selectAppendedRowsForRedo({inserted}, {});
            };
            const auto complete_rows = [this, append, inserted, select, do_refresh] {
                completeAppendedRows(append, {inserted}, {}, select, do_refresh);
            };
            if(!mutateWithReferenceNotifications({inserted}, {}, [&] {
                insertCard(inserted, copy);
                selected_cards_.clear();
                selected_backs_.clear();
                if (select && view_cards_) selected_cards_ = {inserted};
            }, do_refresh,true,false,false,false,redo_selection,complete_rows))return;
        }
    }
    void appendCards(int index, const std::vector<std::shared_ptr<C>>& values,
                     bool select = true, bool raise_event = true,
                     bool initialize = true) {
        if constexpr (UseCards) {
            if (read_only_ || values.empty()) return;
            auto at = index < 0 ? cardCount()
                                : std::min(static_cast<std::size_t>(index), cardCount());
            std::vector<std::shared_ptr<C>> copies;
            copies.reserve(values.size());
            for (const auto& value : values)
                if (value) copies.push_back(detail::cloneObject(value));
            if (copies.empty()) return;

            auto append = beginAppendedRows();
            // Dの初期化経路ではこのビューだけを更新する。 通常の一括追
            // 加では同じAreaの全ビューを更新し、既存の選択は保つ。
            if (initialize) append.peers.clear();
            std::vector<std::size_t> inserted;
            inserted.reserve(copies.size());
            for (std::size_t offset = 0; offset < copies.size(); ++offset)
                inserted.push_back(at + offset);
            const std::weak_ptr<void> life = scene_view_lifetime_;
            const auto last_inserted = inserted.back();
            const auto redo_selection = [this, life, last_inserted] {
                if (!life.expired()) selectAppendedRowsForRedo({last_inserted}, {});
            };
            const auto complete_rows = [this, append, inserted, select, initialize] {
                completeAppendedRows(append, inserted, {}, select, !initialize,
                                     true, false, true);
            };
            if (!mutateWithReferenceNotifications(
                    inserted, {}, [&] {
                        for (std::size_t offset = 0; offset < copies.size(); ++offset)
                            insertCard(at + offset, copies[offset]);
                    }, raise_event, true, raise_event, false, !initialize,
                    redo_selection, complete_rows)) return;
        }
    }
    int appendBgImage(const std::shared_ptr<BgImage>& image, bool select = true,
                       bool do_refresh = true, bool from_image_pane = false) {
        if constexpr (!UseBacks) return -1;
        else {
            if (!image || read_only_) return -1;
            const int index = from_image_pane ? static_cast<int>(backCount())
                                              : insertIndex(*backs_);
            // Dの補助オーバーロードと同じく、添字付きの追加処理へ委譲する。
            appendBgImage(index, image, select, do_refresh, true);
            return index;
        }
    }
    // モードレスの背景セル編集画面は、最初の適用後もこの同じインスタン
    // スを所有・編集する。 クリップボード・取り込みの挿入と異なり、こ
    // こでは複製しない。 挿入はSceneViewのスナップショット経
    // 由とし、適用→Undo→Redoでも DのAreaView#createBackgroundImpl
    // と同じ選択・参照通知を保つ。
    int insertCreatedBgImage(const std::shared_ptr<BgImage>& image) {
        if constexpr (!UseBacks) return -1;
        else {
            if (!image || read_only_) return -1;
            const auto index = static_cast<std::size_t>(
                std::max(insertIndex(*backs_), 0));
            if(!mutateWithReferenceNotifications({}, {index}, [&] {
                insertBack(index, image);
                selected_backs_ = {index};
                selected_cards_.clear();
            },true,true,false))return static_cast<int>(index);
            refreshAfterModelChange();
            return static_cast<int>(index);
        }
    }
    int appendBgImage(const BgImage& image, bool select = true, bool do_refresh = true, bool from_image_pane = false) { return appendBgImage(std::shared_ptr<BgImage>(image.dup()), select, do_refresh, from_image_pane); }
    void appendBgImage(int index, const std::shared_ptr<BgImage>& image, bool select = true, bool do_refresh = true, bool = true) {
        if constexpr (UseBacks) {
            if (!image || read_only_) return;
            const auto copy = detail::cloneObject(image);
            const auto inserted = index < 0 ? backCount()
                                             : static_cast<std::size_t>(index);
            auto append = beginAppendedRows();
            const std::weak_ptr<void> life = scene_view_lifetime_;
            const auto redo_selection = [this, life, inserted] {
                if (!life.expired()) selectAppendedRowsForRedo({}, {inserted});
            };
            const auto complete_rows = [this, append, inserted, select, do_refresh] {
                completeAppendedRows(append, {}, {inserted}, select, do_refresh);
            };
            if(!mutateWithReferenceNotifications({}, {inserted}, [&] {
                insertBack(inserted, copy);
                selected_cards_.clear();
                selected_backs_.clear();
                if (select && view_backs_) selected_backs_ = {inserted};
            }, do_refresh,true,false,false,false,redo_selection,complete_rows))return;
        }
    }
    void appendBgImages(int index, const std::vector<std::shared_ptr<BgImage>>& values, bool select = true, bool = true, bool = true) {
        if constexpr (UseBacks) { auto at = index < 0 ? backCount() : static_cast<std::size_t>(index); if(!mutate([&] { for (const auto& value : values) if (value) insertBack(at++, detail::cloneObject(value)); if (select && at > 0U) { selected_backs_ = {at - 1U}; selected_cards_.clear(); } }))return; refreshAfterModelChange(); }
    }
    void createBackground() { createBackgroundImpl(0); }
    void createTextCell() { createBackgroundImpl(1); }
    void createColorCell() { createBackgroundImpl(2); }
    // Dでは旧形式シナリオでPCセル新規作成を無効にし、他の背景セルは従来どおり許可する。
    [[nodiscard]] bool canCreatePCCell() const noexcept {
        return !read_only_ && !(summary_ && summary_->legacy());
    }
    void createPCCell() { createBackgroundImpl(3); }
    void createBackgroundImpl(int type) {
        if constexpr (UseBacks) {
            if (read_only_) return;
            if (type == 3 && !canCreatePCCell()) return;
            if (context_->create_background) { context_->create_background(type); return; }
            std::shared_ptr<BgImage> value;
            if (type == 1) {
                // Dと同じく、新しいテキストセルは設定された既定サイズ（100x100）で作る。
                auto cell = std::make_shared<background::TextCell>();
                cell->width(prop_->var().textCellDefaultWidth.value);
                cell->height(prop_->var().textCellDefaultHeight.value);
                value = std::move(cell);
            } else if (type == 2) {
                // Dと同じく、色セルはcolorCellDefaultWidth/Heightで作る。
                auto cell = std::make_shared<background::ColorCell>();
                cell->width(prop_->var().colorCellDefaultWidth.value);
                cell->height(prop_->var().colorCellDefaultHeight.value);
                value = std::move(cell);
            } else if (type == 3) {
                // Dと同じく、PCセルはカードのサイズで作る。
                auto cell = std::make_shared<background::PCCell>();
                const auto size = prop_->looks().cardSize();
                cell->width(static_cast<std::int32_t>(size.width));
                cell->height(static_cast<std::int32_t>(size.height));
                value = std::move(cell);
            }
            else value = std::make_shared<background::ImageCell>();
            appendBgImage(value);
        }
    }
    void editBack(const std::vector<int>& indices) {
        if constexpr (UseBacks) {
            if (read_only_) return;
            selected_backs_.clear();
            for (const int index : indices)
                if (index >= 0 && static_cast<std::size_t>(index) < backCount())
                    selected_backs_.push_back(static_cast<std::size_t>(index));
            const auto targets = detail::normalizedSelection<BgImage>(selected_backs_, backCount());
            const std::weak_ptr<void> life = scene_view_lifetime_;
            if (context_->callbacks.edit_target) {
                refreshSelected();
                if (life.expired()) return;
                for (const auto index : targets) {
                    if (life.expired()) return;
                    editTarget(SceneEntryKind::Background, index);
                }
            } else {
                selected_cards_.clear();
                refreshSelected();
                if (life.expired()) return;
                edit();
            }
        }
    }
    void selectBackIndex(const std::size_t index) {
        if constexpr (UseBacks) {
            if (read_only_ || index >= backCount()) return;
            selected_backs_ = {index};
            refreshSelected();
        }
    }
    void editBack(const std::shared_ptr<BgImage>& target) {
        if constexpr (UseBacks) {
            if (read_only_ || !target) return;
            const auto index = backIndex(target);
            if (!index) return;
            if (context_->callbacks.edit_target) {
                // ホストがキャッシュ済みの編集画面を再び有効にするときも、他方の一覧の選
                // 択を保つ。 Dのキャッシュを優先するeditBackと同じ動作にする。
                context_->callbacks.edit_target(
                    areawindow::SceneEditTargetKind::Background, *index);
                return;
            }
            selected_backs_ = {*index};
            selected_cards_.clear();
            const std::weak_ptr<void> life = scene_view_lifetime_;
            refreshSelected();
            if (!life.expired()) edit();
        }
    }
    void editBackApply(const std::shared_ptr<BgImage>&) { refreshAfterModelChange(); }
    [[nodiscard]] const std::vector<std::size_t>& selectedBackIndices() const noexcept {
        return selected_backs_;
    }
    [[nodiscard]] const std::vector<std::size_t>& selectedCardIndices() const noexcept {
        return selected_cards_;
    }
    [[nodiscard]] std::shared_ptr<BgImage> selectedBack() const {
        if constexpr (!UseBacks) return {};
        else return selected_backs_.empty() || selected_backs_.front() >= backCount()
                        ? std::shared_ptr<BgImage>{}
                        : backPtr(selected_backs_.front());
    }
    [[nodiscard]] std::vector<std::shared_ptr<BgImage>> selectedBacks() const {
        std::vector<std::shared_ptr<BgImage>> result;
        if constexpr (UseBacks) {
            for (const auto index : selected_backs_)
                if (index < backCount()) result.push_back(backPtr(index));
        }
        return result;
    }
    [[nodiscard]] bool bgImageCanEdit(const Table&, int) const noexcept {
        return UseBacks && !read_only_;
    }
    void bgImageEditEnd(const Table& table, const int row,
                        const std::shared_ptr<lib::ComboBox>& combo) {
        if (!combo || row < 0) return;
        const auto value = combo->selected_index && *combo->selected_index < combo->items.size()
            ? combo->items[*combo->selected_index].text : combo->edit_text;
        editBackCell(static_cast<std::size_t>(row), 0U, value);
        (void)table;
    }
    [[nodiscard]] bool canFindCellName() const noexcept {
        if constexpr(UseBacks)for(const auto index:selected_backs_)
            if(index<backCount() && !back(index).cellName().empty())return true;
        return false;
    }
    void findCellName() {
        if constexpr (UseBacks) {
            auto indices = detail::normalizedSelection<BgImage>(selected_backs_, backCount());
            // Dと同じく、選択されたTableの行をモデルの行順で走査する。
            std::sort(indices.begin(), indices.end());
            for (const auto index : indices) {
                if (back(index).cellName().empty()) continue;
                const auto callback = context_->find_named_id;
                const auto value = back(index).cellName();
                if (callback) callback(value, true);
                return;
            }
        }
    }
    [[nodiscard]] bool canFindCardGroup() const noexcept {
        if constexpr(UseCards)for(const auto index:selected_cards_)
            if(index<cardCount() && !areaCard(index).cardGroup().empty())return true;
        return false;
    }
    void findCardGroup() {
        if constexpr (UseCards) {
            auto indices = detail::normalizedSelection<C>(selected_cards_, cardCount());
            // Dと同じく、選択されたTableの行をモデルの行順で走査する。
            std::sort(indices.begin(), indices.end());
            for (const auto index : indices) {
                if (areaCard(index).cardGroup().empty()) continue;
                const auto callback = context_->find_named_id;
                const auto value = areaCard(index).cardGroup();
                if (callback) callback(value, false);
                return;
            }
        }
    }
    void sortWithPosition() {
        if constexpr(UseCards) {
            if(read_only_)return;
            const auto indices=indicesFromPosition();if(indices.empty())return;
            std::vector<std::pair<std::size_t,std::size_t>> inverse;
            for(const auto [source,destination]:indices)inverse.emplace_back(destination,source);
            const std::weak_ptr<void> life=scene_view_lifetime_;
            const auto revision=geometry_revision_;
            const auto operation_cards=selected_cards_;
            const auto operation_backs=selected_backs_;
            using Permutation = std::vector<std::pair<std::size_t,std::size_t>>;
            // Undoは要素の格納先のスナップショットを所有する。 D形式の記述子を
            // コピーするだけでは、並べ替えの準備中に現在のモデルを書き換えてしまう。
            using CardOrder = std::vector<std::shared_ptr<C>>;
            const auto live_cards=area_->cards();
            const CardOrder original_cards(live_cards.begin(),live_cards.end());
            auto sorted_cards=original_cards;
            for (const auto [source,destination]:indices) sorted_cards[destination]=original_cards[source];
            std::function<void(const Permutation&,const CardOrder&)> detached_replay;
            if constexpr (std::is_same_v<A,Area> || std::is_same_v<A,Battle>) {
                const auto model=std::dynamic_pointer_cast<A>(area_->weak_from_this().lock());
                const auto summary=summary_ ? summary_->weak_from_this().lock() : nullptr;
                if (model && summary) {
                    const CardOrder identities(original_cards);
                    const std::weak_ptr<areawindow::SceneViewRegistry> registry=context_->scene_view_registry;
                    const auto notify=context_->history_card_indices_refresh;
                    detached_replay=[model,summary,identities,registry,notify,operation_cards,operation_backs](
                            const Permutation& permutation,const CardOrder& expected_order) {
                        const auto& owners=[&]() -> const auto& {
                            if constexpr (std::is_same_v<A,Area>) return summary->areas();
                            else return summary->battles();
                        }();
                        if (std::find(owners.begin(),owners.end(),model)==owners.end())
                            throw std::logic_error("Scene sort history owner is no longer in its Summary");
                        const auto& current=model->cards();
                        if (!std::equal(current.begin(),current.end(),expected_order.begin(),expected_order.end()))
                            throw std::logic_error("Scene sort history card order has changed outside the command");
                        if (current.size()!=identities.size() ||
                            !std::all_of(identities.begin(),identities.end(),[&](const auto& item) {
                                return item && std::find(current.begin(),current.end(),item)!=current.end();
                            }))
                            throw std::logic_error("Scene sort history card membership has changed");
                        for (const auto [source,destination]:permutation)
                            if (source>=current.size() || destination>=current.size())
                                throw std::logic_error("Scene sort history permutation is out of range");
                        model->setCardIndices(permutation);
                        if (notify) notify(model.get(),permutation);
                        if (const auto peers=registry.lock()) {
                            peers->publish(model.get(),nullptr);
                            peers->restoreSelection(model.get(),nullptr,operation_cards,operation_backs);
                        }
                    };
                }
            }
            const auto apply=[this,life,operation_cards,operation_backs,detached_replay](
                                 const auto& permutation, const bool history_replay,const CardOrder& expected_order) {
                if(life.expired()) {
                    if (history_replay && detached_replay) detached_replay(permutation,expected_order);
                    else throw std::logic_error("Scene sort history has no detached model replay");
                    return;
                }
                const auto revision=++geometry_revision_;
                const auto notify=context_->ref_menu_card_indices;
                rememberSceneSelectionKeys();area_->setCardIndices(permutation);
                const auto reorder=[&permutation](auto& values) {
                    const auto before=values;
                    for(const auto [source,destination]:permutation)
                        if(source<before.size() && destination<values.size())values[destination]=before[source];
                };
                reorder(card_images_);reorder(card_preview_images_);
                refreshGeometry();
                if(life.expired() || geometry_revision_!=revision)return;
                if(notify)notify(area_,permutation);
                if(life.expired() || geometry_revision_!=revision)return;
                // DのUndoCardIndicesは作成時の行選択を記録
                // し、Undo・Redoのどちらでも 同じ所有者の全場面
                // へ再適用する。Undoを押した時点の選択で代替しない。
                if (history_replay) {
                    restoreSceneSelection(operation_cards,operation_backs);
                    if(life.expired() || geometry_revision_!=revision)return;
                    if (context_->scene_view_registry)
                        context_->scene_view_registry->restoreSelection(
                            area_,this,operation_cards,operation_backs);
                    if(life.expired() || geometry_revision_!=revision)return;
                }
                // DのrefMenuCardIndicesは同じ行番号の他
                // のビューを更新する。 並べ替え後に選択キーをその行へ結び
                // 直し、カードが変わっても全ホストで同じ行を選択し続ける。
                publishSceneChange(false, true);
            };
            const auto history_owner=undo_owner_; // 共有所有のエントリーは保持し、生ポインターのエントリーは借用のままとする。
            const auto history=undo_;
            if(history)history->add(std::make_shared<detail::LambdaUndo>(
                [apply,inverse,sorted_cards]{apply(inverse,true,sorted_cards);},
                [apply,indices,original_cards]{apply(indices,true,original_cards);}));
            if(life.expired() || geometry_revision_!=revision)return;
            apply(indices,false,original_cards);
        }
    }
    [[nodiscard]] bool canSortWithPosition() const { return !read_only_ && !indicesFromPosition().empty(); }
    [[nodiscard]] std::vector<std::pair<std::size_t, std::size_t>> indicesFromPosition() const {
        std::vector<std::pair<std::size_t, std::size_t>> result;
        if constexpr(UseCards) {
            auto indices=detail::normalizedSelection<C>(selected_cards_,cardCount());
            if(indices.empty()){indices.resize(cardCount());std::iota(indices.begin(),indices.end(),0U);}
            // Dは見た目の順序を、モデル行の昇順の配置先へ対
            // 応付ける。 作者が行を選んだ順序は使わない。
            std::sort(indices.begin(),indices.end());
            auto ordered=indices;
            std::stable_sort(ordered.begin(),ordered.end(),[this](auto a,auto b) {
                return std::pair{areaCard(a).y(),areaCard(a).x()}<std::pair{areaCard(b).y(),areaCard(b).x()};
            });
            for(std::size_t i=0;i<ordered.size();++i)
                if(ordered[i]!=indices[i])result.emplace_back(ordered[i],indices[i]);
        }
        return result;
    }
    [[nodiscard]] bool canSelectConnectedResourceC() const noexcept {
        if constexpr(UseCards)try{return connectedSelection(true).has_value();}catch(...){}
        return false;
    }
    [[nodiscard]] bool canSelectConnectedResourceB() const noexcept {
        if constexpr(UseBacks)try{return connectedSelection(false).has_value();}catch(...){}
        return false;
    }
    [[nodiscard]] bool canSelectConnectedResource() const noexcept {
        return canSelectConnectedResourceC() || canSelectConnectedResourceB();
    }
    void selectConnectedResourceC(){openConnectedSelection(true);}
    void selectConnectedResourceB(){openConnectedSelection(false);}
    void selectConnectedResource(){
        const std::weak_ptr<void> life=scene_view_lifetime_;
        const auto gui=context_->services.gui;
        const auto focus=gui ? gui->focused_widget() : lib::WidgetPtr{};
        if(life.expired())return;
        // Dと同じく、実際にフォーカスがある一覧、カード、背景の順で優先する。
        if constexpr(UseBacks)if(focus && lib::focus_target_in_scope(backs_,focus,false)==focus && canSelectConnectedResourceB()){selectConnectedResourceB();return;}
        if constexpr(UseCards)if(focus && lib::focus_target_in_scope(cards_,focus,false)==focus && canSelectConnectedResourceC()){selectConnectedResourceC();return;}
        if(canSelectConnectedResourceC())selectConnectedResourceC();
        else if(canSelectConnectedResourceB())selectConnectedResourceB();
    }
    void refPath(std::string old_path, std::string new_path, const bool directory) {
        if (directory || !summary_) return;
        // DはPathUserがモデルのパスを改名した後、プレビュー
        // を含む既存の画像を更新する。 ここでは表示だけを変更する。
        const auto root = dutils::filesystemPath(summary_->scenarioPath());
        const auto from = dutils::pathString(root / dutils::filesystemPath(old_path));
        const auto to = dutils::pathString(root / dutils::filesystemPath(new_path));
        bool image_changed = false;
        const auto update = [&](const auto& images) {
            for (const auto& image : images) {
                if (!image) continue;
                // C++の背景画像は編集時の相対パスを保持する。 カード
                // のレイヤーは、既に解決済みの絶対パスを持つ場合がある。
                image_changed = image->updatePath(old_path, new_path) || image_changed;
                image_changed = image->updatePath(from, to) || image_changed;
            }
        };
        if constexpr (UseCards) { update(card_images_); update(card_preview_images_); }
        if constexpr (UseBacks) update(back_images_);
        update(decoration_images_);
        bool row_changed = false;
        if constexpr (UseBacks) {
            // Dと同じく、一致するImageCellのラベルだけを変える。
            // 行の同一性・選択・スクロール位置・無関係な行内編集は維持する。
            for (std::size_t i = 0; backs_ && i < backCount() && i < backs_->rows.size(); ++i) {
                const auto* cell = dynamic_cast<const background::ImageCell*>(&back(i));
                auto& row = backs_->rows[i];
                if (!cell || !utils::cfnmatch(cell->path(), new_path) || row.cells.empty()) continue;
                row.cells.front().text = prop_ ? cell->name(prop_->parent()) : back(i).name();
                row_changed = true;
            }
        }
        const std::weak_ptr<void> life = scene_view_lifetime_;
        if (row_changed) requestWidgetRedraw(backs_);
        if (life.expired()) return;
        if (image_changed) requestSceneRedraw();
    }
    // DのrefImagePaneSelectionFilterと同じく、行を再構成せずキャッシュ画像を更新する。
    void refImagePaneSelectionFilter() {
        if (!prop_ || lib::editor_window_host_options().all_mode) return;
        // 同じ設定通知にはXOR輪郭の設定も含まれる。
        // 値は描画時に読むため、現在の選択も描き直す。
        bool redraw = !selected_cards_.empty() || !selected_backs_.empty();
        if constexpr (UseCards) {
            const bool enabled = prop_->var().showSceneViewSelectionFilter.value;
            for (std::size_t i = 0; i < card_images_.size(); ++i) {
                const auto& image = card_images_[i];
                if (!image || image->hasSelectionFilter == enabled) continue;
                image->hasSelectionFilter = enabled;
                if (image->visible() && std::find(selected_cards_.begin(),
                        selected_cards_.end(), i) != selected_cards_.end()) redraw = true;
            }
        }
        if (redraw) requestSceneRedraw();
    }
    // DのrefSwitchFixedWithCheckBoxと同じく、設定を無効にしたら既存行の固定を解除する。
    void refSwitchFixedWithCheckBox() {
        if (!prop_ || prop_->var().switchFixedWithCheckBox.value ||
            lib::editor_window_host_options().all_mode) return;
        const std::weak_ptr<void> life = scene_view_lifetime_;
        bool cards_changed = false;
        bool backs_changed = false;
        if constexpr (UseCards) {
            ensureCardChecks();
            for (std::size_t i = 0; i < card_grayed_.size(); ++i)
                if (card_grayed_[i]) { card_grayed_[i] = false; cards_changed = true; }
        }
        if constexpr (UseBacks) {
            ensureBackChecks();
            for (std::size_t i = 0; i < back_grayed_.size(); ++i)
                if (back_grayed_[i]) { back_grayed_[i] = false; backs_changed = true; }
        }
        if (cards_changed) refreshCards();
        if (life.expired()) return;
        if (backs_changed) refreshBacks();
        if (life.expired()) return;
        if (cards_changed || backs_changed) checked();
    }
    void checked() { applySceneVisibility(); refreshSelected(true, true); }

    void cut(SelectionEvent& event, std::optional<DropTarg> source = {}) {
        if(read_only_)return;
        if (!source && context_->callbacks.cut) {
            context_->callbacks.cut(event);
            return;
        }
        if (read_only_ || !canDoC()) return;
        const std::weak_ptr<void> life=scene_view_lifetime_;
        const auto revision=geometry_revision_;
        copy(event, source);
        if (life.expired() || geometry_revision_!=revision || !event.handled) return;
        delImpl(source);
    }
    void copy(SelectionEvent& event, std::optional<DropTarg> source = {}) {
        if (!source && context_->callbacks.copy) {
            context_->callbacks.copy(event);
            return;
        }
        auto data = selectedSceneData(source, false);
        if (data.text.empty()) return;
        scene_clipboard_ = data.text;
        // DのxmlCopyは、通常のXML文字列か登録済みのバイト転送かを選ぶ。 変えるのはク
        // リップボードのコピーだけで、ドラッグ情報はselectedSceneDataに残す。
        data = prop_->var().xmlCopy.value
            ? xmlbytestransfer::makeTextPayload(scene_clipboard_)
            : xmlbytestransfer::makeXMLPayload(scene_clipboard_);
        const auto clipboard=context_->services.clipboard;
        const auto refresh_toolbar=context_->refresh_toolbar;
        const std::weak_ptr<void> life=scene_view_lifetime_;
        const auto revision=geometry_revision_;
        if (clipboard) {
            clipboard->clear();
            // C++のホストへの転送中に、ビューが終了したり新しいコピー操作が始まったりする場合がある。
            if(life.expired() || geometry_revision_!=revision)return;
            clipboard->write(data);
        }
        event.handled = true;
        if(life.expired() || geometry_revision_!=revision)return;
        if(refresh_toolbar)refresh_toolbar();
    }
    void paste(SelectionEvent& event) {
        if(read_only_)return;
        if (context_->callbacks.paste) {
            context_->callbacks.paste(event);
            return;
        }
        if (read_only_) return;
        const std::weak_ptr<void> life=scene_view_lifetime_;
        const auto revision=geometry_revision_;
        std::string text = scene_clipboard_;
        const auto clipboard=context_->services.clipboard;
        if (clipboard) {
            // 文字列だけでなく登録済みのXMLバイト列も読む。 外部からのデー
            // タが空でも、サービスがない場合の内部の代替データより優先する。
            text = xmlbytestransfer::CBtoXML(*clipboard);
        }
        if(life.expired() || geometry_revision_!=revision)return;
        if (text.empty()) return;
        std::vector<int> cards_value;
        std::vector<int> backs_value;
        appendFromXML(text, 0, 0, DropTarg::Clipboard, cards_value, backs_value);
        event.handled = !cards_value.empty() || !backs_value.empty();
    }
    void del(SelectionEvent& event, std::optional<DropTarg> source = {}) {
        if(read_only_)return;
        if (!source && context_->callbacks.del) {
            context_->callbacks.del(event);
            return;
        }
        if (!canDoD()) return;
        delImpl(source);
        event.handled = true;
    }
    void clone(SelectionEvent& event, std::optional<DropTarg> source = {}) {
        if(read_only_)return;
        if (!source && context_->callbacks.clone) {
            context_->callbacks.clone(event);
            return;
        }
        if (!canDoClone()) return;
        cloneSelected(source);
        event.handled = true;
    }
    void delImpl(std::optional<DropTarg> source = {}) {
        if (read_only_) return;
        const auto cards = source == DropTarg::Back ? std::vector<std::size_t>{} : selected_cards_;
        const auto backs_value = source == DropTarg::Card ? std::vector<std::size_t>{} : selected_backs_;
        if (cards.empty() && backs_value.empty()) return;
        auto target=snapshot();
        if constexpr(UseCards) {
            const auto erase=[&](auto& identities) {
                auto retained=identities;
                for(const auto index:detail::normalizedSelection<C>(cards,retained.size()))
                    std::erase(identities,retained[index]);
            };
            if constexpr(std::is_same_v<C,MenuCard>)erase(target.menu_identities);
            else erase(target.enemy_identities);
        }
        if constexpr(UseBacks) {
            const auto retained=target.back_identities;
            for(const auto index:detail::normalizedSelection<BgImage>(backs_value,retained.size()))
                std::erase(target.back_identities,retained[index]);
        }
        // Dの種類別のカード・背景削除は、対象の一覧だけを変更する。
        // 他方の選択は保ち、場面から削除する場合だけ両方を外す。
        const auto post_delete_cards = source == DropTarg::Back
            ? selected_cards_ : std::vector<std::size_t>{};
        const auto post_delete_backs = source == DropTarg::Card
            ? selected_backs_ : std::vector<std::size_t>{};
        if(!mutate([&] {
            if(!removeStructureOutside(target))return;
            // 削除後の他方の選択をUndoへ記録する。 Undoでは
            // 削除した行を再選択し、触れなかった種類の選択は保つ。
            selected_cards_ = post_delete_cards;
            selected_backs_ = post_delete_backs;
        }, cards, backs_value))return;
        refreshAfterModelChange();
    }
    void cloneSelected(std::optional<DropTarg> source = {}) {
        if (read_only_) return;
        const auto cards_value = source == DropTarg::Back ? std::vector<std::size_t>{}
            : detail::normalizedSelection<C>(selected_cards_, cardCount());
        const auto backs_value = source == DropTarg::Card ? std::vector<std::size_t>{}
            : detail::normalizedSelection<BgImage>(selected_backs_, backCount());
        if (cards_value.empty() && backs_value.empty()) return;
        if(!mutateWithReferenceNotifications({}, {},[&] {
            std::vector<std::shared_ptr<C>> card_copies;
            std::vector<std::shared_ptr<BgImage>> back_copies;
            if constexpr (UseCards)
                for (const auto index : cards_value)
                    if (index < cardCount()) card_copies.push_back(detail::cloneObject(areaCardPtr(index)));
            if constexpr (UseBacks)
                for (const auto index : backs_value)
                    if (index < backCount()) {
                        auto copy = detail::cloneObject(backPtr(index));
                        copy->comment({}); // DのXMLによる複製では、この保存されないフィールドは含めない。
                        back_copies.push_back(std::move(copy));
                    }
            // Dの複製は貼り付けを使う。すべてを平坦な差分として
            // 扱わず、 背景全体を反映してからカードを挿入する。
            if constexpr (UseBacks) {
                auto at = static_cast<std::size_t>(insertIndex(*backs_));
                selected_backs_.clear();
                for (const auto& value : back_copies) {
                    insertBack(at, value);
                    selected_backs_.push_back(at++);
                }
                if(!notifyAddedIndices({},selected_backs_,false))return;
            }
            if constexpr (UseCards) {
                auto at = static_cast<std::size_t>(insertIndex(*cards_));
                selected_cards_.clear();
                for (const auto& value : card_copies) {
                    insertCard(at, value);
                    selected_cards_.push_back(at++);
                }
                if(!notifyAddedIndices(selected_cards_,{},false))return;
            }
        },true,true,true,true))return;
        refreshAfterModelChange();
    }
    [[nodiscard]] bool canDoTCPD() const noexcept {
        return (UseCards || UseBacks) &&
               (static_cast<bool>(context_->callbacks.cut) ||
                static_cast<bool>(context_->callbacks.copy) ||
                static_cast<bool>(context_->callbacks.paste) ||
                static_cast<bool>(context_->callbacks.del) ||
                static_cast<bool>(context_->callbacks.clone) ||
                static_cast<bool>(prop_));
    }
    [[nodiscard]] bool canDoT() const noexcept {
        return !read_only_ &&
               (static_cast<bool>(context_->callbacks.cut) || canDoC());
    }
    [[nodiscard]] bool canDoC() const noexcept {
        return ((UseCards && !selected_cards_.empty()) ||
                (UseBacks && !selected_backs_.empty())) &&
               (static_cast<bool>(context_->callbacks.copy) ||
                static_cast<bool>(prop_));
    }
    [[nodiscard]] bool canDoP() const noexcept {
        if (read_only_) return false;
        // DのAll/Card/BackgroundのTCPD判定は、クリ
        // ップボードの転送形式を調べる。 内容が場面XMLとして解析でき
        // るかまでは判定しない。未対応の資源XMLでも貼り付けを有効にし
        // 、 実行側で場面の変更を拒否する、Dと同じ二段階の処理にする。
        if (const auto clipboard = context_->services.clipboard)
            return xmlbytestransfer::CBisXML(*clipboard);
        lib::ClipboardData fallback;
        fallback.text = scene_clipboard_;
        return xmlbytestransfer::CBisXML(fallback);
    }
    [[nodiscard]] bool canDoD() const noexcept {
        return !read_only_ && ((UseCards && !selected_cards_.empty()) ||
                              (UseBacks && !selected_backs_.empty()));
    }
    [[nodiscard]] bool canDoClone() const noexcept { return canDoD(); }
    void appendFromXML(std::string_view xml_text, int x, int y, DropTarg target,
                       std::vector<int>& cards_out, std::vector<int>& backs_out) {
        cards_out.clear();
        backs_out.clear();
        if (read_only_ || xml_text.empty()) return;
        try {
            auto node = area::XNode::parse(xml_text);
            const bool image_pane = target == DropTarg::ImagePane;

            auto parsed=parseSceneTransfer(node, target != DropTarg::Clipboard);
            if(moveSameScenePayload(node,target,y,!parsed.cards.empty(),!parsed.backs.empty()).has_value())return;
            auto cards_to_insert=std::move(parsed.cards);
            auto backs_to_insert=std::move(parsed.backs);
            // Dの一覧へのドロップは対象の種類だけを変更する。 クリップボード
            // ・画像ペインの転送は両方の種類を扱い、既存の場面座標を保持する。
            if (!parsed.converted) {
                if(target==DropTarg::Card)backs_to_insert.clear();
                if(target==DropTarg::Back)cards_to_insert.clear();
            }

            if (target == DropTarg::Card && cards_to_insert.empty()) return;
            if (target == DropTarg::Back && backs_to_insert.empty() &&
                !(parsed.converted && !cards_to_insert.empty())) return;
            if (image_pane && cards_to_insert.empty() && backs_to_insert.empty()) return;

            std::size_t card_at = parsed.converted && target != DropTarg::ImagePane
                                       ? static_cast<std::size_t>(insertIndex(*cards_))
                                       : target == DropTarg::Card && y >= 0
                                       ? std::min<std::size_t>(static_cast<std::size_t>(y),
                                                               cardCount())
                                       : target == DropTarg::Clipboard
                                           ? static_cast<std::size_t>(insertIndex(*cards_))
                                           : cardCount();
            std::size_t back_at = target == DropTarg::Back && y >= 0
                                       ? std::min<std::size_t>(static_cast<std::size_t>(y),
                                                               backCount())
                                       : target == DropTarg::Clipboard
                                           ? static_cast<std::size_t>(insertIndex(*backs_))
                                           : backCount();
            if constexpr (UseCards) {
                if (image_pane && parsed.converted) {
                    const auto card_size = prop_->looks().cardSize();
                    const auto insets = std::is_same_v<C, MenuCard>
                                             ? prop_->looks().menuCardInsets()
                                             : prop_->looks().castCardInsets();
                    const int card_x = x - static_cast<int>(card_size.width + insets.e + insets.w) / 2;
                    const int card_y = y - static_cast<int>(card_size.height + insets.n + insets.s) / 2;
                    for (const auto& card_value : cards_to_insert) {
                        if (!card_value) continue;
                        card_value->x(card_x);
                        card_value->y(card_y);
                    }
                }
            }

            const bool clipboard_phase=target==DropTarg::Clipboard;
            if(!clipboard_phase && !parsed.converted) {
                if(target==DropTarg::Card)backs_to_insert.clear();
                if(target==DropTarg::Back)cards_to_insert.clear();
            }
            const bool final_count=clipboard_phase || !parsed.converted || std::is_same_v<C,EnemyCard>;
            std::shared_ptr<std::pair<std::vector<std::size_t>,
                                      std::vector<std::size_t>>> clipboard_added;
            std::function<void()> after_redo;
            std::function<void()> after_model_mutation;
            std::function<void()> after_undo;
            if (clipboard_phase) {
                clipboard_added = std::make_shared<std::pair<
                    std::vector<std::size_t>, std::vector<std::size_t>>>();
                auto append = beginAppendedRows();
                const std::weak_ptr<void> life = scene_view_lifetime_;
                after_model_mutation = [this, life, append = std::move(append), clipboard_added,
                                        &cards_out, &backs_out] {
                    if (life.expired()) return;
                    const auto revision = geometry_revision_;
                    clipboard_added->first.assign(cards_out.begin(), cards_out.end());
                    clipboard_added->second.assign(backs_out.begin(), backs_out.end());
                    completeAppendedRows(append, clipboard_added->first,
                                         clipboard_added->second, true, true, true, true);
                    if (life.expired() || geometry_revision_ != revision) return;
                    // Dと同じく、両方の一括追加と各ビューの行更
                    // 新が終わってから、コピー元の表を更新する。
                    refreshSelected();
                };
                after_redo = [this, life, clipboard_added] {
                    if (life.expired()) return;
                    std::vector<std::size_t> cards;
                    std::vector<std::size_t> backs;
                    // DのUndoDeleteは挿入行ごとに
                    // 単一行の処理を使う。 再生のたびに全ビ
                    // ューの選択を消すため、Redo後は最後
                    // に挿入した種類・行だけが選択される。
                    if (!clipboard_added->second.empty())
                        backs.push_back(clipboard_added->second.back());
                    else if (!clipboard_added->first.empty())
                        cards.push_back(clipboard_added->first.back());
                    selectAppendedRowsForRedo(cards, backs);
                };
                after_undo = [this, life] {
                    if (life.expired()) return;
                    selected_cards_.clear();
                    selected_backs_.clear();
                    rememberSceneSelectionKeys();
                    refreshSelected();
                    if (life.expired()) return;
                    if constexpr (std::is_base_of_v<AbstractArea, A>) {
                        const auto registry = context_->scene_view_registry;
                        const auto* owner = static_cast<const AbstractArea*>(area_);
                        if (registry && owner)
                            registry->restoreSelection(owner, this, {}, {});
                    }
                };
            }
            // DのAllTCPDはUndo状態を取る前に、操作を始
            // めたキャンバスの選択だけを消す。 表の選択は、最後の
            // refreshSelected()まで独立している。
            if (clipboard_phase) {
                selected_cards_.clear();
                selected_backs_.clear();
                rememberSceneSelectionKeys();
            }
            if(!mutateWithReferenceNotifications({}, {}, [&] {
                const auto add_backs=[&]() -> bool {
                if constexpr (UseBacks) {
                    for (const auto& back_value : backs_to_insert) {
                        if (!back_value) continue;
                        insertBack(back_at, detail::cloneObject(back_value));
                        backs_out.push_back(static_cast<int>(back_at++));
                        if(!clipboard_phase && (!notifyAddedIndices({},{back_at-1},false) || !notifySceneUseCount()))return false;
                    }
                    const std::vector<std::size_t> added(backs_out.begin(),backs_out.end());
                    if(clipboard_phase && !notifyAddedIndices({},added,false))return false;
                }
                return true;
                };
                const auto add_cards=[&]() -> bool {
                if constexpr (UseCards) {
                    for (const auto& card_value : cards_to_insert) {
                        if (!card_value) continue;
                        insertCard(card_at, detail::cloneObject(card_value));
                        cards_out.push_back(static_cast<int>(card_at++));
                        if(!clipboard_phase && (!notifyAddedIndices({card_at-1},{},false) || !notifySceneUseCount()))return false;
                    }
                    const std::vector<std::size_t> added(cards_out.begin(),cards_out.end());
                    if(clipboard_phase && !notifyAddedIndices(added,{},false))return false;
                }
                return true;
                };
                // Dのクリップボード貼り付けは背景を先にまとめて反映する。 ドロップ
                // ・取り込みは、カードを一つずつ、その後に背景を一つずつ反映する。
                if(clipboard_phase){if(!add_backs() || !add_cards())return;}
                else {if(!add_cards() || !add_backs())return;}
                selected_cards_.assign(cards_out.begin(), cards_out.end());
                selected_backs_.assign(backs_out.begin(), backs_out.end());
            }, final_count,true,clipboard_phase,true,!clipboard_phase,
                std::move(after_redo),std::move(after_model_mutation),
                std::move(after_undo)))return;
            if (!clipboard_phase) refreshAfterModelChange();
        } catch (const std::exception& error) {
            // Dと同じく、不正な転送XMLはドロップの入口で捕捉して記録し、
            // 外部のドラッグデータによる例外をイベントループへ漏らさない。
            if (context_ && context_->status) {
                context_->status(error.what());
            }
            return;
        }
    }
    [[nodiscard]] int cardFromFile(std::string file, int x, int y, bool from_image_pane) {
        if (read_only_ || file.empty()) return -1;
        if constexpr (UseCards) {
            area::CardImageArray paths;
            paths.push_back(std::make_shared<card::CardImage>(std::move(file)));
            std::shared_ptr<C> value;
            if constexpr (std::is_same_v<C, MenuCard>) {
                value = std::make_shared<MenuCard>("", true, paths, "", "", x, y, 100U,
                                                   types::LAYER_MENU_CARD, "", -1);
            } else if constexpr (std::is_same_v<C, EnemyCard>) {
                value = std::make_shared<EnemyCard>(0U, area::ActionMap{}, "", x, y, 100U,
                                                    types::LAYER_MENU_CARD, "", -1, false,
                                                    "", true, paths);
            }
            return appendCard(value, true, true, from_image_pane);
        }
        return -1;
    }
    [[nodiscard]] int backFromFile(std::string file, int x, int y, int width, int height,
                                   bool from_image_pane) {
        if constexpr (UseBacks) {
            if (read_only_ || file.empty()) return -1;
            auto value = std::make_shared<background::ImageCell>(std::move(file), "", x, y,
                                                                  width, height, false);
            return appendBgImage(std::move(value), true, true, from_image_pane);
        }
        return -1;
    }
    void moveItems(Table& list,int from,int to){
        if(&list==cards_.get())(void)moveSceneItems(DropTarg::Card,from,to);
        else if(&list==backs_.get())(void)moveSceneItems(DropTarg::Back,from,to);
    }
    void refreshCast(const std::shared_ptr<card::CastCard>& cast) {
        refreshCastPresentation(cast, false);
    }
    void deleteCast(const std::shared_ptr<card::CastCard>& cast) {
        refreshCastPresentation(cast, true);
    }

    [[nodiscard]] bool canUndo() const noexcept { return !read_only_ && undo_ && undo_->canUndo(); }
    [[nodiscard]] bool canRedo() const noexcept { return !read_only_ && undo_ && undo_->canRedo(); }
    void undo() {
        if(read_only_)return;
        const std::weak_ptr<void> life=scene_view_lifetime_;
        const auto toolbar=context_?context_->refresh_toolbar:lib::Action{};
        const auto history_owner=undo_owner_;
        const auto history=undo_;
        if (history && history->undo()) {
            if(life.expired())return;
            // UndoManagerは、コマンドの復元通知が終わって
            // から履歴位置を進める。 位置が確定した後に再評価し、
            // 復元された場面と同じフレームでRedoを有効にする。
            refreshPrimaryToolBarEnablement();
            if(life.expired())return;
            if(toolbar)toolbar();
        }
    }
    void redo() {
        if(read_only_)return;
        const std::weak_ptr<void> life=scene_view_lifetime_;
        const auto toolbar=context_?context_->refresh_toolbar:lib::Action{};
        const auto history_owner=undo_owner_;
        const auto history=undo_;
        if (history && history->redo()) {
            if(life.expired())return;
            refreshPrimaryToolBarEnablement();
            if(life.expired())return;
            if(toolbar)toolbar();
        }
    }
    [[nodiscard]] std::vector<std::string> openedCWXPath() const {
        std::vector<std::string> result;
        if constexpr (UseCards) {
            auto indices = detail::normalizedSelection<C>(selected_cards_, cardCount());
            // DWTのTable.getSelectionIndicesは、キャ
            // ンバスで選んだ順序によらず、 選択されたモデル行を行順に列挙する。
            std::sort(indices.begin(), indices.end());
            for (const auto index : indices)
                if (index < cardCount())
                    result.push_back(const_cast<C&>(areaCard(index)).cwxPath(true));
        }
        if constexpr (UseBacks) {
            auto indices = detail::normalizedSelection<BgImage>(selected_backs_, backCount());
            std::sort(indices.begin(), indices.end());
            for (const auto index : indices)
                if (index < backCount()) result.push_back(back(index).cwxPath(true));
        }
        if (result.empty() && area_) result.push_back(area_->cwxPath(true));
        return result;
    }
    [[nodiscard]] bool openCWXPath(std::string_view value, bool shell_activate) {
        namespace cpath = ::cwxeditor::cwx::path;
        const bool no_focus = cpath::cphasattr(value, "nofocus");
        if (cpath::cpempty(value)) {
            if (!no_focus && canvas_ && context_->services.gui) {
                if (shell_activate && root_) context_->services.gui->focus(root_);
                context_->services.gui->focus(canvas_);
            }
            if (context_->refresh_toolbar) context_->refresh_toolbar();
            return true;
        }

        const auto category = cpath::cpcategory(value);
        const auto index = cpath::cpindex(value);
        const bool only = cpath::cphasattr(value, "only");
        const bool open_dialog = cpath::cphasattr(value, "opendialog");
        const auto focus = [&](const lib::WidgetPtr& widget) {
            if (widget && context_->services.gui) {
                if (shell_activate && root_) context_->services.gui->focus(root_);
                context_->services.gui->focus(widget);
            }
        };

        if constexpr (UseCards) {
            const bool card_category =
                (std::is_same_v<C, MenuCard> && category == "menucard") ||
                (std::is_same_v<C, EnemyCard> && category == "enemycard");
            if (card_category) {
                // Dは添字の検査前にonlyを適用するため、 不正・
                // 範囲外のパスでも、その選択状態への作用を維持する。
                if (only) selected_cards_.clear();
                if (index >= cardCount()) {
                    if (only) refreshSelected();
                    return false;
                }
                if (open_dialog) {
                    editCard({static_cast<int>(index)});
                    return true;
                }
                // DのTable.select(indices)は対
                // 象一覧の選択を置き換え、他方の一覧は独立に保つ。
                selected_cards_.clear();
                selected_cards_.push_back(index);
                focus(cards_);
                openCWXPathImpl();
                if (!no_focus) focus(canvas_);
                return true;
            }
        }
        if constexpr (UseBacks) {
            if (category == "background") {
                if (only) selected_backs_.clear();
                if (index >= backCount()) {
                    if (only) refreshSelected();
                    return false;
                }
                if (open_dialog) {
                    editBack({static_cast<int>(index)});
                    return true;
                }
                // 背景にも、Dと同じ対象一覧の選択規則を適用する。
                selected_backs_.clear();
                selected_backs_.push_back(index);
                focus(backs_);
                openCWXPathImpl();
                if (!no_focus) focus(canvas_);
                return true;
            }
        }
        return false;
    }
    void openCWXPathImpl() {
        refreshSelected();
        if (cards_ && !cards_->selection.empty())
            (void)::cwxeditor::lib::reveal_table_row(*cards_, cards_->selection.front());
        if (backs_ && !backs_->selection.empty())
            (void)::cwxeditor::lib::reveal_table_row(*backs_, backs_->selection.front());
        if (context_->refresh_toolbar) context_->refresh_toolbar();
    }

    // DのAreaView.refresh()は表示だけを更新する。 ユーザー編集ではrefreshAfterModelChange()
    // から変更通知を明示的に発生させる。
    void refresh() { refreshContents(false); }
    void refreshFromMenu() {
        const std::weak_ptr<void> life = scene_view_lifetime_;
        const auto revision = geometry_revision_;
        const bool cancel_drag = scene_dragging_;
        if (cancel_drag) finishSceneDrag(false);
        if (life.expired()) return;
        const auto expected_revision = revision + (cancel_drag ? 1U : 0U);
        if (geometry_revision_ != expected_revision) return;
        refresh();
    }
    void refreshAfterModelChange() { refreshContents(true); }
    void refreshPresentation() { refreshContents(false); }
    void refreshDecorationPresentation() {
        const std::weak_ptr<void> life = scene_view_lifetime_;
        const auto revision = geometry_revision_;
        rebuildDecorationImages();
        if (life.expired() || geometry_revision_ != revision) return;
        requestSceneRedraw();
        if (life.expired() || geometry_revision_ != revision) return;
        refreshPrimaryToolBarEnablement();
    }
    void refreshContents(const bool notify_model) {
        const std::weak_ptr<void> life=scene_view_lifetime_;
        const auto revision=geometry_revision_;
        const auto valid=[&]{return !life.expired() && geometry_revision_==revision;};
        const auto toolbar=context_->refresh_toolbar;
        // DのrefreshPanelは全FlexImageを置き換え、それぞれのnewRの編集中の値を破棄する。
        discardSceneKeyboardDrafts();
        if constexpr (std::is_same_v<A, Battle>) syncBattleBGMSelector();
        if constexpr (std::is_same_v<A, Battle>) syncBattleRunAwayControl();
        updateInheritBacks();
        refreshListToolBarEnablement();
        if(!valid())return;
        rebuildDecorationImages();
        if(!valid())return;
        if constexpr (UseCards) { rebuildCardImages(); refreshCards(); }
        if(!valid())return;
        if constexpr (UseBacks) { rebuildBackImages(); refreshBacks(); }
        if(!valid())return;
        applySceneVisibility();
        refreshFlagRows();
        refreshSelected();
        if(!valid())return;
        // refreshSelectedは場面とそのローカルなツールバーを更新
        // する。 モデル・Undoの通知は、DのrefreshPanelによる
        // 所有者のツールバー更新とは分ける。 後者は表示だけの更新でも必要。
        if (notify_model) callModEvent();
        if(!valid())return;
        refreshSceneComments();
        if(!valid())return;
        if(toolbar)toolbar();
    }

protected:
    void retireSceneCallbacks()noexcept {
        read_only_=true; // 完全に終了した後は、保持されていたモデル・履歴への直接コマンドも拒否する。
        if(!scene_view_lifetime_)return;
        clearTopLevelMenuActions();
        scene_view_lifetime_.reset();
        if (context_ && context_->area_window) {
            context_->area_window->signals.ref_cast.disconnect(ref_cast_connection_);
            context_->area_window->signals.del_cast.disconnect(del_cast_connection_);
        }
        ref_cast_connection_ = 0U;
        del_cast_connection_ = 0U;
        const auto disconnect=std::move(disconnect_scene_comments_);if(disconnect)try{disconnect();}catch(...){}
        auto comments=std::move(scene_comment_dialogs_);scene_comment_dialogs_.clear();
        for(auto& [key,entry]:comments)if(entry.dialog){entry.dialog->applied={};entry.dialog->closed={};try{entry.dialog->forceCancel();}catch(...){}}
        auto callbacks=std::move(scene_retirement_callbacks_);
        for(auto& callback:callbacks)try{if(callback)callback();}catch(...){}
    }
private:
    [[nodiscard]] bool focusCanvasBeforeImageVisibilityToggle(
        const bool cards_visible, const bool backs_visible) {
        const std::weak_ptr<void> life = scene_view_lifetime_;
        if (life.expired()) return false;
        const auto* const context = context_;
        const auto root = root_;
        const auto canvas = canvas_;
        const auto* const owner = area_;
        const bool image_pane_visible = canvas && canvas->visible();
        const auto services = context ? context->services : lib::Services{};
        // Dは場面のCompositeへフォーカスを移す。C++の共通Panelは入
        // 力を持たないため、 対応する場面のキーボード操作はCanvasが受け持つ。
        if (image_pane_visible)
            dutils::forceFocus(canvas, false, services);

        // フォーカス通知で場面が同期的に終了・再接続される場合がある。 所有
        // 者・部品・ビュー状態が変わったなら、この古い切り替えを適用しない。
        if (life.expired()) return false;
        const auto still_current = [&] {
            return context_ == context && root_ == root && canvas_ == canvas &&
                   area_ == owner && canvas &&
                   (!context_ || context_->services.gui == services.gui) &&
                   canvas->visible() == image_pane_visible &&
                   view_cards_ == cards_visible && view_backs_ == backs_visible;
        };
        if (!still_current()) return false;
        // Dは包含Compositeへフォーカスを移し、ImagePaneから外すこと
        // で保留中のFlexImage配置を確定する。 C++ではCanvasが既にフ
        // ォーカスを持つ場合があるため、表示を変える前に同じ編集を明示的に確定する。
        finishSceneKeyboardEdit(true);
        return !life.expired() && still_current();
    }
    void clearTopLevelMenuActions() noexcept {
        if (top_level_menu_registry_) {
            for (const auto& [id, generation] : top_level_menu_bindings_)
                top_level_menu_registry_->remove(id, generation);
        }
        top_level_menu_bindings_.clear();
        top_level_menu_registry_ = nullptr;
    }

    void registerTopLevelMenuActions() {
        auto* const registry = context_ ? context_->menu_registry : nullptr;
        clearTopLevelMenuActions();
        if (!registry) return;
        top_level_menu_registry_ = registry;
        const std::weak_ptr<void> life = scene_view_lifetime_;
        const auto bind = [this, registry, life](
                              const types::MenuID id, lib::Action action,
                              std::function<bool()> enabled,
                              std::function<bool()> checked = {}) {
            std::function<bool()> checked_action;
            if (checked) {
                checked_action = [life, checked = std::move(checked)] {
                    return !life.expired() && checked();
                };
            }
            const auto generation = registry->put(
                id,
                [life, action = std::move(action)] {
                    if (!life.expired() && action) action();
                },
                [life, enabled = std::move(enabled)] {
                    return !life.expired() && (!enabled || enabled());
                }, std::move(checked_action));
            top_level_menu_bindings_.emplace_back(id, generation);
        };

        // DのsetupTLPはビューの最上位メニューを接続する。 C++ホストでは
        // 、場面ごとのMenuActionRegistryを通じてそれらを実行する。
        bind(types::MenuID::ShowParty, [this] { reverseViewParty(); }, {},
             [this] { return isViewParty(); });
        if constexpr (UseCards && UseBacks) {
            bind(types::MenuID::ShowCard, [this] { reverseViewCards(); }, {},
                 [this] { return isViewCards(); });
            bind(types::MenuID::ShowBack, [this] { reverseViewBacks(); }, {},
                 [this] { return isViewBacks(); });
        }
        if constexpr (UseCards) {
            bind(types::MenuID::AutoArrange, [this] { setAuto(); },
                 [this] { return !read_only_; },
                 [this] { return !spCustom(); });
            bind(types::MenuID::ManualArrange, [this] { setCustom(); },
                 [this] { return !read_only_; },
                 [this] { return spCustom(); });
            bind(types::MenuID::FixedCards, [this] { reverseFixedCards(); }, {},
                 [this] { return isFixedCards(); });
        }
        if constexpr (UseBacks) {
            bind(types::MenuID::FixedCells, [this] { reverseFixedCells(); }, {},
                 [this] { return isFixedCells(); });
            bind(types::MenuID::FixedBackground,
                 [this] { reverseFixedBackground(); }, {},
                 [this] { return isFixedBackground(); });
        }
        bind(types::MenuID::ShowGrid, [this] { reverseShowGrid(); }, {},
             [this] { return isShowGrid(); });
        bind(types::MenuID::Refresh, [this] { refreshFromMenu(); }, {});
        bind(types::MenuID::Undo, [this] { undo(); },
             [this] { return canUndo(); });
        bind(types::MenuID::Redo, [this] { redo(); },
             [this] { return canRedo(); });
        bind(types::MenuID::Up, [this] { up(); },
             [this] { return canUp(); });
        bind(types::MenuID::Down, [this] { down(); },
             [this] { return canDown(); });
        if constexpr (std::is_same_v<A, Battle>) {
            bind(types::MenuID::PossibleToRunAway,
                 [this] { reversePossibleToRunAway(); },
                 [this] { return canReversePossibleToRunAway(); },
                 [this] { return isPossibleToRunAway(); });
        }
    }

    struct AppendTableSelectionState final {
        std::vector<std::shared_ptr<const void>> selected;
        std::optional<std::shared_ptr<const void>> anchor;
        std::optional<std::shared_ptr<const void>> cursor;
        std::vector<std::size_t> original_selection;
        std::optional<std::size_t> original_anchor;
        std::optional<std::size_t> original_cursor;
        std::size_t row_count{};
        std::uint64_t row_order_revision{};
    };
    struct AppendRowsState final {
        AppendTableSelectionState cards;
        AppendTableSelectionState backs;
        std::vector<std::shared_ptr<const void>> scene_cards;
        std::vector<std::shared_ptr<const void>> scene_backs;
    };
    struct AppendRowsTransaction final {
        std::shared_ptr<void> source_state;
        std::vector<areawindow::SceneViewAppendRowsSnapshot> peers;
    };

    [[nodiscard]] std::shared_ptr<void> captureAppendedRowsState() const {
        auto state = std::make_shared<AppendRowsState>();
        const auto capture = [](const std::shared_ptr<Table>& table,
                                AppendTableSelectionState& target,
                                const std::size_t count, const auto& identity_at) {
            if (!table) return;
            target.original_selection = table->selection;
            target.original_anchor = table->selection_anchor;
            target.original_cursor = table->selection_cursor;
            target.row_count = table->rows.size();
            target.row_order_revision = table->row_order_revision;
            for (const auto row : table->selection)
                if (row < count) target.selected.push_back(identity_at(row));
            if (table->selection_anchor && *table->selection_anchor < count)
                target.anchor.emplace(identity_at(*table->selection_anchor));
            if (table->selection_cursor && *table->selection_cursor < count)
                target.cursor.emplace(identity_at(*table->selection_cursor));
        };
        if constexpr (UseCards)
            capture(cards_, state->cards, cardCount(),
                    [this](const std::size_t row) -> std::shared_ptr<const void> {
                        return areaCardPtr(row);
                    });
        if constexpr (UseBacks)
            capture(backs_, state->backs, backCount(),
                    [this](const std::size_t row) -> std::shared_ptr<const void> {
                        return backPtr(row);
                    });
        if constexpr (UseCards)
            for (const auto row : selected_cards_)
                if (row < cardCount()) state->scene_cards.push_back(areaCardPtr(row));
        if constexpr (UseBacks)
            for (const auto row : selected_backs_)
                if (row < backCount()) state->scene_backs.push_back(backPtr(row));
        return state;
    }

    [[nodiscard]] AppendRowsTransaction beginAppendedRows() const {
        AppendRowsTransaction transaction;
        transaction.source_state = captureAppendedRowsState();
        if constexpr (std::is_base_of_v<AbstractArea, A>) {
            const auto registry = context_->scene_view_registry;
            if (registry && area_)
                transaction.peers = registry->captureAppendRows(
                    static_cast<const AbstractArea*>(area_), this);
        }
        return transaction;
    }

    void completeAppendedRows(const AppendRowsTransaction& transaction,
                              const std::vector<std::size_t>& cards,
                              const std::vector<std::size_t>& backs,
                              const bool select, const bool do_refresh,
                              const bool add_to_peer_selection = false,
                              const bool defer_source_table_sync = false,
                              const bool add_to_source_selection = false) {
        const std::weak_ptr<void> life = scene_view_lifetime_;
        const auto revision = geometry_revision_;
        if constexpr (std::is_base_of_v<AbstractArea, A>) {
            const auto registry = context_->scene_view_registry;
            if (registry && area_)
                registry->appendRows(transaction.peers, cards, backs,
                                     select, do_refresh,
                                     add_to_peer_selection);
        }
        if (life.expired() || geometry_revision_ != revision) return;
        completeAppendedRowsForView(cards, backs, select, do_refresh,
                                    transaction.source_state,
                                    add_to_source_selection,
                                    defer_source_table_sync);
    }

    void selectAppendedRowsForRedo(const std::vector<std::size_t>& cards,
                                   const std::vector<std::size_t>& backs) {
        // 入力行は未選別。同じ所有者の各場面で、 Dと同じビ
        // ュー別のカード・背景の表示条件を個別に適用する。
        const std::weak_ptr<void> life = scene_view_lifetime_;
        const auto revision = ++geometry_revision_;
        const auto restore_cards = detail::normalizedSelection<C>(cards, cardCount());
        const auto restore_backs = detail::normalizedSelection<BgImage>(backs, backCount());
        if constexpr (UseCards)
            selected_cards_ = view_cards_
                ? restore_cards
                : std::vector<std::size_t>{};
        if constexpr (UseBacks)
            selected_backs_ = view_backs_
                ? restore_backs
                : std::vector<std::size_t>{};
        rememberSceneSelectionKeys();
        refreshSelected();
        if (life.expired() || geometry_revision_ != revision) return;
        if constexpr (std::is_base_of_v<AbstractArea, A>) {
            const auto registry = context_->scene_view_registry;
            const auto* owner = static_cast<const AbstractArea*>(area_);
            if (registry && owner)
                registry->restoreSelection(owner, this, restore_cards, restore_backs);
        }
    }

    template <typename Getter>
    [[nodiscard]] static std::optional<std::size_t> findAppendedRowIdentity(
        const std::shared_ptr<const void>& identity, const std::size_t count,
        const Getter& get) {
        if (!identity) return {};
        for (std::size_t row = 0; row < count; ++row)
            if (get(row).get() == identity.get()) return row;
        return {};
    }

    template <typename Getter>
    [[nodiscard]] static std::vector<std::size_t> remapAppendedTableSelection(
        const AppendTableSelectionState& state, const std::size_t count,
        const Getter& get) {
        std::vector<std::size_t> selection;
        selection.reserve(state.selected.size());
        for (const auto& identity : state.selected)
            if (const auto row = findAppendedRowIdentity(identity, count, get))
                selection.push_back(*row);
        return selection;
    }

    template <typename Getter>
    [[nodiscard]] static std::vector<std::size_t> remapAppendedSceneSelection(
        const std::vector<std::shared_ptr<const void>>& identities,
        const std::size_t count, const Getter& get) {
        std::vector<std::size_t> selection;
        selection.reserve(identities.size());
        for (const auto& identity : identities)
            if (const auto row = findAppendedRowIdentity(identity, count, get))
                selection.push_back(*row);
        return selection;
    }

    template <typename Getter>
    [[nodiscard]] static std::optional<std::size_t> remapAppendedTableFocus(
        const std::optional<std::shared_ptr<const void>>& identity,
        const std::size_t count, const Getter& get) {
        return identity ? findAppendedRowIdentity(*identity, count, get)
                        : std::optional<std::size_t>{};
    }

    [[nodiscard]] bool insertAppendedCardRows(
        const std::vector<std::size_t>& input,
        const AppendTableSelectionState& table_state) {
        if constexpr (!UseCards) return true;
        if (!cards_) return false;
        ensureCardChecks();
        auto indices = input;
        std::sort(indices.begin(), indices.end());
        indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
        if (indices.empty()) return true;
        const std::weak_ptr<void> life = scene_view_lifetime_;
        const auto revision = geometry_revision_;
        const auto valid = [&] { return !life.expired() && geometry_revision_ == revision; };
        std::vector<lib::TableRow> rows;
        const bool rows_match = cards_->rows.size() + indices.size() == cardCount() &&
                                cards_->row_order_revision == table_state.row_order_revision &&
                                cards_->rows.size() == table_state.row_count;
        if (rows_match) {
            rows = cards_->rows;
            for (const auto index : indices) {
                if (index > rows.size()) { rows.clear(); break; }
                rows.insert(rows.begin() + static_cast<std::ptrdiff_t>(index), cardTableRow(index));
                if (!valid()) return false;
            }
        }
        if (rows.empty() && cardCount() != 0U) {
            rows.reserve(cardCount());
            for (std::size_t index = 0; index < cardCount(); ++index) {
                rows.push_back(cardTableRow(index));
                if (!valid()) return false;
            }
        }
        const bool images_match = card_images_.size() + indices.size() == cardCount() &&
                                  card_preview_images_.size() + indices.size() == cardCount();
        if (images_match) {
            for (const auto index : indices) {
                if (index > card_images_.size() || index > card_preview_images_.size()) {
                    rebuildCardImages();
                    if (!valid()) return false;
                    break;
                }
                auto image = createCardImage(areaCard(index));
                if (!valid()) return false;
                auto preview = createCardPreviewImage(areaCard(index));
                if (!valid()) return false;
                card_images_.insert(card_images_.begin() + static_cast<std::ptrdiff_t>(index),
                                    std::move(image));
                card_preview_images_.insert(
                    card_preview_images_.begin() + static_cast<std::ptrdiff_t>(index),
                    std::move(preview));
            }
        } else {
            rebuildCardImages();
            if (!valid()) return false;
        }
        const auto selected = remapAppendedTableSelection(
            table_state, cardCount(), [this](const auto row) { return areaCardPtr(row); });
        const auto anchor = remapAppendedTableFocus(
            table_state.anchor, cardCount(), [this](const auto row) { return areaCardPtr(row); });
        const auto cursor = remapAppendedTableFocus(
            table_state.cursor, cardCount(), [this](const auto row) { return areaCardPtr(row); });
        if (!lib::replace_table_rows(*cards_, std::move(rows), selected)) return false;
        cards_->selection_anchor = anchor;
        cards_->selection_cursor = cursor;
        if (context_->services.gui)
            context_->services.gui->request_redraw(cards_, std::nullopt);
        return valid();
    }

    [[nodiscard]] bool insertAppendedBackRows(
        const std::vector<std::size_t>& input,
        const AppendTableSelectionState& table_state) {
        if constexpr (!UseBacks) return true;
        if (!backs_) return false;
        ensureBackChecks();
        auto indices = input;
        std::sort(indices.begin(), indices.end());
        indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
        if (indices.empty()) return true;
        const std::weak_ptr<void> life = scene_view_lifetime_;
        const auto revision = geometry_revision_;
        const auto valid = [&] { return !life.expired() && geometry_revision_ == revision; };
        std::vector<lib::TableRow> rows;
        const bool rows_match = backs_->rows.size() + indices.size() == backCount() &&
                                backs_->row_order_revision == table_state.row_order_revision &&
                                backs_->rows.size() == table_state.row_count;
        if (rows_match) {
            rows = backs_->rows;
            for (const auto index : indices) {
                if (index > rows.size()) { rows.clear(); break; }
                rows.insert(rows.begin() + static_cast<std::ptrdiff_t>(index), backTableRow(index));
                if (!valid()) return false;
            }
        }
        if (rows.empty() && backCount() != 0U) {
            rows.reserve(backCount());
            for (std::size_t index = 0; index < backCount(); ++index) {
                rows.push_back(backTableRow(index));
                if (!valid()) return false;
            }
        }
        const bool images_match = back_images_.size() + indices.size() == backCount();
        if (images_match) {
            for (const auto index : indices) {
                if (index > back_images_.size()) {
                    rebuildBackImages();
                    if (!valid()) return false;
                    break;
                }
                auto image = createBackImage(back(index));
                if (!valid()) return false;
                back_images_.insert(back_images_.begin() + static_cast<std::ptrdiff_t>(index),
                                    std::move(image));
            }
        } else {
            rebuildBackImages();
            if (!valid()) return false;
        }
        const auto selected = remapAppendedTableSelection(
            table_state, backCount(), [this](const auto row) { return backPtr(row); });
        const auto anchor = remapAppendedTableFocus(
            table_state.anchor, backCount(), [this](const auto row) { return backPtr(row); });
        const auto cursor = remapAppendedTableFocus(
            table_state.cursor, backCount(), [this](const auto row) { return backPtr(row); });
        if (!lib::replace_table_rows(*backs_, std::move(rows), selected)) return false;
        backs_->selection_anchor = anchor;
        backs_->selection_cursor = cursor;
        if (context_->services.gui)
            context_->services.gui->request_redraw(backs_, std::nullopt);
        return valid();
    }

    void completeAppendedRowsForView(
        const std::vector<std::size_t>& cards,
        const std::vector<std::size_t>& backs,
        const bool select, const bool do_refresh,
        const std::shared_ptr<void>& opaque_state,
        const bool add_to_scene_selection = false,
        const bool defer_table_sync = false) {
        if (!opaque_state) return;
        const auto state = std::static_pointer_cast<AppendRowsState>(opaque_state);
        const std::weak_ptr<void> life = scene_view_lifetime_;
        const auto revision = geometry_revision_;
        const auto valid = [&] { return !life.expired() && geometry_revision_ == revision; };
        if constexpr (UseCards)
            if (!cards.empty() && !insertAppendedCardRows(cards, state->cards)) return;
        if (!valid()) return;
        if constexpr (UseBacks)
            if (!backs.empty() && !insertAppendedBackRows(backs, state->backs)) return;
        if (!valid()) return;

        if (add_to_scene_selection) {
            // クリップボード追加はDの一括処理を使い、他のビューの選択画像の同一性を
            // 保つ。 selectedIndicesはDと同じモデル順で列挙する。
            if constexpr (UseCards)
                selected_cards_ = remapAppendedSceneSelection(
                    state->scene_cards, cardCount(),
                    [this](const auto row) { return areaCardPtr(row); });
            if constexpr (UseBacks)
                selected_backs_ = remapAppendedSceneSelection(
                    state->scene_backs, backCount(),
                    [this](const auto row) { return backPtr(row); });
        } else {
            // Dの単一行追加は、全ビューの画像選択を消す。 クリ
            // ップボードの操作元は、追加通知の前に選択を消す。
            selected_cards_.clear();
            selected_backs_.clear();
        }
        if constexpr (UseCards)
            if (select && view_cards_ && !cards.empty()) {
                const auto appended = detail::normalizedSelection<C>(cards, cardCount());
                selected_cards_.insert(selected_cards_.end(), appended.begin(), appended.end());
            }
        if constexpr (UseBacks)
            if (select && view_backs_ && !backs.empty()) {
                const auto appended = detail::normalizedSelection<BgImage>(backs, backCount());
                selected_backs_.insert(selected_backs_.end(), appended.begin(), appended.end());
            }
        if (add_to_scene_selection) {
            // DのImagePane#selectedIndicesは所有者の
            // 並び順で走査する。 以前の画像と追加画像を選んだ順序は使わない。
            if constexpr (UseCards) {
                std::sort(selected_cards_.begin(), selected_cards_.end());
                selected_cards_.erase(
                    std::unique(selected_cards_.begin(), selected_cards_.end()),
                    selected_cards_.end());
            }
            if constexpr (UseBacks) {
                std::sort(selected_backs_.begin(), selected_backs_.end());
                selected_backs_.erase(
                    std::unique(selected_backs_.begin(), selected_backs_.end()),
                    selected_backs_.end());
            }
        }
        rememberSceneSelectionKeys();
        if (scene_dragging_ && (!cards.empty() || !backs.empty()))
            rebaseSceneDragSnapshotsAfterAppend();
        if (!valid()) return;
        applySceneVisibility();
        if (!valid()) return;

        const bool sync_table_selection = !add_to_scene_selection &&
            !defer_table_sync &&
            do_refresh && select &&
            ((!cards.empty() && view_cards_) || (!backs.empty() && view_backs_));
        if (sync_table_selection) {
            refreshSelected();
            if (!valid()) return;
        }
        if (!do_refresh) return;
        refreshFlags();
        if (!valid()) return;
        // Dのcheckedは表示・固定状態だけを変えるが、C++のchecked()はrefreshSelected()
        // も呼ぶ。 その選択同期には、Dと同じ条件を適用する。
        applySceneVisibility();
        if (!valid()) return;
        callModEvent();
    }

    void refreshCastPresentation(const std::shared_ptr<card::CastCard>& cast,
                                 const bool deleted) {
        // 両通知は反映済みのSummaryを読む。 削除時には、その状態
        // に基づきcreateCardImageが共通の代替画像を作る。
        if (!cast || !summary_ || read_only_) return;
        if constexpr (std::is_same_v<C, EnemyCard>) {
            // DのrefreshCast/deleteCastと同じく、一致する画像
            // ・ラベルだけを更新する。 表の順序・表示位置・場面の配置は維持する。
            const std::weak_ptr<void> life = scene_view_lifetime_;
            const auto revision = geometry_revision_;
            const auto valid = [&] {
                return !life.expired() && geometry_revision_ == revision;
            };
            bool changed = false;
            for (std::size_t index = 0; index < cardCount(); ++index) {
                if (areaCard(index).id() != cast->id()) continue;
                auto image = createCardImage(areaCard(index));
                if (!valid()) return;
                if (image)
                    image->visible(view_cards_ && index < card_checked_.size() &&
                                   card_checked_[index]);
                auto preview = createCardPreviewImage(areaCard(index));
                if (!valid()) return;
                if (index < card_images_.size())
                    card_images_[index] = std::move(image);
                if (index < card_preview_images_.size())
                    card_preview_images_[index] = std::move(preview);
                if (index < cards_->rows.size() && !cards_->rows[index].cells.empty()) {
                    auto& label = cards_->rows[index].cells.front().text;
                    if (deleted) label.clear();
                    else label = cardNameWithGroup(areaCard(index));
                }
                changed = true;
            }
            if (!changed) return;
            refreshSelected(false);
            if (!valid()) return;
            const auto gui = context_->services.gui;
            const auto cards = cards_;
            const auto canvas = canvas_;
            if (gui && cards) gui->request_redraw(cards, std::nullopt);
            if (!valid()) return;
            if (gui && canvas) gui->request_redraw(canvas, std::nullopt);
        } else if constexpr (!UseCards && UseBacks) {
            // DのRefCards用の処理はEnemyCardの行
            // とは異なり、参照カードの装飾レイヤーだけを作り直す。
            refreshDecorationPresentation();
        }
    }

    void rememberSceneSelectionKeys() {
        if constexpr (UseCards) {
            selected_card_keys_.clear();
            selected_card_keys_.reserve(selected_cards_.size());
            for (const auto index : selected_cards_)
                if (index < cardCount()) selected_card_keys_.push_back(areaCardPtr(index).get());
        }
        if constexpr (UseBacks) {
            selected_back_keys_.clear();
            selected_back_keys_.reserve(selected_backs_.size());
            for (const auto index : selected_backs_)
                if (index < backCount()) selected_back_keys_.push_back(backPtr(index).get());
        }
    }

    void normalizeSceneSelection() {
        if constexpr (UseCards)
            selected_cards_ = detail::normalizedSelection<C>(selected_cards_, cardCount());
        if constexpr (UseBacks)
            selected_backs_ = detail::normalizedSelection<BgImage>(selected_backs_, backCount());
    }

    void receiveSceneChange(const bool geometry_only,
                            const bool model_replaced = false) {
        // Undo/Redoの復元は全カード・背景オブジェクト
        // を置き換える。 破棄済みアドレスが別の行へ再利用され
        // る場合があるため、ポインターキーを照合する前に、
        // 他のビューの有効な行選択を新しいモデルへ保持する。
        if (model_replaced) rememberSceneSelectionKeys();
        normalizeSceneSelection();
        if (geometry_only) {
            const std::weak_ptr<void> life=scene_view_lifetime_;
            const auto revision=geometry_revision_;
            refreshGeometry();
            if(life.expired() || geometry_revision_!=revision)return;
            // Dのサイズ変更通知は、各ビューの編集対象画像だけをリ
            // セットする。 現在の配置を基準にし、共通のC++更
            // 新で一時的に消えた他の画像の未変更の作業値を戻す。
            rebaseSceneKeyboardDrafts();
            if(life.expired() || geometry_revision_!=revision)return;
            restoreSceneKeyboardDraftImages();
        } else refreshAfterModelChange();
    }

    void registerSceneView() {
        if constexpr (std::is_base_of_v<AbstractArea, A>) {
            if (!context_->scene_view_registry || !area_) return;
            scene_view_registration_ = context_->scene_view_registry->add(
                static_cast<const AbstractArea*>(area_), this, scene_view_lifetime_,
                [this] { receiveSceneChange(false); },
                [this] { receiveSceneChange(true); },
                [this] { receiveSceneChange(false, true); },
                [this](const std::vector<std::size_t>& cards,
                       const std::vector<std::size_t>& backs) {
                    if constexpr (UseCards)
                        selected_cards_ = view_cards_
                            ? detail::normalizedSelection<C>(cards, cardCount())
                            : std::vector<std::size_t>{};
                    if constexpr (UseBacks)
                        selected_backs_ = view_backs_
                            ? detail::normalizedSelection<BgImage>(backs, backCount())
                            : std::vector<std::size_t>{};
                    rememberSceneSelectionKeys();
                    rebaseSceneDragSnapshotsForRestoredSelection();
                    refreshSelected();
                },
                [this] { return captureAppendedRowsState(); },
                [this](const std::vector<std::size_t>& cards,
                       const std::vector<std::size_t>& backs, const bool select,
                       const bool do_refresh, const bool add_to_peer_selection,
                       const std::shared_ptr<void>& state) {
                    completeAppendedRowsForView(cards, backs, select, do_refresh,
                                                state, add_to_peer_selection);
                });
        }
    }

    void registerCastNotifications() {
        if constexpr (std::is_same_v<C, EnemyCard> || (!UseCards && UseBacks)) {
            if (!context_->area_window || !summary_ ||
                summary_->scenarioPath().empty() || read_only_) return;
            const std::weak_ptr<void> life = scene_view_lifetime_;
            auto& signals = context_->area_window->signals;
            ref_cast_connection_ = signals.ref_cast.connect(
                [this, life](const std::shared_ptr<card::CastCard>& cast) {
                    if (!life.expired()) refreshCast(cast);
                });
            del_cast_connection_ = signals.del_cast.connect(
                [this, life](const std::shared_ptr<card::CastCard>& cast) {
                    if (!life.expired()) deleteCast(cast);
                });
        }
    }

    void publishSceneChange(const bool geometry_only = false,
                            const bool model_replaced = false) {
        if constexpr (std::is_base_of_v<AbstractArea, A>) {
            if (context_->scene_view_registry && area_)
                context_->scene_view_registry->publish(
                    static_cast<const AbstractArea*>(area_), this, geometry_only,
                    model_replaced);
        }
    }

    static AreaViewContext makeContext(areawindow::AreaWindowContext& context) {
        return AreaViewContext(context);
    }

    struct SceneCommentEntry{std::shared_ptr<comment::CommentDialog> dialog;std::function<bool()> exists;std::function<std::string()> title;};
    template<class Target>void openSceneComment(const std::shared_ptr<Target>& target){
        if(!target || read_only_)return;
        const void* key=target.get();
        if(const auto found=scene_comment_dialogs_.find(key);found!=scene_comment_dialogs_.end()){const auto dialog=found->second.dialog;if(dialog)dialog->active();return;}
        const std::weak_ptr<void> life=scene_view_lifetime_;
        const auto factory=context_->make_scene_comment_dialog;
        if(!factory){const auto fallback=context_->write_comment ? context_->write_comment:context_->callbacks.write_comment;if(fallback)fallback();return;}
        auto dialog=factory(target->comment());if(life.expired() || !dialog)return;
        const auto exists=[this,life,target]{
            if(life.expired())return false;
            if constexpr(UseCards && std::is_same_v<Target,C>)return std::find(areaCardArray().begin(),areaCardArray().end(),target)!=areaCardArray().end();
            else return backIndex(target).has_value();
        };
        const auto title=[this,life,target]{
            if(life.expired())return std::string{};
            std::string name;
            if constexpr(UseCards && std::is_same_v<Target,C>)name=cardNameWithGroup(*target);
            else name=target->name(prop_->parent());
            return utils::tryFormat(prop_->requiredMsgs().dlgTitCommentWith.value,name);
        };
        if(!exists())return;
        dialog->title=title();scene_comment_dialogs_.emplace(key,SceneCommentEntry{dialog,exists,title});
        dialog->applied=[this,life,target,exists](const std::string_view next){
            if(life.expired() || !exists() || target->comment()==next)return;
            // Dのカード・セルごとのUndoEditと同じモデル識別と場面Undo
            // APIを使う。 他のモードレス編集画面は独立に保つ。
            if constexpr(UseCards && std::is_same_v<Target,C>){
                const auto transaction=beginExternalCardEdit(target);if(!transaction)return;
                target->comment(std::string(next));(void)commitExternalCardEdit(transaction);
            }else{
                const auto transaction=beginExternalBackEdit(target);if(!transaction)return;
                target->comment(std::string(next));(void)commitExternalBackEdit(transaction);
            }
        };
        dialog->closed=[this,life,key]{
            if(life.expired())return;scene_comment_dialogs_.erase(key);
            if(scene_comment_dialogs_.empty()){const auto disconnect=std::move(disconnect_scene_comments_);if(disconnect)disconnect();}
        };
        // Dはダイアログごとに通知を登録する。共通の所有者接続も同じ有効
        // ダイアログの範囲を対象とし、 最後の画面が閉じたら接続を外す。
        const auto subscribe=context_->subscribe_scene_comment_updates;
        if(!disconnect_scene_comments_ && subscribe){
            auto disconnect=subscribe([this,life]{if(!life.expired())refreshSceneComments();});
            // ホストへの登録が、戻る前にこの単独所有の対象を終了する場合があ
            // る。 受信先の生存を確認するまで、解除処理は局所的に保持する。
            if(life.expired()){if(disconnect)disconnect();return;}
            if(scene_comment_dialogs_.empty() || disconnect_scene_comments_){if(disconnect)disconnect();}
            else disconnect_scene_comments_=std::move(disconnect);
        }
        if(life.expired())return;
        const auto registered=scene_comment_dialogs_.find(key);
        if(registered==scene_comment_dialogs_.end() || registered->second.dialog!=dialog)return;
        (void)dialog->open();
    }
    void refreshSceneComments(){
        const std::weak_ptr<void> life=scene_view_lifetime_;const auto snapshot=scene_comment_dialogs_;
        for(const auto& [key,entry]:snapshot){
            if(life.expired())return;
            if(!entry.exists || !entry.exists()){if(entry.dialog)entry.dialog->forceCancel();continue;}
            if(entry.dialog && entry.title)entry.dialog->title=entry.title();
        }
    }
    struct ConnectedSelection {std::string cwx_path;std::string file_path;};
    template<class Value>std::optional<ConnectedSelection> connectedValue(Value& value)const{
        if(!summary_ || !prop_)return {};
        if constexpr(std::is_same_v<std::remove_cv_t<Value>,EnemyCard>)
            if(const auto resource=summary_->cwCast(value.id()))
                return ConnectedSelection{::cwxeditor::cwx::path::cpaddattr(::cwxeditor::cwx::path::cpaddattr(resource->cwxPath(true),"shallow"),"only"),{}};
        const auto file=value.connectedFile();
        if(summary_->hasMaterial(file,prop_->var().ignorePaths.value))return ConnectedSelection{{},file};
        return {};
    }
    std::optional<ConnectedSelection> connectedSelection(bool cards)const{
        if constexpr(UseCards)if(cards)for(const auto index:selected_cards_)if(index<cardCount())if(auto value=connectedValue(areaCard(index)))return value;
        if constexpr(UseBacks)if(!cards)for(const auto index:selected_backs_)if(index<backCount())if(auto value=connectedValue(back(index)))return value;
        return {};
    }
    void openConnectedSelection(bool cards){
        const std::weak_ptr<void> life=scene_view_lifetime_;
        const auto path=context_->open_cwx_path;const auto file=context_->open_file_path;
        const auto fallback=context_->callbacks.select_connected_resource;const auto stopped=context_->stop_open;
        // Dは最初の選択モデルを試してから、残りの走査でstopOpenを確認す
        // る。 ホストへの停止問い合わせでC++の受信先が終了し得るため、解決
        // 済みのパスを先に取得する。 可否の問い合わせには副作用を持たせない。
        std::vector<std::optional<ConnectedSelection>> candidates;
        // WindowsのDWTのgetSelectionIndexは、選
        // 択内にフォーカス行があればその行を、 なければ最初の選択行を返す
        // 。後続のgetSelectionは選択行をモデル順で列挙する。
        if constexpr (UseCards) {
            if (cards) {
                auto indices = detail::normalizedSelection<C>(selected_cards_, cardCount());
                std::sort(indices.begin(), indices.end());
                const auto primary = cards_ ? lib::table_selection_index(*cards_)
                                             : std::optional<std::size_t>{};
                if (primary && std::find(indices.begin(), indices.end(), *primary) !=
                                   indices.end())
                    candidates.push_back(connectedValue(areaCard(*primary)));
                for (const auto index : indices)
                    candidates.push_back(connectedValue(areaCard(index)));
            }
        }
        if constexpr (UseBacks) {
            if (!cards) {
                auto indices = detail::normalizedSelection<BgImage>(selected_backs_, backCount());
                std::sort(indices.begin(), indices.end());
                const auto primary = backs_ ? lib::table_selection_index(*backs_)
                                             : std::optional<std::size_t>{};
                if (primary && std::find(indices.begin(), indices.end(), *primary) !=
                                   indices.end())
                    candidates.push_back(connectedValue(back(*primary)));
                for (const auto index : indices)
                    candidates.push_back(connectedValue(back(index)));
            }
        }
        if(candidates.empty())return;
        auto value=candidates.front();
        if(!value)for(const auto& candidate:candidates){
            const bool stop=stopped && stopped();if(life.expired() || stop)return;
            if(candidate){value=candidate;break;}
        }
        if(!value)return;
        if(!value->cwx_path.empty()){if(path)(void)path(value->cwx_path,false);else if(fallback)fallback();}
        else if(file)(void)file(value->file_path,false,true);else if(fallback)fallback();
    }
    struct SceneTransfer {
        std::vector<std::shared_ptr<C>> cards;
        std::vector<std::shared_ptr<BgImage>> backs;
        bool converted{};
    };
    [[nodiscard]] SceneTransfer parseSceneTransfer(
        area::XNode& node,
        const bool allow_menu_card_resource_conversion = true) const {
        SceneTransfer result;
        const system::XMLInfo version(prop_->sys(),versioninfo::LATEST_VERSION);
        if constexpr(std::is_same_v<C,EnemyCard>) {
            area::EnemyCardArray cards;
            if(Battle::CfromXML(node,cards,version))result.cards.assign(cards.begin(),cards.end());
            else if(summary_ && node.name()==card::CastCard::XML_NAME_M &&
                    node.attr("summId",false,std::string{})==summary_->id()) {
                const auto converted=EnemyCard::createCardsFromNode(node,version);
                result.cards.assign(converted.begin(),converted.end());result.converted=true;
            }
        } else {
            area::MenuCardArray cards;area::BgImageArray backs;
            if(Area::CBfromXML(node,cards,backs,version)) {
                if constexpr(UseCards && std::is_same_v<C,MenuCard>)result.cards.assign(cards.begin(),cards.end());
                if constexpr(UseBacks)result.backs.assign(backs.begin(),backs.end());
            } else if constexpr(UseCards && std::is_same_v<C,MenuCard>) {
                if (!allow_menu_card_resource_conversion) return result;
                const auto converted=MenuCard::createFromCardNode(node,prop_->var().copyDesc.value,version);
                result.cards.assign(converted.begin(),converted.end());result.converted=true;
            }
        }
        return result;
    }
    [[nodiscard]] lib::ClipboardData selectedSceneData(std::optional<DropTarg> source={}, bool drag_metadata=true) const {
        lib::ClipboardData result;
        if(!prop_)return result;
        // Dのクリップボード保存はLATEST_VERSIONを使い、Summary
        // を要求しない。 背景だけの場面も混在場面も、同じ書き込み処理を使う。
        card::XMLOption option(prop_->sys(),versioninfo::LATEST_VERSION);
        if constexpr(std::is_same_v<C,EnemyCard>) {
            area::EnemyCardArray cards;
            auto indices=detail::normalizedSelection<C>(selected_cards_,cardCount());
            std::sort(indices.begin(),indices.end());
            for(const auto index:indices)cards.push_back(areaCardPtr(index));
            if(!cards.empty())result.text=Battle::CtoXML(cards,option);
        } else {
            area::MenuCardArray cards;area::BgImageArray backs;
            if constexpr(UseCards && std::is_same_v<C,MenuCard>)if(source!=DropTarg::Back) {
                auto indices=detail::normalizedSelection<C>(selected_cards_,cardCount());
                std::sort(indices.begin(),indices.end());
                for(const auto index:indices)cards.push_back(areaCardPtr(index));
            }
            if constexpr(UseBacks)if(source!=DropTarg::Card) {
                auto indices=detail::normalizedSelection<BgImage>(selected_backs_,backCount());
                std::sort(indices.begin(),indices.end());
                for(const auto index:indices)backs.push_back(backPtr(index));
            }
            if(!cards.empty() || !backs.empty())result.text=Area::CBtoXML(cards,backs,option);
        }
        if(!result.text.empty()){
            if(source && drag_metadata){auto node=area::XNode::parse(result.text);node.new_attr("paneId",sceneDragPaneId(*source));
                const auto& table=*source==DropTarg::Back?backs_:cards_;
                if(table)if(const auto cursor=lib::table_selection_index(*table))
                    node.new_attr("cursorIndex",*cursor);
                result.text=node.text();}
            result.formats.emplace("application/x-cwxeditor-scene",std::vector<std::byte>{});
        }
        return result;
    }

    [[nodiscard]] std::string sceneDragPaneId(const DropTarg) const {
        return "scene:"+scene_drag_identity_;
    }
    [[nodiscard]] lib::DragOrigin sceneDragOrigin(const DropTarg source) const {
        lib::DragOrigin result;
        result.pane_id = sceneDragPaneId(source);
        const auto append_card_keys = [&] {
            if constexpr (UseCards)
                for (const auto index : selected_cards_)
                    if (index < cardCount())
                        result.selected_keys.push_back(
                            const_cast<C&>(areaCard(index)).cwxPath(true));
        };
        const auto append_back_keys = [&] {
            if constexpr (UseBacks)
                for (const auto index : selected_backs_)
                    if (index < backCount())
                        result.selected_keys.push_back(back(index).cwxPath(true));
        };
        if (source == DropTarg::Card) {
            append_card_keys();
            if(cards_)result.cursor_index=lib::table_selection_index(*cards_);
        } else if (source == DropTarg::Back) {
            append_back_keys();
            if(backs_)result.cursor_index=lib::table_selection_index(*backs_);
        } else {
            append_card_keys();
            append_back_keys();
        }
        return result;
    }
    [[nodiscard]] bool moveSceneItems(DropTarg kind,int from,int to){
        if(read_only_ || from<0 || to<0 || from==to)return false;
        const bool cards=kind==DropTarg::Card;const auto count=cards?cardCount():backCount();
        if(static_cast<std::size_t>(from)>=count || static_cast<std::size_t>(to)>count)return false;
        const auto& selected=cards?selected_cards_:selected_backs_;
        const int delta=to-from;if(!canMoveOrderArray(selected,delta,count))return false;
        const std::weak_ptr<void> life=scene_view_lifetime_;const auto revision=geometry_revision_;
        // DのmoveItemsはup/downへ委譲し、飛び飛びの選択の間隔を保つ。
        // ネイティブの操作元と、XMLへ保存された情報に同じモデル規則を適用する。
        moveOrder(delta,cards,!cards);
        return !life.expired() && geometry_revision_!=revision;
    }
    [[nodiscard]] std::optional<bool> moveSameScenePayload(area::XNode& node,DropTarg target,int to,bool has_cards,bool has_backs){
        if(target==DropTarg::Clipboard || node.attr("paneId",false,std::string{})!=sceneDragPaneId(target))return {};
        if(target==DropTarg::ImagePane)return false;
        if((target==DropTarg::Card && !has_cards) || (target==DropTarg::Back && !has_backs))return false;
        const auto count=target==DropTarg::Card?cardCount():backCount();
        if(count>static_cast<std::size_t>(std::numeric_limits<int>::max()))return false;
        return moveSceneItems(target,node.attr("cursorIndex",false,-1),to<0?static_cast<int>(count):to);
    }
    [[nodiscard]] bool canImportSceneImages(const DropTarg target) const noexcept {
        if (read_only_ || !summary_ || summary_->scenarioPath().empty() ||
            !context_ || !context_->import_scene_image)
            return false;
        if (target == DropTarg::Card) return UseCards && std::is_same_v<C, MenuCard>;
        if (target == DropTarg::Back) return UseBacks;
        // DはAreaの画像ペインでファイルドロップを受け付け、Battle
        // のXML専用ペインでは受け付けない。 背景専用の編集画面も画像を受
        // け付ける。メニューカードの場面では既存のカード・背景の分類を保つ。
        return std::is_same_v<C, MenuCard> || (!UseCards && UseBacks);
    }
    [[nodiscard]] bool appendImportedImages(const std::vector<std::filesystem::path>& files,
                                            const DropTarg target, const int x,
                                            const int y,
                                            const std::optional<std::size_t> target_index) {
        if (files.empty() || !canImportSceneImages(target)) return false;
        std::vector<std::shared_ptr<C>> cards_to_insert;
        std::vector<std::shared_ptr<BgImage>> backs_to_insert;
        const auto* skin_value = activeSkin();
        const auto import = context_->import_scene_image;
        const auto ref_paths = context_->ref_paths;
        const std::string material_path = skin_value ? skin_value->materialPath()
                                                     : std::string{};
        const std::weak_ptr<void> life = scene_view_lifetime_;
        const auto revision = geometry_revision_;
        // DのGCは取り込み画面・画像変換の呼び出し中も受信先を保持する。 C++
        // のホストはこの単独所有の場面を終了したり、新しい編集を始めたりできるた
        // め、 ホストを事前に取得し、どちらの場合も古いドロップを再開しない。
        for (const auto& source : files) {
            const auto source_path = dutils::pathString(source);
            bool as_card = target == DropTarg::Card;
            if (target == DropTarg::ImagePane) {
                if constexpr (std::is_same_v<C, MenuCard>)
                    as_card = skin_value && skin_value->isCardImage(
                        source_path, false, false);
            }
            if (skin_value &&
                (as_card ? !skin_value->isCardImage(source_path,
                                                     target != DropTarg::ImagePane, false)
                         : !skin_value->isBgImage(source_path)))
                continue;
            const auto kind = as_card ? lib::ImageImportKind::MenuCard
                                      : lib::ImageImportKind::Background;
            const auto result = import(source, kind);
            if (life.expired() || geometry_revision_ != revision) return false;
            // Dはコピー・リンク・キャンセル処理がファイルを拒否したら、残
            // りのドロップも打ち切る。 一部だけ有効なモデルを作らない。
            if (!result.imported()) break;
            const auto path = dutils::pathString(result.model_path);
            if (as_card) {
                if constexpr (std::is_same_v<C, MenuCard>) {
                    area::CardImageArray paths;
                    paths.push_back(std::make_shared<card::CardImage>(path));
                    cards_to_insert.push_back(std::make_shared<MenuCard>(
                        dutils::pathString(result.model_path.stem()), false,
                        std::move(paths), "", "", x, y, 100U,
                        types::LAYER_MENU_CARD, "", -1));
                }
            } else if constexpr (UseBacks) {
                backs_to_insert.push_back(std::make_shared<background::ImageCell>(
                    path, "", x, y, static_cast<int>(result.pixel_width),
                    static_cast<int>(result.pixel_height), false));
            }
        }
        if (cards_to_insert.empty() && backs_to_insert.empty()) return false;
        // Dのカード・背景一覧へのファイルドロップは、ポインター
        // の行を使わず最後の選択行の後へ挿入する。 ポインター座
        // 標を配置に使うのは、画像ペインへ装飾を置く場合だけ。
        (void)target_index;
        std::size_t card_at = target == DropTarg::Card
            ? static_cast<std::size_t>(insertIndex(*cards_)) : cardCount();
        std::size_t back_at = target == DropTarg::Back
            ? static_cast<std::size_t>(insertIndex(*backs_)) : backCount();
        std::vector<std::size_t> selected_cards;
        std::vector<std::size_t> selected_backs;
        if(!mutateWithReferenceNotifications({}, {}, [&] {
            if constexpr (UseCards) {
                for (const auto& value : cards_to_insert) {
                    insertCard(card_at, detail::cloneObject(value));
                    selected_cards.push_back(card_at++);
                }
            }
            if constexpr (UseBacks) {
                for (const auto& value : backs_to_insert) {
                    insertBack(back_at, detail::cloneObject(value));
                    selected_backs.push_back(back_at++);
                }
            }
            this->selected_cards_ = std::move(selected_cards);
            this->selected_backs_ = std::move(selected_backs);
        }, true))return true;
        if (life.expired()) return true;
        // Dのファイルドロップは、リンクを選んだ場合も含め、 取り込ん
        // だモデルを確定してからシナリオの素材ディレクトリを更新する。
        if (ref_paths) {
            ref_paths(material_path);
            if (life.expired()) return true;
        }
        refreshAfterModelChange();
        return true;
    }

    void setupDragDrop() {
        const auto backend = context_ ? context_->services.drag_drop : nullptr;
        if (!backend) return;
        const auto bind_source = [this, &backend](const lib::WidgetPtr& widget,
                                                   const DropTarg source_target) {
            if (!widget) return;
            lib::DragSourceBinding binding;
            binding.source = widget;
            // Dの場面の操作元はLink|Copyを提示する。共通実装
            // ではビュー内の並べ替えにMoveも使うが、 処理後に元を
            // 削除しない。別の場面との転送ではDと同じLinkを使う。
            binding.allowed = read_only_ ? lib::DropOperation::Copy | lib::DropOperation::Link
                : lib::DropOperation::Copy | lib::DropOperation::Move | lib::DropOperation::Link;
            binding.provide_data = [this, widget, source_target,
                                    life=std::weak_ptr<void>(scene_view_lifetime_)] {
                if (life.expired()) return lib::ClipboardData{};
                if (source_target != DropTarg::ImagePane &&
                    (!context_ || !context_->services.gui ||
                     context_->services.gui->focused_widget() != widget))
                    return lib::ClipboardData{};
                return selectedSceneData(source_target);
            };
            binding.provide_origin = [this, source_target,life=std::weak_ptr<void>(scene_view_lifetime_)] {
                return life.expired()?lib::DragOrigin{}:sceneDragOrigin(source_target);
            };
            backend->bind_source(std::move(binding));
        };
        const auto bind_target = [this, &backend](const lib::WidgetPtr& widget,
                                                  const DropTarg target) {
            if (!widget) return;
            lib::DropTargetBinding binding;
            binding.target = widget;
            // Dは場面内のオブジェクトとEditor右上のカードペインから
            // のドラッグに、同じXMLBytesTransferを使う。
            // C++でも共通化し、下の変換でMenuCard/EnemyCard
            // を作る前に、場面専用形式の判定でカードを拒否しない。
            const bool background_canvas_uses_file_transfer_only =
                target == DropTarg::ImagePane && !UseCards && UseBacks;
            if (background_canvas_uses_file_transfer_only) {
                // DのIPDropTargetは、Settings/DefBgImgDialog
                // など背景専用ビューだけでFileTransferを登録する。
                // Summaryなしでも形式の受け入れを可能にするが、on_enter
                // では変更前に Dと同じシナリオ不在の判定を行う。
                binding.accepted_formats = {"text/uri-list"};
            } else {
                binding.accepted_formats = {
                    "application/x-cwxeditor-scene",
                    "application/x-cwxeditor-card-xml",
                    std::string(xmlbytestransfer::XML_BYTES_FORMAT),
                    std::string(xmlbytestransfer::XML_BYTES_MIME)};
                const bool registers_file_transfer =
                    (target == DropTarg::Card && std::is_same_v<C, MenuCard>) ||
                    (target == DropTarg::Back && UseBacks) ||
                    (target == DropTarg::ImagePane &&
                     (std::is_same_v<C, MenuCard> || (!UseCards && UseBacks)));
                if (registers_file_transfer)
                    binding.accepted_formats.push_back("text/uri-list");
            }
            binding.pane_id = sceneDragPaneId(target);
            binding.accepts_target_index = [this, target,life=std::weak_ptr<void>(scene_view_lifetime_)](const std::optional<std::size_t> index) {
                if(life.expired())return false;
                if (target == DropTarg::Card) return !index || *index <= cardCount();
                if (target == DropTarg::Back) return !index || *index <= backCount();
                return !index;
            };
            const auto accept = [this, target,life=std::weak_ptr<void>(scene_view_lifetime_)](lib::DragEvent& event) {
                if(life.expired())return;
                // DのCard/Background/ImagePane
                // のドロップ先は、保存済みシナリオの編集中だけ有効。 Summary
                // を持たない既定スキンの背景編集では、同じビュー実
                // 装を使っていてもXMLの並べ替えを受け付けない。
                if(read_only_ || !summary_ || summary_->scenarioPath().empty()){
                    event.selected=lib::DropOperation::None;event.handled=true;return;
                }
                // Dはカード・場面のドロップをXMLBytesTransferで
                // 受け取る。 Webは登録済みバイト形式だけを渡す場合があるた
                // め、ClipboardData::textだけで判定しない。
                if (xmlbytestransfer::CBisXML(event.data) ||
                    !event.data.text.empty()) {
                    // Dの画像ペインは位置指定・コピーの受け入れ先であり、
                    // 同じ操作元と操作先で並べ替える領域としては使わない。
                    bool same=event.same_pane(),compatible=true;
                    try {
                        auto xml=xmlbytestransfer::CBtoXML(event.data);
                        if(xml.empty())xml=event.data.text;
                        auto node=area::XNode::parse(xml);
                        same=same || node.attr("paneId",false,std::string{})==sceneDragPaneId(target);
                        const auto parsed=parseSceneTransfer(node);
                        const bool has_cards=!parsed.cards.empty();
                        const bool has_backs=!parsed.backs.empty();
                        // DのappendFromXMLは、背景一覧へ落と
                        // した資源カードもMenuCardへ変換する。 受
                        // け入れ判定を共通パーサーのこの動作に合わせる。
                        compatible=target==DropTarg::Card?has_cards:
                            target==DropTarg::Back?(has_backs || (parsed.converted && has_cards)):
                            has_cards || has_backs;
                    } catch (...) { compatible=false; }
                    event.selected = !compatible || (same && target==DropTarg::ImagePane)
                        ? lib::DropOperation::None : same?lib::DropOperation::Move:lib::DropOperation::Link;
                    event.handled = true;
                } else if (!event.data.files.empty() && canImportSceneImages(target)) {
                    event.selected = lib::DropOperation::Copy;
                    event.handled = true;
                }
            };
            binding.on_enter = accept;
            binding.on_over = accept;
            binding.on_drop = [this, target,life=std::weak_ptr<void>(scene_view_lifetime_)](lib::DragEvent& event) {
                if(life.expired())return;
                if(read_only_ || !summary_ || summary_->scenarioPath().empty()){
                    event.selected=lib::DropOperation::None;event.handled=true;return;
                }
                // ポインター・ドラッグイベントは、共通の当たり判定
                // と同じ表示ルート座標を使う。 ImagePane
                // はキャンバス内座標で、周囲の配置による移動とScrollView
                // のスクロールの両方を考慮する。
                const auto canvas_position = sceneCanvasPosition(event.position);
                const auto logical_canvas_position = lib::Point{
                    canvas_position.x / std::max(0.1F, scene_zoom_),
                    canvas_position.y / std::max(0.1F, scene_zoom_)};
                if (!event.data.files.empty()) {
                    const int x = target == DropTarg::ImagePane
                        ? static_cast<int>(logical_canvas_position.x) : 0;
                    const int y = target == DropTarg::ImagePane
                        ? static_cast<int>(logical_canvas_position.y) : 0;
                    event.handled = appendImportedImages(event.data.files, target, x, y,
                        event.target_index);
                    event.selected = event.handled ? lib::DropOperation::Copy
                                                   : lib::DropOperation::None;
                    return;
                }
                auto xml = xmlbytestransfer::CBtoXML(event.data);
                // 共通のC++場面の操作元が使う、場面のテキスト形式も維持す
                // る。 Dのカードの操作元には、上のXMLBytesを使う。
                if (xml.empty()) xml = event.data.text;
                if (xml.empty()) return;
                try {
                auto node=area::XNode::parse(xml);
                if(event.same_pane() && !node.has_attr("paneId")){
                    node.new_attr("paneId",sceneDragPaneId(target));
                    if(event.origin.cursor_index && *event.origin.cursor_index<=static_cast<std::size_t>(std::numeric_limits<int>::max()))node.new_attr("cursorIndex",*event.origin.cursor_index);
                }
                if(node.attr("paneId",false,std::string{})==sceneDragPaneId(target)){
                    const auto parsed=parseSceneTransfer(node);
                    const auto moved=moveSameScenePayload(node,target,event.target_index && *event.target_index<=static_cast<std::size_t>(std::numeric_limits<int>::max())?static_cast<int>(*event.target_index):-1,!parsed.cards.empty(),!parsed.backs.empty());
                    event.selected=moved.value_or(false)?lib::DropOperation::Move:lib::DropOperation::None;event.handled=true;return;
                }
                } catch (...) {
                    // 不正な転送のエラーは公開の取り込み経路
                    // に任せ、場面の状態通知先へ報告する。
                }
                std::vector<int> cards;
                std::vector<int> backgrounds;
                const int index = event.target_index
                                      ? static_cast<int>(*event.target_index)
                                      : -1;
                appendFromXML(xml,
                              target == DropTarg::ImagePane
                                  ? static_cast<int>(logical_canvas_position.x)
                                  : static_cast<int>(event.position.x),
                              target == DropTarg::ImagePane
                                  ? static_cast<int>(logical_canvas_position.y)
                                  : index,
                              target, cards, backgrounds);
                event.selected = (!cards.empty() || !backgrounds.empty())
                    ? lib::DropOperation::Link : lib::DropOperation::None;
                event.handled = !cards.empty() || !backgrounds.empty();
            };
            backend->bind_target(std::move(binding));
        };
        bind_source(cards_, DropTarg::Card);
        bind_source(backs_, DropTarg::Back);
        bind_source(canvas_, DropTarg::ImagePane);
        // Dは読み取り専用ビューをコピー元として認めるが、ドロップ先に
        // はしない。 上の操作元の登録は残し、ここで変更を制限する。
        if (read_only_) return;
        if constexpr (UseCards) bind_target(cards_, DropTarg::Card);
        if constexpr (UseBacks) bind_target(backs_, DropTarg::Back);
        bind_target(canvas_, DropTarg::ImagePane);
    }

    void construct(const std::shared_ptr<lib::Container>& parent) {
        const std::weak_ptr<void> life=scene_view_lifetime_;
        const bool all_mode = lib::editor_window_host_options().all_mode;
        // ALLはすべてのホストで小型のEditor配
        // 置を使う。Webとネイティブのサービスは異な
        // るが、 ツールバー・一覧・フラグペインの寸
        // 法は、バックエンドの違いだけでは変えない。
        const bool compact_all_mode = all_mode;
        const float horizontal_minimum = all_mode ? 100.0F : 320.0F;
        if constexpr (UseCards && UseBacks) {
            // DはArea/Battleを組み合わせた編集画
            // 面だけで、二つの共通表示設定を復元する。 専用
            // のカード・背景編集では、AreaViewの設
            // 定がfalseでも固有の既定表示を維持する。
            view_cards_ = prop_->var().viewCards.value;
            view_backs_ = prop_->var().viewBgImages.value;
        }
        show_grid_ = prop_->var().showGrid.value;
        if constexpr (!UseCards && UseBacks)
            show_ref_cards_ = prop_->var().viewReferenceCards.value;
        // Dの背景専用編集はEventの設定で初期化するが、切り替え結果はCells
        // へ保存する。 Area/Battleはそれぞれの既存設定を使う。
        fixed_cells_ = !UseCards && UseBacks ? prop_->var().fixedImagesEvent.value
                                            : prop_->var().fixedImagesCells.value;
        grid_x_ = std::max(1U, prop_->var().gridX.value);
        grid_y_ = std::max(1U, prop_->var().gridY.value);
        if constexpr (std::is_same_v<A, Area>) {
            view_party_ = prop_->var().viewPartyCardsArea.value;
            view_message_ = prop_->var().viewMessageArea.value;
            fixed_cards_ = prop_->var().fixedImagesMenuCards.value;
            fixed_background_ = prop_->var().fixedImagesBackground.value;
        } else if constexpr (std::is_same_v<A, Battle>) {
            view_party_ = prop_->var().viewPartyCardsBattle.value;
            view_message_ = prop_->var().viewMessageBattle.value;
            fixed_cards_ = prop_->var().fixedImagesBattle.value;
        } else {
            view_party_ = prop_->var().viewPartyCardsEvent.value;
            view_message_ = prop_->var().viewMessageEvent.value;
            fixed_cards_ = prop_->var().fixedImagesEvent.value;
            fixed_background_ = summary_ ? prop_->var().fixedImagesEventBackground.value
                                         : prop_->var().fixedImagesDefaultBackground.value;
        }
        if constexpr (std::is_same_v<C, EnemyCard> || (!UseCards && UseBacks))
            debug_mode_ = prop_->var().viewEnemyCardDebug.value;
        root_ = std::make_shared<lib::Panel>();
        // D/SWTの画面は、固定高さの配置ツー
        // ルバーと、残りを使う場面・一覧の分割領
        // 域の二段構成。 1列のGridでは余っ
        // た高さが上段へ配られ、大きな空白が生
        // じる場合がある。 LinearLayout
        // で、ツールバーは伸ばさず分割領域が
        // 残りをすべて使う構成を明示する。
        auto root_layout =
            std::make_shared<lib::LinearLayout>(lib::Orientation::Vertical);
        // ALLは高さに制約のあるドックへ埋め込むた
        // め、回転や幅の縮小後に空白帯が出ないよう
        // 、 ツールバーと分割領域の間を空けない。
        // 単独起動ではDの2pxの間隔を維持する。
        root_layout->spacing = all_mode ? 0.0F : 2.0F;
        root_layout->padding = {};
        root_->set_layout(root_layout);
        // TAreaWindowはドックページの唯一の子とし
        // てこのルートパネルを配置する。 SWTの元のAreaView
        // と同じく両方向へ広げる指定を明示し、ネイティブGrid
        // が Panelの1行分の推奨高さを使って場面
        // ・一覧・境界を上部へ潰してしまうのを防ぐ。
        lib::LayoutItem root_item;
        root_item.minimum = {horizontal_minimum, 220.0F};
        root_item.grow = 1.0F;
        root_item.horizontal = lib::Align::Stretch;
        root_item.vertical = lib::Align::Stretch;
        root_->set_layout_item(root_item);
        preview_ = std::make_unique<areaviewutils::Preview>(*prop_, root_, context_->services);
        root_->on_focus = [this,life] { if(life.expired())return; focused_ = true; };
        root_->on_blur = [this,life] { if(life.expired())return; focused_ = false; };

        // DのAreaViewはツールバーの下に左右分
        // 割を置き、左にカード・背景一覧、 右に632x420
        // の場面画像と任意のフラグ一覧を配置する。
        // キャンバスと三つの表を単純に縦へ追加
        // するだけでは、この配置を再現できない。
        toolbar_row_ = std::make_shared<lib::Panel>();
        toolbar_row_->accessibility_name = "SceneView toolbar row";
        auto toolbar_layout = std::make_shared<lib::LinearLayout>(lib::Orientation::Horizontal);
        toolbar_layout->spacing = 3.0F;
        toolbar_layout->cross_axis = lib::Align::Center;
        toolbar_row_->set_layout(toolbar_layout);
        // ALLのツールバー行の推奨高さは34px。XYWHの増減ボタン寸法はaddGeometryControl側の指定に従う。
        const float toolbar_row_height = compact_all_mode ? 34.0F : 30.0F;
        lib::LayoutItem toolbar_row_item;
        toolbar_row_item.row = 0U;
        toolbar_row_item.minimum = {0.0F, toolbar_row_height};
        toolbar_row_item.preferred = {0.0F, toolbar_row_height};
        toolbar_row_item.horizontal = lib::Align::Stretch;
        toolbar_row_item.vertical = lib::Align::Start;
        toolbar_row_->set_layout_item(toolbar_row_item);
        toolbar_row_->set_visible(prop_->var().showSceneToolBar.value);

        canvas_ = std::make_shared<lib::Canvas>();
        canvas_->accessibility_name = "シーンビュー";
        canvas_->double_buffered = true;
        const auto scene_size = prop_->looks().viewSize();
        // DのImagePaneはimageScaleを表示倍率に使う。drawingScale
        // は元画像の品質用に残し、 論理的な場面の拡大は独立して行
        // い、CardWirthPyの編集プレビューに合わせる。
        scene_zoom_ = prop_->imageScaleFactor();
        lib::LayoutItem canvas_item;
        // UIの文字倍率は共通レイアウトで適用する。 場面の
        // ピクセルには表示倍率だけを使い、両方を掛けない。
        const float authored_zoom = scene_zoom_ /
            std::max(0.1F, lib::editor_ui_text_scale());
        canvas_item.minimum = {static_cast<float>(scene_size.width) * authored_zoom,
                               static_cast<float>(scene_size.height) * authored_zoom};
        canvas_item.preferred = canvas_item.minimum;
        canvas_item.horizontal = lib::Align::Start;
        canvas_item.vertical = lib::Align::Start;
        canvas_->set_layout_item(canvas_item);

        cards_ = std::make_shared<lib::TableView>();
        if (lib::editor_window_host_options().all_mode)
            cards_->vertical_scrollbar_extra_width = 2.0F;
        // Dの場面一覧にはSWT.FULL_SELECTION
        // や見出しを付けない。 空の見出し領域を確保すると、
        // 各ツールバーと最初の行の間に余分な空白が生じる。
        cards_->header_visible = false;
        cards_->multi_select = true;
        cards_->checkable = true;
        cards_->check_selected_rows = true;
        cards_->check_selected_rows_copy_grayed = true;
        cards_->edit_on_double_click = false;
        // Dは値が変わらないF2/Enterの確定も、選択カードの一括
        // 更新へ通す。 複数行が選択されている場合も同じ動作を保つ。
        cards_->commit_unchanged_inline_edits = true;
        backs_ = std::make_shared<lib::TableView>();
        backs_->header_visible = false;
        backs_->multi_select = true;
        backs_->checkable = true;
        backs_->check_selected_rows = true;
        backs_->check_selected_rows_copy_grayed = true;
        backs_->edit_on_double_click = false;
        flags_ = std::make_shared<lib::TableView>();
        flags_->header_visible = false;
        flags_->multi_select = true;
        flags_->checkable = true;
        flags_->check_selected_rows = true;
        flags_->edit_on_double_click = false;
        toolbar_ = std::make_shared<lib::ToolBar>(); menu_ = std::make_shared<lib::Menu>(true);
        lib::LayoutItem toolbar_item;
        const float toolbar_item_height = compact_all_mode ? 24.0F : 26.0F;
        toolbar_item.minimum = {148.0F, toolbar_item_height};
        toolbar_item.preferred = {188.0F, toolbar_item_height};
        toolbar_item.vertical = lib::Align::Center;
        toolbar_->set_layout_item(toolbar_item);
        toolbar_row_->add(toolbar_);
        // ALLでも位置・大きさの操作欄を表示する。NumberEdit
        // の共通計測で横並びの増減ボタンの幅を確保し、
        // 固定幅のネイティブスピナーへ置き換えない。
        addGeometryControls();
        if (lib::editor_window_host_options().all_mode) {
            // ALLの操作部品は、すべてのホストで上端へ揃える。
            toolbar_layout->cross_axis = lib::Align::Start;
            toolbar_layout->padding = {};
            toolbar_layout->padding.bottom = 0.0F;
            for (const auto& control : toolbar_row_->children()) {
                auto item = control->layout_item();
                item.vertical = lib::Align::Start;
                control->set_layout_item(item);
            }
        }
        // ALLでは場面・イベント操作をホームの下へ配置
        // する。 子部品を移した後も固定高さを確保し続
        // けるため、元の横並び行を場面本体へ残さない。
        if (!all_mode) root_->add(toolbar_row_);

        main_split_ = std::make_shared<lib::SplitView>();
        main_split_->orientation = lib::Orientation::Horizontal;
        main_split_->constrain_to_child_minimums = !all_mode;
        lib::LayoutItem main_item;
        main_item.row = 1U;
        main_item.minimum = {horizontal_minimum, 220.0F};
        main_item.grow = 1.0F;
        main_split_->set_layout_item(main_item);

        auto left = std::make_shared<lib::Panel>();
        auto left_layout = std::make_shared<lib::GridLayout>();
        left_layout->columns = 1U;
        left_layout->spacing = all_mode ? 0.0F : 3.0F;
        left->set_layout(left_layout);
        if (all_mode)
            left->set_layout_item({.minimum = {160.0F, 0.0F}});
        list_split_ = std::make_shared<lib::SplitView>();
        list_split_->orientation = lib::Orientation::Vertical;
        lib::LayoutItem list_item;
        list_item.row = 0U;
        list_item.minimum = {160.0F, 160.0F};
        list_item.grow = 1.0F;
        list_split_->set_layout_item(list_item);

        card_list_toolbar_ = std::make_shared<lib::ToolBar>();
        back_list_toolbar_ = std::make_shared<lib::ToolBar>();
        if constexpr (UseCards) {
            list_split_->add(createListPane(
                std::is_same_v<C, EnemyCard> ? prop_->requiredMsgs().enemyCards.value
                                             : prop_->requiredMsgs().menuCards.value,
                "cards.png", cards_, card_list_toolbar_));
        }
        if constexpr (UseBacks) {
            list_split_->add(createListPane(prop_->requiredMsgs().backs.value, "backs.png",
                                            backs_, back_list_toolbar_));
        }
        left->add(list_split_);
        if (summary_ && !summary_->scenarioPath().empty())
            left->add(createFlagAssignPane());
        if constexpr (!UseCards && UseBacks) {
            if (summary_ && !summary_->scenarioPath().empty())
                left->add(createReferenceAreaPane());
        }
        if constexpr (std::is_same_v<A, Battle> && std::is_same_v<C, EnemyCard>) {
            if (!lib::editor_window_host_options().all_mode)
                left->add(createRunAwayPane());
            left->add(createBgmPane());
        }

        right_split_ = std::make_shared<lib::SplitView>();
        right_split_->orientation = lib::Orientation::Horizontal;
        auto image_pane = std::make_shared<lib::Panel>();
        auto image_layout = std::make_shared<lib::GridLayout>();
        image_layout->columns = 1U;
        image_layout->spacing = 0.0F;
        image_layout->padding = {};
        image_pane->set_layout(image_layout);
        scene_scroll_ = std::make_shared<lib::ScrollView>();
        // 周囲の側面欄を境界ドラッグで拡縮しても、ALLの場面表
        // 示領域でキャンバスを切り取り・収められるようにする。
        scene_scroll_->horizontal_scroll = !all_mode;
        scene_scroll_->vertical_scroll = true;
        lib::LayoutItem scene_scroll_item;
        scene_scroll_item.row = 0U;
        scene_scroll_item.column = 0U;
        scene_scroll_item.horizontal = lib::Align::Stretch;
        scene_scroll_item.vertical = lib::Align::Stretch;
        scene_scroll_item.minimum = {160.0F, 120.0F};
        scene_scroll_item.grow = 1.0F;
        scene_scroll_->set_layout_item(scene_scroll_item);
        scene_scroll_->add(canvas_);
        image_pane->add(scene_scroll_);
        // ALLはDと異なり、場面、幅を変えられる操作欄、フラグの順
        // にする。 背景セルのダイアログは既存の三分割の順序を保つ。
        const bool tools_on_right = all_mode &&
            (std::is_same_v<A, Area> || std::is_same_v<A, Battle>);
        right_split_->add(tools_on_right ? left : image_pane);
        right_split_->add(createFlagPane());
        main_split_->add(tools_on_right ? image_pane : left);
        main_split_->add(right_split_);
        configureSplitWeights();
        root_->add(main_split_);
        if (parent) parent->add(root_);

        cards_->on_selection_changed = [this,life](const auto& value) { if(life.expired())return;
            selected_cards_ = value;
            focused_ = true;
            // Dの一覧のSelectionListenerは、Table.setSelection
            // を呼ばずに場面の選択を更新する。 操作中の表の行位置を変えない。
            refreshSelected(true, true);
        };
        // Dはクリックした行のチェック・灰色状態を先に決め、両方を選択行
        // へコピーする。 一括変更の後、ビューの更新を一度だけ通知する。
        cards_->on_check_transition = [this,life](const std::size_t row, bool& checked, bool& grayed) {
            if(life.expired() || row >= cardCount())return;
            if (grayed) { grayed = false; checked = true; }
            else if (checked) grayed = prop_->var().switchFixedWithCheckBox.value;
            else grayed = false;
        };
        cards_->on_check_batch_changed = [this,life](const std::size_t,
                                                      const std::vector<std::size_t>& rows) {
            if(life.expired())return;
            ensureCardChecks();
            for (const auto row : rows) if (row < cardCount() && row < cards_->rows.size()) {
                card_checked_[row] = cards_->rows[row].checked;
                card_grayed_[row] = cards_->rows[row].grayed;
            }
            refreshCards();
            if (life.expired()) return;
            syncFlagChecks();
            if (life.expired()) return;
            applySceneVisibility();
        };
        backs_->on_check_transition = [this,life](const std::size_t row, bool& checked, bool& grayed) {
            if(life.expired() || row >= backCount())return;
            if (grayed) { grayed = false; checked = true; }
            else if (checked) grayed = prop_->var().switchFixedWithCheckBox.value;
            else grayed = false;
        };
        backs_->on_check_batch_changed = [this,life](const std::size_t,
                                                      const std::vector<std::size_t>& rows) {
            if(life.expired())return;
            ensureBackChecks();
            for (const auto row : rows) if (row < backCount() && row < backs_->rows.size()) {
                back_checked_[row] = backs_->rows[row].checked;
                back_grayed_[row] = backs_->rows[row].grayed;
            }
            refreshBacks();
            if (life.expired()) return;
            syncFlagChecks();
            if (life.expired()) return;
            applySceneVisibility();
        };
        backs_->on_selection_changed = [this,life](const auto& value) { if(life.expired())return;
            selected_backs_ = value;
            focused_ = true;
            refreshSelected(true, true);
        };
        // DのupdateCheckedはFlagのチェックを選択Flag行へコピーし
        // 、 その後checkFlagが実際にクリックした行だけの灰色状態を解除する。
        flags_->on_check_transition = [this,life](const std::size_t row,
                                                   bool&, bool& grayed) {
            if(life.expired() || row >= flags_->rows.size())return;
            grayed = false;
        };
        flags_->on_check_batch_changed = [this,life](const std::size_t,
                                                      const std::vector<std::size_t>&) {
            if(life.expired())return;
            applyFlagChecks(false);
        };
        flags_->on_pointer = [this,life](lib::PointerEvent& event) { if(life.expired())return;
            if (event.button != lib::PointerButton::Secondary ||
                event.action != lib::PointerAction::Up ||
                !context_->services.gui || !flag_menu_) return;
            if (event.item_hit_test_available && event.item_index >= 0 &&
                static_cast<std::size_t>(event.item_index) < flags_->rows.size())
                flags_->selection = {static_cast<std::size_t>(event.item_index)};
            if (flag_menu_->on_open) flag_menu_->on_open();
            context_->services.gui->show_context_menu(flag_menu_, flags_, event.position);
            event.handled = true;
        };
        if (!lib::editor_window_host_options().all_mode) {
            // Dの既定選択操作では編集を始めない。Enterは設定されたEditPropで扱う。
            cards_->activate_on_enter = false;
            backs_->activate_on_enter = false;
            // DのClosePreviewと同じく、縦横両方のスクロールバーを監視する。
            const auto close_preview = [this,life] {
                if (!life.expired() && preview_) preview_->close();
            };
            cards_->on_scroll_changed = close_preview;
            backs_->on_scroll_changed = close_preview;
        }
        cards_->on_cell_activate = [this,life](std::size_t row, std::size_t) { if(life.expired())return;
            if (row >= cardCount()) return;
            // DのMKListenerは、修飾キーで保持した複数行も含
            // め、共通入力層の選択を編集する。 型付きAPIは種類ごと
            // に対象を明示したホスト通知へ送り、 キャッシュ済みダイ
            // アログを使うときも他方の種類の選択を保てるようにする。
            std::vector<int> indices;
            indices.reserve(cards_->selection.size());
            for (const auto index : cards_->selection)
                if (index < cardCount()) indices.push_back(static_cast<int>(index));
            editCard(indices);
        };
        backs_->on_cell_activate = [this,life](std::size_t row, std::size_t) { if(life.expired())return;
            if (row >= backCount()) return;
            // Dの共通MKListenerは、選択された背景行をeditBack(indices)
            // へ渡す。 修飾キー付きの選択で編集画面が既にキャッシ
            // ュされている場合、他方の種類も選択したままにする。
            std::vector<int> indices;
            indices.reserve(backs_->selection.size());
            for (const auto index : backs_->selection)
                if (index < backCount()) indices.push_back(static_cast<int>(index));
            editBack(indices);
        };
        cards_->on_pointer = [this,life](lib::PointerEvent& event) { if(life.expired())return;
            if (event.action == lib::PointerAction::Down && event.button==lib::PointerButton::Primary &&
                (static_cast<std::uint32_t>(event.modifiers)&
                 (static_cast<std::uint32_t>(lib::Modifiers::Control)|static_cast<std::uint32_t>(lib::Modifiers::Shift)))==0U) {
                // Dは修飾キーなしのマウス押下時だけ、他方の種類の選択を
                // 解除する。 Ctrl/Shift・キーボード・プログラ
                // ム操作を含むTableの選択通知では、混在選択を保つ。
                selected_backs_.clear();refreshSelected(true, true);
            } else if (event.action == lib::PointerAction::Leave ||
                event.action == lib::PointerAction::Cancel) {
                if (preview_) preview_->close();
                if (!life.expired() && prop_->var().showCardStatusUnderPointer.value) refreshStatusLine();
            } else if (event.action == lib::PointerAction::Enter) {
                if (prop_->var().showCardStatusUnderPointer.value) refreshStatusLine();
            } else if (event.button == lib::PointerButton::Primary &&
                       event.action == lib::PointerAction::Up &&
                       event.item_hit_test_available && event.item_index < 0) {
                // SWTでは一覧の空白をクリックすると、SelectionEvent
                // なしで選択が消える場合がある （SCListener.mouseUp
                // ）。Dと同じくCtrlを保ちつつ、実際の表の選択を同期し直す。
                if (lib::editor_window_host_options().all_mode) selected_cards_.clear();
                else selected_cards_ = detail::normalizedSelection<C>(cards_->selection, cardCount());
                refreshSelected(true, true);
            } else if (event.button == lib::PointerButton::Secondary &&
                       event.action == lib::PointerAction::Up) {
                // SWTの表のポップアップは一覧自身に接
                // 続されている。 まず指された行を選んで
                // 見える対象へ操作を適用するが、既存の
                // 複数選択にその行があれば選択を保つ。
                if (event.item_hit_test_available && event.item_index >= 0 &&
                    static_cast<std::size_t>(event.item_index) < cardCount() &&
                    std::find(selected_cards_.begin(), selected_cards_.end(),
                              static_cast<std::size_t>(event.item_index)) == selected_cards_.end()) {
                    selected_cards_ = {static_cast<std::size_t>(event.item_index)};
                    selected_backs_.clear();
                    refreshSelected(true, true);
                }
                showListMenu(cards_, event.position, true);
                event.handled = true;
            } else if (event.action == lib::PointerAction::Move) {
                previewTrigger(*cards_, static_cast<int>(event.position.x),
                               static_cast<int>(event.position.y),
                               event.screen_position, event.item_index,
                               event.item_hit_test_available);
                if (!life.expired()) refreshHoverStatus(*cards_, event);
            }
        };
        backs_->on_pointer = [this,life](lib::PointerEvent& event) { if(life.expired())return;
            if (event.action == lib::PointerAction::Down && event.button==lib::PointerButton::Primary &&
                (static_cast<std::uint32_t>(event.modifiers)&
                 (static_cast<std::uint32_t>(lib::Modifiers::Control)|static_cast<std::uint32_t>(lib::Modifiers::Shift)))==0U) {
                selected_cards_.clear();refreshSelected(true, true);
            } else if (event.action == lib::PointerAction::Leave ||
                event.action == lib::PointerAction::Cancel) {
                if (preview_) preview_->close();
                if (!life.expired() && prop_->var().showCardStatusUnderPointer.value) refreshStatusLine();
            } else if (event.action == lib::PointerAction::Enter) {
                if (prop_->var().showCardStatusUnderPointer.value) refreshStatusLine();
            } else if (event.button == lib::PointerButton::Primary &&
                       event.action == lib::PointerAction::Up &&
                       event.item_hit_test_available && event.item_index < 0) {
                // DのSBListener.mouseUpと同じく、部品
                // の再描画前に 空白での選択解除通知の取りこぼしを補う。
                if (lib::editor_window_host_options().all_mode) selected_backs_.clear();
                else selected_backs_ = detail::normalizedSelection<BgImage>(backs_->selection, backCount());
                refreshSelected(true, true);
            } else if (event.button == lib::PointerButton::Secondary &&
                       event.action == lib::PointerAction::Up) {
                if (event.item_hit_test_available && event.item_index >= 0 &&
                    static_cast<std::size_t>(event.item_index) < backCount() &&
                    std::find(selected_backs_.begin(), selected_backs_.end(),
                              static_cast<std::size_t>(event.item_index)) == selected_backs_.end()) {
                    selected_backs_ = {static_cast<std::size_t>(event.item_index)};
                    selected_cards_.clear();
                    refreshSelected(true, true);
                }
                showListMenu(backs_, event.position, false);
                event.handled = true;
            } else if (event.action == lib::PointerAction::Move) {
                previewTrigger(*backs_, static_cast<int>(event.position.x),
                               static_cast<int>(event.position.y),
                               event.screen_position, event.item_index,
                               event.item_hit_test_available);
                if (!life.expired()) refreshHoverStatus(*backs_, event);
            }
        };
        cards_->on_cell_edited = [this,life](std::size_t row, std::size_t column, std::string value) { if(life.expired())return; editCardCell(row, column, std::move(value)); };
        backs_->on_cell_edited = [this,life](std::size_t row, std::size_t column, std::string value) { if(life.expired())return; editBackCell(row, column, std::move(value)); };
        backs_->on_cell_edit_begin = [this,life](const std::size_t row,
                                                  const std::size_t column) {
            if (life.expired()) return false;
            const auto* const source_area = area_;
            const auto table = backs_;
            if (read_only_ || !source_area || !table || row >= backCount() ||
                row >= table->rows.size() || column >= table->rows[row].cells.size())
                return false;
            if (column != 0U ||
                table->rows[row].cells[column].editor != lib::CellEditorKind::Choice)
                return true;
            const auto source_cell = backPtr(row);
            if (!source_cell) return false;
            const auto cell_snapshot = source_cell->dup();
            if (!cell_snapshot) return false;
            const auto row_order_revision = table->row_order_revision;
            const auto row_update_revision = table->row_update_revision;
            std::string selected;
            auto choices = backgroundEditorChoices(*cell_snapshot, &selected);
            if (life.expired() || area_ != source_area || backs_ != table ||
                table->row_order_revision != row_order_revision ||
                table->row_update_revision != row_update_revision ||
                row >= backCount() || row >= table->rows.size() ||
                column >= table->rows[row].cells.size() || backPtr(row) != source_cell)
                return false;
            auto& item = table->rows[row].cells[column];
            if (!item.enabled || !item.inline_editable ||
                item.editor != lib::CellEditorKind::Choice) return false;
            item.editor_choices = std::move(choices);
            item.editor_value = std::move(selected);
            return true;
        };
        cards_->on_inline_editor_created=[this,life](std::size_t row,std::size_t column,
            const std::shared_ptr<lib::TextEdit>& edit,const std::shared_ptr<lib::ComboBox>& combo) {
            if(!life.expired())attachInlineSearch(cards_,row,column,edit,combo,true);
        };
        backs_->on_inline_editor_created=[this,life](std::size_t row,std::size_t column,
            const std::shared_ptr<lib::TextEdit>& edit,const std::shared_ptr<lib::ComboBox>& combo) {
            if(!life.expired())attachInlineSearch(backs_,row,column,edit,combo,false);
        };
        canvas_->on_paint_context = [this,life](lib::DrawingContext& drawing, lib::Rect dirty) { if(life.expired())return;
            paintScene(drawing, dirty);
        };
        canvas_->on_pointer = [this,life=std::weak_ptr<void>(scene_view_lifetime_)](lib::PointerEvent& event) { if(!life.expired())handleScenePointer(event); };
        canvas_->on_key_down = [this,life=std::weak_ptr<void>(scene_view_lifetime_)](lib::KeyEvent& event) { if(!life.expired())handleSceneKey(event); };
        canvas_->on_blur = [this,life=std::weak_ptr<void>(scene_view_lifetime_)] {
            if (life.expired()) return;
            // DのImagePane.FocusLostは、resize()を通じて保留中のFlexImageの配置を確定する。
            finishSceneDrag(true);
            if (life.expired()) return;
            finishSceneKeyboardEdit(true);
        };
        setupMenu(menu_);
        setupToolBar0(toolbar_);
        // DのToolItemはrefMenuを購読する。部品を再構成した
        // り操作状態を変えたりせず、 現在の場面ツールチップを更新する。
        if (!lib::editor_window_host_options().all_mode && context_->subscribe_menu_updates) {
            const auto subscribe = context_->subscribe_menu_updates;
            auto disconnect = subscribe([this, life](const types::MenuID id) {
                if (!life.expired()) refreshConfiguredTooltips(id);
            });
            if (life.expired()) {
                if (disconnect) disconnect();
                return;
            }
            if (disconnect) scene_retirement_callbacks_.push_back(std::move(disconnect));
        }
        // 通常の編集可能なシナリオビューは、現在のシナリオスキンに追従する。
        // 強制指定・読み取り専用のプレビューは、明示的に取得したスキンを保つ。
        if (!lib::editor_window_host_options().all_mode && !force_skin_ &&
            !read_only_ && summary_ && !summary_->scenarioPath().empty() &&
            context_->subscribe_skin_updates) {
            const auto subscribe = context_->subscribe_skin_updates;
            auto disconnect = subscribe([this, life](Skin::Ptr skin) {
                if (life.expired() || !skin || !context_) return;
                // ホストの借用スキンポインターを含め、キャッシュした表示オ
                // ブジェクトを 再構成し終えるまで以前の所有者を保持する。
                const auto previous_view_skin = context_->skin_owner;
                const auto previous_host_skin = context_->area_window
                    ? context_->area_window->skin_owner : Skin::Ptr{};
                context_->skin_owner = skin;
                if (context_->area_window) {
                    context_->area_window->skin_owner = skin;
                    context_->area_window->skin = skin.get();
                }
                refreshPresentation();
            });
            if (life.expired()) {
                if (disconnect) disconnect();
                return;
            }
            if (disconnect) scene_retirement_callbacks_.push_back(std::move(disconnect));
        }
        // DはAreaViewルート内のFocusIn部品を記録
        // し、別ウィンドウへフォーカスが移っても記録を変えない。
        if (context_ && context_->services.gui && root_) {
            const auto focus_scope = root_;
            const auto scene_life = std::weak_ptr<void>(scene_view_lifetime_);
            const auto observer = lib::add_focus_observer(
                [this, scene_life, focus_scope](const lib::WidgetPtr& target) {
                    if (scene_life.expired() || !target || !focus_scope ||
                        lib::focus_target_in_scope(focus_scope, target, false) != target)
                        return;
                    last_focus_on_list_or_pane_ = target == cards_ || target == backs_ ||
                                                 target == canvas_;
                });
            scene_retirement_callbacks_.push_back([observer] {
                lib::remove_focus_observer(observer);
            });
        }
        setupDragDrop();
        refreshRefAreas();
        refresh();
        // Dのviews()は、このArea/Battleの有効な全
        // SceneViewを含む。 登録直後に他のビューから更新
        // され得るため、全部品の作成後に登録する。 通常のAreaViewContext
        // のコンストラクターでも省略しない。
        registerSceneView();
        registerCastNotifications();
        // MainWindowはこのビューの構築前に場面ごとの登録先を渡す。
        // 共通ホストとの接続時に、DのsetupTLP相当の操作を登録する。
        registerTopLevelMenuActions();
    }

    [[nodiscard]] std::shared_ptr<lib::Panel> createFlagAssignPane() {
        auto pane = std::make_shared<lib::Panel>();
        auto layout = std::make_shared<lib::GridLayout>();
        layout->columns = 2U;
        layout->spacing = 3.0F;
        layout->padding = {};
        pane->set_layout(layout);
        pane->set_layout_item({.row = 1U, .column = 0U,
                               .horizontal = lib::Align::Stretch});
        auto label = std::make_shared<lib::Label>(prop_->requiredMsgs().areaViewFlagDesc.value);
        label->set_layout_item({.row = 0U, .column = 0U,
                                .vertical = lib::Align::Center});
        pane->add(label);
        flag_assign_combo_ = std::make_shared<lib::ComboBox>();
        flag_assign_combo_->editable = false;
        // ALL左下のフラグ選択は、長い一覧のポップアップにする。
        // Dと同じく、20項目まではスクロールなしで表示する。
        if (compactAllMode()) flag_assign_combo_->visible_item_count = 20U;
        // ALLの背景セル編集では一覧列の初期幅を
        // 250pxとし、長いフラグ名で広げない。
        if constexpr (!UseCards)
            flag_assign_combo_->minimum_ignores_items =
                lib::editor_window_host_options().all_mode;
        flag_assign_combo_->set_layout_item({.row = 0U, .column = 1U,
                                             .horizontal = lib::Align::Stretch,
                                             .grow = 1.0F});
        flag_assign_combo_->on_selection_changed = [this,life=std::weak_ptr<void>(scene_view_lifetime_)](const std::optional<std::size_t> value) {
            if (life.expired() || read_only_) return;
            const std::string flag = value && *value > 0U && *value - 1U < flag_assign_paths_.size()
                ? flag_assign_paths_[*value - 1U] : std::string{};
            applySelectedFlag(flag);
        };
        incsearch::IncSearchSettings search_settings;
        search_settings.startIncrementalSearchWhenKeyDown = prop_->var().startIncrementalSearchWhenKeyDown.value;
        search_settings.startIncrementalSearchWhenKeyDownProvider =
            [this, life=std::weak_ptr<void>(scene_view_lifetime_)] {
                return !life.expired() && prop_ &&
                    prop_->var().startIncrementalSearchWhenKeyDown.value;
            };
        search_settings.incrementalSearchBoxWidth = prop_->var().incrementalSearchBoxWidth.value;
        search_settings.incrementalSearchType = prop_->var().incrementalSearchType.value;
        search_settings.containsLabel = prop_->requiredMsgs().incSearchContains.value;
        search_settings.wildcardLabel = prop_->requiredMsgs().incSearchWildcard.value;
        search_settings.regexLabel = prop_->requiredMsgs().incSearchRegex.value;
        search_settings.windowTitle = prop_->requiredMsgs().menuText(types::MenuID::IncSearch);
        flag_assign_inc_search_ = std::make_unique<incsearch::IncSearch>(
            context_->services, flag_assign_combo_.get(), [this] { return flag_assign_has_paths_; },
            std::vector<incsearch::AdditionMatcher>{}, std::move(search_settings));
        flag_assign_inc_search_->modEvent.push_back([this, life=std::weak_ptr<void>(scene_view_lifetime_)] {
            if (life.expired()) return;
            prop_->var().incrementalSearchType.value = static_cast<std::int32_t>(flag_assign_inc_search_->searchType());
            refreshFlagAssignCombo();
        });
        const auto life = std::weak_ptr<void>(scene_view_lifetime_);
        flag_assign_combo_->on_text_input = [this, life](lib::TextInputEvent& event) {
            if (!life.expired() && flag_assign_inc_search_->handleHostTextInput(event, flag_assign_combo_.get()))
                event.handled = true;
        };
        flag_assign_combo_->on_key_down = [this, life](lib::KeyEvent& event) {
            if (!life.expired() && flag_assign_inc_search_->handleHostKeyDown(event, flag_assign_combo_.get()))
                event.handled = true;
        };
        auto search_menu = std::make_shared<lib::Menu>(true);
        search_menu->items.push_back({prop_->buildMenu(types::MenuID::IncSearch), "incremental-search"});
        // DのopenFlagViewCは、参照フラグ表とは別に
        // 割り当ての選択肢を解決し、 浅いパスの属性は付けない。
        const auto selected_flag = [this, life]() -> ::cwxeditor::cwx::flag::FlagPtr {
            if (life.expired() || read_only_ || !flag_assign_combo_ ||
                !flag_assign_combo_->selected_index) return {};
            const auto row = *flag_assign_combo_->selected_index;
            if (row >= flag_assign_combo_->items.size() ||
                flag_assign_combo_->items[row].value.empty()) return {};
            const auto root = summary_ ? summary_->flagDirRoot() : nullptr;
            const auto counter = [&]() -> ::cwxeditor::cwx::flag::UseCounterPtr {
                if constexpr (std::is_same_v<A, BgImageContainer>) return {};
                else return area_->useCounter();
            }();
            return ::cwxeditor::cwx::flag::findVar<::cwxeditor::cwx::flag::Flag>(
                root, counter, flag_assign_combo_->items[row].value);
        };
        search_menu->on_open = [this, life, selected_flag, weak_menu = std::weak_ptr<lib::Menu>(search_menu)] {
            if (life.expired()) return;
            const auto menu = weak_menu.lock();
            if (!menu) return;
            menu->items = {
                {prop_->buildMenu(types::MenuID::IncSearch), "incremental-search", {}, 0, flag_assign_has_paths_},
                {"", "", {}, 0, true, false, false, true},
                {prop_->buildMenu(types::MenuID::OpenAtVarView), "open-variable", {}, 0,
                 static_cast<bool>(selected_flag())}};
        };
        search_menu->on_command = [this, life, selected_flag](std::string_view command) {
            if (life.expired()) return;
            if (command == "incremental-search" && flag_assign_has_paths_)
                flag_assign_inc_search_->startIncSearch();
            else if (command == "open-variable" && context_->open_cwx_path) {
                const auto flag = selected_flag();
                if (flag) (void)context_->open_cwx_path(flag->cwxPath(true), false);
            }
        };
        flag_assign_combo_->on_pointer = [this, life, search_menu](lib::PointerEvent& event) {
            if (life.expired() || !context_->services.gui ||
                event.button != lib::PointerButton::Secondary || event.action != lib::PointerAction::Up) return;
            if (search_menu->on_open) search_menu->on_open();
            context_->services.gui->show_context_menu(search_menu, flag_assign_combo_, event.position);
            event.handled = true;
        };
        pane->add(flag_assign_combo_);
        refreshFlagAssignCombo();
        return pane;
    }

    void refreshFlagAssignCombo() {
        if (!flag_assign_combo_) return;
        std::vector<std::string> paths;
        std::vector<lib::ChoiceItem> items;
        items.push_back({prop_->requiredMsgs().defaultSelection(prop_->requiredMsgs().noFlagRef.value), {}});
        const auto source_summary = summary_;
        const auto root = source_summary ? source_summary->flagDirRoot() : nullptr;
        const auto append = [&paths](auto&& self,
                                            const ::cwxeditor::cwx::flag::FlagDirPtr& directory,
                                            std::string prefix) -> void {
            if (!directory) return;
            if (!directory->name().empty()) {
                if (!prefix.empty()) prefix += "\\";
                prefix += directory->name();
            }
            for (const auto& flag : directory->flags()) {
                if (!flag) continue;
                const auto path = prefix.empty() ? flag->name() : prefix + "\\" + flag->name();
                paths.push_back(path);
            }
            for (const auto& child : directory->subDirs()) self(self, child, prefix);
        };
        if (root) append(append, root, {});
        flag_assign_has_paths_ = !paths.empty();
        std::stable_sort(paths.begin(), paths.end(), [this](const auto& left, const auto& right) {
            return ::cwxeditor::cwx::flag::pathCompare(left, right, prop_->var().logicalSort.value) < 0;
        });
        if (flag_assign_inc_search_) std::erase_if(paths, [this](const auto& path) {
            return !flag_assign_inc_search_->match(path);
        });
        for (const auto& path : paths) items.push_back({path, path});
        // 選択に追従する表示更新では、モデルから部品への反映だけを行いSelFlag
        // を呼ばない。 空の接頭部分と共通フラグの集約は、Dと同じ規則にする。
        std::string common;
        bool different = false;
        bool selected = false;
        const auto collect = [&](const std::string& flag) {
            selected = true;
            if (different) return;
            if (common.empty()) common = flag;
            else if (common != flag) { common.clear(); different = true; }
        };
        if constexpr (UseCards)
            for (const auto index : selected_cards_)
                if (index < cardCount()) collect(areaCard(index).flag());
        if constexpr (UseBacks)
            for (const auto index : selected_backs_)
                if (index < backCount()) collect(back(index).flag());
        std::size_t selection = 0U;
        if (!common.empty()) {
            const auto found = std::find(paths.begin(), paths.end(), common);
            if (found == paths.end()) {
                // DのREAD_ONLYのCombo.setTextは、除外・欠落
                // したパスを追加しない。 モデルを変えず、フラグなしの表示を保つ。
                selection = 0U;
            } else selection = static_cast<std::size_t>(found - paths.begin()) + 1U;
        }
        flag_assign_combo_->set_enabled(!read_only_ && selected);
        const auto revision = ++flag_assign_combo_revision_;
        auto publication = lib::prepare_combo_items(
            flag_assign_combo_, std::move(items), selection, {},
            [this, source_summary, revision] {
                return summary_ == source_summary &&
                    flag_assign_combo_revision_ == revision;
            });
        if (publication.commit()) flag_assign_paths_.swap(paths);
    }

    void applySelectedFlag(const std::string& flag) {
        if (read_only_) return;
        std::vector<std::size_t> changed_cards,changed_backs;
        if constexpr (UseCards) {
            for (const auto index : selected_cards_)
                if (index < cardCount() && areaCard(index).flag() != flag)changed_cards.push_back(index);
        }
        if constexpr (UseBacks) {
            for (const auto index : selected_backs_)
                if (index < backCount() && back(index).flag() != flag)changed_backs.push_back(index);
        }
        // DのSelFlagは同じ値でも選んだ操作を記録する。パス通
        // 知はSummaryの有無によらず行うが、 変更されたセル
        // だけを対象にする。参照数の通知には従来の条件を適用する。
        if(!mutateWithReferenceNotifications(changed_cards,changed_backs,[&] {
            if constexpr (UseCards) for (const auto index : changed_cards)
                if (index < cardCount()) areaCard(index).flag(flag);
            if constexpr (UseBacks) for (const auto index : changed_backs)
                if (index < backCount()) back(index).flag(flag);
        },true,false))return;
        refreshAfterModelChange();
    }

    [[nodiscard]] bool compactAllMode() const noexcept {
        return lib::editor_window_host_options().all_mode;
    }

    [[nodiscard]] std::shared_ptr<lib::Panel> createReferenceAreaPane() {
        auto pane = std::make_shared<lib::Panel>();
        auto layout = std::make_shared<lib::GridLayout>();
        layout->columns = 2U;
        layout->spacing = 3.0F;
        layout->padding = {};
        pane->set_layout(layout);
        pane->set_layout_item({.row = 2U, .column = 0U,
                               .horizontal = lib::Align::Stretch});
        auto label = std::make_shared<lib::Label>(prop_->requiredMsgs().areaViewRefAreaDesc.value);
        label->set_layout_item({.row = 0U, .column = 0U,
                                .vertical = lib::Align::Center});
        pane->add(label);
        ref_area_combo_ = std::make_shared<lib::ComboBox>();
        ref_area_combo_->editable = false;
        if constexpr (!UseCards)
            ref_area_combo_->minimum_ignores_items =
                lib::editor_window_host_options().all_mode;
        ref_area_combo_->set_layout_item({.row = 0U, .column = 1U,
                                          .horizontal = lib::Align::Stretch,
                                          .grow = 1.0F});
        incsearch::IncSearchSettings search_settings;
        search_settings.startIncrementalSearchWhenKeyDown =
            prop_->var().startIncrementalSearchWhenKeyDown.value;
        search_settings.startIncrementalSearchWhenKeyDownProvider =
            [this, life=std::weak_ptr<void>(scene_view_lifetime_)] {
                return !life.expired() && prop_ &&
                    prop_->var().startIncrementalSearchWhenKeyDown.value;
            };
        search_settings.incrementalSearchBoxWidth =
            prop_->var().incrementalSearchBoxWidth.value;
        search_settings.incrementalSearchType = prop_->var().incrementalSearchType.value;
        search_settings.containsLabel = prop_->requiredMsgs().incSearchContains.value;
        search_settings.wildcardLabel = prop_->requiredMsgs().incSearchWildcard.value;
        search_settings.regexLabel = prop_->requiredMsgs().incSearchRegex.value;
        search_settings.windowTitle = prop_->requiredMsgs().menuText(types::MenuID::IncSearch);
        search_settings.closeLabel = prop_->requiredMsgs().menuText(types::MenuID::CloseIncSearch);
        ref_area_inc_search_ = std::make_unique<incsearch::IncSearch>(
            context_->services, ref_area_combo_.get(),
            [this,life=std::weak_ptr<void>(scene_view_lifetime_)] { return !life.expired() && !ref_areas_.empty(); },
            std::vector<incsearch::AdditionMatcher>{}, std::move(search_settings));
        ref_area_inc_search_->modEvent.push_back([this,life=std::weak_ptr<void>(scene_view_lifetime_)] { if(!life.expired())refreshReferenceAreaCombo(); });
        ref_area_combo_->on_selection_changed = [this,life=std::weak_ptr<void>(scene_view_lifetime_)](const std::optional<std::size_t> value) {
            if(life.expired())return;
            if (!value || *value == 0U) {
                ref_card_selected_ = false;
                ref_card_index_ = 0U;
            } else if (*value - 1U < ref_area_combo_indices_.size()) {
                ref_card_index_ = ref_area_combo_indices_[*value - 1U];
                ref_card_selected_ = true;
            } else return;
            // Dでは表示だけの状態。重ね表示の元を選ん
            // だら描き直すが、Undo操作は作らない。
            refresh();
        };
        ref_area_menu_ = std::make_shared<lib::Menu>(true);
        ref_area_menu_->on_open = [this,life=std::weak_ptr<void>(scene_view_lifetime_)] {
            if(life.expired())return;
            ref_area_menu_->items = {
                {prop_->requiredMsgs().menuText(types::MenuID::IncSearch), "incremental-search", {}, 0, true},
                {"", "", {}, 0, true, false, false, true},
                {prop_->requiredMsgs().menuText(types::MenuID::OpenAtTableView), "open-reference", {}, 0,
                 ref_card_selected_ && !read_only_}};
        };
        ref_area_menu_->on_command = [this,life=std::weak_ptr<void>(scene_view_lifetime_)](const std::string_view command) {
            if(life.expired())return;
            if (command == "incremental-search" && ref_area_inc_search_)
                ref_area_inc_search_->startIncSearch();
            else if (command == "open-reference") openRefAreaView();
        };
        ref_area_combo_->on_pointer = [this,life=std::weak_ptr<void>(scene_view_lifetime_)](lib::PointerEvent& event) {
            if(life.expired())return;
            if (event.button != lib::PointerButton::Secondary ||
                event.action != lib::PointerAction::Up || !context_->services.gui || !ref_area_menu_)
                return;
            const auto menu=ref_area_menu_;
            const auto combo=ref_area_combo_;
            const auto gui=context_->services.gui;
            if (menu->on_open) menu->on_open();
            if(life.expired())return;
            gui->show_context_menu(menu, combo, event.position);
            event.handled = true;
        };
        pane->add(ref_area_combo_);
        refreshReferenceAreaCombo();
        return pane;
    }

    void refreshReferenceAreaCombo(const bool select_no_reference = false) {
        if constexpr (UseCards || !UseBacks) return;
        if (!ref_area_combo_) return;
        std::vector<lib::ChoiceItem> items;
        items.reserve(ref_areas_.size() + 1U);
        items.push_back({prop_->requiredMsgs().defaultSelection(prop_->requiredMsgs().noRefArea.value), {}});
        const auto selected_target = ref_card_selected_ && ref_card_index_ < ref_areas_.size()
            ? ref_areas_[ref_card_index_] : nullptr;
        std::vector<std::size_t> indices;
        // Dと同じく、元の対象がなければ参照用Comboを未選択にする。
        std::optional<std::size_t> selected = select_no_reference
            ? std::optional<std::size_t>{0U} : std::nullopt;
        for (std::size_t index = 0U; index < ref_areas_.size(); ++index) {
            const auto* value = ref_areas_[index];
            if (!value) continue;
            if (ref_area_inc_search_ && !ref_area_inc_search_->match(value->name())) continue;
            items.push_back({std::to_string(value->id()) + "." + value->name(),
                             std::to_string(value->id())});
            indices.push_back(index);
            if (ref_card_selected_ && index == ref_card_index_)
                selected = items.size() - 1U;
        }
        const auto revision = ++ref_area_combo_revision_;
        auto publication = lib::prepare_combo_items(
            ref_area_combo_, std::move(items), selected, {},
            [this, revision, selected_target] {
                const auto current_target = ref_card_selected_ && ref_card_index_ < ref_areas_.size()
                    ? ref_areas_[ref_card_index_] : nullptr;
                return ref_area_combo_revision_ == revision &&
                    current_target == selected_target;
            });
        if (publication.commit()) ref_area_combo_indices_.swap(indices);
    }

    [[nodiscard]] std::shared_ptr<lib::ToggleButton> createRunAwayControl() {
        if constexpr (!(std::is_same_v<A, Battle> && std::is_same_v<C, EnemyCard>)) {
            return {};
        } else {
            if (lib::editor_window_host_options().all_mode) {
                // ALLでは同じ設定をチェック欄でなく、小型の「逃」切り替え
                // ボタンで操作する。 完全な説明文はツールチップへ表示する。
                possible_to_run_away_control_ = std::make_shared<lib::ToggleButton>("逃");
                possible_to_run_away_control_->set_tooltip(
                    prop_->requiredMsgs().menuText(types::MenuID::PossibleToRunAway));
            } else {
                possible_to_run_away_control_ = std::make_shared<lib::CheckBox>(
                    prop_->requiredMsgs().menuText(types::MenuID::PossibleToRunAway));
            }
            possible_to_run_away_control_->checked = area_->possibleToRunAway();
            possible_to_run_away_control_->set_enabled(canReversePossibleToRunAway());
            possible_to_run_away_control_->on_changed = [this](const bool checked) {
                if (!canReversePossibleToRunAway() || area_->possibleToRunAway() == checked) return;
                // Dのチェック欄の通知と同じく、この値だけをUndoの対
                // 象にする。 後から行った選択の変更はUndoでも残す。
                if(!mutateSettingWithReferenceNotifications(selected_cards_, {}, [&] {
                    area_->possibleToRunAway(checked);
                }))return;
                refreshAfterModelChange();
            };
            return possible_to_run_away_control_;
        }
    }

    [[nodiscard]] std::shared_ptr<lib::Panel> createRunAwayPane() {
        if constexpr (!(std::is_same_v<A, Battle> && std::is_same_v<C, EnemyCard>)) {
            return {};
        } else {
        auto pane = std::make_shared<lib::Panel>();
        auto layout = std::make_shared<lib::GridLayout>();
        layout->columns = 1U;
        layout->spacing = 3.0F;
        layout->padding = {};
        pane->set_layout(layout);
        pane->set_layout_item({.row = 2U, .column = 0U,
                               .horizontal = lib::Align::Stretch});
        auto before = std::make_shared<lib::Separator>();
        before->set_layout_item({.row = 0U, .column = 0U,
                                 .horizontal = lib::Align::Stretch});
        pane->add(before);
        const auto control = createRunAwayControl();
        control->set_layout_item(
            {.row = 1U, .column = 0U, .horizontal = lib::Align::Center});
        pane->add(control);
        auto after = std::make_shared<lib::Separator>();
        after->set_layout_item({.row = 2U, .column = 0U,
                                .horizontal = lib::Align::Stretch});
        pane->add(after);
        return pane;
        }
    }

    [[nodiscard]] std::shared_ptr<lib::Panel> createBgmPane() {
        if constexpr (!(std::is_same_v<A, Battle> && std::is_same_v<C, EnemyCard>)) {
            return {};
        } else {
        const bool all_mode = lib::editor_window_host_options().all_mode;
        auto pane = std::make_shared<lib::Panel>();
        auto layout = std::make_shared<lib::GridLayout>();
        layout->columns = all_mode ? 1U : 2U;
        layout->spacing = 3.0F;
        layout->padding = {};
        pane->set_layout(layout);
        // ALLでは独立した逃走設定の行を省き、BGMペインを上へ移す。
        pane->set_layout_item({.row = all_mode ? 2U : 3U, .column = 0U,
                               .horizontal = lib::Align::Stretch});

        const auto definitions = [this](const bool) {
            return std::vector<std::string>{
                prop_->requiredMsgs().defaultSelection(prop_->requiredMsgs().bgmNone.value)};
        };
        Skin::Ptr bgm_force_skin;
        if (force_skin_) {
            if (context_->skin_owner.get() == force_skin_)
                bgm_force_skin = context_->skin_owner;
            else
                bgm_force_skin = Skin::Ptr(
                    const_cast<Skin*>(force_skin_), [](Skin*) {});
        }
        bgm_selector_ = std::make_unique<materialselect::MaterialSelect<
            materialselect::MtType::BGM>>(
            context_->services, *prop_, summary_, std::move(bgm_force_skin), read_only_,
            lib::Action{}, definitions);
        // Dの選択部品は構築時にrefSkinを購読する
        // 。AreaViewのホスト接続でも購読用の通知
        // を渡す。 通常のシナリオ選択部品が更新可能なま
        // まであるよう、明示的な強制スキンだけを渡す。
        if (!force_skin_ && !read_only_ && summary_ &&
            !summary_->scenarioPath().empty() &&
            context_->subscribe_skin_updates) {
            const auto subscribe = context_->subscribe_skin_updates;
            const std::weak_ptr<void> life = scene_view_lifetime_;
            auto disconnect = subscribe([this, life](Skin::Ptr) {
                if (life.expired() || !bgm_selector_) return;
                bgm_selector_->refSkin();
            });
            if (life.expired()) {
                if (disconnect) disconnect();
                return {};
            }
            if (disconnect)
                scene_retirement_callbacks_.push_back(std::move(disconnect));
        }
        bgm_selector_->canContinue(!summary_ || !summary_->legacy());

        auto title = std::make_shared<lib::Panel>();
        auto title_layout = std::make_shared<lib::LinearLayout>(lib::Orientation::Horizontal);
        title_layout->spacing = 3.0F;
        title_layout->cross_axis = lib::Align::Center;
        title->set_layout(title_layout);
        title->set_layout_item({.row = 0U, .column = 0U,
                                .horizontal = lib::Align::Stretch});
        if (!all_mode) {
            auto icon = std::make_shared<lib::ImageView>();
            icon->image = prop_->images().load(dprops::StringView{"bgm.png"});
            icon->scale_mode = lib::ImageScaleMode::None;
            icon->set_layout_item({.minimum = {16.0F, 16.0F}, .preferred = {16.0F, 16.0F}});
            title->add(icon);
            title->add(std::make_shared<lib::Label>(prop_->requiredMsgs().bgm.value));
        }
        pane->add(title);

        bgm_toolbar_ = std::make_shared<lib::ToolBar>();
        if (all_mode) {
            bgm_toolbar_->set_layout_item({.vertical = lib::Align::Center,
                                           .minimum = {0.0F, 22.0F},
                                           .preferred = {0.0F, 22.0F}});
        } else {
            bgm_toolbar_->set_layout_item({.row = 0U, .column = 1U,
                                           .horizontal = lib::Align::End});
            pane->add(bgm_toolbar_);
        }
        bgm_selector_->createPlayToolItem(bgm_toolbar_);
        if (!all_mode) {
            bgm_toolbar_->items.push_back({"", "", {}, false, false, true});
            bgm_selector_->createRefreshToolItem(bgm_toolbar_);
            bgm_selector_->createDirectoryToolItem(bgm_toolbar_);
        }

        // ALLは自然な幅の一行に、再生、その直後にディ
        // レクトリ選択を置く。 単独起動ではDの独立した
        // タイトル・2行目の選択欄・ツールバー列を保つ。
        if (all_mode) title->add(bgm_toolbar_);
        const auto dirs = bgm_selector_->createDirsCombo(
            all_mode ? std::static_pointer_cast<lib::Container>(title)
                     : std::static_pointer_cast<lib::Container>(pane));
        if (all_mode) {
            dirs->set_layout_item({.horizontal = lib::Align::Start,
                                   .vertical = lib::Align::Center,
                                   .minimum = {130.0F, 0.0F},
                                   .preferred = {130.0F, 0.0F}});
        } else {
            dirs->set_layout_item({.row = 1U, .column = 0U, .column_span = 2U,
                                   .horizontal = lib::Align::Stretch});
        }
        const auto files = bgm_selector_->createFileCombo(pane);
        files->set_layout_item({.row = all_mode ? 1U : 2U, .column = 0U,
                                .column_span = all_mode ? 1U : 2U,
                                .horizontal = lib::Align::Stretch,
                                .vertical = lib::Align::Center,
                                .minimum = {0.0F, 21.0F},
                                .preferred = {0.0F, 21.0F}});
        const auto playing = bgm_selector_->createPlayingBar(pane);
        if (all_mode) {
            playing->set_layout_item({.row = 2U, .column = 0U, .column_span = 1U,
                                      .horizontal = lib::Align::Stretch,
                                      .vertical = lib::Align::Center,
                                      .minimum = {0.0F, 28.0F * 2.0F / 3.0F},
                                      .preferred = {0.0F, 28.0F * 2.0F / 3.0F},
                                      .constrain_minimum_to_preferred = true});
        } else {
            // DのTimeBar::computeSizeは高さ28.ppisを返す。 指
            // 定しないと共通Canvasの既定値が160pxになるため、高さを明示する。
            playing->set_layout_item({.row = 3U, .column = 0U, .column_span = 2U,
                                      .horizontal = lib::Align::Stretch,
                                      .vertical = lib::Align::Center,
                                      .minimum = {0.0F, 28.0F},
                                      .preferred = {0.0F, 28.0F},
                                      .constrain_minimum_to_preferred = true});
        }
        // ALLの音声設定は、別のモーダル画面の作業コピーで編集する。
        if (all_mode) {
            bgm_settings_button_ = std::make_shared<lib::Button>();
            bgm_settings_button_->text = "BGM設定…";
            bgm_settings_button_->set_layout_item({.row = 3U, .column = 0U,
                .horizontal = lib::Align::Stretch});
            const std::weak_ptr<void> life = scene_view_lifetime_;
            bgm_settings_button_->on_click = [this, life] {
                if (life.expired() || read_only_) return;
                const auto source = area_;
                BattleBGMSettingsDialog dialog(*context_, *prop_, *source, summary_);
                if (!dialog.open() || life.expired() || area_ != source) return;
                (void)applyBattleBGM({source->music(),
                    static_cast<std::uint32_t>(std::max(0, dialog.volume())),
                    static_cast<std::uint32_t>(std::max(0, dialog.loopCount())),
                    static_cast<std::uint32_t>(std::max(0, dialog.fadeIn())),
                    source->continueBGM()});
            };
            pane->add(bgm_settings_button_);
        } else {
            const auto options = bgm_selector_->createPlayingOptions(pane, false, true);
            options->set_layout_item({.row = 4U, .column = 0U, .column_span = 2U,
                                      .horizontal = lib::Align::Stretch});
            const auto fade = bgm_selector_->createFadeIn(pane);
            const auto controls = fade->children();
            fade->clear();
            auto fade_layout = std::make_shared<lib::GridLayout>();
            fade_layout->columns = 3U;
            fade_layout->spacing = 4.0F;
            fade_layout->padding = {};
            fade->set_layout(fade_layout);
            auto label = std::make_shared<lib::Label>(prop_->requiredMsgs().fadeIn.value + ":");
            label->set_layout_item({.row = 0U, .column = 0U,
                                    .preferred = {112.0F, 23.0F}});
            fade->add(label);
            if (!controls.empty()) {
                controls.front()->set_layout_item({.row = 0U, .column = 1U,
                                                    .preferred = {58.0F, 24.0F}});
                fade->add(controls.front());
            }
            auto hint = std::make_shared<lib::Label>(prop_->requiredMsgs().fadeInHint.value);
            hint->set_layout_item({.row = 0U, .column = 2U,
                                   .preferred = {62.0F, 23.0F}});
            fade->add(hint);
            fade->set_layout_item({.row = 5U, .column = 0U,
                                   .column_span = 2U,
                                   .horizontal = lib::Align::End});
        }

        syncBattleBGMSelector();
        bgm_selector_->modEvent.push_back([this] { selectBGM(); });
        return pane;
        }
    }

    void syncBattleBGMSelector() {
        if constexpr (std::is_same_v<A, Battle>) {
            if (!bgm_selector_ || syncing_bgm_) return;
            if (bgm_settings_button_) bgm_settings_button_->set_enabled(!read_only_);
            syncing_bgm_ = true;
            // MaterialSelectの設定はmodEventを
            // 同期的に通知する。これはユーザー編集ではなく表示同期な
            // ので、 通知全体の間、ビューからモデルへのコールバック
            // を外す。 特にBGM継続への切り替え途中の空パスなど
            // が、Battleへ旧既定値を書き戻さないようにする。
            auto selector_mod_events = std::move(bgm_selector_->modEvent);
            bgm_selector_->modEvent.clear();
            const BattleBGMSettings expected{area_->music(), area_->volume(),
                                              area_->loopCount(), area_->fadeIn(),
                                              area_->continueBGM()};
            const bool can_continue = !summary_ || !summary_->legacy();
            if (bgm_selector_->canContinue() != can_continue)
                bgm_selector_->canContinue(can_continue);
            // 共通MaterialSelectは継続へ切り替え
            // る際、ディレクトリ選択の変更中にパスを一時的に空
            // にする。 継続状態を先に設定してからBattle
            // のパスを反映し、後の選択通知で 古い空パスがBGM
            // の五項目の更新結果を上書きしないようにする。
            if (bgm_selector_->continueBGM() != area_->continueBGM())
                bgm_selector_->continueBGM(area_->continueBGM());
            if (bgm_selector_->path() != area_->music())
                bgm_selector_->path2(area_->music(), true, true, -1, false);
            if (bgm_selector_->volume() != static_cast<int>(area_->volume()))
                bgm_selector_->volume(static_cast<int>(area_->volume()));
            if (bgm_selector_->loopCount() != static_cast<int>(area_->loopCount()))
                bgm_selector_->loopCount(static_cast<int>(area_->loopCount()));
            if (bgm_selector_->fadeInModelValue() != static_cast<int>(area_->fadeIn()))
                bgm_selector_->fadeIn(static_cast<int>(area_->fadeIn()));
            // 部品の値を反映中に、ホストが入れ子のイベン
            // トを送る場合がある。 モデルのスナップショ
            // ットを保ち、Undoを追加せず、場面モデル
            // からは一まとまりの更新に見えるようにする。
            if (area_->music() != expected.music || area_->volume() != expected.volume ||
                area_->loopCount() != expected.loop_count || area_->fadeIn() != expected.fade_in ||
                area_->continueBGM() != expected.continue_bgm) {
                area_->music(expected.music);
                area_->volume(expected.volume);
                area_->loopCount(expected.loop_count);
                area_->fadeIn(expected.fade_in);
                area_->continueBGM(expected.continue_bgm);
            }
            bgm_selector_->modEvent = std::move(selector_mod_events);
            syncing_bgm_ = false;
        }
    }

    void syncBattleRunAwayControl() {
        if constexpr (std::is_same_v<A, Battle>) {
            if (!possible_to_run_away_control_) return;
            possible_to_run_away_control_->checked = area_->possibleToRunAway();
            possible_to_run_away_control_->set_enabled(canReversePossibleToRunAway());
        }
    }

    static constexpr float kAllListToolScale = 1.3F;

    [[nodiscard]] std::shared_ptr<lib::Panel> createListPane(
        std::string title, const char* icon_name,
        const std::shared_ptr<lib::TableView>& table,
        const std::shared_ptr<lib::ToolBar>& tools) {
        auto pane = std::make_shared<lib::Panel>();
        auto layout = std::make_shared<lib::GridLayout>();
        layout->columns = 1U;
        layout->spacing = 1.0F;
        layout->padding = {};
        pane->set_layout(layout);

        auto header = std::make_shared<lib::Panel>();
        auto header_layout = std::make_shared<lib::LinearLayout>(lib::Orientation::Horizontal);
        header_layout->spacing = 2.0F;
        header_layout->cross_axis = lib::Align::Center;
        header->set_layout(header_layout);
        lib::LayoutItem header_item;
        header_item.row = 0U;
        header_item.preferred = {0.0F, 24.0F};
        header_item.horizontal = lib::Align::Stretch;
        header->set_layout_item(header_item);

        const bool all_mode = lib::editor_window_host_options().all_mode;
        const bool compact_card_header = all_mode && table == cards_;
        // ALLではエリア・バトル・背景セルの各一覧で
        // 、「背景」の見出しやアイコンを表示しない。
        const bool compact_background_header = all_mode && table == backs_;
        const bool compact_header_title = compact_card_header ||
            compact_background_header;
        if (!compact_header_title) {
            auto icon = std::make_shared<lib::ImageView>();
            icon->image = prop_->images().load(dprops::StringView{icon_name});
            // 単独起動のカード・背景の見出しは、倍率を適用した16px
            // セルを使う。 Noneでは素材をセルの左上へ等倍
            // で描き、UI拡大時に小さく上寄せになってしまう。
            const bool scale_caption_icon = !all_mode &&
                (table == cards_ || table == backs_);
            icon->scale_mode = scale_caption_icon
                ? lib::ImageScaleMode::Fit : lib::ImageScaleMode::None;
            if (scale_caption_icon)
                icon->interpolation = lib::Interpolation::Nearest;
            // Dの見出しアイコンは枠なしの16x16。 ImageView
            // の既定の凹んだ枠では、両側のピクセルが切れてしまう。
            icon->border = false;
            lib::LayoutItem icon_item;
            icon_item.minimum = {16.0F, 16.0F};
            icon_item.preferred = {16.0F, 16.0F};
            icon->set_layout_item(icon_item);
            header->add(icon);

            auto label = std::make_shared<lib::Label>(std::move(title));
            lib::LayoutItem label_item;
            label_item.minimum = {36.0F, 20.0F};
            label_item.preferred = {58.0F, 20.0F};
            label_item.grow = compact_background_header ? 0.0F : 1.0F;
            if constexpr (std::is_same_v<A, Battle> &&
                          std::is_same_v<C, EnemyCard>) {
                if (all_mode) label_item.grow = 0.0F;
            }
            label->set_layout_item(label_item);
            header->add(label);
        }

        lib::LayoutItem tools_item;
        // Dと同じく、各SWT ToolBarに自然な幅を使わせ
        // る。 92px固定では短いカード欄は収まるが、背景欄は
        // 最初の三つで切れ、画像・文字・色・PCの追加が隠れる。
        tools_item.minimum = {0.0F, 22.0F};
        tools_item.preferred = {0.0F, 22.0F};
        if (all_mode) {
            // ALL左下の一覧操作（表示・上・下・追加な
            // ど）は、押しやすいよう縦横1.3倍にする
            // 。 24pxのボタンは約32pxとなる。
            tools->icon_scale = kAllListToolScale;
            tools_item.minimum.height = std::ceil(24.0F * kAllListToolScale);
            tools_item.preferred.height = tools_item.minimum.height;
            header_item.preferred.height = std::max(header_item.preferred.height,
                                                    tools_item.preferred.height);
            header->set_layout_item(header_item);
        }
        tools->set_layout_item(tools_item);
        // ALLはカード・背景の見出し文字とアイコンを省き、カード行は表示切
        // り替えから始める。 単独起動ではDのタイトルを先に置く順序を保つ。
        if (compact_card_header) header->add(tools);

        if constexpr (std::is_same_v<A, Battle> && std::is_same_v<C, EnemyCard>) {
            if (all_mode) {
                const float button_height = tools_item.minimum.height;
                const auto compact_button = [button_height](const std::shared_ptr<lib::Button>& button) {
                    button->text_horizontal_padding = 0.0F;
                    button->set_layout_item({.horizontal = lib::Align::Stretch,
                                             .vertical = lib::Align::Stretch,
                                             .minimum = {40.0F, button_height},
                                             .preferred = {40.0F, button_height},
                                             .constrain_minimum_to_preferred = true});
                };
                const auto run_away = createRunAwayControl();
                compact_button(run_away);
                header->add(run_away);
                // ALLの「他」ボタンは、バトルのその他設定を開く。
                auto other = std::make_shared<lib::Button>("他");
                other->set_tooltip("その他設定");
                other->set_enabled(canOpenBattleOtherSettings());
                other->on_click = [this] { openBattleOtherSettings(); };
                compact_button(other);
                header->add(other);
            }
        }

        if (!compact_card_header) header->add(tools);
        pane->add(header);

        lib::LayoutItem table_item;
        table_item.row = 1U;
        table_item.minimum = {100.0F, 60.0F};
        table_item.grow = 1.0F;
        table->set_layout_item(table_item);
        pane->add(table);
        if constexpr (UseBacks) {
            if (table.get() == backs_.get() && show_inherit_backs_) {
                auto inherit_row = std::make_shared<lib::Panel>();
                auto inherit_layout = std::make_shared<lib::LinearLayout>(
                    lib::Orientation::Horizontal);
                inherit_layout->spacing = 3.0F;
                inherit_layout->cross_axis = lib::Align::Center;
                inherit_row->set_layout(inherit_layout);
                inherit_row->set_layout_item(
                    {.row = 2U, .column = 0U,
                     .horizontal = lib::Align::End});
                inherit_background_icon_ = std::make_shared<lib::ImageView>();
                inherit_background_icon_->scale_mode = lib::ImageScaleMode::None;
                inherit_background_icon_->set_layout_item(
                    {.preferred = {16.0F, 16.0F}});
                inherit_row->add(inherit_background_icon_);
                inherit_background_label_ = std::make_shared<lib::Label>();
                inherit_row->add(inherit_background_label_);
                pane->add(inherit_row);
                updateInheritBacks();
            }
        }
        return pane;
    }

    [[nodiscard]] std::shared_ptr<lib::Panel> createFlagPane() {
        const bool all_mode = lib::editor_window_host_options().all_mode;
        const bool compact_all_mode = all_mode;
        const float all_flag_extent = allFlagPaneExtent();
        auto pane = std::make_shared<lib::Panel>();
        auto layout = std::make_shared<lib::GridLayout>();
        layout->columns = 1U;
        layout->spacing = 2.0F;
        layout->padding = {};
        pane->set_layout(layout);
        // ALL下部の参照領域は100pxまで縮められるようにし
        // 、 見出し部品の推奨最小サイズをそのまま引き継がない。
        if (all_mode) pane->set_layout_item({.minimum = {all_flag_extent, 0.0F}});

        auto header = std::make_shared<lib::Panel>();
        auto header_layout = std::make_shared<lib::LinearLayout>(lib::Orientation::Horizontal);
        header_layout->spacing = compact_all_mode ? 1.0F : 3.0F;
        header_layout->cross_axis = lib::Align::Center;
        header->set_layout(header_layout);
        lib::LayoutItem header_item;
        header_item.row = 0U;
        header_item.preferred = {0.0F, 24.0F};
        header->set_layout_item(header_item);
        // ALLの狭い操作欄には単独起動の参照アイコンを置かず
        // 、 一括チェックの見出し（全TRUE/FALSE）だ
        // けを表示する。通常Editorの見出しは変えない。
        if (!compact_all_mode) {
            auto icon = std::make_shared<lib::ImageView>();
            icon->image = prop_->images().flag();
            icon->scale_mode = lib::ImageScaleMode::None;
            lib::LayoutItem icon_item;
            icon_item.minimum = {16.0F, 16.0F};
            icon_item.preferred = {16.0F, 16.0F};
            icon->set_layout_item(icon_item);
            header->add(icon);
        }
        if (!compact_all_mode) {
            auto label = std::make_shared<lib::Label>(
                all_mode ? prop_->requiredMsgs().areaViewRefAreaDesc.value
                         : prop_->requiredMsgs().refFlags.value);
            lib::LayoutItem label_item;
            // ALLの一括チェックは見出しの直後へ置き、伸縮
            // するラベルに押されて端へ移動しないようにする。
            label_item.grow = all_mode ? 0.0F : 1.0F;
            label->set_layout_item(label_item);
            header->add(label);
        }
        pane->add(header);

        lib::LayoutItem flag_item;
        flag_item.row = 1U;
        flag_item.minimum = {all_mode ? all_flag_extent : 100.0F, 80.0F};
        flag_item.grow = 1.0F;
        flags_->set_layout_item(flag_item);
        pane->add(flags_);

        incsearch::IncSearchSettings search_settings;
        search_settings.startIncrementalSearchWhenKeyDown =
            prop_->var().startIncrementalSearchWhenKeyDown.value;
        search_settings.startIncrementalSearchWhenKeyDownProvider =
            [this, life=std::weak_ptr<void>(scene_view_lifetime_)] {
                return !life.expired() && prop_ &&
                    prop_->var().startIncrementalSearchWhenKeyDown.value;
            };
        search_settings.incrementalSearchBoxWidth =
            prop_->var().incrementalSearchBoxWidth.value;
        search_settings.incrementalSearchType = prop_->var().incrementalSearchType.value;
        search_settings.containsLabel = prop_->requiredMsgs().incSearchContains.value;
        search_settings.wildcardLabel = prop_->requiredMsgs().incSearchWildcard.value;
        search_settings.regexLabel = prop_->requiredMsgs().incSearchRegex.value;
        search_settings.windowTitle = prop_->requiredMsgs().menuText(types::MenuID::IncSearch);
        flag_inc_search_ = std::make_unique<incsearch::IncSearch>(
            context_->services, flags_.get(), [this] { return flag_has_paths_; },
            std::vector<incsearch::AdditionMatcher>{}, std::move(search_settings));
        flag_inc_search_->modEvent.push_back([this] {
            prop_->var().incrementalSearchType.value =
                static_cast<std::int32_t>(flag_inc_search_->searchType());
            refreshFlagRows();
        });

        flag_menu_ = std::make_shared<lib::Menu>(true);
        flag_menu_->on_open = [this,life=std::weak_ptr<void>(scene_view_lifetime_)] {
            if(life.expired())return;
            bool can_open = false;
            if (!read_only_ && flags_ && !flags_->selection.empty()) {
                const auto row = flags_->selection.front();
                if (row < flag_row_paths_.size() && !flag_row_paths_[row].empty()) {
                    const auto root = summary_ ? summary_->flagDirRoot() : nullptr;
                    const auto counter = [&]() -> ::cwxeditor::cwx::flag::UseCounterPtr {
                        if constexpr (std::is_same_v<A, BgImageContainer>) return context_->use_counter_owner;
                        else return area_->useCounter();
                    }();
                    can_open = static_cast<bool>(::cwxeditor::cwx::flag::findVar<
                        ::cwxeditor::cwx::flag::Flag>(root, counter,
                                                     flag_row_paths_[row]));
                }
            }
            flag_menu_->items = {
                {prop_->requiredMsgs().menuText(types::MenuID::IncSearch),
                 "incremental-search", {}, 0, flag_has_paths_},
                {"", "", {}, 0, true, false, false, true},
                {prop_->requiredMsgs().menuText(types::MenuID::OpenAtVarView),
                 "open-variable", {}, 0, can_open}};
        };
        flag_menu_->on_command = [this,life=std::weak_ptr<void>(scene_view_lifetime_)](const std::string_view command) {
                if(life.expired())return;
            if (command == "incremental-search") {
                flagIncSearch();
                return;
            }
            if (command != "open-variable" || !context_->open_cwx_path ||
                !flags_ || flags_->selection.empty()) return;
            const auto row = flags_->selection.front();
            if (row >= flag_row_paths_.size() || flag_row_paths_[row].empty()) return;
            const auto root = summary_ ? summary_->flagDirRoot() : nullptr;
            const auto counter = [&]() -> ::cwxeditor::cwx::flag::UseCounterPtr {
                if constexpr (std::is_same_v<A, BgImageContainer>) return context_->use_counter_owner;
                else return area_->useCounter();
            }();
            const auto flag = ::cwxeditor::cwx::flag::findVar<
                ::cwxeditor::cwx::flag::Flag>(root, counter, flag_row_paths_[row]);
            if (flag) (void)context_->open_cwx_path(
                ::cwxeditor::cwx::path::cpaddattr(flag->cwxPath(true), "shallow"), false);
        };

        all_flags_check_ = std::make_shared<lib::CheckBox>(prop_->requiredMsgs().allCheckFlag.value);
        all_flags_check_->on_changed = [this](const bool checked) {
            for (auto& row : flags_->rows) {
                row.checked = checked;
                row.grayed = false;
            }
            applyFlagChecks();
        };
        if (all_mode) {
            header->add(all_flags_check_);
        } else {
            all_flags_check_->set_layout_item({.row = 2U, .column = 0U});
            pane->add(all_flags_check_);
        }
        return pane;
    }

    // ALLで読み取り専用・背景
    // 専用・カードなしの場面かを
    // 判定する。フラグ欄の実際の
    // 幅はallFlagPaneExtent
    // で決める。
    [[nodiscard]] bool allFlagPaneIsWide() const noexcept {
        return lib::editor_window_host_options().all_mode &&
               (read_only_ || !UseCards || cardCount() == 0U);
    }

    [[nodiscard]] float allFlagPaneExtent() const noexcept {
        // 背景専用ビューは背景セルのダイアログ内に置き、 フラグ一覧はconfigureSplitWeights
        // で指定する250pxにする。
        if constexpr (!UseCards) return kBgImagesListExtent;
        // ALLのフラグ領域は、どの場面でも初期幅・最小幅を180pxと
        // する。 表の列はそれより広くし、横スクロールできるようにする。
        return allFlagPaneIsWide() ? 180.0F : 180.0F;
    }

public:
    // 背景セル編集ダイアログの左右の一覧の初期幅。
    static constexpr float kBgImagesListExtent = 250.0F;
    // 背景ビューの互換アダプターは、設定通知から共有Undo
    // 履歴を更新できる。 ツールバー状態の更新は、非公
    // 開の部品とコンテキストを持つ基底クラス内で行う。
    void refreshUndoToolbarEnablement() {
        refreshPrimaryToolBarEnablement();
        if (context_ && context_->refresh_toolbar) context_->refresh_toolbar();
    }
private:

    void configureSplitWeights() {
        const auto bind = [](const std::shared_ptr<lib::SplitView>& split,
                             auto& first, auto& second,
                             const std::optional<float> initial_first = {},
                             const std::optional<float> initial_second = {},
                             const bool force_initial = false) {
            if (!split) return;
            split->weights = {static_cast<float>(std::max(1, first.value)),
                              static_cast<float>(std::max(1, second.value))};
            if (force_initial || (!first.readValue && !second.readValue)) {
                split->initial_first_extent = initial_first;
                split->initial_second_extent = initial_second;
            }
            split->on_weights_changed = [&first, &second](const std::vector<float>& weights) {
                if (weights.size() < 2U) return;
                first.value = std::max(1, static_cast<int>(std::lround(weights[0])));
                second.value = std::max(1, static_cast<int>(std::lround(weights[1])));
            };
        };
        bind(list_split_, prop_->var().areaSashT, prop_->var().areaSashB);
        if constexpr (std::is_same_v<A, Area>) {
            // ALLではメニューカードと背景一覧の境界を3px
            // 移すが、保存済みの分割比率は変えない。
            if (lib::editor_window_host_options().all_mode)
                list_split_->initial_first_extent_offset = 3.0F;
        }
        if constexpr (std::is_same_v<A, Area>) {
            const bool all_mode = lib::editor_window_host_options().all_mode;
            if (all_mode) {
                bind(main_split_, prop_->var().areaViewR, prop_->var().areaViewL,
                     {}, std::optional<float>{160.0F + allFlagPaneExtent()});
                bind(right_split_, prop_->var().areaViewImageFlagL,
                     prop_->var().areaViewImageFlagR, std::optional<float>{160.0F},
                     std::optional<float>{allFlagPaneExtent()});
            } else {
                bind(main_split_, prop_->var().areaViewL, prop_->var().areaViewR,
                     std::optional<float>{100.0F}, {});
                bind(right_split_, prop_->var().areaViewImageFlagL,
                     prop_->var().areaViewImageFlagR, {}, std::optional<float>{100.0F});
            }
        } else if constexpr (std::is_same_v<A, Battle>) {
            const bool all_mode = lib::editor_window_host_options().all_mode;
            if (all_mode) {
                bind(main_split_, prop_->var().battleViewR, prop_->var().battleViewL,
                     {}, std::optional<float>{160.0F + allFlagPaneExtent()});
                bind(right_split_, prop_->var().battleViewImageFlagL,
                     prop_->var().battleViewImageFlagR, std::optional<float>{160.0F},
                     std::optional<float>{allFlagPaneExtent()});
            } else {
                bind(main_split_, prop_->var().battleViewL, prop_->var().battleViewR,
                     std::optional<float>{100.0F}, {});
                bind(right_split_, prop_->var().battleViewImageFlagL,
                     prop_->var().battleViewImageFlagR, {}, std::optional<float>{100.0F});
            }
        } else {
            if (lib::editor_window_host_options().all_mode) {
                // ALL専用のD版との差異。背景セル編集
                // は、セル一覧・フラグ一覧を各250px
                // で開き、 残りを場面ペインへ割り当てる
                // 。ドラッグ時の比率は保存するが、 そ
                // の値で初期ダイアログの幅を広げない。
                bind(main_split_, prop_->var().bgImageViewL, prop_->var().bgImageViewR,
                     std::optional<float>{kBgImagesListExtent}, {}, true);
                bind(right_split_, prop_->var().bgImageViewImageFlagL,
                     prop_->var().bgImageViewImageFlagR, {},
                     std::optional<float>{kBgImagesListExtent}, true);
            } else {
                bind(main_split_, prop_->var().bgImageViewL, prop_->var().bgImageViewR,
                     std::optional<float>{100.0F});
                bind(right_split_, prop_->var().bgImageViewImageFlagL,
                     prop_->var().bgImageViewImageFlagR, {},
                     std::optional<float>{100.0F});
            }
        }
    }

    void refreshConfiguredTooltips(const types::MenuID id) {
        if (lib::editor_window_host_options().all_mode || !prop_) return;
        using Mapping = std::pair<types::MenuID, std::string_view>;
        std::vector<std::shared_ptr<lib::ToolBar>> changed;
        const auto update = [&](const std::shared_ptr<lib::ToolBar>& bar,
                                const std::initializer_list<Mapping> mappings) {
            if (!bar) return;
            for (const auto& [menu, command] : mappings) {
                if (menu != id) continue;
                const auto text = prop_->buildTool(id);
                bool dirty = false;
                for (auto& item : bar->items) {
                    if (item.command == command && item.tooltip != text) {
                        item.tooltip = text;
                        dirty = true;
                    }
                }
                if (dirty) {
                    ++bar->item_update_revision;
                    changed.push_back(bar);
                }
            }
        };
        update(toolbar_, {
            {types::MenuID::EditEvent, "open_event"},
            {types::MenuID::EditSceneDup, "open_duplicate"},
            {types::MenuID::Refresh, "refresh"},
            {types::MenuID::Undo, "undo"}, {types::MenuID::Redo, "redo"},
            {types::MenuID::Up, "up"}, {types::MenuID::Down, "down"},
            {types::MenuID::ShowParty, "view_party"},
            {types::MenuID::ShowMsg, "view_message"},
            {types::MenuID::AutoArrange, "auto"},
            {types::MenuID::ManualArrange, "custom"}});
        update(card_list_toolbar_, {
            {types::MenuID::ShowCard, "view_cards"},
            {types::MenuID::FixedCards, "fixed_cards"},
            {std::is_same_v<C, EnemyCard> ? types::MenuID::NewEnemyCard
                                        : types::MenuID::NewMenuCard, "new_card"}});
        update(back_list_toolbar_, {
            {types::MenuID::ShowBack, "view_backs"},
            {types::MenuID::FixedCells, "fixed_cells"},
            {types::MenuID::FixedBackground, "fixed_background"},
            {types::MenuID::NewBack, "new_back"},
            {types::MenuID::NewTextCell, "new_text"},
            {types::MenuID::NewColorCell, "new_color"},
            {types::MenuID::NewPCCell, "new_pc"}});
        update(selection_toolbar_, {{types::MenuID::Mask, "mask"},
                                    {types::MenuID::Escape, "escape"}});
        update(grid_toolbar_, {{types::MenuID::ShowGrid, "grid"}});
        // 再生・停止でラベルが変わるBGM欄は含めない。 外部への再描画要
        // 求でビューが終了し得るため、必要な状態を先にすべてコピーする。
        const std::weak_ptr<void> life = scene_view_lifetime_;
        const auto gui = context_ ? context_->services.gui : nullptr;
        for (const auto& bar : changed) {
            if (life.expired()) return;
            if (gui) gui->request_redraw(bar, std::nullopt);
        }
    }

    void setupListToolBars() {
        const auto image = [this](const char* name) {
            return prop_->images().load(dprops::StringView{name});
        };
        const auto tool = [this](const types::MenuID id, std::string command,
                                 const lib::ResourceId icon, const bool enabled = true,
                                 const bool checked = false) {
            lib::ToolItem item;
            item.command = std::move(command);
            item.icon = icon;
            item.enabled = enabled;
            item.checked = checked;
            item.tooltip = prop_->buildTool(id);
            return item;
        };
        const auto separator = lib::ToolItem{"", "", {}, true, false, true};
        if (card_list_toolbar_) {
            std::vector<lib::ToolItem> items{
                tool(types::MenuID::ShowCard, "view_cards", image("show_cards.png"),
                     true, view_cards_),
                tool(types::MenuID::FixedCards, "fixed_cards", image("fixed.png"),
                     !read_only_, fixed_cards_)};
            // ALLのバトルでは、「逃」「他」も置く見出
            // し行の幅を節約するため、区切りを入れない。
            if (!(std::is_same_v<A, Battle> && lib::editor_window_host_options().all_mode))
                items.push_back(separator);
            items.push_back(tool(std::is_same_v<C, EnemyCard> ? types::MenuID::NewEnemyCard
                                                              : types::MenuID::NewMenuCard,
                                 "new_card", image("card_new.png"), canCreateCard()));
            (void)lib::replace_tool_bar_items(card_list_toolbar_, std::move(items));
            card_list_toolbar_->on_command = [this,life=std::weak_ptr<void>(scene_view_lifetime_)](const std::string_view command) {
                if(life.expired())return;
                if (command == "view_cards") reverseViewCards();
                else if (command == "fixed_cards") reverseFixedCards();
                else if (command == "new_card") createCard();
            };
        }
        if (back_list_toolbar_) {
            std::vector<lib::ToolItem> items{
                tool(types::MenuID::ShowBack, "view_backs", image("show_backs.png"),
                     true, view_backs_),
                tool(types::MenuID::FixedCells, "fixed_cells", image("fixed_cell.png"),
                     !read_only_, fixed_cells_),
                tool(types::MenuID::FixedBackground, "fixed_background",
                     image("fixed_background.png"), !read_only_, fixed_background_),
            };
            // ALLには表示・セル固定の操作を残す。背景セ
            // ル作成の四つの操作は、 狭い操作欄には置かず
            // 、コンテキストメニューから使えるようにする。
            if (!lib::editor_window_host_options().all_mode) {
                items.push_back(separator);
                items.push_back(tool(types::MenuID::NewBack, "new_back",
                                     image("back_new.png"), !read_only_));
                items.push_back(tool(types::MenuID::NewTextCell, "new_text",
                                     image("text_cell_new.png"), !read_only_));
                items.push_back(tool(types::MenuID::NewColorCell, "new_color",
                                     image("color_cell_new.png"), !read_only_));
                items.push_back(tool(types::MenuID::NewPCCell, "new_pc",
                                     image("pc_cell_new.png"), canCreatePCCell()));
            }
            (void)lib::replace_tool_bar_items(back_list_toolbar_, std::move(items));
            back_list_toolbar_->on_command = [this,life=std::weak_ptr<void>(scene_view_lifetime_)](const std::string_view command) {
                if(life.expired())return;
                if (command == "view_backs") reverseViewBacks();
                else if (command == "fixed_cells") reverseFixedCells();
                else if (command == "fixed_background") reverseFixedBackground();
                else if (command == "new_back") createBackground();
                else if (command == "new_text") createTextCell();
                else if (command == "new_color") createColorCell();
                else if (command == "new_pc") createPCCell();
            };
        }
    }

    // 一覧ツールバーの切り替え表示を、Dと同じく現在のビュー状
    // 態へ同期する。 切り替え後の再構成でも、当たり判定では
    // 固定済みなのにボタンが未チェックになる状態を残さない。
    void refreshListToolBarEnablement() {
        const auto update = [this](const std::shared_ptr<lib::ToolBar>& toolbar) {
            if (!toolbar) return;
            bool changed = false;
            for (auto& item : toolbar->items) {
                const bool checked = item.command == "view_cards" ? view_cards_
                                   : item.command == "fixed_cards" ? fixed_cards_
                                   : item.command == "view_backs" ? view_backs_
                                   : item.command == "fixed_cells" ? fixed_cells_
                                   : item.command == "fixed_background" ? fixed_background_
                                   : item.checked;
                const bool enabled = item.command == "new_card" ? canCreateCard()
                                   : item.enabled;
                if (item.checked != checked) {
                    item.checked = checked;
                    changed = true;
                }
                if (item.enabled != enabled) {
                    item.enabled = enabled;
                    changed = true;
                }
            }
            if (changed) requestWidgetRedraw(toolbar);
        };
        update(card_list_toolbar_);
        update(back_list_toolbar_);
    }

    void refreshPrimaryToolBarEnablement() {
        if (!toolbar_ && !all_history_toolbar_) return;
        const std::weak_ptr<void> life = scene_view_lifetime_;
        bool changed = false;
        const auto update = [this, &changed, life](const std::shared_ptr<lib::ToolBar>& bar) {
            if (life.expired() || !bar) return;
            for (auto& item : bar->items) {
                // DのreverseViewMsgと同じく、既存のメッセージ表示ボタンをすぐ更新
                // する。 reverseViewPartyも同じようにツールバー項目を更新する。
                const bool checked = item.command == "view_party" ? view_party_
                                   : item.command == "view_message" ? view_message_
                                   : item.command == "show_reference_cards" ? show_ref_cards_
                                   : item.command == "show_debug" ? debug_mode_
                                   : item.checked;
                if (!lib::editor_window_host_options().all_mode &&
                    item.checked != checked &&
                    (item.command == "view_party" || item.command == "view_message" ||
                     item.command == "show_reference_cards" ||
                     item.command == "show_debug")) {
                    item.checked = checked;
                    changed = true;
                }
                const bool enabled = item.command == "undo" ? canUndo()
                                   : item.command == "redo" ? canRedo()
                                   : item.command == "up" ? canUp()
                                   : item.command == "down" ? canDown()
                                   : item.enabled;
                if (item.enabled == enabled) continue;
                item.enabled = enabled;
                changed = true;
            }
            if (changed) requestWidgetRedraw(bar);
        };
        update(toolbar_);
        if (life.expired()) return;
        update(all_history_toolbar_);
    }

    void setupSelectionToolBars() {
        if (selection_toolbar_) {
            std::vector<lib::ToolItem> items;
            bool mask = false;
            bool can_mask = false;
            if constexpr (UseBacks) {
                for (const auto index : selected_backs_) {
                    if (index >= backCount()) continue;
                    const auto* image = dynamic_cast<const background::ImageCell*>(&back(index));
                    if (!image) continue;
                    if (!can_mask) mask = image->mask();
                    can_mask = true;
                }
                lib::ToolItem item;
                item.command = "mask";
                item.icon = prop_->images().load(dprops::StringView{"mask.png"});
                item.enabled = !read_only_ && can_mask;
                item.checked = mask;
                item.tooltip = prop_->buildTool(types::MenuID::Mask);
                items.push_back(std::move(item));
            }
            if constexpr (std::is_same_v<A, Battle>) {
                lib::ToolItem item;
                item.command = "escape";
                item.icon = prop_->images().load(dprops::StringView{"escape.png"});
                item.enabled = !read_only_ && !selected_cards_.empty();
                item.checked = selectedEnemiesCanRunAway();
                item.tooltip = prop_->buildTool(types::MenuID::Escape);
                items.push_back(std::move(item));
            }
            (void)lib::replace_tool_bar_items(selection_toolbar_, std::move(items));
            selection_toolbar_->on_command = [this,life=std::weak_ptr<void>(scene_view_lifetime_)](const std::string_view command) {
                if(life.expired())return;
                if (command == "mask") {
                    setMask();
                } else if (command == "escape") setEscape();
            };
        }
        if (grid_toolbar_) {
            lib::ToolItem item;
            item.command = "grid";
            item.icon = prop_->images().load(dprops::StringView{"grid.png"});
            item.checked = show_grid_;
            item.tooltip = prop_->buildTool(types::MenuID::ShowGrid);
            (void)lib::replace_tool_bar_items(grid_toolbar_, {std::move(item)});
            grid_toolbar_->on_command = [this,life=std::weak_ptr<void>(scene_view_lifetime_)](const std::string_view command) {
                if(life.expired())return;
                if (command == "grid") reverseShowGrid();
            };
        }
    }

    enum class GeometryField { X, Y, Width, Height, Scale, Layer };

    void addGeometryControl(std::string label,
                            std::shared_ptr<lib::NumberEdit>& control,
                            const double minimum, const double maximum,
                            const GeometryField field) {
        const bool all_mode = lib::editor_window_host_options().all_mode;
        if (all_mode && !label.empty() && label.back() == ':') label.pop_back();
        std::shared_ptr<lib::Panel> compact_field;
        if (all_mode) {
            compact_field = std::make_shared<lib::Panel>();
            auto field_layout = std::make_shared<lib::LinearLayout>(lib::Orientation::Horizontal);
            field_layout->spacing = 0.0F;
            field_layout->cross_axis = lib::Align::Center;
            compact_field->set_layout(field_layout);
            compact_field->set_layout_item({.vertical = lib::Align::Center,
                                              .minimum = {0.0F, all_mode ? 34.0F : 24.0F},
                                              .preferred = {0.0F, all_mode ? 34.0F : 24.0F}});
        }
        auto caption = std::make_shared<lib::Label>(std::move(label));
        lib::LayoutItem caption_item;
        caption_item.preferred = {all_mode ? 14.0F : 18.0F,
                                  all_mode ? 34.0F : 24.0F};
        caption_item.vertical = lib::Align::Center;
        caption->set_layout_item(caption_item);
        geometry_labels_[static_cast<std::size_t>(field)] = caption;
        std::string inline_caption;
        if (compact_field) {
            // ALL仕様: [入力欄][X][＋][－]。項目名はNumberEdit内の入力欄と
            // ±ボタンの間に描くので、左側のLabelは配置しない。
            inline_caption = caption->text;
            geometry_fields_[static_cast<std::size_t>(field)] = compact_field;
        }
        else toolbar_row_->add(caption);
        control = std::make_shared<lib::NumberEdit>();
        control->minimum = minimum;
        control->maximum = maximum;
        control->step = 1.0;
        control->inline_label = std::move(inline_caption);
        if (all_mode && (field == GeometryField::X || field == GeometryField::Y ||
                         field == GeometryField::Width || field == GeometryField::Height)) {
            // ALLの入力は3桁まで。input_max_lengthは数字だけを数
            // えるため、 X/Yは先頭の負号も使え、範囲は-999〜999となる。
            control->input_max_length = 3U;
            // ALL の左下 XYWH の入力内余白はゼロとし、三桁分
            // の文字幅を確保する。増減ボタンの幅は 39px とする。
            control->step_button_extent = 39.0F;
            control->input_width_adjust = 0.0F;
            control->zero_text_padding = true;
        }
        if (all_mode && control->step_button_extent == 0.0F)
            control->step_button_extent = 37.0F;
        lib::LayoutItem editor_item;
        // 共通の計測で、この文字欄の幅へ両側の増減ボタンを加える
        // 。 行の高さは、実際の正方形ボタンの寸法へ合わせる。
        const float editor_height = !all_mode ? 24.0F
            : control->step_button_extent > 0.0F ? control->step_button_extent : 34.0F;
        editor_item.minimum = {all_mode ? 44.0F : 42.0F, editor_height};
        editor_item.preferred = {all_mode ? 46.0F : 52.0F, editor_height};
        editor_item.vertical = lib::Align::Center;
        control->set_layout_item(editor_item);
        control->on_changed = [this, field,life=std::weak_ptr<void>(scene_view_lifetime_)](const double value) {
            if(life.expired())return;
            if (!syncing_geometry_) applyGeometryValue(field, value);
        };
        control->on_preview=[this,field,life=std::weak_ptr<void>(scene_view_lifetime_)](double value) {
            if(!life.expired() && !syncing_geometry_)previewGeometryValue(field,value);
        };
        control->on_committed=[this,field,life=std::weak_ptr<void>(scene_view_lifetime_)](double value) {
            if(!life.expired() && !syncing_geometry_)commitNumericPreview(field,value);
        };
        control->on_edit_cancelled=[this,field,life=std::weak_ptr<void>(scene_view_lifetime_)] {
            if(!life.expired())cancelNumericPreview(field);
        };
        if (compact_field) {
            compact_field->add(control);
            toolbar_row_->add(compact_field);
        } else {
            toolbar_row_->add(control);
        }
    }

    void addGeometryControls() {
        const bool all_mode = lib::editor_window_host_options().all_mode;
        // C++での安全性のため、符号付きの配置値の範囲外は
        // Dのuint→intの巻き戻りでなく上限に抑える。
        // ALLの3桁制限も、同じ部品の規則で維持する。
        const auto limit=[all_mode](const std::uint32_t value){return std::min<double>(value,all_mode?999.0:std::numeric_limits<int>::max());};
        const double left_max=limit(prop_->var().posLeftMax.value),top_max=limit(prop_->var().posTopMax.value);
        addGeometryControl("X:", x_control_, -left_max,left_max, GeometryField::X);
        addGeometryControl("Y:", y_control_, -top_max,top_max, GeometryField::Y);
        addGeometryControl("W:", width_control_, 0.0,
                           limit(prop_->var().backWidthMax.value), GeometryField::Width);
        addGeometryControl("H:", height_control_, 0.0,
                           limit(prop_->var().backHeightMax.value), GeometryField::Height);
        addGeometryControl("拡大率:", scale_control_, std::max(1,prop_->var().cardScaleMin.value),
                           std::max(1,prop_->var().cardScaleMax.value), GeometryField::Scale);
        scale_percent_label_ = std::make_shared<lib::Label>("%");
        lib::LayoutItem percent_item;
        percent_item.preferred = {12.0F, 24.0F};
        percent_item.vertical = lib::Align::Center;
        scale_percent_label_->set_layout_item(percent_item);
        toolbar_row_->add(scale_percent_label_);

        selection_toolbar_ = std::make_shared<lib::ToolBar>();
        lib::LayoutItem selection_item;
        selection_item.preferred = {30.0F, 24.0F};
        selection_item.vertical = lib::Align::Center;
        selection_toolbar_->set_layout_item(selection_item);
        toolbar_row_->add(selection_toolbar_);
        addGeometryControl("レイヤ:", layer_control_, types::LAYER_BACK_CELL,
                           std::max(types::LAYER_BACK_CELL,prop_->var().layerMax.value), GeometryField::Layer);

        if (lib::editor_window_host_options().all_mode) {
            // 選択の同期に必要な部品は残すが、ALLの操作欄に
            // 倍率・レイヤー・マスクの表示領域は割り当てない。
            if (scale_control_) scale_control_->set_visible(false);
            if (layer_control_) layer_control_->set_visible(false);
            if (scale_percent_label_) scale_percent_label_->set_visible(false);
            if (selection_toolbar_) selection_toolbar_->set_visible(false);
            if (geometry_labels_[static_cast<std::size_t>(GeometryField::Scale)])
                geometry_labels_[static_cast<std::size_t>(GeometryField::Scale)]->set_visible(false);
            if (geometry_labels_[static_cast<std::size_t>(GeometryField::Layer)])
                geometry_labels_[static_cast<std::size_t>(GeometryField::Layer)]->set_visible(false);
        }

        // ALL環境ではシーンビューのグリッドon/offおよびグリッドの大きさ（X/Y）を非表示にする
        const bool show_grid_controls = !lib::editor_window_host_options().all_mode;

        if (show_grid_controls) {
            grid_toolbar_ = std::make_shared<lib::ToolBar>();
            lib::LayoutItem grid_tools_item;
            grid_tools_item.preferred = {30.0F, 24.0F};
            grid_tools_item.vertical = lib::Align::Center;
            grid_toolbar_->set_layout_item(grid_tools_item);
            toolbar_row_->add(grid_toolbar_);

            auto add_grid_control = [this](std::string title,
                                           std::shared_ptr<lib::NumberEdit>& control,
                                           std::uint32_t value,
                                           const bool horizontal) {
                auto caption = std::make_shared<lib::Label>(std::move(title));
                lib::LayoutItem caption_item;
                caption_item.preferred = {18.0F, 24.0F};
                caption_item.vertical = lib::Align::Center;
                caption->set_layout_item(caption_item);
                toolbar_row_->add(caption);
                control = std::make_shared<lib::NumberEdit>();
                control->minimum = 1.0;
                control->maximum = horizontal
                    ? static_cast<double>(prop_->looks().viewSize().width)
                    : static_cast<double>(prop_->looks().viewSize().height);
                control->value = value;
                control->step = 1.0;
                lib::LayoutItem control_item;
                control_item.minimum = {40.0F, 24.0F};
                control_item.preferred = {48.0F, 24.0F};
                control_item.vertical = lib::Align::Center;
                control->set_layout_item(control_item);
                control->on_changed = [this, horizontal](const double next) {
                    const auto value = static_cast<std::uint32_t>(
                        std::max(1.0, std::round(next)));
                    if (horizontal) {
                        grid_x_ = value;
                        prop_->var().gridX.value = value;
                    } else {
                        grid_y_ = value;
                        prop_->var().gridY.value = value;
                    }
                    refreshGrid();
                };
                toolbar_row_->add(control);
            };
            add_grid_control("X:", grid_x_control_, grid_x_, true);
            add_grid_control("Y:", grid_y_control_, grid_y_, false);
            if constexpr (std::is_same_v<A, Battle>) {
                // Dの単独起動では、この項目を場面のグリッド操作の隣に置く。
                auto other = std::make_shared<lib::Button>("その他設定");
                other->set_enabled(canOpenBattleOtherSettings());
                other->on_click = [this, life = std::weak_ptr<void>(scene_view_lifetime_)] {
                    if (!life.expired()) openBattleOtherSettings();
                };
                toolbar_row_->add(other);
            }
        }
    }

    void applyGeometryValue(const GeometryField field, const double raw_value) {
        if(read_only_ || !std::isfinite(raw_value))return;
        numeric_preview_.reset();
        const int value=static_cast<int>(std::clamp(std::round(raw_value),
            double(std::numeric_limits<int>::min()),double(std::numeric_limits<int>::max())));
        const auto cards=detail::normalizedSelection<C>(selected_cards_,cardCount());
        const auto backs=detail::normalizedSelection<BgImage>(selected_backs_,backCount());
        mutateSceneGeometry([&] {
            if constexpr(UseCards)for(const auto index:cards) {
                auto& card=areaCard(index);
                if(field==GeometryField::X)card.x(value);
                else if(field==GeometryField::Y)card.y(value);
                else if(field==GeometryField::Scale)card.scale(static_cast<std::uint32_t>(std::max(1,value)));
                else if(field==GeometryField::Layer)card.layer(value);
            }
            if constexpr(UseBacks)for(const auto index:backs) {
                auto& cell=back(index);
                if(field==GeometryField::X)cell.x(value);
                else if(field==GeometryField::Y)cell.y(value);
                else if(field==GeometryField::Width)cell.width(std::max(0,value));
                else if(field==GeometryField::Height)cell.height(std::max(0,value));
                else if(field==GeometryField::Layer)cell.layer(value);
            }
        });
    }

    [[nodiscard]] bool numericPreviewCurrent(const GeometryField field)const {
        if(!numeric_preview_ || numeric_preview_->field!=field || numeric_preview_->revision!=geometry_revision_)return false;
        return numeric_preview_->cards==captureCardGeometry() && numeric_preview_->backs==captureBackGeometry() &&
            numeric_preview_->selected_cards==detail::normalizedSelection<C>(selected_cards_,cardCount()) &&
            numeric_preview_->selected_backs==detail::normalizedSelection<BgImage>(selected_backs_,backCount());
    }
    void resetNumericPreviewImages() {
        numeric_preview_.reset();
        if constexpr(UseCards)for(std::size_t i=0;i<card_images_.size() && i<cardCount();++i)if(const auto image=card_images_[i]) {
            const auto& value=areaCard(i);image->x(value.x());image->y(value.y());image->scale(value.scale());image->layer(value.layer()*10);
        }
        if constexpr(UseBacks)for(std::size_t i=0;i<back_images_.size() && i<backCount();++i)if(const auto image=back_images_[i]) {
            const auto& value=back(i);image->x(value.x());image->y(value.y());image->width(value.width());image->height(value.height());image->layer(value.layer()*10);
        }
    }
    void previewGeometryValue(const GeometryField field,const double raw_value) {
        if(read_only_ || !std::isfinite(raw_value))return;
        if(field==GeometryField::Layer && summary_ && summary_->legacy())return;
        if(numeric_preview_ && !numericPreviewCurrent(field))resetNumericPreviewImages();
        if(!numeric_preview_)numeric_preview_=NumericPreview{field,geometry_revision_,captureCardGeometry(),captureBackGeometry(),
            detail::normalizedSelection<C>(selected_cards_,cardCount()),detail::normalizedSelection<BgImage>(selected_backs_,backCount())};
        const int value=static_cast<int>(std::clamp(std::round(raw_value),double(std::numeric_limits<int>::min()),double(std::numeric_limits<int>::max())));
        // DのeditSpnはFlexImageだけを変える。共通セッショ
        // ンが一度確定するまで、 元のモデル・参照数・履歴には触れない。
        if constexpr(UseCards)for(const auto i:numeric_preview_->selected_cards)if(i<card_images_.size())if(const auto image=card_images_[i]) {
            if(field==GeometryField::X)image->x(value);
            else if(field==GeometryField::Y)image->y(value);
            else if(field==GeometryField::Scale)image->scale(static_cast<std::uint32_t>(std::max(1,value)));
            else if(field==GeometryField::Layer)image->layer(value*10);
        }
        if constexpr(UseBacks)for(const auto i:numeric_preview_->selected_backs)if(i<back_images_.size())if(const auto image=back_images_[i]) {
            if(field==GeometryField::X)image->x(value);
            else if(field==GeometryField::Y)image->y(value);
            else if(field==GeometryField::Width)image->width(std::max(0,value));
            else if(field==GeometryField::Height)image->height(std::max(0,value));
            else if(field==GeometryField::Layer)image->layer(value*10);
        }
        requestSceneRedraw();
    }
    void commitNumericPreview(const GeometryField field,const double value) {
        if(numeric_preview_ && numeric_preview_->field!=field)resetNumericPreviewImages();
        if(numeric_preview_ && !numericPreviewCurrent(field)) {cancelNumericPreview(numeric_preview_->field);return;}
        applyGeometryValue(field,value);
    }
    void cancelNumericPreview(const GeometryField field) {
        if(!numeric_preview_ || numeric_preview_->field!=field)return;
        const std::weak_ptr<void> life=scene_view_lifetime_;const auto revision=geometry_revision_;
        resetNumericPreviewImages();syncGeometryControls();
        if(life.expired() || geometry_revision_!=revision)return;
        // Dのキャンセルは倍率混在の番兵値を返し、スピナーが現在の最小
        // 値へ補正する。 通常の選択更新では引き続き100を表示する。
        if constexpr(UseCards) {
            if(field==GeometryField::Scale && scale_control_ &&
               !lib::editor_window_host_options().all_mode) {
                std::vector<int> scales;
                for(const auto index:selected_cards_)if(index<cardCount())
                    scales.push_back(static_cast<int>(areaCard(index).scale()));
                if(!scales.empty())
                    scale_control_->value=spnValue(scales,static_cast<int>(scale_control_->minimum));
            }
        }
        requestSceneRedraw();
    }

    void syncGeometryControls() {
        if (!x_control_) return;
        syncing_geometry_ = true;
        std::vector<int> xs,ys,layers,widths,heights,scales;
        bool selected_card=false,selected_back=false;
        if constexpr(UseCards) {
            for(const auto index:selected_cards_)if(index<cardCount()) {
                const auto& card=areaCard(index);
                xs.push_back(card.x());ys.push_back(card.y());layers.push_back(card.layer());
                scales.push_back(static_cast<int>(card.scale()));selected_card=true;
            }
        }
        if constexpr(UseBacks) {
            for(const auto index:selected_backs_)if(index<backCount()) {
                const auto& cell=back(index);
                xs.push_back(cell.x());ys.push_back(cell.y());layers.push_back(cell.layer());
                widths.push_back(cell.width());heights.push_back(cell.height());selected_back=true;
            }
        }
        const bool selected=selected_card||selected_back;
        x_control_->value=spnValue(xs,0);y_control_->value=spnValue(ys,0);
        layer_control_->value=spnValue(layers,0);scale_control_->value=spnValue(scales,100);
        width_control_->value=spnValue(widths,0);height_control_->value=spnValue(heights,0);
        const auto set_geometry_enabled = [this](const GeometryField field,
                                                  const std::shared_ptr<lib::NumberEdit>& control,
                                                  const bool enabled) {
            if (control) control->set_enabled(enabled);
            const auto& label = geometry_labels_[static_cast<std::size_t>(field)];
            if (label) label->set_enabled(enabled);
        };
        const bool common_enabled = selected && !read_only_;
        const bool size_enabled = selected_back && !read_only_;
        const bool scale_enabled = selected_card && !read_only_;
        set_geometry_enabled(GeometryField::X, x_control_, common_enabled);
        set_geometry_enabled(GeometryField::Y, y_control_, common_enabled);
        set_geometry_enabled(GeometryField::Width, width_control_, size_enabled);
        set_geometry_enabled(GeometryField::Height, height_control_, size_enabled);
        set_geometry_enabled(GeometryField::Scale, scale_control_, scale_enabled);
        if (scale_percent_label_) scale_percent_label_->set_enabled(scale_enabled);
        set_geometry_enabled(GeometryField::Layer, layer_control_, common_enabled && !(summary_ && summary_->legacy()));
        setupSelectionToolBars();
        syncing_geometry_ = false;
    }

    [[nodiscard]] const Skin* activeSkin() const noexcept {
        if (force_skin_) return force_skin_;
        if (!context_) return nullptr;
        if (context_->area_window && context_->area_window->skin)
            return context_->area_window->skin;
        // AreaWindowを持たない埋め込み・単独のSceneView
        // でもAreaViewContextを使う。 その場合も、
        // MainWindowから所有権付きで渡されたSkinを
        // 、 カード・背景・装飾の描画に使う正式なスキンとする。
        return context_->skin_owner.get();
    }

    void requestWidgetRedraw(const std::shared_ptr<lib::Widget>& widget) {
        if (widget && context_ && context_->services.gui)
            context_->services.gui->request_redraw(widget, std::nullopt);
    }

    void requestSceneRedraw() {
        const std::weak_ptr<void> life=scene_view_lifetime_;
        const auto revision=geometry_revision_;
        const auto canvas=canvas_;
        const auto toolbar=toolbar_;
        const auto gui=context_?context_->services.gui:nullptr;
        const auto invalidate=canvas?canvas->invalidate:lib::Action{};
        // DのGCと異なり、公開の再描画通知でビューが解放されたり
        // 、新しい配置編集が始まったりする場合がある。 通知経路と
        // 部品を保持し、通知のたびに古い再描画の継続を判定する。
        if(invalidate)invalidate();
        if(life.expired() || geometry_revision_!=revision)return;
        if(gui && canvas)gui->request_redraw(canvas,std::nullopt);
        if(life.expired() || geometry_revision_!=revision)return;
        if(gui && toolbar)gui->request_redraw(toolbar,std::nullopt);
    }

    enum class SceneEntryKind { Background, Card, Decoration };
    // 操作中の配置はビュー内のプレビュー
    // に保持し、解放時にArea/Battle
    // へ確定する。Undoは既存の一操
    // 作分のスナップショットを使う。
    enum class SceneDragHandle {
        Move,
        LeftTop, LeftMiddle, LeftBottom,
        MiddleTop, MiddleBottom,
        RightTop, RightMiddle, RightBottom,
    };
    struct SceneEntry {
        SceneEntryKind kind{SceneEntryKind::Decoration};
        std::size_t index{};
        std::shared_ptr<areaviewutils::PileImage> image;
        int layer{};
    };

    [[nodiscard]] static bool isLeftHandle(const SceneDragHandle value) noexcept {
        return value == SceneDragHandle::LeftTop ||
               value == SceneDragHandle::LeftMiddle ||
               value == SceneDragHandle::LeftBottom;
    }
    [[nodiscard]] static bool isRightHandle(const SceneDragHandle value) noexcept {
        return value == SceneDragHandle::RightTop ||
               value == SceneDragHandle::RightMiddle ||
               value == SceneDragHandle::RightBottom;
    }
    [[nodiscard]] static bool isTopHandle(const SceneDragHandle value) noexcept {
        return value == SceneDragHandle::LeftTop ||
               value == SceneDragHandle::MiddleTop ||
               value == SceneDragHandle::RightTop;
    }
    [[nodiscard]] static bool isBottomHandle(const SceneDragHandle value) noexcept {
        return value == SceneDragHandle::LeftBottom ||
               value == SceneDragHandle::MiddleBottom ||
               value == SceneDragHandle::RightBottom;
    }

    [[nodiscard]] lib::Point sceneWorldPoint(const lib::Point point) const noexcept {
        const auto zoom = std::max(0.1F, scene_zoom_);
        return {static_cast<float>(static_cast<int>(point.x / zoom)),
                static_cast<float>(static_cast<int>(point.y / zoom))};
    }

    [[nodiscard]] SceneDragHandle sceneDragHandle(const SceneEntry& entry,
                                                   const lib::Point point) const noexcept {
        if (!entry.image) return SceneDragHandle::Move;
        const bool selected = entry.kind == SceneEntryKind::Card
            ? std::find(selected_cards_.begin(), selected_cards_.end(), entry.index) != selected_cards_.end()
            : entry.kind == SceneEntryKind::Background &&
              std::find(selected_backs_.begin(), selected_backs_.end(), entry.index) != selected_backs_.end();
        if (!selected) return SceneDragHandle::Move;
        const auto rect = entry.image->bounds();
        // Dの5pxの操作矩形は両端を含む。 両端
        // のハンドルは辺の3px手前、中央のハンド
        // ルは整数の中点の2px手前から始める。
        const auto in = [](const float value, const float start) {
            return start <= value && value <= start + 5.0F;
        };
        const bool left = in(point.x, rect.x - 3.0F);
        const bool right = in(point.x, rect.x + rect.width - 3.0F);
        const bool top = in(point.y, rect.y - 3.0F);
        const bool bottom = in(point.y, rect.y + rect.height - 3.0F);
        const bool middle_x = in(point.x, rect.x + static_cast<int>(rect.width) / 2 - 2.0F);
        const bool middle_y = in(point.y, rect.y + static_cast<int>(rect.height) / 2 - 2.0F);
        if (!lib::editor_window_host_options().all_mode) {
            // DのFlexImage::inToggleは並べ替えたToggleキーを
            // 逆順に調べる。 両端を含む同じ矩形を使い、重なり時の優先順位を合わせる。
            if (right && bottom) return SceneDragHandle::RightBottom;
            if (right && middle_y) return SceneDragHandle::RightMiddle;
            if (right && top) return SceneDragHandle::RightTop;
            if (middle_x && bottom) return SceneDragHandle::MiddleBottom;
            if (middle_x && top) return SceneDragHandle::MiddleTop;
            if (left && bottom) return SceneDragHandle::LeftBottom;
            if (left && middle_y) return SceneDragHandle::LeftMiddle;
            if (left && top) return SceneDragHandle::LeftTop;
            return SceneDragHandle::Move;
        }
        if (left && top) return SceneDragHandle::LeftTop;
        if (left && bottom) return SceneDragHandle::LeftBottom;
        if (right && top) return SceneDragHandle::RightTop;
        if (right && bottom) return SceneDragHandle::RightBottom;
        if (left && middle_y) return SceneDragHandle::LeftMiddle;
        if (right && middle_y) return SceneDragHandle::RightMiddle;
        if (middle_x && top) return SceneDragHandle::MiddleTop;
        if (middle_x && bottom) return SceneDragHandle::MiddleBottom;
        return SceneDragHandle::Move;
    }

    [[nodiscard]] static lib::CursorKind sceneToggleCursor(const SceneDragHandle handle) noexcept {
        // DのImagePaneのコンストラクターにあるtoggleCursorsに対応する。
        switch (handle) {
        case SceneDragHandle::LeftTop:
        case SceneDragHandle::RightBottom: return lib::CursorKind::SizeDiagonalNorthWest;
        case SceneDragHandle::RightTop:
        case SceneDragHandle::LeftBottom: return lib::CursorKind::SizeDiagonalNorthEast;
        case SceneDragHandle::LeftMiddle:
        case SceneDragHandle::RightMiddle: return lib::CursorKind::SizeHorizontal;
        case SceneDragHandle::MiddleTop:
        case SceneDragHandle::MiddleBottom: return lib::CursorKind::SizeVertical;
        case SceneDragHandle::Move: break;
        }
        return lib::CursorKind::Default;
    }

    void updateSceneHoverCursor(const lib::Point canvas_position) {
        if (!canvas_) return;
        const auto point = sceneWorldPoint(canvas_position);
        auto cursor = lib::CursorKind::Default;
        const auto entries = sceneEntries();
        for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
            if (!it->image || it->kind == SceneEntryKind::Decoration) continue;
            if (!it->image->visible() || sceneEntryFixed(*it)) continue;
            const auto handle = sceneDragHandle(*it, point);
            if (handle != SceneDragHandle::Move) {
                cursor = sceneToggleCursor(handle);
                break;
            }
        }
        canvas_->set_cursor(cursor);
    }

    [[nodiscard]] bool sceneEntryFixed(const SceneEntry& entry) const noexcept {
        if (entry.kind == SceneEntryKind::Card) {
            if constexpr (UseCards) return cardIsFixed(entry.index);
        } else if (entry.kind == SceneEntryKind::Background) {
            if constexpr (UseBacks) return backIsFixed(entry.index);
        }
        return true;
    }

    [[nodiscard]] std::vector<SceneEntry> sceneEntries() const {
        std::vector<SceneEntry> entries;
        if constexpr (UseBacks) {
            if (view_backs_) for (std::size_t index = 0; index < back_images_.size(); ++index)
                if (back_images_[index] && back_images_[index]->visible())
                    entries.push_back({SceneEntryKind::Background, index, back_images_[index],
                                       back_images_[index]->layer()});
        }
        if constexpr (UseCards) {
            if (view_cards_) for (std::size_t index = 0; index < card_images_.size(); ++index)
                if (card_images_[index] && card_images_[index]->visible())
                    entries.push_back({SceneEntryKind::Card, index, card_images_[index],
                                       card_images_[index]->layer()});
        }
        for (std::size_t index = 0; index < decoration_images_.size(); ++index)
            if (decoration_images_[index] && decoration_images_[index]->visible())
                entries.push_back({SceneEntryKind::Decoration, index,
                                   decoration_images_[index],
                                   decoration_images_[index]->layer()});
        std::stable_sort(entries.begin(), entries.end(), [](const auto& left, const auto& right) {
            return left.layer < right.layer;
        });
        return entries;
    }

    [[nodiscard]] static lib::Color toColor(const structs::CRGB& value) noexcept {
        return {static_cast<std::uint8_t>(std::min<std::uint32_t>(255U, value.r)),
                static_cast<std::uint8_t>(std::min<std::uint32_t>(255U, value.g)),
                static_cast<std::uint8_t>(std::min<std::uint32_t>(255U, value.b)),
                static_cast<std::uint8_t>(std::min<std::uint32_t>(255U, value.a))};
    }

    void paintFallbackCell(lib::DrawingContext& drawing, const std::size_t index,
                           const lib::Rect destination,const std::shared_ptr<cellrender::TextRasterCache>& text_cache) const {
        if constexpr (!UseBacks) return;
        if (index >= backCount()) return;
        const auto& cell = back(index);
        if (const auto* color = dynamic_cast<const background::ColorCell*>(&cell)) {
            const auto axis = color->gradientDir() == types::GradientDir::LeftToRight
                ? lib::GradientAxis::Horizontal
                : color->gradientDir() == types::GradientDir::TopToBottom
                    ? lib::GradientAxis::Vertical : lib::GradientAxis::None;
            auto mode = lib::CompositeMode::Normal;
            if (color->mask()) {
                mode = lib::CompositeMode::Mask;
            } else {
                switch (color->blendMode()) {
                case types::BlendMode::Add: mode = lib::CompositeMode::Add; break;
                case types::BlendMode::Subtract: mode = lib::CompositeMode::Subtract; break;
                case types::BlendMode::Multiply: mode = lib::CompositeMode::Multiply; break;
                case types::BlendMode::Mask: mode = lib::CompositeMode::Mask; break;
                default: break;
                }
            }
            auto gradient_rect = destination;
            if (!lib::editor_window_host_options().all_mode && prop_) {
                // Dはグラデーション生成前にColorCellを場面の範囲で切り取る。
                // 画面外にはみ出したセルでは、見えている部分全体でグラデーションを作る。
                const auto view = prop_->looks().viewSize();
                const auto right = std::min(destination.x + destination.width,
                                            static_cast<float>(view.width) * scene_zoom_);
                const auto bottom = std::min(destination.y + destination.height,
                                             static_cast<float>(view.height) * scene_zoom_);
                gradient_rect.x = std::max(0.0F, destination.x);
                gradient_rect.y = std::max(0.0F, destination.y);
                gradient_rect.width = right - gradient_rect.x;
                gradient_rect.height = bottom - gradient_rect.y;
                if (gradient_rect.width <= 0.0F || gradient_rect.height <= 0.0F) return;
            }
            drawing.fill_gradient(gradient_rect, toColor(color->color1()),
                                  toColor(color->color2()), axis, mode);
            return;
        }
        const std::weak_ptr<void> life=scene_view_lifetime_;
        const auto revision=geometry_revision_;
        const auto current=[this,life,revision]{return !life.expired() && geometry_revision_==revision;};
        if(const auto* text=dynamic_cast<const background::TextCell*>(&cell)){
            cellrender::Text style;
            style.font=dutils::createFontFromPixels(prop_?prop_->adjustFont(text->fontName()):text->fontName(),
                static_cast<int>(text->size()),text->bold(),text->italic(),text->underline(),text->strike(),text->antialias());
            style.font.points=std::max(1.0F,style.font.points*scene_zoom_);
            style.foreground=toColor(text->color());style.outline=toColor(text->borderingColor());
            style.border=text->borderingType();style.border_width=text->borderingWidth()*scene_zoom_;style.vertical=text->vertical();
            style.graphics=context_->services.graphics;
            const auto counter=text->useCounter();
            style.value=previewText(text->text(),counter.get());
            if(!current())return;
            cellrender::draw_text(drawing,destination,{destination.x,destination.y},std::move(style),current,text_cache);
            return;
        }
        if(const auto* pc=dynamic_cast<const background::PCCell*>(&cell)){
            const auto card_size=prop_->looks().cardSize();
            const lib::Rect ellipse{destination.x,destination.y,
                pc->expand()?destination.width:static_cast<float>(card_size.width)*scene_zoom_,
                pc->expand()?destination.height:static_cast<float>(card_size.height)*scene_zoom_};
            auto font=dutils::fontSpecFromDwtData(prop_->adjustFont(prop_->looks().pcNumberFont(activeSkin() && activeSkin()->legacy())));
            font.points=std::max(1.0F,font.points*scene_zoom_);
            cellrender::draw_pc(drawing,{destination,ellipse,font,toColor(prop_->looks().pcCellForeColor()),
                toColor(prop_->looks().pcCellBackColor()),std::to_string(pc->pcNumber())},current);
            return;
        }
        const auto name=cell.name(prop_->parent());
        lib::FontSpec font;font.points=10.0F*scene_zoom_;
        drawing.fill_rect(destination,{238,238,238,255});if(!current())return;
        drawing.draw_text(name,{destination.x+3,destination.y+3},font,{32,32,32,255});
    }

    void paintSceneEntry(lib::DrawingContext& drawing, SceneEntry entry, bool paint_image = true) const {
        if (!entry.image) return;
        const std::weak_ptr<void> life=scene_view_lifetime_;
        const auto revision=geometry_revision_;
        const auto current=[this,life,revision]{return !life.expired() && geometry_revision_==revision;};
        // DのGCは描画中の場面画像を保持する。C++では準備中に場面が終了し得る
        // ため、 SceneEntryに所有権付きのスナップショットを持たせる。
        entry.image->createImage();
        if(!current())return;
        const auto bounds = entry.image->bounds();
        auto draw_width = bounds.width;
        auto draw_height = bounds.height;
        // Dは拡縮不可の素材を、モデルの範囲が変わっても元の論理寸法で描く。
        if (!entry.image->dataResizable()) {
            const auto native_size = entry.image->imageData().size();
            if (native_size.width > 0.0F) draw_width = native_size.width;
            if (native_size.height > 0.0F) draw_height = native_size.height;
        }
        const lib::Rect destination{bounds.x * scene_zoom_, bounds.y * scene_zoom_,
                                    draw_width * scene_zoom_, draw_height * scene_zoom_};
        const auto resource = entry.image->resource();
        const auto& data = entry.image->imageData().data;
        if (paint_image && resource != 0 && !data.frames.empty()) {
            const auto& frame = data.frames.front();
            drawing.draw_image(resource,
                               {0.0F, 0.0F, static_cast<float>(frame.pixel_width),
                                static_cast<float>(frame.pixel_height)},
                               destination, entry.image->smoothing()
                                                ? lib::Interpolation::Linear
                                                : lib::Interpolation::Nearest);
        } else if (paint_image && entry.kind == SceneEntryKind::Background) {
            paintFallbackCell(drawing, entry.index, destination,entry.image->textRasterCache());
        }
        if(!current())return;
        // 文字の前景・縁・縮小・アンチエイリアスは画像と一緒に用
        // 意し、 場面の拡大表示と画像書き出しで同じ規則を使う。
        const bool selected = entry.kind == SceneEntryKind::Card
            ? std::find(selected_cards_.begin(), selected_cards_.end(), entry.index) != selected_cards_.end()
            : entry.kind == SceneEntryKind::Background &&
              std::find(selected_backs_.begin(), selected_backs_.end(), entry.index) != selected_backs_.end();
        if (selected) {
            // FlexImageの選択フィルターは表示設定に限
            // る。 選択中の重ね表示だけを覆い、デコード済み
            // 画像・モデル・カードと背景の並び順は変えない。
            const auto* flexible =
                dynamic_cast<const areaviewutils::FlexImage*>(entry.image.get());
            if (flexible && flexible->hasSelectionFilter) {
                drawing.fill_rect(destination, {0, 0, 0, 72});
                if(!current())return;
            }
            const bool xor_line = prop_ && prop_->var().drawXORSelectionLine.value;
            if(!cellrender::draw_selection_rect(drawing,destination,xor_line,1,current))return;
            // DのImagePaneと同じ八つのサイズ変更ハ
            // ンドルを描く。 別の重ね表示用選択状態ではな
            // く、当たり判定と同じモデルの配置から求める。
            if (!read_only_ && !sceneEntryFixed(entry)) {
                const float size = 5.0F * scene_zoom_;
                const float half = size / 2.0F;
                const std::array<lib::Point, 8> points{{
                    {destination.x, destination.y},
                    {destination.x, destination.y + destination.height},
                    {destination.x + destination.width, destination.y},
                    {destination.x + destination.width, destination.y + destination.height},
                    {destination.x, destination.y + destination.height / 2.0F},
                    {destination.x + destination.width, destination.y + destination.height / 2.0F},
                    {destination.x + destination.width / 2.0F, destination.y},
                    {destination.x + destination.width / 2.0F, destination.y + destination.height},
                }};
                for (const auto point : points) {
                    const lib::Rect handle{point.x - half, point.y - half, size, size};
                    drawing.fill_rect(handle, {255, 255, 255, 255});
                    if(!current())return;
                    drawing.draw_rect(handle, {0, 96, 224, 255}, 1.0F,
                                      lib::StrokeStyle::Solid);
                    if(!current())return;
                }
            }
        }
    }

    // Dのimages.d:3145-3258は前面のピクセルを同じアルファの一つのグループへコピーし
    // 、まとめて一度合成する。 カードごとにsource-over合成すると、重なる部分が暗くなる。
    bool paintAlphaGroup(lib::DrawingContext& drawing,
                         const std::vector<SceneEntry>& entries,
                         std::size_t first, std::size_t last,
                         const lib::Rect scene,
                         std::vector<std::shared_ptr<lib::OwnedImageResource>>& uploads) const {
        const std::weak_ptr<void> life = scene_view_lifetime_;
        const auto revision = geometry_revision_;
        const auto current = [this, life, revision] {
            return !life.expired() && geometry_revision_ == revision;
        };
        const auto graphics = context_ && context_->services.graphics
            ? context_->services.graphics : prop_->images().services().graphics;
        if (!graphics || !std::isfinite(scene.width) || !std::isfinite(scene.height) ||
            scene.width <= 0 || scene.height <= 0 ||
            static_cast<double>(scene.width) >= std::numeric_limits<int>::max() ||
            static_cast<double>(scene.height) >= std::numeric_limits<int>::max()) return false;
        const auto width = static_cast<std::uint32_t>(std::ceil(scene.width));
        const auto height = static_cast<std::uint32_t>(std::ceil(scene.height));
        if (!width || !height || std::uint64_t(width)*height > std::numeric_limits<std::size_t>::max()/4) return false;
        lib::ImageFrame output;
        output.pixel_width=width; output.pixel_height=height;
        output.logical_size={scene.width,scene.height};
        output.pixel_format=lib::PixelFormat::RGBA32;
        output.pixels.resize(std::size_t(width)*height*4);
        for (auto i=first;i<last;++i) {
            const auto image=entries[i].image;
            image->createImage();
            if (!current()) return false;
            auto data=image->imageData().data;
            if (data.frames.empty()) continue;
            // DのsetPixels/setAlphasは、アルファキーのピクセ
            // ルも含め対象矩形を置き換える。 画像ごとの不透明度を累積させない。
            lib::set_image_alpha(data,static_cast<std::uint8_t>(image->alpha()));
            const auto& frame=data.frames.front();
            if (!frame.pixel_width || !frame.pixel_height ||
                (frame.pixel_format!=lib::PixelFormat::RGBA32 && frame.pixel_format!=lib::PixelFormat::BGRA32) ||
                frame.pixels.size()<std::uint64_t(frame.pixel_width)*frame.pixel_height*4) continue;
            const auto bounds=image->bounds();
            const float x0=bounds.x*scene_zoom_, y0=bounds.y*scene_zoom_;
            const float w=bounds.width*scene_zoom_, h=bounds.height*scene_zoom_;
            if (!std::isfinite(x0) || !std::isfinite(y0) || !std::isfinite(w) ||
                !std::isfinite(h) || w<=0 || h<=0) continue;
            const auto left=static_cast<int>(std::clamp(std::floor(double(x0)),0.0,double(width)));
            const auto top=static_cast<int>(std::clamp(std::floor(double(y0)),0.0,double(height)));
            const auto right=static_cast<int>(std::clamp(std::ceil(double(x0)+w),0.0,double(width)));
            const auto bottom=static_cast<int>(std::clamp(std::ceil(double(y0)+h),0.0,double(height)));
            const bool bgr=frame.pixel_format==lib::PixelFormat::BGRA32;
            for(int y=top;y<bottom;++y) for(int x=left;x<right;++x) {
                const auto sx=std::min(frame.pixel_width-1,static_cast<std::uint32_t>(std::max(0.0F,(x-x0)*frame.pixel_width/w)));
                const auto sy=std::min(frame.pixel_height-1,static_cast<std::uint32_t>(std::max(0.0F,(y-y0)*frame.pixel_height/h)));
                const auto from=(std::size_t(sy)*frame.pixel_width+sx)*4;
                const auto to=(std::size_t(y)*width+x)*4;
                output.pixels[to]=frame.pixels[from+(bgr?2:0)];
                output.pixels[to+1]=frame.pixels[from+1];
                output.pixels[to+2]=frame.pixels[from+(bgr?0:2)];
                output.pixels[to+3]=frame.pixels[from+3];
            }
        }
        lib::ImageData data; data.frames.push_back(std::move(output));
        auto upload=std::make_shared<lib::OwnedImageResource>(graphics);
        upload->id=graphics->create_image(data);
        if (!current() || !upload->id) return false;
        uploads.push_back(upload);
        drawing.draw_image(upload->id,{0,0,float(width),float(height)},scene,lib::Interpolation::Nearest);
        return current();
    }

    void paintScene(lib::DrawingContext& drawing, lib::Rect) const {
        const std::weak_ptr<void> life=scene_view_lifetime_;
        const auto revision=geometry_revision_;
        const auto current=[this,life,revision]{return !life.expired() && geometry_revision_==revision;};
        const auto view = prop_->looks().viewSize();
        const float zoom=scene_zoom_;
        const lib::Rect scene{0.0F, 0.0F, static_cast<float>(view.width)*zoom,
                              static_cast<float>(view.height)*zoom};
        const auto component=[](std::int32_t v){return static_cast<std::uint8_t>(std::clamp(v,0,255));};
        const lib::Color wall{component(prop_->var().wallColorR.value),component(prop_->var().wallColorG.value),component(prop_->var().wallColorB.value),255};
        const bool grid=show_grid_,range_selecting=scene_range_selecting_;
        const bool xor_line=prop_->var().drawXORSelectionLine.value;
        const auto grid_color=toColor(prop_->var().gridColor.value);
        const auto gx=std::max(1U,grid_x_),gy=std::max(1U,grid_y_);
        const lib::Rect range{std::min(scene_range_start_.x,scene_range_end_.x),
            std::min(scene_range_start_.y,scene_range_end_.y),
            std::abs(scene_range_start_.x-scene_range_end_.x),
            std::abs(scene_range_start_.y-scene_range_end_.y)};
        const auto entries=sceneEntries();
        std::vector<int> grid_highlight_x, grid_highlight_y;
        auto grid_highlight_color = toColor(prop_->var().gridHighlightColor.value);
        grid_highlight_color.alpha = 255;
        if (grid && !lib::editor_window_host_options().all_mode &&
            scene_dragging_ && scene_drag_moved_) {
            // DのredrawGridは、移動・拡縮中の選択でグリッドに一致
            // する辺を示す。 現在のドラッグ配置から状態を取得し、解放・キャ
            // ンセルでscene_dragging_を終えて一時線を消す。
            for (const auto& entry : entries) {
                const bool selected = entry.kind == SceneEntryKind::Card
                    ? std::find(selected_cards_.begin(), selected_cards_.end(), entry.index) != selected_cards_.end()
                    : entry.kind == SceneEntryKind::Background &&
                      std::find(selected_backs_.begin(), selected_backs_.end(), entry.index) != selected_backs_.end();
                if (!selected || !entry.image || !sceneMemberMovable(entry.kind, entry.index)) continue;
                const auto bounds = entry.image->bounds();
                const auto add = [](std::vector<int>& lines, float edge, std::uint32_t step) {
                    const int coordinate = static_cast<int>(edge);
                    if (step > 1U && static_cast<float>(coordinate) == edge &&
                        coordinate == sceneToGrid(coordinate, step)) lines.push_back(coordinate);
                };
                add(grid_highlight_x, bounds.x, gx);
                add(grid_highlight_x, bounds.x + bounds.width, gx);
                add(grid_highlight_y, bounds.y, gy);
                add(grid_highlight_y, bounds.y + bounds.height, gy);
            }
            const auto unique = [](std::vector<int>& lines) {
                std::sort(lines.begin(), lines.end());
                lines.erase(std::unique(lines.begin(), lines.end()), lines.end());
            };
            unique(grid_highlight_x); unique(grid_highlight_y);
        }
        // 外部の描画処理を呼ぶ前に、描画設定と所有者を取得する。 DのGCと
        // 異なり、C++の単独所有の場面はそこで終了・編集される場合がある。
        drawing.clear({240,240,240,255});
        if(!current())return;
        drawing.save();
        struct Restore{lib::DrawingContext& drawing;bool active{true};~Restore(){if(active)drawing.restore();}}restore{drawing};
        if(!current())return;
        drawing.clip(scene);if(!current())return;
        drawing.fill_rect(scene,wall);if(!current())return;
        if (!lib::editor_window_host_options().all_mode && context_ &&
            context_->paint_wallpaper) {
            const auto painter = context_->paint_wallpaper;
            painter(drawing,scene);
            if(!current())return;
        }
        std::vector<std::shared_ptr<lib::OwnedImageResource>> group_uploads;
        const auto old_uploads = scene_alpha_uploads_;
        const auto groupable=[this](const SceneEntry& entry) {
            if (!entry.image || entry.image->alpha() < 0 || entry.image->alpha() >= 255)
                return false;
            // 共通のPileImageはラスタデータを持ち、DのImageType
            // タグは持たない。 文字・色の背景は、それぞれの描画規則を維持する。
            if constexpr (UseBacks) {
                if (entry.kind == SceneEntryKind::Background) {
                    const auto cell = backPtr(entry.index);
                    return cell && !dynamic_cast<const background::TextCell*>(cell.get()) &&
                           !dynamic_cast<const background::ColorCell*>(cell.get());
                }
            }
            return true;
        };
        for(std::size_t i=0;i<entries.size();) {
            auto end=i+1;
            if(groupable(entries[i])) {
                while(end<entries.size() && groupable(entries[end]) &&
                      !entries[end-1].image->separator() && !entries[end].image->separator() &&
                      entries[end].image->alpha()==entries[i].image->alpha()) ++end;
                const bool grouped=paintAlphaGroup(drawing,entries,i,end,scene,group_uploads);
                if(!current())return;
                for(auto j=i;j<end;++j){paintSceneEntry(drawing,entries[j],!grouped);if(!current())return;}
            } else {paintSceneEntry(drawing,entries[i]);if(!current())return;}
            i=end;
        }
        scene_alpha_uploads_=std::move(group_uploads);
        // Dと同じく、looks.viewSize内で全場面要素より手前にグリッドを描く。
        if(grid){
            for(std::uint32_t x=gx;gx>1 && x<view.width;x+=gx){
                drawing.draw_line({x*zoom,0},{x*zoom,scene.height},grid_color,1,lib::StrokeStyle::Dot);
                if(!current())return;
            }
            for(std::uint32_t y=gy;gy>1 && y<view.height;y+=gy){
                drawing.draw_line({0,y*zoom},{scene.width,y*zoom},grid_color,1,lib::StrokeStyle::Dot);
                if(!current())return;
            }
        }
        for (const auto x : grid_highlight_x) {
            drawing.draw_line({x*zoom,0},{x*zoom,scene.height},grid_highlight_color,1,lib::StrokeStyle::Solid);
            if (!current()) return;
        }
        for (const auto y : grid_highlight_y) {
            drawing.draw_line({0,y*zoom},{scene.width,y*zoom},grid_highlight_color,1,lib::StrokeStyle::Solid);
            if (!current()) return;
        }
        if(range_selecting&&!cellrender::draw_selection_rect(drawing,range,xor_line,1,current))return;
        restore.active=false;drawing.restore();if(!current())return;
        drawing.draw_rect(scene,{128,128,128,255},1,lib::StrokeStyle::Solid);
    }

    void updateSceneRangeSelection() {
        const bool toggle = (static_cast<std::uint32_t>(scene_press_modifiers_) &
                            static_cast<std::uint32_t>(lib::Modifiers::Control)) != 0U;
        const bool extend = (static_cast<std::uint32_t>(scene_press_modifiers_) &
                            static_cast<std::uint32_t>(lib::Modifiers::Shift)) != 0U;
        const auto start = sceneWorldPoint(scene_range_start_);
        const auto end = sceneWorldPoint(scene_range_end_);
        const lib::Rect range{std::min(start.x, end.x), std::min(start.y, end.y),
                              std::abs(start.x - end.x), std::abs(start.y - end.y)};
        // Ctrlは常に押下開始時の選択から排他的論理和を再計算する。 Shift
        // は選択を累積し、通常選択は対象となる可視行だけを変える。
        auto cards = toggle ? scene_range_cards_ : selected_cards_;
        auto backs = toggle ? scene_range_backs_ : selected_backs_;
        const auto view_size = prop_->looks().viewSize();
        const auto is_full_background = [&view_size](const SceneEntry& entry) {
            const auto bounds = entry.image ? entry.image->bounds() : lib::Rect{};
            return entry.kind == SceneEntryKind::Background && bounds.x == 0.0F &&
                   bounds.y == 0.0F && bounds.width == static_cast<float>(view_size.width) &&
                   bounds.height == static_cast<float>(view_size.height);
        };
        const auto intersects = [&range](const lib::Rect bounds) {
            return bounds.x < range.x + range.width && range.x < bounds.x + bounds.width &&
                   bounds.y < range.y + range.height && range.y < bounds.y + bounds.height;
        };
        for (const auto& entry : sceneEntries()) {
            if (!entry.image || entry.kind == SceneEntryKind::Decoration ||
                (prop_->var().ignoreBackgroundInRange.value && is_full_background(entry)))
                continue;
            auto& values = entry.kind == SceneEntryKind::Card ? cards : backs;
            const auto found = std::find(values.begin(), values.end(), entry.index);
            const bool inside = intersects(entry.image->bounds());
            const bool selected = found != values.end();
            const bool desired = toggle ? selected != inside : inside || (extend && selected);
            if (desired && !selected) values.push_back(entry.index);
            else if (!desired && selected) values.erase(found);
        }
        selected_cards_ = std::move(cards);
        selected_backs_ = std::move(backs);
        // DのImagePaneの範囲選択と同じく、select/deselect経由で一覧を更新する。
        refreshSelected(true, true);
    }

    [[nodiscard]] lib::Point sceneCanvasPosition(const lib::Point position) const noexcept {
        if (!canvas_) return position;
        const auto bounds = canvas_->bounds();
        return {position.x - bounds.x, position.y - bounds.y};
    }

    void handleScenePointer(lib::PointerEvent& event) {
        const std::weak_ptr<void> life=scene_view_lifetime_;
        const auto revision=geometry_revision_;
        const auto canvas_position = sceneCanvasPosition(event.position);
        if (scene_range_selecting_) {
            if (event.action == lib::PointerAction::Move) {
                scene_range_end_ = canvas_position;
                scene_range_moved_ = true;
                updateSceneRangeSelection();
                event.handled = true;
                return;
            }
            if (event.action == lib::PointerAction::Up ||
                event.action == lib::PointerAction::Cancel) {
                scene_range_selecting_ = false;
                const bool hit_click = event.action == lib::PointerAction::Up &&
                    event.button == lib::PointerButton::Primary && !scene_range_moved_ &&
                    scene_range_target_kind_ != SceneEntryKind::Decoration;
                if (hit_click) {
                    finishSceneClick(scene_range_target_kind_,scene_range_target_index_);
                    refreshSelected(true, true);
                    if(life.expired() || geometry_revision_!=revision)return;
                }
                scene_range_target_kind_=SceneEntryKind::Decoration;
                requestSceneRedraw();
                // 共通MouseInputは2回目のUpでクリックの
                // 組を完成させる。 固定・読み取り専用の選択クリック
                // も、移動量0の配置操作と同様にその経路へ渡す。 実
                // 際の範囲ドラッグとキャンセルは、ここで消費する。
                event.handled = !hit_click;
                return;
            }
        }
        if (scene_dragging_) {
            // DのImagePaneは副ボタンの押下で保留中の配置を取り消し
            // 、 解放時には通常のコンテキストメニューを開けるようにする。
            if (event.button == lib::PointerButton::Secondary &&
                event.action == lib::PointerAction::Down) {
                finishSceneDrag(false);
                event.handled = true;
                return;
            }
            if (event.action == lib::PointerAction::Move) {
                updateSceneDrag(canvas_position, event.modifiers);
                event.handled = true;
                return;
            }
            if (event.action == lib::PointerAction::Up ||
                event.action == lib::PointerAction::Cancel) {
                const bool cancelled = event.action == lib::PointerAction::Cancel;
                // DのImagePane.FocusLostと同じく、すべてのホストで保留配置
                // を確定する。 共通のネイティブ処理はCanvasのフォーカス喪失前にWindowDeactivated
                // のCancelを送るため、 ドラッグ状態が消える前のここで確定す
                // る。明示的なCancel/Escapeは巻き戻しのままにする。
                const bool commit_deactivated = cancelled &&
                    event.cancel_reason == lib::PointerCancelReason::WindowDeactivated;
                finishSceneDrag(!cancelled || commit_deactivated);
                // クリックの組の認識は共通MouseInputが行う。移
                // 動量0のドラッグを終える主ボタンUpは その処理へ通し
                // 、合成されたDoubleClickがImagePane
                // へ届くようにする。 実際のドラッグはMoveでクリッ
                // ク列を取り消しており、Cancelはここで消費する。
                event.handled = cancelled;
                return;
            }
        }
        if (event.action == lib::PointerAction::Move && !scene_dragging_ &&
            !scene_range_selecting_) {
            // DのImagePane.MMListenerと同じく、ドラッ
            // グ中でなければ位置下のハンドルから SIZENWSE/SIZENESW/SIZEWE/SIZENS
            // を選ぶ。セルが他のセルに隠れていても拡縮を優先す
            // る。 MOVE/NONEは通常カーソルを使い、選
            // んだカーソルはSWTと同じくドラッグ中も保つ。
            updateSceneHoverCursor(canvas_position);
        } else if (event.action == lib::PointerAction::Leave && !scene_dragging_) {
            canvas_->set_cursor(lib::CursorKind::Default);
        }
        const bool primary = event.button == lib::PointerButton::Primary &&
            (event.action == lib::PointerAction::Down ||
             event.action == lib::PointerAction::DoubleClick);
        const bool secondary = event.button == lib::PointerButton::Secondary &&
            event.action == lib::PointerAction::Up;
        const bool middle = event.button == lib::PointerButton::Middle &&
            event.action == lib::PointerAction::Down;
        if (!primary && !secondary && !middle) return;
        const auto modifier_bits = static_cast<std::uint32_t>(event.modifiers);
        const bool toggle = (modifier_bits & static_cast<std::uint32_t>(lib::Modifiers::Control)) != 0U;
        const bool extend = toggle ||
            (modifier_bits & static_cast<std::uint32_t>(lib::Modifiers::Shift)) != 0U;
        const auto point = sceneWorldPoint(canvas_position);
        auto entries = sceneEntries();
        if (middle) {
            cycleSceneSelectionAt(entries, point, extend);
            event.handled = true;
            return;
        }
        auto hit = entries.rend();
        auto hit_handle = SceneDragHandle::Move;
        const bool alt = (modifier_bits & static_cast<std::uint32_t>(lib::Modifiers::Alt)) != 0U;
        if (!primary || !alt) {
            if (primary) {
                for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
                    if (!it->image || sceneEntryFixed(*it)) continue;
                    const auto handle = sceneDragHandle(*it, point);
                    if (handle != SceneDragHandle::Move) { hit = it; hit_handle = handle; break; }
                }
            }
            if (hit == entries.rend()) {
                for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
                    if (!it->image || it->kind == SceneEntryKind::Decoration) continue;
                    const auto bounds = it->image->bounds();
                    if (point.x < bounds.x || point.y < bounds.y ||
                        point.x >= bounds.x + bounds.width || point.y >= bounds.y + bounds.height) continue;
                    if (hit == entries.rend()) hit = it;
                    if (!primary || !sceneEntryFixed(*it)) { hit = it; break; }
                }
            }
        }
        if (primary && event.action == lib::PointerAction::Down) {
            scene_press_modifiers_ = event.modifiers;
            scene_range_cards_ = selected_cards_;
            scene_range_backs_ = selected_backs_;
            scene_range_moved_ = false;
            scene_range_target_kind_ = hit==entries.rend() ? SceneEntryKind::Decoration : hit->kind;
            scene_range_target_index_ = hit==entries.rend() ? 0U : hit->index;
        }
        const bool fixed_hit = primary && hit!=entries.rend() && (read_only_ || sceneEntryFixed(*hit));
        if (hit == entries.rend()) {
            // Dのコンテキストクリックは選択を残して場面メニューを開く。
            // 空のキャンバスへの主ボタンクリックは、選択を解除する。
            if (primary && !extend) {
                selected_cards_.clear();
                selected_backs_.clear();
            }
            if (event.action == lib::PointerAction::Down) {
                // ImagePaneは空白での主ボタン押下から
                // 選択矩形を作り、移動中に選択を更新する。 別
                // の画像選択状態を持たず、SceneViewの
                // モデルが所有する重ね表示へ同じ処理を行う。
                scene_range_selecting_ = true;
                scene_range_start_ = canvas_position;
                scene_range_end_ = canvas_position;
            }
        } else if (hit->kind == SceneEntryKind::Card) {
            const auto selected = std::find(selected_cards_.begin(), selected_cards_.end(),
                                            hit->index);
            if (secondary) {
                if (selected == selected_cards_.end()) {
                    selected_cards_ = {hit->index};
                    selected_backs_.clear();
                }
            } else if (extend) {
                // 修飾キー付きクリックの選択切り替えは、移動しなかったUpでだけ確定する。
            } else if (fixed_hit || selected == selected_cards_.end()) {
                selected_cards_ = {hit->index};
                selected_backs_.clear();
            }
        } else {
            const auto selected = std::find(selected_backs_.begin(), selected_backs_.end(),
                                            hit->index);
            if (secondary) {
                if (selected == selected_backs_.end()) {
                    selected_backs_ = {hit->index};
                    selected_cards_.clear();
                }
            } else if (extend) {
                // 操作が終わるまで、選択されたドラッグ対象の組を保つ。
            } else if (fixed_hit || selected == selected_backs_.end()) {
                selected_backs_ = {hit->index};
                selected_cards_.clear();
            }
        }
        if (event.action == lib::PointerAction::Down && fixed_hit) {
            scene_range_selecting_=true;
            scene_range_start_=canvas_position;
            scene_range_end_=canvas_position;
        }
        // キャンバスからの入力も、DのTable.select/deselectの経路で行を変更する。
        refreshSelected(true, true);
        if(life.expired() || geometry_revision_!=revision)return;
        if (event.action == lib::PointerAction::Down && hit != entries.rend() &&
            !read_only_ && !sceneEntryFixed(*hit))
            beginSceneDrag(canvas_position, hit_handle, *hit);
        // Dは選択画像の内側だけを編集対象にする。 外側の拡縮ハン
        // ドルはドラッグできるが、画像設定を開く対象にはしない。
        bool edit_hit = hit != entries.rend();
        if (edit_hit && !lib::editor_window_host_options().all_mode) {
            const auto bounds = hit->image->bounds();
            edit_hit = point.x >= bounds.x && point.y >= bounds.y &&
                point.x < bounds.x + bounds.width && point.y < bounds.y + bounds.height;
        }
        if (event.action == lib::PointerAction::DoubleClick) {
            // 共通MouseInputは2回目の通常Downの直後にDoubleClick
            // を送る。Downは移動の準備を始めるため、 そのままでは
            // canEdit()がドラッグ中として編集を拒否してしまう
            // 。 DのIPEditListenerと同じく、ポインター
            // 下の最前面の選択画像だけを findSelectedIndex
            // で選び、重なっている未選択画像は対象にしない。
            auto edit_entry = entries.rend();
            for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
                if (!it->image || it->kind == SceneEntryKind::Decoration) continue;
                const auto& selection = it->kind == SceneEntryKind::Card
                    ? selected_cards_ : selected_backs_;
                if (std::find(selection.begin(), selection.end(), it->index) ==
                    selection.end()) continue;
                const auto bounds = it->image->bounds();
                if (point.x < bounds.x || point.y < bounds.y ||
                    point.x >= bounds.x + bounds.width ||
                    point.y >= bounds.y + bounds.height) continue;
                edit_entry = it;
                break;
            }
            if (scene_dragging_) {
                const auto before=geometry_revision_;
                finishSceneDrag(false);
                if(life.expired() || geometry_revision_!=before+1)return;
            }
            if (edit_entry != entries.rend())
                editTarget(edit_entry->kind, edit_entry->index);
            if(life.expired())return;
        }
        if (secondary) showSceneMenu(canvas_, event.position);
        // 場面側では対象を選択済みで、移動可能なら配置ドラッグの準備も済んで
        // いる。 共通MouseInputに通常の主ボタンDownを保持させ
        // 、2回目をDoubleClickへ変換できるようにする。 場面側は
        // 引き続きドラッグのMove/Upと全ポップアップ操作を消費する。
        event.handled = event.action != lib::PointerAction::Down;
    }

    void cycleSceneSelectionAt(const std::vector<SceneEntry>& entries,
                               const lib::Point point, const bool extend) {
        std::vector<const SceneEntry*> hits;
        for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
            if (!it->image || it->kind == SceneEntryKind::Decoration) continue;
            const auto bounds = it->image->bounds();
            if (point.x >= bounds.x && point.y >= bounds.y &&
                point.x < bounds.x + bounds.width && point.y < bounds.y + bounds.height)
                hits.push_back(&*it);
        }
        if (hits.empty()) return;
        const auto selected = [this](const SceneEntry& entry) {
            const auto& values = entry.kind == SceneEntryKind::Card
                ? selected_cards_ : selected_backs_;
            return std::find(values.begin(), values.end(), entry.index) != values.end();
        };
        auto next = hits.front();
        const auto selected_hits = std::count_if(hits.begin(), hits.end(),
            [&](const SceneEntry* entry) { return selected(*entry); });
        const bool cycle = selected_hits == 1 && hits.size() > 1U &&
            (selected_cards_.size() + selected_backs_.size() == 1U || extend);
        if (cycle) {
            const auto current = std::find_if(hits.begin(), hits.end(),
                [&](const SceneEntry* entry) { return selected(*entry); });
            next = hits[(static_cast<std::size_t>(std::distance(hits.begin(), current)) + 1U) % hits.size()];
            // DのchangeSelectは、Ctrl/Shift付きでも重な
            // り内の一つの選択画像を置き換える。 他の場所の選択は変えない。
            auto& old = (*current)->kind == SceneEntryKind::Card
                ? selected_cards_ : selected_backs_;
            std::erase(old, (*current)->index);
        } else if (!extend) {
            selected_cards_.clear();
            selected_backs_.clear();
        }
        auto& values = next->kind == SceneEntryKind::Card
            ? selected_cards_ : selected_backs_;
        if (std::find(values.begin(), values.end(), next->index) == values.end())
            values.push_back(next->index);
        // DのchangeSelectと同じく、Table.setSelectionを使わず画像行を再選択する。
        refreshSelected(true, true);
    }

    struct CardGeometry;
    struct BackGeometry;

    void handleSceneKey(lib::KeyEvent& event) {
        const std::weak_ptr<void> life=scene_view_lifetime_;
        const auto revision=geometry_revision_;
        const bool arrow = event.key == lib::Key::Up || event.key == lib::Key::Right ||
                           event.key == lib::Key::Down || event.key == lib::Key::Left;
        if (!arrow) {
            if ((event.key == lib::Key::Enter || event.key == lib::Key::Space) &&
                (scene_keyboard_editing_ || sceneKeyboardHasSelectedDraft())) {
                finishSceneKeyboardEdit(true);
                event.handled = true;
            } else if (event.key == lib::Key::Escape &&
                       (scene_keyboard_editing_ || sceneKeyboardHasSelectedDraft())) {
                finishSceneKeyboardEdit(false);
                event.handled = true;
            }
            // DのTraverseは矢印によるフォーカス
            // 移動だけを抑止する。 Tabは通常の移動と
            // して残し、場面キャンバスでは消費しない。
            return;
        }
        if (read_only_ || (!sceneSelectionMovable() && !scene_keyboard_editing_)) return;
        if (!scene_keyboard_editing_) {
            ++scene_keyboard_revision_;
            scene_keyboard_editing_ = true;
        }
        // キーボードのプレビュー中にも、モデル順や他のビューの配置が変わる
        // 場合がある。 矢印操作のたびに対象の同一性で基準を取り直し、古
        // い行番号で別の対象を動かしたり新しい配置を上書きしたりしない。
        rebaseSceneKeyboardDrafts();
        const auto bits = static_cast<std::uint32_t>(event.modifiers);
        const bool shift = (bits & static_cast<std::uint32_t>(lib::Modifiers::Shift)) != 0U;
        const bool high = (bits & static_cast<std::uint32_t>(lib::Modifiers::Control)) != 0U &&
                          (bits & static_cast<std::uint32_t>(lib::Modifiers::Alt)) != 0U;
        const int amount = high ? static_cast<int>(
            prop_->var().highValueOfImageControl.value) : 1;
        const int dx = event.key == lib::Key::Left ? -amount
                     : event.key == lib::Key::Right ? amount : 0;
        const int dy = event.key == lib::Key::Up ? -amount
                     : event.key == lib::Key::Down ? amount : 0;
        if constexpr (UseCards) for (const auto index : selected_cards_) {
            if (index >= cardCount() || cardIsFixed(index)) continue;
            if (index >= scene_keyboard_preview_cards_.size()) continue;
            auto& value = scene_keyboard_preview_cards_[index];
            if (shift) {
                const auto minimum = static_cast<std::int64_t>(std::max(1,
                    prop_->var().cardScaleMin.value));
                const auto maximum = std::max(minimum, static_cast<std::int64_t>(
                    prop_->var().cardScaleMax.value));
                value.scale = static_cast<std::uint32_t>(std::clamp(
                    static_cast<std::int64_t>(value.scale) + dx + dy,
                    minimum, maximum));
            }
            else { value.x += dx; value.y += dy; }
            if (index < card_images_.size() && card_images_[index]) {
                card_images_[index]->x(value.x);
                card_images_[index]->y(value.y);
                card_images_[index]->scale(value.scale);
            }
        }
        if constexpr (UseBacks) for (const auto index : selected_backs_) {
            if (index >= backCount() || backIsFixed(index)) continue;
            if (index >= scene_keyboard_preview_backs_.size()) continue;
            auto& value = scene_keyboard_preview_backs_[index];
            if (shift) {
                if (dx) value.width = static_cast<int>(std::clamp<std::int64_t>(
                    static_cast<std::int64_t>(value.width) + dx, 0, 65536));
                if (dy) value.height = static_cast<int>(std::clamp<std::int64_t>(
                    static_cast<std::int64_t>(value.height) + dy, 0, 65536));
            } else { value.x += dx; value.y += dy; }
            if (index < back_images_.size() && back_images_[index]) {
                back_images_[index]->x(value.x);
                back_images_[index]->y(value.y);
                back_images_[index]->width(value.width);
                back_images_[index]->height(value.height);
            }
        }
        // Dのキーボード移動と同じく、このビューの保留中のFlexImage配置だけを描き直す。
        requestSceneRedraw();
        if(life.expired() || geometry_revision_!=revision)return;
        event.handled = true;
    }

    [[nodiscard]] bool sceneKeyboardHasSelectedDraft() const {
        const auto pending = [](const auto& before, const auto& preview,
                                const auto& target) {
            const auto start = std::find_if(before.begin(), before.end(),
                [&](const auto& value) { return value.target == target; });
            const auto draft = std::find_if(preview.begin(), preview.end(),
                [&](const auto& value) { return value.target == target; });
            return start != before.end() && draft != preview.end() &&
                   !(*start == *draft);
        };
        if constexpr (UseCards) for (const auto index : selected_cards_)
            if (index < cardCount() && pending(scene_keyboard_cards_before_,
                    scene_keyboard_preview_cards_, areaCardPtr(index))) return true;
        if constexpr (UseBacks) for (const auto index : selected_backs_)
            if (index < backCount() && pending(scene_keyboard_backs_before_,
                    scene_keyboard_preview_backs_, backPtr(index))) return true;
        return false;
    }

    void rebaseSceneKeyboardDrafts() {
        const auto live_cards = captureCardGeometry();
        const auto live_backs = captureBackGeometry();
        const auto old_before_cards = scene_keyboard_cards_before_;
        const auto old_preview_cards = scene_keyboard_preview_cards_;
        const auto old_before_backs = scene_keyboard_backs_before_;
        const auto old_preview_backs = scene_keyboard_preview_backs_;
        scene_keyboard_cards_before_ = live_cards;
        scene_keyboard_backs_before_ = live_backs;
        // DはdragImgsから外れたFlexImageのnewBounds
        // も保持する。 モデルの同一性に基づいて作業値を引き継
        // ぎ、次の選択対象群の開始値は新たにモデルから読む。
        scene_keyboard_preview_cards_ = live_cards;
        scene_keyboard_preview_backs_ = live_backs;
        const auto carry_drafts = [](const auto& old_before, const auto& old_preview,
                                     const auto& live, auto& preview) {
            for (auto& value : preview) {
                const auto start = std::find_if(old_before.begin(), old_before.end(),
                    [&](const auto& old) { return old.target == value.target; });
                const auto draft = std::find_if(old_preview.begin(), old_preview.end(),
                    [&](const auto& old) { return old.target == value.target; });
                if (start == old_before.end() || draft == old_preview.end() ||
                    *start == *draft) continue;
                const auto current = std::find_if(live.begin(), live.end(),
                    [&](const auto& old) { return old.target == value.target; });
                if (current == live.end() || !(*start == *current)) continue;
                value = *draft;
            }
        };
        carry_drafts(old_before_cards, old_preview_cards, live_cards,
                     scene_keyboard_preview_cards_);
        carry_drafts(old_before_backs, old_preview_backs, live_backs,
                     scene_keyboard_preview_backs_);
    }

    void discardSceneKeyboardDrafts() {
        scene_keyboard_editing_ = false;
        scene_keyboard_cards_before_.clear();
        scene_keyboard_backs_before_.clear();
        scene_keyboard_preview_cards_.clear();
        scene_keyboard_preview_backs_.clear();
        ++scene_keyboard_revision_;
    }

    void restoreSceneKeyboardDraftImages() {
        bool restored_preview = false;
        if constexpr (UseCards) for (std::size_t index = 0; index < cardCount(); ++index) {
            if (index >= card_images_.size() || !card_images_[index]) continue;
            const auto target = areaCardPtr(index);
            const auto start = std::find_if(scene_keyboard_cards_before_.begin(),
                scene_keyboard_cards_before_.end(),
                [&](const auto& value) { return value.target == target; });
            const auto draft = std::find_if(scene_keyboard_preview_cards_.begin(),
                scene_keyboard_preview_cards_.end(),
                [&](const auto& value) { return value.target == target; });
            if (start == scene_keyboard_cards_before_.end() ||
                draft == scene_keyboard_preview_cards_.end() || *start == *draft) continue;
            card_images_[index]->x(draft->x);
            card_images_[index]->y(draft->y);
            card_images_[index]->scale(draft->scale);
            restored_preview = true;
        }
        if constexpr (UseBacks) for (std::size_t index = 0; index < backCount(); ++index) {
            if (index >= back_images_.size() || !back_images_[index]) continue;
            const auto target = backPtr(index);
            const auto start = std::find_if(scene_keyboard_backs_before_.begin(),
                scene_keyboard_backs_before_.end(),
                [&](const auto& value) { return value.target == target; });
            const auto draft = std::find_if(scene_keyboard_preview_backs_.begin(),
                scene_keyboard_preview_backs_.end(),
                [&](const auto& value) { return value.target == target; });
            if (start == scene_keyboard_backs_before_.end() ||
                draft == scene_keyboard_preview_backs_.end() || *start == *draft) continue;
            back_images_[index]->x(draft->x);
            back_images_[index]->y(draft->y);
            back_images_[index]->width(draft->width);
            back_images_[index]->height(draft->height);
            restored_preview = true;
        }
        if (restored_preview) requestSceneRedraw();
    }

    void retainUnselectedSceneKeyboardDrafts(
        const std::vector<CardGeometry>& old_before_cards,
        const std::vector<CardGeometry>& old_preview_cards,
        const std::vector<BackGeometry>& old_before_backs,
        const std::vector<BackGeometry>& old_preview_backs,
        const std::vector<std::shared_ptr<C>>& selected_card_targets,
        const std::vector<std::shared_ptr<BgImage>>& selected_back_targets) {
        scene_keyboard_cards_before_ = captureCardGeometry();
        scene_keyboard_backs_before_ = captureBackGeometry();
        scene_keyboard_preview_cards_ = scene_keyboard_cards_before_;
        scene_keyboard_preview_backs_ = scene_keyboard_backs_before_;
        bool restored_preview = false;
        if constexpr (UseCards) for (std::size_t index = 0;
                              index < scene_keyboard_preview_cards_.size(); ++index) {
            auto& value = scene_keyboard_preview_cards_[index];
            if (std::find(selected_card_targets.begin(), selected_card_targets.end(),
                          value.target) != selected_card_targets.end()) continue;
            const auto start = std::find_if(old_before_cards.begin(), old_before_cards.end(),
                [&](const auto& old) { return old.target == value.target; });
            const auto draft = std::find_if(old_preview_cards.begin(), old_preview_cards.end(),
                [&](const auto& old) { return old.target == value.target; });
            if (start == old_before_cards.end() || draft == old_preview_cards.end() ||
                *start == *draft || !(*start == value)) continue;
            value = *draft;
            if (index < card_images_.size() && card_images_[index]) {
                card_images_[index]->x(value.x);
                card_images_[index]->y(value.y);
                card_images_[index]->scale(value.scale);
                restored_preview = true;
            }
        }
        if constexpr (UseBacks) for (std::size_t index = 0;
                              index < scene_keyboard_preview_backs_.size(); ++index) {
            auto& value = scene_keyboard_preview_backs_[index];
            if (std::find(selected_back_targets.begin(), selected_back_targets.end(),
                          value.target) != selected_back_targets.end()) continue;
            const auto start = std::find_if(old_before_backs.begin(), old_before_backs.end(),
                [&](const auto& old) { return old.target == value.target; });
            const auto draft = std::find_if(old_preview_backs.begin(), old_preview_backs.end(),
                [&](const auto& old) { return old.target == value.target; });
            if (start == old_before_backs.end() || draft == old_preview_backs.end() ||
                *start == *draft || !(*start == value)) continue;
            value = *draft;
            if (index < back_images_.size() && back_images_[index]) {
                back_images_[index]->x(value.x);
                back_images_[index]->y(value.y);
                back_images_[index]->width(value.width);
                back_images_[index]->height(value.height);
                restored_preview = true;
            }
        }
        if (restored_preview) requestSceneRedraw();
    }

    void finishSceneKeyboardEdit(const bool commit) {
        if (!scene_keyboard_editing_ && !sceneKeyboardHasSelectedDraft()) return;
        const std::weak_ptr<void> life=scene_view_lifetime_;
        const auto keyboard_revision=++scene_keyboard_revision_;
        scene_keyboard_editing_ = false;
        const auto before_cards = scene_keyboard_cards_before_;
        const auto before_backs = scene_keyboard_backs_before_;
        const auto preview_cards = scene_keyboard_preview_cards_;
        const auto preview_backs = scene_keyboard_preview_backs_;
        const auto selected_cards = detail::normalizedSelection<C>(selected_cards_, cardCount());
        const auto selected_backs = detail::normalizedSelection<BgImage>(selected_backs_, backCount());
        std::vector<std::shared_ptr<C>> selected_card_targets;
        std::vector<std::shared_ptr<BgImage>> selected_back_targets;
        if constexpr (UseCards) for (const auto index : selected_cards)
            if (index < cardCount()) selected_card_targets.push_back(areaCardPtr(index));
        if constexpr (UseBacks) for (const auto index : selected_backs)
            if (index < backCount()) selected_back_targets.push_back(backPtr(index));
        std::vector<CardGeometry> cohort_before_cards, cohort_preview_cards;
        std::vector<BackGeometry> cohort_before_backs, cohort_preview_backs;
        const auto collect_cohort = [](const auto& before, const auto& preview,
                                       const auto& targets, auto& cohort_before,
                                       auto& cohort_preview) {
            for (const auto& start : before) {
                if (std::find(targets.begin(), targets.end(), start.target) == targets.end())
                    continue;
                const auto draft = std::find_if(preview.begin(), preview.end(),
                    [&](const auto& value) { return value.target == start.target; });
                if (draft == preview.end()) continue;
                cohort_before.push_back(start);
                cohort_preview.push_back(*draft);
            }
        };
        collect_cohort(before_cards, preview_cards, selected_card_targets,
                       cohort_before_cards, cohort_preview_cards);
        collect_cohort(before_backs, preview_backs, selected_back_targets,
                       cohort_before_backs, cohort_preview_backs);
        if (!commit) {
            cancelSceneDragPreview(selected_cards, selected_backs);
        } else {
            commitSceneDragPreview(cohort_before_cards, cohort_before_backs,
                                   cohort_preview_cards, cohort_preview_backs,
                                   selected_cards, selected_backs,
                                   false, true);
        }
        if (life.expired() || scene_keyboard_revision_ != keyboard_revision) return;
        retainUnselectedSceneKeyboardDrafts(before_cards, preview_cards,
            before_backs, preview_backs, selected_card_targets, selected_back_targets);
    }

    [[nodiscard]] bool sceneSelectionMovable() const noexcept {
        if (read_only_) return false;
        if constexpr (UseCards) {
            for (const auto index : selected_cards_)
                if (!cardIsFixed(index)) return true;
        }
        if constexpr (UseBacks) {
            for (const auto index : selected_backs_)
                if (!backIsFixed(index)) return true;
        }
        return false;
    }

    void showSceneMenu(const lib::WidgetPtr& owner, const lib::Point position) {
        if (!menu_ || !owner || !context_ || !context_->services.gui) return;
        context_menu_target_ = ContextMenuTarget::Scene;
        if (menu_->on_open) menu_->on_open();
        context_->services.gui->show_context_menu(menu_, owner, position);
    }

    void showListMenu(const lib::WidgetPtr& owner, const lib::Point position,
                      const bool cards) {
        if (!menu_ || !owner || !context_ || !context_->services.gui) return;
        context_menu_target_ = cards ? ContextMenuTarget::Cards
                                     : ContextMenuTarget::Backs;
        if (menu_->on_open) menu_->on_open();
        context_->services.gui->show_context_menu(menu_, owner, position);
    }

    void rebuildSceneMenu() {
        if (!menu_) return;
        const auto make_item = [](std::string text, std::string command,
                                  bool enabled = true, bool checked = false) {
            lib::MenuItem item;
            item.text = std::move(text);
            item.command = std::move(command);
            item.enabled = enabled;
            item.checked = checked;
            return item;
        };
        const auto separator = [] {
            lib::MenuItem item;
            item.separator = true;
            return item;
        };
        const auto text = [this](const types::MenuID id) {
            // Dと同じく、設定されたアクセスキー（(&E)など）・サブメ
            // ニューの省略記号・ショートカットをメニューへ付ける。 メ
            // ニューを開いた後のキー入力で対応項目を選べるようにする。
            return prop_->buildMenu(id);
        };
        std::vector<lib::MenuItem> items;
        items.push_back(make_item(text(types::MenuID::EditProp), "edit", canEdit()));
        if (!read_only_) {
            items.push_back(separator());
            if constexpr (UseCards)
                items.push_back(make_item(text(std::is_same_v<C, EnemyCard>
                                                   ? types::MenuID::NewEnemyCard
                                                   : types::MenuID::NewMenuCard),
                                           "create_card", canCreateCard()));
            if constexpr (UseBacks) {
                items.push_back(make_item(text(types::MenuID::NewBack), "create_back"));
                items.push_back(make_item(text(types::MenuID::NewTextCell), "create_text"));
                items.push_back(make_item(text(types::MenuID::NewColorCell), "create_color"));
                items.push_back(make_item(text(types::MenuID::NewPCCell), "create_pc", canCreatePCCell()));
            }
        }
        items.push_back(separator());
        // Dの場面ポップアップの切り取りは、書き込み可能な選択を要求するcanDoT
        // で判定する。 転送メニューが見えるだけでは有効にしない。
        items.push_back(make_item(text(types::MenuID::Cut), "cut", canDoT()));
        items.push_back(make_item(text(types::MenuID::Copy), "copy", canDoC()));
        items.push_back(make_item(text(types::MenuID::Paste), "paste", canDoP()));
        items.push_back(make_item(text(types::MenuID::Delete), "delete", canDoD()));
        items.push_back(make_item(text(types::MenuID::Clone), "clone", canDoClone()));
        if constexpr (std::is_same_v<A, Area> || std::is_same_v<A, Battle>) {
            items.push_back(separator());
            items.push_back(make_item(text(types::MenuID::EditEvent), "open_event"));
        }
        if (summary_) {
            items.push_back(separator());
            items.push_back(make_item(text(types::MenuID::SelectConnectedResource),
                                      "connected", canSelectConnectedResource()));
        }
        items.push_back(separator());
        items.push_back(make_item(text(types::MenuID::ShowParty), "show_party", true,
                                  view_party_));
        items.push_back(make_item(text(types::MenuID::ShowMsg), "show_message", true,
                                  view_message_));
        if constexpr (!UseCards && UseBacks)
            items.push_back(make_item(text(types::MenuID::ShowRefCards),
                                      "show_reference_cards", true,
                                      show_ref_cards_));
        if constexpr (std::is_same_v<C, EnemyCard> || (!UseCards && UseBacks)) {
            if (summary_)
                items.push_back(make_item(text(types::MenuID::ShowEnemyCardProp),
                                          "show_debug", true, debug_mode_));
        }
        if (!read_only_) {
            items.push_back(separator());
            if constexpr (UseCards)
                items.push_back(make_item(text(types::MenuID::FixedCards),
                                          "fixed_cards", true, fixed_cards_));
            if constexpr (UseBacks) {
                items.push_back(make_item(text(types::MenuID::FixedCells),
                                          "fixed_cells", true, fixed_cells_));
                items.push_back(make_item(text(types::MenuID::FixedBackground),
                                          "fixed_background", true,
                                          fixed_background_));
            }
        }
        if constexpr (UseCards && UseBacks) {
            items.push_back(separator());
            items.push_back(make_item(text(types::MenuID::ShowCard), "show_cards", true,
                                      view_cards_));
            items.push_back(make_item(text(types::MenuID::ShowBack), "show_backs", true,
                                      view_backs_));
        }
        if constexpr (UseCards) {
            items.push_back(separator());
            auto auto_arrange = make_item(text(types::MenuID::AutoArrange), "auto",
                                          !read_only_, !spCustom());
            auto_arrange.radio = true;
            items.push_back(std::move(auto_arrange));
            auto manual_arrange = make_item(text(types::MenuID::ManualArrange), "custom",
                                            !read_only_, spCustom());
            manual_arrange.radio = true;
            items.push_back(std::move(manual_arrange));
        }
        if constexpr (std::is_same_v<A, Battle>) {
            items.push_back(separator());
            items.push_back(make_item(text(types::MenuID::PossibleToRunAway),
                                      "possible_to_run_away",
                                      canReversePossibleToRunAway(),
                                      isPossibleToRunAway()));
        }
        if (!lib::editor_window_host_options().all_mode) {
            items.push_back(separator());
            items.push_back(make_item(text(types::MenuID::ShowGrid), "show_grid", true, show_grid_));
        }
        if (!read_only_) {
            lib::MenuItem change = make_item(text(types::MenuID::ChangePos), "", canChangePos());
            change.children = {
                make_item(text(types::MenuID::NearTop), "near_top", canNearTop()),
                make_item(text(types::MenuID::NearBottom), "near_bottom", canNearBottom()),
                make_item(text(types::MenuID::NearLeft), "near_left", canNearLeft()),
                make_item(text(types::MenuID::NearRight), "near_right", canNearRight()),
                make_item(text(types::MenuID::NearCenterH), "near_center_h", canNearCenter(true, false)),
                make_item(text(types::MenuID::NearCenterV), "near_center_v", canNearCenter(false, true)),
                make_item(text(types::MenuID::NearCenter), "near_center", canNearCenter(true, true)),
                separator(),
                make_item(text(types::MenuID::PosTop), "pos_top", canPosTop()),
                make_item(text(types::MenuID::PosBottom), "pos_bottom", canPosBottom()),
                make_item(text(types::MenuID::PosLeft), "pos_left", canPosLeft()),
                make_item(text(types::MenuID::PosRight), "pos_right", canPosRight()),
                make_item(text(types::MenuID::PosEven), "pos_even", canPosEven())};
            if constexpr (UseCards) {
                change.children.push_back(separator());
                change.children.push_back(make_item(text(types::MenuID::ScaleMin), "scale_min", canScaleCAny(prop_->var().cardScaleMin.value)));
                change.children.push_back(make_item(text(types::MenuID::ScaleMiddle), "scale_middle", canScaleCAny(100)));
                change.children.push_back(make_item(text(types::MenuID::ScaleMax), "scale_max", canScaleCAny(prop_->var().cardScaleMax.value)));
            }
            change.children.push_back(separator());
            change.children.push_back(make_item(text(types::MenuID::ScaleBig), "scale_big", canScaleEvenAny()));
            if constexpr(UseBacks)
                change.children.push_back(make_item(text(types::MenuID::ExpandBack), "expand_backs", canExpandBacks()));
            change.children.push_back(make_item(text(types::MenuID::ScaleSmall), "scale_small", canScaleEvenAny()));
            items.push_back(std::move(change));
        }
        (void)lib::replace_menu_items(menu_, std::move(items));
    }

    void rebuildListMenu(const bool cards) {
        if (!menu_) return;
        const auto make_item = [](std::string text, std::string command,
                                  bool enabled = true) {
            lib::MenuItem item;
            item.text = std::move(text);
            item.command = std::move(command);
            item.enabled = enabled;
            return item;
        };
        const auto separator = [] {
            lib::MenuItem item;
            item.separator = true;
            return item;
        };
        const auto text = [this](const types::MenuID id) {
            // Dと同じく、設定されたアクセスキー（(&E)など）・サブメ
            // ニューの省略記号・ショートカットをメニューへ付ける。 メ
            // ニューを開いた後のキー入力で対応項目を選べるようにする。
            return prop_->buildMenu(id);
        };
        std::vector<lib::MenuItem> items;
        const bool has_selection = cards ? !selected_cards_.empty()
                                         : !selected_backs_.empty();
        items.push_back(make_item(text(types::MenuID::EditProp), "edit",
                                  !read_only_ && has_selection));
        if (!read_only_) {
            items.push_back(separator());
            if (cards) {
                if constexpr (UseCards)
                    items.push_back(make_item(text(std::is_same_v<C, EnemyCard>
                                                       ? types::MenuID::NewEnemyCard
                                                       : types::MenuID::NewMenuCard),
                                              "create_card",
                                               canCreateCard()));
            } else if constexpr (UseBacks) {
                items.push_back(make_item(text(types::MenuID::NewBack), "create_back"));
                items.push_back(make_item(text(types::MenuID::NewTextCell), "create_text"));
                items.push_back(make_item(text(types::MenuID::NewColorCell), "create_color"));
                items.push_back(make_item(text(types::MenuID::NewPCCell), "create_pc",
                                          canCreatePCCell()));
            }
            items.push_back(separator());
            items.push_back(make_item(text(types::MenuID::Comment), "comment",
                                      cards ? canWriteCommentC() : canWriteCommentB()));
        }
        items.push_back(separator());
        items.push_back(make_item(text(types::MenuID::Cut), "cut", !read_only_ && has_selection));
        items.push_back(make_item(text(types::MenuID::Copy), "copy", has_selection));
        items.push_back(make_item(text(types::MenuID::Paste), "paste", canDoP()));
        items.push_back(make_item(text(types::MenuID::Delete), "delete", !read_only_ && has_selection));
        items.push_back(make_item(text(types::MenuID::Clone), "clone", !read_only_ && has_selection));
        items.push_back(separator());
        items.push_back(make_item(text(types::MenuID::SelectAll),
                                  cards ? "select_all_cards" : "select_all_backs",
                                  cards ? selected_cards_.size() < cardCount()
                                        : selected_backs_.size() < backCount()));
        if constexpr (std::is_same_v<A, Area> || std::is_same_v<A, Battle>) {
            items.push_back(separator());
            items.push_back(make_item(text(types::MenuID::EditEvent), "open_event"));
        }
        if (summary_) {
            items.push_back(separator());
            items.push_back(make_item(text(types::MenuID::SelectConnectedResource),
                                      "connected", canSelectConnectedResource()));
        }
        if (!read_only_) {
            items.push_back(separator());
            if (cards) {
                items.push_back(make_item(text(types::MenuID::FindID), "find_card_group",
                                          canFindCardGroup()));
                items.push_back(make_item(text(types::MenuID::SortWithPosition),
                                          "sort_with_position", canSortWithPosition()));
            } else {
                items.push_back(make_item(text(types::MenuID::FindID), "find_cell_name",
                                          canFindCellName()));
            }
        }
        (void)lib::replace_menu_items(menu_, std::move(items));
    }

    // DのImagePaneは選択した全カード・セルを一
    // つの操作で動かす。 ここでもUndoは一件とし、ポ
    // インター移動中に座標を更新して、解放時に開始・終了
    // 配置を確定する。 ネイティブのMoveごとにUndo
    // を増やしたり、パネル全体を再構成したりしない。

    void beginSceneDrag(const lib::Point position, const SceneDragHandle handle,
                        const SceneEntry& target) {
        scene_dragging_ = true;
        scene_drag_moved_ = false;
        scene_drag_handle_ = handle;
        scene_drag_target_kind_ = target.kind;
        scene_drag_target_index_ = target.index;
        scene_drag_origin_ = sceneWorldPoint(position);
        scene_drag_cards_ = captureCardGeometry();
        scene_drag_backs_ = captureBackGeometry();
        // DはMouseUpまで各FlexImageに作業中の配置を保持する。 ビュ
        // ー内の別フレームを使い、キャンセル時はモデルに触れず描き直せるようにする。
        scene_drag_preview_cards_ = scene_drag_cards_;
        scene_drag_preview_backs_ = scene_drag_backs_;
        if (canvas_ && context_ && context_->services.gui)
            context_->services.gui->capture_pointer(canvas_, true);
    }

    [[nodiscard]] lib::Rect cardDragBounds(const std::size_t index,
                                            const CardGeometry& before) const noexcept {
        if constexpr (UseCards) {
            if (index < card_images_.size() && card_images_[index]) {
                auto result = card_images_[index]->bounds();
                // 前のMoveで画像は既に変わっているため、 モデルのス
                // ナップショットからドラッグ開始時の範囲を再構成する。
                result.x = static_cast<float>(before.x);
                result.y = static_cast<float>(before.y);
                result.width = static_cast<float>(static_cast<std::int64_t>(card_images_[index]->baseWidth()) * before.scale / 100);
                result.height = static_cast<float>(static_cast<std::int64_t>(card_images_[index]->baseHeight()) * before.scale / 100);
                return result;
            }
        }
        return {static_cast<float>(before.x), static_cast<float>(before.y), 1.0F, 1.0F};
    }

    [[nodiscard]] lib::Rect sceneDragTargetBounds() const noexcept {
        if (scene_drag_target_kind_ == SceneEntryKind::Card) {
            if constexpr (UseCards) {
                if (scene_drag_target_index_ < scene_drag_cards_.size())
                    return cardDragBounds(scene_drag_target_index_,
                                          scene_drag_cards_[scene_drag_target_index_]);
            }
        } else if (scene_drag_target_kind_ == SceneEntryKind::Background) {
            if constexpr (UseBacks) {
                if (scene_drag_target_index_ < scene_drag_backs_.size()) {
                    const auto& value = scene_drag_backs_[scene_drag_target_index_];
                    return {static_cast<float>(value.x), static_cast<float>(value.y),
                            static_cast<float>(value.width), static_cast<float>(value.height)};
                }
            }
        }
        return {};
    }

    [[nodiscard]] bool sceneMemberMovable(const SceneEntryKind kind,
                                          const std::size_t index) const noexcept {
        if (kind == SceneEntryKind::Card) {
            if constexpr (UseCards)
                return view_cards_ && index < card_images_.size() && card_images_[index] &&
                    card_images_[index]->visible() && !cardIsFixed(index);
        } else if (kind == SceneEntryKind::Background) {
            if constexpr (UseBacks)
                return view_backs_ && index < back_images_.size() && back_images_[index] &&
                    back_images_[index]->visible() && !backIsFixed(index);
        }
        return false;
    }

    [[nodiscard]] static int sceneToGrid(const int value, const std::uint32_t step) noexcept {
        if (step <= 1U) return value;
        const auto grid = static_cast<std::int64_t>(step);
        const auto shifted = static_cast<std::int64_t>(value) + grid / 2;
        return static_cast<int>(shifted - shifted % grid);
    }

    [[nodiscard]] lib::Rect snapSceneMove(lib::Rect rect) const noexcept {
        if (!show_grid_) return rect;
        const auto range = static_cast<std::int64_t>(prop_->var().gridRange.value);
        const auto snap = [range](const int p, const int size, const std::uint32_t grid) {
            const int leading = sceneToGrid(p, grid), trailing = sceneToGrid(p + size, grid);
            const auto first = std::abs(static_cast<std::int64_t>(leading) - p);
            const auto last = std::abs(static_cast<std::int64_t>(trailing) - p - size);
            return first <= last ? (first <= range ? leading : p)
                                 : (last <= range ? trailing - size : p);
        };
        rect.x = static_cast<float>(snap(static_cast<int>(rect.x), static_cast<int>(rect.width), grid_x_));
        rect.y = static_cast<float>(snap(static_cast<int>(rect.y), static_cast<int>(rect.height), grid_y_));
        return rect;
    }

    struct SceneResizeResult { lib::Rect bounds; std::uint32_t scale{100U}; };
    [[nodiscard]] SceneResizeResult resizeSceneRect(
        const lib::Rect original, const int dx, const int dy,
        const bool left, const bool right, const bool top, const bool bottom,
        const bool keep_ratio, const areaviewutils::FlexImage* card = nullptr) const noexcept {
        int x = static_cast<int>(original.x), y = static_cast<int>(original.y);
        int w = static_cast<int>(original.width), h = static_cast<int>(original.height);
        if (left || right) w = std::clamp(w + (left ? -dx : dx), 0, 65536);
        if (top || bottom) h = std::clamp(h + (top ? -dy : dy), 0, 65536);
        const int base_w = card ? card->baseWidth() : static_cast<int>(original.width);
        const int base_h = card ? card->baseHeight() : static_cast<int>(original.height);
        const bool by_width = (left || right) && !(top || bottom) ? true :
            !(left || right) && (top || bottom) ? false : base_w >= base_h;
        std::uint32_t scale = card ? card->scale() : 100U;
        const auto round_ratio = [&] {
            if (!keep_ratio && !card) return;
            const auto base = by_width ? base_w : base_h;
            if (base <= 0) return;
            auto percent = static_cast<std::int64_t>(by_width ? w : h) * 100 / base;
            if (card) percent = std::clamp<std::int64_t>(percent, card->minimumScale, card->maximumScale);
            scale = static_cast<std::uint32_t>(std::max<std::int64_t>(0, percent));
            w = static_cast<int>(static_cast<std::int64_t>(base_w) * scale / 100);
            h = static_cast<int>(static_cast<std::int64_t>(base_h) * scale / 100);
        };
        round_ratio();
        if (left) x = static_cast<int>(original.x + original.width) - w;
        if (top) y = static_cast<int>(original.y + original.height) - h;
        if (show_grid_ && (grid_x_ > 1U || grid_y_ > 1U)) {
            const int far_x = x + w, far_y = y + h;
            const auto range = static_cast<std::int64_t>(prop_->var().gridRange.value);
            const auto near = [range](const int a, const int b) {
                return std::abs(static_cast<std::int64_t>(a) - b) <= range;
            };
            if (left) { const auto p = sceneToGrid(x, grid_x_); if (near(x,p)) { w += x-p; x=p; } }
            if (right) { const auto p = sceneToGrid(x+w, grid_x_); if (near(far_x,p)) w=p-x; }
            if (top) { const auto p = sceneToGrid(y, grid_y_); if (near(y,p)) { h += y-p; y=p; } }
            if (bottom) { const auto p = sceneToGrid(y+h, grid_y_); if (near(far_y,p)) h=p-y; }
            if (keep_ratio || card) {
                if (by_width && left) x=far_x-w;
                if (!by_width && top) y=far_y-h;
                round_ratio();
                if (by_width && top) y=far_y-h;
                if (!by_width && left) x=far_x-w;
            }
        }
        if (!card) { w=std::clamp(w,0,65536); h=std::clamp(h,0,65536); }
        return {{static_cast<float>(x),static_cast<float>(y),static_cast<float>(w),static_cast<float>(h)},scale};
    }

    void updateSceneDrag(const lib::Point position, const lib::Modifiers modifiers) {
        if (!scene_dragging_) return;
        const std::weak_ptr<void> life=scene_view_lifetime_;
        const auto revision=geometry_revision_;
        scene_drag_moved_ = true;
        const auto current = sceneWorldPoint(position);
        auto dx = static_cast<int>(current.x - scene_drag_origin_.x);
        auto dy = static_cast<int>(current.y - scene_drag_origin_.y);
        // ShiftはDのImagePaneと同じく、移動を一
        // 軸に制限する。 拡縮の縦横比固定は、カードでは画像
        // の倍率モード、背景では個別のShift指定で行う。
        const bool shift = (static_cast<std::uint32_t>(modifiers) &
                            static_cast<std::uint32_t>(lib::Modifiers::Shift)) != 0U;
        if (scene_drag_handle_ == SceneDragHandle::Move && shift) {
            if (std::abs(dy) <= std::abs(dx)) dy = 0;
            else dx = 0;
        }
        if (scene_drag_handle_ != SceneDragHandle::Move) {
            updateSceneResize(dx, dy, shift);
            if(life.expired() || geometry_revision_!=revision)return;
            requestSceneRedraw();
            if(life.expired() || geometry_revision_!=revision)return;
            // ポインターを離すまで、保留中のサイズ変更はこのビューが所有する。
            return;
        }
        if constexpr (UseCards) {
            for (const auto index : selected_cards_) {
                if (!sceneMemberMovable(SceneEntryKind::Card,index) || index >= cardCount() ||
                    index >= scene_drag_cards_.size()) continue;
                auto moved = cardDragBounds(index, scene_drag_cards_[index]);
                moved.x += dx; moved.y += dy; moved = snapSceneMove(moved);
                auto& preview=scene_drag_preview_cards_[index];
                preview.x=static_cast<int>(moved.x);
                preview.y=static_cast<int>(moved.y);
                if (index < card_images_.size() && card_images_[index]) {
                    card_images_[index]->x(preview.x);
                    card_images_[index]->y(preview.y);
                }
            }
        }
        if constexpr (UseBacks) {
            for (const auto index : selected_backs_) {
                if (!sceneMemberMovable(SceneEntryKind::Background,index)) continue;
                if (index < backCount() && index < scene_drag_backs_.size()) {
                    const auto& before = scene_drag_backs_[index];
                    const auto moved = snapSceneMove({static_cast<float>(before.x + dx),
                        static_cast<float>(before.y + dy), static_cast<float>(before.width),static_cast<float>(before.height)});
                    auto& preview=scene_drag_preview_backs_[index];
                    preview.x=static_cast<int>(moved.x);
                    preview.y=static_cast<int>(moved.y);
                    if (index < back_images_.size() && back_images_[index]) {
                        back_images_[index]->x(preview.x);
                        back_images_[index]->y(preview.y);
                    }
                }
            }
        }
        requestSceneRedraw();
        if(life.expired() || geometry_revision_!=revision)return;
        // これはビュー内だけのFlexImageプレビュー。 finishSceneDrag
        // が結果を反映するまで、他のビューは確定済みのモデルを使う。
    }

    // ImagePaneのFlexImageドラッグに対応する
    // 配置計算。全選択対象を開始時のスナップショットから計算
    // し、 Moveの繰り返しで丸め誤差を累積させない。設定
    // が有効なら、対象の比率で 他の選択要素も拡縮して相対配
    // 置を保つ（D images.d 2555-2793）。
    void updateSceneResize(const int dx, const int dy, const bool ratio_fix) {
        const bool left = isLeftHandle(scene_drag_handle_);
        const bool right = isRightHandle(scene_drag_handle_);
        const bool top = isTopHandle(scene_drag_handle_);
        const bool bottom = isBottomHandle(scene_drag_handle_);
        const bool group_ratio = prop_ && prop_->var().resizingImageWithRatio.value;
        const auto target_bounds = sceneDragTargetBounds();
        // グループの寸法は対象の未補正の差分から始める。 各要素
        // は自分の新寸法を切り捨ててから、位置の比率を求める。
        std::optional<double> ratio_w;
        std::optional<double> ratio_h;
        if (target_bounds.width != 0.0F)
            ratio_w = (static_cast<double>(target_bounds.width) + dx) / target_bounds.width;
        if (target_bounds.height != 0.0F)
            ratio_h = (static_cast<double>(target_bounds.height) + dy) / target_bounds.height;
        // Dと同じく、拡縮対象の軸が0なら、寸法が0でない選択画像を代わりに使う。
        if (group_ratio && (!ratio_w || !std::isfinite(*ratio_w) ||
                            !ratio_h || !std::isfinite(*ratio_h))) {
            const auto entries = sceneEntries();
            for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
                if (!it->image || it->kind == SceneEntryKind::Decoration) continue;
                const auto bounds = it->image->bounds();
                if ((!ratio_w || !std::isfinite(*ratio_w)) && bounds.width != 0.0F)
                    ratio_w = (static_cast<double>(bounds.width) + dx) / bounds.width;
                if ((!ratio_h || !std::isfinite(*ratio_h)) && bounds.height != 0.0F)
                    ratio_h = (static_cast<double>(bounds.height) + dy) / bounds.height;
                if (ratio_w && ratio_h && std::isfinite(*ratio_w) && std::isfinite(*ratio_h))
                    break;
            }
        }
        const areaviewutils::FlexImage* target_card = nullptr;
        if constexpr (UseCards) {
            if (scene_drag_target_kind_ == SceneEntryKind::Card && scene_drag_target_index_ < card_images_.size())
                target_card = card_images_[scene_drag_target_index_].get();
        }
        const auto target = resizeSceneRect(target_bounds,dx,dy,left,right,top,bottom,ratio_fix,target_card).bounds;
        const auto member = [&](const lib::Rect bounds, const areaviewutils::FlexImage* card, const bool is_target) {
            const int mx = group_ratio && bounds.width != 0.0F && ratio_w && std::isfinite(*ratio_w)
                ? static_cast<int>(bounds.width * *ratio_w) - static_cast<int>(bounds.width) : dx;
            const int my = group_ratio && bounds.height != 0.0F && ratio_h && std::isfinite(*ratio_h)
                ? static_cast<int>(bounds.height * *ratio_h) - static_cast<int>(bounds.height) : dy;
            auto result = resizeSceneRect(bounds,mx,my,left,right,top,bottom,ratio_fix,card);
            auto& changed = result.bounds;
            if (group_ratio && !is_target) {
                if (target.width != target_bounds.width) {
                    const auto ratio = bounds.width != 0.0F ? static_cast<double>(changed.width)/bounds.width : 1.0;
                    changed.x = target.x + static_cast<int>((bounds.x - (left ? target_bounds.x : target.x))*ratio);
                }
                if (target.height != target_bounds.height) {
                    const auto ratio = bounds.height != 0.0F ? static_cast<double>(changed.height)/bounds.height : 1.0;
                    changed.y = target.y + static_cast<int>((bounds.y - (top ? target_bounds.y : target.y))*ratio);
                }
            }
            return result;
        };
        if constexpr (UseCards) {
            for (const auto index : selected_cards_) {
                if (!sceneMemberMovable(SceneEntryKind::Card,index) || index >= cardCount() || index >= scene_drag_cards_.size()) continue;
                const auto result = member(cardDragBounds(index,scene_drag_cards_[index]),card_images_[index].get(),
                    scene_drag_target_kind_==SceneEntryKind::Card && index==scene_drag_target_index_);
                auto& value = scene_drag_preview_cards_[index];
                value.x=static_cast<int>(result.bounds.x);
                value.y=static_cast<int>(result.bounds.y);
                value.scale=result.scale;
                card_images_[index]->x(value.x);card_images_[index]->y(value.y);card_images_[index]->scale(value.scale);
            }
        }
        if constexpr (UseBacks) {
            for (const auto index : selected_backs_) {
                if (!sceneMemberMovable(SceneEntryKind::Background,index) || index >= backCount() || index >= scene_drag_backs_.size()) continue;
                const auto& before = scene_drag_backs_[index];
                const auto result = member({static_cast<float>(before.x),static_cast<float>(before.y),
                    static_cast<float>(before.width),static_cast<float>(before.height)},nullptr,
                    scene_drag_target_kind_==SceneEntryKind::Background && index==scene_drag_target_index_);
                auto& value = scene_drag_preview_backs_[index];
                value.x=static_cast<int>(result.bounds.x);value.y=static_cast<int>(result.bounds.y);
                value.width=static_cast<int>(result.bounds.width);value.height=static_cast<int>(result.bounds.height);
                back_images_[index]->x(value.x);back_images_[index]->y(value.y);
                back_images_[index]->width(value.width);back_images_[index]->height(value.height);
            }
        }
    }

    void finishSceneClick(const SceneEntryKind kind,const std::size_t index) {
        auto& values = kind == SceneEntryKind::Card ? selected_cards_ : selected_backs_;
        auto& others = kind == SceneEntryKind::Card ? selected_backs_ : selected_cards_;
        const auto modifiers = static_cast<std::uint32_t>(scene_press_modifiers_);
        if (modifiers & (static_cast<std::uint32_t>(lib::Modifiers::Control) | static_cast<std::uint32_t>(lib::Modifiers::Shift))) {
            const auto found=std::find(values.begin(),values.end(),index);
            if (found==values.end()) values.push_back(index);else values.erase(found);
        } else { values={index};others.clear(); }
    }

    void finishSceneDrag(const bool commit) {
        if (!scene_dragging_) return;
        scene_dragging_ = false;
        if (commit && !scene_drag_moved_)
            finishSceneClick(scene_drag_target_kind_,scene_drag_target_index_);
        const std::weak_ptr<void> life=scene_view_lifetime_;
        const auto revision=geometry_revision_;
        const auto before_cards = scene_drag_cards_;
        const auto before_backs = scene_drag_backs_;
        const auto preview_cards = scene_drag_preview_cards_;
        const auto preview_backs = scene_drag_preview_backs_;
        // DのUndoEditは編集したセルを記録するが、
        // 後からの表の選択変更は再生時にも保つ。 これら
        // の添字は配置の対象群と参照通知を識別するもので
        // 、Undo/Redo後の行再選択要求ではない。
        const auto selected_cards = detail::normalizedSelection<C>(
            selected_cards_, cardCount());
        const auto selected_backs = detail::normalizedSelection<BgImage>(
            selected_backs_, backCount());
        scene_drag_cards_.clear();
        scene_drag_backs_.clear();
        scene_drag_preview_cards_.clear();
        scene_drag_preview_backs_.clear();
        scene_drag_handle_ = SceneDragHandle::Move;
        scene_drag_target_kind_ = SceneEntryKind::Decoration;
        scene_drag_target_index_ = std::numeric_limits<std::size_t>::max();
        if (canvas_ && context_ && context_->services.gui)
            context_->services.gui->capture_pointer(canvas_, false);
        if(life.expired() || geometry_revision_!=revision)return;
        if(!commit)cancelSceneDragPreview(selected_cards,selected_backs);
        else if(scene_drag_moved_)
            commitSceneDragPreview(before_cards,before_backs,preview_cards,preview_backs,
                                   selected_cards,selected_backs);
        else commitSceneGeometry(before_cards,before_backs,selected_cards,selected_backs,true);
        if(commit && !life.expired() && geometry_revision_==revision+1 && !scene_drag_moved_)
            refreshSelected(true, true);
    }

    void refreshFlagRows() {
        if (!flags_) return;
        if (compactAllMode()) flags_->columns = {{"名称", 300.0F}, {"値", 60.0F}};
        else flags_->columns = {{"名称", 180.0F}, {"値", 80.0F}};
        // Dと同じく、場面のカード・背景が参照するフラグパスだ
        // けを一覧にする。 この表は表示フィルターで、シナリオ
        // 全体のフラグ編集ではない。 特に空の「フラグなし」項
        // 目では、パスと翻訳済みの表示文字列を分けて保持する。
        const auto previous_selection = flags_->selection;
        std::vector<std::string> selected_paths;
        selected_paths.reserve(previous_selection.size());
        for (const auto index : previous_selection)
            if (index < flag_row_paths_.size()) selected_paths.push_back(flag_row_paths_[index]);

        std::string retained_label;
        if (!previous_selection.empty()) {
            const auto index = previous_selection.front();
            if (index < flag_row_paths_.size() && flag_row_paths_[index].empty() &&
                index < flags_->rows.size() && !flags_->rows[index].cells.empty())
                retained_label = flags_->rows[index].cells.front().text;
        }
        bool retained_nonempty_path = false;
        std::vector<std::string> paths;
        const auto append_path = [&paths](const std::string& path) {
            if (std::find(paths.begin(), paths.end(), path) == paths.end()) paths.push_back(path);
        };
        if constexpr (UseCards)
            for (const auto& card : areaCardArray()) if (card) append_path(card->flag());
        if constexpr (UseBacks)
            for (const auto& image : area_->backs()) if (image) append_path(image->flag());
        std::stable_sort(paths.begin(), paths.end(), [this](const auto& left, const auto& right) {
            return scene_flags::compare(left, right, prop_->var().logicalSort.value) < 0;
        });
        flag_has_paths_ = std::any_of(paths.begin(), paths.end(),
                                      [](const auto& path) { return !path.empty(); });

        std::vector<lib::TableRow> rows;
        std::vector<std::string> visible_paths;
        const auto root = summary_ ? summary_->flagDirRoot() : nullptr;
        const auto counter = [&]() -> ::cwxeditor::cwx::flag::UseCounterPtr {
            if constexpr (std::is_same_v<A, BgImageContainer>) {
                // DのrefreshFlagsと同じく、背景の参照はシナリオ
                // ルートで解決する。 プレビュー・参照数用のイベントローカル
                // なカウンターはコンテキストに残すが、この一覧には出さない。
                return {};
            }
            else return area_->useCounter();
        }();
        rows.reserve(paths.size());
        for (const auto& path : paths) {
            if (!path.empty() && path == retained_label) retained_nonempty_path = true;
            if (!path.empty() && flag_inc_search_ && !flag_inc_search_->match(path))
                continue;
            lib::TableRow row;
            std::string value;
            std::shared_ptr<::cwxeditor::cwx::flag::Flag> referenced_flag;
            if (!path.empty())
                if ((referenced_flag = ::cwxeditor::cwx::flag::findVar<
                         ::cwxeditor::cwx::flag::Flag>(root, counter, path)))
                    value = referenced_flag->onOff()
                        ? referenced_flag->on() : referenced_flag->off();
            row.cells = {
                lib::ListItem{path.empty()
                    ? prop_->requiredMsgs().defaultSelection(prop_->requiredMsgs().noFlagRef.value) : path},
                lib::ListItem{std::move(value)}};
            row.cells.front().icon = path.empty()
                ? prop_->images().load(dprops::StringView{"empty.png"})
                : prop_->images().flag();
            if (!path.empty() && !referenced_flag) {
                row.warnings.push_back(::cwxeditor::cwx::utils::tryFormat(
                    prop_->requiredMsgs().noFlag.value, path));
                row.cells.front().warnings = row.warnings;
                row.warning_icon = prop_->images().load(dprops::StringView{"warning.png"});
            }
            rows.push_back(std::move(row));
            visible_paths.push_back(path);
        }
        const auto source_summary = summary_;
        // Dは空アイコン行の表示名だけを記憶してパスと
        // 比較し、空でない一致がなければ最初の行へ戻る
        // 。 通常の選択フラグは同一性で保持しない。
        std::vector<std::size_t> selection;
        for (std::size_t index = 0; index < visible_paths.size(); ++index)
            if (visible_paths[index] == retained_label) selection = {index};
        if (!retained_nonempty_path && !visible_paths.empty()) selection = {0U};
        const auto revision = ++flags_table_revision_;
        const auto reveal = selection.empty()
            ? std::optional<std::size_t>{} : std::optional<std::size_t>{selection.front()};
        auto publication = lib::prepare_table_rows(
            flags_, std::move(rows), selection, reveal,
            [this, source_summary, revision, previous_selection, selected_paths] {
                // 入れ子の更新やネイティブの選択通知を、この準備済みフレームより優先
                // する。 Summaryの同一性だけでは更新の有効性を判断しない。
                if (summary_ != source_summary || flags_table_revision_ != revision ||
                    !flags_ || flags_->selection != previous_selection) return false;
                std::vector<std::string> live_paths;
                for (const auto index : flags_->selection)
                    if (index < flag_row_paths_.size()) live_paths.push_back(flag_row_paths_[index]);
                return live_paths == selected_paths;
            });
        if (publication.commit()) {
            flag_row_paths_ = std::move(visible_paths);
            syncFlagChecks();
            flags_->set_enabled(!flags_->rows.empty());
            if (all_flags_check_) all_flags_check_->set_enabled(!flags_->rows.empty());
        }
    }

    void syncFlagChecks() {
        if (!flags_) return;
        struct CheckState { bool seen{}; bool checked{}; bool mixed{}; };
        std::vector<CheckState> states(flag_row_paths_.size());
        const auto put = [this, &states](const std::string& path, const bool checked) {
            for (std::size_t index = 0; index < flag_row_paths_.size(); ++index) {
                if (::cwxeditor::cwx::enumutils::lowerCase(flag_row_paths_[index]) !=
                    ::cwxeditor::cwx::enumutils::lowerCase(path)) continue;
                auto& state = states[index];
                if (state.seen && state.checked != checked) state.mixed = true;
                else if (!state.seen) state.checked = checked;
                state.seen = true;
            }
        };
        if constexpr (UseCards) {
            ensureCardChecks();
            for (std::size_t index = 0; index < cardCount(); ++index)
                put(areaCard(index).flag(), card_checked_[index]);
        }
        if constexpr (UseBacks) {
            ensureBackChecks();
            for (std::size_t index = 0; index < backCount(); ++index)
                put(back(index).flag(), back_checked_[index]);
        }
        bool all_checked = !states.empty();
        for (std::size_t index = 0; index < states.size() && index < flags_->rows.size(); ++index) {
            flags_->rows[index].checked = states[index].checked || states[index].mixed;
            flags_->rows[index].grayed = states[index].mixed;
            all_checked = all_checked && states[index].checked && !states[index].mixed;
        }
        if (all_flags_check_) all_flags_check_->checked = all_checked;
        requestWidgetRedraw(flags_);
    }

    void applyFlagChecks(const bool resync_reference_rows = true) {
        const auto checked_for = [this](const std::string& path) -> std::optional<bool> {
            std::optional<bool> result;
            const auto key = ::cwxeditor::cwx::enumutils::lowerCase(path);
            for (std::size_t index = 0; index < flag_row_paths_.size() &&
                                      index < flags_->rows.size(); ++index)
                if (!flags_->rows[index].grayed &&
                    ::cwxeditor::cwx::enumutils::lowerCase(flag_row_paths_[index]) == key)
                    result = flags_->rows[index].checked;
            // Dは表示行順に小文字化したキーのマップへ代入する。 大文字・小文
            // 字だけが違う名前では、最初でなく最後の灰色でない行を採用する。
            return result;
        };
        if constexpr (UseCards) {
            ensureCardChecks();
            for (std::size_t index = 0; index < cardCount(); ++index)
                if (const auto checked = checked_for(areaCard(index).flag()))
                    card_checked_[index] = *checked;
            refreshCards();
        }
        if constexpr (UseBacks) {
            ensureBackChecks();
            for (std::size_t index = 0; index < backCount(); ++index)
                if (const auto checked = checked_for(back(index).flag()))
                    back_checked_[index] = *checked;
            refreshBacks();
        }
        applySceneVisibility();
        if (resync_reference_rows) syncFlagChecks();
        else if (all_flags_check_) {
            bool all_checked = !flags_->rows.empty();
            for (const auto& row : flags_->rows)
                all_checked = all_checked && row.checked && !row.grayed;
            all_flags_check_->checked = all_checked;
        }
    }

    [[nodiscard]] std::size_t cardCount() const noexcept {
        if constexpr (!UseCards) return 0U;
        else return areaCardArray().size();
    }
    [[nodiscard]] std::size_t backCount() const noexcept {
        if constexpr (!UseBacks) return 0U;
        else if constexpr (requires(A& value) { value.backs(); }) return area_->backs().size();
        else return 0U;
    }
    [[nodiscard]] decltype(auto) areaCardArray() const {
        if constexpr (std::is_same_v<A, Area>) return area_->cards();
        else return area_->cards();
    }
    [[nodiscard]] C& areaCard(std::size_t index) const { return *areaCardPtr(index); }
    [[nodiscard]] std::shared_ptr<C> areaCardPtr(std::size_t index) const { return areaCardArray()[index]; }
    [[nodiscard]] BgImage& back(std::size_t index) const { return *backPtr(index); }
    [[nodiscard]] std::shared_ptr<BgImage> backPtr(std::size_t index) const {
        if constexpr (requires(A& value) { value.backs(); }) return area_->backs()[index];
        else return {};
    }

    void insertCard(std::size_t index, const std::shared_ptr<C>& value) {
        if (!value) return;
        if constexpr (std::is_same_v<A, Area>) area_->insert(index, std::dynamic_pointer_cast<MenuCard>(value));
        else if constexpr (std::is_same_v<A, Battle>) area_->insert(index, std::dynamic_pointer_cast<EnemyCard>(value));
    }
    void insertBack(std::size_t index, const std::shared_ptr<BgImage>& value) {
        if (!value) return;
        if constexpr (requires(A& owner, std::size_t i, background::BgImagePtr cell) { owner.insert(i, std::move(cell)); }) area_->insert(index, value);
        else if constexpr (requires(A& owner, background::BgImagePtr cell) { owner.append(std::move(cell)); }) area_->append(value);
    }
    void swapBacks(std::size_t first, std::size_t second) {
        if constexpr (requires(A& owner) { owner.swapBacks(first, second); }) area_->swapBacks(first, second);
    }

    [[nodiscard]] std::shared_ptr<C> shallowCardSnapshot(const std::shared_ptr<C>& target)const {
        if constexpr(!UseCards)return {};
        else {
            auto saved=std::dynamic_pointer_cast<C>(target->shallowCopy());
            if(const auto counter=area_->useCounter())saved->setUseCounter(counter->sub(),static_cast<AbstractArea*>(area_));
            return saved;
        }
    }
    Snapshot snapshot(const std::vector<std::size_t>& deleted_cards = {},
                      const std::vector<std::size_t>& deleted_backs = {}) const {
        Snapshot result;
        if constexpr(requires(A& owner){owner.weak_from_this().lock();})result.model_owner=area_->weak_from_this().lock();
        // DのUndoEditは浅い属性コピーを保存し、同じカ
        // ードへ適用する。 モデルレス画面が参照するカードを
        // 深く複製・置換せず、実際の同一性とイベントを保つ。
        const auto counter=[&]() -> usecounter::CounterOwner {
            if constexpr(requires(A& owner){owner.useCounter();})
                if(const auto source=area_->useCounter())return source->sub();
            return {};
        }();
        // Dはエリア自身をカウンターの所有者として渡す。AreaはBgImageOwner
        // 経由にもCWXPath基底を持つため、 Summaryを指す場合があるucOwner()
        // でなく、AbstractArea側のパスで同じエリアを識別する。
        const auto counter_owner=[&]() -> ::cwxeditor::cwx::path::CWXPath* {
            if constexpr(UseCards)return static_cast<::cwxeditor::cwx::area::AbstractArea*>(area_);
            else return static_cast<::cwxeditor::cwx::path::CWXPath*>(area_);
        }();
        if constexpr(UseCards && std::is_same_v<A,Area>)for(const auto& value:area_->cards()) {
            result.menu_identities.push_back(value);
            auto saved=std::dynamic_pointer_cast<MenuCard>(value->shallowCopy());
            if(counter)saved->setUseCounter(counter,counter_owner);result.menu_cards.push_back(std::move(saved));
        }
        if constexpr(UseCards && std::is_same_v<A,Battle>)for(const auto& value:area_->cards()) {
            result.enemy_identities.push_back(value);
            auto saved=std::dynamic_pointer_cast<EnemyCard>(value->shallowCopy());
            if(counter)saved->setUseCounter(counter,counter_owner);result.enemy_cards.push_back(std::move(saved));
        }
        result.card_checked=card_checked_;result.card_grayed=card_grayed_;
        result.back_checked=back_checked_;result.back_grayed=back_grayed_;
        // 新しい行順を表示へ反映する前に履歴を取得する。ここでも
        // 同一性で対応付け、 旧位置のフラグをコピーしてRedo
        // 時に別のセルへ移動前の状態を適用しないようにする。
        if constexpr(UseCards){auto keys=card_check_keys_;detail::synchronizeChecks(areaCardArray(),keys,result.card_checked,result.card_grayed);}
        if constexpr(UseBacks){auto keys=back_check_keys_;detail::synchronizeChecks(area_->backs(),keys,result.back_checked,result.back_grayed);}
        if constexpr (UseBacks && requires(A& value) { value.backs(); })
            for (const auto& value : area_->backs()) {
                result.back_identities.push_back(value);
                auto saved=detail::cloneObject(value);
                if(counter)saved->setUseCounter(counter,counter_owner);result.backs.push_back(std::move(saved));
            }
        result.sp_auto = detail::hasSpAuto(*area_);
        if constexpr (UseCards)
            result.selected_cards = detail::normalizedSelection<C>(selected_cards_, cardCount());
        if constexpr (UseBacks)
            result.selected_backs = detail::normalizedSelection<BgImage>(selected_backs_, backCount());
        if constexpr (std::is_same_v<A, Battle>) {
            result.possible_to_run_away = area_->possibleToRunAway();
            result.transition = area_->battleEndLoseBgTransition(); result.transition_speed = area_->battleEndLoseBgTransitionSpeed();
            result.do_anime = area_->battleEndLoseBgDoAnime(); result.ignore_effect_booster = area_->battleEndLoseBgIgnoreEffectBooster();
            result.music = area_->music();
            result.music_reference = std::make_shared<usecounter::PathUser>();
            if (counter) result.music_reference->setUseCounter(counter);
            result.music_reference->path(result.music);
            result.music_volume = area_->volume();
            result.music_loop_count = area_->loopCount();
            result.music_fade_in = area_->fadeIn();
            result.continue_bgm = area_->continueBGM();
        }
        // DのUndoDeleteはdupを保存し、Undoのたびにさらにdupして挿入する。
        // 切り離されたカードを保持するモードレス画面から、この履歴を編集できないようにする。
        if constexpr (UseCards) {
            result.deep_deleted_cards.resize(cardCount(), false);
            const auto freeze = [&](auto& saved, auto& identities) {
                using Saved = typename std::decay_t<decltype(saved)>::value_type::element_type;
                for (const auto index : detail::normalizedSelection<C>(deleted_cards, saved.size())) {
                    auto copy = std::dynamic_pointer_cast<Saved>(identities[index]->dup());
                    if (counter) copy->setUseCounter(counter, counter_owner);
                    saved[index] = std::move(copy);
                    identities[index].reset();
                    result.deep_deleted_cards[index] = true;
                }
            };
            if constexpr (std::is_same_v<A, Area>) freeze(result.menu_cards, result.menu_identities);
            if constexpr (std::is_same_v<A, Battle>) freeze(result.enemy_cards, result.enemy_identities);
        }
        if constexpr (UseBacks)
            for (const auto index : detail::normalizedSelection<BgImage>(deleted_backs, result.back_identities.size()))
                result.back_identities[index].reset();
        return result;
    }
    void restore(const Snapshot& value, const bool preserve_visibility = false) {
        // DはcwxPathが削除対象へ解決できる間にdelを
        // 通知する。 C++の履歴は、その通知で場面が終了・
        // 編集された場合、スナップショットの復元を打ち切る。
        const auto before=snapshot();
        const bool reselect_peers=structureChanged(value);
        if(!removeStructureOutside(value))return;
        const bool was_syncing_bgm = syncing_bgm_;
        const std::weak_ptr<void> life=scene_view_lifetime_;
        const auto revision=++geometry_revision_;
        if constexpr(UseCards) {
        const auto restore_cards=[&](const auto& saved,const auto& identities) -> bool {
            for(std::size_t index=0;index<saved.size();++index) {
                auto target=index<identities.size()?identities[index]:typename std::decay_t<decltype(identities)>::value_type{};
                if(index < value.deep_deleted_cards.size() && value.deep_deleted_cards[index])
                    target=std::dynamic_pointer_cast<C>(saved[index]->dup());
                else if(!target)target=std::dynamic_pointer_cast<C>(saved[index]->shallowCopy());
                else target->shallowCopyFrom(*saved[index]);
                if(life.expired() || geometry_revision_!=revision)return false;
                const auto found=std::find(areaCardArray().begin(),areaCardArray().end(),target);
                const bool added=found==areaCardArray().end();
                const auto previous=added?cardCount():static_cast<std::size_t>(found-areaCardArray().begin());
                if(added)insertCard(index,target);
                else if(previous!=index) {
                    // 保持中の同じ対象を並べ替える際、共通のスナッ
                    // プショットで資源の削除・追加と誤分類しない。
                    area_->removeCard(previous);insertCard(index,target);
                }
                // DのUndoDeleteはカードを一つ復元するご
                // とに公開し、 全カードの後で背景の復元を始める。
                if(added && !notifyAddedIndices({index},{}))return false;
            }
            return true;
        };
        if constexpr(UseCards && std::is_same_v<A,Area>)if(!restore_cards(value.menu_cards,value.menu_identities))return;
        if constexpr(UseCards && std::is_same_v<A,Battle>)if(!restore_cards(value.enemy_cards,value.enemy_identities))return;
        }
        if constexpr (UseBacks && requires(A& owner) { owner.backs(); }) {
            for (std::size_t index = 0; index < value.backs.size(); ++index) {
                auto restored = index < value.back_identities.size()
                    ? value.back_identities[index] : std::shared_ptr<BgImage>{};
                if (!restored || !value.backs[index] ||
                    typeid(*restored) != typeid(*value.backs[index]))
                    restored = detail::cloneObject(value.backs[index]);
                else
                    restored->deepCopy(*value.backs[index]);
                if(life.expired() || geometry_revision_!=revision)return;
                const auto found=std::find(area_->backs().begin(),area_->backs().end(),restored);
                const bool added=found==area_->backs().end();
                const auto previous=added?backCount():static_cast<std::size_t>(found-area_->backs().begin());
                if(added)insertBack(index,restored);
                else if(previous!=index){area_->removeBgImage(previous);insertBack(index,restored);}
                if(added && !notifyAddedIndices({},{index}))return;
            }
        }
        detail::setSpAuto(*area_, value.sp_auto);
        // 上の資源通知が新しい操作へ切り替える場合がある。通知を伴わな
        // いこのモデル代入の間だけ BGM同期を有効にし、中断された復
        // 元が共有の選択部品の通知を抑止したままにしないようにする。
        if constexpr (std::is_same_v<A, Battle>) { syncing_bgm_ = true; area_->possibleToRunAway(value.possible_to_run_away); area_->battleEndLoseBgTransition(value.transition); area_->battleEndLoseBgTransitionSpeed(value.transition_speed); area_->battleEndLoseBgDoAnime(value.do_anime); area_->battleEndLoseBgIgnoreEffectBooster(value.ignore_effect_booster); area_->music(value.music_reference ? value.music_reference->path() : value.music); area_->volume(value.music_volume); area_->loopCount(value.music_loop_count); area_->fadeIn(value.music_fade_in); area_->continueBGM(value.continue_bgm); syncing_bgm_ = was_syncing_bgm; }
        if constexpr (UseCards) {
            selected_cards_ = view_cards_
                ? detail::normalizedSelection<C>(value.selected_cards, cardCount())
                : std::vector<std::size_t>{};
        } else {
            selected_cards_.clear();
        }
        if constexpr (UseBacks) {
            selected_backs_ = view_backs_
                ? detail::normalizedSelection<BgImage>(value.selected_backs, backCount())
                : std::vector<std::size_t>{};
        } else {
            selected_backs_.clear();
        }
        if constexpr(UseCards){const auto values=areaCardArray();card_check_keys_.assign(values.begin(),values.end());}
        if constexpr(UseBacks){const auto values=area_->backs();back_check_keys_.assign(values.begin(),values.end());}
        if (preserve_visibility) {
            // Dの移動履歴は現在の行を並べ替え、後からの個別の表示変更は
            // 戻さない。 現在のチェックをモデルの同一性で対応付け直す。
            card_checked_=before.card_checked;card_grayed_=before.card_grayed;
            back_checked_=before.back_checked;back_grayed_=before.back_grayed;
            if constexpr (UseCards) {
                auto keys=snapshotCardIdentities(before);
                detail::synchronizeChecks(areaCardArray(),keys,card_checked_,card_grayed_);
            }
            if constexpr (UseBacks) {
                auto keys=before.back_identities;
                detail::synchronizeChecks(area_->backs(),keys,back_checked_,back_grayed_);
            }
        } else {
            card_checked_=value.card_checked;card_grayed_=value.card_grayed;
            back_checked_=value.back_checked;back_grayed_=value.back_grayed;
        }
        rememberSceneSelectionKeys();
        // DのUndoDeleteはセルを再追加して参照数を更新するが、余分な
        // グループ・名前通知は出さない。 逆向きの削除では名前を無効化する。
        if(!notifyAddedStructure(before,true,false,false,false))return;
        rebaseSceneDragSnapshotsForRestoredSelection();
        refreshAfterModelChange();
        if(life.expired() || geometry_revision_!=revision)return;
        publishSceneChange(false,true);
        if (life.expired() || geometry_revision_ != revision) return;
        if constexpr (std::is_base_of_v<AbstractArea, A>) {
            if (reselect_peers && context_->scene_view_registry && area_)
                context_->scene_view_registry->restoreSelection(
                    static_cast<const AbstractArea*>(area_), this,
                    value.selected_cards, value.selected_backs);
        }
    }
    [[nodiscard]] static const auto& snapshotCardIdentities(const Snapshot& value) {
        if constexpr(std::is_same_v<C,MenuCard>)return value.menu_identities;
        else return value.enemy_identities;
    }
    [[nodiscard]] bool structureChanged(const Snapshot& before) const {
        if constexpr(UseCards){const auto values=areaCardArray();const auto& saved=snapshotCardIdentities(before);if(!std::equal(saved.begin(),saved.end(),values.begin(),values.end()))return true;}
        if constexpr(UseBacks){const auto values=area_->backs();if(!std::equal(before.back_identities.begin(),before.back_identities.end(),values.begin(),values.end()))return true;}
        return false;
    }
    [[nodiscard]] bool removeStructureOutside(const Snapshot& target) {
        const bool scenario=summary_ && !summary_->scenarioPath().empty();
        const std::weak_ptr<void> life=scene_view_lifetime_;
        const auto revision=geometry_revision_;
        const auto card_notify=context_->del_menu_card,back_notify=context_->del_bg_image;
        const auto retire_local_back=context_->retire_local_bg_image;
        if constexpr(UseCards) {
            const auto& retained=snapshotCardIdentities(target);
            for(auto index=cardCount();index>0;--index) {
                const auto current=areaCardPtr(index-1);
                if(std::find(retained.begin(),retained.end(),current)!=retained.end())continue;
                const auto path=current->cwxPath(true);
                if(scenario && card_notify)card_notify(path);
                if(life.expired() || geometry_revision_!=revision)return false;
                area_->removeCard(index-1);
            }
        }
        if constexpr(UseBacks)for(auto index=backCount();index>0;--index) {
            const auto current=backPtr(index-1);
            if(std::find(target.back_identities.begin(),target.back_identities.end(),current)!=target.back_identities.end())continue;
            const auto path=current->cwxPath(true);
            // 未保存のSummaryに全体のdel通知用パスがなくても、
            // Dと同じく所有者のモードレスセル編集画面をキャンセルする。
            if(retire_local_back)retire_local_back(path);
            if(life.expired() || geometry_revision_!=revision)return false;
            if(scenario && back_notify)back_notify(path);
            if(life.expired() || geometry_revision_!=revision)return false;
            area_->removeBgImage(index-1);
        }
        return true;
    }
    [[nodiscard]] bool notifyAddedStructure(const Snapshot& before,const bool update_count=true,
                                            const bool name_additions=true,
                                            const bool emit_additions=true,
                                            const bool emit_legacy=true) {
        if(!summary_ || summary_->scenarioPath().empty())return true;
        const std::weak_ptr<void> life=scene_view_lifetime_;
        const auto revision=geometry_revision_;
        // 旧公開コンテキストにはref通知しかないため、挿入通知を互
        // 換用の代替として残す。 通常のMainWindowはDの独
        // 立したadd通知を渡し、重複したref通知は受け取らない。
        const auto card_notify=context_->add_menu_card?context_->add_menu_card:context_->ref_menu_card;
        const auto back_notify=context_->add_bg_image?context_->add_bg_image:context_->ref_bg_image;
        const auto groups_notify=context_->ref_card_groups,names_notify=context_->ref_cell_names;
        const auto count_notify=context_->ref_use_count;
        bool cards_removed{},backs_removed{};
        std::vector<std::pair<std::size_t,std::size_t>> reordered_cards;
        const auto order_notify = context_->ref_menu_card_indices;
        std::vector<std::string> cards,backs;
        if constexpr(UseCards) {
            const auto& identities=snapshotCardIdentities(before);
            if (identities.size() == areaCardArray().size() && !std::equal(identities.begin(),identities.end(),areaCardArray().begin(),areaCardArray().end())) {
                for (std::size_t destination=0; destination<areaCardArray().size(); ++destination) {
                    const auto old=std::find(identities.begin(), identities.end(), areaCardArray()[destination]);
                    if (old==identities.end()) { reordered_cards.clear(); break; }
                    reordered_cards.emplace_back(static_cast<std::size_t>(old-identities.begin()), destination);
                }
            }
            cards_removed=std::any_of(identities.begin(),identities.end(),[&](const auto& old) {
                return std::find(areaCardArray().begin(),areaCardArray().end(),old)==areaCardArray().end();
            });
            for(const auto& current:areaCardArray())
                if(std::find(identities.begin(),identities.end(),current)==identities.end())
                    cards.push_back(current->cwxPath(true));
        }
        if constexpr(UseBacks) {
            backs_removed=std::any_of(before.back_identities.begin(),before.back_identities.end(),[&](const auto& old) {
                return std::find(area_->backs().begin(),area_->backs().end(),old)==area_->backs().end();
            });
            for(const auto& current:area_->backs())
                if(std::find(before.back_identities.begin(),before.back_identities.end(),current)==before.back_identities.end())
                    backs.push_back(current->cwxPath(true));
        }
        if(emit_additions || (emit_legacy && !context_->add_menu_card))for(const auto& path:cards) {
            if(card_notify)card_notify(path);
            if(life.expired() || geometry_revision_!=revision)return false;
        }
        if(emit_additions || (emit_legacy && !context_->add_bg_image))for(const auto& path:backs) {
            if(back_notify)back_notify(path);
            if(life.expired() || geometry_revision_!=revision)return false;
        }
        // 通常の上下移動とそのUndoは、位置による
        // 並べ替えと同じ既存の一括順序通知を使う。
        if constexpr(UseCards) if(!reordered_cards.empty() && order_notify)order_notify(area_,reordered_cards);
        if(life.expired() || geometry_revision_!=revision)return false;
        if((cards_removed || (name_additions && !cards.empty())) && groups_notify)groups_notify();
        if(life.expired() || geometry_revision_!=revision)return false;
        if((backs_removed || (name_additions && !backs.empty())) && names_notify)names_notify();
        if(life.expired() || geometry_revision_!=revision)return false;
        if(update_count && (cards_removed || backs_removed || !cards.empty() || !backs.empty()) && count_notify)count_notify();
        return !life.expired() && geometry_revision_==revision;
    }
    [[nodiscard]] bool notifyAddedIndices(const std::vector<std::size_t>& cards,
                                          const std::vector<std::size_t>& backs,
                                          const bool allow_legacy=true) {
        if(!summary_ || summary_->scenarioPath().empty())return true;
        const std::weak_ptr<void> life=scene_view_lifetime_;
        const auto revision=geometry_revision_;
        const auto card_notify=context_->add_menu_card?context_->add_menu_card:
            allow_legacy?context_->ref_menu_card:std::function<void(std::string_view)>{};
        const auto back_notify=context_->add_bg_image?context_->add_bg_image:
            allow_legacy?context_->ref_bg_image:std::function<void(std::string_view)>{};
        std::vector<std::string> card_paths,back_paths;
        if constexpr(UseCards)for(const auto index:cards)
            if(index<cardCount())card_paths.push_back(areaCard(index).cwxPath(true));
        if constexpr(UseBacks)for(const auto index:backs)
            if(index<backCount())back_paths.push_back(back(index).cwxPath(true));
        for(const auto& path:card_paths) {
            if(card_notify)card_notify(path);
            if(life.expired() || geometry_revision_!=revision)return false;
        }
        for(const auto& path:back_paths) {
            if(back_notify)back_notify(path);
            if(life.expired() || geometry_revision_!=revision)return false;
        }
        return true;
    }
    [[nodiscard]] bool notifySceneUseCount() {
        if(!summary_ || summary_->scenarioPath().empty())return true;
        const std::weak_ptr<void> life=scene_view_lifetime_;
        const auto revision=geometry_revision_;
        const auto notify=context_->ref_use_count;
        if(notify)notify();
        return !life.expired() && geometry_revision_==revision;
    }
    void excludeAddedReferences(const Snapshot& before,std::vector<std::size_t>& cards,
                                std::vector<std::size_t>& backs) const {
        if constexpr(UseCards) {
            const auto& identities=snapshotCardIdentities(before);
            std::erase_if(cards,[&](const auto index) {
                return index>=cardCount() || std::find(identities.begin(),identities.end(),areaCardPtr(index))==identities.end();
            });
        }
        if constexpr(UseBacks)std::erase_if(backs,[&](const auto index) {
            return index>=backCount() || std::find(before.back_identities.begin(),before.back_identities.end(),backPtr(index))==before.back_identities.end();
        });
    }
    void notifyReferencedResources(const std::vector<std::size_t>& cards_value,
                                   const std::vector<std::size_t>& backs_value,
                                   const bool update_use_count = false,
                                   const bool guard_references = true) {
        const bool scenario=summary_ && !summary_->scenarioPath().empty();
        if (guard_references && !scenario) return;
        const std::weak_ptr<void> life=scene_view_lifetime_;
        const auto revision=geometry_revision_;
        const auto card_notify=context_->ref_menu_card,back_notify=context_->ref_bg_image;
        const auto use_notify=context_->ref_use_count;
        std::vector<std::string> card_paths,back_paths;
        if constexpr(UseCards)for(const auto index:detail::normalizedSelection<C>(cards_value,cardCount()))
            card_paths.push_back(const_cast<C&>(areaCard(index)).cwxPath(true));
        if constexpr(UseBacks)for(const auto index:detail::normalizedSelection<BgImage>(backs_value,backCount()))
            back_paths.push_back(back(index).cwxPath(true));
        // 通知前にパスとコールバックを取得し、受信先の破棄や新しい配
        // 置変更があれば打ち切る。 DではGCが受信先の寿命を保つ。
        for(const auto& path:card_paths) {
            if(card_notify)card_notify(path);
            if(life.expired() || geometry_revision_!=revision)return;
        }
        for(const auto& path:back_paths) {
            if(back_notify)back_notify(path);
            if(life.expired() || geometry_revision_!=revision)return;
        }
        if(update_use_count && scenario && use_notify)use_notify();
    }
    [[nodiscard]] static bool sameAreaSettings(const Snapshot& a, const Snapshot& b) {
        return a.sp_auto == b.sp_auto && a.possible_to_run_away == b.possible_to_run_away &&
            a.transition == b.transition && a.transition_speed == b.transition_speed &&
            a.do_anime == b.do_anime && a.ignore_effect_booster == b.ignore_effect_booster &&
            a.music == b.music && a.music_volume == b.music_volume &&
            a.music_loop_count == b.music_loop_count && a.music_fade_in == b.music_fade_in &&
            a.continue_bgm == b.continue_bgm;
    }
    void restoreAreaSettingsDelta(
        const Snapshot& saved, const Snapshot& other,
        const std::vector<std::size_t>& changed_cards = {},
        const std::vector<std::size_t>& changed_backs = {}) {
        // DのUndoSPAuto/UndoMusic/UndoPossibleToRunAway/UndoBattleOtherSettings
        // は、別々の設定群を所有する。 音楽のUndoでカード・背
        // 景・選択や、別のビューが変えた無関係な設定を復元しない。
        const std::weak_ptr<void> life=scene_view_lifetime_;
        const auto revision=++geometry_revision_;
        const auto valid=[&] { return !life.expired() && geometry_revision_==revision; };
        if (saved.sp_auto!=other.sp_auto) {
            detail::setSpAuto(*area_,saved.sp_auto);
            if (!valid()) return;
        }
        if constexpr (std::is_same_v<A,Battle>) {
            if (saved.possible_to_run_away!=other.possible_to_run_away) {
                area_->possibleToRunAway(saved.possible_to_run_away);
                if (!valid()) return;
            }
            if (saved.transition!=other.transition || saved.transition_speed!=other.transition_speed ||
                saved.do_anime!=other.do_anime || saved.ignore_effect_booster!=other.ignore_effect_booster) {
                area_->battleEndLoseBgTransition(saved.transition);
                if (!valid()) return;
                area_->battleEndLoseBgTransitionSpeed(saved.transition_speed);
                if (!valid()) return;
                area_->battleEndLoseBgDoAnime(saved.do_anime);
                if (!valid()) return;
                area_->battleEndLoseBgIgnoreEffectBooster(saved.ignore_effect_booster);
                if (!valid()) return;
            }
            if (saved.music!=other.music || saved.music_volume!=other.music_volume ||
                saved.music_loop_count!=other.music_loop_count || saved.music_fade_in!=other.music_fade_in ||
                saved.continue_bgm!=other.continue_bgm) {
                const bool was_syncing=syncing_bgm_;
                syncing_bgm_=true;
                const auto assign=[&] {
                    area_->music(saved.music_reference ? saved.music_reference->path() : saved.music);
                    if (!valid()) return;
                    area_->volume(saved.music_volume);
                    if (!valid()) return;
                    area_->loopCount(saved.music_loop_count);
                    if (!valid()) return;
                    area_->fadeIn(saved.music_fade_in);
                    if (!valid()) return;
                    area_->continueBGM(saved.continue_bgm);
                };
                try {
                    assign();
                } catch (...) {
                    if (!life.expired()) syncing_bgm_=was_syncing;
                    throw;
                }
                if (life.expired()) return;
                syncing_bgm_=was_syncing;
                if (!valid()) return;
            }
        }
        refreshAfterModelChange();
        if (!valid()) return;
        notifyReferencedResources(changed_cards, changed_backs, true);
        if (!valid()) return;
        publishSceneChange(false,true);
    }
    [[nodiscard]] std::function<void(const Snapshot&,const Snapshot&)>
    detachedSettingsReplay() const {
        if constexpr (std::is_same_v<A,Area> || std::is_same_v<A,Battle>) {
            const auto model=std::dynamic_pointer_cast<A>(area_->weak_from_this().lock());
            const auto summary=summary_ ? summary_->weak_from_this().lock() : nullptr;
            if (!model || !summary) return {};
            const std::weak_ptr<areawindow::SceneViewRegistry> registry=context_->scene_view_registry;
            const auto resources=context_->history_resource_refresh;
            return [model,summary,registry,resources](const Snapshot& saved,const Snapshot& other) {
                const auto& owners=[&]() -> const auto& {
                    if constexpr (std::is_same_v<A,Area>) return summary->areas();
                    else return summary->battles();
                }();
                if (std::find(owners.begin(),owners.end(),model)==owners.end())
                    throw std::logic_error("Scene settings history owner is no longer in its Summary");
                if (saved.sp_auto!=other.sp_auto) detail::setSpAuto(*model,saved.sp_auto);
                if constexpr (std::is_same_v<A,Battle>) {
                    if (saved.possible_to_run_away!=other.possible_to_run_away)
                        model->possibleToRunAway(saved.possible_to_run_away);
                    if (saved.transition!=other.transition || saved.transition_speed!=other.transition_speed ||
                        saved.do_anime!=other.do_anime || saved.ignore_effect_booster!=other.ignore_effect_booster) {
                        model->battleEndLoseBgTransition(saved.transition);
                        model->battleEndLoseBgTransitionSpeed(saved.transition_speed);
                        model->battleEndLoseBgDoAnime(saved.do_anime);
                        model->battleEndLoseBgIgnoreEffectBooster(saved.ignore_effect_booster);
                    }
                    if (saved.music!=other.music || saved.music_volume!=other.music_volume ||
                        saved.music_loop_count!=other.music_loop_count || saved.music_fade_in!=other.music_fade_in ||
                        saved.continue_bgm!=other.continue_bgm) {
                        model->music(saved.music_reference ? saved.music_reference->path() : saved.music);
                        model->volume(saved.music_volume);
                        model->loopCount(saved.music_loop_count);
                        model->fadeIn(saved.music_fade_in);
                        model->continueBGM(saved.continue_bgm);
                    }
                }
                if (resources && !summary->scenarioPath().empty()) resources({},{});
                if (const auto live_registry=registry.lock())
                    live_registry->publish(model.get(),nullptr,false,true);
            };
        }
        return {};
    }
    using DetachedCellsReplay=std::function<void(const Snapshot&,
        const std::vector<std::size_t>&,const std::vector<std::size_t>&)>;
    using DetachedBackReplay =
        std::function<void(const std::shared_ptr<BgImage>&)>;
    [[nodiscard]] DetachedCellsReplay detachedCellsReplay() const {
        if constexpr (std::is_same_v<A,Area> || std::is_same_v<A,Battle>) {
            const auto model=std::dynamic_pointer_cast<A>(area_->weak_from_this().lock());
            const auto summary=summary_ ? summary_->weak_from_this().lock() : nullptr;
            if (!model || !summary) return {};
            const std::weak_ptr<areawindow::SceneViewRegistry> registry=context_->scene_view_registry;
            const auto resources=context_->history_resource_refresh;
            return [model,summary,registry,resources](const Snapshot& saved,
                    const std::vector<std::size_t>& cards,const std::vector<std::size_t>& backs) {
                const auto& owners=[&]() -> const auto& {
                    if constexpr (std::is_same_v<A,Area>) return summary->areas();
                    else return summary->battles();
                }();
                if (std::find(owners.begin(),owners.end(),model)==owners.end())
                    throw std::logic_error("Scene cell history owner is no longer in its Summary");
                const auto& values=[&]() -> const auto& {
                    if constexpr (std::is_same_v<A,Area>) return saved.menu_cards;
                    else return saved.enemy_cards;
                }();
                const auto& identities=[&]() -> const auto& {
                    if constexpr (std::is_same_v<A,Area>) return saved.menu_identities;
                    else return saved.enemy_identities;
                }();
                if constexpr (UseCards) for (const auto index:cards)
                    if (index>=values.size() || index>=identities.size() || !values[index] ||
                        !identities[index] || std::find(model->cards().begin(),model->cards().end(),identities[index])==model->cards().end())
                        throw std::logic_error("Scene cell history card is no longer in its owner");
                if constexpr (UseBacks) for (const auto index:backs)
                    if (index>=saved.backs.size() || index>=saved.back_identities.size() ||
                        !saved.backs[index] || !saved.back_identities[index] ||
                        typeid(*saved.backs[index])!=typeid(*saved.back_identities[index]) ||
                        std::find(model->backs().begin(),model->backs().end(),saved.back_identities[index])==model->backs().end())
                        throw std::logic_error("Scene cell history background is no longer in its owner");
                std::vector<std::string> card_paths,back_paths;
                if constexpr (UseCards) for (const auto index:cards) {
                    const auto target=identities[index];
                    target->shallowCopyFrom(*values[index]);
                    card_paths.push_back(target->cwxPath(true));
                }
                if constexpr (UseBacks) for (const auto index:backs) {
                    const auto target=saved.back_identities[index];
                    target->deepCopy(*saved.backs[index]);
                    back_paths.push_back(target->cwxPath(true));
                }
                if (resources && !summary->scenarioPath().empty()) resources(card_paths,back_paths);
                if (const auto live_registry=registry.lock())
                    live_registry->publish(model.get(),nullptr,false,true);
            };
        }
        return {};
    }
    // DのAreaのUndoEditは編集した背景値を保持し、現在
    // の同じ所有者のビュー経由で再生する。 編集元を閉じた後は複製
    // した場面が共有履歴を持つ場合もあるため、この経路を維持する。
    [[nodiscard]] DetachedBackReplay detachedBackReplay(
        const std::shared_ptr<BgImage>& target) const {
        if constexpr (UseBacks &&
                      (std::is_same_v<A, Area> || std::is_same_v<A, Battle>)) {
            const auto replay = detachedCellsReplay();
            const auto index = backIndex(target);
            if (!target || !index || !replay) return {};
            return [replay, target, index = *index](
                       const std::shared_ptr<BgImage>& value) {
                if (!value) return;
                Snapshot saved;
                saved.backs.reserve(index + 1U);
                while (saved.backs.size() <= index) saved.backs.push_back({});
                saved.back_identities.resize(index + 1U);
                saved.backs[index] = value;
                saved.back_identities[index] = target;
                replay(saved, {}, {index});
            };
        }
        return {};
    }

    void restoreEditedCells(const Snapshot& saved,
                            const std::vector<std::size_t>& saved_cards,
                            const std::vector<std::size_t>& saved_backs) {
        // DのUndoEditと同じく、選んだセルだけを保存する。 無関
        // 係なセル・場面設定・行順・各ビュー独自の選択は置き換えない。
        const std::weak_ptr<void> life=scene_view_lifetime_;
        const auto revision=++geometry_revision_;
        std::vector<std::size_t> cards, backs;
        if constexpr (UseCards) {
            const auto restore_cards=[&](const auto& values,const auto& identities) {
                for (const auto index : saved_cards) {
                    if (index>=values.size() || index>=identities.size() || !values[index] || !identities[index]) continue;
                    const auto target=identities[index];
                    const auto found=std::find(areaCardArray().begin(),areaCardArray().end(),target);
                    if (found==areaCardArray().end()) continue;
                    cards.push_back(static_cast<std::size_t>(found-areaCardArray().begin()));
                    target->shallowCopyFrom(*values[index]);
                    if (life.expired() || geometry_revision_!=revision) return false;
                }
                return true;
            };
            if constexpr (std::is_same_v<A,Area>) {
                if (!restore_cards(saved.menu_cards,saved.menu_identities)) return;
            } else if constexpr (std::is_same_v<A,Battle>) {
                if (!restore_cards(saved.enemy_cards,saved.enemy_identities)) return;
            }
        }
        if constexpr (UseBacks) {
            for (const auto index : saved_backs) {
                if (index>=saved.backs.size() || index>=saved.back_identities.size()) continue;
                const auto target=saved.back_identities[index];
                const auto& value=saved.backs[index];
                if (!target || !value || typeid(*target)!=typeid(*value)) continue;
                const auto current=backIndex(target);
                if (!current) continue;
                backs.push_back(*current);
                target->deepCopy(*value);
                if (life.expired() || geometry_revision_!=revision) return;
            }
        }
        refreshAfterModelChange();
        if (life.expired() || geometry_revision_!=revision) return;
        notifyReferencedResources(cards,backs,true);
        if (life.expired() || geometry_revision_!=revision) return;
        publishSceneChange(false,true);
    }
    bool restoreWithReferenceNotifications(const Snapshot& value,
                                          const std::vector<std::size_t>& cards_value,
                                          const std::vector<std::size_t>& backs_value) {
        const std::weak_ptr<void> life=scene_view_lifetime_;
        const auto revision=geometry_revision_+1;
        const auto before=snapshot();
        restore(value);
        if(life.expired() || geometry_revision_!=revision)return false;
        if(structureChanged(before))return true; // 構造の復元ではadd/del/name/countを通知済み。D
        // のUndoEditと同じく、 資源を持つスナップショットの交換後
        // は参照数を更新する。名前・マスクの直接編集はパスだけを更新する。
        notifyReferencedResources(cards_value, backs_value, true);
        return !life.expired() && geometry_revision_ == revision;
    }
    [[nodiscard]] std::function<void(const Snapshot&)>
    detachedStructureReplay() const {
        if constexpr (std::is_same_v<A,Area> || std::is_same_v<A,Battle>) {
            const auto model=std::dynamic_pointer_cast<A>(area_->weak_from_this().lock());
            const auto summary=summary_ ? summary_->weak_from_this().lock() : nullptr;
            if (!model || !summary) return {};
            const std::weak_ptr<areawindow::SceneViewRegistry> registry=context_->scene_view_registry;
            const auto resources=context_->history_resource_refresh;
            const auto add_card=context_->add_menu_card ? context_->add_menu_card : context_->ref_menu_card;
            const auto del_card=context_->del_menu_card;
            const auto add_back=context_->add_bg_image ? context_->add_bg_image : context_->ref_bg_image;
            const auto del_back=context_->del_bg_image;
            const auto refresh_groups=context_->ref_card_groups;
            const auto refresh_names=context_->ref_cell_names;
            const auto refresh_indices=context_->history_card_indices_refresh
                ? context_->history_card_indices_refresh : context_->ref_menu_card_indices;
            return [model,summary,registry,resources,add_card,del_card,add_back,del_back,
                    refresh_groups,refresh_names,refresh_indices](const Snapshot& saved) {
                const auto& owners=[&]() -> const auto& {
                    if constexpr (std::is_same_v<A,Area>) return summary->areas();
                    else return summary->battles();
                }();
                if (std::find(owners.begin(),owners.end(),model)==owners.end())
                    throw std::logic_error("Scene structure history owner is no longer in its Summary");

                std::vector<std::string> added_cards,removed_cards,added_backs,removed_backs;
                const auto& card_values=[&]() -> const auto& {
                    if constexpr (std::is_same_v<A,Area>) return saved.menu_cards;
                    else return saved.enemy_cards;
                }();
                const auto& card_identities=[&]() -> const auto& {
                    if constexpr (std::is_same_v<A,Area>) return saved.menu_identities;
                    else return saved.enemy_identities;
                }();
                using SavedCard=typename std::decay_t<decltype(card_values)>::value_type::element_type;
                using CardPtrType=std::shared_ptr<SavedCard>;
                const std::vector<CardPtrType> before_cards(
                    model->cards().begin(),model->cards().end());
                std::vector<CardPtrType> desired_cards;
                if constexpr (UseCards) {
                    desired_cards.reserve(card_values.size());
                    for (std::size_t index=0; index<card_values.size(); ++index) {
                        if (!card_values[index])
                            throw std::logic_error("Scene structure history has an empty card snapshot");
                        CardPtrType target=index<card_identities.size()
                            ? card_identities[index] : CardPtrType{};
                        if (index<saved.deep_deleted_cards.size() && saved.deep_deleted_cards[index])
                            target=std::dynamic_pointer_cast<SavedCard>(card_values[index]->dup());
                        else if (!target)
                            target=std::dynamic_pointer_cast<SavedCard>(card_values[index]->shallowCopy());
                        else
                            target->shallowCopyFrom(*card_values[index]);
                        if (!target)
                            throw std::logic_error("Scene structure history could not restore a card");
                        desired_cards.push_back(std::move(target));
                    }
                    for (std::size_t index=model->cards().size(); index>0; --index) {
                        const auto current=model->cards()[index-1];
                        if (std::find(desired_cards.begin(),desired_cards.end(),current)!=desired_cards.end()) continue;
                        const auto path=current->cwxPath(true);
                        if (!summary->scenarioPath().empty() && del_card) del_card(path);
                        model->removeCard(index-1);
                        removed_cards.push_back(path);
                    }
                    for (std::size_t index=0; index<desired_cards.size(); ++index) {
                        const auto& target=desired_cards[index];
                        const auto found=std::find(model->cards().begin(),model->cards().end(),target);
                        if (found==model->cards().end()) {
                            if constexpr (std::is_same_v<A,Area>) model->insert(index,std::dynamic_pointer_cast<MenuCard>(target));
                            else model->insert(index,std::dynamic_pointer_cast<EnemyCard>(target));
                            added_cards.push_back(target->cwxPath(true));
                        } else {
                            const auto previous=static_cast<std::size_t>(found-model->cards().begin());
                            if (previous!=index) {
                                model->removeCard(previous);
                                if constexpr (std::is_same_v<A,Area>) model->insert(index,std::dynamic_pointer_cast<MenuCard>(target));
                                else model->insert(index,std::dynamic_pointer_cast<EnemyCard>(target));
                            }
                        }
                    }
                }

                if constexpr (UseBacks) {
                    std::vector<std::shared_ptr<BgImage>> desired_backs;
                    desired_backs.reserve(saved.backs.size());
                    for (std::size_t index=0; index<saved.backs.size(); ++index) {
                        if (!saved.backs[index])
                            throw std::logic_error("Scene structure history has an empty background snapshot");
                        auto target=index<saved.back_identities.size()
                            ? saved.back_identities[index] : std::shared_ptr<BgImage>{};
                        if (!target || typeid(*target)!=typeid(*saved.backs[index]))
                            target=detail::cloneObject(saved.backs[index]);
                        else
                            target->deepCopy(*saved.backs[index]);
                        desired_backs.push_back(std::move(target));
                    }
                    for (std::size_t index=model->backs().size(); index>0; --index) {
                        const auto current=model->backs()[index-1];
                        if (std::find(desired_backs.begin(),desired_backs.end(),current)!=desired_backs.end()) continue;
                        const auto path=current->cwxPath(true);
                        if (!summary->scenarioPath().empty() && del_back) del_back(path);
                        model->removeBgImage(index-1);
                        removed_backs.push_back(path);
                    }
                    for (std::size_t index=0; index<desired_backs.size(); ++index) {
                        const auto& target=desired_backs[index];
                        const auto found=std::find(model->backs().begin(),model->backs().end(),target);
                        if (found==model->backs().end()) {
                            model->insert(index,target);
                            added_backs.push_back(target->cwxPath(true));
                        } else {
                            const auto previous=static_cast<std::size_t>(found-model->backs().begin());
                            if (previous!=index) {
                                model->removeBgImage(previous);
                                model->insert(index,target);
                            }
                        }
                    }
                }

                for (const auto& path:added_cards) if (!summary->scenarioPath().empty() && add_card) add_card(path);
                for (const auto& path:added_backs) if (!summary->scenarioPath().empty() && add_back) add_back(path);
                // DのUndoDeleteは逆向きの削除でだけ名前を更新する。 保
                // 存セルの再追加では、グループを再構成せず参照数だけを更新する。
                if (!removed_cards.empty() && refresh_groups) refresh_groups();
                if (!removed_backs.empty() && refresh_names) refresh_names();
                if (resources && !summary->scenarioPath().empty()) {
                    auto cards=added_cards; cards.insert(cards.end(),removed_cards.begin(),removed_cards.end());
                    auto backs=added_backs; backs.insert(backs.end(),removed_backs.begin(),removed_backs.end());
                    resources(cards,backs);
                }
                if constexpr (UseCards) {
                    std::vector<std::pair<std::size_t,std::size_t>> reordered;
                    for (std::size_t destination=0; destination<model->cards().size(); ++destination) {
                        const auto old=std::find(before_cards.begin(),before_cards.end(),model->cards()[destination]);
                        if (old!=before_cards.end()) {
                            const auto source=static_cast<std::size_t>(old-before_cards.begin());
                            if (source!=destination) reordered.emplace_back(source,destination);
                        }
                    }
                    if (!reordered.empty() && refresh_indices) refresh_indices(model.get(),reordered);
                }
                if (const auto live_registry=registry.lock()) {
                    live_registry->publish(model.get(),nullptr,false,true);
                    live_registry->restoreSelection(model.get(),nullptr,saved.selected_cards,saved.selected_backs);
                }
            };
        }
        return {};
    }
    [[nodiscard]] std::optional<std::size_t> backIndex(
        const std::shared_ptr<BgImage>& target) const {
        if constexpr (!UseBacks) return std::nullopt;
        for (std::size_t index = 0; index < backCount(); ++index)
            if (backPtr(index) == target) return index;
        return std::nullopt;
    }
    void restoreBackValue(const std::shared_ptr<BgImage>& target,
                          const std::shared_ptr<BgImage>& value) {
        if constexpr (!UseBacks) return;
        const auto index = backIndex(target);
        if (!index || !target || !value || typeid(*target) != typeid(*value))
            return;
        const std::weak_ptr<void> life=scene_view_lifetime_;
        const auto revision=++geometry_revision_;
        target->deepCopy(*value);
        refreshAfterModelChange();
        if(life.expired() || geometry_revision_!=revision)return;
        notifyReferencedResources({}, {*index}, true);
        if(life.expired() || geometry_revision_!=revision)return;
        publishSceneChange(false, true);
    }
    template <typename Function>
    bool mutateWithReferenceNotifications(std::vector<std::size_t> cards_value,
                                          std::vector<std::size_t> backs_value,
                                          Function&& function,
                                          const bool update_use_count = false,
                                          const bool guard_references = true,
                                          const bool name_additions = true,
                                          const bool additions_notified = false,
                                          const bool publish_scene_change = true,
                                          std::function<void()> after_redo = {},
                                          std::function<void()> after_model_mutation = {},
                                          std::function<void()> after_undo = {},
                                          const bool setting_only_history = false) {
        if (read_only_) return false;
        const std::weak_ptr<void> life=scene_view_lifetime_;
        const auto revision=++geometry_revision_;
        const auto before = snapshot();
        function();
        if(life.expired() || geometry_revision_!=revision)return false;
        if (after_model_mutation) after_model_mutation();
        if (life.expired() || geometry_revision_ != revision) return false;
        rememberSceneSelectionKeys();
        const auto after = snapshot();
        const bool structural=structureChanged(before);
        const bool cells_only=!structural && sameAreaSettings(before,after);
        const bool settings_only=!structural && !sameAreaSettings(before,after) &&
            (setting_only_history || (cards_value.empty() && backs_value.empty()));
        excludeAddedReferences(before,cards_value,backs_value);
        const auto history_cards=guard_references?cards_value:selected_cards_;
        const auto history_backs=guard_references?backs_value:selected_backs_;
        const auto history_owner=undo_owner_; // 共有所有のエントリーは保持し、生ポインターのエントリーは借用のままとする。
        const auto history=undo_;
        const auto detached_settings=settings_only ? detachedSettingsReplay() : std::function<void(const Snapshot&,const Snapshot&)>{};
        const auto detached_cells=cells_only ? detachedCellsReplay() : DetachedCellsReplay{};
        if (history)
            history->add(std::make_shared<detail::LambdaUndo>(
                [this, life, before, after, cells_only, settings_only, detached_settings, detached_cells, after_undo, cards_value=history_cards, backs_value=history_backs] {
                    if(life.expired()) {
                        if (settings_only && detached_settings) detached_settings(before,after);
                        else if (cells_only && detached_cells) detached_cells(before,cards_value,backs_value);
                        else throw std::logic_error("Scene history has no detached replay for this command");
                        return;
                    }
                    if (settings_only)
                        restoreAreaSettingsDelta(before,after,cards_value,backs_value);
                    else if (cells_only) restoreEditedCells(before,cards_value,backs_value);
                    else {
                        const bool restored = restoreWithReferenceNotifications(
                            before, cards_value, backs_value);
                        if (restored && !life.expired() && after_undo) after_undo();
                    }
                }, [this, life, after, before, cells_only, settings_only, detached_settings, detached_cells, after_redo, cards_value=history_cards, backs_value=history_backs] {
                    if(life.expired()) {
                        if (settings_only && detached_settings) detached_settings(after,before);
                        else if (cells_only && detached_cells) detached_cells(after,cards_value,backs_value);
                        else throw std::logic_error("Scene history has no detached replay for this command");
                        return;
                    }
                    if (settings_only)
                        restoreAreaSettingsDelta(after,before,cards_value,backs_value);
                    else if (cells_only) restoreEditedCells(after,cards_value,backs_value);
                    else {
                        // すべての通知後も取得時の版がビューを所有
                        // している場合だけ、復元はtrueを返す。
                        const bool restored = restoreWithReferenceNotifications(
                            after, cards_value, backs_value);
                        // DのUndoDeleteのRedoは、復元した追加を全場
                        // 面へ反映し、 その種類を表示している場面だけで選択する。
                        if (restored && !life.expired() && after_redo) after_redo();
                    }
                }));
        // 公開Undoコマンドは、このビューを終了したりdispose()から新しい編集を
        // 始めたりする場合がある。 管理オブジェクトを保持し、古い処理の継続は打ち切る。
        if(life.expired() || geometry_revision_!=revision)return false;
        if(!notifyAddedStructure(before,update_use_count,name_additions,!additions_notified))return false;
        notifyReferencedResources(cards_value, backs_value, update_use_count && !structural,guard_references);
        if(life.expired() || geometry_revision_!=revision)return false;
        if (publish_scene_change) publishSceneChange();
        return !life.expired() && geometry_revision_==revision;
    }
    template <typename Function>
    bool mutateSettingWithReferenceNotifications(
        std::vector<std::size_t> cards_value,
        std::vector<std::size_t> backs_value, Function&& function) {
        // Dの設定Undoは、変更した設定値だけを記録する。 資源更
        // 新の通知用には、呼び出し元から影響する行番号を渡してよい。
        return mutateWithReferenceNotifications(
            std::move(cards_value), std::move(backs_value),
            std::forward<Function>(function), false, true, true, false, true,
            {}, {}, {}, true);
    }
    template <typename Function>
    bool mutate(Function&& function, const std::vector<std::size_t>& deleted_cards = {},
                const std::vector<std::size_t>& deleted_backs = {},
                const bool preserve_visibility = false) {
        if (read_only_) return false;
        const std::weak_ptr<void> life=scene_view_lifetime_;
        const auto revision=++geometry_revision_;
        const auto before = snapshot(deleted_cards, deleted_backs);
        function();
        if(life.expired() || geometry_revision_!=revision)return false;
        rememberSceneSelectionKeys();
        const auto after = snapshot();
        const bool structural=structureChanged(before);
        const auto history_owner=undo_owner_; // 共有所有のエントリーは保持し、生ポインターのエントリーは借用のままとする。
        const auto history=undo_;
        const auto detached_structure=structural ? detachedStructureReplay()
            : std::function<void(const Snapshot&)>{};
        if (history)
            history->add(std::make_shared<detail::LambdaUndo>(
                [this,life,before,preserve_visibility,structural,detached_structure] {
                    if(life.expired()) {
                        if(structural && detached_structure) detached_structure(before);
                        else if(structural) throw std::logic_error("Scene structure history has no detached replay");
                        return;
                    }
                    restore(before,preserve_visibility);
                },
                [this,life,after,preserve_visibility,structural,detached_structure] {
                    if(life.expired()) {
                        if(structural && detached_structure) detached_structure(after);
                        else if(structural) throw std::logic_error("Scene structure history has no detached replay");
                        return;
                    }
                    restore(after,preserve_visibility);
                }));
        // 公開Undoコマンドは、このビューを終了したりdispose()から新しい編集を
        // 始めたりする場合がある。 管理オブジェクトを保持し、古い処理の継続は打ち切る。
        if(life.expired() || geometry_revision_!=revision)return false;
        if(!notifyAddedStructure(before))return false;
        publishSceneChange();
        return !life.expired() && geometry_revision_==revision;
    }
    struct CardGeometry {
        int x{};
        int y{};
        std::uint32_t scale{};
        int layer{};
        std::shared_ptr<C> target;
        bool operator==(const CardGeometry&) const = default;
    };
    struct BackGeometry {
        int x{};
        int y{};
        int width{};
        int height{};
        int layer{};
        std::shared_ptr<BgImage> target;
        bool operator==(const BackGeometry&) const = default;
    };
    template <typename Geometry>
    static void rebaseSceneDragGeometryByIdentity(
        const std::vector<Geometry>& current,
        std::vector<Geometry>& start,
        std::vector<Geometry>& preview) {
        std::vector<Geometry> rebased_start;
        std::vector<Geometry> rebased_preview;
        rebased_start.reserve(current.size());
        rebased_preview.reserve(current.size());
        std::unordered_map<const void*, const Geometry*> start_by_target;
        std::unordered_map<const void*, const Geometry*> preview_by_target;
        start_by_target.reserve(start.size());
        preview_by_target.reserve(preview.size());
        for (const auto& value : start) start_by_target.emplace(value.target.get(), &value);
        for (const auto& value : preview) preview_by_target.emplace(value.target.get(), &value);
        for (const auto& value : current) {
            const auto old_start = start_by_target.find(value.target.get());
            const auto old_preview = preview_by_target.find(value.target.get());
            rebased_start.push_back(old_start == start_by_target.end() ? value : *old_start->second);
            rebased_preview.push_back(old_preview == preview_by_target.end() ? value : *old_preview->second);
        }
        start = std::move(rebased_start);
        preview = std::move(rebased_preview);
    }
    void rebaseSceneDragSnapshotsAfterAppend() {
        if (!scene_dragging_) return;
        const auto current_cards = captureCardGeometry();
        const auto current_backs = captureBackGeometry();
        std::shared_ptr<C> target_card;
        std::shared_ptr<BgImage> target_back;
        if (scene_drag_target_kind_ == SceneEntryKind::Card &&
            scene_drag_target_index_ < scene_drag_cards_.size())
            target_card = scene_drag_cards_[scene_drag_target_index_].target;
        if (scene_drag_target_kind_ == SceneEntryKind::Background &&
            scene_drag_target_index_ < scene_drag_backs_.size())
            target_back = scene_drag_backs_[scene_drag_target_index_].target;

        rebaseSceneDragGeometryByIdentity(current_cards, scene_drag_cards_,
                                           scene_drag_preview_cards_);
        rebaseSceneDragGeometryByIdentity(current_backs, scene_drag_backs_,
                                           scene_drag_preview_backs_);
        if (scene_drag_target_kind_ == SceneEntryKind::Card) {
            const auto found = std::find_if(current_cards.begin(), current_cards.end(),
                [&](const auto& value) { return value.target == target_card; });
            scene_drag_target_index_ = found == current_cards.end()
                ? std::numeric_limits<std::size_t>::max()
                : static_cast<std::size_t>(found - current_cards.begin());
        } else if (scene_drag_target_kind_ == SceneEntryKind::Background) {
            const auto found = std::find_if(current_backs.begin(), current_backs.end(),
                [&](const auto& value) { return value.target == target_back; });
            scene_drag_target_index_ = found == current_backs.end()
                ? std::numeric_limits<std::size_t>::max()
                : static_cast<std::size_t>(found - current_backs.begin());
        }

        // Dの選択解除は古いFlexImageをdragImgs
        // から外し、そのnewBoundsを描かなくする。 選択
        // 群は新しく入った画像の確定位置と、元のマウス位置を基準
        // に始める。 Dの解除済みnewRが残る不具合は修正し、
        // 操作後は確定モデルを正式な状態として、 見えない古い
        // 作業値がcanEditを無効にし続けないようにする。
        if constexpr (UseCards) for (std::size_t index = 0;
             index < std::min(cardCount(), card_images_.size()); ++index) {
            if (std::find(selected_cards_.begin(), selected_cards_.end(), index) !=
                selected_cards_.end()) continue;
            if (!card_images_[index]) continue;
            const auto& value = current_cards[index];
            card_images_[index]->x(value.x);
            card_images_[index]->y(value.y);
            card_images_[index]->scale(value.scale);
        }
        if constexpr (UseBacks) for (std::size_t index = 0;
             index < std::min(backCount(), back_images_.size()); ++index) {
            if (std::find(selected_backs_.begin(), selected_backs_.end(), index) !=
                selected_backs_.end()) continue;
            if (!back_images_[index]) continue;
            const auto& value = current_backs[index];
            back_images_[index]->x(value.x);
            back_images_[index]->y(value.y);
            back_images_[index]->width(value.width);
            back_images_[index]->height(value.height);
        }
    }
    // ポインターを押したままでも、Undoは選択中の画
    // 像を置き換えられる。 元の押下位置と移動量を保ち
    // つつ、復元後の対象の同一性へ操作を合わせ直す。
    void rebaseSceneDragSnapshotsForRestoredSelection() {
        if (!scene_dragging_) return;
        const auto preferred_kind = scene_drag_target_kind_;
        scene_drag_cards_ = captureCardGeometry();
        scene_drag_backs_ = captureBackGeometry();
        scene_drag_preview_cards_ = scene_drag_cards_;
        scene_drag_preview_backs_ = scene_drag_backs_;

        const auto select_card_target = [&] {
            if (selected_cards_.empty()) return false;
            scene_drag_target_kind_ = SceneEntryKind::Card;
            scene_drag_target_index_ = selected_cards_.front();
            return true;
        };
        const auto select_back_target = [&] {
            if (selected_backs_.empty()) return false;
            scene_drag_target_kind_ = SceneEntryKind::Background;
            scene_drag_target_index_ = selected_backs_.front();
            return true;
        };
        if (preferred_kind == SceneEntryKind::Card && select_card_target()) return;
        if (preferred_kind == SceneEntryKind::Background && select_back_target()) return;
        if (select_card_target() || select_back_target()) return;
        scene_drag_target_kind_ = SceneEntryKind::Decoration;
        scene_drag_target_index_ = std::numeric_limits<std::size_t>::max();
    }

    // DのFlexImage.fixed(true)と同じく、固定した画像だけをリセット
    // する。 無関係な画像種別の作業値は、Enter/Escapeで扱えるように残す。
    void discardKeyboardDraftsForFixedImages(const bool reset_cards,
                                             const bool reset_all_backs,
                                             const bool reset_first_full_back) {
        const auto has_pending = [](const auto& before, const auto& preview) {
            return std::any_of(before.begin(), before.end(), [&](const auto& start) {
                const auto draft = std::find_if(preview.begin(), preview.end(),
                    [&](const auto& value) { return value.target == start.target; });
                return draft != preview.end() && !(start == *draft);
            });
        };
        const auto live_cards = captureCardGeometry();
        if (reset_cards) {
            for (const auto& start : scene_keyboard_cards_before_) {
                const auto draft = std::find_if(scene_keyboard_preview_cards_.begin(),
                    scene_keyboard_preview_cards_.end(),
                    [&](const auto& value) { return value.target == start.target; });
                if (draft == scene_keyboard_preview_cards_.end() || start == *draft)
                    continue;
                const auto current = std::find_if(live_cards.begin(), live_cards.end(),
                    [&](const auto& value) { return value.target == start.target; });
                if (current == live_cards.end()) continue;
                const auto index = static_cast<std::size_t>(
                    std::distance(live_cards.begin(), current));
                if (index >= card_images_.size() || !card_images_[index]) continue;
                card_images_[index]->x(current->x);
                card_images_[index]->y(current->y);
                card_images_[index]->scale(current->scale);
            }
            scene_keyboard_cards_before_.clear();
            scene_keyboard_preview_cards_.clear();
        }

        const auto live_backs = captureBackGeometry();
        std::size_t first_full_back = backCount();
        if (reset_first_full_back && prop_) {
            const auto view = prop_->looks().viewSize();
            for (std::size_t index = 0; index < live_backs.size(); ++index) {
                const auto& value = live_backs[index];
                if (value.x == 0 && value.y == 0 &&
                    value.width == static_cast<int>(view.width) &&
                    value.height == static_cast<int>(view.height)) {
                    first_full_back = index;
                    break;
                }
            }
        }
        if (reset_all_backs || reset_first_full_back) {
            for (const auto& start : scene_keyboard_backs_before_) {
                const auto draft = std::find_if(scene_keyboard_preview_backs_.begin(),
                    scene_keyboard_preview_backs_.end(),
                    [&](const auto& value) { return value.target == start.target; });
                if (draft == scene_keyboard_preview_backs_.end() || start == *draft)
                    continue;
                const auto current = std::find_if(live_backs.begin(), live_backs.end(),
                    [&](const auto& value) { return value.target == start.target; });
                if (current == live_backs.end()) continue;
                const auto index = static_cast<std::size_t>(
                    std::distance(live_backs.begin(), current));
                if (!reset_all_backs && index != first_full_back) continue;
                if (index >= back_images_.size() || !back_images_[index]) continue;
                back_images_[index]->x(current->x);
                back_images_[index]->y(current->y);
                back_images_[index]->width(current->width);
                back_images_[index]->height(current->height);
            }
            if (reset_all_backs) {
                scene_keyboard_backs_before_.clear();
                scene_keyboard_preview_backs_.clear();
            } else if (first_full_back < live_backs.size()) {
                const auto fixed_target = live_backs[first_full_back].target;
                std::erase_if(scene_keyboard_backs_before_, [&](const auto& value) {
                    return value.target == fixed_target;
                });
                std::erase_if(scene_keyboard_preview_backs_, [&](const auto& value) {
                    return value.target == fixed_target;
                });
            }
        }
        if (!has_pending(scene_keyboard_cards_before_, scene_keyboard_preview_cards_) &&
            !has_pending(scene_keyboard_backs_before_, scene_keyboard_preview_backs_))
            discardSceneKeyboardDrafts();
    }
    // DのFlexImage.fixed(true)は、ポインタードラッグ中の値も
    // 含め、その画像の保留範囲を消す。 操作自体は続け、新たに固定したプレビュー
    // 項目だけを置き換え、 後のMouseUpで古い配置を確定しないようにする。
    void discardPointerDraftsForFixedImages(const bool reset_cards,
                                             const bool reset_all_backs,
                                             const bool reset_first_full_back) {
        if (!scene_dragging_) return;
        bool changed{};
        const auto live_cards = captureCardGeometry();
        if (reset_cards) {
            for (auto& draft : scene_drag_preview_cards_) {
                const auto current = std::find_if(live_cards.begin(), live_cards.end(),
                    [&](const auto& value) { return value.target == draft.target; });
                if (current == live_cards.end()) continue;
                const auto index = static_cast<std::size_t>(
                    std::distance(live_cards.begin(), current));
                draft = *current;
                if (index < card_images_.size() && card_images_[index]) {
                    card_images_[index]->x(current->x);
                    card_images_[index]->y(current->y);
                    card_images_[index]->scale(current->scale);
                }
                changed = true;
            }
        }

        const auto live_backs = captureBackGeometry();
        const auto reset_back = [&](const std::size_t index) {
            if (index >= live_backs.size()) return;
            const auto& current = live_backs[index];
            const auto draft = std::find_if(scene_drag_preview_backs_.begin(),
                scene_drag_preview_backs_.end(),
                [&](const auto& value) { return value.target == current.target; });
            if (draft == scene_drag_preview_backs_.end()) return;
            *draft = current;
            if (index < back_images_.size() && back_images_[index]) {
                back_images_[index]->x(current.x);
                back_images_[index]->y(current.y);
                back_images_[index]->width(current.width);
                back_images_[index]->height(current.height);
            }
            changed = true;
        };
        if (reset_all_backs) {
            for (std::size_t index = 0; index < live_backs.size(); ++index)
                reset_back(index);
        } else if (reset_first_full_back && prop_) {
            const auto view = prop_->looks().viewSize();
            for (std::size_t index = 0; index < live_backs.size(); ++index) {
                const auto& value = live_backs[index];
                if (value.x == 0 && value.y == 0 &&
                    value.width == static_cast<int>(view.width) &&
                    value.height == static_cast<int>(view.height)) {
                    reset_back(index);
                    break;
                }
            }
        }
        if (changed) requestSceneRedraw();
    }
    struct NumericPreview {
        GeometryField field;std::uint64_t revision;
        std::vector<CardGeometry> cards;std::vector<BackGeometry> backs;
        std::vector<std::size_t> selected_cards,selected_backs;
    };
    // Dはマウスキャプチャの有無でなく、実際の保留配置とモデルの配置を比較する。
    [[nodiscard]] bool sceneGeometryMoving() const noexcept {
        if (!scene_dragging_) return false;
        if constexpr (UseCards) {
            for (std::size_t index = 0;
                 index < std::min(cardCount(), scene_drag_cards_.size()); ++index) {
                const auto& before = scene_drag_cards_[index];
                if (index >= card_images_.size() || !card_images_[index]) continue;
                const auto& image = card_images_[index];
                const auto before_bounds = cardDragBounds(index, before);
                if (image->x() != before.x || image->y() != before.y ||
                    image->width() != before_bounds.width ||
                    image->height() != before_bounds.height) return true;
            }
        }
        if constexpr (UseBacks) {
            for (std::size_t index = 0;
                 index < std::min(backCount(), scene_drag_backs_.size()); ++index) {
                const auto& before = scene_drag_backs_[index];
                if (index >= back_images_.size() || !back_images_[index]) continue;
                const auto& image = back_images_[index];
                if (image->x() != before.x || image->y() != before.y ||
                    image->width() != before.width || image->height() != before.height)
                    return true;
            }
        }
        return false;
    }
    [[nodiscard]] std::vector<CardGeometry> captureCardGeometry() const {
        std::vector<CardGeometry> result;
        if constexpr (UseCards) {
            result.reserve(cardCount());
            for (std::size_t index = 0; index < cardCount(); ++index) {
                const auto value = areaCardPtr(index);
                result.push_back({value->x(), value->y(), value->scale(), value->layer(), value});
            }
        }
        return result;
    }
    [[nodiscard]] std::vector<BackGeometry> captureBackGeometry() const {
        std::vector<BackGeometry> result;
        if constexpr (UseBacks) {
            result.reserve(backCount());
            for (std::size_t index = 0; index < backCount(); ++index) {
                const auto value = backPtr(index);
                result.push_back({value->x(), value->y(), value->width(),
                                   value->height(), value->layer(), value});
            }
        }
        return result;
    }
    void restoreCardGeometry(const std::vector<CardGeometry>& geometry, const bool refresh=true) {
        if constexpr (UseCards) {
            for (std::size_t index = 0; index < geometry.size(); ++index) {
                const auto& item = geometry[index];
                const auto target=item.target;
                if (!target || std::find(areaCardArray().begin(),areaCardArray().end(),target)==areaCardArray().end()) continue;
                auto& value = *target;
                value.x(item.x);
                value.y(item.y);
                value.scale(item.scale);
                value.layer(item.layer);
            }
        }
        if(refresh)refreshGeometry();
    }
    void restoreBackGeometry(const std::vector<BackGeometry>& geometry, const bool refresh=true) {
        for (std::size_t index = 0; index < geometry.size(); ++index) {
            const auto& item = geometry[index];
            auto target=item.target;
            if(!target || !backIndex(target))continue;
            auto& value = *target;
            value.x(item.x);
            value.y(item.y);
            value.width(item.width);
            value.height(item.height);
            value.layer(item.layer);
        }
        if(refresh)refreshGeometry();
    }
    void restoreSceneSelection(const std::vector<std::size_t>& cards_value,
                               const std::vector<std::size_t>& backs_value) {
        // この補助関数を明示的に使うプレビュー中止・外部編集の
        // 再生・並べ替えは、選択復元を伴う。 確定した配置のUndoEdit
        // は、Dと同じく現在の行選択を維持する。
        if constexpr (UseCards)
            selected_cards_ = view_cards_ ? detail::normalizedSelection<C>(cards_value, cardCount()) : std::vector<std::size_t>{};
        else
            selected_cards_.clear();
        if constexpr (UseBacks)
            selected_backs_ = view_backs_ ? detail::normalizedSelection<BgImage>(backs_value, backCount()) : std::vector<std::size_t>{};
        else
            selected_backs_.clear();
        refreshSelected();
    }
    void cancelSceneGeometry(const std::vector<CardGeometry>& cards,
                             const std::vector<BackGeometry>& backs,
                             const std::vector<std::size_t>& selected_cards,
                             const std::vector<std::size_t>& selected_backs,
                             const bool publish, const bool notify_modification=true) {
        const std::weak_ptr<void> life=scene_view_lifetime_;
        const auto revision=++geometry_revision_;
        restoreCardGeometry(cards,false);restoreBackGeometry(backs,false);
        refreshGeometry(false, notify_modification);
        if(life.expired() || geometry_revision_!=revision)return;
        restoreSceneSelection(selected_cards,selected_backs);
        if(life.expired() || geometry_revision_!=revision)return;
        if(publish)publishSceneChange(true);
    }
    void cancelSceneDragPreview(const std::vector<std::size_t>& selected_cards,
                                const std::vector<std::size_t>& selected_backs) {
        const std::weak_ptr<void> life=scene_view_lifetime_;
        const auto revision=++geometry_revision_;
        // DのFlexImage.reset()と同じく、モデルに
        // 触れず保留配置を破棄する。 現在のモデル値からこのビュー
        // だけを更新し、操作中に他で行われた変更を取り消さない。
        refreshGeometry(false,false);
        if(life.expired() || geometry_revision_!=revision)return;
        restoreSceneSelection(selected_cards,selected_backs);
        if(life.expired() || geometry_revision_!=revision)return;
        requestSceneRedraw();
    }
    void commitSceneDragPreview(const std::vector<CardGeometry>& start_cards,
                                const std::vector<BackGeometry>& start_backs,
                                const std::vector<CardGeometry>& preview_cards,
                                const std::vector<BackGeometry>& preview_backs,
                                const std::vector<std::size_t>& selected_cards,
                                const std::vector<std::size_t>& selected_backs,
                                const bool dragged=true,
                                const bool keyboard_history=false) {
        const std::weak_ptr<void> life=scene_view_lifetime_;
        const auto before_cards=captureCardGeometry();
        const auto before_backs=captureBackGeometry();
        const auto same_card=[](const CardGeometry& left,const CardGeometry& right) {
            return left.x==right.x && left.y==right.y && left.scale==right.scale &&
                   left.layer==right.layer && left.target==right.target;
        };
        const auto same_back=[](const BackGeometry& left,const BackGeometry& right) {
            return left.x==right.x && left.y==right.y && left.width==right.width &&
                   left.height==right.height && left.layer==right.layer &&
                   left.target==right.target;
        };
        // 現在のモデルが操作開始時と一致する対象だけへ適
        // 用する。 無関係な同時編集は保ち、同じ対象へ
        // の別の編集は古い局所プレビューより優先する。
        const auto selected_card = [this, &selected_cards](const auto& target) {
            return std::any_of(selected_cards.begin(), selected_cards.end(),
                [&](const auto index) { return index < cardCount() && areaCardPtr(index) == target; });
        };
        const auto selected_back = [this, &selected_backs](const auto& target) {
            return std::any_of(selected_backs.begin(), selected_backs.end(),
                [&](const auto index) { return index < backCount() && backPtr(index) == target; });
        };
        if constexpr(UseCards)for(const auto& preview:preview_cards) {
            if (!selected_card(preview.target)) continue;
            const auto start=std::find_if(start_cards.begin(),start_cards.end(),
                [&](const auto& value){return value.target==preview.target;});
            const auto current=std::find_if(before_cards.begin(),before_cards.end(),
                [&](const auto& value){return value.target==preview.target;});
            if(start==start_cards.end() || current==before_cards.end() ||
               same_card(*start,preview) || !same_card(*start,*current))continue;
            preview.target->x(preview.x);preview.target->y(preview.y);
            preview.target->scale(preview.scale);
        }
        if constexpr(UseBacks)for(const auto& preview:preview_backs) {
            if (!selected_back(preview.target)) continue;
            const auto start=std::find_if(start_backs.begin(),start_backs.end(),
                [&](const auto& value){return value.target==preview.target;});
            const auto current=std::find_if(before_backs.begin(),before_backs.end(),
                [&](const auto& value){return value.target==preview.target;});
            if(start==start_backs.end() || current==before_backs.end() ||
               same_back(*start,preview) || !same_back(*start,*current))continue;
            preview.target->x(preview.x);preview.target->y(preview.y);
            preview.target->width(preview.width);preview.target->height(preview.height);
        }
        if(life.expired())return;
        // Undoには確定直前の値だけを記録し、操作開
        // 始後に行われた無関係なモデル編集は含めない。
        commitSceneGeometry(before_cards,before_backs,selected_cards,selected_backs,
                            dragged,keyboard_history);
    }
    template <typename Function>
    void mutateSceneGeometry(Function&& function) {
        if(read_only_)return;
        const std::weak_ptr<void> life=scene_view_lifetime_;
        const auto revision=++geometry_revision_;
        const auto before_cards=captureCardGeometry();const auto before_backs=captureBackGeometry();
        const auto selected_cards=detail::normalizedSelection<C>(selected_cards_,cardCount());
        const auto selected_backs=detail::normalizedSelection<BgImage>(selected_backs_,backCount());
        function();
        if(life.expired() || geometry_revision_!=revision)return;
        commitSceneGeometry(before_cards,before_backs,selected_cards,selected_backs);
    }
    void commitSceneGeometry(const std::vector<CardGeometry>& before_cards,
                             const std::vector<BackGeometry>& before_backs,
                             const std::vector<std::size_t>& selected_cards,
                             const std::vector<std::size_t>& selected_backs,
                             const bool dragged=false, const bool keyboard_history=false) {
        const std::weak_ptr<void> life=scene_view_lifetime_;
        const auto revision=++geometry_revision_;
        const auto after_cards=captureCardGeometry();const auto after_backs=captureBackGeometry();
        if(before_cards==after_cards && before_backs==after_backs)return;
        // DのUndoEditはSceneView全体でなく編集したセル
        // を保存する。 このコマンドが変えた配置だけを記録し、後から他
        // のビューで行った独立した編集をUndo/Redoで戻さない。
        std::vector<CardGeometry> changed_before_cards, changed_after_cards;
        std::vector<BackGeometry> changed_before_backs, changed_after_backs;
        const auto collect_changed=[](const auto& before, const auto& after,
                                      auto& saved_before, auto& saved_after) {
            for (const auto& old : before) {
                const auto found=std::find_if(after.begin(),after.end(),
                    [&](const auto& value){return value.target==old.target;});
                if (found!=after.end() && !(old==*found)) {
                    saved_before.push_back(old);saved_after.push_back(*found);
                }
            }
        };
        collect_changed(before_cards,after_cards,changed_before_cards,changed_after_cards);
        collect_changed(before_backs,after_backs,changed_before_backs,changed_after_backs);
        const auto model_owner=[&]() -> std::shared_ptr<void> {
            if constexpr(requires(A& owner){owner.weak_from_this().lock();})return area_->weak_from_this().lock();
            return {};
        }();
        // Area/Battleの履歴は、作成元の場面ペインを閉じても
        // 残す。 モデルの構造と、ホストの寿命を安全に扱う通知処理だけ
        // を保持し、ビュー・部品・コンテキストを強く保持しない。 軽量
        // な他のプレビュービューは、従来の借用所有者の寿命契約を保つ。
        const auto detached_geometry=[&]() -> std::function<void(
            const std::vector<CardGeometry>&,const std::vector<BackGeometry>&)> {
            if constexpr (std::is_same_v<A,Area> || std::is_same_v<A,Battle>) {
                const auto model=std::dynamic_pointer_cast<A>(area_->weak_from_this().lock());
                const auto summary=summary_ ? summary_->weak_from_this().lock() : nullptr;
                if (!model || !summary) return {};
                const std::weak_ptr<areawindow::SceneViewRegistry> registry=context_->scene_view_registry;
                const auto resources=context_->history_resource_refresh;
                return [model,summary,registry,resources](const auto& cards,const auto& backs) {
                    const auto& owners=[&]() -> const auto& {
                        if constexpr (std::is_same_v<A,Area>) return summary->areas();
                        else return summary->battles();
                    }();
                    if (std::find(owners.begin(),owners.end(),model)==owners.end())
                        throw std::logic_error("Scene history owner is no longer in its Summary");
                    // 変更前に保存した全対象を検証する。 古くなったコ
                    // マンドで、Undoの位置だけを黙って進めない。
                    if constexpr (UseCards) for (const auto& item:cards)
                        if (!item.target || std::find(model->cards().begin(),model->cards().end(),item.target)==model->cards().end())
                            throw std::logic_error("Scene history card is no longer in its owner");
                    if constexpr (UseBacks) for (const auto& item:backs)
                        if (!item.target || std::find(model->backs().begin(),model->backs().end(),item.target)==model->backs().end())
                            throw std::logic_error("Scene history background is no longer in its owner");
                    std::vector<std::string> card_paths,back_paths;
                    if constexpr (UseCards) for (const auto& item:cards) {
                        item.target->x(item.x); item.target->y(item.y);
                        item.target->scale(item.scale); item.target->layer(item.layer);
                        card_paths.push_back(item.target->cwxPath(true));
                    }
                    if constexpr (UseBacks) for (const auto& item:backs) {
                        item.target->x(item.x); item.target->y(item.y);
                        item.target->width(item.width); item.target->height(item.height);
                        item.target->layer(item.layer);
                        back_paths.push_back(item.target->cwxPath(true));
                    }
                    if (resources && !summary->scenarioPath().empty()) resources(card_paths,back_paths);
                    // DのUndoEditと同じく、Reselectせず現在の他のビューを更新する。
                    if (const auto live_registry=registry.lock())
                        live_registry->publish(model.get(),nullptr,false,true);
                };
            }
            return {};
        }();
        const auto restore=[this,life,model_owner,selected_cards,selected_backs,detached_geometry](auto cards,auto backs) {
            if(life.expired()) {
                if (detached_geometry) detached_geometry(cards,backs);
                else throw std::logic_error("Scene history author and owned replay model are unavailable");
                return;
            }
            const auto revision=++geometry_revision_;
            discardSceneKeyboardDrafts();
            restoreCardGeometry(cards,false);restoreBackGeometry(backs,false);
            refreshGeometry();
            if(life.expired() || geometry_revision_!=revision)return;
            // DのUndoEditは編集セルを再生するが、現在のカード・背景
            // の行選択は置き換えない。 ここでは通知対象の組だけを保持する。
            notifyReferencedResources(selected_cards,selected_backs,true);
            if(life.expired() || geometry_revision_!=revision)return;
            // DのUndoEditは全ビューのImagePaneを再構成し、 再生した
            // 配置を表示する前に、すべてのFlexImageの保留newRを破棄する。
            publishSceneChange(false, true);
            if(life.expired() || geometry_revision_!=revision)return;
            // DのUndoEditはReselectしないため、配置履歴
            // の再生中も、 他の各ビューは自分の有効な選択を維持する。

        };
        const auto history_owner=undo_owner_; // 共有所有のエントリーは保持し、生ポインターのエントリーは借用のままとする。
        const auto history=undo_;
        if(history && keyboard_history) {
            // DのredrawProcBeforeは、いずれの画像も確定
            // する前に、 変更画像ごとに同じ全選択のスナップショットを一
            // 件ずつ登録する。 共通の確定処理で、両ホストともこの件数
            // を保つ。交換方式のUndoEditでは、 先行するRedo
            // は無操作となり、最後の登録だけが確定配置を再適用する。
            const auto count = changed_before_cards.size() + changed_before_backs.size();
            using GeometryPair = std::pair<std::vector<CardGeometry>,std::vector<BackGeometry>>;
            const auto before = std::make_shared<const GeometryPair>(
                std::move(changed_before_cards),std::move(changed_before_backs));
            const auto after = std::make_shared<const GeometryPair>(
                std::move(changed_after_cards),std::move(changed_after_backs));
            // 不変の対象群を共有し、履歴項目ごとに配列を複製しない。
            for (std::size_t index = 0; index < count; ++index) {
                const auto redo = index + 1U == count ? after : before;
                history->add(std::make_shared<detail::LambdaUndo>(
                    [restore,before]{restore(before->first,before->second);},
                    [restore,redo]{restore(redo->first,redo->second);}));
                if(life.expired() || geometry_revision_!=revision)return;
            }
        } else if(history)history->add(std::make_shared<detail::LambdaUndo>(
            [restore,cards=std::move(changed_before_cards),backs=std::move(changed_before_backs)]{restore(cards,backs);},
            [restore,cards=std::move(changed_after_cards),backs=std::move(changed_after_backs)]{restore(cards,backs);}));
        // 公開Undoコマンドは、このビューを終了したりdispose()から新しい編集を
        // 始めたりする場合がある。 管理オブジェクトを保持し、古い処理の継続は打ち切る。
        if(life.expired() || geometry_revision_!=revision)return;
        refreshGeometry(dragged);
        if(life.expired() || geometry_revision_!=revision)return;
        if(dragged) {
            // 通常のドラッグ解放時の再転送は維持する。 バックエンド
            // 通知をまたぐ値は局所的に所有し、古い処理は打ち切る。
            if constexpr(UseCards)for(const auto index:selected_cards) {
                if(index>=cardCount() || index>=card_images_.size())continue;
                const auto card=areaCardPtr(index);
                auto image=createCardImage(*card);
                if(life.expired() || geometry_revision_!=revision)return;
                card_images_[index]=std::move(image);
                if(index<card_preview_images_.size()) {
                    auto preview=createCardPreviewImage(*card);
                    if(life.expired() || geometry_revision_!=revision)return;
                    card_preview_images_[index]=std::move(preview);
                }
            }
            applySceneVisibility();
        }
        notifyReferencedResources(selected_cards,selected_backs);
        if(life.expired() || geometry_revision_!=revision)return;
        publishSceneChange(true);
    }
    template <typename Function>
    void mutateCardGeometry(Function&& function) { mutateSceneGeometry(std::forward<Function>(function)); }
    template <typename Function>
    void mutateBackGeometry(Function&& function) { mutateSceneGeometry(std::forward<Function>(function)); }
    void refreshGeometry(const bool rebuild_dragged_backs = false,
                         const bool notify_modification = true) {
        const std::weak_ptr<void> life=scene_view_lifetime_;
        const auto revision=geometry_revision_;
        const auto valid=[&]{return !life.expired() && geometry_revision_==revision;};
        const auto toolbar=context_->refresh_toolbar;
        if constexpr (UseCards) {
            if (card_images_.size() != cardCount()) rebuildCardImages();
            else for (std::size_t index = 0; index < cardCount(); ++index) {
                const auto& value = areaCard(index);
                auto& image = card_images_[index];
                if (!image) { image = createCardImage(value); continue; }
                image->x(value.x());
                image->y(value.y());
                image->scale(value.scale());
                image->layer(value.layer() * 10);
            }
            refreshCards();
            if(!valid())return;
        }
        if constexpr (UseBacks) {
            if (back_images_.size() != backCount()) rebuildBackImages();
            else for (std::size_t index = 0; index < backCount(); ++index) {
                const auto& value = back(index);
                auto& image = back_images_[index];
                // ドラッグ後のカードと同じく、移動・拡縮したセルも
                // 元の素材から作り直す。 ドラッグ中にFlexImage
                // の範囲は更新済みなので、確定時には選択セルも明
                // 示的に更新する。 Undo/Redoやキーボー
                // ド編集は、前回表示した配置との比較で検出する。
                const bool dragged = rebuild_dragged_backs &&
                    std::find(selected_backs_.begin(), selected_backs_.end(), index) != selected_backs_.end();
                if (!image || dragged || image->x() != value.x() || image->y() != value.y() ||
                    image->width() != value.width() || image->height() != value.height() ||
                    image->transparent() != value.mask()) {
                    image = createBackImage(value);
                    continue;
                }
                image->x(value.x());
                image->y(value.y());
                image->width(value.width());
                image->height(value.height());
                image->transparent(value.mask());
                image->layer(value.layer() * 10);
            }
            refreshBacks();
            if(!valid())return;
        }
        applySceneVisibility();
        refreshSelected();
        if(!valid())return;
        refreshStatusLine();
        if(!valid())return;
        if(notify_modification)callModEvent();
        if(!valid())return;
        if(toolbar)toolbar();
    }
    void rebuildCardImages() {
        if constexpr (UseCards) {
            card_images_.clear();
            card_preview_images_.clear();
            for (std::size_t i = 0; i < cardCount(); ++i) {
                card_images_.push_back(createCardImage(areaCard(i)));
                card_preview_images_.push_back(createCardPreviewImage(areaCard(i)));
            }
        }
    }
    void rebuildDecorationImages() {
        decoration_images_.clear();
        if (!prop_) return;
        const auto* skin_value = activeSkin();
        if (view_message_) {
            areaviewutils::Commons common(*prop_);
            common.skin = skin_value;
            common.summary = summary_;
            common.services = context_ ? context_->services : lib::Services{};
            decoration_images_.push_back(
                areaviewutils::createMessageImage(common, *prop_, summary_));
        }
        if (view_party_ && skin_value) {
            const auto alpha = static_cast<std::uint8_t>(std::clamp(
                prop_->var().partyCardAlpha.value, 0, 255));
            // 従来のD移植由来の非constの宣言を残して
            // いるが、この補助関数はSkinを変更しない。
            auto& mutable_skin = const_cast<Skin&>(*skin_value);
            for (const auto point : prop_->looks().partyCardXY())
                decoration_images_.push_back(areaviewutils::createCastCardBackImage(
                    *prop_, mutable_skin, summary_, point.x, point.y, alpha));
        }
        appendReferenceCards(skin_value);
    }

    void appendReferenceCards(const Skin* skin_value) {
        if constexpr (UseCards || !UseBacks) return;
        if (!show_ref_cards_ || !ref_card_selected_ || !skin_value ||
            ref_card_index_ >= ref_areas_.size()) return;
        const auto* target = ref_areas_[ref_card_index_];
        if (!target) return;
        const auto alpha = static_cast<std::uint8_t>(std::clamp(
            prop_->var().partyCardAlpha.value, 0, 255));
        if (const auto* ref_area = dynamic_cast<const Area*>(target)) {
            for (const auto& card_value : ref_area->cards()) {
                if (!card_value) continue;
                auto image = areaviewutils::createMenuCardImage<FlexImage>(
                    *prop_, *skin_value, summary_, renderMenuCardTitle(*card_value),
                    card_value->paths(), card_value->x(), card_value->y(),
                    card_value->scale(), prop_->var().smoothingCard.value, card_value->layer());
                if (!image) continue;
                image->alpha(alpha);
                image->visible(true);
                decoration_images_.push_back(std::move(image));
            }
        } else if (const auto* ref_battle = dynamic_cast<const Battle*>(target)) {
            areaviewutils::Commons common(*prop_, *skin_value);
            common.summary = summary_;
            for (const auto& card_value : ref_battle->cards()) {
                if (!card_value) continue;
                const auto cast = summary_ ? summary_->cwCast(card_value->id()) : nullptr;
                auto image = areaviewutils::createCastCardImage<FlexImage>(
                    common, *skin_value, summary_, card_value.get(), cast.get(),
                    card_value->x(), card_value->y(), card_value->scale(), prop_->var().smoothingCard.value,
                    debug_mode_, card_value->layer());
                if (!image) continue;
                image->alpha(alpha);
                image->visible(true);
                decoration_images_.push_back(std::move(image));
            }
        }
    }

    [[nodiscard]] bool cardIsFixed(const std::size_t index) const noexcept {
        if constexpr (!UseCards) {
            return true;
        } else {
            return read_only_ || fixed_cards_ || index >= cardCount() ||
                   (view_cards_ && index < card_checked_.size() &&
                    index < card_grayed_.size() && card_checked_[index] &&
                    card_grayed_[index]);
        }
    }
    [[nodiscard]] bool backIsFixed(const std::size_t index) const noexcept {
        if constexpr (!UseBacks) {
            return true;
        } else {
            if (read_only_ || fixed_cells_ || index >= backCount()) return true;
            if (view_backs_ && index < back_checked_.size() &&
                index < back_grayed_.size() && back_checked_[index] &&
                back_grayed_[index]) return true;
            if (!fixed_background_) return false;
            const auto view = prop_->looks().viewSize();
            // Dのchecked()は、モデル順で最初の全面背景だけを固
            // 定する。 後続の全面セルは独立して移動できるようにする。
            for(std::size_t row=0;row<=index;++row) {
                const auto& cell=back(row);
                if(cell.x()==0 && cell.y()==0 && cell.width()==static_cast<int>(view.width) && cell.height()==static_cast<int>(view.height))return row==index;
            }
            return false;
        }
    }
    void rebuildBackImages() {
        if constexpr (UseBacks) {
            back_images_.clear();
            for (std::size_t i = 0; i < backCount(); ++i) back_images_.push_back(createBackImage(back(i)));
        }
    }
    [[nodiscard]] std::string renderMenuCardTitle(const MenuCard& value) const {
        if(!value.expandSPChars() || !prop_)return value.name();
        Skin::Ptr skin_owner;
        if(const auto* skin=activeSkin())skin_owner=Skin::Ptr(const_cast<Skin*>(skin),[](Skin*){});
        const auto counter=[&]() -> usecounter::CounterOwner {
            if constexpr(std::is_same_v<A,BgImageContainer>)return context_->use_counter_owner;
            else return area_->useCounter();
        }();
        // DのcreateCardImageは、描画用のMenuタイトルだけを展開
        // し、#の参照記号は展開しない。 一覧のラベルと行内編集では元の値を保つ。
        return messageutils::createSPCharPreview(*prop_,summary_,skin_owner,counter.get(),value.name(),false);
    }
    [[nodiscard]] FlexImagePtr createCardImage(const C& value) const {
        const auto* active_skin = activeSkin();
        if (prop_ && active_skin) {
            if constexpr (std::is_same_v<C, MenuCard>) {
                return areaviewutils::createMenuCardImage<FlexImage>(
                    *prop_, *active_skin, summary_, renderMenuCardTitle(value), value.paths(),
                    value.x(), value.y(), value.scale(), prop_->var().smoothingCard.value, value.layer());
            } else if constexpr (std::is_same_v<C, EnemyCard>) {
                areaviewutils::Commons common(*prop_, *active_skin);
                common.summary = summary_;
                const auto cast = summary_ ? summary_->cwCast(value.id()) : nullptr;
                return areaviewutils::createCastCardImage<FlexImage>(
                    common, *active_skin, summary_, &value, cast.get(), value.x(),
                    value.y(), value.scale(), prop_->var().smoothingCard.value, debug_mode_, value.layer());
            }
        }
        lib::Size size{1.0F, 1.0F};
        if (prop_) {
            const auto card_size = prop_->looks().cardSize();
            size = {static_cast<float>(card_size.width), static_cast<float>(card_size.height)};
        }
        auto result = std::make_shared<FlexImage>(areaviewutils::ImageDataWithScale{}, prop_ ? prop_->drawingScale() : 1U, value.x(), value.y(), static_cast<int>(size.width), static_cast<int>(size.height), true, true);
        result->scale(value.scale()); result->layer(value.layer() * 10); return result;
    }
    [[nodiscard]] FlexImagePtr createCardPreviewImage(const C& value) const {
        if (!prop_) return {};
        const auto* active_skin = activeSkin();
        if (!active_skin) return {};

        area::CardImageArray paths;
        if constexpr (std::is_same_v<C, MenuCard>) {
            paths = value.paths();
        } else if constexpr (std::is_same_v<C, EnemyCard>) {
            if (value.isOverrideImage()) {
                paths = value.overrideImages();
            } else if (summary_) {
                if (const auto cast = summary_->cwCast(value.id())) paths = cast->paths();
            }
        }

        for (const auto& path : paths) {
            if (!path) continue;
            areaviewutils::ImageDataWithScale data;
            switch (path->type) {
            case types::CardImageType::File:
                if (!path->path().empty())
                    data = areaviewutils::detail::detailLoadImage(
                        *prop_, *active_skin, summary_, path->path(),
                        prop_->drawingScale());
                break;
            case types::CardImageType::PCNumber:
                if (path->pcNumber() > 0U)
                    data = areaviewutils::detail::detailPcNumberImage(
                        *prop_, path->pcNumber(), active_skin->legacy());
                break;
            case types::CardImageType::Talker:
                data = areaviewutils::detail::detailTalkerImage(
                    *prop_, *active_skin, summary_, path->talker());
                break;
            }
            if (!data.valid()) continue;
            const auto size = data.size();
            const auto width = std::max(1, static_cast<int>(std::lround(size.width)));
            const auto height = std::max(1, static_cast<int>(std::lround(size.height)));
            auto result = std::make_shared<FlexImage>(
                std::move(data), prop_->drawingScale(), 0, 0, width, height,
                false, false);
            result->setGraphics(prop_->images().services().graphics);
            result->transparent(true);
            result->createImage();
            return result;
        }
        return {};
    }
    [[nodiscard]] FlexImagePtr createBackImage(const BgImage& value) const {
        if (prop_) if (const auto* skin_value = activeSkin())
            if (const auto image = dynamic_cast<const background::ImageCell*>(&value))
                return areaviewutils::createBackgroundImage(
                    *prop_, *skin_value, summary_, image->path(), image->x(), image->y(),
                    image->width(), image->height(), image->mask(), image->layer(),
                    image->smoothing() == types::Smoothing::True);
        auto result = std::make_shared<FlexImage>(areaviewutils::ImageDataWithScale{}, prop_ ? prop_->drawingScale() : 1U, value.x(), value.y(), value.width(), value.height(), false, true); result->path(value.name(prop_->parent())); result->layer(value.layer() * 10); return result;
    }
    [[nodiscard]] std::string cardNameWithGroup(const C& value) const {
        std::string name;
        if constexpr (std::is_same_v<C, MenuCard>) {
            name = value.name();
        } else if constexpr (std::is_same_v<C, EnemyCard>) {
            if (value.isOverrideName()) {
                name = value.overrideName();
            } else if (summary_) {
                if (const auto cast = summary_->cwCast(value.id())) name = cast->name();
            }
        }
        // ALLは場面編集を一つのページとして表示する。デ
        // スクトップのカード一覧で有用なグループ接頭辞も、
        // 小型のALLでは背景上に不要な文字として出るた
        // め、カード名だけを残し表示専用の接頭辞を省く。
        const bool hide_group = lib::editor_window_host_options().all_mode;
        if (value.cardGroup().empty() || hide_group) return name;
        if (prop_) {
            return ::cwxeditor::cwx::utils::tryFormat(
                prop_->requiredMsgs().nameWithCardGroup.value, value.cardGroup(), name);
        }
        return "[" + value.cardGroup() + "] " + name;
    }
    [[nodiscard]] ResourceId backIcon(const BgImage& value) const {
        if (!prop_) return {};
        const bool with_flag = !value.flag().empty();
        if (dynamic_cast<const background::ImageCell*>(&value))
            return prop_->images().load(lib::StringView{
                with_flag ? "backs_flag.png" : "backs.png"});
        if (dynamic_cast<const background::TextCell*>(&value))
            return prop_->images().load(lib::StringView{
                with_flag ? "text_cell_flag.png" : "text_cell.png"});
        if (dynamic_cast<const background::ColorCell*>(&value))
            return prop_->images().load(lib::StringView{
                with_flag ? "color_cell_flag.png" : "color_cell.png"});
        if (dynamic_cast<const background::PCCell*>(&value))
            return prop_->images().load(lib::StringView{
                with_flag ? "pc_cell_flag.png" : "pc_cell.png"});
        return {};
    }
    void ensureCardChecks(){if constexpr(UseCards)detail::synchronizeChecks(areaCardArray(),card_check_keys_,card_checked_,card_grayed_);}
    void ensureBackChecks(){if constexpr(UseBacks)detail::synchronizeChecks(area_->backs(),back_check_keys_,back_checked_,back_grayed_);}
    // 表示切り替えはモデルの変更ではないため、modEventを
    // 出すrefreshAfterModelChange()は
    // 使わない。 既存のC++のホスト・寿命の保護は維持する。
    void refreshTypeVisibility() {
        const std::weak_ptr<void> life = scene_view_lifetime_;
        const auto revision = geometry_revision_;
        const auto valid = [&] { return !life.expired() && geometry_revision_ == revision; };
        const auto toolbar = context_->refresh_toolbar;
        applySceneVisibility();
        if (!valid()) return;
        // Dのchecked()と同じく、場面と部品の状態だけを更新し、 ど
        // ちらの表にも選択の表示位置やフォーカス・選択印の変更を要求しない。
        refreshSelected(false, true);
        if (!valid()) return;
        refreshListToolBarEnablement();
        if (!valid()) return;
        if (toolbar) toolbar();
    }

    void applySceneVisibility() {
        if (cards_) cards_->set_enabled(view_cards_);
        if (backs_) backs_->set_enabled(view_backs_);
        if constexpr (UseCards) {
            ensureCardChecks();
            for (std::size_t index = 0; index < card_images_.size() &&
                 index < card_checked_.size(); ++index)
                if (card_images_[index])
                    card_images_[index]->visible(view_cards_ && card_checked_[index]);
        }
        if constexpr (UseBacks) {
            ensureBackChecks();
            for (std::size_t index = 0; index < back_images_.size() &&
                 index < back_checked_.size(); ++index)
                if (back_images_[index])
                    back_images_[index]->visible(view_backs_ && back_checked_[index]);
        }
        requestSceneRedraw();
    }
    void attachInlineSearch(const std::shared_ptr<Table>& table,std::size_t row,std::size_t column,
        const std::shared_ptr<lib::TextEdit>& edit,const std::shared_ptr<lib::ComboBox>& combo,bool cards) {
        if(!combo || read_only_ || !summary_ || column!=0 || !prop_)return;
        std::vector<std::pair<lib::ChoiceItem,std::string>> candidates;
        if(cards) {
            if constexpr(std::is_same_v<C,EnemyCard>) {
                if(row>=cardCount())return;
                for(const auto& cast:summary_->casts())if(cast) {
                    const auto id=std::to_string(cast->id());
                    candidates.push_back({{id+"."+cast->name(),id},cast->name()});
                }
            } else return;
        } else {
            if constexpr(UseBacks) {
                if(row>=backCount() || !dynamic_cast<const background::ImageCell*>(&back(row)))return;
                for(const auto& item:combo->items)candidates.push_back({item,item.text});
            } else return;
        }
        if(candidates.empty())return;
        incsearch::IncSearchSettings settings;
        settings.startIncrementalSearchWhenKeyDown=prop_->var().startIncrementalSearchWhenKeyDown.value;
        settings.startIncrementalSearchWhenKeyDownProvider =
            [this, life=std::weak_ptr<void>(scene_view_lifetime_)] {
            return !life.expired() && prop_ &&
                prop_->var().startIncrementalSearchWhenKeyDown.value;
        };
        settings.incrementalSearchBoxWidth=prop_->var().incrementalSearchBoxWidth.value;
        settings.incrementalSearchType=prop_->var().incrementalSearchType.value;
        settings.windowTitle=prop_->requiredMsgs().menuText(types::MenuID::IncSearch);
        settings.closeLabel=prop_->requiredMsgs().menuText(types::MenuID::CloseIncSearch);
        const std::weak_ptr<void> life=scene_view_lifetime_;
        const auto revision=geometry_revision_,order=table->row_order_revision;
        const std::weak_ptr<Table> weak=table;
        const auto current=[this,life,revision,order,weak] {
            if(life.expired())return false;
            const auto owner=weak.lock();
            return owner && !read_only_ && geometry_revision_==revision && owner->row_order_revision==order;
        };
        auto filter=[candidates=std::move(candidates),cards](const incsearch::IncSearch& search) {
            std::vector<lib::ChoiceItem> result;
            // DのbgImageIncSearchは常にNoneを先に含め
            // 、その後Noneも含む全候補を絞り込む。 この順序を保つ。
            if(!cards)result.push_back(candidates.front().first);
            for(const auto& [item,query]:candidates)if(search.match(query))result.push_back(item);
            return result;
        };
        // DのEditEnd.focusLostは、IncSearchへフォーカスが移
        // ったときにTableEditを残す。 共通のComboBoxは編集部品の兄弟
        // なので、一時検索ポップアップに論理的なTableViewの所有者を明示する。
        combo->focus_transaction_owner=table;
        (void)incsearch::attachComboSearch(context_->services,combo,edit,std::move(filter),std::move(settings),current,0,
            [this,life](incsearch::SearchType type){
                if(!life.expired())prop_->var().incrementalSearchType.value=static_cast<std::int32_t>(type);
            });
    }
    [[nodiscard]] lib::TableRow cardTableRow(const std::size_t index) const {
        const auto& value = areaCard(index);
        lib::TableRow row;
        lib::ListItem item;
        item.text = cardNameWithGroup(value);
        item.icon = dutils::cardIcon(prop_, activeSkin(), summary_, &value, false);
        if constexpr(std::is_same_v<C,MenuCard>) {
            // DのTableTextEditは元の名前を編集する。 行の表
            // 示には、引き続きグループ・特殊文字を反映したラベルを使う。
            item.editor=lib::CellEditorKind::Text;
            item.editor_value=value.name();item.inline_editable=!read_only_;
        } else if constexpr(std::is_same_v<C,EnemyCard>) {
            item.editor=lib::CellEditorKind::Choice;
            item.editor_value=std::to_string(value.id());
            item.inline_editable=!read_only_ && summary_ && !summary_->casts().empty();
            if(summary_)for(const auto& cast:summary_->casts())if(cast) {
                item.editor_choices.push_back(std::to_string(cast->id())+"."+cast->name());
                item.editor_choice_values.push_back(std::to_string(cast->id()));
            }
        }
        row.cells = {std::move(item)};
        if (index < card_checked_.size()) row.checked = card_checked_[index];
        if (index < card_grayed_.size()) row.grayed = card_grayed_[index];
        if (prop_ && activeSkin()) {
            row.warnings = ::cwxeditor::cwx::warning::warnings(
                prop_->parent(), *activeSkin(), summary_, value,
                summary_ && summary_->legacy(),
                summary_ ? summary_->dataVersion()
                         : ::cwxeditor::cwx::versioninfo::LATEST_VERSION,
                prop_->var().targetVersion.value);
            if (!row.warnings.empty())
                row.warning_icon = prop_->images().load(
                    dprops::StringView{"warning.png"});
        }
        return row;
    }

    void refreshCards() {
        if constexpr (UseCards) {
            // DWTの左ペインは、一列のチェック付き一覧。配置
            // は画像ペイン上の操作欄で編集し、 実装用の五つ
            // のフィールドを英語の編集列として露出させない。
            cards_->columns = {{"", compactAllMode() ? 160.0F : 210.0F}};
            ensureCardChecks();
            const auto previous_selection = selected_cards_;
            auto selected_keys = selected_card_keys_;
            const auto keys_are_live = !selected_keys.empty() || previous_selection.empty()
                ? std::all_of(selected_keys.begin(), selected_keys.end(), [this](const auto* key) {
                      return key && std::any_of(areaCardArray().begin(), areaCardArray().end(),
                                                 [key](const auto& card) { return card.get() == key; });
                  }) : false;
            if (!keys_are_live) {
                selected_keys.clear();
                selected_keys.reserve(previous_selection.size());
                for (const auto index : previous_selection)
                    if (index < cardCount()) selected_keys.push_back(areaCardPtr(index).get());
            }
            std::vector<lib::TableRow> rows;
            rows.reserve(cardCount());
            for (std::size_t i = 0; i < cardCount(); ++i)
                rows.push_back(cardTableRow(i));
            auto* const source_area = area_;
            std::vector<std::size_t> selection;
            for (std::size_t index = 0; index < cardCount(); ++index)
                if (std::find(selected_keys.begin(), selected_keys.end(),
                              areaCardPtr(index).get()) != selected_keys.end())
                    selection.push_back(index);
            const auto revision = ++cards_table_revision_;
            const auto reveal = selection.empty()
                ? std::optional<std::size_t>{} : std::optional<std::size_t>{selection.front()};
            auto publication = lib::prepare_table_rows(
                cards_, std::move(rows), selection, reveal,
                [this, source_area, revision, previous_selection, selected_keys, selection] {
                    if (area_ != source_area || cards_table_revision_ != revision ||
                        selected_cards_ != previous_selection) return false;
                    std::vector<std::size_t> live_selection;
                    for (std::size_t index = 0; index < cardCount(); ++index)
                        if (std::find(selected_keys.begin(), selected_keys.end(),
                                      areaCardPtr(index).get()) != selected_keys.end())
                            live_selection.push_back(index);
                    return live_selection == selection;
                });
            if (publication.commit()) {
                selected_cards_ = std::move(selection);
                // 他のビューのUndo/Redoで全モデルオブ
                // ジェクトが置き換わる場合がある。 上で有効な
                // 行から選択を対応付け直したら、次回の反映に無
                // 効なキーを残さず、置換後の対象を保持する。
                selected_card_keys_.clear();
                selected_card_keys_.reserve(selected_cards_.size());
                for (const auto index : selected_cards_)
                    if (index < cardCount())
                        selected_card_keys_.push_back(areaCardPtr(index).get());
            }
        }
    }
    [[nodiscard]] std::vector<std::string> backgroundEditorChoices(
        const BgImage& value,std::string* selected=nullptr) const {
        std::vector<std::string> result;
        if (!prop_) return result;
        if (const auto* pc = dynamic_cast<const background::PCCell*>(&value)) {
            const auto count = prop_ ? prop_->looks().partyCardXY().size() : 0U;
            result.reserve(count);
            for (std::size_t index = 0; index < count; ++index)
                result.push_back(::cwxeditor::cwx::utils::tryFormat(
                    prop_->requiredMsgs().pc.value, index + 1U));
            // 独自スキンにはパーティカードの位置がない場合がある。
            // 空の編集部品を公開せず、モデルの現在の選択を保つ。
            if (result.empty()) result.push_back(pc->name(prop_->parent()));
            if(selected){selected->clear();if(pc->pcNumber()>0 && pc->pcNumber()<=result.size())*selected=result[pc->pcNumber()-1U];}
            return result;
        }
        const auto* image = dynamic_cast<const background::ImageCell*>(&value);
        if (!image) return result;
        result.push_back(prop_->requiredMsgs().defaultSelection(prop_->requiredMsgs().imageNone.value));
        if(selected)*selected=result.front();
        if (const auto* skin_value = activeSkin()) {
            const auto absolute=[](std::string_view path){return utils::nabs(path);};
            const auto current=absolute(skin_value->findImagePath(image->path(),summary_?summary_->scenarioPath():std::string{},summary_?summary_->dataVersion():versioninfo::LATEST_VERSION));
            for (const auto& path : skin_value->tables(prop_->var().logicalSort.value)) {
                if(!utils::noScaledPath(dutils::filesystemPath(path)).empty())continue;
                result.push_back(path);
                for(const auto& directory:skin_value->tableDirs())
                    if(utils::cfnmatch(current,absolute(dutils::pathString(dutils::filesystemPath(directory)/dutils::filesystemPath(path))))){if(selected)*selected=path;break;}
            }
            if (summary_ && !summary_->scenarioPath().empty()) {
                try {
                    const std::filesystem::path root =
                        dutils::filesystemPath(summary_->scenarioPath());
                    // Dのrecurse(isDir)との差異として、安全のためディレク
                    // トリのリンクで循環したり、 シナリオの外へ出たりしないようにする。
                    for (std::filesystem::recursive_directory_iterator it(
                             root, std::filesystem::directory_options::skip_permission_denied),
                         end; it != end; ++it) {
                        const auto basename=dutils::pathString(it->path().filename());
                        if(utils::containsPath(prop_->var().ignorePaths.value,basename)){
                            if(it->is_directory())it.disable_recursion_pending();
                            continue;
                        }
                        const auto relative = dutils::pathString(
                            it->path().lexically_relative(root));
                        if(!summary_->legacy() && summary_->loadScaledImage() && !utils::noScaledPath(dutils::filesystemPath(relative)).empty()){
                            if(it->is_directory())it.disable_recursion_pending();
                            continue;
                        }
                        if (!it->is_regular_file() || !skin_value->isBgImage(basename)) continue;
                        const auto encoded=utils::encodePath(dutils::pathString(std::filesystem::path(std::filesystem::path::preferred_separator== '\\'?"\\":"/")/dutils::filesystemPath(relative)));
                        result.push_back(encoded);
                        if(utils::cfnmatch(current,absolute(dutils::pathString(it->path())))){if(selected)*selected=encoded;}
                    }
                } catch (const std::filesystem::filesystem_error&) {
                    // 一時的なエラーやシナリオのディレクトリエラー
                    // では、 Dの列挙と同じくその候補だけを省く。
                }
            }
        }
        return result;
    }
    [[nodiscard]] lib::TableRow backTableRow(const std::size_t index) const {
        const auto& value = back(index);
        lib::TableRow row;
        lib::ListItem item;
        item.text = prop_ ? value.name(prop_->parent()) : value.name();
        item.icon = backIcon(value);
        if (!read_only_ &&
            (dynamic_cast<const background::ImageCell*>(&value) ||
             dynamic_cast<const background::PCCell*>(&value))) {
            item.editor = lib::CellEditorKind::Choice;
            std::string selected;
            item.editor_choices = backgroundEditorChoices(value,&selected);
            item.editor_value=std::move(selected);
        }
        row.cells = {std::move(item)};
        if (index < back_checked_.size()) row.checked = back_checked_[index];
        if (index < back_grayed_.size()) row.grayed = back_grayed_[index];
        if (prop_ && activeSkin()) {
            row.warnings = ::cwxeditor::cwx::warning::warnings(
                prop_->parent(), *activeSkin(), summary_, value,
                summary_ && summary_->legacy(),
                summary_ ? summary_->dataVersion()
                         : ::cwxeditor::cwx::versioninfo::LATEST_VERSION,
                prop_->var().targetVersion.value);
            if (!row.warnings.empty())
                row.warning_icon = prop_->images().load(
                    dprops::StringView{"warning.png"});
        }
        return row;
    }

    void refreshBacks() {
        if constexpr (UseBacks) {
            backs_->columns = {{"", compactAllMode() ? 160.0F : 210.0F}};
            ensureBackChecks();
            const auto previous_selection = selected_backs_;
            auto selected_keys = selected_back_keys_;
            const auto keys_are_live = !selected_keys.empty() || previous_selection.empty()
                ? std::all_of(selected_keys.begin(), selected_keys.end(), [this](const auto* key) {
                      return key && std::any_of(area_->backs().begin(), area_->backs().end(),
                                                 [key](const auto& back_value) { return back_value.get() == key; });
                  }) : false;
            if (!keys_are_live) {
                selected_keys.clear();
                selected_keys.reserve(previous_selection.size());
                for (const auto index : previous_selection)
                    if (index < backCount()) selected_keys.push_back(backPtr(index).get());
            }
            std::vector<lib::TableRow> rows;
            rows.reserve(backCount());
            for (std::size_t i = 0; i < backCount(); ++i)
                rows.push_back(backTableRow(i));
            auto* const source_area = area_;
            std::vector<std::size_t> selection;
            for (std::size_t index = 0; index < backCount(); ++index)
                if (std::find(selected_keys.begin(), selected_keys.end(),
                              backPtr(index).get()) != selected_keys.end())
                    selection.push_back(index);
            const auto revision = ++backs_table_revision_;
            const auto reveal = selection.empty()
                ? std::optional<std::size_t>{} : std::optional<std::size_t>{selection.front()};
            auto publication = lib::prepare_table_rows(
                backs_, std::move(rows), selection, reveal,
                [this, source_area, revision, previous_selection, selected_keys, selection] {
                    if (area_ != source_area || backs_table_revision_ != revision ||
                        selected_backs_ != previous_selection) return false;
                    std::vector<std::size_t> live_selection;
                    for (std::size_t index = 0; index < backCount(); ++index)
                        if (std::find(selected_keys.begin(), selected_keys.end(),
                                      backPtr(index).get()) != selected_keys.end())
                            live_selection.push_back(index);
                    return live_selection == selection;
                });
            if (publication.commit()) {
                selected_backs_ = std::move(selection);
                selected_back_keys_.clear();
                selected_back_keys_.reserve(selected_backs_.size());
                for (const auto index : selected_backs_)
                    if (index < backCount())
                        selected_back_keys_.push_back(backPtr(index).get());
            }
        }
    }
    void refreshStatus() { if (status_) status_->text = status_line_; }
    void refreshStatusLineImpl() { refreshStatus(); }
    void editCardCell(std::size_t row, std::size_t column, std::string value) {
        if constexpr (UseCards) if (row < cardCount() && !read_only_) {
            bool model_changed = false;
            if (column == 0U) {
                if constexpr (std::is_same_v<C, MenuCard>) {
                    // DのnameEditEndと同じく、完了した行内編集を基
                    // 点の行だけでなく、 選択中の全メニューカードへ適用する。
                    auto indices = selected_cards_;
                    if (std::find(indices.begin(), indices.end(), row) == indices.end())
                        indices = {row};
                    indices.erase(std::remove_if(indices.begin(), indices.end(),
                        [this, &value](const auto index) {
                            return index >= cardCount() || areaCard(index).name() == value;
                        }), indices.end());
                    if (!indices.empty()) {
                        if(!mutateWithReferenceNotifications(indices, {}, [&] {
                            for (const auto index : indices) areaCard(index).name(value);
                        }))return;
                        model_changed = true;
                    }
                } else if constexpr(std::is_same_v<C,EnemyCard>) {
                    if(!summary_)return;
                    const auto casts=summary_->casts();
                    const auto found=std::find_if(casts.begin(),casts.end(),[&](const auto& cast){return cast && std::to_string(cast->id())==value;});
                    if(found==casts.end())return;
                    const auto id=(*found)->id();auto indices=selected_cards_;
                    if(std::find(indices.begin(),indices.end(),row)==indices.end())indices={row};
                    std::erase_if(indices,[&](const auto index){return index>=cardCount() || areaCard(index).id()==id;});
                    if(!indices.empty()) {
                        if(!mutateWithReferenceNotifications(indices,{},[&]{for(const auto index:indices)areaCard(index).id(id);} ))return;
                        model_changed = true;
                    }
                }
            } else {
                int number{};
                try { number = std::stoi(value); } catch (...) { refresh(); return; }
                mutateCardGeometry([&] { auto& card = areaCard(row); if (column == 1U) card.x(number); else if (column == 2U) card.y(number); else if (column == 3U) card.scale(static_cast<std::uint32_t>(std::max(1, number))); else if (column == 4U) card.layer(number); });
            }
            if (column == 0U) {
                if (model_changed) refreshAfterModelChange();
                else refresh();
            }
        }
    }
    void editBackCell(std::size_t row, std::size_t column, std::string value) {
        if constexpr (UseBacks) if (row < backCount() && !read_only_) {
            if (column == 0U) {
                auto& cell = back(row);
                if (auto* pc = dynamic_cast<background::PCCell*>(&cell)) {
                    const auto choices = backgroundEditorChoices(cell);
                    const auto found = std::find(choices.begin(), choices.end(), value);
                    if (found == choices.end()) { refresh(); return; }
                    const auto number = static_cast<std::uint32_t>(
                        std::distance(choices.begin(), found) + 1U);
                    const bool model_changed = pc->pcNumber() != number;
                    if (model_changed)
                        if(!mutateWithReferenceNotifications({}, {row}, [&] {
                            pc->pcNumber(number);
                        }))return;
                    if (model_changed) refreshAfterModelChange();
                    else refresh();
                    return;
                }
                if (auto* image = dynamic_cast<background::ImageCell*>(&cell)) {
                    const auto none = prop_->requiredMsgs().defaultSelection(
                        prop_->requiredMsgs().imageNone.value);
                    if (!value.empty() && value.front() == '/') value.erase(value.begin());
                    const std::string path = value == none ? std::string{} : std::move(value);
                    const bool model_changed = image->path() != path;
                    if (model_changed)
                        if(!mutateWithReferenceNotifications({}, {row}, [&] {
                            image->path(path);
                        }))return;
                    if (model_changed) refreshAfterModelChange();
                    else refresh();
                    return;
                }
                refresh();
                return;
            }
            int number{};
            try { number = std::stoi(value); } catch (...) { refresh(); return; }
            mutateBackGeometry([&] { auto& cell = back(row); if (column == 1U) cell.x(number); else if (column == 2U) cell.y(number); else if (column == 3U) cell.width(number); else if (column == 4U) cell.height(number); });
        }
    }
    void refreshSelectedAfterErase() { selected_cards_.clear(); selected_backs_.clear(); }
    void eraseSelected(const std::vector<std::size_t>& cards_value, const std::vector<std::size_t>& backs_value) {
        if constexpr (UseCards) { auto indices = detail::normalizedSelection<C>(cards_value, cardCount()); std::sort(indices.rbegin(), indices.rend()); for (const auto i : indices) if constexpr (requires(A& value) { value.removeCard(i); }) area_->removeCard(i); }
        if constexpr (UseBacks) { auto indices = detail::normalizedSelection<BgImage>(backs_value, backCount()); std::sort(indices.rbegin(), indices.rend()); for (const auto i : indices) if constexpr (requires(A& value) { value.removeBgImage(i); }) area_->removeBgImage(i); }
    }
    void setAutoImpl(bool value) {
        if (read_only_) return;
        if (detail::hasSpAuto(*area_) == value) {
            refresh();
            return;
        }
        if (!mutateWithReferenceNotifications({}, {}, [&] {
                detail::setSpAuto(*area_, value);
            })) return;
        refreshAfterModelChange();
    }
    [[nodiscard]] bool canMoveSelection() const noexcept { return !read_only_ && (!selected_cards_.empty() || !selected_backs_.empty()); }
    [[nodiscard]] bool canMoveOrder(int delta) const noexcept {
        if (read_only_ || !isFocusOnListOrPane() || delta == 0) return false;
        bool selected = false;
        if constexpr (UseCards) {
            if (!selected_cards_.empty()) {
                selected = true;
                if (!canMoveOrderArray(selected_cards_, delta, cardCount())) return false;
            }
        }
        if constexpr (UseBacks) {
            if (!selected_backs_.empty()) {
                selected = true;
                if (!canMoveOrderArray(selected_backs_, delta, backCount())) return false;
            }
        }
        return selected;
    }
    void moveOrder(int delta, bool cards_value, bool backs_value) {
        if (read_only_ || delta == 0) return;
        const bool can_move_cards = [&] {
            if constexpr (!UseCards) return false;
            else return cards_value && canMoveOrderArray(selected_cards_, delta, cardCount());
        }();
        const bool can_move_backs = [&] {
            if constexpr (!UseBacks) return false;
            else return backs_value && canMoveOrderArray(selected_backs_, delta, backCount());
        }();
        if constexpr (UseCards)
            if (cards_value && !selected_cards_.empty() && !can_move_cards) return;
        if constexpr (UseBacks)
            if (backs_value && !selected_backs_.empty() && !can_move_backs) return;
        if (!can_move_cards && !can_move_backs) return;
        // Dは全選択モデル種別で共通の移動数を補正する。 各配列
        // を別々に補正して、混在する選択を分離しないようにする。
        auto steps = static_cast<std::size_t>(delta < 0
            ? -static_cast<std::int64_t>(delta) : delta);
        const auto clamp_steps = [&](const auto& indices, std::size_t count) {
            const auto room = delta < 0
                ? *std::min_element(indices.begin(), indices.end())
                : count - 1U - *std::max_element(indices.begin(), indices.end());
            steps = std::min(steps, room);
        };
        if constexpr (UseCards) if (can_move_cards) clamp_steps(selected_cards_, cardCount());
        if constexpr (UseBacks) if (can_move_backs) clamp_steps(selected_backs_, backCount());
        if (steps == 0U) return;
        delta = delta < 0 ? -static_cast<int>(steps) : static_cast<int>(steps);
        const std::weak_ptr<void> life=scene_view_lifetime_;
        if(!mutate([&] {
            if constexpr (UseCards)
                if (can_move_cards)
                    moveOrderArray(selected_cards_, delta, cardCount(),
                        [this](std::size_t a, std::size_t b) { area_->swapCards(a, b); });
            if constexpr (UseBacks)
                if (can_move_backs)
                    moveOrderArray(selected_backs_, delta, backCount(),
                        [this](std::size_t a, std::size_t b) { swapBacks(a, b); });
        }, {}, {}, !lib::editor_window_host_options().all_mode))return;
        if (life.expired()) return;
        const auto revision=geometry_revision_;
        refreshAfterModelChange();
        if (life.expired() || geometry_revision_!=revision) return;
        // DのupImpl/downImplのsel=trueと同じく、移動した行を同じ所有
        // 者の全有効ビューで選択する。 UndoUp/UndoDownの再生時も同じにする。
        if constexpr (std::is_base_of_v<AbstractArea,A>)
            if (context_->scene_view_registry)
                context_->scene_view_registry->restoreSelection(
                    static_cast<const AbstractArea*>(area_),this,
                    selected_cards_,selected_backs_);
    }
    static bool canMoveOrderArray(const std::vector<std::size_t>& indices,
                                  int delta, std::size_t count) noexcept {
        if (indices.empty() || count == 0U) return false;
        if (delta < 0)
            return *std::min_element(indices.begin(), indices.end()) > 0U;
        return *std::max_element(indices.begin(), indices.end()) + 1U < count;
    }
    template <typename Swap>
    static void moveOrderArray(std::vector<std::size_t>& indices, int delta,
                               std::size_t count, Swap swap) {
        if (indices.empty() || count == 0U) return;
        std::sort(indices.begin(), indices.end());
        const auto steps = static_cast<std::size_t>(std::abs(delta));
        for (std::size_t step = 0; step < steps; ++step) {
            if (delta < 0) {
                if (indices.front() == 0U) break;
                for (auto& index : indices) {
                    swap(index, index - 1U);
                    --index;
                }
            } else {
                if (indices.back() + 1U >= count) break;
                for (auto it = indices.rbegin(); it != indices.rend(); ++it) {
                    swap(*it, *it + 1U);
                    ++*it;
                }
            }
        }
    }
    void moveRefCards(const std::vector<int>& indices, int delta) { if (delta == 0) return; auto selected = indices; if (delta < 0) std::sort(selected.begin(), selected.end()); else std::sort(selected.rbegin(), selected.rend()); for (auto& value : selected) value += delta; }
    // DはTable.select/deselectで画像選択を変え、 setSelection
    // による完全なフォーカス・選択印の同期は通らない。
    void selectImage(FlexImage& image, std::vector<std::size_t>& selected, const std::vector<FlexImagePtr>& images) { for (std::size_t i = 0; i < images.size(); ++i) if (images[i].get() == &image) { selected = {i}; refreshSelected(true, true); return; } }
    void editSelectedCardPosition(int value, bool x_axis) { if constexpr (UseCards) if (canMoveSelection()) mutateCardGeometry([&] { for (const auto i : selected_cards_) if (i < cardCount()) { if (x_axis) areaCard(i).x(value); else areaCard(i).y(value); } }); }
    void editSelectedBackSize(int value, bool width) { if constexpr (UseBacks) if (canMoveSelection()) mutateBackGeometry([&] { for (const auto i : selected_backs_) if (i < backCount()) { if (width) back(i).width(value); else back(i).height(value); } }); }
    int canvasWidth() const noexcept { return canvas_ ? static_cast<int>(canvas_->bounds().width) : 0; }
    int canvasHeight() const noexcept { return canvas_ ? static_cast<int>(canvas_->bounds().height) : 0; }
    [[nodiscard]] std::vector<detail::Placement> selectedPlacement(bool cards) const {
        std::vector<detail::Placement> values;
        if constexpr(UseCards)if(cards) {
            for(const auto index:detail::normalizedSelection<C>(selected_cards_,cardCount())) {
                const auto& value=areaCard(index);const auto image=cardImage(static_cast<int>(index));
                values.push_back({index,value.x(),value.y(),image?image->width():0,
                    image?image->height():0,static_cast<int>(value.scale())});
            }
        }
        if constexpr(UseBacks)if(!cards) {
            for(const auto index:detail::normalizedSelection<BgImage>(selected_backs_,backCount())) {
                const auto& value=back(index);const auto image=backImage(static_cast<int>(index));
                values.push_back({index,value.x(),value.y(),image?image->width():value.width(),
                    image?image->height():value.height(),image?static_cast<int>(image->scale()):100});
            }
        }
        return values;
    }
    [[nodiscard]] std::vector<detail::Placement> plannedPlacement(detail::GeometryCommand command,bool cards) const {
        const auto size=prop_->looks().viewSize();
        const int height=cards&&view_party_?static_cast<int>(prop_->looks().partyTop()):static_cast<int>(size.height);
        auto selected = selectedPlacement(cards);
        if (command == detail::GeometryCommand::Even) {
            // Dと同じく、モデルの行順で選択画像を集めてから、X座標で安定的に並べ替える。
            std::sort(selected.begin(), selected.end(), [](const auto& left, const auto& right) {
                return left.index < right.index;
            });
        }
        return detail::placeSelection(std::move(selected),command,cards,static_cast<int>(size.width),height);
    }
    [[nodiscard]] bool canPlaceSelection(detail::GeometryCommand command) const {
        if(read_only_)return false;
        for(const bool cards:{true,false}) {
            const auto before=selectedPlacement(cards);const auto after=plannedPlacement(command,cards);
            for(const auto& item:after) {
                const auto old=std::find_if(before.begin(),before.end(),[&](const auto& value){return value.index==item.index;});
                if(old!=before.end() && *old!=item)return true;
            }
        }
        return false;
    }
    void placeSelection(detail::GeometryCommand command) {
        if(!canPlaceSelection(command))return;
        const auto cards=plannedPlacement(command,true),backs=plannedPlacement(command,false);
        mutateSceneGeometry([&] {
            if constexpr(UseCards)for(const auto& item:cards) {
                auto& value=areaCard(item.index);value.x(item.x);value.y(item.y);
                if(command==detail::GeometryCommand::Big||command==detail::GeometryCommand::Small)
                    value.scale(static_cast<std::uint32_t>(item.scale));
            }
            if constexpr(UseBacks)for(const auto& item:backs) {
                auto& value=back(item.index);value.x(item.x);value.y(item.y);
                if(command==detail::GeometryCommand::Big||command==detail::GeometryCommand::Small||command==detail::GeometryCommand::Expand) {
                    value.width(item.width);value.height(item.height);
                }
            }
        });
    }
    [[nodiscard]] bool areaCommentable() const noexcept { return area_ != nullptr; }

    AreaViewContext owned_context_{};
    AreaViewContext* context_{};
    Props* prop_{};
    Summary* summary_{};
    const Skin* force_skin_{};
    A* area_{};
    std::vector<lib::Action> scene_retirement_callbacks_;
    std::map<const void*,SceneCommentEntry> scene_comment_dialogs_;
    lib::Action disconnect_scene_comments_;
    std::shared_ptr<undo::UndoManager> undo_owner_;
    undo::UndoManager* undo_{};
    bool show_inherit_backs_{};
    bool read_only_{};
    bool debug_mode_{};
    bool focused_{};
    bool last_focus_on_list_or_pane_{};
    bool view_cards_{true};
    bool view_backs_{true};
    bool view_message_{true};
    bool view_party_{true};
    bool show_ref_cards_{};
    bool fixed_cards_{};
    bool fixed_cells_{};
    bool fixed_background_{};
    bool show_grid_{};
    ContextMenuTarget context_menu_target_{ContextMenuTarget::Scene};
    std::string status_line_;
    std::shared_ptr<lib::Panel> root_;
    std::shared_ptr<lib::Panel> toolbar_row_;
    std::shared_ptr<lib::Panel> all_compact_toolbar_;
    std::shared_ptr<lib::SplitView> main_split_;
    std::shared_ptr<lib::SplitView> list_split_;
    std::shared_ptr<lib::SplitView> right_split_;
    std::shared_ptr<lib::ScrollView> scene_scroll_;
    std::shared_ptr<lib::Canvas> canvas_;
    std::shared_ptr<lib::TableView> cards_;
    std::shared_ptr<lib::TableView> backs_;
    std::shared_ptr<lib::TableView> flags_;
    std::shared_ptr<lib::ComboBox> flag_assign_combo_;
    std::shared_ptr<lib::ComboBox> ref_area_combo_;
    std::shared_ptr<lib::Menu> ref_area_menu_;
    std::shared_ptr<lib::Menu> flag_menu_;
    std::shared_ptr<lib::ToolBar> toolbar_;
    bool toolbar_setup_{};
    std::shared_ptr<lib::ImageView> inherit_background_icon_;
    std::shared_ptr<lib::Label> inherit_background_label_;
    std::shared_ptr<lib::ToolBar> card_list_toolbar_;
    std::shared_ptr<lib::ToolBar> back_list_toolbar_;
    std::shared_ptr<lib::ToolBar> bgm_toolbar_;
    std::shared_ptr<lib::Button> bgm_settings_button_;
    std::shared_ptr<lib::ToolBar> selection_toolbar_;
    std::shared_ptr<lib::ToolBar> all_history_toolbar_;
    std::shared_ptr<lib::ToolBar> grid_toolbar_;
    std::shared_ptr<lib::CheckBox> all_flags_check_;
    // 単独起動ではCheckBox、ALLでは「逃」のToggleButtonを使う。
    std::shared_ptr<lib::ToggleButton> possible_to_run_away_control_;
    std::shared_ptr<lib::Menu> menu_;
    std::shared_ptr<lib::StatusBar> status_;
    std::shared_ptr<lib::NumberEdit> x_control_;
    std::optional<NumericPreview> numeric_preview_;
    std::shared_ptr<lib::NumberEdit> y_control_;
    std::shared_ptr<lib::NumberEdit> width_control_;
    std::shared_ptr<lib::NumberEdit> height_control_;
    std::shared_ptr<lib::NumberEdit> scale_control_;
    std::shared_ptr<lib::NumberEdit> layer_control_;
    std::array<std::shared_ptr<lib::Label>, 6U> geometry_labels_{};
    std::array<std::shared_ptr<lib::Panel>, 6U> geometry_fields_{};
    std::shared_ptr<lib::Label> scale_percent_label_;
    std::shared_ptr<lib::NumberEdit> grid_x_control_;
    std::shared_ptr<lib::NumberEdit> grid_y_control_;
    std::unique_ptr<materialselect::MaterialSelect<materialselect::MtType::BGM>> bgm_selector_;
    bool syncing_bgm_{};
    std::uint32_t grid_x_{10U};
    std::uint32_t grid_y_{10U};
    float scene_zoom_{1.0F};
    bool syncing_geometry_{};
    std::uint64_t geometry_revision_{};
    bool scene_dragging_{};
    bool scene_range_selecting_{};
    bool scene_range_moved_{};
    SceneEntryKind scene_range_target_kind_{SceneEntryKind::Decoration};
    std::size_t scene_range_target_index_{};
    bool scene_drag_moved_{};
    lib::Modifiers scene_press_modifiers_{};
    std::vector<std::size_t> scene_range_cards_;
    std::vector<std::size_t> scene_range_backs_;
    SceneDragHandle scene_drag_handle_{SceneDragHandle::Move};
    SceneEntryKind scene_drag_target_kind_{SceneEntryKind::Decoration};
    std::size_t scene_drag_target_index_{std::numeric_limits<std::size_t>::max()};
    lib::Point scene_drag_origin_{};
    lib::Point scene_range_start_{};
    lib::Point scene_range_end_{};
    std::vector<CardGeometry> scene_drag_cards_;
    std::vector<BackGeometry> scene_drag_backs_;
    std::vector<CardGeometry> scene_drag_preview_cards_;
    std::vector<BackGeometry> scene_drag_preview_backs_;
    bool scene_keyboard_editing_{};
    std::vector<CardGeometry> scene_keyboard_cards_before_;
    std::vector<BackGeometry> scene_keyboard_backs_before_;
    std::vector<CardGeometry> scene_keyboard_preview_cards_;
    std::vector<BackGeometry> scene_keyboard_preview_backs_;
    std::uint64_t scene_keyboard_revision_{};
    std::unique_ptr<areaviewutils::Preview> preview_;
    std::vector<FlexImagePtr> card_images_;
    std::vector<FlexImagePtr> card_preview_images_;
    std::vector<FlexImagePtr> back_images_;
    // 行ごとの表示状態はDのTableのチェックに対応する。 Area/Battle
    // のモデル属性ではなくビューの状態とし、画像・一覧の再構成をまたいで保持する。
    std::vector<bool> card_checked_;
    std::vector<bool> back_checked_;
    std::vector<std::shared_ptr<BgImage>> back_check_keys_;
    // DのTableItem#getGrayedは、表示画像の個別固定を示す
    // 。 これもビューの状態なので、Area/Battleには保存しない。
    std::vector<bool> card_grayed_;
    std::vector<bool> back_grayed_;
    // 挿入・削除・外部の並べ替え後、同一性に基づくビュー状
    // 態の再対応付けが終わるまで、古いカードを保持する。
    std::vector<std::shared_ptr<C>> card_check_keys_;
    std::vector<std::shared_ptr<areaviewutils::PileImage>> decoration_images_;
    std::vector<std::size_t> selected_cards_;
    std::vector<std::size_t> selected_backs_;
    // 表の添字は表示順だけを表す。更新間も選択モデルを保持し、 他のビュ
    // ーや外部から並べ替えられても同じ場面セルを再選択できるようにする。
    std::vector<const C*> selected_card_keys_;
    std::vector<const BgImage*> selected_back_keys_;
    // 準備した表データごとに期待する版番号を持つ。 入れ子の更
    // 新で、新しい表示の後に古い行順を反映しないようにする。
    std::uint64_t cards_table_revision_{};
    std::uint64_t backs_table_revision_{};
    std::uint64_t flags_table_revision_{};
    std::vector<std::string> flag_row_paths_;
    bool flag_has_paths_{};
    std::uint64_t flag_assign_combo_revision_{};
    std::uint64_t ref_area_combo_revision_{};
    // ホストに実際のクリップボード実装がない場合だけ使う。 ネイ
    // ティブ側が文字列を提供できる場合は、常にそちらを優先する。
    std::string scene_clipboard_;
    // DのobjectIDValueはポインターと単調時刻を組み合
    // わせる。 Editorプロセス間で同じ共通処理を使い、さらに
    // 世代を加えて、同じ時刻の刻み内でのアドレス再利用も区別する。
    const std::string scene_drag_identity_{utils::objectIDValue(static_cast<const void*>(this))+":"+std::to_string(++detail::next_scene_drag_identity)};
    mutable std::vector<std::shared_ptr<lib::OwnedImageResource>> scene_alpha_uploads_;
    std::shared_ptr<void> scene_view_lifetime_{std::make_shared<int>(0)};
    areawindow::MenuActionRegistry* top_level_menu_registry_{};
    std::vector<std::pair<types::MenuID, std::uint64_t>>
        top_level_menu_bindings_;
    std::shared_ptr<areawindow::SceneViewRegistration> scene_view_registration_;
    areawindow::Signal<std::shared_ptr<card::CastCard>>::Connection
        ref_cast_connection_{};
    areawindow::Signal<std::shared_ptr<card::CastCard>>::Connection
        del_cast_connection_{};
    ExternalCardEditPtr external_card_edit_before_;
    std::vector<const AbstractArea*> ref_areas_;
    std::vector<std::string> ref_area_names_;
    std::vector<std::string> flag_assign_paths_;
    std::vector<std::size_t> ref_area_combo_indices_;
    std::vector<std::string> ref_card_paths_;
    std::string ref_card_path_;
    std::size_t ref_card_index_{};
    // DのComboは参照エリアなしで始まる。 ユーザーが
    // 明示的に対象を選ぶまで、添字0は仮表示にすぎない。
    bool ref_card_selected_{};
    std::unique_ptr<incsearch::IncSearch> ref_area_inc_search_;
    std::unique_ptr<incsearch::IncSearch> flag_inc_search_;
    std::unique_ptr<incsearch::IncSearch> flag_assign_inc_search_;
    bool flag_assign_has_paths_{};

    [[nodiscard]] static A* requireArea(A* value) {
        if (!value) throw std::invalid_argument("Area view requires an area");
        return value;
    }
};

using AreaView = AbstractAreaView<Area, MenuCard, true, true>;
using BattleView = AbstractAreaView<Battle, EnemyCard, true, false>;

/// 独立したセル一覧を編集するダイアログ用の、背景専用ビュー。
class BgImagesView final : public AbstractAreaView<BgImageContainer, std::nullptr_t, false, true> {
public:
    using Base = AbstractAreaView<BgImageContainer, std::nullptr_t, false, true>;
    BgImagesView(AreaViewContext& context, Props& prop, Summary* summ,
                 const Skin* force_skin, BgImageContainer& container,
                 std::shared_ptr<lib::Container> parent,
                 std::shared_ptr<undo::UndoManager> undo_manager = {},
                 bool show_inherit_backs = true, bool read_only = false)
        : Base(context, prop, summ, force_skin, container, std::move(parent),
               std::move(undo_manager), show_inherit_backs, read_only) {}
    BgImagesView(areaviewutils::Commons comm, Props& prop, const Summary* summ,
                 const Skin& skin, areaviewutils::UseCounterPtr counter,
                 BgImageContainer& container, lib::Container* parent,
                 AbstractArea* ref_target, bool show_inherit_backs, bool read_only)
        : BgImagesView(std::move(comm), prop, summ, skin, std::move(counter),
              container, parent, ref_target, show_inherit_backs, read_only,
              std::make_shared<undo::UndoManager>(100U)) {}
    // このコントローラーの破棄前でも、所有者を閉じると保持中のアダプターが終了する場合がある。
    void retire() noexcept {
        compatibility_lifetime_.reset();
        const auto disconnect=std::move(disconnect_undo_max_);
        this->retireSceneCallbacks();
        if(disconnect)try{disconnect();}catch(...){}
    }
    ~BgImagesView() { retire(); }
    [[nodiscard]] const areaviewutils::Commons& commons()const noexcept{return compatibility_commons_;}
    [[nodiscard]] areaviewutils::Commons& commons()noexcept{return compatibility_commons_;}
private:
    BgImagesView(areaviewutils::Commons comm, Props& prop, const Summary* summ,
                 const Skin& skin, areaviewutils::UseCounterPtr counter,
                 BgImageContainer& container, lib::Container* parent,
                 AbstractArea* ref_target, bool show_inherit_backs, bool read_only,
                 std::shared_ptr<undo::UndoManager> history)
        : Base(makeContext(comm,std::move(counter)),prop,const_cast<Summary*>(summ),
               &skin,container,parent ? std::shared_ptr<lib::Container>(parent,[](lib::Container*){}) : nullptr,
               history,show_inherit_backs,read_only), compatibility_commons_(std::move(comm)) {
        if(prop.var().refCardsAtEditBgImage.value)this->refAreaTarget(ref_target);
        // 従来の補助関数の明示的な上限指定を、共通の追加設定として残す。
        // 通常の経路と補助経路は、一つのUndoManagerを使う。
        const auto provider=compatibility_commons_.undoMaxProvider;
        history->max(provider ? provider() : compatibility_commons_.undoMaxEvent);
        const std::weak_ptr<void> life=compatibility_lifetime_;
        if(compatibility_commons_.subscribeUndoMax)
            disconnect_undo_max_=compatibility_commons_.subscribeUndoMax([this,history,provider,life]{
                if(life.expired() || !provider)return;
                const auto maximum=provider();
                if(life.expired())return;
                history->max(maximum);
                if(life.expired())return;
                refreshUndoToolbarEnablement();
            });
    }
    static AreaViewContext makeContext(const areaviewutils::Commons& comm,
                                       areaviewutils::UseCounterPtr counter) {
        AreaViewContext context;context.services=comm.services;
        context.use_counter_owner=std::move(counter);return context;
    }
    areaviewutils::Commons compatibility_commons_;
    std::shared_ptr<void> compatibility_lifetime_{std::make_shared<int>(0)};
    lib::Action disconnect_undo_max_;

};

}  // namespace cwxeditor::cwx::editor::gui::dwt::areaview
