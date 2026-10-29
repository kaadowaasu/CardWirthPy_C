#pragma once

// UIの実装は一つに統一し、
// 別経路の実装を設けない。

#include "cw/lib/compat.hpp"

#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace cw::dialog {

// ネイティブダイアログは変換済みの不変のフレームだけを描く。生成側
// は別の場所で次のフレームを作り、一度のポインター交換で公開する
// 。WM_PAINT が途中の RGBA 画像を見ることはない。
struct CompletedDialogFrame {
    int width{};
    int height{};
    std::uint64_t generation{};
    std::vector<std::uint8_t> bgra;
};

class CompletedDialogFrameBuffer {
public:
    [[nodiscard]] std::shared_ptr<const CompletedDialogFrame> snapshot() const;

    // 入力を変換し、クライアントサイズのフレームの (0, 0) に合成
    // する。確保・変換が失敗した場合は、以前の表示フレームを保持する。
    bool commit(const std::shared_ptr<const ::cw::lib::Surface>& source,
                int client_width, int client_height,
                ::cw::lib::Color background = {240, 240, 240, 255});
    void clear();

private:
    mutable std::mutex m_mutex;
    std::shared_ptr<const CompletedDialogFrame> m_front;
    std::uint64_t m_generation{};
};

} // namespace cw::dialog
