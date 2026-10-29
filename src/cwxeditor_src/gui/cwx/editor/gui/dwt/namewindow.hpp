#pragma once

// cwx/editor/gui/dwt/namewindow.d
// のC++20移植。 ネイティブの表・シ
// ェル・メニュー・通知を共通libの部品
// で表す。名前の提供元をコールバックに分
// 離し、 生成済み互換型を公開せず、具体
// 的なUseCounter/Summary
// にもヘッドレスホストにも対応する。

#include "cwxeditor_src/core/cwx/path.hpp"
#include "cwxeditor_src/core/cwx/features.hpp"
#include "cwxeditor_src/core/cwx/types.hpp"
#include "cwxeditor_src/core/cwx/usecounter.hpp"
#include "cwxeditor_src/core/cwx/textholder.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dutils.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/customtable.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dmenu.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/incsearch.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/undo.hpp"
#include "cwxeditor_src/lib/clipboard.hpp"
#include "cwxeditor_src/lib/components.hpp"
#include "cwxeditor_src/lib/events.hpp"
#include "cwxeditor_src/lib/portable.hpp"
#include "cwxeditor_src/lib/services.hpp"
#include "cwxeditor_src/lib/types.hpp"
#include "cwxeditor_src/lib/window_host.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace cwxeditor::cwx::editor::gui::dwt::namewindow {

namespace lib = ::cwxeditor::lib;
namespace customtable = ::cwxeditor::cwx::editor::gui::dwt::customtable;
namespace incsearch = ::cwxeditor::cwx::editor::gui::dwt::incsearch;
namespace dmenu = ::cwxeditor::cwx::editor::gui::dwt::dmenu;
namespace undo = ::cwxeditor::cwx::editor::gui::dwt::undo;
namespace usecounter = ::cwxeditor::cwx::usecounter;

using String = lib::String;
using StringView = lib::StringView;
using Parent = lib::Container*;
using Shell = lib::Widget*;
using Widget = lib::Widget;
using WidgetPtr = lib::WidgetPtr;
using Container = lib::Container;
using ContainerPtr = std::shared_ptr<Container>;
using Table = customtable::TableModel;
using TablePtr = customtable::TableModelPtr;
using TableColumn = customtable::TableColumn;
using TableColumnPtr = customtable::TableColumnPtr;
using ClipboardBackend = lib::ClipboardBackend;
using ResourceId = lib::ResourceId;

using CouponId = usecounter::CouponId;
using GossipId = usecounter::GossipId;
using CompleteStampId = usecounter::CompleteStampId;
using KeyCodeId = usecounter::KeyCodeId;
using CellNameId = usecounter::CellNameId;
using CardGroupId = usecounter::CardGroupId;
using CouponType = ::cwxeditor::cwx::features::CouponType;
using FKCKind = ::cwxeditor::cwx::types::FKCKind;
using MenuID = ::cwxeditor::cwx::types::MenuID;

/// 通常Commons.prop.msgs/imagesから渡される文字列・画像ラベル。
/// プロパティサービスがなくても、ヘッドレスのNameWindowで使える既定値を持つ。
struct NameMessages {
    String idName{"名称"};
    String idCount{"使用数"};
    String coupon{"クーポン"};
    String gossip{"ゴシップ"};
    String completeStamp{"終了印"};
    String keyCode{"キーコード"};
    String cellName{"セル名称"};
    String cardGroup{"カードグループ"};
    String idStatus{"%2$s件の%1$s"};
    String idStatusSelected{"%1$s (%2$s件を選択)"};
};

/// 表の最初の列で使う資源ハンドル。バックエンドは画像資
/// 源として扱え、 0は画像なしの有効な代替値とする。
struct NameImages {
    // NameView::setDataが使う行の画像。
    ResourceId couponNormal{};
    ResourceId gossip{};
    ResourceId completeStamp{};
    ResourceId keyCode{};
    ResourceId backs{};
    ResourceId cards{};

    // NameWindow::imageが使うタブ・ウィンドウ
    // 画像。 元のプロパティ群でも、行画像とは別に持っている。
    ResourceId couponView{};
    ResourceId gossipView{};
    ResourceId completeStampView{};
    ResourceId keyCodeView{};
    ResourceId cellNameView{};
    ResourceId cardGroupView{};
};

/// NameViewが使うデリゲートに対応する共通型。keys/count
/// は読み取り専用の問い合わせ。 renameは、変更先ID
/// が既にある場合など置換を拒否したときにfalseを返す。
struct RenameReplay {
    lib::Action undo;
    lib::Action redo;
};

struct NameSource {
    using Keys = lib::Callback<std::vector<String>()>;
    using Count = lib::Callback<std::uint32_t(StringView)>;
    using Rename = lib::Callback<bool(StringView, StringView)>;
    using CouponConvert = lib::Callback<String(StringView, CouponType, bool)>;
    using KeyCodeConvert = lib::Callback<String(StringView, FKCKind)>;

    // DのrefIDFSと同じく、この名前の種類が使う変更済み変数だけを調べる。
    std::function<bool(std::span<const String>, std::span<const String>,
                       std::span<const String>)> referencesVariables{};
    Keys keys{};
    Count count{};
    Rename rename{};
    // 改名・統合前に元のUser群を取得する。 その組だけを再
    // 生し、変更先の名前を以前から使うUserは改名しない。
    std::function<std::optional<RenameReplay>(StringView, StringView)> prepareRename{};
    CouponConvert convertCoupon{};
    KeyCodeConvert convertKeyCode{};
};

enum class SortColumn { Name, UseCount };
enum class SortDirection { Up, Down };
enum class NameIDKind { Coupon, Gossip, CompleteStamp, KeyCode, CellName, CardGroup };

/// namewindow.dが使うCommonsの小さな具体型。 アプリはNameView::summary()
/// から実際のSummary/UseCounterを渡し、 テ
/// スト・ヘッドレスはNameSourceを直接設定できる。
struct Commons {
    lib::Services services{};
    NameMessages messages{};
    NameImages images{};
    NameSource source{};
    std::shared_ptr<ClipboardBackend> clipboard{};

    /// NameViewのポップアップは、メインメニューと同じ翻訳済みMenuID
    /// 文字列と画像を使う。 単独・ヘッドレスのビュー向けの代替も残す。
    std::function<String(MenuID)> menuText{};
    std::function<ResourceId(MenuID)> menuImage{};

    /// 任意のシステム変換サービス。なければNameSource
    /// の変換通知を使い、 それもなければ値を変えず返す。
    std::function<String(StringView, CouponType, bool)> convertCoupon{};
    std::function<String(StringView, FKCKind)> convertKeyCode{};

    /// 任意の形式対応のクリップボード書き込み。 ネイティブ・XML表現を書
    /// いたらtrue、通常文字列への代替を求める場合はfalseを返す。
    std::function<bool(const std::vector<String>&, StringView)> copyConverted{};

    /// Commonsに対応する、汎用のEditor通知・操作。
    lib::Action refreshToolBar{};
    lib::Action replText{};
    lib::Action refUseCount{};
    lib::Action refIds{};
    /// Dは文字列IDの種類ごとに更新通知を持つ
    /// 。 個別通知に対応するホストはこれを登録
    /// し、従来ホストは上の汎用通知を使える。
    std::function<void(NameIDKind)> refNameID{};
    std::function<void(NameIDKind, StringView, bool)> replaceID{};
    std::function<void(Widget*, StringView)> setStatusLine{};
    /// DはIDの種類ごとに独立した並べ替え列・方向を保
    /// 存する。 従来の通知は単独呼び出し向けに残し、
    /// 製品では型付きの通知で各タブを復元・保存する。
    std::function<std::pair<SortColumn, SortDirection>(NameIDKind)> loadSort{};
    std::function<void(NameIDKind, SortColumn, SortDirection)> storeTypedSort{};
    std::function<void(SortColumn, SortDirection)> storeSort{};

    std::size_t undoMaxEvent{100U};
    std::size_t undoMaxReplace{100U};
    bool logicalSort{true};
    std::function<bool()> logicalSortProvider{};
    float nameColumnWidth{330.0F};
    float useCountColumnWidth{60.0F};
    // DのsaveColumnWidthと同じく、再構成時に現在のPropsを読み、サイズ変更を保存する。
    std::function<std::pair<float, float>()> loadColumnWidths{};
    std::function<void(std::size_t, float)> storeColumnWidth{};
    bool xmlCopy{};
    std::function<std::size_t()> undoMaxReplaceProvider{};
    /// DのTableTextEditと同じく、現在のEditor設定からQuick/Slowを選ぶ。
    std::function<bool()> quickEditProvider{};

    [[nodiscard]] std::shared_ptr<ClipboardBackend> clipboardBackend() const {
        return clipboard ? clipboard : services.clipboard;
    }
    [[nodiscard]] String localizedMenuText(const MenuID id, const StringView fallback) const {
        const auto value = menuText ? menuText(id) : String{};
        return value.empty() ? String(fallback) : value;
    }
    [[nodiscard]] ResourceId localizedMenuImage(const MenuID id) const {
        return menuImage ? menuImage(id) : 0U;
    }
};

// Dのクーポン・キーコード変換結果を出力する、共通の製品用処理。
// 未知の種類は拒否し、埋め込みホストの文字列による代替を残す。
[[nodiscard]] bool writeConvertedNames(const std::shared_ptr<ClipboardBackend>& clipboard,
                                       bool xmlCopy, const std::vector<String>& values,
                                       StringView kind);

/// 六つのNameView!IDで共用する、テンプレートでない実装。
/// UI境界のIDはUTF-8文字列で持ち、Summary/UseCounter
/// を渡された場合は下の型付きラッパーで変換する。
class NameViewBase {
public:
    NameViewBase(Commons comm, NameIDKind id_kind, String type_name, ResourceId image,
                 Shell parent_shell = nullptr, Parent parent = nullptr,
                 undo::UndoManager* undo_manager = nullptr,
                 bool read_only = false);
    virtual ~NameViewBase();

    NameViewBase(const NameViewBase&) = delete;
    NameViewBase& operator=(const NameViewBase&) = delete;

    void reconstruct(Parent parent);
    [[nodiscard]] const ContainerPtr& widget() const noexcept { return widget_; }
    [[nodiscard]] ContainerPtr& widget() noexcept { return widget_; }
    [[nodiscard]] const TablePtr& table() const noexcept { return list_; }
    [[nodiscard]] TablePtr& table() noexcept { return list_; }
    [[nodiscard]] undo::UndoManager* undoManager() const noexcept { return undo_; }
    /// 型付きNameWindowは、クーポン・キーコード変換
    /// のサブメニューなどD固有の拡張をここへ渡す。 拡張が
    /// なければ、基底ビューは共通のIDポップアップを使う。
    void setContextMenuProvider(std::function<std::shared_ptr<lib::Menu>()> value) {
        context_menu_provider_ = std::move(value);
    }

    void setSource(NameSource source);
    [[nodiscard]] const NameSource& source() const noexcept { return source_; }
    [[nodiscard]] const NameSource& fallbackSource() const noexcept {
        return fallback_source_;
    }
    [[nodiscard]] const std::shared_ptr<void>& summaryOwner() const noexcept {
        return summary_owner_;
    }
    void setSummaryOwner(std::shared_ptr<void> value) noexcept {
        ++publication_revision_;
        summary_owner_ = std::move(value);
    }

    void refreshStatusLine();
    [[nodiscard]] const String& statusLine() const noexcept { return status_line_; }
    [[nodiscard]] bool canIncSearch() const noexcept { return has_name_; }
    void incSearch();
    [[nodiscard]] std::shared_ptr<incsearch::IncSearch> incSearchControl() const noexcept {
        return inc_search_;
    }

    [[nodiscard]] bool canEditName() const noexcept;
    [[nodiscard]] std::shared_ptr<lib::TextEdit> editName();
    [[nodiscard]] const std::shared_ptr<lib::TextEdit>& activeEditor() const noexcept {
        return active_editor_;
    }

    [[nodiscard]] bool canUndo() const noexcept;
    void undo();
    [[nodiscard]] bool canRedo() const noexcept;
    void redo();
    [[nodiscard]] bool canSelectAll() const noexcept;
    void selectAll();
    [[nodiscard]] bool canFindID() const noexcept;
    void findID();
    [[nodiscard]] bool isReadOnly() const noexcept { return read_only_; }

    void selectStrings(std::span<const String> ids);
    [[nodiscard]] std::vector<String> selectedStrings() const;
    [[nodiscard]] const std::vector<String>& nameStrings() const noexcept {
        return name_list_;
    }

    void changed();
    void refID();
    void refIDFS(std::span<const String> flags = {},
                 std::span<const String> steps = {},
                 std::span<const String> variants = {});
    void refUseCount();
    /// NameView!ID.refUndoMaxと同じく、局所的に所有する履歴だけがEditor
    /// 設定へ追従する。 ホストから渡された履歴は、引き続きホストが管理する。
    void refUndoMax();
    void sort();
    void updateList();

    void copy();
    void cut() noexcept {}
    void paste() noexcept {}
    void del() noexcept {}
    void clone() noexcept {}

    [[nodiscard]] bool canDoTCPD() const noexcept;
    [[nodiscard]] bool canDoT() const noexcept { return false; }
    [[nodiscard]] bool canDoC() const noexcept;
    [[nodiscard]] bool canDoP() const noexcept { return false; }
    [[nodiscard]] bool canDoD() const noexcept { return false; }
    [[nodiscard]] bool canDoClone() const noexcept { return false; }

    [[nodiscard]] bool canCopyWith() const noexcept;
    void copyWith(std::function<String(StringView)> convert,
                  StringView kind = {});
    [[nodiscard]] const String& lastFindID() const noexcept { return last_find_id_; }

    [[nodiscard]] SortColumn sortColumn() const noexcept { return sort_column_; }
    [[nodiscard]] SortDirection sortDirection() const noexcept { return sort_direction_; }
    void sortColumn(SortColumn value) noexcept { sort_column_ = value; }
    void sortDirection(SortDirection value) noexcept { sort_direction_ = value; }
    [[nodiscard]] const TableColumnPtr& nameColumn() const noexcept { return name_column_; }
    [[nodiscard]] const TableColumnPtr& useCountColumn() const noexcept {
        return use_count_column_;
    }

protected:
    [[nodiscard]] bool readOnly() const noexcept { return read_only_; }
    [[nodiscard]] const Commons& commons() const noexcept { return comm_; }
    [[nodiscard]] Commons& commons() noexcept { return comm_; }
    [[nodiscard]] String typeName() const { return type_name_; }
    [[nodiscard]] ResourceId image() const noexcept { return image_; }
    [[nodiscard]] bool inProc() const noexcept { return in_proc_; }
    [[nodiscard]] bool applyRename(StringView old_name, StringView new_name,
                                   std::optional<RenameReplay>* replay = nullptr);
    void refreshRename();
    /// Dのクーポン種類の変換を、一件の履歴として適用する。 record_undo
    /// をfalseにするのは、その処理自身のUndoを再生中の場合だけ。
    [[nodiscard]] bool renameBatch(
        const std::vector<std::pair<String, String>>& replacements,
        bool record_undo = true, bool skip_occupied = false);
    void editEnd(std::size_t row, std::size_t column, String new_text);
    [[nodiscard]] std::shared_ptr<lib::TextEdit> createEditor(std::size_t row,
                                                               std::size_t column);
    void attachEditorTextMenu(const std::shared_ptr<lib::TextEdit>& editor);
    void toggleSort(SortColumn column);

private:
    class RenameUndo;
    class RenameBatchUndo;

    Commons comm_{};
    NameIDKind id_kind_{NameIDKind::Coupon};
    String type_name_{};
    ResourceId image_{};
    Shell parent_shell_{};
    Parent parent_{};
    bool read_only_{};
    bool in_proc_{};
    bool ref_undo_{};
    bool has_name_{};
    bool updating_{};
    String status_line_{};
    String last_find_id_{};
    NameSource fallback_source_{};
    NameSource source_{};
    std::shared_ptr<void> summary_owner_{};
    ContainerPtr widget_{};
    TablePtr list_{};
    TableColumnPtr name_column_{};
    TableColumnPtr use_count_column_{};
    std::shared_ptr<incsearch::IncSearch> inc_search_{};
    std::shared_ptr<lib::TextEdit> active_editor_{};
    dmenu::TextMenuModifyPtr active_editor_menu_{};
    std::shared_ptr<lib::Menu> context_menu_{};
    std::function<std::shared_ptr<lib::Menu>()> context_menu_provider_{};
    std::shared_ptr<undo::UndoManager> owned_undo_{};
    undo::UndoManager* undo_{};
    std::vector<String> local_keys_{};
    std::vector<String> name_list_{};
    SortColumn sort_column_{SortColumn::Name};
    SortDirection sort_direction_{SortDirection::Up};
    std::uint64_t selection_listener_{};
    std::uint64_t edit_listener_{};
    std::uint64_t pointer_listener_{};
    std::uint64_t name_column_listener_{};
    std::uint64_t use_count_column_listener_{};

    void installFallbackSource();
    void installTableListeners();
    void rebuildContextMenu();
    void setSelectionSilently(std::vector<std::size_t> values);

    std::uint64_t publication_revision_{};
    // 保持されたUI通知や同期的な提供元から、破棄済みビューを
    // 使わせない。 コピー元と所有者を解放する前に無効化する。
    std::shared_ptr<int> callback_lifetime_{std::make_shared<int>(0)};
};

/// NameView!IDの公開形式を保つ型付きの窓口。 UI
/// と履歴の全状態はNameViewBaseへ委譲する。
template <typename ID>
class NameView final : public NameViewBase {
public:
    NameView(Commons comm, Shell parent_shell = nullptr, Parent parent = nullptr,
             undo::UndoManager* undo_manager = nullptr, bool read_only = false)
        : NameViewBase(comm, defaultIDKind(), defaultTypeName(), defaultImage(comm), parent_shell, parent,
                       undo_manager, read_only) {}

    template <typename SummaryLike>
    void summary(const std::shared_ptr<SummaryLike>& value) {
        if (summaryOwner() == value) return; // Dと同じ同一性の検査で、履歴を保つ。
        setSummaryOwner(value);
        auto source = fallbackSource();
        if (!value) {
            setSource(std::move(source));
            if (undoManager()) undoManager()->reset();
            return;
        }
        if constexpr (requires(const SummaryLike& summary) { summary.useCounter(); }) {
            const auto counter = value->useCounter();
            if (counter) {
                if constexpr (std::is_same_v<ID, CouponId> || std::is_same_v<ID, GossipId>) {
                    source.referencesVariables = [counter](std::span<const String> flags,
                            std::span<const String> steps, std::span<const String> variants) {
                        namespace text = ::cwxeditor::cwx::textholder;
                        constexpr auto type = std::is_same_v<ID, CouponId>
                            ? text::TextHolderType::Coupon : text::TextHolderType::Gossip;
                        const auto uses = [&]<typename VariableID>(std::span<const String> paths) {
                            for (const auto& path : paths) {
                                for (const auto& user : counter->values(usecounter::toID<VariableID>(path))) {
                                    if (!user) continue;
                                    const auto holder = std::dynamic_pointer_cast<text::SimpleTextHolder>(user->owner());
                                    if (holder && holder->type() == type) return true;
                                }
                            }
                            return false;
                        };
                        return uses.template operator()<usecounter::FlagId>(flags) ||
                               uses.template operator()<usecounter::StepId>(steps) ||
                               uses.template operator()<usecounter::VariantId>(variants);
                    };
                }
                source.keys = [counter] {
                    std::vector<String> result;
                    for (const auto& id : counter->template keys<ID>())
                        result.push_back(static_cast<String>(id));
                    return result;
                };
                source.count = [counter](const StringView id) {
                    return counter->template get<ID>(usecounter::toID<ID>(id));
                };
                source.prepareRename = [counter, value](const StringView old_name,
                        const StringView new_name) -> std::optional<RenameReplay> {
                    const auto old_id = usecounter::toID<ID>(old_name);
                    const auto new_id = usecounter::toID<ID>(new_name);
                    if (old_id == new_id || old_name.empty() || new_name.empty()) return std::nullopt;
                    auto users = counter->template values<ID>(old_id);
                    // Dと同じく、通常カウンターと子カウンターのUserを記
                    // 録する。 子がなければ共通カウンターは自分自身を返す。
                    if (const auto sub = counter->sub(); sub && sub != counter) {
                        for (const auto& user : sub->template values<ID>(old_id))
                            if (user && std::find(users.begin(), users.end(), user) == users.end())
                                users.push_back(user);
                    }
                    if (users.empty()) return std::nullopt;
                    const auto apply = [users, value, counter](const ID& id) {
                        for (const auto& user : users)
                            if (user) usecounter::UseCounter::replaceID(id, user);
                        if constexpr (requires(SummaryLike& summary) { summary.changed(); })
                            value->changed();
                    };
                    return RenameReplay{[apply, old_id] { apply(old_id); },
                                        [apply, new_id] { apply(new_id); }};
                };
                source.rename = [counter, value](const StringView old_name,
                                                 const StringView new_name) {
                    const auto old_id = usecounter::toID<ID>(old_name);
                    const auto new_id = usecounter::toID<ID>(new_name);
                    if (old_id == new_id || counter->template get<ID>(old_id) == 0U)
                        return false;
                    if (counter->template get<ID>(new_id) != 0U) return false;
                    counter->template change<ID>(old_id, new_id);
                    if constexpr (requires(SummaryLike& summary) { summary.changed(); })
                        value->changed();
                    return true;
                };
            }
        }
        setSource(std::move(source));
        if (undoManager()) undoManager()->reset();
    }

    void select(std::span<const ID> ids) {
        std::vector<String> values;
        values.reserve(ids.size());
        for (const auto& id : ids) values.push_back(static_cast<String>(id));
        selectStrings(values);
    }
    void select(const std::vector<ID>& ids) { select(std::span<const ID>(ids)); }

    [[nodiscard]] std::vector<ID> nameList() const {
        std::vector<ID> result;
        result.reserve(nameStrings().size());
        for (const auto& value : nameStrings()) result.emplace_back(value);
        return result;
    }

    [[nodiscard]] bool canConvType(const CouponType type) const {
        if constexpr (std::is_same_v<ID, CouponId>) {
            if (!canCopyWith()) return false;
            const auto converter = [this, type](const StringView value) {
                if (commons().convertCoupon) return commons().convertCoupon(value, type, false);
                if (source().convertCoupon) return source().convertCoupon(value, type, false);
                return String(value);
            };
            for (const auto& value : selectedStrings()) {
                const auto converted = converter(value);
                if (converted == value) continue;
                if (!source().count || source().count(converted) == 0U) return true;
            }
            return false;
        } else {
            return false;
        }
    }
    void convType(const CouponType type) {
        if constexpr (std::is_same_v<ID, CouponId>) {
            if (!canConvType(type)) return;
            const auto converter = [this, type](const StringView value) {
                if (commons().convertCoupon) return commons().convertCoupon(value, type, false);
                if (source().convertCoupon) return source().convertCoupon(value, type, false);
                return String(value);
            };
            std::vector<std::pair<String, String>> replacements;
            std::vector<String> destinations;
            const bool sequential = !lib::editor_window_host_options().all_mode;
            for (const auto& value : selectedStrings()) {
                const auto converted = converter(value);
                // ALLは従来の事前絞り込みを保つ。単独起動ではDの順序
                // に合わせ、 各改名時まで移動先の存在確認を遅らせる。
                if (converted.empty() || converted == value ||
                    (!sequential && source().count && source().count(converted) != 0U) ||
                    (!sequential && std::find(destinations.begin(), destinations.end(), converted) !=
                        destinations.end()))
                    continue;
                destinations.push_back(converted);
                replacements.emplace_back(value, converted);
            }
            // Dと同じく、先行する変換が済んでから各移動先を調べる。
            (void)renameBatch(replacements, true, sequential);
        }
    }
    void copyWithType(const CouponType type) {
        if constexpr (std::is_same_v<ID, CouponId>) {
            const auto converter = [this, type](const StringView value) {
                if (commons().convertCoupon) return commons().convertCoupon(value, type, false);
                if (source().convertCoupon) return source().convertCoupon(value, type, false);
                return String(value);
            };
            // DのCopyTypeConvertedCouponと同じく、複数の値が
            // 同じになっても シナリオは編集せず、変換したクーポンXMLを書き出す。
            copyWith(converter, "coupon");
        }
    }

    void copyKeyCodeWith(const FKCKind kind) {
        if constexpr (std::is_same_v<ID, KeyCodeId>) {
            if (!canCopyWith()) return;
            const auto converter = [this, kind](const StringView value) {
                if (commons().convertKeyCode) return commons().convertKeyCode(value, kind);
                if (source().convertKeyCode) return source().convertKeyCode(value, kind);
                return String(value);
            };
            copyWith(converter, "keycode");
        }
    }

private:
    [[nodiscard]] static constexpr NameIDKind defaultIDKind() noexcept {
        if constexpr (std::is_same_v<ID, CouponId>) return NameIDKind::Coupon;
        else if constexpr (std::is_same_v<ID, GossipId>) return NameIDKind::Gossip;
        else if constexpr (std::is_same_v<ID, CompleteStampId>) return NameIDKind::CompleteStamp;
        else if constexpr (std::is_same_v<ID, KeyCodeId>) return NameIDKind::KeyCode;
        else if constexpr (std::is_same_v<ID, CellNameId>) return NameIDKind::CellName;
        else return NameIDKind::CardGroup;
    }
    [[nodiscard]] static String defaultTypeName() {
        if constexpr (std::is_same_v<ID, CouponId>) return "クーポン";
        else if constexpr (std::is_same_v<ID, GossipId>) return "ゴシップ";
        else if constexpr (std::is_same_v<ID, CompleteStampId>) return "終了印";
        else if constexpr (std::is_same_v<ID, KeyCodeId>) return "キーコード";
        else if constexpr (std::is_same_v<ID, CellNameId>) return "セル名称";
        else if constexpr (std::is_same_v<ID, CardGroupId>) return "カードグループ";
        else return "ID";
    }
    [[nodiscard]] static ResourceId defaultImage(const Commons& comm) noexcept {
        if constexpr (std::is_same_v<ID, CouponId>) return comm.images.couponNormal;
        else if constexpr (std::is_same_v<ID, GossipId>) return comm.images.gossip;
        else if constexpr (std::is_same_v<ID, CompleteStampId>) return comm.images.completeStamp;
        else if constexpr (std::is_same_v<ID, KeyCodeId>) return comm.images.keyCode;
        else if constexpr (std::is_same_v<ID, CellNameId>) return comm.images.backs;
        else if constexpr (std::is_same_v<ID, CardGroupId>) return comm.images.cards;
        else return 0U;
    }
};

template <typename ID>
class NameWindow final {
public:
    NameWindow(Commons comm, Shell parent_shell = nullptr, Parent parent = nullptr,
               undo::UndoManager* undo_manager = nullptr, bool read_only = false)
        : comm_(std::move(comm)), view_(comm_, parent_shell, parent, undo_manager, read_only) {
        view_.setContextMenuProvider([this] {
            buildMenu();
            return menu_;
        });
        buildMenu();
        if (parent) reconstruct(parent);
    }

    NameWindow(const NameWindow&) = delete;
    NameWindow& operator=(const NameWindow&) = delete;
    ~NameWindow() { lifetime_.reset(); if (menu_) menu_->on_command = {}; }

    void reconstruct(Parent parent) {
        view_.reconstruct(parent);
        buildMenu();
    }
    void select(std::span<const ID> ids) {
        view_.select(ids);
        buildMenu();
    }
    void select(const std::vector<ID>& ids) { select(std::span<const ID>(ids)); }

    [[nodiscard]] ContainerPtr shell() const noexcept { return view_.widget(); }
    [[nodiscard]] undo::UndoManager* undoManager() const noexcept {
        return view_.undoManager();
    }
    template <typename SummaryLike>
    void summary(const std::shared_ptr<SummaryLike>& value) {
        view_.summary(value);
        buildMenu();
    }

    // MainWindowが対応するCommonsの通知を
    // ここへ送る。 通知の所有権をドックビューの外へ置き、
    // 別の全体状態管理を作らず安全に終了できるようにする。
    void changed() { view_.changed(); buildMenu(); }
    void refID() { view_.refID(); buildMenu(); }
    void refIDFS() { view_.refIDFS(); buildMenu(); }
    template <typename Flags, typename Steps, typename Variants>
    void refIDFS(const Flags& flags, const Steps& steps, const Variants& variants) {
        const auto paths = [](const auto& values) {
            std::vector<String> result;
            for (const auto& value : values) if (value) result.push_back(value->path());
            return result;
        };
        view_.refIDFS(paths(flags), paths(steps), paths(variants));
        buildMenu();
    }
    void refUseCount() { view_.refUseCount(); buildMenu(); }
    void refUndoMax() { view_.refUndoMax(); buildMenu(); }

    [[nodiscard]] ResourceId image() const noexcept { return commImage(); }
    [[nodiscard]] String title() const { return viewTypeName(); }
    [[nodiscard]] const lib::Action& statusText() const noexcept { return status_text_; }
    void statusText(lib::Action value) { status_text_ = std::move(value); }

    void cut() { view_.cut(); }
    void copy() {
        const std::weak_ptr<int> life = lifetime_;
        view_.copy(); if (!life.expired()) buildMenu();
    }
    void paste() { view_.paste(); }
    void del() { view_.del(); }
    void clone() { view_.clone(); }

    [[nodiscard]] bool canDoTCPD() const noexcept { return view_.canDoTCPD(); }
    [[nodiscard]] bool canDoT() const noexcept { return view_.canDoT(); }
    [[nodiscard]] bool canDoC() const noexcept { return view_.canDoC(); }
    [[nodiscard]] bool canDoP() const noexcept { return view_.canDoP(); }
    [[nodiscard]] bool canDoD() const noexcept { return view_.canDoD(); }
    [[nodiscard]] bool canDoClone() const noexcept { return view_.canDoClone(); }

    [[nodiscard]] bool openCWXPath(const StringView path, const bool shell_activate) {
        if (::cwxeditor::cwx::path::cpcategory(path) != openedPath()) return false;
        ::cwxeditor::cwx::editor::gui::dwt::dutils::forceFocus(
            view_.table(), shell_activate, comm_.services);
        return true;
    }
    [[nodiscard]] std::vector<String> openedCWXPath() const { return {openedPath()}; }
    [[nodiscard]] const std::shared_ptr<lib::Menu>& menu() const noexcept { return menu_; }
    [[nodiscard]] NameView<ID>& view() noexcept { return view_; }
    [[nodiscard]] const NameView<ID>& view() const noexcept { return view_; }

    void invoke(const ::cwxeditor::cwx::types::MenuID id) {
        const std::weak_ptr<int> life = lifetime_;
        switch (id) {
        case ::cwxeditor::cwx::types::MenuID::IncSearch: view_.incSearch(); break;
        case ::cwxeditor::cwx::types::MenuID::EditProp: (void)view_.editName(); break;
        case ::cwxeditor::cwx::types::MenuID::Undo: view_.undo(); break;
        case ::cwxeditor::cwx::types::MenuID::Redo: view_.redo(); break;
        case ::cwxeditor::cwx::types::MenuID::Copy:
        case ::cwxeditor::cwx::types::MenuID::CopyAsText: view_.copy(); break;
        case ::cwxeditor::cwx::types::MenuID::SelectAll: view_.selectAll(); break;
        case ::cwxeditor::cwx::types::MenuID::FindID: view_.findID(); break;
        default: break;
        }
        if (!life.expired()) buildMenu();
    }

private:
    std::shared_ptr<int> lifetime_{std::make_shared<int>(0)};
    Commons comm_{};
    NameView<ID> view_;
    std::shared_ptr<lib::Menu> menu_{std::make_shared<lib::Menu>(true)};
    lib::Action status_text_{};

    [[nodiscard]] ResourceId commImage() const noexcept {
        if constexpr (std::is_same_v<ID, CouponId>) return comm_.images.couponView;
        else if constexpr (std::is_same_v<ID, GossipId>) return comm_.images.gossipView;
        else if constexpr (std::is_same_v<ID, CompleteStampId>) return comm_.images.completeStampView;
        else if constexpr (std::is_same_v<ID, KeyCodeId>) return comm_.images.keyCodeView;
        else if constexpr (std::is_same_v<ID, CellNameId>) return comm_.images.cellNameView;
        else if constexpr (std::is_same_v<ID, CardGroupId>) return comm_.images.cardGroupView;
        else return 0U;
    }
    [[nodiscard]] String viewTypeName() const {
        if constexpr (std::is_same_v<ID, CouponId>) return comm_.messages.coupon;
        else if constexpr (std::is_same_v<ID, GossipId>) return comm_.messages.gossip;
        else if constexpr (std::is_same_v<ID, CompleteStampId>) return comm_.messages.completeStamp;
        else if constexpr (std::is_same_v<ID, KeyCodeId>) return comm_.messages.keyCode;
        else if constexpr (std::is_same_v<ID, CellNameId>) return comm_.messages.cellName;
        else if constexpr (std::is_same_v<ID, CardGroupId>) return comm_.messages.cardGroup;
        else return "ID";
    }
    [[nodiscard]] String openedPath() const {
        if constexpr (std::is_same_v<ID, CouponId>) return "couponview";
        else if constexpr (std::is_same_v<ID, GossipId>) return "gossipview";
        else if constexpr (std::is_same_v<ID, CompleteStampId>) return "completestampview";
        else if constexpr (std::is_same_v<ID, KeyCodeId>) return "keycodeview";
        else if constexpr (std::is_same_v<ID, CellNameId>) return "cellnameview";
        else if constexpr (std::is_same_v<ID, CardGroupId>) return "cardgroupview";
        else return "idview";
    }
    void buildMenu() {
        const auto item = [this](const MenuID id, StringView fallback,
                                 String command, const bool enabled = true) {
            return lib::MenuItem{comm_.localizedMenuText(id, fallback), std::move(command),
                                 {}, comm_.localizedMenuImage(id), enabled};
        };
        const auto separator = [] {
            lib::MenuItem value;
            value.separator = true;
            return value;
        };
        menu_->items = {
            item(MenuID::IncSearch, "インクリメンタル検索", "inc_search",
                 view_.canIncSearch()),
            separator(),
        };
        if (!view_.isReadOnly()) {
            menu_->items.push_back(item(MenuID::EditProp, "名前を編集", "edit_name",
                                        view_.canEditName()));
            menu_->items.push_back(separator());
        }
        menu_->items.push_back(item(MenuID::Undo, "元に戻す", "undo", view_.canUndo()));
        menu_->items.push_back(item(MenuID::Redo, "やり直す", "redo", view_.canRedo()));
        menu_->items.push_back(separator());
        menu_->items.push_back(
            item(MenuID::CopyAsText, "テキストとしてコピー", "copy", view_.canDoC()));
        menu_->items.push_back(separator());
        menu_->items.push_back(
            item(MenuID::SelectAll, "すべて選択", "select_all", view_.canSelectAll()));
        if constexpr (std::is_same_v<ID, CouponId>) {
            menu_->items.push_back(separator());
            auto convert = item(MenuID::ConvertCouponType, "クーポン種別を変換",
                                "coupon-convert", view_.canConvType(CouponType::Normal) ||
                                view_.canConvType(CouponType::Hide) ||
                                view_.canConvType(CouponType::System) ||
                                view_.canConvType(CouponType::Dur) ||
                                view_.canConvType(CouponType::DurBattle));
            convert.children = {
                item(MenuID::ConvertCouponTypeNormal, "通常クーポンへ変換",
                     "coupon-normal", view_.canConvType(CouponType::Normal)),
                item(MenuID::ConvertCouponTypeHide, "隠蔽クーポンへ変換",
                     "coupon-hide", view_.canConvType(CouponType::Hide)),
                item(MenuID::ConvertCouponTypeDur, "時限クーポンへ変換",
                     "coupon-duration", view_.canConvType(CouponType::Dur)),
                item(MenuID::ConvertCouponTypeDurBattle, "戦闘時限クーポンへ変換",
                     "coupon-battle", view_.canConvType(CouponType::DurBattle)),
                item(MenuID::ConvertCouponTypeSystem, "システムクーポンへ変換",
                     "coupon-system", view_.canConvType(CouponType::System)),
            };
            menu_->items.push_back(std::move(convert));
            auto copy = item(MenuID::CopyTypeConvertedCoupon, "変換してコピー",
                             "coupon-copy", view_.canCopyWith());
            copy.children = {
                item(MenuID::CopyTypeConvertedCouponNormal, "通常クーポンとしてコピー",
                     "coupon-copy-normal", view_.canCopyWith()),
                item(MenuID::CopyTypeConvertedCouponHide, "隠蔽クーポンとしてコピー",
                     "coupon-copy-hide", view_.canCopyWith()),
                item(MenuID::CopyTypeConvertedCouponDur, "時限クーポンとしてコピー",
                     "coupon-copy-duration", view_.canCopyWith()),
                item(MenuID::CopyTypeConvertedCouponDurBattle, "戦闘時限クーポンとしてコピー",
                     "coupon-copy-battle", view_.canCopyWith()),
                item(MenuID::CopyTypeConvertedCouponSystem, "システムクーポンとしてコピー",
                     "coupon-copy-system", view_.canCopyWith()),
            };
            menu_->items.push_back(std::move(copy));
        } else if constexpr (std::is_same_v<ID, KeyCodeId>) {
            menu_->items.push_back(separator());
            auto copy = item(MenuID::CopyTimingConvertedKeyCode, "時期を変換してコピー",
                             "keycode-copy", view_.canCopyWith());
            copy.children = {
                item(MenuID::CopyTimingConvertedKeyCodeUse, "使用時キーコードとしてコピー",
                     "keycode-use", view_.canCopyWith()),
                item(MenuID::CopyTimingConvertedKeyCodeSuccess, "成功時キーコードとしてコピー",
                     "keycode-success", view_.canCopyWith()),
                item(MenuID::CopyTimingConvertedKeyCodeFailure, "失敗時キーコードとしてコピー",
                     "keycode-failure", view_.canCopyWith()),
                item(MenuID::CopyTimingConvertedKeyCodeHasNot, "所持無しキーコードとしてコピー",
                     "keycode-has-not", view_.canCopyWith()),
            };
            menu_->items.push_back(std::move(copy));
        }
        menu_->items.push_back(separator());
        menu_->items.push_back(item(MenuID::FindID, "IDを検索", "find_id",
                                    view_.canFindID()));
        menu_->on_command = [this, life = std::weak_ptr<int>(lifetime_)](const StringView command) {
            if (life.expired()) return;
            if (command == "inc_search") invoke(::cwxeditor::cwx::types::MenuID::IncSearch);
            else if (command == "edit_name") invoke(::cwxeditor::cwx::types::MenuID::EditProp);
            else if (command == "undo") invoke(::cwxeditor::cwx::types::MenuID::Undo);
            else if (command == "redo") invoke(::cwxeditor::cwx::types::MenuID::Redo);
            else if (command == "copy") invoke(::cwxeditor::cwx::types::MenuID::CopyAsText);
            else if (command == "select_all") invoke(::cwxeditor::cwx::types::MenuID::SelectAll);
            else if (command == "find_id") invoke(::cwxeditor::cwx::types::MenuID::FindID);
            else if constexpr (std::is_same_v<ID, CouponId>) {
                if (command == "coupon-normal")
                    view_.convType(CouponType::Normal);
                else if (command == "coupon-hide")
                    view_.convType(CouponType::Hide);
                else if (command == "coupon-system")
                    view_.convType(CouponType::System);
                else if (command == "coupon-duration")
                    view_.convType(CouponType::Dur);
                else if (command == "coupon-battle")
                    view_.convType(CouponType::DurBattle);
                else if (command == "coupon-copy-normal")
                    view_.copyWithType(CouponType::Normal);
                else if (command == "coupon-copy-hide")
                    view_.copyWithType(CouponType::Hide);
                else if (command == "coupon-copy-system")
                    view_.copyWithType(CouponType::System);
                else if (command == "coupon-copy-duration")
                    view_.copyWithType(CouponType::Dur);
                else if (command == "coupon-copy-battle")
                    view_.copyWithType(CouponType::DurBattle);
            } else if constexpr (std::is_same_v<ID, KeyCodeId>) {
                if (command == "keycode-use") view_.copyKeyCodeWith(FKCKind::Use);
                else if (command == "keycode-success") view_.copyKeyCodeWith(FKCKind::Success);
                else if (command == "keycode-failure") view_.copyKeyCodeWith(FKCKind::Failure);
                else if (command == "keycode-has-not") view_.copyKeyCodeWith(FKCKind::HasNot);
            }
        };
    }
};

using CouponWindow = NameWindow<CouponId>;
using GossipWindow = NameWindow<GossipId>;
using CompleteStampWindow = NameWindow<CompleteStampId>;
using KeyCodeWindow = NameWindow<KeyCodeId>;
using CellNameWindow = NameWindow<CellNameId>;
using CardGroupWindow = NameWindow<CardGroupId>;

}  // namespace cwxeditor::cwx::editor::gui::dwt::namewindow
