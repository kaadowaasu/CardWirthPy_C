#include "cwxeditor_src/lib/service_defaults.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/commons.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/imageselect.hpp"
#include "cwxeditor_src/lib/file_save_transaction.hpp"
#include "cwxeditor_src/lib/file_picker.hpp"
#include "cwxeditor_src/lib/storage.hpp"
#include "cwxeditor_src/lib/ui_actions.hpp"
#include "cwxeditor_src/lib/ui_metrics.hpp"

#include "cwxeditor_src/core/cwx/imagesize.hpp"
#include "cwxeditor_src/core/cwx/utils.hpp"
#include "cwxeditor_src/core/cwx/versioninfo.hpp"
#include "cwxeditor_src/core/cwx/warning.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/areaviewutils.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dskin.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/imagelayer.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/materialselect.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <iterator>
#include <limits>
#include <span>
#include <stdexcept>
#include <utility>

namespace cwxeditor::cwx::editor::gui::dwt::imageselect {
namespace {

using ::cwxeditor::cwx::utils::isBinImg;
namespace layer = ::cwxeditor::cwx::editor::gui::dwt::imagelayer;
using ::cwxeditor::cwx::structs::pathToUtf8;

constexpr int portableReadOnlyStyle = 0x00800000;

bool isTalkerImagePath(StringView path) {
    const auto separator = path.find_last_of("/\\");
    const auto name = separator == StringView::npos ? path : path.substr(separator + 1);
    return name == "??Selected" || name == "??Unselected" ||
           name == "??Random" || name == "??Card" || name == "??Valued";
}

Services resolveServices(const Props& prop, Services supplied) {
    return lib::with_service_defaults(prop.images().services(), std::move(supplied));
}

template <typename Component>
std::shared_ptr<Component> makeComponent(const Services& services,
                                         const lib::ComponentKind kind) {
    if (services.gui) {
        if (auto candidate = services.gui->create_component(kind)) {
            if (auto component = std::dynamic_pointer_cast<Component>(candidate)) {
                return component;
            }
        }
    }
    return std::make_shared<Component>();
}

String fileLabel(StringView value) {
    if (value.empty()) return {};
    const String name = structs::pathToUtf8(structs::pathFromUtf8(value).filename());
    return name.empty() ? String(value) : name;
}

bool sameString(StringView left, StringView right) {
    // バイナリ画像の参照は内部を解釈せず、完全に同じ識別情報を保持する。
    if (isBinImg(left) || isBinImg(right)) return left == right;
    return ::cwxeditor::cwx::utils::cfnmatch(left, right);
}

bool startsWithFilenameIdentity(StringView value, StringView prefix) {
    if (isBinImg(value) || isBinImg(prefix)) return false;
    // D版と同様、パス要素の境界を調べずに startsWith をそのまま使う（A は AB にも一致する）。
    return value.size() >= prefix.size() &&
        sameString(value.substr(0, prefix.size()), prefix);
}

bool hasModifier(const lib::Modifiers value, const lib::Modifiers bit) noexcept {
    return (static_cast<std::uint32_t>(value) & static_cast<std::uint32_t>(bit)) != 0U;
}

constexpr StringView skinDirectoryKey{"\x1f" "skin-materials"};
constexpr StringView engineDirectoryKey{"\x1f" "engine-materials"};

void appendUnique(std::vector<String>& values, String value) {
    if (value.empty()) return;
    if (std::find(values.begin(), values.end(), value) == values.end()) {
        values.push_back(std::move(value));
    }
}

void appendUniqueMessages(std::vector<String>& values,
                          const std::vector<String>& additions) {
    for (const auto& value : additions) appendUnique(values, value);
}

imagelistwindow::ImageDataWithScale convertImage(
    areaviewutils::ImageDataWithScale source) {
    imagelistwindow::ImageDataWithScale result;
    result.data = std::move(source.data);
    result.resource = source.resource;
    result.logical_size = source.logical_size;
    result.source_scale = source.source_scale;
    result.loaded = source.loaded;
    result.owned_resource = source.owned_resource;
    return result;
}

}  // namespace

ImageSelectBase::ImageSelectBase(
    const ImageSelectKind kind, const bool compact, Parent parent, const int style,
    Props& prop, Summary* summary, const int width, const int height,
    const structs::CInsets insets, const types::CardImagePosition default_position,
    const bool can_include, TextGetter save_name, Action refresh,
    Definitions definitions, const bool is_menu_card, SkinPtr force_skin,
    Services services)
    : ImageSelectBase(kind, compact, std::move(parent), style, prop, summary,
                      width, height, insets, default_position, can_include,
                      std::move(save_name), std::move(refresh),
                      std::move(definitions), is_menu_card, std::move(force_skin),
                      std::move(services), false) {}

ImageSelectBase::ImageSelectBase(
    const ImageSelectKind kind, const bool compact, Parent parent, const int style,
    Props& prop, Summary* summary, const int width, const int height,
    const structs::CInsets insets, const types::CardImagePosition default_position,
    const bool can_include, TextGetter save_name, Action refresh,
    Definitions definitions, const bool is_menu_card, SkinPtr force_skin,
    Services services, const bool combo_file_selector)
    // compact_ は呼び出し元だけが決める。ALL で強制すると
    // 、 すべてのカードダイアログから画像プレビューが消えてしまう。
    : kind_(kind), compact_(compact),
      read_only_((style == 1) || ((style & portableReadOnlyStyle) != 0)),
      can_include_(can_include), is_menu_card_(is_menu_card),
      exclude_card_size_image_(kind == ImageSelectKind::Background &&
                               prop.var().excludeCardSizeImage.value),
      width_(width), height_(height), insets_(insets), default_position_(default_position),
      prop_(&prop), summary_(summary), services_(resolveServices(prop, std::move(services))),
      parent_(std::move(parent)),
      force_skin_(std::move(force_skin)), save_name_(std::move(save_name)),
      refresh_callback_(std::move(refresh)), definitions_(std::move(definitions)),
      combo_file_selector_(combo_file_selector) {
    callback_lifetime_->owner = this;
    if (force_skin_) summ_skin_ = force_skin_;
    if (!summ_skin_ && summary_ && !summary_->type().empty()) {
        summ_skin_ = dskin::findSkin2(*prop_, summary_->type(), summary_->skinName());
    }
    if (!summ_skin_) {
        summ_skin_ = dskin::findSkin2(*prop_, prop_->var().defaultSkin.value,
                                      prop_->var().defaultSkinName.value);
    }
    if (kind_ == ImageSelectKind::Card) {
        // D版の CARD 用 MaterialSelect と同様、最初に空の File
        // を一つ置く。 レイヤー編集の追加・削除の初期条件も同じ状態にする。
        paths_.push_back(std::make_shared<CardImage>(
            String{}, types::CardImagePosition::Default));
        binary_paths_.emplace_back();
    }
    build();
}

ImageSelectBase::ImageSelectBase(
    const ImageSelectKind kind, const bool compact, Props& prop, Summary* summary,
    const int width, const int height, const structs::CInsets insets,
    const types::CardImagePosition default_position, const bool can_include,
    TextGetter save_name, Action refresh, Definitions definitions,
    const bool is_menu_card, SkinPtr force_skin, Services services)
    : ImageSelectBase(kind, compact, prop, summary, width, height, insets,
                      default_position, can_include, std::move(save_name),
                      std::move(refresh), std::move(definitions), is_menu_card,
                      std::move(force_skin), std::move(services), false) {}

ImageSelectBase::ImageSelectBase(
    const ImageSelectKind kind, const bool compact, Props& prop, Summary* summary,
    const int width, const int height, const structs::CInsets insets,
    const types::CardImagePosition default_position, const bool can_include,
    TextGetter save_name, Action refresh, Definitions definitions,
    const bool is_menu_card, SkinPtr force_skin, Services services,
    const bool combo_file_selector)
    : ImageSelectBase(kind, compact, Parent{}, 0, prop, summary, width, height,
                      insets, default_position, can_include,
                      std::move(save_name), std::move(refresh),
                      std::move(definitions), is_menu_card, std::move(force_skin),
                       std::move(services), combo_file_selector) {
}

ImageSelectBase::~ImageSelectBase() {
    callback_lifetime_->owner = nullptr;
    ++drop_generation_;
    if (services_.drag_drop && files_) services_.drag_drop->unbind(files_);
    ++callback_lifetime_->notification_revision;
    ++material_binding_revision_;
    if (disconnect_material_notifications_) disconnect_material_notifications_();
    if (dirs_) dirs_->on_selection_changed = {};
    if (files_) {
        files_->on_selection_changed = {};
        files_->on_cell_activate = {};
    }
    if (file_combo_) file_combo_->on_selection_changed = {};
    if (option_) option_->on_changed = {};
    for (const auto& button : {image_list_button_, add_button_, remove_button_,
             up_button_, down_button_, refresh_button_, directory_button_,
             save_button_, layer_button_}) {
        if (button) button->on_click = {};
    }
    // 明示的な終了では失敗を通知する。一方、破棄時はGUIの提供側が終了
    // を拒否しても、 プレビューと参照回数カウンターの解放を完了する。
    try { closeImageList(); } catch (...) {}
    try {
        if (layer_window_) layer_window_->close();
        layer_window_.reset();
    } catch (...) {}
    clearPreviewResource();
    for (const auto& value : paths_)
        if (value) value->removeUseCounter();
}

void ImageSelectBase::build() {
    // MessageDialog が使うD版の Combo 特殊
    // 化は、タブページに直接埋め込む。 「Image」の見出し付
    // き Group を持つのは Table 特殊化だけ。Combo
    // も別のネイティブの GroupBox で囲むと、余分な
    // 枠と見出しが増え、話者・メッセージ欄がD版より狭くなる。
    if (compact_ || combo_file_selector_) {
        group_ = makeComponent<lib::Panel>(services_, lib::ComponentKind::Panel);
    } else {
        const auto group = makeComponent<lib::GroupBox>(services_, lib::ComponentKind::GroupBox);
        group->title = prop_->requiredMsgs().image.value;
        group_ = group;
    }
    if (!group_) group_ = std::make_shared<lib::Panel>();
    if (parent_) parent_->add(group_);

    auto layout = std::make_shared<lib::GridLayout>();
    // MessageDialog の Combo 特殊化はD版と同様に
    // 縦に並べる。 ディレクトリのコンボ、ファイルのコンボ、中央寄せの
    // プレビューと操作欄の順に配置する。 カードダイアログの Table
    // 特殊化は、従来どおり左にプレビュー、右にファイル一覧を置く。
    layout->columns = (compact_ || combo_file_selector_) ? 1U : 2U;
    layout->equal_width = false;
    layout->spacing = compact_ ? 0.0F : 5.0F;
    if (!compact_ && !combo_file_selector_) {
        // D版の ImageSelect(Table) と同様に normalGridLayout(2,false)
        // を使い、 ネイティブのグループの余白を5px、縦方向の行間をゼロにする。
        layout->padding = {5.0F, 5.0F, 5.0F, 5.0F};
    }
    group_->set_layout(layout);
    group_->set_layout_item({.horizontal = lib::Align::Stretch,
                             .vertical = lib::Align::Stretch,
                             .grow = 1.0F});

    auto makePanel = [this](const lib::Orientation orientation) {
        auto panel = makeComponent<lib::Panel>(services_, lib::ComponentKind::Panel);
        auto panel_layout = std::make_shared<lib::LinearLayout>(orientation);
        panel_layout->spacing = 3.0F;
        panel->set_layout(panel_layout);
        return panel;
    };
    auto left = makePanel(lib::Orientation::Vertical);
    auto right = makePanel(lib::Orientation::Vertical);
    left_panel_ = left;
    right_panel_ = right;
    // D版の ImageSelect.setPreviewSize() と同様
    // 、カード寸法に画像の上下左右の余白を加える。 これらの余白はゲーム描画用
    // の付加情報だけでなく、表示されるプレビューのキャンバスの一部でもある。
    const float preview_width = static_cast<float>(width_ + insets_.w + insets_.e);
    const float preview_height = static_cast<float>(height_ + insets_.n + insets_.s);
    left->set_layout_item({.row = combo_file_selector_ ? 1U : 0U,
                           .column = 0U,
                           // D版と同様、プレビュー列には GridData(FILL_VERTICAL)
                           // を使う。 横方向にも伸ばすと、カードプレビ
                           // ューとファイル一覧の間隔が変わってしまう。
                           .horizontal = combo_file_selector_
                                             ? lib::Align::Stretch
                                             : lib::Align::Start,
                           .vertical = lib::Align::Stretch,
                           .minimum = {preview_width + 12.0F, preview_height},
                           // この列には文字やツールバーも含まれる。 画像の表
                           // 示領域を固定するのは、プレビュー自体だけとする。
                           .grow = combo_file_selector_ ? 1.0F : 0.0F});
    right->set_layout_item({.row = 0U,
                            .column = (compact_ || combo_file_selector_) ? 0U : 1U,
                            // 既定値以外のセル結合数を使い、共通の GridData
                            // モデル内で (0,0) を明示的に保持する。 両ネイテ
                            // ィブバックエンドで、Combo の一列の範囲に収める。
                            .column_span = combo_file_selector_ ? 2U : 1U,
                            .horizontal = lib::Align::Stretch,
                            .vertical = lib::Align::Stretch,
                            .minimum = {
                                static_cast<float>(prop_->var().filesWidth.value),
                                combo_file_selector_
                                    ? 52.0F
                                    : static_cast<float>(prop_->var().filesHeight.value)},
                            .grow = combo_file_selector_ ? 0.0F : 1.0F});
    if (!compact_) group_->add(left);
    group_->add(right);

    if (!compact_) {
        if (cardKind()) {
            layer_name_ = makeComponent<lib::Label>(services_,
                                                    lib::ComponentKind::Label);
            layer_name_->alignment = lib::TextAlignment::Center;
            left->add(layer_name_);
        }
        preview_ = makeComponent<lib::ImageView>(services_, lib::ComponentKind::ImageView);
        preview_->scale_mode = lib::ImageScaleMode::Fit;
        if(cardKind()) preview_->design_size=lib::Size{preview_width,preview_height};
        preview_->set_bounds({0.0F, 0.0F, preview_width, preview_height});
        if (cardKind()) {
            // D版と同様、固定サイズのカード用キャンバスを中央に置く。 最小サイズ
            // がゼロの Spacer で四方の余りを吸収し、 仮の Panel
            // の推奨高さを持ち込んだり、設計上の画素寸法を拡大縮小したりしない。
            auto preview_host = makeComponent<lib::Panel>(
                services_, lib::ComponentKind::Panel);
            auto preview_layout = std::make_shared<lib::GridLayout>();
            preview_layout->columns = 3U;
            preview_host->set_layout(preview_layout);
            preview_host->set_layout_item({.horizontal = lib::Align::Stretch,
                                           .vertical = lib::Align::Stretch,
                                           .minimum = {preview_width, preview_height},
                                           .grow = 1.0F});
            const auto spacer = [&preview_host](const std::size_t row,
                                                const std::size_t column,
                                                const float grow) {
                auto value = std::make_shared<lib::Spacer>();
                // ALL では寸法未指定の Spacer が汎用の 160px
                // の表示領域として計測され、 プレビュー列とその
                // 更新ボタン行が不必要に広くなる。 余りを吸収する
                // だけなので、自然サイズには最小限の値を与える。
                const lib::Size natural = lib::editor_window_host_options().all_mode
                    ? lib::Size{1.0F, 1.0F} : lib::Size{};
                value->set_layout_item({.row = row, .column = column,
                                        .horizontal = lib::Align::Stretch,
                                        .vertical = lib::Align::Stretch,
                                        .preferred = natural,
                                        .grow = grow});
                preview_host->add(value);
                return value;
            };
            preview_top_spacer_ = spacer(0U, 1U, 1.0F);
            spacer(1U, 0U, 1.0F);
            preview_->set_layout_item({.row = 1U, .column = 1U,
                                       .horizontal = lib::Align::Center,
                                       .vertical = lib::Align::Center,
                                       .minimum = {},
                                       .preferred = {}});
            preview_host->add(preview_);
            spacer(1U, 2U, 1.0F);
            spacer(2U, 1U, 1.0F);
            left->add(preview_host);
        } else {
            preview_->set_layout_item({.horizontal = lib::Align::Stretch,
                                       .vertical = lib::Align::Stretch,
                                       .minimum = {preview_width, preview_height},
                                       .preferred = {preview_width, preview_height},
                                       .grow = 1.0F});
            left->add(preview_);
        }
    }

    auto directory_row = makeComponent<lib::Panel>(services_, lib::ComponentKind::Panel);
    auto directory_layout = std::make_shared<lib::GridLayout>();
    // ディレクトリ、埋め込み画像の保存、レイヤー編集の列位置を固定する。
    // 非表示の保存ボタンはゼロサイズのグリッド項目とし、領域を消費しない。
    directory_layout->columns = cardKind() ? 3U : 2U;
    directory_layout->spacing = 3.0F;
    directory_row->set_layout(directory_layout);
    directory_row->set_layout_item({.horizontal = lib::Align::Stretch});
    right->add(directory_row);

    dirs_ = makeComponent<lib::ComboBox>(services_, lib::ComponentKind::ComboBox);
    dirs_->set_layout_item({.row = 0U, .column = 0U,
                            .horizontal = lib::Align::Stretch,
                            .grow = 1.0F});
    dirs_->on_selection_changed = guardCallback([this](const std::optional<std::size_t> value) {
        if (enabled_ && !read_only_) selectDir(value ? static_cast<int>(*value) : -1);
    });
    directory_row->add(dirs_);

    auto makeButton = [this](const std::shared_ptr<lib::Container>& parent,
                             String label, lib::ResourceId icon,
                             String tooltip, Action callback) {
        auto button = makeComponent<lib::Button>(services_, lib::ComponentKind::Button);
        button->text = std::move(label);
        button->icon = icon;
        button->set_tooltip(std::move(tooltip));
        button->on_click = guardCallback(std::move(callback));
        parent->add(button);
        return button;
    };

    save_button_ = makeButton(
        directory_row, {}, prop_->images().load(dprops::StringView{"save_inc_img.png"}),
        prop_->requiredMsgs().menuText(types::MenuID::SaveImage), [this] {
            if (enabled_) saveIncludedImage();
        });
    save_button_->set_layout_item({.row = 0U, .column = 1U,
                                   .horizontal = lib::Align::Center,
                                   .vertical = lib::Align::Center,
                                   .minimum = combo_file_selector_
                                                  ? lib::Size{}
                                                  : lib::Size{27.0F, 26.0F},
                                   .preferred = combo_file_selector_
                                                    ? lib::Size{}
                                                    : lib::Size{27.0F, 26.0F}});
    save_button_->set_visible(false);
    if (cardKind()) {
        layer_button_ = makeButton(
            directory_row, {}, prop_->images().load(dprops::StringView{"imagelayer_edit.png"}),
            prop_->requiredMsgs().menuText(types::MenuID::EditLayers),
            [this] { editLayers(); });
        layer_button_->set_layout_item({.row = 0U, .column = 2U,
                                        .horizontal = lib::Align::Center,
                                        .vertical = lib::Align::Center,
                                        .minimum = {27.0F, 26.0F},
                                        .preferred = {27.0F, 26.0F}});
    }

    if (combo_file_selector_) {
        file_combo_ = makeComponent<lib::ComboBox>(services_, lib::ComponentKind::ComboBox);
        file_combo_->set_layout_item({.horizontal = lib::Align::Stretch});
        file_combo_->on_selection_changed = guardCallback([this](const std::optional<std::size_t> value) {
            if (enabled_ && !read_only_ && value) selectFileRow(*value);
        });
        right->add(file_combo_);
        createIncrementalSearch(file_combo_);
        bindFileActions(file_combo_);
    } else {
        files_ = makeComponent<lib::TableView>(services_, lib::ComponentKind::TableView);
        files_->columns.push_back({prop_->requiredMsgs().image.value, 1.0F,
                                   lib::TextAlignment::Start, true, false});
        // 元実装(D)との差: スペース節約のため一覧の「イメージ」見出しは
        // ALL・standalone共通で表示しない(materialselect と同じ)。
        files_->header_visible = false;
        files_->on_selection_changed = guardCallback([this](const std::vector<std::size_t>& selection) {
            if (enabled_ && !read_only_ && !selection.empty()) selectFileRow(selection.front());
        });
        files_->on_cell_activate = guardCallback([this](const std::size_t, const std::size_t) {
            // 選択は通常の行選択コールバックで確定する。 D版と同様、ダブルクリックで
            // はOSのエクスプローラーではなく、エディタの FileView を開く。
            openSelectedFile();
        });
        files_->set_layout_item({.horizontal = lib::Align::Stretch,
                                 .vertical = lib::Align::Stretch,
                                 .minimum = {
                                     static_cast<float>(prop_->var().filesWidth.value),
                                     static_cast<float>(prop_->var().filesHeight.value)},
                                 .preferred = {
                                     static_cast<float>(prop_->var().filesWidth.value),
                                     static_cast<float>(prop_->var().filesHeight.value)},
                                 .grow = 1.0F});
        right->add(files_);
        createIncrementalSearch(files_);
        bindFileActions(files_);
        bindDropTarget();
    }

    auto controls = makeComponent<lib::Panel>(services_, lib::ComponentKind::Panel);
    auto controls_layout = std::make_shared<lib::GridLayout>();
    const bool all_mode = lib::editor_window_host_options().all_mode;
    // ALL のフォルダーボタンは、ホストが対応していれば、OSのフ
    // ァイル管理ソフトではなく Data Explorer を開く。
    const bool directory_button = !all_mode || lib::editor_can_open_data_directory();
    controls_layout->columns = directory_button ? 3U : 2U;
    controls_layout->spacing = 3.0F;
    controls->set_layout(controls_layout);
    controls->set_layout_item({.horizontal = lib::Align::Stretch});
    (compact_ ? right : left)->add(controls);

    image_list_button_ = makeButton(
        controls, {}, prop_->images().load(dprops::StringView{"img_list.png"}),
        prop_->requiredMsgs().menuText(types::MenuID::LookImages), [this] {
            if (read_only_ || !enabled_) return;
            if (image_list_ && !image_list_->closed()) closeImageList(); else openImageList();
        });
    image_list_button_->set_layout_item({.horizontal = lib::Align::Center,
                                         .vertical = lib::Align::Center,
                                         .minimum = {27.0F, 26.0F},
                                         .preferred = {27.0F, 26.0F}});
    refresh_button_ = makeButton(
        controls, prop_->requiredMsgs().refreshS.value,
        prop_->images().load(dprops::StringView{"refresh.png"}),
        prop_->requiredMsgs().menuText(types::MenuID::Refresh),
        [this] {
            if (!enabled_) return;
            const auto lifetime = callback_lifetime_;
            const auto revision = lifetime->notification_revision;
            refreshList();
            if (lifetime->owner == this && lifetime->notification_revision == revision)
                refreshWithoutImageList();
        });
    refresh_button_->set_layout_item({.horizontal = lib::Align::Stretch,
                                      .vertical = lib::Align::Center,
                                      .minimum = {54.0F, 26.0F},
                                      .preferred = {54.0F, 26.0F}, .grow = 1.0F});
    if (directory_button) {
        directory_button_ = makeButton(
            controls, {}, prop_->images().load(dprops::StringView{"folder.png"}),
            prop_->requiredMsgs().menuText(types::MenuID::OpenDir),
            [this] { openMaterialDirectory(); });
        directory_button_->set_layout_item({.horizontal = lib::Align::Center,
                                            .vertical = lib::Align::Center,
                                            .minimum = {27.0F, 26.0F},
                                            .preferred = {27.0F, 26.0F}});
    }

    if (cardKind()) {
        option_ = makeComponent<lib::CheckBox>(services_, lib::ComponentKind::CheckBox);
        const auto card_size = prop_->looks().cardSize();
        option_->text = ::cwxeditor::cwx::utils::tryFormat(
            prop_->requiredMsgs().useNoCardSizeImage.value,
            card_size.width, card_size.height);
        option_->on_changed = guardCallback([this](const bool value) { noCardSizeImage(value); });
    } else {
        option_ = makeComponent<lib::CheckBox>(services_, lib::ComponentKind::CheckBox);
        option_->text = prop_->requiredMsgs().excludeCardSizeImage.value;
        option_->on_changed = guardCallback([this](const bool value) { excludeCardSizeImage(value); });
    }
    (compact_ ? right : left)->add(option_);

    if (!definitions_) {
        definitions_ = [this](const bool included) {
            std::vector<String> values{
                prop_->requiredMsgs().defaultSelection(prop_->requiredMsgs().imageNone.value)};
            if (included) values.push_back(
                prop_->requiredMsgs().defaultSelection(prop_->requiredMsgs().imageIncluding.value));
            return values;
        };
    }
    // materialselect.d の getDefs と同様、バイナリ画像
    // を保存できるかではなく、 実際に保存されているバイナリ画像から判定する。
    definition_values_ = definitions_(false);
    if (cardKind()) {
        index_of_binary_ = [this](const bool included) {
            return included ? 1 : -1;
        };
        value_from_definition_ = [](const int index, const bool included,
                                    const StringView binary_path) -> CardImagePtr {
            if (index == 0) return std::make_shared<CardImage>(String{});
            if (included && index == 1 && !binary_path.empty()) {
                return std::make_shared<CardImage>(String(binary_path));
            }
            return {};
        };
        value_to_definition_ = [this](const CardImage& image, const bool included) {
            if (image.type != types::CardImageType::File) return -1;
            if (image.path().empty()) return 0;
            if (included && isBinImg(image.path())) return 1;
            return -1;
        };
    } else {
        index_of_binary_ = [this](const bool included) {
            return included ? 1 : -1;
        };
    }
    updateControls();
    refreshList();
    updatePreview();
}

void ImageSelectBase::setPreviewSize(const int width, const int height,
                                     const structs::CInsets insets) {
    width_ = std::max(width, 0);
    height_ = std::max(height, 0);
    insets_ = insets;
    if (preview_) {
        const auto preview_width = static_cast<float>(
            width_ + insets_.w + insets_.e);
        const auto preview_height = static_cast<float>(
            height_ + insets_.n + insets_.s);
        preview_->set_bounds({0.0F, 0.0F, preview_width, preview_height});
        auto item=preview_->layout_item();
        if(cardKind()) {
            preview_->design_size=lib::Size{preview_width,preview_height};
            item.minimum={}; item.preferred={};
        } else {
            item.minimum={preview_width,preview_height}; item.preferred=item.minimum;
        }
        preview_->set_layout_item(item);
    }
    updatePreview();
}

void ImageSelectBase::mask(const bool value) {
    if (mask_ == value) return;
    const auto lifetime = callback_lifetime_;
    const auto revision = ++lifetime->notification_revision;
    mask_ = value;
    const auto list = image_list_;
    if (list) list->mask(value);
    if (layer_window_ && layer_window_->list()) layer_window_->list()->mask(value);
    if (lifetime->owner != this || lifetime->notification_revision != revision) return;
    updatePreview();
}

std::vector<CardImagePtr> ImageSelectBase::cardImages() const {
    std::vector<CardImagePtr> result;
    if (!cardKind()) return result;
    for (const auto& image : paths_) {
        if (!image) continue;
        switch (image->type) {
        case types::CardImageType::File:
            // 空のレイヤーは編集可能な仮の項目として残すが、D版と同様、
            // 確定するカード画像の値からは空の File をすべて除く。
            if (!image->path().empty()) result.push_back(image);
            break;
        case types::CardImageType::PCNumber:
            if (image->pcNumber() != 0U) result.push_back(image);
            break;
        case types::CardImageType::Talker:
            result.push_back(image);
            break;
        }
    }
    return result;
}

void ImageSelectBase::setCardImages(std::vector<CardImagePtr> paths, const bool store_value) {
    if (!cardKind()) return;
    if (paths.empty()) {
        // D版の setPaths と同様、CARD には空の File を一つ残
        // す。 cardImages() では、確定する画像一覧からそれを除く。
        paths.push_back(std::make_shared<CardImage>(
            String{}, types::CardImagePosition::Default));
    }
    const bool first_set = first_card_set_;
    first_card_set_ = false;
    const bool same_paths = paths_.size() == paths.size() &&
        std::equal(paths_.begin(), paths_.end(), paths.begin(),
            [](const CardImagePtr& first, const CardImagePtr& second) {
                return first == second ||
                       (first && second && first->opEquals(*second));
            });
    // D版と同様、初回以外でパスの値が同じなら保存・更新前に戻る。 firstSet
    // の例外では、保存せずに初期表示を行う。ネイティブのホストの
    // コールバックは、 モデル変更後、ディレクトリや一覧への反映前
    // に例外を出す場合があるため、その場合は同値での再試行を許す
    // 。 正常に反映済みの同値の代入は、D版と同様に何もしない。
    const bool retry_publication = card_publication_pending_;
    if (same_paths && !first_set && !retry_publication) return;
    const auto lifetime = callback_lifetime_;
    if (store_value && !first_set && !same_paths) store();
    const auto revision = ++lifetime->notification_revision;
    auto expected_revision = revision;
    // 通知の実行中は失敗状態ではないため、同値による再入はD版と同様に何
    // もしない。 外へ伝播した失敗がある場合だけ、その後の再試行を許す。
    card_publication_pending_ = false;
    try {
        paths_ = std::move(paths);
        binary_paths_.clear(); binary_paths_.reserve(paths_.size());
        for (const auto& image : paths_)
            binary_paths_.push_back(image && image->type == types::CardImageType::File &&
                isBinImg(image->path()) ? image->path() : String{});
        if (!no_card_size_image_ && prop_) {
            const auto skin = summSkin();
            if (lifetime->owner != this || lifetime->notification_revision != revision) return;
            if (skin) for (const auto& image : paths_) {
                if (!image || image->type != types::CardImageType::File ||
                    image->path().empty() || isBinImg(image->path())) continue;
                const auto path = skin->findImagePath(image->path(),
                    summary_ ? summary_->scenarioPath() : String{},
                    summary_ ? summary_->dataVersion() : versioninfo::LATEST_VERSION);
                std::uint32_t width{}, height{};
                if (!path.empty() && ::cwxeditor::cwx::imagesize::imageSize(structs::pathFromUtf8(path), width, height)) {
                    const auto size = prop_->looks().cardSize();
                    if (width != size.width || height != size.height) { no_card_size_image_ = true; break; }
                }
            }
        }
        if (paths_.empty()) image_index_ = 0;
        else if (image_index_ >= paths_.size()) image_index_ = paths_.size() - 1U;
        // モードレスのレイヤー編集を表示中に、カードダイアログがモデル
        // を更新する場合がある。 続くレイヤー編集が新しいモデル値を上
        // 書きしないよう、エディタ内の切り離された複製を置き換える。
        syncLayerWindow();
        // 別のカードを読み込むときは、その選択画像を含む素材ディレクトリ
        // も復元する。 D版の ImageSelect は refreshPaths()
        // で行う。以前のコンボ選択を残すと、 無関係なスキンデ
        // ィレクトリや、定義の二行しか表示されない状態になる。
        if (!store_value || retry_publication) {
            refreshListImpl(true);
        }
        if (lifetime->owner != this || lifetime->notification_revision != revision) return;
        // 同値での再試行はディレクトリと一覧の状態だけを復旧する。 例外の前
        // に副作用を起こした可能性がある購読先への通知列は、繰り返さない。
        if (!same_paths) {
            // publishChange は、コールバックを呼ぶ前に通知の変更番号を進める。
            expected_revision = revision + 1U;
            notifyModified();
        }
    } catch (...) {
        if (lifetime->owner == this &&
            lifetime->notification_revision == expected_revision)
            card_publication_pending_ = true;
        throw;
    }
}

String ImageSelectBase::cachedIncludedImage() const {
    if (cardKind()) return image_index_ < binary_paths_.size()
        ? binary_paths_[image_index_] : String{};
    return isBinImg(image_path_) ? image_path_ : String{};
}

bool ImageSelectBase::isIncluded() const {
    if (cardKind()) {
        // D版の setPaths は各スロットで埋め込みフラグを上書きし
        // 、最後が埋め込みの場合だけ includeEvent を通知する
        // 。 ここでは前方の埋め込み画像も見つけられるよう、共通の判定をす
        // べての CARD レイヤーに適用する。 画像の保存自体は、選択レ
        // イヤーの cachedIncludedImage() を使う。
        return std::any_of(paths_.begin(), paths_.end(), [](const CardImagePtr& image) {
            return image && image->type == types::CardImageType::File &&
                   isBinImg(image->path());
        });
    }
    return isBinImg(image_path_);
}

CardMaterialPath ImageSelectBase::cardMaterialPath(const bool force_export) {
    CardMaterialPath result;
    result.images = cardImages();
    if (!cardKind() || !isIncluded() || (summary_ && summary_->legacy())) return result;

    const auto choice = force_export ? lib::MessageResult::Yes : askExternalize();
    if (choice == lib::MessageResult::Cancel || choice == lib::MessageResult::None) {
        result.cancel = true;
        return result;
    }
    if (choice == lib::MessageResult::No) {
        result.images.erase(
            std::remove_if(result.images.begin(), result.images.end(),
                           [](const CardImagePtr& image) {
                               return image && image->type == types::CardImageType::File &&
                                      isBinImg(image->path());
                           }),
            result.images.end());
        setCardImages(result.images, true);
        return result;
    }
    for (auto& image : result.images) {
        if (!image || image->type != types::CardImageType::File ||
            !isBinImg(image->path())) continue;
        String exported;
        if (!exportBinary(image->path(), exported)) {
            result.cancel = true;
            return result;
        }
        image = std::make_shared<CardImage>(exported, image->positionType());
    }
    setCardImages(result.images, true);
    return result;
}

String ImageSelectBase::backgroundFilePath() const {
    if (!dirs_ || !dirs_->selected_index || *dirs_->selected_index >= dirs_->items.size())
        return {};
    const auto selected_directory = *dirs_->selected_index;
    const auto binary_index = index_of_binary_ ? index_of_binary_(true) : -1;
    if (isBinImg(image_path_) && binary_index >= 0 &&
        selected_directory == static_cast<std::size_t>(binary_index))
        return image_path_;

    const auto value = selectedFileValue();
    if (value.empty()) return {};
    std::optional<std::size_t> selected_row;
    if (file_combo_ && file_combo_->selected_index) {
        selected_row = *file_combo_->selected_index;
    } else if (files_ && !files_->selection.empty()) {
        selected_row = files_->selection.front();
    }
    if (!selected_row) return {};

    std::size_t choice_index{};
    if (selected_directory >= definition_values_.size()) {
        choice_index = selected_directory - definition_values_.size();
    } else {
        if (*selected_row >= showing_directory_indices_.size()) return {};
        choice_index = showing_directory_indices_[*selected_row];
    }
    if (choice_index >= directory_choices_.size()) return {};
    const auto& choice = directory_choices_[choice_index];
    if (choice.sources.empty()) return {};
    const auto filename = structs::pathFromUtf8(value).filename();
    if (filename.empty()) return {};
    for (const auto& source : choice.sources) {
        const auto file = structs::pathFromUtf8(source) / filename;
        if (choice.kind == DirectoryChoice::Kind::Scenario)
            return structs::pathToUtf8(file.lexically_normal());
        std::error_code error;
        if (std::filesystem::exists(file, error)) return structs::pathToUtf8(file);
    }
    return {};
}

void ImageSelectBase::setBackgroundImage(String path, const bool store_value) {
    if (cardKind()) return;
    if (store_value) store();
    // D版の BG_IMG の selectPath と同様、ファイルパスの . や .. を正規化する。埋め込み画像データの中身は解釈しない。
    if (!lib::editor_window_host_options().all_mode && !isBinImg(path))
        path = ::cwxeditor::cwx::utils::normpath(StringView{path});
    image_path_ = std::move(path);
    const auto lifetime = callback_lifetime_;
    refreshListImpl(true);
    if (lifetime->owner != this) return;
    notifyModified();
}

BackgroundMaterialPath ImageSelectBase::backgroundMaterialPath(const bool force_export) {
    BackgroundMaterialPath result{image_path_, false};
    if (cardKind() || !isIncluded() || (summary_ && summary_->legacy())) return result;

    const auto choice = force_export ? lib::MessageResult::Yes : askExternalize();
    if (choice == lib::MessageResult::Cancel || choice == lib::MessageResult::None) {
        result.cancel = true;
        return result;
    }
    if (choice == lib::MessageResult::No) {
        result.images.clear();
        setBackgroundImage({}, true);
        return result;
    }
    if (!exportBinary(image_path_, result.images)) {
        result.cancel = true;
        return result;
    }
    setBackgroundImage(result.images, true);
    return result;
}

void ImageSelectBase::refreshList() {
    refreshListImpl(false);
}

bool ImageSelectBase::canIncSearch() const noexcept {
    return enabled_ && can_inc_search_;
}

void ImageSelectBase::startIncSearch() {
    if (!canIncSearch()) return;
    const lib::WidgetPtr host = files_ ? std::static_pointer_cast<lib::Widget>(files_)
                                       : std::static_pointer_cast<lib::Widget>(file_combo_);
    createIncrementalSearch(host);
    if (search_popup_) search_popup_->startIncSearch();
}

void ImageSelectBase::createIncrementalSearch(const lib::WidgetPtr& host) {
    if (search_popup_ || !host) return;
    incsearch::IncSearchSettings settings;
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
    settings.closeLabel = prop_->requiredMsgs().menuText(types::MenuID::CloseIncSearch);
    const std::weak_ptr<CallbackLifetime> weak = callback_lifetime_;
    search_popup_ = std::make_shared<incsearch::IncSearch>(
        services_, host.get(), [weak] {
            const auto life = weak.lock();
            return life && life->owner && life->owner->canIncSearch();
        }, std::vector<incsearch::AdditionMatcher>{}, std::move(settings));
    search_popup_->modEvent.push_back([weak] {
        const auto life = weak.lock();
        if (!life || !life->owner || !life->owner->search_popup_) return;
        auto* self = life->owner;
        self->prop_->var().incrementalSearchType.value =
            static_cast<int>(self->search_popup_->searchType());
        self->refreshList();
    });
}

String ImageSelectBase::selectedFileValue() const {
    if (file_combo_ && file_combo_->selected_index &&
        *file_combo_->selected_index < file_combo_->items.size()) {
        return file_combo_->items[*file_combo_->selected_index].value;
    }
    if (files_ && !files_->selection.empty() && files_->selection.front() < files_->rows.size()) {
        const auto& row = files_->rows[files_->selection.front()];
        if (!row.cells.empty() && row.cells.front().value.rfind("definition:", 0U) != 0U)
            return row.cells.front().value;
    }
    return {};
}

std::filesystem::path ImageSelectBase::selectedFilePath() const {
    const auto value = selectedFileValue();
    if (value.empty()) return {};
    const auto skin = summSkin();
    if (!skin) return structs::pathFromUtf8(value);
    bool skin_material = false;
    bool engine_material = false;
    const auto resolved = skin->findImagePathF(
        value, summary_ ? summary_->scenarioPath() : String{},
        summary_ ? summary_->dataVersion() : versioninfo::LATEST_VERSION,
        skin_material, engine_material);
    return structs::pathFromUtf8(resolved.empty() ? value : resolved);
}

void ImageSelectBase::bindEditorCommons(commons::Commons* value) {
    if (editor_commons_ == value && (!value || !editor_commons_lifetime_.expired())) return;

    ++material_binding_revision_;
    ++callback_lifetime_->notification_revision;
    if (disconnect_material_notifications_) disconnect_material_notifications_();
    disconnect_material_notifications_ = {};
    editor_commons_ = value;
    editor_commons_lifetime_ = value ? value->lifetimeToken() : std::weak_ptr<const int>{};

    if (!value) return;
    const auto resolve = [weak=std::weak_ptr<CallbackLifetime>(callback_lifetime_),
            host=editor_commons_lifetime_, value, revision=material_binding_revision_]() -> ImageSelectBase* {
        const auto life = weak.lock();
        if (host.expired() || !life || !life->owner) return nullptr;
        auto* owner = life->owner;
        return owner->editor_commons_ == value && owner->material_binding_revision_ == revision ? owner : nullptr;
    };
    // D版の ImageListWindow と同様、モードレスのギャラリーが存続する
    // 間は refImageScale を監視する。 所有者の購読は弱参照にし、再初
    // 期化がリソース解放や配置のコールバックを呼ぶ前に、現在のギャラリーを保持する。
    const auto image_scale = value->refImageScale.add([resolve] {
        auto* owner = resolve();
        if (!owner || !owner->prop_) return;
        // D版の MaterialSelect と同様、この通知で通常のファイ
        // ル行も列挙し直し、 新しく利用可能になった素材を選択欄に反映する。
        owner->refreshList();
        owner = resolve();
        if (!owner || !owner->prop_) return;
        // D版と同様、開いている ImageLayerList は独立してキャッシュした行を無
        // 効化する。 リソース解放コールバックの間は一覧を保持し、その後で所有者を取得し直す。
        const auto layers = owner->layer_window_;
        if (!lib::editor_window_host_options().all_mode && layers) {
            const auto list = layers->list();
            if (list) list->refImageScale();
        }
        owner = resolve();
        if (!owner || !owner->prop_) return;
        const auto gallery = owner->image_list_;
        if (!gallery || gallery->closed()) return;
        auto* const prop = owner->prop_;
        lib::Size size;
        if (owner->cardKind()) {
            const auto card = prop->looks().cardSize();
            size = {static_cast<float>(prop->s(static_cast<int>(card.width))),
                    static_cast<float>(prop->s(static_cast<int>(card.height)))};
        } else {
            size = {static_cast<float>(prop->var().bgImageSampleWidth.value),
                    static_cast<float>(prop->var().bgImageSampleHeight.value)};
        }
        gallery->refImageScale(size, prop->imageScaleFactor(),
                              static_cast<int>(prop->drawingScale()));
    });
    disconnect_material_notifications_ = [value, host=editor_commons_lifetime_, image_scale] {
        if (!host.expired()) value->refImageScale.remove(image_scale);
    };
    // D版の ImageLayerWindow と同様、別名保存でClassic形式へ切り
    // 替わったとき、 レイヤー数がゼロまたは一つの編集画面を閉じる。選択欄の所有者を通じて
    // 購読し、 他のモードレスのレイヤー通知と同じホストの弱参照・接続ガードを維持する。
    const auto data_version = value->refDataVersion.add([resolve] {
        auto* owner = resolve();
        if (!owner || lib::editor_window_host_options().all_mode ||
            !owner->summary_ || !owner->summary_->legacy()) return;
        const auto layers = owner->layer_window_;
        if (layers && layers->list() && layers->list()->items().size() <= 1U)
            layers->close();
    });
    disconnect_material_notifications_ = [value, host=editor_commons_lifetime_, data_version,
            disconnect_scale=std::move(disconnect_material_notifications_)] {
        if (disconnect_scale) disconnect_scale();
        if (!host.expired()) value->refDataVersion.remove(data_version);
    };
    // D版のカード用 ImageSelect と同様、シナリオのパスの有無
    // とは独立して、 コントロールの存続中は取り消し回数の上限を同期する。
    if (cardKind()) {
        const auto undo_limit = value->refUndoMax.add([resolve] {
            if (auto* owner = resolve()) owner->refUndoMax();
        });
        disconnect_material_notifications_ = [value, host=editor_commons_lifetime_, undo_limit,
                disconnect_scale=std::move(disconnect_material_notifications_)] {
            if (disconnect_scale) disconnect_scale();
            if (!host.expired()) value->refUndoMax.remove(undo_limit);
        };
    }
    if (!summary_ || summary_->scenarioPath().empty()) return;
    // D版の MaterialSelect と同様、シナリオに結び付いたコントロールの存続中は購読を維持する。
    const auto skin_changed = value->refSkin.add([resolve] {
        if (auto* owner = resolve()) {
            owner->refreshList();
            if (resolve() == owner) owner->refresh();
        }
    });
    const auto added = value->refPaths.addSender([resolve](void* sender, String) {
        if (auto* owner = resolve()) owner->materialPathsChanged(sender != owner, true);
    });
    const auto renamed = value->refPath.add([resolve](String from, String to, bool directory) {
        if (auto* owner = resolve()) owner->materialPathRenamed(std::move(from), std::move(to), directory);
    });
    const auto deleted = value->delPaths.add([resolve] {
        if (auto* owner = resolve()) owner->materialPathsChanged(true, false);
    });
    const auto replaced = value->replPath.add([resolve](String from, String) {
        if (auto* owner = resolve()) {
            const auto selected = owner->cardKind()
                ? (owner->image_index_ < owner->paths_.size() && owner->paths_[owner->image_index_]
                    ? owner->paths_[owner->image_index_]->path() : String{}) : owner->image_path_;
            if (sameString(selected, from)) owner->materialPathsChanged(true, false);
        }
    });
    disconnect_material_notifications_ = [value, host=editor_commons_lifetime_, added, renamed, deleted, replaced, skin_changed,
            disconnect_undo=std::move(disconnect_material_notifications_)] {
        if (disconnect_undo) disconnect_undo();
        if (host.expired()) return;
        value->refPaths.remove(added); value->refPath.remove(renamed);
        value->delPaths.remove(deleted); value->replPath.remove(replaced);
        value->refSkin.remove(skin_changed);
    };
}

void ImageSelectBase::materialPathsChanged(const bool enumerate, const bool notify_refresh) {
    const auto life = callback_lifetime_;
    const auto revision = life->notification_revision;
    const auto refresh_callback = refresh_callback_;
    // refreshListImpl はモードレスの画像一覧も更新済み。D版と同様
    // 、 削除・置換通知では列挙だけを行い、refPaths では親の更新も呼ぶ。
    if (enumerate) refreshListImpl(false);
    if (life->owner != this || life->notification_revision != revision) return;
    if (notify_refresh && refresh_callback) refresh_callback();
}

void ImageSelectBase::materialPathRenamed(String from, String to, const bool directory) {

    if (from == to) return;
    bool active_changed = false;
    if (!directory) {
        if (cardKind()) {
            for (std::size_t i = 0; i < paths_.size(); ++i) {
                const auto& path = paths_[i];
                if (!path || path->type != types::CardImageType::File ||
                    !sameString(path->path(), from)) continue;
                paths_[i] = std::make_shared<CardImage>(to, path->positionType());
                active_changed = active_changed || i == image_index_;
            }
        } else if (sameString(image_path_, from)) {
            image_path_ = to;
            active_changed = true;
        }
    }

    const auto remap = [&](String& path) {
        if (!directory) {
            if (sameString(path, from)) path = to;
            return;
        }
        // D版の MaterialSelect.refPath と同様、接頭辞をそのまま比較し、残りを
        // 切り詰めずに buildPath へ渡す。 A が AB にも一致する動作と、残りがルート
        // 付きの場合の動作を維持する。 パス要素を考慮して正規化すると、D版の意味が変わってしまう。
        if (startsWithFilenameIdentity(path, from)) {
            const auto suffix = path.substr(from.size());
            path = suffix.empty() ? to : structs::pathToUtf8(
                structs::pathFromUtf8(to) / structs::pathFromUtf8(suffix));
        }
    };
    if (directory && summary_) {
        const auto base = structs::pathFromUtf8(summary_->scenarioPath()).lexically_normal();
        const auto abs_from = structs::pathToUtf8((base / structs::pathFromUtf8(from)).lexically_normal());
        const auto abs_to = structs::pathToUtf8((base / structs::pathFromUtf8(to)).lexically_normal());
        for (auto& choice : directory_choices_) {
            if (choice.kind != DirectoryChoice::Kind::Scenario) continue;
            const auto old_value = choice.value;
            remap(choice.label);
            auto absolute = choice.value;
            if (startsWithFilenameIdentity(absolute, abs_from)) {
                const auto suffix = absolute.substr(abs_from.size());
                choice.value = suffix.empty() ? abs_to : structs::pathToUtf8(
                    structs::pathFromUtf8(abs_to) / structs::pathFromUtf8(suffix));
                for (auto& source : choice.sources) {
                    if (!startsWithFilenameIdentity(source, abs_from)) continue;
                    const auto source_suffix = source.substr(abs_from.size());
                    source = source_suffix.empty() ? abs_to : structs::pathToUtf8(
                        structs::pathFromUtf8(abs_to) / structs::pathFromUtf8(source_suffix));
                }
                for (auto& value : directories_) if (sameString(value, old_value)) value = choice.value;
                if (dirs_) for (auto& item : dirs_->items) if (sameString(item.value, old_value)) {
                    item.value = choice.value; item.text = choice.label;
                }
            }
        }
    }
    // D版はファイル名の行を保持し、改名後のディレクトリ文字列から showingPaths/toFilePath
    // を求める。 ここでキャッシュする行の完全パスも、その組み立て方に合わせる。
    // 完全なファイルパスにディレクトリの接頭辞処理を直接適用すると、 A/foo
    // のディレクトリ A の改名で、誤ってルート付きの /foo になってしまう。
    const auto remap_file = [&](String& path) {
        if (!directory) { remap(path); return; }
        const auto original = structs::pathFromUtf8(path);
        auto parent = structs::pathToUtf8(original.parent_path());
        const auto before = parent;
        remap(parent);
        if (parent != before) path = structs::pathToUtf8(
            structs::pathFromUtf8(parent) / original.filename());
    };
    // ファイルの改名では、表示中の行の識別情報と閲覧中の選択を保持する。
    for (auto& path : showing_paths_) remap_file(path);
    if (files_) for (auto& row : files_->rows) if (!row.cells.empty()) {
        auto& cell = row.cells.front();
        const auto old = cell.value; remap_file(cell.value);
        if (cell.value != old) cell.text = fileLabel(cell.value);
    }
    if (file_combo_) for (auto& item : file_combo_->items) {
        const auto old = item.value; remap_file(item.value);
        if (item.value != old) item.text = fileLabel(item.value);
    }
    const auto life = callback_lifetime_;
    const auto revision = life->notification_revision;
    const auto list_revision = life->list_revision;
    const auto current = [&] { return life->owner == this && life->notification_revision == revision &&
                                    life->list_revision == list_revision; };
    const auto gui = services_.gui;
    const auto widgets = std::vector<lib::WidgetPtr>{dirs_, files_, file_combo_};
    // 文字列を直接変更する場合も、行と編集状態の識別情報を保持しつつ再描画する必要がある。
    for (const auto& widget : widgets) {
        if (gui && widget) gui->request_redraw(widget, std::nullopt);
        if (!current()) return;
    }
    syncLayerWindow();
    if (!current()) return;
    if (active_changed) {
        const auto notification = life->notification_revision + 1U;
        // この変更通知は updateRows を一度だけ呼ぶ。 それ以上の一覧
        // の変更番号の更新は、後続のコールバックや閲覧操作によるものとして扱う。
        const auto list = life->list_revision + 1U;
        const auto binding = material_binding_revision_;
        const auto host = editor_commons_lifetime_;
        const auto commons = editor_commons_;
        publishChange(true, true, true);
        if (life->owner != this || life->notification_revision != notification ||
            life->list_revision != list || material_binding_revision_ != binding ||
            editor_commons_ != commons || host.expired()) return;
        // D版の path2 と同様、refreshPaths を保留する。残りのディレクトリ
        // 改名通知が終わってから、 現在の画像のディレクトリを復元する。新しい編集・閲覧・Commons
        // への接続、 または所有者の終了があった場合は、その新しい状態を優先する。

        const auto refresh = [weak = std::weak_ptr<CallbackLifetime>(life),
                              notification, list, binding, host, commons] {
            const auto state = weak.lock();

            if (host.expired() || !state || !state->owner ||
                state->notification_revision != notification ||
                state->list_revision != list) return;
            auto* owner = state->owner;

            if (owner->material_binding_revision_ != binding ||
                owner->editor_commons_ != commons) return;

            owner->refreshListImpl(true);
        };
        const auto dispatcher = services_.dispatcher;
        if (dispatcher) dispatcher->post(refresh);
        else refresh();
    } else {
        const auto refresh_callback = refresh_callback_;
        if (refresh_callback) refresh_callback();
        if (!current()) return;
        refreshImageList();
    }
}

bool ImageSelectBase::canOpenSelectedFile() const {
    if (!enabled_ || read_only_ || !editor_commons_ || editor_commons_lifetime_.expired() ||
        !dirs_ || !dirs_->selected_index || *dirs_->selected_index >= dirs_->items.size()) return false;
    const auto& directory = dirs_->items[*dirs_->selected_index].value;
    const auto found = std::find_if(directory_choices_.begin(),directory_choices_.end(),
        [&directory](const DirectoryChoice& item) { return item.value == directory; });
    // D版と同様、定義、スキン一覧、エンジン一覧の行を除外する。
    return found != directory_choices_.end() && found->kind == DirectoryChoice::Kind::Scenario &&
        !selectedFileValue().empty();
}

void ImageSelectBase::openSelectedFile() {
    if (!canOpenSelectedFile()) return;
    const auto life = callback_lifetime_;
    const auto revision = life->notification_revision;
    const auto rows_revision = life->list_revision;
    const auto host_life = editor_commons_lifetime_;
    auto* const host = editor_commons_;
    const auto path = selectedFilePath();
    if (life->owner != this || life->notification_revision != revision ||
        life->list_revision != rows_revision || host_life.expired() || path.empty()) return;
    (void)host->openFilePath(structs::pathToUtf8(path),false,true);
}

void ImageSelectBase::copySelectedFilePath() {
    if (!enabled_ || !services_.clipboard) return;
    const auto path = selectedFilePath();
    if (path.empty()) return;
    lib::ClipboardData data;
    data.text = structs::pathToUtf8(path);
    services_.clipboard->clear();
    services_.clipboard->write(data);
}

void ImageSelectBase::includeSelectedImage() {
    if (!cardKind() || !enabled_ || read_only_ || !can_include_ || !summary_ ||
        !summary_->legacy() || !services_.dialogs) return;
    const auto path = selectedFilePath();
    const auto skin = summSkin();
    if (path.empty() || !skin || !skin->canInclude(structs::pathToUtf8(path))) return;
    lib::MessageDialogRequest request;
    request.title = prop_->requiredMsgs().dlgTitQuestion.value;
    request.message = utils::tryFormat(prop_->requiredMsgs().dlgMsgIncludeImage.value,
                                       selectedFileValue());
    request.severity = lib::MessageSeverity::Question;
    request.buttons = lib::MessageButtons::YesNo;
    if (services_.dialogs->message(request) != lib::MessageResult::Yes) return;
    try {
        const auto encoded = utils::bImgToStr(utils::readBinary(path));
        selectPath(encoded, true);
        refreshListImpl(true);
    } catch (...) {
        // D版と同様、ファイルを読めない場合は現在の選択を変えない。
    }
}

void ImageSelectBase::refreshFileListMenu() {
    if (!context_menu_) return;
    const bool has_file = !selectedFileValue().empty();
    const auto skin = summSkin();
    const auto physical = selectedFilePath();
    for (auto& item : context_menu_->items) {
        if (item.command == "incremental-search") item.enabled = canIncSearch();
        else if (item.command == "undo") item.enabled = enabled_ && !read_only_ && !undo_stack_.empty();
        else if (item.command == "redo") item.enabled = enabled_ && !read_only_ && !redo_stack_.empty();
        else if (item.command == "open-file") item.enabled = canOpenSelectedFile();
        else if (item.command == "copy-file-path") item.enabled = enabled_ && has_file && bool(services_.clipboard);
        else if (item.command == "include-image")
            item.enabled = enabled_ && !read_only_ && cardKind() && can_include_ && summary_ &&
                summary_->legacy() && has_file && skin && skin->canInclude(structs::pathToUtf8(physical));
    }
    ++context_menu_->item_update_revision;
}

void ImageSelectBase::bindFileActions(const lib::WidgetPtr& host, const bool child_list) {
    if (!host) return;
    auto menu = std::make_shared<lib::Menu>(true);
    const auto add = [&](const types::MenuID id, String command) {
        menu->items.push_back({prop_->buildMenu(id), std::move(command), {}, prop_->images().menu(id)});
    };
    add(types::MenuID::IncSearch, "incremental-search");
    if (child_list) {
        menu->items.push_back({.separator = true});
        add(types::MenuID::CloseWin, "close-window");
        image_list_menu_ = menu;
    } else {
        if (cardKind()) {
            menu->items.push_back({.separator = true});
            add(types::MenuID::Undo, "undo");
            add(types::MenuID::Redo, "redo");
        }
        menu->items.push_back({.separator = true});
        add(types::MenuID::OpenAtFileView, "open-file");
        add(types::MenuID::CopyFilePath, "copy-file-path");
        if (cardKind() && can_include_ && !read_only_ && summary_ && summary_->legacy()) {
            menu->items.push_back({.separator = true});
            add(types::MenuID::IncludeImage, "include-image");
        }
        context_menu_ = menu;
    }
    const std::weak_ptr<CallbackLifetime> weak = callback_lifetime_;
    const std::weak_ptr<lib::Menu> weak_menu = menu;
    menu->on_open = [weak, weak_menu, child_list] {
        const auto life = weak.lock();
        const auto shown = weak_menu.lock();
        if (!life || !life->owner || !shown) return;
        if (!child_list) life->owner->refreshFileListMenu();
        else {
            shown->items.front().enabled = life->owner->canIncSearch();
            ++shown->item_update_revision;
        }
    };
    menu->on_command = [weak, weak_menu, child_list](const StringView command) {
        const auto life = weak.lock();
        const auto shown = weak_menu.lock();
        if (!life || !life->owner || !shown) return;
        const auto item = std::find_if(shown->items.begin(), shown->items.end(),
            [&](const lib::MenuItem& entry) { return entry.command == command; });
        if (item == shown->items.end() || !item->enabled) return;
        auto* self = life->owner;
        if (command == "incremental-search") self->startIncSearch();
        else if (command == "undo") (void)self->undo();
        else if (command == "redo") (void)self->redo();
        else if (command == "open-file") self->openSelectedFile();
        else if (command == "copy-file-path") self->copySelectedFilePath();
        else if (command == "include-image") self->includeSelectedImage();
        else if (command == "close-window" && child_list) self->closeImageList();
    };
    const auto old_pointer = host->on_pointer;
    const std::weak_ptr<lib::Widget> weak_host = host;
    host->on_pointer = [old_pointer, weak, weak_host, menu](lib::PointerEvent& event) {
        if (old_pointer) old_pointer(event);
        if (event.handled || event.action != lib::PointerAction::Down ||
            event.button != lib::PointerButton::Secondary) return;
        const auto life = weak.lock();
        const auto widget = weak_host.lock();
        if (!life || !life->owner || !widget || !widget->enabled() || !life->owner->services_.gui) return;
        if (menu->on_open) menu->on_open();
        event.handled = true;
        life->owner->services_.gui->show_context_menu(menu, widget, event.position);
    };
    const auto old_text = host->on_text_input;
    host->on_text_input = [old_text, weak, weak_host](lib::TextInputEvent& event) {
        if (old_text) old_text(event);
        const auto life = weak.lock();
        const auto widget = weak_host.lock();
        if (!event.handled && life && life->owner && widget && life->owner->search_popup_ &&
            life->owner->search_popup_->handleHostTextInput(event, widget.get())) event.handled = true;
    };
    const auto old_key = host->on_key_down;
    host->on_key_down = [old_key, weak, weak_host, child_list](lib::KeyEvent& event) {
        if (old_key) old_key(event);
        const auto life = weak.lock();
        const auto widget = weak_host.lock();
        if (event.handled || !life || !life->owner || !widget) return;
        auto* self = life->owner;
        const bool command = hasModifier(event.modifiers, lib::Modifiers::Control) ||
                             hasModifier(event.modifiers, lib::Modifiers::Meta);
        if (command && event.key == lib::Key::I) { self->startIncSearch(); event.handled = true; }
        else if (command && event.key == lib::Key::Z && !self->read_only_) { event.handled = self->undo(); }
        else if (command && event.key == lib::Key::Y && !self->read_only_) { event.handled = self->redo(); }
        else if (self->search_popup_ && self->search_popup_->handleHostKeyDown(event, widget.get()))
            event.handled = true;
        else if (!child_list && widget->enabled() && event.key == lib::Key::Enter) {
            event.handled = true; // 外側のダイアログまで確定しない。
            self->openSelectedFile();
        }
    };
}

void ImageSelectBase::refreshListImpl(const bool restore_image_directory) {
    if (!enabled_) return;
    can_inc_search_ = false;
    const auto lifetime = callback_lifetime_;
    const auto revision = ++lifetime->list_revision;
    const auto notification = lifetime->notification_revision;
    const auto current = [&] {
        return lifetime->owner == this && lifetime->list_revision == revision &&
               lifetime->notification_revision == notification;
    };
    if (!summ_skin_ && prop_) {
        summ_skin_ = dskin::findSkin2(*prop_, prop_->var().defaultSkin.value,
                                      prop_->var().defaultSkinName.value);
    }
    const auto dirs = dirs_;
    const auto definition_index = value_to_definition_;
    const bool included = !cachedIncludedImage().empty();
    const auto definitions_provider = definitions_;
    auto definitions = definitions_provider ? definitions_provider(included)
                                            : std::vector<String>{};
    if (!current()) return;
    const auto image = cardKind() && image_index_ < paths_.size() && paths_[image_index_]
        ? std::make_shared<CardImage>(nullptr, *paths_[image_index_])
        : (!cardKind() ? std::make_shared<CardImage>(image_path_) : CardImagePtr{});
    auto directory_choices = directoryChoices();
    if (!current()) return;
    std::vector<lib::ChoiceItem> choices;
    choices.reserve(definitions.size() + directory_choices.size());
    for (std::size_t i = 0; i < definitions.size(); ++i) {
        choices.push_back({definitions[i], "definition:" + std::to_string(i), 0, true});
    }
    for (const auto& directory : directory_choices) {
        choices.push_back({directory.label, directory.value, 0, true});
    }
    std::size_t selected = 0;
    bool selection_restored = false;
    if (image) {
        int definition = -1;
        if (definition_index) {
            definition = definition_index(*image, included);
        } else if (!cardKind() && image->type == types::CardImageType::File &&
                   isBinImg(image->path()) && index_of_binary_) {
            // D版の BG_IMG 選択と同様、埋め込みパスを indexOfBinPath で対応付ける。
            definition = index_of_binary_(included);
        }
        if (!current()) return;
        // 画像の読み込みではその場所を復元するが、フォルダーの閲
        // 覧ではユーザーが選んだディレクトリを維持する。 以前の
        // 画像がプレイヤー・埋め込み画像・空の定義の場合も同様。
        // 素材フォルダーを閲覧しているだけで画像を変更しない。
        if ((restore_image_directory || dirs->items.empty()) &&
            definition >= 0 && static_cast<std::size_t>(definition) < definitions.size()) {
            selected = static_cast<std::size_t>(definition);
            selection_restored = true;
        }
    }
    if (!selection_restored && !restore_image_directory) {
        // ディレクトリの列挙で行の順序が変わる場合がある。 別ディレクトリ
        // での古いインデックスではなく、まず識別情報に基づいて復元する。
        if (dirs->selected_index && *dirs->selected_index < dirs->items.size()) {
            const auto& value = dirs->items[*dirs->selected_index].value;
            const auto found = std::find_if(choices.begin(), choices.end(),
                [&](const auto& choice) { return sameString(choice.value, value); });
            if (found != choices.end()) {
                selected = static_cast<std::size_t>(found - choices.begin());
                selection_restored = true;
            }
        } else if (old_directory_selection_ >= 0 &&
                   static_cast<std::size_t>(old_directory_selection_) < choices.size()) {
            selected = static_cast<std::size_t>(old_directory_selection_);
            selection_restored = true;
        }
    }
    if (!selection_restored && image && image->type == types::CardImageType::File &&
        !image->path().empty() && !isBinImg(image->path())) {
        const auto& target = image->path();
        for (std::size_t directory_index = 0; directory_index < directory_choices.size();
             ++directory_index) {
            const auto candidates = pathsForDirectory(directory_choices[directory_index].value);
            if (!current()) return;
            if (std::any_of(candidates.begin(), candidates.end(),
                            [&target](const String& candidate) {
                                return sameString(candidate, target);
                            })) {
                selected = definitions.size() + directory_index;
                break;
            }
        }
    }
    const bool selected_definition = selected < definitions.size();
    const auto directory = selected_definition ? String{} : choices[selected].value;
    std::vector<String> paths;
    std::vector<std::size_t> path_directories;
    if (selected_definition) {
        for (std::size_t i = 0; i < directory_choices.size(); ++i) {
            const auto values = pathsForDirectory(directory_choices[i].value);
            if (!current()) return;
            paths.insert(paths.end(), values.begin(), values.end());
            path_directories.insert(path_directories.end(), values.size(), i);
        }
        // directoryChoices は、スキン、エンジン、シナリオの各ディレク
        // トリの順に並ぶ。 各グループ内は pathsForDirectory で整列
        // する。D版と同様、 全素材のギャラリーでも、このグループの区切りを維持する。
    } else if (!selected_definition) {
        paths = pathsForDirectory(directory);
        if (!current()) return;
        path_directories.assign(paths.size(), selected - definitions.size());
    }
    std::vector<String> labels;
    labels.reserve(paths.size());
    for (std::size_t i = 0; i < paths.size(); ++i) {
        String label = fileLabel(paths[i]);
        if (selected_definition && i < path_directories.size() &&
            path_directories[i] < directory_choices.size() &&
            directory_choices[path_directories[i]].kind == DirectoryChoice::Kind::Scenario) {
            label = paths[i];
            std::replace(label.begin(), label.end(), '\\', '/');
            if (!label.empty() && label.front() != '/') label.insert(label.begin(), '/');
        }
        labels.push_back(std::move(label));
    }
    auto prepared = prepareRows(paths, labels, definitions, revision);
    if (!current() || !prepared) return;
    // 提供処理とメモリ確保はすべて完了済み。選択の正規化で確保が発生し得るた
    // め、 まず行を反映し、その後で例外を出さない付随データの交換を行う。
    if(!publishRows(std::move(*prepared))) return;
    if (!lib::replace_combo_items(dirs, std::move(choices), selected, {}, current)) return;
    definition_values_.swap(definitions);
    directory_choices_.swap(directory_choices);
    directories_.clear();
    directories_.reserve(directory_choices_.size());
    for (const auto& entry : directory_choices_) directories_.push_back(entry.value);
    showing_paths_.swap(paths);
    showing_labels_.swap(labels);
    showing_directory_indices_.swap(path_directories);
    old_directory_selection_ = static_cast<int>(selected);
    refreshImageList();
}

void ImageSelectBase::selectDir(const int selection) {
    selectDirImpl(selection);
}

void ImageSelectBase::selectDirImpl(const int selection) {
    const auto lifetime = callback_lifetime_;
    const auto revision = lifetime->notification_revision;
    if (selection < 0 || static_cast<std::size_t>(selection) >= dirs_->items.size()) {
        if (selection == -1 && old_directory_selection_ == -1) return;
        old_directory_selection_ = selection;
        refreshList();
        return;
    }
    old_directory_selection_ = selection;
    (void)lib::select_combo_item(*dirs_, static_cast<std::size_t>(selection), false);
    if (!cardKind() && static_cast<std::size_t>(selection) < definition_values_.size()) {
        // D版の MaterialSelect と同様、どの定義行を選んだ場合も BG_IMG を解除する。
        setBackgroundImage({}, true);
        return;
    }
    if (static_cast<std::size_t>(selection) < definition_values_.size() && cardKind()) {
        // D版と同様、定義のギャラリーを更新してから選択値を適用する。 先に適用す
        // ると、以前の素材フォルダーの行によって埋め込み画像・なしの項目が隠れる。
        const auto refreshed_revision = lifetime->list_revision + 1U;
        refreshList();
        if (lifetime->owner != this || lifetime->notification_revision != revision ||
            lifetime->list_revision != refreshed_revision) return;
        const auto binary_path = cachedIncludedImage();
        // D版と同様、埋め込み画像の項目が選択されているか
        // ではなく、定義一覧に含まれるかを渡す。 PCのイ
        // ンデックスは、一覧全体のオフセットに依存する。
        const bool included = !binary_path.empty();
        const auto definition = value_from_definition_;
        auto image = definition
                         ? definition(selection, included, binary_path)
                         : CardImagePtr{};
        if (lifetime->owner != this || lifetime->notification_revision != revision) return;
        if (image) {
            if (image_index_ >= paths_.size()) paths_.resize(image_index_ + 1U);
            store();
            paths_[image_index_] = std::move(image);
            notifyModified();
            return;
        }
    }
    const auto refreshed_list_revision = lifetime->list_revision + 1U;
    refreshList();
    if (lifetime->owner != this || lifetime->notification_revision != revision ||
        lifetime->list_revision != refreshed_list_revision) return;
    if (combo_file_selector_ &&
        static_cast<std::size_t>(selection) >= definition_values_.size() &&
        !showing_paths_.empty() && file_combo_) {
        // 先頭の素材行を確定できるのは具体的なディレクトリだけで、なし・独
        // 自定義の行には個別の選択規則がある。 D版の ImageSelect!(CARD,
        // Combo) と同様、フォルダー変更後に先頭行を確定
        // する。 表・一覧の表示は、従来の選択方針を維持する。
        const auto combo = file_combo_;
        (void)lib::select_combo_item(*combo, 0U, false);
        if (lifetime->owner != this || lifetime->notification_revision != revision ||
            lifetime->list_revision != refreshed_list_revision || file_combo_ != combo ||
            showing_paths_.empty()) return;
        selectFileRow(0U);
        return;
    }
    // ネイティブのコンボ選択は、ユーザーが項目をクリックした時点
    // で確定した選択となる。 refreshList() は部品
    // のモデルを変えるため、無関係なダイアログの再描画やフォーカ
    // ス変更を待たず、 影響するコントロールを直ちに同期する。
    if (lifetime->owner != this || lifetime->notification_revision != revision) return;
    const auto gui = services_.gui;
    const auto files = files_;
    const auto combo = file_combo_;
    if (gui) {
        if (files) gui->request_redraw(files);
        if (lifetime->owner != this || lifetime->notification_revision != revision) return;
        if (combo) gui->request_redraw(combo);
    }
    // refreshList() で同じディレクトリとパスの集合をモー
    // ドレスの画像一覧に反映済み。 ここで繰り返すと、ディレクトリのク
    // リックごとにタイルと選択を二度再構築し、再入可能な箇所も増える。
}

void ImageSelectBase::refreshImageList() {
    const auto list = image_list_;
    if (!list) return;
    const auto lifetime = callback_lifetime_;
    const auto revision = lifetime->notification_revision;
    const auto current = [lifetime, revision, owner = this, list] {
        return lifetime->owner == owner && lifetime->notification_revision == revision &&
               owner->image_list_ == list;
    };
    const auto directory = dirs_->selected_index &&
                                   *dirs_->selected_index < dirs_->items.size()
                               ? dirs_->items[*dirs_->selected_index].value
                               : String{};
    const auto paths = showing_paths_;
    const auto mask_value = mask_;
    const auto selected = cardKind()
        ? (image_index_ < paths_.size() && paths_[image_index_] ? paths_[image_index_]->path() : String{})
        : image_path_;
    list->images(directory, paths);
    if (!current()) return;
    list->mask(mask_value);
    if (!current()) return;
    list->select(selected);
}

std::vector<String> ImageSelectBase::warnings() const {
    const auto current_skin = summSkin();
    if (!current_skin) return {};
    return warningsWith(summary_ ? summary_->legacy() : false,
                        summary_ ? summary_->dataVersion() : versioninfo::LATEST_VERSION,
                        *current_skin);
}

std::vector<String> ImageSelectBase::warningFrom(StringView image, const bool legacy,
                                                  const Skin& current_skin) const {
    if (!prop_ || image.empty()) return {};
    // 直列化された話者参照はファイルではない。素材選択欄がまだXMLテキストとし
    // て持っていても、 型付きの CardImage と同じ警告処理に合わせる。
    if (cardKind() && isTalkerImagePath(image)) return {};
    return current_skin.warningImage(
        prop_->parent(), image, legacy, can_include_ && !is_menu_card_,
        prop_->var().targetVersion.value);
}

std::vector<String> ImageSelectBase::warningsWith(const bool legacy,
                                                   const StringView data_version,
                                                   const Skin& current_skin) const {
    std::vector<String> result;
    if (!legacy && isIncluded()) {
        appendUnique(result, prop_->requiredMsgs().warningIncludedImage.value);
    }
    if (cardKind()) {
        for (const auto& image : paths_) {
            if (!image) continue;
            if (image->type == types::CardImageType::File) {
                appendUniqueMessages(result, warningFrom(image->path(), legacy, current_skin));
                if (image->positionType() != types::CardImagePosition::Default &&
                    !prop_->isTargetVersion(legacy, data_version, "2")) {
                    appendUnique(result, prop_->requiredMsgs().warningCardImagePosition.value);
                }
            } else if (image->type == types::CardImageType::PCNumber &&
                       image->pcNumber() != 0U && !prop_->targetVersion(legacy, "1.50")) {
                appendUnique(result, prop_->requiredMsgs().warningPCNumberClassic.value);
            }
        }
    } else {
        appendUniqueMessages(result,
                             warningFrom(backgroundFilePath(), legacy, current_skin));
    }
    return result;
}

void ImageSelectBase::active(const bool value) {
    const auto lifetime = callback_lifetime_;
    if (lifetime->active == value) return;
    lifetime->active = value;
    const auto revision = ++lifetime->notification_revision;
    bindDropTarget();
    if (lifetime->owner != this || lifetime->notification_revision != revision ||
        lifetime->active != value) return;
    if (!value) closeImageList();
    if (lifetime->owner != this || lifetime->notification_revision != revision) return;
    if (!value && layer_window_) layer_window_->close();
}

void ImageSelectBase::enabled(const bool value) {
    const auto lifetime = callback_lifetime_;
    const auto revision = ++lifetime->notification_revision;
    enabled_ = value;
    if (!value) closeImageList();
    if (!value && layer_window_) layer_window_->close();
    if (lifetime->owner != this || lifetime->notification_revision != revision) return;
    updateControls();
}

void ImageSelectBase::refreshWithoutImageList() {
    publishChange(false, true, false);
}

void ImageSelectBase::refresh() {
    if (layer_window_ && summary_ && summary_->legacy() && paths_.size() <= 1U) {
        layer_window_->close();
    }
    if (layer_window_ && layer_window_->list()) layer_window_->list()->refImageScale();
    publishChange(false, true, true);
}

SkinPtr ImageSelectBase::summSkin() const noexcept {
    if (force_skin_) return force_skin_;
    // D版と同様、明示的な上書き、切り離されたビュー、読み取り専用ビューの場合だけスキンを固定する。
    if (!read_only_ && summary_ && !summary_->scenarioPath().empty() &&
        editor_commons_ && !editor_commons_lifetime_.expired() &&
        editor_commons_->summary().get() == summary_) {
        return editor_commons_->skin();
    }
    return summ_skin_;
}

void ImageSelectBase::editLayers() {
    if (read_only_ && (paths_.empty() || (summary_ && summary_->legacy()))) return;
    if (!cardKind() || !enabled_ || !cachedIncludedImage().empty() ||
        (summary_ && summary_->legacy() && paths_.size() < 2U)) return;
    if (layer_window_) {
        if (layers_editing_) { layer_window_->close(); return; }
        syncLayerWindow();
        layer_window_->show();
        layers_editing_ = true;
        updateControls();
        return;
    }

    areaviewutils::Commons comm;
    comm.prop = prop_;
    comm.skin = summSkin().get();
    comm.summary = summary_;
    comm.services = services_;
    if (prop_) {
        const auto configured = prop_->var().undoMaxEtc.value;
        comm.undoMaxEvent = configured > 0 ? static_cast<std::size_t>(configured) : 0U;
    }
    comm.dialogOwner = image_list_owner_;
    const auto lifetime = callback_lifetime_;
    // D版と同様、CopyAll がクリップボードへ書き込んだ後でエディタのツールバーを更新する。
    comm.refreshToolBar = [weak = std::weak_ptr<CallbackLifetime>(lifetime)] {
        const auto state = weak.lock();
        if (!state || !state->owner) return;
        auto* owner = state->owner;
        if (!owner->editor_commons_ || owner->editor_commons_lifetime_.expired()) return;
        owner->editor_commons_->refreshToolBar();
    };
    const auto store_layers = [weak = std::weak_ptr<CallbackLifetime>(lifetime)] {
        const auto state = weak.lock();
        if (!state || !state->owner) return;
        state->owner->applyLayerWindowImages();
    };
    const auto closed_layers = [weak = std::weak_ptr<CallbackLifetime>(lifetime)] {
        const auto state = weak.lock();
        if (!state || !state->owner) return;
        state->owner->layers_editing_ = false;
        state->owner->updateControls();
    };
    auto undo_layers = std::make_shared<layer::CallbackUndoManager>();
    undo_layers->can_undo = [weak = std::weak_ptr<CallbackLifetime>(lifetime)] {
        const auto state = weak.lock();
        return state && state->owner && !state->owner->undo_stack_.empty();
    };
    undo_layers->can_redo = [weak = std::weak_ptr<CallbackLifetime>(lifetime)] {
        const auto state = weak.lock();
        return state && state->owner && !state->owner->redo_stack_.empty();
    };
    undo_layers->undo_action = [weak = std::weak_ptr<CallbackLifetime>(lifetime)] {
        const auto state = weak.lock();
        if (state && state->owner) (void)state->owner->undo();
    };
    undo_layers->redo_action = [weak = std::weak_ptr<CallbackLifetime>(lifetime)] {
        const auto state = weak.lock();
        if (state && state->owner) (void)state->owner->redo();
    };
    layer_window_ = std::make_shared<layer::ImageLayerWindow>(
        std::move(comm), summary_, mask_, read_only_, image_list_owner_, save_name_,
        std::move(undo_layers), store_layers, services_, closed_layers);
    if (!layer_window_ || !layer_window_->list()) return;
    layer_window_->list()->createDefaultItem = createDefaultItem;
    layer_window_->list()->selectionEvent.push_back([weak = std::weak_ptr<CallbackLifetime>(lifetime)] {
        const auto state = weak.lock();
        if (!state || !state->owner || !state->owner->layer_window_) return;
        const auto selected = state->owner->layer_window_->list()->selection();
        if (selected >= 0 && static_cast<std::size_t>(selected) != state->owner->image_index_)
            state->owner->imageIndex(static_cast<std::size_t>(selected));
    });
    syncLayerWindow();
    layer_window_->show();
    layers_editing_ = true;
    updateControls();
}

void ImageSelectBase::syncLayerWindow() {
    if (!layer_window_ || !layer_window_->list()) return;
    const auto list = layer_window_->list();
    list->mask(mask_);
    list->createDefaultItem = createDefaultItem;
    list->setImages(paths_, static_cast<int>(image_index_));
    list->refImageScale();
}

void ImageSelectBase::applyLayerWindowImages() {
    if (!layer_window_ || !layer_window_->list() || !cardKind() ||
        read_only_ || !enabled_) return;
    const auto lifetime = callback_lifetime_;
    // setCardImages とその publishChange は、それぞれ既存の変更番号を進める。
    // 選択前にコールバックがこのオブジェクトを無効化したり、編集対象の範囲を置き換えたりする場合がある。
    const auto expected_revision = lifetime->notification_revision + 2U;
    const auto selection = layer_window_->list()->selection();
    setCardImages(layer_window_->list()->images(), true);
    if (lifetime->owner != this ||
        lifetime->notification_revision != expected_revision) return;
    if (selection >= 0) imageIndex(static_cast<std::size_t>(selection));
}

void ImageSelectBase::imageIndex(const std::size_t value) {
    if (!cardKind()) return;
    const auto lifetime = callback_lifetime_;
    image_index_ = paths_.empty() ? 0U : std::min(value, paths_.size() - 1U);
    if (layer_window_ && layer_window_->list()) {
        layer_window_->list()->selection(static_cast<int>(image_index_));
    }
    updateControls();
    refreshListImpl(true);
    if (lifetime->owner != this) return;
    updatePreview();
}

bool ImageSelectBase::addLayer() {
    if (!cardKind() || read_only_ || !enabled_) return false;
    const auto lifetime = callback_lifetime_;
    const auto revision = lifetime->notification_revision;
    const auto factory = createDefaultItem;
    auto image = factory ? factory()
                         : std::make_shared<CardImage>(String{}, default_position_);
    if (lifetime->owner != this || lifetime->notification_revision != revision) return false;
    if (!image) image = std::make_shared<CardImage>(String{}, default_position_);
    store();
    binary_paths_.resize(paths_.size());
    binary_paths_.push_back(image && image->type == types::CardImageType::File &&
        isBinImg(image->path()) ? image->path() : String{});
    paths_.push_back(std::move(image));
    image_index_ = paths_.size() - 1U;
    notifyModified();
    return true;
}

bool ImageSelectBase::removeLayer() {
    if (!cardKind() || read_only_ || !enabled_ || paths_.empty()) return false;
    store();
    binary_paths_.resize(paths_.size());
    binary_paths_.erase(binary_paths_.begin() + static_cast<std::ptrdiff_t>(image_index_));
    paths_.erase(paths_.begin() + static_cast<std::ptrdiff_t>(image_index_));
    if (image_index_ >= paths_.size() && !paths_.empty()) --image_index_;
    notifyModified();
    return true;
}

bool ImageSelectBase::moveLayer(const std::ptrdiff_t delta) {
    if (!cardKind() || read_only_ || !enabled_ || paths_.empty()) return false;
    const auto source = static_cast<std::ptrdiff_t>(image_index_);
    const auto target = source + delta;
    if (target < 0 || target >= static_cast<std::ptrdiff_t>(paths_.size())) return false;
    store();
    binary_paths_.resize(paths_.size());
    std::swap(binary_paths_[static_cast<std::size_t>(source)],
              binary_paths_[static_cast<std::size_t>(target)]);
    std::swap(paths_[static_cast<std::size_t>(source)],
              paths_[static_cast<std::size_t>(target)]);
    image_index_ = static_cast<std::size_t>(target);
    notifyModified();
    return true;
}

void ImageSelectBase::store() {
    if (!cardKind()) return;
    undo_stack_.push_back(snapshot());
    redo_stack_.clear();
    refUndoMax();
}

bool ImageSelectBase::undo() {
    if (undo_stack_.empty()) return false;
    redo_stack_.push_back(snapshot());
    const auto value = std::move(undo_stack_.back());
    undo_stack_.pop_back();
    restoreSnapshot(value);
    return true;
}

bool ImageSelectBase::redo() {
    if (redo_stack_.empty()) return false;
    undo_stack_.push_back(snapshot());
    const auto value = std::move(redo_stack_.back());
    redo_stack_.pop_back();
    restoreSnapshot(value);
    return true;
}

void ImageSelectBase::refUndoMax() {
    if (!prop_) return;
    const auto configured = prop_->var().undoMaxEtc.value;
    const auto limit = configured <= 0 ? std::size_t{0} : static_cast<std::size_t>(configured);
    if (undo_stack_.size() > limit) {
        undo_stack_.erase(undo_stack_.begin(),
                          undo_stack_.begin() +
                              static_cast<std::ptrdiff_t>(undo_stack_.size() - limit));
    }
    if (redo_stack_.size() > limit) {
        redo_stack_.erase(redo_stack_.begin(),
                          redo_stack_.begin() +
                              static_cast<std::ptrdiff_t>(redo_stack_.size() - limit));
    }
}

void ImageSelectBase::noCardSizeImage(const bool value) {
    if (!cardKind() || no_card_size_image_ == value) return;
    no_card_size_image_ = value;
    refreshList();
}

void ImageSelectBase::excludeCardSizeImage(const bool value) {
    if (cardKind() || excludeCardSizeImage() == value) return;
    exclude_card_size_image_ = value;
    if (prop_) prop_->var().excludeCardSizeImage.value = value;
    // D版と同様、除外を有効にしたときは、選択されているカードサイズの背景
    // を解除する。 この共有設定の切り替えでは、画像の履歴を追加しない。
    const auto lifetime = callback_lifetime_;
    if (value && !image_path_.empty() && !isBinImg(image_path_)) {
        const auto skin = summSkin();
        if (lifetime->owner != this) return;
        const auto resolved = skin ? skin->findImagePath(image_path_,
            summary_ ? summary_->scenarioPath() : String{},
            summary_ ? summary_->dataVersion() : versioninfo::LATEST_VERSION) : String{};
        if (skin && !resolved.empty() && skin->isCardImage(resolved, false, false)) {
            setBackgroundImage({}, false);
            return;
        }
    }
    const auto revision = lifetime->notification_revision;
    refreshList();
    if (lifetime->owner == this && lifetime->notification_revision == revision)
        refreshWithoutImageList();
}

void ImageSelectBase::notifyModified(const bool preview_changed) {
    publishChange(true, preview_changed, true);
}

void ImageSelectBase::publishChange(const bool modified, const bool preview_changed,
                                   const bool refresh_list) {
    const auto lifetime = callback_lifetime_;
    const auto revision = ++lifetime->notification_revision;
    const auto current = [lifetime, revision, owner = this] {
        return lifetime->owner == owner && lifetime->notification_revision == revision;
    };
    // D版の「更新→プレビュー→変更→画像通知」の順序を維持する。 外部
    // のコードを呼ぶ前に、両方の購読先一覧を固定する。 一覧の変更は次の
    // 通知から反映し、再入による新しい変更があれば、そちらを優先する。
    const auto refresh_callback = refresh_callback_;
    const auto modified_callbacks = modified ? modEvent : std::vector<Action>{};
    const auto image_callbacks = preview_changed ? updateImageEvent : std::vector<Action>{};
    if (refresh_callback) refresh_callback();
    if (!current()) return;
    if (preview_changed) updatePreview();
    if (!current()) return;
    if (modified) updateRows();
    if (!current()) return;
    updateControls();
    for (const auto& callback : modified_callbacks) {
        if (callback) callback();
        if (!current()) return;
    }
    for (const auto& callback : image_callbacks) {
        if (callback) callback();
        if (!current()) return;
    }
    if (refresh_list) refreshImageList();
}

void ImageSelectBase::selectPath(String path, const bool store_value) {
    if (cardKind()) {
        const auto current = image_index_ < paths_.size() ? paths_[image_index_] : CardImagePtr{};
        auto image = std::make_shared<CardImage>(std::move(path),
            current ? current->positionType() : default_position_);
        if (current && current->opEquals(*image)) return;
        if (store_value) store();
        if (image_index_ >= paths_.size()) paths_.resize(image_index_ + 1U);
        // 明示的な埋め込み指定では、新しいバイナリデータを渡す。 通常のファイルや「なし
        // 」をギャラリーで選ぶ場合は、レイヤーごとの独立した元のキャッシュを維持する。
        if (isBinImg(image->path())) {
            binary_paths_.resize(paths_.size());
            binary_paths_[image_index_] = image->path();
        }
        paths_[image_index_] = std::move(image);
        notifyModified();
    } else {
        setBackgroundImage(std::move(path), store_value);
    }
}

bool ImageSelectBase::addDroppedFiles(const std::vector<std::filesystem::path>& files) {
    const auto life = callback_lifetime_;
    if (!life->active || !enabled_ || read_only_ || !summary_ || summary_->scenarioPath().empty()) return false;
    const auto revision = life->notification_revision;
    const auto rows_revision = life->list_revision;
    const auto generation = drop_generation_;
    auto* summary = summary_;
    const auto scenario = summary->scenarioPath();
    const auto current = [&] {
        return life->owner == this && life->active && life->notification_revision == revision &&
               drop_generation_ == generation && summary_ == summary && summary->scenarioPath() == scenario;
    };
    const auto skin = summSkin();
    const auto dialogs = services_.dialogs;
    if (!skin || !dialogs || !current()) return false;
    const auto selected = dirs_->selected_index;
    String directory;
    if (selected && *selected < dirs_->items.size() && *selected >= definition_values_.size()) {
        const auto value = dirs_->items[*selected].value;
        if (value != skinDirectoryKey && value != engineDirectoryKey) directory = value;
    }
    const bool definition_selected = !selected || *selected >= dirs_->items.size() ||
                                     *selected < definition_values_.size();
    // D版と同様、明示的な空のルート・Skin・Engine は、null の定義とは区別する。
    auto destination = structs::pathFromUtf8(definition_selected ? skin->materialPath() : directory);
    const auto scenario_root = structs::pathFromUtf8(scenario).lexically_normal();
    if (destination.is_absolute()) destination = destination.lexically_relative(scenario_root);
    destination = destination.lexically_normal();
    if (destination.has_root_path()) return false;
    for (const auto& component : destination) if (component == "..") return false;
    std::vector<std::filesystem::path> accepted;
    bool non_card_sized = false;
    const auto size = prop_->looks().cardSize();
    for (const auto& file : files) {
        std::error_code error;
        if (!std::filesystem::is_regular_file(file, error) || error) continue;
        const auto path = structs::pathToUtf8(file);
        const bool target = cardKind() ? skin->isCardImage(path, true, can_include_ && !is_menu_card_)
                                       : skin->isBgImage(path, excludeCardSizeImage());
        if (!target || utils::hasPath(StringView(scenario), StringView(path))) continue;
        accepted.push_back(file);
        if (cardKind()) {
            std::uint32_t width{}, height{};
            ::cwxeditor::cwx::imagesize::imageSize(file, width, height);
            non_card_sized |= width != size.width || height != size.height;
        }
    }
    if (accepted.empty() || !current()) return false;
    lib::MessageDialogRequest question;
    question.title = prop_->requiredMsgs().dlgTitDropFiles.value;
    question.message = accepted.size() == 1
        ? utils::tryFormat(prop_->requiredMsgs().dlgMsgDropFile.value, structs::pathToUtf8(accepted.front()))
        : utils::tryFormat(prop_->requiredMsgs().dlgMsgDropFiles.value, accepted.size());
    question.severity = lib::MessageSeverity::Question;
    question.buttons = lib::MessageButtons::YesNo;
    const auto answer = dialogs->message(question);
    if (!current() || life->list_revision != rows_revision || answer != lib::MessageResult::Yes) return false;
    if (non_card_sized && !no_card_size_image_) {
        no_card_size_image_ = true;
        updateControls();
        if (!current()) return false;
    }
    const auto services = services_;
    auto* storage = summary->storage();
    if (!storage) {
        auto* candidate = prop_->storage();
        if (candidate && (!scenario_root.is_absolute() || utils::hasPath(candidate->root(), scenario_root)))
            storage = candidate;
    }
    std::unique_ptr<lib::EditorStorage> native_storage;
    lib::StorageImportRequest request;
    if (storage) request.destination_directory = scenario_root / destination;
    else {
        native_storage = lib::storage_api::make_native_storage(scenario, services, false);
        storage = native_storage.get();
        request.destination_directory = destination;
    }
    if (!storage || !current()) return false;
    request.resolve_destination = [](const auto& original, const auto& available) {
        return utils::createNewFileName(original, false, available);
    };
    auto selection = lib::FilePickerResult::success();
    for (const auto& file : accepted) selection.selections.emplace_back(file);
    const auto imported = lib::import_to_storage(selection, *storage, request);
    if (!imported.succeeded()) throw std::runtime_error(imported.message);
    if (!current()) return true;
    refreshList();
    if (!current()) return true;
    // D版の doExit の refreshPaths(cur) と同様、コピー先のシナリオ
    // 内フォルダーを選択する。 元の選択が「なし」の場合は Material も対象になる。
    const auto imported_directory = (scenario_root / destination).lexically_normal();
    for (std::size_t i = 0; i < directory_choices_.size(); ++i) {
        const auto& choice = directory_choices_[i];
        if (choice.kind == DirectoryChoice::Kind::Scenario &&
            structs::pathFromUtf8(choice.value).lexically_normal() == imported_directory) {
            selectDir(static_cast<int>(definition_values_.size() + i));
            break;
        }
    }
    if (!current()) return true;
    updateControls();
    if (!current()) return true;
    auto* commons = editor_commons_;
    const auto commons_life = editor_commons_lifetime_;
    if (commons && !commons_life.expired())
        commons->refPaths.call(this, structs::pathToUtf8(destination));
    return true;
}

void ImageSelectBase::bindDropTarget() {
    const auto generation = ++drop_generation_;
    const auto life = callback_lifetime_;
    const auto revision = life->notification_revision;
    const bool active = life->active;
    if (!services_.drag_drop || !files_) return;
    const auto transport = services_.drag_drop;
    const auto source = files_;
    transport->unbind(source);
    if (life->owner != this || life->notification_revision != revision ||
        life->active != active || drop_generation_ != generation || !active) return;
    lib::DropTargetBinding binding;
    binding.target = files_;
    binding.accepted_formats = {"text/uri-list", "application/octet-stream"};
    const std::weak_ptr<CallbackLifetime> weak = callback_lifetime_;
    const std::weak_ptr<lib::TableView> table = files_;
    const auto can_accept_copy = [weak, table, generation](const lib::DragEvent& event) {
        const auto life = weak.lock();
        const auto target = table.lock();
        if (!life || !life->owner || !life->active || !target ||
            life->owner->files_ != target || life->owner->drop_generation_ != generation ||
            life->owner->read_only_ || !life->owner->summary_ ||
            life->owner->summary_->scenarioPath().empty()) return false;
        return (static_cast<std::uint32_t>(event.allowed) &
                static_cast<std::uint32_t>(lib::DropOperation::Copy)) != 0U;
    };
    // D版の FileDropTarget と同様、canDrop() がシナリオの Summary を
    // 持つ場合だけ DROP_COPY を提示する。 ドラッグ中の表示にも同じ転送先の条件を適用する。
    binding.on_enter = [can_accept_copy](lib::DragEvent& event) {
        event.selected = can_accept_copy(event) ? lib::DropOperation::Copy
                                                : lib::DropOperation::None;
    };
    binding.on_over = binding.on_enter;
    binding.on_drop = [weak, table, generation](lib::DragEvent& event) {
        if ((static_cast<std::uint32_t>(event.allowed) & static_cast<std::uint32_t>(lib::DropOperation::Copy)) == 0) return;
        const auto life = weak.lock();
        const auto target = table.lock();
        if (!life || !life->owner || !life->active || !target ||
            life->owner->files_ != target || life->owner->drop_generation_ != generation) return;
        const auto files = event.data.files;
        if (life->owner->addDroppedFiles(files)) {
            event.selected = lib::DropOperation::Copy;
            event.handled = true;
        }
    };
    transport->bind_target(std::move(binding));
}

void ImageSelectBase::addExternalFiles(const std::vector<String>& paths) {
    if (paths.empty() || !enabled_ || read_only_) return;
    if (cardKind()) {
        bool changed = false;
        store();
        for (const auto& path : paths) {
            if (path.empty() || (isBinImg(path) && !can_include_)) continue;
            binary_paths_.resize(paths_.size());
            binary_paths_.push_back(isBinImg(path) ? path : String{});
            paths_.push_back(std::make_shared<CardImage>(path, default_position_));
            changed = true;
        }
        if (changed) {
            image_index_ = paths_.size() - 1U;
            notifyModified();
        } else if (!paths_.empty()) {
            undo_stack_.pop_back();
        }
    } else {
        selectPath(paths.front(), true);
    }
}

void ImageSelectBase::openImageList() {
    if (!summary_) return;
    if (image_list_ && image_list_->closed()) image_list_.reset();
    if (image_list_) return;
    auto options = imagelistwindow::ImageListWindowOptions{};
    options.skin = summSkin();
    options.image_scale = prop_->imageScaleFactor();
    options.drawing_scale = static_cast<int>(prop_->drawingScale());
    options.graphics = services_.graphics;
    options.gui = services_.gui;
    options.drag_drop = services_.drag_drop;
    // D版のモードレスの ImageList は選択専用。 取
    // り込み・ドロップは、検証、コピー確認、相対パス化を行える
    // MaterialSelect のファイル一覧で扱う。
    options.enable_drag_drop = false;
    options.font = lib::effective_editor_widget_font();
    if (const auto measured = lib::measured_effective_ui_text_size("Mg", options.font))
        options.font_height = measured->height;
    else
        options.font_height = std::max(14.0F, options.font.points * 1.25F);
    if (const auto measured = lib::measured_effective_ui_text_size("M", options.font))
        options.character_width = measured->width;
    else
        options.character_width = std::max(7.0F, options.font.points * 0.6F);
    const std::weak_ptr<CallbackLifetime> size_lifetime = callback_lifetime_;
    options.save_window_size = [size_lifetime, card = cardKind()](const lib::Size value) {
        const auto life = size_lifetime.lock();
        if (!life || !life->owner || !life->owner->prop_) return;
        auto& variables = life->owner->prop_->var();
        const auto width = static_cast<std::int32_t>(std::lround(std::max(1.0F, value.width)));
        const auto height = static_cast<std::int32_t>(std::lround(std::max(1.0F, value.height)));
        if (card) {
            variables.imageListWidth.value = width;
            variables.imageListHeight.value = height;
        } else {
            variables.imageListForBgImagesWidth.value = width;
            variables.imageListForBgImagesHeight.value = height;
        }
    };
    if (cardKind()) {
        options.window_size = {
            static_cast<float>(prop_->var().imageListWidth.value),
            static_cast<float>(prop_->var().imageListHeight.value)};
        const auto card = prop_->looks().cardSize();
        options.card_image_size = {
            static_cast<float>(prop_->s(static_cast<int>(card.width))),
            static_cast<float>(prop_->s(static_cast<int>(card.height)))};
    } else {
        options.window_size = {
            static_cast<float>(prop_->var().imageListForBgImagesWidth.value),
            static_cast<float>(prop_->var().imageListForBgImagesHeight.value)};
        options.background_image_size = {
            static_cast<float>(prop_->var().bgImageSampleWidth.value),
            static_cast<float>(prop_->var().bgImageSampleHeight.value)};
    }
    const auto lifetime = callback_lifetime_;
    const std::weak_ptr<CallbackLifetime> weak = lifetime;
    const auto gallery_skin_resolver = [weak](const imagelistwindow::CProps&, const Summary*) -> SkinPtr {
        const auto life = weak.lock();
        if (!life || !life->owner || !life->active) return {};
        const auto notification = life->notification_revision;
        const auto list_revision = life->list_revision;
        const auto* owner = life->owner;
        SkinPtr skin;
        if (owner->force_skin_) {
            skin = owner->force_skin_;
        } else if (owner->summary_ && !owner->summary_->scenarioPath().empty() &&
                   owner->editor_commons_ && !owner->editor_commons_lifetime_.expired() &&
                   owner->editor_commons_->summary().get() == owner->summary_) {
            skin = owner->editor_commons_->skin();
        } else {
            skin = owner->summSkin();
        }
        if (!life->owner || !life->active ||
            life->notification_revision != notification ||
            life->list_revision != list_revision) return {};
        return skin;
    };
    options.skin_resolver = gallery_skin_resolver;
    options.image_resource = [weak](const StringView path, const bool) -> lib::ResourceId {
        const auto life = weak.lock();
        if (!life || !life->owner) return 0U;
        auto* props = life->owner->prop_;
        return props->images().load(path);
    };
    options.image_factory = [weak, gallery_skin_resolver](
                                const StringView path, const bool mask_value) {
        const auto life = weak.lock();
        if (!life || !life->owner || !life->active)
            return imagelistwindow::ImageDataWithScale{};
        const auto notification = life->notification_revision;
        const auto list_revision = life->list_revision;
        auto* const owner = life->owner;
        auto* const props = owner->prop_;
        auto* const summary = owner->summary_;
        if (!props) return imagelistwindow::ImageDataWithScale{};
        const auto drawing_scale = props->drawingScale();
        const bool is_image_cell = !owner->cardKind();
        const auto current_skin = gallery_skin_resolver(props->parent(), summary);
        if (!life->owner || !life->active ||
            life->notification_revision != notification ||
            life->list_revision != list_revision || !current_skin)
            return imagelistwindow::ImageDataWithScale{};
        return convertImage(areaviewutils::detail::detailLoadImage(
            *props, *current_skin, summary, path, drawing_scale, {}, mask_value,
            false, is_image_cell));
    };
    // D版と同様、明示的な forceSkin だけを固定する。通常のギャラリーでは、 createImage
    // が素材パスを要求した時点で、所有する Commons の現在のスキンを解決する。
    const auto list = std::make_shared<imagelistwindow::ImageListWindowBase>(
        prop_->parent(), *summary_, force_skin_, image_list_owner_,
        guardCallback([this](const StringView path) { selectPath(String(path), true); }),
        combo_file_selector_ ? std::static_pointer_cast<lib::Widget>(file_combo_)
                             : std::static_pointer_cast<lib::Widget>(files_),
        std::move(options), cardKind());
    if (lifetime->owner != this) return;
    image_list_ = list;
    bindFileActions(std::static_pointer_cast<lib::Widget>(list->widget()), true);
    refreshImageList();
    if (lifetime->owner != this || image_list_ != list) return;
    list->open();
}

void ImageSelectBase::closeImageList() {
    // 終了通知によって代わりの画面が開かれる場合がある。 通知後に新しい
    // メンバーを消してしまわないよう、通知前に古いラッパーを切り離す。
    auto closing = std::move(image_list_);
    if (closing) closing->close();
}

void ImageSelectBase::saveIncludedImage() {
    if (!enabled_) return;
    const auto source = cachedIncludedImage();
    if (!isBinImg(source) || !services_.dialogs || !services_.file_picker || !services_.filesystem) return;
    const auto lifetime = callback_lifetime_;
    const auto save_revision = ++lifetime->save_revision;
    const auto notification = lifetime->notification_revision;
    const auto current = [lifetime, owner = this, save_revision, notification,
                          source, index = image_index_] {
        if (lifetime->owner != owner || lifetime->save_revision != save_revision ||
            lifetime->notification_revision != notification || !owner->enabled_)
            return false;
        return owner->cardKind()
            ? owner->image_index_ == index && owner->cachedIncludedImage() == source
            : owner->image_path_ == source;
    };
    // 保存名の問い合わせやネイティブの提供処理は、この選択欄を閉じるなどのUIイベント
    // を処理する場合がある。 呼び出す前に、操作が使うすべてのものを所有して保持する。
    const auto save_name = save_name_;
    const auto dialogs = services_.dialogs;
    const auto picker = services_.file_picker;
    const auto filesystem = services_.filesystem;
    const auto fallback_name = prop_->var().noFileName.value;
    const auto error_title = prop_->requiredMsgs().dlgTitError.value;
    const auto scenario_path = summary_ ? summary_->scenarioPath() : String{};
    lib::FilePickerRequest request;
    request.title = prop_->requiredMsgs().dlgTitSaveBitmapImage.value;
    request.initial_directory = structs::pathFromUtf8(scenario_path);
    request.allow_multiple = false;
    request.confirm_overwrite = true;
    const auto bytes = utils::strToBImg(source);
    const auto extension = imagesize::imageType(bytes);
    const auto& messages = prop_->requiredMsgs();
    const String filter_name = extension == ".jpg" ? messages.filterJPEGImage.value
        : extension == ".gif" ? messages.filterGIFImage.value
        : extension == ".tiff" ? messages.filterTIFFImage.value
        : extension == ".bmp" ? messages.filterBitmapImage.value
        : extension == ".png" ? messages.filterPNGImage.value
        : extension == ".webp" ? messages.filterWEBPImage.value : messages.exeFileDescAll.value;
    request.filters.push_back({filter_name, {extension.empty() ? "*.*" : "*" + extension}, {}});
    const auto reportFailure = [dialogs, current, error_title](std::string failure) {
        if (failure.empty() || !current()) return;
        lib::MessageDialogRequest message;
        message.title = error_title;
        message.message = "画像を保存できませんでした。";
        message.details = std::move(failure);
        message.severity = lib::MessageSeverity::Error;
        if (dialogs->supports_message_async())
            (void)dialogs->message_async(message, [](lib::MessageResult) {});
        else
            (void)dialogs->message(message);
    };
    const auto savePicked = [current, dialogs, filesystem, reportFailure,
                             bytes](lib::FilePickerResult picked) {
        if (picked.cancelled()) return;
        std::string failure;
        try {
            if (picked.failed() || picked.selections.empty())
                throw std::runtime_error(picked.message.empty()
                    ? "画像の保存先を選択できませんでした" : picked.message);
            const auto destination = picked.selections.front().native_path();
            if (!destination)
                throw std::runtime_error(
                    "この画像の保存先はnative file pathを提供していません");
            lib::FileSaveTransaction save_path(*dialogs, *destination);
            // 後の保存操作に置き換わった場合や、選択欄が閉じた・接続先が変わった場合も、
            // 選択した一時保存領域の所有権は解放するが、古い要求の書き込みは開始しない。
            if (!current()) return;
            std::string error;
            const auto data = std::span<const std::byte>(
                reinterpret_cast<const std::byte*>(bytes.data()), bytes.size());
            if (!filesystem->durable_write(*destination, data, error))
                throw std::runtime_error(error.empty()
                    ? "画像を書き出せませんでした" : error);
            if (!save_path.commit(error))
                throw std::runtime_error(error.empty()
                    ? "画像を選択先へ保存できませんでした" : error);
        } catch (const std::exception& error) {
            failure = error.what();
        } catch (...) {
            failure = "画像の保存中に不明なエラーが発生しました";
        }
        // 共通のエラーダイアログを出す前に、ガードが失敗した一時保存領
        // 域を解放済み。 アプリの外側のイベントループを終了させないよ
        // う、 通常の入出力エラーをUIコールバックの外へ投げない。
        reportFailure(std::move(failure));
    };
    try {
        String name = save_name ? utils::toFileName(utils::astrip(save_name())) : fallback_name;
        if (!current()) return;
        if (name.empty()) name = fallback_name;
        auto filename = structs::pathFromUtf8(name);
        filename.replace_extension(extension);
        request.initial_name = structs::pathToUtf8(filename);
        if (request.initial_directory.empty()) request.initial_directory = std::filesystem::current_path();
        const auto gui = services_.gui;
        if (gui && lib::editor_uses_async_dialog_flow(gui->platform())) {
            if (!picker->save_file_async(request, savePicked))
                reportFailure("保存先選択ダイアログを開けませんでした");
            return;
        }
        savePicked(picker->save_file(request));
    } catch (const std::exception& error) {
        reportFailure(error.what());
    } catch (...) {
        reportFailure("画像の保存中に不明なエラーが発生しました");
    }
}

void ImageSelectBase::openMaterialDirectory() {
    const bool data_explorer = lib::editor_window_host_options().all_mode &&
                               lib::editor_can_open_data_directory();
    if (!enabled_ || (!services_.shell && !data_explorer)) return;
    const auto shell = services_.shell;
    materialselect::MaterialDirectoryQuery query;
    if (summary_) query.scenario_root = structs::pathFromUtf8(summary_->scenarioPath());
    const auto skin = summSkin();
    if (skin) query.material_directory = structs::pathFromUtf8(skin->materialPath());
    if (dirs_->selected_index && *dirs_->selected_index < dirs_->items.size() &&
        *dirs_->selected_index >= definition_values_.size()) {
        const auto& value = dirs_->items[*dirs_->selected_index].value;
        if (value == skinDirectoryKey) {
            // D版の doDirectory と同様、Scenario/Material
            // を代わりに使うのではなく、 選択されたスキンの標準素材ディレクトリを調べる。
            if (skin) {
                for (const auto& source : skin->tableDirs())
                    query.default_directories.push_back(structs::pathFromUtf8(source));
                const bool card = cardKind();
                const bool size_filter = card ? no_card_size_image_ : excludeCardSizeImage();
                const bool included = can_include_ && !is_menu_card_;
                query.contains_material = [skin, card, size_filter, included](const auto& path) {
                    const auto source = structs::pathToUtf8(path);
                    return card ? skin->hasCardImage(source, false, size_filter, included)
                                : skin->hasBgImage(source, false, size_filter);
                };
            }
        } else if (value == engineDirectoryKey) {
            const auto engine = prop_ ? prop_->enginePath() : String{};
            if (engine.empty()) return;
            query.selected_directory = structs::pathFromUtf8(engine).parent_path() / "Data" / "Materials";
        } else {
            query.selected_directory = structs::pathFromUtf8(value);
        }
    }
    const auto directory = materialselect::materialDirectoryToOpen(std::move(query));
    if (directory.empty()) return;
    if (data_explorer) {
        (void)lib::editor_open_data_directory(directory);
        return;
    }
    std::string error;
    shell->open_path(directory, error);
}

void ImageSelectBase::updateControls() {
    const bool editable = enabled_ && !read_only_;
    for (const auto& button : {add_button_, remove_button_, up_button_, down_button_,
                               refresh_button_, directory_button_, image_list_button_,
                               save_button_, layer_button_}) {
        if (button) button->set_enabled(editable);
    }
    if (refresh_button_) refresh_button_->set_enabled(enabled_);
    if (directory_button_) directory_button_->set_enabled(enabled_ &&
        (bool(services_.shell) || (lib::editor_window_host_options().all_mode &&
                                   lib::editor_can_open_data_directory())));
    if (files_) files_->set_enabled(editable);
    if (file_combo_) file_combo_->set_enabled(editable);
    if (dirs_) dirs_->set_enabled(editable);
    if (option_) {
        option_->set_enabled(editable);
        option_->checked = cardKind() ? no_card_size_image_ : excludeCardSizeImage();
    }
    if (save_button_) {
        const bool included = !cachedIncludedImage().empty();
        save_button_->set_visible(included);
        save_button_->set_enabled(enabled_ && included);
        if (combo_file_selector_) {
            save_button_->set_layout_item({.row = 0U,
                                           .column = 1U,
                                           .horizontal = lib::Align::Center,
                                           .vertical = lib::Align::Center,
                                           .minimum = included ? lib::Size{27.0F, 26.0F}
                                                               : lib::Size{},
                                           .preferred = included ? lib::Size{27.0F, 26.0F}
                                                                 : lib::Size{}});
        }
    }
    if (layer_button_) {
        layer_button_->set_enabled(enabled_ && cachedIncludedImage().empty() &&
            (read_only_ ? ((!summary_ || !summary_->legacy()) && !paths_.empty())
                        : (!summary_ || !summary_->legacy() || paths_.size() > 1U)));
        layer_button_->set_tooltip(
            prop_->requiredMsgs().menuText(types::MenuID::EditLayers) +
            (layers_editing_ ? " *" : ""));
    }
    if (layer_name_) {
        layer_name_->text = ::cwxeditor::cwx::utils::tryFormat(
            prop_->requiredMsgs().layerName.value, static_cast<int>(image_index_ + 1U));
    }
}

void ImageSelectBase::updateRows() {
    const auto lifetime = callback_lifetime_;
    const auto revision = ++lifetime->list_revision;
    const auto notification = lifetime->notification_revision;
    auto prepared = prepareRows(showing_paths_, showing_labels_, definition_values_, revision);
    if (lifetime->owner != this || lifetime->list_revision != revision ||
        lifetime->notification_revision != notification || !prepared) return;
    (void)publishRows(std::move(*prepared));
}

std::optional<ImageSelectBase::PreparedRows> ImageSelectBase::prepareRows(
    const std::vector<String>& input_paths, const std::vector<String>& labels,
    const std::vector<String>& definitions, const std::uint64_t revision) const {
    const auto lifetime = callback_lifetime_;
    const auto notification = lifetime->notification_revision;
    const auto current = [&] {
        return lifetime->owner == this && lifetime->list_revision == revision &&
               lifetime->notification_revision == notification;
    };
    const auto paths = input_paths;
    const auto prop = prop_;
    const bool card = cardKind();
    const String target = card
        ? (image_index_ < paths_.size() && paths_[image_index_]
            ? paths_[image_index_]->path() : String{}) : image_path_;
    PreparedRows result;
    if (file_combo_) {
        result.choices.reserve(paths.size());
        result.choice_selection.reset();
        for (std::size_t i = 0; i < paths.size(); ++i) {
            const String label = i < labels.size() ? labels[i] : fileLabel(paths[i]);
            result.choices.push_back({label, paths[i], 0, true});
            if (!target.empty() && sameString(paths[i], target)) result.choice_selection = i;
        }
        return result;
    }
    const bool show_definitions = card && paths.empty();
    const std::size_t file_offset = show_definitions ? definitions.size() : 0U;
    result.rows.reserve(file_offset + paths.size());
    lib::ResourceId definition_icon{};
    if (file_offset != 0) {
        definition_icon = prop->images().load(dprops::StringView{"empty.png"});
        if (!current()) return std::nullopt;
    }
    const auto current_skin = summSkin();
    if (!current()) return std::nullopt;
    const auto type = card ? materialselect::MtType::CARD
                           : materialselect::MtType::BG_IMG;
    const bool included = can_include_ && !is_menu_card_;
    const String scenario_path = summary_ ? summary_->scenarioPath() : String{};
    const String data_version = summary_ ? summary_->dataVersion()
                                         : versioninfo::LATEST_VERSION;
    if (show_definitions) {
        for (std::size_t i = 0; i < definitions.size(); ++i) {
            result.rows.push_back({{{definitions[i], "definition:" + std::to_string(i),
                                      definition_icon, false, true}}, false});
        }
    }
    for (std::size_t i = 0; i < paths.size(); ++i) {
        String resolved = paths[i];
        if (current_skin) {
            bool skin_material = false, engine_material = false;
            resolved = current_skin->findImagePathF(
                paths[i], scenario_path, data_version, skin_material, engine_material);
            if (!current()) return std::nullopt;
        }
        const String label = i < labels.size() ? labels[i] : fileLabel(paths[i]);
        result.rows.push_back({{{label, paths[i], 0, false, true}}, false});
        auto& row = result.rows.back();
        if (prop && !paths[i].empty()) {
            row.cells.front().icon = materialselect::materialRowIcon(
                *prop, type, resolved, current_skin.get(), included);
            if (!current()) return std::nullopt;
        }
        if (current_skin && !(card && isTalkerImagePath(paths[i]))) {
            row.warnings = warningFrom(paths[i], summary_ && summary_->legacy(), *current_skin);
            const auto encoding = ::cwxeditor::cwx::warning::sjisWarnings(prop->parent(), summary_, paths[i], "");
            row.warnings.insert(row.warnings.end(), encoding.begin(), encoding.end());
            const auto extension = ::cwxeditor::cwx::warning::fileExtensionWarnings(prop->parent(), resolved);
            row.warnings.insert(row.warnings.end(), extension.begin(), extension.end());
            row.cells.front().warnings = row.warnings;
            if (!current()) return std::nullopt;
        }
        if (result.selection.empty() && !target.empty() && sameString(paths[i], target))
            result.selection.push_back(file_offset + i);
    }
    return result;
}

bool ImageSelectBase::publishRows(PreparedRows rows) {
    if (file_combo_) {
        const auto target=file_combo_;
        const auto lifetime=callback_lifetime_;
        const auto revision=lifetime->list_revision;
        const auto notification=lifetime->notification_revision;
        return lib::replace_combo_items(target, std::move(rows.choices), rows.choice_selection, {},
            [lifetime,revision,notification] {
                return lifetime->owner && lifetime->list_revision==revision &&
                       lifetime->notification_revision==notification;
            });
    } else if (files_) {
        const auto target=files_;
        const auto lifetime=callback_lifetime_;
        const auto revision=lifetime->list_revision;
        const auto notification=lifetime->notification_revision;
        const auto reveal=rows.selection.empty()?std::optional<std::size_t>{}:rows.selection.front();
        return lib::replace_table_rows(*target, std::move(rows.rows), std::move(rows.selection), reveal,
            [lifetime,revision,notification] {
                return lifetime->owner && lifetime->list_revision==revision &&
                       lifetime->notification_revision==notification;
            });
    }
    return true;
}

void ImageSelectBase::selectFileRow(const std::size_t row) {
    const auto lifetime = callback_lifetime_;
    const auto revision = lifetime->notification_revision;
    const bool show_definitions = !combo_file_selector_ &&
                                  (cardKind() && showing_paths_.empty());
    if (show_definitions && row < definition_values_.size()) {
        const auto binary_path = cachedIncludedImage();
        const bool included = !binary_path.empty();
        if (cardKind()) {
            const auto definition = value_from_definition_;
            auto image = definition
                             ? definition(static_cast<int>(row), included,
                                                      binary_path)
                             : CardImagePtr{};
            if (lifetime->owner != this || lifetime->notification_revision != revision) return;
            if (!image && row == 0) image = std::make_shared<CardImage>(String{});
            if (image) {
                if (image_index_ >= paths_.size()) paths_.resize(image_index_ + 1U);
                store();
                paths_[image_index_] = std::move(image);
                notifyModified();
            }
        } else if (row == 0) {
            setBackgroundImage({}, true);
        } else if (included && !binary_path.empty()) {
            setBackgroundImage(std::move(binary_path), true);
        }
        return;
    }
    const auto index = row - (show_definitions ? definition_values_.size() : 0U);
    if (index >= showing_paths_.size()) return;
    const auto selected_path = showing_paths_[index];
    if (dirs_->selected_index && *dirs_->selected_index < definition_values_.size()) {
        // スキン、エンジン、シナリオルートの各グループには、同じファイル名が存
        // 在し得る。 文字列だけで照合せず、クリックされた行の出所を保持する。
        if (index >= showing_directory_indices_.size()) return;
        const auto owner = showing_directory_indices_[index];
        if (owner >= directory_choices_.size()) return;
        selectDir(static_cast<int>(definition_values_.size() + owner));
        if (lifetime->owner != this || lifetime->notification_revision != revision) return;
    }
    selectPath(selected_path, true);
}

void ImageSelectBase::clearPreviewResource() noexcept {
    ++callback_lifetime_->preview_revision;
    auto retired = std::move(preview_owned_resources_);
    const auto preview = preview_;
    if (preview) {
        preview->image = 0U;
        preview->layers.clear();
        preview->accessibility_description.clear();
    }
    // 生成元が所有するアップロードを解放する前に、空の状態を反映する。 破棄コ
    // ールバックから、消去途中の有効なリソースベクターが見えないようにする。
}

float ImageSelectBase::compactForAll() {
    float removed = 0.0F;
    if (files_) {
        // 一覧の一行の表示寸法。設計上の論理単位で表す。
        const float unit = std::max(0.01F, lib::scaled_ui_metric(1.0F));
        const float row = lib::item_row_height(*files_, 0.0F) / unit;
        removed = (row > 1.0F ? row : 20.0F) + 10.0F;
        auto item = files_->layout_item();
        item.minimum.height = std::max(0.0F, item.minimum.height - removed);
        item.preferred.height = std::max(0.0F, item.preferred.height - removed);
        // ALL仕様: 画像のパス表示(一覧)を50px広げる。
        constexpr float kWiderFiles = 50.0F;
        item.minimum.width += kWiderFiles;
        item.preferred.width += kWiderFiles;
        files_->set_layout_item(item);
        if (const auto right = right_panel_.lock()) {
            auto panel_item = right->layout_item();
            panel_item.minimum.height =
                std::max(0.0F, panel_item.minimum.height - removed);
            panel_item.minimum.width += kWiderFiles;
            right->set_layout_item(panel_item);
        }
    }
    // プレビューを縦方向に中央寄せすると、「レイヤ 1
    // 」とカードの間に大きな空白ができる。 カードは見
    // 出しの直下に置き、余った高さはカードの下に回す。
    if (const auto spacer = preview_top_spacer_.lock()) {
        auto item = spacer->layout_item();
        item.grow = 0.0F;
        spacer->set_layout_item(item);
    }
    if (const auto left = left_panel_.lock()) {
        if (const auto layout =
                std::dynamic_pointer_cast<lib::LinearLayout>(left->layout())) {
            layout->spacing = 0.0F;
            layout->padding.top = 0.0F;
            layout->padding.bottom = 0.0F;
        }
    }
    return removed;
}

void ImageSelectBase::updatePreview() {
    if (compact_ || !preview_) return;
    const auto lifetime = callback_lifetime_;
    const auto revision = ++lifetime->preview_revision;
    const auto notification = lifetime->notification_revision;
    const auto current = [lifetime, revision, notification, owner = this] {
        return lifetime->owner == owner && lifetime->preview_revision == revision &&
               lifetime->notification_revision == notification;
    };
    const auto preview = preview_;
    const auto graphics = services_.graphics;
    const auto gui = services_.gui;
    auto* props = prop_;
    auto* summary = summary_;
    if (!props) return;
    const auto current_skin = summSkin();
    const auto card_kind = cardKind();
    const auto default_position = default_position_;
    const auto insets = insets_;
    const auto mask_value = mask_;
    const auto width = width_, height = height_;
    const auto background = image_path_;
    const auto load_scaled = load_scaled_image_;
    const auto scenario_path = summary ? summary->scenarioPath() : String{};
    const auto data_version = summary ? summary->dataVersion() : String(versioninfo::LATEST_VERSION);
    const auto drawing_scale = props->drawingScale();
    const auto drawing_scale_for_image = props->drawingScaleForImage(summary);
    const auto image_scale = props->imageScaleFactor();
    const auto interpolation = preview->interpolation;
    const auto design_size = preview->design_size.value_or(lib::Size{static_cast<float>(width), static_cast<float>(height)});
    const auto cfont = props->adjustFont(props->looks().pcNumberFont(current_skin && current_skin->legacy()));
    const lib::FontSpec number_font{cfont.name, static_cast<float>(cfont.point),
        static_cast<std::uint16_t>(cfont.bold ? 700 : 400), cfont.italic};
    std::vector<CardImagePtr> paths;
    paths.reserve(paths_.size());
    for (const auto& image : paths_)
        paths.push_back(image ? std::make_shared<CardImage>(nullptr, *image) : CardImagePtr{});
    // どの提供処理もアップロードを生成する前に、所有権を保持するスロットを確
    // 保する。 確定までは、以前のフレームとそのアップロードを変更しない。
    std::vector<std::shared_ptr<lib::OwnedImageResource>> resources;
    resources.reserve(card_kind ? paths.size() : 1U);
    for (std::size_t i = 0; i < (card_kind ? paths.size() : 1U); ++i)
        resources.push_back(std::make_shared<lib::OwnedImageResource>(graphics));
    std::vector<lib::ImageLayerSpec> layers;
    layers.reserve(resources.size());
    String description_text;
    const auto append=[&](imagelistwindow::ImageDataWithScale loaded,
                          types::CardImagePosition position, structs::CInsets insets,
                          bool shrink, StringView description,
                          const std::shared_ptr<lib::OwnedImageResource>& lease) {
        auto resource=loaded.resource;
        if (loaded.owned_resource) lease->id = resource;
        if (!current()) return;
        // 借用したキャッシュの復号データは、独立してアップロードする。 キャッ
        // シュの無効化によって、ここで表示中の画像が無効にならないようにする。
        if ((!resource || !loaded.owned_resource) && graphics && !loaded.data.frames.empty()) {
            lease->id = graphics->create_image(loaded.data);
            resource = lease->id;
            if (!current()) return;
            if (!resource) throw std::runtime_error("ImageSelect preview upload failed");
        }
        if(!resource) return;
        const auto size=loaded.size();
        const bool centered=position!=types::CardImagePosition::TopLeft;
        layers.push_back({resource,0,size,
            centered?lib::Point{}:lib::Point{static_cast<float>(insets.w),static_cast<float>(insets.n)},
            centered,shrink,interpolation,image_scale});
        if(!description_text.empty()) description_text+="\n";
        description_text+=description;
    };
    const auto load_file=[&](StringView value) {
        imagelistwindow::ImageDataWithScale result;
        if(!current_skin || value.empty()) return result;
        bool skin_material=false,engine_material=false;
        const auto path=current_skin->findImagePathF(value,scenario_path,
            data_version,skin_material,engine_material);
        if(path.empty()) return result;
        const auto scale=!(skin_material||engine_material)
            ? ((load_scaled && !load_scaled()) ? 1U : drawing_scale_for_image)
            : drawing_scale;
        if (!current()) return result;
        // 既存の元画像のマスク・復号経路は変えず、合成だけを変
        // える。 このプレビューは下で独自の所有権を保持する。
        // 先に同じ復号済み画素を Props::Images
        // に渡すと、 管理領域用に復号・アップロードした直
        // 後に、プレビュー用にも再びアップロードしてしまう。
        return convertImage(areaviewutils::detail::detailLoadImage(
            *props, *current_skin, summary, path, scale,
            {static_cast<float>(width), static_cast<float>(height)},
            mask_value, false, !card_kind));
    };
    if(card_kind) {
        for(std::size_t index = 0; index < paths.size(); ++index) {
            if (!current()) return;
            const auto& image = paths[index];
            const auto& lease = resources[index];
            if(!image) continue;
            if(image->type==types::CardImageType::File) {
                auto position=image->positionType();
                if(position==types::CardImagePosition::Default) position=default_position;
                append(load_file(image->path()),position,insets,false,image->path(),lease);
            } else if(image->type==types::CardImageType::Talker) {
                if(image->talker()==types::Talker::Card) {
                    if(current_skin) append(dskin::menuCard(*current_skin,drawing_scale),
                        types::CardImagePosition::Center,{},false,"card",lease);
                } else {
                    StringView resource;
                    switch(image->talker()) {
                    case types::Talker::Selected: resource="talker_sel.png"; break;
                    case types::Talker::Unselected: resource="talker_unsel.png"; break;
                    case types::Talker::Random: resource="talker_random.png"; break;
                    case types::Talker::Valued: resource="talker_valued.png"; break;
                    case types::Talker::Card: break;
                    }
                    imagelistwindow::ImageDataWithScale loaded;
                    loaded.resource=props->images().load(resource);
                    append(std::move(loaded),types::CardImagePosition::Center,insets,false,resource,lease);
                }
            } else if(image->type==types::CardImageType::PCNumber && image->pcNumber()!=0 && graphics) {
                const auto size=design_size;
                if(size.width<=0 || size.height<=0 || size.width>4096 || size.height>4096) continue;
                lib::ImageData data; lib::ImageFrame frame;
                frame.pixel_width=static_cast<unsigned>(size.width); frame.pixel_height=static_cast<unsigned>(size.height);
                frame.pixel_format=lib::PixelFormat::RGBA32; frame.logical_size=size;
                frame.pixels.resize(static_cast<std::size_t>(frame.pixel_width)*frame.pixel_height*4);
                data.frames.push_back(std::move(frame));
                const auto text=std::to_string(image->pcNumber());
                const auto measured=graphics->measure_text(text,number_font,0);
                if (!current()) return;
                const lib::Rect rect{std::floor((size.width-measured.width)*.5F),std::floor((size.height-measured.height)*.5F),
                    measured.width,measured.height};
                const auto painted = graphics->draw_text_to_image(data,0,text,rect,number_font,{0,0,0,255});
                if (!current()) return;
                if(painted) {
                    // 文字はUI文字倍率ではなく、設計上のサイズですでにラスタライズ済み。
                    imagelistwindow::ImageDataWithScale loaded(std::move(data));
                    loaded.logical_size={size.width/image_scale,size.height/image_scale};
                    append(std::move(loaded),types::CardImagePosition::Center,{},false,text,lease);
                }
            }
        }
    } else {
        append(load_file(background),types::CardImagePosition::Center,insets,true,background,resources.front());
    }
    if (!current()) return;
    preview->image = layers.empty() ? 0U : layers.front().image;
    preview->layers.swap(layers);
    preview->accessibility_description.swap(description_text);
    preview_owned_resources_.swap(resources);
    // GUIの提供処理や、古いリソースのデストラクターの後はメンバーにアクセスしない。
    // 再入した更新からは、新しい画像・レイヤー・説明の完成した組だけが見えるようにする。
    if (gui) gui->request_redraw(preview);
}

ImageSelectBase::Snapshot ImageSelectBase::snapshot() const {
    Snapshot value;
    value.image_index = image_index_;
    value.image_path = image_path_;
    value.binary_paths = binary_paths_;
    value.paths.reserve(paths_.size());
    for (const auto& image : paths_)
        value.paths.push_back(cloneImage(image, true));
    return value;
}

void ImageSelectBase::restoreSnapshot(const Snapshot& value) {
    paths_.clear();
    paths_.reserve(value.paths.size());
    for (const auto& image : value.paths)
        paths_.push_back(cloneImage(image, false));
    image_path_ = value.image_path;
    binary_paths_ = value.binary_paths;
    binary_paths_.resize(paths_.size());
    image_index_ = value.image_index;
    if (paths_.empty()) image_index_ = 0U;
    else if (image_index_ >= paths_.size()) image_index_ = paths_.size() - 1U;
    syncLayerWindow();
    if (layer_window_ && summary_ && summary_->legacy() && paths_.size() <= 1U)
        layer_window_->close();
    if (layer_window_ && layer_window_->list()) layer_window_->list()->refImageScale();
    // 取り消し・やり直しも画像の編集として扱う。履歴をさらに追加せ
    // ず、 プレビューのコールバックより前に、親の変更を通知する。
    publishChange(true, true, true);
}

CardImagePtr ImageSelectBase::cloneImage(const CardImagePtr& value,
                                         const bool undo_snapshot) const {
    if (!value) return {};
    auto result = std::make_shared<CardImage>(nullptr, *value);
    // D版と同様、ImagesUndo が保持する複製だけを UseCounter.sub
    // に登録する。 パスの改名には追従させるが、表示する参照回数には加
    // 算しない。 復元された編集用の複製は、意図的に未登録のままにする。
    if (undo_snapshot && summary_ && summary_->useCounter())
        result->setUseCounter(summary_->useCounter()->sub());
    return result;
}

std::vector<String> ImageSelectBase::directoryPaths() const {
    std::vector<String> result;
    const auto current_skin = summSkin();
    if (!current_skin) return result;
    if (summary_ && !summary_->scenarioPath().empty()) {
        const auto scenario_root = structs::pathFromUtf8(summary_->scenarioPath());
        const auto& ignored = prop_->var().ignorePaths.value;
        const auto contains_target = [this, &current_skin](const std::filesystem::path& path) {
            const auto directory = pathToUtf8(path);
            return cardKind()
                       ? current_skin->hasCardImage(directory, true,
                                                    no_card_size_image_,
                                                    can_include_ && !is_menu_card_)
                       : current_skin->hasBgImage(directory, true,
                                                  excludeCardSizeImage());
        };
        const auto ignored_directory = [&ignored](const std::filesystem::path& path) {
            const auto name = pathToUtf8(path.filename());
            return std::find(ignored.begin(), ignored.end(), name) != ignored.end();
        };

        std::error_code error;
        if (std::filesystem::is_directory(scenario_root, error)) {
            if (contains_target(scenario_root)) appendUnique(result, pathToUtf8(scenario_root));
            std::filesystem::recursive_directory_iterator iterator(
                scenario_root, std::filesystem::directory_options::skip_permission_denied,
                error);
            const std::filesystem::recursive_directory_iterator end;
            while (!error && iterator != end) {
                const auto entry = *iterator;
                if (entry.is_directory(error)) {
                    if (ignored_directory(entry.path())) {
                        iterator.disable_recursion_pending();
                    } else if (contains_target(entry.path())) {
                        appendUnique(result, pathToUtf8(entry.path()));
                    }
                }
                iterator.increment(error);
            }
        }
    }
    const auto defaults = current_skin->tableDirs();
    for (const auto& directory : defaults) appendUnique(result, directory);
    if (!summary_ || !summary_->legacy()) {
        const auto engine_directories = current_skin->wsnTableDirs(
            summary_ ? summary_->dataVersion() : versioninfo::LATEST_VERSION);
        // D版と同様、種類別の hasWsnTarg 判定で対象画像
        // が見つかった場合だけ、Engine の選択肢を表示する。
        const bool has_engine_target = std::any_of(
            engine_directories.begin(), engine_directories.end(),
            [this, &current_skin](const String& directory) {
                return cardKind()
                    ? current_skin->hasCardImage(directory, true,
                                                 no_card_size_image_, false)
                    : current_skin->hasBgImage(directory, true,
                                               excludeCardSizeImage());
            });
        if (has_engine_target) {
            for (const auto& directory : engine_directories)
                appendUnique(result, directory);
        }
    }
    return result;
}

std::vector<ImageSelectBase::DirectoryChoice> ImageSelectBase::directoryChoices() const {
    std::vector<DirectoryChoice> result;
    const auto skin = summSkin();
    if (!skin) return result;
    const auto available = directoryPaths();
    const auto defaults = skin->tableDirs();
    const auto engine = (!summary_ || !summary_->legacy())
        ? skin->wsnTableDirs(summary_ ? summary_->dataVersion()
                                     : versioninfo::LATEST_VERSION)
        : std::vector<String>{};
    const auto present = [&available](const String& value) {
        return std::any_of(available.begin(), available.end(),
            [&](const String& candidate) { return sameString(candidate, value); });
    };
    std::vector<String> default_sources;
    for (const auto& value : defaults) if (present(value)) default_sources.push_back(value);
    if (!default_sources.empty()) {
        result.push_back({prop_->requiredMsgs().defaultSelection(prop_->requiredMsgs().pathDef.value),
                          String(skinDirectoryKey), std::move(default_sources),
                          DirectoryChoice::Kind::Skin});
    }
    std::vector<String> engine_sources;
    for (const auto& value : engine) if (present(value)) engine_sources.push_back(value);
    if (!engine_sources.empty()) {
        const auto version = summary_ ? summary_->dataVersion() : versioninfo::LATEST_VERSION;
        auto position = std::find(versioninfo::VERSIONS.begin(), versioninfo::VERSIONS.end(), version);
        if (position == versioninfo::VERSIONS.end())
            position = std::find(versioninfo::VERSIONS.begin(), versioninfo::VERSIONS.end(),
                                 versioninfo::LATEST_VERSION);
        const auto index = position == versioninfo::VERSIONS.end() ? 0U
            : static_cast<std::size_t>(position - versioninfo::VERSIONS.begin());
        result.push_back({prop_->requiredMsgs().defaultSelection(utils::tryFormat(
                              prop_->requiredMsgs().pathWsnBasic.value,
                              versioninfo::VERSION_NAMES[index])),
                          String(engineDirectoryKey), std::move(engine_sources),
                          DirectoryChoice::Kind::Engine});
    }
    if (summary_ && !summary_->scenarioPath().empty()) {
        const auto root = structs::pathFromUtf8(summary_->scenarioPath()).lexically_normal();
        for (const auto& value : available) {
            if (std::any_of(defaults.begin(), defaults.end(), [&](const String& d) { return sameString(d, value); }) ||
                std::any_of(engine.begin(), engine.end(), [&](const String& d) { return sameString(d, value); }))
                continue;
            const auto path = structs::pathFromUtf8(value).lexically_normal();
            const auto relative = path.lexically_relative(root);
            if (relative.empty() || relative.native().find(std::filesystem::path("..").native()) == 0) continue;
            String label = relative == std::filesystem::path(".") ? String{"/"}
                                                                   : structs::pathToUtf8(relative);
            std::replace(label.begin(), label.end(), '\\', '/');
            result.push_back({std::move(label), value, {value}, DirectoryChoice::Kind::Scenario});
        }
    }
    return result;
}

std::vector<String> ImageSelectBase::pathsForDirectory(const StringView directory) const {
    std::vector<String> result;
    const auto lifetime = callback_lifetime_;
    const auto revision = lifetime->list_revision;
    const auto scaled_provider = load_scaled_image_;
    const bool standard_material = !summary_ || summary_->scenarioPath().empty() ||
        !utils::isSubDirectory(directory, StringView(summary_->scenarioPath()));
    const bool default_scaled = summary_ && summary_->loadScaledImage();
    const bool use_provider = summary_ && !summary_->legacy() && bool(scaled_provider);
    const bool exclude_scaled = standard_material || (use_provider ? scaled_provider() : default_scaled);
    if (lifetime->owner != this || lifetime->list_revision != revision) return {};
    const auto current_skin = summSkin();
    std::vector<String> sources;
    if (directory == skinDirectoryKey || directory == engineDirectoryKey) {
        const auto kind = directory == skinDirectoryKey ? DirectoryChoice::Kind::Skin
                                                        : DirectoryChoice::Kind::Engine;
        // refreshListImpl は、新しい選択肢を準備してから反映する。
        // 公開済みの選択肢キャッシュは、まだ以前のスキンを参照している場合がある。
        if (current_skin) sources = kind == DirectoryChoice::Kind::Skin
            ? current_skin->tableDirs()
            : current_skin->wsnTableDirs(summary_ ? summary_->dataVersion()
                                                  : versioninfo::LATEST_VERSION);
    } else if (!directory.empty()) {
        sources.emplace_back(directory);
    }
    if (current_skin && !sources.empty()) {
        const bool logical_sort = prop_->var().logicalSort.value;
        for (const auto& source : sources) {
            auto values = cardKind()
                ? current_skin->cards(source, logical_sort, true, no_card_size_image_,
                                      can_include_ && !is_menu_card_)
                : current_skin->tables(source, logical_sort, true, excludeCardSizeImage());
            result.insert(result.end(), std::make_move_iterator(values.begin()),
                          std::make_move_iterator(values.end()));
        }
        const auto less = [logical_sort](const String& left, const String& right) {
            return (logical_sort ? utils::fnncmp(left, right) : utils::fncmp(left, right)) < 0;
        };
        std::stable_sort(result.begin(), result.end(), less);
        result.erase(std::unique(result.begin(), result.end()), result.end());
    }
    if (exclude_scaled) {
        std::erase_if(result, [](const String& value) {
            const auto path = structs::pathFromUtf8(value);
            return !utils::noScaledPath(path).empty();
        });
    }
    // スキン・エンジンの素材パスはファイル名だけで保存し、シナリオの素材は シ
    // ナリオルートからの相対パスで保存する（例: Material\d_rider_hyaku.bmp
    // ）。 Skin::cards/tables はファイル名だ
    // けを返すため、シナリオの項目には相対ディレクトリを補う。
    if (summary_ && !summary_->scenarioPath().empty() && !directory.empty() &&
        directory != skinDirectoryKey && directory != engineDirectoryKey) {
        const auto scenario_root = structs::pathFromUtf8(summary_->scenarioPath()).lexically_normal();
        const auto selected_root = structs::pathFromUtf8(directory).lexically_normal();
        const auto relative = selected_root.lexically_relative(scenario_root);
        if (!relative.empty() && relative.native() != std::filesystem::path(".").native() &&
            relative.native().find(std::filesystem::path("..").native()) != 0) {
            for (auto& path : result) {
                path = pathToUtf8(relative / structs::pathFromUtf8(path));
            }
        }
    }
    can_inc_search_ = can_inc_search_ || !result.empty();
    if (search_popup_) {
        const auto matcher = search_popup_->textMatcher();
        std::erase_if(result, [&](const String& value) { return !matcher(fileLabel(value)); });
    }
    return result;
}

bool ImageSelectBase::exportBinary(const StringView source, String& result) const {
    if (!summary_ || source.empty() || !isBinImg(source)) return false;
    const auto current_skin = summSkin();
    const auto added = current_skin ? current_skin->materialPath() : String("Material");
    // D版と同様、copyTo のファイル操作や復号の失敗は呼び出し元へ伝え
    // る。 materialPath の明示的なキャンセル結果とは区別する。
    const auto exported = utils::copyTo(
        structs::pathFromUtf8(summary_->scenarioPath()), utils::Path(String(source)),
        structs::pathFromUtf8(added), true, save_name_ ? save_name_() : String{});
    if (exported.empty()) return false;
    result = pathToUtf8(exported);
    std::replace(result.begin(), result.end(), '\\', '/');
    return true;
}

lib::MessageResult ImageSelectBase::askExternalize() const {
    if (!summary_) return lib::MessageResult::No;
    if (!services_.dialogs) return lib::MessageResult::No;
    lib::MessageDialogRequest request;
    request.title = prop_->requiredMsgs().dlgTitQuestion.value;
    request.message = prop_->requiredMsgs().dlgMsgExcludeImage.value;
    request.severity = lib::MessageSeverity::Question;
    request.buttons = lib::MessageButtons::YesNoCancel;
    return services_.dialogs->message(request);
}

}  // namespace cwxeditor::cwx::editor::gui::dwt::imageselect
