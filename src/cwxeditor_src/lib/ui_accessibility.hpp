#pragma once
#include "components.hpp"

#include <functional>
#include <memory>
#include <span>
#include <unordered_set>

namespace cwxeditor::lib {
struct AccessibilityRange {
    double value{}, minimum{}, maximum{}, step{};
    bool operator==(const AccessibilityRange&) const = default;
};
struct AccessibilityTextSelection {
    // 共通TextEditモデルと同じUTF-8のバイトオフセット。ネイティブ
    // のUTF-16やコードポイントとの変換はOSとの受け渡し層だけで行う。
    std::size_t start{}, length{};
    bool operator==(const AccessibilityTextSelection&) const = default;
};
struct AccessibilityPresentation {
    std::string name;
    std::string description;
    // 別のUIクラス階層を作らず、既存のコンポーネント分類を使う。OS
    // アダプターがネイティブのアクセシビリティ上の役割に対応付ける。
    std::optional<ComponentKind> kind;
    bool visible{}, enabled{}, focused{}, read_only{}, password{}, multiline{}, multi_select{};
    std::optional<bool> checked;
    bool indeterminate{};
    std::string value;
    std::optional<AccessibilityRange> range;
    std::optional<AccessibilityTextSelection> text_selection;
    std::optional<Orientation> orientation;
    std::size_t item_count{}, column_count{};
    std::vector<std::size_t> selected_indices;
    std::vector<TreeItemId> selected_tree_items;
    bool operator==(const AccessibilityPresentation&) const = default;
};

// OSのアクセシビリティ連携は論理的な対象を識別し、正
// 規化済みの意味上の操作要求を渡す。別のマウスクリッ
// ク、選択モデル、数値編集規則は作らない。対象が指定
// された共通入力範囲内にある間だけ要求を受け付ける。
enum class AccessibilityAction { Invoke, Select, Toggle, SetValue };
struct AccessibilityActionRequest {
    WidgetPtr target;
    AccessibilityAction action{AccessibilityAction::Invoke};
    std::optional<std::size_t> index;
    std::optional<TreeItemId> tree_item;
    std::optional<double> number;
    std::optional<std::string> text;
};

// ネイティブのアクセシビリティ連携と共通GUIホストの間に置く、ホスト所有
// の操作窓口。バックエンドは弱参照だけを保持し、ネイティブ要素やプラットフ
// ォームの登録表が操作コールバックを通じてSDL/共通ホストを生存させな
// いようにする。描画面は明示的に有効化し、retire(surface)
// は遅れて届いたネイティブのコールバックをホストへ配送する前に拒否する。
class AccessibilityActionDispatcher final {
public:
    using Handler = std::function<bool(std::uint64_t,
                                       const AccessibilityActionRequest&)>;

    // 再接続すると、それまで公開した全描画面の操作権
    // を失効させる。呼び出し側は現在のネイティブ要素
    // を公開するときに、各描画面を再度有効化する。
    void bind(Handler handler);
    [[nodiscard]] bool activate(std::uint64_t surface) noexcept;
    void retire(std::uint64_t surface) noexcept;
    void invalidate() noexcept;
    [[nodiscard]] bool dispatch(std::uint64_t surface,
                                const AccessibilityActionRequest& request) const noexcept;

private:
    Handler handler_;
    std::unordered_set<std::uint64_t> active_surfaces_;
};

// プラットフォーム側のアクセシビリティ連携は、描画面のトー
// クンと弱い論理識別情報だけを保持する。ポインター入力や別
// の選択モデルを作らず、共通ディスパッチャーへ意味上の操作
// を返す。空の実装では操作連携を提供しないことを明示する。
class PlatformAccessibilityBackend {
public:
    virtual ~PlatformAccessibilityBackend() = default;
    // ホストが共有ディスパッチャーを設定し、自身の寿命中は保持する。プ
    // ラットフォームのバックエンドは弱参照だけを保存する。publish/retire
    // の実装は、ネイティブ要素の登録変更とともに、保護されたactivate/retire
    // 補助関数を呼ぶ必要がある。
    void set_host_action_dispatcher(
        std::weak_ptr<AccessibilityActionDispatcher> dispatcher) noexcept;
    // native_surfaceはアダプターの不透明なハンドル（SDLではSDL_Window
    // ）。ネイティブ要素の接続のために参照してよいのは、プラットフォームの受け渡し層だけ。
    virtual void publish(std::uint64_t surface, void* native_surface, const WidgetPtr& root,
                         const WidgetPtr& focused,
                         const AccessibilityPresentation& presentation) = 0;
    virtual void retire(std::uint64_t surface) noexcept = 0;
    [[nodiscard]] virtual bool request(std::uint64_t,
                                       const AccessibilityActionRequest&) { return false; }
    [[nodiscard]] virtual bool action_bridge_available() const noexcept { return false; }

protected:
    [[nodiscard]] bool dispatch_host_action(
        std::uint64_t surface, const AccessibilityActionRequest& request) const noexcept;
    [[nodiscard]] bool activate_host_action_surface(std::uint64_t surface) const noexcept;
    void retire_host_action_surface(std::uint64_t surface) const noexcept;

private:
    std::weak_ptr<AccessibilityActionDispatcher> host_action_dispatcher_;
};

// ネイティブのアクセシビリティ要素は、現在フォーカスを持つ一つの
// 描画面に限定する。新しい状態を公開する前にホストがこれを呼び、
// フォーカスを失った描画面に操作可能な要素を残さない。spanに
// は組み込み/ネイティブの同じ描画面を指す別名が含まれてよく、
// 各トークンの失効は一度だけ行う。ここでは寿命だけを管理し、フ
// ォーカス、選択、操作の意味は共通入力ディスパッチャーで扱う。
void retire_inactive_accessibility_surfaces(
    PlatformAccessibilityBackend& backend, std::span<const std::uint64_t> surfaces,
    std::optional<std::uint64_t> active_surface) noexcept;

// 既存の共通の有効化・選択・値変更の規則を通じて配送する。古いアク
// セシビリティ要素から、非表示、非アクティブなタブ、折りたたみ、
// モーダル終了済み、子ウィンドウの対象を操作できないようにroot
// を必須とする。ネイティブアダプターは要素の識別情報だけを保持し
// 、共通処理の結果を公開してからプラットフォームAPIを呼ぶ。
[[nodiscard]] bool dispatch_accessibility_action(
    const WidgetPtr& root, const AccessibilityActionRequest& request);

// モデルから得たラベルは共有する。ネイティブアダプターはこの状態をアク
// セシビリティ連携へ出力するだけで、パスワードの内容をラベルにしない。
// これはウィジェット単体の状態であり、公開と操作の振り分けでは、所属ウ
// ィンドウの可視性、モーダル範囲、有効な同一性も確認する必要がある。
[[nodiscard]] AccessibilityPresentation prepare_widget_accessibility(
    const WidgetPtr& widget, const WidgetPtr& focused = {});
class AccessibilityAnnouncementStateMachine {
public:
    // ラベルが同じでも別のコントロールならフォーカス変更として扱う。フォ
    // ーカスが空または解除された場合は、古い通知を読み上げずに破棄する。
    bool update(const WidgetPtr& widget, const AccessibilityPresentation& presentation);
private:
    std::weak_ptr<Widget> owner_;
    AccessibilityPresentation previous_;
};
} // namespace cwxeditor::lib
