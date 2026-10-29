#pragma once
#include "cw/lib/compat.hpp"
#include <variant>

namespace cw::skin::win32res {
using ResourceId = std::variant<::cw::lib::Integer, ::cw::lib::String>;
class ResTable;
class ResArray;
// TPF0 の文字列配列はPython実装上、型2/3の整数と型6の文字列が混在し得る。
// 注釈上のList[str]互換も残しつつ、実値を失わないValue配列を併設する。
using ResValue = std::variant<std::shared_ptr<ResArray>, std::vector<::cw::lib::Value>, std::vector<::cw::lib::String>, ::cw::lib::Integer,
                              ::cw::lib::String, bool, ::cw::lib::Bytes,
                              std::vector<::cw::lib::Bytes>, std::shared_ptr<ResTable>>;
class ResTable : public ::cw::lib::Object {
public:
    std::map<::cw::lib::Bytes, ResValue> m_table{};
    void initialize();
};
enum class ResArrayKind { Values, Strings, Bytes };
// 動的な Value の利用側は、find_res が受け付ける三種類の
// Python リストを共通境界で渡すために、このオブジェクトを使う。
class ResArray : public ::cw::lib::Object {
public:
    ResArrayKind m_kind{ResArrayKind::Values};
    std::vector<::cw::lib::Value> m_values{};
};
extern ::cw::lib::Value g_ResType;
class Win32Res : public ::cw::lib::Object {
public:
    Win32Res() = default;
    explicit Win32Res(::cw::lib::String fpath) { initialize(std::move(fpath)); }
    ~Win32Res() override { dispose(); }
    std::map<ResourceId, std::map<ResourceId, ::cw::lib::Bytes>> m_table{};
    std::uintptr_t m_winhandle{0};
    bool m_winapi{false};
    void initialize(::cw::lib::String fpath);
    void py_del();
    void laod_resmodule(::cw::lib::String fpath);
    ResourceId _res_name(const ::cw::lib::Bytes& base, ::cw::lib::Integer res_addr, ::cw::lib::Integer w1);
    void dispose();
    std::optional<::cw::lib::Bytes> lookup_rcdata(ResourceId valtype, ResourceId name) const;
    std::optional<::cw::lib::Bytes> lookup_rcdata(::cw::lib::Integer valtype, ResourceId name) const {
        return lookup_rcdata(ResourceId{valtype}, std::move(name));
    }
    // Python の WinAPI 経路は名前付きのリソース種別も
    // 受け入れる。ResourceId のオーバーロードを保ち、独
    // 自の文字列の種別を標準 RT_* 整数定数だけに制限しない。
    std::optional<::cw::lib::Bytes> get_rcdata(ResourceId valtype, ResourceId name) const;
    std::optional<::cw::lib::Bytes> get_rcdata(::cw::lib::Integer valtype, ResourceId name) const {
        return get_rcdata(ResourceId{valtype}, std::move(name));
    }
    std::optional<::cw::lib::Bytes> get_rcdata(::cw::lib::Integer valtype, ::cw::lib::Value name) const;
    std::optional<::cw::lib::Bytes> get_rcdata(::cw::lib::Value valtype, ::cw::lib::Value name) const;
    // Python はリソース名として UTF-8 のバイトも受け入れる。表のキーは String。
    std::optional<::cw::lib::Bytes> get_rcdata(::cw::lib::Integer valtype, ::cw::lib::Bytes bytes) const;
    template<class T>
    std::optional<::cw::lib::Bytes> get_rcdata(::cw::lib::Integer valtype, T name) const {
        return get_rcdata(valtype, ResourceId{std::move(name)});
    }
    std::optional<::cw::lib::Bytes> get_cursor(::cw::lib::Value number);
    std::optional<::cw::lib::Bytes> get_bitmap(::cw::lib::String name);
    std::optional<std::shared_ptr<ResTable>> get_tpf0form(::cw::lib::Bytes name);
};

inline constexpr ::cw::lib::Integer g_RT_CURSOR=1, g_RT_BITMAP=2, g_RT_ICON=3,
 g_RT_MENU=4, g_RT_DIALOG=5, g_RT_STRING=6, g_RT_FONTDIR=7, g_RT_FONT=8,
 g_RT_ACCELERATOR=9, g_RT_RCDATA=10, g_RT_MESSAGETABLE=11,
 g_RT_GROUP_CURSOR=12, g_RT_GROUP_ICON=14, g_RT_VERSION=16,
 g_RT_DLGINCLUDE=17, g_RT_PLUGPLAY=19, g_RT_VXD=20, g_RT_ANICURSOR=21,
 g_RT_ANIICON=22, g_RT_HTML=23, g_RT_MANIFEST=24;
extern bool g__winapi;

std::vector<::cw::lib::String> find_res(std::shared_ptr<ResTable>, std::vector<::cw::lib::Bytes>, std::vector<::cw::lib::String>);
std::vector<::cw::lib::Value> find_res(std::shared_ptr<ResTable>, std::vector<::cw::lib::Bytes>, std::vector<::cw::lib::Value>);
bool find_res_2(std::shared_ptr<ResTable>, std::vector<::cw::lib::Bytes>, bool);
::cw::lib::Integer find_res_3(std::shared_ptr<ResTable>, std::vector<::cw::lib::Bytes>, ::cw::lib::Integer);
::cw::lib::String find_res_4(std::shared_ptr<ResTable>, std::vector<::cw::lib::Bytes>, ::cw::lib::String);
::cw::lib::Bytes find_res_5(std::shared_ptr<ResTable>, std::vector<::cw::lib::Bytes>, ::cw::lib::Bytes);
std::vector<::cw::lib::Bytes> find_res_6(std::shared_ptr<ResTable>, std::vector<::cw::lib::Bytes>, std::vector<::cw::lib::Bytes>);
std::shared_ptr<ResTable> find_res_7(std::shared_ptr<ResTable>, std::vector<::cw::lib::Bytes>, std::shared_ptr<ResTable>);
::cw::lib::Value find_res_8(std::shared_ptr<ResTable>, std::vector<::cw::lib::Bytes>, ::cw::lib::Value);
void main();
}
