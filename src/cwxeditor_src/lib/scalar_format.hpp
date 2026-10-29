#pragma once

#include "cwxeditor_src/lib/real80.hpp"
#include <string>
#include <type_traits>

namespace cwxeditor::lib::scalar_format {

// EditorのDのstd.conv.to!stringによる浮
// 動小数点変換に合わせる。既定の%gは有効数字6桁を出力する。往
// 復変換でビットを保持できなくても、この丸めは仕様どおり。ネイ
// ティブ入力を共通のreal表現へ正確に変換してから整形する。
template <typename T>
[[nodiscard]] std::string floating_to_string(const T value) {
    static_assert(is_real_number_v<T>,
                  "floating_to_string requires a real-number type");

    if constexpr (std::is_same_v<std::remove_cvref_t<T>, Real80>) {
        return value.toString();
    } else {
        return Real80(value).toString();
    }
}

}  // namespace cwxeditor::lib::scalar_format
