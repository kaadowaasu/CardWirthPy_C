#pragma once

// cwx/editor/gui/dwt/dutils.d
// の移植可能なC++20実装。 補
// 助処理はlibの部品・画像・サー
// ビスとコアモデルで表し、 ネイ
// ティブGUIとヘッドレステスト
// で同じ編集・描画コードを使う。

#include "cwxeditor_src/lib/components.hpp"
#include "cwxeditor_src/lib/events.hpp"
#include "cwxeditor_src/lib/graphics.hpp"
#include "cwxeditor_src/lib/portable.hpp"
#include "cwxeditor_src/lib/services.hpp"
#include "cwxeditor_src/lib/ui_actions.hpp"
#include "cwxeditor_src/core/cwx/card.hpp"
#include "cwxeditor_src/core/cwx/event.hpp"
#include "cwxeditor_src/core/cwx/flag.hpp"
#include "cwxeditor_src/core/cwx/path.hpp"
#include "cwxeditor_src/core/cwx/settings.hpp"
#include "cwxeditor_src/core/cwx/skin.hpp"
#include "cwxeditor_src/core/cwx/structs.hpp"
#include "cwxeditor_src/core/cwx/summary.hpp"
#include "cwxeditor_src/core/cwx/types.hpp"
#include "cwxeditor_src/core/cwx/usecounter.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dprops.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/incsearch.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/splitpane.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <stdexcept>
#include <string_view>
#include <sstream>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace cwxeditor::cwx::area { class AbstractSpCard; }
namespace cwxeditor::cwx::editor::gui::dwt::cardlist {
template <typename C> class CardList;
}

namespace cwxeditor::cwx::editor::gui::dwt::dutils {

namespace lib = ::cwxeditor::lib;
namespace card = ::cwxeditor::cwx::card;
namespace event = ::cwxeditor::cwx::event;
namespace flag = ::cwxeditor::cwx::flag;
namespace path = ::cwxeditor::cwx::path;
namespace settings = ::cwxeditor::cwx::settings;
namespace skin = ::cwxeditor::cwx::skin;
namespace structs = ::cwxeditor::cwx::structs;
namespace summary = ::cwxeditor::cwx::summary;
namespace types = ::cwxeditor::cwx::types;
namespace dprops = ::cwxeditor::cwx::editor::gui::dwt::dprops;
namespace cardlist = ::cwxeditor::cwx::editor::gui::dwt::cardlist;
namespace splitpane = ::cwxeditor::cwx::editor::gui::dwt::splitpane;

using String = lib::String;
using StringView = lib::StringView;
using Props = dprops::Props;
using Skin = skin::Skin;
using Summary = summary::Summary;
using CWXPath = path::CWXPath;
using Content = event::Content;
using SDialog = event::SDialog;
using WidgetPtr = lib::WidgetPtr;
using ContainerPtr = std::shared_ptr<lib::Container>;
using WindowPtr = std::shared_ptr<lib::Window>;
using TablePtr = std::shared_ptr<lib::TableView>;
using TreePtr = std::shared_ptr<lib::TreeView>;
using CardListCard = lib::Owner<card::Card>;
using CardListPtr = std::shared_ptr<cardlist::CardList<CardListCard>>;
using ItemViewPtr = std::shared_ptr<lib::ItemView>;
using TextEditPtr = std::shared_ptr<lib::TextEdit>;
using ComboPtr = std::shared_ptr<lib::ComboBox>;
using NumberEditPtr = std::shared_ptr<lib::NumberEdit>;
using ResourceId = lib::ResourceId;
using Item = std::optional<std::size_t>;
using TableItem = std::size_t;

/// 従来DWTのCommonsを受け取っていた編集補助処理の
/// 共有状態。 全項目を省略可能にし、小さなツールやヘッドレ
/// ステストでもEditor全体を作らず使えるようにする。
struct EditorContext {
    Props* prop{};
    const Skin* skin{};
    const Summary* summary{};
    lib::Services services{};
    lib::Action refreshToolbar{};
    lib::Action saveWindow{};
    bool comboListVisible{};
};

class ImageDataWithScale {
public:
    using SizeCallback = std::function<void(std::size_t, std::size_t)>;

    std::vector<SizeCallback> updateSizeEvent;

    ImageDataWithScale() = default;
    ImageDataWithScale(lib::ImageData data, std::uint32_t scale = 1U);

    [[nodiscard]] lib::ImageData& baseData() noexcept { return data_; }
    [[nodiscard]] const lib::ImageData& baseData() const noexcept { return data_; }
    [[nodiscard]] std::uint32_t scale() const noexcept { return scale_; }
    [[nodiscard]] lib::ImageData scaled(std::uint32_t targetScale);
    [[nodiscard]] lib::ImageData scaledPercent(std::uint32_t targetPercent);
    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] int getWidth(std::uint32_t targetScale) const noexcept;
    [[nodiscard]] int getHeight(std::uint32_t targetScale) const noexcept;
    [[nodiscard]] std::vector<lib::ImageData> allData() const;
    [[nodiscard]] std::size_t bufferSize() const noexcept;
    void destroyAllData() noexcept;

private:
    lib::ImageData data_;
    std::uint32_t scale_{1U};
    std::unordered_map<std::uint32_t, lib::ImageData> scaled_;
    std::unordered_map<std::uint32_t, lib::ImageData> scaled_percent_;
};

bool dwtImageSize(const Props* prop, const Skin* skin, const Summary* summ,
                  StringView path, std::uint32_t& width,
                  std::uint32_t& height, lib::Services services = {});
ImageDataWithScale loadImageWithScale(StringView path,
                                      std::uint32_t targetScale,
                                      bool mask = true,
                                      bool isImageCell = false,
                                      int maskX = 0, int maskY = 0,
                                      lib::Services services = {});
ImageDataWithScale loadImageWithScale(const Props* prop, const Skin* skin,
                                      const Summary* summ, StringView path,
                                      std::uint32_t targetScale,
                                      bool mask = true,
                                      bool isImageCell = false,
                                      int maskX = 0, int maskY = 0,
                                      const std::vector<String>& stratum = {},
                                      lib::Services services = {},
                                      bool* decoded = nullptr);
lib::ImageData loadImage(StringView path, bool mask = true,
                         bool isImageCell = false, int maskX = 0,
                         int maskY = 0, lib::Services services = {});
lib::ImageData loadImage(const Props* prop, const Skin* skin,
                         const Summary* summ, StringView path,
                         bool mask = true, bool isImageCell = false,
                         int maskX = 0, int maskY = 0,
                         const std::vector<String>& stratum = {},
                         lib::Services services = {});
lib::ImageData blankImage(int width = 1, int height = 1);

struct Event {
    WidgetPtr widget;
    int type{};
    lib::KeyEvent key;
    lib::PointerEvent pointer;
    bool handled{};
};
using EventHandler = std::function<void(Event&)>;
using Listener = std::shared_ptr<class ListenerObject>;
Listener listener(const WidgetPtr& widget, int type, EventHandler callback);
Listener listener(const WidgetPtr& widget, int type, lib::Action callback);
Listener listener(EventHandler callback);
Listener listener(lib::Action callback);

class ListenerObject {
public:
    explicit ListenerObject(EventHandler callback) : callback_(std::move(callback)) {}
    explicit ListenerObject(lib::Action callback)
        : callback_([callback = std::move(callback)](Event&) {
              if (callback) callback();
          }) {}
    void handle(Event& event) const { if (callback_) callback_(event); }

private:
    EventHandler callback_;
};

class SpinnerEdit : public std::enable_shared_from_this<SpinnerEdit> {
public:
    SpinnerEdit(NumberEditPtr spinner, std::function<void(int)> enter,
                std::function<void(int)> edit = {},
                std::function<int(int)> cancel = {});
    void bindCallbacks();
    void enter();
    void keyPressed(lib::KeyEvent& event);
    void focusGained();
    void focusLost();
    void valueChanged();

private:
    std::weak_ptr<lib::NumberEdit> spinner_;
    int oldValue_{};
    int oldEditing_{};
    std::function<void(int)> edit_;
    std::function<void(int)> enter_;
    std::function<int(int)> cancel_;
    bool suppressEdit_{};
    std::shared_ptr<int> lifetime_{std::make_shared<int>(0)};
};

enum class EditStartType { Normal, SingleClick, Quick };

class TextEditMFListener {
public:
    TextEditMFListener(EditorContext* context, WidgetPtr control,
                       std::function<void(Item)> startEdit,
                       std::function<Item()> selection,
                       std::function<Item(float, float)> selectionM);
    EditStartType quickStart(EditStartType value) noexcept;
    [[nodiscard]] EditStartType quickStart() const noexcept { return quickStart_; }
    void widgetSelected();
    void widgetDefaultSelected();
    void focusGained();
    void focusLost();
    void mouseDoubleClick();
    void mouseUp(lib::PointerEvent& event);

private:
    EditorContext* context_{};
    WidgetPtr control_;
    bool editorFocusOut_{};
    Item item_;
    Item oldSelection_;
    Item oldSelection2_;
    bool hasFocus_{};
    bool startPending_{};
    EditStartType quickStart_{EditStartType::Normal};
    std::function<Item()> selection_;
    std::function<Item(float, float)> selectionM_;
    std::function<void(Item)> startEdit_;
};

/// DのTextEditMFListener（editTriggerTypeのSlow/Quick
/// ）に対応する一覧の編集開始操作。 単独選択済みの項目を押して離すと、名前の
/// 行内編集を始める。Slowはダブルクリック時間だけ待ち、 ダブルクリックな
/// ら通常の項目実行を優先する。マウス・タッチに対応し、F2やプログラム編集は
/// 変えない。 TableViewのinline_edit_column/on_cell_edited
/// 、またはTreeViewのon_item_editedが設定済
/// みであること。 editableで個別項目の編集を拒否できる。
void installClickSelectedEdit(const std::shared_ptr<lib::TableView>& table,
                              lib::Services services, lib::WidgetPtr focus_scope,
                              std::function<bool()> quick = {},
                              std::function<bool(std::size_t)> editable = {});
void installClickSelectedEdit(const std::shared_ptr<lib::TreeView>& tree,
                              lib::Services services, lib::WidgetPtr focus_scope,
                              std::function<bool()> quick = {},
                              std::function<bool(lib::TreeItemId)> editable = {});
/// DのCardListEdit相当。共通のクリック処理へ選択カー
/// ドの識別子を渡し、 名前の帯だけを有効なポインター対象とする。
void installClickSelectedEdit(const CardListPtr& list,
                              lib::Services services, lib::WidgetPtr focus_scope,
                              std::function<bool()> quick = {},
                              std::function<bool(const CardListCard&)> editable = {},
                              std::function<void(const CardListCard&)> begin = {});

/// 独自の名前編集を持つ表（Dの各TableTextEdit
/// ）向けの同じ開始操作。 beginへ選択行を渡す。
void installClickSelectedAction(const std::shared_ptr<lib::TableView>& table,
                                lib::Services services,
                                std::function<void(std::size_t)> begin,
                                std::function<bool()> quick = {},
                                std::function<bool(std::size_t)> editable = {},
                                std::optional<std::size_t> edit_column = 0U);

class TextEditKListener {
public:
    TextEditKListener(std::function<void(Item)> startEdit,
                      std::function<Item()> selection);
    void keyPressed(lib::KeyEvent& event);

private:
    std::function<Item()> selection_;
    std::function<void(Item)> startEdit_;
};

class EditEnd {
public:
    std::vector<std::function<void(bool)>> exitEvent;

    EditEnd(EditorContext* context, ContainerPtr parent, WidgetPtr control,
            std::function<void(WidgetPtr)> end, bool canOpenCombo = true);
    void setFocus();
    [[nodiscard]] WidgetPtr editor() const noexcept { return control_; }
    void focusGained();
    void focusLost();
    [[nodiscard]] bool isExit() const noexcept { return exited_; }
    void enter();
    void cancel();

private:
    void focusOut();
    EditorContext* context_{};
    ContainerPtr parent_;
    WidgetPtr control_;
    std::function<void(WidgetPtr)> end_;
    bool canOpenCombo_{true};
    bool exited_{};
    bool entering_{};
    std::shared_ptr<int> lifetime_{std::make_shared<int>(0)};
};

TextEditPtr createTextEditor(EditorContext* context, Props* prop,
                             ContainerPtr parent, StringView text);

// シナリオディレクトリからの相対パスを求め、ネイティブの区切り
// を共通のスラッシュ表記へ変換してから翻訳ラベルを適用する。
[[nodiscard]] String searchResultScenarioRelativeLabel(
    StringView format, StringView resourcePath, StringView scenarioPath);

template <typename C = lib::ComboBox>
std::shared_ptr<C> createComboEditor(EditorContext* context, Props* prop,
                                     ContainerPtr parent,
                                     const std::vector<String>& strings,
                                     StringView text, bool readOnly = true,
                                     std::function<std::vector<String>(StringView)> filter = {}) {
    auto combo = std::make_shared<C>();
    combo->editable = !readOnly;
    combo->items.reserve(strings.size());
    for (const auto& value : strings) combo->items.push_back({value, value});
    combo->edit_text = String(text);
    for(std::size_t i=0;i<combo->items.size();++i)
        if(combo->items[i].text==text){combo->selected_index=i;break;}
    if (parent) parent->add(combo);
    if (readOnly && filter) {
        incsearch::IncSearchSettings settings;
        if(prop) {
            settings.startIncrementalSearchWhenKeyDown=prop->var().startIncrementalSearchWhenKeyDown.value;
            settings.startIncrementalSearchWhenKeyDownProvider = [prop] {
                return prop && prop->var().startIncrementalSearchWhenKeyDown.value;
            };
            settings.incrementalSearchBoxWidth=prop->var().incrementalSearchBoxWidth.value;
            settings.incrementalSearchType=prop->var().incrementalSearchType.value;
        }
        // 共通APIはDのIncSearchデリゲートの代わりに検索コールバッ
        // クを受け取る。 一度変換し、同じポップアップと選択規則へ接続する。
        (void)incsearch::attachComboSearch(context?context->services:lib::Services{},combo,{},
            [filter](const incsearch::IncSearch& search){
                std::vector<lib::ChoiceItem> items;
                for(const auto& value:filter(search.searchText()))items.push_back({value,value});
                return items;
            },std::move(settings),{},0,[prop](incsearch::SearchType type){
                if(prop)prop->var().incrementalSearchType.value=static_cast<std::int32_t>(type);
            });
    }
    if (!readOnly) {
        const auto all = strings;
        const std::weak_ptr<C> weak = combo;
        combo->on_text_changed = [weak, all, filter = std::move(filter), context](StringView value) {
            const auto control = weak.lock();
            if (!control) return;
            const auto candidates = filter ? filter(value) : all;
            std::vector<lib::ChoiceItem> items;
            items.reserve(candidates.size());
            for (const auto& candidate : candidates)
                items.push_back({candidate, candidate});
            const String edit_text(value);
            const std::weak_ptr<C> current_control = control;
            if (!lib::replace_combo_items(control, std::move(items), {}, edit_text,
                    [current_control, edit_text] {
                        const auto target = current_control.lock();
                        return target && target->edit_text == edit_text;
                    })) return;
            if (context) context->comboListVisible = true;
            if (context && context->services.gui)
                context->services.gui->request_redraw(control);
        };
        combo->on_focus = [context] { if (context) context->comboListVisible = true; };
        combo->on_blur = [context] { if (context) context->comboListVisible = false; };
    }
    return combo;
}

class AbstractTableEdit {
public:
    AbstractTableEdit(EditorContext* context, TablePtr table, int editColumn,
                      std::function<bool(TableItem, int)> canEdit = {});
    virtual ~AbstractTableEdit() = default;
    void startEdit();
    EditStartType quickStart(EditStartType value) noexcept;
    [[nodiscard]] EditStartType quickStart() const noexcept { return quickStart_; }
    void startEdit(TableItem item);
    [[nodiscard]] bool isEditing() const noexcept { return static_cast<bool>(editEnd_); }
    [[nodiscard]] WidgetPtr editor() const noexcept;
    void enter();
    void cancel();
    std::vector<std::function<void(bool)>> exitEvent;

protected:
    virtual WidgetPtr createEditor(TableItem item, int column);
    virtual void end(WidgetPtr control);
    [[nodiscard]] TablePtr table() const noexcept { return table_; }
    [[nodiscard]] int editColumn() const noexcept { return editColumn_; }
    [[nodiscard]] EditorContext* context() const noexcept { return context_; }
    [[nodiscard]] TableItem* editingItem() noexcept { return editingItem_ ? &*editingItem_ : nullptr; }

private:
    EditorContext* context_{};
    TablePtr table_;
    std::shared_ptr<EditEnd> editEnd_;
    std::optional<TableItem> editingItem_;
    std::vector<std::size_t> selectionsBeforeEdit_;
    int editColumn_{};
    std::function<bool(TableItem, int)> canEdit_;
    EditStartType quickStart_{EditStartType::Normal};
    std::shared_ptr<int> lifetime_{std::make_shared<int>(0)};
};

class TableTextEdit final : public AbstractTableEdit {
public:
    TableTextEdit(EditorContext* context, Props* prop, TablePtr table,
                  int editColumn,
                  std::function<void(TableItem, int, String)> editEnd = {},
                  std::function<bool(TableItem, int)> canEdit = {},
                  std::function<WidgetPtr(TableItem, int)> createEditor = {});
    [[nodiscard]] TextEditPtr editor() const noexcept;

protected:
    WidgetPtr createEditor(TableItem item, int column) override;
    void end(WidgetPtr control) override;

private:
    Props* prop_{};
    std::function<void(TableItem, int, String)> editEnd_;
    std::function<WidgetPtr(TableItem, int)> createEditor_;
};

template <typename C = lib::ComboBox>
class TableComboEdit final : public AbstractTableEdit {
public:
    using CreateCombo = std::function<void(TableItem, int, std::vector<String>&,
                                           String&, bool&)>;
    using EditCallback = std::function<void(TableItem, int, std::shared_ptr<C>)>;

    TableComboEdit(EditorContext* context, Props* prop, TablePtr table,
                   int editColumn, CreateCombo createCombo,
                   EditCallback editEnd = {},
                   std::function<bool(TableItem, int)> canEdit = {},
                   std::function<std::vector<String>(StringView)> filter = {})
        : AbstractTableEdit(context, std::move(table), editColumn,
                            std::move(canEdit)), prop_(prop),
          createCombo_(std::move(createCombo)), editEnd_(std::move(editEnd)),
          filter_(std::move(filter)) {}

    [[nodiscard]] std::shared_ptr<C> editor() const noexcept { return editor_; }

protected:
    WidgetPtr createEditor(TableItem item, int column) override {
        std::vector<String> values;
        String selected;
        bool canSearch = false;
        if (createCombo_) createCombo_(item, column, values, selected, canSearch);
        editor_ = createComboEditor<C>(context(), prop_, {}, values, selected,
                                       true, canSearch ? filter_ : std::function<std::vector<String>(StringView)>{});
        return editor_;
    }
    void end(WidgetPtr control) override {
        auto combo = std::dynamic_pointer_cast<C>(control);
        auto* item = editingItem();
        if (!combo || !item) return;
        if (editEnd_) editEnd_(*item, editColumn(), combo);
        else if (table() && *item < table()->rows.size() && editColumn() >= 0 &&
                 static_cast<std::size_t>(editColumn()) < table()->rows[*item].cells.size())
            (void)lib::publish_table_cell_text(
                *table(), *item, static_cast<std::size_t>(editColumn()),
                combo->edit_text);
    }

private:
    Props* prop_{};
    CreateCombo createCombo_;
    EditCallback editEnd_;
    std::function<std::vector<String>(StringView)> filter_;
    std::shared_ptr<C> editor_;
};

class TableTCEdit final : public AbstractTableEdit {
public:
    using CreateEditor = std::function<WidgetPtr(TableItem, int)>;
    using EditCallback = std::function<void(TableItem, int, WidgetPtr)>;

    TableTCEdit(EditorContext* context, TablePtr table, int editColumn,
                CreateEditor createEditor, EditCallback editEnd = {},
                std::function<bool(TableItem, int)> canEdit = {});

protected:
    WidgetPtr createEditor(TableItem item, int column) override;
    void end(WidgetPtr control) override;

private:
    CreateEditor createEditor_;
    EditCallback editEnd_;
};

class TreeEdit {
public:
    TreeEdit(EditorContext* context, TreePtr tree,
             std::function<void(lib::TreeItemId, WidgetPtr)> editEnd,
             std::function<WidgetPtr(lib::TreeItemId)> createEditor = {});
    void startEdit();
    [[nodiscard]] bool isEditing() const noexcept { return static_cast<bool>(editEnd_); }
    void cancel();
    void enter();

private:
    void startEdit(lib::TreeItemId item);
    EditorContext* context_{};
    TreePtr tree_;
    std::function<void(lib::TreeItemId, WidgetPtr)> editCallback_;
    std::function<WidgetPtr(lib::TreeItemId)> createEditor_;
    std::shared_ptr<EditEnd> editEnd_;
    lib::TreeItemId editingItem_{};
};

template <typename C>
class CardListEdit {
public:
    ItemViewPtr list;
    std::vector<C> values;
    std::function<void(const C&, WidgetPtr)> editEndCallback;
    std::function<WidgetPtr(const C&)> createEditorCallback;
    // 表示中のカード一覧の既存のgetTitleBoundsの配置へ接続する。
    std::function<lib::Rect(std::size_t)> titleBoundsProvider;

    CardListEdit(EditorContext* context, ItemViewPtr view,
                 std::function<void(const C&, WidgetPtr)> editEnd,
                 std::function<WidgetPtr(const C&)> createEditor = {},
                 std::function<lib::Rect(std::size_t)> titleBounds = {})
        : context_(context), list(std::move(view)),
          editEndCallback(std::move(editEnd)),
          createEditorCallback(std::move(createEditor)),
          titleBoundsProvider(std::move(titleBounds)) {}

    Item selectionM(float, float) const;
    Item selectionK() const;
    void startEdit(Item item);
    void layout();
    void startEdit();
    [[nodiscard]] bool isEditing() const noexcept { return editing_.has_value(); }
    void enter();
    void cancel();

private:
    EditorContext* context_{};
    std::optional<std::size_t> editing_;
    WidgetPtr editor_;
};

template <typename C>
Item CardListEdit<C>::selectionM(const float x, const float y) const {
    const auto selected = selectionK();
    const auto boundsProvider = titleBoundsProvider;
    if (!selected || !boundsProvider) return {};
    const auto bounds = boundsProvider(*selected);
    return bounds.width > 0 && bounds.height > 0 &&
           bounds.x <= x && x < bounds.x + bounds.width &&
           bounds.y <= y && y < bounds.y + bounds.height ? selected : Item{};
}

template <typename C>
Item CardListEdit<C>::selectionK() const {
    if (!list || list->selection.size() != 1U) return {};
    return list->selection.front();
}

template <typename C>
void CardListEdit<C>::startEdit(const Item item) {
    if (!item || *item >= values.size() || !createEditorCallback) return;
    if (isEditing()) enter();
    editor_ = createEditorCallback(values[*item]);
    if (editor_) editing_ = *item;
}

template <typename C>
void CardListEdit<C>::layout() {
    if (editor_) editor_->set_visible(editor_->visible());
}

template <typename C>
void CardListEdit<C>::startEdit() { startEdit(selectionK()); }

template <typename C>
void CardListEdit<C>::enter() {
    if (!editing_ || !editor_) return;
    if (editEndCallback && *editing_ < values.size())
        editEndCallback(values[*editing_], editor_);
    editing_.reset();
    editor_.reset();
}

template <typename C>
void CardListEdit<C>::cancel() {
    editing_.reset();
    editor_.reset();
}

bool hasFocus(const WidgetPtr& widget);
WindowPtr topShell(WindowPtr shell);
bool isDescendant(const WindowPtr& shell1, const WindowPtr& shell2);
bool isDescendant(const ContainerPtr& container, const WidgetPtr& widget);

template <typename B = lib::ToggleButton>
class RadioGroup {
public:
    std::vector<lib::Action> modEvent;
    std::unordered_set<std::shared_ptr<B>> _set;
    std::shared_ptr<B> _sel;

    void select(const std::shared_ptr<B>& button) {
        if (_sel == button) return;
        if (_sel) _sel->checked = false;
        _sel = button;
        if (_sel) _sel->checked = true;
    }
    [[nodiscard]] bool contains(const std::shared_ptr<B>& button) const {
        return _set.contains(button);
    }
    [[nodiscard]] const std::unordered_set<std::shared_ptr<B>>& set() const noexcept {
        return _set;
    }
    void append(const std::shared_ptr<B>& button) {
        if (!button) return;
        _set.insert(button);
        if (button->checked && !_sel) _sel = button;
        else if (_sel != button) button->checked = false;
        button->on_changed = [this, weak = std::weak_ptr<B>(button)](bool value) {
            auto current = weak.lock();
            if (value && current) {
                select(current);
                for (const auto& callback : modEvent) if (callback) callback();
            }
        };
    }
};

using ToolItemGroup = RadioGroup<lib::ToggleButton>;

struct TreeItemRef {
    TreePtr tree;
    lib::TreeItemId id{};
    [[nodiscard]] explicit operator bool() const noexcept { return tree && id != 0U; }
};
TreeItemRef createTreeItem(const TreePtr& tree, lib::TreeItemId parent,
                           StringView text, ResourceId image = 0,
                           std::size_t index = static_cast<std::size_t>(-1),
                           StringView data = {});
TreeItemRef topItem(TreeItemRef item);
int treeItemUp(TreeItemRef item);
int treeItemDown(TreeItemRef item);
int treeItemUD(TreeItemRef item, bool down);
int treeItemUD2(const TreePtr& tree, TreeItemRef item, bool down);
TreeItemRef cloneItem(const TreePtr& tree, TreeItemRef old,
                      std::size_t index = static_cast<std::size_t>(-1));
void swapTreeItem(const TreePtr& tree, std::size_t index1, std::size_t index2,
                  lib::TreeItemId parent = 0);
void treeExpandedAll(TreeItemRef item);
void treeExpandedAll(const TreePtr& tree);
void treeUnexpandedAll(TreeItemRef item);
void treeUnexpandedAll(const TreePtr& tree);
TreeItemRef anotherTreeItem(const TreePtr& tree, TreeItemRef item);
std::vector<int> toTreePath(TreeItemRef item);
TreeItemRef fromTreePath(const TreePtr& tree, const std::vector<int>& path);

WidgetPtr createToolItemC(const std::shared_ptr<lib::ToolBar>& bar,
                          WidgetPtr child, int rightSpace = 0);
std::shared_ptr<lib::Label> createLabel(const std::shared_ptr<lib::ToolBar>& bar,
                                        StringView label, int rightSpace = 0);
NumberEditPtr createSpinner(const std::shared_ptr<lib::ToolBar>& bar,
                            StringView label, int maximum, int minimum,
                            int selection, std::function<void(int)> edit = {},
                            std::function<void(int)> enter = {},
                            std::function<int(int)> cancel = {},
                            bool readOnly = false);
bool openFolder(StringView path, lib::Services services = {});
bool openFolderWithFile(StringView path, lib::Services services = {});

void drawCenterText(lib::DrawingContext& gc, const lib::FontSpec& font,
                    lib::Rect area, StringView text);
void drawCenterText(lib::DrawingContext& gc, const structs::CFont& font,
                    lib::Rect area, StringView text);
void drawCenterTextImpl(lib::DrawingContext& gc, const lib::FontSpec& font,
                        lib::Rect area, StringView text);
void hemming(lib::DrawingContext& gc, StringView text, int x, int y,
             lib::Color color, const lib::FontSpec& font = {});

ImageDataWithScale castCardImage(const Props* prop, const Skin* skin,
                                 const Summary* summ, const card::CastCard* card,
                                 bool debugMode = false,
                                 bool overrideName = false,
                                 StringView name = {},
                                 bool overrideImage = false,
                                 const lib::Array<lib::Owner<card::CardImage>>& images = {},
                                 lib::Services services = {});
ImageDataWithScale skillCardImage(const Props* prop, const Skin* skin,
                                  const Summary* summ,
                                  const card::SkillCard& card,
                                  lib::Services services = {});
ImageDataWithScale itemCardImage(const Props* prop, const Skin* skin,
                                 const Summary* summ,
                                 const card::ItemCard& card,
                                 lib::Services services = {});
ImageDataWithScale beastCardImage(const Props* prop, const Skin* skin,
                                  const Summary* summ,
                                  const card::BeastCard& card,
                                  lib::Services services = {});
ImageDataWithScale infoCardImage(const Props* prop, const Skin* skin,
                                 const Summary* summ,
                                 const card::InfoCard& card,
                                 lib::Services services = {});

class PileImage {
public:
    ImageDataWithScale base;
    std::vector<ImageDataWithScale> layers;
    explicit PileImage(ImageDataWithScale image = {}) : base(std::move(image)) {}
    void append(ImageDataWithScale image) { layers.push_back(std::move(image)); }
};
void addToPileImage(const card::CardImage& image, PileImage& pile,
                    const Props* prop, const Skin* skin, const Summary* summ,
                    const structs::CInsets& materialPadding,
                    lib::ImageScaleMode mode = lib::ImageScaleMode::Fit,
                    lib::Services services = {});

// 通常のカード名だけを、共通のD形式の文字画像の規則で合成す
// る。 生成画像に名前の描画を含めた場合はtrueを返す。
bool addCardName(ImageDataWithScale& image, const Props* prop, const Skin* skin,
                 const card::Card& card, lib::Services services = {});

// 公開のD互換テンプレートAPIで使う、解決済みカードの描画処理を一つにまとめる。
ImageDataWithScale renderCardImage(const Props* prop, const Skin* skin,
    const Summary* summ, const card::Card& base, const card::Card* resolved,
    const card::CastCard* owner, bool linked, bool detail, lib::Services services);

template <typename C>
ImageDataWithScale cardImage(const Props* prop, const Skin* skin,
                             const Summary* summ, const C& base,
                             const card::CastCard* owner = nullptr,
                             std::function<const C*(std::uint64_t)> get = {},
                             bool detail = false, bool preview = false,
                             lib::Services services = {}) {
    // 元のD関数でもpreviewは使っていない。
    (void)preview;
    const C* resolved = &base;
    bool linked = false;
    if constexpr (requires { base.linkId(); }) {
        if (get && base.linkId() != 0U) {
            linked = true;
            resolved = get(base.linkId()); // nullはDの空の仮表示を表す。
        }
    }
    return renderCardImage(prop, skin, summ, base, resolved, owner, linked,
                           detail, std::move(services));
}

std::uint8_t getRGBAverage(const lib::ImageData& image,
                           structs::CRect area);
void putHand(const std::shared_ptr<lib::Canvas>& canvas, lib::DrawingContext& gc,
             const Props* prop, bool detail, const card::CastCard* card,
             lib::Rect bounds);
lib::Rect handMarkRect(const Props* prop, bool showMark, int left, int top,
                       const card::CastCard* card, bool detail);
template <typename C>
void putEventTree(const std::shared_ptr<lib::Canvas>& canvas,
                  lib::DrawingContext& gc, const Props* prop,
                  const Summary* summ, bool showMark, const C& card,
                  lib::Rect bounds) {
    (void)canvas;
    (void)summ;
    (void)card;
    if (showMark && prop) gc.draw_rect(bounds, {64, 128, 64, 180}, 1.0F,
                                       lib::StrokeStyle::Solid);
}
template <typename C>
lib::Rect eventTreeMarkRect(const Props* prop, bool showMark, int left, int top,
                            const Summary* summ, const C& card) {
    (void)summ;
    (void)card;
    if (!showMark || !prop) return {};
    return {static_cast<float>(left), static_cast<float>(top), 16.0F, 16.0F};
}
std::vector<String> castCoupons(const EditorContext& context, bool talker,
                                StringView legacyName, bool getLose,
                                bool forValued);
// 参照構造を考慮したD形式の素材転送。モーダル確認のたびにcurrentを再確認する。
bool qGraphMaterialCopy(const EditorContext& context,
                        const usecounter::CounterOwner& counter,
                        StringView toPath, StringView fromPath, bool& copied,
                        bool toIsLegacy,
                        std::function<String(const usecounter::PathUser&)> exportedImageName = {},
                        std::function<bool()> current = {});
struct PathUser;
bool qMaterialCopy(const EditorContext& context, StringView toPath,
                   StringView fromPath, bool& copied,
                   bool toIsLegacy = false,
                   std::function<String(const PathUser&)> exportedImageName = {});

template <typename Value>
void saveColumnWidth(Props*, lib::TableColumn& column, Value& value) {
    if constexpr (requires { value = static_cast<Value>(column.width); })
        value = static_cast<Value>(column.width);
}

void intoDisplay(int& x, int& y, int width, int height,
                 lib::Rect display = {0.0F, 0.0F, 1920.0F, 1080.0F});
lib::ImageData skeletonImage(const lib::ImageData& source, bool mask = true);
std::shared_ptr<lib::GroupBox> createSuccessRateScale(
    const Props* prop, ContainerPtr parent, std::shared_ptr<lib::Slider>& successRate);

// Dの補助処理は空選択でアサーションを失敗させる。 アプリを中
// 断したり列挙の0を作ったりせず、不正入力として明示的に扱う。
template <typename E, std::size_t N>
E getRadioValue(const std::array<std::shared_ptr<lib::ToggleButton>, N>& radios) {
    for (std::size_t i = 0; i < N; ++i)
        if (radios[i] && radios[i]->checked) return static_cast<E>(i);
    throw std::invalid_argument("getRadioValue requires a selected radio button");
}
template <typename E, std::size_t N>
void putRadioValue(const std::array<std::shared_ptr<lib::ToggleButton>, N>& radios,
                   std::function<void(E)> set) {
    if (!set) return; // 既存の空コールバックの保護を維持する。
    set(getRadioValue<E>(radios));
}

// 対象部品を所有する共通GUIホストはServicesから渡す。 DはDisplay.getCurrent()
// で取得するが、C++では明示的に注入する。
void forceFocus(const WidgetPtr& widget, bool shellActivate,
                const lib::Services& services);
// DのTableItem/TreeItemは、共有ビューと固定の行番号・ツ
// リーIDで表す。 これらの版では、先に項目を選択して見える位置へ移す。
void forceFocus(const std::shared_ptr<lib::TableView>& table, std::size_t row,
                bool shellActivate, const lib::Services& services);
void forceFocus(const std::shared_ptr<lib::TreeView>& tree, lib::TreeItemId item,
                bool shellActivate, const lib::Services& services);
String writeRec(const WidgetPtr& control, StringView indent = {});
std::shared_ptr<splitpane::SplitPane> changeVHSide(
    const std::shared_ptr<splitpane::SplitPane>& sash);
void drawWallpaper(lib::DrawingContext& gc, ResourceId image, lib::Rect rect,
                   structs::WallpaperStyle style, lib::Size imageSize = {});
template <typename C>
void setComboItems(const std::shared_ptr<C>& combo, const std::vector<String>& values) {
    if constexpr (std::is_base_of_v<lib::ComboBox, C>) {
        if (!combo) return;
        std::vector<lib::ChoiceItem> items;
        items.reserve(values.size());
        for (const auto& item : values) items.push_back({item, item});
        // DのCombo.removeAll()と同じく、選択と編集文字列の両方を消す。
        (void)lib::replace_combo_items(combo, std::move(items), std::nullopt,
                                       String{});
    }
}
void initTree(EditorContext* context, const TreePtr& tree, bool eventTree,
              bool hideRootLine = true, std::function<bool()> classicStyleTree = {});
void initSpinner(const NumberEditPtr& spinner);

template <typename Window>
class CloseRemover {
public:
    std::unordered_set<std::shared_ptr<Window>>* windows{};
    std::shared_ptr<Window> window;
    CloseRemover(std::unordered_set<std::shared_ptr<Window>>& set,
                 std::shared_ptr<Window> value) : windows(&set), window(std::move(value)) {}
    void widgetDisposed() {
        if (windows && window) windows->erase(window);
    }
};

using PathString = String;
using FileNames = std::vector<String>;
[[nodiscard]] std::filesystem::path filesystemPath(StringView value);
[[nodiscard]] String pathString(const std::filesystem::path& value);
String wrapReturnCode(String value);
int ppis(int value);
int dpiMuls();

struct GridLayoutSpec {
    int columns{1};
    bool equalWidth{};
    int horizontalSpacing{};
    int verticalSpacing{};
    int marginWidth{};
    int marginHeight{};
};
GridLayoutSpec zeroGridLayout(int columns, bool equalWidth = false);
GridLayoutSpec zeroMarginGridLayout(int columns, bool equalWidth);
GridLayoutSpec normalGridLayout(int columns, bool equalWidth = false);
GridLayoutSpec windowGridLayout(int columns, bool equalWidth = false);
void setGridMinW(const WidgetPtr& control, int minimumWidth, int gridStyle = 0);
ContainerPtr centerGroup(const ContainerPtr& parent, StringView text,
                         bool fillH = true, bool fillV = false);
bool playBGMCW(const Props* prop, StringView path, std::uint32_t fadeIn,
               std::uint32_t volume, std::uint32_t loopCount, bool legacy,
               lib::Services services = {});
void playSECW(const Props* prop, StringView path, std::uint32_t fadeIn,
              std::uint32_t volume, std::uint32_t loopCount, bool legacy,
              lib::Services services = {});
int textWidth(const Props* prop, const WidgetPtr& control, StringView text,
              lib::Services services = {});
std::unordered_map<WindowPtr, lib::CursorKind> setWaitCursors(const WindowPtr& shell);
void resetCursors(const std::unordered_map<WindowPtr, lib::CursorKind>& cursors);
void resetCursors(const std::unordered_map<WindowPtr, lib::CursorKind>& cursors,
                  const WindowPtr& realizerTarget);
// Dのscope(exit) resetCursors(setWaitCursors(shell))相当の処理。
class WaitCursorScope final {
public:
    explicit WaitCursorScope(const WindowPtr& shell)
        : shell_(shell), cursors_(setWaitCursors(shell)) {}
    ~WaitCursorScope() { resetCursors(cursors_, shell_); }
    WaitCursorScope(const WaitCursorScope&) = delete;
    WaitCursorScope& operator=(const WaitCursorScope&) = delete;
private:
    WindowPtr shell_;
    std::unordered_map<WindowPtr, lib::CursorKind> cursors_;
};
lib::Color alphaColor(const lib::Color& foreground, const lib::Color& background,
                      int alpha);
structs::CRGB alphaColor(const structs::CRGB& foreground,
                         const structs::CRGB& background, int alpha);

template <typename A>
String objName(const Props* prop) {
    (void)prop;
    if constexpr (std::is_same_v<A, card::CastCard>) return "Cast";
    if constexpr (std::is_same_v<A, card::SkillCard>) return "Skill";
    if constexpr (std::is_same_v<A, card::ItemCard>) return "Item";
    if constexpr (std::is_same_v<A, card::BeastCard>) return "Beast";
    if constexpr (std::is_same_v<A, card::InfoCard>) return "Info";
    return "Object";
}
String objNameFrom(const Props* prop, const CWXPath* object);

enum class CIDKind { Area, Battle, Package, Cast, Skill, Item, Beast, Info,
                     Image, CardImage, CardImages, BGM, SE, Flag, Step,
                     Variant, Start };
String contentTextResourceID(const EditorContext& context, const Summary* summ,
                             CIDKind kind, StringView id, StringView format);
template <CIDKind Kind, typename ID>
String contentTextUseID(const EditorContext& context, const Summary* summ,
                        const ID& id, StringView format, const Content* evt = nullptr) {
    (void)context;
    (void)summ;
    (void)evt;
    String value;
    if constexpr (requires { id.toString(); }) {
        value = id.toString();
    } else if constexpr (std::is_convertible_v<ID, String>) {
        value = static_cast<String>(id);
    } else if constexpr (std::is_convertible_v<ID, StringView>) {
        value = String(static_cast<StringView>(id));
    } else if constexpr (requires { std::to_string(id); }) {
        value = std::to_string(id);
    } else {
        std::ostringstream stream;
        stream << id;
        value = stream.str();
    }
    if constexpr (Kind == CIDKind::Area || Kind == CIDKind::Battle ||
                  Kind == CIDKind::Package || Kind == CIDKind::Cast ||
                  Kind == CIDKind::Skill || Kind == CIDKind::Item ||
                  Kind == CIDKind::Beast || Kind == CIDKind::Info ||
                  Kind == CIDKind::BGM || Kind == CIDKind::SE) {
        if (context.prop) return contentTextResourceID(context, summ, Kind, value, format);
    }
    if (value.empty()) value = "(none)";
    String result(format);
    const auto position = result.find("%s");
    if (position != String::npos) result.replace(position, 2, value);
    return result;
}
String rangeName(const Props* prop, types::Range range, StringView coupon);
String transitionText(const Props* prop, types::Transition transition,
                      std::uint32_t transitionSpeed);
/// ツリー行・ツールチップ用に、副作用のないD
/// 互換のイベント文字列を返す。 summがあ
/// れば参照先の名前の解決だけに使う。対象がな
/// くても表示を失敗させず、固定IDで示す。
String contentText(const EditorContext& context, const Content* content,
                   const Summary* summ);
String msgAttrText(const Props* prop, const Content* content);
String cardSpeedAttrText(const EditorContext& context, const Content* content);
String getStepValue(const Props* prop, const flag::Step* step, int value);
String castRangesName(const Props* prop, const std::vector<types::CastRange>& ranges);
String exprStr(StringView expression);
bool CBisText(const lib::ClipboardData& clipboard);
bool CBisFile(const lib::ClipboardData& clipboard);
bool CBisText(const lib::ClipboardBackend& clipboard);
bool CBisFile(const lib::ClipboardBackend& clipboard);

class DropFiles {
public:
    DropFiles(std::shared_ptr<lib::TextEdit> text,
              std::function<String(const FileNames&)> drop,
              std::function<void(String)> dropPath = {});
    void dragEnter(lib::DragEvent& event);
    void dragOver(lib::DragEvent& event);
    void drop(lib::DragEvent& event);

private:
    std::shared_ptr<lib::TextEdit> text_;
    std::function<String(const FileNames&)> drop_;
    std::function<void(String)> dropPath_;
};
void setupDropFile(const WidgetPtr& control, const std::shared_ptr<lib::TextEdit>& text,
                   std::function<String(const FileNames&)> drop = {},
                   std::function<void(String)> dropPath = {},
                   lib::Services services = {});
String dropDir(const FileNames& files);
String dropDefault(const FileNames& files);
String selectFile(const std::shared_ptr<lib::TextEdit>& file,
                  const std::vector<String>& names, const std::vector<String>& extensions,
                  StringView fileName, StringView title, StringView path,
                  lib::Services services = {});
String selectFile(const WindowPtr& shell, const std::vector<String>& names,
                  const std::vector<String>& extensions, StringView fileName,
                  StringView title, StringView path,
                  lib::Services services = {});
std::filesystem::path pickerInitialDirectory(const Props* prop, StringView value,
                                               bool appPath);
/// ネイティブ・埋め込みEditorで共通の選択画面へ、DのDirectoryDialog
/// のタイトルと説明を両方渡す。
inline lib::FilePickerRequest makeDirectoryPickerRequest(
    StringView title, StringView message,
    std::filesystem::path initialDirectory = {}) {
    lib::FilePickerRequest request;
    request.title = String(title);
    request.description = String(message);
    request.initial_directory = std::move(initialDirectory);
    return request;
}
template <typename T>
String selectDir(const Props* prop, T& directory, StringView title,
                 StringView message, StringView path, bool appPath = true,
                 lib::Services services = {}) {
    String initial(path);
    if constexpr (requires { directory.text; }) {
        if (appPath) initial = directory.text;
    }
    auto request = makeDirectoryPickerRequest(
        title, message, pickerInitialDirectory(prop, initial, appPath));
    String chosen;
    if (services.file_picker) {
        const auto result = services.file_picker->select_directory(request);
        if (result.succeeded() && !result.selections.empty())
            if (const auto selected = result.selections.front().native_path())
                chosen = pathString(*selected);
    }
    if (chosen.empty()) return {};
    if constexpr (requires { directory.text; }) {
        directory.text = chosen;
        if constexpr (requires { directory.on_changed; }) {
            const auto notify = directory.on_changed;
            if (notify) notify(chosen);
        }
    }
    return chosen;
}
std::shared_ptr<lib::Button> createOpenButton(EditorContext* context,
                                               const ContainerPtr& parent,
                                               std::function<String()> getText,
                                               bool directory);
// DのcreateOpenButtonはgetText().length
// > 0をCommonsへ登録する。 埋め込み・ネイティブ
// の両方で同じ有効条件を使う。返された処理は所有者による
// 選択変更後の更新に使い、接続の寿命中は所有者が保持する。
lib::Action bindOpenButtonEnabledToNonEmptyText(
    const std::shared_ptr<lib::Button>& button,
    const std::shared_ptr<lib::TextEdit>& text,
    std::function<bool()> additionalEnabled = {});
lib::Action bindOpenButtonEnabledToNonEmptyText(
    const std::shared_ptr<lib::Button>& button,
    const std::shared_ptr<lib::ComboBox>& combo,
    std::function<bool()> additionalEnabled = {});
// Dの呼び出し元は長さ判定の前にgetText()を変換できる。
// 有効判定と対応するクリック操作には、同じ最終的な取得関数を使う。
lib::Action bindOpenButtonEnabledToTextGetter(
    const std::shared_ptr<lib::Button>& button,
    const std::shared_ptr<lib::ComboBox>& combo,
    std::function<String()> getText,
    std::function<bool()> additionalEnabled = {});
[[nodiscard]] String openButtonText(const Props* prop, StringView value);
[[nodiscard]] String openButtonText(StringView appPath, StringView value);
[[nodiscard]] std::optional<String> openButtonDirectory(
    const Props* prop, StringView value);
[[nodiscard]] std::optional<String> openButtonDirectory(
    StringView appPath, StringView value);
// 変換済みのDのgetText()値をOpenDirとして解決する。 対象がディレク
// トリならそれを、ファイル・未作成の末端なら存在する親を開き、その他は何もしない。
[[nodiscard]] std::optional<String> resolveOpenDirectory(StringView path);
class OpenDir {
public:
    OpenDir(EditorContext* context, std::function<String()> text);
    void widgetSelected();

private:
    EditorContext* context_{};
    std::function<String()> text_;
};
class NoIME {};
template <typename E>
bool updateChecked(E&) { return false; }
// 明示的なアダプターはCHECK以外のイベントにfalseを渡せる。 三引数の呼
// び出しは、クリック項目が指定のチェック状態となったCHECKイベントを表す。
bool updateChecked(lib::TableView& table, TableItem item, bool checked, bool checkEvent = true);
bool updateChecked(lib::TreeView& tree, lib::TreeItemId item, bool checked, bool checkEvent = true);
int getAllItemCount(const TreePtr& tree);
ResourceId fimage(const Props* prop, StringView file, const Skin* skin,
                  lib::Services services = {},
                  const std::vector<String>* plainTextExtensions = nullptr);
bool getSymbols(const EditorContext& context, const Summary* summ,
                const CWXPath* object, bool desc, String& text, ResourceId& image);
String talkDialogText(const EditorContext& context, const Content* content,
                      const SDialog* dialog);
ResourceId cardIcon(const Props* prop, const Skin* skin, const Summary* summ,
                    const ::cwxeditor::cwx::area::AbstractSpCard* card,
                    bool eventView);

void wDrawText(lib::DrawingContext& gc, StringView text, int x, int y,
               const lib::FontSpec& font = {}, lib::Color color = {});
void wDrawText(lib::DrawingContext& gc, StringView text, int x, int y,
               bool transparent, const lib::FontSpec& font = {}, lib::Color color = {});
void wDrawText(lib::DrawingContext& gc, StringView text, int x, int y, int flags,
               const lib::FontSpec& font = {}, lib::Color color = {});
lib::Point wTextExtent(lib::DrawingContext& gc, StringView text,
                       const lib::FontSpec& font = {});
bool isEnableGdipAndNotTrueTypeFont(const lib::DrawingContext& gc);
// DはGCからフォント・前景色・デバイスを読むが、共通APIでは明示
// 的に渡す。 以前の状態なしの入口を呼ぶ箇所は、ソース内にはない。
void shrinkDrawText(lib::DrawingContext& gc, std::shared_ptr<lib::GraphicsBackend> graphics,
                    const lib::FontSpec& font, lib::Color foreground,
                    StringView title, int x, int y, lib::Point size, bool smoothing,
                    structs::CRGB hemmingColor = {});
[[deprecated("Supply GraphicsBackend, FontSpec and foreground to shrinkDrawText")]]
void shrinkDrawText(lib::DrawingContext& gc, StringView title, int x, int y,
                    lib::Point size, bool smoothing,
                    structs::CRGB hemmingColor = {});
void asyncExec(lib::Services services, lib::Action callback);
void setupWeights(const std::shared_ptr<splitpane::SplitPane>& sash,
                  settings::Prop<int, false>& left,
                  settings::Prop<int, false>& right,
                  const EditorContext& context = {});
void setupToggle(const std::shared_ptr<lib::ToggleButton>& toggle,
                 settings::Prop<bool, false>& value);
void setupSpinner(const NumberEditPtr& spinner, settings::Prop<int, false>& value);

struct WindowSettings {
    int x{-1};
    int y{-1};
    int width{-1};
    int height{-1};
    bool maximized{};
};
lib::Rect setupWindow(const WindowPtr& window, WindowSettings& settings,
                      const EditorContext& context = {});
lib::Rect setupWindow(const WindowPtr& window, WindowSettings& settings,
                      std::function<void(bool)>* clearSetup,
                      bool setLocation, bool setSize,
                      const EditorContext& context = {});

inline constexpr std::uint32_t DEFAULT_CHARSET = 0x1U;
inline constexpr std::uint32_t OUT_DEFAULT_PRECIS = 0x0U;
inline constexpr std::uint32_t CLIP_DEFAULT_PRECIS = 0x0U;
inline constexpr std::uint32_t DEFAULT_QUALITY = 0x0U;
inline constexpr std::uint32_t ANTIALIASED_QUALITY = 0x4U;
inline constexpr std::uint32_t DEFAULT_PITCH = 0x0U;
inline constexpr std::uint32_t FIXED_PITCH = 0x1U;
inline constexpr std::uint32_t VARIABLE_PITCH = 0x2U;
inline constexpr std::uint32_t FF_DONTCARE = 0x0U;
inline constexpr std::uint32_t FF_ROMAN = 0x10U;
inline constexpr std::uint32_t FF_MODERN = 0x30U;
lib::FontSpec CreateFontW(int height, int width, int escapement, int orientation,
                          int weight, std::uint32_t italic, std::uint32_t underline,
                          std::uint32_t strikeout, std::uint32_t charset,
                          std::uint32_t outputPrecision, std::uint32_t clipPrecision,
                          std::uint32_t quality, std::uint32_t pitchAndFamily,
                          StringView face);
lib::FontSpec createFontFromPixels(const structs::CFont& font,
                                   bool antialias = true);
lib::FontSpec createFontFromPixels(StringView face, int pixels,
                                   bool bold = false, bool italic = false,
                                   bool underline = false, bool strike = false,
                                   bool antialias = true);
// dprops::dwtData(CFont)と同じ変換。 DがSWT
// のFontDataを作り、指定サイズをポイントで扱う経路に使う。
lib::FontSpec fontSpecFromDwtData(const structs::CFont& font,
                                 bool antialias = true);
lib::FontSpec fontSpecFromDwtData(StringView face, int points,
                                 bool antialias = true);
bool isEnterKey(int keyCode);
struct Warning {
    lib::Rect rect;
    std::vector<String> warnings;
};
ImageDataWithScale warningImage(const Props* prop, int width = -1,
                                lib::Services services = {});
void procShowingTreeItem(const TreePtr& tree,
                         std::function<bool(lib::TreeItemId)> proc);
String commentText(const CWXPath* object, bool eventView);

}  // namespace cwxeditor::cwx::editor::gui::dwt::dutils
