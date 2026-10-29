#pragma once

#include "cw/lib/documents.hpp"
#include "app/explorer/platform_transfer.hpp"

namespace cw::lib::documents::detail {

// ブラウザーのアップロード境界は、ネイティブ選択画面と同じ文書
// 要求を受け取る。選択・フィルターの別方針を Web 専用の
// 呼び出し箇所へ隠さず、オプション変換を単体検査と共有する。
inline ::cardwirth::explorer::UploadFileOptions web_upload_options(const Request& request)
{
    ::cardwirth::explorer::UploadFileOptions options;
    options.multiple = request.multiple;
    for (const auto& filter : request.filters) {
        bool all_files = false;
        std::vector<std::string> extensions;
        std::size_t start = 0;
        for (;;) {
            const auto end = filter.extensions.find(';', start);
            auto extension = filter.extensions.substr(
                start, end == std::string::npos ? end : end - start);
            const auto first = extension.find_first_not_of(" \t");
            const auto last = extension.find_last_not_of(" \t");
            extension = first == std::string::npos
                ? std::string{} : extension.substr(first, last - first + 1);
            if (extension == "*" || extension == "*.*") {
                all_files = true;
            } else if (!extension.empty()) {
                if (extension.rfind("*.", 0) == 0) extension.erase(0, 2);
                else if (extension.front() == '.') extension.erase(0, 1);
                if (!extension.empty()) extensions.push_back(std::move(extension));
            }
            if (end == std::string::npos) break;
            start = end + 1;
        }
        if (all_files) extensions.clear();
        options.filters.emplace_back(filter.label, std::move(extensions));
    }
    if (!options.filters.empty()) options.extensions = options.filters.front().second;
    return options;
}

}  // namespace cw::lib::documents::detail
