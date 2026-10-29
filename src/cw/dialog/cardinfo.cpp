// UIの実装は一つに統一し、
// 別経路の実装を設けない。
#include "cw/dialog/cardinfo.hpp"
#include "cw/animatedimage.hpp"
#include "cw/cw.hpp"
#include "cw/frame.hpp"
#include "cw/lib/ui_surface.hpp"
#include "cw/imageretouch.hpp"
#include "cw/setting.hpp"
#include "cw/thread.hpp"
#include "cw/util.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <map>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>

namespace cw::dialog::cardinfo {

namespace {
using Item = CardInfoItem;

bool same_item(const Item& lhs, const Item& rhs)
{
    return std::visit([](const auto& left, const auto& right) {
        using Left = std::decay_t<decltype(left)>;
        using Right = std::decay_t<decltype(right)>;
        if constexpr (std::is_same_v<Left, Right>) return left.get() == right.get();
        else return false;
    }, lhs, rhs);
}

std::optional<std::size_t> find_item(const std::vector<Item>& items, const Item& target)
{
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (same_item(items[i], target)) return i;
    }
    return std::nullopt;
}

std::optional<Item> item_from_value(const ::cw::lib::Value& value)
{
    const auto* object = std::get_if<::cw::lib::ObjectPtr>(&value.storage());
    if (!object || !*object) return std::nullopt;
    if (const auto card = std::dynamic_pointer_cast<::cw::sprite::card::MenuCard>(*object)) return Item{card};
    if (const auto card = std::dynamic_pointer_cast<::cw::header::CardHeader>(*object)) return Item{card};
    if (const auto card = std::dynamic_pointer_cast<::cw::header::InfoCardHeader>(*object)) return Item{card};
    return std::nullopt;
}

std::shared_ptr<::cw::image::CardImage> card_image(const Item& item)
{
    return std::visit([](const auto& card) -> std::shared_ptr<::cw::image::CardImage> {
        return card ? card->get_cardimg() : nullptr;
    }, item);
}

::cw::lib::String cardinfo_message(const ::cw::lib::String& key,
                                   const ::cw::lib::String& fallback)
{
    if (::cw::g_cwpy && ::cw::g_cwpy->m_setting && ::cw::g_cwpy->m_setting->m_msgs)
        return ::cw::g_cwpy->m_setting->m_msgs->get(key, fallback);
    return fallback;
}

::cw::lib::String item_name(const Item& item)
{
    return std::visit([](const auto& card) -> ::cw::lib::String {
        if (!card) return {};
        using Card = std::decay_t<decltype(card)>;
        if constexpr (std::is_same_v<Card, std::shared_ptr<::cw::sprite::card::MenuCard>>) return card->m_name;
        else return card->m_name;
    }, item);
}

::cw::lib::String item_desc(const Item& item)
{
    return std::visit([](const auto& card) -> ::cw::lib::String { return card ? card->m_desc : ::cw::lib::String{}; }, item);
}

bool item_negaflag(const Item& item)
{
    return std::visit([](const auto& card) { return card && card->m_negaflag; }, item);
}

void set_item_negaflag(const Item& item, bool value)
{
    std::visit([value](const auto& card) { if (card) card->m_negaflag = value; }, item);
}

std::shared_ptr<::cw::lib::Bitmap> item_bitmap(const Item& item)
{
    return std::visit([](const auto& card) -> std::shared_ptr<::cw::lib::Bitmap> {
        if (!card) return nullptr;
        using Card = std::decay_t<decltype(card)>;
        if constexpr (std::is_same_v<Card, std::shared_ptr<::cw::sprite::card::MenuCard>>) {
            const auto image = card->get_cardimg();
            return image ? image->get_cardwxbmp(
                ::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(card)),
                std::nullopt) : nullptr;
        } else if constexpr (std::is_same_v<Card, std::shared_ptr<::cw::header::CardHeader>>) {
            return card->get_cardwxbmp(std::nullopt);
        } else {
            return card->get_cardwxbmp(std::nullopt);
        }
    }, item);
}

}

namespace {
int scaled_win(int value)
{
    const auto scaled = ::cw::wins(::cw::lib::Value(value));
    if (const auto* integer = std::get_if<::cw::lib::Integer>(&scaled.storage())) return static_cast<int>(*integer);
    return value;
}

int cardinfo_bottom_panel_height()
{
    // 下部バーとボタンを kDialogFooterExtraHeight だけ高くする。
    const int button_h = scaled_win(30 + ::cw::dialog::kDialogFooterExtraHeight);
    const int close_h = scaled_win(24 + ::cw::dialog::kDialogFooterExtraHeight);
    const int margin = scaled_win(3);
    return std::max(button_h, close_h + margin * 2);
}

std::pair<int, int> cardinfo_client_size(const CardInfo& dialog)
{
    return {
        static_cast<int>(dialog.m_textwidth) + scaled_win(152),
        static_cast<int>(dialog.m_textheight) + scaled_win(42) + cardinfo_bottom_panel_height(),
    };
}

::cw::lib::ui::FontStyle cardinfo_font(const char* role,int pixels)
{
    auto font=::cw::lib::ui::configured_font_scaled(role,pixels);
    font.pixels=std::max<::cw::lib::Integer>(1,font.pixels-1); // Python の get_wxfont における wx3 用の調整。
    return font;
}
std::shared_ptr<::cw::lib::Surface> cardinfo_label(const ::cw::lib::String& text,
    const ::cw::lib::ui::FontStyle& style)
{
    if(text.empty()) return {};
    ::cw::imageretouch::Font font; font.initialize(style.face,-style.pixels,style.bold,style.italic);
    return font.render(text,true,{0,0,0});
}
bool measure_cardinfo_text_metrics(CardInfo& dialog)
{
    const auto style=cardinfo_font("datadesc",13);
    const auto& old=dialog.m_layout_font;
    if(dialog.m_layout_scale==::cw::g_UP_WIN && old.face==style.face && old.pixels==style.pixels &&
       old.bold==style.bold && old.italic==style.italic) return false;
    ::cw::imageretouch::Font font; font.initialize(style.face,-style.pixels,style.bold,style.italic);
    ::cw::lib::String sample;
    for(int i=0;i<19;++i) sample+="―";
    const auto [width,height]=font.size(sample);
    dialog.m_textwidth=std::max<::cw::lib::Integer>(1,width);
    dialog.m_description_lineheight=std::max<::cw::lib::Integer>(1,height);
    dialog.m_textheight=dialog.m_description_lineheight*9;
    dialog.m_layout_scale=::cw::g_UP_WIN;dialog.m_layout_font=style;
    return true;
}
void cardinfo_adjusted(const std::shared_ptr<::cw::lib::Surface>& surface,
    const std::shared_ptr<::cw::lib::Surface>& label,::cw::lib::Integer x,::cw::lib::Integer y,
    ::cw::lib::Integer width)
{
    if(!label || width<=0) return;
    // Python と同様、draw_adjusted() の前に文字の背後の枠線を消す。
    ::cw::lib::ui::fill_rect(surface,{x,y,width,label->size.height},{240,240,240,255});
    const auto fitted=label->size.width>width
        ?::cw::image::smoothscale_card(label,{width,label->size.height},true):label;
    ::cw::lib::ui::blend_surface(surface,fitted,x,y);
}

class CommonCardInfoUiBackend : public CardInfoUiBackend {
public:
    void open(CardInfo& dialog, const std::shared_ptr<::cw::lib::Window>& parent) override
    {
        (void)parent;
        dialog.visible = false;
    }
    void refresh(CardInfo& dialog) override
    {
        dialog.request_dialog_presentation_refresh();
        if (::cw::g_cwpy && ::cw::g_cwpy->m_backend)
            ::cw::g_cwpy->m_backend->invalidate(*::cw::g_cwpy, dialog.bounds);
    }
    void layout(CardInfo& dialog) override
    {
        const auto [client_w, client_h] = cardinfo_client_size(dialog);
        dialog.bounds.width = client_w;
        dialog.bounds.height = client_h;
    }
    void close(CardInfo& dialog) override { dialog.visible = false; }
    void present(CardInfo& dialog) override { dialog.visible = true; }
    ::cw::lib::Integer show_modal(CardInfo& dialog) override
    {
        present(dialog);
        while (dialog.visible && !dialog.m__modal_ended) {
            if (::cw::g_cwpy && ::cw::g_cwpy->m_backend) ::cw::g_cwpy->m_backend->pump(*::cw::g_cwpy);
            std::this_thread::sleep_for(std::chrono::milliseconds(8));
        }
        return dialog.m_modal_result;
    }
};

}

void CardInfoUiBackend::open(CardInfo& arg_dialog, const std::shared_ptr<::cw::lib::Window>& arg_parent)
{
    CommonCardInfoUiBackend{}.open(arg_dialog, arg_parent);
}
void CardInfoUiBackend::refresh(CardInfo& arg_dialog) { CommonCardInfoUiBackend{}.refresh(arg_dialog); }
void CardInfoUiBackend::layout(CardInfo& arg_dialog) { CommonCardInfoUiBackend{}.layout(arg_dialog); }
void CardInfoUiBackend::close(CardInfo& arg_dialog) { CommonCardInfoUiBackend{}.close(arg_dialog); }
void CardInfoUiBackend::present(CardInfo& arg_dialog) { CommonCardInfoUiBackend{}.present(arg_dialog); }
::cw::lib::Integer CardInfoUiBackend::show_modal(CardInfo& arg_dialog) { return CommonCardInfoUiBackend{}.show_modal(arg_dialog); }

std::shared_ptr<CardInfoUiBackend> get_ui_backend()
{
    // CardInfo の共通モデルを、製品 UI の唯一の実装とする。
    static const auto controller = std::make_shared<CommonCardInfoUiBackend>();
    return controller;
}

CardInfo::~CardInfo()
{
    _stop_animtimer();
    if (const auto backend = get_ui_backend()) backend->close(*this);
}

void CardInfo::initialize(std::shared_ptr<::cw::lib::Window> arg_parent, ::cw::lib::String arg_scedir)
{
    native_handle = arg_parent ? arg_parent->native_handle : 0;
    parent_window = arg_parent;
    m_scedir = std::move(arg_scedir);
    dialog_presentation_pointer_cancel();m_published_controls.clear();m_focus_button=1;m_keyboard_focus=false;
    dialog_retained_surface().reset();
    m_layout_scale=-1;m_side_navigation.reset();
    m_cwpy_debug = false;
    cwpy_debug = m_cwpy_debug;
    static_cast<::cw::lib::Window&>(*this).copy_detail = [this] { copy_detail(); };
    title = cardinfo_message("card_information", "カード情報");
    visible = false;
    bounds.width = scaled_win(380);
    bounds.height = scaled_win(200);
    measure_cardinfo_text_metrics(*this);
    m__modal_ended = false;
    m_modal_result = kIdCancel;
    m_copy_text.clear();
    if (!m_list.empty()) {
        if (m_index < 0 || m_index >= static_cast<::cw::lib::Integer>(m_list.size())) m_index = 0;
        if (!m_selection) m_selection = m_list[static_cast<std::size_t>(m_index)];
    }
    m__dialog_anim_owner = "Dialog:" + std::to_string(reinterpret_cast<std::uintptr_t>(this));
    m__animtimer = std::make_shared<::cw::lib::Timer>();
    _do_layout();
    _sync_dialog_animated_paths();
    draw(false);
    if (const auto backend = get_ui_backend()) backend->open(*this, arg_parent);
}

void CardInfo::OnCopyDetail(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    copy_detail();
}

void CardInfo::copy_detail()
{
    if(m__modal_ended || !m_selection) return;
    m_copy_text.clear();
    if (::cw::g_cwpy) ::cw::g_cwpy->play_sound("equipment", false, 100, 1, 0, 0, false);
    m_copy_text=detail_text();
    if(m_copy_writer) m_copy_writer(m_copy_text);
}

::cw::lib::String CardInfo::detail_text()
{
    if(!m_selection) return {};
    const auto source = get_source();
    return "[ " + item_name(*m_selection) + " ]" + (source.empty()?::cw::lib::String{}:" "+source) + "\n" + get_desc() + "\n";
}

void CardInfo::OnClickLeftBtn(std::shared_ptr<::cw::lib::Window> arg_event) { (void)arg_event; }
void CardInfo::OnClickRightBtn(std::shared_ptr<::cw::lib::Window> arg_event) { (void)arg_event; }

void CardInfo::OnMouseWheel(std::shared_ptr<::cw::lib::Window> arg_event)
{
    if (m__modal_ended || m_list.size() <= 1) return;
    const auto direction = arg_event ? arg_event->bounds.y : 1;
    // Python と同様、ホイール操作は最も派生したページボタンへ送る。
    // 公開 API の回転量ゼロでは DOWN へ進む動作を維持する。
    // 入力元のゼロ値は、この移動処理に届く前の共通スクロール入口で除く。
    if (direction > 0) OnClickLeftBtn(arg_event);
    else OnClickRightBtn(arg_event);
}

void CardInfo::OnCancel(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    if(m__modal_ended) return;
    _stop_animtimer();
    if (::cw::g_cwpy) ::cw::g_cwpy->play_sound("click", false, 100, 1, 0, 0, false);
    _end_modal(kIdCancel);
    visible = false;
    if (const auto backend = get_ui_backend()) backend->close(*this);
    if (m_close_frame_dialog) ::cw::frame::Frame_kill_dlg_if_model(this);
}

void CardInfo::dialog_presentation_context(float x, float y)
{
    (void)x; (void)y;
    // Python と同様、右ボタンを離したらクリック音を含む OnCancel を実行する。
    OnCancel(nullptr);
}

void CardInfo::OnDestroy(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    _stop_animtimer();
    _end_modal(kIdCancel);
    visible = false;
    if (const auto backend = get_ui_backend()) backend->close(*this);
}

void CardInfo::OnClose(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    if(m__modal_ended) return;
    _stop_animtimer();
    _end_modal(kIdCancel);
    visible = false;
    if (const auto backend = get_ui_backend()) backend->close(*this);
    if (m_close_frame_dialog) ::cw::frame::Frame_kill_dlg_if_model(this);
}

void CardInfo::OnCloseButton(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    if(m__modal_ended) return;
    _stop_animtimer();
    _end_modal(kIdCancel);
    visible = false;
    if (const auto backend = get_ui_backend()) backend->close(*this);
    if (m_close_frame_dialog) ::cw::frame::Frame_kill_dlg_if_model(this);
}

void CardInfo::OnAnimate(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    if (m__modal_ended || !m_animtimer_running || !m_selection) return;
    _sync_dialog_animated_paths();
    const auto image = card_image(*m_selection);
    // アニメーションのタイマー処理では現在のカードだけを再描画する。draw(true)
    // はページ移動用で、 ページ音の再生と項目の再選択を伴う。Python の
    // タイマーはカードパネルの Refresh だけを呼ぶため、 33ms ごと
    // のコールバックで移動処理を使うと、効果音と選択の副作用を繰り返してしまう。
    if (image && image->update_animation()) {
        const bool negaflag = item_negaflag(*m_selection);
        set_item_negaflag(*m_selection, false);
        m_rendered_card = item_bitmap(*m_selection);
        set_item_negaflag(*m_selection, negaflag);
        if (!invalidate_dialog_component("card-frame")) {
            const auto [width, height] = cardinfo_client_size(*this);
            (void)width;
            invalidate_dialog_presentation({0, 0, scaled_win(113),
                height - cardinfo_bottom_panel_height()});
        }
    }
}

void CardInfo::_sync_dialog_animated_paths()
{
    std::map<::cw::lib::Integer, std::shared_ptr<::cw::image::CardImage>> current;
    std::set<::cw::lib::String> paths;
    if (m_selection) {
        if (const auto image = card_image(*m_selection)) {
            image->update_animated_paths();
            // Python と同様、この表は id(cardimg) に相当する識
            // 別子をキーにする。 固定キーではページを変えても current.count(1)
            // が真のままとなり、前の画像が登録されたままになる。
            const auto key = static_cast<::cw::lib::Integer>(
                reinterpret_cast<std::uintptr_t>(image.get()));
            const auto animated = image->get_animated_paths();
            if (!animated.empty()) {
                current.emplace(key, image);
                paths.insert(animated.begin(), animated.end());
            }
        }
    }
    for (const auto& [key, image] : m__dialog_anim_cardimgs) {
        if (!current.count(key) && image) image->release_animated_paths();
    }
    m__dialog_anim_cardimgs = std::move(current);
    m__dialog_anim_paths = std::move(paths);
    // Python はアニメーション用のパスがなくても 33ms ごとに確認する
    // ため、以前なかった外部画像が操作なしで表示される。 ネイティブ版では静止画
    // の再描画とファイル確認を続けないよう、デコーダー用パスを持つ選択中の CardImage
    // だけを定期確認する。 ページ変更や明示的な再描画でパスを同期し直す
    // 。静止外部画像が新たに存在するだけでは、このタイマーを開始しない。
    m_animtimer_running = !m__dialog_anim_paths.empty();
}

void CardInfo::_stop_animtimer()
{
    dialog_presentation_pointer_cancel();
    m_animtimer_running = false;
    m__animtimer.reset();
    m__dialog_anim_paths.clear();
    if (!m__dialog_anim_owner.empty()) ::cw::animatedimage::release(m__dialog_anim_owner);
    for (const auto& [unused, image] : m__dialog_anim_cardimgs) {
        (void)unused;
        if (image) image->release_animated_paths();
    }
    m__dialog_anim_cardimgs.clear();
}

void CardInfo::OnPaint(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    draw(false);
}

::cw::lib::String CardInfo::get_source()
{
    if (!m_selection) return {};
    const auto scenario = std::visit([](const auto& card) -> ::cw::lib::String {
        if (!card) return {};
        using Card = std::decay_t<decltype(card)>;
        if constexpr (std::is_same_v<Card, std::shared_ptr<::cw::header::InfoCardHeader>>) {
            if (const auto* text = std::get_if<::cw::lib::String>(&card->m_scenario.storage())) return *text;
            return {};
        } else return card->m_scenario;
    }, *m_selection);
    const auto author = std::visit([](const auto& card) -> ::cw::lib::String {
        if (!card) return {};
        using Card = std::decay_t<decltype(card)>;
        if constexpr (std::is_same_v<Card, std::shared_ptr<::cw::header::InfoCardHeader>>) {
            if (const auto* text = std::get_if<::cw::lib::String>(&card->m_author.storage())) return *text;
            return {};
        } else return card->m_author;
    }, *m_selection);
    return scenario + (author.empty() ? ::cw::lib::String{} : "(" + author + ")");
}

::cw::lib::String CardInfo::get_desc()
{
    if (!m_selection) return {};
    auto text = std::visit([](const auto& card) -> ::cw::lib::String {
        return card ? card->m_desc : ::cw::lib::String{};
    }, *m_selection);
    text = ::cw::util::txtwrap(std::move(text), 1, 30, "", true, nullptr);
    std::size_t lines = 0;
    std::size_t cut = text.size();
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\n' && ++lines == 9) { cut = i; break; }
    }
    if (cut < text.size()) text.erase(cut);
    return text;
}

void CardInfo::draw(bool arg_update)
{
    if(m__modal_ended) return;
    if (arg_update) {
        if (::cw::g_cwpy) ::cw::g_cwpy->play_sound("page", false, 100, 1, 0, 0, false);
        if (!m_list.empty()) m_selection = m_list[static_cast<std::size_t>(m_index)];
    }
    _sync_dialog_animated_paths();
    m_rendered_card.reset();
    m_rendered_name.clear();
    m_rendered_desc.clear();
    m_rendered_source.clear();
    if (m_selection) {
        const bool negaflag = item_negaflag(*m_selection);
        set_item_negaflag(*m_selection, false);
        m_rendered_card = item_bitmap(*m_selection);
        set_item_negaflag(*m_selection, negaflag);
        m_rendered_name = item_name(*m_selection);
        m_rendered_desc = get_desc();
        m_rendered_source = get_source();
    }
    invalidate_dialog_presentation();
    if (const auto backend = get_ui_backend()) backend->refresh(*this);
}

namespace {
template <typename T>
std::shared_ptr<T> cardinfo_object_value(const ::cw::lib::Value& value) {
    const auto* object = std::get_if<::cw::lib::ObjectPtr>(&value.storage());
    return object && *object ? std::dynamic_pointer_cast<T>(*object) : nullptr;
}

std::shared_ptr<::cw::lib::Bitmap> cardinfo_move_bitmap(const char* key) {
    if (!::cw::g_cwpy || !::cw::g_cwpy->m_rsrc) return {};
    const auto table = cardinfo_object_value<::cw::setting::ResourceTable>(
        ::cw::g_cwpy->m_rsrc->m_buttons);
    if (!table || !table->contains(::cw::lib::Value(key))) return {};
    try {
        return cardinfo_object_value<::cw::lib::Bitmap>(
            table->get_item(::cw::lib::Value(key)));
    } catch (...) {
        return {};
    }
}

void cardinfo_control(::cw::dialog::DialogPresentation& presentation,
                      const std::shared_ptr<::cw::lib::Surface>& surface,
                      const ::cw::lib::Rect rect, const ::cw::lib::String& label,
                      const ::cw::dialog::DialogPresentationAction action,
                      const bool enabled,
                      const std::shared_ptr<::cw::lib::Bitmap>& image = {},
                      const bool paint = true) {
    if (paint) {
    ::cw::lib::ui::draw_button_face(surface, rect, {enabled});
    if (image) {
        const auto scaled = ::cw::lib::ui::resource_surface_for_window(image);
        ::cw::lib::ui::blend_surface(
            surface, scaled, rect.x + (rect.width - scaled->size.width) / 2,
            rect.y + (rect.height - scaled->size.height) / 2);
    } else {
        const auto font = ::cw::lib::ui::configured_font_scaled("button", 13);
        const int pixels = font.pixels;
        const int width = ::cw::lib::ui::label_width(
            label, pixels, font.bold, font.face);
        auto clip=std::make_shared<::cw::lib::Surface>();clip->size={rect.width,rect.height};
        clip->rgba.assign(static_cast<std::size_t>(rect.width*rect.height)*4,0);
        ::cw::lib::ui::draw_label(
            clip, label, (rect.width - width) / 2,
            (rect.height - pixels) / 2, pixels, font.bold,
            enabled ? std::tuple<::cw::lib::Integer, ::cw::lib::Integer,
                                 ::cw::lib::Integer>{0, 0, 0}
                    : std::tuple<::cw::lib::Integer, ::cw::lib::Integer,
                                 ::cw::lib::Integer>{100, 100, 100},
            font.face);
        ::cw::lib::ui::blend_surface(surface,clip,rect.x,rect.y);
    }
    }
    presentation.controls.push_back(
        {action, 0, static_cast<float>(rect.x), static_cast<float>(rect.y),
         static_cast<float>(rect.width), static_cast<float>(rect.height), enabled});
    presentation.controls.back().hover_highlight=true;
}
}  // namespace

::cw::dialog::DialogPresentation CardInfo::dialog_presentation() {
    using Action = ::cw::dialog::DialogPresentationAction;
    if(!m__modal_ended && measure_cardinfo_text_metrics(*this)) {
        dialog_presentation_pointer_cancel();
        get_ui_backend()->layout(*this);draw(false);
    }
    if(m_list.size()<=1) {dialog_presentation_pointer_cancel();m_focus_button=1;}
    if(m_page_repeat && !m__modal_ended && m_list.size()>1 &&
       m_page_repeat_timer.consume(card_input_time(),page_repeat_interval())) {
        if(m_page_repeat<0) OnClickLeftBtn(nullptr);else OnClickRightBtn(nullptr);
    }
    if(!m__modal_ended && m_animtimer_running) OnAnimate(nullptr);
    const auto [width, height] = cardinfo_client_size(*this);
    const int bottom_h = cardinfo_bottom_panel_height();
    const int content_h = height - bottom_h;
    auto& retained = dialog_retained_surface();
    const bool full = retained.needs_full({width, height});
    const bool paint_card = full || !retained.dirty.empty();
    if (full) retained.allocate({width, height}, {240, 240, 240, 255});
    else if (paint_card) {
        // ホストは最後に公開した描画面をまだ表示している場合がある。 その
        // 場でカードのピクセルを消去せず、新しい世代の描画面を公開する。
        retained.surface = std::make_shared<::cw::lib::Surface>(*retained.surface);
        retained.restore_dirty();
    }
    define_dialog_component("card-frame", {0, 0, scaled_win(113), content_h});
    auto surface = retained.surface;
    if (!full && paint_card)
        ::cw::lib::ui::fill_rect(surface, {0, 0, scaled_win(113), content_h},
                                 {240, 240, 240, 255});
    const auto title_font=cardinfo_font("paneltitle",14);
    const auto description_font=cardinfo_font("datadesc",13);
    const auto source_font=cardinfo_font("paneltitle2",14);
    if (paint_card && m_rendered_card) {
        ::cw::lib::ui::blend_surface(
            surface, m_rendered_card,
            std::max(0, (scaled_win(113) -
                         static_cast<int>(m_rendered_card->size.width)) / 2),
            std::max(0, (content_h -
                         static_cast<int>(m_rendered_card->size.height)) / 2));
    }
    const ::cw::lib::Rect box{
        scaled_win(113), scaled_win(9),
        static_cast<int>(m_textwidth) + scaled_win(30),
        static_cast<int>(m_textheight) + scaled_win(25)};
    if (full) {
        ::cw::lib::ui::draw_sunken_border(surface, box);
        if(const auto name=cardinfo_label(m_rendered_name,title_font)) {
            const auto fitted=std::min(name->size.width,box.width-scaled_win(14));
            cardinfo_adjusted(surface,name,box.x+scaled_win(9),box.y-name->size.height/2,fitted);
        }
        auto description=std::make_shared<::cw::lib::Surface>();
        description->size={std::max<::cw::lib::Integer>(1,box.width-scaled_win(28)),
            std::max<::cw::lib::Integer>(1,m_textheight)};
        description->rgba.assign(static_cast<std::size_t>(description->size.width*description->size.height)*4,0);
        std::size_t begin=0;
        for(int line=0;line<9 && begin<=m_rendered_desc.size();++line) {
            const auto end=m_rendered_desc.find('\n',begin);
            auto text=m_rendered_desc.substr(begin,end==std::string::npos?end:end-begin);
            if(!text.empty() && text.back()=='\r') text.pop_back();
            if(const auto image=cardinfo_label(text,description_font))
                ::cw::lib::ui::blend_surface(description,image,0,line*m_description_lineheight);
            if(end==std::string::npos) break;
            begin=end+1;
        }
        ::cw::lib::ui::blend_surface(surface,description,box.x+scaled_win(14),box.y+scaled_win(10));
        if(const auto source=cardinfo_label(m_rendered_source,source_font)) {
            const auto fitted=std::min(source->size.width,box.width-scaled_win(14));
            cardinfo_adjusted(surface,source,box.x+box.width-scaled_win(5)-fitted,
                box.y+box.height-source->size.height/2,fitted);
        }
        ::cw::lib::ui::fill_rect(surface, {0, content_h, width, 2},
                                 {128, 128, 128, 255});
    }
    const bool can_move = !m__modal_ended && m_list.size() > 1;
    ::cw::dialog::DialogPresentation result;
    result.route = "MENUCARDINFO";
    result.title = title.empty() ? cardinfo_message("card_information", "カード情報") : title;
    m_side_navigation.append(result,{0,0,width,content_h},can_move,1);
    const int nav = scaled_win(30);
    const int nav_h = scaled_win(30 + ::cw::dialog::kDialogFooterExtraHeight);
    cardinfo_control(result, surface, {0, content_h, nav, nav_h}, "<",
                     Action::Previous, can_move, cardinfo_move_bitmap("LMOVE"), full);
    cardinfo_control(result, surface, {width - nav, content_h, nav, nav_h}, ">",
                     Action::Next, can_move, cardinfo_move_bitmap("RMOVE"), full);
    const int close_w = scaled_win(85);
    const int close_h = scaled_win(24 + ::cw::dialog::kDialogFooterExtraHeight);
    cardinfo_control(result, surface,
        {(width - close_w) / 2, content_h + (bottom_h - close_h) / 2,
         close_w, close_h},
        cardinfo_message("close", "閉じる"), Action::Cancel, !m__modal_ended, {}, full);
    for(auto& control:result.controls)
        if(control.index==0 && (control.action==Action::Previous || control.action==Action::Next))
            control.continuous_pointer=true;
    m_published_controls=result.controls;
    if(full && m_keyboard_focus && !m__modal_ended) {
        const auto focused=m_focus_button==0?Action::Previous:m_focus_button==2?Action::Next:Action::Cancel;
        for(const auto& control:result.controls) if(control.index==0 && control.action==focused && control.enabled) {
            const int left=static_cast<int>(control.x)+scaled_win(3),top=static_cast<int>(control.y)+scaled_win(3);
            const int right=static_cast<int>(control.x+control.width)-scaled_win(3)-1;
            const int bottom=static_cast<int>(control.y+control.height)-scaled_win(3)-1;
            for(int x=left;x<=right;x+=2) for(int y:{top,bottom})
                ::cw::lib::ui::fill_rect(surface,{x,y,1,1},{0,0,0,255});
            for(int y=top;y<=bottom;y+=2) for(int x:{left,right})
                ::cw::lib::ui::fill_rect(surface,{x,y,1,1},{0,0,0,255});
        }
    }
    if (full) retained.capture_base();
    retained.finish();
    result.surface = surface;
    result.damage = retained.damage;
    result.surface_revision = retained.revision;
    result.refresh_interval_ms = platform_refresh_interval(m_animtimer_running ? 33U : 0U);
    return result;
}

void CardInfo::dialog_presentation_action(
    const ::cw::dialog::DialogPresentationAction action, const int index) {
    using Action = ::cw::dialog::DialogPresentationAction;
    if (m__modal_ended) return;
    dialog_presentation_pointer_cancel();
    if(index==0) m_focus_button=action==Action::Previous?0:action==Action::Next?2:1;
    if (action == Action::Previous && m_list.size()>1) OnClickLeftBtn(nullptr);
    else if (action == Action::Next && m_list.size()>1) OnClickRightBtn(nullptr);
    else if (action == Action::Cancel || action == Action::Accept) {
        _stop_animtimer();
        _end_modal(kIdCancel);
    }
}

::cw::dialog::ButtonRepeatTimer::Clock::time_point CardInfo::card_input_time() const noexcept
{
    return ::cw::dialog::ButtonRepeatTimer::Clock::now();
}
std::chrono::milliseconds CardInfo::page_repeat_interval() const noexcept
{
    return ::cw::dialog::dialog_page_repeat_interval();
}
void CardInfo::dialog_presentation_enter(::cw::dialog::DialogPresentationModifiers modifiers)
{
    dialog_presentation_key(::cw::dialog::DialogPresentationKey::Enter,true,modifiers);
}
void CardInfo::dialog_presentation_pointer(::cw::dialog::DialogPresentationAction action,int index,
    float x,float y,bool pressed)
{
    using Action=::cw::dialog::DialogPresentationAction;
    if(m__modal_ended) {dialog_presentation_pointer_cancel();return;}
    if(index!=0 || (action!=Action::Previous && action!=Action::Next)) {
        if(!pressed) dialog_presentation_action(action,index);
        return;
    }
    if(!pressed || m_list.size()<=1) {dialog_presentation_pointer_cancel();return;}
    const auto hit=std::find_if(m_published_controls.begin(),m_published_controls.end(),[&](const auto& control) {
        return control.index==index && control.action==action && control.enabled;
    });
    if(hit==m_published_controls.end() || !std::isfinite(x) || !std::isfinite(y) ||
       x<hit->x || y<hit->y || x>=hit->x+hit->width || y>=hit->y+hit->height) {
        m_page_repeat=0;return; // Python と同様、領域を離れたらタイマーを停止し、再び入っても再開しない。
    }
    if(m_page_capture) return;
    m_page_capture=m_page_repeat=action==Action::Previous?-1:1;
    m_focus_button=action==Action::Previous?0:2;m_keyboard_focus=false;
    if(action==Action::Previous) OnClickLeftBtn(nullptr); else OnClickRightBtn(nullptr);
    if(!m__modal_ended && m_page_capture)
        m_page_repeat_timer.start(card_input_time(),page_repeat_interval());
}
void CardInfo::dialog_presentation_pointer_cancel()
{
    m_page_repeat=0;m_page_capture=0;
}
void CardInfo::dialog_presentation_scroll(float x,float y,int lines)
{
    if(m__modal_ended || m_list.size()<=1 || !lines || !std::isfinite(x) || !std::isfinite(y)) return;
    dialog_presentation_action(lines<0?::cw::dialog::DialogPresentationAction::Previous:
        ::cw::dialog::DialogPresentationAction::Next,1);
}
bool CardInfo::dialog_presentation_needs_refresh() const noexcept
{
    return ::cw::dialog::DialogPresentationProvider::dialog_presentation_needs_refresh() ||
        (!m__modal_ended && m_list.size()>1 && m_page_repeat && m_page_repeat_timer.due(card_input_time()));
}
void CardInfo::dialog_presentation_key(::cw::dialog::DialogPresentationKey key,bool pressed,
    ::cw::dialog::DialogPresentationModifiers modifiers) {
    if(!pressed || m__modal_ended) return;
    using Key=::cw::dialog::DialogPresentationKey;
    using Action=::cw::dialog::DialogPresentationAction;
    const bool ctrl=(modifiers & ::cw::dialog::DialogPresentationModifierControl)!=0;
    const bool shift=(modifiers & ::cw::dialog::DialogPresentationModifierShift)!=0;
    if(modifiers & ::cw::dialog::DialogPresentationModifierAlt) return;
    dialog_presentation_pointer_cancel();
    if(key==Key::Left && !shift && m_list.size()>1) dialog_presentation_action(Action::Previous,1);
    else if(key==Key::Right && !shift && m_list.size()>1) dialog_presentation_action(Action::Next,1);
    else if(key==Key::C && ctrl && !shift) OnCopyDetail(nullptr);
    else if(!ctrl && (key==Key::Escape || (!shift && key==Key::Backspace) ||
        key==Key::Underscore || (shift && key==Key::Minus))) {
        if(key!=Key::Escape && ::cw::g_cwpy) ::cw::g_cwpy->play_sound("click",false, 100, 1, 0,0,false);
        dialog_presentation_action(Action::Cancel,0);
    } else if(key==Key::Tab && !ctrl) {
        m_focus_button=m_list.size()>1?(m_focus_button+(shift?2:1))%3:1;
        m_keyboard_focus=true;invalidate_dialog_presentation();
    } else if((key==Key::Enter || key==Key::Space) && !ctrl && !shift) {
        dialog_presentation_action(m_focus_button==0?Action::Previous:m_focus_button==2?Action::Next:Action::Cancel,0);
    }
}

bool CardInfo::dialog_presentation_finished() const noexcept {
    return m__modal_ended;
}

void CardInfo::dialog_presentation_complete() {
    _stop_animtimer();
    _end_modal(kIdCancel);
    visible = false;
    if (const auto backend = get_ui_backend()) backend->close(*this);
}

void CardInfo::_do_layout()
{
    const auto [client_w, client_h] = cardinfo_client_size(*this);
    const int bottom_h = cardinfo_bottom_panel_height();
    auto top = std::make_shared<::cw::lib::Panel>();
    top->visible = true;
    top->bounds = {0, 0, client_w, client_h - bottom_h};
    m_toppanel = std::static_pointer_cast<::cw::lib::Object>(top);
    auto bottom = std::make_shared<::cw::lib::Panel>();
    bottom->visible = true;
    bottom->bounds = {0, top->bounds.height, client_w, bottom_h};
    m_panel = std::static_pointer_cast<::cw::lib::Object>(bottom);
    auto left = std::make_shared<::cw::lib::Control>(); left->id = 2001; left->title = "<"; left->visible = true;
    auto right = std::make_shared<::cw::lib::Control>(); right->id = 2002; right->title = ">"; right->visible = true;
    auto close = std::make_shared<::cw::lib::Control>(); close->id = 2003; close->title = cardinfo_message("close", "閉じる"); close->visible = true;
    m_leftbtn = std::static_pointer_cast<::cw::lib::Object>(left);
    m_rightbtn = std::static_pointer_cast<::cw::lib::Object>(right);
    m_closebtn = std::static_pointer_cast<::cw::lib::Object>(close);
    m_leftpagekeyid = static_cast<::cw::lib::Integer>(2001);
    m_rightpagekeyid = static_cast<::cw::lib::Integer>(2002);
    if (const auto backend = get_ui_backend()) backend->layout(*this);
}
void CardInfo::set_parent_callbacks(std::function<void(const CardInfoItem&)> change_selection,
                                    std::function<void()> parent_draw,
                                    std::function<void(const CardInfoItem&)> update_cardpocketinfo)
{
    m_change_selection = std::move(change_selection); m_parent_draw = std::move(parent_draw); m_update_cardpocketinfo = std::move(update_cardpocketinfo);
}
void CardInfo::move_page(::cw::lib::Integer arg_delta)
{
    if (m__modal_ended || m_list.size()<=1) return;
    const auto count = static_cast<::cw::lib::Integer>(m_list.size()); m_index = (m_index + arg_delta) % count; if (m_index < 0) m_index += count;
    m_selection = m_list[static_cast<std::size_t>(m_index)];
    if (m_selection) {
        const auto selected=*m_selection;
        if (m_change_selection) m_change_selection(selected);
        if(m__modal_ended) return;
        if (m_update_cardpocketinfo) m_update_cardpocketinfo(selected);
        if(m__modal_ended) return;
    }
    if (m_parent_draw) m_parent_draw();
    if(!m__modal_ended) draw(true);
}
void CardInfo::_end_modal(::cw::lib::Integer arg_result) { if (m__modal_ended) return; dialog_presentation_pointer_cancel(); m_modal_result = arg_result; m__modal_ended = true; visible = false; }
::cw::lib::Integer CardInfo::get_modal_result() const { return m_modal_result; }
::cw::lib::String CardInfo::get_copy_text() const { return m_copy_text; }
std::optional<CardInfoItem> CardInfo::get_selection() const { return m_selection; }
::cw::lib::Integer CardInfo::show_modal()
{
    // 製品内のすべての呼び出し側は shared_ptr
    // で CardInfo を所有する。 この同じ表
    // 示プロバイダーを、実行中の共通モーダルホストへ公
    // 開する。ホストに表示を公開しないまま待機してはな
    // らない。 UI の実装は共通の一つだけとする。
    try {
        const auto model = std::static_pointer_cast<::cw::lib::Object>(shared_from_this());
        if (::cw::dialog::show_common_modal_presentation(model)) return m_modal_result;
    } catch (const std::bad_weak_ptr&) {
        // スタック所有の互換検査用インスタンスは表示できない。 削除済み
        // の専用処理ループを、二つ目の UI 経路として復活させない。
    }
    if (!m__modal_ended)
        dialog_presentation_action(::cw::dialog::DialogPresentationAction::Cancel, -1);
    return m_modal_result;
}
void MenuCardInfo::initialize(std::shared_ptr<::cw::lib::Window> arg_parent,
                              std::optional<CardInfoItem> arg_selection)
{
    if (!::cw::g_cwpy) throw std::logic_error("MenuCardInfo requires CWPy");
    m_list.clear();
    m_selection = std::move(arg_selection);
    if (!m_selection && ::cw::g_cwpy->m_selection) {
        const auto& selected = *::cw::g_cwpy->m_selection;
        if (const auto menu_card = std::dynamic_pointer_cast<::cw::sprite::card::MenuCard>(selected)) m_selection = CardInfoItem{menu_card};
        else if (const auto header_card = std::dynamic_pointer_cast<::cw::header::CardHeader>(selected)) m_selection = CardInfoItem{header_card};
    }
    for (const auto& value : ::cw::g_cwpy->get_mcards(::cw::lib::Value("visiblemenucards"), {})) {
        const auto item = item_from_value(value);
        if (!item || !std::holds_alternative<std::shared_ptr<::cw::sprite::card::MenuCard>>(*item)) continue;
        const auto& card = std::get<std::shared_ptr<::cw::sprite::card::MenuCard>>(*item);
        if (card && !card->m_desc.empty()) m_list.push_back(*item);
    }
    if (!m_selection && !m_list.empty()) m_selection = m_list.front();
    if (!m_selection) throw std::logic_error("MenuCardInfo selection is not a card");
    const auto selected_index = find_item(m_list, *m_selection);
    if (!selected_index) throw std::logic_error("selected menu card is not visible");
    m_index = static_cast<::cw::lib::Integer>(*selected_index);
    CardInfo::initialize(std::move(arg_parent));
}
void MenuCardInfo::OnClickLeftBtn(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    if (m__modal_ended || m_list.empty()) return;
    m_index = m_index == 0 ? static_cast<::cw::lib::Integer>(m_list.size()) - 1 : m_index - 1;
    m_selection = m_list[static_cast<std::size_t>(m_index)];
    if (m_change_selection) m_change_selection(*m_selection);
    if(!m__modal_ended) draw(true);
}
void MenuCardInfo::OnClickRightBtn(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    if (m__modal_ended || m_list.empty()) return;
    m_index = m_index + 1 == static_cast<::cw::lib::Integer>(m_list.size()) ? 0 : m_index + 1;
    m_selection = m_list[static_cast<std::size_t>(m_index)];
    if (m_change_selection) m_change_selection(*m_selection);
    if(!m__modal_ended) draw(true);
}
void YadoCardInfo::initialize(std::shared_ptr<::cw::lib::Window> arg_parent, std::vector<CardInfoItem> arg_clist, CardInfoItem arg_selection, ::cw::lib::String arg_scedir)
{
    m_list = std::move(arg_clist);
    if (m_list.empty()) throw std::logic_error("YadoCardInfo requires a non-empty card list");
    m_selection = std::move(arg_selection);
    m_index = 0;
    for (std::size_t i = 0; i < m_list.size(); ++i) if (same_item(m_list[i], *m_selection)) { m_index = static_cast<::cw::lib::Integer>(i); break; }
    if (!same_item(m_list[static_cast<std::size_t>(m_index)], *m_selection)) throw std::invalid_argument("selection is not in card list");
    CardInfo::initialize(std::move(arg_parent), std::move(arg_scedir));
}
void YadoCardInfo::initialize(std::shared_ptr<::cw::lib::Window> arg_parent, std::vector<::cw::lib::Value> arg_clist, ::cw::lib::Value arg_selection, ::cw::lib::String arg_scedir)
{
    std::vector<CardInfoItem> list;
    for (const auto& value : arg_clist) {
        const auto* object = std::get_if<::cw::lib::ObjectPtr>(&value.storage());
        if (!object || !*object) continue;
        if (auto menu_card = std::dynamic_pointer_cast<::cw::sprite::card::MenuCard>(*object)) list.emplace_back(menu_card);
        else if (auto header_card = std::dynamic_pointer_cast<::cw::header::CardHeader>(*object)) list.emplace_back(header_card);
        else if (auto info_card = std::dynamic_pointer_cast<::cw::header::InfoCardHeader>(*object)) list.emplace_back(info_card);
    }
    const auto* object = std::get_if<::cw::lib::ObjectPtr>(&arg_selection.storage());
    if (!object || !*object) throw std::invalid_argument("selection is not a card");
    std::optional<CardInfoItem> selected;
    if (auto menu_card = std::dynamic_pointer_cast<::cw::sprite::card::MenuCard>(*object)) selected = CardInfoItem{menu_card};
    else if (auto header_card = std::dynamic_pointer_cast<::cw::header::CardHeader>(*object)) selected = CardInfoItem{header_card};
    else if (auto info_card = std::dynamic_pointer_cast<::cw::header::InfoCardHeader>(*object)) selected = CardInfoItem{info_card};
    if (!selected) throw std::invalid_argument("selection is not a card");
    initialize(std::move(arg_parent), std::move(list), *selected, std::move(arg_scedir));
}
void YadoCardInfo::OnClickLeftBtn(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    if (m__modal_ended || m_list.empty()) return;
    if (m_selection) set_item_negaflag(*m_selection, false);
    m_index = m_index == 0 ? static_cast<::cw::lib::Integer>(m_list.size()) - 1 : m_index - 1;
    m_selection = m_list[static_cast<std::size_t>(m_index)];
    set_item_negaflag(*m_selection, true);
    if (m_parent_draw) m_parent_draw();
    if(m__modal_ended) return;
    if (m_update_cardpocketinfo) m_update_cardpocketinfo(*m_selection);
    if(!m__modal_ended) draw(true);
}
void YadoCardInfo::OnClickRightBtn(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    if (m__modal_ended || m_list.empty()) return;
    if (m_selection) set_item_negaflag(*m_selection, false);
    m_index = m_index + 1 == static_cast<::cw::lib::Integer>(m_list.size()) ? 0 : m_index + 1;
    m_selection = m_list[static_cast<std::size_t>(m_index)];
    set_item_negaflag(*m_selection, true);
    if (m_parent_draw) m_parent_draw();
    if(m__modal_ended) return;
    if (m_update_cardpocketinfo) m_update_cardpocketinfo(*m_selection);
    if(!m__modal_ended) draw(true);
}
void CardInfo___init___recurse(std::shared_ptr<::cw::lib::Control> arg_ctrl)
{
    if (!arg_ctrl) return;
    // 互換用 Control には子要素の配列がないため、ネイティブの親ウィンドウの WM_CONTEXTMENU で
    // 全子孫をまとめて処理する。 ここでは再帰対象のコントロールを、有効な当たり判定領域としてバックエンドへ公開する。
    arg_ctrl->visible = true;
}
void main() {}
}
