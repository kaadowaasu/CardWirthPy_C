#include "services.hpp"

#include <algorithm>
#include <cstdlib>
#include <mutex>
#include <unordered_map>
#include <utility>

namespace cwxeditor::lib {
namespace {

class HeadlessGui final : public GuiBackend {
public:
    [[nodiscard]] Platform platform() const noexcept override { return Platform::Headless; }
    void show(const std::shared_ptr<Window>& window) override {
        if (!window) return;
        notify_window_shown(window);
        window->set_visible(true);
    }
    DialogResult show_modal(const std::shared_ptr<Dialog>& dialog,
                            const std::shared_ptr<Window>& owner) override {
        if (!dialog) return DialogResult::Cancel;
        dialog->owner = owner;
        if (owner) dialog->center_on_owner = true;
        notify_window_shown(dialog);
        dialog->set_visible(true);
        return dialog->result;
    }
    void close(const std::shared_ptr<Window>& window) override {
        if (window) window->set_visible(false);
    }
    void request_redraw(const WidgetPtr&, std::optional<Rect>) override {}
    void focus(const WidgetPtr& widget) override { (void)focus_state_.set(widget); }
    [[nodiscard]] WidgetPtr focused_widget() const override {
        return focus_state_.focused();
    }
    [[nodiscard]] std::uint64_t focus_revision() const noexcept override {
        return focus_state_.revision();
    }
    void capture_pointer(const WidgetPtr&, bool) override {}
    void show_context_menu(const std::shared_ptr<Menu>&, const WidgetPtr&, Point) override {}
    bool pump_events(bool) override { return true; }
    void wake_event_loop() override {}
    [[nodiscard]] std::vector<ScreenInfo> screens() const override { return {}; }
private:
    FocusStateMachine focus_state_;
};

class HeadlessGraphics final : public GraphicsBackend {
public:
    ResourceId create_image(const ImageData& image) override {
        const auto id = ++next_;
        images_[id] = image;
        return id;
    }
    void update_image(ResourceId id, const ImageData& image) override { images_[id] = image; }
    void destroy_image(ResourceId id) noexcept override { images_.erase(id); }
    [[nodiscard]] std::vector<std::string> font_families() const override { return {}; }
    [[nodiscard]] Size measure_text(std::string_view, const FontSpec&, float) override { return {}; }
    [[nodiscard]] ResourceId load_application_icon(const std::filesystem::path&, Size) override { return 0; }

private:
    ResourceId next_{};
    std::unordered_map<ResourceId, ImageData> images_;
};

class HeadlessImageCodec final : public ImageCodecBackend {
public:
    [[nodiscard]] ImageData decode(const DecodeImageRequest&, std::string& error) override {
        error = "image decoding is unavailable in the headless backend";
        return {};
    }
    bool encode(const ImageData&, const EncodeImageRequest&, std::string& error) override {
        error = "image encoding is unavailable in the headless backend";
        return false;
    }
    [[nodiscard]] Size probe(std::span<const std::byte>, ImageFormat) override { return {}; }
};

class HeadlessDialogs final : public DialogBackend {
public:
    [[nodiscard]] MessageResult message(const MessageDialogRequest&) override { return MessageResult::Cancel; }
    [[nodiscard]] std::vector<std::filesystem::path> open_files(const FileDialogRequest&) override { return {}; }
    [[nodiscard]] std::optional<std::filesystem::path> save_file(const FileDialogRequest&) override { return std::nullopt; }
    [[nodiscard]] std::optional<std::filesystem::path> select_directory(const FileDialogRequest&) override { return std::nullopt; }
    [[nodiscard]] std::optional<Color> select_color(Color) override { return std::nullopt; }
    [[nodiscard]] std::optional<FontSpec> select_font(const FontSpec&) override { return std::nullopt; }
};

class MemoryClipboard final : public ClipboardBackend {
public:
    void clear() override { std::scoped_lock lock(mutex_); data_ = {}; }
    void write(const ClipboardData& data) override { std::scoped_lock lock(mutex_); data_ = data; }
    [[nodiscard]] ClipboardData read() const override { std::scoped_lock lock(mutex_); return data_; }
    [[nodiscard]] bool contains(std::string_view mime_type) const override {
        std::scoped_lock lock(mutex_);
        return data_.formats.contains(std::string(mime_type)) ||
            ((mime_type=="text/plain" || mime_type=="text/plain;charset=utf-8") &&
             !data_.text.empty());
    }

private:
    mutable std::mutex mutex_;
    ClipboardData data_;
};

class HeadlessDragDrop final : public DragDropBackend {
public:
    void bind_source(DragSourceBinding) override {}
    void bind_target(DropTargetBinding) override {}
    void unbind(const WidgetPtr&) override {}
};

class SilentAudio final : public AudioBackend {
public:
    bool initialize(std::string& error) override { error.clear(); return true; }
    AudioConfigurationResult configure(const AudioConfiguration& request) override {
        configuration_ = {};
        configuration_.requested = request.preference;
        configuration_.effective = request.preference;
        configuration_.error = "audio playback is unsupported by the headless backend";
        return configuration_;
    }
    [[nodiscard]] AudioConfigurationResult configuration() const override {
        return configuration_;
    }
    AudioHandle play(const AudioSource&, const AudioPlayOptions&) override { return ++next_; }
    void pause(AudioHandle) override {}
    void resume(AudioHandle) override {}
    void stop(AudioHandle, std::chrono::milliseconds) noexcept override {}
    void stop_bus(AudioBus) noexcept override {}
    void stop_all() noexcept override {}
    void seek(AudioHandle, std::chrono::milliseconds) override {}
    [[nodiscard]] std::chrono::milliseconds position(AudioHandle) const override { return {}; }
    [[nodiscard]] std::chrono::milliseconds duration(AudioHandle) const override { return {}; }
    [[nodiscard]] bool playing(AudioHandle) const override { return false; }
    void set_bus_volume(AudioBus, float) noexcept override {}
    [[nodiscard]] std::vector<std::string> supported_formats() const override { return {}; }

private:
    AudioHandle next_{};
    AudioConfigurationResult configuration_{};
};

class HeadlessFileSystem final : public FileSystemBackend {
public:
    [[nodiscard]] std::unique_ptr<FileWatchHandle> watch(WatchRequest) override {
        return {}; // ネイティブの監視がない場合は、何もしていないのに成功を返さない。
    }
    bool durable_write(const std::filesystem::path&, std::span<const std::byte>, std::string& error) override {
        error = "durable writes are unavailable in the headless backend";
        return false;
    }
    bool atomic_replace(const std::filesystem::path&, const std::filesystem::path&, std::string& error) override {
        error = "atomic replacement is unavailable in the headless backend";
        return false;
    }
    bool move_to_trash(const std::vector<std::filesystem::path>&, std::string& error) override {
        error = "trash is unavailable in the headless backend";
        return false;
    }
    [[nodiscard]] bool valid_filename_character(char32_t value) const override {
        return value != U'\0' && value != U'/';
    }
};

class HeadlessProcess final : public ProcessBackend {
public:
    [[nodiscard]] std::shared_ptr<ProcessHandle> launch(const ProcessLaunchRequest&, std::string& error) override {
        error = "process launch is unavailable in the headless backend";
        return {};
    }
};

class HeadlessShell final : public ShellBackend {
public:
    bool open_path(const std::filesystem::path&, std::string& error) override { return unavailable(error); }
    bool reveal_path(const std::filesystem::path&, std::string& error) override { return unavailable(error); }
    bool open_url(std::string_view, std::string& error) override { return unavailable(error); }
    [[nodiscard]] std::string associated_command(const std::filesystem::path&) const override { return {}; }

private:
    static bool unavailable(std::string& error) {
        error = "OS shell integration is unavailable in the headless backend";
        return false;
    }
};

class NullIpcServer final : public IpcServer {
public:
    void stop() noexcept override {}
};

class HeadlessIpc final : public IpcBackend {
public:
    [[nodiscard]] std::string allocate_endpoint(std::string_view application_id) override {
        return std::string(application_id) + ".headless";
    }
    [[nodiscard]] std::unique_ptr<IpcServer> listen(
        std::string, std::function<std::string(std::string_view)>, std::string& error) override {
        error.clear();
        return std::make_unique<NullIpcServer>();
    }
    [[nodiscard]] std::string request(std::string_view, std::string_view,
                                      std::chrono::milliseconds, std::string& error) override {
        error = "IPC is unavailable in the headless backend";
        return {};
    }
};

class HeadlessArchive final : public ArchiveBackend {
public:
    [[nodiscard]] std::vector<ArchiveEntry> list(const std::filesystem::path&, ArchiveFormat,
                                                 std::string& error) override {
        error = "archive support is unavailable in the headless backend";
        return {};
    }
    bool create(const ArchiveCreateRequest&, std::string& error) override {
        error = "archive support is unavailable in the headless backend";
        return false;
    }
    bool extract(const ArchiveExtractRequest&, std::string& error) override {
        error = "archive support is unavailable in the headless backend";
        return false;
    }
};

class HeadlessSystem final : public SystemBackend {
public:
    [[nodiscard]] Platform platform() const noexcept override { return Platform::Headless; }
    [[nodiscard]] std::filesystem::path executable_path() const override { return {}; }
    [[nodiscard]] std::filesystem::path special_directory(SpecialDirectory) const override {
        return std::filesystem::current_path();
    }
    [[nodiscard]] std::string locale_name() const override { return "C"; }
    [[nodiscard]] ThemeMode theme_mode() const override { return ThemeMode::System; }
    [[nodiscard]] float scale_factor() const override { return 1.0F; }
    [[nodiscard]] std::size_t working_memory() const override { return 0; }
    [[nodiscard]] std::unique_ptr<DynamicLibrary> load_library(const std::filesystem::path&,
                                                               std::string& error) override {
        error = "dynamic libraries are unavailable in the headless backend";
        return {};
    }
    [[nodiscard]] std::vector<std::byte> executable_resource(const std::filesystem::path&,
                                                             std::string_view,
                                                             std::string_view,
                                                             std::string& error) override {
        error = "executable resources are unavailable in the headless backend";
        return {};
    }
    void set_environment(std::string name, std::string value) override {
        environment_[std::move(name)] = std::move(value);
    }
    [[nodiscard]] std::optional<std::string> environment(std::string_view name) const override {
        const auto found = environment_.find(std::string(name));
        if (found == environment_.end()) return std::nullopt;
        return found->second;
    }

private:
    std::unordered_map<std::string, std::string> environment_;
};

class HeadlessText final : public TextBackend {
public:
    [[nodiscard]] std::string decode(std::span<const std::byte> data,
                                     TextEncoding encoding,
                                     std::string& error) const override {
        if (encoding != TextEncoding::Utf8 && encoding != TextEncoding::System) {
            error = "only UTF-8 is available in the headless backend";
            return {};
        }
        error.clear();
        return {reinterpret_cast<const char*>(data.data()), data.size()};
    }
    [[nodiscard]] std::vector<std::byte> encode(std::string_view text,
                                                TextEncoding encoding,
                                                std::string& error) const override {
        if (encoding != TextEncoding::Utf8 && encoding != TextEncoding::System) {
            error = "only UTF-8 is available in the headless backend";
            return {};
        }
        error.clear();
        const auto* begin = reinterpret_cast<const std::byte*>(text.data());
        return {begin, begin + text.size()};
    }
    [[nodiscard]] std::string normalize(std::string_view text, TextNormalization) const override {
        return std::string(text);
    }
    [[nodiscard]] std::string case_fold(std::string_view text) const override {
        return std::string(text);
    }
    [[nodiscard]] int locale_compare(std::string_view left, std::string_view right) const override {
        return left.compare(right);
    }
};

class ImmediateDispatcher final : public MainDispatcher {
public:
    [[nodiscard]] bool is_main_thread() const noexcept override { return true; }
    void post(std::function<void()> task) override { if (task) task(); }
    void invoke(std::function<void()> task) override { if (task) task(); }
    TimerId post_delayed(std::chrono::milliseconds, std::function<void()> task) override {
        if (task) task();
        return ++next_;
    }
    void cancel_timer(TimerId) noexcept override {}

private:
    TimerId next_{};
};

class FinishedTask final : public TaskHandle {
public:
    void cancel() noexcept override {}
    [[nodiscard]] bool finished() const noexcept override { return true; }
    void wait() override {}
};

class ImmediateTasks final : public TaskBackend {
public:
    [[nodiscard]] std::shared_ptr<TaskHandle> run(std::function<void()> task) override {
        if (task) task();
        return std::make_shared<FinishedTask>();
    }
};

}  // namespace

Services make_headless_services() {
    Services result;
    result.gui = std::make_shared<HeadlessGui>();
    result.graphics = std::make_shared<HeadlessGraphics>();
    result.image_codecs = std::make_shared<HeadlessImageCodec>();
    result.dialogs = std::make_shared<HeadlessDialogs>();
    result.file_picker = std::make_shared<NativeFilePicker>(result.dialogs);
    result.clipboard = std::make_shared<MemoryClipboard>();
    result.drag_drop = std::make_shared<HeadlessDragDrop>();
    result.audio = std::make_shared<SilentAudio>();
    result.filesystem = std::make_shared<HeadlessFileSystem>();
    result.processes = std::make_shared<HeadlessProcess>();
    result.shell = std::make_shared<HeadlessShell>();
    result.ipc = std::make_shared<HeadlessIpc>();
    result.archives = std::make_shared<HeadlessArchive>();
    result.system = std::make_shared<HeadlessSystem>();
    result.text = std::make_shared<HeadlessText>();
    result.dispatcher = std::make_shared<ImmediateDispatcher>();
    result.tasks = std::make_shared<ImmediateTasks>();
    return result;
}

}  // namespace cwxeditor::lib
