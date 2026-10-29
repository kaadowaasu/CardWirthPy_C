#pragma once

#include <exception>
#include <string>

namespace cwxeditor::cwx::utils::detail {

// debugString/createDebuglnへ渡す例外オブジェクトの書式処理を共通化する。
[[nodiscard]] std::string debugExceptionValue(const std::exception& exception);

}  // namespace cwxeditor::cwx::utils::detail
