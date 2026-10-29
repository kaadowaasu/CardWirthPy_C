#pragma once

// 複数アプリをまとめるシェル用の、プラットフォームに依存し
// ない小さなUIインターフェース。 シェルが扱うのはボタン
// ・ラベル・ダイアログの意味であり、HWNDやSDL_Window
// などの ネイティブハンドルには依存しない。各プラットフ
// ォームは状態を持つ部品を ネイティブのコントロールへ対
// 応付け、テストでは同じ部品をヘッドレス実装で使える。

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace cardwirth::ui {

struct Rect {
    float x{};
    float y{};
    float width{};
    float height{};
};

class Widget {
public:
    virtual ~Widget() = default;

    void set_visible(bool value) noexcept { visible_ = value; }
    [[nodiscard]] bool visible() const noexcept { return visible_; }

    void set_enabled(bool value) noexcept { enabled_ = value; }
    [[nodiscard]] bool enabled() const noexcept { return enabled_; }

    void set_rect(Rect value) noexcept { rect_ = value; }
    [[nodiscard]] Rect rect() const noexcept { return rect_; }

    void set_tooltip(std::string value) { tooltip_ = std::move(value); }
    [[nodiscard]] const std::string& tooltip() const noexcept { return tooltip_; }

    // ネイティブ側で必要に応じて実装を上書きできる。 この操作を共通インターフ
    // ェースに含めることで、ダイアログはOSのフォーカスAPIに依存しない。
    virtual void set_focus() noexcept { focused_ = true; }
    [[nodiscard]] bool focused() const noexcept { return focused_; }

protected:
    bool visible_{true};
    bool enabled_{true};
    bool focused_{false};
    Rect rect_{};
    std::string tooltip_{};
};

using WidgetPtr = std::shared_ptr<Widget>;

class Button final : public Widget {
public:
    explicit Button(std::string text = {}) : text_(std::move(text)) {}

    void set_text(std::string value) { text_ = std::move(value); }
    [[nodiscard]] const std::string& text() const noexcept { return text_; }

    void on_click(std::function<void()> callback) { callback_ = std::move(callback); }
    void click() {
        if (enabled_ && visible_ && callback_) callback_();
    }

    void set_default(bool value) noexcept { is_default_ = value; }
    [[nodiscard]] bool is_default() const noexcept { return is_default_; }
    void set_cancel(bool value) noexcept { is_cancel_ = value; }
    [[nodiscard]] bool is_cancel() const noexcept { return is_cancel_; }

private:
    std::string text_{};
    std::function<void()> callback_{};
    bool is_default_{false};
    bool is_cancel_{false};
};

class Label final : public Widget {
public:
    explicit Label(std::string text = {}) : text_(std::move(text)) {}

    void set_text(std::string value) { text_ = std::move(value); }
    [[nodiscard]] const std::string& text() const noexcept { return text_; }

private:
    std::string text_{};
};

class Panel final : public Widget {
public:
    void add(WidgetPtr child) {
        if (child) children_.push_back(std::move(child));
    }
    [[nodiscard]] const std::vector<WidgetPtr>& children() const noexcept { return children_; }

private:
    std::vector<WidgetPtr> children_{};
};

enum class DialogResult {
    None,
    OK,
    Cancel,
    Yes,
    No,
};

class Dialog final : public Widget {
public:
    void set_title(std::string value) { title_ = std::move(value); }
    [[nodiscard]] const std::string& title() const noexcept { return title_; }

    void set_content(WidgetPtr content) { content_ = std::move(content); }
    [[nodiscard]] const WidgetPtr& content() const noexcept { return content_; }

    void show() noexcept { visible_ = true; }
    void close(DialogResult result = DialogResult::None) noexcept {
        result_ = result;
        visible_ = false;
        if (close_callback_) close_callback_(result_);
    }
    [[nodiscard]] DialogResult result() const noexcept { return result_; }
    void on_close(std::function<void(DialogResult)> callback) { close_callback_ = std::move(callback); }

private:
    std::string title_{};
    WidgetPtr content_{};
    DialogResult result_{DialogResult::None};
    std::function<void(DialogResult)> close_callback_{};
};

}  // namespace cardwirth::ui
