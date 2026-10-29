#pragma once

// cwx/editor/gui/dwt/radarspinner.d
// の移植可能なC++20実装。 ラベル
// と数値入力は通常の共通子部品にし、
// レーダーの形はCanvasと描画状
// 態で公開する。 ポインター入力にも
// Editor共通のイベントを使う。

#include "cwxeditor_src/lib/components.hpp"
#include "cwxeditor_src/lib/events.hpp"
#include "cwxeditor_src/lib/graphics.hpp"
#include "cwxeditor_src/lib/types.hpp"
#include "cwxeditor_src/lib/gui.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace cwxeditor::cwx::editor::gui::dwt::radarspinner {

namespace lib = ::cwxeditor::lib;

class RadarSpinner final : public lib::Panel {
public:
    using String = std::string;
    using ModifyHandler = std::function<void(int, int)>;

    /// 元の枠・読み取り専用指定に対応する共通スタイル
    /// 。 特定のツールキットの定数には結び付けない。
    enum Style : std::uint32_t {
        None = 0U,
        Border = 1U << 0U,
        ReadOnly = 1U << 1U,
    };

    /// 有効なレーダー点に描く値の印。
    enum class Toggle {
        SQUARE,
        OVAL,
        OBLIQUE_SQUARE,
        CROSS,
        NOTHING,
    };

    struct ToggleMarker {
        lib::Point center;
        Toggle style{Toggle::OBLIQUE_SQUARE};
    };

    struct RenderState {
        std::vector<lib::Rect> ovals;
        std::vector<std::vector<lib::Point>> polygons;
        std::vector<std::pair<lib::Point, lib::Point>> axes;
        std::vector<lib::Point> activePolygon;
        std::vector<ToggleMarker> markers;
        lib::Color foreground{};
        lib::Color background{};
        int alpha{0xFF};
        int antialias{-1};
    };

    /// ユーザーの編集・ドラッグで部品が変わったときに呼ぶ。
    std::vector<lib::Action> modEvent;

    RadarSpinner(std::shared_ptr<lib::Container> parent = {},
                 std::uint32_t style = None,
                 std::shared_ptr<lib::GuiBackend> gui = {});
    explicit RadarSpinner(std::uint32_t style) : RadarSpinner({}, style) {}
    ~RadarSpinner() override { callback_lifetime_.reset(); spinner_lifetime_.reset(); }

private:
    std::weak_ptr<lib::GuiBackend> gui_;
    class SpnListener {
    public:
        SpnListener(RadarSpinner& owner, int index) : owner_(&owner), index_(index) {}
        void handleEvent(int value);

    private:
        RadarSpinner* owner_{};
        int index_{};
    };

    std::vector<ModifyHandler> mod_handler_{};
    std::vector<std::vector<lib::Point>> toggles_{};
    std::vector<lib::Rect> ovals_{};
    std::vector<std::vector<int>> polygons_{};
    std::vector<std::shared_ptr<lib::Panel>> components_{};
    lib::Size max_size_{};
    std::vector<std::shared_ptr<lib::Label>> labels_{};
    std::vector<lib::WidgetPtr> spinners_{};
    std::vector<String> names_{};
    std::uint32_t step_count_{};
    std::size_t parameter_count_{};
    int minimum_{};
    std::ptrdiff_t on_drag_{-1};
    std::uint32_t oval_step_{1};
    static constexpr int TOGGLE_SIZE = 5;
    static constexpr int TOGGLE_CATCH_SIZE = 11;
    static constexpr int MARGIN = 10;
    [[nodiscard]] static int toggleSize();
    [[nodiscard]] static int toggleCatchSize();
    [[nodiscard]] static int figureMargin();
    int antialias_{-1};
    bool side_{true};
    bool oval_{};
    std::vector<int> borderlines_{};
    int oval_width_{-1};
    int oval_height_{-1};
    lib::Color oval_background_{255, 255, 255, 255};
    lib::Color oval_foreground_{0, 0, 0, 255};
    bool read_only_{};
    Toggle toggle_style_{Toggle::OBLIQUE_SQUARE};
    int alpha_{159};
    bool modified_{};
    lib::Size layout_size_{};
    lib::Action mod_redraw_{};
    std::vector<std::unique_ptr<SpnListener>> listeners_{};
    std::vector<int> values_{};
    std::shared_ptr<lib::Canvas> canvas_{};
    std::shared_ptr<lib::Container> parent_{};
    RenderState render_state_{};
    // Dの部品はGC管理だが、C++では子や通知が所有者より長く残る場合が
    // ある。 所有者の寿命と、再構成した入力部品の世代の両方を無効化する。
    std::shared_ptr<int> callback_lifetime_{std::make_shared<int>(0)};
    std::shared_ptr<int> spinner_lifetime_{std::make_shared<int>(0)};

    void notifyModified();
    void updateRenderState();
    void redraw();
    void checkCursor(float x, float y);
    std::ptrdiff_t cursorFromPos(float x, float y) const;
    static unsigned figure(int value, int minus_mark_length = 1,
                           int radix = 10);
    void calcMaxSize();
    static constexpr double MAX_GAP = 1.5;
    static constexpr double MAX_GAP_3H = 0.8;
    [[nodiscard]] std::vector<lib::PreparedChildBounds> prepareChildLayout(lib::Rect bounds);
    bool isBorderline(int value) const;
    double nPos(std::size_t index) const;
    lib::Rect computeBounds(int width, int height) const;
    void setValueFromSpinner(std::size_t index, int value);
    void updateReadOnlyLabel(std::size_t index, int value);
    void installPointerHandlers();
    void paint(lib::DrawingContext& context, lib::Rect area);

public:
    /// 点の数・ラベル・値の範囲・子の入力欄を設定する。
    void setRadar(std::uint32_t step, const std::vector<String>& names,
                  int minimum = 0);

    [[nodiscard]] bool checkCursorPosition(float x, float y) {
        checkCursor(x, y);
        return cursorFromPos(x, y) >= 0;
    }

    [[nodiscard]] bool __unittest_L385_C4() const;

    void setValue(std::size_t index, int value);
    void setValues(const std::vector<int>& values);
    [[nodiscard]] int getValue(std::size_t index) const;
    [[nodiscard]] std::vector<int> getValues() const;
    [[nodiscard]] const std::vector<String>& names() const noexcept { return names_; }
    [[nodiscard]] std::size_t paramCount() const noexcept { return parameter_count_; }
    [[nodiscard]] int step() const noexcept { return static_cast<int>(step_count_); }
    [[nodiscard]] int minimum() const noexcept { return minimum_; }

    void lineStep(std::uint32_t step);
    [[nodiscard]] std::uint32_t lineStep() const noexcept { return oval_step_; }
    void antialias(int value);
    [[nodiscard]] int antialias() const noexcept { return antialias_; }
    void sideMode(bool value);
    [[nodiscard]] bool sideMode() const noexcept { return side_; }
    void oval(bool value);
    [[nodiscard]] bool oval() const noexcept { return oval_; }
    void borderlines(const std::vector<int>& lines);
    [[nodiscard]] const std::vector<int>& borderlines() const noexcept {
        return borderlines_;
    }
    void toggleStyle(Toggle value);
    [[nodiscard]] Toggle toggleStyle() const noexcept { return toggle_style_; }

    void setRadarSize(int width, int height);
    [[nodiscard]] lib::Point getRadarSize() const noexcept {
        return {static_cast<float>(oval_width_), static_cast<float>(oval_height_)};
    }
    void setRadarColor(lib::Color foreground, lib::Color background);
    [[nodiscard]] lib::Color radarForeground() const noexcept { return oval_foreground_; }
    [[nodiscard]] lib::Color radarBackground() const noexcept { return oval_background_; }
    void alpha(int value);
    [[nodiscard]] int alpha() const noexcept { return alpha_; }
    void addModifyHandler(ModifyHandler handler);

    [[nodiscard]] lib::Size computeSize(int width_hint = -1,
                                        int height_hint = -1) const;
    [[nodiscard]] lib::Size computeSize(int width_hint, int height_hint,
                                        bool changed) const;
    void set_font(std::optional<lib::FontSpec> font) override;
    void set_foreground(std::optional<lib::Color> color) noexcept override;
    void setFont(std::optional<lib::FontSpec> font);
    void setForeground(lib::Color color);
    void resize();
    [[nodiscard]] const RenderState& renderState() const noexcept {
        return render_state_;
    }
    [[nodiscard]] bool readOnly() const noexcept { return read_only_; }
    [[nodiscard]] const std::vector<lib::WidgetPtr>& controls() const noexcept {
        return spinners_;
    }
};

}  // namespace cwxeditor::cwx::editor::gui::dwt::radarspinner
