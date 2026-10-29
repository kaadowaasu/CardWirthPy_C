#pragma once

// cw/util.pyのパス・画像・音声の補助処理の移植。wx/pygame
// の呼び出しは util.cppの小さなアダプターを通し、ネイティブ
// UIがなくても 共通の状態管理とファイル操作を使えるようにする。
#include "cw/lib/exception_origin.hpp"
#include "cw/lib/compat.hpp"
#include "cw/fwd.hpp"

#include <atomic>
#include <condition_variable>
#include <exception>
#include <mutex>
#include <type_traits>

namespace cw::util {
void set_sound_suspended(bool suspended);
// ミキサーのコールバックではなく、Engineスレッドで呼ぶ。
void collect_finished_audio_fades(bool force = false);
// Pythonでwx.DCを受け取る描画API向けの、共
// 通の画像保持先。 ネイティブ・SDL側でビットマップを
// 一度接続し、各描画操作が同じRGBA画像を更新する。
struct DeviceContextState {
    std::shared_ptr<::cw::lib::Bitmap> target{};
    ::cw::lib::Color foreground{0, 0, 0, 255};
    ::cw::lib::Color background{255, 255, 255, 255};
};

using AcceleratorWidgetFilter=std::function<bool(const std::shared_ptr<::cw::lib::Window>&)>;

struct WindowState {
    ::cw::lib::Color background{255, 255, 255, 255};
    std::shared_ptr<::cw::lib::Bitmap> target{};
    std::weak_ptr<::cw::lib::Window> parent{};
    std::vector<std::weak_ptr<::cw::lib::Window>> children{};
    // メインFrameの識別子にだけ設定し、入れ子のパネルの問い合わせでは再帰しない。
    std::weak_ptr<::cw::lib::Window> debugger{};
    bool main_frame{};
    std::vector<std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer>> accelerators{};
    std::vector<std::tuple<::cw::lib::Integer,::cw::lib::Integer,::cw::lib::Integer>> accelerator_source{};
    std::vector<AcceleratorWidgetFilter> accelerator_filters{};
    std::vector<::cw::lib::String> choices{};
    bool modal{};
};

class CallbackHolder : public ::cw::lib::Object {
public:
    ::cw::lib::Callback callback{};
    explicit CallbackHolder(::cw::lib::Callback value = {}) : callback(std::move(value)) {}
    ::cw::lib::Value invoke(const ::cw::lib::Arguments& args = {},
                            const ::cw::lib::KeywordArguments& kwargs = {}) const {
        return callback ? callback(args, kwargs) : ::cw::lib::Value{};
    }
};

// URLを開く規則を共通化し、ホストへの接続先だけを注入可能にする。 製品は
// open_url()を使い、サービスを渡す版では契約テストが外部ブラウザー
// と モーダルの接続先を制御しつつ、同じFrameの配送経路を確認できる。
struct OpenUrlServices {
    std::function<void(const std::shared_ptr<::cw::lib::Window>&,
                       const ::cw::lib::String&)> launch{};
    // URL起動が失敗した後、所有するFrame、元の要求の
    // 親、 初期化済みのエラーダイアログモデルを受け取る。
    std::function<void(const std::shared_ptr<::cw::frame::Frame>&,
                       const std::shared_ptr<::cw::lib::Window>&,
                       const std::shared_ptr<::cw::lib::Window>&)> present_error{};
};

void bind_device_context(std::shared_ptr<::cw::lib::DeviceContext> dc,
                         std::shared_ptr<::cw::lib::Bitmap> target);
std::shared_ptr<::cw::lib::Bitmap> get_device_context_target(
    const std::shared_ptr<::cw::lib::DeviceContext>& dc);
void set_window_state(std::shared_ptr<::cw::lib::Window> window, WindowState state);
std::optional<WindowState> get_window_state(const std::shared_ptr<::cw::lib::Window>& window);

class Alt : public ::cw::lib::Object {
public:
    Alt() = default;
    virtual ~Alt() = default;
    inline static std::tuple<::cw::lib::String> s_py_slots{};
    ::cw::lib::Value m_branches{};
    void initialize(::cw::lib::Value arg_branches);
};

class Anchor : public ::cw::lib::Object {
public:
    Anchor() = default;
    virtual ~Anchor() = default;
    inline static std::tuple<::cw::lib::String> s_py_slots{};
    ::cw::lib::Value m_kind{};
    void initialize(::cw::lib::String arg_kind);
};



class CWPyRichTextCtrl : public ::cw::lib::Control, public std::enable_shared_from_this<CWPyRichTextCtrl> {
public:
    CWPyRichTextCtrl() = default;
    virtual ~CWPyRichTextCtrl() = default;
    inline static std::optional<std::vector<std::tuple<::cw::lib::String, ::cw::lib::String, ::cw::lib::Integer>>> s__search_engines{};
    ::cw::lib::Value m_mi{};
    ::cw::lib::Value m_mi_copy{};
    ::cw::lib::Value m_mi_selectall{};
    ::cw::lib::Value m_parent{};
    ::cw::lib::Value m_popup_menu{};
    std::vector<::cw::lib::Value> m_search_engines{};
    ::cw::lib::Value m_url{};
    ::cw::lib::String m_text_typed{};
    // ネイティブのキャッシュ情報。表示側は共通のリッチテキスト結果と
    // 末尾改行の追加フラグを読み、デコード・正規化を二重に行わない。
    bool m_newline_appended_typed{false};
    std::vector<std::tuple<::cw::lib::String, bool>> m_url_segments_typed{};
    ::cw::lib::Integer m_selection_start_typed{};
    ::cw::lib::Integer m_selection_end_typed{};
    ::cw::lib::Integer m_scroll_line_typed{};
    void initialize(std::shared_ptr<::cw::lib::Panel> arg_parent, ::cw::lib::Integer arg_wid, ::cw::lib::String arg_text, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_size, ::cw::lib::Integer arg_style, bool arg_searchmenu);
    void set_text(::cw::lib::Bytes arg_value, bool arg_linkurl);
    ::cw::lib::String selected_text() const;
    void OnMouseWheel(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnMotion(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnContextMenu(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnCopy(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnSelectAll(std::shared_ptr<::cw::lib::Window> arg_event);
    void go_url(::cw::lib::String arg_url);
    // 契約テスト用の接続箇所。ブラウザーとモーダルの接続先を注入
    // しつつ、 製品と同じ親の選択とFrameへの配送を使う。
    void go_url_with_services(::cw::lib::String arg_url, OpenUrlServices arg_services);
    void OnURL(std::shared_ptr<::cw::lib::Window> arg_event);
};

class CWPyRichTextCtrl_SearchEngine : public ::cw::lib::Object {
public:
    CWPyRichTextCtrl_SearchEngine() = default;
    virtual ~CWPyRichTextCtrl_SearchEngine() = default;
    std::weak_ptr<::cw::util::CWPyRichTextCtrl> m_parent_typed{};
    ::cw::lib::String m_url_typed{};
    ::cw::lib::String m_name_typed{};
    ::cw::lib::Integer m_menuid_typed{};
    void initialize(std::shared_ptr<::cw::util::CWPyRichTextCtrl> parent,
                    ::cw::lib::String url, ::cw::lib::String name,
                    ::cw::lib::Integer menuid);
    ::cw::lib::String format_url(const ::cw::lib::String& selected) const;
    void OnSearch(std::shared_ptr<::cw::lib::Window> event);
};

class CWPyStaticBitmap : public ::cw::lib::Object {
public:
    CWPyStaticBitmap() = default;
    virtual ~CWPyStaticBitmap() = default;
    ::cw::lib::Callback m__ss{};
    ::cw::lib::Value m_bmps{};
    ::cw::lib::Value m_bmps_bmpdepthkey{};
    ::cw::lib::Value m_infos{};
    std::vector<std::shared_ptr<::cw::lib::Bitmap>> m_bmps_typed{};
    std::vector<std::shared_ptr<::cw::lib::Bitmap>> m_bmps_bmpdepthkey_typed{};
    std::optional<std::tuple<::cw::lib::Integer, ::cw::lib::Integer>> m_size_typed{};
    std::vector<std::shared_ptr<::cw::image::ImageInfo>> m_infos_typed{};
    std::shared_ptr<::cw::lib::Surface> render_surface(const std::shared_ptr<::cw::lib::Surface>& background, ::cw::lib::Real position_scale=1.0);
    void initialize(std::shared_ptr<::cw::lib::Panel> arg_parent, ::cw::lib::Integer arg_cid, std::vector<std::shared_ptr<::cw::lib::Bitmap>> arg_bmps, std::vector<std::shared_ptr<::cw::lib::Bitmap>> arg_bmps_bmpdepthkey, std::optional<std::tuple<::cw::lib::Integer, ::cw::lib::Integer>> arg_size, std::optional<std::vector<std::shared_ptr<::cw::image::ImageInfo>>> arg_infos, std::optional<::cw::lib::Callback> arg_ss);
    void _bind();
    void OnPaint(std::shared_ptr<::cw::lib::Window> arg_event);
    void SetBitmap(std::vector<std::shared_ptr<::cw::lib::Bitmap>> arg_bmps, std::vector<std::shared_ptr<::cw::lib::Bitmap>> arg_bmps_bmpdepthkey, std::optional<std::vector<std::shared_ptr<::cw::image::ImageInfo>>> arg_infos);
    std::vector<std::shared_ptr<::cw::lib::Bitmap>> GetBitmap();
};

class CharClass : public ::cw::lib::Object {
public:
    CharClass() = default;
    virtual ~CharClass() = default;
    inline static std::tuple<::cw::lib::String, ::cw::lib::String> s_py_slots{};
    ::cw::lib::Value m_items{};
    ::cw::lib::Value m_negate{};
    void initialize(bool arg_negate, std::vector<std::tuple<::cw::lib::String, ::cw::lib::String>> arg_items);
};

class Comparable : public ::cw::lib::Object {
public:
    Comparable() = default;
    virtual ~Comparable() = default;
    virtual bool less_than(::cw::lib::Value arg_other) = 0;
};

class Concat : public ::cw::lib::Object {
public:
    Concat() = default;
    virtual ~Concat() = default;
    inline static std::tuple<::cw::lib::String> s_py_slots{};
    ::cw::lib::Value m_nodes{};
    void initialize(::cw::lib::Value arg_nodes);
};

class Depth1Surface : public ::cw::lib::Object {
public:
    Depth1Surface() = default;
    virtual ~Depth1Surface() = default;
    bool m_bmpdepthis1{};
    ::cw::lib::Real m_scr_scale{};
    // PythonではSurfaceを継承するが、ネイティブのラッパーは独立した共
    // 通画像を所有する。 copy/convert_alphaは、倍率・色深度の情
    // 報を持つSurfaceを共通画像APIで返す。 ネイティブ画面を利用できな
    // ければnullとし、copy/convert_alphaもそれを引き継ぐ。
    std::shared_ptr<::cw::lib::Surface> m_surface_typed{};
    void initialize(std::shared_ptr<::cw::lib::Surface> arg_surface, ::cw::lib::Real arg_scr_scale, ::cw::lib::Integer arg_bmpdepth);
    std::shared_ptr<::cw::lib::Surface> copy();
    std::shared_ptr<::cw::lib::Surface> convert_alpha(std::optional<std::shared_ptr<::cw::lib::Surface>> arg_surface);
};

class Dot : public ::cw::lib::Object {
public:
    Dot() = default;
    virtual ~Dot() = default;
    inline static std::tuple<> s_py_slots{};
};


class FileSync : public ::cw::lib::Thread {
public:
    FileSync() = default;
    virtual ~FileSync();
    std::vector<std::tuple<::cw::lib::String, ::cw::lib::Value, ::cw::lib::String, std::optional<::cw::lib::String>>> m__files{};
    ::cw::lib::Value m__mutex{};
    bool m__quit{};
    // 型付きの同期状態。m__mutexは生成済みのPython形式のオブジェ
    // クトとの互換用に残し、 C++のキューではこちらのミューテックスを使う。
    std::shared_ptr<::cw::lib::Mutex> m_mutex_typed{};
    std::condition_variable_any m_files_changed_typed{};
    std::atomic_bool m_quit_typed{false};
    bool m_started_typed{};
    // 終了後のワーカーのjoinとキュー処理を直列化する。Pythonのpush()は処
    // 理前にjoinする。 ネイティブのquit()も、確実な終了のためjoinする。
    std::mutex m_join_typed{};
    std::mutex m_writer_typed{};
    // 置き換えが完了するまで、キューの先頭項目を保持する。
    std::size_t m_inflight_typed{};
    // ネイティブ版の保存保証として、Python版とは異なり、キュー内の書き
    // 込み失敗を 記録して破棄するだけでなく、sync()から最初の失敗を再
    // 送出する。 YadoData::saveが成功と誤報せず、メモリ上の
    // 再試行状態を保持できるようにする。 失敗した一時ファイルも削除する。
    ::cw::lib::ExceptionPtr m_write_error_typed{};
    // initialize()より前に設定する。replaceはネイティブの永続化操
    // 作だけを担当し、 キュー・エラー・再試行の規則はすべてのホストで共通にする。
    std::function<void(const std::filesystem::path&,const std::filesystem::path&,std::error_code&)> m_native_replace{};
    void initialize();
    void quit();
    void push(::cw::lib::String arg_file, ::cw::lib::Value arg_data, ::cw::lib::String arg_mode, std::optional<::cw::lib::String> arg_encoding);
    void sync();
    bool is_waiting(::cw::lib::String arg_file);
    static void replace_native_file(const std::filesystem::path& from, const std::filesystem::path& to, std::error_code& error);
    static void flush_native_file(const std::filesystem::path& path);
    static void validate_write_mode(const ::cw::lib::String& mode);
    void _write_files();
    void run();
};

class Group : public ::cw::lib::Object {
public:
    Group() = default;
    virtual ~Group() = default;
    inline static std::tuple<::cw::lib::String> s_py_slots{};
    ::cw::lib::Value m_node{};
    void initialize(::cw::lib::Value arg_node);
};

class Lit : public ::cw::lib::Object {
public:
    Lit() = default;
    virtual ~Lit() = default;
    inline static std::tuple<::cw::lib::String> s_py_slots{};
    ::cw::lib::Value m_ch{};
    void initialize(::cw::lib::String arg_ch);
};

class MusicInterface : public ::cw::lib::Object,
                       public std::enable_shared_from_this<MusicInterface> {
public:
    MusicInterface() = default;
    virtual ~MusicInterface() = default;
    bool m__bass{};
    std::optional<std::shared_ptr<::cw::lib::Object>> m__movie{};
    bool m__winmm{};
    ::cw::lib::Value m_channel{};
    ::cw::lib::String m_fpath{};
    bool m_inusecard{};
    ::cw::lib::Integer m_loopcount{};
    ::cw::lib::Value m_mastervolume{};
    std::optional<std::shared_ptr<::cw::lib::Surface>> m_movie_scr{};
    ::cw::lib::String m_path{};
    ::cw::lib::Integer m_subvolume{};
    // バックエンドがない場合も、型付きの状態でPythonの属性を保持する。
    ::cw::lib::Integer m_channel_typed{};
    ::cw::lib::Integer m_mastervolume_typed{100};
    ::cw::lib::Integer m_subvolume_typed{100};
    ::cw::lib::Integer m_loopcount_typed{};
    ::cw::lib::Real m_volume_typed{};
    ::cw::lib::String m_path_typed{};
    ::cw::lib::String m_fpath_typed{};
    bool m_inusecard_typed{};
    bool m_winmm_typed{};
    bool m_bass_typed{};
    bool m_playing_typed{};
    bool m_paused_typed{};
    void initialize(::cw::lib::Integer arg_channel, ::cw::lib::Integer arg_mastervolume);
    void update_scale();
    void play(::cw::lib::String arg_path, bool arg_updatepredata, bool arg_restart, bool arg_inusecard, ::cw::lib::Integer arg_subvolume, ::cw::lib::Integer arg_loopcount, ::cw::lib::Integer arg_fade, ::cw::lib::String arg_fullpath);
    void _play(::cw::lib::String arg_path, bool arg_updatepredata, bool arg_restart, bool arg_inusecard, ::cw::lib::Integer arg_subvolume, ::cw::lib::Integer arg_loopcount, ::cw::lib::Integer arg_fade, ::cw::lib::String arg_fullpath);
    void stop(::cw::lib::Integer arg_fade);
    void stop_impl(::cw::lib::Integer arg_fade, bool arg_stopfadeout, bool arg_updatepredata);
    void pause();
    void resume();
    ::cw::lib::Real _get_volumevalue(::cw::lib::String arg_fpath);
    void set_volume(std::optional<::cw::lib::Real> arg_volume = std::nullopt, ::cw::lib::Integer arg_fade = 0);
    void set_mastervolume(::cw::lib::Integer arg_volume);
    ::cw::lib::String get_path(::cw::lib::String arg_path, bool arg_inusecard);
};

class Progress : public ::cw::lib::Object {
public:
    Progress() = default;
    virtual ~Progress() = default;
    std::optional<::cw::lib::String> m_result{};
    bool m_cancel{};
    ::cw::lib::Callback m_job{};
};

class Repeat : public ::cw::lib::Object {
public:
    Repeat() = default;
    virtual ~Repeat() = default;
    inline static std::tuple<::cw::lib::String, ::cw::lib::String> s_py_slots{};
    ::cw::lib::Value m_node{};
    ::cw::lib::Value m_op{};
    void initialize(::cw::lib::Value arg_node, ::cw::lib::String arg_op);
};

class SoundInterface : public ::cw::lib::Object,
                       public std::enable_shared_from_this<SoundInterface> {
public:
    SoundInterface() = default;
    virtual ~SoundInterface() = default;
    ::cw::lib::Value m__is_midi{};
    ::cw::lib::Value m__path{};
    ::cw::lib::Value m__sound{};
    ::cw::lib::Integer m__type{};
    ::cw::lib::Value m_channel{};
    ::cw::lib::Integer m_mastervolume{};
    ::cw::lib::Integer m_subvolume{};
    std::optional<::cw::lib::Value> m_sound_typed{};
    ::cw::lib::String m_path_typed{};
    bool m_is_midi_typed{};
    ::cw::lib::Integer m_channel_typed{-1};
    ::cw::lib::Integer m_type_typed{-1};
    ::cw::lib::Integer m_mastervolume_typed{};
    ::cw::lib::Integer m_subvolume_typed{100};
    void initialize(std::optional<::cw::lib::Value> arg_sound = std::nullopt, ::cw::lib::String arg_path = {}, bool arg_is_midi = false);
    std::shared_ptr<::cw::util::SoundInterface> copy();
    ::cw::lib::String get_path();
    ::cw::lib::String _play_before(bool arg_from_scenario, ::cw::lib::Integer arg_channel, ::cw::lib::Integer arg_fade);
    void play(bool arg_from_scenario, ::cw::lib::Integer arg_subvolume, ::cw::lib::Integer arg_loopcount, ::cw::lib::Integer arg_channel, ::cw::lib::Integer arg_fade);
    void stop(bool arg_from_scenario, ::cw::lib::Integer arg_fade);
    void stop_impl(bool arg_from_scenario, ::cw::lib::Integer arg_fade, bool arg_stopfadeout);
    ::cw::lib::Real _get_volumevalue(::cw::lib::String arg_fpath);
    void set_mastervolume(bool arg_from_scenario, ::cw::lib::Integer arg_volume);
    void set_volume(bool arg_from_scenario, std::optional<::cw::lib::Integer> arg_volume = std::nullopt);
};

class _AbsList : public ::cw::lib::Object {
public:
    _AbsList() = default;
    virtual ~_AbsList() = default;
    std::vector<::cw::lib::Value> m_seq_typed{};
};

class _FormatPart : public ::cw::lib::Object {
public:
    _FormatPart() = default;
    virtual ~_FormatPart() = default;
    ::cw::lib::String m_name_typed{};
};

class _LhafileWrapper : public ::cw::lib::Object {
public:
    _LhafileWrapper() = default;
    virtual ~_LhafileWrapper() = default;
    // ソース互換用に残す生成済みフィールド。 読み込み器はPython
    // のインスタンス単位の寿命に合わせ、m_fだけが所有する。
    inline static ::cw::lib::Value s_f{};
    ::cw::lib::Value m_f{};
    void initialize(::cw::lib::String arg_path, ::cw::lib::String arg_mode);
    std::shared_ptr<::cw::util::_LhafileWrapper> enter();
    ::cw::lib::Value exit(std::optional<::cw::lib::Value> arg_exc_type, std::optional<::cw::lib::Value> arg_exc_value, std::optional<::cw::lib::Value> arg_traceback);
    void close();
};

// Pythonはここでctypesのフィールド情報を公開する。ネイティブのロック・解除
// は SDKのOVERLAPPEDを直接使い、ポインター幅とアラインメントをWindows
// ABIへ合わせる。 この互換オブジェクトを別の構造体配置としては使わない。
class _OVERLAPPED : public ::cw::lib::Object {
public:
    _OVERLAPPED() = default;
    virtual ~_OVERLAPPED() = default;
    inline static std::vector<std::tuple<::cw::lib::String, ::cw::lib::Value>> s__fields_{};
};

class _ParseError : public ::cw::lib::Object {
public:
    _ParseError() = default;
    virtual ~_ParseError() = default;
};

class _Parser : public ::cw::lib::Object {
public:
    _Parser() = default;
    virtual ~_Parser() = default;
    ::cw::lib::Integer m_i{};
    ::cw::lib::Integer m_n{};
    ::cw::lib::Value m_p{};
    ::cw::lib::String m_pattern_typed{};
    void initialize(::cw::lib::String arg_pattern);
    ::cw::lib::Value peek();
    ::cw::lib::String consume(std::optional<::cw::lib::String> arg_expected = std::nullopt);
    ::cw::lib::Value parse();
    ::cw::lib::Value parse_alt();
    ::cw::lib::Value parse_concat();
    ::cw::lib::Value parse_repeat();
    ::cw::lib::Value parse_atom();
    ::cw::lib::Value parse_charclass();
    ::cw::lib::String parse_class_char();
};

class _Unlock : public ::cw::lib::Object {
public:
    _Unlock() = default;
    virtual ~_Unlock() = default;
    std::optional<::cw::lib::Value> m_f{};
    ::cw::lib::Value m_name{};
    void initialize(::cw::lib::String arg_name, ::cw::lib::Value arg_f);
    void unlock();
};

class LogicalStr : public ::cw::util::Comparable {
public:
    LogicalStr() = default;
    virtual ~LogicalStr() = default;
    ::cw::lib::Value m_s{};
    std::vector<::cw::lib::Value> m_seq{};
    void initialize(::cw::lib::String arg_s);
    bool less_than(::cw::lib::Value arg_other);
    bool equals(::cw::lib::Value arg_other);
};
// 通常の型付きモデルから使う、元のLogicalStrと共通の比較処理。
[[nodiscard]] ::cw::lib::Integer logical_compare(const ::cw::lib::String& lhs,
                                                const ::cw::lib::String& rhs);

class _List : public ::cw::util::_AbsList {
public:
    _List() = default;
    virtual ~_List() = default;
};

extern ::cw::lib::Integer g_NO_OVERWRITE;
extern ::cw::lib::Integer g_OVERWRITE_ALWAYS;
extern ::cw::lib::Integer g_OVERWRITE_WITH_LATEST_FILES;
extern ::cw::lib::String g_WRAPS_CHARS;
extern std::tuple<::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String> g__ANIMATED_WXBMP_ATTRS;
extern ::cw::lib::ValueSet g__ESCAPABLE;
extern ::cw::lib::Value g__SortableByAttrT;
extern ::cw::lib::Value g__SortableT;
extern ::cw::lib::Value g__SyncLock;
extern std::set<std::shared_ptr<::cw::lib::Thread>> g__cominit_table;
extern ::cw::lib::Value g__lock_mutex;
extern std::vector<std::tuple<::cw::lib::Value, ::cw::lib::String>> g__mutex;
extern ::cw::lib::Value g__re_num;
extern ::cw::lib::Value g__shlwapi;
// Pythonの再入不可のthreading.Lockに対応するネイティブの型付き
// ロック。 zip_fileとcompress_zipの呼び出し全体で共有する。
extern std::mutex g__zip_mutex;
extern std::map<::cw::lib::String, ::cw::lib::Real> g_dictimes;
extern ::cw::lib::Real g_timer;
extern ::cw::lib::Value g_times;

std::tuple<std::shared_ptr<::cw::lib::Surface>, std::shared_ptr<::cw::lib::Surface>, std::optional<std::shared_ptr<::cw::lib::Surface>>, std::shared_ptr<::cw::lib::Object>> init(std::optional<std::tuple<::cw::lib::Integer, ::cw::lib::Integer>> arg_size_noscale, ::cw::lib::String arg_title, bool arg_fullscreen, std::optional<std::vector<std::tuple<::cw::lib::String, bool, ::cw::lib::Integer>>> arg_soundfonts, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_fullscreensize, bool arg_sdlmixer_enabled);
void sdlmixer_init();
// 設定のSoundFont再初期化でも使う、共通ミキサーのライフサイクル処理。
bool sdlmixer_is_initialized();
void sdlmixer_quit();
// 定期処理が必要なバックエンドを、Engineのゲームスレッドから進める。
// 特にWinMM/MCIのシーケンサー命令はエイリアスを開いたのと同じスレッ
// ドで送る必要があり、 切り離したループワーカーからの操作は保証できない。
void poll_music_playback();
std::tuple<::cw::lib::Integer, ::cw::lib::Integer> convert_maskpos(::cw::lib::Value arg_maskpos, ::cw::lib::Integer arg_width, ::cw::lib::Integer arg_height);
std::vector<std::tuple<::cw::lib::String, ::cw::lib::Integer>> get_scaledimagepaths(::cw::lib::String arg_path, bool arg_can_loaded_scaledimage);
void copy_scaledimagepaths(::cw::lib::String arg_frompath, ::cw::lib::String arg_topath, bool arg_can_loaded_scaledimage);
void remove_scaledimagepaths(::cw::lib::String arg_fpath, bool arg_can_loaded_scaledimage, bool arg_trashbox);
std::tuple<::cw::lib::String, ::cw::lib::Integer> find_scaledimagepath(::cw::lib::String arg_path, ::cw::lib::Real arg_up_scr, bool arg_can_loaded_scaledimage, bool arg_noscale);
::cw::lib::String find_noscalepath(::cw::lib::String arg_path);
std::shared_ptr<::cw::lib::Surface> load_image(::cw::lib::String arg_path, bool arg_mask, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_maskpos, std::optional<::cw::lib::Value> arg_f, bool arg_retry, bool arg_isback, bool arg_can_loaded_scaledimage, bool arg_noscale, std::optional<::cw::lib::Value> arg_up_scr, bool arg_use_excache, std::optional<::cw::lib::String> arg_named_maskpos = std::nullopt);
std::shared_ptr<::cw::lib::Surface> load_image_load_pillow_surface(::cw::lib::Bytes arg_data);
::cw::lib::Integer calc_imagesize(std::shared_ptr<::cw::lib::Surface> arg_image);
::cw::lib::Integer calc_wxbmpsize(std::shared_ptr<::cw::lib::Bitmap> arg_wxbmp);
std::shared_ptr<::cw::lib::Surface> put_number(std::shared_ptr<::cw::lib::Surface> arg_image, ::cw::lib::Integer arg_num);
::cw::lib::String get_imageext(::cw::lib::Bytes arg_b);
std::map<std::optional<std::tuple<::cw::lib::Integer, ::cw::lib::String, ::cw::lib::String>>, std::vector<std::vector<std::shared_ptr<::cw::image::ImageInfo>>>> get_facepaths(::cw::lib::String arg_sexcoupon, ::cw::lib::String arg_agecoupon, bool arg_adddefaults, bool include_all_folders = false);
void _get_facepaths(::cw::lib::String arg_facedir, std::map<std::optional<std::tuple<::cw::lib::Integer, ::cw::lib::String, ::cw::lib::String>>, std::vector<std::vector<std::shared_ptr<::cw::image::ImageInfo>>>>& arg_imgpaths, std::vector<std::tuple<::cw::lib::Integer, ::cw::lib::String, ::cw::lib::String>> arg_dpaths, std::set<::cw::lib::String>& arg_passed);
bool is_face_variant_imageinfos(std::vector<std::shared_ptr<::cw::image::ImageInfo>> arg_infos);
std::optional<::cw::lib::String> _extract_base_for_matching(::cw::lib::String arg_fname);
std::vector<std::shared_ptr<::cw::image::ImageInfo>> pick_up_basename_image(std::vector<std::shared_ptr<::cw::image::ImageInfo>> arg_infos, bool arg_skip_xN);
bool pick_up_basename_image__is_scaled_fname(::cw::lib::String arg_fname);
std::vector<std::vector<std::shared_ptr<::cw::image::ImageInfo>>> pick_up_message_imgpaths(std::vector<std::vector<std::shared_ptr<::cw::image::ImageInfo>>> arg_imgpaths, bool arg_skip_xN);
::cw::lib::Integer load_bgm(::cw::lib::String arg_path);
std::shared_ptr<::cw::util::SoundInterface> load_sound(::cw::lib::String arg_path);
bool is_midi(::cw::lib::String arg_path);
::cw::lib::String get_soundfilepath(::cw::lib::String arg_basedir, ::cw::lib::Value arg_path);
void remove_soundtempfile(::cw::lib::String arg_basedir);
std::vector<::cw::lib::Value> _sorted_by_attr_impl(bool arg_d, std::vector<::cw::lib::Value>& arg_seq, const ::cw::lib::Arguments& arg_attr = {}, std::optional<::cw::lib::Callback> arg_cmpfunc = std::nullopt);
::cw::lib::Integer cmp(std::optional<::cw::lib::Value> arg_a, std::optional<::cw::lib::Value> arg_b);
std::vector<::cw::lib::Value> sorted_by_attr(std::vector<::cw::lib::Value> arg_seq, const ::cw::lib::Arguments& arg_attr = {});
std::vector<::cw::lib::Value>& sort_by_attr(std::vector<::cw::lib::Value>& arg_seq, const ::cw::lib::Arguments& arg_attr = {});
std::vector<::cw::lib::Value>& sort_by_filename(std::vector<::cw::lib::Value>& arg_seq, const ::cw::lib::Arguments& arg_attr = {});
// 一時的な配列も同じ変更可能な実装を使い、その後で所有権を移す。
inline std::vector<::cw::lib::Value> sort_by_attr(std::vector<::cw::lib::Value>&& seq,const ::cw::lib::Arguments& attrs={}) { sort_by_attr(seq,attrs);return std::move(seq); }
inline std::vector<::cw::lib::Value> sort_by_filename(std::vector<::cw::lib::Value>&& seq,const ::cw::lib::Arguments& attrs={}) { sort_by_filename(seq,attrs);return std::move(seq); }
inline std::vector<::cw::lib::Value> _sorted_by_attr_impl(bool d,std::vector<::cw::lib::Value>&& seq,const ::cw::lib::Arguments& attrs={},std::optional<::cw::lib::Callback> cmp=std::nullopt) { return _sorted_by_attr_impl(d,seq,attrs,cmp); }
::cw::lib::Integer sort_by_filename_cmp_fname(::cw::lib::String arg_a, ::cw::lib::String arg_b);
// 動的に数値となる順序属性と整数のオーバーフローを、Valueとして保持する。
::cw::lib::Value new_order(::cw::lib::Value arg_seq, ::cw::lib::Integer arg_mode = 1);
// 内部パスの区切りは常に/を使う。 OSのファイル操作との境界でのみ
// 、std::filesystemがネイティブの区切りへ変換する。
::cw::lib::String normalize_path(::cw::lib::String arg_path);
::cw::lib::String join_paths(const ::cw::lib::Arguments& arg_paths);
::cw::lib::String relpath(::cw::lib::String arg_path, ::cw::lib::String arg_start);
::cw::lib::String selected_document_path(const ::cw::lib::String& path,const ::cw::lib::String& base);
::cw::lib::String validate_filepath(std::optional<::cw::lib::String> arg_fpath);
std::vector<::cw::lib::String> validate_filepath_2(std::vector<std::optional<::cw::lib::String>> arg_fpath);
::cw::lib::Value validate_filepath_3(std::optional<::cw::lib::Value> arg_fpath);
::cw::lib::String is_descendant(::cw::lib::String arg_path, ::cw::lib::String arg_start);
std::tuple<::cw::lib::String, ::cw::lib::String> splitext(::cw::lib::String arg_fname);
bool str2bool(::cw::lib::String arg_s);
// Pyはbool入力をそのまま返す。このオーバーロードは本当の
// boolだけに制限する。 制限しないと文字列のfalseが、
// String版で解析される前にtrueへ変換されてしまう。
template<typename T> requires std::is_same_v<T, bool>
constexpr bool str2bool(T arg_s) noexcept { return arg_s; }
// 上下限が逆でも、Pythonのif/elifと同じく下限を先に判定する。
::cw::lib::Integer numwrap(::cw::lib::Integer arg_n, ::cw::lib::Integer arg_nmin, ::cw::lib::Integer arg_nmax);
::cw::lib::Real numwrap_2(::cw::lib::Real arg_n, ::cw::lib::Real arg_nmin, ::cw::lib::Real arg_nmax);
// 選んだ引数の型を保つ。比較できないスカラーの順序にはinvalid_argument （PythonのTypeError
// 相当）、Decimal/NaNにはDecimalInvalidOperationを投げる。
::cw::lib::Value numwrap_3(::cw::lib::Value arg_n, ::cw::lib::Value arg_nmin, ::cw::lib::Value arg_nmax);
::cw::lib::Integer div_vocation(::cw::lib::Integer arg_value);
// Pyと同じ厳密なASCII名の扱い。非ASCIIはEncodingError、途中で切れたフィー
// ルドはout_of_range、 名前テーブルなしはinvalid_argument、空のディ
// レクトリはlogic_error、 ファイルシステムの失敗はruntime_errorとする。
::cw::lib::String get_truetypefontname(::cw::lib::String arg_path);
::cw::lib::String get_md5(::cw::lib::String arg_path);
// 読み取り専用のバイト列はPythonのbytesと同様に借用し、入力全体のコピーを避ける。
::cw::lib::String get_md5_from_data(const ::cw::lib::Bytes& arg_data);
::cw::lib::Integer number_normalization(::cw::lib::Integer arg_value, ::cw::lib::Integer arg_fromvalue, ::cw::lib::Integer arg_tovalue);
void print_ex(std::optional<::cw::lib::Value> arg_file = std::nullopt);
::cw::lib::String screenshot_title(std::map<::cw::lib::String, ::cw::lib::String> arg_titledic);
std::tuple<std::shared_ptr<::cw::lib::Surface>, std::shared_ptr<::cw::lib::Surface>, ::cw::lib::Integer, ::cw::lib::Integer> screenshot_header(::cw::lib::String arg_title, ::cw::lib::Integer arg_w);
void screenshot();
::cw::lib::String create_screenshotfilename(std::map<::cw::lib::String, ::cw::lib::String> arg_titledic);
std::tuple<std::shared_ptr<::cw::lib::Surface>, ::cw::lib::Integer> create_screenshot(std::map<::cw::lib::String, ::cw::lib::String> arg_titledic);
void card_screenshot();
::cw::lib::String create_cardscreenshotfilename(std::map<::cw::lib::String, ::cw::lib::String> arg_titledic);
std::shared_ptr<::cw::lib::Surface> create_cardscreenshot(std::map<::cw::lib::String, ::cw::lib::String> arg_titledic);
void to_clipboard(::cw::lib::String arg_s);
::cw::lib::String dupcheck_plus(::cw::lib::String arg_path, bool arg_yado, bool arg_checkfilename);
::cw::lib::String repl_dischar(::cw::lib::String arg_fname);
bool check_dischar(::cw::lib::String arg_s);
bool path_starts_with(const ::cw::lib::String& arg_path, const ::cw::lib::String& arg_prefix);
bool path_exists(const ::cw::lib::String& arg_path);
bool path_is_file(const ::cw::lib::String& arg_path);
bool path_is_directory(const ::cw::lib::String& arg_path);
::cw::lib::String replace_path_prefix(::cw::lib::String arg_path,
                                      const ::cw::lib::String& arg_prefix,
                                      const ::cw::lib::String& arg_replacement);
::cw::lib::String join_yadodir(::cw::lib::String arg_path);
// 変換ワーカー向けに宿のコンテキストを明示する。Tempの未保存ファイルを優先するパス解決規則は共通にする。
::cw::lib::String join_yadodir(::cw::lib::String arg_path,
    const ::cw::lib::String& saved_root, const ::cw::lib::String& temp_root);
// 絶対パスで比較し、保存済み・Tempのパスは各ルートの名前空間で返す。
std::optional<std::pair<::cw::lib::String, ::cw::lib::String>> resolve_yado_paths(
    const ::cw::lib::String& path, const ::cw::lib::String& saved_root,
    const ::cw::lib::String& temp_root);
// 未確定の宿の書き込みを、元のパスへ流さない。未知のパス
// や保存・作業ルートの重なりは例外にする。 明示的な保存
// ・取り込み・書き出し操作は、それぞれの指定先を使う。
::cw::lib::String get_yado_writepath(const ::cw::lib::String& path,
    const ::cw::lib::String& saved_root, const ::cw::lib::String& temp_root);
::cw::lib::String get_yado_relpath(const ::cw::lib::String& path,
    const ::cw::lib::String& saved_root, const ::cw::lib::String& temp_root);
::cw::lib::String get_yadofilepath(::cw::lib::String arg_path, bool allow_directory = false);
::cw::lib::String find_resource(::cw::lib::String arg_path, ::cw::lib::Integer arg_mtype);
::cw::lib::String get_inusecardmaterialpath(::cw::lib::String arg_path, ::cw::lib::Integer arg_mtype, std::optional<std::shared_ptr<::cw::header::CardHeader>> arg_inusecard, bool arg_findskin);
::cw::lib::String get_materialpath(::cw::lib::String arg_path, ::cw::lib::Integer arg_mtype, ::cw::lib::String arg_scedir, bool arg_system, bool arg_findskin);
::cw::lib::String get_materialpathfromskin(::cw::lib::String arg_path, ::cw::lib::Integer arg_mtype, bool arg_findskin);
void write_file(::cw::lib::String arg_file, ::cw::lib::Value arg_data, std::shared_ptr<::cw::util::FileSync> arg_fsync, ::cw::lib::String arg_mode, std::optional<::cw::lib::String> arg_encoding);
void write_textfile(::cw::lib::String arg_file, ::cw::lib::String arg_data, std::shared_ptr<::cw::util::FileSync> arg_fsync);
void remove_temp();
void remove(::cw::lib::String arg_path, bool arg_trashbox);
void remove_file(::cw::lib::String arg_path, ::cw::lib::Integer arg_retry, bool arg_trashbox);
void add_winauth(::cw::lib::String arg_file);
void remove_tree(::cw::lib::String arg_treepath, ::cw::lib::Integer arg_retry, bool arg_noretry, bool arg_trashbox);
void remove_tree2(::cw::lib::String arg_treepath, bool arg_trashbox);
void remove_treefiles(::cw::lib::String arg_treepath, bool arg_trashbox);
void rename_file(::cw::lib::String arg_path, ::cw::lib::String arg_dstpath, bool arg_trashbox);
// shutil.moveと共通の契約。rename_fileと異なり、移動
// 前の親作成・FileSync・ 既存の移動先ファイルの削除は行わない。
void move_path(::cw::lib::String source, ::cw::lib::String destination);
namespace testing {
using MovePathRename = std::function<void(const std::filesystem::path&,
                                          const std::filesystem::path&,
                                          std::error_code&)>;
// ネイティブのrename結果だけを注入し、同じ移動規則を検証す
// る。 コピーと移動元の削除は、引き続き共通の製品実装を使う。
void move_path_with_rename_for_testing(::cw::lib::String source,
                                      ::cw::lib::String destination,
                                      MovePathRename native_rename);
}  // namespace testing
void send_trashbox(::cw::lib::String arg_path);
void send_trashbox2(std::vector<::cw::lib::String> arg_paths);
void remove_emptydir(::cw::lib::String arg_dpath);
void copytree_overwrite(::cw::lib::String arg_src, ::cw::lib::String arg_dst, ::cw::lib::Integer arg_files_overwrite);
::cw::lib::Callback synclock(std::shared_ptr<::cw::lib::Mutex> arg_lock);
::cw::lib::Value zip_file(::cw::lib::String arg_path, ::cw::lib::Value arg_mode);
::cw::lib::String compress_zip(::cw::lib::String arg_path, ::cw::lib::String arg_zpath, bool arg_unicodefilename);
::cw::lib::String decompress_zip(::cw::lib::String arg_path, ::cw::lib::String arg_dstdir, ::cw::lib::String arg_dname, std::optional<::cw::lib::Callback> arg_startup, std::optional<::cw::lib::Callback> arg_progress, bool arg_overwrite, std::optional<::cw::lib::Value> arg_z);
// PythonのZipInfo/LhaInfoを表すための言語間の変換。 LhaInfoの名
// 前は0x800が立っていてもLatin1なので、アーカイブ種別をflag_bitsとは別に
// 保持する。 既存のInteger Valueを使う呼び出し元はZipInfoのflag_bits
// を持ち、 Readerが復号済みのUTF-8名も含め、同じ公開APIの経路を保つ。
struct ArchiveFilenameInfo final : ::cw::lib::Object {
    enum class Kind { Zip, Lha };
    Kind kind;
    ::cw::lib::Integer flag_bits;
    ArchiveFilenameInfo(Kind archive_kind, ::cw::lib::Integer flags = 0)
        : kind(archive_kind), flag_bits(flags) {}
};
::cw::lib::String decode_zipfilename(::cw::lib::String arg_zname, ::cw::lib::Value arg_info);
::cw::lib::String decode_zipname(::cw::lib::Value arg_name);
::cw::lib::String decode_text(::cw::lib::Bytes arg_name);
::cw::lib::Bytes read_zipdata(::cw::lib::Value arg_zfile, ::cw::lib::String arg_name);
// 論理Storageパスとネイティブファイルを、同じ検査付きの処理で読む。
::cw::lib::Bytes read_file_bytes(const ::cw::lib::String& arg_path);
std::shared_ptr<::cw::data::CWPyElement> get_elementfromzip(::cw::lib::String arg_zpath, ::cw::lib::String arg_name, ::cw::lib::String arg_tag = {});
::cw::lib::String decompress_cab(::cw::lib::String arg_path, ::cw::lib::String arg_dstdir, ::cw::lib::String arg_dname, std::optional<::cw::lib::Callback> arg_startup, std::optional<::cw::lib::Callback> arg_progress, bool arg_overwrite);
void Progress_decompress_cab___init__(::cw::lib::Value arg_self);
void Progress_decompress_cab_run(::cw::lib::Value arg_self);
::cw::lib::Integer cab_filenum(::cw::lib::String arg_cab);
::cw::lib::String cab_hasfile(::cw::lib::String arg_cab, ::cw::lib::Value arg_fname);
std::set<::cw::lib::String> cab_dpaths(::cw::lib::String arg_cab);
::cw::lib::String cab_scdir(::cw::lib::String arg_cab);
::cw::lib::String encodewrap(::cw::lib::String arg_s);
::cw::lib::String decodewrap(::cw::lib::String arg_s, ::cw::lib::String arg_code = "\n");
::cw::lib::String encodetextlist(std::vector<::cw::lib::String> arg_arr);
std::vector<::cw::lib::String> decodetextlist(::cw::lib::String arg_s);
bool _is_hw(::cw::lib::String arg_c);
::cw::lib::Integer get_strlen(::cw::lib::String arg_s);
::cw::lib::Integer get_strlen_ln(::cw::lib::String arg_c);
::cw::lib::Value get_strlen_lambda_line_3413(::cw::lib::Value arg_a, ::cw::lib::Value arg_b);
std::tuple<::cw::lib::String, ::cw::lib::String> slice_str(::cw::lib::String arg_s, ::cw::lib::Integer arg_width, std::optional<::cw::lib::Callback> arg_get_width = std::nullopt);
::cw::lib::String rjustify(::cw::lib::String arg_s, ::cw::lib::Integer arg_length, ::cw::lib::String arg_c);
::cw::lib::String ljustify(::cw::lib::String arg_s, ::cw::lib::Integer arg_length, ::cw::lib::String arg_c);
::cw::lib::String txtwrap(::cw::lib::String arg_s, ::cw::lib::Integer arg_mode, ::cw::lib::Integer arg_width = 30, ::cw::lib::String arg_wrapschars = {}, bool arg_encodedtext = true, std::set<::cw::lib::Integer>* arg_spcharinfo = nullptr);
std::vector<::cw::lib::String> graphemes(::cw::lib::String arg_s);
::cw::lib::String _wordwrap_impl(::cw::lib::String arg_s, ::cw::lib::Integer arg_width, std::optional<::cw::lib::Callback> arg_get_width, ::cw::lib::String arg_open_chars, ::cw::lib::String arg_close_chars, ::cw::lib::Integer arg_startindex, ::cw::lib::Integer arg_resultindex, std::set<::cw::lib::Integer>* arg_spcharinfo, std::vector<::cw::lib::Integer>* arg_spcharinfo2);
bool _wordwrap_impl_match_op(std::tuple<::cw::lib::String, bool> arg_buf);
bool _wordwrap_impl_match_cl(std::tuple<::cw::lib::String, bool> arg_buf);
bool _wordwrap_impl_match_last(std::vector<std::tuple<::cw::lib::String, bool>> arg_bufs, ::cw::lib::Callback arg_matcher);
bool _wordwrap_impl_match_op_last(std::vector<std::tuple<::cw::lib::String, bool>> arg_bufs);
bool _wordwrap_impl_match_cl_last(std::vector<std::tuple<::cw::lib::String, bool>> arg_bufs);
std::tuple<std::vector<std::tuple<::cw::lib::String, bool>>, ::cw::lib::Integer, ::cw::lib::String> _wordwrap_impl_append_word_wrap(std::vector<std::tuple<::cw::lib::String, bool>> arg_buf, ::cw::lib::Integer arg_buflen, ::cw::lib::String arg_word);
std::tuple<std::vector<std::tuple<::cw::lib::String, bool>>, ::cw::lib::Integer, ::cw::lib::String> _wordwrap_impl_break_before_openchar(std::vector<std::tuple<::cw::lib::String, bool>> arg_buf2, std::vector<std::tuple<::cw::lib::String, bool>> arg_buf, ::cw::lib::Integer arg_buflen, ::cw::lib::String arg_word);
::cw::lib::String wordwrap(::cw::lib::String arg_s, ::cw::lib::Integer arg_width, std::optional<::cw::lib::Callback> arg_get_width = std::nullopt, ::cw::lib::String arg_open_chars = {}, ::cw::lib::String arg_close_chars = {}, std::set<::cw::lib::Integer>* arg_spcharinfo = nullptr);
std::tuple<::cw::lib::String, std::set<::cw::lib::Integer>> _test_wordwrap(::cw::lib::String arg_s, ::cw::lib::Integer arg_width, std::set<::cw::lib::Integer> arg_spcharinfo);
::cw::lib::String get_char(::cw::lib::String arg_s, ::cw::lib::Integer arg_index);
::cw::lib::String format_title(::cw::lib::String arg_fmt, std::map<::cw::lib::String, ::cw::lib::String> arg_d, bool arg_use_lf = false);
void _FormatPart_format_title___init__(::cw::lib::Value arg_self, ::cw::lib::String arg_name);
void _AbsList_format_title___init__(::cw::lib::Value arg_self);
void _List_format_title___init__(::cw::lib::Value arg_self, std::vector<::cw::lib::Value> arg_seq);
std::tuple<::cw::lib::String, std::vector<::cw::lib::Value>> format_title_eat_parts(::cw::lib::String arg_fmt, bool arg_subsection);
std::tuple<::cw::lib::String, bool> format_title_do_format(std::vector<::cw::lib::Value> arg_secs);
std::shared_ptr<::cw::lib::Bitmap> load_wxbmp(::cw::lib::String arg_name, bool arg_mask, std::shared_ptr<::cw::lib::Image> arg_image, ::cw::lib::Value arg_maskpos, std::optional<::cw::lib::Value> arg_f, bool arg_retry, bool arg_can_loaded_scaledimage, bool arg_noscale, std::optional<::cw::lib::Value> arg_up_scr);
std::shared_ptr<::cw::lib::Bitmap> load_wxbmp_masked_empty_bitmap();
void set_animated_wxbmp_info(std::shared_ptr<::cw::lib::Bitmap> arg_wxbmp, ::cw::lib::String arg_path, std::optional<::cw::lib::Integer> arg_instance_id, ::cw::lib::Integer arg_frame_id, bool arg_mask, ::cw::lib::Value arg_maskpos, ::cw::lib::Value arg_scr_scale, std::optional<::cw::lib::String> arg_owner = std::nullopt, std::optional<::cw::lib::Bytes> arg_data = std::nullopt);
std::shared_ptr<::cw::lib::Bitmap> copy_animated_wxbmp_info(std::shared_ptr<::cw::lib::Bitmap> arg_src, std::shared_ptr<::cw::lib::Bitmap> arg_dst);
// デコードしたアニメーションフレームにも、置き換え前の静止画像と同じ表示サイズ・
// 倍率の規則を適用する。 ダイアログへデコーダーの生のフレームを直接設定しない。
std::shared_ptr<::cw::lib::Bitmap> conform_animated_wxbmp_frame(
    std::shared_ptr<::cw::lib::Bitmap> arg_frame,
    const std::shared_ptr<::cw::lib::Bitmap>& arg_reference);
bool is_animated_wxbmp(std::shared_ptr<::cw::lib::Bitmap> arg_wxbmp);
::cw::lib::Integer animated_wxbmp_frame_id(std::shared_ptr<::cw::lib::Bitmap> arg_wxbmp);
void set_animated_wxbmp_opaque(std::shared_ptr<::cw::lib::Bitmap> arg_wxbmp);
std::shared_ptr<::cw::lib::Bitmap> resolve_animated_wxbmp(std::shared_ptr<::cw::lib::Bitmap> arg_wxbmp);
void release_animated_wxbmp(std::shared_ptr<::cw::lib::Bitmap> arg_wxbmp);
std::shared_ptr<::cw::lib::Bitmap> empty_bitmap(::cw::lib::Integer arg_w, ::cw::lib::Integer arg_h);
std::shared_ptr<::cw::lib::Bitmap> empty_bitmap_rgba(::cw::lib::Integer arg_w, ::cw::lib::Integer arg_h);
std::shared_ptr<::cw::lib::Bitmap> copy_wxbmp(std::shared_ptr<::cw::lib::Bitmap> arg_bmp, bool arg_usebuffer = false);
std::shared_ptr<::cw::lib::Image> convert_to_image(std::shared_ptr<::cw::lib::Bitmap> arg_bmp);
std::vector<::cw::lib::Integer> wxbmp_to_buffer(std::shared_ptr<::cw::lib::Bitmap> arg_bmp);
void fill_image(std::shared_ptr<::cw::lib::Surface> arg_img, std::shared_ptr<::cw::lib::Surface> arg_surface, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_csize, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_ctrlpos = {0, 0}, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_cpos = {0, 0});
// 不透明なValueを使うネイティブの接続では、空の値を既定の原点として扱う。 公開デー
// タオブジェクトを作らず、value_pairがPythonの(0, 0)へ対応付ける。
void fill_bitmap(std::shared_ptr<::cw::lib::DeviceContext> arg_dc, std::shared_ptr<::cw::lib::Bitmap> arg_bmp, ::cw::lib::Value arg_csize, ::cw::lib::Value arg_ctrlpos = {}, ::cw::lib::Value arg_cpos = {});
// Pythonとの意図的な差: 未使用のwx用文字描画APIと旧widgetを除去した。
// 製品の共通UIは既存の実フォント描画を使うため、仮文字の別経路を保持しない。
std::tuple<::cw::lib::Integer, ::cw::lib::Integer> get_centerposition(::cw::lib::Size arg_size, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_targetpos, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_targetsize = {1, 1});
void draw_box(std::shared_ptr<::cw::lib::DeviceContext> arg_dc, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_pos, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_size);
::cw::lib::Rect window_work_area(const ::cw::lib::Window& window);
::cw::lib::Rect constrain_window_to_work_area(::cw::lib::Rect bounds, ::cw::lib::Rect area);
void adjust_position(std::shared_ptr<::cw::lib::Window> arg_frame);
void clear_background(std::shared_ptr<::cw::lib::DeviceContext> arg_dc, std::shared_ptr<::cw::lib::Control> arg_window);
::cw::lib::Color get_backgroundcolour(::cw::lib::Value arg_window);
::cw::lib::String abbr_longstr(std::shared_ptr<::cw::lib::DeviceContext> arg_dc, ::cw::lib::String arg_text, ::cw::lib::Integer arg_w);
::cw::lib::String abbr_longstr_with_count(::cw::lib::String arg_text, ::cw::lib::Integer arg_w);
void add_sideclickhandlers(std::shared_ptr<::cw::lib::Panel> arg_toppanel, std::shared_ptr<::cw::lib::Window> arg_leftbtn, std::shared_ptr<::cw::lib::Window> arg_rightbtn);
bool add_sideclickhandlers__is_cursorinleft();
bool add_sideclickhandlers__is_cursorinright();
void add_sideclickhandlers__update_mousepos();
void add_sideclickhandlers_OnMotion(std::shared_ptr<::cw::lib::Window> arg_evt);
void add_sideclickhandlers_OnLeftUp(std::shared_ptr<::cw::lib::Window> arg_evt);
std::vector<AcceleratorWidgetFilter> default_accelerator_filters();
void set_acceleratortable(std::shared_ptr<::cw::lib::Window> panel,std::vector<std::tuple<::cw::lib::Integer,::cw::lib::Integer,::cw::lib::Integer>> seq,std::vector<AcceleratorWidgetFilter> filters = default_accelerator_filters());
void set_acceleratortable_legacy(std::shared_ptr<::cw::lib::Window> panel,std::vector<std::tuple<::cw::lib::Integer,::cw::lib::Integer,::cw::lib::Integer>> seq,std::vector<std::shared_ptr<::cw::lib::Control>> exemplars);
// 述語でPythonの型タプルを渡し、継承したネイティブ型も含める
// 。 従来のインスタンス配列は、転送用の互換アダプターとして残す。
template<class T> requires std::is_same_v<T,std::shared_ptr<::cw::lib::Control>>
inline void set_acceleratortable(std::shared_ptr<::cw::lib::Window> panel,std::vector<std::tuple<::cw::lib::Integer,::cw::lib::Integer,::cw::lib::Integer>> seq,std::vector<T> exemplars){set_acceleratortable_legacy(std::move(panel),std::move(seq),std::move(exemplars));}

void set_acceleratortable_with_filters(std::shared_ptr<::cw::lib::Window> panel,std::vector<std::tuple<::cw::lib::Integer,::cw::lib::Integer,::cw::lib::Integer>> seq,std::vector<AcceleratorWidgetFilter> filters);
void set_acceleratortable_recurse(std::shared_ptr<::cw::lib::Control> arg_widget);
void adjust_dropdownwidth(std::shared_ptr<::cw::lib::Window> arg_choice);
bool has_modalchild(std::shared_ptr<::cw::lib::Window> arg_frame);
std::vector<std::tuple<::cw::lib::String, bool>> CWPyRichTextCtrl_set_text_get_urls(::cw::lib::String arg_text);
void open_url(std::shared_ptr<::cw::lib::Window> arg_parentdlg, ::cw::lib::String arg_url);
void open_url_with_services(std::shared_ptr<::cw::lib::Window> arg_parentdlg,
                            ::cw::lib::String arg_url,
                            OpenUrlServices arg_services);
void show_help();
::cw::lib::Integer get_wheelrotation(::cw::lib::Integer rotation,bool horizontal);
::cw::lib::Integer get_wheelrotation(std::shared_ptr<::cw::lib::Window> arg_event);
void _co_initialize();
::cw::lib::String get_linktarget(::cw::lib::String arg_fpath);
void set_linktarget(::cw::lib::String arg_fpath, ::cw::lib::String arg_targetpath);
bool supports_shell_links() noexcept;
void create_link(::cw::lib::String arg_shortcutpath, ::cw::lib::String arg_targetpath);
::cw::lib::String get_symlinktarget(::cw::lib::String arg_path);
::cw::lib::String get_keypath(::cw::lib::String arg_path);
::cw::lib::Value py_get_path(::cw::lib::Value arg_ldata);
std::vector<bool> pick_up_face_image(::cw::lib::Value arg_target, ::cw::lib::Value arg_layer_data, bool arg_skip_xN = true);
bool pick_up_face_image__is_overlay(::cw::lib::Integer arg_idx);
::cw::lib::Real get_scale_xN(::cw::lib::String arg_path);
std::set<::cw::lib::Integer> _match_node(::cw::lib::Value arg_node, ::cw::lib::String arg_s, ::cw::lib::Integer arg_pos);
std::set<::cw::lib::Integer> _closure(::cw::lib::Value arg_node, ::cw::lib::String arg_s, ::cw::lib::Integer arg_pos);
bool regex_fullmatch(::cw::lib::String arg_pattern, ::cw::lib::String arg_string);
std::vector<::cw::lib::String> match_cellnames_by_regex(::cw::lib::String arg_pattern, std::vector<::cw::lib::String> arg_cur_names);
void t_start();
void t_end(::cw::lib::Integer arg_index);
void td_end(::cw::lib::String arg_key);
void t_reset();
void t_print();
bool create_mutex(::cw::lib::String arg_dpath);
bool exists_mutex(::cw::lib::String arg_dpath);
void release_mutex(::cw::lib::Integer arg_index = -1);
void release_mutex_path(const ::cw::lib::String& path);
void clear_mutex();
void main();
}

