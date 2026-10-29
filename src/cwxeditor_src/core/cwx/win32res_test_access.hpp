#pragma once

// Win32Resのバイト順デコーダーへのテスト専用アクセス。 公開の
// Win32資源インターフェースからは、このヘッダーを読み込まない。

#include "cwxeditor_src/lib/portable.hpp"

#include <cstddef>
#include <cstdint>

namespace cwxeditor::cwx::win32res::test_access {

[[nodiscard]] std::uint32_t readLe32(lib::ByteView data,
                                     std::size_t offset = 0U);
[[nodiscard]] std::uint16_t readLe16(lib::ByteView data,
                                     std::size_t offset = 0U);
void appendLe16(lib::Bytes& data, std::uint16_t value);
void appendLe32(lib::Bytes& data, std::uint32_t value);

}  // namespace cwxeditor::cwx::win32res::test_access
