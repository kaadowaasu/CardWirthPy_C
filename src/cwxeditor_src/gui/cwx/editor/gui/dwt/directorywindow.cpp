#include "cwxeditor_src/gui/cwx/editor/gui/dwt/directorywindow.hpp"
#include "cwxeditor_src/lib/ui_actions.hpp"
#include "cwxeditor_src/lib/file_save_transaction.hpp"
#include "cwxeditor_src/lib/ui_metrics.hpp"
#include "cwxeditor_src/core/cwx/path.hpp"
#include "cwxeditor_src/core/cwx/structs.hpp"
#include "cwxeditor_src/core/cwx/utils.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/areaviewutils.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dutils.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dprops.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <system_error>

namespace cwxeditor::cwx::editor::gui::dwt::directorywindow {

namespace {

using FileTime = std::filesystem::file_time_type;

template <typename Action>
class ScopeExit final {
public:
    explicit ScopeExit(Action action) : action_(std::move(action)) {}
    ScopeExit(const ScopeExit&) = delete;
    ScopeExit& operator=(const ScopeExit&) = delete;
    ~ScopeExit() noexcept {
        if (!active_) return;
        try { action_(); } catch (...) {}
    }
    void release() noexcept { active_ = false; }

private:
    Action action_;
    bool active_{true};
};
[[nodiscard]] String lowerAscii(StringView value) {
    String result(value);
    for (char& ch : result)
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return result;
}

[[nodiscard]] String pathString(const Path& value) {
    return structs::pathToUtf8(value.lexically_normal());
}

[[nodiscard]] Path filePath(StringView value) {
    return structs::pathFromUtf8(value);
}

[[nodiscard]] String absolutePath(StringView value) {
    if (value.empty()) return {};
    std::error_code error;
    auto path = std::filesystem::absolute(filePath(value), error);
    if (error) path = filePath(value);
    return pathString(path);
}

[[nodiscard]] String baseName(StringView value) {
    const auto name = pathString(filePath(value).filename());
    return name.empty() ? String(value) : name;
}

[[nodiscard]] String extension(StringView value) {
    return pathString(filePath(value).extension());
}

[[nodiscard]] String stripExtension(StringView value) {
    const auto path = filePath(value);
    const auto stem = pathString(path.stem());
    return stem.empty() ? String(value) : stem;
}

[[nodiscard]] String pathIdentity(
    StringView value, const lib::FileSystemBackend* filesystem) {
    if (value.empty()) return {};
    const auto absolute = filePath(absolutePath(value));
    std::error_code error;
    auto path = std::filesystem::weakly_canonical(absolute, error);
    if (error) path = absolute.lexically_normal();
    return filesystem ? filesystem->filename_identity(path)
                      : structs::pathToUtf8(path);
}

// UI行の同一性は、シンボリックリンクの別名を解決せず、ファイルシステムの表
// 記・大文字小文字の規則に従う。下記の正規化した同一性は安全確認用に残す。
[[nodiscard]] String lexicalPathIdentity(StringView value, const lib::FileSystemBackend* filesystem) {
    if (value.empty()) return {};
    const auto path = filePath(absolutePath(value)).lexically_normal();
    return filesystem ? filesystem->filename_identity(path)
                      : structs::pathToUtf8(path);
}

[[nodiscard]] bool sameDisplayedPath(StringView left, StringView right,
                                     const lib::FileSystemBackend* filesystem) {
    return !left.empty() && !right.empty() &&
        lexicalPathIdentity(left, filesystem) == lexicalPathIdentity(right, filesystem);
}

[[nodiscard]] bool samePath(StringView left, StringView right,
                            const lib::FileSystemBackend* filesystem = nullptr) {
    if (left.empty() || right.empty()) return false;
    return pathIdentity(left, filesystem) == pathIdentity(right, filesystem);
}

[[nodiscard]] bool isSameOrBelow(
    StringView root, StringView value,
    const lib::FileSystemBackend* filesystem = nullptr) {
    const auto first = pathIdentity(root, filesystem);
    const auto second = pathIdentity(value, filesystem);
    if (first.empty() || second.empty()) return false;
    if (first == second) return true;
    if (second.size() <= first.size() || second.compare(0, first.size(), first) != 0)
        return false;
    const char separator = second[first.size()];
    return separator == '/' || separator == '\\';
}

[[nodiscard]] FileTime modifiedTime(const Path& value) {
    std::error_code error;
    const auto result = std::filesystem::last_write_time(value, error);
    return error ? FileTime{} : result;
}

[[nodiscard]] bool isRegularFile(const Path& value) {
    std::error_code error;
    return std::filesystem::is_regular_file(value, error);
}

[[nodiscard]] bool isDirectory(const Path& value) {
    std::error_code error;
    return std::filesystem::is_directory(value, error);
}

[[nodiscard]] bool isImageExtension(StringView value) {
    const auto ext = lowerAscii(extension(value));
    return ext == ".bmp" || ext == ".gif" || ext == ".jpg" || ext == ".jpeg" ||
           ext == ".png" || ext == ".webp" || ext == ".tif" || ext == ".tiff" ||
           ext == ".apng" || ext == ".ico" || ext == ".jpe" || ext == ".jfif" ||
           ext == ".jfi" || ext == ".jif" ||
           ext == ".jpy1" || ext == ".jptx" || ext == ".jpdc";
}

void replaceAll(String& value, StringView from, StringView to) {
    if (from.empty()) return;
    std::size_t position = 0;
    while ((position = value.find(from, position)) != String::npos) {
        value.replace(position, from.size(), to);
        position += to.size();
    }
}

[[nodiscard]] String expandToolVariables(String value, StringView files,
                                          StringView scenario) {
    replaceAll(value, "{files}", files);
    replaceAll(value, "${files}", files);
    replaceAll(value, "%files%", files);
    replaceAll(value, "{scenario}", scenario);
    replaceAll(value, "${scenario}", scenario);
    replaceAll(value, "%scenario%", scenario);
    return value;
}

[[nodiscard]] std::vector<String> splitCommandLine(StringView command) {
    std::vector<String> result;
    String token;
    bool quoted = false;
    for (std::size_t index = 0; index < command.size(); ++index) {
        const char character = command[index];
        if (character == '"') {
            quoted = !quoted;
            continue;
        }
        if (!quoted && std::isspace(static_cast<unsigned char>(character))) {
            if (!token.empty()) {
                result.push_back(std::move(token));
                token.clear();
            }
            continue;
        }
        if (character == '\\' && index + 1U < command.size() &&
            command[index + 1U] == '"') {
            token.push_back('"');
            ++index;
            continue;
        }
        token.push_back(character);
    }
    if (!token.empty()) result.push_back(std::move(token));
    return result;
}

[[nodiscard]] std::vector<String> splitLines(StringView value) {
    std::vector<String> result;
    std::size_t start = 0;
    while (start <= value.size()) {
        const auto end = value.find('\n', start);
        const auto length = end == String::npos ? value.size() - start : end - start;
        String line(value.substr(start, length));
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty()) result.push_back(std::move(line));
        if (end == String::npos) break;
        start = end + 1U;
    }
    return result;
}

}  // namespace

void DirectoryWindow::FileNameObj::initialise(DirectoryWindow* owner) {
    array = absolutePath(array);
    if (basename.empty()) basename = baseName(array);
    relPath = owner ? owner->toRelPath(array) : array;
    ext = extension(basename);
    extl = lowerAscii(ext);
    pathId = usecounter::toPathId(relPath);
    dir = isDirectory(filePath(array));
    if (dir) {
        material = false;
    } else if (owner && owner->prop_.isMaterial) {
        const auto is_material = owner->prop_.isMaterial;
        material = is_material(basename);
    } else {
        material = isRegularFile(filePath(array));
    }
    if (!dir && isRegularFile(filePath(array))) {
        std::error_code error;
        size = std::filesystem::file_size(filePath(array), error);
        if (error) size = 0;
    }
}

DirectoryWindow::FileNameObj::FileNameObj(DirectoryWindow& owner, String fullPath)
    : array(std::move(fullPath)), basename(baseName(array)) {
    initialise(&owner);
}

DirectoryWindow::FileNameObj::FileNameObj(DirectoryWindow& owner, String parent,
                                          String name)
    : array(pathString(filePath(parent) / filePath(name))), basename(std::move(name)) {
    initialise(&owner);
}

DirectoryWindow::FileNameObj::FileNameObj(String fullPath)
    : array(std::move(fullPath)), basename(baseName(array)) {
    initialise(nullptr);
}

void DirectoryWindow::DirsSelection::widgetSelected() {
    if (owner_) owner_->setFocus(Focus::Directories), owner_->refreshFiles();
}

void DirectoryWindow::FilesSelection::widgetSelected() {
    if (owner_) owner_->filesSelected();
}

void DirectoryWindow::FileSelect::select() {
    if (owner_) owner_->filesSelected();
}

void DirectoryWindow::FileSelect::mouseUp() {
    if (owner_) owner_->filesSelected();
}

void DirectoryWindow::FileSelect::mouseDown() {
    if (owner_) owner_->setFocus(Focus::Files);
}

void DirectoryWindow::FileSelect::mouseDoubleClick() {
    if (!owner_) return;
    const auto values = owner_->selFiles();
    if (values.empty()) return;
    if (isDirectory(filePath(values.front()))) {
        (void)owner_->select(values.front(), true);
    } else if (owner_->prop_.launch) {
        owner_->prop_.launch(values.front());
    } else if (owner_->comm_.services.shell) {
        std::string error;
        (void)owner_->comm_.services.shell->open_path(filePath(values.front()), error);
    }
}

void DirectoryWindow::FileSelect::keyPressed(lib::KeyEvent& event) {
    if (!owner_) return;
    if (event.key == lib::Key::Enter) {
        mouseDoubleClick();
        event.handled = true;
    }
}

void DirectoryWindow::ClosePreview::widgetSelected() {
    if (owner_) owner_->closePreview();
}

void DirectoryWindow::PreviewTrigger::mouseExit() {
    if (owner_) owner_->closePreview();
}

void DirectoryWindow::PreviewTrigger::mouseMove(float x, float y) {
    if (owner_) owner_->previewTrigger(x, y, {x, y});
}

void DirectoryWindow::FDListener::widgetDisposed() {
    if (owner_) owner_->closePreview();
}

void DirectoryWindow::DListener::widgetDisposed() {
    if (owner_) owner_->quitTrace();
}

void DirectoryWindow::SDListener::widgetDisposed() {
    if (owner_) owner_->closeTraceHandle();
}

void DirectoryWindow::RefreshThr::run() {
    if (owner_) owner_->refresh();
}

DirectoryWindow::DirectoryWindow(Commons comm, Props prop, Parent parent)
    : prop_(std::move(prop)), comm_(std::move(comm)) {
    switch (prop_.filesSortColumn) {
    case 1: sort_column_ = SortColumn::Extension; break;
    case 2: sort_column_ = SortColumn::Count; break;
    default: sort_column_ = SortColumn::Name; break;
    }
    sort_direction_ = prop_.filesSortDirection == 2 ? SortDirection::Down
                                                     : SortDirection::Up;
    try {
        // 未完成のペインを親へ公開しない。UI作成後も
        // 、登録やメニューの確保が失敗する場合がある。
        construct(nullptr);
        trace_running_ = prop_.traceChanges;
        if (parent) parent->add(window_);
    } catch (...) {
        callback_lifetime_.reset();
        quitTrace();
        disconnectCallbacks();
        throw;
    }
}

void DirectoryWindow::retireHost() noexcept {
    // コピーしたドラッグコールバックがこの表示を保持中でも、モーダルのプロバイダーが
    // MainWindowを破棄し得る。DirectoryWindowの最後のshared_ptr
    // の破棄を待たず、借用ホストへのコールバックを直ちに無効にする。
    disconnectCallbacks();
    try { quitTrace(); } catch (...) {}
}

DirectoryWindow::~DirectoryWindow() {
    callback_lifetime_.reset();
    quitTrace();
    disconnectCallbacks();
}

void DirectoryWindow::disconnectCallbacks() noexcept {
    if (callbacks_disconnected_) return;
    callbacks_disconnected_ = true;
    callback_lifetime_.reset();
    if (dirs_) {
        dirs_->on_focus = {};
        dirs_->on_selection_changed = {};
        dirs_->on_activate = {};
        dirs_->on_item_edited = {};
        dirs_->on_expansion_changed = {};
        dirs_->on_pointer = {};
    }
    if (files_) {
        files_->on_focus = {};
        files_->removeSelectionListener(file_selection_listener_);
        files_->removeEditListener(file_edit_listener_);
        files_->on_cell_activate = {};
        files_->removePointerListener(file_pointer_listener_);
        files_->on_wheel = {};
    }
    if (menu_) {
        menu_->on_command = {};
        menu_->on_command_with_gesture = {};
        menu_->on_open = {};
    }
    // 接続用プロバイダーは、例外送出前にコールバックを保持している
    // 場合がある。構築失敗時も含め、各後処理を独立に試す。コピー済
    // みのコールバックは、上で寿命を無効にしてあるため動作しない。
    const auto drag_drop = comm_.services.drag_drop;
    if (drag_drop) {
        if (dirs_) { try { drag_drop->unbind(dirs_); } catch (...) {} }
        if (files_) { try { drag_drop->unbind(files_); } catch (...) {} }
    }
}

void DirectoryWindow::construct(Parent parent) {
    std::scoped_lock lock(mutex_);
    if (window_) return;
    window_ = std::make_shared<lib::Panel>();
    window_->accessibility_name = prop_.messages.title;
    // Dは分割部にFILL_BOTHを指定する。プレビュー
    // 補助ウィジェットを通常の子の流れに置くと、分割部が推
    // 奨高しか使わず、Fileペインの残りが空白になる。
    window_->set_layout(std::make_shared<lib::FillLayout>());
    if (parent) parent->add(window_);

    split_ = std::make_shared<lib::SplitView>();
    split_->orientation = prop_.directorySashVertical ? lib::Orientation::Vertical
                                                       : lib::Orientation::Horizontal;
    split_->weights = {std::max(1.0F, prop_.directorySashLeft),
                       std::max(1.0F, prop_.directorySashRight)};
    window_->add(split_);

    dirs_ = std::make_shared<TreeView>();
    dirs_->multi_select = false;
    dirs_->accessibility_name = prop_.messages.title + ":directories";
    split_->add(dirs_);

    // DはDirectoryWindowのペイン識別情報と完了コールバックを
    // 持つ、一つのファイルDragSourceを所有する。設定中は汎用の表の
    // 転送元を抑止し、その後、通常のドラッグ入力と後処理用にサービスを戻す。
    auto table_services = comm_.services;
    table_services.drag_drop.reset();
    files_ = customtable::rangeSelectableTable(
        split_.get(), customtable::STYLE_MULTI_SELECT | customtable::STYLE_HEADER,
        std::move(table_services));
    files_->services(comm_.services);
    files_->multi_select = true;
    files_->full_row_selection = true;
    files_->setHeaderVisible(true);
    files_->addColumn(prop_.messages.fileName, std::max(1.0F, prop_.fileNameColumn),
                      customtable::STYLE_NONE)->sortable(true);
    files_->addColumn(prop_.messages.fileExt, std::max(1.0F, prop_.fileExtColumn),
                      customtable::STYLE_NONE)->sortable(true);
    files_->addColumn(prop_.messages.fileCount, std::max(1.0F, prop_.fileCountColumn),
                      customtable::STYLE_NONE)->sortable(true);
    files_->accessibility_name = prop_.messages.title + ":files";
    const std::weak_ptr<CallbackLifetime> lifetime = callback_lifetime_;
    files_->on_column_resized = [this, lifetime](const std::size_t index,
                                                  const float width) {
        if (lifetime.expired() || !files_ || !std::isfinite(width) || width < 0.0F ||
            index >= files_->columns.size()) return;
        auto keep_alive = weak_from_this().lock();
        if (!keep_alive) return;
        auto& column = files_->columns[index];
        if (index == 0U) {
            prop_.fileNameColumn = width;
            if (prop_.editorProps) dutils::saveColumnWidth(
                prop_.editorProps, column, prop_.editorProps->var().fileNameColumn.value);
        } else if (index == 1U) {
            prop_.fileExtColumn = width;
            if (prop_.editorProps) dutils::saveColumnWidth(
                prop_.editorProps, column, prop_.editorProps->var().fileExtColumn.value);
        } else if (index == 2U) {
            prop_.fileCountColumn = width;
            if (prop_.editorProps) dutils::saveColumnWidth(
                prop_.editorProps, column, prop_.editorProps->var().fileCountColumn.value);
        } else {
            return;
        }
        const auto store = prop_.storeFileColumnWidth;
        if (store) store(index, width);
    };
    const std::size_t initial_sort_column = sort_column_ == SortColumn::Extension ? 1U
                                          : sort_column_ == SortColumn::Count ? 2U : 0U;
    files_->sortColumn(files_->column(initial_sort_column));
    files_->sortDirection(sort_direction_ == SortDirection::Up
        ? customtable::SortDirection::Up : customtable::SortDirection::Down);
    files_->highlighted_column = initial_sort_column;
    for (std::size_t index = 0; index < 3; ++index) {
        files_->column(index)->addSelectionListener([this, lifetime, index] {
            if (lifetime.expired()) return;
            const auto keep_alive = weak_from_this().lock();
            if (!keep_alive || !files_) return;
            const auto next = index == 1 ? SortColumn::Extension
                            : index == 2 ? SortColumn::Count : SortColumn::Name;
            const auto selected = selFiles();
            sort_direction_ = next == sort_column_ && sort_direction_ == SortDirection::Up
                                ? SortDirection::Down : SortDirection::Up;
            sort_column_ = next;
            files_->sortColumn(files_->column(index));
            files_->sortDirection(sort_direction_ == SortDirection::Up
                ? customtable::SortDirection::Up : customtable::SortDirection::Down);
            files_->highlighted_column = index;
            storeSortParams();
            if (!lifetime.expired()) refreshFiles(selected);
        });
    }

    menu_ = std::make_shared<Menu>(true);
    if (prop_.editorProps) {
        preview_ = std::make_unique<areaviewutils::Preview>(
            *prop_.editorProps, window_, comm_.services);
    }
    inc_search_ = std::make_shared<incsearch::IncSearch>(
        comm_.services, window_.get(), [this, lifetime] {
            return !lifetime.expired() && summ_ != nullptr && has_file_;
        });
    inc_search_->modEvent.push_back([this, lifetime] {
        if (lifetime.expired()) return;
        const auto keep_alive = weak_from_this().lock();
        refreshFiles(selFiles());
    });
    installCallbacks();
    createFilesMenu();
}

void DirectoryWindow::reconstruct(Parent parent) {
    if (!window_) construct(parent);
    if (summ_) refresh();
}

void DirectoryWindow::installCallbacks() {
    if (!dirs_ || !files_) return;
    const std::weak_ptr<CallbackLifetime> lifetime = callback_lifetime_;
    const auto focused = [this, lifetime](const Focus target) {
        if (lifetime.expired()) return;
        const auto keep_alive = weak_from_this().lock();
        if (!keep_alive) return;
        setFocus(target);
        refreshStatusLine();
        if (!lifetime.expired()) notifyToolbar();
    };
    dirs_->on_focus = [focused] { focused(Focus::Directories); };
    files_->on_focus = [focused] { focused(Focus::Files); };
    dirs_->on_selection_changed = [this, lifetime](const std::vector<TreeItemId>&) {
        if (lifetime.expired()) return;
        const auto keep_alive = weak_from_this().lock();
        closePreview();
        if (lifetime.expired()) return;
        setFocus(Focus::Directories);
        refreshFiles();
        if (lifetime.expired()) return;
        notifyToolbar();
    };
    dirs_->on_expansion_changed = [this, lifetime](const TreeItemId id, const bool expanded) {
        if (lifetime.expired()) return;
        const auto keep_alive = weak_from_this().lock();
        if (auto* node = findNode(id)) {
            node->expanded = node->suppress_expander ? true : expanded;
            if (node->suppress_expander && comm_.services.gui)
                comm_.services.gui->request_redraw(dirs_);
        }
    };
    dirs_->on_activate = [this, lifetime](TreeItemId id) {
        if (lifetime.expired()) return;
        const auto keep_alive = weak_from_this().lock();
        const auto* node = findNode(id);
        if (!node) return;
        const auto path = node->value;
        const auto open_folder = prop_.openFolder;
        const auto shell = comm_.services.shell;
        if (open_folder) open_folder(path);
        else if (shell) {
            std::string error;
            (void)shell->open_path(filePath(path), error);
        }
    };
    dirs_->on_pointer = [this, lifetime](lib::PointerEvent& event) {
        if (lifetime.expired() || event.button != lib::PointerButton::Secondary ||
            event.action != lib::PointerAction::Up)
            return;
        const auto keep_alive = weak_from_this().lock();
        if (!keep_alive || !dirs_) return;
        // Dのツリーのポップアップ規則に合わせ、ポインター下のディレクト
        // リを操作する。空白で開いた場合は現在のディレクトリを維持する。
        if (event.item_hit_test_available && event.item_index >= 0) {
            const auto id = static_cast<TreeItemId>(event.item_index);
            if (findNode(id)) (void)lib::select_tree_items(*dirs_, {id});
        }
        if (!lifetime.expired() && menu_ && comm_.services.gui)
            comm_.services.gui->show_context_menu(menu_, dirs_, event.position);
        event.handled = true;
    };
    file_selection_listener_ = files_->addSelectionListener([this, lifetime](const std::vector<std::size_t>&) {
        if (lifetime.expired()) return;
        const auto keep_alive = weak_from_this().lock();
        closePreview();
        if (lifetime.expired()) return;
        setFocus(Focus::Files);
        filesSelected();
    });
    files_->on_cell_activate = [this, lifetime](std::size_t row, std::size_t) {
        if (lifetime.expired()) return;
        const auto keep_alive = weak_from_this().lock();
        closePreview();
        if (lifetime.expired()) return;
        setFocus(Focus::Files);
        const auto* value = rowObject(row);
        if (!value) return;
        const auto path = value->array;
        const auto launch = prop_.launch;
        const auto shell = comm_.services.shell;
        if (value->dir) {
            (void)select(path, true);
        } else if (launch) {
            launch(path);
        } else if (shell) {
            std::string error;
            (void)shell->open_path(filePath(path), error);
        }
    };
    dirs_->on_item_edited = [this, lifetime](const TreeItemId id, String text) {
        if (lifetime.expired()) return;
        const auto keep_alive = weak_from_this().lock();
        if (keep_alive) dirsEditEnd(id, std::move(text));
    };
    dutils::installClickSelectedEdit(dirs_, comm_.services, split_, {},
        [this, lifetime](const TreeItemId id) {
            if (lifetime.expired()) return false;
            const auto* node = findNode(id);
            return node && node->parent != 0;
        });
    file_edit_listener_ = files_->addEditListener([this, lifetime](std::size_t row, std::size_t column,
                                         String text) {
        if (lifetime.expired()) return;
        const auto keep_alive = weak_from_this().lock();
        filesEditEnd(row, column, std::move(text));
    });
    // DのTableTextEditは、選択済みファイルを押すと名前を編集する。
    files_->inline_edit_column = 0U;
    ::cwxeditor::cwx::editor::gui::dwt::dutils::installClickSelectedEdit(
        files_, comm_.services, split_, [this, lifetime] {
            if (lifetime.expired() || lib::editor_window_host_options().all_mode) return false;
            return prop_.editorProps && prop_.editorProps->var().editTriggerType.value ==
                static_cast<std::int32_t>(::cwxeditor::cwx::structs::EditTrigger::Quick);
        });
    file_pointer_listener_ = files_->addPointerListener([this, lifetime](lib::PointerEvent& event) {
        if (lifetime.expired()) return;
        const auto keep_alive = weak_from_this().lock();
        if (event.action == lib::PointerAction::Move) {
            // ポインターイベントはウィンドウ相対座標。TableModel::rowAt
            // には、ファイル表のクライアント原点からの相対位置を渡す。
            if (const auto table = files_) {
                const auto bounds = table->bounds();
                previewTrigger(event.position.x - bounds.x,
                               event.position.y - bounds.y,
                               event.screen_position);
            }
        } else if (event.action == lib::PointerAction::Leave ||
                   event.action == lib::PointerAction::Down ||
                   event.action == lib::PointerAction::Cancel) {
            closePreview();
        } else if (event.action == lib::PointerAction::DoubleClick) {
            const auto values = selFiles();
            if (!values.empty()) {
                if (isDirectory(filePath(values.front())))
                    (void)select(values.front(), true);
                else if (const auto launch = prop_.launch) launch(values.front());
            }
        }
        if (event.button == lib::PointerButton::Secondary &&
            event.action == lib::PointerAction::Up) {
            // 具体的な行のポップアップは、先にそのファイルを
            // アクティブな選択にする。表の空白では、以前の複
            // 数選択をTCPDコマンドで使えるよう維持する。
            if (event.item_hit_test_available && event.item_index >= 0 &&
                static_cast<std::size_t>(event.item_index) < files_->rows.size())
                (void)lib::select_table_rows(*files_,
                    {static_cast<std::size_t>(event.item_index)});
            if (!lifetime.expired() && menu_ && comm_.services.gui)
                comm_.services.gui->show_context_menu(menu_, files_, event.position);
            event.handled = true;
        }
    });
    files_->on_wheel = [this, lifetime](lib::WheelEvent&) {
        if (lifetime.expired()) return;
        const auto keep_alive = weak_from_this().lock();
        closePreview();
    };
    if (comm_.services.drag_drop) {
        const auto directory_pane = std::string("directory-window:") +
            std::to_string(reinterpret_cast<std::uintptr_t>(this));
        lib::DragSourceBinding directory_source;
        directory_source.source = dirs_;
        directory_source.allowed = lib::DropOperation::Copy | lib::DropOperation::Move;
        directory_source.provide_data = [this, lifetime] {
            ClipboardData data;
            if (lifetime.expired() || last_focus_ != Focus::Directories) return data;
            const auto keep_alive = weak_from_this().lock();
            const auto value = selDirPath();
            if (!value.empty()) data.files.emplace_back(value);
            return data;
        };
        const auto drag_completed = [this, lifetime](lib::DropOperation operation) {
            if (lifetime.expired() || operation == lib::DropOperation::None) return;
            const auto keep_alive = weak_from_this().lock();
            const auto summary = summ_;
            const auto current = [&] { return !lifetime.expired() && summ_ == summary; };
            const auto selection = selFiles();
            const auto directory = selDirPath();
            refreshDirs(directory);
            if (!current()) return;
            refreshFiles(selection);
            if (!current()) return;
            if (prop_.refPaths) prop_.refPaths(toRelPath(directory));
            if (!current()) return;
            clearCut();
            if (summary && summary->useTemp()) summary->changed();
        };
        directory_source.completed = drag_completed;
        directory_source.origin.pane_id = directory_pane;
        comm_.services.drag_drop->bind_source(std::move(directory_source));

        lib::DragSourceBinding file_source;
        file_source.source = files_;
        file_source.allowed = lib::DropOperation::Copy | lib::DropOperation::Move;
        file_source.origin.pane_id = directory_pane;
        file_source.provide_data = [this, lifetime] {
            ClipboardData data;
            if (lifetime.expired() || last_focus_ != Focus::Files) return data;
            const auto keep_alive = weak_from_this().lock();
            for (const auto& value : selFiles()) data.files.emplace_back(value);
            return data;
        };
        file_source.completed = drag_completed;
        comm_.services.drag_drop->bind_source(std::move(file_source));

        lib::DropTargetBinding directory_target;
        directory_target.target = dirs_;
        directory_target.accepted_formats = {"text/uri-list", "text/plain"};
        directory_target.pane_id = directory_pane;
        directory_target.accepts_target_index = [this](std::optional<std::size_t> index) {
            return index && directory_objects_.contains(static_cast<TreeItemId>(*index));
        };
        directory_target.on_over = [lifetime](lib::DragEvent& event) {
            if (lifetime.expired()) {
                event.selected = lib::DropOperation::None;
                event.handled = false;
                return;
            }
            const auto allowed = static_cast<std::uint32_t>(event.allowed);
            event.selected =
                (allowed & static_cast<std::uint32_t>(lib::DropOperation::Move)) != 0U
                    ? lib::DropOperation::Move
                : (allowed & static_cast<std::uint32_t>(lib::DropOperation::Copy)) != 0U
                    ? lib::DropOperation::Copy : lib::DropOperation::None;
            event.handled = true;
        };
        directory_target.on_drop = [this, lifetime](lib::DragEvent& event) {
            if (lifetime.expired() || !event.target_index) {
                event.handled = false;
                return;
            }
            const auto keep_alive = weak_from_this().lock();
            const auto found = directory_objects_.find(
                static_cast<TreeItemId>(*event.target_index));
            if (found == directory_objects_.end() || !found->second.dir) {
                event.handled = false;
                return;
            }
            std::vector<String> values;
            for (const auto& path : event.data.files) values.push_back(pathString(path));
            if (values.empty()) values = splitLines(event.data.text);
            bool from_out{};
            event.handled = pasteImpl(found->second.array, values, true, from_out);
            event.selected = event.handled
                ? (from_out ? lib::DropOperation::Copy : lib::DropOperation::Move)
                : lib::DropOperation::None;
        };
        comm_.services.drag_drop->bind_target(std::move(directory_target));

        comm_.services.drag_drop->bind_target({
            files_, {"text/uri-list", "text/plain"}, {},
            [lifetime](lib::DragEvent& event) {
                if (lifetime.expired()) { event.handled = false; return; }
                // DのFilesDrop(Table).dragOver
                // は常にMOVEを選ぶ。共通の外部転送はSWTの合成MOVE
                // ではなくCopy|Linkを公開するため、転送元にMove
                // がなければCopyを選び、ドロップ可能に保つ。
                const auto allowed = static_cast<std::uint32_t>(event.allowed);
                event.selected =
                    (allowed & static_cast<std::uint32_t>(lib::DropOperation::Move)) != 0U
                        ? lib::DropOperation::Move
                    : (allowed & static_cast<std::uint32_t>(lib::DropOperation::Copy)) != 0U
                        ? lib::DropOperation::Copy : lib::DropOperation::None;
                event.handled = !event.data.files.empty() || !event.data.text.empty();
            },
            [this, lifetime](lib::DragEvent& event) {
                if (lifetime.expired()) { event.handled = false; return; }
                const auto keep_alive = weak_from_this().lock();
                std::vector<String> values;
                for (const auto& path : event.data.files) values.push_back(pathString(path));
                if (values.empty()) values = splitLines(event.data.text);
                auto target = selDirPath();
                // DのTableへのドロップはポインター下の行を使う。ディ
                // レクトリ行ならそこへ、ファイル行ならその親へ転送する。
                if (event.target_index && *event.target_index < files_->rows.size()) {
                    if (const auto* row = rowObject(*event.target_index))
                        target = row->dir ? row->array
                                          : pathString(filePath(row->array).parent_path());
                }
                bool from_out{};
                event.handled = pasteImpl(target, values, true, from_out);
                event.selected = event.handled
                    ? (from_out ? lib::DropOperation::Copy : lib::DropOperation::Move)
                    : lib::DropOperation::None;
            }, {}});
    }
}

void DirectoryWindow::notifyToolbar() const {
    const auto callback = comm_.refreshToolBar;
    if (callback) callback();
}

void DirectoryWindow::updateDisplaySettings(const bool logicalSort,
                                              std::vector<String> ignorePaths) {
    prop_.logicalSort = logicalSort;
    prop_.ignorePaths = std::move(ignorePaths);
    refresh();
}

int DirectoryWindow::comp(StringView a, StringView b) const {
    return prop_.logicalSort ? ::cwxeditor::cwx::utils::fnncmp(a, b)
                             : ::cwxeditor::cwx::utils::fncmp(a, b);
}

bool DirectoryWindow::compDirName(StringView a, StringView b) const {
    return comp(a, b) < 0;
}

bool DirectoryWindow::compFullPath(StringView a, StringView b) const {
    const auto ad = isDirectory(filePath(a));
    const auto bd = isDirectory(filePath(b));
    return compFNameS(baseName(a), baseName(b), ad, bd);
}

bool DirectoryWindow::compFNameS(StringView a, StringView b, bool ad, bool bd) const {
    if (ad && bd) return comp(a, b) < 0;
    if (!ad && bd) return false;
    if (ad && !bd) return true;
    return comp(a, b) < 0;
}

bool DirectoryWindow::compFName(const FileNameObj& a, const FileNameObj& b) const {
    return compFNameS(a.basename, b.basename, a.dir, b.dir);
}

bool DirectoryWindow::revCompFName(const FileNameObj& a, const FileNameObj& b) const {
    if (a.dir && b.dir) return comp(a.basename, b.basename) < 0;
    if (!a.dir && b.dir) return false;
    if (a.dir && !b.dir) return true;
    return comp(a.basename, b.basename) > 0;
}

bool DirectoryWindow::compFExt(const FileNameObj& a, const FileNameObj& b) const {
    if (a.dir && b.dir) return comp(a.basename, b.basename) < 0;
    if (!a.dir && b.dir) return false;
    if (a.dir && !b.dir) return true;
    const auto result = comp(a.ext, b.ext);
    return result != 0 ? result < 0 : comp(a.basename, b.basename) < 0;
}

bool DirectoryWindow::revCompFExt(const FileNameObj& a, const FileNameObj& b) const {
    if (a.dir && b.dir) return comp(a.basename, b.basename) < 0;
    if (!a.dir && b.dir) return false;
    if (a.dir && !b.dir) return true;
    const auto result = comp(a.ext, b.ext);
    return result != 0 ? result > 0 : comp(a.basename, b.basename) < 0;
}

bool DirectoryWindow::compFCount(const FileNameObj& a, const FileNameObj& b) const {
    const auto ad = !a.material;
    const auto bd = !b.material;
    if (ad && bd) return compFExt(a, b);
    if (!ad && bd) return false;
    if (ad && !bd) return true;
    const auto ac = useCount(a);
    const auto bc = useCount(b);
    if (ac != bc) return ac < bc;
    return compFName(a, b);
}

bool DirectoryWindow::revCompFCount(const FileNameObj& a, const FileNameObj& b) const {
    const auto ad = !a.material;
    const auto bd = !b.material;
    if (ad && bd) return compFExt(a, b);
    if (!ad && bd) return false;
    if (ad && !bd) return true;
    const auto ac = useCount(a);
    const auto bc = useCount(b);
    if (ac != bc) return ac > bc;
    return compFName(a, b);
}

std::uint32_t DirectoryWindow::useCount(const FileNameObj& file) const {
    if (!summ_ || !file.material) return 0;
    const auto counter = summ_->useCounter();
    return counter ? counter->get(toUCPath(file)) : 0;
}

PathId DirectoryWindow::toUCPath(const FileNameObj& file) const {
    return counterPathId(file.relPath);
}

bool DirectoryWindow::isIgnore(StringView path) const {
    // DはcontainsPathのプラットフォーム対応グロブを候補のbasename
    // へ適用する。ディレクトリ区切りも含め、設定パターンをそのまま維持する。
    return utils::containsPath(prop_.ignorePaths, baseName(path));
}

bool DirectoryWindow::isDef(StringView path, bool directory) const {
    const bool system = summ_ ? summ_->isSystemFile(path, directory)
                              : (prop_.isSystemFile ? prop_.isSystemFile(path, directory)
                                                    : false);
    return system || isIgnore(path);
}

bool DirectoryWindow::isScenarioRoot(StringView path) const {
    return summ_ && samePath(path, summ_->scenarioPath(),
                             comm_.services.filesystem.get());
}

String DirectoryWindow::toRelPath(StringView file) const {
    const auto absolute = absolutePath(file);
    if (!summ_ || summ_->scenarioPath().empty()) return absolute;
    const auto root = filePath(absolutePath(summ_->scenarioPath()));
    const auto value = filePath(absolute);
    // 参照パスは字句上のシナリオパス。filesystem::relative()
    // は実ファイルを通じて正規化するため、名前
    // 変更後の旧パスでは失敗し得る。絶対パス
    // へ代替するとUseCounterの相対
    // キーに一致せず、表示使用数が0になる。
    const auto relative = value.lexically_relative(root);
    if (relative.empty())
        return samePath(absolute, pathString(root),
                        comm_.services.filesystem.get()) ? String{} : absolute;
    return pathString(relative);
}

bool DirectoryWindow::isCutted(StringView file) const {
    const auto filesystem = comm_.services.filesystem.get();
    const auto value = lexicalPathIdentity(file, filesystem);
    for (const auto& cut : cuts_)
        if (lexicalPathIdentity(cut, filesystem) == value) return true;
    return false;
}

const lib::TreeNode* DirectoryWindow::findNode(TreeItemId id) const {
    if (!dirs_) return nullptr;
    for (const auto& node : dirs_->nodes)
        if (node.id == id) return &node;
    return nullptr;
}

lib::TreeNode* DirectoryWindow::findNode(TreeItemId id) {
    if (!dirs_) return nullptr;
    for (auto& node : dirs_->nodes)
        if (node.id == id) return &node;
    return nullptr;
}

const DirectoryWindow::FileNameObj* DirectoryWindow::rowObject(std::size_t row) const {
    if (!files_ || row >= files_->rows.size()) return nullptr;
    return std::any_cast<FileNameObj>(&std::as_const(*files_).rowState(row).data);
}

DirectoryWindow::FileNameObj* DirectoryWindow::rowObject(std::size_t row) {
    if (!files_ || row >= files_->rows.size()) return nullptr;
    return std::any_cast<FileNameObj>(&files_->rowState(row).data);
}

TreeItemId DirectoryWindow::appendDirectory(
    PreparedDirectories& prepared, TreeItemId parent, StringView path, StringView selected,
    const std::unordered_map<String, bool>& expanded,
    const std::unordered_map<String, TreeItemId>& identities,
    const std::function<bool()>& current) {
    if (!current()) return 0;
    const auto full = absolutePath(path);
    const auto previous_id = identities.find(full);
    const auto id = previous_id == identities.end() ? prepared.next_id++ : previous_id->second;
    lib::TreeNode node;
    node.id = id;
    node.parent = parent;
    node.text = isScenarioRoot(full) ? "Scenario" : baseName(full);
    node.value = full;
    node.icon = isCutted(full) ? prop_.images.skeletonFolder : prop_.images.folder;
    const auto old = expanded.find(full);
    node.expanded = old == expanded.end() ? true : old->second;
    node.suppress_expander = isScenarioRoot(full);
    if (node.suppress_expander) node.expanded = true;
    prepared.tree.nodes.push_back(std::move(node));
    prepared.objects.emplace(id, FileNameObj(*this, full));
    if (!current()) return 0;
    if (!selected.empty() && sameDisplayedPath(full, selected,
                                      comm_.services.filesystem.get())) {
        prepared.tree.selection = {id};
    }

    std::vector<String> children;
    std::error_code error;
    for (std::filesystem::directory_iterator it(filePath(full), error), end;
         !error && it != end; it.increment(error)) {
        const auto entry = *it;
        std::error_code child_error;
        const bool directory = entry.is_directory(child_error);
        if (child_error) throw std::filesystem::filesystem_error(
            "Read editor directory entry", entry.path(), child_error);
        if (!directory) continue;
        const auto child = absolutePath(pathString(entry.path()));
        // Dは全階層でシステムファイルを除外する。スキンや無視設定の判定は
        // 、各下位フォルダーではなくシナリオルートからの相対位置で行う。
        if (summ_->isSystemFile(pathString(entry.path().filename()), true)) continue;
        if (isScenarioRoot(full) ? isDef(child, true) : isIgnore(child)) continue;
        children.push_back(child);
    }
    if (error) throw std::filesystem::filesystem_error("Read editor directories", filePath(full), error);
    std::stable_sort(children.begin(), children.end(),
                     [this](const String& left, const String& right) {
                         return compDirName(baseName(left), baseName(right));
                     });
    for (const auto& child : children) {
        (void)appendDirectory(prepared, id, child, selected, expanded, identities, current);
        if (!current()) return 0;
    }
    return id;
}

bool DirectoryWindow::addp(TreeItemId parent, String path, String selected,
                           String top, TreeItemId& topItem) {
    const auto keep_alive = weak_from_this().lock();
    std::scoped_lock lock(mutex_);
    if (!dirs_ || !summ_) return false;
    const auto tree = dirs_;
    const auto summary = summ_;
    const auto revision = ++file_refresh_revision_;
    const auto old_selection = tree->selection;
    const auto old_nodes = tree->nodes;
    const auto old_node_revision = tree->node_order_revision;
    const auto old_anchor = tree->selection_anchor;
    const auto old_cursor = tree->selection_cursor;
    const auto old_top = tree->top_index;
    const auto old_horizontal = tree->horizontal_offset;
    const auto current = [&] {
        return revision == file_refresh_revision_ && dirs_ == tree && summ_ == summary &&
            tree->node_order_revision == old_node_revision && tree->nodes == old_nodes &&
            tree->selection == old_selection && tree->selection_anchor == old_anchor &&
            tree->selection_cursor == old_cursor && tree->top_index == old_top &&
            tree->horizontal_offset == old_horizontal;
    };
    PreparedDirectories prepared;
    prepared.next_id = next_tree_id_;
    prepared.tree.nodes = tree->nodes;
    prepared.tree.selection = tree->selection;
    prepared.objects = directory_objects_;
    std::unordered_map<String, bool> expanded;
    for (const auto& node : dirs_->nodes) expanded[node.value] = node.expanded;
    (void)appendDirectory(prepared, parent, path, selected, expanded, {}, current);
    if (!current()) return false;
    const bool selected_found = std::ranges::any_of(prepared.tree.nodes,
        [&](const auto& node) { return sameDisplayedPath(
            node.value, selected, comm_.services.filesystem.get()); });
    auto prepared_top = topItem;
    if (!top.empty()) {
        for (const auto& node : prepared.tree.nodes) {
            if (sameDisplayedPath(node.value, top, comm_.services.filesystem.get())) {
                prepared_top = node.id;
                break;
            }
        }
    }
    const auto reveal = selected_found && !prepared.tree.selection.empty()
        ? std::optional<TreeItemId>{prepared.tree.selection.front()} : std::nullopt;
    if (!lib::replace_tree_nodes(tree, std::move(prepared.tree.nodes),
                                std::move(prepared.tree.selection), reveal, current)) return false;
    directory_objects_.swap(prepared.objects);
    next_tree_id_ = prepared.next_id;
    topItem = prepared_top;
    return selected_found;
}

void DirectoryWindow::refreshDirs(String selected) {
    const auto keep_alive = weak_from_this().lock();
    std::scoped_lock lock(mutex_);
    if (!dirs_ || !files_) return;
    const auto tree = dirs_;
    const auto table = files_;
    const auto summary = summ_;
    const auto revision = ++file_refresh_revision_;
    const auto row_revision = table->row_order_revision;
    const auto old_tree_selection = tree->selection;
    const auto old_nodes = tree->nodes;
    const auto old_node_revision = tree->node_order_revision;
    const auto old_anchor = tree->selection_anchor;
    const auto old_cursor = tree->selection_cursor;
    const auto old_top = tree->top_index;
    const auto old_horizontal = tree->horizontal_offset;
    const auto old_file_selection = table->selection;
    const auto old_directory = selDirPath();
    if (selected.empty()) selected = old_directory;
    else selected = absolutePath(selected);
    const auto selected_files = selFiles();
    const auto tree_current = [&] {
        return revision == file_refresh_revision_ && dirs_ == tree && files_ == table &&
            summ_ == summary && tree->node_order_revision == old_node_revision &&
            tree->selection == old_tree_selection &&
            tree->selection_anchor == old_anchor && tree->selection_cursor == old_cursor &&
            tree->top_index == old_top && tree->horizontal_offset == old_horizontal &&
            tree->nodes == old_nodes;
    };
    const auto current = [&] {
        // 複合準備処理が最後の表のリビジョン変更を管理する。確定までは
        // 、外部からのツリー・表・対象モデルの変更をすべて拒否する。
        return tree_current() && table->row_order_revision == row_revision &&
            table->selection == old_file_selection;
    };
    // ファイルシステムの巡回や画像・素材プロバイダーの呼び出し中に、どち
    // らの使用中ペインも空にしない。準備失敗や失効時は両方を維持する。
    PreparedDirectories prepared;
    prepared.next_id = next_tree_id_;
    std::unordered_map<String, bool> expanded;
    std::unordered_map<String, TreeItemId> identities;
    for (const auto& node : tree->nodes) {
        expanded[node.value] = node.expanded;
        identities[node.value] = node.id;
    }
    if (summary) {
        (void)appendDirectory(prepared, 0, absolutePath(summary->scenarioPath()), selected,
                              expanded, identities, current);
        if (!current()) return;
    }
    auto& next = prepared.tree;
    if (next.selection.empty() && !next.nodes.empty()) next.selection = {next.nodes.front().id};
    String directory;
    if (!next.selection.empty()) {
        auto node = std::ranges::find(next.nodes, next.selection.front(), &lib::TreeNode::id);
        directory = node->value;
    }
    const auto reveal = next.selection.empty() ? std::optional<TreeItemId>{} : next.selection.front();
    auto tree_update = lib::prepare_tree_nodes(tree, std::move(next.nodes),
        std::move(next.selection), reveal, tree_current);
    if (!tree_update.current()) return;
    auto files = prepareFiles(directory, selected_files, current);
    if (!files) return;
    const auto publication_current = [&] { return tree_current() && tree_update.current(); };
    // どちらかを表示する前に、両部品の座標計算と確保を終え
    // る。表は最後の計測後にツリーのトークンを検査し、表
    // の公開とツリーの確定の間にプロバイダーを呼ばない。
    if (!publishFiles(*files, publication_current)) return;
    // 表自身の公開で行リビジョンが進むため、準備済み
    // ツリーは公開前の消費済みの表トークンではなく、
    // ツリー・対象モデルのトークンに対して確定する。
    if (!tree_update.commit()) return;
    directory_objects_.swap(prepared.objects);
    next_tree_id_ = prepared.next_id;
    finishFilePublication(revision);
}

void DirectoryWindow::refreshFiles(const std::vector<String>& selections) {
    refreshFilesImpl(selections, true);
}

void DirectoryWindow::refreshFilesImpl(const std::vector<String>& selections,
                                       const bool publish_status) {
    const auto keep_alive = weak_from_this().lock();
    std::scoped_lock lock(mutex_);
    if (!files_) return;
    const auto revision = ++file_refresh_revision_;
    const auto table = files_;
    const auto summary = summ_;
    const auto directory = selDirPath();
    const auto row_revision = table->row_order_revision;
    const auto old_selection = table->selection;
    const auto current = [&] {
        return revision == file_refresh_revision_ && files_ == table && summ_ == summary &&
            table->row_order_revision == row_revision && table->selection == old_selection &&
            directory == selDirPath();
    };
    auto prepared = prepareFiles(directory, selections, current);
    if (!prepared || !current()) return;
    if(!publishFiles(*prepared, current)) return;
    finishFilePublication(revision, publish_status);
}

std::optional<DirectoryWindow::PreparedFiles> DirectoryWindow::prepareFiles(
    const String& directory, const std::vector<String>& selections,
    const std::function<bool()>& current) {
    const auto summary = summ_;
    PreparedFiles prepared;
    prepared.reset_top = selections.empty();
    std::unordered_set<String> wanted;
    for (const auto& value : selections)
        if (!value.empty()) wanted.insert(lowerAscii(absolutePath(value)));
    auto& has_file = prepared.has_file;
    auto& objects = prepared.objects;
    std::error_code error;
    if (summary && !directory.empty()) {
        for (std::filesystem::directory_iterator it(filePath(directory), error), end;
             !error && it != end; it.increment(error)) {
            const auto entry = *it;
            const auto full = absolutePath(pathString(entry.path()));
            const bool directory_entry = entry.is_directory(error);
            if (error) {
                throw std::filesystem::filesystem_error("Read editor file entry", entry.path(), error);
            }
            const bool system_file = summary->isSystemFile(pathString(entry.path().filename()),
                                                         directory_entry);
            if (system_file) continue;
            has_file = true;
            const auto name = baseName(full);
            if (inc_search_ && !inc_search_->match(name)) continue;
            FileNameObj object(*this, full);
            if (!current()) return std::nullopt;
            if (isScenarioRoot(directory)) {
                if (isDef(full, object.dir)) continue;
            } else if (isIgnore(full)) {
                continue;
            }
            objects.push_back(std::move(object));
        }
        if (error) throw std::filesystem::filesystem_error("Read editor directory", filePath(directory), error);
    }
    if (!current()) return std::nullopt;
    const auto sorter = [this](const FileNameObj& left, const FileNameObj& right) {
        switch (sort_column_) {
        case SortColumn::Extension:
            return sort_direction_ == SortDirection::Up ? compFExt(left, right)
                                                         : revCompFExt(left, right);
        case SortColumn::Count:
            return sort_direction_ == SortDirection::Up ? compFCount(left, right)
                                                         : revCompFCount(left, right);
        case SortColumn::Name:
        default:
            return sort_direction_ == SortDirection::Up ? compFName(left, right)
                                                         : revCompFName(left, right);
        }
    };
    std::stable_sort(objects.begin(), objects.end(), sorter);

    auto& rows = prepared.rows;
    auto& states = prepared.states;
    auto& selected = prepared.selection;
    rows.reserve(objects.size());
    states.reserve(objects.size());
    for (const auto& object : objects) {
        lib::TableRow row;
        const auto icon = fimage(object.array, object.dir);
        if (!current()) return std::nullopt;
        const auto shown_name = object.dir ? object.basename : stripExtension(object.basename);
        String shown_ext = object.dir ? String{} : object.ext;
        if (!shown_ext.empty() && shown_ext.front() == '.') shown_ext.erase(0, 1);
        String count;
        if (object.material) count = std::to_string(useCount(object));
        row.cells.push_back({shown_name, object.array, icon});
        row.cells.push_back({shown_ext, object.array, 0});
        row.cells.push_back({count, object.array, 0});
        // DのgetWarningsは拡張子検査に完全パス、Shift-JIS
        // 検査にbasenameを使う。同じ不変の行とともに診断を公開する。
        if (prop_.warnings) row.warnings = prop_.warnings(object.array);
        if (!current()) return std::nullopt;
        if (!row.warnings.empty()) row.warning_icon = prop_.images.warning;
        if (wanted.contains(lowerAscii(object.array))) selected.push_back(rows.size());
        rows.push_back(std::move(row));
        customtable::TableRowState state;
        state.data = object;
        states.push_back(std::move(state));
    }
    if (!current()) return std::nullopt;
    return prepared;
}

bool DirectoryWindow::publishFiles(PreparedFiles& prepared, const std::function<bool()>& current) {
    const auto table = files_;
    const auto reveal=prepared.selection.empty()?std::optional<std::size_t>{}:prepared.selection.front();
    if(!table->replaceRows(std::move(prepared.rows), std::move(prepared.selection),
                           std::move(prepared.states), reveal, current,
                           prepared.reset_top)) return false;
    file_objects_.swap(prepared.objects);
    has_file_ = prepared.has_file;
    return true;
}

void DirectoryWindow::finishFilePublication(std::uint64_t revision, const bool publish_status) {
    const auto table = files_;
    closePreview();
    if (revision != file_refresh_revision_) return;
    table->redraw();
    if (publish_status) refreshStatusLine();
}

void DirectoryWindow::updateIcons() {
    updateIconsImpl(true);
}

void DirectoryWindow::updateIconsImpl(const bool update_tree_icons) {
    const auto keep_alive = weak_from_this().lock();
    std::scoped_lock lock(mutex_);
    if (files_) {
        const auto table = files_;
        const auto revision = ++file_refresh_revision_;
        const auto row_revision = table->row_order_revision;
        // アイコンプロバイダーを呼ぶ前に行データを所有する。同じ表示を
        // 更新・整列して現在の付随データ領域を置換する場合があるため。
        const auto states = table->rowStates();
        std::vector<customtable::TableCellDisplayUpdate> updates;
        updates.reserve(states.size());
        for (std::size_t index = 0; index < states.size(); ++index) {
            const auto object = std::any_cast<FileNameObj>(&states[index].data);
            if (!object) continue;
            const auto image = fimage(object->array, object->dir);
            if (revision != file_refresh_revision_ || files_ != table ||
                table->row_order_revision != row_revision) return;
            if (!table->rows[index].cells.empty())
                updates.push_back({index, 0, std::nullopt, image});
        }
        if (!updates.empty() && table->updateCellDisplays(updates, row_revision)) table->redraw();
    }
    // Dは選択資源の切り取り時に、ツリーとファイルのアイコンを一緒に更新する。
    if (update_tree_icons && dirs_) {
        bool changed = false;
        for (auto& node : dirs_->nodes) {
            const auto found = directory_objects_.find(node.id);
            if (found == directory_objects_.end()) continue;
            const auto image = isCutted(found->second.array)
                ? prop_.images.skeletonFolder : prop_.images.folder;
            if (node.icon == image) continue;
            node.icon = image;
            changed = true;
        }
        if (changed) {
            ++dirs_->node_update_revision;
            if (comm_.services.gui) comm_.services.gui->request_redraw(dirs_);
        }
    }
}

ResourceId DirectoryWindow::classifyImage(StringView file, bool directory) const {
    if (directory) return prop_.images.folder;
    const auto classify = prop_.classifyImage;
    if (classify) return classify(file, false);
    if (isCard(file)) return prop_.images.cards;
    if (prop_.isBackgroundImage && prop_.isBackgroundImage(file)) return prop_.images.backs;
    if (prop_.isBGM && prop_.isBGM(file) && lowerAscii(extension(file)) != ".wav")
        return prop_.images.bgm;
    if (prop_.isSE && prop_.isSE(file)) return prop_.images.se;
    const auto ext = lowerAscii(extension(file));
    for (const auto& plain : prop_.plainTextFileExtensions)
        if (ext == lowerAscii(plain)) return prop_.images.text;
    if (ext == ".txt" || ext == ".ini" || ext == ".sli") return prop_.images.text;
    if (isImageExtension(file)) return prop_.images.backs;
    return prop_.images.unknown;
}

ResourceId DirectoryWindow::fimage(ResourceId image) const {
    if (image == prop_.images.folder || image == prop_.images.skeletonFolder)
        return prop_.images.folder;
    if (image == prop_.images.cards || image == prop_.images.skeletonCards)
        return prop_.images.cards;
    if (image == prop_.images.backs || image == prop_.images.skeletonBacks)
        return prop_.images.backs;
    if (image == prop_.images.bgm || image == prop_.images.skeletonBgm)
        return prop_.images.bgm;
    if (image == prop_.images.se || image == prop_.images.skeletonSe)
        return prop_.images.se;
    if (image == prop_.images.text || image == prop_.images.skeletonText)
        return prop_.images.text;
    if (image == prop_.images.unknown || image == prop_.images.skeletonUnknown)
        return prop_.images.unknown;
    return image;
}

ResourceId DirectoryWindow::sfimage(ResourceId image) const {
    if (image == prop_.images.folder || image == prop_.images.skeletonFolder)
        return prop_.images.skeletonFolder;
    if (image == prop_.images.cards || image == prop_.images.skeletonCards)
        return prop_.images.skeletonCards;
    if (image == prop_.images.backs || image == prop_.images.skeletonBacks)
        return prop_.images.skeletonBacks;
    if (image == prop_.images.bgm || image == prop_.images.skeletonBgm)
        return prop_.images.skeletonBgm;
    if (image == prop_.images.se || image == prop_.images.skeletonSe)
        return prop_.images.skeletonSe;
    if (image == prop_.images.text || image == prop_.images.skeletonText)
        return prop_.images.skeletonText;
    if (image == prop_.images.unknown || image == prop_.images.skeletonUnknown)
        return prop_.images.skeletonUnknown;
    return image;
}

ResourceId DirectoryWindow::fimage(StringView file) const {
    if (isDirectory(filePath(file))) return prop_.images.folder;
    return fimage(classifyImage(file, false));
}

ResourceId DirectoryWindow::sfimage(StringView file) const {
    if (isDirectory(filePath(file))) return prop_.images.skeletonFolder;
    return sfimage(classifyImage(file, false));
}

ResourceId DirectoryWindow::fimage(StringView file, bool directory) const {
    if (isCutted(file)) {
        if (directory) return prop_.images.skeletonFolder;
        return sfimage(classifyImage(file, false));
    }
    return classifyImage(file, directory);
}

bool DirectoryWindow::isCard(StringView file) const {
    const auto is_card = prop_.isCard;
    const bool scaled = summ_ && summ_->loadScaledImage();
    if (is_card) {
        if (is_card(file)) return true;
        if (scaled) {
            const auto unscaled = pathString(
                ::cwxeditor::cwx::utils::noScaledPath(filePath(file)));
            return !unscaled.empty() && is_card(unscaled);
        }
        return false;
    }
    const auto ext = lowerAscii(extension(file));
    return ext == ".card" || ext == ".cwxcard";
}

std::vector<String> DirectoryWindow::getWarnings(std::size_t row) const {
    const auto* value = rowObject(row);
    if (!value || !prop_.warnings) return {};
    return prop_.warnings(value->array);
}

std::vector<String> DirectoryWindow::getWarnings(const lib::TableRow& row) const {
    if (!prop_.warnings || row.cells.empty()) return {};
    return prop_.warnings(row.cells.front().value);
}

void DirectoryWindow::filesSelected() {
    const auto keep_alive = weak_from_this().lock();
    const std::weak_ptr<CallbackLifetime> lifetime = callback_lifetime_;
    setFocus(Focus::Files);
    refreshStatusLine();
    if (lifetime.expired()) return;
    notifyToolbar();
}

String DirectoryWindow::selDirPath() const {
    std::scoped_lock lock(mutex_);
    if (!dirs_ || dirs_->selection.empty()) return {};
    const auto* node = findNode(dirs_->selection.front());
    return node ? node->value : String{};
}

std::vector<String> DirectoryWindow::selFiles() const {
    std::scoped_lock lock(mutex_);
    std::vector<String> result;
    if (!files_) return result;
    result.reserve(files_->selection.size());
    const auto& states = files_->rowStates();
    for (const auto index : files_->selection) {
        if (index >= states.size()) continue;
        if (const auto value = std::any_cast<FileNameObj>(&states[index].data))
            result.push_back(value->array);
    }
    return result;
}

bool DirectoryWindow::selectedDirectoryIsRoot() const {
    const auto directory = selDirPath();
    return directory.empty() || isScenarioRoot(directory);
}

bool DirectoryWindow::copyTree(const Path& source, const Path& target,
                               std::error_code& error) const {
    error.clear();
    if (std::filesystem::is_directory(source, error)) {
        if (isSameOrBelow(pathString(source), pathString(target),
                          comm_.services.filesystem.get())) {
            error = std::make_error_code(std::errc::invalid_argument);
            return false;
        }
        std::filesystem::create_directories(target, error);
        if (error) return false;
        for (std::filesystem::directory_iterator it(source, error), end;
             !error && it != end; it.increment(error)) {
            const auto child_source = it->path();
            const auto child_target = target / child_source.filename();
            if (!copyTree(child_source, child_target, error)) return false;
        }
        return !error;
    }
    std::filesystem::create_directories(target.parent_path(), error);
    if (error) return false;
    std::filesystem::copy_file(source, target,
                                std::filesystem::copy_options::overwrite_existing, error);
    return !error;
}

bool DirectoryWindow::moveTree(const Path& source, const Path& target,
                               std::error_code& error) const {
    error.clear();
    if (std::filesystem::is_directory(source, error) &&
        isSameOrBelow(pathString(source), pathString(target),
                      comm_.services.filesystem.get())) {
        error = std::make_error_code(std::errc::invalid_argument);
        return false;
    }
    error.clear();
    std::filesystem::create_directories(target.parent_path(), error);
    if (error) return false;
    std::filesystem::rename(source, target, error);
    if (!error) return true;
    error.clear();
    if (!copyTree(source, target, error)) return false;
    std::filesystem::remove_all(source, error);
    return !error;
}

String DirectoryWindow::uniqueTarget(const Path& parent, String name,
                                     bool directory) const {
    // Dの空白を入れない番号付けと拡大画像の接尾辞規則を共有する。絶対
    // の拡大画像パスでは、コピーが入力元と同じ場所に残るよう、C++の
    // 共通処理はDの完全パス名ではなくファイル名を対象に番号を付ける。
    return pathString(utils::createNewFileName(parent / filePath(name), directory));
}

bool DirectoryWindow::pasteImpl(StringView target, const std::vector<String>& files,
                                bool move, bool& fromOut) {
    std::scoped_lock lock(mutex_);
    const auto keep_alive = weak_from_this().lock();
    const std::weak_ptr<CallbackLifetime> lifetime = callback_lifetime_;
    const auto summary = summ_;
    fromOut = false;
    if (!summary || target.empty() || files.empty() || lifetime.expired()) return false;
    const auto target_path = filePath(absolutePath(target));
    if (!isDirectory(target_path)) return false;
    const auto scenario_root = summary->scenarioPath();
    const auto current = [&] {
        return !lifetime.expired() && summ_ == summary &&
               summary->scenarioPath() == scenario_root;
    };
    struct Entry { Path source; bool duplicate; bool external; };
    std::vector<Entry> entries;
    std::size_t conflicts = 0;
    String first_conflict;
    const bool top = isScenarioRoot(pathString(target_path));
    // Dは選択全体を事前検査してから一度だけ確認する。特に取消時に、
    // 先行する非衝突ファイルだけがコピー済みにならないようにする。
    for (const auto& text : files) {
        // 意図的な追加機能: copyFilePath()のテキスト一覧はシナリオ相対パス。
        // 貼り付けとドロップの共有経路で、作業ディレクトリではなくシナリオを基準にする。
        // FileTransfer等の絶対パスはそのまま使用する。
        auto source_path = filePath(text);
        if (source_path.is_relative()) source_path = filePath(scenario_root) / source_path;
        const auto source = filePath(absolutePath(pathString(source_path)));
        if (!std::filesystem::exists(source)) continue;
        const bool directory = isDirectory(source);
        if (directory && isSameOrBelow(pathString(source), pathString(target_path),
                                      comm_.services.filesystem.get())) continue;
        const auto destination = target_path / source.filename();
        if (top ? isDef(pathString(destination), directory)
                : isIgnore(pathString(destination))) continue;
        const bool cut = isCutted(pathString(source));
        if (samePath(pathString(source.parent_path()), pathString(target_path),
                     comm_.services.filesystem.get())) {
            if (!move && !cut) entries.push_back({source, true, true});
            continue;
        }
        if (std::filesystem::exists(destination) && isDirectory(destination) == directory) {
            if (!conflicts) first_conflict = pathString(source);
            ++conflicts;
        }
        // Dの切り取りは常に移動。ドラッグは切り取りなしでも内部の転送元を移動する。
        const bool external = !cut && (!move || !isSameOrBelow(
            scenario_root, pathString(source), comm_.services.filesystem.get()));
        entries.push_back({source, false, external});
    }
    if (entries.empty()) return false;
    bool overwrite = false;
    if (conflicts) {
        const auto dialogs = comm_.services.dialogs;
        if (!dialogs) return false;
        String title = "確認";
        String message = conflicts == 1
            ? first_conflict + " を上書きしますか?"
            : std::to_string(conflicts) + " 件の同名ファイルまたはフォルダーを上書きしますか?";
        if (prop_.editorProps) {
            const auto& messages = prop_.editorProps->requiredMsgs();
            title = messages.dlgTitQuestion.value;
            message = conflicts == 1
                ? ::cwxeditor::cwx::utils::tryFormat(messages.dlgMsgDropOverWriteFile.value, first_conflict)
                : ::cwxeditor::cwx::utils::tryFormat(messages.dlgMsgDropOverWriteFiles.value, conflicts);
        }
        const auto answer = dialogs->message({
            title, message, {}, lib::MessageSeverity::Question, lib::MessageButtons::YesNoCancel});
        if (!current() || answer == lib::MessageResult::Cancel ||
            answer == lib::MessageResult::None) return false;
        overwrite = answer == lib::MessageResult::Yes || answer == lib::MessageResult::OK;
    }
    std::stable_sort(entries.begin(), entries.end(), [&](const Entry& a, const Entry& b) {
        if (a.duplicate != b.duplicate) return !a.duplicate;
        if (a.duplicate) return false; // Dは同じ親へのコピーを選択順に保つ。
        return compFullPath(pathString(a.source), pathString(b.source));
    });
    pauseTrace();
    struct Resume { std::function<void()> fn; ~Resume() noexcept { try { fn(); } catch (...) {} } };
    Resume resume{[&] { if (!lifetime.expired()) resumeTrace(); }};
    bool changed = false;
    std::size_t transfer_count = 0;
    std::vector<String> selected;
    std::function<bool(const Path&, const Path&, bool)> transfer;
    transfer = [&](const Path& source, const Path& destination, bool external) {
        if (!current()) return false;
        const bool directory = isDirectory(source);
        if (std::filesystem::exists(destination) &&
            isDirectory(destination) == directory && !overwrite) return true;
        std::error_code error;
        if (directory) {
            std::filesystem::create_directories(destination, error);
            if (error) return false;
            std::vector<Path> children;
            for (std::filesystem::directory_iterator it(source, error), end;
                 !error && it != end; it.increment(error)) children.push_back(it->path());
            if (error) return false;
            for (const auto& child : children)
                if (!transfer(child, destination / child.filename(), external)) return false;
            if (!external) {
                // 全子の転送に成功した後でだけ、空になった入力元を削除する。
                std::filesystem::remove(source, error);
                if (error) return false;
            }
        } else if (!(external ? copyTree(source, destination, error)
                              : moveTree(source, destination, error))) return false;
        changed = true;
        ++transfer_count;
        if (!external) {
            updatePathTree(pathString(source), pathString(destination), directory);
            if (!directory) summary->renameFile(pathString(source), pathString(destination));
            if (prop_.refPath)
                prop_.refPath(toRelPath(pathString(source)), toRelPath(pathString(destination)), directory);
        }
        return current();
    };
    for (const auto& entry : entries) {
        if (!current()) return false;
        auto destination = target_path / entry.source.filename();
        const auto previous_count = transfer_count;
        bool success;
        if (entry.duplicate) {
            destination = filePath(uniqueTarget(target_path, pathString(entry.source.filename()),
                                                isDirectory(entry.source)));
            std::error_code error;
            success = copyTree(entry.source, destination, error);
            changed |= success;
            if (success) ++transfer_count;
        } else {
            fromOut |= entry.external;
            success = transfer(entry.source, destination, entry.external);
        }
        if (!success) return false;
        if (transfer_count != previous_count) selected.push_back(pathString(destination));
    }
    if (!current()) return false;
    if (changed) summary->changed();
    synchronizeJpyFiles(true);
    if (!current()) return false;
    if (prop_.refPaths) prop_.refPaths(toRelPath(pathString(target_path)));
    if (!current()) return false;
    clearCut();
    refreshDirs(pathString(target_path));
    if (!current()) return false;
    refreshFiles(selected);
    if (!current()) return false;
    if (comm_.refUseCount) comm_.refUseCount();
    if (current()) notifyToolbar();
    // Dは既存項目をすべて飛ばした場合も、「いいえ」の回答を受け付ける。
    return true;
}

void DirectoryWindow::pasteImpl(const std::vector<String>& files) {
    if (!canDoTCPD() || files.empty()) return;
    const auto keep_alive = weak_from_this().lock();
    const std::weak_ptr<CallbackLifetime> lifetime = callback_lifetime_;
    const auto table = files_;
    const auto summary = summ_;
    bool from_out{};
    // 共通の転送処理はパスごとに切り取りの印を確認する。ク
    // リップボード経路はDの既定のコピーに保ち、一つの切
    // り取りパスによって他の全選択パスを移動に変えない。
    if (!pasteImpl(selDirPath(), files, false, from_out) || lifetime.expired() ||
        files_ != table || summ_ != summary || !table)
        return;
    // Dはこの処理の成功後にFilesコントロールへ強制フォーカスす
    // る。共通ホストではGUIバックエンドを通じて同じ遷移を行う。
    if (comm_.services.gui) comm_.services.gui->focus(table);
}

std::vector<String> DirectoryWindow::copyImpl() const {
    if (last_focus_ == Focus::Directories) {
        const auto directory = selDirPath();
        return directory.empty() ? std::vector<String>{} : std::vector<String>{directory};
    }
    return selFiles();
}

bool DirectoryWindow::copy2() {
    clearCut();
    const auto values = copyImpl();
    if (values.empty()) return false;
    const auto clipboard = comm_.clipboardBackend();
    if (!clipboard) return false;
    ClipboardData data;
    for (const auto& value : values) data.files.emplace_back(value);
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0) data.text.push_back('\n');
        data.text += values[index];
    }
    clipboard->write(data);
    notifyToolbar();
    return true;
}

void DirectoryWindow::copyFilePath() {
    // 意図的な追加機能: D版の1件コピーを拡張し、選択中の全パスを改行区切りでコピーする。
    // paste()/canDoP() と組み合わせ、テキストのパス一覧を貼り付け可能にする。
    if (!summ_ || selFiles().empty()) return;
    const auto clipboard = comm_.clipboardBackend();
    if (!clipboard) return;
    ClipboardData data;
    const auto values = selFiles();
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0) data.text.push_back('\n');
        data.text += ::cwxeditor::cwx::utils::encodePath(toRelPath(values[index]));
    }
    clipboard->write(data);
    notifyToolbar();
}

void DirectoryWindow::cut() {
    if (!canDoT() || !copy2()) return;
    const auto values = copyImpl();
    for (const auto& value : values) cuts_.insert(absolutePath(value));
    updateIcons();
    notifyToolbar();
}

void DirectoryWindow::copy() {
    if (canDoC()) (void)copy2();
}

void DirectoryWindow::paste() {
    if (!canDoP()) return;
    const auto keep_alive = weak_from_this().lock();
    const std::weak_ptr<CallbackLifetime> lifetime = callback_lifetime_;
    const auto table = files_;
    const auto summary = summ_;
    const auto clipboard = comm_.clipboardBackend();
    if (!clipboard) return;
    const auto data = clipboard->read();
    std::vector<String> values;
    for (const auto& value : data.files) values.push_back(pathString(value));
    // 意図的な追加機能: D版のFileTransferに加え、copyFilePath()等が作る
    // 改行区切りテキストのパス一覧も貼り付け対象として受け付ける。
    if (values.empty()) values = splitLines(data.text);
    if (lifetime.expired() || files_ != table || summ_ != summary || !table) return;
    pasteImpl(values);
}

void DirectoryWindow::removeFiles(const std::vector<String>& values, bool recycle) {
    const auto keep_alive = weak_from_this().lock();
    const std::weak_ptr<CallbackLifetime> lifetime = callback_lifetime_;
    const auto original_summary = summ_;
    const String original_path = original_summary
        ? original_summary->scenarioPath() : String{};
    const auto current = [&] {
        return !lifetime.expired() && summ_ == original_summary &&
            (!original_summary || original_summary->scenarioPath() == original_path);
    };
    if (!current()) return;
    ScopeExit resumeTraceOnExit([this, lifetime] {
        if (!lifetime.expired()) resumeTrace();
    });
    // pauseTraceはFileWatchHandleを停止し、同期的にこのホストを終了させる場合がある。
    pauseTrace();
    if (!current()) return;
    String failure;
    const auto filesystem = comm_.services.filesystem;
    if (recycle && filesystem) {
        std::vector<Path> paths;
        paths.reserve(values.size());
        for (const auto& value : values)
            paths.push_back(filePath(absolutePath(value)));
        std::string error;
        if (!current()) return;
        if (!filesystem->move_to_trash(paths, error)) failure = error;
        if (!current()) return;
    } else {
        for (const auto& value : values) {
            if (!current()) return;
            std::error_code error;
            std::filesystem::remove_all(filePath(absolutePath(value)), error);
            if (!current()) return;
            if (error && failure.empty()) failure = error.message();
        }
    }
    resumeTraceOnExit.release();
    resumeTrace();
    if (!current()) return;
    const auto statusMessage = prop_.statusMessage;
    if (!failure.empty() && statusMessage) statusMessage(failure);
}

void DirectoryWindow::del() {
    del(prop_.recycleDelete);
}

void DirectoryWindow::del(lib::Modifiers modifiers) {
    const bool shift = (static_cast<std::uint32_t>(modifiers) &
                        static_cast<std::uint32_t>(lib::Modifiers::Shift)) != 0U;
    del(!shift);
}

void DirectoryWindow::del(const bool recycle) {
    if (!canDoD()) return;
    const auto keep_alive = weak_from_this().lock();
    const std::weak_ptr<CallbackLifetime> lifetime = callback_lifetime_;
    const auto original_summary = summ_;
    const String original_path = original_summary
        ? original_summary->scenarioPath() : String{};
    const auto current = [&] {
        return !lifetime.expired() && summ_ == original_summary &&
            (!original_summary || original_summary->scenarioPath() == original_path);
    };
    if (!current()) return;
    std::vector<String> values;
    if (last_focus_ == Focus::Directories) {
        if (selectedDirectoryIsRoot()) return;
        values.push_back(selDirPath());
    } else {
        values = selFiles();
    }
    if (values.empty()) return;
    const DestructiveFilePlan plan{values, recycle};
    bool confirmed = false;
    const auto confirmOverwrite = prop_.confirmOverwrite;
    const auto dialogs = comm_.services.dialogs;
    if (confirmOverwrite) {
        const auto decision = confirmOverwrite(plan);
        if (!current()) return;
        confirmed = decision == FileConflictDecision::Yes;
    } else if (dialogs) {
        String title = "確認";
        String message;
        if (prop_.editorProps) {
            const auto& messages = prop_.editorProps->requiredMsgs();
            title = messages.dlgTitQuestion.value;
            if (values.size() == 1U) {
                message = ::cwxeditor::cwx::utils::tryFormat(
                    recycle ? messages.dlgMsgDeleteFileRecycle.value
                            : messages.dlgMsgDeleteFile.value,
                    values.front());
            } else {
                message = ::cwxeditor::cwx::utils::tryFormat(
                    recycle ? messages.dlgMsgDeleteFilesRecycle.value
                            : messages.dlgMsgDeleteFiles.value,
                    values.size());
            }
        } else {
            message = values.size() == 1U
                ? "選択した項目を削除しますか？"
                : std::to_string(values.size()) + "個の項目を削除しますか？";
        }
        const auto result = dialogs->message({
            title, message, {}, lib::MessageSeverity::Question,
            lib::MessageButtons::OKCancel});
        if (!current()) return;
        confirmed = result == lib::MessageResult::OK ||
                    result == lib::MessageResult::Yes;
    }
    // 確認サービスがないことを、破壊的な操作への同意として扱わない。
    if (!confirmed || !current()) return;
    removeFiles(plan.candidates, plan.recycle);
    if (!current()) return;
    clearCut();
    if (!current()) return;
    const auto directory = selDirPath();
    if (!current()) return;
    refreshDirs(directory);
    if (!current()) return;
    refreshFiles();
    if (!current()) return;
    const auto deletePathReferences = prop_.deletePathReferences;
    if (deletePathReferences) {
        deletePathReferences();
        if (!current()) return;
    }
    if (summ_ && summ_->useTemp()) {
        summ_->changed();
        if (!current()) return;
    }
    const auto refUseCount = comm_.refUseCount;
    if (refUseCount) {
        refUseCount();
        if (!current()) return;
    }
    notifyToolbar();
}
void DirectoryWindow::clone() {
    if (!canDoClone()) return;
    auto values = copyImpl();
    if (values.empty()) return;
    auto target = selDirPath();
    if (last_focus_ == Focus::Directories && !selectedDirectoryIsRoot()) {
        const auto parent = filePath(target).parent_path();
        target = pathString(parent);
    }
    bool from_out{};
    (void)pasteImpl(target, values, false, from_out);
}

bool DirectoryWindow::canDoTCPD() const {
    return last_focus_ != Focus::None;
}

bool DirectoryWindow::canDoT() const {
    if (!canDoTCPD()) return false;
    if (last_focus_ == Focus::Directories) return !selectedDirectoryIsRoot();
    return !selFiles().empty();
}

bool DirectoryWindow::canDoC() const {
    if (!canDoTCPD()) return false;
    if (last_focus_ == Focus::Directories) return !selDirPath().empty();
    return !selFiles().empty();
}

bool DirectoryWindow::canDoP() const {
    if (!canDoTCPD() || !summ_) return false;
    const auto clipboard = comm_.clipboardBackend();
    if (!clipboard) return false;
    // 意図的な追加機能: D版のFileTransferだけでなく、テキストのパス一覧でも
    // 貼り付けを有効にする。実際の受け付けはpaste()の共有経路で行う。
    constexpr std::string_view requested[]{"application/x-cwxeditor-file-list", "text/plain;charset=utf-8", "text/plain"};
    const auto data = clipboard->read_formats(requested);
    return !data.files.empty() || !data.text.empty();
}

bool DirectoryWindow::canDoD() const {
    return canDoT();
}

bool DirectoryWindow::canDoClone() const {
    return canDoT();
}

void DirectoryWindow::clearCut() {
    const auto keep_alive = weak_from_this().lock();
    const std::weak_ptr<CallbackLifetime> lifetime = callback_lifetime_;
    if (lifetime.expired()) return;
    const auto tree = dirs_;
    const auto summary = summ_;
    const auto revision = file_refresh_revision_;
    const auto current = [this, tree, lifetime, revision, summary] {
        const auto owner = lifetime.lock();
        return owner && callback_lifetime_ == owner && dirs_ == tree &&
            file_refresh_revision_ == revision && summ_ == summary;
    };
    cuts_.clear();
    if (tree) {
        auto nodes = tree->nodes;
        bool changed{};
        for (auto& node : nodes) {
            if (node.icon == prop_.images.folder) continue;
            node.icon = prop_.images.folder;
            changed = true;
        }
        if (changed) {
            // 切り取りの解除は、共通Treeの一度の公開で行う。準備済み
            // フレーム、選択・表示位置操作、プロバイダーの再入から、一部
            // だけ色変更したベクターを見せたり旧世代を残したりしない。
            const auto selection = tree->selection;
            const auto gui = comm_.services.gui;
            if (lib::replace_tree_nodes(tree, std::move(nodes), selection,
                                        std::nullopt, current) && current() && gui)
                gui->request_redraw(tree);
        }
    }
    // 既存のファイルアイコン更新は維持し、Tree
    // のアイコン変更は上記の共通公開に任せる。
    if (current()) updateIconsImpl(false);
}

void DirectoryWindow::refreshStatusLine() {
    const auto keep_alive = weak_from_this().lock();
    const std::weak_ptr<CallbackLifetime> lifetime = callback_lifetime_;
    if (!files_) return;
    std::uintmax_t size = 0;
    for (const auto& value : file_objects_) size += value.size;
    status_line_ = formatStatus(file_objects_.size(), size, files_->selection.size());
    const auto text = status_line_;
    const auto window = window_;
    const auto set_status = comm_.setStatusLine;
    const auto status_message = prop_.statusMessage;
    const auto revision = file_refresh_revision_;
    if (set_status) set_status(window.get(), text);
    if (!lifetime.expired() && revision == file_refresh_revision_ && status_line_ == text && status_message)
        status_message(text);
}

String DirectoryWindow::formatStatus(std::size_t file_count, std::uintmax_t bytes,
                                     std::size_t selected) const {
    const auto size = std::to_string(bytes / 1024U) + " KB";
    String result = selected == 0 ? prop_.messages.dirStatus
                                  : prop_.messages.dirStatusSelected;
    replaceAll(result, "%1%", std::to_string(file_count));
    replaceAll(result, "%2%", size);
    replaceAll(result, "%3%", std::to_string(selected));
    return result;
}

std::function<void(StringView)> DirectoryWindow::statusText() const {
    const std::weak_ptr<CallbackLifetime> lifetime = callback_lifetime_;
    return [this, lifetime](StringView value) {
        if (lifetime.expired()) return;
        const auto keep_alive = weak_from_this().lock();
        const auto callback = prop_.statusMessage;
        if (callback) callback(value);
    };
}

void DirectoryWindow::refreshUseCount() {
    const auto keep_alive = weak_from_this().lock();
    std::scoped_lock lock(mutex_);
    if (!summ_ || !files_) return;
    const auto table = files_;
    const auto row_revision = table->row_order_revision;
    const auto& states = table->rowStates();
    std::vector<customtable::TableCellDisplayUpdate> updates;
    for (std::size_t index = 0; index < states.size(); ++index) {
        const auto object = std::any_cast<FileNameObj>(&states[index].data);
        if (!object || !object->material || table->rows[index].cells.size() < 3) continue;
        std::error_code error;
        if (!std::filesystem::exists(filePath(object->array), error) || error) continue;
        const auto count = std::to_string(useCount(*object));
        if (table->rows[index].cells[2].text != count)
            updates.push_back({index, 2, count, std::nullopt});
    }
    if (!updates.empty() && sort_column_ == SortColumn::Count) {
        // 並べ替えにも新しい件数が必要。refreshFilesが
        // 完全な新順序を準備する前に、更新の半分だけを公開しない。
        refreshFilesImpl(selFiles(), false);
        return;
    }
    if (!updates.empty() && table->updateCellDisplays(updates, row_revision)) table->redraw();
    // Dの使用数更新はセルと順序だけを変える。非アクティブなファイル
    // ペインが、フォーカス中のイベント編集部の状態欄を上書きしない。
}

void DirectoryWindow::refreshTitle() {
    if (window_) window_->accessibility_name = title();
}

void DirectoryWindow::refPaths(const void* sender, StringView parent) {
    if (sender == this) return;
    const auto selected_directory = selDirPath();
    refreshDirs(selected_directory);
    if (!selected_directory.empty() &&
        ::cwxeditor::cwx::utils::cfnmatch(toRelPath(selected_directory), parent))
        refreshFiles(selFiles());
}

void DirectoryWindow::delPaths(const void* sender) {
    if (sender == this) return;
    const auto selected_directory = selDirPath();
    const auto selected_files = selFiles();
    refreshDirs(selected_directory);
    refreshFiles(selected_files);
}

void DirectoryWindow::saveScenario() {
    if (prop_.saveScenario) prop_.saveScenario();
}

void DirectoryWindow::incSearch() {
    const auto summary = summ_;
    const String scenario_path = summary ? summary->scenarioPath() : String{};
    setFocus(Focus::Files);
    const auto files = files_;
    const auto search = inc_search_;
    const std::weak_ptr<CallbackLifetime> life = callback_lifetime_;
    if (!files || !search) return;
    const auto services = comm_.services;
    if (services.gui) dutils::forceFocus(files, true, services);
    if (life.expired() || files_ != files || inc_search_ != search || summ_ != summary ||
        (summary && summary->scenarioPath() != scenario_path)) return;
    search->startIncSearch();
}

void DirectoryWindow::selectAll() {
    if (!files_) return;
    files_->selectAll();
    filesSelected();
}

void DirectoryWindow::createFilesMenu() {
    if (!menu_) return;
    menu_->items.clear();
    menu_->items.push_back({"検索", "incremental-search", {}, 0, true});
    menu_->items.push_back({"新しいフォルダ", "new-folder", {}, 0, canCreateNewFolder()});
    menu_->items.push_back({"アーカイブ作成", "create-archive", {}, 0, true});
    // 仕様差として、単体版はData Explorerも含む全メニューで外
    // 部ツールを一時的に非表示にする。ALLは現在のメニューを維持する。
    if (lib::editor_window_host_options().all_mode) {
        for (std::size_t index = 0; index < prop_.outerTools.size(); ++index) {
            const auto& tool = prop_.outerTools[index];
            menu_->items.push_back({tool.name, "outer-tool-" + std::to_string(index), tool.hotkey,
                                    tool.icon, true});
        }
    }
    menu_->items.push_back({"切り取り", "cut", {}, 0, true});
    menu_->items.push_back({"コピー", "copy", {}, 0, true});
    menu_->items.push_back({"貼り付け", "paste", {}, 0, true});
    menu_->items.push_back({"削除", "delete", {}, 0, true});
    menu_->items.push_back({"複製", "clone", {}, 0, true});
    menu_->items.push_back({"すべて選択", "select-all", {}, 0, true});
    menu_->items.push_back({"IDを検索", "find-id", {}, 0, true});
    menu_->items.push_back({"ファイルパスをコピー", "copy-file-path", {}, 0, true});
    menu_->items.push_back({"未使用ファイルを削除", "delete-unused", {}, 0, true});
    const std::weak_ptr<CallbackLifetime> lifetime = callback_lifetime_;
    menu_->on_command = [this, lifetime](StringView command) {
        if (lifetime.expired()) return;
        const auto keep_alive = weak_from_this().lock();
        if (command == "incremental-search") incSearch();
        else if (command == "new-folder") createNewFolder();
        else if (command == "create-archive") createArchive();
        else if (command == "cut") cut();
        else if (command == "copy") copy();
        else if (command == "paste") paste();
        else if (command == "delete") del();
        else if (command == "clone") clone();
        else if (command == "select-all") selectAll();
        else if (command == "find-id") replaceID();
        else if (command == "copy-file-path") copyFilePath();
        else if (command == "delete-unused") deleteUnuse();
        else if (command.starts_with("outer-tool-")) {
            const auto number = command.substr(String("outer-tool-").size());
            try {
                const auto index = static_cast<std::size_t>(std::stoul(String(number)));
                if (index < prop_.outerTools.size()) Exec(*this, menu_, prop_.outerTools[index],
                                                          static_cast<int>(index)).run();
            } catch (...) {
            }
        }
    };
    const auto normal_command = menu_->on_command;
    menu_->on_command_with_gesture = [this, lifetime, normal_command](
        const StringView command, const lib::CommandGesture gesture) {
        if (lifetime.expired()) return;
        const auto keep_alive = weak_from_this().lock();
        if (command == "delete") del(gesture.modifiers);
        else if (command == "delete-unused") deleteUnuse(gesture.modifiers);
        else if (normal_command) normal_command(command);
    };
    menu_->on_open = [this, lifetime] {
        if (lifetime.expired() || !menu_) return;
        const auto item = std::find_if(menu_->items.begin(), menu_->items.end(),
            [](const lib::MenuItem& value) { return value.command == "new-folder"; });
        if (item == menu_->items.end()) return;
        const bool enabled = canCreateNewFolder();
        if (item->enabled == enabled) return;
        item->enabled = enabled;
        ++menu_->item_update_revision;
    };
}

String DirectoryWindow::createDir() {
    const auto parent = selDirPath();
    if (!summ_ || parent.empty()) return {};
    String name = prop_.createName ? prop_.createName(prop_.messages.newFolder)
                                   : prop_.messages.newFolder;
    if (name.empty()) name = "NewFolder";
    const auto target_name = uniqueTarget(filePath(parent), name, true);
    if (target_name.empty()) return {};
    std::error_code error;
    std::filesystem::create_directories(filePath(target_name), error);
    if (error) return {};
    summ_->changed();
    return absolutePath(target_name);
}

void DirectoryWindow::createDirDirs() {
    const std::weak_ptr<CallbackLifetime> life = callback_lifetime_;
    const auto value = createDir();
    if (value.empty()) return;
    refreshDirs(value);
    refreshFiles();
    setFocus(Focus::Directories);
    notifyToolbar();
    if (!life.expired()) beginDirectoryNameEdit(value, true);
}

void DirectoryWindow::createDirFiles() {
    const std::weak_ptr<CallbackLifetime> life = callback_lifetime_;
    const auto old = selFiles();
    const auto value = createDir();
    if (value.empty()) return;
    auto selected = old;
    selected.push_back(value);
    refreshDirs(selDirPath());
    refreshFiles(selected);
    setFocus(Focus::Files);
    notifyToolbar();
    if (!life.expired()) beginDirectoryNameEdit(value, false);
}

void DirectoryWindow::beginDirectoryNameEdit(const String& path, const bool tree) {
    const auto gui = comm_.services.gui;
    if (!gui) return;
    lib::ProgrammaticInlineEditRequest request;
    request.focus_scope = window_;
    request.row_height = lib::scaled_ui_metric(lib::kEditorUiMetrics.row_height);
    const std::weak_ptr<CallbackLifetime> life = callback_lifetime_;
    // ネイティブの区切り表記の違いで、新しく公開した編集対象を見失わないようにする。
    const auto filesystem = comm_.services.filesystem;
    if (tree) {
        request.tree = dirs_;
        request.matches_stable_tree_node = [path, life, filesystem](const lib::TreeNode& node) {
            return !life.expired() && node.parent != 0 &&
                sameDisplayedPath(node.value, path, filesystem.get());
        };
    } else {
        request.table = files_;
        request.matches_stable_row = [this, path, life, filesystem](std::size_t row) {
            if (life.expired()) return false;
            const auto* value = rowObject(row);
            return value && sameDisplayedPath(value->array, path, filesystem.get());
        };
    }
    (void)gui->request_programmatic_inline_edit(request);
}

void DirectoryWindow::replaceImpl(StringView selected) {
    if (!summ_) return;
    if (prop_.replacePath) prop_.replacePath(selected, true);
}

void DirectoryWindow::replace() {
    const auto values = selFiles();
    replaceImpl(values.empty() ? StringView{} : toRelPath(values.front()));
}

bool DirectoryWindow::canReplaceID() const {
    return !selFiles().empty();
}

void DirectoryWindow::replaceID() {
    const auto values = selFiles();
    if (values.empty()) return;
    replaceImpl(toRelPath(values.front()));
}

String DirectoryWindow::pathRename(TreeItemId item, String newName) {
    const auto* node = findNode(item);
    return node ? pathRename(node->value, std::move(newName)) : String{};
}

String DirectoryWindow::pathRename(String path, String newName) {
    if (path.empty() || newName.empty()) return {};
    clearCut();
    newName.erase(std::remove(newName.begin(), newName.end(), '/'), newName.end());
    newName.erase(std::remove(newName.begin(), newName.end(), '\\'), newName.end());
    if (newName.empty()) return {};
    const auto source = filePath(absolutePath(path));
    const bool directory = isDirectory(source);
    if (!directory && !extension(pathString(source)).empty()) newName += extension(pathString(source));
    const auto target = source.parent_path() / filePath(newName);
    if (samePath(pathString(source), pathString(target),
                 comm_.services.filesystem.get()) ||
        (std::filesystem::exists(target) && isDirectory(target) == directory)) return {};
    const auto old_path = pathString(source);
    std::error_code error;
    std::filesystem::rename(source, target, error);
    if (error) return {};
    const auto new_path = pathString(target);
    const auto keep_alive = weak_from_this().lock();
    const std::weak_ptr<CallbackLifetime> lifetime = callback_lifetime_;
    const auto summary = summ_;
    const auto current = [&] { return !lifetime.expired() && summ_ == summary; };
    // Dはディレクトリの通知前に、名前変更した各子孫を公開する。保存済
    // みパスはUseCounterに追従するが、未確定の選択部の画像も
    // 同じ実ファイルの名前変更に追従するには、このファイル通知が必要。
    std::vector<std::pair<String, String>> renamed_files;
    if (directory) {
        std::error_code walk_error;
        for (std::filesystem::recursive_directory_iterator it(target, walk_error), end;
             !walk_error && it != end; it.increment(walk_error)) {
            if (it->is_directory(walk_error)) continue;
            if (walk_error) break;
            const auto relative = it->path().lexically_relative(target);
            renamed_files.emplace_back(toRelPath(pathString(source / relative)),
                                       toRelPath(pathString(it->path())));
        }
    }
    if (summary) {
        summary->renameFile(old_path, new_path);
        summary->changed();
    }
    if (!current()) return new_path;
    updatePathTree(old_path, new_path, directory);
    if (!current()) return new_path;
    const auto notify_path = prop_.refPath;
    if (notify_path) for (const auto& [before, after] : renamed_files) {
        notify_path(before, after, false);
        if (!current()) return new_path;
    }
    synchronizeJpyFiles(false);
    if (!current()) return new_path;
    if (prop_.refPath) prop_.refPath(toRelPath(old_path), toRelPath(new_path), directory);
    if (!current()) return new_path;
    if (comm_.refUseCount) comm_.refUseCount();
    return new_path;
}

void DirectoryWindow::synchronizeJpyFiles(const bool reload) {
    if (!summ_ || !prop_.editorProps) return;
    // 別のヘッドレスペインはホストのキューを共有しない場合がある。そ
    // れでも素材移動の成功を公開する前に、その書き込みを完了する。
    std::unique_ptr<summary::FileSync> local;
    auto* sync = prop_.fileSync;
    if (!sync) {
        local = std::make_unique<summary::FileSync>();
        local->start();
        sync = local.get();
    }
    (void)summ_->updateJpy1Files(prop_.editorProps->parent(), prop_.editorProps->var().autoUpdateJpy1File.value, *sync);
    sync->sync();
    if (reload) summ_->updateJpy1List(prop_.editorProps->parent());
}

void DirectoryWindow::updatePaths(StringView from, StringView to) {
    if (!summ_) return;
    const auto counter = summ_->useCounter();
    if (!counter) return;
    // Dは正確に名前変更したファイルの参照を移す。対応する拡大画像に基
    // 底画像の使用数を表示していても、それは名前変更した参照ではない。
    const auto exact_id = [this](const StringView path) {
        const auto value = pathString(filePath(path));
        return usecounter::toPathId(value.empty() ? String(path) : value);
    };
    const auto old_id = exact_id(from);
    if (counter->get(old_id) == 0) return;
    counter->change(old_id, exact_id(to));
    summ_->changed();
}

void DirectoryWindow::updatePathTree(const StringView from, const StringView to,
                                     const bool directory) {
    updatePaths(toRelPath(from), toRelPath(to));
    if (!directory) return;

    const auto old_root=String(from);
    const auto new_root=String(to);
    std::error_code walk_error;
    for (std::filesystem::recursive_directory_iterator it(filePath(new_root), walk_error), end;
         !walk_error && it != end; it.increment(walk_error)) {
        const auto child=pathString(it->path());
        const auto suffix=child.size()>new_root.size()
                            ? child.substr(new_root.size()) : String{};
        updatePaths(toRelPath(old_root+suffix),toRelPath(child));
    }
}

PathId DirectoryWindow::counterPathId(const StringView path) const {
    auto logical_path = filePath(path);
    // ファイル表は対応する拡大画像に基底素材の使用数を表示する
    // 。この表示専用の別名を、名前変更時の参照移行に使わない。
    if (summ_ && !summ_->legacy() && summ_->loadScaledImage())
        logical_path = ::cwxeditor::cwx::utils::noScaledPath(logical_path);
    const auto value = pathString(logical_path);
    return usecounter::toPathId(value.empty() ? String(path) : value);
}

void DirectoryWindow::dirsEditEnd(TreeItemId item, String newText) {
    const auto* node = findNode(item);
    if (!node || node->parent == 0 || newText.empty()) return;
    const auto changed = pathRename(node->value, std::move(newText));
    if (!changed.empty()) refreshDirs(changed);
}

TextEditPtr DirectoryWindow::dirsCreateEditor(TreeItemId item) {
    const auto* node = findNode(item);
    if (!node || node->parent == 0) return {};
    auto editor = std::make_shared<TextEdit>(node->text);
    editor->on_enter = [this, item, editor] { dirsEditEnd(item, editor->text); };
    return editor;
}

void DirectoryWindow::filesEditEnd(std::size_t item, std::size_t column, String newText) {
    if (column != 0 || newText.empty()) return;
    const auto* value = rowObject(item);
    if (!value) return;
    if (!pathRename(value->array, std::move(newText)).empty()) refreshDirs(selDirPath());
}

void DirectoryWindow::storeSortParams() {
    prop_.filesSortColumn = sort_column_ == SortColumn::Name
                              ? 0 : (sort_column_ == SortColumn::Extension ? 1 : 2);
    prop_.filesSortDirection = sort_direction_ == SortDirection::Up ? 1 : 2;
    // Dの三つの列インデックスをすべて維持する。従来のNameWindowコー
    // ルバックはExtension列挙値を持たず、この設定を正確に表せない。
    if (prop_.editorProps) {
        prop_.editorProps->var().filesSortColumn.value = prop_.filesSortColumn;
        prop_.editorProps->var().filesSortDirection.value = prop_.filesSortDirection;
    }
    if (comm_.storeSort) {
        const auto column = sort_column_ == SortColumn::Count
                               ? namewindow::SortColumn::UseCount
                               : namewindow::SortColumn::Name;
        const auto direction = sort_direction_ == SortDirection::Up
                                  ? namewindow::SortDirection::Up
                                  : namewindow::SortDirection::Down;
        comm_.storeSort(column, direction);
    }
}

void DirectoryWindow::changeVHSide() {
    // Dはネイティブのスタイルビットを変えるためSWTの分割部
    // を作り直す。共通SplitViewは向きをモデル状態とし
    // て保持するので、子と現在の比率を残してその場で変更する。
    prop_.directorySashVertical = !prop_.directorySashVertical;
    if (prop_.editorProps)
        prop_.editorProps->var().directorySashV.value = prop_.directorySashVertical;
    if (split_) split_->orientation = prop_.directorySashVertical ? lib::Orientation::Vertical
                                                                   : lib::Orientation::Horizontal;
}

void DirectoryWindow::closePreview() {
    ++preview_revision_;
    preview_path_.clear();
    // バックエンドのコールバック前に切り離す。hide()が新しいプレビ
    // ューを開いた場合、古い終了処理が戻った後に再び消してはならない。
    const auto old_image = std::move(preview_image_);
    if (preview_) preview_->close();
}

void DirectoryWindow::previewTrigger(const float x, const float y,
                                     const lib::Point screen_position) {
    const auto keep_alive = weak_from_this().lock();
    const auto table = files_;
    if (!table) return;
    const auto row = table->rowAt({x, y});
    const auto object = row ? rowObject(*row) : nullptr;
    if (!object) {
        closePreview();
        return;
    }
    const auto value = *object;
    if (value.dir || !isImageExtension(value.array)) {
        closePreview();
        return;
    }
    const auto row_bounds = table->rowBounds(*row);
    // Dはホバー行の左下を基準にポップアップを置く。イベントの
    // ポインターから画面位置を求め、単体ウィンドウの原点とWeb
    // ALLの組み込みCanvas座標を同じ空間に保つ。
    const auto preview_x = static_cast<std::int32_t>(
        screen_position.x + row_bounds.x - x);
    const auto preview_y = static_cast<std::int32_t>(
        screen_position.y + row_bounds.y + row_bounds.height - y);
    const auto row_height = static_cast<std::int32_t>(table->rowHeight());
    if (preview_path_ == value.array) {
        if (preview_ && preview_image_) {
            // スクロールなどで行自体が動いた場合だけ位置を変え
            // る。変わらない行内のポインター移動では、Preview
            // のキャッシュにより再アップロードを避ける。
            preview_->image(preview_image_, preview_x, preview_y, row_height);
            preview_->show();
        }
        return;
    }
    const auto revision = preview_revision_ + 1;
    const auto row_revision = table->row_order_revision;
    closePreview();
    const auto current = [&] {
        return revision == preview_revision_ && files_ == table &&
            table->row_order_revision == row_revision;
    };
    if (!current() || !preview_) return;
    bool transparent{};
    const auto image = previewImage(value.array, transparent);
    if (!current()) return;
    if (!image) {
        return;
    }
    if (image->data.frames.empty()) return;
    const auto& frame = image->data.frames.front();
    const auto width = static_cast<std::int32_t>(std::max<std::uint32_t>(1U,
        frame.logical_size.width > 0.0F
            ? static_cast<std::uint32_t>(frame.logical_size.width)
            : frame.pixel_width));
    const auto height = static_cast<std::int32_t>(std::max<std::uint32_t>(1U,
        frame.logical_size.height > 0.0F
            ? static_cast<std::uint32_t>(frame.logical_size.height)
            : frame.pixel_height));
    auto source = areaviewutils::ImageDataWithScale(image->data, 1U);
    const auto graphics = comm_.services.graphics;
    auto prepared_image = std::make_shared<areaviewutils::PileImage>(
        std::move(source), 1U, 0, 0, width, height, false, false);
    prepared_image->setGraphics(graphics);
    // DのPreviewは、ファイルプレビューがCardWirth
    // の左上透過色を使う場合、輪郭付きの透過表示を作る。マスク
    // 済み画素だけでなくPileImageにもその意味を保持し
    // 、全表示側でポップアップ拡大時にアルファの輪郭を使う。
    prepared_image->transparent(transparent);
    prepared_image->createImage();
    if (!current()) return;
    preview_path_ = value.array;
    preview_image_ = std::move(prepared_image);
    preview_->image(preview_image_, preview_x, preview_y, row_height);
    if (current()) preview_->show();
}

std::optional<PreviewData> DirectoryWindow::previewImage(StringView path,
                                                          bool& transparent) const {
    transparent = false;
    if (!isImageExtension(path) || !std::filesystem::exists(filePath(path))) return std::nullopt;
    const bool mask = prop_.maskCardImagePreview && isCard(path);
    auto data = dutils::loadImage(path, mask, false, 0, 0, comm_.services);
    if (data.frames.empty()) return std::nullopt;
    transparent = mask;
    return PreviewData{std::move(data), transparent};
}

void DirectoryWindow::refresh() {
    const auto keep_alive = weak_from_this().lock();
    std::scoped_lock lock(mutex_);
    if (!window_ || !summ_) return;
    if (prop_.editorProps) summ_->updateJpy1List(prop_.editorProps->parent());
    const auto directory = selDirPath();
    const auto revision = file_refresh_revision_ + 1;
    refreshDirs(directory);
    if (revision != file_refresh_revision_) return;
    syncTraceWatch();
    if (revision != file_refresh_revision_) return;
    refreshTitle();
    notifyToolbar();
}

void DirectoryWindow::refresh(SummaryPtr summ) {
    const auto keep_alive = weak_from_this().lock();
    std::scoped_lock lock(mutex_);
    const auto previous_summary = summ_;
    const auto summary = std::move(summ);
    const auto table = files_;
    const auto old_rows = table ? table->row_order_revision : 0;
    const auto revision = file_refresh_revision_ + 1;
    String selected;
    if (summary) {
        selected = summary->scenarioPath();
        if (!prop_.materialPath.empty()) {
            const auto material = filePath(summary->scenarioPath()) / filePath(prop_.materialPath);
            if (std::filesystem::is_directory(material)) selected = pathString(material);
        }
    }
    // 上記のパス準備は、使用中の状態を変えずに失敗できる。
    summ_ = summary;
    try { refreshDirs(selected); }
    catch (...) {
        // 準備失敗時は状態も戻す。ただし、公開完了
        // 後の通知失敗や、すでにこのSummary
        // を置換した内側の要求は巻き戻さない。
        if (summ_ == summary && files_ == table &&
            table && table->row_order_revision == old_rows) summ_ = previous_summary;
        throw;
    }
    if (summ_ == summary && files_ == table && table && table->row_order_revision == old_rows) {
        // プロバイダーは置換行を公開せず、選択・展開の
        // 変更だけで準備を無効にし得る。シナリオを半分
        // だけ切り替えず、旧状態を旧行と対で維持する。
        summ_ = previous_summary;
        return;
    }
    if (revision != file_refresh_revision_ || summ_ != summary) return;
    syncTraceWatch();
    if (revision != file_refresh_revision_ || summ_ != summary) return;
    refreshTitle();
    notifyToolbar();
}

bool DirectoryWindow::isChanged() {
    if (!summ_) return false;
    if (!std::filesystem::exists(filePath(summ_->scenarioPath()))) return false;
    if (!summ_->isChanged()) summ_->checkPathsIsChanged(prop_.saveInnerImagePath);
    return summ_->isChanged();
}

bool DirectoryWindow::canOpenDirectory() const {
    return summ_ != nullptr;
}

void DirectoryWindow::openDirectory() {
    const auto directory = selDirPath();
    if (directory.empty()) return;
    if (prop_.openFolder) prop_.openFolder(directory);
    else if (comm_.services.shell) {
        std::string error;
        (void)comm_.services.shell->open_path(filePath(directory), error);
    }
}

bool DirectoryWindow::canCreateNewFolder() const {
    return summ_ != nullptr;
}

void DirectoryWindow::createNewFolder() {
    if (!canCreateNewFolder()) {
        if (prop_.openDirectoryWindow) prop_.openDirectoryWindow();
        return;
    }
    if (last_focus_ == Focus::Files) createDirFiles();
    else createDirDirs();
}

bool DirectoryWindow::select(String path, bool deselectEtc) {
    if (!summ_ || path.empty()) return false;
    // Dの文字列はUTF-8。ネイティブのファイルシステムへパスを問い合わせる前に復号する。
    if (!filePath(path).is_absolute()) path = pathString(filePath(summ_->scenarioPath()) / filePath(path));
    path = absolutePath(path);
    std::error_code error;
    if (!std::filesystem::exists(filePath(path), error)) return false;
    const bool directory = isDirectory(filePath(path));
    const auto parent = directory ? path : pathString(filePath(path).parent_path());
    // Dは要求された場合だけ他のファイル行を選択解除する。この行を追加する前
    // に、共通モデルの再構築をまたいで同じディレクトリのパス選択を保持する。
    auto selected_paths = !directory && !deselectEtc &&
        sameDisplayedPath(selDirPath(), parent, comm_.services.filesystem.get())
        ? selFiles() : std::vector<String>{};
    if (!directory) selected_paths.push_back(path);
    TreeItemId id = 0;
    for (const auto& [node_id, object] : directory_objects_)
        if (sameDisplayedPath(object.array, parent, comm_.services.filesystem.get())) {
            id = node_id;
            break;
        }
    if (id == 0) {
        refreshDirs(parent);
        for (const auto& [node_id, object] : directory_objects_)
            if (sameDisplayedPath(object.array, parent, comm_.services.filesystem.get())) {
                id = node_id;
                break;
            }
    }
    if (id == 0) return false;
    (void)lib::select_tree_items(*dirs_, {id}, false, false);
    setFocus(directory ? Focus::Directories : Focus::Files);
    refreshFiles(selected_paths);
    bool matched = directory;
    if (!directory) {
        std::vector<std::size_t> selection;
        const auto& states = files_->rowStates();
        for (std::size_t index = 0; index < states.size(); ++index) {
            const auto object = std::any_cast<FileNameObj>(&states[index].data);
            if (object && sameDisplayedPath(object->array, path,
                                   comm_.services.filesystem.get()))
                selection.push_back(index);
        }
        if (deselectEtc || selection.empty()) files_->clearSelection(false);
        if (!selection.empty()) {
            matched = true;
            if (!deselectEtc) {
                for (const auto previous : files_->selection)
                    if (std::find(selection.begin(),selection.end(),previous)==selection.end())
                        selection.push_back(previous);
            }
            files_->setSelection(selection, false);
        }
    }
    notifyToolbar();
    return matched;
}

void DirectoryWindow::setTraceEnabled(const bool enabled) {
    const auto keep_alive = weak_from_this().lock();
    const std::weak_ptr<CallbackLifetime> lifetime = callback_lifetime_;
    const bool changed = trace_running_ != enabled;
    const auto generation = trace_generation_;
    prop_.traceChanges = enabled;
    trace_running_ = enabled;
    syncTraceWatch();
    if (lifetime.expired()) return;
    // 追跡無効中の変更も取り込むが、独立した保存・停止の
    // 境界は上書きしない。有効設定自体が変わらなくても、
    // 「名前を付けて保存」の成功後は対象を切り替える。
    if (enabled && !trace_paused_ && (changed || generation != trace_generation_))
        requestTraceRefresh();
}

void DirectoryWindow::stopTrace() {
    trace_paused_ = true;
    closeTraceHandle();
}

void DirectoryWindow::resumeTrace() {
    const auto keep_alive = weak_from_this().lock();
    const std::weak_ptr<CallbackLifetime> lifetime = callback_lifetime_;
    trace_paused_ = false;
    syncTraceWatch();
    if (lifetime.expired()) return;
    requestTraceRefresh(); // ネイティブ監視が止まっていた間の変更も取り込む。
}

void DirectoryWindow::pauseTrace() {
    stopTrace();
}

void DirectoryWindow::closeTraceHandle() { closeTraceHandleImpl(); }

void DirectoryWindow::closeTraceHandleImpl() {
    ++trace_generation_;
    trace_refresh_pending_ = false;
    trace_refresh_requested_ = false;
    trace_starting_ = false;
    trace_root_.clear();
    trace_summary_.reset();
    auto watch = std::move(trace_watch_);
    if (watch) watch->stop();
}

void DirectoryWindow::pollTraceRefresh() {
    if (!trace_refresh_requested_ || trace_refresh_pending_ || trace_starting_ ||
        !trace_running_ || trace_paused_ || !summ_) return;
    requestTraceRefresh();
}

void DirectoryWindow::requestTraceRefresh() {
    if (!trace_running_ || trace_paused_ || !summ_) return;
    if (trace_starting_) { trace_refresh_requested_ = true; return; }
    if (trace_refresh_pending_) return;
    const auto dispatcher = comm_.services.dispatcher;
    if (!dispatcher || !dispatcher->is_main_thread()) return;
    const std::weak_ptr<CallbackLifetime> lifetime = callback_lifetime_;
    const auto generation = trace_generation_;
    trace_refresh_requested_ = false;
    trace_refresh_pending_ = true;
    try {
        dispatcher->post([this, lifetime, generation,
                          ui = std::weak_ptr<lib::MainDispatcher>(dispatcher)] {
            const auto dispatcher = ui.lock();
            if (!dispatcher || !dispatcher->is_main_thread()) return;
            if (lifetime.expired()) return;
            const auto keep_alive = weak_from_this().lock();
            if (generation != trace_generation_ || !trace_running_ || trace_paused_) return;
            if (trace_summary_.lock() != summ_) return;
            trace_refresh_pending_ = false;
            const auto summary = summ_;
            const auto current = [&] {
                return !lifetime.expired() && generation == trace_generation_ &&
                    trace_running_ && !trace_paused_ && summ_ == summary;
            };
            if (!std::filesystem::exists(filePath(summary->scenarioPath()))) return;
            if (prop_.canTraceRefresh && !prop_.canTraceRefresh()) {
                // 同期・非同期保存をまたいでイベントを保持する
                // 。メインループが保存後に再試行する。世代変更
                // 、無効化、終了時は、この保留要求を解除する。
                if (current()) trace_refresh_requested_ = true;
                return;
            }
            if (!current()) return;
            if (prop_.fileSync) prop_.fileSync->sync();
            summary->checkPathsIsChanged(prop_.saveInnerImagePath);
            if (!current()) return;
            const auto gui = comm_.services.gui;
            if (gui && (gui->programmatic_inline_edit_active(dirs_) ||
                        gui->programmatic_inline_edit_active(files_))) return;
            if (!current()) return;
            refresh();
            if (current() && prop_.refPaths) prop_.refPaths(summary->scenarioPath());
        });
    } catch (...) {
        if (!lifetime.expired() && generation == trace_generation_) {
            trace_refresh_pending_ = false;
            trace_refresh_requested_ = true;
        }
        throw;
    }
}

void DirectoryWindow::syncTraceWatch() {
    const auto keep_alive = weak_from_this().lock();
    const std::weak_ptr<CallbackLifetime> lifetime = callback_lifetime_;
    const auto summary = summ_;
    const auto root = summary && !summary->scenarioPath().empty()
        ? absolutePath(summary->scenarioPath()) : String{};
    if (!trace_running_ || trace_paused_ || root.empty()) {
        closeTraceHandle();
        return;
    }
    if (trace_root_ == root && trace_summary_.lock() == summary &&
        (trace_watch_ || trace_starting_)) return;
    const auto closing_generation = trace_generation_ + 1;
    closeTraceHandle();
    if (lifetime.expired() || trace_generation_ != closing_generation ||
        summ_ != summary || !trace_running_ || trace_paused_) return;
    const auto filesystem = comm_.services.filesystem;
    const auto dispatcher = comm_.services.dispatcher;
    if (!filesystem || !dispatcher || !dispatcher->is_main_thread()) return;
    trace_root_ = root;
    trace_summary_ = summary;
    const auto generation = trace_generation_;
    const auto valid = [this, lifetime, generation,
                        ui = std::weak_ptr<lib::MainDispatcher>(dispatcher)] {
        const auto dispatcher = ui.lock();
        if (!dispatcher || !dispatcher->is_main_thread()) return false;
        return !lifetime.expired() && generation == trace_generation_ &&
            trace_running_ && !trace_paused_ && trace_summary_.lock() == summ_;
    };
    lib::WatchRequest request;
    request.directory = filePath(root);
    request.recursive = true;
    request.poll_interval = prop_.traceInterval;
    request.on_changed = [this, valid](const std::vector<lib::FileChange>& changes) {
        if (!valid()) return;
        const auto keep_alive = weak_from_this().lock();
        // 無視規則は表示の方針であり、OSアダプターが決めるものではない。
        const auto relevant = [this](const Path& path) {
            for (const auto& part : path.lexically_relative(filePath(trace_root_)))
                if (isIgnore(pathString(part))) return false;
            return true;
        };
        for (const auto& change : changes) {
            if (change.kind == lib::FileChangeKind::Overflow || relevant(change.path) ||
                (!change.old_path.empty() && relevant(change.old_path))) {
                requestTraceRefresh();
                return;
            }
        }
    };
    const auto report = statusText();
    request.on_error = [valid, report](String error) { if (valid()) report(error); };
    trace_starting_ = true;
    try {
        auto watch = filesystem->watch(std::move(request));
        if (!valid()) { if (watch) watch->stop(); return; }
        if (!watch) { closeTraceHandle(); return; }
        trace_starting_ = false;
        trace_watch_ = std::move(watch);
        const bool requested = std::exchange(trace_refresh_requested_, false);
        if (trace_watch_ && requested) requestTraceRefresh();
    } catch (...) {
        if (valid()) closeTraceHandle();
        throw;
    }
}

void DirectoryWindow::quitTrace() {
    trace_running_ = false;
    closeTraceHandle();
}

bool DirectoryWindow::canDeleteUnuse() const {
    return summ_ != nullptr;
}

void DirectoryWindow::deleteUnuse() {
    deleteUnuse(prop_.recycleDelete);
}

void DirectoryWindow::deleteUnuse(lib::Modifiers modifiers) {
    const bool shift = (static_cast<std::uint32_t>(modifiers) &
                        static_cast<std::uint32_t>(lib::Modifiers::Shift)) != 0U;
    deleteUnuse(!shift);
}

void DirectoryWindow::deleteUnuse(bool recycle) {
    if (!canDeleteUnuse()) return;
    const auto keep_alive = weak_from_this().lock();
    const std::weak_ptr<CallbackLifetime> lifetime = callback_lifetime_;
    const auto original_summary = summ_;
    const auto original_path = summ_->scenarioPath();
    const auto current = [&] {
        return !lifetime.expired() && summ_ == original_summary &&
               summ_ && summ_->scenarioPath() == original_path;
    };
    std::vector<String> values;
    if (prop_.notUsedFiles) {
        values = prop_.notUsedFiles();
        if (!current()) return;
        for (auto& value : values) {
            if (!filePath(value).is_absolute())
                value = pathString(filePath(summ_->scenarioPath()) / filePath(value));
        }
    } else if (files_) {
        for (const auto index : files_->selection) {
            const auto* value = rowObject(index);
            if (value && value->material && useCount(*value) == 0)
                values.push_back(value->array);
        }
    }
    if (values.empty()) return;
    const DestructiveFilePlan plan{values, recycle};
    bool confirmed = false;
    if (prop_.confirmDeleteUnused) {
        confirmed = prop_.confirmDeleteUnused(plan);
    } else if (comm_.services.dialogs) {
        String title = "確認";
        String message = std::to_string(values.size()) +
                         "個の未使用素材を削除しますか？";
        if (prop_.editorProps) {
            const auto& messages = prop_.editorProps->requiredMsgs();
            title = messages.dlgTitQuestion.value;
            message = ::cwxeditor::cwx::utils::tryFormat(
                recycle ? messages.dlgMsgDeleteRecycleUnuse.value
                                    : messages.dlgMsgDeleteUnuse.value,
                values.size());
        }
        const auto result = comm_.services.dialogs->message({
            title, message, {}, lib::MessageSeverity::Question,
            lib::MessageButtons::YesNo});
        confirmed = result == lib::MessageResult::Yes ||
                    result == lib::MessageResult::OK;
    }
    if (!confirmed || !current()) return;
    removeFiles(values, recycle);
    if (!current()) return;
    refreshDirs(selDirPath());
    refreshFiles();
    notifyToolbar();
}

bool DirectoryWindow::canCreateArchive() const {
    return summ_ != nullptr && !summ_->scenarioPath().empty();
}

void DirectoryWindow::createArchive() {
    if (!canCreateArchive()) return;
    // Dのアーカイブ前の保存境界を維持する。保存が取消・失
    // 敗なら、シナリオと既存アーカイブの両方を変更しない。
    if (isChanged()) {
        const auto dialogs = comm_.services.dialogs;
        if (!dialogs) return;
        const auto answer = dialogs->message({
            "確認", "アーカイブを作成する前に「" + summ_->scenarioName() +
                    "」を保存しますか?", {}, lib::MessageSeverity::Question,
            lib::MessageButtons::YesNoCancel});
        if (answer == lib::MessageResult::Cancel || answer == lib::MessageResult::None)
            return;
        if ((answer == lib::MessageResult::Yes || answer == lib::MessageResult::OK) &&
            (!prop_.saveScenario || !prop_.saveScenario()))
            return;
    }
    const auto dialogs = comm_.services.dialogs;
    if (!dialogs) return;
    auto filter = lowerAscii(prop_.selectedArchiveFilter);
    if (filter != ".zip" && filter != ".cab" && filter != ".wsn") filter = ".zip";
    auto output_directory = prop_.archivePath.empty() ? std::filesystem::current_path()
                                                       : filePath(prop_.archivePath);
    auto name = summ_->scenarioName().empty() ? baseName(summ_->scenarioPath())
                                              : summ_->scenarioName();
    lib::FileDialogRequest picker;
    picker.title = "アーカイブの作成";
    picker.initial_directory = output_directory;
    picker.initial_name = stripExtension(name) + filter;
    picker.filters = {{"ZIP", {"*.zip"}}, {"CAB", {"*.cab"}}};
    if (!summ_->legacy()) picker.filters.push_back({"WSN", {"*.wsn"}});
    std::optional<Path> selected;
    try {
        selected = dialogs->save_file(picker);
    } catch (const std::exception& exception) {
        if (prop_.statusMessage) prop_.statusMessage(exception.what());
        return;
    } catch (...) {
        if (prop_.statusMessage) prop_.statusMessage("アーカイブ出力先を選択できません");
        return;
    }
    if (!selected || selected->empty()) return;
    const auto extension = lowerAscii(pathString(selected->extension()));
    if (extension != ".zip" && extension != ".cab" &&
        !(extension == ".wsn" && !summ_->legacy())) {
        if (prop_.statusMessage) prop_.statusMessage("アーカイブ形式を判別できません");
        return;
    }
    lib::FileSaveTransaction publication(*dialogs, *selected);
    bool written{};
    String error;
    try {
        if (prop_.createArchive) {
            written = prop_.createArchive(pathString(*selected), extension);
        } else if (comm_.services.archives) {
            lib::ArchiveCreateRequest request;
            request.source_directory = filePath(summ_->scenarioPath());
            request.archive_path = *selected;
            request.format = extension == ".cab" ? lib::ArchiveFormat::Cab : lib::ArchiveFormat::Zip;
            request.include = [this](const Path& value) {
                const auto text = pathString(value);
                return !isDef(text, isDirectory(value));
            };
            std::string archive_error;
            written = comm_.services.archives->create(request, archive_error);
            error = std::move(archive_error);
        }
        if (!written) throw std::runtime_error(error.empty() ? "アーカイブを作成できません" : error);
        std::string publish_error;
        if (!publication.commit(publish_error))
            throw std::runtime_error(publish_error.empty() ? "アーカイブ出力を確定できません" : publish_error);
        prop_.archivePath = pathString(selected->parent_path());
        prop_.selectedArchiveFilter = extension;
        if (prop_.editorProps) {
            prop_.editorProps->var().archivePath.value = prop_.archivePath;
            prop_.editorProps->var().selectedArchiveFilter.value = extension;
        }
    } catch (const std::exception& exception) {
        publication.abort();
        if (prop_.statusMessage) prop_.statusMessage(exception.what());
    } catch (...) {
        publication.abort();
        if (prop_.statusMessage) prop_.statusMessage("アーカイブを作成できません");
    }
}

bool DirectoryWindow::openCWXPath(String path, bool shellActivate) {
    const auto category = ::cwxeditor::cwx::path::cpcategory(path);
    if (category != "fileview") return false;
    dutils::forceFocus(files_, shellActivate, comm_.services);
    return true;
}

std::vector<String> DirectoryWindow::openedCWXPath() const {
    return {"fileview"};
}

Exec::Exec(DirectoryWindow& dirWin, MenuPtr menu, OuterTool tool, int index)
    : dir_win_(&dirWin), tool_(std::move(tool)) {
    if (menu) {
        const auto command = "outer-tool-" + std::to_string(index);
        const auto found = std::find_if(menu->items.begin(), menu->items.end(),
                                        [&command](const lib::MenuItem& item) {
                                            return item.command == command;
                                        });
        if (found == menu->items.end())
            menu->items.push_back({tool_.name, command, tool_.hotkey, tool_.icon, true});
    }
}

void Exec::run() {
    if (!dir_win_) return;
    const auto values = dir_win_->selFiles();
    String joined;
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0) joined.push_back(' ');
        joined += '"';
        joined += values[index];
        joined += '"';
    }
    const auto scenario = dir_win_->summary()
                            ? dir_win_->summary()->scenarioPath()
                            : pathString(std::filesystem::current_path());
    auto work_dir = ::cwxeditor::cwx::structs::OuterTool::parse(
        expandToolVariables(tool_.workDir, joined, scenario), joined, scenario);
    const auto app_dir = filePath(dir_win_->properties().appPath).parent_path();
    if (work_dir.empty()) work_dir = pathString(app_dir);
    else if (!filePath(work_dir).is_absolute() && !app_dir.empty())
        work_dir = pathString(app_dir / filePath(work_dir));
    const auto command = ::cwxeditor::cwx::structs::OuterTool::parse(
        expandToolVariables(tool_.command, joined, scenario), joined, scenario);
    const auto& properties = dir_win_->properties();
    if (properties.executeTool) {
        (void)properties.executeTool(tool_.name, command, work_dir, values);
        return;
    }
    if (dir_win_->commons().services.processes) {
        const auto arguments = splitCommandLine(command);
        if (arguments.empty()) return;
        lib::ProcessLaunchRequest request;
        request.executable = filePath(arguments.front());
        request.arguments.assign(arguments.begin() + 1, arguments.end());
        request.working_directory = filePath(work_dir);
        std::string error;
        (void)dir_win_->commons().services.processes->launch(request, error);
    }
}

}  // namespace cwxeditor::cwx::editor::gui::dwt::directorywindow
