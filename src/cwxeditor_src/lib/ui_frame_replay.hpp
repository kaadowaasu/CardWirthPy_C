#pragma once
#include "ui_frame_presentation.hpp"

namespace cwxeditor::lib {
// ネイティブアダプターはこれらの基本描画だけをラスタライズする。巡回、重な
// り順、選択、ラベル、クリッピングは保持済みの共通フレームに従う。呼び出
// しは同期的で、参照されるフレームの保存領域は呼び出し側が所有し続ける。
class FrameRasterSink {
public:
    virtual ~FrameRasterSink() = default;
    // 独立したグループのアルファを、完成した部分木に一度だけ適用する。begin/end
    // の失敗時はフレームを拒否し、不透明な代替表示に黙って切り替えない。
    // 実装は継承した変換・クリップを保持し、失敗時も描画先を復元する。終
    // 了処理中のend_layer(false)はレイヤーを中止する。
    virtual bool begin_layer(Rect bounds, std::uint8_t opacity) = 0;
    virtual bool end_layer(bool publish) noexcept = 0;
    virtual void clear(Color color) = 0;
    virtual void push_clip(Rect bounds) = 0;
    virtual void pop_clip() = 0;
    virtual void decoration(const DecorationPresentation& commands) = 0;
    virtual void text(std::string_view value, Point origin, Color color, const FontSpec& font) = 0;
    virtual void image(const ImageData& data, std::size_t frame, std::optional<Rect> source,
                       Rect destination, Interpolation interpolation) = 0;
    virtual void owned_image(const std::shared_ptr<const ImageData>& data, std::size_t frame,
                             std::optional<Rect> source, Rect destination,
                             Interpolation interpolation) {
        if (data) image(*data, frame, source, destination, interpolation);
    }
    virtual void canvas(const CanvasFramePresentation& view) = 0;
};
// 使用中の値や選択を読まず、コントロールの配置、グリフの計測、アプリケ
// ーションの描画コールバックも行わない。Canvasの命令は記録済み。
bool replay_window_frame(const WindowFramePresentation& frame, FrameRasterSink& sink);
} // namespace cwxeditor::lib
