#include "cwxeditor_src/lib/service_defaults.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/materialselect.hpp"

#include "cwxeditor_src/core/cwx/imagesize.hpp"
#include "cwxeditor_src/core/cwx/utils.hpp"
#include "cwxeditor_src/core/cwx/versioninfo.hpp"
#include "cwxeditor_src/core/cwx/warning.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dskin.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/commons.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dutils.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/sound.hpp"
#include "cwxeditor_src/lib/ui_actions.hpp"
#include "cwxeditor_src/lib/file_picker.hpp"
#include "cwxeditor_src/lib/storage.hpp"
#include "cwxeditor_src/lib/window_host.hpp"
#include "cwxeditor_src/lib/audio_loop.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <exception>
#include <limits>
#include <map>
#include <mutex>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace cwxeditor::cwx::editor::gui::dwt::materialselect {

SoundPreviewTicket beginSoundPreview(const std::shared_ptr<lib::AudioBackend>& audio) {
    using WeakBackend = std::weak_ptr<lib::AudioBackend>;
    static std::mutex mutex;
    static std::map<WeakBackend, std::weak_ptr<std::atomic<std::uint64_t>>,
                    std::owner_less<WeakBackend>> revisions;
    if (!audio) return {};
    const std::lock_guard lock(mutex);
    for (auto it = revisions.begin(); it != revisions.end();) {
        if (it->first.expired()) it = revisions.erase(it);
        else ++it;
    }
    auto& weak = revisions[WeakBackend(audio)];
    auto revision = weak.lock();
    if (!revision) {
        revision = std::make_shared<std::atomic<std::uint64_t>>(0);
        weak = revision;
    }
    const auto expected = revision->fetch_add(1) + 1;
    return {std::move(revision), expected};
}

// 共通の素材一覧の両所有者で、分類のアイコン選択を同じにする。
lib::ResourceId materialRowIcon(Props& prop, const MtType type,
                                const StringView file, const Skin* skin,
                                const bool included) {
    switch (type) {
    case MtType::CARD:
    case MtType::BG_IMG:
        return skin && skin->isCardImage(file, false, included)
            ? prop.images().load(StringView{"cards.png"})
            : prop.images().load(StringView{"backs.png"});
    case MtType::BGM:
        return prop.images().load(StringView{"evt_bgm.png"});
    case MtType::SE:
        return prop.images().load(StringView{"evt_se.png"});
    }
    return 0;
}
namespace {

using ::cwxeditor::cwx::imagesize::imageSize;
using ::cwxeditor::cwx::utils::abs2rel;
using ::cwxeditor::cwx::utils::bImgToStr;
using ::cwxeditor::cwx::utils::bSndToStr;
using ::cwxeditor::cwx::utils::isBinImg;
using ::cwxeditor::cwx::utils::isBinSnd;
using ::cwxeditor::cwx::utils::readBinary;
using ::cwxeditor::cwx::utils::strToBSnd;
using ::cwxeditor::cwx::structs::pathFromUtf8;
using ::cwxeditor::cwx::structs::pathToUtf8;

constexpr int portableReadOnlyStyle = 0x00800000;

String lower(StringView value) {
    String result(value);
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char character) {
                       return static_cast<char>(std::tolower(character));
                   });
    return result;
}

bool containsInsensitive(StringView value, StringView pattern) {
    if (pattern.empty()) return true;
    return lower(value).find(lower(pattern)) != String::npos;
}

template <typename Component>
std::shared_ptr<Component> makeComponent(const Services& services,
                                          lib::ComponentKind kind) {
    if (services.gui) {
        if (auto candidate = services.gui->create_component(kind)) {
            if (auto result = std::dynamic_pointer_cast<Component>(candidate)) {
                return result;
            }
        }
    }
    return std::make_shared<Component>();
}

void addToParent(const Parent& parent, const std::shared_ptr<lib::Widget>& child) {
    if (parent && child) parent->add(child);
}

bool sameCardImages(const CardImageList& first, const CardImageList& second) {
    if (first.size() != second.size()) return false;
    for (std::size_t i = 0; i < first.size(); ++i) {
        if (first[i] == second[i]) continue;
        if (!first[i] || !second[i] || !first[i]->opEquals(*second[i])) return false;
    }
    return true;
}

String messageValue(const Props* prop, StringView fallback,
                    const std::function<const String&()>& value) {
    if (!prop) return String(fallback);
    try {
        return value();
    } catch (...) {
        return String(fallback);
    }
}

bool containsMaterial(const Skin& skin, MtType type, StringView directory,
                      bool force_refresh, bool no_card_size, bool included) {
    switch (type) {
    case MtType::CARD: return skin.hasCardImage(directory, force_refresh, no_card_size, included);
    case MtType::BG_IMG: return skin.hasBgImage(directory, force_refresh, no_card_size);
    case MtType::BGM: return skin.hasBGM(directory, force_refresh);
    case MtType::SE: return skin.hasSE(directory, force_refresh);
    }
    return false;
}

struct ScenarioDirectoryScanInput {
    SkinPtr skin_owner;
    const Skin* skin{};
    MtType type{MtType::CARD};
    String scenario_root;
    std::vector<String> ignored_paths;
    bool force_refresh{};
    bool no_card_size{};
    bool included{};
    bool scan_scenario{};
};

struct ScenarioDirectoryScanData {
    std::vector<String> scenario_directories;
};

std::vector<String> searchScenarioMaterials(
    const ScenarioDirectoryScanInput& input, StringView directory,
    const std::size_t cut, const std::atomic_bool& obsolete) {
    std::vector<String> result;
    if (obsolete.load(std::memory_order_acquire) || !input.skin) return result;
    if (containsMaterial(*input.skin, input.type, directory, input.force_refresh,
                         input.no_card_size, input.included)) {
        const String directory_string(directory);
        result.push_back(directory_string.size() <= cut ? String("/")
                                                        : MaterialSelectBase::toViewPath(directory_string.substr(cut)));
    }
    if (obsolete.load(std::memory_order_acquire)) return {};
    std::error_code error;
    const auto root = pathFromUtf8(directory);
    if (!std::filesystem::is_directory(root, error)) return result;
    for (std::filesystem::directory_iterator it(root, error), end;
         !error && it != end && !obsolete.load(std::memory_order_acquire);
         it.increment(error)) {
        if (!it->is_directory(error)) continue;
        const auto name = pathToUtf8(it->path().filename());
        if (::cwxeditor::cwx::utils::containsPath(input.ignored_paths, name)) continue;
        auto nested = searchScenarioMaterials(input, pathToUtf8(it->path()), cut, obsolete);
        result.insert(result.end(), nested.begin(), nested.end());
    }
    return result;
}

ScenarioDirectoryScanData scanScenarioDirectories(
    const ScenarioDirectoryScanInput& input, const std::atomic_bool& obsolete) {
    ScenarioDirectoryScanData result;
    if (obsolete.load(std::memory_order_acquire) || !input.skin) return result;
    if (!input.scan_scenario) return result;
    std::size_t cut = input.scenario_root.size();
    if (!input.scenario_root.empty() && input.scenario_root.back() != '/' &&
        input.scenario_root.back() != '\\') ++cut;
    result.scenario_directories = searchScenarioMaterials(
        input, input.scenario_root, cut, obsolete);
    return result;
}

}  // namespace

struct MaterialSelectBase::RefreshTaskState {
    std::shared_ptr<lib::TaskHandle> handle;
    std::atomic_bool obsolete{};
    std::atomic_bool reap_scheduled{};
    std::atomic_bool handle_ready{};
    std::atomic_bool completion_posted{};
    std::atomic_bool started{};
    std::mutex failure_mutex;
    std::exception_ptr publication_failure;
    std::uint64_t async_revision{};
    std::uint64_t rows_revision{};
};

void MaterialSelectBase::scheduleRefreshTaskReap(
    const std::shared_ptr<RefreshTaskState>& task,
    const std::shared_ptr<lib::MainDispatcher>& dispatcher,
    std::function<void()> on_reaped) {
    if (!task) return;
    if (!task->handle) {
        if (!task->handle_ready.load(std::memory_order_acquire)) {
            task->completion_posted.store(true, std::memory_order_release);
            return;
        }
        if (on_reaped) on_reaped();
        return;
    }
    if (task->handle->finished()) {
        task->handle->wait();
        task->handle.reset();
        task->reap_scheduled.store(false, std::memory_order_release);
        if (on_reaped) on_reaped();
        return;
    }
    // 開始済みの走査は、キャンセルを検出した後に必ず完了を通知する。
    // ファイル操作中に繰り返し確認するのではなく、その完了を待つ。
    if (task->started.load(std::memory_order_acquire) &&
        !task->completion_posted.load(std::memory_order_acquire)) return;
    if (!dispatcher) return;
    bool expected = false;
    if (!task->reap_scheduled.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel)) return;
    try {
        const auto timer = dispatcher->post_delayed(std::chrono::milliseconds(10),
            [task, on_reaped = std::move(on_reaped)]() mutable {
                task->reap_scheduled.store(false, std::memory_order_release);
                // 画面なしのサービスの MainDispatcher
                // は、遅延処理をその場で実行する場合がある。 未完了の
                // ハンドルが同期的な再帰でスタックを使い切らないよう、
                // 確認は一回だけにする。 所有者が有効なら次の更新で再
                // 試行し、破棄後はワーカーが終了まで状態を保持する。
                if (!task->handle || !task->handle->finished()) return;
                task->handle->wait();
                task->handle.reset();
                if (on_reaped) on_reaped();
            });
        if (timer == 0U)
            task->reap_scheduled.store(false, std::memory_order_release);
    } catch (...) {
        task->reap_scheduled.store(false, std::memory_order_release);
    }
}

std::filesystem::path materialDirectoryToOpen(MaterialDirectoryQuery query) {
    if (!query.selected_directory.empty()) {
        if (query.selected_directory.is_absolute()) return query.selected_directory;
        if (!query.scenario_root.empty()) return query.scenario_root / query.selected_directory;
        return query.selected_directory;
    }
    if (!query.default_directories.empty()) {
        for (const auto& directory : query.default_directories) {
            std::error_code error;
            if (std::filesystem::is_directory(directory, error) && !error &&
                (!query.contains_material || query.contains_material(directory))) return directory;
        }
        // どの標準ディレクトリにも利用可能な素材がない場合も、D版の動作に合わせる。
        return query.default_directories.front();
    }
    if (query.scenario_root.empty()) return {};
    if (!query.material_directory.empty()) {
        const auto directory = query.scenario_root / query.material_directory;
        std::error_code error;
        if (std::filesystem::is_directory(directory, error) && !error) return directory;
    }
    return query.scenario_root;
}

PlayingOptionsControls createPlayingOptionsControls(
    Parent parent, const Props* prop, const MtType type,
    const bool select_channel, const bool table_layout) {
    PlayingOptionsControls controls;
    controls.panel = std::make_shared<lib::Panel>();
    auto grid = std::make_shared<lib::GridLayout>();
    grid->columns = select_channel ? 3U : 2U;
    grid->padding = {};
    grid->spacing = 4.0F;
    controls.panel->set_layout(grid);
    if (parent) parent->add(controls.panel);
    std::size_t column{};
    const auto section = [&](const String& title) -> Parent {
        Parent result;
        if (table_layout) {
            auto group = std::make_shared<lib::GroupBox>();
            group->title = title;
            result = group;
        } else result = std::make_shared<lib::Panel>();
        auto row = std::make_shared<lib::LinearLayout>(lib::Orientation::Horizontal);
        // 単位ラベルと編集時のフォーカス枠が、グループの枠に重ならないようにする。
        row->padding = table_layout ? lib::Insets{0.0F, 3.0F, 2.0F, 3.0F}
                                    : lib::Insets{};
        row->spacing = 2.0F;
        row->cross_axis = lib::Align::Center;
        result->set_layout(row);
        result->set_layout_item({.row = 0U, .column = column++,
                                .horizontal = lib::Align::Stretch,
                                .vertical = lib::Align::Stretch});
        controls.panel->add(result);
        if (!table_layout) result->add(std::make_shared<lib::Label>(title + ":"));
        return result;
    };
    if (select_channel) {
        const auto group = section(prop ? prop->requiredMsgs().playingChannel.value : "チャンネル");
        controls.channel = std::make_shared<lib::ComboBox>();
        controls.channel->items = {
            {prop ? prop->requiredMsgs().mainChannel.value : "主音声", "0"},
            {prop ? prop->requiredMsgs().subChannel.value : "副音声", "1"}};
        controls.channel->selected_index = 0U;
        group->add(controls.channel);
    }
    const auto volume = section(prop ? prop->requiredMsgs().playingVolume.value : "音量");
    controls.volume = std::make_shared<lib::NumberEdit>();
    controls.volume->minimum = 0;
    controls.volume->maximum = 100;
    controls.volume->value = 100;
    volume->add(controls.volume);
    volume->add(std::make_shared<lib::Label>(
        prop ? prop->requiredMsgs().playingVolumePer.value : "%"));
    const auto loops = section(prop ? prop->requiredMsgs().loopCount.value : "ループ回数");
    controls.loops = std::make_shared<lib::NumberEdit>();
    controls.loops->minimum = type == MtType::BGM ? 0 : 1;
    controls.loops->maximum = std::max(controls.loops->minimum,
        prop ? static_cast<double>(prop->var().loopCountMax.value) : 100.0);
    controls.loops->value = controls.loops->minimum;
    loops->add(controls.loops);
    if (type == MtType::BGM) loops->add(std::make_shared<lib::Label>(
        prop ? prop->requiredMsgs().loopCountHint.value : "(0 = 無限)"));
    return controls;
}

Services MaterialSelectBase::resolveServices(const Props& prop, Services supplied) {
    return lib::with_service_defaults(prop.images().services(), std::move(supplied));
}

std::shared_ptr<lib::Panel> MaterialSelectBase::makePanel(const Services& services,
                                                           Parent parent) {
    auto result = makeComponent<lib::Panel>(services, lib::ComponentKind::Panel);
    addToParent(parent, result);
    return result;
}

std::shared_ptr<lib::Button> MaterialSelectBase::makeButton(const Services& services,
                                                             String text) {
    auto result = makeComponent<lib::Button>(services, lib::ComponentKind::Button);
    result->text = std::move(text);
    return result;
}

std::shared_ptr<lib::NumberEdit> MaterialSelectBase::makeNumber(
    const Services& services) {
    return makeComponent<lib::NumberEdit>(services, lib::ComponentKind::NumberEdit);
}

std::shared_ptr<lib::ComboBox> MaterialSelectBase::makeCombo(const Services& services) {
    return makeComponent<lib::ComboBox>(services, lib::ComponentKind::ComboBox);
}

std::shared_ptr<lib::TableView> MaterialSelectBase::makeTable(const Services& services) {
    return makeComponent<lib::TableView>(services, lib::ComponentKind::TableView);
}

std::shared_ptr<lib::ProgressBar> MaterialSelectBase::makeProgress(
    const Services& services) {
    return makeComponent<lib::ProgressBar>(services, lib::ComponentKind::ProgressBar);
}

Parent MaterialSelectBase::controlParent(Parent requested) {
    if (requested) return requested;
    if (!panel_) {
        panel_ = makePanel(services_, parent_);
        auto layout = std::make_shared<lib::LinearLayout>(
            lib::Orientation::Vertical);
        layout->spacing = 3.0F;
        panel_->set_layout(layout);
    }
    return panel_;
}

MaterialSelectBase::MaterialSelectBase(
    MtType type, Parent parent, int style, Props& prop, Summary* summary,
    SkinPtr force_skin, bool read_only, Action refresh, Definitions definitions,
    bool can_include, bool is_menu_card,
    std::shared_ptr<undo::UndoManager> undo_manager, Action store, Services services)
    : type_(type), prop_(&prop), summary_(summary),
      services_(resolveServices(prop, std::move(services))), parent_(std::move(parent)),
      undo_(std::move(undo_manager)), store_(std::move(store)),
      refresh_callback_(std::move(refresh)), definitions_(std::move(definitions)),
      forced_skin_(std::move(force_skin)), read_only_(read_only ||
                                                       ((style & portableReadOnlyStyle) != 0)),
      can_include_(can_include), is_menu_card_(is_menu_card),
      loop_count_(type == MtType::BGM ? 0 : 1), update_play_bar_(this) {
    publication_lifetime_->owner = this;
    if (forced_skin_) {
        summary_skin_ = forced_skin_;
    } else if (summary_ && !summary_->type().empty()) {
        summary_skin_ = dskin::findSkin2(prop, summary_->type(), summary_->skinName());
    }
    if (!summary_skin_) {
        summary_skin_ = dskin::findSkin2(prop, prop.var().defaultSkin.value,
                                          prop.var().defaultSkinName.value);
    }
    if (type_ == MtType::CARD) {
        paths_.push_back(std::make_shared<CardImage>(String{},
                                                     types::CardImagePosition::Default));
        binary_paths_.emplace_back();
    } else if (type_ == MtType::BG_IMG) {
        binary_paths_.emplace_back();
    }
}

MaterialSelectBase::MaterialSelectBase(
    MtType type, Props& prop, Summary* summary, SkinPtr force_skin, bool read_only,
    Action refresh, Definitions definitions, bool can_include, bool is_menu_card,
    std::shared_ptr<undo::UndoManager> undo_manager, Action store, Services services)
    : MaterialSelectBase(type, Parent{}, 0, prop, summary, std::move(force_skin),
                         read_only, std::move(refresh), std::move(definitions),
                         can_include, is_menu_card, std::move(undo_manager),
                         std::move(store), std::move(services)) {}

MaterialSelectBase::~MaterialSelectBase() {
    if (search_popup_) { search_popup_->modEvent.clear(); search_popup_.reset(); }
    disconnectEditorCommons();
    publication_lifetime_->owner = nullptr;
    ++publication_lifetime_->async_refresh_revision;
    ++publication_lifetime_->rows_revision;
    for (const auto& task : refresh_tasks_) {
        if (!task) continue;
        task->obsolete.store(true, std::memory_order_release);
        if (task->handle && !task->handle->finished()) task->handle->cancel();
        MaterialSelectBase::scheduleRefreshTaskReap(task, services_.dispatcher);
    }
    refresh_tasks_.clear();
    stopAudio();
    if (services_.drag_drop) {
        if (const auto control = fileControl()) services_.drag_drop->unbind(control);
    }
    if (dirs_) {
        dirs_->on_selection_changed = {};
        dirs_->on_pointer = {};
    }
    if (channel_edit_) channel_edit_->on_selection_changed = {};
    if (volume_edit_) volume_edit_->on_changed = {};
    if (loop_edit_) loop_edit_->on_changed = {};
    if (fade_edit_) fade_edit_->on_changed = {};
    if (file_list_) {
        file_list_->on_selection_changed = {};
        file_list_->on_cell_activate = {};
        file_list_->on_pointer = {};
        file_list_->on_text_input = {};
        file_list_->on_key_down = {};
    }
    if (file_combo_) {
        file_combo_->on_selection_changed = {};
        file_combo_->on_pointer = {};
        file_combo_->on_text_input = {};
        file_combo_->on_key_down = {};
    }
    if (context_menu_) context_menu_->on_command = {};
    if (dirs_context_menu_) dirs_context_menu_->on_command = {};
}

std::shared_ptr<lib::ComboBox> MaterialSelectBase::createDirsCombo(Parent parent) {
    auto target = controlParent(std::move(parent));
    dirs_ = makeCombo(services_);
    const auto combo = dirs_;
    const std::weak_ptr<PublicationLifetime> weak = publication_lifetime_;
    const std::weak_ptr<lib::ComboBox> control = combo;
    dirs_->editable = false;
    // 読み取り専用では操作欄を無効にし、有効状態の更新時も読み込み中は操作できないようにする。
    dirs_->set_enabled(enabled_ && !read_only_ && !loading_);
    dirs_->on_selection_changed = [weak, control](std::optional<std::size_t> value) {
        const auto state = weak.lock();
        const auto widget = control.lock();
        if (!state || !state->owner || !widget || state->owner->dirs_ != widget ||
            !state->owner->enabled_ || state->owner->read_only_ || !value) return;
        state->owner->selectDir(static_cast<int>(*value));
    };
    dirs_->on_pointer = [weak, control](lib::PointerEvent& event) {
        const auto state = weak.lock();
        const auto widget = control.lock();
        if (!state || !state->owner || !widget) return;
        auto* const self = state->owner;
        if (self->dirs_ != widget || !self->enabled_ || self->read_only_ ||
            event.button != lib::PointerButton::Secondary ||
            event.action != lib::PointerAction::Up || !self->services_.gui) return;

        const auto change = self->currentChange();
        const auto rows_revision = change.lifetime->rows_revision;
        const auto gui = self->services_.gui;
        const auto position = event.position;
        auto* const props = self->prop_;
        const auto icon = props
            ? props->images().menu(types::MenuID::IncSearch)
            : lib::ResourceId{};
        if (!change.current() || change.lifetime->rows_revision != rows_revision ||
            state->owner != self || self->dirs_ != widget) return;
        const auto text = props
            ? props->buildMenu(types::MenuID::IncSearch)
            : String{"Incremental search"};
        if (!change.current() || change.lifetime->rows_revision != rows_revision ||
            state->owner != self || self->dirs_ != widget) return;
        const bool searchable = self->canIncSearch();
        auto menu = std::make_shared<lib::Menu>(true);
        menu->items.push_back({text, "incremental-search", {}, icon, searchable});
        const std::weak_ptr<PublicationLifetime> menu_lifetime = self->publication_lifetime_;
        const std::weak_ptr<lib::ComboBox> menu_control = widget;
        const std::weak_ptr<lib::Menu> menu_source = menu;
        menu->on_command = [menu_lifetime, menu_control, menu_source](StringView command) {
            if (command != "incremental-search") return;
            const auto current = menu_lifetime.lock();
            const auto control = menu_control.lock();
            const auto shown = menu_source.lock();
            if (!current || !current->owner || !control || !shown) return;
            auto* const owner = current->owner;
            // コマンドは行のスナップショットではなく現在の一覧を対象にする
            // 。 寿命・所有者・ウィジェット・ポップアップの識別確認を維持
            // し、 途中に更新があった場合は現在の操作条件を再検査する。
            if (owner->dirs_ != control || owner->dirs_context_menu_ != shown ||
                !owner->enabled_ || owner->read_only_) return;
            const auto item = std::find_if(shown->items.begin(), shown->items.end(),
                [](const lib::MenuItem& entry) {
                    return entry.command == "incremental-search";
                });
            if (item == shown->items.end() || !owner->canIncSearch()) return;
            owner->startIncSearch();
        };
        if (!change.current() || change.lifetime->rows_revision != rows_revision ||
            state->owner != self || self->dirs_ != widget) return;
        event.handled = true;
        self->dirs_context_menu_ = menu;
        gui->show_context_menu(menu, widget, position);
    };
    dirs_->set_layout_item({.horizontal = lib::Align::Stretch,
                            .grow = 1.0F});
    addToParent(target, dirs_);
    refreshPaths({}, false, false);
    return combo;
}

std::shared_ptr<lib::TableView> MaterialSelectBase::createFileList(Parent parent) {
    // 提供処理が作成中にこの選択欄を破棄する場合がある。 生成処理
    // が結果を保持するため、戻った後は所有者のメンバーを読まない。
    return std::dynamic_pointer_cast<lib::TableView>(
        createFileControl(std::move(parent), false));
}

std::shared_ptr<lib::ComboBox> MaterialSelectBase::createFileCombo(Parent parent) {
    return std::dynamic_pointer_cast<lib::ComboBox>(
        createFileControl(std::move(parent), true));
}

lib::WidgetPtr MaterialSelectBase::fileControl() const noexcept {
    if (file_combo_) return file_combo_;
    return file_list_;
}

lib::WidgetPtr MaterialSelectBase::createFileControl(Parent parent, const bool combo) {
    if (services_.drag_drop) {
        if (const auto old_control = fileControl()) services_.drag_drop->unbind(old_control);
    }
    if (search_popup_) { search_popup_->modEvent.clear(); search_popup_.reset(); }
    if (context_menu_) { context_menu_->on_command = {}; context_menu_.reset(); }
    auto target = controlParent(std::move(parent));
    if (file_list_) {
        file_list_->on_selection_changed = {};
        file_list_->on_cell_activate = {};
        file_list_->on_pointer = {};
        file_list_->on_text_input = {};
        file_list_->on_key_down = {};
    }
    if (file_combo_) {
        file_combo_->on_selection_changed = {};
        file_combo_->on_pointer = {};
        file_combo_->on_text_input = {};
        file_combo_->on_key_down = {};
    }
    file_combo_.reset();
    file_list_ = makeTable(services_);
    const auto table = file_list_;
    const auto lifetime = publication_lifetime_;
    const std::weak_ptr<PublicationLifetime> weak = lifetime;
    const std::weak_ptr<lib::TableView> model = table;
    if (combo) {
        file_combo_ = makeCombo(services_);
        file_combo_->editable = false;
        file_combo_->set_enabled(enabled_ && !read_only_ && !loading_);
        file_combo_->visible_item_count = prop_
            ? static_cast<std::size_t>(std::max<std::int32_t>(1, prop_->var().comboVisibleItemCount.value))
            : 0U;
    }
    const auto control = fileControl();
    const std::weak_ptr<lib::Widget> weak_control = control;
    const auto owner = [weak, model]() -> MaterialSelectBase* {
        const auto state = weak.lock();
        const auto widget = model.lock();
        if (!state || !state->owner || !widget || state->owner->file_list_ != widget ||
            !state->owner->enabled_ || state->owner->read_only_) return nullptr;
        return state->owner;
    };
    file_list_->columns.push_back({String{}, 1.0F, lib::TextAlignment::Start, true, false});
    file_list_->header_visible = false;
    file_list_->set_enabled(enabled_ && !read_only_ && !loading_);
    file_list_->on_selection_changed = [owner](const std::vector<std::size_t>&) {
        if (const auto self = owner()) self->selectFile();
    };
    file_list_->on_cell_activate = [owner](std::size_t, std::size_t) {
        const auto self = owner();
        if (!self) return;
        if (self->type_ == MtType::BGM) {
            self->playBGM(false);
        } else if (self->type_ == MtType::SE) {
            self->playSE();
        } else {
            self->openFilePath();
        }
    };
    if (file_combo_) {
        const std::weak_ptr<lib::ComboBox> combo_control = file_combo_;
        file_combo_->on_selection_changed = [weak, model, combo_control] (
                const std::optional<std::size_t> index) {
            const auto state = weak.lock();
            const auto table = model.lock();
            const auto combo = combo_control.lock();
            if (!state || !state->owner || !table || !combo ||
                state->owner->file_list_ != table || state->owner->file_combo_ != combo ||
                !state->owner->enabled_ || state->owner->read_only_) return;
            std::vector<std::size_t> selection;
            if (index && *index < table->rows.size()) selection.push_back(*index);
            // Combo は同じ行モデルの別の表示なので、先頭
            // の「なし」の行も、具体的な行と同じ selectFile
            // 経路で選択する。 ここでは表の通知を抑止し
            // てから、その経路を明示的に呼ぶ。 ユーザーの
            // 選択イベントは Combo が管理し、対応す
            // る表から二度目の再帰的な選択通知を出さない。
            (void)lib::select_table_rows(*table, std::move(selection), false);
            if (index && *index < table->rows.size()) state->owner->selectFile();
        };
    }
    if (control) {
        control->on_pointer = [weak, weak_control](lib::PointerEvent& event) {
            const auto state = weak.lock();
            const auto widget = weak_control.lock();
            if (!state || !state->owner || !widget ||
                state->owner->fileControl() != widget) return;
            state->owner->handleFilePointer(widget, event);
        };
        if (file_combo_) {
            file_combo_->set_layout_item({.horizontal = lib::Align::Stretch,
                                          .vertical = lib::Align::Center,
                                          .grow = 1.0F});
        } else {
            file_list_->set_layout_item({.horizontal = lib::Align::Stretch,
                                         .vertical = lib::Align::Stretch,
                                         .minimum = {
                                             prop_ ? static_cast<float>(prop_->var().filesWidth.value)
                                                   : 150.0F,
                                             prop_ ? static_cast<float>(prop_->var().filesHeight.value)
                                                   : 150.0F},
                                         .grow = 1.0F});
        }
        addToParent(target, control);
    }
    createIncrementalSearch();
    addDropTarget();
    if (!lifetime->owner || file_list_ != table || fileControl() != control) return control;
    refreshFileListMenu();
    if (!lifetime->owner || file_list_ != table || fileControl() != control) return control;
    refreshPaths({}, false, true);
    return control;
}

void MaterialSelectBase::handleFilePointer(const lib::WidgetPtr& control,
                                           lib::PointerEvent& event) {
    if (event.button != lib::PointerButton::Secondary ||
        event.action != lib::PointerAction::Up || !services_.gui ||
        !enabled_ || read_only_) return;
    const auto change = currentChange();
    const auto rows_revision = change.lifetime->rows_revision;
    const auto gui = services_.gui;
    const auto position = event.position;
    event.handled = true;
    refreshFileListMenu();
    if (!change.current() || change.lifetime->rows_revision != rows_revision ||
        fileControl() != control) return;
    const auto menu = context_menu_;
    gui->show_context_menu(menu, control, position);
}

void MaterialSelectBase::refreshFileListMenu() {
    if (!file_list_ || !fileControl()) return;
    const auto change = currentChange();
    const auto revision = change.lifetime->rows_revision;
    const auto audio_revision = change.lifetime->audio_revision;
    const auto current = [&] {
        return change.current() && change.lifetime->rows_revision == revision &&
            change.lifetime->audio_revision == audio_revision;
    };
    const auto selected_file = filePath();
    const bool has_file = !selected_file.empty();
    if (!current()) return;
    auto menu = std::make_shared<lib::Menu>(true);
    const auto properties = prop_;
    // 画像の復号がホストへ処理を戻し、この選択欄を破棄・置換する場合
    // がある。 その寿命の境界を越えて、メニューの反映を続けない。
    const auto append = [&](types::MenuID id, String command, bool enabled,
                            std::optional<String> caption = std::nullopt) {
        if (!current()) return false;
        auto text = caption ? std::move(*caption)
                            : properties ? properties->buildMenu(id) : String{};
        if (!current()) return false;
        const auto icon = properties ? properties->images().menu(id) : lib::ResourceId{};
        if (!current()) return false;
        menu->items.push_back({std::move(text), std::move(command), {}, icon, enabled});
        return true;
    };
    const auto separator = [&] {
        lib::MenuItem item;
        item.separator = true;
        menu->items.push_back(std::move(item));
    };
    if (!append(types::MenuID::IncSearch, "incremental-search", canIncSearch())) return;
    if (undo_) {
        separator();
        if (!append(types::MenuID::Undo, "undo", !read_only_ && undo_->canUndo())) return;
        if (!append(types::MenuID::Redo, "redo", !read_only_ && undo_->canRedo())) return;
    }
    separator();
    if (!append(types::MenuID::OpenAtFileView, "open-file",
                has_file && !read_only_ && liveEditorCommons())) return;
    if (!append(types::MenuID::CopyFilePath, "copy-file-path", has_file)) return;
    if (type_ == MtType::CARD && can_include_ && !read_only_ && summary_ && summary_->legacy()) {
        separator();
        const auto skin = summSkin();
        if (!current()) return;
        if (!append(types::MenuID::IncludeImage, "include-image",
                    has_file && skin && skin->canInclude(selected_file))) return;
    }
    if (type_ == MtType::BGM) {
        separator();
        // コンテキストメニューの操作と見出しは、ツールバー
        // と同じ、選択曲と再生中の曲の判定に従う。 停止
        // のラベルから、別の選択曲の再生操作を呼ばない。
        const bool play = has_file && (playing_.empty() ||
            !::cwxeditor::cwx::utils::cfnmatch(normalizePath(selected_file),
                                              normalizePath(playing_)));
        if (!current()) return;
        const bool stop = !playing_.empty() && !play;
        const auto id = stop ? types::MenuID::StopBGM : types::MenuID::PlayBGM;
        String text = prop_ ? prop_->buildMenu(id) : String{stop ? "Stop" : "Play"};
        if (!current()) return;
        if (stop && prop_)
            text = ::cwxeditor::cwx::utils::tryFormat(text, baseName(playing_));
        if (!append(id, "play-bgm", stop || has_file, std::move(text))) return;
    } else if (type_ == MtType::SE) {
        separator();
        auto text = prop_ ? prop_->requiredMsgs().menuText(types::MenuID::PlaySE) : String{"Play"};
        if (!current() || !append(types::MenuID::PlaySE, "play-se", has_file, std::move(text))) return;
        text = prop_ ? prop_->requiredMsgs().menuText(types::MenuID::StopSE) : String{"Stop"};
        if (!current() || !append(types::MenuID::StopSE, "stop-se", !playing_.empty(), std::move(text))) return;
    }
    const std::weak_ptr<PublicationLifetime> weak = change.lifetime;
    const std::weak_ptr<lib::Menu> source = menu;
    menu->on_command = [weak, source, revision, mutation = change.revision](StringView command) {
        const auto state = weak.lock();
        const auto shown = source.lock();
        if (!state || !state->owner || !shown || state->owner->context_menu_ != shown ||
            state->rows_revision != revision || state->change_revision != mutation ||
            !state->owner->enabled_) return;
        const auto self = state->owner;
        const auto item = std::find_if(shown->items.begin(), shown->items.end(),
            [&](const lib::MenuItem& entry) { return entry.command == command; });
        if (item == shown->items.end() || !item->enabled) return;
        const auto undo = self->undo_;
        if (command == "incremental-search") self->startIncSearch();
        else if (command == "undo" && undo) undo->undo();
        else if (command == "redo" && undo) undo->redo();
        else if (command == "open-file") self->openFilePath();
        else if (command == "copy-file-path") self->copyFilePath();
        else if (command == "include-image") self->includeImage();
        else if (command == "play-bgm") self->playBGM(false);
        else if (command == "play-se") self->playSE();
        else if (command == "stop-se") self->stopSE();
    };
    if (current()) context_menu_ = std::move(menu);
}

void MaterialSelectBase::stopBGM() {
    if (type_ != MtType::BGM) return;
    bgmStopped();
}

void MaterialSelectBase::canContinue(bool value) {
    if (can_continue_ == value) return;
    can_continue_ = value;
    if (dirs_ && file_list_) refreshPaths();
}

void MaterialSelectBase::continueBGM(bool value) {
    if (type_ != MtType::BGM || continue_bgm_ == value) return;
    const auto change = beginChange();
    const auto old_path = path();
    if (!change.current()) return;
    continue_bgm_ = value;
    if (continue_bgm_) {
        if (!applyPath({}, old_path, true, true, -1, true, change)) return;
        const auto index = continueIndex();
        if (!change.current()) return;
        if (index >= 0) selectDirectory(static_cast<int>(index), change);
    } else if (old_path.empty()) {
        selectDirectory(0, change);
    }
}

void MaterialSelectBase::useNoCardSizeImage(bool value) {
    if (type_ != MtType::CARD || no_card_size_ == value) return;
    const auto change = beginChange();
    no_card_size_ = value;
    const auto selected = path();
    if (!change.current()) return;
    const auto file = filePath();
    if (!change.current()) return;
    if (!no_card_size_ && !selected.empty() && !isBinImg(file)) {
        // D版と同様、先に参照画像を解決し、壊れた参照は変更しな
        // い。 寸法によって拒否できるのは、解決できた画像だけ。
        bool resolved = false;
        if (const auto skin = summSkin()) {
            const auto version = summary_ ? summary_->dataVersion()
                                          : String(versioninfo::LATEST_VERSION);
            const auto image = skin->findImagePath(selected,
                summary_ ? summary_->scenarioPath() : String{}, version);
            if (!change.current()) return;
            resolved = !image.empty();
        } else {
            const auto image = resolveCurrentFile(selected);
            std::error_code error;
            resolved = !image.empty() && std::filesystem::is_regular_file(image, error) && !error;
        }
        if (resolved && !imageIsCardSized(selected) &&
            !applyPath({}, selected, false, false, image_index_, false, change)) return;
    }
    if (!notifyChange(updateNoCardSize, change)) return;
    refresh();
}

void MaterialSelectBase::includeImage() {
    if (type_ != MtType::CARD || !can_include_ || read_only_) return;
    const auto change = beginChange();
    const auto file = filePath();
    if (!change.current()) return;
    if (file.empty() || !services_.dialogs || !summary_ || !summary_->legacy() ||
        !summSkin() || !summSkin()->canInclude(file)) return;
    const auto dialogs = services_.dialogs;
    const auto selected = path();
    if (!change.current()) return;
    const auto& messages = prop_->requiredMsgs();
    lib::MessageDialogRequest request;
    request.title = messages.dlgTitQuestion.value;
    request.message = ::cwxeditor::cwx::utils::tryFormat(
        messages.dlgMsgIncludeImage.value, selected);
    request.severity = lib::MessageSeverity::Question;
    request.buttons = lib::MessageButtons::YesNo;
    const auto result = dialogs->message(request);
    if (!change.current() || result != lib::MessageResult::Yes) return;
    String encoded;
    try {
        encoded = bImgToStr(readBinary(pathFromUtf8(file)));
    } catch (...) {
        return;
    }
    const auto binary_index = indexOfBinPath;
    const int selection = binary_index ? binary_index(true) : -1;
    if (!change.current()) return;
    if (image_index_ < 0 || static_cast<std::size_t>(image_index_) >= binary_paths_.size()) return;
    binary_paths_[static_cast<std::size_t>(image_index_)] = std::move(encoded);
    refreshPaths();
    if (!change.current()) return;
    if (selection >= 0) selectDirectory(selection, change);
    if (!change.current()) return;
    (void)notifyChange(includeEvent, change);
}

void MaterialSelectBase::excludeCardSizeImage(bool value) {
    if (type_ != MtType::BG_IMG || !prop_) return;
    if (prop_->var().excludeCardSizeImage.value == value) return;
    const auto change = beginChange();
    prop_->var().excludeCardSizeImage.value = value;
    const auto selected = path();
    if (!change.current()) return;
    const auto file = filePath();
    if (!change.current()) return;
    if (value && !selected.empty() && !isBinImg(file) && imageIsCardSized(selected)) {
        if (!applyPath({}, selected, false, false, -1, false, change)) return;
    }
    refresh();
}

bool MaterialSelectBase::excludeCardSizeImage() const {
    return type_ == MtType::BG_IMG && prop_ && prop_->var().excludeCardSizeImage.value;
}

std::vector<String> MaterialSelectBase::warnings() const {
    std::vector<String> result;
    if (!prop_ || !summary_) return result;
    const bool unsupported = !prop_->isTargetVersion(summary_, "1");
    if (!unsupported) return result;
    const auto selected = path();
    const auto& messages = prop_->requiredMsgs();
    if ((type_ == MtType::BGM || type_ == MtType::SE) && channel_ != 0 &&
        (!selected.empty() || type_ == MtType::BGM)) {
        result.push_back(messages.warningChannel.value);
    }
    if ((type_ == MtType::BGM || type_ == MtType::SE) && fade_in_ms_ != 0 &&
        !selected.empty()) {
        result.push_back(messages.warningFadeIn.value);
    }
    if ((type_ == MtType::BGM || type_ == MtType::SE) && volume_ != 100 &&
        !selected.empty()) {
        result.push_back(messages.warningVolume.value);
    }
    const int default_loop = type_ == MtType::BGM ? 0 : 1;
    if ((type_ == MtType::BGM || type_ == MtType::SE) && loop_count_ != default_loop &&
        !selected.empty()) {
        result.push_back(messages.warningLoopCount.value);
    }
    return result;
}

Parent MaterialSelectBase::createPlayingOptions(Parent parent, bool select_channel,
                                                 bool table_layout) {
    auto controls = createPlayingOptionsControls(parent ? parent : parent_, prop_,
                                                 type_, select_channel, table_layout);
    auto result = controls.panel;
    if (!panel_) panel_ = result;
    if (select_channel) {
        channel_edit_ = controls.channel;
        (void)lib::select_combo_item(*channel_edit_,
                                     static_cast<std::size_t>(channel_), false);
        channel_edit_->on_selection_changed = bindInput(channel_edit_, &MaterialSelectBase::channel_edit_,
            [](MaterialSelectBase& self, std::optional<std::size_t> value) {
                if (value && !self.read_only_) self.channel(static_cast<int>(*value));
            });
    }
    volume_edit_ = controls.volume;
    volume_edit_->value = volume_;
    volume_input_value_ = volume_;
    volume_edit_->on_changed = bindInput(volume_edit_, &MaterialSelectBase::volume_edit_,
        [](MaterialSelectBase& self, double value) { if (!self.read_only_ && !self.processing_) self.volumeFromUser(static_cast<int>(std::lround(value))); });
    loop_edit_ = controls.loops;
    loop_edit_->value = loop_count_;
    loop_edit_->on_changed = bindInput(loop_edit_, &MaterialSelectBase::loop_edit_,
        [](MaterialSelectBase& self, double value) { if (!self.read_only_) self.loopCount(static_cast<int>(std::lround(value))); });
    refDataVersion();
    return result;
}

Parent MaterialSelectBase::createFadeIn(Parent parent) {
    auto result = makePanel(services_, parent ? parent : parent_);
    if (!panel_) panel_ = result;
    fade_edit_ = makeNumber(services_);
    fade_edit_->minimum = 0;
    fade_edit_->maximum = prop_ ? static_cast<double>(prop_->var().fadeInMax.value) : 1000.0;
    fade_edit_->value = fade_in_ms_ / 100.0;
    fade_edit_->on_changed = bindInput(fade_edit_, &MaterialSelectBase::fade_edit_,
        [](MaterialSelectBase& self, double value) {
            if (!self.read_only_) self.fadeIn(static_cast<int>(std::lround(value * 100.0)));
        });
    result->add(fade_edit_);
    refDataVersion();
    return result;
}

void MaterialSelectBase::refDataVersion() {
    updateAudioOptionControls(true);
}

void MaterialSelectBase::updateAudioOptionControls(const bool respect_selector_enabled) {
    if (!prop_) return;
    const auto change = currentChange();
    const bool legacy = summary_ && summary_->legacy();
    const bool has_path = !path().empty();
    if (!change.current()) return;
    const bool selector_enabled = !respect_selector_enabled || enabled_;
    if (volume_edit_) volume_edit_->set_enabled(selector_enabled && !read_only_ &&
                                                 (!legacy || volume_ != 100) && has_path);
    const int default_loop = type_ == MtType::BGM ? 0 : 1;
    if (loop_edit_) loop_edit_->set_enabled(selector_enabled && !read_only_ &&
                                             (!legacy || loop_count_ != default_loop) && has_path);
    if (channel_edit_) channel_edit_->set_enabled(selector_enabled && !read_only_ &&
                                                   (!legacy || channel_ != 0) &&
                                                   (has_path || type_ == MtType::BGM));
    if (fade_edit_) fade_edit_->set_enabled(selector_enabled && !read_only_ &&
                                             (!legacy || fade_in_ms_ != 0) &&
                                             (has_path || type_ == MtType::BGM) &&
                                             !continue_bgm_);
}

int MaterialSelectBase::volume() const noexcept {
    return volume_edit_ ? static_cast<int>(std::lround(volume_edit_->value)) : volume_;
}

void MaterialSelectBase::volume(int value) {
    value = std::clamp(value, 0, 100);
    const auto change = currentChange();
    const bool old_processing = processing_;
    processing_ = true;
    volume_ = value;
    if (volume_edit_) volume_edit_->value = value;
    try { refDataVersion(); }
    catch (...) {
        if (change.current()) processing_ = old_processing;
        throw;
    }
    if (change.current()) processing_ = old_processing;
    // D版と同様、プログラムからの音量設定は _processing
    // 中に行う。 取り消しはモデルと操作欄を復元するが、再生中プレ
    // ビューの音量は変えない。 実際の Spinner/NumberEdit
    // の入力経路だけが変更を通知し、再生音量を調整する。
}

void MaterialSelectBase::volumeFromUser(int value) {
    value = std::clamp(value, 0, 100);
    const auto change = currentChange();
    volume(value);
    if (!change.current() || volume_ != value) return;
    // D版のリスナーと同様、最後のユーザー操作の値を独立して記憶する。 取り消し・や
    // り直しが _processing 中に復元した操作欄・モデルの値とは分ける。
    if (volume_input_value_ == value) return;
    volume_input_value_ = value;
    notifyModified();
    if (!change.current() || volume_ != value) return;
    const auto playback = audio_playback_;
    if (playback && !playback->retired && !playing_.empty()) {
        if (type_ == MtType::BGM) {
            const auto global = prop_
                ? std::clamp(prop_->var().bgmVolume.value, 0, 100) : 100;
            // D版と同様、全体音量と個別音量を掛けたプレビュー音量を、共通のBGM
            // 系統に渡す。 その後の設定変更では、全体音量で置き換える。
            const auto effective = (global * value) / 100;
            playback->audio->set_bus_volume(lib::AudioBus::Bgm,
                static_cast<float>(effective) / 100.0F);
        } else {
            playback->audio->set_track_volume(
                playback->handle, static_cast<float>(value) / 100.0F);
        }
    }
}

int MaterialSelectBase::loopCount() const noexcept {
    return loop_edit_ ? static_cast<int>(std::lround(loop_edit_->value)) : loop_count_;
}

void MaterialSelectBase::loopCount(int value) {
    const int minimum = type_ == MtType::BGM ? 0 : 1;
    const int maximum = prop_ ? static_cast<int>(prop_->var().loopCountMax.value) : 100;
    value = std::clamp(value, minimum, std::max(minimum, maximum));
    if (loop_count_ == value && (!loop_edit_ || loop_edit_->value == value)) return;
    processing_ = true;
    loop_count_ = value;
    if (loop_edit_) loop_edit_->value = value;
    processing_ = false;
    const auto change = currentChange();
    refDataVersion();
    if (!change.current()) return;
    notifyModified();
}

void MaterialSelectBase::channel(int value) {
    value = std::clamp(value, 0, 1);
    if (channel_ == value && (!channel_edit_ || channel_edit_->selected_index ==
                              static_cast<std::size_t>(value))) return;
    channel_ = value;
    if (channel_edit_)
        (void)lib::select_combo_item(*channel_edit_, static_cast<std::size_t>(value), false);
    const auto change = currentChange();
    refDataVersion();
    if (!change.current()) return;
    notifyModified();
}

void MaterialSelectBase::fadeIn(int value) {
    // 共通の AreaView/ALL アダプターは、NumberEdit の生成前にモデ
    // ルへ値を設定する場合がある。 D版の fadeIn(int) は Spinner
    // を前提とするため、生成前の値は fadeInModelValue() で別に公開す
    // る。 fadeIn() は、コントロールがなければゼロを返すD版の規則を維持する。
    const int maximum = prop_ ? static_cast<int>(prop_->var().fadeInMax.value) * 100 : 100000;
    value = std::clamp(value, 0, std::max(0, maximum));
    // D版の Spinner と同様、公開の設定関数も100ミリ秒単
    // 位に揃える。 操作欄に現れない細かい精度をモデル内に残さない。
    value = static_cast<int>(std::lround(static_cast<double>(value) / 100.0)) * 100;
    if (fade_in_ms_ == value && (!fade_edit_ || fade_edit_->value == value / 100.0)) return;
    fade_in_ms_ = value;
    if (fade_edit_) fade_edit_->value = value / 100.0;
    const auto change = currentChange();
    refDataVersion();
    if (!change.current()) return;
    notifyModified();
}

std::shared_ptr<timebar::TimeBar> MaterialSelectBase::createPlayingBar(Parent parent) {
    auto target = controlParent(std::move(parent));
    play_bar_ = std::make_shared<timebar::TimeBar>(target.get(), 0,
        services_.system.get(), services_.graphics.get());
    play_bar_->length(std::chrono::milliseconds(0));
    play_bar_->current(std::chrono::milliseconds(0));
    play_bar_->set_enabled(false);
    const std::weak_ptr<PublicationLifetime> weak = publication_lifetime_;
    const std::weak_ptr<lib::GuiBackend> gui = services_.gui;
    const std::weak_ptr<timebar::TimeBar> bar = play_bar_;
    // 再生タイマーは共通の Canvas を更新するため、再描画も通知する。 キャッ
    // シュした状態を描くだけでは、ネイティブ・ALL のフレーム描画が予約されない。
    play_bar_->invalidate = [weak, gui, bar] {
        const auto state = weak.lock();
        const auto control = bar.lock();
        const auto backend = gui.lock();
        if (state && state->owner && control && backend)
            backend->request_redraw(control, std::nullopt);
    };
    play_bar_->updateCurrentEvent.push_back([weak] {
        const auto state = weak.lock();
        if (!state || !state->owner) return;
        const auto owner = state->owner;
        const auto playback = owner->audio_playback_;
        const auto bar = owner->play_bar_;
        if (!playback || playback->retired || !playback->audio || !playback->handle ||
            !bar || bar->length().count() <= 0) return;
        const auto audio = playback->audio;
        const auto handle = playback->handle;
        const auto revision = state->audio_revision;
        audio->seek(handle, bar->current());
        if (!state->owner || state->audio_revision != revision ||
            owner->audio_playback_ != playback || playback->retired ||
            playback->handle != handle) return;
        // 共通バックエンドはハンドルごとのフェード曲線を持つ。 手動のシーク
        // では初期フェードを解除し、指定した再生位置の音を聞けるようにする。
        audio->cancel_fade_in(handle);
    });
    addToParent(target, play_bar_);
    return play_bar_;
}

void MaterialSelectBase::initPlayBar() {
    const auto bar = play_bar_;
    if (!bar) return;
    const bool standalone = type_ == MtType::BGM && !lib::editor_window_host_options().all_mode;
    bar->length(std::chrono::milliseconds(0));
    bar->current(std::chrono::milliseconds(0));
    bar->set_enabled(false);
    if (standalone) bar->clearWarning();
}

void MaterialSelectBase::createPlayToolItem(const std::shared_ptr<lib::ToolBar>& bar) {
    if (!bar) return;
    const auto change = currentChange();
    const String command = type_ == MtType::BGM ? "play-bgm" : "play-se";
    const bool playable = canPlay();
    if (!change.current()) return;
    const auto id = type_ == MtType::BGM ? types::MenuID::PlayBGM
                                         : types::MenuID::PlaySE;
    const String text = prop_ ? prop_->requiredMsgs().menuText(id) : String{"Play"};
    const auto icon = prop_ ? prop_->images().menu(id) : lib::ResourceId{};
    bar->items.push_back({text, command, icon, enabled_ && playable, false,
                          false, false, {}, text});
    play_toolbar_ = bar;
    bindToolCommand(bar, command, &MaterialSelectBase::playFromControl);
}

std::shared_ptr<lib::Button> MaterialSelectBase::createPlayButton(Parent parent) {
    auto target = controlParent(std::move(parent));
    const auto id = type_ == MtType::BGM ? types::MenuID::PlayBGM : types::MenuID::PlaySE;
    String text = prop_ ? prop_->requiredMsgs().menuText(id) : String{"Play"};
    if (type_ == MtType::BGM) {
        // D版のBGMと同様に SWT.TOGGLE の状態を使い、共通ウィジェットモデルで保持する。
        auto toggle = makeComponent<lib::ToggleButton>(
            services_, lib::ComponentKind::ToggleButton);
        toggle->text = std::move(text);
        play_button_ = std::move(toggle);
    } else {
        play_button_ = makeButton(services_, std::move(text));
    }
    if (prop_) {
        play_button_->icon = prop_->images().menu(id);
        play_button_->set_tooltip(prop_->requiredMsgs().menuText(id));
    }
    const auto button = play_button_;
    play_button_->on_click = bindInput(button, &MaterialSelectBase::play_button_,
        [](MaterialSelectBase& self) { self.playFromControl(); });
    addToParent(target, play_button_);
    const auto change = currentChange();
    const bool play = isPlayOrStop();
    if (change.current()) updatePlayButton(play);
    return button;
}

std::shared_ptr<lib::Button> MaterialSelectBase::createStopButton(Parent parent) {
    auto target = controlParent(std::move(parent));
    // D版と同様、効果音には独立した停止ボタンを設ける。 共通のC++ファサー
    // ドは、呼び出し元が明示的に求める独立したBGM停止ボタンにも対応する。
    const auto id = type_ == MtType::BGM ? types::MenuID::StopBGM : types::MenuID::StopSE;
    String text = prop_ ? prop_->requiredMsgs().menuText(id) : String{"Stop"};
    if (type_ == MtType::BGM && prop_)
        text = ::cwxeditor::cwx::utils::tryFormat(text, String{});
    stop_button_ = makeButton(services_, text);
    stop_button_->set_tooltip(std::move(text));
    if (prop_) stop_button_->icon = prop_->images().menu(id);
    stop_button_->on_click = bindInput(stop_button_, &MaterialSelectBase::stop_button_,
        [](MaterialSelectBase& self) { if (self.type_ == MtType::BGM) self.stopBGM(); else self.stopSE(); });
    addToParent(target, stop_button_);
    return stop_button_;
}

bool MaterialSelectBase::canPlay() const {
    if (type_ == MtType::BGM) return !playing_.empty() || !filePath().empty();
    if (type_ == MtType::SE) return !filePath().empty();
    return false;
}

bool MaterialSelectBase::isPlayOrStop() const {
    if (type_ != MtType::BGM) return false;
    const auto change = currentChange();
    const auto generation = change.lifetime->audio_revision;
    const auto playing = playing_;
    const auto current = filePath();
    return change.current() && change.lifetime->audio_revision == generation && !current.empty() &&
        (playing.empty() || !::cwxeditor::cwx::utils::cfnmatch(
            normalizePath(current), normalizePath(playing)));
}

void MaterialSelectBase::updatePlayButton(bool play_or_stop) {
    const auto change = currentChange();
    const auto generation = change.lifetime->audio_revision;
    const bool playing = !play_or_stop && !playing_.empty();
    const bool can_play = playing || canPlay();
    if (!change.current() || change.lifetime->audio_revision != generation) return;
    // D版と同様、ツールチップには解決後の再生ファイル名ではなく、選択した
    // 素材パスを使う。 ツールバーの見出しと ALL の方針は変えない。
    const bool full_bgm_tooltip = type_ == MtType::BGM && playing && prop_ &&
        !lib::editor_window_host_options().all_mode;
    String tooltip_path;
    if (full_bgm_tooltip) {
        tooltip_path = ::cwxeditor::cwx::utils::encodePath(path());
        if (!change.current() || change.lifetime->audio_revision != generation) return;
    }
    if (play_button_) {
        if (type_ == MtType::BGM) {
            if (const auto toggle = std::dynamic_pointer_cast<lib::ToggleButton>(play_button_))
                toggle->checked = playing;
            const auto id = playing ? types::MenuID::StopBGM
                                    : types::MenuID::PlayBGM;
            String text = prop_ ? prop_->requiredMsgs().menuText(id)
                                : String{playing ? "Stop" : "Play"};
            if (playing && prop_)
                text = ::cwxeditor::cwx::utils::tryFormat(
                    text, full_bgm_tooltip ? tooltip_path : baseName(playing_));
            play_button_->text.clear();
            play_button_->icon = prop_ ? prop_->images().menu(id) : lib::ResourceId{};
            play_button_->set_tooltip(std::move(text));
        }
        // D版と同様、効果音の再生・停止にはそれぞれ固定のボタンを使
        // う。 再生中もBGMのような可変幅の「停止＋ファイル名」に
        // は変えない。 組み込み先の音声パネルが指定したコンパクト
        // な記号・文字列を維持し、列の配置が変わらないようにする。
        play_button_->set_enabled(enabled_ && can_play);
    }
    if (stop_button_) stop_button_->set_enabled(enabled_ && playing);
    if (const auto toolbar = play_toolbar_.lock()) {
        const String command = type_ == MtType::BGM ? "play-bgm" : "play-se";
        for (auto& item : toolbar->items) {
            if (item.command != command) continue;
            const auto id = type_ == MtType::BGM && playing
                ? types::MenuID::StopBGM
                : (type_ == MtType::BGM ? types::MenuID::PlayBGM
                                        : types::MenuID::PlaySE);
            String text = prop_ ? prop_->requiredMsgs().menuText(id)
                                : String{playing ? "Stop" : "Play"};
            if (type_ == MtType::BGM && playing && prop_)
                text = ::cwxeditor::cwx::utils::tryFormat(text, baseName(playing_));
            item.text = text;
            item.tooltip = full_bgm_tooltip
                ? ::cwxeditor::cwx::utils::tryFormat(prop_->requiredMsgs().menuText(id), tooltip_path)
                : text;
            item.icon = prop_ ? prop_->images().menu(id) : lib::ResourceId{};
            item.enabled = enabled_ && can_play;
            item.checked = playing;
            ++toolbar->item_update_revision;
            break;
        }
    }
}

void MaterialSelectBase::playAudio(lib::AudioBus bus) {
    if (!enabled_ || !services_.audio) return;
    const auto change = currentChange();
    const auto ticket = bus == lib::AudioBus::SoundEffect
        ? beginSoundPreview(services_.audio) : SoundPreviewTicket{};
    const auto request_current = [&] { return change.current() && ticket.current(); };
    const auto source_path = audioPath();
    if (!request_current() || source_path.empty()) return;
    // D版と同様、両方のプレビュー処理に _comm.skin.legacy を渡す。
    // 取り込み元や強制指定の素材スキンで、現在のエディタの再生方針を置き換えない。
    const auto editor_commons_lifetime = editor_commons_lifetime_;
    auto* const editor_commons = liveEditorCommons();
    const auto previewSkin = editor_commons && !editor_commons_lifetime.expired() &&
            editor_commons_ == editor_commons
        ? editor_commons->skin() : summSkin();
    const bool legacy = previewSkin ? previewSkin->legacy() : (summary_ && summary_->legacy());
    const int saved_bgm_type = prop_
        ? prop_->var().soundPlayType.value : sound::SOUND_TYPE_AUTO;
    const int requested_type = prop_
        ? (bus == lib::AudioBus::Bgm ? prop_->var().soundPlayType.value
                                     : prop_->var().soundEffectPlayType.value)
        : sound::SOUND_TYPE_AUTO;
    const auto requested_backend = sound::resolveMaterialPreviewPreference(
        bus, requested_type, saved_bgm_type, legacy, source_path);
    if (!request_current()) return;
    const bool external = requested_backend ==
        lib::AudioBackendPreference::ExternalApplication;
    std::string initialization_error;
    if (!external && !services_.audio->initialize(initialization_error)) return;
    if (!request_current()) return;
    const auto playback = std::make_shared<AudioPlayback>();
    playback->audio = services_.audio;
    playback->dispatcher = services_.dispatcher;
    lib::AudioSource source;
    if (isBinSnd(source_path)) {
        const auto bytes = strToBSnd(source_path);
        playback->bytes.reserve(bytes.size());
        for (const auto byte : bytes) playback->bytes.push_back(static_cast<std::byte>(byte));
        source.encoded_data = playback->bytes;
        source.format_hint = pathToUtf8(pathFromUtf8(source_path).extension());
    } else {
        source.path = pathFromUtf8(source_path);
    }
    lib::AudioPlayOptions options;
    options.bus = bus;
    options.backend = requested_backend;
    const auto global = bus == lib::AudioBus::Bgm && prop_
        ? std::clamp(prop_->var().bgmVolume.value, 0, 100) : 100;
    const auto effective = (global * volume_) / 100;
    // D版と同様、BGMプレビューの全体音量×個別音量を共通の音声系統に設定する。 トラック自体には追加の
    // 倍率を掛けず、設定変更時の sound::bgmVolume がその値を置き換えられるようにする。
    options.volume = bus == lib::AudioBus::Bgm
        ? 1.0F : static_cast<float>(volume_) / 100.0F;
    options.loop_count = loop_count_ == 0 ? std::numeric_limits<std::uint32_t>::max()
                                          : static_cast<std::uint32_t>(loop_count_);
    options.fade_in = std::chrono::milliseconds(fade_in_ms_);
    // D版の playBGMCW と同様、旧形式でないスキンだけで特殊なループ情報を有効にする。
    options.special_loop = bus == lib::AudioBus::Bgm && !legacy;
    if (options.special_loop && !source.path.empty())
        options.loop_region = lib::read_audio_loop_region(source.path);

    const auto generation = change.lifetime->audio_revision + 1;
    stopAudio();
    const auto current = [&] {
        return request_current() && change.lifetime->audio_revision == generation;
    };
    if (!current()) return;
    if (external) {
        // D版の APP モードと同様、チャンネルを終了してから共通のシェルサービスで起動する。
        if (bus == lib::AudioBus::Bgm) sound::stopBGM();
        else sound::stopSE();
        if (!current()) return;
        playback->audio->stop_bus(bus);
        if (!current()) return;
        if (services_.shell) {
            std::string error;
            (void)services_.shell->open_path(pathFromUtf8(source_path), error);
        }
        return;
    }
    if (bus == lib::AudioBus::SoundEffect) {
        // D版の playSE と同様、どちらの効果音枠を再生する場合も、先に全体の効果音プレビューを止める。
        playback->audio->stop_bus(bus);
        if (!current()) return;
    }
    if (bus == lib::AudioBus::Bgm) {
        // 戦闘BGMのプレビューは、D版のBGMと同様に排他的に再生する。
        // 全体音量×個別音量の整数値を共通の音声系統へ一度設定し、 その後の
        // 全体設定の変更は sound::bgmVolume で置き換える。
        playback->audio->stop_bus(bus);
        if (!current()) return;
        playback->audio->set_bus_volume(bus, static_cast<float>(effective) / 100.0F);
    }
    if (!current()) return;
    playback->handle = playback->audio->play(source, options);
    if (!current() || !playback->handle) return; // 遅れて取得したハンドルや未使用のハンドルは、RAIIで解放する。
    const auto length = playback->audio->duration(playback->handle);
    if (!current()) return;
    audio_playback_ = playback;
    playing_ = source_path;
    updatePlayButton(false);
    if (!current()) return;
    if (const auto bar = play_bar_) {
        bar->length(std::max(std::chrono::milliseconds(0), length));
        bar->current(std::chrono::milliseconds(0));
        bar->set_enabled(length.count() > 0);
        if (!current()) return;
        if (type_ == MtType::BGM && !lib::editor_window_host_options().all_mode) {
            // D版は bgmLen が負の場合に警告する。このバックエンドは長さ不明
            // のMIDIをゼロで返すため、 ゼロ以下を同じ長さ不明の状態として扱う。
            if (length.count() <= 0 && prop_) {
                const auto message = prop_->requiredMsgs().canNotGetMusicLength.value;
                const auto icon = prop_->images().warning();
                if (!current()) return;
                bar->setWarning(icon, message);
            } else {
                bar->clearWarning();
            }
        }
    }
    if (!current()) return;
    pollAudio();
}

void MaterialSelectBase::playBGM(bool from_event) {
    if (type_ != MtType::BGM) return;
    const auto change = currentChange();
    const auto generation = change.lifetime->audio_revision;
    const bool play = isPlayOrStop();
    if (!change.current() || change.lifetime->audio_revision != generation) return;
    if (play) {
        playAudio(lib::AudioBus::Bgm);
        // SWT.TOGGLE はクリックのコールバックより先に状態を変える。 playAudio
        // が再生成功を反映する前に音声設定が失敗しても、状態を整合させる。
        if (!change.current()) return;
        updatePlayButton(isPlayOrStop());
    } else if (!from_event) {
        stopBGM();
    }
}

void MaterialSelectBase::bgmStopped() {
    if (type_ != MtType::BGM) return;
    const auto change = currentChange();
    const auto generation = change.lifetime->audio_revision + 1;
    stopAudio();
    if (!change.current() || change.lifetime->audio_revision != generation) return;
    updatePlayButton(true);
    if (!change.current() || change.lifetime->audio_revision != generation) return;
    refreshFileListMenu();
}

void MaterialSelectBase::playSE() {
    if (type_ == MtType::SE) playAudio(lib::AudioBus::SoundEffect);
}

void MaterialSelectBase::stopSE() {
    if (type_ != MtType::SE) return;
    const auto change = currentChange();
    const auto generation = change.lifetime->audio_revision;
    const auto audio = services_.audio;
    const auto ticket = !playing_.empty() ? beginSoundPreview(audio) : SoundPreviewTicket{};
    if (audio && !playing_.empty()) audio->stop_bus(lib::AudioBus::SoundEffect);
    if (!ticket.current() || !change.current() || change.lifetime->audio_revision != generation) return;
    seStopped();
}

void MaterialSelectBase::seStopped() {
    if (type_ != MtType::SE) return;
    const auto change = currentChange();
    const auto generation = change.lifetime->audio_revision + 1;
    stopAudio();
    if (!change.current() || change.lifetime->audio_revision != generation) return;
    updatePlayButton(true);
    if (!change.current() || change.lifetime->audio_revision != generation) return;
    refreshFileListMenu();
}

void MaterialSelectBase::AudioPlayback::stop() noexcept {
    retired = true;
    const auto old_timer = std::exchange(timer, 0);
    const auto old_handle = std::exchange(handle, 0);
    if (dispatcher && old_timer) dispatcher->cancel_timer(old_timer);
    if (audio && old_handle) audio->stop(old_handle);
}

void MaterialSelectBase::stopAudio() {
    ++publication_lifetime_->audio_revision;
    const auto playback = std::move(audio_playback_);
    playing_.clear();
    initPlayBar();
    // バックエンドのコールバックが別の再生を始める前に、終了状態を反映する。
    if (playback) playback->stop();
}

void MaterialSelectBase::pollAudio() {
    const auto lifetime = publication_lifetime_;
    const auto playback = audio_playback_;
    if (!playback || playback->retired || playback->polling || playback->timer) return;
    const auto current = [&] {
        return lifetime->owner && lifetime->owner->audio_playback_ == playback && !playback->retired;
    };
    playback->polling = true;
    struct FinishPoll { AudioPlayback& playback; ~FinishPoll() { playback.polling = false; } } finish{*playback};
    try {
        const bool playing = playback->audio->playing(playback->handle);
        if (!current()) return;
        if (!playing) {
            if (type_ == MtType::BGM) bgmStopped(); else seStopped();
            return;
        }
        const auto position = playback->audio->position(playback->handle);
        if (!current()) return;
        if (play_bar_) play_bar_->current(std::max(std::chrono::milliseconds(0), position));
        if (!current()) return;
        if (playback->dispatcher) {
            const std::weak_ptr<PublicationLifetime> weak = lifetime;
            const std::weak_ptr<AudioPlayback> source = playback;
            const auto delivered = std::make_shared<bool>(false);
            const auto timer = playback->dispatcher->post_delayed(std::chrono::milliseconds(100),
                [weak, source, delivered] {
                    if (std::exchange(*delivered, true)) return;
                    const auto state = weak.lock();
                    const auto active = source.lock();
                    if (!state || !state->owner || !active || active->retired ||
                        state->owner->audio_playback_ != active) return;
                    active->timer = 0;
                    if (!active->polling) state->owner->pollAudio();
                });
            if (current() && !*delivered) playback->timer = timer;
            else if (timer) playback->dispatcher->cancel_timer(timer);
        }
    } catch (...) {
        if (current()) stopAudio();
        throw;
    }
}

void MaterialSelectBase::playThr() {
    pollAudio();
}

void MaterialSelectBase::Play::mouseDoubleClick(lib::PointerEvent& event) {
    if (owner_ && event.button == lib::PointerButton::Primary) widgetSelected();
}

void MaterialSelectBase::Play::keyPressed(lib::KeyEvent& event) {
    if (owner_ && event.key == lib::Key::Enter) {
        widgetSelected();
        event.handled = true;
    }
}

void MaterialSelectBase::Play::widgetSelected() {
    if (!owner_) return;
    if (owner_->type_ == MtType::BGM) owner_->playBGM(false); else owner_->playSE();
}

void MaterialSelectBase::UpdatePlayBar::run() {
    if (owner_) owner_->pollAudio();
}

void MaterialSelectBase::UpdatePlayBar::putPos(std::int64_t position) {
    if (!owner_ || !owner_->play_bar_) return;
    owner_->play_bar_->current(std::chrono::milliseconds(position));
}

std::shared_ptr<lib::Button> MaterialSelectBase::createRefreshButton(Parent parent,
                                                                      bool text) {
    auto target = controlParent(std::move(parent));
    refresh_button_ = makeButton(services_, text && prop_ ? prop_->requiredMsgs().refreshS.value
                                                           : String{});
    if (prop_) {
        refresh_button_->icon = prop_->images().load(
            dprops::StringView{"refresh.png"});
        refresh_button_->set_tooltip(
            prop_->requiredMsgs().menuText(types::MenuID::Refresh));
    }
    refresh_button_->on_click = bindInput(refresh_button_, &MaterialSelectBase::refresh_button_,
        [](MaterialSelectBase& self) { self.doRefresh(); });
    addToParent(target, refresh_button_);
    refresh_button_->set_enabled(enabled_ && canRefresh());
    return refresh_button_;
}

bool MaterialSelectBase::canRefresh() const noexcept {
    return !loading_ && summary_ && !summary_->scenarioPath().empty();
}

std::shared_ptr<lib::Button> MaterialSelectBase::createDirectoryButton(Parent parent,
                                                                        bool text) {
    auto target = controlParent(std::move(parent));
    directory_button_ = makeButton(
        services_, text && prop_ ? prop_->requiredMsgs().menuText(types::MenuID::OpenDir)
                                 : String{});
    if (prop_) {
        directory_button_->icon = prop_->images().load(
            dprops::StringView{"folder.png"});
        directory_button_->set_tooltip(
            prop_->requiredMsgs().menuText(types::MenuID::OpenDir));
    }
    directory_button_->on_click = bindInput(directory_button_, &MaterialSelectBase::directory_button_,
        [](MaterialSelectBase& self) { self.doDirectory(); });
    addToParent(target, directory_button_);
    directory_button_->set_enabled(enabled_ && canDirectoryButton());
    return directory_button_;
}

void MaterialSelectBase::createRefreshToolItem(const std::shared_ptr<lib::ToolBar>& bar) {
    if (!bar) return;
    bar->items.push_back({{}, "refresh",
                          prop_ ? prop_->images().load(
                                      dprops::StringView{"refresh.png"})
                                : 0U,
                          true, false, false, false, {},
                          prop_ ? prop_->buildTool(types::MenuID::Refresh) : "Refresh"});
    // D版と同様、有効化条件は null で常時有効とする。 doRefresh()
    // が共通の更新経路を使うため、Summary のない選択欄でも安全に実行できる。
    bindToolCommand(bar, "refresh", &MaterialSelectBase::doRefresh, false);
}

void MaterialSelectBase::createDirectoryToolItem(const std::shared_ptr<lib::ToolBar>& bar) {
    if (!bar) return;
    bar->items.push_back({prop_ ? prop_->requiredMsgs().menuText(types::MenuID::OpenDir)
                                : "Directory",
                          "directory",
                          prop_ ? prop_->images().load(
                                      dprops::StringView{"folder.png"})
                                : 0U,
                          true, false, false, false});
    // D版はボタンを選択欄の有効状態と操作可否で制限するが、 この ToolItem
    // は有効化条件が null のため、ツールバーの操作として残す。
    bindToolCommand(bar, "directory", &MaterialSelectBase::doDirectory, false);
}

void MaterialSelectBase::playFromControl() {
    if (type_ == MtType::BGM) playBGM(false); else playSE();
}

void MaterialSelectBase::bindToolCommand(const std::shared_ptr<lib::ToolBar>& bar, String command,
                                        void (MaterialSelectBase::* action)(),
                                        const bool require_enabled) {
    const std::weak_ptr<PublicationLifetime> weak = publication_lifetime_;
    const std::weak_ptr<lib::ToolBar> source = bar;
    const auto previous = bar->on_command;
    bar->on_command = [weak, source, command = std::move(command), action, require_enabled, previous](StringView value) {
        if (value != command) { if (previous) previous(value); return; }
        const auto state = weak.lock();
        if (state && state->owner && !source.expired() &&
            (!require_enabled || state->owner->enabled_))
            (state->owner->*action)();
    };
}

String MaterialSelectBase::path(std::ptrdiff_t index) const {
    if (index < 0) index = image_index_;
    const auto change = currentChange();
    const auto type = type_;
    const auto revision = change.lifetime->rows_revision;
    String file = path_;
    String binary;
    if (type == MtType::CARD) {
        file.clear();
        if (index >= 0 && static_cast<std::size_t>(index) < paths_.size() && paths_[index] &&
            paths_[index]->type == types::CardImageType::File) file = paths_[index]->path();
        if (index >= 0 && static_cast<std::size_t>(index) < binary_paths_.size())
            binary = binary_paths_[index];
    } else if (type == MtType::BG_IMG && !binary_paths_.empty()) binary = binary_paths_[0];
    // 定義の提供処理が、この選択欄を変更・破棄する場合が
    // ある。 呼び出す前に、返す値をすべて所有しておく。
    if (!binary.empty()) {
        const bool selected = selectedBinaryDirectory();
        if (!change.current() || change.lifetime->rows_revision != revision) return {};
        if (selected) return binary;
    }
    return file;
}

void MaterialSelectBase::setPaths(CardImageList value, bool store) {
    if (type_ != MtType::CARD) return;
    if (value.empty()) {
        value.push_back(std::make_shared<CardImage>(String{},
                                                    types::CardImagePosition::Default));
    }
    if (!first_set_ && sameCardImages(paths_, value)) return;
    const auto change = beginChange();
    const auto store_callback = store_;
    if (first_set_) {
        first_set_ = false;
        store = false;
    }
    if (store && store_callback) store_callback();
    if (!change.current()) return;
    const bool changed = !sameCardImages(paths_, value);
    paths_ = std::move(value);
    binary_paths_.assign(paths_.size(), {});
    bool included = false;
    for (std::size_t i = 0; i < paths_.size(); ++i) {
        if (paths_[i] && paths_[i]->type == types::CardImageType::File &&
            isBinImg(paths_[i]->path())) {
            binary_paths_[i] = paths_[i]->path();
            included = true;
        }
    }
    image_index_ = std::min<int>(image_index_, static_cast<int>(paths_.size() - 1));
    if (included && !notifyChange(includeEvent, change)) return;
    updateUseNoCardSizeImage();
    if (!change.current()) return;
    refreshPaths();
    if (!change.current()) return;
    if (changed) notifyModified();
    if (!change.current()) return;
    refreshButtons();
}

void MaterialSelectBase::path2(String value, bool refresh_paths, bool update_binary_image,
                               std::ptrdiff_t index, bool store) {
    const auto previous = currentChange();
    if (index < 0) index = image_index_;
    const auto old = path(index);
    if (!previous.current()) return;
    if (old == value) return;
    const auto change = beginChange();
    (void)applyPath(std::move(value), old, refresh_paths, update_binary_image, index, store, change);
}

bool MaterialSelectBase::applyPath(String value, String old, bool refresh_paths,
                                   bool update_binary_image, std::ptrdiff_t index,
                                   bool store, const ChangeGuard& change) {
    if (!change.current()) return false;
    if (old == value) return true;
    if (index < 0) index = image_index_;
    const auto store_callback = store_;
    if (store && store_callback) store_callback();
    if (!change.current()) return false;
    refresh_paths = refresh_paths || (currentDir().empty() && !value.empty());
    if (!selectPathImpl(value, index, store, change)) return false;
    if (update_binary_image && (type_ == MtType::CARD || type_ == MtType::BG_IMG)) {
        if (type_ == MtType::BG_IMG && binary_paths_.empty()) binary_paths_.emplace_back();
        const std::size_t pos = type_ == MtType::CARD ? static_cast<std::size_t>(index) : 0U;
        if (pos < binary_paths_.size()) binary_paths_[pos] = isBinImg(value) ? value : String{};
        if (pos < binary_paths_.size() && !binary_paths_[pos].empty()) {
            if (!notifyChange(includeEvent, change)) return false;
        }
    }
    if (type_ == MtType::CARD) updateUseNoCardSizeImage();
    if (!change.current()) return false;
    if (type_ == MtType::BG_IMG && excludeCardSizeImage() && !value.empty() &&
        !isBinImg(value) && imageIsCardSized(value)) {
        prop_->var().excludeCardSizeImage.value = true;
    }
    if (currentDir().empty() && !value.empty() && summSkin()) {
        const auto version = summary_ ? summary_->dataVersion()
                                      : String(versioninfo::LATEST_VERSION);
        temp_path_ = summSkin()->findImagePath(value,
                                                summary_ ? summary_->scenarioPath() : String{},
                                                version);
    }
    if (refresh_paths) refreshPaths(); else refreshList();
    if (!change.current()) return false;
    if (type_ == MtType::BGM || type_ == MtType::SE) refDataVersion();
    if (!change.current()) return false;
    const auto selected = path(index);
    if (!change.current()) return false;
    if (old != selected) notifyModified();
    if (!change.current()) return false;
    refreshButtons();
    return change.current();
}

void MaterialSelectBase::selectPath(String value, std::ptrdiff_t index, bool store) {
    const auto change = beginChange();
    (void)selectPathImpl(std::move(value), index, store, change);
}

bool MaterialSelectBase::selectPathImpl(String value, std::ptrdiff_t index, bool store,
                                      const ChangeGuard& change) {
    if (!change.current()) return false;
    if (index < 0) index = image_index_;
    const auto store_callback = store_;
    if (type_ == MtType::CARD) {
        if (index < 0 || static_cast<std::size_t>(index) >= paths_.size()) return false;
        const auto position = paths_[index] ? paths_[index]->positionType()
                                            : types::CardImagePosition::Default;
        auto candidate = std::make_shared<CardImage>(normalizePath(value), position);
        if (paths_[index] && paths_[index]->opEquals(*candidate)) return true;
        if (store && store_callback) store_callback();
        if (!change.current()) return false;
        paths_[index] = std::move(candidate);
    } else {
        const auto candidate = normalizePath(value);
        if (path_ == candidate) return true;
        if (store && store_callback) store_callback();
        if (!change.current()) return false;
        path_ = candidate;
    }
    return true;
}

void MaterialSelectBase::updateUseNoCardSizeImage() {
    const auto change = currentChange();
    if (type_ != MtType::CARD || no_card_size_ || !summSkin()) return;
    const auto version = summary_ ? summary_->dataVersion()
                                  : String(versioninfo::LATEST_VERSION);
    for (std::size_t i = 0; i < paths_.size(); ++i) {
        if (i >= binary_paths_.size() || !binary_paths_[i].empty() || !paths_[i] ||
            paths_[i]->type != types::CardImageType::File || paths_[i]->path().empty()) continue;
        const auto resolved = summSkin()->findImagePath(paths_[i]->path(),
                                                        summary_ ? summary_->scenarioPath() : String{},
                                                        version);
        if (!resolved.empty() && !imageIsCardSized(resolved)) {
            no_card_size_ = true;
            const auto refresh_callback = refresh_callback_;
            if (!notifyChange(updateNoCardSize, change)) return;
            if (refresh_callback) refresh_callback();
            break;
        }
    }
}

String MaterialSelectBase::binPath() const {
    if (type_ != MtType::CARD && type_ != MtType::BG_IMG) return {};
    const std::size_t index = type_ == MtType::CARD ? static_cast<std::size_t>(image_index_) : 0U;
    return index < binary_paths_.size() ? binary_paths_[index] : String{};
}

void MaterialSelectBase::imageIndex(int value) {
    if (type_ != MtType::CARD) return;
    if (paths_.empty()) value = 0;
    else value = std::clamp(value, 0, static_cast<int>(paths_.size() - 1));
    if (image_index_ == value) return;
    (void)beginChange();
    image_index_ = value;
    refreshPaths();
}

String MaterialSelectBase::filePath() const {
    if (!temp_path_.empty()) return temp_path_;
    if (loading_ || !dirs_ || !file_list_) return {};
    const auto change = currentChange();
    const auto revision = change.lifetime->rows_revision;
    const auto binary = binPath();
    String file;
    if (!file_list_->selection.empty()) {
        const auto row = file_list_->selection.front();
        if (row < file_list_->rows.size() && !(none_row_ && row == 0)) {
            file = toFilePath(fileText(file_list_->rows[row]));
        }
    }
    if (!binary.empty()) {
        const bool selected = selectedBinaryDirectory();
        if (!change.current() || change.lifetime->rows_revision != revision) return {};
        if (selected) return binary;
    }
    return file;
}

void MaterialSelectBase::refresh() {
    const auto change = currentChange();
    const auto callback = refresh_callback_;
    refreshPaths();
    if (change.current() && callback) callback();
}

std::vector<String> MaterialSelectBase::showingNames() const {
    std::vector<String> result;
    if (!file_list_) return result;
    const std::size_t first = none_row_ && !file_list_->rows.empty() ? 1U : 0U;
    for (std::size_t i = first; i < file_list_->rows.size(); ++i) {
        result.push_back(fileText(file_list_->rows[i]));
    }
    return result;
}

std::vector<String> MaterialSelectBase::showingPaths() const {
    std::vector<String> result;
    const auto current = currentDir();
    for (const auto& value : showingNames()) {
        if (!current.empty()) {
            result.push_back(::cwxeditor::cwx::utils::encodePath(
                normalizePath(pathToUtf8(pathFromUtf8(current) /
                                         pathFromUtf8(value)))));
        } else if (!value.empty() && value.front() == '/') {
            result.push_back(::cwxeditor::cwx::utils::encodePath(
                normalizePath(value.substr(1))));
        } else {
            result.push_back(value);
        }
    }
    return result;
}

String MaterialSelectBase::toFilePath(StringView file) const {
    if (file.empty()) return {};
    return pathToUtf8(resolveCurrentFile(file));
}

void MaterialSelectBase::copyFilePath() {
    const auto change = currentChange();
    const auto clipboard = services_.clipboard;
    auto* const editor_commons = liveEditorCommons();
    const auto editor_commons_lifetime = editor_commons_lifetime_;
    const auto value = path();
    if (!change.current() || value.empty() || !clipboard) return;
    lib::ClipboardData data;
    // ファイルパスのコピーではWindowsの区切り文字をスラッシュへ変換する。バイナリ画像URIは変更しない。
    data.text = ::cwxeditor::cwx::utils::encodePath(value);
    clipboard->write(data);
    if (!change.current() || !editor_commons ||
        editor_commons_ != editor_commons || editor_commons_lifetime.expired()) return;
    editor_commons->refreshToolBar();
}

void MaterialSelectBase::selectDir(int selection) {
    if (!dirs_ || selection < 0 || static_cast<std::size_t>(selection) >= directory_entries_.size()) return;
    selectDirectory(selection, beginChange());
}

void MaterialSelectBase::selectDirectory(int selection, const ChangeGuard& change) {
    if (!change.current()) return;
    if (!dirs_ || directory_entries_.empty()) return;
    if (selection < 0 || static_cast<std::size_t>(selection) >= directory_entries_.size()) return;
    const auto revision = change.lifetime->rows_revision;
    const auto old_path = path();
    if (!change.current() || change.lifetime->rows_revision != revision) return;
    const bool old_continue = continue_bgm_;
    const auto old_selection = selected_dir_;
    const auto defs = getDefs();
    if (!change.current() || change.lifetime->rows_revision != revision) return;
    const bool definition = selection < static_cast<int>(defs.size());
    const auto getter = valueFromDef;
    const auto binary = binPath();
    const auto index = image_index_;
    const auto refresh_callback = refresh_callback_;
    CardImagePtr candidate;
    const bool defined_image = definition && type_ == MtType::CARD && getter &&
        index >= 0 && static_cast<std::size_t>(index) < paths_.size();
    if (defined_image) {
        candidate = getter(selection, !binary.empty(), binary);
        if (!change.current() || change.lifetime->rows_revision != revision) return;
    }
    const auto requested_entry = directory_entries_[static_cast<std::size_t>(selection)];
    const auto requested_combo = dirs_;
    selected_dir_ = selection;
    (void)lib::select_combo_item(*dirs_, static_cast<std::size_t>(selection), false);
    if (type_ == MtType::BGM)
        continue_bgm_ = definition && (can_continue_ || old_continue) &&
            selection == static_cast<int>(defs.size()) - 1;
    bool path_notified = false;
    if (defined_image) {
        paths_[index] = std::move(candidate);
        refreshList();
    } else if (definition && !old_path.empty()) {
        if (!applyPath({}, old_path, true, false, -1, true, change)) return;
        path_notified = true;
    } else refreshList();
    if (!change.current()) return;
    if (dirs_ != requested_combo || selected_dir_ != selection ||
        static_cast<std::size_t>(selection) >= directory_entries_.size() ||
        directory_entries_[selection].label != requested_entry.label ||
        directory_entries_[selection].kind != requested_entry.kind) return;
    if (file_combo_ && file_list_ &&
        requested_entry.kind == DirectoryEntry::Kind::Scenario &&
        requested_entry.label != "/" && !file_list_->rows.empty()) {
        // D版の Combo 特殊化と同様、具体的なシナリオデ
        // ィレクトリを選ぶと、0行目を選択して確定する。 共通
        // の MaterialSelect 経路でこの動作を
        // 保ち、定義やルートの項目では素材を自動選択しない。
        const auto requested_table = file_list_;
        const auto requested_file_combo = file_combo_;
        (void)lib::select_table_rows(*requested_table, {0U}, false, false);
        if (!change.current() || dirs_ != requested_combo ||
            file_list_ != requested_table || file_combo_ != requested_file_combo ||
            selected_dir_ != selection ||
            static_cast<std::size_t>(selection) >= directory_entries_.size() ||
            directory_entries_[selection].label != requested_entry.label ||
            directory_entries_[selection].kind != requested_entry.kind) return;
        selectFile();
        if (!change.current() || dirs_ != requested_combo ||
            file_list_ != requested_table || file_combo_ != requested_file_combo ||
            selected_dir_ != selection ||
            static_cast<std::size_t>(selection) >= directory_entries_.size() ||
            directory_entries_[selection].label != requested_entry.label ||
            directory_entries_[selection].kind != requested_entry.kind) return;
        path_notified = old_path != path();
        if (!change.current()) return;
    }
    const auto selected = path();
    if (!change.current()) return;
    const bool modified = old_path != selected || old_continue != continue_bgm_ ||
        (definition && old_selection != selected_dir_);
    if (modified && !path_notified) notifyModified();
    if (!change.current()) return;
    refreshButtons();
    if (!change.current()) return;
    if (!modified && !path_notified && refresh_callback) refresh_callback();
}

void MaterialSelectBase::startIncSearch() {
    const auto list = file_list_;
    const auto popup = search_popup_;
    const std::weak_ptr<PublicationLifetime> life = publication_lifetime_;
    if (!canIncSearch() || !list) return;
    const auto services = services_;
    // D版と同様、ファイル一覧を復元するのは検索ポップアップを開くときだ
    // け。 すでに開いている場合は、検索入力欄にキーボード入力を残す。
    if ((!popup || !popup->isOpen()) && services.gui)
        dutils::forceFocus(list, true, services);
    if (life.expired() || publication_lifetime_->owner != this ||
        file_list_ != list || search_popup_ != popup || !canIncSearch()) return;
    createIncrementalSearch();
    const auto current_popup = search_popup_;
    if (life.expired() || publication_lifetime_->owner != this ||
        file_list_ != list || !current_popup || search_popup_ != current_popup ||
        !canIncSearch()) return;
    current_popup->startIncSearch();
}

void MaterialSelectBase::createIncrementalSearch() {
    const auto host = fileControl();
    if (search_popup_ || !file_list_ || !host) return;
    incsearch::IncSearchSettings settings;
    if (prop_) {
        settings.startIncrementalSearchWhenKeyDown = prop_->var().startIncrementalSearchWhenKeyDown.value;
        const auto* prop = prop_;
        settings.startIncrementalSearchWhenKeyDownProvider = [prop] {
            return prop && prop->var().startIncrementalSearchWhenKeyDown.value;
        };
        settings.incrementalSearchBoxWidth = prop_->var().incrementalSearchBoxWidth.value;
        settings.incrementalSearchType = prop_->var().incrementalSearchType.value;
        settings.containsLabel = prop_->requiredMsgs().incSearchContains.value;
        settings.wildcardLabel = prop_->requiredMsgs().incSearchWildcard.value;
        settings.regexLabel = prop_->requiredMsgs().incSearchRegex.value;
        settings.windowTitle = prop_->requiredMsgs().menuText(types::MenuID::IncSearch);
    }
    const std::weak_ptr<PublicationLifetime> weak = publication_lifetime_;
    const auto owner = [weak]() -> MaterialSelectBase* {
        const auto state = weak.lock();
        return state ? state->owner : nullptr;
    };
    search_popup_ = std::make_shared<incsearch::IncSearch>(services_, host.get(),
        [owner] { const auto self = owner(); return self && self->canIncSearch(); },
        std::vector<incsearch::AdditionMatcher>{}, std::move(settings));
    search_popup_->modEvent.push_back([owner] {
        const auto self = owner();
        if (!self || !self->search_popup_) return;
        self->inc_search_.matcher(self->search_popup_->textMatcher());
        if (self->prop_) self->prop_->var().incrementalSearchType.value =
            static_cast<int>(self->search_popup_->searchType());
        self->refresh();
    });
    const std::weak_ptr<lib::Widget> weak_host = host;
    host->on_text_input = [owner, weak_host](lib::TextInputEvent& event) {
        const auto self = owner();
        const auto control = weak_host.lock();
        if (self && control && self->fileControl() == control && self->search_popup_ &&
            self->search_popup_->handleHostTextInput(event, control.get()))
            event.handled = true;
    };
    host->on_key_down = [owner, weak_host](lib::KeyEvent& event) {
        const auto self = owner();
        const auto control = weak_host.lock();
        if (self && control && self->fileControl() == control && self->search_popup_ &&
            self->search_popup_->handleHostKeyDown(event, control.get()))
            event.handled = true;
    };
}

bool MaterialSelectBase::canIncSearch() const noexcept {
    return !read_only_ && !loading_ && can_inc_search_;
}

void MaterialSelectBase::enabled(bool value) {
    if (enabled_ != value) (void)beginChange();
    const auto change = currentChange();
    enabled_ = value;
    if (dirs_) dirs_->set_enabled(enabled_ && !read_only_ && !loading_);
    if (file_list_) file_list_->set_enabled(enabled_ && !read_only_ && !loading_);
    if (file_combo_) file_combo_->set_enabled(enabled_ && !read_only_ && !loading_);
    updateEnabledControls();
    if (!change.current()) return;
    if (type_ == MtType::BGM || type_ == MtType::SE)
        updateAudioOptionControls(false);
    if (!change.current()) return;
    if (!enabled_) {
        inc_search_.close();
        if (search_popup_) search_popup_->close();
        if (!change.current()) return;
        if (type_ == MtType::BGM) stopBGM();
        if (type_ == MtType::SE) stopSE();
    }
}

const std::vector<String>& MaterialSelectBase::defExts() const {
    static const std::vector<String> empty;
    const auto skin_value = summSkin();
    if (!skin_value) return empty;
    if (type_ == MtType::CARD || type_ == MtType::BG_IMG) return skin_value->extImage();
    if (type_ == MtType::BGM) return skin_value->extBgm();
    return skin_value->extSound();
}

std::vector<String> MaterialSelectBase::defDirs() const {
    const auto skin_value = summSkin();
    if (!skin_value) return {};
    if (type_ == MtType::CARD || type_ == MtType::BG_IMG) return skin_value->tableDirs();
    if (type_ == MtType::BGM) return skin_value->bgmDirs();
    return skin_value->seDirs();
}

std::vector<String> MaterialSelectBase::engineDefDirs() const {
    const auto skin_value = summSkin();
    if (!skin_value) return {};
    const auto version = summary_ ? summary_->dataVersion() : versioninfo::LATEST_VERSION;
    if (type_ == MtType::CARD || type_ == MtType::BG_IMG) return skin_value->wsnTableDirs(version);
    if (type_ == MtType::BGM) return skin_value->wsnMusicDirs(version);
    return skin_value->wsnSoundDirs(version);
}

bool MaterialSelectBase::isTarg(StringView value) const {
    const auto skin_value = summSkin();
    if (!skin_value) return false;
    if (type_ == MtType::CARD) return skin_value->isCardImage(value, no_card_size_,
                                                               can_include_ && !is_menu_card_);
    if (type_ == MtType::BG_IMG) return skin_value->isBgImage(value, excludeCardSizeImage());
    if (type_ == MtType::BGM) return skin_value->isBGM(value);
    return skin_value->isSE(value);
}

bool MaterialSelectBase::hasTarg(const Skin& value, StringView directory,
                                 bool force_refresh, bool no_card_size) const {
    return containsMaterial(value, type_, directory, force_refresh, no_card_size,
                            can_include_ && !is_menu_card_);
}

bool MaterialSelectBase::hasWsnTarg(bool force_refresh, bool no_card_size) const {
    const auto value = summSkin();
    if (!value) return false;
    const auto version = summary_ ? summary_->dataVersion()
                                  : String(versioninfo::LATEST_VERSION);
    if (type_ == MtType::CARD) return value->hasWsnCardImage(version, force_refresh, no_card_size);
    if (type_ == MtType::BG_IMG) return value->hasWsnBgImage(version, force_refresh, no_card_size);
    if (type_ == MtType::BGM) return value->hasWsnBGM(version, force_refresh);
    return value->hasWsnSE(version, force_refresh);
}

std::vector<String> MaterialSelectBase::targsImpl(StringView directory,
                                                   bool force_refresh) const {
    const auto value = summSkin();
    if (!value) return {};
    const bool logical_sort = prop_ && prop_->var().logicalSort.value;
    if (type_ == MtType::CARD) return value->cards(directory, logical_sort, force_refresh,
                                                   no_card_size_, can_include_ && !is_menu_card_);
    if (type_ == MtType::BG_IMG) return value->tables(directory, logical_sort, force_refresh,
                                                       excludeCardSizeImage());
    if (type_ == MtType::BGM) return value->musics(directory, logical_sort, force_refresh);
    return value->sounds(directory, logical_sort, force_refresh);
}

lib::ResourceId MaterialSelectBase::image(StringView file) const {
    if (!prop_) return 0;
    const auto change = currentChange();
    if (!change.current()) return 0;
    auto* props = prop_;
    const auto type = type_;
    const bool included = can_include_ && !is_menu_card_;
    if (type == MtType::BGM || type == MtType::SE || file.empty())
        return materialRowIcon(*props, type, {}, nullptr, included);
    const auto resolved = resolveCurrentFile(file);
    if (!change.current()) return 0;
    const auto skin = summSkin();
    if (!change.current()) return 0;
    const auto resolved_value = pathToUtf8(resolved);
    return materialRowIcon(*props, type, resolved_value, skin.get(), included);
}

void MaterialSelectBase::doRefresh() {
    const auto change = currentChange();
    const auto callback = refresh_callback_;
    refreshPaths({}, true, true);
    if (change.current() && callback) callback();
}

void MaterialSelectBase::doDirectory() {
    // ALL（Web/Android）にはOSのファイル管理ソフトがないため、 ホストの Data Explorer
    // でフォルダーを表示する（lib::editor_open_data_directory）。
    const bool data_explorer = lib::editor_window_host_options().all_mode &&
                               lib::editor_can_open_data_directory();
    if (!services_.shell && !data_explorer) return;
    const auto shell = services_.shell;
    const auto skin = summSkin();
    MaterialDirectoryQuery query;
    if (table_dir_ >= 0 && selected_dir_ == table_dir_) {
        for (const auto& directory : defDirs())
            query.default_directories.push_back(pathFromUtf8(directory));
        const auto type = type_;
        const bool no_card_size = type == MtType::CARD ? no_card_size_ : excludeCardSizeImage();
        const bool included = can_include_ && !is_menu_card_;
        query.contains_material = [skin, type, no_card_size, included](const auto& directory) {
            return skin && containsMaterial(*skin, type, pathToUtf8(directory), false, no_card_size, included);
        };
    } else if (engine_dir_ >= 0 && selected_dir_ == engine_dir_) {
        const auto engine = prop_ ? prop_->enginePath() : String{};
        if (engine.empty()) return;
        query.selected_directory = pathFromUtf8(engine).parent_path() / "Data" / "Materials";
    } else if (summary_) {
        const auto scenario_root_selected = selected_dir_ >= 0 &&
            static_cast<std::size_t>(selected_dir_) < directory_entries_.size() &&
            directory_entries_[static_cast<std::size_t>(selected_dir_)].kind ==
                DirectoryEntry::Kind::Scenario &&
            directory_entries_[static_cast<std::size_t>(selected_dir_)].label == "/";
        const auto current = currentDir();
        query.scenario_root = pathFromUtf8(summary_->scenarioPath());
        // D版の currentDir は、選択された「
        // /」を空だが非null、定義を null とし
        // て区別する。 C++の String はどちら
        // も空になるため、行の識別情報で区別を保持する。
        if (scenario_root_selected) query.selected_directory = query.scenario_root;
        else if (!current.empty()) query.selected_directory = pathFromUtf8(fromViewPath(current));
        if (skin) query.material_directory = pathFromUtf8(skin->materialPath());
    }
    const auto directory = materialDirectoryToOpen(std::move(query));
    if (directory.empty()) return;
    if (data_explorer) {
        // データ画面への移動が、このダイアログの所有者を閉じる
        // 場合がある。 呼び出し後はメンバーにアクセスしない。
        (void)lib::editor_open_data_directory(directory);
        return;
    }
    std::string error;
    // OSへの転送が所有者を閉じる場合があるため、その後はメンバーにアクセスしない。
    shell->open_path(directory, error);
}

void MaterialSelectBase::CSListener::widgetSelected() {
    if (owner_ && owner_->dirs_ && owner_->dirs_->selected_index) {
        owner_->selectDir(static_cast<int>(*owner_->dirs_->selected_index));
    }
}

void MaterialSelectBase::LSListener::widgetSelected() {
    if (owner_) owner_->selectFile();
}

void MaterialSelectBase::selectFile() {
    if (!file_list_ || file_list_->selection.empty()) return;
    const auto index = file_list_->selection.front();
    if (file_combo_)
        (void)lib::select_combo_item(*file_combo_, index, false);
    if (index >= file_list_->rows.size() || (none_row_ && index == 0)) return;
    const auto change = beginChange();
    const auto revision = change.lifetime->rows_revision;
    const auto file = fileText(file_list_->rows[index]);
    const auto refresh_callback = refresh_callback_;
    const auto old = path();
    if (!change.current() || change.lifetime->rows_revision != revision) return;
    String selected_path;
    if (all_list_) {
        int directory_index = -1;
        String target = file;
        if (!file.empty() && file.front() == '/') {
            selected_path = file.substr(1);
            target = baseName(selected_path);
            const auto parent = dirName(selected_path);
            for (std::size_t i = 0; i < directory_entries_.size(); ++i) {
                if (directory_entries_[i].kind != DirectoryEntry::Kind::Scenario) continue;
                auto label = toViewPath(directory_entries_[i].label);
                while (!label.empty() && label.front() == '/') label.erase(label.begin());
                if (label == toViewPath(parent)) { directory_index = static_cast<int>(i); break; }
            }
        } else {
            selected_path = file;
            const auto row = static_cast<std::ptrdiff_t>(index);
            directory_index = engine_index_ >= 0 && row >= engine_index_ && row < engine_index_to_
                ? engine_dir_ : table_dir_;
        }
        if (directory_index < 0 || !dirs_) return;
        const auto requested_entry = directory_entries_[static_cast<std::size_t>(directory_index)];
        const auto requested_combo = dirs_;
        const auto requested_table = file_list_;
        selected_dir_ = directory_index;
        (void)lib::select_combo_item(*dirs_, static_cast<std::size_t>(directory_index), false);
        refreshList();
        if (!change.current()) return;
        if (dirs_ != requested_combo || file_list_ != requested_table || selected_dir_ != directory_index ||
            static_cast<std::size_t>(directory_index) >= directory_entries_.size() ||
            directory_entries_[directory_index].label != requested_entry.label ||
            directory_entries_[directory_index].kind != requested_entry.kind) return;
        const auto refreshed_rows_revision = change.lifetime->rows_revision;
        // 部分文字列ではなく、行そのものを正確に照合する（foo.bmp と oldfoo.bmp を区別する）。
        const auto row = std::find_if(requested_table->rows.begin(), requested_table->rows.end(),
            [&](const lib::TableRow& value) { return fileText(value) == target; });
        if (row != requested_table->rows.end()) {
            const auto selected = static_cast<std::size_t>(row - requested_table->rows.begin());
            setSelection(selected);
            // setSelection は表のリスナーを通知する。内側の selectFile
            // がアプリのコールバックを呼び、 コントロールを置き換えたり、
            // 共有所有されていないこの所有者を破棄したりする場合がある。 メ
            // ンバーを読む・行を表示する前に、反映状態の有効性を検証する。
            if (!change.current() || change.lifetime->rows_revision != refreshed_rows_revision ||
                file_list_ != requested_table) return;
            (void)lib::reveal_table_row(*requested_table, selected);
        }
    } else {
        selected_path = pathToUtf8(pathFromUtf8(currentDir()) / pathFromUtf8(file));
    }
    if (!applyPath(selected_path, old, false, true, -1, true, change)) return;
    if (old == selected_path) {
        refreshButtons();
        if (change.current() && refresh_callback) refresh_callback();
    }
}

void MaterialSelectBase::OpenMaterial::mouseDoubleClick(lib::PointerEvent& event) {
    if (owner_ && event.button == lib::PointerButton::Primary) owner_->openFilePath();
}

void MaterialSelectBase::OpenMaterial::keyPressed(lib::KeyEvent& event) {
    if (owner_ && event.key == lib::Key::Enter) {
        owner_->openFilePath();
        event.handled = true;
    }
}

void MaterialSelectBase::openFilePath() {
    auto* const editor_commons = liveEditorCommons();
    const auto editor_commons_lifetime = editor_commons_lifetime_;
    if (read_only_ || !editor_commons || selected_dir_ < 0) return;
    const auto change = currentChange();
    const auto revision = change.lifetime->rows_revision;
    const auto defs = getDefs();
    if (!change.current() || change.lifetime->rows_revision != revision) return;
    if (selected_dir_ < static_cast<int>(defs.size()) || selected_dir_ == table_dir_ ||
        selected_dir_ == engine_dir_) return;
    const auto file = filePath();
    if (!change.current() || change.lifetime->rows_revision != revision || file.empty()) return;
    if (editor_commons_ != editor_commons || editor_commons_lifetime.expired()) return;
    editor_commons->openFilePath(file, false, true);
}

void MaterialSelectBase::disconnectEditorCommons() noexcept {
    auto* const editor_commons = editor_commons_;
    const auto editor_commons_lifetime = editor_commons_lifetime_;
    editor_commons_ = nullptr;
    editor_commons_lifetime_.reset();
    // 長く存続する選択欄より先に Commons が無効になる場合があるため、有効なシグナルだけに触れる。
    if (editor_commons && !editor_commons_lifetime.expired()) {
        editor_commons->refDataVersion.remove(ref_version_connection_);
        editor_commons->refSkin.remove(ref_skin_connection_);
        editor_commons->refMenu.remove(ref_menu_connection_);
    }
    ref_version_connection_ = 0;
    ref_skin_connection_ = 0;
    ref_menu_connection_ = 0;
}

commons::Commons* MaterialSelectBase::liveEditorCommons() const noexcept {
    return editor_commons_ && !editor_commons_lifetime_.expired()
        ? editor_commons_ : nullptr;
}

void MaterialSelectBase::bindEditorCommons(commons::Commons* comm) {
    if (editor_commons_ == comm && (!comm || !editor_commons_lifetime_.expired())) return;
    disconnectEditorCommons();
    if (!comm) return;
    const auto editor_commons_lifetime = comm->lifetimeToken();
    if (editor_commons_lifetime.expired()) return;
    editor_commons_ = comm;
    editor_commons_lifetime_ = editor_commons_lifetime;
    const std::weak_ptr<PublicationLifetime> lifetime = publication_lifetime_;
    const auto resolve = [lifetime, editor_commons_lifetime, comm]()
        -> MaterialSelectBase* {
        const auto state = lifetime.lock();
        if (editor_commons_lifetime.expired() || !state || !state->owner) return nullptr;
        auto* const self = state->owner;
        if (self->editor_commons_ != comm || self->editor_commons_lifetime_.expired())
            return nullptr;
        return self;
    };
    ref_version_connection_ = comm->refDataVersion.add([resolve] {
        auto* self = resolve();
        if (!self) return;
        self->refDataVersion();
        self = resolve();
        if (!self) return;
        self->refreshFileListMenu();
        self = resolve();
        if (self) self->refresh();
    });
    if (summary_ && !summary_->scenarioPath().empty()) {
        ref_skin_connection_ = comm->refSkin.add([resolve] {
            if (auto* self = resolve()) self->refSkin();
        });
    }
    ref_menu_connection_ = comm->refMenu.add([resolve](const types::MenuID changed) {
        auto* self = resolve();
        if (!self) return;
        if (self->type_ != MtType::BGM || !self->context_menu_ || !self->prop_) return;
        // D版と同様、トグル状態は選択パスと再生中の曲の関係で決める。 開
        // いているポップアップに翻訳済みメニューの更新が届いた場合も同様。
        const bool play = self->isPlayOrStop();
        self = resolve();
        if (!self || self->type_ != MtType::BGM ||
            !self->context_menu_ || !self->prop_) return;
        const auto id = !self->playing_.empty() && !play
            ? types::MenuID::StopBGM : types::MenuID::PlayBGM;
        if (changed != id) return;
        // D版の addRefMenu と同様、既存項目を更新して整形処理を再適用す
        // る。 ここでメニューを作り直すと、開いているポップアップが古いままになる。
        const auto menu = self->context_menu_;
        const auto active_path = self->playing_;
        const auto change = self->currentChange();
        auto text = self->prop_->buildMenu(id);
        self = resolve();
        if (!change.current() || !self ||
            self->context_menu_ != menu) return;
        if (id == types::MenuID::StopBGM)
            text = ::cwxeditor::cwx::utils::tryFormat(text, active_path);
        for (auto& item : menu->items) {
            if (item.command != "play-bgm") continue;
            item.text = std::move(text);
            ++menu->item_update_revision;
            break;
        }
    });
    refDataVersion();
}

String MaterialSelectBase::fromViewPath(String value) {
    std::replace(value.begin(), value.end(), '/',
                 static_cast<char>(std::filesystem::path::preferred_separator));
    return value;
}

String MaterialSelectBase::toViewPath(String value) {
    std::replace(value.begin(), value.end(),
                 static_cast<char>(std::filesystem::path::preferred_separator), '/');
    return value;
}

String MaterialSelectBase::currentDir() const {
    if (selected_dir_ < 0 || static_cast<std::size_t>(selected_dir_) >= directory_entries_.size()) return {};
    const auto& entry = directory_entries_[static_cast<std::size_t>(selected_dir_)];
    // 新たにアプリの定義取得コールバックを呼ばず、公開済みの項目を解決する。 動的な
    // 提供処理は、ディレクトリ一覧を置き換えたり選択欄を破棄したりする場合がある。
    if (entry.kind != DirectoryEntry::Kind::Scenario) return {};
    return entry.label == "/" ? String{} : toViewPath(entry.label);
}

String MaterialSelectBase::fileText(const lib::TableRow& item) const {
    return item.cells.empty() ? String{} : item.cells.front().text;
}

std::ptrdiff_t MaterialSelectBase::indexOf(const lib::ComboBox& list,
                                           StringView value) const {
    for (std::size_t i = 0; i < list.items.size(); ++i) {
        if (containsInsensitive(list.items[i].text, value)) return static_cast<std::ptrdiff_t>(i);
    }
    return -1;
}

std::ptrdiff_t MaterialSelectBase::indexOf(const lib::TableView& list,
                                           StringView value) const {
    for (std::size_t i = 0; i < list.rows.size(); ++i) {
        if (containsInsensitive(fileText(list.rows[i]), value)) return static_cast<std::ptrdiff_t>(i);
    }
    return -1;
}

std::ptrdiff_t MaterialSelectBase::flIndexOf(StringView value) const {
    return file_list_ ? indexOf(*file_list_, value) : -1;
}

std::ptrdiff_t MaterialSelectBase::dirsIndexOf(StringView value) const {
    return dirs_ ? indexOf(*dirs_, value) : -1;
}

std::vector<String> MaterialSelectBase::targs(StringView directory, bool force_refresh,
                                              bool sub_thread) const {
    (void)sub_thread;
    const auto lifetime = publication_lifetime_;
    const auto revision = lifetime->rows_revision;
    auto result = collectTargets(directory, force_refresh);
    if (!lifetime->owner || lifetime->rows_revision != revision) return {};
    can_inc_search_ = can_inc_search_ || result.searchable;
    return std::move(result.names);
}

MaterialSelectBase::TargetFiles MaterialSelectBase::collectTargets(StringView directory,
                                                                   bool force_refresh) const {
    const auto lifetime = publication_lifetime_;
    const auto revision = lifetime->rows_revision;
    const bool image_kind = type_ == MtType::CARD || type_ == MtType::BG_IMG;
    const auto scaled_provider = load_scaled_image_;
    const bool default_scaled = summary_ && summary_->loadScaledImage();
    const bool standard_material = !summary_ || summary_->scenarioPath().empty() ||
        !::cwxeditor::cwx::utils::isSubDirectory(directory, StringView(summary_->scenarioPath()));
    const bool use_provider = summary_ && !summary_->legacy() && bool(scaled_provider);
    const auto ignored = prop_ ? prop_->var().ignorePaths.value : std::vector<String>{};
    const auto search = inc_search_;
    const auto targets = targsImpl(directory, force_refresh);
    if (!lifetime->owner || lifetime->rows_revision != revision) return {};
    const bool scaled = image_kind && (standard_material ||
        (use_provider ? scaled_provider() : default_scaled));
    if (!lifetime->owner || lifetime->rows_revision != revision) return {};
    TargetFiles result;
    for (auto value : targets) {
        if (scaled) {
            const auto value_path = pathFromUtf8(value);
            if (!::cwxeditor::cwx::utils::noScaledPath(value_path).empty()) continue;
        }
        // D版の ignorePaths と同様、選択ディレクトリ内のファイルではなく、走査
        // するディレクトリを除外する。 ALL は単独エディタとは別の既存動作を維持する。
        if (lib::editor_window_host_options().all_mode &&
            std::find(ignored.begin(), ignored.end(), baseName(value)) != ignored.end()) continue;
        result.searchable = true;
        if (search.match(baseName(value))) result.names.push_back(std::move(value));
    }
    return result;
}

void MaterialSelectBase::refreshListImpl(StringView directory, bool force_refresh,
                                         bool sub_thread) {
    (void)sub_thread;
    const auto lifetime = publication_lifetime_;
    const auto revision = ++lifetime->rows_revision;
    auto location = fileLocation();
    location.all_list = false;
    const auto selected = selected_dir_ >= 0 ? path() : String{};
    if (!lifetime->owner || lifetime->rows_revision != revision) return;
    auto targets = collectTargets(directory, force_refresh);
    if (!lifetime->owner || lifetime->rows_revision != revision) return;
    auto rows = prepareRows(std::move(targets.names), location, selected, revision);
    if (!rows) return;
    if(!commitRows(*rows, false, targets.searchable)) return;
    notifyLoaded(rows->loaded, revision);
}

void MaterialSelectBase::refreshListImpl(const std::vector<String>& paths, bool all_list,
                                         bool force_refresh, bool sub_thread,
                                         std::ptrdiff_t skin_pos,
                                         std::ptrdiff_t engine_pos_from,
                                         std::ptrdiff_t engine_pos_to) {
    (void)force_refresh;
    (void)sub_thread;
    const auto lifetime = publication_lifetime_;
    const auto revision = ++lifetime->rows_revision;
    auto location = fileLocation();
    location.all_list = all_list;
    const auto selected = selected_dir_ >= 0 ? path() : String{};
    if (!lifetime->owner || lifetime->rows_revision != revision) return;
    auto targets = collectTargets(paths, all_list, force_refresh, skin_pos, engine_pos_from, engine_pos_to);
    if (!lifetime->owner || lifetime->rows_revision != revision) return;
    auto rows = prepareRows(std::move(targets.names), location, selected, revision);
    if (!rows) return;
    if(!commitRows(*rows, all_list, targets.searchable, targets.engine_begin, targets.engine_end)) return;
    notifyLoaded(rows->loaded, revision);
}

MaterialSelectBase::TargetFiles MaterialSelectBase::collectTargets(
    const std::vector<String>& paths, bool all_list, bool force_refresh,
    std::ptrdiff_t skin_pos, std::ptrdiff_t engine_pos_from, std::ptrdiff_t engine_pos_to) const {
    const auto lifetime = publication_lifetime_;
    const auto revision = lifetime->rows_revision;
    const auto directories = paths;
    const bool has_summary = summary_ != nullptr;
    const String scenario_root = summary_ ? summary_->scenarioPath() : String{};
    const auto skin_count = static_cast<std::ptrdiff_t>(defDirs().size());
    std::vector<String> definitions;
    std::vector<String> engine;
    std::vector<String> scenario;
    bool searchable = false;
    for (std::size_t i = 0; i < directories.size(); ++i) {
        // D版と同様、skinPos だけを接頭辞なしの標準グループとする。 後の
        // 標準ディレクトリは、元の場所に対する相対ラベルと別のグループを維持する。
        const bool is_skin = skin_pos >= 0 &&
            (lib::editor_window_host_options().all_mode
                 ? static_cast<std::ptrdiff_t>(i) >= skin_pos &&
                       static_cast<std::ptrdiff_t>(i) - skin_pos < skin_count
                 : static_cast<std::ptrdiff_t>(i) == skin_pos);
        const bool is_engine = engine_pos_from >= 0 &&
                               static_cast<std::ptrdiff_t>(i) >= engine_pos_from &&
                               static_cast<std::ptrdiff_t>(i) < engine_pos_to;
        auto targets = collectTargets(directories[i], force_refresh);
        if (!lifetime->owner || lifetime->rows_revision != revision) return {};
        searchable = searchable || targets.searchable;
        for (auto& value : targets.names) {
            if (all_list) {
                if (!is_skin && !is_engine && has_summary) {
                    const String relative = abs2rel(StringView(directories[i]),
                                                    StringView(scenario_root));
                    value = relative.empty() || relative == "." ? "/" + value
                        : "/" + toViewPath(relative) + "/" + value;
                    while (value.find("//") != String::npos) value.erase(value.find("//"), 1);
                }
            }
            if (is_skin) definitions.push_back(std::move(value));
            else if (is_engine) engine.push_back(std::move(value));
            else scenario.push_back(std::move(value));
        }
    }
    const auto less = [logical = prop_ && prop_->var().logicalSort.value](const String& a, const String& b) {
        return (logical ? ::cwxeditor::cwx::utils::fnncmp(a, b)
                        : ::cwxeditor::cwx::utils::fncmp(a, b)) < 0;
    };
    std::stable_sort(definitions.begin(), definitions.end(), less);
    std::stable_sort(engine.begin(), engine.end(), less);
    std::stable_sort(scenario.begin(), scenario.end(), less);
    const auto engine_begin = engine.empty() ? -1 : static_cast<std::ptrdiff_t>(definitions.size());
    const auto engine_end = engine.empty() ? -1 : engine_begin + static_cast<std::ptrdiff_t>(engine.size());
    definitions.insert(definitions.end(), engine.begin(), engine.end());
    definitions.insert(definitions.end(), scenario.begin(), scenario.end());
    return {std::move(definitions), searchable, engine_begin, engine_end};
}

std::vector<String> MaterialSelectBase::getWarnings(const lib::TableRow& item) const {
    return item.cells.empty() ? std::vector<String>{} : warningFor(item.cells.front().text);
}

void MaterialSelectBase::refreshListImpl2(const std::vector<String>& targets) {
    if (!file_list_) return;
    const auto lifetime = publication_lifetime_;
    const auto revision = ++lifetime->rows_revision;
    const auto location = fileLocation();
    const auto selected = selected_dir_ >= 0 ? path() : String{};
    if (!lifetime->owner || lifetime->rows_revision != revision) return;
    auto rows = prepareRows(targets, location, selected, revision);
    if (!rows) return;
    if(!commitRows(*rows, all_list_, !targets.empty() || can_inc_search_)) return;
    notifyLoaded(rows->loaded, revision);
}

std::optional<MaterialSelectBase::PreparedRows> MaterialSelectBase::prepareRows(
    std::vector<String> names, const FileLocation& location, String selected,
    std::uint64_t revision) const {
    if (!file_list_) return std::nullopt;
    const auto lifetime = publication_lifetime_;
    const auto current = [lifetime, revision] {
        return lifetime->owner && lifetime->rows_revision == revision;
    };
    PreparedRows result;
    result.table = file_list_;
    result.none_row = file_combo_ && location.kind == DirectoryEntry::Kind::Definition;
    result.rows.reserve(names.size() + (result.none_row ? 1U : 0U));
    result.paths.reserve(names.size());
    if (result.none_row) {
        lib::TableRow none;
        lib::ListItem item;
        item.text = prop_ ? prop_->requiredMsgs().defaultSelection(
                                prop_->requiredMsgs().fileNone.value)
                          : String{"None"};
        item.value = {};
        none.cells.push_back(std::move(item));
        result.rows.push_back(std::move(none));
    }
    std::optional<std::size_t> selected_row;
    if (!selected.empty()) {
        const auto name = baseName(selected);
        // D版の flIndexOf と同様に cfnmatch を使い、OSごとのファイル名の大文字小文字の規則を保つ。
        auto found = std::find_if(names.begin(), names.end(), [&](const String& candidate) {
            return ::cwxeditor::cwx::utils::cfnmatch(candidate, name);
        });
        if (found == names.end()) {
            for (const auto& ext : defExts()) {
                auto candidate = pathFromUtf8(name);
                candidate.replace_extension(ext);
                const auto candidate_name = pathToUtf8(candidate);
                found = std::find_if(names.begin(), names.end(), [&](const String& value) {
                    return ::cwxeditor::cwx::utils::cfnmatch(value, candidate_name);
                });
                if (found != names.end()) break;
            }
        }
        if (found != names.end()) selected_row = static_cast<std::size_t>(found - names.begin());
    }
    if (selected_row) result.selection.push_back(*selected_row + (result.none_row ? 1U : 0U));
    else if (result.none_row) result.selection.push_back(0U);
    // コーデック・提供処理を呼ぶ前にリスナーを複製する。 通知中の
    // loadedEvent の変更は、次回の通知から反映する。
    result.loaded = loadedEvent;
    auto* props = prop_;
    const auto change = currentChange();
    const auto type = type_;
    const bool included = can_include_ && !is_menu_card_;
    const auto skin = summSkin();
    if (!current() || !change.current()) return std::nullopt;
    std::vector<std::filesystem::path> resolved;
    resolved.reserve(names.size());
    for (const auto& value : names) {
        resolved.push_back(resolveFile(value, location));
        if (!current() || !change.current()) return std::nullopt;
    }
    if (!current() || !change.current()) return std::nullopt;
    for (std::size_t i = 0; i < names.size(); ++i) {
        const auto& value = names[i];
        String display_path = value;
        if (location.all_list && !value.empty() && value.front() == '/') {
            display_path = normalizePath(value.substr(1));
        } else if (!location.current_directory.empty()) {
            display_path = normalizePath(pathToUtf8(
                pathFromUtf8(location.current_directory) / pathFromUtf8(value)));
        }
        result.paths.push_back(std::move(display_path));
        lib::TableRow row;
        lib::ListItem item;
        item.text = value;
        item.value = value;
        const auto resolved_value = pathToUtf8(resolved[i]);
        item.icon = props && !value.empty()
            ? materialRowIcon(*props, type, resolved_value, skin.get(), included) : 0;
        if (!current() || !change.current()) return std::nullopt;
        row.warnings = warningFor(value);
        if (!current()) return std::nullopt;
        item.warnings = row.warnings;
        row.cells.push_back(std::move(item));
        result.rows.push_back(std::move(row));
    }
    if (!current()) return std::nullopt;
    result.names = std::move(names);
    return result;
}

bool MaterialSelectBase::commitRows(PreparedRows& rows, bool all_list, bool searchable,
                                    std::ptrdiff_t engine_begin, std::ptrdiff_t engine_end,
                                    lib::PreparedItemModelUpdate* companion) {
    if (!rows.table) return false;
    const auto lifetime=publication_lifetime_;
    const auto revision=lifetime->rows_revision;
    const auto change=lifetime->change_revision;
    const auto notification=lifetime->notification_revision;
    const auto reveal=rows.selection.empty()?std::optional<std::size_t>{}:rows.selection.front();
    std::optional<lib::PreparedItemModelUpdate> file_combo_update;
    if (file_combo_) {
        std::vector<lib::ChoiceItem> choices;
        choices.reserve(rows.rows.size());
        const auto offset = rows.none_row ? std::size_t{1} : std::size_t{0};
        for (std::size_t index = 0; index < rows.rows.size(); ++index) {
            const auto text = fileText(rows.rows[index]);
            if (rows.none_row && index == 0U) {
                choices.push_back({text, {}, {}, true});
                continue;
            }
            const auto path_index = index - offset;
            if (path_index >= rows.paths.size()) return false;
            choices.push_back({text, rows.paths[path_index], {}, true});
        }
        file_combo_update.emplace(lib::prepare_combo_items(
            file_combo_, std::move(choices), reveal, {},
            [lifetime,revision,change,notification] {
                return lifetime->owner && lifetime->rows_revision==revision &&
                    lifetime->change_revision==change && lifetime->notification_revision==notification;
            }));
    }
    auto table_update=lib::prepare_table_rows(rows.table, std::move(rows.rows),
        std::move(rows.selection), reveal,
        [lifetime,revision,change,notification] {
            return lifetime->owner && lifetime->rows_revision==revision &&
                lifetime->change_revision==change && lifetime->notification_revision==notification;
        });
    if(!table_update.current() || (companion && !companion->current()) ||
       (file_combo_update && !file_combo_update->current())) return false;
    // これらの確定処理の間では、提供処理・コールバック・メモリ確保・ネイティブ
    // 呼び出しを行わない。 付随データも含む、一つの共通の反映区間として扱う。
    if(!table_update.commit()) return false;
    if(companion && !companion->commit()) return false;
    if(file_combo_update && !file_combo_update->commit()) return false;
    showing_names_.swap(rows.names);
    showing_paths_.clear();
    all_list_ = all_list;
    const auto row_offset = rows.none_row ? std::ptrdiff_t{1} : std::ptrdiff_t{0};
    engine_index_ = engine_begin < 0 ? engine_begin : engine_begin + row_offset;
    engine_index_to_ = engine_end < 0 ? engine_end : engine_end + row_offset;
    none_row_ = rows.none_row;
    can_inc_search_ = searchable;
    loading_ = false;
    rows.table->set_enabled(enabled_ && !read_only_);
    if (file_combo_) file_combo_->set_enabled(enabled_ && !read_only_);
    return true;
}

void MaterialSelectBase::notifyLoaded(const std::vector<Action>& callbacks, std::uint64_t revision) {
    const auto lifetime = publication_lifetime_;
    const auto current = [lifetime, revision] {
        return lifetime->owner && lifetime->rows_revision == revision;
    };
    if (!current()) return;
    for (const auto& callback : callbacks) {
        if (callback) callback();
        if (!current()) return;
    }
    temp_path_.clear();
    refreshButtons(false);
}

std::vector<String> MaterialSelectBase::allDirs(std::ptrdiff_t& skin_pos,
                                                std::ptrdiff_t& engine_pos_from,
                                                std::ptrdiff_t& engine_pos_to) const {
    const auto entries = directory_entries_;
    const auto location = fileLocation();
    const auto defs = getDefs();
    return directorySources(entries, defs.size(), location, skin_pos, engine_pos_from, engine_pos_to);
}

std::vector<String> MaterialSelectBase::directorySources(
    const std::vector<DirectoryEntry>& entries, std::size_t definition_count,
    const FileLocation& location, std::ptrdiff_t& skin_pos,
    std::ptrdiff_t& engine_pos_from, std::ptrdiff_t& engine_pos_to) {
    std::vector<String> result;
    skin_pos = -1;
    engine_pos_from = -1;
    engine_pos_to = -1;
    for (std::size_t i = definition_count; i < entries.size(); ++i) {
        const auto& entry = entries[i];
        if (entry.kind == DirectoryEntry::Kind::Skin) {
            if (skin_pos < 0) skin_pos = static_cast<std::ptrdiff_t>(result.size());
            result.insert(result.end(), location.default_dirs.begin(), location.default_dirs.end());
        } else if (entry.kind == DirectoryEntry::Kind::Engine) {
            if (engine_pos_from < 0) engine_pos_from = static_cast<std::ptrdiff_t>(result.size());
            result.insert(result.end(), location.engine_dirs.begin(), location.engine_dirs.end());
        } else {
            if (engine_pos_from >= 0 && engine_pos_to < 0) {
                engine_pos_to = static_cast<std::ptrdiff_t>(result.size());
            }
            if (location.has_summary) {
                result.push_back(entry.label == "/"
                                     ? location.scenario_root
                                     : pathToUtf8(pathFromUtf8(location.scenario_root) /
                                                  pathFromUtf8(fromViewPath(entry.label))));
            }
        }
    }
    if (engine_pos_from >= 0 && engine_pos_to < 0) engine_pos_to = result.size();
    return result;
}

void MaterialSelectBase::refreshList(bool force_refresh, bool sub_thread) {
    (void)sub_thread;
    if (!file_list_ || !dirs_) return;
    const auto lifetime = publication_lifetime_;
    const auto revision = ++lifetime->rows_revision;
    const auto defs = getDefs();
    if (!lifetime->owner || lifetime->rows_revision != revision) return;
    if (selected_dir_ < 0 || selected_dir_ >= static_cast<int>(directory_entries_.size())) {
        const auto target=file_list_;
        const auto current = [lifetime,revision] {
            return lifetime->owner && lifetime->rows_revision==revision;
        };
        std::optional<lib::PreparedItemModelUpdate> combo_update;
        if (file_combo_) {
            combo_update.emplace(lib::prepare_combo_items(file_combo_, {}, std::nullopt, {}, current));
            if (!combo_update->current()) return;
        }
        if(!lib::replace_table_rows(*target, {}, {}, {}, current)) return;
        if (combo_update && !combo_update->commit()) return;
        showing_names_.clear();
        showing_paths_.clear();
        none_row_ = false;
        can_inc_search_ = false;
        loading_ = false;
        return;
    }
    if (selected_dir_ < static_cast<int>(defs.size())) {
        std::ptrdiff_t skin_pos, engine_pos_from, engine_pos_to;
        const auto dirs = allDirs(skin_pos, engine_pos_from, engine_pos_to);
        if (!lifetime->owner || lifetime->rows_revision != revision) return;
        refreshListImpl(dirs, true, force_refresh, false, skin_pos, engine_pos_from,
                        engine_pos_to);
        return;
    }
    if (selected_dir_ == table_dir_) {
        table_index_ = 0;
        refreshListImpl(defDirs(), false, force_refresh, false, -1, 0,
                        static_cast<std::ptrdiff_t>(defDirs().size()));
    } else if (selected_dir_ == engine_dir_) {
        engine_index_ = 0;
        refreshListImpl(engineDefDirs(), false, force_refresh, false, -1, 0,
                        static_cast<std::ptrdiff_t>(engineDefDirs().size()));
    } else {
        String directory = summary_ ? summary_->scenarioPath() : String{};
        const auto current = currentDir();
        if (!current.empty()) directory = pathToUtf8(pathFromUtf8(directory) /
                                                     pathFromUtf8(fromViewPath(current)));
        refreshListImpl(directory, force_refresh, false);
    }
}

std::vector<String> MaterialSelectBase::searchTarg(const Skin& value,
                                                   StringView directory,
                                                   bool force_refresh,
                                                   std::size_t cut) const {
    ScenarioDirectoryScanInput input;
    input.skin_owner = summSkin();
    input.skin = &value;
    input.type = type_;
    input.scenario_root = String(directory);
    input.ignored_paths = prop_ ? prop_->var().ignorePaths.value : std::vector<String>{};
    input.force_refresh = force_refresh;
    input.no_card_size = type_ == MtType::CARD ? no_card_size_
        : type_ == MtType::BG_IMG ? excludeCardSizeImage() : false;
    input.included = can_include_ && !is_menu_card_;
    input.scan_scenario = true;
    std::atomic_bool not_obsolete{};
    return searchScenarioMaterials(input, directory, cut, not_obsolete);
}

void MaterialSelectBase::reapRefreshTask(
    const std::shared_ptr<RefreshTaskState>& task) {
    const std::weak_ptr<PublicationLifetime> weak = publication_lifetime_;
    const auto remove = [weak, task] {
        const auto lifetime = weak.lock();
        if (!lifetime || !lifetime->owner) return;
        auto& tasks = lifetime->owner->refresh_tasks_;
        std::erase(tasks, task);
    };
    MaterialSelectBase::scheduleRefreshTaskReap(task, services_.dispatcher, remove);
}

void MaterialSelectBase::invalidateAsyncRefresh() {
    const auto lifetime = publication_lifetime_;
    ++lifetime->async_refresh_revision;
    const bool was_loading = loading_;
    loading_ = false;
    const auto tasks = refresh_tasks_;
    for (const auto& task : tasks) {
        if (!task) continue;
        task->obsolete.store(true, std::memory_order_release);
        if (task->handle && !task->handle->finished()) task->handle->cancel();
        reapRefreshTask(task);
    }
    if (was_loading) {
        updateEnabledControls(false);
        refreshFileListMenu();
    }
}

bool MaterialSelectBase::beginAsyncRefresh(const RefreshRequest& request) {
    const auto dispatcher = services_.dispatcher;
    const auto tasks = services_.tasks;
    if (!dirs_ || directorySelectorIsVisible() || !dispatcher || !tasks ||
        !dispatcher->is_main_thread()) return false;

    // D版と同様、非表示のディレクトリ更新はバックグラウンドで行う。
    // UIスレッド上で Props・Summary・提供側の値をすべて
    // 複製し、 下のワーカーには所有する値とスキンの強参照だけを渡す。
    auto location = fileLocation();
    const auto skin = summSkin();

    const auto lifetime = publication_lifetime_;
    const auto initial_rows_revision = lifetime->rows_revision;
    auto publication_snapshot = std::make_shared<RefreshPathSnapshot>();
    publication_snapshot->definitions = getDefs();
    if (!lifetime->owner || lifetime->rows_revision != initial_rows_revision) return false;
    publication_snapshot->default_label = prop_->requiredMsgs().defaultSelection(
        prop_->requiredMsgs().pathDef.value);
    const auto version = summary_ ? summary_->dataVersion()
                                  : String(versioninfo::LATEST_VERSION);
    auto version_it = std::find(versioninfo::VERSIONS.begin(), versioninfo::VERSIONS.end(), version);
    if (version_it == versioninfo::VERSIONS.end()) {
        version_it = std::find(versioninfo::VERSIONS.begin(), versioninfo::VERSIONS.end(),
                               versioninfo::LATEST_VERSION);
    }
    const auto version_index = version_it == versioninfo::VERSIONS.end()
        ? std::size_t{0}
        : static_cast<std::size_t>(version_it - versioninfo::VERSIONS.begin());
    publication_snapshot->engine_label = prop_->requiredMsgs().defaultSelection(
        ::cwxeditor::cwx::utils::tryFormat(prop_->requiredMsgs().pathWsnBasic.value,
                                          versioninfo::VERSION_NAMES[version_index]));
    if (!lifetime->owner || lifetime->rows_revision != initial_rows_revision) return false;

    const bool no_card_size = type_ == MtType::CARD ? no_card_size_
        : type_ == MtType::BG_IMG ? excludeCardSizeImage() : false;
    const auto scan_snapshot = scanMaterialTargets(
        location, skin, request.force_refresh, no_card_size);
    if (!lifetime->owner || lifetime->rows_revision != initial_rows_revision) return false;

    ScenarioDirectoryScanInput input;
    input.skin_owner = skin;
    input.skin = skin.get();
    input.type = type_;
    input.scenario_root = location.scenario_root;
    input.ignored_paths = prop_ ? prop_->var().ignorePaths.value : std::vector<String>{};
    input.force_refresh = request.force_refresh;
    input.no_card_size = type_ == MtType::CARD ? no_card_size_
        : type_ == MtType::BG_IMG ? excludeCardSizeImage() : false;
    input.included = can_include_ && !is_menu_card_;
    input.scan_scenario = location.has_summary;

    auto task = std::make_shared<RefreshTaskState>();
    task->async_revision = ++lifetime->async_refresh_revision;
    task->rows_revision = ++lifetime->rows_revision;
    const auto previous_tasks = refresh_tasks_;
    for (const auto& previous : previous_tasks) {
        if (!previous) continue;
        previous->obsolete.store(true, std::memory_order_release);
        if (previous->handle && !previous->handle->finished()) previous->handle->cancel();
        reapRefreshTask(previous);
    }
    refresh_tasks_.push_back(task);
    loading_ = true;
    const auto launch_current = [&] {
        return lifetime->owner == this && task->async_revision == lifetime->async_refresh_revision &&
            task->rows_revision == lifetime->rows_revision;
    };
    const auto abandon_launch = [&] {
        task->obsolete.store(true, std::memory_order_release);
        task->handle_ready.store(true, std::memory_order_release);
        if (lifetime->owner == this) std::erase(refresh_tasks_, task);
    };
    const std::weak_ptr<PublicationLifetime> weak = lifetime;
    try {
        updateEnabledControls(false);
        if (!launch_current()) {
            abandon_launch();
            return true;
        }
        refreshFileListMenu();
        if (!launch_current()) {
            abandon_launch();
            return true;
        }
        task->handle = tasks->run(
            [weak, dispatcher, task, request, input = std::move(input),
             publication_snapshot = std::move(publication_snapshot),
             scan_snapshot = std::move(scan_snapshot)]() mutable {
                task->started.store(true, std::memory_order_release);
                std::optional<ScenarioDirectoryScanData> scan;
                std::exception_ptr failure;
                try {
                    scan = scanScenarioDirectories(input, task->obsolete);
                } catch (...) {
                    failure = std::current_exception();
                }
                if (task->obsolete.load(std::memory_order_acquire)) scan.reset();
                task->completion_posted.store(true, std::memory_order_release);
                try {
                    dispatcher->post(
                        [weak, dispatcher, task, request,
                         publication_snapshot = std::move(publication_snapshot),
                         scan_snapshot = std::move(scan_snapshot),
                         scan = std::move(scan), failure]() mutable {
                            const auto lifetime = weak.lock();
                            auto* const owner = lifetime ? lifetime->owner : nullptr;
                            if (owner && !task->obsolete.load(std::memory_order_acquire) &&
                                task->async_revision == lifetime->async_refresh_revision) {
                                if (task->rows_revision == lifetime->rows_revision && scan && !failure) {
                                    ScenarioDirectoryScanResult prepared;
                                    prepared.skin_has_target = scan_snapshot.skin_has_target;
                                    prepared.engine_has_target = scan_snapshot.engine_has_target;
                                    prepared.scenario_directories =
                                        std::move(scan->scenario_directories);
                                    try {
                                        owner->refreshPathsImplWithScan(
                                            request.select, request.force_refresh,
                                            request.update_list, &prepared,
                                            publication_snapshot.get());
                                    } catch (...) {
                                        // その場でのタスク実行や通知の完了をまたいでも
                                        // 、コールバックの失敗を保持する。 反映が失敗
                                        // した場合、設定処理は後続の購読先を呼ばない。
                                        std::lock_guard lock(task->failure_mutex);
                                        task->publication_failure = std::current_exception();
                                    }
                                }
                                if (lifetime->owner == owner &&
                                    task->async_revision == lifetime->async_refresh_revision) {
                                    if (owner->loading_) {
                                        owner->loading_ = false;
                                        owner->updateEnabledControls(false);
                                    }
                                    owner->refreshFileListMenu();
                                }
                            }
                            if (owner && lifetime && lifetime->owner == owner)
                                owner->reapRefreshTask(task);
                            else MaterialSelectBase::scheduleRefreshTaskReap(task, dispatcher);
                        });
                } catch (...) {
                    // 通知処理が停止した場合、所有するUIは終了中。 タス
                    // クハンドルは、その寿命を管理する所有者が回収する。
                }
            });
        task->handle_ready.store(true, std::memory_order_release);
        if (task->completion_posted.load(std::memory_order_acquire) ||
            task->obsolete.load(std::memory_order_acquire)) {
            if (lifetime->owner == this) reapRefreshTask(task);
            else scheduleRefreshTaskReap(task, dispatcher);
        }
        std::exception_ptr publication_failure;
        {
            std::lock_guard lock(task->failure_mutex);
            publication_failure = task->publication_failure;
        }
        if (publication_failure) std::rethrow_exception(publication_failure);
    } catch (...) {
        task->obsolete.store(true, std::memory_order_release);
        task->handle_ready.store(true, std::memory_order_release);
        if (lifetime->owner == this) std::erase(refresh_tasks_, task);
        if (lifetime->owner == this && task->async_revision == lifetime->async_refresh_revision) {
            loading_ = false;
            updateEnabledControls(false);
            if (lifetime->owner == this) refreshFileListMenu();
        }
        throw;
    }
    return true;
}

bool MaterialSelectBase::directorySelectorIsVisible() const {
    const auto gui = services_.gui;
    if (!dirs_ || !gui) return false;
    const auto owner = gui->window_owner(dirs_);
    // ネイティブの所有者が未登録なら、EventDialog::open() 中
    // など、 選択欄を非表示のダイアログ内でまだ組み立てている段階として扱う。
    return owner && lib::widget_is_visible_in(owner, dirs_.get());
}

void MaterialSelectBase::refreshPaths(String select, bool force_refresh, bool update_list) {
    const auto dispatcher = services_.dispatcher;
    const std::weak_ptr<PublicationLifetime> weak = publication_lifetime_;
    auto execute = [weak, select = std::move(select), force_refresh, update_list]() mutable {
        const auto lifetime = weak.lock();
        if (!lifetime || !lifetime->owner) return;
        const auto change = lifetime->owner->currentChange();
        const auto material_path = lifetime->owner->path();
        if (!change.current()) return;
        RefreshRequest request{std::move(select), force_refresh, update_list, material_path};
        // 同一内容の再入した更新通知は、すでに処理中とする。 異なる選択
        // は保留中の処理を置き換えるが、表示中のモデルは置き換えない。
        if (lifetime->active_refresh && !lifetime->pending_refresh &&
            *lifetime->active_refresh == request) return;
        lifetime->pending_refresh = std::move(request);
        if (lifetime->active_refresh) {
            ++lifetime->rows_revision;
            return;
        }
        std::exception_ptr failure;
        while (lifetime->owner && lifetime->pending_refresh) {
            lifetime->active_refresh = std::move(lifetime->pending_refresh);
            lifetime->pending_refresh.reset();
            try {
                const auto current = *lifetime->active_refresh;
                const bool started_async = lifetime->owner->beginAsyncRefresh(current);
                // 非同期の開始が false を返す場合でも、定義の提供処理が選
                // 択欄を破棄する可能性がある。 その場合は代替処理に入らない。
                if (!lifetime->owner) { lifetime->active_refresh.reset(); break; }
                if (started_async) {
                    lifetime->active_refresh.reset();
                    if (!lifetime->pending_refresh) break;
                    continue;
                }
                lifetime->owner->invalidateAsyncRefresh();
                if (!lifetime->owner) { lifetime->active_refresh.reset(); break; }
                lifetime->owner->refreshPathsImpl(current.select, current.force_refresh,
                                                   current.update_list);
            } catch (...) {
                if (!failure) failure = std::current_exception();
            }
            lifetime->active_refresh.reset();
        }
        // 以前の提供処理が例外を出しても、新しく予約された選択は処
        // 理する。 整合した状態に戻してから、元の失敗を通知する。
        if (failure) std::rethrow_exception(failure);
    };
    // 予約したコマンドは、生の this ではなくUIスレッド上の寿命確認を使う。
    // 通知処理が例外を出した場合や更新に失敗した場合も、予約中のフラグを残さない。
    if (dispatcher && !dispatcher->is_main_thread()) dispatcher->invoke(std::move(execute));
    else execute();
}

void MaterialSelectBase::refreshPathsImpl(String select, bool force_refresh, bool update_list) {
    refreshPathsImplWithScan(std::move(select), force_refresh, update_list, nullptr, nullptr);
}

void MaterialSelectBase::refreshPathsImplWithScan(
    String select, bool force_refresh, bool update_list,
    const ScenarioDirectoryScanResult* scan, const RefreshPathSnapshot* snapshot) {
    const auto lifetime = publication_lifetime_;
    const auto revision = ++lifetime->rows_revision;
    const auto current = [lifetime, revision] {
        return lifetime->owner && lifetime->rows_revision == revision;
    };
    const auto combo = dirs_;
    const auto table = file_list_;
    const String old_label = selected_dir_ >= 0 &&
        static_cast<std::size_t>(selected_dir_) < directory_entries_.size()
        ? directory_entries_[selected_dir_].label : String{};
    const auto old_path = path();
    if (!current()) return;
    const bool included = !binPath().empty();
    const auto definition_index = valueToDef;
    const auto binary_index = indexOfBinPath;
    const auto image = type_ == MtType::CARD && image_index_ >= 0 &&
        static_cast<std::size_t>(image_index_) < paths_.size() && paths_[image_index_]
        ? std::make_shared<CardImage>(nullptr, *paths_[image_index_]) : CardImagePtr{};
    const auto skin_value = summSkin();
    auto location = fileLocation();
    const auto version = summary_ ? summary_->dataVersion() : versioninfo::LATEST_VERSION;
    const bool legacy = summary_ && summary_->legacy();
    const bool no_card_size = type_ == MtType::CARD ? no_card_size_ : excludeCardSizeImage();
    const auto default_label = snapshot ? snapshot->default_label
        : prop_->requiredMsgs().defaultSelection(prop_->requiredMsgs().pathDef.value);
    auto version_it = std::find(versioninfo::VERSIONS.begin(), versioninfo::VERSIONS.end(), version);
    if (version_it == versioninfo::VERSIONS.end()) {
        version_it = std::find(versioninfo::VERSIONS.begin(), versioninfo::VERSIONS.end(),
                               versioninfo::LATEST_VERSION);
    }
    const auto version_index = version_it == versioninfo::VERSIONS.end()
        ? std::size_t{0}
        : static_cast<std::size_t>(version_it - versioninfo::VERSIONS.begin());
    const auto engine_label = snapshot ? snapshot->engine_label
        : prop_->requiredMsgs().defaultSelection(
            ::cwxeditor::cwx::utils::tryFormat(prop_->requiredMsgs().pathWsnBasic.value,
                                              versioninfo::VERSION_NAMES[version_index]));
    const auto defs = snapshot ? snapshot->definitions : getDefs();
    if (!current()) return;

    ScenarioDirectoryScanResult synchronous_scan;
    if (!scan) {
        if (skin_value) {
            synchronous_scan = scanMaterialTargets(
                location, skin_value, force_refresh, no_card_size);
            if (location.has_summary) {
                ScenarioDirectoryScanInput input;
                input.skin_owner = skin_value;
                input.skin = skin_value.get();
                input.type = type_;
                input.scenario_root = location.scenario_root;
                input.ignored_paths = prop_ ? prop_->var().ignorePaths.value
                                            : std::vector<String>{};
                input.force_refresh = force_refresh;
                input.no_card_size = no_card_size;
                input.included = can_include_ && !is_menu_card_;
                input.scan_scenario = true;
                std::atomic_bool not_obsolete{};
                synchronous_scan.scenario_directories =
                    scanScenarioDirectories(input, not_obsolete).scenario_directories;
            }
        }
        scan = &synchronous_scan;
    }
    if (!current()) return;

    std::vector<DirectoryEntry> entries;
    for (const auto& value : defs)
        entries.push_back({value, {}, DirectoryEntry::Kind::Definition});
    int table_dir = -1;
    int engine_dir = -1;
    const bool skin_has_target = scan->skin_has_target;
    if (skin_has_target) {
        table_dir = static_cast<int>(entries.size());
        entries.push_back({default_label, {}, DirectoryEntry::Kind::Skin});
    }
    if (skin_value && !legacy) {
        const bool found = scan->engine_has_target;
        if (!current()) return;
        if (found) {
            engine_dir = static_cast<int>(entries.size());
            entries.push_back({engine_label, {}, DirectoryEntry::Kind::Engine});
        }
    }
    if (location.has_summary && skin_value) {
        const auto& directories = scan->scenario_directories;
        if (!current()) return;
        for (const auto& value : directories)
            entries.push_back({value, {}, DirectoryEntry::Kind::Scenario});
    }
    std::vector<lib::ChoiceItem> choices;
    choices.reserve(entries.size());
    for (const auto& entry : entries) choices.push_back({entry.label, entry.label});
    const auto directory_key = [](String value) {
        value = toViewPath(std::move(value));
        while (!value.empty() && value.front() == '/') value.erase(value.begin());
        while (!value.empty() && value.back() == '/') value.pop_back();
        return value;
    };
    const auto find_directory = [&](const String& label) {
        for (std::size_t i = 0; i < entries.size(); ++i) {
            if (entries[i].label == label ||
                (entries[i].kind == DirectoryEntry::Kind::Scenario &&
                 directory_key(entries[i].label) == directory_key(label))) return static_cast<int>(i);
        }
        return -1;
    };
    int selection = select.empty() ? -1 : find_directory(toViewPath(select));
    // D版と同様、指定されたパスを先に解決し、以前のフォルダーは代替としてだけ使う。
    const bool preserve_old_first = lib::editor_window_host_options().all_mode;
    if (preserve_old_first && selection < 0 && !old_label.empty())
        selection = find_directory(old_label);
    if (selection < 0 && !old_path.empty()) {
        if (image && definition_index) {
            selection = definition_index(*image, included);
            if (!current()) return;
        } else if ((type_ == MtType::CARD || type_ == MtType::BG_IMG) && included && binary_index) {
            selection = binary_index(true);
            if (!current()) return;
        }
        if (selection >= static_cast<int>(defs.size())) selection = -1;
        if (selection < 0 && skin_value) {
            String resource = image && image->type == types::CardImageType::File && !included
                ? image->path() : old_path;
            bool skin_material = false;
            bool engine_material = false;
            const auto resolved = skin_value->findPathF(resource, defExts(), location.default_dirs,
                location.scenario_root, version, location.engine_dirs, skin_material, engine_material);
            if (!current()) return;
            if (skin_material) selection = table_dir;
            else if (engine_material) selection = engine_dir;
            else if (!resolved.empty() && location.has_summary) {
                const auto parent = pathToUtf8(pathFromUtf8(resolved).parent_path());
                const auto relative = abs2rel(StringView(parent), StringView(location.scenario_root));
                selection = find_directory(relative.empty() || relative == "." ? String("/") : toViewPath(relative));
            }
        }
        if (preserve_old_first && selection < 0 && !defs.empty()) selection = 0;
    }
    if (!preserve_old_first && selection < 0 && !old_label.empty())
        selection = find_directory(old_label);
    if (selection < 0 && !entries.empty()) selection = 0;
    if (selection >= static_cast<int>(entries.size())) selection = -1;

    location.current_directory.clear();
    location.all_list = false;
    if (selection >= 0) {
        const auto& entry = entries[static_cast<std::size_t>(selection)];
        location.kind = entry.kind;
        location.all_list = entry.kind == DirectoryEntry::Kind::Definition;
        if (entry.kind == DirectoryEntry::Kind::Scenario && entry.label != "/")
            location.current_directory = entry.label;
    }
    const bool replace_rows = table && (update_list || selection >= 0);
    TargetFiles targets;
    std::optional<PreparedRows> rows;
    if (replace_rows) {
        if (selection >= 0 && location.all_list) {
            std::ptrdiff_t skin_pos, engine_from, engine_to;
            const auto directories = directorySources(entries, defs.size(), location, skin_pos, engine_from, engine_to);
            targets = collectTargets(directories, true, force_refresh, skin_pos, engine_from, engine_to);
        } else if (selection >= 0 && location.kind == DirectoryEntry::Kind::Skin) {
            targets = collectTargets(location.default_dirs, false, force_refresh, -1, 0,
                                     static_cast<std::ptrdiff_t>(location.default_dirs.size()));
        } else if (selection >= 0 && location.kind == DirectoryEntry::Kind::Engine) {
            targets = collectTargets(location.engine_dirs, false, force_refresh, -1, 0,
                                     static_cast<std::ptrdiff_t>(location.engine_dirs.size()));
        } else if (selection >= 0) {
            const auto directory = pathFromUtf8(location.scenario_root) /
                pathFromUtf8(fromViewPath(location.current_directory));
            targets = collectTargets(pathToUtf8(directory), force_refresh);
        }
        if (!current()) return;
        rows = prepareRows(std::move(targets.names), location, selection >= 0 ? old_path : String{}, revision);
        if (!rows) return;
    }
    if (!current()) return;
    // 行の反映とディレクトリの交換の間では、アプリ・提供側のコールバック
    // を呼ばない。 共通の表APIは、失敗し得る準備を反映前に済ませる。
    std::optional<lib::PreparedItemModelUpdate> combo_update;
    if (combo) {
        combo_update.emplace(lib::prepare_combo_items(combo, std::move(choices),
            selection >= 0
                ? std::optional<std::size_t>(static_cast<std::size_t>(selection))
                : std::nullopt, {}, current));
        if (!combo_update->current()) return;
    }
    if (rows && !commitRows(*rows, location.all_list, targets.searchable,
        targets.engine_begin, targets.engine_end,
        combo_update ? &*combo_update : nullptr)) return;
    if (!rows && combo_update && !combo_update->commit()) return;
    directory_entries_.swap(entries);
    table_dir_ = table_dir;
    engine_dir_ = engine_dir;
    selected_dir_ = selection;

    if (selection >= 0 && location.kind == DirectoryEntry::Kind::Skin) table_index_ = 0;
    if (selection >= 0 && location.kind == DirectoryEntry::Kind::Engine) engine_index_ = 0;
    loading_ = false;
    updateEnabledControls(false);
    if (rows) notifyLoaded(rows->loaded, revision);
}

void MaterialSelectBase::refPaths(StringView parent) {
    (void)parent;
    const auto change = currentChange();
    const auto callback = refresh_callback_;
    refreshPaths();
    if (change.current() && callback) callback();
}

void MaterialSelectBase::refPath(StringView old_path, StringView new_path, bool is_directory) {
    // 通知の引数が、この選択欄の所有するパスと同じ領域を参照する場合がある。
    const String from(old_path), to(new_path);
    const String from_view=toViewPath(from);
    const String to_view=toViewPath(to);
    const auto change = beginChange();
    const auto rows_revision = change.lifetime->rows_revision;
    const auto old = path();
    if (!change.current() || change.lifetime->rows_revision != rows_revision) return;
    const auto callback = refresh_callback_;
    auto next_paths = paths_;
    if (type_ == MtType::CARD) {
        for (std::size_t i = 0; i < next_paths.size(); ++i) {
            if (i == static_cast<std::size_t>(image_index_) || !next_paths[i] ||
                next_paths[i]->type != types::CardImageType::File) continue;
            if (toViewPath(next_paths[i]->path()) == from_view)
                next_paths[i] = std::make_shared<CardImage>(to, next_paths[i]->positionType());
        }
    }
    String directory = currentDir();
    if (is_directory) {
        // D版と同様にディレクトリのラベルを対応付け直すが、ここでは保存された
        // カードパスを変更しない。 その参照はシナリオの改名処理が担当する。
        const auto& prefix = from_view;
        if (!prefix.empty() && directory.starts_with(prefix)) {
            auto tail = directory.substr(prefix.size());
            while (!tail.empty() && tail.front() == '/') tail.erase(tail.begin());
            directory = toViewPath(pathToUtf8(pathFromUtf8(to) / pathFromUtf8(tail)));
        }
    }
    paths_.swap(next_paths);
    if (!is_directory && toViewPath(old) == from_view && from_view != to_view) {
        // CARD の現在のレイヤーも処理対象にする。
        // 共通のパス確定処理が、変更通知も一度だけ行う。
        if (!applyPath(to, old, true, false, image_index_, false, change)) return;
    } else {
        // 改名通知で、キャッシュしたファイル一覧を無効化する。 0番目を
        // 選び直さず、新しい名前も含めて選択したフォルダーを維持する。
        refreshPaths(directory, true);
        if (change.current() && callback) callback();
    }
}

void MaterialSelectBase::delPaths() {
    refreshPaths();
}

void MaterialSelectBase::replPath(StringView from, StringView to) {
    (void)to;
    const String expected(from);
    const auto change = currentChange();
    const auto rows_revision = change.lifetime->rows_revision;
    const auto selected = path();
    if (change.current() && change.lifetime->rows_revision == rows_revision && selected == expected)
        refreshPaths();
}

void MaterialSelectBase::refreshButtons(const bool update_audio_options) {
    const auto change = currentChange();
    if (!change.current()) return;
    if (refresh_button_) refresh_button_->set_enabled(enabled_ && canRefresh());
    if (directory_button_) directory_button_->set_enabled(enabled_ && canDirectoryButton());
    updatePlayButton(isPlayOrStop());
    if (update_audio_options) refDataVersion();
    if (!change.current()) return;
    refreshFileListMenu();
}

void MaterialSelectBase::updateSkinMaterialsExtension() {
    (void)updateSkinMaterialsExtension(beginChange());
}

bool MaterialSelectBase::updateSkinMaterialsExtension(const ChangeGuard& change) {
    if (!change.current()) return false;
    if (!selectedDefDir()) return true;
    const auto revision = change.lifetime->rows_revision;
    const auto selected = path();
    if (!change.current() || change.lifetime->rows_revision != revision) return false;
    if (!selected.empty()) return true; // D版と同じ、空・既定選択の条件判定。
    const auto directories = defDirs();
    const auto extensions = defExts();
    const auto update = [&directories, &extensions](String value) {
        if (value.empty() || isBinImg(value)) return value;
        for (const auto& directory : directories) {
            const auto direct = pathFromUtf8(directory) / pathFromUtf8(value);
            if (std::filesystem::exists(direct)) return value;
            for (const auto& extension : extensions) {
                auto candidate = pathFromUtf8(value);
                candidate.replace_extension(extension);
                if (std::filesystem::exists(pathFromUtf8(directory) / candidate)) {
                    return pathToUtf8(candidate);
                }
            }
        }
        return value;
    };
    struct Replacement { std::ptrdiff_t index; String old_path; String new_path; };
    std::vector<Replacement> replacements;
    if (type_ == MtType::CARD) {
        for (std::size_t i = 0; i < paths_.size(); ++i) {
            if (!paths_[i] || paths_[i]->type != types::CardImageType::File) continue;
            const auto old = paths_[i]->path();
            replacements.push_back({static_cast<std::ptrdiff_t>(i), old, update(old)});
        }
    } else {
        replacements.push_back({-1, path_, update(path_)});
    }
    // 提供側に依存しない準備は、最初のアプリのコールバックより前に
    // すべて済ませる。 一つの変更トークンで全体を管理し、入れ子の
    // 編集があったら古い処理を止め、残りのレイヤーを上書きしない。
    for (const auto& replacement : replacements) {
        if (!applyPath(replacement.new_path, replacement.old_path, true, false,
                       replacement.index, false, change)) return false;
    }
    return change.current();
}

void MaterialSelectBase::refSkin() {
    const auto change = beginChange();
    // D版の強制指定のない編集可能なシナリオ選択欄と同様、現在適用されている Summary
    // のスキンを解決する。 明示的な強制指定と読み取り専用のプレビューは固定したままにする。
    if (!forced_skin_ && !read_only_ && summary_ &&
        !summary_->scenarioPath().empty() && prop_) {
        summary_skin_ = dskin::findSkin2(*prop_, summary_->type(), summary_->skinName());
        if (!summary_skin_) summary_skin_ = dskin::findSkin2(*prop_,
            prop_->var().defaultSkin.value, prop_->var().defaultSkinName.value);
    }
    if (updateSkinMaterialsExtension(change)) refresh();
}

SkinPtr MaterialSelectBase::summSkin() const noexcept {
    return summary_skin_ ? summary_skin_ : forced_skin_;
}

std::vector<String> MaterialSelectBase::getDefs() const {
    // アプリのコールバックが、この選択欄を破棄したり自身を置き換えたりする場合がある。
    const auto definitions = definitions_;
    const bool included = !binPath().empty();
    const bool add_continue = type_ == MtType::BGM && (can_continue_ || continue_bgm_) && prop_;
    const String continue_label = add_continue ? prop_->requiredMsgs().continueBGM.value : String{};
    auto result = definitions ? definitions(included) : std::vector<String>{};
    if (add_continue) result.push_back(continue_label);
    return result;
}

std::ptrdiff_t MaterialSelectBase::continueIndex() const {
    if (type_ != MtType::BGM || (!can_continue_ && !continue_bgm_)) return -1;
    const auto change = currentChange();
    const auto definitions = definitions_;
    const bool included = !binPath().empty();
    const auto count = definitions ? definitions(included).size() : 0U;
    return change.current() ? static_cast<std::ptrdiff_t>(count) : -1;
}

bool MaterialSelectBase::addDroppedFiles(const std::vector<std::filesystem::path>& files) {
    if (!enabled_ || read_only_ || !summary_ || summary_->scenarioPath().empty()) return false;
    const auto change = beginChange();
    const auto scenario_path = summary_->scenarioPath();
    const auto dialogs = services_.dialogs;
    const auto skin = summSkin();
    const auto rows_revision = change.lifetime->rows_revision;
    const bool card = type_ == MtType::CARD;
    const auto card_size = prop_->looks().cardSize();
    const auto selected_directory = currentDir();
    String destination = selected_directory;
    // D版の currentDir と同様、定義を表す null と、 明示的なル
    // ート・Skin・Engine の選択を表す非nullの空文字列を区別する。
    const bool definition_selected = selected_dir_ < 0 ||
        static_cast<std::size_t>(selected_dir_) >= directory_entries_.size() ||
        directory_entries_[static_cast<std::size_t>(selected_dir_)].kind == DirectoryEntry::Kind::Definition;
    if (definition_selected && skin) destination = skin->materialPath();
    std::vector<std::filesystem::path> accepted;
    bool non_card_sized = false;
    for (const auto& file : files) {
        const auto file_path = pathToUtf8(file);
        if (!std::filesystem::is_regular_file(file)) continue;
        // D版と同様、現在の表示サイズの制限に関係なくカード素材を受け付ける。 了承
        // された場合は、ファイルを追加する前に大きい画像を表示する設定を有効にする。
        const bool target = card ? skin && skin->isCardImage(file_path, true,
                                      can_include_ && !is_menu_card_) : isTarg(file_path);
        if (!target) continue;
        if (::cwxeditor::cwx::utils::hasPath(StringView(scenario_path),
                                             StringView(file_path))) continue;
        accepted.push_back(file);
        if (card) {
            std::uint32_t width = 0, height = 0;
            imageSize(file, width, height);
            non_card_sized |= width != card_size.width || height != card_size.height;
        }
    }
    if (accepted.empty()) return false;
    if (dialogs) {
        const auto& messages = prop_->requiredMsgs();
        lib::MessageDialogRequest request;
        request.title = messages.dlgTitDropFiles.value;
        request.message = accepted.size() == 1
                              ? ::cwxeditor::cwx::utils::tryFormat(messages.dlgMsgDropFile.value,
                                                                    pathToUtf8(accepted.front()))
                              : ::cwxeditor::cwx::utils::tryFormat(messages.dlgMsgDropFiles.value,
                                                                    accepted.size());
        request.severity = lib::MessageSeverity::Question;
        request.buttons = lib::MessageButtons::YesNo;
        const auto answer = dialogs->message(request);
        if (!change.current() || change.lifetime->rows_revision != rows_revision ||
            answer != lib::MessageResult::Yes) return false;
    }
    if (!change.current() || !summary_ || summary_->scenarioPath() != scenario_path) return false;
    if (non_card_sized && !no_card_size_) {
        no_card_size_ = true;
        if (!notifyChange(updateNoCardSize, change)) return false;
        if (!summary_ || summary_->scenarioPath() != scenario_path ||
            currentDir() != selected_directory) return false;
    }
    // 取り込み範囲は受け付けたシナリオに限り、ドラッグデータ中の任意のパスを使
    // わない。 マウント先を選ぶ前に不正な素材ディレクトリを拒否する。 Storage
    // の書き込みが失敗しても、直接のファイル操作へ切り替えない。
    const auto relative_directory = pathFromUtf8(destination).lexically_normal();
    if (relative_directory.has_root_path()) return false;
    for (const auto& component : relative_directory)
        if (component == "..") return false;
    const auto scenario_directory = pathFromUtf8(scenario_path).lexically_normal();
    const auto import_services = services_;
    const auto* const props_owner = prop_;
    auto* const summary_owner = summary_;
    const auto summary_pin = summary_owner->weak_from_this().lock();
    const auto props_lifetime = props_owner->lifetimeToken();
    auto* storage = summary_->storage();
    const bool summary_storage = storage != nullptr;
    bool props_storage = false;
    if (!storage) {
        auto* candidate = prop_->storage();
        if (candidate && (!scenario_directory.is_absolute() ||
            ::cwxeditor::cwx::utils::hasPath(candidate->root(), scenario_directory))) {
            storage = candidate;
            props_storage = true;
        }
    }
    std::unique_ptr<lib::EditorStorage> scenario_storage;
    lib::StorageImportRequest request;
    if (storage) {
        request.destination_directory = scenario_directory / relative_directory;
    } else {
        // 実ファイルシステム上の外部シナリオも扱う。親ディレクトリではなく、開いて
        // いるシナリオのディレクトリを基点にしたNativeStorageを使う。
        scenario_storage = lib::storage_api::make_native_storage(scenario_path, import_services, false);
        storage = scenario_storage.get();
        request.destination_directory = relative_directory;
    }
    if (!storage || !change.current()) return false;
    request.resolve_destination = [](const auto& original, const auto& available) {
        return ::cwxeditor::cwx::utils::createNewFileName(original, false, available);
    };
    if (lib::editor_window_host_options().all_mode) {
        auto selection = lib::FilePickerResult::success();
        selection.selections.reserve(accepted.size());
        for (const auto& file : accepted) selection.selections.emplace_back(file);
        const auto imported = lib::import_to_storage(selection, *storage, request);
        if (!imported.succeeded()) throw std::runtime_error(imported.message);
        // 入出力の完了中に、提供処理がビューを閉じたり再構築したりする場合がある。 所有す
        // る取り込み用スナップショットの処理は完了させるが、失効した選択欄は更新しない。
        if (!change.current()) return true;
        refreshPaths(destination);
        if (!change.current()) return true; // ビューが閉じられていても、ファイルのコピーは完了している。
        refPaths(destination);
        return true;
    }
    // D版と同様、ファイルごとに独立して確定する。 一回の呼び出し内では既存の同期
    // Storage の所有関係を維持するが、 無効化・置換された所有者から次
    // の呼び出しを行わない。スタック上の Summary にも引き続き対応する。
    const auto current_import = [&] {
        if (!change.current() || props_lifetime.expired()) return false;
        if (prop_ != props_owner || summary_ != summary_owner) return false;
        if (summary_storage && summary_owner->storage() != storage) return false;
        if (props_storage && props_owner->storage() != storage) return false;
        return true;
    };
    std::size_t imported_count = 0U;
    for (const auto& file : accepted) {
        if (!current_import()) return imported_count != 0U;
        auto selection = lib::FilePickerResult::success();
        selection.selections.emplace_back(file);
        const auto imported = lib::import_to_storage(selection, *storage, request);
        if (!imported.succeeded()) {
            if (imported_count && current_import()) {
                refreshPaths(destination);
                if (current_import()) refPaths(destination);
            }
            throw std::runtime_error(imported.message);
        }
        ++imported_count;
        if (!current_import()) return true;
    }
    if (!imported_count) return false;
    if (!current_import()) return true;
    refreshPaths(destination);
    if (current_import()) refPaths(destination);
    return true;
}

void MaterialSelectBase::addDropTarget() {
    const auto control = fileControl();
    if (!services_.drag_drop || !file_list_ || !control) return;
    lib::DropTargetBinding binding;
    binding.target = control;
    binding.accepted_formats = {"text/uri-list", "application/octet-stream"};
    const std::weak_ptr<PublicationLifetime> weak = publication_lifetime_;
    const std::weak_ptr<lib::TableView> model = file_list_;
    const std::weak_ptr<lib::Widget> target = control;
    const auto transport = services_.drag_drop;
    const auto negotiate_copy = [weak, model, target](lib::DragEvent& event) {
        event.selected = lib::DropOperation::None;
        if ((static_cast<std::uint32_t>(event.allowed) &
             static_cast<std::uint32_t>(lib::DropOperation::Copy)) == 0) return;
        const auto state = weak.lock();
        const auto table = model.lock();
        const auto control = target.lock();
        if (!state || !state->owner || !table || !control ||
            state->owner->file_list_ != table ||
            state->owner->fileControl() != control || state->owner->read_only_ ||
            !state->owner->summary_ ||
            state->owner->summary_->scenarioPath().empty()) return;
        // D版の MaterialSelect.canDrop と同様、ドラッグ中に現在のシナ
        // リオと読み取り専用の方針を確認する。 ドロップのコールバックでもモデルを再検証する。
        event.selected = lib::DropOperation::Copy;
    };
    binding.on_enter = negotiate_copy;
    binding.on_over = negotiate_copy;
    binding.on_drop = [weak, model, target](lib::DragEvent& event) {
        if ((static_cast<std::uint32_t>(event.allowed) &
             static_cast<std::uint32_t>(lib::DropOperation::Copy)) == 0) return;
        const auto state = weak.lock();
        const auto table = model.lock();
        const auto control = target.lock();
        if (!state || !state->owner || !table || !control ||
            state->owner->file_list_ != table || state->owner->fileControl() != control) return;
        const auto files = event.data.files;
        if (state->owner->addDroppedFiles(files)) {
            event.selected = lib::DropOperation::Copy;
            event.handled = true;
        }
    };
    transport->bind_target(std::move(binding));
}

void MaterialSelectBase::notifyModified() {
    const auto change = currentChange();
    const auto notification = ++change.lifetime->notification_revision;
    const auto refresh_callback = refresh_callback_;
    const auto callbacks = modEvent;
    if (refresh_callback) refresh_callback();
    for (const auto& callback : callbacks) {
        if (!change.current() || change.lifetime->notification_revision != notification) return;
        if (callback) callback();
    }
}

MaterialSelectBase::ChangeGuard MaterialSelectBase::beginChange() {
    ++publication_lifetime_->rows_revision;
    return {publication_lifetime_, ++publication_lifetime_->change_revision};
}

MaterialSelectBase::ChangeGuard MaterialSelectBase::currentChange() const {
    return {publication_lifetime_, publication_lifetime_->change_revision};
}

bool MaterialSelectBase::notifyChange(const std::vector<Action>& source, const ChangeGuard& change) {
    const auto callbacks = source;
    for (const auto& callback : callbacks) {
        if (!change.current()) return false;
        if (callback) callback();
    }
    return change.current();
}

void MaterialSelectBase::updateEnabledControls(const bool update_audio_options) {
    if (dirs_) dirs_->set_enabled(enabled_ && !read_only_ && !loading_);
    // D版と同様、シナリオの Summary がなくても素材一覧を
    // 使えるようにし、 スキンの定義を閲覧・選択できるようにする。
    if (file_list_) file_list_->set_enabled(enabled_ && !read_only_ && !loading_);
    if (file_combo_) file_combo_->set_enabled(enabled_ && !read_only_ && !loading_);
    if (refresh_button_) refresh_button_->set_enabled(enabled_ && canRefresh());
    if (directory_button_) directory_button_->set_enabled(enabled_ && canDirectoryButton());
    // D版と同様、行の反映では音声設定を再計算せずにパスの操作欄を更新する。
    if (update_audio_options) refDataVersion();
}

void MaterialSelectBase::setSelection(std::size_t index) {
    if (!file_list_ || index >= file_list_->rows.size()) return;
    (void)lib::select_table_rows(*file_list_, {index});
}

void MaterialSelectBase::selectRow(std::size_t index) {
    setSelection(index);
    selectFile();
}

void MaterialSelectBase::setRows(const std::vector<String>& targets) {
    refreshListImpl2(targets);
}

bool MaterialSelectBase::selectedBinaryDirectory() const {
    if (!indexOfBinPath || !dirs_ || !dirs_->selected_index) return false;
    const auto change = currentChange();
    const auto revision = change.lifetime->rows_revision;
    const auto provider = indexOfBinPath;
    const auto selection = *dirs_->selected_index;
    const bool included = !binPath().empty();
    const auto result = provider(included);
    return change.current() && change.lifetime->rows_revision == revision &&
           result >= 0 && selection == static_cast<std::size_t>(result);
}

std::filesystem::path MaterialSelectBase::resolveCurrentFile(StringView file) const {
    return resolveFile(file, fileLocation());
}

MaterialSelectBase::FileLocation MaterialSelectBase::fileLocation() const {
    FileLocation location;
    location.has_summary = summary_ != nullptr;
    if (summary_) location.scenario_root = summary_->scenarioPath();
    location.default_dirs = defDirs();
    location.engine_dirs = engineDefDirs();
    location.all_list = all_list_;
    if (selected_dir_ >= 0 && static_cast<std::size_t>(selected_dir_) < directory_entries_.size()) {
        location.kind = directory_entries_[selected_dir_].kind;
        location.current_directory = currentDir();
    }
    return location;
}

MaterialSelectBase::ScenarioDirectoryScanResult MaterialSelectBase::scanMaterialTargets(
    const FileLocation& location, const SkinPtr& skin,
    bool force_refresh, bool no_card_size) const {
    ScenarioDirectoryScanResult result;
    if (!skin) return result;
    for (const auto& directory : location.default_dirs) {
        if (hasTarg(*skin, directory, force_refresh, no_card_size)) {
            result.skin_has_target = true;
            break;
        }
    }
    if (!summary_ || !summary_->legacy())
        result.engine_has_target = hasWsnTarg(force_refresh, no_card_size);
    return result;
}

std::filesystem::path MaterialSelectBase::resolveFile(StringView file, const FileLocation& location) {
    if (file.empty()) return {};
    String value(file);
    if (isBinImg(value) || isBinSnd(value)) return {};
    if (value.front() == '/') {
        value.erase(value.begin());
        if (location.has_summary) return pathFromUtf8(location.scenario_root) /
                             pathFromUtf8(fromViewPath(value));
    }
    if (location.all_list) {
        for (const auto& directory : location.default_dirs) {
            const auto candidate = pathFromUtf8(directory) / pathFromUtf8(value);
            if (std::filesystem::exists(candidate)) return candidate;
        }
        for (const auto& directory : location.engine_dirs) {
            const auto candidate = pathFromUtf8(directory) / pathFromUtf8(value);
            if (std::filesystem::exists(candidate)) return candidate;
        }
    }
    if (location.kind == DirectoryEntry::Kind::Skin) {
        for (const auto& directory : location.default_dirs) {
            const auto candidate = pathFromUtf8(directory) / pathFromUtf8(value);
            if (std::filesystem::exists(candidate)) return candidate;
        }
        // D版の toFilePath と同様、設定済みの全スキンディレクトリにフ
        // ァイルがなければ空を返す。 再生可能に見える代替候補を勝手に作らない。
        return {};
    }
    if (location.kind == DirectoryEntry::Kind::Engine) {
        for (const auto& directory : location.engine_dirs) {
            const auto candidate = pathFromUtf8(directory) / pathFromUtf8(value);
            if (std::filesystem::exists(candidate)) return candidate;
        }
        // D版の toFilePath と同様、設定済みの全エンジンディレクトリに
        // ファイルがなければ空を返す。 再生可能に見える代替候補を勝手に作らない。
        return {};
    }
    if (location.has_summary) return pathFromUtf8(location.scenario_root) /
                          (location.current_directory.empty() ? std::filesystem::path{}
                                           : pathFromUtf8(fromViewPath(location.current_directory))) /
                          pathFromUtf8(value);
    return pathFromUtf8(value);
}

bool MaterialSelectBase::pathIsCurrent(StringView value) const {
    return normalizePath(path()) == normalizePath(value);
}

String MaterialSelectBase::warningTargetVersion() const {
    return prop_ ? prop_->var().targetVersion.value : String("1.50");
}

std::vector<String> MaterialSelectBase::warningFor(StringView file) const {
    if (!prop_ || (type_ != MtType::CARD && type_ != MtType::BG_IMG)) return {};
    auto result = Skin::warningImage(prop_->parent(), file, summary_ && summary_->legacy(),
                                    can_include_ && !is_menu_card_, warningTargetVersion());
    const auto encoding = ::cwxeditor::cwx::warning::sjisWarnings(prop_->parent(), summary_, file, "");
    result.insert(result.end(), encoding.begin(), encoding.end());
    const auto extension = ::cwxeditor::cwx::warning::fileExtensionWarnings(prop_->parent(), toFilePath(file));
    result.insert(result.end(), extension.begin(), extension.end());
    return result;
}

bool MaterialSelectBase::imageIsCardSized(StringView value) const {
    if (!prop_) return false;
    String resolved(value);
    if (summSkin() && !isBinImg(value)) {
        const auto version = summary_ ? summary_->dataVersion()
                                      : String(versioninfo::LATEST_VERSION);
        const auto candidate = summSkin()->findImagePath(value,
                                                          summary_ ? summary_->scenarioPath() : String{},
                                                          version);
        if (!candidate.empty()) resolved = candidate;
    }
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    // D版の Skin.isCardImage と同様、未解決・読み取り不可のファイルはカードサイズではないと判定する。
    try {
        if (!imageSize(pathFromUtf8(resolved), width, height)) return false;
    } catch (const std::exception&) {
        return false;
    }
    const auto size = prop_->looks().cardSize();
    return width == size.width && height == size.height;
}

bool MaterialSelectBase::validSelection() const {
    return file_list_ && !file_list_->selection.empty() &&
           file_list_->selection.front() < file_list_->rows.size();
}

String MaterialSelectBase::audioPath() const {
    if (type_ != MtType::BGM && type_ != MtType::SE) return {};
    return filePath();
}

bool MaterialSelectBase::hasExtension(const std::filesystem::path& path,
                                      const std::vector<String>& extensions) {
    if (extensions.empty()) return true;
    const auto extension = lower(pathToUtf8(path.extension()));
    for (auto candidate : extensions) {
        candidate = lower(candidate);
        if (!candidate.empty() && candidate.front() != '.') candidate.insert(candidate.begin(), '.');
        if (extension == candidate) return true;
    }
    return false;
}

String MaterialSelectBase::baseName(StringView value) {
    return pathToUtf8(pathFromUtf8(value).filename());
}

String MaterialSelectBase::dirName(StringView value) {
    return pathToUtf8(pathFromUtf8(value).parent_path());
}

String MaterialSelectBase::normalizePath(StringView value) {
    if (value.empty() || isBinImg(value) || isBinSnd(value)) return String(value);
    try {
        return ::cwxeditor::cwx::utils::normpath(value);
    } catch (...) {
        return String(value);
    }
}

void MaterialSelectBase::appendUnique(std::vector<String>& values, String value) {
    if (value.empty()) return;
    if (std::find(values.begin(), values.end(), value) == values.end()) values.push_back(std::move(value));
}

bool MaterialSelectBase::IncrementalSearch::match(StringView value) const {
    if (matcher_) return matcher_(value);
    return !open_ || pattern_.empty() || containsInsensitive(value, pattern_);
}

}  // namespace cwxeditor::cwx::editor::gui::dwt::materialselect
