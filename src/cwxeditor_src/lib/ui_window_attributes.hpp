#pragma once

#include "components.hpp"
#include "window_host.hpp"
#include <array>

namespace cwxeditor::lib {
// 実行時の同期用であり、別のWindowモデルではない。不透明度は完成
// フレームのトランザクション、可視性と所有関係はライフサイクルで扱う。
enum class WindowAttribute : std::size_t {
    Title, Icon, Resizable, SizeLimits, AlwaysOnTop, Maximized, Modified,
    MinimizeButton, MaximizeButton, Count
};
enum class WindowAttributeResult { Pending, Accepted, Unsupported, Failed };
// Acceptedはアダプターが要求を受理したことを表し、画素の表示やネイ
// ティブのウィンドウ管理規則の証明ではない。未対応を成功として報告しない。
struct WindowAttributeReport {
    std::array<WindowAttributeResult,static_cast<std::size_t>(WindowAttribute::Count)> values{};
    [[nodiscard]] WindowAttributeResult get(WindowAttribute attribute) const noexcept;
};
struct WindowAttributes {
    std::string title;
    ResourceId icon{};
    bool resizable{}, always_on_top{}, maximized{}, modified{}, minimizable{}, maximizable{};
    std::optional<Size> minimum, maximum;
    // 変更不可の世代であり、別の画像キャッシュではない。資源を更新してもIDが同
    // じ場合があるため、アダプターはネイティブ呼び出し中もこの画素を保持する。
    std::shared_ptr<const ImageData> icon_image;
};
[[nodiscard]] WindowAttributes prepare_window_attributes(const Window& window,
    const WindowGeometryPlan& geometry);
class WindowAttributeStateMachine {
public:
    using Apply=std::function<WindowAttributeResult(WindowAttribute,const WindowAttributes&)>;
    // ネイティブ属性のポーリングで、ALLのフレームごとにウィジェッ
    // トツリー全体を計測しない。宣言された制限、利用可能な作業領域、
    // UI文字倍率が変わった場合に座標制約を再計測する。明示的な座標
    // 操作は、自身の完全な計画を適用した後にキャッシュを無効化する。
    [[nodiscard]] WindowAttributes prepare(const Window& window,const WindowGeometryRequest& geometry);
    void invalidate_constraints() noexcept { constraint_key_.reset(); }
    void invalidate(WindowAttribute attribute) noexcept;
    // 状態は値で取得する。ネイティブへの反映時にアプリケーションのコードが呼ば
    // れ、モデルの変更、このウィンドウの終了、新しい属性の要求が起こり得る。
    void synchronize(WindowAttributes requested, Apply apply,
                     std::function<bool()> live = {});
    [[nodiscard]] WindowAttributeReport report() const noexcept { return report_; }
    // 描画面の置換や対応機能の変更後に使う。失敗した操作は無
    // 限のタイマーではなく、次の明示的な同期で再試行する。
    void reset() noexcept;
private:
    static constexpr auto count=static_cast<std::size_t>(WindowAttribute::Count);
    std::array<std::optional<WindowAttributes>,count> acknowledged_;
    std::array<std::optional<WindowAttributes>,count> applying_;
    std::array<std::uint64_t,count> applying_revision_{};
    WindowAttributeReport report_;
    std::uint64_t revision_{};
    struct ConstraintKey {
        std::optional<Size> minimum,maximum;
        Rect available;
        float scale{};
        ComponentKind kind{};
        bool embedded{};
    };
    std::optional<ConstraintKey> constraint_key_;
    WindowGeometryPlan constraints_;
};
} // namespace cwxeditor::lib
