#pragma once

#include "cwxeditor_src/core/cwx/card.hpp"
#include "cwxeditor_src/core/cwx/skin.hpp"
#include "cwxeditor_src/core/cwx/structs.hpp"
#include "cwxeditor_src/core/cwx/summary.hpp"
#include "cwxeditor_src/core/cwx/types.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dprops.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/imagelistwindow.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/incsearch.hpp"
#include "cwxeditor_src/lib/components.hpp"
#include "cwxeditor_src/lib/dialogs.hpp"
#include "cwxeditor_src/lib/services.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cwxeditor::cwx::editor::gui::dwt::commons { class Commons; }

namespace cwxeditor::cwx::editor::gui::dwt::imagelayer {
class ImageLayerWindow;
}

namespace cwxeditor::cwx::editor::gui::dwt::imageselect {

namespace lib = ::cwxeditor::lib;
namespace card = ::cwxeditor::cwx::card;
namespace dprops = ::cwxeditor::cwx::editor::gui::dwt::dprops;
namespace skin = ::cwxeditor::cwx::skin;
namespace structs = ::cwxeditor::cwx::structs;
namespace summary = ::cwxeditor::cwx::summary;
namespace types = ::cwxeditor::cwx::types;

using String = lib::String;
using StringView = lib::StringView;
using Props = dprops::Props;
using Summary = summary::Summary;
using Skin = skin::Skin;
using SkinPtr = Skin::Ptr;
using CardImage = card::CardImage;
using CardImagePtr = lib::Owner<CardImage>;
using Services = lib::Services;
using Parent = std::shared_ptr<lib::Container>;
using Action = lib::Action;

/// Dのテンプレートが受け付ける素材分類。下のImageSelect
/// は列挙・整数の非型値を受け付け、 生成ヘッダーを
/// 読み込まずにmaterialselect::MtType
/// を使う呼び出し元とのソース互換性を保つ。
enum class MtType { CARD, BG_IMG, BGM, SE };

enum class ImageSelectKind { Card, Background };

struct CardMaterialPath {
    std::vector<CardImagePtr> images;
    bool cancel{};
};

struct BackgroundMaterialPath {
    String images;
    bool cancel{};
};

/// 共通の画像選択状態とUI。DがMaterialSelect
/// とImageLayerWindow
/// へ委譲する処理を、 具体的なモ
/// デルで保持し、同等のlib部品
/// ツリーを作る。 画像・パスの全
/// 遷移をコールバックで公開し、OS
/// の部品ハンドルは含めない。
class ImageSelectBase {
public:
    using TextGetter = std::function<String()>;
    using Definitions = std::function<std::vector<String>(bool included)>;
    using CardDefinitionGetter =
        std::function<CardImagePtr(int index, bool included, StringView binary_path)>;
    using CardDefinitionIndex =
        std::function<int(const CardImage& image, bool included)>;
    using BinaryIndex = std::function<int(bool included)>;

    ImageSelectBase(ImageSelectKind kind, bool compact, Parent parent, int style,
                    Props& prop, Summary* summary, int width, int height,
                    structs::CInsets insets,
                    types::CardImagePosition default_position,
                    bool can_include, TextGetter save_name = {},
                    Action refresh = {}, Definitions definitions = {},
                    bool is_menu_card = false, SkinPtr force_skin = {},
                    Services services = {});
    ImageSelectBase(ImageSelectKind kind, bool compact, Parent parent, int style,
                    Props& prop, Summary* summary, int width, int height,
                    structs::CInsets insets,
                    types::CardImagePosition default_position,
                    bool can_include, TextGetter save_name, Action refresh,
                    Definitions definitions, bool is_menu_card, SkinPtr force_skin,
                    Services services, bool combo_file_selector);
    ImageSelectBase(ImageSelectKind kind, bool compact, Props& prop,
                    Summary* summary, int width, int height,
                    structs::CInsets insets,
                    types::CardImagePosition default_position,
                    bool can_include, TextGetter save_name = {},
                    Action refresh = {}, Definitions definitions = {},
                    bool is_menu_card = false, SkinPtr force_skin = {},
                    Services services = {});
    ImageSelectBase(ImageSelectKind kind, bool compact, Props& prop,
                    Summary* summary, int width, int height,
                    structs::CInsets insets,
                    types::CardImagePosition default_position,
                    bool can_include, TextGetter save_name, Action refresh,
                    Definitions definitions, bool is_menu_card, SkinPtr force_skin,
                    Services services, bool combo_file_selector);
    ImageSelectBase(const ImageSelectBase&) = delete;
    ImageSelectBase& operator=(const ImageSelectBase&) = delete;
    ImageSelectBase(ImageSelectBase&&) = delete;
    ImageSelectBase& operator=(ImageSelectBase&&) = delete;
    ~ImageSelectBase();

    /// 選択パスまたはレイヤーが変わったときに呼ぶ。
    std::vector<Action> modEvent;
    /// プレビューに影響する変更の後に呼ぶ。
    std::vector<Action> updateImageEvent;
    /// レイヤー編集から追加されたとき、新しいレイヤーを作る。
    std::function<CardImagePtr()> createDefaultItem;

    void setPreviewSize(int width, int height, structs::CInsets insets);
    /// D互換のモードレス画像一覧のネイティブ所有者。 ImageSelect
    /// の直接の親は通常GroupBoxなので、部品ツリ
    /// ーだけでは所有ダイアログを確実に取得できない。
    void imageListOwner(lib::Owner<lib::Window> value) noexcept {
        image_list_owner_ = std::move(value);
    }
    void bindEditorCommons(commons::Commons* value);
    void mask(bool value);
    [[nodiscard]] bool mask() const noexcept { return mask_; }
    void indexOfBinPath(BinaryIndex callback) { index_of_binary_ = std::move(callback); }
    void valueFromDef(CardDefinitionGetter callback) {
        value_from_definition_ = std::move(callback);
    }
    void valueToDef(CardDefinitionIndex callback) {
        value_to_definition_ = std::move(callback);
    }

    [[nodiscard]] std::vector<CardImagePtr> cardImages() const;
    void setCardImages(std::vector<CardImagePtr> paths, bool store = false);
    [[nodiscard]] CardMaterialPath cardMaterialPath(bool force_export);
    [[nodiscard]] bool isIncluded() const;
    [[nodiscard]] String cachedIncludedImage() const;

    [[nodiscard]] String backgroundImage() const { return image_path_; }
    [[nodiscard]] String backgroundFilePath() const;
    void setBackgroundImage(String path, bool store = true);
    [[nodiscard]] BackgroundMaterialPath backgroundMaterialPath(bool force_export);

    [[nodiscard]] Parent widget() const noexcept { return group_; }
    [[nodiscard]] const std::shared_ptr<lib::ImageView>& previewControl() const
        noexcept { return preview_; }
    [[nodiscard]] lib::Point sampleSize() const noexcept {
        return {static_cast<float>(width_), static_cast<float>(height_)};
    }
    [[nodiscard]] const std::shared_ptr<lib::ComboBox>& dirsCombo() const noexcept {
        return dirs_;
    }
    [[nodiscard]] const std::shared_ptr<lib::TableView>& fileList() const noexcept {
        return files_;
    }
    [[nodiscard]] const std::shared_ptr<lib::ComboBox>& fileCombo() const noexcept {
        return file_combo_;
    }
    [[nodiscard]] const std::shared_ptr<incsearch::IncSearch>& incrementalSearchControl() const noexcept {
        return search_popup_;
    }
    void startIncSearch();
    // ALL のメニューカードなどでは、縦の領域を節約す
    // るためファイル一覧を一行分縮め、レイヤー名とプレビ
    // ューの余白を詰める。一覧から減らした論理高を返す。
    float compactForAll();
    [[nodiscard]] bool canIncSearch() const noexcept;
    void loadScaledImage(std::function<bool()> callback) {
        load_scaled_image_ = std::move(callback);
    }
    void refreshList();
    void selectDir(int selection);
    [[nodiscard]] std::vector<String> warnings() const;
    [[nodiscard]] std::vector<String> warningsWith(bool legacy,
                                                     StringView data_version,
                                                     const Skin& skin) const;
    [[nodiscard]] std::vector<String> warningFrom(StringView image, bool legacy,
                                                    const Skin& skin) const;
    void active(bool value);
    void enabled(bool value);
    [[nodiscard]] bool enabled() const noexcept { return enabled_; }
    void selectDirImpl(int selection);
    void refreshImageList();
    void refreshWithoutImageList();
    void refresh();
    [[nodiscard]] SkinPtr summSkin() const noexcept;

    /// 現在のレイヤー添字を保ちながら、レイヤー編集モー
    /// ドを開閉する。 追加・削除・並べ替えは、DWTの
    /// ImageLayerWindow操作に対応する。
    void editLayers();
    [[nodiscard]] bool layersEditing() const noexcept { return layers_editing_; }
    [[nodiscard]] const std::shared_ptr<
        ::cwxeditor::cwx::editor::gui::dwt::imagelayer::ImageLayerWindow>& layerWindow() const noexcept {
        return layer_window_;
    }
    [[nodiscard]] std::size_t imageIndex() const noexcept { return image_index_; }
    void imageIndex(std::size_t value);
    bool addLayer();
    bool removeLayer();
    bool moveLayer(std::ptrdiff_t delta);

    void store();
    bool undo();
    bool redo();
    void refUndoMax();

    [[nodiscard]] ImageSelectKind kind() const noexcept { return kind_; }
    [[nodiscard]] bool canInclude() const noexcept { return can_include_; }
    [[nodiscard]] bool isMenuCard() const noexcept { return is_menu_card_; }
    [[nodiscard]] bool compact() const noexcept { return compact_; }
    [[nodiscard]] bool noCardSizeImage() const noexcept { return no_card_size_image_; }
    void noCardSizeImage(bool value);
    [[nodiscard]] bool excludeCardSizeImage() const noexcept {
        return !cardKind() && prop_ && prop_->var().excludeCardSizeImage.value;
    }
    void excludeCardSizeImage(bool value);

protected:
    [[nodiscard]] bool cardKind() const noexcept { return kind_ == ImageSelectKind::Card; }
    void notifyModified(bool preview_changed = true);
    void selectPath(String path, bool store = true);
    void addExternalFiles(const std::vector<String>& paths);
    void openImageList();
    void closeImageList();
    void saveIncludedImage();

private:
    // 選択部品はスタック・メンバーに置かれることが多く、ネイティブのモデルはそれより長く
    // 残り得る。 コールバックから部品を保持したり、保護のないthisを残したりしない。
    struct CallbackLifetime {
        bool active{true};
        ImageSelectBase* owner{};
        std::uint64_t notification_revision{};
        std::uint64_t preview_revision{};
        std::uint64_t list_revision{};
        std::uint64_t save_revision{};
    };
    std::shared_ptr<CallbackLifetime> callback_lifetime_ =
        std::make_shared<CallbackLifetime>();

    template<class Callback>
    auto guardCallback(Callback callback) const {
        const std::weak_ptr<CallbackLifetime> weak = callback_lifetime_;
        return [weak, callback = std::move(callback)](auto&&... arguments) {
            const auto lifetime = weak.lock();
            if (!lifetime || !lifetime->owner || !lifetime->active) return;
            // 通知で、このクロージャーを持つ部品が破棄される場合がある。
            auto invoke = callback;
            invoke(std::forward<decltype(arguments)>(arguments)...);
        };
    }
    void publishChange(bool modified, bool preview_changed, bool refresh_list);
    void syncLayerWindow();
    void applyLayerWindowImages();

    struct Snapshot {
        std::vector<CardImagePtr> paths;
        std::vector<String> binary_paths;
        String image_path;
        std::size_t image_index{};
    };

    ImageSelectKind kind_;
    bool compact_{};
    bool read_only_{};
    bool can_include_{};
    bool is_menu_card_{};
    bool enabled_{true};
    bool mask_{true};
    bool no_card_size_image_{};
    bool exclude_card_size_image_{};
    bool layers_editing_{};
    mutable bool can_inc_search_{};
    int width_{};
    int height_{};
    structs::CInsets insets_{};
    types::CardImagePosition default_position_{};
    Props* prop_{};
    Summary* summary_{};
    Services services_{};
    commons::Commons* editor_commons_{};
    std::weak_ptr<const int> editor_commons_lifetime_;
    Action disconnect_material_notifications_;
    std::uint64_t material_binding_revision_{};
    void materialPathsChanged(bool enumerate, bool notify_refresh);
    void materialPathRenamed(String from, String to, bool directory);
    Parent parent_{};
    lib::Owner<lib::Window> image_list_owner_{};
    Parent group_{};
    std::shared_ptr<lib::ImageView> preview_{};
    std::shared_ptr<lib::Label> layer_name_{};
    std::weak_ptr<lib::Panel> right_panel_{};
    std::weak_ptr<lib::Panel> left_panel_{};
    std::weak_ptr<lib::Widget> preview_top_spacer_{};
    std::shared_ptr<lib::ComboBox> dirs_{};
    std::shared_ptr<lib::TableView> files_{};
    std::shared_ptr<lib::ComboBox> file_combo_{};
    std::shared_ptr<lib::CheckBox> option_{};
    std::shared_ptr<lib::Button> image_list_button_{};
    std::shared_ptr<lib::Button> add_button_{};
    std::shared_ptr<lib::Button> remove_button_{};
    std::shared_ptr<lib::Button> up_button_{};
    std::shared_ptr<lib::Button> down_button_{};
    std::shared_ptr<lib::Button> refresh_button_{};
    std::shared_ptr<lib::Button> directory_button_{};
    std::shared_ptr<lib::Button> save_button_{};
    std::shared_ptr<lib::Button> layer_button_{};
    // 一覧の操作中に、提供元・選択の通知が一覧を閉じたり置き換
    // えたりする場合がある。 その操作のラッパーを保持する。
    std::shared_ptr<imagelistwindow::ImageListWindowBase> image_list_{};
    std::shared_ptr<incsearch::IncSearch> search_popup_{};
    std::shared_ptr<lib::Menu> context_menu_{};
    std::shared_ptr<lib::Menu> image_list_menu_{};
    // Dの画像レイヤー編集はモードレス。 ボタン通知から戻った
    // 時点で消えないよう、ネイティブ画面を選択部品が所有する。
    std::shared_ptr<::cwxeditor::cwx::editor::gui::dwt::imagelayer::ImageLayerWindow> layer_window_{};
    std::vector<std::shared_ptr<lib::OwnedImageResource>> preview_owned_resources_;
    SkinPtr force_skin_{};
    SkinPtr summ_skin_{};
    TextGetter save_name_{};
    Action refresh_callback_{};
    Definitions definitions_{};
    std::function<bool()> load_scaled_image_{};
    BinaryIndex index_of_binary_{};
    CardDefinitionGetter value_from_definition_{};
    CardDefinitionIndex value_to_definition_{};
    std::vector<CardImagePtr> paths_{};
    bool first_card_set_{true};
    bool card_publication_pending_{};
    // DのMaterialSelect._binPathsはレイヤーごとに保持し、現在の
    // パスと独立している。 通常の画像一覧やNoneの選択で、元のバイト列を消さない。
    std::vector<String> binary_paths_{};
    String image_path_{};
    std::vector<String> directories_{};
    struct DirectoryChoice {
        String label;
        String value;
        std::vector<String> sources;
        enum class Kind { Scenario, Skin, Engine } kind{Kind::Scenario};
    };
    std::vector<DirectoryChoice> directory_choices_{};
    std::vector<String> showing_paths_{};
    // 表示ラベルとパス値を分け、全件一覧のディレクトリ
    // 接頭辞で選択素材のパスが変わらないようにする。
    std::vector<String> showing_labels_{};
    std::vector<std::size_t> showing_directory_indices_{};
    std::vector<String> definition_values_{};
    std::size_t image_index_{};
    int old_directory_selection_{-1};
    bool combo_file_selector_{};
    std::vector<Snapshot> undo_stack_{};
    std::vector<Snapshot> redo_stack_{};

    bool addDroppedFiles(const std::vector<std::filesystem::path>& files);
    void bindDropTarget();
    std::uint64_t drop_generation_{};
    void build();
    void updateControls();
    void updatePreview();
    void updateRows();
    void createIncrementalSearch(const lib::WidgetPtr& host);
    void bindFileActions(const lib::WidgetPtr& host, bool child_list = false);
    void refreshFileListMenu();
    [[nodiscard]] String selectedFileValue() const;
    [[nodiscard]] std::filesystem::path selectedFilePath() const;
    [[nodiscard]] bool canOpenSelectedFile() const;
    void openSelectedFile();
    void copySelectedFilePath();
    void includeSelectedImage();
    void openMaterialDirectory();
    void refreshListImpl(bool restore_image_directory);
    struct PreparedRows {
        std::vector<lib::ChoiceItem> choices;
        std::optional<std::size_t> choice_selection;
        std::vector<lib::TableRow> rows;
        std::vector<std::size_t> selection;
    };
    [[nodiscard]] std::optional<PreparedRows> prepareRows(
        const std::vector<String>& paths, const std::vector<String>& labels,
        const std::vector<String>& definitions, std::uint64_t revision) const;
    [[nodiscard]] bool publishRows(PreparedRows rows);
    void selectFileRow(std::size_t row);
    void clearPreviewResource() noexcept;
    [[nodiscard]] Snapshot snapshot() const;
    void restoreSnapshot(const Snapshot& value);
    [[nodiscard]] CardImagePtr cloneImage(const CardImagePtr& value,
                                          bool undo_snapshot) const;
    [[nodiscard]] std::vector<String> directoryPaths() const;
    [[nodiscard]] std::vector<DirectoryChoice> directoryChoices() const;
    [[nodiscard]] std::vector<String> pathsForDirectory(StringView directory) const;
    [[nodiscard]] bool exportBinary(StringView source, String& result) const;
    [[nodiscard]] lib::MessageResult askExternalize() const;
};

/// 元のDの非型テンプレート形式を保つC++20ラッパー。
/// Combo/Tableを指定した呼び出し元向けにCを残し
/// 、CompactはDと同じプレビューなしの配置を選ぶ。
template <auto Type, typename C = lib::TableView, bool Compact = false>
class ImageSelect final : public ImageSelectBase {
public:
    static constexpr bool card_type = static_cast<int>(Type) == 0;

    using Base = ImageSelectBase;
    using TextGetter = typename Base::TextGetter;
    using Definitions = typename Base::Definitions;
    using Base::Base;
    using Base::backgroundFilePath;
    using Base::backgroundImage;
    using Base::cardImages;
    using Base::cardMaterialPath;
    using Base::setBackgroundImage;
    using Base::setCardImages;

    ImageSelect(Parent parent, int style, Props& prop, Summary* summary,
                int width, int height, structs::CInsets insets,
                types::CardImagePosition default_position, bool can_include,
                TextGetter save_name = {}, Action refresh = {},
                Definitions definitions = {}, bool is_menu_card = false,
                SkinPtr force_skin = {}, Services services = {})
        : Base(card_type ? ImageSelectKind::Card : ImageSelectKind::Background,
               Compact, std::move(parent), style, prop, summary, width, height,
               insets, default_position, can_include, std::move(save_name),
               std::move(refresh), std::move(definitions), is_menu_card,
               std::move(force_skin), std::move(services)) {}

    ImageSelect(Parent parent, int style, Props& prop, Summary& summary,
                int width, int height, structs::CInsets insets,
                types::CardImagePosition default_position, bool can_include,
                TextGetter save_name = {}, Action refresh = {},
                Definitions definitions = {}, bool is_menu_card = false,
                SkinPtr force_skin = {}, Services services = {})
        : ImageSelect(std::move(parent), style, prop, &summary, width, height,
                      insets, default_position, can_include,
                      std::move(save_name), std::move(refresh),
                      std::move(definitions), is_menu_card,
                      std::move(force_skin), std::move(services)) {}

    ImageSelect(Props& prop, Summary* summary, int width, int height,
                structs::CInsets insets,
                types::CardImagePosition default_position, bool can_include,
                TextGetter save_name = {}, Action refresh = {},
                Definitions definitions = {}, bool is_menu_card = false,
                SkinPtr force_skin = {}, Services services = {})
        : Base(card_type ? ImageSelectKind::Card : ImageSelectKind::Background,
               Compact, prop, summary, width, height, insets, default_position,
               can_include, std::move(save_name), std::move(refresh),
               std::move(definitions), is_menu_card, std::move(force_skin),
               std::move(services)) {}

    [[nodiscard]] std::vector<CardImagePtr> images() const requires(card_type) {
        return cardImages();
    }
    void images(std::vector<CardImagePtr> value) requires(card_type) {
        setCardImages(std::move(value), false);
    }
    void setImages(std::vector<CardImagePtr> value, bool store_value)
        requires(card_type) {
        setCardImages(std::move(value), store_value);
    }
    [[nodiscard]] CardMaterialPath materialPath(bool force_export)
        requires(card_type) {
        return cardMaterialPath(force_export);
    }

    [[nodiscard]] String images() const requires(!card_type) {
        return backgroundImage();
    }
    [[nodiscard]] String filePath() const requires(!card_type) {
        return backgroundFilePath();
    }
    void image(String value) requires(!card_type) {
        setBackgroundImage(std::move(value), true);
    }
    [[nodiscard]] BackgroundMaterialPath materialPath(bool force_export)
        requires(!card_type) {
        return backgroundMaterialPath(force_export);
    }
};

}  // namespace cwxeditor::cwx::editor::gui::dwt::imageselect
