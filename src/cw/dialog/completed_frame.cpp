// UIの実装は一つに統一し、
// 別経路の実装を設けない。
#include "cw/dialog/completed_frame.hpp"

#include <algorithm>
#include <limits>

namespace cw::dialog {

std::shared_ptr<const CompletedDialogFrame> CompletedDialogFrameBuffer::snapshot() const
{
    const std::lock_guard lock(m_mutex);
    return m_front;
}

bool CompletedDialogFrameBuffer::commit(
    const std::shared_ptr<const ::cw::lib::Surface>& source,
    int client_width, int client_height, ::cw::lib::Color background)
{
    if (client_width <= 0 || client_height <= 0) return false;
    const auto width = static_cast<std::size_t>(client_width);
    const auto height = static_cast<std::size_t>(client_height);
    if (width > std::numeric_limits<std::size_t>::max() / height ||
        width * height > std::numeric_limits<std::size_t>::max() / 4) return false;

    // 一部だけ公開された Surface は有効な空フレームでは
    // ない。 ここで最後の完全なフレームを背景に置き換えると、一
    // 時的な生成失敗でダイアログやカード内容が消えて見える。 置
    // き換える描画面を確保・公開する前に、入力全体を検証する。
    std::size_t source_width = 0;
    std::size_t source_height = 0;
    if (source) {
        if (source->size.width < 0 || source->size.height < 0 ||
            ((source->size.width == 0) != (source->size.height == 0))) return false;
        source_width = static_cast<std::size_t>(source->size.width);
        source_height = static_cast<std::size_t>(source->size.height);
        if (source_width &&
            (source_width > std::numeric_limits<std::size_t>::max() / source_height ||
             source_width * source_height > std::numeric_limits<std::size_t>::max() / 4 ||
             source->rgba.size() < source_width * source_height * 4)) return false;
    }

    try {
        auto next = std::make_shared<CompletedDialogFrame>();
        next->width = client_width;
        next->height = client_height;
        next->bgra.resize(width * height * 4);
        for (std::size_t i = 0; i < next->bgra.size(); i += 4) {
            next->bgra[i] = background.b;
            next->bgra[i + 1] = background.g;
            next->bgra[i + 2] = background.r;
            next->bgra[i + 3] = background.a;
        }

        if (source_width && source_height) {
            const auto copy_width = std::min(width, source_width);
            const auto copy_height = std::min(height, source_height);
            for (std::size_t y = 0; y < copy_height; ++y) {
                const auto* src = source->rgba.data() + y * source_width * 4;
                auto* dst = next->bgra.data() + y * width * 4;
                for (std::size_t x = 0; x < copy_width; ++x, src += 4, dst += 4) {
                    dst[0] = src[2];
                    dst[1] = src[1];
                    dst[2] = src[0];
                    dst[3] = src[3];
                }
            }
        }

        {
            const std::lock_guard lock(m_mutex);
            next->generation = ++m_generation;
            m_front = std::move(next);
        }
        return true;
    } catch (...) {
        return false;
    }
}

void CompletedDialogFrameBuffer::clear()
{
    const std::lock_guard lock(m_mutex);
    m_front.reset();
}

} // namespace cw::dialog
