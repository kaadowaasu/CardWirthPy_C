#pragma once
#include "cw/lib/compat.hpp"
#include "cw/lib/encoding.hpp"
#include <istream>
#include <stdexcept>
#include <utility>

namespace cw::lib {
// Python の file.read() と同様、I/O エラーの後に部分的な文書を返さない。
inline Bytes read_stream_bytes(std::istream& input) {
    Bytes result;
    char buffer[8192];
    for (;;) {
        input.read(buffer, sizeof(buffer));
        const auto count = input.gcount();
        if (input.bad() || (input.fail() && !input.eof()))
            throw std::runtime_error("file read failed");
        result.insert(result.end(), buffer, buffer + count);
        if (input.eof()) return result;
    }
}

// Python の open(..., encoding="utf-8") は
// BOM を保持して厳密に復号し、行を返す前に CR/CRLF/LF を変換
// する。CPython と同じ 8192 バイトの復号単位に合わせ、不正な単位
// では先行する行も公開しない。一方、前の単位で返した行は例外後も観測できる。
template<class Consumer>
inline void for_each_utf8_text_line(std::istream& input, Consumer&& consume) {
    Bytes pending;
    String line;
    bool pending_cr = false;
    const auto emit = [&] { consume(std::move(line)); line.clear(); };
    char buffer[8192];
    for (;;) {
        input.read(buffer, sizeof(buffer));
        const auto count = input.gcount();
        if (input.bad() || (input.fail() && !input.eof()))
            throw std::runtime_error("file read failed");
        pending.insert(pending.end(), buffer, buffer + count);
        const bool final = input.eof();
        auto complete = pending.size();
        if (!final && !pending.empty()) {
            // CPython の終端ではない復号処理の失敗タイミングに合わせる。
            // E0 80/F0 80/F4 90 は即失敗するが、ED A0..BF
            // は三バイト目または EOF まで保持する。その後、厳密な復号で
            // サロゲートを拒否する。それ以前に完了した単位は公開済みとする。
            auto start = pending.size() - 1;
            while (start && (pending[start] & 0xc0u) == 0x80u) --start;
            const auto first = pending[start];
            const std::size_t expected = first >= 0xc2 && first <= 0xdf ? 2
                : first >= 0xe0 && first <= 0xef ? 3
                : first >= 0xf0 && first <= 0xf4 ? 4 : 0;
            const auto available = pending.size() - start;
            bool valid_prefix = expected && available < expected;
            for (auto i = start + 1; valid_prefix && i < pending.size(); ++i)
                valid_prefix = (pending[i] & 0xc0u) == 0x80u;
            if (valid_prefix && available > 1) {
                const auto second = pending[start + 1];
                valid_prefix = !(first == 0xe0 && second < 0xa0) &&
                    !(first == 0xf0 && second < 0x90) &&
                    !(first == 0xf4 && second > 0x8f);
            }
            if (valid_prefix) complete = start;
        }
        const Bytes bytes(pending.begin(), pending.begin() + complete);
        const auto text = encoding::decode_utf8(bytes, encoding::ErrorPolicy::Strict, false);
        pending.erase(pending.begin(), pending.begin() + complete);
        std::size_t i = 0;
        if (pending_cr && (!text.empty() || final)) {
            pending_cr = false;
            emit();
            if (!text.empty() && text[0] == '\n') i = 1;
        }
        for (; i < text.size(); ++i) {
            if (text[i] == '\r') {
                if (i + 1 == text.size() && !final) pending_cr = true;
                else {
                    emit();
                    if (i + 1 < text.size() && text[i + 1] == '\n') ++i;
                }
            } else if (text[i] == '\n') emit();
            else line.push_back(text[i]);
        }
        if (final) {
            if (!line.empty()) emit();
            return;
        }
    }
}
}
