#pragma once

// cw/sprite/message.py の移植。描画は小さな
// compat::Surface モデルで表し、メッセージ状態
// と文字列展開の全規則を SDL 側から利用できるようにする。
#include "cw/lib/compat.hpp"
#include "cw/fwd.hpp"
#include "cw/sprite/base.hpp"

namespace cw::sprite::message {
struct CharImageRecord {
    ::cw::lib::Point pos{};
    std::shared_ptr<::cw::lib::Surface> text{};
    std::shared_ptr<::cw::lib::Surface> special{};
    std::shared_ptr<::cw::lib::Surface> shadow{};
    ::cw::lib::Rect line{};
    ::cw::lib::Integer frame{};
};

// Value は意図的にスカラー型に限定する。動的値が生成 API の境界を
// 越えるとき、これらの保持型で Python のリスト・辞書の内容を保つ。
struct ValueList : public ::cw::lib::Object { std::vector<::cw::lib::Value> values{}; };
struct ValueMap : public ::cw::lib::Object { std::map<::cw::lib::String, ::cw::lib::Value> values{}; };
struct RectValue : public ::cw::lib::Object { RectValue() = default; explicit RectValue(::cw::lib::Rect v) : value(v) {} ::cw::lib::Rect value{}; };
struct SurfaceValue : public ::cw::lib::Object { SurfaceValue() = default; explicit SurfaceValue(std::shared_ptr<::cw::lib::Surface> v) : value(std::move(v)) {} std::shared_ptr<::cw::lib::Surface> value{}; };

// メッセージ描画をネイティブウィンドウから独立させる。共通実装が compat::Surface
// に画素を書き、SDL/Win32 ホストはメッセージ
// 展開・選択状態を変えずにバックエンドを置き換えられる。
class MessageUiBackend : public ::cw::lib::Object {
public:
    virtual ~MessageUiBackend() = default;
    virtual std::shared_ptr<::cw::lib::Surface> render_text(
        const ::cw::lib::String& arg_text, const ::cw::lib::String& arg_face,
        ::cw::lib::Integer arg_pixels, bool arg_bold, bool arg_italic,
        bool arg_antialias,
        std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> arg_colour) = 0;
    virtual void invalidate(const ::cw::lib::Rect& arg_rect) = 0;
    virtual void play_sound(const ::cw::lib::String& arg_name) = 0;
};
std::shared_ptr<MessageUiBackend> make_default_message_ui_backend();
std::shared_ptr<MessageUiBackend> message_ui_backend();

class BacklogData : public ::cw::lib::Object {
public:
    BacklogData() = default;
    virtual ~BacklogData() = default;
    ::cw::lib::Value m_bottom_noscale{};
    ::cw::lib::Value m_boundarycheck{};
    ::cw::lib::Value m_centering_x{};
    ::cw::lib::Value m_centering_y{};
    ::cw::lib::Value m_columns{};
    ::cw::lib::Value m_flag_table{};
    ::cw::lib::Value m_imgpaths{};
    ::cw::lib::Value m_name_subtable{};
    ::cw::lib::Value m_name_table{};
    ::cw::lib::Value m_names{};
    ::cw::lib::Value m_names_log{};
    std::shared_ptr<::cw::lib::Object> m_rect_noscale{};
    ::cw::lib::Value m_showing_result{};
    ::cw::lib::Value m_spcharinfo{};
    ::cw::lib::Value m_specialchars{};
    ::cw::lib::Value m_step_table{};
    ::cw::lib::Value m_talker_bottom_noscale{};
    ::cw::lib::Value m_talker_name{};
    ::cw::lib::Value m_talker_top_noscale{};
    ::cw::lib::Value m_text{};
    ::cw::lib::Value m_text_log{};
    ::cw::lib::Value m_top_noscale{};
    ::cw::lib::Integer m_type{};
    ::cw::lib::Value m_variant_table{};
    ::cw::lib::Value m_versionhint{};
    // Python の動的フィールドに対応する型付きの保持欄。
    ::cw::lib::Rect m_rect_noscale_typed{};
    ::cw::lib::String m_text_typed{};
    ::cw::lib::Integer m_top_noscale_typed{}, m_bottom_noscale_typed{};
    ::cw::lib::Integer m_talker_top_noscale_typed{}, m_talker_bottom_noscale_typed{};
    ::cw::lib::Integer m_columns_typed{1}, m_showing_result_typed{-1};
    std::optional<std::tuple<::cw::lib::String, ::cw::lib::String, bool, bool, bool>> m_versionhint_typed{};
    std::vector<std::tuple<::cw::lib::Integer, ::cw::lib::String>> m_names_typed{};
    std::vector<std::tuple<::cw::lib::Integer, ::cw::lib::String>> m_names_log_typed{};
    std::vector<std::tuple<std::shared_ptr<::cw::image::ImageInfo>, bool, std::optional<::cw::lib::Value>, std::map<::cw::lib::Integer, std::shared_ptr<::cw::lib::Surface>>>> m_imgpaths_typed{};
    std::map<::cw::lib::String, ::cw::lib::Value> m_name_table_typed{};
    std::map<::cw::lib::String, ::cw::lib::Value> m_name_subtable_typed{};
    std::map<::cw::lib::String, std::shared_ptr<::cw::data::Flag>> m_flag_table_typed{};
    std::map<::cw::lib::String, std::shared_ptr<::cw::data::Step>> m_step_table_typed{};
    std::map<::cw::lib::String, std::shared_ptr<::cw::data::Variant>> m_variant_table_typed{};
    std::set<::cw::lib::Integer> m_spcharinfo_typed{};
    std::shared_ptr<::cw::setting::ResourceTable> m_specialchars_typed{};
    std::set<::cw::lib::String> m_specialchars_used_typed{};
    std::optional<::cw::lib::String> m_talker_name_typed{};
    bool m_centering_x_typed{}, m_centering_y_typed{}, m_boundarycheck_typed{};
    void initialize(::cw::lib::Value arg_base);
    ::cw::lib::Integer get_height_noscale();
    ::cw::lib::Integer get__from_index();
    ::cw::lib::Integer get__to_index();
    ::cw::lib::Value create_message();
};

class NameListItem : public ::cw::lib::Object {
public:
    NameListItem() = default;
    virtual ~NameListItem() = default;
    ::cw::lib::Value m_data{};
    ::cw::lib::Value m_name{};
    ::cw::lib::String m_data_text{};
    ::cw::lib::String m_name_text{};
    std::optional<::cw::lib::Integer> m_name_integer{};
    std::optional<bool> m_name_bool{};
    void initialize(std::optional<::cw::lib::Value> arg_data, std::optional<::cw::lib::Value> arg_name);
};

class _NameGetter : public ::cw::lib::Object {
public:
    _NameGetter() = default;
    virtual ~_NameGetter() = default;
    ::cw::lib::Integer m_count{};
    ::cw::lib::Value m_func{};
    std::optional<::cw::lib::Callback> m_func_typed{};
    std::vector<::cw::lib::Value> m_names{};
    void initialize(::cw::lib::Callback arg_func);
    void reset();
    ::cw::lib::Value get_name();
};

class BacklogCurtain : public ::cw::sprite::base::CWPySprite {
public:
    BacklogCurtain() = default;
    virtual ~BacklogCurtain() = default;
    ::cw::lib::Value m_color{};
    ::cw::lib::Value m_image{};
    ::cw::lib::Value m_pos_noscale{};
    ::cw::lib::Value m_rect{};
    ::cw::lib::Value m_size_noscale{};
    std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> m_curtain_color{0, 0, 0, 128};
    void initialize(std::shared_ptr<::cw::lib::Object> arg_spritegrp, ::cw::lib::Value arg_layer, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_size_noscale, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_pos_noscale, std::optional<std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer>> arg_color);
    void update_scale() override;
};

class BacklogPage : public ::cw::sprite::base::CWPySprite {
public:
    BacklogPage() = default;
    virtual ~BacklogPage() = default;
    ::cw::lib::Value m_image{};
    ::cw::lib::Value m_max{};
    ::cw::lib::Value m_page{};
    ::cw::lib::Value m_rect{};
    void initialize(::cw::lib::Integer arg_page, ::cw::lib::Integer arg_pagemax, std::shared_ptr<::cw::lib::Object> arg_spritegrp);
    void update_page(::cw::lib::Integer arg_page, ::cw::lib::Integer arg_pagemax);
    void update_scale();
};

class MessageWindow : public ::cw::sprite::base::CWPySprite {
public:
    MessageWindow() = default;
    ~MessageWindow() override;
    std::vector<std::tuple<std::shared_ptr<::cw::lib::Object>, std::shared_ptr<::cw::lib::Surface>, std::shared_ptr<::cw::image::ImageInfo>, ::cw::lib::String, ::cw::lib::Integer, ::cw::lib::Integer>> m__animated_talker_blits{};
    ::cw::lib::String m__animated_talker_owner{};
    std::set<::cw::lib::String> m__animated_talker_paths{};
    std::map<::cw::lib::Integer, std::tuple<::cw::lib::String, ::cw::lib::Integer, ::cw::lib::Integer>> m__animated_talker_sources{};
    std::optional<std::shared_ptr<::cw::lib::Surface>> m__back{};
    bool m__barspchr{};
    std::optional<std::shared_ptr<::cw::lib::Surface>> m__fore{};
    ::cw::lib::Value m__linerect{};
    ::cw::lib::Value m_backlog{};
    ::cw::lib::Integer m_blockbottom_noscale{};
    ::cw::lib::Integer m_blocktop_noscale{};
    ::cw::lib::Integer m_bottom_noscale{};
    ::cw::lib::Value m_boundarycheck{};
    ::cw::lib::Value m_centering_x{};
    ::cw::lib::Value m_centering_y{};
    ::cw::lib::Value m_charimgs{};
    ::cw::lib::Value m_columns{};
    ::cw::lib::Value m_flag_table{};
    ::cw::lib::Integer m_frame{};
    ::cw::lib::Value m_image{};
    ::cw::lib::Value m_imgpaths{};
    bool m_is_drawing{};
    ::cw::lib::Value m_name_subtable{};
    ::cw::lib::Value m_name_table{};
    ::cw::lib::Value m_names{};
    std::vector<std::tuple<::cw::lib::Integer, ::cw::lib::String>> m_names_log{};
    std::vector<std::tuple<::cw::lib::Integer, ::cw::lib::String>> m_names_log_typed{};
    std::vector<std::tuple<std::shared_ptr<::cw::image::ImageInfo>, bool, std::optional<::cw::lib::Value>, std::map<::cw::lib::Integer, std::shared_ptr<::cw::lib::Surface>>>> m_imgpaths_typed{};
    ::cw::lib::Value m_rect{};
    std::shared_ptr<::cw::lib::Object> m_rect_noscale{};
    std::optional<::cw::lib::Value> m_result{};
    std::vector<std::shared_ptr<::cw::sprite::message::SelectionBar>> m_selections{};
    ::cw::lib::Value m_showing_result{};
    ::cw::lib::Value m_spcharinfo{};
    ::cw::lib::Value m_specialchars{};
    std::set<::cw::lib::String> m_specialchars_used{};
    ::cw::lib::Value m_speed{};
    ::cw::lib::Value m_step_table{};
    ::cw::lib::Value m_talker{};
    ::cw::lib::Integer m_talker_bottom_noscale{};
    std::vector<::cw::lib::Value> m_talker_image{};
    std::vector<std::tuple<std::shared_ptr<::cw::lib::Surface>, std::shared_ptr<::cw::image::ImageInfo>>> m_talker_image_typed{};
    std::optional<::cw::lib::String> m_talker_name{};
    ::cw::lib::Integer m_talker_top_noscale{};
    ::cw::lib::Value m_text{};
    ::cw::lib::String m_text_log{};
    ::cw::lib::Integer m_top_noscale{};
    ::cw::lib::Value m_trim_top_noscale{};
    ::cw::lib::Value m_variant_table{};
    ::cw::lib::Value m_versionhint{};
    // 移植側が使う型付きの保持欄。上の m_* の Value メン
    // バーは、生成された呼び出し側と保存形式の互換性のために残す。
    ::cw::lib::String m_text_typed{};
    std::vector<std::tuple<::cw::lib::Integer, ::cw::lib::String>> m_names_typed{};
    ::cw::lib::Rect m_rect_noscale_typed{};
    std::map<::cw::lib::String, ::cw::lib::Value> m_name_table_typed{};
    std::map<::cw::lib::String, ::cw::lib::Value> m_name_subtable_typed{};
    std::map<::cw::lib::String, std::shared_ptr<::cw::data::Flag>> m_flag_table_typed{};
    std::map<::cw::lib::String, std::shared_ptr<::cw::data::Step>> m_step_table_typed{};
    std::map<::cw::lib::String, std::shared_ptr<::cw::data::Variant>> m_variant_table_typed{};
    std::set<::cw::lib::Integer> m_spcharinfo_typed{};
    std::vector<CharImageRecord> m_charimgs_typed{};
    std::shared_ptr<::cw::setting::ResourceTable> m_specialchars_typed{};
    std::set<::cw::lib::String> m_specialchars_used_typed{};
    std::optional<::cw::lib::String> m_talker_name_typed{};
    bool m_backlog_typed{}, m_centering_x_typed{}, m_centering_y_typed{}, m_boundarycheck_typed{};
    bool m_barspchr_typed{true};
    std::optional<::cw::lib::Integer> m_result_typed{};
    bool m_selection_committing{};
    ::cw::lib::Integer m_showing_result_typed{-1};
    std::optional<std::tuple<::cw::lib::String, ::cw::lib::String, bool, bool, bool>> m_versionhint_typed{};
    ::cw::lib::Integer m_trim_top_noscale_typed{}, m_columns_typed{1};
    std::shared_ptr<MessageUiBackend> m_ui_backend{};
    std::vector<::cw::lib::Rect> m_dirty_rects{};
    void initialize(::cw::lib::String arg_text, std::vector<std::tuple<::cw::lib::Integer, ::cw::lib::String>> arg_names, std::optional<std::vector<std::tuple<std::shared_ptr<::cw::image::ImageInfo>, bool, std::optional<::cw::lib::Value>, std::map<::cw::lib::Integer, std::shared_ptr<::cw::lib::Surface>>>>> arg_imgpaths, std::optional<::cw::lib::Value> arg_talker, std::optional<std::tuple<::cw::lib::Integer, ::cw::lib::Integer>> arg_pos_noscale, std::optional<std::tuple<::cw::lib::Integer, ::cw::lib::Integer>> arg_size_noscale, std::optional<std::map<::cw::lib::String, ::cw::lib::Value>> arg_nametable, std::optional<std::map<::cw::lib::String, ::cw::lib::Value>> arg_namesubtable, std::optional<std::map<::cw::lib::String, std::shared_ptr<::cw::data::Flag>>> arg_flagtable, std::optional<std::map<::cw::lib::String, std::shared_ptr<::cw::data::Step>>> arg_steptable, std::optional<std::map<::cw::lib::String, std::shared_ptr<::cw::data::Variant>>> arg_varianttable, bool arg_backlog, std::optional<::cw::lib::Value> arg_result, ::cw::lib::Integer arg_showing_result, std::optional<std::tuple<::cw::lib::String, ::cw::lib::String, bool, bool, bool>> arg_versionhint, std::optional<::cw::lib::Value> arg_specialchars, ::cw::lib::Integer arg_trim_top_noscale, ::cw::lib::Integer arg_columns, std::optional<std::set<::cw::lib::Integer>> arg_spcharinfo, bool arg_centering_x, bool arg_centering_y, bool arg_boundarycheck);
    virtual void _init_image(std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_size_noscale, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_pos_noscale);
    virtual void update_scale();
    static void clear_selections();
    static void update_all_scales();
    std::vector<::cw::lib::Rect> teardown_display();
    void update(const ::cw::lib::Arguments& arg_args, const ::cw::lib::KeywordArguments& arg_kwargs) override;
    void _blit_talker_image(::cw::lib::Integer arg_index, std::shared_ptr<::cw::lib::Surface> arg_talker_image, std::shared_ptr<::cw::image::ImageInfo> arg_info, ::cw::lib::Integer arg_x, ::cw::lib::Integer arg_y, std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> arg_wincolour);
    void update_animated_talkers();
    void _restore_rect(std::shared_ptr<::cw::lib::Object> arg_rect, std::shared_ptr<::cw::lib::Surface> arg_background);
    void release_animated_talkers();
    void kill();
    static bool is_sbold();
    static std::tuple<bool, std::tuple<::cw::lib::String, ::cw::lib::String, ::cw::lib::Integer, bool, bool, bool>, ::cw::lib::Real, bool> get_messagestyledata();
    void draw_all();
    // バックログのスクロールは描画済みウィンドウを移動する。型
    // 付き・互換・代理の各矩形を同期する。draw_all()
    // は文字描画を完了するだけなので、位置決めに使わない。
    void set_backlog_position(::cw::lib::Integer arg_y_noscale);
    void draw_char();
    void create_selectionbar();
    std::vector<CharImageRecord> create_charimgs(std::optional<std::tuple<::cw::lib::Integer, ::cw::lib::Integer>> arg_pos_noscale, bool arg_init);
    std::tuple<::cw::lib::String, std::optional<std::set<::cw::lib::Integer>>> rpl_specialstr(::cw::lib::Value arg_full, ::cw::lib::String arg_s, std::optional<std::map<::cw::lib::String, ::cw::lib::Value>> arg_nametable, bool arg_localvariables, bool arg_show_cardname);
    ::cw::lib::String rpl_specialstr_2(::cw::lib::Value arg_full, ::cw::lib::String arg_s, std::optional<std::map<::cw::lib::String, ::cw::lib::Value>> arg_nametable, bool arg_localvariables, bool arg_show_cardname);
    ::cw::lib::Value rpl_specialstr_3(bool arg_full, ::cw::lib::String arg_s, std::optional<std::map<::cw::lib::String, ::cw::lib::Value>> arg_nametable, bool arg_localvariables, bool arg_show_cardname);
    std::tuple<std::optional<::cw::lib::String>, ::cw::lib::Integer> get_stepvalue(::cw::lib::String arg_key, ::cw::lib::Integer arg_full, ::cw::lib::String arg_updatetype, std::map<::cw::lib::String, ::cw::lib::Value> arg_name_table, std::optional<std::vector<std::shared_ptr<::cw::sprite::message::NameListItem>>> arg_basenamelist, ::cw::lib::Integer arg_startindex, std::optional<std::set<::cw::lib::Integer>> arg_spcharinfo, std::optional<std::vector<std::shared_ptr<::cw::sprite::message::NameListItem>>> arg_namelist, ::cw::lib::Integer arg_namelistindex, std::optional<std::tuple<::cw::lib::String, ::cw::lib::String>> arg_scenarioinfo, ::cw::lib::Integer arg_stack, std::set<::cw::lib::Integer>* out_spcharinfo = nullptr, std::vector<std::shared_ptr<NameListItem>>* out_namelist = nullptr);
    std::tuple<std::optional<::cw::lib::String>, ::cw::lib::Integer> get_flagvalue(::cw::lib::String arg_key, ::cw::lib::Integer arg_full, ::cw::lib::String arg_updatetype, std::map<::cw::lib::String, ::cw::lib::Value> arg_name_table, std::optional<std::vector<std::shared_ptr<::cw::sprite::message::NameListItem>>> arg_basenamelist, ::cw::lib::Integer arg_startindex, std::optional<std::set<::cw::lib::Integer>> arg_spcharinfo, std::optional<std::vector<std::shared_ptr<::cw::sprite::message::NameListItem>>> arg_namelist, ::cw::lib::Integer arg_namelistindex, std::optional<std::tuple<::cw::lib::String, ::cw::lib::String>> arg_scenarioinfo, ::cw::lib::Integer arg_stack, std::set<::cw::lib::Integer>* out_spcharinfo = nullptr, std::vector<std::shared_ptr<NameListItem>>* out_namelist = nullptr);
    std::tuple<std::optional<::cw::lib::String>, ::cw::lib::Integer> get_variantvalue(::cw::lib::String arg_key, ::cw::lib::Integer arg_full, ::cw::lib::String arg_updatetype, std::map<::cw::lib::String, ::cw::lib::Value> arg_name_table, std::optional<std::vector<std::shared_ptr<::cw::sprite::message::NameListItem>>> arg_basenamelist, ::cw::lib::Integer arg_startindex, std::optional<std::set<::cw::lib::Integer>> arg_spcharinfo, std::optional<std::vector<std::shared_ptr<::cw::sprite::message::NameListItem>>> arg_namelist, ::cw::lib::Integer arg_namelistindex, std::optional<std::tuple<::cw::lib::String, ::cw::lib::String>> arg_scenarioinfo, ::cw::lib::Integer arg_stack, std::set<::cw::lib::Integer>* out_spcharinfo = nullptr, std::vector<std::shared_ptr<NameListItem>>* out_namelist = nullptr);
    std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> get_fontcolour(::cw::lib::String arg_s);
};

std::shared_ptr<::cw::sprite::base::SelectableSprite> find_owned_selectable(
    const ::cw::sprite::base::SelectableSprite* raw);

// 表示対象として登録されている間は true。kill() で登録を解除する。
bool is_live_message_window(const MessageWindow* arg_window);

class SelectWindow : public ::cw::sprite::message::MessageWindow {
public:
    SelectWindow() = default;
    virtual ~SelectWindow() = default;
    // Python の MessageWindow と SelectWindow
    // は同じ属性を使う。全状態を継承し、描画・選択確定・倍率更新が、特殊文字の
    // 方針も含め initialize() の設定値をそのまま読むようにする。
    void initialize(std::vector<std::tuple<::cw::lib::Integer, ::cw::lib::String>> arg_names, ::cw::lib::String arg_text, std::optional<std::tuple<::cw::lib::Integer, ::cw::lib::Integer>> arg_pos_noscale, std::optional<std::tuple<::cw::lib::Integer, ::cw::lib::Integer>> arg_size_noscale, bool arg_backlog, std::optional<::cw::lib::Value> arg_result, ::cw::lib::Integer arg_showing_result, ::cw::lib::Integer arg_columns, bool arg_barspchr, std::optional<std::map<::cw::lib::String, ::cw::lib::Value>> arg_nametable, std::optional<std::map<::cw::lib::String, ::cw::lib::Value>> arg_namesubtable, std::optional<std::map<::cw::lib::String, std::shared_ptr<::cw::data::Flag>>> arg_flagtable, std::optional<std::map<::cw::lib::String, std::shared_ptr<::cw::data::Step>>> arg_steptable, std::optional<std::map<::cw::lib::String, std::shared_ptr<::cw::data::Variant>>> arg_varianttable, std::optional<std::tuple<::cw::lib::String, ::cw::lib::String, bool, bool, bool>> arg_versionhint, std::optional<::cw::lib::Value> arg_specialchars, std::optional<std::set<::cw::lib::Integer>> arg_spcharinfo, bool arg_centering_x);
    void _init_image(std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_size_noscale, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_pos_noscale) override;
    void update_scale() override;
    void update(const ::cw::lib::Arguments& arg_args, const ::cw::lib::KeywordArguments& arg_kwargs) override;
};

class SelectionBar : public ::cw::sprite::base::SelectableSprite {
public:
    SelectionBar() = default;
    virtual ~SelectionBar() = default;
    bool m__clicking{};
    ::cw::lib::Value m__image{};
    ::cw::lib::Value m_backlog{};
    // アニメーションとイベントの選択状態はスプライトの基底が保持する。
    ::cw::lib::Value m_group{};
    ::cw::lib::Value m_image{};
    ::cw::lib::Value m_index{};
    ::cw::lib::Value m_name{};
    ::cw::lib::Value m_pos_noscale{};
    ::cw::lib::Value m_rect{};
    std::shared_ptr<::cw::lib::Object> m_rect_noscale{};
    ::cw::lib::Value m_selected{};
    ::cw::lib::Value m_showing_index{};
    ::cw::lib::Value m_size_noscale{};
    ::cw::lib::Integer m_showing_index_typed{};
    ::cw::lib::Integer m_index_typed{};
    ::cw::lib::String m_name_typed{};
    ::cw::lib::Point m_pos_noscale_typed{};
    ::cw::lib::Size m_size_noscale_typed{};
    bool m_backlog_typed{}, m_selected_typed{}, m_clicking_typed{};
    std::shared_ptr<::cw::lib::Surface> m_image_typed{};
    std::shared_ptr<MessageUiBackend> m_ui_backend{};
    void initialize(::cw::lib::Integer arg_showing_index, std::tuple<::cw::lib::Integer, ::cw::lib::String> arg_name, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_pos_noscale, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_size_noscale, bool arg_backlog, bool arg_selected);
    std::shared_ptr<::cw::lib::Surface> get_unselectedimage();
    std::shared_ptr<::cw::lib::Surface> get_selectedimage();
    bool is_selection() override;
    void update_scale() override;
    void update(const ::cw::lib::Arguments& arg_args, const ::cw::lib::KeywordArguments& arg_kwargs) override;
    void update_click();
    std::shared_ptr<::cw::lib::Surface> get_image(std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_size);
    void lclick_event() override;
    void lclick_event(bool arg_skip);
};

class MemberSelectWindow : public ::cw::sprite::message::SelectWindow {
public:
    MemberSelectWindow() = default;
    virtual ~MemberSelectWindow() = default;
    ::cw::lib::Value m_selectmembers{};
    std::vector<std::shared_ptr<::cw::character::Character>> m_selectmembers_typed{};
    void initialize(std::vector<std::shared_ptr<::cw::character::Character>> arg_pcards, std::optional<std::tuple<::cw::lib::Integer, ::cw::lib::Integer>> arg_pos_noscale, std::optional<std::tuple<::cw::lib::Integer, ::cw::lib::Integer>> arg_size_noscale);
};

extern ::cw::lib::Value g__NameData;
extern ::cw::lib::Integer g__SP_CARD_NAME;
extern ::cw::lib::Integer g__SP_EXPAND_SHARPS;
extern ::cw::lib::Integer g__SP_FULL;
extern ::cw::lib::Integer g__SP_LOCAL_VARIABLES;
extern ::cw::lib::Integer g__SP_NO_SHARPS;
extern ::cw::lib::String g__WAIT_CHARS;
extern ::cw::lib::String g__WAIT_CHARS_BEFORE_SPACE;
extern std::map<std::tuple<::cw::lib::Integer, ::cw::lib::Integer, std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer>>, std::shared_ptr<::cw::lib::Surface>> g__decorate_cache;
extern ::cw::lib::Real g__decorate_cache_upscr;

::cw::lib::Integer MessageWindow_create_charimgs_calc_w(std::tuple<std::shared_ptr<::cw::lib::Surface>, std::shared_ptr<::cw::image::ImageInfo>> arg_bmp_and_info);
void MessageWindow_create_charimgs_put_xinfo(::cw::lib::Integer arg_x, ::cw::lib::Integer arg_width);
void MessageWindow_create_charimgs_put_topbottom(::cw::lib::Integer arg_y, ::cw::lib::Integer arg_height);
std::tuple<::cw::lib::Real, bool, bool> MessageWindow_create_charimgs_add_wait(bool arg_space, bool arg_is_waitchar);
std::shared_ptr<::cw::lib::Surface> decorate(std::shared_ptr<::cw::lib::Surface> arg_image, ::cw::lib::Integer arg_angle, std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> arg_basecolour);
void draw_frame(std::shared_ptr<::cw::lib::Surface> arg_image, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_size, std::optional<std::tuple<::cw::lib::Integer, ::cw::lib::Integer>> arg_pos, bool arg_backlog);
std::tuple<std::tuple<::cw::lib::Integer, ::cw::lib::Integer>, std::tuple<::cw::lib::Integer, ::cw::lib::Integer>, std::tuple<::cw::lib::Integer, ::cw::lib::Integer>, std::tuple<::cw::lib::Integer, ::cw::lib::Integer>, std::tuple<::cw::lib::Integer, ::cw::lib::Integer>> get_pointlist(std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_size, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_pos);
std::tuple<::cw::lib::String, std::vector<std::shared_ptr<::cw::sprite::message::NameListItem>>> rpl_specialstr(::cw::lib::String arg_s, std::optional<std::vector<std::shared_ptr<::cw::sprite::message::NameListItem>>> arg_basenamelist, bool arg_expandsharps, ::cw::lib::String arg_updatetype, bool arg_localvariables, bool arg_show_cardname, std::optional<std::tuple<::cw::lib::String, ::cw::lib::String>> arg_scenarioinfo);
void _reset_nametable(std::map<::cw::lib::String, ::cw::lib::Value> arg_nametable);
std::tuple<::cw::lib::Integer, ::cw::lib::String> _get_namefromlist(::cw::lib::Integer arg_index, std::vector<std::shared_ptr<::cw::sprite::message::NameListItem>> arg_namelist);
::cw::lib::String _get_namefromtable(::cw::lib::String arg_nc, ::cw::lib::String arg_updatetype, std::map<::cw::lib::String, ::cw::lib::Value> arg_nametable, std::vector<std::shared_ptr<::cw::sprite::message::NameListItem>>& arg_namelist);
std::map<::cw::lib::String, ::cw::lib::Value> _create_nametable(::cw::lib::Integer arg_full, std::optional<::cw::lib::Value> arg_talker, bool arg_show_cardname);
std::optional<std::shared_ptr<::cw::character::Character>> _create_nametable_get_random();
std::tuple<std::optional<::cw::lib::String>, ::cw::lib::Integer> _get_stepvalue(::cw::lib::String arg_key, ::cw::lib::Integer arg_full, ::cw::lib::String arg_updatetype, std::map<::cw::lib::String, ::cw::lib::Value> arg_name_table, std::optional<std::vector<std::shared_ptr<::cw::sprite::message::NameListItem>>> arg_basenamelist, ::cw::lib::Integer arg_startindex, std::optional<std::set<::cw::lib::Integer>> arg_spcharinfo, std::optional<std::vector<std::shared_ptr<::cw::sprite::message::NameListItem>>> arg_namelist, ::cw::lib::Integer arg_namelistindex, std::optional<std::tuple<::cw::lib::String, ::cw::lib::String>> arg_scenarioinfo, ::cw::lib::Integer arg_stack, std::vector<std::shared_ptr<::cw::sprite::message::NameListItem>>* arg_out_namelist = nullptr, std::set<::cw::lib::Integer>* arg_out_spcharinfo = nullptr);
std::optional<std::shared_ptr<::cw::data::Step>> get_spstep(::cw::lib::String arg_name);
std::tuple<std::optional<std::shared_ptr<::cw::data::Step>>, ::cw::lib::Integer> _get_spstep(::cw::lib::String arg_name, ::cw::lib::Integer arg_full, ::cw::lib::String arg_updatetype, std::optional<std::vector<std::shared_ptr<::cw::sprite::message::NameListItem>>> arg_basenamelist, std::optional<std::vector<std::shared_ptr<::cw::sprite::message::NameListItem>>> arg_namelist, ::cw::lib::Integer arg_namelistindex, std::vector<std::shared_ptr<::cw::sprite::message::NameListItem>>* arg_out_namelist = nullptr);
::cw::lib::Integer _get_spstep_get_selectednum();
::cw::lib::Value _get_spstep_lambda_line_1719(::cw::lib::Value arg_pcard);
std::tuple<std::optional<::cw::lib::String>, ::cw::lib::Integer> _get_flagvalue(::cw::lib::String arg_key, ::cw::lib::Integer arg_full, ::cw::lib::String arg_updatetype, std::map<::cw::lib::String, ::cw::lib::Value> arg_name_table, std::optional<std::vector<std::shared_ptr<::cw::sprite::message::NameListItem>>> arg_basenamelist, ::cw::lib::Integer arg_startindex, std::optional<std::set<::cw::lib::Integer>> arg_spcharinfo, std::optional<std::vector<std::shared_ptr<::cw::sprite::message::NameListItem>>> arg_namelist, ::cw::lib::Integer arg_namelistindex, std::optional<std::tuple<::cw::lib::String, ::cw::lib::String>> arg_scenarioinfo, ::cw::lib::Integer arg_stack, std::vector<std::shared_ptr<::cw::sprite::message::NameListItem>>* arg_out_namelist = nullptr, std::set<::cw::lib::Integer>* arg_out_spcharinfo = nullptr);
std::tuple<std::optional<::cw::lib::String>, ::cw::lib::Integer> _get_variantvalue(::cw::lib::String arg_key, ::cw::lib::Integer arg_full, ::cw::lib::String arg_updatetype, std::map<::cw::lib::String, ::cw::lib::Value> arg_name_table, std::optional<std::vector<std::shared_ptr<::cw::sprite::message::NameListItem>>> arg_basenamelist, ::cw::lib::Integer arg_startindex, std::optional<std::set<::cw::lib::Integer>> arg_spcharinfo, std::optional<std::vector<std::shared_ptr<::cw::sprite::message::NameListItem>>> arg_namelist, ::cw::lib::Integer arg_namelistindex, std::optional<std::tuple<::cw::lib::String, ::cw::lib::String>> arg_scenarioinfo, ::cw::lib::Integer arg_stack, std::vector<std::shared_ptr<::cw::sprite::message::NameListItem>>* arg_out_namelist = nullptr, std::set<::cw::lib::Integer>* arg_out_spcharinfo = nullptr);
bool _is_differentscenario(std::optional<std::tuple<::cw::lib::String, ::cw::lib::String>> arg_scenarioinfo);
std::tuple<::cw::lib::String, std::set<::cw::lib::Integer>, std::vector<std::shared_ptr<::cw::sprite::message::NameListItem>>, ::cw::lib::Integer> _rpl_specialstr(::cw::lib::Integer arg_full, ::cw::lib::String arg_updatetype, ::cw::lib::String arg_s, std::map<::cw::lib::String, ::cw::lib::Value> arg_name_table, ::cw::lib::Callback arg_get_step, ::cw::lib::Callback arg_get_flag, ::cw::lib::Callback arg_get_variant, std::optional<std::vector<std::shared_ptr<::cw::sprite::message::NameListItem>>> arg_basenamelist, ::cw::lib::Integer arg_startindex, std::optional<std::set<::cw::lib::Integer>> arg_spcharinfo, std::optional<std::vector<std::shared_ptr<::cw::sprite::message::NameListItem>>> arg_namelist, ::cw::lib::Integer arg_namelistindex, std::optional<std::tuple<::cw::lib::String, ::cw::lib::String>> arg_scenarioinfo, ::cw::lib::Integer arg_stack);
std::tuple<::cw::lib::Integer, ::cw::lib::Integer> _rpl_specialstr_get_varvalue(::cw::lib::Callback arg_get, ::cw::lib::String arg_c, ::cw::lib::Integer arg_namelistindex);
::cw::lib::String get_messagelogtext(std::vector<::cw::lib::Value> arg_mwins, bool arg_lastline = true);
void update_scenariopath_for_log(::cw::lib::String arg_normpath, ::cw::lib::String arg_dst);
void update_scenariopath_for_spchars(::cw::lib::Value arg_restbl, ::cw::lib::String arg_normpath, ::cw::lib::String arg_dst);
void store_messagelogimage(::cw::lib::String arg_path, bool arg_can_loaded_scaledimage);
std::map<::cw::lib::Integer, std::shared_ptr<::cw::lib::Surface>> store_messagelogimage_load_with_scaled(std::optional<std::map<::cw::lib::Integer, std::shared_ptr<::cw::lib::Surface>>> arg_d, std::map<::cw::lib::Integer, std::shared_ptr<::cw::lib::Surface>> arg_scaledimagedict);
std::tuple<std::shared_ptr<::cw::lib::Surface>, bool> store_messagelogimage_load();
void main();
}

