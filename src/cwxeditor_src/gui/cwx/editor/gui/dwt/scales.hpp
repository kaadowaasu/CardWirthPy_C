#pragma once

// cwx.editor.gui.dwt.scales
// のC++20移植。 SWTの操作部品を共
// 通層で表し、HWND/SWTハンドルでな
// くlibの部品・配置を公開する。 ネイテ
// ィブとヘッドレスで同じ値モデルを使う。

#include "cwxeditor_src/lib/components.hpp"
#include "cwxeditor_src/lib/graphics.hpp"
#include "cwxeditor_src/lib/layout.hpp"
#include "cwxeditor_src/lib/portable.hpp"
#include "cwxeditor_src/lib/system.hpp"
#include "cwxeditor_src/lib/types.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace cwxeditor::cwx::editor::gui::dwt::scales {

/// SWT.READ_ONLYに対応する共通の値
/// 。Scales自身が使うのはこのビットだけで
/// 、 他のスタイルはホストの部品層へ任せる。
inline constexpr std::uint32_t STYLE_NONE = 0U;
inline constexpr std::uint32_t STYLE_READ_ONLY = 1U << 0U;

struct HintMark {
    std::string text;
    float x{};
    float y{};
};

class Scales final : public ::cwxeditor::lib::Panel,
                     public std::enable_shared_from_this<Scales> {
public:
    using String = ::cwxeditor::lib::String;
    using StringArray = ::cwxeditor::lib::Array<String>;
    using IntArray = ::cwxeditor::lib::Array<std::int32_t>;
    using ModEvent = ::cwxeditor::lib::Callback<void()>;
    using ModEventArray = ::cwxeditor::lib::Array<ModEvent>;
    using NumberEditPtr = ::cwxeditor::lib::Owner<::cwxeditor::lib::NumberEdit>;
    using SliderPtr = ::cwxeditor::lib::Owner<::cwxeditor::lib::Slider>;
    using LabelPtr = ::cwxeditor::lib::Owner<::cwxeditor::lib::Label>;
    using CanvasPtr = ::cwxeditor::lib::Owner<::cwxeditor::lib::Canvas>;

    /// SWTの親子の所有関係に合わせ、親は借用する。
    explicit Scales(::cwxeditor::lib::Container* parent = nullptr,
                    std::int32_t style = static_cast<std::int32_t>(STYLE_NONE),
                    const ::cwxeditor::lib::SystemBackend* system = nullptr,
                    ::cwxeditor::lib::GraphicsBackend* graphics = nullptr);
    ~Scales() override = default;

    /// システム・DPI情報を保持する。配置は共通の論理UI
    /// 座標で扱い、 デバイス倍率の適用は描画側が担当する。
    void setSystemBackend(const ::cwxeditor::lib::SystemBackend* system) noexcept;
    [[nodiscard]] const ::cwxeditor::lib::SystemBackend* systemBackend() const noexcept {
        return system_;
    }
    void setGraphicsBackend(::cwxeditor::lib::GraphicsBackend* graphics) noexcept {
        graphics_ = graphics;
        redrawHints();
    }
    [[nodiscard]] ::cwxeditor::lib::GraphicsBackend* graphicsBackend() const noexcept {
        return graphics_;
    }
    void setDpiScale(float scale) noexcept;
    [[nodiscard]] float dpiScale() const noexcept { return dpi_scale_; }

    /// ユーザーの選択・編集後に呼ぶ、値変更の通知。
    ModEventArray modEvent;

    /// 名前ごとに一組のスライダー・スピナーを初期化する。
    void setScales(std::uint32_t step_count, const StringArray& names,
                   std::int32_t page, std::int32_t minimum = 0);

    void setValue(std::size_t index, std::int32_t value);
    void setValues(const IntArray& values);
    [[nodiscard]] std::int32_t getValue(std::size_t index) const;
    [[nodiscard]] IntArray getValues() const;

    void borderlines(const IntArray& lines);
    [[nodiscard]] IntArray borderlines() const { return borderlines_; }

    [[nodiscard]] bool readOnly() const noexcept { return read_only_; }
    [[nodiscard]] ::cwxeditor::lib::Container* parent() const noexcept { return parent_; }
    [[nodiscard]] const std::vector<NumberEditPtr>& spinners() const noexcept { return spinners_; }
    [[nodiscard]] const std::vector<SliderPtr>& scales() const noexcept { return scales_; }
    [[nodiscard]] CanvasPtr hints() const noexcept { return hints_; }
    [[nodiscard]] const std::vector<HintMark>& hintMarks() const noexcept { return hint_marks_; }

private:
    class Selection final {
    public:
        Selection(Scales& owner, std::size_t index, SliderPtr scale,
                  NumberEditPtr spinner);
        void widgetSelected(bool from_scale, double value);
        void modifyText(double value);

    private:
        Scales& owner_;
        std::size_t index_{};
        SliderPtr scale_;
        NumberEditPtr spinner_;
    };

    class PaintHints final {
    public:
        explicit PaintHints(Scales& owner) : owner_(owner) {}
        void paint(const ::cwxeditor::lib::Rect& area);

    private:
        Scales& owner_;
    };

    void notifyModified();
    void redrawHints();
    void updateHintMarks(const ::cwxeditor::lib::Rect& area);
    [[nodiscard]] float textWidth(std::string_view value) const;
    [[nodiscard]] std::int32_t clampValue(std::size_t index,
                                           std::int32_t value) const;

    ::cwxeditor::lib::Container* parent_{};
    const ::cwxeditor::lib::SystemBackend* system_{};
    ::cwxeditor::lib::GraphicsBackend* graphics_{};
    bool read_only_{};
    float dpi_scale_{1.0F};
    std::vector<NumberEditPtr> spinners_;
    std::vector<SliderPtr> scales_;
    std::vector<LabelPtr> labels_;
    CanvasPtr hints_;
    IntArray borderlines_;
    std::vector<std::shared_ptr<Selection>> selections_;
    std::shared_ptr<PaintHints> paint_hints_;
    std::vector<HintMark> hint_marks_;
};

}  // namespace cwxeditor::cwx::editor::gui::dwt::scales
