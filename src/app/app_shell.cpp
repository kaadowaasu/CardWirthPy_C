#include "app_shell.hpp"

#include <array>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <unordered_map>

namespace {

struct ManagedAsset {
    std::uintmax_t size{};
    std::uint64_t hash{};
};

std::uint64_t file_hash(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("配布Dataを読み込めません: " + path.string());
    std::uint64_t hash = 14695981039346656037ULL;
    std::array<char, 64 * 1024> buffer{};
    while (stream) {
        stream.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto count = stream.gcount();
        for (std::streamsize index = 0; index < count; ++index) {
            hash ^= static_cast<unsigned char>(buffer[static_cast<std::size_t>(index)]);
            hash *= 1099511628211ULL;
        }
    }
    if (!stream.eof()) throw std::runtime_error("配布Dataの検査に失敗しました: " + path.string());
    return hash;
}

std::string encode_path(const std::filesystem::path& path) {
    const auto bytes = path.generic_u8string();
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (const auto byte : bytes)
        output << std::setw(2) << static_cast<unsigned>(static_cast<unsigned char>(byte));
    return output.str();
}

std::string generic_path_utf8(const std::filesystem::path& path) {
    const auto bytes = path.generic_u8string();
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

std::filesystem::path path_from_utf8(std::string_view value) {
#if defined(__cpp_char8_t)
    return std::filesystem::path(std::u8string(
        reinterpret_cast<const char8_t*>(value.data()), value.size()));
#else
    return std::filesystem::u8path(value.begin(), value.end());
#endif
}

std::optional<std::filesystem::path> decode_path(std::string_view encoded) {
    if (encoded.size() % 2 != 0) return std::nullopt;
    std::string bytes;
    bytes.reserve(encoded.size() / 2);
    for (std::size_t index = 0; index < encoded.size(); index += 2) {
        unsigned value = 0;
        std::istringstream input(std::string(encoded.substr(index, 2)));
        input >> std::hex >> value;
        if (!input || value > 255) return std::nullopt;
        bytes.push_back(static_cast<char>(value));
    }
    return std::filesystem::u8path(bytes);
}

bool is_safe_relative_path(const std::filesystem::path& value) {
    if (value.empty() || value.is_absolute() || value.has_root_path()) return false;
    const auto normalized = value.lexically_normal();
    if (normalized.empty() || normalized == ".") return false;
    for (const auto& component : normalized)
        if (component == "..") return false;
    return true;
}

std::unordered_map<std::string, ManagedAsset> read_manifest(
    const std::filesystem::path& manifest) {
    std::unordered_map<std::string, ManagedAsset> result;
    std::ifstream input(manifest, std::ios::binary);
    std::string line;
    if (!std::getline(input, line) || line != "CWASSET1") return result;
    while (std::getline(input, line)) {
        const auto first = line.find('\t');
        const auto second = first == std::string::npos ? first : line.find('\t', first + 1);
        if (first == std::string::npos || second == std::string::npos) continue;
        try {
            const auto path = decode_path(std::string_view(line).substr(0, first));
            if (!path || !is_safe_relative_path(*path)) continue;
            ManagedAsset asset;
            asset.size = static_cast<std::uintmax_t>(std::stoull(line.substr(first + 1, second - first - 1)));
            asset.hash = std::stoull(line.substr(second + 1), nullptr, 16);
            result[generic_path_utf8(*path)] = asset;
        } catch (...) {
        }
    }
    return result;
}

bool replace_file_safely(const std::filesystem::path& source,
                         const std::filesystem::path& destination,
                         std::string& error) {
    std::error_code ec;
    std::filesystem::create_directories(destination.parent_path(), ec);
    if (ec) { error = "Data更新先を作成できません: " + destination.string(); return false; }
    auto temporary = destination;
    temporary += ".cwinstall-new";
    auto backup = destination;
    backup += ".cwinstall-old";
    if (!std::filesystem::exists(destination, ec) && std::filesystem::exists(backup, ec))
        std::filesystem::rename(backup, destination, ec);
    ec.clear();
    std::filesystem::remove(temporary, ec);
    ec.clear();
    std::filesystem::copy_file(source, temporary,
                               std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) { error = "Data更新用ファイルを作成できません: " + destination.string(); return false; }
    const bool had_destination = std::filesystem::exists(destination, ec) && !ec;
    if (had_destination) {
        std::filesystem::remove(backup, ec);
        ec.clear();
        std::filesystem::rename(destination, backup, ec);
        if (ec) { std::filesystem::remove(temporary, ec); error = "既存Dataを保護できません: " + destination.string(); return false; }
    }
    std::filesystem::rename(temporary, destination, ec);
    if (ec) {
        if (had_destination) {
            std::error_code rollback;
            std::filesystem::rename(backup, destination, rollback);
        }
        error = "Data更新を確定できません: " + destination.string();
        return false;
    }
    if (had_destination) std::filesystem::remove(backup, ec);
    return true;
}

bool update_managed_data_mirror(const std::filesystem::path& installed_data,
                                const std::filesystem::path& destination_data,
                                const std::filesystem::path& manifest,
                                std::string& error) {
    const auto previous = read_manifest(manifest);
    std::unordered_map<std::string, ManagedAsset> current;
    std::error_code ec;
    for (std::filesystem::recursive_directory_iterator iterator(
             installed_data, std::filesystem::directory_options::none, ec), end;
         iterator != end; iterator.increment(ec)) {
        if (ec) { error = "配布Dataを列挙できません: " + installed_data.string(); return false; }
        const bool regular = iterator->is_regular_file(ec);
        if (ec) { error = "配布Dataの種類を確認できません: " + iterator->path().string(); return false; }
        if (!regular) continue;
        const auto relative = iterator->path().lexically_relative(installed_data).lexically_normal();
        if (!is_safe_relative_path(relative)) continue;
        ManagedAsset source{iterator->file_size(ec), file_hash(iterator->path())};
        if (ec) { error = "配布Dataのサイズを取得できません: " + iterator->path().string(); return false; }
        const auto key = generic_path_utf8(relative);
        current[key] = source;
    }
    // 最後のインクリメントが失敗した場合も、イテレーターは終端になり得る。
    if (ec) { error = "配布Dataを完全に列挙できません: " + ec.message(); return false; }
    // コピー元全体の走査が成功するまで、複製先とマニフェストは変更しない。
    for (const auto& [key, source] : current) {
        const auto relative = path_from_utf8(key);
        const auto destination = destination_data / relative;
        bool install = !std::filesystem::is_regular_file(destination, ec) || ec;
        ec.clear();
        if (!install) {
            const auto old = previous.find(key);
            if (old != previous.end()) {
                const auto destination_size = std::filesystem::file_size(destination, ec);
                const bool unchanged = !ec && destination_size == old->second.size &&
                    file_hash(destination) == old->second.hash;
                install = unchanged && (old->second.size != source.size || old->second.hash != source.hash);
            }
        }
        if (install && !replace_file_safely(installed_data / relative, destination, error)) return false;
    }
    for (const auto& [key, old] : previous) {
        if (current.contains(key)) continue;
        const auto relative = path_from_utf8(key);
        const auto destination = destination_data / relative;
        if (!std::filesystem::is_regular_file(destination, ec) || ec) { ec.clear(); continue; }
        const auto size = std::filesystem::file_size(destination, ec);
        if (!ec && size == old.size && file_hash(destination) == old.hash)
            std::filesystem::remove(destination, ec);
        ec.clear();
    }

    auto manifest_source = manifest;
    manifest_source += ".source";
    {
        std::ofstream output(manifest_source, std::ios::binary | std::ios::trunc);
        if (!output) { error = "Data管理情報を書き込めません"; return false; }
        output << "CWASSET1\n";
        std::map<std::string, ManagedAsset> ordered(current.begin(), current.end());
        for (const auto& [key, asset] : ordered)
            output << encode_path(path_from_utf8(key)) << '\t' << asset.size << '\t'
                   << std::hex << asset.hash << std::dec << '\n';
        if (!output) { error = "Data管理情報の書込みに失敗しました"; return false; }
    }
    if (!replace_file_safely(manifest_source, manifest, error)) return false;
    std::filesystem::remove(manifest_source, ec);
    return true;
}

}  // namespace

namespace cardwirth::app {

UserDataLayout UserDataLayout::from_root(std::filesystem::path value) {
    if (value.empty()) value = "UserData";
    value = std::filesystem::absolute(std::move(value));
    return UserDataLayout{
        value,
        value / "Data",
        value / "Scenario",
        value / "Yado",
        value / "Config",
        value / "Backup",
        value / "Database",
        value / "Temp",
        value / "Cache",
        value / "Trash",
    };
}

bool UserDataLayout::ensure(std::string& error,
                            const std::filesystem::path& application_root,
                            bool create_auxiliary_roots) const {
    std::error_code filesystem_error;
    for (const auto& directory :
         {root, data, scenario, yado, config}) {
        std::filesystem::create_directories(directory, filesystem_error);
        if (filesystem_error) {
            error = "UserDataディレクトリを作成できません: " + directory.string();
            return false;
        }
    }
    if (create_auxiliary_roots) {
        for (const auto& directory : {backup, database, temp, cache, trash}) {
            std::filesystem::create_directories(directory, filesystem_error);
            if (filesystem_error) {
                error = "UserDataディレクトリを作成できません: " + directory.string();
                return false;
            }
        }
    }

    // ALLの変更不可の資産は<app>/Dataに置くが、従来のEngine
    // は作業ディレクトリを UserDataへ移してからData/Skinを
    // 開く。読み込み側がEngineFileServicesへ移行するまで
    // は、 UserData/Dataを互換用の複製として保持する。 更新
    // 時は不足する配布資産のみを追加し、ユーザーのファイルを上書きしない。
    if (!application_root.empty()) {
        const auto installed_data =
            std::filesystem::absolute(application_root).lexically_normal() / "Data";
        std::error_code status_error;
        const bool same_root =
            std::filesystem::equivalent(installed_data, data, status_error);
        status_error.clear();
        const bool installed_directory = std::filesystem::is_directory(installed_data, status_error);
        if (status_error && status_error != std::errc::no_such_file_or_directory) {
            error = "配布Dataを確認できません: " + status_error.message();
            return false;
        }
        if (!same_root && installed_directory) {
            try {
                if (!update_managed_data_mirror(installed_data, data,
                                                config / "installed_asset_manifest.tsv", error))
                    return false;
            } catch (const std::exception& exception) {
                error = exception.what();
                return false;
            }
        }
    }
    return true;
}

std::vector<std::filesystem::path> UserDataLayout::missing() const {
    std::vector<std::filesystem::path> result;
    for (const auto& directory :
         {data, scenario, yado, config, backup, database, temp, cache, trash}) {
        std::error_code filesystem_error;
        if (!std::filesystem::is_directory(directory, filesystem_error)) result.push_back(directory);
    }
    return result;
}

AppShell::AppShell(UserDataLayout user_data)
    : user_data_(std::move(user_data)), status_("ホーム") {}

void AppShell::set_initial_page(Page page) {
    page_ = page;
    navigation_.clear();
    has_suspended_parent_ = false;
    suspended_ = false;
    status_ = page_ == Page::XEditor ? "XEditor_C" : "ホーム";
}

void AppShell::open_embedded_child(Page page) {
    if (suspended_) return;
    suspended_parent_ = page_;
    has_suspended_parent_ = true;
    suspended_ = true;
    page_ = page;
    status_ = "子画面: " + (page_ == Page::DataExplorer ? std::string("Data Explorer") : std::string("child"));
}

void AppShell::navigate(Page page) {
    if (suspended_) return;
    if (page_ != page) navigation_.push_back(page_);
    page_ = page;
    switch (page_) {
        case Page::Home: status_ = "ホーム"; break;
        case Page::CardWirthPy: status_ = "CardWirthPy_C"; break;
        case Page::XEditor: status_ = "XEditor_C"; break;
        case Page::DataExplorer: status_ = "Data Explorer"; break;
    }
}

void AppShell::go_back() {
    if (has_suspended_parent_) {
        page_ = suspended_parent_;
        has_suspended_parent_ = false;
        suspended_ = false;
        status_ = page_ == Page::XEditor ? "XEditor_C" : "ホーム";
        return;
    }
    if (suspended_) return;
    if (!navigation_.empty()) {
        page_ = navigation_.back();
        navigation_.pop_back();
    } else {
        page_ = Page::Home;
    }
    status_ = page_ == Page::XEditor ? "XEditor_C" :
              page_ == Page::DataExplorer ? "Data Explorer" : "ホーム";
}

void AppShell::begin_child(Page parent_page) {
    page_ = parent_page;
    suspended_ = true;
    status_ = "子アプリケーションを実行中…";
}

void AppShell::end_child(int exit_code) {
    suspended_ = false;
    status_ = exit_code == 0 ? "子アプリケーションが終了しました" :
                               "子アプリケーションが終了しました (code=" + std::to_string(exit_code) + ")";
}

}  // namespace cardwirth::app
