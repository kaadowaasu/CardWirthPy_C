#pragma once

#include <cstddef>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

#include "portable.hpp"

namespace cwxeditor::lib {

// D形式の配列記述子。コピー・切り出しは同じ固定の確保領域と要
// 素を共有し、範囲は各記述子が持つ。 呼び出し元が記述子を代入
// し直しても、通知中のスナップショットは無効にならない。 構造
// を伸ばすときは新しい領域を使う。DはGCの余剰容量を再利用で
// きるが、 このAPIは追加時の容量・再利用を保証しない。 const
// の記述子だけでは深いconstにはならない。SharedSlice<const
// T>は格納先の変更を防ぐが、 所有ポインターはC++
// の浅いconstなので、Dの読み取り専用のクラス参
// 照にはConstOwnerSlice<T>を使う。
template <typename T>
class SharedArray;

template <typename T>
class SharedSlice {
    template <typename> friend class SharedSlice;
    template <typename> friend class SharedArray;
public:
    using value_type = std::remove_const_t<T>;
    using reference = T&;
    using iterator = T*;

private:
    using Storage = std::conditional_t<std::is_same_v<value_type, bool>,
                                     std::unique_ptr<bool[]>, std::vector<value_type>>;

public:
    SharedSlice() = default;
    explicit SharedSlice(std::vector<value_type> values)
        : length_(values.size()) {
        // 固定配列でbool[]もアドレスを持つ要素として保持する。 std::vector<bool>
        // の詰め込まれた代理要素ではDのスライスを表せない。
        if constexpr (std::is_same_v<value_type, bool>) {
            storage_ = std::make_shared<Storage>(std::make_unique<bool[]>(length_));
            for (std::size_t index = 0; index < length_; ++index)
                (*storage_)[index] = values[index];
        } else {
            // 既存の固定確保領域を受け取る。 要素を既定構築し
            // て再代入せず、既定コンストラクターも要求しない。
            storage_ = std::make_shared<Storage>(std::move(values));
        }
    }
    SharedSlice(std::initializer_list<value_type> values)
        : SharedSlice(std::vector<value_type>(values)) {}
    SharedSlice(const SharedSlice&) = default;
    SharedSlice& operator=(const SharedSlice&) = default;
    SharedSlice(SharedSlice&& other) noexcept
        : storage_(std::move(other.storage_)), offset_(std::exchange(other.offset_, 0)),
          length_(std::exchange(other.length_, 0)) {}
    SharedSlice& operator=(SharedSlice&& other) noexcept {
        if (this != &other) {
            storage_ = std::move(other.storage_);
            offset_ = std::exchange(other.offset_, 0);
            length_ = std::exchange(other.length_, 0);
        }
        return *this;
    }

    template <typename U>
        requires std::is_convertible_v<U*, T*> &&
                 std::is_same_v<std::remove_const_t<U>, value_type>
    SharedSlice(const SharedSlice<U>& other)
        : storage_(other.storage_), offset_(other.offset_), length_(other.length_) {}

    [[nodiscard]] std::size_t size() const noexcept { return length_; }
    [[nodiscard]] bool empty() const noexcept { return length_ == 0; }
    // 容量は可変配列の構築時の最適化情報に限る。 切り出した
    // 記述子から、元の確保領域の末尾へ追加してはならない。
    [[nodiscard]] std::size_t capacity() const noexcept {
        if (!storage_) return 0;
        if constexpr (std::is_same_v<value_type, bool>) return length_;
        else return offset_ == 0 ? storage_->capacity() : length_;
    }
    [[nodiscard]] T* data() const noexcept {
        if (!storage_) return nullptr;
        auto* pointer = [&] {
            if constexpr (std::is_same_v<value_type, bool>) return storage_->get();
            else return storage_->data();
        }();
        return offset_ ? pointer + offset_ : pointer;
    }
    [[nodiscard]] iterator begin() const noexcept { return data(); }
    [[nodiscard]] iterator end() const noexcept {
        return length_ ? data() + length_ : data();
    }
    [[nodiscard]] reference operator[](std::size_t index) const {
        if (index >= length_) throw std::out_of_range("array slice index");
        return (*storage_)[offset_ + index];
    }
    [[nodiscard]] reference at(std::size_t index) const { return (*this)[index]; }
    [[nodiscard]] reference front() const {
        if (empty()) throw std::out_of_range("array slice front");
        return (*this)[0];
    }
    [[nodiscard]] reference back() const {
        if (empty()) throw std::out_of_range("array slice back");
        return (*this)[length_ - 1];
    }
    [[nodiscard]] SharedSlice slice(std::size_t first, std::size_t last) const {
        if (first > last || last > length_) throw std::out_of_range("array slice range");
        return SharedSlice(storage_, offset_ + first, last - first);
    }

private:
    SharedSlice(std::shared_ptr<Storage> storage, std::size_t offset, std::size_t length)
        : storage_(std::move(storage)), offset_(offset), length_(length) {}

    std::shared_ptr<Storage> storage_;
    std::size_t offset_{};
    std::size_t length_{};
};

// Dの配列.dupと同じく、見えている要素を新しい領域へコピ
// ーする。 各要素内の配列メンバーは共有記述子のままにし、呼
// び出し側でvectorへ変換せず、外側のコピーを明示する。
template <typename T>
[[nodiscard]] SharedSlice<typename SharedSlice<T>::value_type>
dupSharedSlice(const SharedSlice<T>& source) {
    using Value = typename SharedSlice<T>::value_type;
    std::vector<Value> values;
    values.reserve(source.size());
    for (std::size_t index = 0; index < source.size(); ++index)
        values.push_back(source[index]);
    return SharedSlice<Value>(std::move(values));
}

namespace detail {

template <typename T, typename U>
bool sliceElementEqual(const T& left, const U& right) {
    return left == right;
}

template <typename T, typename U>
bool sliceElementEqual(const Owner<T>& left, const Owner<U>& right) {
    if (left == right) return true;
    if (!left || !right) return false;
    if constexpr (requires { static_cast<bool>(left->opEquals(*right)); })
        return left->opEquals(*right);
    else
        return left == right;
}

template <typename Left, typename Right>
bool sliceContentsEqual(const Left& left, const Right& right) {
    if (left.size() != right.size()) return false;
    for (std::size_t index = 0; index < left.size(); ++index)
        if (!sliceElementEqual(left[index], right[index])) return false;
    return true;
}

} // namespace detail

// Dの配列の等価比較は、記述子・確保領域の同一性によらず要素の値を
// 比較する。 ネイティブvectorのアダプターも同じ経路を使う。
template <typename T, typename U>
    requires requires(const T& left, const U& right) {
        static_cast<bool>(left == right);
    }
[[nodiscard]] bool operator==(const SharedSlice<T>& left,
                              const SharedSlice<U>& right) {
    return detail::sliceContentsEqual(left, right);
}

template <typename T, typename U, typename Allocator>
    requires requires(const T& left, const U& right) {
        static_cast<bool>(left == right);
    }
[[nodiscard]] bool operator==(const SharedSlice<T>& left,
                              const std::vector<U, Allocator>& right) {
    return detail::sliceContentsEqual(left, right);
}

namespace detail {

// 構造変更では新しい固定確保領域を公開する。 Dの~=は余剰容量を再
// 利用できるが、SharedSliceは容量・再利用を保証しない。
template <typename T, typename Edit>
    requires (!std::is_const_v<T>)
void editSharedSlice(SharedSlice<T>& slice, Edit&& edit) {
    using Value = typename SharedSlice<T>::value_type;
    std::vector<Value> staged;
    staged.reserve(slice.capacity());
    // 空のSharedSliceではbegin()/end()がnull
    // になり得るため、 不正なポインター差分を取らず添字を使う。
    for (std::size_t index = 0; index < slice.size(); ++index)
        staged.push_back(slice[index]);

    std::forward<Edit>(edit)(staged);
    SharedSlice<T> replacement(std::move(staged));
    slice = std::move(replacement);
}

} // namespace detail

template <typename T>
    requires (!std::is_const_v<T>)
void reserveSharedSlice(SharedSlice<T>& slice, std::size_t capacity) {
    if (capacity <= slice.capacity()) return;
    using Value = typename SharedSlice<T>::value_type;
    std::vector<Value> staged;
    staged.reserve(capacity);
    for (std::size_t index = 0; index < slice.size(); ++index)
        staged.push_back(slice[index]);
    slice = SharedSlice<T>(std::move(staged));
}

template <typename T>
    requires (!std::is_const_v<T>)
void appendToSlice(SharedSlice<T>& slice, T value) {
    detail::editSharedSlice(slice, [&value](auto& staged) {
        staged.push_back(std::move(value));
    });
}

template <typename T>
    requires (!std::is_const_v<T>)
void insertIntoSlice(SharedSlice<T>& slice, std::size_t index, T value) {
    if (index > slice.size()) throw std::out_of_range("array slice insert index");
    detail::editSharedSlice(slice, [index, &value](auto& staged) {
        staged.insert(staged.begin() + static_cast<std::ptrdiff_t>(index),
                      std::move(value));
    });
}

template <typename T>
    requires (!std::is_const_v<T>)
void eraseFromSlice(SharedSlice<T>& slice, std::size_t index) {
    if (index >= slice.size()) throw std::out_of_range("array slice erase index");
    detail::editSharedSlice(slice, [index](auto& staged) {
        staged.erase(staged.begin() + static_cast<std::ptrdiff_t>(index));
    });
}

// C++の所有権で安全に保持する、可変のD形式の配列記述子。
// コピーは固定の要素領域を共有し、構造変更では編集した記
// 述子だけを付け替える。 vector入力は所有領域へコピ
// ー・移動し、外部vectorの参照や反復子は保持しない。
template <typename T>
class SharedArray : public SharedSlice<T> {
    static_assert(!std::is_const_v<T>, "SharedArray stores mutable slots");
    using Base = SharedSlice<T>;

public:
    using value_type = typename Base::value_type;
    using reference = typename Base::reference;
    using iterator = typename Base::iterator;

    using Base::Base;
    SharedArray() = default;
    SharedArray(const SharedArray&) = default;
    SharedArray& operator=(const SharedArray&) = default;
    SharedArray(SharedArray&&) noexcept = default;
    SharedArray& operator=(SharedArray&&) noexcept = default;
    SharedArray(const Base& source) : Base(source) {}
    SharedArray(Base&& source) noexcept : Base(std::move(source)) {}
    SharedArray(std::vector<value_type> values) : Base(std::move(values)) {}
    SharedArray(std::initializer_list<value_type> values) : Base(values) {}

    SharedArray& operator=(const Base& source) {
        Base::operator=(source);
        return *this;
    }
    SharedArray& operator=(Base&& source) noexcept {
        Base::operator=(std::move(source));
        return *this;
    }

    void reserve(std::size_t requested) {
        if (requested <= this->capacity()) return;
        if constexpr (!std::is_same_v<value_type, bool>) {
            if (canMutateWholeStorage()) {
                this->storage_->reserve(requested);
                return;
            }
        }
        reserveSharedSlice(static_cast<Base&>(*this), requested);
    }

    void clear() {
        if (this->storage_ && this->offset_ == 0 &&
            this->storage_.use_count() == 1) {
            if constexpr (std::is_same_v<value_type, bool>)
                this->storage_->reset();
            else
                this->storage_->clear();
            this->offset_ = 0;
            this->length_ = 0;
            return;
        }
        Base::operator=(Base{});
    }

    void push_back(value_type value) {
        if constexpr (!std::is_same_v<value_type, bool>) {
            if (canMutateWholeStorage()) {
                this->storage_->push_back(std::move(value));
                ++this->length_;
                return;
            }
        }
        appendToSlice(static_cast<Base&>(*this), std::move(value));
    }

    template <typename... Args>
    reference emplace_back(Args&&... args) {
        if constexpr (!std::is_same_v<value_type, bool>) {
            if (canMutateWholeStorage()) {
                this->storage_->emplace_back(std::forward<Args>(args)...);
                ++this->length_;
                return this->back();
            }
        }
        detail::editSharedSlice(static_cast<Base&>(*this), [&](auto& staged) {
            staged.emplace_back(std::forward<Args>(args)...);
        });
        return this->back();
    }

    iterator insert(iterator position, value_type value) {
        std::size_t index = 0;
        if (this->empty()) {
            if (position != this->data())
                throw std::out_of_range("shared array insert position");
        } else {
            index = static_cast<std::size_t>(position - this->data());
        }
        if (index > this->size())
            throw std::out_of_range("shared array insert position");
        if constexpr (!std::is_same_v<value_type, bool>) {
            if (canMutateWholeStorage()) {
                this->storage_->insert(this->storage_->begin() +
                                           static_cast<std::ptrdiff_t>(index),
                                       std::move(value));
                ++this->length_;
                return this->data() + index;
            }
        }
        insertIntoSlice(static_cast<Base&>(*this), index, std::move(value));
        return this->data() + index;
    }

    iterator erase(iterator position) {
        if (this->empty() || position == this->end())
            throw std::out_of_range("shared array erase position");
        const auto index = static_cast<std::size_t>(position - this->data());
        if constexpr (!std::is_same_v<value_type, bool>) {
            if (canMutateWholeStorage()) {
                this->storage_->erase(this->storage_->begin() +
                                      static_cast<std::ptrdiff_t>(index));
                --this->length_;
                return index < this->size() ? this->data() + index : this->end();
            }
        }
        eraseFromSlice(static_cast<Base&>(*this), index);
        return index < this->size() ? this->data() + index : this->end();
    }

private:
    [[nodiscard]] bool canMutateWholeStorage() const noexcept {
        if constexpr (std::is_same_v<value_type, bool>) {
            return false;
        } else {
            return this->storage_ && this->offset_ == 0 &&
                   this->length_ == this->storage_->size() &&
                   this->storage_.use_count() == 1;
        }
    }
};

template <typename T, typename U>
    requires requires(const T& left, const U& right) {
        static_cast<bool>(left == right);
    }
[[nodiscard]] bool operator==(const SharedArray<T>& left,
                              const SharedArray<U>& right) {
    return detail::sliceContentsEqual(left, right);
}

template <typename T, typename U, typename Allocator>
    requires requires(const T& left, const U& right) {
        static_cast<bool>(left == right);
    }
[[nodiscard]] bool operator==(const SharedArray<T>& left,
                              const std::vector<U, Allocator>& right) {
    return detail::sliceContentsEqual(left, right);
}

template <typename T, typename U, typename Allocator>
    requires requires(const T& left, const U& right) {
        static_cast<bool>(left == right);
    }
[[nodiscard]] bool operator==(const std::vector<T, Allocator>& left,
                              const SharedArray<U>& right) {
    return detail::sliceContentsEqual(left, right);
}

// 共有Owner<T>格納先の深いconstの参照。取得ごと
// に現在の要素をOwner<const T>へ変換し、 可変
// Ownerを公開したり格納領域をコピーしたりせず、別のSharedSlice
// からの置き換えも見えるようにする。 反復子も元
// の記述子を保持し、ビューと同じ寿命を保証する。
template <typename T>
class ConstOwnerSlice {
    static_assert(!std::is_const_v<T>, "ConstOwnerSlice expects a mutable model type");

public:
    using value_type = Owner<const T>;
    using reference = const value_type;

    class const_iterator {
        using SourceSlice = SharedSlice<Owner<T>>;

    public:
        using value_type = Owner<const T>;
        using reference = const value_type;
        using pointer = void;
        using difference_type = std::ptrdiff_t;
        using iterator_category = std::forward_iterator_tag;
        using iterator_concept = std::forward_iterator_tag;

        const_iterator() = default;

        [[nodiscard]] reference operator*() const {
            return value_type{source_.at(index_)};
        }
        [[nodiscard]] reference operator->() const { return **this; }
        const_iterator& operator++() noexcept {
            ++index_;
            return *this;
        }
        const_iterator operator++(int) noexcept {
            auto previous = *this;
            ++(*this);
            return previous;
        }

        friend bool operator==(const const_iterator& left,
                               const const_iterator& right) noexcept {
            return left.source_.data() == right.source_.data() &&
                   left.index_ == right.index_;
        }
        friend bool operator!=(const const_iterator& left,
                               const const_iterator& right) noexcept {
            return !(left == right);
        }

    private:
        friend class ConstOwnerSlice;
        const_iterator(SourceSlice source, std::size_t index)
            : source_(std::move(source)), index_(index) {}

        SourceSlice source_;
        std::size_t index_{};
    };

    ConstOwnerSlice() = default;
    explicit ConstOwnerSlice(SharedSlice<Owner<T>> source)
        : source_(std::move(source)) {}

    [[nodiscard]] std::size_t size() const noexcept { return source_.size(); }
    [[nodiscard]] bool empty() const noexcept { return source_.empty(); }
    [[nodiscard]] reference operator[](std::size_t index) const {
        return value_type{source_[index]};
    }
    [[nodiscard]] reference at(std::size_t index) const {
        return value_type{source_.at(index)};
    }
    [[nodiscard]] reference front() const {
        return value_type{source_.front()};
    }
    [[nodiscard]] reference back() const {
        return value_type{source_.back()};
    }
    [[nodiscard]] const_iterator begin() const noexcept {
        return const_iterator(source_, 0);
    }
    [[nodiscard]] const_iterator end() const noexcept {
        return const_iterator(source_, size());
    }
    [[nodiscard]] ConstOwnerSlice slice(std::size_t first,
                                        std::size_t last) const {
        return ConstOwnerSlice(source_.slice(first, last));
    }

private:
    SharedSlice<Owner<T>> source_;
};

template <typename T, typename U>
    requires requires(const Owner<const T>& left, const Owner<const U>& right) {
        static_cast<bool>(left == right);
    }
[[nodiscard]] bool operator==(const ConstOwnerSlice<T>& left,
                              const ConstOwnerSlice<U>& right) {
    return detail::sliceContentsEqual(left, right);
}

template <typename T, typename U>
    requires requires(const Owner<const T>& left, const U& right) {
        static_cast<bool>(left == right);
    }
[[nodiscard]] bool operator==(const ConstOwnerSlice<T>& left,
                              const SharedSlice<U>& right) {
    return detail::sliceContentsEqual(left, right);
}

template <typename T, typename U, typename Allocator>
    requires requires(const Owner<const T>& left, const U& right) {
        static_cast<bool>(left == right);
    }
[[nodiscard]] bool operator==(const ConstOwnerSlice<T>& left,
                              const std::vector<U, Allocator>& right) {
    return detail::sliceContentsEqual(left, right);
}

} // namespace cwxeditor::lib
