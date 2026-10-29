#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <functional>
#include <istream>
#include <ostream>
#include <stdexcept>

namespace cardwirth::platform {
// 提供元から取り込む際は、調整やダウンロード前に報告された長さを信用せず、読
// み取り中に上限を守る。 部分的に作られたファイルの削除は呼び出し元が行う。
inline bool copy_bounded_stream(std::istream& input, std::ostream& output,
    std::size_t limit, const std::function<bool()>& cancelled = {}) {
    std::array<char, 65536> buffer;
    std::size_t total = 0;
    for (;;) {
        if (cancelled && cancelled()) return false;
        const auto capacity = total == limit ? std::size_t{1} :
            (std::min)(buffer.size(), limit - total);
        input.read(buffer.data(), static_cast<std::streamsize>(capacity));
        const auto count = static_cast<std::size_t>(input.gcount());
        if (input.bad() || (input.fail() && !input.eof()))
            throw std::runtime_error("選択した文書を読み込めません");
        if (count > limit - total)
            throw std::runtime_error("選択した文書が読み込み上限を超えています");
        if (count) output.write(buffer.data(), static_cast<std::streamsize>(count));
        if (!output) throw std::runtime_error("文書の一時ファイルを書き込めません");
        total += count;
        if (input.eof()) break;
        if (!count) throw std::runtime_error("選択した文書の読み込みが停止しました");
    }
    return !cancelled || !cancelled();
}
} // namespace cardwirth::platform
