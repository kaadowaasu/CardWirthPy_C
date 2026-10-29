// cw/dialog/message.py
// から移植。UI 固有の描画はアダプター
// で行い、UI の実装は共通の一つに保つ。
#include "cw/dialog/message.hpp"
#include "cw/cw.hpp"
#include "cw/data.hpp"
#include "cw/frame.hpp"
#include "cw/imageretouch.hpp"
#include "cw/lib/ui_surface.hpp"
#include "cw/setting.hpp"
#include "cw/thread.hpp"
#include "cw/util.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <stdexcept>
#include <map>
#include <memory>
#include <sstream>
#include <utility>

namespace cw::dialog::message {
namespace {
using String = ::cw::lib::String;

template<class T>
std::shared_ptr<T> object_value(const ::cw::lib::Value& value)
{
    const auto* object = std::get_if<::cw::lib::ObjectPtr>(&value.storage());
    return object && *object ? std::dynamic_pointer_cast<T>(*object) : nullptr;
}
using Integer = ::cw::lib::Integer;
constexpr ::cw::lib::Integer ID_OK=kWxIdOk, ID_CANCEL=kWxIdCancel;
constexpr ::cw::lib::Integer ID_YES=kWxIdYes, ID_NO=kWxIdNo;
thread_local SysMessage* sysmessage_callback_context{};
thread_local ::cw::lib::String sysmessage_callback_key{};

struct MessageLabel { String text; char mnemonic{}; };
MessageLabel message_label(const String& label)
{
    MessageLabel result;
    for (std::size_t i=0;i<label.size();++i) {
        if (label[i]=='&' && i+1<label.size()) {
            if (label[i+1]=='&') { result.text+='&'; ++i; }
            else if (!result.mnemonic && static_cast<unsigned char>(label[i+1])<128)
                result.mnemonic=static_cast<char>(std::tolower(static_cast<unsigned char>(label[i+1])));
        } else result.text+=label[i];
    }
    return result;
}

// 重複のないアクセスキーは操作を実行する。重複がある場合はフォーカスだけを移し、意
// 図せず肯定の回答を選ばない。 文字としての && はアクセスキーとして扱わない。
bool message_mnemonic(const std::vector<String>& labels, int& focus,
                      DialogPresentationKey key, DialogPresentationModifiers modifiers,
                      const std::function<void(int)>& activate)
{
    if (modifiers != DialogPresentationModifierAlt) return false;
    const char letter=::cw::dialog::presentation_key_ascii(key);
    if (!letter) return false;
    std::vector<int> matches;
    for (std::size_t i=0;i<labels.size();++i)
        if (message_label(labels[i]).mnemonic==letter) matches.push_back(static_cast<int>(i));
    if (matches.empty()) return false;
    const auto next=std::find_if(matches.begin(),matches.end(),[focus](int index){return index>focus;});
    focus=next==matches.end()?matches.front():*next;
    if (matches.size()==1) activate(focus);
    return true;
}

::cw::lib::String localized_message(const ::cw::lib::String& key, const ::cw::lib::String& fallback)
{
    if (::cw::g_cwpy && ::cw::g_cwpy->m_setting && ::cw::g_cwpy->m_setting->m_msgs)
        return ::cw::g_cwpy->m_setting->m_msgs->get(key, fallback);
    if (::cw::setting::Setting::s_msgs) return ::cw::setting::Setting::s_msgs->get(key, fallback);
    return fallback;
}

::cw::lib::String wrap_text(const ::cw::lib::String& text, ::cw::lib::Integer width)
{
    try {
        return ::cw::util::wordwrap(text, width, std::nullopt, {}, {}, nullptr);
    } catch (const std::exception&) {
        // 任意のテキストアダプターが未初期化でも、メッセージを利用
        // できるようにする。 その場合は元のテキストを維持する。
        return text;
    }
}

::cw::lib::String wrap_pixel_text(const ::cw::lib::String& text, ::cw::lib::Integer width,
                                  const ::cw::lib::ui::FontStyle& font)
{
    ::cw::lib::Callback extent = [font] (const ::cw::lib::Arguments& args,
                                     const ::cw::lib::KeywordArguments&) -> ::cw::lib::Value {
        if (args.empty()) return ::cw::lib::Value(::cw::lib::Integer{0});
        if (const auto* value = std::get_if<::cw::lib::String>(&args.front().storage()))
            return ::cw::lib::Value(::cw::lib::ui::label_width(*value,font.pixels,font.bold,font.face, font.italic));
        return ::cw::lib::Value(::cw::lib::Integer{0});
    };
    try {
        return ::cw::util::wordwrap(text, width, extent, {}, {}, nullptr);
    } catch (const std::exception&) {
        return text;
    }
}

::cw::lib::Integer line_count(const ::cw::lib::String& text)
{
    return text.empty() ? 1 : static_cast<::cw::lib::Integer>(1 + std::count(text.begin(), text.end(), '\n'));
}

::cw::lib::Integer system_scale(::cw::lib::Integer value)
{
    return ::cw::lib::ui::win_i(value);
}

// 従来 HWND が管理していたフォーカス移動を共通モデルで管理する。 「いいえ
// 」「キャンセル」で Enter を押しても、黙って先頭の選択肢を確定しない。
bool move_message_focus(int& focus, int count, DialogPresentationKey key,
                        DialogPresentationModifiers modifiers)
{
    using Key = DialogPresentationKey;
    if (count <= 0 || (modifiers & (DialogPresentationModifierControl |
                                   DialogPresentationModifierAlt))) return false;
    int direction = 0;
    if (key == Key::Tab) direction = (modifiers & DialogPresentationModifierShift) ? -1 : 1;
    else if (key == Key::Left || key == Key::Up) direction = -1;
    else if (key == Key::Right || key == Key::Down) direction = 1;
    if (!direction) return false;
    focus = (std::clamp(focus, 0, count - 1) + direction + count) % count;
    return true;
}

void draw_message_focus(const std::shared_ptr<::cw::lib::Surface>& surface,
                        const ::cw::lib::Rect& rect, Integer inset=system_scale(3),
                        Integer stroke=std::max<Integer>(1,system_scale(1)))
{
    const ::cw::lib::Rect r{rect.x + inset, rect.y + inset,
                           rect.width - 2 * inset, rect.height - 2 * inset};
    for (Integer x = r.x; x < r.x + r.width; x += stroke * 2) {
        ::cw::lib::ui::fill_rect(surface, {x, r.y, stroke, stroke}, {40, 40, 40, 255});
        ::cw::lib::ui::fill_rect(surface, {x, r.y + r.height - stroke, stroke, stroke}, {40, 40, 40, 255});
    }
    for (Integer y = r.y; y < r.y + r.height; y += stroke * 2) {
        ::cw::lib::ui::fill_rect(surface, {r.x, y, stroke, stroke}, {40, 40, 40, 255});
        ::cw::lib::ui::fill_rect(surface, {r.x + r.width - stroke, y, stroke, stroke}, {40, 40, 40, 255});
    }
}

struct MessageTextExtent {
    Integer width{}, height{}, line_height{};
};
MessageTextExtent message_text_extent(const String& text, const ::cw::lib::ui::FontStyle& style)
{
    ::cw::imageretouch::Font font;
    font.initialize(style.face, -std::max<Integer>(1, style.pixels), style.bold, style.italic);
    // wx の GetFullMultiLineTextExtent は、ピ
    // クセル寸法に固定の隙間を足すのではなく、 空行も含め選択フォントの文
    // 字メトリクスを使う。共通のフォント境界が各ホストでその値を供給する。
    MessageTextExtent result;
    result.line_height = std::max<Integer>(1, std::get<1>(font.size("Mg")));
    std::size_t begin = 0;
    do {
        const auto end = text.find('\n', begin);
        const auto line = text.substr(begin, end == String::npos ? String::npos : end - begin);
        result.width = std::max<Integer>(result.width, std::get<0>(font.size(line)));
        result.height += result.line_height;
        if (end == String::npos) break;
        begin = end + 1;
    } while (begin <= text.size());
    return result;
}

Integer message_choice_width(const MessageChoice& choice)
{
    const auto font = ::cw::lib::ui::configured_font_scaled("button", 14);
    return (choice.width >= 0 ? choice.width :
        std::max<Integer>(system_scale(80),
            ::cw::lib::ui::label_width(message_label(choice.label).text, font.pixels, font.bold, font.face, font.italic) + system_scale(24)));
}

std::shared_ptr<::cw::lib::Window> make_button(const MessageChoice& choice)
{
    auto button = std::make_shared<::cw::lib::Window>();
    button->id = choice.id;
    button->title = choice.label;
    button->enabled = choice.enabled;
    return button;
}

MessageChoice parse_choice(const ::cw::lib::Value& value, ::cw::lib::Integer default_id)
{
    if (const auto* label = std::get_if<::cw::lib::String>(&value.storage()))
        return {*label, default_id, -1, {}};
    const auto* object = std::get_if<::cw::lib::ObjectPtr>(&value.storage());
    if (!object || !*object) throw std::invalid_argument("choice must contain a label");
    const auto sequence = std::dynamic_pointer_cast<::cw::data::VariantSequence>(*object);
    if (!sequence || sequence->m_values.empty()) throw std::invalid_argument("choice must contain a label");
    const auto* label = std::get_if<::cw::lib::String>(&sequence->m_values[0].storage());
    if (!label) throw std::invalid_argument("choice label must be a string");
    ::cw::lib::Integer id = default_id;
    ::cw::lib::Integer width = -1;
    ::cw::lib::String description;
    if (sequence->m_values.size() > 1) {
        if (const auto* value_id = std::get_if<::cw::lib::Integer>(&sequence->m_values[1].storage())) id = *value_id;
        else throw std::invalid_argument("choice id must be an integer");
    }
    if (sequence->m_values.size() > 2) {
        if (const auto* value_width = std::get_if<::cw::lib::Integer>(&sequence->m_values[2].storage())) width = *value_width;
        else throw std::invalid_argument("choice width must be an integer");
    }
    if (sequence->m_values.size() > 3) {
        if (const auto* value_description = std::get_if<::cw::lib::String>(&sequence->m_values[3].storage())) description = *value_description;
        else throw std::invalid_argument("choice description must be a string");
    }
    if (sequence->m_values.size() > 4) throw std::invalid_argument("choice has too many fields");
    return {*label, id, width, std::move(description)};
}

std::vector<MessageChoice> parse_choices(const std::vector<::cw::lib::Value>& values)
{
    std::vector<MessageChoice> result;
    result.reserve(values.size());
    for (std::size_t index = 0; index < values.size(); ++index)
        result.push_back(parse_choice(values[index], static_cast<::cw::lib::Integer>(index)));
    return result;
}

void populate_message_buttons(Message& self)
{
    self.m_buttons.clear();
    self.m_button_ids.clear();
    self.m_yesbtn = {};
    self.m_nobtn = {};
    self.m_closebtn = {};
    for (const auto& choice : self.m_choices) {
        self.m_button_ids.push_back(choice.id);
        self.m_buttons.emplace_back(std::static_pointer_cast<::cw::lib::Object>(make_button(choice)));
    }
    if (self.m_mode == 1 && self.m_buttons.size() >= 2) {
        self.m_yesbtn = self.m_buttons[0];
        self.m_nobtn = self.m_buttons[1];
    } else if (self.m_mode == 2 && !self.m_buttons.empty()) {
        self.m_closebtn = self.m_buttons[0];
    }
}

void populate_sys_buttons(SysMessage& self)
{
    self.m_buttons.clear();
    for (const auto& choice : self.m_choices)
        self.m_buttons.emplace_back(std::static_pointer_cast<::cw::lib::Object>(make_button(choice)));
}

}

void MessageUiBackend::show_message(Message* dialog)
{
    if (!dialog) return;
    // 基底バックエンドは UI が利用できない状態を表す。 表示できないモデル
    // をモーダルの呼び出し元が永久に待たないよう、キャンセルとして完了する。
    dialog->m_return_code = ID_CANCEL;
    dialog->m_closed = true;
    dialog->visible = false;
}

void MessageUiBackend::show_sys_message(SysMessage* dialog)
{
    if (!dialog) return;
    dialog->m_return_code = ID_CANCEL;
    dialog->m_closed = true;
    dialog->visible = false;
}
void MessageUiBackend::layout(::cw::lib::Object*) {}
void MessageUiBackend::close(::cw::lib::Object*) {}
void MessageUiBackend::play_sound(::cw::lib::String name)
{
    if (::cw::g_cwpy && !name.empty())
        ::cw::g_cwpy->play_sound(std::move(name), false, 100, 1, 0, 0, false);
}

namespace {
// 製品用の表示専用バックエンド。Message と SysMessage
// 自体が完全な共通の描画面・コントロールモデルを所有する
// 。 このアダプターは MessageUiBackend
// から継承する、プラットフォームに依存しない効果音サー
// ビスだけを保持する。 UI の経路は共通の一つに保つ。
class SharedMessageUiBackend final : public MessageUiBackend {
public:
    void show_message(Message* dialog) override
    {
        (void)dialog;
    }

    void show_sys_message(SysMessage* dialog) override
    {
        (void)dialog;
    }
};

}

std::shared_ptr<MessageUiBackend> get_ui_backend()
{
    static const auto services = std::make_shared<SharedMessageUiBackend>();
    return services;
}

std::optional<bool> show_common_modal_message(
    const String& arg_title, const String& arg_text,
    const bool arg_question, const bool arg_error)
{
    (void)arg_error;
    auto dialog=std::make_shared<Message>();
    dialog->initialize({},arg_title,arg_text,arg_question?1:2,std::nullopt);
    // 現在のモーダルの所有者は、Engine のグローバルフレームとは異なる場
    // 合がある。 未初期化の子を公開せず、共通ホストに所有者の解決を委ねる。
    if(!::cw::dialog::show_common_modal_presentation(dialog)) return std::nullopt;
    return arg_question?dialog->m_return_code==ID_OK:true;
}

std::optional<bool> show_common_modal_message(
    const std::shared_ptr<::cw::frame::Frame>& arg_frame,
    const String& arg_title, const String& arg_text,
    const bool arg_question, const bool arg_error)
{
    if (!arg_frame)
        return std::nullopt;
    (void)arg_error;
    auto dialog = std::make_shared<Message>();
    auto parent = std::make_shared<::cw::lib::Window>();
    dialog->initialize(parent, arg_title, arg_text, arg_question ? 1 : 2,
                       std::nullopt);
    const bool shown = ::cw::dialog::show_common_modal_presentation(arg_frame, dialog);
    if (!shown) return std::nullopt;
    return arg_question ? dialog->m_return_code == ID_OK : true;
}

std::optional<Integer> show_common_modal_choices(
    const String& arg_title, const String& arg_text,
    const std::vector<MessageChoice>& arg_choices)
{
    auto dialog = std::make_shared<Message>();
    dialog->initialize({}, arg_title, arg_text, 3, arg_choices);
    const bool shown = ::cw::dialog::show_common_modal_presentation(dialog);
    if (!shown) return std::nullopt;
    return dialog->m_return_code;
}

void Message::initialize(std::shared_ptr<::cw::lib::Window> arg_parent, ::cw::lib::String arg_name, ::cw::lib::String arg_text, ::cw::lib::Integer arg_mode, std::optional<std::vector<::cw::lib::Value>> arg_choices)
{
    dialog_retained_surface().reset();
    native_handle = arg_parent ? arg_parent->native_handle : 0;
    parent_window = arg_parent;
    if (arg_mode < 1 || 3 < arg_mode) throw std::invalid_argument("message mode");
    if (arg_mode == 3 && !arg_choices) throw std::invalid_argument("choices required");
    m_title = std::move(arg_name);
    title = m_title;
    m_basetext = std::move(arg_text);
    m_wrapped_text = wrap_text(m_basetext, 50);
    m_text = m_wrapped_text;
    m_mode = arg_mode;
    m_cwpy_debug = false;
    cwpy_debug = m_cwpy_debug;
    static_cast<::cw::lib::Window&>(*this).copy_detail = [this] { copy_detail(); };
    m_detailtext.clear();
    m_closed = false;
    m_focus_index = 0;
    visible = true;
    m_return_code = ID_CANCEL;
    m__textheight = line_count(m_text);
    if (m_mode == 1) {
        m_choices = {{localized_message("yes", "はい"), ID_OK, system_scale(120), {}},
                     {localized_message("no", "いいえ"), ID_CANCEL, system_scale(120), {}}};
    } else if (m_mode == 2) {
        m_choices = {{localized_message("close", "閉じる"), ID_CANCEL, system_scale(120), {}}};
    } else {
        m_choices = parse_choices(*arg_choices);
    }
    populate_message_buttons(*this);
    _do_layout();
    get_ui_backend()->show_message(this);
}

void Message::OnCopyDetail(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    copy_detail();
}

void Message::copy_detail()
{
    get_ui_backend()->play_sound("equipment");
    // Python と同様、空のボタン行も追加する。SysMessage はボタンなしの場合の独立したコピー規則を維持する。
    m_detailtext = "[Window Title]\n" + m_title + "\n\n[Content]\n" + m_basetext + "\n\n";
    for (std::size_t i = 0; i < m_choices.size(); ++i) {
        if (i) m_detailtext += " ";
        m_detailtext += "[" + message_label(m_choices[i].label).text + "]";
    }
    copy_text(m_detailtext);
}

void Message::copy_text(const String& text) { ::cw::dialog::presentation_set_clipboard_text(text); }

void Message::OnCancel(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    get_ui_backend()->play_sound("click");
    close_with_result(ID_CANCEL);
}

void Message::OnButton(std::shared_ptr<::cw::lib::Window> arg_event)
{
    if (!arg_event) return;
    close_with_result(arg_event->id);
}

void Message::OnPaint(std::shared_ptr<::cw::lib::Window> arg_evt)
{
    (void)arg_evt;
    get_ui_backend()->layout(this);
}

void Message::_do_layout()
{
    const auto font = ::cw::lib::ui::configured_wx_font("dlgmsg", 15);
    const auto extent = message_text_extent(m_wrapped_text, font);
    m__textheight = extent.height;
    const Integer text_width = std::max<Integer>(
        ::cw::lib::ui::win_i(349), extent.width + ::cw::lib::ui::win_i(20));
    bounds = {0, 0, text_width,
              std::max<Integer>(::cw::lib::ui::win_i(68),
                  m__textheight + ::cw::lib::ui::win_i(68))};
    get_ui_backend()->layout(this);
}

void Message::initialize(std::shared_ptr<::cw::lib::Window> arg_parent, ::cw::lib::String arg_name, ::cw::lib::String arg_text, ::cw::lib::Integer arg_mode, std::vector<MessageChoice> arg_choices)
{
    dialog_retained_surface().reset();
    if (arg_mode != 3) throw std::invalid_argument("custom choices require mode 3");
    native_handle = arg_parent ? arg_parent->native_handle : 0;
    parent_window = arg_parent;
    m_title = std::move(arg_name);
    title = m_title;
    m_basetext = std::move(arg_text);
    m_wrapped_text = wrap_text(m_basetext, 50);
    m_text = m_wrapped_text;
    m_mode = 3;
    m_cwpy_debug = false;
    cwpy_debug = m_cwpy_debug;
    static_cast<::cw::lib::Window&>(*this).copy_detail = [this] { copy_detail(); };
    m_detailtext.clear();
    m_choices = std::move(arg_choices);
    m_closed = false;
    m_focus_index = 0;
    visible = true;
    m_return_code = ID_CANCEL;
    m__textheight = line_count(m_text);
    populate_message_buttons(*this);
    _do_layout();
    get_ui_backend()->show_message(this);
}

void Message::close_with_result(::cw::lib::Integer result_id)
{
    m_return_code = result_id;
    m_closed = true;
    visible = false;
    get_ui_backend()->close(this);
}

bool Message::select_choice(::cw::lib::Integer choice_id)
{
    if (m_closed) return false;
    for (const auto& choice : m_choices) {
        if (choice.id == choice_id && choice.enabled) {
            close_with_result(choice_id);
            return true;
        }
    }
    return false;
}

::cw::dialog::DialogPresentation Message::dialog_presentation()
{
    using Action = ::cw::dialog::DialogPresentationAction;
    const Integer width = std::max<Integer>(::cw::lib::ui::win_i(349), bounds.width);
    const Integer height = std::max<Integer>(::cw::lib::ui::win_i(68), bounds.height);
    auto& retained=dialog_retained_surface();
    const bool full=retained.needs_full({width,height});
    if(full) retained.allocate({width,height},{240,240,240,255});
    auto surface=retained.surface;
    if(full) {
        auto tile = ::cw::lib::ui::resource_dialog_background("CAUTION");
        if (tile && tile->size.width > 1 && tile->size.height > 1) {
            tile = ::cw::lib::ui::resource_surface_for_window(tile);
            for (Integer y = 0; y < height; y += tile->size.height)
                for (Integer x = 0; x < width; x += tile->size.width)
                    ::cw::lib::ui::blend_surface(surface, tile, x, y);
        }
    }

    ::cw::dialog::DialogPresentation result;
    result.route = "YESNO";
    result.title = m_title;
    const auto text_font = ::cw::lib::ui::configured_wx_font("dlgmsg", 15);
    const auto button_font = ::cw::lib::ui::configured_font_scaled("button", 14);
    std::vector<String> lines;
    std::istringstream input(m_wrapped_text);
    for (String line; std::getline(input, line);) lines.push_back(std::move(line));
    if (lines.empty()) lines.emplace_back();
    const Integer text_step = message_text_extent(m_wrapped_text, text_font).line_height;
    Integer text_y = ::cw::lib::ui::win_i(12);
    for (const auto& line : lines) {
        const Integer line_width = ::cw::lib::ui::label_width(
            line, text_font.pixels, text_font.bold, text_font.face, text_font.italic);
        if(full) ::cw::lib::ui::draw_label(
                surface, line,
                std::max<Integer>(::cw::lib::ui::win_i(8), (width - line_width) / 2),
                text_y, text_font.pixels, text_font.bold, {0, 0, 0}, text_font.face, text_font.italic);
        text_y += text_step;
    }

    std::vector<Integer> button_widths;
    Integer total = 0;
    for (const auto& choice : m_choices) {
        const Integer button_width = message_choice_width(choice);
        button_widths.push_back(button_width);
        total += button_width;
    }
    // Python と同様、両端の余白も含め、n+1 個の均等な伸縮可能な間隔を設ける。
    const Integer gap = (width - total) / static_cast<Integer>(m_choices.size() + 1);
    Integer x = gap;
    const Integer button_height = ::cw::lib::ui::win_i(30);
    const Integer button_y = height - ::cw::lib::ui::win_i(44);
    ::cw::lib::Rect focus_rect{0,0,1,1};
    for (std::size_t i = 0; i < m_choices.size(); ++i) {
        const ::cw::lib::Rect rect{x, button_y, button_widths[i], button_height};
        if(full) {
            ::cw::lib::ui::draw_button_face(surface, rect);
        }
        const auto label = message_label(m_choices[i].label).text;
        const Integer label_width = ::cw::lib::ui::label_width(
            label, button_font.pixels, button_font.bold, button_font.face, button_font.italic);
        if(full) ::cw::lib::ui::draw_label(surface, label,
                rect.x + std::max<Integer>(::cw::lib::ui::win_i(5),
                                           (rect.width - label_width) / 2),
                rect.y + ::cw::lib::ui::win_i(6), button_font.pixels,
                button_font.bold, m_choices[i].enabled ? std::tuple<Integer, Integer, Integer>{0, 0, 0} : std::tuple<Integer, Integer, Integer>{128, 128, 128}, button_font.face, button_font.italic);
        result.controls.push_back({Action::Select, static_cast<int>(i),
            static_cast<float>(rect.x), static_cast<float>(rect.y),
            static_cast<float>(rect.width), static_cast<float>(rect.height), !m_closed && m_choices[i].enabled,
            false, label, m_choices[i].description});
        if (!m_closed && m_choices[i].enabled && m_focus_index == static_cast<int>(i)) focus_rect=rect;
        x += rect.width + gap;
    }
    if(full) retained.capture_base();
    const auto focus_signature=(static_cast<std::uint64_t>(m_closed)<<32U) ^
        static_cast<std::uint64_t>(static_cast<std::uint32_t>(m_focus_index+1));
    const bool focus_changed=retained.component_changed("message.focus",focus_rect,focus_signature);
    retained.restore_dirty();
    surface=retained.surface;
    if(focus_changed && !m_closed && m_focus_index>=0 &&
       static_cast<std::size_t>(m_focus_index)<m_choices.size() && m_choices[m_focus_index].enabled)
        draw_message_focus(surface,focus_rect);
    retained.finish();
    result.surface = surface;
    result.damage=retained.damage;
    result.surface_revision=retained.revision;
    return result;
}

void Message::dialog_presentation_action(
    const ::cw::dialog::DialogPresentationAction action, const int index)
{
    using Action = ::cw::dialog::DialogPresentationAction;
    if (m_closed) return;
    if (action == Action::Cancel) {
        OnCancel(nullptr);
        return;
    }
    if (action == Action::Accept && !m_choices.empty()) {
        select_choice(m_choices[std::clamp(m_focus_index, 0, static_cast<int>(m_choices.size()) - 1)].id);
    } else if ((action == Action::Select || action == Action::Activate) && index >= 0 &&
               static_cast<std::size_t>(index) < m_choices.size() && m_choices[index].enabled) {
        // ボタンは Select コントロールで、タップ・クリック回数が 1 を
        // 超える解除は dialog_release_action により Activate
        // として届く。 wx のボタンはクリックごとに作動するため、こ
        // のメッセージを開いた直後などの素早いタップでも押下として扱う。
        m_focus_index = index;
        select_choice(m_choices[static_cast<std::size_t>(index)].id);
    }
}

bool Message::dialog_presentation_finished() const noexcept
{
    return m_closed;
}

void Message::dialog_presentation_key(DialogPresentationKey key, bool pressed,
                                      DialogPresentationModifiers modifiers)
{
    if (!pressed || m_closed) return;
    std::vector<String> labels;
    for (const auto& choice:m_choices) labels.push_back(choice.enabled ? choice.label : String{});
    if (message_mnemonic(labels,m_focus_index,key,modifiers,[this](int index) {
            dialog_presentation_action(DialogPresentationAction::Select,index);
        })) { request_dialog_presentation_refresh(); return; }
    if (key == DialogPresentationKey::C && (modifiers & DialogPresentationModifierControl)) {
        copy_detail(); return;
    }
    if (move_message_focus(m_focus_index, static_cast<int>(m_choices.size()), key, modifiers)) {
        for (std::size_t count = 0; count < m_choices.size() && !m_choices[m_focus_index].enabled; ++count)
            move_message_focus(m_focus_index, static_cast<int>(m_choices.size()), key, modifiers);
        request_dialog_presentation_refresh(); return;
    }
    if (!modifiers && (key == DialogPresentationKey::Space || key == DialogPresentationKey::Enter))
        dialog_presentation_action(DialogPresentationAction::Accept, -1);
}

void Message::dialog_presentation_complete()
{
    // 既存の共通モデルは Message::close_with_result で既に閉じている。
}

void SysMessage::initialize(std::shared_ptr<::cw::lib::Window> arg_parent, ::cw::lib::String arg_name, ::cw::lib::String arg_text, std::optional<std::vector<::cw::lib::Value>> arg_choices, std::optional<std::vector<std::tuple<::cw::lib::String, ::cw::lib::String, bool>>> arg_checkboxes)
{
    dialog_retained_surface().reset();
    native_handle = arg_parent ? arg_parent->native_handle : 0;
    parent_window = arg_parent;
    m_title = std::move(arg_name);
    title = m_title;
    m_basetext = std::move(arg_text);
    m_system_metrics = system_metrics();
    m_wrapped_text = wrap_pixel_text(m_basetext, m_system_metrics.scaled(345), m_system_metrics.font);
    m_text = m_wrapped_text;
    m__st_text = ::cw::lib::Value(m_wrapped_text);
    m__stl = ::cw::lib::Value(false);
    m__textheight = line_count(m_text);
    m_cwpy_debug = true;
    cwpy_debug = m_cwpy_debug;
    static_cast<::cw::lib::Window&>(*this).copy_detail = [this] { copy_detail(); };
    m__check_table.clear();
    m__checkboxes.clear();
    m_check_options.clear();
    m_choices.clear();
    m_detailtext.clear();
    m_closed = false;
    m_focus_index = 0;
    visible = true;
    m_return_code = ID_CANCEL;
    if (arg_checkboxes) {
        for (std::size_t index = 0; index < arg_checkboxes->size(); ++index) {
            const auto& [key, label, value] = (*arg_checkboxes)[index];
            m__check_table[key] = value;
            m_check_options.push_back({key, label, value});
            auto checkbox = std::make_shared<::cw::lib::Window>();
            checkbox->id = static_cast<::cw::lib::Integer>(index);
            checkbox->title = label;
            m__checkboxes.emplace_back(key, std::move(checkbox));
        }
    }
    if (arg_choices) m_choices = parse_choices(*arg_choices);
    populate_sys_buttons(*this);
    m__stl = ::cw::lib::Value(!m_choices.empty());
    sysmessage_callback_context = this;
    _do_layout();
    get_ui_backend()->show_sys_message(this);
}

bool SysMessage::get_check(::cw::lib::String arg_key)
{
    return m__check_table.at(arg_key);
}

void SysMessage::OnCopyDetail(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;copy_detail();
}

void SysMessage::copy_detail()
{
    m_detailtext = "[Window Title]\n" + m_title + "\n\n[Content]\n" + m_basetext;
    if (!m_choices.empty()) {
        m_detailtext += "\n\n";
        for (std::size_t i = 0; i < m_choices.size(); ++i) {
            if (i) m_detailtext += " ";
            m_detailtext += "[" + message_label(m_choices[i].label).text + "]";
        }
    }
    copy_text(m_detailtext);
}

void SysMessage::copy_text(const String& text) { ::cw::dialog::presentation_set_clipboard_text(text); }

void SysMessage::OnCancel(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    close_with_result(ID_CANCEL);
}

void SysMessage::OnButton(std::shared_ptr<::cw::lib::Window> arg_event)
{
    if (!arg_event) return;
    close_with_result(arg_event->id);
}

void SysMessage::OnCheck(std::shared_ptr<::cw::lib::Window> arg_event)
{
    if (!arg_event || arg_event->id < 0 || static_cast<std::size_t>(arg_event->id) >= m_check_options.size()) return;
    const auto& option = m_check_options[static_cast<std::size_t>(arg_event->id)];
    if (arg_event->bounds.width == 1 || arg_event->bounds.width == -1)
        set_check(option.key, arg_event->bounds.width == 1);
    else
        set_check(option.key, !option.value);
}

void SysMessage::_do_layout()
{
    const auto& metrics=m_system_metrics;
    const auto& font=metrics.font;
    const auto scale=[&](Integer value){return metrics.scaled(value);};
    const auto extent=[&](const String& text){return ::cw::lib::ui::label_width(text,font.pixels,font.bold,font.face, font.italic);};
    const Integer margin=scale(10), line_height=std::max<Integer>(1,metrics.line_height);
    m__textheight = line_count(m_wrapped_text)*line_height;
    Integer text_width=0;
    std::istringstream input(m_wrapped_text);
    for (String line;std::getline(input,line);) text_width=std::max<Integer>(text_width,extent(line));
    Integer width=text_width;
    const Integer check_size=scale(13), check_height=std::max<Integer>(line_height,check_size);
    for (const auto& check:m_check_options)
        width=std::max<Integer>(width,check_size+scale(6)+extent(message_label(check.label).text));
    const Integer button_height=std::max<Integer>(scale(23),line_height+scale(8));
    Integer button_width=0;
    std::vector<Integer> button_widths;
    for (const auto& choice:m_choices) {
        // Py と同様、明示的な選択肢の幅は wx.Button の寸法へ直接渡す。 システ
        // ム DPI の寸法を使うのは、ネイティブの推奨サイズとサイザーの余白だけとする。
        Integer item=choice.width>0?choice.width:
            std::max<Integer>(scale(75),extent(message_label(choice.label).text)+scale(16));
        button_widths.push_back(item); button_width+=item;
    }
    if (!m_choices.empty()) button_width+=(static_cast<Integer>(m_choices.size())-1)*margin;
    width=std::max<Integer>(1,std::max(width,button_width)+2*margin);
    m_text_rect={margin,margin,text_width,m__textheight};
    Integer y=margin+m__textheight+margin;
    m_check_rects.clear();m_choice_rects.clear();m_separator_rect={};
    for (std::size_t i=0;i<m_check_options.size();++i) {
        if (i) y+=scale(2);
        m_check_rects.push_back({margin,y,width-2*margin,check_height});
        y+=check_height;
    }
    if (!m_check_options.empty()) y+=margin;
    if (!m_choices.empty()) {
        m_separator_rect={0,y,width,std::max<Integer>(1,scale(1))};
        y+=m_separator_rect.height+margin;
        Integer x=width-margin-button_width;
        for (const auto item:button_widths) {
            m_choice_rects.push_back({x,y,item,button_height});
            x+=item+margin;
        }
        y+=button_height+margin;
    }
    // Python の sizer.Fit と同様、構築時の初期寸法を置き換える。 355×150
    // の固定最小寸法、非表示のチェックボックス用の空間、空のボタン用フッターは残さない。
    bounds={0,0,width,std::max<Integer>(1,y)};
    get_ui_backend()->layout(this);
}

::cw::lib::ui::SystemDialogMetrics SysMessage::system_metrics() const
{
    auto result = ::cw::lib::ui::system_dialog_metrics();
    if (!m_game_window_scale) return result;

    // この選択的な拡大は、拡大済みの CardWirth ウィンドウ
    // 内で表示するゲーム所有の内容に使う。 デスクトップ用の寸法を一
    // 体として拡縮し、配置・折り返し・行高・文字形の整合性を保つ。
    const auto scale = std::max<::cw::lib::Real>(::cw::g_UP_WIN, 1.0);
    result.scale *= scale;
    result.font.pixels = std::max<::cw::lib::Integer>(
        1, static_cast<::cw::lib::Integer>(std::lround(result.font.pixels * scale)));
    result.line_height = std::max<::cw::lib::Integer>(
        1, static_cast<::cw::lib::Integer>(std::lround(result.line_height * scale)));
    return result;
}

void SysMessage::initialize(std::shared_ptr<::cw::lib::Window> arg_parent, ::cw::lib::String arg_name, ::cw::lib::String arg_text, std::vector<MessageChoice> arg_choices, std::vector<MessageCheckBox> arg_checkboxes)
{
    dialog_retained_surface().reset();
    native_handle = arg_parent ? arg_parent->native_handle : 0;
    parent_window = arg_parent;
    m_title = std::move(arg_name);
    title = m_title;
    m_basetext = std::move(arg_text);
    m_system_metrics = system_metrics();
    m_wrapped_text = wrap_pixel_text(m_basetext, m_system_metrics.scaled(345), m_system_metrics.font);
    m_text = m_wrapped_text;
    m__st_text = ::cw::lib::Value(m_wrapped_text);
    m__stl = ::cw::lib::Value(false);
    m__textheight = line_count(m_text);
    m_cwpy_debug = true;
    cwpy_debug = m_cwpy_debug;
    static_cast<::cw::lib::Window&>(*this).copy_detail = [this] { copy_detail(); };
    m_choices = std::move(arg_choices);
    m_check_options = std::move(arg_checkboxes);
    m__check_table.clear();
    m__checkboxes.clear();
    m_closed = false;
    m_detailtext.clear();
    m_focus_index = 0;
    visible = true;
    m_return_code = ID_CANCEL;
    for (std::size_t index = 0; index < m_check_options.size(); ++index) {
        const auto& item = m_check_options[index];
        m__check_table[item.key] = item.value;
        auto checkbox = std::make_shared<::cw::lib::Window>();
        checkbox->id = static_cast<::cw::lib::Integer>(index);
        checkbox->title = item.label;
        m__checkboxes.emplace_back(item.key, std::move(checkbox));
    }
    populate_sys_buttons(*this);
    m__stl = ::cw::lib::Value(!m_choices.empty());
    sysmessage_callback_context = this;
    _do_layout();
    get_ui_backend()->show_sys_message(this);
}

void SysMessage::close_with_result(::cw::lib::Integer result_id)
{
    m_return_code = result_id;
    m_closed = true;
    visible = false;
    get_ui_backend()->close(this);
}

void SysMessage::set_check(const ::cw::lib::String& key, bool value)
{
    if (m_closed) return;
    m__check_table.at(key) = value;
    for (auto& checkbox : m_check_options) if (checkbox.key == key) checkbox.value = value;
    get_ui_backend()->layout(this);
    request_dialog_presentation_refresh();
}

bool SysMessage::select_choice(::cw::lib::Integer choice_id)
{
    if (m_closed) return false;
    for (const auto& choice : m_choices) {
        if (choice.id == choice_id && choice.enabled) {
            close_with_result(choice_id);
            return true;
        }
    }
    return false;
}

::cw::dialog::DialogPresentation SysMessage::dialog_presentation()
{
    using namespace ::cw::lib;
    using Action = ::cw::dialog::DialogPresentationAction;
    const auto& metrics=m_system_metrics;
    const auto& font=metrics.font;
    const auto scale=[&](Integer value){return metrics.scaled(value);};
    const Size surface_size{std::max<Integer>(1,bounds.width),std::max<Integer>(1,bounds.height)};
    auto& retained=dialog_retained_surface();
    const bool full=retained.needs_full(surface_size);
    if(full) retained.allocate(surface_size,{240,240,240,255});
    auto surface=retained.surface;
    ::cw::dialog::DialogPresentation result;
    result.route="SYSMESSAGE";result.title=m_title;
    const auto draw=[&](const String& text,Integer x,Integer y) {
        if(full) ui::draw_label(surface,text,x,y,font.pixels,font.bold,{0,0,0},font.face, font.italic);
    };
    Integer y=m_text_rect.y;
    std::istringstream input(m_wrapped_text);
    for (String line;std::getline(input,line);) {
        draw(line,m_text_rect.x,y);y+=metrics.line_height;
    }
    for (std::size_t i=0;i<m_check_options.size() && i<m_check_rects.size();++i) {
        const auto& option=m_check_options[i];
        const auto& rect=m_check_rects[i];
        const auto size=scale(13);
        const Rect box{rect.x,rect.y+(rect.height-size)/2,size,size};
        const auto label=message_label(option.label).text;
        draw(label,box.x+box.width+scale(6),rect.y+(rect.height-metrics.line_height)/2);
        result.controls.push_back({Action::Toggle,static_cast<int>(i),
            static_cast<float>(rect.x),static_cast<float>(rect.y),static_cast<float>(rect.width),
            static_cast<float>(rect.height),!m_closed,false,label,{}});
    }
    if (full && !m_choices.empty()) ui::fill_rect(surface,m_separator_rect,{160,160,160,255});
    Rect choice_focus{0,0,1,1};
    for (std::size_t i=0;i<m_choices.size() && i<m_choice_rects.size();++i) {
        const auto& rect=m_choice_rects[i];
        const auto label=message_label(m_choices[i].label).text;
        if(full) ui::draw_button_face(surface,rect);
        const auto width=ui::label_width(label,font.pixels,font.bold,font.face, font.italic);
        if(full) ui::draw_label(surface,label,rect.x+std::max<Integer>(scale(4),(rect.width-width)/2),
             rect.y+(rect.height-metrics.line_height)/2,font.pixels,font.bold,
             m_choices[i].enabled ? std::tuple<Integer,Integer,Integer>{0,0,0} : std::tuple<Integer,Integer,Integer>{128,128,128},font.face,font.italic);
        result.controls.push_back({Action::Select,static_cast<int>(i),
            static_cast<float>(rect.x),static_cast<float>(rect.y),static_cast<float>(rect.width),
            static_cast<float>(rect.height),!m_closed && m_choices[i].enabled,false,label,m_choices[i].description});
        if (!m_closed && m_choices[i].enabled && m_focus_index==static_cast<int>(m_check_options.size()+i))
            choice_focus=rect;
    }
    if(full) retained.capture_base();
    std::vector<bool> check_changed(m_check_options.size(),false);
    for(std::size_t i=0;i<m_check_options.size() && i<m_check_rects.size();++i) {
        const auto signature=(static_cast<std::uint64_t>(m_check_options[i].value)<<32U) ^
            (static_cast<std::uint64_t>(m_closed)<<16U) ^
            static_cast<std::uint64_t>(m_focus_index==static_cast<int>(i));
        check_changed[i]=retained.component_changed("sysmessage.check."+std::to_string(i),m_check_rects[i],signature);
    }
    const auto choice_signature=(static_cast<std::uint64_t>(m_closed)<<32U) ^
        static_cast<std::uint64_t>(static_cast<std::uint32_t>(m_focus_index+1));
    const bool choice_changed=retained.component_changed("sysmessage.choice.focus",choice_focus,choice_signature);
    retained.restore_dirty();surface=retained.surface;
    for(std::size_t i=0;i<check_changed.size() && i<m_check_rects.size();++i) if(check_changed[i]) {
        const auto& rect=m_check_rects[i];const auto size=scale(13);
        const Rect box{rect.x,rect.y+(rect.height-size)/2,size,size};
        ui::draw_checkbox(surface,box,m_check_options[i].value,!m_closed);
        const auto label=message_label(m_check_options[i].label).text;
        ui::draw_label(surface,label,box.x+box.width+scale(6),
            rect.y+(rect.height-metrics.line_height)/2,font.pixels,font.bold,{0,0,0},font.face, font.italic);
        if(!m_closed && m_focus_index==static_cast<int>(i))
            draw_message_focus(surface,rect,scale(2),std::max<Integer>(1,scale(1)));
    }
    if(choice_changed && !m_closed && choice_focus.width>1)
        draw_message_focus(surface,choice_focus,scale(3),std::max<Integer>(1,scale(1)));
    retained.finish();
    result.surface=std::move(surface);result.damage=retained.damage;result.surface_revision=retained.revision;
    return result;
}
void SysMessage::dialog_presentation_action(
    const ::cw::dialog::DialogPresentationAction action, const int index)
{
    using Action = ::cw::dialog::DialogPresentationAction;
    if (m_closed) return;
    if (action == Action::Cancel) {
        // Py と同様、ボタンがある場合だけキャンセル入力を割り当てる。
        if (m_choices.empty()) return;
        OnCancel(nullptr);
    } else if (action == Action::Toggle && index >= 0 &&
               static_cast<std::size_t>(index) < m_check_options.size()) {
        const auto& option = m_check_options[static_cast<std::size_t>(index)];
        m_focus_index = index;
        set_check(option.key, !option.value);
    } else if (action == Action::Accept && !m_choices.empty()) {
        const int choice = m_focus_index - static_cast<int>(m_check_options.size());
        select_choice(m_choices[std::clamp(choice, 0, static_cast<int>(m_choices.size()) - 1)].id);
    } else if ((action == Action::Select || action == Action::Activate) && index >= 0 &&
               static_cast<std::size_t>(index) < m_choices.size() && m_choices[index].enabled) {
        // Activate は、ボタンへの素早い連続タップを表す。Message の処理に合わせる。
        m_focus_index = static_cast<int>(m_check_options.size()) + index;
        select_choice(m_choices[static_cast<std::size_t>(index)].id);
    }
}

bool SysMessage::dialog_presentation_finished() const noexcept
{
    return m_closed;
}

void SysMessage::dialog_presentation_key(DialogPresentationKey key, bool pressed,
                                         DialogPresentationModifiers modifiers)
{
    if (!pressed || m_closed) return;
    std::vector<String> labels;
    for (const auto& check:m_check_options) labels.push_back(check.label);
    for (const auto& choice:m_choices) labels.push_back(choice.enabled ? choice.label : String{});
    if (message_mnemonic(labels,m_focus_index,key,modifiers,[this](int index) {
            const auto count=static_cast<int>(m_check_options.size());
            dialog_presentation_action(index<count?DialogPresentationAction::Toggle:DialogPresentationAction::Select,
                                       index<count?index:index-count);
        })) { request_dialog_presentation_refresh(); return; }
    if (key == DialogPresentationKey::C && (modifiers & DialogPresentationModifierControl)) {
        copy_detail(); return;
    }
    if (move_message_focus(m_focus_index, static_cast<int>(m_check_options.size() + m_choices.size()),
                           key, modifiers)) {
        for (std::size_t count = 0; count < m_choices.size() &&
             m_focus_index >= static_cast<int>(m_check_options.size()) &&
             !m_choices[m_focus_index - m_check_options.size()].enabled; ++count)
            move_message_focus(m_focus_index, static_cast<int>(m_check_options.size() + m_choices.size()), key, modifiers);
        request_dialog_presentation_refresh(); return;
    }
    if (modifiers) return;
    if (key == DialogPresentationKey::Space && m_focus_index >= 0 &&
        m_focus_index < static_cast<int>(m_check_options.size()))
        dialog_presentation_action(DialogPresentationAction::Toggle, m_focus_index);
    else if (key == DialogPresentationKey::Space || key == DialogPresentationKey::Enter)
        dialog_presentation_action(DialogPresentationAction::Accept, -1);
}

void SysMessage::dialog_presentation_complete()
{
    // 共通モデルの完了状態は close_with_result() が管理する。
}

void ErrorMessage::initialize(std::shared_ptr<::cw::lib::Window> arg_parent, ::cw::lib::String arg_text)
{
    get_ui_backend()->play_sound("error");
    Message::initialize(std::move(arg_parent), localized_message("error_message", "エラーメッセージ"), std::move(arg_text), 2, std::nullopt);
}

void YesNoCancelMessage::initialize(std::shared_ptr<::cw::lib::Window> arg_parent, ::cw::lib::String arg_name, ::cw::lib::String arg_text)
{
    Message::initialize(std::move(arg_parent),std::move(arg_name),std::move(arg_text),3,
        std::vector<MessageChoice>{{"はい",ID_YES,system_scale(105),{}},{"いいえ",ID_NO,system_scale(105),{}},{"キャンセル",ID_CANCEL,system_scale(105),{} }});
}

void YesNoMessage::initialize(std::shared_ptr<::cw::lib::Window> arg_parent, ::cw::lib::String arg_name, ::cw::lib::String arg_text)
{
    Message::initialize(std::move(arg_parent),std::move(arg_name),std::move(arg_text),1,std::nullopt);
}

::cw::lib::Integer SysMessage___init___extent_w(::cw::lib::String arg_s)
{
    const auto font = ::cw::lib::ui::system_dialog_metrics().font;
    return ::cw::lib::ui::label_width(arg_s, font.pixels, font.bold, font.face, font.italic);
}

void SysMessage___init___func(::cw::lib::String arg_key)
{
    sysmessage_callback_key = std::move(arg_key);
    if (sysmessage_callback_context && sysmessage_callback_context->m__check_table.contains(sysmessage_callback_key))
        sysmessage_callback_context->set_check(sysmessage_callback_key,
            sysmessage_callback_context->m__check_table.at(sysmessage_callback_key));
}

void SysMessage___init___func_OnCheck(std::shared_ptr<::cw::lib::Window> arg_event)
{
    if (!sysmessage_callback_context) return;
    if (arg_event) {
        sysmessage_callback_context->OnCheck(arg_event);
    } else if (!sysmessage_callback_key.empty() &&
               sysmessage_callback_context->m__check_table.contains(sysmessage_callback_key)) {
        sysmessage_callback_context->set_check(sysmessage_callback_key,
            !sysmessage_callback_context->m__check_table.at(sysmessage_callback_key));
    }
}

void main()
{
}

}
