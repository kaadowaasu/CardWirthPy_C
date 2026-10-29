#pragma once

// cwx.editor.gui.dwt.timebar
// の移植可能なC++20実装。 元の
// SWT Canvasと同じ値・キー
// ・ポインター・通知のモデルを保ち、
// 描画は共通部品と描画APIで表す。

#include "cwxeditor_src/lib/components.hpp"
#include "cwxeditor_src/lib/events.hpp"
#include "cwxeditor_src/lib/graphics.hpp"
#include "cwxeditor_src/lib/portable.hpp"
#include "cwxeditor_src/lib/system.hpp"
#include "cwxeditor_src/lib/types.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cwxeditor::cwx::editor::gui::dwt::timebar {

using Duration = std::chrono::milliseconds;

/// DのDuration.minは、値が未初期化であることを示す。
inline constexpr Duration kUnsetDuration = Duration::min();

/// 共通Canvasは常に二重バッファーを使うが、 他のDWT
/// 部品と構築形式を揃えるため、スタイルビットは残す。
inline constexpr std::uint32_t STYLE_NONE = 0U;
inline constexpr std::uint32_t STYLE_DOUBLE_BUFFERED = 1U << 0U;

struct TimeBarLine {
    ::cwxeditor::lib::Point from{};
    ::cwxeditor::lib::Point to{};
    ::cwxeditor::lib::Color color{0U, 0U, 0U, 255U};
    float width{1.0F};
    ::cwxeditor::lib::StrokeStyle style{::cwxeditor::lib::StrokeStyle::Solid};
};

struct TimeBarRectangle {
    ::cwxeditor::lib::Rect rectangle{};
    ::cwxeditor::lib::Color color{0U, 0U, 0U, 255U};
    float width{1.0F};
    ::cwxeditor::lib::StrokeStyle style{::cwxeditor::lib::StrokeStyle::Solid};
};

struct TimeBarText {
    std::string text;
    ::cwxeditor::lib::Point origin{};
    ::cwxeditor::lib::FontSpec font{};
    ::cwxeditor::lib::Color color{0U, 0U, 0U, 255U};
};

struct TimeBarImage {
    ::cwxeditor::lib::ResourceId image{};
    ::cwxeditor::lib::Rect destination{};
};

/// 直近の描画操作をバックエンドに依存せず表す情報。 Canvas
/// の描画通知はRectだけなので、この状態を保持して
/// ヘッドレス側でも描画内容を確認できるようにする。
struct TimeBarRenderState {
    ::cwxeditor::lib::Rect area{};
    std::optional<TimeBarRectangle> fill;
    std::optional<TimeBarRectangle> border;
    std::vector<TimeBarLine> lines;
    std::vector<TimeBarImage> images;
    std::vector<TimeBarText> texts;
    std::optional<::cwxeditor::lib::Rect> focus;
    bool warning{};
};

struct WarningIcon {
    ::cwxeditor::lib::ResourceId image{};
    ::cwxeditor::lib::Size size{16.0F, 16.0F};
};

/// 時間軸を表示し、現在位置を選択できるようにする。
class TimeBar final : public ::cwxeditor::lib::Canvas,
                      public std::enable_shared_from_this<TimeBar> {
public:
    using Callback = ::cwxeditor::lib::Callback<void()>;
    using CallbackArray = ::cwxeditor::lib::Array<Callback>;

    /// DWTの親子関係に合わせ、親は借用する。
    explicit TimeBar(::cwxeditor::lib::Container* parent = nullptr,
                     std::int32_t style = static_cast<std::int32_t>(STYLE_NONE),
                     const ::cwxeditor::lib::SystemBackend* system = nullptr,
                     ::cwxeditor::lib::GraphicsBackend* graphics = nullptr);
    ~TimeBar() override = default;

    /// ポインター・キーによる選択を確定してか
    /// ら通知する。 Dのデリゲート配列と同じ
    /// く、通知中はスナップショットを使う。
    CallbackArray updateCurrentEvent;

    void setWarning(::cwxeditor::lib::ResourceId image, std::string message,
                    ::cwxeditor::lib::Size image_size = {16.0F, 16.0F});
    void clearWarning();
    [[nodiscard]] const std::string& warning() const noexcept { return warning_; }
    [[nodiscard]] const std::optional<WarningIcon>& warningIcon() const noexcept {
        return warning_icon_;
    }

    void length(Duration value);
    [[nodiscard]] Duration length() const noexcept { return duration_; }
    void current(Duration value);
    [[nodiscard]] Duration current() const noexcept { return current_; }

    /// 長いBGMの位置を確認・移動しやすいよう、Dの100.ppis
    /// より広い時間軸を使う。 高さはDの28.ppisを維持する。
    [[nodiscard]] ::cwxeditor::lib::Point computeSize(std::int32_t width_hint,
                                                       std::int32_t height_hint,
                                                       bool changed) const noexcept;

    void setSystemBackend(const ::cwxeditor::lib::SystemBackend* system) noexcept;
    [[nodiscard]] const ::cwxeditor::lib::SystemBackend* systemBackend() const noexcept {
        return system_;
    }
    void setGraphicsBackend(::cwxeditor::lib::GraphicsBackend* graphics) noexcept;
    [[nodiscard]] ::cwxeditor::lib::GraphicsBackend* graphicsBackend() const noexcept {
        return graphics_;
    }
    void setDrawingContext(::cwxeditor::lib::DrawingContext* context) noexcept;
    [[nodiscard]] ::cwxeditor::lib::DrawingContext* drawingContext() const noexcept {
        return drawing_context_;
    }
    void setDpiScale(float scale) noexcept;
    [[nodiscard]] float dpiScale() const noexcept { return dpi_scale_; }

    /// 共通WidgetにSWT形式のsetFocusはない
    /// ため、ホストのイベントループからフォーカスを渡す。
    void setFocused(bool focused) noexcept;
    [[nodiscard]] bool focused() const noexcept { return focused_; }
    [[nodiscard]] bool moving() const noexcept { return moving_; }
    [[nodiscard]] bool keyMoving() const noexcept { return key_moving_; }

    [[nodiscard]] ::cwxeditor::lib::Container* parent() const noexcept { return parent_; }
    [[nodiscard]] std::int32_t style() const noexcept { return style_; }
    [[nodiscard]] const TimeBarRenderState& renderState() const noexcept {
        return render_state_;
    }

private:
    void onKeyDown(::cwxeditor::lib::KeyEvent& event);
    void onMouseDown(::cwxeditor::lib::PointerEvent& event);
    void onMouseUp(::cwxeditor::lib::PointerEvent& event);
    void onMouseMove(::cwxeditor::lib::PointerEvent& event);
    void onPaint(::cwxeditor::lib::Rect area);

    void redraw();
    void notifyCurrentUpdated();
    void drawRenderState();
    [[nodiscard]] ::cwxeditor::lib::Size textExtent(std::string_view text) const;
    [[nodiscard]] std::string timeLabel(Duration value, bool with_hours) const;
    [[nodiscard]] Duration clampSelection(Duration value) const noexcept;
    [[nodiscard]] std::int64_t seconds(Duration value) const noexcept;
    [[nodiscard]] float positiveScale(float scale) const noexcept;

    ::cwxeditor::lib::Container* parent_{};
    const ::cwxeditor::lib::SystemBackend* system_{};
    ::cwxeditor::lib::GraphicsBackend* graphics_{};
    ::cwxeditor::lib::DrawingContext* drawing_context_{};
    std::int32_t style_{};
    float dpi_scale_{1.0F};

    std::optional<WarningIcon> warning_icon_;
    std::string warning_;
    Duration duration_{kUnsetDuration};
    Duration current_{kUnsetDuration};
    Duration set_current_{kUnsetDuration};
    bool moving_{};
    bool key_moving_{};
    bool focused_{};

    TimeBarRenderState render_state_;
};

}  // namespace cwxeditor::cwx::editor::gui::dwt::timebar
