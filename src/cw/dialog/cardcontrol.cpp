// UIの実装は一つに統一し、
// 別経路の実装を設けない。
#include "cw/dialog/cardcontrol.hpp"
#include "cw/dialog/card_price_presentation.hpp"
#include "cw/dialog/cardreorder.hpp"
#include "cw/dialog/dialog.hpp"

#include "app/debug_control_server.hpp"
#include "cw/character.hpp"
#include "cw/cw.hpp"
#include "cw/data.hpp"
#include "cw/dialog/cardinfo.hpp"
#include "cw/dialog/message.hpp"
#include "cw/deck.hpp"
#include "cw/header.hpp"
#include "cw/image.hpp"
#include "cw/imageretouch.hpp"
#include "cw/lib/ui_surface.hpp"
#include "cw/lib/encoding.hpp"
#include "cw/lib/python_percent_string.hpp"
#include "cw/frame.hpp"
#include "cw/setting.hpp"
#include "cw/sprite/card.hpp"
#include "cw/thread.hpp"
#include "cw/util.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <limits>
#include <string>
#include <type_traits>
#include <vector>

namespace cw::dialog::cardcontrol {
namespace {

using CardPtr = std::shared_ptr<::cw::header::CardHeader>;
using InfoPtr = std::shared_ptr<::cw::header::InfoCardHeader>;

const std::array<::cw::lib::String, 8>& card_sort_names()
{
    static const std::array<::cw::lib::String, 8> names{
        {"None", "Name", "Level", "Type", "Price", "Scenario", "Author", "Aptitude"}};
    return names;
}

#if defined(CARDWIRTH_LOG_ENABLED)
void log_cardcontrol(const ::cw::lib::String& message)
{
    ::cardwirth::debug::log(message);
}
#endif

template <typename T>
std::shared_ptr<T> object_value(const ::cw::lib::Value& value)
{
    const auto* object = std::get_if<::cw::lib::ObjectPtr>(&value.storage());
    if (!object || !*object) return {};
    return std::dynamic_pointer_cast<T>(*object);
}

template <typename T>
::cw::lib::Value object_value(const std::shared_ptr<T>& object)
{
    if (!object) return {};
    if constexpr (std::is_base_of_v<::cw::sprite::card::CWPyCard, T>) {
        // PlayerCard/EnemyCard/FriendCard は CWPyCard と
        // Character の両方を継承するため、Object へ直接キャストすると曖昧になる。
        const auto card = std::static_pointer_cast<::cw::sprite::card::CWPyCard>(object);
        return ::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(card));
    } else {
        return ::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(object));
    }
}

bool same_value(const ::cw::lib::Value& lhs, const ::cw::lib::Value& rhs)
{
    const auto* lo = std::get_if<::cw::lib::ObjectPtr>(&lhs.storage());
    const auto* ro = std::get_if<::cw::lib::ObjectPtr>(&rhs.storage());
    if (lo || ro) return lo && ro && lo->get() == ro->get();
    if (const auto* li = std::get_if<::cw::lib::Integer>(&lhs.storage())) {
        const auto* ri = std::get_if<::cw::lib::Integer>(&rhs.storage());
        return ri && *li == *ri;
    }
    if (const auto* ls = std::get_if<::cw::lib::String>(&lhs.storage())) {
        const auto* rs = std::get_if<::cw::lib::String>(&rhs.storage());
        return rs && *ls == *rs;
    }
    return lhs.is_none() && rhs.is_none();
}

bool same_optional_value(const std::optional<::cw::lib::Value>& lhs,
                         const std::optional<::cw::lib::Value>& rhs)
{
    if (lhs.has_value() != rhs.has_value()) return false;
    return !lhs || same_value(*lhs, *rhs);
}

::cw::lib::Integer int_value(const ::cw::lib::Value& value, ::cw::lib::Integer fallback = 0)
{
    if (const auto* v = std::get_if<::cw::lib::Integer>(&value.storage())) return *v;
    if (const auto* v = std::get_if<::cw::lib::Real>(&value.storage())) return static_cast<::cw::lib::Integer>(*v);
    if (const auto* v = std::get_if<bool>(&value.storage())) return *v ? 1 : 0;
    return fallback;
}

::cw::lib::String string_value(const ::cw::lib::Value& value)
{
    if (const auto* v = std::get_if<::cw::lib::String>(&value.storage())) return *v;
    return {};
}

::cw::lib::String lower_ascii(::cw::lib::String value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool bool_value(const ::cw::lib::Value& value, bool fallback = false)
{
    if (const auto* v = std::get_if<bool>(&value.storage())) return *v;
    if (const auto* v = std::get_if<::cw::lib::Integer>(&value.storage())) return *v != 0;
    if (const auto* v = std::get_if<::cw::lib::Real>(&value.storage())) return *v != 0.0;
    return fallback;
}

std::optional<::cw::lib::Value> sequence_at(const ::cw::lib::Value& value, std::size_t index)
{
    const auto* object = std::get_if<::cw::lib::ObjectPtr>(&value.storage());
    if (!object || !*object) return std::nullopt;
    const auto sequence = std::dynamic_pointer_cast<::cw::data::VariantSequence>(*object);
    if (!sequence || index >= sequence->m_values.size()) return std::nullopt;
    return sequence->m_values[index];
}

void sequence_set(::cw::lib::Value& value, std::size_t index, ::cw::lib::Value item)
{
    const auto* object = std::get_if<::cw::lib::ObjectPtr>(&value.storage());
    if (!object || !*object) return;
    const auto sequence = std::dynamic_pointer_cast<::cw::data::VariantSequence>(*object);
    if (sequence && index < sequence->m_values.size()) sequence->m_values[index] = std::move(item);
}

bool setting_sequence_bool(const std::shared_ptr<::cw::setting::Setting>& setting,
                           const ::cw::lib::Value& value, std::size_t index, bool fallback)
{
    (void)setting;
    const auto item = sequence_at(value, index);
    return item ? bool_value(*item, fallback) : fallback;
}

std::vector<::cw::lib::Integer> sequence_ints(const ::cw::lib::Value& value)
{
    std::vector<::cw::lib::Integer> result;
    const auto* object = std::get_if<::cw::lib::ObjectPtr>(&value.storage());
    if (!object || !*object) return result;
    const auto sequence = std::dynamic_pointer_cast<::cw::data::VariantSequence>(*object);
    if (!sequence) return result;
    result.reserve(sequence->m_values.size());
    for (const auto& item : sequence->m_values) result.push_back(int_value(item));
    return result;
}

std::optional<::cw::lib::Integer> value_map_index(const ::cw::lib::ValueMap& values, ::cw::lib::Integer key)
{
    const auto it = values.find(std::to_string(key));
    if (it == values.end()) return std::nullopt;
    return int_value(it->second, -1);
}

std::optional<HeaderItem> value_to_header(const ::cw::lib::Value& value)
{
    if (auto header = object_value<::cw::header::CardHeader>(value)) return HeaderItem{std::move(header)};
    if (auto header = object_value<::cw::header::InfoCardHeader>(value)) return HeaderItem{std::move(header)};
    return std::nullopt;
}

::cw::lib::Value header_to_value(const HeaderItem& item)
{
    return std::visit([](const auto& header) { return object_value(header); }, item);
}

HeaderList typed_headers(const std::vector<::cw::lib::Value>& values)
{
    HeaderList result;
    result.reserve(values.size());
    for (const auto& value : values) {
        if (const auto item = value_to_header(value)) result.push_back(*item);
    }
    return result;
}

std::vector<::cw::lib::Value> values_headers(const HeaderList& values)
{
    std::vector<::cw::lib::Value> result;
    result.reserve(values.size());
    for (const auto& item : values) result.push_back(header_to_value(item));
    return result;
}

::cw::lib::String field(const HeaderItem& item, ::cw::lib::Integer index)
{
    return std::visit([index](const auto& header) -> ::cw::lib::String {
        if (!header) return {};
        using H = std::decay_t<decltype(header)>;
        if constexpr (std::is_same_v<H, CardPtr>) {
            switch (index) {
            case 1: return header->m_name;
            case 2: return header->m_desc;
            case 3: return header->m_scenario;
            case 4: return header->m_author;
            default: return {};
            }
        } else {
            switch (index) {
            case 1: return header->m_name;
            case 2: return header->m_desc;
            case 3: return string_value(header->m_scenario);
            case 4: return string_value(header->m_author);
            default: return {};
            }
        }
    }, item);
}

bool matches(const HeaderItem& item, const ::cw::lib::String& needle, ::cw::lib::Integer narrow_type)
{
    if (needle.empty()) return true;
    const auto match = [&](::cw::lib::Integer index) {
        return ::cw::lib::encoding::python_lower(field(item, index)).find(needle) != ::cw::lib::String::npos;
    };
    if (narrow_type == 0) return match(1) || match(2) || match(3) || match(4);
    return narrow_type >= 1 && narrow_type <= 4 && match(narrow_type);
}

std::vector<::cw::lib::Value> page_values(const HeaderList& list, ::cw::lib::Integer page,
                                          ::cw::lib::Integer page_size)
{
    if (page < 0 || page_size <= 0) return {};
    const auto first = page * page_size;
    if (first < 0 || static_cast<std::size_t>(first) >= list.size()) return {};
    const auto begin = static_cast<std::size_t>(first);
    const auto end = std::min(list.size(), begin + static_cast<std::size_t>(page_size));
    std::vector<::cw::lib::Value> result;
    result.reserve(end - begin);
    for (std::size_t i = begin; i < end; ++i) result.push_back(header_to_value(list[i]));
    return result;
}

bool trade_area(::cw::lib::Integer area)
{
    return area == std::get<0>(::cw::g_AREAS_TRADE) || area == std::get<1>(::cw::g_AREAS_TRADE) ||
           area == std::get<2>(::cw::g_AREAS_TRADE);
}

std::shared_ptr<::cw::character::Character> character_value(const ::cw::lib::Value& value)
{
    return object_value<::cw::character::Character>(value);
}

bool card_type_visible(const CardPtr& header, const std::array<bool, 3>& show)
{
    if (!header) return false;
    if (header->m_type == "SkillCard") return show[0];
    if (header->m_type == "ItemCard") return show[1];
    if (header->m_type == "BeastCard") return show[2];
    return true;
}

::cw::lib::Integer card_type_index(const CardPtr& header)
{
    if (!header) return -1;
    if (header->m_type == "SkillCard") return 0;
    if (header->m_type == "ItemCard") return 1;
    if (header->m_type == "BeastCard") return 2;
    return -1;
}

std::vector<CardPtr> cards_from_values(const std::vector<::cw::lib::Value>& values)
{
    std::vector<CardPtr> result;
    for (const auto& value : values) {
        if (auto card = object_value<CardPtr::element_type>(value)) result.push_back(std::move(card));
    }
    return result;
}

std::vector<::cw::lib::Value> card_values(const std::vector<CardPtr>& cards)
{
    std::vector<::cw::lib::Value> result;
    result.reserve(cards.size());
    for (const auto& card : cards) result.push_back(object_value(card));
    return result;
}

void invoke(const ::cw::lib::Callback& callback)
{
    if (callback) callback({}, {});
}

void change_parent_selection(const ::cw::lib::Value& selection)
{
    if (!::cw::g_cwpy) return;
    if (const auto card = object_value<::cw::sprite::card::CWPyCard>(selection)) {
        // CharaInfo と同様、ダイアログによる変更は所有する Frame
        // を通す。 Frame がゲームスレッドに表示上の選択更新を予約する。
        if (const auto frame = object_value<::cw::frame::Frame>(::cw::g_cwpy->m_frame))
            frame->change_selection(card);
    } else {
        if (const auto frame = object_value<::cw::frame::Frame>(::cw::g_cwpy->m_frame))
            frame->change_selection(std::nullopt);
    }
}

void change_parent_selection_menu(const ::cw::lib::String& kind)
{
    if (!::cw::g_cwpy) return;
    std::optional<std::shared_ptr<::cw::sprite::card::MenuCard>> menu;
    if (kind == "BACKPACK") menu = ::cw::g_cwpy->find_backpackcard();
    else if (kind == "STOREHOUSE") menu = ::cw::g_cwpy->find_storehousecard();
    if (menu && *menu) {
        if (const auto frame = object_value<::cw::frame::Frame>(::cw::g_cwpy->m_frame))
            frame->change_selection(std::static_pointer_cast<::cw::sprite::card::CWPyCard>(*menu));
    } else {
        if (const auto frame = object_value<::cw::frame::Frame>(::cw::g_cwpy->m_frame))
            frame->change_selection(std::nullopt);
    }
}

bool try_open_repl_for_sendto(CardHolder& holder, ::cw::lib::Integer destination, CardPtr header)
{
    if (!::cw::g_cwpy || !::cw::g_cwpy->m_setting || !::cw::g_cwpy->m_setting->m_replacecard_when_sendfullcardpocket || !header)
        return false;
    std::shared_ptr<::cw::sprite::card::PlayerCard> target;
    bool personal = false;
    if (const auto index = value_map_index(holder.m__combo_cast, destination); index && *index >= 0 &&
        static_cast<std::size_t>(*index) < holder.m_list2.size()) {
        target = object_value<::cw::sprite::card::PlayerCard>(holder.m_list2[static_cast<std::size_t>(*index)]);
    } else if (const auto index = value_map_index(holder.m__combo_personal, destination); index && *index >= 0 &&
               static_cast<std::size_t>(*index) < holder.m_list2.size()) {
        target = object_value<::cw::sprite::card::PlayerCard>(holder.m_list2[static_cast<std::size_t>(*index)]);
        personal = true;
    } else {
        return false;
    }
    if (!ReplCardHolder::needs_exchange(target, header, personal)) return false;
    return holder.show_replacement(std::move(target), std::move(header), personal);
}

}  // namespace

namespace {

std::shared_ptr<::cw::lib::Surface> make_ui_surface(::cw::lib::Integer width, ::cw::lib::Integer height,
                                                     ::cw::lib::Color color)
{
    auto surface = std::make_shared<::cw::lib::Surface>();
    surface->size = {std::max<::cw::lib::Integer>(1, width), std::max<::cw::lib::Integer>(1, height)};
    const auto pixels = static_cast<std::size_t>(surface->size.width) * static_cast<std::size_t>(surface->size.height);
    surface->rgba.resize(pixels * 4);
    for (std::size_t i = 0; i < pixels; ++i) {
        surface->rgba[i * 4] = color.r;
        surface->rgba[i * 4 + 1] = color.g;
        surface->rgba[i * 4 + 2] = color.b;
        surface->rgba[i * 4 + 3] = color.a;
    }
    return surface;
}

void blend_ui_surface(const std::shared_ptr<::cw::lib::Surface>& destination,
                      const std::shared_ptr<::cw::lib::Surface>& source,
                      ::cw::lib::Integer x, ::cw::lib::Integer y,
                      ::cw::lib::Integer width = 0, ::cw::lib::Integer height = 0)
{
    ::cw::lib::ui::blend_surface(destination, source, x, y, width, height);
}

void fill_ui_rect(const std::shared_ptr<::cw::lib::Surface>& destination, ::cw::lib::Rect rect,
                  ::cw::lib::Color color)
{
    if (!destination) return;
    const auto left = std::max<::cw::lib::Integer>(0, rect.x);
    const auto top = std::max<::cw::lib::Integer>(0, rect.y);
    const auto right = std::min(destination->size.width, rect.x + std::max<::cw::lib::Integer>(0, rect.width));
    const auto bottom = std::min(destination->size.height, rect.y + std::max<::cw::lib::Integer>(0, rect.height));
    for (::cw::lib::Integer y = top; y < bottom; ++y) for (::cw::lib::Integer x = left; x < right; ++x) {
        const auto index = static_cast<std::size_t>((y * destination->size.width + x) * 4);
        destination->rgba[index] = color.r;
        destination->rgba[index + 1] = color.g;
        destination->rgba[index + 2] = color.b;
        destination->rgba[index + 3] = color.a;
    }
}

// 用途別の共通テキストを最終ピクセル寸法で扱う。計測には描画と同じ装飾付きラ
// スターを使い、 ネイティブウィジェットやバイト数による幅の近似は使わない。
using CardTextColor=std::tuple<::cw::lib::Integer,::cw::lib::Integer,::cw::lib::Integer>;
std::shared_ptr<::cw::lib::Surface> card_label(const CardControl& control,
    const ::cw::lib::String& text,const char* role,::cw::lib::Integer logical_pixels,
    CardTextColor color={0,0,0})
{
    const auto style=control.dialog_font(role,logical_pixels);
    ::cw::imageretouch::Font font;
    font.initialize(style.face,-style.pixels,style.bold,style.italic);
    return font.render(text,true,color);
}
void draw_ui_label(const CardControl& control,const std::shared_ptr<::cw::lib::Surface>& destination,
    const ::cw::lib::String& text,::cw::lib::Integer x,::cw::lib::Integer y,
    ::cw::lib::Integer logical_pixels,CardTextColor color,const char* role="paneltitle")
{
    if(destination && !text.empty())
        blend_ui_surface(destination,card_label(control,text,role,logical_pixels,color),x,y);
}
::cw::lib::Integer half_floor(::cw::lib::Integer value)
{
    return value>=0 ? value/2 : -((-value+1)/2); // 大きすぎるフォントでも Python の // と同じ切り下げ除算を使う。
}
void centered_label(const CardControl& control,const std::shared_ptr<::cw::lib::Surface>& surface,
    const ::cw::lib::String& text,const ::cw::lib::Rect& rect,const char* role,
    ::cw::lib::Integer pixels,CardTextColor color)
{
    const auto glyph=card_label(control,text,role,pixels,color);
    if(glyph) blend_ui_surface(surface,glyph,rect.x+half_floor(rect.width-glyph->size.width),
        rect.y+half_floor(rect.height-glyph->size.height));
}

constexpr ::cw::lib::Integer kPanelWidth = 520;
bool expanded_pc_paging(const CardControl& control) {
    if (control.m_callname != "CARDPOCKET" || !dynamic_cast<const CardHolder*>(&control)) return false;
    const auto owner = character_value(control.m_selection);
    return owner && owner->has_expanded_pc_card_limits();
}
bool replacement_card_paging(const CardControl& control) {
    const auto replacement=dynamic_cast<const ReplCardHolder*>(&control);
    return replacement && replacement->candidate_page_count()>1;
}
::cw::lib::Integer panel_width(const CardControl& control) {
    return kPanelWidth + ((expanded_pc_paging(control) || replacement_card_paging(control)) ? 60 : 0);
}
constexpr ::cw::lib::Integer kPanelHeight = 285;
// 下部バーとボタンは Python の 30px より kDialogFooterExtraHeight
// だけ高くし、 ダイアログ全体も同じ分だけ高くする。
constexpr ::cw::lib::Integer kBottomBarHeight = 30 + ::cw::dialog::kDialogFooterExtraHeight;
constexpr ::cw::lib::Integer kDialogHeight = 285 + kBottomBarHeight;
constexpr ::cw::lib::Integer kFooterButtonHeight = 24 + ::cw::dialog::kDialogFooterExtraHeight;
constexpr ::cw::lib::Integer kCardWidth = 80;
constexpr ::cw::lib::Integer kCardHeight = 110;

inline ::cw::lib::Real cardcontrol_ui_scale()
{
    return std::max<::cw::lib::Real>(::cw::g_UP_WIN, 1.0);
}

inline ::cw::lib::Integer cs(::cw::lib::Integer arg_value)
{
    return static_cast<::cw::lib::Integer>(std::lround(static_cast<::cw::lib::Real>(arg_value) * cardcontrol_ui_scale()));
}

::cw::lib::Integer navigation_button_width(const CardControl& control)
{
    // PC の拡張ポケットではパネル幅に論理 60px を追加する。矢印
    // ボタンの中心とダイアログ中心の距離は、 通常の 520/30 の配
    // 置と揃える（580 - 90 == 520 - 30）。当たり判定
    // の幅は 90px となり、従来の 105px より約 14.3%
    // 小さい。 矢印のビットマップ自体は元のサイズで中央に配置する。
    return cs(expanded_pc_paging(control) ? 90 : 30);
}

std::array<::cw::lib::Rect, 3> expanded_pc_page_layout()
{
    // 上矢印・ページ番号・下矢印の組を、上部ツールバーと下部
    // 操作バーを除いたカード本体の中央に配置する。 小数の
    // UI 拡大率でも、描画と当たり判定で同じ矩形を使う。
    const auto body_top = cs(27);
    const auto height = cs(40);
    const auto step = height + cs(10);
    const auto top = body_top + (cs(kPanelHeight) - body_top - (2 * step + height)) / 2;
    std::array<::cw::lib::Rect, 3> result{};
    for (std::size_t i = 0; i < result.size(); ++i)
        result[i] = {cs(kPanelWidth + 5), top + static_cast<::cw::lib::Integer>(i) * step, cs(50), height};
    return result;
}

// Python にはない CardWirthPy_C 専用機能。「順番入替」は、PC
// が該当カードを 11 枚以上持つ場合、 表示中のスキル（0）・召喚獣（2）・個
// 人（3）ポケットに対する CardReorderDialog を開く。 宿または
// シナリオ内のキャンプで戦闘・イベント中でない場合に利用できる。アイテムは並べ替えない。
std::vector<CardPtr> reorder_pocket_headers(const CardControl& control, ::cw::lib::Integer pocket)
{
    const auto owner = character_value(control.m_selection);
    if (!owner) return {};
    if (pocket == 3) {
        if (const auto player = std::dynamic_pointer_cast<::cw::character::Player>(owner))
            return player->m_personal_pocket;
        return {};
    }
    if (pocket == 0) return std::get<0>(owner->m_cardpocket_typed);
    if (pocket == 2) return std::get<2>(owner->m_cardpocket_typed);
    return {};
}

bool pocket_reorder_available(const CardControl& control)
{
    // ボタンは拡張ポケットのページ操作列に置き、上矢印の上に配置する。
    if (!expanded_pc_paging(control) || control.m__quit) return false;
    const auto holder = dynamic_cast<const CardHolder*>(&control);
    if (!holder) return false;
    const auto pocket = holder->m_last_cardpocket;
    if (pocket != 0 && pocket != 2 && pocket != 3) return false;
    const auto runtime = ::cw::g_cwpy;
    if (!runtime) return false;
    // 宿、またはシナリオのキャンプ中だけで利用でき、テーブル・戦闘・イベント中は対象外とす
    // る。 キャンプへ入ると change_specialarea が m_areaid
    // を AREA_CAMP に設定し、このポケットを開いている間もその値を維持する。
    const bool inn = runtime->m_status == "Yado";
    const bool scenario = runtime->m_status == "Scenario" &&
                          runtime->m_areaid == int_value(::cw::g_AREA_CAMP, -4) &&
                          !runtime->is_battlestatus() && !runtime->is_runningevent();
    if (!inn && !scenario) return false;
    return reorder_pocket_headers(control, pocket).size() >= 11;
}

::cw::lib::Rect pocket_reorder_rect()
{
    auto rect = expanded_pc_page_layout()[0];
    rect.y -= cs(50);
    return rect;
}

std::shared_ptr<::cw::lib::Surface> resource_table_surface(const ::cw::lib::Value& table_value,
                                                            const ::cw::lib::String& key)
{
    return ::cw::lib::ui::resource_table_surface(table_value, key);
}

std::shared_ptr<::cw::lib::Surface> resource_button_surface(const ::cw::lib::String& key)
{
    return ::cw::g_cwpy && ::cw::g_cwpy->m_rsrc
        ? resource_table_surface(::cw::g_cwpy->m_rsrc->m_buttons, key) : std::shared_ptr<::cw::lib::Surface>{};
}

::cw::lib::String cardcontrol_msg(const char* arg_key, const char* arg_fallback)
{
    return ::cw::g_cwpy && ::cw::g_cwpy->m_setting && ::cw::g_cwpy->m_setting->m_msgs
        ? ::cw::g_cwpy->m_setting->m_msgs->get(arg_key, arg_fallback) : ::cw::lib::String(arg_fallback);
}

struct CardControlToolbarLayout {
    ::cw::lib::Rect rsmall{};
    ::cw::lib::Rect lsmall{};
    ::cw::lib::Rect combo{};
    ::cw::lib::Rect sort{};
    ::cw::lib::Rect sortwithstar{};
    std::array<::cw::lib::Rect, 3> cardtype{};
    ::cw::lib::Rect editstar{};
    ::cw::lib::Rect narrow{};
    ::cw::lib::Rect narrow_type{};
    ::cw::lib::Rect page{};
    ::cw::lib::Rect redeal{};
    ::cw::lib::Rect addctrl{};
};

::cw::lib::Rect compute_page_input_rect(const CardControl& control)
{
    const bool show_addctrlbtn=::cw::g_cwpy && ::cw::g_cwpy->m_setting &&
        ::cw::g_cwpy->m_setting->m_show_addctrlbtn;
    const auto top=cs(show_addctrlbtn?80:90),bottom=cs(show_addctrlbtn?210:220);
    // Python の _do_layout はここで paneltitle2/14 と SIZE_CARDIMAGE
    // の高さを使う。 ページ見出しの描画に使う dlgtitle/17 とは独立している。
    const auto cell=card_label(control,"#","paneltitle2",14);
    const auto slash=card_label(control,"/","paneltitle2",14);
    const auto card_height=cs(std::get<1>(::cw::g_SIZE_CARDIMAGE));
    const auto h=(cell?cell->size.height:0)+cs(1)+card_height;
    const auto y=top+half_floor(bottom-top-h)+card_height+cs(1);
    const auto sx=cs(47)-(slash?slash->size.width/2:0);
    return {sx-cs(34),y+(slash?slash->size.height/2:0)-cs(22)/2,cs(34),cs(22)};
}

CardControlToolbarLayout compute_toolbar_layout(CardControl& control, ::cw::lib::Integer panel_w,
                                                ::cw::lib::Integer panel_h)
{
    CardControlToolbarLayout layout{};
    ::cw::lib::Integer x = panel_w - cs(5);
    const ::cw::lib::Integer y = 0;
    const ::cw::lib::Integer toolbar_h = cs(24);
    if (control.m_sendto) {
        x -= cs(20);
        layout.rsmall = {x, y, cs(20), toolbar_h};
        x -= cs(100);
        layout.combo = {x, y, cs(100), toolbar_h};
        x -= cs(20);
        layout.lsmall = {x, y, cs(20), toolbar_h};
        x -= cs(50);
    }
    if (control.m_show_additional_controls &&
        (control.m_callname == "BACKPACK" || control.m_callname == "STOREHOUSE")) {
        for (int i = 0; i < 3; ++i) {
            x -= cs(24);
            layout.cardtype[static_cast<std::size_t>(2 - i)] = {x, y, cs(24), toolbar_h};
        }
        x -= cs(5);
    }
    if (control.m_callname == "HANDVIEW" &&
        ::cw::g_cwpy && ::cw::g_cwpy->is_debugmode()) {
        layout.redeal = {panel_w - cs(29), y, cs(24), toolbar_h};
    }
    if (control.m_show_additional_controls && control._can_narrow()) {
        layout.narrow_type = {cs(425), panel_h - cs(25), cs(90), cs(20)};
        layout.narrow = {cs(323), panel_h - cs(25), cs(100), cs(20)};
    }
    if (const auto holder = dynamic_cast<const CardHolder*>(&control);
        holder && control.m_callname != "CARDPOCKET" &&
        control.m_callname != "HANDVIEW" && control.m_callname != "CARDPOCKET_REPLACE") {
        layout.page = compute_page_input_rect(control);
    }
    if (control.m_callname == "STOREHOUSE" || control.m_callname == "BACKPACK" ||
        control.m_callname == "CARDPOCKET" || control.m_callname == "CARDPOCKETB") {
        x -= cs(24);
        layout.editstar = {x, y, cs(24), toolbar_h};
    }
    if (control.m_show_additional_controls &&
        (control.m_callname == "BACKPACK" || control.m_callname == "STOREHOUSE" ||
         control.m_callname == "CARDPOCKETB")) {
        // Python と同様、スター優先の切り替えを
        // 並べ替えコンボの直前に配置する。 上にある
        // カード種別ボタンとは当たり判定を分離する。
        x -= cs(24);
        layout.sortwithstar = {x, y, cs(24), toolbar_h};
        x -= cs(77);
        layout.sort = {x, y, cs(75), toolbar_h};
    }
    const bool show_addctrlbtn = ::cw::g_cwpy && ::cw::g_cwpy->m_setting &&
                                 ::cw::g_cwpy->m_setting->m_show_addctrlbtn;
    if (show_addctrlbtn && !control.m_addctrlbtn.is_none() &&
        (control.m_callname == "STOREHOUSE" || control.m_callname == "BACKPACK" ||
         control.m_callname == "CARDPOCKET" || control.m_callname == "CARDPOCKETB" ||
         control.m_callname == "INFOVIEW")) {
        layout.addctrl = {cs(2), panel_h - cs(26), cs(24), cs(24)};
    }
    return layout;
}

void draw_page_numbers(const std::shared_ptr<::cw::lib::Surface>& surface,
                       const CardControl& control, ::cw::lib::Integer up_y, ::cw::lib::Integer up_hit_h,
                       ::cw::lib::Integer down_y, ::cw::lib::Integer mark_bottom)
{
    if (control.m_callname == "HANDVIEW" || control.m_callname == "CARDPOCKET_REPLACE" ||
        control.m_callname == "CARDPOCKET")
        return;
    const auto holder = dynamic_cast<const CardHolder*>(&control);
    if (!holder || control.m__leftmarks.empty()) return;
    const auto sy = mark_bottom + cs(1);
    const auto slash = "/";
    const auto max_page = std::to_string(holder->m_page_count);
    const auto current_page = std::to_string(holder->m_index + 1);
    const auto slash_image=card_label(control,slash,"dlgtitle",17,{225,225,235});
    const auto slash_w=slash_image?slash_image->size.width:0;
    const auto sx = cs(40) - slash_w / 2 + cs(7);
    // Python は追加コントロールの表示中、ネイティブのページ入力を使
    // う。 その入力欄が非表示の時だけ、現在のページを背景の描画面へ描く。
    if (!control.m_show_additional_controls) {
        const auto glyph=card_label(control,current_page,"dlgtitle",17,{225,225,235});
        if(glyph) blend_ui_surface(surface,glyph,sx-glyph->size.width,sy);
    }
    blend_ui_surface(surface,slash_image,sx,sy);
    draw_ui_label(control,surface,max_page,sx+slash_w,sy,17,{225,225,235},"dlgtitle");
    (void)up_y;
    (void)up_hit_h;
    (void)down_y;
}

std::shared_ptr<::cw::lib::Surface> resource_dialog_surface(const ::cw::lib::String& key)
{
    return ::cw::g_cwpy && ::cw::g_cwpy->m_rsrc
        ? resource_table_surface(::cw::g_cwpy->m_rsrc->m_dialogs, key) : std::shared_ptr<::cw::lib::Surface>{};
}

std::shared_ptr<::cw::lib::Surface> disabled_ui_surface(
    const std::shared_ptr<::cw::lib::Surface>& source,
    std::tuple<::cw::lib::Integer, ::cw::lib::Integer> mask_position = {0, 0})
{
    if (!source) return {};
    auto resolved = source;
    if (const auto animated = std::dynamic_pointer_cast<::cw::lib::Bitmap>(source))
        resolved = ::cw::util::resolve_animated_wxbmp(animated);
    auto bitmap = std::make_shared<::cw::lib::Bitmap>();
    static_cast<::cw::lib::Surface&>(*bitmap) = *resolved;
    const auto disabled = ::cw::imageretouch::to_disabledimage(std::move(bitmap), mask_position);
    if (!disabled) return source;
    auto result = std::make_shared<::cw::lib::Surface>(static_cast<const ::cw::lib::Surface&>(*disabled));
    ::cw::lib::ui::inherit_surface_animation(result, source);
    return result;
}

::cw::lib::Color cardcontrol_body_colour(const CardControl& control)
{
    // Python は CARDPOCKET/CARDPOCKETB/BACKPACK
    // に取引色を適用するが、STOREHOUSE は常に濃い緑にする。 所有先の
    // 種別より先にエリアを判定すると、二つの保管表示が同じ背景色になってしまう。
    if (control.m_callname != "STOREHOUSE" &&
        (trade_area(control.m_areaid) || control.m_callname == "CARDPOCKET_REPLACE") &&
        ::cw::g_cwpy && ::cw::g_cwpy->m_setting) {
        const auto [r, g, b] = ::cw::g_cwpy->m_setting->m_trademode_cardholder_color;
        return {static_cast<std::uint8_t>(std::clamp<::cw::lib::Integer>(r, 0, 255)),
                static_cast<std::uint8_t>(std::clamp<::cw::lib::Integer>(g, 0, 255)),
                static_cast<std::uint8_t>(std::clamp<::cw::lib::Integer>(b, 0, 255)), 255};
    }
    if (control.m_callname == "STOREHOUSE") return {0, 69, 0, 255};
    return {0, 0, 128, 255};
}

bool can_show_sort_controls(const CardControl& control)
{
    return control.m_show_additional_controls &&
           (control.m_callname == "STOREHOUSE" || control.m_callname == "BACKPACK" ||
            control.m_callname == "CARDPOCKETB");
}

void draw_button_or_placeholder(const CardControl& control,const std::shared_ptr<::cw::lib::Surface>& surface,
                                const std::shared_ptr<::cw::lib::Surface>& bitmap,
                                ::cw::lib::Integer x, ::cw::lib::Integer y,
                                ::cw::lib::Integer width, ::cw::lib::Integer height,
                                bool selected = false, const ::cw::lib::String& fallback = {})
{
    if (bitmap) blend_ui_surface(surface, bitmap, x, y, width, height);
    else {
        // 共通の角丸ボタンの背景。ここはピクセル単位の描画面なので draw_scale は 1 とする。
        ::cw::lib::ui::draw_button_face(surface, {x, y, width, height}, {true, selected});
        if (!fallback.empty())
            centered_label(control,surface,fallback,{x,y,width,height},"button",14,
                selected?std::tuple{0,0,0}:std::tuple{70,70,70});
    }
}

// wx.BitmapButton(chain=True) と同様、当たり判定は wins(size)
// とし、 ビットマップは読み込み時のピクセル寸法を保ってボタン中央に配置する。
void draw_chained_button(const CardControl& control,const std::shared_ptr<::cw::lib::Surface>& surface,
                         const std::shared_ptr<::cw::lib::Surface>& bitmap,
                         ::cw::lib::Integer x, ::cw::lib::Integer y,
                         ::cw::lib::Integer hit_width, ::cw::lib::Integer hit_height,
                         const ::cw::lib::String& fallback = {})
{
    if (bitmap && bitmap->size.width > 0 && bitmap->size.height > 0) {
        const auto scaled = ::cw::lib::ui::resource_surface_for_window(bitmap);
        const auto bx = x + (hit_width - scaled->size.width) / 2;
        const auto by = y + (hit_height - scaled->size.height) / 2;
        blend_ui_surface(surface, scaled, bx, by);
        return;
    }
    draw_button_or_placeholder(control,surface, nullptr, x, y, hit_width, hit_height, false, fallback);
}

// Python の CardControl の OnPaint2 と同様、scr 拡大率の get_image() ではなく wins 拡大率の get_cardwxbmp() を描画する。
std::shared_ptr<::cw::lib::Surface> surface_from_card_wxbmp(const std::shared_ptr<::cw::lib::Bitmap>& arg_bitmap)
{
    if (!arg_bitmap) return {};
    return std::static_pointer_cast<::cw::lib::Surface>(arg_bitmap);
}

std::shared_ptr<::cw::lib::Surface> cardcontrol_card_surface(const std::shared_ptr<::cw::lib::Surface>& arg_surface)
{
    if (!arg_surface) return {};
    if (std::abs(::cw::g_UP_WIN - ::cw::g_UP_SCR) < 1e-9) return arg_surface;
    const auto scaled = ::cw::scr2win_s(
        ::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(arg_surface)));
    if (const auto* object = std::get_if<::cw::lib::ObjectPtr>(&scaled.storage()); object && *object) {
        if (const auto surface = std::dynamic_pointer_cast<::cw::lib::Surface>(*object)) return surface;
    }
    return arg_surface;
}

std::shared_ptr<::cw::lib::Bitmap> load_leftmark_bitmap(const ::cw::lib::String& arg_resource_path)
{
    if (!::cw::g_cwpy || !::cw::g_cwpy->m_setting) return {};
    const auto requested = (std::filesystem::u8path(::cw::g_cwpy->m_setting->m_skindir) / arg_resource_path).generic_string();
    const auto ext = ::cw::g_cwpy->m_rsrc ? ::cw::g_cwpy->m_rsrc->m_ext_img : ::cw::g_M_IMG;
    const auto path = ::cw::util::find_resource(requested, ext);
    if (path.empty()) return {};
    const auto [resolved, load_scale] = ::cw::util::find_scaledimagepath(path, ::cw::g_UP_SCR, true, false);
    auto bitmap = ::cw::util::load_wxbmp(resolved, true, {}, {}, std::nullopt, true, true, false,
                                         ::cw::lib::Value(load_scale));
    if (!bitmap) return {};
    // Py は読み込んだ Bitmap に cw.wins を適用する。その共通の拡縮経路を再
    // 利用し、 整数への切り捨て、読み込み時の拡大率情報、マスクとフィルターの保持を揃える。
    const auto scaled = ::cw::wins(::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(bitmap)));
    if (const auto* object = std::get_if<::cw::lib::ObjectPtr>(&scaled.storage()); object && *object)
        if (const auto result = std::dynamic_pointer_cast<::cw::lib::Bitmap>(*object)) return result;
    throw std::logic_error("cw.wins did not return a Bitmap for a left mark");
}

void decorate_cardcontrol_base(CardControl& control)
{
    auto& surface = control.m_ui_surface;
    if (!surface) return;
    const auto panel_w = surface->size.width;
    const auto panel_h = cs(kPanelHeight);
    const auto body = cardcontrol_body_colour(control);
    fill_ui_rect(surface, {0, 0, panel_w, panel_h}, body);
    fill_ui_rect(surface, {0, panel_h, panel_w, cs(kBottomBarHeight)}, {242, 242, 242, 255});

    // Python の PAD 透かし画像をダイアログの生存期間中保持する。
    // ネイティブの再描画時に ResourceTable を再取得すると、所
    // 有先の切り替え中に一時的に取得できず、紺色の背景だけになる場合がある。
    if (!control.m_ui_pad)
        control.m_ui_pad = ::cw::lib::ui::resource_surface_for_window(
            resource_dialog_surface("PAD"));
    if (control.m_ui_pad) {
        const auto px = (panel_w - control.m_ui_pad->size.width) / 2;
        const auto py = (panel_h - control.m_ui_pad->size.height) / 2;
        blend_ui_surface(surface, control.m_ui_pad, px, py);
    }

    fill_ui_rect(surface, {0, cs(25), panel_w, cs(1)}, {225, 225, 225, 255});
    fill_ui_rect(surface, {0, cs(26), panel_w, cs(1)}, {160, 160, 160, 255});
}

void decorate_cardcontrol_chrome(CardControl& control)
{
    auto& surface = control.m_ui_surface;
    if (!surface) return;
    const auto panel_w = surface->size.width;
    const auto panel_h = cs(kPanelHeight);

    const auto mode = control.get_mode();
    ::cw::lib::String mode_text;
    if (mode == g_CCMODE_SHOW) mode_text = cardcontrol_msg("mode_show", "閲覧モード");
    else if (mode == g_CCMODE_MOVE) mode_text = cardcontrol_msg("mode_move", "移動モード");
    else if (mode == g_CCMODE_BATTLE) mode_text = cardcontrol_msg("mode_battle", "戦闘モード");
    else if (mode == g_CCMODE_REPLACE) {
        const auto holder = dynamic_cast<const ReplCardHolder*>(&control);
        if (!holder || !holder->m_target_header)
            throw std::logic_error("replacement mode requires a target card");
        mode_text = ::cw::lib::python_percent_string(
            cardcontrol_msg("mode_replace", "%sとの交換モード"), holder->m_target_header->m_name);
    } else mode_text = cardcontrol_msg("mode_use", "使用モード");
    const auto mode_label=card_label(control,mode_text,"paneltitle",16,{192,192,192});
    const auto mode_cell=card_label(control,"#","paneltitle",16);
    if(mode_label) blend_ui_surface(surface,mode_label,cs(8),
        half_floor(cs(24)-(mode_cell?mode_cell->size.height:0)));
    const auto toolbar_label=[&](const ::cw::lib::String& text,::cw::lib::Integer right,
        ::cw::lib::Integer top,::cw::lib::Integer height) {
        const auto glyph=card_label(control,text,"paneltitle",14,{192,192,192});
        const auto cell=card_label(control,"#","paneltitle",14);
        if(glyph) blend_ui_surface(surface,glyph,right-glyph->size.width,
            top+half_floor(height-(cell?cell->size.height:0)));
    };

    const bool show_additional = control.m_show_additional_controls;
    const bool show_addctrlbtn = ::cw::g_cwpy && ::cw::g_cwpy->m_setting && ::cw::g_cwpy->m_setting->m_show_addctrlbtn;
    const auto up_y = show_addctrlbtn ? cs(40) : cs(50);
    const auto down_y = up_y + cs(170);

    if (control.m_callname == "CARDPOCKET") {
        const auto holder = dynamic_cast<CardHolder*>(&control);
        const bool show_personal = holder && holder->is_showpersonal();
        const auto base_y = show_personal ? cs(37) : cs(64);
        const std::array<std::pair<const char*, ::cw::lib::String>, 4> pockets{{
            {"技能", "SKILL"}, {"道具", "ITEM"}, {"召喚", "BEAST"}, {"個人", "PERSONAL"}}};
        const auto pocket_count = show_personal ? 4 : 3;
        for (::cw::lib::Integer i = 0; i < pocket_count; ++i) {
            const bool selected = holder && holder->m_last_cardpocket == i;
            const auto x = cs(10);
            const auto y = base_y + cs(54) * i;
            const auto w = cs(74);
            const auto h = cs(54);
            // Python の ThemedGenBitmapToggleButton(size=wins(74,54))
            // と同様、各ポケットは角丸の独立した切り替えボタンとし、間に小さな隙間を
            // 設ける。 選択中のボタンは押された状態で描画する。SetBitmapLabel
            // はビットマップを本来の wins() 寸法で中央に配置する。
            const auto inset = std::max<::cw::lib::Integer>(1, cs(2));
            ::cw::lib::ui::draw_button_face(surface, {x + inset, y + inset, w - inset * 2, h - inset * 2},
                                            {true, selected});
            const auto pocket_bitmap =
                resource_button_surface(pockets[static_cast<std::size_t>(i)].second);
            if (pocket_bitmap)
                draw_chained_button(control,surface, pocket_bitmap, x, y, w, h);
            else
                centered_label(control,surface,pockets[static_cast<std::size_t>(i)].first,
                    {x,y,w,h},"button",14,selected?std::tuple{0,0,0}:std::tuple{70,70,70});
        }
        // 絞り込み後の表示一覧ではなく、所有者のポケット全体の枚数を数える。
        if(holder) if(const auto owner=character_value(holder->m_selection)) {
            ::cw::lib::Integer count=0,capacity=0;
            if(holder->m_last_cardpocket==3) {
                if(const auto player=std::dynamic_pointer_cast<::cw::character::Player>(owner)) {
                    count=static_cast<::cw::lib::Integer>(player->m_personal_pocket.size());
                    capacity=player->get_personalpocketspace();
                }
            } else {
                const auto spaces=owner->get_cardpocketspace();
                const auto& pockets=owner->m_cardpocket_typed;
                const auto index=holder->m_last_cardpocket;
                const auto& pocket=index==1?std::get<1>(pockets):index==2?std::get<2>(pockets):std::get<0>(pockets);
                count=static_cast<::cw::lib::Integer>(pocket.size());
                capacity=index==1?std::get<1>(spaces):index==2?std::get<2>(spaces):std::get<0>(spaces);
            }
            const auto text="Cap "+std::to_string(count)+"/"+std::to_string(capacity);
            const auto glyph=card_label(control,text,"dlgtitle",17,{192,192,192});
            if(glyph) blend_ui_surface(surface,glyph,cs(45)-glyph->size.width/2,
                base_y+cs(54)*pocket_count+cs(5));
        }
    } else if (control.m_callname != "HANDVIEW" && control.m_callname != "CARDPOCKET_REPLACE") {
        const auto up_x = cs(10);
        const auto up_hit_w = cs(70);
        const auto up_hit_h = cs(40);
        // Python の create_wxbutton(chain=True) と同様、wx.BitmapButton の当たり
        // 判定は wins(70,40) とする。 ポケットの切り替えボタンと同じく、白い背景の中央に元の寸法のビットマップを置く。
        ::cw::lib::ui::draw_button_face(surface, {up_x, up_y, up_hit_w, up_hit_h},
            {control.chain_enabled(::cw::dialog::DialogPresentationAction::Auxiliary,13)});
        const auto up_icon=resource_button_surface("UP");
        draw_chained_button(control,surface,control.chain_enabled(::cw::dialog::DialogPresentationAction::Auxiliary,13)
            ? up_icon : disabled_ui_surface(up_icon),up_x,up_y,up_hit_w,up_hit_h,"▲");
        ::cw::lib::ui::draw_button_face(surface, {up_x, down_y, up_hit_w, up_hit_h},
            {control.chain_enabled(::cw::dialog::DialogPresentationAction::Auxiliary,14)});
        const auto down_icon=resource_button_surface("DOWN");
        draw_chained_button(control,surface,control.chain_enabled(::cw::dialog::DialogPresentationAction::Auxiliary,14)
            ? down_icon : disabled_ui_surface(down_icon),up_x,down_y,up_hit_w,up_hit_h,"▼");

        const auto top = up_y + up_hit_h;
        const auto bottom = down_y;
        ::cw::lib::Integer mark_bottom = top;
        ::cw::lib::Integer last_mark_y = top;
        ::cw::lib::Integer max_mark_height = 0;
        const auto page_cell=card_label(control,"#","dlgtitle",17);
        const auto page_height=page_cell?page_cell->size.height:0;
        for (const auto& mark_bitmap : control.m__leftmarks) {
            if (!mark_bitmap) continue;
            const auto& mark = static_cast<const ::cw::lib::Surface&>(*mark_bitmap);
            const auto mark_y=top+half_floor(bottom-top-mark.size.height-page_height-cs(1));
            const auto mark_x = cs(10) + up_hit_w / 2 - mark.size.width / 2;
            blend_ui_surface(surface, std::make_shared<::cw::lib::Surface>(mark), mark_x, mark_y);
            last_mark_y = mark_y;
            max_mark_height = std::max(max_mark_height, mark.size.height);
        }
        // Py と同様、最後の印の y 座標にビットマップの最大高さを加え
        // る。 画像の高さが異なっても、個々の下端の最大値に置き換えない。
        mark_bottom = last_mark_y + max_mark_height;
        draw_page_numbers(surface, control, up_y, up_hit_h, down_y, mark_bottom);
    }

    if (expanded_pc_paging(control) || replacement_card_paging(control)) {
        const auto holder = dynamic_cast<const CardHolder*>(&control);
        const auto replacement=dynamic_cast<const ReplCardHolder*>(&control);
        const auto pages = expanded_pc_page_layout();
        const auto button = [&](const ::cw::lib::Rect& rect, const char* text, bool enabled) {
            ::cw::lib::ui::draw_button_face(surface, rect, {enabled});
            centered_label(control, surface, text, rect, "button", 14,
                           enabled ? std::tuple{20,20,20} : std::tuple{105,105,105});
        };
        const auto page=holder?holder->m_index:replacement?replacement->m_index:0;
        const auto page_count=holder?holder->m_page_count:
            replacement?replacement->candidate_page_count():1;
        button(pages[0], "▲", page > 0);
        if (pocket_reorder_available(control)) {
            const auto rect = pocket_reorder_rect();
            ::cw::lib::ui::draw_button_face(surface, rect);
            const auto half = rect.height / 2;
            centered_label(control, surface, "順番", {rect.x, rect.y + cs(2), rect.width, half - cs(2)},
                           "button", 12, std::tuple{20,20,20});
            centered_label(control, surface, "入替", {rect.x, rect.y + half, rect.width, half - cs(2)},
                           "button", 12, std::tuple{20,20,20});
        }
        centered_label(control, surface,
            std::to_string(page + 1) + "/" + std::to_string(page_count),
            pages[1], "button", 14, std::tuple{225,225,235});
        button(pages[2], "▼", page + 1 < page_count);
    }
    const auto toolbar = compute_toolbar_layout(control, panel_w, panel_h);
    if (toolbar.redeal.width > 0) {
        draw_chained_button(control,surface, resource_dialog_surface("HAND"),
                            toolbar.redeal.x, toolbar.redeal.y,
                            toolbar.redeal.width, toolbar.redeal.height);
        toolbar_label(cardcontrol_msg("re_deal_label","再配布"),toolbar.redeal.x-cs(2),0,cs(24));
    }
    // Python と同様、簡易表示でも展開表示でも移動先コントロールは
    // 表示する。 展開の対象は並べ替え・絞り込み・ページ操作だけとする。
    if (control.m_sendto) {
        draw_chained_button(control,surface, resource_button_surface("RSMALL"),
                            toolbar.rsmall.x, toolbar.rsmall.y, toolbar.rsmall.width, toolbar.rsmall.height);
        // 移動先の選択は一つの共通 Presentation
        // に含める。 ホストはこの描画面を合成し、別
        // のネイティブウィジェット構成に置き換えない。
        fill_ui_rect(surface, toolbar.combo, {245, 245, 245, 255});
        // 最終の Presentation が、切り詰めたテキストとドロップダウンの目印を描画する。
        draw_chained_button(control,surface, resource_button_surface("LSMALL"),
                            toolbar.lsmall.x, toolbar.lsmall.y, toolbar.lsmall.width, toolbar.lsmall.height);
        const auto send_to_label = cardcontrol_msg("send_to", "送り先");
        toolbar_label(send_to_label,toolbar.lsmall.x-cs(2),0,cs(24));
    }

    const auto draw_toolbar_toggle = [&](const ::cw::lib::Rect& rect,
                                         std::shared_ptr<::cw::lib::Surface> icon,
                                         bool selected, bool disabled = false,
                                         std::optional<std::tuple<::cw::lib::Integer,
                                                                  ::cw::lib::Integer>> mask = std::nullopt) {
        if (rect.width <= 0 || rect.height <= 0) return;
        fill_ui_rect(surface, rect, selected ? ::cw::lib::Color{125, 180, 225, 255}
                                             : ::cw::lib::Color{245, 245, 245, 255});
        if (disabled && icon)
            icon = disabled_ui_surface(icon, mask.value_or(std::tuple<::cw::lib::Integer,
                                                                   ::cw::lib::Integer>{0, 0}));
        if (icon) {
            // ResourceTable の項目は、UP_WIN の確定前にキャッシ
            // ュされている場合がある。 現在のウィンドウ拡大率へ正規化するが、枠いっ
            // ぱいに引き伸ばさず、Python と同様に元の寸法で中央へ配置する。
            icon = ::cw::lib::ui::resource_surface_for_window(icon);
            const auto icon_x = rect.x + (rect.width - icon->size.width) / 2;
            const auto icon_y = rect.y + (rect.height - icon->size.height) / 2;
            blend_ui_surface(surface, icon, icon_x, icon_y);
        }
    };

    if (show_additional && (control.m_callname == "BACKPACK" || control.m_callname == "STOREHOUSE")) {
        const std::array<const char*, 3> keys{{"STATUS8", "STATUS9", "STATUS10"}};
        for (std::size_t index = 0; index < keys.size(); ++index) {
            auto icon = resource_dialog_surface(keys[index]);
            const bool enabled = control.m_show_cardtype[index];
            const auto mask = icon
                ? std::optional{std::tuple<::cw::lib::Integer, ::cw::lib::Integer>{
                      std::max<::cw::lib::Integer>(0, icon->size.width - 1), 0}}
                : std::nullopt;
            draw_toolbar_toggle(toolbar.cardtype[index], std::move(icon), enabled, !enabled, mask);
        }
    }

    if (toolbar.editstar.width > 0) {
        // cw/dialog/cardcontrol.py の _update_editstar は ARRANGE_BOOKMARK
        // を使う。 BOOKMARK/BOOKMARK_EMPTY は別の並べ替え優先切り替えに属する。
        draw_toolbar_toggle(toolbar.editstar, resource_dialog_surface("ARRANGE_BOOKMARK"),
                            control.m_edit_star, !control.m_edit_star);
    }

    if (show_additional && can_show_sort_controls(control)) {
        draw_toolbar_toggle(toolbar.sortwithstar,
                            resource_dialog_surface(control.m_sort_with_star
                                                        ? "BOOKMARK" : "BOOKMARK_EMPTY"),
                            control.m_sort_with_star);
        fill_ui_rect(surface, toolbar.sort, {245, 245, 245, 255});
        toolbar_label(cardcontrol_msg("sort_title","整列:"),toolbar.sort.x-cs(2),0,cs(24));
    }

    if (show_additional && control._can_narrow()) {
        const auto narrow_y = panel_h - cs(25);
        fill_ui_rect(surface, {cs(425), narrow_y, cs(90), cs(20)}, {245, 245, 245, 255});
        fill_ui_rect(surface, {cs(323), narrow_y, cs(100), cs(20)}, {245, 245, 245, 255});
        toolbar_label(cardcontrol_msg("narrow_condition","絞込条件"),toolbar.narrow.x-cs(3),toolbar.narrow.y,toolbar.narrow.height);
        // テキストは、下のクリッピング付き共通編集欄で一度だけ描画する。
    }

    // Python と同様、追加行を開いている時は HIDE_CONTROLS、閉じている時
    // は SHOW_CONTROLS を使う。 当たり判定の矩形は compute_toolbar_layout()
    // で取得済みなので、対応するスキン画像をここで一度だけ描く。
    if (toolbar.addctrl.width > 0) {
        draw_toolbar_toggle(
            toolbar.addctrl,
            ::cw::lib::ui::additional_controls_surface(show_additional),
            show_additional);
    }

    const bool disabled_navigation = !control.navigation_enabled();
    const auto navigation_icon = [&](const char* name) {
        const auto icon = resource_button_surface(name);
        return disabled_navigation ? disabled_ui_surface(icon) : icon;
    };
    const auto navigation_width = navigation_button_width(control);
    // wx.BitmapButton と同様、アイコンを共通の角丸ボタン背景の上に置く。
    ::cw::lib::ui::draw_button_face(surface, {0, panel_h, navigation_width, cs(kBottomBarHeight)}, {!disabled_navigation});
    ::cw::lib::ui::draw_button_face(surface, {panel_w - navigation_width, panel_h, navigation_width, cs(kBottomBarHeight)},
                                    {!disabled_navigation});
    draw_chained_button(control,surface, navigation_icon("LMOVE"), 0, panel_h, navigation_width, cs(kBottomBarHeight));
    draw_chained_button(control,surface, navigation_icon("RMOVE"), panel_w - navigation_width, panel_h, navigation_width, cs(kBottomBarHeight));
    const auto close_label = control.m_callname == "CARDPOCKETB"
        ? cardcontrol_msg("return", "戻る")
        : control.m_callname == "HANDVIEW" || control.m_callname == "CARDPOCKET_REPLACE"
            ? cardcontrol_msg("entry_cancel", "中止") : cardcontrol_msg("close", "閉じる");
    const auto close_x = (panel_w - cs(90)) / 2;
    const auto close_y = panel_h + cs(3);
    ::cw::lib::ui::draw_button_face(surface, {close_x, close_y, cs(90), cs(kFooterButtonHeight)});
    centered_label(control,surface,close_label,{close_x,close_y,cs(90),cs(kFooterButtonHeight)},"button",14,{0,0,0});
}

bool should_show_card_price(const CardControl& control)
{
    if (const auto holder = dynamic_cast<const CardHolder*>(&control)) {
        if (control.m_sendto && control.m_sendto_selection &&
            *control.m_sendto_selection == int_value(holder->m__combo_shelf, -1))
            return true;
    }
    return can_show_sort_controls(control) && ::cw::g_cwpy && ::cw::g_cwpy->m_setting &&
           ::cw::g_cwpy->m_setting->m_sort_cards == "Price";
}

std::shared_ptr<::cw::lib::Surface> star_icon_surface(CardControl& control, const CardPtr& header)
{
    if (!control._show_star(header)) return {};
    // Py と同様、共通のスター位置計算と縮小・配布アニメーション変
    // 換の前にホバー画像を選ぶ。 描画座標には一枚の画像だけを描く。
    if (control.m__laststar && object_value<CardPtr::element_type>(*control.m__laststar) == header)
        return resource_dialog_surface("BOOKMARK_LIGHTUP");
    if (header->m_star > 0) return resource_dialog_surface("BOOKMARK");
    if (control.m_edit_star) return resource_dialog_surface("BOOKMARK_EMPTY");
    return {};
}

void draw_card_star(const std::shared_ptr<::cw::lib::Surface>& surface, CardControl& control,
                    const CardPtr& header, ::cw::lib::Integer draw_x, ::cw::lib::Integer draw_y,
                    ::cw::lib::Integer bmp_w, ::cw::lib::Integer bmp_h)
{
    auto star = star_icon_surface(control, header);
    if (!star) return;
    const auto [hit_rect, star_x, star_y] = control._get_starrect(header, draw_x, draw_y, bmp_w, bmp_h);
    (void)hit_rect;
    if (star_x <= 0 && star_y <= 0 && !control._show_star(header)) return;

    auto draw_star_x = star_x;
    auto draw_star_y = star_y;
    auto star_w = star->size.width;
    auto star_h = star->size.height;
    if (header->m_deal_per != 100) {
        star_w = star_w * header->m_deal_per / 100;
        if (star_w <= 0) return;
        // Py と同様、クリック中のカード画像と同じ wx.Image.Rescale の拡縮処理を使う。
        star = ::cw::imageretouch::resize_wx_bilinear(star, star_w, star_h);
        if (!star) return;
    } else if (control.m__starclickedflag) {
        const auto w2 = star_w * 9 / 10;
        const auto h2 = star_h * 9 / 10;
        star = ::cw::imageretouch::resize_wx_bilinear(star, w2, h2);
        if (!star) return;
        draw_star_x += (star_w - w2) / 2;
        draw_star_y += (star_h - h2) / 2;
    }
    blend_ui_surface(surface, star, draw_star_x, draw_star_y);
}

template<class Header>
std::shared_ptr<::cw::lib::Bitmap> rendered_card_bitmap(CardControl& control, const Header& header)
{
    if (!header) return {};
    const auto test_aptitude = control._get_test_aptitude();
    auto bitmap = header->get_cardwxbmp(test_aptitude);
    if (!bitmap) return {};
    if (auto card_image = header->get_cardimg()) {
        const ::cw::lib::Value value(std::static_pointer_cast<::cw::lib::Object>(header));
        // Py の draw_card の描画順に揃え、deal=0 の判定より前にクリック時の表示を処理する。
        if (header->m_clickedflag)
            return card_image->get_wxclickedbmp(value, bitmap, test_aptitude);
        if (header->m_deal_per == 0) return {};
        if (header->m_deal_per != 100) {
            std::optional<std::shared_ptr<::cw::character::Character>> aptitude;
            if (test_aptitude && *test_aptitude)
                aptitude = std::dynamic_pointer_cast<::cw::character::Character>(*test_aptitude);
            return card_image->get_wxdealingbmp(value, bitmap, header->m_deal_per, aptitude);
        }
    }
    return bitmap;
}

void draw_cardcontrol_cards(CardControl& control,
                            const std::set<const ::cw::lib::Object*>* dirty_cards = nullptr)
{
    auto& surface = control.m_ui_surface;
    if (!surface) return;
    std::optional<CardPricePresentation> price;
    if (should_show_card_price(control)) {
        const auto style=::cw::lib::ui::configured_game_font("price",16,
            cardcontrol_ui_scale(),true,false,"ＭＳ 明朝");
        price.emplace(style,cs(2),cs(80));
        control.m__price_sheet=::cw::lib::Value(price->sheet());
    } else {
        control.m__price_sheet=::cw::lib::Value{};
    }

    struct CardDrawState {
        CardPtr header{};
        ::cw::lib::Integer draw_x{};
        ::cw::lib::Integer draw_y{};
        ::cw::lib::Integer bmp_w{};
        ::cw::lib::Integer bmp_h{};
    };
    std::vector<CardDrawState> card_headers;

    for (const auto& value : control.m_draw_headers) {
        const auto item = value_to_header(value);
        if (!item) continue;
        std::visit([&](const auto& header) {
            if (!header) return;
            if (dirty_cards && dirty_cards->count(header.get()) == 0) return;
            const auto rect_it = control.m_card_rects.find(header.get());
            if (rect_it == control.m_card_rects.end()) return;
            const auto rect = rect_it->second;
            const auto drawn = control.m_draw_bitmaps.find(header.get());
            const auto wxbmp = drawn == control.m_draw_bitmaps.end() ? nullptr : drawn->second;
            if (!wxbmp) return;

            std::shared_ptr<::cw::lib::Surface> image = surface_from_card_wxbmp(wxbmp);
            if (!image || image->size.width <= 0 || image->size.height <= 0) return;

            // Python の OnPaint2 と同様、get_cardwxbmp を元の寸法で wxrect の中央へ描く。
            const auto slot_x = cs(rect.x);
            const auto slot_y = cs(rect.y);
            const auto slot_w = cs(rect.width);
            const auto slot_h = cs(rect.height);
            const auto draw_x = slot_x + (slot_w - image->size.width) / 2;
            const auto draw_y = slot_y + (slot_h - image->size.height) / 2;
            blend_ui_surface(surface, image, draw_x, draw_y);
            if constexpr (std::is_same_v<std::decay_t<decltype(header)>, CardPtr>)
                card_headers.push_back({header, draw_x, draw_y, image->size.width, image->size.height});
        }, *item);
    }

    const auto draw_price=[&](const CardDrawState& card) {
        if(price)price->draw(surface,card.header->can_selling()
            ?std::to_string(card.header->get_sellingprice()):"---",
            card.draw_x,card.draw_y,card.bmp_w,card.bmp_h);
    };
    const bool edit_star = ::cw::g_cwpy && ::cw::g_cwpy->m_setting && ::cw::g_cwpy->m_setting->m_edit_star;
    for (const auto& card : card_headers) {
        if (edit_star) {
            draw_price(card);
            draw_card_star(surface, control, card.header, card.draw_x, card.draw_y, card.bmp_w, card.bmp_h);
        } else {
            draw_card_star(surface, control, card.header, card.draw_x, card.draw_y, card.bmp_w, card.bmp_h);
            draw_price(card);
        }
    }

    // 交換用の矢印はカードの後に合成する。ホバー時の強調表示を含むス
    // ターは、共通のスター描画・アニメーション経路で処理済みとする。
    if (control._can_repls()) {
        auto candidates = control.get_headers();
        const auto previous = control.get_beforepageheaders();
        if (!previous.empty()) candidates.insert(candidates.begin(), previous.back());
        for (const auto& value : candidates) {
            const auto [rect, unused_x, unused_y] = control._get_replsrect(value);
            if (rect.width <= 0) continue;
            auto arrow = resource_dialog_surface("REPLACE_POSITION");
            if (!arrow) continue;
            if (control.m__lastrepls && same_value(value, *control.m__lastrepls))
                arrow = ::cw::imageretouch::add_lightness(arrow, 64);
            if (control.m__replclickedflag) {
                const auto w = std::max<::cw::lib::Integer>(1, arrow->size.width * 9 / 10);
                const auto h = std::max<::cw::lib::Integer>(1, arrow->size.height * 9 / 10);
                blend_ui_surface(surface, ::cw::image::smoothscale_card(arrow, {w, h}, true),
                    cs(rect.x) + (cs(rect.width) - w) / 2, cs(rect.y) + (cs(rect.height) - h) / 2);
            } else blend_ui_surface(surface, arrow, cs(rect.x), cs(rect.y));
        }
    }
}

std::optional<::cw::lib::Value> ui_card_value(const CardControl& control,
                                               ::cw::lib::Point position)
{
    for (const auto& value : control.m_draw_headers) {
        const auto item = value_to_header(value);
        if (!item) continue;
        bool hit = false;
        std::visit([&](const auto& header) {
            if (!header) return;
            const auto found = control.m_card_rects.find(header.get());
            if (found == control.m_card_rects.end()) return;
            const auto& rect = found->second;
            hit = position.x >= rect.x && position.y >= rect.y && position.x < rect.x + rect.width &&
                  position.y < rect.y + rect.height;
        }, *item);
        if (hit) return value;
    }
    return std::nullopt;
}

::cw::lib::String cardcontrol_component_key(const ::cw::lib::Object* object)
{
    return "card:" + std::to_string(reinterpret_cast<std::uintptr_t>(object));
}

void invalidate_cardcontrol_pixels(CardControl& control, ::cw::lib::Rect rect)
{
    control.invalidate_dialog_presentation(rect);
    control.m_ui_invalidated = true;
}

void invalidate_cardcontrol_cards_under(CardControl& control, const ::cw::lib::Rect& pixels)
{
    for (const auto& value : control.m_draw_headers) {
        const auto item = value_to_header(value);
        if (!item) continue;
        std::visit([&](const auto& header) {
            if (!header) return;
            const auto found = control.m_card_rects.find(header.get());
            if (found == control.m_card_rects.end()) return;
            const auto& logical = found->second;
            const ::cw::lib::Rect card{cs(logical.x), cs(logical.y),
                                       cs(logical.width), cs(logical.height)};
            if (pixels.x < card.x + card.width && card.x < pixels.x + pixels.width &&
                pixels.y < card.y + card.height && card.y < pixels.y + pixels.height)
                control.draw_card(value, true);
        }, *item);
    }
}

void invalidate_cardcontrol_replacement(CardControl& control, const ::cw::lib::Value& value)
{
    const auto rect = std::get<0>(control._get_replsrect(value));
    if (rect.width > 0 && rect.height > 0) {
        const ::cw::lib::Rect pixels{cs(rect.x), cs(rect.y), cs(rect.width), cs(rect.height)};
        invalidate_cardcontrol_pixels(control, pixels);
        invalidate_cardcontrol_cards_under(control, pixels);
    }
}

void invalidate_cardcontrol_star(CardControl& control, const ::cw::lib::Value& value)
{
    const auto header = object_value<CardPtr::element_type>(value);
    if (!header) return;
    const auto rect = std::get<0>(control._get_starrect(header));
    if (rect.width > 0 && rect.height > 0) {
        invalidate_cardcontrol_pixels(control, rect);
        // 強調表示はカードの上に合成する。その小さな矩形だけを復
        // 元すると、下にある枠部分だけの背景が露出してしまう。
        invalidate_cardcontrol_cards_under(control, rect);
    }
}

void decorate_cardcontrol_fields(CardControl& control);

}  // namespace

void CommonCardControlUiBackend::bind(CardControl& arg_control)
{
    // m_parent は所有者であり、このダイアログの表示ウィンドウではない。
    layout(arg_control);
    invalidate(arg_control);
}

void CommonCardControlUiBackend::layout(CardControl& arg_control)
{
    // portable compat::Window の bounds は native host が指定した値を尊重し、
    // 未指定の場合だけカード描画に必要な最小領域を設定する。
    if (arg_control.m_parent && (arg_control.m_parent->bounds.width <= 0 || arg_control.m_parent->bounds.height <= 0))
        arg_control.m_parent->bounds = {0, 0, 640, 360};
    arg_control.m_ui_invalidated = true;
}

void CommonCardControlUiBackend::update_debug(CardControl& arg_control)
{
    // Debug コンボは compat::Window::id に保持された選択をモデルが読むため、
    // SDL/native 側は invalidate だけを行えば同じ状態を投影できる。
    arg_control.m_ui_invalidated = true;
}

void CommonCardControlUiBackend::paint(CardControl& arg_control, std::shared_ptr<::cw::lib::Window> arg_event)
{
    if (!arg_control.m__redraw) return;
    const auto width = cs(panel_width(arg_control));
    const auto height = cs(kDialogHeight);
    auto& retained = arg_control.dialog_retained_surface();
    const bool full = retained.needs_full({width, height});
    const auto left_chrome = ::cw::lib::Rect{0, 0, cs(94), cs(kPanelHeight)};
    const auto top_chrome = ::cw::lib::Rect{cs(94), 0, width - cs(94), cs(27)};
    const auto lower_chrome = ::cw::lib::Rect{0, cs(kPanelHeight - 25), width,
                                               height - cs(kPanelHeight - 25)};
    bool repaint_chrome = full;
    if (full) {
        retained.allocate({width, height}, {0, 0, 0, 255});
        arg_control.m_ui_surface = retained.surface;
        decorate_cardcontrol_base(arg_control);
        retained.set_component("chrome.left", left_chrome);
        retained.set_component("chrome.top", top_chrome);
        retained.set_component("chrome.lower", lower_chrome);
        if (expanded_pc_paging(arg_control))
            retained.set_component("chrome.pages", {cs(kPanelWidth), 0, cs(60), height});
        for (const auto& [card, logical] : arg_control.m_card_rects)
            retained.set_component(cardcontrol_component_key(card),
                {cs(logical.x), cs(logical.y), cs(logical.width), cs(logical.height)});
        retained.capture_base();
        decorate_cardcontrol_chrome(arg_control);
        decorate_cardcontrol_fields(arg_control);
        draw_cardcontrol_cards(arg_control);
    } else {
        // カードのホバーと押下には、それぞれ独立した更新領域がある。 モデルや配置の
        // 変更では三つの枠領域だけを再描画し、カードと動画のピクセルには触れない。
        if (arg_control.m_ui_invalidated && arg_control.m_ui_dirty_cards.empty()) {
            repaint_chrome = retained.invalidate_component("chrome.left") || repaint_chrome;
            repaint_chrome = retained.invalidate_component("chrome.top") || repaint_chrome;
            repaint_chrome = retained.invalidate_component("chrome.lower") || repaint_chrome;
            if (expanded_pc_paging(arg_control))
                repaint_chrome = retained.invalidate_component("chrome.pages") || repaint_chrome;
        }
        for (const auto* card : arg_control.m_ui_dirty_cards) {
            if (retained.invalidate_component(cardcontrol_component_key(card))) continue;
            const auto found = arg_control.m_card_rects.find(card);
            if (found != arg_control.m_card_rects.end()) {
                const auto& logical = found->second;
                retained.invalidate({cs(logical.x), cs(logical.y), cs(logical.width), cs(logical.height)});
            }
        }
        retained.restore_dirty();
        arg_control.m_ui_surface = retained.surface;
        if (repaint_chrome) {
            decorate_cardcontrol_chrome(arg_control);
            decorate_cardcontrol_fields(arg_control);
        }
        draw_cardcontrol_cards(arg_control, &arg_control.m_ui_dirty_cards);
    }
    if (arg_event) arg_event->bounds = {0, 0, panel_width(arg_control), kDialogHeight};
    arg_control.m_ui_dirty_cards.clear();
    retained.finish();
    arg_control.m_ui_invalidated = false;
}

void CommonCardControlUiBackend::draw_additionals(CardControl& arg_control, std::shared_ptr<::cw::lib::DeviceContext> arg_dc)
{
    (void)arg_control;
    (void)arg_dc;
}

void CommonCardControlUiBackend::pump(CardControl& arg_control)
{
    // Python と同様、クリック・配布アニメーションのコールバックを
    // フレーム単位のキューで扱い、一フレームに一つだけ実行する。 ここで
    // deque 全体を処理すると、押下画像がほとんど見えなくなる。
    if (!arg_control.m_ui_callbacks.empty()) {
        auto callback = std::move(arg_control.m_ui_callbacks.front());
        arg_control.m_ui_callbacks.pop_front();
        if (callback) callback({}, {});
    }
    if (!arg_control.m__quit) {
        ++arg_control.m__animate_frame;
        bool animated = false;
        for (auto& [id, image] : arg_control.m__dialog_anim_cardimgs) {
            if (!image || !image->update_animation()) continue;
            animated = true;
            for (const auto& value : arg_control.m_draw_headers) {
                const auto item = value_to_header(value);
                if (!item) continue;
                std::visit([&](const auto& header) {
                    if (header && header->get_cardimg() == image)
                        arg_control.draw_card(value, false);
                }, *item);
            }
        }
        if (animated) arg_control.m_ui_invalidated = true;
        // Python と同様、押下カードを一度描き直してから、カード全
        // 体を再合成せず四フレーム待ち、解除後のカードを再描画する。 し
        // たがって、時間調整だけの予約コールバックは再描画要求ではない。
        if (animated || arg_control.m_ui_invalidated) paint(arg_control, nullptr);
    }
}

void CommonCardControlUiBackend::start_timer(CardControl& arg_control)
{
    arg_control.m__animtimer = ::cw::lib::Value(true);
    arg_control.m_ui_invalidated = true;
}

void CommonCardControlUiBackend::stop_timer(CardControl& arg_control)
{
    arg_control.m__animtimer = {};
    arg_control.m_ui_callbacks.clear();
}

void CommonCardControlUiBackend::invalidate(CardControl& arg_control)
{
    arg_control.m_ui_invalidated = true;
    arg_control.invalidate_dialog_presentation();
}

std::optional<::cw::lib::Value> CommonCardControlUiBackend::hit_test(const CardControl& arg_control,
                                                                   ::cw::lib::Point arg_position) const
{
    return ui_card_value(arg_control, arg_position);
}

void CommonCardControlUiBackend::post(CardControl& arg_control, ::cw::lib::Callback arg_callback)
{
    // アニメーションの継続処理は、それを予約した入力操作に属する。 フレ
    // ームごとの全コールバックにわたって、その操作の受付権を保持する。
    if (arg_callback) arg_control.m_ui_callbacks.push_back(
        ::cw::dialog::retain_current_dialog_input(std::move(arg_callback)));
    start_timer(arg_control);
}

void CommonCardControlUiBackend::show_error(CardControl& arg_control, const ::cw::lib::String& arg_message)
{
    arg_control.m_ui_last_error = arg_message;
    auto message = std::make_shared<::cw::dialog::message::Message>();
    auto parent = std::make_shared<::cw::lib::Window>();
    message->initialize(parent, cardcontrol_msg("message", "メッセージ"), arg_message, 2, std::nullopt);
    const bool was_busy = arg_control.m__proc;
    arg_control.m__proc = true;
    struct RestoreBusy {
        CardControl& control; bool value;
        ~RestoreBusy() { control.m__proc = value; }
    } restore{arg_control, was_busy};
    if (!::cw::dialog::show_common_modal_presentation(message) && !message->dialog_presentation_finished())
        message->OnCancel(nullptr);
    // エラーは一時的な子メッセージに属する。Python と同様、そ
    // の子を破棄し、復元したカード操作画面へエラー文を描き込まない。
    arg_control.m_ui_last_error.clear();
    arg_control.dialog_presentation_pointer_cancel();arg_control.focus_control({},false);
    arg_control.OnLeave(nullptr);
    invalidate(arg_control);
}

bool CommonCardControlUiBackend::is_visible(const CardControl& arg_control) const noexcept
{
    return !arg_control.m__quit;
}

void CommonCardControlUiBackend::close(CardControl& arg_control)
{
    // キャッシュの破棄時も、通常のキャンセルや OK と同じ動画の所有権を解放する。
    arg_control._stop_animtimer();
    // ホストが閉じるのはこのモデルのウィンドウであり、m_parent を隠してはならない
    // 。 交換用の子画面は、引き続き有効な CardHolder と同じ所有者を共有する。
    arg_control.m__animtimer = {};
    arg_control.m_ui_callbacks.clear();
    arg_control.m_ui_invalidated = false;
}

void CommonCardControlUiBackend::show_info(CardControl& arg_control, const ::cw::lib::Value& arg_header)
{
    const auto selected = value_to_header(arg_header);
    if (!selected) return;
    std::vector<::cw::dialog::cardinfo::CardInfoItem> list;
    const auto headers = arg_control.get_headers();
    list.reserve(headers.size());
    std::optional<::cw::dialog::cardinfo::CardInfoItem> selection;
    for (const auto& value : headers) {
        const auto item = value_to_header(value);
        if (!item) continue;
        std::visit([&](const auto& header) {
            if (!header) return;
            ::cw::dialog::cardinfo::CardInfoItem info_item{header};
            if (!selection && same_value(value, arg_header)) selection = info_item;
            list.push_back(std::move(info_item));
        }, *item);
    }
    if (!selection || list.empty()) return;
    auto dialog = std::make_shared<::cw::dialog::cardinfo::YadoCardInfo>();
    dialog->m_close_frame_dialog = false;
    arg_control.m_ui_info_header = arg_header;
    dialog->set_parent_callbacks(
        [&arg_control](const auto& item) {
            if (arg_control.m_ui_info_header) {
                if (const auto previous = value_to_header(*arg_control.m_ui_info_header))
                    std::visit([](const auto& header) { if (header) header->m_negaflag = false; }, *previous);
            }
            std::visit([](const auto& header) { if (header) header->m_negaflag = true; }, item);
            arg_control.m_ui_info_header = std::visit(
                [](const auto& header) { return header ? ::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(header)) : ::cw::lib::Value{}; }, item);
            if (arg_control.m_ui_backend) arg_control.m_ui_backend->invalidate(arg_control);
        },
        [&arg_control]() {
            if (!arg_control.m__quit && arg_control.m_ui_backend) arg_control.m_ui_backend->invalidate(arg_control);
        },
        [&arg_control](const auto& item) {
            if (auto* holder = dynamic_cast<CardHolder*>(&arg_control)) {
                const auto value = std::visit([](const auto& card) -> ::cw::lib::Value {
                    return card ? ::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(card))
                                : ::cw::lib::Value{};
                }, item);
                holder->update_cardpocketinfo_with(value);
            }
        });
    dialog->initialize(arg_control.m_parent, std::move(list), *selection);
    arg_control.m_cardinfo_dialog = dialog;
    // Python と同様、子モーダルとして開く。visible=true に
    // するだけでは共通 Presentation が公開されず、ホストで描画も
    // 終了もできない。 共通のモーダル処理で各ホストの所有者を維持・復元する。
    const bool shown = ::cw::dialog::show_common_modal_presentation(dialog);
    if (!shown && !dialog->dialog_presentation_finished())
        dialog->dialog_presentation_action(::cw::dialog::DialogPresentationAction::Cancel, -1);
    arg_control.m_cardinfo_dialog.reset();
    // CardInfo と CardControl は意図的に CardImage
    // モデルを共有する。 子画面を閉じるとアニメーションの所有権が解放され
    // るため、次のタイマー処理より前に親の表示カードの所有権を再取得する。
    arg_control._sync_dialog_animated_paths();
    arg_control._start_animtimer();
    arg_control.dialog_presentation_pointer_cancel();arg_control.focus_control({},false);
    arg_control.OnLeave(nullptr);
    invalidate(arg_control);
}

std::shared_ptr<CardControlUiBackend> get_cardcontrol_ui_backend()
{
    // 唯一のコントローラーは API 境界で不変とする。ホスト
    // は Surface の合成と共通操作の転送だけを行う。
    static const auto controller = std::make_shared<CommonCardControlUiBackend>();
    return controller;
}

void CardControl::initialize(std::shared_ptr<::cw::lib::Window> arg_parent)
{
    dialog_presentation_pointer_cancel();m_chain_controls.clear();
    m_focus_target.reset();m_space_held=false;m_keyboard_focus=false;m_keyboard_scale=-1;
    m_side_navigation.reset();m_side_controls.clear();m_side_exclusions.clear();m_side_layout_scale=-1;
    m_parent = std::move(arg_parent);
    // 各ダイアログが共通の表示コントローラーを一つ所有する。 ホ
    // ストはその Surface の合成と操作の転送だけを行う。
    m_ui_backend = std::make_shared<CommonCardControlUiBackend>();
    m_ui_callbacks.clear();
    m_ui_surface.reset();
    dialog_retained_surface().reset();
    m_ui_dirty_cards.clear();
    m_ui_pad.reset();
    m_ui_info_header.reset();
    m_ui_last_error.clear();
    m_ui_invalidated = true;
    m__redraw = true;
    m__cancel_animation = true;
    m__quit = false;
    m__proc = false;
    m__dialog_anim_owner = "CardControl";
    m_narrow_selection = 0;
    m_sort_selection = 0;
    m_sort_with_star = false;
    m_sort_enabled = false;
    m_edit_star = false;
    m_show_additional_controls = true;
    m_sendto = false;
    m_sendto_selection.reset();
    m_choice_popup = -1;
    m_choice_cursor = 0;m_choice_first=0;m_choice_visible=1;
    m_choice_drag.reset();m_choice_prefix.clear();m_choice_composition.clear();
    m_input_focus = -1;
    m_narrow_input={};m_page_editor={};
    m_page_input.clear();
    m_pointer_card.reset();
    m_pointer_bounds = {};
    m_pointer_reorder = false;
    m_show_cardtype = {{true, true, true}};
    m_narrow_choices.clear();
    m_leftkeyid = ::cw::lib::Value(1001);
    m_rightkeyid = ::cw::lib::Value(1002);
    m_returnkeyid = ::cw::lib::Value(1003);
    m_infokeyid = ::cw::lib::Value(1004);
    m__drawlist.clear();
    m_draw_bitmaps.clear();
    m__dialog_anim_paths.clear();
    m__dialog_anim_cardimgs.clear();
    if (m_ui_backend) m_ui_backend->bind(*this);
}

void CardControl::reconstruct(::cw::lib::String arg_callname, ::cw::lib::String arg_name, bool arg_sendto,
                              bool arg_sort, std::optional<::cw::lib::Integer> arg_areaid)
{
    retire_keyboard_press();m_focus_target.reset();m_keyboard_focus=false;
    retire_side_input();
    m_side_controls.clear();m_side_exclusions.clear();m_side_layout_scale=-1;
    if(!m_chain_dispatching) retire_chain_input();
    retire_field_inputs();m_choice_popup=-1;m_choice_first=0;
    m_choice_drag.reset();m_choice_prefix.clear();m_choice_composition.clear();
    m_narrow_input.edit.reset("",false,false);m_page_editor.edit.reset("",false,false);
    m_narrow_input.modifiers=0;m_page_editor.modifiers=0;m_page_input.clear();
    m_callname = std::move(arg_callname);
    m_title = cardcontrol_msg("card_control", "カード操作") + " - " + arg_name;
    m_sendto = arg_sendto;
    m_cwpy_debug = ::cw::g_cwpy ? ::cw::g_cwpy->is_debugmode() : m_cwpy_debug;
    m_addctrlbtn = ::cw::lib::Value(
        m_callname == "STOREHOUSE" || m_callname == "BACKPACK" ||
        m_callname == "CARDPOCKET" || m_callname == "CARDPOCKETB" ||
        m_callname == "INFOVIEW");
    if (arg_areaid)
        m_areaid = *arg_areaid;
    else if (::cw::g_cwpy)
        m_areaid = ::cw::g_cwpy->m_areaid;
    m__quit = false;
    m__proc = false;
    m__cancel_animation = true;
    m__redraw = true;
    m__after_event = {};
    m__drawlist.clear();
    m_draw_bitmaps.clear();
    m__leftmarks.clear();
    m__starclickedflag = false;
    m__replclickedflag = false;
    m__animate_frame = 0;
    m__price_sheet = {};
    m_sendto_selection.reset();
    m_sort_selection = arg_sort ? m_sort_selection : 0;
    m_narrow_text.clear();
    m_narrow_selection = 0;
    m__laststar.reset();
    m__lastrepls.reset();
    if (!arg_sort) m_sort_selection = 0;
    m_sort_enabled = arg_sort;
    if (::cw::g_cwpy && ::cw::g_cwpy->m_setting) {
        const auto& setting = ::cw::g_cwpy->m_setting;
        m_edit_star = setting->m_edit_star;
        m_sort_with_star = setting->m_sort_cardswithstar;
        m_show_additional_controls = setting->m_show_additional_card;
        m_narrow_text = setting->m_card_narrow;
        m_narrow_selection = m_callname == "INFOVIEW" ? setting->m_infoview_narrowtype : setting->m_card_narrowtype;
        if (arg_sort) {
            const auto& sort_names = card_sort_names();
            const auto sort_name = setting->m_sort_cards;
            const auto it = std::find(sort_names.begin(), sort_names.end(), sort_name);
            if (it != sort_names.end()) m_sort_selection = static_cast<::cw::lib::Integer>(std::distance(sort_names.begin(), it));
        }
        for (std::size_t i = 0; i < m_show_cardtype.size(); ++i)
            m_show_cardtype[i] = setting_sequence_bool(setting, setting->m_show_cardtype, i, true);
    }
    m_sort = ::cw::lib::Value(m_sort_selection);
    m_narrow = ::cw::lib::Value(m_narrow_text);
    m_narrow_type = ::cw::lib::Value(m_narrow_selection);
    const auto all_label = cardcontrol_msg("all", "すべて");
    const auto name_label = cardcontrol_msg("card_name", "カード名");
    const auto desc_label = cardcontrol_msg("description", "説明");
    const auto scenario_label = cardcontrol_msg("scenario_name", "シナリオ");
    const auto author_label = cardcontrol_msg("author", "作者");
    const auto key_label = cardcontrol_msg("key_code", "キーコード");
    if (m_callname == "INFOVIEW") {
        m_narrow_choices = {all_label, name_label, desc_label};
    } else if (::cw::g_cwpy && ::cw::g_cwpy->is_debugmode()) {
        m_narrow_choices = {all_label, name_label, desc_label, scenario_label, author_label, key_label};
    } else {
        m_narrow_choices = {all_label, name_label, desc_label, scenario_label, author_label};
    }
    // Py と同様、保存済みの位置が現在の選択肢を超えている場合は名前の項目を
    // 選ぶ。 -1 は wx の未選択値として維持し、全項目の選択にはしない。
    if (m_narrow_selection >= static_cast<::cw::lib::Integer>(m_narrow_choices.size()))
        m_narrow_selection = 1;
    m_narrow_type = ::cw::lib::Value(m_narrow_selection);
    _update_sortwithstar();
    _update_editstar();
    m__sendto = ::cw::lib::Value(arg_sendto);
    if (!m_addctrlbtn.is_none()) update_additionals();
    draw_cards(true, -1);
    _start_animtimer();
    focus_control({}, false);
}

void CardControl::_start_animtimer()
{
    _sync_dialog_animated_paths();
    if (m_ui_backend) m_ui_backend->start_timer(*this);
}
void CardControl::_stop_animtimer()
{
    m__cancel_animation = true;
    m__proc = false;
    // ネイティブ版では終了時に予約済みのアニメーションコールバックを破棄する。 Py
    // はキャンセル分岐でこれらのフラグを消すため、ここでも同じ後処理を行う。
    m__starclickedflag = false;
    m__replclickedflag = false;
    for (const auto& [id, image] : m__dialog_anim_cardimgs) {
        (void)id;
        if (image) image->release_animated_paths();
    }
    m__dialog_anim_paths.clear();
    m__dialog_anim_cardimgs.clear();
    if (m_ui_backend) m_ui_backend->stop_timer(*this);
}
void CardControl::_sync_dialog_animated_paths()
{
    std::set<::cw::lib::String> paths;
    std::map<::cw::lib::Integer, std::shared_ptr<::cw::image::CardImage>> cardimgs;
    for (const auto& value : get_headers()) {
        std::shared_ptr<::cw::image::CardImage> image;
        if (const auto item = value_to_header(value))
            std::visit([&image](const auto& header) { if (header) image = header->get_cardimg(); }, *item);
        if (!image) continue;
        image->update_animated_paths();
        const auto image_paths = image->get_animated_paths();
        // 静止カード画像のためにモーダル表示のタイマーを動かし続けない。
        // 実際にアニメーションするリソースを持つ画像だけを定期確認する。
        if (image_paths.empty()) continue;
        const auto id = static_cast<::cw::lib::Integer>(reinterpret_cast<std::uintptr_t>(image.get()));
        cardimgs[id] = image;
        paths.insert(image_paths.begin(), image_paths.end());
    }
    for (const auto& [id, image] : m__dialog_anim_cardimgs)
        if (!cardimgs.contains(id) && image) image->release_animated_paths();
    m__dialog_anim_cardimgs = std::move(cardimgs);
    m__dialog_anim_paths = std::move(paths);
}
void CardControl::_bind() { if (m_ui_backend) m_ui_backend->bind(*this); }
bool CardControl::_can_narrow()
{
    return m_callname == "STOREHOUSE" || m_callname == "BACKPACK" || m_callname == "CARDPOCKETB" ||
           m_callname == "INFOVIEW";
}
void CardControl::OnNumberKeyDown(std::shared_ptr<::cw::lib::Window> arg_event)
{
    if (!arg_event) return;
    m__cancel_animation = true;
    const auto narrow = std::find_if(m_narrowkeydown.begin(), m_narrowkeydown.end(),
                                     [arg_event](const auto& id) { return int_value(id, -1) == arg_event->id; });
    const auto narrow_index = narrow == m_narrowkeydown.end() ? arg_event->id - 1 :
        static_cast<::cw::lib::Integer>(std::distance(m_narrowkeydown.begin(), narrow));
    if ((narrow != m_narrowkeydown.end() ||
         (m_narrowkeydown.empty() && arg_event->id >= 1 && arg_event->id <=
          static_cast<::cw::lib::Integer>(m_narrow_choices.size()))) &&
        _can_narrow() && m_show_additional_controls && narrow_index >= 0 &&
        narrow_index < static_cast<::cw::lib::Integer>(m_narrow_choices.size())) {
        m_narrow_selection = narrow_index;
        m_narrow_type = ::cw::lib::Value(m_narrow_selection);
        OnNarrowCondition(std::move(arg_event));
        return;
    }
    const auto sort = std::find_if(m_sortkeydown.begin(), m_sortkeydown.end(),
                                   [arg_event](const auto& id) { return int_value(id, -1) == arg_event->id; });
    const auto sort_index = sort == m_sortkeydown.end() ? arg_event->id - 1 :
        static_cast<::cw::lib::Integer>(std::distance(m_sortkeydown.begin(), sort));
    if ((sort != m_sortkeydown.end() ||
        (m_sortkeydown.empty() && m_sort_enabled && m_show_additional_controls &&
         arg_event->id >= 1 && arg_event->id <= 8)) && m_sort_enabled &&
        m_show_additional_controls && sort_index >= 0 && sort_index < 8) {
        m_sort_selection = sort_index;
        m_sort = ::cw::lib::Value(m_sort_selection);
        // wx/Python と同様、合成コンボボックスイベントを OnSort
        // へ送る。 アクセラレーターによる並べ替えも同じ仮想関数を通し、CardHolder
        // のフォーカス・効果音・モード別処理を省略しない。
        auto selected = std::make_shared<::cw::lib::Window>();
        selected->id = sort_index;
        OnSort(std::move(selected));
    }
}
void CardControl::_do_layout() { if (m_ui_backend) m_ui_backend->layout(*this); }
void CardControl::OnClickLeftBtn(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    // 左右ページ/対象切替はCardHolder・HandViewの派生実装で処理する。
}
void CardControl::OnClickRightBtn(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    // 左右ページ/対象切替はCardHolder・HandViewの派生実装で処理する。
}
void CardControl::OnDebugMode(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    if (::cw::g_cwpy) {
        const auto runtime = ::cw::g_cwpy;
        const auto frame = object_value<::cw::frame::Frame>(runtime->m_frame);
        const auto owner = frame
            ? std::dynamic_pointer_cast<CardControl>(frame->dialog_presentation_model())
            : std::shared_ptr<CardControl>{};
        // Python はゲームスレッドで Engine の状態を変えてから
        // 、UI スレッドへ update_debug を送る。 ここで更新
        // すると古い実行状態を読み、表示上の切り替えを直ちに戻してしまう。
        runtime->exec_func([runtime, frame, owner](const auto&, const auto&) {
            if (::cw::g_cwpy != runtime) return ::cw::lib::Value{};
            runtime->play_sound("page", false, 100, 1, 0, 0, false);
            // 反転後の値はゲームスレッドで求める。 キー押下が二回予約された
            // 場合は二回反転させ、両方で同じ古い値の反転結果を公開しない。
            runtime->set_debug(!runtime->is_debugmode());
            if (frame && owner) {
                frame->exec_func([runtime, owner](const auto&, const auto&) {
                    if (::cw::g_cwpy == runtime && !owner->m__quit) owner->update_debug();
                    return ::cw::lib::Value{};
                });
            }
            return ::cw::lib::Value{};
        });
    } else {
        m_cwpy_debug = !m_cwpy_debug;
        update_debug();
    }
}
void CardControl::update_debug()
{
    if (::cw::g_cwpy) m_cwpy_debug = ::cw::g_cwpy->is_debugmode();
    if (m_callname != "INFOVIEW") {
        const auto key_code = cardcontrol_msg("key_code", "キーコード");
        const auto found = std::find(m_narrow_choices.begin(), m_narrow_choices.end(), key_code);
        if (m_cwpy_debug) {
            if (found == m_narrow_choices.end()) m_narrow_choices.push_back(key_code);
        } else if (found != m_narrow_choices.end()) {
            const auto index = static_cast<::cw::lib::Integer>(
                std::distance(m_narrow_choices.begin(), found));
            if (m_narrow_selection == index) {
                m_narrow_selection = 0;
                m_narrow_type = ::cw::lib::Value(0);
                update_narrowcondition();
            }
            m_narrow_choices.erase(found);
        }
    }
    if (m_ui_backend) m_ui_backend->update_debug(*this);
}
void CardControl::update_additionals()
{
    // Py と同様、現在の切り替え状態を変えずに同期する。 ネイ
    // ティブ入力は、この共通 API を呼ぶ前に状態を変更する。
    if(!m_show_additional_controls) retire_field_inputs();
    if (::cw::g_cwpy && ::cw::g_cwpy->m_setting)
        ::cw::g_cwpy->m_setting->m_show_additional_card = m_show_additional_controls;
    m_addctrlbtn = ::cw::lib::Value(true);
    set_cardpos(get_posmode());
}
void CardControl::OnClose(std::shared_ptr<::cw::lib::Window> arg_event) { OnCancel(std::move(arg_event)); }
void CardControl::OnDestroy(std::shared_ptr<::cw::lib::Window> arg_event) { (void)arg_event; retire_field_inputs();_stop_animtimer(); }
void CardControl::OnAnimate(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    if (m__quit || (m_ui_backend && !m_ui_backend->is_visible(*this))) return;
    // Python の OnAnimate は force_exec_func(update, True)
    // を送る。 C++ では表示専用の要求を使い、WM_TIMER から実行状態の更新を呼ばない。
    if (::cw::g_cwpy) ::cw::g_cwpy->request_presentation_update();
    if (m_ui_backend) m_ui_backend->pump(*this);
    _sync_dialog_animated_paths();
}
void CardControl::OnToggleAdditionalControls(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    // Python と同様、Ctrl+F はこの四つのモードだけで有効にする。 CARDPOCKET
    // に通常の追加コントロールボタンがあっても、このアクセラレーターは動作させない。
    if (m_addctrlbtn.is_none() || (m_callname != "STOREHOUSE" &&
        m_callname != "BACKPACK" && m_callname != "CARDPOCKETB" &&
        m_callname != "INFOVIEW")) return;
    _additional_controls();
}
void CardControl::OnAdditionalControls(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    _additional_controls();
}
void CardControl::_additional_controls()
{
    m__cancel_animation = true;
    if (::cw::g_cwpy) ::cw::g_cwpy->play_sound("equipment", false, 100, 1, 0, 0, false);
    m__redraw = false;
    m_show_additional_controls = !m_show_additional_controls;
    update_additionals();
    const std::weak_ptr<int> lifetime = m_deferred_lifetime;
    const auto finish = [this,lifetime](const auto&, const auto&) -> ::cw::lib::Value {
        if (lifetime.expired()) return {};
        _do_layout();
        m__redraw = true;
        update_narrowcondition();
        request_dialog_presentation_refresh();
        return {};
    };
    if (::cw::g_cwpy) {
        if (const auto frame = object_value<::cw::frame::Frame>(::cw::g_cwpy->m_frame)) {
            frame->exec_func(finish);
            return;
        }
    }
    if (m_ui_backend) m_ui_backend->post(*this,finish);
}
void CardControl::OnNarrowCondition(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    if (m__quit || (m_ui_backend && !m_ui_backend->is_visible(*this))) return;
    m__cancel_animation = true;
    if (::cw::g_cwpy) ::cw::g_cwpy->play_sound("page", false, 100, 1, 0, 0, false);
    focus_control(FocusTarget{::cw::dialog::DialogPresentationAction::Auxiliary,12},true);
    m__reserved_narrowconditin = true;
    // モデル所有の UI キューは、共通ホストで wx.CallAfter
    // に相当する。 Py と同様、遅延コールバックの実行時に最新入力
    // を一度だけ読む。IME の通知ごとに即座に絞り込みを行わない。
    auto update = [this](const auto&, const auto&) -> ::cw::lib::Value {
        if (!m__reserved_narrowconditin) return {};
        m__reserved_narrowconditin = false;
        if (m__quit) return {};
        m_narrow = ::cw::lib::Value(m_narrow_text);
        m_narrow_type = ::cw::lib::Value(m_narrow_selection);
        if (::cw::g_cwpy && ::cw::g_cwpy->m_setting) {
            const auto runtime = ::cw::g_cwpy;
            const auto setting = runtime->m_setting;
            const auto narrow = m_narrow_text;
            const auto narrow_type = m_narrow_selection;
            // C++ は共通の UI・ゲーム境界を維持するため、ゲームキューで設定を公開する。
            // Py も INFOVIEW の card_narrowtype を書き込む。
            runtime->exec_func([runtime, setting, narrow, narrow_type](const auto&, const auto&) {
                if (::cw::g_cwpy != runtime || runtime->m_setting != setting) return ::cw::lib::Value{};
                setting->m_card_narrow = narrow;
                setting->m_card_narrowtype = narrow_type;
                return ::cw::lib::Value{};
            });
        }
        update_narrowcondition();
        focus_control(FocusTarget{::cw::dialog::DialogPresentationAction::Auxiliary,12},true);
        return {};
    };
    // 絞り込み通知ではアニメーションの入力受付権を保持しない。 この処理が実
    // 行される前でも、次の IME・テキスト通知を受け付けられるようにする。
    m_ui_callbacks.push_back(std::move(update));
    if (m_ui_backend) m_ui_backend->start_timer(*this);
}
void CardControl::OnSendTo(std::shared_ptr<::cw::lib::Window> arg_event)
{
    focus_control({},false); // Python と同様、直接のコールバックからも先頭パネルへ戻る。
    if (::cw::g_cwpy) ::cw::g_cwpy->play_sound("page", false, 100, 1, 0, 0, false);
    if (::cw::g_cwpy && ::cw::g_cwpy->m_setting && arg_event)
        ::cw::g_cwpy->m_setting->m_last_sendto = arg_event->id;
    if (arg_event) {
        m_sendto_selection = arg_event->id;
#if defined(CARDWIRTH_LOG_ENABLED)
        ::cw::lib::String label;
        if (const auto holder = dynamic_cast<CardHolder*>(this); holder &&
            arg_event->id >= 0 && static_cast<std::size_t>(arg_event->id) < holder->m_sendto_labels.size())
            label = holder->m_sendto_labels[static_cast<std::size_t>(arg_event->id)];
        log_cardcontrol("CardControl sendto select index=" + std::to_string(arg_event->id) +
            " label=" + label + " callname=" + m_callname);
#endif
    }
    if (m_callname == "BACKPACK" || m_callname == "CARDPOCKETB" || m_callname == "STOREHOUSE") {
        if (::cw::g_cwpy && ::cw::g_cwpy->m_setting &&
            ::cw::g_cwpy->m_setting->m_sort_cards == "Aptitude")
            _update_sortattr(true);
        else
            draw_cards(true, -1);
    } else {
        draw_cards(true, -1);
    }
}
void CardControl::update_narrowcondition() { draw_cards(true, -1); }
void CardControl::OnSort(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event; // PyのCardControl基底と同じno-op。並べ替えはCardHolderが担う。
}
void CardControl::OnSortWithStar(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event; // PyのCardControl基底と同じno-op。星優先切替はCardHolderが担う。
}
void CardControl::OnEditStar(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    if (::cw::g_cwpy) ::cw::g_cwpy->play_sound("page", false, 100, 1, 0, 0, false);
    m_edit_star = !m_edit_star;
    if (::cw::g_cwpy && ::cw::g_cwpy->m_setting)
        ::cw::g_cwpy->m_setting->m_edit_star = m_edit_star;
    m_editstar = ::cw::lib::Value(m_edit_star);
    _update_editstar();
}
void CardControl::OnShowSkill(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event; // PyのCardControl基底と同じno-op。具象selectorが表示条件を担う。
}
void CardControl::OnShowItem(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event; // PyのCardControl基底と同じno-op。具象selectorが表示条件を担う。
}
void CardControl::OnShowBeast(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event; // PyのCardControl基底と同じno-op。具象selectorが表示条件を担う。
}
void CardControl::_update_sortwithstar() { m_sortwithstar = ::cw::lib::Value(m_sort_with_star); }
void CardControl::_update_editstar() { m_editstar = ::cw::lib::Value(m_edit_star); }
void CardControl::OnUp(std::shared_ptr<::cw::lib::Window> arg_event) { (void)arg_event; /* 上下対象切替は派生CardHolderが所有する。 */ }
void CardControl::OnDown(std::shared_ptr<::cw::lib::Window> arg_event) { (void)arg_event; /* 上下対象切替は派生CardHolderが所有する。 */ }

void CardControl::OnKeyDown(std::shared_ptr<::cw::lib::Window> arg_event)
{
    if (m__proc || !arg_event) return;
    m__cancel_animation = true;
    const auto headers = get_headers();
    if (headers.empty()) return;
    const auto key = arg_event->id;
    if (key == int_value(m_returnkeyid, std::numeric_limits<::cw::lib::Integer>::min()) ||
        key == int_value(m_infokeyid, std::numeric_limits<::cw::lib::Integer>::min())) {
        for (const auto& value : headers) {
            const auto item = value_to_header(value);
            const bool selected = item && std::visit([](const auto& header) { return header && header->m_negaflag; }, *item);
            if (!selected) continue;
            if (::cw::g_cwpy) ::cw::g_cwpy->play_sound("click", false, 100, 1, 0, 0, false);
            if (key == int_value(m_returnkeyid, std::numeric_limits<::cw::lib::Integer>::min())) {
                animate_click(value, ::cw::lib::Callback{[this, value](const auto&, const auto&) -> ::cw::lib::Value { lclick_event(value); return {}; }});
            } else {
                animate_click(value, ::cw::lib::Callback{[this, value](const auto&, const auto&) -> ::cw::lib::Value { rclick_event(value); return {}; }});
            }
            return;
        }
        return;
    }
    std::vector<::cw::lib::Value> sequence = headers;
    if (key == int_value(m_leftkeyid, std::numeric_limits<::cw::lib::Integer>::min())) std::reverse(sequence.begin(), sequence.end());
    else if (key != int_value(m_rightkeyid, std::numeric_limits<::cw::lib::Integer>::min())) return;
    std::optional<std::size_t> selected_index;
    for (std::size_t i = 0; i < sequence.size(); ++i) {
        const auto item = value_to_header(sequence[i]);
        if (item && std::visit([](const auto& header) { return header && header->m_negaflag; }, *item)) {
            selected_index = i;
            break;
        }
    }
    const auto current = selected_index ? sequence[*selected_index] : ::cw::lib::Value{};
    const auto next = selected_index ? sequence[(*selected_index + 1) % sequence.size()] : sequence.front();
    // Python と同様、現在ページの配置を更新してから、変更前後の
    // キーボード選択を再描画対象にする。 ページ・絞り込み・モードの
    // 変更直後は保持中の矩形が古い可能性があるため、この順序を守る。
    set_cardpos(get_posmode());
    if (selected_index) if (const auto item = value_to_header(current)) std::visit([](const auto& header) { if (header) { header->m_negaflag = false; header->m_clickedflag = false; } }, *item);
    if (const auto item = value_to_header(next)) std::visit([](const auto& header) { if (header) header->m_negaflag = true; }, *item);
    draw_card(current, true);
    draw_card(next, true);
    update_cardpocketinfo_with(next);
    if (m_ui_backend) m_ui_backend->invalidate(*this);
}
// 基底のコントロールはホバー中のカード候補だけを保持する。 その候補で継続表示するポケット情報を更新するかは CardHolder が決める。
void CardControl::update_cardpocketinfo_with(std::optional<::cw::lib::Value> arg_header) { m__cardpocketinfo = arg_header.value_or(::cw::lib::Value{}); }
bool CardControl::side_click_option() const {
    return !::cw::g_cwpy || !::cw::g_cwpy->m_setting ||
        ::cw::g_cwpy->m_setting->m_can_clicksidesofcardcontrol;
}
bool CardControl::_can_sideclick() {
    if(!side_click_option() || !navigation_enabled() || navigation_input_blocked() ||
       !m_side_pointer || m_side_layout_scale!=cardcontrol_ui_scale()) return false;
    const auto [x,y]=*m_side_pointer;
    if(!std::isfinite(x) || !std::isfinite(y) || x<0 || y<0 || x>=cs(kPanelWidth) || y>=cs(kPanelHeight))
        return false;
    // 予約済みの更新で古い公開情報が無効になっていても、現在のカード配置を再確
    // 認する。 絞り込み結果が空であること自体は、操作を拒否する理由にしない。
    for(const auto& header:get_headers()) {
        const auto* object=std::get_if<::cw::lib::ObjectPtr>(&header.storage());
        if(!object || !*object) continue;
        const auto found=m_card_rects.find(object->get());
        if(found==m_card_rects.end()) continue;
        const auto& r=found->second;
        if(x>=cs(r.x) && y>=cs(r.y) && x<cs(r.x+r.width) && y<cs(r.y+r.height)) return false;
    }
    for(const auto& r:m_side_exclusions)
        if(x>=r.x && y>=r.y && x<r.x+r.width && y<r.y+r.height) return false;
    return true;
}
bool CardControl::_cursor_in_ctrlsarea(std::shared_ptr<::cw::lib::Window> mousepos) {
    if(!mousepos) return false;
    // Python の smallctrls はページ操作だけを含む。その余白は物理 10px
    // とし、wins(10) にはしない。 他の子コントロールの矩形は公開時に除外する。
    if(m_show_additional_controls && dynamic_cast<CardHolder*>(this) &&
       m_callname!="CARDPOCKET" && m_callname!="HANDVIEW" && m_callname!="CARDPOCKET_REPLACE") {
        const auto r=compute_page_input_rect(*this);
        const auto x=cs(mousepos->bounds.x),y=cs(mousepos->bounds.y);
        if(x>=r.x-10 && y>=r.y-10 && x<r.x+r.width+10 && y<r.y+r.height+10) return false;
    }
    return true;
}
bool CardControl::_is_cursorinleft() {
    return _can_sideclick() && m_side_pointer->first<cs(kPanelWidth)/4;
}
bool CardControl::_is_cursorinright() {
    return _can_sideclick() && m_side_pointer->first>(cs(kPanelWidth)/4)*3;
}
void CardControl::OnMouseWheel(std::shared_ptr<::cw::lib::Window> arg_event) {
    if (navigation_input_blocked()) return;
    m__cancel_animation = true;
    if (!navigation_enabled()) return;
    if (arg_event && arg_event->id < 0) OnClickLeftBtn(std::move(arg_event));
    else OnClickRightBtn(std::move(arg_event));
}
void CardControl::OnLeftUp(std::shared_ptr<::cw::lib::Window> arg_event)
{
    if (m__proc || m__quit) return;
    m__cancel_animation = true;
    const auto x = arg_event ? arg_event->bounds.x : 0;
    const auto y = arg_event ? arg_event->bounds.y : 0;
    // 交換用の矢印はカードの当たり判定矩形の外に
    // あるため、通常のカード判定より先に調べる。
    if (_can_repls()) {
        std::vector<::cw::lib::Value> candidates;
        const auto before = get_beforepageheaders();
        if (!before.empty()) candidates.push_back(before.back());
        const auto current = get_headers();
        candidates.insert(candidates.end(), current.begin(), current.end());
        for (const auto& value : candidates) {
            const auto header = object_value<CardPtr::element_type>(value);
            if (!header) continue;
            const auto [rect, unused_x, unused_y] = _get_replsrect(value);
            (void)unused_x; (void)unused_y;
            if (rect.width <= 0 || x < rect.x || y < rect.y || x >= rect.x + rect.width || y >= rect.y + rect.height)
                continue;
            const auto it = std::find_if(m_list.begin(), m_list.end(), [&header](const auto& item) {
                return object_value<CardPtr::element_type>(item) == header;
            });
            if (it == m_list.end() || std::next(it) == m_list.end()) return;
            const auto next = *std::next(it);
            if (::cw::g_cwpy) ::cw::g_cwpy->play_sound("click", false, 100, 1, 0, 0, false);
            animate_replsclick(value, [this, value, next](const auto&, const auto&) -> ::cw::lib::Value {
                _replace_position(value, next);
                return {};
            });
            return;
        }
    }
    if (arg_event && arg_event->id >= 0 && static_cast<std::size_t>(arg_event->id) < m_draw_headers.size()) {
        const auto value = m_draw_headers[static_cast<std::size_t>(arg_event->id)];
        if (::cw::g_cwpy) ::cw::g_cwpy->play_sound("click", false, 100, 1, 0, 0, false);
        animate_click(value, [this, value](const auto&, const auto&) -> ::cw::lib::Value { lclick_event(value); return {}; });
        return;
    }
    if (arg_event && m_ui_backend) {
        if (const auto value = m_ui_backend->hit_test(*this, {arg_event->bounds.x, arg_event->bounds.y})) {
            if (const auto header = object_value<CardPtr::element_type>(*value)) {
                const auto [rect, unused_x, unused_y] = _get_starrect(header);
                (void)unused_x; (void)unused_y;
                const auto sx = cs(arg_event->bounds.x);
                const auto sy = cs(arg_event->bounds.y);
                if (m_edit_star && rect.width > 0 && sx >= rect.x && sy >= rect.y &&
                    sx < rect.x + rect.width && sy < rect.y + rect.height) {
                    if (::cw::g_cwpy) ::cw::g_cwpy->play_sound("page", false, 100, 1, 0, 0, false);
                    animate_starclick(*value, [this, header](const auto&, const auto&) -> ::cw::lib::Value {
                        header->set_star(header->m_star ? 0 : 1);
                        if (::cw::g_cwpy && ::cw::g_cwpy->m_setting &&
                            ::cw::g_cwpy->m_setting->m_sort_cardswithstar &&
                            (m_callname == "STOREHOUSE" || m_callname == "BACKPACK" || m_callname == "CARDPOCKETB"))
                            _update_sortattr(true);
                        return {};
                    });
                    return;
                }
            }
            if (::cw::g_cwpy) ::cw::g_cwpy->play_sound("click", false, 100, 1, 0, 0, false);
            animate_click(*value, [this, value = *value](const auto&, const auto&) -> ::cw::lib::Value { lclick_event(value); return {}; });
            return;
        }
    }
    if(arg_event) {
        m_side_pointer=std::pair<float,float>{static_cast<float>(cs(x)),static_cast<float>(cs(y))};
        if(_is_cursorinleft()) OnClickLeftBtn(arg_event);
        else if(_is_cursorinright()) OnClickRightBtn(arg_event);
    }
}
bool CardControl::_update_sortattr(bool arg_draw) { (void)arg_draw; return false; }
void CardControl::_replace_position(::cw::lib::Value arg_header1, ::cw::lib::Value arg_header2)
{
    if (m__proc) return;
    const auto before = get_beforepageheaders();
    if (!before.empty() && same_value(before.back(), arg_header1)) std::swap(arg_header1, arg_header2);
    const auto first = object_value<::cw::header::CardHeader>(arg_header1);
    const auto second = object_value<::cw::header::CardHeader>(arg_header2);
    if (!first || !second) throw std::invalid_argument("Reorder requires card headers");
    const auto find_header = [this](const ::cw::lib::Value& value) {
        return std::find_if(m_list.begin(), m_list.end(), [&value](const auto& item) { return same_value(item, value); });
    };
    const auto i = find_header(arg_header1), j = find_header(arg_header2);
    if (i == m_list.end() || j == m_list.end()) throw std::invalid_argument("Reorder card is not in the displayed list");
    const auto index1 = static_cast<std::size_t>(std::distance(m_list.begin(), i));
    const auto index2 = static_cast<std::size_t>(std::distance(m_list.begin(), j));
    const auto visible = get_headers();
    const bool second_shown = std::any_of(visible.begin(), visible.end(), [&arg_header2](const auto& value) { return same_value(value, arg_header2); });
    animate_hide(arg_header1, second_shown ? std::optional{arg_header2} : std::nullopt,
        [this, arg_header1, arg_header2, first, second, index1, index2, second_shown](const auto&, const auto&) -> ::cw::lib::Value {
            if (!::cw::g_cwpy || !::cw::g_cwpy->m_ydata || !*::cw::g_cwpy->m_ydata)
                throw std::runtime_error("Reorder requires inn data");
            (*::cw::g_cwpy->m_ydata)->changed();
            // Py と同様、この共通の呼び出し側で表示と順序を交換してから
            // 、 所有先固有のコンテナー更新と配布アニメーションを行う。
            std::swap(first->m_wxrect, second->m_wxrect);
            std::swap(first->m_negaflag, second->m_negaflag);
            std::swap(first->m_order, second->m_order);
            // m_card_rects は、交換した Py の wxrect を共通ホスト向けに反映したものである。
            const auto r1 = m_card_rects.find(first.get()), r2 = m_card_rects.find(second.get());
            if (r1 != m_card_rects.end() && r2 != m_card_rects.end()) std::swap(r1->second, r2->second);
            m_list.at(index1) = arg_header2;
            m_list.at(index2) = arg_header1;
            _replace_position_impl(arg_header1, arg_header2);
            animate_deal(arg_header2, second_shown ? std::optional{arg_header1} : std::nullopt,
                [](const auto&, const auto&) -> ::cw::lib::Value { return {}; });
            first->m_deal_per = 100;
            second->m_deal_per = 100;
            set_cardpos(get_posmode());
            if (m_side_pointer) {
                auto pointer = std::make_shared<::cw::lib::Window>();
                pointer->bounds.x = static_cast<::cw::lib::Integer>(m_side_pointer->first / cs(1));
                pointer->bounds.y = static_cast<::cw::lib::Integer>(m_side_pointer->second / cs(1));
                _on_move(pointer);
            }
            m_ui_invalidated = true;
            request_dialog_presentation_refresh();
            return {};
        });
}
void CardControl::_replace_position_impl(::cw::lib::Value arg_header1, ::cw::lib::Value arg_header2)
{
    (void)arg_header1;
    (void)arg_header2;
}
void CardControl::OnRightUp(std::shared_ptr<::cw::lib::Window> arg_event)
{
    if (m__proc || m__quit) return;
    m__cancel_animation = true;
    if (::cw::g_cwpy) ::cw::g_cwpy->play_sound("click", false, 100, 1, 0, 0, false);
    if (arg_event && arg_event->id >= 0 && static_cast<std::size_t>(arg_event->id) < m_draw_headers.size()) {
        const auto value = m_draw_headers[static_cast<std::size_t>(arg_event->id)];
        animate_click(value, [this, value](const auto&, const auto&) -> ::cw::lib::Value { rclick_event(value); return {}; });
        return;
    }
    if (arg_event && m_ui_backend) {
        if (const auto value = m_ui_backend->hit_test(*this, {arg_event->bounds.x, arg_event->bounds.y})) {
            animate_click(*value, [this, value = *value](const auto&, const auto&) -> ::cw::lib::Value { rclick_event(value); return {}; });
            return;
        }
    }
    // Python と同様、カード外の右クリックは閉じる・キャンセルの操作として扱う。
    OnCancel(std::move(arg_event));
}
void CardControl::OnRightUp2(std::shared_ptr<::cw::lib::Window> arg_event) { m__cancel_animation = true; if (::cw::g_cwpy) ::cw::g_cwpy->play_sound("click", false, 100, 1, 0, 0, false); OnCancel(std::move(arg_event)); }
void CardControl::OnMove(std::shared_ptr<::cw::lib::Window> arg_event)
{
    if (m__proc || m__quit || !arg_event) return;
    m_side_pointer=std::pair<float,float>{static_cast<float>(cs(arg_event->bounds.x)),
        static_cast<float>(cs(arg_event->bounds.y))};
    set_cardpos(get_posmode());
    const auto mouse = ::cw::lib::Point{arg_event->bounds.x, arg_event->bounds.y};
    std::optional<::cw::lib::Value> hovered;
    if (m_ui_backend) hovered = m_ui_backend->hit_test(*this, mouse);

    std::optional<::cw::lib::Value> lastrepls;
    std::optional<::cw::lib::Value> laststar;
    if (_can_repls()) {
        const auto before = get_beforepageheaders();
        if (!before.empty()) {
            const auto value = before.back();
            const auto [rect, unused_x, unused_y] = _get_replsrect(value);
            (void)unused_x; (void)unused_y;
            if (rect.width > 0 && mouse.x >= rect.x && mouse.y >= rect.y &&
                mouse.x < rect.x + rect.width && mouse.y < rect.y + rect.height) lastrepls = value;
        }
    }
    const auto visible = get_headers();
    const auto sx = cs(mouse.x);
    const auto sy = cs(mouse.y);
    bool card_changed = false;
    for (const auto& value : visible) {
        const auto item = value_to_header(value);
        if (!item) continue;
        std::visit([&](const auto& header) {
            if (!header) return;
            if constexpr (std::is_same_v<std::decay_t<decltype(header)>, CardPtr>) {
                if (_can_repls()) {
                    const auto [rect, unused_x, unused_y] = _get_replsrect(value);
                    (void)unused_x; (void)unused_y;
                    if (rect.width > 0 && mouse.x >= rect.x && mouse.y >= rect.y &&
                        mouse.x < rect.x + rect.width && mouse.y < rect.y + rect.height) lastrepls = value;
                }
                const auto [star_rect, unused_star_x, unused_star_y] = _get_starrect(header);
                (void)unused_star_x; (void)unused_star_y;
                if (!lastrepls && m_edit_star && star_rect.width > 0 && sx >= star_rect.x && sy >= star_rect.y &&
                    sx < star_rect.x + star_rect.width && sy < star_rect.y + star_rect.height) laststar = value;
            }
            const auto found = m_card_rects.find(header.get());
            if (found == m_card_rects.end()) return;
            const auto& rect = found->second;
            const bool inside = mouse.x >= rect.x - 2 && mouse.y >= rect.y - 2 &&
                                mouse.x < rect.x + rect.width + 2 && mouse.y < rect.y + rect.height + 2;
            const bool select = !lastrepls && !laststar && inside && hovered && same_value(value, *hovered);
            const bool old = header->m_negaflag;
            header->m_negaflag = select;
            if (old != select) { draw_card(value, true); card_changed = true; }
        }, *item);
    }
    if (auto holder = dynamic_cast<CardHolder*>(this)) {
        holder->update_cardpocketinfo_with(hovered);
    }
    const bool star_changed = !same_optional_value(m__laststar, laststar);
    const bool replacement_changed = !same_optional_value(m__lastrepls, lastrepls);
    if (star_changed) {
        if (m__laststar) invalidate_cardcontrol_star(*this, *m__laststar);
        if (laststar) invalidate_cardcontrol_star(*this, *laststar);
    }
    if (replacement_changed) {
        if (m__lastrepls) invalidate_cardcontrol_replacement(*this, *m__lastrepls);
        if (lastrepls) invalidate_cardcontrol_replacement(*this, *lastrepls);
    }
    m__laststar = std::move(laststar);
    m__lastrepls = std::move(lastrepls);
    if (star_changed || replacement_changed || card_changed || m_ui_invalidated) {
        request_dialog_presentation_refresh();
    }
}
void CardControl::_on_move(std::shared_ptr<::cw::lib::Window> arg_mousepos) { OnMove(std::move(arg_mousepos)); }
void CardControl::OnEnter(std::shared_ptr<::cw::lib::Window> arg_event) { OnMove(std::move(arg_event)); }
void CardControl::OnLeave(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    // ホストはポインターが外れるとカーソルを解除する。OnMove の処理中判定
    // とは独立して、 アニメーション中や終了中もモデルの側面領域をリセットする。
    m_side_pointer.reset();
    if (!m_window_active) return;
    set_cardpos(get_posmode());
    for (const auto& value:get_headers()) if (const auto item=value_to_header(value))
        std::visit([&](const auto& header) {
            if (header && header->m_negaflag) { header->m_negaflag=false; draw_card(value,false); }
        },*item);
}
void CardControl::OnClickLeftBtn2(std::shared_ptr<::cw::lib::Window> arg_event)
{
    const auto holder = dynamic_cast<CardHolder*>(this);
    m__cancel_animation = true;
    const auto count = holder ? static_cast<::cw::lib::Integer>(holder->m_sendto_labels.size()) : 0;
    auto index = m_sendto_selection.value_or(
        ::cw::g_cwpy && ::cw::g_cwpy->m_setting ? ::cw::g_cwpy->m_setting->m_last_sendto : 0);
    index = index <= 0 ? count - 1 : index - 1;
    auto event = arg_event ? arg_event : std::make_shared<::cw::lib::Window>();
    m_sendto_selection = index >= 0 && index < count ? index : -1;
    event->id = *m_sendto_selection;
    OnSendTo(event);
}
void CardControl::OnClickRightBtn2(std::shared_ptr<::cw::lib::Window> arg_event)
{
    const auto holder = dynamic_cast<CardHolder*>(this);
    m__cancel_animation = true;
    const auto count = holder ? static_cast<::cw::lib::Integer>(holder->m_sendto_labels.size()) : 0;
    auto index = m_sendto_selection.value_or(
        ::cw::g_cwpy && ::cw::g_cwpy->m_setting ? ::cw::g_cwpy->m_setting->m_last_sendto : 0);
    index = index + 1 >= count ? 0 : index + 1;
    auto event = arg_event ? arg_event : std::make_shared<::cw::lib::Window>();
    m_sendto_selection = index >= 0 && index < count ? index : -1;
    event->id = *m_sendto_selection;
    OnSendTo(event);
}
void CardControl::OnPaint2(std::shared_ptr<::cw::lib::Window> arg_event) { if (m_ui_backend) m_ui_backend->paint(*this, std::move(arg_event)); }
void CardControl::_draw_additionals(std::shared_ptr<::cw::lib::DeviceContext> arg_dc) { (void)arg_dc; }

bool CardControl::_show_star(std::shared_ptr<::cw::header::CardHeader> arg_header)
{
    if (!arg_header || (m_callname != "STOREHOUSE" && m_callname != "BACKPACK" && m_callname != "CARDPOCKETB" && m_callname != "CARDPOCKET")) return false;
    if (m_callname == "CARDPOCKET") {
        const auto owner = arg_header->get_owner();
        const bool player_owner = owner && object_value<::cw::character::Player>(*owner) != nullptr;
        const auto holder = dynamic_cast<CardHolder*>(this);
        const bool personal_owner = holder && holder->is_showpersonal() && arg_header->m_personal_owner.lock();
        if (!player_owner && !personal_owner) return false;
    }
    return m_draw_bitmaps.contains(arg_header.get());
}
// 描画とポインターの当たり判定は、同じビットマップ生成処理を使う。
std::tuple<::cw::lib::Rect, ::cw::lib::Integer, ::cw::lib::Integer> CardControl::_get_starrect(
    std::shared_ptr<::cw::header::CardHeader> arg_header, ::cw::lib::Real arg_draw_x,
    ::cw::lib::Real arg_draw_y, ::cw::lib::Integer arg_bmp_w, ::cw::lib::Integer arg_bmp_h)
{
    if (!_show_star(arg_header)) return {::cw::lib::Rect{}, 0, 0};
    auto star = resource_dialog_surface("BOOKMARK");
    if (!star) star = resource_dialog_surface("BOOKMARK_EMPTY");
    if (!star) return {::cw::lib::Rect{}, 0, 0};

    const auto deal_per = arg_header->m_deal_per;
    // Py と同様、配布中の幅と余白の小数部分を最後の int(x) まで保持する。
    // どちらかを先に切り捨てると、半分まで配布されたスターの位置が 1px ずれる。
    const auto sw = static_cast<::cw::lib::Real>(star->size.width) * deal_per / 100.0;
    const auto sh = star->size.height;
    auto x = arg_draw_x + arg_bmp_w - sw - cs(5) * deal_per / 100.0;
    auto y = arg_draw_y + arg_bmp_h - sh - cs(5);

    if (m_callname == "CARDPOCKET") {
        const auto holder = dynamic_cast<CardHolder*>(this);
        const bool personal_skill = holder && holder->is_showpersonal() && !arg_header->m_personal_owner.expired();
        if (arg_header->m_type == "SkillCard" && !personal_skill) y -= cs(32);
        else y -= cs(16);
    }
    if (::cw::g_cwpy && ::cw::g_cwpy->m_setting && ::cw::g_cwpy->m_setting->m_show_cardkind) {
        const auto holder = dynamic_cast<CardHolder*>(this);
        if (m_callname == "STOREHOUSE" || m_callname == "BACKPACK" ||
            (holder && holder->is_showpersonal() && !arg_header->m_personal_owner.expired()))
            y -= cs(16);
    }
    if (m_callname == "CARDPOCKETB") y -= cs(16);
    if (m_callname == "STOREHOUSE" || m_callname == "BACKPACK") {
        const auto holder = dynamic_cast<CardHolder*>(this);
        if (holder && m_sendto && m_sendto_selection) {
            const auto destination = *m_sendto_selection;
            if (value_map_index(holder->m__combo_cast, destination) || value_map_index(holder->m__combo_personal, destination))
                y -= cs(16);
        }
    }

    const auto ix = static_cast<::cw::lib::Integer>(x);
    const auto iy = static_cast<::cw::lib::Integer>(y);
    return {::cw::lib::Rect{ix - cs(5), iy - cs(5),
        static_cast<::cw::lib::Integer>(sw + cs(10)), sh + cs(10)}, ix, iy};
}
std::tuple<::cw::lib::Rect, ::cw::lib::Integer, ::cw::lib::Integer> CardControl::_get_starrect(std::shared_ptr<::cw::header::CardHeader> arg_header)
{
    const auto rect_it = m_card_rects.find(arg_header.get());
    if (rect_it == m_card_rects.end()) return {::cw::lib::Rect{}, 0, 0};
    const auto& rect = rect_it->second;
    const auto slot_x = cs(rect.x);
    const auto slot_y = cs(rect.y);
    const auto slot_w = cs(rect.width);
    const auto slot_h = cs(rect.height);
    const auto drawn = m_draw_bitmaps.find(arg_header.get());
    const auto bitmap = drawn == m_draw_bitmaps.end() ? nullptr : drawn->second;
    if (!bitmap || bitmap->size.width <= 0 || bitmap->size.height <= 0)
        return {::cw::lib::Rect{}, 0, 0};
    const auto bmp_w = bitmap->size.width;
    const auto bmp_h = bitmap->size.height;
    const auto draw_x = slot_x + (slot_w - bmp_w) / 2.0;
    const auto draw_y = slot_y + (slot_h - bmp_h) / 2.0;
    return _get_starrect(std::move(arg_header), draw_x, draw_y, bmp_w, bmp_h);
}
bool CardControl::_can_repls() { return m_callname != "HANDVIEW" && m_callname != "INFOVIEW" && m_sort_selection == 0 && m_edit_star; }
std::tuple<::cw::lib::Rect, ::cw::lib::Integer, ::cw::lib::Integer> CardControl::_get_replsrect(::cw::lib::Value arg_header)
{
    const auto header = object_value<CardPtr::element_type>(arg_header);
    if (!_can_repls() || !header) return {::cw::lib::Rect{}, 0, 0};
    const auto all = dynamic_cast<CardHolder*>(this);
    if (all) {
        const auto typed = all->m_typed_list;
        const auto it_all = std::find_if(typed.begin(), typed.end(), [&header](const auto& item) {
            const auto card = std::get_if<CardPtr>(&item);
            return card && *card == header;
        });
        if (it_all == typed.end() || std::next(it_all) == typed.end()) return {::cw::lib::Rect{}, 0, 0};
        const auto next_card = std::get_if<CardPtr>(&*std::next(it_all));
        if (!next_card || !*next_card) return {::cw::lib::Rect{}, 0, 0};
        if (m_sort_with_star && m_callname != "CARDPOCKET" && (*next_card)->m_star != header->m_star) return {::cw::lib::Rect{}, 0, 0};
    }
    auto it = m_card_rects.find(header.get());
    bool before_page = false;
    if (it == m_card_rects.end()) {
        const auto before = get_beforepageheaders();
        const auto current = get_headers();
        if (before.empty() || current.empty() || !same_value(before.back(), arg_header)) return {{}, 0, 0};
        const auto first = object_value<CardPtr::element_type>(current.front());
        if (!first) return {{}, 0, 0};
        it = m_card_rects.find(first.get());
        if (it == m_card_rects.end()) return {{}, 0, 0};
        before_page = true;
    }
    const auto arrow = ::cw::lib::ui::resource_surface_for_logical(resource_dialog_surface("REPLACE_POSITION"));
    const auto width = arrow ? arrow->size.width : 16;
    const auto height = arrow ? arrow->size.height : 16;
    const auto& r = it->second;
    const auto gap = get_spacer_x(get_posmode());
    const auto x = static_cast<::cw::lib::Integer>((before_page ? r.x - gap / 2.0 : r.x + r.width + gap / 2.0) - width / 2.0);
    return {::cw::lib::Rect{x, r.y + 20, width, height}, x, r.y + 20};
}
void CardControl::draw(bool arg_update) { if (arg_update) draw_cards(true, -1); m__redraw = true; request_dialog_presentation_refresh(); }
::cw::lib::Integer CardControl::get_mode()
{
    if (m_callname == "INFOVIEW") return g_CCMODE_SHOW;
    if (m_callname == "CARDPOCKET_REPLACE") return g_CCMODE_REPLACE;
    if (m_callname == "CARDPOCKET" && object_value<::cw::character::Friend>(m_selection)) return g_CCMODE_SHOW;
    if (m_callname == "HANDVIEW" && !m_cwpy_debug && (object_value<::cw::character::Enemy>(m_selection) || object_value<::cw::character::Friend>(m_selection))) return g_CCMODE_SHOW;
    if (trade_area(m_areaid)) return g_CCMODE_MOVE;
    if (m_callname == "HANDVIEW") return g_CCMODE_BATTLE;
    return g_CCMODE_USE;
}
void CardControl::draw_cards(bool arg_update, ::cw::lib::Integer arg_mode)
{
    (void)arg_update;
    m_draw_headers = get_headers();
    m__drawlist.clear();
    m_draw_bitmaps.clear();
    set_cardpos(arg_mode < 0 ? get_posmode() : arg_mode);
    _sync_dialog_animated_paths();
    for (const auto& value : m_draw_headers) draw_card(value, false);
    if (m_ui_backend) m_ui_backend->invalidate(*this);
}
std::optional<std::shared_ptr<::cw::sprite::card::PlayerCard>> CardControl::_get_test_aptitude()
{
    if (auto holder = dynamic_cast<CardHolder*>(this)) {
        if (holder->m_callname == "CARDPOCKETB") if (auto card = object_value<::cw::sprite::card::PlayerCard>(holder->m_selection)) return card;
        if (holder->m_sendto_selection) {
            const auto destination = *holder->m_sendto_selection;
            if (const auto index = value_map_index(holder->m__combo_cast, destination); index && *index >= 0 && static_cast<std::size_t>(*index) < holder->m_list2.size())
                if (auto card = object_value<::cw::sprite::card::PlayerCard>(holder->m_list2[static_cast<std::size_t>(*index)])) return card;
            if (const auto index = value_map_index(holder->m__combo_personal, destination); index && *index >= 0 && static_cast<std::size_t>(*index) < holder->m_list2.size())
                if (auto card = object_value<::cw::sprite::card::PlayerCard>(holder->m_list2[static_cast<std::size_t>(*index)])) return card;
        }
    }
    return std::nullopt;
}
void CardControl::draw_card(::cw::lib::Value arg_header, bool arg_fromkeyevent)
{
    (void)arg_fromkeyevent;
    if (const auto item = value_to_header(arg_header)) std::visit([this](const auto& header) {
        if (!header) return;
        m_draw_bitmaps[header.get()] = rendered_card_bitmap(*this, header);
        m__drawlist[header->m_name] = ::cw::lib::Value(true);
        if (header->m_negaflag) m__drawlist[header->m_name + ":nega"] = ::cw::lib::Value(true);
        if (header->m_clickedflag) m__drawlist[header->m_name + ":clicked"] = ::cw::lib::Value(true);
        m__drawlist[header->m_name + ":deal"] = ::cw::lib::Value(header->m_deal_per);
        m_ui_dirty_cards.insert(header.get());
        const auto key = cardcontrol_component_key(header.get());
        if (!dialog_retained_surface().invalidate_component(key)) {
            const auto found = m_card_rects.find(header.get());
            if (found != m_card_rects.end()) {
                const auto& logical = found->second;
                dialog_retained_surface().invalidate(
                    {cs(logical.x), cs(logical.y), cs(logical.width), cs(logical.height)});
            }
        }
        m_ui_invalidated = true;
        request_dialog_presentation_refresh();
    }, *item);
}
::cw::lib::Integer CardControl::get_posmode() { if (m_callname == "INFOVIEW" || m_callname == "BACKPACK" || m_callname == "STOREHOUSE" || m_callname == "CARDPOCKETB") return 1; if (m_callname == "CARDPOCKET") return 2; return 3; }
void CardControl::set_cardpos(::cw::lib::Integer arg_mode)
{
    const auto headers = get_headers();
    const auto positions = get_poslist(static_cast<::cw::lib::Integer>(headers.size()), arg_mode);
    m_card_rects.clear();
    for (std::size_t i = 0; i < headers.size() && i < positions.size(); ++i) if (const auto item = value_to_header(headers[i])) std::visit([this, &positions, i](const auto& header) { if (header) m_card_rects[header.get()] = {std::get<0>(positions[i]), std::get<1>(positions[i]), kCardWidth, kCardHeight}; }, *item);
}
bool CardControl::is_showpersonal() { return false; }
// Py の基底処理は [] を返すが、ネイティブ版は再利用可能な型付きの十枚
// 単位のページを提供する。 具体的な選択画面が所持カードを管理し、別のウィジ
// ェットを実装せずこのページモデルを使う。未対応の Value は除外する。
// 基底処理から直接結果を返す点は、Py の抽象フックに対する拡張である。
std::vector<::cw::lib::Value> CardControl::get_headers() { return page_values(typed_headers(m_list), m_index, 10); }
// Py の基底処理は [] を返すが、ネイティブ版では型付きのページ処
// 理を再利用して前の十枚を返す。 負のページは空とする。具体的な選択画
// 面が所持カードを供給し、この基底フック自体も結果を返せるようにする。
std::vector<::cw::lib::Value> CardControl::get_beforepageheaders() { return page_values(typed_headers(m_list), m_index - 1, 10); }
void CardControl::_after_animation_paint(::cw::lib::Callback arg_func)
{
    // Py と同様、押下解除後のカードを描いてから、次の _after_event
    // で func3 を実行する。 解除コールバック内で直接実行すると
    // 、共通の処理ループが解除後の描画面を描く前に子画面が開いてしまう。
    const auto dispatch = [this, arg_func = std::move(arg_func)](const auto&, const auto&) mutable -> ::cw::lib::Value {
        m__proc = false;
        if (!m__cancel_animation && !m__quit) invoke(arg_func);
        return {};
    };
    try {
        if (m_ui_backend) m_ui_backend->post(*this, dispatch);
        else invoke(dispatch);
    } catch (...) {
        m__proc = false;
        throw;
    }
}
void CardControl::animate_click(::cw::lib::Value arg_header, ::cw::lib::Callback arg_func)
{
    if (m__proc) return;
    m__proc = true; m__cancel_animation = false;
    // Py は func2 より前にダイアログが閉じても押下フラグを解除す
    // る。 ネイティブ版は終了時に予約済みコールバックを破棄するため、その
    // 実行に頼らず、ヘッダーだけを操作する後処理をコールバックに保持する。
    struct ReleasePressedCard {
        ::cw::lib::Value value;
        bool active{true};
        ~ReleasePressedCard() {
            if (!active) return;
            if (const auto item = value_to_header(value))
                std::visit([](const auto& header) {
                    if (header) header->m_clickedflag = false;
                }, *item);
        }
    };
    const auto release = std::make_shared<ReleasePressedCard>();
    release->value = arg_header;
    set_cardpos(get_posmode());
    if (const auto item = value_to_header(arg_header)) std::visit([](const auto& header) { if (header) header->m_clickedflag = true; }, *item);
    draw_card(arg_header, true);
    const auto complete = [this, release, arg_header = std::move(arg_header), arg_func = std::move(arg_func)](const auto&, const auto&) mutable -> ::cw::lib::Value {
        if (const auto item = value_to_header(arg_header)) std::visit([](const auto& header) { if (header) header->m_clickedflag = false; }, *item);
        // 呼び出し前に後処理を解除する。同期的なバックエンドでは arg_func から次のク
        // リックが始まり、 その押下状態は新しいクリックに属するため、古い処理で解除しない。
        release->active = false;
        if (m__cancel_animation || m__quit) {
            m__proc = false;
            return {};
        }
        draw_card(arg_header, true);
        if (const auto item = value_to_header(arg_header)) std::visit([](const auto& header) { if (header) header->m_negaflag = false; }, *item);
        _after_animation_paint(std::move(arg_func));
        return {};
    };
    const auto frame = [this](const auto&, const auto&) -> ::cw::lib::Value {
        ++m__animate_frame;
        return {};
    };
    try {
        if (m_ui_backend) {
            for (int i = 0; i < 3; ++i) m_ui_backend->post(*this, frame);
            m_ui_backend->post(*this, complete);
        } else invoke(complete);
    } catch (...) { m__proc = false; throw; }
}
void CardControl::animate_starclick(::cw::lib::Value arg_header, ::cw::lib::Callback arg_func)
{
    if (m__proc) return;
    m__proc = true; m__cancel_animation = false; m__starclickedflag = true;
    draw_card(arg_header, true);
    const auto complete = [this, arg_header = std::move(arg_header), arg_func = std::move(arg_func)](const auto&, const auto&) mutable -> ::cw::lib::Value {
        m__starclickedflag = false;
        if (m__cancel_animation || m__quit) {
            m__proc = false;
            return {};
        }
        draw_card(arg_header, true);
        if (const auto item = value_to_header(arg_header)) std::visit([](const auto& header) { if (header) header->m_negaflag = false; }, *item);
        // 通常のクリックと同様、表示の後処理を終えてからモーダルの処理を呼ぶ。
        _after_animation_paint(std::move(arg_func));
        return {};
    };
    const auto frame = [this](const auto&, const auto&) -> ::cw::lib::Value {
        ++m__animate_frame;
        return {};
    };
    try {
        if (m_ui_backend) {
            for (int i = 0; i < 3; ++i) m_ui_backend->post(*this, frame);
            m_ui_backend->post(*this, complete);
        } else invoke(complete);
    } catch (...) { m__proc = false; m__starclickedflag = false; throw; }
}
void CardControl::animate_replsclick(::cw::lib::Value arg_header, ::cw::lib::Callback arg_func)
{
    if (m__proc) return;
    m__proc = true; m__cancel_animation = false; m__replclickedflag = true;
    invalidate_cardcontrol_replacement(*this, arg_header);
    const auto complete = [this, arg_header = std::move(arg_header), arg_func = std::move(arg_func)](const auto&, const auto&) mutable -> ::cw::lib::Value {
        m__replclickedflag = false;
        if (m__cancel_animation || m__quit) {
            m__proc = false;
            return {};
        }
        invalidate_cardcontrol_replacement(*this, arg_header);
        // 交換操作はこのダイアログを閉じる可能性があるため、最後に実行する。
        _after_animation_paint(std::move(arg_func));
        return {};
    };
    const auto frame = [this](const auto&, const auto&) -> ::cw::lib::Value {
        ++m__animate_frame;
        return {};
    };
    try {
        if (m_ui_backend) {
            for (int i = 0; i < 3; ++i) m_ui_backend->post(*this, frame);
            m_ui_backend->post(*this, complete);
        } else invoke(complete);
    } catch (...) { m__proc = false; m__replclickedflag = false; throw; }
}
void CardControl::_animate_dealhide(::cw::lib::Value arg_header1, std::optional<::cw::lib::Value> arg_header2, ::cw::lib::Callback arg_func, std::vector<::cw::lib::Integer> arg_dealing_scales, ::cw::lib::Integer arg_lastscale)
{
    if (m__proc) return;
    m__proc = true;
    m__cancel_animation = false;
    m__animate_frame = 0;
    // キャンセル状態はダイアログのフラグだけでなく、このアニメーション内にも保持する。
    // Py と同じキャンセル終了処理で両カードを 100% に戻して _proc
    // を解放した後、 予約済みの古い処理が次のアニメーションに影響しないようにする。
    struct RestoreCancelledScale {
        ::cw::lib::Value first;
        std::optional<::cw::lib::Value> second;
        bool cancelled{};
        bool finished{};
        ~RestoreCancelledScale() {
            if (finished) return;
            // 予約済みの処理は stop() 分岐を実行せず破棄さ
            // れる場合がある。 その時点まで元の両ヘッダーを保持し
            // 、ダイアログや消去済みかもしれない一覧は参照しない。
            for (const auto* value : {&first, second ? &*second : nullptr})
                if (value) if (const auto item = value_to_header(*value))
                    std::visit([](const auto& header) {
                        if (header) header->m_deal_per = 100;
                    }, *item);
        }
    };
    const auto cancelled = std::make_shared<RestoreCancelledScale>();
    cancelled->first = arg_header1;
    cancelled->second = arg_header2;
    const auto set_scale = [this, arg_header1, arg_header2](::cw::lib::Integer scale, bool draw) {
        for (const auto* value : {&arg_header1, arg_header2 ? &*arg_header2 : nullptr}) if (value)
            if (const auto item = value_to_header(*value)) {
                std::visit([scale](const auto& header) { if (header) header->m_deal_per = scale; }, *item);
                if (draw) draw_card(*value, true);
            }
    };
    const auto stop = [this, cancelled, set_scale]() {
        if (cancelled->cancelled) return true;
        if (m__cancel_animation || m__quit) {
            cancelled->cancelled = true;
            set_scale(100, false);
            // _proc を解放する前に後処理を完了する。 古い予約処理は、新
            // しいアニメーションがカードを取得した後も残っている場合がある。
            cancelled->finished = true;
            m__proc = false;
            return true;
        }
        return false;
    };
    const auto complete = [this, stop, cancelled, arg_func = std::move(arg_func)](const auto&, const auto&) mutable -> ::cw::lib::Value {
        if (stop()) return {};
        m__proc = false;
        m__animate_frame = 0;
        // arg_func がこれらのヘッダーで新しいアニメーションを開始する前に、古い後処理を解除する。
        cancelled->finished = true;
        invoke(arg_func);
        return {};
    };
    const auto terminal = [this, stop, set_scale, arg_lastscale, complete](const auto&, const auto&) mutable -> ::cw::lib::Value {
        if (stop()) return {};
        set_scale(arg_lastscale, true);
        // Py と同様、最終拡大率で描画した後、次の _after_event
        // から操作を呼ぶ前に、もう一度キャンセルを確認する。
        if (m_ui_backend) m_ui_backend->post(*this, complete);
        else invoke(complete);
        return {};
    };
    try {
        if (!arg_dealing_scales.empty()) set_scale(arg_dealing_scales.front(), true);
        for (std::size_t i = 1; i < arg_dealing_scales.size(); ++i) {
            const auto step = [this, stop, set_scale, scale = arg_dealing_scales[i], i](const auto&, const auto&) -> ::cw::lib::Value {
                if (stop()) return {};
                m__animate_frame = static_cast<::cw::lib::Integer>(i);
                set_scale(scale, true);
                return {};
            };
            if (m_ui_backend) m_ui_backend->post(*this, step);
            else invoke(step);
        }
        if (m_ui_backend && !arg_dealing_scales.empty()) m_ui_backend->post(*this, terminal);
        else invoke(terminal);
    } catch (...) {
        cancelled->cancelled = true;
        set_scale(100, false);
        cancelled->finished = true;
        m__proc = false;
        throw;
    }
}
void CardControl::animate_hide(::cw::lib::Value arg_header1, std::optional<::cw::lib::Value> arg_header2, ::cw::lib::Callback arg_func)
{
    std::vector<::cw::lib::Integer> scales;
    if (::cw::g_cwpy && ::cw::g_cwpy->m_setting) scales = sequence_ints(::cw::g_cwpy->m_setting->m_dealing_scales);
    _animate_dealhide(std::move(arg_header1), std::move(arg_header2), std::move(arg_func), std::move(scales), 0);
}
void CardControl::animate_deal(::cw::lib::Value arg_header1, std::optional<::cw::lib::Value> arg_header2, ::cw::lib::Callback arg_func)
{
    std::vector<::cw::lib::Integer> scales;
    if (::cw::g_cwpy && ::cw::g_cwpy->m_setting) scales = sequence_ints(::cw::g_cwpy->m_setting->m_dealing_scales);
    std::reverse(scales.begin(), scales.end());
    _animate_dealhide(std::move(arg_header1), std::move(arg_header2), std::move(arg_func), std::move(scales), 100);
}
void CardControl::lclick_event(::cw::lib::Value arg_header)
{
    if (m__proc || m__quit) return;
    const auto item = value_to_header(arg_header);
    if (!item) return;
    if (const auto info = std::get_if<InfoPtr>(&*item)) {
        if (*info) rclick_event(arg_header);
        return;
    }
    const auto header = std::get<CardPtr>(*item);
    if (!header) return;
    header->m_negaflag = false;
    const auto owner_value = header->get_owner();
    auto owner = owner_value ? character_value(*owner_value) : std::shared_ptr<::cw::character::Character>{};
    // ゲーム画面で選択したキャラクターの正本は HandView 自体
    // が保持する。CardHeader は Python と同様に所有
    // 者を弱参照するため、 PlayerCard が HandView
    // の選択対象であっても、再構築した手札ヘッダーの weak_ptr
    // が一時的に無効な状態で届くことがある。 関連付けを復元しないと
    // change_specialarea() は Character
    // なしと判断し、対象選択のカーテンを設定せずに戻ってしまう。
    if (m_callname == "HANDVIEW") {
        const auto displayed_owner = character_value(m_selection);
        if (owner && displayed_owner && owner != displayed_owner) {
#if defined(CARDWIRTH_LOG_ENABLED)
            log_cardcontrol("HANDVIEW rejected cross-owner card displayed=" +
                displayed_owner->get_showingname() + " card_owner=" +
                owner->get_showingname() + " card=" + header->m_name);
#endif
            // Python は全行を self.selection.deck.hand
            // から取得するため、この状態にはならない。 別のキャラクターのカ
            // ードをクリック先の PC に適用せず、表示中の手札を再構築する。
            draw_cards(true, -1);
            return;
        }
        if (!owner) owner = displayed_owner;
#if defined(CARDWIRTH_LOG_ENABLED)
        log_cardcontrol("HANDVIEW card click displayed=" +
            (displayed_owner ? displayed_owner->get_showingname() : "<none>") +
            " owner=" + (owner ? owner->get_showingname() : "<none>") +
            " card=" + header->m_name);
#endif
    }
    if (trade_area(m_areaid) && header->m_type == "BeastCard" && !header->m_attachment) {
        auto prompt = cardcontrol_msg("confirm_dump", "「%s」を破棄しますか？");
        if (const auto marker = prompt.find("%s"); marker != ::cw::lib::String::npos)
            prompt.replace(marker, 2, header->m_name);
        bool confirmed = false;
        if (const auto result = ::cw::dialog::message::show_common_modal_message(
                cardcontrol_msg("message", "確認"), prompt, true)) {
            confirmed = *result;
        }
        if (confirmed) {
            const auto runtime = ::cw::g_cwpy;
            const auto frame = runtime ? object_value<::cw::frame::Frame>(runtime->m_frame) : nullptr;
            const auto owner_control = frame
                ? std::dynamic_pointer_cast<CardControl>(frame->dialog_presentation_model())
                : std::shared_ptr<CardControl>{};
            const auto finish = [this, owner_control](const auto&, const auto&) -> ::cw::lib::Value {
                (void)owner_control;
                m__proc = false;
                if (!m__quit) draw_cards(true, -1);
                request_dialog_presentation_refresh();
                return {};
            };
            const auto operation = [runtime, owner, header, frame, finish](const auto&, const auto&) -> ::cw::lib::Value {
                try {
                    if (runtime) runtime->play_sound("dump", false, 100, 1, 0, 0, false);
                    if (owner) owner->throwaway_card(header, true, true);
                    else if (runtime) runtime->trade("TRASHBOX", std::nullopt, header, true, nullptr, -1, -1,
                        false, true, std::nullopt, false, false, false, true, 1);
                } catch (...) {
                    if (frame) frame->exec_func(finish);
                    throw;
                }
                if (frame) frame->exec_func(finish);
                else finish(::cw::lib::Arguments{}, ::cw::lib::KeywordArguments{});
                return {};
            };
            m__proc = true;
            try {
                if (runtime) runtime->exec_func(operation);
                else operation(::cw::lib::Arguments{}, ::cw::lib::KeywordArguments{});
            } catch (...) {
                (void)finish(::cw::lib::Arguments{}, ::cw::lib::KeywordArguments{});
                throw;
            }
            return;
        }
        draw_cards(true, -1);
        return;
    }
    if (!trade_area(m_areaid) && owner && !check_using(owner, header)) {
        draw_cards(true, -1);
        return;
    }
    const bool selected_friend_outside_battle =
        object_value<::cw::character::Friend>(m_selection) &&
        (!::cw::g_cwpy || !::cw::g_cwpy->is_battlestatus());
    const bool hidden_nonplayer_owner = !m_cwpy_debug &&
        (object_value<::cw::character::Enemy>(owner_value.value_or(::cw::lib::Value{})) ||
         object_value<::cw::character::Friend>(owner_value.value_or(::cw::lib::Value{})));
    if (selected_friend_outside_battle || hidden_nonplayer_owner) {
        rclick_event(arg_header);
        return;
    }
    if (auto holder = dynamic_cast<CardHolder*>(this)) {
        if (holder->is_showpersonal() && holder->m_callname == "CARDPOCKET" && trade_area(m_areaid)) {
            if (const auto selection = character_value(holder->m_selection)) {
                if (selection->m_reversed && holder->m_last_cardpocket != 3) {
                    if (::cw::g_cwpy && ::cw::g_cwpy->m_setting && ::cw::g_cwpy->m_setting->m_msgs)
                        show_errordialog(::cw::g_cwpy->m_setting->m_msgs->get(
                            "can_not_trade_reversed", "反転中のキャラクターからはトレードできません。"));
                    return;
                }
            }
        }
    }
    if (m_sendto_selection && ::cw::g_cwpy) {
        if (auto holder = dynamic_cast<CardHolder*>(this)) {
            const auto destination = *m_sendto_selection;
            ::cw::lib::String target_type;
            std::optional<::cw::lib::Value> target;
            bool personal_target = false;
            if (destination == holder->m__combo_storehouse) target_type = "STOREHOUSE";
            else if (destination == holder->m__combo_backpack) target_type = "BACKPACK";
            else if (destination == holder->m__combo_shelf) target_type = "PAWNSHOP";
            else if (destination == holder->m__combo_trush) target_type = "TRASHBOX";
            else if (const auto cast = value_map_index(holder->m__combo_cast, destination); cast && *cast >= 0 && static_cast<std::size_t>(*cast) < holder->m_list2.size()) {
                target_type = "PLAYERCARD"; target = holder->m_list2[static_cast<std::size_t>(*cast)];
            } else if (const auto personal = value_map_index(holder->m__combo_personal, destination); personal && *personal >= 0 && static_cast<std::size_t>(*personal) < holder->m_list2.size()) {
                target_type = "BACKPACK"; target = holder->m_list2[static_cast<std::size_t>(*personal)]; personal_target = true;
            }
            if (!target_type.empty()) {
                if ((target_type == "PLAYERCARD" || personal_target) && try_open_repl_for_sendto(*holder, destination, header)) {
                    return;
                }
                const auto trade_target = personal_target ? std::optional<::cw::lib::Value>{} : target;
                const auto frame = object_value<::cw::frame::Frame>(::cw::g_cwpy->m_frame);
                const auto parentdialog = frame ? frame->m_active_dialog
                                                : std::shared_ptr<::cw::lib::Dialog>{};
                // ゲームスレッドでの取引と UI スレッドでの完了処理が両方戻るまで
                // 、Presentation モデルを保持する。 Python の
                // wx ダイアログも保留中のコールバックを通じて同じ生存期間を持つ。
                const auto owner_control = frame
                    ? std::dynamic_pointer_cast<CardControl>(frame->dialog_presentation_model())
                    : std::shared_ptr<CardControl>{};
                if (parentdialog && owner_control) {
                    const std::weak_ptr<CardControl> weak_owner = owner_control;
                    parentdialog->after_message_callback = [weak_owner] {
                        if (const auto owner = weak_owner.lock()) owner->after_message();
                    };
                }
                const auto target_player = personal_target && target
                    ? object_value<::cw::character::Player>(*target)
                    : std::shared_ptr<::cw::character::Player>{};
                const bool sort = !personal_target || target_type == "STOREHOUSE";
                const auto finish = [this, owner_control](const ::cw::lib::Arguments&, const ::cw::lib::KeywordArguments&) -> ::cw::lib::Value {
                    (void)owner_control;
#if defined(CARDWIRTH_LOG_ENABLED)
                    log_cardcontrol("CardControl sendto UI finish target=" + m_callname);
#endif
                    if (auto holder = dynamic_cast<CardHolder*>(this)) {
                        holder->refresh_after_transfer();
                        return {};
                    }
                    m__proc = false;
                    if (!m__quit) update_narrowcondition();
                    return {};
                };
                const auto queued_at = std::chrono::steady_clock::now();
                const auto operation = [this, target_type, trade_target, header, parentdialog, frame,
                                        queued_at,
                                        target_player, personal_target, sort, finish](const auto&, const auto&) mutable -> ::cw::lib::Value {
#if defined(CARDWIRTH_LOG_ENABLED)
                    ::cardwirth::debug::log_slow("cardcontrol.trade.queue_wait",
                        std::chrono::steady_clock::now() - queued_at,
                        "target=" + target_type + " card=" + header->m_name);
                    const auto trade_started = std::chrono::steady_clock::now();
#endif
                    // Python の CardControl と同様、モーダルを開く可能性
                    // のある取引はネイティブのマウスコールバックの外へ予約する。 WM_LBUTTONUP
                    // から同期的に実行すると、PAWNSHOP/TRASHBOX
                    // の確認でデッドロックし、カード操作画面の再描画とも競合する。
#if defined(CARDWIRTH_LOG_ENABLED)
                    log_cardcontrol("CardControl sendto begin target=" + target_type +
                        " card=" + header->m_name + " personal=" + (personal_target ? "1" : "0"));
#endif
                    try {
                        if (const auto owner = header->m_personal_owner.lock(); personal_target && owner)
                            owner->remove_personalpocket(header);
                        // Py のダイアログからの取引と同様、insertorder_mode=1
                        // を使う。 既存の順序をずらし、新しく挿入するカードの順序を 0 にする。
                        ::cw::g_cwpy->trade(target_type, trade_target, header, false, parentdialog,
                                            -1, -1, sort, false, std::nullopt, false, true,
                                            false, true, 1);
                        if (personal_target && target_player) {
                            target_player->add_personalpocket(header, -1);
                            if (::cw::g_cwpy->m_ydata && *::cw::g_cwpy->m_ydata &&
                                (*::cw::g_cwpy->m_ydata)->m_party && *(*::cw::g_cwpy->m_ydata)->m_party)
                                (*(*::cw::g_cwpy->m_ydata)->m_party)->sort_backpack(std::nullopt, std::nullopt);
                        }
#if defined(CARDWIRTH_LOG_ENABLED)
                        log_cardcontrol("CardControl sendto applied target=" + target_type +
                            " card=" + header->m_name);
#endif
                    } catch (const std::exception& error) {
                        const auto message = ::cw::lib::String("カード移動に失敗しました。\n") + error.what() +
                            "\n\nStack trace:\n" + ::cw::thread::current_exception_stacktrace();
#if defined(CARDWIRTH_LOG_ENABLED)
                        log_cardcontrol("CardControl sendto error target=" + target_type +
                            " card=" + header->m_name + " message=" + message);
#endif
                        auto show = [this, message, finish](const auto&, const auto&) mutable -> ::cw::lib::Value {
                                // ネイティブのエラー表示自体が失敗する場合も
                                // ある。移動処理は既に終了しているため、 子
                                // 画面を開けなくても処理中フラグを残さない。
                                try { show_errordialog(message); }
                                catch (...) { m__proc = false; throw; }
                                return finish({}, {});
                            };
                        if (frame) frame->exec_func(show);
                        else show(::cw::lib::Arguments{}, ::cw::lib::KeywordArguments{});
                        return {};
                    } catch (...) {
                        const auto message = ::cw::lib::String("カード移動に失敗しました。\n不明なエラー") +
                            "\n\nStack trace:\n" + ::cw::thread::current_exception_stacktrace();
#if defined(CARDWIRTH_LOG_ENABLED)
                        log_cardcontrol("CardControl sendto error target=" + target_type +
                            " card=" + header->m_name + " message=" + message);
#endif
                        auto show = [this, message, finish](const auto&, const auto&) mutable -> ::cw::lib::Value {
                                // 標準例外以外の取引失敗にも、同じ後処理を行う。
                                try { show_errordialog(message); }
                                catch (...) { m__proc = false; throw; }
                                return finish({}, {});
                            };
                        if (frame) frame->exec_func(show);
                        else show(::cw::lib::Arguments{}, ::cw::lib::KeywordArguments{});
                        return {};
                    }
#if defined(CARDWIRTH_LOG_ENABLED)
                    ::cardwirth::debug::log_slow("cardcontrol.trade.total",
                        std::chrono::steady_clock::now() - trade_started,
                        "target=" + target_type + " card=" + header->m_name);
                    log_cardcontrol("CardControl sendto game trade returned target=" + target_type +
                        " card=" + header->m_name);
#endif
                    // trade() は YESNO/NOTICE を同期的に開く可能
                    // 性があるため、ゲームスレッドで実行する。 cw/dialog/cardcontrol.py::lclick_event
                    // と同様、モデル更新だけを Frame
                    // の UI ディスパッチャーへ戻す。
                    if (frame) frame->exec_func(finish);
                    else finish({}, {});
                    return {};
                };
                m__proc = true;
                m__cancel_animation = true;
#if defined(CARDWIRTH_LOG_ENABLED)
                log_cardcontrol("CardControl sendto queued target=" + target_type +
                    " card=" + header->m_name + " personal=" + (personal_target ? "1" : "0"));
#endif
                // Python と同様、移動処理は常に CWPy のゲームスレッドに予約する。
                // Presentation の処理ループで実行すると、PAWNSHOP/TRASHBOX
                // が同じ UI スレッドに確認を求めた時点でデッドロックする。
                try { ::cw::g_cwpy->exec_func(operation); }
                catch (...) { (void)finish({}, {}); throw; }
                return;
            }
            // 未知のコンボIDはWindows combo adapterの再マッピング後に再試行する。
            m_sendto_selection.reset();
        }
    }
    // 開いていたダイアログの履歴を、wx の exec_func による遅延コールバッ
    // クと同じタイミングで記録する。 selection は表示中のキャラクターのまま
    // とし、Python と同様、クリックした CardHeader で上書きしない。
    if (::cw::g_cwpy) {
        const auto runtime = ::cw::g_cwpy;
        std::shared_ptr<::cw::sprite::card::CWPyCard> index2;
        if (const auto holder = dynamic_cast<CardHolder*>(this))
            index2 = object_value<::cw::sprite::card::CWPyCard>(holder->m_index2);
        else if (const auto hand = dynamic_cast<HandView*>(this))
            index2 = object_value<::cw::sprite::card::CWPyCard>(hand->m_index2);
        const auto callname = m_callname;
        const auto pos = m_parent
            ? std::tuple<::cw::lib::Integer, ::cw::lib::Integer>{m_parent->bounds.x, m_parent->bounds.y}
            : std::tuple<::cw::lib::Integer, ::cw::lib::Integer>{0, 0};
        // Python は OK イベントを送る前に selectedheader
        // を書き込む。ネイティブ版でも書き込みはゲームスレッ
        // ドへ送るが、 以前の対象選択エリアを終了しているとい
        // う理由だけで取り消さない。通常、その終了処理は交換先
        // の選択直前に予約されるため、 全体の世代で無効化する
        // と新しい選択が消え、以前の戦闘行動が残ってしまう。
        m__proc = true;
        try {
            runtime->exec_func([runtime, callname,
                                index2 = std::move(index2), pos, header,
                                selection_owner = owner](const auto&, const auto&) mutable {
                if (::cw::g_cwpy != runtime) return ::cw::lib::Value{};
                // UI からゲームキューへの受け渡し中も所有者を保持し、selectedheader
                // の公開前に CardHeader の弱参照を復元する。 Python
                // が HandView.selection と CardHeader.get_owner()
                // を通して保持するものと同じ関連付けである。
                if (selection_owner && callname == "HANDVIEW") {
                    header->set_owner(::cw::lib::Value(
                        std::static_pointer_cast<::cw::lib::Object>(selection_owner)));
                }
                // Python と同様、戦闘の手動対象選択前に必ず元のダイアログを記録
                // する。 index2 が null でない場合だけに制限すると、HANDVIEW
                // や CARDPOCKET が必要な履歴なしで Character::set_action()
                // に到達することがある。
                runtime->m_pre_dialogs.emplace_back(
                    callname, std::move(index2), pos, ::cw::g_UP_WIN);
                runtime->m_selectedheader = header;
#if defined(CARDWIRTH_LOG_ENABLED)
                log_cardcontrol("CardControl selection committed callname=" + callname +
                    " card=" + header->m_name + " target=" + header->m_target);
#endif
                return ::cw::lib::Value{};
            });
            // 共通のゲームキューで、Py のヘッダー・履歴の設定、更新、OnOk による
            // 対象エリア処理の順序を、 ネイティブの UI・ゲーム境界を越えて維持する。
            runtime->exec_func([runtime](const auto&, const auto&) -> ::cw::lib::Value {
                runtime->update_tradecards();
                runtime->set_testaptitude(runtime->m_selectedheader);
                runtime->update_selectablelist();
                return {};
            });
        } catch (...) {
            m__proc = false;
            throw;
        }
    }
    OnOk(nullptr);
}
void CardControl::rclick_event(::cw::lib::Value arg_header) {
    if (const auto item = value_to_header(arg_header)) {
        std::visit([](const auto& header) { if (header) header->m_negaflag = true; }, *item);
        if (m_ui_backend) m_ui_backend->show_info(*this, arg_header);
    }
    if (m_ui_backend) m_ui_backend->invalidate(*this);
}
void CardControl::process_ui_events() { if (m_ui_backend) m_ui_backend->pump(*this); }
std::shared_ptr<CardControlUiBackend> CardControl::ui_backend() const { return m_ui_backend; }
void CardControl::after_message() { focus_control({},false); }
void CardControl::show_errordialog(::cw::lib::String arg_s)
{
    if (::cw::g_cwpy) ::cw::g_cwpy->play_sound("error", false, 100, 1, 0, 0, false);
    if (::cw::g_cwpy && ::cw::g_cwpy->m_setting && !::cw::g_cwpy->m_setting->m_noticeimpossibleaction) return;
    if (m_ui_backend) m_ui_backend->show_error(*this, arg_s);
}
bool CardControl::check_using(std::shared_ptr<::cw::character::Character> arg_owner, std::shared_ptr<::cw::header::CardHeader> arg_header)
{
    if (!arg_owner || !arg_header) return false;
    const auto notify = [this, &arg_owner](const ::cw::lib::String& key, const ::cw::lib::String& fallback) {
        auto message = fallback;
        if (::cw::g_cwpy && ::cw::g_cwpy->m_setting && ::cw::g_cwpy->m_setting->m_msgs)
            message = ::cw::g_cwpy->m_setting->m_msgs->get(key, fallback);
        const auto marker = message.find("%s");
        if (marker != ::cw::lib::String::npos) message.replace(marker, 2, arg_owner->get_showingname());
        show_errordialog(std::move(message));
    };
    if (arg_owner->m_reversed) { notify("reversed", "%sは隠蔽中です。"); return false; }
    if (arg_owner->is_inactive()) { notify("inactive", "%sは行動不能です。"); return false; }
    const auto holder = dynamic_cast<CardHolder*>(this);
    if (holder && holder->is_showpersonal() && holder->m_callname == "CARDPOCKET" && holder->m_last_cardpocket == 3 && arg_header->m_type == "SkillCard") {
        show_errordialog(cardcontrol_msg("use_failed_personal_skill_card", "私有カードから特殊技能カードは使用できません。"));
        return false;
    }
    if (arg_header->m_uselimit <= 0 &&
        ((arg_header->m_type != "ItemCard" && arg_header->m_type != "BeastCard") ||
         arg_header->m_recycle || arg_header->m_maxuselimit != 0)) {
        if (::cw::g_cwpy) ::cw::g_cwpy->play_sound("error", false, 100, 1, 0, 0, false);
        return false;
    }
    if (holder && holder->is_showpersonal() && holder->m_callname == "CARDPOCKET" && holder->m_last_cardpocket == 3 && arg_header->is_backpackheader()) {
        const auto player = std::dynamic_pointer_cast<::cw::character::Player>(arg_owner);
        if (player && (arg_header->m_type == "ItemCard" || arg_header->m_type == "BeastCard")) {
            const auto pocket = arg_header->m_type == "ItemCard" ? 1 : 2;
            const auto& cards = player->m_cardpocket_typed;
            const auto spaces = player->get_cardpocketspace();
            const auto count = pocket == 1 ? std::get<1>(cards).size() : std::get<2>(cards).size();
            const auto capacity = pocket == 1 ? std::get<1>(spaces) : std::get<2>(spaces);
            if (static_cast<::cw::lib::Integer>(count) >= capacity) {
                notify("use_failed_hand_be_full", "%sの手札が一杯です。");
                return false;
            }
        }
    }
    // 同じカードを使えない理由が複数ある場合、Py と同様、戦闘
    // ペナルティや発動条件より先に個人ポケットの容量を通知する。
    if (arg_owner->is_autoselectedpenalty(std::nullopt) && !m_cwpy_debug) {
        show_errordialog(cardcontrol_msg("selected_penalty", "ペナルティカードは選択できません。"));
        return false;
    }
    if (arg_header->m_type == "BeastCard" && !arg_header->is_activewithstatus(arg_owner)) {
        show_errordialog(cardcontrol_msg("beast_invocation_failed", "召喚獣を発動できません。"));
        return false;
    }
    return true;
}
void CardControl::OnOk(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
#if defined(CARDWIRTH_LOG_ENABLED)
    log_cardcontrol("CardControl OnOk callname=" + m_callname + " quit=" + (m__quit ? "1" : "0"));
#endif
    if (m__quit) return;
    m__quit = true;
    m_ui_info_header.reset();
    m_cardinfo_dialog.reset();
    _stop_animtimer();
    if ((m_callname == "CARDPOCKET" || m_callname == "CARDPOCKETB" || m_callname == "HANDVIEW") &&
        ::cw::g_cwpy && ::cw::g_cwpy->is_playingscenario() && ::cw::g_cwpy->m_areaid >= 0) {
        const auto runtime = ::cw::g_cwpy;
        const auto area = runtime->m_areaid;
        runtime->exec_func([runtime, area](const auto&, const auto&) -> ::cw::lib::Value {
            runtime->change_specialarea(area, false);
            return {};
        });
    }
    if (m_ui_backend) m_ui_backend->close(*this);
    ::cw::frame::Frame_kill_dlg_if_model(this);
}
void CardControl::OnCancel(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
#if defined(CARDWIRTH_LOG_ENABLED)
    log_cardcontrol("CardControl OnCancel callname=" + m_callname + " quit=" + (m__quit ? "1" : "0"));
#endif
    retire_field_inputs();
    if (m__quit) return;
    m__quit = true;
    m_ui_info_header.reset();
    m_cardinfo_dialog.reset();
    _stop_animtimer();
    m_list.clear();
    m_list2.clear();
    m_selection = {};
    // Python と同様、一時エリアの終了はゲームスレッドに予約す
    // る。 ネイティブのウィンドウプロシージャーから直接呼ぶと描画ル
    // ープと競合し、ウィンドウが消えても取引エリアが残る場合がある。
    if (::cw::g_cwpy && m_callname != "CARDPOCKET_REPLACE" && m_callname != "INFOVIEW") {
        auto runtime = ::cw::g_cwpy;
        runtime->exec_func([runtime](const ::cw::lib::Arguments&, const ::cw::lib::KeywordArguments&) {
            if (trade_area(runtime->m_areaid)) {
                runtime->clear_specialarea(false, false);
                // add_lazydraw(
                // nullptr)は何もしな
                // い。この空矩形も現在のadd_lazydraw_rect
                // では無視
                // される。
                runtime->add_lazydraw_rect({});
            }
            return ::cw::lib::Value{};
        });
    }
    if (m_ui_backend) m_ui_backend->close(*this);
    ::cw::frame::Frame_kill_dlg_if_model(this);
}

void CardHolder::initialize(std::shared_ptr<::cw::lib::Window> arg_parent, ::cw::lib::String arg_callname) { m_callname = std::move(arg_callname); CardControl::initialize(std::move(arg_parent)); CardControl::m_callname = m_callname; m_page_size = 10; m_page_count = 1; }
void CardHolder::reconstruct_cardholder(::cw::lib::String arg_callname, std::optional<std::shared_ptr<::cw::sprite::card::CWPyCard>> arg_selection, std::optional<std::tuple<::cw::lib::String, std::shared_ptr<::cw::sprite::card::CWPyCard>, std::tuple<::cw::lib::Integer, ::cw::lib::Integer>, ::cw::lib::Real>> arg_pre_info, std::optional<::cw::lib::Integer> arg_areaid)
{
#if defined(CARDWIRTH_LOG_ENABLED)
    ::cardwirth::debug::log("reconstruct_cardholder begin: " + arg_callname);
#endif
    if (!::cw::g_cwpy || !::cw::g_cwpy->m_ydata || !*::cw::g_cwpy->m_ydata)
        throw std::logic_error("Card holder requires inn data");
    m_callname = std::move(arg_callname);
    if (m_callname == "CARDPOCKETB" && ::cw::g_cwpy) {
        if (auto scenario = object_value<::cw::data::ScenarioData>(::cw::g_cwpy->m_sdata); scenario && !scenario->m_party_environment_backpack) m_callname = "CARDPOCKET";
    }
    CardControl::m_callname = m_callname;
    if (m_callname != "INFOVIEW" && ::cw::g_cwpy->m_setting &&
        !::cw::g_cwpy->m_setting->m_show_personal_cards && ::cw::g_cwpy->m_setting->m_last_cardpocket == 3)
        ::cw::g_cwpy->m_setting->m_last_cardpocket = 0;
    CardControl::m_areaid = arg_areaid.value_or(::cw::g_cwpy ? ::cw::g_cwpy->m_areaid : 0);
    m_areaid = ::cw::lib::Value(CardControl::m_areaid);

    // Python と同様、カード所有先の画面を再構築する前に、一時的な
    // 取引表示をすべて除く。 これらのスプライトはゲーム・描画スレッドに
    // 属するため、そのスレッドで後処理し、 ネイティブのダイアログ表示
    // 中に適性・カード枚数・価格の印をキャラクターカード上へ残さない。
    if (::cw::g_cwpy) {
        auto runtime = ::cw::g_cwpy;
        runtime->exec_func([runtime](const ::cw::lib::Arguments&, const ::cw::lib::KeywordArguments&) {
            for (const auto& pcard : runtime->get_pcards(::cw::lib::Value(""))) {
                if (!pcard) continue;
                if (pcard->m_test_aptitude) {
                    pcard->m_test_aptitude.reset();
                    pcard->update_image(false, std::nullopt);
                    ::cw::sprite::card::refresh_card_layer(*pcard);
                }
                runtime->add_lazydraw_rect(pcard->m_rect);
            }
            runtime->clear_numberofcards();
            runtime->remove_pricesprites();
            return ::cw::lib::Value{};
        });
    }

    m_selection = {};
    m_index2 = {};
    m_index = 0;
    m_page_history.clear();
    if (::cw::g_cwpy) if (auto system = object_value<::cw::data::SystemData>(::cw::g_cwpy->m_sdata)) m_environment_backpack = system->m_party_environment_backpack;
    const bool personal_roster = ::cw::g_cwpy && ::cw::g_cwpy->m_setting &&
        ::cw::g_cwpy->m_setting->m_show_personal_cards && arg_selection && object_value<::cw::character::Player>(object_value(*arg_selection));
    const auto status = trade_area(CardControl::m_areaid)
        ? ::cw::lib::Value(personal_roster ? "" : "unreversed") : ::cw::lib::Value("active");
#if defined(CARDWIRTH_LOG_ENABLED)
    ::cardwirth::debug::log("reconstruct_cardholder _init_list: " + m_callname);
#endif
    _init_list(status);
    if (arg_pre_info) {
        m_pre_pos = std::get<2>(*arg_pre_info);
        m_index2 = object_value(std::get<1>(*arg_pre_info));
        // pre_pos は変更前のウィンドウ拡大率に基づく物理座標である。
        // Python と同様、UP_WIN が変わった場合は破棄する。
        if (::cw::g_UP_WIN != std::get<3>(*arg_pre_info)) m_pre_pos.reset();
    } else m_pre_pos.reset();
    if (::cw::g_cwpy && ::cw::g_cwpy->m_setting) {
        const auto& setting = ::cw::g_cwpy->m_setting;
        if (!arg_pre_info) {
            setting->m_card_narrow.clear();
            setting->m_edit_star = false;
            setting->m_last_backpackpage = 0;
            setting->m_last_storehousepage = 0;
            setting->m_last_sendto = 0;
            for (std::size_t i = 0; i < 3; ++i) sequence_set(setting->m_show_cardtype, i, ::cw::lib::Value(true));
            for (std::size_t i = 0; i < 3; ++i) sequence_set(setting->m_last_cardpocketbpage, i, ::cw::lib::Value(0));
        }
        m_show_personal = setting->m_show_personal_cards;
        m_last_cardpocket = std::clamp<::cw::lib::Integer>(setting->m_last_cardpocket, 0, 3);
        if (!m_show_personal && m_last_cardpocket == 3) m_last_cardpocket = 0;
        if (m_callname == "STOREHOUSE") m_index = setting->m_last_storehousepage;
        else if (m_callname == "BACKPACK") m_index = setting->m_last_backpackpage;
        else if (m_callname == "CARDPOCKETB") {
            if (const auto p = sequence_at(setting->m_last_cardpocketbpage, static_cast<std::size_t>(m_last_cardpocket))) m_index = int_value(*p);
        }
    }
    if (!arg_pre_info && m_callname == "CARDPOCKET") {
        m_selection = arg_selection ? object_value(*arg_selection) : ::cw::lib::Value{};
        m_index2 = m_selection;
    }

    // Python と同様、手札を再構築する前に pre_info に保
    // 存したキャラクターを復元する。 index2 と selection
    // を同期させないと、CARDPOCKET が以前のキャラクター
    // のカードで再表示され、側面ボタンも現在位置を特定できなくなる。
    if (m_callname == "CARDPOCKET" || m_callname == "CARDPOCKETB") {
        if (arg_pre_info) m_selection = m_index2;
        m_list2.clear();
        if (::cw::g_cwpy) {
            if (arg_pre_info || object_value<::cw::character::Player>(m_selection)) {
                for (const auto& card : ::cw::g_cwpy->get_pcards(status)) m_list2.push_back(object_value(card));
            } else if (object_value<::cw::sprite::card::FriendCard>(m_selection)) {
                for (const auto& card : ::cw::g_cwpy->get_fcards(status)) m_list2.push_back(object_value(card));
                if (::cw::g_cwpy->m_sct) {
                    if (const auto scenario = object_value<::cw::data::ScenarioData>(::cw::g_cwpy->m_sdata);
                        scenario && ::cw::g_cwpy->m_sct->lessthan("1.30", scenario->get_versionhint(0)))
                        std::reverse(m_list2.begin(), m_list2.end());
                }
            }
        }
    }
    if ((m_callname == "CARDPOCKET" || m_callname == "CARDPOCKETB") && !character_value(m_selection))
        throw std::invalid_argument("Card pocket requires a character");
    if (m_callname == "CARDPOCKETB") if (const auto owner = character_value(m_selection)) {
        const auto& pocket = owner->m_cardpocket_typed;
        const auto spaces = owner->get_cardpocketspace();
        const auto count = m_last_cardpocket == 0 ? std::get<0>(pocket).size() : m_last_cardpocket == 1 ? std::get<1>(pocket).size() : std::get<2>(pocket).size();
        const auto capacity = m_last_cardpocket == 0 ? std::get<0>(spaces) : m_last_cardpocket == 1 ? std::get<1>(spaces) : std::get<2>(spaces);
        if (static_cast<::cw::lib::Integer>(count) >= capacity) { m_callname = "CARDPOCKET"; CardControl::m_callname = m_callname; }
    }
    if (m_callname == "CARDPOCKET") _init_cardpocketlist();
    else if (m_callname == "CARDPOCKETB") _set_backpacklist(false);
    bool has_party_members = false;
    if (::cw::g_cwpy && ::cw::g_cwpy->m_ydata && *::cw::g_cwpy->m_ydata && (*::cw::g_cwpy->m_ydata)->m_party &&
        *(*::cw::g_cwpy->m_ydata)->m_party)
        has_party_members = !(*(*::cw::g_cwpy->m_ydata)->m_party)->m_members.empty();
    m_can_open_cardpocket = m_callname != "INFOVIEW" && has_party_members;
    m_can_open_storehouse = m_callname != "INFOVIEW" && ::cw::g_cwpy && !::cw::g_cwpy->is_playingscenario();
    const bool player_selected = object_value<::cw::character::Player>(m_selection) != nullptr;
    const bool hand_sendto = player_selected && ::cw::g_cwpy &&
        (!::cw::g_cwpy->is_playingscenario() || CardControl::m_areaid == int_value(::cw::g_AREA_CAMP) || trade_area(CardControl::m_areaid));
    const bool sendto = m_callname == "BACKPACK" || m_callname == "STOREHOUSE" ||
                        ((m_callname == "CARDPOCKET" || m_callname == "CARDPOCKETB") && hand_sendto);
    m_can_open_backpack = m_can_open_cardpocket && m_environment_backpack &&
                          (m_callname == "BACKPACK" || m_callname == "STOREHOUSE" ||
                           m_callname == "CARDPOCKETB" || hand_sendto);
    ::cw::lib::String title = m_callname;
    if (::cw::g_cwpy && ::cw::g_cwpy->m_setting && ::cw::g_cwpy->m_setting->m_msgs) {
        if (m_callname == "BACKPACK") title = ::cw::g_cwpy->m_setting->m_msgs->get("cards_backpack", "荷物袋の手札カード");
        else if (m_callname == "STOREHOUSE") title = ::cw::g_cwpy->m_setting->m_msgs->get("cards_storehouse", "カード置場の手札カード");
        else if (m_callname == "INFOVIEW") title = ::cw::g_cwpy->m_setting->m_msgs->get("info_card", "情報カード");
    }
    if (m_callname == "CARDPOCKET" || m_callname == "CARDPOCKETB")
        title = ::cw::lib::python_percent_string(cardcontrol_msg("cards_hand", "%sの手札カード"), character_value(m_selection)->get_name());
#if defined(CARDWIRTH_LOG_ENABLED)
    ::cardwirth::debug::log("reconstruct_cardholder reconstruct: " + m_callname);
#endif
    CardControl::reconstruct(m_callname, title, sendto, m_callname == "BACKPACK" || m_callname == "STOREHOUSE" || m_callname == "CARDPOCKETB", CardControl::m_areaid);
    if (m_callname == "CARDPOCKETB" && ::cw::g_cwpy && ::cw::g_cwpy->m_setting && ::cw::g_cwpy->m_setting->m_sort_cards == "None") m_index = 0;
    if (::cw::g_cwpy && ::cw::g_cwpy->m_setting) m__cardpocketinfo = ::cw::lib::Value(::cw::g_cwpy->m_setting->m_last_cardpocket);
    _build_sendto_combo();
    m__fulllist = m_list;
#if defined(CARDWIRTH_LOG_ENABLED)
    ::cardwirth::debug::log("reconstruct_cardholder _narrow: " + m_callname +
        " cards=" + std::to_string(m__fulllist.size()));
#endif
    _narrow(m__fulllist);
    _update_page();
    _update_sortattr(true);
    _enable_updown();
    if (m_callname == "CARDPOCKET" || m_callname == "CARDPOCKETB") change_parent_selection(m_selection);
    else if (m_callname == "BACKPACK") {
        m_selection = ::cw::lib::Value{};
        change_parent_selection_menu("BACKPACK");
    } else if (m_callname == "STOREHOUSE") {
        m_selection = ::cw::lib::Value{};
        change_parent_selection_menu("STOREHOUSE");
    }
#if defined(CARDWIRTH_LOG_ENABLED)
    ::cardwirth::debug::log("reconstruct_cardholder draw_cards: " + m_callname);
#endif
    draw_cards(true, -1);
    _start_animtimer();
    // ルートの Presentation は Frame が公開する。再
    // 構築時に別のホストウィンドウや二つ目のモーダルループを開かない。
#if defined(CARDWIRTH_LOG_ENABLED)
    ::cardwirth::debug::log("reconstruct_cardholder end: " + m_callname);
#endif
}
void CardHolder::_init_list(::cw::lib::Value)
{
    throw std::invalid_argument("CardHolder::_init_list requires a concrete selector");
}
void CardHolder::initialize_inventory_list(::cw::lib::Value arg_status)
{
    m_list.clear();
    m_list2.clear();
    const auto status = string_value(arg_status);
    if (::cw::g_cwpy && (m_callname == "CARDPOCKET" || m_callname == "HANDVIEW")) {
        const auto values = ::cw::g_cwpy->get_pcards(::cw::lib::Value(status));
        for (const auto& card : values) m_list2.push_back(object_value(card));
    }
    if (m_callname == "INFOVIEW") {
        // get_infocardheaders() は ScenarioData のキャ
        // ッシュにまだないヘッダーを遅延生成する。 m__infocard_cache だ
        // けを走査すると、有効な情報カードが一覧に現れなくなる。 Python は具体的な
        // 型を制限せず sdata.get_infocardheaders() を呼ぶ。
        // キャッシュ済みヘッダーを含め、この API は SystemData が持つ。
        if (::cw::g_cwpy) if (auto scenario = object_value<::cw::data::SystemData>(::cw::g_cwpy->m_sdata))
            for (const auto& header : scenario->get_infocardheaders()) if (header) m_list.push_back(object_value(header));
    } else if (::cw::g_cwpy && ::cw::g_cwpy->m_ydata && *::cw::g_cwpy->m_ydata) {
        auto& ydata = **::cw::g_cwpy->m_ydata;
        if (m_callname == "STOREHOUSE") m_list = card_values(ydata.m_storehouse);
        else if ((m_callname == "BACKPACK" || m_callname == "CARDPOCKETB") && ydata.m_party && *ydata.m_party) m_list = card_values((**ydata.m_party).m_backpack);
        else if (m_callname == "CARDPOCKET") if (auto owner = character_value(m_selection)) {
            if (m_last_cardpocket == 3) {
                if (auto player = std::dynamic_pointer_cast<::cw::character::Player>(owner)) m_list = card_values(player->m_personal_pocket);
            } else {
                const auto& p = owner->m_cardpocket_typed;
                const auto& source = m_last_cardpocket == 1 ? std::get<1>(p) : m_last_cardpocket == 2 ? std::get<2>(p) : std::get<0>(p);
                m_list = card_values(source);
            }
        }
    }
    m_card_list = cards_from_values(m_list);
    m_card_full_list = m_card_list;
    m__fulllist = m_list;
    m_typed_full_list = typed_headers(m_list);
    m_typed_list = m_typed_full_list;
}
void CardHolder::_bind() { CardControl::_bind(); }
void CardHolder::_build_sendto_combo()
{
    m_sendto_labels.clear();m_sendto_icons.clear();
    // ラベルは名前だけで再構築されるため、記憶したカード種
    // 別を無効にする。 再構築や移動の前後で種別が同じでも
    // 、次に表示・ホバーしたカードから枚数を再設定する。
    m__cardpocketinfo = ::cw::lib::Value{};
    m__combo_manual = -1;
    m__combo_storehouse = -1;
    m__combo_backpack = -1;
    m__combo_shelf = -1;
    m__combo_trush = -1;
    m__combo_cast.clear();
    m__combo_personal.clear();
    if (!m_sendto) return;

    const auto append = [this](const ::cw::lib::String& label,const char* icon) {
        m_sendto_labels.push_back(label);m_sendto_icons.emplace_back(icon);
        return static_cast<::cw::lib::Integer>(m_sendto_labels.size() - 1);
    };

    m__combo_manual = append(cardcontrol_msg("send_to_manual", "手動選択"),"ARROW");
    if (m_can_open_storehouse)
        m__combo_storehouse = append(cardcontrol_msg("send_to_storehouse", "カード置場"),"DECK");
    if (m_can_open_backpack)
        m__combo_backpack = append(cardcontrol_msg("send_to_backpack", "荷物袋"),"SACK");
    if (m_can_open_cardpocket) {
        for (std::size_t raw_index = 0; raw_index < m_list2.size(); ++raw_index) {
            const auto& value = m_list2[raw_index];
            const auto card = object_value<::cw::sprite::card::CWPyCard>(value);
            if (!card || card->m_reversed) continue;
            const auto combo_index = append(card->get_showingname(),"CAST");
            m__combo_cast[std::to_string(combo_index)] = ::cw::lib::Value(static_cast<::cw::lib::Integer>(raw_index));
        }
        for (std::size_t raw_index = 0; raw_index < m_list2.size(); ++raw_index) {
            const auto& value = m_list2[raw_index];
            const auto player = object_value<::cw::character::Player>(value);
            if (!player) continue;
            ::cw::lib::String label = cardcontrol_msg("send_to_personal", "%sの私有");
            const auto name = player->get_showingname();
            if (const auto marker = label.find("%s"); marker != ::cw::lib::String::npos)
                label.replace(marker, 2, name);
            label += "(" + std::to_string(player->m_personal_pocket.size()) + "/" +
                     std::to_string(player->get_personalpocketspace()) + ")";
            const auto combo_index = append(label,"CAST_PERSONAL");
            m__combo_personal[std::to_string(combo_index)] = ::cw::lib::Value(static_cast<::cw::lib::Integer>(raw_index));
        }
        if (m_callname == "CARDPOCKET" && ::cw::g_cwpy && ::cw::g_cwpy->m_setting)
            _update_cardpocketinfo(::cw::g_cwpy->m_setting->m_last_cardpocket, true);
    }
    if (::cw::g_cwpy && !::cw::g_cwpy->is_playingscenario())
        m__combo_shelf = append(cardcontrol_msg("send_to_shelf", "棚"),"SHELF");
    if (::cw::g_cwpy && (!::cw::g_cwpy->is_playingscenario() || ::cw::g_cwpy->is_debugmode()))
        m__combo_trush = append(cardcontrol_msg("send_to_trush", "ゴミ箱"),"TRUSH");

    ::cw::lib::Integer selection = 0;
    if (::cw::g_cwpy && ::cw::g_cwpy->m_setting)
        selection = static_cast<::cw::lib::Integer>(::cw::g_cwpy->m_setting->m_last_sendto);
    if (!m_sendto_labels.empty())
        selection = std::clamp<::cw::lib::Integer>(
            selection, 0, static_cast<::cw::lib::Integer>(m_sendto_labels.size()) - 1);
    m_sendto_selection = selection;
    if (::cw::g_cwpy && ::cw::g_cwpy->m_setting)
        ::cw::g_cwpy->m_setting->m_last_sendto = selection;
}
void CardHolder::update_debug()
{
    CardControl::update_debug();
    if (m_sendto && ::cw::g_cwpy && ::cw::g_cwpy->is_playingscenario()) {
        const auto previous_trash = m__combo_trush;
        const bool was_trash = m_sendto_selection && previous_trash >= 0 &&
                               *m_sendto_selection == previous_trash;
        _build_sendto_combo();
        if (!::cw::g_cwpy->is_debugmode() && was_trash) {
            m_sendto_selection = m__combo_manual >= 0 ? std::optional<::cw::lib::Integer>(m__combo_manual)
                                                      : std::optional<::cw::lib::Integer>{};
            if (::cw::g_cwpy->m_setting)
                ::cw::g_cwpy->m_setting->m_last_sendto = m__combo_manual >= 0 ? m__combo_manual : 0;
        }
        draw_cards(true, -1);
    }
}
void CardHolder::_load_index()
{
    // 新しく選択した PC は先頭ページから表示する。 保管場所
    // のページ履歴や、設定が初期化済みかどうかには依存させない。
    if (m_callname == "CARDPOCKET") { m_index = 0; return; }
    if (::cw::g_cwpy && ::cw::g_cwpy->m_setting) {
        const auto& setting = ::cw::g_cwpy->m_setting;
        if (m_callname == "STOREHOUSE") m_index = setting->m_last_storehousepage;
        else if (m_callname == "BACKPACK") m_index = setting->m_last_backpackpage;
        else if (m_callname == "CARDPOCKETB") if (const auto p = sequence_at(setting->m_last_cardpocketbpage, static_cast<std::size_t>(m_last_cardpocket))) m_index = int_value(*p);
        else m_index = m_page_history.empty() ? 0 : m_page_history.front();
    } else m_index = m_page_history.empty() ? 0 : m_page_history.front();
}
void CardHolder::_store_index()
{
    if (m_page_history.empty()) m_page_history.push_back(m_index);
    else m_page_history.front() = m_index;
    if (!::cw::g_cwpy || !::cw::g_cwpy->m_setting) return;
    auto& setting = *::cw::g_cwpy->m_setting;
    // Py と同様、ページ番号コントロールの変更時だけでなく、この共通の入口
    // で保存する。 絞り込み変更後の draw_cards() も含める。
    if (m_callname == "STOREHOUSE") setting.m_last_storehousepage = m_index;
    else if (m_callname == "BACKPACK") setting.m_last_backpackpage = m_index;
    else if (m_callname == "CARDPOCKETB") {
        const auto sequence = object_value<::cw::data::VariantSequence>(setting.m_last_cardpocketbpage);
        if (!sequence) throw std::invalid_argument("card pocket page settings require a list");
        auto pocket = setting.m_last_cardpocket;
        if (pocket < 0) pocket += static_cast<::cw::lib::Integer>(sequence->m_values.size());
        if (pocket < 0 || static_cast<std::size_t>(pocket) >= sequence->m_values.size())
            throw std::out_of_range("card pocket page setting index out of range");
        sequence->m_values[static_cast<std::size_t>(pocket)] = ::cw::lib::Value(m_index);
    }
}
void CardHolder::OnPageSetFocus(std::shared_ptr<::cw::lib::Window> arg_event) {
    (void)arg_event;
    focus_control(FocusTarget{::cw::dialog::DialogPresentationAction::Auxiliary,15},true);
    if(m_input_focus==15) {m_page_editor.edit.anchor=0;m_page_editor.edit.move(m_page_editor.edit.text.size(),true);}
}
void CardHolder::OnSort(std::shared_ptr<::cw::lib::Window> arg_event)
{
    m__cancel_animation = true;
    focus_control({},false); // Python と同様、直接のコールバックからも先頭パネルへ戻る。
    if (arg_event) m_sort_selection = std::clamp<::cw::lib::Integer>(arg_event->id, 0, 7);
    m_sort_selection = std::clamp<::cw::lib::Integer>(m_sort_selection, 0, 7);
    m_sort = ::cw::lib::Value(m_sort_selection);
    if (m_callname != "STOREHOUSE" && m_callname != "BACKPACK" && m_callname != "CARDPOCKETB") return;
    // ネイティブのモデルは実行環境の終了後も残る場合がある。Python は cwpy が存在する場合だけここを呼ぶ。
    if (!::cw::g_cwpy || !::cw::g_cwpy->m_setting) return;
    const auto& sort_name = card_sort_names()[static_cast<std::size_t>(m_sort_selection)];
    auto& selected_sort = ::cw::g_cwpy->m_setting->m_sort_cards;
    if (selected_sort == sort_name) return;
    if (::cw::g_cwpy) ::cw::g_cwpy->play_sound("page", false, 100, 1, 0, 0, false);
    selected_sort = sort_name;
    _update_sortattr(true);
}
void CardHolder::OnSortWithStar(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    m__cancel_animation = true;
    if (m_callname != "BACKPACK" && m_callname != "STOREHOUSE" && m_callname != "CARDPOCKETB") return;
    if (::cw::g_cwpy) ::cw::g_cwpy->play_sound("page", false, 100, 1, 0, 0, false);
    m_sort_with_star = !m_sort_with_star;
    if (::cw::g_cwpy && ::cw::g_cwpy->m_setting)
        ::cw::g_cwpy->m_setting->m_sort_cardswithstar = m_sort_with_star;
    _update_sortattr(true);
}
void CardHolder::_update_sortwithstar() { CardControl::_update_sortwithstar(); }
void CardHolder::_update_editstar() { CardControl::_update_editstar(); }
bool CardHolder::_update_sortattr(bool)
{
    throw std::invalid_argument("CardHolder::_update_sortattr requires a concrete selector");
}
void CardHolder::OnShowSkill(std::shared_ptr<::cw::lib::Window> arg_event) { (void)arg_event; _on_show(0); }
void CardHolder::OnShowItem(std::shared_ptr<::cw::lib::Window> arg_event) { (void)arg_event; _on_show(1); }
void CardHolder::OnShowBeast(std::shared_ptr<::cw::lib::Window> arg_event) { (void)arg_event; _on_show(2); }
void CardHolder::_on_show(::cw::lib::Integer)
{
    throw std::invalid_argument("CardHolder::_on_show requires a concrete selector");
}
void CardHolder::OnClickLeftBtn(std::shared_ptr<::cw::lib::Window> arg_event)
{
    m__cancel_animation = true;
    (void)arg_event;
    if (::cw::g_cwpy) ::cw::g_cwpy->play_sound("page", false, 100, 1, 0, 0, false);
    const auto old = m_callname;
#if defined(CARDWIRTH_LOG_ENABLED)
    log_cardcontrol("CardHolder OnClickLeftBtn from=" + old);
#endif
    if ((m_callname == "CARDPOCKET" || m_callname == "CARDPOCKETB") && !m_list2.empty()) {
        auto it = std::find_if(m_list2.begin(), m_list2.end(), [this](const auto& v) { return same_value(v, m_index2); });
        const bool first = it == m_list2.begin() || it == m_list2.end();
        if (first && m_can_open_backpack) m_callname = "BACKPACK";
        else {
            m_callname = "CARDPOCKET";
            if (first) it = m_list2.end();
            m_index2 = it == m_list2.end() ? m_list2.back() : *std::prev(it);
            m_selection = m_index2;
            // Python と同様、同じ CARDPOCKET 表示内で
            // 別のメンバーへ移動したら、フレームの選択を直ちに更新する。
            change_parent_selection(m_selection);
        }
        _change_callname(old);
#if defined(CARDWIRTH_LOG_ENABLED)
        log_cardcontrol("CardHolder OnClickLeftBtn to=" + m_callname);
#endif
        return;
    }
    if (m_callname == "BACKPACK" && m_can_open_storehouse) m_callname = "STOREHOUSE";
    else {
        m_callname = "CARDPOCKET";
        if (!m_list2.empty()) { m_index2 = m_list2.back(); m_selection = m_index2; }
        change_parent_selection(m_selection);
    }
    _change_callname(old);
#if defined(CARDWIRTH_LOG_ENABLED)
    log_cardcontrol("CardHolder OnClickLeftBtn to=" + m_callname);
#endif
}
void CardHolder::OnClickRightBtn(std::shared_ptr<::cw::lib::Window> arg_event)
{
    m__cancel_animation = true;
    (void)arg_event;
    if (::cw::g_cwpy) ::cw::g_cwpy->play_sound("page", false, 100, 1, 0, 0, false);
    const auto old = m_callname;
#if defined(CARDWIRTH_LOG_ENABLED)
    log_cardcontrol("CardHolder OnClickRightBtn from=" + old);
#endif
    if ((m_callname == "CARDPOCKET" || m_callname == "CARDPOCKETB") && !m_list2.empty()) {
        auto it = std::find_if(m_list2.begin(), m_list2.end(), [this](const auto& v) { return same_value(v, m_index2); });
        const bool last = it == m_list2.end() || std::next(it) == m_list2.end();
        if (last && m_can_open_storehouse) m_callname = "STOREHOUSE";
        else if (last && m_can_open_backpack) m_callname = "BACKPACK";
        else {
            m_callname = "CARDPOCKET";
            if (it == m_list2.end() || std::next(it) == m_list2.end()) it = m_list2.begin(); else ++it;
            m_index2 = *it;
            m_selection = m_index2;
            change_parent_selection(m_selection);
        }
        _change_callname(old);
#if defined(CARDWIRTH_LOG_ENABLED)
        log_cardcontrol("CardHolder OnClickRightBtn to=" + m_callname);
#endif
        return;
    }
    if (m_callname == "STOREHOUSE") m_callname = "BACKPACK";
    else {
        m_callname = "CARDPOCKET";
        if (!m_list2.empty()) { m_index2 = m_list2.front(); m_selection = m_index2; }
        change_parent_selection(m_selection);
    }
    _change_callname(old);
#if defined(CARDWIRTH_LOG_ENABLED)
    log_cardcontrol("CardHolder OnClickRightBtn to=" + m_callname);
#endif
}
void CardHolder::OnClose(std::shared_ptr<::cw::lib::Window> arg_event)
{
#if defined(CARDWIRTH_LOG_ENABLED)
    log_cardcontrol("CardHolder OnClose callname=" + m_callname);
#endif
    _clear_flags(); m_list.clear(); m_list2.clear(); CardControl::OnCancel(std::move(arg_event));
}
void CardHolder::OnCancel(std::shared_ptr<::cw::lib::Window> arg_event)
{
#if defined(CARDWIRTH_LOG_ENABLED)
    log_cardcontrol("CardHolder OnCancel callname=" + m_callname + " quit=" + (m__quit ? "1" : "0"));
#endif
    _clear_flags();
    if (m_callname == "CARDPOCKETB") {
        if (::cw::g_cwpy) ::cw::g_cwpy->play_sound("page", false, 100, 1, 0, 0, false);
        m_callname = "CARDPOCKET";
        CardControl::m_callname = m_callname;
        _change_callname("CARDPOCKETB");
        draw_cards(true, -1);
    } else {
        m_list.clear(); m_list2.clear();
        CardControl::OnCancel(std::move(arg_event));
    }
}
void CardHolder::_clear_flags()
{
    m__cancel_animation = true;
    // Python の _fulllist は CardHeader または InfoCardHeader
    // を格納する汎用一覧である。 C++ 側が二つの型を variant
    // で保持することを理由に、情報カードの選択・クリック状態を残さない。
    for (const auto& value : m__fulllist) {
        if (const auto item = value_to_header(value)) {
            std::visit([](const auto& header) {
                if (!header) return;
                header->m_negaflag = false;
                header->m_clickedflag = false;
            }, *item);
        }
    }
}
// Py と同様、呼び出し名による再構築は具体的な選択クラスで実装する。
void CardHolder::_change_callname(::cw::lib::String)
{
    throw std::invalid_argument("CardHolder::_change_callname requires a concrete selector");
}
void CardHolder::_update_cardpocketinfo(::cw::lib::Integer arg_pocket, bool arg_force)
{
    if (!m_can_open_cardpocket) return;
    if (!arg_force && !m__cardpocketinfo.is_none() && int_value(m__cardpocketinfo) == arg_pocket) return;
    m__cardpocketinfo = ::cw::lib::Value(arg_pocket);
    const bool normal_pocket = 0 <= arg_pocket && arg_pocket <= 2;
    const auto pocket = std::clamp<::cw::lib::Integer>(arg_pocket, 0, 2);
    auto update_label = [this](::cw::lib::Integer combo_index, const ::cw::lib::String& label) {
        if (combo_index >= 0 && static_cast<std::size_t>(combo_index) < m_sendto_labels.size())
            m_sendto_labels[static_cast<std::size_t>(combo_index)] = label;
    };
    for (const auto& [key, raw] : m__combo_cast) {
        const auto combo_index = std::atoi(key.c_str());
        const auto raw_index = int_value(raw, -1);
        const auto player = raw_index >= 0 && static_cast<std::size_t>(raw_index) < m_list2.size()
            ? object_value<::cw::character::Player>(m_list2[static_cast<std::size_t>(raw_index)])
            : std::shared_ptr<::cw::character::Player>{};
        if (!player) continue;
        auto label = player->get_showingname();
        if (normal_pocket) {
            const auto& cards = player->m_cardpocket_typed;
            const auto spaces = player->get_cardpocketspace();
            const auto count = pocket == 0 ? std::get<0>(cards).size() : pocket == 1 ? std::get<1>(cards).size() : std::get<2>(cards).size();
            const auto capacity = pocket == 0 ? std::get<0>(spaces) : pocket == 1 ? std::get<1>(spaces) : std::get<2>(spaces);
            label += "(" + std::to_string(count) + "/" + std::to_string(capacity) + ")";
        }
        update_label(combo_index, label);
    }
    for (const auto& [key, raw] : m__combo_personal) {
        const auto combo_index = std::atoi(key.c_str());
        const auto raw_index = int_value(raw, -1);
        const auto player = raw_index >= 0 && static_cast<std::size_t>(raw_index) < m_list2.size()
            ? object_value<::cw::character::Player>(m_list2[static_cast<std::size_t>(raw_index)])
            : std::shared_ptr<::cw::character::Player>{};
        if (!player) continue;
        auto label = cardcontrol_msg("send_to_personal", "%sの私有");
        if (const auto marker = label.find("%s"); marker != ::cw::lib::String::npos) label.replace(marker, 2, player->get_showingname());
        label += "(" + std::to_string(player->m_personal_pocket.size()) + "/" + std::to_string(player->get_personalpocketspace()) + ")";
        update_label(combo_index, label);
    }
}
void CardHolder::update_cardpocketinfo_with(std::optional<::cw::lib::Value> arg_header) { if (!m_can_open_cardpocket || m_callname == "CARDPOCKET" || !arg_header) return; if (const auto h = object_value<CardPtr::element_type>(*arg_header)) _update_cardpocketinfo(card_type_index(h), false); }
void CardHolder::_enable_updown() { m_page_count = std::max<::cw::lib::Integer>(1, (static_cast<::cw::lib::Integer>(m_typed_list.size()) + 9) / 10); m_index = std::clamp<::cw::lib::Integer>(m_index, 0, m_page_count - 1); const bool enabled = m_callname != "CARDPOCKET" && m_typed_list.size() > 10; m_upbtn = ::cw::lib::Value(enabled); m_downbtn = ::cw::lib::Value(enabled); }
void CardHolder::_show_controls()
{
    m_sort_enabled = m_callname == "BACKPACK" || m_callname == "STOREHOUSE" ||
                     m_callname == "CARDPOCKETB";
    if (m_sort_enabled && ::cw::g_cwpy && ::cw::g_cwpy->m_setting) {
        const auto& sort_names = card_sort_names();
        const auto found = std::find(sort_names.begin(), sort_names.end(),
                                     ::cw::g_cwpy->m_setting->m_sort_cards);
        m_sort_selection = found == sort_names.end() ? 0
            : static_cast<::cw::lib::Integer>(std::distance(sort_names.begin(), found));
        m_sort = ::cw::lib::Value(m_sort_selection);
    }
    _enable_updown();
}
void CardHolder::_update_page()
{
    _clamp_page_index();
    _store_index();
    m_page = ::cw::lib::Value(m_index + 1);
}
// Py と同様、この基底フックは具体的な選択クラスの実装を必要とする。
void CardHolder::_set_backpacklist(bool)
{
    throw std::invalid_argument("CardHolder::_set_backpacklist requires a concrete selector");
}
void CardHolder::lclick_event(::cw::lib::Value arg_header) { CardControl::lclick_event(std::move(arg_header)); }
void CardHolder::refresh_after_transfer()
{
    if (m__quit) return;
    // ネイティブ版では移動完了処理が処理中フラグを所有する。Python のロ
    // ーカルなコールバック状態と異なり、 一覧再構築に失敗しても明示的に解除す
    // るまで残るため、元の例外を保ちつつ、カード入力を操作不能のままにしない。
    struct ReleaseBusy {
        CardHolder& control;
        ~ReleaseBusy() { control.m__proc = false; }
    } release_busy{*this};
    // ゲームスレッドでの取引完了後にだけ、現在のモデルからスナップシ
    // ョットを再構築する。 現在の絞り込みと選択ポケットは維持する。
    if (::cw::g_cwpy) {
        const auto status = trade_area(CardControl::m_areaid)
            ? ::cw::lib::Value("unreversed") : ::cw::lib::Value("active");
        _init_list(status);
        if (m_callname == "CARDPOCKET") _init_cardpocketlist();
        m__fulllist = m_list;
        m_card_full_list = cards_from_values(m_list);
        _build_sendto_combo();
    }
    m__proc = false;
    update_narrowcondition();
    OnLeave(nullptr);
    request_dialog_presentation_refresh();
}

bool CardHolder::show_replacement(std::shared_ptr<::cw::sprite::card::PlayerCard> target,
                                  std::shared_ptr<::cw::header::CardHeader> card, bool personal)
{
    if (m__quit || m__proc || !target || !card) return false;
    const auto frame = ::cw::g_cwpy
        ? object_value<::cw::frame::Frame>(::cw::g_cwpy->m_frame) : nullptr;
    const auto owner = frame ? std::dynamic_pointer_cast<CardHolder>(frame->dialog_presentation_model()) : nullptr;
    auto child = std::make_shared<ReplCardHolder>();
    child->initialize(m_parent);
    child->m_parent_holder = this;
    m__replcardholder = child;
    m__proc = true;
    try {
        child->reconstruct_replcardholder(std::move(target), std::move(card), personal);
        // Python の ShowModal/Destroy と同様、親はそのまま保ち、共通ホス
        // トが覆い隠して復元する。 親のバックエンドを閉じると、そのキューまで破棄されてしまう。
        const bool shown = ::cw::dialog::show_common_modal_presentation(child);
        if (!shown && !child->m__quit) child->OnCancel(nullptr);
        child->m_parent_holder = nullptr;
        m__replcardholder.reset();
    } catch (...) {
        child->m_parent_holder = nullptr;
        m__replcardholder.reset();
        m__proc = false;
        request_dialog_presentation_refresh();
        throw;
    }
    if (::cw::g_cwpy && frame && owner.get() == this) {
        const std::weak_ptr<CardHolder> weak_owner = owner;
        // ゲームスレッドで子の交換処理の後に予約し、完了後にだけ UI
        // のスナップショットを再構築する。 非同期の完了処理をまたいで所
        // 有先の生ポインターを保持したり、閉じた親を復活させたりしない。
        try {
            ::cw::g_cwpy->exec_func([frame, weak_owner](const auto&, const auto&) -> ::cw::lib::Value {
                frame->exec_func([weak_owner](const auto&, const auto&) -> ::cw::lib::Value {
                    if (const auto holder = weak_owner.lock()) holder->refresh_after_transfer();
                    return {};
                });
                return {};
            });
        } catch (...) { refresh_after_transfer(); throw; }
    } else refresh_after_transfer();
    return true; // 子画面がキャンセルされた場合や利用できない場合は、取引へ進まない。
}
void CardHolder::OnClickToggleBtn(std::shared_ptr<::cw::lib::Window> arg_event)
{
    m__cancel_animation=true;
    if (::cw::g_cwpy) ::cw::g_cwpy->play_sound("click",false,100,1,0,0,false);
    if (arg_event) m_last_cardpocket=arg_event->id;
    if (::cw::g_cwpy && ::cw::g_cwpy->m_setting) ::cw::g_cwpy->m_setting->m_last_cardpocket=m_last_cardpocket;
    draw_cards(true,-1);
    _update_cardpocketinfo(m_last_cardpocket,false);
}

void CardHolder::OnUp(std::shared_ptr<::cw::lib::Window> event)
{
    if (m_callname=="CARDPOCKET") {
        m__cancel_animation=true;
        auto toggle=std::make_shared<::cw::lib::Window>();
        toggle->id=m_last_cardpocket==0 ? (is_showpersonal()?3:2) : m_last_cardpocket-1;
        OnClickToggleBtn(toggle);
    } else if (chain_enabled(::cw::dialog::DialogPresentationAction::Auxiliary,13)) {
        m__cancel_animation=true;OnClickUpBtn(event);
    }
}

void CardHolder::OnDown(std::shared_ptr<::cw::lib::Window> event)
{
    if (m_callname=="CARDPOCKET") {
        m__cancel_animation=true;
        auto toggle=std::make_shared<::cw::lib::Window>();
        toggle->id=m_last_cardpocket==(is_showpersonal()?3:2) ? 0 : m_last_cardpocket+1;
        OnClickToggleBtn(toggle);
    } else if (chain_enabled(::cw::dialog::DialogPresentationAction::Auxiliary,14)) {
        m__cancel_animation=true;OnClickDownBtn(event);
    }
}

void CardHolder::OnClickUpBtn(std::shared_ptr<::cw::lib::Window> event)
{
    (void)event;
    m__cancel_animation=true;
    if (::cw::g_cwpy) ::cw::g_cwpy->play_sound("click",false,100,1,0,0,false);

    std::optional<std::size_t> selected_slot;
    const auto before = get_headers();
    for (std::size_t slot = 0; slot < before.size(); ++slot) {
        if (const auto header = value_to_header(before[slot]))
            std::visit([&](const auto& item) {
                if (item && item->m_negaflag) selected_slot = slot;
                if (item) item->m_negaflag = false;
            }, *header);
    }
    _clamp_page_index();
    m_index = m_index <= 0 ? m_page_count - 1 : m_index - 1;
    const auto after = get_headers();
    if (selected_slot && *selected_slot < after.size())
        if (const auto header = value_to_header(after[*selected_slot]))
            std::visit([](const auto& item) { if (item) item->m_negaflag = true; }, *header);
    m__proc_page=true;m_proc_page=true;
    m_page=::cw::lib::Value(m_index+1);
    m__proc_page=false;m_proc_page=false;
    _on_pagenum(m_index+1);
    draw_cards(true, -1);
}

void CardHolder::OnClickDownBtn(std::shared_ptr<::cw::lib::Window> event)
{
    (void)event;
    m__cancel_animation=true;
    if (::cw::g_cwpy) ::cw::g_cwpy->play_sound("click",false,100,1,0,0,false);

    std::optional<std::size_t> selected_slot;
    const auto before = get_headers();
    for (std::size_t slot = 0; slot < before.size(); ++slot) {
        if (const auto header = value_to_header(before[slot]))
            std::visit([&](const auto& item) {
                if (item && item->m_negaflag) selected_slot = slot;
                if (item) item->m_negaflag = false;
            }, *header);
    }
    _clamp_page_index();
    m_index = (m_index + 1) % m_page_count;
    const auto after = get_headers();
    if (selected_slot && *selected_slot < after.size())
        if (const auto header = value_to_header(after[*selected_slot]))
            std::visit([](const auto& item) { if (item) item->m_negaflag = true; }, *header);
    m__proc_page=true;m_proc_page=true;
    m_page=::cw::lib::Value(m_index+1);
    m__proc_page=false;m_proc_page=false;
    _on_pagenum(m_index+1);
    draw_cards(true, -1);
}

void CardHolder::OnPageNum(std::shared_ptr<::cw::lib::Window> arg_event) {
    m__cancel_animation = true;
    if(!arg_event || m__quit || m__proc || m__proc_page || m_proc_page ||
       arg_event->id<1 || arg_event->id>m_page_count || arg_event->id==m_index+1) return;
    if(::cw::g_cwpy) ::cw::g_cwpy->play_sound("page",false, 100, 1, 0,0,false);
    _on_pagenum(arg_event->id);
}
void CardHolder::_on_pagenum(::cw::lib::Integer arg_page)
{
    m_index = std::max<::cw::lib::Integer>(0, arg_page - 1);
    _clamp_page_index();
    _update_page();
    draw_cards(true, -1);
}
void CardHolder::OnMouseWheel(std::shared_ptr<::cw::lib::Window> arg_event) {
    if(!arg_event || !arg_event->id || navigation_input_blocked()) return;
    if(arg_event->bounds.x<80 || m_callname=="INFOVIEW") {
        if(m_callname=="CARDPOCKET" || chain_enabled(::cw::dialog::DialogPresentationAction::Auxiliary,
                                                    arg_event->id<0?13:14)) {
            if(arg_event->id<0) OnUp(std::move(arg_event));else OnDown(std::move(arg_event));
            return;
        }
    }
    CardControl::OnMouseWheel(std::move(arg_event));
}
void CardHolder::draw_cards(bool arg_update, ::cw::lib::Integer arg_mode)
{
    // Python と同様、CARDPOCKET の描画直前に毎回
    // 、選択キャラクターのポケットを更新する。 左右のメンバーボタ
    // ンで選択を変えた後に更新しないと、m_typed_list
    // が以前のキャラクターを指したままになり、表示が変わらない。
    if (m_callname == "CARDPOCKET") {
        if (const auto owner = character_value(m_selection)) {
            _init_cardpocketlist();
            if (::cw::g_cwpy && ::cw::g_cwpy->m_setting && ::cw::g_cwpy->m_setting->m_msgs) {
                const auto title = ::cw::lib::python_percent_string(
                    ::cw::g_cwpy->m_setting->m_msgs->get("cards_hand", "%sの手札カード"), owner->get_showingname());
                m_title = ::cw::g_cwpy->m_setting->m_msgs->get("card_control", "カード操作") + " - " + title;
            }
        }
    }
    // 選択した所有者のカードを再構築してから、ページを範囲内に収める。 現在の PC
    // が 50 枚持っていても、前の表示はページ数が少ないか空だった可能性がある。
    _clamp_page_index();
    _store_index();
    // Python は再描画のたびに _leftmarks を置き換える。 新しく
    // 選択した所有先の画像を加える前に、基底クラスのビットマップ一覧を消去する。
    CardControl::m__leftmarks.clear();
    if (m_callname == "BACKPACK") {
        if (auto mark = load_leftmark_bitmap("Resource/Image/Card/COMMAND7")) CardControl::m__leftmarks.push_back(std::move(mark));
    } else if (m_callname == "STOREHOUSE") {
        if (auto mark = load_leftmark_bitmap("Resource/Image/Card/COMMAND5")) CardControl::m__leftmarks.push_back(std::move(mark));
    } else if (m_callname == "INFOVIEW") {
        if (auto mark = load_leftmark_bitmap("Resource/Image/Card/COMMAND8")) CardControl::m__leftmarks.push_back(std::move(mark));
    } else if (m_callname == "CARDPOCKETB" && ::cw::g_cwpy && ::cw::g_cwpy->m_rsrc) {
        // Py と同様、一時的な荷物袋の印には ItemCard
        // の全画像パスを保存順に使う。 COMMAND7 で代用
        // せず、他の所有先の印と同じローダーで拡張子を解決する。
        const auto& cards = ::cw::g_cwpy->m_rsrc->m_backpackcards;
        const auto item = cards.find("ItemCard");
        if (item != cards.end() && item->second) {
            const auto images = object_value<::cw::data::VariantSequence>(item->second->m_imgpaths);
            if (images) for (const auto& value : images->m_values) {
                if (const auto info = object_value<::cw::image::ImageInfo>(value)) {
                    const auto path = std::get<0>(::cw::util::splitext(info->m_path));
                    if (auto mark = load_leftmark_bitmap(path))
                        CardControl::m__leftmarks.push_back(std::move(mark));
                }
            }
        }
    }
    CardControl::m_list = values_headers(m_typed_list);
    CardControl::m_index = m_index;
    CardControl::draw_cards(arg_update, arg_mode);
}
// Py の Holder はこのフックを具体的な選択クラスに委ねるが、ネイティブ版は
// ここで型付きポケットのスナップショットをコピーする。 SelectCard が個人
// ポケットの権限と荷物袋の疑似カードの規則を追加する。所有者がない場合は、 抽象基
// 底の ValueError フックを実行せず、現在のスナップショットを維持する。
void CardHolder::_init_cardpocketlist()
{
    if (m_callname != "CARDPOCKET") return;
    const auto owner = character_value(m_selection);
    if (!owner) return;
    if (m_last_cardpocket == 3) {
        if (auto player = std::dynamic_pointer_cast<::cw::character::Player>(owner)) m_list = card_values(player->m_personal_pocket);
        else m_list.clear();
    } else {
        const auto& p = owner->m_cardpocket_typed;
        const auto& source = m_last_cardpocket == 1 ? std::get<1>(p) : m_last_cardpocket == 2 ? std::get<2>(p) : std::get<0>(p);
        m_list = card_values(source);
    }
    m_card_full_list = cards_from_values(m_list);
    m__fulllist = m_list;
    m_typed_full_list = typed_headers(m_list);
    m_typed_list = m_typed_full_list;
    if (::cw::g_cwpy && ::cw::g_cwpy->m_setting) ::cw::g_cwpy->m_setting->m_last_cardpocket = m_last_cardpocket;
}
std::vector<::cw::lib::Value> CardHolder::get_headers() { _clamp_page_index(); return page_values(m_typed_list, m_index, m_page_size); }
bool CardHolder::is_showpersonal() { return m_show_personal && m_environment_backpack && (m_callname == "BACKPACK" || m_callname == "STOREHOUSE" || object_value<::cw::character::Player>(m_selection)); }
std::vector<::cw::lib::Value> CardHolder::get_beforepageheaders() { _clamp_page_index(); return page_values(m_typed_list, m_index - 1, m_page_size); }
std::vector<::cw::lib::Value> CardHolder::_narrow(std::vector<::cw::lib::Value> arg_clist)
{
    m__fulllist = arg_clist; m_typed_full_list = typed_headers(arg_clist); m_typed_list.clear();
    if (m_callname != "STOREHOUSE" && m_callname != "BACKPACK" && m_callname != "CARDPOCKETB" && m_callname != "INFOVIEW") {
        m_typed_list = m_typed_full_list;
        _clamp_page_index();
        return values_headers(m_typed_list);
    }
    if (m_narrow_text.empty()) m_narrow_text = string_value(m_narrow);
    m_narrow_selection = int_value(m_narrow_type, m_narrow_selection);
    // Python の CardControl と同様、検索文字列と各項目の双方に str.lower()
    // の規則を使う。 ASCII だけでなく、非 ASCII のカード名やキーコードも検索できるようにする。
    const auto needle = ::cw::lib::encoding::python_lower(m_narrow_text);
    const bool filter = _can_narrow() && m_show_additional_controls;
    const bool all_types = (m_callname != "STOREHOUSE" && m_callname != "BACKPACK") ||
        std::all_of(m_show_cardtype.begin(), m_show_cardtype.end(), [](bool shown) { return shown; });
    // Py の絞り込みなしの高速経路では、BACKPACK の場合だけ個人の
    // 所有カードを除く。 STOREHOUSE などの表示では、ここでは入力
    // 一覧全体を維持する。下の絞り込み経路では通常の所有者条件を適用する。
    if ((!filter || needle.empty()) && all_types &&
        !(m_callname == "BACKPACK" && is_showpersonal())) {
        m_typed_list = m_typed_full_list;
        _clamp_page_index();
        return values_headers(m_typed_list);
    }
    const bool info_view = m_callname == "INFOVIEW";
    const bool debug_keycode = ::cw::g_cwpy && ::cw::g_cwpy->is_debugmode() && !info_view;
    const bool all_selected = m_narrow_selection == 0;
    const bool name_selected = m_narrow_selection == 1;
    const bool desc_selected = m_narrow_selection == 2;
    const bool scenario_selected = m_narrow_selection == 3;
    const bool author_selected = m_narrow_selection == 4;
    const bool keycode_selected = debug_keycode && m_narrow_selection == 5;
    for (const auto& item : m_typed_full_list) {
        if (const auto card = std::get_if<CardPtr>(&item)) {
            if (!*card) continue;
            if ((m_callname == "BACKPACK" || m_callname == "STOREHOUSE") &&
                !card_type_visible(*card, m_show_cardtype)) continue;
            if (is_showpersonal() && m_callname != "CARDPOCKET" && (*card)->m_personal_owner.lock()) continue;
            bool key = false;
            for (const auto& k : (*card)->m_keycodes)
                key = key || ::cw::lib::encoding::python_lower(k).find(needle) != ::cw::lib::String::npos;
            bool match = !filter || needle.empty();
            if (!match && info_view) {
                if (all_selected)
                    match = ::cw::lib::encoding::python_lower(field(item, 1)).find(needle) != ::cw::lib::String::npos ||
                            ::cw::lib::encoding::python_lower(field(item, 2)).find(needle) != ::cw::lib::String::npos;
                else if (name_selected)
                    match = ::cw::lib::encoding::python_lower(field(item, 1)).find(needle) != ::cw::lib::String::npos;
                else if (desc_selected)
                    match = ::cw::lib::encoding::python_lower(field(item, 2)).find(needle) != ::cw::lib::String::npos;
            } else if (!match) {
                if (keycode_selected)
                    match = key || ::cw::lib::encoding::python_lower((*card)->m_name).find(needle) != ::cw::lib::String::npos;
                else if (all_selected)
                    match = matches(item, needle, 0) || (debug_keycode && key);
                else if (name_selected)
                    match = matches(item, needle, 1);
                else if (desc_selected)
                    match = matches(item, needle, 2);
                else if (scenario_selected)
                    match = matches(item, needle, 3);
                else if (author_selected)
                    match = matches(item, needle, 4);
            }
            if (match) m_typed_list.push_back(item);
        } else if (!filter || needle.empty()) {
            m_typed_list.push_back(item);
        } else if (info_view) {
            bool match = false;
            if (all_selected)
                match = ::cw::lib::encoding::python_lower(field(item, 1)).find(needle) != ::cw::lib::String::npos ||
                        ::cw::lib::encoding::python_lower(field(item, 2)).find(needle) != ::cw::lib::String::npos;
            else if (name_selected)
                match = ::cw::lib::encoding::python_lower(field(item, 1)).find(needle) != ::cw::lib::String::npos;
            else if (desc_selected)
                match = ::cw::lib::encoding::python_lower(field(item, 2)).find(needle) != ::cw::lib::String::npos;
            if (match) m_typed_list.push_back(item);
        }
    }
    _clamp_page_index(); return values_headers(m_typed_list);
}
namespace {
// 順序エディターを開き、OK で確定したら、手動のカード交換と同じ入れ替え操作
// で新しい順序を反映する。 実行中のポケットと保存 XML の双方を更新する。
void open_pocket_reorder(CardHolder& holder)
{
    const auto pocket = holder.m_last_cardpocket;
    const auto headers = reorder_pocket_headers(holder, pocket);
    if (headers.size() < 11) return;
    if (::cw::g_cwpy) ::cw::g_cwpy->play_sound("click", false, 100, 1, 0, 0, false);
    std::vector<::cw::dialog::cardreorder::ReorderCard> cards;
    for (const auto& header : headers) {
        if (!header) continue;
        cards.push_back({header, surface_from_card_wxbmp(header->get_cardwxbmp(std::nullopt))});
    }
    auto dialog = std::make_shared<::cw::dialog::cardreorder::CardReorderDialog>();
    dialog->initialize(std::move(cards), cardcontrol_body_colour(holder), cardcontrol_ui_scale());
    if (!::cw::dialog::show_common_modal_presentation(dialog) || !dialog->accepted()) return;
    const auto player = object_value<::cw::character::Player>(holder.m_selection);
    if (!player) return;
    const auto wanted = dialog->order();
    // 入れ替えによる選択ソート。手順 i の終了時に、位置 i が wanted[i] を持つ。
    for (std::size_t i = 0; i < wanted.size(); ++i) {
        const auto current = reorder_pocket_headers(holder, pocket);
        if (i >= current.size() || current[i] == wanted[i]) continue;
        if (std::find(current.begin(), current.end(), wanted[i]) == current.end()) continue;
        if (pocket == 3) player->replace_personalcardposition(current[i], wanted[i]);
        else player->replace_cardposition(pocket, current[i], wanted[i]);
    }
    holder._init_cardpocketlist();
    holder.update_narrowcondition();
}
}  // namespace

void CardHolder::_clamp_page_index() { m_page_size = std::max<::cw::lib::Integer>(1, m_page_size); m_page_count = std::max<::cw::lib::Integer>(1, (static_cast<::cw::lib::Integer>(m_typed_list.size()) + m_page_size - 1) / m_page_size); m_index = std::clamp<::cw::lib::Integer>(m_index, 0, m_page_count - 1); }
void CardHolder::update_narrowcondition() { _update_sortattr(false); _narrow(m__fulllist.empty() ? m_list : m__fulllist); _update_page(); draw_cards(true, -1); }
void CardHolder::_replace_position_impl(::cw::lib::Value arg_header1, ::cw::lib::Value arg_header2)
{
    const auto a = object_value<CardPtr::element_type>(arg_header1), b = object_value<CardPtr::element_type>(arg_header2);
    if (!a || !b) return;
    if (m_callname == "CARDPOCKET") {
        if (auto player = object_value<::cw::character::Player>(m_selection)) {
            if (m_last_cardpocket == 3) player->replace_personalcardposition(a, b);
            else player->replace_cardposition(m_last_cardpocket, a, b);
            _init_cardpocketlist();
            CardControl::m_list = values_headers(m_typed_list);
            return;
        }
        throw std::runtime_error("Pocket reorder requires a player");
    }
    if (!::cw::g_cwpy || !::cw::g_cwpy->m_ydata || !*::cw::g_cwpy->m_ydata)
        throw std::runtime_error("Reorder requires inn data");
    auto& ydata = **::cw::g_cwpy->m_ydata;
    std::vector<CardPtr>* source = nullptr;
    if (m_callname == "STOREHOUSE") source = &ydata.m_storehouse;
    else if ((m_callname == "BACKPACK" || m_callname == "CARDPOCKETB") && ydata.m_party && *ydata.m_party)
        source = &(**ydata.m_party).m_backpack;
    if (!source) throw std::runtime_error("Invalid reorder holder");
    const auto i = std::find(source->begin(), source->end(), a), j = std::find(source->begin(), source->end(), b);
    if (i == source->end() || j == source->end()) throw std::invalid_argument("Reorder card is not in its container");
    // ネイティブの一覧は値のコピーだが、Py は実体のコンテナーを参
    // 照し得る。 正本のコンテナーは一度だけ入れ替える。表示用コピー
    // とヘッダーの表示・順序は、共通の呼び出し側で交換済みである。
    std::iter_swap(i, j);
    auto list = cards_from_values(m__fulllist.empty() ? m_list : m__fulllist);
    const auto li = std::find(list.begin(), list.end(), a), lj = std::find(list.begin(), list.end(), b);
    if (li != list.end() && lj != list.end()) std::iter_swap(li, lj);
    m__fulllist = card_values(list);
    m_card_full_list = list;
    _narrow(m__fulllist);
    m_list = values_headers(m_typed_list);
    CardControl::m_list = m_list;
}
void HandView::initialize(std::shared_ptr<::cw::lib::Window> arg_parent) { CardControl::initialize(std::move(arg_parent)); m_callname = "HANDVIEW"; CardControl::m_callname = m_callname; }
void HandView::reconstruct_handview(std::shared_ptr<::cw::sprite::card::CWPyCard> arg_selection, std::optional<std::tuple<::cw::lib::String, std::shared_ptr<::cw::sprite::card::CWPyCard>, std::tuple<::cw::lib::Integer, ::cw::lib::Integer>, ::cw::lib::Real>> arg_pre_info)
{
    m_owner = object_value(arg_selection);
    const auto selected=character_value(m_owner);
    if (!selected) throw std::invalid_argument("Hand view requires a character");
    _update_cardlist(selected);
    m_selection = object_value(arg_selection);
    m_index2 = m_selection;
    m_index = 0;
    if (arg_pre_info) {
        m_pre_pos = std::get<2>(*arg_pre_info);
        m_index2 = object_value(std::get<1>(*arg_pre_info));
        m_selection = m_index2;
        if (::cw::g_UP_WIN != std::get<3>(*arg_pre_info)) m_pre_pos.reset();
    } else {
        m_pre_pos.reset();
        if (::cw::g_cwpy && ::cw::g_cwpy->m_setting) ::cw::g_cwpy->m_setting->m_card_narrow.clear();
    }
    const auto restored = character_value(m_selection);
    if (!restored || !restored->m_deck) throw std::invalid_argument("Hand view requires a deck");
    m_card_list = restored->m_deck->m_hand;
    for (const auto& header : m_card_list) if (header) { header->m_negaflag = false; header->m_clickedflag = false; }
    CardControl::reconstruct("HANDVIEW", "HANDVIEW", false, false, std::nullopt);
    // Python の HandView はフレームが所有するため、開く
    // 際は最初の手札描画より前に選択キャラクターのスプライトも更新する。
    change_parent_selection(m_selection);
}
void HandView::_do_layout() { CardControl::_do_layout(); if (m_parent) m_parent->bounds.height = std::max<::cw::lib::Integer>(m_parent->bounds.height, 360); }
void HandView::_bind() { CardControl::_bind(); }
void HandView::update_debug()
{
    // このオーバーライドは基底メソッドより先に終了・絞り込みを判断するため、
    // まず反映直後の Engine の値を読む。 そうしないと、デバッグ切
    // り替えを前の UI スナップショットで解釈し、逆の結果にしてしまう。
    if (::cw::g_cwpy) m_cwpy_debug = ::cw::g_cwpy->is_debugmode();
    const auto selection = character_value(m_selection);
    if (!m_cwpy_debug && selection && (object_value<::cw::character::Friend>(m_selection) || selection->is_autoselectedpenalty(std::nullopt))) {
        OnCancel(nullptr);
        return;
    }
    _update_cardlist(selection);
    CardControl::update_debug();
}
void HandView::_update_cardlist(std::shared_ptr<::cw::character::Character> arg_selection)
{
    m_card_list.clear();
    m_list2.clear();
    if (!arg_selection || !::cw::g_cwpy) return;
    if (std::dynamic_pointer_cast<::cw::character::Player>(arg_selection)) {
        for (const auto& card : ::cw::g_cwpy->get_pcards(::cw::lib::Value("active"))) m_list2.push_back(object_value(card));
    } else if (std::dynamic_pointer_cast<::cw::character::Friend>(arg_selection)) {
        for (const auto& card : ::cw::g_cwpy->get_fcards(::cw::lib::Value("active"))) m_list2.push_back(object_value(card));
        if (::cw::g_cwpy->m_sct) if (auto scenario = object_value<::cw::data::ScenarioData>(::cw::g_cwpy->m_sdata); scenario && ::cw::g_cwpy->m_sct->lessthan("1.30", scenario->get_versionhint(0))) std::reverse(m_list2.begin(), m_list2.end());
    } else if (auto enemy = std::dynamic_pointer_cast<::cw::character::Enemy>(arg_selection)) {
        if (!_update_enemylist(enemy)) return;
    }
    // Python はここで cwpy.is_debugmode() を確認する。初回の再構
    // 築では、CardControl::reconstruct がまだ その状態を Presentation
    // モデルへコピーしていないため、UI のキャッシュ値は古い。 キャッシュ値で
    // 判断すると、初めて開いた時にデバッグ専用のペナルティ手札が消える場合がある。
    const bool debug = ::cw::g_cwpy->is_debugmode();
    // Python と同様、この除外は PlayerCard 用に開いた HandView
    // だけに適用する。 同行者・敵の切り替えには、それぞれの表示規則を使う。
    if (!debug && object_value<::cw::sprite::card::PlayerCard>(m_owner)) {
        m_list2.erase(std::remove_if(m_list2.begin(), m_list2.end(), [](const auto& value) {
            const auto owner = character_value(value);
            return owner && owner->is_autoselectedpenalty(std::nullopt);
        }), m_list2.end());
    }
    m_list2.erase(std::remove_if(m_list2.begin(), m_list2.end(), [](const auto& value) {
        const auto owner = character_value(value);
        return !owner || !owner->m_deck || owner->m_deck->m_hand.empty();
    }), m_list2.end());
    if (arg_selection->m_deck) m_card_list = arg_selection->m_deck->m_hand;
}
bool HandView::_update_enemylist(std::shared_ptr<::cw::character::Enemy> arg_selection)
{
    auto selected_card = std::dynamic_pointer_cast<::cw::sprite::card::EnemyCard>(arg_selection);
    if (!selected_card || !::cw::g_cwpy) return true;

    m_list2.clear();
    for (const auto& card : ::cw::g_cwpy->get_ecards(::cw::lib::Value("active"))) {
        if (::cw::g_cwpy->is_debugmode() || (card && card->is_analyzable()))
            m_list2.emplace_back(object_value(card));
    }
    const auto selected = object_value(selected_card);
    const bool found = std::any_of(m_list2.begin(), m_list2.end(), [&selected](const auto& value) {
        return same_value(value, selected);
    });
    if (!found) {
        OnCancel(nullptr);
        return false;
    }
    return true;
}
void HandView::OnReDeal(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    m__cancel_animation = true;
    const auto owner = character_value(m_selection);
    if (!owner || !owner->m_deck || owner->is_inactive()) {
        if (::cw::g_cwpy) ::cw::g_cwpy->play_sound("error", false, 100, 1, 0, 0, false);
        return;
    }
    if (::cw::g_cwpy) ::cw::g_cwpy->play_sound("dump", false, 100, 1, 0, 0, false);
    owner->m_deck->throwaway();
    owner->m_deck->draw(owner);
    m_card_list = owner->m_deck->m_hand;
    for (const auto& header : m_card_list) if (header) header->m_negaflag = false;
    draw_cards(true, 3);
}
void HandView::OnClickLeftBtn(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    if (::cw::g_cwpy) ::cw::g_cwpy->play_sound("page", false, 100, 1, 0, 0, false);
    if (m_list2.empty()) return;
    auto i = std::find_if(m_list2.begin(), m_list2.end(),
                          [this](const auto& v) { return same_value(v, m_index2); });
    m_index2 = i == m_list2.begin() || i == m_list2.end() ? m_list2.back() : *std::prev(i);
    m_selection = m_index2;
    change_parent_selection(m_selection);
    _update_cardlist(character_value(m_selection));
    draw_cards(true, 3);
}
void HandView::OnClickRightBtn(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    if (::cw::g_cwpy) ::cw::g_cwpy->play_sound("page", false, 100, 1, 0, 0, false);
    if (m_list2.empty()) return;
    auto i = std::find_if(m_list2.begin(), m_list2.end(),
                          [this](const auto& v) { return same_value(v, m_index2); });
    if (i == m_list2.end() || ++i == m_list2.end()) i = m_list2.begin();
    m_index2 = *i;
    m_selection = m_index2;
    change_parent_selection(m_selection);
    _update_cardlist(character_value(m_selection));
    draw_cards(true, 3);
}
void HandView::draw_cards(bool arg_update, ::cw::lib::Integer arg_mode)
{
    if (const auto owner = character_value(m_selection)) {
        if (owner->m_deck) m_card_list = owner->m_deck->m_hand;
        const auto name = ::cw::lib::python_percent_string(
            cardcontrol_msg("cards_hand", "%sの手札"), owner->get_showingname());
        m_title = cardcontrol_msg("card_control", "カード操作") + " - " + name;
    }
    CardControl::m_list = card_values(m_card_list);
    CardControl::m_index = 0;
    CardControl::draw_cards(arg_update, arg_mode < 0 ? 3 : arg_mode);
}
std::vector<::cw::lib::Value> HandView::get_headers() { return card_values(m_card_list); }
void HandView::_draw_additionals(std::shared_ptr<::cw::lib::DeviceContext> arg_dc) { CardControl::_draw_additionals(std::move(arg_dc)); }

void InfoView::initialize(std::shared_ptr<::cw::lib::Window> arg_parent) { CardHolder::initialize(std::move(arg_parent), "INFOVIEW"); }
void InfoView::_init_list(::cw::lib::Value arg_status) { initialize_inventory_list(std::move(arg_status)); }
void InfoView::reconstruct_infoview() { reconstruct_cardholder("INFOVIEW", std::nullopt, std::nullopt, std::nullopt); }
bool InfoView::is_showpersonal() { return false; }
bool InfoView::_update_sortattr(bool arg_draw) { if (arg_draw) draw_cards(true, 1); return false; }
void InfoView::OnLeftUp(std::shared_ptr<::cw::lib::Window> arg_event) { /* 情報カードは左クリックを情報表示として扱う。 */ OnRightUp(std::move(arg_event)); }

void ReplCardHolder::initialize(std::shared_ptr<::cw::lib::Window> arg_parent) { CardControl::initialize(std::move(arg_parent)); m_callname = "CARDPOCKET_REPLACE"; }
bool ReplCardHolder::needs_exchange(const std::shared_ptr<::cw::sprite::card::PlayerCard>& target,
                                   const std::shared_ptr<::cw::header::CardHeader>& card, bool personal)
{
    if (!target || !card) return false;
    if (personal) {
        // Python と同様、ポケットが満杯であることと所有者が異なることの両方を必要とする。
        return static_cast<::cw::lib::Integer>(target->m_personal_pocket.size()) >= target->get_personalpocketspace() &&
            (!card->m_personal_owner.lock() || card->m_personal_owner.lock() != target);
    }
    const auto type = card_type_index(card);
    if (type < 0) return false;
    const auto owner = card->get_owner();
    if (owner && object_value<::cw::character::Player>(*owner) == target) return false;
    // load_cardpocket_from_xml() は XML の読み込み処理であり、Python の現在の cardpocket
    // プロパティとは異なる。 表示や当たり判定で呼ぶと、同一性の異なるヘッダーを新たに生成してしまう。
    const auto& pockets = target->m_cardpocket_typed;
    const auto space = target->get_cardpocketspace();
    const auto count = type == 0 ? std::get<0>(pockets).size() : type == 1 ? std::get<1>(pockets).size() : std::get<2>(pockets).size();
    const auto capacity = type == 0 ? std::get<0>(space) : type == 1 ? std::get<1>(space) : std::get<2>(space);
    return static_cast<::cw::lib::Integer>(count) >= capacity;
}
void ReplCardHolder::reconstruct_replcardholder(std::shared_ptr<::cw::sprite::card::PlayerCard> arg_selection, std::shared_ptr<::cw::header::CardHeader> arg_target, bool arg_personal)
{
    m_owner = object_value(arg_selection);
    m_selection = object_value(arg_selection);
    m_index2 = m_selection;
    m_target = object_value(arg_target);
    m_target_header = arg_target;
    m_personal_mode = arg_personal;
    m_index = 0;
    m_cardtype = ::cw::lib::Value(card_type_index(arg_target));
    m_list2.clear();
    const auto status = arg_personal ? ::cw::lib::Value("") : ::cw::lib::Value("unreversed");
    if (::cw::g_cwpy) for (const auto& card : ::cw::g_cwpy->get_pcards(status)) {
        if (!card) continue;
        if (arg_personal) {
            // Python の個人ポケットの交換先一覧と同様、明示的に Player
            // を受け付ける。 互換性のあるポケットを持つというだけで、すべての
            // PlayerCard/Character 派生型を対象にしない。
            if (const auto player = std::dynamic_pointer_cast<::cw::character::Player>(card);
                player && !player->m_personal_pocket.empty())
                m_list2.push_back(object_value(player));
        }
        else {
            const auto& p = card->m_cardpocket_typed;
            const auto type = card_type_index(arg_target);
            const auto has = type == 0 ? !std::get<0>(p).empty() : type == 1 ? !std::get<1>(p).empty() : type == 2 && !std::get<2>(p).empty();
            if (has) m_list2.push_back(object_value(card));
        }
    }
    m_card_list.clear();
    if (arg_selection) {
        if (arg_personal) m_card_list = arg_selection->m_personal_pocket;
        else {
            const auto& p = arg_selection->m_cardpocket_typed;
            const auto type = card_type_index(arg_target);
            if (type == 0) m_card_list = std::get<0>(p);
            else if (type == 1) m_card_list = std::get<1>(p);
            else if (type == 2) m_card_list = std::get<2>(p);
        }
    }
    CardControl::reconstruct("CARDPOCKET_REPLACE", "CARDPOCKET_REPLACE", false, false, std::nullopt);
    m_choice_popup = -1;
    m_input_focus = -1;
    m_pointer_card.reset();
    // Python と同様、CardHolder から開いた交換ダイアログは子画面
    // として扱う。 ゲームの選択を変更するのはフレーム所有のダイアログだけとする。
    if (!m_parent_holder) change_parent_selection(m_selection);
    draw_cards(true, 3);
    _start_animtimer();
}
void ReplCardHolder::_bind() { CardControl::_bind(); }
void ReplCardHolder::OnClickLeftBtn(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    if (m__quit || m__proc) return;
    m__cancel_animation = true;
    if (::cw::g_cwpy) ::cw::g_cwpy->play_sound("page", false, 100, 1, 0, 0, false);
    if (m_list2.empty()) throw std::out_of_range("Replacement player list is empty");
    auto it = std::find_if(m_list2.begin(), m_list2.end(), [this](const auto& value) { return same_value(value, m_index2); });
    if (it == m_list2.end()) throw std::invalid_argument("Replacement player is not in list");
    m_index2 = it == m_list2.begin() ? m_list2.back() : *std::prev(it);
    m_selection = m_index2;
    m_index=0;
    if (!m_parent_holder) change_parent_selection(m_selection);
    draw_cards(true, 3);
}
void ReplCardHolder::OnClickRightBtn(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    if (m__quit || m__proc) return;
    m__cancel_animation = true;
    if (::cw::g_cwpy) ::cw::g_cwpy->play_sound("page", false, 100, 1, 0, 0, false);
    if (m_list2.empty()) throw std::out_of_range("Replacement player list is empty");
    auto it = std::find_if(m_list2.begin(), m_list2.end(), [this](const auto& value) { return same_value(value, m_index2); });
    if (it == m_list2.end()) throw std::invalid_argument("Replacement player is not in list");
    if (++it == m_list2.end()) it = m_list2.begin();
    m_index2 = *it;
    m_selection = m_index2;
    m_index=0;
    if (!m_parent_holder) change_parent_selection(m_selection);
    draw_cards(true, 3);
}
void ReplCardHolder::draw_cards(bool arg_update, ::cw::lib::Integer arg_mode)
{
    if (const auto selected = object_value<::cw::sprite::card::PlayerCard>(m_selection)) {
        if (m_personal_mode) m_card_list = selected->m_personal_pocket;
        else if (m_target_header) {
            const auto& p = selected->m_cardpocket_typed;
            const auto type = card_type_index(m_target_header);
            if (type == 0) m_card_list = std::get<0>(p); else if (type == 1) m_card_list = std::get<1>(p); else if (type == 2) m_card_list = std::get<2>(p);
        }
        const auto name = ::cw::lib::python_percent_string(
            cardcontrol_msg(m_personal_mode ? "personal_cards" : "cards_hand",
                           m_personal_mode ? "%s の私有カード" : "%s の手札"), selected->get_showingname());
        m_title = cardcontrol_msg("card_control", "カード操作") + " - " + name;
    }
    CardControl::m_list = card_values(m_card_list);
    m_index=0;
    CardControl::draw_cards(arg_update, arg_mode < 0 ? 3 : arg_mode);
}
std::vector<::cw::lib::Value> ReplCardHolder::all_candidate_headers() const
{
    if (m_personal_mode || !m_target_header || m_target_header->m_type != "BeastCard")
        return card_values(m_card_list);
    std::vector<CardPtr> result;
    for (const auto& card : m_card_list) if (card && card->m_attachment) result.push_back(card);
    return card_values(result);
}
::cw::lib::Integer ReplCardHolder::candidate_page_count() const { return 1; }
void ReplCardHolder::change_candidate_page(::cw::lib::Integer delta)
{
    (void)delta;
    m_index = 0;
}
std::vector<::cw::lib::Value> ReplCardHolder::get_headers()
{
    return all_candidate_headers();
}
void ReplCardHolder::lclick_event(::cw::lib::Value arg_header_value)
{
    const auto card = object_value<::cw::header::CardHeader>(arg_header_value);
    if (m__quit || m__proc || !card || !m_target_header) return;
    const auto visible = get_headers();
    if (std::none_of(visible.begin(), visible.end(),
        [&](const auto& value) { return same_value(value, arg_header_value); })) return;
    const auto owner = card->get_owner();
    const auto target_owner = m_target_header->get_owner();
    if (card->m_type != m_target_header->m_type &&
        ((owner && object_value<::cw::character::Player>(*owner)) ||
         (target_owner && object_value<::cw::character::Player>(*target_owner)))) {
        show_errordialog(cardcontrol_msg("replace_failed_different_card_type", "カード種類が異なるため交換できません"));
        draw_cards(true, 3);
        return;
    }
    const auto candidate = std::find(m_card_list.begin(), m_card_list.end(), card);
    const auto selected = object_value<::cw::sprite::card::PlayerCard>(m_selection);
    if (candidate == m_card_list.end() || !selected) return;
    card->m_negaflag = false;
    const auto index = static_cast<::cw::lib::Integer>(std::distance(m_card_list.begin(), candidate));
    if (schedule_exchange(card, selected, index)) OnOk(nullptr);
}

bool ReplCardHolder::schedule_exchange(std::shared_ptr<::cw::header::CardHeader> card,
    std::shared_ptr<::cw::character::Player> selection, ::cw::lib::Integer index)
{
    if (!::cw::g_cwpy) return false;
    const auto runtime = ::cw::g_cwpy;
    const auto target = m_target_header;
    const bool personal = m_personal_mode;
    const bool call_predlg = !m_parent_holder;
    const auto frame = object_value<::cw::frame::Frame>(runtime->m_frame);
    // Python と同様、子画面で確定してからゲームスレッドで所持カードを変更す
    // る。 値を取得して渡し、モーダル破棄をまたいで this や親を保持しない。
    runtime->exec_func([target, card, selection, index, personal, call_predlg, frame]
                      (const auto&, const auto&) -> ::cw::lib::Value {
        try {
            ReplCardHolder_lclick_event_func(target, card, index, selection, personal, call_predlg);
        } catch (const std::exception& error) {
            if (frame) {
                const auto message = ::cw::lib::String("カード交換に失敗しました。\n") + error.what();
                frame->exec_func([message](const auto&, const auto&) -> ::cw::lib::Value {
                    ::cw::dialog::message::show_common_modal_message("エラー", message, false);
                    return {};
                });
            }
        }
        return {};
    });
    m__proc = true;
    return true;
}

void ReplCardHolder::OnOk(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    if (m__quit) return;
    m__quit = true;
    m__cancel_animation = true;
    _stop_animtimer();
    if (m_ui_backend) m_ui_backend->close(*this);
    // 子画面の所有者は共通モーダルホストが復元する。 ここで親を開き直
    // したり、予約した交換の完了前に親の処理中状態を解除したりしない。
    if (!m_parent_holder) ::cw::frame::Frame_kill_dlg_if_model(this);
}

void ReplCardHolder::OnCancel(std::shared_ptr<::cw::lib::Window> arg_event)
{
    if (m__quit) return;
    OnOk(std::move(arg_event));
    m_list.clear();
    m_list2.clear();
    m_card_list.clear();
    m_selection = {};
}
void ReplCardHolder::_replace_position_impl(::cw::lib::Value arg_header1_value, ::cw::lib::Value arg_header2_value)
{
    const auto first = object_value<::cw::header::CardHeader>(arg_header1_value);
    const auto second = object_value<::cw::header::CardHeader>(arg_header2_value);
    if (!first || !second) return;
    const auto owner = object_value<::cw::character::Player>(m_owner);
    if (!owner) throw std::runtime_error("Replacement reorder requires its owner");
    owner->replace_cardposition(int_value(m_cardtype, -1), first, second);
    const auto i = std::find(m_card_list.begin(), m_card_list.end(), first);
    const auto j = std::find(m_card_list.begin(), m_card_list.end(), second);
    if (i != m_card_list.end() && j != m_card_list.end()) std::iter_swap(i, j);
}
void SelectCard::initialize(std::shared_ptr<::cw::lib::Window> arg_parent, ::cw::lib::String arg_callname) { CardHolder::initialize(std::move(arg_parent), std::move(arg_callname)); }
void SelectCard::_init_list(::cw::lib::Value arg_status)
{
    if (m_callname != "BACKPACK" && m_callname != "STOREHOUSE") {
        initialize_inventory_list(std::move(arg_status));
        return;
    }
    CardHolder::m_list.clear(); m_list2.clear();
    const auto status = string_value(arg_status).empty() ? ::cw::lib::Value("active") : arg_status;
    if (::cw::g_cwpy && (m_callname == "BACKPACK" || m_callname == "STOREHOUSE")) {
        for (const auto& card : ::cw::g_cwpy->get_pcards(status)) m_list2.push_back(object_value(card));
        if (::cw::g_cwpy->m_ydata && *::cw::g_cwpy->m_ydata) {
            auto& ydata = **::cw::g_cwpy->m_ydata;
            if (m_callname == "STOREHOUSE") CardHolder::m_list = card_values(ydata.m_storehouse);
            else if (ydata.m_party && *ydata.m_party) CardHolder::m_list = card_values((**ydata.m_party).m_backpack);
        }
    }
    m_card_full_list = cards_from_values(CardHolder::m_list);
    m__fulllist = CardHolder::m_list;
    m_typed_full_list = typed_headers(CardHolder::m_list);
    m_typed_list = m_typed_full_list;
}
bool SelectCard::_update_sortattr(bool arg_draw)
{
    if (m_callname != "BACKPACK" && m_callname != "STOREHOUSE" &&
        m_callname != "CARDPOCKETB") return false;
    if (::cw::g_cwpy && ::cw::g_cwpy->m_setting) {
        const auto& sort_names = card_sort_names();
        if (m_sort_selection >= 0 && m_sort_selection < static_cast<::cw::lib::Integer>(sort_names.size())) ::cw::g_cwpy->m_setting->m_sort_cards = sort_names[static_cast<std::size_t>(m_sort_selection)];
    }
    if (!::cw::g_cwpy || !::cw::g_cwpy->m_ydata || !*::cw::g_cwpy->m_ydata)
        throw std::logic_error("Sorting requires inn data");
    {
        auto& ydata = **::cw::g_cwpy->m_ydata;
        std::optional<std::shared_ptr<::cw::character::Character>> test_aptitude;
        if (const auto card = _get_test_aptitude(); card && *card)
            test_aptitude = std::dynamic_pointer_cast<::cw::character::Character>(*card);
        if (m_callname == "BACKPACK" || m_callname == "CARDPOCKETB") {
            if (!ydata.m_party || !*ydata.m_party) throw std::logic_error("Sorting requires a party");
            (**ydata.m_party).sort_backpack(std::nullopt, test_aptitude);
            if (m_callname == "CARDPOCKETB") _set_backpacklist(true);
            else if (ydata.m_party && *ydata.m_party) { m_card_full_list = (**ydata.m_party).m_backpack; m__fulllist = card_values(m_card_full_list); _narrow(m__fulllist); }
        } else if (m_callname == "STOREHOUSE") {
            ydata.sort_storehouse(test_aptitude);
            m_card_full_list = ydata.m_storehouse; m__fulllist = card_values(m_card_full_list); _narrow(m__fulllist);
        } else return false;
        _clamp_page_index();
        if (arg_draw) draw_cards(true, -1);
        return true;
    }
    return false;
}
void SelectCard::_on_show(::cw::lib::Integer arg_cardtype)
{
    if (arg_cardtype < 0 || arg_cardtype >= 3) throw std::invalid_argument("invalid card type");
    m__cancel_animation = true;
    if (::cw::g_cwpy) ::cw::g_cwpy->play_sound("page",false,100,1,0,0,false);
    if (m_callname != "BACKPACK" && m_callname != "STOREHOUSE")
        throw std::logic_error("card type filters require backpack or storehouse");
    m_show_cardtype[static_cast<std::size_t>(arg_cardtype)] = !m_show_cardtype[static_cast<std::size_t>(arg_cardtype)];
    if (::cw::g_cwpy && ::cw::g_cwpy->m_setting)
        sequence_set(::cw::g_cwpy->m_setting->m_show_cardtype, static_cast<std::size_t>(arg_cardtype),
                     ::cw::lib::Value(m_show_cardtype[static_cast<std::size_t>(arg_cardtype)]));
    if (::cw::g_cwpy && ::cw::g_cwpy->m_ydata && *::cw::g_cwpy->m_ydata) {
        auto& ydata = **::cw::g_cwpy->m_ydata;
        if (m_callname == "BACKPACK" && ydata.m_party && *ydata.m_party) { m__fulllist = card_values((**ydata.m_party).m_backpack); _narrow(m__fulllist); }
        else if (m_callname == "STOREHOUSE") { m__fulllist = card_values(ydata.m_storehouse); _narrow(m__fulllist); }
    }
    _enable_updown();
    _update_page();
    draw_cards(true, -1);
}
void SelectCard::_change_callname(::cw::lib::String arg_old_callname)
{
#if defined(CARDWIRTH_LOG_ENABLED)
    log_cardcontrol("SelectCard _change_callname " + arg_old_callname + " -> " + m_callname);
#endif
    // SelectCard は、ネイティブ描画に使う基底 CardControl
    // の callname に加えて、互換用の callname も持つ。 両者を
    // 同期させ、BACKPACK/STOREHOUSE の切り替え時に、配置と Python
    // 互換の紺色・緑色の背景を直ちに更新する。PAD 透かしも含める。
    CardControl::m_callname = m_callname;
    _load_index();
    const auto status = trade_area(CardControl::m_areaid) ? ::cw::lib::Value("unreversed") : ::cw::lib::Value("active");
    if (m_callname == "CARDPOCKET") {
        // ページ数やコントロールを計算する前に、新しい手札を構築する。
        // 直接開いた場合も保管場所から切り替えた場合も、同じ表示にする。
        _init_cardpocketlist();
        change_parent_selection(m_selection);
    } else if (m_callname == "CARDPOCKETB") {
        _set_backpacklist(true);
        change_parent_selection(m_selection);
    } else if (m_callname == "BACKPACK") {
        if (::cw::g_cwpy && ::cw::g_cwpy->m_setting && ::cw::g_cwpy->m_setting->m_msgs) {
            auto& msgs = *::cw::g_cwpy->m_setting->m_msgs;
            m_title = msgs.get("card_control", "カード操作") + " - " + msgs.get("cards_backpack", "荷物袋");
        }
        m_selection = ::cw::lib::Value{};
        change_parent_selection_menu("BACKPACK");
        _init_list(status);
        m__fulllist = CardHolder::m_list;
        m_card_full_list = cards_from_values(CardHolder::m_list);
        _narrow(m__fulllist);
    } else if (m_callname == "STOREHOUSE") {
        if (::cw::g_cwpy && ::cw::g_cwpy->m_setting && ::cw::g_cwpy->m_setting->m_msgs) {
            auto& msgs = *::cw::g_cwpy->m_setting->m_msgs;
            m_title = msgs.get("card_control", "カード操作") + " - " + msgs.get("cards_storehouse", "カード置場");
        }
        m_selection = ::cw::lib::Value{};
        change_parent_selection_menu("STOREHOUSE");
        _init_list(status);
        m__fulllist = CardHolder::m_list;
        m_card_full_list = cards_from_values(CardHolder::m_list);
        _narrow(m__fulllist);
    } else {
        CardHolder::_change_callname(std::move(arg_old_callname));
        return;
    }
    if (m_callname != arg_old_callname) {
        _show_controls();
    }
    _enable_updown();
    _update_cardpocketinfo(m_last_cardpocket, false);
    _update_page();
    draw_cards(true, -1);
}
void SelectCard::lclick_event(::cw::lib::Value arg_header)
{
    if (m__proc || m__quit) return;
    if (m_callname == "INFOVIEW") {
        rclick_event(arg_header);
        return;
    }
    const auto header = object_value<CardPtr::element_type>(arg_header);
    if (!header) {
        CardControl::lclick_event(std::move(arg_header));
        return;
    }
    header->m_negaflag = false;
    const auto owner = character_value(m_selection);
    if (m_callname == "CARDPOCKET" && get_mode() == g_CCMODE_MOVE && owner && owner->m_reversed && m_last_cardpocket != 3) {
        if (::cw::g_cwpy && ::cw::g_cwpy->m_setting && ::cw::g_cwpy->m_setting->m_msgs) {
            auto message = ::cw::g_cwpy->m_setting->m_msgs->get("move_failed_reversed", "%sは反転中のため移動できません。");
            const auto marker = message.find("%s");
            if (marker != ::cw::lib::String::npos) message.replace(marker, 2, owner->get_showingname());
            show_errordialog(std::move(message));
        }
        draw_cards(true, -1);
        return;
    }
    const bool personal_use_takeout = m_callname == "CARDPOCKET" && m_last_cardpocket == 3 && get_mode() == g_CCMODE_USE;
    if (m_callname == "CARDPOCKETB" || personal_use_takeout) {
        if (!owner || !check_using(owner, header)) {
            draw_cards(true, -1);
            return;
        }
        if (const auto runtime = ::cw::g_cwpy) {
            // Python は共通の CardControl の確定処理の直前
            // に取り出しを行う。 ネイティブの Presentation は
            // UI スレッドで動くため、ウィンドウのコールバックからデッキ
            // や宿のモデルを変更せず、両操作を同じ先入れ先出し順で予約する。
            runtime->exec_func([runtime, owner, header](const auto&, const auto&) {
                if (::cw::g_cwpy != runtime) return ::cw::lib::Value{};
                runtime->m_card_takenouttemporarily = header;
                runtime->m_card_takenouttemporarily_personal_owner.reset();
                runtime->m_card_takenouttemporarily_personal_index = ::cw::lib::Value(-1);
                if (const auto personal_owner = header->m_personal_owner.lock()) {
                    runtime->m_card_takenouttemporarily_personal_owner = personal_owner;
                    const auto& pocket = personal_owner->m_personal_pocket;
                    const auto found = std::find(pocket.begin(), pocket.end(), header);
                    if (found != pocket.end())
                        runtime->m_card_takenouttemporarily_personal_index = ::cw::lib::Value(
                            static_cast<::cw::lib::Integer>(std::distance(pocket.begin(), found)));
                }
                runtime->trade("PLAYERCARD", object_value(owner), header, false, nullptr,
                    -1, -1, false, false, std::nullopt, false, false, false, true, 1);
                return ::cw::lib::Value{};
            });
        }
        CardControl::lclick_event(std::move(arg_header));
        return;
    }
    if (header->m_type == "UseCardInBackpack") {
        // この疑似カードはパーティーの荷物袋を開くだけである。 現在のシナリオ
        // の許可状態を再確認し、古い手札から後の禁止を回避できないようにする。
        if (!m_environment_backpack) {
            draw_cards(true, -1);
            return;
        }
        if (!owner) {
            CardControl::lclick_event(std::move(arg_header));
            return;
        }
        if (owner->is_inactive()) {
            if (::cw::g_cwpy && ::cw::g_cwpy->m_setting && ::cw::g_cwpy->m_setting->m_msgs) {
                auto message = ::cw::g_cwpy->m_setting->m_msgs->get("inactive", "%sは行動不能です。");
                const auto marker = message.find("%s");
                if (marker != ::cw::lib::String::npos) message.replace(marker, 2, owner->get_showingname());
                show_errordialog(std::move(message));
            }
            draw_cards(true, -1);
            return;
        }
        const auto old_callname = m_callname;
        m_callname = "CARDPOCKETB";
        CardControl::m_callname = m_callname;
        _change_callname(old_callname);
        return;
    }
    CardControl::lclick_event(std::move(arg_header));
}
void SelectCard::_set_backpacklist(bool arg_narrow)
{
    if (!::cw::g_cwpy || !::cw::g_cwpy->m_ydata || !*::cw::g_cwpy->m_ydata || !(**::cw::g_cwpy->m_ydata).m_party || !(**::cw::g_cwpy->m_ydata).m_party) return;
    const auto& backpack = (**::cw::g_cwpy->m_ydata).m_party.value()->m_backpack;
    m_card_full_list.clear();
    for (const auto& card : backpack) if (card_type_index(card) == m_last_cardpocket && (!is_showpersonal() || !card->m_personal_owner.lock())) m_card_full_list.push_back(card);
    m__fulllist = card_values(m_card_full_list);
    if (arg_narrow) _narrow(m__fulllist); else { m_typed_full_list = typed_headers(m__fulllist); m_typed_list = m_typed_full_list; }
    _clamp_page_index();
}
void SelectCard::_init_cardpocketlist()
{
    if (m_callname != "CARDPOCKET") return;
    if (::cw::g_cwpy && ::cw::g_cwpy->m_setting) {
        auto& setting = *::cw::g_cwpy->m_setting;
        if (setting.m_last_cardpocket == 3 &&
            !(setting.m_show_personal_cards && m_environment_backpack)) {
            setting.m_last_cardpocket = 0;
            m_last_cardpocket = 0;
        }
    }
    CardHolder::_init_cardpocketlist();
    if (!m_environment_backpack || !::cw::g_cwpy || !::cw::g_cwpy->m_setting || !::cw::g_cwpy->m_setting->m_show_backpackcard || (m_last_cardpocket != 1 && m_last_cardpocket != 2) || get_mode() != g_CCMODE_USE) return;
    const auto owner = object_value<::cw::character::Character>(m_selection);
    if (!owner) return;
    const auto spaces = owner->get_cardpocketspace();
    const auto count = m_last_cardpocket == 1 ? std::get<1>(owner->m_cardpocket_typed).size() : std::get<2>(owner->m_cardpocket_typed).size();
    const auto space = m_last_cardpocket == 1 ? std::get<1>(spaces) : std::get<2>(spaces);
    if (static_cast<::cw::lib::Integer>(count) >= space || !::cw::g_cwpy->m_ydata || !*::cw::g_cwpy->m_ydata || !(**::cw::g_cwpy->m_ydata).m_party || !(**::cw::g_cwpy->m_ydata).m_party) return;
    const auto type = m_last_cardpocket == 1 ? ::cw::lib::String("ItemCard") : ::cw::lib::String("BeastCard");
    const auto it = std::find_if((**::cw::g_cwpy->m_ydata).m_party.value()->m_backpack.begin(), (**::cw::g_cwpy->m_ydata).m_party.value()->m_backpack.end(), [type](const auto& card) { return card && card->m_type == type; });
    if (it == (**::cw::g_cwpy->m_ydata).m_party.value()->m_backpack.end()) return;
    if (::cw::g_cwpy->m_rsrc) {
        const auto special = ::cw::g_cwpy->m_rsrc->m_backpackcards.find(type);
        if (special != ::cw::g_cwpy->m_rsrc->m_backpackcards.end()) {
            if (::cw::g_cwpy->m_setting->m_show_backpackcardatend) CardHolder::m_list.insert(CardHolder::m_list.begin() + std::min<std::size_t>(10, CardHolder::m_list.size()), object_value(special->second));
            else CardHolder::m_list.insert(CardHolder::m_list.begin(), object_value(special->second));
            m__fulllist = CardHolder::m_list; m_card_full_list = cards_from_values(CardHolder::m_list); _narrow(m__fulllist);
        }
    }
}

::cw::lib::Integer g_CCMODE_BATTLE = 2;
::cw::lib::Integer g_CCMODE_MOVE = 1;
::cw::lib::Integer g_CCMODE_REPLACE = 4;
::cw::lib::Integer g_CCMODE_SHOW = 0;
::cw::lib::Integer g_CCMODE_USE = 3;
::cw::lib::Value g_CardHeaderType{};

namespace {
void drain_legacy_callback()
{
    if (!CardControl::s__after_event) return;
    auto callback = std::move(CardControl::s__after_event);
    CardControl::s__after_event.reset();
    if (callback && *callback) (*callback)({}, {});
}
}

bool CardControl___init___can_sort() { /* インスタンスを持たない生成互換helper。実判定はCardControl::_can_narrow/派生sortで行う。 */ return true; }
::cw::lib::Value CardControl___init___lambda_line_148() { /* 表示ボタンの callname 判定は共通モデルが保持する。 */ return ::cw::lib::Value(true); }
void CardControl_OnDebugMode_func(::cw::lib::Value arg_self) { if (auto control = object_value<CardControl>(arg_self)) control->update_debug(); else drain_legacy_callback(); }
void CardControl_OnDebugMode_func_func(::cw::lib::Value arg_self) { CardControl_OnDebugMode_func(std::move(arg_self)); }
void CardControl__additional_controls_func() { drain_legacy_callback(); }
void CardControl_OnNarrowCondition_func() { drain_legacy_callback(); }
void CardControl_OnKeyDown_func() { drain_legacy_callback(); }
void CardControl_OnKeyDown_func_2() { drain_legacy_callback(); }
void CardControl_OnLeftUp_func() { drain_legacy_callback(); }
void CardControl_OnLeftUp_func_2() { drain_legacy_callback(); }
void CardControl_OnLeftUp_func_3() { drain_legacy_callback(); }
void CardControl__replace_position_func() { drain_legacy_callback(); }
::cw::lib::Value CardControl__replace_position_func_lambda_line_933() { drain_legacy_callback(); return {}; }
void CardControl_OnRightUp_func() { drain_legacy_callback(); }
void CardControl_OnPaint2_draw_price(std::shared_ptr<::cw::lib::DeviceContext> arg_dc, std::shared_ptr<::cw::header::CardHeader> arg_header) { (void)arg_dc; (void)arg_header; drain_legacy_callback(); }
void CardControl_OnPaint2_draw_star(std::shared_ptr<::cw::lib::DeviceContext> arg_dc, std::shared_ptr<::cw::header::CardHeader> arg_header) { (void)arg_dc; (void)arg_header; drain_legacy_callback(); }
void CardControl_animate_click_func2() { drain_legacy_callback(); }
void CardControl_animate_click_func2_func3() { drain_legacy_callback(); }
void CardControl_animate_starclick_func2() { drain_legacy_callback(); }
void CardControl_animate_starclick_func2_func3() { drain_legacy_callback(); }
void CardControl_animate_replsclick_func2() { drain_legacy_callback(); }
void CardControl_animate_replsclick_func2_func3() { drain_legacy_callback(); }
void CardControl__animate_dealhide_func2() { drain_legacy_callback(); }
void CardControl__animate_dealhide_func2_func3() { drain_legacy_callback(); }
void CardControl_lclick_event_func(std::shared_ptr<::cw::header::CardHeader> arg_header) {
    if (arg_header) {
        arg_header->m_negaflag = false;
        if (::cw::g_cwpy) {
            const auto runtime = ::cw::g_cwpy;
            runtime->exec_func([runtime, arg_header = std::move(arg_header)](const auto&, const auto&) mutable {
                if (::cw::g_cwpy == runtime) runtime->m_selectedheader = std::move(arg_header);
                return ::cw::lib::Value{};
            });
        }
    }
    drain_legacy_callback();
}
void CardControl_lclick_event_func_func() { drain_legacy_callback(); }
void CardControl_lclick_event_func2(::cw::lib::Value arg_self) { if (auto control = object_value<CardControl>(arg_self)) control->process_ui_events(); else drain_legacy_callback(); }
void CardControl_lclick_event_func2_func(::cw::lib::Value arg_self) { CardControl_lclick_event_func2(std::move(arg_self)); }
void CardControl_lclick_event_append_predialogs(::cw::lib::String arg_callname, std::shared_ptr<::cw::sprite::card::CWPyCard> arg_index2, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_pos) {
    if (::cw::g_cwpy && arg_index2) {
        const auto runtime = ::cw::g_cwpy;
        runtime->exec_func([runtime, arg_callname = std::move(arg_callname), arg_index2 = std::move(arg_index2), arg_pos](const auto&, const auto&) mutable {
            if (::cw::g_cwpy == runtime)
                runtime->m_pre_dialogs.emplace_back(std::move(arg_callname), std::move(arg_index2), arg_pos, ::cw::g_UP_WIN);
            return ::cw::lib::Value{};
        });
    }
}
void CardControl_lclick_event_func3() {
    if (::cw::g_cwpy) {
        const auto runtime = ::cw::g_cwpy;
        runtime->exec_func([runtime](const auto&, const auto&) {
            if (::cw::g_cwpy == runtime) {
                runtime->update_tradecards();
                runtime->update_selectablelist();
            }
            return ::cw::lib::Value{};
        });
    }
    drain_legacy_callback();
}
void CardControl_OnCancel_func() { drain_legacy_callback(); }
::cw::lib::Value CardHolder___init___lambda_line_2064() { return ::cw::lib::Value(true); }
void CardHolder_reconstruct_cardholder_func() { drain_legacy_callback(); }
void CardHolder_OnPageSetFocus_func() { drain_legacy_callback(); }
bool CardHolder_OnMouseWheel_selcombo(::cw::lib::Value arg_combo) { /* 互換用の判定。コンボの値がNoneでなければtrueを返す。 */ return !arg_combo.is_none(); }
bool CardHolder__narrow_has_personalowner(::cw::lib::Value arg_header) { auto h = object_value<CardPtr::element_type>(arg_header); return h && h->m_personal_owner.lock(); }
::cw::lib::Value CardHolder__narrow_lambda_line_3084(::cw::lib::Value arg_header) { return arg_header; }
bool CardHolder__narrow_match_keycode() { return false; }
std::shared_ptr<::cw::character::Character> HandView__update_cardlist_character(std::shared_ptr<::cw::sprite::card::CWPyCard> arg_pcard) { return object_value<::cw::character::Character>(object_value(arg_pcard)); }
bool ReplCardHolder_get_headers_is_attachment(::cw::lib::Value arg_header) { auto h = object_value<CardPtr::element_type>(arg_header); return h && h->m_attachment; }
void ReplCardHolder_lclick_event_func(std::shared_ptr<::cw::header::CardHeader> arg_target, std::shared_ptr<::cw::header::CardHeader> arg_header, ::cw::lib::Integer arg_index, std::shared_ptr<::cw::character::Player> arg_selection, bool arg_personal, bool arg_call_predlg)
{
    if (!::cw::g_cwpy || !arg_target || !arg_header || !arg_selection) return;
    const auto runtime = ::cw::g_cwpy;
    const auto owner_value = arg_target->get_owner();
    const auto owner = owner_value ? object_value<::cw::character::Player>(*owner_value) : nullptr;
    const auto personal_owner = arg_target->m_personal_owner.lock();
    ::cw::lib::String from_type;
    if (owner) from_type = "PLAYERCARD";
    else if (arg_target->is_backpackheader()) from_type = "BACKPACK";
    else if (arg_target->is_storehouseheader()) from_type = "STOREHOUSE";
    else throw std::invalid_argument("交換元のカードが見つかりません。");
    auto from_index = arg_index;
    ::cw::lib::Integer personal_index = -1;
    if (owner) {
        const auto type = card_type_index(arg_target);
        const auto& pockets = owner->m_cardpocket_typed;
        const auto& cards = type == 0 ? std::get<0>(pockets) : type == 1 ? std::get<1>(pockets) : std::get<2>(pockets);
        const auto it = std::find(cards.begin(), cards.end(), arg_target);
        if (type < 0 || it == cards.end()) throw std::invalid_argument("交換元の手札が変更されています。");
        from_index = static_cast<::cw::lib::Integer>(std::distance(cards.begin(), it));
        // 両方のプレイヤーが満杯の場合があるため、Py と同様、まず移動元の枠を空ける。
        runtime->trade("BACKPACK", std::nullopt, arg_target, false, nullptr, -1, -1, false, false,
                       std::nullopt, false, false, false, true, 1);
    } else if (personal_owner) {
        const auto it = std::find(personal_owner->m_personal_pocket.begin(), personal_owner->m_personal_pocket.end(), arg_target);
        if (it == personal_owner->m_personal_pocket.end()) throw std::invalid_argument("交換元の私有カードが変更されています。");
        personal_index = static_cast<::cw::lib::Integer>(std::distance(personal_owner->m_personal_pocket.begin(), it));
        from_index = -1;
        personal_owner->remove_personalpocket(arg_target);
    }
    const auto insert_order = int_value(arg_target->m_order, -1);
    if (const auto owner = arg_header->m_personal_owner.lock())
        owner->remove_personalpocket(arg_header);
    runtime->trade(from_type, owner_value, arg_header, false, nullptr, from_index, insert_order, false, false,
                   std::nullopt, false, false, false, true, 1);
    if (personal_owner) personal_owner->add_personalpocket(arg_header, personal_index);
    if (arg_personal) {
        runtime->trade("BACKPACK", std::nullopt, arg_target, false, nullptr, -1, -1, false, false,
                       std::nullopt, false, false, false, true, 1);
        arg_selection->add_personalpocket(arg_target, arg_index);
    } else {
        runtime->trade("PLAYERCARD", object_value(arg_selection), arg_target, false, nullptr, arg_index, -1, true, false,
                       std::nullopt, false, false, false, true, 1);
    }
    if ((personal_owner || arg_personal) && runtime->m_ydata && *runtime->m_ydata &&
        (*runtime->m_ydata)->m_party && *(*runtime->m_ydata)->m_party)
        (*(*runtime->m_ydata)->m_party)->sort_backpack(std::nullopt, std::nullopt);
    if (arg_call_predlg)
        runtime->exec_func([runtime](const auto&, const auto&) -> ::cw::lib::Value {
            runtime->call_predlg();
            return {};
        });
}

::cw::lib::Integer get_spacer_x(::cw::lib::Integer arg_mode) { return arg_mode == 1 ? 4 : 3; }
std::vector<std::tuple<::cw::lib::Integer, ::cw::lib::Integer>> get_poslist(::cw::lib::Integer arg_num, ::cw::lib::Integer arg_mode)
{
    std::vector<std::tuple<::cw::lib::Integer, ::cw::lib::Integer>> result; if (arg_num <= 0) return result;
    if (arg_mode == 1) {
        const bool additional = ::cw::g_cwpy && ::cw::g_cwpy->m_setting && ::cw::g_cwpy->m_setting->m_show_additional_card;
        const auto y1 = additional ? 31 : 39;
        const auto y2 = additional ? 145 : 161;
        for (::cw::lib::Integer i = 0; i < arg_num; ++i) result.emplace_back(90 + 84 * (i % 5), i < 5 ? y1 : y2);
        return result;
    }
    const ::cw::lib::Integer width = arg_mode == 2 ? 425 : 525, count = arg_mode == 2 ? 5 : 6, left = arg_mode == 2 ? 88 : 0;
    if (arg_num < count || (arg_mode == 3 && arg_num == count)) { const auto x = (width - 83 * arg_num) / 2 + left; for (::cw::lib::Integer i = 0; i < arg_num; ++i) result.emplace_back(x + 83 * i, 95); }
    else { const auto row1 = arg_num / 2 + arg_num % 2, row2 = arg_num / 2; const auto x1 = (width - 83 * row1) / 2 + left, x2 = (width - 83 * row2) / 2 + left; for (::cw::lib::Integer i = 0; i < row1; ++i) result.emplace_back(x1 + 83 * i, 40); for (::cw::lib::Integer i = 0; i < row2; ++i) result.emplace_back(x2 + 83 * i, 160); }
    return result;
}

namespace {
bool chain_button(::cw::dialog::DialogPresentationAction action,int index) {
    using Action=::cw::dialog::DialogPresentationAction;
    return ((action==Action::Previous || action==Action::Next) && index==-1) ||
        (action==Action::Auxiliary && (index==1 || index==2 || index==13 || index==14));
}

constexpr int choice_dismiss = 1800000;
constexpr int choice_scroll = 1800001;
constexpr int choice_item = 1800100;
constexpr int reorder_item = 1800200;

std::vector<::cw::lib::String> control_choices(CardControl& control, int kind)
{
    if (kind == 0) {
        if (auto holder = dynamic_cast<CardHolder*>(&control)) return holder->m_sendto_labels;
    } else if (kind == 11) return control.m_narrow_choices;
    else if (kind == 10) return {
        cardcontrol_msg("sort_no", "整列しない"), cardcontrol_msg("sort_name", "名前"),
        cardcontrol_msg("sort_level", "レベル"), cardcontrol_msg("sort_type", "種別"),
        cardcontrol_msg("sort_price", "価格"), cardcontrol_msg("scenario_name", "シナリオ"),
        cardcontrol_msg("author", "作者"), cardcontrol_msg("sort_aptitude", "適性")};
    return {};
}


struct CardChoiceRow {
    ::cw::lib::String text;
    std::shared_ptr<::cw::lib::Surface> bitmap;
    ::cw::lib::Integer width{},height{1};
};
std::vector<CardChoiceRow> measured_choices(CardControl& control,int kind)
{
    std::vector<CardChoiceRow> rows;
    const auto labels=control_choices(control,kind);
    const auto holder=dynamic_cast<const CardHolder*>(&control);
    for(std::size_t i=0;i<labels.size();++i) {
        CardChoiceRow row;row.text=labels[i];
        if(kind==0 && holder && i<holder->m_sendto_icons.size())
            row.bitmap=control.combo_bitmap(holder->m_sendto_icons[i]);
        const auto glyph=card_label(control,row.text,"combo",14);
        const auto cell=glyph?glyph:card_label(control,"Mg","combo",14);
        // Py の CWPyBitmapComboBox.OnMeasureItem は、ビット
        // マップとテキストからポップアップ行を計測する。 _set_itemheight のネ
        // イティブ選択欄向けメッセージは、独自描画ポップアップの計測コールバックを代替しない。
        row.height=std::max<::cw::lib::Integer>(1,std::max(row.bitmap?row.bitmap->size.height:0,cell?cell->size.height:0));
        row.width=(row.bitmap?row.bitmap->size.width:0)+(glyph?glyph->size.width:0);
        rows.push_back(std::move(row));
    }
    return rows;
}

int control_choice_index(const CardControl& control, int kind)
{
    if (kind == 0) return static_cast<int>(control.m_sendto_selection.value_or(0));
    return static_cast<int>(kind == 10 ? control.m_sort_selection : control.m_narrow_selection);
}

::cw::lib::Rect choice_rect(const CardControlToolbarLayout& toolbar, int kind)
{
    return kind == 0 ? toolbar.combo : kind == 10 ? toolbar.sort : toolbar.narrow_type;
}

bool choice_enabled(CardControl& control, int kind)
{
    if (kind == 0) return control.m_sendto;
    if (!control.m_show_additional_controls) return false;
    if (kind == 10) return can_show_sort_controls(control) && control.m_sort_enabled;
    return kind == 11 && control._can_narrow();
}

void apply_control_choice(CardControl& control, int kind, int selection)
{
    const auto choices = control_choices(control, kind);
    if (!choice_enabled(control, kind) || selection < 0 || selection >= static_cast<int>(choices.size())) return;
    auto event = std::make_shared<::cw::lib::Window>();
    // OnSort/OnSendTo は選択された行を受け取り、ツールバーのボタン ID は使わない。
    event->id = selection;
    if (kind == 0) control.OnSendTo(event);
    else if (kind == 10) control.OnSort(event);
    else { control.m_narrow_selection = selection; control.OnNarrowCondition(event); }
    if (control.m_ui_backend) control.m_ui_backend->invalidate(control);
}

// キャッシュ済みカードキャンバスのコピー上で、範囲を制限した入力欄を描画する。 ポ
// ップアップや入力欄のフォーカス表示を、次のポップアップなしのフレームに残さない。
void control_field(const CardControl& control,const std::shared_ptr<::cw::lib::Surface>& surface,
                   const ::cw::lib::Rect& rect, const ::cw::lib::String& text, bool focused,
                   bool dropdown = false, bool selected_row = false,
                   const std::shared_ptr<::cw::lib::Surface>& bitmap = {}, bool popup_row = false)
{
    if (rect.width <= 0 || rect.height <= 0) return;
    auto field = std::make_shared<::cw::lib::Surface>();
    field->size = {rect.width, rect.height};
    field->rgba.assign(static_cast<std::size_t>(rect.width * rect.height) * 4, 255);
    const auto selection = ::cw::lib::ui::system_selection_colours();
    if (selected_row) ::cw::lib::ui::fill_rect(
        field, {0, 0, rect.width, rect.height}, selection.background);
    if(!popup_row) ::cw::lib::ui::draw_sunken_border(field, {0, 0, rect.width, rect.height});
    const auto font = control.dialog_font("combo",14);
    const auto ink = selected_row ? selection.text : ::cw::lib::Color{0,0,0,255};
    const auto glyph=card_label(control,text,"combo",14,{ink.r,ink.g,ink.b});
    const auto left=popup_row?0:cs(3);
    if(bitmap) blend_ui_surface(field,bitmap,left,half_floor(rect.height-bitmap->size.height));
    if(glyph) blend_ui_surface(field,glyph,left+(bitmap?bitmap->size.width:0),half_floor(rect.height-glyph->size.height));
    if (dropdown) {
        const auto x = rect.width - cs(15);
        ::cw::lib::ui::fill_rect(field, {x, 1, cs(15) - 1, rect.height - 2}, {240, 240, 240, 255});
        ::cw::lib::ui::draw_label(field, "▾", x + cs(2), cs(3), font.pixels, font.bold, {0, 0, 0}, font.face, font.italic);
    }
    if (focused) ::cw::lib::ui::fill_rect(field, {1, rect.height - 2, rect.width - 2, 1}, {0, 120, 215, 255});
    ::cw::lib::ui::blend_surface(surface, field, rect.x, rect.y);
}

// 変化のないコンボボックスや編集欄の内容は、保持中の CardControl の描画面に
// 含める。 マウスのホバーだけで、変更のないツールバーの文字を描き直すために拡大後の 640
// ×360 のダイアログ全体をコピーしない。 フォーカス・キャレット・ポップアップ・
// 押下の装飾は一時表示とし、dialog_presentation() で合成する。
void decorate_cardcontrol_fields(CardControl& control)
{
    const auto& surface=control.m_ui_surface;
    if(!surface) return;
    const auto toolbar=compute_toolbar_layout(control,surface->size.width,cs(kPanelHeight));
    for(const int kind:{0,10,11}) {
        const auto rows=measured_choices(control,kind);
        const auto selected=control_choice_index(control,kind);
        if(selected>=0 && selected<static_cast<int>(rows.size()))
            control_field(control,surface,choice_rect(toolbar,kind),rows[selected].text,
                false,true,false,rows[selected].bitmap);
    }
    if(toolbar.narrow.width>0)
        control_field(control,surface,toolbar.narrow,control.m_narrow_text,false);
    if(dynamic_cast<CardHolder*>(&control) && control.m_show_additional_controls && toolbar.page.width>0)
        control_field(control,surface,toolbar.page,control.m_page_input,false);
}

bool control_contains(const ::cw::lib::Rect& rect, float x, float y)
{
    return rect.width > 0 && rect.height > 0 && x >= rect.x && y >= rect.y &&
        x < rect.x + rect.width && y < rect.y + rect.height;
}
} // namespace


void CardControl::retire_side_input() {
    if(m_side_capture) m_side_stale=true;
}
void CardControl::append_side_controls(::cw::dialog::DialogPresentation& view)
{
    using Control=::cw::dialog::DialogPresentationControl;
    using Action=::cw::dialog::DialogPresentationAction;
    m_side_controls.clear();m_side_exclusions=view.controls;
    // ネイティブ風のボタンが無効でも、その領域を除外したままにする。 ク
    // リックが背後のダイアログの移動操作として処理されないようにする。
    for(const auto& c:view.controls) if(c.action==Action::Auxiliary && c.index==15 && c.enabled) {
        auto halo=c;halo.x-=10;halo.y-=10;halo.width+=20;halo.height+=20;
        m_side_exclusions.push_back(halo);
    }
    m_side_layout_scale=cardcontrol_ui_scale();
    if(m_side_capture && m_side_capture_scale!=m_side_layout_scale) retire_side_input();
    if(!side_click_option() || !navigation_enabled() || navigation_input_blocked()) {
        retire_side_input();return;
    }
    ::cw::dialog::DialogPresentation sides;
    m_side_navigation.append(sides,{0,0,cs(kPanelWidth),cs(kPanelHeight)},true,-2);
    // 見えない仮コントロールを置くのではなく、子コントロールとカードの当たり判定領
    // 域を差し引く。 クリックの転送とカーソル判定の双方で、同じ残りの領域を使う。
    for(auto side:sides.controls) {
        std::vector<Control> pieces{side};
        for(const auto& excluded:m_side_exclusions) {
            std::vector<Control> next;
            for(const auto& piece:pieces) {
                const auto l=std::max(piece.x,excluded.x),t=std::max(piece.y,excluded.y);
                const auto r=std::min(piece.x+piece.width,excluded.x+excluded.width);
                const auto b=std::min(piece.y+piece.height,excluded.y+excluded.height);
                if(l>=r || t>=b){next.push_back(piece);continue;}
                const auto add=[&](float x,float y,float w,float h) {
                    if(w<=0 || h<=0) return;
                    auto part=piece;part.x=x;part.y=y;part.width=w;part.height=h;next.push_back(part);
                };
                add(piece.x,piece.y,piece.width,t-piece.y);
                add(piece.x,b,piece.width,piece.y+piece.height-b);
                add(piece.x,t,l-piece.x,b-t);
                add(r,t,piece.x+piece.width-r,b-t);
            }
            pieces=std::move(next);
        }
        for(auto& piece:pieces) {
            piece.continuous_pointer=true; // キャプチャーのみを行い、押下操作やタイマーは開始しない。
            m_side_controls.push_back(std::move(piece));
        }
    }
    view.controls.insert(view.controls.begin(),m_side_controls.begin(),m_side_controls.end());
}
std::optional<::cw::dialog::DialogPresentationAction> CardControl::side_action_at(float x,float y)
{
    m_side_pointer=std::pair{x,y};
    if(!_can_sideclick()) return {};
    for(const auto& c:m_side_controls)
        if(x>=c.x && y>=c.y && x<c.x+c.width && y<c.y+c.height) return c.action;
    return {};
}

std::shared_ptr<::cw::lib::Surface> CardControl::combo_bitmap(const ::cw::lib::String& role) const
{
    return role.empty()?nullptr:resource_button_surface(role);
}

::cw::lib::ui::FontStyle CardControl::dialog_font(const ::cw::lib::String& role,
    ::cw::lib::Integer logical_pixels) const
{
    const bool bold=role!="combo" && role!="paneltitle2";
    return ::cw::lib::ui::configured_wx_font(role,logical_pixels,false,0,bold);
}

bool CardControl::navigation_enabled() const
{
    if(m_callname=="INFOVIEW") return false;
    if(m_callname=="CARDPOCKET_REPLACE") return m_list2.size()>1;
    if(const auto hand=dynamic_cast<const HandView*>(this)) return hand->m_list2.size()>1;
    if(const auto holder=dynamic_cast<const CardHolder*>(this)) {
        bool party=!holder->m_list2.empty() || holder->m_can_open_backpack || holder->m_can_open_cardpocket;
        if(::cw::g_cwpy)
            party=::cw::g_cwpy->m_ydata && *::cw::g_cwpy->m_ydata &&
                (*::cw::g_cwpy->m_ydata)->m_party && *(*::cw::g_cwpy->m_ydata)->m_party;
        return party && (m_sendto || holder->m_list2.size()!=1);
    }
    return m_list2.size()>1;
}

bool CardControl::chain_enabled(::cw::dialog::DialogPresentationAction action,int index) const
{
    using Action=::cw::dialog::DialogPresentationAction;
    if(action==Action::Previous || action==Action::Next) return navigation_enabled();
    if(action!=Action::Auxiliary) return false;
    if(index==1 || index==2) {
        const auto holder=dynamic_cast<const CardHolder*>(this);
        return holder && m_sendto && !holder->m_sendto_labels.empty();
    }
    if(index==13 || index==14) {
        const auto holder=dynamic_cast<const CardHolder*>(this);
        return holder && m_callname!="CARDPOCKET" && holder->m_typed_list.size()>10;
    }
    return false;
}

bool CardControl::navigation_input_blocked() const
{
    const auto holder=dynamic_cast<const CardHolder*>(this);
    return m__quit || m__proc || m_cardinfo_dialog || m_choice_popup>=0 ||
        m_narrow_input.menu.opened() || m_page_editor.menu.opened() ||
        !m_narrow_input.edit.composition.empty() || !m_page_editor.edit.composition.empty() ||
        (holder && (holder->m__proc_page || holder->m_proc_page));
}

::cw::dialog::ButtonRepeatTimer::Clock::time_point CardControl::card_input_time() const noexcept
{
    return ::cw::dialog::ButtonRepeatTimer::Clock::now();
}

void CardControl::retire_chain_input()
{
    retire_side_input();
    if(m_button_capture) {m_button_stale=true;request_dialog_presentation_refresh();}
    if(m_chain_running) request_dialog_presentation_refresh();
    m_chain_running=false;
    if(m_chain_capture) m_chain_stale=true; // 以前のキャプチャーが解放されるまで、同じ対象を保持する。
}

void CardControl::dispatch_chain(::cw::dialog::DialogPresentationAction action,int index)
{
    struct Guard {bool& value;~Guard(){value=false;}} guard{m_chain_dispatching};
    m_chain_dispatching=true;
    dialog_presentation_action(action,index);
}

void CardControl::dialog_presentation_pointer_cancel()
{
    retire_keyboard_press();m_choice_drag.reset();m_choice_drag_stale=false;
    m_button_capture.reset();m_button_inside=false;m_button_stale=false;
    m_side_capture.reset();m_side_stale=false;m_side_pointer.reset();
    m_chain_capture.reset();m_chain_running=false;m_chain_stale=false;
    m_pointer_card.reset();m_pointer_reorder=false;
    m_narrow_input.dragging=false;m_page_editor.dragging=false;
    request_dialog_presentation_refresh();
}

bool CardControl::dialog_presentation_needs_refresh() const noexcept
{
    return DialogPresentationProvider::dialog_presentation_needs_refresh() || m_ui_invalidated ||
        m_presentation_scale!=cardcontrol_ui_scale() ||
        (m_chain_running && !m_chain_stale && m_chain_timer.due(card_input_time()));
}

void CardControl::retire_field_inputs() {
    m_narrow_input.retire();m_page_editor.retire();m_input_focus=-1;
}
void CardControl::sync_field_inputs() {
    m_narrow_input.sync(m_narrow_text);
    if(auto holder=dynamic_cast<CardHolder*>(this)) {
        const auto current=std::to_string(holder->m_index+1);
        if(m_page_input!=current) m_page_input=current;
        m_page_editor.sync(m_page_input);
    }
}
::cw::dialog::textedit::FilterInput* CardControl::active_field() {
    if(m__quit || m__proc || m_cardinfo_dialog || !m_show_additional_controls || m_choice_popup>=0) return nullptr;
    if(m_input_focus==12 && _can_narrow()) return &m_narrow_input;
    if(m_input_focus==15 && dynamic_cast<CardHolder*>(this) &&
       compute_toolbar_layout(*this,cs(panel_width(*this)),cs(kPanelHeight)).page.width>0) return &m_page_editor;
    return nullptr;
}
void CardControl::publish_field_input() {
    if(m_input_focus==12) {
        if(m_narrow_text!=m_narrow_input.edit.text) {
            m_narrow_text=m_narrow_input.edit.text;OnNarrowCondition(nullptr);
        }
    } else if(m_input_focus==15) {
        m_page_input=m_page_editor.edit.text;
        commit_page_input();
    }
    request_dialog_presentation_refresh();
}
bool CardControl::edit_field(const std::function<bool(::cw::dialog::textedit::FilterInput&)>& operation,bool clamp_page) {
    sync_field_inputs();
    auto input=active_field();
    if(!input) return false;
    if(m_input_focus==12) {
        if(!operation(*input)) return false;
        publish_field_input();return true;
    }
    // wx の IntValidator と同様、コントロールやカードページを変更する前
    // に変更後の値を検証する。 切り取りを拒否した場合は、クリップボードも変更しない。
    auto candidate=*input;
    std::optional<std::string> copied;
    candidate.write=[&](const std::string& value){copied=value;};
    candidate.read=[&] {
        auto value=input->read();
        const auto first=value.find_first_not_of(" \t\r\n"),last=value.find_last_not_of(" \t\r\n");
        return first==std::string::npos?std::string{}:value.substr(first,last-first+1);
    };
    if(!operation(candidate)) return false;
    input->menu=candidate.menu;input->modifiers=candidate.modifiers;input->dragging=candidate.dragging;
    bool valid=true;
    if(candidate.edit.text!=input->edit.text) {
        const auto& text=candidate.edit.text;
        const bool empty=text.empty();
        ::cw::lib::Integer number=0;
        std::size_t first=0;
        bool negative=false;
        if(!text.empty() && (text[0]=='+' || text[0]=='-')) {negative=text[0]=='-';first=1;}
        if(first==text.size() && (!text.empty() || !clamp_page)) valid=false;
        for(std::size_t i=first;valid && i<text.size();++i) {
            const auto c=static_cast<unsigned char>(text[i]);
            if(c<'0' || c>'9' || number>(std::numeric_limits<::cw::lib::Integer>::max()-(c-'0'))/10) valid=false;
            else number=number*10+(c-'0');
        }
        if(negative) number=-number;
        const auto holder=dynamic_cast<CardHolder*>(this);
        const auto maximum=std::max<::cw::lib::Integer>(1,holder->m_page_count);
        if(valid && clamp_page) number=std::clamp<::cw::lib::Integer>(number,1,maximum);
        if(valid && (number<1 || number>maximum)) valid=false;
        if(valid) {
            const auto canonical=std::to_string(number);
            if(candidate.edit.text!=canonical) {
                const auto removed=candidate.edit.text.size()>canonical.size()?candidate.edit.text.size()-canonical.size():0;
                candidate.edit.text=canonical;
                candidate.edit.caret=std::min(canonical.size(),candidate.edit.caret>removed?candidate.edit.caret-removed:0);
                candidate.edit.anchor=std::min(canonical.size(),candidate.edit.anchor>removed?candidate.edit.anchor-removed:0);
                if(empty) {candidate.edit.anchor=0;candidate.edit.caret=canonical.size();}
                if(canonical==input->edit.text) {
                    candidate.edit.undo=input->edit.undo;candidate.edit.redo=input->edit.redo;
                }
            }
        }
    }
    if(valid) {
        input->edit=std::move(candidate.edit);
        if(copied) input->write(*copied);
        publish_field_input();
    } else {
        input->edit.composition.clear();
        request_dialog_presentation_refresh();
    }
    return true;
}


std::vector<::cw::dialog::DialogPresentationControl> CardControl::control_layout()
{
    using Action=::cw::dialog::DialogPresentationAction;
    std::vector<::cw::dialog::DialogPresentationControl> controls;
    const auto panel_w=cs(panel_width(*this)),panel_h=cs(kPanelHeight);
    const auto add=[&](Action action,int index,const ::cw::lib::Rect& rect,bool enabled=true) {
        if(rect.width>0 && rect.height>0) {
            controls.push_back({action,index,static_cast<float>(rect.x),static_cast<float>(rect.y),
                static_cast<float>(rect.width),static_cast<float>(rect.height),enabled});
            // 押しボタンと切り替えボタンには共通のホバー表示を適用する。 移動先コ
            // ンボ（0）、絞り込み欄（12）、ページラベル（15）には適用しない。
            controls.back().hover_highlight=
                !(action==Action::Auxiliary && (index==0 || index==12 || index==15));
        }
    };
    const auto navigation_width = navigation_button_width(*this);
    add(Action::Previous, -1, {0, panel_h, navigation_width, cs(kBottomBarHeight)}, navigation_enabled());
    add(Action::Next, -1, {panel_w - navigation_width, panel_h, navigation_width, cs(kBottomBarHeight)}, navigation_enabled());
    add(Action::Cancel, -1, {(panel_w - cs(90)) / 2, panel_h + cs(3), cs(90), cs(kFooterButtonHeight)});

    const auto toolbar = compute_toolbar_layout(*this, panel_w, panel_h);
    add(Action::Auxiliary, 0, toolbar.combo, m_sendto);
    add(Action::Auxiliary, 1, toolbar.rsmall, m_sendto);
    add(Action::Auxiliary, 2, toolbar.lsmall, m_sendto);
    add(Action::Auxiliary, 3, toolbar.editstar);
    add(Action::Auxiliary, 4, toolbar.sortwithstar);
    add(Action::Auxiliary, 5, toolbar.cardtype[0]);
    add(Action::Auxiliary, 6, toolbar.cardtype[1]);
    add(Action::Auxiliary, 7, toolbar.cardtype[2]);
    add(Action::Auxiliary, 8, toolbar.addctrl);
    add(Action::Auxiliary, 9, toolbar.redeal);
    add(Action::Auxiliary, 10, toolbar.sort, m_sort_enabled);
    add(Action::Auxiliary, 11, toolbar.narrow_type);
    add(Action::Auxiliary, 12, toolbar.narrow);

    if (auto holder = dynamic_cast<CardHolder*>(this)) {
        const bool show_addctrlbtn = ::cw::g_cwpy && ::cw::g_cwpy->m_setting &&
            ::cw::g_cwpy->m_setting->m_show_addctrlbtn;
        const auto up_y = show_addctrlbtn ? cs(40) : cs(50);
        const auto down_y = up_y + cs(170);
        if (m_callname == "CARDPOCKET") {
            const bool personal = holder->is_showpersonal();
            const auto base_y = personal ? cs(37) : cs(64);
            const int count = personal ? 4 : 3;
            for (int index = 0; index < count; ++index)
                add(Action::Auxiliary, 20 + index,
                    {cs(10), base_y + cs(54) * index, cs(74), cs(54)});
            if (expanded_pc_paging(*this)) {
                const auto pages = expanded_pc_page_layout();
                add(Action::Auxiliary, 40, pages[0], holder->m_index > 0);
                add(Action::Auxiliary, 41, pages[2], holder->m_index+1 < holder->m_page_count);
                if (pocket_reorder_available(*this))
                    add(Action::Auxiliary, 42, pocket_reorder_rect());
            }
        } else if (replacement_card_paging(*this)) {
            const auto pages=expanded_pc_page_layout();
            const auto replacement=dynamic_cast<ReplCardHolder*>(this);
            add(Action::Auxiliary,40,pages[0],replacement && replacement->m_index>0);
            add(Action::Auxiliary,41,pages[2],replacement &&
                replacement->m_index+1<replacement->candidate_page_count());
        } else if (m_callname != "HANDVIEW" && m_callname != "CARDPOCKET_REPLACE") {
            add(Action::Auxiliary, 13, {cs(10), up_y, cs(70), cs(40)},chain_enabled(Action::Auxiliary,13));
            add(Action::Auxiliary, 14, {cs(10), down_y, cs(70), cs(40)},chain_enabled(Action::Auxiliary,14));
        }
    }
    if(dynamic_cast<CardHolder*>(this) && m_show_additional_controls)
        add(Action::Auxiliary,15,toolbar.page);
    for(auto& c:controls) {
        c.continuous_pointer=true; // ネイティブ風のフォーカス・押下・解除状態は共通モデルで管理する。
        if(chain_button(c.action,c.index)) c.enabled=c.enabled && chain_enabled(c.action,c.index);
    }
    return controls;
}

std::vector<CardControl::FocusTarget> CardControl::focus_targets()
{
    using Action=::cw::dialog::DialogPresentationAction;
    // wx の子コントロール生成順を維持し、toppanel より前に生成される下部パ
    // ネルも含める。 初期フォーカスは最初の子ではなく、toppanel 自体に置く。
    const std::vector<FocusTarget> order={
        {Action::Cancel,-1},{Action::Previous,-1},{Action::Next,-1},
        {Action::Auxiliary,10},{Action::Auxiliary,4},{Action::Auxiliary,3},
        {Action::Auxiliary,5},{Action::Auxiliary,6},{Action::Auxiliary,7},
        {Action::Auxiliary,2},{Action::Auxiliary,0},{Action::Auxiliary,1},
        {Action::Auxiliary,8},{Action::Auxiliary,12},{Action::Auxiliary,11},
        {Action::Auxiliary,20},{Action::Auxiliary,21},{Action::Auxiliary,22},{Action::Auxiliary,23},
        {Action::Auxiliary,40},{Action::Auxiliary,41},
        {Action::Auxiliary,13},{Action::Auxiliary,15},{Action::Auxiliary,14},{Action::Auxiliary,9}};
    std::vector<FocusTarget> result;
    if(m__quit || m__proc || m_cardinfo_dialog) return result;
    const auto controls=control_layout();
    for(const auto target:order)
        if(std::any_of(controls.begin(),controls.end(),[&](const auto& c) {
            return c.enabled && c.action==target.first && c.index==target.second;
        })) result.push_back(target);
    return result;
}

void CardControl::retire_keyboard_press()
{
    if(m_space_target) {m_space_target.reset();request_dialog_presentation_refresh();}
    // キーを離すまで m_space_held を保持し、キーリピートで終了済みの押下を再開しない。
}

void CardControl::focus_control(std::optional<FocusTarget> target,bool keyboard)
{
    if(target) {
        const auto targets=focus_targets();
        if(std::find(targets.begin(),targets.end(),*target)==targets.end()) target.reset();
    }
    if(m_focus_target!=target) {
        retire_keyboard_press();
        m_choice_prefix.clear();m_choice_composition.clear();
        if(m_input_focus==15) commit_page_input();
        const bool same_edit=target && target->first==::cw::dialog::DialogPresentationAction::Auxiliary &&
            target->second==m_input_focus;
        if(!same_edit) retire_field_inputs();
        m_focus_target=target;
    }
    m_keyboard_focus=keyboard;
    if(target && target->first==::cw::dialog::DialogPresentationAction::Auxiliary &&
       (target->second==12 || target->second==15)) {
        const bool entering=m_input_focus!=target->second;
        m_input_focus=target->second;
        sync_field_inputs();
        if(entering && m_input_focus==15) {
            m_page_editor.edit.anchor=0;m_page_editor.edit.move(m_page_editor.edit.text.size(),true);
        }
    }
    request_dialog_presentation_refresh();
}

void CardControl::focus_next(int direction)
{
    using Action=::cw::dialog::DialogPresentationAction;
    const auto targets=focus_targets();
    if(targets.empty()) {focus_control({},true);return;}
    auto found=m_focus_target?std::find(targets.begin(),targets.end(),*m_focus_target):targets.end();
    if(found==targets.end()) {
        // toppanel から順方向へ進むと最初の子へ移り、逆方
        // 向では先に生成された下部パネルの最後の有効な子へ移る。
        if(direction>0)
            found=std::find_if(targets.begin(),targets.end(),[](const auto& t){return t.first==Action::Auxiliary;});
        else {
            const auto reverse=std::find_if(targets.rbegin(),targets.rend(),[](const auto& t){return t.first!=Action::Auxiliary;});
            if(reverse!=targets.rend()) found=std::prev(reverse.base());
        }
        if(found==targets.end()) found=direction>0?targets.begin():std::prev(targets.end());
    } else {
        const auto count=static_cast<std::ptrdiff_t>(targets.size());
        found=targets.begin()+(std::distance(targets.begin(),found)+direction+count)%count;
    }
    focus_control(*found,true);
}

void CardControl::decorate_keyboard_controls(::cw::dialog::DialogPresentation& view,
    const std::shared_ptr<::cw::lib::Surface>& surface)
{
    using Action=::cw::dialog::DialogPresentationAction;
    const auto targets=focus_targets();
    if(m_focus_target && std::find(targets.begin(),targets.end(),*m_focus_target)==targets.end())
        focus_control({},false);
    if(m_keyboard_scale!=cardcontrol_ui_scale()) {
        retire_keyboard_press();m_keyboard_scale=cardcontrol_ui_scale();
    }
    if(navigation_input_blocked()) retire_keyboard_press();
    const auto live=control_layout();
    if(m_space_target && std::none_of(live.begin(),live.end(),[&](const auto& c){return c==*m_space_target;}))
        retire_keyboard_press();
    if(m_button_capture && (navigation_input_blocked() || m_button_scale!=cardcontrol_ui_scale() ||
        std::find(live.begin(),live.end(),*m_button_capture)==live.end()))
        m_button_stale=true;

    for(auto& c:view.controls) {
        if(c.action!=Action::Auxiliary) continue;
        if(c.index==3) c.tooltip=cardcontrol_msg("edit_star","カードにスターをつける");
        else if(c.index==4) c.tooltip=cardcontrol_msg("sort_with_star","スターつきのカードを先頭に表示");
        else if(c.index>=5 && c.index<=7) {
            const auto name=cardcontrol_msg(c.index==5?"skillcard":c.index==6?"itemcard":"beastcard",
                c.index==5?"特殊技能":c.index==6?"アイテム":"召喚獣");
            c.tooltip=cardcontrol_msg("show_object","%sを表示");
            const auto pos=c.tooltip.find("%s");
            if(pos!=std::string::npos)c.tooltip.replace(pos,2,name);
        } else if(c.index==8)c.tooltip=cardcontrol_msg("show_additional_controls","拡張機能欄を表示");
        else if(c.index==9)c.tooltip=cardcontrol_msg("re_deal","手札を捨てて新しくカードを引く");
    }
    if(!surface || !m_focus_target || m_choice_popup>=0) return;
    const auto it=std::find_if(live.begin(),live.end(),[&](const auto& c) {
        return c.enabled && c.action==m_focus_target->first && c.index==m_focus_target->second;
    });
    if(it==live.end()) return;
    const auto& c=*it;
    const ::cw::lib::Rect rect{static_cast<::cw::lib::Integer>(c.x),static_cast<::cw::lib::Integer>(c.y),
        static_cast<::cw::lib::Integer>(c.width),static_cast<::cw::lib::Integer>(c.height)};
    if(m_space_target || (m_button_capture && m_button_inside && !m_button_stale))
        ::cw::lib::ui::draw_sunken_border(surface,rect);
    if(!m_keyboard_focus || (c.action==Action::Auxiliary && (c.index==12 || c.index==15))) return;
    const auto inset=cs(3);
    for(auto x=rect.x+inset;x<rect.x+rect.width-inset;x+=2)
        for(auto y:{rect.y+inset,rect.y+rect.height-inset-1})
            ::cw::lib::ui::fill_rect(surface,{x,y,1,1},{0,0,0,255});
    for(auto y=rect.y+inset;y<rect.y+rect.height-inset;y+=2)
        for(auto x:{rect.x+inset,rect.x+rect.width-inset-1})
            ::cw::lib::ui::fill_rect(surface,{x,y,1,1},{0,0,0,255});
}

::cw::dialog::DialogPresentation CardControl::dialog_presentation()
{
    using Action = ::cw::dialog::DialogPresentationAction;
    if(m__quit || m__proc || m_cardinfo_dialog) retire_field_inputs();
    m_narrow_input.begin_frame();m_page_editor.begin_frame();
    sync_field_inputs();
    process_ui_events();
    if(m_presentation_scale!=cardcontrol_ui_scale()) {
        m_presentation_scale=cardcontrol_ui_scale();
        if(m_ui_backend) m_ui_backend->invalidate(*this);
    }
    if(navigation_input_blocked() || (m_chain_capture &&
        (m_chain_scale!=cardcontrol_ui_scale() || !chain_enabled(m_chain_capture->action,m_chain_capture->index))))
        retire_chain_input();
    if(m_chain_running && !m_chain_stale && m_chain_capture &&
        m_chain_timer.consume(card_input_time(),::cw::dialog::dialog_page_repeat_interval())) {
        const auto target=*m_chain_capture;
        dispatch_chain(target.action,target.index);
        if(navigation_input_blocked() || !chain_enabled(target.action,target.index)) retire_chain_input();
    }
    m_chain_controls.clear();
    if (m_ui_backend && (m_ui_invalidated || !m_ui_surface))
        m_ui_backend->paint(*this, nullptr);

    ::cw::dialog::DialogPresentation result;
    result.route = m_callname.empty() ? "BACKPACK" : m_callname;
    result.title = m_title;
    result.surface = m_ui_surface;
    result.damage = dialog_retained_surface().damage;
    result.surface_revision = dialog_retained_surface().revision;
    // クリック・配布の四フレーム間隔を維持する。入力操作のコールバックがない時は、Python
    // のダイアログと同じ 33ms 間隔でアニメーションを確認する。 静止カードは上の m__dialog_anim_cardimgs
    // から除外し、タイマー処理を要求しない。
    result.refresh_interval_ms = platform_refresh_interval(
        !m_ui_callbacks.empty() ? 16U : !m__dialog_anim_cardimgs.empty() ? 33U : 0U);
    if (!m_ui_surface) return result;

    for (std::size_t index = 0; index < m_draw_headers.size(); ++index) {
        const auto* object = std::get_if<::cw::lib::ObjectPtr>(&m_draw_headers[index].storage());
        if (!object || !*object) continue;
        const auto rect = m_card_rects.find(object->get());
        if (rect == m_card_rects.end()) continue;
        result.controls.push_back({Action::Select, static_cast<int>(index),
            static_cast<float>(cs(rect->second.x)), static_cast<float>(cs(rect->second.y)),
            static_cast<float>(cs(rect->second.width)), static_cast<float>(cs(rect->second.height)), true, true});
        // Engine のテーブルと同様、長押しは右クリック相当のカード情報表示として扱う。
        result.controls.back().long_press_context = true;
    }
    // 並べ替え矢印はカード枠をまたぐため、隣のカードより先に当たり判定を行う
    // 。 スター・並べ替え・カードの優先順位は引き続きモデル側で解決する。
    if (_can_repls()) {
        auto candidates = get_headers();
        const auto previous = get_beforepageheaders();
        if (!previous.empty()) candidates.insert(candidates.begin(), previous.back());
        for (std::size_t i = 0; i < candidates.size(); ++i) {
            const auto [rect, unused_x, unused_y] = _get_replsrect(candidates[i]);
            if (rect.width <= 0 || rect.height <= 0) continue;
            result.controls.push_back({Action::Auxiliary, reorder_item + static_cast<int>(i),
                static_cast<float>(cs(rect.x)), static_cast<float>(cs(rect.y)),
                static_cast<float>(cs(rect.width)), static_cast<float>(cs(rect.height)), true, true});
        }
    }

    const auto panel_w = cs(panel_width(*this));
    const auto panel_h = cs(kPanelHeight);
    const auto add = [&](Action action, int index, const ::cw::lib::Rect& rect, bool enabled = true) {
        if (rect.width > 0 && rect.height > 0)
            result.controls.push_back({action, index, static_cast<float>(rect.x),
                static_cast<float>(rect.y), static_cast<float>(rect.width),
                static_cast<float>(rect.height), enabled});
    };
    const auto toolbar=compute_toolbar_layout(*this,panel_w,panel_h);
    const auto layout=control_layout();
    result.controls.insert(result.controls.end(),layout.begin(),layout.end());
    for(auto& control:result.controls) if(chain_button(control.action,control.index)) {
        control.enabled=control.enabled && chain_enabled(control.action,control.index);
        control.continuous_pointer=true;
        m_chain_controls.push_back(control);
    }
    // 共通の保持型描画面には、固定の枠と入力欄の値が既に含まれる。 全体
    // の一時描画面を確保するのは、実際に重ねる表示がある時だけとする。通
    // 常のカードのホバー・クリックアニメーションでは、 毎フレーム数 MB
    // のキャンバス全体をコピーせず、小さな更新領域だけを公開する。
    const bool transient_overlay=m_choice_popup>=0 || active_field() ||
        m_narrow_input.menu.opened() || m_page_editor.menu.opened() ||
        m_space_target || m_button_capture ||
        (m_keyboard_focus && m_focus_target) ||
        (m_chain_running && !m_chain_stale && m_chain_capture);
    auto surface = transient_overlay
        ? std::make_shared<::cw::lib::Surface>(*m_ui_surface)
        : m_ui_surface;
    if(transient_overlay) ::cw::lib::ui::inherit_surface_animation(surface,m_ui_surface);
    result.surface = surface;
    if(m_chain_running && !m_chain_stale && m_chain_capture) {
        const auto& control=*m_chain_capture;
        ::cw::lib::ui::draw_sunken_border(surface,{static_cast<::cw::lib::Integer>(control.x),
            static_cast<::cw::lib::Integer>(control.y),static_cast<::cw::lib::Integer>(control.width),
            static_cast<::cw::lib::Integer>(control.height)});
    }
    if(transient_overlay) {
        for (int kind : {0, 10, 11}) {
            const auto rows = measured_choices(*this, kind);
            const auto selected = control_choice_index(*this, kind);
            if (selected >= 0 && selected < static_cast<int>(rows.size()))
                control_field(*this,surface, choice_rect(toolbar, kind), rows[selected].text, m_choice_popup == kind, true,false,rows[selected].bitmap);
        }
        control_field(*this,surface,toolbar.narrow,"",m_input_focus==12);
        if(toolbar.narrow.width>0)
            m_narrow_input.draw(surface,toolbar.narrow,m_input_focus==12,static_cast<int>(cs(3)),"paneltitle",15);
        if (auto holder=dynamic_cast<CardHolder*>(this);holder && m_show_additional_controls && toolbar.page.width>0) {
            control_field(*this,surface,toolbar.page,"",m_input_focus==15);
            m_page_editor.draw(surface,toolbar.page,m_input_focus==15,static_cast<int>(cs(3)),"paneltitle",17,true);
        }
    }
    for(auto& c:result.controls) if(c.action==Action::Auxiliary && (c.index==12 || c.index==15))
        c.continuous_pointer=true;
    if(!m_show_additional_controls || (m_input_focus==12 && !toolbar.narrow.width) ||
       (m_input_focus==15 && !toolbar.page.width)) retire_field_inputs();
    decorate_keyboard_controls(result,surface);
    append_side_controls(result);
    if(auto input=active_field()) {
        if(input==&m_narrow_input) m_page_editor.retire();else m_narrow_input.retire();
        input->finish_frame(result,surface,true,true);
        result.input_text=input->edit.text;
        if(input->menu.opened()) return result;
    } else {
        retire_field_inputs();result.wants_text_input=false;
    }
    if(m__quit || m__proc || m_cardinfo_dialog)
        for(auto& c:result.controls) c.enabled=false;

    if(m_choice_drag && m_choice_drag_scale!=cardcontrol_ui_scale())m_choice_drag_stale=true;
    m_choice_scrollbar={};m_choice_thumb={};m_choice_rows.clear();
    m_choice_rows_scale=cardcontrol_ui_scale();
    dialog_retained_surface().set_component_hits("dropdown.rows",{});
    if (m_choice_popup >= 0) {
        const auto rows = measured_choices(*this,m_choice_popup);
        if(rows.empty() || !choice_enabled(*this,m_choice_popup)) {
            m_choice_popup=-1;m_choice_drag.reset();
        } else {
            const int count=static_cast<int>(rows.size());
            m_choice_cursor=std::clamp(m_choice_cursor,0,count-1);
            m_choice_first=std::clamp(m_choice_first,0,m_choice_cursor);
            const auto origin=choice_rect(toolbar,m_choice_popup);
            const auto below=surface->size.height-origin.y-origin.height;
            const auto above=origin.y;
            const auto cap=std::max<::cw::lib::Integer>(1,std::min(cs(14*20),std::max(below,above)));
            const auto through_cursor=[&] {
                ::cw::lib::Integer h=0;
                for(int i=m_choice_first;i<=m_choice_cursor;++i)h+=rows[i].height;
                return h;
            };
            while(m_choice_first<m_choice_cursor && through_cursor()>cap)++m_choice_first;
            ::cw::lib::Integer height=0,width=origin.width,total=0;
            for(const auto& row:rows) {width=std::max(width,row.width+cs(6));total+=row.height;}
            m_choice_visible=0;
            for(int i=m_choice_first;i<count;++i) {
                if(height+rows[i].height>cap && m_choice_visible)break;
                height+=std::min(rows[i].height,cap);++m_choice_visible;
                if(height>=cap)break;
            }
            const bool scrolling=total>height;
            const auto barwidth=scrolling?cs(12):0;
            width=std::min(surface->size.width,width+barwidth);
            const auto x=std::clamp<::cw::lib::Integer>(origin.x,0,surface->size.width-width);
            const auto y=below>=height?origin.y+origin.height:std::max<::cw::lib::Integer>(0,origin.y-height);
            add(Action::Auxiliary,choice_dismiss,{0,0,surface->size.width,surface->size.height});
            std::vector<::cw::lib::ui::RetainedSurface::ComponentHitRegion> popup_hits;
            ::cw::lib::Integer offset=0;
            for(int i=m_choice_first;i<m_choice_first+m_choice_visible;++i) {
                const auto rowheight=std::min(rows[i].height,height-offset);
                const ::cw::lib::Rect rect{x,y+offset,width-barwidth,rowheight};
                control_field(*this,surface,rect,rows[i].text,false,false,i==m_choice_cursor,rows[i].bitmap,true);
                add(Action::Auxiliary,choice_item+i,rect);
                // wxVListBoxComboPopup と同様、全体が見えている行だけを強調表示する。
                if(rowheight==rows[i].height) {
                    m_choice_rows.push_back(result.controls.back());
                    popup_hits.push_back({rect,i,true});
                }
                offset+=rowheight;
            }
            define_dialog_component("dropdown.popup",{x,y,width,height});
            dialog_retained_surface().set_component_hits(
                "dropdown.rows",std::move(popup_hits));
            if(scrolling) {
                m_choice_scrollbar={x+width-barwidth,y,barwidth,height};
                const auto thumbheight=std::min(height,std::max(cs(10),height*m_choice_visible/count));
                const auto maxfirst=std::max(1,count-m_choice_visible);
                m_choice_thumb={m_choice_scrollbar.x,y+(height-thumbheight)*std::min(m_choice_first,maxfirst)/maxfirst,
                    barwidth,thumbheight};
                ::cw::lib::ui::fill_rect(surface,m_choice_scrollbar,{235,235,235,255});
                ::cw::lib::ui::fill_rect(surface,m_choice_thumb,{145,145,145,255});
                add(Action::Auxiliary,choice_scroll,m_choice_scrollbar);
                result.controls.back().continuous_pointer=true;
            }
        }
    }
    if(!m__quit && !m__proc && !m_cardinfo_dialog && (m_choice_popup>=0 ||
       (m_focus_target && m_focus_target->first==Action::Auxiliary &&
        (m_focus_target->second==0 || m_focus_target->second==10 || m_focus_target->second==11)))) {
        result.wants_text_input=true;result.text_input_hardware_only=true;
        result.composition=m_choice_composition;
    }
    return result;
}

void CardControl::dialog_presentation_action(const ::cw::dialog::DialogPresentationAction action,
                                             const int index)
{
    using Action = ::cw::dialog::DialogPresentationAction;
    if(m__quit || m__proc || m_cardinfo_dialog) {retire_chain_input();retire_field_inputs();return;}
    if(!m_chain_dispatching) retire_chain_input();
    if(index==-2 && (action==Action::Previous || action==Action::Next)) {
        if(!m_side_pointer || side_action_at(m_side_pointer->first,m_side_pointer->second)!=action) return;
        commit_page_input();retire_field_inputs();
        if(action==Action::Previous) OnClickLeftBtn(nullptr);else OnClickRightBtn(nullptr);
        request_dialog_presentation_refresh();return;
    }
    sync_field_inputs();
    auto input=active_field();
    if(input && input->menu.opened()) {
        edit_field([&](auto& field){return field.menu.action(action,index,field.edit,field.read,field.write);},true);
        return;
    }
    if(::cw::dialog::textedit::FilterInput::menu_control(action,index)) return;
    if(input && !input->edit.composition.empty()) {
        if(action==Action::Cancel) {input->retire();request_dialog_presentation_refresh();return;}
        if(action==Action::Accept || action==Action::Select || action==Action::Activate) return;
        input->retire();
    }
    if (m_choice_popup >= 0) {
        const int kind = m_choice_popup;
        m_choice_popup = -1;m_choice_drag.reset();m_choice_prefix.clear();m_choice_composition.clear();
        if (action == Action::Auxiliary && index >= choice_item)
            apply_control_choice(*this, kind, index - choice_item);
        else if (action == Action::Accept || action == Action::Activate)
            apply_control_choice(*this, kind, m_choice_cursor);
        request_dialog_presentation_refresh();
        return;
    }
    if(chain_button(action,index) && !chain_enabled(action,index)) return;
    const FocusTarget target{action,index};
    const auto targets=focus_targets();
    if(std::find(targets.begin(),targets.end(),target)!=targets.end())
        focus_control(target,m_keyboard_focus);
    else if(action==Action::Select || action==Action::Activate) focus_control({},false);
    if (action == Action::Accept) { dialog_presentation_enter(0); return; }
    if (action != Action::Auxiliary || (index != 12 && index != 15)) {
        if (m_input_focus == 15 && action != Action::Cancel) commit_page_input();
        retire_field_inputs();
    }
    auto event = std::make_shared<::cw::lib::Window>();
    event->id = index;
    if (action == Action::Cancel) OnCancel(event);
    else if (action == Action::Previous) OnClickLeftBtn(event);
    else if (action == Action::Next) OnClickRightBtn(event);
    else if (action == Action::Select) OnLeftUp(event);
    else if (action == Action::Auxiliary) {
        if (index == 0 || index == 10 || index == 11) {
            if (choice_enabled(*this, index) && !control_choices(*this, index).empty()) {
                m_choice_popup = index;m_choice_first=0;m_choice_drag.reset();m_choice_rows.clear();
                m_choice_prefix.clear();m_choice_composition.clear();
                m_choice_cursor = control_choice_index(*this, index);
            }
        }
        else if (index == 1) OnClickRightBtn2(event);
        else if (index == 2) OnClickLeftBtn2(event);
        else if (index == 3) OnEditStar(event);
        else if (index == 4) OnSortWithStar(event);
        else if (index == 5) OnShowSkill(event);
        else if (index == 6) OnShowItem(event);
        else if (index == 7) OnShowBeast(event);
        else if (index == 8) OnAdditionalControls(event);
        else if (index == 9) { if (auto hand = dynamic_cast<HandView*>(this)) hand->OnReDeal(event); }
        else if (index == 12 && m_show_additional_controls && _can_narrow()) {
            if (m_input_focus == 15) commit_page_input();
            m_page_editor.retire();m_input_focus=12;
        } else if (index == 15 && m_show_additional_controls) {
            const auto toolbar = compute_toolbar_layout(*this, cs(panel_width(*this)), cs(kPanelHeight));
            if (auto holder = dynamic_cast<CardHolder*>(this); holder && toolbar.page.width > 0) {
                const bool entering=m_input_focus!=15;
                m_narrow_input.retire();m_input_focus=15;
                sync_field_inputs();
                if(entering) {m_page_editor.edit.anchor=0;m_page_editor.edit.move(m_page_editor.edit.text.size(),true);}
            }
        } else if (index == 13) {
            if (auto holder = dynamic_cast<CardHolder*>(this)) holder->OnClickUpBtn(event);
        } else if (index == 14) {
            if (auto holder = dynamic_cast<CardHolder*>(this)) holder->OnClickDownBtn(event);
        } else if (index >= 20 && index < 24) {
            event->id = index - 20;
            if (auto holder = dynamic_cast<CardHolder*>(this)) holder->OnClickToggleBtn(event);
        } else if ((index == 40 || index == 41) &&
                   (expanded_pc_paging(*this) || replacement_card_paging(*this))) {
            if (auto holder = dynamic_cast<CardHolder*>(this)) {
                event->id = holder->m_index + 1 + (index == 40 ? -1 : 1);
                holder->OnPageNum(event);
            } else if(auto replacement=dynamic_cast<ReplCardHolder*>(this))
                replacement->change_candidate_page(index==40?-1:1);
        } else if (index == 42 && pocket_reorder_available(*this)) {
            if (auto holder = dynamic_cast<CardHolder*>(this)) open_pocket_reorder(*holder);
        }
    }
    if (m_ui_backend && !m__quit) m_ui_backend->invalidate(*this);
    request_dialog_presentation_refresh();
}

void CardControl::dialog_presentation_text_event(const ::cw::dialog::DialogTextEditingEvent& event) {
    const int kind=m_choice_popup>=0?m_choice_popup:
        m_focus_target && m_focus_target->first==::cw::dialog::DialogPresentationAction::Auxiliary?
            m_focus_target->second:-1;
    if(!m__quit && !m__proc && !m_cardinfo_dialog && !active_field() &&
       (kind==0 || kind==10 || kind==11) && choice_enabled(*this,kind)) {
        if(event.composition) {m_choice_composition=event.text;request_dialog_presentation_refresh();return;}
        m_choice_composition.clear();
        if(event.text.empty())return;
        const auto choices=control_choices(*this,kind);
        if(choices.empty())return;
        const auto text=::cw::lib::encoding::lower(event.text);
        const auto now=card_input_time();
        if(now-m_choice_prefix_time>std::chrono::seconds(1))m_choice_prefix.clear();
        const bool cycle=m_choice_prefix==text;
        m_choice_prefix=cycle?text:m_choice_prefix+text;
        m_choice_prefix_time=now;
        const int current=m_choice_popup>=0?m_choice_cursor:control_choice_index(*this,kind);
        const int count=static_cast<int>(choices.size());
        const auto search=[&](const std::string& prefix,bool next) {
            for(int step=next?1:0;step<(next?count+1:count);++step) {
                const auto index=(std::max(0,current)+step)%count;
                if(::cw::lib::encoding::lower(choices[index]).starts_with(prefix))return index;
            }
            return -1;
        };
        int found=search(m_choice_prefix,cycle);
        if(found<0) {m_choice_prefix=text;found=search(text,true);}
        if(found>=0) {
            if(m_choice_popup>=0)m_choice_cursor=found;
            else apply_control_choice(*this,kind,found);
        }
        request_dialog_presentation_refresh();return;
    }
    edit_field([&](auto& field) {
        if(m_input_focus==15 && !event.composition && event.text!="\b" &&
           !std::all_of(event.text.begin(),event.text.end(),[](unsigned char c){return c>='0' && c<='9';})) {
            field.edit.composition.clear();return true;
        }
        if(m_input_focus==15 && !event.composition && event.text=="0" &&
           field.edit.caret==0 && field.edit.anchor==0) return true;
        return field.text(event.text,event.composition,event.start,event.length);
    });
}
void CardControl::dialog_presentation_text(const std::string& text,bool composition) {
    dialog_presentation_text_event({text,composition,-1,0});
}
void CardControl::commit_page_input() {
    if(m_input_focus!=15) return;
    if(auto holder=dynamic_cast<CardHolder*>(this)) {
        if(!m_page_input.empty()) {
            const auto value=std::strtoll(m_page_input.c_str(),nullptr,10);
            if(value>=1 && value<=holder->m_page_count && value!=holder->m_index+1) {
                auto event=std::make_shared<::cw::lib::Window>();event->id=value;
                holder->OnPageNum(event);
            }
        }
        m_page_input=std::to_string(holder->m_index+1);
        m_page_editor.sync(m_page_input);
    }
}

void CardControl::dialog_presentation_enter(::cw::dialog::DialogPresentationModifiers modifiers)
{
    dialog_presentation_key(::cw::dialog::DialogPresentationKey::Enter, true, modifiers);
}

void CardControl::dialog_presentation_key(::cw::dialog::DialogPresentationKey key, bool pressed,
                                         ::cw::dialog::DialogPresentationModifiers modifiers)
{
    using Key = ::cw::dialog::DialogPresentationKey;
    using Action=::cw::dialog::DialogPresentationAction;
    if(key==Key::Space && !pressed) {
        const auto target=std::exchange(m_space_target,std::nullopt);
        m_space_held=false;
        if(target) {
            request_dialog_presentation_refresh();
            const auto live=control_layout();
            if(!navigation_input_blocked() && m_keyboard_scale==cardcontrol_ui_scale() &&
               m_focus_target==FocusTarget{target->action,target->index} &&
               std::find(live.begin(),live.end(),*target)!=live.end())
                dialog_presentation_action(target->action,target->index);
        }
        return;
    }
    if(!pressed) return;
    if(m__quit || m__proc || m_cardinfo_dialog) {
        retire_keyboard_press();if(key==Key::Space)m_space_held=true;return;
    }
    const bool ctrl = (modifiers & ::cw::dialog::DialogPresentationModifierControl) != 0;
    const bool alt = (modifiers & ::cw::dialog::DialogPresentationModifierAlt) != 0;
    retire_chain_input();
    if(key==Key::Escape && !m_choice_composition.empty()) {
        m_choice_composition.clear();request_dialog_presentation_refresh();return;
    }
    if(key==Key::Escape) {dialog_presentation_action(::cw::dialog::DialogPresentationAction::Cancel,-1);return;}
    if(!m_show_additional_controls) retire_field_inputs();
    if(auto field=active_field();field && (field->menu.opened() || !field->edit.composition.empty())) {
        const bool menu=field->menu.opened();
        edit_field([&](auto& input){return input.key(key,modifiers);},menu);return;
    }
    if(key==Key::Space && m_space_held) return;
    if(!m_choice_composition.empty())return; // コンボボックス内の移動より先に、IME の確定・取消を処理する。
    if (m_choice_popup >= 0) {
        const int kind=m_choice_popup;
        if(key==Key::Space) m_space_held=true;
        const auto choices=control_choices(*this,kind);
        if(choices.empty()) {m_choice_popup=-1;m_choice_drag.reset();}
        else if(!m_choice_composition.empty()) return;
        else if(kind==0 && !ctrl && (key==Key::F4 || (alt && (key==Key::Up || key==Key::Down)))) {
            // Python の画像付き移動先選択と同様、wxVListBoxComboPopup::OnKey の動
            // 作に合わせる。 ポップアップの切り替えでは DismissWithEvent() を呼ばずに閉じる。
            dialog_presentation_action(Action::Cancel,-1);
        } else if(key==Key::Tab && !ctrl && !alt) {
            const int selected=m_choice_cursor;
            m_choice_popup=-1;m_choice_drag.reset();
            apply_control_choice(*this,kind,selected);
            focus_control(FocusTarget{Action::Auxiliary,kind},true);
            focus_next((modifiers & ::cw::dialog::DialogPresentationModifierShift)?-1:1);
        } else if(key==Key::Enter || key==Key::Space || key==Key::F4 || (alt && key==Key::Up))
            dialog_presentation_action(Action::Accept,-1);
        else {
            if(key==Key::Up) --m_choice_cursor;
            else if(key==Key::Down) ++m_choice_cursor;
            else if(key==Key::Home) m_choice_cursor=0;
            else if(key==Key::End)m_choice_cursor=static_cast<int>(choices.size())-1;
            else if(key==Key::PageUp)m_choice_cursor-=std::max(1,m_choice_visible-1);
            else if(key==Key::PageDown)m_choice_cursor+=std::max(1,m_choice_visible-1);
            m_choice_cursor=std::clamp(m_choice_cursor,0,static_cast<int>(choices.size())-1);
        }
        request_dialog_presentation_refresh();return;
    }
    if(!active_field() && m_focus_target && m_focus_target->first==Action::Auxiliary &&
       (m_focus_target->second==0 || m_focus_target->second==10 || m_focus_target->second==11)) {
        const int kind=m_focus_target->second;
        if((alt && key==Key::Down) || (!ctrl && !alt && key==Key::F4)) {
            dialog_presentation_action(Action::Auxiliary,kind);return;
        }
        if(!ctrl && !alt && (key==Key::Home || key==Key::End || key==Key::PageUp || key==Key::PageDown)) {
            const auto choices=control_choices(*this,kind);
            if(!choices.empty()) {
                const int last=static_cast<int>(choices.size())-1;
                const int current=control_choice_index(*this,kind);
                const int selected=key==Key::Home?0:key==Key::End?last:
                    std::clamp(current+(key==Key::PageUp?-1:1)*std::max(1,m_choice_visible-1),0,last);
                apply_control_choice(*this,kind,selected);
            }
            request_dialog_presentation_refresh();return;
        }
    }
    if(key==Key::Tab && !ctrl && !alt) {
        retire_keyboard_press();
        focus_next((modifiers & ::cw::dialog::DialogPresentationModifierShift)?-1:1);
        return;
    }
    if(key!=Key::Space) retire_keyboard_press();
    // 編集中は選択カードを実行しない。既存の選択画面・デバッグ用アクセラレーター
    // は、 IME や編集メニューが入力を扱っていない時に引き続き利用できる。
    if(active_field()) {
        if(key==Key::Enter) {
            commit_page_input();
            if(m_input_focus==15) {m_page_editor.edit.anchor=0;m_page_editor.edit.move(m_page_editor.edit.text.size(),true);}
            request_dialog_presentation_refresh();return;
        }
        if(key==Key::ContextMenu || (key==Key::F10 && (modifiers & ::cw::dialog::DialogPresentationModifierShift)))
            (void)dialog_presentation();
        if(edit_field([&](auto& input){return input.key(key,modifiers);})) return;
        if(!ctrl && !alt) return;
    }
    if(key==Key::Space && !ctrl && !alt) {
        if(m_space_held) return;
        m_space_held=true;
        const auto live=control_layout();
        if(m_focus_target) {
            const auto found=std::find_if(live.begin(),live.end(),[&](const auto& c) {
                return c.enabled && c.action==m_focus_target->first && c.index==m_focus_target->second;
            });
            if(found!=live.end()) {
                m_keyboard_focus=true;m_keyboard_scale=cardcontrol_ui_scale();m_space_target=*found;
                request_dialog_presentation_refresh();
            }
        }
        return;
    }
    auto event = std::make_shared<::cw::lib::Window>();
    if ((ctrl || alt) && key >= Key::Digit1 && key <= Key::Digit9) {
        apply_control_choice(*this, ctrl ? 11 : 10, static_cast<int>(key) - static_cast<int>(Key::Digit1));
    } else if (ctrl && key == Key::F && !m_addctrlbtn.is_none()) {
        OnToggleAdditionalControls(event); m_input_focus = -1;
    } else if (ctrl && key == Key::D) OnDebugMode(event);
    else if (ctrl && key == Key::Left) {if(navigation_enabled()) OnClickLeftBtn(event);}
    else if (ctrl && key == Key::Right) {if(navigation_enabled()) OnClickRightBtn(event);}
    else if (ctrl && key == Key::Up) OnClickLeftBtn2(event);
    else if (ctrl && key == Key::Down) OnClickRightBtn2(event);
    else if (key == Key::Up) {if(m_callname=="CARDPOCKET" || chain_enabled(::cw::dialog::DialogPresentationAction::Auxiliary,13)) OnUp(event);}
    else if (key == Key::Down) {if(m_callname=="CARDPOCKET" || chain_enabled(::cw::dialog::DialogPresentationAction::Auxiliary,14)) OnDown(event);}
    else if (key == Key::Enter || key == Key::Left || key == Key::Right) {
        event->id = int_value(key == Key::Enter ? (ctrl ? m_infokeyid : m_returnkeyid) :
            key == Key::Left ? m_leftkeyid : m_rightkeyid, -1);
        OnKeyDown(event);
    }
    if (m_ui_backend && !m__quit) m_ui_backend->invalidate(*this);
    request_dialog_presentation_refresh();
}

void CardControl::dialog_presentation_scroll(float x, float y, int lines)
{
    if (!lines || m__quit || m__proc || m_cardinfo_dialog || !std::isfinite(x) || !std::isfinite(y) ||
        x<0 || y<0 || x>=cs(panel_width(*this)) || y>=cs(kDialogHeight)) return;
    retire_chain_input();
    if(auto field=active_field()) {
        if(field->menu.scroll(lines,field->edit)) {request_dialog_presentation_refresh();return;}
        if(!field->edit.composition.empty()) return;
    }
    if (m_choice_popup >= 0) {
        const auto choices = control_choices(*this, m_choice_popup);
        if (!choices.empty()) m_choice_cursor = std::clamp(m_choice_cursor + (lines > 0 ? 1 : -1), 0, static_cast<int>(choices.size()) - 1);
    } else {
        const auto toolbar = compute_toolbar_layout(*this, cs(panel_width(*this)), cs(kPanelHeight));
        for (int kind : {0, 10, 11}) if (control_contains(choice_rect(toolbar, kind), x, y)) {
            const auto choices = control_choices(*this, kind);
            if (choice_enabled(*this,kind) && !choices.empty()) {
                const int count=static_cast<int>(choices.size());
                const int current=std::clamp(control_choice_index(*this,kind),0,count-1);
                apply_control_choice(*this,kind,(current+(lines>0?1:count-1))%count);
            }
            request_dialog_presentation_refresh(); return;
        }
        if (m_input_focus == 15) commit_page_input();
        m_input_focus = -1;
        auto event = std::make_shared<::cw::lib::Window>(); event->id = lines;
        event->bounds.x=static_cast<::cw::lib::Integer>(std::floor(x/cardcontrol_ui_scale()));
        event->bounds.y=static_cast<::cw::lib::Integer>(std::floor(y/cardcontrol_ui_scale()));
        OnMouseWheel(event);
    }
    request_dialog_presentation_refresh();
}

void CardControl::dialog_presentation_pointer(::cw::dialog::DialogPresentationAction action,
    int index, float x, float y, bool pressed)
{
    using Action = ::cw::dialog::DialogPresentationAction;
    if(action==Action::Auxiliary && index==choice_scroll) {
        if(!pressed) {m_choice_drag.reset();m_choice_drag_stale=false;return;}
        if(m_choice_drag_stale)return;
        if(m_choice_popup<0 || m__quit || m__proc || m_cardinfo_dialog ||
           !std::isfinite(x) || !std::isfinite(y) || m_choice_scrollbar.height<=0) {
            m_choice_drag_stale=true;return;
        }
        if(m_choice_drag && m_choice_drag_scale!=cardcontrol_ui_scale()) {m_choice_drag_stale=true;return;}
        if(!m_choice_drag) {
            m_choice_drag_scale=cardcontrol_ui_scale();
            m_choice_drag=control_contains(m_choice_thumb,x,y)?
                y-static_cast<float>(m_choice_thumb.y):static_cast<float>(m_choice_thumb.height)/2;
        }
        const auto count=static_cast<int>(control_choices(*this,m_choice_popup).size());
        const auto travel=std::max<::cw::lib::Integer>(1,m_choice_scrollbar.height-m_choice_thumb.height);
        const auto ratio=std::clamp((y-*m_choice_drag-static_cast<float>(m_choice_scrollbar.y))/travel,0.0f,1.0f);
        m_choice_first=static_cast<int>(std::lround(ratio*std::max(0,count-m_choice_visible)));
        m_choice_cursor=m_choice_first;
        request_dialog_presentation_refresh();return;
    }
    if(pressed) {
        retire_keyboard_press();
        if(!navigation_input_blocked() && std::isfinite(x) && std::isfinite(y)) {
            const auto live=control_layout();
            const auto found=std::find_if(live.begin(),live.end(),[&](const auto& c) {
                return c.enabled && c.action==action && c.index==index &&
                    x>=c.x && y>=c.y && x<c.x+c.width && y<c.y+c.height;
            });
            if(found!=live.end()) {
                if(action==Action::Auxiliary && (index==12 || index==15)) {
                    m_focus_target=FocusTarget{action,index};m_keyboard_focus=false;
                } else focus_control(FocusTarget{action,index},false);
            } else if(action==Action::Select || index==-2) focus_control({},false);
        }
    }
    if(index==-2 && (action==Action::Previous || action==Action::Next)) {
        const auto hit=side_action_at(x,y);
        if(pressed) {
            if(!m_side_capture) {
                m_side_capture=action;m_side_capture_scale=cardcontrol_ui_scale();
                m_side_stale=hit!=action;
            }
            if(!std::isfinite(x) || !std::isfinite(y) || navigation_input_blocked() ||
               !side_click_option() || !navigation_enabled() || m_side_capture_scale!=cardcontrol_ui_scale()) retire_side_input();
            return;
        }
        const auto captured=m_side_capture;
        // Python の OnLeftUp と同様、一つの上部パネル上で解除位置を評価する。
        // 空白から空白へのドラッグは反対側で終わる場合もあり、この時は押下操作を実行していない。
        const bool valid=captured && !m_side_stale && m_side_capture_scale==cardcontrol_ui_scale() && hit;
        m_side_capture.reset();m_side_stale=false;
        if(valid) dialog_presentation_action(*hit,-2);
        return;
    }
    const bool ordinary_button=action==Action::Cancel ||
        (action==Action::Auxiliary && ((index>=0 && index<=11 && index!=1 && index!=2) ||
                                      (index>=20 && index<=23)));
    if(ordinary_button) {
        const auto live=control_layout();
        const auto found=std::find_if(live.begin(),live.end(),[&](const auto& c) {
            return c.enabled && c.action==action && c.index==index;
        });
        const bool inside=found!=live.end() && std::isfinite(x) && std::isfinite(y) &&
            x>=found->x && y>=found->y && x<found->x+found->width && y<found->y+found->height;
        if(pressed) {
            if(!m_button_capture) {
                if(!inside || navigation_input_blocked()) return;
                m_button_capture=*found;m_button_scale=cardcontrol_ui_scale();m_button_stale=false;
            }
            if(navigation_input_blocked() || m_button_scale!=cardcontrol_ui_scale() ||
               found==live.end() || *found!=*m_button_capture) m_button_stale=true;
            m_button_inside=inside;request_dialog_presentation_refresh();return;
        }
        const bool valid=m_button_capture && !m_button_stale && !navigation_input_blocked() &&
            m_button_scale==cardcontrol_ui_scale() && inside && *found==*m_button_capture;
        m_button_capture.reset();m_button_inside=false;m_button_stale=false;
        request_dialog_presentation_refresh();
        if(valid) dialog_presentation_action(action,index);
        return;
    }
    if(chain_button(action,index)) {
        if(!pressed) {dialog_presentation_pointer_cancel();return;} // ボタンを離したらキャプチャーを解除するだけとし、追加の一段送りは行わない。
        if(navigation_input_blocked() || !std::isfinite(x) || !std::isfinite(y)) {retire_chain_input();return;}
        if(m_chain_capture && m_chain_scale!=cardcontrol_ui_scale()) retire_chain_input();
        if(m_chain_stale) return;
        if(!m_chain_capture && m_chain_controls.empty()) (void)dialog_presentation();
        const auto found=std::find_if(m_chain_controls.begin(),m_chain_controls.end(),[&](const auto& control) {
            return control.action==action && control.index==index && control.enabled;
        });
        const bool inside=found!=m_chain_controls.end() && x>=found->x && y>=found->y &&
            x<found->x+found->width && y<found->y+found->height && chain_enabled(action,index);
        if(m_chain_capture) {
            if(!inside || m_chain_capture->action!=action || m_chain_capture->index!=index) retire_chain_input();
            return; // 領域を離れたら停止し、再び入っても同じ押下を再開しない。
        }
        if(!inside) return;
        m_chain_capture=*found;m_chain_scale=cardcontrol_ui_scale();
        m_chain_stale=false;
        dispatch_chain(action,index);
        if(!navigation_input_blocked() && m_chain_capture && !m_chain_stale && chain_enabled(action,index)) {
            m_chain_running=true;
            m_chain_timer.start(card_input_time(),::cw::dialog::dialog_page_repeat_interval());
        }
        request_dialog_presentation_refresh();return;
    }

    if(action==Action::Auxiliary && (index==12 || index==15)) {
        if(m__quit || m__proc || m_choice_popup>=0 || m_cardinfo_dialog || !m_show_additional_controls) return;
        auto& field=index==12?m_narrow_input:m_page_editor;
        if((index==12 && !_can_narrow()) || field.menu.opened() || !field.edit.composition.empty()) return;
        if(!field.visible || (pressed && !field.dragging && !field.contains(x,y))) return;
        if(!pressed && !field.dragging) return;
        sync_field_inputs();m_pointer_card.reset();m_pointer_reorder=false;
        if(pressed && index==15 && m_input_focus!=15) {
            m_narrow_input.retire();m_input_focus=15;
            field.edit.anchor=0;field.edit.move(field.edit.text.size(),true);
            request_dialog_presentation_refresh();return;
        }
        if(field.pointer(x,y,pressed)) {
            if(index==12) m_page_editor.retire();else m_narrow_input.retire();
            m_input_focus=index;request_dialog_presentation_refresh();
        }
        return;
    }
    if((m_narrow_input.menu.opened() || m_page_editor.menu.opened()) &&
       (action==Action::Select || (action==Action::Auxiliary && index>=reorder_item))) {
        m_pointer_card.reset();return;
    }
    if(auto field=active_field();field && !field->edit.composition.empty()) {
        m_pointer_card.reset();return;
    }
    if (action != Action::Select && !(action == Action::Auxiliary && index >= reorder_item)) {
        DialogPresentationProvider::dialog_presentation_pointer(action, index, x, y, pressed);
        return;
    }
    if (m__quit || m__proc || m_choice_popup >= 0 || m_cardinfo_dialog) {
        m_pointer_card.reset(); return;
    }
    const float lx = static_cast<float>(x / cardcontrol_ui_scale());
    const float ly = static_cast<float>(y / cardcontrol_ui_scale());
    if (pressed) {
        if (m_pointer_card) return; // 新たな押下ではなく、ドラッグの継続として扱う。
        const bool reorder = action == Action::Auxiliary;
        auto candidates = reorder ? get_headers() : m_draw_headers;
        if (reorder) {
            const auto previous = get_beforepageheaders();
            if (!previous.empty()) candidates.insert(candidates.begin(), previous.back());
        }
        const int row = reorder ? index - reorder_item : index;
        if (row < 0 || row >= static_cast<int>(candidates.size())) return;
        ::cw::lib::Rect rect;
        if (reorder) rect = std::get<0>(_get_replsrect(candidates[row]));
        else {
            const auto* object = std::get_if<::cw::lib::ObjectPtr>(&candidates[row].storage());
            if (!object || !*object) return;
            const auto it = m_card_rects.find(object->get());
            if (it == m_card_rects.end()) return;
            rect = it->second;
        }
        if (!control_contains(rect, lx, ly)) return;
        m_pointer_card = candidates[row];
        m_pointer_bounds = rect;
        m_pointer_reorder = reorder;
        dialog_presentation_hover(x, y);
        return;
    }
    const auto card = std::move(m_pointer_card);
    m_pointer_card.reset();
    if (!card || !control_contains(m_pointer_bounds, lx, ly)) return;
    auto current = get_headers();
    if (m_pointer_reorder) {
        const auto previous = get_beforepageheaders();
        if (!previous.empty()) current.insert(current.begin(), previous.back());
    }
    if (std::none_of(current.begin(), current.end(), [&](const auto& value) { return same_value(value, *card); })) return;
    if (m_input_focus == 15) commit_page_input();
    retire_field_inputs();
    auto event = std::make_shared<::cw::lib::Window>();
    event->id = -1; // 位置に基づくスター・並べ替えの当たり判定を維持し、位置番号だけの高速経路は使わない。
    event->bounds.x = static_cast<::cw::lib::Integer>(std::floor(lx));
    event->bounds.y = static_cast<::cw::lib::Integer>(std::floor(ly));
    OnLeftUp(event);
    request_dialog_presentation_refresh();
}

void CardControl::dialog_presentation_hover(float x, float y)
{
    if(!std::isfinite(x) || !std::isfinite(y)) {m_side_pointer.reset();return;}
    if (m__quit || m__proc || m_cardinfo_dialog) return;
    if(m_choice_popup>=0) {
        // 公開済みの行矩形を共有し、ポインター移動中にフォントや画像の処理を行わない。 ホ
        // バーで変えるのは候補行だけとし、OnSendTo/OnSort は実行しない。
        if(m_choice_drag || m_choice_drag_stale || !m_choice_composition.empty() ||
           m_choice_rows_scale!=cardcontrol_ui_scale() || !choice_enabled(*this,m_choice_popup))return;
        if(const auto candidate=dialog_retained_surface().component_hit("dropdown.rows",x,y)) {
            if(*candidate!=m_choice_cursor) {
                m_choice_cursor=static_cast<int>(*candidate);
                invalidate_dialog_component("dropdown.popup");
                request_dialog_presentation_refresh();
            }
            return;
        }
        return;
    }
    if(auto field=active_field();field && field->menu.opened()) {
        if(field->menu.hover(x,y,field->edit)) request_dialog_presentation_refresh();
        return;
    }
    auto event = std::make_shared<::cw::lib::Window>();
    event->bounds.x = static_cast<::cw::lib::Integer>(std::floor(x / cardcontrol_ui_scale()));
    event->bounds.y = static_cast<::cw::lib::Integer>(std::floor(y / cardcontrol_ui_scale()));
    OnMove(event);
    m_side_pointer=std::pair{x,y};
}

void CardControl::dialog_presentation_context(float x,float y) {
    if(m__quit || m__proc || m_cardinfo_dialog) return;
    retire_side_input();
    m_pointer_card.reset();m_pointer_reorder=false;
    if(auto field=active_field();field && field->menu.opened()) {
        field->menu.close();request_dialog_presentation_refresh();return;
    }
    if(m_choice_popup>=0) {dialog_presentation_action(::cw::dialog::DialogPresentationAction::Cancel,-1);return;}
    (void)dialog_presentation();
    if(m_show_additional_controls) for(int index:{12,15}) {
        auto& field=index==12?m_narrow_input:m_page_editor;
        if(!field.contains(x,y)) continue;
        if(!field.edit.composition.empty()) return;
        if(index==12 && !_can_narrow()) return;
        if(index==12) m_page_editor.retire();else m_narrow_input.retire();
        if(index==15 && m_input_focus!=15) {field.edit.anchor=0;field.edit.move(field.edit.text.size(),true);}
        m_input_focus=index;field.open(x,y);request_dialog_presentation_refresh();return;
    }
    commit_page_input();retire_field_inputs();
    auto event=std::make_shared<::cw::lib::Window>();event->id=-1;
    event->bounds.x=static_cast<::cw::lib::Integer>(std::floor(x/cardcontrol_ui_scale()));
    event->bounds.y=static_cast<::cw::lib::Integer>(std::floor(y/cardcontrol_ui_scale()));
    OnRightUp(event);request_dialog_presentation_refresh();
}

bool CardControl::dialog_presentation_finished() const noexcept { return m__quit; }
void CardControl::dialog_presentation_complete() {}

void main() {}

}  // namespace cw::dialog::cardcontrol
