#pragma once
#include <algorithm>
#include <initializer_list>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace cw::lib {
// ElementTree の属性辞書は挿入順を保つ。ネイティブ XML
// の利用側向けの map インターフェースを保ちながら、設定、
// 削除・再挿入、複製を通じて順序を保持し、保存時に推測しない。
template<class T>
class InsertionOrderedStringMap : public std::map<std::string, T> {
    using Base = std::map<std::string, T>;
    std::vector<std::string> m_order;
public:
    using typename Base::iterator;
    using typename Base::const_iterator;
    using typename Base::size_type;
    InsertionOrderedStringMap() = default;
    InsertionOrderedStringMap(const InsertionOrderedStringMap&) = default;
    InsertionOrderedStringMap(InsertionOrderedStringMap&&) = default;
    InsertionOrderedStringMap& operator=(const InsertionOrderedStringMap&) = default;
    InsertionOrderedStringMap& operator=(InsertionOrderedStringMap&&) = default;
    // ネイティブ std::map の引数はすでに挿入順を失
    // っている。その言語境界では反復順だけが利用可能な順序。
    InsertionOrderedStringMap(const Base& source) {
        for (const auto& [key, value] : source) (*this)[key] = value;
    }
    InsertionOrderedStringMap(std::initializer_list<typename Base::value_type> source) {
        for (const auto& [key, value] : source) (*this)[key] = value;
    }
    InsertionOrderedStringMap& operator=(const Base& source) {
        if (&source != static_cast<const Base*>(this)) {
            clear();
            for (const auto& [key, value] : source) (*this)[key] = value;
        }
        return *this;
    }
    InsertionOrderedStringMap& operator=(std::initializer_list<typename Base::value_type> source) {
        clear();
        for (const auto& [key, value] : source) (*this)[key] = value;
        return *this;
    }
    T& operator[](const std::string& key) {
        if (!this->contains(key)) m_order.push_back(key);
        return Base::operator[](key);
    }
    T& operator[](std::string&& key) {
        if (!this->contains(key)) m_order.push_back(key);
        return Base::operator[](std::move(key));
    }
    template<class Key, class Value>
    std::pair<iterator, bool> emplace(Key&& key, Value&& value) {
        auto result = Base::emplace(std::forward<Key>(key), std::forward<Value>(value));
        if (result.second) m_order.push_back(result.first->first);
        return result;
    }
    std::pair<iterator, bool> insert(const typename Base::value_type& value) {
        return emplace(value.first, value.second);
    }
    template<class Iterator> void insert(Iterator first, Iterator last) {
        for (; first != last; ++first) insert(*first);
    }
    template<class Key, class Value>
    std::pair<iterator, bool> insert_or_assign(Key&& key, Value&& value) {
        auto result = Base::insert_or_assign(std::forward<Key>(key), std::forward<Value>(value));
        if (result.second) m_order.push_back(result.first->first);
        return result;
    }
    template<class Key, class... Args>
    std::pair<iterator, bool> try_emplace(Key&& key, Args&&... args) {
        auto result = Base::try_emplace(std::forward<Key>(key), std::forward<Args>(args)...);
        if (result.second) m_order.push_back(result.first->first);
        return result;
    }
    size_type erase(const std::string& key) {
        const auto count = Base::erase(key);
        if (count) m_order.erase(std::remove(m_order.begin(), m_order.end(), key), m_order.end());
        return count;
    }
    iterator erase(const_iterator position) {
        const auto key = position->first;
        auto next = Base::erase(position);
        m_order.erase(std::remove(m_order.begin(), m_order.end(), key), m_order.end());
        return next;
    }
    iterator erase(const_iterator first, const_iterator last) {
        while (first != last) first = erase(first);
        return Base::erase(first, first);
    }
    void swap(InsertionOrderedStringMap& other) noexcept {
        Base::swap(other);
        m_order.swap(other.m_order);
    }
    void clear() noexcept { Base::clear(); m_order.clear(); }
    const std::vector<std::string>& insertion_keys() const noexcept { return m_order; }
};
using XmlAttributes = InsertionOrderedStringMap<std::string>;
}
