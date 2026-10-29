// UIの実装は一つに統一し、
// 別経路の実装を設けない。
#include "cw/dialog/charainfo.hpp"

#include "cw/binary/image.hpp"
#include "cw/character.hpp"
#include "cw/cw.hpp"
#include "cw/data.hpp"
#include "cw/dialog/cardinfo.hpp"
#include "cw/dialog/create.hpp"
#include "cw/dialog/edit.hpp"
#include "cw/dialog/message.hpp"
#include "cw/debug/charaedit.hpp"
#include "cw/debug/edit.hpp"
#include "cw/debug/statusedit.hpp"
#include "cw/frame.hpp"
#include "cw/features.hpp"
#include "cw/header.hpp"
#include "cw/image.hpp"
#include "cw/animatedimage.hpp"
#include "cw/imageretouch.hpp"
#include "cw/lib/ui_surface.hpp"
#include "cw/lib/simd_image.hpp"
#include "cw/lib/encoding.hpp"
#include "cw/lib/python_integer.hpp"
#include "cw/lib/python_text.hpp"
#include "cw/setting.hpp"
#include "cw/sprite/card.hpp"
#include "cw/thread.hpp"
#include "cw/util.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>

namespace cw::dialog::charainfo {
namespace {

using ElementPtr = std::shared_ptr<::cw::data::CWPyElement>;
using CharacterPtr = std::shared_ptr<::cw::character::Character>;
using AlbumPtr = std::shared_ptr<::cw::character::AlbumPage>;
using HeaderPtr = std::shared_ptr<::cw::header::CardHeader>;

template <typename T>
std::shared_ptr<T> object_value(const ::cw::lib::Value& value)
{
    const auto* object = std::get_if<::cw::lib::ObjectPtr>(&value.storage());
    if (!object || !*object) return {};
    return std::dynamic_pointer_cast<T>(*object);
}

::cw::lib::String value_string(const ::cw::lib::Value& value)
{
    if (const auto* text = std::get_if<::cw::lib::String>(&value.storage())) return *text;
    if (const auto* number = std::get_if<::cw::lib::Integer>(&value.storage())) return std::to_string(*number);
    if (const auto* real = std::get_if<::cw::lib::Real>(&value.storage())) return std::to_string(*real);
    if (const auto* flag = std::get_if<bool>(&value.storage())) return *flag ? "True" : "False";
    return {};
}

::cw::lib::Integer value_integer(const ::cw::lib::Value& value, ::cw::lib::Integer fallback = 0)
{
    if (const auto* number = std::get_if<::cw::lib::Integer>(&value.storage())) return *number;
    if (const auto* real = std::get_if<::cw::lib::Real>(&value.storage())) return static_cast<::cw::lib::Integer>(*real);
    try {
        const auto text = value_string(value);
        if (!text.empty()) return std::stoll(text);
    } catch (...) {
    }
    return fallback;
}

bool value_bool(const ::cw::lib::Value& value, bool fallback = false)
{
    if (const auto* flag = std::get_if<bool>(&value.storage())) return *flag;
    const auto text = value_string(value);
    if (text == "True" || text == "true" || text == "1" || text == "yes") return true;
    if (text == "False" || text == "false" || text == "0" || text == "no") return false;
    return fallback;
}

ElementPtr value_root(const ::cw::lib::Value& value)
{
    const auto tree = object_value<::cw::data::CWPyElementTree>(value);
    if (tree) return tree->getroot();
    const auto element = object_value<::cw::data::CWPyElement>(value);
    if (element) return element;
    const auto character = object_value<::cw::character::Character>(value);
    if (character) return value_root(character->m_data);
    const auto album = object_value<::cw::character::AlbumPage>(value);
    if (album) return value_root(album->m_data);
    return {};
}

ElementPtr find_element(const ::cw::lib::Value& value, const ::cw::lib::String& path)
{
    const auto root = value_root(value);
    if (!root) return {};
    if (path == "." || path.empty()) return root;
    const auto found = root->find(path);
    return found && *found ? *found : ElementPtr{};
}

::cw::lib::Integer element_value(const ElementPtr& element)
{
    if (!element) throw std::invalid_argument("character history coupon is missing");
    const auto text = element->get("value", "");
    const auto normalized = ::cw::lib::normalize_python_integer(text);
    if (!normalized) throw std::invalid_argument("invalid character history coupon value: " + text);
    std::size_t used = 0;
    const auto value = std::stoll(*normalized, &used, 10);
    if (used != normalized->size())
        throw std::invalid_argument("invalid character history coupon value: " + text);
    return static_cast<::cw::lib::Integer>(value);
}

bool debug_mode()
{
    return ::cw::g_cwpy && ::cw::g_cwpy->is_debugmode();
}

CharacterPtr character_value(const ::cw::lib::Value& value)
{
    return object_value<::cw::character::Character>(value);
}

AlbumPtr album_value(const ::cw::lib::Value& value)
{
    return object_value<::cw::character::AlbumPage>(value);
}

::cw::lib::String character_showing_name(const ::cw::lib::Value& value)
{
    if (const auto character = character_value(value)) return character->get_showingname();
    if (const auto album = album_value(value)) return album->get_showingname();
    return {};
}

::cw::lib::Integer character_level(const ::cw::lib::Value& value)
{
    if (const auto character = character_value(value)) return character->m_level;
    if (const auto album = album_value(value)) return value_integer(album->m_level, 1);
    return 0;
}

std::map<::cw::lib::String, ::cw::lib::Integer> character_special_coupons(
    const ::cw::lib::Value& value)
{
    if (const auto character = character_value(value)) return character->get_specialcoupons();
    if (const auto album = album_value(value)) return album->get_specialcoupons();
    return {};
}

HeaderPtr header_value(const ::cw::lib::Value& value)
{
    return object_value<::cw::header::CardHeader>(value);
}

::cw::lib::String header_description(const HeaderPtr& header)
{
    return header ? header->m_desc : ::cw::lib::String{};
}

::cw::lib::String header_type(const HeaderPtr& header, ::cw::lib::String fallback = "Item")
{
    if (!header) return fallback;
    if (header->m_type == "SkillCard") return "Skill";
    if (header->m_type == "ItemCard") return "Item";
    if (header->m_type == "BeastCard") return "Beast";
    return fallback;
}

::cw::lib::String pocket_path(const HeaderPtr& header)
{
    const auto type = header_type(header);
    if (type == "Skill") return "SkillCards";
    if (type == "Beast") return "BeastCards";
    return "ItemCards";
}

::cw::lib::Integer pocket_for_type(const ::cw::lib::String& type)
{
    if (type == "Skill") return ::cw::g_POCKET_SKILL;
    if (type == "Beast") return ::cw::g_POCKET_BEAST;
    return ::cw::g_POCKET_ITEM;
}

::cw::lib::String wrap_lines(const ::cw::lib::String& text, std::size_t width)
{
    if (text.empty() || width == 0) return text;
    // ここでは UTF-8 のバイト数で数えない。日本語や絵文字は複数バイ
    // トになる場合があり、 その途中へ改行を入れると不正な UTF-8 に
    // なる。Python のダイアログと同じ折り返し実装である util::txtwrap
    // を使い、書記素・コードポイント単位で処理する。
    return ::cw::util::txtwrap(text, 0, static_cast<::cw::lib::Integer>(width), {}, false, nullptr);
}

::cw::lib::String coupon_suffix(bool debug, const ::cw::lib::String& text, ::cw::lib::Integer value)
{
    if (!debug || text == "：Ｒ") return {};
    if (text.rfind("：", 0) == 0 || text.rfind("；", 0) == 0) return value > 0 ? "(" + std::to_string(value) + "ラウンド)" : ::cw::lib::String{};
    return value >= 0 ? "(+" + std::to_string(value) + ")" : "(" + std::to_string(value) + ")";
}

::cw::lib::String coupon_prefix(bool debug, ::cw::lib::Integer value)
{
    if (debug) return {};
    if (value >= 2) return "(@)";
    if (value >= 1) return "(+)";
    if (value >= 0) return "( )";
    return "(X)";
}

struct SearchMatch {
    ::cw::lib::String source;
    ::cw::lib::String pocket;
    HeaderPtr header;
};

struct SearchMatchList : ::cw::lib::Object {
    std::vector<SearchMatch> values;
};

std::shared_ptr<SearchMatchList> matches_from(const ::cw::lib::Value& value)
{
    return object_value<SearchMatchList>(value);
}

::cw::lib::String map_string(const std::map<::cw::lib::String, ::cw::lib::Value>& map, const ::cw::lib::String& key)
{
    const auto it = map.find(key);
    return it == map.end() ? ::cw::lib::String{} : value_string(it->second);
}

bool map_bool(const std::map<::cw::lib::String, ::cw::lib::Value>& map, const ::cw::lib::String& key, bool fallback = false)
{
    const auto it = map.find(key);
    return it == map.end() ? fallback : value_bool(it->second, fallback);
}

void set_map_string(std::map<::cw::lib::String, ::cw::lib::Value>& map, const ::cw::lib::String& key, const ::cw::lib::String& value)
{
    map[key] = ::cw::lib::Value(value);
}

void set_map_bool(std::map<::cw::lib::String, ::cw::lib::Value>& map, const ::cw::lib::String& key, bool value)
{
    map[key] = ::cw::lib::Value(value);
}

std::vector<HeaderPtr> character_headers(const CharacterPtr& character, ::cw::lib::Integer pocket)
{
    // Python の DeckDialog と同様、trade() が移動する実体のヘッダーを Character.cardpocket
    // から取得する。 load_cardpocket_from_xml() は XML から複製を再構
    // 築するため、それを trade() に渡しても移動せず、読み込みが終わらないように見えてしまう。
    if (!character || pocket < 0 || pocket > 2) return {};
    return character->get_pocketcards(pocket);
}

bool deck_personal_owner_is_self(const HeaderPtr& header, const CharacterPtr& self)
{
    if (!header || !self) return false;
    const auto owner = header->m_personal_owner.lock();
    if (!owner) return false;
    if (owner.get() == self.get()) return true;
    if (const auto player = std::dynamic_pointer_cast<::cw::character::Player>(self))
        return static_cast<const ::cw::character::Player*>(owner.get()) == player.get();
    return false;
}

bool deck_header_allowed(const HeaderPtr& header, const CharacterPtr& self)
{
    if (!header) return false;
    if (header->m_personal_owner.expired()) return true;
    return deck_personal_owner_is_self(header, self);
}

using DeckItemTuple = std::tuple<::cw::lib::String, ::cw::lib::String, ::cw::lib::String,
                                 ::cw::lib::String, ::cw::lib::String, ::cw::lib::String>;

std::vector<DeckItemTuple> deck_registration_items(
    const CharacterPtr& character, const std::map<::cw::lib::String, ::cw::lib::Value>& registration)
{
    std::vector<DeckItemTuple> items;
    if (!character) return items;
    const auto name = map_string(registration, "name");
    const auto type = map_string(registration, "type");
    for (const auto& typed : character->m_deck_registrations_typed) {
        if (typed.name != name || typed.type != type) continue;
        for (const auto& item : typed.items)
            items.emplace_back(item.name, item.desc, item.type, item.scenario, item.author, item.personal);
        return items;
    }
    return items;
}

::cw::lib::String deck_ppath_to_type(const ::cw::lib::String& path, const ::cw::lib::String& fallback)
{
    if (path == "SkillCards" || path.ends_with("SkillCards")) return "Skill";
    if (path == "ItemCards" || path.ends_with("ItemCards")) return "Item";
    if (path == "BeastCards" || path.ends_with("BeastCards")) return "Beast";
    return fallback;
}

bool deck_header_is_limited_beast(const HeaderPtr& header)
{
    return header && header->m_type == "BeastCard" && header->m_maxuselimit > 0;
}

::cw::lib::String deck_trim(const ::cw::lib::String& text)
{
    return ::cw::lib::python_strip(text);
}

std::size_t deck_utf8_units(const ::cw::lib::String& text)
{
    std::size_t count = 0;
    for (const unsigned char ch : text)
        if ((ch & 0xC0) != 0x80) ++count;
    return count;
}

::cw::lib::String deck_pad_right(const ::cw::lib::String& text, std::size_t width)
{
    const auto units = deck_utf8_units(text);
    if (units >= width) return text;
    return text + ::cw::lib::String(width - units, ' ');
}

std::shared_ptr<::cw::character::Player> deck_as_player(const CharacterPtr& character)
{
    return std::dynamic_pointer_cast<::cw::character::Player>(character);
}

std::shared_ptr<::cw::data::Party> deck_party()
{
    if (!::cw::g_cwpy || !::cw::g_cwpy->m_ydata || !*::cw::g_cwpy->m_ydata) return {};
    const auto& yado = **::cw::g_cwpy->m_ydata;
    if (!yado.m_party || !*yado.m_party) return {};
    return *yado.m_party;
}

void deck_trade_backpack(const HeaderPtr& header)
{
    if (!header || !::cw::g_cwpy) return;
    const auto party = deck_party();
    const auto toindex = party ? static_cast<::cw::lib::Integer>(party->m_backpack.size()) : 0;
    std::optional<std::shared_ptr<::cw::data::Party>> party_arg;
    if (party) party_arg = party;
    ::cw::g_cwpy->trade("BACKPACK", std::nullopt, header, true, nullptr, toindex, -1,
                        false, true, party_arg, false, true, true, true, 0);
}

void deck_trade_playercard(const CharacterPtr& character, const HeaderPtr& header)
{
    if (!character || !header || !::cw::g_cwpy) return;
    const auto party = deck_party();
    std::optional<std::shared_ptr<::cw::data::Party>> party_arg;
    if (party) party_arg = party;
    const auto target = ::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(character));
    ::cw::g_cwpy->trade("PLAYERCARD", target, header, true, nullptr, -1, -1,
                        false, true, party_arg, false, true, true, true, 1);
}

::cw::lib::String deck_load_summary(const ::cw::lib::String& registration_name,
                                    const std::vector<std::map<::cw::lib::String, ::cw::lib::Value>>& rows)
{
    std::vector<::cw::lib::String> skill_col;
    std::vector<::cw::lib::String> item_col;
    std::vector<::cw::lib::String> beast_col;
    std::vector<::cw::lib::String> personal_col;
    for (const auto& row : rows) {
        const auto line = "- " + map_string(row, "name") + ": " + map_string(row, "status");
        if (map_bool(row, "personal")) personal_col.push_back(line);
        else if (map_string(row, "type") == "Skill") skill_col.push_back(line);
        else if (map_string(row, "type") == "Beast") beast_col.push_back(line);
        else item_col.push_back(line);
    }
    std::vector<std::pair<::cw::lib::String, std::vector<::cw::lib::String>>> sections;
    if (!skill_col.empty()) sections.emplace_back("[スキル]", skill_col);
    if (!item_col.empty()) sections.emplace_back("[アイテム]", item_col);
    if (!beast_col.empty()) sections.emplace_back("[召喚獣]", beast_col);
    if (!personal_col.empty()) sections.emplace_back("[私有物]", personal_col);

    ::cw::lib::String summary = "登録名: " + registration_name + "\n\n";
    for (std::size_t si = 0; si < sections.size(); ++si) {
        summary += sections[si].first + "\n";
        const auto& items = sections[si].second;
        std::vector<std::pair<::cw::lib::String, ::cw::lib::String>> pairs;
        for (std::size_t i = 0; i < items.size(); i += 2)
            pairs.emplace_back(items[i], i + 1 < items.size() ? items[i + 1] : ::cw::lib::String{});
        std::size_t left_width = 0;
        for (const auto& [left, unused] : pairs) {
            (void)unused;
            left_width = std::max(left_width, deck_utf8_units(left));
        }
        for (const auto& [left, right] : pairs) {
            if (!right.empty()) summary += deck_pad_right(left, left_width) + "  " + right + "\n";
            else summary += left + "\n";
        }
        if (si + 1 != sections.size()) summary += "\n";
    }
    summary += "\nこの登録を読み込みますか？";
    return summary;
}

bool deck_common_message(const ::cw::lib::String& title,
                         const ::cw::lib::String& text,
                         const bool confirmation,
                         const bool scale_to_game_window = false)
{
    auto dialog = std::make_shared<::cw::dialog::message::SysMessage>();
    std::vector<::cw::dialog::message::MessageChoice> choices;
    if (confirmation) {
        choices = {
            {"はい(&Y)", ::cw::dialog::message::kWxIdYes, 105, {}},
            {"いいえ(&N)", ::cw::dialog::message::kWxIdNo, 105, {}}};
    } else {
        choices = {{"OK", ::cw::dialog::message::kWxIdOk, 105, {}}};
    }
    // PC のデッキ読み込み結果表示では、意図的に仕様を変更する。Python の cw/dialog/charainfo.py
    // は デスクトップの ppis()/DPI に従う wx.MessageDialog
    // を使い、cw.UP_WIN は適用しない。 C++ では同じ SysMessage
    // の経路を使いつつ、CardWirth ウィンドウを拡大した
    // 時の読みやすさのため、ゲームウィンドウの拡大率を適用する。
    dialog->set_game_window_scale(scale_to_game_window);
    dialog->initialize({}, title, text, std::move(choices), {});
    if (!::cw::dialog::show_common_modal_presentation(dialog)) return false;
    return confirmation
        ? dialog->m_return_code == ::cw::dialog::message::kWxIdYes
        : dialog->m_return_code == ::cw::dialog::message::kWxIdOk;
}

std::optional<::cw::lib::String> deck_common_text_input(
    const ::cw::lib::String& title, const ::cw::lib::String& message,
    const ::cw::lib::String& initial)
{
    auto dialog = std::make_shared<::cw::dialog::edit::InputTextDialog>();
    dialog->initialize({}, title, message, initial, 0, {}, std::nullopt);
    if (!::cw::dialog::show_common_modal_presentation(dialog) ||
        !dialog->m_presentation_accepted) return std::nullopt;
    return deck_trim(dialog->m_text);
}

std::vector<HeaderPtr> all_character_headers(const CharacterPtr& character)
{
    std::vector<HeaderPtr> result;
    for (::cw::lib::Integer pocket = 0; pocket != 3; ++pocket) {
        const auto values = character_headers(character, pocket);
        result.insert(result.end(), values.begin(), values.end());
    }
    return result;
}

void invalidate_panel(::cw::lib::Value& value)
{
    const auto object = std::get_if<::cw::lib::ObjectPtr>(&value.storage());
    if (!object || !*object) return;
    if (const auto card_panel = std::dynamic_pointer_cast<CardPanel>(*object)) card_panel->draw(true);
    else if (const auto history_panel = std::dynamic_pointer_cast<HistoryPanel>(*object)) history_panel->draw(true);
    else if (const auto desc_panel = std::dynamic_pointer_cast<DescPanel>(*object)) desc_panel->draw(true);
    else if (const auto status_panel = std::dynamic_pointer_cast<StatusPanel>(*object)) status_panel->draw(true);
    else if (const auto edit_panel = std::dynamic_pointer_cast<EditPanel>(*object)) edit_panel->draw(true);
}

void cycle_index(::cw::lib::Integer& index, std::size_t count, ::cw::lib::Integer delta)
{
    if (count == 0) { index = 0; return; }
    const auto n = static_cast<::cw::lib::Integer>(count);
    index = (index + delta) % n;
    if (index < 0) index += n;
}

::cw::lib::Value load_adventurer_value(const std::shared_ptr<::cw::header::AdventurerHeader>& header)
{
    try {
        return read_adventurer_character(header);
    } catch (...) {
        return {};
    }
}

void write_character_value(const ::cw::lib::Value& value)
{
    const auto character = character_value(value);
    if (!character) return;
    const auto tree = object_value<::cw::data::CWPyElementTree>(character->m_data);
    if (tree && !tree->m_fpath.empty()) tree->write_xml(false);
}

void refresh_character_panels(CharaInfo& info)
{
    info.m_published_controls_dirty=true;
    info.m_scroll_y = 0;
    info.m_page_scrolls.assign(info.m_bottompanel.size(), 0);
    const auto character = character_value(info.m_ccard);
    const auto showing_name = character_showing_name(info.m_ccard);
    info.m_title_text = showing_name.empty() ? "Character" : showing_name;
    if (info.m_toppanel) { info.m_toppanel->m_ccard = info.m_ccard; info.m_toppanel->draw(true); }
    for (auto& value : info.m_bottompanel) {
        const auto object = std::get_if<::cw::lib::ObjectPtr>(&value.storage());
        if (!object || !*object) continue;
        if (const auto desc = std::dynamic_pointer_cast<DescPanel>(*object)) { desc->m_ccard = info.m_ccard; desc->draw(true); }
        else if (const auto history = std::dynamic_pointer_cast<HistoryPanel>(*object)) { history->m_ccard = info.m_ccard; history->draw(true); }
        else if (const auto status = std::dynamic_pointer_cast<StatusPanel>(*object)) { status->m_ccard = info.m_ccard; status->draw(true); }
        else if (const auto edit = std::dynamic_pointer_cast<EditPanel>(*object)) {
            edit->m_ccard = info.m_ccard;
            if (const auto standby = dynamic_cast<StandbyCharaInfo*>(&info)) {
                edit->m_parent_index = standby->m_index;
                if (edit->m_party_mode) {
                    edit->m_charalist.clear();
                    const auto current = object_value<::cw::character::Player>(info.m_ccard);
                    const auto current_tree = current ? object_value<::cw::data::CWPyElementTree>(current->m_data) : nullptr;
                    for (const auto& header : standby->m_headers) {
                        const auto header_path = header ? value_string(header->m_fpath) : ::cw::lib::String{};
                        if (current && current_tree && current_tree->m_fpath == header_path) {
                            edit->m_charalist.push_back(current);
                        } else if (const auto loaded = object_value<::cw::character::Player>(load_adventurer_value(header))) {
                            edit->m_charalist.push_back(loaded);
                        }
                    }
                }
            }
            edit->draw(true);
        }
        else if (const auto cards = std::dynamic_pointer_cast<CardPanel>(*object)) { cards->m_ccard = info.m_ccard; cards->m_character = character; cards->draw(true); }
    }
    // SDL バックエンドのコマンド処理は、左右のハンドラーを呼んだ後
    // にネイティブの描画要求を出さない。 どのバックエンドでもキャラク
    // ター変更が見えるよう、ここで所有ダイアログを再描画対象にする。
    if (info.m_ui_backend) {
        auto& retained = info.dialog_retained_surface();
        for (const auto* key : {"charainfo.top", "charainfo.title", "charainfo.content",
                                "charainfo.tabs", "charainfo.footer"})
            (void)retained.invalidate_component(key);
        info.m_ui_backend->invalidate(info);
    }
}

} // namespace

namespace {

std::shared_ptr<::cw::lib::Surface> charainfo_canvas(::cw::lib::Integer width, ::cw::lib::Integer height,
                                                      ::cw::lib::Color color)
{
    auto image = std::make_shared<::cw::lib::Surface>();
    image->size = {std::max<::cw::lib::Integer>(1, width), std::max<::cw::lib::Integer>(1, height)};
    const auto count = static_cast<std::size_t>(image->size.width) * static_cast<std::size_t>(image->size.height);
    image->rgba.resize(count * 4);
    for (std::size_t i = 0; i < count; ++i) {
        image->rgba[i * 4] = color.r;
        image->rgba[i * 4 + 1] = color.g;
        image->rgba[i * 4 + 2] = color.b;
        image->rgba[i * 4 + 3] = color.a;
    }
    return image;
}

void charainfo_blit(const std::shared_ptr<::cw::lib::Surface>& destination,
                    const std::shared_ptr<::cw::lib::Surface>& source,
                    ::cw::lib::Integer x, ::cw::lib::Integer y,
                    ::cw::lib::Integer width = 0, ::cw::lib::Integer height = 0)
{
    if (!destination || !source || source->size.width <= 0 || source->size.height <= 0) return;
    auto image = source;
    if (width > 0 && height > 0 && (width != source->size.width || height != source->size.height))
        image = ::cw::image::smoothscale_card(source, {width, height}, true);
    if (!image || image->rgba.size() < static_cast<std::size_t>(image->size.width) * image->size.height * 4) return;
    for (::cw::lib::Integer sy = 0; sy < image->size.height; ++sy) {
        const auto dy = y + sy;
        if (dy < 0 || dy >= destination->size.height) continue;
        for (::cw::lib::Integer sx = 0; sx < image->size.width; ++sx) {
            const auto dx = x + sx;
            if (dx < 0 || dx >= destination->size.width) continue;
            const auto si = static_cast<std::size_t>((sy * image->size.width + sx) * 4);
            const auto di = static_cast<std::size_t>((dy * destination->size.width + dx) * 4);
            const auto alpha = static_cast<unsigned>(image->rgba[si + 3]);
            if (!alpha) continue;
            if (alpha == 255) {
                destination->rgba[di] = image->rgba[si];
                destination->rgba[di + 1] = image->rgba[si + 1];
                destination->rgba[di + 2] = image->rgba[si + 2];
                destination->rgba[di + 3] = 255;
            } else {
                const auto inverse = 255U - alpha;
                destination->rgba[di] = static_cast<std::uint8_t>((image->rgba[si] * alpha + destination->rgba[di] * inverse) / 255U);
                destination->rgba[di + 1] = static_cast<std::uint8_t>((image->rgba[si + 1] * alpha + destination->rgba[di + 1] * inverse) / 255U);
                destination->rgba[di + 2] = static_cast<std::uint8_t>((image->rgba[si + 2] * alpha + destination->rgba[di + 2] * inverse) / 255U);
            }
        }
    }
}

void charainfo_text(const std::shared_ptr<::cw::lib::Surface>& image, ::cw::lib::String text,
                    ::cw::lib::Integer x, ::cw::lib::Integer y, ::cw::lib::Integer size,
                    std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> color)
{
    if (!image || text.empty()) return;
    auto font = std::make_shared<::cw::imageretouch::Font>();
    font->initialize("ＭＳ Ｐゴシック", -std::max<::cw::lib::Integer>(1, size), false, false);
    charainfo_blit(image, font->render(std::move(text), true, color), x, y);
}

::cw::lib::String charainfo_msg(const char* key, const char* fallback)
{
    if (::cw::g_cwpy && ::cw::g_cwpy->m_setting && ::cw::g_cwpy->m_setting->m_msgs)
        return ::cw::g_cwpy->m_setting->m_msgs->get(key, fallback);
    return fallback;
}

::cw::lib::String charainfo_format_s(::cw::lib::String format, const ::cw::lib::String& value)
{
    const auto pos = format.find("%s");
    if (pos != ::cw::lib::String::npos) format.replace(pos, 2, value);
    else format += value;
    return format;
}

::cw::lib::String charainfo_format_ss(::cw::lib::String format, const ::cw::lib::String& a, const ::cw::lib::String& b)
{
    return charainfo_format_s(charainfo_format_s(std::move(format), a), b);
}

int charainfo_scaled(int value)
{
    return static_cast<int>(::cw::lib::ui::win_i(value));
}

int charainfo_layout_top_h() { return charainfo_scaled(105); }
int charainfo_layout_title_h() { return charainfo_scaled(24); }
int charainfo_layout_notebook_h() { return charainfo_scaled(203); }
int charainfo_layout_tab_h() { return charainfo_scaled(30); }
int charainfo_layout_bottom_h() { return charainfo_scaled(30 + ::cw::dialog::kDialogFooterExtraHeight); }
int charainfo_footer_button_h() { return charainfo_scaled(24 + ::cw::dialog::kDialogFooterExtraHeight); }
int charainfo_layout_content_h() { return charainfo_layout_notebook_h() - charainfo_layout_tab_h(); }
int charainfo_layout_surface_h()
{
    return charainfo_layout_top_h() + charainfo_layout_title_h() + charainfo_layout_content_h();
}
int charainfo_layout_client_h()
{
    return charainfo_layout_top_h() + charainfo_layout_title_h() + charainfo_layout_notebook_h() +
           charainfo_layout_bottom_h();
}

using CharainfoFont = ::cw::lib::ui::FontStyle;

std::vector<CharainfoFont> charainfo_font_styles(const CharaInfo& info)
{
    return {info.dialog_font("charadesc", 13), info.dialog_font("charaparam", 16),
            info.dialog_font("charaparam", 14), info.dialog_font("charaparam2", 16),
            info.dialog_font("tab", 13), info.dialog_font("button", 14)};
}

std::vector<CharainfoFont> deck_font_styles(const DeckDialog& dialog)
{
    return {dialog.dialog_font("charaparam2", 16), dialog.dialog_font("charadesc", 20),
            dialog.dialog_font("button", 14)};
}

bool charainfo_same_fonts(const std::vector<CharainfoFont>& left,
                         const std::vector<CharainfoFont>& right)
{
    return left.size() == right.size() &&
        std::equal(left.begin(), left.end(), right.begin(), [](const auto& a, const auto& b) {
            return a.face == b.face && a.pixels == b.pixels &&
                   a.bold == b.bold && a.italic == b.italic;
        });
}

::cw::lib::Integer charainfo_text_width(const ::cw::lib::String& text, const CharainfoFont& font)
{
    return ::cw::lib::ui::label_width(text, font.pixels, font.bold, font.face, font.italic);
}

void charainfo_draw_text(const std::shared_ptr<::cw::lib::Surface>& image, const ::cw::lib::String& text,
                         ::cw::lib::Integer x, ::cw::lib::Integer y, const CharainfoFont& font,
                         std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> color)
{
    ::cw::lib::ui::draw_label(image, text, x, y, font.pixels, font.bold, color, font.face, font.italic);
}

void charainfo_draw_outline(const std::shared_ptr<::cw::lib::Surface>& image, const ::cw::lib::String& text,
                            ::cw::lib::Integer x, ::cw::lib::Integer y, const CharainfoFont& font,
                            ::cw::lib::Color outline,
                            std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> color)
{
    ::cw::lib::ui::draw_outline_label(image, text, x, y, font.pixels, font.bold, outline, color, font.face, font.italic);
}

void charainfo_draw_adjusted(const std::shared_ptr<::cw::lib::Surface>& image, const ::cw::lib::String& text,
                             ::cw::lib::Integer x, ::cw::lib::Integer y, const CharainfoFont& font,
                             ::cw::lib::Integer maxwidth,
                             std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> color)
{
    if (!image || text.empty()) return;
    auto label = ::cw::lib::ui::render_label(text, font.pixels, font.bold, color, font.face, font.italic);
    if (!label) return;
    if (maxwidth > 0 && label->size.width > maxwidth)
        label = ::cw::image::smoothscale(label, {maxwidth, label->size.height}, true, false);
    ::cw::lib::ui::blend_surface(image, label, x, y);
}

void charainfo_draw_outline_adjusted(const std::shared_ptr<::cw::lib::Surface>& image, const ::cw::lib::String& text,
                                     ::cw::lib::Integer x, ::cw::lib::Integer y, const CharainfoFont& font,
                                     ::cw::lib::Integer maxwidth, ::cw::lib::Color outline,
                                     std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> color)
{
    if (!image || text.empty()) return;
    const auto outline_rgb = std::make_tuple(static_cast<::cw::lib::Integer>(outline.r),
                                             static_cast<::cw::lib::Integer>(outline.g),
                                             static_cast<::cw::lib::Integer>(outline.b));
    for (::cw::lib::Integer ox = -1; ox <= 1; ++ox)
        for (::cw::lib::Integer oy = -1; oy <= 1; ++oy)
            if (ox || oy)
                charainfo_draw_adjusted(image, text, x + ox, y + oy, font, maxwidth, outline_rgb);
    charainfo_draw_adjusted(image, text, x, y, font, maxwidth, color);
}

std::shared_ptr<::cw::lib::Surface> charainfo_copy_half_alpha(const std::shared_ptr<::cw::lib::Surface>& source)
{
    if (!source) return {};
    auto copy = std::make_shared<::cw::lib::Surface>(*source);
    ::cw::lib::simd_image::multiply_alpha(copy->rgba.data(), copy->rgba.size() / 4, 128);
    return copy;
}

::cw::lib::Integer charainfo_card_column_x(::cw::lib::Integer area_w, std::size_t column)
{
    if (column == 0) return charainfo_scaled(30);
    return area_w / 2 + charainfo_scaled(24);
}

::cw::lib::Integer charainfo_layout_edit_headers(EditPanel& panel)
{
    auto y = charainfo_scaled(10);
    for (auto& button : panel.m_headers) {
        if (!button) continue;
        if (value_bool(button->m_has_separator, false)) {
            y += charainfo_scaled(5);
            y += charainfo_scaled(12);
        }
        button->m_textpos = {charainfo_scaled(32), y};
        y += charainfo_scaled(17);
    }
    return y;
}

std::shared_ptr<::cw::lib::Surface> charainfo_dialog_surface(const ::cw::lib::String& key,
                                                             ::cw::lib::Integer design_w = -1,
                                                             ::cw::lib::Integer design_h = -1)
{
    auto surface = ::cw::lib::ui::resource_dialog_surface(key);
    if (!surface) return {};
    if (design_w <= 0 || design_h <= 0) {
        try {
            const auto [w, h] = ::cw::setting::get_resourcesize("Dialog/" + key);
            design_w = w;
            design_h = h;
        } catch (...) {
        }
    }
    // Python の rsrc.dialogs は読み込み時に cw.wins で拡縮さ
    // れる。 リソース表に設計時のピクセル寸法が残っている場合は、同じ拡大率を適用する。
    if (design_w > 0 && design_h > 0 && surface->size.width <= design_w + 1 && surface->size.height <= design_h + 1)
        surface = ::cw::lib::ui::wins_surface(surface);
    return surface;
}

void charainfo_draw_centered_watermark(const std::shared_ptr<::cw::lib::Surface>& image, const char* key,
                                       ::cw::lib::Integer area_x, ::cw::lib::Integer area_y,
                                       ::cw::lib::Integer area_w, ::cw::lib::Integer area_h)
{
    if (auto pad = charainfo_dialog_surface(key)) {
        const auto px = area_x + (area_w - pad->size.width) / 2;
        const auto py = area_y + (area_h - pad->size.height) / 2;
        ::cw::lib::ui::blend_surface(image, pad, px, py);
    }
}

void charainfo_draw_wrapped_centered_text(const std::shared_ptr<::cw::lib::Surface>& image,
                                          const ::cw::lib::String& text, ::cw::lib::Integer area_x,
                                          ::cw::lib::Integer area_y, ::cw::lib::Integer area_w,
                                          ::cw::lib::Integer area_h, ::cw::lib::Integer line_height,
                                          const CharainfoFont& font, ::cw::lib::Integer scroll_y)
{
    if (!image || text.empty()) return;
    const auto maxwidth = charainfo_text_width("―――――――――――――――――――", font);
    const auto text_x = area_x + (area_w - maxwidth) / 2;
    ::cw::lib::Integer y = area_y + charainfo_scaled(7) - scroll_y;
    std::size_t begin = 0;
    while (begin <= text.size()) {
        const auto end = text.find('\n', begin);
        const auto line = text.substr(begin, end == ::cw::lib::String::npos ? ::cw::lib::String::npos : end - begin);
        charainfo_draw_text(image, line, text_x, y, font, {255, 255, 255});
        y += line_height;
        if (y > area_y + area_h) break;
        if (end == ::cw::lib::String::npos) break;
        begin = end + 1;
    }
}

std::tuple<std::vector<std::shared_ptr<::cw::image::ImageInfo>>, std::vector<bool>>
charainfo_portrait_infos(const ::cw::lib::Value& value)
{
    const auto character = character_value(value);
    const auto root = value_root(value);
    if (!root) return {};
    if (const auto enemy = std::dynamic_pointer_cast<::cw::sprite::card::EnemyCard>(character)) {
        if (enemy->m_mcarddata &&
            enemy->m_mcarddata->getbool("Property/ImagePaths", ::cw::lib::Value("override"), false)) {
            const auto property = enemy->m_mcarddata->find("Property");
            if (property && *property) {
                auto infos = ::cw::image::get_imageinfos(*property, true);
                if (::cw::util::is_face_variant_imageinfos(infos))
                    infos = ::cw::util::pick_up_basename_image(std::move(infos), false);
                return ::cw::sprite::card::imageinfos_to_pathdata(std::move(infos));
            }
        }
    }
    const auto property = root ? root->find("Property") : std::optional<ElementPtr>{};
    if (!property || !*property) return {};
    auto infos = ::cw::image::get_imageinfos(*property, false);
    if (::cw::util::is_face_variant_imageinfos(infos))
        infos = ::cw::util::pick_up_basename_image(std::move(infos), false);
    std::vector<bool> can_scaled(infos.size(), root->getbool(".", ::cw::lib::Value("scaledimage"), false));
    return {std::move(infos), std::move(can_scaled)};
}

void charainfo_blit_portraits(const std::shared_ptr<::cw::lib::Surface>& image, const ::cw::lib::Value& value,
                              ::cw::lib::Integer panel_width)
{
    if (!image) return;
    auto [infos, can_scaled_images] = charainfo_portrait_infos(value);
    if (infos.empty()) return;
    const auto portrait_x = (panel_width - charainfo_scaled(74)) / 2;
    const bool setpos = std::any_of(infos.begin(), infos.end(), [](const auto& info) {
        return info && info->m_postype != "Default" && !info->m_postype.empty();
    });
    for (std::size_t i = 0; i < infos.size(); ++i) {
        const auto& info = infos[i];
        if (!info) continue;
        auto path = info->m_path;
        if (!info->m_pcnumber) {
            std::shared_ptr<::cw::sprite::base::SelectableSprite> selected;
            if (::cw::g_cwpy && ::cw::g_cwpy->m_selection && *::cw::g_cwpy->m_selection)
                selected = *::cw::g_cwpy->m_selection;
            if (std::dynamic_pointer_cast<::cw::character::Enemy>(selected) ||
                std::dynamic_pointer_cast<::cw::character::Friend>(selected))
                path = ::cw::util::get_materialpath(path, ::cw::g_M_IMG, {}, false, true);
            else if (!::cw::binary::image::path_is_code(path))
                path = ::cw::util::join_yadodir(path);
        }
        if (path.empty()) continue;
        const bool can_scaled = i < can_scaled_images.size() ? can_scaled_images[i] : false;
        // 元の論理パスと XML の許可設定を一緒に渡す。 xN を先に解決
        // すると scaledimage="False" が効かなくなる。
        auto bitmap = ::cw::util::load_wxbmp(path, true, {}, {}, std::nullopt, true,
                                             can_scaled, false, ::cw::lib::Value(::cw::g_UP_SCR));
        if (!bitmap || bitmap->size.width <= 1 || bitmap->size.height <= 1) continue;
        const auto load_scale=std::max<::cw::lib::Real>(1.0,bitmap->scr_scale);
        auto frame_bitmap = ::cw::util::resolve_animated_wxbmp(bitmap);
        auto surface = ::cw::lib::ui::wins_surface(frame_bitmap, load_scale);
        if (!surface) continue;
        ::cw::lib::Rect baserect{0, 0, 0, 0};
        if (setpos)
            baserect = info->calc_basecardposition_wx({surface->size.width, surface->size.height}, false, "LargeCard", "NotCard");
        const auto x = portrait_x + baserect.x;
        const auto y = charainfo_scaled(5) + baserect.y;
        ::cw::imageretouch::blit_2bitbmp_to_card(image, surface, {x, y});
        ::cw::lib::ui::inherit_surface_animation(image, bitmap);
    }
}

bool root_tag_is_album(const ::cw::lib::Value& value)
{
    const auto root = value_root(value);
    return root && root->m_tag == "Album";
}

void charainfo_paint_top_panel(const std::shared_ptr<::cw::lib::Surface>& image, TopPanel& panel,
                               ::cw::lib::Integer width, const CharaInfo& info)
{
    const auto param = info.dialog_font("charaparam", 16);
    const auto ep_font = info.dialog_font("charaparam", 14);
    const auto name_font = info.dialog_font("charaparam2", 16);
    if (!image) return;
    const auto top_h = charainfo_scaled(105);
    // Python の標準ダイアログ背景色を共通ラスターで再
    // 現する。 ALL や他の単体ホストがこの色を使う一方で
    // 、Windows 版だけテーマ依存の色に置き換えない。
    const ::cw::lib::Color backcolor{240, 240, 240, 255};
    ::cw::lib::ui::fill_rect(image, {0, 0, width, top_h}, backcolor);
    if (auto status = charainfo_dialog_surface("STATUS")) {
        const auto cx = width / 2;
        const auto cy = charainfo_scaled(52);
        ::cw::lib::ui::blend_surface(image, status, cx - status->size.width / 2, cy - status->size.height / 2);
    }
    const auto character = character_value(panel.m_ccard);
    const auto album = album_value(panel.m_ccard);
    const bool has_character_data = static_cast<bool>(character) || static_cast<bool>(album);
    if (has_character_data) charainfo_blit_portraits(image, panel.m_ccard, width);

    const auto coupons = character_special_coupons(panel.m_ccard);
    const auto level = character_level(panel.m_ccard);
    ::cw::lib::Integer baselevel = level;
    bool maxlevel = false;
    if (has_character_data && coupons.count("＠レベル原点") && level != coupons.at("＠レベル原点"))
        baselevel = coupons.at("＠レベル原点");
    ::cw::lib::String level_text = "Level: " + std::to_string(level);
    if (has_character_data && coupons.count("＠レベル原点") && level != coupons.at("＠レベル原点"))
        level_text = "Level: " + std::to_string(level) + " / " + std::to_string(baselevel);
    if (has_character_data && coupons.count("＠レベル上限") && coupons.at("＠レベル上限") <= baselevel) {
        const auto max_y = ::cw::setting::Setting::s_races.size() > 1 ? charainfo_scaled(17) : charainfo_scaled(20);
        charainfo_draw_outline(image, "max", charainfo_scaled(25), max_y, param,
                                          backcolor, {255, 0, 0});
        maxlevel = true;
    }
    charainfo_draw_outline(image, level_text, charainfo_scaled(5), charainfo_scaled(5),
                                      param, backcolor, {0, 0, 0});
    const auto levelr = charainfo_scaled(5) + charainfo_text_width(level_text, param) +
                      charainfo_scaled(5);

    if (::cw::g_cwpy && ::cw::g_cwpy->m_setting && ::cw::g_cwpy->m_setting->m_show_experiencebar &&
        std::dynamic_pointer_cast<::cw::character::Player>(character) && !maxlevel) {
        const auto player = std::dynamic_pointer_cast<::cw::character::Player>(character);
        const auto exp = player->get_couponsvalue();
        const auto curexp = static_cast<::cw::lib::Integer>(baselevel * (baselevel - 1) * player->get_levelcoeff());
        const auto nextlevel = baselevel + 1;
        const auto nextexp = static_cast<::cw::lib::Integer>(nextlevel * (nextlevel - 1) * player->get_levelcoeff());
        const auto prange = nextexp - curexp;
        const auto bx = charainfo_scaled(5) + 1;
        const auto by = charainfo_scaled(23) + 1;
        const auto bw = charainfo_scaled(42) - 2;
        const auto bh = charainfo_scaled(5) - 2;
        ::cw::lib::Integer fill = 0;
        ::cw::lib::Color light{192, 192, 255, 255};
        ::cw::lib::Color dark{128, 128, 192, 255};
        bool reverse = false;
        if (exp < curexp) {
            light = {192, 32, 32, 255};
            dark = {64, 0, 0, 255};
            const auto val = curexp - exp;
            fill = prange == 0 ? bw
                               : std::min<::cw::lib::Integer>(
                                     bw, static_cast<::cw::lib::Integer>(std::lround(
                                             bw * (static_cast<double>(val) / static_cast<double>(prange)))));
            reverse = true;
        } else {
            if (nextexp <= exp) {
                light = {128, 224, 128, 255};
                dark = {64, 160, 64, 255};
            }
            const auto val = exp - curexp;
            if (val >= 0)
                fill = prange == 0 ? bw
                                   : std::min<::cw::lib::Integer>(
                                         bw, static_cast<::cw::lib::Integer>(std::lround(
                                                 bw * (static_cast<double>(val) / static_cast<double>(prange)))));
        }
        ::cw::lib::ui::draw_rounded_bar(image, bx, by, bw, bh, light, dark, fill, reverse);
    }

    if (has_character_data) {
        const auto name = character_showing_name(panel.m_ccard);
        const auto name_w = charainfo_text_width(name, name_font);
        const auto width2 = width - charainfo_scaled(5);
        const auto maxwidth = std::max<::cw::lib::Integer>(1, width2 - levelr);
        const auto name_x = name_w <= maxwidth ? width2 - name_w : width2 - maxwidth;
        charainfo_draw_outline_adjusted(image, name, name_x, charainfo_scaled(3), name_font, maxwidth,
                                        backcolor, {0, 0, 0});
    }

    const bool hide_traits = character &&
        (std::dynamic_pointer_cast<::cw::sprite::card::EnemyCard>(character) ||
         std::dynamic_pointer_cast<::cw::sprite::card::FriendCard>(character));
    if (has_character_data && !hide_traits) {
        const auto race = panel.m_race;
        const bool single_unknown_race = ::cw::setting::Setting::s_races.size() == 1 &&
            std::dynamic_pointer_cast<::cw::header::UnknownRaceHeader>(::cw::setting::Setting::s_races.front());
        const auto ep_text = "EP: " + panel.m_ep;
        if (single_unknown_race) {
            charainfo_draw_outline(image, ep_text, charainfo_scaled(8), charainfo_scaled(84),
                                              ep_font, backcolor, {0, 0, 0});
        } else {
            const bool isalbum = root_tag_is_album(panel.m_ccard);
            const bool show_experiencebar=::cw::g_cwpy && ::cw::g_cwpy->m_setting &&
                ::cw::g_cwpy->m_setting->m_show_experiencebar;
            const auto ep_y = !maxlevel && (!show_experiencebar || isalbum)
                ? charainfo_scaled(22) : charainfo_scaled(32);
            charainfo_draw_outline(image, ep_text, charainfo_scaled(5), ep_y, ep_font,
                                              backcolor, {0, 0, 0});
            if (race && !std::dynamic_pointer_cast<::cw::header::UnknownRaceHeader>(race) && !race->m_name.empty())
                charainfo_draw_outline(image, race->m_name, charainfo_scaled(5), charainfo_scaled(82),
                                                  name_font, backcolor, {0, 0, 0});
        }
        const auto trait = panel.m_age + panel.m_sex;
        if (!trait.empty()) {
            const auto trait_w = charainfo_text_width(trait, name_font);
            charainfo_draw_outline(image, trait, width - charainfo_scaled(5) - trait_w, charainfo_scaled(82),
                                              name_font, backcolor, {0, 0, 0});
        }
    }
}

void charainfo_paint_title_panel(const std::shared_ptr<::cw::lib::Surface>& image, TitlePanel& title,
                                 ::cw::lib::Color bgcolor, ::cw::lib::Integer width, ::cw::lib::Integer top_h, const CharaInfo& info)
{
    const auto font = info.dialog_font("charadesc", 13);
    if (!image) return;
    const auto title_h = charainfo_scaled(24);
    ::cw::lib::ui::fill_rect(image, {0, top_h, width, title_h}, bgcolor);
    ::cw::lib::ui::draw_sunken_border(image, {0, top_h, width, title_h});
    const auto label = ::cw::lib::ui::render_label(title.m_text_value, font.pixels, font.bold,
                                                  {255, 255, 255}, font.face, font.italic);
    if (label) ::cw::lib::ui::blend_surface(image, label,
        std::max<::cw::lib::Integer>(0, (width - label->size.width) / 2),
        top_h + (title_h - label->size.height) / 2);
}

::cw::lib::String charainfo_tab_label_for(CharaInfo& info, ::cw::lib::Integer index)
{
    if (index == 0) return charainfo_msg("description", "解説");
    if (index == 1) return charainfo_msg("history", "経歴");
    if (index == 2) return info.m_is_playingscenario ? charainfo_msg("status", "状態") : charainfo_msg("edit", "編集");
    if (index == 3) return charainfo_msg("skills", "技能");
    if (index == 4) return charainfo_msg("items", "道具");
    return charainfo_msg("beasts", "召喚");
}

::cw::lib::String charainfo_title_label_for(CharaInfo& info, ::cw::lib::Integer index)
{
    if (index == 0) return charainfo_msg("description", "解説");
    if (index == 1) return charainfo_msg("history", "経歴");
    if (index == 2) return info.m_is_playingscenario ? charainfo_msg("status", "状態") : charainfo_msg("edit", "編集");
    if (index == 3) return charainfo_msg("skillcard", "技能カード");
    if (index == 4) return charainfo_msg("itemcard", "アイテムカード");
    return charainfo_msg("beastcard", "召喚獣カード");
}

bool charainfo_current_page_scrolls(const CharaInfo& info)
{
    if (info.m_page_index < 0 || info.m_page_index >= static_cast<::cw::lib::Integer>(info.m_bottompanel.size()))
        return false;
    const auto object = std::get_if<::cw::lib::ObjectPtr>(
        &info.m_bottompanel[static_cast<std::size_t>(info.m_page_index)].storage());
    if (!object || !*object) return false;
    return static_cast<bool>(std::dynamic_pointer_cast<DescPanel>(*object)) ||
           static_cast<bool>(std::dynamic_pointer_cast<HistoryPanel>(*object)) ||
           static_cast<bool>(std::dynamic_pointer_cast<StatusPanel>(*object));
}

std::shared_ptr<::cw::setting::ResourceTable> charainfo_resource_table(const ::cw::lib::Value& value)
{
    const auto* object = std::get_if<::cw::lib::ObjectPtr>(&value.storage());
    return object && *object ? std::dynamic_pointer_cast<::cw::setting::ResourceTable>(*object) : nullptr;
}

std::shared_ptr<::cw::lib::Surface> charainfo_resource_surface(const ::cw::lib::Value& table_value, const ::cw::lib::String& key)
{
    return ::cw::lib::ui::resource_table_surface(table_value, key);
}

std::shared_ptr<::cw::lib::Surface> charainfo_status_surface(const ::cw::lib::String& key)
{
    if (!::cw::g_cwpy || !::cw::g_cwpy->m_rsrc) return {};
    return charainfo_resource_surface(::cw::g_cwpy->m_rsrc->m_wxstatuses, key);
}

std::shared_ptr<::cw::lib::Surface> charainfo_stone_surface(const ::cw::lib::String& key)
{
    if (!::cw::g_cwpy || !::cw::g_cwpy->m_rsrc) return {};
    return charainfo_resource_surface(::cw::g_cwpy->m_rsrc->m_wxstones, key);
}

void charainfo_play_sound(const char* name)
{
    if (::cw::g_cwpy) ::cw::g_cwpy->play_sound(name, false, 100, 1, 0, 0, false);
}

CardPanel* charainfo_active_card_panel(CharaInfo& info)
{
    if (info.m_page_index < 0 || info.m_page_index >= static_cast<::cw::lib::Integer>(info.m_bottompanel.size())) return nullptr;
    const auto object = std::get_if<::cw::lib::ObjectPtr>(&info.m_bottompanel[static_cast<std::size_t>(info.m_page_index)].storage());
    if (!object || !*object) return nullptr;
    return std::dynamic_pointer_cast<CardPanel>(*object).get();
}

EditPanel* charainfo_active_edit_panel(CharaInfo& info)
{
    if (info.m_page_index < 0 || info.m_page_index >= static_cast<::cw::lib::Integer>(info.m_bottompanel.size())) return nullptr;
    const auto object = std::get_if<::cw::lib::ObjectPtr>(&info.m_bottompanel[static_cast<std::size_t>(info.m_page_index)].storage());
    if (!object || !*object) return nullptr;
    return std::dynamic_pointer_cast<EditPanel>(*object).get();
}

struct CharainfoPaintHits {
    ::cw::lib::Rect content_rect{};
    std::vector<std::pair<::cw::lib::Rect, std::size_t>> card_row_rects{};
    std::vector<::cw::lib::Rect> edit_button_rects{};
    ::cw::lib::Rect deck_button_rect{};
    std::vector<std::pair<::cw::lib::Rect, bool>> card_page_buttons{};
    ::cw::lib::Rect hold_all_rect{};
    bool has_deck_button{};
    bool has_hold_all{};
};

std::map<CharaInfo*, CharainfoPaintHits>& charainfo_paint_hits()
{
    static std::map<CharaInfo*, CharainfoPaintHits> hits;
    return hits;
}

void charainfo_paint_card_panel(const std::shared_ptr<::cw::lib::Surface>& image, CardPanel& panel,
                                ::cw::lib::Integer area_x, ::cw::lib::Integer area_y, ::cw::lib::Integer area_w,
                                ::cw::lib::Integer area_h, ::cw::lib::Integer scroll_y, ::cw::lib::Color bgcolor,
                                bool record_hits, std::vector<std::pair<::cw::lib::Rect, std::size_t>>* card_hits,
                                ::cw::lib::Rect* deck_rect, ::cw::lib::Rect* hold_all_rect, bool* has_deck, bool* has_hold_all,
                                std::vector<std::pair<::cw::lib::Rect, bool>>* page_buttons, const CharaInfo& info)
{
    const auto font = info.dialog_font("charadesc", 13);
    if (!image) return;
    panel._update_rects(nullptr);
    const auto cardnum = panel._get_cardnum();
    charainfo_draw_text(image, cardnum, area_x + charainfo_scaled(10),
                              area_y + charainfo_scaled(10) - scroll_y, font, {255, 255, 255});

    if (panel.m_hold_all) {
        const auto hold_y = area_y + charainfo_scaled(30) - scroll_y;
        const auto hold_text = charainfo_msg("hold_all", "全てホールド");
        const auto hold_w = charainfo_text_width(hold_text, font) + charainfo_scaled(20);
        if (auto icon = charainfo_dialog_surface(
                panel.m_character && panel.m_pocket >= 0 &&
                        panel.m_pocket < static_cast<::cw::lib::Integer>(panel.m_character->m_hold_all.size()) &&
                        panel.m_character->m_hold_all[static_cast<std::size_t>(panel.m_pocket)]
                    ? "STATUS6"
                    : "STATUS5"))
            ::cw::lib::ui::blend_surface(image, icon, area_x + charainfo_scaled(10), hold_y - charainfo_scaled(1));
        const auto color = (*panel.m_hold_all)->m_negaflag ? std::tuple{255, 0, 0} : std::tuple{255, 255, 255};
        charainfo_draw_text(image, hold_text, area_x + charainfo_scaled(30), hold_y, font, color);
        if (record_hits && hold_all_rect) {
            *hold_all_rect = {area_x + charainfo_scaled(10), hold_y + scroll_y - charainfo_scaled(1), hold_w, charainfo_scaled(16)};
            if (has_hold_all) *has_hold_all = true;
        }
    }

    const auto fw = charainfo_text_width("―", font);
    const std::size_t first_card = 0;
    const auto last_card = panel.m_headers.size();
    for (std::size_t i = first_card; i < last_card; ++i) {
        const auto& header = panel.m_headers[i];
        if (!header) continue;
        // Py の表示領域内の相対配置を維持する。 共通ホストは、描
        // 画と公開する当たり判定矩形にだけパネルの原点を適用する。
        const auto& client_hit=panel.m_card_rects[i];
        const auto x=area_x+client_hit.x+charainfo_scaled(20);
        const auto y_base=area_y+client_hit.y+charainfo_scaled(1);
        const auto y = y_base - scroll_y;
        ::cw::lib::String line = header->m_name;
        if ((header->m_type == "ItemCard" || header->m_type == "BeastCard") &&
            (header->m_uselimit || header->m_recycle))
            line += "(" + std::to_string(header->m_uselimit) + ")";
        const auto color = header->m_negaflag ? std::tuple{255, 0, 0} : std::tuple{255, 255, 255};
        const auto max_em = header->m_type == "SkillCard" ? 6 : 8;
        charainfo_draw_adjusted(image, line, x, y, font, fw * max_em, color);
        std::shared_ptr<::cw::lib::Surface> mark;
        if (header->m_type == "BeastCard")
            mark = charainfo_dialog_surface(header->m_attachment ? "STATUS10" : "STATUS11");
        else if (header->m_penalty)
            mark = charainfo_dialog_surface("STATUS7");
        else if (header->m_hold || (!panel.m_hold_all && panel.m_character &&
                                    panel.m_pocket >= 0 &&
                                    panel.m_pocket < static_cast<::cw::lib::Integer>(panel.m_character->m_hold_all.size()) &&
                                    panel.m_character->m_hold_all[static_cast<std::size_t>(panel.m_pocket)]))
            mark = charainfo_dialog_surface("STATUS6");
        else
            mark = charainfo_dialog_surface("STATUS5");
        if (mark) ::cw::lib::ui::blend_surface(image, mark, x - charainfo_scaled(20), y - charainfo_scaled(1));
        if (header->m_type == "SkillCard" && panel.m_character) {
            const auto vocation = std::max<::cw::lib::Integer>(0, header->get_showed_vocation_level(panel.m_character));
            const auto uselimit = header->get_uselimit_level() + 5;
            if (auto stone = charainfo_stone_surface("HAND" + std::to_string(vocation)))
                ::cw::lib::ui::blend_surface(image, stone, x + fw * 6 + charainfo_scaled(1), y - charainfo_scaled(1));
            if (auto stone = charainfo_stone_surface("HAND" + std::to_string(uselimit)))
                ::cw::lib::ui::blend_surface(image, stone, x + fw * 6 + charainfo_scaled(16), y - charainfo_scaled(1));
        }
        const ::cw::lib::Rect hit{area_x+client_hit.x,area_y+client_hit.y,
            client_hit.width,client_hit.height};
        if (record_hits && card_hits) card_hits->emplace_back(hit, i);
    }

    if (panel.m_character && std::dynamic_pointer_cast<::cw::character::Player>(panel.m_character)) {
        const auto btn_w = charainfo_scaled(100);
        const auto btn_h = charainfo_scaled(28);
        // ページ矢印と重ならないよう、従来の -16 の位置から右へ 10px ずらす。
        const auto bx = area_x + area_w - btn_w - charainfo_scaled(6);
        // 従来の 24px のボタンを左へ 6px、下へ 4px 移動し、上下の辺をそれぞれ 2px 広げる。
        const auto by = area_y + area_h - charainfo_scaled(24 + 6 - 4 + 2);
        ::cw::lib::ui::fill_rect(image, {bx, by, btn_w, btn_h}, {48, 48, 64, 230});
        const auto button_font = info.dialog_font("button", 14);
        charainfo_draw_adjusted(image, charainfo_msg("deck_register", "カード登録/読込"),
            bx + charainfo_scaled(4), by + (btn_h - button_font.pixels) / 2,
            button_font, btn_w - charainfo_scaled(8), {255, 255, 255});
        if (record_hits && deck_rect) {
            *deck_rect = {bx, by, btn_w, btn_h};
            if (has_deck) *has_deck = true;
        }
    }
    if (panel.card_page_count() > 1) {
        const auto button_h = charainfo_scaled(24), button_w = charainfo_scaled(20);
        const auto label_w = charainfo_scaled(34), gap = charainfo_scaled(4);
        const bool has_register = panel.m_character &&
            std::dynamic_pointer_cast<::cw::character::Player>(panel.m_character);
        // 登録ボタンの左移動に合わせる。各矢印は中心位置を保って拡大する。
        const auto right = area_x + area_w - charainfo_scaled(has_register ? 122 : 13);
        const auto left = right - 2 * button_w - label_w - 2 * gap;
        const auto top = area_y + area_h - button_h - charainfo_scaled(6);
        const auto button_font = info.dialog_font("button",14);
        const auto draw_centered = [&](const ::cw::lib::String& text, const ::cw::lib::Rect& rect,
                                       std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> color) {
            auto label = ::cw::lib::ui::render_label(text, button_font.pixels, button_font.bold,
                                                    color, button_font.face, button_font.italic);
            if (!label || label->size.width <= 0 || label->size.height <= 0) return;
            if (label->size.width > rect.width)
                label = ::cw::image::smoothscale(label, {rect.width, label->size.height}, true, false);
            if (!label) return;
            // フォントの非対称な左右余白や上部余白ではなく、実際に見える文字形を中央へ配置する。
            auto x0 = label->size.width, y0 = label->size.height;
            ::cw::lib::Integer x1 = -1, y1 = -1;
            for (::cw::lib::Integer y = 0; y < label->size.height; ++y)
                for (::cw::lib::Integer x = 0; x < label->size.width; ++x) {
                    const auto alpha = static_cast<std::size_t>((y * label->size.width + x) * 4 + 3);
                    if (alpha < label->rgba.size() && label->rgba[alpha]) {
                        x0 = std::min(x0, x); y0 = std::min(y0, y);
                        x1 = std::max(x1, x); y1 = std::max(y1, y);
                    }
                }
            if (x1 < x0 || y1 < y0) return;
            ::cw::lib::ui::blend_surface(image, label,
                rect.x + (rect.width - (x1 - x0 + 1)) / 2 - x0,
                rect.y + (rect.height - (y1 - y0 + 1)) / 2 - y0);
        };
        for (int direction = 0; direction < 2; ++direction) {
            const bool enabled = direction == 0 ? panel.m_card_page > 0 :
                panel.m_card_page + 1 < panel.card_page_count();
            const ::cw::lib::Rect rect{(direction == 0 ? left : right - button_w) - charainfo_scaled(3),
                top, button_w + charainfo_scaled(6), button_h};
            ::cw::lib::ui::fill_rect(image,rect,enabled ? ::cw::lib::Color{48,48,64,230} : ::cw::lib::Color{96,96,104,230});
            draw_centered(direction == 0 ? "◀" : "▶", rect,
                enabled ? std::tuple{255,255,255} : std::tuple{160,160,160});
            if (record_hits && page_buttons) page_buttons->emplace_back(rect,enabled);
        }
        draw_centered(std::to_string(panel.m_card_page+1)+"/"+std::to_string(panel.card_page_count()),
            {left+button_w+gap, top, label_w, button_h}, {255,255,255});
    }
    (void)bgcolor;
}

void charainfo_paint_history_panel(const std::shared_ptr<::cw::lib::Surface>& image, HistoryPanel& panel,
                                   ::cw::lib::Integer area_x, ::cw::lib::Integer area_y, ::cw::lib::Integer area_w,
                                   ::cw::lib::Integer scroll_y, const CharaInfo& info)
{
    const auto font = info.dialog_font("charadesc", 13);
    if (!image) return;
    ::cw::lib::Integer y = area_y + charainfo_scaled(8) - scroll_y;
    const auto line_h = charainfo_scaled(17);
    const auto x_text = area_x + charainfo_scaled(32);
    for (std::size_t i = 0; i < panel.m_coupons.size(); ++i) {
        const auto& [text, value] = panel.m_coupons[i];
        const bool hidden = panel.is_hidden(text);
        std::shared_ptr<::cw::lib::Surface> icon;
        if (value > 1) icon = charainfo_dialog_surface(hidden ? "STATUS3" : "STATUS3");
        else if (value == 1) icon = charainfo_dialog_surface(hidden ? "STATUS2" : "STATUS2");
        else if (value == 0) icon = charainfo_dialog_surface(hidden ? "STATUS1" : "STATUS1");
        else icon = charainfo_dialog_surface(hidden ? "STATUS0" : "STATUS0");
        if (hidden && icon) icon = charainfo_copy_half_alpha(icon);
        if (icon) ::cw::lib::ui::blend_surface(image, icon, area_x + charainfo_scaled(12), y - charainfo_scaled(1));
        const auto suffix = panel._append_text(text, value);
        const auto color = hidden ? std::tuple{160, 160, 160} : std::tuple{255, 255, 255};
        const auto suffix_w = suffix.empty() ? 0 : charainfo_text_width(suffix, font);
        const auto maxwidth = std::max<::cw::lib::Integer>(
            1, area_w - (x_text - area_x) - suffix_w - charainfo_scaled(8));
        charainfo_draw_adjusted(image, text, x_text, y, font, maxwidth, color);
        if (!suffix.empty()) {
            const auto text_w = std::min(charainfo_text_width(text, font), maxwidth);
            charainfo_draw_text(image, suffix, x_text + text_w, y, font, {128, 128, 128});
        }
        y += line_h;
        if (y > area_y + charainfo_scaled(180)) break;
    }
}

void charainfo_paint_status_row(const std::shared_ptr<::cw::lib::Surface>& image, const ::cw::lib::String& msg,
                                const ::cw::lib::String& icon_key, ::cw::lib::Color fill_color, bool fill_background,
                                ::cw::lib::Integer area_x, ::cw::lib::Integer& y, ::cw::lib::Integer scroll_y, const CharainfoFont& font)
{
    if (!image || msg.empty()) return;
    const auto row_y = y - scroll_y;
    if (auto icon = charainfo_status_surface(icon_key)) {
        if (fill_background)
            ::cw::lib::ui::fill_rect(image, {area_x + charainfo_scaled(12), row_y - charainfo_scaled(1),
                                              icon->size.width, icon->size.height},
                                     fill_color);
        ::cw::lib::ui::blend_surface(image, icon, area_x + charainfo_scaled(12), row_y - charainfo_scaled(1));
    }
    charainfo_draw_text(image, msg, area_x + charainfo_scaled(32), row_y, font, {255, 255, 255});
    y += charainfo_scaled(17);
}

void charainfo_paint_status_panel(const std::shared_ptr<::cw::lib::Surface>& image, StatusPanel& panel,
                                  ::cw::lib::Integer area_x, ::cw::lib::Integer area_y, ::cw::lib::Integer scroll_y, const CharaInfo& info)
{
    const auto font = info.dialog_font("charadesc", 13);
    if (!image) return;
    const auto character = character_value(panel.m_ccard);
    if (!character) return;
    ::cw::lib::Integer y = area_y + charainfo_scaled(8);
    const auto [life_color, life_msg] = panel._get_life();
    charainfo_paint_status_row(image, life_msg, "LIFE", life_color, true, area_x, y, scroll_y, font);
    if (character->is_poison())
        charainfo_paint_status_row(image, panel._get_poison(), "BODY0", {}, false, area_x, y, scroll_y, font);
    if (character->is_paralyze()) {
        if (character->is_petrified())
            charainfo_paint_status_row(image, panel._get_petrified(), "BODY1", {}, false, area_x, y, scroll_y, font);
        else
            charainfo_paint_status_row(image, panel._get_paralyze(), "BODY1", {}, false, area_x, y, scroll_y, font);
    }
    const auto [mental_msg, mental_icon] = panel._get_mentality();
    if (!mental_icon.empty())
        charainfo_paint_status_row(image, mental_msg, mental_icon, {}, false, area_x, y, scroll_y, font);
    if (character->is_bind())
        charainfo_paint_status_row(image, panel._get_bind(), "MAGIC0", {}, false, area_x, y, scroll_y, font);
    if (character->is_silence())
        charainfo_paint_status_row(image, panel._get_silence(), "MAGIC1", {}, false, area_x, y, scroll_y, font);
    if (character->is_faceup())
        charainfo_paint_status_row(image, panel._get_faceup(), "MAGIC2", {}, false, area_x, y, scroll_y, font);
    if (character->is_antimagic())
        charainfo_paint_status_row(image, panel._get_antimagic(), "MAGIC3", {}, false, area_x, y, scroll_y, font);
    const auto draw_enh = [&](const ::cw::lib::String& name, ::cw::lib::Integer value, ::cw::lib::Integer dur,
                              const ::cw::lib::String& up, const ::cw::lib::String& down) {
        const auto [color, bmp, msg] = panel._get_enhance(name, value, dur, up, down);
        if (msg.empty()) return;
        charainfo_paint_status_row(image, msg, value > 0 ? up : down,
                                   color.a ? color : ::cw::lib::Color{192, 192, 192, 255}, true, area_x, y, scroll_y, font);
        (void)bmp;
    };
    draw_enh(charainfo_msg("enhance_action", "行動"), character->m_enhance_act, character->m_enhance_act_dur, "UP0", "DOWN0");
    draw_enh(charainfo_msg("enhance_avoid", "回避"), character->m_enhance_avo, character->m_enhance_avo_dur, "UP1", "DOWN1");
    draw_enh(charainfo_msg("enhance_resist", "抵抗"), character->m_enhance_res, character->m_enhance_res_dur, "UP2", "DOWN2");
    draw_enh(charainfo_msg("enhance_defense", "防御"), character->m_enhance_def, character->m_enhance_def_dur, "UP3", "DOWN3");
}

void charainfo_paint_edit_panel(const std::shared_ptr<::cw::lib::Surface>& image, EditPanel& panel,
                                ::cw::lib::Integer area_x, ::cw::lib::Integer area_y, ::cw::lib::Integer area_w,
                                ::cw::lib::Integer scroll_y, bool record_hits, std::vector<::cw::lib::Rect>* edit_hits, const CharaInfo& info)
{
    const auto font = info.dialog_font("charadesc", 13);
    if (!image) return;
    // WM_PAINT のたびに編集パネルのコントロールを再構築しない
    // 。 draw(true) は m_headers を消去・再作成
    // してホバー状態を戻すため、再描画要求中に呼ぶと激しくちらつく。
    panel.draw(false);
    auto y = area_y + charainfo_scaled(10);
    for (std::size_t i = 0; i < panel.m_headers.size(); ++i) {
        const auto& button = panel.m_headers[i];
        if (!button) continue;
        if (value_bool(button->m_has_separator, false)) {
            y += charainfo_scaled(5);
            const auto line_y = y - scroll_y;
            ::cw::lib::ui::fill_rect(image,
                                     {area_x + charainfo_scaled(20), line_y, area_w - charainfo_scaled(40),
                                      std::max(1, charainfo_scaled(1))},
                                     {255, 255, 255, 255});
            y += charainfo_scaled(12);
        }
        const auto draw_y = y - scroll_y;
        const auto x = area_x + charainfo_scaled(32);
        button->m_textpos = {x, y};
        if (auto icon = charainfo_dialog_surface("STATUS12"))
            ::cw::lib::ui::blend_surface(image, icon, area_x + charainfo_scaled(12), draw_y - charainfo_scaled(1));
        const auto color = button->m_negaflag ? std::tuple{255, 0, 0} : std::tuple{255, 255, 255};
        charainfo_draw_text(image, value_string(button->m_name), x, draw_y, font, color);
        if (record_hits && edit_hits)
            edit_hits->push_back({area_x + charainfo_scaled(12), y - charainfo_scaled(1),
                                  charainfo_scaled(200), charainfo_scaled(17)});
        y += charainfo_scaled(17);
    }
}

} // namespace

::cw::lib::Value read_adventurer_character(const std::shared_ptr<::cw::header::AdventurerHeader>& header)
{
    if (!header) throw std::invalid_argument("人物が選択されていません。");
    const auto path = value_string(header->m_fpath);
    if (path.empty()) throw std::invalid_argument("人物データのパスがありません。");
    const auto tree = ::cw::data::yadoxml2etree(path, "", std::nullopt);
    const auto root = tree ? tree->getroot() : nullptr;
    if (!root) throw std::runtime_error("人物データを読み込めません: " + path);
    if (root->m_tag == "Album") {
        auto album = std::make_shared<::cw::character::AlbumPage>();
        album->initialize(tree);
        return ::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(album));
    }
    if (root->m_tag != "Adventurer")
        throw std::runtime_error("人物データではありません (" + root->m_tag + "): " + path);
    auto player = std::make_shared<::cw::character::Player>();
    player->initialize(tree);
    return ::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(player));
}

void show_standby_charainfo_modal(std::shared_ptr<::cw::lib::Window> arg_parent,
                                  std::vector<std::shared_ptr<::cw::header::AdventurerHeader>> arg_headers,
                                  ::cw::lib::Value arg_ccard, ::cw::lib::Integer arg_index,
                                  ::cw::lib::Callback arg_redrawfunc, bool arg_is_playingscenario,
                                  std::optional<std::shared_ptr<::cw::data::Party>> arg_party,
                                  StandbySelectionCallback selection_changed)
{
#if defined(CARDWIRTH_LOG_ENABLED)
    ::cardwirth::debug::log("StandbyCharaInfo open");
#endif
    auto dialog = std::make_shared<StandbyCharaInfo>();
    dialog->initialize(std::move(arg_parent), std::move(arg_headers), std::move(arg_ccard), arg_index,
                       std::move(arg_redrawfunc), arg_is_playingscenario, std::move(arg_party));
    dialog->m_selection_changed = std::move(selection_changed);
    (void)::cw::dialog::show_common_modal_presentation(dialog);
}

void show_standby_party_charainfo_modal(std::shared_ptr<::cw::lib::Window> arg_parent,
                                        std::shared_ptr<::cw::header::PartyHeader> arg_partyheader,
                                        ::cw::lib::Callback arg_redrawfunc)
{
#if defined(CARDWIRTH_LOG_ENABLED)
    ::cardwirth::debug::log("StandbyPartyCharaInfo open");
#endif
    auto dialog = std::make_shared<StandbyPartyCharaInfo>();
    dialog->initialize(std::move(arg_parent), std::move(arg_partyheader), std::move(arg_redrawfunc));
    (void)::cw::dialog::show_common_modal_presentation(dialog);
}

void CommonCharainfoUiBackend::bind(CharaInfo& arg_info)
{
    layout(arg_info);
}

void CommonCharainfoUiBackend::layout(CharaInfo& arg_info)
{
    // m_parent は呼び出し元に属し、このダイアログの表示領域矩形ではない。
    arg_info.dialog_retained_surface().invalidate_all();
    invalidate(arg_info);
}

void CommonCharainfoUiBackend::paint(CharaInfo& arg_info, std::shared_ptr<::cw::lib::Window> arg_event)
{
    arg_info.refresh_font_metrics();
    const auto width = std::max<::cw::lib::Integer>(charainfo_scaled(302),
        value_integer(arg_info.m_width, charainfo_scaled(302)));
    const auto top_h = charainfo_layout_top_h();
    const auto title_h = charainfo_layout_title_h();
    const auto notebook_h = charainfo_layout_notebook_h();
    const auto tab_h = charainfo_layout_tab_h();
    const auto content_y = top_h + title_h;
    const auto content_h = notebook_h - tab_h;
    const auto surface_h = charainfo_layout_surface_h();
    const auto render_h = charainfo_layout_client_h();
    const auto inset = charainfo_scaled(4);
    const auto bgcolor = get_bgcolor(arg_info.m_ccard);

    if (const auto object = std::get_if<::cw::lib::ObjectPtr>(&arg_info.m_titlepanel.storage()); object && *object)
        if (const auto title = std::dynamic_pointer_cast<TitlePanel>(*object)) {
            title->m_text_value = charainfo_title_label_for(arg_info, arg_info.m_page_index);
        }

    auto& retained=arg_info.dialog_retained_surface();
    bool full=retained.needs_full({width,render_h});
    if (!full && retained.base && retained.base->rgba.size() >= 4 &&
        (retained.base->rgba[0] != bgcolor.r || retained.base->rgba[1] != bgcolor.g ||
         retained.base->rgba[2] != bgcolor.b || retained.base->rgba[3] != bgcolor.a)) {
        full = true;
    }
    if(full)retained.allocate({width,render_h},bgcolor);
    const ::cw::lib::Rect top_rect{0,0,width,top_h},title_rect{0,top_h,width,title_h};
    const ::cw::lib::Rect content_rect{0,content_y,width,content_h};
    const ::cw::lib::Rect tabs_rect{0,surface_h,width,tab_h};
    const ::cw::lib::Rect footer_rect{0,surface_h+tab_h,width,charainfo_layout_bottom_h()};
    const auto mix=[](std::uint64_t& value,std::uint64_t part){value^=part+0x9e3779b97f4a7c15ULL+(value<<6U)+(value>>2U);};
    std::uint64_t top_signature=reinterpret_cast<std::uintptr_t>(arg_info.m_toppanel.get());mix(top_signature,arg_info.m_toppanel&&arg_info.m_toppanel->m_animating);mix(top_signature,std::hash<::cw::lib::String>{}(arg_info.m_toppanel ? arg_info.m_toppanel->m_detail_text : ::cw::lib::String{}));
    std::uint64_t title_signature=std::hash<::cw::lib::String>{}(charainfo_title_label_for(arg_info,arg_info.m_page_index));mix(title_signature,arg_info.m_page_index);
    std::uint64_t content_signature=static_cast<std::uint64_t>(arg_info.m_page_index);mix(content_signature,arg_info.m_scroll_y);if(arg_info.m_page_index>=0&&arg_info.m_page_index<static_cast<::cw::lib::Integer>(arg_info.m_bottompanel.size()))if(const auto object=std::get_if<::cw::lib::ObjectPtr>(&arg_info.m_bottompanel[static_cast<std::size_t>(arg_info.m_page_index)].storage());object&&*object){mix(content_signature,reinterpret_cast<std::uintptr_t>(object->get()));::cw::lib::String detail;if(const auto panel=std::dynamic_pointer_cast<DescPanel>(*object))detail=panel->get_detailtext();else if(const auto panel=std::dynamic_pointer_cast<HistoryPanel>(*object))detail=panel->get_detailtext();else if(const auto panel=std::dynamic_pointer_cast<StatusPanel>(*object))detail=panel->get_detailtext();else if(const auto panel=std::dynamic_pointer_cast<CardPanel>(*object))detail=panel->get_detailtext();else if(const auto panel=std::dynamic_pointer_cast<EditPanel>(*object))detail=panel->get_detailtext();mix(content_signature,std::hash<::cw::lib::String>{}(detail));}
    if (const auto cards = charainfo_active_card_panel(arg_info)) {
        mix(content_signature,cards->m_card_page);
        mix(content_signature,cards->m_headers.size());
    }
    std::uint64_t tabs_signature=static_cast<std::uint64_t>(arg_info.m_page_index);mix(tabs_signature,arg_info.m_bottompanel.size());
    std::uint64_t footer_signature=arg_info.can_change_character();mix(footer_signature,std::hash<::cw::lib::String>{}(arg_info.m_ui_last_info));
    for(const auto& font:charainfo_font_styles(arg_info))for(auto* signature:{&top_signature,&title_signature,&content_signature,&tabs_signature,&footer_signature}){mix(*signature,std::hash<::cw::lib::String>{}(font.face));mix(*signature,font.pixels);mix(*signature,font.bold);mix(*signature,font.italic);}
    bool paint_top=retained.component_changed("charainfo.top",top_rect,top_signature),paint_title=retained.component_changed("charainfo.title",title_rect,title_signature),paint_content=retained.component_changed("charainfo.content",content_rect,content_signature),paint_tabs=retained.component_changed("charainfo.tabs",tabs_rect,tabs_signature),paint_footer=retained.component_changed("charainfo.footer",footer_rect,footer_signature);
    const auto dirty_intersects=[&](const ::cw::lib::Rect rect){return std::any_of(retained.dirty.begin(),retained.dirty.end(),[&](const auto d){return d.x<rect.x+rect.width&&d.x+d.width>rect.x&&d.y<rect.y+rect.height&&d.y+d.height>rect.y;});};
    paint_top=paint_top||dirty_intersects(top_rect);paint_title=paint_title||dirty_intersects(title_rect);paint_content=paint_content||dirty_intersects(content_rect);paint_tabs=paint_tabs||dirty_intersects(tabs_rect);paint_footer=paint_footer||dirty_intersects(footer_rect);
    if(full)retained.capture_base();retained.restore_dirty();arg_info.m_ui_surface=retained.surface;
    if(paint_top&&arg_info.m_toppanel)charainfo_paint_top_panel(arg_info.m_ui_surface,*arg_info.m_toppanel,width,arg_info);
    if(paint_title)if(const auto object=std::get_if<::cw::lib::ObjectPtr>(&arg_info.m_titlepanel.storage());object&&*object)if(const auto title=std::dynamic_pointer_cast<TitlePanel>(*object))charainfo_paint_title_panel(arg_info.m_ui_surface,*title,bgcolor,width,top_h,arg_info);

    std::vector<std::pair<::cw::lib::Rect, std::size_t>> card_hits;
    std::vector<std::pair<::cw::lib::Rect, bool>> card_page_buttons;
    std::vector<::cw::lib::Rect> edit_hits;
    ::cw::lib::Rect deck_rect{};
    ::cw::lib::Rect hold_all_rect{};
    bool has_deck = false;
    bool has_hold_all = false;

    const auto page = arg_info.m_page_index >= 0 && arg_info.m_page_index < static_cast<::cw::lib::Integer>(arg_info.m_bottompanel.size())
        ? &arg_info.m_bottompanel[static_cast<std::size_t>(arg_info.m_page_index)] : nullptr;
    if (paint_content && page) {
        const auto object = std::get_if<::cw::lib::ObjectPtr>(&page->storage());
        if (object && *object) {
            auto content_layer = charainfo_canvas(width, content_h, bgcolor);
            ::cw::lib::ui::draw_sunken_border(content_layer, {0, 0, width, content_h});
            const auto page_x = inset;
            const auto page_y = inset;
            const auto page_w = std::max<::cw::lib::Integer>(1, width - inset * 2);
            const auto page_h = std::max<::cw::lib::Integer>(1, content_h - inset * 2);
            charainfo_draw_centered_watermark(content_layer, "PAD", page_x, page_y, page_w, page_h);
            if (const auto cards = std::dynamic_pointer_cast<CardPanel>(*object)) {
                cards->draw(false);
                charainfo_paint_card_panel(content_layer, *cards, page_x, page_y, page_w, page_h, arg_info.m_scroll_y, bgcolor,
                                           true, &card_hits, &deck_rect, &hold_all_rect, &has_deck, &has_hold_all, &card_page_buttons, arg_info);
            } else if (const auto desc = std::dynamic_pointer_cast<DescPanel>(*object)) {
                charainfo_draw_wrapped_centered_text(content_layer, desc->m_text, page_x, page_y, page_w, page_h,
                                                     charainfo_scaled(13), arg_info.dialog_font("charadesc", 13), arg_info.m_scroll_y);
            } else if (const auto history = std::dynamic_pointer_cast<HistoryPanel>(*object)) {
                history->draw(false);
                charainfo_paint_history_panel(content_layer, *history, page_x, page_y, page_w, arg_info.m_scroll_y, arg_info);
            } else if (const auto status = std::dynamic_pointer_cast<StatusPanel>(*object)) {
                status->draw(false);
                charainfo_paint_status_panel(content_layer, *status, page_x, page_y, arg_info.m_scroll_y, arg_info);
            } else if (const auto edit = std::dynamic_pointer_cast<EditPanel>(*object)) {
                charainfo_paint_edit_panel(content_layer, *edit, page_x, page_y, page_w, arg_info.m_scroll_y,
                                           true, &edit_hits, arg_info);
            }
            if (const auto maximum = arg_info.maximum_scroll(); maximum > 0) {
                const ::cw::lib::Rect track{width - charainfo_scaled(10), 0,
                                            charainfo_scaled(10), content_h};
                const auto thumb_h = std::max<::cw::lib::Integer>(charainfo_scaled(16),
                    track.height * track.height / (track.height + maximum));
                const auto thumb_y = std::clamp<::cw::lib::Integer>(arg_info.m_scroll_y, 0, maximum) *
                    (track.height - thumb_h) / maximum;
                ::cw::lib::ui::fill_rect(content_layer, track, {230, 230, 230, 255});
                ::cw::lib::ui::fill_rect(content_layer,
                    {track.x + 2, thumb_y, track.width - 4, thumb_h}, {140, 140, 140, 255});
            }
            ::cw::lib::ui::blend_surface(arg_info.m_ui_surface, content_layer, 0, content_y);
            for (auto& [rect, index] : card_hits) rect.y += content_y;
            for (auto& [rect, enabled] : card_page_buttons) rect.y += content_y;
            for (auto& rect : edit_hits) rect.y += content_y;
            if (has_deck) deck_rect.y += content_y;
            if (has_hold_all) hold_all_rect.y += content_y;
        }
    }

    auto& hits = charainfo_paint_hits()[&arg_info];
    hits.content_rect = {0, content_y, width, content_h};
    if (paint_content) {
        hits.card_row_rects = std::move(card_hits);
        hits.card_page_buttons = std::move(card_page_buttons);
        hits.edit_button_rects = std::move(edit_hits);
        hits.deck_button_rect = deck_rect;
        hits.hold_all_rect = hold_all_rect;
        hits.has_deck_button = has_deck;
        hits.has_hold_all = has_hold_all;
    }

    {
        const auto tab_y = surface_h;
        const auto tab_h = charainfo_layout_tab_h();
        const auto count = static_cast<::cw::lib::Integer>(arg_info.m_bottompanel.size());
        const auto tab_w = count > 0 ? std::min<::cw::lib::Integer>(charainfo_scaled(84), width / count) : 0;
        auto tab_x = count > 0 ? std::max<::cw::lib::Integer>(0, (width - tab_w * count) / 2) : 0;
        if(paint_tabs) for (::cw::lib::Integer index = 0; index < count; ++index) {
            const ::cw::lib::Rect rect{tab_x, tab_y, tab_w, tab_h};
            ::cw::lib::ui::fill_rect(arg_info.m_ui_surface, rect,
                index == arg_info.m_page_index ? ::cw::lib::Color{255, 255, 255, 255}
                                               : ::cw::lib::Color{192, 192, 192, 255});
            ::cw::lib::ui::draw_raised_border(arg_info.m_ui_surface, rect);
            const auto label = charainfo_tab_label_for(arg_info, index);
            const auto font = arg_info.dialog_font("tab", 13);
            const auto text = ::cw::lib::ui::render_label(label, font.pixels, font.bold,
                                                         {0, 0, 0}, font.face, font.italic);
            if (text) ::cw::lib::ui::blend_surface(arg_info.m_ui_surface, text,
                rect.x + std::max<::cw::lib::Integer>(0, (rect.width - text->size.width) / 2),
                rect.y + (rect.height - text->size.height) / 2);
            tab_x += tab_w;
        }
        const auto bottom_y = surface_h + tab_h;
        if(paint_footer) {
        ::cw::lib::ui::fill_rect(arg_info.m_ui_surface,
            {0, bottom_y, width, charainfo_layout_bottom_h()}, {240, 240, 240, 255});
        const ::cw::lib::Rect left{0, bottom_y, charainfo_scaled(30), charainfo_layout_bottom_h()};
        const ::cw::lib::Rect right{width - charainfo_scaled(30), bottom_y,
                                    charainfo_scaled(30), charainfo_layout_bottom_h()};
        const ::cw::lib::Rect close{(width - charainfo_scaled(85)) / 2,
                                    bottom_y + charainfo_scaled(3),
                                    charainfo_scaled(85), charainfo_footer_button_h()};
        const auto draw_move = [&](const ::cw::lib::Rect& rect, const char* key, const char* fallback) {
            ::cw::lib::ui::draw_button_face(arg_info.m_ui_surface, rect);
            if (::cw::g_cwpy && ::cw::g_cwpy->m_rsrc) {
                if (const auto icon = ::cw::lib::ui::resource_table_surface(
                        ::cw::g_cwpy->m_rsrc->m_buttons, key)) {
                    auto scaled = ::cw::lib::ui::resource_surface_for_window(icon);
                    if (!arg_info.can_change_character()) {
                        const auto source=scaled;
                        scaled=::cw::imageretouch::to_disabledsurface(scaled);
                        ::cw::lib::ui::inherit_surface_animation(scaled,source);
                    }
                    ::cw::lib::ui::blend_surface(arg_info.m_ui_surface, scaled,
                        rect.x + (rect.width - scaled->size.width) / 2,
                        rect.y + (rect.height - scaled->size.height) / 2);
                    return;
                }
            }
            charainfo_draw_text(arg_info.m_ui_surface, fallback,
                rect.x + charainfo_scaled(8), rect.y + charainfo_scaled(5),
                arg_info.dialog_font("button", 14),
                arg_info.can_change_character() ? std::tuple{0, 0, 0} : std::tuple{128, 128, 128});
        };
        draw_move(left, "LMOVE", "<");
        draw_move(right, "RMOVE", ">");
        ::cw::lib::ui::draw_button_face(arg_info.m_ui_surface, close);
        const auto close_text = charainfo_msg("entry_cancel", "閉じる");
        const auto font = arg_info.dialog_font("button", 14);
        const auto label = ::cw::lib::ui::render_label(close_text, font.pixels, font.bold,
                                                      {0, 0, 0}, font.face, font.italic);
        if (label) ::cw::lib::ui::blend_surface(arg_info.m_ui_surface, label,
            close.x + std::max<::cw::lib::Integer>(0, (close.width - label->size.width) / 2),
            close.y + (close.height - label->size.height) / 2);
        if (!arg_info.m_ui_last_info.empty())
            ::cw::lib::ui::draw_label(arg_info.m_ui_surface, arg_info.m_ui_last_info, charainfo_scaled(8),
                                      render_h - charainfo_scaled(18), charainfo_scaled(9), false, {255, 220, 120});
        }
    }
    retained.finish();arg_info.m_ui_surface=retained.surface;
    if (arg_event) arg_event->bounds = {0, 0, width, render_h};
    arg_info.m_ui_invalidated = false;
}

void CommonCharainfoUiBackend::paint_deck(DeckDialog& arg_dialog, std::shared_ptr<::cw::lib::Window> arg_event)
{
    const auto width = charainfo_scaled(530), height = charainfo_scaled(420);
    if (arg_event) arg_event->bounds = {0, 0, width, height};
}

void CommonCharainfoUiBackend::pump(CharaInfo& arg_info)
{
    if (arg_info.m_modal_ended || arg_info.m_child_open) return;
    if (arg_info.m_toppanel && arg_info.m_toppanel->m_animating) {
        arg_info.m_toppanel->OnAnimate(nullptr);
        (void)arg_info.dialog_retained_surface().invalidate_component("charainfo.top");
        arg_info.m_ui_invalidated = true;
    }
    if (arg_info.m_ui_invalidated) paint(arg_info, nullptr);
}

void CommonCharainfoUiBackend::invalidate(CharaInfo& arg_info)
{
    // 通常のモデル更新は現在のページに反映する。構造・ページ・タイトルの変
    // 更は各構成要素の識別情報で検出し、配置変更では全体を更新対象にする。
    (void)arg_info.dialog_retained_surface().invalidate_component("charainfo.content");
    arg_info.m_ui_invalidated = true;
    arg_info.request_dialog_presentation_refresh();
}

void CommonCharainfoUiBackend::dispatch_command(CharaInfo& arg_info, ::cw::lib::Integer arg_command)
{
    auto event = std::make_shared<::cw::lib::Window>();
    event->id = arg_command;
    switch (arg_command) {
    case 1: arg_info.OnClickLeftBtn(event); break;
    case 2: arg_info.OnClickRightBtn(event); break;
    case 3: arg_info.OnUp(event); break;
    case 4: arg_info.OnDown(event); break;
    case 5: arg_info.OnPageUp(event); break;
    case 6: arg_info.OnPageDown(event); break;
    case 7: arg_info.OnHome(event); break;
    case 8: arg_info.OnEnd(event); break;
    case 9: arg_info.OnCancel(event); break;
    case 10: arg_info.OnOpenInfo(event); break;
    case 11: arg_info.OnCopyDetail(event); break;
    default: arg_info.OnPageChanged(event); break;
    }
}

void CommonCharainfoUiBackend::show_card_info(CardPanel& arg_panel, const ::cw::lib::Value& arg_header)
{
    const auto header = header_value(arg_header);
    if (!header || arg_panel.m_child_open ||
        (arg_panel.m_charainfo_owner && (arg_panel.m_charainfo_owner->m_modal_ended || arg_panel.m_charainfo_owner->m_child_open))) return;
    if (std::find(arg_panel.m_headers.begin(), arg_panel.m_headers.end(), header) == arg_panel.m_headers.end()) return;
    struct ModalScope {
        CardPanel& panel;
        explicit ModalScope(CardPanel& p) : panel(p) {
            panel.m_child_open = true;
            if (panel.m_charainfo_owner) {panel.m_charainfo_owner->dialog_presentation_pointer_cancel();panel.m_charainfo_owner->m_child_open = true;}
        }
        ~ModalScope() {
            if (panel.m_cardinfo_dialog) panel.m_cardinfo_dialog->set_parent_callbacks({}, {});
            panel.m_cardinfo_dialog.reset();
            panel.m_child_open = false;
            if (panel.m_charainfo_owner) {
                panel.m_charainfo_owner->m_child_open = false;
                if (!panel.m_charainfo_owner->m_modal_ended) panel.m_charainfo_owner->draw(false);
            }
        }
    } scope(arg_panel);
    std::vector<::cw::dialog::cardinfo::CardInfoItem> headers;
    for (const auto& item : arg_panel.m_headers) if (item) headers.emplace_back(item);
    auto dialog = std::make_shared<::cw::dialog::cardinfo::YadoCardInfo>();
    dialog->initialize(arg_panel.m_parent, std::move(headers), ::cw::dialog::cardinfo::CardInfoItem{header}, "");
    dialog->m_close_frame_dialog = false;
    // コールバックが終了するまで、モーダルのスコープで元パネルを保持する。 Py
    // と同様、詳細ページを変えるたびに所有者のキャラクターダイアログを再描画する。
    dialog->set_parent_callbacks({}, [&arg_panel]() {
        if (arg_panel.m_charainfo_owner) {
            if (!arg_panel.m_charainfo_owner->m_modal_ended) arg_panel.m_charainfo_owner->draw(true);
        } else arg_panel.draw(true);
    });
    arg_panel.m_cardinfo_dialog = dialog;
    // Py と同様、ポケット全体を開くため、詳細画面の移動には実体のヘッダーを使う。
    if (!::cw::dialog::show_common_modal_presentation(dialog)) dialog->OnCancel(nullptr);
    arg_panel.draw(true);
}

void CommonCharainfoUiBackend::open_deck(CardPanel& arg_panel)
{
    if (!arg_panel.m_character || arg_panel.m_child_open ||
        (arg_panel.m_charainfo_owner && (arg_panel.m_charainfo_owner->m_modal_ended || arg_panel.m_charainfo_owner->m_child_open))) return;
    auto dialog = std::make_shared<DeckDialog>();
    dialog->initialize(arg_panel.m_parent, arg_panel.m_pocket, arg_panel.m_character);
    dialog->m_source_info = arg_panel.m_charainfo_owner;
    arg_panel.m_child_open = true;
    if (arg_panel.m_charainfo_owner) {arg_panel.m_charainfo_owner->dialog_presentation_pointer_cancel();arg_panel.m_charainfo_owner->m_child_open = true;}
    struct Reset {
        CardPanel& panel;
        ~Reset() {
            panel.m_child_open = false;
            if (panel.m_charainfo_owner) {
                panel.m_charainfo_owner->m_child_open = false;
                if (!panel.m_charainfo_owner->m_modal_ended) panel.m_charainfo_owner->draw(true);
            }
        }
    } reset{arg_panel};
    if (!::cw::dialog::show_common_modal_presentation(dialog))
        dialog->dialog_presentation_action(::cw::dialog::DialogPresentationAction::Cancel, -1);
    dialog->m_source_info = nullptr;
}

void CommonCharainfoUiBackend::end_modal(CharaInfo& arg_info, ::cw::lib::Integer arg_result)
{
    arg_info.dialog_presentation_pointer_cancel();
    if (arg_info.m_modal_ended || arg_info.m_closing || arg_info.m_child_open) return;
    // 終了処理中と終了済みを区別する。D の AbsDialog
    // と同様、再入を防ぎつつ、反映・終了操作が失敗する場合を扱う。
    struct CloseScope {
        CharaInfo& info;
        std::shared_ptr<int> lifetime;
        explicit CloseScope(CharaInfo& value) : info(value), lifetime(value.m_callback_lifetime) {
            info.m_closing = true;
            info.dialog_presentation_pointer_cancel();info.m_child_open = true;
        }
        ~CloseScope() {
            if (info.m_callback_lifetime != lifetime) return;
            info.m_closing = false;
            info.m_child_open = false;
            info.request_dialog_presentation_refresh();
        }
    } scope(arg_info);
    const auto standby = dynamic_cast<StandbyCharaInfo*>(&arg_info);
    try {
        // 破棄を完了する前に、Python と同様に待機中キャラクターの XML を書き込む。
        if (standby) standby->save_current_character();
    } catch (const std::exception& error) {
        if (arg_info.m_callback_lifetime == scope.lifetime)
            (void)::cw::dialog::message::show_common_modal_message("エラー",
                ::cw::lib::String("人物情報を保存できませんでした。\n") + error.what(), false, true);
        return; // Close を再試行できるよう、パネルと現在のキャラクターをそのまま保持する。
    } catch (...) {
        if (arg_info.m_callback_lifetime == scope.lifetime)
            (void)::cw::dialog::message::show_common_modal_message("エラー",
                "人物情報を保存できませんでした。", false, true);
        return;
    }
    if (arg_info.m_callback_lifetime != scope.lifetime) return;
    arg_info.m_modal_ended = true;
    arg_info.m_modal_result = arg_result;
    close_window(arg_info);
    // 親のコールバックを呼ぶ前に完了済みとし、コールバックから二重に保存されないようにする。
    if (standby && arg_info.m_callback_lifetime == scope.lifetime && arg_info.m_redrawfunc) {
        const auto redraw = *arg_info.m_redrawfunc;
        try { redraw({}, {}); }
        catch (const std::exception& error) {
            (void)::cw::dialog::message::show_common_modal_message("エラー",
                ::cw::lib::String("保存は完了しましたが、一覧を更新できませんでした。\n") + error.what(), false, true);
        } catch (...) {
            (void)::cw::dialog::message::show_common_modal_message("エラー",
                "保存は完了しましたが、一覧を更新できませんでした。", false, true);
        }
    }
}

void CommonCharainfoUiBackend::present(CharaInfo& arg_info)
{
    bind(arg_info);
    paint(arg_info, nullptr);
}

void CommonCharainfoUiBackend::close_window(CharaInfo& arg_info)
{
    if (arg_info.m_toppanel) arg_info.m_toppanel->OnDestroy(nullptr);
    for (auto& value : arg_info.m_bottompanel) {
        if (const auto panel = object_value<CardPanel>(value)) panel->OnDestroy(nullptr);
        if (const auto edit = object_value<EditPanel>(value)) edit->OnDestroy(nullptr);
    }
    charainfo_paint_hits().erase(&arg_info);
    arg_info.request_dialog_presentation_refresh();
}


void CommonCharainfoUiBackend::clipboard(const ::cw::lib::String& arg_text)
{
    ::cw::dialog::presentation_set_clipboard_text(arg_text);
}

std::shared_ptr<CharainfoUiBackend> get_charainfo_ui_backend()
{
    // CharaInfo は全プラットフォームで一つの完全な DialogPresentation
    // を公開する。 この固定のコントローラーをホスト固有の UI に置き換えない。
    static const auto controller = std::make_shared<CommonCharainfoUiBackend>();
    return controller;
}

void ActiveCharaInfo::initialize(
    std::shared_ptr<::cw::frame::Frame> arg_parent,
    std::shared_ptr<::cw::sprite::card::CWPyCard> arg_selection)
{
    m_parent = std::move(arg_parent); m_closed = false; m_cards.clear(); m_index = 0;
    std::shared_ptr<::cw::sprite::base::SelectableSprite> selected = std::move(arg_selection);
    if (!selected && ::cw::g_cwpy && ::cw::g_cwpy->m_selection)
        selected = *::cw::g_cwpy->m_selection;
    if (selected) {
        if (const auto player = std::dynamic_pointer_cast<::cw::sprite::card::PlayerCard>(selected)) m_ccard = ::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(std::static_pointer_cast<::cw::character::Character>(player)));
        else if (const auto enemy = std::dynamic_pointer_cast<::cw::sprite::card::EnemyCard>(selected)) m_ccard = ::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(std::static_pointer_cast<::cw::character::Character>(enemy)));
        else if (const auto friendcard = std::dynamic_pointer_cast<::cw::sprite::card::FriendCard>(selected)) m_ccard = ::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(std::static_pointer_cast<::cw::character::Character>(friendcard)));
    }
    m_is_playingscenario = ::cw::g_cwpy && ::cw::g_cwpy->is_playingscenario(); _update_list(); m_list = ::cw::lib::Value(static_cast<::cw::lib::Integer>(m_cards.size())); CharaInfo::initialize(nullptr, std::nullopt, true, std::nullopt);
}

void ActiveCharaInfo::update_debug()
{
    if (!_update_list()) { m_closed = true; OnCancel(nullptr); return; }
    CharaInfo::update_debug();
}

bool ActiveCharaInfo::_update_list()
{
    m_cards.clear(); if (!::cw::g_cwpy) return false;
    const auto selected = character_value(m_ccard);
    if (std::dynamic_pointer_cast<::cw::character::Enemy>(selected)) {
        for (const auto& card : ::cw::g_cwpy->get_ecards(debug_mode() ? ::cw::lib::Value("all") : ::cw::lib::Value("unreversed"))) if (card && (debug_mode() || card->is_analyzable())) m_cards.push_back(std::static_pointer_cast<::cw::character::Character>(card));
    } else if (std::dynamic_pointer_cast<::cw::character::Friend>(selected)) {
        for (const auto& card : ::cw::g_cwpy->get_fcards(::cw::lib::Value("selectable"))) if (card) m_cards.push_back(std::static_pointer_cast<::cw::character::Character>(card));
        if (::cw::g_cwpy->m_sct) {
            const auto scenario = object_value<::cw::data::ScenarioData>(::cw::g_cwpy->m_sdata);
            if (scenario && ::cw::g_cwpy->m_sct->lessthan("1.30", scenario->get_versionhint(::cw::g_HINT_SCENARIO)))
                std::reverse(m_cards.begin(), m_cards.end());
        }
    } else {
        for (const auto& card : ::cw::g_cwpy->get_pcards(debug_mode() ? ::cw::lib::Value("all") : ::cw::lib::Value("unreversed"))) if (card) m_cards.push_back(std::static_pointer_cast<::cw::character::Character>(card));
    }
    const auto selected_object = character_value(m_ccard);
    for (std::size_t i = 0; i < m_cards.size(); ++i) if (m_cards[i] == selected_object) { m_index = static_cast<::cw::lib::Integer>(i); return true; }
    return false;
}

void CardPanel::initialize(std::shared_ptr<::cw::lib::Window> arg_parent, std::shared_ptr<::cw::character::Character> arg_ccard, ::cw::lib::Integer arg_pocket)
{
    m_card_page = 0;
    m_parent = std::move(arg_parent); m_character = std::move(arg_ccard); m_ccard = m_character ? ::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(m_character)) : ::cw::lib::Value{}; m_pocket = arg_pocket; m_headers.clear(); m_card_rects.clear(); _update_debug(); draw(true);
}

void CardPanel::_update_debug()
{
    if (m_pocket != ::cw::g_POCKET_BEAST && (debug_mode() || std::dynamic_pointer_cast<::cw::character::Player>(m_character))) { m_hold_all = std::make_shared<HoldAll>(); (*m_hold_all)->initialize(); }
    else m_hold_all.reset();
    _update_rects(nullptr);
}

void CardPanel::update_debug() { _update_debug(); }

void CardPanel::OnDeck(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
#if defined(CARDWIRTH_LOG_ENABLED)
    ::cardwirth::debug::log("CardPanel OnDeck");
#endif
    if (const auto backend = get_charainfo_ui_backend()) backend->open_deck(*this);
}

void CardPanel::_on_size_deck(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    if (m_charainfo_owner) m_charainfo_owner->draw(false);
}

void CardPanel::OnDestroy(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event; for (const auto& header : m_headers) if (header) header->m_subrect = ::cw::lib::Value{}; m_card_rects.clear();
}

void CardPanel::OnLeftUp(std::shared_ptr<::cw::lib::Window> arg_event) { (void)arg_event; _switch_hold(); }
void CardPanel::OnKeyUp(std::shared_ptr<::cw::lib::Window> arg_event) { if (arg_event && arg_event->id == 32) _switch_hold(); }

void CardPanel::_switch_hold()
{
    if (!m_character) return;
    if (!debug_mode() && !std::dynamic_pointer_cast<::cw::character::Player>(m_character)) {
        _open_cardinfo();
        return;
    }
    if (m_hold_all && (*m_hold_all)->m_negaflag) { charainfo_play_sound("click"); if (m_pocket >= 0 && m_pocket < static_cast<::cw::lib::Integer>(m_character->m_hold_all.size())) m_character->set_hold_all(m_pocket, !m_character->m_hold_all[static_cast<std::size_t>(m_pocket)]); return; }
    for (const auto& header : m_headers) if (header && header->m_negaflag) {
        if (header->m_penalty) { charainfo_play_sound("error"); return; }
        charainfo_play_sound("click");
        if (::cw::g_cwpy && ::cw::g_cwpy->m_ydata && *::cw::g_cwpy->m_ydata) (*::cw::g_cwpy->m_ydata)->changed();
        header->set_hold(!header->m_hold);
        return;
    }
}

bool CardPanel::_open_cardinfo()
{
    for (const auto& header : m_headers) {
        if (!header || !header->m_negaflag) continue;
        charainfo_play_sound("click");
        if (const auto backend = get_charainfo_ui_backend())
            backend->show_card_info(*this, ::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(header)));
        return true;
    }
    return false;
}

void CardPanel::OnRightUp(std::shared_ptr<::cw::lib::Window> arg_event) {
    if (!_open_cardinfo() && m_charainfo_owner) m_charainfo_owner->OnCancel(arg_event);
}
void CardPanel::OnLeave(std::shared_ptr<::cw::lib::Window> arg_event) { (void)arg_event; for (const auto& header : m_headers) if (header) header->m_negaflag = false; if (m_hold_all) (*m_hold_all)->m_negaflag = false; }
void CardPanel::OnMove(std::shared_ptr<::cw::lib::Window> arg_event) { _update_mousepos(arg_event); }

void CardPanel::_update_mousepos(std::shared_ptr<::cw::lib::Window> arg_mousepos)
{
    if (arg_mousepos) m_mousepos = {arg_mousepos->bounds.x, arg_mousepos->bounds.y};
    if (m_hold_all) (*m_hold_all)->m_negaflag = m_mousepos.x >= 10 && m_mousepos.x < 190 && m_mousepos.y >= 28 && m_mousepos.y < 50;
    for (std::size_t i = 0; i < m_headers.size() && i < m_card_rects.size(); ++i) { const auto& r = m_card_rects[i]; m_headers[i]->m_negaflag = r.x <= m_mousepos.x && m_mousepos.x < r.x + r.width && r.y <= m_mousepos.y && m_mousepos.y < r.y + r.height; }
}

void CardPanel::update_negaflag() { _update_mousepos(nullptr); }
void CardPanel::draw_header(std::shared_ptr<::cw::lib::DeviceContext> arg_dc, ::cw::lib::Value arg_header) { (void)arg_dc; if (const auto header = header_value(arg_header)) m_detail_text = header->m_name; }

void CardPanel::up()
{
    const auto count = m_headers.size() + (m_hold_all ? 1U : 0U); if (!count) return; std::size_t selected = count - 1; bool found = false;
    for (std::size_t i = 0; i < m_headers.size(); ++i) if (m_headers[i]->m_negaflag) { selected = i + (m_hold_all ? 1U : 0U); found = true; break; }
    if (m_hold_all && (*m_hold_all)->m_negaflag) { selected = 0; found = true; }
    if (found) selected = selected ? selected - 1 : count - 1;
    if (m_hold_all) (*m_hold_all)->m_negaflag = selected == 0;
    for (std::size_t i = 0; i < m_headers.size(); ++i)
        m_headers[i]->m_negaflag = selected == i + (m_hold_all ? 1U : 0U);
    m_card_page = 0;
    _update_rects(nullptr);
}

void CardPanel::down()
{
    const auto count = m_headers.size() + (m_hold_all ? 1U : 0U); if (!count) return; std::size_t selected = 0; bool found = false;
    for (std::size_t i = 0; i < m_headers.size(); ++i) if (m_headers[i]->m_negaflag) { selected = i + (m_hold_all ? 1U : 0U); found = true; break; }
    if (m_hold_all && (*m_hold_all)->m_negaflag) { selected = 0; found = true; }
    if (found) selected = (selected + 1) % count;
    if (m_hold_all) (*m_hold_all)->m_negaflag = selected == 0;
    for (std::size_t i = 0; i < m_headers.size(); ++i)
        m_headers[i]->m_negaflag = selected == i + (m_hold_all ? 1U : 0U);
    m_card_page = 0;
    _update_rects(nullptr);
}

std::size_t CardPanel::card_page_count() const noexcept
{
    return 1;
}

void CardPanel::change_card_page(int direction)
{
    const auto current = std::min(m_card_page, card_page_count() - 1);
    const auto next = direction < 0 ? (current > 0 ? current - 1 : 0) :
        std::min(current + 1, card_page_count() - 1);
    if (next == m_card_page) return;
    m_card_page = next;
    for (const auto& header : m_headers) if (header) header->m_negaflag = false;
    if (m_hold_all && *m_hold_all) (*m_hold_all)->m_negaflag = false;
    m_detail_text.clear();
    _update_rects(nullptr);
}

void CardPanel::_update_rects(std::shared_ptr<::cw::lib::DeviceContext> arg_dc)
{
    (void)arg_dc;
    m_card_page=0;
    m_card_rects.assign(m_headers.size(), {});
    const auto font = m_charainfo_owner ? m_charainfo_owner->dialog_font("charadesc",13)
        : ::cw::lib::ui::configured_font_scaled("charadesc",13);
    ::cw::imageretouch::Font measure;
    measure.initialize(font.face,-font.pixels,font.bold,font.italic);
    const auto hold_offset=m_hold_all?charainfo_scaled(20):0;
    for (std::size_t i=0;i<m_headers.size();++i) {
        if (!m_headers[i]) continue;
        const auto column=i<5?0:1;
        const auto row=i<5?i:i-5;
        const auto x=charainfo_card_column_x(m_charainfo_owner ? value_integer(m_charainfo_owner->m_width,charainfo_scaled(302))-charainfo_scaled(8) : charainfo_scaled(294),column);
        const auto y=charainfo_scaled(30)+hold_offset+charainfo_scaled(static_cast<::cw::lib::Integer>(row*17));
        m_headers[i]->m_textpos={x,y};
        m_card_rects[i]={x-charainfo_scaled(20),y-charainfo_scaled(1),
            charainfo_text_width(m_headers[i]->m_name,font)+charainfo_scaled(20),
            std::get<1>(measure.size(m_headers[i]->m_name))+charainfo_scaled(2)};
    }
}

void CardPanel::OnPaint(std::shared_ptr<::cw::lib::Window> arg_event) { (void)arg_event; draw(false); }

void CardPanel::draw(bool arg_update)
{
    if (arg_update || m_headers.empty()) { m_headers = character_headers(m_character, m_pocket); _update_rects(nullptr); }
    m_detail_text = get_detailtext();
}

::cw::lib::String CardPanel::_get_cardnum()
{
    auto maximum = static_cast<::cw::lib::Integer>(m_headers.size()); if (m_character) { const auto space = m_character->get_cardpocketspace(); maximum = m_pocket == ::cw::g_POCKET_SKILL ? std::get<0>(space) : m_pocket == ::cw::g_POCKET_ITEM ? std::get<1>(space) : std::get<2>(space); }
    return charainfo_format_ss(charainfo_msg("card_number", "カード枚数 %s / %s"),
                               std::to_string(m_headers.size()), std::to_string(maximum));
}

::cw::lib::String CardPanel::get_detailtext()
{
    const auto character = m_character ? m_character : character_value(m_ccard);
    ::cw::lib::String result = _get_cardnum();
    if (character && m_pocket >= 0 && m_pocket < static_cast<::cw::lib::Integer>(character->m_hold_all.size()) &&
        character->m_hold_all[static_cast<std::size_t>(m_pocket)]) {
        result += " <" + charainfo_msg("hold_all", "全てホールド") + ">";
    }
    for (const auto& header : m_headers) {
        if (!header) continue;
        ::cw::lib::String mark;
        if (header->m_penalty) mark = "X";
        else if (header->m_hold) mark = "#";
        else if (header->m_type == "SkillCard") mark = "S";
        else if (header->m_type == "ItemCard") mark = "I";
        else if (header->m_type == "BeastCard") mark = header->m_attachment ? "A" : "B";
        else mark = "?";
        auto line = "[" + mark + "] " + header->m_name;
        const auto width = ::cw::util::get_strlen(line);
        if (width < 26) line.append(static_cast<std::size_t>(26 - width), ' ');
        ::cw::lib::Integer vocation = 0;
        if (character)
            vocation = std::max<::cw::lib::Integer>(0, header->get_showed_vocation_level(character) + 1);
        const auto uselimit = header->get_uselimit_level();
        auto bars = [](const ::cw::lib::Integer value) {
            return ::cw::lib::String(static_cast<std::size_t>(std::max<::cw::lib::Integer>(0, std::min<::cw::lib::Integer>(4, value))), '|');
        };
        const auto vocation_text = bars(vocation);
        const auto uselimit_text = bars(uselimit);
        line += " [" + vocation_text + ::cw::lib::String(4 - vocation_text.size(), ' ') + "] [" + uselimit_text + ::cw::lib::String(4 - uselimit_text.size(), ' ') + "]";
        result += "\n" + line;
    }
    return result;
}

CharaInfo::~CharaInfo()
{
    m_callback_lifetime.reset();
    if (m_toppanel) m_toppanel->OnDestroy(nullptr);
    for (auto& value : m_bottompanel) {
        if (const auto panel = object_value<CardPanel>(value)) {
            panel->OnDestroy(nullptr);
            panel->m_charainfo_owner = nullptr;
        }
        if (const auto edit = object_value<EditPanel>(value)) {
            edit->OnDestroy(nullptr);
            edit->m_charainfo_owner = nullptr;
        }
    }
    charainfo_paint_hits().erase(this);
}

bool CharaInfo::can_change_character() const
{
    if (const auto standby = dynamic_cast<const StandbyCharaInfo*>(this)) return standby->m_headers.size() > 1;
    if (const auto active = dynamic_cast<const ActiveCharaInfo*>(this)) return active->m_cards.size() > 1;
    return false;
}

CharacterPageEditor CharaInfo::page_editor() const
{
    // Python と同様、これらのページの編集は ActiveCharaInfo のデバッ
    // グ操作とする。 待機中・アルバムの解説と通常モードのページは読み取り専用のままにする。
    if (m_modal_ended || !m_editable || !m_cwpy_debug ||
        (::cw::g_cwpy && !debug_mode()) || !dynamic_cast<const ActiveCharaInfo*>(this) ||
        !character_value(m_ccard) || m_page_index < 0 ||
        m_page_index >= static_cast<::cw::lib::Integer>(m_bottompanel.size())) return CharacterPageEditor::None;
    const auto& page = m_bottompanel[static_cast<std::size_t>(m_page_index)];
    if (const auto panel = object_value<DescPanel>(page); panel && panel->m__editable)
        return CharacterPageEditor::Description;
    if (const auto panel = object_value<HistoryPanel>(page); panel && panel->m__editable)
        return CharacterPageEditor::History;
    if (const auto panel = object_value<StatusPanel>(page); panel && panel->m__editable)
        return CharacterPageEditor::Status;
    return CharacterPageEditor::None;
}

void CharaInfo::open_page_editor(CharacterPageEditor kind)
{
    if (kind == CharacterPageEditor::None || kind != page_editor() || m_child_open) return;
    bool accepted = false;
    {
        struct ChildScope {
            CharaInfo& owner;
            explicit ChildScope(CharaInfo& value) : owner(value) { owner.dialog_presentation_pointer_cancel();owner.m_child_open = true; }
            ~ChildScope() { owner.m_child_open = false; owner.request_dialog_presentation_refresh(); }
        } child(*this);
        charainfo_play_sound("click");
        if (kind == CharacterPageEditor::Description) {
            // CharacterEditDialog::initialize が共通モーダルを既に一つ
            // 開いている。 show をもう一度呼ぶとウィンドウが二つ作られるため、重ねて呼ばない。
            auto dialog = std::make_shared<::cw::debug::charaedit::CharacterEditDialog>();
            // 意図的な変更(Py版と異なる): Py版のデバッグ用エディタ(解説/経歴/状態)は
            // デスクトップDPIに従い cw.UP_WIN を適用しない。キャラ情報(ゲーム側の
            // ダイアログ)から開くこの3つは、親と同じゲーム画面の拡大率で表示する。
            dialog->game_window_scale = true;
            dialog->initialize(nullptr, m_index, false);
            accepted = dialog->m_modal_result_ok;
        } else if (kind == CharacterPageEditor::History) {
            auto dialog = std::make_shared<::cw::debug::edit::CouponEditDialog>();
            dialog->game_window_scale = true;
            dialog->initialize(nullptr, m_index);
            accepted = ::cw::debug::edit::show_modal(*dialog);
        } else if (const auto active = dynamic_cast<ActiveCharaInfo*>(this); active && !active->m_cards.empty()) {
            auto dialog = std::make_shared<::cw::debug::statusedit::StatusEditDialog>();
            dialog->game_window_scale = true;
            dialog->initialize(nullptr, active->m_cards, active->m_index);
            accepted = dialog->show_modal();
        }
    }
    if (accepted && !m_modal_ended) refresh_after_editor();
}

void CharaInfo::refresh_after_editor()
{
    const std::weak_ptr<int> lifetime = m_callback_lifetime;
    const auto refresh = [this, lifetime](const ::cw::lib::Arguments&, const ::cw::lib::KeywordArguments&) {
        if (!lifetime.expired() && !m_modal_ended) {
            refresh_character_panels(*this);
            m_scroll_y = std::clamp<::cw::lib::Integer>(m_scroll_y, 0, maximum_scroll());
            draw(true);
        }
        return ::cw::lib::Value{};
    };
    if (const auto runtime = ::cw::g_cwpy) {
        // 称号の OK 操作は確定処理をキューに入れる。その後に処理を予約してから UI
        // スレッドへ戻す。 直ちに再描画すると古い XML を再表示してしまう。
        runtime->exec_func([runtime, refresh](const ::cw::lib::Arguments&, const ::cw::lib::KeywordArguments&) {
            if (const auto frame = object_value<::cw::frame::Frame>(runtime->m_frame)) frame->exec_func(refresh);
            return ::cw::lib::Value{};
        });
    } else {
        refresh({}, {});
    }
}

::cw::lib::ui::FontStyle CharaInfo::dialog_font(const ::cw::lib::String& role,
                                               ::cw::lib::Integer logical_pixels) const
{
    return ::cw::lib::ui::configured_wx_font(role, logical_pixels);
}

bool CharaInfo::font_metrics_changed() const
{
    return m_font_layout_scale != ::cw::g_UP_WIN ||
           !charainfo_same_fonts(m_layout_fonts, charainfo_font_styles(*this));
}

bool CharaInfo::refresh_font_metrics()
{
    auto fonts = charainfo_font_styles(*this);
    const auto scale = ::cw::g_UP_WIN;
    if (m_font_layout_scale == scale && charainfo_same_fonts(m_layout_fonts, fonts)) return false;
    // 表示だけを更新する操作であり、メンバー変更や保存のコールバックは呼ばない。
    dialog_presentation_pointer_cancel();
    if (m_font_layout_scale > 0.0 && std::isfinite(scale) && scale > 0.0) {
        const auto ratio = scale / m_font_layout_scale;
        m_scroll_y = static_cast<::cw::lib::Integer>(std::lround(m_scroll_y * ratio));
        for (auto& scroll : m_page_scrolls)
            scroll = static_cast<::cw::lib::Integer>(std::lround(scroll * ratio));
    }
    m_layout_fonts = std::move(fonts);
    m_font_layout_scale = scale;
    const auto sample_width = charainfo_text_width("――――――――――――――――――――", m_layout_fonts.front());
    m_width = std::max<::cw::lib::Integer>(charainfo_scaled(302), sample_width + charainfo_scaled(20));
    m_scroll_y = std::clamp<::cw::lib::Integer>(m_scroll_y, 0, maximum_scroll());
    m_published_controls.clear();
    m_published_controls_dirty = true;
    charainfo_paint_hits().erase(this);
    m_ui_invalidated = true;
    request_dialog_presentation_refresh();
    return true;
}

void CharaInfo::initialize(std::shared_ptr<::cw::lib::Window> arg_parent, std::optional<::cw::lib::Callback> arg_redrawfunc, bool arg_editable, std::optional<std::shared_ptr<::cw::data::Party>> arg_party)
{
    dialog_retained_surface().reset();
    m_callback_lifetime = std::make_shared<int>(0);
    dialog_presentation_pointer_cancel();m_side_navigation.reset();m_published_controls.clear();m_published_controls_dirty=true;
    m_last_hover_x = -1.0F; m_last_hover_y = -1.0F; m_has_hover_position = false;
    m_closing = false;
    m_child_open = false;
    m_parent = std::move(arg_parent);
    m_ui_backend = get_charainfo_ui_backend();
    m_ui_surface.reset();
    m_ui_last_info.clear();
    m_scroll_y = 0;
    m_page_scrolls.clear();
    m_ui_invalidated = true;
    m_redrawfunc = std::move(arg_redrawfunc);
    m_editable = arg_editable;
    m_party = arg_party && *arg_party
        ? ::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(*arg_party))
        : ::cw::lib::Value{};
    m_cwpy_debug = debug_mode();
    m_modal_ended = false;
    m_modal_result = 0;
    m_page_index = 0;
    m_copy_text.clear();
    m_bottompanel.clear();
    m_font_layout_scale = -1;
    m_layout_fonts.clear();
    refresh_font_metrics();

    const auto character = character_value(m_ccard);
    const auto showing_name = character_showing_name(m_ccard);
    m_title_text = showing_name.empty() ? "Character" : showing_name;
    auto desc = std::make_shared<DescPanel>();
    desc->initialize(m_parent, m_ccard, m_editable);
    const std::weak_ptr<int> lifetime = m_callback_lifetime;
    desc->m_on_edit = [this, lifetime] { if (!lifetime.expired()) open_page_editor(CharacterPageEditor::Description); };
    desc->m_on_cursor_changed = [this, lifetime] { if (!lifetime.expired()) request_dialog_presentation_refresh(); };
    m_descpanel = std::static_pointer_cast<::cw::lib::Object>(desc);
    m_bottompanel.emplace_back(m_descpanel);
    auto history = std::make_shared<HistoryPanel>();
    history->initialize(m_parent, m_ccard, m_editable);
    history->m_on_edit = [this, lifetime] { if (!lifetime.expired()) open_page_editor(CharacterPageEditor::History); };
    history->m_on_cursor_changed = [this, lifetime] { if (!lifetime.expired()) request_dialog_presentation_refresh(); };
    m_historypanel = std::static_pointer_cast<::cw::lib::Object>(history);
    m_bottompanel.emplace_back(m_historypanel);

    // Python版は表示中の全メンバを StatusPanel に渡して、デバッグ編集時の
    // selected/index と一致させる。現在カードだけを渡すと他メンバの状態編集が
    // 常に先頭へ収束してしまうため、Active/Standby の元リストを再構築する。
    std::vector<std::shared_ptr<::cw::character::Character>> status_list;
    if (const auto active = dynamic_cast<ActiveCharaInfo*>(this)) {
        status_list = active->m_cards;
    } else if (const auto standby = dynamic_cast<StandbyCharaInfo*>(this)) {
        for (const auto& header : standby->m_headers) {
            const auto loaded = load_adventurer_value(header);
            if (const auto member = character_value(loaded)) status_list.push_back(member);
        }
    }
    if (status_list.empty() && character) status_list.push_back(character);

    if (m_is_playingscenario && character) {
        auto status = std::make_shared<StatusPanel>();
        status->initialize(m_parent, std::move(status_list), character, m_editable);
        status->m_on_edit = [this, lifetime] { if (!lifetime.expired()) open_page_editor(CharacterPageEditor::Status); };
        status->m_on_cursor_changed = [this, lifetime] { if (!lifetime.expired()) request_dialog_presentation_refresh(); };
        m_editpanel = std::static_pointer_cast<::cw::lib::Object>(status);
        m_bottompanel.emplace_back(m_editpanel);
    } else if (m_editable) {
        std::vector<::cw::lib::Value> edit_values;
        std::vector<std::shared_ptr<::cw::header::AdventurerHeader>> edit_headers;
        if (const auto standby = dynamic_cast<StandbyCharaInfo*>(this)) {
            edit_headers = standby->m_headers;
            const auto current_player = std::dynamic_pointer_cast<::cw::character::Player>(character);
            const auto current_tree = current_player ? object_value<::cw::data::CWPyElementTree>(current_player->m_data) : nullptr;
            for (const auto& header : edit_headers) {
                const auto header_path = header ? value_string(header->m_fpath) : ::cw::lib::String{};
                if (current_player && current_tree && current_tree->m_fpath == header_path) {
                    edit_values.emplace_back(::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(current_player)));
                } else {
                    const auto loaded = load_adventurer_value(header);
                    if (const auto player = object_value<::cw::character::Player>(loaded))
                        edit_values.emplace_back(::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(player)));
                }
            }
        } else if (const auto active = dynamic_cast<ActiveCharaInfo*>(this)) {
            for (const auto& card : active->m_cards)
                if (const auto player = std::dynamic_pointer_cast<::cw::character::Player>(card))
                    edit_values.emplace_back(::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(player)));
        }
        if (edit_values.empty()) {
            if (const auto player = std::dynamic_pointer_cast<::cw::character::Player>(character))
                edit_values.emplace_back(::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(player)));
        }
        auto edit = std::make_shared<EditPanel>();
        edit->initialize(m_parent ? ::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(m_parent)) : ::cw::lib::Value{}, std::move(edit_values), std::dynamic_pointer_cast<::cw::character::Player>(character));
        edit->m_parentheaders = std::move(edit_headers);
        edit->m_party_mode = dynamic_cast<StandbyPartyCharaInfo*>(this) != nullptr;
        if (const auto standby = dynamic_cast<StandbyCharaInfo*>(this)) edit->m_parent_index = standby->m_index;
        edit->m_charainfo_owner = this;
        edit->m_owner_lifetime = m_callback_lifetime;
        edit->m_parent_window = m_parent;
        m_editpanel = std::static_pointer_cast<::cw::lib::Object>(edit);
        m_bottompanel.emplace_back(m_editpanel);
    }
    if (character && value_root(character->m_data) && value_root(character->m_data)->find("SkillCards")) {
        auto skill = std::make_shared<SkillPanel>();
        skill->initialize(m_parent, character);
        skill->m_charainfo_owner = this;
        m_skillpanel = skill;
        m_bottompanel.emplace_back(std::static_pointer_cast<::cw::lib::Object>(skill));
        auto item = std::make_shared<ItemPanel>();
        item->initialize(m_parent, character);
        item->m_charainfo_owner = this;
        m_itempanel = item;
        m_bottompanel.emplace_back(std::static_pointer_cast<::cw::lib::Object>(item));
        auto beast = std::make_shared<BeastPanel>();
        beast->initialize(m_parent, character);
        beast->m_charainfo_owner = this;
        m_beastpanel = beast;
        m_bottompanel.emplace_back(std::static_pointer_cast<::cw::lib::Object>(beast));
    }
    m_toppanel = std::make_shared<TopPanel>();
    m_toppanel->initialize(m_parent ? ::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(m_parent)) : ::cw::lib::Value{}, m_ccard, m_redrawfunc);
    auto title = std::make_shared<TitlePanel>();
    title->initialize(m_parent ? ::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(m_parent)) : ::cw::lib::Value{}, m_parent);
    title->m_is_playingscenario = m_is_playingscenario;
    m_titlepanel = std::static_pointer_cast<::cw::lib::Object>(title);
    _do_layout();
    _bind();
    draw(true);
}

void CharaInfo::_bind() { if (m_ui_backend) m_ui_backend->bind(*this); }
void CharaInfo::OnCopyDetail(std::shared_ptr<::cw::lib::Window> arg_event) { (void)arg_event; copy_detail(); }

::cw::lib::String CharaInfo::detail_text() const
{
    auto text = m_toppanel ? m_toppanel->get_detailtext() : ::cw::lib::String{};
    text += "\n----------------------------------------";
    ::cw::lib::String page_text;
    if (m_page_index >= 0 && m_page_index < static_cast<::cw::lib::Integer>(m_bottompanel.size())) {
        const auto& value = m_bottompanel[static_cast<std::size_t>(m_page_index)];
        if (const auto panel = object_value<DescPanel>(value)) page_text = panel->get_detailtext();
        else if (const auto panel = object_value<HistoryPanel>(value)) page_text = panel->get_detailtext();
        else if (const auto panel = object_value<StatusPanel>(value)) page_text = panel->get_detailtext();
        else if (const auto panel = object_value<CardPanel>(value)) page_text = panel->get_detailtext();
        else if (const auto panel = object_value<EditPanel>(value)) page_text = panel->get_detailtext();
    }
    // Py と同様、詳細テキストが空でない場合だけページの行を追加する。
    if (!page_text.empty()) text += '\n' + page_text;
    return text + '\n';
}
void CharaInfo::copy_detail()
{
    charainfo_play_sound("equipment");
    m_copy_text = detail_text();
    if (m_ui_backend) m_ui_backend->clipboard(m_copy_text);
    else ::cw::dialog::presentation_set_clipboard_text(m_copy_text);
}

void CharaInfo::OnEnter(std::shared_ptr<::cw::lib::Window> arg_event)
{
    if (m_page_index < 0 || m_page_index >= static_cast<::cw::lib::Integer>(m_bottompanel.size())) return;
    if (const auto o=std::get_if<::cw::lib::ObjectPtr>(&m_bottompanel[static_cast<std::size_t>(m_page_index)].storage());o&&*o) {
        if (const auto card_panel=std::dynamic_pointer_cast<CardPanel>(*o)) {card_panel->OnLeftUp(arg_event);return;}
        if (const auto edit_panel=std::dynamic_pointer_cast<EditPanel>(*o)) {edit_panel->OnLeftUp(arg_event);return;}
    }
    // Python と同様、解説・経歴・状態の Enter による
    // 編集開始にも、ポインター用の編集表示と同じ権限判定を使う。
    open_page_editor(page_editor());
}
void CharaInfo::OnOpenInfo(std::shared_ptr<::cw::lib::Window> arg_event) { if (m_page_index >= 0 && m_page_index < static_cast<::cw::lib::Integer>(m_bottompanel.size())) if (const auto o = std::get_if<::cw::lib::ObjectPtr>(&m_bottompanel[static_cast<std::size_t>(m_page_index)].storage()); o && *o) if (const auto card_panel = std::dynamic_pointer_cast<CardPanel>(*o)) card_panel->OnRightUp(arg_event); }
void CharaInfo::OnUp(std::shared_ptr<::cw::lib::Window>)
{
    if (auto* panel = charainfo_active_card_panel(*this)) panel->up();
    else if (auto* panel = charainfo_active_edit_panel(*this)) panel->up();
    else scroll_content(-scroll_unit());
}
void CharaInfo::OnPageUp(std::shared_ptr<::cw::lib::Window>) { scroll_content(-10 * scroll_unit()); }
void CharaInfo::OnHome(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    if (!charainfo_current_page_scrolls(*this)) return;
    m_scroll_y = 0;
    draw(false);
}
void CharaInfo::OnDown(std::shared_ptr<::cw::lib::Window>)
{
    if (auto* panel = charainfo_active_card_panel(*this)) panel->down();
    else if (auto* panel = charainfo_active_edit_panel(*this)) panel->down();
    else scroll_content(scroll_unit());
}
void CharaInfo::OnPageDown(std::shared_ptr<::cw::lib::Window>) { scroll_content(10 * scroll_unit()); }
void CharaInfo::OnLeftKey(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    if (m_bottompanel.empty()) return;
    OnPageChanging(nullptr);
    cycle_index(m_page_index, m_bottompanel.size(), -1);
    auto changed = std::make_shared<::cw::lib::Window>();
    changed->id = m_page_index;
    OnPageChanged(changed);
}

void CharaInfo::OnRightKey(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    if (m_bottompanel.empty()) return;
    OnPageChanging(nullptr);
    cycle_index(m_page_index, m_bottompanel.size(), 1);
    auto changed = std::make_shared<::cw::lib::Window>();
    changed->id = m_page_index;
    OnPageChanged(changed);
}
::cw::lib::Integer CharaInfo::maximum_scroll() const
{
    if (!charainfo_current_page_scrolls(*this)) return 0;
    const auto content_height = [&]() -> ::cw::lib::Integer {
        if (m_page_index < 0 || m_page_index >= static_cast<::cw::lib::Integer>(m_bottompanel.size()))
            return ::cw::lib::Integer{0};
        const auto object = std::get_if<::cw::lib::ObjectPtr>(
            &m_bottompanel[static_cast<std::size_t>(m_page_index)].storage());
        if (!object || !*object) return ::cw::lib::Integer{0};
        if (const auto desc = std::dynamic_pointer_cast<DescPanel>(*object)) {
            const auto lines = desc->m_text.empty()
                ? std::size_t{0}
                : 1 + static_cast<std::size_t>(std::count(desc->m_text.begin(), desc->m_text.end(), '\n'));
            return static_cast<::cw::lib::Integer>(
                charainfo_scaled(static_cast<int>(7 + lines * 13 + 2)));
        }
        if (const auto history = std::dynamic_pointer_cast<HistoryPanel>(*object))
            return static_cast<::cw::lib::Integer>(
                charainfo_scaled(static_cast<int>(8 + history->m_coupons.size() * 17)));
        if (const auto edit = std::dynamic_pointer_cast<EditPanel>(*object))
            return static_cast<::cw::lib::Integer>(
                charainfo_scaled(static_cast<int>(10 + edit->m_headers.size() * 17)));
        if (const auto cards = std::dynamic_pointer_cast<CardPanel>(*object)) {
            cards->draw(false);
            ::cw::lib::Integer result = charainfo_scaled(55 + (cards->m_hold_all ? 20 : 0));
            for (const auto& rect : cards->m_card_rects)
                result = std::max<::cw::lib::Integer>(
                    result, static_cast<::cw::lib::Integer>(
                        charainfo_scaled(static_cast<int>(rect.y + rect.height + 4))));
            return result;
        }
        if (const auto status = std::dynamic_pointer_cast<StatusPanel>(*object)) {
            const auto character = character_value(status->m_ccard);
            if (!character) return ::cw::lib::Integer{0};
            ::cw::lib::Integer rows = 1;
            if (character->is_poison()) ++rows;
            if (character->is_paralyze()) ++rows;
            if (character->m_mentality_dur && character->m_mentality != "Normal") ++rows;
            if (character->is_bind()) ++rows;
            if (character->is_silence()) ++rows;
            if (character->is_faceup()) ++rows;
            if (character->is_antimagic()) ++rows;
            if (character->m_enhance_act && character->m_enhance_act_dur) ++rows;
            if (character->m_enhance_avo && character->m_enhance_avo_dur) ++rows;
            if (character->m_enhance_res && character->m_enhance_res_dur) ++rows;
            if (character->m_enhance_def && character->m_enhance_def_dur) ++rows;
            return static_cast<::cw::lib::Integer>(
                charainfo_scaled(static_cast<int>(8 + rows * 17)));
        }
        return ::cw::lib::Integer{0};
    }();
    const auto viewport_height = charainfo_layout_notebook_h() - charainfo_layout_tab_h() - charainfo_scaled(8);
    return std::max<::cw::lib::Integer>(0, content_height - viewport_height);
}
::cw::lib::Integer CharaInfo::scroll_unit() const
{
    if (m_page_index >= 0 && m_page_index < static_cast<::cw::lib::Integer>(m_bottompanel.size()) &&
        object_value<DescPanel>(m_bottompanel[static_cast<std::size_t>(m_page_index)])) return charainfo_scaled(13);
    return charainfo_scaled(17);
}

void CharaInfo::scroll_content(::cw::lib::Integer delta)
{
    if (m_modal_ended || m_child_open || !charainfo_current_page_scrolls(*this)) return;
    m_scroll_y = std::clamp<::cw::lib::Integer>(m_scroll_y + delta, 0, maximum_scroll());
    draw(false);
}
void CharaInfo::OnEnd(std::shared_ptr<::cw::lib::Window>)
{
    m_scroll_y = maximum_scroll();
    draw(false);
}
void CharaInfo::OnMouseWheel(std::shared_ptr<::cw::lib::Window> arg_event) {
    // wx は正のホイール回転を前のメンバー・タブへの移動として扱う。Win32
    // の GET_WHEEL_DELTA_WPARAM も同じ符号なので、
    // 逆向きに比較するとネイティブの二つのホイール操作経路がどちらも逆になる。
    const bool backward = !arg_event || ::cw::util::get_wheelrotation(arg_event) > 0;
    const auto split = charainfo_layout_client_h() / 2;
    const bool top = arg_event && arg_event->bounds.y < split;
    if (top && can_change_character()) {
        if (backward) OnClickLeftBtn(arg_event);
        else OnClickRightBtn(arg_event);
    } else if (!m_bottompanel.empty()) {
        OnPageChanging(nullptr);
        if (backward) {
            if (m_page_index <= 0) m_page_index = static_cast<::cw::lib::Integer>(m_bottompanel.size()) - 1;
            else --m_page_index;
        } else if (m_page_index + 1 >= static_cast<::cw::lib::Integer>(m_bottompanel.size()) ) m_page_index = 0;
        else ++m_page_index;
        auto changed = std::make_shared<::cw::lib::Window>();
        changed->id = m_page_index;
        OnPageChanged(changed);
    }
}
void CharaInfo::OnDebugMode(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    const auto runtime = ::cw::g_cwpy;
    if (!runtime) {
        update_debug();
        draw(true);
        return;
    }

    // Python と同様、ゲームスレッドで CWPy を変更してから
    // 、タブと画像の更新を UI スレッドへ送る。 特に set_debug()
    // は表示カード一覧を変更するため、ゲームループが走査中かも
    // しれないネイティブのウィンドウコールバックからは呼ばない。
    charainfo_play_sound("page");
    const std::weak_ptr<int> lifetime = m_callback_lifetime;
    runtime->exec_func([this, lifetime, runtime](const ::cw::lib::Arguments&, const ::cw::lib::KeywordArguments&) {
        // 反転はゲームキュー上で計算する。連続して受け付けた要求は
        // 、古い値を共通に保持せず、直前の反転結果を順に参照する。
        runtime->set_debug(!runtime->is_debugmode());
        if (const auto frame = object_value<::cw::frame::Frame>(runtime->m_frame)) {
            frame->exec_func([this, lifetime](const ::cw::lib::Arguments&, const ::cw::lib::KeywordArguments&) {
                if (!lifetime.expired() && !m_modal_ended) {
                    update_debug();
                    draw(true);
                }
                return ::cw::lib::Value{};
            });
        }
        return ::cw::lib::Value{};
    });
}

void CharaInfo::update_debug()
{
    m_cwpy_debug = debug_mode(); invalidate_panel(m_descpanel); invalidate_panel(m_historypanel); invalidate_panel(m_editpanel); if (m_skillpanel) m_skillpanel->update_debug(); if (m_itempanel) m_itempanel->update_debug(); if (m_beastpanel) m_beastpanel->update_debug(); if (m_ui_backend) m_ui_backend->invalidate(*this);
}

void CharaInfo::OnCancel(std::shared_ptr<::cw::lib::Window>)
{
    if (m_modal_ended || m_child_open) return;
    charainfo_play_sound("click");
    (m_ui_backend ? m_ui_backend : get_charainfo_ui_backend())->end_modal(*this, 0);
}
void CharaInfo::OnDestroy(std::shared_ptr<::cw::lib::Window>)
{
    (m_ui_backend ? m_ui_backend : get_charainfo_ui_backend())->end_modal(*this, m_modal_result);
}

void CharaInfo::OnClickLeftBtn(std::shared_ptr<::cw::lib::Window> arg_event)
{
    if (m_modal_ended || m_child_open) return;
    (void)arg_event;
    if (const auto standby = dynamic_cast<StandbyCharaInfo*>(this); standby && !standby->m_headers.empty()) {
        standby->change_character(-1); return;
    }
    if (const auto active = dynamic_cast<ActiveCharaInfo*>(this); active && !active->m_cards.empty()) {
        charainfo_play_sound("page");
        cycle_index(active->m_index, active->m_cards.size(), -1);
        m_ccard = ::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(active->m_cards[static_cast<std::size_t>(active->m_index)]));
        if (active->m_parent) {
            if (const auto card = std::dynamic_pointer_cast<::cw::sprite::card::CWPyCard>(active->m_cards[static_cast<std::size_t>(active->m_index)]))
                active->m_parent->change_selection(card);
        }
    }
    refresh_character_panels(*this);
}

void CharaInfo::OnClickRightBtn(std::shared_ptr<::cw::lib::Window> arg_event)
{
    if (m_modal_ended || m_child_open) return;
    (void)arg_event;
    if (const auto standby = dynamic_cast<StandbyCharaInfo*>(this); standby && !standby->m_headers.empty()) {
        standby->change_character(1); return;
    }
    if (const auto active = dynamic_cast<ActiveCharaInfo*>(this); active && !active->m_cards.empty()) {
        charainfo_play_sound("page");
        cycle_index(active->m_index, active->m_cards.size(), 1);
        m_ccard = ::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(active->m_cards[static_cast<std::size_t>(active->m_index)]));
        if (active->m_parent) {
            if (const auto card = std::dynamic_pointer_cast<::cw::sprite::card::CWPyCard>(active->m_cards[static_cast<std::size_t>(active->m_index)]))
                active->m_parent->change_selection(card);
        }
    }
    refresh_character_panels(*this);
}

void CharaInfo::OnPageChanged(std::shared_ptr<::cw::lib::Window> arg_event)
{
    if (arg_event) m_page_index = arg_event->id;
    if (m_page_index < 0 || m_page_index >= static_cast<::cw::lib::Integer>(m_bottompanel.size())) m_page_index = 0;
    if (m_page_scrolls.size() < m_bottompanel.size()) m_page_scrolls.resize(m_bottompanel.size(), 0);
    m_scroll_y = (m_page_index >= 0 && m_page_index < static_cast<::cw::lib::Integer>(m_page_scrolls.size()))
        ? m_page_scrolls[static_cast<std::size_t>(m_page_index)] : 0;
    m_scroll_y = std::clamp<::cw::lib::Integer>(m_scroll_y, 0, maximum_scroll());
    if (const auto object = std::get_if<::cw::lib::ObjectPtr>(&m_titlepanel.storage()); object && *object)
        if (const auto title = std::dynamic_pointer_cast<TitlePanel>(*object)) {
            title->m_notebook = arg_event ? ::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(arg_event)) : title->m_notebook;
            title->draw(true);
        }
    draw(true);
}
void CharaInfo::OnPageChanging(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
#if defined(CARDWIRTH_LOG_ENABLED)
    ::cardwirth::debug::log("CharaInfo tab change");
#endif
    if (m_page_scrolls.size() < m_bottompanel.size()) m_page_scrolls.resize(m_bottompanel.size(), 0);
    if (m_page_index >= 0 && m_page_index < static_cast<::cw::lib::Integer>(m_page_scrolls.size()))
        m_page_scrolls[static_cast<std::size_t>(m_page_index)] = m_scroll_y;
    charainfo_play_sound("click");
    if (m_toppanel) m_toppanel->draw(true);
    if (m_ui_backend) m_ui_backend->invalidate(*this);
}
void CharaInfo::draw(bool arg_update) { if (m_modal_ended) return; m_published_controls_dirty=true; if (m_toppanel) m_toppanel->draw(arg_update); if (!m_bottompanel.empty()) { if (m_page_index < 0 || m_page_index >= static_cast<::cw::lib::Integer>(m_bottompanel.size())) m_page_index = 0; if (arg_update) invalidate_panel(m_bottompanel[static_cast<std::size_t>(m_page_index)]); } if (m_ui_backend) { m_ui_backend->invalidate(*this); m_ui_backend->paint(*this, nullptr); } }
void CharaInfo::_do_layout() { if (m_ui_backend) m_ui_backend->layout(*this); }

::cw::dialog::DialogPresentation CharaInfo::dialog_presentation()
{
    using Action = ::cw::dialog::DialogPresentationAction;
    if (m_modal_ended) {dialog_presentation_pointer_cancel();return {};}
    refresh_font_metrics();
    process_ui_events();
    if(m_modal_ended) {dialog_presentation_pointer_cancel();return {};}
    if(m_child_open || !can_change_character()) dialog_presentation_pointer_cancel();
    if(m_page_repeat && m_page_repeat_timer.consume(character_input_time(),page_repeat_interval())) {
        if(m_page_repeat<0) OnClickLeftBtn(nullptr);else OnClickRightBtn(nullptr);
        if(m_child_open) dialog_presentation_pointer_cancel();
        if(m_modal_ended) return {};
    }
    if (m_ui_backend && (m_ui_invalidated || !m_ui_surface)) m_ui_backend->paint(*this, nullptr);
    ::cw::dialog::DialogPresentation result;
    result.route = "CHARAINFO";
    result.title = m_title_text;
    result.surface = m_ui_surface;
    // Python の TopPanel と同じ 33ms のタイマー間隔を使う。静止した顔画像は
    // タイマーを要求しない。 動画などのアニメーションは既存の時間に基づくフレーム選択を維持する。
    result.refresh_interval_ms = platform_refresh_interval(
        m_toppanel && m_toppanel->m_animating ? 33U : 0U);
    if (!m_ui_surface) return result;
    const auto width = m_ui_surface->size.width;
    m_side_navigation.append(result,{0,0,width,charainfo_layout_top_h()},!m_child_open && can_change_character(),-2);
    const auto surface_h = charainfo_layout_surface_h();
    const auto tab_h = charainfo_layout_tab_h();
    const auto count = static_cast<::cw::lib::Integer>(m_bottompanel.size());
    const auto tab_w = count > 0 ? std::min<::cw::lib::Integer>(charainfo_scaled(84), width / count) : 0;
    auto tab_x = count > 0 ? std::max<::cw::lib::Integer>(0, (width - tab_w * count) / 2) : 0;
    for (::cw::lib::Integer index = 0; index < count; ++index) {
        result.controls.push_back({Action::Select, static_cast<int>(index),
            static_cast<float>(tab_x), static_cast<float>(surface_h),
            static_cast<float>(tab_w), static_cast<float>(tab_h), true});
        tab_x += tab_w;
    }
    const auto bottom_y = surface_h + tab_h;
    result.controls.push_back({Action::Previous, -1, 0.0F, static_cast<float>(bottom_y),
        static_cast<float>(charainfo_scaled(30)), static_cast<float>(charainfo_layout_bottom_h()), !m_child_open && can_change_character(),true});
    result.controls.back().hover_highlight = true;
    result.controls.push_back({Action::Next, -1,
        static_cast<float>(width - charainfo_scaled(30)), static_cast<float>(bottom_y),
        static_cast<float>(charainfo_scaled(30)), static_cast<float>(charainfo_layout_bottom_h()), !m_child_open && can_change_character(),true});
    result.controls.back().hover_highlight = true;
    result.controls.push_back({Action::Cancel, -1,
        static_cast<float>((width - charainfo_scaled(85)) / 2),
        static_cast<float>(bottom_y + charainfo_scaled(3)),
        static_cast<float>(charainfo_scaled(85)), static_cast<float>(charainfo_footer_button_h()), true});
    result.controls.back().hover_highlight = true;
    if (const auto found = charainfo_paint_hits().find(this); found != charainfo_paint_hits().end()) {
        const auto& hits = found->second;
        const auto clip = hits.content_rect;
        const auto add = [&](::cw::lib::Rect rect, int index, bool scroll = true, bool enabled = true) {
            if (scroll) rect.y -= m_scroll_y;
            const auto left = std::max(rect.x, clip.x);
            const auto top = std::max(rect.y, clip.y);
            const auto right = std::min(rect.x + rect.width, clip.x + clip.width);
            const auto bottom = std::min(rect.y + rect.height, clip.y + clip.height);
            if (left < right && top < bottom)
                result.controls.push_back({Action::Auxiliary, index, static_cast<float>(left),
                    static_cast<float>(top), static_cast<float>(right - left), static_cast<float>(bottom - top), enabled});
        };
        if (page_editor() != CharacterPageEditor::None) {
            auto editor_rect = clip;
            if (maximum_scroll() > 0) editor_rect.width -= charainfo_scaled(10);
            const auto control_count = result.controls.size();
            add(editor_rect, 3200, false);
            if (result.controls.size() != control_count) {
                result.controls.back().label = "編集";
                result.controls.back().tooltip = "クリックして編集";
                // Py はリソース表に CURSOR_FINGER を保持する。共
                // 通のネイティブリソースもスキン・カーソルの再読み込みまでキャッシ
                // ュし、 表示やポインターの更新ごとにファイルシステムを調べない。
                static thread_local std::uint64_t generation = std::numeric_limits<std::uint64_t>::max();
                static thread_local ::cw::lib::String skin;
                static thread_local ::cw::dialog::DialogPresentationCursor cursor;
                const auto current_generation = ::cw::setting::cursor_resource_generation();
                const auto current_skin = ::cw::g_cwpy && ::cw::g_cwpy->m_rsrc
                    ? ::cw::g_cwpy->m_rsrc->m_skindir : ::cw::lib::String{};
                if (generation != current_generation || skin != current_skin) {
                    cursor = ::cw::dialog::resolve_presentation_cursor(
                        ::cw::dialog::DialogCursorShape::Finger, current_skin);
                    generation = current_generation;
                    skin = current_skin;
                }
                result.controls.back().cursor = cursor;
            }
        }
        for (const auto& [rect, index] : hits.card_row_rects) add(rect, 1000 + static_cast<int>(index));
        for (std::size_t index = 0; index < hits.edit_button_rects.size(); ++index)
            add(hits.edit_button_rects[index], 2000 + static_cast<int>(index));
        if (hits.has_deck_button) add(hits.deck_button_rect, 3000, false);
        for (std::size_t i = 0; i < hits.card_page_buttons.size(); ++i)
            add(hits.card_page_buttons[i].first, 3002 + static_cast<int>(i), false, hits.card_page_buttons[i].second);
        if (hits.has_hold_all) add(hits.hold_all_rect, 3001);
        if (const auto maximum = maximum_scroll(); maximum > 0) {
            const ::cw::lib::Rect track{clip.x + clip.width - charainfo_scaled(10), clip.y,
                                       charainfo_scaled(10), clip.height};
            result.controls.push_back({Action::Auxiliary, 3100, static_cast<float>(track.x),
                static_cast<float>(track.y), static_cast<float>(track.width), static_cast<float>(track.height), true, true});
        }
    }
    m_published_controls=result.controls;m_published_controls_dirty=false;
    // ポインターが動いていなくても、再描画で当たり判定領域が再構築される場合がある。 位
    // 置の再評価で状態が変わるのは一度だけなので、それに伴う更新は次の処理で収束する。
    if (m_has_hover_position) apply_dialog_presentation_hover(m_last_hover_x, m_last_hover_y);
    return result;
}

void CharaInfo::dialog_presentation_action(const ::cw::dialog::DialogPresentationAction action,
                                           const int index)
{
    using Action = ::cw::dialog::DialogPresentationAction;
    if (m_modal_ended || m_child_open) return;
    dialog_presentation_pointer_cancel();
    auto event = std::make_shared<::cw::lib::Window>();
    event->id = index;
    if (action == Action::Cancel) OnCancel(event);
    else if (action == Action::Previous) OnClickLeftBtn(event);
    else if (action == Action::Next) OnClickRightBtn(event);
    // 共通ポインター API は連続した Select クリックを Activate に変
    // 換する。 Python のタブは素早い連続クリックも含め、毎回そのタブを選択する。
    else if ((action == Action::Select || action == Action::Activate) &&
             index >= 0 && index < static_cast<int>(m_bottompanel.size())) {
        OnPageChanging(event); OnPageChanged(event);
    } else if (action == Action::Auxiliary && index >= 1000 && index < 2000) {
        if (auto* panel = charainfo_active_card_panel(*this)) {
            const auto selected = static_cast<std::size_t>(index - 1000);
            if (selected < panel->m_headers.size() && panel->m_headers[selected]) {
                for (auto& header : panel->m_headers) if (header) header->m_negaflag = false;
                if (panel->m_hold_all) (*panel->m_hold_all)->m_negaflag = false;
                panel->m_headers[selected]->m_negaflag = true;
                OnEnter(event);
            }
        }
    } else if (action == Action::Auxiliary && index >= 2000 && index < 3000) {
        if (auto* edit = charainfo_active_edit_panel(*this)) {
            const auto selected = static_cast<std::size_t>(index - 2000);
            for (auto& button : edit->m_headers) if (button) button->m_negaflag = false;
            if (selected < edit->m_headers.size() && edit->m_headers[selected]) {
                edit->m_headers[selected]->m_negaflag = true; edit->OnLeftUp(event);
            }
        }
    } else if (action == Action::Auxiliary && index == 3200) {
        open_page_editor(page_editor());
    } else if (action == Action::Auxiliary && index == 3000) {
        if (auto* panel = charainfo_active_card_panel(*this)) panel->OnDeck(event);
    } else if (action == Action::Auxiliary && (index == 3002 || index == 3003)) {
        if (auto* panel = charainfo_active_card_panel(*this)) {
            const auto before = panel->m_card_page;
            panel->change_card_page(index == 3002 ? -1 : 1);
            if (before != panel->m_card_page) charainfo_play_sound("page");
        }
    } else if (action == Action::Auxiliary && index == 3001) {
        if (auto* panel = charainfo_active_card_panel(*this); panel && panel->m_hold_all && *panel->m_hold_all) {
            for (auto& header : panel->m_headers) if (header) header->m_negaflag = false;
            (*panel->m_hold_all)->m_negaflag = true;
            panel->OnLeftUp(event);
        }
    }
    if (m_ui_backend && !m_modal_ended) m_ui_backend->invalidate(*this);
}

void CharaInfo::dialog_presentation_key(::cw::dialog::DialogPresentationKey key, bool pressed,
    ::cw::dialog::DialogPresentationModifiers modifiers)
{
    if (!pressed || m_modal_ended || m_child_open) return;
    using Key = ::cw::dialog::DialogPresentationKey;
    const bool control = (modifiers & ::cw::dialog::DialogPresentationModifierControl) != 0;
    const bool shift=(modifiers & ::cw::dialog::DialogPresentationModifierShift)!=0;
    if(modifiers & ::cw::dialog::DialogPresentationModifierAlt) return;
    dialog_presentation_pointer_cancel();
    if(!control && (key==Key::Escape || key==Key::Underscore || (key==Key::Minus && shift))) {
        m_has_hover_position = false;
        OnCancel(nullptr);return;
    }
    if(shift) return;
    // 新たなポインター移動が来るまでは、最後のポインター位置よりキーボード選択を優先する。
    m_has_hover_position = false;
    if (control) {
        if (key == Key::Left) OnClickLeftBtn(nullptr);
        else if (key == Key::Right) OnClickRightBtn(nullptr);
        else if (key == Key::Enter) OnOpenInfo(nullptr);
        else if (key == Key::C) OnCopyDetail(nullptr);
        else if (key == Key::D) OnDebugMode(nullptr);
    } else {
        switch (key) {
        case Key::Left: OnLeftKey(nullptr); break;
        case Key::Right: OnRightKey(nullptr); break;
        case Key::Up: OnUp(nullptr); break;
        case Key::Down: OnDown(nullptr); break;
        case Key::PageUp: OnPageUp(nullptr); break;
        case Key::PageDown: OnPageDown(nullptr); break;
        case Key::Home: OnHome(nullptr); break;
        case Key::End: OnEnd(nullptr); break;
        case Key::Enter: case Key::Space: OnEnter(nullptr); break;
        case Key::Backspace: OnCancel(nullptr); break;
        default: return;
        }
    }
    if (!m_modal_ended) draw(false);
}

void CharaInfo::dialog_presentation_hover(float x, float y)
{
    m_has_hover_position = std::isfinite(x) && std::isfinite(y) && x >= 0.0F && y >= 0.0F;
    if (m_has_hover_position) { m_last_hover_x = x; m_last_hover_y = y; }
    if (m_modal_ended || m_child_open) return;
    if(m_published_controls.empty() || m_published_controls_dirty) (void)dialog_presentation();
    if(m_modal_ended || m_child_open) return;
    apply_dialog_presentation_hover(x, y);
}

void CharaInfo::apply_dialog_presentation_hover(float x, float y)
{
    if (m_modal_ended || m_child_open) return;
    int selected = -1;
    for (auto it = m_published_controls.rbegin(); it != m_published_controls.rend(); ++it)
        if (it->enabled && x >= it->x && x < it->x + it->width && y >= it->y && y < it->y + it->height) {
            if (it->action == ::cw::dialog::DialogPresentationAction::Auxiliary) selected = it->index;
            break;
        }
    bool changed = false;
    const auto set = [&](bool& target, bool value) { if (target != value) { target = value; changed = true; } };
    if (auto* panel = charainfo_active_card_panel(*this)) {
        for (std::size_t i = 0; i < panel->m_headers.size(); ++i)
            if (panel->m_headers[i]) set(panel->m_headers[i]->m_negaflag, selected == 1000 + static_cast<int>(i));
        if (panel->m_hold_all && *panel->m_hold_all) set((*panel->m_hold_all)->m_negaflag, selected == 3001);
    }
    if (auto* panel = charainfo_active_edit_panel(*this)) {
        for (std::size_t i = 0; i < panel->m_headers.size(); ++i)
            if (panel->m_headers[i]) set(panel->m_headers[i]->m_negaflag, selected == 2000 + static_cast<int>(i));
    }
    if (changed && m_ui_backend) m_ui_backend->invalidate(*this);
}

void CharaInfo::dialog_presentation_context(float x, float y)
{
    if (m_modal_ended || m_child_open) return;
    dialog_presentation_hover(x, y);
    const auto found = charainfo_paint_hits().find(this);
    if (found != charainfo_paint_hits().end()) {
        const auto rect = found->second.content_rect;
        if (x >= rect.x && x < rect.x + rect.width && y >= rect.y && y < rect.y + rect.height)
            if (auto* panel = charainfo_active_card_panel(*this)) { panel->OnRightUp(nullptr); return; }
    }
    OnCancel(nullptr);
}

void CharaInfo::dialog_presentation_scroll(float x, float y, int lines)
{
    if (!lines || m_modal_ended || m_child_open) return;
    const auto found = charainfo_paint_hits().find(this);
    if (found != charainfo_paint_hits().end() && charainfo_current_page_scrolls(*this)) {
        const auto rect = found->second.content_rect;
        if (x >= rect.x && x < rect.x + rect.width && y >= rect.y && y < rect.y + rect.height) {
            scroll_content(lines * scroll_unit());
            return;
        }
    }
    auto event = std::make_shared<::cw::lib::Window>();
    event->bounds.x = static_cast<::cw::lib::Integer>(x);
    event->bounds.y = static_cast<::cw::lib::Integer>(y);
    event->id = lines < 0 ? 1 : -1; // 共通スクロールは正が下方向、wx は正が上方向である。
    OnMouseWheel(event);
}

void CharaInfo::dialog_presentation_pointer(::cw::dialog::DialogPresentationAction action,
    int index, float x, float y, bool pressed)
{
    using Action=::cw::dialog::DialogPresentationAction;
    if(m_modal_ended || m_child_open) {dialog_presentation_pointer_cancel();return;}
    if(index==-1 && (action==Action::Previous || action==Action::Next)) {
        if(!pressed || !can_change_character()) {dialog_presentation_pointer_cancel();return;}
        const auto hit=std::find_if(m_published_controls.begin(),m_published_controls.end(),[&](const auto& c) {
            return c.action==action && c.index==index && c.enabled;
        });
        if(hit==m_published_controls.end() || !std::isfinite(x) || !std::isfinite(y) ||
            x<hit->x || y<hit->y || x>=hit->x+hit->width || y>=hit->y+hit->height) {
            m_page_repeat=0;return;
        }
        if(m_page_capture) return;
        m_page_capture=m_page_repeat=action==Action::Previous?-1:1;
        if(m_page_repeat<0) OnClickLeftBtn(nullptr);else OnClickRightBtn(nullptr);
        if(!m_modal_ended && !m_child_open && m_page_capture)
            m_page_repeat_timer.start(character_input_time(),page_repeat_interval());
        return;
    }
    if(action!=Action::Auxiliary || index!=3100) return;

    const auto found = charainfo_paint_hits().find(this);
    if (found == charainfo_paint_hits().end()) return;
    const auto track = found->second.content_rect;
    const auto maximum = maximum_scroll();
    const auto thumb = std::max<::cw::lib::Integer>(charainfo_scaled(16), track.height * track.height / (track.height + maximum));
    const auto fraction = std::clamp((y - track.y - thumb / 2.0) / std::max<::cw::lib::Integer>(1, track.height - thumb), 0.0, 1.0);
    m_scroll_y = static_cast<::cw::lib::Integer>(std::lround(fraction * maximum));
    draw(false);
}

::cw::dialog::ButtonRepeatTimer::Clock::time_point CharaInfo::character_input_time() const noexcept {
    return ::cw::dialog::ButtonRepeatTimer::Clock::now();
}
std::chrono::milliseconds CharaInfo::page_repeat_interval() const noexcept {
    return ::cw::dialog::dialog_page_repeat_interval();
}
void CharaInfo::dialog_presentation_pointer_cancel() {m_page_repeat=0;m_page_capture=0;}
bool CharaInfo::dialog_presentation_needs_refresh() const noexcept {
    if (!m_modal_ended) {
        try { if (font_metrics_changed()) return true; }
        catch (...) { return true; }
    }
    return ::cw::dialog::DialogPresentationProvider::dialog_presentation_needs_refresh() ||
        (!m_modal_ended && !m_child_open && can_change_character() && m_page_repeat &&
         m_page_repeat_timer.due(character_input_time()));
}

bool CharaInfo::dialog_presentation_finished() const noexcept { return m_modal_ended; }
void CharaInfo::dialog_presentation_complete() {dialog_presentation_pointer_cancel();}
void CharaInfo::process_ui_events()
{
    if (m_ui_backend) m_ui_backend->pump(*this);
}
std::shared_ptr<CharainfoUiBackend> CharaInfo::ui_backend() const { return m_ui_backend; }

void DeckDialog::initialize(std::shared_ptr<::cw::lib::Window> arg_parent, ::cw::lib::Integer arg_pocket, std::shared_ptr<::cw::character::Character> arg_ccard)
{
    dialog_retained_surface().reset();
    m_operation_pending = false;
    m_source_info = nullptr;
    m_title_text.clear();
    dialog_presentation_pointer_cancel();
    m_focus_target.reset(); m_keyboard_focus = false;
    m_cached_presentation = {}; m_publication_scale = -1; m_publication_fonts.clear();
    m_window_parent = std::move(arg_parent); m_parent = m_window_parent ? ::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(m_window_parent)) : ::cw::lib::Value{}; m_ui_backend = get_charainfo_ui_backend(); m_ui_surface.reset(); m_ui_invalidated = true; m_modal_ended = false; m_scroll_y = 0; m_pocket = arg_pocket; m_ccard = std::move(arg_ccard); m__reg_types = {"All", "All_No_Personal", "Skill", "Item", "Beast", "Personal"}; m_reg_type = m_pocket == ::cw::g_POCKET_SKILL ? "Skill" : m_pocket == ::cw::g_POCKET_ITEM ? "Item" : m_pocket == ::cw::g_POCKET_BEAST ? "Beast" : "All"; m_registrations = m_ccard ? m_ccard->get_deck_registrations() : std::vector<std::map<::cw::lib::String, ::cw::lib::Value>>{}; _rebuild_area_buttons(); _layout();
}
void DeckDialog::OnSize(std::shared_ptr<::cw::lib::Window> arg_event) { (void)arg_event; _layout(); }
void DeckDialog::_layout() { _layout_area_buttons(); m_ui_invalidated = true; request_dialog_presentation_refresh(); }
void DeckDialog::_do_load_pc(::cw::lib::Value arg_target) { if (const auto character = character_value(arg_target)) m_ccard = character; else if (const auto player = object_value<::cw::character::Player>(arg_target)) m_ccard = player; else if (const auto header = object_value<::cw::header::AdventurerHeader>(arg_target)) { const auto loaded = load_adventurer_value(header); if (const auto loaded_character = character_value(loaded)) m_ccard = loaded_character; else return; } else return; m_registrations = m_ccard->get_deck_registrations(); _rebuild_area_buttons(); }
void DeckDialog::OnLoadLeftPC(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    // 再入の保護は dialog_presentation_action()
    // が行う。このハンドラーは保護中の処理から呼ばれるため、
    // m_operation_pending を理由に戻ると
    // 、PC の矢印が処理を始める前に自身を無効にしてしまう。
    if (m_modal_ended || !can_change_character()) return;
    if (m_source_info) {
        // このモーダルの表示中は親を子画面表示中とし、覆われ
        // た親への入力を防ぐ。 ただし、この子からの移動要求
        // は親の正規のキャラクター切り替え処理を使う必要があ
        // る。Python も親の index を更新する。
        const bool child_open = m_source_info->m_child_open;
        m_source_info->m_child_open = false;
        try { m_source_info->OnClickLeftBtn(nullptr); }
        catch (...) { m_source_info->m_child_open = child_open; throw; }
        m_source_info->m_child_open = child_open;
        m_ccard = character_value(m_source_info->m_ccard);
        m_registrations = m_ccard ? m_ccard->get_deck_registrations() : std::vector<std::map<::cw::lib::String, ::cw::lib::Value>>{};
        _rebuild_area_buttons();
    }
    invalidate_presentation();
}
void DeckDialog::OnLoadRightPC(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    if (m_modal_ended || !can_change_character()) return;
    if (m_source_info) {
        const bool child_open = m_source_info->m_child_open;
        m_source_info->m_child_open = false;
        try { m_source_info->OnClickRightBtn(nullptr); }
        catch (...) { m_source_info->m_child_open = child_open; throw; }
        m_source_info->m_child_open = child_open;
        m_ccard = character_value(m_source_info->m_ccard);
        m_registrations = m_ccard ? m_ccard->get_deck_registrations() : std::vector<std::map<::cw::lib::String, ::cw::lib::Value>>{};
        _rebuild_area_buttons();
    }
    invalidate_presentation();
}
void DeckDialog::OnPaint(std::shared_ptr<::cw::lib::Window> arg_event)
{
    invalidate_presentation();
    const auto view = dialog_presentation();
    if (arg_event && view.surface) arg_event->bounds = {0, 0, view.surface->size.width, view.surface->size.height};
}

bool DeckDialog::can_change_character() const
{
    return m_source_info && !m_source_info->m_modal_ended && m_source_info->can_change_character();
}

void DeckDialog::_rebuild_area_buttons()
{
    const auto row_target = [](const FocusTarget& target) {
        return target.first == ::cw::dialog::DialogPresentationAction::Select ||
            (target.first == ::cw::dialog::DialogPresentationAction::Auxiliary && decode_row_control(target.second));
    };
    if (m_pointer_target && (row_target(*m_pointer_target) ||
        (m_pointer_target->first == ::cw::dialog::DialogPresentationAction::Auxiliary &&
         m_pointer_target->second == scrollbar_control_id))) retire_pointer_input();
    if (m_space_target && row_target(*m_space_target)) m_space_target.reset();
    if (m_focus_target && row_target(*m_focus_target)) { m_focus_target.reset(); m_keyboard_focus = false; }
    m_area_buttons.clear();
    m_small_buttons.clear();
    m_visible_registration_values.clear();
    m_registrations = m_ccard ? m_ccard->get_deck_registrations()
                             : std::vector<std::map<::cw::lib::String, ::cw::lib::Value>>{};
    // PC・種別・編集の変更で行全体が置き換わる。 空の一覧から戻
    // る場合も含め、古い行を保持型の構成要素キャッシュに残さない。
    dialog_retained_surface().invalidate_all();
    for (const auto& registration : m_registrations)
        if (map_string(registration, "type") == m_reg_type)
            m_visible_registration_values.push_back(registration);
    // Python と同様、Unicode の str.lower() によるキーで一度だけ安定ソート
    // し、同じキーの順序を保つ。 元の名前と XML は変更せず、キーは再構築ごとに一度だけ計算する。
    std::vector<std::pair<::cw::lib::String, std::size_t>> order;
    order.reserve(m_visible_registration_values.size());
    for (std::size_t row = 0; row < m_visible_registration_values.size(); ++row)
        order.emplace_back(::cw::lib::encoding::lower(map_string(m_visible_registration_values[row], "name")), row);
    std::stable_sort(order.begin(), order.end(), [](const auto& left, const auto& right) { return left.first < right.first; });
    auto unsorted = std::move(m_visible_registration_values);
    m_visible_registration_values.clear();
    m_visible_registration_values.reserve(unsorted.size());
    for (const auto& entry : order) m_visible_registration_values.push_back(std::move(unsorted[entry.second]));
    for (std::size_t i = 0; i < m_visible_registration_values.size(); ++i) {
        auto button = std::make_shared<::cw::lib::Window>();
        button->id = static_cast<::cw::lib::Integer>(i);
        m_area_buttons.push_back(button);
    }
    m_visible_registrations = ::cw::lib::Value(static_cast<::cw::lib::Integer>(m_visible_registration_values.size()));
    // Python と同様、種別・PC・編集の変更後は絞り込み一覧を再構築して先頭に戻す。
    m_scroll_y = 0;
    _layout_area_buttons();
    m_ui_invalidated = true;
    request_dialog_presentation_refresh();
}
void DeckDialog::_layout_area_buttons()
{
    for (std::size_t i = 0; i < m_area_buttons.size(); ++i)
        if (m_area_buttons[i])
            m_area_buttons[i]->bounds = {0, charainfo_scaled(static_cast<int>(i) * 34),
                                         charainfo_scaled(maximum_scroll() > 0 ? 396 : 410), charainfo_scaled(30)};
}

bool DeckDialog::_check_headers_for_limited_beast(::cw::lib::Value arg_headers)
{
    const auto header = header_value(arg_headers);
    if (header) return deck_header_is_limited_beast(header);
    if (const auto matches = matches_from(arg_headers))
        for (const auto& match : matches->values)
            if (deck_header_is_limited_beast(match.header)) return true;
    return false;
}
bool DeckDialog::_match_header(::cw::lib::Value arg_header, ::cw::lib::String arg_name, ::cw::lib::String arg_desc, ::cw::lib::String arg_scenario, ::cw::lib::String arg_author, bool arg_ms, bool arg_ma) { const auto header = header_value(arg_header); if (!header || header->m_name != arg_name) return false; if (!arg_desc.empty() && header_description(header) != arg_desc) return false; if (arg_ms && !arg_scenario.empty() && header->m_scenario != arg_scenario) return false; if (arg_ma && !arg_author.empty() && header->m_author != arg_author) return false; return true; }
void DeckDialog::_remove_small_buttons() { m_small_buttons.clear(); }
void DeckDialog::OnRegTypeLeft(std::shared_ptr<::cw::lib::Window> arg_event) { (void)arg_event; if (m__reg_types.empty()) return; auto it = std::find(m__reg_types.begin(), m__reg_types.end(), m_reg_type); const auto index = it == m__reg_types.end() ? 0U : static_cast<std::size_t>(it - m__reg_types.begin()); m_reg_type = m__reg_types[(index + m__reg_types.size() - 1) % m__reg_types.size()]; _rebuild_area_buttons(); }
void DeckDialog::OnRegTypeRight(std::shared_ptr<::cw::lib::Window> arg_event) { (void)arg_event; if (m__reg_types.empty()) return; auto it = std::find(m__reg_types.begin(), m__reg_types.end(), m_reg_type); const auto index = it == m__reg_types.end() ? 0U : static_cast<std::size_t>(it - m__reg_types.begin()); m_reg_type = m__reg_types[(index + 1) % m__reg_types.size()]; _rebuild_area_buttons(); }

void DeckDialog::_on_change_reg_name(std::shared_ptr<::cw::lib::Window> arg_event, ::cw::lib::Integer arg_idx)
{
    (void)arg_event;
    if (!m_ccard || arg_idx < 0 || arg_idx >= static_cast<::cw::lib::Integer>(m_visible_registration_values.size())) return;
    const auto& registration = m_visible_registration_values[static_cast<std::size_t>(arg_idx)];
    const auto old_name = map_string(registration, "name");
    const auto type = map_string(registration, "type");
    const auto input = deck_common_text_input("登録名の変更", "新しい登録名を入力してください。", old_name);
    if (!input) return;
    const auto new_name = deck_trim(*input);
    if (new_name.empty()) {
        (void)deck_common_message("入力エラー", "登録名は空にできません", false);
        return;
    }
    if (new_name == old_name) return;
    for (const auto& existing : m_ccard->get_deck_registrations())
        if (map_string(existing, "name") == new_name && map_string(existing, "type") == type) {
            (void)deck_common_message("エラー", "同名の登録が既に存在します", false);
            return;
        }
    m_ccard->rename_deck_registration(old_name, new_name, type);
    m_registrations = m_ccard->get_deck_registrations();
    _rebuild_area_buttons();
    charainfo_play_sound("click");
    (void)deck_common_message("登録名", "登録名を変更しました: " + new_name, false);
}

void DeckDialog::_on_delete_registration(std::shared_ptr<::cw::lib::Window> arg_event, ::cw::lib::Integer arg_idx)
{
    (void)arg_event;
    if (!m_ccard || arg_idx < 0 || arg_idx >= static_cast<::cw::lib::Integer>(m_visible_registration_values.size())) return;
    const auto registration = m_visible_registration_values[static_cast<std::size_t>(arg_idx)];
    const auto name = map_string(registration, "name");
    if (name.empty()) return;
    if (!deck_common_message("確認", "登録 '" + name + "' を削除しますか？", true)) return;
    m_ccard->remove_deck_registration(name, map_string(registration, "type"));
    m_registrations = m_ccard->get_deck_registrations();
    _rebuild_area_buttons();
}

void DeckDialog::_on_update_registration(std::shared_ptr<::cw::lib::Window> arg_event, ::cw::lib::Integer arg_idx)
{
    if (arg_idx < 0 || arg_idx >= static_cast<::cw::lib::Integer>(m_visible_registration_values.size())) return;
    const auto registration = m_visible_registration_values[static_cast<std::size_t>(arg_idx)];
    const auto name = map_string(registration, "name");
    const auto type = map_string(registration, "type");
    if (name.empty()) return;
    if (!deck_common_message("確認", "登録 '" + name + "' を更新しますか？", true)) return;
    // 保存済み登録を取り除く前に、置き換える内容を検証する。 使用回数
    // 制限付きの召喚獣、エラー、キャンセルの場合は前の登録を維持する。
    OnRegister(arg_event, name, type, true);
}

std::vector<std::map<::cw::lib::String, ::cw::lib::Value>> DeckDialog::_search_item_sources(std::vector<std::tuple<::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String>> arg_items, std::optional<::cw::lib::String> arg_rtype, bool arg_match_scenario, bool arg_match_author)
{
    const auto type = arg_rtype.value_or(m_reg_type);
    const bool include_character = type != "Personal";
    const bool include_personal = type == "Personal" || type == "All";
    const auto selected_player = deck_as_player(m_ccard);
    const auto allowed = [this](const HeaderPtr& header) {
        return deck_header_allowed(header, m_ccard);
    };
    const auto matches_header = [&](const HeaderPtr& header, const auto& item) {
        const auto& [name, desc, unused_type, scenario, author, unused_personal] = item;
        (void)unused_type;
        (void)unused_personal;
        if (!allowed(header) || header->m_name != name) return false;
        if (!desc.empty() && header_description(header) != desc) return false;
        if (arg_match_scenario && !scenario.empty() && header->m_scenario != scenario) return false;
        if (arg_match_author && !author.empty() && header->m_author != author) return false;
        return true;
    };

    std::vector<HeaderPtr> character = all_character_headers(m_ccard);
    std::vector<HeaderPtr> personal;
    std::vector<HeaderPtr> backpack;
    std::vector<HeaderPtr> storehouse;
    if (include_personal && selected_player) personal = selected_player->m_personal_pocket;
    if (::cw::g_cwpy && ::cw::g_cwpy->m_ydata && *::cw::g_cwpy->m_ydata) {
        const auto& yado = **::cw::g_cwpy->m_ydata;
        if (yado.m_party && *yado.m_party) backpack = (*yado.m_party)->m_backpack;
        const auto scenario_data = object_value<::cw::data::ScenarioData>(::cw::g_cwpy->m_sdata);
        if (!::cw::g_cwpy->is_playingscenario() && !scenario_data) storehouse = yado.m_storehouse;
    }

    // Python と同様、同じ登録項目でもそれぞれ別の実体ヘッダ
    // ーを使う。 取得元ごとに使用済み集合を持ち、名前や解説が重
    // 複しても一つにまとめたり、同じカードを再利用したりしない。
    std::unordered_set<const ::cw::header::CardHeader*> used_character;
    std::unordered_set<const ::cw::header::CardHeader*> used_backpack;
    std::unordered_set<const ::cw::header::CardHeader*> used_storehouse;
    const auto append_match = [](const HeaderPtr& header, const ::cw::lib::String& source,
                                 std::unordered_set<const ::cw::header::CardHeader*>& used,
                                 const auto& item, const auto& matches_header,
                                 const std::shared_ptr<SearchMatchList>& matches) {
        if (!header || used.count(header.get()) || !matches_header(header, item)) return false;
        matches->values.push_back({source, pocket_path(header), header});
        used.insert(header.get());
        return true;
    };
    const auto find_one = [&](const std::vector<HeaderPtr>& source_headers, const ::cw::lib::String& source,
                              std::unordered_set<const ::cw::header::CardHeader*>& used, const auto& item,
                              const auto& matcher, const std::shared_ptr<SearchMatchList>& matches) {
        for (const auto& header : source_headers)
            if (append_match(header, source, used, item, matcher, matches))
                return true;
        return false;
    };

    // 登録の適用前に表示する状態と同じ利用可能枚数
    // を求める。 復元には所有者と取得元が必要なの
    // で、一致一覧には実体のヘッダーも保持する。
    std::map<std::pair<::cw::lib::String, ::cw::lib::String>, std::size_t> available;
    const auto count_headers = [&](const std::vector<HeaderPtr>& headers) {
        for (const auto& header : headers) if (allowed(header))
            ++available[{header->m_name, header_description(header)}];
    };
    count_headers(character);
    count_headers(personal);
    count_headers(backpack);
    count_headers(storehouse);

    std::vector<std::map<::cw::lib::String, ::cw::lib::Value>> result;
    for (const auto& item : arg_items) {
        const auto& [name, desc, item_type, scenario, author, personal_text] = item;
        const bool item_personal = personal_text == "True" || personal_text == "true" ||
            personal_text == "1" || personal_text == "yes";
        auto row = std::map<::cw::lib::String, ::cw::lib::Value>{};
        set_map_string(row, "name", name);
        set_map_string(row, "desc", desc);
        set_map_string(row, "type", item_type);
        set_map_string(row, "scenario", scenario);
        set_map_string(row, "author", author);
        set_map_bool(row, "personal", item_personal);
        const auto key = std::make_pair(name, desc);
        const auto available_it = available.find(key);
        const bool has = available_it != available.end() && available_it->second > 0;
        if (has) --available_it->second;
        set_map_bool(row, "has", has);
        set_map_string(row, "status", has ? "ある" : "なし");

        auto matches = std::make_shared<SearchMatchList>();
        if (item_personal && selected_player)
            find_one(personal, "Character", used_character, item, matches_header, matches);

        // 個人ポケットで見つかった個人用アイテムは再検索しない
        // 。 それ以外は Python と同様、通常のキャラク
        // ター、個人ポケット、荷物袋、カード置場の順に探す。
        const bool found_personal = !matches->values.empty();
        if (!(item_personal && found_personal)) {
            if (include_character)
                find_one(character, "Character", used_character, item, matches_header, matches);
            if (include_personal)
                find_one(personal, "Character", used_character, item, matches_header, matches);
            find_one(backpack, "Backpack", used_backpack, item, matches_header, matches);
            find_one(storehouse, "Storehouse", used_storehouse, item, matches_header, matches);
        }
        row["matches"] = std::static_pointer_cast<::cw::lib::Object>(matches);
        result.push_back(std::move(row));
    }
    m_last_search = result;
    return result;
}

void DeckDialog::OnArea(std::shared_ptr<::cw::lib::Window> arg_event)
{
    if (!arg_event || arg_event->id < 0 || arg_event->id >= static_cast<::cw::lib::Integer>(m_visible_registration_values.size())) {
        charainfo_play_sound("click");
        return;
    }
    const auto& registration = m_visible_registration_values[static_cast<std::size_t>(arg_event->id)];
    const auto rname = map_string(registration, "name");
    const auto type = map_string(registration, "type");
    const auto items = deck_registration_items(m_ccard, registration);
    m_last_search = _search_item_sources(items, type, false, false);
    m_title_text = rname.empty() ? "(名前なし)" : rname;

    if (!deck_common_message(m_title_text,
                             deck_load_summary(m_title_text, m_last_search), true, true)) return;
    if (!m_ccard || !::cw::g_cwpy) return;

    const auto player = deck_as_player(m_ccard);
    std::unordered_set<const ::cw::header::CardHeader*> personal_snapshot;
    if (player)
        for (const auto& header : player->m_personal_pocket)
            if (header) personal_snapshot.insert(header.get());

    if (type == "All" && player) {
        const auto personal_cards = player->m_personal_pocket;
        for (const auto& header : personal_cards)
            if (header) deck_trade_backpack(header);
    }

    auto results = _search_item_sources(items, type, false, false);
    std::unordered_set<const ::cw::header::CardHeader*> keep;
    for (const auto& row : results) {
        const auto matches_it = row.find("matches");
        if (matches_it == row.end()) continue;
        const auto matches = matches_from(matches_it->second);
        if (!matches) continue;
        for (const auto& match : matches->values)
            if (match.source == "Character" && match.header) keep.insert(match.header.get());
    }

    std::vector<HeaderPtr> current;
    if (type == "All") {
        current = all_character_headers(m_ccard);
        if (player) current.insert(current.end(), player->m_personal_pocket.begin(), player->m_personal_pocket.end());
    } else if (type == "All_No_Personal") {
        current = all_character_headers(m_ccard);
    } else if (type == "Personal") {
        if (player) current = player->m_personal_pocket;
    } else {
        current = character_headers(m_ccard, pocket_for_type(type));
    }
    for (const auto& header : current)
        if (header && !keep.count(header.get())) deck_trade_backpack(header);

    results = _search_item_sources(items, type, false, false);
    m_last_search = results;

    using Candidate = SearchMatch;
    std::map<std::pair<::cw::lib::String, ::cw::lib::String>, std::vector<Candidate>> avail_by_key;
    std::unordered_set<const ::cw::header::CardHeader*> used_header_ids;
    for (const auto& row : results) {
        const auto key = std::make_pair(map_string(row, "name"), map_string(row, "desc"));
        auto& list = avail_by_key[key];
        const auto matches_it = row.find("matches");
        if (matches_it == row.end()) continue;
        const auto matches = matches_from(matches_it->second);
        if (!matches) continue;
        for (const auto& match : matches->values) {
            if (!match.header || used_header_ids.count(match.header.get())) continue;
            list.push_back(match);
        }
    }

    for (const auto& row : results) {
        const auto key = std::make_pair(map_string(row, "name"), map_string(row, "desc"));
        auto candidates_all = avail_by_key[key];
        if (candidates_all.empty()) continue;

        const Candidate* pick = nullptr;
        if (type == "All" || type == "All_No_Personal") {
            const std::vector<::cw::lib::String> type_order =
                type == "All"
                    ? std::vector<::cw::lib::String>{"Skill", "Item", "Beast", "Personal"}
                    : std::vector<::cw::lib::String>{"Skill", "Item", "Beast"};
            for (const auto& preferred_type : type_order) {
                std::vector<const Candidate*> typed;
                for (const auto& candidate : candidates_all) {
                    if (!candidate.header) continue;
                    const bool is_original_personal = personal_snapshot.count(candidate.header.get()) != 0;
                    if (preferred_type == "Personal") {
                        if (is_original_personal) typed.push_back(&candidate);
                    } else if (deck_ppath_to_type(candidate.pocket, map_string(row, "type")) == preferred_type) {
                        typed.push_back(&candidate);
                    }
                }
                if (typed.empty()) continue;
                for (const auto& source : {::cw::lib::String("Character"),
                                           ::cw::lib::String("Backpack"),
                                           ::cw::lib::String("Storehouse")}) {
                    for (const auto* candidate : typed)
                        if (candidate->source == source) { pick = candidate; break; }
                    if (pick) break;
                }
                if (!pick) pick = typed.front();
                if (pick) break;
            }
        }
        if (!pick) {
            for (const auto& candidate : candidates_all)
                if (candidate.source == "Character") { pick = &candidate; break; }
        }
        if (!pick) {
            for (const auto& candidate : candidates_all)
                if (candidate.source == "Backpack") { pick = &candidate; break; }
        }
        if (!pick && !candidates_all.empty()) pick = &candidates_all.front();
        if (!pick || !pick->header) continue;

        const auto header = pick->header;
        const auto hid = header.get();
        for (auto& [unused_key, list] : avail_by_key) {
            (void)unused_key;
            list.erase(std::remove_if(list.begin(), list.end(),
                                      [hid](const Candidate& item) { return item.header.get() == hid; }),
                       list.end());
        }
        used_header_ids.insert(hid);

        if (deck_header_is_limited_beast(header)) {
            charainfo_play_sound("error");
            (void)deck_common_message(
                "エラー",
                "使用回数制限のある召喚獣カードが含まれているため、この操作は行えません",
                false);
            return;
        }

        if (const auto owner = header->m_personal_owner.lock(); owner &&
            !deck_personal_owner_is_self(header, m_ccard))
            owner->remove_personalpocket(header);

        if (map_bool(row, "personal")) {
            if (!header->is_backpackheader()) deck_trade_backpack(header);
            if (player) {
                if (header->m_personal_owner.expired()) {
                    player->add_personalpocket(header, -1);
                    if (const auto party = deck_party()) party->sort_backpack(std::nullopt, std::nullopt);
                }
            } else {
                deck_trade_playercard(m_ccard, header);
            }
        } else {
            deck_trade_playercard(m_ccard, header);
        }
    }

    if (::cw::g_cwpy->m_ydata && *::cw::g_cwpy->m_ydata) (*::cw::g_cwpy->m_ydata)->changed();
    m_registrations = m_ccard->get_deck_registrations();
    _rebuild_area_buttons();
    if (m_source_info) {
        refresh_character_panels(*m_source_info);
        m_source_info->draw(true);
    }
    charainfo_play_sound("click");
    m_modal_ended = true;
}

void DeckDialog::OnRegister(std::shared_ptr<::cw::lib::Window> arg_event, std::optional<::cw::lib::String> arg_preset_name, std::optional<::cw::lib::String> arg_preset_type, bool replace_existing)
{
    (void)arg_event;
    if (!m_ccard) return;
    std::optional<::cw::lib::String> requested_name = arg_preset_name;
    if (!requested_name)
        requested_name = deck_common_text_input(
            "カード登録", "登録名を入力してください。", "");
    if (!requested_name) return;
    const auto name = deck_trim(*requested_name);
    if (name.empty()) {
        (void)deck_common_message("入力エラー", "登録名は空にできません", false);
        return;
    }
    auto type = arg_preset_type.value_or(m_reg_type);
    if (!arg_preset_type) {
        const std::vector<::cw::lib::String> types{"Skill", "Item", "Beast", "All", "All_No_Personal", "Personal"};
        const auto initial = std::find(types.begin(), types.end(), m_reg_type);
        auto choice = std::make_shared<::cw::dialog::edit::SingleChoiceDialog>(
            "登録種別", "登録種別を選択してください",
            std::vector<::cw::lib::String>{"スキル", "アイテム", "召喚獣", "全体", "全体(私有物除外)", "私有物"},
            initial == types.end() ? 0 : static_cast<int>(initial - types.begin()));
        if (!show_common_modal_presentation(choice) || !choice->result()) return;
        type = types[static_cast<std::size_t>(*choice->result())];
    }
    if (std::find(m__reg_types.begin(), m__reg_types.end(), type) == m__reg_types.end()) return;
    for (const auto& existing : m_ccard->get_deck_registrations())
        if (!replace_existing && map_string(existing, "name") == name && map_string(existing, "type") == type) {
            (void)deck_common_message("エラー", "同名の登録が既に存在します", false);
            return;
        }

    std::vector<std::tuple<::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, std::optional<::cw::lib::String>, std::optional<::cw::lib::String>>> items;
    const auto player = deck_as_player(m_ccard);
    const auto personal_snapshot = player ? player->m_personal_pocket : std::vector<HeaderPtr>{};
    const auto is_personal_header = [&](const HeaderPtr& header) {
        return header && std::find(personal_snapshot.begin(), personal_snapshot.end(), header) != personal_snapshot.end();
    };
    const auto append_headers = [&](const std::vector<HeaderPtr>& headers, const bool force_personal) {
        for (const auto& header : headers) {
            if (!header) continue;
            if (deck_header_is_limited_beast(header)) {
                charainfo_play_sound("error");
                (void)deck_common_message(
                    "エラー",
                    "使用回数制限のある召喚獣カードが含まれているため登録できません",
                    false);
                return false;
            }
            const bool personal = force_personal || is_personal_header(header);
            items.emplace_back(header->m_name, header_description(header), header_type(header), header->m_scenario,
                               header->m_author.empty() ? std::nullopt : std::optional<::cw::lib::String>(header->m_author),
                               personal ? std::optional<::cw::lib::String>("True") : std::optional<::cw::lib::String>("False"));
        }
        return true;
    };
    if (type == "All" || type == "All_No_Personal") {
        if (!append_headers(all_character_headers(m_ccard), false)) return;
        if (type == "All" && player && !append_headers(player->m_personal_pocket, true)) return;
    } else if (type == "Personal") {
        if (player && !append_headers(player->m_personal_pocket, true)) return;
    } else if (!append_headers(character_headers(m_ccard, pocket_for_type(type)), false)) return;

    std::optional<::cw::lib::String> scenario_name;
    std::optional<::cw::lib::String> author_name;
    if (::cw::g_cwpy) {
        if (const auto scenario = object_value<::cw::data::ScenarioData>(::cw::g_cwpy->m_sdata)) {
            auto current_name = value_string(scenario->m_name);
            if (current_name.empty()) current_name = scenario->::cw::data::SystemData::m_name;
            auto current_author = value_string(scenario->m_author);
            if (current_author.empty()) current_author = scenario->::cw::data::SystemData::m_author;
            if (!current_name.empty()) scenario_name = current_name;
            if (!current_author.empty()) author_name = current_author;
        } else if (const auto system = object_value<::cw::data::SystemData>(::cw::g_cwpy->m_sdata)) {
            if (!system->m_name.empty()) scenario_name = system->m_name;
            if (!system->m_author.empty()) author_name = system->m_author;
        }
    }
    if (replace_existing) m_ccard->remove_deck_registration(name, type);
    m_ccard->add_deck_registration(name, std::move(items), type,
                                   std::move(scenario_name), std::move(author_name));
    m_registrations = m_ccard->get_deck_registrations();
    m_title_text = name;
    m_reg_type = type;
    _rebuild_area_buttons();
    charainfo_play_sound("click");
}

::cw::lib::ui::FontStyle DeckDialog::dialog_font(const ::cw::lib::String& role, ::cw::lib::Integer pixels) const
{
    return ::cw::lib::ui::configured_wx_font(role, pixels);
}

::cw::dialog::ButtonRepeatTimer::Clock::time_point DeckDialog::deck_input_time() const noexcept
{
    return ::cw::dialog::ButtonRepeatTimer::Clock::now();
}

::cw::lib::Integer DeckDialog::maximum_scroll() const noexcept
{
    return std::max<::cw::lib::Integer>(0, static_cast<::cw::lib::Integer>(m_visible_registration_values.size()) * 34 - 334);
}

void DeckDialog::invalidate_presentation()
{
    m_ui_invalidated = true;
    request_dialog_presentation_refresh();
}

int DeckDialog::row_control_id(std::size_t row, RowOperation operation)
{
    const auto slot = static_cast<int>(operation);
    if (slot < 0 || slot > 2 || row > static_cast<std::size_t>((std::numeric_limits<int>::max() - 2) / 3))
        throw std::out_of_range("Deck row control is not representable");
    return static_cast<int>(row * 3) + slot;
}

std::optional<DeckDialog::RowTarget> DeckDialog::decode_row_control(int index) noexcept
{
    if (index < 0) return std::nullopt; // スクロールバーは行や補助操作として扱わない。
    return RowTarget{static_cast<std::size_t>(index / 3), static_cast<RowOperation>(index % 3)};
}

std::vector<DeckDialog::FocusTarget> DeckDialog::focus_targets() const
{
    using Action = ::cw::dialog::DialogPresentationAction;
    std::vector<FocusTarget> targets;
    if (m_modal_ended || m_operation_pending) return targets;
    for (std::size_t row = 0; row < m_visible_registration_values.size(); ++row) {
        const auto first = row_control_id(row, RowOperation::Rename);
        targets.emplace_back(Action::Select, static_cast<int>(row));
        for (int slot = 0; slot < 3; ++slot) targets.emplace_back(Action::Auxiliary, first + slot);
    }
    // wx の子生成順に合わせ、スクロール対象の行の後に下部の六つのコントロールを並べる。
    targets.emplace_back(Action::Previous, -1);
    if (can_change_character()) targets.emplace_back(Action::PreviousGroup, -1);
    if (m_ccard) targets.emplace_back(Action::Create, -1);
    if (can_change_character()) targets.emplace_back(Action::NextGroup, -1);
    targets.emplace_back(Action::Cancel, -1);
    targets.emplace_back(Action::Next, -1);
    return targets;
}

bool DeckDialog::target_enabled(FocusTarget target) const
{
    const auto targets = focus_targets();
    return std::find(targets.begin(), targets.end(), target) != targets.end();
}

void DeckDialog::reveal_focus()
{
    if (!m_focus_target) return;
    using Action = ::cw::dialog::DialogPresentationAction;
    const auto [action, index] = *m_focus_target;
    const auto helper = action == Action::Auxiliary ? decode_row_control(index) : std::nullopt;
    const int row = action == Action::Select ? index : helper ? static_cast<int>(helper->first) : -1;
    if (row < 0) return;
    const auto top = static_cast<::cw::lib::Integer>(row) * 34;
    if (top < m_scroll_y) m_scroll_y = top;
    else if (top + 30 > m_scroll_y + 334) m_scroll_y = top + 30 - 334;
    m_scroll_y = std::clamp<::cw::lib::Integer>(m_scroll_y, 0, maximum_scroll());
}

void DeckDialog::focus_next(int direction)
{
    const auto targets = focus_targets();
    if (targets.empty()) return;
    auto found = m_focus_target ? std::find(targets.begin(), targets.end(), *m_focus_target) : targets.end();
    const auto count = static_cast<std::ptrdiff_t>(targets.size());
    const auto index = found == targets.end() ? (direction < 0 ? count - 1 : 0) :
        (std::distance(targets.begin(), found) + direction + count) % count;
    m_space_target.reset(); m_enter_held = false;
    m_focus_target = targets[static_cast<std::size_t>(index)];
    m_keyboard_focus = true;
    reveal_focus();
    invalidate_presentation();
}

::cw::dialog::DialogPresentation DeckDialog::dialog_presentation()
{
    // UIの実装は一つに統一し、
    // 別経路の実装を設けない。
    if (m_modal_ended) { dialog_presentation_pointer_cancel(); return {}; }
    const auto fonts = deck_font_styles(*this);
    const auto can_change = can_change_character();
    if (m_publication_scale != ::cw::g_UP_WIN || !charainfo_same_fonts(fonts, m_publication_fonts)) {
        retire_pointer_input(); // 論理スクロール位置とフォーカスを維持する。
        m_publication_scale = ::cw::g_UP_WIN; m_publication_fonts = fonts;
        dialog_retained_surface().invalidate_all();
        invalidate_presentation();
    }
    if (m_publication_pending != m_operation_pending || m_publication_can_change != can_change)
        invalidate_presentation();
    m_publication_pending = m_operation_pending; m_publication_can_change = can_change;
    if (m_operation_pending) retire_pointer_input();
    if (m_type_repeat && m_type_repeat_timer.consume(deck_input_time(), ::cw::dialog::dialog_page_repeat_interval())) {
        const auto action = m_type_repeat < 0 ? ::cw::dialog::DialogPresentationAction::Previous :
                                               ::cw::dialog::DialogPresentationAction::Next;
        dialog_presentation_action(action, -1);
        if (m_modal_ended) { dialog_presentation_pointer_cancel(); return {}; }
    }
    ::cw::dialog::DialogPresentation result;
    result.route = "DECK";
    result.title = "カード登録/読み込み";
    const auto width=charainfo_scaled(530),height=charainfo_scaled(420);
    auto& retained=dialog_retained_surface();
    m_scroll_y = std::clamp<::cw::lib::Integer>(m_scroll_y, 0, maximum_scroll());
    // 表示領域の復元では、ラベルを変更した行だけでなく全行が消え
    // る。 表示範囲が変わった場合は、その構成要素を再構築する。
    const auto viewport = retained.component_signatures.find("deck.list.viewport");
    std::uint64_t viewport_signature = static_cast<std::uint64_t>(m_scroll_y);
    const auto mix_viewport = [&](std::uint64_t part) {
        viewport_signature ^= part + 0x9e3779b97f4a7c15ULL +
            (viewport_signature << 6U) + (viewport_signature >> 2U);
    };
    mix_viewport(maximum_scroll() > 0);
    mix_viewport(m_visible_registration_values.size());
    if (viewport != retained.component_signatures.end() && viewport->second != viewport_signature)
        retained.invalidate_all();
    const bool full=retained.needs_full({width,height});
    if(full){
        retained.allocate({width,height},{240,240,240,255});
        auto list=charainfo_canvas(charainfo_scaled(514),charainfo_scaled(334),{240,240,240,255});
        charainfo_draw_centered_watermark(list,"PAD",0,0,list->size.width,list->size.height);
        ::cw::lib::ui::blend_surface(retained.surface,list,charainfo_scaled(8),charainfo_scaled(38));
    }
    m_ui_surface=retained.surface;
    struct PaintComponent {bool changed{};std::function<void()> paint;};
    std::vector<PaintComponent> paint_components;
    const auto mix=[](std::uint64_t& value,std::uint64_t part){value^=part+0x9e3779b97f4a7c15ULL+(value<<6U)+(value>>2U);};
    const std::map<::cw::lib::String,::cw::lib::String> titles{{"Skill","スキルリスト"},{"Item","アイテムリスト"},{"Beast","召喚獣リスト"},{"All","全体登録リスト"},{"All_No_Personal","スキル/アイテム/召喚獣登録リスト"},{"Personal","私有物登録リスト"}};
    auto found=titles.find(m_reg_type);auto title=found==titles.end()?titles.at("All"):found->second;
    const auto character_name=m_ccard ? m_ccard->get_showingname() : ::cw::lib::String{};if(!character_name.empty())title+="("+character_name+")";
    const ::cw::lib::Rect title_rect{charainfo_scaled(8),charainfo_scaled(8),charainfo_scaled(514),charainfo_scaled(24)};
    std::uint64_t title_signature=std::hash<::cw::lib::String>{}(title);for(const auto& font:fonts){mix(title_signature,std::hash<::cw::lib::String>{}(font.face));mix(title_signature,font.pixels);mix(title_signature,font.bold);mix(title_signature,font.italic);}
    paint_components.push_back({retained.component_changed("deck.title",title_rect,title_signature),[&,title,title_rect]{const auto font=dialog_font("charaparam2",16);auto layer=charainfo_canvas(title_rect.width,title_rect.height,{240,240,240,255});::cw::lib::ui::draw_label(layer,title,0,0,font.pixels,font.bold,{0,0,0},font.face,font.italic);::cw::lib::ui::blend_surface(retained.surface,layer,title_rect.x,title_rect.y);}});
    using Action = ::cw::dialog::DialogPresentationAction;
    const auto add = [&](const ::cw::lib::String& label, const ::cw::lib::Rect logical,
                         Action action, int index = -1, bool enabled = true,
                         const ::cw::lib::String& font_role = "button", int pixels = 14,
                         const char* icon = nullptr) {
        const ::cw::lib::Rect rect{charainfo_scaled(logical.x), charainfo_scaled(logical.y),
                                  charainfo_scaled(logical.width), charainfo_scaled(logical.height)};
        enabled = enabled && !m_modal_ended && !m_operation_pending;
        const FocusTarget target{action, index};
        const bool depressed = enabled && ((m_pointer_target == target && m_pointer_inside && !m_pointer_stale) || m_space_target == target);
        auto visible = rect;
        const bool list_button = action == Action::Select ||
            (action == Action::Auxiliary && decode_row_control(index));
        if (list_button) {
            visible.x = std::max<::cw::lib::Integer>(rect.x, charainfo_scaled(8));
            visible.y = std::max<::cw::lib::Integer>(rect.y, charainfo_scaled(38));
            visible.width = std::min<::cw::lib::Integer>(rect.x + rect.width, charainfo_scaled(522)) - visible.x;
            visible.height = std::min<::cw::lib::Integer>(rect.y + rect.height, charainfo_scaled(372)) - visible.y;
            if (visible.width <= 0 || visible.height <= 0) return;
        }
        const auto font=dialog_font(font_role,pixels);const bool focused=enabled&&m_keyboard_focus&&m_focus_target==target;
        std::uint64_t signature=std::hash<::cw::lib::String>{}(label);mix(signature,static_cast<std::uint64_t>(action));mix(signature,index);mix(signature,enabled);mix(signature,depressed);mix(signature,focused);mix(signature,std::hash<::cw::lib::String>{}(font.face));mix(signature,font.pixels);mix(signature,font.bold);mix(signature,font.italic);const ::cw::lib::String icon_key=icon?icon:"";mix(signature,std::hash<::cw::lib::String>{}(icon_key));
        const auto key="deck.control."+std::to_string(static_cast<int>(action))+"."+std::to_string(index)+"."+std::to_string(rect.x)+"."+std::to_string(rect.y);
        paint_components.push_back({retained.component_changed(key,visible,signature),[&,label,rect,visible,action,enabled,depressed,focused,font,icon_key]{
            auto button=charainfo_canvas(rect.width,rect.height,{0,0,0,0});
            ::cw::lib::ui::draw_button_face(button,{0,0,rect.width,rect.height},{enabled,depressed});
            if(!icon_key.empty()){auto image=::cw::g_cwpy&&::cw::g_cwpy->m_rsrc?::cw::lib::ui::resource_table_surface(::cw::g_cwpy->m_rsrc->m_buttons,icon_key):nullptr;if(image){image=::cw::lib::ui::resource_surface_for_window(image);if(!enabled){const auto source=image;image=::cw::imageretouch::to_disabledsurface(image);::cw::lib::ui::inherit_surface_animation(image,source);}::cw::lib::ui::blend_surface(button,image,(rect.width-image->size.width)/2,(rect.height-image->size.height)/2);}else{const auto fallback=action==Action::Previous?"◀":"▶";::cw::lib::ui::draw_label(button,fallback,charainfo_scaled(6),charainfo_scaled(6),font.pixels,font.bold,enabled?std::tuple{0,0,0}:std::tuple{128,128,128},font.face,font.italic);}}
            else{const auto text_width=::cw::lib::ui::label_width(label,font.pixels,font.bold,font.face,font.italic);::cw::lib::ui::draw_label(button,label,std::max<::cw::lib::Integer>(charainfo_scaled(2),(rect.width-text_width)/2),(rect.height-font.pixels)/2,font.pixels,font.bold,enabled?std::tuple{0,0,0}:std::tuple{128,128,128},font.face,font.italic);}
            if(focused){const auto inset=charainfo_scaled(3);for(::cw::lib::Integer x=inset;x<rect.width-inset;x+=2)for(const auto y:std::array<::cw::lib::Integer,2>{inset,rect.height-inset-1})::cw::lib::ui::fill_rect(button,{x,y,1,1},{0,0,0,255});for(::cw::lib::Integer y=inset;y<rect.height-inset;y+=2)for(const auto x:std::array<::cw::lib::Integer,2>{inset,rect.width-inset-1})::cw::lib::ui::fill_rect(button,{x,y,1,1},{0,0,0,255});}
            if(visible.x!=rect.x||visible.y!=rect.y||visible.width!=rect.width||visible.height!=rect.height){auto clipped=charainfo_canvas(visible.width,visible.height,{0,0,0,0});::cw::lib::ui::blend_surface(clipped,button,rect.x-visible.x,rect.y-visible.y);button=std::move(clipped);}
            ::cw::lib::ui::blend_surface(retained.surface,button,visible.x,visible.y);
        }});
        result.controls.push_back({action, index, static_cast<float>(visible.x), static_cast<float>(visible.y),
            static_cast<float>(visible.width), static_cast<float>(visible.height), enabled});
        result.controls.back().hover_highlight=true;
        result.controls.back().continuous_pointer = true;
        result.controls.back().label = label;
    };
    m_scroll_y = std::clamp<::cw::lib::Integer>(m_scroll_y, 0, maximum_scroll());
    const bool scrollable = maximum_scroll() > 0;
    const ::cw::lib::Rect list_rect{charainfo_scaled(8),charainfo_scaled(38),charainfo_scaled(514),charainfo_scaled(334)};
    const auto list_signature = viewport_signature;
    paint_components.push_back({retained.component_changed("deck.list.viewport",list_rect,list_signature),[&,list_rect]{auto list=charainfo_canvas(list_rect.width,list_rect.height,{240,240,240,255});charainfo_draw_centered_watermark(list,"PAD",0,0,list_rect.width,list_rect.height);::cw::lib::ui::blend_surface(retained.surface,list,list_rect.x,list_rect.y);}});
    const int main_width = scrollable ? 396 : 410;
    const auto first = static_cast<std::size_t>(m_scroll_y / 34);
    const auto last = std::min(m_visible_registration_values.size(),
        static_cast<std::size_t>((m_scroll_y + 334 + 33) / 34));
    for (std::size_t row = first; row < last; ++row) {
        const auto y = 38 + static_cast<::cw::lib::Integer>(row) * 34 - m_scroll_y;
        auto name = map_string(m_visible_registration_values[row], "name");
        if (name.empty()) name = "タッチ可能エリア " + std::to_string(row + 1);
        add(name, {8, y, main_width, 30}, Action::Select, static_cast<int>(row), true, "charadesc", 20);
        add("名", {8 + main_width + 5, y + 1, 30, 28}, Action::Auxiliary, row_control_id(row, RowOperation::Rename));
        add("消", {8 + main_width + 40, y + 1, 30, 28}, Action::Auxiliary, row_control_id(row, RowOperation::Delete));
        add("更", {8 + main_width + 75, y + 1, 30, 28}, Action::Auxiliary, row_control_id(row, RowOperation::Update));
    }
    if (scrollable) {
        const ::cw::lib::Rect track{charainfo_scaled(510), charainfo_scaled(38), charainfo_scaled(12), charainfo_scaled(334)};
        const auto thumb_h = std::max<::cw::lib::Integer>(charainfo_scaled(16),
            track.height * track.height / (track.height + charainfo_scaled(maximum_scroll())));
        const auto thumb_y = track.y + (track.height - thumb_h) * m_scroll_y / maximum_scroll();
        std::uint64_t signature=static_cast<std::uint64_t>(m_scroll_y);mix(signature,maximum_scroll());
        paint_components.push_back({retained.component_changed("deck.scrollbar",track,signature),[&,track,thumb_h,thumb_y]{::cw::lib::ui::fill_rect(retained.surface,track,{224,224,224,255});::cw::lib::ui::fill_rect(retained.surface,{track.x+1,thumb_y,track.width-2,thumb_h},{140,140,140,255});}});
        result.controls.push_back({Action::Auxiliary, scrollbar_control_id, static_cast<float>(track.x), static_cast<float>(track.y),
            static_cast<float>(track.width), static_cast<float>(track.height), !m_operation_pending && !m_modal_ended});
        result.controls.back().continuous_pointer = true;
    } else (void)retained.remove_component("deck.scrollbar");
    add("左PCへ", {8, 381, 80, 28}, Action::PreviousGroup, -1, can_change_character());
    add("登録種類を左へ", {103, 380, 30, 30}, Action::Previous, -1, true, "button", 14, "LMOVE");
    add("登録", {158, 381, 100, 28}, Action::Create, -1, m_ccard != nullptr);
    add("キャンセル", {272, 381, 100, 28}, Action::Cancel);
    add("登録種類を右へ", {397, 380, 30, 30}, Action::Next, -1, true, "button", 14, "RMOVE");
    add("右PCへ", {442, 381, 80, 28}, Action::NextGroup, -1, can_change_character());
    if(full)retained.capture_base();
    retained.restore_dirty();
    for(auto& component:paint_components)if(component.changed)component.paint();
    retained.finish();m_ui_surface=retained.surface;result.surface=m_ui_surface;
    m_ui_invalidated = false;
    m_cached_presentation = result;
    return result;
}

void DeckDialog::dialog_presentation_action(
    const ::cw::dialog::DialogPresentationAction action, const int index)
{
    using Action = ::cw::dialog::DialogPresentationAction;
    if (m_modal_ended || m_operation_pending) return;
    struct OperationScope {
        DeckDialog& dialog;
        explicit OperationScope(DeckDialog& value) : dialog(value) { dialog.m_operation_pending = true; dialog.invalidate_presentation(); }
        ~OperationScope() { dialog.m_operation_pending = false; dialog.invalidate_presentation(); }
    } operation(*this);
    if (action == Action::Cancel) {
        dialog_presentation_pointer_cancel();
        m_modal_ended = true;
    } else if (action == Action::Previous) {
        OnRegTypeLeft(nullptr);
    } else if (action == Action::Next) {
        OnRegTypeRight(nullptr);
    } else if (action == Action::PreviousGroup) {
        OnLoadLeftPC(nullptr);
    } else if (action == Action::NextGroup) {
        OnLoadRightPC(nullptr);
    } else if (action == Action::Create) {
        OnRegister(nullptr, std::nullopt, std::nullopt);
    } else if (action == Action::Select && index >= 0) {
        auto event = std::make_shared<::cw::lib::Window>();
        event->id = index;
        OnArea(std::move(event));
    } else if (action == Action::Auxiliary) {
        if (const auto target = decode_row_control(index); target && target->first < m_visible_registration_values.size()) {
            const auto row = static_cast<::cw::lib::Integer>(target->first);
            switch (target->second) {
            case RowOperation::Rename: _on_change_reg_name(nullptr, row); break;
            case RowOperation::Delete: _on_delete_registration(nullptr, row); break;
            case RowOperation::Update: _on_update_registration(nullptr, row); break;
            }
        }
    }
    m_ui_invalidated = true;
}

void DeckDialog::retire_pointer_input()
{
    // ホストは古いキャプチャー先へ移動・解除イベントを送る場合がある。 解除ま
    // ではその対象を識別できるようにするが、再構築した行を再び有効化しない。
    const bool changed = m_pointer_inside || m_type_repeat || m_space_target;
    if (m_pointer_target) m_pointer_stale = true;
    m_pointer_inside = false; m_type_repeat = 0;
    m_space_target.reset(); m_enter_held = false;
    if (changed) invalidate_presentation();
}

void DeckDialog::dialog_presentation_pointer_cancel()
{
    const bool changed = m_pointer_target || m_space_target || m_type_repeat;
    m_pointer_target.reset(); m_space_target.reset();
    m_pointer_inside = false; m_pointer_stale = false;
    m_type_repeat = 0; m_enter_held = false;
    if (changed) invalidate_presentation();
}

void DeckDialog::dialog_presentation_scroll(const float x, const float y, const int lines)
{
    if (m_modal_ended || m_operation_pending || lines == 0 || !std::isfinite(x) || !std::isfinite(y) ||
        x < charainfo_scaled(8) || x >= charainfo_scaled(522) ||
        y < charainfo_scaled(38) || y >= charainfo_scaled(372)) return;
    const auto next = std::clamp<std::int64_t>(
        static_cast<std::int64_t>(m_scroll_y) + static_cast<std::int64_t>(lines) * 30, 0, maximum_scroll());
    if (next == m_scroll_y) return;
    retire_pointer_input();
    m_scroll_y = static_cast<::cw::lib::Integer>(next);
    invalidate_presentation();
}

void DeckDialog::dialog_presentation_pointer(::cw::dialog::DialogPresentationAction action,
                                             int index, float x, float y, bool pressed)
{
    using Action = ::cw::dialog::DialogPresentationAction;
    if (m_modal_ended || m_operation_pending || !std::isfinite(x) || !std::isfinite(y)) {
        if (pressed) retire_pointer_input();
        else dialog_presentation_pointer_cancel();
        return;
    }
    const FocusTarget target{action, index};
    // 入力処理前の公開は新しい押下時だけに行う。 解除時に公開すると、期限
    // を迎えたリピートタイマーが処理され、最後に余分な種別変更が起きる。
    if (pressed && !m_pointer_target) (void)dialog_presentation();
    if (m_publication_scale != ::cw::g_UP_WIN) retire_pointer_input();
    if (m_pointer_stale) {
        if (!pressed) dialog_presentation_pointer_cancel();
        return;
    }
    const auto found = std::find_if(m_cached_presentation.controls.begin(), m_cached_presentation.controls.end(),
        [&](const auto& control) { return control.action == action && control.index == index && control.enabled; });
    const bool inside = found != m_cached_presentation.controls.end() &&
        x >= found->x && x < found->x + found->width && y >= found->y && y < found->y + found->height;
    const bool scrollbar = action == Action::Auxiliary && index == scrollbar_control_id;
    const bool chain = (action == Action::Previous || action == Action::Next) && index == -1;
    if (!pressed) {
        const bool activate = m_pointer_target == target && inside && !chain && !scrollbar && target_enabled(target);
        dialog_presentation_pointer_cancel();
        if (activate) dialog_presentation_action(action, index);
        return;
    }
    if (m_pointer_target && m_pointer_target != target) return;
    const bool first = !m_pointer_target;
    if (first) {
        if (!inside || (!scrollbar && !target_enabled(target))) return;
        m_pointer_target = target; m_pointer_inside = true;
        m_space_target.reset(); m_enter_held = false; m_keyboard_focus = false;
        if (!scrollbar) m_focus_target = target;
        invalidate_presentation();
    } else if (m_pointer_inside != inside) {
        m_pointer_inside = inside;
        if (!inside) m_type_repeat = 0; // Python と同様、LEAVE で停止し、ENTER では再開しない。
        invalidate_presentation();
    }
    if (scrollbar) {
        const auto maximum = maximum_scroll();
        if (maximum <= 0) { retire_pointer_input(); return; }
        const auto height = charainfo_scaled(334);
        const auto thumb = std::max<::cw::lib::Integer>(charainfo_scaled(16),
            height * height / (height + charainfo_scaled(maximum)));
        const auto fraction = std::clamp((y - charainfo_scaled(38) - thumb / 2.0F) /
            static_cast<float>(std::max<::cw::lib::Integer>(1, height - thumb)), 0.0F, 1.0F);
        const auto next = static_cast<::cw::lib::Integer>(std::lround(fraction * maximum));
        if (next != m_scroll_y) { m_scroll_y = next; invalidate_presentation(); }
    } else if (chain && first) {
        dialog_presentation_action(action, index);
        if (!m_modal_ended && !m_pointer_stale && m_pointer_target == target) {
            m_type_repeat = action == Action::Previous ? -1 : 1;
            m_type_repeat_timer.start(deck_input_time(), ::cw::dialog::dialog_page_repeat_interval());
        }
    }
}

void DeckDialog::dialog_presentation_key(::cw::dialog::DialogPresentationKey key, bool pressed,
                                         ::cw::dialog::DialogPresentationModifiers modifiers)
{
    using Key = ::cw::dialog::DialogPresentationKey;
    using Action = ::cw::dialog::DialogPresentationAction;
    if (m_modal_ended || m_operation_pending) { retire_pointer_input(); return; }
    if (key == Key::Enter && !pressed) { m_enter_held = false; return; }
    if (key == Key::Space && !pressed) {
        const auto target = std::exchange(m_space_target, std::nullopt);
        if (target) invalidate_presentation();
        if (target && m_focus_target == target && target_enabled(*target))
            dialog_presentation_action(target->first, target->second);
        return;
    }
    if (!pressed || (modifiers & (::cw::dialog::DialogPresentationModifierControl |
                                  ::cw::dialog::DialogPresentationModifierAlt))) return;
    if (key == Key::Escape) { dialog_presentation_action(Action::Cancel, -1); return; }
    if (key == Key::Tab) {
        retire_pointer_input();
        focus_next(modifiers & ::cw::dialog::DialogPresentationModifierShift ? -1 : 1);
        return;
    }
    if (key != Key::Space && key != Key::Enter) return;
    if (m_pointer_target && !m_pointer_stale) { retire_pointer_input(); invalidate_presentation(); }
    if (!m_focus_target || !target_enabled(*m_focus_target)) focus_next(1);
    if (!m_focus_target) return;
    m_keyboard_focus = true;
    reveal_focus();
    if (key == Key::Space) {
        if (!m_space_target) { m_space_target = m_focus_target; invalidate_presentation(); }
    } else if (!m_enter_held) {
        m_enter_held = true;
        const auto target = *m_focus_target;
        dialog_presentation_action(target.first, target.second);
    }
}

void DeckDialog::dialog_presentation_enter(::cw::dialog::DialogPresentationModifiers modifiers)
{
    dialog_presentation_key(::cw::dialog::DialogPresentationKey::Enter, true, modifiers);
}

bool DeckDialog::dialog_presentation_needs_refresh() const noexcept
{
    try {
        return m_ui_invalidated || DialogPresentationProvider::dialog_presentation_needs_refresh() ||
            m_publication_scale != ::cw::g_UP_WIN ||
            !charainfo_same_fonts(deck_font_styles(*this), m_publication_fonts) ||
            m_publication_pending != m_operation_pending || m_publication_can_change != can_change_character() ||
            (m_type_repeat && m_type_repeat_timer.due(deck_input_time()));
    } catch (...) { return true; }
}

bool DeckDialog::dialog_presentation_finished() const noexcept
{
    return m_modal_ended;
}

void DeckDialog::dialog_presentation_complete() {}

void DescPanel::initialize(std::shared_ptr<::cw::lib::Window> arg_parent, ::cw::lib::Value arg_ccard, bool arg_editable) { (void)arg_parent; m_ccard = std::move(arg_ccard); m__editable = arg_editable; m_text.clear(); _init_view(); }
// ネイティブパネルは wx.SetCursor の代わりに、所有する CharaInfo
// の共通 Presentation を通して公開する。 page_editor が編集操作
// と実際のホストカーソルの双方へ、Active・デバッグ・編集可能の同じ適用条件を渡す。
void DescPanel::update_cursor() { if (m_on_cursor_changed) m_on_cursor_changed(); }
void DescPanel::OnLeftUp(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    if (m__editable && m_on_edit) m_on_edit();
}
void DescPanel::_init_view()
{
    const auto description = find_element(m_ccard, "Property/Description");
    m_text = description ? description->get_text() : ::cw::lib::String{};
    if (m_text.empty())
        if (const auto character = character_value(m_ccard)) m_text = character->get_description();
    m_text = ::cw::util::txtwrap(m_text, 4, 0, {}, true, nullptr);
    m__ratey = ::cw::lib::Integer(charainfo_scaled(13));
}
void DescPanel::draw(bool arg_update) { if (arg_update) _init_view(); }
void DescPanel::OnPaint(std::shared_ptr<::cw::lib::Window> arg_event) { (void)arg_event; draw(false); }
::cw::lib::String DescPanel::get_detailtext() { return m_text; }

void EditButton::initialize(::cw::lib::String arg_name, ::cw::lib::Integer arg_btype, bool arg_has_separator) { m_name = std::move(arg_name); m_type = arg_btype; m_has_separator = arg_has_separator; m_negaflag = false; m_textpos = {0, 0}; m_subrect = ::cw::lib::Value{}; }
void EditPanel::initialize(::cw::lib::Value arg_parent, std::vector<::cw::lib::Value> arg_mlist, std::shared_ptr<::cw::character::Player> arg_ccard) { (void)arg_parent; m_ccard = arg_ccard ? ::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(arg_ccard)) : ::cw::lib::Value{}; m_list = ::cw::lib::Value(static_cast<::cw::lib::Integer>(arg_mlist.size())); m_charalist.clear(); for (const auto& value : arg_mlist) if (const auto player = object_value<::cw::character::Player>(value)) m_charalist.push_back(player); m_selected = -1; m__destroy = false; draw(true); }
void EditPanel::OnDestroy(std::shared_ptr<::cw::lib::Window> arg_event) { (void)arg_event; m__destroy = true; }
void EditPanel::OnLeftUp(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    if (m__destroy || m_edit_child_open) return;
    auto* owner = m_charainfo_owner;
    const auto lifetime = m_owner_lifetime;
    if (owner && (lifetime.expired() || owner->m_modal_ended || owner->m_child_open)) return;
    for (const auto& header : m_headers) {
        if (!header || !header->m_negaflag) continue;
        const auto current = object_value<::cw::character::Player>(m_ccard);
        if (!current) return;
        const auto type = header->m_type;
        if (type < 0 || type > 2) return;
        struct ChildScope {
            EditPanel& panel;
            CharaInfo* owner;
            std::weak_ptr<int> lifetime;
            ChildScope(EditPanel& p, CharaInfo* o, std::weak_ptr<int> token)
                : panel(p), owner(o), lifetime(std::move(token)) {
                panel.m_edit_child_open = true;
                if (owner) {owner->dialog_presentation_pointer_cancel();owner->m_child_open = true;}
            }
            ~ChildScope() {
                panel.m_edit_child_open = false;
                if (owner && !lifetime.expired()) {
                    owner->m_child_open = false;
                    if (!owner->m_modal_ended) owner->draw(false);
                }
            }
        } child(*this, owner, lifetime);
        charainfo_play_sound("click");
        bool accepted = false;
        bool completion_deferred = false;
        if (type == 0) {
            auto dialog = std::make_shared<::cw::dialog::create::AdventurerDesignDialog>();
            dialog->initialize(nullptr, current);
            accepted = ::cw::dialog::show_common_modal_presentation(dialog) &&
                       dialog->m_presentation_accepted;
        } else if (type == 1) {
            const auto players = get_charalist();
            const auto found = std::find(players.begin(), players.end(), current);
            if (found == players.end()) return;
            const auto selected = static_cast<::cw::lib::Integer>(found - players.begin());
            std::optional<std::shared_ptr<::cw::data::Party>> party;
            // 確認対象のパーティーは、宿の現在のパーティーではなく待機中のパーティーの場合もある。
            if (owner) {
                if (const auto inspected = object_value<::cw::data::Party>(owner->m_party))
                    party = inspected;
            }
            auto dialog = std::make_shared<::cw::dialog::edit::LevelEditDialog>();
            dialog->m_on_complete = [this, owner, lifetime, players](bool success) {
                if (!success || lifetime.expired()) return;
                update_charalist(players);
                if (owner && !owner->m_modal_ended) owner->refresh_after_editor();
            };
            dialog->initialize(nullptr, players, selected, party);
            ::cw::dialog::edit::run_edit_ui_modal(0);
            accepted = dialog->m_modal_result_ok;
            // キューへの受付は編集成功を意味しない。ヘッダーの
            // 保存と再描画は、 ゲームスレッドの処理が例外なく
            // 完了した後、m_on_complete で行う。
            completion_deferred = accepted;
        } else {
            auto dialog = std::make_shared<::cw::dialog::edit::BackColorEditDialog>();
            dialog->m_on_complete = [owner, lifetime](bool success) {
                if (success && owner && !lifetime.expired() && !owner->m_modal_ended)
                    owner->refresh_after_editor();
            };
            dialog->initialize(nullptr, current);
            ::cw::dialog::edit::run_edit_ui_modal(0);
            accepted = dialog->m_modal_result_ok;
            completion_deferred = accepted;
        }
        if (accepted && !completion_deferred && owner && !lifetime.expired() && !owner->m_modal_ended)
            owner->refresh_after_editor();
        return;
    }
}
std::vector<std::shared_ptr<::cw::character::Player>> EditPanel::get_charalist()
{
    // 非PartyのStandbyCharaInfoではPython版と同じく編集中の一人だけを
    // LevelEditDialogへ渡す。Party表示だけは全メンバを渡して相互比較できる。
    if (!m_party_mode && !m_parentheaders.empty()) {
        const auto current = object_value<::cw::character::Player>(m_ccard);
        if (current) return {current};
        const auto index = static_cast<std::size_t>(std::max<::cw::lib::Integer>(0, m_parent_index));
        if (index < m_charalist.size() && m_charalist[index]) return {m_charalist[index]};
        return {};
    }
    return m_charalist;
}
void EditPanel::update_charalist(std::vector<std::shared_ptr<::cw::character::Player>> arg_mlist)
{
    m_charalist = arg_mlist;
    // 保存を予約する前に、確認中のヘッダーとメンバーのスナップショットを取得する。
    // 後のキャラクター切り替えや親の破棄によって、確定した編集の対象を変えない。
    const auto headers = m_parentheaders;
    const auto index = m_parent_index;
    const bool party_mode = m_party_mode;
    const auto sync_headers = [headers, players = std::move(arg_mlist), index, party_mode]
        (const ::cw::lib::Arguments&, const ::cw::lib::KeywordArguments&) {
        const auto save = [&](std::size_t h, std::size_t p) {
            if (h >= headers.size() || p >= players.size() || !players[p]) return;
            write_character_value(::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(players[p])));
            if (headers[h]) headers[h]->m_level = players[p]->m_level;
        };
        if (!party_mode) {
            if (index >= 0) save(static_cast<std::size_t>(index), 0);
        } else {
            for (std::size_t i = 0; i < std::min(headers.size(), players.size()); ++i) save(i, i);
        }
        return ::cw::lib::Value{};
    };
    // Python と同様、待機中キャラクターのファイル・ヘッダー更新はゲームキューで行う。
    if (const auto runtime = ::cw::g_cwpy) runtime->exec_func(sync_headers);
    else sync_headers({}, {});
}
void EditPanel::OnPaint(std::shared_ptr<::cw::lib::Window> arg_event) { (void)arg_event; draw(false); }
void EditPanel::OnLeave(std::shared_ptr<::cw::lib::Window> arg_event) { (void)arg_event; for (const auto& header : m_headers) if (header) header->m_negaflag = false; }
void EditPanel::update_negaflag() { for (const auto& header : m_headers) if (header) { const auto x = std::get<0>(header->m_textpos); const auto y = std::get<1>(header->m_textpos); header->m_negaflag = m_mousepos.x >= x - 20 && m_mousepos.x < x + 180 && m_mousepos.y >= y - 1 && m_mousepos.y < y + 18; } }
void EditPanel::OnMove(std::shared_ptr<::cw::lib::Window> arg_event) { _update_mousepos(arg_event); }
void EditPanel::_update_mousepos(std::shared_ptr<::cw::lib::Window> arg_mousepos) { if (arg_mousepos) m_mousepos = {arg_mousepos->bounds.x, arg_mousepos->bounds.y}; update_negaflag(); }
void EditPanel::draw_header(std::shared_ptr<::cw::lib::DeviceContext> arg_dc, std::shared_ptr<EditButton> arg_header) { (void)arg_dc; if (arg_header) { m_selected = arg_header->m_type; arg_header->m_negaflag = true; } }
void EditPanel::up() { if (m_headers.empty()) draw(true); if (m_headers.empty()) return; std::size_t current = 0; for (std::size_t i = 0; i < m_headers.size(); ++i) if (m_headers[i]->m_negaflag) { current = i; break; } current = (current + m_headers.size() - 1) % m_headers.size(); for (std::size_t i = 0; i < m_headers.size(); ++i) m_headers[i]->m_negaflag = i == current; }
void EditPanel::down() { if (m_headers.empty()) draw(true); if (m_headers.empty()) return; std::size_t current = 0; bool found=false; for (std::size_t i = 0; i < m_headers.size(); ++i) if (m_headers[i]->m_negaflag) { current = i; found=true; break; } if(found) current = (current + 1) % m_headers.size(); for (std::size_t i = 0; i < m_headers.size(); ++i) m_headers[i]->m_negaflag = i == current; }
void EditPanel::draw(bool arg_update)
{
    if (m_headers.empty() || arg_update) {
        m_headers.clear();
        auto level = std::make_shared<EditButton>();
        level->initialize(charainfo_msg("regulate_level", "レベルを調節する"), 1, false);
        auto design = std::make_shared<EditButton>();
        design->initialize(charainfo_msg("edit_design", "デザインを変更する"), 0, true);
        auto color = std::make_shared<EditButton>();
        color->initialize(charainfo_msg("edit_bgcolor", "背景色を変更する"), 2, false);
        m_headers = {level, design, color};
        charainfo_layout_edit_headers(*this);
    }
}
::cw::lib::String EditPanel::get_detailtext()
{
    // Py と同様、編集ページからはキャラクター詳細のクリップボードへテキストを追加しない。
    return {};
}

void HistoryPanel::initialize(std::shared_ptr<::cw::lib::Window> arg_parent, ::cw::lib::Value arg_ccard, bool arg_editable) { (void)arg_parent; m_ccard = std::move(arg_ccard); m__editable = arg_editable; _init_view(); }
std::shared_ptr<::cw::lib::Bitmap> HistoryPanel::_get_bmps(::cw::lib::String arg_name)
{
    if (auto surface = charainfo_dialog_surface(arg_name)) {
        auto bitmap = std::make_shared<::cw::lib::Bitmap>(static_cast<const ::cw::lib::Bitmap&>(*surface));
        // Py はアルファ値に 128/255 を掛ける。共通のピクセル処理でも同じ丸め
        // を維持し、 すべてのホストで不透明なピクセルの値が 128 になるようにする。
        ::cw::lib::simd_image::multiply_alpha(bitmap->rgba.data(), bitmap->rgba.size() / 4, 128);
        return bitmap;
    }
    auto bitmap = std::make_shared<::cw::lib::Bitmap>();
    bitmap->size = {16, 16};
    bitmap->rgba.resize(16 * 16 * 4, 0);
    const auto shade = static_cast<std::uint8_t>(arg_name.empty() ? 128 : 160);
    for (std::size_t i = 0; i < 16 * 16; ++i) {
        bitmap->rgba[i * 4] = shade;
        bitmap->rgba[i * 4 + 1] = shade;
        bitmap->rgba[i * 4 + 2] = shade;
        bitmap->rgba[i * 4 + 3] = 180;
    }
    return bitmap;
}
void HistoryPanel::update_cursor() { if (m_on_cursor_changed) m_on_cursor_changed(); }
void HistoryPanel::OnLeftUp(std::shared_ptr<::cw::lib::Window> arg_event) { (void)arg_event; if (m__editable && m_on_edit) m_on_edit(); }
void HistoryPanel::draw(bool arg_update) { if (arg_update) _init_view(); }
void HistoryPanel::_init_view() { m_coupons.clear(); const auto root = value_root(m_ccard); const auto coupons = root ? root->find("Property/Coupons") : std::optional<ElementPtr>{}; if (!coupons || !*coupons) return; const bool album = root->m_tag == "Album"; for (const auto& coupon : (*coupons)->m_children) { if (!coupon) continue; const auto text = coupon->get_text(); if (text.empty() || text.rfind("＠", 0) == 0) continue; const bool timed = text.rfind("：", 0) == 0 || text.rfind("；", 0) == 0; if (album && timed) continue; if (debug_mode() || !(text.rfind("＿", 0) == 0 || timed)) m_coupons.emplace_back(text, element_value(coupon)); } std::reverse(m_coupons.begin(), m_coupons.end()); m__ratey = ::cw::lib::Integer(17); }
bool HistoryPanel::is_hidden(::cw::lib::String arg_coupon) { return arg_coupon.rfind("＿", 0) == 0 || is_timed(arg_coupon); }
bool HistoryPanel::is_timed(::cw::lib::String arg_coupon) { return arg_coupon.rfind("：", 0) == 0 || arg_coupon.rfind("；", 0) == 0; }
void HistoryPanel::OnPaint(std::shared_ptr<::cw::lib::Window> arg_event) { (void)arg_event; draw(false); }
::cw::lib::String HistoryPanel::_append_text(::cw::lib::String arg_text, ::cw::lib::Integer arg_value) { return coupon_suffix(debug_mode(), arg_text, arg_value); }
::cw::lib::String HistoryPanel::get_detailtext() { ::cw::lib::String result; for (std::size_t i = 0; i < m_coupons.size(); ++i) { if (i) result += '\n'; const auto& [text, value] = m_coupons[i]; const auto suffix = _append_text(text, value); result += suffix.empty() ? coupon_prefix(debug_mode(), value) : ::cw::lib::String{}; result += text + suffix; } return result; }

void HoldAll::initialize() { m_negaflag = false; m_subrect = ::cw::lib::Value{}; }
void StandbyCharaInfo::initialize(std::shared_ptr<::cw::lib::Window> arg_parent, std::vector<std::shared_ptr<::cw::header::AdventurerHeader>> arg_headers, ::cw::lib::Value arg_ccard, ::cw::lib::Integer arg_index, ::cw::lib::Callback arg_redrawfunc, bool arg_is_playingscenario, std::optional<std::shared_ptr<::cw::data::Party>> arg_party) { m_character_change_token.reset(); m_selection_changed={}; m_parent = std::move(arg_parent); m_headers = std::move(arg_headers); m_list = ::cw::lib::Value(static_cast<::cw::lib::Integer>(m_headers.size())); m_ccard = std::move(arg_ccard); m_index = arg_index; m_redrawfunc = std::move(arg_redrawfunc); m_is_playingscenario = arg_is_playingscenario; const auto player = object_value<::cw::character::Player>(m_ccard); CharaInfo::initialize(m_parent, m_redrawfunc, static_cast<bool>(player), arg_party); }

void StandbyCharaInfo::save_current_character() { write_character_value(m_ccard); }
::cw::lib::Value StandbyCharaInfo::read_character(const std::shared_ptr<::cw::header::AdventurerHeader>& header)
{
    return load_adventurer_value(header);
}

bool StandbyCharaInfo::change_character(::cw::lib::Integer delta)
{
    if (m_modal_ended || m_child_open || m_character_change_token || m_headers.empty() || !delta)
        return false;
    const auto old_index = m_index;
    auto next_index = old_index;
    cycle_index(next_index, m_headers.size(), delta);
    const auto header = m_headers[static_cast<std::size_t>(next_index)];
    const auto lifetime = m_callback_lifetime;
    const auto token = std::make_shared<int>(0);
    m_character_change_token = token;
    const auto release = [&] { if(m_character_change_token == token) m_character_change_token.reset(); };
    try {
        // Python と同様、切り替え前に保存する。読み込みに成功してから位置とカードを確定する。
        save_current_character();
        const auto loaded = read_character(header);
        if (m_modal_ended || m_callback_lifetime != lifetime || m_index != old_index ||
            next_index >= static_cast<::cw::lib::Integer>(m_headers.size()) ||
            m_headers[static_cast<std::size_t>(next_index)] != header) {
            release(); return false;
        }
        if (loaded.is_none()) throw std::runtime_error("人物情報を読み込めません: " +
            (header ? value_string(header->m_fpath) : ::cw::lib::String{}));
        m_index = next_index;
        m_ccard = loaded;
        refresh_character_panels(*this);
        // Python と同様、StandbyCharaInfo では親の選択画面もページ移動するが、パーティーメンバー情報では行わない。
        if (!dynamic_cast<StandbyPartyCharaInfo*>(this) && m_selection_changed)
            m_selection_changed(header);
        if (::cw::g_cwpy) ::cw::g_cwpy->play_sound("page",false, 100, 1, 0,0,false);
        release(); return true;
    } catch (const std::exception& error) {
        try {
            if (!m_modal_ended && m_callback_lifetime == lifetime)
                (void)::cw::dialog::message::show_common_modal_message("エラー", error.what(), false, true);
        } catch (...) { release(); throw; }
        release(); return false;
    } catch (...) { release(); throw; }
}
void StandbyPartyCharaInfo::initialize(std::shared_ptr<::cw::lib::Window> arg_parent, std::shared_ptr<::cw::header::PartyHeader> arg_partyheader, ::cw::lib::Callback arg_redrawfunc)
{
    m_parent = std::move(arg_parent);
    m_redrawfunc = std::move(arg_redrawfunc);
    m_headers.clear();
    m_index = 0;
    m_is_playingscenario = false;
    std::shared_ptr<::cw::data::Party> party;
    if (arg_partyheader) {
        party = std::make_shared<::cw::data::Party>();
        party->initialize(arg_partyheader, true);
        arg_partyheader->m_data = party;
    }
    if (party) {
        for (const auto& path : party->get_memberpaths()) {
            if (::cw::g_cwpy && ::cw::g_cwpy->m_ydata && *::cw::g_cwpy->m_ydata) {
                if (const auto header = (*::cw::g_cwpy->m_ydata)->create_advheader(path, false, std::nullopt))
                    m_headers.push_back(header);
            } else {
                auto header = std::make_shared<::cw::header::AdventurerHeader>();
                header->m_fpath = path;
                m_headers.push_back(std::move(header));
            }
        }
    }
    if (!m_headers.empty()) {
        const auto loaded = load_adventurer_value(m_headers.front());
        if (!loaded.is_none()) m_ccard = loaded;
    }
    m_is_playingscenario = arg_partyheader && arg_partyheader->is_adventuring();
    CharaInfo::initialize(m_parent, m_redrawfunc, true, party);
}

void StatusPanel::initialize(std::shared_ptr<::cw::lib::Window> arg_parent, std::vector<std::shared_ptr<::cw::character::Character>> arg_mlist, std::shared_ptr<::cw::character::Character> arg_ccard, bool arg_editable) { (void)arg_parent; m_charalist = std::move(arg_mlist); m_ccard = arg_ccard ? ::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(arg_ccard)) : ::cw::lib::Value{}; m__editable = arg_editable; _init_view(); }
void StatusPanel::update_cursor() { if (m_on_cursor_changed) m_on_cursor_changed(); }
void StatusPanel::OnLeftUp(std::shared_ptr<::cw::lib::Window> arg_event) { (void)arg_event; if (m__editable && m_on_edit) m_on_edit(); }
void StatusPanel::_init_view() { m__ratey = ::cw::lib::Integer(17); }
void StatusPanel::draw(bool arg_update) { if (arg_update) _init_view(); }
void StatusPanel::OnPaint(std::shared_ptr<::cw::lib::Window> arg_event) { (void)arg_event; draw(false); }
::cw::lib::String StatusPanel::get_detailtext()
{
    const auto c = character_value(m_ccard);
    if (!c) return {};
    ::cw::lib::String r = std::get<1>(_get_life());
    const auto add = [&r](const ::cw::lib::String& s) { if (!s.empty()) r += "\n" + s; };
    if (c->is_poison()) add(_get_poison());
    if (c->is_paralyze()) add(c->is_petrified() ? _get_petrified() : _get_paralyze());
    const auto mental = _get_mentality();
    if (!std::get<1>(mental).empty()) add(std::get<0>(mental));
    if (c->is_bind()) add(_get_bind());
    if (c->is_silence()) add(_get_silence());
    if (c->is_faceup()) add(_get_faceup());
    if (c->is_antimagic()) add(_get_antimagic());
    add(std::get<2>(_get_enhance(charainfo_msg("enhance_action", "行動"), c->m_enhance_act, c->m_enhance_act_dur, "UP0", "DOWN0")));
    add(std::get<2>(_get_enhance(charainfo_msg("enhance_avoid", "回避"), c->m_enhance_avo, c->m_enhance_avo_dur, "UP1", "DOWN1")));
    add(std::get<2>(_get_enhance(charainfo_msg("enhance_resist", "抵抗"), c->m_enhance_res, c->m_enhance_res_dur, "UP2", "DOWN2")));
    add(std::get<2>(_get_enhance(charainfo_msg("enhance_defense", "防御"), c->m_enhance_def, c->m_enhance_def_dur, "UP3", "DOWN3")));
    return r;
}
std::tuple<::cw::lib::Color, ::cw::lib::String> StatusPanel::_get_life()
{
    const auto c = character_value(m_ccard);
    if (!c) return {{192, 192, 192, 255}, charainfo_msg("fine", "正常")};
    if (c->is_unconscious()) return {{0, 0, 128, 255}, charainfo_msg("unconscious", "意識不明")};
    if (c->is_heavyinjured())
        return {{127, 0, 0, 255}, charainfo_msg("heavy_injured", "重傷") + " (" + std::to_string(c->get_lifeper()) + "%)"};
    if (c->is_injured())
        return {{0, 153, 187, 255}, charainfo_msg("injured", "負傷") + " (" + std::to_string(c->get_lifeper()) + "%)"};
    return {{192, 192, 192, 255}, charainfo_msg("fine", "正常")};
}
::cw::lib::String StatusPanel::_get_poison()
{
    const auto c = character_value(m_ccard);
    if (!c) return {};
    return charainfo_format_ss("%s (%s)", charainfo_msg("poison", "中毒"),
                               charainfo_format_s(charainfo_msg("intensity", "強度%s"), std::to_string(c->m_poison)));
}
::cw::lib::String StatusPanel::_get_paralyze()
{
    const auto c = character_value(m_ccard);
    if (!c) return {};
    return charainfo_format_ss("%s (%s)", charainfo_msg("paralyze", "麻痺"),
                               charainfo_format_s(charainfo_msg("intensity", "強度%s"), std::to_string(c->m_paralyze)));
}
::cw::lib::String StatusPanel::_get_petrified()
{
    const auto c = character_value(m_ccard);
    if (!c) return {};
    return charainfo_format_ss("%s (%s)", charainfo_msg("petrified", "石化"),
                               charainfo_format_s(charainfo_msg("intensity", "強度%s"), std::to_string(c->m_paralyze)));
}
std::tuple<::cw::lib::String, ::cw::lib::String> StatusPanel::_get_mentality()
{
    const auto c = character_value(m_ccard);
    if (!c) return {::cw::lib::String{}, ::cw::lib::String{}};
    const auto dur = charainfo_format_s(charainfo_msg("duration", "%sラウンド"), std::to_string(c->m_mentality_dur));
    if (c->is_sleep()) return {charainfo_format_ss("%s (%s)", charainfo_msg("sleep", "眠り状態"), dur), "MIND1"};
    if (c->is_confuse()) return {charainfo_format_ss("%s (%s)", charainfo_msg("confuse", "混乱状態"), dur), "MIND2"};
    if (c->is_overheat()) return {charainfo_format_ss("%s (%s)", charainfo_msg("overheat", "激昂状態"), dur), "MIND3"};
    if (c->is_brave()) return {charainfo_format_ss("%s (%s)", charainfo_msg("brave", "勇敢状態"), dur), "MIND4"};
    if (c->is_panic()) return {charainfo_format_ss("%s (%s)", charainfo_msg("panic", "恐慌状態"), dur), "MIND5"};
    return {::cw::lib::String{}, ::cw::lib::String{}};
}
::cw::lib::String StatusPanel::_get_bind()
{
    const auto c = character_value(m_ccard);
    if (!c) return {};
    return charainfo_format_ss("%s (%s)", charainfo_msg("bind", "呪縛状態"),
                               charainfo_format_s(charainfo_msg("duration", "%sラウンド"), std::to_string(c->m_bind)));
}
::cw::lib::String StatusPanel::_get_silence()
{
    const auto c = character_value(m_ccard);
    if (!c) return {};
    return charainfo_format_ss("%s (%s)", charainfo_msg("silence", "沈黙状態"),
                               charainfo_format_s(charainfo_msg("duration", "%sラウンド"), std::to_string(c->m_silence)));
}
::cw::lib::String StatusPanel::_get_faceup()
{
    const auto c = character_value(m_ccard);
    if (!c) return {};
    return charainfo_format_ss("%s (%s)", charainfo_msg("faceup", "暴露状態"),
                               charainfo_format_s(charainfo_msg("duration", "%sラウンド"), std::to_string(c->m_faceup)));
}
::cw::lib::String StatusPanel::_get_antimagic()
{
    const auto c = character_value(m_ccard);
    if (!c) return {};
    return charainfo_format_ss("%s (%s)", charainfo_msg("antimagic", "完全魔法防御状態"),
                               charainfo_format_s(charainfo_msg("duration", "%sラウンド"), std::to_string(c->m_antimagic)));
}
::cw::lib::Integer StatusPanel::_draw_status(std::shared_ptr<::cw::lib::DeviceContext> arg_dc, ::cw::lib::String arg_msg, ::cw::lib::String arg_imgname, ::cw::lib::Integer arg_height)
{
    (void)arg_dc;
    (void)arg_msg;
    (void)arg_imgname;
    return arg_height + charainfo_scaled(17);
}

std::tuple<::cw::lib::Color, std::shared_ptr<::cw::lib::Bitmap>, ::cw::lib::String> StatusPanel::_get_enhance(::cw::lib::String arg_enhname, ::cw::lib::Integer arg_value, ::cw::lib::Integer arg_dur, ::cw::lib::String arg_enhimage, ::cw::lib::String arg_pnlimage)
{
    if (!arg_value) return {::cw::lib::Color{}, std::shared_ptr<::cw::lib::Bitmap>{}, ::cw::lib::String{}};
    const bool positive = arg_value > 0;
    const auto magnitude = std::llabs(arg_value);
    const ::cw::lib::String level = magnitude >= 10 ? (positive ? charainfo_msg("maximum_bonus", "%s最大ボーナス") : charainfo_msg("maximum_penalty", "%s最大ペナルティ"))
        : magnitude >= 7 ? (positive ? charainfo_msg("big_bonus", "%s大ボーナス") : charainfo_msg("big_penalty", "%s大ペナルティ"))
        : magnitude >= 4 ? (positive ? charainfo_msg("middle_bonus", "%s中ボーナス") : charainfo_msg("middle_penalty", "%s中ペナルティ"))
        : (positive ? charainfo_msg("small_bonus", "%s小ボーナス") : charainfo_msg("small_penalty", "%s小ペナルティ"));
    const ::cw::lib::Color color = positive ? (magnitude >= 10 ? ::cw::lib::Color{255, 0, 0, 255} : magnitude >= 7 ? ::cw::lib::Color{175, 0, 0, 255}
        : magnitude >= 4 ? ::cw::lib::Color{127, 0, 0, 255} : ::cw::lib::Color{79, 0, 0, 255})
        : (magnitude >= 10 ? ::cw::lib::Color{0, 0, 51, 255} : magnitude >= 7 ? ::cw::lib::Color{0, 0, 85, 255}
        : magnitude >= 4 ? ::cw::lib::Color{0, 0, 136, 255} : ::cw::lib::Color{0, 0, 187, 255});
    const auto image_key = positive ? arg_enhimage : arg_pnlimage;
    std::shared_ptr<::cw::lib::Bitmap> bitmap;
    if (auto surface = charainfo_status_surface(image_key)) bitmap = std::make_shared<::cw::lib::Bitmap>(static_cast<const ::cw::lib::Bitmap&>(*surface));
    const auto dur_s = charainfo_format_s(charainfo_msg("duration", "%sラウンド"),
                                         std::to_string(std::max<::cw::lib::Integer>(0, arg_dur)));
    ::cw::lib::String msg = charainfo_format_ss(level + " (%s)", arg_enhname, dur_s);
    return {color, bitmap, msg};
}

::cw::lib::Integer StatusPanel::_draw_enhance(std::shared_ptr<::cw::lib::Window> arg_dc, ::cw::lib::String arg_enhname, ::cw::lib::Integer arg_value, ::cw::lib::Integer arg_dur, ::cw::lib::String arg_enhimage, ::cw::lib::String arg_pnlimage, ::cw::lib::Integer arg_height)
{
    (void)arg_dc;
    if (!arg_value) return arg_height;
    (void)_get_enhance(std::move(arg_enhname), arg_value, arg_dur, std::move(arg_enhimage), std::move(arg_pnlimage));
    return arg_height + charainfo_scaled(17);
}

void TitlePanel::initialize(::cw::lib::Value arg_parent, std::shared_ptr<::cw::lib::Window> arg_notebook) { m_parent = std::move(arg_parent); m_notebook = arg_notebook ? ::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(arg_notebook)) : ::cw::lib::Value{}; m_text_value.clear(); }
void TitlePanel::draw(bool arg_update)
{
    (void)arg_update;
    const auto notebook = object_value<::cw::lib::Window>(m_notebook);
    const auto index = notebook ? notebook->id : 0;
    if (index == 0) m_text_value = charainfo_msg("description", "解説");
    else if (index == 1) m_text_value = charainfo_msg("history", "経歴");
    else if (index == 2) m_text_value = m_is_playingscenario ? charainfo_msg("status", "状態") : charainfo_msg("edit", "編集");
    else if (index == 3) m_text_value = charainfo_msg("skillcard", "技能カード");
    else if (index == 4) m_text_value = charainfo_msg("itemcard", "アイテムカード");
    else m_text_value = charainfo_msg("beastcard", "召喚獣カード");
}
void TitlePanel::OnPaint(std::shared_ptr<::cw::lib::Window> arg_event) { (void)arg_event; draw(false); }
void TopPanel::initialize(::cw::lib::Value arg_parent, ::cw::lib::Value arg_ccard, std::optional<::cw::lib::Callback> arg_redrawfunc) { m_parent = std::move(arg_parent); m_ccard = std::move(arg_ccard); m_redrawfunc = arg_redrawfunc; m_animating = false; draw(true); }
bool TopPanel::_has_animated_images()
{
    const auto [infos, unused_scaled] = charainfo_portrait_infos(m_ccard);
    (void)unused_scaled;
    for (const auto& info : infos) {
        if (!info) continue;
        const auto path = info->m_path;
        if (::cw::animatedimage::is_supported_path(path)) return true;
        auto lower = path;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (lower.ends_with(".jpy1") || lower.ends_with(".jpdc")) return true;
    }
    return false;
}
void TopPanel::OnAnimate(std::shared_ptr<::cw::lib::Window> arg_event) { (void)arg_event; m_animating = _has_animated_images(); if (m_animating) draw(false); }
void TopPanel::OnDestroy(std::shared_ptr<::cw::lib::Window> arg_event) { (void)arg_event; m_animating = false; m__animtimer = ::cw::lib::Value{}; }
void TopPanel::OnPaint(std::shared_ptr<::cw::lib::Window> arg_event) { (void)arg_event; draw(false); }
void TopPanel::draw(bool arg_update)
{
    if (arg_update) m_animating = _has_animated_images();
    const auto c = character_value(m_ccard);
    const auto album = album_value(m_ccard);
    const bool has_character_data = static_cast<bool>(c) || static_cast<bool>(album);
    m_age.clear();
    m_sex.clear();
    m_ep.clear();
    m_baselevel = 0;
    m_race.reset();
    if (has_character_data) {
        m_baselevel = character_level(m_ccard);
        const auto player = std::dynamic_pointer_cast<::cw::character::Player>(c);
        const bool hide_traits = std::dynamic_pointer_cast<::cw::sprite::card::EnemyCard>(c) ||
                                 std::dynamic_pointer_cast<::cw::sprite::card::FriendCard>(c);
        if (!hide_traits) {
            if (c) {
                m_age = c->get_age();
                m_sex = c->get_sex();
                m_race = c->get_race();
            } else {
                if (!::cw::setting::Setting::s_periods.empty() && ::cw::setting::Setting::s_periods.front())
                    m_age = ::cw::setting::Setting::s_periods.front()->m_name;
                if (!::cw::setting::Setting::s_sexes.empty() && ::cw::setting::Setting::s_sexes.front())
                    m_sex = ::cw::setting::Setting::s_sexes.front()->m_name;
                m_race = ::cw::setting::Setting::s_unknown_race;
            }
            m_ep = "0";
            if (const auto root = value_root(m_ccard))
                if (const auto coupons = root->find("Property/Coupons"); coupons && *coupons)
                    for (const auto& coupon : (*coupons)->m_children) {
                        if (!coupon) continue;
                        const auto text = coupon->get_text();
                        if (text == "＠ＥＰ") m_ep = coupon->get("value", "0");
                        if (!c) {
                            if (std::find(::cw::setting::Setting::s_periodcoupons.begin(),
                                ::cw::setting::Setting::s_periodcoupons.end(), text) !=
                                ::cw::setting::Setting::s_periodcoupons.end())
                                m_age = text.rfind("＿", 0) == 0 ? text.substr(std::string("＿").size()) : text;
                            if (std::find(::cw::setting::Setting::s_sexcoupons.begin(),
                                ::cw::setting::Setting::s_sexcoupons.end(), text) !=
                                ::cw::setting::Setting::s_sexcoupons.end())
                                m_sex = text.rfind("＿", 0) == 0 ? text.substr(std::string("＿").size()) : text;
                            if (text.rfind("＠Ｒ", 0) == 0)
                                for (const auto& race : ::cw::setting::Setting::s_races)
                                    if (race && text == "＠Ｒ" + race->m_name) { m_race = race; break; }
                        }
                    }
        }
        m_detail_text = "[ " + character_showing_name(m_ccard) + " ] Level " +
            std::to_string(character_level(m_ccard));
        if (player || album) {
            const auto race = m_race;
            if (race && !std::dynamic_pointer_cast<::cw::header::UnknownRaceHeader>(race) && !race->m_name.empty())
                m_detail_text += " / " + race->m_name;
            if (!m_age.empty() || !m_sex.empty()) m_detail_text += " / " + m_age + m_sex;
            if (!m_ep.empty()) m_detail_text += " / EP " + m_ep;
        }
    } else {
        m_detail_text.clear();
    }
    if (m_redrawfunc) (*m_redrawfunc)({}, {});
}
::cw::lib::String TopPanel::get_detailtext() { return m_detail_text; }
void ItemPanel::initialize(std::shared_ptr<::cw::lib::Window> arg_parent, std::shared_ptr<::cw::character::Character> arg_ccard) { CardPanel::initialize(std::move(arg_parent), std::move(arg_ccard), ::cw::g_POCKET_ITEM); }
void SkillPanel::initialize(std::shared_ptr<::cw::lib::Window> arg_parent, std::shared_ptr<::cw::character::Character> arg_ccard) { CardPanel::initialize(std::move(arg_parent), std::move(arg_ccard), ::cw::g_POCKET_SKILL); }
void BeastPanel::initialize(std::shared_ptr<::cw::lib::Window> arg_parent, std::shared_ptr<::cw::character::Character> arg_ccard) { CardPanel::initialize(std::move(arg_parent), std::move(arg_ccard), ::cw::g_POCKET_BEAST); }
void BeastPanel::OnLeftUp(std::shared_ptr<::cw::lib::Window> arg_event) { (void)arg_event; _open_cardinfo(); }
void BeastPanel::OnRightUp(std::shared_ptr<::cw::lib::Window> arg_event) { if (std::dynamic_pointer_cast<::cw::character::Player>(m_character)) OnDeck(std::move(arg_event)); else CardPanel::OnRightUp(std::move(arg_event)); }

::cw::lib::Value g__N{};
::cw::lib::Value g__T{};
void CharaInfo___init___onfocus(std::shared_ptr<::cw::lib::Window> arg_event) { if (arg_event) arg_event->id = 0; }
::cw::lib::Value CharaInfo___init___lambda_line_164() { return ::cw::lib::Value(false); }
::cw::lib::Value CharaInfo___init___lambda_line_165() { return ::cw::lib::Value(false); }
::cw::lib::Value CharaInfo___init___lambda_line_166() { return ::cw::lib::Value(false); }
::cw::lib::Value CharaInfo___init___lambda_line_167() { return ::cw::lib::Value(false); }
::cw::lib::Value CharaInfo___init___lambda_line_168() { return ::cw::lib::Value(false); }
void CharaInfo___init___onfocus_2(std::shared_ptr<::cw::lib::Window> arg_event) { CharaInfo___init___onfocus(std::move(arg_event)); }
void CharaInfo__bind_recurse(std::shared_ptr<::cw::lib::Control> arg_ctrl) { if (!arg_ctrl) return; arg_ctrl->visible = true; }
void CharaInfo_OnDebugMode_func(::cw::lib::Value arg_self) { if (const auto object = std::get_if<::cw::lib::ObjectPtr>(&arg_self.storage()); object && *object) if (const auto info = std::dynamic_pointer_cast<CharaInfo>(*object)) info->update_debug(); }
void CharaInfo_OnDebugMode_func_func(::cw::lib::Value arg_self) { CharaInfo_OnDebugMode_func(std::move(arg_self)); }
void HistoryPanel_OnLeftUp_func(::cw::lib::Value arg_panel) { if (const auto panel = object_value<HistoryPanel>(arg_panel)) panel->draw(true); }
void HistoryPanel_OnLeftUp_func_func(::cw::lib::Value arg_panel) { HistoryPanel_OnLeftUp_func(std::move(arg_panel)); }
void EditPanel_OnLeftUp_func(std::shared_ptr<::cw::lib::Panel> arg_panel) { if (arg_panel) arg_panel->visible = true; }
void EditPanel_OnLeftUp_func_2(std::shared_ptr<::cw::lib::Panel> arg_panel) { EditPanel_OnLeftUp_func(std::move(arg_panel)); }
void EditPanel_OnLeftUp_func2(std::shared_ptr<::cw::lib::Panel> arg_panel, std::shared_ptr<::cw::character::Character> arg_ccard) { (void)arg_ccard; EditPanel_OnLeftUp_func(std::move(arg_panel)); }
void EditPanel_update_charalist_func(std::vector<::cw::lib::Value> arg_parentheaders, std::vector<std::shared_ptr<::cw::character::Player>> arg_mlist)
{
    const auto count = std::min(arg_parentheaders.size(), arg_mlist.size());
    for (std::size_t i = 0; i < count; ++i) {
        const auto& player = arg_mlist[i];
        if (!player) continue;
        write_character_value(::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(player)));
        if (const auto header = object_value<::cw::header::AdventurerHeader>(arg_parentheaders[i]))
            header->m_level = player->m_level;
    }
}
void EditPanel_update_charalist_func2(::cw::lib::Integer arg_index, std::vector<::cw::lib::Value> arg_headers, std::vector<std::shared_ptr<::cw::character::Player>> arg_mlist)
{
    if (arg_index < 0 || arg_index >= static_cast<::cw::lib::Integer>(arg_headers.size()) || arg_mlist.empty()) return;
    const auto& player = arg_mlist.front();
    if (!player) return;
    write_character_value(::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(player)));
    if (const auto header = object_value<::cw::header::AdventurerHeader>(arg_headers[static_cast<std::size_t>(arg_index)]))
        header->m_level = player->m_level;
}
::cw::lib::Value DeckDialog___init___lambda_line_2362(::cw::lib::Value arg_evt) { return arg_evt; }
::cw::lib::Value DeckDialog___init___lambda_line_2385(::cw::lib::Value arg_evt) { return arg_evt; }
::cw::lib::Value DeckDialog___init___lambda_line_2388(::cw::lib::Value arg_evt) { return arg_evt; }
::cw::lib::Value DeckDialog__rebuild_area_buttons_lambda_line_2603(::cw::lib::Value arg_r) { return arg_r; }
::cw::lib::Value DeckDialog__rebuild_area_buttons_lambda_line_2619(::cw::lib::Value arg_idx) { return arg_idx; }
::cw::lib::Value DeckDialog__rebuild_area_buttons_lambda_line_2619_2(::cw::lib::Value arg_evt) { return arg_evt; }
::cw::lib::Value DeckDialog__rebuild_area_buttons_lambda_line_2622(::cw::lib::Value arg_idx) { return arg_idx; }
::cw::lib::Value DeckDialog__rebuild_area_buttons_lambda_line_2622_2(::cw::lib::Value arg_evt) { return arg_evt; }
::cw::lib::Value DeckDialog__rebuild_area_buttons_lambda_line_2625(::cw::lib::Value arg_idx) { return arg_idx; }
::cw::lib::Value DeckDialog__rebuild_area_buttons_lambda_line_2625_2(::cw::lib::Value arg_evt) { return arg_evt; }
std::tuple<::cw::lib::String, ::cw::lib::String> DeckDialog__search_item_sources__hdr_key(::cw::lib::Value arg_hdr) { const auto header = header_value(arg_hdr); return header ? std::make_tuple(header->m_name, header_description(header)) : std::make_tuple(::cw::lib::String{}, ::cw::lib::String{}); }
::cw::lib::String DeckDialog_OnArea__ppath_to_type(std::optional<::cw::lib::String> arg_ppath, ::cw::lib::String arg_fallback_type) { if (!arg_ppath) return arg_fallback_type; if (arg_ppath->rfind("SkillCards", 0) == 0) return "Skill"; if (arg_ppath->rfind("BeastCards", 0) == 0) return "Beast"; if (arg_ppath->rfind("ItemCards", 0) == 0) return "Item"; return arg_fallback_type; }
::cw::lib::Color get_bgcolor(::cw::lib::Value arg_ccard)
{
    const auto root = value_root(arg_ccard);
    if (!root) return {0,0,128,255};
    const auto read = [&root](const ::cw::lib::String& key) {
        return std::clamp<::cw::lib::Integer>(
            root->getint("Property/BackColor", ::cw::lib::Value(key), -1), -1, 128);
    };
    const auto r = read("r");
    const auto g = read("g");
    const auto b = read("b");
    if (r < 0 || g < 0 || b < 0) return {0,0,128,255};
    return {static_cast<std::uint8_t>(r), static_cast<std::uint8_t>(g),
            static_cast<std::uint8_t>(b), 255};
}
void apply_bgcolor(std::shared_ptr<::cw::lib::Window> arg_currentpanel, ::cw::lib::Value arg_ccard) { if (!arg_currentpanel) return; const auto color = get_bgcolor(std::move(arg_ccard)); arg_currentpanel->title = "bgcolor:" + std::to_string(color.r) + "," + std::to_string(color.g) + "," + std::to_string(color.b); arg_currentpanel->bounds.x = color.r; arg_currentpanel->bounds.y = color.g; }
void main() { /* Python のモジュール用エントリーポイントは、意図的に副作用を持たない。 */ }

} // namespace cw::dialog::charainfo
