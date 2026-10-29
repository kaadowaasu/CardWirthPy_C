#pragma once

#include "cwxeditor_src/lib/portable.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>

namespace cwxeditor::cwx::win32res {

enum class ResType : std::uint32_t {
    RT_CURSOR = 1U,
    RT_BITMAP = 2U,
    RT_ICON = 3U,
    RT_MENU = 4U,
    RT_DIALOG = 5U,
    RT_STRING = 6U,
    RT_FONTDIR = 7U,
    RT_FONT = 8U,
    RT_ACCELERATOR = 9U,
    RT_RCDATA = 10U,
    RT_MESSAGETABLE = 11U,
    RT_GROUP_CURSOR = 12U,
    RT_GROUP_ICON = 14U,
    RT_VERSION = 16U,
    RT_DLGINCLUDE = 17U,
    RT_PLUGPLAY = 19U,
    RT_VXD = 20U,
    RT_ANICURSOR = 21U,
    RT_ANIICON = 22U,
    RT_HTML = 23U,
    RT_MANIFEST = 24U,
};

class ResID final {
public:
    ResID() = default;
    explicit ResID(lib::String name);
    explicit ResID(std::uint32_t id) noexcept;
    explicit ResID(ResType id) noexcept;

    [[nodiscard]] bool nameIsString() const noexcept { return name_is_string_; }
    [[nodiscard]] const lib::String& name() const noexcept { return name_; }
    [[nodiscard]] std::uint32_t id() const noexcept { return id_; }
    [[nodiscard]] const wchar_t* lpcwstr() const;
    [[nodiscard]] std::size_t toHash() const noexcept;
    [[nodiscard]] bool opEquals(const ResID& res) const noexcept;
    [[nodiscard]] int opCmp(const ResID& res) const noexcept;
    [[nodiscard]] lib::String toString() const;

    friend bool operator==(const ResID& left, const ResID& right) noexcept {
        return left.opEquals(right);
    }
    friend bool operator!=(const ResID& left, const ResID& right) noexcept {
        return !left.opEquals(right);
    }
    friend bool operator<(const ResID& left, const ResID& right) noexcept {
        return left.opCmp(right) < 0;
    }

private:
    bool name_is_string_ = false;
    lib::String name_{};
    std::uint32_t id_ = 0;
    mutable std::wstring wide_name_{};
};

class Win32Res final {
public:
    Win32Res() = default;
    explicit Win32Res(const std::filesystem::path& file);
    ~Win32Res();

    Win32Res(const Win32Res& other);
    Win32Res& operator=(const Win32Res& other);
    Win32Res(Win32Res&& other) noexcept;
    Win32Res& operator=(Win32Res&& other) noexcept;

    void loadResModule(
        const std::filesystem::path& file,
        std::size_t maximum_file_bytes = 512U * 1024U * 1024U,
        std::size_t maximum_resources = 100000U);
    // win32res.dで使う表記を維持する。
    void laodResModule(const std::filesystem::path& file) {
        loadResModule(file);
    }
    void dispose() noexcept;

    [[nodiscard]] lib::Bytes getRCData(const ResID& type,
                                       const ResID& name) const;
    // 代替言語へ切り替えず、正確なPE言語を選ぶために追加した共通API。
    [[nodiscard]] lib::Bytes getRCData(const ResID& type, const ResID& name,
                                       std::uint32_t language) const;
    [[nodiscard]] lib::Bytes getBitmap(const ResID& name) const;
    /// DのEditorと同じバイト順・固定情報の位置で、 RT_VERSION/1
    /// からVS_FIXEDFILEINFOのFileVersionを読み取る。
    [[nodiscard]] std::optional<std::uint64_t> fileVersion() const;
    [[nodiscard]] static std::optional<std::uint64_t> decodeFileVersion(
        lib::ByteView version_resource) noexcept;
    [[nodiscard]] std::size_t size() const noexcept;

    // Dの生成された代入演算子を、通常のC++の
    // 代入演算子に加え、この名前でも公開する。
    Win32Res& opAssign(const Win32Res& other) { return operator=(other); }

private:
    using Languages = std::map<std::uint32_t, lib::Bytes>;
    std::map<ResID, std::map<ResID, Languages>> table_{};
    std::function<std::optional<std::uint32_t>(const ResID&, const ResID&)> language_selector_{};
};

}  // namespace cwxeditor::cwx::win32res
