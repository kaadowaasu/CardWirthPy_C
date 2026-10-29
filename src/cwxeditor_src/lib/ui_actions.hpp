#pragma once

#include "components.hpp"
#include "ui_layout.hpp"
#include "text_input.hpp"
#include "ui_tooltip_timing.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cwxeditor::lib {

// ウィジェットの意味上の変更処理をここに置く。表示側はネイティブ/SDL
// の入力をこれらの操作に変換し、結果の共通モデルの状態を画面へ反映する。
void normalize_text_selection(TextEdit& edit) noexcept;
[[nodiscard]] bool set_text_selection(TextEdit& edit,
                                      std::size_t start,
                                      std::size_t length) noexcept;
[[nodiscard]] std::size_t previous_utf8_boundary(
    std::string_view value, std::size_t position) noexcept;
[[nodiscard]] std::size_t next_utf8_boundary(
    std::string_view value, std::size_t position) noexcept;
[[nodiscard]] std::size_t clamp_utf8_boundary(
    std::string_view value, std::size_t position) noexcept;
[[nodiscard]] std::size_t utf8_codepoint_count(
    std::string_view value) noexcept;

struct TextEditComposition {
    TextComposition value;
    std::string document;
    TextSelection replaced;
    std::uint64_t focus_revision{};
};

struct TextEditInteractionState {
    float scroll_x{};
    float scroll_y{};
    std::size_t anchor{};
    std::size_t caret{};
    float preferred_x{-1.0F};
    bool initialized{};
    std::size_t preferred_line_start{};
    std::size_t preferred_line_end{};
    std::optional<TextEditComposition> composition;
    std::uint64_t composition_reset_serial{};
    bool cancel_composition() noexcept {
        if (!composition) return false;
        composition.reset(); ++composition_reset_serial; return true;
    }
};

void synchronize_text_edit_state(TextEdit& edit,
                                 TextEditInteractionState& state) noexcept;
void commit_text_edit_state(TextEdit& edit,
                            TextEditInteractionState& state) noexcept;
[[nodiscard]] bool set_text_caret(TextEdit& edit,
                                  TextEditInteractionState& state,
                                  std::size_t position,
                                  bool keep_anchor) noexcept;
[[nodiscard]] bool select_all_text(TextEdit& edit,
                                   TextEditInteractionState& state) noexcept;
[[nodiscard]] bool replace_text_selection(TextEdit& edit,
                                           std::string_view replacement);
enum class TextValueOrigin {
    UserInput,
    Programmatic,
};
// ネイティブの編集コントロールから受け取った完全な値を、SDL
// のテキスト状態機械と同じ検証・コールバックの遷移で確定する。
[[nodiscard]] bool set_text_value(TextEdit& edit, std::string value,
                                  TextValueOrigin origin = TextValueOrigin::UserInput);
[[nodiscard]] bool replace_text_selection(TextEdit& edit,
                                          TextEditInteractionState& state,
                                          std::string_view replacement);
[[nodiscard]] bool erase_text_backward(TextEdit& edit,
                                       TextEditInteractionState& state);
[[nodiscard]] bool erase_text_forward(TextEdit& edit,
                                      TextEditInteractionState& state);

enum class TextCaretMove {
    CharacterBackward,
    CharacterForward,
    LineStart,
    LineEnd,
    DocumentStart,
    DocumentEnd,
};

// フォントに依存しないキャレットの移動規則を適用する。折り返しは表
// 示側の文字計測に依存するため、現在の表示行の境界は表示側が渡す
// 。選択と選択解除時の位置の規則はプラットフォーム非依存とする。
[[nodiscard]] bool move_text_caret(TextEdit& edit,
                                   TextEditInteractionState& state,
                                   TextCaretMove movement,
                                   std::size_t line_start,
                                   std::size_t line_end,
                                   bool extend = false) noexcept;
[[nodiscard]] bool set_toggle_checked(ToggleButton& toggle, bool checked);
[[nodiscard]] bool select_radio_button(
    RadioButton& selected, const std::vector<std::shared_ptr<RadioButton>>& group);
// ネイティブコントロールがまだない遅延生成のタブページも含め、共通モ
// デル全体からラジオグループを列挙する。表示側は生成済みのハンドルか
// ら別のグループを作らない。子ウィンドウは別範囲とし、名前のないグ
// ループはDWTのラジオ用複合コントロールと同様に直近の親を使う。
[[nodiscard]] std::vector<std::shared_ptr<RadioButton>> radio_button_group(
    const WidgetPtr& root, const RadioButton& selected);
enum class NumberValueOrigin { Committed, Preview };
[[nodiscard]] std::optional<double> validated_number_value(const NumberEdit& number,double value) noexcept;
[[nodiscard]] bool set_number_value(NumberEdit& number, double value,
    NumberValueOrigin origin = NumberValueOrigin::Committed);
[[nodiscard]] bool step_number_value(NumberEdit& number, double steps);
[[nodiscard]] bool set_slider_value(Slider& slider, double value);
[[nodiscard]] bool set_slider_value_from_point(Slider& slider, Point point);
[[nodiscard]] bool set_color_value(ColorWell& well, Color value);
// notify=falseは処理側から表示側への同期用。ComboBoxモデ
// ルの完全な状態は公開するが、アプリケーションのコールバックへは再入しない。
[[nodiscard]] bool select_combo_item(ComboBox& combo,
                                     std::optional<std::size_t> index,
                                     bool notify = true);
[[nodiscard]] bool set_combo_edit_text(ComboBox& combo, std::string text,
    std::optional<std::pair<std::size_t, std::size_t>> selection = std::nullopt);
[[nodiscard]] std::optional<std::size_t> first_enabled_combo_item(
    const ComboBox& combo, std::size_t preferred = 0U) noexcept;
[[nodiscard]] std::optional<std::size_t> adjacent_combo_item(
    const ComboBox& combo, std::size_t start, int direction) noexcept;

enum class ComboPopupClick { Outside, Consumed, Anchor };

class ComboPopupStateMachine {
public:
    [[nodiscard]] std::uint64_t revision() const noexcept { return generation_; }
    [[nodiscard]] bool open(const std::shared_ptr<ComboBox>& combo,
                            std::size_t visible_rows);
    void close() noexcept;
    [[nodiscard]] std::shared_ptr<ComboBox> combo() const noexcept;
    [[nodiscard]] bool is_open_for(
        const std::shared_ptr<ComboBox>& combo) const noexcept;
    [[nodiscard]] bool set_highlight(std::size_t index);
    [[nodiscard]] bool move_highlight(int direction);
    [[nodiscard]] bool commit();
    [[nodiscard]] bool handle_key(const std::shared_ptr<ComboBox>& combo,
                                  Key key, bool alt,
                                  std::size_t visible_rows, bool command = false);
    void ensure_visible(std::size_t visible_rows) noexcept;
    [[nodiscard]] ComboPopupLayout layout(Rect viewport);
    [[nodiscard]] ComboPopupClick click(const ComboPopupLayout& layout, Point point);
    [[nodiscard]] bool hover(const ComboPopupLayout& layout, Point point);
    [[nodiscard]] bool wheel(const ComboPopupLayout& layout, Point point, float delta);
    void scroll_rows(int rows, std::size_t visible_rows) noexcept;
    // 変位は配置後の画素単位。端数は一つの接触に属する。
    [[nodiscard]] bool pan(const ComboPopupLayout& layout, float displacement,
                           float& remainder);
    // スクロールバーのつまみのドラッグ。つまみを押すとclick()から開始する。
    [[nodiscard]] bool dragging_thumb() const noexcept { return thumb_grab_.has_value(); }
    [[nodiscard]] bool drag_thumb(const ComboPopupLayout& layout, Point point);
    void end_thumb_drag() noexcept { thumb_grab_.reset(); }

    [[nodiscard]] std::size_t highlighted() const noexcept {
        return highlighted_;
    }
    [[nodiscard]] std::size_t top() const noexcept { return top_; }
    void set_top(std::size_t top, std::size_t visible_rows) noexcept;

private:
    std::weak_ptr<ComboBox> combo_;
    // ネイティブのポップアップは、コントロールモデルのある時点の不変の
    // 表示を表す。プログラムによる項目や選択の更新ではその表示を失効
    // させ、古い行番号で新しく公開した別の項目を選ばないようにする。
    std::vector<ChoiceItem> items_snapshot_;
    std::optional<std::size_t> selected_snapshot_;
    std::size_t highlighted_{};
    std::size_t top_{};
    std::size_t visible_rows_{};
    std::size_t requested_visible_rows_{9U};
    std::uint64_t generation_{};
    // つまみのドラッグ中に使う、つまみ上端からポインターまでの距離。
    std::optional<float> thumb_grab_;
};

// ポップアップの階層、選択、コマンド配送は共通の表示処理で管理する
// 。バックエンドは描画面と変換済みの入力イベントだけを担当する。
class MenuPopupStateMachine {
public:
    [[nodiscard]] std::uint64_t revision() const noexcept { return generation_; }
    struct Level {
        std::shared_ptr<Menu> menu;
        std::optional<std::size_t> selected;
        std::size_t top{};
        bool reveal_selection{};
    };
    void open(const std::shared_ptr<Menu>& menu, const WidgetPtr& owner, Point anchor);
    [[nodiscard]] bool open_menu_bar_item(const std::shared_ptr<Menu>& menu,
                                           std::size_t index, const WidgetPtr& owner,
                                           CommandGesture gesture = {});
    void close() noexcept;
    [[nodiscard]] bool is_open() const noexcept { return !levels_.empty(); }
    [[nodiscard]] bool is_active() const noexcept { return is_open() || !menu_bar_.expired(); }
    [[nodiscard]] std::optional<std::size_t> selected_menu_bar_item(const Menu& menu) const noexcept;
    [[nodiscard]] WidgetPtr owner() const noexcept { return owner_.lock(); }
    [[nodiscard]] const std::vector<Level>& levels() const noexcept { return levels_; }
    [[nodiscard]] std::vector<MenuPopupLayout> layout(Rect viewport);
    [[nodiscard]] bool contains(Point point, Rect viewport);
    [[nodiscard]] bool click(Point point, Rect viewport, CommandGesture gesture = {});
    [[nodiscard]] bool hover(Point point, Rect viewport);
    [[nodiscard]] bool wheel(Point point, float delta, Rect viewport);
    [[nodiscard]] bool pan(Point anchor, float displacement, Rect viewport,
                           float& remainder);
    [[nodiscard]] bool handle_key(Key key, Modifiers modifiers = Modifiers::None,
                                  std::optional<bool> physical_control = std::nullopt);
    // 変換済みのキーの押下・解放を、フォーカス中のコントロールより先に処理
    // する。メニューモードは直前のフォーカスを保持し、閉じると編集に戻る。
    [[nodiscard]] bool handle_key_event(const WidgetPtr& root, const KeyEvent& event, bool pressed);
    void cancel_for(const WidgetPtr& root);
private:
    [[nodiscard]] bool focus_menu_bar(const std::shared_ptr<Menu>& menu);
    [[nodiscard]] bool activate(std::size_t level, std::size_t index, bool open_only = false,
                                CommandGesture gesture = {});
    [[nodiscard]] bool switch_menu_bar(int direction);
    std::vector<Level> levels_;
    std::weak_ptr<Widget> owner_;
    std::weak_ptr<Menu> menu_bar_;
    std::size_t menu_bar_index_{};
    Point anchor_{};
    std::weak_ptr<Widget> alt_root_;
    std::optional<Key> consumed_release_;
    std::weak_ptr<Widget> consumed_root_;
    std::uint64_t generation_{};
};
// メニュー配送と編集欄で共有する、D互換のショートカット文法。先
// 頭の修飾指定は空白を除去せずに認識し、未知の接頭辞は無視する。
[[nodiscard]] KeyEvent parse_menu_shortcut(std::string_view shortcut);
[[nodiscard]] bool menu_shortcut_matches(std::string_view shortcut, Key key, Modifiers modifiers,
    std::string_view printable = {});
[[nodiscard]] bool dispatch_menu_shortcut(const std::shared_ptr<Menu>& menu, Key key, Modifiers modifiers,
    std::string_view printable = {}, std::optional<bool> physical_control = std::nullopt);
[[nodiscard]] bool dispatch_menu_shortcuts(const WidgetPtr& root, Key key, Modifiers modifiers,
    std::string_view printable = {}, std::optional<bool> physical_control = std::nullopt);
// notify=falseは、処理側の状態からページを復元・同期する場合に使
// う。ユーザー入力では既定値を使い、選択コールバックをちょうど一度通知する。
[[nodiscard]] bool select_tab_page(TabView& tabs,
                                   std::optional<std::size_t> index,
                                   bool notify = true);
enum class TabNavigation { Previous, Next, First, Last };
[[nodiscard]] bool navigate_tab_page(TabView& tabs, TabNavigation navigation,
                                     bool wrap = true);
[[nodiscard]] bool set_expand_state(ExpandBar& expand, std::size_t index,
                                     bool expanded);
[[nodiscard]] bool expand_item_is_open(const ExpandBar& expand,
                                       std::size_t index) noexcept;
[[nodiscard]] bool expand_child_is_open(const ExpandBar& expand,
                                        const Widget* child) noexcept;
// rootから可視のコンテナー、選択中のタブページ、展開中
// の区画を通ってtargetに到達できる場合にtrue。
[[nodiscard]] bool widget_is_visible_in(const WidgetPtr& root,
                                        const Widget* target);
[[nodiscard]] bool move_cool_bar_item(CoolBar& cool_bar, std::size_t from,
                                      std::size_t to);

struct FrameDragHit {
    std::shared_ptr<Container> owner;
    std::size_t index{};
    CursorKind cursor{CursorKind::Default};
};
[[nodiscard]] std::optional<FrameDragHit> hit_test_frame_drag(
    const WidgetPtr& root, Point point);
// Split/CoolBarのジェスチャー状態はネイティブの入力元から独立させる。Split
// の移動は現在の比率をプレビューし、解放またはフォーカス喪失で一度だけ確定する。モデルの
// 置換、ページの非アクティブ化、外部からのサイズ変更、倍率変更でキャプチャを終了する。
class FrameDragStateMachine {
public:
    bool begin(const WidgetPtr& root, Point point);
    bool move(Point point);
    bool finish(Point point);
    bool finish() { return finish(last_point_); }
    void reset();
    [[nodiscard]] std::shared_ptr<Container> owner() const { return owner_.lock(); }
    [[nodiscard]] CursorKind cursor() const { return cursor_; }
private:
    [[nodiscard]] bool valid() const;
    std::weak_ptr<Widget> root_;
    std::weak_ptr<Container> owner_;
    std::vector<std::pair<std::weak_ptr<Widget>, bool>> children_;
    std::vector<float> weights_;
    Rect bounds_{};
    Point last_point_{};
    float scale_{1}, grab_offset_{};
    std::size_t index_{};
    Orientation orientation_{Orientation::Horizontal};
    CursorKind cursor_{CursorKind::Default};
    bool changed_{};
    std::uint64_t generation_{};
};

// メニューとツールバーのモデルが意味上のコマンドを所有する。表示側は操作された
// 項目を識別してここへ委ね、無効項目・区切り・ドロップダウンの扱いやaction
// とcommandの優先順位がプラットフォームごとに異ならないようにする。
[[nodiscard]] bool activate_menu_item(
    const MenuItem& item,
    const std::function<void(std::string_view)>& on_command = {},
    const std::function<void(std::string_view, CommandGesture)>& on_command_with_gesture = {},
    CommandGesture gesture = {});
[[nodiscard]] bool activate_tool_item(ToolBar& toolbar, std::size_t index,
                                      CommandGesture gesture = {});
// 対象として指定されたバーで、矢印キーの移動とDWT系のSpaceによる実
// 行を処理する。Enterはネイティブまたは既定のキー処理経路へ委ねる。
[[nodiscard]] bool handle_tool_bar_key(ToolBar& toolbar, Key key,
                                        Modifiers modifiers);
[[nodiscard]] bool activate_button(Button& button);
// ボタン系コントロールの意味上の実行操作を適用する。ラジオグルー
// プはネイティブハンドルではなく、共通モデル全体から解決する。
[[nodiscard]] bool activate_button_control(
    const WidgetPtr& root, const std::shared_ptr<Button>& button);
// 単一行の編集欄で、Enterの意味上の操作を実行する
// 。複数行ではfalseを返し、周囲のダイアログを誤っ
// て確定せずに、各表示側で改行を挿入できるようにする。
[[nodiscard]] bool activate_text_enter(TextEdit& edit);
[[nodiscard]] bool activate_link(Link& link);
[[nodiscard]] bool activate_list_item(ListView& list, std::size_t index,
                                      Modifiers modifiers = Modifiers::None);
[[nodiscard]] bool activate_table_cell(TableView& table, std::size_t row,
                                       std::size_t column,
                                       Modifiers modifiers = Modifiers::None);
[[nodiscard]] bool request_table_sort(TableView& table, std::size_t column);
[[nodiscard]] bool activate_tree_item(TreeView& tree, TreeItemId id,
                                      Modifiers modifiers = Modifiers::None);
[[nodiscard]] bool request_close_tab_page(TabView& tabs, std::size_t index);
// falseは計測結果が失効したことを表す。古い表示領域は公開していない。
bool update_tab_header_viewport(TabView& tabs);
// 共通の配置準備で使う、副作用のない対応処理。計測入力をすべて所
// 有し、新しい表示領域を使用中のTabViewには公開しない。
[[nodiscard]] TabHeaderViewport resolved_tab_header_viewport(
    const TabLayoutSnapshot& snapshot);
[[nodiscard]] bool scroll_tab_headers(TabView& tabs, int direction);
[[nodiscard]] bool activate_tab_header(TabView& tabs, Point point);
// ヘッダー上の点にあるページを返す。選択や表示領域を変えず
// 、コールバックも呼ばない。コンテキストメニューとネイティ
// ブアダプターは、主操作と同じ意味上のヒット判定を使う。
[[nodiscard]] std::optional<std::size_t> hit_test_tab_page(
    const TabView& tabs, Point point);
// 点がページの閉じる記号の範囲内にある場合だけ、そのページを返す。ド
// ッキングホストと通常の操作で、共通のタブのヒット判定規則を使う。
[[nodiscard]] std::optional<std::size_t> hit_test_tab_close_page(
    const TabView& tabs, Point point);
[[nodiscard]] bool activate_expand_header(ExpandBar& expand, Point point);
[[nodiscard]] Rect expand_item_header_bounds(const Widget& child) noexcept;
// 入力範囲からは、無効・非表示の祖先、非アクティブなページ、折りたたまれた区画、所
// 有する子ウィンドウを除く。フォーカス範囲と異なり、受動的なコントロールも含める。
[[nodiscard]] bool widget_in_input_scope(const WidgetPtr& root, const WidgetPtr& widget);
// 同じアクティブページ・所有ウィンドウの規則を使った、ルートからコントロールまでの祖先列。
[[nodiscard]] std::vector<WidgetPtr> input_scope_path(const WidgetPtr& root, const WidgetPtr& widget);
// 共通の項目のヒット情報を設定し、安定した状態のコピーをコールバックへ渡す。ハン
// ドラーがイベントを消費したかを返し、trueなら表示側は後から既定のクリック
// 処理を実行しない。ツリーの情報には行番号ではなくTreeItemIdを使う。
[[nodiscard]] bool dispatch_widget_pointer(const WidgetPtr& target, PointerEvent& event);
// ホバーはモデルの状態であり、SDLやネイティブハンドルの属性ではない。Leave/Enter
// コールバックは入力範囲を閉じたり、移動先のコントロールを置換したり、この状態
// 機械へ再入したりできる。通知前に遷移を公開し、より新しい要求を上書きしない。
class HoverStateMachine {
public:
    using TransitionObserver = std::function<bool(
        const PointerEvent&, const WidgetPtr& next)>;
    [[nodiscard]] std::uint64_t revision() const noexcept { return generation_; }
    [[nodiscard]] WidgetPtr hovered() const;
    [[nodiscard]] Point position() const noexcept { return position_; }
    [[nodiscard]] TooltipStateMachine& tooltip() noexcept { return tooltip_; }
    void update(const WidgetPtr& scope, const WidgetPtr& next, PointerEvent event,
                const TransitionObserver& before_transition = {});
    void clear_for(const WidgetPtr& scope, PointerEvent event);
    // 終了処理で部分木が隠れた後でも、ウィンドウの破棄時には最後のEnterに対応す
    // るLeaveを送る。通常のポインター遷移では表示状態のフィルターを維持する。
    void clear_for_retirement(const WidgetPtr& scope, PointerEvent event);
    // 遅れて届いたペンの接近イベントで、新しいマウスのホバー状態を消
    // してはならない。通常の範囲解除は全デバイスに適用し、ウィンドウ
    // 全体の破棄では非表示後に専用の経路で対応するLeaveを送る。
    void clear_pointer(const WidgetPtr& scope, PointerEvent event);
private:
    void update_impl(const WidgetPtr& scope, const WidgetPtr& next, PointerEvent event,
                     const TransitionObserver& before_transition, bool balance_previous);
    std::weak_ptr<Widget> hovered_;
    std::weak_ptr<Widget> scope_;
    Point position_{};
    std::uint64_t generation_{};
    PointerType pointer_type_{PointerType::Mouse};
    std::uint64_t pointer_id_{};
    TooltipStateMachine tooltip_;
};
// コンテキストメニュー用の選択では、チェックボックスの切り替え、枝の展開、ボタン
// 実行は行わない。既存の複数選択の中を右クリックした場合は、その選択を保持する。
[[nodiscard]] bool select_context_item(const WidgetPtr& view, Point point);

struct SelectionInput {
    bool toggle{};
    bool extend{};
};

enum class SelectionNavigation {
    Previous,
    Next,
    First,
    Last,
    PagePrevious,
    PageNext,
};

struct ItemViewKeyResult {
    bool handled{};
    bool layout_changed{};
};
// List/Table/Treeの主クリック操作。表示側は入力座標と修
// 飾キーを変換し、ヒット判定、チェックボックス、選択、並べ替え、スク
// ロールバーのページ送りクリックはここで共通モデルに対して処理する。
[[nodiscard]] bool handle_item_view_click(const WidgetPtr& view, Point point,
                                           SelectionInput input = {});
// ポインター下の共通モデルのインデックス。Treeは表示行の番号ではなくノードの
// インデックスを返す。ヘッダー、スクロールバー、表示領域外の点は対象なしとする。
[[nodiscard]] std::optional<std::size_t> hit_test_item_view(
    const WidgetPtr& view, Point point);
class InlineEditStateMachine;
// TreeEditのすべての開始経路で共有する、ホスト側の受付判定フック。
[[nodiscard]] bool tree_inline_edit_admitted(const TreeView& tree,
                                             const TreeNode& node);
// 実行と省略可能なその場編集を、一つの意味上のジェスチャーとして扱う。コールバックがモ
// デルを再構築・終了する場合があるため、古いインデックスで置換後の対象を編集しない。
[[nodiscard]] bool handle_item_view_double_click(const WidgetPtr& view,
    Point point, InlineEditStateMachine* edit = nullptr,
    Modifiers modifiers = Modifiers::None);
// 単クリックでの編集は、すでに公開済みの行選択に
// 従う。セルの実行コールバックは再度呼ばない。
[[nodiscard]] bool handle_item_view_single_click(const WidgetPtr& view,
    Point point, InlineEditStateMachine* edit = nullptr,
    Modifiers modifiers = Modifiers::None, SelectionInput input = {});
// DのAbstractTableEditは選択行の先頭を対象とする。AbstractTreeEdit
// は選択ノードがちょうど一つであることを要求し、Ctrl操作の移動カーソル
// は使わない。共通の座標計算で選択行を見える位置へ移し、編集欄を配置する。
[[nodiscard]] bool begin_selected_item_edit(InlineEditStateMachine& edit,
    const WidgetPtr& view, float row_height, std::size_t column = 0);
// ネイティブのツールキットに動作を依存させないコントロールの
// 、キーによる実行と値変更を処理する。表示側はキーイベントを
// 変換し、この関数が処理済みと報告した場合に再描画するだけ。
[[nodiscard]] bool handle_control_key(const WidgetPtr& root,
                                      const WidgetPtr& focused,
                                      Key key);

// タブと線形の項目表示にキー操作の規則を適用する。表示側はプラ
// ットフォームのキーを変換し、表示中のデータ行数を渡すだけ。
[[nodiscard]] ItemViewKeyResult handle_item_view_key(
    const WidgetPtr& focused, Key key, std::size_t visible_count,
    Modifiers modifiers = Modifiers::None);
[[nodiscard]] std::size_t item_view_visible_row_count(const WidgetPtr& view, float row_height);
// Ctrl+矢印では未選択の行へキーボードフォーカスを移せる。フ
// ォーカス枠は選択色と独立し、ヒット判定と同じ表示領域を使う。
[[nodiscard]] std::optional<Rect> item_view_cursor_geometry(const WidgetPtr& view, float row_height);

// 項目の集合全体を一つの共通トランザクションで公開する。呼
// び出し側は処理上の値と付随データを準備してから呼ぶ。アプ
// リケーションのコールバックは通知しない。current()
// の実行中は共有の所有元を保持し、再入、新しい要求、所有
// 元の規則による拒否があれば、表示中の集合を維持する。
class PreparedItemModelUpdate {
public:
    PreparedItemModelUpdate() = default;
    PreparedItemModelUpdate(PreparedItemModelUpdate&&) noexcept = default;
    PreparedItemModelUpdate& operator=(PreparedItemModelUpdate&&) noexcept = default;
    PreparedItemModelUpdate(const PreparedItemModelUpdate&) = delete;
    PreparedItemModelUpdate& operator=(const PreparedItemModelUpdate&) = delete;
    [[nodiscard]] bool current() const;
    [[nodiscard]] bool commit();
private:
    struct State;
    std::shared_ptr<State> state_;
    friend PreparedItemModelUpdate prepare_combo_items(
        const std::shared_ptr<ComboBox>&, std::vector<ChoiceItem>,
        std::optional<std::size_t>, std::optional<std::string>, std::function<bool()>);
    friend PreparedItemModelUpdate prepare_list_items(
        const std::shared_ptr<ListView>&, std::vector<ListItem>,
        std::vector<std::size_t>, std::optional<std::size_t>, std::function<bool()>);
    friend PreparedItemModelUpdate prepare_tab_pages(
        const std::shared_ptr<TabView>&, std::vector<TabPage>,
        std::optional<std::size_t>, std::function<bool()>);
    friend PreparedItemModelUpdate prepare_menu_items(
        const std::shared_ptr<Menu>&, std::vector<MenuItem>, std::function<bool()>);
    friend PreparedItemModelUpdate prepare_tool_bar_items(
        const std::shared_ptr<ToolBar>&, std::vector<ToolItem>, std::function<bool()>);
};
[[nodiscard]] PreparedItemModelUpdate prepare_combo_items(
    const std::shared_ptr<ComboBox>& combo, std::vector<ChoiceItem> items,
    std::optional<std::size_t> selected,
    std::optional<std::string> edit_text = {},
    std::function<bool()> current = {});
[[nodiscard]] PreparedItemModelUpdate prepare_list_items(
    const std::shared_ptr<ListView>& list, std::vector<ListItem> items,
    std::vector<std::size_t> selection,
    std::optional<std::size_t> reveal = {},
    std::function<bool()> current = {});
[[nodiscard]] PreparedItemModelUpdate prepare_tab_pages(
    const std::shared_ptr<TabView>& tabs, std::vector<TabPage> pages,
    std::optional<std::size_t> selected,
    std::function<bool()> current = {});
[[nodiscard]] PreparedItemModelUpdate prepare_menu_items(
    const std::shared_ptr<Menu>& menu, std::vector<MenuItem> items,
    std::function<bool()> current = {});
[[nodiscard]] PreparedItemModelUpdate prepare_tool_bar_items(
    const std::shared_ptr<ToolBar>& toolbar, std::vector<ToolItem> items,
    std::function<bool()> current = {});
[[nodiscard]] bool replace_combo_items(
    const std::shared_ptr<ComboBox>& combo, std::vector<ChoiceItem> items,
    std::optional<std::size_t> selected,
    std::optional<std::string> edit_text = {},
    std::function<bool()> current = {});
[[nodiscard]] bool replace_list_items(
    const std::shared_ptr<ListView>& list, std::vector<ListItem> items,
    std::vector<std::size_t> selection,
    std::optional<std::size_t> reveal = {},
    std::function<bool()> current = {});
[[nodiscard]] bool replace_tab_pages(
    const std::shared_ptr<TabView>& tabs, std::vector<TabPage> pages,
    std::optional<std::size_t> selected,
    std::function<bool()> current = {});
[[nodiscard]] bool replace_menu_items(
    const std::shared_ptr<Menu>& menu, std::vector<MenuItem> items,
    std::function<bool()> current = {});
[[nodiscard]] bool replace_tool_bar_items(
    const std::shared_ptr<ToolBar>& toolbar, std::vector<ToolItem> items,
    std::function<bool()> current = {});

// 項目選択の規則はネイティブとSDLの表示側で共有する。アンカーは
// モデル順のインデックスであり、プラットフォームのハンドルではない
// 。TreeViewは表示中のモデル順も提供する。select_list_rows/select_table_rows/select_tree_items
// は通知前に選択とフォーカスを公開する。コー
// ルバックと引数は所有された状態のコピーであ
// り、通知中の再入や破棄でも引数は変わらな
// い。通知の例外は選択の確定後に伝播する。
[[nodiscard]] bool update_index_selection(
    std::vector<std::size_t>& selection, std::size_t index,
    std::size_t item_count, bool multi_select, SelectionInput input = {});
[[nodiscard]] bool update_index_selection(
    std::vector<std::size_t>& selection,
    std::optional<std::size_t>& anchor, std::size_t index,
    std::size_t item_count, bool multi_select, SelectionInput input = {});
[[nodiscard]] bool update_list_selection(
    ListView& list, std::size_t index, SelectionInput input = {});
[[nodiscard]] bool navigate_list_selection(
    ListView& list, SelectionNavigation navigation,
    std::size_t visible_count, bool extend = false, bool preserve_selection = false);
[[nodiscard]] bool select_list_rows(
    ListView& list, std::vector<std::size_t> selection,
    bool preserve_focus = false, bool notify = true);
[[nodiscard]] bool set_list_item_checked(ListView& list, std::size_t index,
                                         bool checked);
[[nodiscard]] bool select_table_rows(
    TableView& table, std::vector<std::size_t> selection,
    bool preserve_focus = false, bool notify = true);
[[nodiscard]] bool update_table_selection(
    TableView& table, std::size_t index, SelectionInput input = {});
[[nodiscard]] bool navigate_table_selection(
    TableView& table, SelectionNavigation navigation,
    std::size_t visible_count, bool extend = false, bool preserve_selection = false);
// 描画・ヒット判定と同じ表示領域とヘッダーの寸法を使い、モデルの行を
// 見える位置へ移す。選択、通知、再描画、インライン編集状態の失効は
// 行わない。無効な行は無視し、未配置の表示領域は一行分として扱う。
[[nodiscard]] bool reveal_table_row(TableView& table, std::size_t row,
                                     float row_height = 0);
[[nodiscard]] bool set_table_row_checked(TableView& table, std::size_t index,
                                         bool checked);
// 処理側から指定されたセル値を、ユーザー編集のコールバックを呼ばずに
// 公開する。成功すると準備済みの行処理を失効させるが、行の同一性、
// 選択、表示領域は維持する。無効表示のセルもモデルから更新できる。
[[nodiscard]] bool publish_table_cell_text(TableView& table, std::size_t row,
                                           std::size_t column, std::string text);
// 表示文字列と処理上の値を、一つのモデルのリビジョンとして公開する。publish_table_cell_text
// と同様に通知は行わない。表示ラベルが値と異なる場合や、値と
// 常に対で保持する必要があるアプリケーション側の更新に使う。
[[nodiscard]] bool publish_table_cell_text_value(TableView& table,
                                                 std::size_t row,
                                                 std::size_t column,
                                                 std::string text,
                                                 std::string value);
[[nodiscard]] bool edit_table_cell(TableView& table, std::size_t row,
                                   std::size_t column, std::string text);
// new_to_oldはモデルの全行の順列でなければならない。セル全体を並
// べ替え、選択・アンカー・カーソルを再対応付けするが、アプリケーションのコ
// ールバックは通知しない。不正な順列では表を変えずにfalseを返し、メモ
// リ確保の失敗時も変更しない。スクロール量は表示領域の状態として保持する。
[[nodiscard]] bool reorder_table_rows(TableView& table,
                                      const std::vector<std::size_t>& new_to_old);
// 準備済みの置換内容と、呼び出し側が解決した選択を公開する。アプリケーシ
// ョンへの通知は行わず、呼び出し側は先に自身の状態を更新する。ラベルが同
// じでも古いインライン編集のインデックスを失効させ、選択を正規化し、新し
// い件数に合わせて既存の表示領域を制限してから公開する。行や座標の構築は
// 例外を送出し得るが、公開までは古い表を維持する。座標計算は所有された
// 投影データを使う。falseは新しい要求による失効、計測中の状態変更
// 、またはcurrent()による所有元の拒否を表す。呼び出し側は同期
// プロバイダーの実行中も表を保持し、trueの場合だけ付随データを確定
// する。任意の表示位置調整も後の再計測ではなく、同じ公開処理に含める。
class PreparedTableUpdate {
public:
    PreparedTableUpdate() = default;
    PreparedTableUpdate(PreparedTableUpdate&&) noexcept = default;
    PreparedTableUpdate& operator=(PreparedTableUpdate&&) noexcept = default;
    PreparedTableUpdate(const PreparedTableUpdate&) = delete;
    PreparedTableUpdate& operator=(const PreparedTableUpdate&) = delete;
    [[nodiscard]] bool current() const noexcept;
    [[nodiscard]] bool commit() noexcept;
private:
    struct State;
    std::shared_ptr<State> state_;
    friend PreparedTableUpdate prepare_table_rows(
        std::shared_ptr<TableView>, std::vector<TableRow>,
        std::vector<std::size_t>, std::optional<std::size_t>, std::function<bool()>);
};
[[nodiscard]] PreparedTableUpdate prepare_table_rows(
    std::shared_ptr<TableView> table, std::vector<TableRow> rows,
    std::vector<std::size_t> selection,
    std::optional<std::size_t> reveal = {},
    std::function<bool()> current = {});
[[nodiscard]] bool replace_table_rows(std::shared_ptr<TableView> table,
                        std::vector<TableRow> rows,
                        std::vector<std::size_t> selection,
                        std::optional<std::size_t> reveal = {},
                        std::function<bool()> current = {});
// 逐次生成側は追加するまとまりだけを準備する。確定処理は行の内容を変える前に
// 領域を確保し、アプリケーションやネイティブのコールバックなしで追加する。
class PreparedTableAppend {
public:
    PreparedTableAppend() = default;
    PreparedTableAppend(PreparedTableAppend&&) noexcept = default;
    PreparedTableAppend& operator=(PreparedTableAppend&&) noexcept = default;
    PreparedTableAppend(const PreparedTableAppend&) = delete;
    PreparedTableAppend& operator=(const PreparedTableAppend&) = delete;
    [[nodiscard]] bool current() const noexcept;
    [[nodiscard]] bool commit() noexcept;
private:
    struct State;
    std::shared_ptr<State> state_;
    friend PreparedTableAppend prepare_table_append(
        std::shared_ptr<TableView>, std::vector<TableRow>, bool,
        std::function<bool()>);
};
[[nodiscard]] PreparedTableAppend prepare_table_append(
    std::shared_ptr<TableView> table, std::vector<TableRow> rows,
    bool reveal_last = false, std::function<bool()> current = {});
[[nodiscard]] bool append_table_rows(
    std::shared_ptr<TableView> table, std::vector<TableRow> rows,
    bool reveal_last = false, std::function<bool()> current = {});
[[nodiscard]] bool replace_table_rows(TableView& table, std::vector<TableRow> rows,
                        std::vector<std::size_t> selection,
                        std::optional<std::size_t> reveal = {},
                        std::function<bool()> current = {},
                        bool reset_top = false);
[[nodiscard]] bool cycle_table_cell_choice(TableView& table, std::size_t row,
                                           std::size_t column);
[[nodiscard]] bool resize_table_column(TableView& table, std::size_t column,
                                       float width);
[[nodiscard]] bool select_tree_items(
    TreeView& tree, std::vector<TreeItemId> selection,
    bool preserve_focus = false, bool notify = true);
// ツリー全体を準備してから、選択・展開・再描画のコールバックなしで公
// 開する。呼び出し側は残存する対象の安定したIDを維持する。不正なID
// 、親、循環は公開前に例外とする。選択・フォーカス・表示領域をまとめ
// て正規化し、表示位置調整では祖先を展開するが兄弟は展開しない。計測
// は再入し得るため、falseは新しい要求による失効またはcurrent()
// による所有元の拒否を表す。プロバイダー呼び出し中も共有ツリーを所
// 有し続ける。ノードの直接変更時は、呼び出し側で編集状態を無効にす
// る必要がある。 ムーブ専用の準備済み更新は、ディレクトリツリー、
// ファイル表、付随データなどの複合的な公開に対応する。prepare
// は確保・計測・再入を行い得るが、current/commitはプ
// ロバイダー呼び出し、確保、アプリケーションへの通知を行わない。
// すべてUIスレッドで使う。他コンポーネントの最終的な妥当性判定
// 内でcurrent()を確認し、間に別の呼び出しを挟まず確定す
// る。対象の寿命は呼び出し側が別途検証し、その判定関数は保存しな
// い。更新を破棄すると旧表示は残るが、それ以前の要求は失効する。
class PreparedTreeUpdate {
public:
    PreparedTreeUpdate() = default;
    PreparedTreeUpdate(PreparedTreeUpdate&&) noexcept = default;
    PreparedTreeUpdate& operator=(PreparedTreeUpdate&&) noexcept = default;
    PreparedTreeUpdate(const PreparedTreeUpdate&) = delete;
    PreparedTreeUpdate& operator=(const PreparedTreeUpdate&) = delete;
    [[nodiscard]] bool current() const noexcept;
    [[nodiscard]] bool commit() noexcept;
private:
    struct State;
    std::shared_ptr<State> state_;
    friend PreparedTreeUpdate prepare_tree_nodes(std::shared_ptr<TreeView>,
        std::vector<TreeNode>, std::vector<TreeItemId>,
        std::optional<TreeItemId>, std::function<bool()>);
};
[[nodiscard]] PreparedTreeUpdate prepare_tree_nodes(std::shared_ptr<TreeView> tree,
    std::vector<TreeNode> nodes, std::vector<TreeItemId> selection,
    std::optional<TreeItemId> reveal = {}, std::function<bool()> current = {});
[[nodiscard]] bool replace_tree_nodes(std::shared_ptr<TreeView> tree,
    std::vector<TreeNode> nodes, std::vector<TreeItemId> selection,
    std::optional<TreeItemId> reveal = {}, std::function<bool()> current = {});
// すでに表示対象のノードを、描画と同じ表示領域の寸法で見える位置へ移す。展
// 開、選択、通知、再描画は行わず、非表示または存在しないIDは無視する。
[[nodiscard]] bool reveal_tree_item(TreeView& tree, TreeItemId id,
                                     float row_height = 0);
// 対象の現在の祖先経路を展開してから、同じ表示領域の寸法で見える位
// 置へ移す。展開コールバックはノードを置換し得るため、呼び出しごと
// に経路を再検証する。対象が表示対象になった場合にtrueを返す。
[[nodiscard]] bool reveal_tree_item_with_ancestors(
    TreeView& tree, TreeItemId id, float row_height = 0);
[[nodiscard]] bool update_tree_selection(
    TreeView& tree, TreeItemId id, const std::vector<TreeItemId>& visible_order,
    SelectionInput input = {});
[[nodiscard]] bool navigate_tree_selection(
    TreeView& tree, SelectionNavigation navigation,
    std::size_t visible_count, bool extend = false, bool preserve_selection = false);
[[nodiscard]] bool navigate_tree_horizontal(TreeView& tree, bool forward,
                                            std::size_t visible_count = 1);
[[nodiscard]] bool set_tree_expanded(TreeView& tree, TreeItemId id,
                                     bool expanded, bool request_children = true,
                                     bool notify = true);
[[nodiscard]] bool set_tree_item_checked(TreeView& tree, TreeItemId id,
                                         bool checked);
[[nodiscard]] bool edit_tree_item_text(TreeView& tree, TreeItemId id,
                                       std::string text);

// TableView/TreeViewのその場編集の意
// 味上の寿命を管理する。ネイティブ表示側は自身の編集コ
// ントロールでこのバッファを表示してよいが、開始・取消
// ・検証・確定は全プラットフォームで同じ動作にする。
class InlineEditStateMachine {
public:
    [[nodiscard]] WidgetPtr owner() const noexcept;
    [[nodiscard]] const std::shared_ptr<TextEdit>& editor() const { return editor_; }
    [[nodiscard]] TextEditInteractionState& text_state() { return text_state_; }
    [[nodiscard]] const std::shared_ptr<ComboBox>& choice() const { return choice_; }
    void set_bounds(Rect bounds);
    [[nodiscard]] bool layout(float row_height);
    [[nodiscard]] bool insert(std::string_view text);
    [[nodiscard]] bool commit_choice();
    [[nodiscard]] bool begin(const std::shared_ptr<TableView>& table,
                             std::size_t row, std::size_t column);
    [[nodiscard]] bool begin(const std::shared_ptr<TreeView>& tree,
                             std::size_t node_index);
    [[nodiscard]] bool begin_tree_item(const std::shared_ptr<TreeView>& tree,
                                       TreeItemId id);
    /// 独自描画ウィジェット上で編集を開始する。矩形を返すコールバックはウ
    /// ィンドウ相対の座標を返し、スクロールや配置変更後に再評価される。
    [[nodiscard]] bool begin(const WidgetPtr& owner, std::string text,
        std::function<std::optional<Rect>()> bounds,
        std::function<bool(std::string)> commit,
        std::vector<ChoiceItem> choices = {}, bool editable_choice = false,
        std::function<void(const std::shared_ptr<TextEdit>&,
                           const std::shared_ptr<ComboBox>&)> created = {},
        std::function<void()> cancelled = {});
    [[nodiscard]] bool active() const noexcept;
    [[nodiscard]] bool matches(const std::shared_ptr<TableView>& table,
                               std::size_t row,
                               std::size_t column) const noexcept;
    [[nodiscard]] bool matches(const std::shared_ptr<TreeView>& tree,
                               std::size_t node_index) const noexcept;
    [[nodiscard]] const std::string& buffer() const noexcept;
    [[nodiscard]] bool set_buffer(std::string text);
    void synchronize_choice_selection();
    void publish_choice_selection();
    [[nodiscard]] bool append(std::string_view text);
    [[nodiscard]] bool erase_backward();
    [[nodiscard]] bool clear_buffer();
    [[nodiscard]] bool commit();
    void cancel();

private:
    struct InlineCompletion {
        std::function<void(bool)> callback;
        bool accepted{};
        ~InlineCompletion() noexcept {
            // 表示側の破棄中も含め、終了処理から例外を送出してはならない。
            if (callback) { try { callback(accepted); } catch (...) {} }
        }
    };
    struct TableSession {
        std::weak_ptr<TableView> table;
        std::size_t row{};
        std::size_t column{};
        std::vector<std::pair<std::string, std::string>> original_row;
        std::uint64_t row_order_revision{};
        std::optional<std::string> original_editor_value;
        std::vector<std::string> original_choice_values;
        std::shared_ptr<InlineCompletion> completion;
    };
    struct TreeSession {
        std::weak_ptr<TreeView> tree;
        TreeItemId id{};
        std::string original_text;
        std::uint64_t node_order_revision{};
    };
    struct GenericSession {
        std::weak_ptr<Widget> owner;
        std::function<std::optional<Rect>()> bounds;
        std::function<bool(std::string)> commit;
        std::function<void()> cancelled;
    };

    std::optional<TableSession> table_;
    std::optional<TreeSession> tree_;
    std::optional<GenericSession> generic_;
    std::shared_ptr<TextEdit> editor_{std::make_shared<TextEdit>()};
    TextEditInteractionState text_state_;
    std::shared_ptr<ComboBox> choice_;
    void initialize(std::string text, const WidgetPtr& owner);
    void reset_session() noexcept;
};

// 新規作成した資源など、ドメイン側の操作で要求された編集を開始する。 対象
// を現在公開されている行・ツリーノードへ解決してから実行する。 一致判定に
// は記憶した行・ノード番号ではなく、ドメイン上の同一性を使う。 編集列には
// TableView::inline_edit_columnの指定を使う。
struct ProgrammaticInlineEditRequest {
    std::shared_ptr<TableView> table;
    std::function<bool(std::size_t)> matches_stable_row;
    std::shared_ptr<TreeView> tree;
    std::function<bool(const TreeNode&)> matches_stable_tree_node;
    WidgetPtr generic_owner;
    std::string generic_text;
    std::function<std::optional<Rect>()> generic_bounds;
    std::function<bool(std::string)> generic_commit;
    std::function<void()> generic_cancelled;
    std::vector<ChoiceItem> generic_choices;
    bool generic_editable_choice{};
    std::function<void(const std::shared_ptr<TextEdit>&,
                       const std::shared_ptr<ComboBox>&)>
        generic_editor_created;
    WidgetPtr focus_scope;
    float row_height{};
};

// 表示側専用の補助処理。表示側が通常のフォーカス経路を提供し、編集状態機械を所有する。呼び
// 出し側はGuiBackend::request_programmatic_inline_edit
// を通じてProgrammaticInlineEditRequestを渡すだけ。選択、
// 表示位置調整、フォーカス変更は同期的に表を変更し得るため、InlineEditStateMachine::begin
// の前に安定した同一性と行順のリビジョンを再検証する。
[[nodiscard]] bool begin_presented_programmatic_table_inline_edit(
    InlineEditStateMachine& edit, const ProgrammaticInlineEditRequest& request,
    const std::function<bool(const WidgetPtr&)>& focus);
[[nodiscard]] bool begin_presented_programmatic_inline_edit(
    InlineEditStateMachine& edit, const ProgrammaticInlineEditRequest& request,
    const std::function<bool(const WidgetPtr&)>& focus);

// ツリーの可視性と行の対応付けはモデルで定める。表示側には、展
// 開状態とスクロールを適用した後のモデルのインデックスを渡す。
[[nodiscard]] std::vector<std::size_t> visible_tree_indices(const TreeView& tree);
// サイズ計算とスクロール範囲で共有する、スクロール前の可視内容の
// 寸法。非負のラベル上限は、読める最小幅の計算にだけ使う。フォン
// トプロバイダーを呼ばずに準備し、プロバイダーがノードを置換・破
// 棄する前に、可視ラベルと既存のツリー座標の先頭余白を所有する。
struct TreeWidthMeasurement {
    struct Row { std::string text; float leading{}; };
    std::vector<Row> rows;
    std::optional<FontSpec> font;
    float trailing{};
};
[[nodiscard]] TreeWidthMeasurement prepare_tree_width_measurement(const TreeView& tree);
// 値渡しにする。呼び出し側の準備済み入力自体が、コールバック内で置換される場合がある。
[[nodiscard]] float tree_content_width(TreeWidthMeasurement input, float label_limit = -1);
[[nodiscard]] float tree_content_width(const TreeView& tree, float label_limit = -1);
[[nodiscard]] std::size_t tree_visible_row_count(const TreeView& tree, float row_height);
[[nodiscard]] std::optional<std::size_t> hit_test_tree_row(
    const TreeView& tree, Rect bounds, Point point, float row_height,
    float header_height = 0.0F);

// 表示側ごとにツリー行の描き方は異なっても、展開部、チェック
// ボックス、アイコン、文字の配置はヒット判定と一致させる。
struct TreeRowGeometry {
    std::size_t model_index{};
    std::size_t depth{};
    Rect row{};
    Rect expander{};
    Rect check_box{};
    Rect icon{};
    Rect text{};
    bool has_expander{};
};

enum class TreeRowPart { Row, Expander, CheckBox, Icon, Text };

// Editorの標準の行寸法は、計測、描画、ヒット判定、イ
// ンライン編集で共有する。表示側で独自の間隔を指定しない。
[[nodiscard]] std::optional<TreeRowGeometry> tree_row_geometry(
    const TreeView& tree, Rect bounds, std::size_t model_index, float row_y,
    float row_height);
[[nodiscard]] std::optional<TreeRowGeometry> tree_row_geometry(
    const TreeView& tree, Rect bounds, std::size_t model_index, float row_y,
    float row_height, float leading_inset, float indent_width,
    float expander_width, float check_box_size, float icon_size,
    float check_box_gap, float icon_gap);
[[nodiscard]] std::optional<TreeRowPart> hit_test_tree_row_part(
    const TreeRowGeometry& geometry, Point point) noexcept;
// マウスまたはタッチの意味上の主クリック。選択が変わらなくても、可視
// 行がクリックを処理したかを返す。通知でツリーが再構築される場合があ
// るため、通知をまたいで保持するのは行の参照ではなく項目IDとする。
[[nodiscard]] bool click_tree_at(TreeView& tree, Point point,
                                  SelectionInput input = {});

[[nodiscard]] std::size_t clamp_top_index(std::size_t current,
                                           std::size_t item_count,
                                           std::size_t visible_count) noexcept;
[[nodiscard]] std::size_t visible_row_count(float viewport_height,
                                             float row_height,
                                             std::size_t reserved_rows = 0U) noexcept;
[[nodiscard]] bool set_top_index(std::size_t& current, std::size_t requested,
                                 std::size_t item_count,
                                 std::size_t visible_count) noexcept;
[[nodiscard]] bool scroll_top_index(std::size_t& current, std::int64_t delta,
                                    std::size_t item_count,
                                    std::size_t visible_count) noexcept;
// 線形の項目表示に行単位の差分を適用する。表示側はホイー
// ルやタッチの操作を行数に変換する。項目数、表示可能な
// 行数、ツリーの展開、範囲制限は共通の表示規則で扱う。
[[nodiscard]] bool scroll_item_view_rows(const WidgetPtr& view,
                                         std::int64_t delta,
                                         float row_height) noexcept;
// ホイールとタッチの画素差分には同じ変換を使い、一行未満の移動量も保持する。
[[nodiscard]] bool scroll_item_view_by(const WidgetPtr& view, Point delta,
                                      float row_height, float& row_remainder,
                                      bool* invalidated = nullptr,
                                      const std::function<bool()>& current = {});
class ItemViewScrollStateMachine {
public:
    ItemViewScrollStateMachine();
    ItemViewScrollStateMachine(const ItemViewScrollStateMachine&);
    ItemViewScrollStateMachine& operator=(const ItemViewScrollStateMachine&);
    ItemViewScrollStateMachine(ItemViewScrollStateMachine&&) noexcept = default;
    ItemViewScrollStateMachine& operator=(ItemViewScrollStateMachine&&) noexcept = default;
    ~ItemViewScrollStateMachine();
    [[nodiscard]] bool scroll(const WidgetPtr& view, Point delta, float row_height,
                              bool* invalidated = nullptr);
private:
    struct State;
    std::shared_ptr<State> state_;
};
// ネイティブ表示側の先頭表示行を共通モデルへ同期する。表示側
// はネイティブの行と表示可能な行数だけを提供する。項目数、ツ
// リーの可視性、範囲制限はプラットフォームに依存させない。
[[nodiscard]] bool set_item_view_top_index(Widget& view,
                                           std::size_t requested,
                                           std::size_t visible_count) noexcept;
[[nodiscard]] std::optional<std::size_t> tree_visible_position(
    const TreeView& tree, TreeItemId id);
[[nodiscard]] bool set_scroll_offset(ScrollView& scroll, Point offset,
                                     Size content_size, Size viewport_size);
[[nodiscard]] bool scroll_by(ScrollView& scroll, Point delta,
                             Size content_size, Size viewport_size);
// 配置済みの共通モデルから論理的な内容の寸法を計算する。表
// 示側はネイティブのホイールやつまみの移動量だけを渡し、
// プラットフォーム固有の別のスクロール範囲を計算しない。
[[nodiscard]] Size scroll_content_size(const ScrollView& scroll) noexcept;

[[nodiscard]] bool point_inside(Rect bounds, Point point) noexcept;
[[nodiscard]] std::optional<std::size_t> hit_test_tool_bar_item(
    const ToolBar& toolbar, Point point);
[[nodiscard]] std::optional<std::size_t> hit_test_menu_bar_item(
    const Menu& menu, Point point);
[[nodiscard]] std::string tool_bar_tooltip(const ToolBar& toolbar, Point point);
[[nodiscard]] bool drag_threshold_reached(Point start, Point current,
                                          float threshold) noexcept;

struct PointerGestureUpdate {
    Point delta{};
    bool began{};
    bool active{};
};

// ポインタードラッグのしきい値と移動差分を、プラットフォーム非依存で管理する。
// 表示側はポインターIDとキャプチャだけを保持し、ネイティブ座標を変換する。
class PointerGestureStateMachine {
public:
    void begin(Point point) noexcept;
    [[nodiscard]] PointerGestureUpdate update(Point point, float threshold,
                                              bool eligible = true) noexcept;
    void reset() noexcept;
    [[nodiscard]] bool active() const noexcept { return active_; }
    [[nodiscard]] Point start() const noexcept { return start_; }
    [[nodiscard]] Point last() const noexcept { return last_; }

private:
    Point start_{};
    Point last_{};
    bool initialized_{};
    bool active_{};
};

[[nodiscard]] std::optional<std::size_t> hit_test_linear_row(
    Rect bounds, Point point, float row_height, std::size_t top_index,
    std::size_t item_count, float header_height = 0.0F) noexcept;
[[nodiscard]] float table_row_height(const TableView& table, float fallback) noexcept;
// 代替値はすでに配置後の画素単位。指定された表の高
// さは論理的な最小値とする。実効高を再計算しても、
// 代替値にUIやフォントの倍率を二重適用しない。
[[nodiscard]] float item_row_height(const Widget& view, float fallback) noexcept;
[[nodiscard]] float table_header_height(const TableView& table, float fallback) noexcept;
[[nodiscard]] std::size_t table_visible_row_count(const TableView& table, float row_height) noexcept;
[[nodiscard]] Rect table_row_geometry(const TableView& table, std::size_t row, float row_height) noexcept;
struct HorizontalScrollGeometry {
    Rect viewport{};
    Rect track{};
    Rect thumb{};
    float maximum{};
};
struct ItemScrollGeometry {
    Rect viewport{};
    HorizontalScrollGeometry horizontal{}, vertical{};
    std::size_t visible_rows{}, total_rows{};
};
[[nodiscard]] ItemScrollGeometry pixel_scroll_geometry(Rect bounds, Size content,
                                                       Point offset, bool horizontal = true,
                                                       bool vertical = true, bool reserve_gutters = false);
// 一方のバーの表示で他方も必要になる場合があるため、両バーを同時に決め
// る。描画、ヒット判定、キー移動、ポインターによるスクロールで共有する
// 。準備済みの置換内容は、公開済みの行を変更せずに件数を提供できる。
[[nodiscard]] ItemScrollGeometry item_scroll_geometry(
    const Widget& view, float row_height = 0, std::optional<Rect> bounds = {},
    std::optional<std::size_t> planned_row_count = {});
[[nodiscard]] HorizontalScrollGeometry horizontal_scroll_geometry(Rect bounds, float content_width, float offset);
[[nodiscard]] HorizontalScrollGeometry table_horizontal_scroll_geometry(const TableView& table);
[[nodiscard]] HorizontalScrollGeometry tree_horizontal_scroll_geometry(const TreeView& tree);
[[nodiscard]] HorizontalScrollGeometry item_horizontal_scroll_geometry(const Widget& view);
[[nodiscard]] bool set_table_horizontal_offset(TableView& table, float value);
[[nodiscard]] bool set_item_horizontal_offset(Widget& view, float value);
// つまみをつかんだ位置のずれと、ページ送りクリックの動作を管理する。バックエン
// ドはポインターをキャプチャして位置を渡すだけで、スクロール値は計算しない。
struct ScrollBarAccess {
    std::function<ItemScrollGeometry()> geometry;
    // 垂直軸、画素位置、相対的なページ移動量または絶対的なつまみ位置。
    std::function<bool(bool, float, bool)> set_position;
    // 省略可能な外部状態の確認（TextEditの操作状態、仮想プロバ
    // イダーなど）。falseならポインターキャプチャ全体を終了する。
    std::function<bool()> current;
};
// この表示の縦または横スクロールバーのつまみを、ポインターで保
// 持・ドラッグしている間はtrue。表示側は全ホストで、つか
// んだつまみを押下色で描く。ALLと単体版は同じ経路を使う。
[[nodiscard]] bool scroll_thumb_pressed(const Widget& view, bool vertical) noexcept;

class ItemScrollBarStateMachine {
public:
    bool begin(const WidgetPtr& view, Point point, ScrollBarAccess access = {});
    bool move(Point point);
    void reset() noexcept;
    [[nodiscard]] WidgetPtr owner() const noexcept { return view_.lock(); }
private:
    enum class Mode { None, Passive, Page, Thumb };
    [[nodiscard]] bool source_current(const WidgetPtr& view) const noexcept;
    [[nodiscard]] bool remember_source(const WidgetPtr& view) noexcept;
    struct ContainerGeneration {
        std::weak_ptr<Container> owner;
        std::uint64_t scroll{},provider{};
        std::weak_ptr<Layout> layout;
        Rect bounds;
        std::vector<std::weak_ptr<Widget>> children;
        // スクロール対象の表示自身の矩形だけを比較する。スク
        // ロールや、保留中のつまみ位置を適用するフレーム配
        // 置によって、すべての子孫の位置は正当に変化する。
        bool compare_bounds{};
    };
    std::weak_ptr<Widget> view_;
    ScrollBarAccess access_;
    float grab_{};
    float scale_{};
    Rect track_{};
    bool vertical_{};
    float maximum_{}, thumb_length_{};
    float expected_thumb_origin_{};
    std::optional<std::uint64_t> expected_revision_;
    std::vector<ContainerGeneration> container_generations_;
    Mode mode_{Mode::None};
    // 公開済みのつまみ押下状態を保持する。reset()またはこ
    // の状態機械（ジェスチャーのキャプチャ）の破棄で解放する。
    std::shared_ptr<const void> pressed_token_;
};
[[nodiscard]] std::optional<std::size_t> hit_test_table_row(
    const TableView& table, Point point, float row_height) noexcept;
// ドラッグによる挿入では、最下行の見えている部分についてDの座標から項目
// を取得する動作に合わせる。通常のポインター選択のヒット判定は変えない。
[[nodiscard]] std::optional<std::size_t> hit_test_table_drop_row(
    const TableView& table, Point point, float row_height) noexcept;
// チェック可能な線形の行では、描画とヒット判定にまったく同じ矩形を使う。
[[nodiscard]] std::optional<Rect> linear_row_check_box_geometry(
    Rect bounds, float row_y, float row_height, float leading_inset,
    float check_box_size) noexcept;
// 表のすべての列幅が(0, 1]の形式なら、列幅は相対比率を表
// す。この共通モデルの規則を一か所で解決し、ネイティブとSDL
// の表示、サイズ変更、ヒット判定が食い違わないようにする。
[[nodiscard]] std::vector<float> resolved_table_column_widths(
    const TableView& table, float available_width,
    float fallback_width = 120.0F, float minimum_width = 40.0F);
// ユーザーが相対幅や伸縮幅の一列を変更する前に、解決済みの配置を指定時の
// UI単位（拡大後の画素ではない）で固定する。固定しないと編集列だけが絶
// 対幅になり、未編集の相対幅の列が次のフレームで最小幅に縮んでしまう。
[[nodiscard]] bool materialize_table_column_widths(
    TableView& table, float available_width,
    float fallback_width = 120.0F, float minimum_width = 40.0F);
[[nodiscard]] std::optional<std::size_t> hit_test_table_column(
    const TableView& table, float x, float fallback_width = 120.0F);
[[nodiscard]] std::optional<std::size_t> hit_test_table_resize_column(
    const TableView& table, Point point, float header_height,
    float tolerance = 4.0F, float fallback_width = 120.0F);
// ポインター座標は表示画素単位、保存値とコールバックの幅は拡大
// 前のUI単位。押しただけでは伸縮幅や相対幅の列を固定しない。
class TableColumnResizeStateMachine {
public:
    bool begin(const std::shared_ptr<TableView>& table, Point point, float row_height);
    bool move(Point point);
    bool finish(Point point);
    void reset() { table_.reset(); }
    [[nodiscard]] std::shared_ptr<TableView> owner() const { return table_.lock(); }
private:
    std::weak_ptr<TableView> table_;
    std::size_t column_{}, column_count_{};
    std::string column_title_;
    float initial_x_{}, initial_width_{}, expected_width_{}, scale_{1};
    float expected_stretch_{};
    TextAlignment column_alignment_{TextAlignment::Start};
    bool column_sortable_{};
    bool materialized_{};
};
// 並べ替え列の強調は表本体だけを覆う。
// 行全体の強調表示が無効なら先頭列だけ
// を覆い、暗黙に行の全幅を強調しない。
[[nodiscard]] std::optional<Rect> table_body_column_geometry(
    const TableView& table, std::size_t column, float header_height,
    float fallback_width = 120.0F);
[[nodiscard]] Rect table_row_selection_geometry(
    const TableView& table, float row_y, float row_height,
    float fallback_width = 120.0F);
[[nodiscard]] WidgetPtr hit_test_widget(const WidgetPtr& root, Point point,
                                        float tab_header_height,
                                        float expand_header_height);

// 指定点にある、スクロールを所有する最も深いウィジェットを返す。非アクティブなタブペ
// ージと折りたたまれたExpandBarの内容は、共通のヒット判定規則で除外する。
[[nodiscard]] WidgetPtr hit_test_scrollable_widget(
    const WidgetPtr& root, Point point, float tab_header_height,
    float expand_header_height);

struct FocusKeyResult {
    bool handled{};
    std::shared_ptr<TabView> changed_tabs;
};

// DはDisplay全体のSWT.FocusInフィルター（DockingFolder.FocusL
// ）で、最後に操作したドッキングペインを把握する。共通のフォーカス
// 状態機械は、確定したすべてのフォーカス変更をここに公開し、OS固
// 有のフックなしで同じ情報を得られるようにする。UIスレッド専用。
using FocusObserver = std::function<void(const WidgetPtr&)>;
[[nodiscard]] std::uint64_t add_focus_observer(FocusObserver observer);
void remove_focus_observer(std::uint64_t id) noexcept;

class FocusStateMachine {
public:
    [[nodiscard]] WidgetPtr focused() const noexcept { return focused_.lock(); }
    [[nodiscard]] std::uint64_t revision() const noexcept { return generation_; }
    // 編集中の値の確定自体がフォーカスを変更し得るため、フォーカス喪失・取得の
    // 通知と同じ保護された遷移内で実行する。before_changeがfalse
    // を返した場合は、旧コントロールのフォーカスを外さずに移動を取り消す。
    [[nodiscard]] bool set(const WidgetPtr& next, const WidgetPtr& scope = {},
                           std::function<bool()> before_change = {});
    [[nodiscard]] bool move(const WidgetPtr& root, bool backward = false);
    [[nodiscard]] FocusKeyResult key(const WidgetPtr& root, Key key, Modifiers modifiers);
    void clear() { (void)set({}); }

private:
    std::weak_ptr<Widget> focused_;
    std::uint64_t generation_{};
};

// プラットフォーム非依存のキーボード巡回順でコントロールを返
// す。非表示ページと無効な部分木はフォーカス巡回に含めない。
[[nodiscard]] bool accepts_keyboard_focus(const WidgetPtr& widget);
[[nodiscard]] std::vector<WidgetPtr> focusable_widgets(const WidgetPtr& root);
// 末端のフラグだけでなく、所有関係と祖先の可視・有効状態を検証する。ドッキング
// 選択の要求では、DのComposite.setFocusと同様に、優先された
// アクティブなコンテナーがフォーカス不可なら、最初のフォーカス可能な子孫を選
// ぶ。ほかの呼び出し側では選択中のページヘッダーを既定とする。削除・終了した
// コントロールには入力を渡さない。代替先は初期フォーカスと復元時だけに使う。
[[nodiscard]] WidgetPtr focus_target_in_scope(const WidgetPtr& root,
    const WidgetPtr& preferred, bool fallback = false,
    bool prefer_active_descendant = false);
// フォーカス中のコントロールとポップアップがキーを処理しなかった場合だけ実行
// する。ダイアログの確定・取消は、ボタンクリックと同じコールバックを呼ぶ。
[[nodiscard]] bool handle_dialog_key(const WidgetPtr& root, Key key,
                                     Modifiers modifiers = Modifiers::None);

class ModalStateMachine {
public:
    using SessionId = std::uint64_t;
    struct Session {
        SessionId id{};
        std::shared_ptr<Dialog> dialog;
        std::shared_ptr<Window> owner;
        // モーダルセッションの前にキーボードフォーカスを持っていた論理コ
        // ントロール。表示側はfinish()後に復元する。ここで状態を
        // 保持し、Win32とSDLで異なるフォーカス規則を作らない。
        WidgetPtr focused;
        bool owner_was_enabled{true};
        bool external{};
        // 取得時の祖先関係であり、別のウィンドウ/フォーカススタックではない。ダイアロ
        // グや所有元の親が変わった場合、古い操作を新しい範囲へ公開できないようにする。
        std::vector<std::shared_ptr<Window>> owner_chain;
    };

    [[nodiscard]] std::optional<SessionId> begin(
        std::shared_ptr<Dialog> dialog,
        std::shared_ptr<Window> owner,
        WidgetPtr focused = {});
    [[nodiscard]] std::optional<Session> finish(SessionId id);
    // OSの選択画面は自身の描画面を所有するが、Editor描画のダイアログ
    // と同じモーダル入力スタック、所有元・フォーカスの寿命管理に参加する。
    [[nodiscard]] std::optional<SessionId> begin_external(
        std::shared_ptr<Window> owner, WidgetPtr focused = {});
    [[nodiscard]] bool has_external_session() const noexcept;
    [[nodiscard]] bool active(SessionId id) const noexcept;
    // 所有元の可視性はライフサイクルの状態であり、有効状態や入力状態ではない。
    // 所有元の連鎖全体をたどり、循環はモーダルループを停止不能にせず拒否する。
    [[nodiscard]] bool can_continue(SessionId id) const;
    // ダイアログの結果や可視性とは独立に判定する。確定済みのダイアロ
    // グがすでに非表示でも、所有元が消失していれば結果を取り消す。
    [[nodiscard]] bool owner_available(SessionId id) const;
    [[nodiscard]] static bool owner_available(const Session& session);
    [[nodiscard]] bool allows(const std::shared_ptr<Window>& window) const;
    [[nodiscard]] const std::vector<Session>& sessions() const noexcept {
        return sessions_;
    }

private:
    std::vector<Session> sessions_;
    SessionId next_id_{1U};
};

}  // namespace cwxeditor::lib
