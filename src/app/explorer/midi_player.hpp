#pragma once

#include <filesystem>
#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace cardwirth::explorer {

// Data配下のSoundFontを使い、標準MIDIファイルをストリーム再生する
// 。 シンセサイザーをPreviewServiceから分離し、動作をテストできる
// ようにする。 通常のSDL_mixerによる再生はMIDI対応に依存させない。
class MidiPlayer {
public:
    explicit MidiPlayer(std::filesystem::path data_directory = {},
                        std::filesystem::path preferred_soundfont = {});
    ~MidiPlayer();
    MidiPlayer(MidiPlayer&&) noexcept;
    MidiPlayer& operator=(MidiPlayer&&) noexcept;
    MidiPlayer(const MidiPlayer&) = delete;
    MidiPlayer& operator=(const MidiPlayer&) = delete;

    [[nodiscard]] std::optional<std::filesystem::path> find_soundfont() const;
    // repeat_countはSDL_mixerと同じ意味で、0は1回再
    // 生、正の値はその回数だけ繰り返し、 -1はstop()まで繰り返す。
    void play(const std::filesystem::path& midi_path, int repeat_count = 0,
              float gain = 1.0F);
    void play_memory(std::span<const std::byte> midi_data,
                     std::span<const std::byte> soundfont_data,
                     int repeat_count = 0, float gain = 1.0F,
                     std::string_view midi_name = "<memory>",
                     std::string_view soundfont_name = "<memory>");
    void set_volume(float gain) noexcept;
    void pause() noexcept;
    void resume() noexcept;
    void stop() noexcept;
    // 所有者のUI・ライフサイクル更新から呼ぶ。音声コールバックでは完了を記
    // 録するだけとし、 SDLストリームの破棄は必ずコールバックの外で行う。
    [[nodiscard]] bool collect_finished() noexcept;
    [[nodiscard]] bool playing() const noexcept;

private:
    void play_buffers(std::vector<unsigned char> midi_data,
                      std::vector<unsigned char> soundfont_data,
                      int repeat_count, float gain,
                      std::string_view midi_name,
                      std::string_view soundfont_name);
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace cardwirth::explorer
