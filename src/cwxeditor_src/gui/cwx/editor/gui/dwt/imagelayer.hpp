#pragma once

// CardImageのレイヤー編集の、移植可能なC++20実装。
#include "cwxeditor_src/core/cwx/card.hpp"
#include "cwxeditor_src/core/cwx/summary.hpp"
#include "cwxeditor_src/core/cwx/types.hpp"
#include "cwxeditor_src/core/cwx/xml.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/areaviewutils.hpp"
#include "cwxeditor_src/lib/components.hpp"
#include "cwxeditor_src/lib/events.hpp"
#include "cwxeditor_src/lib/graphics.hpp"
#include "cwxeditor_src/lib/services.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cwxeditor::cwx::editor::gui::dwt::imagelayer {

namespace lib = ::cwxeditor::lib;
namespace area = ::cwxeditor::cwx::area;
namespace card = ::cwxeditor::cwx::card;
namespace skin = ::cwxeditor::cwx::skin;
namespace types = ::cwxeditor::cwx::types;
namespace xml = ::cwxeditor::cwx::xml;
namespace areaview = ::cwxeditor::cwx::editor::gui::dwt::areaviewutils;

using Commons = areaview::Commons;
using Props = areaview::Props;
using Skin = areaview::Skin;
using Summary = areaview::Summary;
using CardImage = card::CardImage;
using CardImagePtr = lib::Owner<CardImage>;
using CardImageArray = area::CardImageArray;
using String = lib::String;
using StringView = lib::StringView;

/// EditorのUndoサービスへの共通アダプター。元のDは部品側のUndoManager
/// を受け取るが、 呼び出し元はこのインターフェースを渡せる。 ImageLayerList
/// 自身も、ヘッドレス用に件数制限付きの局所履歴を持つ。
class UndoManager {
public:
    virtual ~UndoManager() = default;
    [[nodiscard]] virtual bool canUndo() const noexcept { return false; }
    [[nodiscard]] virtual bool canRedo() const noexcept { return false; }
    virtual void undo() {}
    virtual void redo() {}
};
using UndoManagerPtr = lib::Owner<UndoManager>;

/// モードレスのレイヤー画面を、選択部品の実際の履歴へ接続する。 Dと
/// 同じく所有するImageSelectのUndoManagerを子
/// でも使い、 選択を失う別のUndo履歴を子ウィンドウに作らない。
class CallbackUndoManager final : public UndoManager {
public:
    lib::Callback<bool()> can_undo;
    lib::Callback<bool()> can_redo;
    lib::Action undo_action;
    lib::Action redo_action;
    [[nodiscard]] bool canUndo() const noexcept override {
        try { return can_undo && can_undo(); } catch (...) { return false; }
    }
    [[nodiscard]] bool canRedo() const noexcept override {
        try { return can_redo && can_redo(); } catch (...) { return false; }
    }
    void undo() override { if (undo_action) undo_action(); }
    void redo() override { if (redo_action) redo_action(); }
};

using SelectionHandler = lib::Action;
using SelectionHandlers = std::vector<SelectionHandler>;

class ImageLayerList;
class ImageLayerItem;
class ImageLayerPanel;

class ImageLayerWindow final {
public:
    ImageLayerWindow(Commons comm, const Summary* summary, bool mask,
                     bool read_only, lib::Owner<lib::Widget> parent = {},
                      lib::Callback<String()> save_name = {},
                      UndoManagerPtr undo = {}, lib::Action store = {},
                      lib::Services services = {}, lib::Action on_closed = {});
    ~ImageLayerWindow();
    ImageLayerWindow(const ImageLayerWindow&) = delete;
    ImageLayerWindow& operator=(const ImageLayerWindow&) = delete;

    [[nodiscard]] const lib::Owner<lib::Window>& shell() const noexcept { return window_; }
    [[nodiscard]] const lib::Owner<ImageLayerList>& list() const noexcept { return list_; }
    void show() noexcept;
    void close() noexcept;
    void handleKey(lib::KeyEvent& event);

private:
    void createWindow();
    void saveWindowSize();

    std::shared_ptr<int> key_lifetime_{std::make_shared<int>(0)};
    Commons comm_;
    const Summary* summary_{};
    bool mask_{};
    bool read_only_{};
    lib::Owner<lib::Widget> parent_;
    lib::Callback<String()> save_name_;
    UndoManagerPtr undo_;
    lib::Action store_;
    lib::Action on_closed_;
    std::function<void(bool)> parent_visibility_changed_previous_;
    lib::Services services_;
    lib::Owner<lib::Window> window_;
    lib::Owner<lib::ToolBar> toolbar_;
    lib::Owner<ImageLayerList> list_;
    lib::Owner<ImageLayerPanel> layer_panel_;
    // ALLの1.3倍のレイヤー操作部品に合わせて追加する幅。設定には保存しない。
    float all_extra_width_{};
};

class ImageLayerList final : public lib::Canvas {
public:
    SelectionHandlers selectionEvent;
    SelectionHandlers modEvent;

    ImageLayerList(Commons comm, const Summary* summary, lib::Container* parent,
                   bool mask, lib::Callback<String()> save_name = {},
                   UndoManagerPtr undo = {}, lib::Action store = {},
                   bool read_only = false, lib::Services services = {});
    ~ImageLayerList() override;
    ImageLayerList(const ImageLayerList&) = delete;
    ImageLayerList& operator=(const ImageLayerList&) = delete;

    void refImageScale();
    void mask(bool value);
    [[nodiscard]] const Skin* summSkin() const noexcept;
    void setImages(const CardImageArray& card_paths, int selection = -1);
    void images(CardImageArray card_paths);
    [[nodiscard]] CardImageArray images() const;
    void selection(int index);
    [[nodiscard]] int selection() const noexcept { return selection_; }

    void copyAll();
    [[nodiscard]] bool canCopyAll() const;
    void cut();
    void copy();
    void paste();
    void del();
    void clone();
    [[nodiscard]] bool canDoTCPD() const noexcept { return true; }
    [[nodiscard]] bool canDoT() const noexcept { return false; }
    [[nodiscard]] bool canDoC() const;
    [[nodiscard]] bool canDoP() const;
    [[nodiscard]] bool canDoClone() const noexcept { return false; }
    [[nodiscard]] bool canDoD() const noexcept { return false; }

    lib::Callback<CardImagePtr()> createDefaultItem;
    void addLayer();
    [[nodiscard]] bool canAddLayer() const noexcept { return !read_only_; }
    void removeLayer();
    [[nodiscard]] bool canRemoveLayer() const noexcept;
    void upLayer();
    [[nodiscard]] bool canUpLayer() const noexcept;
    void downLayer();
    [[nodiscard]] bool canDownLayer() const noexcept;

    [[nodiscard]] int indexOf(int x, int y) const noexcept;
    void showSelection() noexcept;
    void setupScrollBar() noexcept;
    [[nodiscard]] int itemHeight() const noexcept;
    void paint(lib::DrawingContext* context, lib::Rect area);
    void addUpdateListener(lib::Action listener);

    void undo();
    void redo();
    [[nodiscard]] bool canUndo() const noexcept;
    [[nodiscard]] bool canRedo() const noexcept;
    /// Dと同じく、所有する選択部品の履歴があればポップアップ・ショ
    /// ートカットをそこへ送る。 なければ保持中の局所履歴を使う。
    void undoCommand();
    void redoCommand();
    [[nodiscard]] bool canUndoCommand() const noexcept;
    [[nodiscard]] bool canRedoCommand() const noexcept;
    [[nodiscard]] const std::shared_ptr<lib::Menu>& contextMenu() const noexcept {
        return context_menu_;
    }

    [[nodiscard]] Commons& commons() noexcept { return comm_; }
    [[nodiscard]] const Commons& commons() const noexcept { return comm_; }
    [[nodiscard]] const Summary* summary() const noexcept { return summary_; }
    [[nodiscard]] bool mask() const noexcept { return mask_; }
    [[nodiscard]] bool readOnly() const noexcept { return read_only_; }
    [[nodiscard]] const std::vector<lib::Owner<ImageLayerItem>>& items() const noexcept {
        return items_;
    }
    [[nodiscard]] lib::Owner<ImageLayerItem> item(std::size_t index) const noexcept;
    [[nodiscard]] int contentHeight() const noexcept { return content_height_; }
    [[nodiscard]] int viewportHeight() const noexcept { return viewport_height_; }
    [[nodiscard]] int scrollOffset() const noexcept { return scroll_offset_; }
    void setPosition(std::size_t index, types::CardImagePosition position);

private:
    // Dと同じくSWT.V_SCROLL付きで一覧を作り、縦スクロー
    // ルバーを常に表示する。 全体が収まる場合はつまみを表示しない。
    struct ScrollbarGeometry {
        lib::Rect track{};
        lib::Rect thumb{};
        bool active{};  // 内容が表示領域より高い。
    };
    [[nodiscard]] ScrollbarGeometry scrollbarGeometry(lib::Rect area) const noexcept;
    [[nodiscard]] float scrollbarWidth(lib::Rect area) const noexcept;
    void scrollTo(int offset) noexcept;
    void captureScrollbarPointer(bool capture);
    bool handleScrollbarPointer(lib::PointerEvent& event);
    using Snapshot = CardImageArray;
    void setImagesImpl(const CardImageArray& card_paths);
    void recordHistory();
    [[nodiscard]] Snapshot snapshot() const;
    void restore(Snapshot value);
    void notifyModified(bool selection_changed);
    void rebuildContextMenu();
    [[nodiscard]] std::optional<CardImageArray> qMaterialCopy(
        const xml::XNode& node, CardImageArray card_paths) const;
    [[nodiscard]] CardImageArray cloneImages(const CardImageArray& source) const;
    [[nodiscard]] bool isDefault(const CardImagePtr& image) const;

    Commons comm_;
    const Summary* summary_{};
    const Skin* summ_skin_{};
    bool mask_{};
    bool read_only_{};
    lib::Container* parent_{};
    lib::Callback<String()> save_name_;
    UndoManagerPtr undo_;
    lib::Action store_;
    lib::Services services_;
    lib::FontSpec ui_font_{};
    std::vector<lib::Owner<ImageLayerItem>> items_;
    int selection_{-1};
    std::vector<lib::Action> updateEvent_;
    std::vector<Snapshot> undo_stack_;
    std::vector<Snapshot> redo_stack_;
    std::shared_ptr<lib::Menu> context_menu_{};
    int scroll_offset_{};
    int content_height_{};
    int viewport_height_{};
    bool scrollbar_dragging_{};
    float scrollbar_grab_{};
};

class ImageLayerItem final {
public:
    ImageLayerItem(ImageLayerList& parent, Commons comm, CardImagePtr card_path);
    ~ImageLayerItem();
    ImageLayerItem(const ImageLayerItem&) = delete;
    ImageLayerItem& operator=(const ImageLayerItem&) = delete;

    void disposeImage() noexcept;
    void loadImage();
    void draw(lib::DrawingContext* context, lib::Rect bounds, bool selected);
    void drawImage(lib::DrawingContext* context, lib::Rect bounds);
    [[nodiscard]] const CardImagePtr& cardPath() const noexcept { return card_path_; }
    void cardPath(CardImagePtr value);
    [[nodiscard]] const String& tooltip() const noexcept { return tooltip_; }
    [[nodiscard]] bool warning() const noexcept { return warning_; }
    [[nodiscard]] lib::ResourceId imageResource() const noexcept;
    [[nodiscard]] lib::Size imageSize() const noexcept;

private:
    ImageLayerList* parent_{};
    Commons comm_;
    CardImagePtr card_path_;
    String tooltip_;
    bool warning_{};
    lib::Owner<areaview::PileImage> image_;
};

class ImageLayerPanel final : public lib::Panel {
public:
    ImageLayerPanel(Commons comm, lib::Container* parent, bool read_only,
                    lib::Services services = {});
    void list(lib::Owner<ImageLayerList> value);
    [[nodiscard]] const lib::Owner<ImageLayerList>& list() const noexcept { return list_; }
    [[nodiscard]] const lib::Owner<lib::ComboBox>& positionType() const noexcept {
        return position_type_;
    }
    void selected();

private:
    Commons comm_;
    bool read_only_{};
    lib::Services services_;
    lib::Owner<lib::Label> position_label_;
    lib::Owner<lib::ComboBox> position_type_;
    std::vector<types::CardImagePosition> position_types_;
    lib::Owner<ImageLayerList> list_;
};

}  // namespace cwxeditor::cwx::editor::gui::dwt::imagelayer
