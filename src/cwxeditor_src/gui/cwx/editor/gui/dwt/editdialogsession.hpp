#pragma once

// 未適用の編集を持ち得る、全Editorダイアログの
// 共通登録先。 Dは保存・再読み込み・終了前に全AbsDialog
// を列挙する。C++には継承でなく 部品の組み合
// わせで作るダイアログもあるため、AbsDialog
// だけの登録ではそれらの編集が失われる。

#include "cwxeditor_src/lib/components.hpp"

#include <algorithm>
#include <mutex>
#include <vector>

namespace cwxeditor::cwx::editor::gui::dwt::editdialogsession {

class EditDialogSession {
public:
    EditDialogSession(const EditDialogSession&) = delete;
    EditDialogSession& operator=(const EditDialogSession&) = delete;
    EditDialogSession(EditDialogSession&&) = delete;
    EditDialogSession& operator=(EditDialogSession&&) = delete;

    virtual ~EditDialogSession() {
        std::scoped_lock lock(storageMutex());
        std::erase(storage(), this);
    }

    [[nodiscard]] virtual lib::Window* editDialogWindow() const noexcept = 0;
    [[nodiscard]] virtual bool editDialogPending() const noexcept = 0;
    [[nodiscard]] virtual bool applyEditDialog() = 0;
    [[nodiscard]] virtual bool noScenario() const noexcept { return false; }

    [[nodiscard]] static std::vector<EditDialogSession*> liveSessions() {
        std::scoped_lock lock(storageMutex());
        return storage();
    }

protected:
    EditDialogSession() {
        std::scoped_lock lock(storageMutex());
        storage().push_back(this);
    }

private:
    [[nodiscard]] static std::mutex& storageMutex() {
        static std::mutex value;
        return value;
    }

    [[nodiscard]] static std::vector<EditDialogSession*>& storage() {
        static std::vector<EditDialogSession*> value;
        return value;
    }
};

}  // namespace cwxeditor::cwx::editor::gui::dwt::editdialogsession
