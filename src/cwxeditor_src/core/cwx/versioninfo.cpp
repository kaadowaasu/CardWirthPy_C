#include "cwxeditor_src/core/cwx/versioninfo.hpp"

#include <cstddef>
#include <limits>

namespace cwxeditor::cwx::versioninfo {
namespace {

// Dは遅延初期化の連想配列で、各バージョンにVERSIONS.length
// - iを割り当てる。未知の値は、要求バージョンならint.min
// 、データバージョンならint.maxを使う。
[[nodiscard]] std::ptrdiff_t versionRank(
    ::cwxeditor::lib::StringView version,
    std::ptrdiff_t unknownRank) noexcept {
    for (std::size_t index = 0; index < VERSIONS.size(); ++index) {
        if (VERSIONS[index] == version) {
            return static_cast<std::ptrdiff_t>(VERSIONS.size() - index);
        }
    }
    return unknownRank;
}

}  // namespace

bool isTargetVersion(::cwxeditor::lib::StringView dataVersion,
                     ::cwxeditor::lib::StringView ver) {
    const auto unknownRequestedVersion = static_cast<std::ptrdiff_t>(
        std::numeric_limits<std::int32_t>::min());
    const auto unknownDataVersion = static_cast<std::ptrdiff_t>(
        std::numeric_limits<std::int32_t>::max());

    return versionRank(ver, unknownRequestedVersion) <=
           versionRank(dataVersion, unknownDataVersion);
}

}  // namespace cwxeditor::cwx::versioninfo
