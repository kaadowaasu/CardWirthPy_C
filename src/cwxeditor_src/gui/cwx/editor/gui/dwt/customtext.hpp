#pragma once

#include "cwxeditor_src/gui/cwx/editor/gui/dwt/undo.hpp"
#include "cwxeditor_src/lib/clipboard.hpp"
#include "cwxeditor_src/lib/components.hpp"
#include "cwxeditor_src/lib/events.hpp"
#include "cwxeditor_src/lib/graphics.hpp"
#include "cwxeditor_src/lib/portable.hpp"
#include "cwxeditor_src/lib/services.hpp"
#include "cwxeditor_src/lib/types.hpp"
#include "cwxeditor_src/lib/ui_text_layout.hpp"

#include <algorithm>
#include <any>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace cwxeditor::cwx::editor::gui::dwt::customtext {

namespace lib = ::cwxeditor::lib;
namespace undo = ::cwxeditor::cwx::editor::gui::dwt::undo;

using String = lib::String;
using StringView = lib::StringView;
using TextEdit = lib::TextEdit;
using TextEditPtr = lib::Owner<TextEdit>;
using ComboBox = lib::ComboBox;
using ComboBoxPtr = lib::Owner<ComboBox>;
using Parent = lib::Container*;
using GraphicsBackend = lib::GraphicsBackend;
using ClipboardBackend = lib::ClipboardBackend;

/// DのStyledTextとCComboには独立した共通インター
/// フェースを設けない。 この二つの小さな型でTMMの両オーバーロ
/// ードを保ち、対応するlibの文字入力・Comboの状態を使う。
class StyledText : public TextEdit {
public:
    using TextEdit::TextEdit;
};
using StyledTextPtr = lib::Owner<StyledText>;

class CCombo : public ComboBox {
public:
    using ComboBox::ComboBox;

};
using CComboPtr = lib::Owner<CCombo>;

inline constexpr std::int32_t STYLE_NONE = 0;
inline constexpr std::int32_t STYLE_READ_ONLY = 1 << 0;

enum class TextEventType {
    KeyDown,
    KeyUp,
    PointerDown,
    PointerMove,
    PointerUp,
    DoubleClick,
    Modify,
    SelectionChanged,
    FocusIn,
    FocusOut,
};

struct TextEvent {
    TextEventType type{};
    lib::KeyEvent* key{};
    lib::PointerEvent* pointer{};
};

using TextListener = lib::Callback<void(TextEvent&)>;
using TextListenerToken = std::size_t;

[[nodiscard]] String acceleratorText(const lib::KeyEvent& event);
[[nodiscard]] String acceleratorText(lib::Key key);
[[nodiscard]] std::uint32_t convertAccelerator(StringView text);

/// 読み取り専用のアクセスキー入力欄を作る。 共通のキーイベ
/// ントへ通知を接続し、parentがあればそこへ追加する。
[[nodiscard]] TextEditPtr mnemonicText(Parent parent, std::int32_t style,
                                        GraphicsBackend* graphics = nullptr);
inline TextEditPtr mnemonicText(lib::Container& parent, const std::int32_t style,
                                GraphicsBackend* graphics = nullptr) {
    return mnemonicText(&parent, style, graphics);
}

/// 設定画面でキーと修飾キーを取得する入力欄。
class HotKeyField final {
public:
    explicit HotKeyField(Parent parent = nullptr, std::int32_t style = STYLE_NONE,
                         GraphicsBackend* graphics = nullptr);
    ~HotKeyField();

    [[nodiscard]] const TextEditPtr& widget() const noexcept { return char_; }
    [[nodiscard]] TextEditPtr& widget() noexcept { return char_; }
    [[nodiscard]] String acceleratorText() const;
    void accelerator(StringView hotkey);

private:
    TextEditPtr char_{};
    GraphicsBackend* graphics_{};
    void refText(const lib::KeyEvent& event);
};

namespace detail {

[[nodiscard]] lib::Size measureText(GraphicsBackend* graphics,
                                     StringView text,
                                     const lib::FontSpec& font);
[[nodiscard]] float fontHeight(GraphicsBackend* graphics,
                               const lib::FontSpec& font);
[[nodiscard]] std::vector<String> splitLines(StringView text);
[[nodiscard]] std::vector<String> utf8Characters(StringView text);
void replaceSelection(TextEdit& widget, StringView value);
void setText(TextEdit& widget, StringView value);
[[nodiscard]] bool isEditable(const TextEdit& widget) noexcept;

}  // namespace detail

/// 指定された文字セル数を折り返し幅にする文字入力。 Tにはホ
/// ストが提供するlib::TextEdit互換の部品を使う。
template <typename T = TextEdit>
class FixedWidthText final {
    static_assert(std::is_base_of_v<TextEdit, T>,
                  "FixedWidthText requires a TextEdit component");

public:
    FixedWidthText(const lib::FontSpec& font_data, std::int32_t num,
                   Parent parent = nullptr, std::int32_t style = STYLE_NONE,
                   bool word_wrap = false, GraphicsBackend* graphics = nullptr)
        : widget_(std::make_shared<T>()), graphics_(graphics), num_(num) {
        if (parent) parent->add(widget_);
        widget_->multiline = true;
        widget_->wrap = word_wrap || true;
        // Dと同じく、この固定幅の入力欄にはMULTI|WRAPを加え、V_SCROLLは加えない。
        widget_->vertical_scroll = false;
        widget_->read_only = (style & STYLE_READ_ONLY) != 0;
        font(font_data);
    }

    FixedWidthText(const lib::FontSpec& font_data, std::int32_t num,
                   lib::Container& parent, std::int32_t style = STYLE_NONE,
                   bool word_wrap = false, GraphicsBackend* graphics = nullptr)
        : FixedWidthText(font_data, num, &parent, style, word_wrap, graphics) {}

    void num(const std::int32_t value) {
        num_ = value;
        calcWidth();
    }
    [[nodiscard]] std::int32_t num() const noexcept { return num_; }

    void font(const lib::FontSpec& value) {
        font_ = value;
        widget_->set_font(font_);
        calcWidth();
    }
    [[nodiscard]] const lib::FontSpec& font() const noexcept { return font_; }

    void setGraphicsBackend(GraphicsBackend* graphics) noexcept {
        graphics_ = graphics;
        calcWidth();
    }
    [[nodiscard]] GraphicsBackend* graphicsBackend() const noexcept {
        return graphics_;
    }

    [[nodiscard]] const std::shared_ptr<T>& widget() const noexcept {
        return widget_;
    }
    [[nodiscard]] std::shared_ptr<T>& widget() noexcept { return widget_; }
    // クライアント幅は設定された文字幅とする。 共通TextEdit
    // が余白と要求されたスクロールバー領域を加え、外側の寸法を返す。
    [[nodiscard]] lib::Point computeTextBaseSize(std::int32_t line) const;
    [[nodiscard]] String getRRText(bool last_ret = true) const;

    [[nodiscard]] static String toRRText(
        StringView target, std::int32_t num, const lib::FontSpec& font_data,
        bool last_ret = true, GraphicsBackend* graphics = nullptr);
    [[nodiscard]] static String toRRText(StringView target, float width,
                                         GraphicsBackend* graphics,
                                         bool last_ret = true,
                                         const lib::FontSpec& font = {});

    void insert(StringView text) { detail::replaceSelection(*widget_, text); }
    void setText(StringView text) { detail::setText(*widget_, text); }
    [[nodiscard]] String getText() const { return widget_->text; }
    [[nodiscard]] float width() const noexcept { return width_; }

private:
    std::shared_ptr<T> widget_{};
    GraphicsBackend* graphics_{};
    lib::FontSpec font_{};
    float width_{};
    std::int32_t num_{};

    void calcWidth();
};

/// 実測した文字幅がnum文字分を超えたときに通知す
/// る入力欄。 UTF-8のバイト数ではなく、元のGB
/// 形式の全角・半角の扱いに合わせて幅で制限する。
class GBLimitText final {
public:
    using LimitCallback = lib::Action;
    std::vector<LimitCallback> limitEvent;

    GBLimitText(String font, std::int32_t num, Parent parent = nullptr,
                std::int32_t style = STYLE_NONE,
                GraphicsBackend* graphics = nullptr);
    ~GBLimitText();
    GBLimitText(const GBLimitText&)=delete;
    GBLimitText& operator=(const GBLimitText&)=delete;

    [[nodiscard]] const TextEditPtr& widget() const noexcept { return widget_; }
    [[nodiscard]] TextEditPtr& widget() noexcept { return widget_; }
    // SWT.DEFAULT（-1）なら設定済みの幅、または現在の共通の表示行
    // の高さを使う。 他の負の指定は0へ補正する。指定値は文字のクライアント寸
    // 法で、 TextEditが余白とスクロールバー領域を外枠として加える。
    [[nodiscard]] lib::Point computeSize(std::int32_t width_hint,
                                          std::int32_t height_hint) const;
    [[nodiscard]] bool over() const noexcept { return over_; }
    [[nodiscard]] float width() const noexcept { return width_; }
    void setGraphicsBackend(GraphicsBackend* graphics) noexcept;
    void refreshLimit();
    void insert(StringView text);
    void setText(StringView text);
    [[nodiscard]] String getText() const { return widget_->text; }

private:
    bool over_{};
    bool cut_{true};
    TextEditPtr widget_{};
    bool editing_{};
    float width_{};
    std::int32_t num_{};
    lib::FontSpec font_{};
    GraphicsBackend* graphics_{};
    String old_{};
    std::shared_ptr<void> lifetime_{std::make_shared<int>(0)};

    void updateLimit();
};

inline constexpr std::int32_t TMM_T = 0;
inline constexpr std::int32_t TMM_ST = 1;
inline constexpr std::int32_t TMM_C = 2;
inline constexpr std::int32_t TMM_CC = 3;

enum class TMMKind : std::int32_t { Text = TMM_T, StyledText = TMM_ST,
                                    Combo = TMM_C, CCombo = TMM_CC };

/// Dの文字列メニューが受け付ける四つのDWT入力部
/// 品への共通の参照。 弱参照を使い、親が解放した部
/// 品をメニューや履歴が生かし続けないようにする。
class TMM final {
public:
    struct State;

    TMM() = default;
    explicit TMM(TextEditPtr text);
    explicit TMM(StyledTextPtr text);
    explicit TMM(ComboBoxPtr combo);
    explicit TMM(CComboPtr combo);

    static TMM opCall(TextEditPtr text) { return TMM(std::move(text)); }
    static TMM opCall(StyledTextPtr text) { return TMM(std::move(text)); }
    static TMM opCall(ComboBoxPtr combo) { return TMM(std::move(combo)); }
    static TMM opCall(CComboPtr combo) { return TMM(std::move(combo)); }

    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] TMMKind kind() const noexcept;
    [[nodiscard]] String getText() const;
    void setText(StringView value);
    [[nodiscard]] lib::Point getSelection() const;
    void setSelection(lib::Point value);
    [[nodiscard]] bool readOnly() const noexcept;
    [[nodiscard]] bool editable() const noexcept;

    TextListenerToken addListener(TextEventType type, TextListener listener);
    bool removeListener(TextEventType type, TextListenerToken token) noexcept;
    void dispatch(TextEvent& event) const;

    [[nodiscard]] TextEditPtr textWidget() const;
    [[nodiscard]] ComboBoxPtr comboWidget() const;

private:
    std::shared_ptr<State> state_{};

    explicit TMM(std::shared_ptr<State> state) : state_(std::move(state)) {}
};

struct TMAppendData {
    using Read = lib::Callback<std::any(const std::any&)>;
    using Write = lib::Callback<void(const std::any&)>;

    Read read{};
    Write write{};
};

/// 変更リスナーと件数制限付きの文字履歴。lib::ClipboardBackend
/// を使い、 DWTの切り取り・コピー・貼り付
/// け・削除・全選択に対応する操作も提供する。
class TextMenuModify final {
public:
    using CanSaveHistory = lib::Callback<bool()>;

    TextMenuModify(TMM text, CanSaveHistory can_save_history = {},
                   undo::UndoManager* undo_manager = nullptr,
                   TMAppendData append_data = {},
                   std::shared_ptr<ClipboardBackend> clipboard = {});
    TextMenuModify(TMM text, CanSaveHistory can_save_history,
                   undo::UndoManager& undo_manager,
                   TMAppendData append_data = {},
                   std::shared_ptr<ClipboardBackend> clipboard = {})
        : TextMenuModify(std::move(text), std::move(can_save_history),
                         &undo_manager, std::move(append_data),
                         std::move(clipboard)) {}
    ~TextMenuModify();

    [[nodiscard]] bool inProc() const noexcept { return in_proc_; }
    lib::Action selectChanged;
    lib::Action refreshToolBar;

    void reset();
    void modifyText();
    void modifyText(TextEvent& event);

    void setClipboard(std::shared_ptr<ClipboardBackend> clipboard) noexcept {
        clipboard_ = std::move(clipboard);
        buildMenu();
    }
    [[nodiscard]] const std::shared_ptr<ClipboardBackend>& clipboard() const
        noexcept {
        return clipboard_;
    }

    [[nodiscard]] bool canUndo() const noexcept;
    [[nodiscard]] bool canRedo() const noexcept;
    bool undo();
    bool redo();
    [[nodiscard]] bool canCopy() const;
    [[nodiscard]] bool canCut() const;
    [[nodiscard]] bool canPaste() const;
    [[nodiscard]] bool canDelete() const;
    [[nodiscard]] bool canSelectAll() const;
    void copy();
    void cut();
    void paste();
    void erase();
    void selectAll();
    [[nodiscard]] const TMM& text() const noexcept { return text_; }
    [[nodiscard]] std::shared_ptr<lib::Menu> menu() const noexcept {
        return menu_;
    }

private:
    class TextMenuUndo;
    class SelectChanged;

    // SelectChangedは初期化子リストで作成され、このトークンを受
    // け取る。 宣言順によって、リスナーより先にトークンを初期化すること。
    std::shared_ptr<void> lifetime_{std::make_shared<int>(0)};
    bool in_proc_{};
    TMM text_{};
    TMAppendData append_data_{};
    std::any old_append_data_{};
    lib::Point old_selection_{};
    String old_text_{};
    CanSaveHistory can_save_history_{};
    undo::UndoManager* undo_{};
    std::shared_ptr<ClipboardBackend> clipboard_{};
    std::shared_ptr<lib::Menu> menu_{};
    std::shared_ptr<SelectChanged> select_listener_{};
    std::vector<std::pair<TextEventType, TextListenerToken>> listener_records_{};
    std::uint64_t revision_{};
    bool save();
    void emitRefresh();
    void buildMenu();
    [[nodiscard]] bool hasSelection() const;
};

/// createNumberEditorと文字列メニューが使うDWT Commons
/// の一部。 小さく保ち、ネイティブ・ヘッドレスの両方で使えるようにする。
struct Commons {
    lib::Services services{};
    lib::Action refreshToolBar{};
    std::size_t undoMaxEtc{100};

    [[nodiscard]] std::shared_ptr<ClipboardBackend> clipboard() const noexcept {
        return services.clipboard;
    }
    [[nodiscard]] GraphicsBackend* graphics() const noexcept {
        return services.graphics.get();
    }
};

[[nodiscard]] TextEditPtr createNumberEditor(
    Commons comm, Parent parent, std::int32_t style,
    TextMenuModify::CanSaveHistory catch_mod = {});
inline TextEditPtr createNumberEditor(Commons comm, lib::Container& parent,
                                      const std::int32_t style,
                                      TextMenuModify::CanSaveHistory catch_mod = {}) {
    return createNumberEditor(std::move(comm), &parent, style,
                              std::move(catch_mod));
}

template <typename T>
lib::Point FixedWidthText<T>::computeTextBaseSize(const std::int32_t line) const {
    // Dと同じく、計測前に寸法用の描画コンテキストをTextの現在のFontへ合わせる。
    const auto font=widget_->font().value_or(font_);
    // DはGCのFontMetrics.heightを使い、Text.computeSizeで部品の外枠を加える。
    const auto height=detail::fontHeight(graphics_,font);
    const auto content=lib::Size{width_,height*static_cast<float>(std::max(0,line))};
    const auto outer=lib::text_edit_outer_size(*widget_,content);
    return {outer.width,outer.height};
}

template <typename T>
String FixedWidthText<T>::getRRText(const bool last_ret) const {
    return toRRText(widget_->text, width_, graphics_, last_ret, font_);
}

template <typename T>
String FixedWidthText<T>::toRRText(StringView target, const std::int32_t num,
                                   const lib::FontSpec& font_data,
                                   const bool last_ret,
                                   GraphicsBackend* graphics) {
    const auto columns = std::max(0, num);
    // Dの静的な補助関数は半角空白の送り幅を使う。 インスタンスのcalcWidth
    // は、全角空白と表示領域の1pxの余白を使う。
    const auto width=detail::measureText(graphics," ",font_data).width*static_cast<float>(columns);
    return toRRText(target, width, graphics,
                    last_ret, font_data);
}

template <typename T>
String FixedWidthText<T>::toRRText(StringView target, const float width,
                                   GraphicsBackend* graphics,
                                   const bool last_ret,
                                   const lib::FontSpec& font) {
    if (target.empty()) return {};
    // D版より安全にするため、0以下の幅を補正し、 最初の
    // 文字が大きすぎても空行を前置せず、その文字を残す。
    const auto max_width = width > 0.0F ? width : 1.0F;
    std::vector<String> lines;
    for (const auto& line : detail::splitLines(target)) {
        const auto characters=detail::utf8Characters(line);
        // Dと同じく先に行全体を測る。カーニングにより、 個
        // 々の文字幅の合計より行全体が小さくなる場合がある。
        if(detail::measureText(graphics,line,font).width<=max_width){
            lines.push_back(line);
            continue;
        }
        String current;
        float current_width = 0.0F;
        for (const auto& character : characters) {
            const auto character_width =
                detail::measureText(graphics, character, font).width;
            current_width = detail::measureText(graphics, current, font).width;
            if (!current.empty() && current_width + character_width > max_width) {
                lines.push_back(std::move(current));
                current.clear();
                current_width = 0.0F;
            }
            current += character;
            current_width += character_width;
        }
        lines.push_back(std::move(current));
    }

    String result;
    for (std::size_t index = lines.size(); index > 0; --index) {
        if (lines[index - 1].empty()) continue;
        for (std::size_t line = 0; line < index; ++line) {
            result += lines[line];
            if (last_ret || line + 1U < index) result.push_back('\n');
        }
        return result;
    }
    return {};
}

template <typename T>
void FixedWidthText<T>::calcWidth() {
    const auto full = detail::measureText(graphics_, "　", font_).width;
    const auto half = detail::measureText(graphics_, " ", font_).width;
    width_ = full * static_cast<float>(num_ / 2) + 1.0F;
    if ((num_ & 1) != 0) width_ += half;
}

}  // namespace cwxeditor::cwx::editor::gui::dwt::customtext
