// UIの実装は一つに統一し、
// 別経路の実装を設けない。
#include "cw/debug/logging.hpp"
#include "cw/lib/encoding.hpp"
#include "cw/debug/retained_presentation.hpp"
#include "app/debug_control_server.hpp"
#include "cw/cw.hpp"
#include "cw/thread.hpp"
#include "cw/data.hpp"
#include "cw/setting.hpp"
#include "cw/character.hpp"
#include "cw/sprite/card.hpp"
#include "cw/sprite/statusbar.hpp"
#include "cw/frame.hpp"
#include "cw/util.hpp"
#include "cw/lib/ui_surface.hpp"
#include "cw/lib/decimal.hpp"
#include "cw/imageretouch.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <fstream>
#include <map>
#include <mutex>

namespace cw::debug::logging {
namespace {
using String = ::cw::lib::String;
using Integer = ::cw::lib::Integer;
using Real = ::cw::lib::Real;
using CardKey = std::tuple<String, String, String, String>;

template<class T>
std::shared_ptr<T> common_object_value(const ::cw::lib::Value& value)
{
    const auto* object = std::get_if<::cw::lib::ObjectPtr>(&value.storage());
    return object && *object ? std::dynamic_pointer_cast<T>(*object) : nullptr;
}

std::shared_ptr<::cw::lib::Surface> common_debug_surface(const String& key)
{
    if (!::cw::g_cwpy || !::cw::g_cwpy->m_rsrc) return {};
    const auto table_value = key == "PREMIER_ICON_dbg" || key == "RARE_ICON_dbg"
        ? ::cw::g_cwpy->m_rsrc->m_dialogs : ::cw::g_cwpy->m_rsrc->m_debugs;
    return ::cw::lib::ui::resource_table_surface(table_value,key);
}


thread_local std::vector<DebugLogDisplayLine>* display_line_builder{};

void line(String& out, const String& value = {})
{
    out += value;
    out.push_back('\n');
    if (display_line_builder) {
        DebugLogDisplayLine display;
        display.text = value;
        display_line_builder->push_back(std::move(display));
    }
}

void display_line(String& out, const String& value, const String& icon)
{
    line(out, value);
    if (display_line_builder && !display_line_builder->empty())
        display_line_builder->back().icons.push_back(icon);
}

class DisplayLineBuilderScope final {
public:
    explicit DisplayLineBuilderScope(std::vector<DebugLogDisplayLine>& lines)
        : m_previous(display_line_builder)
    {
        display_line_builder = &lines;
    }
    ~DisplayLineBuilderScope() { display_line_builder = m_previous; }
    DisplayLineBuilderScope(const DisplayLineBuilderScope&) = delete;
    DisplayLineBuilderScope& operator=(const DisplayLineBuilderScope&) = delete;

private:
    std::vector<DebugLogDisplayLine>* m_previous{};
};

std::vector<DebugLogDisplayLine> make_display_lines(
    const String& text, const std::vector<DebugLogDisplayLine>* semantic_lines = nullptr)
{
    std::vector<DebugLogDisplayLine> result;
    std::size_t line_index = 0;
    for (std::size_t begin = 0; begin <= text.size();) {
        const auto end = text.find('\n', begin);
        String value = text.substr(begin, end == String::npos ? String::npos : end - begin);
        if (!value.empty() && value.back() == '\r') value.pop_back();

        DebugLogDisplayLine display;
        display.text = value;
        const auto add = [&display](const String& icon) {
            display.icons.push_back(icon);
        };
        if (!semantic_lines) {
            if (value.rfind("「", 0) == 0 &&
                value.find("を宿帳に登録します。") != String::npos) {
                add("FRIEND");
            } else if (value.rfind("所持金が ", 0) == 0) {
                add(value.find("増加しています") != String::npos
                        ? "EVT_GET_MONEY" : "EVT_LOSE_MONEY");
            } else if (value.rfind("ゴシップ「", 0) == 0) {
                add(value.find("追加しました") != String::npos
                        ? "EVT_GET_GOSSIP" : "EVT_LOSE_GOSSIP");
            } else if (value.rfind("終了印「", 0) == 0) {
                add(value.find("追加しました") != String::npos
                        ? "EVT_GET_COMPLETESTAMP" : "EVT_LOSE_COMPLETESTAMP");
            } else if (value.rfind("JPDCイメージ「", 0) == 0) {
                add("JPDCIMAGE");
            } else if (value.rfind("コモン「", 0) == 0) {
                add("VARIANT");
            } else if (value.rfind("ステップ「", 0) == 0) {
                add("STEP");
            } else if (value.rfind("フラグ「", 0) == 0) {
                add("FLAG");
            } else if (value == "保存されていた状態変数値を破棄しました。") {
                add("REMOVE_VARIABLES");
            }
        }

        if (value.rfind("    * ", 0) == 0) {
            display.indent = 50;
            display.text.erase(0, 6);
            const auto value_begin = value.find('(', 6);
            const auto value_end = value_begin == String::npos
                ? String::npos : value.find(')', value_begin + 1);
            if (!semantic_lines && value.find("を喪失") != String::npos) {
                add("EVT_LOSE_COUPON");
            } else if (!semantic_lines && value.find("を獲得") != String::npos && value_end != String::npos) {
                const auto score = value.substr(value_begin + 1, value_end - value_begin - 1);
                if (!score.empty() && score.front() == '-') add("COUPON_MINUS");
                else if (score == "+0") add("COUPON_ZERO");
                else if (score == "+1") add("COUPON_PLUS");
                else add("COUPON");
            }
        }

        const auto card_icon = [&]() -> String {
            if (value.rfind("特殊技能「", 0) == 0) return "EVT_";
            if (value.rfind("アイテム「", 0) == 0) return "EVT_";
            if (value.rfind("召喚獣「", 0) == 0) return "EVT_";
            return {};
        };
        const auto card_prefix = card_icon();
        if (!semantic_lines && !card_prefix.empty()) {
            const bool got = value.find("獲得しました") != String::npos;
            const bool lost = value.find("喪失しました") != String::npos;
            if (got || lost) {
                const auto type = value.rfind("特殊技能", 0) == 0 ? "SKILL" :
                                  value.rfind("アイテム", 0) == 0 ? "ITEM" : "BEAST";
                add(String("EVT_") + (got ? "GET_" : "LOSE_") + type);
                const auto premier = value.find("(プレミア)");
                const auto rare = value.find("(レア)");
                if (premier != String::npos) {
                    display.inline_icon = std::make_pair(String("PREMIER_ICON_dbg"), premier);
                    display.text.erase(premier, String("(プレミア)").size());
                } else if (rare != String::npos) {
                    display.inline_icon = std::make_pair(String("RARE_ICON_dbg"), rare);
                    display.text.erase(rare, String("(レア)").size());
                }
            }
        }

        // この行に属するインラインマーカーは意味解析側で判定済みなの
        // で、ここでは文字列中のマーカーだけを除く。 アイコンと位置
        // は日本語の文章ではなく、型付きのメタデータから取得する。
        if (semantic_lines && line_index < semantic_lines->size() &&
            (*semantic_lines)[line_index].inline_icon) {
            const auto& key = (*semantic_lines)[line_index].inline_icon->first;
            const auto marker = key == "PREMIER_ICON_dbg" ? String("(プレミア)") : String("(レア)");
            const auto marker_pos = (*semantic_lines)[line_index].inline_icon->second;
            if (value.compare(marker_pos,marker.size(),marker)==0)
                display.text.erase(marker_pos, marker.size());
        }

        if (semantic_lines && line_index < semantic_lines->size()) {
            display.icons = (*semantic_lines)[line_index].icons;
            display.inline_icon = (*semantic_lines)[line_index].inline_icon;
        }
        result.push_back(std::move(display));
        ++line_index;
        if (end == String::npos) break;
        begin = end + 1;
    }
    return result;
}

String lower_text(String value)
{
    return ::cw::lib::encoding::lower(value);
}

struct LogicalToken {
    bool number{};
    String text{};
    String normalized{};
    String raw{};
};

std::vector<LogicalToken> logical_tokens(const String& value)
{
    std::vector<LogicalToken> result;
    std::size_t pos = 0;
    std::size_t text_begin = 0;
    while (pos < value.size()) {
        if (!std::isdigit(static_cast<unsigned char>(value[pos]))) {
            ++pos;
            continue;
        }
        // Python と同様、接頭辞が空でも終端の目印を含める。
        result.push_back({false, lower_text(value.substr(text_begin, pos - text_begin)) + "0", {}, {}});
        const auto number_begin = pos;
        while (pos < value.size() && std::isdigit(static_cast<unsigned char>(value[pos]))) ++pos;
        const auto raw = value.substr(number_begin, pos - number_begin);
        const auto first = raw.find_first_not_of('0');
        const auto normalized = first == String::npos ? String("0") : raw.substr(first);
        result.push_back({true, {}, normalized, raw});
        text_begin = pos;
    }
    if (!value.empty()) result.push_back({false, lower_text(value.substr(text_begin)), {}, {}});
    return result;
}

int logical_compare(const String& lhs, const String& rhs)
{
    const auto left = logical_tokens(lhs);
    const auto right = logical_tokens(rhs);
    const auto count = std::min(left.size(), right.size());
    for (std::size_t i = 0; i < count; ++i) {
        const auto& a = left[i];
        const auto& b = right[i];
        if (a.number && b.number) {
            if (a.normalized.size() != b.normalized.size()) {
                return a.normalized.size() < b.normalized.size() ? -1 : 1;
            }
            if (a.normalized != b.normalized) return a.normalized < b.normalized ? -1 : 1;
            if (a.raw != b.raw) return a.raw < b.raw ? -1 : 1;
        } else if (!a.number && !b.number) {
            if (a.text != b.text) return a.text < b.text ? -1 : 1;
        } else {
            // Python は通常、同種のトークンを比較する。不正な名前や型の混
            // 在した名前でも、 ログ出力で例外を出さず、一定の順序を維持する。
            return a.number ? 1 : -1;
        }
    }
    if (left.size() != right.size()) return left.size() < right.size() ? -1 : 1;
    if (lhs == rhs) return 0;
    return lhs < rhs ? -1 : 1;
}

bool logical_less(const String& lhs, const String& rhs)
{
    return logical_compare(lhs, rhs) < 0;
}

String typename_of(const String& type)
{
    if (type == "SkillCard") return "特殊技能";
    if (type == "ItemCard") return "アイテム";
    if (type == "BeastCard") return "召喚獣";
    throw std::logic_error("未知のカード種別: " + type);
}

template<class T>
void toggles(String& out, T values, bool get, const String& noun,
             const String& get_icon, const String& lose_icon)
{
    std::sort(values.begin(), values.end(), [](const auto& lhs, const auto& rhs) {
        return logical_less(std::get<0>(lhs), std::get<0>(rhs));
    });
    for (const auto& value : values) {
        if (std::get<1>(value) == get) {
            display_line(out, noun + "「" + std::get<0>(value) + "」を" +
                        (get ? "追加しました。" : "削除しました。"),
                        get ? get_icon : lose_icon);
        }
    }
}

String local_datetime(const ::cw::lib::DateTime& value)
{
    const auto local = value.calendar();
    std::ostringstream out;
    out << std::put_time(&local, "%Y-%m-%d %H:%M:%S");
    return out.str();
}

::cw::lib::Decimal round_seconds(Real value)
{
    if (!std::isfinite(value))
        throw std::overflow_error("play time is not finite");
    const Real lower = std::floor(value);
    const Real fraction = value - lower;
    Real rounded = lower;
    if (fraction > 0.5) {
        rounded = lower + 1.0;
    } else if (fraction == 0.5 &&
               std::fmod(std::fabs(lower), 2.0) >= 1.0) {
        rounded = lower + 1.0;
    }
    // Python の round(float) は任意精度整数を返す。整数化済み
    // の二進浮動小数点値を直接整形し、 範囲外となるネイティブ整数へのキャストを避
    // ける。Boost がないビルドでも Decimal で正確な代替処理を行う。
    std::ostringstream text;
    text.imbue(std::locale::classic());
    text << std::fixed << std::setprecision(0) << rounded;
    return ::cw::lib::Decimal::parse(text.str());
}

std::shared_ptr<::cw::data::ScenarioData> scenario_data()
{
    if (!::cw::g_cwpy) return {};
    const auto* object = std::get_if<::cw::lib::ObjectPtr>(&::cw::g_cwpy->m_sdata.storage());
    if (!object || !*object) return {};
    return std::dynamic_pointer_cast<::cw::data::ScenarioData>(*object);
}

template <typename Number>
String currency_text(Number value)
{
    if (!::cw::g_cwpy) throw std::logic_error("currency message requires CWPy");
    const auto* object = std::get_if<::cw::lib::ObjectPtr>(&::cw::g_cwpy->m_msgs.storage());
    if (!object || !*object) throw std::logic_error("currency message table is unavailable");
    const auto messages = std::dynamic_pointer_cast<::cw::setting::MsgDict>(*object);
    if (!messages) throw std::logic_error("currency message table has an invalid type");
    const auto format = messages->get_item("currency");
    String result;
    bool substituted = false;
    for (std::size_t i = 0; i < format.size();) {
        if (format[i] != '%') {
            result.push_back(format[i++]);
            continue;
        }
        if (i + 1 < format.size() && format[i + 1] == '%') {
            result.push_back('%');
            i += 2;
            continue;
        }

        const auto begin = i++;
        while (i < format.size() &&
               (format[i] == '#' || format[i] == '0' || format[i] == '-' ||
                format[i] == '+' || format[i] == ' ')) {
            ++i;
        }
        const auto digit = [](char ch) { return ch >= '0' && ch <= '9'; };
        const auto flags = format.substr(begin + 1, i - begin - 1);
        const auto width_begin = i;
        while (i < format.size() && digit(format[i])) ++i;
        const auto width_text = format.substr(width_begin, i - width_begin);
        String precision_text;
        const bool has_precision = i < format.size() && format[i] == '.';
        if (has_precision) {
            ++i;
            const auto precision_begin = i;
            while (i < format.size() && digit(format[i])) ++i;
            precision_text = format.substr(precision_begin, i - precision_begin);
        }
        // Python は単独の h/l/L 長さ修飾子を無視する。
        // 引数サイズの解釈が異なる snprintf へ渡さない。
        const auto spec_end = i;
        if (i < format.size() && (format[i] == 'h' || format[i] == 'l' || format[i] == 'L')) ++i;
        if (i >= format.size()) throw std::logic_error("currency message has an incomplete format");
        const char conversion = format[i++];
        if (substituted)
            throw std::logic_error("currency message has multiple format arguments");
        if (conversion != 'c' && conversion != 's' && conversion != 'r' && conversion != 'a' && conversion != 'd' &&
            conversion != 'i' && conversion != 'u' && conversion != 'o' &&
            conversion != 'x' && conversion != 'X' && conversion != 'f' &&
            conversion != 'F' && conversion != 'e' && conversion != 'E' &&
            conversion != 'g' && conversion != 'G') {
            throw std::logic_error("currency message has an unsupported format");
        }
        const String spec = format.substr(begin, spec_end - begin) + conversion;
        const auto extent = [](const String& text) -> std::size_t {
            if (text.empty()) return 0;
            const auto n = std::stoull(text);
            if (n > String{}.max_size()) throw std::length_error("currency field is too wide");
            return static_cast<std::size_t>(n);
        };
        // Python の書式変換結果には固定の 255 バイト制限がな
        // い。 終端のゼロも含め、整形結果からバッファーサイズを求める。
        const auto append_formatted = [&](const String& specifier, auto argument) {
            const int size = std::snprintf(nullptr, 0, specifier.c_str(), argument);
            if (size < 0) throw std::logic_error("currency message formatting failed");
            std::vector<char> buffer(static_cast<std::size_t>(size) + 1);
            const int written = std::snprintf(buffer.data(), buffer.size(), specifier.c_str(), argument);
            if (written != size) throw std::logic_error("currency message formatting changed size");
            result.append(buffer.data(), static_cast<std::size_t>(written));
        };
        if (conversion == 'c') {
            if (value < 0 || static_cast<unsigned long long>(value) > 0x10ffffULL)
                throw std::logic_error("currency character is outside Unicode range");
            const auto cp = static_cast<std::uint32_t>(value);
            const ::cw::lib::Bytes bytes{
                static_cast<std::uint8_t>(cp), static_cast<std::uint8_t>(cp >> 8),
                static_cast<std::uint8_t>(cp >> 16), static_cast<std::uint8_t>(cp >> 24)};
            const auto character = ::cw::lib::encoding::utf32_to_utf8(bytes,
                ::cw::lib::encoding::ByteOrder::Little,
                ::cw::lib::encoding::ErrorPolicy::Strict, false);
            // %c は精度と数値フラグを無視する。幅は UTF-8
            // のバイト数ではなく、一文字として数える。
            const auto width = extent(width_text);
            const auto padding = width > 1 ? width - 1 : 0;
            if (flags.find('-') == String::npos) result.append(padding, ' ');
            result += character;
            if (flags.find('-') != String::npos) result.append(padding, ' ');
        } else if (conversion == 's' || conversion == 'r' || conversion == 'a') {
            // 入力は整数なので、str/repr/ascii は同じ ASCII
            // 表記になる。 Python はこれらの変換で数値フラグを無視する。
            auto text = std::to_string(value);
            if (has_precision && extent(precision_text) < text.size()) text.resize(extent(precision_text));
            const auto width = extent(width_text);
            if (width > text.size()) {
                const auto padding = width - text.size();
                if (flags.find('-') != String::npos) text.append(padding, ' ');
                else text.insert(0, padding, ' ');
            }
            result += text;
        } else if (conversion == 'f' || conversion == 'F' || conversion == 'e' ||
                   conversion == 'E' || conversion == 'g' || conversion == 'G') {
            append_formatted(spec, static_cast<double>(value));
        } else {
            // Python の %u は符号付きで、%#o は 0o を使い、整数の精度指定で
            // ゼロを省略しない。 ネイティブの printf はこの三点すべてで動作が異なる。
            const auto width = extent(width_text), precision = extent(precision_text);
            const unsigned base = conversion == 'o' ? 8 :
                (conversion == 'x' || conversion == 'X') ? 16 : 10;
            const char* alphabet = conversion == 'X' ? "0123456789ABCDEF" : "0123456789abcdef";
            auto magnitude = value < 0 ? 0ULL - static_cast<unsigned long long>(value) :
                                        static_cast<unsigned long long>(value);
            String digits;
            do { digits.push_back(alphabet[magnitude % base]); magnitude /= base; } while (magnitude);
            std::reverse(digits.begin(), digits.end());
            if (precision > digits.size()) digits.insert(0, precision - digits.size(), '0');
            String prefix = value < 0 ? "-" : flags.find('+') != String::npos ? "+" :
                flags.find(' ') != String::npos ? " " : "";
            if (flags.find('#') != String::npos) {
                if (base == 8) prefix += "0o";
                else if (base == 16) prefix += conversion == 'X' ? "0X" : "0x";
            }
            String formatted = prefix + digits;
            if (width > formatted.size()) {
                const auto padding = width - formatted.size();
                if (flags.find('-') != String::npos) formatted.append(padding, ' ');
                else if (flags.find('0') != String::npos) formatted.insert(prefix.size(), padding, '0');
                else formatted.insert(0, padding, ' ');
            }
            result += formatted;
        }
        substituted = true;
    }
    if (!substituted)
        throw std::logic_error("currency message has no format placeholder");
    return result;
}

void append_card_lines(String& out,
                       const std::map<CardKey, Integer>& cards,
                       bool got)
{
    if (cards.empty()) return;
    line(out);
    const std::array<String, 3> types{"SkillCard", "ItemCard", "BeastCard"};
    for (const auto& type : types) {
        std::vector<std::pair<CardKey, Integer>> entries;
        for (const auto& entry : cards) {
            if (std::get<0>(entry.first) == type) entries.push_back(entry);
        }
        std::sort(entries.begin(), entries.end(), [](const auto& lhs, const auto& rhs) {
            auto result = logical_compare(std::get<1>(lhs.first), std::get<1>(rhs.first));
            if (result != 0) return result < 0;
            result = logical_compare(std::get<2>(lhs.first), std::get<2>(rhs.first));
            if (result != 0) return result < 0;
            result = logical_compare(std::get<3>(lhs.first), std::get<3>(rhs.first));
            if (result != 0) return result < 0;
            return false;
        });
        for (const auto& [key, count] : entries) {
            String name = std::get<1>(key);
            const auto& premium = std::get<3>(key);
            if (got) {
                if (premium == "Premium") name += "(プレミア)";
                else if (premium == "Rare") name += "(レア)";
                else if (premium != "Normal") {
                    throw std::logic_error("未知のカード格付け: " + premium);
                }
            }
            const auto value = typename_of(type) + "「" + name + "」を" + std::to_string(count) + "枚" +
                               (got ? "獲得しました。" : "喪失しました。");
            line(out, value);
            if (display_line_builder && !display_line_builder->empty()) {
                auto& display = display_line_builder->back();
                display.icons.push_back(String("EVT_") + (got ? "GET_" : "LOSE_") +
                                        (type == "SkillCard" ? "SKILL" :
                                         type == "ItemCard" ? "ITEM" : "BEAST"));
                if (got && (premium == "Premium" || premium == "Rare")) {
                    const auto marker = premium == "Premium" ? String("(プレミア)") : String("(レア)");
                    const auto marker_pos = typename_of(type).size()+String("「").size()+std::get<1>(key).size();
                    if (value.compare(marker_pos,marker.size(),marker)==0)
                        display.inline_icon = std::make_pair(
                            premium == "Premium" ? String("PREMIER_ICON_dbg") : String("RARE_ICON_dbg"),
                            marker_pos);
                }
            }
        }
    }
}
}

void DebugLogUiBackend::show(DebugLogDialog*, const String&, const String&) {}
void DebugLogUiBackend::layout(DebugLogDialog*) {}
void DebugLogUiBackend::close(DebugLogDialog*) {}
void DebugLogUiBackend::report_error(const String& message)
{
    std::cerr << message << '\n';
}

std::shared_ptr<DebugLogUiBackend> get_ui_backend()
{
    static const auto backend = std::make_shared<DebugLogUiBackend>();
    return backend;
}

void DebugLog::initialize(String sname)
{
    m_sname = std::move(sname);
    m_friend.clear();
    m_lost_player.clear();
    m_player.clear();
    m_got_card.clear();
    m_lost_card.clear();
    m_money = {0, 0};
    m_compstamp.clear();
    m_gossip.clear();
    m_jpdc_image.clear();
    m_startdatetime.reset();
    m_pausedtime.reset();
    m_flags.clear();
    m_steps.clear();
    m_variants.clear();
    m_is_removevariables = false;
}

void DebugLog::add_friend(std::shared_ptr<::cw::sprite::card::FriendCard> fcard)
{
    if (!fcard) throw std::invalid_argument("friend card is null");
    m_friend.push_back(static_cast<::cw::character::Friend*>(fcard.get())->m_name);
}

void DebugLog::add_lostplayer(std::shared_ptr<::cw::character::Character> ccard)
{
    if (!ccard) throw std::invalid_argument("character is null");
    m_lost_player.emplace_back(ccard->m_name, !ccard->has_coupon("＿消滅予約"));
}

void DebugLog::add_player(std::shared_ptr<::cw::sprite::card::PlayerCard> pcard,
                          std::vector<std::tuple<String, Integer>> got_coupons,
                          std::vector<std::tuple<String, Integer>> lost_coupons)
{
    if (!pcard) throw std::invalid_argument("player card is null");
    m_player.emplace_back(static_cast<::cw::character::Player*>(pcard.get())->m_name,
                          std::move(got_coupons), std::move(lost_coupons));
}

void DebugLog::set_money(Integer before, Integer after)
{
    m_money = {before, after};
}

void DebugLog::add_gotcard(String ctype, String name, String desc, String,
                           String, String premium)
{
    ++m_got_card[std::make_tuple(std::move(ctype), std::move(name), std::move(desc), std::move(premium))];
}

void DebugLog::add_lostcard(String ctype, String name, String desc, String,
                            String, String premium)
{
    ++m_lost_card[std::make_tuple(std::move(ctype), std::move(name), std::move(desc), std::move(premium))];
}

void DebugLog::add_compstamp(String compstamp, bool get)
{
    m_compstamp.emplace_back(std::move(compstamp), get);
}

void DebugLog::add_gossip(String gossip, bool get)
{
    m_gossip.emplace_back(std::move(gossip), get);
}

void DebugLog::add_jpdcimage(String fname)
{
    m_jpdc_image.push_back(std::move(fname));
}

void DebugLog::add_flag(String name)
{
    m_flags.push_back(std::move(name));
}

void DebugLog::add_step(String name)
{
    m_steps.push_back(std::move(name));
}

void DebugLog::add_variant(String name)
{
    m_variants.push_back(std::move(name));
}

void DebugLog::remove_variables()
{
    m_is_removevariables = true;
}

void DebugLog::set_times(std::optional<::cw::lib::DateTime> startdatetime, Real pausedtime)
{
    m_startdatetime = std::move(startdatetime);
    m_pausedtime = pausedtime;
}

String DebugLogDialog___init___timestr(Real seconds)
{
    const auto rounded = round_seconds(seconds);
    using ::cw::lib::Decimal;
    const ::cw::lib::DecimalContext exact{0};
    const auto hours = rounded.floor_divide(Decimal(3600), exact);
    const auto remainder = rounded.subtract(hours.multiply(Decimal(3600), exact), exact);
    const auto within_hour = remainder.to_integer(); // 常に 0 以上 3599 以下となる。
    const auto minutes = within_hour / 60;
    const auto secs = within_hour % 60;
    if (!hours.is_zero()) return hours.to_string() + "時間" + std::to_string(minutes) + "分" + std::to_string(secs) + "秒";
    if (minutes) return std::to_string(minutes) + "分" + std::to_string(secs) + "秒";
    return std::to_string(secs) + "秒";
}

void DebugLogDialog::initialize(std::shared_ptr<::cw::lib::Window> parent,
                                std::shared_ptr<DebugLog> debuglog)
{
    if (!debuglog) throw std::invalid_argument("debuglog is null");
    m_cwpy_debug = true;
    m_title = "「" + debuglog->m_sname + "」のプレイ結果";
    m_presentation_scroll = 0;
    m_presentation_finished = false;
    m_close_notified=false;
    m_selecting=m_scroll_drag=m_popup=false;
    m_anchor=m_caret=0;
    m_focus=0;
    m_rows.clear();
    m_native_parent_handle = parent ? parent->native_handle : 0;
    if (!m_native_parent_handle && ::cw::g_cwpy) {
        const auto* object = std::get_if<::cw::lib::ObjectPtr>(
            &::cw::g_cwpy->m_frame.storage());
        if (object) {
            if (const auto frame = std::dynamic_pointer_cast<::cw::frame::Frame>(*object))
                m_native_parent_handle = frame->m_native_window_handle;
        }
    }

    if (::cw::g_cwpy) {
        ::cw::g_cwpy->exec_func([](const ::cw::lib::Arguments&, const ::cw::lib::KeywordArguments&) {
            const auto scenario = scenario_data();
            if (scenario && scenario->m_notice_debuglog) {
                scenario->m_notice_debuglog = 0;
                if (::cw::g_cwpy->m_statusbar) {
                    ::cw::g_cwpy->m_statusbar->change(true, false);
                }
                if (const auto backend = ::cw::sprite::statusbar::host_services(); backend && backend->invalidate)
                    backend->invalidate({0, 0, 0, 0});
            }
            return ::cw::lib::Value{};
        });
    }

    std::vector<DebugLogDisplayLine> semantic_lines;
    DisplayLineBuilderScope display_scope(semantic_lines);
    auto& out = m_plain_text;
    out.clear();
    line(out, "「" + debuglog->m_sname + "」のプレイ結果");
    line(out, "========================================");
    // Python は区切りの後の plain_text を空の要素から
    // 始める。最初の表示項目が所持金の集計でも、この空行を維持する。 DebugInfo.txt
    // の利用側は、この節の配置を前提とする。
    line(out);

    // RichTextCtrl はヘッダ文字列を受け取らないため、Python版の
    // GetValue() は時刻または同行者を書いた後に初めて真になる。
    bool has_visible_text = false;
    if (debuglog->m_startdatetime) {
        if (!debuglog->m_pausedtime) {
            throw std::logic_error("startdatetimeがある場合はpausedtimeが必要です。");
        }
        const auto finished = ::cw::lib::DateTime::now();
        line(out, "プレイ開始 : " + local_datetime(*debuglog->m_startdatetime));
        line(out, "プレイ終了 : " + local_datetime(finished));
        const auto elapsed = finished.seconds_since(*debuglog->m_startdatetime) -
                             *debuglog->m_pausedtime;
        line(out, "総プレイ時間 : " + DebugLogDialog___init___timestr(elapsed));
        if (*debuglog->m_pausedtime) {
            line(out, "中断時間 : " + DebugLogDialog___init___timestr(*debuglog->m_pausedtime));
        }
        has_visible_text = true;
    }

    // Py は時間の行が既にある場合、最初の同行者の前に RichText 専用の
    // 空行を追加する。 この空行は plain_text には意図的に含めない。
    const auto friend_display_break = has_visible_text && !debuglog->m_friend.empty()
        ? std::optional<std::size_t>{semantic_lines.size() - 3} : std::nullopt;
    for (const auto& name : debuglog->m_friend) {
        display_line(out, "「" + name + "」を宿帳に登録します。", "FRIEND");
    }
    if (!debuglog->m_friend.empty()) has_visible_text = true;

    // plain_text は初期リストの末尾の空要素を持つ。money 前の空要素は
    // Python側でRichTextCtrlが既に表示済みのときだけ追加される。
    if (has_visible_text) line(out);
    const auto before = debuglog->m_money.first;
    const auto after = debuglog->m_money.second;
    if (before < after) {
        const auto difference = static_cast<unsigned long long>(after) -
                                static_cast<unsigned long long>(before);
        display_line(out, "所持金が " + currency_text(difference) + " 増加しています: " +
                     currency_text(before) + " → " + currency_text(after), "EVT_GET_MONEY");
    } else if (after < before) {
        const auto difference = static_cast<unsigned long long>(before) -
                                static_cast<unsigned long long>(after);
        display_line(out, "所持金が " + currency_text(difference) + " 減少しています: " +
                     currency_text(before) + " → " + currency_text(after), "EVT_LOSE_MONEY");
    } else {
        line(out, "所持金に変更はありません。");
    }

    if (!debuglog->m_gossip.empty()) {
        line(out);
        toggles(out, debuglog->m_gossip, true, "ゴシップ", "EVT_GET_GOSSIP", "EVT_LOSE_GOSSIP");
        toggles(out, debuglog->m_gossip, false, "ゴシップ", "EVT_GET_GOSSIP", "EVT_LOSE_GOSSIP");
    }
    if (!debuglog->m_compstamp.empty()) {
        line(out);
        toggles(out, debuglog->m_compstamp, true, "終了印", "EVT_GET_COMPLETESTAMP", "EVT_LOSE_COMPLETESTAMP");
        toggles(out, debuglog->m_compstamp, false, "終了印", "EVT_GET_COMPLETESTAMP", "EVT_LOSE_COMPLETESTAMP");
    }

    append_card_lines(out, debuglog->m_got_card, true);
    append_card_lines(out, debuglog->m_lost_card, false);

    if (!debuglog->m_lost_player.empty() || !debuglog->m_player.empty()) line(out);
    for (const auto& [name, album] : debuglog->m_lost_player) {
        line(out, name + (album ? " は消去され、アルバムに掲載されました。" :
                               " は消去されました(アルバム不掲載)。"));
    }
    for (const auto& [name, got_coupons, lost_coupons] : debuglog->m_player) {
        bool changed = false;
        for (const auto& coupon : got_coupons) if (!std::get<0>(coupon).starts_with("＠")) changed = true;
        for (const auto& coupon : lost_coupons) if (!std::get<0>(coupon).starts_with("＠")) changed = true;
        if (!changed) {
            line(out, name + " の称号に変更はありません。");
            continue;
        }
        line(out, name + " の称号が以下のように変更されています。");
        for (const auto& [coupon, value] : got_coupons) {
            if (!coupon.starts_with("＠")) {
                const auto icon = value < 0 ? "COUPON_MINUS" :
                                  value == 0 ? "COUPON_ZERO" :
                                  value == 1 ? "COUPON_PLUS" : "COUPON";
                display_line(out, "    * 「" + coupon + "(" + (value >= 0 ? "+" : "") +
                             std::to_string(value) + ")」を獲得", icon);
            }
        }
        for (const auto& [coupon, value] : lost_coupons) {
            (void)value;
            if (!coupon.starts_with("＠"))
                display_line(out, "    * 「" + coupon + "」を喪失", "EVT_LOSE_COUPON");
        }
    }

    if (!debuglog->m_flags.empty() || !debuglog->m_steps.empty() ||
        !debuglog->m_variants.empty() || debuglog->m_is_removevariables) {
        line(out);
    }
    for (const auto& name : debuglog->m_variants)
        display_line(out, "コモン「" + name + "」の値を保存しました。", "VARIANT");
    for (const auto& name : debuglog->m_steps)
        display_line(out, "ステップ「" + name + "」の値を保存しました。", "STEP");
    for (const auto& name : debuglog->m_flags)
        display_line(out, "フラグ「" + name + "」の値を保存しました。", "FLAG");
    if (debuglog->m_is_removevariables)
        display_line(out, "保存されていた状態変数値を破棄しました。", "REMOVE_VARIABLES");
    if (!debuglog->m_jpdc_image.empty()) {
        line(out);
        for (const auto& fname : debuglog->m_jpdc_image) {
            display_line(out, "JPDCイメージ「" + fname + "」を保存しました。", "JPDCIMAGE");
        }
    }
    // 最後の通常行が既に持つ改行が、Pythonの plain_text.append("") と
    // join() が生成する末尾改行に相当する。

    String display_text;
    const auto title_end = m_plain_text.find('\n');
    const auto header_end = title_end == String::npos
        ? String::npos : m_plain_text.find('\n', title_end + 1);
    if (header_end != String::npos)
        display_text = m_plain_text.substr(header_end + 1);
    // ファイル用の plain_text は Python のリストの先頭の空要素を維
    // 持する。 RichTextCtrl は空の状態から始め、この要素を描画しない。
    if (!display_text.empty() && display_text.front() == '\n')
        display_text.erase(0, 1);
    std::vector<DebugLogDisplayLine> display_semantic_lines;
    if (semantic_lines.size() > 3) {
        display_semantic_lines.assign(semantic_lines.begin() + 3, semantic_lines.end());
    }
    m_display_lines = make_display_lines(display_text, &display_semantic_lines);
    if (friend_display_break) {
        m_display_lines.insert(m_display_lines.begin() + *friend_display_break,
                               DebugLogDisplayLine{});
        std::size_t text_offset = 0;
        for (std::size_t line_index = 0; line_index < *friend_display_break; ++line_index) {
            const auto newline = display_text.find('\n', text_offset);
            text_offset = newline == String::npos ? display_text.size() : newline + 1;
        }
        display_text.insert(text_offset, 1, '\n');
    }

    m_writetext = true;
    auto panel = std::make_shared<::cw::lib::Panel>();
    m_text = std::make_shared<::cw::util::CWPyRichTextCtrl>();
    m_text->initialize(panel, -1, display_text, {400, 380}, 0, false);
    auto ok = std::make_shared<::cw::lib::Window>();
    ok->title = "OK";
    ok->id = 1;
    ok->visible = true;
    m_okbtn = ::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(ok));
    _bind();
    _do_layout();
    visible = true;
    (void)::cw::dialog::show_common_modal_presentation(shared_from_this());
}

void DebugLogDialog::_bind()
{
    // 入力処理は共通 Presentation のコールバックが担当し、ホストは入力を受け渡すだけとする。
}

void DebugLogDialog::OnClose(std::shared_ptr<::cw::lib::Window>)
{
    if (m_presentation_finished) return;
    visible = false;
    m_presentation_finished = true;
    m_selecting=m_scroll_drag=m_popup=false;
    const auto* write = std::get_if<bool>(&m_writetext.storage());
    if (write && *write) {
        try {
            write_result(m_plain_text);
        } catch (...) {
            ::cw::util::print_ex(std::nullopt);
        }
    }
    dialog_presentation_complete();
}

void DebugLogDialog::write_result(const String& text)
{
    // Python と同様、UTF-8 で書き込み、改行は各環境のテキストモードに従って変換する。
    std::ofstream file("DebugInfo.txt",std::ios::out|std::ios::trunc);
    file.exceptions(std::ios::failbit|std::ios::badbit);
    file.write(text.data(),static_cast<std::streamsize>(text.size()));
    file.flush();
    file.close();
}

void DebugLogDialog::copy_result(const String& text)
{
    ::cw::dialog::presentation_set_clipboard_text(text);
}

std::shared_ptr<::cw::lib::Surface> DebugLogDialog::result_icon(const String& key)
{
    return common_debug_surface(key);
}

void DebugLogDialog::_do_layout()
{
    bounds = {0, 0, 440, 470};
    get_ui_backend()->layout(this);
}

void DebugLogDialog::ensure_layout()
{
    // Python の DebugLogDialog は、カードウィンドウの wins() ではなく ppis()
    // を使う。 このモデルはデバッガーの論理ピクセルで保持し、dialog_presentation()
    // で DebuggerDpi を一度だけ適用する。 別のプラットフォーム専用 UI 経路は追加しない。
    const auto s=[](Integer value){return value;};
    const auto font=::cw::lib::ui::configured_font("datadesc",13);
    m_viewport={s(10),s(8),s(410),s(421)};
    if (!m_rows.empty() && m_layout_lines==m_display_lines && m_layout_width==m_viewport.width &&
        font.face==m_body_font.face && font.pixels==m_body_font.pixels &&
        font.bold==m_body_font.bold && font.italic==m_body_font.italic) return;
    m_body_font=font;
    m_row_height=std::max<Integer>(s(16),font.pixels+s(3));
    m_layout_width=m_viewport.width;
    m_layout_lines=m_display_lines;
    m_rows.clear(); m_copy_text.clear();
    for (std::size_t line=0;line<m_display_lines.size();++line) {
        const auto& display=m_display_lines[line];
        const auto base=m_copy_text.size();
        m_copy_text+=display.text;
        if (line+1<m_display_lines.size()) m_copy_text+='\n';
        const Integer indent=std::clamp<Integer>(s(display.indent),0,m_viewport.width-s(20));
        std::vector<::cw::lib::ui::InlineLabelImage> images;
        const auto add_image=[&](const String& key,std::size_t offset) {
            auto size=::cw::lib::Size{s(16),s(16)};
            if (const auto image=::cw::lib::ui::resource_surface_for_logical(result_icon(key)))
                if (image->size.width>1 && image->size.height>1) size={s(image->size.width),s(image->size.height)};
            m_row_height=std::max<Integer>(m_row_height,size.height);
            images.push_back({offset,size,key});
        };
        for (const auto& key:display.icons) add_image(key,0);
        if (display.inline_icon) add_image(display.inline_icon->first,display.inline_icon->second);
        auto rows=::cw::lib::ui::wrap_label(display.text,font,m_viewport.width-indent,std::move(images));
        if (line+1<m_display_lines.size() && !rows.empty()) ++rows.back().next;
        for (auto& row:rows) m_rows.push_back({std::move(row),indent,base});
    }
    if (m_rows.empty()) m_rows.push_back({::cw::lib::ui::wrap_label("",font,m_viewport.width).front(),0,0});
    m_visible_rows=std::max<Integer>(1,m_viewport.height/m_row_height);
    m_max_scroll=std::max<Integer>(0,static_cast<Integer>(m_rows.size())-m_visible_rows);
    if (m_text) m_text->m_text_typed=m_copy_text;
    select_text(m_anchor,m_caret);
    scroll_to(m_presentation_scroll);
}

void DebugLogDialog::scroll_to(Integer first)
{
    m_presentation_scroll=std::clamp<Integer>(first,0,m_max_scroll);
    if (m_text) m_text->m_scroll_line_typed=m_presentation_scroll;
    request_dialog_presentation_refresh();
}

void DebugLogDialog::select_text(std::size_t anchor,std::size_t caret)
{
    const auto boundary=[&](std::size_t pos) {
        pos=std::min(pos,m_copy_text.size());
        while (pos && pos<m_copy_text.size() && (static_cast<unsigned char>(m_copy_text[pos])&0xc0U)==0x80U) --pos;
        return pos;
    };
    m_anchor=boundary(anchor); m_caret=boundary(caret);
    if (m_text) {
        m_text->m_selection_start_typed=static_cast<Integer>(std::min(m_anchor,m_caret));
        m_text->m_selection_end_typed=static_cast<Integer>(std::max(m_anchor,m_caret));
    }
    request_dialog_presentation_refresh();
}

std::size_t DebugLogDialog::text_hit(float x,float y) const
{
    if (m_rows.empty()) return 0;
    const auto index=std::clamp<Integer>(m_presentation_scroll+static_cast<Integer>(std::floor((y-m_viewport.y)/m_row_height)),
        0,static_cast<Integer>(m_rows.size())-1);
    const auto& display=m_rows[static_cast<std::size_t>(index)];
    const auto& row=display.label;
    x-=static_cast<float>(m_viewport.x+display.indent);
    for (std::size_t i=1;i<row.positions.size();++i)
        if (x<(row.positions[i-1]+row.positions[i])*0.5F) return display.base+row.offsets[i-1];
    return display.base+row.offsets.back();
}

void DebugLogDialog::copy_selection()
{
    if (m_anchor!=m_caret) copy_result(m_copy_text.substr(std::min(m_anchor,m_caret),
        std::max(m_anchor,m_caret)-std::min(m_anchor,m_caret)));
}

::cw::dialog::DialogPresentation DebugLogDialog::dialog_presentation()
{
    using Action=::cw::dialog::DialogPresentationAction;
    namespace retained_ui=::cw::debug::retained_presentation;
    const auto s=[](Integer n){return n;};
    ensure_layout();
    const Integer width=s(440),height=s(470);
    auto& retained=dialog_retained_surface();
    ::cw::lib::ui::ScaledRetainedCanvas canvas(
        retained,{width,height},retained_ui::dpi_scale(),{240,240,240,255});
    const auto first=std::min(m_anchor,m_caret),last=std::max(m_anchor,m_caret);
    const ::cw::lib::Rect body_region{0,0,width,438},footer_region{0,438,width,height-438};
    std::uint64_t body_signature{};
    retained_ui::mix(body_signature,static_cast<std::uint64_t>(m_presentation_scroll+1));
    retained_ui::mix(body_signature,static_cast<std::uint64_t>(first));
    retained_ui::mix(body_signature,static_cast<std::uint64_t>(last));
    retained_ui::mix(body_signature,static_cast<std::uint64_t>(m_popup));
    retained_ui::mix(body_signature,m_copy_text);retained_ui::mix(body_signature,m_body_font.face);
    retained_ui::mix(body_signature,static_cast<std::uint64_t>(m_body_font.pixels));
    std::uint64_t footer_signature=static_cast<std::uint64_t>(m_writetext.as_bool());
    retained_ui::mix(footer_signature,static_cast<std::uint64_t>(m_focus+1));
    retained_ui::mix(footer_signature,static_cast<std::uint64_t>(m_popup));
    const bool paint_body=canvas.component_changed("debug.log.body",body_region,body_signature);
    const bool paint_footer=canvas.component_changed("debug.log.footer",footer_region,footer_signature);
    ::cw::lib::Rect popup_region{};bool paint_popup=false;
    if(m_popup){const Integer popup_row_height=std::max(s(24),m_body_font.pixels+s(6)),popup_width=s(145);
        popup_region={std::clamp<Integer>(static_cast<Integer>(m_popup_x),0,width-popup_width),
            std::clamp<Integer>(static_cast<Integer>(m_popup_y),0,height-popup_row_height*2),
            popup_width,popup_row_height*2};
        std::uint64_t signature=static_cast<std::uint64_t>(m_popup_row+1);
        retained_ui::mix(signature,static_cast<std::uint64_t>(first));retained_ui::mix(signature,static_cast<std::uint64_t>(last));
        paint_popup=canvas.component_changed("debug.log.popup",popup_region,signature);
    }else (void)retained.remove_component("debug.log.popup");
    canvas.capture_base_and_restore();
    auto surface=std::make_shared<::cw::lib::Surface>();
    surface->size={width,height};
    surface->rgba.assign(static_cast<std::size_t>(width*height)*4U,255);
    ::cw::lib::ui::fill_rect(surface,{0,0,width,height},{240,240,240,255});
    ::cw::lib::ui::draw_sunken_border(surface,{s(5),s(5),s(430),s(430)});
    auto body=std::make_shared<::cw::lib::Surface>();
    body->size={m_viewport.width,m_viewport.height};
    body->rgba.assign(static_cast<std::size_t>(body->size.width*body->size.height)*4U,255);
    ::cw::imageretouch::Font font;
    font.initialize(m_body_font.face,-m_body_font.pixels,m_body_font.bold,m_body_font.italic);
    for (Integer i=m_presentation_scroll,y=0;i<static_cast<Integer>(m_rows.size()) && y<m_viewport.height;++i,y+=m_row_height) {
        const auto& display=m_rows[static_cast<std::size_t>(i)];
        const auto& row=display.label;
        for (std::size_t c=1;c<row.offsets.size();++c)
            if (display.base+row.offsets[c]>first && display.base+row.offsets[c-1]<last)
                ::cw::lib::ui::fill_rect(body,{display.indent+row.positions[c-1],y,
                    row.positions[c]-row.positions[c-1],m_row_height},{190,220,250,255});
        if (row.next>row.offsets.back() && first<=display.base+row.offsets.back() && last>display.base+row.offsets.back())
            ::cw::lib::ui::fill_rect(body,{display.indent+row.positions.back(),y,
                m_viewport.width-display.indent-row.positions.back(),m_row_height},{190,220,250,255});
        // テキストは埋め込み画像の位置だけで分割する。すべてのオフセットは
        // 、選択と当たり判定が使うものと同じ折り返し後の配置から求める。
        std::vector<std::size_t> splits{0,row.offsets.size()-1};
        for (const auto& image:row.images) {
            splits.push_back(static_cast<std::size_t>(std::lower_bound(row.offsets.begin(),row.offsets.end(),image.offset)-row.offsets.begin()));
            if (const auto bitmap=result_icon(image.key))
                ::cw::lib::ui::blend_surface(body,bitmap,display.indent+image.x,
                    y+std::max<Integer>(0,(m_row_height-image.size.height)/2),image.size.width,image.size.height);
        }
        std::sort(splits.begin(),splits.end());
        splits.erase(std::unique(splits.begin(),splits.end()),splits.end());
        for (std::size_t j=1;j<splits.size();++j) {
            const auto begin=splits[j-1],end=splits[j];
            if (end>=row.columns.size()) continue;
            const auto run=row.text.substr(row.columns[begin],row.columns[end]-row.columns[begin]);
            ::cw::lib::ui::blend_surface(body,font.render(run,true,{0,0,0}),
                display.indent+row.positions[begin],y+(m_row_height-m_body_font.pixels)/2);
        }
    }
    ::cw::lib::ui::blend_surface(surface,body,m_viewport.x,m_viewport.y);
    ::cw::dialog::DialogPresentation result;
    result.route="DEBUGLOG"; result.title=m_title;
    result.controls.push_back({Action::Auxiliary,200,static_cast<float>(m_viewport.x),static_cast<float>(m_viewport.y),
        static_cast<float>(m_viewport.width),static_cast<float>(m_viewport.height),true,true});
    if (m_max_scroll) {
        const ::cw::lib::Rect track{s(421),m_viewport.y,s(13),m_viewport.height};
        const Integer thumb=std::min(track.height,std::max(s(16),track.height*m_visible_rows/static_cast<Integer>(m_rows.size())));
        ::cw::lib::ui::fill_rect(surface,track,{225,225,225,255});
        ::cw::lib::ui::fill_rect(surface,{track.x,track.y+(track.height-thumb)*m_presentation_scroll/m_max_scroll,
            track.width,thumb},{145,145,145,255});
        result.controls.push_back({Action::Auxiliary,201,static_cast<float>(track.x),static_cast<float>(track.y),
            static_cast<float>(track.width),static_cast<float>(track.height),true,true});
    }
    const bool write=m_writetext.as_bool();
    const auto button_height=retained_ui::button_extent(23);
    const ::cw::lib::Rect check{s(5),s(442),s(327),button_height},ok{s(347),s(442),s(88),button_height};
    ::cw::lib::ui::fill_rect(surface,{check.x,check.y+s(4),s(14),s(14)},{255,255,255,255});
    ::cw::lib::ui::draw_sunken_border(surface,{check.x,check.y+s(4),s(14),s(14)});
    if (write) ::cw::lib::ui::draw_label(surface,"✓",check.x,check.y+s(2),s(14),true,{0,70,150});
    // フッターのテキストは、結果の表示領域とは独立してクリッピングする。
    auto caption=std::make_shared<::cw::lib::Surface>();
    caption->size={check.width-s(20),check.height};
    caption->rgba.assign(static_cast<std::size_t>(caption->size.width*caption->size.height)*4U,0);
    ::cw::lib::ui::draw_label(caption,"「DebugInfo.txt」に保存する",0,s(3),m_body_font.pixels,
        m_body_font.bold,{0,0,0},m_body_font.face);
    ::cw::lib::ui::blend_surface(surface,caption,check.x+s(20),check.y);
    ::cw::lib::ui::draw_button_face(surface,ok);
    const auto ok_width=::cw::lib::ui::label_width("OK",m_body_font.pixels,m_body_font.bold,m_body_font.face);
    ::cw::lib::ui::draw_label(surface,"OK",ok.x+(ok.width-ok_width)/2,ok.y+(ok.height-m_body_font.pixels)/2,
        m_body_font.pixels,m_body_font.bold,{0,0,0},m_body_font.face);
    for (const auto& pair:std::array<std::pair<Action,::cw::lib::Rect>,2>{{{Action::Toggle,check},{Action::Accept,ok}}})
        result.controls.push_back({pair.first,0,static_cast<float>(pair.second.x),static_cast<float>(pair.second.y),
            static_cast<float>(pair.second.width),static_cast<float>(pair.second.height),true});
    if (m_focus==1 || m_focus==2) {
        const auto rect=m_focus==1 ? check : ok;
        // キーボードフォーカスは押下状態ではない。Python/wx と同様、チェッ
        // クボックスと OK ボタンは通常の状態を保ち、フォーカスの目印だけを描く。
        const ::cw::lib::Rect focus{rect.x+s(2),rect.y+s(2),rect.width-s(4),rect.height-s(4)};
        const auto stroke=std::max<Integer>(1,s(1));
        const ::cw::lib::Color colour{0,120,215,255};
        ::cw::lib::ui::fill_rect(surface,{focus.x,focus.y,focus.width,stroke},colour);
        ::cw::lib::ui::fill_rect(surface,{focus.x,focus.y+focus.height-stroke,focus.width,stroke},colour);
        ::cw::lib::ui::fill_rect(surface,{focus.x,focus.y,stroke,focus.height},colour);
        ::cw::lib::ui::fill_rect(surface,{focus.x+focus.width-stroke,focus.y,stroke,focus.height},colour);
    }
    if (m_popup) {
        const Integer row_height=std::max(s(24),m_body_font.pixels+s(6)),menu_width=s(145);
        const Integer x=std::clamp<Integer>(static_cast<Integer>(m_popup_x),0,width-menu_width);
        const Integer y=std::clamp<Integer>(static_cast<Integer>(m_popup_y),0,height-row_height*2);
        result.controls.clear();
        result.controls.push_back({Action::Auxiliary,-100,0,0,static_cast<float>(width),static_cast<float>(height),true});
        for (int row=0;row<2;++row) {
            const bool enabled=row==0 ? first<last : !m_copy_text.empty();
            ::cw::lib::ui::fill_rect(surface,{x,y+row_height*row,menu_width,row_height},
                row==m_popup_row ? ::cw::lib::Color{210,228,250,255} : ::cw::lib::Color{250,250,250,255});
            ::cw::lib::ui::draw_label(surface,row==0 ? "コピー" : "すべて選択",x+s(5),y+row_height*row+s(4),
                m_body_font.pixels,m_body_font.bold,enabled ? std::tuple<Integer,Integer,Integer>{0,0,0}
                : std::tuple<Integer,Integer,Integer>{128,128,128},m_body_font.face);
            result.controls.push_back({Action::Select,row,static_cast<float>(x),static_cast<float>(y+row*row_height),
                static_cast<float>(menu_width),static_cast<float>(row_height),enabled});
        }
        ::cw::lib::ui::draw_sunken_border(surface,{x,y,menu_width,row_height*2});
    }
    retained_ui::composite_changed(canvas,surface,body_region,paint_body);
    retained_ui::composite_changed(canvas,surface,footer_region,paint_footer);
    retained_ui::composite_changed(canvas,surface,popup_region,paint_popup);
    retained_ui::finish(result,canvas);
    return result;
}

void DebugLogDialog::dialog_presentation_action(::cw::dialog::DialogPresentationAction action,int index)
{
    using Action=::cw::dialog::DialogPresentationAction;
    if (m_presentation_finished) return;
    ensure_layout();
    if (m_popup) {
        const int row=action==Action::Accept || action==Action::Activate ? m_popup_row : index;
        if (action==Action::Accept || action==Action::Activate || action==Action::Select) {
            if (row==0 && m_anchor==m_caret) return;
            m_popup=false;
            if (row==0) copy_selection();
            else if (row==1) select_text(0,m_copy_text.size());
        } else m_popup=false;
    } else if (action==Action::Accept || action==Action::Cancel) OnClose(nullptr);
    else if (action==Action::Toggle) {m_writetext=!m_writetext.as_bool();m_focus=1;}
    else if (action==Action::Previous) scroll_to(m_presentation_scroll-1);
    else if (action==Action::Next) scroll_to(m_presentation_scroll+1);
    request_dialog_presentation_refresh();
}

void DebugLogDialog::dialog_presentation_context(float x,float y)
{
    if (m_presentation_finished) return;
    const auto dpi=(std::max<::cw::lib::Real>)(
        static_cast<::cw::lib::Real>(::cw::g_dpi_level),::cw::lib::Real{1});
    x=static_cast<float>(x/dpi);y=static_cast<float>(y/dpi);
    ensure_layout();
    if (x<m_viewport.x || x>=m_viewport.x+m_viewport.width || y<m_viewport.y || y>=m_viewport.y+m_viewport.height) return;
    m_selecting=false; m_popup=true;
    m_popup_x=x; m_popup_y=y; m_popup_row=m_anchor==m_caret ? 1 : 0;
    request_dialog_presentation_refresh();
}

void DebugLogDialog::dialog_presentation_pointer(::cw::dialog::DialogPresentationAction action,
    int index,float x,float y,bool pressed)
{
    using Action=::cw::dialog::DialogPresentationAction;
    if (m_presentation_finished) return;
    if (m_popup || action!=Action::Auxiliary || (index!=200 && index!=201)) {
        if (!pressed) dialog_presentation_action(action,index);
        return;
    }
    // finalize_logical_presentation() はデバッガー DPI
    // 適用後の座標を公開するが、 下の当たり判定は意図的に共通の論理モデルの座標を使う。
    const auto dpi=(std::max<::cw::lib::Real>)(
        static_cast<::cw::lib::Real>(::cw::g_dpi_level),::cw::lib::Real{1});
    x=static_cast<float>(x/dpi);y=static_cast<float>(y/dpi);
    ensure_layout();
    m_focus=0;
    if (index==201) {
        const Integer thumb=std::min(m_viewport.height,std::max<Integer>(16,
            m_viewport.height*m_visible_rows/static_cast<Integer>(m_rows.size())));
        const Integer travel=m_viewport.height-thumb;
        if (!pressed) {m_scroll_drag=false;return;}
        if (!m_max_scroll || !travel) return;
        if (!m_scroll_drag) {
            const float top=static_cast<float>(m_viewport.y+travel*m_presentation_scroll/m_max_scroll);
            m_scroll_grab=y>=top && y<top+thumb ? y-top : thumb*0.5F;
            m_scroll_drag=true;
        }
        scroll_to(static_cast<Integer>(std::lround((y-m_viewport.y-m_scroll_grab)*m_max_scroll/travel)));
    } else {
        if (pressed && !m_selecting) {m_selecting=true;const auto p=text_hit(x,y);select_text(p,p);return;}
        if (!m_selecting) return;
        if (y<m_viewport.y) scroll_to(m_presentation_scroll-1);
        else if (y>=m_viewport.y+m_viewport.height) scroll_to(m_presentation_scroll+1);
        select_text(m_anchor,text_hit(x,y));
        if (!pressed) m_selecting=false;
    }
}

void DebugLogDialog::dialog_presentation_scroll(float x,float y,int lines)
{
    if (m_presentation_finished || !lines || m_popup) return;
    const auto dpi=(std::max<::cw::lib::Real>)(
        static_cast<::cw::lib::Real>(::cw::g_dpi_level),::cw::lib::Real{1});
    x=static_cast<float>(x/dpi);y=static_cast<float>(y/dpi);
    ensure_layout();
    if (x<m_viewport.x || x>=m_viewport.x+m_viewport.width || y<m_viewport.y || y>=m_viewport.y+m_viewport.height) return;
    scroll_to(m_presentation_scroll+lines);
}

void DebugLogDialog::dialog_presentation_enter(::cw::dialog::DialogPresentationModifiers)
{
    dialog_presentation_action(::cw::dialog::DialogPresentationAction::Accept,-1);
}

void DebugLogDialog::dialog_presentation_key(::cw::dialog::DialogPresentationKey key,bool pressed,
    ::cw::dialog::DialogPresentationModifiers modifiers)
{
    using Key=::cw::dialog::DialogPresentationKey;
    using Action=::cw::dialog::DialogPresentationAction;
    if (!pressed || m_presentation_finished) return;
    ensure_layout();
    const bool ctrl=(modifiers & ::cw::dialog::DialogPresentationModifierControl)!=0;
    const bool shift=(modifiers & ::cw::dialog::DialogPresentationModifierShift)!=0;
    if (m_popup) {
        if (key==Key::Enter || key==Key::Space) dialog_presentation_action(Action::Accept,-1);
        else if (key==Key::Up || key==Key::Down || key==Key::Home || key==Key::End)
            m_popup_row=m_anchor==m_caret || m_copy_text.empty() ? 1 : (key==Key::Home ? 0 : key==Key::End ? 1 : 1-m_popup_row);
        request_dialog_presentation_refresh();
        return;
    }
    if (key==Key::Enter || ((modifiers & ::cw::dialog::DialogPresentationModifierAlt) && key==Key::O))
        {OnClose(nullptr);return;}
    if (key==Key::Tab) {m_focus=(m_focus+(shift ? 2 : 1))%3;request_dialog_presentation_refresh();return;}
    if (key==Key::Space && m_focus==1) {dialog_presentation_action(Action::Toggle,0);return;}
    if (key==Key::Space && m_focus==2) {OnClose(nullptr);return;}
    if (ctrl && key==Key::A) {select_text(0,m_copy_text.size());return;}
    if (ctrl && key==Key::C) {copy_selection();return;}
    if (m_focus) return;
    Integer row=0;
    for (std::size_t i=1;i<m_rows.size();++i)
        if (m_rows[i].base+m_rows[i].label.offsets.front()<=m_caret) row=static_cast<Integer>(i);
    auto caret=m_caret;
    if (key==Key::Left) {
        if (!shift && m_anchor!=m_caret) caret=std::min(m_anchor,m_caret);
        else if (caret) {
            --caret;
            while (caret && (static_cast<unsigned char>(m_copy_text[caret])&0xc0U)==0x80U) --caret;
        }
    } else if (key==Key::Right) {
        if (!shift && m_anchor!=m_caret) caret=std::max(m_anchor,m_caret);
        else if (caret<m_copy_text.size()) {
            ++caret;
            while (caret<m_copy_text.size() && (static_cast<unsigned char>(m_copy_text[caret])&0xc0U)==0x80U) ++caret;
        }
    } else if (key==Key::Home) caret=ctrl ? 0 : m_rows[static_cast<std::size_t>(row)].base+m_rows[static_cast<std::size_t>(row)].label.offsets.front();
    else if (key==Key::End) caret=ctrl ? m_copy_text.size() : m_rows[static_cast<std::size_t>(row)].base+m_rows[static_cast<std::size_t>(row)].label.offsets.back();
    else if (key==Key::Up || key==Key::Down || key==Key::PageUp || key==Key::PageDown) {
        const auto& current=m_rows[static_cast<std::size_t>(row)];
        const auto column=std::lower_bound(current.label.offsets.begin(),current.label.offsets.end(),caret-current.base)-current.label.offsets.begin();
        const Integer x=current.indent+current.label.positions[std::min<std::size_t>(column,current.label.positions.size()-1)];
        const Integer step=key==Key::PageUp ? -m_visible_rows : key==Key::PageDown ? m_visible_rows : key==Key::Up ? -1 : 1;
        row=std::clamp<Integer>(row+step,0,static_cast<Integer>(m_rows.size())-1);
        const auto& target=m_rows[static_cast<std::size_t>(row)];
        caret=target.base+target.label.offsets.back();
        for (std::size_t i=1;i<target.label.positions.size();++i)
            if (x<target.indent+(target.label.positions[i-1]+target.label.positions[i])/2)
                {caret=target.base+target.label.offsets[i-1];break;}
    } else return;
    select_text(shift ? m_anchor : caret,caret);
    row=0;
    for (std::size_t i=1;i<m_rows.size();++i)
        if (m_rows[i].base+m_rows[i].label.offsets.front()<=caret) row=static_cast<Integer>(i);
    if (row<m_presentation_scroll) scroll_to(row);
    else if (row>=m_presentation_scroll+m_visible_rows) scroll_to(row-m_visible_rows+1);
}

bool DebugLogDialog::dialog_presentation_finished() const noexcept
{
    return m_presentation_finished;
}

void DebugLogDialog::dialog_presentation_complete()
{
    if (!m_presentation_finished || m_close_notified) return;
    m_close_notified=true;
    get_ui_backend()->close(this);
}

void DebugLogDialog___init___func()
{
    // Python の入れ子関数 func は、initialize() で CWPy::exec_func を通して実行される。
}

void main()
{
    // Python版も pass。ログ集計はDebugLogの明示的な呼び出しで行う。
}
}
