#include "cwxeditor_src/gui/cwx/editor/gui/dwt/imagelayer.hpp"

#include "cwxeditor_src/core/cwx/structs.hpp"
#include "cwxeditor_src/core/cwx/utils.hpp"
#include "cwxeditor_src/core/cwx/versioninfo.hpp"
#include "cwxeditor_src/lib/ui_actions.hpp"
#include "cwxeditor_src/lib/ui_window_lifecycle.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dmenu.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dutils.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/xmlbytestransfer.hpp"
#include "cwxeditor_src/lib/ui_metrics.hpp"
#include "cwxeditor_src/lib/ui_text_layout.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <map>
#include "cwxeditor_src/core/cwx/usecounter.hpp"
#include <system_error>
#include <utility>

namespace cwxeditor::cwx::editor::gui::dwt::imagelayer {
namespace {

namespace structs = ::cwxeditor::cwx::structs;
namespace utils = ::cwxeditor::cwx::utils;

[[nodiscard]] lib::Services completeServices(const Commons& comm,
                                              lib::Services services) {
    if (!services.gui) services.gui = comm.services.gui;
    if (!services.graphics) services.graphics = comm.services.graphics;
    if (!services.image_codecs) services.image_codecs = comm.services.image_codecs;
    if (!services.clipboard) services.clipboard = comm.services.clipboard;
    if (!services.dialogs) services.dialogs = comm.services.dialogs;
    return services;
}

[[nodiscard]] lib::Owner<lib::Window> makeWindow() {
    // D版の Shell(TITLE | RESIZE | CLOSE | TOOL) に相当するモー
    // ドレスのツールダイアログ。 ALL では lib::Dialog だけが見出し・枠・閉じるボタン
    // 付きの埋め込みダイアログとして表示され、 通常の Window ではダイアログ表示にならない。
    auto result = std::make_shared<lib::Dialog>();
    result->modal = false;
    result->maximizable = false;
    return result;
}

[[nodiscard]] CardImagePtr cloneImage(const CardImagePtr& image) {
    if (!image) return {};
    return std::make_shared<CardImage>(nullptr, *image);
}

[[nodiscard]] CardImagePtr defaultImage() {
    return std::make_shared<CardImage>(String{}, types::CardImagePosition::Default);
}

[[nodiscard]] int positionIndex(const types::CardImagePosition position) noexcept {
    switch (position) {
    case types::CardImagePosition::Default: return 0;
    case types::CardImagePosition::Center: return 1;
    case types::CardImagePosition::TopLeft: return 2;
    }
    return 0;
}

[[nodiscard]] lib::Size cardSize(const Props* prop) {
    if (prop == nullptr) return {};
    const auto size = prop->looks().cardSize();
    return {static_cast<float>(std::max(0, prop->s(size.width))),
            static_cast<float>(std::max(0, prop->s(size.height)))};
}

[[nodiscard]] bool regularFile(StringView path) {
    if (path.empty()) return false;
    std::error_code error;
    return std::filesystem::is_regular_file(structs::pathFromUtf8(path), error);
}

[[nodiscard]] String scenarioPath(const Summary* summary) {
    return summary != nullptr ? summary->scenarioPath() : String{};
}

[[nodiscard]] String dataVersion(const Summary* summary) {
    return summary != nullptr ? summary->dataVersion()
                               : String(versioninfo::LATEST_VERSION);
}

[[nodiscard]] bool resolvedImageExists(const Skin* skin, const Summary* summary,
                                       StringView path) {
    if (regularFile(path)) return true;
    if (skin == nullptr || path.empty()) return false;
    std::error_code error;
    const auto resolved = skin->findImagePath(path, scenarioPath(summary),
                                              dataVersion(summary));
    return !resolved.empty() &&
           std::filesystem::is_regular_file(structs::pathFromUtf8(resolved), error);
}

[[nodiscard]] lib::Color rowColor(const bool selected) noexcept {
    // 共通のDWTの一覧・表と同じ選択色を使う。 共通の項目表示
    // が読みやすい淡い青に変わっても、画像レイヤーだけが 従来
    // の濃いWindowsの選択色のままにならないようにする。
    return selected ? lib::Color{204U, 232U, 255U, 255U}
                    : lib::Color{255U, 255U, 255U, 255U};
}

[[nodiscard]] String fitText(lib::DrawingContext& context, const StringView value,
                             const lib::FontSpec& font, const float width) {
    return lib::ellipsize_gc_text(context, value, font, width);
}

}  // namespace



ImageLayerWindow::ImageLayerWindow(Commons comm, const Summary* summary,
                                   const bool mask, const bool read_only,
                                   lib::Owner<lib::Widget> parent,
                                    lib::Callback<String()> save_name,
                                    UndoManagerPtr undo, lib::Action store,
                                    lib::Services services, lib::Action on_closed)
    : comm_(std::move(comm)), summary_(summary), mask_(mask),
      read_only_(read_only), parent_(std::move(parent)),
      save_name_(std::move(save_name)), undo_(std::move(undo)),
      store_(std::move(store)), on_closed_(std::move(on_closed)),
      services_(completeServices(comm_, std::move(services))) {
    createWindow();
}

// キーボードによる移動は ImageLayerWindow::handleKey で実装する。

// shell() は imagelayer.hpp 内でインライン定義する。

// list() は imagelayer.hpp 内でインライン定義する。

void ImageLayerWindow::close() noexcept {
    if (!window_) return;
    const bool was_visible = window_->visible();
    saveWindowSize();
    if (services_.gui) services_.gui->close(window_);
    window_->set_visible(false);
    if (was_visible) {
        const auto callback = on_closed_;
        if (callback) callback();
    }
}

void ImageLayerWindow::show() noexcept {
    if (!window_) return;
    window_->set_visible(true);
    if (services_.gui) services_.gui->show(window_);
}

ImageLayerWindow::~ImageLayerWindow() {
    key_lifetime_.reset();
    if (window_) {
        window_->on_key_down = {};
        window_->on_descendant_key_down = {};
    }
    if (list_) list_->on_key_down = {};
    // プラットフォームのGUIが、終了通知中もネイティブのウィンドウを保持する場合がある
    // 。 この所有者の破棄後に、生の this を参照する終了コールバックを残さない。
    if (window_) window_->on_close_requested = {};
    if (parent_) parent_->on_visibility_changed = parent_visibility_changed_previous_;
    close();
}

void ImageLayerWindow::createWindow() {
    window_ = makeWindow();
    if (const auto owner = std::dynamic_pointer_cast<lib::Window>(parent_)) {
        lib::configure_owned_window(window_, owner,
            lib::OwnedWindowKind::Modeless);
    }
    window_->resizable = true;
    // 上段はツールバーと配置欄、下段は一覧とする。上段を独立し
    // たパネルにまとめ、 配置欄を水平方向に伸ばしても、その
    // グリッド行が余った高さの半分まで広がらないようにする。
    auto layout = std::make_shared<lib::GridLayout>();
    layout->columns = 1;
    layout->spacing = 4.0F;
    window_->set_layout(layout);
    const bool all_mode = lib::editor_window_host_options().all_mode;
    // ALL のレイヤーツールは1.3倍とし、拡大したアイコンの分だけウィンドウの幅を広げる。
    constexpr float kAllLayerToolScale = 1.3F;
    constexpr std::size_t kLayerTools = 4U;
    all_extra_width_ = all_mode
        ? std::ceil(static_cast<float>(kLayerTools) * 24.0F * (kAllLayerToolScale - 1.0F))
        : 0.0F;
    if (parent_) {
        parent_visibility_changed_previous_ = parent_->on_visibility_changed;
        parent_->on_visibility_changed = [this, previous = parent_visibility_changed_previous_](bool visible) {
            if (previous) previous(visible);
            if (!visible) close();
        };
    }
    window_->title = comm_.prop != nullptr
        ? (read_only_ ? comm_.prop->requiredMsgs().dlgTitImageLayerWindowReadOnly.value
                      : comm_.prop->requiredMsgs().dlgTitImageLayerWindow.value)
        : String("Image layers");
    if (comm_.prop != nullptr) {
        const auto width = std::max<std::int32_t>(1, comm_.prop->var().layerListWidth.value);
        const auto height = std::max<std::int32_t>(1, comm_.prop->var().layerListHeight.value);
        window_->set_bounds({0.0F, 0.0F, static_cast<float>(width) + all_extra_width_,
                             static_cast<float>(height)});
    }
    if (!read_only_) {
        toolbar_ = std::make_shared<lib::ToolBar>();
        toolbar_->items = {
            {"Add layer", "addLayer", 0, true, false, false, false},
            {"Remove layer", "removeLayer", 0, true, false, false, false},
            {"", "", 0, true, false, true, false},
            {"Up", "upLayer", 0, true, false, false, false},
            {"Down", "downLayer", 0, true, false, false, false},
        };
        if (comm_.prop) for (auto& item : toolbar_->items) {
            if (item.separator) continue;
            const auto id = item.command == "addLayer" ? types::MenuID::AddLayer :
                item.command == "removeLayer" ? types::MenuID::RemoveLayer :
                item.command == "upLayer" ? types::MenuID::Up : types::MenuID::Down;
            // D版と同様、ツールバーはアイコンだけを表示し、コマンド名はツールチップにする。
            item.icon = comm_.prop->images().menu(id);
            item.tooltip = comm_.prop->buildMenu(id);
            item.text = item.icon ? String{} : item.tooltip;
        }
        toolbar_->on_command = [this](const std::string_view command) {
            if (!list_) return;
            if (command == "addLayer") list_->addLayer();
            else if (command == "removeLayer") list_->removeLayer();
            else if (command == "upLayer") list_->upLayer();
            else if (command == "downLayer") list_->downLayer();
        };
        if (all_mode) toolbar_->icon_scale = kAllLayerToolScale;
        auto top = std::make_shared<lib::Panel>();
        auto top_layout = std::make_shared<lib::GridLayout>();
        top_layout->columns = 2;
        top_layout->spacing = 4.0F;
        top->set_layout(top_layout);
        top->set_layout_item({.row = 0, .column = 0,
            .horizontal = lib::Align::Stretch, .vertical = lib::Align::Center});
        window_->add(top);
        top->add(toolbar_);
        toolbar_->set_layout_item({.row = 0, .column = 0,
            .horizontal = lib::Align::Start, .vertical = lib::Align::Center});
        layer_panel_ = std::make_shared<ImageLayerPanel>(
            comm_, top.get(), read_only_, services_);
        top->add(layer_panel_);
        layer_panel_->set_layout_item({.row = 0, .column = 1,
            .horizontal = lib::Align::Stretch, .vertical = lib::Align::Center,
            .grow = 1.0F});
    }
    list_ = std::make_shared<ImageLayerList>(
        comm_, summary_, window_.get(), mask_, save_name_, undo_, store_,
        read_only_, services_);
    if (toolbar_) {
        const auto refresh_toolbar = [toolbar = std::weak_ptr<lib::ToolBar>(toolbar_),
                                      list = std::weak_ptr<ImageLayerList>(list_)] {
            const auto current_toolbar = toolbar.lock();
            const auto current_list = list.lock();
            if (!current_toolbar || !current_list) return;
            for (auto& item : current_toolbar->items) {
                if (item.command == "addLayer") item.enabled = current_list->canAddLayer();
                else if (item.command == "removeLayer") item.enabled = current_list->canRemoveLayer();
                else if (item.command == "upLayer") item.enabled = current_list->canUpLayer();
                else if (item.command == "downLayer") item.enabled = current_list->canDownLayer();
            }
            ++current_toolbar->item_update_revision;
        };
        list_->addUpdateListener(refresh_toolbar);
        refresh_toolbar();
    }
    if (layer_panel_) layer_panel_->list(list_);
    const auto bounds = window_->bounds();
    list_->set_bounds({0.0F, 0.0F,
                       bounds.width > 0.0F ? bounds.width : 640.0F,
                       bounds.height > 0.0F ? bounds.height : 480.0F});
    window_->on_bounds_changed = [this](lib::Rect) {
        // 一覧はウィンドウのグリッド配置によりツールバーの下へ置く。 ここでウ
        // ィンドウ寸法を使ってローカル座標 (0,0) に配置すると、 ツール
        // バーに重なったり、ALL の埋め込みダイアログからはみ出したりする。
        if (list_) list_->setupScrollBar();
        saveWindowSize();
    };
    window_->add(list_);
    const std::weak_ptr<int> key_life = key_lifetime_;
    list_->on_key_down = [this, key_life](lib::KeyEvent& event) { if (!key_life.expired()) handleKey(event); };
    window_->on_key_down = [this, key_life](lib::KeyEvent& event) { if (!key_life.expired() && !event.handled) handleKey(event); };
    // D版の表示フィルターと同様、位置のコンボや内側の別コントロール
    // にフォーカスがあっても、 レイヤーのショートカットを受け取る。
    window_->on_descendant_key_down = [this, key_life](lib::KeyEvent& event) {
        if (!key_life.expired() && !event.handled) handleKey(event);
    };
    list_->set_layout_item({.row = read_only_ ? 0U : 1U, .column = 0,
        .horizontal = lib::Align::Stretch,
        .vertical = lib::Align::Stretch, .grow = 1.0F});
    window_->on_close_requested = [this]() {
        saveWindowSize();
        if (window_) window_->set_visible(false);
        const auto callback = on_closed_;
        if (callback) callback();
    };
    show();
}

void ImageLayerWindow::saveWindowSize() {
    if (comm_.prop == nullptr || !window_) return;
    const auto bounds = window_->bounds();
    if (bounds.width > 0.0F) {
        // ALL の追加幅は開くときに加えるため、保存する幅はD版の
        // 単位に戻す。 開くたびにウィンドウが広がらないようにする。
        comm_.prop->var().layerListWidth.value = static_cast<std::int32_t>(
            std::lround(std::max(1.0F, bounds.width - all_extra_width_)));
    }
    if (bounds.height > 0.0F) {
        comm_.prop->var().layerListHeight.value =
            static_cast<std::int32_t>(std::lround(bounds.height));
    }
}

void ImageLayerWindow::handleKey(lib::KeyEvent& event) {
    const auto list = list_;
    if (!list || event.handled) return;
    const auto modifiers = static_cast<std::uint32_t>(event.modifiers);
    const bool control =
        (modifiers & static_cast<std::uint32_t>(lib::Modifiers::Control)) != 0U;
    const bool shift =
        (modifiers & static_cast<std::uint32_t>(lib::Modifiers::Shift)) != 0U;
    const auto configured = [&](types::MenuID id, bool fallback) {
        const auto acc = comm_.prop ? dmenu::convertAccelerator(comm_.prop->buildMenu(id)) : 0;
        return acc ? dmenu::eqAcc(static_cast<int>(acc), static_cast<int>(event.key), 0U, modifiers) : fallback;
    };
    if (configured(types::MenuID::CopyAll, control && shift && event.key == lib::Key::C)) {
        if (list->canCopyAll()) list->copyAll();
        event.handled = true; return;
    }
    if (configured(types::MenuID::Copy, control && !shift && event.key == lib::Key::C)) {
        if (list->canDoC()) list->copy();
        event.handled = true; return;
    }
    if (!read_only_ && configured(types::MenuID::Paste, control && event.key == lib::Key::V)) {
        if (list->canDoP()) list->paste();
        event.handled = true; return;
    }
    if (!read_only_ && configured(types::MenuID::Undo, control && !shift && event.key == lib::Key::Z)) {
        if (shift) list->redoCommand();
        else list->undoCommand();
        event.handled = true;
        return;
    }
    if (!read_only_ && configured(types::MenuID::Redo, control &&
            (event.key == lib::Key::Y || (shift && event.key == lib::Key::Z)))) {
        list->redoCommand();
        event.handled = true;
        return;
    }
    // D版の表示フィルターと同様、ここではメニューの範囲外になる設定済みの移動・追加
    // ・削除の ショートカットも含め、すべてのレイヤーメニューのコマンドを解決する。
    if (configured(types::MenuID::AddLayer, false)) {
        if (list->canAddLayer()) list->addLayer();
        event.handled = true; return;
    }
    if (configured(types::MenuID::RemoveLayer, false)) {
        if (list->canRemoveLayer()) list->removeLayer();
        event.handled = true; return;
    }
    if (configured(types::MenuID::Up, control && !shift && event.key == lib::Key::Up)) {
        if (list->canUpLayer()) list->upLayer();
        event.handled = true; return;
    }
    if (configured(types::MenuID::Down, control && !shift && event.key == lib::Key::Down)) {
        if (list->canDownLayer()) list->downLayer();
        event.handled = true; return;
    }
    if (list->items().empty()) return;
    if (control &&
        (event.key == lib::Key::Up || event.key == lib::Key::Down)) return;
    const auto count = static_cast<int>(list->items().size());
    int index = list->selection();
    if (index < 0) index = 0;
    const auto page = std::max(1, list->viewportHeight() /
                                   std::max(1, list->itemHeight()));
    switch (event.key) {
    case lib::Key::PageUp:
        index = std::max(0, index - page);
        break;
    case lib::Key::PageDown:
        index = std::min(count - 1, index + page);
        break;
    case lib::Key::Home: index = 0; break;
    case lib::Key::End: index = count - 1; break;
    case lib::Key::Up: index = index == 0 ? count - 1 : index - 1; break;
    case lib::Key::Down: index = (index + 1) % count; break;
    case lib::Key::Left:
    case lib::Key::Right:
        list->showSelection(); event.handled = true; return;
    default: return;
    }
    list->selection(index);
    list->showSelection();
    event.handled = true;
}

ImageLayerList::ImageLayerList(Commons comm, const Summary* summary,
                               lib::Container* parent, const bool mask,
                               lib::Callback<String()> save_name,
                               UndoManagerPtr undo, lib::Action store,
                               const bool read_only, lib::Services services)
    : comm_(std::move(comm)), summary_(summary), mask_(mask),
      read_only_(read_only), parent_(parent), save_name_(std::move(save_name)),
      undo_(std::move(undo)), store_(std::move(store)),
      services_(completeServices(comm_, std::move(services))) {
    if (comm_.prop != nullptr) {
        comm_.undoMaxEvent = static_cast<std::size_t>(
            std::max<std::int32_t>(0, comm_.prop->var().undoMaxEtc.value));
    }
    ui_font_ = lib::effective_editor_widget_font();
    double_buffered = true;
    // 画像レイヤーには、ホストが提供する描画コンテキストが必要。矩形
    // だけを受け取る互換コールバックを 経由すると、全項目が描画前に
    // 戻ってしまい、モデルは更新されても一覧は空に見える。 描画は
    // 共通の Canvas のAPIで行い、SDL/Windows
    // は具体的な DrawingContext だけを提供する。
    on_paint_context = [this](lib::DrawingContext& context,
                              const lib::Rect area) {
        paint(&context, area);
    };
    on_pointer = [this](lib::PointerEvent& event) {
        if (event.action == lib::PointerAction::Down && services_.gui && parent_) {
            // D版と同様、右クリックを含むすべての MouseDown で画像一覧にフォーカスを移す。
            const auto found = std::ranges::find_if(
                parent_->children(), [this](const auto& child) {
                    return child.get() == this;
                });
            if (found != parent_->children().end()) services_.gui->focus(*found);
        }
        if (handleScrollbarPointer(event)) return;
        // 表示側はウィンドウ座標を渡すが、項目は一覧自身の上端を基準
        // に配置する。 描画時の開始位置は area.y となる。
        const auto list_bounds = bounds();
        const int local_x = static_cast<int>(event.position.x - list_bounds.x);
        const int local_y = static_cast<int>(event.position.y - list_bounds.y);
        if (event.action == lib::PointerAction::Move) {
            const auto index = indexOf(local_x, local_y);
            set_tooltip(index >= 0 && items_[static_cast<std::size_t>(index)]
                            ? items_[static_cast<std::size_t>(index)]->tooltip()
                            : String{});
        }
        if (event.action == lib::PointerAction::Up) {
            const auto index = indexOf(local_x, local_y);
            if (index >= 0) {
                selection(index);
                showSelection();
                event.handled = true;
            }
            if (event.button == lib::PointerButton::Secondary &&
                context_menu_ && services_.gui) {
                rebuildContextMenu();
                lib::WidgetPtr owner;
                if (parent_) {
                    const auto found = std::ranges::find_if(
                        parent_->children(), [this](const auto& child) {
                            return child.get() == this;
                        });
                    if (found != parent_->children().end()) owner = *found;
                }
                services_.gui->show_context_menu(context_menu_, owner,
                                                  event.position);
                event.handled = true;
            }
        }
    };
    on_wheel = [this](lib::WheelEvent& event) {
        const auto step = std::max(1, itemHeight() / 2);
        scroll_offset_ = std::clamp(
            scroll_offset_ - static_cast<int>(std::lround(event.delta_y * step)),
            0, std::max(0, content_height_ - viewport_height_));
        event.handled = true;
    };
    if (!read_only_) {
        context_menu_ = std::make_shared<lib::Menu>(true);
        context_menu_->on_open = [this] { rebuildContextMenu(); };
        context_menu_->on_command = [this](const StringView command) {
            if (command == "undo") undoCommand();
            else if (command == "redo") redoCommand();
            else if (command == "add-layer") addLayer();
            else if (command == "remove-layer") removeLayer();
            else if (command == "up-layer") upLayer();
            else if (command == "down-layer") downLayer();
            else if (command == "copy" && canDoC()) copy();
            else if (command == "paste" && canDoP()) paste();
            else if (command == "copy-all" && canCopyAll()) copyAll();
        };
        rebuildContextMenu();
    }
    setupScrollBar();
}

ImageLayerList::~ImageLayerList() {
    // ネイティブの終了通知中は、一覧がモードレスウィンドウより少し長く存続する場合が
    // ある。 Canvas の破棄前にリスナーのクロージャーとアップロードした画像
    // を解放し、 エディタのコールバックや描画資源への所有権が残らないようにする。
    selectionEvent.clear();
    modEvent.clear();
    updateEvent_.clear();
    if (context_menu_) {
        context_menu_->on_open = {};
        context_menu_->on_command = {};
    }
    for (const auto& item : items_) {
        if (item) item->disposeImage();
    }
    items_.clear();
}

void ImageLayerList::refImageScale() {
    for (const auto& item : items_) if (item) item->disposeImage();
    setupScrollBar();
}

void ImageLayerList::mask(const bool value) {
    if (mask_ == value) return;
    mask_ = value;
    refImageScale();
}

const Skin* ImageLayerList::summSkin() const noexcept {
    return summ_skin_ != nullptr ? summ_skin_ : comm_.skin;
}

void ImageLayerList::setImages(const CardImageArray& card_paths, const int selection) {
    setImagesImpl(card_paths);
    if (items_.empty()) selection_ = -1;
    else if (selection >= 0) {
        selection_ = std::min(selection, static_cast<int>(items_.size()) - 1);
    } else if (selection_ >= static_cast<int>(items_.size())) {
        selection_ = static_cast<int>(items_.size()) - 1;
    }
    showSelection();
    for (const auto& listener : updateEvent_) if (listener) listener();
}

void ImageLayerList::images(CardImageArray card_paths) {
    setImages(card_paths, selection_);
}

// setImages(const CardImageArray&) は公開オーバーロードに統合している。

CardImageArray ImageLayerList::images() const {
    CardImageArray result;
    result.reserve(items_.size());
    for (const auto& item : items_) {
        result.push_back(item ? cloneImage(item->cardPath()) : CardImagePtr{});
    }
    return result;
}

void ImageLayerList::selection(const int index) {
    const auto old = selection_;
    selection_ = items_.empty() ? -1 :
        std::clamp(index, -1, static_cast<int>(items_.size()) - 1);
    showSelection();
    for (const auto& listener : updateEvent_) if (listener) listener();
    if (old != selection_) {
        for (const auto& listener : selectionEvent) if (listener) listener();
    }
}

// selection() は imagelayer.hpp 内でインライン定義する。

void ImageLayerList::copyAll() {
    if (!services_.clipboard || !canCopyAll()) return;
    auto node = CardImage::toNode(images());
    if (summary_ != nullptr) {
        node.newAttr("scenarioPath", utils::nabs(StringView{summary_->scenarioPath()}));
    }
    xmlbytestransfer::ClipData clipboard(services_.clipboard);
    // D版と同様、ユーザーのテキスト・独自転送形式の設定に従って、レイヤーのXMLを転送する。
    xmlbytestransfer::XMLtoCB(comm_.prop && comm_.prop->var().xmlCopy.value,
                             clipboard, node.text());
    const auto refresh = comm_.refreshToolBar;
    if (refresh) refresh();
}

bool ImageLayerList::canCopyAll() const {
    if (!services_.clipboard) return false;
    for (const auto& item : items_) {
        if (item && !isDefault(item->cardPath())) return true;
    }
    return false;
}

void ImageLayerList::cut() {}

void ImageLayerList::copy() {
    if (!canDoC()) return;
    CardImageArray selected{items_[selection_]->cardPath()};
    auto node = CardImage::toNode(selected);
    if (summary_ != nullptr) {
        node.newAttr("scenarioPath", utils::nabs(StringView{summary_->scenarioPath()}));
    }
    xmlbytestransfer::ClipData clipboard(services_.clipboard);
    // D版と同様、ユーザーのテキスト・独自転送形式の設定に従って、レイヤーのXMLを転送する。
    xmlbytestransfer::XMLtoCB(comm_.prop && comm_.prop->var().xmlCopy.value,
                             clipboard, node.text());
    const auto refresh = comm_.refreshToolBar;
    if (refresh) refresh();
}

void ImageLayerList::paste() {
    if (read_only_ || !services_.clipboard || selection_ < 0 ||
        selection_ >= static_cast<int>(items_.size())) return;
    const auto text = xmlbytestransfer::CBtoXML(*services_.clipboard);
    if (text.empty()) return;
    try {
        auto node = xml::XNode::parse(text);
        auto paths = CardImage::createFromNode(node);
        if (paths.empty()) return;
        auto copied_paths = qMaterialCopy(node, std::move(paths));
        if (!copied_paths) return;
        paths = std::move(*copied_paths);
        const auto before = images();
        const bool replace_default = isDefault(before[selection_]);
        CardImageArray result;
        result.reserve(before.size() - (replace_default ? 1U : 0U) + paths.size());
        for (int i = 0; i < selection_; ++i) result.push_back(before[static_cast<std::size_t>(i)]);
        for (const auto& path : paths) result.push_back(cloneImage(path));
        const auto after_start = replace_default ? selection_ + 1 : selection_;
        for (std::size_t i = static_cast<std::size_t>(after_start); i < before.size(); ++i) {
            result.push_back(before[i]);
        }
        // D版の動的配列は CardImage の値を比較する。C++版
        // の配列は shared_ptr を持つため、 履歴を記録する
        // 前に画像の値を比較し、同一内容の貼り付けでは何も変更しない。
        const bool unchanged = before.size() == result.size() &&
            std::equal(before.begin(), before.end(), result.begin(),
                [](const CardImagePtr& left, const CardImagePtr& right) {
                    return (!left && !right) ||
                           (left && right && left->opEquals(*right));
                });
        if (unchanged) return;
        recordHistory();
        setImagesImpl(result);
        selection_ = selection_ + static_cast<int>(paths.size()) - 1;
        showSelection();
        // D版と同様、ローカルの操作欄を更新し、modEvent で所有者のモデルを保存
        // してから選択を更新する。 保存コールバックがこの一覧を無効化する場合がある。
        const auto update_listeners = updateEvent_;
        const auto modification_listeners = modEvent;
        const auto selection_listeners = selectionEvent;
        const auto store = store_;
        const auto refresh = comm_.refreshToolBar;
        for (const auto& listener : update_listeners) if (listener) listener();
        if (store) store();
        // D版と同様、貼り付け後に共有ツールバーを更新する。 操作可否の判定
        // で新しい選択レイヤーを参照できるよう、先に所有者のパスを確定する。
        if (refresh) refresh();
        for (const auto& listener : modification_listeners) if (listener) listener();
        for (const auto& listener : selection_listeners) if (listener) listener();
    } catch (...) {
        // D版と同様、不正なクリップボードデータは無視する。
    }
}

void ImageLayerList::del() {}

void ImageLayerList::clone() {}

// canDoTCPD() は imagelayer.hpp 内でインライン定義する。

// canDoT() は imagelayer.hpp 内でインライン定義する。

bool ImageLayerList::canDoC() const {
    return services_.clipboard && selection_ >= 0 &&
           selection_ < static_cast<int>(items_.size()) &&
           items_[selection_] && !isDefault(items_[selection_]->cardPath());
}

bool ImageLayerList::canDoP() const {
    // D版と同様、OSクリップボードにXMLがあるかで貼り付け可否を決める。データの構造は実行時に調べる。
    return !read_only_ && services_.clipboard &&
           xmlbytestransfer::CBisXML(*services_.clipboard);
}

// canDoClone() は imagelayer.hpp 内でインライン定義する。

// canDoD() は imagelayer.hpp 内でインライン定義する。

std::optional<CardImageArray> ImageLayerList::qMaterialCopy(
    const xml::XNode& node, CardImageArray card_paths) const {
    if (!summary_) return card_paths;
    const auto from_scenario = node.attr("scenarioPath", false);
    if (from_scenario.empty() ||
        utils::cfnmatch(utils::nabs(StringView{from_scenario}),
                        utils::nabs(StringView{summary_->scenarioPath()})))
        return card_paths;
    if (!comm_.prop) return std::nullopt;

    const auto source_root = structs::pathFromUtf8(from_scenario);
    const auto target_root = structs::pathFromUtf8(summary_->scenarioPath());
    // 問い合わせ前に、カウンターの PathId による同一性判定に合わせる。 重
    // 複するレイヤーは一つの素材として数え、それぞれの配置位置は独立して保持する。
    using Groups = std::map<usecounter::PathId, std::vector<std::size_t>>;
    Groups binary;
    Groups referenced;
    std::vector<String> source_names;
    for (std::size_t index = 0; index < card_paths.size(); ++index) {
        const auto& image = card_paths[index];
        if (!image || image->type != types::CardImageType::File || image->path().empty()) continue;
        const auto key = usecounter::toPathId(image->path());
        if (utils::isBinImg(image->path())) {
            binary[key].push_back(index);
            continue;
        }
        const auto source = (source_root / structs::pathFromUtf8(image->path())).lexically_normal();
        std::error_code error;
        if (std::filesystem::is_regular_file(source, error)) {
            auto& users = referenced[key];
            if (users.empty()) source_names.push_back(structs::pathToUtf8(source));
            users.push_back(index);
        }
    }
    if (binary.empty() && referenced.empty()) return card_paths;

    bool cancelled = false;
    const auto question = [&](const String& message) {
        if (!services_.dialogs || !comm_.prop) {
            cancelled = true;
            return false;
        }
        lib::MessageDialogRequest request;
        request.title = comm_.prop->requiredMsgs().dlgTitQuestion.value;
        request.message = message;
        request.severity = lib::MessageSeverity::Question;
        request.buttons = lib::MessageButtons::YesNoCancel;
        const auto answer = services_.dialogs->message(request);
        if (answer == lib::MessageResult::Cancel || answer == lib::MessageResult::None) {
            cancelled = true;
            return false;
        }
        return answer == lib::MessageResult::Yes;
    };

    bool externalize_binary = false;
    if (!binary.empty()) {
        if (!summary_->legacy()) {
            externalize_binary = question(comm_.prop->requiredMsgs().dlgMsgExcludeImage.value);
        } else if (comm_.prop->var().saveInnerImagePath.value) {
            externalize_binary = question(comm_.prop->requiredMsgs().dlgMsgCopyMaterial1.value);
        }
    }
    if (cancelled) return std::nullopt;

    bool copy_references = false;
    if (!referenced.empty()) {
        copy_references = question(referenced.size() == 1U
            ? utils::tryFormat(comm_.prop->requiredMsgs().dlgMsgCopyMaterial2.value, source_names.front())
            : utils::tryFormat(comm_.prop->requiredMsgs().dlgMsgCopyMaterial3.value, referenced.size()));
    }
    if (cancelled) return std::nullopt;

    const auto added = comm_.skin ? comm_.skin->materialPath() : String("Material");
    bool error = false;
    const auto copy_group = [&](const std::vector<std::size_t>& indices, const bool isBinary) {
        try {
            const auto old = card_paths[indices.front()];
            const auto source = isBinary ? utils::Path(String(old->path()))
                : (source_root / structs::pathFromUtf8(old->path())).lexically_normal();
            const auto copied = utils::copyTo(target_root, source,
                structs::pathFromUtf8(added), isBinary,
                isBinary && save_name_ ? save_name_() : String{});
            if (copied.empty()) { error = true; return; }
            for (const auto index : indices)
                card_paths[index] = std::make_shared<CardImage>(
                    structs::pathToUtf8(copied), card_paths[index]->positionType());
        } catch (...) {
            // D版と同様、個々のコピーが失敗しても、承認済みの転送を続ける
            // 。 それ以前に成功したファイルとレイヤーの順序を保持する。
            error = true;
        }
    };
    if (externalize_binary)
        for (const auto& [key, indices] : binary) copy_group(indices, true);
    if (copy_references)
        for (const auto& [key, indices] : referenced) copy_group(indices, false);
    if (!summary_->legacy()) {
        for (auto& image : card_paths)
            if (image && image->type == types::CardImageType::File && utils::isBinImg(image->path()))
                image = std::make_shared<CardImage>(String{}, image->positionType());
    }
    if (error && services_.dialogs && comm_.prop) {
        lib::MessageDialogRequest request;
        request.title = comm_.prop->requiredMsgs().dlgTitWarning.value;
        request.message = comm_.prop->requiredMsgs().dlgMsgCopyError.value;
        request.severity = lib::MessageSeverity::Warning;
        request.buttons = lib::MessageButtons::OK;
        (void)services_.dialogs->message(request);
    }
    return card_paths;
}

void ImageLayerList::addLayer() {
    if (!canAddLayer()) return;
    recordHistory();
    auto path = createDefaultItem ? createDefaultItem() : defaultImage();
    items_.push_back(std::make_shared<ImageLayerItem>(*this, comm_, std::move(path)));
    selection_ = static_cast<int>(items_.size()) - 1;
    setupScrollBar();
    showSelection();
    for (const auto& listener : modEvent) if (listener) listener();
    // D版の imagelayer.d と同様、selectionEvent より先に modEvent
    // でパスを通知する。 選択欄の選択を変える前に、新しいレイヤー数を確定する。
    if (store_) store_();
    notifyModified(true);
}

// canAddLayer() は imagelayer.hpp 内でインライン定義する。

void ImageLayerList::removeLayer() {
    if (!canRemoveLayer()) return;
    recordHistory();
    items_.erase(items_.begin() + selection_);
    selection_ = std::min(selection_, static_cast<int>(items_.size()) - 1);
    setupScrollBar();
    showSelection();
    for (const auto& listener : modEvent) if (listener) listener();
    // D版の imagelayer.d と同様、selectionEvent より先に modEvent
    // でパスを通知する。 選択欄の選択を変える前に、新しいレイヤー数を確定する。
    if (store_) store_();
    notifyModified(true);
}

bool ImageLayerList::canRemoveLayer() const noexcept {
    return !read_only_ && selection_ >= 0 &&
           selection_ < static_cast<int>(items_.size()) && items_.size() > 1U;
}

void ImageLayerList::upLayer() {
    if (!canUpLayer()) return;
    recordHistory();
    std::swap(items_[selection_], items_[selection_ - 1]);
    --selection_;
    showSelection();
    const auto update_listeners = updateEvent_;
    const auto modification_listeners = modEvent;
    const auto selection_listeners = selectionEvent;
    const auto store = store_;
    const auto refresh = comm_.refreshToolBar;
    for (const auto& listener : update_listeners) if (listener) listener();
    if (store) store();
    // D版の選択設定と同様、レイヤーの移動後に共有ツールバーを更新する。
    if (refresh) refresh();
    for (const auto& listener : modification_listeners) if (listener) listener();
    for (const auto& listener : selection_listeners) if (listener) listener();
}

bool ImageLayerList::canUpLayer() const noexcept {
    return !read_only_ && selection_ > 0 &&
           selection_ < static_cast<int>(items_.size());
}

void ImageLayerList::downLayer() {
    if (!canDownLayer()) return;
    recordHistory();
    std::swap(items_[selection_], items_[selection_ + 1]);
    ++selection_;
    showSelection();
    const auto update_listeners = updateEvent_;
    const auto modification_listeners = modEvent;
    const auto selection_listeners = selectionEvent;
    const auto store = store_;
    const auto refresh = comm_.refreshToolBar;
    for (const auto& listener : update_listeners) if (listener) listener();
    if (store) store();
    // D版の選択設定と同様、レイヤーの移動後に共有ツールバーを更新する。
    if (refresh) refresh();
    for (const auto& listener : modification_listeners) if (listener) listener();
    for (const auto& listener : selection_listeners) if (listener) listener();
}

bool ImageLayerList::canDownLayer() const noexcept {
    return !read_only_ && selection_ >= 0 &&
           selection_ + 1 < static_cast<int>(items_.size());
}

int ImageLayerList::indexOf(const int x, const int y) const noexcept {
    static_cast<void>(x);
    const auto height = itemHeight();
    if (height <= 0) return -1;
    const auto index = (y + scroll_offset_) / height;
    return index >= 0 && index < static_cast<int>(items_.size()) ? index : -1;
}

void ImageLayerList::showSelection() noexcept {
    if (selection_ < 0 || items_.empty()) return;
    const auto height = itemHeight();
    const auto top = selection_ * height;
    const auto bottom = top + height;
    if (bottom > scroll_offset_ + viewport_height_) scroll_offset_ = bottom - viewport_height_;
    if (top < scroll_offset_) scroll_offset_ = top;
    scroll_offset_ = std::clamp(scroll_offset_, 0,
                                std::max(0, content_height_ - viewport_height_));
}

void ImageLayerList::setupScrollBar() noexcept {
    content_height_ = itemHeight() * static_cast<int>(items_.size());
    viewport_height_ = std::max(0, static_cast<int>(std::lround(bounds().height)));
    scroll_offset_ = std::clamp(scroll_offset_, 0,
                                std::max(0, content_height_ - viewport_height_));
}

int ImageLayerList::itemHeight() const noexcept {
    const auto size = cardSize(comm_.prop);
    const auto line = lib::measured_effective_ui_text_size("Hg", ui_font_)
                          .value_or(lib::Size{0.0F, ui_font_.points * 1.35F}).height;
    return std::max(static_cast<int>(std::ceil(line * 2.0F + lib::scaled_ui_metric(9.0F))),
                    static_cast<int>(std::lround(size.height)) + 2);
}

void ImageLayerList::paint(lib::DrawingContext* context, const lib::Rect area) {
    const auto height = itemHeight();
    const auto start = std::clamp(scroll_offset_ / height, 0,
                                  static_cast<int>(items_.size()));
    const float row_width = std::max(0.0F, area.width - scrollbarWidth(area));
    int y = static_cast<int>(std::lround(area.y)) - scroll_offset_ % height;
    for (int index = start; index < static_cast<int>(items_.size()); ++index) {
        if (area.height > 0.0F && y >= static_cast<int>(std::ceil(area.y + area.height))) break;
        const lib::Rect row{area.x, static_cast<float>(y), row_width,
                            static_cast<float>(height)};
        if (items_[static_cast<std::size_t>(index)]) {
            items_[static_cast<std::size_t>(index)]->draw(context, row,
                                                          index == selection_);
        }
        y += height;
    }
    if (!context) return;
    const auto bar = scrollbarGeometry(area);
    if (bar.track.width <= 0.0F || bar.track.height <= 0.0F) return;
    context->fill_rect(bar.track, {238U, 240U, 244U, 255U});
    context->draw_rect(bar.track, {190U, 196U, 205U, 255U}, 1.0F,
                       lib::StrokeStyle::Solid);
    // SWT の無効なスクロールバーと同様、全体が収まる場合はつまみを表示しない。
    if (bar.active)
        context->fill_rect(bar.thumb, scrollbar_dragging_
                                          ? lib::Color{64U, 72U, 86U, 255U}
                                          : lib::Color{112U, 124U, 143U, 255U});
}

float ImageLayerList::scrollbarWidth(const lib::Rect area) const noexcept {
    return std::min(std::max(0.0F, area.width), lib::editor_scrollbar_extent());
}

ImageLayerList::ScrollbarGeometry ImageLayerList::scrollbarGeometry(
    const lib::Rect area) const noexcept {
    ScrollbarGeometry result;
    const float width = scrollbarWidth(area);
    const float viewport = std::max(0.0F, area.height);
    if (width <= 0.0F || viewport <= 0.0F) return result;
    result.track = {area.x + area.width - width, area.y, width, viewport};
    const float content = static_cast<float>(content_height_);
    const float maximum = std::max(0.0F, content - viewport);
    result.active = maximum > 0.0F;
    if (!result.active) return result;
    const float minimum_thumb = std::min(
        viewport, std::max(24.0F, lib::scaled_ui_metric(28.0F)));
    const float thumb = std::clamp(viewport * viewport / content, minimum_thumb, viewport);
    const float ratio = std::clamp(static_cast<float>(scroll_offset_) / maximum, 0.0F, 1.0F);
    const float inset = std::min(2.0F, width * 0.25F);
    result.thumb = {result.track.x + inset, result.track.y + (viewport - thumb) * ratio,
                    std::max(0.0F, width - inset * 2.0F), thumb};
    return result;
}

void ImageLayerList::scrollTo(const int offset) noexcept {
    scroll_offset_ = std::clamp(offset, 0, std::max(0, content_height_ - viewport_height_));
    if (services_.gui) {
        if (parent_) {
            for (const auto& child : parent_->children())
                if (child.get() == this) { services_.gui->request_redraw(child); break; }
        }
    }
}

void ImageLayerList::captureScrollbarPointer(const bool capture) {
    if (!services_.gui || !parent_) return;
    for (const auto& child : parent_->children()) {
        if (child.get() != this) continue;
        services_.gui->capture_pointer(child, capture);
        return;
    }
}

bool ImageLayerList::handleScrollbarPointer(lib::PointerEvent& event) {
    const auto area = bounds();
    const auto bar = scrollbarGeometry(area);
    const auto inside = [](const lib::Rect r, const lib::Point p) {
        return p.x >= r.x && p.y >= r.y && p.x < r.x + r.width && p.y < r.y + r.height;
    };
    if (scrollbar_dragging_) {
        if (event.action == lib::PointerAction::Move && bar.active) {
            const float travel = std::max(1.0F, bar.track.height - bar.thumb.height);
            const float top = std::clamp(event.position.y - scrollbar_grab_ - bar.track.y,
                                         0.0F, travel);
            const float maximum = static_cast<float>(
                std::max(0, content_height_ - viewport_height_));
            scrollTo(static_cast<int>(std::lround(maximum * top / travel)));
        } else if (event.action == lib::PointerAction::Up ||
                   event.action == lib::PointerAction::Cancel) {
            scrollbar_dragging_ = false;
            captureScrollbarPointer(false);
            scrollTo(scroll_offset_);
        }
        event.handled = true;
        return true;
    }
    if (!inside(bar.track, event.position)) return false;
    // バーの軌道上のイベントはすべてバーが処理し、その下の行を選択しない。
    if (event.action == lib::PointerAction::Down &&
        event.button == lib::PointerButton::Primary && bar.active) {
        if (inside(bar.thumb, event.position)) {
            scrollbar_dragging_ = true;
            scrollbar_grab_ = event.position.y - bar.thumb.y;
            captureScrollbarPointer(true);
            scrollTo(scroll_offset_);
        } else {
            // D版の ImageLayerList と同様、ページ移動のクリックにはクライアント領域の高さ
            // の半分を整数で使う。 ALL では、従来どおり表示領域全体を単位とするページ移動を維持する。
            const int page = lib::editor_window_host_options().all_mode
                ? viewport_height_ : viewport_height_ / 2;
            scrollTo(scroll_offset_ + (event.position.y < bar.thumb.y ? -page : page));
        }
    }
    event.handled = true;
    return true;
}

void ImageLayerList::addUpdateListener(lib::Action listener) {
    if (listener) updateEvent_.push_back(std::move(listener));
}

void ImageLayerList::notifyModified(const bool selection_changed) {
    for (const auto& listener : updateEvent_) if (listener) listener();
    if (selection_changed) {
        for (const auto& listener : selectionEvent) if (listener) listener();
    }
}

bool ImageLayerList::isDefault(const CardImagePtr& image) const {
    if (!image) return true;
    const auto value = defaultImage();
    return image->opEquals(*value);
}

void ImageLayerList::setImagesImpl(const CardImageArray& card_paths) {
    items_.clear();
    items_.reserve(card_paths.size());
    for (const auto& path : card_paths) {
        items_.push_back(std::make_shared<ImageLayerItem>(
            *this, comm_, cloneImage(path)));
    }
    if (items_.empty()) selection_ = -1;
    else if (selection_ >= static_cast<int>(items_.size())) {
        selection_ = static_cast<int>(items_.size()) - 1;
    }
    setupScrollBar();
}

CardImageArray ImageLayerList::cloneImages(const CardImageArray& source) const {
    CardImageArray result;
    result.reserve(source.size());
    for (const auto& image : source) result.push_back(cloneImage(image));
    return result;
}

ImageLayerList::Snapshot ImageLayerList::snapshot() const {
    return images();
}

void ImageLayerList::recordHistory() {
    if (comm_.undoMaxEvent != 0U) {
        undo_stack_.push_back(snapshot());
        if (undo_stack_.size() > comm_.undoMaxEvent) undo_stack_.erase(undo_stack_.begin());
    }
    redo_stack_.clear();
}

void ImageLayerList::restore(Snapshot value) {
    setImagesImpl(value);
    if (items_.empty()) selection_ = -1;
    else selection_ = std::clamp(selection_, 0, static_cast<int>(items_.size()) - 1);
    showSelection();
    notifyModified(true);
    // 取り消し・やり直しでも、ツールバー編集と同じ保存経路を通じて実際の選択欄
    // を変更する。 これを行わないと、モードレスの一覧だけが変わってしまう。
    if (store_) store_();
}

void ImageLayerList::undo() {
    if (undo_stack_.empty()) return;
    redo_stack_.push_back(snapshot());
    auto value = std::move(undo_stack_.back());
    undo_stack_.pop_back();
    restore(std::move(value));
}

void ImageLayerList::redo() {
    if (redo_stack_.empty()) return;
    undo_stack_.push_back(snapshot());
    auto value = std::move(redo_stack_.back());
    redo_stack_.pop_back();
    restore(std::move(value));
}

bool ImageLayerList::canUndo() const noexcept {
    return !undo_stack_.empty();
}

bool ImageLayerList::canRedo() const noexcept {
    return !redo_stack_.empty();
}

bool ImageLayerList::canUndoCommand() const noexcept {
    return undo_ ? undo_->canUndo() : canUndo();
}

bool ImageLayerList::canRedoCommand() const noexcept {
    return undo_ ? undo_->canRedo() : canRedo();
}

void ImageLayerList::undoCommand() {
    if (undo_) {
        if (undo_->canUndo()) undo_->undo();
    } else {
        undo();
    }
    rebuildContextMenu();
}

void ImageLayerList::redoCommand() {
    if (undo_) {
        if (undo_->canRedo()) undo_->redo();
    } else {
        redo();
    }
    rebuildContextMenu();
}

void ImageLayerList::rebuildContextMenu() {
    if (!context_menu_) return;
    context_menu_->items = {
        {"レイヤーを追加", "add-layer", {}, 0, canAddLayer()},
        {"レイヤーを削除", "remove-layer", {}, 0, canRemoveLayer()},
        {"", "", {}, 0, true, false, false, true},
        {"元に戻す", "undo", {}, 0, canUndoCommand()},
        {"やり直し", "redo", {}, 0, canRedoCommand()},
        {"", "", {}, 0, true, false, false, true},
        {"上へ", "up-layer", {}, 0, canUpLayer()},
        {"下へ", "down-layer", {}, 0, canDownLayer()},
        {"", "", {}, 0, true, false, false, true},
        {"コピー", "copy", {}, 0, canDoC()},
        {"貼り付け", "paste", {}, 0, canDoP()},
        {"全てコピー", "copy-all", {}, 0, canCopyAll()},
    };
    if (comm_.prop) for (auto& item : context_menu_->items) {
        if (item.separator) continue;
        const auto id = item.command == "add-layer" ? types::MenuID::AddLayer :
            item.command == "remove-layer" ? types::MenuID::RemoveLayer :
            item.command == "undo" ? types::MenuID::Undo : item.command == "redo" ? types::MenuID::Redo :
            item.command == "up-layer" ? types::MenuID::Up : item.command == "down-layer" ? types::MenuID::Down :
            item.command == "copy" ? types::MenuID::Copy : item.command == "paste" ? types::MenuID::Paste : types::MenuID::CopyAll;
        item.text = comm_.prop->buildMenu(id);
        item.icon = comm_.prop->images().menu(id);
    }
    ++context_menu_->item_update_revision;
}

lib::Owner<ImageLayerItem> ImageLayerList::item(const std::size_t index) const noexcept {
    return index < items_.size() ? items_[index] : lib::Owner<ImageLayerItem>{};
}

void ImageLayerList::setPosition(const std::size_t index,
                                 const types::CardImagePosition position) {
    if (read_only_ || index >= items_.size() || !items_[index] ||
        !items_[index]->cardPath() ||
        items_[index]->cardPath()->type != types::CardImageType::File) return;
    const auto current = items_[index]->cardPath();
    if (current->positionType() == position) return;
    recordHistory();
    items_[index]->cardPath(std::make_shared<CardImage>(current->path(), position));
    items_[index]->loadImage();
    for (const auto& listener : modEvent) if (listener) listener();
    notifyModified(false);
    if (store_) store_();
}

ImageLayerItem::ImageLayerItem(ImageLayerList& parent, Commons comm,
                               CardImagePtr card_path)
    : parent_(&parent), comm_(std::move(comm)),
      card_path_(card_path ? std::move(card_path) : defaultImage()) {}

void ImageLayerItem::disposeImage() noexcept {
    image_.reset();
}

void ImageLayerItem::draw(lib::DrawingContext* context, const lib::Rect bounds,
                          const bool selected) {
    loadImage();
    if (context == nullptr) return;
    context->fill_rect(bounds, rowColor(selected));
    drawImage(context, bounds);
}

void ImageLayerItem::drawImage(lib::DrawingContext* context,
                               const lib::Rect bounds) {
    if (context == nullptr || !card_path_ || comm_.prop == nullptr) return;
    const auto size = cardSize(comm_.prop);
    const lib::Rect image_bounds{bounds.x + 1.0F, bounds.y + 1.0F,
                                 size.width, size.height};
    if (image_ && image_->resource() != 0) {
        auto source_size = imageSize();
        if (source_size.width <= 0.0F || source_size.height <= 0.0F) source_size = size;
        const auto scale = std::min(
            image_bounds.width / std::max(1.0F, source_size.width),
            image_bounds.height / std::max(1.0F, source_size.height));
        // Card は枠なので、D版と同様に設定されたカード全体の寸法へ拡大
        // する。 通常の画像レイヤーでは、従来どおり縦横比を保って収める。
        const bool card_frame = card_path_->type == types::CardImageType::Talker &&
                                card_path_->talker() == types::Talker::Card;
        const lib::Rect destination = card_frame ? image_bounds : lib::Rect{
            image_bounds.x + (image_bounds.width - source_size.width * scale) / 2.0F,
            image_bounds.y + (image_bounds.height - source_size.height * scale) / 2.0F,
            source_size.width * scale, source_size.height * scale};
        context->draw_image(image_->resource(),
                            {0.0F, 0.0F, source_size.width, source_size.height},
                             destination, lib::Interpolation::Linear);
    } else if (card_path_->type == types::CardImageType::Talker &&
               card_path_->talker() != types::Talker::Card) {
        const auto resource = comm_.prop->images().talker(card_path_->talker());
        if (resource) context->draw_image(resource, {0.0F, 0.0F, size.width, size.height},
                                           image_bounds, lib::Interpolation::Nearest);
    } else if (card_path_->type == types::CardImageType::PCNumber &&
               card_path_->pcNumber() != 0U) {
        const auto* skin = parent_->summSkin();
        const auto value = std::to_string(card_path_->pcNumber());
        const auto configured = comm_.prop->adjustFont(
            comm_.prop->looks().pcNumberFont(skin && skin->legacy()));
        const lib::FontSpec font{configured.name, static_cast<float>(configured.point),
            static_cast<std::uint16_t>(configured.bold ? 700 : 400), configured.italic};
        const auto extent = context->measure_text(value, font, image_bounds.width);
        context->draw_text(value,
            {image_bounds.x + (image_bounds.width - extent.width) / 2.0F,
             image_bounds.y + (image_bounds.height - extent.height) / 2.0F},
            font, lib::Color{0U, 0U, 0U, 255U});
    }
    auto text_x = image_bounds.x + image_bounds.width + lib::scaled_ui_metric(6.0F);
    String name = tooltip_;
    if (name.empty()) name = comm_.prop->requiredMsgs().noSelectImage.value;
    if (warning_) {
        // 警告アイコンは画像の後ろに配置し、テキス
        // トを画像の上へ押し戻さないようにする。
        const auto icon = comm_.prop->images().warning();
        const auto icon_size = lib::scaled_ui_metric(16.0F);
        if (icon) context->draw_image(icon, {0.0F, 0.0F, 16.0F, 16.0F},
            {text_x, bounds.y + (bounds.height - icon_size) / 2.0F,
             icon_size, icon_size}, lib::Interpolation::Nearest);
        text_x += icon_size + lib::scaled_ui_metric(5.0F);
    }
    const auto position = comm_.prop->requiredMsgs().cardImagePositionName(
        card_path_->positionType());
    const auto font = lib::effective_editor_widget_font();
    const auto position_layout = lib::layout_gc_text(*context, position, font);
    const auto position_size = position_layout.extent;
    const auto line_height = std::max(1.0F, position_size.height);
    const auto text_y = bounds.y + (bounds.height - line_height * 2.0F -
                                    lib::scaled_ui_metric(3.0F)) / 2.0F;
    const auto available = std::max(0.0F, bounds.x + bounds.width - text_x - 1.0F);
    const auto shown_name = fitText(*context, name, font, available);
    const auto shown_name_layout = lib::layout_gc_text(*context, shown_name, font);
    lib::draw_gc_text(*context, shown_name_layout, {text_x, text_y},
                      font, lib::Color{0U, 0U, 0U, 255U});
    const auto shown_position = fitText(*context, position, font, available);
    const auto shown_position_layout =
        lib::layout_gc_text(*context, shown_position, font);
    lib::draw_gc_text(*context, shown_position_layout,
        {text_x, text_y + line_height + lib::scaled_ui_metric(3.0F)},
        font, lib::Color{70U, 70U, 70U, 255U});
}

// cardPath() は imagelayer.hpp 内でインライン定義する。

ImageLayerItem::~ImageLayerItem() {
    disposeImage();
}

void ImageLayerItem::loadImage() {
    if (image_ || !card_path_ || parent_ == nullptr || comm_.prop == nullptr) return;
    warning_ = false;
    tooltip_.clear();
    const auto fallback = cardSize(comm_.prop);
    const auto& messages = comm_.prop->requiredMsgs();
    switch (card_path_->type) {
    case types::CardImageType::File: {
        const auto path = card_path_->path();
        if (path.empty()) {
            tooltip_ = messages.noSelectImage.value;
            return;
        }
        const auto* skin = parent_->summSkin();
        if (!resolvedImageExists(skin, parent_->summary(), path)) {
            tooltip_ = utils::tryFormat(messages.noImage.value, utils::encodePath(path));
            warning_ = true;
            return;
        }
        if (skin == nullptr) {
            tooltip_ = utils::encodePath(path);
            warning_ = true;
            return;
        }
        auto data = areaview::detail::detailLoadImage(
            *comm_.prop, *skin, parent_->summary(), path,
            comm_.prop->drawingScaleForImage(parent_->summary()), fallback,
            parent_->mask());
        tooltip_ = utils::encodePath(path);
        auto image = std::make_shared<areaview::PileImage>(
            std::move(data), comm_.prop->drawingScale(), 0, 0,
            static_cast<std::int32_t>(fallback.width),
            static_cast<std::int32_t>(fallback.height), false, false);
        image->setGraphics(comm_.prop->images().services().graphics);
        image->createImage();
        image_ = std::move(image);
        if (image_->resource() == 0 && !image_->imageData().loaded) warning_ = true;
        break;
    }
    case types::CardImageType::PCNumber:
        tooltip_ = card_path_->pcNumber() != 0U
            ? utils::tryFormat(messages.pcNumber.value, card_path_->pcNumber())
            : messages.noSelectImage.value;
        break;
    case types::CardImageType::Talker:
        tooltip_ = messages.talkerName(card_path_->talker());
        if (card_path_->talker() == types::Talker::Card) {
            // D版の imagelayer.d と同様、Card にはスキンのメニューカード枠を描く
            // 。 Images::talker には、この列挙値用のリソースを意図的に設けていない。
            if (const auto* skin = parent_->summSkin()) {
                auto data = areaview::detail::detailMenuCardImage(
                    *comm_.prop, *skin, parent_->summary());
                auto image = std::make_shared<areaview::PileImage>(
                    std::move(data), comm_.prop->drawingScale(), 0, 0,
                    static_cast<std::int32_t>(fallback.width),
                    static_cast<std::int32_t>(fallback.height), false, false);
                image->setGraphics(comm_.prop->images().services().graphics);
                image->createImage();
                image_ = std::move(image);
            }
        }
        break;
    }
}

void ImageLayerItem::cardPath(CardImagePtr value) {
    card_path_ = value ? std::move(value) : defaultImage();
    disposeImage();
    tooltip_.clear();
    warning_ = false;
}

lib::ResourceId ImageLayerItem::imageResource() const noexcept {
    return image_ ? image_->resource() : 0;
}

lib::Size ImageLayerItem::imageSize() const noexcept {
    if (image_) return image_->imageData().size();
    return cardSize(comm_.prop);
}

ImageLayerPanel::ImageLayerPanel(Commons comm, lib::Container* parent,
                                 const bool read_only, lib::Services services)
    : comm_(std::move(comm)), read_only_(read_only),
      services_(completeServices(comm_, std::move(services))),
      position_label_(std::make_shared<lib::Label>()),
      position_type_(std::make_shared<lib::ComboBox>()) {
    static_cast<void>(parent);
    auto layout = std::make_shared<lib::GridLayout>();
    layout->columns = 2;
    layout->spacing = lib::scaled_ui_metric(4.0F);
    set_layout(layout);
    position_types_ = {
        types::CardImagePosition::Default,
        types::CardImagePosition::Center,
        types::CardImagePosition::TopLeft,
    };
    if (comm_.prop != nullptr) {
        position_label_->text = comm_.prop->requiredMsgs().cardImagePosition.value;
        position_type_->set_tooltip(comm_.prop->requiredMsgs().cardImagePosition.value);
    }
    for (const auto position : position_types_) {
        position_type_->items.push_back({
            comm_.prop != nullptr
                ? comm_.prop->requiredMsgs().cardImagePositionName(position)
                : String{},
            {}, 0, true});
    }
    position_type_->selected_index = 0;
    position_type_->on_selection_changed =
        [this](const std::optional<std::size_t> selected) {
            if (!list_ || !selected.has_value() ||
                *selected >= position_types_.size() || list_->selection() < 0) return;
            list_->setPosition(static_cast<std::size_t>(list_->selection()),
                               position_types_[*selected]);
        };
    add(position_label_);
    position_label_->set_layout_item({.row = 0, .column = 0,
        .vertical = lib::Align::Center});
    add(position_type_);
    position_type_->set_layout_item({.row = 0, .column = 1,
        .horizontal = lib::Align::Stretch, .grow = 1.0F});
}

void ImageLayerPanel::list(lib::Owner<ImageLayerList> value) {
    list_ = std::move(value);
    selected();
    if (list_) {
        list_->selectionEvent.push_back([this]() { selected(); });
        list_->addUpdateListener([this]() { selected(); });
    }
}

void ImageLayerPanel::selected() {
    if (!position_type_) return;
    if (!list_ || list_->selection() < 0) {
        (void)lib::select_combo_item(*position_type_, 0U, false);
        position_type_->set_enabled(false);
        return;
    }
    const auto item = list_->item(static_cast<std::size_t>(list_->selection()));
    const auto path = item ? item->cardPath() : CardImagePtr{};
    if (path && path->type == types::CardImageType::File) {
        (void)lib::select_combo_item(
            *position_type_,
            static_cast<std::size_t>(positionIndex(path->positionType())), false);
        position_type_->set_enabled(!read_only_);
    } else {
        (void)lib::select_combo_item(*position_type_, 0U, false);
        position_type_->set_enabled(false);
    }
}

}  // namespace cwxeditor::cwx::editor::gui::dwt::imagelayer
