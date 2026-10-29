#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "cwxeditor_src/lib/portable.hpp"

namespace cwxeditor::cwx::editor::gui::dwt::undo {

using String = lib::String;

/// 順方向・逆方向に適用し、その後破棄できる操作。
class Undo {
public:
    virtual ~Undo() = default;

    virtual void undo() = 0;
    virtual void redo() = 0;
    virtual void dispose() = 0;
};

using UndoOwner = lib::Owner<Undo>;
using UndoArray = lib::Array<UndoOwner>;

/// 旧値・新値と、Undo/Redoが使う設定関数を保持する。
template <typename T>
class TUndo : public Undo {
public:
    using Value = T;
    using Setter = lib::Callback<void(T)>;
    using Copier = lib::Callback<T(T)>;

    TUndo(T oldValue, T newValue, Setter setter, Copier copier = {})
        : old_(std::move(oldValue)),
          new_(std::move(newValue)),
          set_(std::move(setter)),
          copy_(std::move(copier)) {}

    void undo() override {
        if (copy_) old_ = copy_(old_);
        set_(old_);
    }

    void redo() override {
        if (copy_) new_ = copy_(new_);
        set_(new_);
    }

    void dispose() override {}

protected:
    T old_;
    T new_;
    Setter set_;
    Copier copy_;
};

class StrUndo : public TUndo<String> {
public:
    StrUndo(String oldValue, String newValue, Setter setter)
        : TUndo<String>(std::move(oldValue), std::move(newValue),
                        std::move(setter)) {}
};

class StrArrUndo : public TUndo<lib::Array<String>> {
public:
    StrArrUndo(lib::Array<String> oldValue, lib::Array<String> newValue,
               Setter setter);
};

/// 複数の操作を一件へまとめる。Undo
/// は既定で生成と逆順、Redo
/// は常に生成順で実行する。
class UndoArr : public Undo {
public:
    explicit UndoArr(UndoArray array, bool reverse = true,
                     lib::Callback<void()> after = {});

    void undo() override;
    void redo() override;
    void dispose() override;

private:
    UndoArray array_;
    bool reverse_{};
    lib::Callback<void()> after_;
};

/// 件数制限付きのUndo/Redo履歴。
class UndoManager {
public:
    explicit UndoManager(std::size_t max);
    ~UndoManager() = default;

    UndoManager(const UndoManager&) = delete;
    UndoManager& operator=(const UndoManager&) = delete;

    /// Dのmanager ~= commandは、C++のmanager += commandに対応する。
    UndoManager& operator+=(UndoOwner undo);

    void max(std::size_t value);
    [[nodiscard]] std::size_t max() const noexcept { return max_; }
    [[nodiscard]] std::size_t pointer() const noexcept { return pointer_; }

    void add(UndoOwner undo);
    [[nodiscard]] bool canUndo() const noexcept { return pointer_ > 0; }
    bool undo();
    [[nodiscard]] bool canRedo() const noexcept {
        return pointer_ < undos_.size();
    }
    bool redo();
    void reset();
    void dispose();

private:
    // 任意のコマンド破棄処理を呼ぶ前に、履歴を切り離す。 終了中の
    // コマンドが同期的に履歴を追加・リセットしても、 古いRedo
    // 分岐を残したり破棄時の例外を外へ漏らしたりしないようにする。
    static void disposeAll(UndoArray commands) noexcept;
    [[nodiscard]] UndoArray cut();

    UndoArray undos_;
    std::size_t max_{};
    std::size_t pointer_{};
    std::size_t revision_{};
    bool operating_{};
};

}  // namespace cwxeditor::cwx::editor::gui::dwt::undo
