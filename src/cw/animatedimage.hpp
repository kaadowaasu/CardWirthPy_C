#pragma once

// cw/animatedimage.py の非同期デコーダ／共有スロットを移植した宣言。
// PIL/pygame/wx の変換は既定のportable RGBA実装を持ち、必要なら
// SDL/native backendをこのファイルのadapterへ差し替えられる。
#include <mutex>
#include <memory>
#include <functional>
#include <string>
#include <vector>
#include "cw/lib/compat.hpp"
#include "cw/fwd.hpp"
#include "cw/lib/imagecodec.hpp"

namespace cw::animatedimage {
struct FrameMetadata {
    ::cw::lib::Integer frame_index{};
    ::cw::lib::Integer duration_ms{100};
    bool has_alpha{};
    bool has_transparency{};
};

using SurfaceFrameAdapter = std::function<std::shared_ptr<::cw::lib::Surface>(
    const ::cw::lib::Bytes&, ::cw::lib::Integer, ::cw::lib::Integer, const FrameMetadata&)>;
using BitmapFrameAdapter = std::function<std::shared_ptr<::cw::lib::Bitmap>(
    const ::cw::lib::Bytes&, ::cw::lib::Integer, ::cw::lib::Integer, const FrameMetadata&)>;
using BitmapSubrectAdapter = std::function<std::shared_ptr<::cw::lib::Bitmap>(
    const std::shared_ptr<::cw::lib::Bitmap>&, ::cw::lib::Integer, ::cw::lib::Integer,
    ::cw::lib::Integer, ::cw::lib::Integer)>;

void set_surface_frame_adapter(SurfaceFrameAdapter arg_adapter);
void set_bitmap_frame_adapter(BitmapFrameAdapter arg_adapter);
void set_bitmap_subrect_adapter(BitmapSubrectAdapter arg_adapter);

class RawFrame : public ::cw::lib::Object {
public:
    RawFrame() = default;
    virtual ~RawFrame() = default;
    ::cw::lib::Bytes m_data{};
    ::cw::lib::Integer m_duration_ms{100};
    ::cw::lib::Bytes m_frame_hash{};
    std::tuple<::cw::lib::Integer, ::cw::lib::Integer> m_size{};
    ::cw::lib::Integer m_frame_index{};
    bool m_has_alpha{};
    bool m_has_transparency{};
};

class _Decoder : public ::cw::lib::Object {
public:
    _Decoder() = default;
    virtual ~_Decoder() = default;
    ::cw::lib::Bytes m_data{};
    ::cw::lib::Integer m_frame_index{};
    ::cw::lib::Integer m_frame_count{1};
    ::cw::lib::Integer m_width{};
    ::cw::lib::Integer m_height{};
    std::vector<::cw::lib::Integer> m_durations{};
    std::optional<::cw::lib::imagecodec::Animation> m_animation{};
    ::cw::lib::imagecodec::Info m_info{};
    ::cw::lib::Integer m_loop_count{};
    bool m_loop_defined{};
    bool m_has_alpha{};
    bool m_has_transparency{};
    ::cw::lib::Value m_image{};
    ::cw::lib::Value m_path{};
    void initialize(::cw::lib::String arg_path, std::optional<::cw::lib::Bytes> arg_data);
    std::shared_ptr<::cw::animatedimage::RawFrame> next_frame();
    std::shared_ptr<::cw::animatedimage::_Decoder> copy() const;
    void seek(::cw::lib::Integer arg_frame_index);
    ::cw::lib::Integer tell() const noexcept;
    ::cw::lib::Integer frame_count() const noexcept;
    ::cw::lib::Integer duration_ms(::cw::lib::Integer arg_frame_index) const;
    ::cw::lib::imagecodec::RGBAImage convert_rgba(::cw::lib::Integer arg_frame_index) const;
    ::cw::lib::Integer loop_count() const noexcept;
    bool loop_defined() const noexcept;
    bool has_alpha() const noexcept;
    bool has_transparency() const noexcept;
};

class _Instance : public ::cw::lib::Object {
public:
    _Instance() = default;
    virtual ~_Instance() = default;
    ::cw::lib::Value m_instance_id{};
    ::cw::lib::Value m_key{};
    ::cw::lib::Value m_last_update_request{};
    ::cw::lib::Value m_owner{};
    ::cw::lib::Value m_path{};
    ::cw::lib::String m_key_id;
    bool m_stopped_by_idle{};
    void initialize(::cw::lib::Integer arg_instance_id, ::cw::lib::String arg_owner, ::cw::lib::String arg_path, std::vector<::cw::lib::Value> arg_key);
    bool touch_update_request();
    bool stop_if_idle(::cw::lib::Real arg_now);
};

class _Slot : public ::cw::lib::Object {
public:
    _Slot() = default;
    virtual ~_Slot() = default;
    std::optional<std::shared_ptr<::cw::animatedimage::RawFrame>> m__current{};
    ::cw::lib::Value m__data{};
    std::optional<::cw::lib::Bytes> m__last_hash{};
    std::optional<std::shared_ptr<::cw::animatedimage::RawFrame>> m__latest{};
    ::cw::lib::Value m__lock{};
    bool m_active{};
    std::optional<std::shared_ptr<::cw::animatedimage::_Decoder>> m_decoder{};
    ::cw::lib::Integer m_frame_id{};
    ::cw::lib::Value m_generation{};
    ::cw::lib::Value m_key{};
    ::cw::lib::Value m_last_update_request{};
    ::cw::lib::Value m_path{};
    bool m_stopped_by_idle{};
    std::optional<std::shared_ptr<::cw::lib::Surface>> m_surface{};
    std::optional<std::shared_ptr<::cw::lib::Bitmap>> m_wxbmp{};
    ::cw::lib::String m_key_id;
    ::cw::lib::String m_path_text;
    std::mutex m_state_mutex;
    void initialize(std::vector<::cw::lib::Value> arg_key, ::cw::lib::String arg_path, ::cw::lib::Integer arg_generation, std::optional<::cw::lib::Bytes> arg_data);
    void initialize_decoder();
    std::shared_ptr<::cw::animatedimage::RawFrame> transparent_frame();
    bool touch_update_request();
    bool stop_if_idle(::cw::lib::Real arg_now);
    void push_frame(std::shared_ptr<::cw::animatedimage::RawFrame> arg_frame);
    bool update_if_new();
    std::optional<std::shared_ptr<::cw::animatedimage::RawFrame>> current_frame();
    bool is_active();
    void deactivate();
    ::cw::lib::Integer generation_value();
    ::cw::lib::Integer frame_id();
    std::shared_ptr<::cw::animatedimage::_Decoder> decoder_snapshot();
    std::tuple<std::optional<std::shared_ptr<::cw::animatedimage::RawFrame>>,
               std::optional<std::shared_ptr<::cw::lib::Surface>>,
               ::cw::lib::Integer> surface_snapshot();
    std::tuple<std::optional<std::shared_ptr<::cw::animatedimage::RawFrame>>,
               std::optional<std::shared_ptr<::cw::lib::Bitmap>>,
               ::cw::lib::Integer> bitmap_snapshot();
    void cache_surface(::cw::lib::Integer arg_frame_id,
                       std::shared_ptr<::cw::lib::Surface> arg_surface);
    void cache_bitmap(::cw::lib::Integer arg_frame_id,
                      std::shared_ptr<::cw::lib::Bitmap> arg_bitmap);
};

class _WorkerPool : public ::cw::lib::Object, public std::enable_shared_from_this<_WorkerPool> {
public:
    _WorkerPool() = default;
    virtual ~_WorkerPool();
    std::map<::cw::lib::String, ::cw::lib::Integer> m__generation{};
    std::map<::cw::lib::Integer, std::shared_ptr<::cw::animatedimage::_Instance>> m__instances{};
    ::cw::lib::Value m__lock{};
    std::optional<std::shared_ptr<::cw::lib::Thread>> m__monitor_thread{};
    ::cw::lib::Integer m__next_instance_id{};
    std::map<::cw::lib::String, std::set<::cw::lib::Integer>> m__owner_instances{};
    std::map<::cw::lib::String, std::set<::cw::lib::Integer>> m__path_instances{};
    ::cw::lib::Value m__queue{};
    std::map<::cw::lib::String, std::shared_ptr<::cw::animatedimage::_Slot>> m__slots{};
    ::cw::lib::Value m__stop{};
    ::cw::lib::Value m__threads{};
    std::recursive_mutex m_state_mutex;
    void initialize(::cw::lib::Integer arg_workers = 2);
    void _idle_monitor_loop();
    std::optional<std::shared_ptr<::cw::animatedimage::_Slot>> request(::cw::lib::String arg_path, std::optional<::cw::lib::Bytes> arg_data);
    std::optional<std::vector<::cw::lib::Value>> _make_key(::cw::lib::String arg_path, std::optional<::cw::lib::Bytes> arg_data);
    std::optional<std::shared_ptr<::cw::animatedimage::_Slot>> _request_locked(std::vector<::cw::lib::Value> arg_key, ::cw::lib::String arg_path, std::optional<::cw::lib::Bytes> arg_data);
    std::optional<::cw::lib::Integer> acquire(::cw::lib::String arg_path, ::cw::lib::String arg_owner, bool arg_isolated, std::optional<::cw::lib::Bytes> arg_data);
    void release(::cw::lib::String arg_owner, std::optional<::cw::lib::String> arg_path);
    void _release_instance_locked(::cw::lib::Integer arg_instance_id);
    void _drop_path(::cw::lib::String arg_path);
    void stop_idle_slots();
    std::optional<std::shared_ptr<::cw::animatedimage::_Instance>> _get_instance(std::optional<::cw::lib::Integer> arg_instance_id);
    std::tuple<std::optional<std::shared_ptr<::cw::animatedimage::_Slot>>, bool> get_slot_for_update(::cw::lib::String arg_path, std::optional<::cw::lib::Integer> arg_instance_id);
    ::cw::lib::Integer _active_count();
    bool has_active();
    bool has_instance(::cw::lib::String arg_path, ::cw::lib::Integer arg_instance_id);
    bool has_owner_prefix(::cw::lib::String arg_prefix);
    ::cw::lib::Integer owner_prefix_frame_token(::cw::lib::String arg_prefix);
    void _print_status(::cw::lib::String arg_event, ::cw::lib::String arg_path);
    void _worker_loop();
    void _requeue(std::vector<::cw::lib::Value> arg_key, ::cw::lib::Integer arg_generation);
    void shutdown();
};

extern bool g_IDLE_STOP_ENABLED;
extern ::cw::lib::Real g_IDLE_STOP_SECONDS;
extern std::tuple<::cw::lib::String, ::cw::lib::String> g_SUPPORTED_EXTS;
extern ::cw::lib::Value g__STOP;
extern std::optional<std::shared_ptr<::cw::animatedimage::_WorkerPool>> g__pool;

void _WorkerPool__worker_loop_requeue_after_delay(::cw::lib::Real arg_delay, std::vector<::cw::lib::Value> arg_request_key, ::cw::lib::Integer arg_request_generation);
std::shared_ptr<::cw::animatedimage::_WorkerPool> _get_pool();
bool is_supported_path(::cw::lib::String arg_path);
::cw::lib::String get_supported_extension(::cw::lib::String arg_path = "", std::optional<::cw::lib::Bytes> arg_data = std::nullopt);
std::optional<::cw::lib::Integer> acquire(::cw::lib::String arg_path, ::cw::lib::String arg_owner, bool arg_isolated = false, std::optional<::cw::lib::Bytes> arg_data = std::nullopt);
void release(::cw::lib::String arg_owner, std::optional<::cw::lib::String> arg_path = std::nullopt);
bool has_owner_prefix(::cw::lib::String arg_prefix);
bool has_active();
bool has_instance(::cw::lib::String arg_path, ::cw::lib::Integer arg_instance_id);
::cw::lib::Integer owner_prefix_frame_token(::cw::lib::String arg_prefix);
std::tuple<std::optional<std::shared_ptr<::cw::lib::Surface>>, ::cw::lib::Integer> get_surface(::cw::lib::String arg_path, std::optional<::cw::lib::Integer> arg_instance_id = std::nullopt, std::optional<::cw::lib::Bytes> arg_data = std::nullopt);
std::tuple<std::optional<std::shared_ptr<::cw::lib::Bitmap>>, ::cw::lib::Integer> get_wxbmp(::cw::lib::String arg_path, std::optional<::cw::lib::Integer> arg_instance_id = std::nullopt, std::optional<::cw::lib::Bytes> arg_data = std::nullopt);
::cw::lib::Integer get_frame_id(::cw::lib::String arg_path, std::optional<::cw::lib::Integer> arg_instance_id = std::nullopt, std::optional<::cw::lib::Bytes> arg_data = std::nullopt);
void shutdown();
}

