#pragma once

// cwx/importutils.dのC++20
// 移植。共通のCWXインターフェースだけに依存
// する。 Summary・Card・Areaは
// 相互参照するため、ここでは資源クラスを前方宣
// 言し、 公開の結果型からインクルードと所有権
// の循環を作らない。 取り込み処理は制約付き関
// 数テンプレートにし、モデルの宣言が揃ってから
// 実体化する。 別のOS専用モデルは設けない。

#include "cwxeditor_src/core/cwx/path.hpp"
#include "cwxeditor_src/core/cwx/types.hpp"
#include "cwxeditor_src/core/cwx/flag.hpp"
#include "cwxeditor_src/core/cwx/imagesize.hpp"
#include "cwxeditor_src/core/cwx/usecounter.hpp"
#include "cwxeditor_src/core/cwx/utils.hpp"
#include "cwxeditor_src/lib/portable.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

namespace cwxeditor::cwx {

namespace area {
class Area;
class Battle;
class Package;
class MenuCard;
}  // namespace area

namespace background {
class ImageCell;
}  // namespace background

namespace card {
class Card;
class CastCard;
class SkillCard;
class ItemCard;
class BeastCard;
class InfoCard;
class EffectCard;
class CardImage;
}  // namespace card

namespace flag {
class Flag;
class Step;
class Variant;
class FlagDir;
}  // namespace flag

namespace motion {
class Motion;
}  // namespace motion

namespace props {
class CProps;
}  // namespace props

namespace summary {
class Summary;
}  // namespace summary

namespace usecounter {
// 上で完全なカウンターヘッダーを読み込んでいるが、 従来の
// インクルード順に対応する前方宣言の並びにも宣言を残す。
class UseCounter;
}  // namespace usecounter

namespace importutils {

using String = ::cwxeditor::lib::String;
using StringView = ::cwxeditor::lib::StringView;
using Path = std::filesystem::path;

using AreaPtr = ::cwxeditor::lib::Owner<area::Area>;
using BattlePtr = ::cwxeditor::lib::Owner<area::Battle>;
using PackagePtr = ::cwxeditor::lib::Owner<area::Package>;
using CastPtr = ::cwxeditor::lib::Owner<card::CastCard>;
using SkillPtr = ::cwxeditor::lib::Owner<card::SkillCard>;
using ItemPtr = ::cwxeditor::lib::Owner<card::ItemCard>;
using BeastPtr = ::cwxeditor::lib::Owner<card::BeastCard>;
using InfoPtr = ::cwxeditor::lib::Owner<card::InfoCard>;
using FlagPtr = ::cwxeditor::lib::Owner<flag::Flag>;
using StepPtr = ::cwxeditor::lib::Owner<flag::Step>;
using VariantPtr = ::cwxeditor::lib::Owner<flag::Variant>;

/// 取り込みの設定。
struct ImportOption {
    types::ImportTypeReference1 materials{types::ImportTypeReference1::NoOverwrite};
    types::ImportTypeReference1 variables{types::ImportTypeReference1::Rename};
    types::ImportTypeReference2 casts{types::ImportTypeReference2::NoImport};
    types::ImportTypeReference2 skills{types::ImportTypeReference2::NoImport};
    types::ImportTypeReference2 items{types::ImportTypeReference2::NoImport};
    types::ImportTypeReference2 beasts{types::ImportTypeReference2::NoImport};
    types::ImportTypeReference2 infos{types::ImportTypeReference2::NoImport};
    types::ImportTypeReference2 areas{types::ImportTypeReference2::NoImport};
    types::ImportTypeReference2 battles{types::ImportTypeReference2::NoImport};
    types::ImportTypeReference2 packages{types::ImportTypeReference2::NoImport};
    types::ImportTypeIncluded includedFiles{types::ImportTypeIncluded::AsIs};
    types::ImportTypeIncluded includedBgImages{types::ImportTypeIncluded::AsIs};
    types::ImportTypeIncluded hands{types::ImportTypeIncluded::AsIs};
    types::ImportTypeIncluded beastsInMotions{types::ImportTypeIncluded::AsIs};
    bool overwriteScenarioInfo{false};
};

/// 取り込み計画内のファイルのコピー。
struct ImportFile {
    String dst;
    String src;
    bool overwrite{false};

    ImportFile() = default;
    ImportFile(String destination, String source, bool replace = false)
        : dst(std::move(destination)), src(std::move(source)), overwrite(replace) {}
};

/// 取り込み計画内のフラグのコピー。
struct ImportFlag {
    String path;
    FlagPtr flag;
    bool overwrite{false};

    ImportFlag() = default;
    ImportFlag(String destination, FlagPtr value, bool replace = false)
        : path(std::move(destination)), flag(std::move(value)),
          overwrite(replace) {}

    [[nodiscard]] String name() const;
};

/// 取り込み計画内のステップのコピー。
struct ImportStep {
    String path;
    StepPtr step;
    bool overwrite{false};

    ImportStep() = default;
    ImportStep(String destination, StepPtr value, bool replace = false)
        : path(std::move(destination)), step(std::move(value)),
          overwrite(replace) {}

    [[nodiscard]] String name() const;
};

/// 取り込み計画内のコモンのコピー。
struct ImportVariant {
    String path;
    VariantPtr variant;
    bool overwrite{false};

    ImportVariant() = default;
    ImportVariant(String destination, VariantPtr value, bool replace = false)
        : path(std::move(destination)), variant(std::move(value)),
          overwrite(replace) {}

    [[nodiscard]] String name() const;
};

/// 取り込み結果。呼び出し元が一覧の配置を適用する。
struct ImportResult {
    ::cwxeditor::lib::Array<ImportFile> materials;
    ::cwxeditor::lib::Dictionary<String, ::cwxeditor::lib::Array<ImportFlag>> flags;
    ::cwxeditor::lib::Dictionary<String, ::cwxeditor::lib::Array<ImportStep>> steps;
    ::cwxeditor::lib::Dictionary<String, ::cwxeditor::lib::Array<ImportVariant>> variants;
    ::cwxeditor::lib::Dictionary<std::uint64_t, CastPtr> casts;
    ::cwxeditor::lib::Dictionary<std::uint64_t, SkillPtr> skills;
    ::cwxeditor::lib::Dictionary<std::uint64_t, ItemPtr> items;
    ::cwxeditor::lib::Dictionary<std::uint64_t, BeastPtr> beasts;
    ::cwxeditor::lib::Dictionary<std::uint64_t, InfoPtr> infos;
    ::cwxeditor::lib::Dictionary<std::uint64_t, AreaPtr> areas;
    ::cwxeditor::lib::Dictionary<std::uint64_t, BattlePtr> battles;
    ::cwxeditor::lib::Dictionary<std::uint64_t, PackagePtr> packages;
};

namespace detail {

// モデルにはD形式のプロパティを移したname()/id()
// と、公開データメンバーが混在する。 この小
// さな補助処理で両方の表記を受け付け、取り込
// み手順をアクセス方法の違いから分離する。
template <typename T>
[[nodiscard]] decltype(auto) objectRef(T& value) {
    if constexpr (requires { value.get(); }) return *value;
    else if constexpr (std::is_pointer_v<std::remove_reference_t<T>>) return *value;
    else return (value);
}

template <typename T>
[[nodiscard]] decltype(auto) objectRef(const T& value) {
    if constexpr (requires { value.get(); }) return *value;
    else if constexpr (std::is_pointer_v<std::remove_reference_t<T>>) return *value;
    else return (value);
}

template <typename T>
[[nodiscard]] std::uint64_t idOf(const T& value) {
    const auto& object = objectRef(value);
    if constexpr (requires { object.id(); }) {
        return static_cast<std::uint64_t>(object.id());
    } else if constexpr (requires { object.id; }) {
        return static_cast<std::uint64_t>(object.id);
    } else {
        return 0;
    }
}

template <typename T>
void setId(T& value, std::uint64_t id) {
    auto& object = objectRef(value);
    if constexpr (requires { object.id(id); }) {
        object.id(id);
    } else if constexpr (requires { object.id = id; }) {
        object.id = id;
    }
}

template <typename T>
[[nodiscard]] String nameOf(const T& value) {
    const auto& object = objectRef(value);
    if constexpr (requires { object.name(); }) {
        return object.name();
    } else if constexpr (requires { object.name; }) {
        return object.name;
    } else {
        return String{};
    }
}

template <typename T>
void setName(T& value, String name) {
    auto& object = objectRef(value);
    if constexpr (requires { object.name(std::move(name)); }) {
        object.name(std::move(name));
    } else if constexpr (requires { object.name = std::move(name); }) {
        object.name = std::move(name);
    }
}

template <typename T>
[[nodiscard]] auto cloneOwner(const ::cwxeditor::lib::Owner<T>& value)
    -> ::cwxeditor::lib::Owner<T> {
    if (!value) return ::cwxeditor::lib::Owner<T>{};
    if constexpr (requires { value->dupCast(); }) {
        return value->dupCast();
    } else if constexpr (requires { value->dupSkill(); }) {
        return value->dupSkill();
    } else if constexpr (requires { value->dupItem(); }) {
        return value->dupItem();
    } else if constexpr (requires { value->dupBeast(); }) {
        return value->dupBeast();
    } else if constexpr (requires { value->dupInfo(); }) {
        return value->dupInfo();
    } else if constexpr (requires { value->dup(); }) {
        auto duplicate = value->dup();
        if constexpr (std::is_same_v<decltype(duplicate),
                                     ::cwxeditor::lib::Owner<T>>) {
            return duplicate;
        } else {
            return std::dynamic_pointer_cast<T>(duplicate);
        }
    } else if constexpr (std::is_copy_constructible_v<T>) {
        return std::make_shared<T>(*value);
    } else {
        return ::cwxeditor::lib::Owner<T>{};
    }
}

template <typename To, typename From>
[[nodiscard]] ::cwxeditor::lib::Owner<To> dynamicOwner(
    const ::cwxeditor::lib::Owner<From>& value) {
    if (!value) return ::cwxeditor::lib::Owner<To>{};
    if constexpr (std::is_same_v<To, From>) {
        return value;
    } else {
        return std::dynamic_pointer_cast<To>(value);
    }
}

template <typename T>
void setCounter(const ::cwxeditor::lib::Owner<T>& value,
                const ::cwxeditor::lib::Owner<usecounter::UseCounter>& counter) {
    if (!value) return;
    if constexpr (requires { value->setUseCounter(counter); }) {
        value->setUseCounter(counter);
    } else if constexpr (requires { value->useCounter(counter); }) {
        value->useCounter(counter);
    } else if constexpr (requires { value->setUseCounter(counter, nullptr); }) {
        value->setUseCounter(counter, {});
    }
}

template <typename T>
[[nodiscard]] bool exists(const T& value) {
    if constexpr (std::is_convertible_v<T, bool>) return static_cast<bool>(value);
    else return true;
}

template <typename Summary>
[[nodiscard]] auto findPath(Summary& value, StringView path) {
    return value.findCWXPath(path);
}

template <typename Summary>
[[nodiscard]] Path scenarioPath(const Summary& value) {
    if constexpr (requires { value.scenarioPath(); }) {
        auto pathValue = value.scenarioPath();
        if constexpr (std::is_same_v<std::remove_cvref_t<decltype(pathValue)>,
                                     Path>) {
            return pathValue;
        } else {
            const auto text = String(pathValue);
            const auto* first = reinterpret_cast<const char8_t*>(text.data());
            return Path(std::u8string(first, text.size()));
        }
    } else if constexpr (requires { value.scenarioPath; }) {
        const auto text = String(value.scenarioPath);
        const auto* first = reinterpret_cast<const char8_t*>(text.data());
        return Path(std::u8string(first, text.size()));
    } else {
        return Path{};
    }
}

template <typename Summary>
[[nodiscard]] String scenarioName(const Summary& value) {
    if constexpr (requires { value.scenarioName(); }) return value.scenarioName();
    else if constexpr (requires { value.scenarioName; }) return value.scenarioName;
    else return String{};
}

template <typename Summary>
[[nodiscard]] String author(const Summary& value) {
    if constexpr (requires { value.author(); }) return value.author();
    else if constexpr (requires { value.author; }) return value.author;
    else return String{};
}

template <typename Summary>
[[nodiscard]] auto flagRoot(Summary& value) {
    if constexpr (requires { value.flagDirRoot(); }) return value.flagDirRoot();
    else if constexpr (requires { value.flagDirRoot; }) return value.flagDirRoot;
    else return std::shared_ptr<flag::FlagDir>{};
}

template <typename T>
[[nodiscard]] bool pathExists(const T& value) {
    std::error_code error;
    return std::filesystem::exists(value, error) && !error;
}

template <typename Map>
[[nodiscard]] std::vector<std::uint64_t> sortedKeys(const Map& map) {
    std::vector<std::uint64_t> result;
    result.reserve(map.size());
    for (const auto& [key, value] : map) {
        (void)value;
        result.push_back(key);
    }
    std::sort(result.begin(), result.end());
    return result;
}

template <typename Map>
[[nodiscard]] std::uint64_t nextId(const Map& map, std::uint64_t fallback = 1) {
    const auto ids = sortedKeys(map);
    return ids.empty() ? fallback : ids.back() + 1U;
}

template <typename Id>
[[nodiscard]] std::uint64_t numericId(const Id& value) {
    if constexpr (requires { value.id; }) {
        return static_cast<std::uint64_t>(value.id);
    } else {
        return static_cast<std::uint64_t>(value);
    }
}

template <typename Id>
[[nodiscard]] String stringId(const Id& value) {
    if constexpr (requires { value.id; }) {
        return String(value.id);
    } else {
        return std::to_string(static_cast<std::uint64_t>(value));
    }
}

template <typename Summary, typename Yield>
void forEachArea(const Summary& summary, Yield&& yield) {
    if constexpr (requires { summary.areas(); }) {
        for (const auto& value : summary.areas()) yield(value);
    }
}

template <typename Summary, typename Yield>
void forEachBattle(const Summary& summary, Yield&& yield) {
    if constexpr (requires { summary.battles(); }) {
        for (const auto& value : summary.battles()) yield(value);
    }
}

template <typename Summary, typename Yield>
void forEachPackage(const Summary& summary, Yield&& yield) {
    if constexpr (requires { summary.packages(); }) {
        for (const auto& value : summary.packages()) yield(value);
    }
}

template <typename Summary, typename Yield>
void forEachCast(const Summary& summary, Yield&& yield) {
    if constexpr (requires { summary.casts(); }) {
        for (const auto& value : summary.casts()) yield(value);
    }
}

template <typename Summary, typename Yield>
void forEachSkill(const Summary& summary, Yield&& yield) {
    if constexpr (requires { summary.skills(); }) {
        for (const auto& value : summary.skills()) yield(value);
    }
}

template <typename Summary, typename Yield>
void forEachItem(const Summary& summary, Yield&& yield) {
    if constexpr (requires { summary.items(); }) {
        for (const auto& value : summary.items()) yield(value);
    }
}

template <typename Summary, typename Yield>
void forEachBeast(const Summary& summary, Yield&& yield) {
    if constexpr (requires { summary.beasts(); }) {
        for (const auto& value : summary.beasts()) yield(value);
    }
}

template <typename Summary, typename Yield>
void forEachInfo(const Summary& summary, Yield&& yield) {
    if constexpr (requires { summary.infos(); }) {
        for (const auto& value : summary.infos()) yield(value);
    }
}

template <typename Summary, typename Yield>
void forEachResource(const Summary& summary, Yield&& yield) {
    forEachArea(summary, yield);
    forEachBattle(summary, yield);
    forEachPackage(summary, yield);
    forEachCast(summary, yield);
    forEachSkill(summary, yield);
    forEachItem(summary, yield);
    forEachBeast(summary, yield);
    forEachInfo(summary, yield);
}

template <typename Summary, typename Yield>
void forEachChild(const Summary& summary, Yield&& yield) {
    forEachResource(summary, [&](const auto& resource) {
        if constexpr (requires { resource->cwxChilds(); }) {
            for (const auto& child : resource->cwxChilds()) {
                if (auto object = child.lock()) yield(std::move(object));
            }
        }
    });
}

template <typename SummaryList>
[[nodiscard]] std::uint64_t firstImportedId(const SummaryList& existing) {
    std::uint64_t result = 1;
    for (const auto& value : existing) result = std::max(result, idOf(value) + 1U);
    return result;
}

template <typename T, typename Map, typename SummaryList, typename ToID>
void renumbering(const SummaryList& existing, Map& table,
                 const lib::Owner<usecounter::UseCounter>& counter,
                 ToID toId) {
    const auto keys = sortedKeys(table);
    if (keys.empty()) return;

    auto next = firstImportedId(existing);
    // D版と同じく、まず衝突しない大きな
    // 仮IDへ参照を移す。 その後、コピ
    // ー元の昇順で最終IDを割り当てる。
    for (std::size_t index = 0; index < keys.size(); ++index) {
        const auto old = keys[index];
        auto& value = table.at(old);
        const auto temporary = std::numeric_limits<std::uint64_t>::max() -
                                static_cast<std::uint64_t>(keys.size()) + index;
        setId(value, temporary);
        if (counter) counter->change(toId(old), toId(temporary));
    }

    lib::Dictionary<std::uint64_t, lib::Owner<T>> moved;
    for (std::size_t index = 0; index < keys.size(); ++index) {
        const auto old = keys[index];
        auto& value = table.at(old);
        const auto final_id = next++;
        const auto old_id = idOf(value);
        setId(value, final_id);
        if (counter) counter->change(toId(old_id), toId(final_id));
        moved.emplace(final_id, value);
    }
    table = std::move(moved);
}

template <typename ID, typename T, typename Map, typename Getter,
          typename Prefix>
bool importReferences(types::ImportTypeReference2 type, Getter&& getter,
                      Map& table,
                      const lib::Owner<usecounter::UseCounter>& counter,
                      StringView prefix, Prefix&& prefixArea) {
    if (type == types::ImportTypeReference2::NoImport) return false;
    bool changed = false;
    for (const auto& id : counter->template keys<ID>()) {
        const auto number = numericId(id);
        if (table.contains(number)) continue;
        auto source = getter(number);
        if (!source) continue;
        auto value = cloneOwner(source);
        if (!value) continue;
        if (prefixArea()) setName(*value, String(prefix) + "\\" + nameOf(*value));
        setCounter(value, counter);
        table.emplace(number, std::move(value));
        changed = true;
    }
    return changed;
}

template <typename T, typename Map>
bool putDirect(const lib::Owner<path::CWXPath>& object, Map& table,
               const lib::Owner<usecounter::UseCounter>& counter) {
    const auto source = dynamicOwner<T>(object);
    if (!source) return false;
    auto value = cloneOwner(source);
    if (!value) return false;
    auto id = idOf(*value);
    setCounter(value, counter);
    table[id] = std::move(value);
    return true;
}

[[nodiscard]] inline String utf8Path(const Path& value) {
    if (value.empty()) return String{};
    const auto u8 = value.u8string();
    return String(reinterpret_cast<const char*>(u8.data()), u8.size());
}

[[nodiscard]] inline Path appendPath(const Path& parent, StringView child) {
    if (child.empty()) return parent;
    const auto* first = reinterpret_cast<const char8_t*>(child.data());
    return parent / std::filesystem::path(std::u8string(first, child.size()));
}

template <typename T>
[[nodiscard]] decltype(auto) imageObject(T& value) {
    if constexpr (requires { value.get(); }) return *value;
    else if constexpr (std::is_pointer_v<std::remove_reference_t<T>>) return *value;
    else return (value);
}

template <typename T>
[[nodiscard]] std::uint64_t linkIdOf(const T& value) {
    const auto& object = objectRef(value);
    if constexpr (requires { object.linkId(); }) {
        return static_cast<std::uint64_t>(object.linkId());
    } else if constexpr (requires { object.linkId; }) {
        return static_cast<std::uint64_t>(object.linkId);
    } else {
        return 0;
    }
}

template <typename T>
void setLinkId(T& value, const std::uint64_t id) {
    auto& object = objectRef(value);
    if constexpr (requires { object.linkId(id); }) {
        object.linkId(id);
    } else if constexpr (requires { object.linkId = id; }) {
        object.linkId = id;
    }
}

template <typename T>
[[nodiscard]] bool holdOf(const T& value) {
    const auto& object = objectRef(value);
    if constexpr (requires { object.hold(); }) {
        return static_cast<bool>(object.hold());
    } else if constexpr (requires { object.hold; }) {
        return static_cast<bool>(object.hold);
    } else {
        return false;
    }
}

template <typename T>
void setHold(T& value, const bool hold) {
    auto& object = objectRef(value);
    if constexpr (requires { object.hold(hold); }) {
        object.hold(hold);
    } else if constexpr (requires { object.hold = hold; }) {
        object.hold = hold;
    }
}

template <typename T>
[[nodiscard]] String imagePathOf(const T& value) {
    const auto& object = imageObject(value);
    if constexpr (requires { object.path(); }) {
        return String(object.path());
    } else if constexpr (requires { object.path; }) {
        return String(object.path);
    } else {
        return String{};
    }
}

template <typename T>
bool setImagePath(T& value, String pathValue) {
    auto& object = imageObject(value);
    if constexpr (requires { object.path(std::move(pathValue)); }) {
        object.path(std::move(pathValue));
        return true;
    } else if constexpr (requires { object.path = std::move(pathValue); }) {
        object.path = std::move(pathValue);
        return true;
    } else {
        using Image = std::remove_cvref_t<decltype(object)>;
        if constexpr (requires { object.positionType();
                                 Image(String{}, object.positionType()); }) {
            if constexpr (requires {
                              value = std::make_shared<Image>(
                                  String{}, object.positionType());
                          }) {
                value = std::make_shared<Image>(
                    std::move(pathValue), object.positionType());
                return true;
            } else if constexpr (requires {
                                     value = Image(String{},
                                                   object.positionType());
                                 }) {
                value = Image(std::move(pathValue), object.positionType());
                return true;
            }
        }
        return false;
    }
}

template <typename T>
[[nodiscard]] bool isFileImage(T& value) {
    auto& object = imageObject(value);
    if constexpr (requires { object.type(); }) {
        return object.type() == types::CardImageType::File;
    } else if constexpr (requires { object.type; }) {
        return object.type == types::CardImageType::File;
    } else {
        return !imagePathOf(value).empty();
    }
}

template <typename T, typename Paths>
void setPaths(T& value, Paths&& paths) {
    if constexpr (requires { value.paths(std::forward<Paths>(paths)); }) {
        value.paths(std::forward<Paths>(paths));
    } else if constexpr (requires { value.setPaths(std::forward<Paths>(paths)); }) {
        value.setPaths(std::forward<Paths>(paths));
    }
}

template <typename T, typename Props>
[[nodiscard]] auto makeReferenceCard(const Props& prop,
                                     const std::uint64_t id)
    -> lib::Owner<T> {
    if constexpr (requires { typename std::remove_cvref_t<
                                  decltype(std::declval<T&>().paths())>; }) {
        using Paths = std::remove_cvref_t<decltype(std::declval<T&>().paths())>;
        if constexpr (requires { T(prop.sys(), id, String{}, Paths{}, String{}); }) {
            return std::make_shared<T>(prop.sys(), id, String{}, Paths{}, String{});
        } else if constexpr (requires { T(id, String{}, Paths{}, String{}); }) {
            return std::make_shared<T>(id, String{}, Paths{}, String{});
        }
    }
    return lib::Owner<T>{};
}

template <typename Map, typename Existing>
[[nodiscard]] std::uint64_t nextResourceId(const Map& table,
                                            const Existing& existing) {
    std::uint64_t result = 1;
    for (const auto& value : existing) result = std::max(result, idOf(value) + 1U);
    for (const auto& [id, value] : table) {
        (void)value;
        result = std::max(result, id + 1U);
    }
    return result;
}

template <typename OwnerType, typename List, typename CardPtr>
bool replaceCard(OwnerType& owner, List& list, const CardPtr& oldValue,
                 const CardPtr& newValue, const std::size_t index) {
    if constexpr (requires { owner.remove(oldValue); owner.insert(index, newValue, true); }) {
        owner.remove(oldValue);
        owner.insert(index, newValue, true);
        return true;
    } else if constexpr (requires { owner.remove(oldValue); owner.insert(index, newValue); }) {
        owner.remove(oldValue);
        owner.insert(index, newValue);
        return true;
    } else if constexpr (requires { list[index] = newValue; }) {
        list[index] = newValue;
        return true;
    } else {
        return false;
    }
}

template <typename OwnerType, typename List, typename Map, typename Existing,
          typename Getter, typename Props>
void importHandList(OwnerType& owner, List& list, Map& table,
                    const Existing& existing, Getter&& get,
                    const Props& prop, const types::ImportTypeIncluded type) {
    if (type == types::ImportTypeIncluded::AsIs) return;
    for (std::size_t index = 0; index < list.size(); ++index) {
        auto current = list[index];
        if (!current) continue;
        const auto link = linkIdOf(current);
        if (type == types::ImportTypeIncluded::Exclude) {
            if (link != 0U) continue;
            const auto id = nextResourceId(table, existing);
            auto linked = makeReferenceCard<std::remove_cvref_t<decltype(*current)>>(prop,
                                                                                      idOf(current));
            if (!linked) continue;
            setLinkId(linked, id);
            setHold(linked, holdOf(current));
            setId(current, id);
            setHold(current, false);
            if (!replaceCard(owner, list, current, linked, index)) continue;
            table[id] = std::move(current);
        } else if (link != 0U) {
            auto found = table.find(link);
            auto source = found == table.end() ? get(link) : found->second;
            if (!source) continue;
            auto replacement = cloneOwner(source);
            if (!replacement) continue;
            setId(replacement, idOf(current));
            setHold(replacement, holdOf(current));
            replaceCard(owner, list, current, replacement, index);
        }
    }
}

template <typename Cast, typename Props, typename Summary, typename Result>
void processHands(Cast& cast, const Props& prop, Summary& to, Summary& from,
                  const types::ImportTypeIncluded type, Result& result) {
    if (type == types::ImportTypeIncluded::AsIs) return;
    if constexpr (requires { cast.skills(); to.skills(); from.skill(std::uint64_t{}); }) {
        auto&& list = cast.skills();
        importHandList(cast, list, result.skills, to.skills(),
                       [&](std::uint64_t id) { return from.skill(id); }, prop, type);
    }
    if constexpr (requires { cast.items(); to.items(); from.item(std::uint64_t{}); }) {
        auto&& list = cast.items();
        importHandList(cast, list, result.items, to.items(),
                       [&](std::uint64_t id) { return from.item(id); }, prop, type);
    }
    if constexpr (requires { cast.beasts(); to.beasts(); from.beast(std::uint64_t{}); }) {
        auto&& list = cast.beasts();
        importHandList(cast, list, result.beasts, to.beasts(),
                       [&](std::uint64_t id) { return from.beast(id); }, prop, type);
    }
}

template <typename Motion, typename Props, typename Result, typename Summary>
void processMotion(Motion& value, const Props& prop, Result& result,
                   Summary& from, const types::ImportTypeIncluded type) {
    if (type == types::ImportTypeIncluded::AsIs) return;
    if constexpr (requires { value.beast(); value.newBeast(lib::Owner<card::BeastCard>{}); }) {
        auto beast = value.beast();
        if (!beast) return;
        const auto link = linkIdOf(beast);
        if (type == types::ImportTypeIncluded::Exclude) {
            if (link != 0U) return;
            const auto id = nextResourceId(result.beasts, result.beasts);
            auto linked = makeReferenceCard<card::BeastCard>(prop, idOf(beast));
            if (!linked) return;
            setLinkId(linked, id);
            value.newBeast(linked);
            setId(beast, id);
            result.beasts[id] = std::move(beast);
        } else if (link != 0U) {
            auto found = result.beasts.find(link);
            auto source = found == result.beasts.end() ? from.beast(link) : found->second;
            if (!source) return;
            auto replacement = cloneOwner(source);
            if (replacement) value.newBeast(replacement);
        }
    }
}

template <typename Props>
[[nodiscard]] String exportedImageName(const Props& prop, StringView scenario,
                                        StringView authorValue,
                                        StringView cardName) {
    if constexpr (requires { prop.requiredMsgs().exportedImageName;
                              prop.requiredMsgs().exportedImageNameWithAuthor;
                              prop.requiredMsgs().exportedImageNameWithCard; }) {
        String base;
        if (authorValue.empty()) {
            base = utils::tryFormat(
                static_cast<String>(prop.requiredMsgs().exportedImageName), scenario);
        } else {
            base = utils::tryFormat(
                static_cast<String>(prop.requiredMsgs().exportedImageNameWithAuthor),
                scenario, authorValue);
        }
        return utils::tryFormat(
            static_cast<String>(prop.requiredMsgs().exportedImageNameWithCard), base,
            cardName);
    } else {
        String result(scenario);
        if (!authorValue.empty()) {
            result += "(";
            result += authorValue;
            result += ")";
        }
        if (!cardName.empty()) {
            result += "_";
            result += cardName;
        }
        return result;
    }
}

template <typename Node, typename Props, typename Put, typename Include>
void processImages(Node& value, const Props& prop, StringView scenario,
                   StringView authorValue,
                   const types::ImportTypeIncluded files,
                   const types::ImportTypeIncluded bgImages, Put&& putBinImg,
                   Include&& includeImg) {
    if (files != types::ImportTypeIncluded::AsIs) {
        if constexpr (requires { value.paths(); }) {
            auto images = value.paths();
            for (auto& image : images) {
                if (!isFileImage(image)) continue;
                const auto oldPath = imagePathOf(image);
                if (files == types::ImportTypeIncluded::Exclude &&
                    utils::isBinImg(oldPath)) {
                    const auto name = nameOf(value);
                    setImagePath(image, putBinImg(
                                           exportedImageName(prop, scenario,
                                                             authorValue, name),
                                           oldPath));
                } else if (files == types::ImportTypeIncluded::Include &&
                           !oldPath.empty()) {
                    setImagePath(image, includeImg(oldPath));
                }
            }
            setPaths(value, std::move(images));
        }
    }

    if constexpr (requires { value.cellName(); value.path(); }) {
        const auto oldPath = String(value.path());
        if (bgImages != types::ImportTypeIncluded::Exclude &&
            utils::isBinImg(oldPath)) {
            setImagePath(value, putBinImg(exportedImageName(
                                               prop, scenario, authorValue,
                                               String(value.cellName())),
                                           oldPath));
        } else if (bgImages == types::ImportTypeIncluded::Include) {
            if (!oldPath.empty()) setImagePath(value, includeImg(oldPath));
        }
    }
}

template <typename Node>
void overwriteScenarioInfo(Node& value, StringView scenario, StringView authorValue) {
    if constexpr (requires { value.scenario(); value.author(); }) {
        if constexpr (requires { value.scenario(String{}); }) value.scenario(String(scenario));
        if constexpr (requires { value.author(String{}); }) value.author(String(authorValue));
    } else if constexpr (requires { value.scenario; value.author; }) {
        value.scenario = String(scenario);
        value.author = String(authorValue);
    }
}

template <typename Target, typename Callback>
void visitKnown(const lib::Owner<path::CWXPath>& value, Callback& callback) {
    if (auto object = dynamicOwner<Target>(value)) callback(*object);
}

template <typename... Targets, typename Callback>
void walkKnownImpl(const lib::Owner<path::CWXPath>& value, Callback& callback,
                   std::unordered_set<const void*>& visited) {
    if (!value) return;
    const auto identity = dynamic_cast<const void*>(value.get());
    if (!visited.insert(identity).second) return;
    // Dは現在のノードを変える前にcwxChildsを取得する。 Motion
    // が内包する召喚獣を外部化し、子を置き換える場合に必要となる。
    const auto children = value->cwxChilds();
    (visitKnown<Targets>(value, callback), ...);
    for (const auto& child : children) {
        if (auto object = child.lock()) {
            walkKnownImpl<Targets...>(object, callback, visited);
        }
    }
}

template <typename... Targets, typename Callback>
void walkKnown(const lib::Owner<path::CWXPath>& value, Callback& callback) {
    std::unordered_set<const void*> visited;
    walkKnownImpl<Targets...>(value, callback, visited);
}

}  // namespace detail

/// 取り込み計画を作る。SummaryとCPropsをテン
/// プレート引数にし、 公開モジュールの循環インクルードを
/// 避けつつ、具体的なSummaryごとにDと同じ呼び出し
/// 形を保つ。 モデル側はDでも使うfindCWXPath
/// ・scenarioPath・scenarioName
/// ・author・flagDirRootを提供する。
template <typename CProps, typename Summary>
[[nodiscard]] ImportResult importResource(
    const CProps& prop, Summary& to, Summary& from,
    std::span<const String> resource_paths, const ImportOption& option) {
    (void)prop;
    ImportResult result;
    const auto counter = std::make_shared<usecounter::UseCounter>(
        lib::Owner<path::CWXPath>{});

    // 直接指定された資源を先にコピーする。Dの動的
    // キャストと同じく具体的な型から一般的な型の順
    // で調べ、 Areaの派生オブジェクトを単な
    // るCWXPathとして扱わないようにする。
    for (const auto& resource_path : resource_paths) {
        auto object = detail::findPath(from, resource_path);
        if (!object) {
            throw std::invalid_argument("Can't import: " + resource_path);
        }
        bool accepted = false;
        accepted = detail::putDirect<area::Area>(object, result.areas, counter);
        if (!accepted)
            accepted = detail::putDirect<area::Battle>(object, result.battles, counter);
        if (!accepted)
            accepted = detail::putDirect<area::Package>(object, result.packages, counter);
        if (!accepted)
            accepted = detail::putDirect<card::CastCard>(object, result.casts, counter);
        if (!accepted)
            accepted = detail::putDirect<card::SkillCard>(object, result.skills, counter);
        if (!accepted)
            accepted = detail::putDirect<card::ItemCard>(object, result.items, counter);
        if (!accepted)
            accepted = detail::putDirect<card::BeastCard>(object, result.beasts, counter);
        if (!accepted)
            accepted = detail::putDirect<card::InfoCard>(object, result.infos, counter);
        if (!accepted) {
            throw std::invalid_argument("Can't import: " + resource_path);
        }
    }

    // 参照を通じて追加の資源が必要になる場合がある。コピ
    // ーしたカードが別のカードやエリアを参照できるため
    // 、 カウンターが増えなくなるまで走査を繰り返す。
    const auto sourceName = detail::scenarioName(from);
    auto safeSourceName = sourceName;
    safeSourceName.erase(
        std::remove(safeSourceName.begin(), safeSourceName.end(), '\\'),
        safeSourceName.end());
    if (safeSourceName.empty()) safeSourceName = "_";
    for (;;) {
        bool grew = false;
        if constexpr (requires { from.area(std::uint64_t{}); }) {
            grew = detail::importReferences<usecounter::AreaId, area::Area>(
                       option.areas,
                       [&](std::uint64_t id) { return from.area(id); },
                       result.areas, counter, safeSourceName, [] { return true; }) || grew;
        }
        if constexpr (requires { from.battle(std::uint64_t{}); }) {
            grew = detail::importReferences<usecounter::BattleId, area::Battle>(
                       option.battles,
                       [&](std::uint64_t id) { return from.battle(id); },
                       result.battles, counter, safeSourceName, [] { return true; }) || grew;
        }
        if constexpr (requires { from.cwPackage(std::uint64_t{}); }) {
            grew = detail::importReferences<usecounter::PackageId, area::Package>(
                       option.packages,
                       [&](std::uint64_t id) { return from.cwPackage(id); },
                       result.packages, counter, safeSourceName, [] { return true; }) || grew;
        }
        if constexpr (requires { from.cwCast(std::uint64_t{}); }) {
            grew = detail::importReferences<usecounter::CastId, card::CastCard>(
                       option.casts,
                       [&](std::uint64_t id) { return from.cwCast(id); },
                       result.casts, counter, safeSourceName, [] { return false; }) || grew;
        }
        if constexpr (requires { from.skill(std::uint64_t{}); }) {
            grew = detail::importReferences<usecounter::SkillId, card::SkillCard>(
                       option.skills,
                       [&](std::uint64_t id) { return from.skill(id); },
                       result.skills, counter, safeSourceName, [] { return false; }) || grew;
        }
        if constexpr (requires { from.item(std::uint64_t{}); }) {
            grew = detail::importReferences<usecounter::ItemId, card::ItemCard>(
                       option.items,
                       [&](std::uint64_t id) { return from.item(id); },
                       result.items, counter, safeSourceName, [] { return false; }) || grew;
        }
        if constexpr (requires { from.beast(std::uint64_t{}); }) {
            grew = detail::importReferences<usecounter::BeastId, card::BeastCard>(
                       option.beasts,
                       [&](std::uint64_t id) { return from.beast(id); },
                       result.beasts, counter, safeSourceName, [] { return false; }) || grew;
        }
        if constexpr (requires { from.info(std::uint64_t{}); }) {
            grew = detail::importReferences<usecounter::InfoId, card::InfoCard>(
                       option.infos,
                       [&](std::uint64_t id) { return from.info(id); },
                       result.infos, counter, safeSourceName, [] { return false; }) || grew;
        }
        if (!grew) break;
    }

    // 取り込み先Summaryと衝突しないIDにする。
    // 配置先のコレクション操作は宣言時点では必須にし
    // ないが、具体的なSummaryはすべて提供する。
    if constexpr (requires { to.areas(); }) {
        detail::renumbering<area::Area>(to.areas(), result.areas, counter,
                                        usecounter::toAreaId);
    }
    if constexpr (requires { to.battles(); }) {
        detail::renumbering<area::Battle>(to.battles(), result.battles, counter,
                                          usecounter::toBattleId);
    }
    if constexpr (requires { to.packages(); }) {
        detail::renumbering<area::Package>(to.packages(), result.packages, counter,
                                           usecounter::toPackageId);
    }
    if constexpr (requires { to.casts(); }) {
        detail::renumbering<card::CastCard>(to.casts(), result.casts, counter,
                                            usecounter::toCastId);
    }
    if constexpr (requires { to.skills(); }) {
        detail::renumbering<card::SkillCard>(to.skills(), result.skills, counter,
                                             usecounter::toSkillId);
    }
    if constexpr (requires { to.items(); }) {
        detail::renumbering<card::ItemCard>(to.items(), result.items, counter,
                                            usecounter::toItemId);
    }
    if constexpr (requires { to.beasts(); }) {
        detail::renumbering<card::BeastCard>(to.beasts(), result.beasts, counter,
                                             usecounter::toBeastId);
    }
    if constexpr (requires { to.infos(); }) {
        detail::renumbering<card::InfoCard>(to.infos(), result.infos, counter,
                                            usecounter::toInfoId);
    }

    const auto source_root = detail::scenarioPath(from);
    const auto target_root = detail::scenarioPath(to);
    const auto inner_folder = utils::createNewFileName(
        target_root / source_root.filename(), true);
    const auto source_folder = utils::createNewFileName(
        target_root / inner_folder.filename(), true);
    const auto new_folder = source_folder.filename();

    // 外部素材はPathIdの利用元から計画する。バイナリー内容は
    // オブジェクト内に保ち、 実在するファイルだけをコピーする。
    if (option.materials != types::ImportTypeReference1::NoImport) {
        if (counter) {
            for (const auto& path_id : counter->template keys<usecounter::PathId>()) {
                if (path_id.isBinData()) continue;
                const auto relative = String(path_id);
                const auto source = detail::appendPath(source_root, relative);
                if (!detail::pathExists(source)) continue;
                if (option.materials == types::ImportTypeReference1::Rename) {
                    const auto renamed = detail::appendPath(new_folder, relative);
                    const auto destination = target_root / renamed;
                    result.materials.emplace_back(detail::utf8Path(destination),
                                                  detail::utf8Path(source), false);
                    counter->change(path_id,
                                    usecounter::toPathId(detail::utf8Path(renamed)));
                } else {
                    const auto destination = detail::appendPath(target_root, relative);
                    const bool overwrite = detail::pathExists(destination);
                    if (!overwrite ||
                        option.materials != types::ImportTypeReference1::NoOverwrite) {
                        result.materials.emplace_back(detail::utf8Path(destination),
                                                      detail::utf8Path(source), overwrite);
                    }
                }
            }
        }
    }

    // 状態変数は独立した計画としてコピーする。IDはUseCounter
    // を通じて変更し、 呼び出し元が計画を適用したときに、取り込
    // んだ構造内の全利用元へ新しいパスが反映されるようにする。
    if (option.variables != types::ImportTypeReference1::NoImport) {
        auto variable_source_root = detail::flagRoot(from);
        auto variable_target_root = detail::flagRoot(to);
        if (variable_source_root && variable_target_root) {
            const auto new_dir = variable_target_root->createNewDirName(
                variable_source_root->createNewDirName(
                    detail::scenarioName(from), ""), "");

            auto copyFlags = [&](auto idTag, auto findSource, auto findTarget,
                                 auto makeId, auto& output) {
                using Id = decltype(idTag);
                for (const auto& id : counter->template keys<Id>()) {
                    const auto old_path = detail::stringId(id);
                    auto source = findSource(old_path);
                    if (!source) continue;
                    auto copy = detail::cloneOwner(source);
                    if (!copy) continue;
                    if (option.variables == types::ImportTypeReference1::Rename) {
                        const auto path = flag::FlagDir::join(new_dir, old_path);
                        const auto directory = flag::FlagDir::up(path);
                        output[directory].emplace_back(directory, copy, false);
                        counter->change(id, makeId(path));
                    } else {
                        const bool overwrite = static_cast<bool>(findTarget(old_path));
                        if (!overwrite || option.variables !=
                                              types::ImportTypeReference1::NoOverwrite) {
                            const auto directory = flag::FlagDir::up(old_path);
                            output[directory].emplace_back(directory, copy, overwrite);
                        }
                    }
                }
            };
            // NoOverwriteでは取り込み先にあるパスを省略する。D
            // のimportResourceは完全パスを root.getFlag/getStep/getVariant
            // へ渡すが、直接の名前しか検査せず入れ子の衝
            // 突を見落とす。 仕様どおり衝突を省略するた
            // め、ここでは再帰的なfind*検索を使う。
            copyFlags(usecounter::FlagId{}, [&](String path) {
                          return variable_source_root->findFlag(std::move(path));
                      },
                      [&](String path) {
                          return variable_target_root->findFlag(std::move(path));
                      },
                      [](const String& path) {
                          return usecounter::toFlagId(std::string_view(path));
                      },
                      result.flags);
            copyFlags(usecounter::StepId{}, [&](String path) {
                          return variable_source_root->findStep(std::move(path));
                      },
                      [&](String path) {
                          return variable_target_root->findStep(std::move(path));
                      },
                      [](const String& path) {
                          return usecounter::toStepId(std::string_view(path));
                      },
                      result.steps);
            copyFlags(usecounter::VariantId{}, [&](String path) {
                          return variable_source_root->findVariant(std::move(path));
                      },
                      [&](String path) {
                          return variable_target_root->findVariant(std::move(path));
                      },
                      [](const String& path) {
                          return usecounter::toVariantId(std::string_view(path));
                      },
                      result.variants);
        }
    }

    // シナリオの保存形式では手札をリンクカードと
    // して表す。 取り込んだカードを結果の表へ保
    // 持し、キャストには軽量なリンクカードを渡す
    // 。 Include指定時は逆の変換を行う。
    if (option.hands != types::ImportTypeIncluded::AsIs) {
        for (auto& [id, cast] : result.casts) {
            (void)id;
            if (!cast) continue;
            detail::processHands(*cast, prop, to, from, option.hands, result);
        }
    }

    // 背景画像の取り込み設定はカード画像と独立している。
    // includedFilesがAsIsでも、外部参
    // 照の背景を埋め込むInclude処理は実行する。
    const bool rewriteImages =
        option.includedFiles != types::ImportTypeIncluded::AsIs ||
        option.includedBgImages != types::ImportTypeIncluded::Exclude;
    if (rewriteImages || option.overwriteScenarioInfo ||
        option.beastsInMotions != types::ImportTypeIncluded::AsIs) {
        const auto scenario = detail::scenarioName(from);
        const auto authorValue = detail::author(from);

        auto putBinImg = [&](String name, String binImg) {
            const auto bytes = utils::strToBImg(binImg);
            const auto extension = imagesize::imageType(bytes);
            const auto base = utils::cleanFileName(std::move(name));
            const auto fileName = utils::createNewName(
                base,
                [&](String candidate) {
                    const auto relative = detail::appendPath(
                        new_folder, candidate + extension);
                    const auto target = detail::utf8Path(target_root / relative);
                    for (const auto& file : result.materials) {
                        if (utils::cfnmatch(file.dst, target)) return false;
                    }
                    return true;
                },
                false);
            const auto relative = detail::appendPath(
                new_folder, fileName + extension);
            result.materials.emplace_back(detail::utf8Path(target_root / relative),
                                          binImg, false);
            counter->change(usecounter::toPathId(binImg),
                            usecounter::toPathId(detail::utf8Path(relative)));
            return detail::utf8Path(relative);
        };

        auto includeImg = [&](StringView imagePath) {
            try {
                const auto source = detail::appendPath(source_root, imagePath);
                if (detail::pathExists(source)) {
                    return utils::bImgToStr(utils::readBinary(source));
                }
            } catch (const std::exception&) {
            }
            return String(imagePath);
        };

        auto processBeast = [&](auto& value) {
            detail::processMotion(value, prop, result, from, option.beastsInMotions);
        };
        auto processNode = [&](auto& value) {
            if (rewriteImages) {
                detail::processImages(value, prop, scenario, authorValue,
                                      option.includedFiles,
                                      option.includedBgImages, putBinImg,
                                      includeImg);
            }
            if (option.overwriteScenarioInfo) {
                detail::overwriteScenarioInfo(value, detail::scenarioName(to),
                                               detail::author(to));
            }
        };

        auto walk = [&](auto& table, auto& callback, auto beastPhase) {
            using Table = std::remove_reference_t<decltype(table)>;
            using Object = typename Table::mapped_type;
            lib::Array<Object> roots;
            roots.reserve(table.size());
            for (const auto& [id, object] : table) {
                (void)id;
                roots.push_back(object);
            }
            // Motionの処理で、外部化した召喚獣が結果表へ追加される場合がある
            // 。 ルートのスナップショットでunordered_mapの再ハッシュ
            // による反復子の無効化を避け、 取得済みの子でDと同じ走査を続ける。
            for (auto& object : roots) {
                if (!object) continue;
                auto pathObject = object->findCWXPath({});
                if (!pathObject) continue;
                if constexpr (decltype(beastPhase)::value)
                    detail::walkKnown<motion::Motion>(pathObject, callback);
                else
                    detail::walkKnown<card::Card, area::MenuCard,
                                      card::EffectCard, background::ImageCell>(pathObject, callback);
            }
        };
        const auto walkAll = [&](auto& callback, auto beastPhase) {
            walk(result.areas, callback, beastPhase);
            walk(result.battles, callback, beastPhase);
            walk(result.packages, callback, beastPhase);
            walk(result.casts, callback, beastPhase);
            walk(result.skills, callback, beastPhase);
            walk(result.items, callback, beastPhase);
            walk(result.beasts, callback, beastPhase);
            walk(result.infos, callback, beastPhase);
        };
        if (option.beastsInMotions != types::ImportTypeIncluded::AsIs)
            walkAll(processBeast, std::true_type{});
        if (rewriteImages || option.overwriteScenarioInfo)
            walkAll(processNode, std::false_type{});
    }

    return result;
}

template <typename CProps, typename Summary>
[[nodiscard]] ImportResult importResource(
    const CProps& prop, Summary& to, Summary& from,
    const ::cwxeditor::lib::Array<String>& resource_paths,
    const ImportOption& option) {
    return importResource(prop, to, from,
                          std::span<const String>(resource_paths), option);
}

}  // namespace importutils
}  // namespace cwxeditor::cwx
