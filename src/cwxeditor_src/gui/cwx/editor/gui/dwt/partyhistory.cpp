#include "cwxeditor_src/gui/cwx/editor/gui/dwt/partyhistory.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/xmlbytestransfer.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dmenu.hpp"
// UIの実装は一つに統一し、
// 別経路の実装を設けない。

#include "cwxeditor_src/core/cwx/system.hpp"
#include "cwxeditor_src/core/cwx/structs.hpp"
#include "cwxeditor_src/core/cwx/variables.hpp"
#include "cwxeditor_src/core/cwx/utils.hpp"
#include "cwxeditor_src/core/cwx/win32res.hpp"
#include "cwxeditor_src/lib/ui_actions.hpp"
#include "cwxeditor_src/lib/ui_window_lifecycle.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace cwxeditor::cwx::editor::gui::dwt::partyhistory {

namespace {

using String = lib::String;
using StringView = lib::StringView;
using Path = std::filesystem::path;

[[nodiscard]] Path fs_path(const StringView value) {
    return structs::pathFromUtf8(value);
}

[[nodiscard]] String utf8_path(const Path& value) {
    return structs::pathToUtf8(value);
}

[[nodiscard]] std::optional<std::uint64_t> classic_file_version(
    const Path& execute, const Path& engine) noexcept {
    std::error_code error;
    const auto version_source = std::filesystem::is_regular_file(engine, error) && !error
                                    ? engine : execute;
    error.clear();
    if (!std::filesystem::is_regular_file(version_source, error) || error)
        return std::nullopt;
    try {
        const win32res::Win32Res resources(version_source);
        return resources.fileVersion();
    } catch (...) {
        // D版と同様、RT_VERSION/1 の確認は、直接の起動項目とパーティメニューの両方の追加を制限する。
        return std::nullopt;
    }
}

[[nodiscard]] bool supports_classic_party_execution(
    const std::uint64_t version) noexcept {
    constexpr std::uint64_t version_1_50 =
        (std::uint64_t{1} << 24U) | (std::uint64_t{5} << 16U);
    return version >= version_1_50;
}

[[nodiscard]] String lower_ascii(StringView value) {
    String result(value);
    for (char& character : result) {
        if (character >= 'A' && character <= 'Z') {
            character = static_cast<char>(character + ('a' - 'A'));
        }
    }
    return result;
}

[[nodiscard]] String normalized_absolute(StringView value) {
    if (value.empty()) return {};
    std::error_code error;
    const Path path = fs_path(value);
    const auto absolute = std::filesystem::absolute(path, error);
    if (error) return utf8_path(path.lexically_normal());
    return utf8_path(absolute.lexically_normal());
}

[[nodiscard]] String basename(StringView value) {
    if (value.empty()) return {};
    return utf8_path(fs_path(value).filename());
}

[[nodiscard]] String dirname(StringView value) {
    if (value.empty()) return {};
    return utf8_path(fs_path(value).parent_path());
}

[[nodiscard]] String extension(StringView value) {
    if (value.empty()) return {};
    return lower_ascii(utf8_path(fs_path(value).extension()));
}

[[nodiscard]] bool usesClassicEngineIcon(StringView engine_path) {
    return extension(engine_path) != ".py";
}

using EngineIconTarget = std::pair<std::size_t, String>;

// D版はメニュー表示時の各アイコン用にワーカーを起動し、SWTへの反映を表示
// スレッドへ渡す。 C++版も非ブロッキングにし、SDLへのアップロードは通
// 知処理へ渡す。 共通レンダラーの画像マップはUIスレッドが管理するため。
struct EngineIconRefreshGeneration {
    std::uint64_t value{};
};

void installEngineIconRefresh(
    const std::shared_ptr<lib::Menu>& menu,
    std::vector<EngineIconTarget> targets,
    const bool with_party,
    std::function<void(StringView, std::function<void(lib::ResourceId)>,
                       std::function<bool()>)> request_icon,
    std::function<void(bool, std::size_t, lib::ResourceId)> icon_loaded) {
    if (!menu || targets.empty() || !request_icon) return;
    const auto previous_open = menu->on_open;
    const std::weak_ptr<lib::Menu> weak_menu = menu;
    const auto generation = std::make_shared<EngineIconRefreshGeneration>();
    menu->on_open = [weak_menu, previous_open, targets = std::move(targets),
                     request_icon = std::move(request_icon),
                     icon_loaded = std::move(icon_loaded), generation, with_party] {
        if (previous_open) previous_open();
        const auto current = weak_menu.lock();
        if (!current) return;
        auto open_generation = ++generation->value;
        if (open_generation == 0U) open_generation = ++generation->value;
        for (const auto& [index, executable] : targets) {
            if (index >= current->items.size()) continue;
            const auto still_needed = [weak_menu, generation, open_generation] {
                return !weak_menu.expired() && generation->value == open_generation;
            };
            request_icon(executable,
                [weak_menu, generation, open_generation, index, with_party,
                 icon_loaded](const lib::ResourceId icon) {
                    if (!icon || generation->value != open_generation) return;
                    const auto target = weak_menu.lock();
                    if (!target || index >= target->items.size() ||
                        target->items[index].icon == icon) return;
                    target->items[index].icon = icon;
                    ++target->item_update_revision;
                    if (icon_loaded) icon_loaded(with_party, index, icon);
                }, still_needed);
        }
    };
}
[[nodiscard]] bool exists(StringView value) {
    if (value.empty()) return false;
    std::error_code error;
    return std::filesystem::exists(fs_path(value), error);
}

[[nodiscard]] bool is_directory(StringView value) {
    if (value.empty()) return false;
    std::error_code error;
    return std::filesystem::is_directory(fs_path(value), error);
}

[[nodiscard]] String format_warning(String format, std::size_t count,
                                    std::size_t maximum) {
    auto replace = [&format](StringView needle, const String& value) {
        std::size_t offset = 0;
        while ((offset = format.find(needle, offset)) != String::npos) {
            format.replace(offset, needle.size(), value);
            offset += value.size();
        }
    };
    replace("%1$s", std::to_string(count));
    replace("%2$s", std::to_string(maximum));
    return format;
}

[[nodiscard]] std::vector<std::byte> bytes_from_string(StringView value) {
    std::vector<std::byte> result;
    result.reserve(value.size());
    for (const unsigned char character : value) {
        result.push_back(static_cast<std::byte>(character));
    }
    return result;
}

[[nodiscard]] String string_from_bytes(const std::vector<std::byte>& value) {
    String result;
    result.reserve(value.size());
    for (const auto byte : value) {
        result.push_back(static_cast<char>(std::to_integer<unsigned char>(byte)));
    }
    return result;
}

[[nodiscard]] bool contains_xml(const lib::ClipboardData& data) {
    String serialized = data.text;
    if (const auto found = data.formats.find(String(EXECUTION_PARTIES_MIME));
        found != data.formats.end()) serialized = string_from_bytes(found->second);
    if (serialized.empty()) return false;
    try {
        auto root = XNode::parse(serialized);
        return root.name() == "executionParties" &&
               std::ranges::any_of(root.element_children(), [](const XNode& child) {
                   return child.name() == ExecutionParty::XML_NAME;
               });
    } catch (...) {
        return false;
    }
}

[[nodiscard]] std::vector<Path> sorted_children(const Path& directory) {
    std::error_code error;
    std::vector<Path> result;
    if (!std::filesystem::is_directory(directory, error)) return result;
    for (const auto& item : std::filesystem::directory_iterator(directory, error)) {
        if (error) return {};
        result.push_back(item.path());
    }
    std::sort(result.begin(), result.end(), [](const Path& left, const Path& right) {
        return lower_ascii(utf8_path(left.filename())) < lower_ascii(utf8_path(right.filename()));
    });
    return result;
}

[[nodiscard]] String read_text(const Path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return {};
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

[[nodiscard]] String property_name(const Path& path) {
    try {
        auto node = XNode::parse(read_text(path));
        const auto property = node.child("Property", false);
        if (!property.valid()) return {};
        const auto name = property.child("Name", false);
        return name.valid() ? name.value() : String{};
    } catch (...) {
        return {};
    }
}

[[nodiscard]] String normal_party_name(const Path& directory) {
    for (const auto& item : sorted_children(directory)) {
        std::error_code error;
        if (!std::filesystem::is_regular_file(item, error) ||
            extension(utf8_path(item)) != ".xml") continue;
        const auto name = property_name(item);
        if (!name.empty()) return name;
    }
    return {};
}

void append_party_item(const ExecEngineMenuOptions& options,
                       lib::MenuItem& parent, StringView launch_path,
                       StringView engine_name, StringView yado_path,
                       StringView yado_name, String party_name,
                       String party_key, bool classic) {
    const auto yado_key = basename(yado_path);
    const auto key = epKey(launch_path, yado_key, party_key, options.filesystem);
    lib::MenuItem item;
    item.text = party_name;
    item.command = "exec-party:" + key.engine + ":" + key.yado + ":" + key.party;
    item.icon = options.images.team;
    const auto permitted = !options.selectable_party || options.selectable_party(key);
    // C++版の意図的な追加として、登録済みのパーティはメ
    // ニューから隠す。 D版では表示したまま無効にする。
    if (!permitted) return;
    item.enabled = !options.can_exec_with_party || options.can_exec_with_party();
    if (options.exec_engine) {
        ExecutionParty party;
        party.engineName = String(engine_name);
        party.enginePath = String(launch_path);
        party.isClassic = classic;
        party.yadoName = String(yado_name);
        party.yadoPath = yado_key;
        party.partyName = party_name;
        party.partyPath = party_key;
        // D版はコマンドラインを連結するため、この値を引
        // 用符で囲む。 共通のプロセスAPIは引数を個別
        // の文字列で渡すため、生のパスを保持し、 OS
        // バックエンドに一度だけ引用符の処理を任せる。
        const auto scenario = options.orig_scenario_path
                                  ? options.orig_scenario_path() : String{};
        item.action = [callback = options.exec_engine, engine = String(launch_path), scenario,
                       party = std::move(party)] { callback(engine, scenario, party); };
    }
    parent.children.push_back(std::move(item));
}

[[nodiscard]] lib::MenuItem normal_yado_menu(const ExecEngineMenuOptions& options,
                                              StringView launch_path, const Path& yado,
                                              StringView retained_yado_name = {}) {
    lib::MenuItem result;
    const auto environment = yado / "Environment.xml";
    if (retained_yado_name.empty()) {
        if (!std::filesystem::is_regular_file(environment)) return result;
        result.text = property_name(environment);
        if (result.text.empty()) result.text = utf8_path(yado.filename());
    } else {
        // D版の子メニュー処理と同様、親のラベルを保持し、パーティのディレクトリ
        // だけを再走査する。 Environment.xml は読み直さない。
        result.text = String(retained_yado_name);
    }
    // D版と同様、CardWirthPy の Environment.xml 形式には通常の宿画像を使
    // う。 デバッグ宿の検査は、Classic の Environment.wyd だけに適用する。
    result.icon = options.images.yado;
    for (const auto& party : sorted_children(yado / "Party")) {
        std::error_code error;
        if (!std::filesystem::is_directory(party, error)) continue;
        const auto name = normal_party_name(party);
        if (!name.empty()) append_party_item(options, result, launch_path,
                                             options.engine_name, utf8_path(yado),
                                             result.text, name,
                                             utf8_path(party.filename()), false);
    }
    // D版の通常エンジンの走査への意図的な追加として、有効な空の宿も開け
    // るようにする。 開き直す前にパーティを追加できるようにするため。
    result.enabled = !options.can_exec_with_party || options.can_exec_with_party();
    if (lib::editor_window_host_options().all_mode) {
        result.open_children = [options, launch = String(launch_path), yado] {
            return normal_yado_menu(options, launch, yado).children;
        };
    } else {
        // D版の宿サブメニューと同様、親メニューの構築時に Environment.xml
        // の表示名を取得する。 展開時に更新するのはパーティであり、その名前ではない。
        const auto retained_name = result.text;
        result.open_children = [options, launch = String(launch_path), yado,
                                retained_name] {
            return normal_yado_menu(options, launch, yado, retained_name).children;
        };
    }
    return result;
}

[[nodiscard]] lib::MenuItem classic_yado_menu(const ExecEngineMenuOptions& options,
                                               StringView launch_path, StringView engine_name,
                                               const Path& yado) {
    lib::MenuItem result;
    if (utf8_path(yado.filename()).starts_with("~") ||
        !std::filesystem::is_regular_file(yado / "Environment.wyd")) return result;
    result.text = utf8_path(yado.filename());
    result.icon = options.is_debug_yado && options.is_debug_yado(utf8_path(yado))
                      ? options.images.debug_yado : options.images.yado;
    for (const auto& wpl : sorted_children(yado)) {
        std::error_code error;
        if (!std::filesystem::is_regular_file(wpl, error) || extension(utf8_path(wpl)) != ".wpl") continue;
        auto name = options.read_classic_party_name ? options.read_classic_party_name(utf8_path(wpl))
                                                     : String{};
        if (name.empty()) name = utf8_path(wpl.stem());
        const auto party_key = name;
        append_party_item(options, result, launch_path, engine_name, utf8_path(yado),
                          result.text, std::move(name), party_key, true);
    }
    result.enabled = (!options.can_exec_with_party || options.can_exec_with_party()) &&
                     (!options.can_exec_classic || options.can_exec_classic());
    result.open_children = [options, launch = String(launch_path),
                            name = String(engine_name), yado] {
        return classic_yado_menu(options, launch, name, yado).children;
    };
    return result;
}

}  // namespace

PartyHistorySettings partyHistorySettings(
    const ::cwxeditor::cwx::variables::FlexEtcProps& etc,
    const ::cwxeditor::cwx::system::System* system) {
    PartyHistorySettings result;
    result.xmlCopy = etc.xmlCopy.value;
    result.history_max = static_cast<std::size_t>(etc.executedPartiesMax);
    result.remove_non_existing_with_priority =
        static_cast<bool>(etc.removePartyHistoryNonExistingWithPriority);
    const auto undo_max = static_cast<std::int64_t>(etc.undoMaxEtc);
    result.undo_max = undo_max > 0 ? static_cast<std::size_t>(undo_max) : 0U;
    result.name_column_width = etc.executionPartyNameColumn;
    result.yado_column_width = etc.executionPartyYadoColumn;
    result.engine_column_width = etc.executionPartyEngineColumn;
    result.engine_path = etc.enginePath;
    result.classic_engines = etc.classicEngines;
    if (system) {
        result.yado_name_for_legacy = [system](StringView value) {
            return system->yadoName(value);
        };
    }
    return result;
}

class ExecutedPartyHistoryDialog::HUndo final
    : public ::cwxeditor::cwx::editor::gui::dwt::undo::Undo {
public:
    explicit HUndo(ExecutedPartyHistoryDialog& owner) : owner_(&owner) { save(); }

    void undo() override { impl(); }
    void redo() override { impl(); }
    void dispose() override {}

private:
    void save() {
        selected_ = owner_->selected_;
        old_hist_ = owner_->hist_;
    }

    void impl() {
        auto selected = selected_;
        auto old_hist = old_hist_;
        save();
        owner_->replace_state(std::move(old_hist), std::move(selected));
    }

    ExecutedPartyHistoryDialog* owner_{};
    std::vector<std::size_t> selected_;
    std::vector<PartyHistoryEntry> old_hist_;
};

ExecutedPartyHistoryDialog::ExecutedPartyHistoryDialog(
    lib::Services services, std::shared_ptr<lib::Window> owner,
    lib::String title, lib::ResourceId icon, std::vector<ExecutionParty> history,
    std::vector<ExecutionParty> bookmark, PartyHistorySettings settings,
    PartyHistoryCallbacks callbacks)
    : services_(std::move(services)),
      owner_(std::move(owner)),
      title_(std::move(title)),
      icon_(icon),
      settings_(std::move(settings)),
      callbacks_(std::move(callbacks)),
      history_(std::move(history)),
      bookmark_(std::move(bookmark)),
      undo_(std::make_unique<
            ::cwxeditor::cwx::editor::gui::dwt::undo::UndoManager>(settings_.undo_max)) {
    hist_.reserve(history_.size() + bookmark_.size());
    for (const auto& value : bookmark_) hist_.emplace_back(value, true);
    for (const auto& value : history_) hist_.emplace_back(value, false);
    original_hist_ = hist_;
    refresh_warning();
}

ExecutedPartyHistoryDialog::ExecutedPartyHistoryDialog(
    lib::Services services, std::shared_ptr<lib::Window> owner,
    lib::String title, lib::ResourceId icon, std::vector<ExecutionParty> history,
    std::vector<ExecutionParty> bookmark,
    const ::cwxeditor::cwx::variables::FlexEtcProps& etc,
    const ::cwxeditor::cwx::system::System* system,
    PartyHistoryCallbacks callbacks)
    : ExecutedPartyHistoryDialog(std::move(services), std::move(owner),
                                 std::move(title), icon, std::move(history),
                                 std::move(bookmark), partyHistorySettings(etc, system),
                                 std::move(callbacks)) {}

ExecutedPartyHistoryDialog::~ExecutedPartyHistoryDialog() {
    if (services_.drag_drop && list_) services_.drag_drop->unbind(list_);
}

bool ExecutedPartyHistoryDialog::setup(lib::Container& area) {
    auto area_layout = std::make_shared<lib::GridLayout>();
    area_layout->columns = 1U;
    area_layout->spacing = 4.0F;
    area_layout->padding = {5.0F, 5.0F, 5.0F, 5.0F};
    area.set_layout(area_layout);

    if (!hint_) {
        hint_ = std::make_shared<lib::Label>(settings_.bookmark_hint);
        hint_->wrap = true;
        hint_->accessibility_name = "executed party history hint";
        hint_->set_layout_item({.row = 0U, .column = 0U,
                                .preferred = {300.0F, 18.0F}});
        area.add(hint_);
    }
    if (!list_) {
        list_ = std::make_shared<lib::TableView>();
        // Ctrl+X/C/V は個別に処理する。一覧の切り取り・コピー・貼り付けはポップアップにしかな
        // く、 共通のメニュー検索ではショートカットを見つけられないため（D版のTCPDキーに相当）。
        list_->on_key_down = [this](lib::KeyEvent& event) {
            (void)dmenu::handleClipboardShortcut(event, {
                [this] { return canDoT(); }, [this] { return canDoC(); },
                [this] { return canDoP(); },
                [this] { cut(); }, [this] { copy(); }, [this] { (void)paste(); }});
        };
        list_->multi_select = true;
        list_->checkable = true;
        list_->check_selected_rows = true;
        list_->full_row_selection = true;
        list_->virtualized = true;
        list_->edit_on_double_click = false;
        list_->columns = {
            {settings_.party_name, static_cast<float>(settings_.name_column_width)},
            {settings_.yado_name_label, static_cast<float>(settings_.yado_column_width)},
            {settings_.engine_name_label, static_cast<float>(settings_.engine_column_width)},
        };
        list_->accessibility_name = "executed party history";
        list_->set_layout_item({.row = 1U, .column = 0U,
                                .minimum = {300.0F, 160.0F},
                                .preferred = {450.0F, 220.0F}, .grow = 1.0F});
        list_->on_selection_changed = [this](const std::vector<std::size_t>& values) {
            select(values);
        };
        list_->on_check_changed = [this](std::size_t, bool) {
            if (list_->rows.size() != hist_.size()) return;
            bool changed = false;
            // 表はコールバック前に、選択行全体の処理を完了する。 並べ替えで表
            // 示される前に、その全体を一度複製し、後のコールバックでは変更なし
            // とする。 D版の CHECK と同様、取り消し履歴は作らない。
            for (std::size_t index = 0; index < hist_.size(); ++index) {
                const bool checked = list_->rows[index].checked;
                changed = changed || hist_[index].bookmark != checked;
                hist_[index].bookmark = checked;
            }
            if (changed) {
                refresh_warning();
                notify_changed();
            }
        };
        // 既存の追加機能: D版の履歴編集に加え、行のダブルクリックから
        // 保存済みパーティを起動できる。実際の起動は所有ウィンドウへ委譲する。
        list_->on_cell_activate = [this](std::size_t, std::size_t) {
            if (selected_.empty() || !callbacks_.execute_engine) return;
            const auto& value = hist_[selected_.front()].hist;
            callbacks_.execute_engine({}, value);
        };
        context_menu_ = std::make_shared<lib::Menu>(true);
        context_menu_->items = {
            {"元に戻す", "undo"}, {"やり直し", "redo"},
            {"上へ", "up"}, {"下へ", "down"},
            {"切り取り", "cut"}, {"コピー", "copy"},
            {"貼り付け", "paste"}, {"削除", "delete"},
            {"すべて選択", "select-all"},
            {"存在しない履歴を削除", "delete-missing"},
            {"パーティを追加", "put-party"},
        };
        context_menu_->on_command = [this](const std::string_view command) {
            if (command == "undo") (void)undo();
            else if (command == "redo") (void)redo();
            else if (command == "up") up();
            else if (command == "down") down();
            else if (command == "cut") cut();
            else if (command == "copy") copy();
            else if (command == "paste") (void)paste();
            else if (command == "delete") del();
            else if (command == "select-all") {
                std::vector<std::size_t> indices;
                indices.reserve(hist_.size());
                for (std::size_t index = 0; index < hist_.size(); ++index)
                    indices.push_back(index);
                select(std::move(indices));
            }
            else if (command == "delete-missing") del_not_exists();
            if (context_menu_ && context_menu_->on_open)
                context_menu_->on_open();
        };
        context_menu_->on_open = [this] {
            if (!context_menu_) return;
            for (auto& item : context_menu_->items) {
                if (item.command == "undo") item.enabled = can_undo();
                else if (item.command == "redo") item.enabled = can_redo();
                else if (item.command == "up") item.enabled = can_up();
                else if (item.command == "down") item.enabled = can_down();
                else if (item.command == "cut" || item.command == "copy")
                    item.enabled = can_do_C();
                else if (item.command == "paste") item.enabled = can_do_P();
                else if (item.command == "delete") item.enabled = can_do_D();
                else if (item.command == "select-all")
                    item.enabled = !hist_.empty() &&
                                   selected_.size() != hist_.size();
                else if (item.command == "delete-missing")
                    item.enabled = std::ranges::any_of(hist_, [this](const auto& entry) {
                        return !existsParty(settings_, entry.hist);
                    });
            }
            auto& put = context_menu_->items.back();
            put.open_children = [this] {
                ExecEngineMenuOptions options;
                options.filesystem = services_.filesystem.get();
                options.app_path = settings_.app_path;
                options.engine_path = settings_.engine_path;
                options.engine_name = utf8_path(fs_path(settings_.engine_path).stem());
                options.classic_engines = settings_.classic_engines;
                options.images = settings_.images;
                options.read_classic_party_name = settings_.read_classic_party_name;
                options.selectable_party = [this](const EPKey& key) {
                    return std::ranges::none_of(hist_, [&](const auto& entry) {
                        const auto& p = entry.hist;
                        return epKey(p.enginePath, p.yadoPath, p.partyPath,
                                     services_.filesystem.get()) == key;
                    });
                };
                options.exec_engine = [this](StringView, StringView,
                                             const ExecutionParty& party) {
                    // 既存の取り消し・選択・適用経路を通じて登録する。 こ
                    // のコールバックからは、意図的にエンジンを起動しない。
                    auto document = XNode::create("executionParties");
                    (void)party.toNode(document);
                    (void)paste_impl(document);
                };
                auto parties = std::make_shared<lib::Menu>();
                createExecEngineMenu(options, {}, parties);
                // この動的サブメニューは、ユーザーが開くときだけ存在する。
                // 返す行を反映する前に、アイコンの提供処理を解決する。
                if (parties->on_open) parties->on_open();
                return parties->items;
            };
            ++context_menu_->item_update_revision;
        };
        context_menu_->on_open();
        list_->on_pointer = [this](lib::PointerEvent& event) {
            if (event.button != lib::PointerButton::Secondary ||
                event.action != lib::PointerAction::Up || !services_.gui)
                return;
            if (context_menu_->on_open) context_menu_->on_open();
            services_.gui->show_context_menu(context_menu_, list_, event.position);
            event.handled = true;
        };
        area.add(list_);
    }

    if (services_.drag_drop) {
        lib::DragSourceBinding source;
        source.source = list_;
        source.allowed = lib::DropOperation::Copy | lib::DropOperation::Move;
        source.provide_data = [this] {
            lib::ClipboardData data;
            if (!drag_state_.begin(*list_, selected_)) return data;
            const auto serialized = create_node(selected_).text();
            data.text = serialized;
            data.formats.emplace(String(EXECUTION_PARTIES_MIME),
                                 bytes_from_string(serialized));
            return data;
        };
        source.provide_origin = [this] { return drag_state_.origin(); };
        source.completed = [this](lib::DropOperation) { drag_state_.finish(); };
        services_.drag_drop->bind_source(std::move(source));

        lib::DropTargetBinding target;
        target.target = list_;
        target.pane_id = drag_state_.paneId();
        target.accepts_target_index = [this](std::optional<std::size_t> index) {
            return !index || *index <= hist_.size();
        };
        target.accepted_formats = {String(EXECUTION_PARTIES_MIME)};
        const auto negotiate = [](lib::DragEvent& event) {
            event.selected = event.same_pane() ? lib::DropOperation::Move
                                               : lib::DropOperation::Copy;
        };
        target.on_enter = negotiate;
        target.on_over = negotiate;
        target.on_drop = [this](lib::DragEvent& event) {
            event.selected = lib::DropOperation::None;
            event.handled = drop(event, event.target_index.value_or(hist_.size()));
            if (event.handled) event.selected = event.same_pane()
                ? lib::DropOperation::Move : lib::DropOperation::Copy;
        };
        services_.drag_drop->bind_target(std::move(target));
    }
    if (!ok_button_) {
        auto separator = std::make_shared<lib::Separator>();
        separator->orientation = lib::Orientation::Horizontal;
        separator->set_layout_item({.row = 2U, .column = 0U,
                                    .preferred = {450.0F, 1.0F}});
        area.add(separator);

        auto footer = std::make_shared<lib::Panel>();
        auto footer_layout = std::make_shared<lib::GridLayout>();
        footer_layout->columns = 4U;
        footer_layout->spacing = 8.0F;
        footer->set_layout(footer_layout);
        footer->set_layout_item({.row = 3U, .column = 0U,
                                 .preferred = {450.0F, 30.0F}});
        auto spacer = std::make_shared<lib::Panel>();
        spacer->set_layout_item({.row = 0U, .column = 0U, .grow = 1.0F});
        footer->add(spacer);
        ok_button_ = std::make_shared<lib::Button>(settings_.ok_text);
        ok_button_->default_button = true;
        ok_button_->set_layout_item({.row = 0U, .column = 1U,
                                     .preferred = {96.0F, 24.0F}});
        ok_button_->on_click = [this] {
            if (!apply()) return;
            original_hist_ = hist_;
            if (dialog_) dialog_->result = lib::DialogResult::OK;
            close();
        };
        cancel_button_ = std::make_shared<lib::Button>(settings_.cancel_text);
        cancel_button_->cancel_button = true;
        cancel_button_->set_layout_item({.row = 0U, .column = 2U,
                                         .preferred = {96.0F, 24.0F}});
        cancel_button_->on_click = [this] { cancel(); };
        apply_button_ = std::make_shared<lib::Button>(settings_.apply_text);
        apply_button_->set_enabled(false);
        apply_button_->set_layout_item({.row = 0U, .column = 3U,
                                        .preferred = {96.0F, 24.0F}});
        apply_button_->on_click = [this] {
            if (!apply()) return;
            original_hist_ = hist_;
            apply_button_->set_enabled(false);
            if (dialog_) dialog_->result = lib::DialogResult::Apply;
        };
        footer->add(ok_button_);
        footer->add(cancel_button_);
        footer->add(apply_button_);
        area.add(footer);
    }
    render();
    return true;
}

bool ExecutedPartyHistoryDialog::setup() {
    if (!services_.gui) return false;
    if (!dialog_) dialog_ = services_.gui->create_dialog();
    if (!dialog_) return false;
    dialog_->title = title_;
    dialog_->icon = icon_;
    lib::configure_owned_window(dialog_, owner_,
        lib::OwnedWindowKind::Modeless);
    dialog_->resizable = true;
    dialog_->minimizable = true;
    dialog_->maximizable = true;
    const lib::Size client_size{
        std::max(320.0F, settings_.dialog_size.width - 16.0F),
        std::max(220.0F, settings_.dialog_size.height - 39.0F)};
    dialog_->set_bounds({0.0F, 0.0F, client_size.width, client_size.height});
    dialog_->minimum_size = {320.0F, 220.0F};
    dialog_->accessibility_name = title_;
    dialog_->on_close_requested = [this] { cancel(); };
    return setup(*dialog_);
}

void ExecutedPartyHistoryDialog::show() {
    if (!dialog_ && !setup()) return;
    if (!dialog_ || !services_.gui) return;
    dialog_->result = lib::DialogResult::None;
    services_.gui->show(dialog_);
}

void ExecutedPartyHistoryDialog::close() {
    if (dialog_ && services_.gui) services_.gui->close(dialog_);
}

void ExecutedPartyHistoryDialog::cancel() {
    replace_state(original_hist_, {});
    if (dialog_) dialog_->result = lib::DialogResult::Cancel;
    close();
}

void ExecutedPartyHistoryDialog::update_live_settings(const std::size_t historyMax,
    const bool priority, const std::size_t undoMax, const std::size_t committedCount) {
    const bool historyChanged = settings_.history_max != historyMax;
    settings_.history_max = historyMax;
    settings_.remove_non_existing_with_priority = priority;
    settings_.undo_max = undoMax;
    ref_undo_max();
    if (historyChanged) {
        history_ = removePartyHistoryNonExisting(settings_, priority, std::move(history_), historyMax);
        const auto ordinary = static_cast<std::size_t>(std::count_if(hist_.begin(), hist_.end(),
            [](const auto& entry) { return !entry.bookmark; }));
        if (apply_button_ && committedCount < historyMax && committedCount < ordinary)
            apply_button_->set_enabled(true);
        refresh_warning();
    }
    if (callbacks_.toolbar_refresh) callbacks_.toolbar_refresh();
}

void ExecutedPartyHistoryDialog::set_settings(PartyHistorySettings settings) {
    settings_ = std::move(settings);
    ref_undo_max();
    ref_history_max();
    render();
    refresh_warning();
}

void ExecutedPartyHistoryDialog::select(std::vector<std::size_t> indices) {
    indices.erase(std::remove_if(indices.begin(), indices.end(),
                                 [this](const auto index) {
                                     return index >= hist_.size();
                                 }),
                  indices.end());
    std::sort(indices.begin(), indices.end());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
    selected_ = std::move(indices);
    if (list_) (void)lib::select_table_rows(*list_, selected_, false, false);
}

void ExecutedPartyHistoryDialog::set_bookmark(const std::size_t index,
                                              const bool value) {
    if (index >= hist_.size() || hist_[index].bookmark == value) return;
    store();
    hist_[index].bookmark = value;
    render();
    refresh_warning();
    notify_changed();
}

void ExecutedPartyHistoryDialog::refresh_warning() {
    warnings_.clear();
    const auto count = static_cast<std::size_t>(std::count_if(
        hist_.begin(), hist_.end(), [](const PartyHistoryEntry& value) {
            return !value.bookmark;
        }));
    if (settings_.history_max < count) {
        warnings_.push_back(format_warning(settings_.warning_history_too_many,
                                            count, settings_.history_max));
    }
    if (callbacks_.warning_changed) callbacks_.warning_changed(warnings_);
}

void ExecutedPartyHistoryDialog::render() {
    if (!list_) return;
    std::vector<lib::TableRow> rows;
    rows.reserve(hist_.size());
    for (const auto& entry : hist_) {
        lib::TableRow row;
        row.cells.resize(3);
        row.cells[0].text = entry.hist.partyName;
        row.cells[1].text = entry.hist.yadoName;
        row.cells[2].text = entry.hist.engineName;
        row.cells[0].icon = existsParty(settings_, entry.hist)
                                ? settings_.images.execution_engine
                                : settings_.images.warning;
        row.checked = entry.bookmark;
        row.cells[0].checked = entry.bookmark;
        rows.push_back(std::move(row));
    }
    const auto target=list_;
    (void)lib::replace_table_rows(*target, std::move(rows), selected_);
}

void ExecutedPartyHistoryDialog::notify_changed(const bool toolbar) {
    if (apply_button_) apply_button_->set_enabled(true);
    if (callbacks_.changed) callbacks_.changed();
    if (toolbar && callbacks_.toolbar_refresh) callbacks_.toolbar_refresh();
}

void ExecutedPartyHistoryDialog::replace_state(
    std::vector<PartyHistoryEntry> value, std::vector<std::size_t> selected) {
    hist_ = std::move(value);
    selected_ = std::move(selected);
    selected_.erase(std::remove_if(selected_.begin(), selected_.end(),
                                   [this](const auto index) {
                                       return index >= hist_.size();
                                   }),
                    selected_.end());
    std::sort(selected_.begin(), selected_.end());
    selected_.erase(std::unique(selected_.begin(), selected_.end()), selected_.end());
    render();
    refresh_warning();
    notify_changed();
}

bool ExecutedPartyHistoryDialog::can_up() const noexcept {
    return !selected_.empty() && *std::min_element(selected_.begin(), selected_.end()) > 0;
}

void ExecutedPartyHistoryDialog::up() {
    if (!can_up()) return;
    store();
    up_impl(1);
    notify_changed();
}

void ExecutedPartyHistoryDialog::up_impl(const std::size_t count) {
    auto indices = selected_;
    std::sort(indices.begin(), indices.end());
    for (std::size_t move = 0; move < count && !indices.empty(); ++move) {
        if (indices.front() == 0) break;
        for (const auto index : indices) std::swap(hist_[index - 1], hist_[index]);
        for (auto& index : indices) --index;
    }
    select(std::move(indices));
    render();
}

bool ExecutedPartyHistoryDialog::can_down() const noexcept {
    return !selected_.empty() &&
           *std::max_element(selected_.begin(), selected_.end()) + 1U < hist_.size();
}

void ExecutedPartyHistoryDialog::down() {
    if (!can_down()) return;
    store();
    down_impl(1);
    notify_changed();
}

void ExecutedPartyHistoryDialog::down_impl(const std::size_t count) {
    auto indices = selected_;
    std::sort(indices.begin(), indices.end());
    for (std::size_t move = 0; move < count && !indices.empty(); ++move) {
        if (indices.back() + 1U >= hist_.size()) break;
        for (auto it = indices.rbegin(); it != indices.rend(); ++it) {
            std::swap(hist_[*it], hist_[*it + 1U]);
        }
        for (auto& index : indices) ++index;
    }
    select(std::move(indices));
    render();
}

bool ExecutedPartyHistoryDialog::can_undo() const noexcept {
    return undo_ && undo_->canUndo();
}

bool ExecutedPartyHistoryDialog::can_redo() const noexcept {
    return undo_ && undo_->canRedo();
}

bool ExecutedPartyHistoryDialog::undo() {
    return undo_ && undo_->undo();
}

bool ExecutedPartyHistoryDialog::redo() {
    return undo_ && undo_->redo();
}

void ExecutedPartyHistoryDialog::ref_undo_max() {
    if (undo_) undo_->max(settings_.undo_max);
}

void ExecutedPartyHistoryDialog::ref_classic_skin(const bool notify_owner) {
    // Commons の更新を受けたときは、D版と同様に行だけを更新する
    // 。 所有者へ再通知すると同じシグナルを再帰的に出し続け、スタックが
    // あふれるため。 明示的な呼び出し元からの通知は引き続き許可する。
    if (notify_owner && callbacks_.classic_skin_changed) callbacks_.classic_skin_changed();
    render();
}

void ExecutedPartyHistoryDialog::ref_history_max() {
    refresh_warning();
    notify_changed(false);
}

void ExecutedPartyHistoryDialog::store() {
    if (undo_) undo_->add(std::make_shared<HUndo>(*this));
}

XNode ExecutedPartyHistoryDialog::create_node(
    const std::span<const std::size_t> indices) const {
    auto document = XNode::create("executionParties");
    for (const auto index : indices) {
        if (index >= hist_.size()) continue;
        auto element = hist_[index].hist.toNode(document);
        if (hist_[index].bookmark) element.newAttr("bookmark", true);
    }
    return document;
}

bool ExecutedPartyHistoryDialog::can_do_C() const noexcept {
    return services_.clipboard && !selected_.empty();
}

bool ExecutedPartyHistoryDialog::can_do_P() const {
    if (!services_.clipboard) return false;
    return xmlbytestransfer::CBisXML(xmlbytestransfer::ClipData(services_.clipboard));
}

bool ExecutedPartyHistoryDialog::can_do_D() const noexcept {
    return !selected_.empty();
}

void ExecutedPartyHistoryDialog::copy() {
    if (!can_do_C() || !services_.clipboard) return;
    xmlbytestransfer::ClipData clipboard(services_.clipboard);
    xmlbytestransfer::XMLtoCB(settings_.xmlCopyProvider ? settings_.xmlCopyProvider()
                                                      : settings_.xmlCopy,
        clipboard, create_node(selected_).text());
    if (callbacks_.toolbar_refresh) callbacks_.toolbar_refresh();
}

void ExecutedPartyHistoryDialog::cut() {
    if (!can_do_C()) return;
    copy();
    del();
}

bool ExecutedPartyHistoryDialog::paste() {
    if (!services_.clipboard) return false;
    const auto xml = xmlbytestransfer::CBtoXML(*services_.clipboard);
    return !xml.empty() && paste_xml(xml);
}

void ExecutedPartyHistoryDialog::del() {
    if (selected_.empty()) return;
    store();
    for (auto it = selected_.rbegin(); it != selected_.rend(); ++it) {
        if (*it < hist_.size()) hist_.erase(hist_.begin() + static_cast<std::ptrdiff_t>(*it));
    }
    selected_.clear();
    render();
    refresh_warning();
    notify_changed();
}

void ExecutedPartyHistoryDialog::clone() {
    // D版と同様、このダイアログの複製は意図的に無効化する。
}

bool ExecutedPartyHistoryDialog::paste_xml(const std::string_view serialized) {
    try {
        return paste_impl(XNode::parse(serialized));
    } catch (const std::exception&) {
        return false;
    }
}

bool ExecutedPartyHistoryDialog::paste_impl(XNode node) {
    if (!node.valid() || node.name() != "executionParties") return false;
    std::vector<PartyHistoryEntry> added;
    std::vector<std::size_t> selected;
    std::map<EPKey, std::size_t> known;
    for (std::size_t index = 0; index < hist_.size(); ++index) {
        const auto& value = hist_[index].hist;
        known[epKey(value.enginePath, value.yadoPath, value.partyPath,
                    services_.filesystem.get())] = index;
    }
    node.onTag[String(ExecutionParty::XML_NAME)] =
        [this, &added, &selected, &known](XNode& element) {
            ExecutionParty value;
            value.fromNode(element);
            const auto key = epKey(value.enginePath, value.yadoPath, value.partyPath,
                                   services_.filesystem.get());
            const auto found = known.find(key);
            if (found != known.end()) {
                selected.push_back(found->second);
                return;
            }
            const auto index = hist_.size() + added.size();
            const bool marked = element.attr("bookmark", false, false);
            added.emplace_back(std::move(value), marked);
            selected.push_back(index);
            known.emplace(key, index);
        };
    node.parse();
    if (added.empty()) return false;

    store();
    hist_.insert(hist_.end(), std::make_move_iterator(added.begin()),
                 std::make_move_iterator(added.end()));
    select(std::move(selected));
    render();
    refresh_warning();
    notify_changed();
    return true;
}

void ExecutedPartyHistoryDialog::del_not_exists() {
    const auto same_entry = [](const PartyHistoryEntry& left,
                               const PartyHistoryEntry& right) {
        // DのselsはHist値（bookmarkを含む）で照合するため、全項目で選択を復元する。
        return left.bookmark == right.bookmark &&
               std::tie(left.hist.engineName, left.hist.enginePath,
                        left.hist.isClassic, left.hist.yadoName, left.hist.yadoPath,
                        left.hist.partyName, left.hist.partyPath) ==
                   std::tie(right.hist.engineName, right.hist.enginePath,
                            right.hist.isClassic, right.hist.yadoName, right.hist.yadoPath,
                            right.hist.partyName, right.hist.partyPath);
    };
    std::vector<PartyHistoryEntry> selected_entries;
    selected_entries.reserve(selected_.size());
    for (const auto index : selected_) {
        if (index < hist_.size()) selected_entries.push_back(hist_[index]);
    }

    std::vector<PartyHistoryEntry> kept;
    kept.reserve(hist_.size());
    std::vector<std::size_t> selected;
    for (const auto& value : hist_) {
        if (!existsParty(settings_, value.hist)) continue;
        if (std::ranges::any_of(selected_entries, [&](const auto& old) {
                return same_entry(old, value);
            })) {
            selected.push_back(kept.size());
        }
        kept.push_back(value);
    }
    if (kept.size() == hist_.size()) return;

    store();
    hist_ = std::move(kept);
    select(std::move(selected));
    render();
    refresh_warning();
    notify_changed();
}

bool ExecutedPartyHistoryDialog::drop(const lib::DragEvent& event,
                                      std::size_t drop_index) {
    if (event.same_pane()) {
        if (!list_) return false;
        const auto anchor = drag_state_.anchor(event, *list_, selected_);
        if (!anchor) return false;
        if (drop_index == static_cast<std::size_t>(-1)) drop_index = hist_.size();
        if (drop_index == *anchor || drop_index > hist_.size()) return false;
        store();
        if (drop_index < *anchor) up_impl(*anchor - drop_index);
        else down_impl(drop_index - *anchor);
        refresh_warning();
        notify_changed();
        return true;
    }
    const auto found = event.data.formats.find(String(EXECUTION_PARTIES_MIME));
    if (found == event.data.formats.end()) return false;
    return paste_xml(string_from_bytes(found->second));
}

bool ExecutedPartyHistoryDialog::apply() {
    if (list_ && list_->rows.size() == hist_.size()) {
        for (std::size_t index = 0; index < hist_.size(); ++index)
            hist_[index].bookmark = list_->rows[index].checked;
    }
    bookmark_.clear();
    history_.clear();
    for (const auto& value : hist_) {
        if (value.bookmark) bookmark_.push_back(value.hist);
        else history_.push_back(value.hist);
    }
    history_ = removePartyHistoryNonExisting(
        settings_, settings_.remove_non_existing_with_priority, std::move(history_),
        settings_.history_max);
    if (callbacks_.persisted) callbacks_.persisted(history_, bookmark_);
    if (apply_button_) apply_button_->set_enabled(false);
    if (callbacks_.changed) callbacks_.changed();
    if (callbacks_.toolbar_refresh) callbacks_.toolbar_refresh();
    return true;
}

EPKey epKey(const lib::StringView engine, const lib::StringView yado,
            const lib::StringView party,
            const lib::FileSystemBackend* filesystem) {
    // D版の partyhistory.d と同様、正規化するのは実行ファイルのパスだけ。
    // 宿・パーティの相対表記は区別し、ホストのファイル名の大文字小文字の規則だけを適用する。
    (void)filesystem;
    const auto key = [](StringView value) {
        return ::cwxeditor::cwx::utils::cfnmatch("A", "a")
            ? ::cwxeditor::cwx::utils::caseFold(value) : String(value);
    };
    return {key(normalized_absolute(engine)), key(yado), key(party)};
}

bool existsParty(const PartyHistorySettings& settings, const ExecutionParty& party) {
    if (party.enginePath.empty() || !exists(party.enginePath)) return false;
    const Path engine(party.enginePath);
    const Path engine_parent = engine.parent_path();
    Path yado_root;
    if (party.isClassic) {
        const auto legacy_name = settings.yado_name_for_legacy
                                     ? settings.yado_name_for_legacy(utf8_path(engine.filename()))
                                     : utf8_path(engine.filename());
        yado_root = engine_parent / legacy_name;
    } else {
        yado_root = engine_parent / "Yado";
    }
    const Path yado = yado_root / Path(party.yadoPath).filename();
    const Path party_path = party.isClassic
                                ? yado / Path(party.partyPath).filename().replace_extension(".wpl")
                                : yado / "Party" / Path(party.partyPath).filename();
    std::error_code error;
    if (!std::filesystem::is_regular_file(engine, error)) return false;
    if (!std::filesystem::is_directory(yado, error)) return false;
    if (!std::filesystem::exists(party_path, error)) return false;
    return party.isClassic ? std::filesystem::is_regular_file(party_path, error)
                           : std::filesystem::is_directory(party_path, error);
}

std::vector<ExecutionParty> removePartyHistoryNonExisting(
    const PartyHistorySettings& settings, const bool priority,
    std::vector<ExecutionParty> parties, const std::size_t history_max) {
    if (parties.size() <= history_max) return parties;
    if (priority) {
        for (std::size_t index = parties.size();
             index-- > 0 && parties.size() > history_max;) {
            if (!existsParty(settings, parties[index])) {
                parties.erase(parties.begin() + static_cast<std::ptrdiff_t>(index));
            }
        }
    }
    if (parties.size() > history_max) parties.resize(history_max);
    return parties;
}

void putIcon(lib::MenuItem* first, lib::MenuItem* second,
             const PartyImageIds& images, const bool has_warning) {
    const auto icon = has_warning ? images.warning : images.execution_engine;
    if (first) first->icon = icon;
    if (second) second->icon = icon;
}

void putEngineIcon(lib::MenuItem* first, lib::MenuItem* second,
                   const PartyImageIds& images, const lib::StringView engine_path,
                   const bool has_warning) {
    if (!usesClassicEngineIcon(engine_path) || !has_warning) return;
    // D版はメニュー項目にネイティブの実行ファイルのアイコンを付けられ
    // る。 この共通項目は所有権付きの実行ファイルアイコンを持たないた
    // め、 指定された画像を維持し、ここでは警告への置換だけを行う。
    if (first) first->icon = images.warning;
    if (second) second->icon = images.warning;
}

void createExecEngineMenu(const ExecEngineMenuOptions& options,
                           const std::shared_ptr<lib::Menu>& menu,
                           const std::shared_ptr<lib::Menu>& with_party) {
    if (!menu && !with_party) return;
    const auto normal_scan = options.engine_scan_path.empty()
                                 ? options.engine_path : options.engine_scan_path;
    const auto normal_launch = options.engine_launch_path.empty()
                                   ? options.engine_path : options.engine_launch_path;
    const auto normal_version = options.engine_version_path.empty()
                                    ? normal_launch : options.engine_version_path;
    std::vector<EngineIconTarget> direct_icon_targets;
    std::vector<EngineIconTarget> party_icon_targets;
    // D版の partyhistory.d と同様、通常エンジンの分岐は設定済みの走査
    // パスで制限する。 直接の起動コマンドは prop.enginePath を使う。
    if (!normal_scan.empty()) {
        lib::MenuItem* direct = nullptr;
        if (menu) {
            if (!menu->items.empty()) {
                lib::MenuItem separator;
                separator.separator = true;
                menu->items.push_back(std::move(separator));
            }
            lib::MenuItem item;
            item.text = options.engine_display_name.empty()
                             ? options.engine_name
                             : options.engine_display_name;
            item.command = "exec-engine-main";
            item.icon = options.images.execution_engine;
            item.enabled = !normal_launch.empty();
            if (options.exec_engine) {
                item.action = [callback = options.exec_engine,
                               engine = normal_launch] {
                    callback(engine, {}, ExecutionParty{});
                };
            }
            menu->items.push_back(item);
            direct = &menu->items.back();
        }
        if (with_party) {
            if (!with_party->items.empty()) {
                lib::MenuItem separator;
                separator.separator = true;
                with_party->items.push_back(std::move(separator));
            }
            // D版の mWithParty と同様、エンジン、宿、パーテ
            // ィの順にサブメニューを作る。 無効でも有効な宿データなら
            // 除外せず表示し、重複・履歴の選択状態を分かるようにする。
            lib::MenuItem engine_item;
            engine_item.text = options.engine_display_name.empty()
                                   ? options.engine_name : options.engine_display_name;
            engine_item.command = "exec-engine-main";
            engine_item.icon = options.images.execution_engine;
            engine_item.enabled = !options.can_exec_with_party || options.can_exec_with_party();
            const Path engine(normal_scan);
            const Path yado_root = engine.parent_path() / options.yado_root_name;
            engine_item.open_children = [options, normal_launch, yado_root] {
                std::vector<lib::MenuItem> children;
                std::error_code error;
                for (const auto& yado : sorted_children(yado_root)) {
                    error.clear();
                    if (!std::filesystem::is_directory(yado, error)) continue;
                    auto item = normal_yado_menu(options, normal_launch, yado);
                    if (!item.text.empty()) children.push_back(std::move(item));
                }
                return children;
            };
            engine_item.children = engine_item.open_children();
            putEngineIcon(direct, &engine_item, options.images, normal_version, false);
            with_party->items.push_back(std::move(engine_item));
            if (options.application_icon && usesClassicEngineIcon(normal_version))
                party_icon_targets.emplace_back(with_party->items.size() - 1U, normal_version);
        } else {
            putEngineIcon(direct, nullptr, options.images, normal_version, false);
        }
        if (direct && options.application_icon && usesClassicEngineIcon(normal_version))
            direct_icon_targets.emplace_back(menu->items.size() - 1U, normal_version);
    }

    bool classic_menu_started = false;
    bool classic_party_started = false;
    for (const auto& classic : options.classic_engines) {
        const auto execute = classic.executePath(options.app_path, false);
        const auto engine = classic.executePath(options.app_path, true);
        std::error_code execute_error;
        if (execute.empty() || !std::filesystem::is_regular_file(fs_path(execute), execute_error) ||
            execute_error) continue;
        const auto version = classic_file_version(fs_path(execute), fs_path(engine));
        // D版の partyhistory.d と同様、putMenu の前に RT_VERSION/ID
        // 1 を確認する。 なければ、直接起動の項目も含め、そのClassicエンジン全体を省略する。
        if (!version) continue;
        const auto label = options.classic_menu_text ? options.classic_menu_text(classic)
                                                      : classic.name.empty() ? classic.legacyName()
                                                                             : classic.name;
        lib::MenuItem* direct = nullptr;
        if (menu) {
            if (!classic_menu_started && !menu->items.empty()) {
                lib::MenuItem separator;
                separator.separator = true;
                menu->items.push_back(std::move(separator));
            }
            classic_menu_started = true;
            lib::MenuItem item;
            item.text = label;
            item.command = "exec-classic:" + normalized_absolute(execute);
            item.icon = options.images.classic_engine;
            // D版と同様、上の RT_VERSION/1 確認後に直接起動の行を挿入
            // する。 下の1.50の条件は、パーティのサブメニューだけに適用する。
            item.enabled = true;
            if (options.exec_engine) {
                item.action = [callback = options.exec_engine, execute] {
                    callback(execute, {}, ExecutionParty{});
                };
            }
            menu->items.push_back(item);
            direct = &menu->items.back();
        }
        if (with_party && supports_classic_party_execution(*version)) {
            if (!classic_party_started && !with_party->items.empty()) {
                lib::MenuItem separator;
                separator.separator = true;
                with_party->items.push_back(std::move(separator));
            }
            classic_party_started = true;
            const Path root = fs_path(execute).parent_path() /
                ::cwxeditor::cwx::system::System{}.yadoName(utf8_path(fs_path(execute).stem()));
            lib::MenuItem engine_item;
            engine_item.text = label;
            engine_item.command = "exec-classic:" + normalized_absolute(execute);
            engine_item.icon = options.images.classic_engine;
            engine_item.enabled = (!options.can_exec_with_party || options.can_exec_with_party()) &&
                                  (!options.can_exec_classic || options.can_exec_classic());
            engine_item.open_children = [options, execute, label, root] {
                std::vector<lib::MenuItem> children;
                std::error_code error;
                for (const auto& yado : sorted_children(root)) {
                    error.clear();
                    if (!std::filesystem::is_directory(yado, error)) continue;
                    auto item = classic_yado_menu(options, execute, label, yado);
                    if (!item.text.empty()) children.push_back(std::move(item));
                }
                return children;
            };
            engine_item.children = engine_item.open_children();
            putEngineIcon(direct, &engine_item, options.images, engine, false);
            with_party->items.push_back(std::move(engine_item));
            if (options.application_icon && usesClassicEngineIcon(engine))
                party_icon_targets.emplace_back(with_party->items.size() - 1U, engine);
        } else {
            putEngineIcon(direct, nullptr, options.images, engine, false);
        }
        if (direct && options.application_icon && usesClassicEngineIcon(engine))
            direct_icon_targets.emplace_back(menu->items.size() - 1U, engine);
    }
    installEngineIconRefresh(menu, std::move(direct_icon_targets), false,
                             options.application_icon, options.application_icon_loaded);
    installEngineIconRefresh(with_party, std::move(party_icon_targets), true,
                             options.application_icon, options.application_icon_loaded);
}

}  // namespace cwxeditor::cwx::editor::gui::dwt::partyhistory
