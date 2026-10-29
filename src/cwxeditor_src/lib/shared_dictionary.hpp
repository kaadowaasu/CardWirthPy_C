#pragma once

#include <cstddef>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include <utility>

#include "portable.hpp"

namespace cwxeditor::lib {

// Dの連想配列の記述子を表すラッパー。初期化済みのコピーは
// 内部マップを共有し、nullの記述子は最初の書き込み時に
// 個別に確保する。値コピーが必要なAPIには、別のDictionary
// 別名でネイティブの値コピー動作を提供する。
template <typename Key, typename Value>
class SharedDictionary {
public:
    using NativeDictionary = Dictionary<Key, Value>;
    using key_type = typename NativeDictionary::key_type;
    using mapped_type = typename NativeDictionary::mapped_type;
    using value_type = typename NativeDictionary::value_type;
    using size_type = typename NativeDictionary::size_type;
    using difference_type = typename NativeDictionary::difference_type;

private:
    using Storage = std::shared_ptr<NativeDictionary>;

public:
    template <bool IsConst>
    class BasicIterator {
        template <bool>
        friend class BasicIterator;
        friend class SharedDictionary;

        using MapIterator = std::conditional_t<
            IsConst, typename NativeDictionary::const_iterator,
            typename NativeDictionary::iterator>;

    public:
        using iterator_category = std::forward_iterator_tag;
        using iterator_concept = std::forward_iterator_tag;
        using value_type = typename NativeDictionary::value_type;
        using difference_type = typename NativeDictionary::difference_type;
        using pointer = std::conditional_t<IsConst, const value_type*, value_type*>;
        using reference = std::conditional_t<IsConst, const value_type&, value_type&>;

        BasicIterator() = default;

        template <bool OtherConst>
            requires (IsConst && !OtherConst)
        BasicIterator(const BasicIterator<OtherConst>& other)
            : storage_(other.storage_), iterator_(other.iterator_) {}

        [[nodiscard]] reference operator*() const { return *iterator_; }
        [[nodiscard]] pointer operator->() const {
            return std::addressof(*iterator_);
        }

        BasicIterator& operator++() {
            ++iterator_;
            return *this;
        }

        BasicIterator operator++(int) {
            auto previous = *this;
            ++(*this);
            return previous;
        }

        template <bool OtherConst>
        [[nodiscard]] bool operator==(
            const BasicIterator<OtherConst>& other) const {
            if (storage_.get() != other.storage_.get()) return false;
            // nullのbegin/endは等しい既定イテレーターで表す。共
            // 有状態を持たずに、nullの記述子を有効な空範囲として扱う。
            return !storage_ || iterator_ == other.iterator_;
        }

        template <bool OtherConst>
        [[nodiscard]] bool operator!=(
            const BasicIterator<OtherConst>& other) const {
            return !(*this == other);
        }

    private:
        BasicIterator(Storage storage, MapIterator iterator)
            : storage_(std::move(storage)), iterator_(std::move(iterator)) {}

        // 記述子の付け替え後も呼び出し側がイテレーターを保持している場合は、保存領域
        // を生存させる。DのGCが連想配列の反復中に領域を保持する寿命に合わせる。
        Storage storage_;
        MapIterator iterator_{};
    };

    using iterator = BasicIterator<false>;
    using const_iterator = BasicIterator<true>;

    SharedDictionary() = default;

    SharedDictionary(std::initializer_list<value_type> values)
        : storage_(std::make_shared<NativeDictionary>(values)) {}

    SharedDictionary& operator=(std::initializer_list<value_type> values) {
        // 連想配列の記述子への代入は、この記述子の参照先を付け替える
        // 。以前の記述子のコピーが参照する保存領域は置き換えない。
        storage_ = std::make_shared<NativeDictionary>(values);
        return *this;
    }

    /// ネイティブの値型辞書を、独立した共有連想配列の保存領域へコピーする。
    [[nodiscard]] static SharedDictionary fromSnapshot(
        const NativeDictionary& values) {
        return SharedDictionary(std::make_shared<NativeDictionary>(values));
    }

    [[nodiscard]] bool empty() const noexcept {
        return !storage_ || storage_->empty();
    }

    [[nodiscard]] size_type size() const noexcept {
        return storage_ ? storage_->size() : 0U;
    }

    [[nodiscard]] bool contains(const key_type& key) const {
        return storage_ && storage_->contains(key);
    }

    [[nodiscard]] iterator begin() {
        return storage_ ? iterator(storage_, storage_->begin()) : iterator{};
    }

    [[nodiscard]] const_iterator begin() const {
        return storage_ ? const_iterator(storage_, storage_->begin())
                        : const_iterator{};
    }

    [[nodiscard]] const_iterator cbegin() const { return begin(); }

    [[nodiscard]] iterator end() {
        return storage_ ? iterator(storage_, storage_->end()) : iterator{};
    }

    [[nodiscard]] const_iterator end() const {
        return storage_ ? const_iterator(storage_, storage_->end())
                        : const_iterator{};
    }

    [[nodiscard]] const_iterator cend() const { return end(); }

    [[nodiscard]] iterator find(const key_type& key) {
        return storage_ ? iterator(storage_, storage_->find(key)) : iterator{};
    }

    [[nodiscard]] const_iterator find(const key_type& key) const {
        return storage_ ? const_iterator(storage_, storage_->find(key))
                        : const_iterator{};
    }

    [[nodiscard]] mapped_type& at(const key_type& key) {
        if (!storage_) throw std::out_of_range("SharedDictionary::at");
        return storage_->at(key);
    }

    [[nodiscard]] const mapped_type& at(const key_type& key) const {
        if (!storage_) throw std::out_of_range("SharedDictionary::at");
        return storage_->at(key);
    }

    mapped_type& operator[](const key_type& key) {
        return ensureStorage()[key];
    }

    mapped_type& operator[](key_type&& key) {
        return ensureStorage()[std::move(key)];
    }

    template <typename... Args>
    std::pair<iterator, bool> emplace(Args&&... args) {
        auto [position, inserted] =
            ensureStorage().emplace(std::forward<Args>(args)...);
        return {iterator(storage_, position), inserted};
    }

    template <typename M>
    std::pair<iterator, bool> insert_or_assign(key_type key, M&& value) {
        auto [position, inserted] = ensureStorage().insert_or_assign(
            std::move(key), std::forward<M>(value));
        return {iterator(storage_, position), inserted};
    }

    iterator erase(const_iterator position) {
        if (!storage_ || position.storage_.get() != storage_.get()) return end();
        return iterator(storage_, storage_->erase(position.iterator_));
    }

    iterator erase(const_iterator first, const_iterator last) {
        if (!storage_ || first.storage_.get() != storage_.get() ||
            last.storage_.get() != storage_.get()) {
            return end();
        }
        return iterator(storage_, storage_->erase(first.iterator_, last.iterator_));
    }

    size_type erase(const key_type& key) {
        return storage_ ? storage_->erase(key) : size_type{0};
    }

    /// ネイティブのerase APIに加え、Dと同じremoveという名前も提供する。
    [[nodiscard]] bool remove(const key_type& key) {
        return erase(key) != size_type{0};
    }

    void clear() {
        if (storage_) storage_->clear();
    }

private:
    explicit SharedDictionary(Storage storage) noexcept
        : storage_(std::move(storage)) {}

    [[nodiscard]] NativeDictionary& ensureStorage() {
        if (!storage_) storage_ = std::make_shared<NativeDictionary>();
        return *storage_;
    }

    Storage storage_;
};

}  // namespace cwxeditor::lib
