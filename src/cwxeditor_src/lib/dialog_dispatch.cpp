#include "dialog_dispatch.hpp"
#include "portable_async.hpp"

#include <stdexcept>
#include <utility>

namespace cwxeditor::lib {
namespace {
class DispatchedDialogs final : public DialogBackend {
public:
    DispatchedDialogs(std::shared_ptr<DialogBackend> backend,
                      std::shared_ptr<MainDispatcher> dispatcher)
        : backend_(std::move(backend)), dispatcher_(std::move(dispatcher)) {}

    void set_document_root(std::filesystem::path root) override {
        backend_->set_document_root(std::move(root));
    }

    MessageResult message(const MessageDialogRequest& request) override {
        return call(MessageResult::None, [&] { return backend_->message(request); });
    }
    bool supports_message_async() const noexcept override {
        return backend_ && backend_->supports_message_async();
    }
    bool message_async(const MessageDialogRequest& request,
                       MessageCompletion completed) override {
        if (!completed) return false;
        bool started = false;
        try {
            dispatcher_->invoke([&] {
                started = backend_->message_async(request, std::move(completed));
            });
        } catch (const DispatcherStopped&) {
            return false;
        }
        return started;
    }
    std::vector<std::filesystem::path> open_files(const FileDialogRequest& request) override {
        return call(std::vector<std::filesystem::path>{}, [&] { return backend_->open_files(request); });
    }
    bool open_files_async(const FileDialogRequest& request,
                          FileDialogOpenCompletion completed) override {
        if (!completed) return false;
        bool started = false;
        try {
            dispatcher_->invoke([&] {
                started = backend_->open_files_async(request, std::move(completed));
            });
        } catch (const DispatcherStopped&) {
            return false;
        }
        return started;
    }
    std::optional<std::filesystem::path> save_file(const FileDialogRequest& request) override {
        return call(std::optional<std::filesystem::path>{}, [&] { return backend_->save_file(request); });
    }
    FileDialogSaveResult save_file_result(const FileDialogRequest& request) override {
        return call(FileDialogSaveResult{}, [&] {
            return backend_->save_file_result(request);
        });
    }
    bool save_file_result_async(const FileDialogRequest& request,
                                FileDialogSaveCompletion completed) override {
        if (!completed) return false;
        bool started = false;
        try {
            dispatcher_->invoke([&] {
                started = backend_->save_file_result_async(
                    request, std::move(completed));
            });
        } catch (const DispatcherStopped&) {
            return false;
        }
        return started;
    }
    std::optional<std::filesystem::path> select_directory(const FileDialogRequest& request) override {
        return call(std::optional<std::filesystem::path>{}, [&] { return backend_->select_directory(request); });
    }
    bool select_directory_async(const FileDialogRequest& request,
                                FileDialogDirectoryCompletion completed) override {
        if (!completed) return false;
        bool started = false;
        try {
            dispatcher_->invoke([&] {
                started = backend_->select_directory_async(
                    request, std::move(completed));
            });
        } catch (const DispatcherStopped&) {
            return false;
        }
        return started;
    }
    bool finalize_save_file(const std::filesystem::path& path, bool success,
                            std::string& error) override {
        error = "Dialog dispatcher stopped before completing the save";
        return call(false, [&] { return backend_->finalize_save_file(path, success, error); });
    }
    void release_import_file(const std::filesystem::path& path) override {
        dispatcher_->invoke([&] { backend_->release_import_file(path); });
    }
    std::optional<Color> select_color(Color initial) override {
        return call(std::optional<Color>{}, [&] { return backend_->select_color(initial); });
    }
    bool select_color_async(Color initial, ColorCompletion completed) override {
        if (!completed) return false;
        bool started = false;
        try {
            dispatcher_->invoke([&] {
                started = backend_->select_color_async(initial, std::move(completed));
            });
        } catch (const DispatcherStopped&) {
            return false;
        }
        return started;
    }
    std::optional<FontSpec> select_font(const FontSpec& initial) override {
        return call(std::optional<FontSpec>{}, [&] { return backend_->select_font(initial); });
    }
private:
    template<class T, class F> T call(T cancelled, F&& operation) {
        T result = cancelled;
        try {
            // invoke は完了・キャンセルしてから戻るため
            // 、表示中は呼び出し元の要求・出力への参照が有効。
            dispatcher_->invoke([&] { result = operation(); });
        } catch (const DispatcherStopped&) {
            return cancelled;
        }
        return result;
    }
    std::shared_ptr<DialogBackend> backend_;
    std::shared_ptr<MainDispatcher> dispatcher_;
};
} // namespace

std::shared_ptr<DialogBackend> make_dispatched_dialog_backend(
    std::shared_ptr<DialogBackend> backend, std::shared_ptr<MainDispatcher> dispatcher) {
    if (!backend || !dispatcher)
        throw std::invalid_argument("Dialog dispatch requires a backend and dispatcher");
    return std::make_shared<DispatchedDialogs>(std::move(backend), std::move(dispatcher));
}
} // namespace cwxeditor::lib
