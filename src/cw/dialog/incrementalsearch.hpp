#pragma once
#include "cw/lib/encoding.hpp"
#include <algorithm>
#include <chrono>
#include <optional>
#include <string>

namespace cw::dialog {
// OS のウィジェットに依存しない、一覧・読み取り専用選択欄の前方一致による移動。ホストは
// 確定した UTF-8 を一度だけ渡し、キーイベントで ASCII 検索を重複実行しない。
class IncrementalSearch {
public:
    using Clock = std::chrono::steady_clock;
    bool composing{};
    void reset() { prefix_.clear(); composing=false; }
    bool active(Clock::time_point now=Clock::now()) const {
        return !prefix_.empty() && now-last_ < std::chrono::seconds(1);
    }
    template<class Label>
    std::optional<int> find(const std::string& text, int count, int current,
                           Label label, Clock::time_point now=Clock::now()) {
        if(composing || text.empty() || count<=0 || text.size()>4096 ||
           std::any_of(text.begin(),text.end(),[](unsigned char c){return c<32 || c==127;}))
            return std::nullopt;
        const auto typed=::cw::lib::encoding::casefold(text);
        const bool extending=active(now) && prefix_!=typed;
        const auto query=extending?prefix_+typed:typed;
        if(query.size()>4096)return std::nullopt;
        const int start=extending && current>=0 && current<count?current:
            (current>=0 && current<count?(current+1)%count:0);
        prefix_=query;last_=now;
        for(int n=0;n<count;++n) {
            const int row=(start+n)%count;
            if(::cw::lib::encoding::casefold(label(row)).starts_with(query)) return row;
        }
        return std::nullopt;
    }
private:
    std::string prefix_;
    Clock::time_point last_{};
};
} // namespace cw::dialog
