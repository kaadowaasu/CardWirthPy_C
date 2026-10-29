#pragma once

#include "cw/lib/compat.hpp"

namespace cw::binary::next {

struct ConversionResult {
    ::cw::lib::String directory;
    std::int32_t data_version{};
    std::size_t resource_errors{};
};

// CardWirthNext のバイナリーシナリオを、Engine の通常の WSN/XML
// の実行時形式へ変換する。source_directory は読み取り専用とし、
// 生成ファイルはすべて destination_directory の下に書く。
[[nodiscard]] ConversionResult convert_scenario(
    const ::cw::lib::String& source_directory,
    const ::cw::lib::String& destination_directory,
    const ::cw::lib::String& skin_type,
    const ::cw::lib::String& skin_name);

}  // namespace cw::binary::next
