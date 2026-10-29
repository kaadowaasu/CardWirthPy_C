#include "cw/sprite/card.hpp"

#include "app/debug_control_server.hpp"
#include "cw/animation.hpp"
#include "cw/advlog.hpp"
#include "cw/content.hpp"
#include "cw/cw.hpp"
#include "cw/data.hpp"
#include "cw/deck.hpp"
#include "cw/event.hpp"
#include "cw/header.hpp"
#include "cw/image.hpp"
#include "cw/imageretouch.hpp"
#include "cw/lib/simd_image.hpp"
#include "cw/lib/python_float.hpp"
#include "cw/setting.hpp"
#include "cw/sprite/background.hpp"
#include "cw/sprite/message.hpp"
#include "cw/thread.hpp"
#include "cw/util.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>

namespace cw::sprite::card {

namespace {
using ::cw::lib::Integer;
using ::cw::lib::Real;
using ::cw::lib::Rect;
using ::cw::lib::String;
using ::cw::lib::Value;

Integer value_integer(const Value& value, Integer fallback = 0)
{
    if (const auto* integer = std::get_if<Integer>(&value.storage()))
        return *integer;
    if (const auto* real = std::get_if<::cw::lib::Real>(&value.storage()))
        return static_cast<Integer>(*real);
    if (const auto* boolean = std::get_if<bool>(&value.storage()))
        return *boolean ? 1 : 0;
    if (const auto* text = std::get_if<String>(&value.storage())) {
        try {
            std::size_t used = 0;
            const auto result = std::stoll(*text, &used);
            return used == text->size() ? static_cast<Integer>(result) : fallback;
        } catch (const std::exception&) {
            return fallback;
        }
    }
    return fallback;
}

String value_string(const Value& value, String fallback = {})
{
    if (const auto* text = std::get_if<String>(&value.storage()))
        return *text;
    return fallback;
}

std::shared_ptr<::cw::data::CWPyElement> value_element(const Value& value)
{
    const auto* object = std::get_if<::cw::lib::ObjectPtr>(&value.storage());
    if (!object || !*object)
        return {};
    return std::dynamic_pointer_cast<::cw::data::CWPyElement>(*object);
}

std::shared_ptr<::cw::data::CWPyElementTree> value_tree(const Value& value)
{
    const auto* object = std::get_if<::cw::lib::ObjectPtr>(&value.storage());
    if (!object || !*object)
        return {};
    if (auto tree = std::dynamic_pointer_cast<::cw::data::CWPyElementTree>(*object))
        return tree;
    if (auto element = std::dynamic_pointer_cast<::cw::data::CWPyElement>(*object))
        return std::make_shared<::cw::data::CWPyElementTree>(element, element->m_fpath);
    return {};
}

Integer scaled_coordinate(Integer value)
{
    // cw.s() の整数座標相当。SDL側のRect拡縮はここでは扱わない。
    const auto scale = ::cw::g_UP_SCR == 0.0 ? 1.0 : ::cw::g_UP_SCR;
    // Python の int(float) と同様、ゼロ方向へ切り捨てる。
    // 丸めると非整数倍率で負の位置や半ピクセルのカード位置が 1px ずれる。
    return static_cast<Integer>(static_cast<double>(value) * scale);
}

std::tuple<Integer, Integer> scaled_point(std::optional<std::tuple<Integer, Integer>> point)
{
    if (!point)
        return {0, 0};
    const auto [x, y] = *point;
    return {scaled_coordinate(x), scaled_coordinate(y)};
}

Integer read_int(const std::shared_ptr<::cw::data::CWPyElement>& element,
                 const String& path, const String& attr, Integer fallback)
{
    if (!element)
        return fallback;
    return element->getint(path, Value(attr), fallback);
}

bool read_bool(const std::shared_ptr<::cw::data::CWPyElement>& element,
               const String& path, const String& attr, bool fallback)
{
    if (!element)
        return fallback;
    return element->getbool(path, Value(attr), fallback);
}

Real read_real(const std::shared_ptr<::cw::data::CWPyElement>& element,
               const String& path, const String& attr, Real fallback)
{
    if (!element)
        return fallback;
    return element->getfloat(path, Value(attr), fallback);
}

String read_attr(const std::shared_ptr<::cw::data::CWPyElement>& element,
                 const String& path, const String& attr, const String& fallback)
{
    if (!element)
        return fallback;
    return element->getattr(path, attr, fallback);
}

String read_text(const std::shared_ptr<::cw::data::CWPyElement>& element,
                 const String& path, const String& fallback)
{
    if (!element)
        return fallback;
    return element->gettext(path, fallback);
}

Integer clamp_scale(Integer scale)
{
    return std::clamp(scale, Integer{0}, Integer{1000});
}

std::shared_ptr<::cw::image::ImageInfo> copy_imageinfo(
    const std::shared_ptr<::cw::image::ImageInfo>& info)
{
    if (!info)
        return {};
    auto copy = std::make_shared<::cw::image::ImageInfo>(*info);
    copy->m_base = info;
    return copy;
}

std::vector<std::shared_ptr<::cw::image::ImageInfo>> get_infos(
    const std::shared_ptr<::cw::data::CWPyElement>& element, bool arg_pcnumber = false)
{
    if (!element)
        return {};
    auto property = element->find("Property");
    if (!property || !*property)
        return {};
    return ::cw::image::get_imageinfos(*property, arg_pcnumber);
}

// 通常は image::get_imageinfos() を
// 使い、画像情報が空の場合だけ同じ XML 構造からここで復
// 元する。CardImage への接続は描画層に委譲する。
std::vector<std::shared_ptr<::cw::image::ImageInfo>> player_imageinfos(
    const std::shared_ptr<::cw::data::CWPyElement>& element, bool arg_pcnumber = false)
{
    auto result = get_infos(element, arg_pcnumber);
    if (result.empty() && element)
        result = ::cw::image::get_imageinfos(element, arg_pcnumber);
    if (!result.empty() || !element)
        return result;

    auto add_path = [&result, arg_pcnumber](const std::shared_ptr<::cw::data::CWPyElement>& node,
                                            const String& text, const String& postype) {
        if (text.empty())
            return;
        auto info = std::make_shared<::cw::image::ImageInfo>();
        const auto pcnumber = arg_pcnumber ? read_int(node, ".", "pcnumber", 0) : Integer{0};
        info->initialize(text, pcnumber, std::nullopt, postype.empty() ? String("Default") : postype,
                         std::nullopt);
        result.push_back(std::move(info));
        (void)node;
    };
    auto add_node = [&add_path](const std::shared_ptr<::cw::data::CWPyElement>& node) {
        if (!node)
            return;
        add_path(node, node->get_text(), node->get("positiontype", "Default"));
    };
    if (element->m_tag == "ImagePaths") {
        for (const auto& child : element->m_children)
            if (child && child->m_tag == "ImagePath")
                add_node(child);
        return result;
    }
    const auto direct_path = element->get("path", "");
    add_path(element, direct_path, element->get("positiontype", "Default"));
    if (const auto path = element->find("ImagePath"); path && *path)
        add_path(*path, (*path)->get_text(), (*path)->get("positiontype", "Default"));
    if (const auto paths = element->find("ImagePaths"); paths && *paths) {
        for (const auto& child : (*paths)->m_children)
            if (child && child->m_tag == "ImagePath")
                add_node(child);
    }
    return result;
}

void load_player_character_state(::cw::character::Player& player,
                                 const std::shared_ptr<::cw::data::CWPyElementTree>& data)
{
    const auto root = data ? data->getroot() : nullptr;
    player.m_data = data ? Value(std::static_pointer_cast<::cw::lib::Object>(data)) : Value{};
    player.m_name = read_text(root, "Property/Name", {});
    player.m_level = std::clamp<Integer>(read_int(root, "Property/Level", "", 1), 1, 65536);
    player.m_life = std::max<Integer>(0, read_int(root, "Property/Life", "", 0));
    player.m_maxlife = std::max<Integer>(1, read_int(root, "Property/Life", "max", 1));
    player.m_life = std::min(player.m_life, player.m_maxlife);
    player.m_mentality = read_text(root, "Property/Status/Mentality", "Normal");
    player.m_mentality_dur = std::clamp<Integer>(read_int(root, "Property/Status/Mentality", "duration", 0), 0, 999);
    if (player.m_mentality == "Normal" || player.m_mentality_dur == 0) {
        player.m_mentality = "Normal";
        player.m_mentality_dur = 0;
    }
    player.m_paralyze = std::clamp<Integer>(read_int(root, "Property/Status/Paralyze", "", 0), 0, 40);
    player.m_poison = std::clamp<Integer>(read_int(root, "Property/Status/Poison", "", 0), 0, 40);
    player.m_bind = std::clamp<Integer>(read_int(root, "Property/Status/Bind", "duration", 0), 0, 999);
    player.m_silence = std::clamp<Integer>(read_int(root, "Property/Status/Silence", "duration", 0), 0, 999);
    player.m_faceup = std::clamp<Integer>(read_int(root, "Property/Status/FaceUp", "duration", 0), 0, 999);
    player.m_antimagic = std::clamp<Integer>(read_int(root, "Property/Status/AntiMagic", "duration", 0), 0, 999);

    auto read_enhance = [&root, &player](const String& name, Integer& value, Integer& duration) {
        const auto path = String("Property/Enhance/") + name;
        value = std::clamp<Integer>(read_int(root, path, "", 0), -10, 10);
        duration = std::clamp<Integer>(read_int(root, path, "duration", 0), 0, 999);
        if (value == 0 || duration == 0) {
            value = 0;
            duration = 0;
        }
    };
    read_enhance("Action", player.m_enhance_act, player.m_enhance_act_dur);
    read_enhance("Avoid", player.m_enhance_avo, player.m_enhance_avo_dur);
    read_enhance("Resist", player.m_enhance_res, player.m_enhance_res_dur);
    read_enhance("Defense", player.m_enhance_def, player.m_enhance_def_dur);
    player.m__coeff_level = read_real(root, "Property/Coefficient", "level", 1.0);
    player.m__coeff_ep = read_int(root, "Property/Coefficient", "ep", 10);
    player.m_reversed = false;
    player.m_coupons.clear();
    if (const auto coupons = root ? root->find("Property/Coupons") : std::nullopt; coupons && *coupons) {
        for (const auto& coupon : (*coupons)->m_children) {
            if (!coupon)
                continue;
            const auto name = coupon->get_text();
            if (name.empty() || name == "＠効果対象" || name == "イベント対象" || name == "使用者")
                continue;
            player.m_coupons[name] = Value(read_int(coupon, ".", "value", 0));
            if (name == "：Ｒ")
                player.m_reversed = true;
        }
    }
    player.m__vanished = false;
    player.m_actionend = true;
    player.m_is_entered_battle = true;
    player.m_hold_all = {
        read_bool(root, "SkillCards", "hold_all", false),
        read_bool(root, "ItemCards", "hold_all", false),
        read_bool(root, "BeastCards", "hold_all", false)};
    // Deck.initialize() には共有された Character
    // 所有者が必要である。 カード実行処理が実際のデッキを供給す
    // るまで、空の型付きデッキを持ち、手札状態の判定を可能にする。
    player.m_deck = std::make_shared<::cw::deck::Deck>();
}

bool player_has_coupon(const ::cw::character::Player& player, const String& coupon)
{
    return player.m_coupons.find(coupon) != player.m_coupons.end();
}

std::map<String, Integer> player_special_coupons(const ::cw::character::Player& player)
{
    std::map<String, Integer> result;
    for (const auto& [name, value] : player.m_coupons) {
        if (name.rfind("＠", 0) == 0)
            result[name] = value_integer(value);
    }
    return result;
}

std::shared_ptr<::cw::data::SystemData> current_sdata();

std::shared_ptr<::cw::sprite::card::PlayerCard> find_player_card(
    ::cw::sprite::card::PlayerCard& card)
{
    if (!::cw::g_cwpy)
        return {};
    for (const auto& candidate : ::cw::g_cwpy->m_pcards)
        if (candidate && candidate.get() == &card)
            return candidate;
    return {};
}

std::shared_ptr<::cw::sprite::card::EnemyCard> find_enemy_card(
    ::cw::sprite::card::EnemyCard& card)
{
    if (!::cw::g_cwpy)
        return {};
    // EnemyCard は CWPy の m_file_updates/m_animations から共有所有者を
    // 復元できる。生成直後は非所有のプロキシを登録し、所有者登録後に
    // m_mcards の Value が実体を保持する。
    const auto find = [&card](const auto& sprites) -> std::shared_ptr<::cw::sprite::card::EnemyCard> {
        for (const auto& candidate : sprites) {
            if (!candidate || dynamic_cast<::cw::sprite::card::EnemyCard*>(candidate.get()) != &card)
                continue;
            return std::dynamic_pointer_cast<::cw::sprite::card::EnemyCard>(candidate);
        }
        return {};
    };
    if (auto result = find(::cw::g_cwpy->m_file_updates); result)
        return result;
    if (auto result = find(::cw::g_cwpy->m_animations); result)
        return result;
    for (const auto& value : ::cw::g_cwpy->m_mcards) {
        const auto* object = std::get_if<::cw::lib::ObjectPtr>(&value.storage());
        if (!object || !*object)
            continue;
        if (auto result = std::dynamic_pointer_cast<::cw::sprite::card::EnemyCard>(*object);
            result && result.get() == &card)
            return result;
    }
    return {};
}

std::shared_ptr<::cw::sprite::card::FriendCard> find_friend_card(
    ::cw::sprite::card::FriendCard& card)
{
    const auto sdata = current_sdata();
    if (!sdata)
        return {};
    for (const auto& candidate : sdata->m_friendcards)
        if (candidate && candidate.get() == &card)
            return candidate;
    return {};
}

// 互換用の別名を明示的に識別する。 スタック所有カードの再初期化では、weak_from_this
// も何もしない制御ブロックを参照する場合がある。
struct BorrowedCharacterDeleter {
    void operator()(::cw::character::Character*) const noexcept {}
};

std::shared_ptr<::cw::character::Character> borrowed_friend_character(
    ::cw::sprite::card::FriendCard& card)
{
    auto& character = static_cast<::cw::character::Character&>(card);
    // CardHeader の所有者は弱参照なので、FriendCard の
    // 実際の制御ブロックを再利用する。 このヘルパー終了時に無効になる所有
    // 者を Deck::set()/draw() が公開しないようにする。
    if (auto owned = character.weak_from_this().lock()) return owned;
    return std::shared_ptr<::cw::character::Character>(&character,
                                                       BorrowedCharacterDeleter{});
}

std::shared_ptr<::cw::sprite::card::MenuCard> find_menu_card(
    ::cw::sprite::card::MenuCard& card)
{
    if (!::cw::g_cwpy)
        return {};
    const auto find = [&card](const auto& sprites) -> std::shared_ptr<::cw::sprite::card::MenuCard> {
        for (const auto& candidate : sprites) {
            if (!candidate || dynamic_cast<::cw::sprite::card::MenuCard*>(candidate.get()) != &card)
                continue;
            return std::dynamic_pointer_cast<::cw::sprite::card::MenuCard>(candidate);
        }
        return {};
    };
    if (auto result = find(::cw::g_cwpy->m_file_updates); result)
        return result;
    if (auto result = find(::cw::g_cwpy->m_animations); result)
        return result;
    for (const auto& value : ::cw::g_cwpy->m_mcards) {
        const auto* object = std::get_if<::cw::lib::ObjectPtr>(&value.storage());
        if (!object || !*object)
            continue;
        if (auto result = std::dynamic_pointer_cast<::cw::sprite::card::MenuCard>(*object);
            result && result.get() == &card)
            return result;
    }
    return {};
}

bool is_special_area()
{
    return ::cw::g_cwpy &&
        (::cw::g_cwpy->m_areaid == value_integer(std::get<0>(::cw::g_AREAS_SP)) ||
         ::cw::g_cwpy->m_areaid == value_integer(std::get<1>(::cw::g_AREAS_SP)) ||
         ::cw::g_cwpy->m_areaid == value_integer(std::get<2>(::cw::g_AREAS_SP)) ||
         ::cw::g_cwpy->m_areaid == value_integer(std::get<3>(::cw::g_AREAS_SP)) ||
         ::cw::g_cwpy->m_areaid == value_integer(std::get<4>(::cw::g_AREAS_SP)));
}

Integer compatible_menu_layer(Integer layer)
{
    layer = std::clamp<Integer>(layer, -1, ::cw::g_LAYER_MAX);
    if (layer >= 0)
        return layer;
    bool old_zindex = false;
    const auto sdata = current_sdata();
    if (::cw::g_cwpy && sdata && ::cw::g_cwpy->m_sct) {
        old_zindex = ::cw::g_cwpy->m_sct->zindexmode(
            sdata->get_versionhint(::cw::g_HINT_SCENARIO)) ||
            ::cw::g_cwpy->m_sct->zindexmode(
                sdata->get_versionhint(::cw::g_HINT_AREA));
    }
    return old_zindex ? ::cw::g_LAYER_MCARDS_120 : ::cw::g_LAYER_MCARDS;
}

class CardInputGuard final {
public:
    explicit CardInputGuard(std::shared_ptr<CWPyCard> card)
        : card_(std::move(card)), acquired_(card_ && !card_->m_input_dispatching)
    {
        if (acquired_) card_->m_input_dispatching = true;
    }
    ~CardInputGuard() { if (acquired_) card_->m_input_dispatching = false; }
    explicit operator bool() const noexcept { return acquired_; }

private:
    // animate_sprite() は外側のハンドラーが実
    // 行中でも入力を処理する。 再入を防ぐだけでなく実際の所有
    // 者を保持し、入れ子の処理で戦闘・イベント遷移がカードを実
    // 行中の集合から外しても、 この保護の終了まで生存させる。
    std::shared_ptr<CWPyCard> card_;
    bool acquired_{};
};

class TargetSelectionDispatchGuard final {
public:
    explicit TargetSelectionDispatchGuard(::cw::thread::CWPy* runtime, bool required)
        : runtime_(required ? runtime : nullptr)
    {
        if (!runtime_) {
            acquired_ = true;
            return;
        }
        bool expected = false;
        acquired_ = runtime_->m_target_selection_dispatching.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel);
    }
    ~TargetSelectionDispatchGuard()
    {
        if (runtime_ && acquired_)
            runtime_->m_target_selection_dispatching.store(false, std::memory_order_release);
    }
    explicit operator bool() const noexcept { return acquired_; }

private:
    ::cw::thread::CWPy* runtime_{};
    bool acquired_{};
};

class CardClickAnimationGuard final {
public:
    explicit CardClickAnimationGuard(CWPyCard& card) : card_(card) {
        card_.m_click_animating = true;
    }
    ~CardClickAnimationGuard() { card_.m_click_animating = false; }

private:
    CWPyCard& card_;
};

bool start_card_click(const std::shared_ptr<CWPyCard>& owner,
                      bool battlespeed = false)
{
    // アニメーション中の入力処理で、カードが現在の集合から除かれる場合がある。
    // 借用の shared_ptr では生存を保てないため、所有権を必須にする。
    if (!owner) return false;
    if (owner->m_click_animating || owner->m_status == "click" || owner->m__clicking)
        return false;
    CardClickAnimationGuard guard(*owner);
    owner->m_frame = 0;
    owner->m__clicking = false;
    if (::cw::g_cwpy) {
        // animate_sprite が返すのは実行したかで
        // はなく、省略されたかである。 正常終了したクリックは
        // false なので、失敗とみなして再開しない。 再
        // 開すると縮小・反転フレームと古い背景が画面に残る。
        (void)::cw::animation::animate_sprite(owner, "click", true, false, false, battlespeed);
    } else {
        owner->m_old_status = owner->m_status;
        owner->m_status = "click";
        owner->m_frame = 0;
    }
    return true;
}

std::shared_ptr<::cw::character::Character> borrowed_enemy_character(
    ::cw::sprite::card::EnemyCard& card)
{
    auto& character = static_cast<::cw::character::Character&>(card);
    // CharacterCardImage と Deck はどちらも所有者を弱参照する。 一
    // 時的な非所有の別名ではなく、EnemyCard の実際の所有制御ブロックが必要である。
    if (auto owned = character.weak_from_this().lock()) return owned;
    return std::shared_ptr<::cw::character::Character>(&character,
                                                       BorrowedCharacterDeleter{});
}

bool value_points_to_enemy(const Value& value, const ::cw::sprite::card::EnemyCard& card)
{
    const auto* object = std::get_if<::cw::lib::ObjectPtr>(&value.storage());
    if (!object || !*object)
        return false;
    return dynamic_cast<const ::cw::sprite::card::EnemyCard*>(object->get()) == &card;
}

bool area_in_trade(Integer area)
{
    return area == -1 || area == -2 || area == -5;
}

bool start_player_animation(::cw::sprite::card::PlayerCard& card, const String& type)
{
    // 状態遷移はアニメーションのバックエンドが管理し、未登録の場合だけ同じ遷移をここで行う。
    const auto shared = find_player_card(card);
    if (!shared) return false;
    const auto owner = std::static_pointer_cast<::cw::sprite::card::CWPyCard>(shared);
    const bool click = type == "click";
    if (click && (owner->m_click_animating || owner->m_status == "click" ||
                  owner->m__clicking)) return false;
    // PC専用の表示設定。クリックによる処理と効果音は維持する。
    if (click && ::cw::g_cwpy && ::cw::g_cwpy->m_setting &&
        ::cw::g_cwpy->m_setting->m_skip_pc_click_motion) return true;
    std::optional<CardClickAnimationGuard> click_guard;
    if (click) click_guard.emplace(*owner);
    if (::cw::g_cwpy) {
        // animate_sprite() が返すのはアニメー
        // ションを省略したかであり、実行したかではない。 正常
        // 終了の false を受けても、ここで再開しない。
        (void)::cw::animation::animate_sprite(owner, type, true, false, false, card.m_battlespeed);
    } else {
        if (type == "levelup" || type == "click" || type == "hide" || type == "deal") {
            card.m_old_status = card.CWPyCard::m_status;
            card.CWPyCard::m_status = type;
            card.m_frame = 0;
        }
    }
    return true;
}

String player_message(const String& key, const String& fallback)
{
    if (::cw::g_cwpy && ::cw::g_cwpy->m_setting && ::cw::g_cwpy->m_setting->m_msgs)
        return ::cw::g_cwpy->m_setting->m_msgs->get(key, fallback);
    return fallback;
}

String message_with_name(String text, const String& name)
{
    const auto pos = text.find("%s");
    if (pos != String::npos)
        text.replace(pos, 2, name);
    return text;
}

void set_card_geometry(CWPyCard& card, Integer width, Integer height)
{
    const auto scale = card.m_scale <= 0 ? Integer{100} : card.m_scale;
    card.m__rect.width = width * scale / 100;
    card.m__rect.height = height * scale / 100;
    card.m_rect = card.m__rect;
}

std::shared_ptr<::cw::lib::Surface> empty_surface(Integer width = 0, Integer height = 0)
{
    auto result = std::make_shared<::cw::lib::Surface>();
    result->size = {std::max<Integer>(0, width), std::max<Integer>(0, height)};
    if (result->size.width > 0 && result->size.height > 0)
        result->rgba.resize(static_cast<std::size_t>(result->size.width * result->size.height * 4), 0);
    return result;
}

std::shared_ptr<::cw::lib::Surface> copy_surface(const std::shared_ptr<::cw::lib::Surface>& source)
{
    if (!source)
        return empty_surface();
    return std::make_shared<::cw::lib::Surface>(*source);
}

std::shared_ptr<::cw::lib::Surface> scale_surface(const std::shared_ptr<::cw::lib::Surface>& source,
                                                   Integer width, Integer height)
{
    width = std::max<Integer>(0, width);
    height = std::max<Integer>(0, height);
    if (!source) return empty_surface(width, height);
    // Py の pygame.transform.scale と同様、入
    // 力の不透明度・透過色・ピクセル形式を維持する。 共通の最近傍処理で
    // 、配布・非表示・クリック・拡大フレームもピクセルとこれらの属性を保
    // つ。 ネイティブの入力なし・非負寸法の代替処理はこの境界に残す。
    return ::cw::image::scale_nearest(source, {width, height});
}

std::shared_ptr<::cw::lib::Surface> negative_surface(const std::shared_ptr<::cw::lib::Surface>& source)
{
#if defined(CARDWIRTH_LOG_ENABLED)
    const auto started = std::chrono::steady_clock::now();
#endif
    auto result = copy_surface(source);
    if (result->rgba.size() < static_cast<std::size_t>(result->size.width * result->size.height * 4)) {
#if defined(CARDWIRTH_LOG_ENABLED)
        ::cardwirth::debug::log_slow(
            "hover.negative_surface", std::chrono::steady_clock::now() - started,
            "width=" + std::to_string(result->size.width) +
            " height=" + std::to_string(result->size.height) + " bytes=" +
            std::to_string(result->rgba.size()));
#endif
        return result;
    }
    for (std::size_t i = 0; i + 3 < result->rgba.size(); i += 4) {
        result->rgba[i] = static_cast<std::uint8_t>(255 - result->rgba[i]);
        result->rgba[i + 1] = static_cast<std::uint8_t>(255 - result->rgba[i + 1]);
        result->rgba[i + 2] = static_cast<std::uint8_t>(255 - result->rgba[i + 2]);
    }
#if defined(CARDWIRTH_LOG_ENABLED)
    ::cardwirth::debug::log_slow(
        "hover.negative_surface", std::chrono::steady_clock::now() - started,
        "width=" + std::to_string(result->size.width) +
        " height=" + std::to_string(result->size.height) + " bytes=" +
        std::to_string(result->rgba.size()));
#endif
    return result;
}

void set_surface_alpha(const std::shared_ptr<::cw::lib::Surface>& surface,
                       std::optional<Integer> alpha)
{
    if (!surface || !alpha)
        return;
    const auto required = static_cast<std::size_t>(std::max<Integer>(0, surface->size.width)) *
        static_cast<std::size_t>(std::max<Integer>(0, surface->size.height)) * 4;
    if (surface->rgba.size() < required)
        surface->rgba.resize(required, 0);
    const auto value = static_cast<unsigned>(std::clamp<Integer>(*alpha, 0, 255));
    // pygame の Surface.set_alpha() は画像全体
    // の不透明度であり、PNG のピクセルアルファを置き換えるものではない
    // 。 全体値を各ピクセルへ組み合わせ、透明なカードの角や絵を維持する。
    ::cw::lib::simd_image::multiply_alpha(
        surface->rgba.data(), surface->rgba.size() / 4U, static_cast<std::uint8_t>(value));
}

std::optional<Integer> optional_integer(const Value& value)
{
    if (value.is_none())
        return std::nullopt;
    if (const auto* integer = std::get_if<Integer>(&value.storage()))
        return *integer;
    if (const auto* real = std::get_if<::cw::lib::Real>(&value.storage()))
        return static_cast<Integer>(*real);
    if (const auto* boolean = std::get_if<bool>(&value.storage()))
        return *boolean ? 1 : 0;
    return std::nullopt;
}

std::shared_ptr<::cw::data::SystemData> current_sdata()
{
    if (!::cw::g_cwpy)
        return {};
    const auto* object = std::get_if<::cw::lib::ObjectPtr>(&::cw::g_cwpy->m_sdata.storage());
    if (!object || !*object)
        return {};
    return std::dynamic_pointer_cast<::cw::data::SystemData>(*object);
}

std::shared_ptr<::cw::setting::Setting> current_setting()
{
    return ::cw::g_cwpy ? ::cw::g_cwpy->m_setting : nullptr;
}

std::vector<Integer> scales_from_value(const Value& value)
{
    const auto* object = std::get_if<::cw::lib::ObjectPtr>(&value.storage());
    if (!object || !*object)
        return {};
    const auto sequence = std::dynamic_pointer_cast<::cw::data::VariantSequence>(*object);
    if (!sequence)
        return {};
    std::vector<Integer> result;
    result.reserve(sequence->m_values.size());
    for (const auto& item : sequence->m_values)
        result.push_back(value_integer(item));
    return result;
}

bool is_selected(const CWPyCard& card)
{
    return ::cw::g_cwpy && ::cw::g_cwpy->m_selection &&
        (*::cw::g_cwpy->m_selection).get() == &card;
}

// 互換 SpriteGroup は共通の Sprite プロキシーを保持する。CWPySprite
// はモデル専用であり、描画側の Sprite 型を継承しない。 カードご
// とに一つのプロキシーを持ち、状態更新ごとに画像・矩形を同期して、 ソフト
// ウェアと SDL の描画側が同じカードレイヤーを参照できるようにする。
struct CardRenderProxy : ::cw::lib::Sprite {
    std::shared_ptr<::cw::lib::Surface> source_image;
    std::optional<Integer> alpha;
};
struct CardLayerRegistry {
    std::unordered_map<const CWPyCard*, std::shared_ptr<CardRenderProxy>> card_proxies;
    std::unordered_map<const ::cw::lib::Sprite*, CWPyCard*> card_proxy_owners;
    std::unordered_map<const ::cw::lib::Sprite*, Integer> card_proxy_layers;
    std::unordered_map<const CWPyCard*,
                       std::shared_ptr<::cw::sprite::background::LifeBar>> card_lifebars;
    std::weak_ptr<::cw::lib::SpriteGroup> registered_group;
};

CardLayerRegistry& card_layer_registry()
{
    // EventHandlerRuntimeContext は静的破棄までカ
    // ードを所有する場合がある。 通常の関数内 static の登録表が先
    // に破棄されると、CWPyCard のデストラクターが解放済みの unordered_map
    // に触れる。 小さな登録表はプロセス終了まで保持し、グルー
    // プは弱参照にして実際の描画オブジェクトは解放可能にする。
    static auto* registry = new CardLayerRegistry();
    return *registry;
}

auto& g_card_proxies = card_layer_registry().card_proxies;
auto& g_card_proxy_owners = card_layer_registry().card_proxy_owners;
auto& g_card_proxy_layers = card_layer_registry().card_proxy_layers;
auto& g_card_lifebars = card_layer_registry().card_lifebars;

void sync_card_proxy(const CWPyCard& card)
{
    const auto it = g_card_proxies.find(&card);
    if (it == g_card_proxies.end() || !it->second)
        return;
    auto& proxy = *it->second;
    // アルファ描画はカードが変わらなくても一時 Surface を作る。 そのポ
    // インターではなく入力状態を比較し、待機中の毎フレームの再描画を避ける。
    // Python の実体スプライト画像には、このプロキシー追跡は不要である。
    const bool image_changed = proxy.source_image != card.m_image || proxy.alpha != card.m_alpha;
    if (image_changed && proxy.visible)
        ::cw::sprite::base::invalidate_sprite_rect(proxy.rect);
    ::cw::sprite::base::sync_sprite_proxy(proxy, card.get_render_image(), card.m_rect,
                                         card.m_status != "hidden", false);
    proxy.source_image = card.m_image;
    proxy.alpha = card.m_alpha;
    if (image_changed && proxy.visible)
        ::cw::sprite::base::invalidate_sprite_rect(proxy.rect);
}

void register_card_layer(CWPyCard& card, std::optional<Integer> layer = std::nullopt)
{
    if (!::cw::g_cwpy)
        return;
    const auto* object = std::get_if<::cw::lib::ObjectPtr>(&::cw::g_cwpy->m_cardgrp.storage());
    if (!object || !*object)
        return;
    const auto group = std::dynamic_pointer_cast<::cw::lib::SpriteGroup>(*object);
    if (!group)
        return;
    card_layer_registry().registered_group = group;
    auto& proxy = g_card_proxies[&card];
    if (!proxy)
        proxy = std::make_shared<CardRenderProxy>();
    g_card_proxy_owners[proxy.get()] = &card;
    sync_card_proxy(card);
    const bool newly_registered =
        std::find(group->sprites.begin(), group->sprites.end(), proxy) == group->sprites.end();
    const auto previous_layer_it = g_card_proxy_layers.find(proxy.get());
    const auto previous_layer = previous_layer_it == g_card_proxy_layers.end()
        ? std::optional<::cw::lib::Integer>{}
        : std::optional<::cw::lib::Integer>{previous_layer_it->second};
    try {
        // Python と同様、FriendCard は通常カードの tlayer
        // ではなく、キャンプ・前面対象用の tlayer_t で cardgrp
        // に入れる。 互換グループは当たり判定と update()
        // 用のプロキシーを持つため、tlayer で並べると NPC
        // がメニューカードの後ろへ入り、対象を選べなくなる場合がある。
        const auto* friend_card = dynamic_cast<const FriendCard*>(&card);
        g_card_proxy_layers[proxy.get()] = layer ? *layer : ::cw::layer_val(
            friend_card ? friend_card->m_tlayer_t : card.m_tlayer);
    } catch (const std::exception&) {
        g_card_proxy_layers[proxy.get()] = 0;
    }
    if (newly_registered)
        ::cw::sprite::base::insert_sprite_group_by_layer(group, proxy);
    else if (!previous_layer || *previous_layer != g_card_proxy_layers[proxy.get()])
        ::cw::sprite::base::reposition_sprite_group_by_layer(group, proxy);
}

void unregister_card_layer(CWPyCard& card)
{
    // カードのプロキシーがなくても、生命力バーの借用所有者を解放する。
    clear_lifebars(&card);
    const auto proxy_it = g_card_proxies.find(&card);
    if (proxy_it == g_card_proxies.end()) return;
    if (const auto group = card_layer_registry().registered_group.lock())
        ::cw::sprite::base::remove_sprite_from_group(group, proxy_it->second);
    g_card_proxy_layers.erase(proxy_it->second.get());
    g_card_proxy_owners.erase(proxy_it->second.get());
    g_card_proxies.erase(proxy_it);
}

void sync_lifebar_proxy(const std::shared_ptr<::cw::sprite::background::LifeBar>& bar)
{
    if (!bar || !::cw::g_cwpy) return;
    const auto* object = std::get_if<::cw::lib::ObjectPtr>(&::cw::g_cwpy->m_cardgrp.storage());
    if (object && *object) {
        if (const auto backend = ::cw::sprite::background::background_ui_backend()) {
            // 初期化とアニメーションで同じ背景プロキシーを更新し、 Python
            // のグループ所有の単一 Sprite に合わせる。
            const auto& layer = ::cw::g_LAYER_FRONT_LIFEBAR;
            backend->add_sprite(*object, bar, ::cw::layer_val({
                value_integer(std::get<0>(layer)), std::get<1>(layer),
                std::get<2>(layer), std::get<3>(layer)}));
        }
    }
}

void register_lifebar_layer(const std::shared_ptr<::cw::sprite::background::LifeBar>& bar)
{
    sync_lifebar_proxy(bar);
}

std::shared_ptr<::cw::sprite::background::LifeBar> card_lifebar(CWPyCard& card, bool create)
{
    // Python と同様、Player/Enemy/Friend のキャラクターを対象にし、MenuCard は含めない。
    if (!dynamic_cast<::cw::character::Character*>(&card))
        return {};
    const auto it = g_card_lifebars.find(&card);
    if (it != g_card_lifebars.end() || !create) {
        if (it != g_card_lifebars.end())
            register_lifebar_layer(it->second);
        return it == g_card_lifebars.end() ? nullptr : it->second;
    }
    auto bar = std::make_shared<::cw::sprite::background::LifeBar>();
    // ネイティブの登録表がバーを所有し、カードへの参照は借用する。 clear_inusecardimg
    // とカード破棄の両方で解除する。Python は双方をグループ参照
    // で所有するが、 ネイティブでは登録表とカードの循環所有を避ける。
    bar->initialize(Value(std::static_pointer_cast<::cw::lib::Object>(
        std::shared_ptr<CWPyCard>(&card, [](CWPyCard*) {}))));
    g_card_lifebars.emplace(&card, bar);
    register_lifebar_layer(bar);
    return bar;
}

Rect rect_from_size_center(Integer width, Integer height, const Rect& center)
{
    // pygame.Rect.center と同様、各寸法を別々に切り捨てる。 減
    // 算をまとめてから割ると、奇数サイズの拡大・配布フレームが 1px ずれる。
    return {center.x + center.width / 2 - width / 2,
            center.y + center.height / 2 - height / 2, width, height};
}

Integer rect_center_x(const Rect& rect) { return rect.x + rect.width / 2; }
Integer rect_center_y(const Rect& rect) { return rect.y + rect.height / 2; }

} // namespace

void refresh_card_layer(CWPyCard& arg_card, std::optional<Integer> arg_layer)
{
    register_card_layer(arg_card, arg_layer);
}

bool is_in_cardgrp(const CWPyCard& arg_card)
{
    const auto proxy = g_card_proxies.find(&arg_card);
    if (proxy == g_card_proxies.end() || !proxy->second)
        return false;
    const auto group = card_layer_registry().registered_group.lock();
    return group && std::find(group->sprites.begin(), group->sprites.end(), proxy->second) != group->sprites.end();
}

void remove_from_cardgrp(CWPyCard& arg_card)
{
    unregister_card_layer(arg_card);
}

void remove_all_menu_cards_from_cardgrp()
{
    // unregister_card_layer() が g_card_proxies
    // を変更するため、先に対象を集める。 Python は実際の Sprite を cardgrp
    // から除くが、ネイティブの互換層では同等の明示的なプロキシー除去を行う。
    std::vector<CWPyCard*> stale;
    stale.reserve(g_card_proxies.size());
    for (const auto& [card, unused] : g_card_proxies) {
        (void)unused;
        if (card && dynamic_cast<const MenuCard*>(card))
            stale.push_back(const_cast<CWPyCard*>(card));
    }
    for (auto* card : stale)
        unregister_card_layer(*card);
}

void clear_lifebars(std::optional<CWPyCard*> arg_user)
{
    std::vector<const CWPyCard*> targets;
    for (const auto& [card, unused] : g_card_lifebars) {
        (void)unused;
        if (!arg_user || card == *arg_user) targets.push_back(card);
    }
    for (const auto* card : targets) {
        const auto it = g_card_lifebars.find(card);
        if (it == g_card_lifebars.end()) continue;
        // バーは初期化・クリックと同じ背景プロキシーを使う。 登録表が実際の
        // グループを記憶し、現在の Engine が変わった・消えた場合も
        // 、Python の Sprite と同様に解除できるようにする。
        ::cw::sprite::background::remove_background_proxies_for_owner(it->second.get());
        g_card_lifebars.erase(it);
    }
}

void show_lifebar(CWPyCard& arg_card)
{
    (void)card_lifebar(arg_card, true);
}

std::shared_ptr<::cw::sprite::background::LifeBar> find_owned_lifebar(
    const ::cw::sprite::background::LifeBar* arg_bar)
{
    if (!arg_bar || !arg_bar->m_ccard_typed) return {};
    const auto it = g_card_lifebars.find(arg_bar->m_ccard_typed.get());
    return it != g_card_lifebars.end() && it->second.get() == arg_bar ? it->second : nullptr;
}

CWPyCard::~CWPyCard()
{
    // set_sprites() は m_mcards を消してタイトル・宿のメニューカード
    // を破棄する。 LayeredSpriteProxy は CWPySprite の破棄で
    // 消えるが、cardgrp は別の g_card_proxies の Sprite を保
    // 持する。 その所有者ポインターを残すと、エリア変更後の最初の update_groups()
    // が解放済みメモリーに入り、RTTI で異常終了するため、明示的に解除する。
    unregister_card_layer(*this);
}

CWPyCard* card_layer_sprite_owner(const std::shared_ptr<::cw::lib::Sprite>& arg_sprite)
{
    if (!arg_sprite) return nullptr;
    const auto it = g_card_proxy_owners.find(arg_sprite.get());
    return it == g_card_proxy_owners.end() ? nullptr : it->second;
}

std::optional<::cw::lib::Integer> card_layer_sprite_layer(
    const std::shared_ptr<::cw::lib::Sprite>& arg_sprite)
{
    if (!arg_sprite) return std::nullopt;
    const auto it = g_card_proxy_layers.find(arg_sprite.get());
    return it == g_card_proxy_layers.end()
        ? std::nullopt
        : std::optional<::cw::lib::Integer>(it->second);
}

void CWPyCard::initialize(::cw::lib::String arg_status, std::optional<::cw::lib::String> arg_flag)
{
    // Python CWPyCard.__init__ の全状態をカード単位で初期化する。
    // SelectableSprite の pygame SpriteGroup 登録は SDL の所有層で行う。
    ::cw::sprite::base::SelectableSprite::initialize({});
    m_alpha.reset();
    m_status = arg_status;
    m_old_status = arg_status;
    m__clicking = false;
    m_input_dispatching = false;
    m_click_animating = false;
    m_debug_only = false;
    m__rect = Rect{};
    m_rect = m__rect;
    m__pos_noscale.reset();
    m__center_noscale.reset();
    m_frame = 0;
    m_zoomimgs.clear();
    m_zoomsize_noscale = {0, 0};
    m_reversed = false;
    m_cardtarget = false;
    m_flag = std::move(arg_flag);
    m_scale = 100;
    m_actions.clear();
    m_actions[7] = false;
    m_highspeed = false;
    m_battlespeed = false;
    m_hide_inusecardimg = true;
    m_dealspeed = -1;
    m_spchars = false;
    m__cardimg.reset();
    m_command.clear();
    m_arg.clear();
    m__image = empty_surface();
    m_image = empty_surface();
}

std::shared_ptr<::cw::image::CardImage> CWPyCard::get_cardimg()
{
    if (!has_cardimg())
        throw std::logic_error("CWPyCard.cardimg is not initialized");
    return *m__cardimg;
}

bool CWPyCard::has_cardimg()
{
    return m__cardimg.has_value() && static_cast<bool>(*m__cardimg);
}

void CWPyCard::release_animated_images()
{
    if (has_cardimg())
        get_cardimg()->release_animated_paths();
}

::cw::lib::String CWPyCard::get_showingname()
{
    return {};
}

bool CWPyCard::is_flagtrue()
{
    bool mcardflag = true;
    if (m_flag) {
        const auto sdata = current_sdata();
        mcardflag = !sdata || sdata->get_flagvalue(*m_flag);
    }
    if (mcardflag)
        mcardflag = !m_debug_only || (::cw::g_cwpy && ::cw::g_cwpy->is_debugmode());

    const auto sdata = current_sdata();
    const auto setting = current_setting();
    if (mcardflag && m_command == "ShowDialog" && m_arg == "INFOVIEW") {
        mcardflag = ::cw::g_cwpy && ::cw::g_cwpy->is_playingscenario() &&
            sdata && sdata->has_infocards();
    } else if (mcardflag && (m_command == "MoveCard" || m_command == "ShowDialog") &&
               m_arg == "BACKPACK") {
        mcardflag = sdata && sdata->m_party_environment_backpack;
    } else if (mcardflag && setting && !setting->m_show_sell_with_premiercard &&
               m_command == "MoveCard" && (m_arg == "PAWNSHOP" || m_arg == "TRASHBOX")) {
        bool protected_premium = false;
        if (::cw::g_cwpy && ::cw::g_cwpy->m_selectedheader &&
            *::cw::g_cwpy->m_selectedheader)
            protected_premium = (*::cw::g_cwpy->m_selectedheader)->m_premium == "Premium";
        mcardflag = (::cw::g_cwpy && ::cw::g_cwpy->is_debugmode()) ||
            !setting->m_protect_premiercard || !protected_premium;
    }
    return mcardflag;
}

bool CWPyCard::is_flagtrue_static(std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    if (!arg_data)
        return false;
    const auto flag = read_text(arg_data, "Property/Flag", "");
    const auto sdata = current_sdata();
    bool mcardflag = sdata ? sdata->get_flagvalue(flag) : true;
    if (mcardflag) {
        const auto debug_only = read_bool(arg_data, ".", "debugOnly", false);
        mcardflag = !debug_only || (::cw::g_cwpy && ::cw::g_cwpy->is_debugmode());
    }
    if (mcardflag) {
        const auto command = read_attr(arg_data, ".", "command", "");
        const auto arg = read_attr(arg_data, ".", "arg", "");
        const auto setting = current_setting();
        if (command == "ShowDialog" && arg == "INFOVIEW") {
            mcardflag = ::cw::g_cwpy && ::cw::g_cwpy->is_playingscenario() &&
                sdata && sdata->has_infocards();
        } else if ((command == "MoveCard" || command == "ShowDialog") && arg == "BACKPACK") {
            mcardflag = sdata && sdata->m_party_environment_backpack;
        } else if (setting && !setting->m_show_sell_with_premiercard && command == "MoveCard" &&
                   (arg == "PAWNSHOP" || arg == "TRASHBOX")) {
            bool protected_premium = false;
            if (::cw::g_cwpy && ::cw::g_cwpy->m_selectedheader &&
                *::cw::g_cwpy->m_selectedheader)
                protected_premium = (*::cw::g_cwpy->m_selectedheader)->m_premium == "Premium";
            mcardflag = (::cw::g_cwpy && ::cw::g_cwpy->is_debugmode()) ||
                !setting->m_protect_premiercard || !protected_premium;
        }
    }
    return mcardflag;
}

std::shared_ptr<::cw::lib::Surface> CWPyCard::get_unselectedimage()
{
    return m_status == "click" ? m_image : get_animeimage();
}

std::shared_ptr<::cw::lib::Surface> CWPyCard::get_selectedimage()
{
    // Python の CWPyCard.get_selectedimage
    // と同様、 外枠と透過色の縁を維持するカード専用の反転を使う。
    return ::cw::imageretouch::to_negative_for_card(get_animeimage(), 0);
}

void CWPyCard::set_alpha(std::optional<::cw::lib::Integer> arg_alpha)
{
    m_alpha = arg_alpha;
    sync_card_proxy(*this);
}

std::shared_ptr<::cw::lib::Surface> CWPyCard::get_render_image() const
{
    if (!m_image || !m_alpha || *m_alpha >= 255) return m_image;
    auto rendered = copy_surface(m_image);
    set_surface_alpha(rendered, m_alpha);
    rendered->known_opaque = false;
    return rendered;
}

std::shared_ptr<::cw::lib::Surface> CWPyCard::get_animeimage()
{
    if (!m_zoomimgs.empty())
        return std::get<0>(m_zoomimgs.back());
    return m__image;
}

::cw::lib::Rect CWPyCard::get_animerect()
{
    return m_zoomimgs.empty() ? m__rect : std::get<1>(m_zoomimgs.back());
}

::cw::lib::Rect CWPyCard::get_baserect()
{
    return m__rect;
}

std::vector<::cw::lib::Integer> CWPyCard::_get_dealingscales()
{
    const auto setting = current_setting();
    if (!setting)
        return {100};
    const auto force = ::cw::g_cwpy ? ::cw::g_cwpy->m_force_dealspeed : -1;
    if (force != -1)
        return setting->create_dealingscales(force);
    if (m_dealspeed != -1)
        return setting->create_dealingscales(m_dealspeed);
    const auto override_speed = ::cw::g_cwpy ? optional_integer(::cw::g_cwpy->m_override_dealspeed) : std::nullopt;
    if (override_speed && *override_speed != -1)
        return setting->create_dealingscales(*override_speed);
    std::vector<Integer> result;
    if (m_battlespeed && setting->m_use_battlespeed)
        result = scales_from_value(setting->m_dealing_scales_battle);
    else
        result = scales_from_value(setting->m_dealing_scales);
    if (result.empty())
        result = setting->create_dealingscales(
            setting->get_dealspeed(m_battlespeed && setting->m_use_battlespeed));
    return result;
}

::cw::lib::Integer CWPyCard::get_dealspeed(bool arg_battlespeed)
{
    const auto setting = current_setting();
    const auto force = ::cw::g_cwpy ? ::cw::g_cwpy->m_force_dealspeed : -1;
    if (force != -1)
        return force;
    if (m_dealspeed != -1)
        return m_dealspeed;
    const auto override_speed = ::cw::g_cwpy ? optional_integer(::cw::g_cwpy->m_override_dealspeed) : std::nullopt;
    if (override_speed && *override_speed != -1)
        return *override_speed;
    return setting ? setting->get_dealspeed(arg_battlespeed) : 0;
}

::cw::lib::Integer CWPyCard::_get_dealspeed()
{
    const auto setting = current_setting();
    return get_dealspeed(m_battlespeed && setting && setting->m_use_battlespeed);
}

void CWPyCard::update(const ::cw::lib::Arguments& arg_args, const ::cw::lib::KeywordArguments& arg_kwargs)
{
#if defined(CARDWIRTH_LOG_ENABLED)
    const auto update_started = std::chrono::steady_clock::now();
#endif
    (void)arg_args;
    (void)arg_kwargs;
    if (m_status == "normal") update_normal();
    else if (m_status == "delete") update_delete();
    else if (m_status == "reversed") update_reversed();
    else if (m_status == "hidden") update_hidden();
    else if (m_status == "reverse") update_reverse();
    else if (m_status == "click") update_click();
    else if (m_status == "deal") update_deal();
    else if (m_status == "hide") update_hide();
    else if (m_status == "lateralvibe") update_lateralvibe();
    else if (m_status == "axialvibe") update_axialvibe();
    else if (m_status == "zoomin") update_zoomin();
    else if (m_status == "zoomin_slow") update_zoomin_slow();
    else if (m_status == "zoomout") update_zoomout();
    else if (m_status == "zoomout_slow") update_zoomout_slow();
    else if (m_status == "shiftup") update_shiftup();
    else if (m_status == "shiftdown") update_shiftdown();
    else if (m_status == "scale") update_scale();

    if (has_cardimg() && m_status != "hidden" && m_status != "reversed" &&
        m_status != "hide" && m_status != "deal" && m_status != "reverse" &&
        get_cardimg()->update_animation()) {
        if (auto* inuse = dynamic_cast<::cw::sprite::background::InuseCardImage*>(this)) {
            // Py は self の InuseCardImage.update_image
            // の上書きを呼ぶ。 C++ は重ね合わせの Object のクリップ
            // を基底の Rect の結果型と分けるため、 BattleCardImage
            // や状態遷移を変えず、ここでその限定的な呼び分けを維持する。
            const auto clip = inuse->update_image(false, std::nullopt);
            if (clip && ::cw::g_cwpy)
                ::cw::g_cwpy->add_lazydraw(clip);
        } else {
            const auto clip = update_image(false, std::nullopt);
            if (clip && ::cw::g_cwpy)
                ::cw::g_cwpy->add_lazydraw_rect(*clip);
        }
    }
    sync_card_proxy(*this);
#if defined(CARDWIRTH_LOG_ENABLED)
    ::cardwirth::debug::log_slow(
        "card.update", std::chrono::steady_clock::now() - update_started,
        "status=" + m_status + " width=" + std::to_string(m_rect.width) +
        " height=" + std::to_string(m_rect.height) +
        " has_image=" + (has_cardimg() ? "1" : "0"));
#endif
}

void CWPyCard::update_normal()
{
    update_selection();
}

bool CWPyCard::is_selection()
{
    const auto log_camp_hit_test = [this](const char* result) {
#if defined(CARDWIRTH_LOG_ENABLED)
        if (!::cw::g_cwpy || ::cw::g_cwpy->m_areaid != -4) return;
        const auto* context = ::cw::sprite::base::runtime_context();
        if (!context) return;
        const auto& p = context->mouse_pos;
        if (p.x < m_rect.x || p.x >= m_rect.x + m_rect.width ||
            p.y < m_rect.y || p.y >= m_rect.y + m_rect.height)
            return;
        const std::string signature = std::string(result) + ":" + m_status + ":" +
            (m_is_pointed ? "1" : "0") + ":" +
            std::to_string(m_rect.x) + "," + std::to_string(m_rect.y) + "," +
            std::to_string(m_rect.width) + "," + std::to_string(m_rect.height);
        static thread_local std::unordered_map<const CWPyCard*, std::string> last_signatures;
        auto& last = last_signatures[this];
        if (last == signature) return;
        last = signature;
        ::cardwirth::debug::log(
            "Camp card hit-test: type=" +
            std::string(dynamic_cast<const MenuCard*>(this) ? "menu" :
                        (dynamic_cast<const ::cw::character::Player*>(this) ? "player" :
                         (dynamic_cast<const FriendCard*>(this) ? "friend" : "card"))) +
            " result=" + result +
            " status=" + m_status +
            " pointed=" + (m_is_pointed ? "1" : "0") +
            " rect=" + std::to_string(m_rect.x) + "," + std::to_string(m_rect.y) + "," +
            std::to_string(m_rect.width) + "," + std::to_string(m_rect.height) +
            " mouse=" + std::to_string(p.x) + "," + std::to_string(p.y));
#else
        (void)result;
#endif
    };
    // Python と同様、非表示・配布中のカードは update_selection()
    // に入れない。 古いネイティブプロキシーには以前の矩形が残る場合が
    // あるため、表示中のキャンプカードのホバー領域を覆う前に拒否する。
    if (m_status != "normal" && m_status != "reversed" && m_status != "click") {
        log_camp_hit_test("rejected-status");
        return false;
    }
    if (!m_image || m_image->size.width <= 0 || m_image->size.height <= 0 ||
        m_rect.width <= 0 || m_rect.height <= 0 || !is_flagtrue()) {
        log_camp_hit_test("rejected-image-rect-or-flag");
        return false;
    }
    if (dynamic_cast<MenuCard*>(this) && ::cw::g_cwpy) {
        const bool is_current_menu_card = std::any_of(
            ::cw::g_cwpy->m_mcards.begin(), ::cw::g_cwpy->m_mcards.end(),
            [this](const Value& value) {
                const auto* object = std::get_if<::cw::lib::ObjectPtr>(&value.storage());
                return object && *object && object->get() == this;
            });
        // Python は現在 cardgrp に属するスプライトだけを当たり判定する
        // 。 C++ はアニメーション・エリア変更前の保存で古いキャンプの MenuCard
        // を保持し得るため、現在の m_mcards への所属も要求する。
        if (!is_current_menu_card) {
            log_camp_hit_test("rejected-not-current-menu");
            return false;
        }
    }
    const bool selected = ::cw::sprite::base::SelectableSprite::is_selection();
    log_camp_hit_test(selected ? "accepted" : "rejected-base");
    return selected;
}

void CWPyCard::update_delete()
{
    release_animated_images();
}

void CWPyCard::update_reversed()
{
    // デバッグ中、またはキャンプで私有カード表示が許可される場合だけ選択する。
    const auto setting = current_setting();
    const auto area = value_integer(::cw::g_AREA_CAMP, -9999);
    const bool is_player = dynamic_cast<::cw::character::Player*>(this) != nullptr;
    if (::cw::g_cwpy && (!::cw::g_cwpy->m_selectedheader || !*::cw::g_cwpy->m_selectedheader) &&
        (::cw::g_cwpy->is_debugmode() ||
         (setting && setting->m_show_personal_cards && is_player && ::cw::g_cwpy->m_areaid == area)))
        update_selection();
}

void CWPyCard::update_hidden()
{
}

void CWPyCard::update_reverse()
{
    if (m_old_status == "hidden") {
        m_reversed = !m_reversed;
        _reverse();
        m_status = "hidden";
        return;
    }
    m_hide_inusecardimg = false;
    const auto target = m_cardtarget;
    update_hide();
    m_cardtarget = target;
    m_hide_inusecardimg = true;
    if (m_status == "hidden") {
        if (::cw::g_cwpy)
            ::cw::g_cwpy->wait_frame(1, false, std::nullopt, 0);
        m_reversed = !m_reversed;
        _reverse();
        m_frame = 0;
        m_old_status = m_reversed ? "reversed" : "normal";
        m_status = "deal";
        if (::cw::g_cwpy)
            ::cw::animation::animate_sprite(
                std::shared_ptr<CWPyCard>(this, [](CWPyCard*) {}),
                "deal", true, false, false, m_battlespeed);
        if (m_reversed)
            m_status = "reversed";
    }
}

void CWPyCard::reverse()
{
    m_reversed = !m_reversed;
    _reverse();
}

void CWPyCard::_reverse()
{
    if (!m_reversed) {
        update_image(false, std::nullopt);
        return;
    }

    std::shared_ptr<::cw::lib::Surface> image;
    if (::cw::g_cwpy && ::cw::g_cwpy->m_rsrc) {
        const auto* object = std::get_if<::cw::lib::ObjectPtr>(&::cw::g_cwpy->m_rsrc->m_cardbgs.storage());
        if (object && *object) {
            const auto table = std::dynamic_pointer_cast<::cw::setting::ResourceTable>(*object);
            if (table) {
                const auto value = table->get_item(Value("REVERSE"));
                if (const auto* image_object = std::get_if<::cw::lib::ObjectPtr>(&value.storage());
                    image_object && *image_object)
                    image = std::dynamic_pointer_cast<::cw::lib::Surface>(*image_object);
            } else {
                image = std::dynamic_pointer_cast<::cw::lib::Surface>(*object);
            }
        }
    }
    if (!image) {
        image = empty_surface(m__rect.width, m__rect.height);
    }
    if (m_scale != 100)
        image = ::cw::image::zoomcard(image, static_cast<double>(m_scale) / 100.0);
    else
        image = copy_surface(image);
    m__image = image;
    if (!m_zoomimgs.empty())
        m_rect = std::get<1>(m_zoomimgs.back());
    const auto setting = current_setting();
    for (std::size_t i = 0; i < m_zoomimgs.size(); ++i) {
        const auto rect = std::get<1>(m_zoomimgs[i]);
        const bool smooth = setting && setting->m_smoothing_card_up && i + 1 == m_zoomimgs.size();
        auto scaled = smooth ? ::cw::image::smoothscale_card(image, {rect.width, rect.height}, true)
                             : scale_surface(image, rect.width, rect.height);
        m_zoomimgs[i] = {std::move(scaled), rect};
    }
}

void CWPyCard::update_click()
{
    if (!has_cardimg()) {
        // クリック処理がスプライトを保持した後、ダイアログ終了や戦闘更新で
        // CardImage が一時的に外れる場合がある。 status を
        // click のままにすると animate_sprite() が
        // 終わらないため、 論理的なカード選択は変えず表示操作を終了する。
        m_status = m_old_status.empty() ? "normal" : m_old_status;
        m__clicking = false;
        m_frame = 0;
        return;
    }
    if (m_frame < 3 && !m__clicking) {
        if (const auto bar = card_lifebar(*this, false)) {
            bar->click();
            sync_lifebar_proxy(bar);
        }
        const auto anchor = get_animerect();
        auto clicked = get_cardimg()->get_clickedimg(
            std::make_shared<::cw::RectValue>(anchor),
            m_reversed ? std::optional<std::shared_ptr<::cw::lib::Surface>>(m_image) : std::nullopt);
        m_image = clicked ? copy_surface(clicked) : copy_surface(get_animeimage());
        m_rect = rect_from_size_center(m_image->size.width, m_image->size.height, anchor);
        m_status = "click";
        m__clicking = true;
    } else if (m_frame >= 3) {
        if (const auto bar = card_lifebar(*this, false)) {
            bar->declick();
            sync_lifebar_proxy(bar);
        }
        m_status = m_old_status;
        m__clicking = false;
        m_image = get_selectedimage();
        m_rect = get_animerect();
        m_frame = 0;
        return;
    }
    ++m_frame;
}

void CWPyCard::update_deal()
{
    const auto scales = _get_dealingscales();
    if (m_frame >= static_cast<Integer>(scales.size())) {
        deal();
        m_frame = 0;
        return;
    }
    if (m_frame == 0 && has_cardimg() && get_cardimg()->is_modifiedfile())
        update_image(false, std::nullopt);
    const auto source = get_animeimage();
    const auto anchor = get_animerect();
    const auto width = anchor.width * scales[scales.size() - 1 - static_cast<std::size_t>(m_frame)] / 100;
    m_image = scale_surface(source, width, anchor.height);
    if (is_selected(*this))
        m_image = ::cw::imageretouch::to_negative_for_card(m_image, 0);
    m_rect = rect_from_size_center(m_image->size.width, m_image->size.height, anchor);
    m_frame += m_highspeed ? 2 : 1;
}

void CWPyCard::deal()
{
    m_status = m_reversed ? "reversed" : "normal";
    if (has_cardimg() && (get_cardimg()->is_modifiedfile() || !m_image || m_image->size.width <= 0))
        update_image(false, std::nullopt);
    m_image = get_animeimage();
    if (is_selected(*this))
        m_image = ::cw::imageretouch::to_negative_for_card(m_image, 0);
    m_rect = get_animerect();
}

void CWPyCard::update_hide()
{
    const auto scales = _get_dealingscales();
    if (m_frame >= static_cast<Integer>(scales.size())) {
        hide();
        m_frame = 0;
        return;
    }
    const auto anchor = get_animerect();
    const auto source = get_animeimage();
    const auto width = anchor.width * scales[static_cast<std::size_t>(m_frame)] / 100;
    m_image = scale_surface(source, width, anchor.height);
    if (is_selected(*this))
        m_image = ::cw::imageretouch::to_negative_for_card(m_image, 0);
    m_rect = rect_from_size_center(m_image->size.width, m_image->size.height, anchor);
    m_frame += m_highspeed ? 2 : 1;
}

void CWPyCard::hide()
{
    m_status = "hidden";
    clear_image(true);
    if (m_hide_inusecardimg) {
        const auto self = std::shared_ptr<CWPyCard>(&*this, [](CWPyCard*) {});
        if (::cw::g_cwpy)
            ::cw::g_cwpy->clear_inusecardimg(self);
    }
    clear_cardtarget();
    if (::cw::g_cwpy && ::cw::g_cwpy->m_selection && (*::cw::g_cwpy->m_selection).get() == this)
        ::cw::g_cwpy->clear_selection();
}

void CWPyCard::update_lateralvibe()
{
    const auto n = std::max<Integer>(1, ( _get_dealspeed() + 1) * 3);
    if (m_frame >= n) {
        m_rect = get_animerect();
        m_status = m_old_status;
        m_frame = 0;
        return;
    }
    const Integer count = n < 14 ? 2 : 6;
    const double nb = static_cast<double>(n) / (count * 4.0);
    const auto f0 = std::max<Integer>(0, static_cast<Integer>(std::llround(m_frame / nb)) - 1);
    const double nx = (static_cast<double>(m_frame) - nb * f0) / nb * 2.0;
    const auto f = f0 % 4;
    double value = f <= 0 ? nx : f <= 1 ? 2.0 - nx : f <= 2 ? -nx : -2.0 + nx;
    const auto val = static_cast<Integer>(std::llround(value));
    const auto anchor = get_animerect();
    m_rect = anchor;
    m_rect.x += scaled_coordinate(val);
    ++m_frame;
}

void CWPyCard::update_axialvibe()
{
    const auto n = std::max<Integer>(1, (_get_dealspeed() + 1) * 3);
    const auto anchor = get_animerect();
    if (m_frame >= n) {
        m_rect = anchor;
        if (!m_image || m_image->size.width != m_rect.width || m_image->size.height != m_rect.height)
            m_image = scale_surface(get_animeimage(), m_rect.width, m_rect.height);
        m_status = m_old_status;
        m_frame = 0;
        return;
    }
    const Integer count = n < 12 ? 2 : 4;
    const double nb = static_cast<double>(n) / (count * 2.0);
    const auto f0 = std::max<Integer>(0, static_cast<Integer>(std::llround(m_frame / nb)) - 1);
    const double nx = (static_cast<double>(m_frame) - nb * f0) / nb * (m__rect.width / 20);
    const auto f = f0 % 2;
    auto value = static_cast<Integer>(std::llround(f <= 0 ? -nx : -(m__rect.width / 20) + nx));
    if (value % 2 != 0)
        value += value < 0 ? -1 : 1;
    m_rect = anchor;
    m_rect.x -= scaled_coordinate(value / 2);
    m_rect.width += scaled_coordinate(value);
    m_image = scale_surface(get_animeimage(), m_rect.width, m_rect.height);
    ++m_frame;
}

void CWPyCard::update_zoomin()
{
    _update_zoominout(_get_dealspeed() + 1, true);
}

void CWPyCard::update_zoomin_slow()
{
    _update_zoominout(_get_dealspeed() * 2 + 1, true);
}

void CWPyCard::update_zoomout()
{
    _update_zoominout(_get_dealspeed() + 1, false);
}

void CWPyCard::update_zoomout_slow()
{
    _update_zoominout(_get_dealspeed() * 2 + 1, false);
}

void CWPyCard::_update_zoominout(::cw::lib::Integer arg_ds, bool arg_inout)
{
    arg_ds = std::max<Integer>(1, arg_ds);
    if (m_frame == 0 && arg_inout)
        m_zoomimgs.emplace_back(get_animeimage(), get_animerect());
    if (m_zoomimgs.empty())
        return;
    Integer zoom_w = 0;
    Integer zoom_h = 0;
    if (arg_inout) {
        zoom_w = scaled_coordinate(std::get<0>(m_zoomsize_noscale));
        zoom_h = scaled_coordinate(std::get<1>(m_zoomsize_noscale));
    } else {
        zoom_w = std::get<1>(m_zoomimgs.back()).width - std::get<1>(m_zoomimgs.front()).width;
        zoom_h = std::get<1>(m_zoomimgs.back()).height - std::get<1>(m_zoomimgs.front()).height;
    }
    const auto maxw = std::max<Integer>(0, m__rect.width + zoom_w);
    const auto maxh = std::max<Integer>(0, m__rect.height + zoom_h);
    Integer width = m__rect.width;
    Integer height = m__rect.height;
    if (m_old_status == "hidden" || arg_ds <= 1) {
        if (arg_inout) { width = maxw; height = maxh; }
        m_frame = arg_ds;
    } else {
        const auto calc_zoom = [&](Integer value) {
            const auto frame = arg_inout ? m_frame : arg_ds - m_frame - 1;
            const double den = (arg_ds + 1.0) * arg_ds / 2.0;
            return static_cast<Integer>(::cw::lib::python_float_round_integral(
                value * ((arg_ds * 2.0 - frame + 1.0) * frame / 2.0) / den));
        };
        auto value = calc_zoom(zoom_w);
        if (value % 2 != 0) value += arg_ds / 2 <= m_frame ? 1 : -1;
        width = std::clamp<Integer>(m__rect.width + value, 0, maxw);
        value = calc_zoom(zoom_h);
        if (value % 2 != 0) value += arg_ds / 2 <= m_frame ? 1 : -1;
        height = std::clamp<Integer>(m__rect.height + value, 0, maxh);
        ++m_frame;
    }
    const auto setting = current_setting();
    m_image = scale_surface(std::get<0>(m_zoomimgs.front()), width, height);
    if (setting && setting->m_smoothing_card_up && arg_ds <= m_frame)
        m_image = ::cw::image::smoothscale_card(std::get<0>(m_zoomimgs.front()), {width, height}, true);
    m_rect = rect_from_size_center(m_image->size.width, m_image->size.height, get_animerect());
    if (arg_ds <= m_frame) {
        if (arg_inout)
            m_zoomimgs.emplace_back(m_image, m_rect);
        else
            m_zoomimgs.clear();
        m_status = m_old_status;
        m_frame = 0;
        if (m_status == "hidden")
            clear_image(false);
    }
}

void CWPyCard::clear_zoomimgs()
{
    m_zoomimgs.clear();
    if (m_status == "hidden")
        clear_image(false);
}

void CWPyCard::update_shiftup()
{
    const auto speed = std::max<Integer>(1, (_get_dealspeed() + 1) * 3);
    // ネイティブのアニメーション管理は、最初の更新より前に m_frame
    // を進める場合がある。 フレームゼロだけでなく、表示用の
    // 描画面が空ならキャッシュしたカード画像から初期化する。
    if (!m_image || m_image->size.width <= 0 || m_image->size.height <= 0) {
        const auto cached = get_animeimage();
        if ((!cached || cached->size.width <= 0 || cached->size.height <= 0) && has_cardimg())
            update_image(false, std::nullopt);
        m_image = get_animeimage();
    }
    const auto offset = scaled_coordinate(150);
    auto y = m__rect.y + offset - static_cast<Integer>(static_cast<double>(offset) / speed * m_frame);
    if (!m_zoomimgs.empty())
        y += std::get<1>(m_zoomimgs.back()).y - std::get<1>(m_zoomimgs.front()).y;
    m_rect.x = get_animerect().x;
    m_rect.y = y;
    m_rect.width = m_image ? m_image->size.width : 0;
    m_rect.height = m_image ? m_image->size.height : 0;
    for (auto& item : m_zoomimgs) {
        auto& rect = std::get<1>(item);
        rect.x = rect_center_x(m_rect) - rect.width / 2;
        rect.y = rect_center_y(m_rect) - rect.height / 2;
    }
    if (m_frame >= speed) {
        m_status = m_reversed ? "reversed" : "normal";
        m_rect.x = get_animerect().x;
        m_rect.y = get_animerect().y;
        m_frame = 0;
    } else ++m_frame;
}

void CWPyCard::update_shiftdown()
{
    const auto speed = std::max<Integer>(1, (_get_dealspeed() + 1) * 3);
    const auto offset = scaled_coordinate(150);
    const auto y = m__rect.y + static_cast<Integer>(static_cast<double>(offset) / speed * m_frame);
    if (!m_image) m_image = empty_surface();
    m_rect.width = m_image->size.width;
    m_rect.height = m_image->size.height;
    if (!m_zoomimgs.empty()) {
        auto& first = std::get<1>(m_zoomimgs.front());
        first.x = m__rect.x;
        first.y = y;
        for (std::size_t i = 1; i < m_zoomimgs.size(); ++i) {
            auto& rect = std::get<1>(m_zoomimgs[i]);
            rect.x = rect_center_x(first) - rect.width / 2;
            rect.y = rect_center_y(first) - rect.height / 2;
        }
        m_rect.x = rect_center_x(first) - m_rect.width / 2;
        m_rect.y = rect_center_y(first) - m_rect.height / 2;
    } else {
        m_rect.x = m__rect.x;
        m_rect.y = y;
    }
    if (m_frame >= speed) {
        m_image = empty_surface();
        m_status = "hidden";
        m_frame = 0;
    } else ++m_frame;
}

void CWPyCard::update_scale()
{
    if (!is_initialized() || !has_cardimg())
        return;
    const bool zoom = !m_zoomimgs.empty();
    if (zoom && m_status != "zoomin" && m_status != "zoomout") {
        m_old_status = m_status;
        m_status = "zoomout";
        while (m_status == "zoomout") update_zoomout();
    }
    get_cardimg()->update_scale();
    update_image(false, std::nullopt);
    if (m__pos_noscale || m__center_noscale)
        set_pos_noscale(m__pos_noscale, m__center_noscale);
    if (zoom && m_status != "zoomin" && m_status != "zoomout") {
        m_old_status = m_status;
        m_status = "zoomin";
        while (m_status == "zoomin") update_zoomin();
    }
    if (m_status == "hidden")
        clear_image(true);
}

std::optional<::cw::lib::Rect> CWPyCard::update_image(bool arg_update_statusimg, std::optional<bool> arg_is_runningevent)
{
    if (!has_cardimg())
        return std::nullopt;

    auto clip = m_rect;
    auto cardimg = get_cardimg();
    const auto card_owner = std::shared_ptr<CWPyCard>(this, [](CWPyCard*) {});
    if (arg_update_statusimg) {
        if (auto character_image = std::dynamic_pointer_cast<::cw::image::CharacterCardImage>(cardimg)) {
            const auto status_clip = character_image->update_statusimg(
                Value(std::static_pointer_cast<::cw::lib::Object>(card_owner)), arg_is_runningevent);
            if (!status_clip)
                return std::nullopt;
            if (const auto rect = std::dynamic_pointer_cast<::cw::RectValue>(*status_clip))
                clip = rect->rect;
        } else {
            return std::nullopt;
        }
    } else if (auto character_image = std::dynamic_pointer_cast<::cw::image::CharacterCardImage>(cardimg)) {
        std::optional<std::shared_ptr<::cw::header::CardHeader>> aptitude;
        if (const auto character = dynamic_cast<::cw::character::Character*>(this))
            aptitude = character->m_test_aptitude;
        character_image->update(card_owner, aptitude);
    } else {
        cardimg->update(card_owner, std::nullopt);
    }

    auto image = cardimg->get_image();
    const bool missing_image = !image || image->size.width <= 0 || image->size.height <= 0;
    if (!image)
        image = empty_surface();
    image = copy_surface(image);
    if (missing_image) {
        // 画像実装が SDL texture を返せない場合も、カ
        // ード領域の大きさを保持して状態遷移・ヒット判定を保つ。m__rect
        // は現在倍率の大きさなので、倍率を戻してから下の
        // zoomcard 相当の処理を一度だけ適用する。
        const auto scale = m_scale == 0 ? 1.0 : static_cast<double>(m_scale) / 100.0;
        image->size = {std::max<Integer>(0, static_cast<Integer>(std::llround(m__rect.width / scale))),
                       std::max<Integer>(0, static_cast<Integer>(std::llround(m__rect.height / scale)))};
        if (image->size.width > 0 && image->size.height > 0 && image->rgba.empty())
            image->rgba.resize(static_cast<std::size_t>(image->size.width * image->size.height * 4), 0);
    }
    const auto scale = static_cast<double>(m_scale) / 100.0;
    image = ::cw::image::zoomcard(image, scale);
    if (m_cardtarget)
        image = ::cw::imageretouch::to_negative_for_card(image, 0);

    m__image = image;
    if (!m_reversed && m_zoomimgs.empty())
        m_image = m__image;
    m__rect.width = image->size.width;
    m__rect.height = image->size.height;
    if (m_rect.width <= 0 && m_rect.height <= 0)
        m_rect = m__rect;
    else if (m_zoomimgs.empty()) {
        m_rect.width = m__rect.width;
        m_rect.height = m__rect.height;
    }

    if (m_reversed) {
        _reverse();
        if (m_zoomimgs.empty())
            m_image = m__image;
    } else if (!m_zoomimgs.empty()) {
        m_zoomimgs.front() = {m__image, std::get<1>(m_zoomimgs.front())};
        const auto setting = current_setting();
        for (std::size_t i = 1; i < m_zoomimgs.size(); ++i) {
            const auto rect = std::get<1>(m_zoomimgs[i]);
            auto resized = scale_surface(m__image, rect.width, rect.height);
            if (setting && setting->m_smoothing_card_up && i + 1 == m_zoomimgs.size())
                resized = ::cw::image::smoothscale_card(m__image, {rect.width, rect.height}, true);
            m_zoomimgs[i] = {std::move(resized), rect};
        }
        m_image = std::get<0>(m_zoomimgs.back());
        m_rect = std::get<1>(m_zoomimgs.back());
    }
    if (m_status == "hidden")
        clear_image(false);
    else if (is_selected(*this))
        m_image = get_selectedimage();
    sync_card_proxy(*this);
    return clip;
}

void CWPyCard::clear_image(bool arg_move)
{
    const auto x = m_rect.x;
    const auto y = m_rect.y;
    m_image = empty_surface();
    if (arg_move) {
        m_rect = Rect{};
        m_rect.x = x;
        m_rect.y = y;
    }
    sync_card_proxy(*this);
}

void CWPyCard::set_pos_noscale(std::optional<std::tuple<::cw::lib::Integer, ::cw::lib::Integer>> arg_pos_noscale, std::optional<std::tuple<::cw::lib::Integer, ::cw::lib::Integer>> arg_center_noscale)
{
    // Python版は pos 優先、未指定時のみ center を採用する。
    if (arg_pos_noscale)
        m__pos_noscale = arg_pos_noscale;
    else if (arg_center_noscale)
        m__center_noscale = arg_center_noscale;
    std::optional<std::tuple<Integer, Integer>> pos;
    std::optional<std::tuple<Integer, Integer>> center;
    if (arg_pos_noscale)
        pos = scaled_point(arg_pos_noscale);
    else if (arg_center_noscale)
        center = scaled_point(arg_center_noscale);
    set_pos(pos, center);
}

void CWPyCard::set_pos(std::optional<std::tuple<::cw::lib::Integer, ::cw::lib::Integer>> arg_pos, std::optional<std::tuple<::cw::lib::Integer, ::cw::lib::Integer>> arg_center)
{
    const auto visible_width = m_rect.width;
    const auto visible_height = m_rect.height;
    if (arg_pos) {
        const auto [x, y] = *arg_pos;
        m__rect.x = x;
        m__rect.y = y;
    } else if (arg_center) {
        const auto [cx, cy] = *arg_center;
        m__rect.x = cx - m__rect.width / 2;
        m__rect.y = cy - m__rect.height / 2;
    }
    m_rect.width = visible_width;
    m_rect.height = visible_height;
    m_rect.x = rect_center_x(m__rect) - m_rect.width / 2;
    m_rect.y = rect_center_y(m__rect) - m_rect.height / 2;
    for (auto& item : m_zoomimgs) {
        auto& rect = std::get<1>(item);
        rect.x = rect_center_x(m__rect) - rect.width / 2;
        rect.y = rect_center_y(m__rect) - rect.height / 2;
    }
    if (has_cardimg())
        get_cardimg()->m_rect = Value(std::make_shared<::cw::RectValue>(m__rect));
    sync_card_proxy(*this);
}

std::tuple<::cw::lib::Integer, ::cw::lib::Integer> CWPyCard::get_pos_noscale()
{
    if (m__pos_noscale)
        return *m__pos_noscale;
    throw std::logic_error("CWPyCard position was not initialized");
}

void CWPyCard::set_scale(::cw::lib::Integer arg_scale)
{
    if (arg_scale == m_scale)
        return;
    if (m_scale != 0) {
        for (auto& item : m_zoomimgs) {
            auto& rect = std::get<1>(item);
            rect.width = static_cast<Integer>(rect.width * (static_cast<double>(arg_scale) / m_scale));
            rect.height = static_cast<Integer>(rect.height * (static_cast<double>(arg_scale) / m_scale));
        }
    }
    m_scale = arg_scale;
    update_image(false, std::nullopt);
}

void CWPyCard::set_size(::cw::lib::Integer arg_width, ::cw::lib::Integer arg_height)
{
    if (arg_width <= 0 && arg_height <= 0)
        return;
    const auto pos = m__pos_noscale;
    if (has_cardimg())
        get_cardimg()->set_size(arg_width, arg_height);
    if (arg_width > 0)
        m__rect.width = arg_width * m_scale / 100;
    if (arg_height > 0)
        m__rect.height = arg_height * m_scale / 100;
    update_image(false, std::nullopt);
    if (pos)
        set_pos_noscale(pos, std::nullopt);
}

void CWPyCard::set_cardtarget()
{
    if (!m_cardtarget) {
        m_cardtarget = true;
        update_image(false, std::nullopt);
        if (::cw::g_cwpy)
            ::cw::g_cwpy->add_lazydraw_rect(m_rect);
    }
}

void CWPyCard::clear_cardtarget()
{
    if (m_cardtarget) {
        m_cardtarget = false;
        update_image(false, std::nullopt);
        if (::cw::g_cwpy)
            ::cw::g_cwpy->add_lazydraw_rect(m_rect);
    }
}

// ネイティブ XML の数値境界では、末尾のパーセント記号を一つだけ除き、全
// 体を範囲付き Integer として解析する。 Py の int(s.rstrip("%"))
// と異なり、複数のパーセント記号・末尾空白・範囲外の値は失敗し得る。
// 不完全なネイティブ XML では空の配布速度を Default として
// 許す。 XML だけでなく復元した移動データのレイヤーも範囲内へ収め
// 、全タプルを共通の有限範囲に保つ。Py は XML だけを制限する。
void EnemyCard::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_mcarddata, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_pos_noscale, ::cw::lib::String arg_status, bool arg_addgroup, ::cw::lib::Integer arg_index, std::optional<std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer>> arg_moveddata)
{
    if (!arg_mcarddata)
        throw std::invalid_argument("EnemyCard requires menu-card data");
    CWPyCard::initialize(arg_status, std::nullopt);
    m_zoomsize_noscale = {16, 22};
    CWPyCard::m_zoomsize_noscale = m_zoomsize_noscale;
    m_index = arg_index;
    m_mcarddata = std::move(arg_mcarddata);
    m__moveddata = arg_moveddata;
    m__init_pos_noscale = arg_moveddata
        ? std::optional<std::tuple<Integer, Integer>>{{std::get<0>(*arg_moveddata), std::get<1>(*arg_moveddata)}}
        : std::optional<std::tuple<Integer, Integer>>{arg_pos_noscale};
    m_flag = read_text(m_mcarddata, "Property/Flag", "");
    m_cardgroup = read_text(m_mcarddata, "Property/CardGroup", "");
    m_actions[7] = read_bool(m_mcarddata, ".", "escape", false);

    // WSN.4 の Property/Actions は属性値をテキストとして読む。
    if (const auto actions = m_mcarddata->find("Property/Actions"); actions && *actions) {
        for (const auto& action : (*actions)->m_children) {
            if (!action || action->m_tag != "Action")
                continue;
            const auto actid = action->getint(".", Value("id"), std::nullopt);
            m_actions[actid] = action->getbool(".", Value{}, true);
        }
    }
    m_spchars = read_bool(m_mcarddata, "Property/Name", "override", false);

    if (arg_moveddata && std::get<2>(*arg_moveddata) != -1)
        m_scale = std::get<2>(*arg_moveddata);
    else if (::cw::g_cwpy && ::cw::g_cwpy->is_autospread())
        m_scale = 100;
    else {
        auto scale = read_attr(m_mcarddata, "Property/Size", "scale", "100%");
        if (!scale.empty() && scale.back() == '%')
            scale.pop_back();
        std::size_t used = 0;
        m_scale = std::stoll(scale, &used);
        if (used != scale.size())
            throw std::invalid_argument("EnemyCard scale is not an integer");
    }
    auto dealing = read_text(m_mcarddata, "Property/DealingSpeed", "Default");
    if (dealing == "Default" || dealing.empty())
        m_dealspeed = -1;
    else {
        std::size_t used = 0;
        const auto parsed = std::stoll(dealing, &used);
        if (used != dealing.size())
            throw std::invalid_argument("EnemyCard dealing speed is not an integer");
        m_dealspeed = std::clamp<Integer>(parsed, 0, 10);
    }

    m__init = false;
    m__rect = Rect{};
    m_rect = m__rect;
    if (arg_status == "hidden")
        clear_image(false);
    else if (!initialize_2())
        throw std::runtime_error("EnemyCard initialization failed");

    Integer layer = arg_moveddata && std::get<3>(*arg_moveddata) != -1
        ? std::get<3>(*arg_moveddata)
        : read_int(m_mcarddata, "Property/Layer", "", -1);
    layer = std::clamp<Integer>(layer, -1, ::cw::g_LAYER_MAX);
    if (layer < 0) {
        // CardWirth 1.20以前のメニューカード順序を維持する互換分岐。
        bool old_zindex = false;
        const auto sdata = current_sdata();
        if (::cw::g_cwpy && sdata && ::cw::g_cwpy->m_sct) {
            old_zindex = ::cw::g_cwpy->m_sct->zindexmode(
                sdata->get_versionhint(::cw::g_HINT_SCENARIO)) ||
                ::cw::g_cwpy->m_sct->zindexmode(
                    sdata->get_versionhint(::cw::g_HINT_AREA));
        }
        layer = old_zindex ? ::cw::g_LAYER_MCARDS_120 : ::cw::g_LAYER_MCARDS;
    }
    const bool curtain_all = ::cw::g_cwpy && ::cw::g_cwpy->m_background &&
        ::cw::g_cwpy->m_background->m_curtain_all;
    const bool special_area = ::cw::g_cwpy &&
        (::cw::g_cwpy->m_areaid == value_integer(std::get<0>(::cw::g_AREAS_SP)) ||
         ::cw::g_cwpy->m_areaid == value_integer(std::get<1>(::cw::g_AREAS_SP)) ||
         ::cw::g_cwpy->m_areaid == value_integer(std::get<2>(::cw::g_AREAS_SP)) ||
         ::cw::g_cwpy->m_areaid == value_integer(std::get<3>(::cw::g_AREAS_SP)) ||
         ::cw::g_cwpy->m_areaid == value_integer(std::get<4>(::cw::g_AREAS_SP)));
    if (curtain_all || special_area)
        layer += ::cw::g_LAYER_SP_LAYER;
    m_tlayer = {layer, ::cw::g_LTYPE_MCARDS, m_index, 0};

    if (arg_addgroup) {
        register_card_layer(*this);
        // ChangeEnemy 中は旧カードが除去済みで、新しいカードも
        // まだ m_mcards にないため、find_enemy_card()
        // で所有者を取得できない。 Python のコンストラクターが
        // 新規インスタンスを直接追加するのと同様、 実行中の集合へ公開す
        // る前に Character 基底から同じ共有所有者を取得する。
        const auto self_character = ::cw::character::Character::weak_from_this().lock();
        const auto shared = std::dynamic_pointer_cast<::cw::sprite::card::EnemyCard>(self_character);
        if (shared && ::cw::g_cwpy) {
            const auto* object = std::get_if<::cw::lib::ObjectPtr>(
                &::cw::g_cwpy->m_cardgrp.storage());
            if (object && *object) {
                ::cw::lib::Arguments sprites;
                sprites.emplace_back(Value(std::static_pointer_cast<::cw::lib::Object>(
                    std::static_pointer_cast<::cw::sprite::base::CWPySprite>(shared))));
                ::cw::add_layer(*object, sprites, ::cw::layer_val(m_tlayer));
            }
            const auto sprite_value = Value(std::static_pointer_cast<::cw::lib::Object>(
                std::static_pointer_cast<::cw::sprite::base::CWPySprite>(shared)));
            ::cw::g_cwpy->m_mcards.push_back(sprite_value);
            if (m_spchars)
                ::cw::g_cwpy->m_mcards_expandspchars.push_back(sprite_value);
        }
    }
}

// ネイティブの不完全 XML への対応として、Id がなければリソース 0、Events
// がなければ空のイベント Engine を使う。Py は両項目を必須とする
// 。 ネイティブではリソース取得失敗時に不要なイベント Engine を作ら
// ないよう、CastCard と Character・デッキの準備後にイベ
// ントを構築する。 Py は CastCard の確認前にイベントを作る。
bool EnemyCard::initialize_2()
{
    if (m__init)
        return true;
    if (!m_mcarddata)
        return false;
    m__init = true;

    // メニューカードのIdからシナリオのCastCardを遅延取得する。
    const auto sdata = current_sdata();
    const auto resid = read_int(m_mcarddata, "Property/Id", "", 0);
    // get_castdata() は既に表から CastCard リソースを
    // 選んでいるため、Python と同様にここへ空のタグを渡す。 CastCard
    // を渡すと、C++ の XML ローダーが CastCard 文書ル
    // ートの下にさらに同名の子を探し、全キャストカードを拒否してしまう。
    const auto cast = sdata ? sdata->get_castdata(resid, "", true, std::nullopt)
                            : std::optional<std::shared_ptr<::cw::data::CWPyElement>>{};
    if (!cast || !*cast) {
        if (::cw::g_cwpy) {
            ::cw::g_cwpy->m_mcards.erase(
                std::remove_if(::cw::g_cwpy->m_mcards.begin(), ::cw::g_cwpy->m_mcards.end(),
                               [this](const Value& value) { return value_points_to_enemy(value, *this); }),
                ::cw::g_cwpy->m_mcards.end());
            ::cw::g_cwpy->m_mcards_expandspchars.erase(
                std::remove_if(::cw::g_cwpy->m_mcards_expandspchars.begin(),
                               ::cw::g_cwpy->m_mcards_expandspchars.end(),
                               [this](const Value& value) { return value_points_to_enemy(value, *this); }),
                ::cw::g_cwpy->m_mcards_expandspchars.end());
            for (auto it = ::cw::g_cwpy->m_file_updates.begin();
                 it != ::cw::g_cwpy->m_file_updates.end();) {
                if (*it && dynamic_cast<EnemyCard*>((*it).get()) == this)
                    it = ::cw::g_cwpy->m_file_updates.erase(it);
                else
                    ++it;
            }
        }
        unregister_card_layer(*this);
        return false;
    }
    m_data_typed = std::make_shared<::cw::data::CWPyElementTree>(*cast, (*cast)->m_fpath);
    m_fpath = m_data_typed->m_fpath;
    ::cw::character::Character::initialize(m_data_typed);
    // EnemyCard は Python の二つの基底クラスを
    // 別々の C++ 基底として持つ。 キャラクター読み込みは称
    // 号「：Ｒ」から Character::m_reversed
    // を更新するが、配布・画像更新は CWPyCard::m_reversed
    // を読む。 Python では一つの共有属性なので、
    // 最初の画像合成前に読み込み値をコピーし、 シナリオ
    // カードの初期の表裏を戦闘エリアの配布中も維持する。
    CWPyCard::m_reversed = ::cw::character::Character::m_reversed;
    update_skin();
    // Character::initialize() はデッキを作るが、Python の EnemyCard.initialize()
    // と同様、 戦闘参加前にその中身も設定する。
    if (m_deck)
        m_deck->set(borrowed_enemy_character(*this), false);

    if (m_mcarddata) {
        const auto events = m_mcarddata->find("Events");
        m_events = std::make_shared<::cw::event::EventEngine>();
        m_events->initialize(events && *events ? (*events)->m_children
                                               : std::vector<std::shared_ptr<::cw::data::CWPyElement>>{},
                             "EnemyCard");
    }

    if (m_spchars)
        m__name = read_text(m_mcarddata, "Property/Name", "");
    else
        m__name = read_text(m_data_typed->getroot(), "Property/Name", "");

    m_imgpaths.clear();
    for (const auto& info : player_imageinfos(m_data_typed->getroot())) {
        if (!info)
            continue;
        auto path = copy_imageinfo(info);
        if (path)
            path->m_path = ::cw::util::get_materialpath(
                path->m_path, ::cw::g_M_IMG, sdata ? sdata->m_scedir : String{}, false, true);
        m_imgpaths.push_back(std::move(path));
    }

    const bool override_image = read_bool(m_mcarddata, "Property/ImagePaths", "override", false);
    std::optional<std::tuple<std::vector<std::shared_ptr<::cw::image::ImageInfo>>, std::vector<bool>>> overrides;
    if (override_image) {
        auto infos = player_imageinfos(m_mcarddata, true);
        overrides = imageinfos_to_pathdata(std::move(infos));
    }
    const bool can_scaled = read_bool(m_data_typed->getroot(), ".", "scaledimage", false);
    m_width = read_int(m_mcarddata, "Property/Size", "width", 95);
    m_height = read_int(m_mcarddata, "Property/Size", "height", 130);
    if (m__moveddata) {
        if (std::get<4>(*m__moveddata) > 0)
            m_width = std::get<4>(*m__moveddata);
        if (std::get<5>(*m__moveddata) > 0)
            m_height = std::get<5>(*m__moveddata);
    }
    set_card_geometry(*this, m_width, m_height);

    auto cardimg = std::make_shared<::cw::image::CharacterCardImage>();
    const auto owner = borrowed_enemy_character(*this);
    const bool borrowed_image_owner = std::get_deleter<BorrowedCharacterDeleter>(owner) != nullptr;
    const auto owner_value = Value(std::static_pointer_cast<::cw::lib::Object>(owner));
    const auto override_name = m_spchars
        ? std::get<0>(::cw::sprite::message::rpl_specialstr(
            m__name, std::nullopt, false, "All", false, true, std::nullopt))
        : String{};
    cardimg->initialize(owner_value, m__init_pos_noscale.value_or(std::tuple<Integer, Integer>{0, 0}),
                        can_scaled, true, String{}, m_spchars, override_name, override_image,
                        overrides, std::make_tuple(m_width, m_height), borrowed_image_owner);
    // CharacterCardImageの描画実装が未接続でも、名前/上書き/サイズ状態は
    // Python同様にキャッシュへ保持し、後段SDLがそのまま再利用できるようにする。
    cardimg->m_override_name = Value(override_name);
    cardimg->m_is_override_name = Value(m_spchars);
    cardimg->m_is_override_image = Value(override_image);
    cardimg->m_can_loaded_scaledimage = Value(can_scaled);
    cardimg->m_is_scenariocard = Value(true);
    if (overrides)
        cardimg->m_override_images_upd = *overrides;
    m__cardimg = cardimg;
    set_pos_noscale(m__init_pos_noscale, std::nullopt);
    // CharacterCardImage が共通の Surface キャッシュを生成する。
    // 精神力の回復は Character::set_skillpower() に接続する。
    update_image(false, std::nullopt);
    clear_image(true);
    ::cw::character::Character::set_skillpower();
    return true;
}

bool EnemyCard::is_initialized()
{
    return m__init;
}

void EnemyCard::update_skin()
{
    if (!::cw::g_cwpy || !::cw::g_cwpy->m_classicdata ||
        !*::cw::g_cwpy->m_classicdata || !::cw::g_cwpy->m_rsrc)
        return;
    const auto old_action = m_actions.count(0) ? m_actions[0] : true;
    const auto& items = std::get<1>(m_cardpocket_typed);
    const auto action = ::cw::g_cwpy->m_rsrc->m_actioncards.find(0);
    const bool card_exchange = !items.empty() && action != ::cw::g_cwpy->m_rsrc->m_actioncards.end() &&
        items.front() && action->second && items.front()->m_name == action->second->m_name;
    m_actions[0] = !card_exchange;
    if (old_action != m_actions[0] && m_deck)
        m_deck->set(borrowed_enemy_character(*this), false);
}

std::shared_ptr<::cw::image::CharacterCardImage> EnemyCard::get_cardimg()
{
    // MenuCard::get_cardimg() と同様、遅延された
    // 非表示の敵は初回使用時に初期化する。 Python の assert
    // self._cardimg は Release では除かれるため
    // 、 ここで未初期化の例外を出して戦闘や長押しを異常終了させない。
    if (!m__init)
        initialize_2();
    if (!CWPyCard::has_cardimg())
        throw std::logic_error("EnemyCard.cardimg is not initialized");
    auto result = std::dynamic_pointer_cast<::cw::image::CharacterCardImage>(CWPyCard::get_cardimg());
    if (!result)
        throw std::logic_error("EnemyCard.cardimg has an invalid type");
    return result;
}

::cw::lib::String EnemyCard::get_showingname()
{
    if (!initialize_2())
        return {};
    // 戦闘・ダイアログ遷移中、論理的には初期化済み
    // でも顔画像が外れる場合がある。 名前取得は対
    // 象確定の診断にも使うため、一時的な表示状態を
    // 致命的エラーにしない。 他のキャラクターカー
    // ドと同じ代替として、論理的なカード名を返す。
    if (!has_cardimg())
        return m__name;
    const auto cardimg = get_cardimg();
    return m_spchars ? value_string(get_cardimg()->m_override_name, m__name) : m__name;
}

void EnemyCard::update_name()
{
    // Python は画像の型のアサート後、未初期
    // 化なら戻るが、Release ではアサートが
    // 除かれる。 戦闘前などの特殊文字を含む名前の
    // 非表示敵は未初期化の場合があるため、 画像を
    // 要求して例外を出す前に初期化状態を確認する。
    if (!m__init || !m_spchars || !has_cardimg())
        return;
    const auto cardimg = get_cardimg();
    const bool old_inuse = ::cw::g_cwpy && ::cw::g_cwpy->m_event
        ? ::cw::g_cwpy->m_event->m_in_inusecardevent : false;
    if (::cw::g_cwpy && ::cw::g_cwpy->m_event)
        ::cw::g_cwpy->m_event->m_in_inusecardevent = false;
    try {
        const auto name = std::get<0>(::cw::sprite::message::rpl_specialstr(
            m__name, std::nullopt, false, "All", false, true, std::nullopt));
        if (value_string(cardimg->m_override_name, {}) != name) {
            cardimg->m_override_name = Value(name);
            cardimg->set_nameimg(name);
            update_image(false, std::nullopt);
        }
    } catch (...) {
        if (::cw::g_cwpy && ::cw::g_cwpy->m_event)
            ::cw::g_cwpy->m_event->m_in_inusecardevent = old_inuse;
        throw;
    }
    if (::cw::g_cwpy && ::cw::g_cwpy->m_event)
        ::cw::g_cwpy->m_event->m_in_inusecardevent = old_inuse;
}

void EnemyCard::update(const ::cw::lib::Arguments& arg_args, const ::cw::lib::KeywordArguments& arg_kwargs)
{
    if (CWPyCard::m_status != "hidden" && !m__init && !initialize_2())
        return;
    CWPyCard::update(arg_args, arg_kwargs);
}

void EnemyCard::update_delete()
{
    if (CWPyCard::m_old_status == "hidden")
        CWPyCard::hide();
    else
        CWPyCard::update_hide();
    if (value_integer(CWPyCard::m_frame) != 0)
        return;
    release_animated_images();
    if (!::cw::g_cwpy)
        return;
    ::cw::g_cwpy->m_mcards.erase(
        std::remove_if(::cw::g_cwpy->m_mcards.begin(), ::cw::g_cwpy->m_mcards.end(),
                       [this](const Value& value) { return value_points_to_enemy(value, *this); }),
        ::cw::g_cwpy->m_mcards.end());
    ::cw::g_cwpy->m_mcards_expandspchars.erase(
        std::remove_if(::cw::g_cwpy->m_mcards_expandspchars.begin(),
                       ::cw::g_cwpy->m_mcards_expandspchars.end(),
                       [this](const Value& value) { return value_points_to_enemy(value, *this); }),
        ::cw::g_cwpy->m_mcards_expandspchars.end());
    for (auto it = ::cw::g_cwpy->m_file_updates.begin();
         it != ::cw::g_cwpy->m_file_updates.end();) {
        if (*it && dynamic_cast<EnemyCard*>((*it).get()) == this)
            it = ::cw::g_cwpy->m_file_updates.erase(it);
        else
            ++it;
    }
    unregister_card_layer(*this);
}

void EnemyCard::lclick_event()
{
    const auto runtime = ::cw::g_cwpy;
    if (!runtime)
        return;
    const auto shared = find_enemy_card(*this);
    const auto selected_header = runtime->m_selectedheader && *runtime->m_selectedheader
        ? *runtime->m_selectedheader : std::shared_ptr<::cw::header::CardHeader>{};
    TargetSelectionDispatchGuard target_guard(
        runtime.get(), runtime->is_curtained() && !area_in_trade(runtime->m_areaid) &&
            static_cast<bool>(selected_header));
    if (!target_guard) return;
    const auto owner = std::static_pointer_cast<::cw::sprite::card::CWPyCard>(shared);
    CardInputGuard input_guard(owner);
    if (!input_guard) return;
    runtime->play_sound("click", false, 100, 1, 0, 0, false);
    if (!start_card_click(owner, m_battlespeed)) return;
    if (::cw::g_cwpy != runtime) return;
    if (const auto live = find_enemy_card(*this); !live || live.get() != shared.get()) return;

    const bool area_camp = runtime->m_areaid == value_integer(::cw::g_AREA_CAMP);
    if ((!runtime->is_curtained() || area_camp) && is_analyzable()) {
        if (runtime->is_battlestatus()) {
            if (is_inactive()) {
                runtime->call_modaldlg(
                    "NOTICE", {{"text", Value(message_with_name(
                        player_message("inactive", "%s is inactive"), get_showingname()))}});
            } else if (m_deck && !m_deck->m_hand.empty()) {
                runtime->call_modaldlg("HANDVIEW", {{"selection", Value(
                    std::static_pointer_cast<::cw::lib::Object>(owner))}});
            }
        }
    } else if (selected_header && runtime->m_selectedheader &&
               *runtime->m_selectedheader == selected_header &&
               runtime->is_battlestatus()) {
        // Python は現在の敵カード一覧から所有されたスプライト
        // を渡す。 その所有者を解決できなければ対象が消えたものと
        // して、借用の this を保存せず選択をキャンセルする。
        if (shared)
            _select_action(std::static_pointer_cast<::cw::sprite::card::CWPyCard>(shared));
    }
}

void EnemyCard::rclick_event()
{
    const auto runtime = ::cw::g_cwpy;
    if (!runtime)
        return;
    const auto shared = find_enemy_card(*this);
    const auto owner = std::static_pointer_cast<::cw::sprite::card::CWPyCard>(shared);
    CardInputGuard input_guard(owner);
    if (!input_guard || !start_card_click(owner, m_battlespeed)) return;
    if (::cw::g_cwpy != runtime) return;
    if (const auto live = find_enemy_card(*this); !live || live.get() != shared.get()) return;
    runtime->play_sound("click", false, 100, 1, 0, 0, false);
    if (is_analyzable())
        runtime->call_modaldlg("CHARAINFO", {{"selection", Value(
            std::static_pointer_cast<::cw::lib::Object>(owner))}});
}

std::tuple<::cw::lib::Integer, ::cw::lib::Integer> EnemyCard::get_pos_noscale()
{
    if (m__init)
        return CWPyCard::get_pos_noscale();
    if (!m__init_pos_noscale)
        throw std::logic_error("EnemyCard position was not initialized");
    return *m__init_pos_noscale;
}

void EnemyCard::set_pos_noscale(std::optional<std::tuple<::cw::lib::Integer, ::cw::lib::Integer>> arg_pos_noscale, std::optional<std::tuple<::cw::lib::Integer, ::cw::lib::Integer>> arg_center_noscale)
{
    if (m__init)
        CWPyCard::set_pos_noscale(arg_pos_noscale, arg_center_noscale);
    else
        m__init_pos_noscale = arg_pos_noscale;
}

void EnemyCard::set_scale(::cw::lib::Integer arg_scale)
{
    if (m__init)
        CWPyCard::set_scale(arg_scale);
    else {
        m_scale = arg_scale;
    }
}

void FriendCard::initialize(::cw::lib::Value arg_data, ::cw::lib::Integer arg_index)
{
    CWPyCard::initialize("hidden", std::nullopt);
    m_zoomsize_noscale = {32, 42};
    m_index = arg_index;
    auto tree = value_tree(arg_data);
    if (!tree)
        throw std::invalid_argument("FriendCard requires Element or ElementTree");
    m_data_typed = std::move(tree);
    m_id = read_int(m_data_typed->getroot(), "Property/Id", "", 1);
    m_fpath = m_data_typed->m_fpath;
    ::cw::character::Character::initialize(m_data_typed);
    // Python の Character とスプライトは reversed を共有
    // する。 EnemyCard と同様、顔画像の構築前に同じ初期状態を読み込む。
    CWPyCard::m_reversed = ::cw::character::Character::m_reversed;
    if (m_deck)
        m_deck->set(borrowed_friend_character(*this), false);
    m_tlayer = {::cw::g_LAYER_FCARDS, ::cw::g_LTYPE_FCARDS, m_index, 0};
    const auto friend_layer = value_integer(::cw::g_LAYER_FCARDS_T, ::cw::g_LAYER_FCARDS);
    m_tlayer_t = {friend_layer, ::cw::g_LTYPE_FCARDS, m_index, 0};

    m_imgpaths.clear();
    for (const auto& info : player_imageinfos(m_data_typed->getroot())) {
        if (!info)
            continue;
        auto path = copy_imageinfo(info);
        if (path)
            path->m_path = ::cw::util::get_materialpath(
                path->m_path, ::cw::g_M_IMG, String{}, false, true);
        m_imgpaths.push_back(std::move(path));
    }
    const bool can_scaled = read_bool(m_data_typed->getroot(), ".", "scaledimage", false);
    set_card_geometry(*this, 95, 130);
    auto cardimg = std::make_shared<::cw::image::CharacterCardImage>();
    const auto image_owner = borrowed_friend_character(*this);
    const bool borrowed_image_owner = std::get_deleter<BorrowedCharacterDeleter>(image_owner) != nullptr;
    cardimg->initialize(Value(std::static_pointer_cast<::cw::lib::Object>(image_owner)),
                         std::make_tuple(Integer{0}, Integer{0}), can_scaled, true,
                         String{}, false, String{}, false,
                         std::nullopt, std::make_tuple(Integer{95}, Integer{130}), borrowed_image_owner);
    cardimg->m_can_loaded_scaledimage = Value(can_scaled);
    cardimg->m_is_scenariocard = Value(true);
    m__cardimg = cardimg;
    update_image(false, std::nullopt);
    clear_image(true);
    // 精神力回復はCharacter実装へ委譲する。
    ::cw::character::Character::set_skillpower();
}

std::shared_ptr<::cw::image::CharacterCardImage> FriendCard::get_cardimg()
{
    if (!CWPyCard::has_cardimg())
        throw std::logic_error("FriendCard.cardimg is not initialized");
    auto result = std::dynamic_pointer_cast<::cw::image::CharacterCardImage>(CWPyCard::get_cardimg());
    if (!result)
        throw std::logic_error("FriendCard.cardimg has an invalid type");
    return result;
}

::cw::lib::String FriendCard::get_showingname()
{
    return ::cw::character::Character::get_name();
}

void FriendCard::update_name()
{
    // Python版もFriendCard.update_name は意図的な pass。
}

void FriendCard::update_delete()
{
    release_animated_images();
    const auto sdata = current_sdata();
    if (sdata) {
        const auto self = find_friend_card(*this);
        if (self) {
            if (::cw::g_cwpy && ::cw::g_cwpy->m_ydata && *::cw::g_cwpy->m_ydata)
                (*::cw::g_cwpy->m_ydata)->changed();
            sdata->m_friendcards.erase(
                std::remove(sdata->m_friendcards.begin(), sdata->m_friendcards.end(), self),
                sdata->m_friendcards.end());
        }
    }
    CWPyCard::m_status = "hidden";
}

void FriendCard::lclick_event()
{
    const auto runtime = ::cw::g_cwpy;
    if (!runtime)
        return;
    const auto shared = find_friend_card(*this);
    const auto owner = std::static_pointer_cast<::cw::sprite::card::CWPyCard>(shared);
    CardInputGuard input_guard(owner);
    if (!input_guard) return;
    runtime->play_sound("click", false, 100, 1, 0, 0, false);
    if (!start_card_click(owner)) return;
    if (::cw::g_cwpy != runtime) return;
    if (const auto live = find_friend_card(*this); !live || live.get() != shared.get()) return;
    if (runtime->is_battlestatus()) {
        if (is_inactive()) {
            runtime->call_modaldlg(
                "NOTICE", {{"text", Value(message_with_name(
                    player_message("inactive", "%s is inactive"), get_showingname()))}});
        } else if (is_autoselectedpenalty(std::nullopt) && !runtime->is_debugmode()) {
            runtime->call_modaldlg(
                "NOTICE", {{"text", Value(player_message("selected_penalty", "Selected penalty"))}});
        } else if (m_deck && !m_deck->m_hand.empty()) {
            runtime->call_modaldlg("HANDVIEW", {{"selection", Value(
                std::static_pointer_cast<::cw::lib::Object>(owner))}});
        }
    } else if ((!runtime->is_curtained() ||
                runtime->m_areaid == value_integer(::cw::g_AREA_CAMP)) &&
               is_analyzable()) {
        runtime->call_modaldlg("CARDPOCKET", {{"selection", Value(
            std::static_pointer_cast<::cw::lib::Object>(owner))}});
    }
}

void FriendCard::rclick_event()
{
    const auto runtime = ::cw::g_cwpy;
    if (!runtime)
        return;
    const auto shared = find_friend_card(*this);
    const auto owner = std::static_pointer_cast<::cw::sprite::card::CWPyCard>(shared);
    CardInputGuard input_guard(owner);
    if (!input_guard) return;
    runtime->play_sound("click", false, 100, 1, 0, 0, false);
    if (!start_card_click(owner)) return;
    if (::cw::g_cwpy != runtime) return;
    if (const auto live = find_friend_card(*this); !live || live.get() != shared.get()) return;
    if (is_analyzable())
        runtime->call_modaldlg("CHARAINFO", {{"selection", Value(
            std::static_pointer_cast<::cw::lib::Object>(owner))}});
}

void MenuCard::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_pos_noscale, ::cw::lib::String arg_status, bool arg_addgroup, ::cw::lib::Integer arg_index, std::optional<std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer>> arg_moveddata, std::optional<bool> arg_splayer)
{
    if (!arg_data)
        throw std::invalid_argument("MenuCard requires element data");
    CWPyCard::initialize(arg_status, std::nullopt);
    m_index = arg_index;
    m__data = arg_data;
    m__pos_noscale2 = arg_moveddata
        ? std::optional<std::tuple<Integer, Integer>>{{std::get<0>(*arg_moveddata), std::get<1>(*arg_moveddata)}}
        : std::optional<std::tuple<Integer, Integer>>{arg_pos_noscale};
    m__name = read_text(arg_data, "Property/Name", "");
    m_desc = read_text(arg_data, "Property/Description", "");
    m_flag = read_text(arg_data, "Property/Flag", "");
    m_cardgroup = read_text(arg_data, "Property/CardGroup", "");
    m_debug_only = arg_data->getbool(".", Value("debugOnly"), false);
    m_author.clear();
    m_scenario.clear();
    m_negaflag = false;
    m__is_backpack = false;
    m__is_storehouse = false;
    m_spchars = read_bool(arg_data, "Property/Name", "spchars", false);
    m_command = read_attr(arg_data, ".", "command", "");
    m_arg = read_attr(arg_data, ".", "arg", "");

    if (arg_moveddata && std::get<2>(*arg_moveddata) != -1)
        m_scale = std::get<2>(*arg_moveddata);
    else if (::cw::g_cwpy && ::cw::g_cwpy->is_autospread())
        m_scale = 100;
    else {
        auto scale = read_attr(arg_data, "Property/Size", "scale", "100%");
        if (!scale.empty() && scale.back() == '%')
            scale.pop_back();
        std::size_t used = 0;
        m_scale = std::stoll(scale, &used);
        if (used != scale.size())
            throw std::invalid_argument("MenuCard scale is not an integer");
    }
    const bool large = arg_data->m_tag == "LargeMenuCard";
    m_width = read_int(arg_data, "Property/Size", "width", large ? 95 : 80);
    m_height = read_int(arg_data, "Property/Size", "height", large ? 130 : 110);
    if (arg_moveddata) {
        if (std::get<4>(*arg_moveddata) > 0)
            m_width = std::get<4>(*arg_moveddata);
        if (std::get<5>(*arg_moveddata) > 0)
            m_height = std::get<5>(*arg_moveddata);
    }
    auto dealing = read_text(arg_data, "Property/DealingSpeed", "Default");
    if (dealing == "Default")
        m_dealspeed = -1;
    else {
        std::size_t used = 0;
        const auto parsed = std::stoll(dealing, &used);
        if (used != dealing.size())
            throw std::invalid_argument("MenuCard dealing speed is not an integer");
        m_dealspeed = std::clamp<Integer>(parsed, 0, 10);
    }

    m__init = false;
    m__rect = Rect{};
    m_rect = m__rect;
    if (arg_status == "hidden")
        clear_image(false);
    else
        initialize_2();

    Integer layer = arg_moveddata && std::get<3>(*arg_moveddata) != -1
        ? std::get<3>(*arg_moveddata)
        : read_int(arg_data, "Property/Layer", "", -1);
    layer = compatible_menu_layer(layer);
    const bool splayer = arg_splayer.value_or(
        (::cw::g_cwpy && ::cw::g_cwpy->m_background &&
         ::cw::g_cwpy->m_background->m_curtain_all) || is_special_area());
    if (splayer)
        layer += ::cw::g_LAYER_SP_LAYER;
    m_tlayer = {layer, ::cw::g_LTYPE_MCARDS, m_index, 0};

    if (arg_addgroup) {
        // コンストラクタ中はshared_from_this()を使えないため、Pythonのself参照に
        // 相当する非所有shared_ptrを作る。実所有権は生成側のshared_ptrが保持する。
        const auto shared = std::shared_ptr<MenuCard>(this, [](MenuCard*) {});
        if (::cw::g_cwpy) {
            register_card_layer(*this);
            const auto* object = std::get_if<::cw::lib::ObjectPtr>(
                &::cw::g_cwpy->m_cardgrp.storage());
            if (object && *object) {
                ::cw::lib::Arguments sprites;
                sprites.emplace_back(Value(std::static_pointer_cast<::cw::lib::Object>(
                    std::static_pointer_cast<::cw::sprite::base::CWPySprite>(shared))));
                ::cw::add_layer(*object, sprites, ::cw::layer_val(m_tlayer));
            }
            const auto sprite_value = Value(std::static_pointer_cast<::cw::lib::Object>(
                std::static_pointer_cast<::cw::sprite::base::CWPySprite>(shared)));
            ::cw::g_cwpy->m_mcards.push_back(sprite_value);
            if (m_spchars)
                ::cw::g_cwpy->m_mcards_expandspchars.push_back(sprite_value);
        }
    }
}

bool MenuCard::initialize_2()
{
    if (m__init)
        return true;
    if (!m__data || !*m__data)
        return false;
    m__init = true;
    const auto data = *m__data;
    update_name();

    const auto events = data->find("Events");
    m_events = std::make_shared<::cw::event::EventEngine>();
    m_events->initialize(events && *events ? (*events)->m_children
                                           : std::vector<std::shared_ptr<::cw::data::CWPyElement>>{},
                         "MenuCard");

    auto infos = player_imageinfos(data->find_exists("Property"), true);
    auto pathdata = imageinfos_to_pathdata(std::move(infos));
    auto scaled_values = std::make_shared<::cw::data::VariantSequence>();
    for (const auto can_scaled : std::get<1>(pathdata))
        scaled_values->m_values.emplace_back(can_scaled);
    const auto can_loaded_scaledimage = Value(std::static_pointer_cast<::cw::lib::Object>(scaled_values));
    const bool scenario_card = ::cw::g_cwpy && ::cw::g_cwpy->m_areaid >= 0 &&
        ::cw::g_cwpy->is_playingscenario();
    if (data->m_tag == "LargeMenuCard") {
        auto image = std::make_shared<::cw::image::LargeCardImage>();
        image->initialize(std::get<0>(pathdata), "NORMAL", m_name, String{},
                          can_loaded_scaledimage, scenario_card, String{}, false,
                          std::make_tuple(m_width, m_height));
        image->m_name = Value(m_name);
        image->m_bgtype = Value("NORMAL");
        image->m_can_loaded_scaledimage = can_loaded_scaledimage;
        image->m_is_scenariocard = Value(scenario_card);
        image->m_paths_upd = std::get<0>(pathdata);
        image->m_can_loaded_scaledimage_upd = std::get<1>(pathdata);
        image->set_menu_card_text(true);
        m__cardimg = image;
    } else {
        auto image = std::make_shared<::cw::image::CardImage>();
        image->initialize(std::get<0>(pathdata), "NORMAL", m_name, String{},
                          can_loaded_scaledimage, scenario_card, String{}, false,
                          std::make_tuple(m_width, m_height));
        image->m_name = Value(m_name);
        image->m_bgtype = Value("NORMAL");
        image->m_can_loaded_scaledimage = can_loaded_scaledimage;
        image->m_is_scenariocard = Value(scenario_card);
        image->m_paths_upd = std::get<0>(pathdata);
        image->m_can_loaded_scaledimage_upd = std::get<1>(pathdata);
        image->set_menu_card_text(true);
        m__cardimg = image;
    }
    set_card_geometry(*this, m_width, m_height);
    update_image(false, std::nullopt);
    if (m__pos_noscale2)
        CWPyCard::set_pos_noscale(m__pos_noscale2, std::nullopt);

    if (m_command == "MoveCard") {
        m__is_backpack = m_arg == "BACKPACK";
        m__is_storehouse = m_arg == "STOREHOUSE";
    }
    // Python版同様、初期化後は元データを解放して遅延状態を終了する。
    m__data.reset();
    m__pos_noscale2.reset();
    return true;
}

::cw::lib::String MenuCard::get_showingname()
{
    return m_name;
}

void MenuCard::update_name()
{
    if (!m__init)
        return;
    m_name = m_spchars
        ? std::get<0>(::cw::sprite::message::rpl_specialstr(
            m__name, std::nullopt, false, "All", false, true, std::nullopt))
        : m__name;
    if (m__cardimg && *m__cardimg && value_string((*m__cardimg)->m_name, {}) != m_name) {
        (*m__cardimg)->set_name(m_name);
        update_image(false, std::nullopt);
    }
}

std::shared_ptr<::cw::image::CardImage> MenuCard::get_cardimg()
{
    if (!m__init)
        initialize_2();
    if (!CWPyCard::has_cardimg())
        throw std::logic_error("MenuCard.cardimg is not initialized");
    return CWPyCard::get_cardimg();
}

bool MenuCard::is_initialized()
{
    return m__init;
}

bool MenuCard::is_backpack()
{
    return m__is_backpack;
}

bool MenuCard::is_storehouse()
{
    return m__is_storehouse;
}

void MenuCard::update(const ::cw::lib::Arguments& arg_args, const ::cw::lib::KeywordArguments& arg_kwargs)
{
    if (m_status != "hidden" && !m__init)
        initialize_2();
    CWPyCard::update(arg_args, arg_kwargs);
}

void MenuCard::lclick_event()
{
    const auto runtime = ::cw::g_cwpy;
    if (!runtime || runtime->m_card_use_depth > 0)
        return;
    const auto shared = find_menu_card(*this);
    if (!shared) return;
    const auto owner = std::static_pointer_cast<::cw::sprite::card::CWPyCard>(shared);
    const auto selected_header = runtime->m_selectedheader && *runtime->m_selectedheader
        ? *runtime->m_selectedheader : std::shared_ptr<::cw::header::CardHeader>{};
    // メニュー対象にも、キャラクター対象と同じカード間のアニメーション
    // 再入保護を使う。 取引コマンドの既存のクリック動作は維持する。
    TargetSelectionDispatchGuard target_guard(runtime.get(),
        runtime->is_curtained() && selected_header && !area_in_trade(runtime->m_areaid));
    if (!target_guard) return;
    // すべてのカード種別で、カードごとの同じ実行・生存期間の保護を使う。 旧メ
    // ニュー専用の thread_local フラグでは右クリックを保護できず
    // 、 一枚の同期アニメーション中に無関係なメニューカードまで遮ってしまう。
    CardInputGuard input_guard(owner);
    if (!input_guard) return;
    const auto animate = [this, &shared]() {
        return start_card_click(
            std::static_pointer_cast<::cw::sprite::card::CWPyCard>(shared));
    };
    const auto start_event = [this]() {
        if (m_events)
            m_events->start(1, std::nullopt, false, false);
    };
    const auto start_command_or_event = [this, &start_event]() {
        if (m_events && m_events->check_keynum(1))
            start_event();
        else if (!m_command.empty())
            ::cw::content::PostEventContent::do_action(m_command, m_arg);
        else
            start_event();
    };

    // 通常のクリックイベント。
    if (!::cw::g_cwpy->is_curtained()) {
        ::cw::g_cwpy->play_sound("click", true, 100, 1, 0, 0, false);
        if (!animate()) return;
        if (!m_command.empty()) {
            start_command_or_event();
        } else {
            if (const auto logger = ::cw::g_cwpy->m_advlog; logger)
                logger->click_menucard(shared);
            start_event();
        }
        return;
    }

    // 交易エリアで選択中カードを移動する操作。
    if (area_in_trade(::cw::g_cwpy->m_areaid) &&
        ::cw::g_cwpy->m_selectedheader && *::cw::g_cwpy->m_selectedheader) {
        if (!animate()) return;
        start_command_or_event();
        return;
    }

    // カード使用イベント。
    if (::cw::g_cwpy->m_selectedheader && *::cw::g_cwpy->m_selectedheader) {
        ::cw::g_cwpy->play_sound("click", false, 100, 1, 0, 0, false);
        if (!animate()) return;
        if (::cw::g_cwpy != runtime || !runtime->m_selectedheader ||
            *runtime->m_selectedheader != selected_header || !runtime->is_curtained() ||
            find_menu_card(*this) != shared) return;
        if (::cw::g_cwpy->m_status == "Scenario")
            ::cw::g_cwpy->call_modaldlg("USECARD", {});
        else if (::cw::g_cwpy->m_status == "ScenarioBattle") {
            _select_action(std::static_pointer_cast<::cw::sprite::card::CWPyCard>(owner));
        }
        return;
    }

    // キャンプ・パーティ解散。
    const bool camp = ::cw::g_cwpy->m_areaid == value_integer(::cw::g_AREA_CAMP);
    const bool breakup = ::cw::g_cwpy->m_areaid == value_integer(::cw::g_AREA_BREAKUP);
    if (camp || breakup) {
        ::cw::g_cwpy->play_sound(breakup ? "page" : "click", false, 100, 1, 0, 0, false);
        if (!animate()) return;
        start_command_or_event();
    }
}

void MenuCard::rclick_event()
{
    if (!::cw::g_cwpy || ::cw::g_cwpy->is_modal_input_blocked())
        return;
#if defined(CARDWIRTH_LOG_ENABLED)
    ::cardwirth::debug::log("MenuCard rclick");
#endif
    ::cw::g_cwpy->play_sound("click", false, 100, 1, 0, 0, false);
    const auto shared = find_menu_card(*this);
    if (!shared) return;
    const auto owner = std::static_pointer_cast<::cw::sprite::card::CWPyCard>(shared);
    CardInputGuard input_guard(owner);
    if (!input_guard || !start_card_click(owner)) return;
    if (!m_desc.empty())
        ::cw::g_cwpy->call_modaldlg("MENUCARDINFO", {{"selection", Value(
            std::static_pointer_cast<::cw::lib::Object>(
                std::static_pointer_cast<::cw::sprite::card::CWPyCard>(shared)))}});
}

std::tuple<::cw::lib::Integer, ::cw::lib::Integer> MenuCard::get_pos_noscale()
{
    if (m__init)
        return CWPyCard::get_pos_noscale();
    if (!m__pos_noscale2)
        throw std::logic_error("MenuCard position was not initialized");
    return *m__pos_noscale2;
}

void MenuCard::set_pos_noscale(std::optional<std::tuple<::cw::lib::Integer, ::cw::lib::Integer>> arg_pos_noscale, std::optional<std::tuple<::cw::lib::Integer, ::cw::lib::Integer>> arg_center_noscale)
{
    if (m__init)
        CWPyCard::set_pos_noscale(arg_pos_noscale, arg_center_noscale);
    else
        m__pos_noscale2 = arg_pos_noscale;
}

void MenuCard::set_scale(::cw::lib::Integer arg_scale)
{
    if (m__init)
        CWPyCard::set_scale(arg_scale);
    else {
        m_scale = arg_scale;
    }
}

void MenuCard::set_size(::cw::lib::Integer arg_width, ::cw::lib::Integer arg_height)
{
    if (arg_width > 0)
        m_width = arg_width;
    if (arg_height > 0)
        m_height = arg_height;
    if (m__init)
        CWPyCard::set_size(arg_width, arg_height);
}

void PlayerCard::initialize(std::shared_ptr<::cw::data::CWPyElementTree> arg_data, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_pos_noscale, ::cw::lib::String arg_status, ::cw::lib::Integer arg_index)
{
    if (!arg_data)
        throw std::invalid_argument("PlayerCard requires character data");

    CWPyCard::initialize(arg_status, std::nullopt);
    CWPyCard::m_zoomsize_noscale = {16, 22};
    // Player.initialize() を呼び、card.cpp
    // 内の同じ XML 読み取りで表示・状態フィールドを設定する。
    ::cw::character::Player::initialize(arg_data);
    load_player_character_state(*this, arg_data);
    m_can_loaded_scaledimage = arg_data->getbool(".", Value("scaledimage"), false);

    m_imgpaths.clear();
    const auto property = arg_data->find_exists("Property");
    for (const auto& info : player_imageinfos(property)) {
        if (!info)
            continue;
        auto path = copy_imageinfo(info);
        if (path && !path->m_path.empty())
            path->m_path = ::cw::util::join_yadodir(path->m_path);
        m_imgpaths.push_back(std::move(path));
    }

    // ネイティブの make_shared は、この Python 型の初期化より先
    // に所有権を作るため、直接その所有者を取得する。 新しいカードはまだ cwpy.pcards
    // にない。画像は弱い逆参照を持ち、 Py の PlayerCard.__init__
    // と同様に下で一度登録し、呼び出し側で別に追加させない。
    auto owner = std::dynamic_pointer_cast<PlayerCard>(
        ::cw::character::Character::weak_from_this().lock());
    if (!owner) owner = find_player_card(*this);
    const Value owner_value = owner
        ? Value(std::static_pointer_cast<::cw::lib::Object>(
                    std::static_pointer_cast<::cw::sprite::base::CWPySprite>(owner)))
        : Value(std::static_pointer_cast<::cw::lib::Object>(
                    std::shared_ptr<CWPyCard>(this, [](CWPyCard*) {})));
    const auto pathdata = imageinfos_to_pathdata(m_imgpaths);
    auto cardimg = std::make_shared<::cw::image::CharacterCardImage>();
    cardimg->initialize(owner_value, arg_pos_noscale, m_can_loaded_scaledimage, false,
                        String{}, false, String{}, false,
                        std::make_tuple(std::get<0>(pathdata), std::get<1>(pathdata)),
                        std::make_tuple(Integer{95}, Integer{130}), !owner);
    CWPyCard::m__cardimg = cardimg;

    set_card_geometry(*this, 95, 130);
    CWPyCard::update_image(false, std::nullopt);
    CWPyCard::m_image = empty_surface();
    CWPyCard::set_pos_noscale(arg_pos_noscale, std::nullopt);
    if (CWPyCard::m_status == "hidden") {
        CWPyCard::m_rect = CWPyCard::m__rect;
        CWPyCard::m_rect.y += scaled_coordinate(150);
    }

    // Python の PlayerCard と同様、欠けた EP は set_coupon
    // で補う。 旧 Value 表だけでなく、実行時の表と保存 XML の両方を更新する。
    if (!m_coupons_typed.contains("＠ＥＰ"))
        set_coupon("＠ＥＰ", 0);
    if (player_has_coupon(*this, "：Ｒ")) {
        CWPyCard::m_reversed = true;
        ::cw::character::Character::m_reversed = true;
        CWPyCard::_reverse();
    }

    m_index_typed = arg_index;
    const bool curtain_all = ::cw::g_cwpy && ::cw::g_cwpy->m_background &&
        ::cw::g_cwpy->m_background->m_curtain_all;
    const bool special_area = ::cw::g_cwpy &&
        (::cw::g_cwpy->m_areaid == -1 || ::cw::g_cwpy->m_areaid == -2 ||
         ::cw::g_cwpy->m_areaid == -3 || ::cw::g_cwpy->m_areaid == -4 ||
         ::cw::g_cwpy->m_areaid == -5);
    const auto layer = curtain_all || special_area
        ? ::cw::g_LAYER_PCARDS + ::cw::g_LAYER_SP_LAYER : ::cw::g_LAYER_PCARDS;
    CWPyCard::m_tlayer = {layer, ::cw::g_LTYPE_PCARDS, m_index_typed, 0};
    register_card_layer(*this);
    if (owner && ::cw::g_cwpy) {
        const auto insert_at = std::clamp<Integer>(arg_index, 0,
            static_cast<Integer>(::cw::g_cwpy->m_pcards.size()));
        ::cw::g_cwpy->m_pcards.insert(
            ::cw::g_cwpy->m_pcards.begin() + static_cast<std::ptrdiff_t>(insert_at), owner);
    }
}

void PlayerCard::update(const ::cw::lib::Arguments& arg_args,
                        const ::cw::lib::KeywordArguments& arg_kwargs)
{
    if (CWPyCard::m_status == "levelup")
        update_levelup();
    else if (CWPyCard::m_status == "vanish")
        update_vanish();
    else
        CWPyCard::update(arg_args, arg_kwargs);
}

void PlayerCard::set_pos(std::optional<std::tuple<::cw::lib::Integer, ::cw::lib::Integer>> arg_pos, std::optional<std::tuple<::cw::lib::Integer, ::cw::lib::Integer>> arg_center)
{
    CWPyCard::set_pos(arg_pos, arg_center);
    if (CWPyCard::m_status == "hidden") {
        CWPyCard::m_rect = CWPyCard::m__rect;
        CWPyCard::m_rect.y += scaled_coordinate(150);
    }
}

std::shared_ptr<::cw::image::CharacterCardImage> PlayerCard::get_cardimg()
{
    if (!CWPyCard::has_cardimg())
        throw std::logic_error("PlayerCard.cardimg is not initialized");
    auto result = std::dynamic_pointer_cast<::cw::image::CharacterCardImage>(CWPyCard::get_cardimg());
    if (!result)
        throw std::logic_error("PlayerCard.cardimg has an invalid type");
    return result;
}

::cw::lib::String PlayerCard::get_showingname()
{
    return m_name;
}

void PlayerCard::set_name(::cw::lib::String arg_name)
{
    auto cardimg = get_cardimg();
    ::cw::character::Player::set_name(arg_name);
    m_name = std::move(arg_name);
    cardimg->set_nameimg(m_name);
    if (m_data_typed)
        m_data_typed->edit("Property/Name", m_name, std::nullopt);
}

std::vector<std::shared_ptr<::cw::image::ImageInfo>> PlayerCard::set_images(std::vector<std::shared_ptr<::cw::image::ImageInfo>> arg_paths)
{
    auto cardimg = get_cardimg();
    const auto requested = arg_paths;
    auto paths = ::cw::character::Player::set_images(std::move(arg_paths));
    // Character の共通実装がまだ空の環境でも、呼出し元の差替え順を失わない。
    if (paths.empty() && !requested.empty())
        paths = requested;

    m_imgpaths.clear();
    for (const auto& info : paths) {
        if (!info)
            continue;
        auto copy = copy_imageinfo(info);
        if (copy && !copy->m_path.empty())
            copy->m_path = ::cw::util::join_yadodir(copy->m_path);
        m_imgpaths.push_back(std::move(copy));
    }
    cardimg->set_faceimgs(m_imgpaths, Value(true));

    // XMLのImagePaths/ImagePath列も同期する。ファイルコピー自体は
    // Character.set_images/xmlcreaterの責務なので、ここでは状態と表示を同期。
    if (m_data_typed) {
        const auto property = m_data_typed->find_exists("Property");
        std::vector<std::shared_ptr<::cw::data::CWPyElement>> remove;
        for (const auto& child : property->m_children)
            if (child && (child->m_tag == "ImagePath" || child->m_tag == "ImagePaths"))
                remove.push_back(child);
        for (const auto& child : remove)
            property->remove(child);
        auto paths_element = ::cw::data::make_element("ImagePaths");
        for (const auto& info : m_imgpaths) {
            if (!info || info->m_path.empty())
                continue;
            auto stored_path = info->m_path;
            if (info->m_base && *info->m_base)
                stored_path = (*info->m_base)->m_path;
            auto image_path = ::cw::data::make_element("ImagePath", stored_path);
            if (!info->m_postype.empty() && info->m_postype != "Default")
                image_path->set("positiontype", info->m_postype);
            paths_element->append(image_path);
        }
        property->append(paths_element);
        m_data_typed->m_is_edited = true;
        // 外部指定画像は常にscaledimage=TrueにするPython仕様。
        if (auto root = m_data_typed->getroot())
            root->set("scaledimage", "True");
    }

    if (::cw::g_cwpy) {
        const auto shared = find_player_card(*this);
        Integer number = 0;
        if (shared) {
            const auto it = std::find(::cw::g_cwpy->m_pcards.begin(),
                                      ::cw::g_cwpy->m_pcards.end(), shared);
            if (it != ::cw::g_cwpy->m_pcards.end())
                number = static_cast<Integer>(std::distance(::cw::g_cwpy->m_pcards.begin(), it)) + 1;
        }
        if (number > 0)
            ::cw::g_cwpy->update_pcimage(number, true);
        // Pythonの二重exec_func相当は現行SDLメインループでは即時更新する。
    }
    return m_imgpaths;
}

void PlayerCard::update_levelup()
{
    auto cardimg = get_cardimg();
    if (CWPyCard::m_frame % 5 != 0) {
        CWPyCard::m_image = empty_surface();
    } else {
        CWPyCard::m_image = CWPyCard::get_animeimage();
        if (CWPyCard::m_frame == 15) {
            CWPyCard::m_status = CWPyCard::m_old_status;
            cardimg->set_levelimg(m_level);
            CWPyCard::m_frame = 0;
            return;
        }
    }
    ++CWPyCard::m_frame;
}

void PlayerCard::update_delete()
{
    if (!::cw::g_cwpy || !::cw::g_cwpy->m_ydata || !*::cw::g_cwpy->m_ydata ||
        !(*::cw::g_cwpy->m_ydata)->m_party || !*(*::cw::g_cwpy->m_ydata)->m_party) {
        throw std::logic_error("PlayerCard.update_delete requires an initialized party");
    }
    if (CWPyCard::m_old_status == "hidden")
        CWPyCard::hide();
    else
        CWPyCard::update_hide();
    if (CWPyCard::m_frame == 0) {
        release_animated_images();
        if (::cw::g_cwpy && ::cw::g_cwpy->m_ydata && *::cw::g_cwpy->m_ydata &&
            (*::cw::g_cwpy->m_ydata)->m_party && *(*::cw::g_cwpy->m_ydata)->m_party)
            (*(*::cw::g_cwpy->m_ydata)->m_party)->remove(find_player_card(*this));
    }
}

void PlayerCard::update_vanish()
{
    if (CWPyCard::m_old_status == "hidden")
        CWPyCard::hide();
    else
        CWPyCard::update_hide();
    if (CWPyCard::m_frame == 0 && ::cw::g_cwpy) {
        if (const auto self = find_player_card(*this)) {
            auto& pcards = ::cw::g_cwpy->m_pcards;
            pcards.erase(std::remove(pcards.begin(), pcards.end(), self), pcards.end());
        }
        unregister_card_layer(*this);
    }
}

void PlayerCard::lclick_event()
{
    // Character.is_inactive() が参照する状態も、CWPyCardの表示反転と同期。
    ::cw::character::Character::m_reversed = CWPyCard::m_reversed;
    const bool show_personal = ::cw::g_cwpy && ::cw::g_cwpy->m_setting &&
        ::cw::g_cwpy->m_setting->m_show_personal_cards;
    const bool camp = ::cw::g_cwpy && ::cw::g_cwpy->m_areaid == value_integer(::cw::g_AREA_CAMP);
    if (CWPyCard::m_reversed && !(show_personal && camp)) {
        rclick_event();
        return;
    }

    const auto runtime = ::cw::g_cwpy;
    if (!runtime)
        return;
    auto& cwpy = *runtime;
    // ダイアログ・音声・取引は、CWPy の共通イベントバックエンドを通して実行する。
    const auto shared = find_player_card(*this);
    const auto selected_header = runtime->m_selectedheader && *runtime->m_selectedheader
        ? *runtime->m_selectedheader : std::shared_ptr<::cw::header::CardHeader>{};
    TargetSelectionDispatchGuard target_guard(
        runtime.get(), runtime->is_curtained() && !area_in_trade(runtime->m_areaid) &&
            static_cast<bool>(selected_header));
    if (!target_guard) return;
    const auto owner = std::static_pointer_cast<::cw::sprite::card::CWPyCard>(shared);
    CardInputGuard input_guard(owner);
    if (!input_guard) return;
    if (!cwpy.is_curtained()) {
        cwpy.play_sound("click", false, 100, 1, 0, 0, false);
        if (!start_player_animation(*this, "click")) return;
        if (::cw::g_cwpy != runtime) return;
        if (const auto live = find_player_card(*this); !live || live.get() != shared.get()) return;
        if (cwpy.is_battlestatus()) {
            if (::cw::character::Character::is_inactive()) {
                cwpy.call_modaldlg("NOTICE", {{"text", Value(message_with_name(player_message("inactive", "%s is inactive"), m_name))}});
            } else if (::cw::character::Character::is_autoselectedpenalty(cwpy.m_selectedheader) && !cwpy.is_debugmode()) {
                cwpy.call_modaldlg("NOTICE", {{"text", Value(player_message("selected_penalty", "Selected penalty"))}});
            } else if (m_deck && !m_deck->m_hand.empty()) {
                cwpy.call_modaldlg("HANDVIEW", {{"selection", Value(
                    std::static_pointer_cast<::cw::lib::Object>(owner))}});
            }
        } else if (::cw::character::Character::is_inactive() && !area_in_trade(cwpy.m_areaid)) {
            cwpy.call_modaldlg("NOTICE", {{"text", Value(message_with_name(player_message("inactive", "%s is inactive"), m_name))}});
        } else {
            cwpy.call_modaldlg("CARDPOCKET", {{"selection", Value(
                std::static_pointer_cast<::cw::lib::Object>(owner))}});
        }
        return;
    }

    const auto selection_is_current = [&]() {
        return ::cw::g_cwpy == runtime && selected_header &&
            runtime->m_selectedheader && *runtime->m_selectedheader == selected_header;
    };
    const bool selected = selection_is_current();
    if (area_in_trade(cwpy.m_areaid) && selected) {
        if (!start_player_animation(*this, "click")) return;
        if (::cw::g_cwpy != runtime) return;
        if (const auto live = find_player_card(*this); !live || live.get() != shared.get()) return;
        if (!selection_is_current()) return;
        cwpy.trade("PLAYERCARD", shared ? std::optional<Value>(Value(std::static_pointer_cast<::cw::lib::Object>(
                       std::static_pointer_cast<::cw::sprite::base::CWPySprite>(shared)))) : std::nullopt,
                   std::nullopt, false, nullptr, -1, -1, true, true, std::nullopt,
                   false, true, false, true, 0);
    } else if (selected) {
        cwpy.play_sound("click", false, 100, 1, 0, 0, false);
        if (!start_player_animation(*this, "click")) return;
        if (::cw::g_cwpy != runtime) return;
        if (const auto live = find_player_card(*this); !live || live.get() != shared.get()) return;
        if (!selection_is_current()) return;
        if (cwpy.m_status == "Scenario")
            cwpy.call_modaldlg("USECARD", {});
        else if (cwpy.m_status == "ScenarioBattle")
            _select_action(shared ? std::static_pointer_cast<::cw::sprite::card::CWPyCard>(shared) : nullptr);
    } else if (cwpy.m_areaid == value_integer(::cw::g_AREA_BREAKUP)) {
        if (!start_player_animation(*this, "click")) return;
        if (::cw::g_cwpy != runtime) return;
        if (const auto live = find_player_card(*this); !live || live.get() != shared.get()) return;
        cwpy.dissolve_party(shared, true);
    } else if (cwpy.m_areaid == value_integer(::cw::g_AREA_CAMP)) {
        cwpy.play_sound("click", false, 100, 1, 0, 0, false);
        if (!start_player_animation(*this, "click")) return;
        if (::cw::g_cwpy != runtime) return;
        if (const auto live = find_player_card(*this); !live || live.get() != shared.get()) return;
        cwpy.call_modaldlg("CARDPOCKET", {{"selection", Value(
            std::static_pointer_cast<::cw::lib::Object>(owner))}});
    }
}

void PlayerCard::rclick_event()
{
    const auto runtime = ::cw::g_cwpy;
    if (!runtime)
        return;
    const auto shared = find_player_card(*this);
    CardInputGuard input_guard(
        std::static_pointer_cast<::cw::sprite::card::CWPyCard>(shared));
    if (!input_guard) return;
    // Py と同様、animate_sprite より前に効果音を鳴らす。 反転したプ
    // レイヤーカードの左クリックから rclick_event へ委ねる経路も含める。
    runtime->play_sound("click", false, 100, 1, 0, 0, false);
    if (!start_player_animation(*this, "click")) return;
    if (::cw::g_cwpy != runtime) return;
    if (const auto live = find_player_card(*this); !live || live.get() != shared.get()) return;
    runtime->call_modaldlg("CHARAINFO", {{"selection", Value(
        std::static_pointer_cast<::cw::lib::Object>(
            std::static_pointer_cast<::cw::sprite::card::CWPyCard>(shared)))}});
}

void PlayerCard::set_level(::cw::lib::Integer arg_value, bool arg_regulate, bool arg_debugedit, std::optional<std::shared_ptr<::cw::data::Party>> arg_backpack_party, bool arg_revert_cardpocket)
{
    auto cardimg = get_cardimg();
    ::cw::character::Player::set_level(arg_value, arg_regulate, arg_debugedit,
                                       arg_backpack_party, arg_revert_cardpocket);
    // Py と同様、共通のキャラクター設定処理が求めたレベルを表
    // 示する。 元の要求値をここで再代入すると補正を回避し、基底
    // が変更なしで戻った場合も XML を再書き込みしてしまう。
    cardimg->set_levelimg(m_level);
}

bool PlayerCard::adjust_level(bool arg_fromscenario)
{
    if (!::cw::g_cwpy || !::cw::g_cwpy->m_ydata || !*::cw::g_cwpy->m_ydata)
        throw std::logic_error("adjust_level requires ydata");
    bool result = false;
    Integer levelup = 0;
    const auto setting = ::cw::g_cwpy->m_setting;
    if (!(arg_fromscenario && ::cw::g_cwpy->is_debugmode() && setting && setting->m_no_levelup_in_debugmode)) {
        levelup = ::cw::character::Character::check_level();
        if (arg_fromscenario)
            levelup = std::max<Integer>(0, levelup);
    }
    const auto level = m_level;
    Integer base = 0;
    if (levelup != 0) {
        // Py と同様、必須の元レベルの称号は、レベルが変わる場合だけ読む。
        const auto special = player_special_coupons(*this);
        base = special.at("＠レベル原点");
        const auto n = base + levelup;
        if (arg_fromscenario) {
            if (levelup > 1) {
                start_player_animation(*this, "levelup");
                for (Integer i = 0; i < levelup - 1; ++i) {
                    start_player_animation(*this, "hide");
                    set_level(base + i + 1, false, false, std::nullopt, false);
                    start_player_animation(*this, "deal");
                }
                set_level(n, false, false, std::nullopt, false);
            } else {
                set_level(n, false, false, std::nullopt, false);
                start_player_animation(*this, "levelup");
            }
        } else {
            set_level(n, false, false, std::nullopt, false);
        }
    }

    if (arg_fromscenario || levelup != 0) {
        result = true;
        ::cw::g_cwpy->play_sound("harvest", true, 100, 1, 0, 0, false);
        start_player_animation(*this, "hide");
        if (arg_fromscenario)
            set_fullrecovery();
        CWPyCard::update_image(false, std::nullopt);
        start_player_animation(*this, "deal");
    }

    if (arg_fromscenario && levelup > 0) {
        auto party = (*::cw::g_cwpy->m_ydata)->m_party;
        // Py のアサート時期を維持し、必須のパーティー確認より前に回復を行う。
        if (!party || !*party)
            throw std::logic_error("level-up message requires party");
        {
            const auto text = player_message("level_up", "Level up");
            using MessageImage = std::tuple<std::shared_ptr<::cw::image::ImageInfo>, bool,
                                            std::optional<Value>,
                                            std::map<Integer, std::shared_ptr<::cw::lib::Surface>>>;
            const auto self = find_player_card(*this);
            if (!self)
                throw std::logic_error("level-up message requires the owning PlayerCard");
            const Value talker(std::static_pointer_cast<::cw::lib::Object>(
                std::static_pointer_cast<::cw::sprite::base::CWPySprite>(self)));
            const bool can_scaled = m_data_typed->getbool(".", Value("scaledimage"), false);
            std::vector<MessageImage> infos;
            for (const auto& info : m_imgpaths) {
                if (!info)
                    continue;
                auto display_info = copy_imageinfo(info);
                display_info->m_basecardtype = String("LargeCard");
                infos.emplace_back(display_info, can_scaled,
                                   talker,
                                   std::map<Integer, std::shared_ptr<::cw::lib::Surface>>{});
            }
            auto message = std::make_shared<::cw::sprite::message::MessageWindow>();
            message->initialize(text, {{0, player_message("ok", "OK")}}, infos,
                                talker, std::nullopt, std::nullopt, std::nullopt,
                                std::nullopt, std::nullopt, std::nullopt, std::nullopt,
                                false, std::nullopt, 0, m_versionhint_typed,
                                std::nullopt, 0, 1, std::nullopt, false, true, true);
            ::cw::g_cwpy->show_message(message);
            if (base != level || (*party)->m_is_suspendlevelup) {
                start_player_animation(*this, "hide");
                set_level(level, true, false, std::nullopt, true);
                CWPyCard::update_image(false, std::nullopt);
                start_player_animation(*this, "deal");
            }
        }
    }
    return result;
}

void PlayerCard::lost()
{
    ::cw::character::Player::lost();
    if (!::cw::g_cwpy)
        return;
    const auto pockets = m_cardpocket_typed; // Python と同様、trade が所有者のポケットから削除する間は pocket[:] のコピーを走査する。
    const auto move_to_trash = [this](const std::shared_ptr<::cw::header::CardHeader>& header) {
        if (!header || !::cw::g_cwpy)
            return;
        ::cw::g_cwpy->trade("TRASHBOX", std::nullopt, header, true, nullptr, -1, -1,
                             false, true, std::nullopt, false, false, false, true, 0);
    };
    for (const auto& header : std::get<0>(pockets)) move_to_trash(header);
    for (const auto& header : std::get<1>(pockets)) move_to_trash(header);
    for (const auto& header : std::get<2>(pockets)) move_to_trash(header);
}

::cw::lib::Integer CWPyCard__update_zoominout_calc_zoom(::cw::lib::Integer arg_zoom_val)
{
    return arg_zoom_val;
}

void PlayerCard_set_images_func()
{
    // Python の入れ子の exec_func に相当する処理は、PlayerCard::set_images
    // 本体で同期実行する。 この公開互換フックは、呼び出し側が登録したカード更新を一度進める。
    if (::cw::g_cwpy)
        ::cw::g_cwpy->update_pcimage(0, false);
}

void PlayerCard_set_images_func_func()
{
    if (::cw::g_cwpy)
        ::cw::g_cwpy->update_pcimage(0, false);
}

void _select_action(std::shared_ptr<::cw::sprite::card::CWPyCard> arg_sprite)
{
    if (!::cw::g_cwpy || !arg_sprite || !::cw::g_cwpy->m_selectedheader ||
        !*::cw::g_cwpy->m_selectedheader)
        return;
    const auto owner_value = (*::cw::g_cwpy->m_selectedheader)->get_owner();
    if (!owner_value)
        return;
    const auto* object = std::get_if<::cw::lib::ObjectPtr>(&owner_value->storage());
    const auto owner = object && *object
        ? std::dynamic_pointer_cast<::cw::character::Character>(*object) : nullptr;
    if (!owner)
        return;
    const auto runtime = ::cw::g_cwpy;
    const auto header = *runtime->m_selectedheader;
#if defined(CARDWIRTH_LOG_ENABLED)
    ::cardwirth::debug::log("Battle target commit begin: target=" + arg_sprite->get_showingname());
#endif
    owner->set_action(Value(std::static_pointer_cast<::cw::lib::Object>(arg_sprite)),
                      header, std::nullopt, false);
    if (::cw::g_cwpy != runtime || !runtime->m_selectedheader ||
        *runtime->m_selectedheader != header)
        return;
    runtime->clear_specialarea(false, false);
#if defined(CARDWIRTH_LOG_ENABLED)
    ::cardwirth::debug::log("Battle target commit end");
#endif
    if (runtime->m_selection && (*runtime->m_selection).get() == arg_sprite.get()) {
        const auto owner_card = std::dynamic_pointer_cast<::cw::sprite::card::CWPyCard>(owner);
        runtime->change_selection(arg_sprite, owner_card);
    }
}

std::tuple<std::vector<std::shared_ptr<::cw::image::ImageInfo>>, std::vector<bool>> imageinfos_to_pathdata(std::vector<std::shared_ptr<::cw::image::ImageInfo>> arg_infos)
{
    std::vector<std::shared_ptr<::cw::image::ImageInfo>> paths;
    std::vector<bool> can_loaded_scaledimages;
    for (const auto& info : arg_infos) {
        if (!info)
            continue;
        if (!info->m_path.empty()) {
            paths.push_back(copy_imageinfo(info));
            // Python版の areaid<0 または ScenarioData.can_loaded_scaledimage。
            bool can_scaled = !::cw::g_cwpy || ::cw::g_cwpy->m_areaid < 0;
            if (::cw::g_cwpy && ::cw::g_cwpy->m_areaid >= 0) {
                const auto sdata = current_sdata();
                can_scaled = sdata && sdata->m_can_loaded_scaledimage;
            }
            can_loaded_scaledimages.push_back(can_scaled);
            continue;
        }

        if (info->m_pcnumber <= 0)
            continue;
        // メニューカードの PC 画像を展開する。Party.members の Value
        // 列が Element/ElementTree を保持し、解釈できる場合だけ展開する。
        if (!::cw::g_cwpy || !::cw::g_cwpy->m_ydata || !*::cw::g_cwpy->m_ydata ||
            !(*::cw::g_cwpy->m_ydata)->m_party ||
            !*(*::cw::g_cwpy->m_ydata)->m_party)
            continue;
        const auto party = *(*::cw::g_cwpy->m_ydata)->m_party;
        const auto pi = info->m_pcnumber - 1;
        if (pi < 0 || static_cast<std::size_t>(pi) >= party->m_members.size())
            continue;
        auto member_tree = value_tree(party->m_members[static_cast<std::size_t>(pi)]);
        std::shared_ptr<::cw::data::CWPyElement> member;
        if (member_tree)
            member = member_tree->getroot();
        else
            member = value_element(party->m_members[static_cast<std::size_t>(pi)]);
        if (!member)
            continue;
        const bool can_scaled = read_bool(member, ".", "scaledimage", false);
        auto member_infos = get_infos(member);
        member_infos = ::cw::util::pick_up_basename_image(std::move(member_infos), true);
        for (const auto& member_info : member_infos) {
            if (!member_info)
                continue;
            auto path = copy_imageinfo(member_info);
            path->m_pcnumber = info->m_pcnumber;
            path->m_basecardtype = String("LargeCard");
            if (!path->m_path.empty()) path->m_path = ::cw::util::join_yadodir(path->m_path);
            paths.push_back(std::move(path));
            can_loaded_scaledimages.push_back(can_scaled);
        }
    }
    return {std::move(paths), std::move(can_loaded_scaledimages)};
}

void main()
{
}

}

