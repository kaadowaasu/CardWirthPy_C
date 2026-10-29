#pragma once
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace cwxeditor::lib {
// 入力文字列と一致位置をUTF-32で持ち、Dのdstringの添字単位を保つ。
// 既定のメッセージ用の単語表現を含め、全呼び出し元がこのコンパイル済み処理を使う。
class DRegex {
public:
    explicit DRegex(std::u32string_view pattern);
    ~DRegex();
    DRegex(DRegex&&) noexcept;
    DRegex& operator=(DRegex&&) noexcept;
    DRegex(const DRegex&) = delete;
    DRegex& operator=(const DRegex&) = delete;
    [[nodiscard]] std::vector<std::u32string> matchAll(std::u32string_view subject) const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
