#pragma once

// cw/__init__.pyの移植。スカラー値・パッケージのAPI
// はバックエンドに依存させず、 SDL/wxオブジェクトとの
// 接続にはcw/lib/compat.hppの互換型を使う。
#include "cw/lib/compat.hpp"
#include "cw/fwd.hpp"

#include <atomic>

namespace cw {

// C++のABIではpygame.RectをスカラーのValueで表せないため、
// _s_impl()/uns()が矩形値を受け渡す明示的なラッパーを用意する。
struct RectValue final : ::cw::lib::Object {
    RectValue() = default;
    explicit RectValue(::cw::lib::Rect value) : rect(value) {}
    ::cw::lib::Rect rect{};
};

extern ::cw::lib::String g_APP_NAME;
extern std::tuple<::cw::lib::Integer, ::cw::lib::String> g_APP_VERSION;
extern std::tuple<::cw::lib::Value, ::cw::lib::Value, ::cw::lib::Value, ::cw::lib::Value, ::cw::lib::Value> g_AREAS_SP;
extern std::tuple<::cw::lib::Integer> g_AREAS_TITLE;
extern std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> g_AREAS_TRADE;
extern std::tuple<::cw::lib::Value, ::cw::lib::Value, ::cw::lib::Value, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> g_AREAS_YADO;
extern ::cw::lib::Value g_AREA_BREAKUP;
extern ::cw::lib::Value g_AREA_CAMP;
extern ::cw::lib::Value g_AREA_TRADE1;
extern ::cw::lib::Value g_AREA_TRADE2;
extern ::cw::lib::Value g_AREA_TRADE3;
extern ::cw::lib::String g_DEFAULT_SOUNDFONT;
extern std::tuple<::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String> g_EXTS_IMG;
extern std::tuple<::cw::lib::String, ::cw::lib::String> g_EXTS_MIMG;
extern std::tuple<::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String> g_EXTS_MSC;
extern std::tuple<::cw::lib::String, ::cw::lib::String, ::cw::lib::String> g_EXTS_SND;
extern ::cw::lib::Integer g_FORCE_USEREVENT;
extern ::cw::lib::Integer g_HINT_AREA;
extern ::cw::lib::Integer g_HINT_CARD;
extern ::cw::lib::Integer g_HINT_MESSAGE;
extern ::cw::lib::Integer g_HINT_SCENARIO;
extern ::cw::lib::Value g_IDX_TREEEND;
extern ::cw::lib::Integer g_LAYER_BACKGROUND;
extern std::tuple<::cw::lib::Value, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> g_LAYER_BATTLE_START;
extern ::cw::lib::Integer g_LAYER_CLICKABLE_SPRITES;
extern ::cw::lib::Integer g_LAYER_FCARDS;
extern ::cw::lib::Value g_LAYER_FCARDS_T;
extern ::cw::lib::Integer g_LAYER_FRONT;
extern std::tuple<::cw::lib::Value, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> g_LAYER_FRONT_INUSECARD;
extern std::tuple<::cw::lib::Value, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> g_LAYER_FRONT_LIFEBAR;
extern ::cw::lib::Integer g_LAYER_JPY_TEMPORAL;
extern std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> g_LAYER_LOG;
extern std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> g_LAYER_LOG_BAR;
extern std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> g_LAYER_LOG_CURTAIN;
extern std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> g_LAYER_LOG_PAGE;
extern std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> g_LAYER_LOG_SCROLLBAR;
extern ::cw::lib::Integer g_LAYER_MAX;
extern ::cw::lib::Integer g_LAYER_MCARDS;
extern ::cw::lib::Integer g_LAYER_MCARDS_120;
extern std::tuple<::cw::lib::Integer, ::cw::lib::Value, ::cw::lib::Integer, ::cw::lib::Integer> g_LAYER_MESSAGE;
extern ::cw::lib::Integer g_LAYER_NUMBER_OF_CARDS;
extern ::cw::lib::Integer g_LAYER_PCARDS;
extern std::tuple<::cw::lib::Integer, ::cw::lib::Value, ::cw::lib::Integer, ::cw::lib::Integer> g_LAYER_SELECTIONBAR_1;
extern std::tuple<::cw::lib::Integer, ::cw::lib::Value, ::cw::lib::Integer, ::cw::lib::Integer> g_LAYER_SELECTIONBAR_2;
extern ::cw::lib::Value g_LAYER_SPBACKGROUND;
extern std::tuple<::cw::lib::Integer, ::cw::lib::Value, ::cw::lib::Integer, ::cw::lib::Integer> g_LAYER_SPMESSAGE;
extern std::tuple<::cw::lib::Integer, ::cw::lib::Value, ::cw::lib::Integer, ::cw::lib::Integer> g_LAYER_SPSELECTIONBAR_1;
extern std::tuple<::cw::lib::Integer, ::cw::lib::Value, ::cw::lib::Integer, ::cw::lib::Integer> g_LAYER_SPSELECTIONBAR_2;
extern ::cw::lib::Integer g_LAYER_SP_LAYER;
extern std::tuple<::cw::lib::Value, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> g_LAYER_TARGET_ARROW;
extern ::cw::lib::Integer g_LAYER_TITLE;
extern std::tuple<::cw::lib::Value, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> g_LAYER_TRANSITION;
extern ::cw::lib::Integer g_LIMIT_RECURSE;
extern ::cw::lib::Integer g_LINDEX_MAX;
extern ::cw::lib::Integer g_LOG_SEPARATOR_LEN_LONG;
extern ::cw::lib::Integer g_LOG_SEPARATOR_LEN_MIDDLE;
extern ::cw::lib::Integer g_LOG_SEPARATOR_LEN_SHORT;
extern ::cw::lib::Integer g_LTYPE2_MAX;
extern ::cw::lib::Integer g_LTYPE_BACKGROUND;
extern ::cw::lib::Integer g_LTYPE_BACKGROUND_CURTAIN;
extern ::cw::lib::Integer g_LTYPE_FCARDS;
extern ::cw::lib::Integer g_LTYPE_MAX;
extern ::cw::lib::Integer g_LTYPE_MCARDS;
extern ::cw::lib::Integer g_LTYPE_MESSAGE;
extern ::cw::lib::Integer g_LTYPE_PCARDS;
extern ::cw::lib::Integer g_LTYPE_SPMCARDS;
extern ::cw::lib::Integer g_LTYPE_SPMESSAGE;
extern ::cw::lib::String g_MBCS;
extern ::cw::lib::Integer g_M_IMG;
extern ::cw::lib::Integer g_M_MSC;
extern ::cw::lib::Integer g_M_SND;
extern ::cw::lib::Value g_OPTIONS;
extern ::cw::lib::Integer g_POCKET_BEAST;
extern ::cw::lib::Integer g_POCKET_ITEM;
extern ::cw::lib::Integer g_POCKET_PERSONAL;
extern ::cw::lib::Integer g_POCKET_SKILL;
extern std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> g_RECT_STATUSBAR;
extern ::cw::lib::Value g_RESCALE_QUALITY;
extern std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> g_SCALE_LIST;
extern std::tuple<::cw::lib::Integer, ::cw::lib::Integer> g_SIZE_AREA;
extern std::tuple<::cw::lib::Integer, ::cw::lib::Integer> g_SIZE_BILL;
extern std::tuple<::cw::lib::Integer, ::cw::lib::Integer> g_SIZE_BOOK;
extern std::tuple<::cw::lib::Integer, ::cw::lib::Integer> g_SIZE_CARDIMAGE;
extern std::tuple<::cw::lib::Integer, ::cw::lib::Integer> g_SIZE_GAME;
extern std::tuple<::cw::lib::Integer, ::cw::lib::Integer> g_SIZE_SCR;
extern ::cw::lib::Integer g_SKIN_AREAS_MAX;
extern ::cw::lib::Integer g_SKIN_AREAS_MIN;
extern std::vector<::cw::lib::String> g_SKIN_CONV_ARGS;
std::vector<::cw::lib::String> skin_conversion_arguments(const std::vector<::cw::lib::String>& leftovers);
extern std::tuple<::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String> g_SUPPORTED_SKIN;
extern std::tuple<::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String> g_SUPPORTED_WSN;
extern ::cw::lib::Value g_Scalable;
extern ::cw::lib::Real g_UP_SCR;
extern ::cw::lib::Real g_UP_WIN;
extern ::cw::lib::Real g_UP_WIN_M;
extern std::vector<::cw::lib::String> g_py_all;
extern std::shared_ptr<::cw::argparser::ArgParser> g__argparser;
extern ::cw::lib::Value g__options;
extern std::shared_ptr<::cw::thread::CWPy> g_cwpy;
extern ::cw::lib::Integer g_dpi_level;
extern ::cw::lib::String g_exepath;
extern ::cw::lib::String g_filesystem_encoding;
extern std::shared_ptr<::cw::util::FileSync> g_fsync;
extern std::atomic_bool g_quit_app;
extern ::cw::lib::Value g_tempdir;
extern ::cw::lib::String g_tempdir_init;

::cw::lib::Integer layer_val(std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> arg_tlayer);
void add_layer(std::shared_ptr<::cw::lib::Object> arg_grp, const ::cw::lib::Arguments& arg_sprites, ::cw::lib::Integer arg_layer);
::cw::lib::Value wins(::cw::lib::Value arg_num);
::cw::lib::Value s(::cw::lib::Value arg_num);
::cw::lib::Value scr2win_s(::cw::lib::Value arg_num);
::cw::lib::Value win2scr_s(::cw::lib::Value arg_num);
::cw::lib::Value scr2mwin_s(::cw::lib::Value arg_num);
::cw::lib::Value mwin2scr_s(::cw::lib::Value arg_num);
::cw::lib::Value _s_impl(::cw::lib::Value arg_num, ::cw::lib::Real arg_up_scr);
::cw::lib::Value uns(::cw::lib::Value arg_num);
::cw::lib::Value ppis(::cw::lib::Value arg_num);
void main();
}

