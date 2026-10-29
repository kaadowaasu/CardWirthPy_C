#pragma once

#include "cw/binary/skill.hpp"
#include "cw/binary/item.hpp"
#include "cw/binary/beast.hpp"
#include "cw/binary/event.hpp"
#include "cw/binary/content.hpp"
#include "cw/binary/effectmotion.hpp"
#include "cw/binary/bgimage.hpp"
#include "cw/binary/coupon.hpp"
#include "cw/binary/dialog.hpp"
#include "cw/data.hpp"
#include <unordered_map>
#include <stdexcept>
#include <type_traits>

namespace cw::binary::cwyado::detail {

// Python の deepcopy 呼び出しごとに一つの対応表を使い、強参照のノード
// を一度だけ複製して、内部の別名参照と循環を保つ。バイナリーのルート参照は弱参照であり
// 、Python の deepcopy は weakref と元の参照先を保持する。
class CardCloneGraph {
    using Object = ::cw::lib::Object;
    using ObjectPtr = ::cw::lib::ObjectPtr;
    using Base = ::cw::binary::base::CWBinaryBase;
    std::unordered_map<const Object*, ObjectPtr> memo_;

    template<class T> std::shared_ptr<T> allocate(const std::shared_ptr<T>& source)
    {
        auto result = std::make_shared<T>(*source);
        memo_.emplace(source.get(), result);
        if constexpr (std::is_base_of_v<Base, T>) {
            // ネイティブの最上位ルートは空の弱参照で表す。複
            // 製後の弱参照も、元のルートを指す必要がある。
            const std::weak_ptr<Base> empty;
            const bool self_root_sentinel =
                !source->m_root.owner_before(empty) &&
                !empty.owner_before(source->m_root);
            if (self_root_sentinel) result->m_root = source;
            // 制御ブロックがある失効済み参照は、自身がルートであることを表す印ではな
            // い。deepcopy と同様に、その失効済み弱参照を正確に保持する。
        }
        return result;
    }

    template<class T> void children(std::vector<std::shared_ptr<T>>& values)
    {
        for (auto& value : values) value = copy(value);
    }

    void xml(std::optional<std::shared_ptr<::cw::data::CWPyElement>>& value)
    {
        if (value) *value = copy(*value);
    }

    void properties(::cw::lib::ValueMap& values)
    {
        for (auto& [key, value] : values) value = copy_value(value);
    }

    template<class T> ObjectPtr card(const std::shared_ptr<T>& source)
    {
        auto result = allocate(source);
        children(result->m_motions);
        children(result->m_events);
        result->m_data = copy(result->m_data);
        return result;
    }

public:
    template<class T> std::shared_ptr<T> copy(const std::shared_ptr<T>& source)
    {
        if (!source) return {};
        return std::dynamic_pointer_cast<T>(copy_object(source));
    }

    ::cw::lib::Value copy_value(const ::cw::lib::Value& value)
    {
        if (const auto* object = std::get_if<ObjectPtr>(&value.storage()))
            return ::cw::lib::Value(copy_object(*object));
        return value;
    }

    ObjectPtr copy_object(const ObjectPtr& source)
    {
        if (!source) return {};
        if (const auto found = memo_.find(source.get()); found != memo_.end())
            return found->second;
        if (auto value = std::dynamic_pointer_cast<::cw::binary::skill::SkillCard>(source)) return card(value);
        if (auto value = std::dynamic_pointer_cast<::cw::binary::item::ItemCard>(source)) return card(value);
        if (auto value = std::dynamic_pointer_cast<::cw::binary::beast::BeastCard>(source)) return card(value);
        if (auto value = std::dynamic_pointer_cast<::cw::binary::event::SimpleEvent>(source)) {
            auto result = allocate(value);
            children(result->m_contents); xml(result->m_data);
            return result;
        }
        if (auto value = std::dynamic_pointer_cast<::cw::binary::event::Event>(source)) {
            auto result = allocate(value);
            children(result->m_contents); xml(result->m_data);
            return result;
        }
        if (auto value = std::dynamic_pointer_cast<::cw::binary::content::Content>(source)) {
            auto result = allocate(value);
            children(result->m_bgimgs); children(result->m_children);
            children(result->m_coupons); children(result->m_dialogs);
            children(result->m_motions); xml(result->m_data);
            properties(result->m_properties);
            return result;
        }
        if (auto value = std::dynamic_pointer_cast<::cw::binary::effectmotion::EffectMotion>(source)) {
            auto result = allocate(value);
            if (result->m_beasts) children(*result->m_beasts);
            xml(result->m_data); properties(result->m_properties);
            return result;
        }
        if (auto value = std::dynamic_pointer_cast<::cw::binary::bgimage::BgImage>(source)) {
            auto result = allocate(value); xml(result->m_data); return result;
        }
        if (auto value = std::dynamic_pointer_cast<::cw::binary::coupon::Coupon>(source)) {
            auto result = allocate(value); result->m_data = copy(result->m_data); return result;
        }
        if (auto value = std::dynamic_pointer_cast<::cw::binary::dialog::Dialog>(source)) {
            auto result = allocate(value); xml(result->m_data); return result;
        }
        if (auto value = std::dynamic_pointer_cast<::cw::data::VariantSequence>(source)) {
            auto result = allocate(value);
            for (auto& item : result->m_values) item = copy_value(item);
            return result;
        }
        if (auto value = std::dynamic_pointer_cast<::cw::data::VariantMapping>(source)) {
            auto result = allocate(value); properties(result->m_values); return result;
        }
        if (auto value = std::dynamic_pointer_cast<::cw::data::StructVal>(source)) {
            auto result = allocate(value);
            result->m_members = copy_value(result->m_members);
            result->m_name = copy_value(result->m_name);
            for (auto& item : result->m_member_values) item = copy_value(item);
            return result;
        }
        if (auto value = std::dynamic_pointer_cast<::cw::lib::XmlNode>(source)) {
            auto result = allocate(value); children(result->children); return result;
        }
        if (auto value = std::dynamic_pointer_cast<::cw::lib::XmlDocument>(source)) {
            auto result = allocate(value); result->root = copy(result->root); return result;
        }
        if (auto value = std::dynamic_pointer_cast<::cw::data::CWPyElementTree>(source)) {
            auto result = allocate(value);
            result->m_root_node = copy(result->m_root_node);
            result->m__etree = copy_value(result->m__etree);
            result->m__root = copy_value(result->m__root);
            return result;
        }
        if (auto value = std::dynamic_pointer_cast<::cw::data::_ElementWrapper>(source)) {
            auto result = allocate(value);
            result->m_wrap = copy_value(result->m_wrap);
            return result;
        }
        if (auto value = std::dynamic_pointer_cast<::cw::data::CWPyElement>(source)) {
            // バイナリーの読み込みは、実行時の EventContent を
            // 作る前に解析済みカードを複製する。別の実行時グラフは拒否し、
            // 読み込んだカードと変更可能なエンジン状態を黙って共有しない。
            if (value->m_content && *value->m_content)
                throw std::logic_error("Binary card clone runtime XML graph requires completion");
            auto result = allocate(value);
            result->m_element = copy(result->m_element);
            children(result->m_children);
            if (result->m_nextelements) children(*result->m_nextelements);
            if (auto parent = value->m_parent.lock()) result->m_parent = copy(parent);
            if (value->m_cwxparent) result->m_cwxparent = copy(*value->m_cwxparent);
            return result;
        }
        throw std::logic_error("Binary card clone contains an unsupported mutable object");
    }
};

inline std::shared_ptr<::cw::binary::base::CWBinaryBase> deepcopy_card(
    const std::shared_ptr<::cw::binary::base::CWBinaryBase>& source)
{
    return CardCloneGraph{}.copy(source);
}

} // namespace cw::binary::cwyado::detail
