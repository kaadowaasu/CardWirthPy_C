#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include "types.hpp"
#include "events.hpp"

namespace cwxeditor::lib {

enum class CursorKind;

// 単体版またはCardWirthPy_C_Allの共通Editor表
// 示側を作る際に使う、プロセス全体の合成パラメーター。ホストはハン
// ドルと規則を渡し、別の配置・描画・入力・モーダル実装は選ばない。
struct EditorWindowHostGlobals final {
    std::atomic_bool all_mode{false};
    std::atomic_uintptr_t native_parent{0};
    std::atomic_uintptr_t native_sdl_window{0};
    std::atomic_uintptr_t native_renderer{0};
    std::atomic_bool keep_top_bar{true};
    std::atomic_bool allow_internal_move{true};
    std::atomic<float> hosted_canvas_width{1590.0F};
    std::atomic<float> hosted_canvas_height{910.0F};
};

struct EditorWindowHostOptions final {
    bool all_mode{};
    std::uintptr_t native_parent{};
    std::uintptr_t native_sdl_window{};
    std::uintptr_t native_renderer{};
    bool keep_top_bar{true};
    bool allow_internal_move{true};
    Size hosted_canvas_size{1590.0F, 910.0F};
    // Web ALLとAndroid ALLは一つのEditor UIを共有する。
    // Web専用やOS専用のサイズ、メニュー、ツールを選ぶホストフラグは設けない。
};

extern EditorWindowHostGlobals g_editor_window_host;

[[nodiscard]] EditorWindowHostOptions editor_window_host_options() noexcept;
void set_editor_window_host_options(const EditorWindowHostOptions& options) noexcept;
// Web/AndroidのALLでは「フォルダーを開く」からOSのファイルマ
// ネージャーを起動できないため、ALLホスト自身のData Explorer
// ページで表示する。開始処理はEditorの絶対パスを受け取り、Data Explorer
// のユーザー保存領域外ならfalseを返す。UIスレッド専用。
using EditorDataDirectoryOpener = std::function<bool(const std::filesystem::path&)>;
void set_editor_data_directory_opener(EditorDataDirectoryOpener opener);
// Editorが公開する、ALLのタイトル部の「元に戻す」「やり
// 直し」ボタンのアイコン。0はアイコンなし。UIスレッド専用。
void set_hosted_caption_history_icons(ResourceId undo, ResourceId redo) noexcept;
[[nodiscard]] ResourceId hosted_caption_undo_icon() noexcept;
[[nodiscard]] ResourceId hosted_caption_redo_icon() noexcept;
// Dのdutils.setWaitCursors/resetCursors
// に対応し、同期処理中は全ShellでCURSOR_WAITを表示す
// る。UIスレッドが処理中は入力イベントが来ないため、バックエンドが
// 反映処理を登録してカーソルを直ちに変更する。一つでも要求が有効な
// 間は、ホバー状態の公開でもWaitを維持する。UIスレッド専用。
void set_cursor_realizer(std::function<void(CursorKind)> realizer);
void begin_wait_cursor();
void end_wait_cursor();
void end_wait_cursor(CursorKind restored_cursor);
[[nodiscard]] bool wait_cursor_active() noexcept;
// 反映処理を登録したスレッド、またはまだ登録されていない場合にtrue。
[[nodiscard]] bool cursor_ui_thread() noexcept;
// 公開するCursorKind::Customカーソルの画像。UIスレッド専用。
void set_active_custom_cursor(ResourceId image) noexcept;
[[nodiscard]] ResourceId active_custom_cursor() noexcept;
[[nodiscard]] bool editor_can_open_data_directory() noexcept;
// 製品ごとにダイアログの処理経路を統一する。すべてのALLホスト（Web、
// Android、iOS、デスクトップ）は非同期の完了連鎖を使う。単体Web
// 版も、入れ子のモーダルループがブラウザの描画を止めるため同じ経路を使う
// 。単体ネイティブEditorだけがDの同期的なモーダル処理を維持する。
[[nodiscard]] bool editor_uses_async_dialog_flow(Platform platform) noexcept;
bool editor_open_data_directory(const std::filesystem::path& directory);
// ALLのコンパクト表示では、Editorの各フォントに論理画
// 素を2画素追加する。単体版はD互換の指定サイズを変更しない。
[[nodiscard]] float editor_presented_font_points(float authored_points) noexcept;
// 論理UI寸法には全ホストでcw.sを一度だけ適用する。ALLのフォント
// 調整は、別の全体倍率ではなくフォントを考慮した最小サイズ計測で扱う。
[[nodiscard]] float editor_presented_ui_metric_scale() noexcept;
// 合成ホストが提供する論理矩形。ホストの値が不正ならEditorキャ
// ンバスの一定の既定値を使い、描画アダプターで別のサイズを定めない。
[[nodiscard]] Rect editor_hosted_canvas_bounds(
    const EditorWindowHostOptions& options) noexcept;

// ネイティブとSDLの表示側で共有する座標規則。単一描画面のモバ
// イルウィンドウなど、ホスト矩形の原点は0とは限らない。すべて
// の範囲制限はデスクトップ原点を仮定せず、ホストに対して行う。
[[nodiscard]] Rect constrain_hosted_bounds(Rect bounds, Rect host,
                                           bool fill_host = false) noexcept;
[[nodiscard]] Rect center_hosted_bounds(Rect bounds, Rect anchor,
                                        Rect host) noexcept;
[[nodiscard]] Rect move_hosted_bounds(Rect bounds, Point pointer,
                                      Point drag_offset, Rect host) noexcept;

class Window;
enum class CursorKind;

// 矩形はすべてアダプターが提供する座標系のクライアント矩形。初期要求は
// 指定済みの矩形で、すでにUI倍率が適用されている場合もある。明示的な
// 要求は実クライアント画素であり、ドラッグ中に中央へ戻してはならない。
enum class WindowGeometryIntent { Initial, Explicit };
struct WindowGeometryRequest {
    Rect bounds;
    Rect available;
    std::optional<Rect> owner;
    bool embedded{};
    WindowGeometryIntent intent{WindowGeometryIntent::Initial};
};
struct WindowGeometryPlan {
    Rect bounds;
    std::optional<Size> minimum, maximum;
    // ネイティブのウィンドウマネージャーが最大化時の座標を管理する場合はfalse。
    // アダプターは制限を適用するが、通常サイズへの変更や移動を要求してはならない。
    bool apply_bounds{true};
};
[[nodiscard]] Rect authored_window_bounds(const Window& window) noexcept;
// モデル変更、配置コールバック、ネイティブハンドル、OSの列挙値を扱わない。描画面
// の制約によって指定された内容サイズを書き換えず、小さいホストではスクロールする。
[[nodiscard]] WindowGeometryPlan plan_window_geometry(
    const Window& window,const WindowGeometryRequest& request) noexcept;

// 組み込みの初期配置は、デスクトップALLと単一描画面のモバ
// イルホストで同じ。アダプターは同じ座標系の矩形を渡すだけ。
[[nodiscard]] Rect place_hosted_window(const Window& window, Rect bounds, Rect host,
                                        std::optional<Rect> owner = {}) noexcept;

struct WindowTextScaleChange {
    Size logical_size{};
    Rect current_bounds{};
    Rect available_bounds{};
    std::optional<Rect> owner_bounds;
    bool embedded{};
};

// 初期表示、明示的な移動、文字倍率の変更で、同じ配置・制
// 約の結果を使う。制限が空なら古いOSの制限を解除する。
using WindowTextScalePlan = WindowGeometryPlan;

// 文字倍率の変更後、すでに表示済みのダイアログに対する座標・制約の
// 更新内容を作る。モデルの変更やOSウィンドウへの反映は行わない。
[[nodiscard]] std::optional<WindowTextScalePlan> plan_window_text_scale_change(
    const Window& window, const WindowTextScaleChange& change) noexcept;

// いずれの計画も反映する前に、全体の状態を取得する。共通座標系の
// 入れ子のダイアログは、ネイティブ側の反映途中で見える新旧いず
// れかの矩形ではなく、所有元の新しい座標に対して中央配置する。
struct WindowTextScaleSurface {
    std::shared_ptr<Window> window;
    WindowTextScaleChange change;
    // 座標系の不透明な識別子。OSの規則を選ぶものではない。ネイティブのデスクトッ
    // プウィンドウは同じ座標系を共有し、組み込みウィンドウはホストの座標系を使う。
    std::uintptr_t coordinate_space{};
};
// 結果は入力のインデックスを維持する。存在しないウィンドウや通常のウィンドウには計画を
// 作らない。異なる座標系の所有元や不正な循環の場合は、取得済みの基準位置を維持する。
[[nodiscard]] std::vector<std::optional<WindowTextScalePlan>> plan_window_text_scale_changes(
    std::span<const WindowTextScaleSurface> surfaces);

// ネイティブの可視性は論理的な可視性と分ける。画面外の配置、入
// 力テスト、フレーム取得に必要。所有元が非表示なら、そのフラグ
// を明示的にコピーしないダイアログも含め、子孫も画面外に保つ。
[[nodiscard]] bool window_presentation_hidden(const std::shared_ptr<Window>& window);

// 描画とヒット判定で共有する座標情報。通常のUI文字倍率はタイトル部
// の装飾に適用し、カードやメッセージで指定された座標には適用しない。
struct HostedCaptionLayout {
    Rect caption;
    Rect close_button;
    Rect content;
    // タイトル部に最小化ボタン（閉じるボタンの左）がない場合は空。
    Rect minimize_button;
    // Dにはない追加仕様。タイトル部の中央に元に戻す・やり直
    // しボタンを置き、誤操作を防ぐ間隔を空ける。タイトルや
    // ほかのボタンと重ならずに収まる幅がなければ空にする。
    Rect undo_button;
    Rect redo_button;
};
// buttons=falseでは閉じるボタンと元に戻す・やり直しボタンを省く。
// 呼び出し側はminimize=falseも渡し、タイトルだけの見出しにする。
[[nodiscard]] HostedCaptionLayout layout_hosted_caption(Rect bounds,
                                                       bool visible = true,
                                                       bool minimize = false,
                                                       bool buttons = true) noexcept;
// ALLの組み込みモードレスダイアログはタイトル部から最小化し、Editor
// のダイアログメニューから復元できる。モーダルダイアログはそ
// のメニューを操作できなくするため、最小化ボタンを設けない。
[[nodiscard]] bool hosted_minimizable(const Window& window) noexcept;
// Editorダイアログの指定クライアントサイズは単体版とALLで同じ
// 。共通のALLタイトル部を実際に持つダイアログだけ、クライアント矩形
// を組み込みの外寸へ変換する。ルートウィンドウと重ね表示は変更しない。
[[nodiscard]] Rect hosted_window_outer_bounds(const Window& window,
                                               Rect content_bounds,
                                               bool caption_visible = true) noexcept;

struct HostedCaptionResult {
    bool consumed{};
    bool moved{};
    bool close_requested{};
    std::optional<CursorKind> cursor;
    bool minimize_requested{};
    // タイトル部の元に戻す・やり直しボタンを、押したボタン上で解放した状態。
    bool undo_requested{};
    bool redo_requested{};
};

// 表示側はマウス・タッチ・ペン入力を変換し、結果の矩形を反映する。こ
// の状態機械はキャプチャ、解放時の実行、取消を管理する。利用側のコ
// ールバックは呼ばず、終了結果はキャプチャのリセット後に通知する。
class HostedCaptionStateMachine {
public:
    HostedCaptionResult handle(const std::shared_ptr<Window>& window, Rect& bounds,
                               Rect host, bool visible, bool movable,
                               const PointerEvent& event, bool resizable = false);
    void cancel() noexcept;
    void cancel_for(const std::shared_ptr<Window>& window) noexcept;
    // コールバックは呼ばない。通知前に、対応するネイティブの取消によりキャプチャを解放する。
    bool cancel_pointer(const std::shared_ptr<Window>& window, const PointerEvent& event) noexcept;
    [[nodiscard]] std::shared_ptr<Window> owner() const { return owner_.lock(); }
private:
    std::weak_ptr<Window> owner_;
    PointerType pointer_type_{};
    std::uint64_t pointer_id_{};
    Point offset_{};
    Rect expected_{};
    float caption_height_{};
    bool close_{};
    bool minimize_{};
    bool undo_{};
    bool redo_{};
    bool movable_{};
    bool resizable_{};
    std::uint8_t resize_edges_{};
    Point start_pointer_{};
    Rect start_bounds_{}, host_{};
    WindowGeometryPlan resize_limits_;
    std::optional<Size> declared_minimum_, declared_maximum_;
};

}  // namespace cwxeditor::lib
