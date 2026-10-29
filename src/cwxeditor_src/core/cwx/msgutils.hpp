#pragma once

// cwx/msgutils.dのC++20移植。公開
// APIはUTF-8、メッセージ解析中はUTF-32
// のコードポイントを使う。 CardWirthのフォ
// ント・色・折り返し位置はバイト単位ではなくコードポ
// イント単位なので、 二つの表現を明確に区別する。

#include <cstddef>
#include <functional>
#include <initializer_list>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "cwxeditor_src/lib/portable.hpp"

namespace cwxeditor::cwx::msgutils {

using DChar = char32_t;
using DString = std::u32string;
using Grapheme = DString;

using FontMap = ::cwxeditor::lib::Dictionary<std::size_t, std::string>;
using ColorMap = ::cwxeditor::lib::Dictionary<std::size_t, char>;
// ネイティブGUIの履歴は値コピーを使う。そのインターフェースを保
// ちつつ、 コアのデリゲートはDの連想配列ハンドルを保持できるよ
// うにする。 Dと同じく、nullと確保済みの空の表を区別する。
class NameMap {
public:
    using Storage = ::cwxeditor::lib::Dictionary<char, std::string>;
    using value_type = Storage::value_type;
    using key_type = Storage::key_type;
    using mapped_type = Storage::mapped_type;
    using size_type = Storage::size_type;
    using iterator = Storage::iterator;
    using const_iterator = Storage::const_iterator;
    using SharedStorage = std::shared_ptr<Storage>;

    NameMap() = default;
    NameMap(std::initializer_list<value_type> values) {
        if (values.size()) data_ = std::make_shared<Storage>(values);
    }
    NameMap(const NameMap& other)
        : data_(other.data_ ? std::make_shared<Storage>(*other.data_) : nullptr) {}
    NameMap(NameMap&&) noexcept = default;
    NameMap& operator=(const NameMap& other) {
        if (this != &other) {
            auto copy = other.data_ ? std::make_shared<Storage>(*other.data_) : nullptr;
            data_ = std::move(copy);
        }
        return *this;
    }
    NameMap& operator=(NameMap&&) noexcept = default;
    NameMap& operator=(std::initializer_list<value_type> values) {
        NameMap copy(values);
        swap(copy);
        return *this;
    }

    [[nodiscard]] SharedStorage sharedStorage() const noexcept { return data_; }
    [[nodiscard]] static NameMap fromSharedStorage(SharedStorage data) noexcept {
        return NameMap(SharedTag{}, std::move(data));
    }

    [[nodiscard]] bool empty() const noexcept { return !data_ || data_->empty(); }
    [[nodiscard]] size_type size() const noexcept { return data_ ? data_->size() : 0; }
    iterator begin() noexcept { return data_ ? data_->begin() : iterator{}; }
    iterator end() noexcept { return data_ ? data_->end() : iterator{}; }
    const_iterator begin() const noexcept { return data_ ? data_->cbegin() : const_iterator{}; }
    const_iterator end() const noexcept { return data_ ? data_->cend() : const_iterator{}; }
    const_iterator cbegin() const noexcept { return begin(); }
    const_iterator cend() const noexcept { return end(); }
    iterator find(char key) { return data_ ? data_->find(key) : end(); }
    const_iterator find(char key) const { return data_ ? data_->find(key) : end(); }
    [[nodiscard]] bool contains(char key) const { return data_ && data_->contains(key); }
    [[nodiscard]] size_type count(char key) const { return data_ ? data_->count(key) : 0; }
    mapped_type& operator[](char key) { return ensure()[key]; }
    mapped_type& at(char key) {
        if (!data_) throw std::out_of_range("NameMap key");
        return data_->at(key);
    }
    const mapped_type& at(char key) const {
        if (!data_) throw std::out_of_range("NameMap key");
        return data_->at(key);
    }
    void clear() noexcept { if (data_) data_->clear(); }
    size_type erase(char key) { return data_ ? data_->erase(key) : 0; }
    iterator erase(const_iterator position) {
        if (!data_) throw std::out_of_range("NameMap iterator");
        return data_->erase(position);
    }
    iterator erase(const_iterator first, const_iterator last) {
        if (!data_) return end();
        return data_->erase(first, last);
    }
    template<class... Args> decltype(auto) emplace(Args&&... args) {
        return ensure().emplace(std::forward<Args>(args)...);
    }
    template<class... Args> decltype(auto) insert(Args&&... args) {
        return ensure().insert(std::forward<Args>(args)...);
    }
    void insert(std::initializer_list<value_type> values) {
        if (values.size()) ensure().insert(values);
    }
    template<class... Args> decltype(auto) insert_or_assign(Args&&... args) {
        return ensure().insert_or_assign(std::forward<Args>(args)...);
    }
    template<class... Args> decltype(auto) try_emplace(Args&&... args) {
        return ensure().try_emplace(std::forward<Args>(args)...);
    }
    void reserve(size_type count) { ensure().reserve(count); }
    void rehash(size_type count) { ensure().rehash(count); }
    void swap(NameMap& other) noexcept { data_.swap(other.data_); }
    friend bool operator==(const NameMap& lhs, const NameMap& rhs) {
        if (!lhs.data_ || !rhs.data_) return lhs.empty() && rhs.empty();
        return *lhs.data_ == *rhs.data_;
    }

private:
    struct SharedTag {};
    NameMap(SharedTag, SharedStorage data) noexcept : data_(std::move(data)) {}
    Storage& ensure() {
        if (!data_) data_ = std::make_shared<Storage>();
        return *data_;
    }
    SharedStorage data_;
};

using NameGetter = std::function<std::string(char)>;
using VersionPredicate = std::function<bool(const std::string&)>;
using MaterialPredicate = std::function<bool(const std::string&)>;
using WidthGetter = std::function<std::size_t(const std::string&)>;

/// 状態変数の選択値と、その値に含まれる
/// 特殊文字を再帰的に展開するかどうか。
struct VarValue {
    bool exists{false};
    std::string value{};
    bool expandSPChars{false};
};

using VarGetter = std::function<VarValue(const std::string&)>;

[[nodiscard]] DChar decodeFontPath(std::string_view path);
[[nodiscard]] std::string encodeFontPath(DChar c, std::string_view ext);

[[nodiscard]] std::string formatMsg(
    std::string_view text,
    const VarGetter& getFlag,
    const VarGetter& getStep,
    const VarGetter& getVariant,
    const NameGetter& getName,
    const VersionPredicate& isTargetVersion,
    std::string_view prefixSystemVarName,
    const MaterialPredicate& hasMaterial,
    FontMap& fonts,
    ColorMap& colors);

[[nodiscard]] std::string simpleFormatMsg(
    std::string_view text,
    const VarGetter& flags,
    const VarGetter& steps,
    const VarGetter& variants,
    const VarGetter& sysSteps,
    const NameMap& names,
    const VersionPredicate& isTargetVersion,
    std::string_view prefixSystemVarName);

// Dでは非公開の補助関数。生成済みのモジュール宣言と
// のソース互換のため、 具体的な型で宣言を残す。通
// 常はformatMsg/simpleFormatMsg
// と、 下の公開の置換・折り返し関数を使う。
[[nodiscard]] std::string formatMsgImpl(
    std::string_view text,
    const VarGetter& getFlag,
    const VarGetter& getStep,
    const VarGetter& getVariant,
    const NameGetter& getName,
    const VersionPredicate& isTargetVersion,
    std::string_view prefixSystemVarName,
    const MaterialPredicate& hasMaterial,
    FontMap& fonts,
    ColorMap& colors,
    bool full,
    std::size_t startIndex,
    std::size_t stack);

/// UTF-8/UTF-16/UTF-32文字列を表示用の書記素へ分割す
/// る。 D版と同じく、半角の濁点・半濁点は前のまとまりから分離する。
[[nodiscard]] std::vector<Grapheme> toGraphemeArray(std::string_view str);
[[nodiscard]] std::vector<Grapheme> toGraphemeArray(std::u16string_view str);
[[nodiscard]] std::vector<Grapheme> toGraphemeArray(std::u32string_view str);
[[nodiscard]] std::vector<Grapheme> toGraphemeArray(std::wstring_view str);
[[nodiscard]] inline std::vector<Grapheme> toGraphemeArray(std::u8string_view str) {
    return toGraphemeArray(std::string_view(reinterpret_cast<const char*>(str.data()), str.size()));
}

// 通常のC++文字列でもDのテンプレート相当の処理を使える汎用オーバーロ
// ード。 すべての文字コード変換を同じD互換の書記素デコーダーへ渡す。
template <typename CharT, typename Traits, typename Allocator>
[[nodiscard]] std::vector<Grapheme> toGraphemeArray(
    const std::basic_string<CharT, Traits, Allocator>& str)
{
    if constexpr (std::is_same_v<CharT, char>) {
        return toGraphemeArray(std::string_view(str.data(), str.size()));
    } else if constexpr (std::is_same_v<CharT, char8_t>) {
        return toGraphemeArray(std::u8string_view(str.data(), str.size()));
    } else if constexpr (std::is_same_v<CharT, char16_t>) {
        return toGraphemeArray(std::u16string_view(str.data(), str.size()));
    } else if constexpr (std::is_same_v<CharT, char32_t>) {
        return toGraphemeArray(std::u32string_view(str.data(), str.size()));
    } else if constexpr (std::is_same_v<CharT, wchar_t>) {
        return toGraphemeArray(std::wstring_view(str.data(), str.size()));
    } else {
        static_assert(std::is_same_v<CharT, void>,
                      "msgutils requires a Unicode character string");
    }
}

void sortChars(std::vector<char>& chars);
void sortChars(std::string& chars);

void replOn(
    DString& dtext,
    DString& buffer,
    std::size_t& index,
    std::u32string_view oldValue,
    std::u32string_view newValue,
    DChar fromChar,
    DChar targetChar);
void replOff(DString& dtext, DString& buffer, std::size_t& index, DChar targetChar);

[[nodiscard]] std::string replTextFlagStepImpl(
    std::string_view text,
    std::string_view oldValue,
    std::string_view newValue,
    DChar primary,
    DChar secondary,
    DChar tertiary,
    DChar target);

template <DChar Ch1, DChar Ch2, DChar Ch3, DChar Ch4>
[[nodiscard]] std::string replTextFlagStep(
    std::string_view text, std::string_view oldValue, std::string_view newValue)
{
    return replTextFlagStepImpl(text, oldValue, newValue, Ch1, Ch2, Ch3, Ch4);
}

void textUseItems(
    std::string_view text,
    std::vector<std::string>& flags,
    std::vector<std::string>& steps,
    std::vector<std::string>& variables,
    std::vector<std::string>& fonts,
    std::vector<char>& colors);

[[nodiscard]] std::vector<char> namesInText(std::string_view text, bool full);

[[nodiscard]] std::string replTextUseFlag(
    std::string_view text, std::string_view oldFlag, std::string_view newFlag);
[[nodiscard]] std::string replTextUseStep(
    std::string_view text, std::string_view oldStep, std::string_view newStep);
[[nodiscard]] std::string replTextUseVariant(
    std::string_view text, std::string_view oldVariant, std::string_view newVariant);
[[nodiscard]] std::string replStepToVariantInText(
    std::string_view text, std::string_view oldStep, std::string_view newStep);

[[nodiscard]] bool isSPFontFile(std::string_view file);
[[nodiscard]] std::string replTextUseFont(
    std::string_view text, std::string_view oldFont, std::string_view newFont);

[[nodiscard]] DString putColor(DString text, DChar color, std::size_t start, std::size_t end);

[[nodiscard]] std::string wrapMsg(
    std::string_view text,
    std::size_t width,
    const WidthGetter& getWidth,
    bool boundaryCheck,
    std::u32string_view openChars,
    std::u32string_view closeChars,
    std::u32string_view wordRegex,
    FontMap& fonts,
    ColorMap& colors);

[[nodiscard]] DString wrapMsgImpl(
    DString text,
    std::size_t width,
    const WidthGetter& getWidth,
    bool boundaryCheck,
    const std::vector<DString>& openChars,
    const std::vector<DString>& closeChars,
    FontMap& fonts,
    ColorMap& colors,
    FontMap& fonts2,
    ColorMap& colors2,
    std::size_t startIndex,
    std::size_t resultIndex,
    std::u32string_view wordRegex);

[[nodiscard]] std::size_t slicePos(
    std::u32string_view text, std::size_t width, const WidthGetter& getWidth);

// Dの文字配列をu32stringではなくvector
// で保持する呼び出し元向けのオーバーロード。
[[nodiscard]] std::string wrapMsg(
    std::string_view text,
    std::size_t width,
    const WidthGetter& getWidth,
    bool boundaryCheck,
    const std::vector<DChar>& openChars,
    const std::vector<DChar>& closeChars,
    std::u32string_view wordRegex,
    FontMap& fonts,
    ColorMap& colors);

}  // namespace cwxeditor::cwx::msgutils
