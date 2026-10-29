#include "cwxeditor_src/core/cwx/win32res.hpp"
#include "cwxeditor_src/core/cwx/win32res_test_access.hpp"
#include "cwxeditor_src/core/cwx/enumutils.hpp"

#include <algorithm>
#include <fstream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#undef RT_CURSOR
#undef RT_BITMAP
#undef RT_ICON
#undef RT_MENU
#undef RT_DIALOG
#undef RT_STRING
#undef RT_FONTDIR
#undef RT_FONT
#undef RT_ACCELERATOR
#undef RT_RCDATA
#undef RT_MESSAGETABLE
#undef RT_GROUP_CURSOR
#undef RT_GROUP_ICON
#undef RT_VERSION
#undef RT_DLGINCLUDE
#undef RT_PLUGPLAY
#undef RT_VXD
#undef RT_ANICURSOR
#undef RT_ANIICON
#undef RT_HTML
#undef RT_MANIFEST

namespace {
constexpr DWORD load_library_as_datafile = 0x2U;
constexpr DWORD load_with_altered_search_path = 0x8U;
}
#endif

namespace cwxeditor::cwx::win32res {
namespace {

using Byte = std::uint8_t;
using ByteView = lib::ByteView;

lib::Bytes resourceBytes(const lib::Bytes& bytes) {
    // 存在する空項目について、全バックエンドでDのWindowsのSizeofResourceの
    // enforceに合わせる。存在しない場合は、引き続きgetRCDataから空の値を返す。
    if (bytes.empty()) throw std::runtime_error("resource has zero size");
    return bytes;
}

[[nodiscard]] bool range(ByteView data, std::size_t offset,
                          std::size_t count) noexcept {
    return offset <= data.size() && count <= data.size() - offset;
}

void requireRange(ByteView data, std::size_t offset, std::size_t count,
                  const char* message) {
    if (!range(data, offset, count)) throw std::runtime_error(message);
}

// Dの非公開のle補助処理は、このモジュールではushort/uintだけに使う。DのBigEndian
// 分岐は符号化を逆にするが、ここでは全ホストでPEファイルのリトルエンディアン
// を保つ。Dの範囲検査付き配列アクセスは、ネイティブの範囲エラーに対応させる。
[[nodiscard]] std::uint16_t le16(ByteView data, std::size_t offset) {
    requireRange(data, offset, 2U, "truncated PE resource");
    return static_cast<std::uint16_t>(data[offset]) |
           static_cast<std::uint16_t>(data[offset + 1U] << 8U);
}

[[nodiscard]] std::uint32_t le32(ByteView data, std::size_t offset) {
    requireRange(data, offset, 4U, "truncated PE resource");
    return static_cast<std::uint32_t>(data[offset]) |
           (static_cast<std::uint32_t>(data[offset + 1U]) << 8U) |
           (static_cast<std::uint32_t>(data[offset + 2U]) << 16U) |
           (static_cast<std::uint32_t>(data[offset + 3U]) << 24U);
}

void append16(lib::Bytes& data, std::uint16_t value) {
    // 読み込みと同様に、ビッグエンディアンのホストでもPE出力はリトル
    // エンディアンを維持する。Dのホスト依存の符号化分岐は再現しない。
    data.push_back(static_cast<Byte>(value));
    data.push_back(static_cast<Byte>(value >> 8U));
}

void append32(lib::Bytes& data, std::uint32_t value) {
    data.push_back(static_cast<Byte>(value));
    data.push_back(static_cast<Byte>(value >> 8U));
    data.push_back(static_cast<Byte>(value >> 16U));
    data.push_back(static_cast<Byte>(value >> 24U));
}

[[nodiscard]] std::size_t checkedAdd(std::size_t left, std::size_t right,
                                     const char* message) {
    if (right > std::numeric_limits<std::size_t>::max() - left) {
        throw std::runtime_error(message);
    }
    return left + right;
}

[[nodiscard]] std::size_t checkedMultiply(std::size_t left,
                                           std::size_t right,
                                           const char* message) {
    if (right != 0U && left > std::numeric_limits<std::size_t>::max() / right) {
        throw std::runtime_error(message);
    }
    return left * right;
}

[[nodiscard]] lib::Bytes readFile(const std::filesystem::path& file,
                                  std::size_t maximum_file_bytes) {
    std::error_code error;
    const auto size = std::filesystem::file_size(file, error);
    if (error) throw std::runtime_error("could not stat PE file");
    if (size > maximum_file_bytes) {
        throw std::runtime_error("PE file exceeds configured limit");
    }
    if (size > static_cast<std::uintmax_t>(
                   std::numeric_limits<std::size_t>::max()) ||
        size > static_cast<std::uintmax_t>(
                   std::numeric_limits<std::streamsize>::max())) {
        throw std::runtime_error("PE file is too large");
    }

    lib::Bytes data(static_cast<std::size_t>(size));
    std::ifstream input(file, std::ios::binary);
    if (!input) throw std::runtime_error("could not open PE file");
    if (!data.empty()) {
        input.read(reinterpret_cast<char*>(data.data()),
                   static_cast<std::streamsize>(data.size()));
        if (input.gcount() != static_cast<std::streamsize>(data.size())) {
            throw std::runtime_error("could not read PE file");
        }
    }
    return data;
}

void appendUtf8(lib::String& output, std::uint32_t code_point) {
    if (code_point <= 0x7fU) {
        output.push_back(static_cast<char>(code_point));
    } else if (code_point <= 0x7ffU) {
        output.push_back(static_cast<char>(0xc0U | (code_point >> 6U)));
        output.push_back(static_cast<char>(0x80U | (code_point & 0x3fU)));
    } else if (code_point <= 0xffffU) {
        output.push_back(static_cast<char>(0xe0U | (code_point >> 12U)));
        output.push_back(static_cast<char>(
            0x80U | ((code_point >> 6U) & 0x3fU)));
        output.push_back(static_cast<char>(0x80U | (code_point & 0x3fU)));
    } else {
        output.push_back(static_cast<char>(0xf0U | (code_point >> 18U)));
        output.push_back(static_cast<char>(
            0x80U | ((code_point >> 12U) & 0x3fU)));
        output.push_back(static_cast<char>(
            0x80U | ((code_point >> 6U) & 0x3fU)));
        output.push_back(static_cast<char>(0x80U | (code_point & 0x3fU)));
    }
}

[[nodiscard]] lib::String utf16Name(ByteView data, std::size_t offset) {
    const auto length = le16(data, offset);
    offset = checkedAdd(offset, 2U, "truncated PE resource name");
    const auto byte_count = checkedMultiply(
        static_cast<std::size_t>(length), 2U, "PE resource name is too large");
    requireRange(data, offset, byte_count, "truncated PE resource name");

    std::u16string units;
    units.reserve(length);
    for (std::size_t index = 0; index < length; ++index) {
        units.push_back(static_cast<char16_t>(le16(data, offset + index * 2U)));
    }
    lib::String output;
    output.reserve(byte_count);
    for (const auto codePoint : enumutils::toUtf32(std::u16string_view(units)))
        appendUtf8(output, codePoint);
    return output;
}

[[nodiscard]] std::wstring utf8ToWide(lib::StringView input) {
    // DのwcharはUTF-16。wchar_tはネイティブAPIの境界で、32ビ
    // ットの場合もある。同じ厳密な基本処理で復号してから、コード単位幅を適応させる。
    std::wstring output;
    output.reserve(input.size());
    for (std::size_t index = 0; index < input.size();) {
        const auto scalar = enumutils::decodeUtf8Scalar(input, index);
        index = scalar.next;
        const auto code_point = scalar.value;
        if constexpr (sizeof(wchar_t) == 2U) {
            if (code_point <= 0xffffU) {
                output.push_back(static_cast<wchar_t>(code_point));
            } else {
                const auto value = code_point - 0x10000U;
                output.push_back(static_cast<wchar_t>(
                    0xd800U + (value >> 10U)));
                output.push_back(static_cast<wchar_t>(
                    0xdc00U + (value & 0x3ffU)));
            }
        } else {
            output.push_back(static_cast<wchar_t>(code_point));
        }
    }
    return output;
}

[[nodiscard]] const char* resTypeName(std::uint32_t id) noexcept {
    switch (static_cast<ResType>(id)) {
    case ResType::RT_CURSOR: return "RT_CURSOR";
    case ResType::RT_BITMAP: return "RT_BITMAP";
    case ResType::RT_ICON: return "RT_ICON";
    case ResType::RT_MENU: return "RT_MENU";
    case ResType::RT_DIALOG: return "RT_DIALOG";
    case ResType::RT_STRING: return "RT_STRING";
    case ResType::RT_FONTDIR: return "RT_FONTDIR";
    case ResType::RT_FONT: return "RT_FONT";
    case ResType::RT_ACCELERATOR: return "RT_ACCELERATOR";
    case ResType::RT_RCDATA: return "RT_RCDATA";
    case ResType::RT_MESSAGETABLE: return "RT_MESSAGETABLE";
    case ResType::RT_GROUP_CURSOR: return "RT_GROUP_CURSOR";
    case ResType::RT_GROUP_ICON: return "RT_GROUP_ICON";
    case ResType::RT_VERSION: return "RT_VERSION";
    case ResType::RT_DLGINCLUDE: return "RT_DLGINCLUDE";
    case ResType::RT_PLUGPLAY: return "RT_PLUGPLAY";
    case ResType::RT_VXD: return "RT_VXD";
    case ResType::RT_ANICURSOR: return "RT_ANICURSOR";
    case ResType::RT_ANIICON: return "RT_ANIICON";
    case ResType::RT_HTML: return "RT_HTML";
    case ResType::RT_MANIFEST: return "RT_MANIFEST";
    }
    return nullptr;
}

struct Section {
    std::uint32_t virtual_address = 0;
    std::uint32_t virtual_size = 0;
    std::uint32_t raw_address = 0;
    std::uint32_t raw_size = 0;
};

[[nodiscard]] std::size_t rvaOffset(std::uint32_t rva,
                                     const std::vector<Section>& sections,
                                     std::size_t file_size) {
    for (const auto& section : sections) {
        const auto span = std::max(section.virtual_size, section.raw_size);
        if (rva < section.virtual_address ||
            static_cast<std::uint64_t>(rva) - section.virtual_address >= span) {
            continue;
        }
        const auto delta = rva - section.virtual_address;
        if (delta >= section.raw_size) {
            throw std::runtime_error("PE RVA points into zero-filled section data");
        }
        const auto offset = checkedAdd(
            static_cast<std::size_t>(section.raw_address), delta,
            "PE section offset overflow");
        if (offset > file_size) {
            throw std::runtime_error("PE RVA points outside the file");
        }
        return offset;
    }
    throw std::runtime_error("PE resource RVA is not mapped");
}

struct DirectoryEntry {
    std::uint32_t name = 0;
    std::uint32_t value = 0;
};

[[nodiscard]] std::vector<DirectoryEntry> directoryEntries(
    ByteView data, std::size_t resource_base, std::uint32_t relative,
    std::size_t maximum_entries) {
    const auto position = checkedAdd(
        resource_base, relative, "PE resource directory offset overflow");
    requireRange(data, position, 16U, "truncated PE resource directory");
    const auto count = static_cast<std::size_t>(le16(data, position + 12U)) +
                       static_cast<std::size_t>(le16(data, position + 14U));
    if (count > maximum_entries) {
        throw std::runtime_error("PE resource directory is too large");
    }
    const auto bytes = checkedMultiply(
        count, 8U, "PE resource directory size overflow");
    const auto entries = checkedAdd(
        position, 16U, "PE resource directory offset overflow");
    requireRange(data, entries, bytes, "truncated PE resource directory");

    std::vector<DirectoryEntry> result;
    result.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        const auto entry = entries + index * 8U;
        result.push_back({le32(data, entry), le32(data, entry + 4U)});
    }
    return result;
}

[[nodiscard]] ResID resourceId(ByteView data, std::size_t resource_base,
                                std::uint32_t raw) {
    if ((raw & 0x80000000U) == 0U) return ResID(raw);
    // Dのuintの折り返しではなく、ネイティブの検査付き位置を使う。壊れ
    // たPEの名前位置が、ファイルの前方の無関係な領域を指してはならない。
    const auto offset = checkedAdd(
        resource_base, raw & 0x7fffffffU, "PE resource name offset overflow");
    return ResID(utf16Name(data, offset));
}

#ifdef _WIN32
struct ResourceLanguageSelection {
    HRSRC resource{};
    std::uint32_t language{};
    bool found{};
};
BOOL CALLBACK identifyResourceLanguage(HMODULE module, LPCWSTR type, LPCWSTR name,
                                      WORD language, LONG_PTR context) {
    auto& selected = *reinterpret_cast<ResourceLanguageSelection*>(context);
    if (FindResourceExW(module, type, name, language) == selected.resource) {
        selected.language = language;
        selected.found = true;
        return FALSE;
    }
    return TRUE;
}
#endif

}  // namespace

ResID::ResID(lib::String name)
    : name_is_string_(true), name_(std::move(name)), id_(0x80000000U) {}

ResID::ResID(std::uint32_t id) noexcept : id_(id) {}

ResID::ResID(ResType id) noexcept
    : id_(static_cast<std::uint32_t>(id)) {}

const wchar_t* ResID::lpcwstr() const {
    if (name_is_string_) {
        // API呼び出し中にポインターを有効に保つため、ネイティブ文字列を保持する
        // 。DのtoUTFzの領域はGCが所有する。不変の名前なので再取得は不要。
        if (wide_name_.empty() && !name_.empty()) {
            wide_name_ = utf8ToWide(name_);
        }
        return wide_name_.c_str();
    }
    return reinterpret_cast<const wchar_t*>(
        static_cast<std::uintptr_t>(id_));
}

std::size_t ResID::toHash() const noexcept {
    std::size_t hash = 0;
    for (const auto character : name_) {
        hash = hash * 37U + static_cast<unsigned char>(character);
    }
    hash = hash * 37U + id_;
    return hash;
}

bool ResID::opEquals(const ResID& res) const noexcept {
    return name_is_string_ == res.name_is_string_ && name_ == res.name_ &&
           id_ == res.id_;
}

int ResID::opCmp(const ResID& res) const noexcept {
    if (name_is_string_ == res.name_is_string_) {
        if (name_is_string_) {
            if (name_ < res.name_) return -1;
            if (name_ > res.name_) return 1;
        } else {
            // Dの数値のopCmpは常に0を返す。C++の順序付きmapでは
            // 数値キーを区別しないと、無関係な資源IDを上書きしてしまう。
            if (id_ < res.id_) return -1;
            if (id_ > res.id_) return 1;
        }
        return 0;
    }
    // 数値IDと名前付きIDは別の範囲に属する。追加のC++ uint32 ID
    // APIの0x80000000（内部では名前付きIDの番兵）も同様に区別する。
    return name_is_string_ ? 1 : -1;
}

lib::String ResID::toString() const {
    if (name_is_string_) return name_;
    if (id_ >= 1U && id_ <= 24U) {
        const auto* name = resTypeName(id_);
        return lib::String(name ? name : std::to_string(id_)) + "(" +
               std::to_string(id_) + ")";
    }
    return std::to_string(id_);
}

Win32Res::Win32Res(const std::filesystem::path& file) {
    loadResModule(file);
}

Win32Res::~Win32Res() { dispose(); }

Win32Res::Win32Res(const Win32Res& other) = default;
Win32Res& Win32Res::operator=(const Win32Res& other) = default;
Win32Res::Win32Res(Win32Res&& other) noexcept = default;
Win32Res& Win32Res::operator=(Win32Res&& other) noexcept = default;

void Win32Res::loadResModule(const std::filesystem::path& file,
                             std::size_t maximum_file_bytes,
                             std::size_t maximum_resources) {
    dispose();

    // Dのプラットフォーム依存の保存方式と異なり、全プラットフォームで同じ範
    // 囲検査済みのPEデータを所有する。ネイティブハンドルはWindows
    // の言語選択の基本処理だけに使い、オブジェクトの所有管理には使わない。
    const auto file_data = readFile(file, maximum_file_bytes);
    const ByteView data(file_data.data(), file_data.size());
    requireRange(data, 0, 0x40U, "not a PE file");
    if (data[0] != 'M' || data[1] != 'Z') {
        throw std::runtime_error("not a PE file");
    }

    const auto pe_offset = static_cast<std::size_t>(le32(data, 0x3cU));
    requireRange(data, pe_offset, 24U, "invalid PE header");
    if (data[pe_offset] != 'P' || data[pe_offset + 1U] != 'E' ||
        data[pe_offset + 2U] != 0 || data[pe_offset + 3U] != 0) {
        throw std::runtime_error("invalid PE header");
    }

    const auto section_count = static_cast<std::size_t>(le16(data, pe_offset + 6U));
    const auto optional_size = static_cast<std::size_t>(le16(data, pe_offset + 20U));
    const auto optional = checkedAdd(pe_offset, 24U, "PE header overflow");
    requireRange(data, optional, optional_size, "truncated PE optional header");
    const auto magic = le16(data, optional);
    std::size_t directory_offset = 0;
    if (magic == 0x10bU) {
        directory_offset = checkedAdd(optional, 96U,
                                      "PE resource directory offset overflow");
    } else if (magic == 0x20bU) {
        directory_offset = checkedAdd(optional, 112U,
                                      "PE resource directory offset overflow");
    } else {
        throw std::runtime_error("unsupported PE optional header");
    }
    const auto optional_end = checkedAdd(optional, optional_size,
                                         "PE optional header overflow");
    if (directory_offset > optional_end ||
        optional_end - directory_offset < 24U) {
        throw std::runtime_error("PE has no resource directory");
    }

    const auto resource_rva = le32(data, directory_offset + 16U);

    const auto section_table = optional_end;
    const auto section_bytes = checkedMultiply(
        section_count, 40U, "PE section table size overflow");
    requireRange(data, section_table, section_bytes,
                 "truncated PE section table");
    std::vector<Section> sections;
    std::optional<std::size_t> resource_section;
    sections.reserve(section_count);
    for (std::size_t index = 0; index < section_count; ++index) {
        const auto section = section_table + index * 40U;
        if (!resource_section &&
            (std::string_view(reinterpret_cast<const char*>(data.data() + section), 5) == ".rsrc" ||
             le32(data, section + 12U) == resource_rva))
            resource_section = index;
        sections.push_back({le32(data, section + 12U),
                            le32(data, section + 8U),
                            le32(data, section + 20U),
                            le32(data, section + 16U)});
    }

    if (!resource_section) throw std::runtime_error("PE has no resource section");
    const auto resource_base = rvaOffset(sections[*resource_section].virtual_address, sections, file_data.size());
    const auto idAt = [&data, resource_base](std::uint32_t raw) {
        return resourceId(data, resource_base, raw);
    };

    std::size_t resource_count = 0;
    const auto type_entries = directoryEntries(
        data, resource_base, 0U, maximum_resources);
    for (const auto& type_entry : type_entries) {
        if ((type_entry.value & 0x80000000U) == 0U) {
            throw std::runtime_error("invalid PE resource type directory");
        }
        const auto type = idAt(type_entry.name);
        const auto name_entries = directoryEntries(
            data, resource_base, type_entry.value & 0x7fffffffU,
            maximum_resources);
        for (const auto& name_entry : name_entries) {
            if ((name_entry.value & 0x80000000U) == 0U) {
                throw std::runtime_error("invalid PE resource name directory");
            }
            const auto name = idAt(name_entry.name);
            const auto languages = directoryEntries(
                data, resource_base, name_entry.value & 0x7fffffffU,
                maximum_resources);
            // DのWindows以外での単一言語の制約を、有効な多言
            // 語のWindows PEへ広げず、全言語を保持する。
            for (const auto& language : languages) {
                if ((language.name & 0x80000000U) != 0U)
                    throw std::runtime_error("invalid PE resource language ID");
                const auto leaf = language.value;
                if ((leaf & 0x80000000U) != 0U) {
                    throw std::runtime_error("invalid PE resource data entry");
                }
                const auto data_entry = checkedAdd(
                    resource_base, leaf, "PE resource data offset overflow");
                requireRange(data, data_entry, 16U,
                             "truncated PE resource data entry");
                const auto data_offset = rvaOffset(
                    le32(data, data_entry), sections, file_data.size());
                const auto data_size = static_cast<std::size_t>(
                    le32(data, data_entry + 4U));
                requireRange(data, data_offset, data_size,
                             "truncated PE resource data");

                table_[type][name][language.name] = lib::Bytes(
                    file_data.begin() + static_cast<std::ptrdiff_t>(data_offset),
                    file_data.begin() +
                        static_cast<std::ptrdiff_t>(data_offset + data_size));
                ++resource_count;
                if (resource_count > maximum_resources) {
                    throw std::runtime_error("PE resource count exceeds limit");
                }
            }

        }
    }
#ifdef _WIN32
    // FindResourceの仕様上のスレッド言語・MUIの代替
    // 処理はOSの機能。共通規則でロケールや代替順を推測しない。
    struct ResourceModule {
        HMODULE value{};
        ~ResourceModule() { if (value) FreeLibrary(value); }
    };
    auto module = std::make_shared<ResourceModule>();
    module->value = LoadLibraryExW(file.c_str(), nullptr,
        load_library_as_datafile | load_with_altered_search_path);
    if (module->value) {
        // コピー間では、このネイティブの選択処理の寿命だけを共有する。全
        // 資源のバイト列は共通の値の表が所有する。選択処理は、その後のス
        // レッド言語の変更にもFindResourceの動作で応答する。
        language_selector_ = [module](const ResID& type, const ResID& name)
            -> std::optional<std::uint32_t> {
            ResourceLanguageSelection selected{FindResourceW(module->value, name.lpcwstr(), type.lpcwstr())};
            if (!selected.resource) return std::nullopt;
            EnumResourceLanguagesW(module->value, type.lpcwstr(), name.lpcwstr(),
                identifyResourceLanguage, reinterpret_cast<LONG_PTR>(&selected));
            if (!selected.found) throw std::runtime_error("could not identify selected PE resource language");
            return selected.language;
        };
    }
#endif

}

void Win32Res::dispose() noexcept {
    table_.clear();
    language_selector_ = {};
}

lib::Bytes Win32Res::getRCData(const ResID& type, const ResID& name) const {
    const auto type_it = table_.find(type);
    if (type_it == table_.end()) return {};
    const auto name_it = type_it->second.find(name);
    if (name_it == type_it->second.end()) return {};
    const auto& languages = name_it->second;
    if (languages.empty()) return {};
    if (language_selector_) {
        const auto language = language_selector_(type, name);
        if (!language) return {}; // ネイティブのFindResourceが資源の不在を報告する。
        const auto chosen = languages.find(*language);
        if (chosen == languages.end()) throw std::runtime_error("selected resource language is absent from PE snapshot");
        return resourceBytes(chosen->second);
    }
    // 追加の共通多言語対応では、OSに資源選択機能がなけ
    // れば、中立言語、最初のIDの順に一定の代替を選ぶ。
    if (const auto neutral = languages.find(0U); neutral != languages.end()) return resourceBytes(neutral->second);
    return resourceBytes(languages.begin()->second);
}

lib::Bytes Win32Res::getRCData(const ResID& type, const ResID& name,
                              const std::uint32_t language) const {
    const auto types = table_.find(type);
    if (types == table_.end()) return {};
    const auto names = types->second.find(name);
    if (names == types->second.end()) return {};
    const auto data = names->second.find(language);
    return data == names->second.end() ? lib::Bytes{} : resourceBytes(data->second);
}

std::optional<std::uint64_t> Win32Res::decodeFileVersion(
    const lib::ByteView version_resource) noexcept {
    // partyhistory.dはバイト位置48へ移動し、dwFileVersionMS
    // の下位・上位半分、続いてdwFileVersionLSの下位・上位半分を読む。
    if (version_resource.size() < 56U) return std::nullopt;
    const auto read16 = [&](const std::size_t offset) -> std::uint16_t {
        return static_cast<std::uint16_t>(version_resource[offset]) |
               static_cast<std::uint16_t>(version_resource[offset + 1U] << 8U);
    };
    const auto minor = read16(48U);
    const auto major = read16(50U);
    const auto build = read16(52U);
    const auto patch = read16(54U);
    return (static_cast<std::uint64_t>(major) << 24U) |
           (static_cast<std::uint64_t>(minor) << 16U) |
           (static_cast<std::uint64_t>(patch) << 8U) |
           static_cast<std::uint64_t>(build);
}

std::optional<std::uint64_t> Win32Res::fileVersion() const {
    const auto data = getRCData(ResID(ResType::RT_VERSION), ResID(1U));
    if (data.empty()) return std::nullopt;
    return decodeFileVersion(lib::ByteView(data.data(), data.size()));
}

lib::Bytes Win32Res::getBitmap(const ResID& name) const {
    constexpr std::size_t bitmap_file_header_size = 14U;
    constexpr std::size_t rgb_quad_size = 4U;

    const auto data = getRCData(ResID(ResType::RT_BITMAP), name);
    if (data.empty()) return {};
    const ByteView body(data.data(), data.size());
    requireRange(body, 0, 40U, "truncated bitmap resource");
    auto header_size = le32(body, 0U);
    if (header_size != 40U) {
        throw std::runtime_error("unsupported bitmap resource header");
    }
    const auto bit_count = le16(body, 14U);
    const auto color_count = le32(body, 32U);
    if (color_count == 0U) {
        switch (bit_count) {
        case 1U: header_size += rgb_quad_size * 0x02U; break;
        case 4U: header_size += rgb_quad_size * 0x10U; break;
        case 8U: header_size += rgb_quad_size * 0x100U; break;
        default:
            throw std::runtime_error("bitmap resource has no palette count");
        }
    } else {
        const auto palette_bytes = static_cast<std::uint64_t>(color_count) *
                                   rgb_quad_size;
        if (palette_bytes > std::numeric_limits<std::uint32_t>::max() -
                                header_size) {
            throw std::runtime_error("bitmap resource palette is too large");
        }
        header_size += static_cast<std::uint32_t>(palette_bytes);
    }
    const auto pixel_offset = static_cast<std::uint64_t>(
        bitmap_file_header_size) + header_size;
    const auto file_size = static_cast<std::uint64_t>(
        bitmap_file_header_size) + data.size();
    if (pixel_offset > std::numeric_limits<std::uint32_t>::max() ||
        file_size > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error("bitmap resource is too large");
    }

    lib::Bytes result;
    result.reserve(static_cast<std::size_t>(file_size));
    result.push_back('B');
    result.push_back('M');
    append32(result, static_cast<std::uint32_t>(file_size));
    append16(result, 0U);
    append16(result, 0U);
    append32(result, static_cast<std::uint32_t>(pixel_offset));
    result.insert(result.end(), data.begin(), data.end());
    return result;
}

std::size_t Win32Res::size() const noexcept {
    std::size_t total = 0;
    for (const auto& type : table_) total += type.second.size();
    return total;
}

namespace test_access {

std::uint32_t readLe32(const lib::ByteView data, const std::size_t offset) {
    return le32(data, offset);
}

std::uint16_t readLe16(const lib::ByteView data, const std::size_t offset) {
    return le16(data, offset);
}

void appendLe16(lib::Bytes& data, const std::uint16_t value) {
    append16(data, value);
}

void appendLe32(lib::Bytes& data, const std::uint32_t value) {
    append32(data, value);
}

}  // namespace test_access

}  // namespace cwxeditor::cwx::win32res
