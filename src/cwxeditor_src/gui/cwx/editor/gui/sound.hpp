#pragma once

// cwx/editor/gui/sound.dの
// 移植可能なC++20実装。再生はOS側の音声
// 部品へ送る。 公開定数はDと同じ値を保ち、設
// 定ファイルに変換層を挟まず使えるようにする。

#include "cwxeditor_src/core/cwx/structs.hpp"
#include "cwxeditor_src/lib/portable.hpp"
#include "cwxeditor_src/lib/services.hpp"

#include <chrono>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace cwxeditor::cwx::editor::gui::sound {

namespace lib = ::cwxeditor::lib;
namespace structs = ::cwxeditor::cwx::structs;

using String = lib::String;
using StringView = lib::StringView;
using c_double = double;
using AudioHandle = lib::AudioHandle;
using SoundFontWithVolume = structs::SoundFontWithVolume;
using StopEvent = std::function<void()>;

inline constexpr int SOUND_TYPE_AUTO = 0;
inline constexpr int SOUND_TYPE_SDL = 1;
inline constexpr int SOUND_TYPE_MCI = 2;
inline constexpr int SOUND_TYPE_APP = 3;
inline constexpr int SOUND_TYPE_BASS = 4;
inline constexpr int SOUND_TYPE_SAME_BGM = -1;

inline constexpr StringView CWBGM = "cwbgm";
inline constexpr StringView CWSE = "cwse";

// Dが公開する通知一覧と最後に選んだ再生モードを、
// 通常のC++値として保持する。 GUIツールキッ
// トに依存せず、ライフサイクル通知を登録できる。
extern std::vector<StopEvent> stopBGMEvent;
extern std::vector<StopEvent> stopSEEvent;
extern int lastSoundType;

// 以下の全関数が使うアプリのサービス群を設定する。 音声
// サービスなしの値も有効で、その場合は再生を行わない。
void setServices(lib::Services services);
// 共有サービスの初回接続時に、MainWindowが保存済みの全
// 体音量を渡す。 音声デバイスを初期化せず、設定だけを保持する。
void setServices(lib::Services services, std::uint32_t bgmVolume,
                 std::uint32_t seVolume);
// 閉じるEditorセッションが所有する接続だけを解除する。 同一性を確認
// し、古いウィンドウが新しいセッションのバックエンドを消さないようにする。
void clearServices(const lib::Services& ownerServices);
[[nodiscard]] lib::Services services() noexcept;
[[nodiscard]] lib::AudioConfigurationResult configureAudio(
    const lib::AudioConfiguration& request);
[[nodiscard]] int soundType(lib::AudioBackendPreference preference) noexcept;
// 通常再生を変えず、共通の再生管理の能力とDのplayBGMCW/playSECW
// の選択規則で素材プレビューを解決する。
[[nodiscard]] lib::AudioBackendPreference resolveMaterialPreviewPreference(
    lib::AudioBus bus, int requestedType, int savedBgmType, bool legacy,
    StringView path);

void initWinmm();
void disposeWinmm();
void initSdl();
void disposeSdl();
// 所有する再生を止めて設定キャッシュを破棄する。 代替実装の初
// 期化や共有バックエンドの終了はせず、次の要求で初期化し直す。
void disposeSound();
void d_static_constructor_11();

[[nodiscard]] std::uint64_t bgmPos();
void bgmPos(std::uint64_t position);
[[nodiscard]] std::int64_t bgmLen();
[[nodiscard]] std::uint64_t sePos();
void sePos(std::uint64_t position);
[[nodiscard]] std::int64_t seLen();
[[nodiscard]] bool isBGMPlaying();

void playBGM(StringView path, std::uint32_t fadeIn,
             std::uint32_t loopCount, int soundPlayType, bool spLoop);
// DWTの素材補助処理は再生方式を選び、全体×個別の出力音量を適用する。
// 停止通知・再生状態・バックエンドの所有は同じ管理オブジェクトを使う。 戻
// り値はDのplayBGMCWと同じく、APPモードだけfalseとする。
[[nodiscard]] bool playMaterialPreviewWithDwtPolicy(
    lib::AudioBus bus, StringView path, std::uint32_t fadeIn,
    std::uint32_t loopCount, int requestedType, int savedBgmType,
    bool legacy, int globalVolume, std::uint32_t localVolume);
void stopBGM();
// 実行中のMaterialSelectプレビューも含め、共有BGM出力の音量を置き換える。
void bgmVolume(std::uint32_t volume);

void playSE(StringView path, std::uint32_t fadeIn,
            std::uint32_t loopCount, int soundPlayType, bool spLoop);
void stopSE();
void seVolume(std::uint32_t volume);

// Dの互換用入口は、共通AudioBackend設定への薄いラッ
// パーにする。 initBass(dir, fonts)は遅延実
// 行の要求を保存し、実行時ファイルの有無だけを返す。 準備完了
// の判定は、後の共通configure・能力問い合わせで行う。
[[nodiscard]] bool initBass(String bassDir,
                            std::span<const SoundFontWithVolume> soundFonts);
[[nodiscard]] bool initBass(String bassDir,
                            const std::vector<SoundFontWithVolume>& soundFonts);
void initBass();
[[nodiscard]] bool loadBassSoundFont(
    std::span<const SoundFontWithVolume> soundFonts);
void releaseBassSoundFont();
void disposeBass();
[[nodiscard]] bool useBass();
[[nodiscard]] bool canPlayBass(StringView file);

[[nodiscard]] bool isMidi(StringView file);

}  // namespace cwxeditor::cwx::editor::gui::sound
