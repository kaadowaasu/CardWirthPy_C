#include "sound.hpp"
#include "cwxeditor_src/lib/audio_loop.hpp"
#include "cwxeditor_src/lib/window_host.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <memory>
#include <mutex>
#include <optional>
#include <system_error>
#include <utility>

namespace cwxeditor::cwx::editor::gui::sound {

std::vector<StopEvent> stopBGMEvent;
std::vector<StopEvent> stopSEEvent;
int lastSoundType = SOUND_TYPE_AUTO;

namespace {

using Clock = std::chrono::steady_clock;
using Milliseconds = std::chrono::milliseconds;

struct PlaybackState {
    AudioHandle handle{};
    String path;
    bool active{};
    bool usesBass{};
    int selectedType{SOUND_TYPE_AUTO};
    std::uint64_t offset{};
    std::int64_t duration{};
    Clock::time_point started{};
};

struct SoundController {
    mutable std::recursive_mutex mutex;
    lib::Services services = lib::make_platform_services();
    bool audioInitialized{};
    lib::AudioConfiguration configurationRequest{};
    lib::AudioConfigurationResult configuration{};
    PlaybackState bgm;
    PlaybackState se;
    bool inStopBGM{};
    bool inStopSE{};
    std::uint32_t bgmVolume{100U};
    std::uint32_t seVolume{100U};

    [[nodiscard]] bool ensureAudioLocked() {
        if (!services.audio) return false;
        const bool initializing = !audioInitialized;
        if (initializing) {
            std::string error;
            try {
                audioInitialized = services.audio->initialize(error);
            } catch (...) {
                audioInitialized = false;
            }
        }
        if (audioInitialized && (initializing || !configuration.available())) {
            configuration = services.audio->configure(configurationRequest);
        }
        if (audioInitialized) useConfiguredFontFallbackLocked();
        return audioInitialized;
    }

    [[nodiscard]] bool configuredFontsNeedRetryLocked(
        const lib::AudioConfiguration& request) const noexcept {
        return request.preference == lib::AudioBackendPreference::Bass &&
               !request.sound_fonts.empty() &&
               (configuration.effective != lib::AudioBackendPreference::Bass ||
                !lib::has_audio_capability(
                    configuration.capabilities, lib::AudioCapability::SoundFont));
    }

    void useConfiguredFontFallbackLocked() {
        if (!services.audio ||
            configuration.effective != lib::AudioBackendPreference::Bass ||
            configurationRequest.preference != lib::AudioBackendPreference::Bass ||
            configurationRequest.sound_fonts.empty() ||
            lib::has_audio_capability(
                configuration.capabilities, lib::AudioCapability::SoundFont) ||
            !configurationRequest.allow_fallback) return;

        // WindowsのBASSアダプターは FontInit
        // 失敗後もPCMの再生機能を保持するが、D版はその場
        // 合BASSを破棄する。 共通ファサードでBASS以
        // 外の経路を選び、元の要求は次の遅延再試行用に残す。
        auto fallbackRequest = configurationRequest;
        fallbackRequest.allow_fallback = false;
        lib::AudioConfigurationResult fallback;
        for (const auto preference : {lib::AudioBackendPreference::Native,
                                      lib::AudioBackendPreference::SDL}) {
            fallbackRequest.preference = preference;
            fallback = services.audio->configure(fallbackRequest);
            fallback.requested = configurationRequest.preference;
            fallback.used_fallback = true;
            if (fallback.available() && fallback.effective == preference) break;
        }
        configuration = std::move(fallback);
    }

    [[nodiscard]] lib::AudioConfigurationResult configureLocked(
        const lib::AudioConfiguration& request, const bool force = false) {
        // D版と同様、構成が同じなら読み込み済みのBASSライブラリを保持する。
        // 適用・更新の繰り返しでDLLを解放したり、音声ワーカーを停止して終了
        // 待ちしたりしない。 要求の変更や初期化失敗では、引き続き再試行する。
        const bool retryConfiguredFonts = configuredFontsNeedRetryLocked(request);
        if (!force && !retryConfiguredFonts && audioInitialized && configuration.available() &&
            configurationRequest == request) return configuration;
        configuration = {};
        configurationRequest = request;
        if (!ensureAudioLocked()) {
            configuration = {};
            configuration.requested = request.preference;
            configuration.effective = request.preference;
            configuration.error = "audio service initialization failed";
            return configuration;
        }
        return configuration;
    }

    void resetState(PlaybackState& state) noexcept {
        state = PlaybackState{};
    }

    void stopLocked(PlaybackState& state) noexcept {
        if (state.active && state.handle != 0U && services.audio) {
            try {
                services.audio->stop(state.handle);
            } catch (...) {
            }
        }
        resetState(state);
    }

    void setBusVolumeLocked(lib::AudioBus bus, std::uint32_t value) noexcept {
        if (!services.audio || !ensureAudioLocked()) return;
        try {
            services.audio->set_bus_volume(
                bus, static_cast<float>(std::min(value, 100U)) / 100.0F);
        } catch (...) {
        }
    }

    [[nodiscard]] lib::AudioBackendPreference preference(int type) const noexcept {
        switch (type) {
        case SOUND_TYPE_SDL:
            return lib::AudioBackendPreference::SDL;
        case SOUND_TYPE_MCI:
            return lib::AudioBackendPreference::Native;
        case SOUND_TYPE_APP:
            return lib::AudioBackendPreference::ExternalApplication;
        case SOUND_TYPE_BASS:
            return lib::AudioBackendPreference::Bass;
        default:
            return lib::AudioBackendPreference::Automatic;
        }
    }


    [[nodiscard]] static String lowerExtension(StringView file) {
        const auto extension = pathFromUtf8(file).extension().string();
        String result = extension;
        std::transform(result.begin(), result.end(), result.begin(),
                       [](unsigned char value) {
                           return static_cast<char>(std::tolower(value));
                       });
        return result;
    }

    [[nodiscard]] static bool isMciFormat(StringView file) {
        const auto extension = lowerExtension(file);
        return extension == ".mid" || extension == ".midi" ||
               extension == ".mp3" || extension == ".wav";
    }

    [[nodiscard]] bool canPlayBassLocked(StringView file) const {
        if (file.empty() || !lib::has_audio_capability(
                configuration.capabilities, lib::AudioCapability::Bass)) return false;
        // D版との意図的な追加機能として、サウンドフォントの設定
        // が空でも、BASSによるPCM再生は利用可能にする。
        if (configurationRequest.sound_fonts.empty()) return !isMidi(file);
        // D版は、設定されたフォントを一つも読み込めない場合にBASSを破棄す
        // る。 アダプターのPCM機能は保持するが、遅延再試行で少なくとも一つ
        // 読み込めるまで、 ファサードではすべてのBASSの再生元を拒否する。
        return lib::has_audio_capability(
            configuration.capabilities, lib::AudioCapability::SoundFont);
    }

    [[nodiscard]] int resolveTypeLocked(int requested, StringView file) const {
        int type = requested;
        if (type == SOUND_TYPE_SAME_BGM) type = lastSoundType;
        if (type == SOUND_TYPE_AUTO || type == SOUND_TYPE_SAME_BGM) {
            type = soundType(configuration.effective);
        }
        if (type == SOUND_TYPE_BASS && !canPlayBassLocked(file)) {
            type = soundType(configuration.effective);
        }
        if (type == SOUND_TYPE_MCI && !lib::has_audio_capability(
                configuration.capabilities, lib::AudioCapability::Native)) {
            type = SOUND_TYPE_SDL;
        }
        if (type == SOUND_TYPE_MCI && !isMciFormat(file)) {
            // WinMM はSDL/BASSが受け付ける形式をすべて
            // 開けるわけではない。 D版と同じ代替経路を維持する。
            type = SOUND_TYPE_SDL;
        }
        return type;
    }

    [[nodiscard]] bool launchExternalLocked(PlaybackState& state,
                                             StringView file) {
        if (!services.shell || file.empty()) return false;
        const auto path = pathFromUtf8(file);
        if (path.empty()) return false;
        std::string error;
        try {
            if (!services.shell->open_path(path, error)) return false;
        } catch (...) {
            return false;
        }
        // sound.d の SOUND_TYPE_APP は、意図的に起動後の管理を行
        // わない （D版の dutils.d は Program.launch を直接呼
        // ぶ）。 そのため、停止・シーク用の仮の AudioHandle は作らない。
        state.path = String(file);
        state.selectedType = SOUND_TYPE_APP;
        state.active = false;
        lastSoundType = SOUND_TYPE_APP;
        return true;
    }


    [[nodiscard]] bool playLocked(PlaybackState& state, lib::AudioBus bus,
                                  StringView file, std::uint32_t fadeIn,
                                  std::uint32_t loopCount, bool specialLoop,
                                  int soundPlayType, std::uint32_t volume) {
        stopLocked(state);
        if (file.empty()) return false;

        // バックエンドを初期化してから利用可否を調べる。 先に
        // 調べると、初回のBASS要求だけ空の機能キャッシュを
        // 見て代替経路へ進み、二度目以降だけ成功してしまう。
        // 明示的な外部プレイヤーには音声バックエンドは不要。
        const int requestedType = soundPlayType == SOUND_TYPE_SAME_BGM
            ? lastSoundType : soundPlayType;
        if (requestedType != SOUND_TYPE_APP && !ensureAudioLocked()) return false;
        const int effectiveType = resolveTypeLocked(soundPlayType, file);
        if (effectiveType == SOUND_TYPE_APP) {
            return launchExternalLocked(state, file);
        }
        // ファサードが拒否した要求を、生のBASS経路へ戻さない。 許可され
        // た代替を選んだ場合、effectiveType はすでに Native/SDL
        // 。 それ以外は、バックエンドを呼ぶ前に失敗とする。
        if (effectiveType == SOUND_TYPE_BASS && !canPlayBassLocked(file)) return false;
        lib::AudioSource source;
        source.path = pathFromUtf8(file);
        source.format_hint = source.path.extension().string();


        lib::AudioPlayOptions options;
        options.bus = bus;
        options.backend = preference(effectiveType);
        // D版の sound.d と同様、設定済みの全体音量を一度だけ適用する。 BGM
        // 出力音量は共通の音声系統が管理し、MaterialSelect が全体×個
        // 別の整数プレビュー音量を置き、 後の設定変更がその値を置き換える。 バック
        // エンドのトラックには追加倍率を掛けず、音量が重複して掛からないようにする。
        options.volume = 1.0F;
        options.loop_count = loopCount;
        options.fade_in = Milliseconds(fadeIn);
        options.special_loop = specialLoop; // D版と同様、SLIと埋め込みループ情報の両方の検索をここで制限する。
        if (specialLoop) {
            options.loop_region = lib::read_audio_loop_region(source.path);
            // C++版の共通SDLバックエンドも .sli の範囲を扱う。 D
            // 版がこの情報を渡すのは playBass だけなので、SDLの範
            // 囲ループは D版の再現ではなく共通エディタの追加機能として扱う。
        }

        AudioHandle handle{};
        try {
            handle = services.audio->play(source, options);
        } catch (...) {
            return false;
        }
        if (handle == 0U) return false;

        state.handle = handle;
        state.path = String(file);
        state.active = true;
        state.usesBass = effectiveType == SOUND_TYPE_BASS;
        state.selectedType = effectiveType;
        state.offset = 0U;
        state.started = Clock::now();
        try {
            state.duration = services.audio->duration(handle).count();
        } catch (...) {
            state.duration = 0;
        }
        setBusVolumeLocked(bus, volume);
        lastSoundType = effectiveType;
        return true;
    }

    [[nodiscard]] std::uint64_t positionLocked(const PlaybackState& state) const {
        if (!state.active) return 0U;
        if (services.audio) {
            try {
                const auto value = services.audio->position(state.handle).count();
                // D版のBASS/MCI経路と同様、成功して得たゼロは保持し、不正な値だけを経過時間で補う。
                if (value >= 0) return static_cast<std::uint64_t>(value);
            } catch (...) {
            }
        }
        const auto elapsed = std::chrono::duration_cast<Milliseconds>(
                                 Clock::now() - state.started)
                                 .count();
        if (elapsed <= 0) return state.offset;
        const auto value = state.offset + static_cast<std::uint64_t>(elapsed);
        if (state.duration > 0) {
            return value % static_cast<std::uint64_t>(state.duration);
        }
        return value;
    }

    [[nodiscard]] std::int64_t durationLocked(PlaybackState& state) const {
        if (!state.active) return -1;
        if (services.audio) {
            try {
                const auto value = services.audio->duration(state.handle).count();
                if (value > 0) {
                    state.duration = value;
                    return value;
                }
            } catch (...) {
            }
        }
        return state.duration > 0 ? state.duration : -1;
    }

    [[nodiscard]] bool playingLocked(const PlaybackState& state) const noexcept {
        if (!state.active || !services.audio) return false;
        try {
            return services.audio->playing(state.handle);
        } catch (...) {
            return false;
        }
    }

    static std::filesystem::path pathFromUtf8(StringView value) {
        return structs::pathFromUtf8(value);
    }


};

SoundController& controller() {
    static SoundController value;
    return value;
}

void bindServices(
    lib::Services servicesValue,
    const std::optional<std::pair<std::uint32_t, std::uint32_t>>& initialVolumes) {
    auto& value = controller();
    stopBGM();
    stopSE();
    std::scoped_lock lock(value.mutex);
    // コントローラーはプロセス内で静的に存続するが、ウィンドウの Services
    // 全体はセッションが所有する。 このモジュールで使う二つのバックエ
    // ンドだけを保持し、閉じた組み込みエディタが 次のセッションまでGUI
    // ・タスク・保存領域やコールバックを生かし続けないようにする。
    lib::Services audioServices;
    audioServices.audio = std::move(servicesValue.audio);
    audioServices.shell = std::move(servicesValue.shell);
    value.services = std::move(audioServices);
    value.audioInitialized = false;
    value.configurationRequest = {};
    value.configuration = {};
    if (!initialVolumes) return;

    value.bgmVolume = std::min<std::uint32_t>(initialVolumes->first, 100U);
    value.seVolume = std::min<std::uint32_t>(initialVolumes->second, 100U);
    if (!value.services.audio) return;
    // 音声バックエンドは initialize() 前にも全体音量を保持するため、
    // MainWindow は音声を先に起動せず、保存済みの設定を読み込める。
    value.services.audio->set_bus_volume(
        lib::AudioBus::Bgm, static_cast<float>(value.bgmVolume) / 100.0F);
    value.services.audio->set_bus_volume(
        lib::AudioBus::SoundEffect, static_cast<float>(value.seVolume) / 100.0F);
}

template <typename CallbackRange>
void invokeCallbacks(const CallbackRange& callbacks) noexcept {
    for (const auto& callback : callbacks) {
        if (!callback) continue;
        try {
            callback();
        } catch (...) {
        }
    }
}

struct BassStopNotifications {
    SoundController& owner;
    std::vector<StopEvent> bgm;
    std::vector<StopEvent> se;
    bool dispatchBgm{};
    bool dispatchSe{};

    explicit BassStopNotifications(SoundController& value) : owner(value) {}
    ~BassStopNotifications() {
        std::scoped_lock lock(owner.mutex);
        if (dispatchBgm) owner.inStopBGM = false;
        if (dispatchSe) owner.inStopSE = false;
    }
    void retireLocked(const bool stopBgm, const bool stopSe) {
        if (stopBgm && !owner.inStopBGM) bgm = stopBGMEvent;
        if (stopSe && !owner.inStopSE) se = stopSEEvent;
        if (stopBgm) {
            owner.stopLocked(owner.bgm);
            if (!owner.inStopBGM) owner.inStopBGM = dispatchBgm = true;
        }
        if (stopSe) {
            owner.stopLocked(owner.se);
            if (!owner.inStopSE) owner.inStopSE = dispatchSe = true;
        }
    }
    void notify() const noexcept {
        // 意図的な差(D版と異なる): 古い状態の解放を終えてから通知する。
        // C++の同期通知で新しく設定されたサービスと再生を古い解放から保護する。
        invokeCallbacks(bgm);
        invokeCallbacks(se);
    }
};

void stopWithCallbacks(PlaybackState SoundController::*stateMember,
                       std::vector<StopEvent>& callbacks,
                       bool SoundController::*inStopMember) {
    auto& value = controller();
    std::vector<StopEvent> copy;
    {
        std::scoped_lock lock(value.mutex);
        // D版と同様、再入時も再生は止めるが、コールバックの通知は一度だけにする。
        value.stopLocked(value.*stateMember);
        if (value.*inStopMember) return;
        copy = callbacks;
        value.*inStopMember = true;
    }
    struct StopScope {
        SoundController& controller;
        bool SoundController::*member;
        ~StopScope() {
            std::scoped_lock lock(controller.mutex);
            controller.*member = false;
        }
    } scope{value, inStopMember};
    invokeCallbacks(copy);
}

}  // namespace

void setServices(lib::Services servicesValue) {
    bindServices(std::move(servicesValue), std::nullopt);
}

void setServices(lib::Services servicesValue, const std::uint32_t bgmVolume,
                 const std::uint32_t seVolume) {
    bindServices(std::move(servicesValue), std::pair{bgmVolume, seVolume});
}

void clearServices(const lib::Services& ownerServices) {
    auto& value = controller();
    std::vector<StopEvent> bgmCallbacks;
    std::vector<StopEvent> seCallbacks;
    {
        std::scoped_lock lock(value.mutex);
        if (value.services.audio != ownerServices.audio ||
            value.services.shell != ownerServices.shell) return;
        value.stopLocked(value.bgm);
        value.stopLocked(value.se);
        value.audioInitialized = false;
        value.configurationRequest = {};
        value.configuration = {};
        value.services = {};
        // コールバック登録も同じコントローラーのミューテ
        // ックスで守る。 ロック中に複製し、解除後に呼
        // ぶことで、競合とロック中の再入を両方避ける。
        bgmCallbacks = stopBGMEvent;
        seCallbacks = stopSEEvent;
    }
    invokeCallbacks(bgmCallbacks);
    invokeCallbacks(seCallbacks);
}

lib::Services services() noexcept {
    auto& value = controller();
    std::scoped_lock lock(value.mutex);
    return value.services;
}

lib::AudioConfigurationResult configureAudio(
    const lib::AudioConfiguration& request) {
    auto& value = controller();
    std::scoped_lock lock(value.mutex);
    return value.configureLocked(request);
}

int soundType(const lib::AudioBackendPreference preference) noexcept {
    switch (preference) {
    case lib::AudioBackendPreference::Native: return SOUND_TYPE_MCI;
    case lib::AudioBackendPreference::SDL: return SOUND_TYPE_SDL;
    case lib::AudioBackendPreference::Bass: return SOUND_TYPE_BASS;
    case lib::AudioBackendPreference::ExternalApplication: return SOUND_TYPE_APP;
    case lib::AudioBackendPreference::Automatic: return SOUND_TYPE_AUTO;
    }
    return SOUND_TYPE_AUTO;
}

lib::AudioBackendPreference resolveMaterialPreviewPreference(
    const lib::AudioBus bus, int requestedType, const int savedBgmType,
    const bool legacy, const StringView path) {
    auto& value = controller();
    std::scoped_lock lock(value.mutex);
    if (bus == lib::AudioBus::SoundEffect &&
        requestedType == SOUND_TYPE_SAME_BGM)
        requestedType = savedBgmType;
    // D版のBGMプレビューと同様、SAME_BGM は AUTO として扱う。 保存済みのBGM
    // 設定自体が SAME_BGM の場合も、playSECW の分岐は AUTO に進む。
    if (requestedType == SOUND_TYPE_SAME_BGM) requestedType = SOUND_TYPE_AUTO;
    if (requestedType == SOUND_TYPE_APP)
        return lib::AudioBackendPreference::ExternalApplication;
    if (requestedType != SOUND_TYPE_SDL && requestedType != SOUND_TYPE_MCI) {
        // D版と同様、BASSなど明示指定以外の種類も、既定の AUTO
        // 分岐へ渡す。 BASSが使えない場合でも configuration.effective
        // へ変えず、プレビューの音声系統・旧形式に応じた基準を保
        // つ。 プレビューの系統ごとに AUTO の基準を選び、共
        // 通の再生処理で通常音声と同じ機能・形式の代替判定を行う。
        int baseType = bus == lib::AudioBus::Bgm && !legacy
            ? SOUND_TYPE_SDL : SOUND_TYPE_MCI;
        if (value.services.audio && value.ensureAudioLocked() &&
            value.canPlayBassLocked(path))
            baseType = SOUND_TYPE_BASS;
        return value.preference(value.resolveTypeLocked(baseType, path));
    }
    if (!value.services.audio || !value.ensureAudioLocked())
        return value.preference(requestedType);
    return value.preference(value.resolveTypeLocked(requestedType, path));
}

void initWinmm() {
    auto& value = controller();
    std::scoped_lock lock(value.mutex);
    (void)value.ensureAudioLocked();
}

void disposeWinmm() {
    auto& value = controller();
    std::scoped_lock lock(value.mutex);
    value.audioInitialized = false;
}

void initSdl() {
    auto& value = controller();
    std::scoped_lock lock(value.mutex);
    (void)value.ensureAudioLocked();
}

void disposeSdl() {
    auto& value = controller();
    std::scoped_lock lock(value.mutex);
    value.audioInitialized = false;
}

void disposeSound() {
    stopBGM();
    stopSE();
    auto& value = controller();
    std::scoped_lock lock(value.mutex);
    // 代替バックエンドの選択・初期化をせず、このコントローラーを終了する。 AudioBackend
    // は共有され、終了APIを持たないため、ネイティブ資源の寿命は所有者が管理す
    // る。 disposeBass() は明示的な代替経路への切り替えとして残す。
    value.audioInitialized = false;
    value.configurationRequest = {};
    value.configuration = {};
}

void d_static_constructor_11() {
    (void)controller();
}

std::uint64_t bgmPos() {
    auto& value = controller();
    std::scoped_lock lock(value.mutex);
    return value.positionLocked(value.bgm);
}

void bgmPos(std::uint64_t position) {
    auto& value = controller();
    std::scoped_lock lock(value.mutex);
    if (!value.bgm.active) {
        // 共通バックエンドを新たに初期化せず、キャッシュした音量を再適用する。
        if (value.services.audio) {
            value.services.audio->set_bus_volume(
                lib::AudioBus::Bgm,
                static_cast<float>(std::min(value.bgmVolume, 100U)) / 100.0F);
        }
        return;
    }
    if (value.bgm.active && value.services.audio) {
        const auto audio = value.services.audio;
        const auto handle = value.bgm.handle;
        try {
            audio->seek(handle, Milliseconds(position));
        } catch (...) {
        }
        if (value.services.audio != audio || !value.bgm.active || value.bgm.handle != handle)
            return;
        value.bgm.offset = position;
        value.bgm.started = Clock::now();
        // D版と同様、シーク後にBGM音量を再適用してフェードインを解除す
        // る。 共通バックエンドは両方の表示でフェードの変化を解除する。
        audio->cancel_fade_in(handle);
        if (value.services.audio != audio || !value.bgm.active || value.bgm.handle != handle)
            return;
    }
    value.setBusVolumeLocked(lib::AudioBus::Bgm, value.bgmVolume);
}

std::int64_t bgmLen() {
    auto& value = controller();
    std::scoped_lock lock(value.mutex);
    return value.durationLocked(value.bgm);
}

std::uint64_t sePos() {
    auto& value = controller();
    std::scoped_lock lock(value.mutex);
    return value.positionLocked(value.se);
}

void sePos(std::uint64_t position) {
    auto& value = controller();
    std::scoped_lock lock(value.mutex);
    // D版と同様、有効な効果音がなければ保存音量の再適用は出力に影響しない。
    // 再生していない位置の設定だけで、共通バックエンドを新規初期化しない。
    if (!value.se.active || !value.services.audio) return;
    const auto audio = value.services.audio;
    const auto handle = value.se.handle;
    try {
        audio->seek(handle, Milliseconds(position));
    } catch (...) {
    }
    if (value.services.audio != audio || !value.se.active ||
        value.se.handle != handle) return;
    value.se.offset = position;
    value.se.started = Clock::now();
    // D版と同様、シーク後に保存済みの効果音量を再適用し、フェードインを終了する。
    audio->cancel_fade_in(handle);
    if (value.services.audio != audio || !value.se.active ||
        value.se.handle != handle) return;
    value.setBusVolumeLocked(lib::AudioBus::SoundEffect, value.seVolume);
}

std::int64_t seLen() {
    auto& value = controller();
    std::scoped_lock lock(value.mutex);
    return value.durationLocked(value.se);
}

bool isBGMPlaying() {
    auto& value = controller();
    std::scoped_lock lock(value.mutex);
    return value.playingLocked(value.bgm);
}

void playBGM(StringView path, std::uint32_t fadeIn, std::uint32_t loopCount,
             int soundPlayType, bool specialLoop) {
    auto& value = controller();
    std::scoped_lock lock(value.mutex);
    (void)value.playLocked(value.bgm, lib::AudioBus::Bgm, path, fadeIn,
                           loopCount, specialLoop, soundPlayType,
                           value.bgmVolume);
}

bool playMaterialPreviewWithDwtPolicy(
    const lib::AudioBus bus, const StringView path, const std::uint32_t fadeIn,
    const std::uint32_t loopCount, const int requestedType,
    const int savedBgmType, const bool legacy, const int globalVolume,
    const std::uint32_t localVolume) {
    if (bus != lib::AudioBus::Bgm && bus != lib::AudioBus::SoundEffect)
        return false;

    // D版と同様、共通チャンネルを止めてから設定されたプレイヤーを
    // 解決し、sound.playBGM/playSE で開始す
    // る。 順序を維持し、実際の要求は下のコントローラーへ渡す。
    if (bus == lib::AudioBus::Bgm) stopBGM();
    else stopSE();

    const auto backend = resolveMaterialPreviewPreference(
        bus, requestedType, savedBgmType, legacy, path);
    const auto global = static_cast<std::uint32_t>(
        std::clamp(globalVolume, 0, 100));
    const auto multipliedVolume = static_cast<std::uint64_t>(global) *
                                  localVolume / 100U;
    // D版と同様、乗算した値を音声モジュールの音量プロパティへ渡してか
    // ら範囲内に収める。 個別音量が範囲外の場合も、この順序を保つ。
    const auto effectiveVolume = static_cast<std::uint32_t>(
        std::min<std::uint64_t>(multipliedVolume, 100U));

    auto& value = controller();
    std::scoped_lock lock(value.mutex);
    auto& state = bus == lib::AudioBus::Bgm ? value.bgm : value.se;
    if (bus == lib::AudioBus::Bgm) value.bgmVolume = effectiveVolume;
    else value.seVolume = effectiveVolume;

    // D版と同様、playBGM/playSE より先にモジュールの音量を設定する。
    // 共通の音声系統に先に反映し、新しいプレビューが以前の音量で始まらないようにする。
    value.setBusVolumeLocked(bus, effectiveVolume);
    (void)value.playLocked(state, bus, path, fadeIn, loopCount, !legacy,
                           soundType(backend), effectiveVolume);
    // D版と同様、組み込みのBGMプレイヤーを選んだ場合は、再生を開けなくて
    // も true を返し、APP 分岐は false を返す。 playSECW
    // の呼び出し元は、元のvoid APIと同様、この値を使わない。
    return backend != lib::AudioBackendPreference::ExternalApplication;
}

void stopBGM() {
    stopWithCallbacks(&SoundController::bgm, stopBGMEvent, &SoundController::inStopBGM);
}

void bgmVolume(std::uint32_t volume) {
    auto& value = controller();
    std::scoped_lock lock(value.mutex);
    value.bgmVolume = std::min(volume, 100U);
    value.setBusVolumeLocked(lib::AudioBus::Bgm, value.bgmVolume);
}

void playSE(StringView path, std::uint32_t fadeIn, std::uint32_t loopCount,
            int soundPlayType, bool specialLoop) {
    auto& value = controller();
    std::scoped_lock lock(value.mutex);
    (void)value.playLocked(value.se, lib::AudioBus::SoundEffect, path, fadeIn,
                           loopCount, specialLoop, soundPlayType,
                           value.seVolume);
}

void stopSE() {
    stopWithCallbacks(&SoundController::se, stopSEEvent, &SoundController::inStopSE);
}

void seVolume(std::uint32_t volume) {
    auto& value = controller();
    std::scoped_lock lock(value.mutex);
    value.seVolume = std::min(volume, 100U);
    value.setBusVolumeLocked(lib::AudioBus::SoundEffect, value.seVolume);
}

bool initBass(String bassDir, std::span<const SoundFontWithVolume> fonts) {
    lib::AudioConfiguration request;
    request.preference = lib::AudioBackendPreference::Bass;
    request.allow_fallback = false;
    // 明示的なディレクトリをそのまま保持する。 D版の File.exists()
    // で現在の作業ディレクトリを意味する、空の指定も含む。
    request.library_search_roots.push_back(SoundController::pathFromUtf8(bassDir));
    request.sound_fonts.reserve(fonts.size());
    for (const auto& font : fonts)
        request.sound_fonts.push_back(
            {SoundController::pathFromUtf8(font.path), font.volume});

    auto& value = controller();
    BassStopNotifications notifications(value);
    bool filesPresent{};
    {
        std::scoped_lock lock(value.mutex);
        const bool hadConfiguredBass = lib::has_audio_capability(
            value.configuration.capabilities, lib::AudioCapability::Bass);
        value.configurationRequest = request;
        value.configuration = {};
        if (hadConfiguredBass) {
            // D版の disposeBass と同様、BASSを読み
            // 込んでいた場合だけ公開の二チャンネルを止める。 要求の
            // 準備前にコールバックがサービスを置き換えないよう、所有
            // ハンドルを直接止め、その後で登録済みの停止イベントを通
            // 知する。 D版の解放前通知とは異なり、先に古いネイテ
            // ィブ状態を無効化し、 同期コールバックが設定した新し
            // いサービス・構成を古い解放処理が壊さないようにする。
            notifications.retireLocked(true, true);
        }
        if (value.services.audio)
            filesPresent = value.services.audio->defer_configuration(request);
    }
    notifications.notify();
    // 遅延した結果が示すのはファイルの存在だけ。 BASS_Init とサウンド
    // フォントの読み込みは、initBass()/canPlayBass()
    // または 明示的な configureAudio() の要求まで遅延する。
    return filesPresent;
}

bool initBass(String bassDir,
              const std::vector<SoundFontWithVolume>& fonts) {
    return initBass(std::move(bassDir),
                    std::span<const SoundFontWithVolume>(fonts));
}

void initBass() {
    auto& value = controller();
    std::scoped_lock lock(value.mutex);
    (void)value.configureLocked(value.configurationRequest);
}

bool loadBassSoundFont(std::span<const SoundFontWithVolume> fonts) {
    auto& value = controller();
    std::scoped_lock lock(value.mutex);
    auto request = value.configurationRequest;
    request.sound_fonts.clear();
    for (const auto& font : fonts)
        request.sound_fonts.push_back(
            {SoundController::pathFromUtf8(font.path), font.volume});
    // D版の明示的な loadBassSoundFont と同様、パスと音量一覧が同じでも FontInit
    // を再試行する。 サウンドフォントのファイルを置き換えた場合などに対応する。
    const auto result = value.configureLocked(request, true);
    // この補助処理は、別バックエンドの音源ではなく、BASSのフォントバンクの準備状態を返す。
    return result.available() &&
        lib::has_audio_capability(result.capabilities, lib::AudioCapability::Bass) &&
        lib::has_audio_capability(result.capabilities, lib::AudioCapability::SoundFont);
}

void releaseBassSoundFont() {
    auto& value = controller();
    std::scoped_lock lock(value.mutex);
    auto request = value.configurationRequest;
    request.sound_fonts.clear();
    (void)value.configureLocked(request);
}

void disposeBass() {
    auto& value = controller();
    BassStopNotifications notifications(value);
    {
        std::scoped_lock lock(value.mutex);
        notifications.retireLocked(value.bgm.usesBass, value.se.usesBass);
        lib::AudioConfiguration request;
        request.preference = lib::AudioBackendPreference::Automatic;
        request.allow_fallback = true;
        // 明示的な解放は同じfallback設定でもバックエンドへ伝える。
        value.configuration = {};
        (void)value.configureLocked(request);
    }
    notifications.notify();
}

bool useBass() {
    auto& value = controller();
    std::scoped_lock lock(value.mutex);
    return lib::has_audio_capability(value.configuration.capabilities,
                                     lib::AudioCapability::Bass);
}

bool canPlayBass(StringView file) {
    initBass();
    auto& value = controller();
    std::scoped_lock lock(value.mutex);
    return value.canPlayBassLocked(file);
}

bool isMidi(StringView file) {
    std::ifstream input(
        SoundController::pathFromUtf8(String(file)), std::ios::binary);
    if (!input) return false;
    char header[4]{};
    input.read(header, sizeof(header));
    return input.gcount() == static_cast<std::streamsize>(sizeof(header)) &&
           header[0] == 'M' && header[1] == 'T' && header[2] == 'h' &&
           header[3] == 'd';
}

}  // namespace cwxeditor::cwx::editor::gui::sound
