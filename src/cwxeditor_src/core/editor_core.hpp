#pragma once

#include <filesystem>
#include <string>
#include <utility>

namespace cwxeditor::core {

// Editorのデータルートと文書・変更状態を保持
// する、OSに依存しない基底の管理オブジェクト。
class EditorCore {
public:
    void set_data_root(std::filesystem::path value) { data_root_ = std::move(value); }
    [[nodiscard]] const std::filesystem::path& data_root() const noexcept { return data_root_; }
    [[nodiscard]] bool has_document() const noexcept { return has_document_; }
    [[nodiscard]] bool changed() const noexcept { return changed_; }

private:
    std::filesystem::path data_root_{};
    bool has_document_{};
    bool changed_{};
};

}  // namespace cwxeditor::core
