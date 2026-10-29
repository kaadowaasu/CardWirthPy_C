#pragma once

#include "cwxeditor_src/lib/portable.hpp"

#include <compare>
#include <ostream>
#include <type_traits>

namespace cwxeditor::lib {

// Dの不変文字列を所有する記述子。内容比較ではnullと空文字列を等
// 価とするが、同一性や値の有無の判定にはisNull()を使う。ネイ
// ティブのstd::string入力は、空でも非nullの値を表す。
class NullableString {
public:
    NullableString() = default;
    NullableString(std::nullptr_t) noexcept {}
    NullableString(StringView value) {
        if (value.data()) value_ = std::make_shared<const String>(value);
    }
    NullableString(const char* value) {
        if (value) value_ = std::make_shared<const String>(value);
    }
    explicit NullableString(const String& value) : value_(std::make_shared<const String>(value)) {}
    explicit NullableString(String&& value) : value_(std::make_shared<const String>(std::move(value))) {}
    NullableString& operator=(const String& value) {
        value_ = std::make_shared<const String>(value); return *this;
    }
    NullableString& operator=(String&& value) {
        value_ = std::make_shared<const String>(std::move(value)); return *this;
    }
    NullableString& operator=(StringView value) { return *this = NullableString(value); }
    NullableString& operator=(const char* value) { return *this = NullableString(value); }
    NullableString& operator=(std::nullptr_t) noexcept { value_.reset(); return *this; }

    [[nodiscard]] bool isNull() const noexcept { return !value_; }
    [[nodiscard]] StringView view() const noexcept {
        return value_ ? StringView(*value_) : StringView{};
    }
    [[nodiscard]] const String& str() const noexcept {
        static const String empty;
        return value_ ? *value_ : empty;
    }
    [[nodiscard]] operator StringView() const noexcept { return view(); }
    // std::stringへ明示的に変換するとnullとの区別が失われる。記述子
    // のコピーや取得値を設定し直す処理ではNullableStringを維持する。
    [[nodiscard]] operator String() const { return str(); }
    [[nodiscard]] bool empty() const noexcept { return view().empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return view().size(); }
    [[nodiscard]] const char* data() const noexcept { return view().data(); }
    [[nodiscard]] auto begin() const noexcept { return view().begin(); }
    [[nodiscard]] auto end() const noexcept { return view().end(); }

    friend bool operator==(const NullableString& a, const NullableString& b) noexcept {
        return a.view() == b.view();
    }
    friend auto operator<=>(const NullableString& a, const NullableString& b) noexcept {
        return a.view() <=> b.view();
    }
    template <typename T>
        requires (!std::is_same_v<std::remove_cvref_t<T>, NullableString> &&
                  std::is_convertible_v<const T&, StringView>)
    friend bool operator==(const NullableString& a, const T& b) noexcept {
        return a.view() == StringView(b);
    }
    template <typename T>
        requires (!std::is_same_v<std::remove_cvref_t<T>, NullableString> &&
                  std::is_convertible_v<const T&, StringView>)
    friend auto operator<=>(const NullableString& a, const T& b) noexcept {
        return a.view() <=> StringView(b);
    }
    friend String operator+(StringView a, const NullableString& b) {
        String result(a);
        result.append(b.view());
        return result;
    }
    friend String operator+(const NullableString& a, const NullableString& b) {
        return a.view() + b;
    }
    friend String operator+(const NullableString& a, StringView b) {
        String result(a.str());
        result.append(b);
        return result;
    }
    friend std::ostream& operator<<(std::ostream& stream, const NullableString& value) {
        return stream << value.view();
    }

private:
    // 記述子のコピーは、非nullの空文字列の領域も含め、不変のバイト列
    // を共有する。元のモデルや呼び出し側のバッファが破棄された後も有効。
    Owner<const String> value_;
};

} // namespace cwxeditor::lib
