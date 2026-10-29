#include "cw/novel.hpp"

#include "cw/lib/encoding.hpp"
#include "platform/storage/storage_mount.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace cw::novel {
namespace {

using ::cw::lib::String;

String trim(String value)
{
    const auto whitespace = [](unsigned char ch) { return std::isspace(ch) != 0; };
    value.erase(value.begin(), std::find_if_not(value.begin(), value.end(), whitespace));
    value.erase(std::find_if_not(value.rbegin(), value.rend(), whitespace).base(), value.end());
    if (value.size() >= 2 && ((value.front() == '"' && value.back() == '"') ||
                              (value.front() == '\'' && value.back() == '\'')))
        value = value.substr(1, value.size() - 2);
    return value;
}

String lower(String value)
{
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

String strip_yaml_comment(String line)
{
    char quote = 0;
    bool escaped = false;
    for (std::size_t index = 0; index < line.size(); ++index) {
        const char ch = line[index];
        if (quote == '"' && ch == '\\' && !escaped) {
            escaped = true;
            continue;
        }
        if ((ch == '"' || ch == '\'') && !escaped) {
            if (quote == 0) quote = ch;
            else if (quote == ch) quote = 0;
        } else if (ch == '#' && quote == 0) {
            line.erase(index);
            break;
        }
        escaped = false;
    }
    return line;
}

std::optional<std::filesystem::path> find_config()
{
    std::error_code ec;
    const auto cwd = std::filesystem::current_path(ec);
    if (ec) return std::nullopt;
    const std::filesystem::path candidates[] = {
        cwd / "novel.yaml", cwd.parent_path() / "novel.yaml",
        cwd / "UserData" / "novel.yaml"};
    for (const auto& path : candidates)
        if (std::filesystem::is_regular_file(path, ec)) return path;
    return std::nullopt;
}

std::filesystem::path logical_slot_root(const std::filesystem::path& base_yado)
{
    auto name = base_yado.filename();
    if (name.empty()) name = base_yado.parent_path().filename();
    auto logical = std::filesystem::path("Yado") / ".novel" / name;
    if (const auto mounted = ::cardwirth::platform::resolve_mounted_user_path(logical))
        return *mounted;
    return logical;
}

std::optional<::cardwirth::platform::StorageStat> stat_path(const std::filesystem::path& path)
{
    if (::cardwirth::platform::is_mounted_user_path(path))
        return ::cardwirth::platform::stat_mounted_user_path(path, true);
    std::error_code ec;
    const auto status = std::filesystem::status(path, ec);
    if (ec && ec != std::errc::no_such_file_or_directory)
        throw std::filesystem::filesystem_error("novel slot lookup", path, ec);
    if (!std::filesystem::exists(status)) return std::nullopt;
    ::cardwirth::platform::StorageStat result;
    result.type = std::filesystem::is_directory(status)
        ? ::cardwirth::platform::StorageEntryType::Directory
        : std::filesystem::is_regular_file(status)
            ? ::cardwirth::platform::StorageEntryType::File
            : ::cardwirth::platform::StorageEntryType::Other;
    return result;
}

} // namespace

Config load_config()
{
    Config result;
    const auto path = find_config();
    if (!path) return result;
    result.source = *path;
    std::ifstream input(*path, std::ios::binary);
    String line;
    bool first_line = true;
    while (std::getline(input, line)) {
        if (first_line && line.size() >= 3 &&
            static_cast<unsigned char>(line[0]) == 0xEF &&
            static_cast<unsigned char>(line[1]) == 0xBB &&
            static_cast<unsigned char>(line[2]) == 0xBF)
            line.erase(0, 3);
        first_line = false;
        line = strip_yaml_comment(std::move(line));
        const auto colon = line.find(':');
        if (colon == String::npos) continue;
        const auto key = lower(trim(line.substr(0, colon)));
        const auto value = trim(line.substr(colon + 1));
        if (key == "enabled" || key == "enable") {
            const auto v = lower(value);
            result.enabled = v == "true" || v == "yes" || v == "on" || v == "1";
        } else if (key == "max_saves" || key == "maxsaves" || key == "max_save_count") {
            try { result.max_saves = std::clamp(std::stoi(value), 1, 100); }
            catch (...) {}
        } else if (key == "yado" || key == "auto_yado" || key == "inn") {
            result.yado = value;
        }
    }
    return result;
}

const Config& config()
{
    static const Config value = load_config();
    return value;
}

bool enabled() { return config().enabled; }

std::filesystem::path slot_path(const std::filesystem::path& base_yado, int slot)
{
    std::ostringstream name;
    name << "slot" << std::setw(3) << std::setfill('0') << std::max(1, slot);
    return logical_slot_root(base_yado) / name.str();
}

SlotState slot_state(const std::filesystem::path& base_yado, int slot)
{
    const auto path = slot_path(base_yado, slot);
    const auto slot_status = stat_path(path);
    if (!slot_status) return SlotState::Missing;
    const auto environment = stat_path(path / "Environment.xml");
    return slot_status->is_directory() && environment && environment->is_file()
        ? SlotState::Saved : SlotState::Incomplete;
}

bool slot_exists(const std::filesystem::path& base_yado, int slot)
{
    return slot_state(base_yado, slot) == SlotState::Saved;
}

std::vector<String> slot_labels(const std::filesystem::path& base_yado, int max_saves)
{
    std::vector<String> result;
    for (int slot = 1; slot <= std::max(1, max_saves); ++slot) {
        std::ostringstream text;
        text << "セーブ " << slot;
        switch (slot_state(base_yado, slot)) {
        case SlotState::Missing: text << "（新規）"; break;
        case SlotState::Incomplete: text << "（不完全）"; break;
        case SlotState::Saved: break;
        }
        result.push_back(text.str());
    }
    return result;
}

bool ensure_slot(const std::filesystem::path& base_yado, int slot, std::string& error)
{
    const auto state = slot_state(base_yado, slot);
    if (state == SlotState::Saved) return true;
    if (state == SlotState::Incomplete) {
        error = "Novelセーブ先に不完全なデータがあります。";
        return false;
    }
    const auto destination = slot_path(base_yado, slot);
    auto staging = destination;
    staging += ".creating";
    const bool source_mounted = ::cardwirth::platform::is_mounted_user_path(base_yado);
    const bool destination_mounted = ::cardwirth::platform::is_mounted_user_path(destination);
    if (source_mounted != destination_mounted) {
        error = "宿とNovelセーブのストレージが一致しません。";
        return false;
    }
    if (destination_mounted) {
        (void)::cardwirth::platform::remove_mounted_user_path(staging, true);
        if (!::cardwirth::platform::make_mounted_user_directory(destination.parent_path(), true) ||
            !::cardwirth::platform::copy_mounted_user_path(base_yado, staging, false) ||
            !::cardwirth::platform::rename_mounted_user_path(staging, destination, false)) {
            (void)::cardwirth::platform::remove_mounted_user_path(staging, true);
            error = "暗号化UserData内にNovelセーブを作成できません。";
            return false;
        }
        return true;
    }
    std::error_code ec;
    std::filesystem::create_directories(destination.parent_path(), ec);
    std::filesystem::remove_all(staging, ec);
    if (!ec) std::filesystem::copy(base_yado, staging,
        std::filesystem::copy_options::recursive, ec);
    if (!ec) std::filesystem::rename(staging, destination, ec);
    if (ec) {
        std::error_code cleanup;
        std::filesystem::remove_all(staging, cleanup);
        error = "Novelセーブを作成できません: " + ec.message();
        return false;
    }
    return true;
}

namespace {
std::filesystem::path session_path(const std::filesystem::path& root, const char* prefix)
{
    static std::atomic<unsigned long long> serial{};
    return root / (String(prefix) + std::to_string(
        std::chrono::high_resolution_clock::now().time_since_epoch().count()) +
        "-" + std::to_string(serial.fetch_add(1)));
}

bool copy_inn(const std::filesystem::path& source, const std::filesystem::path& destination,
              bool mounted, ::cardwirth::platform::NativeStorage* native)
{
    // 使用中の宿の Windows ロックは、共有なしで開かれる。 保存デー
    // タではなくプロセスの状態なので、保存枠や作業セッションへコピーしない。
    struct PartialCopy {
        const std::filesystem::path& path;
        bool mounted;
        ::cardwirth::platform::NativeStorage* native;
        bool complete{};
        ~PartialCopy() {
            if (complete) return;
            try {
                if (mounted) (void)::cardwirth::platform::remove_mounted_user_path(path, true);
                else (void)native->remove(path, true);
            } catch (...) {}
        }
    };
    if (mounted) {
        // Storage::list の失敗は、空の vector と last_error
        // で返る場合がある。 列挙成功とみなして空の保存データを公開しない。
        const auto entries = ::cardwirth::platform::list_mounted_user_path(source, true);
        const auto existing = ::cardwirth::platform::stat_mounted_user_path(destination, true);
        if (!entries || (existing && existing->exists()) ||
            !::cardwirth::platform::make_mounted_user_directory(destination, true)) return false;
        PartialCopy cleanup{destination, true, nullptr};
        for (const auto& entry : *entries) {
            if (entry.name == ".CardWirthPy.lock") continue;
            if (!::cardwirth::platform::copy_mounted_user_path(
                    source / std::filesystem::u8path(entry.name),
                    destination / std::filesystem::u8path(entry.name), false)) {
                return false;
            }
        }
        cleanup.complete = true;
        return true;
    }
    if (!native || native->exists(destination)) return false;
    // 一般のファイル一覧 UI と異なり、保存では権限拒否・走査エラー
    // を失敗とする。 不完全なディレクトリー一覧を黙って公開しない。
    std::error_code ec;
    std::vector<std::filesystem::path> entries;
    std::filesystem::directory_iterator it(source, ec), end;
    if (ec) return false;
    for (; it != end; it.increment(ec)) {
        if (ec) return false;
        entries.push_back(it->path());
    }
    if (ec || !native->mkdir(destination, true)) return false;
    PartialCopy cleanup{destination, false, native};
    for (const auto& entry : entries) {
        if (entry.filename() == ".CardWirthPy.lock") continue;
        if (!native->copy(entry, destination / entry.filename(), false)) {
            return false;
        }
    }
    cleanup.complete = true;
    return true;
}
}

Workspace::~Workspace()
{
    // 削除できるのは、このオブジェクトが生成した直接の子だけとする。 暗号化
    // ・Web のマウントが終了済みでも、ネイティブ入出力へ切り替えない。
    try {
        if (path_.parent_path() != root_ || path_.filename().string().rfind(".working-", 0) != 0)
            return;
        if (mounted_) {
            if (::cardwirth::platform::is_mounted_user_path(path_))
                (void)::cardwirth::platform::remove_mounted_user_path(path_, true);
        } else {
            ::cardwirth::platform::NativeStorage storage(root_);
            (void)storage.remove(path_, true);
        }
    } catch (...) {} // セッションの後処理から、終了処理を越えて例外を出さない。
}

std::shared_ptr<Workspace> Workspace::open(
    const std::filesystem::path& base_yado, int slot, std::string& error)
{
    error.clear();
    if (slot < 1 || slot > 100) { error = "セーブスロットが範囲外です。"; return {}; }
    const auto base = std::filesystem::absolute(base_yado).lexically_normal();
    const auto root = std::filesystem::absolute(logical_slot_root(base)).lexically_normal();
    const auto state = slot_state(base, slot);
    if (state == SlotState::Incomplete) {
        error = "選択したNovelセーブは不完全です。別のスロットを選択してください。";
        return {};
    }
    const auto source = state == SlotState::Saved
        ? root / slot_path(base, slot).filename() : base;
    const auto environment = stat_path(source / "Environment.xml");
    if (!environment || !environment->is_file()) {
        error = "Novelの読み込み元にEnvironment.xmlがありません。"; return {};
    }
    const auto destination = session_path(root, ".working-");
    const bool mounted = ::cardwirth::platform::is_mounted_user_path(source);
    if (mounted != ::cardwirth::platform::is_mounted_user_path(destination)) {
        error = "宿とNovelセーブのストレージが一致しません。"; return {};
    }
    bool copied = false;
    if (mounted) {
        copied = copy_inn(source, destination, true, nullptr);
    } else {
        // 設定された宿とその .novel ディレクトリーは、どちらも Yado の下にある。
        ::cardwirth::platform::NativeStorage storage(base.parent_path());
        copied = copy_inn(source, destination, false, &storage);
    }
    if (!copied) { error = "Novelの作業用セーブデータを作成できません。"; return {}; }
    return std::shared_ptr<Workspace>(new Workspace(root, destination, mounted));
}

bool Workspace::save_slot(int slot, bool overwrite, std::string& error) const
{
    error.clear();
    if (slot < 1 || slot > 100) { error = "セーブスロットが範囲外です。"; return false; }
    std::ostringstream name;
    name << "slot" << std::setw(3) << std::setfill('0') << slot;
    const auto destination = root_ / name.str();
    const auto staging = session_path(root_, ".working-save-");
    // 所有する一時コピーは、全コピーの成功後にだけ名前変更で公開する。 Storage
    // の名前変更はネイティブファイルシステムでバックアップ・復元を提供する。
    // 削除済みのカード・ファイルを復活させないよう、古い保存枠へ混ぜない。
    if (mounted_ && !::cardwirth::platform::is_mounted_user_path(path_)) {
        error = "Novelセーブのストレージが解除されています。"; return false;
    }
    ::cardwirth::platform::ScopedMountedStorageTransaction transaction(path_);
    if (!transaction.good()) { error = transaction.last_error(); return false; }
    bool copied = false, published = false;
    if (mounted_) {
        copied = copy_inn(path_, staging, true, nullptr);
        if (copied) {
            Workspace cleanup(root_, staging, true);
            published = ::cardwirth::platform::rename_mounted_user_path(staging, destination, overwrite);
        }
    } else {
        ::cardwirth::platform::NativeStorage storage(root_);
        copied = copy_inn(path_, staging, false, &storage);
        if (copied) {
            Workspace cleanup(root_, staging, false);
            published = storage.rename(staging, destination, overwrite);
        }
    }
    if (!copied || !published || !transaction.commit()) {
        error = "Novelセーブの保存に失敗しました。";
        return false;
    }
    return true;
}

} // namespace cw::novel
