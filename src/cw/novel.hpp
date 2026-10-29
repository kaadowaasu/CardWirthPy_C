#pragma once

#include "cw/lib/compat.hpp"

#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

namespace cw::novel {

struct Config {
    bool enabled{};
    int max_saves{10};
    ::cw::lib::String yado;
    std::filesystem::path source;
};

// novel.yamlでアプリ単位の有効化を指定する。Engine
// の作業ディレクトリは アプリルートとUserDataのどちら
// にもなり得るため両方を認め、現在のディレクトリを優先する。
[[nodiscard]] Config load_config();
[[nodiscard]] const Config& config();
[[nodiscard]] bool enabled();

[[nodiscard]] std::filesystem::path slot_path(
    const std::filesystem::path& base_yado, int slot);
enum class SlotState { Missing, Saved, Incomplete };
[[nodiscard]] SlotState slot_state(const std::filesystem::path& base_yado, int slot);
[[nodiscard]] bool slot_exists(const std::filesystem::path& base_yado, int slot);
[[nodiscard]] std::vector<::cw::lib::String> slot_labels(
    const std::filesystem::path& base_yado, int max_saves);
[[nodiscard]] bool ensure_slot(const std::filesystem::path& base_yado, int slot,
                               std::string& error);

// ノベルの保存は稼働中の宿ではなくスナップショットとする。 通常のPython互換の
// YadoData保存をセッション専用の宿へ確定してから、一つのスロットへ公開する。
class Workspace final {
public:
    ~Workspace();
    Workspace(const Workspace&) = delete;
    Workspace& operator=(const Workspace&) = delete;
    [[nodiscard]] const std::filesystem::path& path() const { return path_; }
    [[nodiscard]] bool save_slot(int slot, bool overwrite, std::string& error) const;
    [[nodiscard]] static std::shared_ptr<Workspace> open(
        const std::filesystem::path& base_yado, int slot, std::string& error);
private:
    Workspace(std::filesystem::path root, std::filesystem::path path, bool mounted)
        : root_(std::move(root)), path_(std::move(path)), mounted_(mounted) {}
    std::filesystem::path root_, path_;
    bool mounted_{};
};

} // namespace cw::novel
