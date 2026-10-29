// UIの実装は一つに統一し、
// 別経路の実装を設けない。
#include "cw/lib/exception_origin.hpp"
#include "cw/lib/runtime_access.hpp"
#include "cw/dialog/select.hpp"
#include "cw/dialog/placement.hpp"
#include "cw/lib/finally_call.hpp"
#include "cw/dialog/partyrecord.hpp"
#include "cw/dialog/create.hpp"
#include "cw/dialog/transfer.hpp"
#include "cw/dialog/edit.hpp"
#include "cw/dialog/etc.hpp"
#include "cw/dialog/message.hpp"
#include "cw/dialog/dialog.hpp"
#include "cw/dialog/progress.hpp"
#include "cw/dialog/charainfo.hpp"
#include "cw/animatedimage.hpp"
#include "cw/binary/cwyado.hpp"
#include "cw/binary/adventurer.hpp"
#include "cw/binary/environment.hpp"
#include "cw/binary/party.hpp"
#include "cw/binary/cwfile.hpp"
#include "cw/binary/util.hpp"
#include "cw/character.hpp"
#include "cw/data.hpp"
#include "cw/debug/charaedit.hpp"
#include "cw/cw.hpp"
#include "cw/dice.hpp"
#include "cw/header.hpp"
#include "cw/image.hpp"
#include "cw/imageretouch.hpp"
#include "cw/lib/encoding.hpp"
#include "cw/lib/python_integer.hpp"
#include "cw/frame.hpp"
#include "cw/xmlcreater.hpp"
#include "cw/setting.hpp"
#include "cw/sprite/card.hpp"
#include "cw/thread.hpp"
#include "cw/util.hpp"
#include "cw/lib/ui_surface.hpp"
#include "cw/yadodb.hpp"
#include "app/debug_control_server.hpp"
#include "platform/storage/storage_mount.hpp"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <functional>
#include <limits>
#include <map>
#include <mutex>
#include <numeric>
#include <iostream>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace cw::dialog::select {
namespace {
using Integer = ::cw::lib::Integer;
using String = ::cw::lib::String;
using Value = ::cw::lib::Value;

// wx のコントロールは互換用 Window にない有効状態・ラベル・選択
// 状態を持つ。 その状態をコントロールの識別情報ごとに保持し、ネイティブコ
// ントロールを接続した SDL/wx アダプターでも反映できるようにする。
std::mutex control_mutex;
std::unordered_map<const ::cw::lib::Window*, bool> control_enabled;
std::unordered_map<const ::cw::lib::Window*, String> control_labels;
std::atomic<Integer> next_control_id{1000};

// Python の update_narrowcondition 内の入れ子判定関数は、narrow/header/debug
// の状態をクロージャーに保持する。 生成された C++ ABI では自由
// 関数になるため、呼び出し中だけ同じ値をこの型付きコンテキストへまとめる。
struct NarrowPredicateContext {
    std::shared_ptr<_YadoObj> yado{};
    std::shared_ptr<::cw::header::PartyHeader> party{};
    std::shared_ptr<::cw::header::AdventurerHeader> player{};
    String narrow{};
    std::set<String> attrs{};
    bool debug{};
    bool int_valid{};
    Integer int_value{};
};
thread_local NarrowPredicateContext narrow_context{};

thread_local PlayerSelect* player_callback_context{};
thread_local PartySelect* party_callback_context{};
thread_local std::function<void()> narrow_callback{};

struct RandomPocket : ::cw::lib::Object {
    std::shared_ptr<::cw::header::AdventurerHeader> header{};
    Integer point{};
};

std::shared_ptr<::cw::lib::Window> make_window(String title = {}, Integer id = 0)
{
    auto result = std::make_shared<::cw::lib::Window>();
    result->title = std::move(title);
    result->id = id == 0 ? next_control_id.fetch_add(1) : id;
    result->visible = true;
    std::lock_guard<std::mutex> guard(control_mutex);
    control_enabled[result.get()] = true;
    control_labels[result.get()] = result->title;
    return result;
}

void set_enabled(const std::shared_ptr<::cw::lib::Window>& control, bool enabled)
{
    if (!control) return;
    // wx.Window.Enable() と同様、変更するのは入力状
    // 態だけとする。 無効な Select ボタンもダイアログ内に残し
    // 、配置・フォーカス順・周囲のボタンバーを変えない。 共通モデル
    // は control_enabled に状態を保持し、ネイティブ
    // バックエンドも EnableWindow() に同じ値を使う。
    std::lock_guard<std::mutex> guard(control_mutex);
    control_enabled[control.get()] = enabled;
}

bool is_enabled(const std::shared_ptr<::cw::lib::Window>& control)
{
    if (!control) return false;
    std::lock_guard<std::mutex> guard(control_mutex);
    const auto iter = control_enabled.find(control.get());
    return iter == control_enabled.end() ? control->visible : iter->second;
}

void set_label(const std::shared_ptr<::cw::lib::Window>& control, String label)
{
    if (!control) return;
    control->title = std::move(label);
    std::lock_guard<std::mutex> guard(control_mutex);
    control_labels[control.get()] = control->title;
}

template <typename T>
std::shared_ptr<T> object_value(const Value& value)
{
    const auto* object = std::get_if<::cw::lib::ObjectPtr>(&value.storage());
    return object && *object ? std::dynamic_pointer_cast<T>(*object) : nullptr;
}

Integer value_integer(const Value& value, Integer fallback = 0)
{
    if (const auto* integer = std::get_if<Integer>(&value.storage())) return *integer;
    if (const auto* real = std::get_if<::cw::lib::Real>(&value.storage())) return static_cast<Integer>(*real);
    if (const auto* boolean = std::get_if<bool>(&value.storage())) return *boolean ? 1 : 0;
    return fallback;
}

bool value_bool(const Value& value, bool fallback = false)
{
    if (const auto* boolean = std::get_if<bool>(&value.storage())) return *boolean;
    if (const auto* integer = std::get_if<Integer>(&value.storage())) return *integer != 0;
    return fallback;
}

String value_string(const Value& value, String fallback = {})
{
    if (const auto* string = std::get_if<String>(&value.storage())) return *string;
    if (const auto* integer = std::get_if<Integer>(&value.storage())) return std::to_string(*integer);
    if (const auto* boolean = std::get_if<bool>(&value.storage())) return *boolean ? "True" : "False";
    return fallback;
}

String current_exception_details(const String& context)
{
    const auto active = ::cw::lib::capture_exception();
    String details = context;
    if (!active) return details;
    try {
        ::cw::lib::rethrow_exception(active);
    } catch (const std::exception& error) {
        details += "\r\n";
        details += error.what();
        details += "\r\n\r\nStack trace:\r\n";
        details += ::cw::thread::current_exception_stacktrace();
    } catch (...) {
        details += "\r\n不明な例外\r\n\r\nStack trace:\r\n";
        details += ::cw::thread::current_exception_stacktrace();
    }
    return details;
}

void report_select_exception(const String& context)
{
    try {
        const auto details = current_exception_details(context);
        if (::cw::g_cwpy && ::cw::g_cwpy->m_backend) {
            ::cw::g_cwpy->m_backend->notify(*::cw::g_cwpy, "YadoSelect内部エラー", details, true);
        } else {
            std::cerr << details << '\n';
        }
    } catch (...) {
        // 例外の報告処理から、ネイティブのコールバック境界を越えて例外を出さない。
    }
}

String lower_ascii(String value)
{
    // Engine の選択画面は、参照元の Python 3.8 の str.lower()
    // に従い、Unicode 12.1 の表と文脈依存の語末シグマ規
    // 則を使う。 汎用の Unicode 15 の lower() は
    // その実行環境が知らない文字も変換し、異なる検索文字列を意図せず一
    // 致させる場合がある。 全選択画面・ホストで同じ固定の規則を使う。
    return ::cw::lib::encoding::python_lower(value);
}

std::shared_ptr<::cw::data::VariantSequence> sequence_value(const Value& value)
{
    return object_value<::cw::data::VariantSequence>(value);
}

Value make_sequence(const std::vector<Value>& values)
{
    auto sequence = std::make_shared<::cw::data::VariantSequence>();
    sequence->m_values = values;
    return Value(std::static_pointer_cast<::cw::lib::Object>(sequence));
}

std::vector<Value> sequence_values(const Value& value)
{
    const auto sequence = sequence_value(value);
    return sequence ? sequence->m_values : std::vector<Value>{};
}

std::vector<String> sequence_strings(const Value& value)
{
    std::vector<String> result;
    for (const auto& item : sequence_values(value)) result.push_back(value_string(item));
    return result;
}

bool path_is_directory(const String& path)
{
    if (path.empty()) return false;
    const auto native = std::filesystem::u8path(path);
    if (::cardwirth::platform::is_mounted_user_path(native)) {
        const auto status = ::cardwirth::platform::stat_mounted_user_path(native);
        return status && status->is_directory();
    }
    std::error_code error;
    return std::filesystem::is_directory(native, error);
}

// shutil.copytree と同様、各部分ツリーをコピーした後にディレクトリー
// のメタデータを復元する。 順序と規則は共通化し、マウントされたストレージでは列挙と
// 時刻操作の基本処理だけを変える。 時刻情報を持たないストレージでは保存できない。
void restore_copied_directory_times(const std::filesystem::path& source,
                                    const std::filesystem::path& destination)
{
    const bool mounted = ::cardwirth::platform::is_mounted_user_path(destination);
    std::vector<std::filesystem::path> children;
    if (mounted) {
        const auto entries = ::cardwirth::platform::list_mounted_user_path(destination, true);
        if (!entries) throw std::runtime_error("複製先のフォルダ一覧を取得できませんでした。");
        for (const auto& entry : *entries) if (entry.stat.is_directory())
            children.push_back(destination / std::filesystem::u8path(entry.name));
    } else {
        for (const auto& entry : std::filesystem::directory_iterator(destination))
            if (entry.is_directory()) children.push_back(entry.path());
    }
    for (const auto& child : children)
        restore_copied_directory_times(source / child.filename(), child);
    if (mounted) {
        const auto status = ::cardwirth::platform::stat_mounted_user_path(source, true);
        if (!status) throw std::runtime_error("複製元のフォルダ情報を取得できませんでした。");
        if ((status->modified_valid || status->modified != 0) &&
            !::cardwirth::platform::set_modified_mounted_user_path(destination, status->modified))
            throw std::runtime_error("複製先のフォルダの更新時刻を設定できませんでした。");
    } else {
        std::filesystem::last_write_time(destination, std::filesystem::last_write_time(source));
    }
}

Integer event_wheel(const std::shared_ptr<::cw::lib::Window>& event)
{
    if (!event) return 0;
    // 共通のイベントアダプターは、ホイールの正負の方向を id に格納する。
    return event->id;
}

Integer event_button(const std::shared_ptr<::cw::lib::Window>& event)
{
    return event ? event->id : 0;
}

bool is_native_button_event(
    const std::shared_ptr<::cw::lib::Window>& event,
    const std::shared_ptr<::cw::lib::Window>& button)
{
    return event && button &&
           (event == button ||
            (event->native_handle != 0 &&
             event->native_handle == button->native_handle));
}

constexpr Integer kLeft = 1;
constexpr Integer kRight = 3;

void select_play_sound(const String& sound)
{
    if (sound.empty()) return;
    if (const auto backend = get_ui_backend())
        backend->play_select_sound(sound);
}

bool select_confirm(const String& title, const String& message)
{
    select_play_sound("signal");
    return ::cw::dialog::message::show_common_modal_message(
               title, message, true).value_or(false);
}

bool select_debug_mode(bool fallback)
{
    return ::cw::g_cwpy ? ::cw::g_cwpy->is_debugmode() : fallback;
}

// Python の Select と同様、CWPy で変更して Frame で更新する。
// 終了済み・再初期化済みの選択画面には遅延更新を送らない。両方の選択画面でこの経路を使う。
template<class Dialog>
bool select_queue_debug_change(Dialog& dialog,
    const std::shared_ptr<::cw::frame::Frame>& frame,
    const std::function<void(::cw::lib::Callback)>& post_game, std::function<bool()> change)
{
    const auto weak_self = dialog.weak_from_this();
    if (dialog.m_finished || weak_self.expired() || !frame || !frame->m_native_adapter ||
        !post_game || !change) return false;
    const std::weak_ptr<int> lifetime = dialog.m_debug_lifetime;
    const std::weak_ptr<::cw::frame::Frame> weak_frame = frame;
    const auto consumed = std::make_shared<std::atomic_bool>(false);
    try { post_game([weak_self, weak_frame, lifetime, consumed, change=std::move(change)](const auto&, const auto&) {
        const auto frame = weak_frame.lock();
        if (!frame || consumed->exchange(true)) return Value{};
        bool value = false;
        String error;
        try { value = change(); }
        catch (const std::exception& e) { error = e.what(); }
        catch (...) { error = "デバッグ設定の変更に失敗しました。"; }
        frame->post_func([weak_self, weak_frame, lifetime, value, error](const auto&, const auto&) {
            const auto self = weak_self.lock();
            if (!self || self->m_finished || lifetime.expired()) return Value{};
            if (error.empty()) {
                self->m_cwpy_debug = select_debug_mode(value);
                self->update_debug();
            } else {
                select_play_sound("error");
                (void)::cw::dialog::message::show_common_modal_message(
                    weak_frame.lock(), "CWPy内部エラー", error, false, true);
            }
            return Value{};
        });
        return Value{};
    }); } catch (...) {
        consumed->store(true); // 送信に失敗した場合は、既にキューへ入ったコピーも無効にする。
        throw;
    }
    return true;
}

template<class Dialog>
void select_toggle_debug(Dialog& dialog)
{
    const auto runtime = ::cw::g_cwpy;
    if (!runtime) return;
    const auto frame = object_value<::cw::frame::Frame>(runtime->m_frame);
    const std::weak_ptr<::cw::thread::CWPy> weak_runtime = runtime;
    dialog.queue_debug_change(frame, [runtime](auto callback) { runtime->exec_func(std::move(callback)); },
        [weak_runtime] {
            const auto runtime = weak_runtime.lock();
            if (!runtime || runtime != ::cw::g_cwpy) throw std::runtime_error("Engineは終了しています。");
            select_play_sound("page");
            const bool value = !runtime->is_debugmode();
            runtime->set_debug(value);
            return value;
        });
}

void select_feedback(::cw::lib::Object* owner, SelectUiKind kind,
                     const String& sound = {}, bool relayout = false)
{
    if (auto* provider = dynamic_cast<::cw::dialog::DialogPresentationProvider*>(owner))
        provider->request_dialog_presentation_refresh();
    const auto backend = get_ui_backend();
    if (!backend) return;
    if (!sound.empty()) backend->play_select_sound(sound);
    backend->refresh_select_dialog(owner, kind, relayout);
}

template <typename Dialog>
void select_run_etc_modal(const std::shared_ptr<Dialog>& dialog)
{
    if (!dialog) return;
    (void)::cw::dialog::show_common_modal_presentation(dialog);
}

template <typename Converter, typename Work>
bool select_run_conversion(const std::shared_ptr<::cw::lib::Window>& parent,
    const String& title, Integer maximum, Converter& converter, Work work)
{
    auto progress=std::make_shared<::cw::dialog::progress::ProgressDialog>();
    progress->initialize(parent,title,{},maximum,0,false);
    return ::cw::dialog::progress::run_modal_task(progress,[&](const auto& update) {
        converter.m_progress_callback=update;
        try {
            work();
        } catch (...) {
            converter.m_progress_callback={};
            throw;
        }
        converter.m_progress_callback={};
    });
}

// すべての進捗・結果の子画面が戻るまで、親の入力を防ぐ。
struct SelectProcessingScope {
    YadoSelect& owner;
    bool previous;
    bool async_handoff{};
    explicit SelectProcessingScope(YadoSelect& value):owner(value),previous(value.m__processing) {
        owner.m__processing=true;owner.request_dialog_presentation_refresh();
    }
    ~SelectProcessingScope(){
        if(!async_handoff)
            owner.m__processing=owner.m__processing && !owner.m_finished && previous;
        owner.request_dialog_presentation_refresh();
    }
    void handoff() noexcept {async_handoff=true;}
};

::cw::lib::Integer wrap_index(::cw::lib::Integer value,
                              ::cw::lib::Integer count)
{
    if (count <= 0) return 0;
    value %= count;
    return value < 0 ? value + count : value;
}
} // namespace

namespace {

String select_msg_format(const String& key, const String& fallback, const String& arg)
{
    auto text = select_msg(key, fallback);
    const auto pos = text.find("%s");
    if (pos != String::npos) text.replace(pos, 2, arg);
    return text;
}

using PortraitImageTuple = std::tuple<std::shared_ptr<::cw::lib::Bitmap>,
                                      std::shared_ptr<::cw::lib::Bitmap>,
                                      std::shared_ptr<::cw::image::ImageInfo>>;

std::vector<PortraitImageTuple> load_adventurer_portrait_images(
    std::shared_ptr<::cw::header::AdventurerHeader> header)
{
    if (!header) return {};
    bool can_loaded_scaledimage = false;
    if (!value_string(header->m_fpath).empty()) {
        auto root_attrs = std::make_shared<::cw::header::GetRootAttribute>();
        // 保存後も一時XMLのパスが残る場合があるため、一時保存
        // 領域と宿の保存先から現在有効なXMLパスを解決する。
        const auto path=::cw::util::get_yadofilepath(value_string(header->m_fpath));
        root_attrs->initialize(path.empty()?value_string(header->m_fpath):path);
        const auto scaled_it = root_attrs->m_attrs.find("scaledimage");
        if (scaled_it != root_attrs->m_attrs.end()) {
            const String& value = scaled_it->second;
            can_loaded_scaledimage = value == "True" || value == "true" || value == "1";
        }
    }
    auto infos = header->get_imgpaths();
    if (::cw::util::is_face_variant_imageinfos(infos))
        infos = ::cw::util::pick_up_basename_image(std::move(infos), true);
    std::vector<PortraitImageTuple> images;
    for (const auto& info : infos) {
        if (!info) continue;
        const String path = info->m_path;
        if (path.empty()) continue;
        const auto [resolved, load_scale] =
            ::cw::util::find_scaledimagepath(path, ::cw::g_UP_SCR, can_loaded_scaledimage, false);
        auto bitmap = ::cw::util::load_wxbmp(resolved, true, {}, {}, std::nullopt, true,
                                             can_loaded_scaledimage, false,
                                             ::cw::lib::Value(load_scale));
        if (!bitmap || bitmap->size.width <= 1 || bitmap->size.height <= 1) continue;
        auto scaled_surface = ::cw::lib::ui::wins_surface(bitmap,
            static_cast<::cw::lib::Real>(load_scale));
        std::shared_ptr<::cw::lib::Bitmap> scaled_bitmap = bitmap;
        if (scaled_surface) {
            scaled_bitmap = std::make_shared<::cw::lib::Bitmap>();
            static_cast<::cw::lib::Surface&>(*scaled_bitmap) = *scaled_surface;
            ::cw::util::copy_animated_wxbmp_info(bitmap, scaled_bitmap);
        }
        images.emplace_back(bitmap, scaled_bitmap, info);
    }
    return images;
}

// YadoSelect の寸法は論理単位の等倍で定義する。 ネイティブ・ホスト内の両バ
// ックエンドで、UP_WIN を正確に一度だけ適用する。 以前の 800/800 は
// 既に二倍の参照ピクセルであり、再度拡縮すると 1600/1600 になってしまう。
constexpr Integer kYadoPresentationWidth = 400;
constexpr Integer kYadoCanvasHeight = 370;
constexpr Integer kYadoControlsHeight = 29;
constexpr Integer kYadoFilterHeight = 21;
constexpr Integer kYadoNavigationSize = 30;
constexpr Integer kYadoActionWidth = 49;
constexpr Integer kYadoActionHeight = 24;
constexpr Integer kYadoActionGap = 4;
constexpr Integer kYadoActionTopOffset = 26;

std::shared_ptr<::cw::lib::Surface> make_yado_surface(
    const Integer width, const Integer height,
    const ::cw::lib::Color colour = {240, 240, 240, 255}) {
    auto result = std::make_shared<::cw::lib::Surface>();
    result->size = {std::max<Integer>(1, width), std::max<Integer>(1, height)};
    result->rgba.assign(static_cast<std::size_t>(result->size.width) *
                            static_cast<std::size_t>(result->size.height) * 4U,
                        0);
    ::cw::lib::ui::fill_rect(
        result, {0, 0, result->size.width, result->size.height}, colour);
    return result;
}

Integer select_scaled_win(const Integer value)
{
    return ::cw::lib::ui::win_i(value);
}

struct YadoLayoutMetrics {
    Integer width{};
    Integer height{};
    Integer controls_height{};
    Integer filter_height{};
    Integer canvas_height{};
    Integer navigation_size{};
    Integer navigation_height{};
    Integer action_width{};
    Integer action_height{};
    Integer action_gap{};
    Integer action_top_offset{};
};

YadoLayoutMetrics yado_layout_metrics(const bool show_filter,
                                      const bool scale_to_window = true)
{
    const auto scale = [scale_to_window](const Integer value) {
        return scale_to_window ? select_scaled_win(value) : value;
    };
    YadoLayoutMetrics layout{};
    layout.width = scale(kYadoPresentationWidth);
    layout.controls_height = scale(kYadoControlsHeight + ::cw::dialog::kDialogFooterExtraHeight);
    layout.filter_height = show_filter ? scale(kYadoFilterHeight) : 0;
    // Python と同様、toppanel は 400×370 を維持する。 任
    // 意の絞り込み行はその外側に追加し、追加コントロールの表示で張り紙を縮めない。
    layout.canvas_height = scale(kYadoCanvasHeight);
    layout.height = layout.canvas_height + layout.controls_height + layout.filter_height;
    layout.navigation_size = scale(kYadoNavigationSize);
    layout.navigation_height = scale(kYadoNavigationSize + ::cw::dialog::kDialogFooterExtraHeight);
    layout.action_width = scale(kYadoActionWidth);
    layout.action_height = scale(kYadoActionHeight + ::cw::dialog::kDialogFooterExtraHeight);
    layout.action_gap = scale(kYadoActionGap);
    layout.action_top_offset = scale(kYadoActionTopOffset + ::cw::dialog::kDialogFooterExtraHeight);
    return layout;
}

::cw::lib::ui::FontStyle select_font(const Integer pixels, const bool bold,
                                     const bool scale_to_window,
                                     const String& type = "dlglist")
{
    auto result = scale_to_window
        ? ::cw::lib::ui::configured_font_scaled(type, pixels, bold)
        : ::cw::lib::ui::configured_font(type, std::max<Integer>(1, pixels), bold);
    // Python の get_wxfont(..., pixelsize=...) は wx3
    // に対して 1px の補正を行う。 共通フォントヘルパーは変えず、同じ見た目の寸法を維持する。
    result.pixels = std::max<Integer>(1, result.pixels - 1);
    return result;
}

void draw_yado_centered(const std::shared_ptr<::cw::lib::Surface>& surface,
                        const String& text, const ::cw::lib::Rect rect,
                        const Integer pixels, const bool bold = false,
                         const std::tuple<Integer, Integer, Integer> colour =
                             {0, 0, 0}, const bool scale_to_window = false,
                        const String& font_type = "dlglist") {
    if (!surface || text.empty()) return;
    const auto style = select_font(pixels, bold, scale_to_window, font_type);
    const auto width = ::cw::lib::ui::label_width(
        text, style.pixels, style.bold, style.face);
    const auto x = rect.x + std::max<Integer>(0, (rect.width - width) / 2);
    const auto y = rect.y + std::max<Integer>(0, (rect.height - style.pixels - 3) / 2);
    ::cw::lib::ui::draw_label(surface, text, x, y, style.pixels,
                              style.bold, colour, style.face);
}

struct YadoCanvasComponents {
    ::cw::lib::Rect title{};
    ::cw::lib::Rect portrait{};
    ::cw::lib::Rect heading{};
    std::array<::cw::lib::Rect, 24> members{};
    ::cw::lib::Rect page{};
};

void yado_hash_append(std::uint64_t& seed, const String& value)
{
    seed ^= static_cast<std::uint64_t>(std::hash<String>{}(value)) +
            0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U);
}

void yado_hash_append(std::uint64_t& seed, const Integer value)
{
    seed ^= static_cast<std::uint64_t>(value) + 0x9e3779b97f4a7c15ULL +
            (seed << 6U) + (seed >> 2U);
}

YadoCanvasComponents yado_canvas_components(const bool scale_to_window)
{
    const auto scale = [scale_to_window](const Integer value) {
        return scale_to_window ? select_scaled_win(value) : value;
    };
    YadoCanvasComponents result{};
    result.title = {scale(5), scale(40), scale(390), scale(26)};
    result.portrait = {scale(0), scale(70), scale(400), scale(105)};
    result.heading = {scale(0), scale(175), scale(400), scale(20)};
    for (std::size_t index = 0; index < result.members.size(); ++index) {
        result.members[index] = {
            scale(65 + static_cast<Integer>(index % 3U) * 95),
            scale(200 + static_cast<Integer>(index / 3U) * 16),
            scale(90), scale(16)};
    }
    result.page = {scale(0), scale(338), scale(400), scale(20)};
    return result;
}

void draw_yado_centered_at(const std::shared_ptr<::cw::lib::Surface>& surface,
                           const String& text, const Integer y,
                           const Integer width, const Integer pixels,
                           const bool scale_to_window,
                           const String& font_type)
{
    if (!surface || text.empty()) return;
    const auto style = select_font(pixels, false, scale_to_window, font_type);
    const auto text_width = ::cw::lib::ui::label_width(
        text, style.pixels, style.bold, style.face);
    ::cw::lib::ui::draw_label(surface, text, (width - text_width) / 2, y,
                              style.pixels, style.bold, {0, 0, 0}, style.face);
}

void draw_yado_adjusted(const std::shared_ptr<::cw::lib::Surface>& surface,
                        const String& text, const ::cw::lib::Rect rect,
                        const Integer pixels, const bool scale_to_window)
{
    if (!surface || text.empty()) return;
    const auto style = select_font(pixels, false, scale_to_window, "dlglist");
    const auto label = ::cw::lib::ui::render_label(
        text, style.pixels, style.bold, {0, 0, 0}, style.face);
    if (!label) return;
    const auto width = std::min(rect.width, label->size.width);
    ::cw::lib::ui::blend_surface(surface, label, rect.x, rect.y, width,
                                 label->size.height);
}

std::shared_ptr<::cw::lib::Bitmap> common_select_button_bitmap(
    const char* key) {
    if (!::cw::g_cwpy || !::cw::g_cwpy->m_rsrc) return nullptr;
    const auto table = object_value<::cw::setting::ResourceTable>(
        ::cw::g_cwpy->m_rsrc->m_buttons);
    if (!table || !table->contains(::cw::lib::Value(key))) return nullptr;
    try {
        return object_value<::cw::lib::Bitmap>(
            table->get_item(::cw::lib::Value(key)));
    } catch (...) {
        return nullptr;
    }
}

// 一件表示と一覧表示で、宿の重ね合わせ顔画像を共通化する。 Python と同様、
// 順序付きの ImageInfo の合成全体を一枚のカードに収める。 そのクリップ
// 範囲を共通 RGBA レンダラーへ直接渡し、半透明のレイヤーを一度だけ合成する。
void draw_yado_portrait(YadoSelect& panel, Integer item,
    const std::shared_ptr<::cw::lib::Surface>& surface, Integer x, Integer y,
    bool scale_to_window)
{
    const auto scale=[scale_to_window](Integer n) {
        return scale_to_window?select_scaled_win(n):n;
    };
    const auto i=static_cast<std::size_t>(item);
    if (item<0 || i>=panel.m_list.size()) return;
    if (i<panel.m_imgpaths_typed.size() && !panel.m_imgpaths_typed[i].empty()) {
        const ::cw::lib::Rect clip{x,y,scale(74),scale(95)};
        for (const auto& info:panel.m_imgpaths_typed[i]) {
            if (!info || info->m_path.empty()) continue;
            const auto path=::cw::lib::encoding::path_to_utf8(
                std::filesystem::u8path(panel.m_list[i])/std::filesystem::u8path(info->m_path));
            const auto bitmap=get_ui_backend()->load_select_bitmap(path,true);
            const auto portrait=scale_to_window?::cw::lib::ui::resource_surface_for_window(bitmap)
                : ::cw::lib::ui::resource_surface_for_logical(bitmap);
            if (!portrait) continue;
            const auto position=info->calc_basecardposition_wx(
                {portrait->size.width,portrait->size.height},!scale_to_window,"Bill","NotCard");
            ::cw::lib::ui::blend_surface(surface,portrait,x+position.x,y+position.y,0,0,clip);
        }
    } else {
        String skin=i<panel.m_skins.size()?panel.m_skins[i]:String{};
        if (skin.empty() && ::cw::g_cwpy && ::cw::g_cwpy->m_rsrc)
            skin=::cw::g_cwpy->m_rsrc->m_skindir;
        const auto requested=skin.empty()?String("Resource/Image/Card/COMMAND0")
            : ::cw::lib::encoding::path_to_utf8(std::filesystem::u8path(skin)/"Resource/Image/Card/COMMAND0");
        const auto path=::cw::util::find_resource(requested,::cw::g_M_IMG);
        const auto bitmap=get_ui_backend()->load_select_bitmap(path.empty()?requested:path,true);
        const auto portrait=scale_to_window?::cw::lib::ui::resource_surface_for_window(bitmap)
            : ::cw::lib::ui::resource_surface_for_logical(bitmap);
        if (portrait) ::cw::lib::ui::blend_surface(surface,portrait,x,y);
    }
}

std::shared_ptr<::cw::lib::Surface> yado_marker(const String& key,
    const String& skin, bool scale_to_window)
{
    std::shared_ptr<::cw::lib::Bitmap> bitmap;
    if (key=="PLAYING_YADO") {
        for (const auto* name:{"PLAYING_YADO","PLAYING"}) {
            const auto requested=::cw::lib::encoding::path_to_utf8(
                std::filesystem::u8path(skin)/"Resource/Image/Dialog"/name);
            const auto path=::cw::util::find_resource(requested,::cw::g_M_IMG);
            if (::cw::util::path_is_file(path)) {
                bitmap=get_ui_backend()->load_select_bitmap(path,true);break;
            }
        }
    }
    if (!bitmap && ::cw::g_cwpy && ::cw::g_cwpy->m_rsrc) {
        const auto table=object_value<::cw::setting::ResourceTable>(::cw::g_cwpy->m_rsrc->m_dialogs);
        if (table) {
            const String fallback=key=="PLAYING_YADO" && !table->contains(Value(key))?"PLAYING":key;
            if (table->contains(Value(fallback)))
                bitmap=object_value<::cw::lib::Bitmap>(table->get_item(Value(fallback)));
        }
    }
    return scale_to_window?::cw::lib::ui::resource_surface_for_window(bitmap)
        : ::cw::lib::ui::resource_surface_for_logical(bitmap);
}

void draw_yado_markers(YadoSelect& panel, Integer item,
    const std::shared_ptr<::cw::lib::Surface>& surface, ::cw::lib::Rect cell,
    bool scale_to_window, bool shortcuts=true, bool playing=true)
{
    const auto scale=[scale_to_window](Integer n) {return scale_to_window?select_scaled_win(n):n;};
    const auto i=static_cast<std::size_t>(item);
    if (item<0 || i>=panel.m_list.size()) return;
    String skin=i<panel.m_skins.size()?panel.m_skins[i]:String{};
    if (skin.empty() && ::cw::g_cwpy && ::cw::g_cwpy->m_rsrc) skin=::cw::g_cwpy->m_rsrc->m_skindir;
    if (shortcuts && i<panel.m_isshortcuts.size() && !panel.m_isshortcuts[i].empty()) {
        if (const auto marker=yado_marker("LINK",skin,scale_to_window))
            ::cw::lib::ui::blend_surface(surface,marker,
                cell.x+(panel.m_views==1?(cell.width-scale(74))/2-scale(3):scale(2)),
                cell.y+scale(panel.m_views==1?135:85));
    }
    if (playing && ::cw::util::exists_mutex(panel.m_list[i])) {
        if (const auto marker=yado_marker("PLAYING_YADO",skin,scale_to_window))
            ::cw::lib::ui::blend_surface(surface,marker,
                cell.x+(cell.width-marker->size.width)/2,cell.y+(cell.height-marker->size.height)/2);
    }
}

std::shared_ptr<::cw::lib::Surface> render_yado_canvas(
    YadoSelect& panel, const Integer width, const Integer height,
    const bool scale_to_window = true) {
    // 通常は Python と同様に宿の張り紙画像を使う。 リソースが欠
    // 落・破損していても、黒地に黒文字を描かずラベルを読めるようにする。
    auto surface = make_yado_surface(width, height, {240, 240, 240, 255});
    if (const auto background = panel._get_draw_bg()) {
        ::cw::lib::ui::blend_surface(surface, background, 0, 0, width, height);
    }

    if (panel.m_views > 1) {
        // Python の MultiViewSelect と同様、一ページ全体を 2×3 の
        // グリッドで表示する。 各セルは安定した一覧位置を持ち、一回のクリックで選択し、ダブル
        // クリックは共通 Presentation ホストが Activate として伝える。
        const Integer columns = 2;
        const Integer rows = std::max<Integer>(1, panel.m_lines);
        const Integer cell_width = width / columns;
        const Integer cell_height = height / rows;
        const Integer page_count = std::max<Integer>(1,
            (static_cast<Integer>(panel.m_list.size()) + panel.m_views - 1) / panel.m_views);
        const Integer page = std::clamp<Integer>(panel.m_index / panel.m_views, 0, page_count - 1);
        const Integer first = page * panel.m_views;
        const auto scale = [scale_to_window](const Integer value) {
            return scale_to_window ? select_scaled_win(value) : value;
        };
        for (Integer slot = 0; slot < panel.m_views; ++slot) {
            const Integer item = first + slot;
            if (item >= static_cast<Integer>(panel.m_list.size())) break;
            const Integer column = slot % columns;
            const Integer row = slot / columns;
            const Integer x = column * cell_width;
            const Integer y = row * cell_height;
            const ::cw::lib::Rect cell{x, y, cell_width, cell_height};
            if (item == panel.m_index) {
                ::cw::lib::ui::draw_sunken_border(surface, cell);
                ::cw::lib::ui::draw_sunken_border(surface,
                    {x + scale(1), y + scale(1), cell_width - scale(2), cell_height - scale(2)});
            }

            const String name = item < static_cast<Integer>(panel.m_names.size())
                ? panel.m_names[static_cast<std::size_t>(item)] : String{};
            draw_yado_centered(surface, name,
                {x + scale(3), y + scale(2), cell_width - scale(6), scale(19)},
                17, false, {0, 0, 0}, scale_to_window, "scenario");

            draw_yado_portrait(panel,item,surface,x+scale(5),y+scale(20),scale_to_window);
            draw_yado_markers(panel,item,surface,cell,scale_to_window,true,false);
            Integer member_y = y + scale(25);
            if (item < static_cast<Integer>(panel.m_classic.size()) &&
                panel.m_classic[static_cast<std::size_t>(item)]) {
                const auto warning = select_font(14, false, scale_to_window, "dlgmsg");
                ::cw::lib::ui::draw_label(surface, "変換が必要です",
                    x + scale(84), y + scale(22), warning.pixels, warning.bold,
                    {192, 0, 0}, warning.face);
                member_y += scale(15);
            }
            if (item < static_cast<Integer>(panel.m_list2.size())) {
                const auto& members = panel.m_list2[static_cast<std::size_t>(item)];
                const Integer maximum = panel.m_classic[static_cast<std::size_t>(item)] ? 5 : 6;
                for (Integer member = 0; member < maximum &&
                     member < static_cast<Integer>(members.size()); ++member) {
                    String label = members[static_cast<std::size_t>(member)];
                    if (member == maximum - 1 && static_cast<Integer>(members.size()) > maximum)
                        label = select_msg("scenario_etc", "その他");
                    draw_yado_adjusted(surface, label,
                        {x + scale(84), member_y, cell_width - scale(86), scale(15)},
                        14, scale_to_window);
                    member_y += scale(15);
                }
            }
        }
        draw_yado_centered(surface,
            std::to_string(page + 1) + "/" + std::to_string(page_count),
            {0, height - scale(18), width, scale(18)}, 13, false,
            {0, 0, 0}, scale_to_window, "dlgtitle");
        // Python と同様、全セルとページを描き終えてから使用中の表示を重ねる。
        for(Integer slot=0;slot<panel.m_views && first+slot<static_cast<Integer>(panel.m_list.size());++slot)
            draw_yado_markers(panel,first+slot,surface,
                {(slot%columns)*cell_width,(slot/columns)*cell_height,cell_width,cell_height},
                scale_to_window,false,true);
        return surface;
    }

    const auto components = yado_canvas_components(scale_to_window);
    const Integer index = panel.m_index;
    const auto scale=[scale_to_window](Integer n) {return scale_to_window?select_scaled_win(n):n;};
    draw_yado_portrait(panel,index,surface,(width-scale(74))/2,scale(70),scale_to_window);
    draw_yado_markers(panel,index,surface,{0,0,width,height},scale_to_window,true,false);
    if (index>=0 && index<static_cast<Integer>(panel.m_classic.size()) && panel.m_classic[static_cast<std::size_t>(index)]) {
        draw_yado_centered(surface,"変換が必要です",{0,scale(20),width,scale(20)},
            16,false,{192,0,0},scale_to_window,"dlgmsg");
    }
    const String name = index >= 0 && index < static_cast<Integer>(panel.m_names.size())
        ? panel.m_names[static_cast<std::size_t>(index)] : String{};
    draw_yado_centered_at(surface, name, components.title.y, width, 21,
                          scale_to_window, "scenario");
    draw_yado_centered_at(surface, select_msg("adventurers", "冒険者"), components.heading.y,
                          width, 15, scale_to_window, "dlgtitle");
    if (index >= 0 && index < static_cast<Integer>(panel.m_list2.size())) {
        const auto& members = panel.m_list2[static_cast<std::size_t>(index)];
        for (std::size_t member = 0;
             member < members.size() && member < components.members.size();
             ++member) {
            String label = members[member];
            if (member == 23U && members.size() > 24U)
                label = select_msg("scenario_etc", "その他");
            draw_yado_adjusted(surface, label, components.members[member], 14,
                               scale_to_window);
        }
    }
    const auto count = std::max<Integer>(1,
        static_cast<Integer>(panel.m_list.size()));
    const String page = std::to_string(std::clamp<Integer>(index + 1, 1, count)) +
                        "/" + std::to_string(count);
    draw_yado_centered_at(surface, page, components.page.y, width, 15,
                          scale_to_window, "dlgtitle");
    draw_yado_markers(panel,index,surface,{0,0,width,height},scale_to_window,false,true);
    return surface;
}

void draw_yado_control(
    ::cw::dialog::DialogPresentation& presentation,
    const std::shared_ptr<::cw::lib::Surface>& surface, const String& label,
    const ::cw::lib::Rect rect,
    const ::cw::dialog::DialogPresentationAction action,
    const int index, const bool enabled, const bool primary = false,
    const std::shared_ptr<::cw::lib::Surface>& image = {},
    const bool paint = true, const Integer logical_icon_height = 14) {
    if (paint) {
      ::cw::lib::ui::draw_button_face(surface, rect, {enabled, false, primary});
    }
    if (paint && image && image->size.width > 0 && image->size.height > 0) {
        auto scaled = ::cw::lib::ui::resource_surface_for_logical(image);
        // Python と同様、LMOVE/RMOVE は論理 9×14、LJUMP/RJUMP
        // は 16×14 の画像として 30×30 のボタン内に描く
        // 。 拡大済みスキン画像のメタデータが不完全な場合もあるため、
        // アイコンの高さを明示的に正規化し、二重に半分へ縮小しない。
        const Integer icon_height = select_scaled_win(logical_icon_height);
        if (scaled && scaled->size.height != icon_height) {
            const Integer icon_width = std::max<Integer>(1,
                static_cast<Integer>(std::lround(
                    static_cast<double>(scaled->size.width) * icon_height /
                    std::max<Integer>(1, scaled->size.height))));
            scaled = ::cw::image::smoothscale_card(
                scaled, {icon_width, icon_height}, true);
        }
        if (scaled) {
            const Integer x = rect.x + (rect.width - scaled->size.width) / 2;
            const Integer y = rect.y + (rect.height - scaled->size.height) / 2;
            ::cw::lib::ui::blend_surface(surface, scaled, x, y);
        }
    } else if (paint) {
        draw_yado_centered(surface, label, rect, 14, false,
                           enabled ? std::tuple<Integer, Integer, Integer>{0, 0, 0}
                                    : std::tuple<Integer, Integer, Integer>{100, 100, 100},
                           true, "button");
    }
    presentation.controls.push_back(
        {action, index, static_cast<float>(rect.x), static_cast<float>(rect.y),
         static_cast<float>(rect.width), static_cast<float>(rect.height), enabled});
    presentation.controls.back().hover_highlight = true;
}

bool show_additional_controls_button()
{
    return !::cw::g_cwpy || !::cw::g_cwpy->m_setting ||
           ::cw::g_cwpy->m_setting->m_show_addctrlbtn;
}

std::shared_ptr<::cw::lib::Surface> select_additional_controls_image(
    const bool expanded)
{
    return ::cw::lib::ui::additional_controls_surface(expanded);
}

constexpr Integer kSelectPresentationCanvasWidth = 460;
constexpr Integer kSelectPresentationCanvasHeight = 280;
constexpr Integer kSelectPresentationFilterHeight = 30;
constexpr Integer kSelectPresentationBottomHeight = 30 + ::cw::dialog::kDialogFooterExtraHeight;

String portable_party_money(String format, const Integer money) {
    const String value = std::to_string(money);
    for (const String token : {String("%d"), String("%s")}) {
        if (const auto position = format.find(token); position != String::npos) {
            format.replace(position, token.size(), value);
            return format;
        }
    }
    return format.empty() ? value : format + " " + value;
}

void draw_select_label(const std::shared_ptr<::cw::lib::Surface>& surface,
                       const String& text, const Integer x, const Integer y,
                       const Integer pixels, const bool bold = false,
                       const std::tuple<Integer,Integer,Integer> colour = {0,0,0}) {
    if (!surface || text.empty()) return;
    // 共通のプレイヤー・パーティー用キャンバスは、wins() で変換済みのピクセルを渡す。
    const auto style = select_font(pixels, bold, false);
    ::cw::lib::ui::draw_label(surface, text, x, y, style.pixels,
                              style.bold, colour, style.face);
}

void draw_select_centered(const std::shared_ptr<::cw::lib::Surface>& surface,
                          const String& text, const Integer center_x,
                          const Integer y, const Integer pixels,
                          const bool bold = false, const Integer maxwidth = 0,
                          const String& font_type = "dlglist") {
    if (!surface || text.empty()) return;
    const auto style = select_font(pixels, bold, false, font_type);
    auto label = ::cw::lib::ui::render_label(text,style.pixels,style.bold,{0,0,0},style.face);
    if (!label) return;
    // Py の draw_adjusted と同様、幅の広い文字列は高さを保って水平方向へ
    // 圧縮する。 wx の高品質ビットマップ拡縮を、共通の CPU 画像処理で置き換える。
    if (maxwidth > 0 && label->size.width > maxwidth)
        label = ::cw::image::smoothscale_card(label, {maxwidth, label->size.height}, true);
    if (!label) return;
    const Integer width = label->size.width;
    ::cw::lib::ui::blend_surface(surface,label,center_x-width/2,y,width,label->size.height);
}

void blend_player_portrait(
    const std::shared_ptr<::cw::lib::Surface>& surface,
    const std::shared_ptr<::cw::header::AdventurerHeader>& header,
    const Integer x, const Integer y) {
    if (!surface || !header) return;
    // Py の PlayerSelect と同様、一件表示でも一覧表示でも顔画像を wins(74)
    // ×wins(94) に切り取る。 共通 CPU レンダラーにはその wx DC 相当
    // のクリップを明示的に渡す。透明な中間画像ではアルファが二重に合成されるため使わない。
    const ::cw::lib::Rect clip{x, y, select_scaled_win(74), select_scaled_win(94)};
    for (const auto& image : load_adventurer_portrait_images(header)) {
        const auto& bitmap = std::get<1>(image);
        const auto& info = std::get<2>(image);
        if (!bitmap || !info) continue;
        const auto base = info->calc_basecardposition_wx(
            {bitmap->size.width, bitmap->size.height}, false,
            "LargeCard", "NotCard");
        ::cw::lib::ui::blend_surface(surface, bitmap, x + base.x, y + base.y, 0, 0, clip);
    }
}

std::shared_ptr<::cw::header::AdventurerHeader> first_party_member(
    const std::shared_ptr<::cw::header::PartyHeader>& header) {
    if (!header || !::cw::g_cwpy || !::cw::g_cwpy->m_ydata ||
        !*::cw::g_cwpy->m_ydata) return {};
    const auto paths = header->get_memberpaths(
        ::cw::g_cwpy->m_yadodir.empty()
            ? std::nullopt
            : std::optional<String>(::cw::g_cwpy->m_yadodir));
    // Py と同様、paths[0] だけを読む。先頭の顔画像が見つからなくても、後の PC を代わりに選ばない。
    if (paths.empty()) return {};
    return (*::cw::g_cwpy->m_ydata)->create_advheader(paths.front(), false, std::nullopt);
}

void draw_player_canvas_portable(
    const std::shared_ptr<::cw::lib::Surface>& surface, PlayerSelect& panel) {
    if (!surface) return;
    const auto scale = [](const Integer value) { return select_scaled_win(value); };
    const Integer canvas_width = surface->size.width;
    const Integer canvas_height = surface->size.height;
    if (const auto background = panel._get_bg())
        ::cw::lib::ui::blend_surface(surface, background, 0, 0,
                                     canvas_width, canvas_height);
    if (panel.m_typed_list.empty()) return;
    const Integer index = std::clamp<Integer>(
        panel.m_index, 0, static_cast<Integer>(panel.m_typed_list.size()) - 1);
    if (panel.m_views == 1) {
        const auto& header = panel.m_typed_list[static_cast<std::size_t>(index)];
        if (!header) return;
        const Integer xpos = scale(8);
        const String level = std::to_string(value_integer(header->m_level, 0));
        const String level_label = select_msg("character_level", "Level");
        const auto label_font = select_font(scale(14), false, false);
        const auto level_font = select_font(scale(25), false, false);
        const Integer label_x = scale(value_integer(header->m_level, 0) < 10 ? 44 : 39) + xpos;
        const Integer level_x = label_x + ::cw::lib::ui::label_width(
            level_label, label_font.pixels, label_font.bold, label_font.face) + scale(5);
        draw_select_label(surface, level_label, label_x, scale(42), scale(14));
        draw_select_label(surface, level, level_x, scale(34), scale(25));
        draw_select_label(surface, select_msg("character_class", "Adventurer"),
                          level_x + ::cw::lib::ui::label_width(level, level_font.pixels,
                              level_font.bold, level_font.face) + scale(5), scale(42), scale(14));
        draw_select_centered(surface, header->m_name,
                             scale(45) + xpos + (scale(140) - scale(5) * 2) / 2,
                             scale(67), scale(22), false,
                             scale(140) - scale(5) * 2, "inputname");
        blend_player_portrait(surface, header, scale(73) + xpos, scale(90));
        draw_select_centered(surface,
            select_msg_format("character_age", "Age:%s", header->get_age()),
            scale(112) + xpos, scale(195), scale(14));
        draw_select_centered(surface,
            select_msg_format("character_sex", "Sex:%s", header->get_sex()),
            scale(112) + xpos, scale(210), scale(14));
        draw_select_centered(surface,
            select_msg_format("character_ep", "EP:%s",
                              std::to_string(value_integer(header->m_ep, 0))),
            scale(112) + xpos, scale(225), scale(14));
        // Python と同様、両見出しと経歴は計測した右側の解説列を
        // 基準に配置し、顔画像付近の固定 x=213 には置かない。
        const auto description_font = select_font(scale(13), false, false, "charadesc");
        const Integer text_left = scale(190);
        const Integer text_right = canvas_width - scale(20);
        const Integer text_width = std::max<Integer>(1, text_right - text_left);
        const Integer measured_width = static_cast<Integer>(::cw::lib::ui::label_width(
            String(36, '#'), description_font.pixels, description_font.bold,
            description_font.face) * 0.85);
        const Integer text_center = text_left + std::min(text_width, measured_width) / 2;
        const auto right_text = [&](const String& text, Integer y, Integer pixels, bool centered) {
            const auto style = select_font(pixels, false, false, "charadesc");
            const auto label = ::cw::lib::ui::render_label(
                text, style.pixels, style.bold, {0, 0, 0}, style.face);
            if (!label) return;
            const Integer available = centered
                ? 2 * std::min(text_center - text_left, text_right - text_center) : text_width;
            const Integer width = std::min(label->size.width, available);
            if (width <= 0) return;
            // 長い称号も右列に収め、PC の画像へ重ねない。
            ::cw::lib::ui::blend_surface(surface, label,
                centered ? text_center - width / 2 : text_left, y, width, label->size.height);
        };
        right_text(select_msg("character_profile", "【Profile】"), scale(42), scale(14), true);
        const String wrapped = ::cw::util::txtwrap(
            value_string(header->m_desc), 4, 0, {}, false, nullptr);
        std::vector<String> description;
        // Python の PROFILE_LINES < len(desc) の条件を維
        // 持する。 五行目は表示し、入力が六行になった場合に五行目を「...」で省略する。
        for (const auto& line : ::cw::lib::encoding::splitlines(wrapped)) {
            if (4 < description.size()) {
                description.back() = ::cw::util::abbr_longstr_with_count(
                    description.back() + "...", 37);
                break;
            }
            description.push_back(line);
        }
        Integer row = 0;
        for (const auto& line : description) {
            right_text(line, scale(63 + row * 14), scale(13), false);
            ++row;
        }
        right_text(select_msg("character_history", "【History】"), scale(148), scale(14), true);
        std::vector<String> history;
        for (const auto& coupon_value : header->m_history) {
            const String coupon = value_string(coupon_value);
            if (coupon.empty() || coupon.rfind("＿", 0) == 0 ||
                coupon.rfind("＠", 0) == 0) continue;
            // Python の hist_num < len(history) の条件に合わせ、五項目を
            // 表示する。 表示対象の称号が六つある場合、五つ目を history_etc に置き換える。
            if (4 < history.size()) {
                history.back() = select_msg("history_etc", "その他");
                break;
            }
            history.push_back(coupon);
        }
        row = 0;
        for (const auto& coupon : history)
            right_text(coupon, scale(169 + row++ * 14), scale(13), true);
        draw_select_centered(surface,
            std::to_string(index + 1) + "/" +
                std::to_string(panel.m_typed_list.size()),
            canvas_width / 2, scale(250), scale(14));
        return;
    }
    const Integer views = std::max<Integer>(1, panel.m_views);
    const Integer columns = std::max<Integer>(1, views / 2);
    const Integer cell_w = canvas_width / columns;
    const Integer cell_h = canvas_height / 2;
    const Integer first = panel.get_page() * views;
    for (Integer i = 0; i < views && first + i <
         static_cast<Integer>(panel.m_typed_list.size()); ++i) {
        const auto& header = panel.m_typed_list[static_cast<std::size_t>(first + i)];
        if (!header) continue;
        const Integer x = (i % columns) * cell_w;
        const Integer y = (i / columns) * cell_h;
        // Py と同様、この一覧表示では wins(72) と拡縮しない 5px のずれを使う。
        const Integer image_x = x + (cell_w - scale(72)) / 2;
        blend_player_portrait(surface, header, image_x, y + 5);
        draw_select_centered(surface, header->m_name, x + cell_w / 2,
                             y + scale(105), scale(14));
        draw_select_label(surface, select_msg("character_level", "Level"),
                          x + cell_w / 2 - scale(20), y + scale(120), scale(14));
        draw_select_label(surface, std::to_string(value_integer(header->m_level, 0)),
                          x + cell_w / 2 + scale(20), y + scale(118), scale(20));
        if (first + i == panel.m_index)
            ::cw::lib::ui::draw_sunken_border(
                surface, {x + 1, y + 1, cell_w - 2, cell_h - 2});
    }
    draw_select_label(surface,
        std::to_string(panel.get_page() + 1) + "/" +
            std::to_string(std::max<Integer>(1, panel.get_pagecount())),
        scale(5), scale(5), scale(14));
}

void draw_party_canvas_portable(
    const std::shared_ptr<::cw::lib::Surface>& surface, PartySelect& panel) {
    if (!surface) return;
    const auto scale = [](const Integer value) { return select_scaled_win(value); };
    const Integer canvas_width = surface->size.width;
    const Integer canvas_height = surface->size.height;
    if (const auto background = panel._get_bg())
        ::cw::lib::ui::blend_surface(surface, background, 0, 0,
                                     canvas_width, canvas_height);
    if (panel.m_typed_list.empty()) return;
    const Integer index = std::clamp<Integer>(
        panel.m_index, 0, static_cast<Integer>(panel.m_typed_list.size()) - 1);
    const auto draw_party = [&](const auto& header, const Integer x,
                                const Integer y, const Integer width,
                                const bool detail) {
        if (!header) return;
        if (detail) {
            draw_select_centered(surface, select_msg("adventurers_team", "Adventurers"),
                                 x + width / 2, y + scale(25), scale(14));
            draw_select_centered(surface, header->m_name, x + width / 2,
                                 y + scale(40), scale(20), true, width-scale(10));
            draw_select_centered(surface,
                portable_party_money(select_msg("adventurers_money", "Money %d"),
                                     header->m_money),
                 x + width / 2, y + scale(60), scale(14));
            const auto members = header->get_membernames();
            for (std::size_t i = 0; i < members.size() && i < 6; ++i) {
                const Integer column = static_cast<Integer>(i % 3);
                const Integer row = static_cast<Integer>(i / 3);
                const Integer count = row == 0 ? std::min<Integer>(3, members.size())
                                              : static_cast<Integer>(members.size()) - 3;
                draw_select_centered(surface, members[i],
                    x + (width - count * scale(90)) / 2 + column * scale(90) + scale(45),
                    y + scale(85 + row * 20), scale(14), false, scale(84));
            }
        }
        // Python の PartySelect と同様、シナリオ・宿の張り紙を表示す
        // る。 右下には先頭メンバーだけを半分の寸法で描き、PC 全体の表示にはしない。
        const auto scenario = header->get_sceheader().value_or(nullptr);
        const auto inn = ::cw::g_cwpy && ::cw::g_cwpy->m_ydata
            ? *::cw::g_cwpy->m_ydata : nullptr;
        auto poster = make_yado_surface(scale(74), scale(94), {0,0,0,0});
        const auto draw_poster = [&](const auto& bitmap, const auto& info) {
            if (!bitmap || !info) return;
            const auto base = info->calc_basecardposition_wx(
                {bitmap->size.width, bitmap->size.height}, false, "Bill", "NotCard");
            ::cw::lib::ui::blend_surface(poster, bitmap, base.x, base.y);
        };
        if (scenario) {
            const auto [images, unscaled, infos] = scenario->get_wxbmps(true);
            for (std::size_t i=0; i<std::min(images.size(),infos.size()); ++i)
                draw_poster(images[i], infos[i]);
        } else if (inn) {
            for (const auto& info : inn->m_imgpaths) {
                if (!info || info->m_path.empty()) continue;
                const auto bitmap = get_ui_backend()->load_select_bitmap(
                    ::cw::util::join_yadodir(info->m_path), true);
                draw_poster(::cw::lib::ui::resource_surface_for_window(bitmap), info);
            }
            if (inn->m_imgpaths.empty()) {
                const String requested=::cw::util::join_paths({::cw::g_cwpy->m_skindir,
                    Value("Resource/Image/Card/COMMAND0")});
                const auto path=::cw::util::find_resource(requested,::cw::g_M_IMG);
                auto info=std::make_shared<::cw::image::ImageInfo>();
                info->initialize(path,0,std::nullopt,"Default",std::nullopt);
                draw_poster(::cw::lib::ui::resource_surface_for_window(
                    get_ui_backend()->load_select_bitmap(path,true)),info);
            }
        }
        const Integer image_y = y + scale(detail ? 125 : 5);
        ::cw::lib::ui::blend_surface(surface,poster,x+(width-scale(74))/2,image_y);
        if (const auto member = first_party_member(header)) {
            auto portrait = make_yado_surface(scale(74),scale(94),{0,0,0,0});
            blend_player_portrait(portrait,member,0,0);
            // Py は合成済みの wx.Image を IMAGE_QUALITY_NORMAL
            // で拡縮する。 固定小数点の丸めが異なる pygame の smoothscale
            // ではなく、共通の wx 互換整数ブロック平均を使う。
            auto bitmap = std::make_shared<::cw::lib::Bitmap>();
            static_cast<::cw::lib::Surface&>(*bitmap) = *portrait;
            auto small = ::cw::lib::rescale_bitmap_normal(bitmap,{scale(74)/2,scale(94)/2});
            ::cw::lib::ui::blend_surface(surface,small,x+width/2,image_y+scale(47));
        }
        if (detail)
            draw_select_centered(surface,scenario ? value_string(scenario->m_name) : inn ? value_string(inn->m_name) : String{},
                                 x+width/2,y+scale(225),scale(14));
        if (!detail) {
            draw_select_centered(surface, header->m_name, x + width / 2,
                                 y + scale(105), scale(14));
            if (scenario)
                draw_select_centered(surface, value_string(scenario->m_name),
                                     x + width / 2, y + scale(120), scale(12));
        }
    };
    if (panel.m_views == 1) {
        draw_party(panel.m_typed_list[static_cast<std::size_t>(index)], 0, 0,
                   canvas_width, true);
        draw_select_centered(surface,
            std::to_string(index + 1) + "/" +
                std::to_string(panel.m_typed_list.size()),
            canvas_width / 2, scale(250), scale(14));
        return;
    }
    const Integer views = std::max<Integer>(2, panel.m_views);
    const Integer columns = std::max<Integer>(1, views / 2);
    const Integer cell_w = canvas_width / columns;
    const Integer cell_h = canvas_height / 2;
    const Integer first = panel.get_page() * views;
    for (Integer i = 0; i < views && first + i <
         static_cast<Integer>(panel.m_typed_list.size()); ++i) {
        const Integer x = (i % columns) * cell_w;
        const Integer y = (i / columns) * cell_h;
        draw_party(panel.m_typed_list[static_cast<std::size_t>(first + i)],
                   x, y, cell_w, false);
        if (first + i == panel.m_index)
            ::cw::lib::ui::draw_sunken_border(
                surface, {x + 1, y + 1, cell_w - 2, cell_h - 2});
    }
    draw_select_label(surface,
        std::to_string(panel.get_page() + 1) + "/" +
            std::to_string(std::max<Integer>(1, panel.get_pagecount())),
        scale(5), scale(5), scale(14));
}

constexpr int select_choice_dismiss=1900000, select_choice_first=1900100;
template<class Panel> constexpr int narrow_choice_id() { return std::is_same_v<Panel,YadoSelect>?1:6; }
template<class Panel> constexpr int sort_choice_id() { return std::is_same_v<Panel,YadoSelect>?2:7; }
template<class Panel> std::vector<String> select_sort_keys() {
    if constexpr(std::is_same_v<Panel,PartySelect>) return {"None","Name","HighestLevel","AverageLevel","Money"};
    if constexpr(std::is_same_v<Panel,YadoSelect>) return {"None","Name","Skin"};
    return {"None","Name","Level"};
}
template<class Panel> std::vector<String> select_choice_labels(bool sorting) {
    if(sorting) {
        std::vector<String> labels{select_msg("sort_no","指定順"),select_msg("sort_name","名前")};
        if constexpr(std::is_same_v<Panel,PartySelect>) {
            labels.push_back(select_msg("highest_level","最高レベル"));
            labels.push_back(select_msg("average_level","平均レベル"));
            labels.push_back(select_msg("money","所持金"));
        } else if constexpr(std::is_same_v<Panel,YadoSelect>) labels.push_back(select_msg("skin","スキン"));
        else labels.push_back(select_msg("sort_level","レベル"));
        return labels;
    }
    std::vector<String> labels{select_msg("all","すべて"),
        std::is_same_v<Panel,PartySelect>?select_msg("narrow_party_name","パーティ名"):select_msg("sort_name","名前")};
    if constexpr(std::is_same_v<Panel,YadoSelect> || std::is_same_v<Panel,PartySelect>) labels.push_back(select_msg("member_name","メンバー名"));
    if constexpr(std::is_same_v<Panel,YadoSelect>) labels.push_back(select_msg("skin","スキン"));
    else {
        labels.push_back(select_msg("description","解説")); labels.push_back(select_msg("history","経歴"));
        labels.push_back(select_msg("character_attribute","特徴")); labels.push_back(select_msg("sort_level","レベル"));
    }
    return labels;
}
template<class Panel> int select_choice_index(const Panel& panel,bool sorting) {
    if(!sorting) return static_cast<int>(value_integer(panel.m_narrow_type));
    const auto keys=select_sort_keys<Panel>();
    const auto found=std::find(keys.begin(),keys.end(),value_string(panel.m_sort,"None"));
    return found==keys.end()?0:static_cast<int>(found-keys.begin());
}
template<class Panel> bool select_is_input(::cw::dialog::DialogPresentationAction action,int index);

template<class Panel> void select_request_narrow(Panel& panel) {
    const bool processing=[&] {
        if constexpr(std::is_same_v<Panel,PartySelect>) return panel.m_processing;
        else return panel.m__processing;
    }();
    if(processing || panel.m_finished) return;
    const std::weak_ptr<Panel> weak=panel.weak_from_this();
    // wx が選択ウィンドウを所有するのと同様、ネイティブの遅延更新モデルも共有所有
    // を必要とする。 所有されていないモデルへ非同期更新を安全に送ることはできない。
    if(weak.expired()) return;
    select_play_sound("page");
    auto& state=panel.m_input;
    if(!state.initialized || !select_is_input<Panel>(state.action,state.index)) {
        state.top_focus=true; state.initialized=false; state.filter.retire();
    }
    state.narrow_update.request([weak] {
        const auto owner=weak.lock();
        if(!owner || owner->m_finished) return;
        // Py と同様、processing は通知の受付時だけ確認する。 受付済
        // みのコールバックでは再確認せず、実行時の最新の検索文字列・種別を使う。
        owner->_on_narrowcondition();
        owner->request_dialog_presentation_refresh();
    });
}

template<class Panel> void apply_select_choice(Panel& panel,bool sorting,int index) {
    if(sorting) {
        if constexpr(std::is_same_v<Panel,PlayerSelect>) {
            panel.m_sort=select_sort_keys<Panel>()[index]; panel.OnSort(nullptr);
        } else { auto event=std::make_shared<::cw::lib::Window>(); event->id=index; panel.OnSort(event); }
    } else { panel.m_narrow_type=Integer{index}; select_request_narrow(panel); }
    panel.request_dialog_presentation_refresh();
}
template<class Panel> void open_select_choice(Panel& panel,int id) {
    panel.m_filter_choice={id,select_choice_index(panel,id==sort_choice_id<Panel>())};
    panel.request_dialog_presentation_refresh();
}
template<class Panel> bool select_choice_action(Panel& panel,::cw::dialog::DialogPresentationAction action,int index) {
    using Action=::cw::dialog::DialogPresentationAction;
    auto& state=panel.m_filter_choice;
    if(!state.kind) return false;
    const bool sorting=state.kind==sort_choice_id<Panel>();
    const int selected=action==Action::Accept?state.cursor:index-select_choice_first;
    const auto count=static_cast<int>(select_choice_labels<Panel>(sorting).size());
    state.kind=0;
    if((action==Action::Accept || action==Action::Auxiliary) && selected>=0 && selected<count)
        apply_select_choice(panel,sorting,selected);
    panel.request_dialog_presentation_refresh();
    return true;
}
template<class Panel> bool select_choice_key(Panel& panel,::cw::dialog::DialogPresentationKey key,::cw::dialog::DialogPresentationModifiers modifiers,bool visible) {
    using Key=::cw::dialog::DialogPresentationKey;
    auto& state=panel.m_filter_choice;
    if(state.kind) {
        const int last=static_cast<int>(select_choice_labels<Panel>(state.kind==sort_choice_id<Panel>()).size())-1;
        if(key==Key::Up || key==Key::Down) state.cursor=std::clamp(state.cursor+(key==Key::Up?-1:1),0,last);
        else if(key==Key::Home) state.cursor=0;
        else if(key==Key::End) state.cursor=last;
        else if(key==Key::Enter || key==Key::Space) select_choice_action(panel,::cw::dialog::DialogPresentationAction::Accept,0);
        panel.request_dialog_presentation_refresh(); return true;
    }
    if(visible && (modifiers & ::cw::dialog::DialogPresentationModifierControl) && key>=Key::Digit1 && key<=Key::Digit9) {
        const int index=static_cast<int>(key)-static_cast<int>(Key::Digit1);
        if(index<static_cast<int>(select_sort_keys<Panel>().size())) apply_select_choice(panel,true,index);
        return true;
    }
    return false;
}
template<class Panel> void select_choice_scroll(Panel& panel,float x,float y,int lines) {
    auto& edit=panel.m_input.filter;
    if(edit.menu.scroll(lines,edit.edit)) {panel.request_dialog_presentation_refresh();return;}
    if(!lines) return;
    auto& state=panel.m_filter_choice;
    if(state.kind) {
        const int last=static_cast<int>(select_choice_labels<Panel>(state.kind==sort_choice_id<Panel>()).size())-1;
        state.cursor=std::clamp(state.cursor+lines,0,last);
        panel.request_dialog_presentation_refresh(); return;
    }
    const auto view=panel.dialog_presentation();
    for(const auto& c:view.controls) if(c.action==::cw::dialog::DialogPresentationAction::Auxiliary &&
        (c.index==narrow_choice_id<Panel>() || c.index==sort_choice_id<Panel>()) &&
        x>=c.x && x<c.x+c.width && y>=c.y && y<c.y+c.height) {
        const bool sorting=c.index==sort_choice_id<Panel>();
        // 公開ヘルパーと同じ実際の選択肢数で範囲を決め、ポップア
        // ップの表示行数をデータ数として扱わない。 この表示側の
        // 一段ずつのホイール移動と、変更時だけの通知は維持する。
        auto combo=std::make_shared<ComboChoiceControl>();
        combo->choices=select_choice_labels<Panel>(sorting);
        combo->selection=select_choice_index(panel,sorting);
        combo->visible=true;
        combo->bounds={static_cast<Integer>(c.x),static_cast<Integer>(c.y),
            static_cast<Integer>(c.width),static_cast<Integer>(c.height)};
        const auto current=combo->selection;
        combo->on_selection_changed=[&panel,sorting,current](Integer next) {
            if(next!=current) apply_select_choice(panel,sorting,static_cast<int>(next));
        };
        auto event=std::make_shared<ComboWheelEvent>();
        event->pointer_x=x; event->pointer_y=y;
        event->rotation=lines<0?1:-1;
        (void)change_combo(std::move(combo),std::move(event));
        return;
    }
    if constexpr(std::is_same_v<Panel,PlayerSelect> || std::is_same_v<Panel,PartySelect>) {
        auto event=std::make_shared<::cw::lib::Window>();
        event->id=lines<0?1:-1;
        panel.OnMouseWheel(event);
    } else panel.dialog_presentation_action(lines<0?::cw::dialog::DialogPresentationAction::Previous: ::cw::dialog::DialogPresentationAction::Next,-1);
}
template<class Panel> bool select_is_input(::cw::dialog::DialogPresentationAction action,int index) {
    using Action=::cw::dialog::DialogPresentationAction;
    if constexpr(std::is_same_v<Panel,YadoSelect>) return action==Action::Auxiliary && index==3;
    return action==Action::Select && index==-100;
}

template<class Panel> auto select_focus_controls(const ::cw::dialog::DialogPresentation& view,bool footer_only=false) {
    using Action=::cw::dialog::DialogPresentationAction;
    std::vector<::cw::dialog::DialogPresentationControl> controls;
    float footer=-1;
    for(const auto& c:view.controls) if(c.action==Action::Cancel) footer=std::max(footer,c.y);
    for(const auto& c:view.controls) {
        const bool footer_button=c.y==footer && c.action!=Action::Previous && c.action!=Action::Next &&
            c.action!=Action::PreviousGroup && c.action!=Action::NextGroup;
        const bool input=select_is_input<Panel>(c.action,c.index);
        const bool choice=c.action==Action::Auxiliary &&
            (c.index==narrow_choice_id<Panel>() || c.index==sort_choice_id<Panel>());
        if(c.enabled && (footer_button || (!footer_only && (input || choice)))) controls.push_back(c);
    }
    return controls;
}

template<class Panel> void select_validate_focus(Panel& panel,const ::cw::dialog::DialogPresentation& view) {
    auto& state=panel.m_input;
    const auto controls=select_focus_controls<Panel>(view);
    if(state.top_focus) return;
    if(state.initialized && std::any_of(controls.begin(),controls.end(),[&](const auto& c){
        return c.action==state.action && c.index==state.index;
    })) return;
    const auto footer=select_focus_controls<Panel>(view,true);
    state.initialized=!footer.empty();
    if(state.initialized) { state.action=footer.front().action; state.index=footer.front().index; }
}

template<class Panel> void select_focus_action(Panel& panel,::cw::dialog::DialogPresentationAction action,int index) {
    using Action=::cw::dialog::DialogPresentationAction;
    auto& state=panel.m_input;
    if(action==Action::Previous || action==Action::Next || action==Action::PreviousGroup ||
       action==Action::NextGroup || action==Action::Activate) return;
    state.top_focus=false;
    // 張り紙をクリックしたら、キーボードの実行対象を主な下部
    // ボタンへ戻す。 一覧のクリックを編入操作に変えない。
    if(action==Action::Select && index>=0) state.initialized=false;
    else {
        state.filter.sync(value_string(panel.m_narrow));
        state.initialized=true; state.action=action; state.index=index;
    }
    // 次の表示更新で、有効かつ表示中のコントロールに照らして検証する
    // 。 ポインターのコールバックごとに張り紙全体を描き直さない。
    panel.request_dialog_presentation_refresh();
}

template<class Panel> bool select_input_busy(const Panel& panel) {
    if constexpr(std::is_same_v<Panel,::cw::dialog::select::PartySelect>) return panel.m_finished || panel.m_processing;
    else return panel.m_finished || panel.m__processing;
}
template<class Panel> bool select_input_visible(const Panel& panel) {
    if constexpr(std::is_same_v<Panel,::cw::dialog::select::YadoSelect>)
        return !::cw::g_cwpy || !::cw::g_cwpy->m_setting || ::cw::g_cwpy->m_setting->m_show_additional_yado;
    else if constexpr(std::is_same_v<Panel,::cw::dialog::select::PlayerSelect>) return !panel.m_isalbum && panel.m_presentation_additionals;
    else return panel.m_presentation_additionals;
}

// Py の Select._additional_controls は隅のボタンと Ctrl+F
// の両方を処理する。 生成されたネイティブのクラスではこの基底クラスが平坦化されてい
// るため、 各プロバイダーで切り替え規則を複製せず、この共通ヘルパーに動作をまとめる。
template<class Panel> void select_toggle_additionals(Panel& panel) {
    if constexpr(std::is_same_v<Panel,::cw::dialog::select::YadoSelect>) {
        if (::cw::g_cwpy && ::cw::g_cwpy->m_setting)
            ::cw::g_cwpy->m_setting->m_show_additional_yado =
                !::cw::g_cwpy->m_setting->m_show_additional_yado;
    } else {
        panel.m_presentation_additionals = !panel.m_presentation_additionals;
        if (::cw::g_cwpy && ::cw::g_cwpy->m_setting) {
            if constexpr(std::is_same_v<Panel,::cw::dialog::select::PartySelect>)
                ::cw::g_cwpy->m_setting->m_show_additional_party = panel.m_presentation_additionals;
            else
                ::cw::g_cwpy->m_setting->m_show_additional_player = panel.m_presentation_additionals;
        }
    }
    select_play_sound("equipment");
    panel.update_narrowcondition();
}

template<class Panel> bool select_additional_controls_key(Panel& panel,
    ::cw::dialog::DialogPresentationKey key,
    ::cw::dialog::DialogPresentationModifiers modifiers) {
    if (key != ::cw::dialog::DialogPresentationKey::F ||
        !(modifiers & ::cw::dialog::DialogPresentationModifierControl)) return false;
    // Py と同様、Album には追加コントロール用のボタンや Ctrl+F の割り当てを設けない。
    if constexpr(std::is_same_v<Panel,::cw::dialog::select::PlayerSelect>)
        if (panel.m_isalbum) return false;
    constexpr int button = std::is_same_v<Panel,::cw::dialog::select::YadoSelect> ? 4 : 5;
    panel.dialog_presentation_action(::cw::dialog::DialogPresentationAction::Auxiliary, button);
    return true;
}
template<class Panel> void select_publish_input(Panel& panel) {
    const auto& text=panel.m_input.filter.edit.text;
    if(value_string(panel.m_narrow)!=text) {panel.m_narrow=text;select_request_narrow(panel);}
    panel.request_dialog_presentation_refresh();
}
template<class Panel> bool select_priority_input_key(Panel& panel,::cw::dialog::DialogPresentationKey key,
                                                    ::cw::dialog::DialogPresentationModifiers mods) {
    auto& input=panel.m_input.filter;
    if(!select_input_visible(panel)) input.retire();
    if(input.menu.opened() || !input.edit.composition.empty()) {
        input.key(key,mods);select_publish_input(panel);return true;
    }
    return false;
}
template<class Panel> bool select_input_action(Panel& panel,::cw::dialog::DialogPresentationAction action,int index) {
    auto& input=panel.m_input.filter;
    if(select_input_busy(panel) || !select_input_visible(panel)) input.retire();
    if(input.menu.action(action,index,input.edit,input.read,input.write)) {select_publish_input(panel);return true;}
    if(::cw::dialog::textedit::FilterInput::menu_control(action,index)) return true;
    if(!input.edit.composition.empty()) {
        if(action==::cw::dialog::DialogPresentationAction::Cancel) {input.retire();panel.request_dialog_presentation_refresh();return true;}
        if(action==::cw::dialog::DialogPresentationAction::Accept || action==::cw::dialog::DialogPresentationAction::Activate) return true;
    }
    if(!select_is_input<Panel>(action,index)) input.retire();
    return false;
}
template<class Panel> void select_input_text(Panel& panel,const String& text,bool composing,int start=-1,int length=0) {
    if(select_input_busy(panel) || !select_input_visible(panel) || panel.m_filter_choice.kind) return;
    const auto view=panel.dialog_presentation();
    if(!view.wants_text_input) return;
    if(panel.m_input.filter.text(text,composing,start,length)) select_publish_input(panel);
}
template<class Panel> void select_input_pointer(Panel& panel,::cw::dialog::DialogPresentationAction action,int index,float x,float y,bool pressed) {
    if(select_input_busy(panel) || !select_input_visible(panel) || panel.m_filter_choice.kind ||
       !select_is_input<Panel>(action,index)) return;
    auto& state=panel.m_input;
    state.filter.sync(value_string(panel.m_narrow));
    if(state.filter.pointer(x,y,pressed)) {
        state.initialized=true;state.top_focus=false;state.action=action;state.index=index;
        panel.request_dialog_presentation_refresh();
    }
}
template<class Panel> void select_input_context(Panel& panel,float x,float y) {
    if(select_input_busy(panel)) return;
    auto& state=panel.m_input;
    if(state.filter.menu.opened()) {state.filter.menu.close();panel.request_dialog_presentation_refresh();return;}
    if(panel.m_filter_choice.kind) {panel.m_filter_choice.kind=0;panel.request_dialog_presentation_refresh();return;}
    (void)panel.dialog_presentation();
    if(select_input_visible(panel) && state.filter.contains(x,y)) {
        if(!state.filter.edit.composition.empty()) return;
        state.initialized=true;state.top_focus=false;
        if constexpr(std::is_same_v<Panel,::cw::dialog::select::YadoSelect>) {
            state.action=::cw::dialog::DialogPresentationAction::Auxiliary;state.index=3;
        } else {state.action=::cw::dialog::DialogPresentationAction::Select;state.index=-100;}
        state.filter.open(x,y);
        panel.request_dialog_presentation_refresh();
    } else panel.dialog_presentation_action(::cw::dialog::DialogPresentationAction::Cancel,-1);
}

template<class Panel> bool select_focus_key(Panel& panel,::cw::dialog::DialogPresentationKey key,
                                          ::cw::dialog::DialogPresentationModifiers modifiers) {
    using Key=::cw::dialog::DialogPresentationKey;
    using Action=::cw::dialog::DialogPresentationAction;
    const bool ctrl=(modifiers & ::cw::dialog::DialogPresentationModifierControl)!=0;
    const bool shift=(modifiers & ::cw::dialog::DialogPresentationModifierShift)!=0;
    const bool alt=(modifiers & ::cw::dialog::DialogPresentationModifierAlt)!=0;
    auto& state=panel.m_input;
    if(state.filter.menu.opened()) {
        state.filter.key(key,modifiers);select_publish_input(panel);return true;
    }
    // Ctrl によるページ移動・デバッグのショートカットは、引き続き選択モデルが管理する。
    const auto view=panel.dialog_presentation();
    const bool editing=view.wants_text_input;
    panel.request_dialog_presentation_refresh();
    if(editing) {
        if(state.filter.key(key,modifiers)) {select_publish_input(panel);return true;}
        if(!ctrl && !alt && (key==Key::Enter || key==Key::Space)) return true;
    }
    if((key==Key::Tab && !ctrl && !alt) || (!editing && !ctrl && !alt && (key==Key::Left || key==Key::Right))) {
        const auto controls=select_focus_controls<Panel>(view,key!=Key::Tab);
        if(controls.empty()) return true;
        const auto found=std::find_if(controls.begin(),controls.end(),[&](const auto& c){return c.action==state.action && c.index==state.index;});
        const bool previous=key==Key::Left || (key==Key::Tab && shift);
        const auto next=found==controls.end()?(previous?controls.size()-1:0):
            (static_cast<std::size_t>(found-controls.begin())+controls.size()+(previous?-1:1))%controls.size();
        const auto& c=controls[next];
        state.action=c.action; state.index=c.index; state.initialized=true; state.top_focus=false;
        if(!select_is_input<Panel>(c.action,c.index)) state.filter.retire();
        return true;
    }
    if(!ctrl && !alt && (key==Key::Enter || key==Key::Space)) {
        if(state.initialized) panel.dialog_presentation_action(state.action,state.index);
        return true;
    }
    if(!editing && (key==Key::Up || key==Key::Down) && state.action==Action::Auxiliary &&
        (state.index==narrow_choice_id<Panel>() || state.index==sort_choice_id<Panel>())) {
        open_select_choice(panel,state.index); return true;
    }
    return false;
}

template<class Panel> void draw_select_focus(::cw::dialog::DialogPresentation& result,
    const std::shared_ptr<::cw::lib::Surface>& surface,Panel& panel,const bool paint=true) {
    using Action=::cw::dialog::DialogPresentationAction;
    select_validate_focus(panel,result);
    const auto& state=panel.m_input;
    result.wants_text_input=false;
    for(const auto& c:select_focus_controls<Panel>(result)) if(state.initialized && c.action==state.action && c.index==state.index) {
        result.wants_text_input=select_is_input<Panel>(c.action,c.index);
        if (paint) {
            const Integer x=static_cast<Integer>(c.x)+2,y=static_cast<Integer>(c.y)+2;
            const Integer w=static_cast<Integer>(c.width)-4,h=static_cast<Integer>(c.height)-4;
            const ::cw::lib::Color color{45,110,190,255};
            ::cw::lib::ui::fill_rect(surface,{x,y,w,1},color);
            ::cw::lib::ui::fill_rect(surface,{x,y+h-1,w,1},color);
            ::cw::lib::ui::fill_rect(surface,{x,y,1,h},color);
            ::cw::lib::ui::fill_rect(surface,{x+w-1,y,1,h},color);
        }
    }
    for(auto& c:result.controls) if(select_is_input<Panel>(c.action,c.index)) {
        c.continuous_pointer=true;
        if(select_input_busy(panel)) c.enabled=false;
    }
    panel.m_input.filter.finish_frame(result,surface,result.wants_text_input && !panel.m_filter_choice.kind,
                                    !select_input_busy(panel));
}

template<class Panel> void draw_select_input(const std::shared_ptr<::cw::lib::Surface>& surface,
    Panel& panel,const ::cw::lib::Rect& rect,Integer /*font_size*/,const bool paint=true) {
    auto& state=panel.m_input;
    state.filter.sync(value_string(panel.m_narrow));
    state.filter.draw(surface,rect,state.initialized && select_is_input<Panel>(state.action,state.index),
                      static_cast<int>(select_scaled_win(3)),"paneltitle2",15,false,true,paint);
}

template<class Panel> void draw_select_choice_popup(::cw::dialog::DialogPresentation& result,const std::shared_ptr<::cw::lib::Surface>& surface,Panel& panel,const bool paint=true) {
    auto& state=panel.m_filter_choice;
    if(!state.kind) {
        panel.dialog_retained_surface().set_component_hits("dropdown.rows",{});
        return;
    }
    const auto choices=select_choice_labels<Panel>(state.kind==sort_choice_id<Panel>());
    const auto source=std::find_if(result.controls.begin(),result.controls.end(),[&](const auto& c){return c.action==::cw::dialog::DialogPresentationAction::Auxiliary && c.index==state.kind;});
    if(source==result.controls.end()) { state.kind=0; return; }
    const Integer row=select_scaled_win(24), width=select_scaled_win(160), height=row*choices.size();
    const Integer x=std::clamp<Integer>(source->x,0,std::max<Integer>(0,surface->size.width-width));
    Integer y=source->y+source->height;
    if(y+height>surface->size.height) y=static_cast<Integer>(source->y)-height;
    y=std::max<Integer>(0,y);
    result.wants_text_input=false;
    result.controls.push_back({::cw::dialog::DialogPresentationAction::Auxiliary,select_choice_dismiss,0,0,
        static_cast<float>(surface->size.width),static_cast<float>(surface->size.height),true});
    state.cursor=std::clamp(state.cursor,0,static_cast<int>(choices.size())-1);
    const auto selection=::cw::lib::ui::system_selection_colours();
    std::vector<::cw::lib::ui::RetainedSurface::ComponentHitRegion> popup_hits;
    for(std::size_t i=0;i<choices.size();++i) {
        const ::cw::lib::Rect r{x,y+static_cast<Integer>(i)*row,width,row};
        if (paint) {
            const bool selected=state.cursor==static_cast<int>(i);
            ::cw::lib::ui::fill_rect(surface,r,selected?selection.background: ::cw::lib::Color{250,250,250,255});
            const auto ink=selected ? selection.text : ::cw::lib::Color{0,0,0,255};
            draw_select_label(surface,choices[i],x+select_scaled_win(5),r.y+select_scaled_win(5),select_scaled_win(13),false,{ink.r,ink.g,ink.b});
        }
        result.controls.push_back({::cw::dialog::DialogPresentationAction::Auxiliary,select_choice_first+static_cast<int>(i),
            static_cast<float>(x),static_cast<float>(r.y),static_cast<float>(width),static_cast<float>(row),true,false,choices[i],choices[i]});
        popup_hits.push_back({r,static_cast<Integer>(i),true});
    }
    if (paint) ::cw::lib::ui::draw_sunken_border(surface,{x,y,width,height});
    panel.define_dialog_component("dropdown.popup",{x,y,width,height});
    panel.dialog_retained_surface().set_component_hits(
        "dropdown.rows",std::move(popup_hits));
}

template<class Panel> bool select_choice_hover(Panel& panel,float x,float y) {
    if(!panel.m_filter_choice.kind) return false;
    const auto row=panel.dialog_retained_surface().component_hit("dropdown.rows",x,y);
    if(!row || panel.m_filter_choice.cursor==*row) return static_cast<bool>(row);
    panel.m_filter_choice.cursor=static_cast<int>(*row);
    panel.invalidate_dialog_component("dropdown.popup");
    panel.request_dialog_presentation_refresh();
    return true;
}

template <typename Panel>
void draw_select_filter(::cw::dialog::DialogPresentation& presentation,
                         const std::shared_ptr<::cw::lib::Surface>& surface,
                         Panel& panel, const Integer y,const bool paint=true) {
    const auto scale = [](const Integer value) { return select_scaled_win(value); };
    if(paint)draw_select_label(surface, select_msg("narrow_condition", "絞込条件:"),
                              scale(3), y + scale(7), scale(13));
    const ::cw::lib::Rect input{scale(65), y + scale(3), scale(158), scale(24)};
    if(paint){::cw::lib::ui::fill_rect(surface,input,{255,255,255,255});
        ::cw::lib::ui::draw_sunken_border(surface,input);}
    draw_select_input(surface,panel,input,scale(13),paint);
    presentation.controls.push_back(
        {::cw::dialog::DialogPresentationAction::Select, -100,
         static_cast<float>(input.x), static_cast<float>(input.y),
         static_cast<float>(input.width), static_cast<float>(input.height), true});
    presentation.wants_text_input = true;
    presentation.input_text = value_string(panel.m_narrow);
    const auto narrow=select_choice_labels<Panel>(false), sorts=select_choice_labels<Panel>(true);
    draw_yado_control(presentation, surface,
        narrow[std::clamp(select_choice_index(panel,false),0,static_cast<int>(narrow.size())-1)]+" ▾",
        {scale(226), y + scale(3), scale(104), scale(24)},
        ::cw::dialog::DialogPresentationAction::Auxiliary,6,true,false,{},paint);
    draw_yado_control(presentation, surface,
        sorts[select_choice_index(panel,true)]+" ▾",
        {scale(351), y + scale(3), scale(105), scale(24)},
        ::cw::dialog::DialogPresentationAction::Auxiliary,7,true,false,{},paint);
}

void draw_select_bottom(
    ::cw::dialog::DialogPresentation& presentation,
    const std::shared_ptr<::cw::lib::Surface>& surface,
    const std::vector<std::tuple<String, ::cw::dialog::DialogPresentationAction,
                                 int, bool>>& buttons,
    const Integer y, const bool navigation_enabled = true,const bool paint=true) {
    const Integer width = surface ? surface->size.width : select_scaled_win(460);
    const Integer nav = select_scaled_win(30);
    const Integer nav_h = select_scaled_win(30 + ::cw::dialog::kDialogFooterExtraHeight);
    draw_yado_control(presentation, surface, "", {0, y, nav, nav_h},
                      ::cw::dialog::DialogPresentationAction::PreviousGroup, 0, navigation_enabled,
                      false,common_select_button_bitmap("LJUMP"),paint);
    draw_yado_control(presentation, surface, "", {nav, y, nav, nav_h},
                      ::cw::dialog::DialogPresentationAction::Previous, 0, navigation_enabled,
                      false,common_select_button_bitmap("LMOVE"),paint);
    draw_yado_control(presentation, surface, "",
                       {width - nav * 2, y, nav, nav_h},
                      ::cw::dialog::DialogPresentationAction::Next, 0, navigation_enabled,
                      false,common_select_button_bitmap("RMOVE"),paint);
    draw_yado_control(presentation, surface, "",
                       {width - nav, y, nav, nav_h},
                      ::cw::dialog::DialogPresentationAction::NextGroup, 0, navigation_enabled,
                      false,common_select_button_bitmap("RJUMP"),paint);
    if (buttons.empty()) return;
    const Integer gap = select_scaled_win(4);
    const Integer available = width - nav * 4;
    const Integer button_width = (available - gap * (static_cast<Integer>(buttons.size()) - 1)) /
                           static_cast<Integer>(buttons.size());
    Integer x = nav * 2;
    for (const auto& [label, action, index, enabled] : buttons) {
        draw_yado_control(presentation, surface, label,
                          {x, y + select_scaled_win(3), button_width,
                           select_scaled_win(24 + ::cw::dialog::kDialogFooterExtraHeight)},
                          action,index,enabled,false,{},paint);
        x += button_width + gap;
    }
}
}  // namespace

void SelectUiBackend::bind_select_dialog(::cw::lib::Object* arg_dialog, SelectUiKind arg_kind)
{
    (void)arg_dialog;
    (void)arg_kind;
}

void SelectUiBackend::layout_select_dialog(::cw::lib::Object* arg_dialog, SelectUiKind arg_kind)
{
    (void)arg_dialog;
    (void)arg_kind;
}

void SelectUiBackend::destroy_select_dialog(::cw::lib::Object* arg_dialog)
{
    (void)arg_dialog;
}

void SelectUiBackend::refresh_select_dialog(::cw::lib::Object* arg_dialog, SelectUiKind arg_kind, bool arg_layout)
{
    (void)arg_dialog;
    (void)arg_kind;
    (void)arg_layout;
}

void SelectUiBackend::play_select_sound(::cw::lib::String arg_name)
{
    if (::cw::g_cwpy && !arg_name.empty())
        ::cw::g_cwpy->play_sound(std::move(arg_name), false, 100, 1, 0, 0, false);
}

std::shared_ptr<::cw::lib::Bitmap> SelectUiBackend::load_select_bitmap(::cw::lib::String arg_path, bool arg_mask)
{
    if (arg_path.empty()) return {};
    return ::cw::util::load_wxbmp(std::move(arg_path), arg_mask, {}, Value{}, std::nullopt,
                                  false, true, false, std::nullopt);
}

void SelectUiBackend::open_party_editor(PartySelect* arg_dialog) { (void)arg_dialog; }
void SelectUiBackend::open_party_members(PartySelect* arg_dialog) { (void)arg_dialog; }
void SelectUiBackend::open_player_information(PlayerSelect* arg_dialog) { (void)arg_dialog; }

namespace {
class DefaultSelectUiBackend final : public SelectUiBackend {
public:
    void bind_select_dialog(::cw::lib::Object* dialog, SelectUiKind kind) override
    {
        // 選択モデルが完全な DialogPresentation を所有する。 既定
        // バックエンドは共通サービスだけを供給し、OS のウィジェットは生成しない。
        (void)dialog;
        (void)kind;
    }
    void layout_select_dialog(::cw::lib::Object* dialog, SelectUiKind kind) override
    {
        (void)dialog;
        (void)kind;
    }
    void destroy_select_dialog(::cw::lib::Object* dialog) override
    {
        (void)dialog;
    }
    void refresh_select_dialog(::cw::lib::Object* dialog, SelectUiKind kind, bool relayout) override
    {
        (void)dialog;
        (void)kind;
        (void)relayout;
    }
    void open_party_editor(PartySelect* dialog) override
    {
        if (!dialog || dialog->m_typed_list.empty()) return;
        const auto index = std::clamp<Integer>(dialog->m_index, 0, static_cast<Integer>(dialog->m_typed_list.size()) - 1);
        const auto header = dialog->m_typed_list[static_cast<std::size_t>(index)];
        if (!header) return;
        // Python と同様、毎回 Party(header, True) で外部の XML を
        // 読み直す。 Party::_init だけでは、キャッシュ済みの未保存の荷物袋が残る。
        auto party = std::make_shared<::cw::data::Party>();
        party->initialize(header, true);
        header->m_data = party;
#if defined(CARDWIRTH_LOG_ENABLED)
        ::cardwirth::debug::log("PartySelect information open: index=" + std::to_string(index) + " name=" + header->m_name);
#endif
        auto child = std::make_shared<::cw::dialog::edit::PartyEditor>();
        child->m_on_accept = [weak=dialog->weak_from_this(), header, party] {
            const auto dialog=weak.lock();
            if(!dialog || dialog->m_finished) return;
            if (::cw::g_cwpy && ::cw::g_cwpy->m_ydata &&
                *::cw::g_cwpy->m_ydata && party) {
                const auto ydata = *::cw::g_cwpy->m_ydata;
                const auto tree=object_value<::cw::data::CWPyElementTree>(party->m_data);
                if(!tree) return;
                const auto property=tree->find("Property");
                if(!property || !*property ||
                    std::find(ydata->m_partys.begin(),ydata->m_partys.end(),header)==ydata->m_partys.end()) return;
                // Python と同様、ディスク上の古い XML ではなく、編集直後の Property
                // から再構築する。 キャンセルではパーティーヘッダーを更新・置換しない。
                const auto replacement = ydata->create_partyheader(value_string(header->m_fpath),property);
                if (replacement) {
                    replacement->m_data = party;
                    const auto iter = std::find(ydata->m_partys.begin(),
                                                ydata->m_partys.end(), header);
                    if (iter != ydata->m_partys.end()) {
                        *iter = replacement;
                        dialog->m_source_list = ydata->m_partys;
                    }
                    // ヘッダーを置き換えても、編集したパーティーの
                    // 同一性を保持する。 続く並べ替えで、以前の位
                    // 置に偶然入った別の項目を選ばないようにする。
                    const auto visible = std::find(dialog->m_typed_list.begin(),
                                                   dialog->m_typed_list.end(), header);
                    if (visible != dialog->m_typed_list.end()) *visible = replacement;
                    dialog->update_narrowcondition();
                }
            }
        };
        child->initialize(dialog->m_toppanel_window, party);
        // PartyEditor が唯一の共通コントロール構成を所有する。 全製品
        // で、完了後は共通モーダルホストが PartySelect を復元する。
        (void)::cw::dialog::show_common_modal_presentation(child);
        dialog->draw(true);
#if defined(CARDWIRTH_LOG_ENABLED)
        ::cardwirth::debug::log("PartySelect information common edit closed");
#endif
    }
    void open_party_members(PartySelect* dialog) override
    {
        if (!dialog || dialog->m_typed_list.empty()) return;
        const auto index = std::clamp<Integer>(dialog->m_index, 0, static_cast<Integer>(dialog->m_typed_list.size()) - 1);
        const auto header = dialog->m_typed_list[static_cast<std::size_t>(index)];
        if (!header) return;
        ::cw::dialog::charainfo::show_standby_party_charainfo_modal(
            dialog->m_toppanel_window, header,
            [dialog, header](const ::cw::lib::Arguments&, const ::cw::lib::KeywordArguments&) -> Value {
                dialog->update_standbys(header);
                dialog->draw(true);
                return {};
            });
    }
    void open_player_information(PlayerSelect* dialog) override
    {
        if (!dialog || dialog->m_finished || dialog->m__processing) return;
        const auto selected = dialog->get_selected();
        if (!selected || !*selected) return;
        const auto index = dialog->m_index;
        Value character;
        try {
            character = ::cw::dialog::charainfo::read_adventurer_character(*selected);
        } catch (const std::exception& error) {
            select_play_sound("error");
            (void)::cw::dialog::message::show_common_modal_message(
                select_msg("error", "エラー"), error.what(), false, true);
            return;
        }
        ::cw::dialog::charainfo::show_standby_charainfo_modal(
            dialog->m_toppanel_window, dialog->m_typed_list, character, index,
            dialog->information_refresh_callback(), false, std::nullopt,
            dialog->information_selection_callback());
    }
};
}

std::shared_ptr<SelectUiBackend> get_ui_backend()
{
    static const auto backend = std::make_shared<DefaultSelectUiBackend>();
    return backend;
}

void MultiViewSelect::initialize(std::shared_ptr<::cw::lib::Window> arg_parent, ::cw::lib::String arg_title, ::cw::lib::Integer arg_enterid, ::cw::lib::Integer arg_views, bool arg_show_multi, ::cw::lib::Integer arg_lines)
{
    m_native_handle = arg_parent ? arg_parent->native_handle : 0;
    m_native_parent_handle = m_native_handle;
    m_toppanel = make_window(std::move(arg_title));
    if (arg_parent) m_toppanel->bounds = arg_parent->bounds;
    m_buttonlist.clear();
    m_clickmode = 0;
    m_downbutton = -1;
    m_cwpy_debug = false;
    m_reserved_narrowcondition = false;
    m_enterid_typed = arg_enterid;
    m_max_views_typed = std::max<::cw::lib::Integer>(1, arg_views);
    m_lines_typed = std::max<::cw::lib::Integer>(1, arg_lines);
    m_views = arg_show_multi ? m_max_views_typed : 1;
    m__enterid = arg_enterid;
    m__lines = m_lines_typed;
    m__views = m_views;
    m_index = 0;
    m_list.clear();
    m__processing = false;
    m_decision_pending = false;
    m_finished = false;
    m_modal_result = -1;
    if (const auto backend = get_ui_backend()) backend->bind_select_dialog(this, SelectUiKind::multi_view);
}

bool MultiViewSelect::can_clickside()
{
    return m_views <= 1;
}

bool MultiViewSelect::can_clickcenter()
{
    // Py と同様、Select の既定動作を継承し、派生した選択画面で上書きできるようにする。
    return true;
}

void MultiViewSelect::OnLeftDClick(std::shared_ptr<::cw::lib::Window> arg_event)
{
    if (m__processing) return;
    if (m_views > 1 && !m_list.empty() && can_clickcenter() && m_clickmode == 0) {
        m_decision_pending = true;
        m_finished = true;
        m_modal_result = m_enterid_typed;
    } else if (can_clickside() && m_clickmode == kLeft) {
        OnClickLeftBtn(arg_event);
    } else if (can_clickside() && m_clickmode == kRight) {
        OnClickRightBtn(arg_event);
    }
}

void MultiViewSelect::OnMouseWheel(std::shared_ptr<::cw::lib::Window> arg_event)
{
    if (m__processing || m_list.size() <= 1) return;
    const Integer count = static_cast<Integer>(m_list.size());
    const Integer step = count <= m_views ? 1 : std::max<Integer>(1, m_views);
    const Integer domain = count <= m_views ? count : get_pagecount() * m_views;
    const Integer delta = event_wheel(arg_event) > 0 ? -step : step;
    m_index = wrap_index(m_index + delta, domain);
    if (m_index >= count) m_index = count - 1;
    select_feedback(this, SelectUiKind::multi_view, "page");
}

void MultiViewSelect::OnClickLeftBtn(std::shared_ptr<::cw::lib::Window> arg_evt)
{
    if (m__processing || m_list.size() <= 1) return;
    const auto count = static_cast<::cw::lib::Integer>(m_list.size());
    const bool real_button = arg_evt && m_buttonlist.size() > 1 &&
        is_native_button_event(arg_evt, m_buttonlist[1]);
    const auto step = real_button && m_views > 1 && count > m_views ? m_views : 1;
    const auto domain = (step == 1) ? count : get_pagecount() * m_views;
    m_index = wrap_index(m_index - step, domain);
    if (m_index >= count) m_index = count - 1;
    m__views = m_views;
    select_feedback(this, SelectUiKind::multi_view, "page");
}

void MultiViewSelect::OnClickLeft2Btn(std::shared_ptr<::cw::lib::Window> arg_evt)
{
    if (m__processing || m_list.size() <= 1) return;
    const auto count = static_cast<::cw::lib::Integer>(m_list.size());
    const bool real_button = arg_evt && !m_buttonlist.empty() &&
        is_native_button_event(arg_evt, m_buttonlist[0]);
    if (!real_button || m_views <= 1 || count <= m_views) {
        if (m_index == 0) m_index = count - 1;
        else m_index = std::max<::cw::lib::Integer>(0, m_index - 10);
        select_feedback(this, SelectUiKind::multi_view, "page");
        return;
    }
    if (get_page() == 0) m_index = count - 1;
    else m_index = std::max<::cw::lib::Integer>(0,
        m_index - m_views * m_max_views_typed);
    m__views = m_views;
    select_feedback(this, SelectUiKind::multi_view, "page");
}

void MultiViewSelect::OnClickRightBtn(std::shared_ptr<::cw::lib::Window> arg_evt)
{
    if (m__processing || m_list.size() <= 1) return;
    const auto count = static_cast<::cw::lib::Integer>(m_list.size());
    const bool real_button = arg_evt && m_buttonlist.size() > 2 &&
        is_native_button_event(arg_evt, m_buttonlist[2]);
    const auto step = real_button && m_views > 1 && count > m_views ? m_views : 1;
    const auto domain = (step == 1) ? count : get_pagecount() * m_views;
    m_index = wrap_index(m_index + step, domain);
    if (m_index >= count) m_index = count - 1;
    m__views = m_views;
    select_feedback(this, SelectUiKind::multi_view, "page");
}

void MultiViewSelect::OnClickRight2Btn(std::shared_ptr<::cw::lib::Window> arg_evt)
{
    if (m__processing || m_list.size() <= 1) return;
    const auto count = static_cast<::cw::lib::Integer>(m_list.size());
    const bool real_button = arg_evt && m_buttonlist.size() > 3 &&
        is_native_button_event(arg_evt, m_buttonlist[3]);
    if (!real_button || m_views <= 1 || count <= m_views) {
        if (m_index == count - 1) m_index = 0;
        else m_index = std::min<::cw::lib::Integer>(count - 1, m_index + 10);
        select_feedback(this, SelectUiKind::multi_view, "page");
        return;
    }
    if (get_page() == get_pagecount() - 1) m_index = 0;
    else m_index = std::min<::cw::lib::Integer>(
        count - 1, m_index + m_views * m_max_views_typed);
    m__views = m_views;
    select_feedback(this, SelectUiKind::multi_view, "page");
}

void MultiViewSelect::OnSelect(std::shared_ptr<::cw::lib::Event> arg_event)
{
    if (m__processing || m_list.empty()) return;
    if (m_views == 1) {
        m_decision_pending = true;
        m_finished = true;
        m_modal_result = m_enterid_typed;
        return;
    }
    // Python と同様、ロックされた宿など操作できないページでは、ダブ
    // ルクリックや選択を拒否する。 共通の基底クラスには派生固有の判定がな
    // いため、一覧が存在する場合を操作可能とする。 YadoSelect
    // は OnSelect でさらに厳しいボタン・パスの条件を適用する。
    const Integer columns = std::max<Integer>(1, m_views / std::max<Integer>(1, m_lines_typed));
    Integer cell = arg_event ? arg_event->position.x + arg_event->position.y * columns : 0;
    cell = std::max<Integer>(0, cell);
    const Integer candidate = get_page() * m_views + cell;
    const Integer index = std::min<Integer>(candidate, static_cast<Integer>(m_list.size()) - 1);
    if (m_index != index) {
        m_index = index;
        m_decision_pending = false;
        enable_btn();
        select_feedback(this, SelectUiKind::multi_view, "click");
    }
}

void MultiViewSelect::OnClickViewBtn(std::shared_ptr<::cw::lib::Window> arg_event)
{
    if (m__processing) return;
    (void)arg_event;
    change_view();
    select_feedback(this, SelectUiKind::multi_view, "equipment");
    enable_btn();
}

void MultiViewSelect::change_view()
{
    m_views = (m_views == 1) ? m_max_views_typed : 1;
    if (!m_list.empty()) {
        m_index = std::clamp<::cw::lib::Integer>(
            m_index, 0, static_cast<::cw::lib::Integer>(m_list.size()) - 1);
    } else {
        m_index = 0;
    }
    m__views = m_views;
    save_views(m_views > 1);
}

void MultiViewSelect::enable_btn()
{
    // Python の基底処理は何もしない。ネイティブの基底では一覧が空ならコントロ
    // ールを無効にし、 基底を直接使う呼び出し側が存在しない項目を操作しないようにす
    // る。実際の派生クラスは固有の OK・新規・削除・ページ操作の規則を管理する。
    for (const auto& button : m_buttonlist) set_enabled(button, !m_list.empty());
}

::cw::lib::Integer MultiViewSelect::get_page()
{
    return m_views > 0 ? m_index / m_views : 0;
}

::cw::lib::Integer MultiViewSelect::get_pagecount()
{
    if (m_list.empty()) return 0;
    const auto views = std::max<::cw::lib::Integer>(1, m_views);
    return (static_cast<::cw::lib::Integer>(m_list.size()) + views - 1) / views;
}

// Python の基底メソッドは顔画像のビットマップとパスキャッシュを所有する。 ネイティブ
// の Party/Player キャンバスは load_adventurer_portrait_images
// と blend_player_portrait で読み込むため、 この旧基底処理
// は空の結果を返し、異なるアニメーション・生存期間を持つ並行キャッシュを作らない。
std::vector<std::tuple<std::shared_ptr<::cw::lib::Bitmap>, std::shared_ptr<::cw::lib::Bitmap>, std::shared_ptr<::cw::image::ImageInfo>>> MultiViewSelect::_get_player_images(std::shared_ptr<::cw::header::AdventurerHeader> arg_header)
{
    (void)arg_header;
    return {};
}

// Python はここで wx の再描画矩形を記録する。ネイティブの保
// 持型顔画像の描画面は、描画中にアニメーション依存関係を記録するため、
// この旧基底アダプターでは領域駆動の二つ目の再描画経路を登録しない。
void MultiViewSelect::_register_player_animation_region(::cw::lib::Rect arg_rect, std::vector<std::tuple<std::shared_ptr<::cw::lib::Bitmap>, std::shared_ptr<::cw::lib::Bitmap>, std::shared_ptr<::cw::image::ImageInfo>>> arg_images)
{
    (void)arg_rect;
    (void)arg_images;
}

// 使われていない互換用領域マップでは false を返す。 Python
// はここで wx の領域を再描画するが、ネイティブの保持型キャンバス
// はデコーダーフレームへの依存を検出し、共通の表示更新で再描画する。
bool MultiViewSelect::_refresh_player_animation(::cw::lib::Value arg_key)
{
    (void)arg_key;
    return {};
}

void MultiViewSelect::save_views(bool arg_multi)
{
    m_views = arg_multi ? m_max_views_typed : 1;
    m__views = m_views;
}

void PartySelect::initialize(std::shared_ptr<::cw::lib::Window> arg_parent)
{
    dialog_retained_surface().reset();
    m_input={}; m_filter_choice={};
    m_presentation_additionals = ::cw::g_cwpy && ::cw::g_cwpy->m_setting &&
        ::cw::g_cwpy->m_setting->m_show_additional_party;
    m_native_handle = arg_parent ? arg_parent->native_handle : 0;
    m_native_parent_handle = m_native_handle;
    party_callback_context = this;
    m_processing = false;
    m_cwpy_debug = select_debug_mode(false);
    m_debug_lifetime = std::make_shared<int>(0);
    m_decision_pending = false;
    m_finished = false;
    m_modal_result = -1;
    m_index = 0;
    m_max_views = 8;
    m_views = (::cw::g_cwpy && ::cw::g_cwpy->m_setting &&
               ::cw::g_cwpy->m_setting->m_show_multipleparties) ? m_max_views : 1;
    m_lines = 2;
    m_enterid = 1;
    m_clickmode = 0;
    m_downbutton = -1;
    m_narrow = String{};
    m_narrow_type = Integer{};
    // 最初のスナップショットを取る前に、保存済みの wx の並べ替え選択を復元する。
    m_sort = (::cw::g_cwpy && ::cw::g_cwpy->m_setting)
        ? Value(::cw::g_cwpy->m_setting->m_sort_parties) : Value(String("None"));
    m_toppanel_window = make_window("PartySelect");
    if (arg_parent) m_toppanel_window->bounds = arg_parent->bounds;
    m_toppanel = Value(std::static_pointer_cast<::cw::lib::Object>(m_toppanel_window));
    m_left2btn = make_window("left2", -1);
    m_leftbtn = make_window("left", -2);
    m_rightbtn = make_window("right", -3);
    m_right2btn = make_window("right2", -4);
    m_buttonlist = {m_left2btn, m_leftbtn, m_rightbtn, m_right2btn};
    m_okbtn = Value(std::static_pointer_cast<::cw::lib::Object>(make_window("ok", 1)));
    m_infobtn = Value(std::static_pointer_cast<::cw::lib::Object>(make_window("info", 2)));
    m_editbtn = Value(std::static_pointer_cast<::cw::lib::Object>(make_window("edit", 3)));
    m_viewbtn = Value(std::static_pointer_cast<::cw::lib::Object>(make_window("view", 4)));
    m_partyrecordbtn = Value(std::static_pointer_cast<::cw::lib::Object>(make_window("partyrecord", 5)));
    m_closebtn = Value(std::static_pointer_cast<::cw::lib::Object>(make_window("close", 6)));
    // 再開・パーティー選択には、現在読み込んでいる宿
    // のデータを使う。 静的キャッシュは互換用の代
    // 替に限り、最初の並べ替え前は空の場合がある。
    if (::cw::g_cwpy && ::cw::g_cwpy->m_ydata && *::cw::g_cwpy->m_ydata)
        m_source_list = (*::cw::g_cwpy->m_ydata)->m_partys;
    else
        m_source_list = ::cw::data::YadoData::s_partys;
    if (::cw::g_cwpy && ::cw::g_cwpy->m_ydata && *::cw::g_cwpy->m_ydata) {
        (*::cw::g_cwpy->m_ydata)->sort_parties();
        m_source_list = (*::cw::g_cwpy->m_ydata)->m_partys;
    }
    m_typed_list = m_source_list;
    if (::cw::g_cwpy && ::cw::g_cwpy->m_ydata && *::cw::g_cwpy->m_ydata) {
        const String lastparty = (*::cw::g_cwpy->m_ydata)->m_lastparty;
        if (!lastparty.empty()) {
            const String lastpath = lower_ascii(::cw::util::get_yadofilepath(lastparty));
            for (std::size_t i = 0; i < m_typed_list.size(); ++i) {
                const auto& header = m_typed_list[i];
                if (header && lower_ascii(::cw::util::get_yadofilepath(value_string(header->m_fpath))) == lastpath) {
                    m_index = static_cast<Integer>(i);
                    break;
                }
            }
        }
        if (::cw::g_cwpy->m_setting)
            m_narrow_type = ::cw::g_cwpy->m_setting->m_parties_narrowtype;
    }
    const String sort = value_string(m_sort, "None");
    std::stable_sort(m_typed_list.begin(), m_typed_list.end(), [sort](const auto& left, const auto& right) {
        if (!left || !right) return static_cast<bool>(right);
        if (sort == "Name") return ::cw::util::logical_compare(left->m_name,right->m_name)<0;
        if (sort == "HighestLevel") return left->get_highest_level() < right->get_highest_level();
        if (sort == "AverageLevel") return left->get_average_level() < right->get_average_level();
        if (sort == "Money") return left->m_money < right->m_money;
        return value_integer(left->m_order, 0x7fffffff) < value_integer(right->m_order, 0x7fffffff);
    });
    std::vector<Value> values;
    values.reserve(m_typed_list.size());
    for (const auto& header : m_typed_list) {
        values.emplace_back(header ? Value(std::static_pointer_cast<::cw::lib::Object>(header)) : Value{});
    }
    m_list = make_sequence(values);
    update_narrowcondition();
    enable_btn();
#if defined(CARDWIRTH_LOG_ENABLED)
    ::cardwirth::debug::log("PartySelect open: party_count=" + std::to_string(m_source_list.size()) +
        " typed_count=" + std::to_string(m_typed_list.size()) +
        " index=" + std::to_string(m_index) + " views=" + std::to_string(m_views));
#endif
    if (const auto backend = get_ui_backend()) backend->bind_select_dialog(this, SelectUiKind::party);
}

bool PartySelect::can_clickside()
{
    return m_views <= 1;
}

void PartySelect::OnLeftDClick(std::shared_ptr<::cw::lib::Window> arg_event)
{
    if (m_processing) return;
    if (m_views > 1 && !m_typed_list.empty() && m_clickmode == 0 && can_clickcenter()) {
        m_decision_pending = true;
        m_finished = true;
        m_modal_result = m_enterid;
    } else if (m_views <= 1 && m_clickmode == kLeft) {
        OnClickLeftBtn(arg_event);
    } else if (m_views <= 1 && m_clickmode == kRight) {
        OnClickRightBtn(arg_event);
    }
}

void PartySelect::OnClickLeftBtn(std::shared_ptr<::cw::lib::Window> arg_event)
{
    if (m_processing || m_typed_list.size() <= 1) return;
    const bool real_button = is_native_button_event(arg_event, m_leftbtn);
    const Integer step = real_button && m_views > 1 &&
        m_typed_list.size() > static_cast<std::size_t>(m_views) ? m_views : 1;
    const Integer domain = step == 1 ? static_cast<Integer>(m_typed_list.size()) : get_pagecount() * m_views;
    m_index = wrap_index(m_index - step, domain);
    if (m_index >= static_cast<Integer>(m_typed_list.size())) m_index = static_cast<Integer>(m_typed_list.size()) - 1;
    enable_btn();
    select_feedback(this, SelectUiKind::party, "page");
}

void PartySelect::OnClickLeft2Btn(std::shared_ptr<::cw::lib::Window> arg_event)
{
    if (m_processing || m_typed_list.size() <= 1) return;
    const Integer count = static_cast<Integer>(m_typed_list.size());
    const bool real_button = is_native_button_event(arg_event, m_left2btn);
    if (!real_button || m_views <= 1 || count <= m_views) {
        m_index = m_index == 0 ? count - 1 : std::max<Integer>(0, m_index - 10);
    } else if (get_page() == 0) {
        m_index = count - 1;
    } else {
        m_index = std::max<Integer>(0, m_index - m_views * m_max_views);
    }
    enable_btn();
    select_feedback(this, SelectUiKind::party, "page");
}

void PartySelect::OnClickRightBtn(std::shared_ptr<::cw::lib::Window> arg_event)
{
    if (m_processing || m_typed_list.size() <= 1) return;
    const Integer count = static_cast<Integer>(m_typed_list.size());
    const bool real_button = is_native_button_event(arg_event, m_rightbtn);
    const Integer step = real_button && m_views > 1 && count > m_views ? m_views : 1;
    const Integer domain = step == 1 ? count : get_pagecount() * m_views;
    m_index = wrap_index(m_index + step, domain);
    if (m_index >= count) m_index = count - 1;
    enable_btn();
    select_feedback(this, SelectUiKind::party, "page");
}

void PartySelect::OnClickRight2Btn(std::shared_ptr<::cw::lib::Window> arg_event)
{
    if (m_processing || m_typed_list.size() <= 1) return;
    const Integer count = static_cast<Integer>(m_typed_list.size());
    const bool real_button = is_native_button_event(arg_event, m_right2btn);
    if (!real_button || m_views <= 1 || count <= m_views) {
        m_index = m_index == count - 1 ? 0 : std::min<Integer>(count - 1, m_index + 10);
    } else if (get_page() == get_pagecount() - 1) {
        m_index = 0;
    } else {
        m_index = std::min<Integer>(count - 1, m_index + m_views * m_max_views);
    }
    enable_btn();
    select_feedback(this, SelectUiKind::party, "page");
}

void PartySelect::OnSelect(std::shared_ptr<::cw::lib::Event> arg_event)
{
    if (m_processing || m_typed_list.empty()) return;
    if (m_views <= 1) {
        m_decision_pending = true;
        m_finished = true;
        m_modal_result = m_enterid;
        return;
    }
    // ネイティブの「決定」ボタンにはマウスイベントがない。 現在強調表
    // 示しているパーティーを確定し、セル 0 の操作とは解釈しない。
    if (!arg_event) {
        m_decision_pending = true;
        m_finished = true;
        m_modal_result = m_enterid;
        return;
    }
    const Integer columns = std::max<Integer>(1, m_views / std::max<Integer>(1, m_lines));
    const Integer cell = arg_event ? std::max<Integer>(0, arg_event->position.x + arg_event->position.y * columns) : 0;
    const Integer candidate = get_page() * m_views + cell;
    const Integer index = std::min<Integer>(candidate, static_cast<Integer>(m_typed_list.size()) - 1);
    if (m_index != index) {
        m_index = index;
        enable_btn();
        select_feedback(this, SelectUiKind::party, "click");
    } else {
        m_decision_pending = true;
        m_finished = true;
        m_modal_result = m_enterid;
    }
}

void PartySelect::OnClickViewBtn(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    if (m_processing) return;
    change_view();
    enable_btn();
    select_feedback(this, SelectUiKind::party, "equipment");
}

void PartySelect::change_view()
{
    m_views = m_views == 1 ? m_max_views : 1;
    // Py と同様、両方の表示で Table/Book を使う。 配置が変わったら共通キャンバスのキ
    // ャッシュを無効にする。Book_Multi は PartySelect のリソースではない。
    m__bg = Value{};
    if (m_typed_list.empty()) m_index = 0;
    else m_index = std::clamp<Integer>(m_index, 0, static_cast<Integer>(m_typed_list.size()) - 1);
    save_views(m_views > 1);
}

Integer PartySelect::get_page()
{
    return m_views > 0 ? m_index / m_views : 0;
}

Integer PartySelect::get_pagecount()
{
    if (m_typed_list.empty()) return 0;
    return (static_cast<Integer>(m_typed_list.size()) + std::max<Integer>(1, m_views) - 1) /
           std::max<Integer>(1, m_views);
}

void PartySelect::save_views(bool arg_multi)
{
    m_views = arg_multi ? m_max_views : 1;
    if (::cw::g_cwpy && ::cw::g_cwpy->m_setting)
        ::cw::g_cwpy->m_setting->m_show_multipleparties = arg_multi;
}

void PartySelect::update_additionals()
{
    // ネイティブの配置は m_show_additional_party を直接読む。 PartySelect
    // は平坦化したモデルを保持するため、Select::m_addctrlbtn は持たない。
}

void PartySelect::_add_topsizer()
{
    // 絞り込みと並べ替えの配置は、Windows の SelectUiBackend::layout_select_dialog で行う。
}

void PartySelect::_on_narrowcondition()
{
    if (::cw::g_cwpy && ::cw::g_cwpy->m_setting)
        ::cw::g_cwpy->m_setting->m_parties_narrowtype = value_integer(m_narrow_type);
    update_narrowcondition();
    // Py と同様、ページ音は遅延した絞り込み時ではなく、通知の受付時に鳴らす。
    select_feedback(this, SelectUiKind::party);
}

void PartySelect::OnDebugMode(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    select_toggle_debug(*this);
}

bool PartySelect::queue_debug_change(const std::shared_ptr<::cw::frame::Frame>& frame,
    const std::function<void(::cw::lib::Callback)>& post_game, std::function<bool()> change)
{
    return !m_processing && select_queue_debug_change(*this, frame, post_game, std::move(change));
}

void PartySelect::update_debug()
{
    _on_narrowcondition();
}

void PartySelect::update_narrowcondition()
{
    const std::shared_ptr<::cw::header::PartyHeader> selected =
        m_index >= 0 && m_index < static_cast<Integer>(m_typed_list.size())
            ? m_typed_list[static_cast<std::size_t>(m_index)] : nullptr;
    // 再開・パーティー選択には、現在読み込んでいる宿
    // のデータを使う。 静的キャッシュは互換用の代
    // 替に限り、最初の並べ替え前は空の場合がある。
    if (::cw::g_cwpy && ::cw::g_cwpy->m_ydata && *::cw::g_cwpy->m_ydata)
        m_source_list = (*::cw::g_cwpy->m_ydata)->m_partys;
    else
        m_source_list = ::cw::data::YadoData::s_partys;
    if (::cw::g_cwpy && ::cw::g_cwpy->m_ydata && *::cw::g_cwpy->m_ydata) {
        (*::cw::g_cwpy->m_ydata)->sort_parties();
        m_source_list = (*::cw::g_cwpy->m_ydata)->m_partys;
    }
    m_typed_list = m_source_list;
    const String sort = value_string(m_sort, "None");
    std::stable_sort(m_typed_list.begin(), m_typed_list.end(), [sort](const auto& left, const auto& right) {
        if (!left || !right) return static_cast<bool>(right);
        if (sort == "Name") return ::cw::util::logical_compare(left->m_name,right->m_name)<0;
        if (sort == "HighestLevel") return left->get_highest_level() < right->get_highest_level();
        if (sort == "AverageLevel") return left->get_average_level() < right->get_average_level();
        if (sort == "Money") return left->m_money < right->m_money;
        return value_integer(left->m_order, 0x7fffffff) < value_integer(right->m_order, 0x7fffffff);
    });
    const String narrow = lower_ascii(value_string(m_narrow));
    const Integer type = value_integer(m_narrow_type);
    const bool do_narrow = !narrow.empty() &&
        (!::cw::g_cwpy || !::cw::g_cwpy->m_setting ||
         ::cw::g_cwpy->m_setting->m_show_additional_party);
    if (do_narrow) {
        narrow_context.narrow = narrow;
        narrow_context.debug = select_debug_mode(m_cwpy_debug);
        narrow_context.attrs.clear();
        narrow_context.attrs.insert(::cw::setting::Setting::s_periodnames.begin(), ::cw::setting::Setting::s_periodnames.end());
        narrow_context.attrs.insert(::cw::setting::Setting::s_sexnames.begin(), ::cw::setting::Setting::s_sexnames.end());
        narrow_context.attrs.insert(::cw::setting::Setting::s_naturenames.begin(), ::cw::setting::Setting::s_naturenames.end());
        narrow_context.attrs.insert(::cw::setting::Setting::s_makingnames.begin(), ::cw::setting::Setting::s_makingnames.end());
        narrow_context.int_valid = false;
        if (type == 0 || type == 6) {
            if (const auto normalized = ::cw::lib::normalize_python_integer(narrow)) {
                const char* first = normalized->data();
                const char* last = first + normalized->size();
                if (first != last && *first == '+') ++first;
                const auto parsed = std::from_chars(first, last, narrow_context.int_value);
                narrow_context.int_valid = parsed.ec == std::errc{} && parsed.ptr == last;
            }
        }
        const bool all = type == 0;
        std::vector<std::shared_ptr<::cw::header::PartyHeader>> filtered;
        for (const auto& header : m_typed_list) {
            if (!header) continue;
            const bool name = lower_ascii(header->m_name).find(narrow) != String::npos;
            narrow_context.party = header;
            // Py の型付き判定と左から右への OR の短絡評価に合わせる。
            // 先の条件で一致した後に、無関係なメンバーの XML を読まない。
            if (((all || type == 1) && name) ||
                ((all || type == 2) && PartySelect_update_narrowcondition_has_membername()) ||
                ((all || type == 3) && PartySelect_update_narrowcondition_has_memberdesc()) ||
                ((all || type == 4) && PartySelect_update_narrowcondition_has_memberhistory()) ||
                ((all || type == 5) && PartySelect_update_narrowcondition_has_memberfeatures()) ||
                ((all || type == 6) && PartySelect_update_narrowcondition_has_memberlevel())) {
                filtered.push_back(header);
            }
        }
        m_typed_list = std::move(filtered);
    }
    std::vector<Value> values;
    values.reserve(m_typed_list.size());
    for (const auto& header : m_typed_list) {
        values.emplace_back(header ? Value(std::static_pointer_cast<::cw::lib::Object>(header)) : Value{});
    }
    m_list = make_sequence(values);
    if (selected) {
        const auto iter = std::find(m_typed_list.begin(), m_typed_list.end(), selected);
        if (iter != m_typed_list.end()) m_index = static_cast<Integer>(iter - m_typed_list.begin());
        else if (!m_typed_list.empty()) m_index = wrap_index(m_index, static_cast<Integer>(m_typed_list.size()));
        else m_index = 0;
    } else if (m_typed_list.empty()) {
        m_index = 0;
    } else {
        m_index = std::clamp<Integer>(m_index, 0, static_cast<Integer>(m_typed_list.size()) - 1);
    }
    m_names.clear();
    for (const auto& header : m_typed_list) if (header) m_names.push_back(header->m_name);
    enable_btn();
}

std::shared_ptr<::cw::lib::Bitmap> PartySelect::_get_bg()
{
    if (const auto* object = std::get_if<::cw::lib::ObjectPtr>(&m__bg.storage()))
        if (const auto bitmap = std::dynamic_pointer_cast<::cw::lib::Bitmap>(*object)) return bitmap;
    const String base = ::cw::g_cwpy && ::cw::g_cwpy->m_rsrc ? ::cw::g_cwpy->m_rsrc->m_skindir : String{};
    // Python の PartySelect と同様、常に Table/Book
    // を使い、どの表示も同じ背景に配置する。 Book_Multi はスキンリソー
    // スではないため、複数表示に使うとネイティブのキャンバスが代替の黒背景になる。
    const String name = "Table/Book";
    const String requested = base.empty() ? name : ::cw::lib::encoding::path_to_utf8(
        std::filesystem::u8path(base) / std::filesystem::u8path(name));
    const String resolved = ::cw::util::find_resource(requested, ::cw::g_M_IMG);
    const auto bitmap = get_ui_backend()->load_select_bitmap(resolved.empty() ? requested : resolved, false);
#if defined(_DEBUG)
    ::cardwirth::debug::log("YadoSelect background: requested=" + requested + " resolved=" + resolved +
        " bitmap=" + (bitmap ? std::to_string(bitmap->size.width) + "x" + std::to_string(bitmap->size.height) : String("null")));
#endif
    if (bitmap) m__bg = Value(std::static_pointer_cast<::cw::lib::Object>(bitmap));
    return bitmap;
}

std::vector<std::tuple<std::shared_ptr<::cw::lib::Bitmap>, std::shared_ptr<::cw::lib::Bitmap>, std::shared_ptr<::cw::image::ImageInfo>>> PartySelect::_get_player_images(std::shared_ptr<::cw::header::AdventurerHeader> arg_header)
{
    if (!arg_header) return {};
    const String key = value_string(arg_header->m_fpath);
    const auto found = m_player_image_cache.find(key);
    if (found != m_player_image_cache.end()) return found->second;
    std::vector<std::tuple<std::shared_ptr<::cw::lib::Bitmap>, std::shared_ptr<::cw::lib::Bitmap>, std::shared_ptr<::cw::image::ImageInfo>>> images;
    for (const auto& info : arg_header->get_imgpaths()) {
        // Python はここで読み込み済みビットマップをキャッシュする。旧ネ
        // イティブ取得 API は代わりに、 ビットマップ欄を null とし
        // たメタデータをキャッシュし、後の描画でもそのタプルを埋めない。 共通
        // Party キャンバスが専用の共通ローダーで顔画像を読み、保持画像
        // の依存を追跡するため、別の画像・アニメーション描画経路は作らない。
        images.emplace_back(nullptr, nullptr, info);
    }
    m_player_image_cache[key] = images;
    return images;
}

// Python はアニメーションキーに各 wx 領域と全画像
// を保持する。ネイティブのキーは Py の画像所有者・パスで
// はなく、 十進表記のデコーダーインスタンス ID である。
// 再登録では保存した x 座標を上書きし、Py の矩形・画像
// の組は追加しない。 共通キャンバスは表示中の顔画像を再構築
// するため、この互換マップはインスタンスの所属だけを記録す
// る。 実際の Party キャンバスが描画中に合成と保持
// 描画面の依存追跡を行い、別の領域再描画実装は開始しない。
void PartySelect::_register_player_animation_region(::cw::lib::Rect arg_rect, std::vector<std::tuple<std::shared_ptr<::cw::lib::Bitmap>, std::shared_ptr<::cw::lib::Bitmap>, std::shared_ptr<::cw::image::ImageInfo>>> arg_images)
{
    for (const auto& image : arg_images) {
        const auto& info = std::get<2>(image);
        if (!info || !::cw::animatedimage::is_supported_path(info->m_path)) continue;
        const auto instance = ::cw::animatedimage::acquire(info->m_path, "select.party", false, std::nullopt);
        if (instance) m_player_animation_regions[std::to_string(*instance)] = Value(arg_rect.x);
    }
}

// Python は表示中の登録矩形だけを再描画する。この旧ネイティブのキ
// ーアダプターは共通パーティー表示全体の更新を要求し、 実際の再描画範囲
// は保持中の更新領域とデコーダーフレームへの依存で決める。 したがって、
// 直接呼び出した場合は wx の非表示パネル時の戻り方までは再現しない。
bool PartySelect::_refresh_player_animation(::cw::lib::Value arg_key)
{
    const Integer key = value_integer(arg_key, -1);
    if (key < 0 || !m_player_animation_regions.contains(std::to_string(key))) return false;
    select_feedback(this, SelectUiKind::party);
    return true;
}

void PartySelect::OnNumberKeyDown(std::shared_ptr<::cw::lib::Window> arg_event)
{
    if (m_processing || !arg_event) return;
    Integer index = arg_event->id;
    for (std::size_t i = 0; i < m_sortkeydown.size(); ++i) {
        if (value_integer(m_sortkeydown[i], -1) == arg_event->id) {
            index = static_cast<Integer>(i);
            break;
        }
    }
    if (0 <= index && index <= 4) {
        // wx と同様、アクセラレーターを同じ Choice イベントへ送る。 経路
        // を一つに保ち、保存する順序と YadoData の順序の両方を更新する。
        auto event = std::make_shared<::cw::lib::Window>();
        event->id = index;
        OnSort(std::move(event));
    }
}

void PartySelect::OnSort(std::shared_ptr<::cw::lib::Window> arg_event)
{
    if (m_processing) return;
    const Integer index = arg_event ? arg_event->id : 0;
    const String next = index == 1 ? String("Name") : index == 2 ? String("HighestLevel") :
                        index == 3 ? String("AverageLevel") : index == 4 ? String("Money") : String("None");
    if (value_string(m_sort, "None") != next) {
        m_sort = next;
        auto comparator = [this](const auto& left, const auto& right) {
            if (!left || !right) return static_cast<bool>(right);
            const String sort = value_string(m_sort, "None");
            if (sort == "Name") return ::cw::util::logical_compare(left->m_name,right->m_name)<0;
            if (sort == "HighestLevel") return left->get_highest_level() < right->get_highest_level();
            if (sort == "AverageLevel") return left->get_average_level() < right->get_average_level();
            if (sort == "Money") return left->m_money < right->m_money;
            return value_integer(left->m_order, 0x7fffffff) < value_integer(right->m_order, 0x7fffffff);
        };
        std::stable_sort(m_source_list.begin(), m_source_list.end(), comparator);
        if (::cw::g_cwpy && ::cw::g_cwpy->m_setting)
            ::cw::g_cwpy->m_setting->m_sort_parties = value_string(m_sort, "None");
        if (::cw::g_cwpy && ::cw::g_cwpy->m_ydata && *::cw::g_cwpy->m_ydata) {
            (*::cw::g_cwpy->m_ydata)->sort_parties();
            m_source_list = (*::cw::g_cwpy->m_ydata)->m_partys;
        } else {
            ::cw::data::YadoData::s_partys = m_source_list;
        }
        update_narrowcondition();
        select_feedback(this, SelectUiKind::party, "page");
    }
}

void PartySelect::OnMouseWheel(std::shared_ptr<::cw::lib::Window> arg_event)
{
    if (m_processing || m_typed_list.size() <= 1) return;
    const Integer size = static_cast<Integer>(m_typed_list.size());
    const Integer step = size <= m_views ? 1 : std::max<Integer>(1, m_views);
    const Integer range = size <= m_views ? size : std::max<Integer>(1, get_pagecount() * m_views);
    m_index = wrap_index(m_index + (event_wheel(arg_event) > 0 ? -step : step), range);
    if (m_index >= size) m_index = size - 1;
    enable_btn();
    select_feedback(this, SelectUiKind::party, "page");
}

void PartySelect::OnClickInfoBtn(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    if (m_processing || m_typed_list.empty()) return;
    select_play_sound("click");
    get_ui_backend()->open_party_editor(this);
}

void PartySelect::OnClickEditBtn(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    if (m_processing || m_typed_list.empty()) return;
    select_play_sound("click");
    get_ui_backend()->open_party_members(this);
}

void PartySelect::OnClickPartyRecordBtn(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    if (m_processing) return;
    if (!::cw::g_cwpy || !::cw::g_cwpy->m_ydata || !*::cw::g_cwpy->m_ydata) return;
    select_play_sound("click");
    const auto ydata = *::cw::g_cwpy->m_ydata;
    auto dialog = std::make_shared<::cw::dialog::partyrecord::SelectPartyRecord>();
    const auto self=weak_from_this().lock();
    dialog->initialize(self ? Value(std::static_pointer_cast<::cw::lib::Object>(self)) : Value(m_toppanel_window));
    dialog->set_records(ydata->m_partyrecord, ydata->m_party.has_value() && *ydata->m_party);
    // 共通モデルには構築・描画だけでなくモーダルホストが必要である
    // 。 ホストの処理終了時に、非同期の親コールバックを切り離す。
    try { (void)::cw::dialog::show_common_modal_presentation(dialog); }
    catch(...) { dialog->set_parent_callbacks({}, {}, {}, {}); m_processing=false; throw; }
    dialog->set_parent_callbacks({}, {}, {}, {});
    m_processing=false;
    draw(true);
}

std::optional<std::shared_ptr<::cw::header::PartyHeader>> PartySelect::get_selected()
{
    if (m_typed_list.empty() || m_index < 0 || m_index >= static_cast<Integer>(m_typed_list.size())) return std::nullopt;
    return m_typed_list[static_cast<std::size_t>(m_index)];
}

void PartySelect::update_standbys(std::shared_ptr<::cw::header::PartyHeader> arg_selected)
{
    // Python の PartySelect.update_standbys と同様、何もしない。
    (void)arg_selected;
}

bool PartySelect::can_clickcenter()
{
    return is_enabled(object_value<::cw::lib::Window>(m_okbtn));
}

void PartySelect::enable_btn()
{
    const auto ok = object_value<::cw::lib::Window>(m_okbtn);
    const auto info = object_value<::cw::lib::Window>(m_infobtn);
    const auto edit = object_value<::cw::lib::Window>(m_editbtn);
    const auto view = object_value<::cw::lib::Window>(m_viewbtn);
    const auto records = object_value<::cw::lib::Window>(m_partyrecordbtn);
    const auto close = object_value<::cw::lib::Window>(m_closebtn);
    const bool has_partyrecord = ::cw::g_cwpy && ::cw::g_cwpy->m_ydata && *::cw::g_cwpy->m_ydata &&
        ((*::cw::g_cwpy->m_ydata)->m_party.has_value() || !(*::cw::g_cwpy->m_ydata)->m_partyrecord.empty());
    if (m_typed_list.empty()) {
        set_enabled(ok, false); set_enabled(info, false); set_enabled(edit, false); set_enabled(view, false);
        set_enabled(records, has_partyrecord); set_enabled(close, true);
        for (const auto& button : m_buttonlist) set_enabled(button, false);
        return;
    }
    set_enabled(ok, true); set_enabled(info, true); set_enabled(edit, true); set_enabled(view, true);
    set_enabled(records, has_partyrecord); set_enabled(close, true);
    const bool many = m_typed_list.size() > 1;
    set_enabled(m_leftbtn, many); set_enabled(m_left2btn, many);
    set_enabled(m_rightbtn, many); set_enabled(m_right2btn, many);
}

void PartySelect::draw(bool arg_update)
{
    (void)_get_bg();
    // 共通キャンバスは表示中のパーティーと先頭の顔画像だけを読む。 使わ
    // れない先行読み込みで、実際の描画前に同じデータを二重に読まない。
    select_feedback(this, SelectUiKind::party, {}, arg_update);
}

void PlayerSelect::initialize(std::shared_ptr<::cw::lib::Window> arg_parent, bool arg_bind_native_ui)
{
    dialog_retained_surface().reset();
    m_input={}; m_filter_choice={};
    m_presentation_additionals = ::cw::g_cwpy && ::cw::g_cwpy->m_setting &&
        ::cw::g_cwpy->m_setting->m_show_additional_player;
    // Python版は MultiViewSelect.__init__ → Select.__init__ の順に初期化する。
    // wx の実体は互換 Window に留め、選択・ページ状態はここで完全に確定する。
    m_native_handle = arg_parent ? arg_parent->native_handle : 0;
    m_native_parent_handle = m_native_handle;
    m_source_list.clear();
    m_typed_list.clear();
    m_list.clear();
    m_isalbum = false;
    m_index = 0;
    m_max_views = 10;
    m_views = (::cw::g_cwpy && ::cw::g_cwpy->m_setting &&
               ::cw::g_cwpy->m_setting->m_show_multipleplayers) ? m_max_views : 1;
    m_lines = 2;
    m_enterid = 510;
    m_clickmode = 0;
    m_downbutton = -1;
    m_cwpy_debug = select_debug_mode(false);
    m_debug_lifetime = std::make_shared<int>(0);
    m__processing = false;
    m_enrollment_token.reset();
    m_adventurer_change_token.reset();
    m_information_open_token.reset();
    m_information_lifetime = std::make_shared<int>(0);
    m_decision_pending = false;
    m_finished = false;
    m_modal_result = -1;
    m_narrow = Value(String{});
    m_narrow_type = (::cw::g_cwpy && ::cw::g_cwpy->m_setting)
        ? Value(::cw::g_cwpy->m_setting->m_standbys_narrowtype)
        : Value(Integer(0));
    // Python の PlayerSelect と同様、保存済みの待機中キャラクターの並べ替え設定を維持する。
    m_sort = (::cw::g_cwpy && ::cw::g_cwpy->m_setting)
        ? Value(::cw::g_cwpy->m_setting->m_sort_standbys) : Value(String("None"));
    m__player_animation_regions.clear();
    m__player_target_rects.clear();
    m__player_target_overlay = {Value{}, Value{}};
    m__player_image_cache.clear();
    m_extension_enabled.clear();
    // 開き直したダイアログが、キャラクター更新のコールバックを所有する。
    player_callback_context = this;

    m_toppanel_window = make_window("player_top");
    if (arg_parent) m_toppanel_window->bounds = arg_parent->bounds;
    m_toppanel = Value(std::static_pointer_cast<::cw::lib::Object>(m_toppanel_window));
    m_left2btn = make_window("left2", -1);
    m_leftbtn = make_window("left", -2);
    m_rightbtn = make_window("right", -3);
    m_right2btn = make_window("right2", -4);
    m_buttonlist = {m_left2btn, m_leftbtn, m_rightbtn, m_right2btn};
    m_addbtn = Value(std::static_pointer_cast<::cw::lib::Object>(make_window("add", 510)));
    m_infobtn = Value(std::static_pointer_cast<::cw::lib::Object>(make_window("info")));
    m_newbtn = Value(std::static_pointer_cast<::cw::lib::Object>(make_window("new")));
    m_exbtn = Value(std::static_pointer_cast<::cw::lib::Object>(make_window("extension")));
    m_viewbtn = Value(std::static_pointer_cast<::cw::lib::Object>(make_window("view")));
    m_closebtn = Value(std::static_pointer_cast<::cw::lib::Object>(make_window("close", -1)));
    for (const auto& value : {m_addbtn, m_infobtn, m_newbtn, m_exbtn, m_viewbtn, m_closebtn}) {
        if (auto window = object_value<::cw::lib::Window>(value)) m_buttonlist.push_back(window);
    }

    if (::cw::g_cwpy && ::cw::g_cwpy->m_ydata && *::cw::g_cwpy->m_ydata) {
        (*::cw::g_cwpy->m_ydata)->sort_standbys();
        m_source_list = (*::cw::g_cwpy->m_ydata)->m_standbys;
    }
    update_narrowcondition();
    enable_btn();
    if (arg_bind_native_ui) {
        if (const auto backend = get_ui_backend()) backend->bind_select_dialog(this, SelectUiKind::player);
    }
#if defined(CARDWIRTH_LOG_ENABLED)
    ::cardwirth::debug::log("PlayerSelect open: standby_count=" + std::to_string(m_source_list.size()) +
                            " typed=" + std::to_string(m_typed_list.size()) +
                            " index=" + std::to_string(m_index) +
                            " views=" + std::to_string(m_views) +
                            " first=" + (m_typed_list.empty() || !m_typed_list.front() ? String("<none>") : m_typed_list.front()->m_name));
#endif
    draw(true);
}

void PlayerSelect::save_views(bool arg_multi)
{
    m_views = arg_multi ? m_max_views : 1;
    if (::cw::g_cwpy && ::cw::g_cwpy->m_setting)
        ::cw::g_cwpy->m_setting->m_show_multipleplayers = arg_multi;
    m__bg = Value{};
    m__player_target_overlay = {Value{}, Value{}};
    auto view = object_value<::cw::lib::Window>(m_viewbtn);
    if (::cw::g_cwpy && ::cw::g_cwpy->m_setting && ::cw::g_cwpy->m_setting->m_msgs) {
        set_label(view, ::cw::g_cwpy->m_setting->m_msgs->get(
            m_views == 1 ? "member_list" : "member_one", m_views == 1 ? "一覧" : "一人"));
    } else {
        set_label(view, m_views == 1 ? "一覧" : "一人");
    }
    enable_btn();
}

void PlayerSelect::update_additionals()
{
    // ネイティブの配置は m_show_additional_player を直接読む。 PlayerSelect
    // は平坦化したモデルを持つため、Select::m_addctrlbtn は持たない。
}

void PlayerSelect::_add_topsizer()
{
    // 絞り込み・絞り込み種別・並べ替えは、Windows の SelectUiBackend のサイズ変更時の配置処理で並べる。
}

void PlayerSelect::OnDebugMode(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    select_toggle_debug(*this);
}

bool PlayerSelect::queue_debug_change(const std::shared_ptr<::cw::frame::Frame>& frame,
    const std::function<void(::cw::lib::Callback)>& post_game, std::function<bool()> change)
{
    return !m__processing && select_queue_debug_change(*this, frame, post_game, std::move(change));
}

void PlayerSelect::update_debug()
{
    _on_narrowcondition();
}

void PlayerSelect::_on_narrowcondition()
{
    // Py と同様、Album には絞り込み種別の選択欄がない。 デバッグ更新でも待
    // 機中キャラクターの設定を保持し、存在しない絞り込みコールバックは実行しない。
    if (m_isalbum || std::holds_alternative<std::monostate>(m_narrow_type.storage())) return;
    if (::cw::g_cwpy && ::cw::g_cwpy->m_setting)
        ::cw::g_cwpy->m_setting->m_standbys_narrowtype = value_integer(m_narrow_type);
    m_narrow_type = Value(std::max<Integer>(0, value_integer(m_narrow_type)));
    update_narrowcondition();
    draw(true);
}

void PlayerSelect::update_narrowcondition()
{
    std::shared_ptr<::cw::header::AdventurerHeader> selected;
    if (m_index >= 0 && static_cast<std::size_t>(m_index) < m_typed_list.size())
        selected = m_typed_list[static_cast<std::size_t>(m_index)];

    std::shared_ptr<::cw::data::YadoData> ydata;
    if (::cw::g_cwpy && ::cw::g_cwpy->m_ydata && *::cw::g_cwpy->m_ydata)
        ydata = *::cw::g_cwpy->m_ydata;
    if (ydata && !m_isalbum) {
        // YadoData は Yado.db から m_standbys
        // を再構築し、パーティーメンバーや無効な行を除外済みである。 m_fpath
        // は宿からの相対パスの場合があるため、ここで再検査しない。
        // 二重に検査すると、有効な先頭の冒険者まで除外してしまう。
        m_source_list = ydata->m_standbys;
    } else if (ydata) {
        m_source_list = ydata->m_album;
    }

    const String narrow = lower_ascii(value_string(m_narrow));
    const bool do_narrow = !narrow.empty() &&
        (!::cw::g_cwpy || !::cw::g_cwpy->m_setting ||
         ::cw::g_cwpy->m_setting->m_show_additional_player);
    const Integer ntype = value_integer(m_narrow_type);
    const bool debug = select_debug_mode(m_cwpy_debug);
    std::set<String> attrs;
    attrs.insert(::cw::setting::Setting::s_periodnames.begin(), ::cw::setting::Setting::s_periodnames.end());
    attrs.insert(::cw::setting::Setting::s_sexnames.begin(), ::cw::setting::Setting::s_sexnames.end());
    attrs.insert(::cw::setting::Setting::s_naturenames.begin(), ::cw::setting::Setting::s_naturenames.end());
    attrs.insert(::cw::setting::Setting::s_makingnames.begin(), ::cw::setting::Setting::s_makingnames.end());
    const std::set<String> hiddens = {"＿", "＠"};

    m_typed_list.clear();
    for (const auto& header : m_source_list) {
        if (!header) continue;
        bool matched = !do_narrow;
        if (do_narrow) {
            const auto contains = [&](const String& text) {
                return lower_ascii(text).find(narrow) != String::npos;
            };
            const auto level_match = [&]() {
                const auto normalized = ::cw::lib::normalize_python_integer(narrow);
                if (!normalized) return false;
                Integer requested{};
                auto first = normalized->data();
                const auto last = normalized->data() + normalized->size();
                if (first != last && *first == '+') ++first;
                const auto parsed = std::from_chars(first, last, requested);
                return parsed.ec == std::errc{} && parsed.ptr == last &&
                    value_integer(header->m_level, -1) == requested;
            };
            narrow_context.player = header;
            narrow_context.narrow = narrow;
            narrow_context.attrs = attrs;
            narrow_context.debug = debug;
            if (ntype == 0) matched = contains(header->m_name) || contains(value_string(header->m_desc)) ||
                PlayerSelect_update_narrowcondition_has_history() ||
                PlayerSelect_update_narrowcondition_has_features() || level_match();
            else if (ntype == 1) matched = contains(header->m_name);
            else if (ntype == 2) matched = contains(value_string(header->m_desc));
            else if (ntype == 3) matched = PlayerSelect_update_narrowcondition_has_history();
            else if (ntype == 4) matched = PlayerSelect_update_narrowcondition_has_features();
            else if (ntype == 5) matched = level_match();
        }
        if (matched) m_typed_list.push_back(header);
    }

    m_list.clear();
    for (const auto& header : m_typed_list)
        m_list.emplace_back(Value(std::static_pointer_cast<::cw::lib::Object>(header)));
    if (selected) {
        const auto iter = std::find(m_typed_list.begin(), m_typed_list.end(), selected);
        if (iter != m_typed_list.end()) m_index = static_cast<Integer>(iter - m_typed_list.begin());
        else if (!m_typed_list.empty()) m_index = wrap_index(m_index, static_cast<Integer>(m_typed_list.size()));
        else m_index = 0;
    } else if (m_typed_list.empty()) {
        m_index = 0;
    } else {
        m_index = wrap_index(m_index, static_cast<Integer>(m_typed_list.size()));
    }
    enable_btn();
}

void PlayerSelect::OnNumberKeyDown(std::shared_ptr<::cw::lib::Window> arg_event)
{
    if (m__processing || !arg_event) return;
    Integer index = arg_event->id;
    for (std::size_t i = 0; i < m_sortkeydown.size(); ++i) {
        if (value_integer(m_sortkeydown[i], -1) == arg_event->id) {
            index = static_cast<Integer>(i);
            break;
        }
    }
    if (0 <= index && index <= 2) {
        m_sort = Value(index == 1 ? String("Name") : index == 2 ? String("Level") : String("None"));
        OnSort(arg_event);
    }
}

void PlayerSelect::enable_btn()
{
    const auto add = object_value<::cw::lib::Window>(m_addbtn);
    const auto info = object_value<::cw::lib::Window>(m_infobtn);
    const auto fresh = object_value<::cw::lib::Window>(m_newbtn);
    const auto extension = object_value<::cw::lib::Window>(m_exbtn);
    const auto view = object_value<::cw::lib::Window>(m_viewbtn);
    const auto close = object_value<::cw::lib::Window>(m_closebtn);
    const bool party_full = ::cw::g_cwpy && ::cw::g_cwpy->m_pcards.size() >= 6;
    const bool has_list = !m_typed_list.empty();
    set_enabled(add, has_list && !party_full);
    set_enabled(info, has_list);
    if (!has_list) {
        set_enabled(fresh, true);
        set_enabled(close, true);
        set_enabled(extension, true);
        set_enabled(view, true);
        set_enabled(m_leftbtn, false); set_enabled(m_left2btn, false);
        set_enabled(m_rightbtn, false); set_enabled(m_right2btn, false);
    } else {
        set_enabled(fresh, true);
        set_enabled(close, true);
        set_enabled(extension, true);
        set_enabled(view, true);
        const bool can_page = m_typed_list.size() > static_cast<std::size_t>(std::max<Integer>(1, m_views));
        set_enabled(m_leftbtn, can_page); set_enabled(m_left2btn, can_page);
        set_enabled(m_rightbtn, can_page); set_enabled(m_right2btn, can_page);
    }
}

void PlayerSelect::OnSort(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    if (m__processing || m_isalbum) return;
    const String sort = value_string(m_sort, "None");
    if (::cw::g_cwpy && ::cw::g_cwpy->m_setting &&
        ::cw::g_cwpy->m_setting->m_sort_standbys == sort) return;
    select_feedback(this, SelectUiKind::player, "page");
    if (::cw::g_cwpy && ::cw::g_cwpy->m_setting) ::cw::g_cwpy->m_setting->m_sort_standbys = sort;
    if (::cw::g_cwpy && ::cw::g_cwpy->m_ydata && *::cw::g_cwpy->m_ydata)
        (*::cw::g_cwpy->m_ydata)->sort_standbys();
    update_narrowcondition();
    draw(true);
}

bool PlayerSelect::can_clickcenter()
{
    const bool add_enabled = is_enabled(object_value<::cw::lib::Window>(m_addbtn));
    std::shared_ptr<::cw::data::YadoData> ydata;
    if (::cw::g_cwpy && ::cw::g_cwpy->m_ydata && *::cw::g_cwpy->m_ydata) ydata = *::cw::g_cwpy->m_ydata;
    return add_enabled || (ydata ? ydata->m_standbys.empty() : m_source_list.empty());
}

bool PlayerSelect::can_clickside()
{
    return m_views <= 1;
}

void PlayerSelect::OnLeftDClick(std::shared_ptr<::cw::lib::Window> arg_event)
{
    if (m_finished || m__processing || m_isalbum ||
        (::cw::g_cwpy && ::cw::g_cwpy->m_pcards.size() >= 6)) return;
    if (m_views > 1 && !m_typed_list.empty() && m_clickmode == 0 &&
        is_enabled(object_value<::cw::lib::Window>(m_addbtn))) {
        OnClickAddBtn(nullptr);
    } else if (m_views <= 1 && m_clickmode == kLeft) {
        OnClickLeftBtn(arg_event);
    } else if (m_views <= 1 && m_clickmode == kRight) {
        OnClickRightBtn(arg_event);
    }
}

void PlayerSelect::OnMouseWheel(std::shared_ptr<::cw::lib::Window> arg_event)
{
    if (m_finished || m__processing || m_typed_list.size() <= 1 || !event_wheel(arg_event)) return;
    const Integer step = m_typed_list.size() <= static_cast<std::size_t>(m_views) ? 1 : std::max<Integer>(1, m_views);
    const Integer domain = m_typed_list.size() <= static_cast<std::size_t>(m_views) ?
        static_cast<Integer>(m_typed_list.size()) : ((static_cast<Integer>(m_typed_list.size()) + m_views - 1) / m_views) * m_views;
    const Integer delta = event_wheel(arg_event) > 0 ? -step : step;
    m_index = wrap_index(m_index + delta, domain);
    if (m_index >= static_cast<Integer>(m_typed_list.size())) m_index = static_cast<Integer>(m_typed_list.size()) - 1;
    select_play_sound("page");
    draw(true);
}

void PlayerSelect::OnClickLeftBtn(std::shared_ptr<::cw::lib::Window> arg_event)
{
    if (m_finished || m__processing || m_typed_list.size() <= 1) return;
    const Integer count = static_cast<Integer>(m_typed_list.size());
    const bool real_button = is_native_button_event(arg_event, m_leftbtn);
    const Integer step = real_button && m_views > 1 && count > m_views ? m_views : 1;
    const Integer domain = step == 1 ? count : get_pagecount() * m_views;
    m_index = std::min(count - 1, wrap_index(m_index - step, domain));
    enable_btn();
    select_play_sound("page");
    draw(true);
}

void PlayerSelect::OnClickRightBtn(std::shared_ptr<::cw::lib::Window> arg_event)
{
    if (m_finished || m__processing || m_typed_list.size() <= 1) return;
    const Integer count = static_cast<Integer>(m_typed_list.size());
    const bool real_button = is_native_button_event(arg_event, m_rightbtn);
    const Integer step = real_button && m_views > 1 && count > m_views ? m_views : 1;
    const Integer domain = step == 1 ? count : get_pagecount() * m_views;
    m_index = std::min(count - 1, wrap_index(m_index + step, domain));
    enable_btn();
    select_play_sound("page");
    draw(true);
}

void PlayerSelect::OnClickLeft2Btn(std::shared_ptr<::cw::lib::Window> arg_event)
{
    if (m_finished || m__processing || m_typed_list.size() <= 1) return;
    const Integer count = static_cast<Integer>(m_typed_list.size());
    const bool real_button = is_native_button_event(arg_event, m_left2btn);
    if (!real_button || m_views <= 1 || count <= m_views) {
        m_index = m_index == 0 ? count - 1 : std::max<Integer>(0, m_index - 10);
    } else if (get_page() == 0) {
        m_index = count - 1;
    } else {
        m_index = std::max<Integer>(0, m_index - m_views * m_max_views);
    }
    enable_btn();
    select_play_sound("page");
    draw(true);
}

void PlayerSelect::OnClickRight2Btn(std::shared_ptr<::cw::lib::Window> arg_event)
{
    if (m_finished || m__processing || m_typed_list.size() <= 1) return;
    const Integer count = static_cast<Integer>(m_typed_list.size());
    const bool real_button = is_native_button_event(arg_event, m_right2btn);
    if (!real_button || m_views <= 1 || count <= m_views) {
        m_index = m_index == count - 1 ? 0 : std::min<Integer>(count - 1, m_index + 10);
    } else if (get_page() == get_pagecount() - 1) {
        m_index = 0;
    } else {
        m_index = std::min<Integer>(count - 1, m_index + m_views * m_max_views);
    }
    enable_btn();
    select_play_sound("page");
    draw(true);
}

void PlayerSelect::OnClickViewBtn(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    if (m_finished || m__processing || m_isalbum) return;
    change_view();
    select_play_sound("equipment");
    enable_btn();
    draw(true);
}

void PlayerSelect::change_view()
{
    m_views = m_views == 1 ? m_max_views : 1;
    if (m_typed_list.empty()) m_index = 0;
    else m_index = std::clamp<Integer>(m_index, 0, static_cast<Integer>(m_typed_list.size()) - 1);
    save_views(m_views > 1);
}

Integer PlayerSelect::get_page()
{
    return m_views > 0 ? m_index / m_views : 0;
}

Integer PlayerSelect::get_pagecount()
{
    if (m_typed_list.empty()) return 0;
    const Integer views = std::max<Integer>(1, m_views);
    return (static_cast<Integer>(m_typed_list.size()) + views - 1) / views;
}

void PlayerSelect::OnSelect(std::shared_ptr<::cw::lib::Event> arg_event)
{
    (void)arg_event;
    if (m_finished || m__processing || m_isalbum) return;
    const bool add_enabled = is_enabled(object_value<::cw::lib::Window>(m_addbtn));
    std::shared_ptr<::cw::data::YadoData> ydata;
    if (::cw::g_cwpy && ::cw::g_cwpy->m_ydata && *::cw::g_cwpy->m_ydata) ydata = *::cw::g_cwpy->m_ydata;
    if (m_typed_list.empty()) {
        // 宿にキャラクターがいない場合は作成画面を開くが、絞り込み結果が空の場合は開かない。
        if ((ydata ? ydata->m_standbys.empty() : m_source_list.empty()) &&
            is_enabled(object_value<::cw::lib::Window>(m_newbtn))) OnClickNewBtn(nullptr);
        return;
    }
    if (m_views > 1) { (void)_select_player_in_current_page(); return; }
    // Python と同様、一人表示の張り紙では Add を実行し、編入して
    // も選択画面は閉じない。 ボタンとダブルクリックで同じ操作処理を使う。
    if (add_enabled) OnClickAddBtn(nullptr);
}

bool PlayerSelect::_select_player_in_current_page()
{
    if (m_typed_list.empty()) return false;
    // 現在のm_indexを一覧の範囲へ循環補正する。同じ位置な
    // ら何もせず、補正が必要な場合だけ共通の選択変更処理を呼ぶ。
    const Integer candidate = wrap_index(m_index, static_cast<Integer>(m_typed_list.size()));
    if (candidate == m_index) return true;
    return _change_player_in_current_page(candidate);
}

// Py は一件表示で項目が変わると false を返すが、この旧ネイティブ
// ヘルパーはその表示も直接更新できる。 内部の呼び出し元は _select_player_in_current_page
// であり、現在位置が有効ならこのヘルパーの前に戻
// る。 共通表示側はこのヘルパーを直接呼ばない。
bool PlayerSelect::_change_player_in_current_page(::cw::lib::Integer arg_index)
{
    if (arg_index < 0 || arg_index >= static_cast<Integer>(m_typed_list.size())) return false;
    if (arg_index == m_index) return true;
    if (m_views > 1 && m_index / m_views != arg_index / m_views) return false;
    m_index = arg_index;
    enable_btn();
    draw(true);
    return true;
}

void PlayerSelect::OnClickNewBtn(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    if (m_finished || m__processing || m_isalbum) return;
    select_play_sound("click");
    const bool debug = ::cw::g_cwpy && ::cw::g_cwpy->m_setting
        ? ::cw::g_cwpy->m_setting->m_debug : select_debug_mode(m_cwpy_debug);
    if (!debug) {
        _create_normal();
        return;
    }
    const auto weak_self = weak_from_this();
    const std::weak_ptr<int> lifetime = m_debug_lifetime;
    std::vector<Value> items;
    const auto add_item = [&](const String& name, const String& description,
                              ::cw::lib::Callback callback) {
        auto item = std::make_shared<::cw::dialog::etc::ExtensionItem>();
        item->m_name = name;
        item->m_description = description;
        item->m_callback = std::move(callback);
        items.emplace_back(Value(std::static_pointer_cast<::cw::lib::Object>(item)));
    };
    add_item(select_msg("create_normal", "通常作成"),
             select_msg("create_normal_description", "通常の作成画面を開きます。"),
             [weak_self, lifetime](const ::cw::lib::Arguments&,
                    const ::cw::lib::KeywordArguments&) -> Value {
                 if (const auto self = weak_self.lock(); self && !lifetime.expired() &&
                     !self->m_finished && !self->m__processing && !self->m_isalbum) self->_create_normal();
                 return {};
             });
    add_item(select_msg("create_debug", "デバッグ作成"),
             select_msg("create_debug_description", "デバッグ用の作成画面を開きます。"),
             [weak_self, lifetime](const ::cw::lib::Arguments&,
                    const ::cw::lib::KeywordArguments&) -> Value {
                 if (const auto self = weak_self.lock(); self && !lifetime.expired() &&
                     !self->m_finished && !self->m__processing && !self->m_isalbum) self->_create_debug();
                 return {};
             });
    auto dialog = std::make_shared<::cw::dialog::etc::ExtensionDialog>();
    dialog->initialize(m_toppanel_window,
                       select_msg("select_creator", "作成方法を選択"),
                       std::move(items));
    select_run_etc_modal(dialog);
}

void PlayerSelect::_create_normal()
{
    if (!::cw::g_cwpy || !::cw::g_cwpy->m_ydata || !*::cw::g_cwpy->m_ydata) return;
    auto dialog = std::make_shared<::cw::dialog::create::AdventurerCreater>();
    dialog->initialize(m_toppanel_window);
    (void)::cw::dialog::show_common_modal_presentation(dialog);
    const String path = value_string(dialog->m_fpath);
    if (!path.empty()) {
        select_play_sound("page");
        _create_common(Value(path));
    }
}

void PlayerSelect::_create_debug()
{
    if (!::cw::g_cwpy || !::cw::g_cwpy->m_ydata || !*::cw::g_cwpy->m_ydata) return;
    auto dialog = std::make_shared<::cw::debug::charaedit::CharacterEditDialog>();
    dialog->initialize(m_toppanel_window, -1, true);
    // 他の CharacterEditDialog の呼び出し側と同様、initialize がこのモーダル
    // を管理する。 完了済みモデルを再表示すると、一瞬 PlayerSelect の上に載せ直されてしまう。
    const String path = value_string(dialog->m_fpath);
    if (!path.empty()) {
        select_play_sound("page");
        _create_common(Value(path));
    }
}

void PlayerSelect::_create_common(::cw::lib::Value arg_dlg)
{
    const String path = value_string(arg_dlg);
    if (path.empty() || !::cw::g_cwpy || !::cw::g_cwpy->m_ydata || !*::cw::g_cwpy->m_ydata) return;
    // Python の FileSync と同様、YadoData.add_standbys
    // が戻る前に顔画像素材のコピーを完了する。 C++ への移植でもこの順序を維持する。
    if (::cw::g_fsync) ::cw::g_fsync->sync();
    const auto header = (*::cw::g_cwpy->m_ydata)->add_standbys(path, true);
    if (!header) return;
    m__player_image_cache.erase(value_string(header->m_fpath));
    (*::cw::g_cwpy->m_ydata)->changed();
    m_source_list = (*::cw::g_cwpy->m_ydata)->m_standbys;
    update_narrowcondition();
    const auto iter = std::find(m_typed_list.begin(), m_typed_list.end(), header);
    if (iter != m_typed_list.end()) m_index = static_cast<Integer>(iter - m_typed_list.begin());
    enable_btn();
    draw(true);
}

std::optional<std::shared_ptr<::cw::header::AdventurerHeader>> PlayerSelect::get_selected()
{
    if (m_typed_list.empty() || m_index < 0 || m_index >= static_cast<Integer>(m_typed_list.size())) return std::nullopt;
    return m_typed_list[static_cast<std::size_t>(m_index)];
}

void PlayerSelect::update_standbys(std::shared_ptr<::cw::header::AdventurerHeader> arg_selected)
{
    update_narrowcondition();
    if (arg_selected) {
        const auto iter = std::find(m_typed_list.begin(), m_typed_list.end(), arg_selected);
        if (iter != m_typed_list.end()) m_index = static_cast<Integer>(iter - m_typed_list.begin());
    }
    if (!m_typed_list.empty()) m_index = wrap_index(m_index, static_cast<Integer>(m_typed_list.size()));
    else m_index = 0;
    enable_btn();
    draw(true);
}

void PlayerSelect::OnClickAddBtn(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    if (m_finished || m_isalbum || m__processing || m_typed_list.empty() || !::cw::g_cwpy ||
        m_index < 0 || m_index >= static_cast<Integer>(m_typed_list.size())) return;

    const auto frame=object_value<::cw::frame::Frame>(::cw::g_cwpy->m_frame);
    const auto header=m_typed_list[static_cast<std::size_t>(m_index)];
    queue_enrollment(frame,[](auto callback) {::cw::g_cwpy->exec_func(std::move(callback));},
        [header] {return PlayerSelect::_add(header);});
}

bool PlayerSelect::queue_enrollment(const std::shared_ptr<::cw::frame::Frame>& frame,
    const std::function<void(::cw::lib::Callback)>& post_game,std::function<bool()> work)
{
    if(m_isalbum) return false;
    return queue_selector_work(frame,post_game,std::move(work),
        [](PlayerSelect& self) {self.update_standbys(nullptr);});
}

bool PlayerSelect::queue_character_refresh(const std::shared_ptr<::cw::frame::Frame>& frame,
    const std::function<void(::cw::lib::Callback)>& post_game,
    std::shared_ptr<::cw::header::AdventurerHeader> original,
    std::function<std::shared_ptr<::cw::header::AdventurerHeader>()> work)
{
    if(!original || !work) return false;
    const auto replacement=std::make_shared<std::shared_ptr<::cw::header::AdventurerHeader>>();
    return queue_selector_work(frame,post_game,
        [replacement,work=std::move(work)] { *replacement=work();return static_cast<bool>(*replacement); },
        [original=std::move(original),replacement](PlayerSelect& self) {
            // 選択画面の vector と顔画像キャッシュは、Frame スレッドだけが操作する。
            self.m__player_image_cache.erase(value_string(original->m_fpath));
            if(*replacement) {
                std::replace(self.m_source_list.begin(),self.m_source_list.end(),original,*replacement);
                std::replace(self.m_typed_list.begin(),self.m_typed_list.end(),original,*replacement);
            }
            self.update_standbys(*replacement ? *replacement : original);
        });
}

bool PlayerSelect::queue_selector_work(const std::shared_ptr<::cw::frame::Frame>& frame,
    const std::function<void(::cw::lib::Callback)>& post_game,std::function<bool()> work,
    std::function<void(PlayerSelect&)> finish)
{
    if(m_finished || m__processing || !frame || !frame->m_native_adapter || !post_game || !work || !finish) return false;
    const auto weak_self=weak_from_this();
    if(weak_self.expired()) return false;
    const std::weak_ptr<::cw::frame::Frame> weak_frame = frame;
    auto token=std::make_shared<std::atomic_bool>(false);
    m_enrollment_token=token;
    const std::weak_ptr<std::atomic_bool> weak_token=token;
    m__processing = true;
    request_dialog_presentation_refresh();
    try { post_game([weak_self,weak_frame,weak_token,work=std::move(work),finish=std::move(finish)](const auto&, const auto&) {
        const auto token=weak_token.lock();
        const auto frame=weak_frame.lock();
        if(!token || !frame || weak_self.expired() || token->exchange(true)) return Value{};
        String error_message;
        try {
            (void)work();
        } catch (const std::exception& error) {
            error_message = error.what();
        } catch (...) {
            error_message = "不明なエラーが発生しました。";
        }

        frame->post_func([weak_self,weak_frame,token,error_message,finish](const auto&, const auto&) mutable {
            const auto self=weak_self.lock();
            if(!self || self->m_enrollment_token!=token) return Value{};
            self->m_enrollment_token.reset();
            self->m__processing=false;
            self->request_dialog_presentation_refresh();
            if(self->m_finished) return Value{};
            try { finish(*self); }
            catch(const std::exception& error) { error_message += String(error_message.empty()?"":"\n")+error.what(); }
            catch(...) { error_message += "\n画面の更新に失敗しました。"; }
            // 変更は CWPy、更新・通知は必ず Frame で行う。
            // 子のエラーモーダルへ入る前に、処理中状態は解除済みとする。
            if(!error_message.empty()) {
                select_play_sound("error");
                (void)::cw::dialog::message::show_common_modal_message(weak_frame.lock(),
                    "CWPy内部エラー",error_message,false,true);
            }
            return Value{};
        });
        return ::cw::lib::Value{};
    }); } catch(...) {
        m_enrollment_token.reset();m__processing=false;
        request_dialog_presentation_refresh();
        throw;
    }
    return true;
}

bool PlayerSelect::_add(std::shared_ptr<::cw::header::AdventurerHeader> arg_header)
{
    if (!arg_header || !::cw::g_cwpy || !::cw::g_cwpy->m_ydata || !*::cw::g_cwpy->m_ydata) return false;
    const auto ydata = *::cw::g_cwpy->m_ydata;
    const auto party=ydata->m_party.value_or(nullptr);
    if(party && party->m_members.size()>=6) return false;
    if(std::find(ydata->m_standbys.begin(),ydata->m_standbys.end(),arg_header)==ydata->m_standbys.end()) return false;
    const auto path=value_string(arg_header->m_fpath);
    const auto data=::cw::data::yadoxml2etree(path,"",std::nullopt);
    if(!data || !data->getroot() || data->getroot()->get_tag()!="Adventurer")
        throw std::runtime_error("冒険者のデータを読み込めません: "+path);
    // 実際にメンバーを追加できるまで、待機一覧から削除しない。 戻り値のない add()
    // では失敗を見逃して編入成功と報告する可能性があるため、それを避ける。
    const auto stem=std::filesystem::u8path(path).stem();
    const auto enrolled=[&]() {
        const auto current=ydata->m_party.value_or(nullptr);
        if(!current) return false;
        return std::any_of(current->m_members.begin(),current->m_members.end(),[&](const auto& value) {
            const auto tree=object_value<::cw::data::CWPyElementTree>(value);
            return tree && (tree==data || (!tree->m_fpath.empty() && std::filesystem::u8path(tree->m_fpath).stem()==stem));
        });
    };
    const auto commit_standby=[&]() {
        ydata->m_standbys.erase(std::remove(ydata->m_standbys.begin(),ydata->m_standbys.end(),arg_header),ydata->m_standbys.end());
        ydata->changed();
    };
    const auto previous_money=ydata->m_money;
    bool added=false;
    try {
        if(party) added=party->add(arg_header,data);
        else { ydata->create_party(arg_header,false); added=enrolled(); }
    } catch(...) {
        // 追加後のアニメーション・通知に失敗しても、待機一覧へメンバーを重複して戻
        // さない。 新しいパーティーの書き込みに失敗した場合は所持金を保持する。
        if(enrolled()) commit_standby();
        else if(!party) ydata->set_money(previous_money-ydata->m_money,false);
        throw;
    }
    if(!added) {
        if(!party) ydata->set_money(previous_money-ydata->m_money,false);
        return false;
    }
    commit_standby();
    select_play_sound("harvest");
    if(party && ::cw::g_cwpy->m_areaid==value_integer(::cw::g_AREA_BREAKUP,-1)) ::cw::g_cwpy->create_poschangearrow();
    return true;
}

void PlayerSelect::OnClickExBtn(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    if (m_finished || m_isalbum || m__processing) return;
    select_play_sound("click");
    const auto selected=get_selected();
    const bool has_selection=selected && *selected;
    const bool has_standbys =
        ::cw::g_cwpy && ::cw::g_cwpy->m_ydata && *::cw::g_cwpy->m_ydata &&
        !(*::cw::g_cwpy->m_ydata)->m_standbys.empty();
    m_extension_enabled = {
        has_selection, has_selection,
        static_cast<bool>(::cw::g_cwpy && ::cw::g_cwpy->m_ydata && *::cw::g_cwpy->m_ydata &&
                          ((*::cw::g_cwpy->m_ydata)->m_party || !(*::cw::g_cwpy->m_ydata)->m_partyrecord.empty())),
        true,
        has_standbys && is_enabled(object_value<::cw::lib::Window>(m_addbtn))
    };
    const String title = !has_selection
        ? select_msg("extension_title_2", "拡張機能")
        : select_msg_format("extension_title", "拡張機能 - %sの編集",
                            (*selected)->m_name);
    std::vector<Value> items;
    const auto add_item = [&](const String& name, const String& description,
                              ::cw::lib::Callback callback, const bool enabled) {
        auto item = std::make_shared<::cw::dialog::etc::ExtensionItem>();
        item->m_name = name;
        item->m_description = description;
        item->m_callback = std::move(callback);
        item->m_enabled = enabled;
        items.emplace_back(Value(std::static_pointer_cast<::cw::lib::Object>(item)));
    };
    const auto callback = [weak=weak_from_this()](auto method) {
        return [weak, method](const ::cw::lib::Arguments&,
                              const ::cw::lib::KeywordArguments&) -> Value {
            if(const auto self=weak.lock(); self && !self->m_finished && !self->m__processing)
                (self.get()->*method)();
            return {};
        };
    };
    add_item(select_msg("grow", "成長"),
             select_msg("grow_adventurer_description", "キャラクターを成長させ、年代を進めます。"),
             callback(&PlayerSelect::grow_adventurer), m_extension_enabled[0]);
    add_item(select_msg("delete", "削除"),
             select_msg("delete_adventurer_description", "選択中のキャラクターを削除します。"),
             callback(&PlayerSelect::delete_adventurer), m_extension_enabled[1]);
    add_item(select_msg("select_party_record", "パーティ記録"),
             select_msg("select_party_record_description", "保存されているパーティ記録を選択します。"),
             callback(&PlayerSelect::select_partyrecord), m_extension_enabled[2]);
    add_item(select_msg("random_character", "ランダム冒険者"),
             select_msg("random_character_description", "ランダムなキャラクターを作成します。"),
             callback(&PlayerSelect::create_randomadventurer), m_extension_enabled[3]);
    add_item(select_msg("random_team", "ランダムチーム"),
             select_msg("random_team_description", "待機中のメンバーからパーティを自動編成します。"),
             callback(&PlayerSelect::random_team), m_extension_enabled[4]);
    auto dialog = std::make_shared<::cw::dialog::etc::ExtensionDialog>();
    dialog->initialize(m_toppanel_window, title, std::move(items));
    select_run_etc_modal(dialog);
    enable_btn();
    draw(true);
}

void PlayerSelect::grow_adventurer()
{
    if (m_finished || m__processing || m_isalbum) return;
    const auto selected = get_selected();
    if (!selected || !*selected || (*selected)->m_album) return;
    const String age = value_string((*selected)->m_age);
    const auto period_it = std::find(::cw::setting::Setting::s_periodcoupons.begin(),
                                     ::cw::setting::Setting::s_periodcoupons.end(), age);
    if (period_it == ::cw::setting::Setting::s_periodcoupons.end()) {
        throw std::invalid_argument("age coupon is not in configured periods");
    }
    const bool final_age =
        std::next(period_it) == ::cw::setting::Setting::s_periodcoupons.end();
    String prompt = final_age
        ? select_msg_format("confirm_die", "「%s」を引退させますか？", (*selected)->m_name)
        : select_msg("confirm_grow", "%sを%sから%sへ成長させます。よろしいですか？");
    if (!final_age) {
        const auto display_age=[](const String& coupon) {
            const String prefix="＿";
            return coupon.starts_with(prefix)?coupon.substr(prefix.size()):coupon;
        };
        std::size_t offset=0;
        for (const auto& value : {(*selected)->m_name, display_age(age), display_age(*std::next(period_it))}) {
            const auto marker=prompt.find("%s",offset);
            if(marker==String::npos) break;
            prompt.replace(marker,2,value); offset=marker+value.size();
        }
    }
    run_adventurer_change(*selected,
        final_age ? AdventurerChange::Retire : AdventurerChange::Grow, prompt);
}

void PlayerSelect::delete_adventurer()
{
    if (m_finished || m__processing) return;
    const auto selected = get_selected();
    if (!selected || !*selected || m_isalbum || (*selected)->m_album) return;
    run_adventurer_change(*selected, AdventurerChange::Delete,
        select_msg_format("confirm_delete_character", "「%s」を削除しますか？", (*selected)->m_name));
}

void PlayerSelect::run_adventurer_change(
    const std::shared_ptr<::cw::header::AdventurerHeader>& header,
    AdventurerChange change, const String& prompt)
{
    const auto token = std::make_shared<int>(0);
    m_adventurer_change_token = token;
    m__processing = true;
    const auto age = value_string(header->m_age);
    const auto current = [&] {
        const auto selected = get_selected();
        return m_adventurer_change_token == token && !m_finished &&
               selected && *selected == header && value_string(header->m_age) == age;
    };
    const auto release = [&] {
        if (m_adventurer_change_token != token) return;
        m_adventurer_change_token.reset();
        m__processing = false;
    };
    try {
        if (!select_confirm(select_msg("message", "メッセージ"), prompt) || !current()) {
            release();
            return;
        }
        select_play_sound(change == AdventurerChange::Delete || change == AdventurerChange::DeleteAlbum
            ? "dump" : "harvest");
        if (change == AdventurerChange::Retire) {
            // Python と同様、カードの移動・メンバー削除より前に通常のモー
            // ド 2 のメッセージを表示する。 表示経路は共通の一つだけを使う。
            auto notice = std::make_shared<::cw::dialog::message::Message>();
            notice->initialize(m_toppanel_window, select_msg("message", "メッセージ"),
                select_msg_format("die_message", "「%s」は引退しました。", header->m_name), 2, std::nullopt);
            if (!::cw::dialog::show_common_modal_presentation(notice) || !notice->m_closed || !current()) {
                release();
                return;
            }
        }
        const bool committed = commit_adventurer_change(header, change);
        const bool alive = m_adventurer_change_token == token && !m_finished;
        release();
        if (committed && alive) {
            enable_btn();
            draw(true);
            // 成長処理の同期操作の保護を解除してから、ヘッダー更新を予約する。
            if(change == AdventurerChange::Grow) update_character();
        }
    } catch (const std::exception& error) {
        const bool alive = m_adventurer_change_token == token && !m_finished;
        release();
        if (alive) {
            enable_btn();
            draw(true);
            (void)::cw::dialog::message::show_common_modal_message(
                select_msg("error", "エラー"), error.what(), false, true);
        }
    } catch (...) {
        release();
        throw;
    }
}

bool PlayerSelect::commit_adventurer_change(
    const std::shared_ptr<::cw::header::AdventurerHeader>& header, AdventurerChange change)
{
    if (change == AdventurerChange::DeleteAlbum || !header || header->m_album ||
        !::cw::g_cwpy || !::cw::g_cwpy->m_ydata ||
        !*::cw::g_cwpy->m_ydata) return false;
    const auto ydata = *::cw::g_cwpy->m_ydata;
    if (std::find(ydata->m_standbys.begin(), ydata->m_standbys.end(), header) == ydata->m_standbys.end())
        return false; // 子ダイアログを開いている間に、宿やメンバーが変わっている場合がある。
    if (change == AdventurerChange::Grow) {
        header->grow();
    } else {
        _delete_adventurer(header, change == AdventurerChange::Retire);
    }
    return true;
}

void PlayerSelect::_move_allcards(std::shared_ptr<::cw::header::AdventurerHeader> arg_header)
{
    if (!arg_header || !::cw::g_cwpy || !::cw::g_cwpy->m_ydata || !*::cw::g_cwpy->m_ydata)
        throw std::runtime_error("冒険者のカードを移動する宿がありません。");
    const String path = value_string(arg_header->m_fpath);
    if (path.empty()) throw std::runtime_error("冒険者のファイルパスがありません。");
    const auto tree = ::cw::data::yadoxml2etree(path, "", std::nullopt);
    if (!tree || !tree->getroot() || tree->getroot()->m_tag != "Adventurer")
        throw std::runtime_error("冒険者を読み込めないため、削除・引退を中止しました: " + path);
    auto character = std::make_shared<::cw::character::Character>();
    character->initialize(tree);
    const auto pockets = character->m_cardpocket_typed; // 取引中は、Python の pocket[:] と同じスナップショットを維持する。
    auto move = [&](const std::vector<std::shared_ptr<::cw::header::CardHeader>>& cards) {
        for (const auto& card : cards) {
            if (!card) continue;
            ::cw::g_cwpy->trade("STOREHOUSE", std::nullopt, card, true, nullptr, -1, -1,
                                false, false, std::nullopt, false, false, true, true, 1);
        }
    };
    move(std::get<0>(pockets));
    move(std::get<1>(pockets));
    move(std::get<2>(pockets));
    (*::cw::g_cwpy->m_ydata)->sort_storehouse(std::nullopt);
}

// Py と同様、冒険者のカードを移動する前に宿を変更済みにする。
// 最初のメンバー XML の読み取りで失敗し、trade 自
// 身の通知に届かなくても、この削除の試行を保存対象として残す。
void PlayerSelect::_delete_adventurer(std::shared_ptr<::cw::header::AdventurerHeader> arg_header,
                                      bool retirement)
{
    if (!arg_header || !::cw::g_cwpy || !::cw::g_cwpy->m_ydata || !*::cw::g_cwpy->m_ydata) return;
    const auto ydata = *::cw::g_cwpy->m_ydata;
    ydata->changed();
    _move_allcards(arg_header);
    if ((retirement || value_integer(arg_header->m_level) >= 3) && !arg_header->m_leavenoalbum) {
        const auto album_path = ::cw::xmlcreater::create_albumpage(value_string(arg_header->m_fpath), false, !retirement);
        if (!album_path.empty()) ydata->add_album(album_path);
    }
    for (const auto& record : ydata->m_partyrecord) if (record) record->vanish_member(value_string(arg_header->m_fpath));
    ydata->remove_emptypartyrecord();
    // Python と同様、削除するのは冒険者の Property だけとし、カード置場へ移動したカードの素材は削除しない。
    ::cw::g_cwpy->remove_xml(Value(std::static_pointer_cast<::cw::lib::Object>(arg_header)));
    auto& source = m_isalbum ? ydata->m_album : ydata->m_standbys;
    source.erase(std::remove(source.begin(), source.end(), arg_header), source.end());
    // Py と同様、冒険者を削除した後に二巡目を行う。vanish_member
    // は一回で一致する一枠だけを除くため、 XML に同じメンバーが
    // 二度記録された場合、削除・引退の両方でこの処理が結果に影響する。
    for (const auto& record : ydata->m_partyrecord)
        if (record) record->vanish_member(value_string(arg_header->m_fpath));
    ydata->remove_emptypartyrecord();
    ydata->changed();
    update_narrowcondition();
    if (!m_typed_list.empty()) m_index = wrap_index(m_index, static_cast<Integer>(m_typed_list.size()));
    else m_index = 0;
}

void PlayerSelect::select_partyrecord()
{
    if (m__processing || !::cw::g_cwpy || !::cw::g_cwpy->m_ydata || !*::cw::g_cwpy->m_ydata) return;
    select_play_sound("click");
    auto dialog = std::make_shared<::cw::dialog::partyrecord::SelectPartyRecord>();
    const auto self=weak_from_this().lock();
    dialog->initialize(self ? Value(std::static_pointer_cast<::cw::lib::Object>(self)) : Value(m_toppanel_window));
    const auto ydata = *::cw::g_cwpy->m_ydata;
    dialog->set_records(ydata->m_partyrecord, ydata->m_party.has_value() && *ydata->m_party);
    try { (void)::cw::dialog::show_common_modal_presentation(dialog); }
    catch(...) { dialog->set_parent_callbacks({}, {}, {}, {}); m__processing=false; throw; }
    dialog->set_parent_callbacks({}, {}, {}, {});
    m__processing=false;
    draw(true);
}

void PlayerSelect::random_team()
{
    if (m_finished || m_isalbum || m__processing || !::cw::g_cwpy || !::cw::g_cwpy->m_ydata || !*::cw::g_cwpy->m_ydata) return;
    const auto frame=object_value<::cw::frame::Frame>(::cw::g_cwpy->m_frame);
    queue_enrollment(frame,[](auto callback) {::cw::g_cwpy->exec_func(std::move(callback));},
        [] {return PlayerSelect::_random_team();});
}

bool PlayerSelect::_random_team()
{
    if(!::cw::g_cwpy || !::cw::g_cwpy->m_ydata || !*::cw::g_cwpy->m_ydata) return false;
    auto ydata = *::cw::g_cwpy->m_ydata;
    bool added=false;
    const auto dice=::cw::g_cwpy->m_dice;
    if(!dice) throw std::runtime_error("ランダム編成の乱数機能が利用できません。");
    PlayerSelect calculator;
    while (!ydata->m_standbys.empty() && (!ydata->m_party.value_or(nullptr) || (*ydata->m_party)->m_members.size() < 6)) {
        std::shared_ptr<::cw::header::AdventurerHeader> selected;
        if (!ydata->m_party.value_or(nullptr)) {
            std::vector<Value> candidates;
            candidates.reserve(ydata->m_standbys.size());
            for (const auto& header : ydata->m_standbys)
                if (header) candidates.emplace_back(
                    Value(std::static_pointer_cast<::cw::lib::Object>(header)));
            if (!candidates.empty())
                selected = object_value<::cw::header::AdventurerHeader>(
                    dice->choice_exists(std::move(candidates)));
        } else {
            selected=choose_random_member(calculator.calc_needs(ydata->m_standbys),
                [dice](auto values) {return dice->shuffle(std::move(values));},
                [dice](Integer need) {return dice->roll(1,need);});
        }
        if (!selected || !_add(selected)) break;
        added=true;
    }
    return added;
}

std::shared_ptr<::cw::header::AdventurerHeader> PlayerSelect::choose_random_member(
    std::vector<std::tuple<Integer,std::shared_ptr<::cw::header::AdventurerHeader>>> needs,
    const std::function<std::vector<Value>(std::vector<Value>)>& shuffle,
    const std::function<Integer(Integer)>& roll)
{
    if(!shuffle || !roll) return {};
    std::vector<Value> indices;
    for(std::size_t i=0;i<needs.size();++i) if(std::get<1>(needs[i])) indices.emplace_back(static_cast<Integer>(i));
    std::shared_ptr<::cw::header::AdventurerHeader> selected;
    Integer best=std::numeric_limits<Integer>::max();
    // Python と同様、抽選前にシャッフルし、同じ点数は安定ソートで順序を維持する。
    for(const auto& value:shuffle(std::move(indices))) {
        const auto i=value_integer(value,-1);
        if(i<0 || static_cast<std::size_t>(i)>=needs.size()) continue;
        const auto& [need,header]=needs[static_cast<std::size_t>(i)];
        if(!header) continue;
        if(need<1) throw std::runtime_error("ランダム編成の必要度が不正です。");
        const auto point=roll(need);
        if(point<1 || point>need) throw std::runtime_error("ランダム編成の出目が範囲外です。");
        if(!selected || point<best) {selected=header;best=point;}
    }
    return selected;
}

// Python は生成した冒険者の命名・保存より先に harvest を鳴ら
// す。 ネイティブ版では書き込み失敗を作成成功のように伝えないため、確定した
// 名前の保存後に成功音を鳴らす。キャンセル時は従来どおり dump を使う。
void PlayerSelect::create_randomadventurer()
{
    if (m_finished || m_isalbum || m__processing || !::cw::g_cwpy || !::cw::g_cwpy->m_ydata || !*::cw::g_cwpy->m_ydata) return;
    m__processing = true;
    // プレビューは一時表示であり、ユーザーの一覧表示設定は変更しない。
    struct RestorePreview {
        PlayerSelect& panel;
        Integer views;
        ~RestorePreview() {
            panel.m_views=views;
            panel.m__processing=false;
            panel.request_dialog_presentation_refresh();
        }
    } restore{*this,m_views};
    select_play_sound("signal");
    auto info = std::make_shared<::cw::debug::charaedit::CharaInfo>();
    info->initialize(nullptr, false);
    info->set_randomfeatures();
    const String path = info->create_adventurer(false);
    auto ydata = *::cw::g_cwpy->m_ydata;
    m_views=1;
    auto header = ydata->add_standbys(path, true);
    if (header) {
        ydata->changed();
        m_narrow = String{};
        update_narrowcondition();
        const auto iter = std::find(m_typed_list.begin(), m_typed_list.end(), header);
        if (iter != m_typed_list.end()) m_index = static_cast<Integer>(iter - m_typed_list.begin());
        draw(true);
        const auto sex=header->get_sex();
        std::optional<::cw::lib::Callback> automatic;
        if(::cw::g_cwpy->m_setting && ::cw::g_cwpy->m_setting->m_show_autobuttoninentrydialog)
            automatic=[sex](const auto&,const auto&) {return Value(::cw::dialog::create::get_randomname(sex));};
        const auto name=input_random_name(::cw::dialog::create::get_randomname(sex),std::move(automatic));
        if (name) {
            const auto tree = ::cw::data::yadoxml2etree(
                value_string(header->m_fpath), "", std::nullopt);
            if (tree && !name->empty()) {
                auto character = std::make_shared<::cw::character::Character>();
                character->initialize(tree);
                character->set_name(*name);
                tree->write_xml(true);
                header->m_name = *name;
                ydata->changed();
                select_play_sound("harvest");
            }
        } else {
            select_play_sound("dump");
            _delete_adventurer(header);
        }
    }
    m_views=restore.views;
    m__processing = false;
    update_narrowcondition();
    enable_btn();
    draw(true);
}

std::optional<String> PlayerSelect::input_random_name(String initial,
    std::optional<::cw::lib::Callback> automatic)
{
    if(m_finished || m_isalbum) return std::nullopt;
    auto dialog=std::make_shared<::cw::dialog::edit::InputTextDialog>();
    const auto addition=automatic?select_msg("auto","自動"):String{};
    dialog->initialize(m_toppanel_window,select_msg("naming","命名"),
        select_msg("naming_random_character","冒険者の名前を入力してください。"),
        std::move(initial),14,addition,std::move(automatic));
    ::cw::dialog::set_dialog_center_offset(dialog, {130, 0});
    if(!::cw::dialog::show_common_modal_presentation(dialog) || !dialog->m_presentation_accepted)
        return std::nullopt;
    return dialog->m_text;
}

void PlayerSelect::OnClickInfoBtn(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    if (m_finished || m__processing || m_information_open_token || !get_selected().value_or(nullptr)) return;
    const auto token = std::make_shared<int>(0);
    m_information_open_token = token;
    try {
        select_play_sound("click");
        get_ui_backend()->open_player_information(this);
    }
    catch (...) {
        if (m_information_open_token == token) m_information_open_token.reset();
        throw;
    }
    // 入れ子のモーダルでこの選択画面が開き直された場合、新しい操作の状態を解除しない。
    if (m_information_open_token == token) m_information_open_token.reset();
}

std::function<void(std::shared_ptr<::cw::header::AdventurerHeader>)> PlayerSelect::information_selection_callback()
{
    const auto weak = weak_from_this();
    const std::weak_ptr<int> lifetime = m_information_lifetime;
    return [weak,lifetime](const std::shared_ptr<::cw::header::AdventurerHeader>& header) {
        const auto self = weak.lock();
        if (!self || lifetime.expired() || self->m_finished || !header) return;
        const auto path = value_string(header->m_fpath);
        const auto it = std::find_if(self->m_typed_list.begin(), self->m_typed_list.end(), [&](const auto& item) {
            return item == header || (item && !path.empty() && value_string(item->m_fpath) == path);
        });
        if (it == self->m_typed_list.end()) return;
        self->m_index = static_cast<Integer>(it - self->m_typed_list.begin());
        self->enable_btn();
        self->draw(true);
    };
}

::cw::lib::Callback PlayerSelect::information_refresh_callback()
{
    const auto weak = weak_from_this();
    const std::weak_ptr<int> lifetime = m_information_lifetime;
    return [weak,lifetime](const auto&,const auto&) -> Value {
        if (const auto self = weak.lock(); self && !lifetime.expired() && !self->m_finished)
            self->update_character();
        return {};
    };
}

void PlayerSelect::update_character()
{
    if(m_finished || m__processing) return;
    const auto selected = get_selected();
    if (!selected || !*selected || !::cw::g_cwpy || !::cw::g_cwpy->m_ydata || !*::cw::g_cwpy->m_ydata) return;
    const auto ydata = *::cw::g_cwpy->m_ydata;
    const auto frame=object_value<::cw::frame::Frame>(::cw::g_cwpy->m_frame);
    const auto original=*selected;
    const auto album=m_isalbum;
    const String path=value_string(original->m_fpath);
    try {
        if(!queue_character_refresh(frame,[](auto callback) {::cw::g_cwpy->exec_func(std::move(callback));},
            original,[ydata,original,album,path] {
                if(!::cw::g_cwpy || !::cw::g_cwpy->m_ydata || *::cw::g_cwpy->m_ydata!=ydata)
                    return std::shared_ptr<::cw::header::AdventurerHeader>{};
                auto& source=album ? ydata->m_album : ydata->m_standbys;
                if(std::find(source.begin(),source.end(),original)==source.end())
                    return std::shared_ptr<::cw::header::AdventurerHeader>{};
                auto replacement=ydata->create_advheader(path,album,std::nullopt);
                if(!replacement) throw std::runtime_error("人物情報を更新できません: "+path);
                replacement->m_order=original->m_order;
                // 読み込みで保留ファイルが同期される場合があるため、その前後にわたって vector のイテレーターを保持しない。
                const auto it=std::find(source.begin(),source.end(),original);
                if(!::cw::g_cwpy || !::cw::g_cwpy->m_ydata || *::cw::g_cwpy->m_ydata!=ydata || it==source.end())
                    return std::shared_ptr<::cw::header::AdventurerHeader>{};
                *it=replacement;
                return replacement;
            })) throw std::runtime_error("人物情報の更新を予約できません。");
    } catch(const std::exception& error) {
        throw;
    }
}

std::vector<std::tuple<::cw::lib::Integer, std::shared_ptr<::cw::header::AdventurerHeader>>> PlayerSelect::calc_needs(std::vector<std::shared_ptr<::cw::header::AdventurerHeader>> arg_mlist)
{
    std::shared_ptr<::cw::data::YadoData> ydata;
    if (::cw::g_cwpy && ::cw::g_cwpy->m_ydata && *::cw::g_cwpy->m_ydata) ydata = *::cw::g_cwpy->m_ydata;
    const auto party=ydata?ydata->m_party.value_or(nullptr):nullptr;
    std::vector<std::pair<Integer,String>> members;
    const auto pcards = ::cw::g_cwpy ? ::cw::g_cwpy->get_pcards(Value{}) : std::vector<std::shared_ptr<::cw::sprite::card::PlayerCard>>{};
    for (const auto& pcard : pcards) {
        if (!pcard) continue;
        members.emplace_back(pcard->::cw::character::Character::m_level,
            pcard->::cw::character::Character::get_talent());
    }
    return calculate_needs(arg_mlist,members,party?static_cast<Integer>(party->m_members.size()):0,
        ::cw::setting::Setting::s_naturecoupons);
}

std::vector<std::tuple<Integer,std::shared_ptr<::cw::header::AdventurerHeader>>> PlayerSelect::calculate_needs(
    const std::vector<std::shared_ptr<::cw::header::AdventurerHeader>>& candidates,
    const std::vector<std::pair<Integer,String>>& members,Integer member_count,const std::vector<String>& naturecoupons)
{
    std::vector<std::tuple<Integer,std::shared_ptr<::cw::header::AdventurerHeader>>> result;
    if(member_count<=0) {
        for(const auto& header:candidates) if(header) result.emplace_back(10,header);
        return result;
    }
    const std::set<String> talents(naturecoupons.begin(),naturecoupons.end());
    std::map<String,Integer> types;
    long double level_sum=0;
    for(const auto& [level,talent]:members) {level_sum+=level;++types[talent];}
    // Python と同様、ここでは //= を使う。候補ごとに小数の平均を丸める処理にはしない。
    const auto average=std::floor(level_sum/static_cast<long double>(member_count));
    for (const auto& header : candidates) {
        if (!header) continue;
        Integer need = 10;
        String talent = naturecoupons.empty() ? String{} : naturecoupons.front();
        for (const auto& coupon_value : header->m_history) {
            const String coupon = value_string(coupon_value);
            if (talents.count(coupon)) { talent = coupon; break; }
        }
        // Py の need は任意精度だが、この公開タプルは符号付き 64
        // ビットの Integer を持つ。 2 倍・4 倍で桁あふれする
        // 前に失敗させ、折り返してゼロ以下になった抽選の重みを公開しない。
        const auto multiply=[&](Integer factor) {
            if(need>std::numeric_limits<Integer>::max()/factor)
                throw std::overflow_error("ランダム編成の必要度が整数の上限を超えました。");
            need*=factor;
        };
        for (Integer i = 0; i < types[talent]; ++i) multiply(2);
        const auto distance=std::floor(std::abs(average-static_cast<long double>(value_integer(header->m_level,0)))+0.5L);
        // 大きな距離は検査付きの乗算で失敗させる。 整数の桁あふれ
        // で、低確率の候補が一面のサイコロにならないようにする。
        for (Integer i=0;static_cast<long double>(i)<distance;++i) multiply(4);
        result.emplace_back(need, header);
    }
    return result;
}

std::shared_ptr<::cw::lib::Bitmap> PlayerSelect::_get_bg()
{
    if (const auto* object = std::get_if<::cw::lib::ObjectPtr>(&m__bg.storage()))
        if (const auto bitmap = std::dynamic_pointer_cast<::cw::lib::Bitmap>(*object)) return bitmap;
    const String base = ::cw::g_cwpy && ::cw::g_cwpy->m_rsrc ? ::cw::g_cwpy->m_rsrc->m_skindir : String{};
    // Python の PlayerSelect と同様、一件表示と一覧表示
    // の両方で Table/Book を使う。 非標準の Book_Multi
    // を要求すると通常のスキンでは背景画像を読めず、一覧全体が黒くなる。
    const String name = "Table/Book";
    const String requested = base.empty() ? name : ::cw::lib::encoding::path_to_utf8(
        std::filesystem::u8path(base) / std::filesystem::u8path(name));
    const String resolved = ::cw::util::find_resource(requested, ::cw::g_M_IMG);
    const auto bitmap = get_ui_backend()->load_select_bitmap(resolved.empty() ? requested : resolved, false);
#if defined(_DEBUG)
    ::cardwirth::debug::log("PlayerSelect background: requested=" + requested + " resolved=" + resolved +
        " bitmap=" + (bitmap ? std::to_string(bitmap->size.width) + "x" + std::to_string(bitmap->size.height) : String("null")));
#endif
    if (bitmap) m__bg = Value(std::static_pointer_cast<::cw::lib::Object>(bitmap));
    return bitmap;
}

void PlayerSelect::draw(bool arg_update)
{
    (void)arg_update;
    m__player_animation_regions.clear();
    m__player_target_rects.clear();
    m__player_target_overlay = {Value{}, Value{}};
    (void)_get_bg();
    // Py と同様、画像を読むのは選択キャラクターまたは表示中のページだけとする。
    const Integer first = m_views == 1 ? wrap_index(m_index, static_cast<Integer>(m_typed_list.size()))
        : get_page() * std::max<Integer>(1, m_views);
    const Integer count = m_views == 1 ? 1 : std::max<Integer>(1, m_views);
    for (Integer i=first; i<first+count && i<static_cast<Integer>(m_typed_list.size()); ++i) {
        const auto& header=m_typed_list[static_cast<std::size_t>(i)];
        if (!header) continue;
        const auto key = value_string(header->m_fpath);
        if (!m__player_image_cache.contains(key)) {
            auto images = load_adventurer_portrait_images(header);
            if (!images.empty()) m__player_image_cache.emplace(key, std::move(images));
        }
    }
    select_feedback(this, m_isalbum ? SelectUiKind::album : SelectUiKind::player, {}, arg_update);
}

namespace {
std::shared_ptr<::cw::lib::Window> presentation_button(
    const std::shared_ptr<::cw::lib::Window>& button) {
    return button;
}

void complete_party_presentation(PartySelect& dialog) {
    const auto header = dialog.take_selected_party();
    const auto runtime = ::cw::g_cwpy;
    if (!runtime || !header) return;
    const std::weak_ptr<::cw::thread::CWPy> weak_runtime = runtime;
    const auto selected_yado = runtime->m_ydata.value_or(nullptr);
    runtime->exec_func(
        [header, weak_runtime, selected_yado](const ::cw::lib::Arguments&,
                 const ::cw::lib::KeywordArguments&) -> Value {
            const auto runtime = weak_runtime.lock();
            if (!runtime || runtime != ::cw::g_cwpy || !selected_yado ||
                runtime->m_ydata.value_or(nullptr) != selected_yado) return {};
            const auto frame = object_value<::cw::frame::Frame>(runtime->m_frame);
            try {
                const auto sceheader = header->get_sceheader();
                if (sceheader) {
                    selected_yado->load_party(header);
                    runtime->set_scenario(sceheader, std::nullopt, {}, true, false);
                } else if (header->is_adventuring()) {
                    runtime->play_sound("error", false, 100, 1, 0, 0, false);
                    // Python の Frame.OnPARTYSELECT と同様、復元で
                    // きないシナリオを黙って破棄しない。 保存した状態を読み込む前に確認する。
                    PartySelect::recover_missing_scenario(
                        [&] { return PartySelect::confirm_missing_scenario(frame,
                            select_msg("message", "メッセージ"), select_msg("load_scenario_failure",
                            "シナリオのロードに失敗しました。\nパーティを宿に帰還させますか？")); },
                        [&] {
                            if (runtime != ::cw::g_cwpy || runtime->m_ydata.value_or(nullptr) != selected_yado)
                                throw std::runtime_error("選択時の宿は閉じられています。");
                            runtime->load_party(header, false, false, false);
                        },
                        [&] {
                            const auto data = object_value<::cw::data::SystemData>(runtime->m_sdata);
                            if (!data) throw std::runtime_error("復旧するシナリオデータがありません。");
                            if (!std::get<0>(data->set_log(false)))
                                throw std::runtime_error("パーティのシナリオログを復旧できません。");
                        },
                        [&] { runtime->f9(true, false); });
                } else {
                    runtime->load_party(header, true, false, true);
                }
                if (runtime->m_backend) runtime->m_backend->refresh_debugger(*runtime);
            } catch (const std::exception& error) {
                if (frame) {
                    const std::weak_ptr<::cw::frame::Frame> owner = frame;
                    frame->post_func([owner, text=String(error.what())](const auto&, const auto&) -> Value {
                        if (const auto frame = owner.lock())
                            (void)::cw::dialog::message::show_common_modal_message(
                                frame, "パーティ読み込みエラー", text, false, true);
                        return {};
                    });
                }
            } catch (...) {
                ::cardwirth::debug::log_current_stacktrace(
                    "PartySelect presentation completion failed");
                runtime->play_sound("error", false, 100, 1, 0, 0, false);
            }
            return {};
        });
}

void complete_player_presentation(PlayerSelect& dialog) {
    if (dialog.m_isalbum || !::cw::g_cwpy) return;
    ::cw::g_cwpy->exec_func([](const auto&, const auto&) {
        if (!::cw::g_cwpy || !::cw::g_cwpy->m_ydata ||
            !*::cw::g_cwpy->m_ydata) return Value{};
        auto& ydata = **::cw::g_cwpy->m_ydata;
        Integer areaid = 3;
        if (ydata.m_party) areaid = 2;
        else if (!ydata.is_empty() || ydata.is_changed()) areaid = 1;
        if (areaid != ::cw::g_cwpy->m_areaid &&
            (::cw::g_cwpy->m_areaid == 1 || ::cw::g_cwpy->m_areaid == 2 ||
             ::cw::g_cwpy->m_areaid == 3)) {
            if (ydata.m_party && *ydata.m_party) (*ydata.m_party)->m__loading = false;
            ::cw::g_cwpy->change_area(
                areaid, true, false, {"", Value{}}, false, false, false, true,
                std::nullopt, false, false, false, false, false);
        }
        return Value{};
    });
}
}  // namespace

::cw::dialog::DialogPresentation PartySelect::dialog_presentation() {
    m_input.filter.begin_frame();
    using Action = ::cw::dialog::DialogPresentationAction;
    ::cw::dialog::DialogPresentation result;
    result.route = "PARTYSELECT";
    result.title = select_msg("select_party", "パーティの選択");
    const Integer presentation_width = select_scaled_win(kSelectPresentationCanvasWidth);
    const Integer presentation_canvas_height = select_scaled_win(kSelectPresentationCanvasHeight);
    const Integer filter = m_presentation_additionals
        ? select_scaled_win(kSelectPresentationFilterHeight) : 0;
    const Integer bottom_height = select_scaled_win(kSelectPresentationBottomHeight);
    const Integer presentation_height=presentation_canvas_height+filter+bottom_height;
    std::uint64_t interaction_signature{};
    yado_hash_append(interaction_signature,static_cast<Integer>(m_input.action));
    yado_hash_append(interaction_signature,static_cast<Integer>(m_input.index));
    yado_hash_append(interaction_signature,static_cast<Integer>(m_filter_choice.kind));
    yado_hash_append(interaction_signature,static_cast<Integer>(m_filter_choice.cursor));
    yado_hash_append(interaction_signature,static_cast<Integer>(m_processing));
    yado_hash_append(interaction_signature,static_cast<Integer>(m_finished));
    std::uint64_t canvas_signature=interaction_signature;
    yado_hash_append(canvas_signature,m_index);yado_hash_append(canvas_signature,m_views);
    // キャッシュの確認は表示ページに合わせる。複数表示ではメンバー名の行を描
    // かないため、 そのハッシュを求める余分な XML 読み取りは行わない。
    const Integer visible_first=m_views==1 ? std::clamp<Integer>(m_index,0,
        std::max<Integer>(0,static_cast<Integer>(m_typed_list.size())-1)) : get_page()*std::max<Integer>(1,m_views);
    const Integer visible_count=m_views==1 ? 1 : std::max<Integer>(1,m_views);
    for(Integer i=visible_first;i<visible_first+visible_count && i<static_cast<Integer>(m_typed_list.size());++i)
        if(const auto& header=m_typed_list[static_cast<std::size_t>(i)]) {
            yado_hash_append(canvas_signature,header->m_name);
            yado_hash_append(canvas_signature,header->m_money);
            if(m_views==1)for(const auto& name:header->get_membernames())yado_hash_append(canvas_signature,name);
        }
    std::uint64_t filter_signature=interaction_signature;
    yado_hash_append(filter_signature,value_string(m_narrow));yado_hash_append(filter_signature,value_integer(m_narrow_type));
    yado_hash_append(filter_signature,value_string(m_sort));yado_hash_append(filter_signature,m_input.filter.edit.caret);
    yado_hash_append(filter_signature,m_input.filter.edit.anchor);yado_hash_append(filter_signature,m_input.filter.edit.composition);
    yado_hash_append(filter_signature,m_input.filter.edit.composition_start);
    yado_hash_append(filter_signature,m_input.filter.edit.composition_length);
    yado_hash_append(filter_signature,m_input.filter.edit.horizontal);
    yado_hash_append(filter_signature,m_input.filter.menu.presentation_signature(m_input.filter.edit));
    const bool records = ::cw::g_cwpy && ::cw::g_cwpy->m_ydata &&
        *::cw::g_cwpy->m_ydata &&
        ((*::cw::g_cwpy->m_ydata)->m_party.has_value() ||
         !(*::cw::g_cwpy->m_ydata)->m_partyrecord.empty());
    std::uint64_t footer_signature=interaction_signature;
    yado_hash_append(footer_signature,m_views);yado_hash_append(footer_signature,m_typed_list.size());
    // 保持中の描画と現在の当たり判定コントロールで、記録の利用可否を共通にする。
    yado_hash_append(footer_signature,static_cast<Integer>(records));
    auto& retained=dialog_retained_surface();
    const bool full=retained.needs_full({presentation_width,presentation_height});
    if(full){retained.allocate({presentation_width,presentation_height},{240,240,240,255});retained.capture_base();}
    const ::cw::lib::Rect canvas_rect{0,0,presentation_width,presentation_canvas_height};
    const ::cw::lib::Rect filter_rect{0,presentation_canvas_height,presentation_width,filter};
    const ::cw::lib::Rect footer_rect{0,presentation_canvas_height+filter,presentation_width,bottom_height};
    const bool canvas_changed=retained.component_changed("partyselect.canvas",canvas_rect,canvas_signature);
    const bool filter_changed=filter&&retained.component_changed("partyselect.filter",filter_rect,filter_signature);
    const bool footer_changed=retained.component_changed("partyselect.footer",footer_rect,footer_signature);
    retained.restore_dirty();auto surface=retained.surface;
    if(canvas_changed){auto canvas=make_yado_surface(presentation_width,presentation_canvas_height,{0,0,0,255});
        draw_party_canvas_portable(canvas,*this);::cw::lib::ui::blend_surface(surface,canvas,0,0);}
    if (m_views > 1 && !m_typed_list.empty()) {
        const Integer columns = std::max<Integer>(1, m_views / 2);
        const Integer cell_w = presentation_width / columns;
        const Integer cell_h = presentation_canvas_height / 2;
        const Integer first = get_page() * m_views;
        for (Integer i = 0; i < m_views && first + i <
             static_cast<Integer>(m_typed_list.size()); ++i) {
            result.controls.push_back(
                {Action::Select, static_cast<int>(first + i),
                 static_cast<float>((i % columns) * cell_w),
                 static_cast<float>((i / columns) * cell_h),
                 static_cast<float>(cell_w), static_cast<float>(cell_h), true});
        }
    } else {
        result.controls.push_back(
            {Action::Accept, 0, 0, 0,
             static_cast<float>(presentation_width),
             static_cast<float>(presentation_canvas_height),
             !m_typed_list.empty()});
        if (can_clickside()) {
            // Py の Select._update_mousepos と同様、無効な
            // 移動操作の領域では中央の操作を使う。 共通の四分割・カーソル処理を使い
            // 、後の絞り込み・ボタンコントロールは通常の当たり判定優先度を維持する。
            const bool left = is_enabled(m_leftbtn);
            const bool right = is_enabled(m_rightbtn);
            const auto first_side = result.controls.size();
            m_side_navigation.append(result, canvas_rect, left || right, -1);
            if (result.controls.size() == first_side + 2) {
                result.controls[first_side].enabled = left;
                result.controls.back().enabled = right;
            }
        }
    }
    if (m_presentation_additionals)
        draw_select_filter(result, surface, *this,
                           presentation_canvas_height,filter_changed);
    const bool has = !m_typed_list.empty();
    draw_select_bottom(result, surface, {
        {select_msg("decide", "決定"), Action::Accept, 0, has},
        {select_msg("information", "情報"), Action::Auxiliary, 0, has},
        {select_msg("members", "メンバー"), Action::Auxiliary, 1, has},
        {select_msg(m_views == 1 ? "member_list" : "member_one", "一覧"),
            Action::Toggle, 0, has},
        {select_msg("party_record", "パーティ記録"), Action::Auxiliary, 2, records},
        {select_msg("entry_cancel", "中止"), Action::Cancel, 0, true}},
         presentation_canvas_height+filter,true,footer_changed);
    if (show_additional_controls_button())
        draw_yado_control(result, surface, {},
            {presentation_width - select_scaled_win(28), select_scaled_win(2),
             select_scaled_win(24), select_scaled_win(24)},
            Action::Auxiliary, 5, true, m_presentation_additionals,
            select_additional_controls_image(m_presentation_additionals),
            canvas_changed, 16);
    const bool presentation_changed=canvas_changed||filter_changed||footer_changed;
    draw_select_focus(result,surface,*this,presentation_changed);
    draw_select_choice_popup(result,surface,*this,presentation_changed);
    retained.finish();result.damage=retained.damage;result.surface_revision=retained.revision;
    result.surface=surface;
    return result;
}

void PartySelect::dialog_presentation_action(
    const ::cw::dialog::DialogPresentationAction action, const int index) {
    if(select_input_action(*this,action,index)) return;
    using Action = ::cw::dialog::DialogPresentationAction;
    if (m_finished || (m_processing && action!=Action::Cancel)) return;
    if(select_choice_action(*this,action,index)) return;
    select_focus_action(*this,action,index);
    switch (action) {
    case Action::Accept: OnSelect(nullptr); break;
    case Action::Activate:
        if (index >= 0 && index < static_cast<int>(m_typed_list.size()))
            m_index = index;
        OnSelect(nullptr);
        break;
    case Action::Cancel:
        select_play_sound("click");
        m_finished = true; m_modal_result = 0; m_decision_pending = false; break;
    case Action::Previous: OnClickLeftBtn(presentation_button(m_leftbtn)); break;
    case Action::Next: OnClickRightBtn(presentation_button(m_rightbtn)); break;
    case Action::PreviousGroup: OnClickLeft2Btn(presentation_button(m_left2btn)); break;
    case Action::NextGroup: OnClickRight2Btn(presentation_button(m_right2btn)); break;
    case Action::Toggle: OnClickViewBtn(nullptr); break;
    case Action::Select:
        if (index >= 0 && index < static_cast<int>(m_typed_list.size())) {
            if (m_index != index) {
                m_index = index; enable_btn(); select_play_sound("click");
            }
        }
        break;
    case Action::Auxiliary:
        if (index == 0) OnClickInfoBtn(nullptr);
        else if (index == 1) OnClickEditBtn(nullptr);
        else if (index == 2) OnClickPartyRecordBtn(nullptr);
        else if (index == 5) {
            select_toggle_additionals(*this);
        }
        else if (index == 6) {
            if(m_presentation_additionals) open_select_choice(*this,index);
        } else if (index == 7) {
            if(m_presentation_additionals) open_select_choice(*this,index);
        }
        break;
    default: break;
    }
}

void PartySelect::dialog_presentation_key(
    const ::cw::dialog::DialogPresentationKey key, const bool pressed,
    const ::cw::dialog::DialogPresentationModifiers modifiers) {
    if (!pressed || m_finished || m_processing) return;
    if(select_priority_input_key(*this,key,modifiers)) return;
    if(select_choice_key(*this,key,modifiers,m_presentation_additionals)) return;
    if(select_focus_key(*this,key,modifiers)) return;
    if(select_additional_controls_key(*this,key,modifiers)) return;
    const bool control =
        (modifiers & ::cw::dialog::DialogPresentationModifierControl) != 0;
    const bool alt =
        (modifiers & ::cw::dialog::DialogPresentationModifierAlt) != 0;
    if (key == ::cw::dialog::DialogPresentationKey::D && control) {
        OnDebugMode(nullptr);
    } else if (key == ::cw::dialog::DialogPresentationKey::Left && control) {
        if (alt) OnClickLeft2Btn(nullptr);
        else OnClickLeftBtn(nullptr);
    } else if (key == ::cw::dialog::DialogPresentationKey::Right && control) {
        if (alt) OnClickRight2Btn(nullptr);
        else OnClickRightBtn(nullptr);
    }
}

void PartySelect::dialog_presentation_text_event(const ::cw::dialog::DialogTextEditingEvent& event) {
    select_input_text(*this,event.text,event.composition,event.start,event.length);
}
void PartySelect::dialog_presentation_pointer(::cw::dialog::DialogPresentationAction action,int index,float x,float y,bool pressed) {
    select_input_pointer(*this,action,index,x,y,pressed);
}
void PartySelect::dialog_presentation_context(float x,float y) {select_input_context(*this,x,y);}
void PartySelect::dialog_presentation_hover(float x,float y) {
    if(select_choice_hover(*this,x,y)) return;
    if(select_input_busy(*this) || !select_input_visible(*this)) {m_input.filter.retire();return;}
    if(m_input.filter.menu.hover(x,y,m_input.filter.edit)) request_dialog_presentation_refresh();
}

void PartySelect::dialog_presentation_text(const std::string& text,
                                            const bool composition) {
    if(m_finished || m_processing) return;
    select_input_text(*this,text,composition);
}

void PartySelect::dialog_presentation_enter(::cw::dialog::DialogPresentationModifiers modifiers) {
    dialog_presentation_key(::cw::dialog::DialogPresentationKey::Enter,true,modifiers);
}

void PartySelect::dialog_presentation_scroll(float x,float y,int lines) {
    if(!m_finished && !m_processing) select_choice_scroll(*this,x,y,lines);
}

bool PartySelect::dialog_presentation_finished() const noexcept { return m_finished; }

std::shared_ptr<::cw::header::PartyHeader> PartySelect::take_selected_party()
{
    const bool pending = std::exchange(m_decision_pending, false);
    if (!pending || !m_finished || m_modal_result <= 0 || m_index < 0 ||
        m_index >= static_cast<Integer>(m_typed_list.size())) return {};
    return m_typed_list[static_cast<std::size_t>(m_index)];
}

bool PartySelect::recover_missing_scenario(const std::function<bool()>& confirm,
    const std::function<void()>& load_party, const std::function<void()>& restore_log,
    const std::function<void()>& return_to_inn)
{
    if (!confirm || !load_party || !restore_log || !return_to_inn || !confirm()) return false;
    load_party();
    restore_log(); // 失敗した場合はここで止め、一部だけの緊急帰還は行わない。
    return_to_inn();
    return true;
}

bool PartySelect::confirm_missing_scenario(const std::shared_ptr<::cw::frame::Frame>& frame,
    const String& title, const String& message)
{
    if (!frame || frame->m_exec_shutdown.load(std::memory_order_acquire)) return false;
    // この復旧要求は CWPy から来る。既存の Frame キュ
    // ーを使い、 共通モーダルの入口や送信時の保護は変更しない。
    try {
        return frame->sync_exec([frame, title, message](const auto&, const auto&) -> Value {
            return Value(::cw::dialog::message::show_common_modal_message(frame, title, message, true, true).value_or(false));
        }).as_bool();
    } catch (...) { return false; }
}

void PartySelect::dialog_presentation_complete() {
    m_input.narrow_update.cancel();
    complete_party_presentation(*this);
    if (party_callback_context == this) party_callback_context = nullptr;
}

::cw::dialog::DialogPresentation PlayerSelect::dialog_presentation() {
    m_input.filter.begin_frame();
    using Action = ::cw::dialog::DialogPresentationAction;
    ::cw::dialog::DialogPresentation result;
    result.route = m_isalbum ? "ALBUM" : "PLAYERSELECT";
    result.title = select_msg(m_isalbum ? "album" : "select_member",
                              m_isalbum ? "アルバム" : "メンバーの選択");
    const Integer presentation_width = select_scaled_win(kSelectPresentationCanvasWidth);
    const Integer presentation_canvas_height = select_scaled_win(kSelectPresentationCanvasHeight);
    const Integer filter = (!m_isalbum && m_presentation_additionals)
        ? select_scaled_win(kSelectPresentationFilterHeight) : 0;
    const Integer bottom_height = select_scaled_win(kSelectPresentationBottomHeight);
    const bool interactive = !m_finished && !m__processing;
    const bool navigation_enabled = interactive &&
        m_typed_list.size() > static_cast<std::size_t>(std::max<Integer>(1, m_views));
    const Integer presentation_height=presentation_canvas_height+filter+bottom_height;
    std::uint64_t interaction_signature{};
    yado_hash_append(interaction_signature,static_cast<Integer>(m_input.action));
    yado_hash_append(interaction_signature,static_cast<Integer>(m_input.index));
    yado_hash_append(interaction_signature,static_cast<Integer>(m_filter_choice.kind));
    yado_hash_append(interaction_signature,static_cast<Integer>(m_filter_choice.cursor));
    yado_hash_append(interaction_signature,static_cast<Integer>(m__processing));
    yado_hash_append(interaction_signature,static_cast<Integer>(m_finished));
    std::uint64_t canvas_signature=interaction_signature;
    yado_hash_append(canvas_signature,m_index);yado_hash_append(canvas_signature,m_views);
    yado_hash_append(canvas_signature,static_cast<Integer>(m_isalbum));
    for(const auto& header:m_typed_list)if(header){yado_hash_append(canvas_signature,header->m_name);
        yado_hash_append(canvas_signature,value_integer(header->m_level));yado_hash_append(canvas_signature,value_integer(header->m_ep));
        yado_hash_append(canvas_signature,value_string(header->m_desc));for(const auto& item:header->m_history)yado_hash_append(canvas_signature,value_string(item));}
    std::uint64_t filter_signature=interaction_signature;
    yado_hash_append(filter_signature,value_string(m_narrow));yado_hash_append(filter_signature,value_integer(m_narrow_type));
    yado_hash_append(filter_signature,value_string(m_sort));yado_hash_append(filter_signature,m_input.filter.edit.caret);
    yado_hash_append(filter_signature,m_input.filter.edit.anchor);yado_hash_append(filter_signature,m_input.filter.edit.composition);
    yado_hash_append(filter_signature,m_input.filter.edit.composition_start);
    yado_hash_append(filter_signature,m_input.filter.edit.composition_length);
    yado_hash_append(filter_signature,m_input.filter.edit.horizontal);
    yado_hash_append(filter_signature,m_input.filter.menu.presentation_signature(m_input.filter.edit));
    std::uint64_t footer_signature=interaction_signature;
    yado_hash_append(footer_signature,m_views);yado_hash_append(footer_signature,m_typed_list.size());
    yado_hash_append(footer_signature,static_cast<Integer>(m_isalbum));
    auto& retained=dialog_retained_surface();
    const bool full=retained.needs_full({presentation_width,presentation_height});
    if(full){retained.allocate({presentation_width,presentation_height},{240,240,240,255});retained.capture_base();}
    const ::cw::lib::Rect canvas_rect{0,0,presentation_width,presentation_canvas_height};
    const ::cw::lib::Rect filter_rect{0,presentation_canvas_height,presentation_width,filter};
    const ::cw::lib::Rect footer_rect{0,presentation_canvas_height+filter,presentation_width,bottom_height};
    const bool canvas_changed=retained.component_changed("playerselect.canvas",canvas_rect,canvas_signature);
    const bool filter_changed=filter&&retained.component_changed("playerselect.filter",filter_rect,filter_signature);
    const bool footer_changed=retained.component_changed("playerselect.footer",footer_rect,footer_signature);
    retained.restore_dirty();auto surface=retained.surface;
    if(canvas_changed){auto canvas=make_yado_surface(presentation_width,presentation_canvas_height,{0,0,0,255});
        draw_player_canvas_portable(canvas,*this);::cw::lib::ui::blend_surface(surface,canvas,0,0);}
    if (m_views > 1 && !m_typed_list.empty()) {
        const Integer columns = std::max<Integer>(1, m_views / 2);
        const Integer cell_w = presentation_width / columns;
        const Integer cell_h = presentation_canvas_height / 2;
        const Integer first = get_page() * m_views;
        for (Integer i = 0; i < m_views; ++i)
            result.controls.push_back(
                {Action::Select, static_cast<int>(std::min<Integer>(first + i, m_typed_list.size() - 1)),
                 static_cast<float>((i % columns) * cell_w),
                 static_cast<float>((i / columns) * cell_h),
                 static_cast<float>(cell_w), static_cast<float>(cell_h), interactive});
    } else {
        // CardWirthPy の PlayerSelect.OnSelectBase
        // と同様、一件表示では外側の四分の一ずつを移動に使い、パー
        // ティーが満員でも操作できるようにする。 追加を行うのは中
        // 央の半分だけとする。三領域を重ねず、下で追加する追加コン
        // トロールボタンは後に置いて当たり判定の優先度を維持する。
        const float quarter = static_cast<float>(presentation_width) / 4.0f;
        result.controls.push_back(
            {Action::Previous, -1, 0, 0, quarter,
             static_cast<float>(presentation_canvas_height), navigation_enabled});
        result.controls.push_back(
            {Action::Activate, -1, quarter, 0, quarter * 2.0f,
             static_cast<float>(presentation_canvas_height),
             interactive && !m_isalbum && can_clickcenter()});
        result.controls.push_back(
            {Action::Next, -1, quarter * 3.0f, 0, quarter,
             static_cast<float>(presentation_canvas_height), navigation_enabled});
    }
    if (filter)
        draw_select_filter(result, surface, *this,
                           presentation_canvas_height,filter_changed);
    const bool has = !m_typed_list.empty();
    if (m_isalbum) {
        draw_select_bottom(result, surface, {
            {select_msg("information", "情報"), Action::Auxiliary, 0, has && interactive},
            {select_msg("delete", "削除"), Action::Auxiliary, 4, has && interactive},
            {select_msg("close", "閉じる"), Action::Cancel, 0, true}},
             presentation_canvas_height+filter,navigation_enabled,footer_changed);
    } else {
        const bool add_enabled = has && interactive &&
            is_enabled(object_value<::cw::lib::Window>(m_addbtn));
        draw_select_bottom(result, surface, {
            {select_msg("add_member", "編入"), Action::Accept, 0, add_enabled},
            {select_msg("information", "情報"), Action::Auxiliary, 0, has && interactive},
            {select_msg("new", "新規"), Action::Auxiliary, 2, interactive},
            {select_msg("extension", "拡張"), Action::Auxiliary, 3, interactive},
            {select_msg(m_views == 1 ? "member_list" : "member_one", "一覧"),
                Action::Toggle, 0, interactive},
            {select_msg("close", "閉じる"), Action::Cancel, 0, true}},
             presentation_canvas_height+filter,navigation_enabled,footer_changed);
        if (show_additional_controls_button())
            draw_yado_control(result, surface, {},
                {presentation_width - select_scaled_win(28), select_scaled_win(2),
                 select_scaled_win(24), select_scaled_win(24)},
                Action::Auxiliary, 5, interactive, m_presentation_additionals,
                select_additional_controls_image(m_presentation_additionals),
                canvas_changed, 16);
    }
    const bool presentation_changed=canvas_changed||filter_changed||footer_changed;
    draw_select_focus(result,surface,*this,presentation_changed);
    draw_select_choice_popup(result,surface,*this,presentation_changed);
    if (!interactive) {
        for (auto& control : result.controls)
            if (control.action != Action::Cancel) control.enabled = false;
        result.wants_text_input = false;
    }
    retained.finish();result.damage=retained.damage;result.surface_revision=retained.revision;
    result.surface=surface;
    return result;
}

void PlayerSelect::dialog_presentation_action(
    const ::cw::dialog::DialogPresentationAction action, const int index) {
    if(select_input_action(*this,action,index)) return;
    using Action = ::cw::dialog::DialogPresentationAction;
    if (m_finished || (m__processing && action != Action::Cancel)) return;
    if(select_choice_action(*this,action,index)) return;
    select_focus_action(*this,action,index);
    request_dialog_presentation_refresh();
    if ((action == Action::Previous || action == Action::Next ||
         action == Action::PreviousGroup || action == Action::NextGroup) &&
        m_typed_list.size() <= static_cast<std::size_t>(std::max<Integer>(1, m_views))) return;
    switch (action) {
    case Action::Accept:
        // Python とネイティブの追加ボタンと同様、選択画面を開いたまま _add()
        // を予約し、 その後 UI スレッドで残った待機中キャラクターを更新する。
        if (!m_isalbum && !m_typed_list.empty() && is_enabled(object_value<::cw::lib::Window>(m_addbtn)))
            OnClickAddBtn(nullptr);
        break;
    case Action::Activate:
        if (index >= static_cast<int>(m_typed_list.size()) && index >= 0) break;
        if (index >= 0 && index < static_cast<int>(m_typed_list.size()))
            m_index = index;
        if (m_typed_list.empty()) OnSelect(nullptr);
        else if (!m_isalbum && is_enabled(object_value<::cw::lib::Window>(m_addbtn)))
            OnClickAddBtn(nullptr);
        break;
    case Action::Cancel:
        select_play_sound("click");
        m_finished = true; m_modal_result = 0; m_decision_pending = false; break;
    case Action::Previous: OnClickLeftBtn(presentation_button(m_leftbtn)); break;
    case Action::Next: OnClickRightBtn(presentation_button(m_rightbtn)); break;
    case Action::PreviousGroup: OnClickLeft2Btn(presentation_button(m_left2btn)); break;
    case Action::NextGroup: OnClickRight2Btn(presentation_button(m_right2btn)); break;
    case Action::Toggle: OnClickViewBtn(nullptr); break;
    case Action::Select:
        if (index >= 0 && index < static_cast<int>(m_typed_list.size())) {
            // 一覧の一回クリックは選択だけとし、ダブルクリック・Enter で追加を行う。
            if (m_index != index) { m_index = index; select_play_sound("click"); enable_btn(); draw(true); }
        }
        break;
    case Action::Auxiliary:
        if (index == 0) OnClickInfoBtn(nullptr);
        else if (index == 2) OnClickNewBtn(nullptr);
        else if (index == 3) OnClickExBtn(nullptr);
        else if (index == 4) {
            if (auto* album = dynamic_cast<Album*>(this)) album->OnClickDelBtn(nullptr);
        } else if (index == 5) {
            select_toggle_additionals(*this);
        }
        else if (index == 6) {
            if(!m_isalbum && m_presentation_additionals) open_select_choice(*this,index);
        } else if (index == 7) {
            if(!m_isalbum && m_presentation_additionals) open_select_choice(*this,index);
        }
        break;
    default: break;
    }
}

void PlayerSelect::dialog_presentation_key(
    const ::cw::dialog::DialogPresentationKey key, const bool pressed,
    const ::cw::dialog::DialogPresentationModifiers modifiers) {
    if (!pressed || m_finished || m__processing) return;
    if(select_priority_input_key(*this,key,modifiers)) return;
    if(select_choice_key(*this,key,modifiers,!m_isalbum && m_presentation_additionals)) return;
    if(select_focus_key(*this,key,modifiers)) return;
    if(select_additional_controls_key(*this,key,modifiers)) return;
    const bool control =
        (modifiers & ::cw::dialog::DialogPresentationModifierControl) != 0;
    const bool alt =
        (modifiers & ::cw::dialog::DialogPresentationModifierAlt) != 0;
    if (key == ::cw::dialog::DialogPresentationKey::D && control) {
        OnDebugMode(nullptr);
    } else if (key == ::cw::dialog::DialogPresentationKey::Left && control) {
        if (alt) OnClickLeft2Btn(nullptr);
        else OnClickLeftBtn(nullptr);
    } else if (key == ::cw::dialog::DialogPresentationKey::Right && control) {
        if (alt) OnClickRight2Btn(nullptr);
        else OnClickRightBtn(nullptr);
    }
}

void PlayerSelect::dialog_presentation_text_event(const ::cw::dialog::DialogTextEditingEvent& event) {
    select_input_text(*this,event.text,event.composition,event.start,event.length);
}
void PlayerSelect::dialog_presentation_pointer(::cw::dialog::DialogPresentationAction action,int index,float x,float y,bool pressed) {
    select_input_pointer(*this,action,index,x,y,pressed);
}
void PlayerSelect::dialog_presentation_context(float x,float y) {select_input_context(*this,x,y);}
void PlayerSelect::dialog_presentation_hover(float x,float y) {
    if(select_choice_hover(*this,x,y)) return;
    if(select_input_busy(*this) || !select_input_visible(*this)) {m_input.filter.retire();return;}
    if(m_input.filter.menu.hover(x,y,m_input.filter.edit)) request_dialog_presentation_refresh();
}

void PlayerSelect::dialog_presentation_text(const std::string& text,
                                             const bool composition) {
    if (m_finished || m__processing || m_isalbum || m_filter_choice.kind) return;
    select_input_text(*this,text,composition);
}

void PlayerSelect::dialog_presentation_enter(::cw::dialog::DialogPresentationModifiers modifiers) {
    dialog_presentation_key(::cw::dialog::DialogPresentationKey::Enter,true,modifiers);
}

void PlayerSelect::dialog_presentation_scroll(float x,float y,int lines) {
    if(!m_finished && !m__processing) select_choice_scroll(*this,x,y,lines);
}

bool PlayerSelect::dialog_presentation_finished() const noexcept { return m_finished; }

void PlayerSelect::dialog_presentation_complete() {
    m_input.narrow_update.cancel();
    complete_player_presentation(*this);
    if (player_callback_context == this) player_callback_context = nullptr;
}

void Select::initialize(std::shared_ptr<::cw::lib::Window> arg_parent, ::cw::lib::String arg_name)
{
    m_narrow_update.cancel();
    m_native_handle = arg_parent ? arg_parent->native_handle : 0;
    m_native_parent_handle = m_native_handle;
    m_cwpy_debug = false;
    m__processing = false;
    m__reserved_narrowconditin = false;
    m__downbutton = -1;
    m_clickmode = 0;
    m_list.clear();
    m_index = 0;
    m_decision_pending = false;
    m_finished = false;
    m_modal_result = -1;
    m_panel = Value(std::static_pointer_cast<::cw::lib::Object>(make_window("panel")));
    m_topsizer = Value{};
    m_buttonlist.clear();
    m_additionals.clear();
    m_left2btn = make_window("left2", -1);
    m_leftbtn = make_window("left", -2);
    m_rightbtn = make_window("right", -3);
    m_right2btn = make_window("right2", -4);
    m_buttonlist = {m_left2btn, m_leftbtn, m_rightbtn, m_right2btn};
    if (arg_parent) {
        m_left2btn->bounds = arg_parent->bounds;
        m_leftbtn->bounds = arg_parent->bounds;
        m_rightbtn->bounds = arg_parent->bounds;
        m_right2btn->bounds = arg_parent->bounds;
    }
    m_previd = next_control_id.fetch_add(1);
    m_nextid = next_control_id.fetch_add(1);
    m_leftkeyid = next_control_id.fetch_add(1);
    m_rightkeyid = next_control_id.fetch_add(1);
    m_left2keyid = next_control_id.fetch_add(1);
    m_right2keyid = next_control_id.fetch_add(1);
    // 表示と入力は共通DialogPresentationが担う。
    // 既定のSelectUiBackendは共通サービスを提供し、OSのウィジェットは生成しない。
    (void)arg_name;
    if (const auto backend = get_ui_backend()) backend->bind_select_dialog(this, SelectUiKind::select);
}

void Select::_bind()
{
    if (const auto backend=get_ui_backend()) backend->bind_select_dialog(this,SelectUiKind::select);
    m_focused_button.reset();
    for (const auto& button:m_buttonlist) if (is_enabled(button)) {m_focused_button=button;break;}
}

void Select::process_navigation_command(bool left)
{
    // ホイールとキャンバスの左・中央ボタン解除で、共通のコマンド送信を使う。 具体的な選択クラ
    // スは、wx の ProcessEvent と同じようにそのコマンドを監視・上書きできる。
    if (left) OnClickLeftBtn(m_leftbtn);
    else OnClickRightBtn(m_rightbtn);
}

bool Select::is_processing()
{
    return m__processing;
}

void Select::OnPrevButton(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    std::vector<std::shared_ptr<::cw::lib::Window>> enabled;
    for (const auto& button : m_buttonlist) if (is_enabled(button)) enabled.push_back(button);
    if (enabled.empty()) return;
    // Window::id は共通のフォーカス識別子とし、wx の実際のフォーカスは UI アダプターで管理する。
    const Integer current = arg_event ? arg_event->id : 0;
    auto iter = std::find_if(enabled.begin(), enabled.end(), [current](const auto& button) {
        return button && button->id == current;
    });
    const auto index = iter == enabled.end() ? 0 : static_cast<std::size_t>(iter - enabled.begin());
    const auto target = enabled[index == 0 ? enabled.size() - 1 : index - 1];
    if (arg_event && target) arg_event->id = target->id;
}

void Select::OnNextButton(std::shared_ptr<::cw::lib::Window> arg_event)
{
    std::vector<std::shared_ptr<::cw::lib::Window>> enabled;
    for (const auto& button : m_buttonlist) if (is_enabled(button)) enabled.push_back(button);
    if (enabled.empty()) return;
    const Integer current = arg_event ? arg_event->id : 0;
    auto iter = std::find_if(enabled.begin(), enabled.end(), [current](const auto& button) {
        return button && button->id == current;
    });
    const auto index = iter == enabled.end() ? 0 : static_cast<std::size_t>(iter - enabled.begin());
    // Py と同様、フォーカスが一覧外なら最初の有効なボタンを選ぶ。
    const auto target = enabled[iter == enabled.end() ? 0 : (index + 1) % enabled.size()];
    if (arg_event && target) arg_event->id = target->id;
}

void Select::OnDebugMode(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
}

void Select::OnMotion(std::shared_ptr<::cw::lib::Window> arg_evt)
{
    if (arg_evt) m_pointer_position={arg_evt->bounds.x,arg_evt->bounds.y};
    _update_mousepos();
}

void Select::_update_mousepos()
{
    m_clickmode=0;
    if (!can_clickside()) return;
    const auto panel=object_value<::cw::lib::Window>(m_panel);
    const auto width=panel && panel->bounds.width>0 ? panel->bounds.width : 400;
    if (m_pointer_position.x<width/4 && is_enabled(m_leftbtn)) m_clickmode=kLeft;
    else if (m_pointer_position.x>width/4*3 && is_enabled(m_rightbtn)) m_clickmode=kRight;
}

void Select::OnClickLeftBtn(std::shared_ptr<::cw::lib::Window> arg_evt)
{
    (void)arg_evt;
    const auto count = static_cast<::cw::lib::Integer>(m_list.size());
    if (count <= 1) return;
    m_index = wrap_index(m_index, count);
    m_index = (m_index == 0) ? count - 1 : m_index - 1;
    get_ui_backend()->play_select_sound("page");
    draw(true);
    index_changed();
}

void Select::OnClickLeft2Btn(std::shared_ptr<::cw::lib::Window> arg_evt)
{
    (void)arg_evt;
    const auto count = static_cast<::cw::lib::Integer>(m_list.size());
    if (count <= 1) return;
    m_index = wrap_index(m_index, count);
    if (m_index == 0) m_index = count - 1;
    else m_index = std::max<::cw::lib::Integer>(0, m_index - 10);
    get_ui_backend()->play_select_sound("page");
    draw(true);
    index_changed();
}

void Select::OnClickRightBtn(std::shared_ptr<::cw::lib::Window> arg_evt)
{
    (void)arg_evt;
    const auto count = static_cast<::cw::lib::Integer>(m_list.size());
    if (count <= 1) return;
    m_index = wrap_index(m_index, count);
    m_index = (m_index == count - 1) ? 0 : m_index + 1;
    get_ui_backend()->play_select_sound("page");
    draw(true);
    index_changed();
}

void Select::OnClickRight2Btn(std::shared_ptr<::cw::lib::Window> arg_evt)
{
    (void)arg_evt;
    const auto count = static_cast<::cw::lib::Integer>(m_list.size());
    if (count <= 1) return;
    m_index = wrap_index(m_index, count);
    if (m_index == count - 1) m_index = 0;
    else m_index = std::min<::cw::lib::Integer>(count - 1, m_index + 10);
    get_ui_backend()->play_select_sound("page");
    draw(true);
    index_changed();
}

void Select::index_changed()
{
}

void Select::OnMouseWheel(std::shared_ptr<::cw::lib::Window> arg_event)
{
    if (::cw::util::has_modalchild(object_value<::cw::lib::Window>(m_panel))) return;
    if (m_list.size() <= 1) return;
    process_navigation_command(event_wheel(arg_event)>0);
}

void Select::OnMouseDown(std::shared_ptr<::cw::lib::Window> arg_event)
{
    if (::cw::util::has_modalchild(object_value<::cw::lib::Window>(m_panel))) return;
    m__downbutton = event_button(arg_event);
}

void Select::OnSelectBase(std::shared_ptr<::cw::lib::Window> arg_event)
{
    if (m__processing) return;
    if (value_integer(m__downbutton, -1) != event_button(arg_event)) {
        m__downbutton = -1;
        return;
    }
    m__downbutton = -1;
    if (arg_event) m_pointer_position={arg_event->bounds.x,arg_event->bounds.y};
    _update_mousepos();
    if (m_clickmode == kLeft && is_enabled(m_leftbtn)) process_navigation_command(true);
    else if (m_clickmode == kRight && is_enabled(m_rightbtn)) process_navigation_command(false);
    else OnSelect(arg_event ? std::make_shared<::cw::lib::Event>() : nullptr);
}

void Select::OnSelect(std::shared_ptr<::cw::lib::Event> arg_event)
{
    (void)arg_event;
    if (m_list.empty() || m__processing) return;
    m_narrow_update.cancel(); m__reserved_narrowconditin=false;
    m_decision_pending = true;
    m_finished = true;
    m_modal_result = 1;
}

void Select::OnCancel(std::shared_ptr<::cw::lib::Window> arg_event)
{
    m_narrow_update.cancel(); m__reserved_narrowconditin=false;
    (void)arg_event;
    select_play_sound("click");
    m_decision_pending = false;
    m_finished = true;
    m_modal_result = 0;
}

void Select::OnPaint2(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
}

std::tuple<std::shared_ptr<::cw::lib::Window>, std::shared_ptr<::cw::lib::Bitmap>> Select::draw2(bool arg_update)
{
    (void)arg_update;
    auto target = ::cw::util::empty_bitmap_rgba(1, 1);
    return {make_window("select-render-target"), target};
}

void Select::draw(bool arg_update)
{
    (void)arg_update;
    select_feedback(this, SelectUiKind::select, {}, arg_update);
}

void Select::draw3(std::shared_ptr<::cw::lib::Window> arg_dc, std::shared_ptr<::cw::lib::Bitmap> arg_dest, bool arg_update)
{
    (void)arg_dc;
    (void)arg_dest;
    (void)arg_update;
    if (arg_dest) select_feedback(this, SelectUiKind::select, {}, arg_update);
}

void Select::_do_layout()
{
    get_ui_backend()->layout_select_dialog(this, SelectUiKind::select);
}

void Select::_add_topsizer()
{
}

void Select::set_panelsizer()
{
    // Python と同様、左へ大きく移動、左、派生クラスのボタン、右、右へ大きく移動の順序を維持する。
    if (m_buttonlist.size() < 4) m_buttonlist = {m_left2btn, m_leftbtn, m_rightbtn, m_right2btn};
}

void Select::_disable_btn(std::optional<std::vector<std::shared_ptr<::cw::lib::Control>>> arg_enables)
{
    const std::vector<std::shared_ptr<::cw::lib::Control>> enables = arg_enables.value_or(std::vector<std::shared_ptr<::cw::lib::Control>>{});
    std::vector<std::shared_ptr<::cw::lib::Window>> buttons = m_buttonlist;
    for (const auto& button : {m_left2btn, m_leftbtn, m_rightbtn, m_right2btn}) {
        if (button && std::find(buttons.begin(), buttons.end(), button) == buttons.end()) buttons.push_back(button);
    }
    for (const auto& button : buttons) {
        const bool keep = std::any_of(enables.begin(), enables.end(), [&button](const auto& control) {
            return control && button && (control.get() == button.get() || control->id == button->id);
        });
        set_enabled(button, keep);
    }
}

void Select::_enable_btn(std::optional<std::vector<std::shared_ptr<::cw::lib::Control>>> arg_disables)
{
    const std::vector<std::shared_ptr<::cw::lib::Control>> disables = arg_disables.value_or(std::vector<std::shared_ptr<::cw::lib::Control>>{});
    std::vector<std::shared_ptr<::cw::lib::Window>> buttons = m_buttonlist;
    for (const auto& button : {m_left2btn, m_leftbtn, m_rightbtn, m_right2btn}) {
        if (button && std::find(buttons.begin(), buttons.end(), button) == buttons.end()) buttons.push_back(button);
    }
    for (const auto& button : buttons) {
        const bool blocked = std::any_of(disables.begin(), disables.end(), [&button](const auto& control) {
            return control && button && (control.get() == button.get() || control->id == button->id);
        });
        set_enabled(button, !blocked);
    }
}

bool Select::can_clickcenter()
{
    return true;
}

bool Select::can_clickside()
{
    return true;
}

void Select::_init_narrowpanel(std::vector<::cw::lib::String> arg_choices, ::cw::lib::String arg_narrowtext, ::cw::lib::Integer arg_narrowtype, bool arg_tworows)
{
    m_narrow = arg_narrowtext;
    m_narrow_type = arg_narrowtype;
    m_narrow_label = arg_tworows ? "narrow_keyword" : "narrow_condition";
    m_keyword_label = arg_tworows ? "narrow_condition2" : Value{};
    std::vector<Value> choices;
    choices.reserve(arg_choices.size());
    for (const auto& choice : arg_choices) choices.emplace_back(choice);
    m_topsizer = make_sequence(choices);
}

void Select::OnFind(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
}

void Select::OnNarrowCondition(std::shared_ptr<::cw::lib::Window> arg_event)
{
    if (m__processing || m_finished) return;
    (void)arg_event;
    const std::weak_ptr<Select> weak=weak_from_this();
    if(weak.expired()) return;
    select_play_sound("page");
    // 実際の表示プロバイダーでは、共通の意味上の入力モデルで上部パネルへの代替処理を行う。
    // 汎用 Window には wx の FindFocus に相当する基本操作がない。
    m__reserved_narrowconditin = true;
    if(!m_narrow_update.request([weak] {
        const auto owner=weak.lock();
        if(!owner) return;
        if(owner->m_finished) {owner->m__reserved_narrowconditin=false;return;}
        try {owner->_on_narrowcondition();}
        catch (...) {owner->m__reserved_narrowconditin=false;throw;}
        owner->m__reserved_narrowconditin=false;
    })) m__reserved_narrowconditin=false;
}

void Select::_on_narrowcondition()
{
    update_narrowcondition();
}

void Select::create_addctrlbtn(std::shared_ptr<::cw::lib::Panel> arg_parent, std::shared_ptr<::cw::lib::Bitmap> arg_bg, bool arg_show)
{
    (void)arg_parent;
    (void)arg_bg;
    if (m_addctrlbtn && m_addctrlbtn.value()) m_addctrlbtn.value()->visible = false;
    auto button = make_window("additional", -5);
    set_enabled(button, true);
    button->visible = arg_show;
    m_addctrlbtn = button;
}

void Select::update_additionals()
{
    const auto button = m_addctrlbtn.value_or(nullptr);
    update_additionals_impl(button && button->visible, m_additionals);
}

void Select::update_additionals_impl(bool arg_show, std::vector<std::shared_ptr<::cw::lib::Control>> arg_additionals)
{
    for (const auto& control : arg_additionals) {
        if (control) control->visible = arg_show;
    }
}

void Select::append_addctrlaccelerator(std::vector<std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer>> arg_seq)
{
    std::vector<Value> values = sequence_values(m_accels);
    values.reserve(arg_seq.size());
    for (const auto& [flags, key, id] : arg_seq) {
        values.push_back(make_sequence({Value(flags), Value(key), Value(id)}));
    }
    // 生成済み ABI は列を値で受け取るため、追加した wx.ACCEL_CTRL（2）+F
    // の項目を m_accels に保持する。 共通プロバイダーは上でそれを処理する。
    values.push_back(make_sequence({Value(Integer{2}), Value(Integer{'F'}),
                                   Value(next_control_id.fetch_add(1))}));
    m_accels = make_sequence(values);
}

void Select::OnToggleAdditionalControls(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    if (m_addctrlbtn) m_addctrlbtn.value()->visible = !m_addctrlbtn.value()->visible;
    _additional_controls();
}

void Select::OnAdditionalControls(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    _additional_controls();
}

void Select::_additional_controls()
{
    select_play_sound("equipment");
    update_additionals();
    update_narrowcondition();
    // Win32 では WM_COMMAND 後に native_layout を再実行し、wx の Freeze/Thaw
    // ・Refresh 相当を一度の同期更新で行う。 SDL バックエンドも同じ状態を参照する。
}

void Select::update_narrowcondition()
{
}

void YadoSelect::initialize(std::shared_ptr<::cw::lib::Window> arg_parent)
{
    cancel_conversion_folder();m_conversion_folder_error.clear();m_conversion_root.clear();
    m_input={}; m_filter_choice={};
    m__lastbillskindir.reset();
    m_native_handle = arg_parent ? arg_parent->native_handle : 0;
    m_native_parent_handle = m_native_handle;
    m__processing = false;
    m_max_views = 6;
    m_views = (::cw::g_cwpy && ::cw::g_cwpy->m_setting &&
               ::cw::g_cwpy->m_setting->m_show_multiplebases) ? m_max_views : 1;
    m_lines = 3;
    m_enterid = 1;
    m_clickmode = 0;
    m_downbutton = -1;
    m_decision_pending = false;
    m_finished = false;
    m_modal_result = -1;
    m_index = 0;
    m_narrow = String{};
    m_narrow_type = (::cw::g_cwpy && ::cw::g_cwpy->m_setting)
        ? Value(::cw::g_cwpy->m_setting->m_yado_narrowtype) : Value(Integer{});
    m_sort = (::cw::g_cwpy && ::cw::g_cwpy->m_setting)
        ? Value(::cw::g_cwpy->m_setting->m_sort_yado) : Value(String("None"));
    m_toppanel_window = make_window("YadoSelect");
    if (arg_parent) m_toppanel_window->bounds = arg_parent->bounds;
    m_toppanel = Value(std::static_pointer_cast<::cw::lib::Object>(m_toppanel_window));
    m_okbtn = Value(std::static_pointer_cast<::cw::lib::Object>(make_window("ok", 1)));
    m_newbtn = Value(std::static_pointer_cast<::cw::lib::Object>(make_window("new", 2)));
    m_exbtn = Value(std::static_pointer_cast<::cw::lib::Object>(make_window("extension", 3)));
    m_closebtn = Value(std::static_pointer_cast<::cw::lib::Object>(make_window("close", 4)));
    m_viewbtn = Value(std::static_pointer_cast<::cw::lib::Object>(make_window("view", 5)));
    m__bg = Value{};
    const auto lists = get_yadolist();
    m__names_typed = std::get<0>(lists);
    m__list_typed = std::get<1>(lists);
    m__list2_typed = std::get<2>(lists);
    m__skins_typed = std::get<3>(lists);
    m__classic_typed = std::get<4>(lists);
    m__isshortcuts_typed = std::get<5>(lists);
    m__imgpaths_typed = std::get<6>(lists);
    m_names = m__names_typed;
    m_list = m__list_typed;
    m_list2 = m__list2_typed;
    m_skins = m__skins_typed;
    m_classic = m__classic_typed;
    m_isshortcuts = m__isshortcuts_typed;
    m_imgpaths_typed = m__imgpaths_typed;
    m_imgpaths.assign(m_imgpaths_typed.size(), Value{});
    // Python と同様、絞り込み・並べ替えの前に以前開いた宿を復元する。
    // パスを選択の識別情報として保持し、update_narrowcondition()
    // がどちらの操作後も同じ宿を見つけ直せるようにする。
    if (::cw::g_cwpy && ::cw::g_cwpy->m_setting &&
        !::cw::g_cwpy->m_setting->m_lastyado.empty()) {
        const auto& last = ::cw::g_cwpy->m_setting->m_lastyado;
        for (std::size_t i = 0; i < m_list.size(); ++i) {
            if (::cw::lib::encoding::path_to_utf8(
                    std::filesystem::u8path(m_list[i]).filename()) == last) {
                m_index = static_cast<Integer>(i);
                break;
            }
        }
    }
    update_narrowcondition();
    enable_btn();
    if (const auto backend = get_ui_backend()) backend->bind_select_dialog(this, SelectUiKind::yado);
}

::cw::dialog::DialogPresentation YadoSelect::dialog_presentation()
{
    m_input.filter.begin_frame();
    using Action = ::cw::dialog::DialogPresentationAction;
    ::cw::dialog::DialogPresentation presentation;
    presentation.route = "YADOSELECT";
    presentation.title = "宿を選択";

    const bool show_additionals = !::cw::g_cwpy || !::cw::g_cwpy->m_setting ||
        ::cw::g_cwpy->m_setting->m_show_additional_yado;
    // PartySelect/PlayerSelect と同様、全ホストでウィンドウのピクセル
    // を公開する。 配置・フォント・画像・当たり判定矩形に UP_WIN を一度だけ適用する。
    const auto scale = [](const Integer value) { return select_scaled_win(value); };
    const auto layout = yado_layout_metrics(show_additionals);
    const Integer presentation_width = layout.width;
    const Integer presentation_height = layout.height;
    const Integer controls_height = layout.controls_height;
    const Integer filter_height = layout.filter_height;
    const Integer canvas_height = layout.canvas_height;
    const auto ok = object_value<::cw::lib::Window>(m_okbtn);
    const auto newly = object_value<::cw::lib::Window>(m_newbtn);
    const auto extension = object_value<::cw::lib::Window>(m_exbtn);
    const auto close = object_value<::cw::lib::Window>(m_closebtn);

    std::uint64_t interaction_signature{};
    yado_hash_append(interaction_signature, static_cast<Integer>(m_input.action));
    yado_hash_append(interaction_signature, static_cast<Integer>(m_input.index));
    yado_hash_append(interaction_signature, static_cast<Integer>(m_filter_choice.kind));
    yado_hash_append(interaction_signature, static_cast<Integer>(m_filter_choice.cursor));
    yado_hash_append(interaction_signature, static_cast<Integer>(m__processing));
    yado_hash_append(interaction_signature, static_cast<Integer>(m_finished));

    std::uint64_t canvas_signature = interaction_signature;
    yado_hash_append(canvas_signature, m_index);
    yado_hash_append(canvas_signature, m_views);
    yado_hash_append(canvas_signature, static_cast<Integer>(m_list.size()));
    if (m_views > 1 && !m_list.empty()) {
        const Integer first = (m_index / m_views) * m_views;
        const Integer last = std::min<Integer>(static_cast<Integer>(m_list.size()), first + m_views);
        for (Integer item = first; item < last; ++item) {
            if (item < static_cast<Integer>(m_names.size()))
                yado_hash_append(canvas_signature, m_names[static_cast<std::size_t>(item)]);
            if (item < static_cast<Integer>(m_list2.size()))
                for (const auto& member : m_list2[static_cast<std::size_t>(item)])
                    yado_hash_append(canvas_signature, member);
            if (item < static_cast<Integer>(m_skins.size()))
                yado_hash_append(canvas_signature, m_skins[static_cast<std::size_t>(item)]);
            if (item < static_cast<Integer>(m_classic.size()))
                yado_hash_append(canvas_signature, static_cast<Integer>(m_classic[static_cast<std::size_t>(item)]));
            if (item < static_cast<Integer>(m_imgpaths_typed.size()))
                for (const auto& info : m_imgpaths_typed[static_cast<std::size_t>(item)])
                    if (info) yado_hash_append(canvas_signature, info->m_path);
        }
    }
    if (m_index >= 0 && m_index < static_cast<Integer>(m_names.size()))
        yado_hash_append(canvas_signature, m_names[static_cast<std::size_t>(m_index)]);
    if (m_index >= 0 && m_index < static_cast<Integer>(m_list2.size()))
        for (const auto& member : m_list2[static_cast<std::size_t>(m_index)])
            yado_hash_append(canvas_signature, member);
    if (m_index >= 0 && m_index < static_cast<Integer>(m_skins.size()))
        yado_hash_append(canvas_signature, m_skins[static_cast<std::size_t>(m_index)]);
    if (m_index >= 0 && m_index < static_cast<Integer>(m_imgpaths_typed.size()))
        for (const auto& info : m_imgpaths_typed[static_cast<std::size_t>(m_index)])
            if (info) yado_hash_append(canvas_signature, info->m_path);

    yado_hash_append(canvas_signature,m__lastbillskindir.value_or(""));
    yado_hash_append(canvas_signature,select_msg("adventurers","冒険者"));
    for (std::size_t i=0;i<m_list.size();++i) {
        yado_hash_append(canvas_signature,static_cast<Integer>(::cw::util::exists_mutex(m_list[i])));
        if (i<m_isshortcuts.size()) yado_hash_append(canvas_signature,m_isshortcuts[i]);
        if (i<m_classic.size()) yado_hash_append(canvas_signature,static_cast<Integer>(m_classic[i]));
        if (i<m_imgpaths_typed.size()) for(const auto& info:m_imgpaths_typed[i]) if(info) {
            yado_hash_append(canvas_signature,info->m_postype);
            yado_hash_append(canvas_signature,info->m_basecardtype.value_or(""));
        }
    }
    std::uint64_t filter_signature = interaction_signature;
    yado_hash_append(filter_signature, value_string(m_narrow));
    yado_hash_append(filter_signature, value_integer(m_narrow_type));
    yado_hash_append(filter_signature, value_string(m_sort));
    yado_hash_append(filter_signature, static_cast<Integer>(show_additionals));
    yado_hash_append(filter_signature, static_cast<Integer>(m_input.filter.edit.caret));
    yado_hash_append(filter_signature, static_cast<Integer>(m_input.filter.edit.anchor));
    yado_hash_append(filter_signature, m_input.filter.edit.composition);
    yado_hash_append(filter_signature, m_input.filter.edit.composition_start);
    yado_hash_append(filter_signature, m_input.filter.edit.composition_length);
    yado_hash_append(filter_signature, static_cast<Integer>(m_input.filter.edit.horizontal));
    yado_hash_append(filter_signature, static_cast<Integer>(m_input.filter.menu.opened()));

    std::uint64_t footer_signature = interaction_signature;
    yado_hash_append(footer_signature, m_index);
    yado_hash_append(footer_signature, m_views);
    yado_hash_append(footer_signature, static_cast<Integer>(m_list.size()));
    for (const auto& button : {ok, newly, extension, close})
        yado_hash_append(footer_signature,
            static_cast<Integer>(is_enabled(button)));

    auto& retained = dialog_retained_surface();
    const bool full = retained.needs_full({presentation_width, presentation_height});
    if (full) {
        retained.allocate({presentation_width, presentation_height},
                          {240, 240, 240, 255});
        retained.capture_base();
    }
    const ::cw::lib::Rect canvas_rect{0, filter_height,
        presentation_width, canvas_height};
    const ::cw::lib::Rect filter_rect{0, 0, presentation_width, filter_height};
    const ::cw::lib::Rect footer_rect{0, presentation_height-controls_height,
        presentation_width, controls_height};
    const bool canvas_changed = retained.component_changed(
        "yado.canvas", canvas_rect, canvas_signature);
    const bool filter_changed = show_additionals && retained.component_changed(
        "yado.filter", filter_rect, filter_signature);
    const bool footer_changed = retained.component_changed(
        "yado.footer", footer_rect, footer_signature);
    retained.restore_dirty();
    auto surface = retained.surface;
    if (canvas_changed) {
        if (const auto canvas = render_yado_canvas(
                *this, presentation_width, canvas_height))
            ::cw::lib::ui::blend_surface(surface, canvas, 0, filter_height);
    }

    // 内容のキャンバスは YadoSelect 自身が公開する。 一覧モ
    // ードでは各表示セルを独立して選択でき、実行操作は選択とは分ける。
    if (m_views > 1 && !m_list.empty()) {
        const Integer first = (m_index / m_views) * m_views;
        const Integer cell_width = presentation_width / 2;
        const Integer cell_height = canvas_height / std::max<Integer>(1, m_lines);
        for (Integer slot = 0; slot < m_views; ++slot) {
            const Integer item = first + slot;
            if (item >= static_cast<Integer>(m_list.size())) break;
            presentation.controls.push_back({Action::Select, static_cast<int>(item),
                static_cast<float>((slot % 2) * cell_width),
                static_cast<float>(filter_height + (slot / 2) * cell_height),
                static_cast<float>(cell_width), static_cast<float>(cell_height), true});
        }
    } else {
        presentation.controls.push_back({
            Action::Select, -1, 0.0F, static_cast<float>(filter_height),
            static_cast<float>(presentation_width),
            static_cast<float>(canvas_height), can_clickcenter()});
        // 他の選択画面と同じ、モデル側の四分割配置とカーソルキャッシュを使
        // う。 隅のボタンの当たり判定が優先されるよう、その前に配置する。
        m_side_navigation.append(presentation, canvas_rect,
                                 m_views == 1 && m_list.size() > 1U, -1);
    }

    if (show_additional_controls_button())
        draw_yado_control(presentation, surface, {},
            {presentation_width - scale(28), filter_height + scale(2),
             scale(24), scale(24)},
            Action::Auxiliary, 4, true, show_additionals,
            select_additional_controls_image(show_additionals),
            canvas_changed, 16);

    if (show_additionals) {
        const ::cw::lib::Rect narrow_rect{
            scale(6), scale(4), presentation_width / 3, scale(12)};
        const ::cw::lib::Rect narrow_type_rect{
            presentation_width / 3 + scale(9), scale(4),
            presentation_width / 4, scale(12)};
        const ::cw::lib::Rect sort_rect{
            presentation_width * 7 / 12 + scale(10), scale(4),
            presentation_width / 5, scale(12)};
        if (filter_changed) {
            ::cw::lib::ui::fill_rect(surface, narrow_rect, {255, 255, 255, 255});
            ::cw::lib::ui::draw_sunken_border(surface, narrow_rect);
        }
        const auto narrow = value_string(m_narrow);
        draw_select_input(surface,*this,narrow_rect,scale(15));
        presentation.controls.push_back({
            Action::Auxiliary, 3, static_cast<float>(narrow_rect.x),
            static_cast<float>(narrow_rect.y),
            static_cast<float>(narrow_rect.width),
            static_cast<float>(narrow_rect.height), true});

        const auto narrow_labels=select_choice_labels<YadoSelect>(false);
        const auto narrow_type = std::clamp<Integer>(
            value_integer(m_narrow_type), 0,
            static_cast<Integer>(narrow_labels.size() - 1U));
        draw_yado_control(presentation, surface,
                          narrow_labels[static_cast<std::size_t>(narrow_type)]+" ▾",
                          narrow_type_rect, Action::Auxiliary, 1, true, false,
                          {}, filter_changed);

        const auto sort_labels=select_choice_labels<YadoSelect>(true);
        const auto current_sort = value_string(m_sort, "None");
        const Integer sort = current_sort == "Name" ? 1 :
                             current_sort == "Skin" ? 2 : 0;
        draw_yado_control(presentation, surface,
                          sort_labels[static_cast<std::size_t>(sort)]+" ▾", sort_rect,
                          Action::Auxiliary, 2, true, false, {}, filter_changed);
        presentation.wants_text_input = true;
        presentation.input_text = narrow;
    }

    const Integer bottom_y = presentation_height - controls_height;
    if (footer_changed) ::cw::lib::ui::fill_rect(
        surface, {0, bottom_y, presentation_width, controls_height},
        {232, 232, 232, 255});
    const Integer nav_y = presentation_height - layout.action_top_offset +
        (layout.action_height - layout.navigation_height) / 2;
    const Integer navigation_size = layout.navigation_size;
    const Integer navigation_height = layout.navigation_height;
    const bool can_page = m_list.size() > 1U;
    draw_yado_control(presentation, surface, "先頭",
        {0, nav_y, navigation_size, navigation_height},
        Action::PreviousGroup, -1, can_page, false,
        common_select_button_bitmap("LJUMP"), footer_changed);
    draw_yado_control(presentation, surface, "前",
        {navigation_size, nav_y, navigation_size, navigation_height},
        Action::Previous, -1, can_page, false,
        common_select_button_bitmap("LMOVE"), footer_changed);
    draw_yado_control(presentation, surface, "次",
        {presentation_width - navigation_size * 2, nav_y,
         navigation_size, navigation_height},
        Action::Next, -1, can_page, false,
        common_select_button_bitmap("RMOVE"), footer_changed);
    draw_yado_control(presentation, surface, "末尾",
        {presentation_width - navigation_size, nav_y,
         navigation_size, navigation_height},
        Action::NextGroup, -1, can_page, false,
        common_select_button_bitmap("RJUMP"), footer_changed);

    constexpr Integer action_count = 5;
    const Integer action_width = layout.action_width;
    const Integer action_height = layout.action_height;
    const Integer action_gap = layout.action_gap;
    const Integer action_total = action_count * action_width +
        (action_count - 1) * action_gap;
    Integer action_x = navigation_size * 2 +
        (presentation_width - navigation_size * 4 - action_total) / 2;
    const Integer action_y = presentation_height - layout.action_top_offset;
    const bool classic = m_index >= 0 &&
        m_index < static_cast<Integer>(m_classic.size()) &&
        m_classic[static_cast<std::size_t>(m_index)];
    const auto add_action = [&](const String& label, const Action action,
                                const int index, const bool enabled,
                                const bool primary = false) {
        draw_yado_control(presentation, surface, label,
            {action_x, action_y, action_width, action_height},
            action, index, enabled, primary, {}, footer_changed);
        action_x += action_width + action_gap;
    };
    add_action(classic ? select_msg("convert", "変換")
                       : select_msg("decide", "決定"),
               Action::Accept, -1, is_enabled(ok), true);
    add_action(select_msg("new", "新規"), Action::Create, -1,
               is_enabled(newly));
    add_action(select_msg("extension", "拡張"), Action::Auxiliary, 0,
               is_enabled(extension));
    add_action(select_msg(m_views == 1 ? "member_list" : "member_one",
                          m_views == 1 ? "一覧" : "一人"),
               Action::Toggle, -1, true);
    add_action(select_msg("entry_cancel", "中止"), Action::Cancel, -1,
               is_enabled(close));

    const bool presentation_changed = canvas_changed || filter_changed || footer_changed;
    draw_select_focus(presentation,surface,*this,presentation_changed);
    draw_select_choice_popup(presentation,surface,*this,presentation_changed);
    if(m__processing || m_finished) {
        presentation.wants_text_input=false;
        for(auto& control:presentation.controls) control.enabled=false;
    }
    retained.finish();
    presentation.damage = retained.damage;
    presentation.surface_revision = retained.revision;
    presentation.surface = std::move(surface);
    return presentation;
}

void YadoSelect::dialog_presentation_action(
    const ::cw::dialog::DialogPresentationAction arg_action,
    const int arg_index)
{
    if(select_input_action(*this,arg_action,arg_index)) return;
    using Action = ::cw::dialog::DialogPresentationAction;
    if (m__processing || m_finished) return;
    if(select_choice_action(*this,arg_action,arg_index)) return;
    select_focus_action(*this,arg_action,arg_index);
    if (arg_action == Action::Create) {
        OnClickNewBtn(nullptr);
        return;
    }
    if (arg_action == Action::Cancel) {
        select_play_sound("click");
        m_decision_pending = false;
        m_finished = true;
        m_modal_result = 0;
        return;
    }
    if (arg_action == Action::Select && arg_index == -1) {
        // Py と同様、キャンバスでの選択は OK ボタンとは別に OnSelect
        // を通す。 元の一覧が実際に空の場合だけ、最初の宿を作るダイアログを開く。
        OnSelect(nullptr);
        return;
    }
    if ((arg_action == Action::Select || arg_action == Action::Activate) && arg_index >= 0 &&
        arg_index < static_cast<int>(m_list.size())) {
        if (m_index != arg_index) select_play_sound("click");
        m_index = arg_index;
        index_changed();
        enable_btn();
        if (arg_action == Action::Activate) OnOk(nullptr);
        return;
    }
    if (arg_action == Action::Toggle) {
        select_play_sound("equipment");
        save_views(m_views == 1);
        m__bg = Value{};
        enable_btn();
        return;
    }
    if (arg_action == Action::Accept) {
        OnOk(nullptr);
        return;
    }
    if (arg_action == Action::Auxiliary) {
        if (arg_index == 0) {
            OnClickExBtn(nullptr);
        } else if (arg_index == 1) {
            open_select_choice(*this,arg_index);
        } else if (arg_index == 2) {
            open_select_choice(*this,arg_index);
        } else if (arg_index == 4) {
            select_toggle_additionals(*this);
        }
        return;
    }
    if (m_list.empty()) return;
    const Integer count = static_cast<Integer>(m_list.size());
    const Integer page_step = m_views > 1 && count > m_views ? m_views : 1;
    const Integer jump = m_views > 1 ? std::max<Integer>(1, m_views * m_max_views) : 10;
    const Integer page = m_views > 0 ? m_index / m_views : 0;
    const Integer page_count = (count + std::max<Integer>(1, m_views) - 1) /
        std::max<Integer>(1, m_views);
    if (arg_action == Action::PreviousGroup)
        m_index = page == 0 ? count - 1 : std::max<Integer>(0, m_index - jump);
    else if (arg_action == Action::Previous) {
        const Integer range = m_views > 1 && count > m_views ? page_count * m_views : count;
        m_index = wrap_index(m_index - page_step, range);
        if (m_index >= count) m_index = count - 1;
    } else if (arg_action == Action::Next) {
        const Integer range = m_views > 1 && count > m_views ? page_count * m_views : count;
        m_index = wrap_index(m_index + page_step, range);
        if (m_index >= count) m_index = count - 1;
    }
    else if (arg_action == Action::NextGroup)
        m_index = page == page_count - 1 ? 0
                                        : std::min<Integer>(count - 1, m_index + jump);
    else
        return;
    select_play_sound("page");
    index_changed();
    enable_btn();
}

void YadoSelect::dialog_presentation_text_event(const ::cw::dialog::DialogTextEditingEvent& event) {
    select_input_text(*this,event.text,event.composition,event.start,event.length);
}
void YadoSelect::dialog_presentation_pointer(::cw::dialog::DialogPresentationAction action,int index,float x,float y,bool pressed) {
    select_input_pointer(*this,action,index,x,y,pressed);
}
void YadoSelect::dialog_presentation_context(float x,float y) {select_input_context(*this,x,y);}
void YadoSelect::dialog_presentation_hover(float x,float y) {
    if(select_choice_hover(*this,x,y)) return;
    if(select_input_busy(*this) || !select_input_visible(*this)) {m_input.filter.retire();return;}
    if(m_input.filter.menu.hover(x,y,m_input.filter.edit)) request_dialog_presentation_refresh();
}

void YadoSelect::dialog_presentation_text(const std::string& arg_text,
                                          const bool arg_composition)
{
    if(m_finished || m__processing) return;
    select_input_text(*this,arg_text,arg_composition);
}

void YadoSelect::dialog_presentation_enter(::cw::dialog::DialogPresentationModifiers modifiers) {
    dialog_presentation_key(::cw::dialog::DialogPresentationKey::Enter,true,modifiers);
}

void YadoSelect::dialog_presentation_key(::cw::dialog::DialogPresentationKey key,bool pressed,
    ::cw::dialog::DialogPresentationModifiers modifiers) {
    if(!pressed || m_finished || m__processing) return;
    const bool visible=!::cw::g_cwpy || !::cw::g_cwpy->m_setting || ::cw::g_cwpy->m_setting->m_show_additional_yado;
    if(select_priority_input_key(*this,key,modifiers)) return;
    if(select_choice_key(*this,key,modifiers,visible)) return;
    if(select_focus_key(*this,key,modifiers)) return;
    (void)select_additional_controls_key(*this,key,modifiers);
}

void YadoSelect::dialog_presentation_scroll(float x,float y,int lines) {
    if(!m_finished && !m__processing) select_choice_scroll(*this,x,y,lines);
}

bool YadoSelect::dialog_presentation_finished() const noexcept
{
    return m_finished;
}

void YadoSelect::dialog_presentation_complete()
{
    m_input.narrow_update.cancel();
    if(conversion_folder_pending()) {
        cancel_conversion_folder();m_decision_pending=false;m_finished=true;m_modal_result=0;
        return;
    }
    if (!m_decision_pending || m_modal_result == 0 || m_index < 0 ||
        m_index >= static_cast<Integer>(m_list.size()) || !::cw::g_cwpy)
        return;
    const String path = m_list[static_cast<std::size_t>(m_index)];
    m_decision_pending = false;
    ::cw::g_cwpy->exec_func([path](const auto&, const auto&) {
        if (!::cw::g_cwpy) return ::cw::lib::Value{};
        try {
            const auto load_path = ::cw::g_cwpy->select_novel_save(path);
            if (load_path) (void)::cw::g_cwpy->load_yado(*load_path, true);
        } catch (const std::exception& error) {
            const String details = String(error.what()) +
                "\r\n\r\nStack trace:\r\n" +
                ::cw::thread::current_exception_stacktrace();
            if (::cw::g_cwpy->m_backend)
                ::cw::g_cwpy->m_backend->notify(
                    *::cw::g_cwpy, "CWPy内部エラー", details, true);
            ::cw::g_cwpy->play_sound("error", false, 100, 1, 0, 0, false);
        } catch (...) {
            const String details =
                "不明なエラーが発生しました。\r\n\r\nStack trace:\r\n" +
                ::cw::thread::current_exception_stacktrace();
            if (::cw::g_cwpy->m_backend)
                ::cw::g_cwpy->m_backend->notify(
                    *::cw::g_cwpy, "CWPy内部エラー", details, true);
            ::cw::g_cwpy->play_sound("error", false, 100, 1, 0, 0, false);
        }
        return ::cw::lib::Value{};
    });
}

void YadoSelect::update_additionals()
{
    // wx の切り替えボタン状態はネイティブアダプターが管理し、一覧の状態はモデル内で保持する。
}

void YadoSelect::_add_topsizer()
{
    // 絞り込み・並べ替えの実配置はSelectUiBackend::layout_select_dialogが担当する。
    // m_narrow/m_narrow_type/m_sortはmodel側に保持し、backend交換時も再利用する。
}

void YadoSelect::_on_narrowcondition()
{
    if (::cw::g_cwpy && ::cw::g_cwpy->m_setting)
        ::cw::g_cwpy->m_setting->m_yado_narrowtype = value_integer(m_narrow_type);
    update_narrowcondition();
    select_feedback(this, SelectUiKind::yado);
}

void YadoSelect::update_narrowcondition()
{
    const String selected = m_index >= 0 && m_index < static_cast<Integer>(m_list.size())
        ? m_list[static_cast<std::size_t>(m_index)] : String{};
    auto objects = _list_to_obj();
    const String narrow = lower_ascii(value_string(m_narrow));
    const Integer type = value_integer(m_narrow_type);
    const bool do_narrow = !narrow.empty() &&
        (!::cw::g_cwpy || !::cw::g_cwpy->m_setting ||
         ::cw::g_cwpy->m_setting->m_show_additional_yado);
    if (do_narrow) {
        std::vector<std::shared_ptr<_YadoObj>> filtered;
        for (const auto& object : objects) {
            if (!object) continue;
            const bool name = lower_ascii(value_string(object->m_name)).find(narrow) != String::npos;
            narrow_context.yado = object;
            narrow_context.narrow = narrow;
            const bool member = YadoSelect_update_narrowcondition_has_advname();
            const bool skin = lower_ascii(value_string(object->m_skin)).find(narrow) != String::npos;
            if ((type == 0 && (name || member || skin)) || (type == 1 && name) ||
                (type == 2 && member) || (type == 3 && skin)) filtered.push_back(object);
        }
        objects = std::move(filtered);
    }
    _sort_objs(objects);
    _obj_to_list(objects);
    auto iter = std::find_if(m_list.begin(), m_list.end(), [&selected](const auto& value) {
        return !selected.empty() && value == selected;
    });
    if (iter != m_list.end()) m_index = static_cast<Integer>(iter - m_list.begin());
    else if (!m_list.empty()) m_index = wrap_index(m_index, static_cast<Integer>(m_list.size()));
    else m_index = 0;
    enable_btn();
}

std::shared_ptr<::cw::lib::Bitmap> load_yado_bill(const String& base)
{
    const String name = "Table/Bill";
    const String requested = base.empty() ? name :
        ::cw::lib::encoding::path_to_utf8(std::filesystem::u8path(base) / std::filesystem::u8path(name));
    const String resolved = ::cw::util::find_resource(requested, ::cw::g_M_IMG);
    // Py と同様、見つからないリソースは存在しないものとして扱う。 共
    // 通ローダーの相対パスの代替画像で、張り紙を黒い画像に置き換えない。
    return resolved.empty() ? std::shared_ptr<::cw::lib::Bitmap>{}
        : get_ui_backend()->load_select_bitmap(resolved, false);
}

std::shared_ptr<::cw::lib::Bitmap> YadoSelect::_get_bg()
{
    if (const auto* object = std::get_if<::cw::lib::ObjectPtr>(&m__bg.storage()))
        if (const auto bitmap = std::dynamic_pointer_cast<::cw::lib::Bitmap>(*object)) return bitmap;
    const String current = ::cw::g_cwpy && ::cw::g_cwpy->m_rsrc
        ? ::cw::g_cwpy->m_rsrc->m_skindir : String{};
    const auto bitmap = load_yado_bill(current);
    if (bitmap) m__bg = Value(std::static_pointer_cast<::cw::lib::Object>(bitmap));
    return bitmap;
}

std::shared_ptr<::cw::lib::Bitmap> YadoSelect::_get_draw_bg()
{
    String skin;
    if (m_views != 1 && m__lastbillskindir) skin = *m__lastbillskindir;
    else if (!m_list.empty() && m_views == 1 && m_index >= 0 &&
             m_index < static_cast<Integer>(m_skins.size())) {
        skin = m_skins[static_cast<std::size_t>(m_index)];
        m__lastbillskindir = skin;
    } else if (::cw::g_cwpy && ::cw::g_cwpy->m_rsrc)
        skin = ::cw::g_cwpy->m_rsrc->m_skindir;
    return load_yado_bill(skin);
}

void YadoSelect::OnNumberKeyDown(std::shared_ptr<::cw::lib::Window> arg_event)
{
    if (m__processing || !arg_event) return;
    Integer index = arg_event->id;
    for (std::size_t i = 0; i < m_sortkeydown.size(); ++i) {
        if (value_integer(m_sortkeydown[i], -1) == arg_event->id) {
            index = static_cast<Integer>(i);
            break;
        }
    }
    if (0 <= index && index <= 2) {
        // Python と同様、アクセラレーターは表示中の並べ
        // 替え欄と同じ選択イベントとして送る。 設定の保存とペ
        // ージ変更の効果音も行うため、OnSort を通す。
        auto event = std::make_shared<::cw::lib::Window>();
        event->id = index;
        OnSort(std::move(event));
    }
}

void YadoSelect::OnSort(std::shared_ptr<::cw::lib::Window> arg_event)
{
    if (m__processing) return;
    const Integer index = arg_event ? arg_event->id : 0;
    const String next = index == 1 ? String("Name") : index == 2 ? String("Skin") : String("None");
    if (value_string(m_sort, "None") != next) {
        m_sort = next;
        if (::cw::g_cwpy && ::cw::g_cwpy->m_setting)
            ::cw::g_cwpy->m_setting->m_sort_yado = next;
        update_narrowcondition();
        select_feedback(this, SelectUiKind::yado, "page");
    }
}

void YadoSelect::_sort_objs(std::vector<std::shared_ptr<::cw::dialog::select::_YadoObj>>& arg_objs)
{
    const String sort = value_string(m_sort, "None");
    auto less = [sort](const std::shared_ptr<_YadoObj>& left, const std::shared_ptr<_YadoObj>& right) {
        // Py の sort_by_attr と同様、文字列は LogicalStr の規則で比較する。
        // Name/Skin モードでは両方の表示項目、数値の順序、ディレクトリー名の順に比較する。
        const String left_name = value_string(left ? left->m_name : Value{});
        const String right_name = value_string(right ? right->m_name : Value{});
        const String left_skin = value_string(left ? left->m_skin : Value{});
        const String right_skin = value_string(right ? right->m_skin : Value{});
        const String left_dir = left ? left->m_yadodirname : String{};
        const String right_dir = right ? right->m_yadodirname : String{};
        const Integer left_order = left ? value_integer(left->m_order, (std::numeric_limits<Integer>::max)()) : (std::numeric_limits<Integer>::max)();
        const Integer right_order = right ? value_integer(right->m_order, (std::numeric_limits<Integer>::max)()) : (std::numeric_limits<Integer>::max)();
        if (sort == "Name") {
            if (const auto cmp = ::cw::util::logical_compare(left_name, right_name)) return cmp < 0;
            if (const auto cmp = ::cw::util::logical_compare(left_skin, right_skin)) return cmp < 0;
        } else if (sort == "Skin") {
            if (const auto cmp = ::cw::util::logical_compare(left_skin, right_skin)) return cmp < 0;
            if (const auto cmp = ::cw::util::logical_compare(left_name, right_name)) return cmp < 0;
        }
        if (left_order != right_order) return left_order < right_order;
        if (const auto cmp = ::cw::util::logical_compare(left_name, right_name)) return cmp < 0;
        if (const auto cmp = ::cw::util::logical_compare(left_skin, right_skin)) return cmp < 0;
        return ::cw::util::logical_compare(left_dir, right_dir) < 0;
    };
    std::stable_sort(arg_objs.begin(), arg_objs.end(), std::move(less));
}

/*
 * Python の元コードはキーのタプルを使う。上
 * の比較関数ではタプルの各項目を明示し、 None
 * の並べ替えモードで数値順を維持する。順序を文字
 * 列化すると 10 が 2 より前になってしまう。
 */
/*
    auto key = [sort](const std::shared_ptr<_YadoObj>& object) {
        const String name = value_string(object ? object->m_name : Value{});
        const String skin = value_string(object ? object->m_skin : Value{});
        const String dir = object ? object->m_yadodirname : String{};
        const Integer order = object ? value_integer(object->m_order, (std::numeric_limits<Integer>::max)()) : (std::numeric_limits<Integer>::max)();
        if (sort == "Name") return std::tuple<String, String, Integer, String>{name, skin, order, dir};
        if (sort == "Skin") return std::tuple<String, String, Integer, String>{skin, name, order, dir};
        return std::tuple<String, String, Integer, String>{std::to_string(order), name, order, dir};
    };
    std::stable_sort(arg_objs.begin(), arg_objs.end(), [&key](const auto& left, const auto& right) {
        return key(left) < key(right);
    });
*/

// 旧 Window イベントには選択欄の当たり判定結果がなく、m_views ずつ
// 進めて項目位置を循環させる。 Python はコンボ上のホイール操作をコンボへ
// 送る。通常のポインター入力では、 実際の位置・選択欄の情報を保持する select_choice_scroll
// を先に使い、その後で共通のページ操作を行う。
void YadoSelect::OnMouseWheel(std::shared_ptr<::cw::lib::Window> arg_event)
{
    if (m__processing || m_list.size() <= 1) return;
    const Integer direction = event_wheel(arg_event);
    const Integer step = std::max<Integer>(1, m_views);
    m_index = wrap_index(m_index + (direction > 0 ? -step : step), static_cast<Integer>(m_list.size()));
    index_changed();
    enable_btn();
    select_feedback(this, SelectUiKind::yado, "page");
}

std::vector<std::shared_ptr<::cw::dialog::select::_YadoObj>> YadoSelect::_list_to_obj()
{
    std::vector<std::shared_ptr<_YadoObj>> result;
    // initialize() を使わずダイアログモデルを直接構築する呼
    // び出し側でも、 公開の互換メンバーを使えるように代替動作を残す。
    const auto& names = m__names_typed.empty() ? m_names : m__names_typed;
    const auto& list = m__list_typed.empty() ? m_list : m__list_typed;
    const auto& list2 = m__list2_typed.empty() ? m_list2 : m__list2_typed;
    const auto& skins = m__skins_typed.empty() ? m_skins : m__skins_typed;
    const auto& classic = m__classic_typed.empty() ? m_classic : m__classic_typed;
    const auto& shortcuts = m__isshortcuts_typed.empty() ? m_isshortcuts : m__isshortcuts_typed;
    const auto& images = m__imgpaths_typed.empty() ? m_imgpaths_typed : m__imgpaths_typed;
    const std::size_t count = std::min({names.size(), list.size(), list2.size(), skins.size(),
                                        classic.size(), shortcuts.size(), images.size()});
    result.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        auto object = std::make_shared<_YadoObj>();
        object->initialize(names[i], list[i], list2[i], skins[i], classic[i], shortcuts[i], images[i]);
        result.push_back(std::move(object));
    }
    return result;
}

void YadoSelect::_obj_to_list(std::vector<std::shared_ptr<::cw::dialog::select::_YadoObj>> arg_objs)
{
    m_names.clear(); m_list.clear(); m_list2.clear(); m_skins.clear(); m_classic.clear();
    m_isshortcuts.clear(); m_imgpaths.clear(); m_imgpaths_typed.clear();
    for (const auto& object : arg_objs) {
        if (!object) continue;
        m_names.push_back(value_string(object->m_name));
        m_list.emplace_back(value_string(object->m_yadodir));
        m_list2.push_back(sequence_strings(object->m_advnames));
        m_skins.push_back(value_string(object->m_skin));
        m_classic.push_back(value_bool(object->m_classic));
        m_isshortcuts.emplace_back(value_string(object->m_isshortcut));
        m_imgpaths_typed.push_back(object->m_imgpaths_typed);
    }
    m_imgpaths.assign(m_imgpaths_typed.size(), Value{});
}

void YadoSelect::save_views(bool arg_multi)
{
    m_views = arg_multi ? m_max_views : 1;
    if (::cw::g_cwpy && ::cw::g_cwpy->m_setting)
        ::cw::g_cwpy->m_setting->m_show_multiplebases = arg_multi;
}

void YadoSelect::index_changed()
{
    if (m_list.empty()) m_index = 0;
    else m_index = std::clamp<Integer>(m_index, 0, static_cast<Integer>(m_list.size()) - 1);
    m__bg = Value{};
    enable_btn();
    // Py と同様、ページ・選択の変更ごとに最初の有効な下部ボタンへフォー
    // カスを戻す。 共通表示では、表示中のコントロールから対象を決める。
    m_input.initialized = false;
    request_dialog_presentation_refresh();
}

bool YadoSelect::can_clickcenter()
{
    const bool source_has_items = !m__list_typed.empty();
    if (m_views == 1 && source_has_items &&
        !is_enabled(object_value<::cw::lib::Window>(m_okbtn))) return false;
    if (!source_has_items)
        return is_enabled(object_value<::cw::lib::Window>(m_newbtn));
    if (m_list.empty()) return false;
    if (m_index < 0 || m_index >= static_cast<Integer>(m_list.size())) return false;
    return path_is_directory(m_list[static_cast<std::size_t>(m_index)]);
}

void YadoSelect::enable_btn()
{
    const auto ok = object_value<::cw::lib::Window>(m_okbtn);
    const auto newly = object_value<::cw::lib::Window>(m_newbtn);
    const auto extension = object_value<::cw::lib::Window>(m_exbtn);
    const auto close = object_value<::cw::lib::Window>(m_closebtn);
    const bool has_list = !m_list.empty();
    if (!has_list) {
        set_label(ok, "決定");
        set_enabled(ok, false);
        // 空の UserData も有効であり、このダイアログから最
        // 初の宿を作れるようにする。 新規作成を無効にすると、Web
        // ・移植用ホストが事前の保存データなしで起動できなくなる。
        set_enabled(newly, true);
        // Python と同様、絞り込みで表示される宿がなくなっ
        // ても、新規・拡張・キャンセルは使えるようにする。 宿を
        // 必要とする操作は、拡張の子ダイアログ側で無効にする。
        set_enabled(extension, true);
        set_enabled(close, true);
        return;
    }
    const bool classic = m_index >= 0 && m_index < static_cast<Integer>(m_classic.size()) &&
                         m_classic[static_cast<std::size_t>(m_index)];
    set_label(ok, classic ? "変換" : "決定");
    set_enabled(ok, true);
    set_enabled(newly, true);
    set_enabled(extension, true);
    set_enabled(close, true);
    if (m_index >= 0 && m_index < static_cast<Integer>(m_list.size()) &&
        ::cw::util::exists_mutex(m_list[static_cast<std::size_t>(m_index)]))
        set_enabled(ok, false);
    if (m_index >= 0 && m_index < static_cast<Integer>(m_classic.size()) &&
        m_index < static_cast<Integer>(m_list.size()) && !path_is_directory(m_list[static_cast<std::size_t>(m_index)])) {
        set_enabled(ok, false);
    }
}

void YadoSelect::OnSelect(std::shared_ptr<::cw::lib::Event> arg_event)
{
    if (m__processing || m_finished) return;
    if (m__list_typed.empty()) {
        if (is_enabled(object_value<::cw::lib::Window>(m_newbtn)))
            OnClickNewBtn(nullptr);
        return;
    }
    if (!m_list.empty() && can_clickcenter()) {
        if (m_views > 1 && arg_event) {
            const Integer columns = std::max<Integer>(1, m_views / std::max<Integer>(1, m_lines));
            const Integer cell = std::max<Integer>(
                0, arg_event->position.x + arg_event->position.y * columns);
            m_index = std::min<Integer>(
                (m_index / std::max<Integer>(1, m_views)) * m_views + cell,
                static_cast<Integer>(m_list.size()) - 1);
            index_changed();
            enable_btn();
            select_feedback(this, SelectUiKind::yado, "click");
            return;
        }
        // 一件表示のキャンバスは、Py と同じボタンイベントを送る。 無効状態
        // や Classic 変換の規則は共通の OK ハンドラーにまとめる。
        if (m_views == 1) OnOk(nullptr);
    }
}

void YadoSelect::OnOk(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    if(m_finished || m__processing) return;
    const auto ok = object_value<::cw::lib::Window>(m_okbtn);
    if (m_list.empty() || !is_enabled(ok)) return;
    if (m_index >= 0 && m_index < static_cast<Integer>(m_classic.size()) && m_classic[static_cast<std::size_t>(m_index)])
        _convert_current();
    else {
        m_decision_pending = true;
        m_finished = true;
        m_modal_result = 1;
    }
}

void YadoSelect::OnDropFiles(std::shared_ptr<::cw::lib::Window> arg_event)
{
    if (arg_event) dialog_presentation_drop_files({arg_event->title});
}

void YadoSelect::dialog_presentation_drop_files(const std::vector<std::string>& paths)
{
    if (m_finished || m__processing || paths.empty()) return;
    const auto keep_alive = weak_from_this().lock();
    SelectProcessingScope processing(*this);
    m_filter_choice = {};
    m_input.filter.dragging = false;
    m_conversion_folder_error.clear();
    // Py の OnDropFiles と同様、全フォルダーを順番に変換する。 ファ
    // イル選択経路の変換・確認を再利用し、一括処理全体で親の入力ロックを保持する。
    try {
        const auto root = std::filesystem::current_path();
        for (const auto& path : paths) {
            if (m_finished) break;
            if (std::filesystem::current_path() != root)
                throw std::runtime_error("宿の変換中に作業場所が変更されました。選択し直してください。");
            if (path.empty()) continue;
            if (path.find('\0') != String::npos) {
                // ネイティブのパス API は NUL で文字列を切り詰める場合がある。 ネイ
                // ティブパスを作る前に、共通の変換入口で Py の exists()==False
                // 相当を通知し、 その後は Py と同様に残りのドロップ項目を処理する。
                conv_yado(path, false, false, {});
            } else {
                const auto source = std::filesystem::u8path(path);
                ::cw::lib::documents::Document document;
                document.location = ::cw::lib::encoding::path_to_utf8(
                    (source.is_absolute() ? source : root / source).lexically_normal());
                convert_acquired_yado(document);
            }
            {
                ::cw::lib::RuntimeAccessSuspension suspend;
                std::this_thread::sleep_for(std::chrono::milliseconds(300));
            }
        }
    } catch (const std::exception& error) {
        m_conversion_folder_error = error.what();
        throw;
    }
}

void YadoSelect::OnClickExBtn(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    if(m_finished || m__processing) return;
    select_play_sound("click");
    // Python と同様、ExtensionDialog の六項目を作る。
    // 実際のモーダルを共通表示ホストへ渡す前に、各項目の利用可否を求める。
    m_extension_enabled.assign(6, false);
    if (m_list.empty()) {
        // Python と同様、現在選択した宿がなくても、全
        // 体の変換用一時領域がロック中なら変換を無効にする。
        m_extension_enabled[3] = !::cw::util::exists_mutex(::cw::g_tempdir_init);
    } else if (m_index >= 0 && m_index < static_cast<Integer>(m_list.size())) {
        const bool directory = path_is_directory(m_list[static_cast<std::size_t>(m_index)]);
        const bool classic = m_index < static_cast<Integer>(m_classic.size()) && m_classic[static_cast<std::size_t>(m_index)];
        const bool hasmutexlocal = directory &&
            !::cw::util::exists_mutex(m_list[static_cast<std::size_t>(m_index)]);
        bool cantransfer = directory;
        Integer normal_count = 0;
        for (std::size_t i = 0; i < m_list.size(); ++i) {
            if (i >= m_classic.size() || !m_classic[i]) ++normal_count;
        }
        if (cantransfer) {
            for (std::size_t i = 0; i < m_list.size(); ++i) {
                if (i < m_classic.size() && !m_classic[i] &&
                    ::cw::util::exists_mutex(m_list[i])) {
                    cantransfer = false;
                    break;
                }
            }
        }
        cantransfer = cantransfer && normal_count > 1;
        m_extension_enabled = {
            !classic && hasmutexlocal,
            !classic && hasmutexlocal,
            cantransfer,
            !::cw::util::exists_mutex(::cw::g_tempdir_init),
            !classic && hasmutexlocal,
            hasmutexlocal
        };
    }
    const String title = m_list.empty() || m_index<0 || m_index>=static_cast<Integer>(m_names.size())
        ? select_msg("extension_title_2", "拡張機能")
        : select_msg_format("extension_title", "拡張機能 - %sの編集",
                            m_names[static_cast<std::size_t>(m_index)]);
    std::vector<Value> items;
    const auto add_item = [&](const String& name, const String& description,
                              ::cw::lib::Callback callback, const bool enabled) {
        auto item = std::make_shared<::cw::dialog::etc::ExtensionItem>();
        item->m_name = name;
        item->m_description = description;
        item->m_callback = std::move(callback);
        item->m_enabled = enabled;
        items.emplace_back(Value(std::static_pointer_cast<::cw::lib::Object>(item)));
    };
    const auto callback = [weak=weak_from_this()](auto method) {
        return [weak, method](const ::cw::lib::Arguments&,
                              const ::cw::lib::KeywordArguments&) -> Value {
            if(const auto self=weak.lock(); self && !self->m_finished && !self->m__processing)
                (self.get()->*method)();
            return {};
        };
    };
    add_item(select_msg("settings", "設定"),
             select_msg("edit_base_description", "この拠点の名称などの設定を編集します。"),
             callback(&YadoSelect::rename_yado), m_extension_enabled[0]);
    add_item(select_msg("copy", "複製"),
             select_msg("copy_base_description", "選択中の拠点を複製します。"),
             callback(&YadoSelect::copy_yado), m_extension_enabled[1]);
    add_item(select_msg("transfer", "転送"),
             select_msg("transfer_base_description", "他の拠点のデータをこの拠点へコピーします。"),
             callback(&YadoSelect::trasnfer_yadodata), m_extension_enabled[2]);
    add_item("変換", "CardWirth用の宿データをCardWirthPy用の拠点データに変換します。",
             callback(&YadoSelect::_conv_yado), m_extension_enabled[3]);
    add_item("逆変換", "選択中の拠点データをCardWirth用のデータに逆変換します。",
             callback(&YadoSelect::unconv_yado), m_extension_enabled[4]);
    add_item(select_msg("delete", "削除"),
             select_msg("delete_base_description", "選択中の拠点を削除します。"),
             callback(&YadoSelect::delete_yado), m_extension_enabled[5]);
    auto dialog = std::make_shared<::cw::dialog::etc::ExtensionDialog>();
    dialog->initialize(m_toppanel_window, title, std::move(items));
    select_run_etc_modal(dialog);
    enable_btn();
}

void YadoSelect::rename_yado()
{
    if (m_list.empty() || m_index < 0 || m_index >= static_cast<Integer>(m_list.size())) return;
    if (!path_is_directory(m_list[static_cast<std::size_t>(m_index)])) return;
    select_play_sound("click");
    auto editor = std::make_shared<::cw::dialog::create::YadoCreater>();
    editor->initialize(nullptr, m_list[static_cast<std::size_t>(m_index)]);
    (void)::cw::dialog::show_common_modal_presentation(editor);
    // どちらの経路でも編集の確定処理は OnOk が行う。 ここで edit_yado
    // を呼ぶと、Win32 のモーダル終了後に二度目の編集を実行してしまう。
    if (editor->m_modal_result_ok && editor->m_yadodir) {
        // Py と同様、名前変更の成功音は編集画面側が鳴らす。
        update_list(*editor->m_yadodir, true);
    }
}

void YadoSelect::copy_yado()
{
    if (m_list.empty() || m_index < 0 || m_index >= static_cast<Integer>(m_list.size())) return;
    if (!path_is_directory(m_list[static_cast<std::size_t>(m_index)])) return;
    const String source = m_list[static_cast<std::size_t>(m_index)];
    const String source_name = m_names[static_cast<std::size_t>(m_index)];
    select_play_sound("signal");
    if (!select_confirm(select_msg("message", "メッセージ"),
                        select_msg_format("copy_base", "「%s」を複製しますか？", source_name)))
        return;
    if (!::cw::util::create_mutex("Yado")) { select_play_sound("error"); return; }
    try {
        if (!::cw::util::create_mutex(source)) {
            select_play_sound("error");
            ::cw::util::release_mutex();
            return;
        }
        // Py と同様、宿の利用可否の確認用ロックはすぐ解放する。 コピーと一
        // 覧更新が終わるまで保持するのは、共通の Yado ロックだけとする。
        ::cw::util::release_mutex();
        const String source_environment = ::cw::lib::encoding::path_to_utf8(
            std::filesystem::u8path(source) / "Environment.xml");
        const auto environment = ::cw::data::xml2etree(
            source_environment, "", std::nullopt, std::nullopt, true);
        const String stored_name = environment
            ? environment->gettext("Property/Name",
                ::cw::lib::encoding::path_to_utf8(std::filesystem::u8path(source).filename()))
            : source_name;
        const String copy_name = "コピー - " + stored_name;
        const String safe_name = ::cw::binary::util::check_filename(copy_name);
        const String destination = ::cw::binary::util::check_duplicate(
            ::cw::lib::encoding::path_to_utf8(std::filesystem::u8path(source).parent_path() / safe_name));
        const auto source_path = std::filesystem::u8path(source);
        const auto destination_path = std::filesystem::u8path(destination);
        const bool mounted_source = ::cardwirth::platform::is_mounted_user_path(source_path);
        const bool mounted_destination = ::cardwirth::platform::is_mounted_user_path(destination_path);
        if (mounted_source || mounted_destination) {
            if (!mounted_source || !mounted_destination ||
                !::cardwirth::platform::copy_mounted_user_path(
                    source_path, destination_path, false))
                throw std::runtime_error("保存領域の宿を複製できませんでした。");
        } else {
            std::filesystem::copy(source_path, destination_path,
                                  std::filesystem::copy_options::recursive);
        }
        restore_copied_directory_times(source_path, destination_path);
        const String environment_path = ::cw::lib::encoding::path_to_utf8(std::filesystem::u8path(destination) / "Environment.xml");
        if (environment) {
            const auto name = environment->find("Property/Name");
            if (name && *name) (*name)->set_text(copy_name);
            else environment->insert("Property", ::cw::data::make_element("Name", copy_name), 0);
            environment->write_file(environment_path);
        }
        select_play_sound("harvest");
        if (::cw::g_cwpy && ::cw::g_cwpy->m_setting)
            ::cw::g_cwpy->m_setting->insert_yadoorder(::cw::lib::encoding::path_to_utf8(std::filesystem::u8path(destination).filename()));
        update_list(destination, false);
        ::cw::util::release_mutex();
    } catch (...) {
        ::cw::util::release_mutex();
        throw;
    }
}

void YadoSelect::trasnfer_yadodata()
{
    if(m_finished || m__processing || m_index<0 || m_index>=static_cast<Integer>(m_list.size())) return;
    const String selected=m_list[static_cast<std::size_t>(m_index)];
    if(!path_is_directory(selected)) return;
    SelectProcessingScope processing(*this);
    // Python と同様、モーダル操作全体で共通の Yado ロックを
    // 保持し、 転送データを選ばせる前に一覧内の全宿の利用可否を調べる。
    if(!::cw::util::create_mutex("Yado")) {select_play_sound("error");return;}
    ::cw::lib::finally_call([&] {
    try {
        Integer probes = 0;
        const bool available = ::cw::lib::finally_call([&] {
            for(const auto& path:m_list) {
                if(!::cw::util::create_mutex(path)) {select_play_sound("error");return false;}
                ++probes;
            }
            return true;
        }, [&] {
            while(probes>0) {::cw::util::release_mutex();--probes;}
        });
        if (!available) return;
        std::vector<String> dirs,names;
        for(std::size_t i=0;i<m_list.size();++i) {
            if(i<m_classic.size() && !m_classic[i]) {
                dirs.push_back(m_list[i]);
                names.push_back(i<m_names.size()?m_names[i]:m_list[i]);
            }
        }
        if(dirs.empty()) return;
        select_play_sound("click");
        auto transfer=std::make_shared<::cw::dialog::transfer::TransferYadoDataDialog>();
        transfer->initialize(m_toppanel_window,std::move(dirs),std::move(names),selected);
        if(::cw::dialog::show_common_modal_presentation(transfer) && transfer->m_modal_result==1 && !m_finished)
            update_list(selected,false);
    } catch(const std::exception& error) {
        throw;
    }
    }, [] { ::cw::util::release_mutex(); });
}

void YadoSelect::delete_yado()
{
    if (m_list.empty() || m_index < 0 || m_index >= static_cast<Integer>(m_list.size())) return;
    if (!path_is_directory(m_list[static_cast<std::size_t>(m_index)])) return;
    const std::size_t index = static_cast<std::size_t>(m_index);
    const String path = m_list[index];
    if (!::cw::util::create_mutex("Yado")) {
        select_play_sound("error");
        return;
    }
    try {
        if (!::cw::util::create_mutex(path)) {
            select_play_sound("error");
        } else {
            // Py と同様、確認を尋ねる前に選択宿の利用可否を調べ、その確認用ロック
            // は解放して Windows でディレクトリーを削除できるようにする。
            // 共通の Yado ロックは、確認・削除・一覧更新が終わるまで保持する。
            ::cw::util::release_mutex();
            const String shortcut = index < m_isshortcuts.size() ? m_isshortcuts[index] : String{};
            const String name = index < m_names.size() ? m_names[index] : path;
            const String prompt = shortcut.empty()
                ? select_msg_format("delete_base", "「%s」を削除しますか？", name)
                : select_msg_format("delete_base_shortcut", "ショートカット「%s」を削除しますか？", name);
            if (select_confirm(select_msg("message", "メッセージ"), prompt)) {
                if (!shortcut.empty()) ::cw::util::remove(shortcut, false);
                else ::cw::util::remove(path, true);
                if (index < m_classic.size() && !m_classic[index])
                    ::cw::util::remove(::cw::util::join_paths({String("Data/Temp/Local"), path}), false);
                select_play_sound("dump");
                if (::cw::g_cwpy && ::cw::g_cwpy->m_setting)
                    ::cw::g_cwpy->m_setting->m_yado_order.erase(::cw::lib::encoding::path_to_utf8(std::filesystem::u8path(path).filename()));
                const String next = index + 1 < m_list.size() ? m_list[index + 1] : (index > 0 ? m_list[index - 1] : String{});
                update_list(next, false);
            }
        }
    } catch (...) {
        ::cw::util::release_mutex();
        throw;
    }
    ::cw::util::release_mutex();
}

void YadoSelect::OnClickNewBtn(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    select_play_sound("click");
    auto creator = std::make_shared<::cw::dialog::create::YadoCreater>();
    creator->initialize(nullptr, std::nullopt);
    (void)::cw::dialog::show_common_modal_presentation(creator);
    // 作成の確定はダイアログの Accept 操作で一度だけ行
    // う。 キャンセルで閉じた直後に宿が作成されることも防ぐ。
    if (creator->m_modal_result_ok && creator->m_yadodir) {
        select_play_sound("harvest");
        if (::cw::g_cwpy && ::cw::g_cwpy->m_setting)
            ::cw::g_cwpy->m_setting->insert_yadoorder(::cw::lib::encoding::path_to_utf8(std::filesystem::u8path(*creator->m_yadodir).filename()));
        update_list(*creator->m_yadodir, true);
    }
}

void YadoSelect::_conv_yado()
{
    if(m__processing || m_finished) return;
    select_play_sound("click");
    request_conversion_folder();
}

bool YadoSelect::request_conversion_folder(ConversionFolderRequest start)
{
    using namespace ::cw::lib::documents;
    if(m__processing || m_finished || weak_from_this().expired()) return false;
    if(!start) start=::cw::dialog::request_presentation_document;
    const auto token=std::make_shared<int>(0);m_conversion_folder_token=token;
    m__processing=true;m_filter_choice={};m_conversion_folder_error.clear();
    request_dialog_presentation_refresh();
    try {
        Request request;request.operation=Operation::OpenFolder;request.multiple=false;
        request.title="CardWirthの宿のデータをCardWirthPy用に変換します。\n変換する宿のフォルダを選択してください";
        m_conversion_root=::cw::lib::encoding::path_to_utf8(std::filesystem::current_path());
        request.initial_location=m_conversion_root;
        const auto weak=weak_from_this();
        auto ticket=start(request,[weak,token](Result result) {
            if(const auto owner=weak.lock()) owner->finish_conversion_folder(token,std::move(result));
        });
        if(m_conversion_folder_token==token) {
            m_conversion_folder_ticket=std::move(ticket);
            if(!m_conversion_folder_ticket || !m_conversion_folder_ticket->pending())
                finish_conversion_folder(token,{Status::Failed,{},"フォルダ選択を開始できませんでした。"});
        }
    } catch(const std::exception& error) {finish_conversion_folder(token,{Status::Failed,{},error.what()});}
      catch(...) {finish_conversion_folder(token,{Status::Failed,{},"フォルダ選択を開始できませんでした。"});}
    return true;
}
void YadoSelect::cancel_conversion_folder()
{
    if(!m_conversion_folder_token) return;
    m_conversion_folder_token.reset();m_conversion_folder_ticket.reset();
    m__processing=false;request_dialog_presentation_refresh();
}
void YadoSelect::finish_conversion_folder(const std::shared_ptr<int>& token,
    ::cw::lib::documents::Result result)
{
    using namespace ::cw::lib::documents;
    if(m_conversion_folder_token!=token || m_finished) return;
    struct Release {
        YadoSelect& owner;std::shared_ptr<int> token;
        ~Release(){if(owner.m_conversion_folder_token==token) owner.cancel_conversion_folder();}
    } release{*this,token};
    if(result.status==Status::Cancelled) return;
    try {
        if(result.status==Status::Success && result.documents.size()==1) {
            auto& document=result.documents.front();
            if(document.kind!=LocationKind::NativePath)
                result.error="この変換処理ではプロバイダーURIの宿フォルダを使用できません。ローカルフォルダを選択してください。";
            else if(document.location.empty() || document.location.find('\0')!=String::npos)
                result.error="宿フォルダのパスが不正です。";
            else {
                const auto root=std::filesystem::u8path(m_conversion_root);
                if(std::filesystem::current_path()!=root)
                    throw std::runtime_error("フォルダ選択中に作業場所が変更されました。選択し直してください。");
                auto path=std::filesystem::u8path(document.location);
                document.location=::cw::lib::encoding::path_to_utf8((path.is_absolute()?path:root/path).lexically_normal());
                // 取得したアクセス権と親の入力ロックは、選択画
                // 面だけでなく確認・変換が終わるまで保持する。
                convert_acquired_yado(document);
                return;
            }
        }
    } catch(const std::exception& error) {result.error=error.what();}
      catch(...) {result.error="宿データの変換に失敗しました。";}
    m_conversion_folder_error=result.error.empty()?"宿フォルダを取得できませんでした。":result.error;
    report_conversion_error(m_conversion_folder_error);
}
void YadoSelect::convert_acquired_yado(const ::cw::lib::documents::Document& document)
{
    conv_yado(document.location,false,false,{});
}
void YadoSelect::report_conversion_error(const String& error)
{
    select_play_sound("error");
    (void)::cw::dialog::message::show_common_modal_message("宿データの変換",error,false,true);
}
bool YadoSelect::confirm_yado_conversion(const String& path)
{
    select_play_sound("click");
    const auto name=::cw::lib::encoding::path_to_utf8(std::filesystem::u8path(path).filename());
    return ::cw::dialog::message::show_common_modal_message(select_msg("message","メッセージ"),
        name+" を変換します。\nよろしいですか？",true,false).value_or(false);
}

void YadoSelect::_convert_current()
{
    if(m_finished || m__processing) return;
    if (m_index < 0 || m_index >= static_cast<Integer>(m_list.size()) ||
        m_index >= static_cast<Integer>(m_classic.size()) || !m_classic[static_cast<std::size_t>(m_index)]) return;
    const String path = m_list[static_cast<std::size_t>(m_index)];
    const String shortcut = m_index < static_cast<Integer>(m_isshortcuts.size())
        ? m_isshortcuts[static_cast<std::size_t>(m_index)] : String{};
    if (!confirm_yado_conversion(path)) return;
    // 確認中に入れ子のモーダルが処理された可能性がある。 変換や Classic
    // の元データ移動を始める前に、選択レコードを再検証する。
    if (m_finished || m__processing || m_index < 0 ||
        m_index >= static_cast<Integer>(m_list.size()) ||
        m_list[static_cast<std::size_t>(m_index)] != path ||
        m_index >= static_cast<Integer>(m_classic.size()) ||
        !m_classic[static_cast<std::size_t>(m_index)] ||
        !path_is_directory(path)) return;
    conv_yado(path, true, true, shortcut);
}

void YadoSelect::draw(bool arg_update)
{
    (void)_get_bg();
    // Py は一件表示の宿のスキン履歴を描画時に記録する。 共通表示は
    // 遅延する場合があるため、描画を予約する前にその履歴を更新する。
    (void)_get_draw_bg();
    if (m_index >= 0 && m_index < static_cast<Integer>(m_imgpaths_typed.size())) {
        for (const auto& info : m_imgpaths_typed[static_cast<std::size_t>(m_index)]) {
            if (!info) continue;
            (void)get_ui_backend()->load_select_bitmap(info->m_path, true);
            if (::cw::animatedimage::is_supported_path(info->m_path))
                (void)::cw::animatedimage::acquire(info->m_path, "select.yado", false, std::nullopt);
        }
    }
    select_feedback(this, SelectUiKind::yado, {}, arg_update);
}

void YadoSelect::conv_yado(::cw::lib::String arg_path, bool arg_ok, bool arg_moveconverted, ::cw::lib::String arg_deletepath)
{
    if (arg_path.empty() || m_finished) return;
    SelectProcessingScope processing(*this);
    // Py の os.path.exists は NUL を含む値の ValueError を捕捉し、Environment.wyd
    // がないと通知する。 ネイティブ API が文字列を切り詰める前に拒否する。
    if (arg_path.find('\0') != String::npos) {
        report_conversion_error("CardWirthの宿のディレクトリではありません。");
        return;
    }
    const auto root=std::filesystem::current_path();
    const auto source = std::filesystem::absolute(std::filesystem::u8path(arg_path)).lexically_normal();
    std::error_code error;
    if (!std::filesystem::is_directory(source,error) || !std::filesystem::is_regular_file(source / "Environment.wyd",error)) {
        report_conversion_error("CardWirthの宿のディレクトリではありません。");
        return;
    }
    if (!arg_ok) {
        // フォルダーの選択やドロップだけでは、変換開始への同意とは扱わない。
        if(!confirm_yado_conversion(::cw::lib::encoding::path_to_utf8(source))) return;
    }
    if(m_finished) return;
    if(std::filesystem::current_path()!=root) {
        report_conversion_error("確認中に作業場所が変更されました。変換し直してください。");return;
    }
    const String skin_type = ::cw::g_cwpy && ::cw::g_cwpy->m_setting
        ? ::cw::g_cwpy->m_setting->m_skintype : String("Classic");
    ::cw::binary::cwyado::CWYado converter;
    converter.initialize(::cw::lib::encoding::path_to_utf8(source), "Yado", skin_type);
    if (!converter.is_convertible()) {
        report_conversion_error("CardWirth ver.1.20-1.50の宿しか変換できません。");return;
    }
    if (!::cw::util::create_mutex("Yado")) {select_play_sound("error");return;}
    try {
        String converted;
        if(!select_run_conversion(m_toppanel_window,converter.m_name+"の変換",100,converter,
            [&]{converted=converter.convert();})) {
            report_conversion_error("変換の進捗画面を表示できませんでした。");
            ::cw::util::release_mutex();return;
        }
        if(m_finished) {::cw::util::release_mutex();return;}
        if (!converter.m_errorlog.empty()) {
            auto errors=std::make_shared<::cw::dialog::etc::ErrorLogDialog>();
            errors->initialize(m_toppanel_window,converter.m_errorlog);
            select_run_etc_modal(errors);
        }
        select_play_sound("harvest");
        (void)::cw::dialog::message::show_common_modal_message(
            select_msg("message","メッセージ"),"データの変換が完了しました。",false,false);
        if(m_finished) {::cw::util::release_mutex();return;}
        if (!arg_deletepath.empty()) ::cw::util::remove(arg_deletepath, false);
        else if (arg_moveconverted) {
            const auto destination_dir = std::filesystem::u8path("ConvertedYado");
            std::filesystem::create_directories(destination_dir);
            const String destination = ::cw::binary::util::check_duplicate(
                ::cw::lib::encoding::path_to_utf8(destination_dir / source.filename()));
            ::cw::util::move_path(::cw::lib::encoding::path_to_utf8(source),destination);
        }
        if (::cw::g_cwpy && ::cw::g_cwpy->m_setting)
            ::cw::g_cwpy->m_setting->insert_yadoorder(::cw::lib::encoding::path_to_utf8(std::filesystem::u8path(converted).filename()));
        select_play_sound("page");
        update_list(converted, true);
        ::cw::util::release_mutex();
    } catch (...) {
        ::cw::util::release_mutex();
        throw;
    }
}

void YadoSelect::unconv_yado()
{
    if(m_finished || m__processing) return;
    SelectProcessingScope processing(*this);
    if (m_index < 0 || m_index >= static_cast<Integer>(m_list.size()) ||
        m_index >= static_cast<Integer>(m_classic.size()) || m_classic[static_cast<std::size_t>(m_index)] ||
        !::cw::g_cwpy) return;
    const std::size_t index = static_cast<std::size_t>(m_index);
    const String yadodir = m_list[index];
    const String yadono = index < m_names.size() ? m_names[index] : yadodir;
    select_play_sound("click");
    auto dialog = std::make_shared<::cw::dialog::etc::ConvertYadoDialog>();
    dialog->initialize(m_toppanel_window, yadono);
    select_run_etc_modal(dialog);
    if (!dialog->m_modal_ok || dialog->m_dstpath.empty())
        return;
    auto destination_document=dialog->m_destination_document;
    const String destination = destination_document ? destination_document->location : dialog->m_dstpath;
    const auto engine = dialog->m_targetengine;
    std::error_code directory_error;
    std::filesystem::create_directories(
        std::filesystem::u8path(destination), directory_error);
    if (directory_error) {
        report_conversion_error("変換先フォルダを生成できません。");
        return;
    }
    // プロバイダーを使う出力ではアプリ専用の一時ディレクトリーを使う。
    // その一時パスを、ユーザーの次回のネイティブ出力先として保存しない。
    if (::cw::g_cwpy->m_setting &&
        (!destination_document || !destination_document->finalize))
        ::cw::g_cwpy->m_setting->m_unconvert_targetfolder = destination;
    const String shared_temp = !::cw::g_tempdir_init.empty()
        ? ::cw::g_tempdir_init : value_string(::cw::g_tempdir);
    const auto tempdir = ::cw::lib::encoding::path_to_utf8(
        std::filesystem::u8path(shared_temp) / "Yado" / std::filesystem::u8path(
            ::cw::util::get_yado_relpath(yadodir, "Yado", "Yado")));
    (void)::cw::util::get_yado_writepath(yadodir, yadodir, tempdir);
    auto ydata = std::make_shared<::cw::data::YadoData>();
    ydata->initialize(yadodir, tempdir, false);
    ::cw::binary::cwyado::UnconvCWYado converter;
    converter.initialize(ydata, destination, engine);
    if(!select_run_conversion(m_toppanel_window,yadono+"の逆変換",converter.m_maxnum,converter,
        [&]{converter.convert();})) {
        report_conversion_error("逆変換の進捗画面を表示できませんでした。");return;
    }
    if(m_finished) return;
    if (!converter.m_errorlog.empty()) {
        auto errors = std::make_shared<::cw::dialog::etc::ErrorLogDialog>();
        errors->initialize(m_toppanel_window, converter.m_errorlog);
        select_run_etc_modal(errors);
    }
    const auto completion_label=destination_document && destination_document->finalize
        ? yadono : converter.m_dir;
    const auto show_success=[weak=weak_from_this(),name=completion_label]() {
        if(const auto owner=weak.lock()) {
            owner->m__processing=false;owner->request_dialog_presentation_refresh();
            select_play_sound("harvest");
            (void)::cw::dialog::message::show_common_modal_message(
                select_msg("message","メッセージ"),"データの逆変換が完了しました。\n"+name,false,false);
        }
    };
    if(destination_document && destination_document->finalize) {
        processing.handoff();
        const auto weak=weak_from_this();
        try {
            destination_document->finalize(true,[weak,show_success](bool ok,std::string error) {
                if(ok) {show_success();return;}
                if(const auto owner=weak.lock()) {
                    owner->m__processing=false;owner->request_dialog_presentation_refresh();
                    owner->report_conversion_error(error.empty()?"変換先フォルダへ書き出せませんでした。":error);
                }
            });
        } catch(const std::exception& error) {
            m__processing=false;request_dialog_presentation_refresh();report_conversion_error(error.what());
        } catch(...) {
            m__processing=false;request_dialog_presentation_refresh();report_conversion_error("変換先フォルダへ書き出せませんでした。");
        }
    } else show_success();
}

void YadoSelect::update_list(::cw::lib::String arg_yadodir, bool arg_clear_narrowcondition)
{
    if (arg_clear_narrowcondition) {
        // Py と同様、検索文字列は消去するが、選択した絞り込みカテゴリーは維持する。
        m_narrow = String{};
    }
    const auto lists = get_yadolist();
    m__names_typed = std::get<0>(lists);
    m__list_typed = std::get<1>(lists);
    m__list2_typed = std::get<2>(lists);
    m__skins_typed = std::get<3>(lists);
    m__classic_typed = std::get<4>(lists);
    m__isshortcuts_typed = std::get<5>(lists);
    m__imgpaths_typed = std::get<6>(lists);
    m_names = m__names_typed;
    m_list = m__list_typed;
    m_list2 = m__list2_typed;
    m_skins = m__skins_typed;
    m_classic = m__classic_typed;
    m_isshortcuts = m__isshortcuts_typed;
    m_imgpaths_typed = m__imgpaths_typed;
    m_imgpaths.assign(m_imgpaths_typed.size(), Value{});
    if (!arg_yadodir.empty()) {
        const auto iter = std::find(m_list.begin(), m_list.end(), arg_yadodir);
        if (iter == m_list.end()) throw std::out_of_range("selected inn is absent from the refreshed list");
        m_index = static_cast<Integer>(iter - m_list.begin());
    }
    update_narrowcondition();
    draw(true);
    enable_btn();
}

std::tuple<std::vector<::cw::lib::String>, std::vector<::cw::lib::String>, std::vector<std::vector<::cw::lib::String>>, std::vector<::cw::lib::String>, std::vector<bool>, std::vector<::cw::lib::String>, std::vector<std::vector<std::shared_ptr<::cw::image::ImageInfo>>>> YadoSelect::get_yadolist()
{
    // Py と同様、列挙や Environment.xml の読み取り前に、保留中の宿の書き込みを完了する。
    if (::cw::g_fsync) ::cw::g_fsync->sync();
    std::vector<String> names;
    std::vector<String> yadodirs;
    std::vector<std::vector<String>> advnames;
    std::vector<String> skins;
    std::vector<bool> classic;
    std::vector<String> isshortcuts;
    std::vector<std::vector<std::shared_ptr<::cw::image::ImageInfo>>> imgpaths;
    const String current_skin = ::cw::g_cwpy ? ::cw::g_cwpy->m_skindir.as_string() : String{};
    std::map<String, bool> skin_support;
    std::filesystem::path root = std::filesystem::u8path("Yado");
    if (const auto mounted = ::cardwirth::platform::resolve_mounted_user_path(root))
        root = *mounted;
    const bool mounted_root = ::cardwirth::platform::is_mounted_user_path(root);
    std::error_code error;
    std::vector<std::filesystem::path> entries;
    if (mounted_root) {
        if (!::cardwirth::platform::make_mounted_user_directory(root, true))
            return {names, yadodirs, advnames, skins, classic, isshortcuts, imgpaths};
        if (const auto mounted_entries = ::cardwirth::platform::list_mounted_user_path(root))
            for (const auto& entry : *mounted_entries) entries.push_back(entry.path);
    } else {
        std::filesystem::create_directories(root, error);
        if (error) return {names, yadodirs, advnames, skins, classic, isshortcuts, imgpaths};
        for (std::filesystem::directory_iterator it(root, error), end;
             !error && it != end; it.increment(error)) entries.push_back(it->path());
    }
    for (const auto& entry : entries) {
        if (error) break;
        // 古いミューテックスファイルが宿ディレクトリーより先に列挙される場合がある。
        // その Environment.xml の確認失敗を、後続項目に使い回さない。
        error.clear();
        const String dname = ::cw::lib::encoding::path_to_utf8(entry.filename());
        const auto environment = entry / "Environment.xml";
        std::error_code environment_error;
        const auto mounted_environment = mounted_root
            ? ::cardwirth::platform::stat_mounted_user_path(environment)
            : std::optional<::cardwirth::platform::StorageStat>{};
        if (mounted_root ? (mounted_environment && mounted_environment->is_file())
                         : std::filesystem::is_regular_file(environment, environment_error)) {
            auto property = std::make_shared<::cw::header::GetProperty>();
            try {
                property->initialize(::cw::lib::encoding::path_to_utf8(environment), std::nullopt, std::nullopt);
            } catch (...) {
                report_select_exception(
                    "宿のEnvironment.xmlを読み込めませんでした:\r\n" +
                    ::cw::lib::encoding::path_to_utf8(environment));
                // 一つの宿が破損していても、YadoSelect
                // で残りの宿を表示できるようにする。
                continue;
            }
            const auto name_iter = property->m_properties.find("Name");
            names.push_back(name_iter != property->m_properties.end() && !name_iter->second.empty()
                                ? name_iter->second : dname);
            const auto skin_iter = property->m_properties.find("Skin");
            const String skin_name = skin_iter != property->m_properties.end()
                                         ? skin_iter->second : String("Classic");
            const String skin_path = ::cw::lib::encoding::path_to_utf8(
                std::filesystem::u8path("Data/Skin") / std::filesystem::u8path(skin_name));
            const auto skin_xml = std::filesystem::u8path(skin_path) / "Skin.xml";
            const String skin_key = ::cw::lib::encoding::path_to_utf8(skin_xml);
            auto support = skin_support.find(skin_key);
            if (support == skin_support.end()) {
                std::error_code skin_error;
                const bool mounted_skin = ::cardwirth::platform::is_mounted_user_path(skin_xml);
                const auto skin_stat = mounted_skin
                    ? ::cardwirth::platform::stat_mounted_user_path(skin_xml)
                    : std::optional<::cardwirth::platform::StorageStat>{};
                bool supported = false;
                if (mounted_skin ? (skin_stat && skin_stat->is_file())
                                 : std::filesystem::is_regular_file(skin_xml, skin_error)) {
                    ::cw::header::GetProperty skin_property;
                    skin_property.initialize(skin_key, std::nullopt, std::nullopt);
                    String version = "0";
                    if (const auto attrs = skin_property.m_attrs.find(std::nullopt);
                        attrs != skin_property.m_attrs.end())
                        if (const auto value = attrs->second.find("dataVersion"); value != attrs->second.end())
                            version = value->second;
                    supported = std::apply([&](const auto&... value) {
                        return ((version == value) || ...);
                    }, ::cw::g_SUPPORTED_SKIN);
                }
                support = skin_support.emplace(skin_key, supported).first;
            }
            skins.push_back(support->second ? skin_path : current_skin);
            yadodirs.push_back(::cw::lib::encoding::path_to_utf8(entry));
            classic.push_back(false);
            isshortcuts.emplace_back();
            imgpaths.push_back(::cw::image::get_imageinfos_p(property, false));
            ::cw::yadodb::YadoDB database;
            database.initialize(::cw::lib::encoding::path_to_utf8(entry));
            std::vector<String> members = database.get_standbynames();
            // Python の YadoSelect に合わせ、空の
            // キャッシュと、冒険者ごとの行はあるが名前のない旧形式の
            // 両方を修復する。 members.empty() だ
            // けでは後者が空ラベルのまま残るため、名前も確認する。
            const bool has_blank_name = std::any_of(members.begin(), members.end(),
                [](const String& value) { return value.empty(); });
            if (members.empty() || has_blank_name) {
                database.update(false, true, false, std::nullopt, std::nullopt, std::nullopt,
                                false, false);
                members = database.get_standbynames();
            }
            database.close();
            advnames.push_back(std::move(members));
            continue;
        }
        if (mounted_root) continue;
        const String entry_path = ::cw::lib::encoding::path_to_utf8(entry);
        const String linked_path = ::cw::util::get_linktarget(entry_path);
        const bool shortcut = linked_path != entry_path;
        const std::filesystem::path target = std::filesystem::u8path(linked_path);
        const auto wyd = target / "Environment.wyd";
        std::error_code wyd_error;
        if (!std::filesystem::is_regular_file(wyd, wyd_error)) continue;
        names.push_back(::cw::lib::encoding::path_to_utf8(target.filename()));
        skins.push_back(current_skin);
        yadodirs.push_back(::cw::lib::encoding::path_to_utf8(target));
        classic.push_back(true);
        isshortcuts.push_back(shortcut ? ::cw::lib::encoding::path_to_utf8(entry) : String{});
        imgpaths.emplace_back();
        std::vector<String> members;
        try {
            auto environment_file = std::make_shared<::cw::binary::cwfile::CWFile>(
                ::cw::lib::encoding::path_to_utf8(wyd), "rb");
            auto environment_data = std::make_shared<::cw::binary::environment::Environment>();
            environment_data->initialize({}, environment_file, true, true);
            const auto dataversion = environment_data->m_dataversion_int;
            environment_file->close();
            if (dataversion >= 13) {
                members.push_back("*読込失敗*");
            } else {
                for (const auto& file_entry : std::filesystem::directory_iterator(target, error)) {
                    if (error || !file_entry.is_regular_file()) continue;
                    const String filename = ::cw::lib::encoding::path_to_utf8(file_entry.path().filename());
                    const String extension = lower_ascii(
                        ::cw::lib::encoding::path_to_utf8(file_entry.path().extension()));
                    if (extension == ".wch") {
                        auto file = std::make_shared<::cw::binary::cwfile::CWFile>(
                            ::cw::lib::encoding::path_to_utf8(file_entry.path()), "rb");
                        if (dataversion <= 8) { (void)file->string(); members.push_back(file->string()); }
                        else {
                            auto adventurer = std::make_shared<::cw::binary::adventurer::Adventurer>();
                            adventurer->initialize(nullptr, file, true, true, false);
                            members.push_back(adventurer->m_name);
                        }
                        file->close();
                    } else if (extension == ".wpl") {
                        auto file = std::make_shared<::cw::binary::cwfile::CWFile>(
                            ::cw::lib::encoding::path_to_utf8(file_entry.path()), "rb");
                        auto party = std::make_shared<::cw::binary::party::Party>();
                        party->initialize({}, file, true, dataversion);
                        members.insert(members.end(), party->m_memberslist.begin(), party->m_memberslist.end());
                        file->close();
                    }
                    (void)filename;
                }
            }
        } catch (...) {
            ::cardwirth::debug::log_current_stacktrace("Classic yado member-list load failed");
            // Py と同様、Classic のファイルで例外が出ても、それまでに読んだ
            // 名前は保持する。 明示的に未対応のバージョンの場合だけ失敗の印を付ける。
        }
        advnames.push_back(std::move(members));
    }
#if defined(CARDWIRTH_LOG_ENABLED)
    std::size_t member_count = 0;
    for (const auto& members : advnames) member_count += members.size();
    ::cardwirth::debug::log("YadoSelect candidates: yado_count=" + std::to_string(names.size()) +
        " member_entries=" + std::to_string(member_count));
    for (std::size_t i = 0; i < names.size() && i < advnames.size(); ++i) {
        String members;
        for (const auto& member : advnames[i]) {
            if (!members.empty()) members += ",";
            members += member.empty() ? "<empty>" : member;
        }
        const String yado_path = i < yadodirs.size() ? yadodirs[i] : "<missing>";
        ::cardwirth::debug::log("YadoSelect yado[" + std::to_string(i) + "] name=" + names[i] +
            " path=" + yado_path + " members=" + (members.empty() ? "<none>" : members));
    }
#endif
    return {std::move(names), std::move(yadodirs), std::move(advnames), std::move(skins),
            std::move(classic), std::move(isshortcuts), std::move(imgpaths)};
}

void _YadoObj::initialize(::cw::lib::String arg_name, ::cw::lib::String arg_yadodir, std::vector<::cw::lib::String> arg_advnames, ::cw::lib::String arg_skin, bool arg_classic, ::cw::lib::String arg_isshortcut, std::vector<std::shared_ptr<::cw::image::ImageInfo>> arg_imgpaths)
{
    m_name = arg_name;
    m_yadodir = arg_yadodir;
    std::vector<Value> names;
    names.reserve(arg_advnames.size());
    for (const auto& value : arg_advnames) names.emplace_back(value);
    m_advnames = make_sequence(names);
    m_skin = arg_skin;
    m_classic = arg_classic;
    m_isshortcut = arg_isshortcut;
    m_imgpaths_typed = std::move(arg_imgpaths);
    m_imgpaths = Value{}; // ImageInfo のポインターは型付きブリッジのフィールドに保持する。
    const String basename = !arg_isshortcut.empty()
        ? ::cw::lib::encoding::path_to_utf8(std::filesystem::u8path(arg_isshortcut).filename())
        : ::cw::lib::encoding::path_to_utf8(std::filesystem::u8path(arg_yadodir).filename());
    m_yadodirname = basename;
    Integer configured_order = static_cast<Integer>(0x7fffffff);
    if (const auto setting = ::cw::g_cwpy ? ::cw::g_cwpy->m_setting : nullptr) {
        if (const auto order = setting->m_yado_order.find(m_yadodirname);
            order != setting->m_yado_order.end()) configured_order = order->second;
    }
    m_order = configured_order;
}

// Python は Select を直接初期化する。ネイティブの Album
// は PlayerSelect の顔画像・キャッシュ初期化を一度再利用してか
// ら、取得元をアルバムへ置き換える。 一時的に待機一覧の並べ替え・準備が行わ
// れ得るが、バックエンドへの接続を抑止し、最終のアルバム表示だけを公開する。
void Album::initialize(std::shared_ptr<::cw::lib::Window> arg_parent)
{
    // AlbumはPlayerSelectの描画・画像キャッシュを再利用するが、Python版と同じく
    // Select.__init__相当で追加/新規ボタンを持たない読み取り専用一覧にする。
    // Python Album.__init__ は PlayerSelect.__init__ を呼ばないため、ネイティブUIも
    // 一度だけ album として生成する。
    PlayerSelect::initialize(arg_parent, false);
    auto& base = static_cast<PlayerSelect&>(*this);
    base.m_isalbum = true;
    m_isalbum = true;
    base.m_views = 1;
    m_views = 1;
    base.m_narrow_type = Value{};
    m_narrow_type = Value{};
    base.m_sort = Value{};
    m_sort = Value{};
    base.m_index = 0;
    m_index = 0;
    base.m__bg = Value{};
    m__bg = Value{};
    base.m__player_image_cache.clear();
    m__player_image_cache.clear();
    base.m_source_list.clear();
    if (::cw::g_cwpy && ::cw::g_cwpy->m_ydata && *::cw::g_cwpy->m_ydata)
        base.m_source_list = (*::cw::g_cwpy->m_ydata)->m_album;

    m_toppanel = base.m_toppanel;
    m_infobtn = Value(std::static_pointer_cast<::cw::lib::Object>(make_window("album_info")));
    m_delbtn = Value(std::static_pointer_cast<::cw::lib::Object>(make_window("album_delete")));
    m_closebtn = Value(std::static_pointer_cast<::cw::lib::Object>(make_window("album_close", -1)));
    base.m_buttonlist.clear();
    base.m_buttonlist = {
        object_value<::cw::lib::Window>(m_infobtn),
        object_value<::cw::lib::Window>(m_delbtn),
        object_value<::cw::lib::Window>(m_closebtn),
        base.m_left2btn, base.m_leftbtn, base.m_rightbtn, base.m_right2btn
    };
    update_narrowcondition();
    enable_btn();
    if (const auto backend = get_ui_backend()) backend->bind_select_dialog(this, SelectUiKind::album);
#if defined(CARDWIRTH_LOG_ENABLED)
    ::cardwirth::debug::log("Album open: source=" + std::to_string(base.m_source_list.size()) +
                            " typed=" + std::to_string(base.m_typed_list.size()));
#endif
    base.draw(true);
}

bool Album::can_clickcenter()
{
    return false;
}

void Album::OnMouseWheel(std::shared_ptr<::cw::lib::Window> arg_event)
{
    auto& base = static_cast<PlayerSelect&>(*this);
    if (base.m_finished || base.m__processing || base.m_typed_list.size() <= 1) return;
    if (event_wheel(arg_event) == 0) return;
    if (event_wheel(arg_event) > 0) base.OnClickLeftBtn(nullptr);
    else base.OnClickRightBtn(nullptr);
    m_index = base.m_index;
}

void Album::_add_topsizer()
{
    // Python版は追加絞込/ソートUIを置かない。nativeレイアウト側でも空Sizerを維持する。
}

void Album::update_narrowcondition()
{
    auto& base = static_cast<PlayerSelect&>(*this);
    std::shared_ptr<::cw::data::YadoData> ydata;
    if (::cw::g_cwpy && ::cw::g_cwpy->m_ydata && *::cw::g_cwpy->m_ydata)
        ydata = *::cw::g_cwpy->m_ydata;
    if (ydata) base.m_source_list = ydata->m_album;
    std::shared_ptr<::cw::header::AdventurerHeader> selected;
    if (base.m_index >= 0 && static_cast<std::size_t>(base.m_index) < base.m_typed_list.size())
        selected = base.m_typed_list[static_cast<std::size_t>(base.m_index)];
    base.m_typed_list = base.m_source_list;
    base.m_list.clear();
    for (const auto& header : base.m_typed_list) if (header)
        base.m_list.emplace_back(Value(std::static_pointer_cast<::cw::lib::Object>(header)));
    m_list = make_sequence(base.m_list);
    if (selected) {
        const auto iter = std::find(base.m_typed_list.begin(), base.m_typed_list.end(), selected);
        if (iter != base.m_typed_list.end()) base.m_index = static_cast<Integer>(iter - base.m_typed_list.begin());
    }
    if (base.m_typed_list.empty()) base.m_index = 0;
    else base.m_index = wrap_index(base.m_index, static_cast<Integer>(base.m_typed_list.size()));
    m_index = base.m_index;
}

void Album::OnClickDelBtn(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    auto& base = static_cast<PlayerSelect&>(*this);
    if (base.m_finished || base.m__processing) return;
    const auto selected = base.get_selected();
    if (!selected || !*selected) return;
    // Album は PlayerSelect の保護付き共通モーダル処理を共有し、二つ目の UI は作らない。
    run_adventurer_change(*selected, AdventurerChange::DeleteAlbum,
        select_msg_format("confirm_delete_character_in_album",
                          "%sを抹消します。\nよろしいですか？", (*selected)->m_name));
}

bool Album::commit_adventurer_change(
    const std::shared_ptr<::cw::header::AdventurerHeader>& header, AdventurerChange change)
{
    if (change != AdventurerChange::DeleteAlbum || !header || !::cw::g_cwpy ||
        !::cw::g_cwpy->m_ydata || !*::cw::g_cwpy->m_ydata) return false;
    const auto ydata = *::cw::g_cwpy->m_ydata;
    auto& album = ydata->m_album;
    // 確認の後に検索し直す。保存した vector のイテレーターは、入れ子のモーダルをまたいで使えない。
    if (std::find(album.begin(), album.end(), header) == album.end()) return false;
    ::cw::g_cwpy->remove_xml(Value(std::static_pointer_cast<::cw::lib::Object>(header)));
    album.erase(std::remove(album.begin(), album.end(), header), album.end());
    // Python の Album.OnClickDelBtn と同様、XML・素材
    // の削除を予約し、アルバムヘッダーを除くだけとする。 YadoData.changed()
    // は呼ばない。追加すると後の保存・終了確認の条件が変わってしまう。
    static_cast<PlayerSelect&>(*this).m__player_image_cache.erase(value_string(header->m_fpath));
    update_narrowcondition();
    return true;
}

void Album::enable_btn()
{
    auto& base = static_cast<PlayerSelect&>(*this);
    const bool has_list = !base.m_typed_list.empty();
    set_enabled(object_value<::cw::lib::Window>(m_infobtn), has_list);
    set_enabled(object_value<::cw::lib::Window>(m_delbtn), has_list);
    set_enabled(object_value<::cw::lib::Window>(m_closebtn), true);
    const bool can_page = base.m_typed_list.size() > 1;
    set_enabled(base.m_leftbtn, can_page); set_enabled(base.m_left2btn, can_page);
    set_enabled(base.m_rightbtn, can_page); set_enabled(base.m_right2btn, can_page);
}

void Album::OnSelect(std::shared_ptr<::cw::lib::Event> arg_event)
{
    (void)arg_event;
    // Python版はAlbum上で選択確定を発生させない（情報/削除のみ許可）。
}

::cw::lib::Value g__T{};
::cw::lib::Value g__okid{};

void Select__bind_empty(std::shared_ptr<::cw::lib::Window> arg_event)
{
    (void)arg_event;
    // Python の空のコールバックと同様、モデルの状態は変更しない。 Windows の WndProc
    // の未使用通知も同じ扱いとし、SDL バックエンドは必要なイベントだけを受け取る。
}

void Select__bind_recurse(std::shared_ptr<::cw::lib::Control> arg_ctrl)
{
    (void)arg_ctrl;
    // Windows の子コントロールの通知は親 WndProc の WM_COMMAND/WM_NOTIFY に
    // 集約されるため、 wx の再帰 Bind を個別に複製しない。SDL バックエンドも各 On* を直接呼ぶ。
}

void Select_OnNarrowCondition_func()
{
    if (narrow_callback) narrow_callback();
}

bool YadoSelect_update_narrowcondition_has_advname()
{
    if (!narrow_context.yado) return false;
    for (const auto& advname : sequence_strings(narrow_context.yado->m_advnames)) {
        if (lower_ascii(advname).find(narrow_context.narrow) != String::npos) return true;
    }
    return false;
}

std::shared_ptr<::cw::lib::Bitmap> YadoSelect_draw_get_playingbmp()
{
    const String base = ::cw::g_cwpy && ::cw::g_cwpy->m_rsrc ? ::cw::g_cwpy->m_rsrc->m_skindir : String{};
    const String path = base.empty() ? "Dialog/PLAYING" :
        ::cw::lib::encoding::path_to_utf8(std::filesystem::u8path(base) / std::filesystem::u8path("Dialog/PLAYING"));
    return get_ui_backend()->load_select_bitmap(path, true);
}

void PartySelect_OnDebugMode_func(std::shared_ptr<::cw::dialog::select::PartySelect> arg_self)
{
    if (arg_self) arg_self->OnDebugMode(nullptr);
}

void PartySelect_OnDebugMode_func_func(std::shared_ptr<::cw::dialog::select::PartySelect> arg_self)
{
    if (arg_self) arg_self->update_debug();
}

bool PartySelect_update_narrowcondition_has_membername()
{
    if (!narrow_context.party) return false;
    for (const auto& value : narrow_context.party->get_membernames()) {
        if (lower_ascii(value).find(narrow_context.narrow) != String::npos) return true;
    }
    return false;
}

bool PartySelect_update_narrowcondition_has_memberdesc()
{
    if (!narrow_context.party) return false;
    for (const auto& value : narrow_context.party->get_memberdescs()) {
        if (lower_ascii(value).find(narrow_context.narrow) != String::npos) return true;
    }
    return false;
}

bool PartySelect_update_narrowcondition_has_memberhistory()
{
    if (!narrow_context.party) return false;
    for (const auto& coupons : narrow_context.party->get_membercoupons()) {
        for (const auto& coupon : coupons) {
            if (coupon.empty()) continue;
            if (!narrow_context.debug &&
                (coupon.rfind("＿", 0) == 0 || coupon.rfind("＠", 0) == 0)) continue;
            if (narrow_context.debug && coupon.rfind("＿", 0) == 0 &&
                narrow_context.attrs.count(coupon.substr(std::string("＿").size()))) continue;
            if (lower_ascii(coupon).find(narrow_context.narrow) != String::npos) return true;
        }
    }
    return false;
}

bool PartySelect_update_narrowcondition_has_memberfeatures()
{
    if (!narrow_context.party) return false;
    for (const auto& coupons : narrow_context.party->get_membercoupons()) {
        for (const auto& coupon : coupons) {
            if (!coupon.empty() && coupon.rfind("＿", 0) == 0 &&
                narrow_context.attrs.count(coupon.substr(std::string("＿").size())) &&
                lower_ascii(coupon.substr(std::string("＿").size())).find(narrow_context.narrow) != String::npos)
                return true;
        }
    }
    return false;
}

// Python の min(*levels)/max(*levels) は、一人のパ
// ーティーでは単一の整数を反復可能な値として扱い例外になる。 その不具合を避けるため
// 、vector 自体から範囲を計算し、一人でもレベル検索に一致できるようにする。
bool PartySelect_update_narrowcondition_has_memberlevel()
{
    if (!narrow_context.party || !narrow_context.int_valid) return false;
    const auto levels = narrow_context.party->get_memberlevels();
    if (levels.empty()) return false;
    const auto bounds = std::minmax_element(levels.begin(), levels.end());
    return *bounds.first <= narrow_context.int_value && narrow_context.int_value <= *bounds.second;
}

void PartySelect_OnClickEditBtn_redrawfunc()
{
    if (party_callback_context) party_callback_context->draw(true);
}

void PartySelect_OnClickEditBtn_redrawfunc_func()
{
    if (party_callback_context) {
        party_callback_context->update_narrowcondition();
        party_callback_context->draw(true);
    }
}

std::tuple<std::vector<std::shared_ptr<::cw::lib::Bitmap>>, std::vector<std::shared_ptr<::cw::lib::Bitmap>>, std::vector<std::shared_ptr<::cw::lib::Bitmap>>, std::optional<std::shared_ptr<::cw::header::ScenarioHeader>>, std::vector<std::shared_ptr<::cw::image::ImageInfo>>> PartySelect_draw_get_image(std::shared_ptr<::cw::header::PartyHeader> arg_header)
{
    std::vector<std::shared_ptr<::cw::lib::Bitmap>> normal;
    std::vector<std::shared_ptr<::cw::lib::Bitmap>> depthkey;
    std::vector<std::shared_ptr<::cw::lib::Bitmap>> playing;
    std::vector<std::shared_ptr<::cw::image::ImageInfo>> infos;
    std::optional<std::shared_ptr<::cw::header::ScenarioHeader>> scenario;
    if (!arg_header) return {normal, depthkey, playing, scenario, infos};
    scenario = arg_header->get_sceheader();
    const auto paths = arg_header->get_memberpaths(::cw::g_cwpy ? std::optional<String>(::cw::g_cwpy->m_yadodir) : std::nullopt);
    // Py の張り紙サムネイルと同様、先頭のメンバーだけを使う。
    if (!paths.empty()) {
        const auto& path = paths.front();
        std::shared_ptr<::cw::header::AdventurerHeader> member;
        if (::cw::g_cwpy && ::cw::g_cwpy->m_ydata && *::cw::g_cwpy->m_ydata)
            member = (*::cw::g_cwpy->m_ydata)->create_advheader(path, false, std::nullopt);
        if (!member) return {normal, depthkey, playing, scenario, infos};
        for (const auto& info : member->get_imgpaths()) {
            if (!info) continue;
            const auto bitmap = get_ui_backend()->load_select_bitmap(info->m_path, true);
            normal.push_back(bitmap);
            depthkey.push_back(bitmap ? ::cw::util::copy_wxbmp(bitmap, true) : nullptr);
            if (::cw::animatedimage::is_supported_path(info->m_path)) {
                const auto [frame, frame_id] = ::cw::animatedimage::get_wxbmp(info->m_path);
                (void)frame_id;
                playing.push_back(frame
                    ? ::cw::util::conform_animated_wxbmp_frame(*frame, bitmap)
                    : bitmap);
            } else {
                playing.push_back(bitmap);
            }
            infos.push_back(info);
        }
    }
    return {normal, depthkey, playing, scenario, infos};
}

void PlayerSelect_OnDebugMode_func(std::shared_ptr<::cw::dialog::select::PlayerSelect> arg_self)
{
    if (arg_self) arg_self->OnDebugMode(nullptr);
}

void PlayerSelect_OnDebugMode_func_func(std::shared_ptr<::cw::dialog::select::PlayerSelect> arg_self)
{
    if (arg_self) arg_self->update_debug();
}

bool PlayerSelect_update_narrowcondition_has_history()
{
    if (!narrow_context.player) return false;
    for (const auto& coupon_value : narrow_context.player->m_history) {
        const String coupon = value_string(coupon_value);
        if (coupon.empty()) continue;
        if (!narrow_context.debug && (coupon.rfind("＿", 0) == 0 || coupon.rfind("＠", 0) == 0)) continue;
        if (narrow_context.debug && coupon.rfind("＿", 0) == 0 &&
            narrow_context.attrs.count(coupon.substr(std::string("＿").size()))) continue;
        if (lower_ascii(coupon).find(narrow_context.narrow) != String::npos) return true;
    }
    return false;
}

bool PlayerSelect_update_narrowcondition_has_features()
{
    if (!narrow_context.player) return false;
    for (const auto& coupon_value : narrow_context.player->m_history) {
        const String coupon = value_string(coupon_value);
        const String feature = coupon.rfind("＿", 0) == 0
            ? coupon.substr(std::string("＿").size()) : String{};
        if (!feature.empty() && narrow_context.attrs.count(feature) &&
            lower_ascii(feature).find(narrow_context.narrow) != String::npos) return true;
    }
    return false;
}

void PlayerSelect_OnClickAddBtn_func(std::shared_ptr<::cw::dialog::select::PlayerSelect> arg_self, std::shared_ptr<::cw::header::AdventurerHeader> arg_header, ::cw::lib::Integer arg_index)
{
    (void)arg_index;
    if (!arg_self || !arg_header) return;
    if (PlayerSelect::_add(arg_header)) {
        arg_self->m__processing = false;
        arg_self->update_standbys(nullptr);
    } else {
        arg_self->m__processing = false;
    }
}

void PlayerSelect_OnClickAddBtn_func_func(std::shared_ptr<::cw::dialog::select::PlayerSelect> arg_self)
{
    if (!arg_self) return;
    arg_self->m__processing = false;
    arg_self->update_standbys(nullptr);
}

void PlayerSelect_OnClickAddBtn_func_func_2(std::shared_ptr<::cw::dialog::select::PlayerSelect> arg_self)
{
    if (arg_self) arg_self->m__processing = false;
}

void PlayerSelect_random_team_func(std::shared_ptr<::cw::dialog::select::PartySelect> arg_panel)
{
    if (!arg_panel || !::cw::g_cwpy ||
        !::cw::g_cwpy->m_ydata || !*::cw::g_cwpy->m_ydata) return;
    // 生成済みのゲームワーカー入口でも同じアルゴリズムを使う。 同順位の
    // 扱いや UI スレッドへの副作用が異なる二つ目の実装は作らない。
    const auto frame=object_value<::cw::frame::Frame>(::cw::g_cwpy->m_frame);
    if(!frame) return;
    String error;
    try {(void)PlayerSelect::_random_team();}
    catch(const std::exception& failure) {error=failure.what();}
    catch(...) {error="ランダム編成に失敗しました。";}
    const std::weak_ptr<PartySelect> weak_panel=arg_panel;
    const std::weak_ptr<::cw::frame::Frame> weak_frame=frame;
    frame->post_func([weak_panel,weak_frame,error](const auto&,const auto&) {
        if(const auto panel=weak_panel.lock()) {
            panel->m_processing=false;
            if(!panel->m_finished) {
                panel->update_narrowcondition();panel->enable_btn();panel->draw(true);
                if(!error.empty()) (void)::cw::dialog::message::show_common_modal_message(
                    weak_frame.lock(),"CWPy内部エラー",error,false,true);
            }
        }
        return Value{};
    });
}

void PlayerSelect_Pocket_random_team_func___init__(::cw::lib::Value arg_self, std::shared_ptr<::cw::header::AdventurerHeader> arg_header, ::cw::lib::Integer arg_point)
{
    if (auto pocket = object_value<RandomPocket>(arg_self)) {
        pocket->header = std::move(arg_header);
        pocket->point = arg_point;
    }
}

void PlayerSelect_random_team_func_func(std::shared_ptr<::cw::dialog::select::PartySelect> arg_panel)
{
    if (!arg_panel) return;
    arg_panel->m_processing = false;
    arg_panel->update_narrowcondition();
    if (arg_panel->m_typed_list.empty()) arg_panel->m_index = 0;
    else arg_panel->m_index = wrap_index(arg_panel->m_index, static_cast<Integer>(arg_panel->m_typed_list.size()));
    arg_panel->enable_btn();
}

::cw::lib::String PlayerSelect_create_randomadventurer_random_name()
{
    return ::cw::dialog::create::get_randomname({});
}

void PlayerSelect_update_character_func()
{
    if (player_callback_context) player_callback_context->update_character();
}

bool change_combo(std::shared_ptr<::cw::lib::Window> arg_combo, std::shared_ptr<::cw::lib::Window> arg_event)
{
    const auto combo=std::dynamic_pointer_cast<ComboChoiceControl>(arg_combo);
    const auto event=std::dynamic_pointer_cast<ComboWheelEvent>(arg_event);
    if(!combo || !event || !combo->visible) return false;
    const auto& rect=combo->bounds;
    if(!(event->pointer_x>=rect.x && event->pointer_y>=rect.y &&
        event->pointer_x<static_cast<::cw::lib::Real>(rect.x)+rect.width &&
        event->pointer_y<static_cast<::cw::lib::Real>(rect.y)+rect.height)) return false;
    // Py と同様、GetCount()・GetSelection()
    // ・ポインター位置を使い、選択肢が一つで選択が変わらなくても選択イ
    // ベントを一度送る。 ALL の固定件数表示は配置だけの方針であり
    // 、ここでは具体的な表示行数は定義しない。両モードでデータ全件数
    // と通知を維持する。 三で剰余を取る旧不具合や Editor 固
    // 有の行寸法を、この Engine の表示領域の定義に使わない。
    const auto count=static_cast<Integer>(combo->choices.size());
    const auto current=combo->selection;
    // 空のネイティブ選択欄には有効な位置がない。Py の下方向分岐は
    // Select(0) を試み、wx が診断を出す場合があるが、 こ
    // の共通データアダプターには wx の添字検査機能がない。存在しな
    // い先頭項目を公開せず、未選択の -1 を維持してその状態を通知す
    // る。 空コントロールの扱いは ALL の表示方針とは独立させる。
    const auto next=count==0 ? Integer{-1} : event->rotation>0
        ? (current<=0?count-1:current-1)
        : (current>=count-1?0:current+1);
    combo->selection=next;
    if(combo->on_selection_changed) combo->on_selection_changed(next);
    return true;
}

void main()
{
    // Python版はモジュールエントリであり、GUI起動はアプリケーション本体が担う。
}

::cw::lib::String select_msg(const ::cw::lib::String& key,
                             const ::cw::lib::String& fallback)
{
    if (::cw::g_cwpy && ::cw::g_cwpy->m_setting && ::cw::g_cwpy->m_setting->m_msgs)
        return ::cw::g_cwpy->m_setting->m_msgs->get(key, fallback);
    return fallback;
}

}

