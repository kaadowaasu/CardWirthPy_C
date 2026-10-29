#include "cwxeditor_src/core/cwx/enumutils.hpp"
#include "cwxeditor_src/core/cwx/perf.hpp"

#include <stdexcept>

#include "cwxeditor_src/core/cwx/unicode_case_data.hpp"
#include "cwxeditor_src/core/cwx/unicode_case_index_data.hpp"
#include "cwxeditor_src/core/cwx/unicode_full_case_data.hpp"
#include <algorithm>
namespace cwxeditor::cwx::enumutils {
namespace {

char ascii_to_lower(char value) noexcept {
    return value >= 'A' && value <= 'Z' ? static_cast<char>(value - 'A' + 'a') : value;
}

using decoded_code_point = Utf8Scalar;

decoded_code_point decode_utf8(std::string_view value, std::size_t offset) {
    const auto at = [&](std::size_t index) -> unsigned char {
        return static_cast<unsigned char>(value[index]);
    };
    const auto length = value.size();
    const unsigned char first = at(offset);
    if (first <= 0x7f) return {first, offset + 1};

    std::size_t width = 0;
    char32_t code_point = 0;
    char32_t minimum = 0;
    if (first >= 0xc2 && first <= 0xdf) {
        width = 2;
        code_point = first & 0x1f;
        minimum = 0x80;
    } else if (first >= 0xe0 && first <= 0xef) {
        width = 3;
        code_point = first & 0x0f;
        minimum = 0x800;
    } else if (first >= 0xf0 && first <= 0xf4) {
        width = 4;
        code_point = first & 0x07;
        minimum = 0x10000;
    } else {
        throw std::invalid_argument("invalid UTF-8 sequence");
    }

    if (offset + width > length) throw std::invalid_argument("truncated UTF-8 sequence");
    for (std::size_t index = 1; index < width; ++index) {
        const unsigned char continuation = at(offset + index);
        if ((continuation & 0xc0) != 0x80) {
            throw std::invalid_argument("invalid UTF-8 continuation byte");
        }
        code_point = (code_point << 6) | (continuation & 0x3f);
    }
    if (code_point < minimum || code_point > 0x10ffff ||
        (code_point >= 0xd800 && code_point <= 0xdfff)) {
        throw std::invalid_argument("invalid UTF-8 code point");
    }
    return {code_point, offset + width};
}

void append_utf8(std::string& result, char32_t code_point) {
    if (code_point <= 0x7f) {
        result.push_back(static_cast<char>(code_point));
    } else if (code_point <= 0x7ff) {
        result.push_back(static_cast<char>(0xc0 | (code_point >> 6)));
        result.push_back(static_cast<char>(0x80 | (code_point & 0x3f)));
    } else if (code_point <= 0xffff) {
        result.push_back(static_cast<char>(0xe0 | (code_point >> 12)));
        result.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3f)));
        result.push_back(static_cast<char>(0x80 | (code_point & 0x3f)));
    } else {
        result.push_back(static_cast<char>(0xf0 | (code_point >> 18)));
        result.push_back(static_cast<char>(0x80 | ((code_point >> 12) & 0x3f)));
        result.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3f)));
        result.push_back(static_cast<char>(0x80 | (code_point & 0x3f)));
    }
}

template<std::size_t N>
char32_t map_scalar(char32_t value, const unicode_case::Mapping (&mapping)[N]) noexcept {
    const auto found = std::lower_bound(std::begin(mapping), std::end(mapping), value,
        [](const auto& item, char32_t scalar) { return item.from < scalar; });
    return found != std::end(mapping) && found->from == value ? found->to : value;
}
char32_t unicode_lower(char32_t value) noexcept { return map_scalar(value, unicode_case::lower); }
char32_t unicode_upper(char32_t value) noexcept { return map_scalar(value, unicode_case::upper); }

std::string change_first_code_point(std::string_view value, bool upper) {
    if (value.empty()) return {};
    const auto first = decode_utf8(value, 0);
    for (std::size_t offset = first.next; offset < value.size();) {
        offset = decode_utf8(value, offset).next;
    }
    std::string result;
    result.reserve(value.size());
    append_utf8(result, upper ? unicode_upper(first.value) : unicode_lower(first.value));
    result.append(value.substr(first.next));
    return result;
}

// std.utf.byDcharは既定で不正な入力を置換して復号する。
// 不正な継続バイトの消費など、厳密なforeach復号とは不正列の読み
// 進め方が異なる。icmpのUTF-8入力ではその基本動作を維持する。
Utf8Scalar decode_replacing_utf8(std::string_view value, std::size_t offset) {
    const auto first = static_cast<unsigned char>(value[offset]);
    if (first < 0x80U) return {first, offset + 1U};
    if ((first & 0xc0U) != 0xc0U) return {0xfffd, offset + 1U};
    unsigned control = (first << 1U) & 0xffU;
    char32_t scalar = first;
    constexpr char32_t masks[] = {0x7f, 0x7ff, 0xffff, 0x1fffff};
    for (std::size_t i = 1; i <= 3; ++i) {
        if (i == value.size() - offset) return {0xfffd, offset + i};
        const auto next = static_cast<unsigned char>(value[offset + i]);
        if ((next & 0xc0U) != 0x80U) return {0xfffd, offset + i + 1U};
        scalar = (scalar << 6U) | (next & 0x3fU);
        control = (control << 1U) & 0xffU;
        if ((control & 0x80U) == 0) {
            scalar &= masks[i];
            if ((scalar & ~masks[i - 1U]) == 0 ||
                (i == 2 && scalar >= 0xd800 && scalar <= 0xdfff) ||
                scalar > 0x10ffff) scalar = 0xfffd;
            return {scalar, offset + i + 1U};
        }
    }
    return {0xfffd, offset + 4U};
}

template<std::size_t N, std::size_t I>
std::string change_case(std::string_view value, bool upper,
                       const unicode_case::FullMapping (&mapping)[N],
                       const char32_t (&identity)[I]) {
    const auto fullMapping = [&](char32_t scalar) {
        return std::lower_bound(std::begin(mapping), std::end(mapping), scalar,
            [](const auto& item, char32_t cp) { return item.from < cp; });
    };
    std::size_t index = 0;
    for (std::size_t offset = 0; offset < value.size();) {
        const auto scalar = decode_replacing_utf8(value, offset);
        const auto found = fullMapping(scalar.value);
        const auto simple = upper ? unicode_upper(scalar.value) : unicode_lower(scalar.value);
        const bool full = found != std::end(mapping) && found->from == scalar.value;
        // Phobosの索引には、スカラー値を自身へ対応させる項目
        // もある。値が変わらなくても、後続部分の変換を開始する。
        if (full || simple != scalar.value ||
            std::binary_search(std::begin(identity), std::end(identity), scalar.value)) {
            std::string result;
            result.reserve(value.size());
            // DはU+FFFDも含め、復号したスカラー値の符号化後の
            // 幅だけiを進め、元のバイト列をiで切り出す。この位置を
            // 、デコーダーが消費したバイト数の位置へ置き換えない。
            if (index > value.size()) throw std::out_of_range("Unicode case slice index");
            result.append(value.substr(0, index));
            for (auto cursor = index; cursor < value.size();) {
                const auto current = decode_replacing_utf8(value, cursor);
                cursor = current.next;
                const auto entry = fullMapping(current.value);
                if (entry != std::end(mapping) && entry->from == current.value) {
                    for (unsigned i = 0; i < entry->count; ++i) append_utf8(result, entry->to[i]);
                } else {
                    append_utf8(result, upper ? unicode_upper(current.value) : unicode_lower(current.value));
                }
            }
            return result;
        }
        index += scalar.value <= 0x7f ? 1U : scalar.value <= 0x7ff ? 2U :
                 scalar.value <= 0xffff ? 3U : 4U;
        offset = scalar.next;
    }
    // Phobosは索引が見つからなければ元の不変文字列を返す。ネイティ
    // ブの値文字列も、不正な内容を含めて同じ未変更のバイト列を所有する。
    return std::string(value);
}

template <typename Character>
struct CaseCursor {
    std::basic_string_view<Character> value;
    std::size_t offset{};
    [[nodiscard]] bool empty() const { return offset == value.size(); }
    [[nodiscard]] Utf8Scalar scalar() const {
        if constexpr (std::is_same_v<Character, char>) {
            return decode_replacing_utf8(value, offset);
        } else {
            return {value[offset], offset + 1U};
        }
    }
    [[nodiscard]] char32_t front() const { return scalar().value; }
    void popFront() { offset = scalar().next; }
};

const full_case::Group* fullCaseGroup(const char32_t scalar) {
    const auto group = std::lower_bound(std::begin(full_case::groups),
        std::end(full_case::groups), scalar,
        [](const auto& entry, char32_t value) { return entry.from < value; });
    return group != std::end(full_case::groups) && group->from == scalar ? group : nullptr;
}

bool sameSingleCaseScalar(const char32_t first, const char32_t second) {
    if (first == second) return true;
    const auto contains = [](const full_case::Group* group, const char32_t scalar) {
        if (!group) return false;
        for (unsigned index = 0; index < group->count; ++index) {
            const auto& entry = full_case::entries[group->offset + index];
            if (entry.length == 1 && entry.sequence[0] == scalar) return true;
        }
        return false;
    };
    return contains(fullCaseGroup(first), second) || contains(fullCaseGroup(second), first);
}

template <typename Cursor>
int fullCasedCompare(char32_t first, char32_t second, Cursor& tail, bool exactAlternatives) {
    const auto* group = fullCaseGroup(first);
    if (!group)
        return static_cast<int>(first);
    for (unsigned index = 0; index < group->count; ++index) {
        const auto& entry = full_case::entries[group->offset + index];
        if (exactAlternatives ? entry.sequence[0] != second
                              : !sameSingleCaseScalar(entry.sequence[0], second)) continue;
        if (entry.length == 1) return 0;
        auto candidate = tail;
        unsigned i = 1;
        for (; i < entry.length; ++i) {
            if (candidate.empty() ||
                (exactAlternatives ? candidate.front() != entry.sequence[i]
                                   : !sameSingleCaseScalar(candidate.front(), entry.sequence[i]))) break;
            candidate.popFront();
        }
        if (i == entry.length) {
            tail = candidate; // skipOverは完全に一致した場合だけ位置を確定する。
            return 0;
        }
    }
    return static_cast<int>(full_case::entries[group->offset].sequence[0]);
}

template <typename Character>
int caseCompare(std::basic_string_view<Character> first,
                std::basic_string_view<Character> second, bool exactAlternatives = false) {
    std::size_t i = 0;
    const auto end = std::min(first.size(), second.size());
    for (; i < end; ++i) {
        const auto left = static_cast<std::make_unsigned_t<Character>>(first[i]);
        const auto right = static_cast<std::make_unsigned_t<Character>>(second[i]);
        if ((left | right) >= 0x80U) break;
        if (left == right) continue;
        const auto difference = ascii_to_lower(static_cast<char>(left)) -
                                ascii_to_lower(static_cast<char>(right));
        if (difference) return difference;
    }
    if (i == end) return (first.size() > second.size()) - (second.size() > first.size());
    auto left = CaseCursor<Character>{first.substr(i)};
    auto right = CaseCursor<Character>{second.substr(i)};
    for (;;) {
        if (left.empty()) return right.empty() ? 0 : -1;
        const auto lhs = left.front();
        if (right.empty()) return 1;
        const auto rhs = right.front();
        left.popFront();
        right.popFront();
        if (lhs == rhs) continue;
        const auto comparisonLR = fullCasedCompare(lhs, rhs, right, exactAlternatives);
        if (!comparisonLR) continue;
        const auto comparisonRL = fullCasedCompare(rhs, lhs, left, exactAlternatives);
        if (!comparisonRL) continue;
        return comparisonLR - comparisonRL;
    }
}

}  // namespace

void validateUtf8(std::string_view value) {
    for (std::size_t offset = 0; offset < value.size();) offset = decode_utf8(value, offset).next;
}

Utf8Scalar decodeUtf8Scalar(std::string_view value, std::size_t offset) {
    if (offset >= value.size()) throw std::out_of_range("UTF-8 scalar offset");
    return decode_utf8(value, offset);
}

std::u32string toUtf32(std::string_view value) {
    std::u32string result;
    for (std::size_t offset = 0; offset < value.size();) {
        const auto scalar = decode_utf8(value, offset);
        result.push_back(scalar.value);
        offset = scalar.next;
    }
    return result;
}

std::u32string toUtf32Replacing(std::string_view value) {
    std::u32string result;
    for (std::size_t offset = 0; offset < value.size();) {
        const auto scalar = decode_replacing_utf8(value, offset);
        result.push_back(scalar.value);
        offset = scalar.next;
    }
    return result;
}

int compareIgnoreCase(std::string_view first, std::string_view second) {
    return caseCompare(first, second);
}

int compareDStringIgnoreCase(std::string_view first, std::string_view second) {
    return caseCompare(first, second, true);
}

std::u32string toUtf32(std::u16string_view value) {
    std::u32string result;
    for (std::size_t offset = 0; offset < value.size();) {
        char32_t scalar = value[offset++];
        if (scalar >= 0xd800 && scalar <= 0xdbff) {
            if (offset == value.size() || value[offset] < 0xdc00 || value[offset] > 0xdfff)
                throw std::invalid_argument("invalid UTF-16 surrogate pair");
            scalar = 0x10000 + ((scalar - 0xd800) << 10U) + (value[offset++] - 0xdc00);
        } else if (scalar >= 0xdc00 && scalar <= 0xdfff) {
            throw std::invalid_argument("unpaired UTF-16 low surrogate");
        }
        result.push_back(scalar);
    }
    return result;
}

int compareIgnoreCase(std::u32string_view first, std::u32string_view second) {
    return caseCompare(first, second);
}

std::string stripWhiteSpace(std::string_view value) {
    // Phobos v2.070のstd.string.stripとstd.internal.unicode_tables.isWhiteGen
    // に対応する。Dと同様に両端だけを復号し、触れない内部の
    // 不正バイトを、新たな文字列全体の検証エラーにしない。
    const auto white = isWhiteSpace;
    std::size_t begin = 0, end = value.size();
    while (begin < end) {
        const auto scalar = decode_utf8(value, begin);
        if (!white(scalar.value)) break;
        begin = scalar.next;
    }
    while (end > begin) {
        auto last = end - 1;
        while (last > begin && (static_cast<unsigned char>(value[last]) & 0xc0U) == 0x80U) --last;
        const auto scalar = decode_utf8(value.substr(0, end), last);
        if (scalar.next != end) throw std::invalid_argument("invalid UTF-8 trailing sequence");
        if (!white(scalar.value)) break;
        end = last;
    }
    return std::string(value.substr(begin, end - begin));
}

char32_t lowerScalar(char32_t value) noexcept { return unicode_lower(value); }
char32_t upperScalar(char32_t value) noexcept { return unicode_upper(value); }
std::string lowerCase(std::string_view value) { return change_case(value, false, unicode_case::lower_full, unicode_case_index::lower_identity); }
std::string simpleLowerCase(std::string_view value, bool replaceInvalid) {
    std::string result;
    result.reserve(value.size());
    for (std::size_t offset=0;offset<value.size();) {
        const auto scalar=replaceInvalid ? decode_replacing_utf8(value,offset) : decode_utf8(value,offset);
        offset=scalar.next;
        append_utf8(result,unicode_lower(scalar.value));
    }
    return result;
}
std::string upperCase(std::string_view value) { return change_case(value, true, unicode_case::upper_full, unicode_case_index::upper_identity); }

std::string upperToCap(std::string_view value) {
    if (value.empty()) return {};

    bool is_capitalized = true;
    for (const char character : value) {
        if (character >= 'a' && character <= 'z') {
            is_capitalized = false;
            break;
        }
    }
    if (!is_capitalized) return std::string(value);

    std::string result;
    result.reserve(value.size());
    bool upper_letter = true;
    for (const char character : value) {
        if (upper_letter) {
            result.push_back(character);
            upper_letter = false;
        } else if (character == '_') {
            upper_letter = true;
        } else {
            result.push_back(ascii_to_lower(character));
        }
    }
    return result;
}

std::string capLower(std::string_view value) {
    return change_first_code_point(value, false);
}

std::string capUpper(std::string_view value) {
    ::cwxeditor::cwx::perf::TraceScope trace_enumutils_d136(__FILE__, __LINE__);
    return change_first_code_point(value, true);
}

}  // namespace cwxeditor::cwx::enumutils
