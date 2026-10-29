#pragma once
#include "audio.hpp"
#include <algorithm>
#include <array>
#include <mutex>
#include <unordered_map>
#include <utility>

namespace cwxeditor::lib {
class RoutedAudioBackend final : public AudioBackend {
public:
    RoutedAudioBackend(std::shared_ptr<AudioBackend> native,
                       std::shared_ptr<AudioBackend> sdl)
        : native_(std::move(native)), sdl_(std::move(sdl)) {}

    bool initialize(std::string& error) override {
        std::string native_error;
        std::string sdl_error;
        native_ready_ = native_ && native_->initialize(native_error);
        sdl_ready_ = sdl_ && sdl_->initialize(sdl_error);
        if (native_ready_ || sdl_ready_) {
            error.clear();
            return true;
        }
        error = !native_error.empty() ? native_error : sdl_error;
        return false;
    }

    [[nodiscard]] bool defer_configuration(
        const AudioConfiguration& request) noexcept override {
        if (request.preference != AudioBackendPreference::Bass || !native_)
            return false;
        const bool files_present = native_->defer_configuration(request);
        try {
            std::scoped_lock lock(mutex_);
            // 不透明な経路の所有関係を維持する。一時停止中のMCI音源は再生中ではない
            // が、BASSだけの再読み込み後も有効。以後の操作もネイティブの所有元へ届
            // ける必要があり、playing()では破棄済みかどうかを判定できない。
            native_ready_ = false;
            configuration_request_ = request;
            configuration_ = {};
        } catch (...) {
            return false;
        }
        return files_present;
    }

    AudioConfigurationResult configure(const AudioConfiguration& request) override {
        stop_all();
        std::array<AudioConfigurationResult, 2U> candidates{};
        candidates[0] = native_ ? native_->configure(request) : AudioConfigurationResult{};
        candidates[1] = sdl_ ? sdl_->configure(request) : AudioConfigurationResult{};
        native_ready_ = candidates[0].available();
        sdl_ready_ = candidates[1].available();
        auto selected = select_audio_configuration(request, candidates);
        std::scoped_lock lock(mutex_);
        configuration_request_ = request;
        configuration_ = std::move(selected);
        return configuration_;
    }

    [[nodiscard]] AudioConfigurationResult configuration() const override {
        std::scoped_lock lock(mutex_);
        return configuration_;
    }

    AudioHandle play(const AudioSource& source, const AudioPlayOptions& options) override {
        std::shared_ptr<AudioBackend> first;
        std::shared_ptr<AudioBackend> second;
        switch (options.backend) {
        case AudioBackendPreference::SDL:
            first = sdl_ready_ ? sdl_ : nullptr;
            second = native_ready_ ? native_ : nullptr;
            break;
        case AudioBackendPreference::Bass:
            // BASSはWindows固有の機能。Windowsアダプ
            // ターはSDLへの代替より先に、動的に読み込んだBASS/BASSMIDI
            // 実装を選ぶ。BassをSDLへ渡すとSoundFont
            // やBASS固有のループ動作が失われる。
            first = native_ready_ ? native_ : nullptr;
            second = sdl_ready_ ? sdl_ : nullptr;
            break;
        case AudioBackendPreference::Native:
            first = native_ready_ ? native_ : nullptr;
            second = sdl_ready_ ? sdl_ : nullptr;
            break;
        case AudioBackendPreference::ExternalApplication:
            return 0U;
        case AudioBackendPreference::Automatic:
            first = native_ready_ ? native_ : nullptr;
            second = sdl_ready_ ? sdl_ : nullptr;
            break;
        }
        AudioHandle inner = first ? first->play(source, options) : 0U;
        auto selected = first;
        if (inner == 0U && configuration_request_.allow_fallback &&
            second && second != first) {
            inner = second->play(source, options);
            selected = second;
        }
        if (inner == 0U || !selected) return 0U;
        std::scoped_lock lock(mutex_);
        AudioHandle handle = next_handle_++;
        if (handle == 0U) handle = next_handle_++;
        tracks_[handle] = Route{std::move(selected), inner, options.bus};
        if (suspended_) {
            auto& route = tracks_[handle];
            route.resume_after_suspend = true;
            route.backend->pause(route.inner);
        }
        return handle;
    }

    void pause(AudioHandle handle) override {
        std::scoped_lock lock(mutex_);
        if (auto it = tracks_.find(handle); it != tracks_.end()) {
            it->second.user_paused = true;
            it->second.resume_after_suspend = false;
            it->second.backend->pause(it->second.inner);
        }
    }
    void resume(AudioHandle handle) override {
        std::scoped_lock lock(mutex_);
        if (auto it = tracks_.find(handle); it != tracks_.end()) {
            it->second.user_paused = false;
            it->second.resume_after_suspend = suspended_;
            if (!suspended_) it->second.backend->resume(it->second.inner);
        }
    }
    void set_suspended(bool suspended) override {
        std::scoped_lock lock(mutex_);
        if (suspended_ == suspended) return;
        suspended_ = suspended;
        for (auto& [handle, route] : tracks_) {
            if (suspended) {
                route.resume_after_suspend = !route.user_paused && route.backend->playing(route.inner);
                if (route.resume_after_suspend) route.backend->pause(route.inner);
            } else if (route.resume_after_suspend) {
                route.resume_after_suspend = false;
                route.backend->resume(route.inner);
            }
        }
    }
    void stop(AudioHandle handle, std::chrono::milliseconds fade = {}) noexcept override {
        try {
            Route route;
            {
                std::scoped_lock lock(mutex_);
                const auto found = tracks_.find(handle);
                if (found == tracks_.end()) return;
                route = found->second;
                tracks_.erase(found);
            }
            route.backend->stop(route.inner, fade);
        } catch (...) {
        }
    }
    void stop_bus(AudioBus bus) noexcept override {
        try {
            if (native_) native_->stop_bus(bus);
            if (sdl_) sdl_->stop_bus(bus);
            std::scoped_lock lock(mutex_);
            std::erase_if(tracks_, [bus](const auto& value) { return value.second.bus == bus; });
        } catch (...) {
        }
    }
    void stop_all() noexcept override {
        try {
            if (native_) native_->stop_all();
            if (sdl_) sdl_->stop_all();
            std::scoped_lock lock(mutex_);
            tracks_.clear();
        } catch (...) {
        }
    }
    void seek(AudioHandle handle, std::chrono::milliseconds value) override {
        with(handle, [value](auto& backend, auto inner) { backend.seek(inner, value); });
    }
    std::chrono::milliseconds position(AudioHandle handle) const override {
        return query<std::chrono::milliseconds>(handle, {}, [](auto& backend, auto inner) {
            return backend.position(inner);
        });
    }
    std::chrono::milliseconds duration(AudioHandle handle) const override {
        return query<std::chrono::milliseconds>(handle, {}, [](auto& backend, auto inner) {
            return backend.duration(inner);
        });
    }
    bool playing(AudioHandle handle) const override {
        std::scoped_lock lock(mutex_);
        const auto it = tracks_.find(handle);
        return it != tracks_.end() && (it->second.resume_after_suspend ||
            it->second.backend->playing(it->second.inner));
    }
    void set_bus_volume(AudioBus bus, float value) noexcept override {
        try {
            if (native_) native_->set_bus_volume(bus, value);
            if (sdl_) sdl_->set_bus_volume(bus, value);
        } catch (...) {
        }
    }
    void cancel_fade_in(AudioHandle handle) noexcept override {
        try {
            with(handle, [](auto& backend, const auto inner) {
                backend.cancel_fade_in(inner);
            });
        } catch (...) {
        }
    }
    void set_track_volume(AudioHandle handle, float value) noexcept override {
        try {
            with(handle, [value](auto& backend, const auto inner) {
                backend.set_track_volume(inner, value);
            });
        } catch (...) {
        }
    }
    std::vector<std::string> supported_formats() const override {
        std::vector<std::string> result;
        if (native_) result = native_->supported_formats();
        if (sdl_) {
            for (auto format : sdl_->supported_formats())
                if (std::find(result.begin(), result.end(), format) == result.end())
                    result.push_back(std::move(format));
        }
        return result;
    }

private:
    struct Route {
        std::shared_ptr<AudioBackend> backend;
        AudioHandle inner{};
        AudioBus bus{AudioBus::SoundEffect};
        bool resume_after_suspend{};
        bool user_paused{};
    };

    template <typename F>
    void with(AudioHandle handle, F&& action) {
        Route route;
        {
            std::scoped_lock lock(mutex_);
            const auto found = tracks_.find(handle);
            if (found == tracks_.end()) return;
            route = found->second;
        }
        action(*route.backend, route.inner);
    }

    template <typename T, typename F>
    T query(AudioHandle handle, T fallback, F&& action) const {
        Route route;
        {
            std::scoped_lock lock(mutex_);
            const auto found = tracks_.find(handle);
            if (found == tracks_.end()) return fallback;
            route = found->second;
        }
        return action(*route.backend, route.inner);
    }

    std::shared_ptr<AudioBackend> native_;
    std::shared_ptr<AudioBackend> sdl_;
    mutable std::mutex mutex_;
    std::unordered_map<AudioHandle, Route> tracks_;
    AudioHandle next_handle_{1U};
    bool native_ready_{};
    bool sdl_ready_{};
    bool suspended_{};
    AudioConfiguration configuration_request_{};
    AudioConfigurationResult configuration_{};
};

} // namespace cwxeditor::lib
