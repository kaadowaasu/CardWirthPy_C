#pragma once

// cw/argparser.pyに対
// 応する、コマンドライン引数の宣言。
#include "cw/lib/compat.hpp"
#include "cw/fwd.hpp"

namespace cw::argparser {
enum class ArgType { Boolean, Integer, String };
// 言語差: ネイティブの引数値は自身のリストを所有し、呼出し元の配列の寿命を借用しない。
// Pythonの参照共有とは異なるが、保存値・引数の解析・既定値の内容は同じにする。
using ArgValue = std::variant<bool, ::cw::lib::Integer, ::cw::lib::String,
    std::vector<::cw::lib::Integer>, std::vector<::cw::lib::String>>;

// プロセス引数はアプリのエントリーポイントからUTF-8で渡す
// 。 OS固有のUTF-16などはアプリ境界で一度だけ変換し、
// 解析処理にはOS別の分岐を持ち込まない。 SDL・Win32
// に依存しない値オブジェクトとし、デスクトップ・埋め込み・モ
// バイルで同じ解析処理を使う。 Pythonのsys.argv[1:]
// とArgParser::parse_args()に合わ
// せ、argumentsには実行ファイル名を含めない。
struct AppLaunchOptions {
    std::vector<::cw::lib::String> arguments{};

    [[nodiscard]] static AppLaunchOptions from_argv(int argc, char* const* argv);
};

// Engineが解析する前にコマンドライン引数を渡す。
// CRTのグローバル変数やLinuxの/procから取得
// する方法では、macOSなどの埋め込み環境で空になる。
void set_app_launch_options(AppLaunchOptions options);
[[nodiscard]] AppLaunchOptions app_launch_options();
void set_app_launch_options(int argc, char* const* argv);
// 渡されたargvの内容を返す。呼び出し元からの指定がなければ、従来のOS別
// の取得処理を使う。 Engine起動時の引数取得は、この入口に統一する。
[[nodiscard]] std::vector<::cw::lib::String> application_arguments();

class Arg : public ::cw::lib::Object {
public:
    Arg() = default;
    virtual ~Arg() = default;
    ::cw::lib::String m_arg{};
    ::cw::lib::String m_arg2{};
    std::optional<ArgValue> m_py_default{};
    ::cw::lib::String m_help{};
    std::optional<::cw::lib::String> m_metavar{};
    ::cw::lib::Integer m_nargs{};
    ArgType m_type{ArgType::String};
    void initialize(::cw::lib::String arg_arg, ArgType arg_argtype, ::cw::lib::Integer arg_nargs, ::cw::lib::String arg_helptext, ::cw::lib::String arg_arg2 = {}, std::optional<ArgValue> arg_py_default = std::nullopt, std::optional<::cw::lib::String> arg_metavar = std::nullopt);
    ArgValue eat(std::vector<::cw::lib::String>& arg_args);
    ArgValue parse(const ::cw::lib::String& arg_value) const;
    ::cw::lib::String get_help(::cw::lib::String arg_sep = ", ") const;
};

class ArgParser : public ::cw::lib::Object {
public:
    ArgParser() = default;
    virtual ~ArgParser() = default;
    ::cw::lib::String m_appname{};
    std::map<::cw::lib::String, std::shared_ptr<::cw::argparser::Arg>> m_args{};
    ::cw::lib::String m_desc{};
    std::vector<std::shared_ptr<::cw::argparser::Arg>> m_largs{};
    std::vector<::cw::lib::String> m_key_order{};
    void initialize(::cw::lib::String arg_appname = {}, ::cw::lib::String arg_description = {});
    void add_argument(::cw::lib::String arg_arg, ArgType arg_argtype, ::cw::lib::Integer arg_nargs, ::cw::lib::String arg_helptext, ::cw::lib::String arg_arg2 = {}, std::optional<ArgValue> arg_py_default = std::nullopt, std::optional<::cw::lib::String> arg_metavar = std::nullopt);
    std::optional<std::shared_ptr<::cw::argparser::ArgResult>> parse_args(std::optional<std::vector<::cw::lib::String>> arg_args = std::nullopt);
    void print_help();
};

class ArgResult : public ::cw::lib::Object {
public:
    ArgResult() = default;
    virtual ~ArgResult() = default;
    std::map<::cw::lib::String, bool> m__bool_values{};
    std::map<::cw::lib::String, ::cw::lib::Integer> m__int_values{};
    std::map<::cw::lib::String, std::vector<::cw::lib::Integer>> m__intlist_values{};
    std::map<::cw::lib::String, ::cw::lib::String> m__str_values{};
    std::map<::cw::lib::String, std::vector<::cw::lib::String>> m__strlist_values{};
    std::vector<::cw::lib::String> m_leftovers{};
    void initialize();
    ::cw::lib::Integer getint(::cw::lib::String arg_key);
    void setint(::cw::lib::String arg_key, ::cw::lib::Integer arg_value);
    ::cw::lib::String getstr(::cw::lib::String arg_key);
    void setstr(::cw::lib::String arg_key, ::cw::lib::String arg_value);
    bool getbool(::cw::lib::String arg_key);
    void setbool(::cw::lib::String arg_key, bool arg_value);
    // 言語差: リストの保存・読出しは値コピー。結果の内容を保ち、後の配列変更は共有しない。
    std::vector<::cw::lib::Integer> getintlist(::cw::lib::String arg_key);
    void setintlist(::cw::lib::String arg_key, std::vector<::cw::lib::Integer> arg_value);
    std::vector<::cw::lib::String> getstrlist(::cw::lib::String arg_key);
    void setstrlist(::cw::lib::String arg_key, std::vector<::cw::lib::String> arg_value);
};

void ArgParser_parse_args_put_value(std::shared_ptr<::cw::argparser::ArgResult> arg_r, ::cw::lib::Value arg_argtype, ::cw::lib::String arg_key, ::cw::lib::Value arg_val);
void main();
}

