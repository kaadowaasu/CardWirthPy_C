#include "cw/lib/runtime_access.hpp"
// UIの実装は一つに統一し、
// 別経路の実装を設けない。
#include "cw/dialog/presentation.hpp"
#include "cw/dialog/placement.hpp"
#include "cw/dialog/input_presentation.hpp"
#include "cw/dialog/textedit_menu.hpp"
#include "cw/dialog/text.hpp"
#include "cw/dialog/richtext_scroll.hpp"

#include "cw/cw.hpp"
#include "cw/util.hpp"
#include "cw/frame.hpp"
#include "cw/debug/debugger.hpp"
#include "cw/image.hpp"
#include "app/debug_control_server.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <functional>
#include <stdexcept>
#include "cw/lib/ui_surface.hpp"
#include "cw/lib/stdlib.hpp"
#include "cw/lib/pointer_capture.hpp"
#include "cw/lib/sdl_game_key.hpp"
#include "cw/lib/sdl_key.hpp"
#include "cw/lib/system_clipboard.hpp"
#include "cw/thread.hpp"
#include "cw/setting.hpp"
#include "cw/data.hpp"
#include "cw/util.hpp"
#include <filesystem>
#include <map>

#include <chrono>
#include <cmath>
#include <mutex>
#include <thread>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif

namespace cw::dialog {
void dispatch_dialog_wheel(DialogPresentationProvider& provider,float x,float y,::cw::lib::Integer rotation)
{
    if (auto* text=dynamic_cast<::cw::dialog::text::Text*>(&provider))
        text->dialog_presentation_wheel(x,y,rotation);
    else
        provider.dialog_presentation_scroll(x,y,rotation>0?-3:3);
}


namespace {
std::mutex g_dialog_debug_mutex;
std::map<std::uintptr_t, DialogDebugSnapshot> g_dialog_debug_snapshots;
struct CenterOffsetRequest {
    std::weak_ptr<::cw::lib::Object> model;
    ::cw::lib::Point logical_point;
};
std::mutex g_center_offset_mutex;
std::map<const ::cw::lib::Object*, CenterOffsetRequest> g_center_offsets;
}

void set_dialog_center_offset(const std::shared_ptr<::cw::lib::Object>& model,
                             ::cw::lib::Point logical_point) {
    if (!model) return;
    std::scoped_lock lock(g_center_offset_mutex);
    for (auto it = g_center_offsets.begin(); it != g_center_offsets.end();) {
        if (it->second.model.expired()) it = g_center_offsets.erase(it);
        else ++it;
    }
    g_center_offsets[model.get()] = {model, logical_point};
}

std::optional<DialogCenterOffset> dialog_center_offset(
    const std::shared_ptr<::cw::lib::Object>& model) {
    if (!model) return std::nullopt;
    ::cw::lib::Point point{};
    {
        std::scoped_lock lock(g_center_offset_mutex);
        const auto it = g_center_offsets.find(model.get());
        if (it == g_center_offsets.end()) return std::nullopt;
        if (it->second.model.lock() != model) {
            g_center_offsets.erase(it);
            return std::nullopt;
        }
        point = it->second.logical_point;
    }
    const auto scale = ::cw::g_cwpy ? ::cw::g_cwpy->m_scr_scale : 1.0;
    // Py と同様、先に wins() を適用し、その後で int(point * scr_scale)
    // を求める。 一度にまとめて丸めると結果が変わるため、二つの変換を統合しない。
    const auto axis = [scale](::cw::lib::Integer value) {
        const auto scaled = ::cw::wins(::cw::lib::Value(value));
        return std::trunc(static_cast<double>(std::get<::cw::lib::Integer>(scaled.storage())) * scale);
    };
    return DialogCenterOffset{axis(point.x), axis(point.y)};
}

void publish_dialog_debug_snapshot(const void* owner,
                                   const DialogPresentation& presentation) noexcept {
    if (!owner) return;
    try {
        DialogDebugSnapshot snapshot;
        snapshot.owner = reinterpret_cast<std::uintptr_t>(owner);
        snapshot.route = presentation.route;
        snapshot.title = presentation.title;
        if (presentation.surface) snapshot.surface_size = presentation.surface->size;
        snapshot.controls = presentation.controls;
        std::scoped_lock lock(g_dialog_debug_mutex);
        g_dialog_debug_snapshots[snapshot.owner] = std::move(snapshot);
    } catch (...) {
        // 遠隔の診断処理によって、ダイアログの公開状態を変えない。
    }
}

void clear_dialog_debug_snapshot(const void* owner) noexcept {
    if (!owner) return;
    std::scoped_lock lock(g_dialog_debug_mutex);
    g_dialog_debug_snapshots.erase(reinterpret_cast<std::uintptr_t>(owner));
}

void clear_dialog_debug_snapshots() noexcept {
    std::scoped_lock lock(g_dialog_debug_mutex);
    g_dialog_debug_snapshots.clear();
}

std::vector<DialogDebugSnapshot> dialog_debug_snapshots() {
    std::scoped_lock lock(g_dialog_debug_mutex);
    std::vector<DialogDebugSnapshot> result;
    result.reserve(g_dialog_debug_snapshots.size());
    for (const auto& [_, snapshot] : g_dialog_debug_snapshots) result.push_back(snapshot);
    return result;
}

struct DialogPresentationInputState {
    std::atomic_bool dispatching{false};
    std::atomic_size_t pending_operations{0};
};

namespace {
struct DialogPresentationInputLease final {
    std::shared_ptr<DialogPresentationInputState> state;
    bool debugger_origin{};
    explicit DialogPresentationInputLease(
        std::shared_ptr<DialogPresentationInputState> value, bool origin)
        : state(std::move(value)), debugger_origin(origin) {}
    DialogPresentationInputLease(const DialogPresentationInputLease&) = delete;
    DialogPresentationInputLease& operator=(const DialogPresentationInputLease&) = delete;
    ~DialogPresentationInputLease() {
        state->pending_operations.fetch_sub(1, std::memory_order_acq_rel);
    }
};
}

namespace {
std::shared_ptr<::cw::frame::Frame> active_frame() noexcept;
thread_local DialogPresentationProvider* current_input_provider{};
thread_local std::shared_ptr<void> current_input_operation_context{};
thread_local bool retain_input_callbacks=true;

// このキャッシュは各ダイアログオブジェクトの外に置く。Windows では全
// ダイアログの契約検証用実行ファイルが、 一つのスタックフレームに多数の具
// 体的なダイアログを持つためである。 レジストリーを一つにすることで、各モ
// デルを肥大化させず、各表示プロバイダーに同じ保持型の構成要素を提供する。
struct TouchToolsCache {
    std::shared_ptr<::cw::lib::Surface> surface{};
    const ::cw::lib::Surface* source{};
    ::cw::lib::Size source_size{};
    std::uint64_t source_revision{};
    std::uint64_t output_revision{};
    int configuration{};
    bool strip_hovered{};
    ::cw::lib::Rect strip_bounds{};
    std::optional<DialogPresentationAction> hovered{}, pressed{};
    std::vector<std::pair<::cw::lib::Rect,DialogPresentationAction>> controls;
};
struct DialogPresentationCacheRegistry {
    std::map<const DialogPresentationProvider*,
             std::unique_ptr<::cw::lib::ui::RetainedSurface>> retained_surfaces;
    std::map<const DialogPresentationProvider*, TouchToolsCache> touch_tools_surfaces;
    std::mutex mutex;
};

DialogPresentationCacheRegistry& dialog_presentation_cache_registry()
{
    // ダイアログモデルは静的オブジェクトの破棄まで Engine のグローバル変
    // 数に保持される場合がある。 翻訳単位をまたぐ破棄順に関係なく、基底デストラ
    // クターから保持中の描画面を解放できるようにする。 レジストリーはプロセス
    // の生存期間中維持し、各表示プロバイダーは自身のキャッシュ項目を取り除く。
    static auto* registry = new DialogPresentationCacheRegistry();
    return *registry;
}

auto& retained_dialog_surfaces = dialog_presentation_cache_registry().retained_surfaces;
auto& touch_tools_surfaces = dialog_presentation_cache_registry().touch_tools_surfaces;
auto& retained_dialog_surfaces_mutex = dialog_presentation_cache_registry().mutex;

::cw::lib::ui::RetainedSurface* find_retained_surface(
    const DialogPresentationProvider* provider) noexcept
{
    const std::scoped_lock lock(retained_dialog_surfaces_mutex);
    const auto found = retained_dialog_surfaces.find(provider);
    return found == retained_dialog_surfaces.end() ? nullptr : found->second.get();
}
}

DialogPresentationProvider::DialogPresentationProvider()
    : m_presentation_input_state(std::make_shared<DialogPresentationInputState>()) {}

DialogPresentationProvider::DialogPresentationProvider(
    const DialogPresentationProvider& other)
    : m_presentation_input_state(std::make_shared<DialogPresentationInputState>()),
      m_presentation_refresh_revision(
          other.m_presentation_refresh_revision.load(std::memory_order_acquire)),
      m_presentation_refresh_observed(
          other.m_presentation_refresh_observed.load(std::memory_order_acquire)),
      m_presentation_refresh_acknowledged(
          other.m_presentation_refresh_acknowledged.load(std::memory_order_acquire)),
      m_presentation_refresh_observation_sequence(
          other.m_presentation_refresh_observation_sequence.load(std::memory_order_acquire)),
      m_presentation_refresh_acknowledged_observation_sequence(
          other.m_presentation_refresh_acknowledged_observation_sequence.load(std::memory_order_acquire)),
      m_last_text_event(other.m_last_text_event) {}

DialogPresentationProvider& DialogPresentationProvider::operator=(
    const DialogPresentationProvider& other)
{
    if (this == &other) return *this;
    {
        const std::scoped_lock lock(retained_dialog_surfaces_mutex);
        if (const auto found = retained_dialog_surfaces.find(this); found != retained_dialog_surfaces.end() && found->second)
            found->second->reset();
        retained_dialog_surfaces.erase(this);
        touch_tools_surfaces.erase(this);
    }
    m_presentation_input_state = std::make_shared<DialogPresentationInputState>();
    m_presentation_refresh_revision.store(
        other.m_presentation_refresh_revision.load(std::memory_order_acquire),
        std::memory_order_release);
    m_presentation_refresh_observed.store(
        other.m_presentation_refresh_observed.load(std::memory_order_acquire),
        std::memory_order_release);
    m_presentation_refresh_acknowledged.store(
        other.m_presentation_refresh_acknowledged.load(std::memory_order_acquire),
        std::memory_order_release);
    m_presentation_refresh_observation_sequence.store(
        other.m_presentation_refresh_observation_sequence.load(std::memory_order_acquire),
        std::memory_order_release);
    m_presentation_refresh_acknowledged_observation_sequence.store(
        other.m_presentation_refresh_acknowledged_observation_sequence.load(std::memory_order_acquire),
        std::memory_order_release);
    m_last_text_event = other.m_last_text_event;
    return *this;
}

DialogPresentationProvider::DialogPresentationProvider(
    DialogPresentationProvider&& other) noexcept
    : m_presentation_input_state(std::make_shared<DialogPresentationInputState>()),
      m_presentation_refresh_revision(
          other.m_presentation_refresh_revision.load(std::memory_order_acquire)),
      m_presentation_refresh_observed(
          other.m_presentation_refresh_observed.load(std::memory_order_acquire)),
      m_presentation_refresh_acknowledged(
          other.m_presentation_refresh_acknowledged.load(std::memory_order_acquire)),
      m_presentation_refresh_observation_sequence(
          other.m_presentation_refresh_observation_sequence.load(std::memory_order_acquire)),
      m_presentation_refresh_acknowledged_observation_sequence(
          other.m_presentation_refresh_acknowledged_observation_sequence.load(std::memory_order_acquire)),
      m_last_text_event(std::move(other.m_last_text_event)) {}

DialogPresentationProvider& DialogPresentationProvider::operator=(
    DialogPresentationProvider&& other) noexcept
{
    if (this == &other) return *this;
    {
        const std::scoped_lock lock(retained_dialog_surfaces_mutex);
        if (const auto found = retained_dialog_surfaces.find(this); found != retained_dialog_surfaces.end() && found->second)
            found->second->reset();
        retained_dialog_surfaces.erase(this);
        touch_tools_surfaces.erase(this);
    }
    m_presentation_input_state = std::make_shared<DialogPresentationInputState>();
    m_presentation_refresh_revision.store(
        other.m_presentation_refresh_revision.load(std::memory_order_acquire),
        std::memory_order_release);
    m_presentation_refresh_observed.store(
        other.m_presentation_refresh_observed.load(std::memory_order_acquire),
        std::memory_order_release);
    m_presentation_refresh_acknowledged.store(
        other.m_presentation_refresh_acknowledged.load(std::memory_order_acquire),
        std::memory_order_release);
    m_presentation_refresh_observation_sequence.store(
        other.m_presentation_refresh_observation_sequence.load(std::memory_order_acquire),
        std::memory_order_release);
    m_presentation_refresh_acknowledged_observation_sequence.store(
        other.m_presentation_refresh_acknowledged_observation_sequence.load(std::memory_order_acquire),
        std::memory_order_release);
    m_last_text_event = std::move(other.m_last_text_event);
    return *this;
}

DialogPresentationProvider::~DialogPresentationProvider()
{
    const std::scoped_lock lock(retained_dialog_surfaces_mutex);
    if (const auto found = retained_dialog_surfaces.find(this); found != retained_dialog_surfaces.end() && found->second)
        found->second->reset();
    retained_dialog_surfaces.erase(this);
    touch_tools_surfaces.erase(this);
}

bool DialogPresentationProvider::try_begin_dialog_presentation_input() noexcept
{
    const auto state = m_presentation_input_state;
    if (!state || state->pending_operations.load(std::memory_order_acquire) != 0)
        return false;
    bool expected = false;
    if (!state->dispatching.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel, std::memory_order_acquire))
        return false;
    if (state->pending_operations.load(std::memory_order_acquire) != 0) {
        state->dispatching.store(false, std::memory_order_release);
        return false;
    }
    return true;
}

void DialogPresentationProvider::end_dialog_presentation_input() noexcept
{
    if (m_presentation_input_state)
        m_presentation_input_state->dispatching.store(false, std::memory_order_release);
}

bool DialogPresentationProvider::dialog_presentation_input_blocked() const noexcept
{
    const auto state = m_presentation_input_state;
    return !state || state->dispatching.load(std::memory_order_acquire) ||
        state->pending_operations.load(std::memory_order_acquire) != 0;
}

std::shared_ptr<void> DialogPresentationProvider::hold_dialog_presentation_input() noexcept
{
    const auto state = m_presentation_input_state;
    if (!state) return {};
    state->pending_operations.fetch_add(1, std::memory_order_acq_rel);
    try {
        return std::static_pointer_cast<void>(
            std::make_shared<DialogPresentationInputLease>(state,
                debugger_descendant || dynamic_cast<::cw::debug::debugger::Debugger*>(this)!=nullptr));
    } catch (...) {
        state->pending_operations.fetch_sub(1, std::memory_order_acq_rel);
        return {};
    }
}

DialogPresentationInputGuard::DialogPresentationInputGuard(
    DialogPresentationProvider& provider) noexcept
    : provider_(&provider), previous_(current_input_provider),
      acquired_(provider.try_begin_dialog_presentation_input())
{
    if (acquired_) current_input_provider = &provider;
}

DialogPresentationInputGuard::~DialogPresentationInputGuard()
{
    if (!acquired_) return;
    current_input_provider = previous_;
    provider_->end_dialog_presentation_input();
}

bool current_dialog_input_from_debugger() noexcept
{
    if(current_input_provider)
        return current_input_provider->debugger_descendant ||
            dynamic_cast<::cw::debug::debugger::Debugger*>(current_input_provider)!=nullptr;
    if(current_input_operation_context)
        return std::static_pointer_cast<DialogPresentationInputLease>(current_input_operation_context)->debugger_origin;
    return false;
}

std::shared_ptr<void> capture_current_dialog_input_lease() noexcept
{
    if (current_input_provider)
        return current_input_provider->hold_dialog_presentation_input();
    return current_input_operation_context;
}

bool current_dialog_input_operation_active() noexcept
{
    return current_input_provider != nullptr ||
        static_cast<bool>(current_input_operation_context);
}

ScopedDialogInputRetention::ScopedDialogInputRetention(bool retain) noexcept
    : previous_(retain_input_callbacks)
{
    retain_input_callbacks=retain;
}

ScopedDialogInputRetention::~ScopedDialogInputRetention()
{
    retain_input_callbacks=previous_;
}

::cw::lib::Callback retain_current_dialog_input(::cw::lib::Callback callback)
{
    if (!callback) return {};
    if (!retain_input_callbacks) return callback;
    auto lease = capture_current_dialog_input_lease();
    if (!lease) return callback;
    return [callback = std::move(callback), lease = std::move(lease)](
               const ::cw::lib::Arguments& args,
               const ::cw::lib::KeywordArguments& kwargs) mutable {
        const auto previous = current_input_operation_context;
        current_input_operation_context = lease;
        struct RestoreOperationContext final {
            std::shared_ptr<void> previous;
            ~RestoreOperationContext() {
                current_input_operation_context = std::move(previous);
            }
        } restore{previous};
        return callback(args, kwargs);
    };
}

::cw::lib::ui::RetainedSurface&
DialogPresentationProvider::dialog_retained_surface()
{
    const std::scoped_lock lock(retained_dialog_surfaces_mutex);
    auto& retained = retained_dialog_surfaces[this];
    if (!retained) retained = std::make_unique<::cw::lib::ui::RetainedSurface>();
    (void)retained->consume_animated_content_change();
    return *retained;
}

const ::cw::lib::ui::RetainedSurface&
DialogPresentationProvider::dialog_retained_surface() const
{
    const std::scoped_lock lock(retained_dialog_surfaces_mutex);
    auto& retained = retained_dialog_surfaces[this];
    if (!retained) retained = std::make_unique<::cw::lib::ui::RetainedSurface>();
    return *retained;
}

bool DialogPresentationProvider::dialog_presentation_needs_refresh() const noexcept
{
    const auto requested = m_presentation_refresh_revision.load(std::memory_order_acquire);
    if (requested != m_presentation_refresh_acknowledged.load(std::memory_order_acquire)) {
        return true;
    }
    if (auto* retained = find_retained_surface(this))
        return retained->animated_content_changed();
    return false;
}

void DialogPresentationProvider::begin_dialog_presentation_refresh() noexcept
{
    m_presentation_refresh_observed.store(
        m_presentation_refresh_revision.load(std::memory_order_acquire),
        std::memory_order_release);
    m_presentation_refresh_observation_sequence.fetch_add(1, std::memory_order_acq_rel);
}

void DialogPresentationProvider::dialog_presentation_refreshed() noexcept
{
    const auto acknowledged = m_presentation_refresh_acknowledged.load(std::memory_order_acquire);
    const auto observation = m_presentation_refresh_observation_sequence.load(std::memory_order_acquire);
    const auto acknowledged_observation =
        m_presentation_refresh_acknowledged_observation_sequence.load(std::memory_order_acquire);
    // 描画開始時に観測した版番号を、描画済みとして記録する。初回やホ
    // ストからの更新要求では needs_refresh() を呼ば
    // ない場合があるため、 その場合は完了時に取得した最新の版を記録
    // する。後の公開はそれより大きい版となり、更新待ちのまま残る。
    const auto rendered = observation != acknowledged_observation
        ? m_presentation_refresh_observed.load(std::memory_order_acquire)
        : m_presentation_refresh_revision.load(std::memory_order_acquire);
    // 以前のタイマー確認時の観測値を再利用し
    // ても、描画済みの世代を後退させない。
    m_presentation_refresh_acknowledged.store(
        std::max(acknowledged, rendered), std::memory_order_release);
    m_presentation_refresh_acknowledged_observation_sequence.store(
        observation, std::memory_order_release);
}

void DialogPresentationProvider::invalidate_dialog_presentation() noexcept
{
    if (auto* retained = find_retained_surface(this)) retained->invalidate_all();
    request_dialog_presentation_refresh();
}

void DialogPresentationProvider::invalidate_dialog_presentation(
    ::cw::lib::Rect rect)
{
    dialog_retained_surface().invalidate(rect);
    request_dialog_presentation_refresh();
}

void DialogPresentationProvider::define_dialog_component(
    ::cw::lib::String key, ::cw::lib::Rect rect)
{
    dialog_retained_surface().set_component(std::move(key), rect);
}

bool DialogPresentationProvider::invalidate_dialog_component(
    const ::cw::lib::String& key)
{
    auto* retained = find_retained_surface(this);
    if (!retained || !retained->invalidate_component(key)) return false;
    request_dialog_presentation_refresh();
    return true;
}

void decorate_dialog_touch_tools(DialogPresentation& presentation,
                                 const DialogPresentationProvider& provider)
{
    DialogTouchToolsOptions options{};
    options.enabled=::cw::g_cwpy && ::cw::g_cwpy->m_setting &&
                    ::cw::g_cwpy->m_setting->m_show_tiles;
    options.party=::cw::g_cwpy && ::cw::g_cwpy->m_ydata &&
                  *::cw::g_cwpy->m_ydata && (*::cw::g_cwpy->m_ydata)->m_party;
    decorate_dialog_touch_tools(presentation,provider,options);
}

void decorate_dialog_touch_tools(DialogPresentation& presentation,
                                 const DialogPresentationProvider& provider,
                                 DialogTouchToolsOptions options)
{
    using namespace ::cw::lib;
    if (!presentation.surface || presentation.debugger || provider.debugger_descendant || !options.enabled ||
        presentation.surface->size.width <= 0 || presentation.surface->size.height <= 0) return;
    struct Tool {
        DialogPresentationAction action;
        String resource;
        String tooltip;
        std::shared_ptr<Surface> icon;
    };
    std::vector<Tool> tools;
    const auto message=[](const String& key,const String& fallback) {
        return ::cw::g_cwpy && ::cw::g_cwpy->m_setting && ::cw::g_cwpy->m_setting->m_msgs
            ? ::cw::g_cwpy->m_setting->m_msgs->get(key,fallback) : fallback;
    };
    tools.push_back({DialogPresentationAction::Screenshot,"SCREENSHOT",
        message("screenshot","スクリーンショット")+"(PrtScn)\n"+
            message("desc_screenshot","画面のスクリーンショットを保存します。"),
        ui::resource_dialog_surface("SCREENSHOT")});
    if (options.party)
        tools.push_back({DialogPresentationAction::ScreenshotCards,"SCREENSHOT_HANDS",
            message("screenshot_hands","カード画像")+"(Shift+PrtScn)\n"+
                message("desc_screenshot_hands","所持カードのスクリーンショットを保存します。"),
            ui::resource_dialog_surface("SCREENSHOT_HANDS")});
    if (provider.dialog_presentation_can_copy_detail())
        tools.push_back({DialogPresentationAction::CopyDetail,"COPY",
            message("copy_dialog","詳細のコピー")+"(Ctrl+C)\n"+
                message("desc_copy_dialog","表示中の詳細をクリップボードへコピーします。"),
            ui::resource_dialog_surface("COPY")});
    if (tools.empty()) return;

    const Integer scale=std::max<Integer>(1,static_cast<Integer>(std::lround(
        std::max<::cw::lib::Real>(1.0,::cw::g_UP_WIN))));
    const Integer gap=6*scale,margin=3*scale;
    Integer strip_width=30*scale,total_height=margin*2;
    for (const auto& tool:tools) {
        const Integer width=tool.icon && tool.icon->size.width>1?tool.icon->size.width:24*scale;
        const Integer height=tool.icon && tool.icon->size.height>1?tool.icon->size.height:24*scale;
        strip_width=std::max(strip_width,width+margin*2);
        total_height+=height;
    }
    total_height+=gap*static_cast<Integer>(tools.size()-1);
    const auto source=presentation.surface;
    // 所有者の高さが小さくても、共通の補助キャンバスには全ツールを保持する。
    const auto combined_height=std::max(source->size.height,total_height);
    const Integer strip_y=std::max<Integer>(0,source->size.height-total_height-10*scale);
    std::optional<DialogPresentationAction> hovered,pressed;
    bool strip_hovered=false;
    {
        const std::scoped_lock lock(retained_dialog_surfaces_mutex);
        auto& state=touch_tools_surfaces[&provider];hovered=state.hovered;pressed=state.pressed;strip_hovered=state.strip_hovered;
        // Py と同様、出入りの判定はキャンバスの Y=0 の矩
        // 形ではなく、 余白を含むツールバーの実際の位置で行う。
        state.strip_bounds={source->size.width,strip_y,strip_width,total_height};
        state.controls.clear();
    }
    const int configuration=(options.party?1:0)|(provider.dialog_presentation_can_copy_detail()?2:0)|(scale<<2)|
        (strip_hovered?1<<24:0)|(hovered?(static_cast<int>(*hovered)+1)<<8:0)|(pressed?(static_cast<int>(*pressed)+1)<<16:0);
    std::shared_ptr<Surface> combined;
    bool full=false,pixels_changed=false;
    {
        const std::scoped_lock lock(retained_dialog_surfaces_mutex);
        auto& cache=touch_tools_surfaces[&provider];
        full=!cache.surface || cache.source!=source.get() ||
            cache.source_size.width!=source->size.width || cache.source_size.height!=source->size.height ||
            cache.configuration!=configuration;
        if(full) {
            cache.surface=std::make_shared<Surface>();
            cache.surface->size={source->size.width+strip_width,combined_height};
            cache.surface->rgba.assign(static_cast<std::size_t>(cache.surface->size.width)*cache.surface->size.height*4U,255);
            ui::fill_rect(cache.surface,{0,0,cache.surface->size.width,cache.surface->size.height},{222,222,222,255});
            ui::blend_surface(cache.surface,std::const_pointer_cast<Surface>(source),0,0);
            pixels_changed=true;
        } else if(!presentation.damage.empty()) {
            // ホスト側・ネイティブ側のスナップショットが、前に公開した合
            // 成描画面を保持している場合がある。 部分更新の前に分離し、
            // 利用側が置き換えるまで古いスナップショットを不変に保つ。
            if (cache.surface.use_count() > 1)
                cache.surface = std::make_shared<Surface>(*cache.surface);
            for(auto rect:presentation.damage) {
                const Integer left=std::clamp<Integer>(rect.x,0,source->size.width);
                const Integer top=std::clamp<Integer>(rect.y,0,source->size.height);
                const Integer right=std::clamp<Integer>(rect.x+rect.width,left,source->size.width);
                const Integer bottom=std::clamp<Integer>(rect.y+rect.height,top,source->size.height);
                for(Integer y=top;y<bottom;++y) {
                    const auto source_offset=(static_cast<std::size_t>(y)*source->size.width+left)*4U;
                    const auto target_offset=(static_cast<std::size_t>(y)*cache.surface->size.width+left)*4U;
                    std::copy_n(source->rgba.begin()+static_cast<std::ptrdiff_t>(source_offset),
                        static_cast<std::size_t>(right-left)*4U,
                        cache.surface->rgba.begin()+static_cast<std::ptrdiff_t>(target_offset));
                }
            }
            pixels_changed=true;
        } else if(cache.source_revision!=presentation.surface_revision) {
            // 更新領域を説明できない表示プロバイダーでも、正しく表示できるようにす
            // る。 元の領域全体を更新する負担は、そのプロバイダーだけに限定する。
            if (cache.surface.use_count() > 1)
                cache.surface = std::make_shared<Surface>(*cache.surface);
            ui::blend_surface(cache.surface,std::const_pointer_cast<Surface>(source),0,0);
            pixels_changed=true;
            full=true;
        }
        cache.source=source.get();cache.source_size=source->size;
        cache.source_revision=presentation.surface_revision;
        cache.configuration=configuration;
        if(pixels_changed) ++cache.output_revision;
        presentation.surface_revision=cache.output_revision;
        combined=cache.surface;
    }
    Integer y=strip_y; // Python と同様、所有者の下端から 10px 上に配置する。
    for (std::size_t index=0;index<tools.size();++index) {
        const auto& tool=tools[index];
        const Integer icon_width=tool.icon && tool.icon->size.width>1?tool.icon->size.width:24*scale;
        const Integer icon_height=tool.icon && tool.icon->size.height>1?tool.icon->size.height:24*scale;
        const Rect rect{source->size.width,y,strip_width,icon_height+margin*2};
        if(full) {
            ui::fill_rect(combined,rect,hovered==tool.action?Color{255,255,255,255}:Color{222,222,222,255});
            if (tool.icon && tool.icon->size.width>1 && tool.icon->size.height>1)
                ui::blend_surface(combined,tool.icon,source->size.width+(strip_width-icon_width)/2+(pressed==tool.action?scale:0),y+margin+(pressed==tool.action?scale:0));
            else {
                ui::draw_button_face(combined,rect);
                ui::draw_label(combined,tool.resource.substr(0,1),rect.x+strip_width/2-4*scale+(pressed==tool.action?scale:0),
                    rect.y+margin+(pressed==tool.action?scale:0),12*scale,true,{0,0,0});
            }
        }
        presentation.controls.push_back({tool.action,static_cast<int>(index),
            static_cast<float>(rect.x),static_cast<float>(rect.y),
            static_cast<float>(rect.width),static_cast<float>(rect.height),true,true,
            tool.resource,tool.tooltip});
        presentation.controls.back().hover_highlight=true;
        {
            const std::scoped_lock lock(retained_dialog_surfaces_mutex);
            touch_tools_surfaces[&provider].controls.emplace_back(rect,tool.action);
        }
        y+=icon_height+gap;
    }
    if (full && !strip_hovered) {
        for (Integer row=0;row<combined->size.height;++row)
            for (Integer column=source->size.width;column<combined->size.width;++column)
                combined->rgba[(static_cast<std::size_t>(row)*combined->size.width+column)*4+3]=128;
    }
    if(full) presentation.damage.clear();
    presentation.surface=std::move(combined);
}

namespace {
bool utility_action(DialogPresentationAction action) {
    return action==DialogPresentationAction::Screenshot || action==DialogPresentationAction::ScreenshotCards ||
        action==DialogPresentationAction::CopyDetail;
}
}

bool dispatch_dialog_utility_action(DialogPresentationProvider& provider,
                                    DialogPresentationAction action)
{
    if (action==DialogPresentationAction::Screenshot) {
        const auto* frame_value=::cw::g_cwpy?std::get_if<::cw::lib::ObjectPtr>(&::cw::g_cwpy->m_frame.storage()):nullptr;
        const auto frame=frame_value?std::dynamic_pointer_cast<::cw::frame::Frame>(*frame_value):nullptr;
        if (!frame) throw std::logic_error("screenshot frame is required");
        (void)frame->save_screenshot();return true;
    }
    if (action==DialogPresentationAction::ScreenshotCards) {
        if (!::cw::g_cwpy) throw std::logic_error("screenshot runtime is required");
        ::cw::g_cwpy->force_exec_func([](const auto&,const auto&)->::cw::lib::Value {
            ::cw::util::card_screenshot();return {};
        });
        return true;
    }
    if (action==DialogPresentationAction::CopyDetail) {
        if (!provider.dialog_presentation_can_copy_detail())
            throw std::logic_error("dialog cannot copy details");
        provider.dialog_presentation_copy_detail();return true;
    }
    return false;
}

void update_dialog_utility_hover(DialogPresentationProvider& provider,
    const DialogPresentation& presentation,float x,float y)
{
    std::optional<DialogPresentationAction> action;
    for (const auto& control : presentation.controls)
        if (utility_action(control.action) && x>=control.x && y>=control.y &&
            x<control.x+control.width && y<control.y+control.height) {action=control.action;break;}
    bool changed=false;
    {
        const std::scoped_lock lock(retained_dialog_surfaces_mutex);
        auto& state=touch_tools_surfaces[&provider];
        const auto& strip=state.strip_bounds;
        const bool inside=state.surface && x>=strip.x && x<strip.x+strip.width &&
            y>=strip.y && y<strip.y+strip.height;
        changed=state.hovered!=action || state.strip_hovered!=inside;
        state.hovered=action;state.strip_hovered=inside;
    }
    if(changed)provider.request_dialog_presentation_refresh();
}

bool dispatch_dialog_utility_pointer(DialogPresentationProvider& provider,
    DialogPresentationAction action,float x,float y,bool pressed)
{
    if (!utility_action(action)) return false;
    bool inside=false;
    {
        const std::scoped_lock lock(retained_dialog_surfaces_mutex);
        auto& state=touch_tools_surfaces[&provider];
        for (const auto& [rect,which] : state.controls)
            if (which==action && x>=rect.x && y>=rect.y && x<rect.x+rect.width && y<rect.y+rect.height) {inside=true;break;}
        state.pressed=pressed&&inside?std::optional{action}:std::nullopt;
    }
    provider.request_dialog_presentation_refresh();
    if(!pressed&&inside) dispatch_dialog_utility_action(provider,action);
    return true;
}

void cancel_dialog_utility_pointer(DialogPresentationProvider& provider)
{
    {
        const std::scoped_lock lock(retained_dialog_surfaces_mutex);
        if(const auto found=touch_tools_surfaces.find(&provider);found!=touch_tools_surfaces.end()) found->second.pressed.reset();
    }
    provider.request_dialog_presentation_refresh();
}

::cw::lib::Size fit_dialog_presentation_to_height(
    const DialogPresentation& presentation, ::cw::lib::Size source,
    const ::cw::lib::Integer available_height,
    const ::cw::lib::Integer available_width) noexcept
{
    if (!presentation.fit_work_area_height || presentation.resizable ||
        source.width <= 0 || source.height <= 0 || available_height <= 0)
        return source;
    auto scale=std::min(1.0,static_cast<double>(available_height)/source.height);
    if (available_width > 0)
        scale=std::min(scale,static_cast<double>(available_width)/source.width);
    if (scale >= 1.0) return source;
    return {std::max<::cw::lib::Integer>(1,static_cast<::cw::lib::Integer>(
                std::lround(source.width*scale))),
            std::max<::cw::lib::Integer>(1,static_cast<::cw::lib::Integer>(
                std::lround(source.height*scale)))};
}

bool resize_dialog_presentation(DialogPresentationProvider& provider,
    const DialogPresentation& view, ::cw::lib::Size size) {
    if (!view.resizable || !view.surface || provider.dialog_presentation_finished() ||
        size.width <= 0 || size.height <= 0) return false;
    size.width = std::max(size.width, std::max<::cw::lib::Integer>(1, view.minimum_size.width));
    size.height = std::max(size.height, std::max<::cw::lib::Integer>(1, view.minimum_size.height));
    if (size.width == view.surface->size.width && size.height == view.surface->size.height) return false;
    provider.dialog_presentation_pointer_cancel();
    provider.dialog_presentation_resize(size);
    return true;
}

namespace {
const char* dialog_cursor_name(DialogCursorShape shape) {
    switch(shape) {
    case DialogCursorShape::Back:return "CURSOR_BACK";
    case DialogCursorShape::Forward:return "CURSOR_FORE";
    case DialogCursorShape::Finger:return "CURSOR_FINGER";
    default:return "CURSOR_ARROW";
    }
}
thread_local const DialogPresentationCursorHost* cursor_owner{};
void apply_dialog_cursor(const DialogPresentationCursor& cursor) {
    if(!(SDL_WasInit(SDL_INIT_VIDEO)&SDL_INIT_VIDEO)) return;
    const auto backend=::cw::setting::backend();
    if(backend && backend->create_cursor)
        backend->create_cursor(dialog_cursor_name(cursor.shape),cursor.resource);
}
}
std::chrono::milliseconds dialog_page_repeat_interval() noexcept {
    const auto value=::cw::g_cwpy && ::cw::g_cwpy->m_setting?::cw::g_cwpy->m_setting->m_move_repeat:250;
    return std::chrono::milliseconds(std::max<::cw::lib::Integer>(1,value));
}
DialogPresentationCursor resolve_presentation_cursor(DialogCursorShape shape,
    const std::string& skin_directory,const std::string& base_directory)
{
    DialogPresentationCursor result{shape,{}};
    if(shape==DialogCursorShape::Arrow) return result;
    for(const auto& root:{base_directory,skin_directory}) {
        if(root.empty()) continue;
        const auto path=std::filesystem::u8path(root)/"Resource/Image/Cursor"/
            (std::string(dialog_cursor_name(shape))+".cur");
        std::error_code error;
        if(!std::filesystem::is_regular_file(path,error) || error) continue;
        const auto absolute=std::filesystem::absolute(path,error);
        if(error) continue;
        const auto utf8=absolute.u8string();
        result.resource.assign(utf8.begin(),utf8.end());break;
    }
    return result;
}
DialogPresentationCursor dialog_cursor_at(const std::vector<DialogPresentationControl>& controls,float x,float y)
{
    if(!std::isfinite(x) || !std::isfinite(y)) return {};
    for(auto it=controls.rbegin();it!=controls.rend();++it)
        if(it->enabled && x>=it->x && y>=it->y && x<it->x+it->width && y<it->y+it->height)
            return it->cursor;
    return {};
}
DialogPresentationCursorHost::DialogPresentationCursorHost(
    std::function<void(const DialogPresentationCursor&)> apply)
    :apply_(apply?std::move(apply):apply_dialog_cursor) {}
DialogPresentationCursorHost::~DialogPresentationCursorHost() {clear();}
bool DialogPresentationCursorHost::update(const std::vector<DialogPresentationControl>& controls,float x,float y)
{
    // ホバー表示は、押下・カーソル判定と同じく、最前面の有効なコントロールに適用する。
    std::optional<DialogPresentationControl> hovered;
    if(std::isfinite(x) && std::isfinite(y))
        for(auto it=controls.rbegin();it!=controls.rend();++it)
            if(it->enabled && x>=it->x && y>=it->y && x<it->x+it->width && y<it->y+it->height) {
                if(it->hover_highlight) hovered=*it;
                break;
            }
    const bool hover_changed=hovered!=hovered_;
    hovered_=std::move(hovered);
    const auto next=dialog_cursor_at(controls,x,y);
    const auto generation=::cw::setting::cursor_resource_generation();
    if(cursor_owner==this && current_==next && generation_==generation) return hover_changed;
    apply_(next);current_=next;generation_=generation;cursor_owner=this;
    return hover_changed;
}
void DialogPresentationCursorHost::clear()
{
    hovered_.reset();
    if(cursor_owner!=this) return;
    cursor_owner=nullptr;current_={};apply_(current_);
}
void render_dialog_hover_highlight(SDL_Renderer* renderer, const DialogPresentationControl& control,
                                   const float origin_x, const float origin_y,
                                   const float scale_x, const float scale_y)
{
    if(!renderer || !(scale_x>0.0F) || !(scale_y>0.0F)) return;
    // Windows/wx のホバー表示に合わせ、淡い青の塗りと青い枠を使う。
    // 背景は半透明にし、モデルが描いた見出しやアイコンが見えるようにする。
    const SDL_FRect rect{origin_x+control.x*scale_x,origin_y+control.y*scale_y,
                         control.width*scale_x,control.height*scale_y};
    if(rect.w<=0.0F || rect.h<=0.0F) return;
    SDL_BlendMode previous{};
    SDL_GetRenderDrawBlendMode(renderer,&previous);
    SDL_SetRenderDrawBlendMode(renderer,SDL_BLENDMODE_BLEND);
    // 共通ボタン背景の draw_button_face と同じ角丸の輪郭を使う。
    const float edge=std::max(1.0F,std::round(std::min(scale_x,scale_y)));
    const float radius=::cw::lib::ui::button_corner_radius(rect.w,rect.h);
    const auto inset_at=[&](float row) {
        const float from_edge=std::min(row,rect.h-row);
        if(from_edge>=radius) return 0.0F;
        const float dy=radius-from_edge;
        return radius-std::sqrt(std::max(0.0F,radius*radius-dy*dy));
    };
    const int rows=static_cast<int>(std::ceil(rect.h));
    for(int row=0;row<rows;++row) {
        const float y=rect.y+static_cast<float>(row);
        const float height=std::min(1.0F,rect.y+rect.h-y);
        const float inset=inset_at(static_cast<float>(row)+0.5F);
        const SDL_FRect line{rect.x+inset,y,rect.w-inset*2.0F,height};
        if(line.w<=0.0F) continue;
        const bool frame_row=static_cast<float>(row)<edge || static_cast<float>(row)>=rect.h-edge;
        SDL_SetRenderDrawColor(renderer,0,120,215,frame_row?255:40);
        SDL_RenderFillRect(renderer,&line);
        if(!frame_row) {
            SDL_SetRenderDrawColor(renderer,0,120,215,255);
            const SDL_FRect sides[]{{line.x,y,edge,height},{line.x+line.w-edge,y,edge,height}};
            SDL_RenderFillRects(renderer,sides,2);
        }
    }
    SDL_SetRenderDrawBlendMode(renderer,previous);
}
void DialogSideNavigation::append(DialogPresentation& view,const ::cw::lib::Rect& panel,bool enabled,int index)
{
    const auto generation=::cw::setting::cursor_resource_generation();
    if((!ready_ || generation_!=generation) && enabled) {
        const auto skin=::cw::g_cwpy && ::cw::g_cwpy->m_rsrc
            ?::cw::g_cwpy->m_rsrc->m_skindir:std::string{};
        back_=resolve_presentation_cursor(DialogCursorShape::Back,skin);
        forward_=resolve_presentation_cursor(DialogCursorShape::Forward,skin);
        ready_=true;generation_=generation;
    }
    // Python と同様、右側の判定は x > width//4*3 とし、>= にはしない。中央では操作しない。
    const auto edges=::cw::lib::sideclick_quarter_edges(panel);
    const auto quarter=edges.first-panel.x,right=edges.second-panel.x+1;
    if(quarter<=0 || panel.height<=0) return;
    DialogPresentationControl left{DialogPresentationAction::Previous,index,
        static_cast<float>(panel.x),static_cast<float>(panel.y),static_cast<float>(quarter),
        static_cast<float>(panel.height),enabled,false};
    left.cursor=back_;
    auto next=left;next.action=DialogPresentationAction::Next;
    next.x=static_cast<float>(panel.x+right);next.width=static_cast<float>(panel.width-right);
    next.cursor=forward_;
    view.controls.push_back(std::move(left));view.controls.push_back(std::move(next));
}



namespace {
constexpr std::array<std::uint32_t,48> basic_colours{
    0xff8080,0xffff80,0x80ff80,0x00ff80,0x80ffff,0x0080ff,0xff80c0,0xff80ff,
    0xff0000,0xffff00,0x80ff00,0x00ff40,0x00ffff,0x0080c0,0x8080c0,0xff00ff,
    0x804040,0xff8040,0x00ff00,0x008080,0x004080,0x8080ff,0x800040,0xff0080,
    0x800000,0xff8000,0x008000,0x008040,0x0000ff,0x0000a0,0x800080,0x8000ff,
    0x400000,0x804000,0x004000,0x004040,0x000080,0x000040,0x400040,0x400080,
    0x000000,0x808000,0x808040,0x808080,0x408080,0xc0c0c0,0x400040,0xffffff};
thread_local std::array<std::uint32_t,16> custom_colours=[] {
    std::array<std::uint32_t,16> colours{}; colours.fill(0xffffff); return colours;
}();
::cw::lib::Color unpack_colour(std::uint32_t rgb) {
    return {static_cast<std::uint8_t>(rgb>>16),static_cast<std::uint8_t>(rgb>>8),static_cast<std::uint8_t>(rgb),255};
}
}
namespace {
namespace colour_input=::cw::dialog::input_presentation;
::cw::lib::Rect colour_rect(int x,int y,int w,int h) {
    return {colour_input::px(x),colour_input::px(y),colour_input::px(w),colour_input::px(h)};
}
::cw::lib::Rect colour_field_rect(int index) {
    return colour_rect(index<3?386:288,214+(index%3)*29,48,24);
}
bool colour_digits(const std::string& text) {
    return text.size()<=3 && std::all_of(text.begin(),text.end(),[](unsigned char c){return c>='0' && c<='9';});
}
bool colour_contains(const ::cw::lib::Rect& r,float x,float y) {
    return x>=r.x && y>=r.y && x<r.x+r.width && y<r.y+r.height;
}
}
struct ColourSelectionDialog::EditorState {
    textedit::Buffer buffer;
    textedit::EditMenu menu;
    std::function<std::string()> read;
    std::function<void(const std::string&)> write;
    int drag{-1};
    DialogPresentationModifiers modifiers{};
};
ColourSelectionDialog::ColourSelectionDialog(::cw::lib::Color initial,
    std::function<std::string()> read,std::function<void(const std::string&)> write)
    :colour_(initial),editor_(std::make_unique<EditorState>()) {
    editor_->read=read?std::move(read):presentation_clipboard_text;
    editor_->write=write?std::move(write):presentation_set_clipboard_text;
    from_rgb();
}
ColourSelectionDialog::~ColourSelectionDialog()=default;
void ColourSelectionDialog::from_rgb() {
    const auto hls=::cw::lib::rgb_to_hls({colour_.r/255.0,colour_.g/255.0,colour_.b/255.0});
    // 灰色では前の色相を維持する。後から彩度を上げる時に必要となる。
    if(hls[2]>0) hue_=hls[0]; luminance_=hls[1]; saturation_=hls[2];
}
void ColourSelectionDialog::from_hsl() {
    const auto rgb=::cw::lib::hls_to_rgb({hue_,luminance_,saturation_});
    colour_.r=static_cast<std::uint8_t>(std::clamp(std::lround(rgb[0]*255),0L,255L));
    colour_.g=static_cast<std::uint8_t>(std::clamp(std::lround(rgb[1]*255),0L,255L));
    colour_.b=static_cast<std::uint8_t>(std::clamp(std::lround(rgb[2]*255),0L,255L));
}
int ColourSelectionDialog::component(int index) const {
    switch(index) {
    case 0:return colour_.r; case 1:return colour_.g; case 2:return colour_.b;
    case 3:return static_cast<int>(std::lround(hue_*239));
    case 4:return static_cast<int>(std::lround(saturation_*240));
    default:return static_cast<int>(std::lround(luminance_*240));
    }
}
void ColourSelectionDialog::set_component(int index,int value) {
    value=std::clamp(value,0,index<3?255:index==3?239:240);
    if(index<3) {
        if(index==0) colour_.r=value; if(index==1) colour_.g=value; if(index==2) colour_.b=value;
        from_rgb();
    } else {
        if(index==3) hue_=value/239.0; if(index==4) saturation_=value/240.0; if(index==5) luminance_=value/240.0;
        from_hsl();
    }
    request_dialog_presentation_refresh();
}
bool ColourSelectionDialog::commit() {
    if(edit_<0) return true;
    if(!editor_->buffer.composition.empty())return false;
    const auto& text=editor_->buffer.text;
    if(text.empty() || !colour_digits(text)) return false;
    const int value=std::stoi(text);
    if(value>(edit_<3?255:edit_==3?239:240)) return false;
    set_component(edit_,value);edit_=-1;editor_->drag=-1;
    return true;
}
void ColourSelectionDialog::focus_control(int id) {
    focus_=id;
    if(id>=300 && id<306) {
        edit_=id-300;editor_->buffer.reset(std::to_string(component(edit_)),true);
    } else edit_=-1;
    request_dialog_presentation_refresh();
}
DialogPresentation ColourSelectionDialog::dialog_presentation() {
    using namespace ::cw::lib;
    namespace input=colour_input;
    const Size surface_size{input::px(500),input::px(344)};
    auto& retained=dialog_retained_surface();
    const bool full=retained.needs_full(surface_size);
    if(full) {
        retained.allocate(surface_size,{240,240,240,255});
    }
    auto surface=retained.surface;
    DialogPresentation result;result.route="COLOUR";result.title="色の設定";
    const auto font=ui::configured_wx_font("dlgmsg2",12);
    const auto button_font=ui::configured_wx_font("button",14);
    const auto mix=[](std::uint64_t& seed,std::uint64_t value) {
        seed^=value+0x9e3779b97f4a7c15ULL+(seed<<6U)+(seed>>2U);
    };
    const auto font_signature=[&](const ui::FontStyle& value) {
        std::uint64_t signature=std::hash<String>{}(value.face);
        mix(signature,static_cast<std::uint64_t>(value.pixels));
        mix(signature,static_cast<std::uint64_t>(value.bold));
        mix(signature,static_cast<std::uint64_t>(value.italic));
        return signature;
    };
    const auto label=[&](const String& text,Integer x,Integer y) {
        ui::draw_label(surface,text,x,y,font.pixels,font.bold,{0,0,0},font.face,font.italic);
    };
    const auto focus_border=[&](Rect r) {
        ui::fill_rect(surface,{r.x,r.y,r.width,1},{0,120,215,255});
        ui::fill_rect(surface,{r.x,r.y+r.height-1,r.width,1},{0,120,215,255});
        ui::fill_rect(surface,{r.x,r.y,1,r.height},{0,120,215,255});
        ui::fill_rect(surface,{r.x+r.width-1,r.y,1,r.height},{0,120,215,255});
    };
    const auto hit=[&](int id,Rect rect,bool continuous=false) {
        result.controls.push_back({DialogPresentationAction::Auxiliary,id,
            static_cast<float>(rect.x),static_cast<float>(rect.y),static_cast<float>(rect.width),
            static_cast<float>(rect.height),!finished_,continuous});
    };
    const auto button=[&](const String& text,Rect rect,DialogPresentationAction action,int id,bool paint) {
        input::button(result,surface,text,rect,action,id,!finished_,button_font,paint);
    };

    // 変化しない枠、基本パレット、描画負荷の高い色相・彩
    // 度の平面は保持中の背景に含める。 動的なカーソルと
    // コントロールはそこから復元し、個別に再描画する。
    const auto plane=colour_rect(226,28,224,170);
    if(full) {
        label("基本色",input::px(12),input::px(12));
        label("作成した色",input::px(12),input::px(178));
        for(int i=0;i<48;++i) {
            const auto rect=colour_rect(12+(i%8)*24,34+(i/8)*22,21,19);
            ui::fill_rect(surface,rect,unpack_colour(basic_colours[i]));
            ui::draw_sunken_border(surface,rect);
        }
        for(Integer y=0;y<plane.height;++y) for(Integer x=0;x<plane.width;++x) {
            const auto rgb=::cw::lib::hls_to_rgb({static_cast<double>(x)/std::max<Integer>(1,plane.width-1),
                0.5,1-static_cast<double>(y)/std::max<Integer>(1,plane.height-1)});
            const auto offset=static_cast<std::size_t>(((plane.y+y)*surface->size.width+plane.x+x)*4);
            for(int channel=0;channel<3;++channel)
                surface->rgba[offset+channel]=static_cast<std::uint8_t>(std::lround(rgb[channel]*255));
        }
        retained.capture_base();
    }

    for(int i=0;i<64;++i) {
        const bool custom=i>=48;const int slot=custom?i-48:i;
        const auto rect=colour_rect(12+(slot%8)*24,(custom?198:34)+(slot/8)*22,21,19);
        hit(100+i,rect);
    }
    hit(201,plane,true);
    const auto strip=colour_rect(462,28,16,170);
    hit(202,strip,true);

    const Rect basic_region=colour_rect(8,30,197,137);
    const Rect custom_region=colour_rect(8,194,197,48);
    const Rect add_region=colour_rect(8,246,197,34);
    const Rect preview_region=colour_rect(8,282,197,28);
    const Rect plane_region=colour_rect(222,24,232,178);
    const Rect strip_region=colour_rect(458,24,30,178);
    bool paint_basic=retained.component_changed("colour.basic-focus",basic_region,
        focus_>=100&&focus_<148?static_cast<std::uint64_t>(focus_+1):0);
    std::uint64_t custom_signature=static_cast<std::uint64_t>(custom_slot_+1);
    mix(custom_signature,focus_>=148&&focus_<164?static_cast<std::uint64_t>(focus_+1):0);
    for(const auto value:custom_colours)mix(custom_signature,value);
    bool paint_custom=retained.component_changed("colour.custom",custom_region,custom_signature);
    bool paint_add=retained.component_changed("colour.add",add_region,
        font_signature(button_font)^(focus_==200?1ULL:0ULL)^(finished_?2ULL:0ULL));
    std::uint64_t colour_signature=(static_cast<std::uint64_t>(colour_.r)<<24U)|
        (static_cast<std::uint64_t>(colour_.g)<<16U)|(static_cast<std::uint64_t>(colour_.b)<<8U)|colour_.a;
    bool paint_preview=retained.component_changed("colour.preview",preview_region,colour_signature);
    std::uint64_t plane_signature=static_cast<std::uint64_t>(std::lround(hue_*1000000));
    mix(plane_signature,static_cast<std::uint64_t>(std::lround(saturation_*1000000)));
    mix(plane_signature,focus_==201);
    bool paint_plane=retained.component_changed("colour.plane-cursor",plane_region,plane_signature);
    std::uint64_t strip_signature=static_cast<std::uint64_t>(std::lround(hue_*1000000));
    mix(strip_signature,static_cast<std::uint64_t>(std::lround(saturation_*1000000)));
    mix(strip_signature,static_cast<std::uint64_t>(std::lround(luminance_*1000000)));
    mix(strip_signature,focus_==202);
    bool paint_strip=retained.component_changed("colour.luminance",strip_region,strip_signature);

    struct FieldPaint {Rect region{},field{};int index{};bool paint{};};
    std::array<FieldPaint,6> fields;
    const char* labels[]={"赤","緑","青","色合い","鮮やかさ","明るさ"};
    for(int i=0;i<6;++i) {
        const auto field=colour_field_rect(i);
        const Rect region=colour_rect(i<3?358:222,210+(i%3)*29,i<3?80:112,29);
        std::uint64_t signature=static_cast<std::uint64_t>(component(i)+1);
        mix(signature,static_cast<std::uint64_t>(edit_==i));
        mix(signature,static_cast<std::uint64_t>(focus_==300+i));
        mix(signature,static_cast<std::uint64_t>(finished_));
        mix(signature,font_signature(font));
        if(edit_==i) {
            mix(signature,std::hash<std::string>{}(editor_->buffer.text));
            mix(signature,std::hash<std::string>{}(editor_->buffer.composition));
            mix(signature,editor_->buffer.caret);mix(signature,editor_->buffer.anchor);
            mix(signature,static_cast<std::uint64_t>(editor_->buffer.horizontal+1));
            mix(signature,static_cast<std::uint64_t>(editor_->buffer.composition_start+1));
            mix(signature,static_cast<std::uint64_t>(editor_->buffer.composition_length+1));
        }
        fields[i]={region,field,i,retained.component_changed("colour.field."+std::to_string(i),region,signature)};
    }
    const Rect footer_region=colour_rect(270,308,222,34);
    bool paint_footer=retained.component_changed("colour.footer",footer_region,
        font_signature(button_font)^(static_cast<std::uint64_t>(focus_+1)<<8U)^(finished_?1ULL:0ULL));
    const auto menu_font=ui::configured_wx_font("menu",13);
    const auto popup_bounds=editor_->menu.presentation_bounds(surface_size,
        std::max<double>(::cw::g_UP_WIN,1),menu_font);
    bool paint_popup=false;
    if(!finished_&&editor_->menu.opened())
        paint_popup=retained.component_changed("colour.popup",popup_bounds,
            editor_->menu.presentation_signature(editor_->buffer)^font_signature(menu_font));
    else (void)retained.remove_component("colour.popup");
    const auto intersects=[&](Rect area) {
        if(area.width<=0||area.height<=0)return false;
        return std::any_of(retained.dirty.begin(),retained.dirty.end(),[&](const Rect& dirty) {
            return dirty.x<area.x+area.width&&dirty.x+dirty.width>area.x&&
                dirty.y<area.y+area.height&&dirty.y+dirty.height>area.y;
        });
    };
    paint_basic=paint_basic||intersects(basic_region);paint_custom=paint_custom||intersects(custom_region);
    paint_add=paint_add||intersects(add_region);paint_preview=paint_preview||intersects(preview_region);
    paint_plane=paint_plane||intersects(plane_region);paint_strip=paint_strip||intersects(strip_region);
    for(auto& value:fields)value.paint=value.paint||intersects(value.region);
    paint_footer=paint_footer||intersects(footer_region);paint_popup=paint_popup||intersects(popup_bounds);
    retained.restore_dirty();surface=retained.surface;

    if(paint_basic&&focus_>=100&&focus_<148)
        focus_border(colour_rect(12+((focus_-100)%8)*24,34+((focus_-100)/8)*22,21,19));
    if(paint_custom) {
        for(int slot=0;slot<16;++slot) {
            const auto rect=colour_rect(12+(slot%8)*24,198+(slot/8)*22,21,19);
            ui::fill_rect(surface,rect,unpack_colour(custom_colours[slot]));ui::draw_sunken_border(surface,rect);
            if(slot==custom_slot_)ui::draw_sunken_border(surface,
                {rect.x-input::px(2),rect.y-input::px(2),rect.width+input::px(4),rect.height+input::px(4)});
            if(focus_==148+slot)focus_border(rect);
        }
    }
    button("作成した色に追加",colour_rect(12,250,189,26),DialogPresentationAction::Auxiliary,200,paint_add);
    if(paint_add&&focus_==200)focus_border(colour_rect(12,250,189,26));
    // RGB の選択では、呼び出し側が別に持つアルファ設定は変更しない。
    auto preview=colour_;preview.a=255;
    const auto preview_rect=colour_rect(12,286,189,20);
    if(paint_preview){ui::fill_rect(surface,preview_rect,preview);ui::draw_sunken_border(surface,preview_rect);}
    if(paint_strip)for(Integer y=0;y<strip.height;++y) {
        const auto rgb=::cw::lib::hls_to_rgb({hue_,1-static_cast<double>(y)/std::max<Integer>(1,strip.height-1),saturation_});
        ui::fill_rect(surface,{strip.x,strip.y+y,strip.width,1},
            {static_cast<std::uint8_t>(std::lround(rgb[0]*255)),static_cast<std::uint8_t>(std::lround(rgb[1]*255)),
             static_cast<std::uint8_t>(std::lround(rgb[2]*255)),255});
    }
    const Integer cx=plane.x+static_cast<Integer>(std::lround(hue_*(plane.width-1)));
    const Integer cy=plane.y+static_cast<Integer>(std::lround((1-saturation_)*(plane.height-1)));
    if(paint_plane) {
        ui::fill_rect(surface,{cx-input::px(4),cy,input::px(8)+1,1},{0,0,0,255});
        ui::fill_rect(surface,{cx,cy-input::px(4),1,input::px(8)+1},{0,0,0,255});
        if(focus_==201)focus_border(plane);
    }
    const Integer ly=strip.y+static_cast<Integer>(std::lround((1-luminance_)*(strip.height-1)));
    if(paint_strip)for(Integer x=0;x<input::px(6);++x)
        ui::fill_rect(surface,{strip.x+strip.width+input::px(2)+x,ly-x,1,x*2+1},{0,0,0,255});
    if(paint_strip&&focus_==202)focus_border(strip);
    for(const auto& value:fields) {
        const int i=value.index;const auto rect=value.field;
        textedit::Buffer inactive;
        if(edit_!=i)inactive.reset(std::to_string(component(i)));
        auto& buffer=edit_==i?editor_->buffer:inactive;
        if(value.paint) {
            label(labels[i],input::px(i<3?362:226),rect.y+(rect.height-input::cell_height(font))/2);
            input::field(surface,rect,buffer,font,edit_==i && !finished_);
            if(focus_==300+i)focus_border(rect);
        }
        hit(300+i,rect,true);
        if(edit_==i && !finished_) {
            result.wants_text_input=true;result.input_text=buffer.text;
            result.composition=buffer.composition;
            result.composition_start=buffer.composition_start;
            result.composition_length=buffer.composition_length;
        }
    }
    button("OK",colour_rect(274,312,100,26),DialogPresentationAction::Accept,-1,paint_footer);
    button("キャンセル",colour_rect(382,312,106,26),DialogPresentationAction::Cancel,-1,paint_footer);
    if(paint_footer&&focus_==400)focus_border(colour_rect(274,312,100,26));
    if(paint_footer&&focus_==401)focus_border(colour_rect(382,312,106,26));
    if(!finished_)editor_->menu.draw(result,surface,editor_->buffer,
        std::max<double>(::cw::g_UP_WIN,1),menu_font,paint_popup);
    retained.finish();
    result.damage=retained.damage;result.surface_revision=retained.revision;
    result.surface=surface;return result;
}
void ColourSelectionDialog::dialog_presentation_action(DialogPresentationAction action,int index) {
    if(finished_)return;
    request_dialog_presentation_refresh();
    auto& state=*editor_;
    if(state.menu.opened()) {
        const auto saved=state.buffer;
        state.menu.action(action,index,state.buffer,state.read,state.write,0,false);
        if(!colour_digits(state.buffer.text))state.buffer=saved;
        return;
    }
    if(action==DialogPresentationAction::Cancel) {
        finished_=true;accepted_=false;dialog_presentation_pointer_cancel();return;
    }
    if(action!=DialogPresentationAction::Accept &&
        !(action==DialogPresentationAction::Auxiliary &&
          ((index>=100 && index<164)||(index>=200 && index<=202)||(index>=300 && index<306))))return;
    if(!commit())return;
    if(action==DialogPresentationAction::Accept) {
        accepted_=true;finished_=true;dialog_presentation_pointer_cancel();return;
    }
    focus_control(index);
    if(index>=100 && index<164) {
        const auto alpha=colour_.a;
        if(index>=148) {custom_slot_=index-148;colour_=unpack_colour(custom_colours[custom_slot_]);}
        else {basic_slot_=index-100;colour_=unpack_colour(basic_colours[basic_slot_]);}
        colour_.a=alpha;from_rgb();
    } else if(index==200) {
        custom_colours[custom_slot_]=(std::uint32_t(colour_.r)<<16)|(std::uint32_t(colour_.g)<<8)|colour_.b;
        custom_slot_=(custom_slot_+1)%16;
    }
}
void ColourSelectionDialog::dialog_presentation_text(const std::string& text,bool composition) {
    if(finished_ || edit_<0 || editor_->menu.opened())return;
    if(composition) {editor_->buffer.set_composition(text);request_dialog_presentation_refresh();return;}
    const auto saved=editor_->buffer;
    editor_->buffer.insert(text,0,false);
    if(!colour_digits(editor_->buffer.text))editor_->buffer=saved;
    request_dialog_presentation_refresh();
}
void ColourSelectionDialog::dialog_presentation_text_event(const DialogTextEditingEvent& event) {
    if(event.composition && !finished_ && edit_>=0 && !editor_->menu.opened()) {
        editor_->buffer.set_composition(event.text,event.start,event.length);
        request_dialog_presentation_refresh();return;
    }
    dialog_presentation_text(event.text,false);
}
void ColourSelectionDialog::dialog_presentation_key(DialogPresentationKey key,bool pressed,DialogPresentationModifiers modifiers) {
    if(!pressed || finished_)return;
    using Key=DialogPresentationKey;
    using Action=DialogPresentationAction;
    auto& state=*editor_;
    request_dialog_presentation_refresh();
    if(state.menu.opened()) {
        const auto saved=state.buffer;
        state.menu.key(key,modifiers,state.buffer,state.read,state.write,0,false);
        if(!colour_digits(state.buffer.text))state.buffer=saved;
        return;
    }
    if(!state.buffer.composition.empty()) {
        if(key==Key::Escape){state.buffer.composition.clear();request_dialog_presentation_refresh();}
        return;
    }
    if(key==Key::Escape){dialog_presentation_action(Action::Cancel,-1);return;}
    if(key==Key::Enter){dialog_presentation_enter(modifiers);return;}
    if(key==Key::ContextMenu || (key==Key::F10 && (modifiers&DialogPresentationModifierShift))) {
        if(edit_>=0) {
            const auto point=colour_input::menu_anchor(state.buffer,colour_field_rect(edit_),
                ::cw::lib::ui::configured_wx_font("dlgmsg2",12));
            state.menu.open(state.buffer,!state.read().empty(),static_cast<float>(point.x),static_cast<float>(point.y),true);
        }
        return;
    }
    if(edit_>=0) {
        const auto saved=state.buffer;
        if(textedit::single_line_key(state.buffer,key,modifiers,0,state.read,state.write)) {
            if(!colour_digits(state.buffer.text))state.buffer=saved;
            return;
        }
    }
    if(modifiers&DialogPresentationModifierControl || modifiers&DialogPresentationModifierAlt)return;
    if(key==Key::Tab) {
        if(!commit())return;
        const std::array<int,13> order{100+basic_slot_,148+custom_slot_,200,201,202,303,304,305,300,301,302,400,401};
        auto it=std::find(order.begin(),order.end(),focus_);
        const int current=it==order.end()?0:static_cast<int>(it-order.begin());
        focus_control(order[(current+13+((modifiers&DialogPresentationModifierShift)?-1:1))%13]);
    } else if(focus_>=100 && focus_<164 &&
        (key==Key::Left||key==Key::Right||key==Key::Up||key==Key::Down||key==Key::Home||key==Key::End)) {
        const int first=focus_<148?100:148,last=focus_<148?147:163;
        const int next=key==Key::Home?first:key==Key::End?last:
            std::clamp(focus_+(key==Key::Left?-1:key==Key::Right?1:key==Key::Up?-8:8),first,last);
        dialog_presentation_action(Action::Auxiliary,next);
    } else if(focus_>=300 && focus_<306 && (key==Key::Up||key==Key::Down)) {
        const auto field=focus_-300;
        if(commit()){set_component(field,component(field)+(key==Key::Up?1:-1));focus_control(300+field);}
    } else if((focus_==201||focus_==202) &&
        (key==Key::Left||key==Key::Right||key==Key::Up||key==Key::Down||key==Key::Home||key==Key::End)) {
        const int field=focus_==202?5:(key==Key::Left||key==Key::Right?3:4);
        const int value=key==Key::Home?0:key==Key::End?(field==3?239:240):
            component(field)+(key==Key::Right||key==Key::Up?1:-1);
        set_component(field,value);
    } else if(key==Key::Space) {
        if(focus_==400||focus_==401)dialog_presentation_action(focus_==400?Action::Accept:Action::Cancel,-1);
        else if(focus_<300)dialog_presentation_action(Action::Auxiliary,focus_);
    }
}
void ColourSelectionDialog::dialog_presentation_enter(DialogPresentationModifiers modifiers) {
    if(finished_)return;
    if(editor_->menu.opened()) {
        dialog_presentation_key(DialogPresentationKey::Enter,true,modifiers);return;
    }
    if(focus_==200)dialog_presentation_action(DialogPresentationAction::Auxiliary,200);
    else dialog_presentation_action(focus_==401?DialogPresentationAction::Cancel:DialogPresentationAction::Accept,-1);
}
void ColourSelectionDialog::dialog_presentation_modifiers(DialogPresentationModifiers modifiers) {
    editor_->modifiers=modifiers;
}
void ColourSelectionDialog::dialog_presentation_pointer(DialogPresentationAction action,int index,float x,float y,bool pressed) {
    if(finished_ || !std::isfinite(x) || !std::isfinite(y))return;
    auto& state=*editor_;
    if(state.menu.opened()) {if(!pressed)dialog_presentation_action(action,index);return;}
    if(action!=DialogPresentationAction::Auxiliary || !((index>=300&&index<306)||index==201||index==202)) {
        if(!pressed)dialog_presentation_action(action,index);return;
    }
    if(index>=300 && index<306) {
        const int field=index-300;
        if(edit_!=field) {if(!commit())return;focus_control(index);}
        const auto font=::cw::lib::ui::configured_wx_font("dlgmsg2",12);
        const auto caret=colour_input::caret_at(state.buffer,colour_field_rect(field),font,x);
        state.buffer.move(caret,(state.drag==field) ||
            (state.modifiers&DialogPresentationModifierShift));
        state.drag=pressed?field:-1;
        request_dialog_presentation_refresh();return;
    }
    if(!commit())return;
    focus_control(index);
    const auto rect=index==201?colour_rect(226,28,224,170):colour_rect(462,28,16,170);
    const double vertical=std::clamp((static_cast<double>(y)-rect.y)/std::max<::cw::lib::Integer>(1,rect.height-1),0.0,1.0);
    if(index==201) {
        hue_=std::clamp((static_cast<double>(x)-rect.x)/std::max<::cw::lib::Integer>(1,rect.width-1),0.0,1.0);
        saturation_=1-vertical;
    } else luminance_=1-vertical;
    from_hsl();request_dialog_presentation_refresh();
}
void ColourSelectionDialog::dialog_presentation_pointer_cancel() {
    editor_->drag=-1;
}
void ColourSelectionDialog::dialog_presentation_hover(float x,float y) {
    if(!finished_ && editor_->menu.hover(x,y,editor_->buffer))request_dialog_presentation_refresh();
}
void ColourSelectionDialog::dialog_presentation_context(float x,float y) {
    if(finished_ || !std::isfinite(x) || !std::isfinite(y))return;
    auto& state=*editor_;request_dialog_presentation_refresh();
    if(state.menu.opened()){state.menu.close();return;}
    for(int field=0;field<6;++field)if(colour_contains(colour_field_rect(field),x,y)) {
        if(edit_!=field) {if(!commit())return;focus_control(300+field);}
        state.menu.open(state.buffer,!state.read().empty(),x,y);return;
    }
    // 編集欄の外で通常の右クリックをしても、色選択ダイアログはキャンセルしない。
}

std::string presentation_clipboard_text() {
    return ::cw::lib::system_clipboard_text();
}

void presentation_set_clipboard_text(const std::string& text) {
    ::cw::lib::set_system_clipboard_text(text);
}

namespace {
std::string tooltip_text_at(const DialogPresentation& presentation, float x, float y) {
    if (!std::isfinite(x) || !std::isfinite(y)) return {};
    for (auto it=presentation.controls.rbegin();it!=presentation.controls.rend();++it)
        if (x>=it->x && y>=it->y && x<it->x+it->width && y<it->y+it->height)
            return it->tooltip; // 無効なコントロールや他を覆うコントロールにも、それぞれの説明を保持する。
    return {};
}
}

bool DialogPresentationTooltip::clear() noexcept {
    const bool changed=!text_.empty(); text_.clear(); return changed;
}

bool DialogPresentationTooltip::update(const DialogPresentation& presentation, float x, float y) {
    auto text=tooltip_text_at(presentation,x,y);
    if (text==text_) return false;
    text_=std::move(text); x_=x; y_=y; return true;
}

std::shared_ptr<const ::cw::lib::Surface> DialogPresentationTooltip::compose(
    const DialogPresentation& presentation) const {
    const auto& base=presentation.surface;
    if (!base || text_.empty() || tooltip_text_at(presentation,x_,y_)!=text_ ||
        base->size.width<16 || base->size.height<16) return base;
    using namespace ::cw::lib;
    // ツールチップは論理キャンバスではなく、最終のダイアログ画像へ合成
    // する。 文字・折り返し・余白に同じウィンドウ拡大率を適用する。
    const auto tooltip_scale=presentation.debugger ? static_cast<Real>(::cw::g_dpi_level) : ::cw::g_UP_WIN;
    const auto px=[tooltip_scale](Integer value) {
        return std::max<Integer>(1,static_cast<Integer>(std::lround(value *
            (std::isfinite(tooltip_scale) && tooltip_scale>0 ? tooltip_scale : 1.0))));
    };
    auto font=ui::configured_font("button",12);
    font.pixels=px(font.pixels);
    const auto rows=ui::wrap_label(text_,font,std::max<Integer>(1,std::min<Integer>(px(360),base->size.width-px(12))));
    if (rows.empty()) return base;
    const Integer step=font.pixels+px(3);
    const Integer count=std::min<Integer>(static_cast<Integer>(rows.size()),
        std::max<Integer>(1,(base->size.height-px(8))/step));
    Integer width=px(12);
    for (Integer i=0;i<count;++i)
        width=std::max<Integer>(width,ui::label_width(rows[static_cast<std::size_t>(i)].text,
            font.pixels,font.bold,font.face)+px(12));
    width=std::min<Integer>(width,base->size.width);
    const Integer height=std::min<Integer>(base->size.height,count*step+px(8));
    const Integer x=std::clamp<Integer>(static_cast<Integer>(x_)+px(12),0,base->size.width-width);
    const Integer y=std::clamp<Integer>(static_cast<Integer>(y_)+px(20),0,base->size.height-height);
    auto overlay=std::make_shared<Surface>(*base);
    ui::inherit_surface_animation(overlay,base);
    ui::fill_rect(overlay,{x,y,width,height},{255,255,225,255});
    ui::draw_sunken_border(overlay,{x,y,width,height});
    for (Integer i=0;i<count;++i)
        ui::draw_label(overlay,rows[static_cast<std::size_t>(i)].text,x+px(6),y+px(4)+i*step,
            font.pixels,font.bold,{0,0,0},font.face);
    return overlay;
}

namespace {
thread_local std::weak_ptr<::cw::frame::Frame> g_modal_frame_override{};
// Presentation の入力コールバックから別の共通ダイアログを開く間、
// 実際の呼び出し元ウィンドウをネイティブの所有者として保持する。 これにより、
// デバッガーから開いた親子ダイアログをデバッガーと Engine の両方より
// 前に保ち、 二つ目の UI 実装やダイアログ専用のホスト処理を不要にする。
thread_local SDL_Window* g_presentation_action_owner{};
thread_local std::weak_ptr<::cw::lib::Window> g_modal_requested_owner{};

std::shared_ptr<::cw::frame::Frame> active_frame() noexcept
{
    // 入れ子のダイアログでも、明示されたモーダル所有者を優先する。 ALL で
    // は、Engine のグローバルフレームがこの操作のホストとは限らない。
    if (auto frame = g_modal_frame_override.lock()) return frame;
    if (::cw::g_cwpy) {
        const auto* value = std::get_if<::cw::lib::ObjectPtr>(
            &::cw::g_cwpy->m_frame.storage());
        if (value && *value)
            if (auto frame = std::dynamic_pointer_cast<::cw::frame::Frame>(*value))
                return frame;
    }
    return {};
}

// 外部ウィンドウも ALL と同じ表示プロバイダーを使う。 ここにはダイアログの配置
// ・コントロール・デバッガー状態を持たず、SDL はピクセルと入力だけを受け渡す。
struct PresentationWindow {
    std::weak_ptr<::cw::frame::Frame> frame_owner;
    std::shared_ptr<::cw::lib::Object> model;
    std::shared_ptr<DialogPresentationProvider> provider;
    std::weak_ptr<::cw::lib::Window> identity;
    DialogPresentation presentation;
    SDL_Window* window{};
    SDL_Window* owner_window{};
    SDL_Renderer* renderer{};
    SDL_Texture* texture{};
    DialogPointerCapture pointer;
    DialogPresentationCursorHost cursor;
    cw::lib::PointerCapture touch;
    int touch_clicks{1};
    DialogScrollGesture scroll_gesture;
    std::vector<std::string> dropped_files;
    bool debugger{};
    // モードレスのデバッガーまたはその子孫から開いたモーダルは、終了まで
    // 一時的に最前面の階層へ置く。 Windows ではネイティブの所有
    // 関係だけでは、別のウィンドウのアクティブ化による隠蔽を防げない。
    bool debugger_descendant{};
    // Engine の内容領域の外寸、つまり親の表示領域から Engine
    // のステータスバーを除いた高さを使う。 Windows では SDL
    // の画面寸法が物理ピクセルでも、ウィンドウ寸法は DPI 仮想化さ
    // れる場合があるため、 通常の単体モーダルの上限は親を基準とする。
    int fit_outer_height{};
    bool dirty{true};
    bool shown{};
    bool completion_dispatched{};
    bool iconized{};
    bool tooltip_composed{};
    std::uint64_t uploaded_surface_revision{};
    Uint64 rendered_at{};
    DialogPresentationTooltip tooltip;
    ~PresentationWindow() {
        cursor.clear();
        if (texture) SDL_DestroyTexture(texture);
        if (renderer) SDL_DestroyRenderer(renderer);
        if (window) SDL_DestroyWindow(window);
    }
};

void publish_window_presentation(PresentationWindow& state, DialogPresentation presentation)
{
    if (state.window && state.presentation.title != presentation.title)
        SDL_SetWindowTitle(state.window, presentation.title.c_str());
    state.presentation = std::move(presentation);
}

bool native_window_hidden(const PresentationWindow& state)
{
    return !state.window || (SDL_GetWindowFlags(state.window) & SDL_WINDOW_HIDDEN) != 0;
}

::cw::lib::Size fitted_window_size(PresentationWindow& state, const int source_width,
                                   const int source_height)
{
    ::cw::lib::Size result{source_width, source_height};
    if (!state.presentation.fit_work_area_height || state.presentation.resizable ||
        !state.window || source_width <= 0 || source_height <= 0) return result;
    SDL_Rect usable{};
    if (!SDL_GetDisplayUsableBounds(SDL_GetDisplayForWindow(state.window), &usable) ||
        usable.h <= 0 || usable.w <= 0)
        return result;
    int top{}, left{}, bottom{}, right{};
    if (!SDL_GetWindowBordersSize(state.window, &top, &left, &bottom, &right))
        top = left = bottom = right = 0;
    int available = std::max(1, usable.h - std::max(0, top) - std::max(0, bottom));
    const int available_width = std::max(
        1, usable.w - std::max(0, left) - std::max(0, right));
    if (state.fit_outer_height > 0)
        available = std::min(available, std::max(1, state.fit_outer_height -
            std::max(0, top) - std::max(0, bottom)));
    return fit_dialog_presentation_to_height(
        state.presentation,result,available,available_width);
}
thread_local std::vector<std::shared_ptr<PresentationWindow>> windows;
thread_local std::vector<std::shared_ptr<::cw::lib::Object>> modal_models;
thread_local std::vector<std::shared_ptr<::cw::lib::Object>> suspended_models;
thread_local bool background_enabled = true;
thread_local bool quit_hint_saved = false;
thread_local std::optional<std::string> previous_quit_hint;

void preserve_dialog_window_lifetime_hint()
{
    if (quit_hint_saved) return;
    if (const char* value = SDL_GetHint(SDL_HINT_QUIT_ON_LAST_WINDOW_CLOSE))
        previous_quit_hint = value;
    SDL_SetHint(SDL_HINT_QUIT_ON_LAST_WINDOW_CLOSE, "0");
    quit_hint_saved = true;
}

void retire_window(PresentationWindow& state)
{
    clear_dialog_debug_snapshot(state.model.get());
    if (state.shown || !native_window_hidden(state)) SDL_HideWindow(state.window);
    if (state.window && SDL_TextInputActive(state.window)) SDL_StopTextInput(state.window);
    if (state.provider) state.pointer.cancel(*state.provider);
    else state.pointer.reset();
    state.cursor.clear();
    state.touch.cancel();
    state.model.reset();
    state.provider.reset();
    state.presentation = {};
    state.dropped_files.clear();
    state.fit_outer_height = 0;
    state.owner_window = nullptr;
    state.debugger_descendant = false;
    state.dirty = true;
    state.shown = false;
    state.completion_dispatched = false;
    state.tooltip_composed = false;
    state.uploaded_surface_revision = 0;
    state.rendered_at = 0;
    state.tooltip = {};
    if (state.window) {
        SDL_SetWindowModal(state.window, false);
        SDL_SetWindowAlwaysOnTop(state.window, false);
    }
}

void prewarm_dialog_window()
{
    if (!windows.empty()) return;
    preserve_dialog_window_lifetime_hint();
    const auto started = std::chrono::steady_clock::now();
    auto state = std::make_shared<PresentationWindow>();
    state->window = SDL_CreateWindow("", 1, 1, SDL_WINDOW_HIDDEN);
    if (!state->window) throw std::runtime_error(SDL_GetError());
    if (::cardwirth::debug::nonactive_automation_enabled())
        SDL_SetWindowFocusable(state->window, false);
    state->renderer = SDL_CreateRenderer(state->window, nullptr);
    if (!state->renderer) throw std::runtime_error(SDL_GetError());
    windows.push_back(std::move(state));
#if defined(CARDWIRTH_LOG_ENABLED)
    ::cardwirth::debug::log_slow("dialog.window.prewarm",
        std::chrono::steady_clock::now() - started, "hidden standalone host");
#endif
}

void set_background_enabled(::cw::frame::Frame& frame, bool enabled) {
    // ゲームのモーダル終了には、ワーカーの反映とポインター解除の完了待
    // ちがある。 ウィンドウが消えただけで、この待機を早く解除しない。
    if (enabled && ::cw::g_cwpy && ::cw::g_cwpy->is_showingdlg()) return;
    if (background_enabled == enabled) return;
    background_enabled = enabled;
    frame.set_background_enabled(enabled);
}

std::shared_ptr<PresentationWindow> find_window(SDL_Window* window) {
    for (const auto& state : windows) if (state->window == window) return state;
    return {};
}

SDL_Window* main_sdl_window() {
    int count = 0;
    SDL_Window** list = SDL_GetWindows(&count);
    SDL_Window* result = nullptr;
    for (int i = 0; i < count; ++i)
        if (!find_window(list[i])) { result = list[i]; break; }
    SDL_free(list);
    return result;
}

::cw::lib::Rect owner_rect(const ::cw::frame::Frame& frame) {
#ifdef _WIN32
    RECT rect{};
    if (GetWindowRect(reinterpret_cast<HWND>(frame.m_native_window_handle), &rect))
        return {rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top};
#endif
    if (auto* parent = main_sdl_window()) {
        int x{}, y{}, w{}, h{};
        SDL_GetWindowPosition(parent, &x, &y);
        SDL_GetWindowSize(parent, &w, &h);
        return {x, y, w, h};
    }
    return {std::get<0>(frame.m_window_position), std::get<1>(frame.m_window_position),
            ::cw::lib::ui::win_i(632), ::cw::lib::ui::win_i(453)};
}

::cw::lib::Rect owner_content_rect(const ::cw::frame::Frame& frame) {
    const auto engine_content_height=static_cast<int>(::cw::lib::ui::win_i(
        std::get<1>(::cw::g_RECT_STATUSBAR)));
#ifdef _WIN32
    auto owner=reinterpret_cast<HWND>(frame.m_native_window_handle);
    RECT client{};
    POINT origin{};
    if (owner && GetClientRect(owner,&client) && ClientToScreen(owner,&origin)) {
        const int width=std::max(1,static_cast<int>(client.right-client.left));
        const int height=std::max(1,std::min(static_cast<int>(client.bottom-client.top),
                                             engine_content_height));
        return {origin.x,origin.y,width,height};
    }
#endif
    if (auto* parent=main_sdl_window()) {
        int x{},y{},w{},h{},top{},left{},bottom{},right{};
        SDL_GetWindowPosition(parent,&x,&y);
        SDL_GetWindowSize(parent,&w,&h);
        if (!SDL_GetWindowBordersSize(parent,&top,&left,&bottom,&right))
            top=left=bottom=right=0;
        return {x+std::max(0,left),y+std::max(0,top),std::max(1,w),
                std::max(1,std::min(h,engine_content_height))};
    }
    return {std::get<0>(frame.m_window_position),std::get<1>(frame.m_window_position),
            ::cw::lib::ui::win_i(632),std::max(1,engine_content_height)};
}

::cw::lib::Rect presentation_owner_rect(SDL_Window* owner) {
    if (!owner) return {};
    int x{},y{},w{},h{};
    SDL_GetWindowPosition(owner,&x,&y);
    SDL_GetWindowSize(owner,&w,&h);
    return {x,y,std::max(1,w),std::max(1,h)};
}

void draw_window(PresentationWindow& state) {
    const auto surface = state.presentation.surface;
    publish_dialog_debug_snapshot(state.model.get(), state.presentation);
    // 単体のモーダルダイアログは独自の入れ子ループを実行し、その間
    // Frame のループは意図的に停止する。 ダイアログ終了ま
    // で遠隔デバッグに古いゲームのスナップショットを残さず、 この
    // UI スレッドから同じ共通コントロールを直ちに公開する。
    ::cardwirth::debug::refresh_debug_control_clickable_areas();
    if (!surface || surface->size.width <= 0 || surface->size.height <= 0) return;
    const int width = static_cast<int>(surface->size.width);
    const int height = static_cast<int>(surface->size.height);
    if (surface->rgba.size() < static_cast<std::size_t>(width) * height * 4U) return;
    float tw{}, th{};
    bool texture_recreated=false;
    if (!state.texture || !SDL_GetTextureSize(state.texture, &tw, &th) ||
        tw != width || th != height) {
        if (state.texture) SDL_DestroyTexture(state.texture);
        state.texture = SDL_CreateTexture(state.renderer, SDL_PIXELFORMAT_RGBA32,
                                          SDL_TEXTUREACCESS_STREAMING, width, height);
        if (!state.texture) throw std::runtime_error(SDL_GetError());
        SDL_SetTextureBlendMode(state.texture, SDL_BLENDMODE_NONE);
        SDL_SetTextureScaleMode(state.texture, SDL_SCALEMODE_NEAREST);
        texture_recreated=true;
    }
    const auto fitted = fitted_window_size(state, width, height);
    const bool fractionally_fitted = fitted.width != width || fitted.height != height;
    SDL_SetTextureScaleMode(state.texture,
        state.presentation.smooth_fitted_scaling && fractionally_fitted
            ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);
    int current_width{}, current_height{};
    SDL_GetWindowSize(state.window, &current_width, &current_height);
    if (current_width != fitted.width || current_height != fitted.height)
        SDL_SetWindowSize(state.window, fitted.width, fitted.height);
    // 無効なツールも含め、ツールの説明は共通プロバイダーから取得する。 従来
    // のデバッガー専用ネイティブホストを置き換えても、その説明を維持する。
    const auto overlay=state.tooltip.compose(state.presentation);
    const bool tooltip_composed=overlay.get()!=surface.get();
    const bool partial=dialog_presentation_can_upload_damage(texture_recreated,tooltip_composed,
        state.tooltip_composed,state.presentation.surface_revision,state.uploaded_surface_revision,
        state.presentation.damage.size());
    bool updated=false;
    if(partial) {
        for(const auto& damage:state.presentation.damage) {
            const int left=std::clamp<int>(damage.x,0,width);
            const int top=std::clamp<int>(damage.y,0,height);
            const int right=std::clamp<int>(damage.x+damage.width,0,width);
            const int bottom=std::clamp<int>(damage.y+damage.height,0,height);
            if(left>=right || top>=bottom) continue;
            const SDL_Rect rect{left,top,right-left,bottom-top};
            const auto offset=(static_cast<std::size_t>(top)*width+left)*4U;
            if(!SDL_UpdateTexture(state.texture,&rect,overlay->rgba.data()+offset,width*4))
                throw std::runtime_error(SDL_GetError());
            updated=true;
        }
    }
    if(!partial || !updated)
        if(!SDL_UpdateTexture(state.texture,nullptr,overlay->rgba.data(),width*4))
            throw std::runtime_error(SDL_GetError());
    state.tooltip_composed=tooltip_composed;
    state.uploaded_surface_revision=state.presentation.surface_revision;
    SDL_RenderClear(state.renderer);
    SDL_RenderTexture(state.renderer, state.texture, nullptr, nullptr);
    if(SDL_GetMouseFocus()==state.window) {
        float x{},y{};int w{},h{};
        SDL_GetMouseState(&x,&y);SDL_GetWindowSize(state.window,&w,&h);
        state.cursor.update(state.presentation.controls,x*width/std::max(1,w),y*height/std::max(1,h));
    }
    if(const auto& hovered=state.cursor.hovered_button()) {
        int output_width{},output_height{};
        SDL_GetCurrentRenderOutputSize(state.renderer,&output_width,&output_height);
        render_dialog_hover_highlight(state.renderer,*hovered,0.0F,0.0F,
            static_cast<float>(output_width)/static_cast<float>(std::max(1,width)),
            static_cast<float>(output_height)/static_cast<float>(std::max(1,height)));
    }
    SDL_RenderPresent(state.renderer);
}

std::shared_ptr<PresentationWindow> ensure_window(
    ::cw::frame::Frame& frame, const std::shared_ptr<::cw::lib::Object>& model,
    bool debugger) {
    if (!model) return {};
    auto provider = std::dynamic_pointer_cast<DialogPresentationProvider>(model);
    if (!provider) return {};
    for (const auto& state : windows) {
        if (state->model != model) continue;
#if !defined(_WIN32)
        // SDL ホストは完了したウィンドウを直ちに隠せる。 古い状態を、
        // 表示中のネイティブダイアログとして Restore へ渡さない。
        if (provider->dialog_presentation_finished()) return {};
#endif
        return state;
    }
    if (provider->dialog_presentation_finished()) return {};
    preserve_dialog_window_lifetime_hint();
    const auto total_started = std::chrono::steady_clock::now();
    auto state = [&]() -> std::shared_ptr<PresentationWindow> {
        for (const auto& candidate : windows)
            if (!candidate->model && candidate->debugger == debugger) return candidate;
        return std::make_shared<PresentationWindow>();
    }();
    const bool reused = state->window != nullptr;
    // 完了したモーダルが、反映までネイティブのウィンドウを表示し続ける場合がある。
    // 新しいダイアログへ再利用する際、表示中の古い画面の寸法やラベルを変更しな
    // い。ensure_window で置き換え先を描いてから表示を切り替える。
    if (reused) {
        SDL_HideWindow(state->window);
        state->shown = false;
    }
    state->frame_owner = frame.weak_from_this();
    state->model = model;
    state->provider = provider;
    state->debugger = debugger;
    SDL_Window* action_owner = g_presentation_action_owner;
    if (const auto requested = g_modal_requested_owner.lock()) {
        for (const auto& candidate : windows)
            if (candidate != state && candidate->model &&
                candidate->identity.lock() == requested && candidate->window) {
                action_owner = candidate->window;
                break;
            }
    }
    const auto owner_state=find_window(action_owner);
    // 共通のタッチツール装飾を処理する前に、ホスト側の外枠と同じ祖
    // 先属性を伝える。 Py はデバッガーの子孫を対象外にする。
    provider->debugger_descendant = provider->debugger_descendant ||
        (!debugger && owner_state && (owner_state->debugger || owner_state->debugger_descendant));
    state->debugger_descendant = !debugger && provider->debugger_descendant;
    state->completion_dispatched = false;
    const auto presentation_started = std::chrono::steady_clock::now();
    provider->begin_dialog_presentation_refresh();
    state->presentation = provider->dialog_presentation();
    state->presentation.debugger = state->presentation.debugger || debugger;
    decorate_dialog_touch_tools(state->presentation,*provider);
#if defined(CARDWIRTH_LOG_ENABLED)
    ::cardwirth::debug::log_slow("dialog.window.presentation",
        std::chrono::steady_clock::now() - presentation_started,
        "title=" + state->presentation.title);
#endif
    state->owner_window = action_owner ? action_owner : main_sdl_window();
    const auto inherited_owner_rect=presentation_owner_rect(state->owner_window);
    const auto rect=debugger ? owner_rect(frame) :
        (action_owner && inherited_owner_rect.width>0
            ? inherited_owner_rect : owner_content_rect(frame));
    if (!debugger && state->presentation.fit_work_area_height)
        state->fit_outer_height=static_cast<int>(rect.height);
    const auto surface = state->presentation.surface;
    if (!surface) return {};
    const int w = static_cast<int>(surface->size.width);
    const int h = static_cast<int>(surface->size.height);
    const auto native_started = std::chrono::steady_clock::now();
    if (!state->window) {
        state->window = SDL_CreateWindow(state->presentation.title.c_str(), w, h,
            SDL_WINDOW_HIDDEN | (state->presentation.resizable ? SDL_WINDOW_RESIZABLE : 0));
        if (!state->window) throw std::runtime_error(SDL_GetError());
    } else {
        SDL_SetWindowTitle(state->window, state->presentation.title.c_str());
        SDL_SetWindowResizable(state->window, state->presentation.resizable);
    }
    if (state->presentation.resizable)
        SDL_SetWindowMinimumSize(state->window,
            static_cast<int>(std::max<::cw::lib::Integer>(1,state->presentation.minimum_size.width)),
            static_cast<int>(std::max<::cw::lib::Integer>(1,state->presentation.minimum_size.height)));
    else SDL_SetWindowMinimumSize(state->window, 1, 1);
    SDL_SetWindowFocusable(state->window,
        !::cardwirth::debug::nonactive_automation_enabled());
    SDL_SetWindowAlwaysOnTop(state->window,state->debugger_descendant);
    if (!state->renderer) {
        state->renderer = SDL_CreateRenderer(state->window, nullptr);
        if (!state->renderer) throw std::runtime_error(SDL_GetError());
    }
#if defined(CARDWIRTH_LOG_ENABLED)
    ::cardwirth::debug::log_slow("dialog.window.native",
        std::chrono::steady_clock::now() - native_started,
        std::string(reused ? "reused" : "created") + " title=" + state->presentation.title);
#endif
    const auto fitted=fitted_window_size(*state,w,h);
    SDL_SetWindowSize(state->window,fitted.width,fitted.height);
    int top{},left{},bottom{},right{};
    if (!SDL_GetWindowBordersSize(state->window,&top,&left,&bottom,&right))
        top=left=bottom=right=0;
    const int outer_width=fitted.width+std::max(0,left)+std::max(0,right);
    const int outer_height=fitted.height+std::max(0,top)+std::max(0,bottom);
    int x = static_cast<int>(debugger ? rect.x + rect.width :
        rect.x + (rect.width - outer_width) / 2);
    int y = static_cast<int>(debugger ? rect.y :
        rect.y + (rect.height - outer_height) / 2);
    if (const auto offset = dialog_center_offset(model)) {
        const auto translated = translate_dialog_center(
            {static_cast<double>(x), static_cast<double>(y)}, *offset);
        x = static_cast<int>(translated.x);
        y = static_cast<int>(translated.y);
    }
    SDL_Rect usable{};
    if (SDL_GetDisplayUsableBounds(SDL_GetDisplayForWindow(state->window), &usable)) {
        if (debugger && x + outer_width > usable.x + usable.w &&
            rect.x - outer_width >= usable.x)
            x = static_cast<int>(rect.x - outer_width);
        x = std::clamp(x, usable.x, usable.x + std::max(0, usable.w - outer_width));
        y = std::clamp(y, usable.y, usable.y + std::max(0, usable.h - outer_height));
    }
    SDL_SetWindowPosition(state->window, x, y);
    // 実際のウィンドウ所有者を、共通の識別関係へ伝える。 汎用 Object
    // モデルの方針・モデルのフィールドは別に保ち、この保持値は
    // SDL/Win32 のネイティブハンドル境界だけに使う。
    auto identity=std::dynamic_pointer_cast<::cw::lib::Window>(model);
    if (!identity && frame.m_active_dialog_model == model) identity = frame.m_active_dialog;
    if(auto debug_model=std::dynamic_pointer_cast<::cw::debug::debugger::Debugger>(model)) {
        identity=debug_model->m_window_identity;
        identity->bounds={x,y,outer_width,outer_height};
    }
    state->identity=identity;
    if(identity) identity->native_handle=reinterpret_cast<std::uintptr_t>(state->window);
    if (auto* parent = state->owner_window; parent && parent != state->window) {
        SDL_SetWindowParent(state->window, parent);
        if (!debugger) SDL_SetWindowModal(state->window, true);
    }
#ifdef _WIN32
    // 従来の Engine の主描画面は SDL_Window ではなく HWND で
    // ある。 ここでは所有者だけを受け渡し、子の内容とイベント経路は共通のままにする。
    auto handle = static_cast<HWND>(SDL_GetPointerProperty(
        SDL_GetWindowProperties(state->window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
    if(identity) identity->native_handle=reinterpret_cast<std::uintptr_t>(handle);
    HWND native_owner=nullptr;
    if (state->owner_window)
        native_owner=static_cast<HWND>(SDL_GetPointerProperty(
            SDL_GetWindowProperties(state->owner_window),SDL_PROP_WINDOW_WIN32_HWND_POINTER,nullptr));
    if (!native_owner && frame.m_native_window_handle)
        native_owner=reinterpret_cast<HWND>(frame.m_native_window_handle);
    if (handle && native_owner)
        SetWindowLongPtrW(handle,GWLP_HWNDPARENT,reinterpret_cast<LONG_PTR>(native_owner));
    if (handle)
        SetWindowPos(handle,state->debugger_descendant?HWND_TOPMOST:HWND_NOTOPMOST,
            0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_NOOWNERZORDER);
#endif
#if defined(CARDWIRTH_LOG_ENABLED)
    ::cardwirth::debug::log("Dialog stacking: title="+state->presentation.title+
        " debugger="+(state->debugger?"1":"0")+
        " debugger_descendant="+(state->debugger_descendant?"1":"0")+
        " owner="+(state->owner_window?"presentation":"engine"));
#endif
    const auto draw_started = std::chrono::steady_clock::now();
    draw_window(*state); // 表示する前に、最初のフレームの描画と位置決めを完了する。
#if defined(CARDWIRTH_LOG_ENABLED)
    ::cardwirth::debug::log_slow("dialog.window.first_draw",
        std::chrono::steady_clock::now() - draw_started,
        std::string(reused ? "reused" : "created") + " title=" + state->presentation.title);
#endif
    provider->dialog_presentation_refreshed();
    state->dirty = false;
    state->rendered_at = SDL_GetTicks();
    if (!reused) windows.push_back(state);
#if defined(CARDWIRTH_LOG_ENABLED)
    ::cardwirth::debug::log_slow("dialog.window.ensure.total",
        std::chrono::steady_clock::now() - total_started,
        std::string(reused ? "reused" : "created") + " title=" + state->presentation.title);
#endif
    return state;
}

std::optional<DialogPresentationControl> control_at(
    const std::vector<DialogPresentationControl>& controls, float x, float y) {
    for (auto it = controls.rbegin(); it != controls.rend(); ++it)
        if (it->enabled && x >= it->x && y >= it->y &&
            x < it->x + it->width && y < it->y + it->height) return *it;
    return {};
}

std::optional<DialogPresentationControl> control_at(
    const DialogPresentation& presentation, float x, float y) {
    return control_at(presentation.controls,x,y);
}

} // namespace

void DialogPointerCapture::down(DialogPresentationProvider& provider,
    const DialogPresentation& presentation, float x, float y) {
    if (pressed_) cancel(provider);
    down(presentation.controls,x,y,[&provider](const auto& control,float px,float py,bool pressed) {
        if (!dispatch_dialog_utility_pointer(provider,control.action,px,py,pressed))
            provider.dialog_presentation_pointer(control.action,control.index,px,py,pressed);
    });
}
bool DialogPointerCapture::move(DialogPresentationProvider& provider,float x,float y,bool pressed) {
    return move(x,y,pressed,[&provider](const auto& control,float px,float py,bool held) {
        if (!dispatch_dialog_utility_pointer(provider,control.action,px,py,held))
            provider.dialog_presentation_pointer(control.action,control.index,px,py,held);
    });
}
void DialogPointerCapture::up(DialogPresentationProvider& provider,
    const DialogPresentation& presentation,float x,float y,int clicks) {
    up(presentation.controls,x,y,clicks,
        [&provider](const auto& control,float px,float py,bool pressed) {
            if (!dispatch_dialog_utility_pointer(provider,control.action,px,py,pressed))
            provider.dialog_presentation_pointer(control.action,control.index,px,py,pressed);
        },
        [&provider](DialogPresentationAction action,int index) {
            if (!provider.dialog_presentation_finished() &&
                !dispatch_dialog_utility_action(provider,action))
                provider.dialog_presentation_action(action,index);
        });
}
void DialogPointerCapture::down(const std::vector<DialogPresentationControl>& controls,
    float x,float y,const PointerRelay& pointer) {
    pressed_.reset();
    pressed_=control_at(controls,x,y);
    if(pressed_ && pressed_->continuous_pointer && pointer) pointer(*pressed_,x,y,true);
}
bool DialogPointerCapture::move(float x,float y,bool pressed,const PointerRelay& pointer) {
    if(!pressed_ || !pressed_->continuous_pointer || !pressed) return false;
    if(pointer) pointer(*pressed_,x,y,true);
    return true;
}
void DialogPointerCapture::up(const std::vector<DialogPresentationControl>& controls,
    float x,float y,int clicks,const PointerRelay& pointer,const ActionRelay& action_relay) {
    const auto held=std::exchange(pressed_,std::nullopt);
    if (!held) return;
    if(held->continuous_pointer && pointer) pointer(*held,x,y,false);
    const auto released=control_at(controls,x,y);
    if(const auto action=dialog_release_action(*held,released?&*released:nullptr,x,y,clicks))
        if(action_relay) action_relay(*action,held->index);
}
void DialogPointerCapture::cancel(DialogPresentationProvider& provider) {
    pressed_.reset();
    cancel_dialog_utility_pointer(provider);
    provider.dialog_presentation_pointer_cancel();
}

char presentation_key_ascii(DialogPresentationKey key) noexcept {
    constexpr DialogPresentationKey letters[] = {
        DialogPresentationKey::A, DialogPresentationKey::B, DialogPresentationKey::C,
        DialogPresentationKey::D, DialogPresentationKey::E, DialogPresentationKey::F,
        DialogPresentationKey::G, DialogPresentationKey::H, DialogPresentationKey::I,
        DialogPresentationKey::J, DialogPresentationKey::K, DialogPresentationKey::L,
        DialogPresentationKey::M, DialogPresentationKey::N, DialogPresentationKey::O,
        DialogPresentationKey::P, DialogPresentationKey::Q, DialogPresentationKey::R,
        DialogPresentationKey::S, DialogPresentationKey::T, DialogPresentationKey::U,
        DialogPresentationKey::V, DialogPresentationKey::W, DialogPresentationKey::X,
        DialogPresentationKey::Y, DialogPresentationKey::Z};
    for (int i=0;i<26;++i) if (key==letters[i]) return static_cast<char>('a'+i);
    if (key==DialogPresentationKey::Digit0) return '0';
    if (key>=DialogPresentationKey::Digit1 && key<=DialogPresentationKey::Digit9)
        return static_cast<char>('1'+static_cast<int>(key)-static_cast<int>(DialogPresentationKey::Digit1));
    return 0;
}

std::optional<DialogPresentationKey> presentation_key_from_sdl(std::uint32_t key) noexcept {
    switch (key) {
    case SDLK_ESCAPE: case SDLK_AC_BACK: return DialogPresentationKey::Escape;
    case SDLK_INSERT: return DialogPresentationKey::Insert;
    case SDLK_APPLICATION: return DialogPresentationKey::ContextMenu;
    case SDLK_UNDERSCORE: return DialogPresentationKey::Underscore;
    case SDLK_MINUS: return DialogPresentationKey::Minus;
    case SDLK_LEFT: return DialogPresentationKey::Left;
    case SDLK_RIGHT: return DialogPresentationKey::Right;
    case SDLK_UP: return DialogPresentationKey::Up;
    case SDLK_DOWN: return DialogPresentationKey::Down;
    case SDLK_RETURN: case SDLK_KP_ENTER: return DialogPresentationKey::Enter;
    case SDLK_SPACE: return DialogPresentationKey::Space;
    case SDLK_B: return DialogPresentationKey::B;
    case SDLK_D: return DialogPresentationKey::D;
    case SDLK_E: return DialogPresentationKey::E;
    case SDLK_F: return DialogPresentationKey::F;
    case SDLK_G: return DialogPresentationKey::G;
    case SDLK_H: return DialogPresentationKey::H;
    case SDLK_J: return DialogPresentationKey::J;
    case SDLK_K: return DialogPresentationKey::K;
    case SDLK_M: return DialogPresentationKey::M;
    case SDLK_N: return DialogPresentationKey::N;
    case SDLK_P: return DialogPresentationKey::P;
    case SDLK_Q: return DialogPresentationKey::Q;
    case SDLK_U: return DialogPresentationKey::U;
    case SDLK_W: return DialogPresentationKey::W;
    case SDLK_Y: return DialogPresentationKey::Y;
    case SDLK_Z: return DialogPresentationKey::Z;
    case SDLK_0: return DialogPresentationKey::Digit0;
    case SDLK_BACKSPACE: return DialogPresentationKey::Backspace;
    case SDLK_PAGEUP: return DialogPresentationKey::PageUp;
    case SDLK_PAGEDOWN: return DialogPresentationKey::PageDown;
    case SDLK_I: return DialogPresentationKey::I;
    case SDLK_L: return DialogPresentationKey::L;
    case SDLK_O: return DialogPresentationKey::O;
    case SDLK_R: return DialogPresentationKey::R;
    case SDLK_S: return DialogPresentationKey::S;
    case SDLK_T: return DialogPresentationKey::T;
    case SDLK_X: return DialogPresentationKey::X;
    case SDLK_A: return DialogPresentationKey::A;
    case SDLK_C: return DialogPresentationKey::C;
    case SDLK_V: return DialogPresentationKey::V;
    case SDLK_HOME: return DialogPresentationKey::Home;
    case SDLK_END: return DialogPresentationKey::End;
    case SDLK_DELETE: return DialogPresentationKey::Delete;
    case SDLK_TAB: return DialogPresentationKey::Tab;
    default:
        if (key >= SDLK_1 && key <= SDLK_9)
            return static_cast<DialogPresentationKey>(
                static_cast<int>(DialogPresentationKey::Digit1) + key - SDLK_1);
        if (SDLK_F1 <= key && key <= SDLK_F12)
            return static_cast<DialogPresentationKey>(
                static_cast<int>(DialogPresentationKey::F1) + key - SDLK_F1);
        return {};
    }
}

namespace {
void cancel_window_pointer(PresentationWindow& state) {
    state.cursor.clear();
    state.touch.cancel();
    state.touch_clicks=1;
    state.scroll_gesture.reset();
    state.pointer.cancel(*state.provider);
    state.tooltip.clear();
    state.dirty=true;
}

void dispatch_window_event(const std::shared_ptr<PresentationWindow>& state, const SDL_Event& event) {
    auto& provider = *state->provider;
    // SDL は FINGER_CANCELED より前にも合成マウス解除を送る。 そのイベン
    // トは SDL のクリック回数だけに使い、解除とキャンセルの判定は元のタッチ入力で行う。
    if ((event.type==SDL_EVENT_MOUSE_BUTTON_DOWN || event.type==SDL_EVENT_MOUSE_BUTTON_UP) &&
        event.button.which==SDL_TOUCH_MOUSEID) {
        if (event.type==SDL_EVENT_MOUSE_BUTTON_DOWN && !state->touch.active())
            state->touch_clicks=std::max(1,static_cast<int>(event.button.clicks));
        return;
    }
    if ((event.type==SDL_EVENT_MOUSE_MOTION && event.motion.which==SDL_TOUCH_MOUSEID) ||
        (event.type==SDL_EVENT_MOUSE_WHEEL && event.wheel.which==SDL_TOUCH_MOUSEID)) return;
    if (state->touch.active() &&
        (event.type==SDL_EVENT_MOUSE_BUTTON_DOWN || event.type==SDL_EVENT_MOUSE_BUTTON_UP ||
         event.type==SDL_EVENT_MOUSE_MOTION || event.type==SDL_EVENT_MOUSE_WHEEL)) return;
    if ((event.type==SDL_EVENT_FINGER_DOWN || event.type==SDL_EVENT_FINGER_MOTION ||
         event.type==SDL_EVENT_FINGER_UP || event.type==SDL_EVENT_FINGER_CANCELED) &&
        (event.tfinger.touchID==SDL_MOUSE_TOUCHID || event.tfinger.touchID==SDL_PEN_TOUCHID)) return;
    // 生存期間の通知はダイアログの入力受付権とは独立して扱う。
    // ワーカーの操作が保留中でも、最小化・復元を無視しない。
    if (event.type == SDL_EVENT_WINDOW_MINIMIZED || event.type == SDL_EVENT_WINDOW_RESTORED) {
        const bool iconized = event.type == SDL_EVENT_WINDOW_MINIMIZED;
        if (state->iconized != iconized) {
            state->iconized = iconized;
            auto notification = std::make_shared<::cw::lib::Window>();
            notification->id = iconized ? 1 : 0;
            ::cw::frame::Frame_move_dlg_OnIconize(std::move(notification));
        }
        return;
    }
    if (event.type == SDL_EVENT_WINDOW_EXPOSED) {
        state->dirty = true;
        return;
    }
    if (event.type == SDL_EVENT_RENDER_DEVICE_RESET) {
        // 保持中の CPU 描画面は有効だが、レンダラー所有のリソースは無効
        // である。 テクスチャーを再作成し、次のフレームで全体を転送する。
        SDL_DestroyTexture(state->texture);
        state->texture = nullptr;
        state->uploaded_surface_revision = 0;
        state->tooltip_composed = false;
        state->dirty = true;
        return;
    }
    DialogPresentationInputGuard input_guard(provider);
    if (!input_guard) {
        // 予約済み・ワーカー実行中の操作がモデルを占有する間、
        // 物理入力は遅延せず破棄する。 処理中のクリックが操
        // 作可能になった後で実行されないようにする。ホストの
        // 露出・再描画は上で処理するため、引き続き機能する。
        state->pointer.cancel(provider);
        state->touch.cancel();
        state->cursor.clear();
        state->tooltip.clear();
        return;
    }
    struct OwnerScope {
        SDL_Window* previous{};
        explicit OwnerScope(SDL_Window* owner)
            : previous(std::exchange(g_presentation_action_owner,owner)) {}
        ~OwnerScope() {g_presentation_action_owner=previous;}
    } owner_scope(state->window);
    // 初期フォーカスでは文字入力を始めない（presentation.hpp参照）。
    // 押す操作（クリック・タッチ・キー）を受けてから入力欄を有効にする。
    if (event.type==SDL_EVENT_MOUSE_BUTTON_DOWN || event.type==SDL_EVENT_FINGER_DOWN ||
        event.type==SDL_EVENT_KEY_DOWN)
        provider.note_dialog_presentation_user_input();
    const auto mods = SDL_GetModState();
    provider.dialog_presentation_modifiers(
        ::cw::lib::engine_input_modifiers_from_sdl(mods));
    const auto point = [&](float x, float y) {
        int w{}, h{};
        SDL_GetWindowSize(state->window, &w, &h);
        const auto surface = state->presentation.surface;
        return std::pair<float, float>{x * surface->size.width / std::max(1, w),
                                      y * surface->size.height / std::max(1, h)};
    };
    const bool was_dirty = state->dirty;
    state->dirty = true;
    if (event.type != SDL_EVENT_MOUSE_MOTION && event.type != SDL_EVENT_WINDOW_EXPOSED)
        state->tooltip.clear();
    switch (event.type) {
    case SDL_EVENT_DROP_BEGIN:
        state->dropped_files.clear();
        break;
    case SDL_EVENT_DROP_FILE:
        if (event.drop.data && *event.drop.data)
            state->dropped_files.emplace_back(event.drop.data);
        break;
    case SDL_EVENT_DROP_COMPLETE: {
        auto paths = std::move(state->dropped_files);
        state->dropped_files.clear();
        if (!paths.empty()) provider.dialog_presentation_drop_files(paths);
        break;
    }
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        cancel_window_pointer(*state);
        provider.dialog_presentation_action(DialogPresentationAction::Cancel, -1); break;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
    case SDL_EVENT_WILL_ENTER_BACKGROUND:
        cancel_window_pointer(*state);
        provider.dialog_presentation_focus(false);
        break;
    case SDL_EVENT_WINDOW_RESIZED:
        if (state->touch.active() || state->pointer.active()) cancel_window_pointer(*state);
        // ウィンドウ座標は既存の描画面・入力の規則に合わせる。 ピクセ
        // ル密度の通知を、二回目のモデルのサイズ変更として扱わない。
        if (resize_dialog_presentation(provider,state->presentation,
                                      {event.window.data1,event.window.data2}))
            publish_window_presentation(*state, provider.dialog_presentation());
        break;
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        if (state->touch.active() || state->pointer.active()) cancel_window_pointer(*state);
        ::cw::lib::ui::set_system_dialog_scale(SDL_GetWindowDisplayScale(state->window));
        break;
    case SDL_EVENT_WINDOW_MOUSE_LEAVE:
        if (state->cursor.hovered_button()) state->dirty = true;
        state->cursor.clear();
        update_dialog_utility_hover(provider,state->presentation,-1,-1);
        provider.dialog_presentation_hover(-1, -1);
        state->tooltip.clear();
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (event.button.button == SDL_BUTTON_MIDDLE) {
            const auto [x, y] = point(event.button.x, event.button.y);
            provider.dialog_presentation_middle(x, y, true);
        }
        if (event.button.button == SDL_BUTTON_LEFT) {
            const auto [x, y] = point(event.button.x, event.button.y);
            state->pointer.down(provider,state->presentation,x,y);
        }
        break;
    case SDL_EVENT_MOUSE_MOTION:
        {
            const auto [x, y] = point(event.motion.x, event.motion.y);
            update_dialog_utility_hover(provider,state->presentation,x,y);
            const bool hover_changed=state->cursor.update(state->presentation.controls,x,y);
            if (!state->pointer.move(provider,x,y,(event.motion.state & SDL_BUTTON_LMASK)!=0)) {
                provider.dialog_presentation_hover(x, y);
                const bool tooltip_changed=state->tooltip.update(state->presentation,x,y);
                state->dirty = was_dirty || tooltip_changed || hover_changed ||
                    provider.dialog_presentation_needs_refresh();
            }
        }
        break;
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (event.button.button == SDL_BUTTON_MIDDLE) {
            const auto [x, y] = point(event.button.x, event.button.y);
            provider.dialog_presentation_middle(x, y, false); break;
        }
        if (event.button.button == SDL_BUTTON_RIGHT) {
            const auto [x, y] = point(event.button.x, event.button.y);
            provider.dialog_presentation_context(x, y); break;
        }
        if (event.button.button == SDL_BUTTON_LEFT) {
            const auto [x, y] = point(event.button.x, event.button.y);
            state->pointer.up(provider,state->presentation,x,y,event.button.clicks);
        }
        break;
    case SDL_EVENT_FINGER_DOWN:
    case SDL_EVENT_FINGER_MOTION:
    case SDL_EVENT_FINGER_UP:
    case SDL_EVENT_FINGER_CANCELED: {
        if (event.type==SDL_EVENT_FINGER_DOWN) {
            if (!state->touch.begin(event.tfinger.touchID,event.tfinger.fingerID)) break;
        } else if (!state->touch.owns(event.tfinger.touchID,event.tfinger.fingerID)) break;
        if (event.type==SDL_EVENT_FINGER_CANCELED) {
            cancel_window_pointer(*state);
            break;
        }
        const float x=event.tfinger.x*state->presentation.surface->size.width;
        const float y=event.tfinger.y*state->presentation.surface->size.height;
        if (event.type==SDL_EVENT_FINGER_DOWN) {
            state->pointer.down(provider,state->presentation,x,y);
            state->scroll_gesture.reset();
            if(!state->pointer.continuous()) {
                int width=0,height=0;SDL_GetWindowSize(state->window,&width,&height);
                state->scroll_gesture.begin(x,y,24.0F*state->presentation.surface->size.height/std::max(1,height));
            }
        } else if (event.type==SDL_EVENT_FINGER_MOTION) {
            const int lines=state->scroll_gesture.move(x,y);
            if(state->scroll_gesture.dragged()) {
                state->pointer.cancel(provider);
                if(lines) provider.dialog_presentation_scroll(state->scroll_gesture.x(),state->scroll_gesture.y(),lines);
            } else state->pointer.move(provider,x,y,true);
        }
        else {
            state->touch.end(event.tfinger.touchID,event.tfinger.fingerID);
            const int clicks=std::exchange(state->touch_clicks,1);
            if(!state->scroll_gesture.dragged()) state->pointer.up(provider,state->presentation,x,y,clicks);
            else state->pointer.cancel(provider);
            state->scroll_gesture.reset();
        }
        break;
    }
    case SDL_EVENT_MOUSE_WHEEL: {
        const auto [x, y] = point(event.wheel.mouse_x, event.wheel.mouse_y);
        const bool horizontal=event.wheel.y==0 && event.wheel.x!=0;
        const auto raw=horizontal?event.wheel.x:event.wheel.y;
        const auto directed=event.wheel.direction==SDL_MOUSEWHEEL_FLIPPED?-raw:raw;
        const auto delta=::cw::util::get_wheelrotation(directed>0?1:(directed<0?-1:0),horizontal);
        if (delta != 0) dispatch_dialog_wheel(provider,x,y,delta);
        break;
    }
    case SDL_EVENT_TEXT_INPUT:
        if (state->presentation.wants_text_input) provider.dialog_presentation_text_event({event.text.text ? event.text.text : "", false, -1, 0});
        break;
    case SDL_EVENT_TEXT_EDITING:
        if (state->presentation.wants_text_input) provider.dialog_presentation_text_event({event.edit.text ? event.edit.text : "", true, event.edit.start, event.edit.length});
        break;
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP: {
        const bool down = event.type == SDL_EVENT_KEY_DOWN;
        const bool repeated_commit = down && event.key.repeat &&
            (event.key.key == SDLK_ESCAPE || event.key.key == SDLK_AC_BACK ||
             event.key.key == SDLK_RETURN || event.key.key == SDLK_KP_ENTER);
        if (repeated_commit) {
            // wx と同様、キーリピートでモーダルの確定・キャン
            // セルを繰り返さない。 ここで消費し、汎用キーハンド
            // ラーへ流れて新たに露出した親を閉じないようにする。
        }
        else if (!state->debugger && ::cw::lib::engine_sdl_cancel(event.key.key, down, event.key.repeat))
            provider.dialog_presentation_escape();
        else if (down && (event.key.key == SDLK_RETURN || event.key.key == SDLK_KP_ENTER) && !state->debugger) {
            DialogPresentationModifiers modifiers{};
            if (event.key.mod & SDL_KMOD_SHIFT) modifiers |= DialogPresentationModifierShift;
            if (event.key.mod & SDL_KMOD_CTRL) modifiers |= DialogPresentationModifierControl;
#if defined(__APPLE__)
            if (event.key.mod & SDL_KMOD_GUI) modifiers |= DialogPresentationModifierControl;
#endif
            if (event.key.mod & SDL_KMOD_ALT) modifiers |= DialogPresentationModifierAlt;
            provider.dialog_presentation_enter(modifiers);
        }
        else if (const auto key = presentation_key_from_sdl(event.key.key)) {
            DialogPresentationModifiers modifiers{};
            if (event.key.mod & SDL_KMOD_SHIFT) modifiers |= DialogPresentationModifierShift;
            if (event.key.mod & SDL_KMOD_CTRL) modifiers |= DialogPresentationModifierControl;
#if defined(__APPLE__)
            if (event.key.mod & SDL_KMOD_GUI) modifiers |= DialogPresentationModifierControl;
#endif
            if (event.key.mod & SDL_KMOD_ALT) modifiers |= DialogPresentationModifierAlt;
            provider.dialog_presentation_key(*key, down, modifiers);
        }
        break;
    }
    default: break;
    }
}
}

bool dispatch_standalone_presentation_event(const SDL_Event& event) {
    if (event.type==SDL_EVENT_WILL_ENTER_BACKGROUND) {
        for (const auto& state : windows)
            if (state->shown) cancel_window_pointer(*state);
        return false; // Engine にも生存期間のイベントを渡す。
    }
    auto* event_window = SDL_GetWindowFromEvent(&event);
    if (event.type == SDL_EVENT_RENDER_DEVICE_RESET) {
        bool handled = false;
        for (const auto& state : windows) {
            if (event_window && state->window != event_window) continue;
            SDL_DestroyTexture(state->texture);
            state->texture = nullptr;
            state->uploaded_surface_revision = 0;
            state->tooltip_composed = false;
            state->dirty = true;
            handled = true;
        }
        // ウィンドウを持たないレンダラーイベントはプロセス全体に対するものである。
        // ダイアログのリソースを無効化した後、Engine の主レンダラーにも渡す。
        return event_window ? handled : false;
    }
    // 破棄した子画面の Presentation が終了した後も、SDL キューに HIDDEN/FOCUS_LOST
    // が残る場合がある。 そのウィンドウ通知は Engine やアプリの生存期間イ
    // ベントではない。Engine の両方のイベント取得処理でこの境界を使う。 全
    // 体の Background/Foreground 通知はこの範囲に含めない。
    if (event.type >= SDL_EVENT_WINDOW_FIRST && event.type <= SDL_EVENT_WINDOW_LAST &&
        !event_window) return true;
    const auto state = find_window(event_window);
    if (!state) {
        if (!event_window ||
            (event.type != SDL_EVENT_DROP_BEGIN && event.type != SDL_EVENT_DROP_FILE &&
             event.type != SDL_EVENT_DROP_COMPLETE)) return false;
        const auto* object = ::cw::g_cwpy
            ? std::get_if<::cw::lib::ObjectPtr>(&::cw::g_cwpy->m_frame.storage()) : nullptr;
        const auto frame = object && *object
            ? std::dynamic_pointer_cast<::cw::frame::Frame>(*object) : nullptr;
        if (!frame || !frame->m_native_adapter || frame->m_native_adapter->is_hosted())
            return false;
#ifdef _WIN32
        const auto native_window = reinterpret_cast<std::uintptr_t>(SDL_GetPointerProperty(
            SDL_GetWindowProperties(event_window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
#else
        const auto native_window = reinterpret_cast<std::uintptr_t>(event_window);
#endif
        if (!native_window || native_window != frame->m_native_window_handle) return false;
        // 単体版の両方の SDL イベント取得処理がこの境界を通る。 SDL から借用し
        // た UTF-8 データを一まとまりでコピーし、Win32 やホスト内のドロップ
        // と同じ Frame キュー、 モーダル登録、インストール・スキン処理を使う。
        if (event.type == SDL_EVENT_DROP_BEGIN) frame->m_native_drop_files.clear();
        else if (event.type == SDL_EVENT_DROP_FILE) {
            if (event.drop.data && *event.drop.data)
                frame->m_native_drop_files.emplace_back(event.drop.data);
        } else {
            auto payload = std::make_shared<::cw::lib::Window>();
            payload->dropped_files = std::move(frame->m_native_drop_files);
            frame->m_native_drop_files.clear();
            if (!payload->dropped_files.empty())
                frame->dispatch_native_event("DROPFILES", std::move(payload));
        }
        return true;
    }
    if (state->shown && !state->provider->dialog_presentation_finished())
        dispatch_window_event(state, event);
    return true;
}

::cw::frame::ScreenshotWindows capture_presentation_windows(
    const ::cw::frame::Frame& frame, bool capture_clients)
{
    frame.assert_ui_thread();
    const auto identity = [](SDL_Window* window) -> std::uint64_t {
#ifdef _WIN32
        if (window) if (const auto handle = SDL_GetPointerProperty(
                SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr))
            return reinterpret_cast<std::uintptr_t>(handle);
#endif
        return reinterpret_cast<std::uintptr_t>(window);
    };
    const auto outer_bounds = [](SDL_Window* window) -> ::cw::frame::ScreenshotRect {
        int x{}, y{}, w{}, h{}, top{}, left{}, bottom{}, right{};
        SDL_GetWindowPosition(window, &x, &y);
        SDL_GetWindowSize(window, &w, &h);
        if (!SDL_GetWindowBordersSize(window, &top, &left, &bottom, &right))
            top = left = bottom = right = 0;
        // SDL は表示領域の原点と寸法を返すが、wx.GetRect は枠などの
        // 装飾も含む。 UI 配置は変えず、このキャプチャー処理だけで変換する。
        return {static_cast<double>(x-left), static_cast<double>(y-top),
            static_cast<double>(w+left+right), static_cast<double>(h+top+bottom)};
    };
    ::cw::frame::ScreenshotWindows result;
    result.root = frame.m_native_window_handle;
    if (auto* root = main_sdl_window()) result.root = identity(root);
    const auto bounds = owner_rect(frame);
    result.frame_bounds = {static_cast<double>(bounds.x), static_cast<double>(bounds.y),
        static_cast<double>(bounds.width), static_cast<double>(bounds.height)};
    if (auto* root = main_sdl_window()) result.frame_bounds = outer_bounds(root);
    for (const auto& state : windows) {
        if (!state || state->frame_owner.lock().get() != &frame || !state->window || !state->model) continue;
        ::cw::frame::ScreenshotWindow captured;
        captured.id = identity(state->window);
        const auto parent = find_window(state->owner_window);
        captured.parent = parent ? identity(parent->window) : result.root;
        captured.title = state->presentation.title;
        int w{}, h{};
        SDL_GetWindowSize(state->window, &w, &h);
        captured.bounds = outer_bounds(state->window);
        const auto flags = SDL_GetWindowFlags(state->window);
        captured.visible = state->shown && !(flags & SDL_WINDOW_HIDDEN);
        captured.iconized = state->iconized || (flags & SDL_WINDOW_MINIMIZED);
        captured.debugger = state->debugger || state->presentation.debugger;
        captured.focused = SDL_GetKeyboardFocus() == state->window;
        captured.touch_tools = state->presentation.route == "TOUCHTOOLS";
        if (capture_clients && captured.visible && !captured.iconized && state->presentation.surface) {
            // 共通 Presentation はホストが描く表示領域の画像である。 wx.ClientDC
            // の取得結果は RGB なので、元の RGBA テクスチャーにモデ
            // ル描画用のアルファがあっても、表示されている RGB を保持する。
            auto pixels = std::make_shared<::cw::lib::Surface>(
                *state->tooltip.compose(state->presentation));
            pixels->has_pixel_alpha = false;
            for (std::size_t i = 3; i < pixels->rgba.size(); i += 4) pixels->rgba[i] = 255;
            if (pixels->size.width != w || pixels->size.height != h)
                pixels = ::cw::image::smoothscale(pixels, {w, h}, true, false);
            captured.client = std::move(pixels);
        }
        result.windows.push_back(std::move(captured));
    }
    return result;
}

void pump_standalone_presentations(const std::shared_ptr<::cw::frame::Frame>& frame) {
    ::cw::lib::RuntimeAccess runtime_access;
    if (!frame || !frame->m_native_adapter || frame->m_native_adapter->is_hosted() ||
        frame->m_native_adapter->is_headless()) return;
    prewarm_dialog_window();
    const auto model = frame->dialog_presentation_model();
    const std::shared_ptr<::cw::lib::Object> debugger =
        frame->m_debugger ? *frame->m_debugger : nullptr;
    auto active = ensure_window(*frame, model, false);
    auto debug = ensure_window(*frame, debugger, true);
#if !defined(_WIN32)
    // SDL ホストではモデルの完了とネイティブウィンドウの表示状態を分ける。 完了済み
    // の表示プロバイダーを、アクティブなネイティブのモーダルウィンドウとして残さない。
    if (active && active->provider &&
        active->provider->dialog_presentation_finished()) {
        active.reset();
    }
#endif
    // モードレスのデバッガーは、Engine のメインウィンドウを無効にしない。
    set_background_enabled(*frame, !active);
    bool hid_completed_modal = false;
    for (const auto& state : windows) {
        // ゲームスレッドの終了処理が反映されるまで、終了中のモーダルを表示しておく。
        // 無効な所有者の最後のポップアップを先に隠すと、Windows が Engine
        // の背後の無関係なウィンドウを一フレームだけアクティブにすることがある。 反
        // 映後の処理では上の set_background_enabled() で
        // Engine を先に復元し、フォーカスの空白を作らずこのウィンドウを隠す。
#ifdef _WIN32
        const bool awaiting_modal_commit = state->shown && state->completion_dispatched &&
            !state->debugger && !active &&
            ::cw::g_cwpy && ::cw::g_cwpy->is_showingdlg();
#else
        // 無効な所有者を復元する間、完了したポップアップを表示しておく必要があるのは
        // Win32 だけである。 SDL のダイアログは完了直後に隠してよい。
        const bool awaiting_modal_commit = false;
#endif
        // 覆われたモーダルの親もすべて表示したままにする。Python/wx
        // はデバッガー、状態編集、持続時間編集の順に重ねる。 中
        // 間のウィンドウを隠すと、見た目と所有関係の連鎖が崩れ、最
        // 深の子を閉じた時に親がデバッガーの背後へ戻ってしまう。
        const bool suspended = state->model &&
            std::find(suspended_models.begin(),suspended_models.end(),state->model)!=suspended_models.end();
#ifdef _WIN32
        const bool current_visible = state == active || state == debug;
#else
        // Frame の終了処理を反映している間、ensure_window()
        // が完了済みの表示プロバイダーを返す場合がある。 ユーザーがダイアログを閉
        // じた後に、それを理由に SDL のネイティブウィンドウを表示し続けない。
        const bool current_visible = (state == active || state == debug) &&
            (!state->provider || !state->provider->dialog_presentation_finished());
#endif
        const bool visible = current_visible || suspended || awaiting_modal_commit;
        // OS の閉じるボタンは、モデルのキャンセル処理が終了する前に
        // SDL ウィンドウを隠す場合がある。 shown はネイティブ
        // の表示変化ではなく Presentation の生存期間を記録
        // するため、再利用時は両方の状態を整合させる。 そうしないと、
        // 見えないモーダルが残って Engine が無効のままになる。
        const bool native_hidden = native_window_hidden(*state);
        if (visible && (!state->shown || native_hidden)) {
            SDL_ShowWindow(state->window);
            state->shown = true;
#if !defined(_WIN32)
            // ダイアログが表示状態になった時だけ前面へ出す。 継続的に前面へ出すと
            // 、SDL ホストで Cmd+Tab などのアプリ切り替えを妨げる。
            if (state == active &&
                !::cardwirth::debug::nonactive_automation_enabled()) {
                SDL_RaiseWindow(state->window);
            }
#endif
        } else if (!visible && (state->shown || !native_hidden)) {
            cancel_window_pointer(*state);
            SDL_HideWindow(state->window);
            hid_completed_modal = hid_completed_modal ||
                (!state->debugger && state->completion_dispatched);
            state->shown = false;
            state->dropped_files.clear();
        }
    }

    // 所有関係のある複数のウィンドウを表示しても、既に表示中のどれが
    // 最前面に残るかは保証されない。 デバッガー・親・子の全体を表
    // 示した後、最深のアクティブなモーダルだけを改めて前面へ出す。
#ifdef _WIN32
    if (active && active->shown &&
        !::cardwirth::debug::nonactive_automation_enabled()) {
        SDL_RaiseWindow(active->window);
        auto handle=static_cast<HWND>(SDL_GetPointerProperty(
            SDL_GetWindowProperties(active->window),SDL_PROP_WINDOW_WIN32_HWND_POINTER,nullptr));
        if(handle)SetWindowPos(handle,active->debugger_descendant?HWND_TOPMOST:HWND_TOP,
            0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_NOOWNERZORDER);
    }
#endif

    // wx の ShowModal は所有者の復元後に戻る。SDL は完了
    // した子を隠す時にアクティブ状態を変える場合があり、 SDL_HideWindow()
    // の直前に Engine を前面へ出しても防げない。完了したモー
    // ダルごとに一度だけ、非表示後に所有者を再びアクティブにし、 最
    // 初の対象クリックがアクティブ化だけで消費されず届くようにする。
    if (hid_completed_modal && !active &&
        (!::cw::g_cwpy || !::cw::g_cwpy->is_showingdlg()))
        frame->set_background_enabled(true);

    // 対象ウィンドウのイベントだけを取り出し、Engine の入力は元のキューに残す。 コー
    // ルバックがモーダルを開く場合があるため、SDL のキューロックを解放してから処理する。
    struct Queued {
        std::shared_ptr<PresentationWindow> state;
        SDL_Event event;
        std::string text;
    };
    std::vector<Queued> pending;
    SDL_PumpEvents();
    SDL_FilterEvents([](void* data, SDL_Event* event) -> bool {
        if (event->type==SDL_EVENT_WILL_ENTER_BACKGROUND) {
            for (const auto& window : windows)
                if (window->shown) static_cast<std::vector<Queued>*>(data)->push_back({window,*event,{}});
            return true;
        }
        auto state = find_window(SDL_GetWindowFromEvent(event));
        if (!state) return true;
        if (!state->shown) return false;
        Queued next{state, *event, {}};
        if (event->type == SDL_EVENT_TEXT_INPUT && event->text.text) next.text = event->text.text;
        if (event->type == SDL_EVENT_TEXT_EDITING && event->edit.text) next.text = event->edit.text;
        // SDL が drop.data を所有するのはイベントの生存期間中だけである。 遅延
        // キューでは文字入力データと同様に自身で所有する。処理中にモーダルを開く場合もある。
        if (event->type == SDL_EVENT_DROP_FILE && event->drop.data) next.text = event->drop.data;
        static_cast<std::vector<Queued>*>(data)->push_back(std::move(next));
        return false;
    }, &pending);
    for (auto& next : pending) {
        if (!next.state->shown || next.state->provider->dialog_presentation_finished()) continue;
        if (next.event.type == SDL_EVENT_TEXT_INPUT) next.event.text.text = next.text.c_str();
        if (next.event.type == SDL_EVENT_TEXT_EDITING) next.event.edit.text = next.text.c_str();
        if (next.event.type == SDL_EVENT_DROP_FILE) next.event.drop.data = next.text.c_str();
        dispatch_window_event(next.state, next.event);
    }
    // コールバックでウィンドウが追加・削除される場合があるため、安定した共有スナップショットを走査する。
    const auto snapshot = windows;
    for (const auto& state : snapshot) {
        const bool finished = state->provider && state->provider->dialog_presentation_finished();
        // Windows 以外では、この生存期間処理より前の表示更新で完了し
        // たウィンドウを隠す場合がある。 その場合も完了を通知し、Frame
        // がモーダル処理を終えて再利用用の状態を解放できるようにする。
        if (!state->shown && !finished) continue;
        if (finished) {
            // 完了通知からゲームスレッドの終了処理を開始する。Windows
            // では showingdlg が解除されるまで最後の子を
            // 表示しておく。 SDL ホストでは上で既に隠している場合
            // がある。YadoSelect と ScenarioSelect
            // も含め、全表示プロバイダーでこの共通経路を使う。
            if (!state->completion_dispatched &&
                std::find(modal_models.begin(), modal_models.end(), state->model) == modal_models.end()) {
                state->completion_dispatched = true;
                state->provider->dialog_presentation_complete();
                if (state->debugger) frame->close_debugger();
                else if (frame->dialog_presentation_model() == state->model) {
                    frame->clear_transient_dialog_model(state->model.get());
                    if (frame->m_active_dialog_model == state->model) {
                        if (frame->m_active_dialog_session != 0) {
                            frame->kill_dlg(std::nullopt, false, true);
                        } else {
                            // 単独の Presentation には CWPy のモーダル
                            // セッションがないため、アクティブな参照を直接解放する。 kill_dlg()
                            // は意図的にセッション 0 を拒否する。
                            if (frame->m_active_dialog)
                                frame->m_active_dialog->visible = false;
                            frame->m_active_dialog.reset();
                            frame->m_active_dialog_model.reset();
                        }
                    }
                }
            }
            continue;
        }
        const auto elapsed = SDL_GetTicks() - state->rendered_at;
        if (state->dirty || (state->provider->dialog_presentation_needs_refresh() && elapsed >= 16) ||
            (state->presentation.refresh_interval_ms && elapsed >= state->presentation.refresh_interval_ms)) {
            state->provider->begin_dialog_presentation_refresh();
            auto presentation = state->provider->dialog_presentation();
            decorate_dialog_touch_tools(presentation,*state->provider);
            publish_window_presentation(*state, std::move(presentation));
            draw_window(*state);
            state->provider->dialog_presentation_refreshed();
            state->dirty = false;
            state->rendered_at = SDL_GetTicks();
        }
        if (dialog_text_input_enabled(
                state->provider->dialog_presentation_text_input_requested(state->presentation),
                state->presentation.text_input_hardware_only, SDL_HasKeyboard())) {
            if (!SDL_TextInputActive(state->window)) SDL_StartTextInput(state->window);
        } else if (SDL_TextInputActive(state->window)) SDL_StopTextInput(state->window);
    }
    const auto current = frame->dialog_presentation_model();
    for (const auto& state : windows) {
        if (!state->model) continue;
        const bool retired = state->model != current &&
            (!frame->m_debugger || state->model != *frame->m_debugger) &&
            std::find(suspended_models.begin(), suspended_models.end(), state->model) == suspended_models.end() &&
            std::find(modal_models.begin(), modal_models.end(), state->model) == modal_models.end();
#ifdef _WIN32
        const bool awaiting_modal_commit = retired && state->shown &&
            state->completion_dispatched && !state->debugger && ::cw::g_cwpy &&
            ::cw::g_cwpy->is_showingdlg();
#else
        // SDL ホストには、Win32 の所有者アクティブ化の空白を保護する必要はない。
        const bool awaiting_modal_commit = false;
#endif
        if (retired && !awaiting_modal_commit) retire_window(*state);
    }
}

void close_standalone_presentations() {
    clear_dialog_debug_snapshots();
    windows.clear();
    background_enabled = true;
    if (quit_hint_saved) {
        if (previous_quit_hint)
            SDL_SetHint(SDL_HINT_QUIT_ON_LAST_WINDOW_CLOSE, previous_quit_hint->c_str());
        else SDL_ResetHint(SDL_HINT_QUIT_ON_LAST_WINDOW_CLOSE);
        previous_quit_hint.reset();
        quit_hint_saved = false;
    }
}

bool show_standalone_modal(const std::shared_ptr<::cw::frame::Frame>& frame,
                           const std::shared_ptr<::cw::lib::Object>& model) {
    ::cw::lib::RuntimeAccess runtime_access;
    frame->assert_ui_thread();
    const auto provider = std::dynamic_pointer_cast<DialogPresentationProvider>(model);
    if (!provider || frame->m_native_adapter->is_hosted() || frame->m_native_adapter->is_headless())
        return false;
    const auto previous_model = frame->m_active_dialog_model;
    const auto previous_dialog = frame->m_active_dialog;
    const auto previous_session = frame->m_active_dialog_session;
    const auto advertised_presentation = frame->dialog_presentation_model();
    // 共通編集コントロールは、このモーダル関数へ入る直前に自身を
    // 一時表示として公開する。 その新しい子は前の表示ではない。
    // 自身を親として扱うと、デバッガー、状態編集、持続時間編集の
    // 実際の階層を失い、 別のウィンドウを復元・終了してしまう
    // 。この場合、実際に覆われる親はアクティブなモデルである。
    const bool model_already_transient = advertised_presentation == model &&
        previous_model && previous_model != model;
    auto previous_presentation = model_already_transient
        ? previous_model : advertised_presentation;
    // 開く対象として既に公開されている Presentation
    // は、覆われる親ではない。 前の表示として扱うと、隠したばかり
    // の完了済みダイアログを Restore が再表示してしまう。
    if (previous_presentation == model) {
        previous_presentation = previous_model && previous_model != model
            ? previous_model : nullptr;
    }
    const bool covered_transient = previous_presentation && previous_presentation != previous_model;
    bool modal_model_registered = false;
    bool suspended_model_registered = false;
    struct Restore {
        std::function<void()> action;
        ~Restore() noexcept {
            try { action(); } catch (...) {}
        }
    } restore{[&] {
        if (modal_model_registered && !modal_models.empty()) modal_models.pop_back();
        if (suspended_model_registered && !suspended_models.empty()) suspended_models.pop_back();
        if (frame->m_active_dialog_model == model || !frame->m_active_dialog_model) {
            frame->m_active_dialog_model = previous_model;
            frame->m_active_dialog = previous_dialog;
            frame->m_active_dialog_session = previous_session;
            if (previous_dialog) previous_dialog->visible = true;
        }
        if (covered_transient && previous_presentation && previous_presentation != model) {
            try { frame->set_transient_dialog_model(previous_presentation); } catch (...) {}
        }
        // 子を隠す前に、覆われた所有者を復元して前面へ出す。 す
        // べての同期的な共通ダイアログでこの順序を使い、Engine
        // の背後のウィンドウがアクティブになる隙間を防ぐ。
        std::shared_ptr<PresentationWindow> restored;
        try {
            if (previous_presentation && previous_presentation != model) {
                restored = ensure_window(*frame, previous_presentation, false);
                if (restored) {
                    if (!restored->shown || native_window_hidden(*restored)) {
                        SDL_ShowWindow(restored->window);
                        restored->shown = true;
                    }
                    SDL_RaiseWindow(restored->window);
                }
            }
            if (restored) {
                set_background_enabled(*frame, false);
            } else {
                // RETURNTITLE などの Frame コマンドは論理的なモーダルセ
                // ッションを持つが、同期的な確認の背後に表示中の Presentation
                // の親があるとは限らない。 戻る前に実際のネイティブ所有者を復元する。
                background_enabled = true;
                frame->set_background_enabled(true);
            }
        } catch (...) {
            // 上の階層と所有者の復元は、モーダル処理で必ず行う。
            // 例外の巻き戻し中のネイティブウィンドウ再作成は可
            // 能な範囲で試み、失敗してもプロセスを終了させない。
        }
        for (const auto& state : windows) {
            if (state->model != model) continue;
            try { retire_window(*state); } catch (...) {}
        }
        // SDL_HideWindow は、直前に所有者を前面へ出してい
        // てもアクティブ状態を変える場合がある。 非同期のモーダル終了
        // 経路と同様、子を隠した後に残る所有者を再びアクティブにする。
        if (restored) {
            if (restored->shown && !::cardwirth::debug::nonactive_automation_enabled())
                try { SDL_RaiseWindow(restored->window); } catch (...) {}
        } else {
            try { frame->set_background_enabled(true); } catch (...) {}
        }
    }};
    modal_models.push_back(model);
    modal_model_registered = true;
    if (previous_presentation && previous_presentation != model) {
        suspended_models.push_back(previous_presentation);
        suspended_model_registered = true;
    }
    if (model_already_transient) frame->clear_transient_dialog_model(model.get());
    if (covered_transient) frame->clear_transient_dialog_model(previous_presentation.get());
    frame->m_active_dialog_model = model;
    frame->m_active_dialog = std::make_shared<::cw::lib::Dialog>();
    frame->m_active_dialog->visible = true;
    frame->m_active_dialog_session = 0;
    if (!ensure_window(*frame, model, false)) return false;
    while (!::cw::g_quit_app && !provider->dialog_presentation_finished()) {
        // 前面のダイアログが占有するのは入力であり、Engine の時計ではない。
        // 表示更新をまとめて予約し、メインウィンドウの動画などを動かし続ける。
        if (::cw::g_cwpy) ::cw::g_cwpy->request_presentation_update();
        pump_standalone_presentations(frame);
#ifdef _WIN32
        // 主描画面はまだ HWND を使うため、SDL が子ウィンドウを所有する間も
        // 、そのメッセージ受信を維持する。 ここではダイアログ UI を実装しない。
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) { ::cw::g_quit_app = true; break; }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
#endif
        frame->drain_present_queue();
        frame->OnEXECFUNC(nullptr);
        ::cw::lib::without_runtime_access([&] { std::this_thread::sleep_for(std::chrono::milliseconds(4)); });
    }
    // complete() より前に終了理由を取得する。Progress::complete
    // はモデルを閉じるため、 先に呼ぶとアプリ終了による中断が通常の
    // モーダル成功終了になってしまう。ホスト内の経路もこの順序を使う。
    const bool finished=provider->dialog_presentation_finished();
    provider->dialog_presentation_complete();
    return finished;
}


void finalize_logical_presentation(
    DialogPresentation& presentation,
    const std::shared_ptr<::cw::lib::Surface>& logical_surface,
    const DialogPresentationScalePolicy policy)
{
    const auto scale = policy == DialogPresentationScalePolicy::DebuggerDpi
        ? (std::max<::cw::lib::Real>)(static_cast<::cw::lib::Real>(::cw::g_dpi_level), 1.0)
        : (std::max<::cw::lib::Real>)(::cw::g_UP_WIN, 1.0);
    presentation.debugger = policy==DialogPresentationScalePolicy::DebuggerDpi;
    if (scale > 1.0 + 1e-9) {
        for (auto& control : presentation.controls) {
            control.x = static_cast<float>(control.x * scale);
            control.y = static_cast<float>(control.y * scale);
            control.width = static_cast<float>(control.width * scale);
            control.height = static_cast<float>(control.height * scale);
        }
    }
    if (!logical_surface || scale <= 1.0 + 1e-9) {
        presentation.surface = logical_surface;
    } else if (policy == DialogPresentationScalePolicy::GameWindow) {
        presentation.surface = ::cw::lib::ui::wins_surface(logical_surface);
    } else {
        const auto width = static_cast<::cw::lib::Integer>(
            std::lround(logical_surface->size.width * scale));
        const auto height = static_cast<::cw::lib::Integer>(
            std::lround(logical_surface->size.height * scale));
        presentation.surface = ::cw::image::smoothscale_card(
            logical_surface, {width, height}, true);
        ::cw::lib::ui::inherit_surface_animation(presentation.surface,logical_surface);
    }
}

void add_presentation_control(
    DialogPresentation& presentation,
    const std::shared_ptr<::cw::lib::Surface>& logical_surface,
    const ::cw::lib::String& label, const ::cw::lib::Rect& rect,
    const DialogPresentationAction action, const int index,
    const bool enabled, const ::cw::lib::String& font_role,
    const bool center_vertically)
{
    ::cw::lib::ui::draw_button_face(logical_surface, rect, {enabled});
    const auto font = ::cw::lib::ui::configured_font(font_role, 14);
    const auto label_width = ::cw::lib::ui::label_width(
        label, font.pixels, font.bold, font.face);
    // コントロールの寸法でクリッピングして描画する。スキンの見出
    // しは Python の固定幅ボタンより長い場合があるが、
    // 当たり判定が変わらない隣のコントロールへはみ出させない。ク
    // リップ範囲は描画先の拡大率に従い、 拡大済みの canvas.layer
    // 上では見出しを最終サイズでラスター化する。
    const auto clip_scale=logical_surface && std::isfinite(logical_surface->draw_scale) &&
        logical_surface->draw_scale>0.0 ? logical_surface->draw_scale : 1.0;
    auto clipped=std::make_shared<::cw::lib::Surface>();
    clipped->size={static_cast<::cw::lib::Integer>(std::lround(std::max<::cw::lib::Integer>(0,rect.width)*clip_scale)),
                   static_cast<::cw::lib::Integer>(std::lround(std::max<::cw::lib::Integer>(0,rect.height)*clip_scale))};
    clipped->draw_scale=clip_scale;
    clipped->rgba.assign(static_cast<std::size_t>(clipped->size.width)*clipped->size.height*4,0);
    ::cw::lib::ui::draw_label(clipped, label,
        (std::max<::cw::lib::Integer>)(0,(rect.width-label_width)/2),
        center_vertically?(std::max<::cw::lib::Integer>)(0,(rect.height-font.pixels)/2):5,
        font.pixels,font.bold,
        enabled?std::tuple<::cw::lib::Integer,::cw::lib::Integer,::cw::lib::Integer>{0,0,0}:
            std::tuple<::cw::lib::Integer,::cw::lib::Integer,::cw::lib::Integer>{100,100,100},font.face);
    ::cw::lib::ui::blend_surface(logical_surface,clipped,rect.x,rect.y);
    presentation.controls.push_back(
        {action, index, static_cast<float>(rect.x), static_cast<float>(rect.y),
         static_cast<float>(rect.width), static_cast<float>(rect.height), enabled});
    presentation.controls.back().hover_highlight=true;
}

bool is_hosted_presentation_host() noexcept
{
    const auto frame = active_frame();
    return frame && frame->m_native_adapter &&
           frame->m_native_adapter->supports_common_presentation();
}

bool is_hosted_dialog_presentation(const ::cw::lib::Object* model) noexcept
{
    if (!model) return false;
    const auto frame = active_frame();
    if (!frame || !frame->m_native_adapter ||
        !frame->m_native_adapter->supports_common_presentation() ||
        frame->dialog_presentation_model().get() != model)
        return false;
    return static_cast<bool>(std::dynamic_pointer_cast<DialogPresentationProvider>(
        frame->dialog_presentation_model()));
}

void publish_transient_dialog_presentation(
    std::shared_ptr<::cw::lib::Object> model) noexcept
{
    if (!model) return;
    if (const auto frame = active_frame())
        frame->set_transient_dialog_model(std::move(model));
}

void clear_transient_dialog_presentation(const ::cw::lib::Object* model) noexcept
{
    if (!model) return;
    if (const auto frame = active_frame())
        frame->clear_transient_dialog_model(model);
}

static ::cw::lib::documents::Dispatch document_owner_dispatch(std::weak_ptr<::cw::frame::Frame> owner)
{
    return [owner](std::function<void()> callback) {
        const auto frame=owner.lock();
        if(!frame || !frame->m_native_adapter || frame->m_native_adapter->is_headless()) return false;
        frame->post_func([callback=std::move(callback)](const auto&,const auto&){callback();return ::cw::lib::Value{};});
        return true;
    };
}

std::shared_ptr<::cw::lib::documents::Ticket> request_presentation_document(
    const ::cw::lib::documents::Request& request,::cw::lib::documents::Completion completion)
{
    auto input_lease = capture_current_dialog_input_lease();
    if (completion && input_lease) {
        completion = [completion = std::move(completion),
                      input_lease = std::move(input_lease)](auto result) mutable {
            completion(std::move(result));
        };
    }
    const auto frame=active_frame();
    if(!frame || !frame->m_native_adapter || frame->m_native_adapter->is_headless()) {
        if(completion) completion({::cw::lib::documents::Status::Failed,{},"ファイル選択のホストがありません"});
        return {};
    }
    const std::weak_ptr<::cw::frame::Frame> owner=frame;
    auto owned_request=request;
    if(!owned_request.parent_window_id && !frame->m_native_adapter->is_hosted() && SDL_IsMainThread()) {
        const auto model=frame->dialog_presentation_model();
        for(const auto& window:windows) if(window->model==model && window->window) {
            owned_request.parent_window_id=SDL_GetWindowID(window->window);break;
        }
    }
    return ::cw::lib::documents::request(owned_request,document_owner_dispatch(owner),std::move(completion));
}

std::shared_ptr<::cw::lib::documents::Ticket> write_presentation_document(
    const ::cw::lib::documents::Document& target,std::vector<std::uint8_t> bytes,
    ::cw::lib::documents::Completion completion)
{
    auto input_lease = capture_current_dialog_input_lease();
    if (completion && input_lease) {
        completion = [completion = std::move(completion),
                      input_lease = std::move(input_lease)](auto result) mutable {
            completion(std::move(result));
        };
    }
    const auto frame=active_frame();
    if(!frame || !frame->m_native_adapter || frame->m_native_adapter->is_headless()) {
        if(completion) completion({::cw::lib::documents::Status::Failed,{},"文書保存のホストがありません"});
        return {};
    }
    const std::weak_ptr<::cw::frame::Frame> owner=frame;
    return ::cw::lib::documents::write(target,std::move(bytes),document_owner_dispatch(owner),std::move(completion));
}

bool post_common_dialog_action(::cw::lib::Callback callback)
{
    const auto frame=active_frame();
    if(!frame || !callback) return false;
    frame->post_func(std::move(callback));
    return true;
}

bool post_common_dialog_input_update(::cw::lib::Callback callback)
{
    const auto frame=active_frame();
    return frame && frame->post_input_update(std::move(callback));
}

bool show_common_modal_presentation(
    std::shared_ptr<::cw::lib::Object> model) noexcept
{
    return show_common_modal_presentation(active_frame(), std::move(model));
}

bool show_common_modal_presentation_with_owner(
    std::shared_ptr<::cw::lib::Object> model,
    std::shared_ptr<::cw::lib::Window> owner) noexcept
{
    return show_common_modal_presentation(active_frame(), std::move(model), std::move(owner));
}

bool show_common_modal_presentation(
    std::shared_ptr<::cw::frame::Frame> frame,
    std::shared_ptr<::cw::lib::Object> model,
    std::shared_ptr<::cw::lib::Window> requested_owner) noexcept
{
    ::cw::lib::RuntimeAccess runtime_access;
    if (!frame || !model || !frame->m_native_adapter) return false;
    try {
        if (frame->m_exec_shutdown.load(std::memory_order_acquire)) return false;
        if (frame->m_ui_thread != std::thread::id{} &&
            frame->m_ui_thread != std::this_thread::get_id()) {
            // イベントのコモンエラー・InputDialog、起動時通知、ワーカーの確
            // 認は、call_modaldlg を通らずここへ届く場合がある。 Python
            // の wx モーダルホストと同様、単体 SDL と ALL の双方で、
            // モーダル階層やネイティブアダプターに触れる前に UI スレッドへ送る。
            // Frame の同期メッセージ処理で実際の結果を返し、入れ子の確認も一
            // つの操作となるよう元の入力受付権を保持する。 ディスパッチャーが停止・
            // 拒否した場合は下で失敗として終了し、ワーカー上の表示へ切り替えない。
            // Wasm も同じ pthread のメッセージ処理を使い、Asyncify
            // や JavaScript の継続を例外で渡す方式は使わない。
            return frame->sync_exec(retain_current_dialog_input(
                [frame, model, requested_owner](const auto&, const auto&) -> ::cw::lib::Value {
                    return ::cw::lib::Value(show_common_modal_presentation(frame, model, requested_owner));
                })).as_bool();
        }
        const auto previous_override = g_modal_frame_override;
        const auto previous_owner = g_modal_requested_owner;
        g_modal_frame_override = frame;
        g_modal_requested_owner = requested_owner;
        struct RestoreOverride {
            std::weak_ptr<::cw::frame::Frame> previous;
            std::weak_ptr<::cw::lib::Window> owner;
            ~RestoreOverride() {
                g_modal_frame_override = std::move(previous);
                g_modal_requested_owner = std::move(owner);
            }
        } restore{previous_override, previous_owner};
        // Frame/Debugger は wx ウィンドウではなく Object モデルである
        // 。 どちらのホスト処理よりも前のこの共通入口でモーダル所有関係を公開し、util::has_modalchild
        // が単体版と ALL で同じ関係を参照できるようにする。
        auto root=frame->window_identity();
        auto owner=requested_owner ? requested_owner : root;
        if(!requested_owner && current_dialog_input_from_debugger() && frame->m_debugger && *frame->m_debugger)
            owner=(*frame->m_debugger)->m_window_identity;
        auto modal=std::dynamic_pointer_cast<::cw::lib::Dialog>(model);
        if(!modal) modal=std::make_shared<::cw::lib::Dialog>();
        const auto old_parent=modal->parent_window;
        const auto old_state=::cw::util::get_window_state(modal);
        const bool old_visible=modal->visible;
        modal->visible=true;
        modal->parent_window=owner;
        auto child_state=old_state.value_or(::cw::util::WindowState{});
        child_state.parent=owner;child_state.modal=true;
        ::cw::util::set_window_state(modal,std::move(child_state));
        auto owner_state=::cw::util::get_window_state(owner).value_or(::cw::util::WindowState{});
        owner_state.children.emplace_back(modal);
        ::cw::util::set_window_state(owner,std::move(owner_state));
        struct RestoreModalIdentity {
            std::function<void()> action;
            ~RestoreModalIdentity() noexcept {try{action();}catch(...){}}
        } restore_modal{[owner,modal,old_parent,old_state,old_visible] {
            auto state=::cw::util::get_window_state(owner).value_or(::cw::util::WindowState{});
            std::erase_if(state.children,[&](const auto& child){return child.lock()==modal;});
            ::cw::util::set_window_state(owner,std::move(state));
            const auto current=::cw::util::get_window_state(modal);
            // ホスト呼び出し中にモデルの終了ハンドラーが表示・モー
            // ダル状態を解除する場合がある。 所有権のスコープを抜
            // ける時、その変化を維持し、表示前の状態へ戻さない。
            const bool closed=!modal->visible || (current && !current->modal);
            auto restored=old_state.value_or(::cw::util::WindowState{});
            if(closed) restored.modal=false;
            modal->parent_window=old_parent;modal->visible=old_visible && !closed;
            ::cw::util::set_window_state(modal,std::move(restored));
        }};
        if (frame->m_native_adapter->modal_presentation(*frame, model)) return true;
        return show_standalone_modal(frame, model);
    } catch (const std::exception& error) {
        ::cardwirth::debug::log(
            std::string("Common modal presentation failed: ") + error.what());
        return false;
    } catch (...) {
        ::cardwirth::debug::log("Common modal presentation failed: unknown exception");
        return false;
    }
}

}  // namespace cw::dialog
