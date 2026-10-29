#include "system.hpp"

#include <ctime>
#include <iomanip>
#include <mutex>
#include <sstream>

namespace cwxeditor::lib {

std::optional<std::string> SystemBackend::format_local_time(
    const std::chrono::system_clock::time_point instant,
    const std::string_view format) const {
    const std::time_t raw = std::chrono::system_clock::to_time_t(instant);
    std::tm calendar{};
    {
        // std::localtime はライブラリの共有状態を使う。
        // 共通ロックを保持して複製し、すべてのバックエンドで競合を防ぐ。
        static std::mutex local_time_mutex;
        const std::lock_guard lock(local_time_mutex);
        const std::tm* value = std::localtime(&raw);
        if (value == nullptr) return std::nullopt;
        calendar = *value;
    }

    const std::string format_text(format);
    std::ostringstream output;
    output << std::put_time(&calendar, format_text.c_str());
    if (!output) return std::nullopt;
    return output.str();
}

}  // namespace cwxeditor::lib
