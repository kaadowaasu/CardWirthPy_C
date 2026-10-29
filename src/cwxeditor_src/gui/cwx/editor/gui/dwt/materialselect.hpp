#pragma once

#include "cwxeditor_src/core/cwx/card.hpp"
#include "cwxeditor_src/core/cwx/skin.hpp"
#include "cwxeditor_src/core/cwx/summary.hpp"
#include "cwxeditor_src/core/cwx/types.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dprops.hpp"
#include "cwxeditor_src/lib/components.hpp"
#include "cwxeditor_src/lib/ui_actions.hpp"
#include "cwxeditor_src/lib/drag_drop.hpp"
#include "cwxeditor_src/lib/services.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/undo.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/timebar.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/incsearch.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <utility>

namespace cwxeditor::cwx::editor::gui::dwt::commons { class Commons; }

namespace cwxeditor::cwx::editor::gui::dwt::materialselect {

namespace lib = ::cwxeditor::lib;
namespace card = ::cwxeditor::cwx::card;
namespace dprops = ::cwxeditor::cwx::editor::gui::dwt::dprops;
namespace skin = ::cwxeditor::cwx::skin;
namespace summary = ::cwxeditor::cwx::summary;
namespace types = ::cwxeditor::cwx::types;
namespace undo = ::cwxeditor::cwx::editor::gui::dwt::undo;

using String = lib::String;
using StringView = lib::StringView;
using Props = dprops::Props;
using Summary = summary::Summary;
using Skin = skin::Skin;
using SkinPtr = Skin::Ptr;
using CardImage = card::CardImage;
using CardImagePtr = lib::Owner<CardImage>;
using CardImageList = lib::Array<CardImagePtr>;
using Services = lib::Services;
using Parent = std::shared_ptr<lib::Container>;
using Action = lib::Action;

enum class MtType { CARD, BG_IMG, BGM, SE };

// 同じ音声バックエンドを使う全選択部品で、SEプレビューの出力経路を一つ
// 共有する。 再入で新しい要求が来たら、処理中の古い要求を無効にする。
struct SoundPreviewTicket {
    std::shared_ptr<std::atomic<std::uint64_t>> revision;
    std::uint64_t expected{};
    [[nodiscard]] bool current() const noexcept {
        return !revision || revision->load() == expected;
    }
};
[[nodiscard]] SoundPreviewTicket beginSoundPreview(
    const std::shared_ptr<lib::AudioBackend>& audio);

// 型付きの素材表の行で使う、D互換の分類アイコン規則。
[[nodiscard]] lib::ResourceId materialRowIcon(
    Props& prop, MtType type, StringView file, const Skin* skin,
    bool included);


// Dのフォルダーボタンは、表示中の素材の場所を開く。 新しいディレ
// クトリの選択や素材値の編集は行わず、両方の選択部品で共用する。
struct MaterialDirectoryQuery {
    std::filesystem::path selected_directory;
    std::filesystem::path scenario_root;
    std::filesystem::path material_directory;
    std::vector<std::filesystem::path> default_directories;
    std::function<bool(const std::filesystem::path&)> contains_material;
};
[[nodiscard]] std::filesystem::path materialDirectoryToOpen(MaterialDirectoryQuery query);

// 素材選択と効果系カードの音声ページに共通のフォーム。 状態の接続は各コ
// ントローラーが行い、この構築処理は共通部品の組み合わせだけを担当する。
struct PlayingOptionsControls final {
    std::shared_ptr<lib::Panel> panel;
    std::shared_ptr<lib::ComboBox> channel;
    std::shared_ptr<lib::NumberEdit> volume;
    std::shared_ptr<lib::NumberEdit> loops;
};
[[nodiscard]] PlayingOptionsControls createPlayingOptionsControls(
    Parent parent, const Props* prop, MtType type,
    bool select_channel = false, bool table_layout = false);

/// 素材ディレクトリ・ファイル選択の共通モデルと
/// 部品アダプター。 通知には共通の値を使い、
/// OS固有のイベントオブジェクトは渡さない。
class MaterialSelectBase {
public:
    using Definitions = std::function<std::vector<String>(bool included)>;
    using CardDefinitionGetter =
        std::function<CardImagePtr(int index, bool included, StringView binary_path)>;
    using CardDefinitionIndex =
        std::function<int(const CardImage& image, bool included)>;
    using BinaryIndex = std::function<int(bool included)>;

    MaterialSelectBase(MtType type, Parent parent, int style, Props& prop,
                       Summary* summary, SkinPtr force_skin, bool read_only,
                       Action refresh = {}, Definitions definitions = {},
                       bool can_include = false, bool is_menu_card = false,
                       std::shared_ptr<undo::UndoManager> undo_manager = {},
                       Action store = {}, Services services = {});
    MaterialSelectBase(MtType type, Props& prop, Summary* summary,
                       SkinPtr force_skin, bool read_only,
                       Action refresh = {}, Definitions definitions = {},
                       bool can_include = false, bool is_menu_card = false,
                       std::shared_ptr<undo::UndoManager> undo_manager = {},
                       Action store = {}, Services services = {});
    MaterialSelectBase(const MaterialSelectBase&) = delete;
    MaterialSelectBase& operator=(const MaterialSelectBase&) = delete;
    MaterialSelectBase(MaterialSelectBase&&) = delete;
    MaterialSelectBase& operator=(MaterialSelectBase&&) = delete;
    ~MaterialSelectBase();

    std::vector<Action> modEvent;
    std::vector<Action> includeEvent;
    std::vector<Action> loadedEvent;
    CardDefinitionGetter valueFromDef;
    CardDefinitionIndex valueToDef;
    std::vector<Action> updateNoCardSize;
    BinaryIndex indexOfBinPath;

    [[nodiscard]] MtType type() const noexcept { return type_; }
    [[nodiscard]] bool canInclude() const noexcept { return can_include_; }
    [[nodiscard]] bool isMenuCard() const noexcept { return is_menu_card_; }
    [[nodiscard]] Parent widget() const noexcept { return panel_; }

    [[nodiscard]] std::shared_ptr<lib::ComboBox> createDirsCombo(Parent parent = {});
    [[nodiscard]] std::shared_ptr<lib::TableView> createFileList(Parent parent = {});
    [[nodiscard]] std::shared_ptr<lib::ComboBox> createFileCombo(Parent parent = {});
    void refreshFileListMenu();

    void stopBGM();
    void canContinue(bool value);
    [[nodiscard]] bool canContinue() const noexcept { return can_continue_; }
    void continueBGM(bool value);
    [[nodiscard]] bool continueBGM() const noexcept { return continue_bgm_; }

    void useNoCardSizeImage(bool value);
    [[nodiscard]] bool useNoCardSizeImage() const noexcept { return no_card_size_; }
    void includeImage();
    void excludeCardSizeImage(bool value);
    [[nodiscard]] bool excludeCardSizeImage() const;

    [[nodiscard]] std::vector<String> warnings() const;
    [[nodiscard]] Parent createPlayingOptions(Parent parent = {},
                                               bool select_channel = false,
                                               bool table_layout = false);
    [[nodiscard]] Parent createFadeIn(Parent parent = {});
    [[nodiscard]] std::shared_ptr<lib::NumberEdit> volumeControl() const noexcept { return volume_edit_; }
    [[nodiscard]] std::shared_ptr<lib::NumberEdit> loopControl() const noexcept { return loop_edit_; }
    void refDataVersion();
    void bindEditorCommons(commons::Commons* comm);
    [[nodiscard]] int volume() const noexcept;
    void volume(int value);
    [[nodiscard]] int loopCount() const noexcept;
    void loopCount(int value);
    [[nodiscard]] int channel() const noexcept { return channel_; }
    void channel(int value);
    // Dの任意のSpinner取得に合わせつつ、場面側から潜在的なモデル値を使えるようにする。
    [[nodiscard]] int fadeIn() const noexcept { return fade_edit_ ? fade_in_ms_ : 0; }
    [[nodiscard]] int fadeInModelValue() const noexcept { return fade_in_ms_; }
    void fadeIn(int value);

    [[nodiscard]] std::shared_ptr<timebar::TimeBar> createPlayingBar(Parent parent = {});
    void initPlayBar();
    void createPlayToolItem(const std::shared_ptr<lib::ToolBar>& bar);
    [[nodiscard]] std::shared_ptr<lib::Button> createPlayButton(Parent parent = {});
    [[nodiscard]] std::shared_ptr<lib::Button> createStopButton(Parent parent = {});
    [[nodiscard]] bool canPlay() const;
    [[nodiscard]] bool isPlayOrStop() const;
    void updatePlayButton(bool play_or_stop);
    void playBGM(bool from_event = false);
    void bgmStopped();
    void playSE();
    void stopSE();
    void seStopped();

    class Play {
    public:
        explicit Play(MaterialSelectBase* owner = nullptr) : owner_(owner) {}
        void mouseUp(lib::PointerEvent&) {}
        void mouseDown(lib::PointerEvent&) {}
        void mouseDoubleClick(lib::PointerEvent& event);
        void keyReleased(lib::KeyEvent&) {}
        void keyPressed(lib::KeyEvent& event);
        void widgetSelected();

    private:
        MaterialSelectBase* owner_{};
    };

    void playThr();

    class UpdatePlayBar {
    public:
        explicit UpdatePlayBar(MaterialSelectBase* owner = nullptr) : owner_(owner) {}
        int len{};
        void run();
        void putPos(std::int64_t position);

    private:
        MaterialSelectBase* owner_{};
    };

    [[nodiscard]] std::shared_ptr<lib::Button> createRefreshButton(
        Parent parent = {}, bool text = false);
    [[nodiscard]] bool canRefresh() const noexcept;
    [[nodiscard]] std::shared_ptr<lib::Button> createDirectoryButton(
        Parent parent = {}, bool text = false);
    [[nodiscard]] bool canDirectoryButton() const noexcept { return !loading_; }
    void createRefreshToolItem(const std::shared_ptr<lib::ToolBar>& bar);
    void createDirectoryToolItem(const std::shared_ptr<lib::ToolBar>& bar);
    [[nodiscard]] bool loading() const noexcept { return loading_; }

    [[nodiscard]] CardImageList paths() const { return paths_; }
    [[nodiscard]] String path(std::ptrdiff_t index = -1) const;
    void paths(CardImageList value) { setPaths(std::move(value), false); }
    void setPaths(CardImageList value, bool store);
    void path(String value) { path2(std::move(value), true, true); }
    void path2(String value, bool refresh_paths, bool update_binary_image,
               std::ptrdiff_t index = -1, bool store = true);
    void selectPath(String value, std::ptrdiff_t index = -1, bool store = true);
    void updateUseNoCardSizeImage();
    [[nodiscard]] String binPath() const;
    [[nodiscard]] int imageIndex() const noexcept { return image_index_; }
    void imageIndex(int value);
    [[nodiscard]] String filePath() const;
    [[nodiscard]] const std::shared_ptr<lib::ComboBox>& dirsCombo() const noexcept {
        return dirs_;
    }
    [[nodiscard]] const std::shared_ptr<lib::TableView>& fileList() const noexcept {
        return file_list_;
    }
    [[nodiscard]] const std::shared_ptr<lib::ComboBox>& fileCombo() const noexcept {
        return file_combo_;
    }
    void refresh();
    [[nodiscard]] std::vector<String> showingNames() const;
    [[nodiscard]] std::vector<String> showingPaths() const;
    [[nodiscard]] String toFilePath(StringView file) const;
    void copyFilePath();
    [[nodiscard]] bool selectedDefDir() const noexcept { return table_dir_ >= 0 && selected_dir_ == table_dir_; }
    void selectDir(int selection);

    class IncrementalSearch {
    public:
        void startIncSearch() { open_ = true; }
        void close() { open_ = false; pattern_.clear(); matcher_ = {}; }
        [[nodiscard]] bool isOpen() const noexcept { return open_; }
        [[nodiscard]] bool match(StringView value) const;
        void pattern(String value) { pattern_ = std::move(value); open_ = true; }
        [[nodiscard]] const String& pattern() const noexcept { return pattern_; }
        void matcher(std::function<bool(StringView)> value) { matcher_ = std::move(value); open_ = true; }

    private:
        bool open_{};
        String pattern_;
        std::function<bool(StringView)> matcher_;
    };

    [[nodiscard]] IncrementalSearch& incSearch() noexcept { return inc_search_; }
    void startIncSearch();
    void createIncrementalSearch();
    [[nodiscard]] bool canIncSearch() const noexcept;
    void loadScaledImage(std::function<bool()> callback) {
        load_scaled_image_ = std::move(callback);
    }
    void enabled(bool value);
    [[nodiscard]] bool enabled() const noexcept { return enabled_; }

    [[nodiscard]] const std::vector<String>& defExts() const;
    [[nodiscard]] std::vector<String> defDirs() const;
    [[nodiscard]] std::vector<String> engineDefDirs() const;
    [[nodiscard]] bool isTarg(StringView path) const;
    [[nodiscard]] bool hasTarg(const Skin& value, StringView path,
                               bool force_refresh, bool no_card_size) const;
    [[nodiscard]] bool hasWsnTarg(bool force_refresh, bool no_card_size) const;
    [[nodiscard]] std::vector<String> targsImpl(StringView directory,
                                                 bool force_refresh) const;
    [[nodiscard]] lib::ResourceId image(StringView file) const;
    void doRefresh();
    void doDirectory();

    class CSListener {
    public:
        explicit CSListener(MaterialSelectBase* owner = nullptr) : owner_(owner) {}
        void widgetSelected();

    private:
        MaterialSelectBase* owner_{};
    };
    class LSListener {
    public:
        explicit LSListener(MaterialSelectBase* owner = nullptr) : owner_(owner) {}
        void widgetSelected();

    private:
        MaterialSelectBase* owner_{};
    };

    void selectFile();

    class OpenMaterial {
    public:
        explicit OpenMaterial(MaterialSelectBase* owner = nullptr) : owner_(owner) {}
        void mouseDoubleClick(lib::PointerEvent& event);
        void keyReleased(lib::KeyEvent&) {}
        void keyPressed(lib::KeyEvent& event);

    private:
        MaterialSelectBase* owner_{};
    };

    void openFilePath();
    static String fromViewPath(String value);
    static String toViewPath(String value);
    [[nodiscard]] String currentDir() const;
    [[nodiscard]] String fileText(const lib::TableRow& item) const;
    [[nodiscard]] String fileText(StringView item) const { return String(item); }
    [[nodiscard]] std::ptrdiff_t indexOf(const lib::ComboBox& list,
                                          StringView path) const;
    [[nodiscard]] std::ptrdiff_t indexOf(const lib::TableView& list,
                                          StringView path) const;
    [[nodiscard]] std::ptrdiff_t flIndexOf(StringView path) const;
    [[nodiscard]] std::ptrdiff_t dirsIndexOf(StringView path) const;
    [[nodiscard]] std::vector<String> targs(StringView path,
                                             bool force_refresh,
                                             bool sub_thread) const;
    void refreshListImpl(StringView path, bool force_refresh,
                         bool sub_thread = false);
    void refreshListImpl(const std::vector<String>& paths, bool all_list,
                         bool force_refresh, bool sub_thread,
                         std::ptrdiff_t skin_pos,
                         std::ptrdiff_t engine_pos_from,
                         std::ptrdiff_t engine_pos_to);

    class WarningsObj {
    public:
        explicit WarningsObj(std::vector<String> values = {})
            : warnings(std::move(values)) {}
        std::vector<String> warnings;
    };
    [[nodiscard]] std::vector<String> getWarnings(const lib::TableRow& item) const;
    void refreshListImpl2(const std::vector<String>& targets);
    [[nodiscard]] std::vector<String> allDirs(std::ptrdiff_t& skin_pos,
                                               std::ptrdiff_t& engine_pos_from,
                                               std::ptrdiff_t& engine_pos_to) const;
    void refreshList(bool force_refresh = false, bool sub_thread = false);
    [[nodiscard]] std::vector<String> searchTarg(const Skin& value,
                                                 StringView directory,
                                                 bool force_refresh,
                                                 std::size_t cut) const;
    void refreshPaths(String select = {}, bool force_refresh = false,
                      bool update_list = true);
    void refreshPathsImpl(String select, bool force_refresh, bool update_list);
    void refPaths(StringView parent = {});
    void refPath(StringView old_path, StringView new_path, bool is_directory);
    void delPaths();
    void replPath(StringView from, StringView to);
    void refreshButtons(bool update_audio_options = true);
    void updateSkinMaterialsExtension();
    void refSkin();
    [[nodiscard]] SkinPtr summSkin() const noexcept;
    [[nodiscard]] std::vector<String> getDefs() const;
    [[nodiscard]] std::ptrdiff_t continueIndex() const;
    bool addDroppedFiles(const std::vector<std::filesystem::path>& files);

protected:
    [[nodiscard]] const Services& services() const noexcept { return services_; }
    [[nodiscard]] Props* props() const noexcept { return prop_; }
    [[nodiscard]] Summary* summary() const noexcept { return summary_; }

private:
    // タスクの寿命管理の詳細を公開APIへ広げないよう、不透明な型にする。
    struct RefreshTaskState;
    static void scheduleRefreshTaskReap(
        const std::shared_ptr<RefreshTaskState>& task,
        const std::shared_ptr<lib::MainDispatcher>& dispatcher,
        std::function<void()> on_reaped = {});
    struct DirectoryEntry {
        String label;
        String path;
        enum class Kind { Definition, Skin, Engine, Scenario } kind{Kind::Scenario};
    };
    struct RefreshPathSnapshot {
        std::vector<String> definitions;
        String default_label;
        String engine_label;
    };
    struct ScenarioDirectoryScanResult {
        bool skin_has_target{};
        bool engine_has_target{};
        std::vector<String> scenario_directories;
    };

    static Services resolveServices(const Props& prop, Services supplied);
    static std::shared_ptr<lib::Panel> makePanel(const Services& services,
                                                 Parent parent);
    static std::shared_ptr<lib::Button> makeButton(const Services& services,
                                                   String text);
    static std::shared_ptr<lib::NumberEdit> makeNumber(const Services& services);
    static std::shared_ptr<lib::ComboBox> makeCombo(const Services& services);
    static std::shared_ptr<lib::TableView> makeTable(const Services& services);
    static std::shared_ptr<lib::ProgressBar> makeProgress(const Services& services);
    [[nodiscard]] Parent controlParent(Parent requested = {});
    [[nodiscard]] lib::WidgetPtr createFileControl(Parent parent, bool combo);
    [[nodiscard]] lib::WidgetPtr fileControl() const noexcept;
    void handleFilePointer(const lib::WidgetPtr& control, lib::PointerEvent& event);
    static bool hasExtension(const std::filesystem::path& path,
                             const std::vector<String>& extensions);
    static String baseName(StringView path);
    static String dirName(StringView path);
    static String normalizePath(StringView path);
    static void appendUnique(std::vector<String>& values, String value);
    void volumeFromUser(int value);
    void notifyModified();
    void disconnectEditorCommons() noexcept;
    [[nodiscard]] commons::Commons* liveEditorCommons() const noexcept;
    void updateEnabledControls(bool update_audio_options = true);
    void updateAudioOptionControls(bool respect_selector_enabled);
    void stopAudio();
    void pollAudio();
    void addDropTarget();
    void setSelection(std::size_t index);
    void selectRow(std::size_t index);
    void setRows(const std::vector<String>& targets);
    struct FileLocation {
        bool has_summary{};
        String scenario_root;
        std::vector<String> default_dirs;
        std::vector<String> engine_dirs;
        DirectoryEntry::Kind kind{DirectoryEntry::Kind::Scenario};
        String current_directory;
        bool all_list{};
    };
    [[nodiscard]] FileLocation fileLocation() const;
    [[nodiscard]] ScenarioDirectoryScanResult scanMaterialTargets(
        const FileLocation& location, const SkinPtr& skin,
        bool force_refresh, bool no_card_size) const;
    [[nodiscard]] static std::vector<String> directorySources(
        const std::vector<DirectoryEntry>& entries, std::size_t definition_count,
        const FileLocation& location, std::ptrdiff_t& skin_pos,
        std::ptrdiff_t& engine_from, std::ptrdiff_t& engine_to);
    [[nodiscard]] static std::filesystem::path resolveFile(StringView file,
                                                          const FileLocation& location);
    struct TargetFiles {
        std::vector<String> names;
        bool searchable{};
        std::ptrdiff_t engine_begin{-1};
        std::ptrdiff_t engine_end{-1};
    };
    [[nodiscard]] TargetFiles collectTargets(StringView directory, bool force_refresh) const;
    [[nodiscard]] TargetFiles collectTargets(const std::vector<String>& directories,
        bool all_list, bool force_refresh, std::ptrdiff_t skin_pos,
        std::ptrdiff_t engine_from, std::ptrdiff_t engine_to) const;
    struct PreparedRows {
        std::shared_ptr<lib::TableView> table;
        std::vector<String> names;
        std::vector<String> paths;
        std::vector<lib::TableRow> rows;
        std::vector<std::size_t> selection;
        std::vector<Action> loaded;
        bool none_row{};
    };
    [[nodiscard]] std::optional<PreparedRows> prepareRows(std::vector<String> names,
        const FileLocation& location, String selected, std::uint64_t revision) const;
    [[nodiscard]] bool commitRows(PreparedRows& rows, bool all_list, bool searchable,
                    std::ptrdiff_t engine_begin = -1, std::ptrdiff_t engine_end = -1,
                    lib::PreparedItemModelUpdate* companion = nullptr);
    void notifyLoaded(const std::vector<Action>& callbacks, std::uint64_t revision);
    struct RefreshRequest {
        String select;
        bool force_refresh{};
        bool update_list{};
        String material_path;
        bool operator==(const RefreshRequest&) const = default;
    };
    [[nodiscard]] bool beginAsyncRefresh(const RefreshRequest& request);
    [[nodiscard]] bool directorySelectorIsVisible() const;
    void invalidateAsyncRefresh();
    void reapRefreshTask(const std::shared_ptr<RefreshTaskState>& task);
    void refreshPathsImplWithScan(String select, bool force_refresh,
        bool update_list, const ScenarioDirectoryScanResult* scan,
        const RefreshPathSnapshot* snapshot);
    // UIスレッドでの反映用トークン。通知中にこの非共有の所有者が閉じられる場
    // 合がある。 別の表の状態管理は持たず、行・表示位置はlib::replace_table_rows/reveal_table_row
    // に任せる。
    struct PublicationLifetime {
        MaterialSelectBase* owner{};
        std::uint64_t rows_revision{};
        std::uint64_t change_revision{};
        std::uint64_t notification_revision{};
        std::uint64_t audio_revision{};
        std::uint64_t async_refresh_revision{};
        std::optional<RefreshRequest> active_refresh;
        std::optional<RefreshRequest> pending_refresh;
    };
    std::shared_ptr<PublicationLifetime> publication_lifetime_{
        std::make_shared<PublicationLifetime>()};
    struct ChangeGuard {
        std::shared_ptr<PublicationLifetime> lifetime;
        std::uint64_t revision{};
        [[nodiscard]] bool current() const noexcept {
            return lifetime->owner && lifetime->change_revision == revision;
        }
    };
    [[nodiscard]] ChangeGuard beginChange();
    [[nodiscard]] ChangeGuard currentChange() const;
    bool updateSkinMaterialsExtension(const ChangeGuard& change);
    template <class Control, class Callback>
    auto bindInput(const std::shared_ptr<Control>& control,
                   std::shared_ptr<Control> MaterialSelectBase::* member, Callback callback) {
        const std::weak_ptr<PublicationLifetime> weak = publication_lifetime_;
        const std::weak_ptr<Control> source = control;
        return [weak, source, member, callback = std::move(callback)](auto&&... args) {
            const auto state = weak.lock();
            const auto widget = source.lock();
            if (!state || !state->owner || !widget || !state->owner->enabled_ ||
                (state->owner->*member) != widget) return;
            callback(*state->owner, std::forward<decltype(args)>(args)...);
        };
    }
    void bindToolCommand(const std::shared_ptr<lib::ToolBar>& bar, String command,
                         void (MaterialSelectBase::* action)(), bool require_enabled = true);
    bool selectPathImpl(String value, std::ptrdiff_t index, bool store,
                        const ChangeGuard& change);
    bool applyPath(String value, String old, bool refresh_paths, bool update_binary_image,
                   std::ptrdiff_t index, bool store, const ChangeGuard& change);
    void selectDirectory(int selection, const ChangeGuard& change);
    static bool notifyChange(const std::vector<Action>& callbacks, const ChangeGuard& change);
    [[nodiscard]] bool selectedBinaryDirectory() const;
    [[nodiscard]] std::filesystem::path resolveCurrentFile(StringView file) const;
    [[nodiscard]] bool pathIsCurrent(StringView value) const;
    [[nodiscard]] String warningTargetVersion() const;
    [[nodiscard]] std::vector<String> warningFor(StringView file) const;
    [[nodiscard]] bool imageIsCardSized(StringView value) const;
    [[nodiscard]] bool validSelection() const;
    [[nodiscard]] String audioPath() const;
    void playAudio(lib::AudioBus bus);
    void playFromControl();

    MtType type_;
    Props* prop_{};
    Summary* summary_{};
    Services services_{};
    commons::Commons* editor_commons_{};
    std::weak_ptr<const int> editor_commons_lifetime_;
    std::uint64_t ref_version_connection_{};
    std::uint64_t ref_skin_connection_{};
    std::uint64_t ref_menu_connection_{};
    Parent parent_{};
    std::shared_ptr<lib::Panel> panel_{};
    std::shared_ptr<lib::ComboBox> dirs_{};
    std::shared_ptr<lib::TableView> file_list_{};
    std::shared_ptr<lib::ComboBox> file_combo_{};
    std::shared_ptr<lib::Button> directory_button_{};
    std::shared_ptr<lib::Button> refresh_button_{};
    std::shared_ptr<lib::Button> play_button_{};
    std::shared_ptr<lib::Button> stop_button_{};
    std::weak_ptr<lib::ToolBar> play_toolbar_{};
    std::shared_ptr<lib::NumberEdit> volume_edit_{};
    std::shared_ptr<lib::NumberEdit> loop_edit_{};
    std::shared_ptr<lib::NumberEdit> fade_edit_{};
    std::shared_ptr<lib::ComboBox> channel_edit_{};
    std::shared_ptr<timebar::TimeBar> play_bar_{};
    std::shared_ptr<incsearch::IncSearch> search_popup_{};
    std::shared_ptr<lib::Menu> context_menu_{};
    std::shared_ptr<lib::Menu> dirs_context_menu_{};
    std::shared_ptr<undo::UndoManager> undo_{};
    Action store_{};
    Action refresh_callback_{};
    Definitions definitions_{};
    SkinPtr forced_skin_{};
    SkinPtr summary_skin_{};
    bool read_only_{};
    bool enabled_{true};
    bool can_include_{};
    bool is_menu_card_{};
    bool no_card_size_{};
    bool can_continue_{};
    bool continue_bgm_{};
    bool first_set_{true};
    bool loading_{};
    bool all_list_{};
    bool none_row_{};
    bool processing_{};
    mutable bool can_inc_search_{};
    int selected_dir_{-1};
    int table_dir_{-1};
    int engine_dir_{-1};
    std::ptrdiff_t table_index_{-1};
    std::ptrdiff_t engine_index_{-1};
    std::ptrdiff_t engine_index_to_{-1};
    int image_index_{};
    int volume_{100};
    int volume_input_value_{100}; // DのSpinnerのデリゲート用キャッシュ。プログラムによる復元では変更しない。
    int loop_count_{};
    int channel_{};
    int fade_in_ms_{};
    String path_{};
    String temp_path_{};
    String playing_{};
    std::vector<String> binary_paths_{};
    CardImageList paths_{};
    std::vector<DirectoryEntry> directory_entries_{};
    std::vector<String> showing_names_{};
    std::vector<String> showing_paths_{};
    IncrementalSearch inc_search_{};
    std::function<bool()> load_scaled_image_{};
    std::vector<std::shared_ptr<RefreshTaskState>> refresh_tasks_{};
    // プレビュー資源はコールバックの生の所有者でなく、再生処理に属する。
    // 保留中の再生・問い合わせは、選択部品が閉じた後に完了する場合もある。
    struct AudioPlayback {
        std::shared_ptr<lib::AudioBackend> audio;
        std::shared_ptr<lib::MainDispatcher> dispatcher;
        std::vector<std::byte> bytes;
        lib::AudioHandle handle{};
        lib::TimerId timer{};
        bool retired{};
        bool polling{};
        void stop() noexcept;
        ~AudioPlayback() { stop(); }
    };
    std::shared_ptr<AudioPlayback> audio_playback_;
    UpdatePlayBar update_play_bar_{this};
};

template <auto Type, typename D = lib::ComboBox, typename C = lib::TableView>
class MaterialSelect final : public MaterialSelectBase {
public:
    static constexpr MtType material_type = static_cast<MtType>(Type);
    using Base = MaterialSelectBase;
    using Base::Base;
    using Base::canContinue;
    using Base::continueBGM;
    using Base::excludeCardSizeImage;
    using Base::imageIndex;
    using Base::includeImage;
    using Base::path;
    using Base::paths;
    using Base::useNoCardSizeImage;

    MaterialSelect(Parent parent, int style, Props& prop, Summary* summary,
                   SkinPtr skin, bool read_only, Action refresh = {},
                   typename Base::Definitions definitions = {},
                   bool can_include = false, bool is_menu_card = false,
                   std::shared_ptr<undo::UndoManager> undo_manager = {},
                   Action store = {}, Services services = {})
        : Base(material_type, std::move(parent), style, prop, summary,
               std::move(skin), read_only, std::move(refresh),
               std::move(definitions), can_include, is_menu_card,
               std::move(undo_manager), std::move(store), std::move(services)) {}

    MaterialSelect(Props& prop, Summary* summary, SkinPtr skin, bool read_only,
                   Action refresh = {}, typename Base::Definitions definitions = {},
                   bool can_include = false, bool is_menu_card = false,
                   std::shared_ptr<undo::UndoManager> undo_manager = {},
                   Action store = {}, Services services = {})
        : Base(material_type, prop, summary, std::move(skin), read_only,
               std::move(refresh), std::move(definitions), can_include,
               is_menu_card, std::move(undo_manager), std::move(store),
               std::move(services)) {}

    MaterialSelect(Services services, Props& prop, Summary* summary, SkinPtr skin,
                   bool read_only, Action refresh = {},
                   typename Base::Definitions definitions = {},
                   bool can_include = false, bool is_menu_card = false,
                   std::shared_ptr<undo::UndoManager> undo_manager = {},
                   Action store = {})
        : Base(material_type, prop, summary, std::move(skin), read_only,
               std::move(refresh), std::move(definitions), can_include,
               is_menu_card, std::move(undo_manager), std::move(store),
               std::move(services)) {}

    MaterialSelect(Props& prop, Summary& summary, SkinPtr skin, bool read_only,
                   Action refresh = {}, typename Base::Definitions definitions = {},
                   bool can_include = false, bool is_menu_card = false,
                   std::shared_ptr<undo::UndoManager> undo_manager = {},
                   Action store = {}, Services services = {})
        : MaterialSelect(prop, &summary, std::move(skin), read_only,
                         std::move(refresh), std::move(definitions), can_include,
                         is_menu_card, std::move(undo_manager), std::move(store),
                         std::move(services)) {}

    MaterialSelect(Parent parent, int style, Props& prop, Summary& summary,
                   SkinPtr skin, bool read_only, Action refresh = {},
                   typename Base::Definitions definitions = {},
                   bool can_include = false, bool is_menu_card = false,
                   std::shared_ptr<undo::UndoManager> undo_manager = {},
                   Action store = {}, Services services = {})
        : MaterialSelect(std::move(parent), style, prop, &summary,
                         std::move(skin), read_only, std::move(refresh),
                         std::move(definitions), can_include, is_menu_card,
                         std::move(undo_manager), std::move(store),
                         std::move(services)) {}
};

}  // namespace cwxeditor::cwx::editor::gui::dwt::materialselect
