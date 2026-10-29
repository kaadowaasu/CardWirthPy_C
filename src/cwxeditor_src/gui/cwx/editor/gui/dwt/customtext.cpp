#include "cwxeditor_src/lib/window_host.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/customtext.hpp"
#include "cwxeditor_src/lib/ui_actions.hpp"
#include "cwxeditor_src/lib/ui_combo_edit.hpp"
#include "cwxeditor_src/core/cwx/utils.hpp"
#include "cw/lib/encoding.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace cwxeditor::cwx::editor::gui::dwt::customtext {

namespace {

constexpr std::uint32_t MOD_SHIFT = 1U << 24U;
constexpr std::uint32_t MOD_CONTROL = 1U << 25U;
constexpr std::uint32_t MOD_ALT = 1U << 26U;
constexpr std::uint32_t MOD_META = 1U << 27U;

[[nodiscard]] std::uint32_t modifierBits(const lib::Modifiers modifiers) noexcept {
    const auto value = static_cast<std::uint32_t>(modifiers);
    std::uint32_t result = 0U;
    if ((value & static_cast<std::uint32_t>(lib::Modifiers::Shift)) != 0U)
        result |= MOD_SHIFT;
    if ((value & static_cast<std::uint32_t>(lib::Modifiers::Control)) != 0U)
        result |= MOD_CONTROL;
    if ((value & static_cast<std::uint32_t>(lib::Modifiers::Alt)) != 0U)
        result |= MOD_ALT;
    if ((value & static_cast<std::uint32_t>(lib::Modifiers::Meta)) != 0U)
        result |= MOD_META;
    return result;
}

[[nodiscard]] lib::Modifiers parseModifiers(const std::uint32_t bits) noexcept {
    auto result = lib::Modifiers::None;
    if ((bits & MOD_SHIFT) != 0U) result = result | lib::Modifiers::Shift;
    if ((bits & MOD_CONTROL) != 0U) result = result | lib::Modifiers::Control;
    if ((bits & MOD_ALT) != 0U) result = result | lib::Modifiers::Alt;
    if ((bits & MOD_META) != 0U) result = result | lib::Modifiers::Meta;
    return result;
}

[[nodiscard]] String lower(StringView value) {
    String result;
    result.reserve(value.size());
    for (const auto character : value) {
        result.push_back(static_cast<char>(
            std::tolower(static_cast<unsigned char>(character))));
    }
    return result;
}

[[nodiscard]] String trim(StringView value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())))
        value.remove_prefix(1);
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
        value.remove_suffix(1);
    return String(value);
}

[[nodiscard]] lib::Key keyFromToken(StringView value) {
    const auto token = lower(trim(value));
    if (token.size() == 1U) {
        const auto character = token.front();
        if (character >= 'a' && character <= 'z') {
            return static_cast<lib::Key>(static_cast<int>(lib::Key::A) +
                                         character - 'a');
        }
        if (character >= '0' && character <= '9') {
            return static_cast<lib::Key>(static_cast<int>(lib::Key::Digit0) +
                                         character - '0');
        }
        if (character == ' ') return lib::Key::Space;
    }

    if (token == "backspace" || token == "bs") return lib::Key::Backspace;
    if (token == "enter" || token == "return" || token == "cr")
        return lib::Key::Enter;
    if (token == "delete" || token == "del") return lib::Key::Delete;
    if (token == "escape" || token == "esc") return lib::Key::Escape;
    if (token == "tab") return lib::Key::Tab;
    if (token == "space") return lib::Key::Space;
    if (token == "arrow_up" || token == "arrowup" || token == "up")
        return lib::Key::Up;
    if (token == "arrow_down" || token == "arrowdown" || token == "down")
        return lib::Key::Down;
    if (token == "arrow_left" || token == "arrowleft" || token == "left")
        return lib::Key::Left;
    if (token == "arrow_right" || token == "arrowright" || token == "right")
        return lib::Key::Right;
    if (token == "page_up" || token == "pageup") return lib::Key::PageUp;
    if (token == "page_down" || token == "pagedown") return lib::Key::PageDown;
    if (token == "home") return lib::Key::Home;
    if (token == "end") return lib::Key::End;
    if (token == "insert") return lib::Key::Insert;
    if (token.size() >= 2U && token.front() == 'f') {
        try {
            const auto number = std::stoi(token.substr(1));
            if (number >= 1 && number <= 15) {
                return static_cast<lib::Key>(static_cast<int>(lib::Key::F1) +
                                             number - 1);
            }
        } catch (const std::exception&) {
        }
    }
    return lib::Key::Unknown;
}

[[nodiscard]] std::uint32_t parseAcceleratorBits(StringView value) {
    std::uint32_t modifiers = 0U;
    StringView key = value;
    while (true) {
        const auto plus = key.find('+');
        if (plus == StringView::npos) break;
        const auto part = lower(trim(key.substr(0, plus)));
        if (part == "control" || part == "ctrl") modifiers |= MOD_CONTROL;
        else if (part == "shift") modifiers |= MOD_SHIFT;
        else if (part == "alt") modifiers |= MOD_ALT;
        else if (part == "command" || part == "cmd" || part == "meta")
            modifiers |= MOD_META;
        key.remove_prefix(plus + 1U);
    }
    const auto parsed_key = keyFromToken(key);
    return modifiers | static_cast<std::uint32_t>(parsed_key);
}

[[nodiscard]] lib::KeyEvent eventFromAccelerator(StringView value) {
    const auto tab = value.rfind('\t');
    value = tab == StringView::npos ? value : value.substr(tab + 1U);
    return lib::parse_menu_shortcut(value);
}

[[nodiscard]] String keyName(const lib::Key key) {
    switch (key) {
    case lib::Key::Backspace: return "Backspace";
    case lib::Key::Tab: return "Tab";
    case lib::Key::Enter: return "Enter";
    case lib::Key::Escape: return "Esc";
    case lib::Key::Space: return "Space";
    case lib::Key::Insert: return "Insert";
    case lib::Key::Delete: return "Delete";
    case lib::Key::Home: return "Home";
    case lib::Key::End: return "End";
    case lib::Key::PageUp: return "Page_Up";
    case lib::Key::PageDown: return "Page_Down";
    case lib::Key::Left: return "Arrow_Left";
    case lib::Key::Right: return "Arrow_Right";
    case lib::Key::Up: return "Arrow_Up";
    case lib::Key::Down: return "Arrow_Down";
    case lib::Key::F1: return "F1";
    case lib::Key::F2: return "F2";
    case lib::Key::F3: return "F3";
    case lib::Key::F4: return "F4";
    case lib::Key::F5: return "F5";
    case lib::Key::F6: return "F6";
    case lib::Key::F7: return "F7";
    case lib::Key::F8: return "F8";
    case lib::Key::F9: return "F9";
    case lib::Key::F10: return "F10";
    case lib::Key::F11: return "F11";
    case lib::Key::F12: return "F12";
    case lib::Key::F13: return "F13";
    case lib::Key::F14: return "F14";
    case lib::Key::F15: return "F15";
    default: break;
    }

    const auto value = static_cast<int>(key);
    if (value >= static_cast<int>(lib::Key::A) &&
        value <= static_cast<int>(lib::Key::Z)) {
        return String(1, static_cast<char>('A' + value -
                                           static_cast<int>(lib::Key::A)));
    }
    if (value >= static_cast<int>(lib::Key::Digit0) &&
        value <= static_cast<int>(lib::Key::Digit9)) {
        return String(1, static_cast<char>('0' + value -
                                           static_cast<int>(lib::Key::Digit0)));
    }
    return {};
}

[[nodiscard]] String keyText(const lib::KeyEvent& event) {
    auto result = keyName(event.key);
    if (result.empty()) result = event.printable;
    return result;
}

[[nodiscard]] bool hasModifier(const lib::Modifiers modifiers,
                                const lib::Modifiers bit) noexcept {
    return (static_cast<std::uint32_t>(modifiers) &
            static_cast<std::uint32_t>(bit)) != 0U;
}

[[nodiscard]] std::pair<std::size_t, std::size_t> selectionBounds(
    const lib::Point point, const std::size_t length) noexcept {
    const auto start = point.x < 0.0F
                           ? 0U
                           : std::min(length, static_cast<std::size_t>(point.x));
    const auto end = point.y < 0.0F
                         ? 0U
                         : std::min(length, static_cast<std::size_t>(point.y));
    return {std::min(start, end), std::max(start, end)};
}

[[nodiscard]] std::size_t selectionOffset(const float value,
                                          const std::size_t length) noexcept {
    if (value <= 0.0F) return 0U;
    return std::min(length, static_cast<std::size_t>(value));
}

void setNativeEditSelection(const StringView text, std::size_t& selection_start,
                            std::size_t& selection_length,
                            bool& selection_reversed,
                            const bool multiline,
                            const lib::Point point) noexcept {
    const auto old_start = std::min(selection_start, text.size());
    const auto old_end = old_start +
        std::min(selection_length, text.size() - old_start);
    if (point.x == -1.0F) {
        // Text.EM_SETSELと編集可能なCombo.CB_SETEDITSEL
        // は、始点の-1で範囲を解除する。ネイティブ
        // EDITは単一行なら以前の上限位置、複数行
        // ならアクティブ側（逆向きなら下限、順方向な
        // ら上限）に畳む。二つ目の端点は無視する。
        selection_start = multiline && selection_reversed ? old_start : old_end;
        selection_length = 0U;
        selection_reversed = false;
        return;
    }
    const auto start = selectionOffset(point.x, text.size());
    const auto end = point.y == -1.0F
                         ? text.size()
                         : selectionOffset(point.y, text.size());
    selection_start = std::min(start, end);
    selection_length = std::max(start, end) - selection_start;
    // 単一行EDITと編集可能なComboは、アクティブ側を上限へ正
    // 規化する。複数行EDITは端点の向きを保持する。この確認処理は
    // ネイティブEDITのスタイル動作を反映し、DWTやUIモデルと
    // 同じ扱いにする。StyledTextは自身の範囲制限を使う。
    selection_reversed = multiline && start > end;
}

[[nodiscard]] bool isCrLfInteriorOffset(const StringView text,
                                         const std::int64_t offset) noexcept {
    if (offset <= 0 || offset >= static_cast<std::int64_t>(text.size()))
        return false;
    const auto index = static_cast<std::size_t>(offset);
    return text[index - 1U] == '\r' && text[index] == '\n';
}

void setStyledTextSelection(TextEdit& widget, const lib::Point point) {
    // DWTのStyledTextは、開始位置を制限する前にlength=end-start
    // を計算する。Win32のTextの直接のEM_SETSELの番兵値とは意図的に異なる。
    const auto requested_start = static_cast<std::int64_t>(point.x);
    const auto requested_end = static_cast<std::int64_t>(point.y);
    const auto text_length = static_cast<std::int64_t>(widget.text.size());
    const auto start = std::clamp(requested_start, std::int64_t{0}, text_length);
    auto selection_length = requested_end - requested_start;
    const auto end = start + selection_length;
    if (end < 0) selection_length = -start;
    else if (end > text_length) selection_length = text_length - start;
    const auto adjusted_end = start + selection_length;
    // DWTは調整後の範囲端をisLineDelimiter()
    // で検査する。Win32では選択でCRLFの途中を
    // 分断できない。C++はUTF-8のバイト列として
    // 保存するため、CRとLFの間の境界に対応する。
    if (isCrLfInteriorOffset(widget.text, start) ||
        isCrLfInteriorOffset(widget.text, adjusted_end))
        throw std::invalid_argument(
            "StyledText selection endpoint splits a CRLF delimiter");
    const auto anchor_or_low = start + std::min<std::int64_t>(selection_length, 0);
    const auto high_or_caret = start + std::max<std::int64_t>(selection_length, 0);
    widget.selection_start = static_cast<std::size_t>(anchor_or_low);
    widget.selection_length = static_cast<std::size_t>(high_or_caret - anchor_or_low);
    widget.selection_reversed = widget.selection_length != 0U && selection_length < 0;
}

}  // namespace

String acceleratorText(const lib::Key key) {
    return keyName(key);
}

String acceleratorText(const lib::KeyEvent& event) {
    auto modifiers = event.modifiers;
    switch (event.key) {
    case lib::Key::Control: modifiers = modifiers | lib::Modifiers::Control; break;
    case lib::Key::Shift: modifiers = modifiers | lib::Modifiers::Shift; break;
    case lib::Key::Alt: modifiers = modifiers | lib::Modifiers::Alt; break;
    case lib::Key::Meta: modifiers = modifiers | lib::Modifiers::Meta; break;
    default: break;
    }
    String result;
    const auto put = [&result](StringView value) {
        if (value.empty()) return;
        if (!result.empty()) result += " + ";
        result += value;
    };
    if (hasModifier(modifiers, lib::Modifiers::Control)) put("Ctrl");
    if (hasModifier(modifiers, lib::Modifiers::Shift)) put("Shift");
    if (hasModifier(modifiers, lib::Modifiers::Alt)) put("Alt");
    if (hasModifier(modifiers, lib::Modifiers::Meta)) put("Command");
    put(keyText(event));
    return result;
}

std::uint32_t convertAccelerator(const StringView text) {
    const auto tab = text.rfind('\t');
    return parseAcceleratorBits(tab == StringView::npos ? text
                                                        : text.substr(tab + 1U));
}

TextEditPtr mnemonicText(const Parent parent, const std::int32_t style,
                         GraphicsBackend* graphics) {
    (void)style;
    (void)graphics;
    auto text = std::make_shared<TextEdit>();
    text->read_only = true;
    if (parent) parent->add(text);
    const std::weak_ptr<TextEdit> weak = text;
    text->on_key_down = [weak](lib::KeyEvent& event) {
        if (const auto target = weak.lock()) {
            (void)lib::set_text_value(*target, keyText(event),
                                      lib::TextValueOrigin::Programmatic);
            // DWTのText.setTextはWM_SETTEXTを使い、
            // 読み取り専用EDITの選択を[0,0]へ戻す。ユーザーが明示
            // 的に文字を選ぶまで、共通コントロールのコピー可否も合わせる。
            target->selection_start = 0U;
            target->selection_length = 0U;
            event.handled = true;
        }
    };
    return text;
}

HotKeyField::HotKeyField(const Parent parent, const std::int32_t style,
                         GraphicsBackend* graphics)
    : char_(mnemonicText(parent, style, graphics)), graphics_(graphics) {
    char_->on_key_down = [this](lib::KeyEvent& event) {
        refText(event);
        event.handled = true;
    };
}

HotKeyField::~HotKeyField() {
    if (char_) char_->on_key_down = {};
}

String HotKeyField::acceleratorText() const {
    String result = char_ ? char_->text : String{};
    std::size_t position = 0U;
    while ((position = result.find(" + ", position)) != String::npos) {
        result.replace(position, 3U, "+");
        ++position;
    }
    return result;
}

void HotKeyField::accelerator(const StringView hotkey) {
    refText(eventFromAccelerator(hotkey));
}

void HotKeyField::refText(const lib::KeyEvent& event) {
    if (!char_) return;
    (void)lib::set_text_value(*char_, customtext::acceleratorText(event),
                              lib::TextValueOrigin::Programmatic);
    char_->selection_start = 0U;
    char_->selection_length = char_->text.size();
}

namespace detail {

lib::Size measureText(GraphicsBackend* graphics, const StringView text,
                      const lib::FontSpec& font) {
    if (graphics) {
        const auto measured = graphics->measure_text(text, font, 0.0F);
        // ヘッドレスのバックエンドは、フォントの計測値がないこと
        // を{0,0}で示す。取得不能として扱い、ヘッドレスの
        // サービスでも共通の一定の代替計測を使えるようにする。
        if (std::isfinite(measured.width) && measured.width > 0.0F &&
            std::isfinite(measured.height) && measured.height > 0.0F) {
            return measured;
        }
    }
    const auto point_size = font.points > 0.0F ? font.points : 10.0F;
    // 意図的なヘッドレス用の代替。Dには常にネイティブGCがあるが、フォント
    // プロバイダーなしのセル推定は、プロポーショナルフォントの実測ではない。
    float width = 0.0F;
    for (const auto& character : utf8Characters(text)) {
        const auto half_kana=character.size()==3U &&
            static_cast<unsigned char>(character[0])==0xefU &&
            static_cast<unsigned char>(character[1])==0xbdU &&
            static_cast<unsigned char>(character[2])>=0xa1U;
        const auto half_kana2=character.size()==3U &&
            static_cast<unsigned char>(character[0])==0xefU &&
            static_cast<unsigned char>(character[1])==0xbeU &&
            static_cast<unsigned char>(character[2])<=0x9fU;
        const auto full_width = !half_kana && !half_kana2 && character.size() > 1U &&
                                static_cast<unsigned char>(character.front()) >= 0xE0U;
        width += (full_width ? 14.0F : 7.0F) * point_size / 10.0F;
    }
    return {width, point_size * 1.2F};
}

float fontHeight(GraphicsBackend* graphics, const lib::FontSpec& font) {
    if (graphics) {
        const auto metrics=graphics->font_metrics(font);
        if (std::isfinite(metrics.height) && metrics.height>0.0F)
            return metrics.height;
    }
    const auto measured=measureText(graphics,"M",font);
    if (std::isfinite(measured.height) && measured.height>0.0F)
        return measured.height;
    const auto points=std::isfinite(font.points) && font.points>0.0F
        ? font.points : 10.0F;
    return points*1.2F;
}

std::vector<String> splitLines(const StringView text) {
    return ::cwxeditor::cwx::utils::splitLines(text);
}

std::vector<String> utf8Characters(const StringView text) {
    // DのtoUTF32は不正列を拒否する。過長形式、サロゲ
    // ート、範囲外も含め、一つの共通符号化処理で検証する。
    (void)::cw::lib::encoding::decode_utf8(
        ::cw::lib::Bytes(text.begin(),text.end()),::cw::lib::encoding::ErrorPolicy::Strict,false);
    std::vector<String> result;
    for (std::size_t index = 0U; index < text.size();) {
        const auto lead = static_cast<unsigned char>(text[index]);
        std::size_t length = 1U;
        if ((lead & 0x80U) == 0U) length = 1U;
        else if ((lead & 0xE0U) == 0xC0U) length = 2U;
        else if ((lead & 0xF0U) == 0xE0U) length = 3U;
        else if ((lead & 0xF8U) == 0xF0U) length = 4U;
        if (index + length > text.size()) length = 1U;
        bool valid = length > 1U;
        for (std::size_t offset = 1U; valid && offset < length; ++offset) {
            valid = (static_cast<unsigned char>(text[index + offset]) & 0xC0U) ==
                    0x80U;
        }
        if (!valid) length = 1U;
        result.emplace_back(text.substr(index, length));
        index += length;
    }
    return result;
}

void replaceSelection(TextEdit& widget, const StringView value) {
    (void)lib::replace_text_selection(widget,value);
}

void setText(TextEdit& widget, const StringView value) {
    const String next(value);
    const bool same_value = widget.text == next;
    widget.selection_start = 0U;
    widget.selection_length = 0U;
    const bool changed = lib::set_text_value(
        widget, next, lib::TextValueOrigin::Programmatic);
    if (!changed && same_value) {
        // DWTのText.setTextとStyledText.setTextは、受理し
        // た値が同じでもModifyを通知する。ComboのTMMは別の設定処理を使う。
        const auto notify = widget.on_changed;
        const auto published = widget.text;
        if (notify) notify(published);
    }
}

bool isEditable(const TextEdit& widget) noexcept { return !widget.read_only; }

}  // namespace detail

GBLimitText::GBLimitText(String font, const std::int32_t num, const Parent parent,
                         const std::int32_t style, GraphicsBackend* graphics)
    : widget_(std::make_shared<TextEdit>()), num_(std::max<std::int32_t>(0, num)),
      graphics_(graphics) {
    widget_->read_only = (style & STYLE_READ_ONLY) != 0;
    font_.family = std::move(font);
    font_.points = 10.0F;
    widget_->set_font(font_);
    if (parent) parent->add(widget_);
    width_ = detail::measureText(graphics_, " ", font_).width *
             static_cast<float>(num_);
    const std::weak_ptr<void> life=lifetime_;
    widget_->on_changed = [this,life](StringView) { if(!life.expired())updateLimit(); };
    updateLimit();
}
GBLimitText::~GBLimitText(){lifetime_.reset();if(widget_)widget_->on_changed={};}

lib::Point GBLimitText::computeSize(const std::int32_t width_hint,
                                    const std::int32_t height_hint) const {
    const auto width=width_hint==-1 ? width_
        : static_cast<float>(std::max(0,width_hint));
    const auto font=widget_->font().value_or(font_);
    float height{};
    if (height_hint==-1) {
        const auto line_height=detail::fontHeight(graphics_,font);
        const auto measure=[&](const StringView text) {
            return detail::measureText(graphics_,text,font).width;
        };
        const auto lines=lib::layout_text_lines(*widget_,width,measure);
        height=line_height*static_cast<float>(std::max<std::size_t>(1U,lines.size()));
    } else {
        height=static_cast<float>(std::max(0,height_hint));
    }
    const auto outer=lib::text_edit_outer_size(*widget_,{width,height});
    return {outer.width,outer.height};
}

void GBLimitText::setGraphicsBackend(GraphicsBackend* graphics) noexcept {
    const std::weak_ptr<void> life=lifetime_;
    graphics_ = graphics;
    const auto count=num_;
    const auto font=font_;
    const auto measured=detail::measureText(graphics_," ",font).width;
    if(life.expired())return;
    width_=measured*static_cast<float>(count);
    updateLimit();
}

void GBLimitText::refreshLimit() { updateLimit(); }

void GBLimitText::insert(const StringView text) {
    detail::replaceSelection(*widget_, text);
}

void GBLimitText::setText(const StringView text) {
    const std::weak_ptr<void> life=lifetime_;
    detail::setText(*widget_, text);
    if(life.expired())return;
    old_=widget_->text;
}

void GBLimitText::updateLimit() {
    const std::weak_ptr<void> life=lifetime_;
    const auto text=widget_->text;
    const auto threshold=width_;
    const auto font=font_;
    const auto measured = detail::measureText(graphics_, text, font);
    if(life.expired())return;
    const auto current = measured.width > threshold;
    if (current != over_) {
        over_ = current;
        const auto callbacks = limitEvent;
        for (const auto& callback : callbacks) {
            if (callback) callback();
            if(life.expired())return;
        }
    }
    old_ = widget_->text;
}

struct TMM::State {
    using TextReference = std::variant<std::weak_ptr<TextEdit>,
                                      std::weak_ptr<StyledText>,
                                      std::weak_ptr<ComboBox>,
                                      std::weak_ptr<CCombo>>;

    struct ListenerEntry {
        TextListenerToken token{};
        TextListener listener{};
    };

    TextReference reference;
    TMMKind type{TMMKind::Text};
    bool callbacks_attached{};
    TextListenerToken next_token{1U};
    std::unordered_map<TextEventType, std::vector<ListenerEntry>> listeners;

    [[nodiscard]] TextEditPtr textWidget() const {
        return std::visit(
            [](const auto& value) -> TextEditPtr {
                using Value = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<Value, std::weak_ptr<TextEdit>>)
                    return value.lock();
                else if constexpr (std::is_same_v<Value, std::weak_ptr<StyledText>>)
                    return std::static_pointer_cast<TextEdit>(value.lock());
                else
                    return {};
            },
            reference);
    }

    [[nodiscard]] ComboBoxPtr comboWidget() const {
        return std::visit(
            [](const auto& value) -> ComboBoxPtr {
                using Value = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<Value, std::weak_ptr<ComboBox>>)
                    return value.lock();
                else if constexpr (std::is_same_v<Value, std::weak_ptr<CCombo>>)
                    return std::static_pointer_cast<ComboBox>(value.lock());
                else
                    return {};
            },
            reference);
    }

    [[nodiscard]] String getText() const {
        if (const auto widget = textWidget()) return widget->text;
        if (const auto combo = comboWidget()) {
            if (type == TMMKind::Combo && !combo->editable) {
                if (combo->selected_index &&
                    *combo->selected_index < combo->items.size())
                    return combo->items[*combo->selected_index].text;
                return {};
            }
            return lib::combo_display_text(*combo);
        }
        return {};
    }

    [[nodiscard]] lib::Point getSelection() const {
        if (const auto widget = textWidget()) {
            return {static_cast<float>(widget->selection_start),
                    static_cast<float>(widget->selection_start +
                                       widget->selection_length)};
        }
        const auto combo = comboWidget();
        if (!combo) return {};
        if (type == TMMKind::Combo && !combo->editable) {
            const auto text = getText();
            return {0.0F, static_cast<float>(text.size())};
        }
        if (type == TMMKind::CCombo) {
            const auto ccombo = std::dynamic_pointer_cast<CCombo>(combo);
            if (ccombo) {
                return {static_cast<float>(ccombo->selection_start),
                        static_cast<float>(ccombo->selection_start +
                                           ccombo->selection_length)};
            }
        }
        if (!combo->edit_selection) {
            if (combo->selected_index &&
                *combo->selected_index < combo->items.size()) {
                return {0.0F, static_cast<float>(combo->edit_text.size())};
            }
            return {};
        }
        return {static_cast<float>(combo->edit_selection->first),
                static_cast<float>(combo->edit_selection->second)};
    }

    void emit(TextEvent& event) {
        const auto found = listeners.find(event.type);
        if (found == listeners.end()) return;
        const auto callbacks = found->second;
        for (const auto& entry : callbacks) {
            // DのGC所有リスナーに対応する追加の寿命保護。通知中に
            // 削除された監視先は、取得済みの一覧にいても無効にする。
            const auto current=listeners.find(event.type);
            if(current==listeners.end())break;
            const auto live=std::find_if(current->second.begin(),current->second.end(),
                [&entry](const auto& peer){return peer.token==entry.token;});
            if(live!=current->second.end() && entry.listener)entry.listener(event);
        }
    }

    template <typename Widget>
    void attachTextCallbacks(const std::shared_ptr<Widget>& widget,
                             const std::weak_ptr<State>& weak) {
        if constexpr (std::is_base_of_v<TextEdit, Widget>) {
            auto old_changed = widget->on_changed;
            widget->on_changed = [weak, old_changed](StringView value) {
                if (old_changed) old_changed(value);
                if (const auto state = weak.lock()) {
                    TextEvent event{TextEventType::Modify};
                    state->emit(event);
                }
            };
        }
        auto old_key_down = widget->on_key_down;
        widget->on_key_down = [weak, old_key_down](lib::KeyEvent& value) {
            if (old_key_down) old_key_down(value);
            if (const auto state = weak.lock()) {
                TextEvent event{TextEventType::KeyDown, &value};
                state->emit(event);
            }
        };
        auto old_key_up = widget->on_key_up;
        widget->on_key_up = [weak, old_key_up](lib::KeyEvent& value) {
            if (old_key_up) old_key_up(value);
            if (const auto state = weak.lock()) {
                TextEvent event{TextEventType::KeyUp, &value};
                state->emit(event);
            }
        };
        auto old_pointer = widget->on_pointer;
        widget->on_pointer = [weak, old_pointer](lib::PointerEvent& value) {
            if (old_pointer) old_pointer(value);
            if (const auto state = weak.lock()) {
                TextEventType type = TextEventType::PointerMove;
                if (value.action == lib::PointerAction::Down)
                    type = TextEventType::PointerDown;
                else if (value.action == lib::PointerAction::Up)
                    type = TextEventType::PointerUp;
                else if (value.action == lib::PointerAction::DoubleClick)
                    type = TextEventType::DoubleClick;
                TextEvent event{type, nullptr, &value};
                state->emit(event);
            }
        };
        auto old_focus = widget->on_focus;
        widget->on_focus = [weak, old_focus] {
            if (old_focus) old_focus();
            if (const auto state = weak.lock()) {
                TextEvent event{TextEventType::FocusIn};
                state->emit(event);
            }
        };
        auto old_blur = widget->on_blur;
        widget->on_blur = [weak, old_blur] {
            if (old_blur) old_blur();
            if (const auto state = weak.lock()) {
                TextEvent event{TextEventType::FocusOut};
                state->emit(event);
            }
        };
    }

    template <typename Widget>
    void attachComboCallbacks(const std::shared_ptr<Widget>& widget,
                              const std::weak_ptr<State>& weak) {
        attachTextCallbacks(widget, weak);
        const auto old_text = widget->on_text_changed;
        widget->on_text_changed = [weak, old_text](StringView value) {
            if (old_text) old_text(value);
            if (const auto state = weak.lock()) {
                TextEvent event{TextEventType::Modify};
                state->emit(event);
            }
        };
        auto old_selection = widget->on_selection_changed;
        widget->on_selection_changed = [weak, old_selection](
                                           std::optional<std::size_t> value) {
            if (old_selection) old_selection(value);
            if (const auto state = weak.lock()) {
                TextEvent modified{TextEventType::Modify};
                state->emit(modified);
                TextEvent event{TextEventType::SelectionChanged};
                state->emit(event);
            }
        };
    }

    void attachCallbacks(const std::weak_ptr<State>& weak) {
        if (callbacks_attached) return;
        if (const auto widget = textWidget()) {
            attachTextCallbacks(widget, weak);
            callbacks_attached = true;
        } else if (const auto combo = comboWidget()) {
            attachComboCallbacks(combo, weak);
            callbacks_attached = true;
        }
    }
};

namespace {

template <typename Reference>
std::shared_ptr<TMM::State> makeState(Reference reference, const TMMKind type) {
    auto state = std::make_shared<TMM::State>();
    state->reference = std::move(reference);
    state->type = type;
    // DのTMMコンストラクターは受動的なコントロール表示。共通イベ
    // ントのアダプターはリスナーの購読時だけ設定し、クーポン変換など
    // の一時的な取得・設定用表示で、同じコールバックを再度包まない。
    return state;
}

}  // namespace

TMM::TMM(TextEditPtr text)
    : state_(text ? makeState(std::weak_ptr<TextEdit>(text), TMMKind::Text)
                  : nullptr) {}

TMM::TMM(StyledTextPtr text)
    : state_(text ? makeState(std::weak_ptr<StyledText>(text), TMMKind::StyledText)
                  : nullptr) {}

TMM::TMM(ComboBoxPtr combo)
    : state_(combo ? makeState(std::weak_ptr<ComboBox>(combo), TMMKind::Combo)
                   : nullptr) {}

TMM::TMM(CComboPtr combo)
    : state_(combo ? makeState(std::weak_ptr<CCombo>(combo), TMMKind::CCombo)
                   : nullptr) {}

bool TMM::empty() const noexcept { return !state_ || !state_->textWidget() && !state_->comboWidget(); }

TMMKind TMM::kind() const noexcept {
    return state_ ? state_->type : TMMKind::Text;
}

String TMM::getText() const { return state_ ? state_->getText() : String{}; }

void TMM::setText(const StringView value) {
    const auto state=state_;
    if (!state) return;
    if (const auto widget = state->textWidget()) {
        detail::setText(*widget,value);
        return;
    }
    if (const auto combo = state->comboWidget()) {
        const auto found = std::find_if(combo->items.begin(), combo->items.end(),
            [value](const auto& item) { return item.text == value; });
        const std::optional<std::size_t> matching_item = found == combo->items.end()
            ? std::nullopt
            : std::optional<std::size_t>{static_cast<std::size_t>(
                  std::distance(combo->items.begin(), found))};

        // DWTの読み取り専用ComboのsetTextは既存行だけを選ぶ。select(index)
        // は行が変わった場合だけModifyを送る。CCombo.setText
        // は、自身の文字欄と一覧選択を別に同期する。
        if (state->type == TMMKind::Combo && !combo->editable) {
            if (!matching_item) return;
            const bool selection_changed =
                combo->selected_index != matching_item;
            combo->selected_index = matching_item;
            if (selection_changed) {
                const auto notify = combo->on_text_changed;
                const auto published = lib::combo_display_text(*combo);
                if (notify) notify(published);
            }
            return;
        }
        combo->edit_text.assign(value.data(), value.size());
        if (state->type == TMMKind::CCombo)
            combo->selected_index = matching_item;
        else
            combo->selected_index.reset();
        combo->selection_start = combo->selection_length = 0;
        combo->edit_selection = std::pair<std::size_t, std::size_t>{0, 0};
        combo->edit_selection_reversed = false;
        const auto notify=combo->on_text_changed;
        const auto published=combo->edit_text;
        if(notify)notify(published);
        // attachComboCallbacksは、一度だけのModify通知を送信済み。
    }
}

lib::Point TMM::getSelection() const { return state_ ? state_->getSelection() : lib::Point{}; }

void TMM::setSelection(lib::Point value) {
    const auto state=state_;
    if (!state) return;
    if (const auto widget = state->textWidget()) {
        if (state->type == TMMKind::StyledText)
            setStyledTextSelection(*widget, value);
        else
            setNativeEditSelection(widget->text, widget->selection_start,
                                   widget->selection_length,
                                   widget->selection_reversed,
                                   widget->multiline, value);
    } else if (const auto combo = state->comboWidget()) {
        // DWTの読み取り専用DROP_DOWN Comboは編集コントロールを持たない。setSelection
        // が送るCB_SETEDITSELは、Win32のCBS_DROPDOWNLISTでは
        // 拒否される。CComboは別の文字用の子を所有するため、文字の選択範囲を変更できる。
        if (state->type == TMMKind::Combo && !combo->editable) return;
        setNativeEditSelection(combo->edit_text, combo->selection_start,
                               combo->selection_length,
                               combo->edit_selection_reversed, false, value);
        combo->edit_selection = std::pair{combo->selection_start,
            combo->selection_start + combo->selection_length};
    }
}

bool TMM::readOnly() const noexcept {
    if (!state_) return true;
    if (const auto widget = state_->textWidget()) return widget->read_only;
    if (const auto combo = state_->comboWidget()) return !combo->editable;
    return true;
}

bool TMM::editable() const noexcept { return !readOnly(); }

TextListenerToken TMM::addListener(const TextEventType type, TextListener listener) {
    if (!state_ || !listener) return 0U;
    state_->attachCallbacks(state_);
    const auto token = state_->next_token++;
    state_->listeners[type].push_back({token, std::move(listener)});
    return token;
}

bool TMM::removeListener(const TextEventType type,
                         const TextListenerToken token) noexcept {
    if (!state_ || token == 0U) return false;
    const auto found = state_->listeners.find(type);
    if (found == state_->listeners.end()) return false;
    const auto old_size = found->second.size();
    std::erase_if(found->second,
                  [token](const TMM::State::ListenerEntry& entry) {
                      return entry.token == token;
                  });
    return found->second.size() != old_size;
}

void TMM::dispatch(TextEvent& event) const {
    const auto state=state_;
    if(state)state->emit(event);
}

TextEditPtr TMM::textWidget() const { return state_ ? state_->textWidget() : nullptr; }

ComboBoxPtr TMM::comboWidget() const { return state_ ? state_->comboWidget() : nullptr; }

class TextMenuModify::TextMenuUndo final : public undo::Undo {
public:
    explicit TextMenuUndo(TextMenuModify& owner)
        : owner_(owner), old_text_(owner.old_text_),
          selection_(owner.old_selection_),life_(owner.lifetime_) {
        if (owner_.append_data_.read)
            append_data_ = owner_.old_append_data_;
    }

    void undo() override { impl(); }
    void redo() override { impl(); }
    void dispose() override {}

private:
    TextMenuModify& owner_;
    std::any append_data_{};
    lib::Point selection_{};
    String old_text_{};
    std::weak_ptr<void> life_;

    void impl() {
        if(life_.expired())return;
        const auto revision=owner_.revision_;
        const auto live=[&]{return !life_.expired() && owner_.revision_==revision;};
        struct Guard {
            TextMenuModify* owner;
            std::weak_ptr<void> life;
            Guard(TextMenuModify& target,std::weak_ptr<void> token):owner(&target),life(std::move(token)){owner->in_proc_=true;}
            ~Guard(){if(!life.expired())owner->in_proc_=false;}
        } guard(owner_,life_);

        auto old_append = append_data_;
        const auto old_text = old_text_;
        const auto old_selection = selection_;

        const auto read=owner_.append_data_.read;
        const auto write=owner_.append_data_.write;
        auto text=owner_.text_;
        if(read)append_data_=read(old_append);
        if(!live())return;
        old_text_ = text.getText();
        selection_ = text.getSelection();

        if(write)write(old_append);
        if(!live())return;
        text.setText(old_text);
        if(!live())return;
        text.setSelection(old_selection);
        if(!live())return;
        owner_.old_append_data_ = old_append;
        owner_.old_selection_ = old_selection;
        owner_.old_text_ = old_text;
    }
};

class TextMenuModify::SelectChanged final {
public:
    explicit SelectChanged(TextMenuModify& owner):owner_(owner),life_(owner.lifetime_){}

    void handle(TextEvent& event) {
        if(life_.expired())return;
        if (event.type == TextEventType::PointerMove && !mouse_down_) return;
        if (event.type == TextEventType::PointerUp) {
            mouse_down_ = false;
            return;
        }
        if (event.type == TextEventType::PointerDown) mouse_down_ = true;

        const auto selection = owner_.text_.getSelection();
        if (selection.x != owner_.old_selection_.x ||
            selection.y != owner_.old_selection_.y) {
            owner_.old_selection_ = selection;
            const auto callback=owner_.selectChanged;
            if(callback)callback();
        }
    }

private:
    TextMenuModify& owner_;
    bool mouse_down_{};
    std::weak_ptr<void> life_;
};

TextMenuModify::TextMenuModify(TMM text, CanSaveHistory can_save_history,
                               undo::UndoManager* undo_manager,
                               TMAppendData append_data,
                               std::shared_ptr<ClipboardBackend> clipboard)
    : text_(std::move(text)), append_data_(std::move(append_data)),
      can_save_history_(std::move(can_save_history)), undo_(undo_manager),
      clipboard_(std::move(clipboard)), menu_(std::make_shared<lib::Menu>(true)),
      select_listener_(std::make_shared<SelectChanged>(*this)) {
    if (text_.empty()) return;
    const std::weak_ptr<void> life=lifetime_;
    listener_records_.push_back(
        {TextEventType::Modify, text_.addListener(
                                    TextEventType::Modify,
                                    [this,life](TextEvent& event) {if(!life.expired())modifyText(event);})});
    for (const auto type : {TextEventType::KeyDown, TextEventType::KeyUp,
                            TextEventType::PointerDown, TextEventType::PointerMove,
                            TextEventType::PointerUp, TextEventType::DoubleClick,
                            TextEventType::SelectionChanged}) {
        listener_records_.push_back(
            {type, text_.addListener(
                       type, [listener = select_listener_](TextEvent& event) {
                           listener->handle(event);
                       })});
    }
    if(!save())return;
    buildMenu();
}

TextMenuModify::~TextMenuModify() {
    lifetime_.reset();
    // ネイティブのポップアップは編集部より長く残り得る。thisへのコールバックを残さない。
    if (menu_) { menu_->on_command = {}; menu_->on_open = {}; menu_->items.clear(); }
    for (const auto& [type, token] : listener_records_)
        text_.removeListener(type, token);
}

bool TextMenuModify::save() {
    const std::weak_ptr<void> life=lifetime_;
    const auto revision=++revision_;
    const auto read=append_data_.read;
    if(read){const auto prior=old_append_data_;auto value=read(prior);if(life.expired() || revision_!=revision)return false;old_append_data_=std::move(value);}
    old_selection_ = text_.getSelection();
    old_text_ = text_.getText();
    return true;
}

void TextMenuModify::modifyText(TextEvent& event) {
    (void)event;
    modifyText();
}

void TextMenuModify::modifyText() {
    if (in_proc_ || old_text_ == text_.getText()) return;
    const std::weak_ptr<void> life=lifetime_;
    const auto revision=++revision_;
    const auto canSave=can_save_history_;
    const bool eligible=!canSave || canSave();
    if(life.expired() || revision_!=revision)return;
    if (undo_ && eligible) {
        *undo_ += std::make_shared<TextMenuUndo>(*this);
    }
    if(life.expired() || revision_!=revision)return;
    if(!save())return;
    emitRefresh();
}

void TextMenuModify::reset() {
    const std::weak_ptr<void> life=lifetime_;
    ++revision_;
    if (undo_) undo_->reset();
    if(life.expired() || !save())return;
    // リセットするのは履歴で、ダイアログが提供したメニュー構造ではない。ここ
    // で再構築すると、翻訳済みの見出しや追加の特殊文字メニューが失われる。
    const auto open=menu_?menu_->on_open:lib::Action{};
    if(open)open();
}

void TextMenuModify::emitRefresh() {
    const auto notify=refreshToolBar;
    if(notify)notify();
}

bool TextMenuModify::canUndo() const noexcept { return undo_ && undo_->canUndo(); }

bool TextMenuModify::canRedo() const noexcept { return undo_ && undo_->canRedo(); }

bool TextMenuModify::undo() {
    if (!undo_) return false;
    const std::weak_ptr<void> life=lifetime_;
    const auto result = undo_->undo();
    if(life.expired())return result;
    if (result) emitRefresh();
    return result;
}

bool TextMenuModify::redo() {
    if (!undo_) return false;
    const std::weak_ptr<void> life=lifetime_;
    const auto result = undo_->redo();
    if(life.expired())return result;
    if (result) emitRefresh();
    return result;
}

bool TextMenuModify::hasSelection() const {
    const auto value = text_.getText();
    const auto [start, end] = selectionBounds(text_.getSelection(), value.size());
    return end > start;
}

bool TextMenuModify::canCopy() const { return !text_.empty() && hasSelection(); }

bool TextMenuModify::canCut() const { return text_.editable() && canCopy(); }

bool TextMenuModify::canPaste() const {
    if (!text_.editable() || !clipboard_) return false;
    // 利用可否の確認で全クリップボード形式を実体化しない。Windows
    // のクリップボードは数MBの画像や独自データを持ち得
    // るうえ、設定画面の構築で各文字欄がこの確認を行う。
    const std::weak_ptr<void> life=lifetime_;
    const auto clipboard=clipboard_;
    const bool plain=clipboard->contains("text/plain");
    if(life.expired())return false;
    const bool typed=plain || clipboard->contains("text/plain;charset=utf-8");
    return !life.expired() && typed;
}

bool TextMenuModify::canDelete() const { return text_.editable() && hasSelection(); }

bool TextMenuModify::canSelectAll() const {
    const auto value = text_.getText();
    const auto selection = text_.getSelection();
    // TMMはSWTのUTF-16位置と異なり、UTF-8のバイト端点を公開する。
    // selectAll()と同じ全範囲を使い、Dの選択文字列の規則を維持する。
    return !value.empty() && (selection.x != 0.0F ||
                             selection.y != static_cast<float>(value.size()));
}

void TextMenuModify::copy() {
    if (!clipboard_ || !canCopy()) return;
    const std::weak_ptr<void> life=lifetime_;
    const auto clipboard=clipboard_;
    const auto value = text_.getText();
    const auto [start, end] = selectionBounds(text_.getSelection(), value.size());
    clipboard->write({value.substr(start, end - start), {}, {}});
    if(life.expired())return;
    emitRefresh();
}

void TextMenuModify::cut() {
    if (!canCut()) return;
    const std::weak_ptr<void> life=lifetime_;
    copy();
    if(life.expired())return;
    erase();
}

void TextMenuModify::paste() {
    const std::weak_ptr<void> life=lifetime_;
    if (!canPaste()) return;
    if(life.expired())return;
    const auto clipboard=clipboard_;
    auto text=text_;
    const auto data = clipboard->read();
    if(life.expired())return;
    if (const auto widget = text.textWidget()) {
        detail::replaceSelection(*widget, data.text);
    } else if (const auto combo = text.comboWidget()) {
        if (!combo->editable) return;
        const auto [start, end] = selectionBounds(text.getSelection(), combo->edit_text.size());
        auto next = combo->edit_text;
        next.replace(start, end - start, data.text);
        text.setText(next);
        if(life.expired())return;
        text.setSelection({static_cast<float>(start + data.text.size()),
                            static_cast<float>(start + data.text.size())});
    }
    if(life.expired())return;
    emitRefresh();
}

void TextMenuModify::erase() {
    if (!canDelete()) return;
    const std::weak_ptr<void> life=lifetime_;
    auto text=text_;
    const auto value = text_.getText();
    const auto [start, end] = selectionBounds(text_.getSelection(), value.size());
    text.setText(value.substr(0U, start) + value.substr(end));
    if(life.expired())return;
    text.setSelection({static_cast<float>(start), static_cast<float>(start)});
    if(life.expired())return;
    emitRefresh();
}

void TextMenuModify::selectAll() {
    const std::weak_ptr<void> life=lifetime_;
    const auto value = text_.getText();
    text_.setSelection({0.0F, static_cast<float>(value.size())});
    if(life.expired())return;
    emitRefresh();
}

void TextMenuModify::buildMenu() {
    if (!menu_) menu_ = std::make_shared<lib::Menu>(true);
    menu_->items = {
        {"Undo", "undo"}, {"Redo", "redo"},
        {"", "", {}, {}, true, false, false, true},
        {"Cut", "cut"}, {"Copy", "copy"}, {"Paste", "paste"},
        {"Delete", "delete"}, {"", "", {}, {}, true, false, false, true},
        {"Select All", "select_all"},
    };
    const std::weak_ptr<void> life=lifetime_;
    menu_->on_command = [this,life](const StringView command) {
        if(life.expired())return;
        if (command == "undo") undo();
        else if (command == "redo") redo();
        else if (command == "cut") cut();
        else if (command == "copy") copy();
        else if (command == "paste") paste();
        else if (command == "delete") erase();
        else if (command == "select_all") selectAll();
        if(life.expired())return;
        const auto popup=menu_;
        const auto open=popup?popup->on_open:lib::Action{};
        if(open)open();
    };
    menu_->on_open = [this,life] {
        if(life.expired())return;
        if (!menu_) return;
        const auto popup=menu_;
        const auto revision=popup->item_update_revision;
        auto items=popup->items;
        for (auto& item : items) {
            if (item.command == "undo") item.enabled = canUndo();
            else if (item.command == "redo") item.enabled = canRedo();
            else if (item.command == "cut") item.enabled = canCut();
            else if (item.command == "copy") item.enabled = canCopy();
            else if (item.command == "paste") item.enabled = canPaste();
            else if (item.command == "delete") item.enabled = canDelete();
            else if (item.command == "select_all") item.enabled = canSelectAll();
            if(life.expired() || popup->item_update_revision!=revision)return;
        }
        popup->items=std::move(items);
        ++popup->item_update_revision;
    };
    menu_->on_open();
}

namespace {

[[nodiscard]] String numericPart(StringView text) {
    String result;
    result.reserve(text.size());
    for (const auto character : text) {
        if ((character>='0' && character<='9') || character == '-' || character == '.')
            result.push_back(character);
    }
    return result;
}

class NumberEditor final : public TextEdit {
public:
    std::shared_ptr<undo::UndoManager> undo_manager;
    std::shared_ptr<TextMenuModify> menu;
    [[nodiscard]] String filterInsertedText(StringView value)const override {
        return numericPart(value);
    }
};

}  // namespace

TextEditPtr createNumberEditor(Commons comm, const Parent parent,
                               const std::int32_t style,
                               TextMenuModify::CanSaveHistory catch_mod) {
    (void)style;
    auto number = std::make_shared<NumberEditor>();
    // DのcreateNumberEditorはTextにNoIMEを指定する。共通
    // 入力表示側はnumeric_inputで、現在のセッションのIMEを抑止する。
    number->numeric_input = !lib::editor_window_host_options().all_mode;
    if (parent) parent->add(number);

    number->undo_manager = std::make_shared<undo::UndoManager>(comm.undoMaxEtc);
    number->menu = std::make_shared<TextMenuModify>(
        TMM(std::static_pointer_cast<TextEdit>(number)), std::move(catch_mod),
        number->undo_manager.get(), TMAppendData{}, comm.clipboard());
    number->menu->refreshToolBar = comm.refreshToolBar;

    const std::weak_ptr<NumberEditor> weak_number = number;
    const auto old_focus = number->on_focus;
    number->on_focus = [weak_number, old_focus] {
        if (old_focus) old_focus();
        if (const auto target = weak_number.lock()) {
            if (target->read_only) return;
            target->selection_start = 0U;
            target->selection_length = target->text.size();
        }
    };
    number->on_text_input = [weak_number](lib::TextInputEvent& event) {
        if (const auto target = weak_number.lock()) {
            if (event.composing || target->read_only) return;
            detail::replaceSelection(*target,event.text);
            event.handled = true;
        }
    };
    return number;
}

}  // namespace cwxeditor::cwx::editor::gui::dwt::customtext
