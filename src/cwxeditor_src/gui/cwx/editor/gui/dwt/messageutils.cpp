#include "messageutils.hpp"
// UIの実装は一つに統一し、
// 別経路の実装を設けない。
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/centerlayout.hpp"
#include "cwxeditor_src/core/cwx/structs.hpp"
#include "cwxeditor_src/core/cwx/sjis.hpp"
#include "cwxeditor_src/core/cwx/utils.hpp"
#include "cwxeditor_src/core/cwx/versioninfo.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/commons.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dmenu.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dskin.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dutils.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/imagelistwindow.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/incsearch.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/xmlbytestransfer.hpp"
#include "cwxeditor_src/lib/graphics.hpp"
#include "cwxeditor_src/lib/ui_actions.hpp"
#include "cwxeditor_src/lib/ui_window_lifecycle.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <limits>
#include <sstream>
#include <type_traits>

namespace cwxeditor::cwx::editor::gui::dwt::messageutils {

namespace {

// ALLの変数参照コン
// ボの標準幅は160px
// 。セリフは指定幅、画
// 像フォントはさらに
// 40px狭くする。
// 単体版は従来の伸縮
// する幅を維持する。
constexpr float kVariableComboWidth = 160.0F;

class SkinSCharToolBar final : public lib::ToolBar {
public:
    commons::Commons* common{};
    std::uint64_t skin_connection{}, scale_connection{};
    std::vector<std::unique_ptr<lib::ImageResourceSlot>> images;
    // 1より大きい場合、ボタンをこの行数に折り返す。幅は個数に応じて決まる。
    std::size_t rows{1U};
    ~SkinSCharToolBar() override {
        if (!common) return;
        common->refSkin.remove(skin_connection);
        common->refImageScale.remove(scale_connection);
    }
};

class ReferenceInsertPanel final : public lib::Panel {
public:
    commons::Commons* common{};
    std::weak_ptr<const int> common_lifetime;
    void bind(commons::Commons* value) {
        common = value;
        common_lifetime = value ? value->lifetimeToken() : std::weak_ptr<const int>{};
    }
    std::uint64_t variables{}, deleted_variables{}, uses{}, paths{}, path_tree{},
        deleted_paths{}, replaced_path{}, ignored_paths{}, skin{}, version{}, gallery_scale{};
    std::vector<lib::Action> refreshers;

    void refresh() {
        if (common && common_lifetime.expired()) return;
        const auto callbacks = refreshers;
        for (const auto& callback : callbacks) if (callback) callback();
    }
    ~ReferenceInsertPanel() override {
        if (!common || common_lifetime.expired()) return;
        common->refFlagAndStep.remove(variables);
        common->delFlagAndStep.remove(deleted_variables);
        common->refUseCount.remove(uses);
        common->refPath.remove(paths);
        common->refPaths.remove(path_tree);
        common->delPaths.remove(deleted_paths);
        common->replPath.remove(replaced_path);
        common->refIgnorePaths.remove(ignored_paths);
        common->refSkin.remove(skin);
        common->refDataVersion.remove(version);
        common->refImageScale.remove(gallery_scale);
    }
};

void refreshReferenceContextMenu(
    const std::weak_ptr<lib::Menu>& weak_menu,
    const std::weak_ptr<lib::ComboBox>& weak_combo,
    const std::weak_ptr<ReferenceInsertPanel>& weak_panel,
    const Props* prop, const types::MenuID open_menu, String fallback)
{
    const auto panel = weak_panel.lock();
    const auto menu = weak_menu.lock();
    const auto combo = weak_combo.lock();
    if (!panel || !menu || !combo ||
        (panel->common && panel->common_lifetime.expired()) ||
        menu->items.size() < 3U) return;
    auto items = menu->items;
    const auto label = [prop](const types::MenuID id, String value) {
        return prop ? const_cast<Props*>(prop)->buildMenu(id) : std::move(value);
    };
    const auto icon = [prop](const types::MenuID id) {
        return prop ? const_cast<Props*>(prop)->images().menu(id)
                    : lib::ResourceId{};
    };
    items[0].text = label(types::MenuID::IncSearch, "絞り込み検索");
    items[0].icon = icon(types::MenuID::IncSearch);
    items[0].enabled = combo->enabled();
    items[2].text = label(open_menu, std::move(fallback));
    items[2].icon = icon(open_menu);
    items[2].enabled = combo->selected_index.has_value() &&
                       *combo->selected_index < combo->items.size();
    (void)lib::replace_menu_items(menu, std::move(items));
}

[[nodiscard]] std::uint32_t acceleratorCharacter(const lib::KeyEvent& event)
{
    if (!event.printable.empty())
        return static_cast<unsigned char>(event.printable.front());
    if (event.key >= lib::Key::A && event.key <= lib::Key::Z)
        return static_cast<std::uint32_t>(
            'A' + static_cast<int>(event.key) - static_cast<int>(lib::Key::A));
    if (event.key >= lib::Key::Digit0 && event.key <= lib::Key::Digit9)
        return static_cast<std::uint32_t>(
            '0' + static_cast<int>(event.key) - static_cast<int>(lib::Key::Digit0));
    return 0U;
}

[[nodiscard]] String lowerAscii(String value)
{
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

[[nodiscard]] String normalizeMessage(String value)
{
    String result;
    result.reserve(value.size());
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '\r' && i + 1 < value.size() && value[i + 1] == '\n') {
            ++i;
            result.push_back('\n');
        } else {
            result.push_back(value[i]);
        }
    }
    // 末尾のLFは、Enterで新しく作った空行を表す。 こ
    // の正規化は編集のたびに走るため、末尾のLFを削ると、メ
    // ッセージ・セリフ欄の末尾でEnterが効かなくなる。
    return result;
}

[[nodiscard]] String normalizeSpeakDialogText(String value)
{
    return ::cwxeditor::cwx::utils::lastRet(normalizeMessage(std::move(value)));
}

[[nodiscard]] String firstLine(StringView value)
{
    const auto position = value.find('\n');
    return String(value.substr(0, position));
}

[[nodiscard]] std::filesystem::path utf8Path(StringView value)
{
    if (value.empty()) return std::filesystem::path{};
    return structs::pathFromUtf8(value);
}

[[nodiscard]] lib::Color rgbColor(const structs::CRGB& value)
{
    return lib::Color{
        static_cast<std::uint8_t>(std::min<std::uint32_t>(value.r, 255)),
        static_cast<std::uint8_t>(std::min<std::uint32_t>(value.g, 255)),
        static_cast<std::uint8_t>(std::min<std::uint32_t>(value.b, 255)),
        static_cast<std::uint8_t>(std::min<std::uint32_t>(value.a, 255))};
}

[[nodiscard]] lib::FontSpec fontSpec(const structs::CFont& value)
{
    lib::FontSpec result;
    result.family = value.name;
    result.points = static_cast<float>(value.point);
    result.weight = value.bold ? 700 : 400;
    result.italic = value.italic;
    result.content_font = true;
    // D版のメッセージ編集は FixedWidthText で、フォント設定時に正の画素高さを指
    // 定する CreateFont 補助処理を呼ぶ。 ラスターのプレビューにも同じ処理を使う。
    result.size_mode = value.point > 0U ? lib::FontSizeMode::CellPixels
                                        : lib::FontSizeMode::DefaultHeight;
    return result;
}

void applyMessageColors(const MessageContext& context,
                        const std::shared_ptr<lib::TextEdit>& editor)
{
    if (!editor) return;
    if (!context.props ||
        !static_cast<bool>(
            context.props->var().useMessageWindowColorInTextContentDialog)) {
        editor->set_background(std::nullopt);
        editor->set_foreground(std::nullopt);
        return;
    }
    const auto clamp = [](const std::int32_t value) {
        return static_cast<std::uint8_t>(std::clamp(value, 0, 255));
    };
    const auto& values = context.props->var();
    editor->set_background(lib::Color{
        clamp(values.msgBackR.value), clamp(values.msgBackG.value),
        clamp(values.msgBackB.value), 255U});
    editor->set_foreground(lib::Color{
        clamp(values.msgForeR.value), clamp(values.msgForeG.value),
        clamp(values.msgForeB.value), 255U});
}

void applyMessageEditorStyle(const MessageContext& context,
                             const Skin::Ptr& active_skin,
                             const std::shared_ptr<lib::TextEdit>& editor,
                             const bool image = true)
{
    if (!editor || !context.props) return;
    const auto font = context.props->adjustFont(
        context.props->looks().messageFont(active_skin && active_skin->legacy()));
    editor->set_font(fontSpec(font));
    // D版の FixedWidthText と同様、SWT.MULTI | SWT.WRAP
    // を使い、SWT.V_SCROLL は付けない。 スクロールバー用の幅を確保す
    // ると折り返し幅が狭まり、44桁を埋めた行の最後の字が折り返され、バーが現れる。
    if (editor->multiline) editor->vertical_scroll = false;
    editor->minimum_visible_lines = editor->multiline
        ? static_cast<std::size_t>(std::max(1, context.props->looks().messageLine())) : 1U;
    editor->minimum_visible_columns = static_cast<std::size_t>(std::max(1,
        image ? context.props->looks().messageImageLen() : context.props->looks().messageLen()));
    // 意図的な変更(D版と異なる): D はネイティブ Text のピクセル幅で折り返すが、
    // 代替フォント(特に Web)では全角/半角の比率が崩れ、プレビューと改行位置が
    // ずれる。プレビュー(renderMessagePreview)と同じ「半角1・全角2」の桁数で
    // 折り返す。
    editor->wrap_columns = editor->multiline ? editor->minimum_visible_columns : 0U;
    applyMessageColors(context, editor);
}

[[nodiscard]] std::uint32_t scaleOrOne(const Props& prop)
{
    return std::max<std::uint32_t>(1, prop.drawingScale());
}

template <typename WindowProperties>
[[nodiscard]] WindowGeometry readWindowGeometry(const WindowProperties& value)
{
    return WindowGeometry{value.x(), value.y(), value.width(), value.height(),
                          value.maximized()};
}

template <typename WindowProperties>
void writeWindowGeometry(WindowProperties& value, const WindowGeometry& geometry)
{
    value.x(geometry.x);
    value.y(geometry.y);
    if (geometry.width > 0) value.width(geometry.width);
    if (geometry.height > 0) value.height(geometry.height);
    value.maximized(geometry.maximized);
}

[[nodiscard]] WindowGeometry editorGeometry(const Props& prop,
                                             const types::CType type)
{
    if (type == types::CType::TalkDialog)
        return readWindowGeometry(prop.flex().speakDlg);
    return readWindowGeometry(prop.flex().msgDlg);
}

void storeEditorGeometry(Props& prop, const types::CType type,
                         const WindowGeometry& geometry)
{
    if (type == types::CType::TalkDialog)
        writeWindowGeometry(prop.flex().speakDlg, geometry);
    else
        writeWindowGeometry(prop.flex().msgDlg, geometry);
}

[[nodiscard]] WindowGeometry floatingPreviewGeometry(
    const Props& prop, const types::CType type)
{
    if (type == types::CType::TalkDialog)
        return readWindowGeometry(prop.flex().dlgPrev);
    return readWindowGeometry(prop.flex().msgPrev);
}

void storeFloatingPreviewGeometry(Props& prop, const types::CType type,
                                  const WindowGeometry& geometry)
{
    if (type == types::CType::TalkDialog)
        writeWindowGeometry(prop.flex().dlgPrev, geometry);
    else
        writeWindowGeometry(prop.flex().msgPrev, geometry);
}

[[nodiscard]] String numberText(std::uint32_t value)
{
    return std::to_string(value);
}

[[nodiscard]] String charText(char32_t value)
{
    if (value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) return {};
    String result;
    if (value <= 0x7f) {
        result.push_back(static_cast<char>(value));
    } else if (value <= 0x7ff) {
        result.push_back(static_cast<char>(0xc0 | (value >> 6)));
        result.push_back(static_cast<char>(0x80 | (value & 0x3f)));
    } else if (value <= 0xffff) {
        result.push_back(static_cast<char>(0xe0 | (value >> 12)));
        result.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3f)));
        result.push_back(static_cast<char>(0x80 | (value & 0x3f)));
    } else {
        result.push_back(static_cast<char>(0xf0 | (value >> 18)));
        result.push_back(static_cast<char>(0x80 | ((value >> 12) & 0x3f)));
        result.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3f)));
        result.push_back(static_cast<char>(0x80 | (value & 0x3f)));
    }
    return result;
}

[[nodiscard]] msgutils::DString decodeUtf8(StringView value)
{
    msgutils::DString result;
    for (std::size_t index = 0; index < value.size();) {
        const auto first = static_cast<unsigned char>(value[index]);
        char32_t codepoint = first;
        std::size_t length = 1U;
        if ((first & 0xe0U) == 0xc0U) {
            codepoint = first & 0x1fU;
            length = 2U;
        } else if ((first & 0xf0U) == 0xe0U) {
            codepoint = first & 0x0fU;
            length = 3U;
        } else if ((first & 0xf8U) == 0xf0U) {
            codepoint = first & 0x07U;
            length = 4U;
        }
        if (index + length > value.size()) length = 1U;
        for (std::size_t offset = 1U; offset < length; ++offset) {
            const auto next = static_cast<unsigned char>(value[index + offset]);
            if ((next & 0xc0U) != 0x80U) {
                length = 1U;
                codepoint = first;
                break;
            }
            codepoint = (codepoint << 6U) | (next & 0x3fU);
        }
        result.push_back(codepoint);
        index += length;
    }
    return result;
}

[[nodiscard]] String encodeUtf8(std::u32string_view value)
{
    String result;
    for (const auto codepoint : value) result += charText(codepoint);
    return result;
}

// D版の SpeakDialog と同様、表示用の一行化は splitLines で分割し、各行のUnicode
// 空白を除いてから連結する。 ASCIIだけの処理ではU+3000やNBSPが見出しの端に残るため、この表
// 示処理で対応する。 ALL と保存するテキストは変更しない。 splitLines の区切り文字: https://dlang.org/library/std/string/split_lines.html
//
[[nodiscard]] String standaloneDialogCaption(StringView value)
{
    const auto chars = decodeUtf8(value);
    const auto white = [](const char32_t c) {
        return (c >= 0x09 && c <= 0x0d) || c == 0x20 || c == 0x85 ||
            c == 0xa0 || c == 0x1680 || (c >= 0x2000 && c <= 0x200a) ||
            c == 0x2028 || c == 0x2029 || c == 0x202f || c == 0x205f || c == 0x3000;
    };
    const auto lineBreak = [](const char32_t c) {
        return c == U'\r' || c == U'\n' || c == U'\v' || c == U'\f' ||
            c == 0x85 || c == 0x2028 || c == 0x2029;
    };
    String result;
    std::size_t start = 0;
    for (std::size_t end = 0; end <= chars.size(); ++end) {
        if (end != chars.size() && !lineBreak(chars[end])) continue;
        auto first = start;
        auto last = end;
        while (first < last && white(chars[first])) ++first;
        while (last > first && white(chars[last - 1U])) --last;
        result += encodeUtf8(std::u32string_view(chars).substr(first, last - first));
        start = end + 1U;
    }
    return result;
}

[[nodiscard]] std::size_t byteToCodepointIndex(StringView value,
                                                std::size_t byte_offset)
{
    byte_offset = std::min(byte_offset, value.size());
    return decodeUtf8(value.substr(0, byte_offset)).size();
}

void setPixel(lib::ImageFrame& frame, std::uint32_t x, std::uint32_t y,
              const lib::Color& color)
{
    if (x >= frame.pixel_width || y >= frame.pixel_height) return;
    const auto index =
        (static_cast<std::size_t>(y) * frame.pixel_width + x) * 4;
    if (index + 3 >= frame.pixels.size()) return;
    frame.pixels[index + 0] = static_cast<std::byte>(color.red);
    frame.pixels[index + 1] = static_cast<std::byte>(color.green);
    frame.pixels[index + 2] = static_cast<std::byte>(color.blue);
    frame.pixels[index + 3] = static_cast<std::byte>(color.alpha);
}

[[nodiscard]] lib::Color imagePixel(const lib::ImageFrame& frame,
                                    const std::uint32_t x,
                                    const std::uint32_t y)
{
    if (x >= frame.pixel_width || y >= frame.pixel_height) return {};
    const std::size_t channels = frame.pixel_format == lib::PixelFormat::Gray8 ? 1U
        : (frame.pixel_format == lib::PixelFormat::RGB24 ||
           frame.pixel_format == lib::PixelFormat::BGR24) ? 3U : 4U;
    const auto index = (static_cast<std::size_t>(y) * frame.pixel_width + x) * channels;
    if (index + channels > frame.pixels.size()) return {};
    const auto byte = [&frame](const std::size_t value) {
        return std::to_integer<std::uint8_t>(frame.pixels[value]);
    };
    if (frame.pixel_format == lib::PixelFormat::Gray8) {
        const auto value = byte(index);
        return {value, value, value, 255U};
    }
    if (frame.pixel_format == lib::PixelFormat::BGR24)
        return {byte(index + 2U), byte(index + 1U), byte(index), 255U};
    if (frame.pixel_format == lib::PixelFormat::BGRA32)
        return {byte(index + 2U), byte(index + 1U), byte(index), byte(index + 3U)};
    return {byte(index), byte(index + 1U), byte(index + 2U),
            channels == 4U ? byte(index + 3U) : static_cast<std::uint8_t>(255U)};
}

void blitPreviewImage(const ImageDataWithScale& source, lib::ImageFrame& target,
                      const std::int32_t left, const std::int32_t top,
                      const std::int32_t width, const std::int32_t height,
                      const std::optional<lib::Color> tint = {})
{
    const auto* data = source.data();
    if (!data || data->frames.empty() || width <= 0 || height <= 0) return;
    const auto& input = data->frames.front();
    if (input.pixel_width == 0U || input.pixel_height == 0U) return;
    const auto y_begin=std::max<std::int64_t>(0,-std::int64_t(top));
    const auto y_end=std::min<std::int64_t>(height,std::int64_t(target.pixel_height)-top);
    const auto x_begin=std::max<std::int64_t>(0,-std::int64_t(left));
    const auto x_end=std::min<std::int64_t>(width,std::int64_t(target.pixel_width)-left);
    for (auto y = y_begin; y < y_end; ++y) {
        const auto dy = top + y;
        if (dy < 0 || dy >= static_cast<std::int32_t>(target.pixel_height)) continue;
        const auto sy = static_cast<std::uint32_t>(
            static_cast<std::uint64_t>(y) * input.pixel_height /
            static_cast<std::uint32_t>(height));
        for (auto x = x_begin; x < x_end; ++x) {
            const auto dx = left + x;
            if (dx < 0 || dx >= static_cast<std::int32_t>(target.pixel_width)) continue;
            const auto sx = static_cast<std::uint32_t>(
                static_cast<std::uint64_t>(x) * input.pixel_width /
                static_cast<std::uint32_t>(width));
            auto foreground = imagePixel(input, sx, sy);
            if (foreground.alpha == 0U) continue;
            if (tint) {
                foreground.red = tint->red;
                foreground.green = tint->green;
                foreground.blue = tint->blue;
                foreground.alpha = static_cast<std::uint8_t>(
                    static_cast<std::uint32_t>(foreground.alpha) * tint->alpha / 255U);
            }
            const auto background = imagePixel(target, static_cast<std::uint32_t>(dx),
                                                static_cast<std::uint32_t>(dy));
            const auto alpha = static_cast<std::uint32_t>(foreground.alpha);
            const auto blend = [alpha](const std::uint8_t front,
                                       const std::uint8_t back) {
                return static_cast<std::uint8_t>((front * alpha +
                                                  back * (255U - alpha) + 127U) /
                                                 255U);
            };
            setPixel(target, static_cast<std::uint32_t>(dx),
                     static_cast<std::uint32_t>(dy),
                     {blend(foreground.red, background.red),
                      blend(foreground.green, background.green),
                      blend(foreground.blue, background.blue), 255U});
        }
    }
}

void fillRect(lib::ImageFrame& frame, std::int32_t left, std::int32_t top,
              std::int32_t width, std::int32_t height, const lib::Color& color)
{
    if (width <= 0 || height <= 0) return;
    const auto right = std::min<std::int32_t>(
        static_cast<std::int32_t>(frame.pixel_width), left + width);
    const auto bottom = std::min<std::int32_t>(
        static_cast<std::int32_t>(frame.pixel_height), top + height);
    for (auto y = std::max<std::int32_t>(0, top); y < bottom; ++y) {
        for (auto x = std::max<std::int32_t>(0, left); x < right; ++x) {
            setPixel(frame, static_cast<std::uint32_t>(x),
                     static_cast<std::uint32_t>(y), color);
        }
    }
}

void strokeRect(lib::ImageFrame& frame, std::int32_t left, std::int32_t top,
                std::int32_t width, std::int32_t height, const lib::Color& color)
{
    if (width <= 0 || height <= 0) return;
    fillRect(frame, left, top, width, 1, color);
    fillRect(frame, left, top + height - 1, width, 1, color);
    fillRect(frame, left, top, 1, height, color);
    fillRect(frame, left + width - 1, top, 1, height, color);
}

[[nodiscard]] lib::ImageData makeRaster(std::uint32_t width,
                                         std::uint32_t height,
                                         const lib::Color& background)
{
    lib::ImageData image;
    image.format = lib::ImageFormat::Png;
    lib::ImageFrame frame;
    frame.logical_size = lib::Size{static_cast<float>(width),
                                   static_cast<float>(height)};
    frame.pixel_width = width;
    frame.pixel_height = height;
    frame.pixel_format = lib::PixelFormat::RGBA32;
    frame.pixels.resize(static_cast<std::size_t>(width) * height * 4);
    fillRect(frame, 0, 0, static_cast<std::int32_t>(width),
             static_cast<std::int32_t>(height), background);
    image.frames.push_back(std::move(frame));
    return image;
}

[[nodiscard]] ImageDataWithScale decodeImage(const MessageContext& context,
                                             StringView path,
                                             std::uint32_t scale)
{
    if (path.empty() || !context.props) return {};
    Skin::Ptr active_skin = context.forced_skin;
    if (!active_skin && context.summary && !context.summary->type().empty())
        active_skin = dskin::findSkin2(*context.props, context.summary->type(),
                                       context.summary->skinName());
    if (!active_skin)
        active_skin = dskin::findSkin2(*context.props,
                                       context.props->var().defaultSkin.value,
                                       context.props->var().defaultSkinName.value);
    String resolved(path);
    if (::cwxeditor::cwx::utils::isBinImg(path)) {
        if (!context.services.image_codecs) return {};
        auto bytes = ::cwxeditor::cwx::utils::strToBImg(path);
        lib::DecodeImageRequest request;
        request.encoded_data = std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(bytes.data()), bytes.size());
        std::string error;
        auto image = context.services.image_codecs->decode(request, error);
        return image.frames.empty()
                   ? ImageDataWithScale{}
                   : ImageDataWithScale(std::move(image), scale,
                                        context.services.graphics);
    }
    if (active_skin) {
        const auto candidate = active_skin->findImagePath(
            path, context.summary ? context.summary->scenarioPath() : String{},
            context.summary ? context.summary->dataVersion() : String("1.50"));
        if (!candidate.empty()) resolved = candidate;
    }
    auto loaded = dutils::loadImageWithScale(
        context.props, active_skin.get(), context.summary, resolved, scale,
        true, false, 0, 0, {}, context.services);
    auto image = loaded.baseData();
    if (image.frames.empty()) return {};
    return ImageDataWithScale(std::move(image), std::max(1U, loaded.scale()),
                              context.services.graphics);
}

[[nodiscard]] ImageDataWithScale decodeEditorResource(
    const MessageContext& context, StringView filename, std::uint32_t scale)
{
    if (!context.props || !context.services.image_codecs || filename.empty())
        return {};
    const auto path = utf8Path(context.props->images().appPath()).parent_path() /
                      utf8Path("resource") / utf8Path(filename);
    lib::DecodeImageRequest request;
    request.path = path;
    std::string error;
    auto image = context.services.image_codecs->decode(request, error);
    if (image.frames.empty() && context.services.system) {
        try {
            const auto executable = utf8Path(context.props->images().appPath());
            const auto resource_name = String("resource/") + String(filename);
            const auto bytes = context.services.system->executable_resource(
                executable, "RCDATA", resource_name, error);
            if (!bytes.empty()) {
                request.path.clear();
                request.encoded_data = bytes;
                image = context.services.image_codecs->decode(request, error);
            }
        } catch (...) {
            // 実行ファイル内のリソース格納機能がないプラットフォームで
            // は、 エディタのプレビュー用リソースを省略可能にする。
        }
    }
    if (image.frames.empty()) return {};

    // エディタのリソースは、dprops::Images およびD版
    // ・SWTと同様、左上の画素を透明色にする。 プレビューには元の
    // 画素が必要なので、dprops::Images が作ったネイテ
    // ィブの ResourceId だけを再利用することはできない。
    for (auto& frame : image.frames) {
        const std::size_t width = frame.pixel_width;
        const std::size_t height = frame.pixel_height;
        const std::size_t source_bpp =
            frame.pixel_format == lib::PixelFormat::Gray8 ? 1U :
            frame.pixel_format == lib::PixelFormat::RGB24 ||
                    frame.pixel_format == lib::PixelFormat::BGR24 ? 3U :
            frame.pixel_format == lib::PixelFormat::RGBA32 ||
                    frame.pixel_format == lib::PixelFormat::BGRA32 ? 4U : 0U;
        if (width == 0U || height == 0U || source_bpp == 0U ||
            frame.pixels.size() < width * height * source_bpp) continue;
        const auto* source =
            reinterpret_cast<const std::uint8_t*>(frame.pixels.data());
        const auto read = [&](std::size_t offset, std::uint8_t& red,
                              std::uint8_t& green, std::uint8_t& blue,
                              std::uint8_t& alpha) {
            alpha = 255U;
            switch (frame.pixel_format) {
            case lib::PixelFormat::Gray8:
                red = green = blue = source[offset]; break;
            case lib::PixelFormat::RGB24:
                red = source[offset]; green = source[offset + 1U];
                blue = source[offset + 2U]; break;
            case lib::PixelFormat::BGR24:
                blue = source[offset]; green = source[offset + 1U];
                red = source[offset + 2U]; break;
            case lib::PixelFormat::RGBA32:
                red = source[offset]; green = source[offset + 1U];
                blue = source[offset + 2U]; alpha = source[offset + 3U]; break;
            case lib::PixelFormat::BGRA32:
                blue = source[offset]; green = source[offset + 1U];
                red = source[offset + 2U]; alpha = source[offset + 3U]; break;
            default:
                red = green = blue = alpha = 0U; break;
            }
        };
        std::uint8_t key_red{}, key_green{}, key_blue{}, ignored{};
        read(0U, key_red, key_green, key_blue, ignored);
        std::vector<std::byte> rgba(width * height * 4U);
        for (std::size_t pixel = 0; pixel < width * height; ++pixel) {
            std::uint8_t red{}, green{}, blue{}, alpha{};
            read(pixel * source_bpp, red, green, blue, alpha);
            if (red == key_red && green == key_green && blue == key_blue)
                alpha = 0U;
            const auto output = pixel * 4U;
            rgba[output] = static_cast<std::byte>(red);
            rgba[output + 1U] = static_cast<std::byte>(green);
            rgba[output + 2U] = static_cast<std::byte>(blue);
            rgba[output + 3U] = static_cast<std::byte>(alpha);
        }
        frame.pixel_format = lib::PixelFormat::RGBA32;
        frame.pixels = std::move(rgba);
    }
    return ImageDataWithScale(std::move(image), std::max(1U, scale),
                              context.services.graphics);
}

[[nodiscard]] String itemCommand(StringView prefix, char value)
{
    String result(prefix);
    result.push_back(value);
    return result;
}

[[nodiscard]] std::vector<SPChar> defaultSPChars(bool message,
                                                  bool legacy)
{
    if (message) return std::vector<SPChar>(SPCHAR_ALL.begin(), SPCHAR_ALL.end());
    if (legacy) {
        return std::vector<SPChar>(SPCHAR_TEXT_CLASSIC.begin(),
                                   SPCHAR_TEXT_CLASSIC.end());
    }
    return std::vector<SPChar>(SPCHAR_TEXT.begin(), SPCHAR_TEXT.end());
}

[[nodiscard]] String mapName(const NameMap& names, char value)
{
    const auto upper = static_cast<char>(std::toupper(static_cast<unsigned char>(value)));
    for (const auto& [name, text] : names) {
        if (std::toupper(static_cast<unsigned char>(name)) == upper) return text;
    }
    return String{};
}

[[nodiscard]] String replacementValue(const ValueMap& values, StringView key)
{
    const auto found = values.find(String(key));
    return found == values.end() || !found->second.exists ? String{} : found->second.value;
}

[[nodiscard]] VarValue replacement(const ValueMap& values, StringView key)
{
    const auto found = values.find(String(key));
    return found == values.end() ? VarValue{} : found->second;
}

[[nodiscard]] std::uint32_t nonzero(std::int32_t value)
{
    return value > 0 ? static_cast<std::uint32_t>(value) : 1U;
}

[[nodiscard]] lib::Array<String> nonEmptyLines(StringView value)
{
    lib::Array<String> result;
    std::size_t start{};
    while (start <= value.size()) {
        auto end = value.find_first_of("\r\n", start);
        if (end == StringView::npos) end = value.size();
        auto line = String(value.substr(start, end - start));
        if (!line.empty()) result.push_back(std::move(line));
        if (end == value.size()) break;
        start = end + 1;
        if (value[end] == '\r' && start < value.size() && value[start] == '\n') ++start;
    }
    return result;
}

// SpeakDialog の必要称号の行は、D版のUnicode対応の splitLines
// の規則を使う。 値付き称号の別の代替処理と ALL の動作は変えない。
[[nodiscard]] lib::Array<String> requiredCouponLines(StringView value)
{
    if (lib::editor_window_host_options().all_mode) return nonEmptyLines(value);
    auto lines = ::cwxeditor::cwx::utils::splitLines(value);
    lines.erase(std::remove_if(lines.begin(), lines.end(),
                              [](const String& line) { return line.empty(); }),
                lines.end());
    return lines;
}

[[nodiscard]] CardImagePtr decodeStoredCardImage(StringView value)
{
    // 元のメッセージコンテント形式では、明示的な空の path は現在の
    // 選択メンバー（#M）を表す。 画像なしは、カードパスの項目自体がな
    // いことで表す。 空の項目をファイルパスとして扱うと、ImageSelect
    // と MsgPreview の両方が空になってしまう。
    if (value.empty())
        return std::make_shared<CardImage>(types::Talker::Selected);
    constexpr StringView pcPrefix = "PC#";
    if (value.starts_with(pcPrefix)) {
        try {
            return std::make_shared<CardImage>(static_cast<std::uint32_t>(
                std::stoul(String(value.substr(pcPrefix.size())))));
        } catch (...) {
        }
    }
    for (const auto talker : {types::Talker::Selected, types::Talker::Unselected,
                              types::Talker::Random, types::Talker::Card,
                              types::Talker::Valued}) {
        const auto suffix = String("??") + types::fromTalker(talker);
        if (value.size() >= suffix.size() &&
            value.substr(value.size() - suffix.size()) == suffix) {
            return std::make_shared<CardImage>(talker);
        }
    }
    return std::make_shared<CardImage>(String(value));
}

[[nodiscard]] String encodeStoredCardImage(const CardImage& image)
{
    switch (image.type) {
    case types::CardImageType::File:
        return image.path();
    case types::CardImageType::PCNumber:
        return String("PC#") + std::to_string(image.pcNumber());
    case types::CardImageType::Talker:
        return String("Material/??") + types::fromTalker(image.talker());
    }
    return {};
}

}  // namespace

ImageDataWithScale::Resource::~Resource()
{
    if (graphics && id != 0) graphics->destroy_image(id);
}

ImageDataWithScale::ImageDataWithScale(
    lib::ImageData image, std::uint32_t drawing_scale,
    std::shared_ptr<lib::GraphicsBackend> backend)
    : value(std::make_shared<Resource>()),
      scale(std::max<std::uint32_t>(1, drawing_scale))
{
    value->data = std::move(image);
    value->graphics = std::move(backend);
    if (value->graphics && !value->data.frames.empty()) {
        value->id = value->graphics->create_image(value->data);
    }
}

bool ImageDataWithScale::valid() const noexcept
{
    return value && !value->data.frames.empty();
}

lib::ResourceId ImageDataWithScale::resource() const noexcept
{
    return value ? value->id : 0;
}

const lib::ImageData* ImageDataWithScale::data() const noexcept
{
    return value ? &value->data : nullptr;
}

lib::Size ImageDataWithScale::logicalSize() const noexcept
{
    if (!valid()) return lib::Size{};
    return value->data.frames.front().logical_size;
}

AbstractMessageDialog::AbstractMessageDialog(
    MessageContext context, types::CType type, const Content* parent,
    Content* event, WindowGeometry size)
    : context_(std::move(context)),
      parent_(parent),
      event_(event),
      type_(type),
      geometry_(size)
{
    if (context_.props && (geometry_.width <= 0 || geometry_.height <= 0))
        geometry_ = editorGeometry(*context_.props, type_);
    preview_host_window_ = context_.owner;
    if (context_.props)
        preview_scaled_message_width_ = static_cast<float>(
            context_.props->s(context_.props->looks().messageBounds().width));
    initializeFromEvent();
    if (context_.editor_commons)
        message_color_connection_ = context_.editor_commons->refUseMessageWindowColorInTextContentDialog.add(
            [this] { refSkin(); });
    if (context_.editor_commons)
        float_preview_connection_ = context_.editor_commons->refFloatMessagePreview.add(
            [this] { refFloatMessagePreview(); });
}

AbstractMessageDialog::~AbstractMessageDialog()
{
    preview_scale_lifetime_.reset();
    if (context_.editor_commons)
        context_.editor_commons->refUseMessageWindowColorInTextContentDialog.remove(message_color_connection_);
    if (context_.editor_commons)
        context_.editor_commons->refFloatMessagePreview.remove(float_preview_connection_);
    if (const auto host = preview_host_window_.lock(); host && context_.props) {
        const auto bounds = host->bounds();
        geometry_ = WindowGeometry{
            static_cast<std::int32_t>(std::lround(bounds.x)),
            static_cast<std::int32_t>(std::lround(bounds.y)),
            static_cast<std::int32_t>(std::lround(bounds.width)),
            static_cast<std::int32_t>(std::lround(bounds.height)),
            host->maximized};
        storeEditorGeometry(*context_.props, type_, geometry_);
    }
    if (preview_window_) {
        preview_window_->visibilityChanged = {};
        preview_window_->saveWin();
        preview_window_->dispose();
    }
}

void AbstractMessageDialog::createFloatingPreview()
{
    if (!context_.props) return;
    preview_window_ = std::make_shared<MsgPreviewWindow>(
        context_, floatingPreviewGeometry(*context_.props, type_));
    preview_window_->geometryChanged = [this](const WindowGeometry& geometry) {
        if (context_.props)
            storeFloatingPreviewGeometry(*context_.props, type_, geometry);
    };
    preview_window_->visibilityChanged = [this](const bool visible) {
        if (preview_toggle_) preview_toggle_->checked = visible;
        if (!context_.props) return;
        if (type_ == types::CType::TalkMessage)
            context_.props->var().showMessagePreview = visible;
        else if (type_ == types::CType::TalkDialog)
            context_.props->var().showDialogPreview = visible;
    };
    if (const auto host = preview_host_window_.lock();
        host && preview_window_->window())
        lib::configure_owned_window(preview_window_->window(), host,
            lib::OwnedWindowKind::Modeless, lib::OwnedWindowCenter::Never);
}

void AbstractMessageDialog::createEmbeddedPreview()
{
    if (!context_.props) return;
    preview_ = std::make_shared<MsgPreview>(context_);
    preview_image_view_ = std::make_shared<lib::ImageView>();
    preview_image_view_->scale_mode = lib::ImageScaleMode::Center;
    preview_image_view_->interpolation = lib::Interpolation::Best;
    preview_image_view_->set_tooltip("メッセージのプレビュー");
    preview_values_view_ = preview_->valuesControl();
    preview_container_ = std::make_shared<lib::Panel>();
    auto layout = std::make_shared<lib::GridLayout>();
    layout->columns = 1U;
    layout->spacing = 5.0F;
    preview_container_->set_layout(layout);
    const bool dialog = type_ == types::CType::TalkDialog;
    preview_image_view_->set_layout_item(lib::LayoutItem{
        0, 0, 1, 1, lib::Align::Stretch, lib::Align::Stretch,
        {360.0F, 150.0F},
        {470.0F, dialog ? 190.0F : 180.0F}, {}, dialog ? 0.45F : 0.42F});
    preview_values_view_->set_layout_item(lib::LayoutItem{
        1, 0, 1, 1, lib::Align::Stretch, lib::Align::Stretch,
        {360.0F, dialog ? 170.0F : 175.0F},
        // ALL のセリフでは、値の一覧は編集欄の列で残った高
        // さだけを使い、 編集欄の下に未使用の空白を作らない。
        {470.0F, dialog ? (lib::editor_window_host_options().all_mode
                               ? 170.0F : 260.0F)
                        : 245.0F}, {}, dialog ? 0.55F : 0.58F});
    preview_container_->add(preview_image_view_);
    preview_container_->add(preview_values_view_);
    preview_container_->set_layout_item(lib::LayoutItem{
        0, 1, 2, 1, lib::Align::Stretch, lib::Align::Stretch,
        {previewWidth(), 320.0F},
        {previewWidth(), dialog ? 500.0F : 430.0F}, {}, 1.0F});
}

void AbstractMessageDialog::refFloatMessagePreview()
{
    if (!context_.props) return;
    const bool floating = static_cast<bool>(context_.props->var().floatMessagePreview);
    if (floating == static_cast<bool>(preview_window_)) return;
    const bool visible = preview_toggle_ ? preview_toggle_->checked
        : preview_layout_visible_;
    // D版は表示方式を切り替える際に値表を作り直す。名前などの設定は
    // 旧プレビューの破棄時に保存し、Flag・Step・Variantの仮入力は引き継がない。
    const auto pane_width = previewWidth();
    if (preview_window_) {
        preview_window_->visibilityChanged = {};
        preview_window_->saveWin();
        preview_window_->dispose();
        preview_window_.reset();
    }
    preview_.reset();
    preview_image_view_.reset();
    preview_values_view_.reset();
    preview_container_.reset();
    if (floating) {
        createFloatingPreview();
    } else {
        createEmbeddedPreview();
    }
    if (preview_mode_changed_) preview_mode_changed_();
    // D版と同様、独立表示・埋め込み表示の切り替えでウィンドウ幅を変えるのはプ
    // レビュー表示中だけ。 非表示のプレビューには、追加・削除する幅がない。
    if (const auto host = preview_host_window_.lock(); host && visible) {
        auto bounds = host->bounds();
        const float delta = floating ? -pane_width : pane_width;
        bounds.width = std::max(1.0F, bounds.width + delta);
        host->set_bounds(bounds);
        if (host->minimum_size)
            host->minimum_size->width = std::max(
                1.0F, host->minimum_size->width + delta);
    }
    preview_layout_visible_ = visible;
    if (preview_toggle_) preview_toggle_->checked = visible;
    if (preview_window_) {
        updatePreview();
        if (visible) preview_window_->open();
    } else {
        setPreviewLayoutData(visible, false);
        if (visible) updatePreview();
    }
}

void AbstractMessageDialog::settings(MessageSettings value)
{
    settings_ = value;
    if (selection_columns_edit_)
        selection_columns_edit_->value = settings_.selection_columns;
    if (center_x_check_) center_x_check_->checked = settings_.center_x;
    if (center_y_check_) center_y_check_->checked = settings_.center_y;
    if (boundary_check_) boundary_check_->checked = settings_.boundary_check;
    if (select_talker_check_)
        select_talker_check_->checked = settings_.select_talker;
    if (event_) {
        event_->selectionColumns(settings_.selection_columns);
        event_->centeringX(settings_.center_x);
        event_->centeringY(settings_.center_y);
        event_->boundaryCheck(settings_.boundary_check);
        event_->selectTalker(settings_.select_talker);
        event_->singleLine(settings_.single_line);
    }
    refDataVersion();
    refreshPreview();
    notifyChanged();
}

Skin::Ptr AbstractMessageDialog::summSkin() const
{
    if (context_.forced_skin) return context_.forced_skin;
    if (context_.current_skin) return context_.current_skin();
    if (!context_.props) return {};
    if (context_.summary && !context_.summary->type().empty()) {
        if (auto value = dskin::findSkin2(*context_.props,
                                         context_.summary->type(),
                                         context_.summary->skinName()))
            return value;
    }
    return dskin::findSkin2(*context_.props,
                            context_.props->var().defaultSkin.value,
                            context_.props->var().defaultSkinName.value);
}

void AbstractMessageDialog::refDataVersion()
{
    const bool legacy = summary() && summary()->legacy();
    if (selection_columns_edit_) {
        selection_columns_edit_->set_enabled(
            !legacy || settings_.selection_columns != 1U);
    }
    if (center_x_check_)
        center_x_check_->set_enabled(!legacy || settings_.center_x);
    if (center_y_check_) {
        center_y_check_->set_enabled(
            (!legacy || settings_.center_y) && !singleLine());
    }
    if (boundary_check_) {
        boundary_check_->set_enabled(
            (!legacy || settings_.boundary_check) && !singleLine());
    }
    updateSelectTalkerEnabled();
    refreshWarning();
}

void AbstractMessageDialog::updateSelectTalkerEnabled()
{
    if (!select_talker_check_) return;
    select_talker_check_->set_enabled(
        !singleLine() &&
        (!summary() || !summary()->legacy() || settings_.select_talker) &&
        hasCharacterTalker());
}

bool AbstractMessageDialog::hasCharacterTalker() const
{
    return true;
}

bool AbstractMessageDialog::singleLine() const
{
    return settings_.single_line;
}

std::vector<String> AbstractMessageDialog::warnings() const
{
    std::vector<String> result;
    const auto* prop = context_.props;
    if (!prop) return result;
    if (!prop->isTargetVersion(context_.summary, "1") &&
        settings_.selection_columns != 1) {
        result.push_back(prop->requiredMsgs().warningSelectionColumns.value);
    }
    if (!prop->isTargetVersion(context_.summary, "2")) {
        if (settings_.center_x)
            result.push_back(prop->requiredMsgs().warningCenteringX.value);
        if (settings_.center_y)
            result.push_back(prop->requiredMsgs().warningCenteringY.value);
        if (settings_.boundary_check)
            result.push_back(prop->requiredMsgs().warningBoundaryCheck.value);
    }
    if (!prop->isTargetVersion(context_.summary, "3") &&
        settings_.select_talker && hasCharacterTalker()) {
        result.push_back(prop->requiredMsgs().warningSelectTalker.value);
    }
    return result;
}

warning::TextWarnings AbstractMessageDialog::textWarnings(
    StringView text, std::span<const String> flags,
    std::span<const String> steps, std::span<const String> variants,
    std::span<const String> fonts, std::span<const char> colors,
    lib::Dictionary<String, bool>& warning_flags,
    lib::Dictionary<String, bool>& warning_steps,
    lib::Dictionary<String, bool>& warning_variants,
    lib::Dictionary<String, bool>& warning_fonts,
    lib::Dictionary<char, bool>& warning_colors) const
{
    warning::TextWarnings result;
    const auto* prop = context_.props;
    const auto active_skin = summSkin();
    if (!prop || !active_skin) return result;
    const auto legacy = context_.summary && context_.summary->legacy();
    const auto version = context_.summary ? context_.summary->dataVersion()
                                           : versioninfo::LATEST_VERSION;
    result = warning::textWarnings(
        prop->parent(), *active_skin, context_.summary, context_.use_counter,
        legacy, version, prop->var().targetVersion.value, text, flags, steps,
        variants, fonts, colors, warning_flags, warning_steps, warning_variants,
        warning_fonts, warning_colors);
    return result;
}

static void presentMessagePreview(const std::shared_ptr<lib::ImageView>& view,
                                  const ImageDataWithScale& image) {
    if (!view) return;
    view->image = image.resource();
    if (const auto data = image.data(); data && !data->frames.empty()) {
        const auto& frame = data->frames.front();
        // 選んだ画像倍率ですでにラスタライズ済みなので、 ダイアロ
        // グのフォント倍率や表示領域に収める倍率を再適用しない。
        view->design_size = lib::Size{static_cast<float>(frame.pixel_width),
                                      static_cast<float>(frame.pixel_height)};
    }
}

void AbstractMessageDialog::updatePreview()
{
    if (!preview_ && !preview_window_) {
        if (context_.props) preview_ = std::make_shared<MsgPreview>(context_);
    }
    const auto paths = imgPaths();
    if (preview_window_) {
        preview_window_->text(paths, singleLine(), text(), settings_.center_x,
                               settings_.center_y, settings_.boundary_check);
    } else if (preview_) {
        preview_->text(paths, singleLine(), text(), settings_.center_x,
                       settings_.center_y, settings_.boundary_check,
                       preview_layout_visible_);
        if (preview_layout_visible_ && preview_image_view_)
            presentMessagePreview(preview_image_view_, preview_->image());
    }
}

void AbstractMessageDialog::updateShowPreview(bool visible)
{
    if (preview_window_) {
        if (visible) preview_window_->open();
        else preview_window_->close();
    }
    if (preview_toggle_) preview_toggle_->checked = visible;
    setPreviewLayoutData(visible, true);
    if (context_.props) {
        if (type_ == types::CType::TalkMessage) {
            context_.props->var().showMessagePreview = visible;
        } else if (type_ == types::CType::TalkDialog) {
            context_.props->var().showDialogPreview = visible;
        }
    }
    // 独立表示のプレビューは open() で一度更新する。 非表示中
    // は text() が、ラスタライズせずに最新のモデルを保持済み。
    if (visible && !preview_window_) updatePreview();
}

void AbstractMessageDialog::setPreviewLayoutData(bool visible,
                                                  bool register_window)
{
    const bool old_visible = preview_container_
                                 ? preview_container_->visible()
                                 : preview_layout_visible_;
    if (!visible && preview_container_ && preview_container_->bounds().width > 1.0F)
        preview_layout_width_ = preview_container_->bounds().width;
    // 配置の反映ではプレビュー画像を再構築しない。 両方の
    // 呼び出し元が表示切り替え後、見えるようになったときに
    // 一度だけ更新する。 ここでも生成すると同じメッセージ
    // を二度描き、非表示にするときにも不要な画像処理が走る
    // 。 再表示できるよう切り替えボタン自体は残し、D版
    // と同様にプレビューペインの配置・表示だけを変える。
    if (preview_image_view_) preview_image_view_->set_visible(visible);
    if (preview_values_view_) preview_values_view_->set_visible(visible);
    if (preview_container_) preview_container_->set_visible(visible);
    preview_layout_visible_ = visible;

    if (register_window && preview_ && old_visible != visible) {
        if (const auto host = preview_host_window_.lock()) {
            auto bounds = host->bounds();
            const auto pane_width = previewWidth();
            if (pane_width > 0.0F && bounds.width > 0.0F) {
                const auto requested_delta = visible ? pane_width : -pane_width;
                bounds.width = std::max(1.0F, bounds.width + requested_delta);
                host->set_bounds(bounds);
                if (host->minimum_size) {
                    host->minimum_size->width = std::max(
                        1.0F, host->minimum_size->width + requested_delta);
                }
                geometry_ = WindowGeometry{
                    static_cast<std::int32_t>(std::lround(bounds.x)),
                    static_cast<std::int32_t>(std::lround(bounds.y)),
                    static_cast<std::int32_t>(std::lround(bounds.width)),
                    static_cast<std::int32_t>(std::lround(bounds.height)),
                    host->maximized};
                if (context_.props)
                    storeEditorGeometry(*context_.props, type_, geometry_);
                if (context_.services.gui)
                    context_.services.gui->request_redraw(host);
            }
        }
    }
}

void AbstractMessageDialog::setPreviewHostWindow(
    const std::shared_ptr<lib::Window>& window)
{
    preview_host_window_ = window;
    context_.owner = window;
    if (preview_window_ && preview_window_->window()) {
        lib::configure_owned_window(preview_window_->window(), window,
            lib::OwnedWindowKind::Modeless, lib::OwnedWindowCenter::Never);
        preview_window_->setOwner(window);
    }
    // ImageSelect は複数の中間パネルの下にあり、部品ツリーから EventDialog
    // のウィンドウを取得できない。 D版と同様、画像一覧のツールウ
    // ィンドウの親をこのダイアログにするため、所有者を明示的に渡す。
    if (auto* message = dynamic_cast<MessageDialog*>(this);
        message && message->imageSelect()) {
        message->imageSelect()->imageListOwner(window);
    }
}

float AbstractMessageDialog::previewFixedWidth() const noexcept
{
    if (!context_.props) return 0.0F;
    // ALL のセリフのプレビューも、メッセージダイアログと同様、
    // メッセージウィンドウと同じ幅にして左右の余白を設けない。
    const bool fixed = type_ == types::CType::TalkMessage ||
        (type_ == types::CType::TalkDialog &&
         lib::editor_window_host_options().all_mode);
    if (!fixed) return 0.0F;
    const auto width = context_.props->looks().messageBounds().width;
    return static_cast<float>(lib::editor_window_host_options().all_mode
        ? width : context_.props->s(width)) + 2.0F;
}

float AbstractMessageDialog::previewWidth() const noexcept
{
    if (const float fixed = previewFixedWidth(); fixed > 0.0F) return fixed;
    if (preview_layout_width_ > 0.0F) return preview_layout_width_;
    if (preview_container_ && preview_container_->bounds().width > 1.0F)
        return preview_container_->bounds().width;
    // 基準の配置では、プレビューと変数用に470論理画素を確保する。 変
    // えるのはエディタの操作領域だけで、メッセージの描画座標は変えない。
    return 470.0F;
}

void AbstractMessageDialog::refImageScale()
{
    const std::weak_ptr<int> lifetime = preview_scale_lifetime_;
    const auto preview = preview_;
    const auto floating = preview_window_;
    if (!lib::editor_window_host_options().all_mode && context_.props) {
        const auto width = static_cast<float>(
            context_.props->s(context_.props->looks().messageBounds().width));
        const auto delta = width - preview_scaled_message_width_;
        preview_scaled_message_width_ = width;
        preview_layout_width_ = width + 2.0F;
        if (preview && preview_container_) {
            auto item = preview_container_->layout_item();
            item.minimum.width = item.preferred.width = preview_layout_width_;
            preview_container_->set_layout_item(item);
            if (preview_container_->visible() && delta != 0.0F) {
                if (const auto host = preview_host_window_.lock()) {
                    auto bounds = host->bounds();
                    bounds.width = std::max(1.0F, bounds.width + delta);
                    if (host->minimum_size)
                        host->minimum_size->width = std::max(1.0F, host->minimum_size->width + delta);
                    geometry_ = WindowGeometry{
                        static_cast<std::int32_t>(std::lround(bounds.x)),
                        static_cast<std::int32_t>(std::lround(bounds.y)),
                        static_cast<std::int32_t>(std::lround(bounds.width)),
                        static_cast<std::int32_t>(std::lround(bounds.height)), host->maximized};
                    const auto gui = context_.services.gui;
                    host->set_bounds(bounds);
                    if (lifetime.expired()) return;
                    if (gui) gui->request_redraw(host);
                    if (lifetime.expired()) return;
                }
            }
        }
    }
    if (preview) preview->refImageScale();
    if (lifetime.expired()) return;
    // D版と同様、倍率適用後にキャンバスの寸法を再通知する。 生成画像は拡
    // 大縮小済みなので、そのラスターの寸法を使い、二重に倍率を掛けない。
    if (!lib::editor_window_host_options().all_mode && preview &&
        preview_layout_visible_ && preview_image_view_)
        presentMessagePreview(preview_image_view_, preview->image());
    if (floating) floating->refImageScale();
}

bool AbstractMessageDialog::canHookKeyDown() const noexcept
{
    return !preview_ || !preview_->focusInValues();
}

void AbstractMessageDialog::refUndoMax()
{
    // プレビューの画素は、取り消し履歴の上限に依存しない。 派
    // 生エディタは、画像を無効化せずに自身の履歴を更新する。
}

void AbstractMessageDialog::opened()
{
    // D版の opened() と同様、独立表示のプレビューだけを開
    // く。 埋め込みのデータは setup で初期化済みなので、こ
    // こで再生成しない。 C++版では保存済みの表示設定も再適用し
    // 、設定が無効になっていれば以前開いていたプレビューを閉じる。
    if (!preview_window_) return;
    const bool visible = context_.props &&
        (type_ == types::CType::TalkDialog
             ? static_cast<bool>(context_.props->var().showDialogPreview)
             : static_cast<bool>(context_.props->var().showMessagePreview));
    if (preview_window_) {
        if (visible) preview_window_->open();
        else preview_window_->close();
    }
}

std::shared_ptr<lib::Panel> AbstractMessageDialog::setup(
    const std::shared_ptr<lib::Container>& area)
{
    root_panel_ = std::make_shared<lib::Panel>();
    auto root_layout = std::make_shared<lib::LinearLayout>();
    root_layout->spacing = 5.0F;
    root_layout->padding = {5.0F, 5.0F, 5.0F, 5.0F};
    root_panel_->set_layout(root_layout);
    wsn_settings_panel_ = createWsnSettingsBar(root_panel_);
    preview_toggle_ = std::make_shared<lib::ToggleButton>(
        context_.props ? context_.props->requiredMsgs().messagePreview.value
                       : String{"プレビュー(P)"});
    preview_toggle_->checked =
        context_.props &&
        (type_ == types::CType::TalkDialog
             ? static_cast<bool>(context_.props->var().showDialogPreview)
             : static_cast<bool>(context_.props->var().showMessagePreview));
    preview_layout_visible_ = preview_toggle_->checked;
    preview_toggle_->set_layout_item(lib::LayoutItem{
        0, 0, 1, 1, lib::Align::Start, lib::Align::Center, {},
        {context_.props
             ? static_cast<float>(context_.props->var().buttonWidth.value)
             : 98.0F,
         26.0F}});
    preview_toggle_->on_changed = [this](bool value) { updateShowPreview(value); };
    if (context_.props && context_.props->var().floatMessagePreview) {
        createFloatingPreview();
    } else if (context_.props) {
        preview_ = std::make_shared<MsgPreview>(context_);
        preview_image_view_ = std::make_shared<lib::ImageView>();
        preview_image_view_->scale_mode = lib::ImageScaleMode::Center;
        preview_image_view_->interpolation = lib::Interpolation::Best;
        preview_image_view_->set_tooltip("メッセージのプレビュー");
        root_panel_->add(preview_image_view_);
        preview_values_view_ = preview_->valuesControl();
        root_panel_->add(preview_values_view_);
    }
    if (area) area->add(root_panel_);
    // ここでは派生した操作欄と初期値がまだ準備できていない。 D版と同
    // 様、それらの設定後に検証し、親の表示後に独立プレビューを開く。
    return root_panel_;
}

std::shared_ptr<lib::Panel> AbstractMessageDialog::createSelectionColumns(
    const std::shared_ptr<lib::Container>& parent)
{
    auto panel = std::make_shared<ReferenceInsertPanel>();
    panel->bind(context_.editor_commons);
    auto layout = std::make_shared<lib::LinearLayout>(lib::Orientation::Horizontal);
    layout->spacing = 3.0F;
    layout->cross_axis = lib::Align::Center;
    panel->set_layout(layout);
    panel->set_layout_item(lib::LayoutItem{
        0, 0, 1, 1, lib::Align::Stretch, lib::Align::Center,
        {0.0F, 24.0F}, {0.0F, 24.0F}, {}, 0.0F});
    panel->add(std::make_shared<lib::Label>(
        context_.props ? context_.props->requiredMsgs().selectionColumns.value
                       : String{"選択肢の列数"}));
    selection_columns_edit_ = std::make_shared<lib::NumberEdit>();
    selection_columns_edit_->minimum = 1;
    selection_columns_edit_->maximum = context_.props
                        ? static_cast<double>(context_.props->var().selectionColumnsMax)
                        : 32.0;
    selection_columns_edit_->value = static_cast<double>(settings_.selection_columns);
    selection_columns_edit_->on_changed = [this](double value) {
        settings_.selection_columns =
            static_cast<std::uint32_t>(std::max(1.0, value));
        refDataVersion();
        refreshPreview();
        notifyChanged();
    };
    panel->add(selection_columns_edit_);
    const auto maximum = static_cast<std::uint32_t>(selection_columns_edit_->maximum);
    panel->add(std::make_shared<lib::Label>(context_.props
        ? utils::tryFormat(context_.props->requiredMsgs().rangeHint.value, 1, maximum)
        : String{"(1.." + std::to_string(maximum) + ")"}));
    if (parent) parent->add(panel);
    return panel;
}

std::shared_ptr<lib::Panel> AbstractMessageDialog::createWsnSettingsBar(
    const std::shared_ptr<lib::Container>& parent)
{
    auto panel = std::make_shared<lib::Panel>();
    // D版と同様、これらの設定を一つの横並びの行に置く。 メッセージ
    // のキャンバスは変えず、ダイアログの計測で自然な幅を確保する。
    auto layout = std::make_shared<lib::LinearLayout>(lib::Orientation::Horizontal);
    layout->cross_axis = lib::Align::Center;
    // ALL では、幅を節約するためチェックボックス間の水平余白を設けない。
    layout->spacing = lib::editor_window_host_options().all_mode ? 0.0F : 5.0F;
    panel->set_layout(layout);
    const auto columns = createSelectionColumns(panel);
    if (columns) columns->set_layout_item({0, 0, 1, 1, lib::Align::Start, lib::Align::Center});
    const auto add_check = [&panel](String label, bool value,
                                    std::function<void(bool)> changed) {
        auto check = std::make_shared<lib::CheckBox>(std::move(label));
        check->checked = value;
        check->on_changed = std::move(changed);
        panel->add(check);
        return check;
    };
    center_x_check_ = add_check(
        context_.props ? context_.props->requiredMsgs().centeringX.value
                       : String{"横方向に中央寄せ"},
        settings_.center_x, [this](bool value) {
        settings_.center_x = value;
        refDataVersion();
        refreshPreview();
        notifyChanged();
    });
    center_y_check_ = add_check(
        context_.props ? context_.props->requiredMsgs().centeringY.value
                       : String{"縦方向に中央寄せ"},
        settings_.center_y, [this](bool value) {
        settings_.center_y = value;
        refDataVersion();
        refreshPreview();
        notifyChanged();
    });
    boundary_check_ = add_check(
        context_.props ? context_.props->requiredMsgs().boundaryCheck.value
                       : String{"禁則処理"},
        settings_.boundary_check, [this](bool value) {
        settings_.boundary_check = value;
        refDataVersion();
        refreshPreview();
        notifyChanged();
    });
    if (context_.props) boundary_check_->set_tooltip(context_.props->requiredMsgs().boundaryCheckDesc.value);
    select_talker_check_ = std::make_shared<lib::CheckBox>(
        context_.props ? context_.props->requiredMsgs().selectTalker.value
                       : String{"話者を選択状態にする"});
    select_talker_check_->checked = settings_.select_talker;
    select_talker_check_->on_changed = [this](bool value) {
        settings_.select_talker = value;
        refDataVersion();
        refreshPreview();
        notifyChanged();
    };
    panel->add(select_talker_check_);
    center_x_check_->set_layout_item({0, 1, 1, 1, lib::Align::Start, lib::Align::Center});
    center_y_check_->set_layout_item({0, 2, 1, 1, lib::Align::Start, lib::Align::Center});
    boundary_check_->set_layout_item({1, 0, 1, 1, lib::Align::Start, lib::Align::Center});
    select_talker_check_->set_layout_item({1, 1, 1, 2, lib::Align::Start, lib::Align::Center});
    if (parent) parent->add(panel);
    return panel;
}

void AbstractMessageDialog::refreshPreview()
{
    updatePreview();
}

String AbstractMessageDialog::text() const
{
    return text_;
}

CardImageList AbstractMessageDialog::imgPaths() const
{
    return image_paths_;
}

void AbstractMessageDialog::text(String value)
{
    text_ = std::move(value);
    notifyChanged();
    refreshPreview();
}

void AbstractMessageDialog::refreshWarning()
{
    warning_cache_ = warnings();
    publishWarnings();
}

void AbstractMessageDialog::imgPaths(CardImageList value)
{
    image_paths_ = std::move(value);
    updateSelectTalkerEnabled();
    refreshPreview();
}

bool AbstractMessageDialog::refPath(StringView old_path, StringView new_path,
                                    bool is_directory)
{
    (void)old_path;
    (void)is_directory;
    if (preview_) preview_->invalidateImageCache();
    if (preview_window_) preview_window_->invalidateImageCache();
    refreshWarning();
    refreshPreview();
    return !new_path.empty();
}

void AbstractMessageDialog::refPaths(StringView parent)
{
    (void)parent;
    if (preview_) preview_->invalidateImageCache();
    if (preview_window_) preview_window_->invalidateImageCache();
    refreshWarning();
    refreshPreview();
}

void AbstractMessageDialog::refFlagAndStep()
{
    refreshWarning();
    refreshPreview();
}

void AbstractMessageDialog::initializeFromEvent()
{
    if (!event_) return;
    text_ = event_->text();
    settings_.selection_columns = event_->selectionColumns();
    settings_.center_x = event_->centeringX();
    settings_.center_y = event_->centeringY();
    settings_.boundary_check = event_->boundaryCheck();
    settings_.select_talker = event_->selectTalker();
    settings_.single_line = event_->singleLine();
    if (!event_->cardImages().empty()) {
        for (const auto& image : event_->cardImages())
            if (image) image_paths_.push_back(std::make_shared<CardImage>(nullptr, *image));
    } else {
        for (const auto& path : event_->cardPaths())
            image_paths_.push_back(decodeStoredCardImage(path));
    }
}

void AbstractMessageDialog::notifyChanged()
{
    if (context_.on_changed) context_.on_changed();
}

void AbstractMessageDialog::publishWarnings()
{
    if (context_.on_warnings_changed)
        context_.on_warnings_changed(warning_cache_);
}

SpeakDialog::SpeakDialog(MessageContext context, const Content* parent,
                         Content* event, WindowGeometry size)
    : AbstractMessageDialog(std::move(context), types::CType::TalkDialog,
                            parent, event, size)
{
    if (!lib::editor_window_host_options().all_mode) {
        // ダイアログごとに専用の連携処理を持つ。 入力の MessageContext
        // を複製してフックを共有すると、兄弟ダイアログ同士で上書き・解除してしまう。
        context_.parent_history_hooks =
            std::make_shared<MessageContext::ParentHistoryHooks>();
        const std::weak_ptr<void> lifetime = key_filter_lifetime_;
        context_.parent_history_hooks->undo = [this, lifetime] {
            if (!lifetime.expired()) (void)undoEdit();
        };
        context_.parent_history_hooks->redo = [this, lifetime] {
            if (!lifetime.expired()) (void)redoEdit();
        };
    } else {
        context_.parent_history_hooks.reset();
    }
    pane_id_ = "SpeakDialog-" +
               std::to_string(reinterpret_cast<std::uintptr_t>(this));
    if (event_) {
        for (const auto& dialog : event_->dialogsRef()) {
            if (dialog) dialogs_.push_back(cloneDialog(*dialog));
        }
        selected_talker_ = event_->talkerNC();
        // 所有者が共有するスロット情報を保持せず、ダイ
        // アログの編集内容を専用のベクターに保存する。
        const auto eventCoupons = event_->coupons();
        coupon_records_.assign(eventCoupons.begin(), eventCoupons.end());
        coupons_.clear();
        for (const auto& coupon : coupon_records_)
            if (coupon) coupons_.push_back(coupon->name());
        init_value_ = event_->initValue();
    }
    if (dialogs_.empty()) {
        dialogs_.push_back(std::make_shared<SDialog>());
        repaired_empty_dialogs_ = event_ &&
            (selected_talker_ != types::Talker::Selected ||
             !coupon_records_.empty() || init_value_ != 1);
    }
    selected_ = std::min(selected_, dialogs_.size() - 1);
    syncSelectedText();
}

SpeakDialog::~SpeakDialog()
{
    // インラインの Combo 編集欄は、現在の入力処理が戻るまで、ダイアログの
    // ウィジェットツリーより長く存続する場合がある。 派生クラスの履歴メンバーを
    // 破棄する前にコールバックを失効させ、その後で共有のフック状態を切り離す。
    key_filter_lifetime_.reset();
    if (context_.parent_history_hooks) {
        context_.parent_history_hooks->undo = {};
        context_.parent_history_hooks->redo = {};
    }
}

DialogSnapshot SpeakDialog::readAPD() const
{
    DialogSnapshot result;
    result.selected = selected_;
    for (const auto& dialog : dialogs_) {
        if (!dialog) continue;
        result.texts.push_back(dialog->text());
        result.required_coupons.push_back(dialog->rCoupons());
    }
    return result;
}

void SpeakDialog::writeAPD(const DialogSnapshot& snapshot)
{
    dialogs_.clear();
    for (std::size_t i = 0; i < snapshot.texts.size(); ++i) {
        auto dialog = std::make_shared<SDialog>(
            snapshot.texts[i],
            i < snapshot.required_coupons.size() ? snapshot.required_coupons[i]
                                                   : lib::Array<String>{});
        dialogs_.push_back(std::move(dialog));
    }
    if (dialogs_.empty()) dialogs_.push_back(std::make_shared<SDialog>());
    selected_ = std::min(snapshot.selected, dialogs_.size() - 1);
    syncSelectedText();
    refreshDlgList();
}

void SpeakDialog::storeEdit()
{
    if (restoring_history_) return;
    const auto maximum = context_.props
                             ? static_cast<std::size_t>(std::max<std::int32_t>(
                                   0, context_.props->var().undoMaxEtc))
                             : 128U;
    if (maximum == 0U) { undo_history_.clear(); redo_history_.clear(); return; }
    undo_history_.push_back(readAPD());
    if (undo_history_.size() > maximum) undo_history_.erase(undo_history_.begin());
    redo_history_.clear();
}

bool SpeakDialog::undoEdit()
{
    if (undo_history_.empty()) return false;
    syncSelectedText();
    redo_history_.push_back(readAPD());
    auto value = std::move(undo_history_.back());
    undo_history_.pop_back();
    restoring_history_ = true;
    writeAPD(value);
    restoring_history_ = false;
    notifyChanged();
    refreshPreview();
    refreshWarning();
    return true;
}

bool SpeakDialog::redoEdit()
{
    if (redo_history_.empty()) return false;
    syncSelectedText();
    undo_history_.push_back(readAPD());
    auto value = std::move(redo_history_.back());
    redo_history_.pop_back();
    restoring_history_ = true;
    writeAPD(value);
    restoring_history_ = false;
    notifyChanged();
    refreshPreview();
    refreshWarning();
    return true;
}

void SpeakDialog::setupTextMenu(const std::shared_ptr<lib::TextEdit>& editor, bool special)
{
    if (!editor) return;
    dmenu::Commons comm;
    comm.services = context_.services;
    if (context_.props) {
        comm.presentation.menuText = [prop = context_.props](auto id) { return prop->buildMenu(id); };
        comm.presentation.menuImage = [prop = context_.props](auto id) { return prop->images().menu(id); };
    }
    // D版と同様、ネイティブの Text/StyledText のキーボードによる取り消しは、
    // そのコントロール内に留める。 テキストのポップアップの取り消しは SpeakDialog
    // の複合履歴を使う。 両方の表示側で、共通のウィジェット経路を通じてこの区別を維持する。
    auto facade = dmenu::createTextMenu(comm, dmenu::TMM(editor));
    facade->nativeTextKeyboardUndo =
        static_cast<bool>(facade->text().textWidget());
    text_menus_.push_back(facade);
    const auto menu = facade->menu();
    facade->historyUndo = [this] { return undoEdit(); };
    facade->historyRedo = [this] { return redoEdit(); };
    facade->historyCanUndo = [this] { return !undo_history_.empty(); };
    facade->historyCanRedo = [this] { return !redo_history_.empty(); };
    if (special) (void)setupSPCharsMenu(context_, summary(), [this] { return summSkin(); },
        context_.use_counter, menu, true, true, [] { return true; }, [this](StringView value) {
            if (value.size() == 2 && value.front() == '&') put(value[1]); else insert(value);
        });
}

void SpeakDialog::refreshWarning()
{
    if (valued_coupon_view_ && context_.props) {
        std::int64_t maximum = init_value_, minimum = init_value_;
        for (const auto& coupon : coupon_records_) if (coupon) {
            if (coupon->value() < 0) minimum += coupon->value();
            else maximum += coupon->value();
        }
        const auto& msgs = context_.props->requiredMsgs();
        const auto format = maximum <= 0 ? msgs.valuedTalkerMaxIsLess0.value
            : minimum <= 0 ? msgs.valuedTalkerMaxMinLess0.value : msgs.valuedTalkerMaxMin.value;
        valued_coupon_view_->toolTip(coupon_records_.empty() ? String{}
            : ::cwxeditor::cwx::utils::tryFormat(format, maximum, minimum));
    }
    warning_cache_ = warnings();
    dialog_warnings_.assign(dialogs_.size(), {});
    const auto* prop = context_.props;
    if (prop && selected_talker_ == types::Talker::Valued &&
        !prop->targetVersion(context_.summary, "1.50")) {
        warning_cache_.push_back(prop->requiredMsgs().warningValuedTalker.value);
    }
    lib::Dictionary<String, bool> wf;
    lib::Dictionary<String, bool> ws;
    lib::Dictionary<String, bool> wv;
    lib::Dictionary<String, bool> wfont;
    lib::Dictionary<char, bool> wc;
    // D版と同様、行の診断はテキストの警告だけとし、称号の警告は全体で扱う。
    for (std::size_t dialog_index = 0; dialog_index < dialogs_.size();
         ++dialog_index) {
        const auto& dialog = dialogs_[dialog_index];
        if (!dialog) continue;
        auto& dialog_warnings = dialog_warnings_[dialog_index];
        const auto tw = textWarnings(
            dialog->text(), dialog->flagsInText(), dialog->stepsInText(),
            dialog->variantsInText(), dialog->fontsInText(), dialog->colorsInText(),
            wf, ws, wv, wfont, wc);
        warning_cache_.insert(warning_cache_.end(), tw.noDup.begin(), tw.noDup.end());
        dialog_warnings.insert(dialog_warnings.end(), tw.all.begin(), tw.all.end());
        if (prop) {
            const bool classic = context_.summary && context_.summary->legacy();
            const auto version = context_.summary
                                     ? context_.summary->dataVersion()
                                     : String("1.50");
            for (const auto& coupon : dialog->rCoupons()) {
                const auto current = warning::couponWarnings(
                    prop->parent(), classic, version,
                    prop->var().targetVersion.value, coupon, false,
                    prop->requiredMsgs().toneCoupons.value);
                warning_cache_.insert(warning_cache_.end(), current.begin(),
                                      current.end());
            }
        }
        warning_cache_.insert(warning_cache_.end(), dialog_warnings.begin(),
                              dialog_warnings.end());
    }
    if (selected_talker_ == types::Talker::Valued && valued_coupon_view_) {
        const auto current = valued_coupon_view_->warnings();
        warning_cache_.insert(warning_cache_.end(), current.begin(), current.end());
    }
    std::sort(warning_cache_.begin(), warning_cache_.end());
    warning_cache_.erase(
        std::unique(warning_cache_.begin(), warning_cache_.end()),
        warning_cache_.end());
    for (auto& current : dialog_warnings_) {
        std::sort(current.begin(), current.end());
        current.erase(std::unique(current.begin(), current.end()), current.end());
    }
    if (dialog_table_) {
        refreshHoveredDialogWarning();
        if (dialog_table_->rows.size() == dialog_warnings_.size()) {
            auto rows = dialog_table_->rows;
            const auto warning_icon = context_.props
                                          ? context_.props->images().load(
                                                StringView{"warning.png"})
                                          : lib::ResourceId{};
            for (std::size_t index = 0; index < rows.size(); ++index) {
                if (!rows[index].cells.empty())
                    rows[index].cells.front().icon =
                        dialog_warnings_[index].empty() ? lib::ResourceId{}
                                                        : warning_icon;
            }
            (void)lib::replace_table_rows(
                dialog_table_, std::move(rows),
                selected_ < dialogs_.size()
                    ? std::vector<std::size_t>{selected_}
                    : std::vector<std::size_t>{});
        }
    }
    publishWarnings();
}
void SpeakDialog::refUndoMax()
{
    AbstractMessageDialog::refUndoMax();
    const auto maximum = context_.props
                             ? static_cast<std::size_t>(std::max<std::int32_t>(
                                   0, context_.props->var().undoMaxEtc))
                             : 128U;
    const auto trim = [maximum](auto& history) {
        if (maximum == 0U) {
            history.clear();
        } else if (history.size() > maximum) {
            history.erase(history.begin(),
                          history.begin() + static_cast<std::ptrdiff_t>(
                                                history.size() - maximum));
        }
    };
    trim(undo_history_);
    trim(redo_history_);
    refreshDialogMenu();
}

bool SpeakDialog::canHookKeyDown() const noexcept
{
    return AbstractMessageDialog::canHookKeyDown();
}

bool SpeakDialog::selectChanged(std::size_t index)
{
    if (index >= dialogs_.size()) return false;
    const std::weak_ptr<void> lifetime = preview_scale_lifetime_;
    selected_ = index;
    syncSelectedText();
    if (lifetime.expired()) return true;
    refreshDialogActions();
    refreshPreview();
    if (lifetime.expired()) return true;
    if (context_.editor_commons) context_.editor_commons->refreshToolBar();
    if (lifetime.expired()) return true;
    refreshWarning();
    return true;
}

SDialogPtr SpeakDialog::createDialog(const SDialog& dialog)
{
    return cloneDialog(dialog);
}

SDialogPtr SpeakDialog::createDialog()
{
    auto dialog = std::make_shared<SDialog>();
    insertDialog(dialog, selected_ < dialogs_.size() ? selected_ : dialogs_.size());
    return dialog;
}

bool SpeakDialog::insertDialog(SDialogPtr dialog, std::size_t index, bool store)
{
    if (!dialog) return false;
    if (store) storeEdit();
    index = std::min(index, dialogs_.size());
    dialogs_.insert(dialogs_.begin() + static_cast<std::ptrdiff_t>(index),
                    std::move(dialog));
    selected_ = index;
    syncSelectedText();
    refreshDlgList();
    refreshPreview();
    refreshWarning();
    notifyChanged();
    return true;
}

bool SpeakDialog::deleteDialog(std::size_t index, bool store)
{
    if (dialogs_.size() <= 1 || index >= dialogs_.size()) return false;
    if (store) storeEdit();
    dialogs_.erase(dialogs_.begin() + static_cast<std::ptrdiff_t>(index));
    // D版の Table と同様、前の行が削除されたら、残った項目に選択
    // を追従させる。 下向きのドラッグ移動で元の行を削除する場合も同様。
    if (!lib::editor_window_host_options().all_mode && index < selected_)
        --selected_;
    selected_ = std::min(selected_, dialogs_.size() - 1);
    syncSelectedText();
    refreshDlgList();
    refreshPreview();
    refreshWarning();
    notifyChanged();
    return true;
}

bool SpeakDialog::canDeleteDialog() const noexcept
{
    return dialogs_.size() > 1 && selected_ < dialogs_.size();
}

bool SpeakDialog::deleteDialogSel()
{
    return deleteDialog(selected_);
}

bool SpeakDialog::canOverDialog() const noexcept
{
    return selected_ > 0 && selected_ < dialogs_.size();
}

bool SpeakDialog::overDialog()
{
    if (!canOverDialog()) return false;
    --selected_;
    syncSelectedText();
    refreshDlgList();
    refreshPreview();
    refreshWarning();
    return true;
}

bool SpeakDialog::canUnderDialog() const noexcept
{
    return selected_ + 1 < dialogs_.size();
}

bool SpeakDialog::underDialog()
{
    if (!canUnderDialog()) return false;
    ++selected_;
    syncSelectedText();
    refreshDlgList();
    refreshPreview();
    refreshWarning();
    return true;
}

bool SpeakDialog::canUp() const noexcept
{
    return canOverDialog();
}

bool SpeakDialog::up()
{
    if (!canUp()) return false;
    storeEdit();
    std::swap(dialogs_[selected_], dialogs_[selected_ - 1]);
    --selected_;
    refreshDlgList();
    refreshPreview();
    refreshWarning();
    notifyChanged();
    return true;
}

bool SpeakDialog::canDown() const noexcept
{
    return canUnderDialog();
}

bool SpeakDialog::down()
{
    if (!canDown()) return false;
    storeEdit();
    std::swap(dialogs_[selected_], dialogs_[selected_ + 1]);
    ++selected_;
    refreshDlgList();
    refreshPreview();
    refreshWarning();
    notifyChanged();
    return true;
}

bool SpeakDialog::canCopyToUpper() const noexcept
{
    return canOverDialog();
}

bool SpeakDialog::copyToUpper()
{
    if (!canCopyToUpper()) return false;
    storeEdit();
    // D版と同様、ダイアログの文字列をコピーして渡す際に、末尾の改行を正規化する。
    const auto value = normalizeSpeakDialogText(text());
    for (std::size_t index = 0; index < selected_; ++index)
        dialogs_[index]->text(value);
    refreshDlgList();
    refreshPreview();
    refreshWarning();
    notifyChanged();
    return true;
}

bool SpeakDialog::canCopyToLower() const noexcept
{
    return canUnderDialog();
}

bool SpeakDialog::copyToLower()
{
    if (!canCopyToLower()) return false;
    storeEdit();
    // D版と同様、ダイアログの文字列をコピーして渡す際に、末尾の改行を正規化する。
    const auto value = normalizeSpeakDialogText(text());
    for (std::size_t index = selected_ + 1; index < dialogs_.size(); ++index)
        dialogs_[index]->text(value);
    refreshDlgList();
    refreshPreview();
    refreshWarning();
    notifyChanged();
    return true;
}

bool SpeakDialog::canCopyToDialogs() const noexcept
{
    return dialogs_.size() > 1 && selected_ < dialogs_.size();
}

bool SpeakDialog::copyToDialogs()
{
    if (!canCopyToDialogs()) return false;
    storeEdit();
    // D版と同様、ダイアログの文字列をコピーして渡す際に、末尾の改行を正規化する。
    const auto value = normalizeSpeakDialogText(text());
    for (std::size_t i = 0; i < dialogs_.size(); ++i) {
        if (i != selected_) dialogs_[i]->text(value);
    }
    refreshDlgList();
    refreshPreview();
    refreshWarning();
    notifyChanged();
    return true;
}

bool SpeakDialog::canCopyDialog() const noexcept
{
    return selected_ < dialogs_.size() && dialogs_[selected_] != nullptr;
}

bool SpeakDialog::canPasteDialog() const
{
    if (!context_.services.clipboard) return false;
    return xmlbytestransfer::CBisXML(*context_.services.clipboard);
}

bool SpeakDialog::copyDialog()
{
    if (!canCopyDialog() || !context_.services.clipboard) return false;
    auto* const common = context_.editor_commons;
    const std::weak_ptr<const int> commonLife = common
        ? common->lifetimeToken() : std::weak_ptr<const int>{};
    xmlbytestransfer::ClipData clipboard(context_.services.clipboard);
    const auto xml = dialogs_[selected_]->toNode().text();
    if (context_.props)
        xmlbytestransfer::XMLtoCB(*context_.props, clipboard, xml);
    else
        xmlbytestransfer::XMLtoCB(false, clipboard, xml);
    // クリップボードへの反映が、ホストを同期的に無効化する場合がある。
    if (common && !commonLife.expired()) common->refreshToolBar();
    return true;
}

bool SpeakDialog::cutDialog()
{
    if (!canDeleteDialog() || !copyDialog()) return false;
    return deleteDialogSel();
}

bool SpeakDialog::pasteDialog()
{
    if (!context_.services.clipboard) return false;
    try {
        const auto xml = xmlbytestransfer::CBtoXML(*context_.services.clipboard);
        if (xml.empty()) return false;
        auto node = event::XNode::parse(xml);
        if (node.name() != SDialog::XML_NAME) return false;
        auto dialog = SDialog::createFromNode(node, nullptr);
        if (!dialog) return false;
        return insertDialog(std::move(dialog),
                            selected_ < dialogs_.size() ? selected_ : dialogs_.size());
    } catch (...) {
        return false;
    }
}

bool SpeakDialog::cloneSelectedDialog()
{
    if (!canCopyDialog()) return false;
    return insertDialog(cloneDialog(*dialogs_[selected_]), selected_);
}

void SpeakDialog::put(char32_t value)
{
    if (text_editor_) {
        putColor(*text_editor_, value);
    } else {
        putColor(text_, value);
        syncSelectedText();
    }
}

void SpeakDialog::insert(StringView value)
{
    if (text_editor_) {
        const auto start = std::min(text_editor_->selection_start,
                                    text_editor_->text.size());
        const auto length = std::min(text_editor_->selection_length,
                                     text_editor_->text.size() - start);
        text_editor_->text.replace(start, length, value);
        text_editor_->selection_start = start + value.size();
        text_editor_->selection_length = 0;
        if (text_editor_->on_changed)
            text_editor_->on_changed(text_editor_->text);
        else
            text(text_editor_->text);
    } else {
        text_.insert(text_.size(), value);
        syncSelectedText();
    }
}

void SpeakDialog::putText(SDialog& dialog)
{
    const auto value = normalizeMessage(text_editor_ ? text_editor_->text : text_);
    // D版の putText と同様、編集欄自体は生の入力を残し、保存する編集テキストには lastRet を適用する。
    dialog.text(normalizeSpeakDialogText(value));
    text_ = value;
    refreshDlgList(selected_);
    refreshWarning();
}

void SpeakDialog::putRCoupons(SDialog& dialog)
{
    dialog.rCoupons(coupon_editor_ ? requiredCouponLines(coupon_editor_->text)
                                   : dialog.rCoupons());
    refreshWarning();
}

void SpeakDialog::updateTalker()
{
    if (talker_combo_) {
        const auto index = selected_talker_ == types::Talker::Selected
                               ? 0
                               : selected_talker_ == types::Talker::Unselected
                                     ? 1
                                     : selected_talker_ == types::Talker::Random
                                           ? 2
                                           : 3;
        auto items = talker_combo_->items;
        (void)lib::replace_combo_items(talker_combo_, std::move(items), index,
                                       talker_combo_->edit_text);
    }
    const auto valued = selected_talker_ == types::Talker::Valued;
    if (valued_panel_) valued_panel_->set_enabled(valued);
    if (valued_coupon_editor_) valued_coupon_editor_->set_enabled(valued);
    if (valued_coupon_view_) valued_coupon_view_->enabled(valued);
    if (init_value_editor_) init_value_editor_->set_enabled(valued);
    if (context_.editor_commons) context_.editor_commons->refreshToolBar();
}

void SpeakDialog::refreshDlgList()
{
    if (!dialog_table_) return;
    std::vector<lib::TableRow> rows;
    rows.reserve(dialogs_.size());
    for (const auto& dialog : dialogs_) {
        if (!dialog) continue;
        lib::TableRow row;
        // 先頭の空行は、セリフの縦位置を決める配置情報。 一覧
        // の見出し用ではないため、実際のメッセージには残す。
        const String message = normalizeMessage(dialog->text());
        const auto content = message.find_first_not_of(" \t\r\n");
        row.cells.push_back(lib::ListItem{
            content == String::npos ? String{} :
                (lib::editor_window_host_options().all_mode
                    ? firstLine(StringView(message).substr(content))
                    : standaloneDialogCaption(StringView(message).substr(content))),
            {}, 0, false, true});
        rows.push_back(std::move(row));
    }
    auto selection = selected_ < dialogs_.size()
                         ? std::vector<std::size_t>{selected_}
                         : std::vector<std::size_t>{};
    if (!lib::replace_table_rows(dialog_table_, std::move(rows),
                                 std::move(selection))) return;
    refreshDialogActions();
}

void SpeakDialog::refreshDlgList(std::size_t index)
{
    (void)index;
    refreshDlgList();
}

SDialogPtr SpeakDialog::selection() const
{
    if (selected_ >= dialogs_.size()) return SDialogPtr{};
    return dialogs_[selected_];
}

void SpeakDialog::refSkin()
{
    applyMessageEditorStyle(context_, summSkin(), text_editor_);
}

bool SpeakDialog::refMenu(StringView command)
{
    if (command == "undo") return undoEdit();
    if (command == "redo") return redoEdit();
    if (command == "create-dialog") {
        const auto dialog = createDialog();
        (void)dialog;
        return true;
    }
    if (command == "delete-dialog") return deleteDialogSel();
    if (command == "up") return up();
    if (command == "down") return down();
    if (command == "copy-all") return copyToDialogs();
    if (command == "copy-upper") return copyToUpper();
    if (command == "copy-lower") return copyToLower();
    if (command == "select-upper") return overDialog();
    if (command == "select-lower") return underDialog();
    if (command == "cut") return cutDialog();
    if (command == "copy") return copyDialog();
    if (command == "paste") return pasteDialog();
    if (command == "clone") return cloneSelectedDialog();
    return false;
}

bool SpeakDialog::refMenuImpl(StringView command, const lib::Action& action,
                              const std::function<bool()>& enabled)
{
    if (enabled && !enabled()) return false;
    if (command.empty() || !action) return false;
    action();
    return true;
}

bool SpeakDialog::openCWXPath(StringView path, bool shell_activate)
{
    namespace cwx_path = ::cwxeditor::cwx::path;
    const auto category = cwx_path::cpcategory(path);
    if (category != "dialog") return cwx_path::cpempty(path);
    std::size_t index{};
    try {
        index = cwx_path::cpindex(path);
    } catch (...) {
        return false;
    }
    if (index >= dialogs_.size()) return false;

    // D版と同様、任意のテキスト要素を処理する前に表の行を選択する。
    if (dialog_table_)
        (void)lib::select_table_rows(*dialog_table_, {index}, true, false);
    if (!selectChanged(index)) return false;
    if (shell_activate && context_.services.gui && dialog_table_)
        context_.services.gui->focus(dialog_table_);

    auto rest = cwx_path::cpbottom(path);
    if (cwx_path::cpcategory(rest) == "text")
        rest = cwx_path::cpbottom(rest);
    return cwx_path::cpempty(rest);
}

String SpeakDialog::text() const
{
    if (text_editor_) return normalizeMessage(text_editor_->text);
    if (selected_ >= dialogs_.size() || !dialogs_[selected_]) return String{};
    return normalizeMessage(dialogs_[selected_]->text());
}

void SpeakDialog::refreshDialogMenu()
{
    if (!dialog_menu_) return;
    auto items = dialog_menu_->items;
    for (auto& item : items) {
        if (item.command == "undo") item.enabled = !undo_history_.empty();
        else if (item.command == "redo") item.enabled = !redo_history_.empty();
        else if (item.command == "up" || item.command == "select-upper" ||
                 item.command == "copy-upper") item.enabled = canUp();
        else if (item.command == "down" || item.command == "select-lower" ||
                 item.command == "copy-lower") item.enabled = canDown();
        else if (item.command == "copy-all") item.enabled = canCopyToDialogs();
        else if (item.command == "cut" || item.command == "delete-dialog")
            item.enabled = canDeleteDialog();
        else if (item.command == "copy" || item.command == "clone")
            item.enabled = canCopyDialog();
        else if (item.command == "paste") item.enabled = canPasteDialog();
    }
    (void)lib::replace_menu_items(dialog_menu_, std::move(items));
}

void SpeakDialog::refreshDialogActions()
{
    if (!dialog_toolbar_) return;
    auto items = dialog_toolbar_->items;
    for (auto& item : items) {
        if (item.command == "delete-dialog") item.enabled = canDeleteDialog();
        else if (item.command == "up") item.enabled = canUp();
        else if (item.command == "down") item.enabled = canDown();
        else if (item.command == "copy-all") item.enabled = canCopyToDialogs();
        else if (item.command == "copy-upper") item.enabled = canCopyToUpper();
        else if (item.command == "copy-lower") item.enabled = canCopyToLower();
    }
    (void)lib::replace_tool_bar_items(dialog_toolbar_, std::move(items));
}

void SpeakDialog::setupDialogTransfers()
{
    if (!dialog_table_) return;
    dialog_menu_ = std::make_shared<lib::Menu>(true);
    auto add = [this](types::MenuID id, String fallback, String command) {
        auto text = context_.props ? context_.props->buildMenu(id) : std::move(fallback);
        const auto icon = context_.props ? context_.props->images().menu(id) : lib::ResourceId{};
        dialog_menu_->items.push_back({std::move(text), std::move(command), {}, icon});
    };
    auto separator = [this] {
        dialog_menu_->items.push_back({});
        dialog_menu_->items.back().separator = true;
    };
    add(types::MenuID::Undo, "元に戻す", "undo");
    add(types::MenuID::Redo, "やり直す", "redo");
    separator();
    add(types::MenuID::CreateDialog, "セリフを追加", "create-dialog");
    separator();
    add(types::MenuID::Up, "上へ移動", "up");
    add(types::MenuID::Down, "下へ移動", "down");
    separator();
    add(types::MenuID::CopyToAllDialogs, "全てにコピー", "copy-all");
    add(types::MenuID::CopyToUpperDialogs, "上の全てにコピー", "copy-upper");
    add(types::MenuID::CopyToLowerDialogs, "下の全てにコピー", "copy-lower");
    separator();
    add(types::MenuID::OverDialog, "一つ上を選択", "select-upper");
    add(types::MenuID::UnderDialog, "一つ下を選択", "select-lower");
    separator();
    add(types::MenuID::Cut, "切り取り", "cut");
    add(types::MenuID::Copy, "コピー", "copy");
    add(types::MenuID::Paste, "貼り付け", "paste");
    add(types::MenuID::Delete, "削除", "delete-dialog");
    add(types::MenuID::Clone, "複製", "clone");
    dialog_menu_->on_command = [this](StringView command) {
        (void)refMenu(command);
    };
    dialog_menu_->on_open = [this] { refreshDialogMenu(); };
    dialog_table_->on_pointer = [this, life = std::weak_ptr<void>{key_filter_lifetime_}](lib::PointerEvent& pointer) {
        if (life.expired()) return;
        if (pointer.action == lib::PointerAction::Move || pointer.action == lib::PointerAction::Leave) {
            hovered_dialog_ = pointer.action == lib::PointerAction::Leave ? std::nullopt
                : pointer.item_hit_test_available
                    ? (pointer.item_index >= 0 ? std::optional<std::size_t>(pointer.item_index) : std::nullopt)
                    : lib::hit_test_table_row(*dialog_table_, pointer.position,
                        lib::table_row_height(*dialog_table_, 24.0F));
            refreshHoveredDialogWarning();
        }
        if (pointer.button != lib::PointerButton::Secondary ||
            pointer.action != lib::PointerAction::Up || !context_.services.gui)
            return;
        refreshDialogMenu();
        context_.services.gui->show_context_menu(dialog_menu_, dialog_table_,
                                                  pointer.position);
        pointer.handled = true;
    };

    if (!context_.services.drag_drop) return;
    lib::DragSourceBinding source;
    source.source = dialog_table_;
    source.allowed = lib::DropOperation::Copy | lib::DropOperation::Move;
    source.provide_data = [this] {
        dragged_dialog_ = selection();
        if (!dragged_dialog_) return lib::ClipboardData{};
        auto node = dragged_dialog_->toNode();
        node.newAttr("paneId", pane_id_);
        return xmlbytestransfer::makeXMLPayload(node.text());
    };
    source.completed = [this](lib::DropOperation operation) {
        if (operation == lib::DropOperation::Move && dragged_dialog_) {
            const auto found = std::find(dialogs_.begin(), dialogs_.end(),
                                         dragged_dialog_);
            if (found != dialogs_.end())
                (void)deleteDialog(static_cast<std::size_t>(
                                       std::distance(dialogs_.begin(), found)),
                                   false);
        }
        dragged_dialog_.reset();
    };
    context_.services.drag_drop->bind_source(std::move(source));

    lib::DropTargetBinding target;
    target.target = dialog_table_;
    target.accepted_formats = {
        String(xmlbytestransfer::XML_BYTES_FORMAT),
        String(xmlbytestransfer::XML_BYTES_MIME)};
    target.on_enter = [](lib::DragEvent& drag) {
        drag.selected = lib::DropOperation::Move;
        drag.handled = true;
    };
    target.on_over = target.on_enter;
    target.on_drop = [this](lib::DragEvent& drag) {
        try {
            String xml;
            if (!xmlbytestransfer::extractXML(drag, xml)) return;
            auto node = event::XNode::parse(xml);
            if (node.name() != SDialog::XML_NAME) return;
            auto dialog = SDialog::createFromNode(node, nullptr);
            if (!dialog) return;
            const auto hit_index = lib::editor_window_host_options().all_mode
                ? drag.target_index
                : lib::hit_test_table_drop_row(*dialog_table_, drag.position,
                    lib::table_row_height(*dialog_table_, 24.0F));
            const auto index = std::min(hit_index.value_or(dialogs_.size()),
                                        dialogs_.size());
            storeEdit();
            if (!insertDialog(std::move(dialog), index, false)) return;
            const bool same_pane = node.attr("paneId", false) == pane_id_;
            // D版と同様、別ペインからは複製を挿入するが DROP_NONE のままにし、転送元を残す。
            drag.selected = same_pane ? lib::DropOperation::Move
                                       : lib::DropOperation::None;
            drag.handled = true;
        } catch (...) {
        }
    };
    context_.services.drag_drop->bind_target(std::move(target));
}

types::Talker SpeakDialog::selectedTalker() const noexcept
{
    return selected_talker_;
}

CardImageList SpeakDialog::imgPaths() const
{
    CardImageList result;
    result.push_back(std::make_shared<CardImage>(selected_talker_));
    return result;
}

namespace {

// ALL のセリフのツールボタンは、標準サイズの1.3倍にする。
constexpr float kAllSpeechToolScale = 1.3F;

// 一行に per_row 個のアイコンボタンを並べる折り返し欄の幅
// 。 左右に1pxの余白を設け、各ボタンの後に1pxの間隔を置く。
[[nodiscard]] float iconStripWidth(std::size_t per_row, float icon_scale) noexcept
{
    return std::ceil(2.0F + static_cast<float>(per_row) *
                     (24.0F * std::max(1.0F, icon_scale) + 1.0F));
}

// ALL のセリフでは、メンバーの特殊文字とプレイヤ
// ー番号の数値欄をまとめ、 その下に九つの色ボタンを
// 3×3で置く。どちらもテキスト欄の右側に配置する。
void createAllSpeechCharBars(
    const MessageContext& context, const Summary* summ,
    const std::shared_ptr<lib::Container>& parent,
    std::function<void(StringView)> insert, std::function<void(char32_t)> put_color,
    const Props* prop)
{
    const auto icon = [prop](StringView filename) {
        return prop ? const_cast<Props*>(prop)->images().load(filename)
                    : lib::ResourceId{};
    };
    // メンバーアイコンは一行四つで折り返し、プレ
    // イヤー番号の数値欄と増減ボタンは直下の独立
    // した行に置く。 アイコン横の二列目に置くと
    // 、狭い側面パネルに収まらず切れてしまう。
    auto row = std::make_shared<lib::Panel>();
    auto row_layout = std::make_shared<lib::GridLayout>();
    row_layout->columns = 1;
    row_layout->spacing = 2.0F;
    row->set_layout(row_layout);
    row->set_layout_item({.horizontal = lib::Align::Start,
                          .vertical = lib::Align::Start});
    auto members = std::make_shared<lib::ToolBar>();
    members->icon_scale = kAllSpeechToolScale;
    // 幅を指定し、折り返した全行の高さを計測させる。 幅
    // がないと一行として計測され、残りが切れてしまう。
    const float members_width = iconStripWidth(4U, kAllSpeechToolScale);
    members->set_layout_item({.row = 0U, .column = 0U,
                              .horizontal = lib::Align::Start,
                              .vertical = lib::Align::Start,
                              .minimum = {members_width, 0.0F},
                              .preferred = {members_width, 0.0F}});
    row->add(members);
    const auto special_text = [prop](char value) -> String {
        if (!prop) return String("#") + value;
        switch (value) {
        case 'I': return prop->requiredMsgs().scRef.value;
        case 'M': return prop->requiredMsgs().scTalkerName(types::Talker::Selected);
        case 'U': return prop->requiredMsgs().scTalkerName(types::Talker::Unselected);
        case 'R': return prop->requiredMsgs().scTalkerName(types::Talker::Random);
        case 'C': return prop->requiredMsgs().scTalkerName(types::Talker::Card);
        case 'T': return prop->requiredMsgs().scTeam.value;
        case 'Y': return prop->requiredMsgs().scYado.value;
        default: return String("#") + value;
        }
    };
    for (const char value : std::array<char, 7>{'I', 'M', 'U', 'R', 'C', 'T', 'Y'}) {
        members->items.push_back({special_text(value), String("#") + value,
                                  icon(String("sc_") +
                                       static_cast<char>(std::tolower(value)) + ".png"),
                                  true, false, false, false});
    }
    lib::ToolItem separator;
    separator.separator = true;
    members->items.push_back(separator);
    const auto player_number = createPCSPCharBar(context, summ, members, insert, row);
    if (player_number) {
        // メンバーアイコンの下の行に置く、一桁の数値欄。
        player_number->input_max_length = 1U;
        auto item = player_number->layout_item();
        item.row = 1U;
        item.column = 0U;
        item.horizontal = lib::Align::Start;
        item.minimum.width = 0.0F;
        item.preferred.width = 0.0F;
        player_number->set_layout_item(item);
    }
    members->on_command = [insert, player_number, prop](StringView command) {
        if (!insert) return;
        if (command == "@player-card-name") {
            const auto number = player_number
                ? static_cast<std::uint32_t>(std::max(
                      1.0, std::round(player_number->value)))
                : 1U;
            insert(prop ? String("$") + prop->sys().playerCardName(number) + "$"
                        : String("$Player") + std::to_string(number) + "$");
        } else {
            insert(command);
        }
    };
    if (parent) parent->add(row);

    auto colors = std::make_shared<lib::ToolBar>();
    colors->icon_scale = kAllSpeechToolScale;
    const auto color_text = [prop](const char value) -> String {
        if (!prop) return itemCommand("&", value);
        switch (value) {
        case 'W': return prop->requiredMsgs().colorW.value;
        case 'R': return prop->requiredMsgs().colorR.value;
        case 'B': return prop->requiredMsgs().colorB.value;
        case 'G': return prop->requiredMsgs().colorG.value;
        case 'Y': return prop->requiredMsgs().colorY.value;
        case 'O': return prop->requiredMsgs().colorO.value;
        case 'P': return prop->requiredMsgs().colorP.value;
        case 'L': return prop->requiredMsgs().colorL.value;
        case 'D': return prop->requiredMsgs().colorD.value;
        default: return itemCommand("&", value);
        }
    };
    for (const auto value : std::array<char, 9>{'W', 'R', 'B', 'G', 'Y',
                                                 'O', 'P', 'L', 'D'}) {
        colors->items.push_back({color_text(value), itemCommand("&", value),
                                 icon(String("cc_") +
                                      static_cast<char>(std::tolower(value)) + ".png"),
                                 true, false, false, false});
    }
    // 色ボタンは一行三つ、三行に並べる。
    const float colors_width = iconStripWidth(3U, kAllSpeechToolScale);
    colors->set_layout_item({.horizontal = lib::Align::Start,
                             .vertical = lib::Align::Start,
                             .minimum = {colors_width, 0.0F},
                             .preferred = {colors_width, 0.0F}});
    colors->on_command = [put_color](StringView command) {
        if (command.size() == 2 && command.front() == '&' && put_color)
            put_color(static_cast<unsigned char>(command[1]));
    };
    if (parent) parent->add(colors);
}

}  // namespace

std::shared_ptr<lib::Panel> SpeakDialog::setup(
    const std::shared_ptr<lib::Container>& area, const bool combine_preview)
{
    auto panel = AbstractMessageDialog::setup();
    if (wsn_settings_panel_) panel->remove(wsn_settings_panel_);
    if (preview_toggle_) panel->remove(preview_toggle_);
    if (preview_image_view_) panel->remove(preview_image_view_);
    if (preview_values_view_) panel->remove(preview_values_view_);

    auto body = std::make_shared<lib::SplitView>();
    body->orientation = lib::Orientation::Horizontal;
    if (context_.props) {
        const auto left = std::max(1, context_.props->var().talkMainSashL.value);
        const auto right = std::max(1, context_.props->var().talkMainSashR.value);
        body->weights = {static_cast<float>(left), static_cast<float>(right)};
        body->on_weights_changed = [this](const std::vector<float>& weights) {
            if (!context_.props || weights.size() < 2U) return;
            context_.props->var().talkMainSashL = static_cast<std::int32_t>(
                std::max(1.0F, std::round(weights[0])));
            context_.props->var().talkMainSashR = static_cast<std::int32_t>(
                std::max(1.0F, std::round(weights[1])));
        };
    } else {
        body->weights = {3.0F, 5.0F};
    }

    auto talker_side = std::make_shared<lib::Panel>();
    auto talker_layout = std::make_shared<lib::LinearLayout>();
    talker_layout->spacing = 5.0F;
    talker_side->set_layout(talker_layout);
    auto talker_group = std::make_shared<lib::GroupBox>();
    talker_group->title = context_.props ? context_.props->requiredMsgs().talker.value
                                         : String{"話者"};
    auto talker_group_layout = std::make_shared<lib::LinearLayout>();
    talker_group_layout->spacing = 4.0F;
    talker_group->set_layout(talker_group_layout);
    talker_combo_ = std::make_shared<lib::ComboBox>();
    const auto talkerName = [this](types::Talker talker, String fallback) {
        if (!context_.props) return fallback;
        return context_.props->requiredMsgs().defaultSelection(
            context_.props->requiredMsgs().talkerName(talker));
    };
    talker_combo_->items = {
        {talkerName(types::Talker::Selected, "選択中メンバ"), "selected", 0, true},
        {talkerName(types::Talker::Unselected, "選択外メンバ"), "unselected", 0, true},
        {talkerName(types::Talker::Random, "ランダム"), "random", 0, true},
        {talkerName(types::Talker::Valued, "評価"), "valued", 0, true}};
    talker_combo_->on_selection_changed = [this](std::optional<std::size_t> index) {
        if (!index) return;
        selected_talker_ = *index == 0
                               ? types::Talker::Selected
                               : *index == 1 ? types::Talker::Unselected
                                             : *index == 2 ? types::Talker::Random
                                                           : types::Talker::Valued;
        updateTalker();
        refreshPreview();
        refreshWarning();
        notifyChanged();
    };
    talker_group->add(talker_combo_);

    auto tone_group = std::make_shared<lib::GroupBox>();
    tone_group->title = context_.props ? context_.props->requiredMsgs().toneCoupons.value
                                       : String{"口調条件"};
    auto tone_layout = std::make_shared<lib::GridLayout>();
    tone_layout->columns = 2;
    tone_layout->spacing = 3.0F;
    tone_group->set_layout(tone_layout);
    tone_coupon_combo_ = std::make_shared<lib::ComboBox>();
    tone_coupon_combo_->editable = true;
    const auto tone_skin = summSkin();
    dutils::EditorContext tone_context;
    tone_context.prop = context_.props;
    tone_context.skin = tone_skin.get();
    tone_context.summary = summary();
    // D版の話者選択と同様、使用済み称号がない新規シナリオで
    // も、 スキンの性別・年代・素質・特徴の称号を含める。
    const auto standard_tones = dutils::castCoupons(tone_context, true, {}, false, false);
    std::vector<String> used_tones;
    if (context_.use_counter &&
        (!context_.props || context_.props->var().usedCouponToCombo.value)) {
        auto ids = context_.use_counter->keys<usecounter::CouponId>();
        std::sort(ids.begin(), ids.end(), [](const auto& left, const auto& right) {
            return left.id < right.id;
        });
        for (const auto& id : ids)
            if (!id.id.empty() &&
                std::find(standard_tones.begin(), standard_tones.end(), id.id) == standard_tones.end())
                used_tones.push_back(id.id);
    }
    const auto append_tones = [this](const auto& names) {
        for (const auto& name : names)
            tone_coupon_combo_->items.push_back({name, name, 0, true});
    };
    const bool standard_first = context_.props && context_.props->var().useNamesAfterStandard.value;
    append_tones(standard_first ? standard_tones : used_tones);
    if (!standard_tones.empty() && !used_tones.empty())
        tone_coupon_combo_->items.push_back({{}, {}, 0, false});
    append_tones(standard_first ? used_tones : standard_tones);
    tone_coupon_combo_->set_layout_item({.row = 0, .column = 0,
                                           .horizontal = lib::Align::Stretch,
                                          .preferred = {
                                              context_.props
                                                  ? static_cast<float>(context_.props->var().talkersWidth.value)
                                                  : 170.0F,
                                              24.0F},
                                          .grow = 1.0F});
    tone_group->add(tone_coupon_combo_);
    auto append_tone_coupon = std::make_shared<lib::Button>("＋");
    if (context_.props) {
        append_tone_coupon->text.clear();
        append_tone_coupon->icon = context_.props->images().load(
            lib::StringView{"set_beast.png"});
        append_tone_coupon->accessibility_name = context_.props->requiredMsgs().setTalkerCoupon.value;
    }
    append_tone_coupon->set_tooltip(
        context_.props ? context_.props->requiredMsgs().setTalkerCoupon.value
                       : String{"話者条件クーポンを追加"});
    append_tone_coupon->set_layout_item({.row = 0, .column = 1,
                                         .preferred = {28.0F, 24.0F}});
    append_tone_coupon->on_click = [this] {
        if (!tone_coupon_combo_) return;
        // D版と同様、項目選択後の編集も含めて Combo.getText()
        // を追加する。 編集可能なコンボは選択時にラベルを反映済みなので
        // 、 保持している選択インデックスで表示中の入力を上書きしない。
        const String value = tone_coupon_combo_->edit_text;
        if (value.empty() || !coupon_editor_) return;
        auto text = coupon_editor_->text;
        if (!text.empty() && text.back() != '\n') text.push_back('\n');
        text += value;
        text.push_back('\n');
        (void)lib::set_text_value(*coupon_editor_, std::move(text));
    };
    tone_group->add(append_tone_coupon);
    coupon_editor_ = std::make_shared<lib::TextEdit>();
    coupon_editor_->multiline = true;
    // ALL の口調条件の欄は、高さを30px短くする。
    coupon_editor_->set_layout_item(lib::LayoutItem{1, 0, 1, 2,
        lib::Align::Stretch, lib::Align::Stretch, {140, 70},
        {200, lib::editor_window_host_options().all_mode ? 82.0F : 112.0F},
        {}, 1.0F});
    coupon_editor_->on_changed = [this](StringView value) {
        if (auto dialog = selection()) {
            storeEdit();
            dialog->rCoupons(requiredCouponLines(value));
        }
        refreshWarning();
        notifyChanged();
    };
    tone_group->add(coupon_editor_);

    valued_panel_ = std::make_shared<lib::GroupBox>();
    valued_panel_->title = context_.props ? context_.props->requiredMsgs().valued.value : "評価条件";
    auto valued_layout = std::make_shared<lib::GridLayout>();
    valued_layout->columns = 2;
    valued_layout->spacing = 4.0F;
    valued_panel_->set_layout(valued_layout);
    if (context_.editor_commons) {
        auto view = std::make_shared<couponview::CouponView<couponview::CVType::Valued>>(
            *context_.editor_commons, context_.summary_owner,
            context_.use_counter_owner, valued_panel_);
        view->coupons(coupon_records_);
        view->panel()->set_layout_item(lib::LayoutItem{1, 0, 1, 2,
            lib::Align::Stretch, lib::Align::Stretch, {180, 100}, {260, 148}, {}, 1.0F});
        view->addModifyListener([this, weak = std::weak_ptr<couponview::CouponViewBase>(view)] {
            const auto current = weak.lock();
            if (!current) return;
            const auto currentCoupons = current->coupons();
            coupon_records_.assign(currentCoupons.begin(), currentCoupons.end());
            coupons_.clear();
            for (const auto& coupon : coupon_records_)
                if (coupon) coupons_.push_back(coupon->name());
            refreshWarning();
            notifyChanged();
        });
        // ALL の値付き称号のツールボタンは1.3倍にする。
        if (lib::editor_window_host_options().all_mode)
            view->toolIconScale(kAllSpeechToolScale);
        valued_coupon_view_ = std::move(view);
    } else {
        auto coupon_label = std::make_shared<lib::Label>("必要クーポン");
        coupon_label->set_layout_item({0, 0});
        valued_panel_->add(coupon_label);
        valued_coupon_editor_ = std::make_shared<lib::TextEdit>();
        valued_coupon_editor_->multiline = true;
        for (const auto& coupon : coupons_) {
            if (!valued_coupon_editor_->text.empty()) valued_coupon_editor_->text.push_back('\n');
            valued_coupon_editor_->text += coupon;
        }
        valued_coupon_editor_->set_layout_item(lib::LayoutItem{1, 0, 1, 2,
            lib::Align::Stretch, lib::Align::Stretch, {140, 70}, {200, 100}, {}, 1.0F});
        valued_coupon_editor_->on_changed = [this](StringView value) {
            coupons_ = nonEmptyLines(value);
            coupon_records_.clear();
            for (const auto& name : coupons_)
                coupon_records_.push_back(
                    std::make_shared<::cwxeditor::cwx::coupon::Coupon>(name, 0));
            refreshWarning();
            notifyChanged();
        };
        valued_panel_->add(valued_coupon_editor_);
    }
    auto init_label = std::make_shared<lib::Label>(
        context_.props ? context_.props->requiredMsgs().initValue.value
                       : String{"初期点"});
    init_label->set_layout_item({0, 0});
    valued_panel_->add(init_label);
    init_value_editor_ = std::make_shared<lib::NumberEdit>();
    const auto coupon_value_max = context_.props
        ? static_cast<int>(context_.props->var().couponValueMax.value) : 999;
    init_value_editor_->minimum = -coupon_value_max;
    init_value_editor_->maximum = coupon_value_max;
    init_value_editor_->value = init_value_;
    init_value_editor_->set_layout_item({0, 1});
    init_value_editor_->on_changed = [this](double value) {
        init_value_ = static_cast<int>(value);
        refreshWarning();
        notifyChanged();
    };
    valued_panel_->add(init_value_editor_);
    talker_side->add(talker_group);
    auto talker_split = std::make_shared<lib::SplitView>();
    talker_split->orientation = lib::Orientation::Vertical;
    if (context_.props) {
        const auto upper = std::max(1, context_.props->var().talkLeftSashL.value);
        const auto lower = std::max(1, context_.props->var().talkLeftSashR.value);
        talker_split->weights = {static_cast<float>(upper), static_cast<float>(lower)};
        talker_split->on_weights_changed = [this](const std::vector<float>& weights) {
            if (!context_.props || weights.size() < 2U) return;
            context_.props->var().talkLeftSashL = static_cast<std::int32_t>(
                std::max(1.0F, std::round(weights[0])));
            context_.props->var().talkLeftSashR = static_cast<std::int32_t>(
                std::max(1.0F, std::round(weights[1])));
        };
    } else {
        talker_split->weights = {4.0F, 5.0F};
    }
    talker_split->add(tone_group);
    talker_split->add(valued_panel_);
    talker_side->add(talker_split);
    body->add(talker_side);

    auto dialog_side = std::make_shared<lib::Panel>();
    auto dialog_layout = std::make_shared<lib::GridLayout>();
    dialog_layout->columns = 2;
    dialog_layout->spacing = 4.0F;
    dialog_side->set_layout(dialog_layout);

    dialog_table_ = std::make_shared<lib::TableView>();
    dialog_table_->columns.push_back({"セリフ", 1.0F});
    // ALL のセリフ一覧の上には、「セリフ」の見出し行を置かない。
    if (lib::editor_window_host_options().all_mode)
        dialog_table_->header_visible = false;
    // ALL のセリフ一覧は、高さを60px短くする。 すべての ALL
    // ホストで同じ高さを使い、Webだけの行数削減は行わない。
    {
        const bool all_mode = lib::editor_window_host_options().all_mode;
        dialog_table_->set_layout_item(lib::LayoutItem{0, 0, 1, 1,
            lib::Align::Stretch, lib::Align::Stretch,
            {220, all_mode ? 85.0F : 115.0F},
            {340, all_mode ? 110.0F : 170.0F},
            {}, 1.0F});
    }
    dialog_table_->on_selection_changed = [this](const std::vector<std::size_t>& selection) {
        if (selection.empty()) {
            // D版の selectChanged と同様、表に選択行がない場合は0行目を選ぶ。
            if (!dialogs_.empty()) {
                (void)lib::select_table_rows(*dialog_table_, {0U}, true, false);
                (void)selectChanged(0U);
            }
        } else {
            const std::weak_ptr<void> lifetime = preview_scale_lifetime_;
            const auto changed = selectChanged(selection.front());
            (void)changed;
            if (lifetime.expired()) return;
            refreshHoveredDialogWarning();
        }
    };
    setupDialogTransfers();
    dialog_side->add(dialog_table_);
    dialog_toolbar_ = std::make_shared<lib::ToolBar>();
    const auto tool_text = [this](types::MenuID id, String fallback) {
        return context_.props ? context_.props->requiredMsgs().menuText(id)
                              : fallback;
    };
    const auto tool_icon = [this](StringView file) {
        return context_.props ? context_.props->images().load(file)
                              : lib::ResourceId{};
    };
    const auto add_tool = [this](String text, String command,
                                 lib::ResourceId icon) {
        dialog_toolbar_->items.push_back(
            {std::move(text), std::move(command), icon, true, false, false,
             false});
    };
    // ALL のセリフ用ツールは、一行二つのアイコンとし、区切りを
    // 置かない。 従来の一列配置に比べ、高さをおよそ半分にする。
    const bool two_column_tools = lib::editor_window_host_options().all_mode;
    dialog_toolbar_->keyboard_navigation = !two_column_tools;
    dialog_toolbar_->keyboard_vertical = !two_column_tools;
    const auto separator = [this, two_column_tools] {
        if (two_column_tools) return;
        lib::ToolItem item;
        item.separator = true;
        dialog_toolbar_->items.push_back(std::move(item));
    };
    add_tool(tool_text(types::MenuID::CreateDialog, "セリフの作成"),
             "create-dialog", tool_icon("evt_speak.png"));
    add_tool(tool_text(types::MenuID::DeleteDialog, "セリフの削除"),
             "delete-dialog", tool_icon("del_res.png"));
    separator();
    add_tool(tool_text(types::MenuID::Up, "上へ移動"), "up",
             tool_icon("up.png"));
    add_tool(tool_text(types::MenuID::Down, "下へ移動"), "down",
             tool_icon("down.png"));
    separator();
    add_tool(tool_text(types::MenuID::CopyToAllDialogs, "全てにコピー"),
             "copy-all", tool_icon("copy_dialog.png"));
    add_tool(tool_text(types::MenuID::CopyToUpperDialogs, "上の全てにコピー"),
             "copy-upper", tool_icon("copy_dialog_u.png"));
    add_tool(tool_text(types::MenuID::CopyToLowerDialogs, "下の全てにコピー"),
             "copy-lower", tool_icon("copy_dialog_l.png"));
    dialog_toolbar_->on_command = [this](StringView command) {
        (void)refMenu(command);
        refreshDialogActions();
    };
    if (two_column_tools) {
        // 一行二つ、1.3倍のアイコンを並べる。七つのツールで四行になる。
        dialog_toolbar_->icon_scale = kAllSpeechToolScale;
        const float tools_width = iconStripWidth(2U, kAllSpeechToolScale);
        const float tools_height = std::ceil(
            4.0F * 24.0F * kAllSpeechToolScale + 2.0F);
        dialog_toolbar_->set_layout_item({0, 1, 1, 1, lib::Align::Center,
                                          lib::Align::Start,
                                          {tools_width, tools_height},
                                          {tools_width, tools_height}, {}});
    } else {
        dialog_toolbar_->set_layout_item({0, 1, 1, 1, lib::Align::Center,
                                          lib::Align::Stretch, {32, 240}, {32, 240}, {}});
    }
    dialog_side->add(dialog_toolbar_);
    text_editor_ = std::make_shared<lib::TextEdit>(text());
    text_editor_->multiline = true;
    text_editor_->wrap = true;
    applyMessageEditorStyle(context_, summSkin(), text_editor_);
    const bool all_speech = lib::editor_window_host_options().all_mode;
    // ALL のテキスト欄は左寄せにし、その右にメンバーの行と
    // 3×3の色ボタンを置く（下の editor_row）。
    text_editor_->set_layout_item(all_speech
        ? lib::LayoutItem{0, 0, 1, 1, lib::Align::Start, lib::Align::Start,
                          {}, {}, {}, 0.0F}
        : lib::LayoutItem{1, 0, 1, 2, lib::Align::Center, lib::Align::Center,
                          {}, {}, {}, 0.0F});
    text_editor_->on_changed = [this](StringView value) {
        storeEdit();
        text_ = normalizeMessage(String(value));
        if (auto dialog = selection())
            dialog->text(normalizeSpeakDialogText(text_));
        refreshDlgList(selected_);
        notifyChanged();
        refreshPreview();
        refreshWarning();
    };
    std::shared_ptr<lib::Panel> editor_side;
    if (all_speech) {
        auto editor_row = std::make_shared<lib::Panel>();
        auto editor_row_layout = std::make_shared<lib::LinearLayout>(
            lib::Orientation::Horizontal);
        editor_row_layout->spacing = 4.0F;
        editor_row_layout->cross_axis = lib::Align::Start;
        editor_row->set_layout(editor_row_layout);
        editor_row->set_layout_item(lib::LayoutItem{1, 0, 1, 2,
            lib::Align::Start, lib::Align::Start, {}, {}, {}, 0.0F});
        editor_row->add(text_editor_);
        editor_side = std::make_shared<lib::Panel>();
        auto editor_side_layout = std::make_shared<lib::LinearLayout>();
        editor_side_layout->spacing = 2.0F;
        editor_side->set_layout(editor_side_layout);
        editor_side->set_layout_item({.horizontal = lib::Align::Start,
                                      .vertical = lib::Align::Start});
        editor_row->add(editor_side);
        dialog_side->add(editor_row);
    } else {
        dialog_side->add(text_editor_);
    }
    setupTextMenu(text_editor_, true);
    setupTextMenu(coupon_editor_, false);
    // D版の SpeakDialog と同様、二つの文字ボタン欄は左側の
    // 話者・称号設定の下ではなく、右側のテキスト欄の下に置く。 共通の
    // ボタン欄の実装を使い、ホストに二列にまたがる明示的な行を与える。
    auto character_bars = std::make_shared<lib::Panel>();
    auto character_layout = std::make_shared<lib::LinearLayout>();
    character_layout->spacing = 2.0F;
    character_bars->set_layout(character_layout);
    character_bars->set_layout_item(lib::LayoutItem{2, 0, 1, 2,
        lib::Align::Stretch, lib::Align::Center, {}, {}, {}, 0.0F});
    if (all_speech) {
        // ALL ではテキスト欄の右にメンバーと3×3の色ボタンを置き、 その下にス
        // キンの文字（#A～#Z）を二行で並べる。すべてのボタンを1.3倍にする。
        createAllSpeechCharBars(
            context_, summary(), editor_side,
            [this](StringView value) { insert(value); },
            [this](char32_t value) { put(value); }, context_.props);
        (void)createSkinSCharBar(
            context_, character_bars, [this](StringView value) { insert(value); },
            [this] { return summSkin(); }, 2U, kAllSpeechToolScale);
    } else {
        (void)createSCharBar(
            context_, summary(), character_bars,
            [this](StringView value) { insert(value); },
            [this](char32_t value) { put(value); }, context_.props, false);
        (void)createSkinSCharBar(
            context_, character_bars, [this](StringView value) { insert(value); },
            [this] { return summSkin(); });
    }
    dialog_side->add(character_bars);
    body->add(dialog_side);
    auto editor_column = std::make_shared<lib::Panel>();
    auto editor_layout = std::make_shared<lib::LinearLayout>();
    editor_layout->spacing = 5.0F;
    editor_column->set_layout(editor_layout);
    editor_container_ = editor_column;
    editor_column->add(body);
    // セリフの各参照ドロップダウンは、メッセージダイア
    // ログより30px狭くする（160 - 30）。
    (void)createFlagStepBar(
        editor_column, [this](StringView value) { insert(value); }, context_,
        context_.props, summary(), context_.use_counter, context_.forced_skin,
        true, {}, lib::editor_window_host_options().all_mode
                      ? kVariableComboWidth - 30.0F : 0.0F);
    if (wsn_settings_panel_) editor_column->add(wsn_settings_panel_);

    if (preview_image_view_ && preview_values_view_) {
        // ALL の編集欄の列は、操作部品が必要とする高さにする。
        // 設定欄の下に空白を残す、固定の500px指定は使わない。
        const bool all_mode = lib::editor_window_host_options().all_mode;
        editor_column->set_layout_item(lib::LayoutItem{0, 0, 1, 1,
            lib::Align::Stretch, lib::Align::Stretch,
            {620, all_mode ? 0.0F : 390.0F}, {760, all_mode ? 0.0F : 500.0F},
            {}, 1.0F});

        auto preview_column = std::make_shared<lib::Panel>();
        preview_container_ = preview_column;
        auto preview_layout = std::make_shared<lib::GridLayout>();
        preview_layout->columns = 1;
        preview_layout->spacing = 2.0F;
        preview_layout->padding = {};
        preview_column->set_layout(preview_layout);
        preview_image_view_->set_layout_item(lib::LayoutItem{0, 0, 1, 1,
            lib::Align::Stretch, lib::Align::Stretch,
            {360, 150}, {470, 180}, {}, 0.0F});
        preview_values_view_->set_layout_item(lib::LayoutItem{1, 0, 1, 1,
            lib::Align::Stretch, lib::Align::Stretch,
            {360, 170}, {470, all_mode ? 170.0F : 260.0F}, {}, 1.0F});
        preview_column->add(preview_image_view_);
        preview_column->add(preview_values_view_);
        preview_column->set_layout_item(lib::LayoutItem{
            0, 0, 1, 1, lib::Align::Stretch, lib::Align::Stretch,
            {previewWidth(), 320.0F}, {previewWidth(), 500.0F}, {}, 1.0F});
        if (combine_preview) {
            auto main_split = std::make_shared<lib::SplitView>();
            main_split->orientation = lib::Orientation::Horizontal;
            main_split->weights = {0.67F, 0.33F};
            main_split->add(editor_column);
            main_split->add(preview_column);
            panel->add(main_split);
        }
    } else {
        preview_container_.reset();
        if (combine_preview) panel->add(editor_column);
    }
    auto result = combine_preview ? panel : editor_column;
    if (area) area->add(result);

    // D版の SpeakDialog と同様、表示全体に対する二つのキー処理
    // を設ける。 共通バックエンドはフォーカスのあるウィジェットへ入力を渡す
    // ため、完成した子ツリーに処理を設定し、 各ウィジェットのネイティブ処理
    // も維持する。本文・口調のテキストメニューはダイアログの履歴を共有し、
    // 値付き称号とプレビューの値のペインは、D版と同様に独立した履歴を持つ。
    const std::weak_ptr<void> key_lifetime = key_filter_lifetime_;
    struct DialogAccelerator {
        int accelerator{};
        StringView command;
    };
    const auto dialog_accelerator = [this](const types::MenuID id,
                                            const StringView command) {
        return DialogAccelerator{
            context_.props ? static_cast<int>(dmenu::convertAccelerator(
                                 context_.props->buildMenu(id))) : 0,
            command};
    };
    // D版と同様、現在の MenuID のキー割り当てを正とする。
    // ユーザーの変更後に、以前の既定キーを別名として残さない。
    const auto current_accelerators = [dialog_accelerator] { return std::array{
        dialog_accelerator(types::MenuID::Up, "up"),
        dialog_accelerator(types::MenuID::Down, "down"),
        dialog_accelerator(types::MenuID::OverDialog, "select-upper"),
        dialog_accelerator(types::MenuID::UnderDialog, "select-lower"),
        dialog_accelerator(types::MenuID::CreateDialog, "create-dialog"),
        dialog_accelerator(types::MenuID::DeleteDialog, "delete-dialog"),
        dialog_accelerator(types::MenuID::CopyToAllDialogs, "copy-all"),
        dialog_accelerator(types::MenuID::CopyToUpperDialogs, "copy-upper"),
        dialog_accelerator(types::MenuID::CopyToLowerDialogs, "copy-lower")}; };
    const auto valued_root = valued_coupon_view_ ? valued_coupon_view_->panel()
                                                  : lib::WidgetPtr{};
    const std::function<void(const lib::WidgetPtr&, bool)> bind_key_filter =
        [this, key_lifetime, current_accelerators, valued_root,
         &bind_key_filter](const lib::WidgetPtr& widget, bool hook_undo) {
            if (!widget) return;
            if (widget == preview_values_view_ || widget == valued_root)
                hook_undo = false;
            const bool native_text =
                static_cast<bool>(std::dynamic_pointer_cast<lib::TextEdit>(widget)) ||
                ([&] {
                    const auto combo =
                        std::dynamic_pointer_cast<lib::ComboBox>(widget);
                    return combo && combo->editable;
                })();
            const bool dialog_list = widget.get() == dialog_table_.get();
            const auto previous = widget->on_key_down;
            widget->on_key_down =
                [this, key_lifetime, previous, hook_undo, native_text,
                  dialog_list, current_accelerators](lib::KeyEvent& event) {
                    if (previous) previous(event);
                    if (event.handled || key_lifetime.expired()) return;
                    const auto undo_accelerator = context_.props ? static_cast<int>(dmenu::convertAccelerator(
                        context_.props->buildMenu(types::MenuID::Undo))) : 0;
                    const auto redo_accelerator = context_.props ? static_cast<int>(dmenu::convertAccelerator(
                        context_.props->buildMenu(types::MenuID::Redo))) : 0;
                    const auto state = static_cast<std::uint32_t>(event.modifiers);
                    const auto character = acceleratorCharacter(event);
                    // D版の基本の履歴フィルターと同様、ネイティブのテキストとプレビュ
                    // ーの値は対象外にする。 下のセリフ専用のコマンドフィルターでは、
                    // テキスト欄からの設定済みダイアログショートカットを受け付ける。
                    if (hook_undo && !native_text && canHookKeyDown()) {
                        if (dmenu::eqAcc(undo_accelerator,
                                         static_cast<int>(event.key), character,
                                         state)) {
                            event.handled = undoEdit();
                            if (event.handled) return;
                        } else if (dmenu::eqAcc(redo_accelerator,
                                               static_cast<int>(event.key),
                                               character, state)) {
                            event.handled = redoEdit();
                            if (event.handled) return;
                        }
                    }
                    // D版と同様、基本のフィルターがダイアログ一覧の取り消し・やり直しを処
                    // 理する。 自身の一覧を除外するのは SpeakDialog の二つ目
                    // のフィルターだけ。 先に除外すると、基本の履歴キーまで無効になる。
                    if (dialog_list) return;
                    for (const auto& accelerator : current_accelerators()) {
                        const bool configured = dmenu::eqAcc(
                            accelerator.accelerator,
                            static_cast<int>(event.key), character, state);
                        if (!configured)
                            continue;
                        event.handled = refMenu(accelerator.command);
                        return;
                    }
                };
            if (const auto container =
                    std::dynamic_pointer_cast<lib::Container>(widget))
                for (const auto& child : container->children())
                    bind_key_filter(child, hook_undo);
        };
    bind_key_filter(result, true);

    updateTalker();
    refreshDlgList();
    refSkin();
    refreshWarning();
    refDataVersion();
    // 派生ペインの生成後に、D版の初期プレビュー表示を適用する。 変える
    // のは配置だけであり、opened() で再びラスタライズしない。
    if (!preview_window_)
        setPreviewLayoutData(preview_toggle_ && preview_toggle_->checked, false);
    return result;
}

bool SpeakDialog::apply()
{
    if (!event_) return false;
    // D版と同様、重複するローカルの取り消しスナップショットを追加せず、編集した一覧を反映する。
    lib::Array<SDialogPtr> result;
    for (const auto& dialog : dialogs_) {
        if (!dialog) continue;
        // D版の適用と同様、_dlgs をそのまま反映する。テキストは編集・コピーの経路で正規化済み。
        auto saved = cloneDialog(*dialog);
        result.push_back(std::move(saved));
    }
    event_->dialogs(std::move(result));
    event_->talkerNC(selected_talker_);
    if (selected_talker_ == types::Talker::Valued)
        event_->couponRecords(coupon_records_);
    else
        event_->couponRecords({});
    event_->initValue(selected_talker_ == types::Talker::Valued ? init_value_ : 1);
    event_->selectionColumns(settings_.selection_columns);
    event_->centeringX(settings_.center_x);
    event_->centeringY(settings_.center_y);
    event_->boundaryCheck(settings_.boundary_check);
    event_->selectTalker(settings_.select_talker);
    notifyChanged();
    return true;
}

void SpeakDialog::selectedIndex(std::size_t value)
{
    if (dialogs_.empty()) {
        selected_ = 0;
    } else {
        selected_ = std::min(value, dialogs_.size() - 1);
    }
    syncSelectedText();
    refreshDlgList();
}

void SpeakDialog::syncSelectedText()
{
    const auto sync_revision = ++text_sync_revision_;
    if (selected_ >= dialogs_.size() || !dialogs_[selected_]) return;
    text_ = dialogs_[selected_]->text();
    if (text_editor_) {
        text_editor_->text = text_;
        // セリフの切り替えは、SWTの Text の値全体を置き換える。
        // 以前の話者の選択位置が、新しいテキストのUTF-8の複数バイ
        // ト文字の途中を指す場合があるため引き継がない。 その位置へ挿
        // 入すると文字列が壊れ、後の厳密なメッセージ整形で例外になる。
        text_editor_->selection_start = text_editor_->text.size();
        text_editor_->selection_length = 0;
    }
    if (coupon_editor_) {
        coupon_editor_->text.clear();
        for (const auto& coupon : dialogs_[selected_]->rCoupons()) {
            coupon_editor_->text += coupon;
            coupon_editor_->text.push_back('\n');
        }
    }
    // 選択や親のスナップショットの復元は、ネイティブのテキスト値を置き換える。
    // 別のセリフ・本文に以前の履歴を適用しないよう、ローカルの取り消し・やり
    // 直し履歴を消す。 TextMenuModify::reset は自身の履
    // 歴だけを消し、ファサードの共有コンテキストメニューのコールバックは残す。
    const std::weak_ptr<void> life = key_filter_lifetime_;
    const auto menus = text_menus_;
    for (const auto& menu : menus) {
        if (menu && menu->nativeTextKeyboardUndo) menu->reset();
        // リセットはコンテキストメニューを更新し、選択・履歴へ再入する場合がある。 メンバーのベク
        // ターのイテレーターを保持せず、新しくなったテキストのスナップショットをリセットしない。
        if (life.expired() || text_sync_revision_ != sync_revision) return;
    }
}

SDialogPtr SpeakDialog::cloneDialog(const SDialog& dialog)
{
    return dialog.dup();
}

MessageDialog::MessageDialog(MessageContext context, const Content* parent,
                             Content* event, WindowGeometry size)
    : AbstractMessageDialog(std::move(context), types::CType::TalkMessage,
                            parent, event, size)
{
    selected_images_ = image_paths_;
    if (context_.editor_commons) {
        const std::weak_ptr<int> lifetime = preview_scale_lifetime_;
        image_scale_connection_ = context_.editor_commons->refImageScale.add([this, lifetime] {
            if (lifetime.expired() || lib::editor_window_host_options().all_mode ||
                !context_.props || !image_select_) return;
            const auto size = context_.props->looks().cardSize();
            const auto width = context_.props->s(static_cast<std::int32_t>(size.width));
            const auto height = context_.props->s(static_cast<std::int32_t>(size.height));
            image_select_->setPreviewSize(width, height, structs::CInsets{});
        });
    }
    if (event_) {
        // D版と同様、保存されたカードパス一覧が空の場合だけ画
        // 像なしタブを選ぶ。 空の File が明示的にある
        // 場合は、描画できるパスがなくても画像モードを選ぶ。
        const bool has_card_path = !event_->cardPaths().empty();
        selected_tab_ = event_->singleLine() ? 2U : has_card_path ? 0U : 1U;
    }
}

MessageDialog::~MessageDialog()
{
    preview_scale_lifetime_.reset();
    if (context_.editor_commons)
        context_.editor_commons->refImageScale.remove(image_scale_connection_);
}

void MessageDialog::selectedImages(CardImageList value)
{
    selected_images_ = std::move(value);
    image_paths_ = selected_images_;
    if (image_select_) image_select_->setCardImages(selected_images_, false);
    updateSelectTalkerEnabled();
    refreshPreview();
    refreshWarning();
}

void MessageDialog::refDataVersion()
{
    disposeSingleLine();
    AbstractMessageDialog::refDataVersion();
}

void MessageDialog::disposeSingleLine()
{
    if (!tabs_) return;
    const bool legacy = context_.summary && context_.summary->legacy();
    if (!singleLine() && tabs_->pages.size() == 3U && legacy) {
        auto pages = tabs_->pages;
        pages.erase(pages.begin() + 2);
        (void)lib::replace_tab_pages(tabs_, std::move(pages),
                                     tabs_->selected_index);
    } else if (!legacy && tabs_->pages.size() == 2U) {
        createSingleLineTab();
    }
}

void MessageDialog::createSingleLineTab()
{
    if (!tabs_) return;
    if (tabs_->pages.size() < 3 && single_line_page_) {
        auto pages = tabs_->pages;
        pages.push_back({
            context_.props ? context_.props->requiredMsgs().singleLineMessage.value
                           : String{"単行メッセージ"},
            0, single_line_page_, false});
        (void)lib::replace_tab_pages(tabs_, std::move(pages),
                                     tabs_->selected_index);
    }
}

void MessageDialog::refreshWarning()
{
    warning_cache_.clear();
    if (context_.props && singleLine() &&
        !context_.props->isTargetVersion(context_.summary, "5")) {
        warning_cache_.push_back(
            context_.props->requiredMsgs().warningSingleLineMessage.value);
    }
    if (image_select_) {
        const auto image_warnings = image_select_->warnings();
        warning_cache_.insert(warning_cache_.end(), image_warnings.begin(),
                              image_warnings.end());
    }
    // C++版の意図的な拡張として、D版の警告に加
    // え、 実行時の話者、一行表示での切り詰め、
    // 空のメッセージに関する補足警告を維持する。
    for (const auto& image : selectedTalkerParam()) {
        if (!image) continue;
        if (image->type == types::CardImageType::PCNumber) {
            warning_cache_.push_back(
                "指定したPC番号が実行時に存在しない場合、話者画像は表示されません。");
        } else if (image->type == types::CardImageType::Talker &&
                   image->talker() == types::Talker::Unselected) {
            warning_cache_.push_back(
                "選択外メンバが存在しない場合、このコンテントは実行されません。");
        } else if (image->type == types::CardImageType::Talker &&
                   image->talker() == types::Talker::Card) {
            warning_cache_.push_back(
                "使用中カードが存在しない場合、このコンテントは実行されません。");
        }
    }
    std::vector<String> flags;
    std::vector<String> steps;
    std::vector<String> variants;
    std::vector<String> fonts;
    std::vector<char> colors;
    const auto warning_text = text();
    const auto token_text = ::cwxeditor::cwx::utils::lastRet(
        dutils::wrapReturnCode(warning_text));
    msgutils::textUseItems(token_text, flags, steps, variants, fonts, colors);
    lib::Dictionary<String, bool> wf;
    lib::Dictionary<String, bool> ws;
    lib::Dictionary<String, bool> wv;
    lib::Dictionary<String, bool> wfont;
    lib::Dictionary<char, bool> wc;
    const auto tw = textWarnings(warning_text, flags, steps, variants, fonts, colors,
                                 wf, ws, wv, wfont, wc);
    warning_cache_.insert(warning_cache_.end(), tw.all.begin(), tw.all.end());
    const auto base_warnings = warnings();
    warning_cache_.insert(warning_cache_.end(), base_warnings.begin(),
                          base_warnings.end());
    if (singleLine() && text_.find('\n') != String::npos) {
        warning_cache_.push_back("単行メッセージとして保存すると2行目以降は保存されません。");
    }
    const auto named_selections = event_
        ? static_cast<std::size_t>(std::count_if(
              event_->next().begin(), event_->next().end(),
              [](const auto& child) { return child && !child->name().empty(); }))
        : 0U;
    if (normalizeMessage(text_).empty() && named_selections <= 1U) {
        warning_cache_.push_back(
            "本文と有効な選択肢が無いため、このコンテントは表示されません。");
    }
    // D版と同様、組み立てた順に警告を表示し、重複も保持する。
    publishWarnings();
}

bool MessageDialog::hasCharacterTalker() const
{
    for (const auto& path : selectedTalkerParam()) {
        if (path && path->type == types::CardImageType::Talker &&
            path->talker() != types::Talker::Card) {
            return true;
        }
    }
    return false;
}

bool MessageDialog::singleLine() const
{
    return selected_tab_ == 2;
}

void MessageDialog::tabChanged(std::size_t tab)
{
    if (!tabs_ || tab >= tabs_->pages.size()) return;
    const auto previous = selected_tab_;
    selected_tab_ = tab;
    (void)lib::select_tab_page(*tabs_, selected_tab_);
    settings_.single_line = selected_tab_ == 2;
    if (tab_text_editors_[selected_tab_]) {
        text_editor_ = tab_text_editors_[selected_tab_];
    }
    refDataVersion();
    refreshPreview();
    if (selected_tab_ != previous) notifyChanged();
}

void MessageDialog::put(char32_t value)
{
    if (text_editor_) {
        putColor(*text_editor_, value);
    } else {
        putColor(text_, value);
        refreshPreview();
    }
}

void SpeakDialog::refreshHoveredDialogWarning() {
    if (!dialog_table_) return;
    String tooltip;
    if (hovered_dialog_ && *hovered_dialog_ < dialog_warnings_.size()) {
        for (const auto& warning : dialog_warnings_[*hovered_dialog_]) {
            if (!tooltip.empty()) tooltip.push_back('\n');
            tooltip += warning;
        }
    }
    dialog_table_->set_tooltip(std::move(tooltip));
}

void MessageDialog::insert(StringView value)
{
    if (text_editor_) {
        // 元のエディタの Modify 経路を通し、共通のテキスト履歴に、
        // ボタンによる挿入をネイティブの文字入力と同じように記録する。
        const auto editor = text_editor_;
        (void)lib::replace_text_selection(*editor, value);
    } else {
        text_.append(value);
        refreshPreview();
    }
}

void MessageDialog::refSkin()
{
    if (!context_.props) return;
    const auto active_skin = summSkin();
    for (std::size_t i = 0; i < tab_text_editors_.size(); ++i) {
        applyMessageEditorStyle(context_, active_skin, tab_text_editors_[i], i == 0U);
    }
}

CardImageList MessageDialog::selectedTalkerParam() const
{
    return selected_tab_ == 0 ? selected_images_ : CardImageList{};
}

CardImageList MessageDialog::imgPaths() const
{
    return selectedTalkerParam();
}

String MessageDialog::text() const
{
    return normalizeMessage(text_);
}

void MessageDialog::text(String value)
{
    if (!tabs_) {
        AbstractMessageDialog::text(std::move(value));
        return;
    }
    // プログラムによる置換では、一行ページが選択中
    // でも完全な正規の値を渡す。 見えない末尾を保
    // 持するのは、そのページでのユーザー編集だけ。
    syncTextEditors(value, tab_text_editors_.size());
}

bool MessageDialog::openCWXPath(StringView path, bool shell_activate)
{
    // D版と同様、末端のフォーカスは所有ウィンドウの経路に任せる。
    (void)shell_activate;
    namespace cwx_path = ::cwxeditor::cwx::path;
    const auto category = cwx_path::cpcategory(path);
    if (category == "text")
        return cwx_path::cpempty(cwx_path::cpbottom(path));
    return cwx_path::cpempty(path);
}

void MessageDialog::syncTextEditors(StringView value, std::size_t source)
{
    if (syncing_text_) return;
    syncing_text_ = true;
    const auto normalized = normalizeMessage(String(value));
    if (source == 2U) {
        const auto newline = text_.find('\n');
        const auto tail = newline == String::npos ? String{} : text_.substr(newline);
        text_ = firstLine(normalized) + tail;
    } else {
        text_ = normalized;
    }
    for (std::size_t i = 0; i < tab_text_editors_.size(); ++i) {
        if (tab_text_editors_[i]) {
            tab_text_editors_[i]->text = i == 2 ? firstLine(text_) : text_;
            // 操作を記録するのは入力元のエディタだけ。 他のタブも、
            // 次の編集に備えて以前の値のスナップショットを更新する。
            if (i != source && text_menus_[i]) text_menus_[i]->modifyText();
        }
    }
    syncing_text_ = false;
    notifyChanged();
    refreshPreview();
    refreshWarning();
}

void MessageDialog::refUndoMax()
{
    AbstractMessageDialog::refUndoMax();
    if (text_history_) text_history_->max(context_.props
        ? static_cast<std::size_t>(std::max<std::int32_t>(0, context_.props->var().undoMaxEtc)) : 128U);
}

void MessageDialog::refreshSelectedImageControls()
{
    if (image_select_) {
        selected_images_ = image_select_->cardImages();
        image_paths_ = selected_images_;
        updateSelectTalkerEnabled();
        return;
    }
    if (!image_source_combo_) return;
    std::size_t source{};
    if (!selected_images_.empty() && selected_images_.front()) {
        const auto& image = *selected_images_.front();
        if (image.type == types::CardImageType::Talker) {
            source = image.talker() == types::Talker::Selected ? 1
                   : image.talker() == types::Talker::Unselected ? 2
                   : image.talker() == types::Talker::Random ? 3 : 4;
        }
        if (image_path_editor_) {
            image_path_editor_->text = image.type == types::CardImageType::File
                                           ? image.path()
                                           : String{};
        }
        if (selected_image_view_) {
            selected_image_view_->image = 0;
            selected_image_data_ = {};
            if (image.type == types::CardImageType::File && !image.path().empty()) {
                selected_image_data_ = decodeImage(
                    context_, image.path(),
                    context_.props ? scaleOrOne(*context_.props) : 1);
                selected_image_view_->image = selected_image_data_.resource();
            }
        }
    }
    auto source_items = image_source_combo_->items;
    (void)lib::replace_combo_items(image_source_combo_, std::move(source_items),
                                   source, image_source_combo_->edit_text);
    if (image_path_editor_) image_path_editor_->set_enabled(source == 0);
    image_paths_ = selected_images_;
    updateSelectTalkerEnabled();
}

std::shared_ptr<lib::Panel> MessageDialog::setup(
    const std::shared_ptr<lib::Container>& area, const bool combine_preview)
{
    auto panel = AbstractMessageDialog::setup();
    if (wsn_settings_panel_) panel->remove(wsn_settings_panel_);
    if (preview_toggle_) panel->remove(preview_toggle_);
    if (preview_image_view_) panel->remove(preview_image_view_);
    if (preview_values_view_) panel->remove(preview_values_view_);
    tabs_ = std::make_shared<lib::TabView>();

    auto image_page = std::make_shared<lib::Panel>();
    auto image_layout = std::make_shared<lib::GridLayout>();
    image_layout->columns = 2;
    image_layout->spacing = 6.0F;
    image_layout->padding = {5.0F, 5.0F, 5.0F, 5.0F};
    image_page->set_layout(image_layout);
    if (context_.props) {
        const auto size = context_.props->looks().cardSize();
        const auto width = context_.props->s(static_cast<std::int32_t>(size.width));
        const auto height = context_.props->s(static_cast<std::int32_t>(size.height));
        const auto definitions = [this](bool) {
            const auto& messages = context_.props->requiredMsgs();
            return std::vector<String>{
                messages.defaultSelection(messages.talkerName(types::Talker::Selected)),
                messages.defaultSelection(messages.talkerName(types::Talker::Unselected)),
                messages.defaultSelection(messages.talkerName(types::Talker::Random)),
                messages.defaultSelection(messages.talkerName(types::Talker::Card))};
        };
        image_select_ = std::make_unique<imageselect::ImageSelectBase>(
            imageselect::ImageSelectKind::Card, false, image_page, 0,
            *context_.props,
            context_.summary_owner ? context_.summary_owner.get()
                                   : const_cast<Summary*>(context_.summary),
            width, height,
            structs::CInsets{}, types::CardImagePosition::TopLeft, false,
            [] { return String{}; }, lib::Action{}, definitions, false,
            summSkin(), context_.services, true);
        image_select_->bindEditorCommons(context_.editor_commons);
        image_select_->createDefaultItem = [] {
            return std::make_shared<CardImage>(types::Talker::Selected);
        };
        image_select_->valueFromDef(
            [](int index, bool, StringView) -> CardImagePtr {
                switch (index) {
                case 0: return std::make_shared<CardImage>(types::Talker::Selected);
                case 1: return std::make_shared<CardImage>(types::Talker::Unselected);
                case 2: return std::make_shared<CardImage>(types::Talker::Random);
                case 3: return std::make_shared<CardImage>(types::Talker::Card);
                default:
                    return std::make_shared<CardImage>(
                        String{}, types::CardImagePosition::Default);
                }
            });
        image_select_->valueToDef(
            [](const CardImage& image, bool) {
                switch (image.type) {
                case types::CardImageType::File:
                    return image.path().empty() ? 0 : -1;
                case types::CardImageType::PCNumber:
                    return -1;
                case types::CardImageType::Talker:
                    switch (image.talker()) {
                    case types::Talker::Selected: return 0;
                    case types::Talker::Unselected: return 1;
                    case types::Talker::Random: return 2;
                    case types::Talker::Card: return 3;
                    case types::Talker::Valued: return -1;
                    }
                }
                return -1;
            });
        image_select_->setCardImages(
            selected_images_.empty()
                ? CardImageList{std::make_shared<CardImage>(types::Talker::Selected)}
                : selected_images_,
            false);
        image_select_->modEvent.push_back([this] {
            // ImageSelect は変更通知の直後に updateImageEvent
            // を通知する。 ここではモデルの変更状態だけを
            // 扱い、その一回の画像通知に表示更新を任せる。
            notifyChanged();
        });
        image_select_->updateImageEvent.push_back([this] {
            refreshSelectedImageControls();
            refreshPreview();
            refreshWarning();
        });
        image_select_->widget()->set_layout_item(lib::LayoutItem{
             0, 0, 1, 1, lib::Align::Stretch, lib::Align::Stretch,
            {281, 190}, {291, 230}, {}, 0.0F});
    } else {
        auto image_controls = std::make_shared<lib::GroupBox>();
        image_controls->title = "メッセージ画像";
        auto controls_layout = std::make_shared<lib::LinearLayout>();
        controls_layout->spacing = 4.0F;
        image_controls->set_layout(controls_layout);
        image_source_combo_ = std::make_shared<lib::ComboBox>();
        image_source_combo_->items = {
            {"ファイルを選択", "file", 0, true},
            {"選択中のメンバ", "selected", 0, true},
            {"選択中以外のメンバ", "unselected", 0, true},
            {"ランダムメンバ", "random", 0, true},
            {"カード使用者", "card", 0, true}};
        image_source_combo_->on_selection_changed =
            [this](std::optional<std::size_t> index) {
                if (!index) return;
                if (*index == 0) {
                    selected_images_ = {std::make_shared<CardImage>(
                        image_path_editor_ ? image_path_editor_->text : String{})};
                } else {
                    const auto talker = *index == 1 ? types::Talker::Selected
                                      : *index == 2 ? types::Talker::Unselected
                                      : *index == 3 ? types::Talker::Random
                                                    : types::Talker::Card;
                    selected_images_ = {std::make_shared<CardImage>(talker)};
                }
                refreshSelectedImageControls();
                refreshPreview();
                refreshWarning();
                notifyChanged();
            };
        image_controls->add(image_source_combo_);
        image_path_editor_ = std::make_shared<lib::TextEdit>();
        image_path_editor_->on_changed = [this](StringView value) {
            if (image_source_combo_ &&
                image_source_combo_->selected_index.value_or(0) != 0)
                return;
            selected_images_ = {std::make_shared<CardImage>(String(value))};
            image_paths_ = selected_images_;
            refreshPreview();
            refreshWarning();
            notifyChanged();
        };
        image_controls->add(image_path_editor_);
        selected_image_view_ = std::make_shared<lib::ImageView>();
        selected_image_view_->set_layout_item(lib::LayoutItem{
            0, 0, 1, 1, lib::Align::Stretch, lib::Align::Stretch,
            {160, 100}, {220, 140}, {}, 1.0F});
        image_controls->add(selected_image_view_);
        image_controls->set_layout_item(lib::LayoutItem{
            0, 0, 1, 1, lib::Align::Stretch, lib::Align::Stretch,
            {190, 180}, {250, 240}, {}, 1.0F});
        image_page->add(image_controls);
    }

    text_history_ = std::make_shared<undo::UndoManager>(context_.props
        ? static_cast<std::size_t>(std::max<std::int32_t>(0, context_.props->var().undoMaxEtc)) : 128U);
    const auto create_editor = [this](std::size_t index, bool multiline) {
        auto editor = std::make_shared<lib::TextEdit>(index == 2 ? firstLine(text_) : text_);
        editor->multiline = multiline;
        editor->wrap = true;
        // 幅と高さは、配置時に現在の本文フォント、D版の
        // 桁数、行数、バックエンドの余白から計測する。
        editor->set_layout_item({.horizontal = lib::Align::Center,
                                 .vertical = lib::Align::Center});
        editor->on_changed = [this, index](StringView value) {
            syncTextEditors(value, index);
        };
        applyMessageEditorStyle(context_, summSkin(), editor, index == 0U);
        tab_text_editors_[index] = editor;
        dmenu::Commons comm;
        comm.services = context_.services;
        if (context_.props) {
            comm.presentation.menuText = [prop = context_.props](auto id) { return prop->buildMenu(id); };
            comm.presentation.menuImage = [prop = context_.props](auto id) { return prop->images().menu(id); };
        }
        auto text_menu = dmenu::createTextMenu(comm, dmenu::TMM(editor),
            [this] { return !syncing_text_; }, text_history_);
        text_menus_[index] = text_menu;
        (void)setupSPCharsMenu(context_, summary(), [this] { return summSkin(); },
            context_.use_counter, text_menu->menu(), true, true, [] { return true; },
            [weak = std::weak_ptr<dmenu::TextMenuModify>(text_menu)](StringView value) {
                if (auto owner = weak.lock()) {
                    const auto target = owner->text().textWidget();
                    if (!target) return;
                    if (value.size() == 2 && value.front() == '&') putColor(*target, value[1]);
                    else {
                        auto text = owner->text();
                        const auto start = std::min(target->selection_start, target->text.size());
                        const auto length = std::min(target->selection_length, target->text.size() - start);
                        auto next = target->text;
                        next.replace(start, length, value);
                        text.setText(next);
                        text.setSelection({static_cast<float>(start + value.size()), static_cast<float>(start + value.size())});
                    }
                }
            });
        return editor;
    };
    const auto make_centered_editor_page = [&create_editor](const std::size_t index,
                                                            const bool multiline) {
        auto page = std::make_shared<lib::Panel>();
        // D版は CenterLayout を使うが、共通バックエンドはDWTの
        // 派生クラスではなく標準の LayoutKind を扱う。 同じ中央配
        // 置を、ネイティブの実体を持たない Spacer の3×3グリッドで表
        // す。 Panel ではなく Spacer を使い、タブを素早く切り替
        // えてもプレビューや値の操作欄の上に HWND が残らないようにする。
        auto grid = std::make_shared<lib::GridLayout>();
        grid->columns = 3;
        page->set_layout(grid);

        const auto add_spacer = [&page](std::size_t row, std::size_t column,
                                        float grow, bool horizontal) {
            auto spacer = std::make_shared<lib::Spacer>();
            spacer->set_layout_item(lib::LayoutItem{
                row, column, 1, 1,
                horizontal ? lib::Align::Stretch : lib::Align::Center,
                horizontal ? lib::Align::Center : lib::Align::Stretch,
                {}, {}, {}, grow});
            page->add(spacer);
        };
        add_spacer(0, 1, 1.0F, false);
        add_spacer(1, 0, 1.0F, true);
        auto editor = create_editor(index, multiline);
        editor->set_layout_item(lib::LayoutItem{
            1, 1, 1, 1, lib::Align::Center, lib::Align::Center,
            editor->layout_item().minimum, editor->layout_item().preferred,
            {}, 0.0F});
        page->add(editor);
        add_spacer(1, 2, 1.0F, true);
        add_spacer(2, 1, 1.0F, false);
        return page;
    };
    auto image_text_host = make_centered_editor_page(0, true);
    image_text_host->set_layout_item(lib::LayoutItem{
        0, 1, 1, 1, lib::Align::Stretch, lib::Align::Stretch,
        {}, {}, {}, 1.0F});
    image_page->add(image_text_host);
    auto no_image_page = make_centered_editor_page(1, true);
    single_line_page_ = make_centered_editor_page(2, false);

    std::vector<lib::TabPage> initial_pages;
    initial_pages.reserve(2U);
    initial_pages.push_back({
        context_.props ? context_.props->requiredMsgs().imageMessage.value
                       : String{"イメージ付きメッセージ"},
        0, image_page, false});
    initial_pages.push_back({
        context_.props ? context_.props->requiredMsgs().noImageMessage.value
                       : String{"イメージ無しメッセージ"},
        0, no_image_page, false});
    // 初期ページも共通の一括反映処理を通す。 任意
    // の一行ページと初期選択ページも、後のタブ再構
    // 築と同じ順序・見出し表示の世代で管理する。
    (void)lib::replace_tab_pages(tabs_, std::move(initial_pages), {});
    if (!(context_.summary && context_.summary->legacy()) || selected_tab_ == 2U)
        createSingleLineTab();
    (void)lib::select_tab_page(*tabs_, selected_tab_, false);
    tabs_->set_layout_item(lib::LayoutItem{
        0, 0, 1, 1, lib::Align::Stretch, lib::Align::Stretch,
        {0, 205}, {0, 235}, {}, 1.0F});
    tabs_->on_selection_changed = [this](std::optional<std::size_t> index) {
        if (index) tabChanged(*index);
    };
    auto editor_column = std::make_shared<lib::Panel>();
    auto editor_layout = std::make_shared<lib::LinearLayout>();
    // SWTのメッセージ編集のボタン欄は、高さ24pxのコンパクトな行
    // 。 共通の LinearLayout に汎用の5px間隔を残す
    // と約30px余計に使い、三つのメッセージペインを上へ圧迫する。
    editor_layout->spacing = 2.0F;
    editor_column->set_layout(editor_layout);
    editor_container_ = editor_column;
    editor_column->add(tabs_);
    // ALL ではセリフダイアログと同様に一桁のプレイヤー番号
    // 欄を詰めて配置し、 参照欄より列が広がらないようにする
    // 。二つの文字アイコン行も、セリフと同じ1.3倍にする。
    const bool all_message = lib::editor_window_host_options().all_mode;
    const float char_icon_scale = all_message ? kAllSpeechToolScale : 1.0F;
    (void)createSCharBar(
        context_, summary(), editor_column,
        [this](StringView value) { insert(value); },
        [this](char32_t value) { put(value); }, context_.props,
        all_message, char_icon_scale);
    (void)createSkinSCharBar(
        context_, editor_column, [this](StringView value) { insert(value); },
        [this] { return summSkin(); }, 1U, char_icon_scale);
    (void)createFlagStepBar(
        editor_column, [this](StringView value) { insert(value); }, context_,
        context_.props, summary(), context_.use_counter, context_.forced_skin,
        true);
    if (wsn_settings_panel_) editor_column->add(wsn_settings_panel_);

    if (preview_image_view_ && preview_values_view_) {
        auto preview_column = std::make_shared<lib::Panel>();
        preview_container_ = preview_column;
        auto preview_layout = std::make_shared<lib::GridLayout>();
        preview_layout->columns = 1;
        preview_layout->spacing = 2.0F;
        preview_layout->padding = {};
        preview_column->set_layout(preview_layout);
        preview_image_view_->set_layout_item(lib::LayoutItem{0, 0, 1, 1,
            lib::Align::Stretch, lib::Align::Stretch,
            {420, 150}, {470, 180}, {}, 0.0F});
        preview_values_view_->set_layout_item(lib::LayoutItem{1, 0, 1, 1,
            lib::Align::Stretch, lib::Align::Stretch,
            {420, 175}, {470, 210}, {}, 1.0F});
        preview_column->add(preview_image_view_);
        preview_column->add(preview_values_view_);
        // 意図的な変更(D版と異なる): スペース節約のため、右側プレビューの
        // 左右余白をメッセージウィンドウ幅(470)の両側1pxずつにする。
        const float preview_extent = static_cast<float>(
            context_.props ? context_.props->looks().messageBounds().width : 470) + 2.0F;
        preview_column->set_layout_item(lib::LayoutItem{
            0, 0, 1, 1, lib::Align::Stretch, lib::Align::Stretch,
            {preview_extent, 320.0F}, {preview_extent, 400.0F}, {}, 1.0F});
        if (combine_preview) {
            auto main_split = std::make_shared<lib::SplitView>();
            main_split->orientation = lib::Orientation::Horizontal;
            main_split->weights = {0.59F, 0.41F};
            main_split->initial_second_extent = preview_extent;
            main_split->add(editor_column);
            main_split->add(preview_column);
            panel->add(main_split);
        }
    } else {
        preview_container_.reset();
        if (combine_preview) panel->add(editor_column);
    }
    auto result = combine_preview ? panel : editor_column;
    if (area) area->add(result);
    refreshSelectedImageControls();
    refSkin();
    tabChanged(selected_tab_);
    refDataVersion();
    // 派生ペインの生成後に、D版の初期プレビュー表示を適用する。 変える
    // のは配置だけであり、opened() で再びラスタライズしない。
    if (!preview_window_)
        setPreviewLayoutData(preview_toggle_ && preview_toggle_->checked, false);
    return result;
}

bool MessageDialog::apply()
{
    if (!event_) return false;
    // text() は共有バッファ全体を公開し、D版に合わせた切り詰めは適用時だけに行う。
    const auto editable_text = text();
    // D版と同様、空でない一行入力は保持する。 空のバ
    // ッファは、末尾LFの正規化経路を通して反映する。
    const auto value = singleLine() && !editable_text.empty()
        ? firstLine(editable_text)
        : ::cwxeditor::cwx::utils::lastRet(editable_text);
    event_->text(value);
    CardImageList images;
    for (const auto& image : selectedTalkerParam()) {
        if (image) images.push_back(std::make_shared<CardImage>(nullptr, *image));
    }
    event_->cardImages(std::move(images));
    event_->selectionColumns(settings_.selection_columns);
    event_->centeringX(settings_.center_x);
    event_->centeringY(settings_.center_y);
    event_->boundaryCheck(settings_.boundary_check);
    event_->selectTalker(settings_.select_talker);
    event_->singleLine(singleLine());
    notifyChanged();
    return true;
}

std::shared_ptr<lib::Panel> createTalkerPane2(
    const std::shared_ptr<lib::Container>& parent, const MessageContext& context,
    const Summary* summ, const usecounter::UseCounter* uc,
    std::span<const String> coupons, std::shared_ptr<lib::TextEdit>& coupon_list,
    std::shared_ptr<lib::ComboBox>& coupon_combo)
{
    (void)context;
    (void)summ;
    (void)uc;
    auto panel = std::make_shared<lib::Panel>();
    coupon_list = std::make_shared<lib::TextEdit>();
    coupon_list->multiline = true;
    for (const auto& coupon : coupons) {
        if (!coupon_list->text.empty()) coupon_list->text.push_back('\n');
        coupon_list->text += coupon;
    }
    coupon_combo = std::make_shared<lib::ComboBox>();
    coupon_combo->editable = true;
    for (const auto& coupon : coupons) {
        coupon_combo->items.push_back({coupon, coupon, 0, true});
    }
    panel->add(coupon_list);
    panel->add(coupon_combo);
    if (parent) parent->add(panel);
    return panel;
}

std::shared_ptr<lib::Panel> createTalkerPane(
    const std::shared_ptr<lib::Container>& parent, const MessageContext& context,
    const Summary* summ, const CardImageList& paths,
    std::shared_ptr<lib::ImageView>& image_select)
{
    (void)summ;
    auto panel = std::make_shared<lib::Panel>();
    image_select = std::make_shared<lib::ImageView>();
    if (!paths.empty() && paths.front()) {
        const auto image = decodeImage(context, paths.front()->path(),
                                       context.props ? scaleOrOne(*context.props) : 1);
        image_select->image = image.resource();
    }
    panel->add(image_select);
    if (parent) parent->add(panel);
    return panel;
}

std::shared_ptr<lib::TextEdit> createMessagePane(
    const MessageContext& context, bool image,
    const std::shared_ptr<lib::Container>& parent, const Summary* summ,
    const Skin::Ptr& active_skin)
{
    (void)summ;
    auto text = std::make_shared<lib::TextEdit>();
    text->multiline = true;
    text->wrap = true;
    if (context.props) {
        applyMessageEditorStyle(context, active_skin, text, image);
        // messageLen/messageImageLen は配置の幅であり、入力の上限ではない。
        text->max_length = 0U;
    }
    if (parent) parent->add(text);
    return text;
}

PutC::PutC(std::function<void(StringView)> insert, String value)
    : insert_(std::move(insert)), value_([value = std::move(value)] { return value; })
{
}

PutC::PutC(std::function<void(StringView)> insert, std::function<String()> value)
    : insert_(std::move(insert)), value_(std::move(value))
{
}

void PutC::put() const
{
    if (insert_ && value_) insert_(value_());
}

PutColor::PutColor(std::function<void(char32_t)> put, char32_t color)
    : put_(std::move(put)), color_(color)
{
}

void PutColor::put() const
{
    if (put_) put_(color_);
}

std::shared_ptr<lib::ToolBar> createSCharBar(
    const MessageContext& context, const Summary* summ,
    const std::shared_ptr<lib::Container>& parent,
    std::function<void(StringView)> insert, std::function<void(char32_t)> put_color,
    const Props* prop, const bool compact_player, const float icon_scale)
{
    // 拡大したアイコンには標準の24pxより高い行が必要。 高さ指
    // 定をゼロにして、拡大後の欄が自身の高さを計測するようにする。
    const float strip_height = icon_scale > 1.0F ? 0.0F : 24.0F;
    auto row = std::make_shared<lib::Panel>();
    auto row_layout = std::make_shared<lib::GridLayout>();
    row_layout->columns = 2;
    row_layout->spacing = 3.0F;
    row->set_layout(row_layout);
    row->set_layout_item(lib::LayoutItem{
        0, 0, 1, 2, lib::Align::Stretch, lib::Align::Center,
        {0.0F, strip_height}, {0.0F, strip_height}, {}, 0.0F});
    auto bar = std::make_shared<lib::ToolBar>();
    bar->keyboard_navigation = !lib::editor_window_host_options().all_mode;
    bar->icon_scale = std::max(1.0F, icon_scale);
    bar->set_layout_item(lib::LayoutItem{
        0, 0, 1, 1, lib::Align::Stretch, lib::Align::Center,
        {0.0F, strip_height}, {0.0F, strip_height}, {}, 1.0F});
    row->add(bar);
    const auto icon = [prop](StringView filename) {
        return prop ? const_cast<Props*>(prop)->images().load(filename)
                    : lib::ResourceId{};
    };
    const auto color_text = [prop](const char value) -> String {
        if (!prop) return itemCommand("&", value);
        switch (value) {
        case 'W': return prop->requiredMsgs().colorW.value;
        case 'R': return prop->requiredMsgs().colorR.value;
        case 'B': return prop->requiredMsgs().colorB.value;
        case 'G': return prop->requiredMsgs().colorG.value;
        case 'Y': return prop->requiredMsgs().colorY.value;
        case 'O': return prop->requiredMsgs().colorO.value;
        case 'P': return prop->requiredMsgs().colorP.value;
        case 'L': return prop->requiredMsgs().colorL.value;
        case 'D': return prop->requiredMsgs().colorD.value;
        default: return itemCommand("&", value);
        }
    };
    for (const auto value : std::array<char, 9>{'W', 'R', 'B', 'G', 'Y',
                                                 'O', 'P', 'L', 'D'}) {
        const auto command = itemCommand("&", value);
        bar->items.push_back({color_text(value), command,
                              icon(String("cc_") +
                                   static_cast<char>(std::tolower(value)) + ".png"),
                              true, false, false, false});
    }
    lib::ToolItem separator;
    separator.separator = true;
    bar->items.push_back(separator);
    const auto special_text = [prop](char value) -> String {
        if (!prop) return String("#") + value;
        switch (value) {
        case 'I': return prop->requiredMsgs().scRef.value;
        case 'M': return prop->requiredMsgs().scTalkerName(types::Talker::Selected);
        case 'U': return prop->requiredMsgs().scTalkerName(types::Talker::Unselected);
        case 'R': return prop->requiredMsgs().scTalkerName(types::Talker::Random);
        case 'C': return prop->requiredMsgs().scTalkerName(types::Talker::Card);
        case 'T': return prop->requiredMsgs().scTeam.value;
        case 'Y': return prop->requiredMsgs().scYado.value;
        default: return String("#") + value;
        }
    };
    const auto special_icon = [&icon](char value) {
        return icon(String("sc_") +
                    static_cast<char>(std::tolower(value)) + ".png");
    };
    for (const char value : std::array<char, 7>{'I', 'M', 'U', 'R', 'C', 'T', 'Y'}) {
        const auto command = String("#") + value;
        bar->items.push_back({special_text(value), command, special_icon(value),
                              true, false, false, false});
    }
    bar->items.push_back(separator);
    const auto player_number = createPCSPCharBar(context, summ, bar, insert, row);
    if (compact_player) {
        // プレイヤー番号の数値欄は一桁分とし、最後のア
        // イコンのすぐ後に、伸縮する空白なしで置く。
        auto bar_item = bar->layout_item();
        bar_item.horizontal = lib::Align::Start;
        bar_item.grow = 0.0F;
        bar->set_layout_item(bar_item);
        if (player_number) {
            player_number->input_max_length = 1U;
            auto item = player_number->layout_item();
            item.horizontal = lib::Align::Start;
            item.minimum.width = 30.0F;
            item.preferred.width = 30.0F;
            player_number->set_layout_item(item);
        }
    }
    bar->on_command = [insert = std::move(insert), put_color = std::move(put_color),
                       player_number, prop](StringView command) {
        if (command.size() == 2 && command.front() == '&') {
            if (put_color) put_color(static_cast<unsigned char>(command[1]));
        } else if (command == "@player-card-name") {
            if (!insert) return;
            const auto number = player_number
                ? static_cast<std::uint32_t>(std::max(
                      1.0, std::round(player_number->value)))
                : 1U;
            insert(prop ? String("$") + prop->sys().playerCardName(number) + "$"
                        : String("$Player") + std::to_string(number) + "$");
        } else if (insert) {
            insert(command);
        }
    };
    if (parent) parent->add(row);
    return bar;
}

std::shared_ptr<lib::ToolBar> createSimpleSCharBar(
    const std::shared_ptr<lib::Container>& parent,
    std::function<void(StringView)> insert, const MessageContext& context,
    const Props* prop, const Summary* summ)
{
    auto row = std::make_shared<lib::Panel>();
    auto row_layout = std::make_shared<lib::GridLayout>();
    row_layout->columns = 2;
    row_layout->spacing = 3.0F;
    row->set_layout(row_layout);
    row->set_layout_item(lib::LayoutItem{
        0, 0, 1, 2, lib::Align::Stretch, lib::Align::Center, {}, {}, {}, 0.0F});
    auto bar = std::make_shared<lib::ToolBar>();
    bar->set_layout_item(lib::LayoutItem{
        0, 0, 1, 1, lib::Align::Stretch, lib::Align::Center, {}, {}, {}, 1.0F});
    // D版と同様、この単純なボタン欄でもフォーカス移動と KeyDown を許可する。
    // 配置は変えず、共通のツールバーのフォーカス・キーボード操作の方針を適用する。
    bar->keyboard_navigation = true;
    row->add(bar);
    const auto icon = [prop](StringView filename) {
        return prop ? const_cast<Props*>(prop)->images().load(filename)
                    : lib::ResourceId{};
    };
    const auto text = [prop](char value) -> String {
        if (!prop) return String("#") + value;
        switch (value) {
        case 'M': return prop->requiredMsgs().scTalkerName(types::Talker::Selected);
        case 'U': return prop->requiredMsgs().scTalkerName(types::Talker::Unselected);
        case 'R': return prop->requiredMsgs().scTalkerName(types::Talker::Random);
        case 'T': return prop->requiredMsgs().scTeam.value;
        case 'Y': return prop->requiredMsgs().scYado.value;
        default: return String("#") + value;
        }
    };
    for (const char value : std::array<char, 5>{'M', 'U', 'R', 'T', 'Y'}) {
        const auto command = String("#") + value;
        bar->items.push_back({text(value), command,
                              icon(String("sc_") +
                                   static_cast<char>(std::tolower(value)) + ".png"),
                              true, false, false, false});
    }
    lib::ToolItem separator;
    separator.separator = true;
    bar->items.push_back(separator);
    const auto player_number = createPCSPCharBar(context, summ, bar, insert, row);
    bar->on_command = [insert = std::move(insert), player_number, prop](
                          StringView command) {
        if (!insert) return;
        if (command == "@player-card-name") {
            const auto number = player_number
                ? static_cast<std::uint32_t>(std::max(
                      1.0, std::round(player_number->value)))
                : 1U;
            insert(prop ? String("$") + prop->sys().playerCardName(number) + "$"
                        : String("$Player") + std::to_string(number) + "$");
        } else {
            insert(command);
        }
    };
    if (parent) parent->add(row);
    return bar;
}

std::shared_ptr<lib::NumberEdit> createPCSPCharBar(
    const MessageContext& context, const Summary* summ,
    const std::shared_ptr<lib::ToolBar>& bar,
    std::function<void(StringView)> insert,
    const std::shared_ptr<lib::Container>& control_parent)
{
    if (!bar) return {};
    (void)insert;
    auto* prop = context.props;
    const bool enabled = prop && prop->isTargetVersion(summ, "2");
    const auto selected_command = prop
        ? String("$") + prop->sys().selectedPlayerCardNumber() + "$"
        : String{"$SelectedPlayer$"};
    bar->items.push_back({
        prop ? prop->requiredMsgs().selectedPlayerCardNumber.value
             : String{"選択メンバ番号"},
        selected_command,
        prop ? const_cast<Props*>(prop)->images().load(
                   StringView{"sc_sel_pc_num.png"})
             : lib::ResourceId{},
        enabled, false, false, false});
    const auto party_max = prop ? std::max<std::uint32_t>(
                                      1U, static_cast<std::uint32_t>(
                                              prop->looks().partyMax()))
                                : 6U;
    const auto configured = prop
        ? std::clamp<std::uint32_t>(prop->var().selectedPlayerCardName.value,
                                    1U, party_max)
        : 1U;
    const auto player_index = bar->items.size();
    bar->items.push_back({
        prop ? utils::tryFormat(prop->requiredMsgs().playerCardName.value, configured)
             : String{"プレイヤーカード名"},
        "@player-card-name",
        prop ? const_cast<Props*>(prop)->images().load(
                   StringView{"sc_pc_name.png"})
              : lib::ResourceId{},
        enabled, false, false, false});

    auto spinner = std::make_shared<lib::NumberEdit>();
    spinner->minimum = 1.0;
    spinner->maximum = static_cast<double>(party_max);
    spinner->step = 1.0;
    spinner->decimals = 0;
    spinner->value = static_cast<double>(configured);
    spinner->set_enabled(enabled);
    spinner->accessibility_name = prop ? prop->requiredMsgs().playerCardName.value
                                       : String{"プレイヤーカード番号"};
    spinner->set_layout_item(lib::LayoutItem{
        0, 1, 1, 1, lib::Align::End, lib::Align::Center,
        {52.0F, 24.0F}, {56.0F, 24.0F}});
    const auto refresh = [context, summ, weak_bar = std::weak_ptr<lib::ToolBar>(bar),
                          weak_spinner = std::weak_ptr<lib::NumberEdit>(spinner), player_index](double value) {
        const auto bar = weak_bar.lock();
        const auto spinner = weak_spinner.lock();
        if (!bar || !spinner) return;
        auto* current_prop = context.props;
        const auto maximum = static_cast<std::uint32_t>(std::max(1.0, spinner->maximum));
        const auto number = std::clamp<std::uint32_t>(
            static_cast<std::uint32_t>(std::max(1.0, std::round(value))),
            1U, maximum);
        spinner->value = static_cast<double>(number);
        const bool supported = current_prop &&
                               current_prop->isTargetVersion(summ, "2");
        spinner->set_enabled(supported);
        if (current_prop)
            current_prop->var().selectedPlayerCardName.value = number;
        auto items = bar->items;
        if (player_index > 0 && player_index - 1 < items.size())
            items[player_index - 1].enabled = supported;
        if (player_index < items.size()) {
            auto& item = items[player_index];
            item.enabled = supported;
            item.text = current_prop
                ? utils::tryFormat(current_prop->requiredMsgs().playerCardName.value,
                                   number)
                : String{"プレイヤーカード名"};
            spinner->set_tooltip(item.text);
            (void)lib::replace_tool_bar_items(bar, std::move(items));
        }
        if (context.services.gui) {
            context.services.gui->request_redraw(bar);
            context.services.gui->request_redraw(spinner);
        }
    };
    struct VersionSubscription {
        commons::Commons* common{};
        std::uint64_t data{}, target{};
        ~VersionSubscription() {
            if (!common) return;
            common->refDataVersion.remove(data);
            common->refTargetVersion.remove(target);
        }
    };
    auto subscription = std::make_shared<VersionSubscription>();
    subscription->common = context.editor_commons;
    if (subscription->common) {
        const auto update = [refresh, weak = std::weak_ptr<lib::NumberEdit>(spinner)] {
            if (const auto input = weak.lock()) refresh(input->value);
        };
        subscription->data = subscription->common->refDataVersion.add(update);
        subscription->target = subscription->common->refTargetVersion.add(update);
    }
    spinner->on_changed = [refresh, subscription](double value) { refresh(value); };
    refresh(spinner->value);
    if (control_parent) control_parent->add(spinner);
    return spinner;
}

std::shared_ptr<lib::ToolBar> createSkinSCharBar(
    const MessageContext& context, const std::shared_ptr<lib::Container>& parent,
    std::function<void(StringView)> insert, const std::function<Skin::Ptr()>& skin,
    const std::size_t rows, const float icon_scale)
{
    auto bar = std::make_shared<SkinSCharToolBar>();
    bar->keyboard_navigation = !lib::editor_window_host_options().all_mode;
    bar->rows = std::max<std::size_t>(1U, rows);
    bar->icon_scale = std::max(1.0F, icon_scale);
    // 拡大したアイコンは、自身が必要とする高い行の寸法を計測する。
    const float strip_height = bar->icon_scale > 1.0F ? 0.0F : 24.0F;
    bar->set_layout_item(lib::LayoutItem{
        0, 0, 1, 1, lib::Align::Stretch, lib::Align::Center,
        {0.0F, strip_height}, {0.0F, strip_height}, {}, 0.0F});
    bar->common = context.editor_commons;
    const auto refresh = [context, weak = std::weak_ptr<lib::ToolBar>(bar), insert, skin] {
        if (const auto current = weak.lock()) {
            updateSkinSCharBar(context, current, insert, skin);
            if (context.services.gui) context.services.gui->request_redraw(current);
        }
    };
    if (bar->common) {
        bar->skin_connection = bar->common->refSkin.add(refresh);
        bar->scale_connection = bar->common->refImageScale.add(refresh);
    }
    refresh();
    if (parent) parent->add(bar);
    return bar;
}

void updateSkinSCharBar(const MessageContext& context,
                        const std::shared_ptr<lib::ToolBar>& bar,
                        std::function<void(StringView)> insert,
                        const std::function<Skin::Ptr()>& skin)
{
    if (!bar) return;
    std::vector<lib::ToolItem> items;
    auto owned = std::dynamic_pointer_cast<SkinSCharToolBar>(bar);
    std::vector<std::unique_ptr<lib::ImageResourceSlot>> images;
    const auto active_skin = skin ? skin() : Skin::Ptr{};
    if (active_skin) {
        std::vector<std::pair<char32_t, String>> characters(
            active_skin->spChars().begin(), active_skin->spChars().end());
        std::sort(characters.begin(), characters.end(),
                  [](const auto& left, const auto& right) {
                      return left.first < right.first;
                  });
        for (const auto& [value, path] : characters) {
            const auto suffix = charText(value);
            if (suffix.empty()) continue;
            const auto command = String("#") + suffix;
            lib::ResourceId icon{};
            if (context.props) {
                // D版と同様、spChar で旧形式のリソースとスキンの上
                // 書きを解決し、その後で imageScale の画像を
                // 選ぶ。 不正な字形は空のボタンとして出さず、省略する。
                const auto source_scale = lib::editor_window_host_options().all_mode
                    ? std::max<std::uint32_t>(1U, context.props->var().imageScale.value)
                    : context.props->drawingScale();
                auto image = dskin::spChar(*active_skin, source_scale, value,
                                           context.services);
                if (!image.valid()) continue;
                if (image.resource) icon = image.resource;
                else if (context.services.graphics && !image.data.frames.empty()) {
                    auto slot = std::make_unique<lib::ImageResourceSlot>();
                    icon = slot->publish(context.services.graphics, image.data);
                    if (icon) images.push_back(std::move(slot));
                }
            }
            if (!icon) continue;
            items.push_back({command, command, icon, true, false, false, false});
        }
    }
    if (owned && owned->rows > 1U) {
        // owned->rows 行に折り返し、ボタン欄の幅を一行分にする。
        const auto per_row = std::max<std::size_t>(
            1U, (items.size() + owned->rows - 1U) / owned->rows);
        const float width = iconStripWidth(per_row, bar->icon_scale);
        auto item = bar->layout_item();
        item.horizontal = lib::Align::Start;
        item.minimum = {width, 0.0F};
        item.preferred = {width, 0.0F};
        bar->set_layout_item(item);
    }
    if (!lib::replace_tool_bar_items(bar, std::move(items))) return;
    if (owned) owned->images = std::move(images);
    bar->on_command = [insert = std::move(insert)](StringView command) {
        if (insert) insert(command);
    };
}

std::shared_ptr<lib::Panel> createFlagStepBar(
    const std::shared_ptr<lib::Container>& parent,
    std::function<void(StringView)> insert, const MessageContext& context,
    const Props* prop, const Summary* summ, const usecounter::UseCounter* uc,
    const Skin::Ptr& force_skin, bool image_font,
    const std::function<std::shared_ptr<lib::Button>(
        const std::shared_ptr<lib::Container>&, types::VariableType)>& create_button,
    const float combo_width)
{
    // セリフ側は独自の狭い幅を渡す。ゼロなら標準幅を使う。
    const float comboWidth = combo_width > 0.0F ? combo_width : kVariableComboWidth;
    // 固定幅で左に詰める参照欄は ALL のエディタの仕様。 単独版
    // は従来の伸縮する130px（最小80px）のコンボを維持する。
    const bool fixedRow = lib::editor_window_host_options().all_mode;
    auto panel = std::make_shared<ReferenceInsertPanel>();
    panel->bind(context.editor_commons);
    // 保持される検索・メニューのコントロールは、通知用パネルと有効範囲を
    // 共有する。 Commons がなくても独立した部品として動くが、失
    // 効した Commons を複製済みのコールバックから参照しない。
    const auto common_is_live = [common = panel->common, life = panel->common_lifetime] {
        return !common || !life.expired();
    };
    auto outer_layout = std::make_shared<lib::GridLayout>();
    outer_layout->columns = image_font && summ ? 2U : 1U;
    outer_layout->spacing = 4.0F;
    panel->set_layout(outer_layout);
    panel->set_layout_item(lib::LayoutItem{
        0, 0, 1, 1, lib::Align::Stretch, lib::Align::Center,
        {0.0F, 24.0F}, {0.0F, 24.0F}, {}, 0.0F});

    const auto variable_values = [summ, uc,
                                  counter_owner = context.use_counter_owner,
                                  prop](const types::VariableType type) {
        std::vector<String> result;
        if (!summ) return result;
        namespace vars = ::cwxeditor::cwx::flag;
        const auto counter = counter_owner ? counter_owner
            : usecounter::CounterOwner(const_cast<usecounter::UseCounter*>(uc), [](auto*) {});
        const auto append = [&result](const auto& values) {
            for (const auto& value : values)
                if (value) result.push_back(value->path());
        };
        if (type == types::VariableType::Flag)
            append(vars::allVars<vars::Flag>(summ->flagDirRoot(), counter));
        else if (type == types::VariableType::Step)
            append(vars::allVars<vars::Step>(summ->flagDirRoot(), counter));
        else
            append(vars::allVars<vars::Variant>(summ->flagDirRoot(), counter));
        const bool logical = prop && prop->var().logicalSort.value;
        std::stable_sort(result.begin(), result.end(),
                         [logical](const String& left, const String& right) {
                             return ::cwxeditor::cwx::flag::pathCompare(
                                        left, right, logical) < 0;
                         });
        return result;
    };
    const auto variable_cwx_path = [summ, uc,
                                    counter_owner = context.use_counter_owner](
                                       const types::VariableType type,
                                       const StringView selected) -> String {
        if (!summ || selected.empty()) return {};
        namespace vars = ::cwxeditor::cwx::flag;
        const auto counter = counter_owner ? counter_owner
            : usecounter::CounterOwner(const_cast<usecounter::UseCounter*>(uc), [](auto*) {});
        const auto find_path = [selected](const auto& values) -> String {
            for (const auto& value : values)
                if (value && value->path() == selected) return value->cwxPath(true);
            return {};
        };
        if (type == types::VariableType::Flag)
            return find_path(vars::allVars<vars::Flag>(summ->flagDirRoot(), counter));
        if (type == types::VariableType::Step)
            return find_path(vars::allVars<vars::Step>(summ->flagDirRoot(), counter));
        return find_path(vars::allVars<vars::Variant>(summ->flagDirRoot(), counter));
    };

    auto variable_panel = std::make_shared<lib::Panel>();
    auto variable_layout = std::make_shared<lib::GridLayout>();
    variable_layout->columns = 3;
    // コンボは kVariableComboWidth の固定幅にす
    // る。 等幅の列にすると、エディタ列全体に引き伸ばされてしまう。
    variable_layout->equal_width = !fixedRow;
    variable_layout->spacing = 4.0F;
    variable_panel->set_layout(variable_layout);
    const auto icon = [prop](StringView path) {
        return prop ? const_cast<Props*>(prop)->images().load(path)
                    : lib::ResourceId{};
    };
    const auto add_variable = [&](types::VariableType type,
                                  StringView prefix, StringView suffix,
                                  String tooltip, StringView icon_path,
                                  std::size_t column) {
        auto item_panel = std::make_shared<lib::Panel>();
        auto layout = std::make_shared<lib::GridLayout>();
        layout->columns = 2;
        layout->spacing = 2.0F;
        item_panel->set_layout(layout);
        // 左に詰めて配置する。伸縮するセルで中央寄せすると、 挿入ボ
        // タンがコンボから離れ、ドロップダウンの横に空白ができる。
        if (fixedRow)
            item_panel->set_layout_item({0, column, 1, 1, lib::Align::Start,
                                         lib::Align::Center});
        else
            item_panel->set_layout_item({0, column});
        auto combo = std::make_shared<lib::ComboBox>();
        auto search = std::make_shared<incsearch::IncSearch>(
            context.services, combo.get(), [weak = std::weak_ptr<lib::ComboBox>(combo)] {
                const auto value = weak.lock();
                return value && !value->items.empty();
            });
        auto refresh = std::make_shared<lib::Action>();
        *refresh = [combo, search, variable_values, type, common_is_live] {
            if (!common_is_live()) return;
            String selected;
            if (combo->selected_index && *combo->selected_index < combo->items.size())
                selected = combo->items[*combo->selected_index].value;
            std::vector<lib::ChoiceItem> items;
            for (const auto& value : variable_values(type))
                if (search->match(value)) items.push_back({value, value, 0, true});
            std::optional<std::size_t> index;
            for (std::size_t i = 0; i < items.size(); ++i)
                if (items[i].value == selected) { index = i; break; }
            if (!index && !items.empty()) index = 0U;
            (void)lib::replace_combo_items(combo, std::move(items), index);
            combo->set_enabled(!combo->items.empty());
        };
        search->modEvent.push_back([refresh] { if (*refresh) (*refresh)(); });
        panel->refreshers.push_back([refresh] { if (*refresh) (*refresh)(); });
        combo->on_blur = [search, refresh] {};
        combo->on_text_input = [search, weak = std::weak_ptr<lib::ComboBox>(combo)](
                                   lib::TextInputEvent& event) {
            const auto target = weak.lock();
            if (target && search->handleHostTextInput(event, target.get()))
                event.handled = true;
        };
        combo->on_key_down = [search, weak = std::weak_ptr<lib::ComboBox>(combo)](
                                 lib::KeyEvent& event) {
            const auto target = weak.lock();
            if (target && search->handleHostKeyDown(event, target.get()))
                event.handled = true;
        };
        auto menu = std::make_shared<lib::Menu>(true);
        menu->items.push_back({prop ? prop->buildMenu(types::MenuID::IncSearch)
                                    : String{"絞り込み検索"},
                               "search", {},
                               prop ? const_cast<Props*>(prop)->images().menu(types::MenuID::IncSearch)
                                    : lib::ResourceId{}});
        menu->items.push_back({.separator = true});
        const auto open_menu = types::MenuID::OpenAtVarView;
        menu->items.push_back({prop ? prop->buildMenu(open_menu)
                                    : String{"状態変数ビューで表示"},
                               "open", {},
                               prop ? const_cast<Props*>(prop)->images().menu(open_menu)
                                    : lib::ResourceId{}});
        const std::weak_ptr<lib::Menu> weak_menu = menu;
        const std::weak_ptr<lib::ComboBox> weak_combo = combo;
        const std::weak_ptr<ReferenceInsertPanel> weak_panel = panel;
        menu->on_open = [weak_menu, weak_combo, weak_panel, prop, open_menu] {
            refreshReferenceContextMenu(weak_menu, weak_combo, weak_panel, prop,
                                        open_menu, "状態変数ビューで表示");
        };
        menu->on_open();
        menu->on_command = [search, combo, common = context.editor_commons,
                            variable_cwx_path, type, common_is_live](StringView command) {
            if (!common_is_live()) return;
            if (command == "search") search->startIncSearch();
            else if (command == "open" && common && combo->selected_index &&
                     *combo->selected_index < combo->items.size()) {
                const auto& item = combo->items[*combo->selected_index];
                const auto& selected = item.value.empty() ? item.text : item.value;
                const auto target = variable_cwx_path(type, selected);
                if ((target.empty() || !common->openCWXPath(target, false)) && common_is_live())
                    (void)common->openFlagWin(true);
            }
        };
        combo->on_pointer = [menu, combo, gui = context.services.gui](lib::PointerEvent& event) {
            if (!gui || event.button != lib::PointerButton::Secondary ||
                event.action != lib::PointerAction::Down) return;
            gui->show_context_menu(menu, combo, event.position);
            event.handled = true;
        };
        if (*refresh) (*refresh)();
        if (fixedRow) {
            combo->fixed_width = comboWidth;
            combo->set_layout_item({0, 0, 1, 1, lib::Align::Start,
                                    lib::Align::Center, {comboWidth, 24}, {comboWidth, 24}, {}, 0.0F});
        } else {
            combo->set_layout_item({0, 0, 1, 1, lib::Align::Stretch,
                                    lib::Align::Center, {80, 24}, {130, 24}, {}, 0.0F});
        }
        item_panel->add(combo);
        std::shared_ptr<lib::Button> button;
        if (create_button) {
            button = create_button(item_panel, type);
        } else {
            button = std::make_shared<lib::Button>();
            button->icon = icon(icon_path);
            button->accessibility_name = tooltip;
            button->set_tooltip(std::move(tooltip));
            button->on_click = [insert, combo, prefix = String(prefix),
                                suffix = String(suffix)] {
                if (!insert || !combo->selected_index ||
                    *combo->selected_index >= combo->items.size()) return;
                const auto& selected = combo->items[*combo->selected_index];
                const auto& value = selected.value.empty() ? selected.text
                                                            : selected.value;
                insert(prefix + value + suffix);
            };
        }
        if (button) {
            button->set_enabled(combo->enabled());
            button->set_layout_item({0, 1, 1, 1, lib::Align::Center,
                                     lib::Align::Center, {26, 24}});
            if (std::find(item_panel->children().begin(),
                          item_panel->children().end(), button) ==
                item_panel->children().end())
                item_panel->add(button);
            panel->refreshers.push_back(
                [weak_button = std::weak_ptr<lib::Button>(button),
                 weak_combo = std::weak_ptr<lib::ComboBox>(combo)] {
                    const auto current_button = weak_button.lock();
                    const auto current_combo = weak_combo.lock();
                    if (current_button && current_combo)
                        current_button->set_enabled(current_combo->enabled());
                });
        }
        variable_panel->add(item_panel);
    };
    add_variable(types::VariableType::Flag, "%", "%",
                 prop ? prop->requiredMsgs().addMsgRefFlag.value : String{"フラグ参照の追加"},
                 "flag.png", 0U);
    add_variable(types::VariableType::Step, "$", "$",
                 prop ? prop->requiredMsgs().addMsgRefStep.value : String{"ステップ参照の追加"},
                 "step.png", 1U);
    add_variable(types::VariableType::Variant, "@", "@",
                 prop ? prop->requiredMsgs().addMsgRefVariant.value : String{"コモン参照の追加"},
                 "variant.png", 2U);
    // 伸縮させず、フォントのグループを変数グループの
    // すぐ後に置く。 余分な空白で右へ押し出さない。
    if (fixedRow)
        variable_panel->set_layout_item({0, 0, 1, 1, lib::Align::Start,
                                         lib::Align::Center});
    else
        variable_panel->set_layout_item({0, 0, 1, 1, lib::Align::Stretch,
                                         lib::Align::Center, {}, {450, 24}, {}, 1.0F});
    panel->add(variable_panel);

    if (image_font && summ) {
        auto font_panel = std::make_shared<lib::Panel>();
        auto font_layout = std::make_shared<lib::GridLayout>();
        font_layout->columns = 3;
        font_layout->spacing = 2.0F;
        font_panel->set_layout(font_layout);
        auto fonts = std::make_shared<lib::ComboBox>();
        auto font_search = std::make_shared<incsearch::IncSearch>(
            context.services, fonts.get(), [weak = std::weak_ptr<lib::ComboBox>(fonts)] {
                const auto value = weak.lock();
                return value && value->enabled();
            });
        const auto collect_fonts = [context, prop, summ, force_skin, font_search, common_is_live](bool& has_fonts) {
            std::vector<lib::ChoiceItem> items;
            has_fonts = false;
            if (!common_is_live()) return items;
            const auto active_skin = force_skin ? force_skin
                : (context.forced_skin ? context.forced_skin
                   : (context.editor_commons ? context.editor_commons->skin()
                                              : Skin::Ptr{}));
            std::error_code error;
            const auto scenario_path = utf8Path(summ->scenarioPath());
            if (!scenario_path.empty() &&
                std::filesystem::is_directory(scenario_path, error)) {
                for (const auto& entry : std::filesystem::directory_iterator(
                         scenario_path, error)) {
                    if (error) break;
                    if (!entry.is_regular_file(error)) continue;
                    const String value = structs::pathToUtf8(entry.path().filename());
                    const auto full_path = structs::pathToUtf8(entry.path());
                    if (!msgutils::isSPFontFile(value) ||
                        (prop && utils::containsPath(prop->var().ignorePaths.value, value)) ||
                        summ->isSystemFile(full_path) ||
                        (summ->legacy() && !::cwxeditor::cwx::sjis::isSJIS1ByteChar(
                            msgutils::decodeFontPath(value))) ||
                        (active_skin && !active_skin->isBgImage(full_path))) continue;
                    has_fonts = true;
                    if (!font_search->match(charText(msgutils::decodeFontPath(value)))) continue;
                    items.push_back({value, value, 0, true});
                }
            }
            // D版の createFlagStepBar と同様、clistdir のシナリオ内の順序を維持する。
            return items;
        };
        auto refresh_fonts = std::make_shared<lib::Action>();
        // イメージフォントのドロップダウンは、フラグ・ステップ・コモンより
        // さらに40px狭くする。 メッセージとセリフの両方に適用する。
        const float fontComboWidth = comboWidth - 40.0F;
        if (fixedRow) {
            fonts->fixed_width = fontComboWidth;
            fonts->set_layout_item({0, 0, 1, 1, lib::Align::Start,
                                    lib::Align::Center, {fontComboWidth, 24}, {fontComboWidth, 24}, {}, 0.0F});
        } else {
            fonts->set_layout_item({0, 0, 1, 1, lib::Align::Stretch,
                                    lib::Align::Center, {80, 24}, {130, 24}, {}, 0.0F});
        }
        font_panel->add(fonts);
        auto put_font = std::make_shared<lib::Button>();
        put_font->icon = icon("backs.png"); // D prop.images.imageFont
        put_font->accessibility_name = prop
            ? prop->requiredMsgs().addMsgRefImageFont.value : String{"画像参照の追加"};
        put_font->set_tooltip(put_font->accessibility_name);
        put_font->on_click = [insert, fonts] {
            if (!insert || !fonts->selected_index ||
                *fonts->selected_index >= fonts->items.size()) return;
            const auto& item = fonts->items[*fonts->selected_index];
            const auto& path = item.value.empty() ? item.text : item.value;
            if (path.empty() || !msgutils::isSPFontFile(path)) return;
            insert(String("#") + charText(msgutils::decodeFontPath(path)));
        };
        font_panel->add(put_font);
        auto image_list = std::make_shared<lib::ToggleButton>();
        const auto look_images_menu = types::MenuID::LookImages;
        image_list->icon = prop ? const_cast<Props*>(prop)->images().menu(look_images_menu)
                                : icon("img_list.png");
        image_list->accessibility_name = prop ? prop->buildMenu(look_images_menu)
                                              : String{"画像一覧"};
        image_list->set_tooltip(image_list->accessibility_name);
        auto image_list_window =
            std::make_shared<std::shared_ptr<imagelistwindow::ImageListWindowBase>>();
        // D版と同様、特殊フォントの選択も有効な ImageListWindow
        // として扱う。 更新中は現在のインスタンスを保持し、パネルや
        // Commons の無効化後に残ったコールバックは拒否する。
        if (panel->common) {
            panel->gallery_scale = panel->common->refImageScale.add(
                [weak_panel=std::weak_ptr<ReferenceInsertPanel>(panel),
                 weak_gallery=std::weak_ptr<std::shared_ptr<imagelistwindow::ImageListWindowBase>>(image_list_window),
                 prop] {
                    const auto current_panel = weak_panel.lock();
                    if (!current_panel || current_panel->common_lifetime.expired() || !prop) return;
                    const auto holder = weak_gallery.lock();
                    const auto gallery = holder ? *holder : nullptr;
                    if (!gallery || gallery->closed()) return;
                    const auto card = prop->looks().cardSize();
                    gallery->refImageScale(
                        {static_cast<float>(prop->s(static_cast<int>(card.width))),
                         static_cast<float>(prop->s(static_cast<int>(card.height)))},
                        prop->imageScaleFactor(), static_cast<int>(prop->drawingScale()));
                });
        }
        *refresh_fonts = [fonts, put_font, image_list, image_list_window,
                          collect_fonts, common_is_live] {
            if (!common_is_live()) return;
            const String selected = fonts->selected_index &&
                    *fonts->selected_index < fonts->items.size()
                ? fonts->items[*fonts->selected_index].value : String{};
            bool has_fonts{};
            auto items = collect_fonts(has_fonts);
            const bool available = !items.empty();
            if (!has_fonts) items.push_back({"font_?.bmp", "", 0, true});
            std::optional<std::size_t> selection{0U};
            for (std::size_t i = 0; i < items.size(); ++i)
                if (!selected.empty() && items[i].value == selected) {
                    selection = i; break;
                }
            (void)lib::replace_combo_items(fonts, std::move(items), selection);
            fonts->set_enabled(has_fonts);
            put_font->set_enabled(available);
            image_list->set_enabled(available);
            const bool standalone = !lib::editor_window_host_options().all_mode;
            if (!available && !standalone && *image_list_window) {
                (*image_list_window)->close();
            } else if ((available || standalone) && *image_list_window &&
                       !(*image_list_window)->closed()) {
                // D版と同様、絞り込みで空になっても、開いている特殊フォントの
                // 一覧を更新する。 フォントなしの案内は画像として扱わない。
                const auto list = *image_list_window;
                lib::Array<String> paths;
                paths.reserve(fonts->items.size());
                if (has_fonts || !standalone) for (const auto& item : fonts->items) {
                    const auto& path = item.value.empty() ? item.text : item.value;
                    if (msgutils::isSPFontFile(path)) paths.push_back(path);
                }
                const String current_selection = standalone
                    ? (has_fonts && fonts->selected_index &&
                       *fonts->selected_index < fonts->items.size()
                        ? fonts->items[*fonts->selected_index].value : String{})
                    : selected;
                list->images("/", std::move(paths));
                if (common_is_live() && *image_list_window == list && !list->closed())
                    list->select(current_selection);
            }
        };
        font_search->modEvent.push_back(
            [refresh_fonts] { if (*refresh_fonts) (*refresh_fonts)(); });
        fonts->on_blur = [font_search, refresh_fonts] {};
        fonts->on_text_input = [font_search, weak = std::weak_ptr<lib::ComboBox>(fonts)](
                                   lib::TextInputEvent& event) {
            const auto target = weak.lock();
            if (target && font_search->handleHostTextInput(event, target.get()))
                event.handled = true;
        };
        fonts->on_key_down = [font_search, weak = std::weak_ptr<lib::ComboBox>(fonts)](
                                 lib::KeyEvent& event) {
            const auto target = weak.lock();
            if (target && font_search->handleHostKeyDown(event, target.get()))
                event.handled = true;
        };
        auto font_menu = std::make_shared<lib::Menu>(true);
        font_menu->items.push_back({prop ? prop->buildMenu(types::MenuID::IncSearch)
                                         : String{"絞り込み検索"},
                                    "search", {},
                                    prop ? const_cast<Props*>(prop)->images().menu(types::MenuID::IncSearch)
                                         : lib::ResourceId{}});
        font_menu->items.push_back({.separator = true});
        const auto open_file_menu = types::MenuID::OpenAtFileView;
        font_menu->items.push_back({prop ? prop->buildMenu(open_file_menu)
                                         : String{"素材ビューで表示"},
                                    "open", {},
                                    prop ? const_cast<Props*>(prop)->images().menu(open_file_menu)
                                         : lib::ResourceId{}});
        const std::weak_ptr<lib::Menu> weak_font_menu = font_menu;
        const std::weak_ptr<lib::ComboBox> weak_fonts = fonts;
        const std::weak_ptr<ReferenceInsertPanel> weak_panel = panel;
        font_menu->on_open = [weak_font_menu, weak_fonts, weak_panel, prop,
                              open_file_menu] {
            refreshReferenceContextMenu(weak_font_menu, weak_fonts, weak_panel,
                                        prop, open_file_menu,
                                        "素材ビューで表示");
        };
        font_menu->on_open();
        font_menu->on_command = [font_search, fonts, common = context.editor_commons,
                                 summ, common_is_live](StringView command) {
            if (!common_is_live()) return;
            if (command == "search") { font_search->startIncSearch(); return; }
            if (command != "open" || !common || !fonts->selected_index ||
                *fonts->selected_index >= fonts->items.size()) return;
            const auto& item = fonts->items[*fonts->selected_index];
            if (!item.value.empty()) (void)common->openFilePath(
                structs::pathToUtf8(utf8Path(summ->scenarioPath()) /
                                    utf8Path(item.value)), false, true);
        };
        fonts->on_pointer = [font_menu, fonts, gui = context.services.gui](
                                lib::PointerEvent& event) {
            if (!gui || event.button != lib::PointerButton::Secondary ||
                event.action != lib::PointerAction::Down) return;
            gui->show_context_menu(font_menu, fonts, event.position);
            event.handled = true;
        };
        panel->refreshers.push_back(
            [refresh_fonts] { if (*refresh_fonts) (*refresh_fonts)(); });
        if (*refresh_fonts) (*refresh_fonts)();
        const auto font_paths = [fonts] {
            lib::Array<lib::String> paths;
            paths.reserve(fonts->items.size());
            for (const auto& item : fonts->items) {
                const auto& path = item.value.empty() ? item.text : item.value;
                if (msgutils::isSPFontFile(path)) paths.push_back(path);
            }
            return paths;
        };
        const auto selected_font = [fonts] {
            if (!fonts->selected_index ||
                *fonts->selected_index >= fonts->items.size())
                return String{};
            const auto& item = fonts->items[*fonts->selected_index];
            return item.value.empty() ? item.text : item.value;
        };
        fonts->on_selection_changed = [image_list_window, selected_font](
                                          std::optional<std::size_t>) {
            if (*image_list_window && !(*image_list_window)->closed())
                (*image_list_window)->select(selected_font());
        };
        image_list->on_changed =
            [image_list_window, image_list, font_paths, selected_font, insert,
             font_search, fonts, context, prop, summ, force_skin, common_is_live,
             weak_panel=std::weak_ptr<ReferenceInsertPanel>(panel)](const bool checked) {
                if (!weak_panel.lock() || !common_is_live()) return;
                if (!checked) {
                    if (*image_list_window) (*image_list_window)->close();
                    return;
                }
                if (!prop || !summ || !context.services.gui) {
                    image_list->checked = false;
                    return;
                }
                if (*image_list_window && !(*image_list_window)->closed()) {
                    (*image_list_window)->open();
                    return;
                }

                imagelistwindow::ImageListWindowOptions options;
                options.skin = force_skin;
                options.image_scale = prop->imageScaleFactor();
                options.drawing_scale = static_cast<int>(prop->drawingScale());
                options.graphics = context.services.graphics;
                options.gui = context.services.gui;
                options.drag_drop = context.services.drag_drop;
                options.enable_drag_drop = false;
                options.window_size = {
                    static_cast<float>(prop->var().imageListWidth.value),
                    static_cast<float>(prop->var().imageListHeight.value)};
                const auto card = prop->looks().cardSize();
                options.card_image_size = {
                    static_cast<float>(prop->s(static_cast<int>(card.width))),
                    static_cast<float>(prop->s(static_cast<int>(card.height)))};
                // ResourceId だけでは復号後の寸法が分からない。 D版と
                // 同じ倍率対応の通常の読み込みを使い、倍率変更後も含めて、 ギャラ
                // リーが実際の特殊フォント画像の寸法を求めて描画できるようにする。
                options.image_factory = [weak_panel, common_is_live, context, prop, summ, force_skin](
                    const StringView path, const bool mask) {
                    const auto panel = weak_panel.lock();
                    if (!panel || !common_is_live() || !prop)
                        return imagelistwindow::ImageDataWithScale{};
                    const auto skin = force_skin ? force_skin
                        : (context.forced_skin ? context.forced_skin
                           : (context.editor_commons ? context.editor_commons->skin() : Skin::Ptr{}));
                    bool skin_material = false, engine_material = false;
                    auto resolved = String(path);
                    if (skin) resolved = skin->findImagePathF(path,
                        summ ? summ->scenarioPath() : String{},
                        summ ? summ->dataVersion() : versioninfo::LATEST_VERSION,
                        skin_material, engine_material);
                    if (resolved.empty()) resolved = String(path);
                    const auto scale = skin_material || engine_material
                        ? prop->drawingScale() : prop->drawingScaleForImage(summ);
                    auto decoded = dutils::loadImageWithScale(prop, skin.get(), summ,
                        resolved, scale, mask, false, 0, 0, {}, context.services);
                    if (!decoded.valid()) return imagelistwindow::ImageDataWithScale{};
                    return imagelistwindow::ImageDataWithScale(std::move(decoded.baseData()), decoded.scale());
                };

                auto window = std::make_shared<imagelistwindow::ImageListWindowBase>(
                    prop->parent(), context.summary_owner, force_skin,
                    context.owner,
                    [insert](const StringView path) {
                        if (insert && msgutils::isSPFontFile(path))
                            insert(String("#") +
                                   charText(msgutils::decodeFontPath(path)));
                    },
                    lib::WidgetPtr{}, std::move(options), true);
                if (!lib::editor_window_host_options().all_mode) {
                    const auto menu = std::make_shared<lib::Menu>(true);
                    menu->items.push_back({prop->buildMenu(types::MenuID::IncSearch), "incremental-search"});
                    menu->items.push_back({"", "", {}, {}, true, false, false, true});
                    menu->items.push_back({prop->buildMenu(types::MenuID::CloseWin), "close-window"});
                    const auto weak_window = std::weak_ptr<imagelistwindow::ImageListWindowBase>(window);
                    const auto weak_fonts = std::weak_ptr<lib::ComboBox>(fonts);
                    menu->on_open = [weak_menu = std::weak_ptr<lib::Menu>(menu), weak_fonts] {
                        const auto current = weak_menu.lock();
                        if (!current || current->items.empty()) return;
                        const auto fonts = weak_fonts.lock();
                        current->items[0].enabled = fonts && fonts->enabled();
                        ++current->item_update_revision;
                    };
                    menu->on_command = [weak_window, weak_fonts, font_search, common_is_live](StringView command) {
                        const auto current = weak_window.lock();
                        if (!current || current->closed() || !common_is_live()) return;
                        if (command == "close-window") current->close();
                        else if (command == "incremental-search") {
                            const auto fonts = weak_fonts.lock();
                            if (fonts && fonts->enabled()) font_search->startIncSearch();
                        }
                    };
                    const auto list = window->widget();
                    const auto previous_pointer = list->on_pointer;
                    list->on_pointer = [weak_window, menu, previous_pointer, gui = context.services.gui](lib::PointerEvent& event) {
                        const auto current = weak_window.lock();
                        if (!current || current->closed()) return;
                        if (event.action == lib::PointerAction::Down && event.button == lib::PointerButton::Secondary && gui) {
                            if (menu->on_open) menu->on_open();
                            event.handled = true;
                            gui->show_context_menu(menu, current->widget(), event.position);
                            return;
                        }
                        if (previous_pointer) previous_pointer(event);
                    };
                }
                const auto weak_button = std::weak_ptr<lib::ToggleButton>(image_list);
                window->on_closed = [weak_button, gui = context.services.gui] {
                    if (const auto button = weak_button.lock()) {
                        button->checked = false;
                        if (gui) gui->request_redraw(button);
                    }
                };
                window->images("/", font_paths());
                window->mask(true);
                window->select(selected_font());
                *image_list_window = std::move(window);
                (*image_list_window)->open();
            };
        font_panel->add(image_list);
        font_panel->set_layout_item({0, 1, 1, 1, lib::Align::Start, lib::Align::Center});
        panel->add(font_panel);
    }
    if (panel->common) {
        const auto weak = std::weak_ptr<ReferenceInsertPanel>(panel);
        const auto refresh = [weak](auto&&...) {
            if (const auto current = weak.lock()) current->refresh();
        };
        panel->variables = panel->common->refFlagAndStep.add(refresh);
        panel->deleted_variables = panel->common->delFlagAndStep.add(refresh);
        panel->uses = panel->common->refUseCount.add(refresh);
        panel->paths = panel->common->refPath.add(refresh);
        panel->path_tree = panel->common->refPaths.add(refresh);
        panel->deleted_paths = panel->common->delPaths.add(refresh);
        panel->replaced_path = panel->common->replPath.add(refresh);
        panel->ignored_paths = panel->common->refIgnorePaths.add(refresh);
        panel->skin = panel->common->refSkin.add(refresh);
        panel->version = panel->common->refDataVersion.add(refresh);
    }
    if (parent) parent->add(panel);
    return panel;
}

void putColor(lib::TextEdit& control, char32_t color)
{
    if (charText(color).empty()) return;
    const auto byte_start = std::min(control.selection_start,
                                     control.text.size());
    const auto byte_end = std::min(byte_start + control.selection_length,
                                   control.text.size());
    const auto old_value = decodeUtf8(control.text);
    const auto start = byteToCodepointIndex(control.text, byte_start);
    const auto end = byteToCodepointIndex(control.text, byte_end);
    const auto new_value = msgutils::putColor(old_value, color, start, end);
    auto caret = end;
    if (new_value.size() >= old_value.size())
        caret += new_value.size() - old_value.size();
    else
        caret -= std::min(caret, old_value.size() - new_value.size());
    caret = std::min(caret, new_value.size());
    control.text = encodeUtf8(new_value);
    control.selection_start = encodeUtf8(
        std::u32string_view(new_value).substr(0, caret)).size();
    control.selection_length = 0;
    if (control.on_changed) control.on_changed(control.text);
}

void putColor(String& value, char32_t color)
{
    const auto suffix = charText(color);
    if (!suffix.empty()) value += String("&") + suffix;
}

MsgPreviewWindow::MsgPreviewWindow(MessageContext context, WindowGeometry size)
    : context_(std::move(context)),
      geometry_(size),
      preview_(std::make_shared<MsgPreview>(context_))
{
    if (context_.props) {
        const auto bounds = context_.props->looks().messageBounds();
        scaled_message_size_ = {static_cast<float>(context_.props->s(bounds.width)),
                                static_cast<float>(context_.props->s(bounds.height))};
    }
    if (context_.services.gui) window_ = context_.services.gui->create_dialog();
    if (!window_) window_ = std::make_shared<lib::Dialog>();
    window_->title = context_.props
        ? context_.props->requiredMsgs().dlgTitMessagePreview.value
        : String{"プレビュー"};
    window_->resizable = true;
    window_->minimizable = false;
    window_->maximizable = false;
    window_->initially_hidden = true;
    // D版の setupWindow と同様、ホストの再接続後も保存済みのプレビュー配置を復元する。
    lib::configure_owned_window(window_, context_.owner,
        lib::OwnedWindowKind::Modeless, lib::OwnedWindowCenter::Never);
    window_->minimum_size = lib::Size{360.0F, 260.0F};
    const std::weak_ptr<int> close_lifetime = close_lifetime_;
    window_->on_close_requested = [this, close_lifetime] {
        if (close_lifetime.expired()) return;
        close();
    };
    const auto width = geometry_.width > 0 ? geometry_.width : 500;
    const auto height = geometry_.height > 0 ? geometry_.height : 360;
    window_->set_bounds(lib::Rect{static_cast<float>(geometry_.x),
                                  static_cast<float>(geometry_.y),
                                  static_cast<float>(width),
                                  static_cast<float>(height)});
    auto content = std::make_shared<lib::Panel>();
    auto layout = std::make_shared<lib::GridLayout>();
    layout->columns = 1;
    layout->padding = {5.0F, 5.0F, 5.0F, 5.0F};
    layout->spacing = 5.0F;
    content->set_layout(layout);
    preview_image_view_ = std::make_shared<lib::ImageView>();
    preview_image_view_->scale_mode = lib::ImageScaleMode::Center;
    preview_image_view_->interpolation = lib::Interpolation::Best;
    preview_image_view_->set_layout_item({0, 0, 1, 1, lib::Align::Stretch,
                                          lib::Align::Stretch,
                                          {340, 130}, {}, {}, 0.45F});
    content->add(preview_image_view_);
    preview_values_view_ = preview_->valuesControl();
    if (preview_values_view_) {
        preview_values_view_->set_layout_item({1, 0, 1, 1,
                                               lib::Align::Stretch,
                                               lib::Align::Stretch,
                                               {340, 120}, {}, {}, 0.55F});
        content->add(preview_values_view_);
    }
    window_->add(content);
    setOwner(context_.owner);
}

MsgPreviewWindow::~MsgPreviewWindow()
{
    close_lifetime_.reset();
    try {
        if (disconnect_owner_move_) {
            auto disconnect = std::exchange(disconnect_owner_move_, {});
            disconnect();
        }
    } catch (...) {
        // コールバック複製のメモリ確保が失敗しても、破棄処理から例外を出さない。 失効
        // 済みのトークンにより、残ったリスナーが self を使うことは防がれている。
    }
}

void MsgPreviewWindow::setOwner(const std::shared_ptr<lib::Window>& owner)
{
    if (disconnect_owner_move_) {
        auto disconnect = std::exchange(disconnect_owner_move_, {});
        disconnect();
    }
    context_.owner = owner;
    if (owner && window_) {
        auto bounds = window_->bounds();
        const auto owner_bounds = owner->bounds();
        bounds.x = owner_bounds.x + static_cast<float>(geometry_.x);
        bounds.y = owner_bounds.y + static_cast<float>(geometry_.y);
        if (context_.services.gui)
            context_.services.gui->set_window_bounds(window_, bounds);
        else
            window_->set_bounds(bounds);
    }
    // 共通の Window::set_bounds コールバックは、ALL 内の子ウィンドウの移動経路でもある。
    if (!owner || !window_) return;

    const auto active = std::make_shared<bool>(true);
    const auto prior = owner->on_bounds_changed;
    const auto lifetime = std::weak_ptr<int>(close_lifetime_);
    const auto preview = std::weak_ptr<lib::Dialog>(window_);
    const auto gui = context_.services.gui;
    const auto before = std::make_shared<lib::Rect>(owner->bounds());
    struct OwnerMove {
        std::shared_ptr<bool> active;
        std::function<void(lib::Rect)> prior;
        std::weak_ptr<int> lifetime;
        std::weak_ptr<lib::Dialog> preview;
        std::shared_ptr<lib::GuiBackend> gui;
        std::shared_ptr<lib::Rect> before;
        MsgPreviewWindow* self{};
        void operator()(lib::Rect next) const {
            const auto dx = next.x - before->x;
            const auto dy = next.y - before->y;
            *before = next;
            if (prior) prior(next);
            if (!*active || lifetime.expired() || (dx == 0.0F && dy == 0.0F)) return;
            const auto current = preview.lock();
            if (!current) return;
            auto bounds = current->bounds();
            bounds.x += dx;
            bounds.y += dy;
            if (gui) gui->set_window_bounds(current, bounds);
            else current->set_bounds(bounds);
            if (!*active || lifetime.expired()) return;
            self->saveWin();
        }
    };
    owner->on_bounds_changed = OwnerMove{active, prior, lifetime, preview, gui,
                                          before, this};
    const std::weak_ptr<lib::Window> weak_owner = owner;
    disconnect_owner_move_ = [weak_owner, active, prior] {
        *active = false;
        if (const auto current = weak_owner.lock()) {
            if (const auto* installed = current->on_bounds_changed.target<OwnerMove>();
                installed && installed->active == active)
                current->on_bounds_changed = prior;
        }
    };
}

void MsgPreviewWindow::refImageScale()
{
    const std::weak_ptr<int> lifetime = close_lifetime_;
    const auto preview = preview_;
    const auto window = window_;
    if (!lib::editor_window_host_options().all_mode && context_.props && window) {
        const auto message = context_.props->looks().messageBounds();
        const lib::Size scaled{static_cast<float>(context_.props->s(message.width)),
                               static_cast<float>(context_.props->s(message.height))};
        auto bounds = window->bounds();
        bounds.width = std::max(1.0F, bounds.width + scaled.width - scaled_message_size_.width);
        bounds.height = std::max(1.0F, bounds.height + scaled.height - scaled_message_size_.height);
        scaled_message_size_ = scaled;
        window->set_bounds(bounds);
        if (lifetime.expired()) return;
        // 保存済みのサイズは設定側で調整済み。 倍率による
        // 差を再び加算せず、実際の現在の配置を通知する。
        saveWin();
        if (lifetime.expired()) return;
    }
    if (preview) preview->refImageScale();
}

void MsgPreviewWindow::invalidateImageCache()
{
    if (preview_) preview_->invalidateImageCache();
}

void MsgPreviewWindow::saveWin()
{
    // D版と同様、独立プレビューが非表示の間は位置・寸法の通知を無視する。 非
    // 表示ウィンドウの配置変更で、最後にユーザーが見ていた位置を上書きしない。
    if (!window_ || !visible_) return;
    const auto bounds = window_->bounds();
    const auto owner = context_.owner;
    const auto owner_bounds = owner ? owner->bounds() : lib::Rect{};
    geometry_.x = static_cast<std::int32_t>(std::lround(bounds.x - owner_bounds.x));
    geometry_.y = static_cast<std::int32_t>(std::lround(bounds.y - owner_bounds.y));
    geometry_.width = static_cast<std::int32_t>(std::lround(bounds.width));
    geometry_.height = static_cast<std::int32_t>(std::lround(bounds.height));
    geometry_.maximized = window_->maximized;
    if (geometryChanged) geometryChanged(geometry_);
}

void MsgPreviewWindow::open()
{
    if (visible_) return;
    // ネイティブ・SDLのウィンドウがまだ非表示の間に、最
    // 初の完成したフレームを用意する。 更新前に表示すると
    // 、一回の表示周期でホストの空の背景が見えてしまう。
    refresh();
    visible_ = true;
    if (window_) {
        window_->initially_hidden = false;
        window_->set_visible(true);
        if (context_.services.gui) context_.services.gui->show(window_);
    }
    if (visibilityChanged) visibilityChanged(true);
}

void MsgPreviewWindow::close()
{
    if (!visible_) return;
    const std::weak_ptr<int> lifetime = close_lifetime_;
    saveWin();
    if (lifetime.expired()) return;
    visible_ = false;
    const auto window = window_;
    const auto gui = context_.services.gui;
    if (window) {
        // モデルの表示フラグだけでは、既存のSDLウィンドウは非表示にならない。
        if (!lib::editor_window_host_options().all_mode && gui) gui->hide(window);
        else window->set_visible(false);
    }
    // 非表示化のコールバックが、このプレビューを無効化・置換・再表示する場合がある。
    if (lifetime.expired() || window_ != window || visible_) return;
    const auto notify = visibilityChanged;
    if (notify) notify(false);
}

void MsgPreviewWindow::dispose()
{
    const std::weak_ptr<int> lifetime = close_lifetime_;
    close();
    if (lifetime.expired()) return;
    const auto gui = context_.services.gui;
    const auto window = window_;
    if (disconnect_owner_move_) {
        auto disconnect = std::exchange(disconnect_owner_move_, {});
        disconnect();
    }
    if (gui && window) gui->close(window);
    if (lifetime.expired()) return;
    window_.reset();
    preview_.reset();
    close_lifetime_.reset();
}

void MsgPreviewWindow::text(const CardImageList& paths, bool single_line,
                            String message, bool center_x, bool center_y,
                            bool boundary_check)
{
    if (preview_) {
        preview_->text(paths, single_line, std::move(message), center_x, center_y,
                       boundary_check, visible_);
        if (visible_ && preview_image_view_)
            presentMessagePreview(preview_image_view_, preview_->image());
    }
}

void MsgPreviewWindow::refresh()
{
    if (!preview_) return;
    preview_->refresh();
    if (preview_image_view_)
        presentMessagePreview(preview_image_view_, preview_->image());
}

PreviewValues::PreviewValues(MessageContext context, bool message,
                             std::vector<SPChar> target_chars)
    : context_(std::move(context)),
      props_lifetime_(context_.props ? context_.props->lifetimeToken() : std::weak_ptr<void>{}),
      editor_commons_lifetime_(context_.editor_commons
          ? context_.editor_commons->lifetimeToken() : std::weak_ptr<const int>{}),
      message_(message),
      target_chars_(std::move(target_chars))
{
    if (target_chars_.empty()) {
        const bool legacy = context_.summary && context_.summary->legacy();
        target_chars_ = defaultSPChars(message_, legacy);
    }
    rebuild();
    initial_ = getValues2();
    history_.push_back(initial_);
    refUndoMax();
    if (auto* comm = context_.editor_commons) {
        const std::weak_ptr<bool> lifetime = notification_lifetime_;
        const auto bind = [this, lifetime](auto& signal, auto callback) {
            const auto token = signal.add([lifetime, callback](auto&&...) {
                const auto alive = lifetime.lock();
                if (alive && *alive) callback();
            });
            disconnect_notifications_.push_back([source = &signal, token] { source->remove(token); });
        };
        // D版と同様、シーンに結び付いた Summary の場合だけ、この更新を受け付ける。 ContentInitialValueEditor
        // は、パスのない仮の Summary を使う。
        if (context_.summary && !context_.summary->scenarioPath().empty()) {
            bind(comm->refFlagAndStep, [this] { refFlagAndStep(); });
            bind(comm->delFlagAndStep, [this] { refFlagAndStep(); });
        }
        bind(comm->refDataVersion, [this] { refDataVersion(); });
        bind(comm->refUndoMax, [this] { refUndoMax(); });
    }
}

PreviewValues::~PreviewValues() {
    cancelLiveVariantEdit(false);
    *notification_lifetime_ = false;
    // どちらのホストモードでも、この接続解除処理が保持するシ
    // グナルのアドレスは、Commons の存続中だけ有効。
    if (!editor_commons_lifetime_.expired())
        for (const auto& disconnect : disconnect_notifications_) disconnect();
    if (table_) { table_->on_inline_editor_created = {}; table_->on_cell_edited = {}; table_->on_focus = {}; table_->on_blur = {};
        table_->on_pointer = {}; table_->on_key_down = {}; table_->on_column_resized = {}; }
    if (values_menu_) { values_menu_->on_open = {}; values_menu_->on_command = {}; values_menu_->items.clear(); }
    // D版と同様、編集時だけでなく破棄時にも保存する。先に全コールバック
    // を切り離し、 他のビューの更新が終了中のビューへ再入しないようにす
    // る。 永続化や購読先が例外を出しても、破棄でプロセスを終了させな
    // い。 両ホストモードで、プレビューの値と列幅を破棄時に保存する。
    try { savePreviewValuesOnClose(); } catch (...) {}
}

void PreviewValues::targetChars(std::vector<SPChar> value)
{
    cancelLiveVariantEdit(false);
    target_chars_ = std::move(value);
    rebuild();
    store();
}

void PreviewValues::getValues(NameMap& names, ValueMap& flags,
                              ValueMap& steps, ValueMap& variants,
                              ValueMap& sys_steps) const
{
    names = names_;
    flags = flags_;
    steps = steps_;
    variants = variants_;
    sys_steps = sys_steps_;
}

PreviewSnapshot PreviewValues::getValues2() const
{
    auto result = typed_values_;
    result.names = names_;
    result.selected_player_card_number = selected_player_card_number_;
    result.player_card_names = player_card_names_;
    return result;
}

void PreviewValues::setValues2(const PreviewSnapshot& snapshot)
{
    cancelLiveVariantEdit(false);
    applySnapshot(snapshot);
    store();
    raiseModEvent();
}

void PreviewValues::store()
{
    const auto snapshot = getValues2();
    if (!history_.empty() && history_index_ < history_.size()) {
        const auto& current = history_[history_index_];
        if (current.names == snapshot.names && current.flags == snapshot.flags && current.steps == snapshot.steps &&
            current.variants == snapshot.variants && current.flag_expansion == snapshot.flag_expansion &&
            current.step_expansion == snapshot.step_expansion &&
            current.selected_player_card_number == snapshot.selected_player_card_number &&
            current.player_card_names == snapshot.player_card_names) return;
    }
    if (!history_.empty() && history_index_ + 1 < history_.size()) {
        history_.erase(history_.begin() +
                           static_cast<std::ptrdiff_t>(history_index_ + 1),
                       history_.end());
    }
    if (history_.empty() || history_.back().names != snapshot.names ||
        history_.back().flags != snapshot.flags ||
        history_.back().steps != snapshot.steps ||
        history_.back().flag_expansion != snapshot.flag_expansion ||
        history_.back().step_expansion != snapshot.step_expansion ||
        history_.back().selected_player_card_number !=
            snapshot.selected_player_card_number ||
        history_.back().player_card_names != snapshot.player_card_names) {
        history_.push_back(snapshot);
    } else if (history_.empty() || history_.back().variants != snapshot.variants) {
        history_.push_back(snapshot);
    }
    history_index_ = history_.empty() ? 0 : history_.size() - 1;
    refUndoMax();
}

void PreviewValues::refUndoMax()
{
    if (context_.props) {
        undo_max_ = static_cast<std::size_t>(
            std::max<std::int32_t>(0, context_.props->var().undoMaxEtc));
    }
    if (history_.empty()) { history_index_ = 0; return; }
    history_index_ = std::min(history_index_, history_.size() - 1U);
    if (history_index_ > undo_max_) {
        const auto removed = history_index_ - undo_max_;
        history_.erase(history_.begin(), history_.begin() + static_cast<std::ptrdiff_t>(removed));
        history_index_ -= removed;
    }
}

void PreviewValues::saveColumnWidths()
{
    if (!table_ || !context_.props) return;
    const auto props_alive = props_lifetime_.lock();
    if (!props_alive) return;

    if (table_->columns.size() >= 2U &&
        std::isfinite(table_->columns[0].width) && std::isfinite(table_->columns[1].width)) {
        const auto width = [this](const std::size_t column) {
            return static_cast<std::int32_t>(
                std::clamp(static_cast<double>(table_->columns[column].width),
                    20.0, static_cast<double>(std::numeric_limits<std::int32_t>::max())));
        };
        if (message_) {
            context_.props->var().messageVarKindColumn = width(0);
            context_.props->var().messageVarValueColumn = width(1);
        } else {
            context_.props->var().textVarKindColumn = width(0);
            context_.props->var().textVarValueColumn = width(1);
        }
    }
}

void PreviewValues::savePreviewValues()
{
    if (context_.props) {
        const auto row_present = [this](const ValueKind kind) {
            return std::any_of(value_rows_.begin(), value_rows_.end(),
                [kind](const ValueRow& row) { return row.kind == kind; });
        };
        const auto assignName = [this](char key, auto& property) {
            if (const auto found = names_.find(key); found != names_.end())
                property = found->second;
        };
        assignName('M', context_.props->var().messageVarSelected);
        assignName('U', context_.props->var().messageVarUnselected);
        assignName('R', context_.props->var().messageVarRandom);
        assignName('C', context_.props->var().messageVarCard);
        assignName('I', context_.props->var().messageVarRef);
        assignName('T', context_.props->var().messageVarTeam);
        assignName('Y', context_.props->var().messageVarYado);
        if (row_present(ValueKind::SelectedPlayerNumber))
            context_.props->var().messageVarSelectedPlayerCardNumber =
                selected_player_card_number_;
        if (row_present(ValueKind::PlayerName) && !player_card_names_.empty())
            context_.props->var().messageVarPlayerCardName = player_card_names_;
    }
    saveColumnWidths();
    // D版と同様、プレビューの全設定を保存してから通知する。
    if (auto* comm = context_.editor_commons) comm->refPreviewValues.call();
}

void PreviewValues::savePreviewValuesOnClose()
{
    if (!table_ || !context_.props) return;
    const auto props_alive = props_lifetime_.lock();
    if (!props_alive) return;

    const auto row_present = [this](const ValueKind kind, const char name_key = 0) {
        return std::any_of(value_rows_.begin(), value_rows_.end(),
            [kind, name_key](const ValueRow& row) {
                return row.kind == kind &&
                    (kind != ValueKind::Name || row.name_key == name_key);
            });
    };
    const auto assignName = [this, &row_present](const char key, auto& property) {
        if (!row_present(ValueKind::Name, key)) return;
        if (const auto found = names_.find(key); found != names_.end())
            property = found->second;
    };
    auto& settings = context_.props->var();
    assignName('M', settings.messageVarSelected);
    assignName('U', settings.messageVarUnselected);
    assignName('R', settings.messageVarRandom);
    assignName('C', settings.messageVarCard);
    assignName('I', settings.messageVarRef);
    assignName('T', settings.messageVarTeam);
    assignName('Y', settings.messageVarYado);
    if (row_present(ValueKind::SelectedPlayerNumber))
        settings.messageVarSelectedPlayerCardNumber = selected_player_card_number_;
    if (row_present(ValueKind::PlayerName) && !player_card_names_.empty())
        settings.messageVarPlayerCardName = player_card_names_;

    saveColumnWidths();

    // D版と同様、列幅を含むプレビューの全設定を保存してから通知する。
    const auto commons_alive = editor_commons_lifetime_.lock();
    if (commons_alive && context_.editor_commons)
        context_.editor_commons->refPreviewValues.call();
}

void PreviewValues::refreshFlags()
{
    rebuild();
    raiseModEvent();
}

void PreviewValues::refFlagAndStep()
{
    rebuild();
    history_ = {getValues2()};
    history_index_ = 0;
    raiseModEvent(false);
}

void PreviewValues::refDataVersion()
{
    target_chars_ = defaultSPChars(message_, context_.summary && context_.summary->legacy());
    rebuild(false, false);
    history_ = {getValues2()};
    history_index_ = 0;
    raiseModEvent(false);
}

std::shared_ptr<lib::TextEdit> PreviewValues::createEditor(
    const std::shared_ptr<lib::Container>& parent, std::size_t column,
    String value) const
{
    auto editor = std::make_shared<lib::TextEdit>(std::move(value));
    editor->multiline = false;
    editor->accessibility_description = "preview value column " + std::to_string(column);
    if (parent) parent->add(editor);
    return editor;
}

String PreviewValues::ctrlText(const std::shared_ptr<lib::Widget>& control)
{
    if (const auto choice = std::dynamic_pointer_cast<lib::ComboBox>(control)) {
        if (choice->selected_index && *choice->selected_index < choice->items.size())
            return choice->items[*choice->selected_index].value;
        return choice->edit_text;
    }
    if (const auto text = std::dynamic_pointer_cast<lib::TextWidget>(control)) {
        return text->text;
    }
    if (const auto number = std::dynamic_pointer_cast<lib::NumberEdit>(control)) {
        std::ostringstream stream;
        stream << number->value;
        return stream.str();
    }
    return String{};
}

void PreviewValues::editEnd(std::size_t row, std::size_t column,
                            const std::shared_ptr<lib::Widget>& control)
{
    if (column != 1U || row >= value_rows_.size() || !table_ ||
        row >= table_->rows.size() || column >= table_->rows[row].cells.size())
        return;
    String previous_display = table_->rows[row].cells[column].text;
    if (inline_edit_origin_ && inline_edit_origin_->row == row &&
        inline_edit_origin_->column == column)
        previous_display = inline_edit_origin_->displayed;
    inline_edit_origin_.reset();
    commitEditedValue(row, column, ctrlText(control), std::move(previous_display));
}

void PreviewValues::commitEditedValue(std::size_t row, std::size_t column,
                                      String value, String previous_display)
{
    if (column != 1U || row >= value_rows_.size() || !table_ ||
        row >= table_->rows.size() || column >= table_->rows[row].cells.size())
        return;
    const auto binding = value_rows_[row];
    const auto previous_values = getValues2();
    if (live_variant_edit_) {
        const auto parsed = expression::variantValueFromText(value);
        if (binding.kind == ValueKind::Variant &&
            binding.key == live_variant_edit_->key && parsed.valid) {
            live_variant_edit_->history_before.clear();
            live_variant_edit_.reset();
        } else cancelLiveVariantEdit(false);
    }
    applyEditedValue(binding, std::move(value));
    refreshTable();
    const auto displayed = table_->rows[row].cells[column].text;
    // D版の引数なしの TableItem::getText() は、old
    // を column 列から読んだ後でも0列目を返す。 誤った変更判定を
    // 防ぐため、編集したセル自体を比較する。 共通の直接セル編集は、このコー
    // ルバック前に表示を反映するため、モデルの差分も保持する。 確定値を保
    // 存しつつ、Modify で通知済みの編集中の変更には履歴を重ねない。
    const auto current_values = getValues2();
    if (previous_display == displayed &&
        previous_values.names == current_values.names &&
        previous_values.flags == current_values.flags &&
        previous_values.steps == current_values.steps &&
        previous_values.variants == current_values.variants &&
        previous_values.flag_expansion == current_values.flag_expansion &&
        previous_values.step_expansion == current_values.step_expansion &&
        previous_values.selected_player_card_number == current_values.selected_player_card_number &&
        previous_values.player_card_names == current_values.player_card_names) return;
    store();
    raiseModEventThenSaveValues();
}

void PreviewValues::cancelLiveVariantEdit(bool notify)
{
    auto session = std::move(live_variant_edit_);
    if (!session) return;
    const auto found = typed_values_.variants.find(session->key);
    // D版との意図的な違いとして、Escape・キャンセルはこの未完了
    // の編集だけを戻す。 以前の確定値や別の所有者の更新は戻さない。
    if (found == typed_values_.variants.end() || found->second != session->published ||
        (!session->stored && found->second == session->original)) return;
    const bool value_changed = found->second != session->original;
    if (value_changed) {
        found->second = session->original;
        variants_[session->key] = VarValue{true,
            expression::variantValueToPreviewText(session->original), false};
    }
    // D版と同様、編集中の最初の有効なコモン値の変更で取り消し履歴の枠
    // を確保する。 キャンセル時は、プレビュー値だけでなく編集前の履歴
    // の分岐も戻し、 破棄した入力をやり直しで復元できないようにする。
    if (session->stored) {
        history_ = std::move(session->history_before);
        history_index_ = session->history_index_before;
        refUndoMax();
    }
    if (notify && (value_changed || session->stored)) raiseModEvent();
}

void PreviewValues::applyEditedValue(const ValueRow& binding, String value)
{
    switch (binding.kind) {
    case ValueKind::Name:
        names_[binding.name_key] = std::move(value);
        break;
    case ValueKind::SelectedPlayerNumber:
        try {
            const auto maximum = context_.props
                ? static_cast<int>(context_.props->looks().partyMax()) : 6;
            std::size_t used = 0;
            const auto number = std::stoull(value, &used);
            if (value.empty() || value.front() == '-' || used != value.size() || number > std::numeric_limits<std::uint32_t>::max())
                throw std::invalid_argument("invalid player number");
            selected_player_card_number_ = static_cast<std::uint32_t>(
                std::min<std::uint64_t>(number, static_cast<std::uint64_t>(std::max(0, maximum))));
        } catch (...) {
            selected_player_card_number_ = 0U;
        }
        if (context_.props) {
            sys_steps_[lowerAscii(
                context_.props->sys().selectedPlayerCardNumber())] =
                VarValue{true, std::to_string(selected_player_card_number_), false};
        }
        break;
    case ValueKind::PlayerName:
        if (binding.index < player_card_names_.size()) {
            player_card_names_[binding.index] = std::move(value);
            if (context_.props) {
                sys_steps_[lowerAscii(context_.props->sys().playerCardName(
                    static_cast<std::uint32_t>(binding.index + 1)))] =
                    VarValue{true, player_card_names_[binding.index], false};
            }
        }
        break;
    case ValueKind::Flag:
        if (value != "0" && value != "1") break;
        typed_values_.flags[binding.key] = value == "1";
        flags_[binding.key] = VarValue{true, value == "1" ? flag_on_values_[binding.key] : flag_off_values_[binding.key],
            typed_values_.flag_expansion[binding.key]};
        break;
    case ValueKind::Step:
        try {
            std::size_t consumed = 0;
            const auto index = std::stoul(value, &consumed);
            const auto& choices = step_values_[binding.key];
            if (consumed != value.size() || index >= choices.size()) break;
            typed_values_.steps[binding.key] = static_cast<std::int32_t>(index);
            steps_[binding.key] = VarValue{true, choices[index], typed_values_.step_expansion[binding.key]};
        } catch (const std::exception&) {}
        break;
    case ValueKind::Variant: {
        const auto parsed = expression::variantValueFromText(value);
        if (!parsed.valid) break;
        typed_values_.variants[binding.key] = parsed;
        variants_[binding.key] = VarValue{true, expression::variantValueToPreviewText(parsed), false};
        break;
    }
    case ValueKind::SystemStep:
        sys_steps_[binding.key] = VarValue{true, std::move(value), false};
        break;
    }
}

void PreviewValues::raiseModEvent(const bool save_values)
{
    const std::weak_ptr<bool> lifetime = notification_lifetime_;
    const auto handlers = modEvent;
    if (save_values) savePreviewValues();
    for (const auto& handler : handlers) {
        const auto alive = lifetime.lock();
        if (!alive || !*alive) return;
        if (handler) handler();
    }
    if (!save_values) {
        const auto alive = lifetime.lock();
        if (!alive || !*alive) return;
        // D版と同様、値のリセットではプレビューを永続化せずに操作可否を更新する。
        if (context_.editor_commons && !editor_commons_lifetime_.expired())
            context_.editor_commons->refreshToolBar();
    }
}

void PreviewValues::raiseModEventThenSaveValues()
{
    const std::weak_ptr<bool> lifetime = notification_lifetime_;
    raiseModEvent(false);
    const auto alive = lifetime.lock();
    if (!alive || !*alive) return;
    savePreviewValues();
}

PreviewSnapshot PreviewValues::defaultValues() const {
    if (!context_.props) return initial_;
    const auto& settings = context_.props->var();
    PreviewSnapshot result;
    result.names = {{'M', settings.messageVarSelected.INIT}, {'U', settings.messageVarUnselected.INIT},
        {'R', settings.messageVarRandom.INIT}, {'C', settings.messageVarCard.INIT},
        {'I', settings.messageVarRef.INIT}, {'T', settings.messageVarTeam.INIT}, {'Y', settings.messageVarYado.INIT}};
    result.selected_player_card_number = settings.messageVarSelectedPlayerCardNumber.INIT;
    result.player_card_names = settings.messageVarPlayerCardName.INIT;
    while (result.player_card_names.size() < player_card_names_.size()) {
        lib::Dictionary<char, String> params;
        params['N'] = std::to_string(result.player_card_names.size() + 1);
        result.player_card_names.push_back(structs::parseDollarParams(settings.messageVarPlayerCardNameDefault.value, params));
    }
    namespace vars = ::cwxeditor::cwx::flag;
    const auto root = context_.summary ? context_.summary->flagDirRoot() : vars::FlagDirPtr{};
    const auto counter = context_.use_counter_owner ? context_.use_counter_owner
        : usecounter::CounterOwner(const_cast<usecounter::UseCounter*>(context_.use_counter), [](auto*) {});
    for (const auto& value : vars::allVars<vars::Flag>(root, counter)) if (value) {
        result.flags[value->path()] = value->onOff();
        result.flag_expansion[value->path()] = value->expandSPChars();
    }
    for (const auto& value : vars::allVars<vars::Step>(root, counter)) if (value) {
        result.steps[value->path()] = static_cast<std::int32_t>(value->select());
        result.step_expansion[value->path()] = value->expandSPChars();
    }
    for (const auto& value : vars::allVars<vars::Variant>(root, counter)) if (value)
        result.variants[value->path()] = expression::VariantVal(*value);
    return result;
}

void PreviewValues::resetValues()
{
    if (table_) resetValues(table_->selection);
}

void PreviewValues::resetValuesAll()
{
    std::vector<std::size_t> indices(value_rows_.size());
    for (std::size_t index = 0; index < indices.size(); ++index)
        indices[index] = index;
    resetValues(indices);
}

void PreviewValues::resetValues(std::span<const std::size_t> indices)
{
    if (indices.empty()) return;
    cancelLiveVariantEdit(false);
    const auto defaults = defaultValues();
    auto snapshot = getValues2();
    for (const auto index : indices) {
        if (index >= value_rows_.size()) continue;
        const auto& binding = value_rows_[index];
        switch (binding.kind) {
        case ValueKind::Name:
            if (const auto found = defaults.names.find(binding.name_key);
                found != defaults.names.end())
                snapshot.names[binding.name_key] = found->second;
            else
                snapshot.names.erase(binding.name_key);
            break;
        case ValueKind::SelectedPlayerNumber:
            snapshot.selected_player_card_number =
                defaults.selected_player_card_number;
            break;
        case ValueKind::PlayerName:
            if (binding.index < defaults.player_card_names.size() &&
                binding.index < snapshot.player_card_names.size())
                snapshot.player_card_names[binding.index] =
                    defaults.player_card_names[binding.index];
            break;
        case ValueKind::Flag:
            if (const auto found = defaults.flags.find(binding.key);
                found != defaults.flags.end()) snapshot.flags[binding.key] = found->second;
            else snapshot.flags.erase(binding.key);
            break;
        case ValueKind::Step:
            if (const auto found = defaults.steps.find(binding.key);
                found != defaults.steps.end()) snapshot.steps[binding.key] = found->second;
            else snapshot.steps.erase(binding.key);
            break;
        case ValueKind::Variant:
            if (const auto found = defaults.variants.find(binding.key);
                found != defaults.variants.end()) snapshot.variants[binding.key] = found->second;
            else snapshot.variants.erase(binding.key);
            break;
        case ValueKind::SystemStep:
            break;
        }
    }
    applySnapshot(snapshot);
    store();
    // C++版の意図的な拡張として、プレビューを開いている間は値のリセットを局所的な変更に留
    // める。 別のメッセージエディタは共有 Props を引き続き読み、破棄時に保存する。
    raiseModEvent(false);
}

bool PreviewValues::isInitialValues() const
{
    return !table_ || isInitialValues(table_->selection);
}

bool PreviewValues::isInitialValuesAll() const
{
    std::vector<std::size_t> indices(value_rows_.size());
    for (std::size_t index = 0; index < indices.size(); ++index)
        indices[index] = index;
    return isInitialValues(indices);
}

bool PreviewValues::isInitialValues(std::span<const std::size_t> indices) const
{
    if (indices.empty()) return true;
    const auto defaults = defaultValues();
    const auto current = getValues2();
    const auto same_entry = [](const auto& left, const auto& right,
                               const auto& key) {
        const auto left_value = left.find(key);
        const auto right_value = right.find(key);
        if (left_value == left.end() || right_value == right.end())
            return left_value == left.end() && right_value == right.end();
        return left_value->second == right_value->second;
    };
    for (const auto index : indices) {
        if (index >= value_rows_.size()) continue;
        const auto& binding = value_rows_[index];
        switch (binding.kind) {
        case ValueKind::Name:
            if (!same_entry(current.names, defaults.names, binding.name_key))
                return false;
            break;
        case ValueKind::SelectedPlayerNumber:
            if (current.selected_player_card_number !=
                defaults.selected_player_card_number) return false;
            break;
        case ValueKind::PlayerName:
            if (binding.index < current.player_card_names.size() &&
                binding.index < defaults.player_card_names.size() &&
                current.player_card_names[binding.index] !=
                    defaults.player_card_names[binding.index]) return false;
            break;
        case ValueKind::Flag:
            if (!same_entry(current.flags, defaults.flags, binding.key))
                return false;
            break;
        case ValueKind::Step:
            if (!same_entry(current.steps, defaults.steps, binding.key))
                return false;
            break;
        case ValueKind::Variant:
            if (!same_entry(current.variants, defaults.variants, binding.key))
                return false;
            break;
        case ValueKind::SystemStep:
            break;
        }
    }
    return true;
}

bool PreviewValues::undo()
{
    const bool had_draft = bool(live_variant_edit_);
    cancelLiveVariantEdit(false);
    if (history_.empty() || history_index_ == 0) { if (had_draft) raiseModEvent(); return false; }
    const auto target = history_index_ - 1U;
    applySnapshot(history_[target]);
    history_index_ = target;
    raiseModEventThenSaveValues();
    return true;
}

bool PreviewValues::redo()
{
    const bool had_draft = bool(live_variant_edit_);
    cancelLiveVariantEdit(false);
    if (history_.empty() || history_index_ + 1 >= history_.size()) { if (had_draft) raiseModEvent(); return false; }
    const auto target = history_index_ + 1U;
    applySnapshot(history_[target]);
    history_index_ = target;
    raiseModEventThenSaveValues();
    return true;
}

void PreviewValues::copyValues()
{
    const auto clipboard = context_.services.clipboard;
    if (!table_ || !clipboard) return;
    auto selection = table_->selection;
    std::erase_if(selection, [this](auto index) { return index >= value_rows_.size(); });
    if (selection.empty()) return;
    const auto [first, last] = std::minmax_element(selection.begin(), selection.end());
    lib::ClipboardData data;
    // D版と同様、選択が飛び飛びでも、それらを含む連続範囲をコピーする。
    for (auto row = *first; row <= *last; ++row) {
        const auto& binding = value_rows_[row];
        if (binding.kind == ValueKind::Flag)
            data.text += typed_values_.flags[binding.key] ? "true" : "false";
        else if (binding.kind == ValueKind::Step) data.text += std::to_string(typed_values_.steps[binding.key]);
        else if (row < table_->rows.size() && table_->rows[row].cells.size() > 1)
            data.text += table_->rows[row].cells[1].text;
        data.text += '\n';
    }
    auto* const commons = context_.editor_commons;
    const auto commons_lifetime = editor_commons_lifetime_;
    const auto lifetime = std::weak_ptr<bool>(notification_lifetime_);
    clipboard->write(data);
    const auto alive = lifetime.lock();
    if (!alive || !*alive || !commons || commons_lifetime.expired()) return;
    // D版と同様、コピーがクリップボードへ文字列を渡した直後に、コマンドの有効状態を更新する。
    commons->refreshToolBar();
}

void PreviewValues::pasteValues()
{
    if (!table_ || !context_.services.clipboard) return;
    auto selected = table_->selection;
    std::erase_if(selected, [this](auto index) { return index >= value_rows_.size(); });
    if (selected.empty()) return;
    const auto first = *std::min_element(selected.begin(), selected.end());
    const auto life = std::weak_ptr<bool>(notification_lifetime_);
    const auto revision = table_->row_order_revision;
    const auto values_revision = table_->row_update_revision;
    const auto text = lib::normalize_clipboard_text(context_.services.clipboard->read_text());
    const auto alive = life.lock();
    if (!alive || !*alive || !table_ || table_->row_order_revision != revision ||
        table_->row_update_revision != values_revision || text.empty()) return;
    cancelLiveVariantEdit(false);
    std::istringstream input(text);
    String line;
    std::vector<std::size_t> affected;
    for (auto row = first; row < value_rows_.size() && std::getline(input, line); ++row) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto& binding = value_rows_[row];
        if (binding.kind == ValueKind::Flag) {
            const auto boolean = lowerAscii(line);
            if (boolean == "true" || boolean == "1") applyEditedValue(binding, "1");
            else if (boolean == "false" || boolean == "0") applyEditedValue(binding, "0");
        } else applyEditedValue(binding, line);
        affected.push_back(row);
    }
    refreshTable();
    table_->selection = std::move(affected);
    if (!table_->selection.empty()) {
        table_->selection_anchor = table_->selection.front();
        table_->selection_cursor = table_->selection.back();
        table_->top_index = std::min(table_->top_index, table_->selection.front());
    }
    store();
    // D版と同様、貼り付けで現在のプレビューを更新し、終了時にプレビュー設定を保存する。
    raiseModEvent(false);
}

void PreviewValues::valuesCommand(StringView command)
{
    if (command == "undo") (void)undo();
    else if (command == "redo") (void)redo();
    else if (command == "reset") resetValues();
    else if (command == "reset-all") resetValuesAll();
    else if (command == "copy") copyValues();
    else if (command == "paste") pasteValues();
    else if (command == "select-all" && table_) {
        table_->selection.resize(table_->rows.size());
        for (std::size_t i = 0; i < table_->selection.size(); ++i) table_->selection[i] = i;
    }
}

void PreviewValues::refreshValuesMenu()
{
    if (!values_menu_ || !table_) return;
    const auto life = std::weak_ptr<bool>(notification_lifetime_);
    const auto clipboard = context_.services.clipboard;
    const bool has_text = clipboard && clipboard->contains("text/plain");
    const auto alive = life.lock();
    if (!alive || !*alive) return;
    for (auto& item : values_menu_->items) {
        if (item.command == "undo") item.enabled = history_index_ > 0;
        else if (item.command == "redo") item.enabled = history_index_ + 1 < history_.size();
        else if (item.command == "reset") item.enabled = !isInitialValues();
        else if (item.command == "reset-all") item.enabled = !isInitialValuesAll();
        else if (item.command == "copy") item.enabled = !table_->selection.empty() && context_.services.clipboard;
        else if (item.command == "paste") item.enabled = !table_->selection.empty() && has_text;
        else if (item.command == "select-all") item.enabled = table_->selection.size() < table_->rows.size();
    }
    ++values_menu_->item_update_revision;
}

std::shared_ptr<lib::TableView> PreviewValues::control()
{
    if (!table_) {
        table_ = std::make_shared<lib::TableView>();
        const float kind_width = context_.props
            ? static_cast<float>(message_
                ? context_.props->var().messageVarKindColumn.value
                : context_.props->var().textVarKindColumn.value)
            : 205.0F;
        const float value_width = context_.props
            ? static_cast<float>(message_
                ? context_.props->var().messageVarValueColumn.value
                : context_.props->var().textVarValueColumn.value)
            : 235.0F;
        table_->columns = {{context_.props
                                ? context_.props->requiredMsgs().messageVarKindColumn.value
                                : String{"状態変数"}, kind_width},
                           {context_.props
                                ? context_.props->requiredMsgs().messageVarValueColumn.value
                                : String{"サンプル値"}, value_width}};
        const float table_height = context_.props
            ? static_cast<float>(context_.props->var().messageVarTableHeight.value)
            : 300.0F;
        auto table_item = table_->layout_item();
        table_item.minimum.height = std::max(1.0F, table_height);
        table_item.preferred.height = table_item.minimum.height;
        table_->set_layout_item(table_item);
        // D版と同様、列幅はサイズ変更時ではなく、プレビューの値とともに保存する。
        table_->full_row_selection = true;
        table_->multi_select = true;
        table_->inline_edit_column = 1U;
        table_->on_focus = [this] { focus_in_values_ = true; };
        table_->on_blur = [this] { focus_in_values_ = false; };
        values_menu_ = std::make_shared<lib::Menu>(true);
        const auto add = [this](types::MenuID id, String command, String fallback) {
            values_menu_->items.push_back({context_.props ? context_.props->buildMenu(id) : fallback,
                std::move(command), {}, context_.props ? context_.props->images().menu(id) : lib::ResourceId{}});
        };
        add(types::MenuID::Undo, "undo", "元に戻す"); add(types::MenuID::Redo, "redo", "やり直す");
        values_menu_->items.push_back({"", "", {}, {}, true, false, false, true});
        add(types::MenuID::ResetValues, "reset", "選択した値を初期化");
        add(types::MenuID::ResetValuesAll, "reset-all", "すべての値を初期化");
        values_menu_->items.push_back({"", "", {}, {}, true, false, false, true});
        add(types::MenuID::Copy, "copy", "コピー"); add(types::MenuID::Paste, "paste", "貼り付け");
        values_menu_->items.push_back({"", "", {}, {}, true, false, false, true});
        add(types::MenuID::SelectAll, "select-all", "すべて選択");
        values_menu_->on_open = [this] { refreshValuesMenu(); };
        values_menu_->on_command = [this](StringView command) { valuesCommand(command); };
        table_->on_pointer = [this](lib::PointerEvent& event) {
            if (event.action != lib::PointerAction::Down || event.button != lib::PointerButton::Secondary || !context_.services.gui) return;
            const auto life = std::weak_ptr<bool>(notification_lifetime_);
            refreshValuesMenu();
            const auto alive = life.lock();
            if (!alive || !*alive) { event.handled = true; return; }
            event.handled = true;
            context_.services.gui->show_context_menu(values_menu_, table_, event.position);
        };
        table_->on_key_down = [this, life = std::weak_ptr<bool>(notification_lifetime_),
                               weak_table = std::weak_ptr<lib::TableView>(table_)](
                                  lib::KeyEvent& key) {
            const auto entry_alive = life.lock();
            if (!entry_alive || !*entry_alive || key.handled) return;
            const std::array<std::pair<types::MenuID, StringView>, 7> commands{{
                {types::MenuID::Undo,"undo"}, {types::MenuID::Redo,"redo"},
                {types::MenuID::ResetValues,"reset"}, {types::MenuID::ResetValuesAll,"reset-all"},
                {types::MenuID::Copy,"copy"}, {types::MenuID::Paste,"paste"}, {types::MenuID::SelectAll,"select-all"}}};
            for (const auto& [id, command] : commands) {
                bool matches = false;
                if (context_.props) matches = dmenu::eqAcc(static_cast<int>(dmenu::convertAccelerator(context_.props->buildMenu(id))),
                    static_cast<int>(key.key), acceleratorCharacter(key), static_cast<std::uint32_t>(key.modifiers));
                else if (key.modifiers == lib::Modifiers::Control) matches =
                    (id == types::MenuID::Undo && key.key == lib::Key::Z) ||
                    (id == types::MenuID::Redo && key.key == lib::Key::Y) ||
                    (id == types::MenuID::Copy && key.key == lib::Key::C) ||
                    (id == types::MenuID::Paste && key.key == lib::Key::V) ||
                    (id == types::MenuID::SelectAll && key.key == lib::Key::A);
                if (!matches) continue;
                // どのホストでも、開いているインライン編集欄が取り消し・やり直しのキーを管理する。
                if (id == types::MenuID::Undo || id == types::MenuID::Redo) {
                    const auto table = weak_table.lock();
                    const auto gui = context_.services.gui;
                    const bool inline_edit = table && gui &&
                        gui->programmatic_inline_edit_active(table);
                    // バックエンドへの問い合わせがコールバックを実行する場合がある
                    // 。 メンバーに再び触れる前に、プレビューの寿命と表が同じもの
                    // かを確認する。 ここで抑止するのは取り消し・やり直しだけ。
                    const auto alive = life.lock();
                    if (!alive || !*alive || !table || table_ != table) return;
                    if (inline_edit) return;
                }
                key.handled = true;
                valuesCommand(command);
                return;
            }
        };
        table_->commit_unchanged_inline_edits = true;
        table_->on_inline_editor_created = [this, life = std::weak_ptr<bool>(notification_lifetime_),
                                                weak_table = std::weak_ptr<lib::TableView>(table_)](
            std::size_t row, std::size_t column, const std::shared_ptr<lib::TextEdit>& editor,
            const std::shared_ptr<lib::ComboBox>& combo) {
            const auto alive = life.lock();
            if (!alive || !*alive || column != 1 || row >= value_rows_.size()) return;
            const auto initial_binding = value_rows_[row];
            if (combo &&
                (initial_binding.kind == ValueKind::Flag || initial_binding.kind == ValueKind::Step) &&
                context_.parent_history_hooks) {
                const auto hooks = context_.parent_history_hooks;
                const auto previous = combo->on_key_down;
                combo->on_key_down = [this, life, hooks, previous](lib::KeyEvent& key) {
                    const auto alive = life.lock();
                    if (!alive || !*alive || key.handled) return;
                    if (!context_.props) {
                        if (previous) previous(key);
                        return;
                    }
                    const auto character = acceleratorCharacter(key);
                    const auto modifiers = static_cast<std::uint32_t>(key.modifiers);
                    const auto matches = [&](const types::MenuID id) {
                        return dmenu::eqAcc(static_cast<int>(dmenu::convertAccelerator(
                            context_.props->buildMenu(id))), static_cast<int>(key.key),
                            character, modifiers);
                    };
                    if (matches(types::MenuID::Undo)) {
                        const auto action = hooks->undo;
                        key.handled = true;
                        if (action) action();
                    } else if (matches(types::MenuID::Redo)) {
                        const auto action = hooks->redo;
                        key.handled = true;
                        if (action) action();
                    } else if (previous) previous(key);
                };
            }
            // 上のコンボの履歴振り分けは、TextEdit としての表示に依存し
            // ない。 下のテキストのキャンセル・接続経路は、元の順序を維持する。
            if (!editor) return;
            const auto table_before_cancel = weak_table.lock();
            if (!table_before_cancel || table_ != table_before_cancel) return;
            const auto row_revision = table_before_cancel->row_order_revision;
            cancelLiveVariantEdit(false);
            const auto still_alive = life.lock();
            const auto table_after_cancel = weak_table.lock();
            if (!still_alive || !*still_alive || !table_after_cancel ||
                table_ != table_after_cancel ||
                table_after_cancel->row_order_revision != row_revision ||
                row >= value_rows_.size()) return;
            if (row >= table_after_cancel->rows.size() ||
                column >= table_after_cancel->rows[row].cells.size()) return;
            const auto& binding = value_rows_[row];
            if (binding.kind != initial_binding.kind || binding.name_key != initial_binding.name_key ||
                binding.key != initial_binding.key || binding.index != initial_binding.index) return;
            inline_edit_origin_ = InlineEditOrigin{
                row, column, table_after_cancel->rows[row].cells[column].text};
            // D版の表の ModifyListener と同様、単独版
            // ・ALL のどちらでも、入力中に名前の変更を反映する。
            if (binding.kind == ValueKind::Name) {
                struct NameEdit {
                    char key{};
                    String published;
                    bool active{true};
                    bool stored{};
                    std::size_t history_index{};
                    std::size_t row_order_revision{};
                };
                const auto found = names_.find(binding.name_key);
                if (found == names_.end()) return;
                const auto session = std::make_shared<NameEdit>(NameEdit{binding.name_key, found->second});
                session->row_order_revision = table_->row_order_revision;
                editor->on_changed = [this, life, session, row, weak_table = std::weak_ptr<lib::TableView>(table_)](StringView text) {
                    const auto alive = life.lock();
                    const auto table = weak_table.lock();
                    if (!alive || !*alive || !session->active || !table || row >= table->rows.size()) return;
                    if (table->row_order_revision != session->row_order_revision || row >= value_rows_.size() ||
                        value_rows_[row].kind != ValueKind::Name || value_rows_[row].name_key != session->key) return;
                    const auto found = names_.find(session->key);
                    if (found == names_.end() || found->second != session->published || text == session->published) return;
                    if (session->stored && (history_index_ != session->history_index || history_index_ >= history_.size())) return;
                    session->published = String(text);
                    found->second = session->published;
                    if (table->rows[row].cells.size() > 1) table->rows[row].cells[1].text = session->published;
                    if (inline_edit_origin_ && inline_edit_origin_->row == row &&
                        inline_edit_origin_->column == 1U)
                        inline_edit_origin_->displayed = session->published;
                    // 取り消し可能な一回の編集として扱い、キー入力のたびに表示と設定を更新する。
                    if (!session->stored) {
                        store();
                        session->stored = true;
                        session->history_index = history_index_;
                    } else history_[history_index_] = getValues2();
                    raiseModEventThenSaveValues();
                };
                editor->on_inline_edit_finished = [this, life, session](bool) {
                    const auto alive = life.lock();
                    if (!alive || !*alive) return;
                    // D版と同様、Escape は編集欄を閉じるが、反映済みの名前は戻さない。
                    session->active = false;
                    inline_edit_origin_.reset();
                };
                return;
            }
            if (binding.kind != ValueKind::Variant) return;
            const auto found = typed_values_.variants.find(binding.key);
            if (found == typed_values_.variants.end()) return;
            auto session = std::make_shared<LiveVariantEdit>(LiveVariantEdit{
                binding.key, found->second, found->second});
            live_variant_edit_ = session;
            editor->on_changed = [this, life, session, row](std::string_view text) {
                const auto alive = life.lock();
                if (!alive || !*alive || live_variant_edit_ != session) return;
                const auto parsed = expression::variantValueFromText(String{text});
                if (!parsed.valid || parsed == session->published) return;
                const auto found = typed_values_.variants.find(session->key);
                if (found == typed_values_.variants.end() || found->second != session->published) return;
                if (session->stored &&
                    (history_index_ != session->history_index ||
                     history_index_ >= history_.size())) return;
                if (!session->stored) {
                    // D版と同様、最初の有効なコモン値の変更で編集前の値を保存する
                    // 。 その後の編集で元の値に戻った場合も、この記録を維持する。
                    session->history_before = history_;
                    session->history_index_before = history_index_;
                    if (history_index_ + 1U < history_.size())
                        history_.erase(history_.begin() +
                                           static_cast<std::ptrdiff_t>(history_index_ + 1U),
                                       history_.end());
                    history_.push_back(getValues2());
                    history_index_ = history_.size() - 1U;
                    refUndoMax();
                    session->stored = true;
                    session->history_index = history_index_;
                }
                session->published = parsed;
                found->second = parsed;
                const auto displayed = expression::variantValueToPreviewText(parsed);
                variants_[session->key] = VarValue{true, displayed, false};
                if (inline_edit_origin_ && inline_edit_origin_->row == row &&
                    inline_edit_origin_->column == 1U)
                    inline_edit_origin_->displayed = displayed;
                // 描画だけを更新する。表の行を再反映すると、編集欄が無効になる。
                if (session->stored && history_index_ == session->history_index &&
                    history_index_ < history_.size())
                    history_[history_index_] = getValues2();
                raiseModEventThenSaveValues();
            };
            editor->on_inline_edit_finished = [this, life, session](bool accepted) {
                const auto alive = life.lock();
                if (!alive || !*alive || live_variant_edit_ != session) return;
                if (accepted) {
                    session->history_before.clear();
                    live_variant_edit_.reset();
                }
                else cancelLiveVariantEdit();
                inline_edit_origin_.reset();
            };
        };
        table_->on_cell_edited = [this](std::size_t row, std::size_t column,
                                        String value) {
            if (column != 1 || row >= value_rows_.size() || !table_ ||
                row >= table_->rows.size() || column >= table_->rows[row].cells.size())
                return;
            String previous_display = table_->rows[row].cells[column].text;
            if (inline_edit_origin_ && inline_edit_origin_->row == row &&
                inline_edit_origin_->column == column)
                previous_display = inline_edit_origin_->displayed;
            inline_edit_origin_.reset();
            commitEditedValue(row, column, std::move(value),
                              std::move(previous_display));
        };
    }
    if (!live_variant_edit_) refreshTable();
    return table_;
}

void PreviewValues::refreshTable()
{
    if (!table_) return;
    std::vector<ValueRow> selected_bindings;
    for (const auto index : table_->selection)
        if (index < value_rows_.size()) selected_bindings.push_back(value_rows_[index]);
    std::vector<lib::TableRow> rows;
    std::vector<ValueRow> value_rows;
    const auto appendRow = [&rows](String label, String value, lib::ResourceId icon,
                                  lib::CellEditorKind editor = lib::CellEditorKind::Text,
                                  std::vector<String> choices = {}, double minimum = 0.0,
                                  double maximum = 0.0) {
        lib::ListItem kind_cell;
        kind_cell.text = std::move(label);
        kind_cell.icon = icon;
        lib::ListItem value_cell;
        value_cell.text = std::move(value);
        value_cell.editor = editor;
        value_cell.editor_choices = std::move(choices);
        value_cell.editor_minimum = minimum;
        value_cell.editor_maximum = maximum;
        rows.push_back({{std::move(kind_cell), std::move(value_cell)}});
    };
    const auto labelFor = [this](char key) -> String {
        if (context_.props) {
            const auto& messages = context_.props->requiredMsgs();
            switch (key) {
            case 'M': return messages.scTalkerName(types::Talker::Selected);
            case 'U': return messages.scTalkerName(types::Talker::Unselected);
            case 'R': return messages.scTalkerName(types::Talker::Random);
            case 'C': return messages.scTalkerName(types::Talker::Card);
            case 'I': return messages.scRef.value;
            case 'T': return messages.scTeam.value;
            case 'Y': return messages.scYado.value;
            default: break;
            }
        }
        switch (key) {
        case 'M': return "選択メンバ名(#M)";
        case 'U': return "選択外ランダムメンバ名(#U)";
        case 'R': return "ランダムメンバ名(#R)";
        case 'C': return "選択カード名(#C)";
        case 'I': return "話者(#I)";
        case 'T': return "チーム名(#T)";
        case 'Y': return "宿屋名(#Y)";
        default: return String{"#"} + key;
        }
    };
    for (const auto target : target_chars_) {
        const auto key = C_TBL[static_cast<std::size_t>(target)];
        const auto found = names_.find(key);
        if (found == names_.end()) continue;
        lib::ResourceId icon{};
        if (context_.props) {
            switch (key) {
            case 'M': icon = context_.props->images().load(StringView{"sc_m.png"}); break;
            case 'U': icon = context_.props->images().load(StringView{"sc_u.png"}); break;
            case 'R': icon = context_.props->images().load(StringView{"sc_r.png"}); break;
            case 'C': icon = context_.props->images().load(StringView{"sc_c.png"}); break;
            case 'I': icon = context_.props->images().load(StringView{"sc_i.png"}); break;
            case 'T': icon = context_.props->images().load(StringView{"sc_t.png"}); break;
            case 'Y': icon = context_.props->images().load(StringView{"sc_y.png"}); break;
            default: break;
            }
        }
        appendRow(labelFor(key), found->second, icon);
        value_rows.push_back({ValueKind::Name, key, {}, 0});
    }
    if (include_player_values_) {
        const auto selectedPlayerLabel = context_.props
            ? context_.props->requiredMsgs().selectedPlayerCardNumber.value
            : String{"選択中PC番号"};
        appendRow(selectedPlayerLabel, std::to_string(selected_player_card_number_),
                  context_.props ? context_.props->images().load(StringView{"sc_sel_pc_num.png"})
                                 : lib::ResourceId{},
                  lib::CellEditorKind::Number, {}, 0.0,
                  context_.props ? static_cast<double>(context_.props->looks().partyMax())
                                 : 6.0);
        value_rows.push_back({ValueKind::SelectedPlayerNumber, 0, {}, 0});
        for (std::size_t index = 0; index < player_card_names_.size(); ++index) {
            const auto playerNameLabel = context_.props
                ? utils::tryFormat(context_.props->requiredMsgs().playerCardName.value,
                                   index + 1)
                : String{"プレイヤーカード名"} + std::to_string(index + 1);
            appendRow(playerNameLabel, player_card_names_[index],
                      context_.props ? context_.props->images().load(StringView{"sc_pc_name.png"})
                                     : lib::ResourceId{});
            value_rows.push_back({ValueKind::PlayerName, 0, {}, index});
        }
    }
    const auto appendValues = [this, &appendRow, &value_rows, &rows](const ValueMap& values,
                                                 ValueKind kind,
                                                 StringView prefix) {
        std::vector<String> keys;
        keys.reserve(values.size());
        for (const auto& [key, value] : values) {
            if (value.exists) keys.push_back(key);
        }
        const bool logical = context_.props && context_.props->var().logicalSort.value;
        std::stable_sort(keys.begin(), keys.end(), [logical](const String& left,
                                                             const String& right) {
            return ::cwxeditor::cwx::flag::pathCompare(left, right, logical) < 0;
        });
        for (const auto& key : keys) {
            const auto& value = values.at(key);
            lib::ResourceId icon{};
            lib::CellEditorKind editor = lib::CellEditorKind::Text;
            std::vector<String> choices;
            if (context_.props) {
                if (kind == ValueKind::Flag) icon = context_.props->images().load(StringView{"flag.png"});
                else if (kind == ValueKind::Step) icon = context_.props->images().load(StringView{"step.png"});
                else if (kind == ValueKind::Variant) icon = context_.props->images().load(StringView{"variant.png"});
            }
            if (kind == ValueKind::Flag) {
                editor = lib::CellEditorKind::Choice;
                if (const auto on = flag_on_values_.find(key);
                    on != flag_on_values_.end()) choices.push_back(on->second);
                if (const auto off = flag_off_values_.find(key);
                    off != flag_off_values_.end())
                    choices.push_back(off->second);
            } else if (kind == ValueKind::Step) {
                editor = lib::CellEditorKind::Choice;
                if (const auto found = step_values_.find(key);
                    found != step_values_.end()) choices = found->second;
            }
            appendRow(String(prefix) + key, kind == ValueKind::Variant
                ? expression::variantValueToText(typed_values_.variants.at(key)) : value.value, icon, editor,
                      std::move(choices));
            auto& cell = rows.back().cells[1];
            if (kind == ValueKind::Flag) {
                cell.editor_value = typed_values_.flags.at(key) ? "1" : "0";
                cell.editor_choice_values = {"1", "0"};
            } else if (kind == ValueKind::Step) {
                cell.editor_value = std::to_string(typed_values_.steps.at(key));
                for (std::size_t i = 0; i < cell.editor_choices.size(); ++i)
                    cell.editor_choice_values.push_back(std::to_string(i));
            } else if (kind == ValueKind::Variant) {
                cell.editor_value = expression::variantValueToText(typed_values_.variants.at(key));
            }
            value_rows.push_back({kind, 0, key, 0});
        }
    };
    appendValues(variants_, ValueKind::Variant, {});
    appendValues(steps_, ValueKind::Step, {});
    appendValues(flags_, ValueKind::Flag, {});
    std::vector<std::size_t> selection;
    for (std::size_t index = 0; index < value_rows.size(); ++index) {
        const auto& current = value_rows[index];
        const auto found = std::find_if(
            selected_bindings.begin(), selected_bindings.end(),
            [&current](const ValueRow& previous) {
                return current.kind == previous.kind &&
                       current.name_key == previous.name_key &&
                       current.key == previous.key && current.index == previous.index;
            });
        if (found != selected_bindings.end()) selection.push_back(index);
    }
    auto publication = lib::prepare_table_rows(table_, std::move(rows),
                                               std::move(selection));
    if (!publication.commit()) return;
    value_rows_.swap(value_rows);
}

void PreviewValues::rebuild(bool preserve_names, bool preserve_typed_values)
{
    cancelLiveVariantEdit(false);
    const auto previous = (!names_.empty() || !flags_.empty() || !steps_.empty() || !variants_.empty())
                              ? std::optional<PreviewSnapshot>(getValues2())
                              : std::nullopt;
    names_.clear();
    flags_.clear();
    steps_.clear();
    variants_.clear();
    sys_steps_.clear();
    flag_on_values_.clear();
    flag_off_values_.clear();
    step_values_.clear();
    typed_values_ = {};
    if (!context_.props) {
        for (const auto target : target_chars_) {
            const auto key = C_TBL[static_cast<std::size_t>(target)];
            names_.emplace(key, String{"#"} + key);
        }
        refreshTable();
        return;
    }
    selected_player_card_number_ =
        context_.props->var().messageVarSelectedPlayerCardNumber;
    player_card_names_ = context_.props->var().messageVarPlayerCardName;
    if (previous && preserve_names) {
        selected_player_card_number_ = previous->selected_player_card_number;
        player_card_names_ = previous->player_card_names;
    }
    include_player_values_ =
        context_.props->isTargetVersion(context_.summary, "2");
    if (include_player_values_) {
        const auto party_max = context_.props->looks().partyMax();
        while (player_card_names_.size() < party_max) {
            lib::Dictionary<char, String> params;
            params.emplace('N', std::to_string(player_card_names_.size() + 1));
            player_card_names_.push_back(structs::parseDollarParams(
                context_.props->var().messageVarPlayerCardNameDefault.value,
                params));
        }
        if (player_card_names_.size() > party_max)
            player_card_names_.resize(party_max);
        sys_steps_[lowerAscii(
            context_.props->sys().selectedPlayerCardNumber())] =
            VarValue{true, std::to_string(selected_player_card_number_), false};
        for (std::size_t index = 0; index < player_card_names_.size(); ++index) {
            sys_steps_[lowerAscii(context_.props->sys().playerCardName(
                static_cast<std::uint32_t>(index + 1)))] =
                VarValue{true, player_card_names_[index], false};
        }
    }
    msgutils::VarGetter flags;
    msgutils::VarGetter steps;
    msgutils::VarGetter variants;
    msgutils::VarGetter sys_steps;
    getPreviewValues(*context_.props, context_.summary, context_.use_counter,
                     target_chars_, names_, flags, steps, variants, sys_steps);
    if (previous && preserve_names)
        for (auto& [key, value] : names_)
            if (const auto old = previous->names.find(key); old != previous->names.end()) value = old->second;

    const auto root = context_.summary ? context_.summary->flagDirRoot() : nullptr;
    namespace vars = ::cwxeditor::cwx::flag;
    const auto counter = context_.use_counter_owner ? context_.use_counter_owner
        : usecounter::CounterOwner(const_cast<usecounter::UseCounter*>(context_.use_counter), [](auto*) {});
        for (const auto& item : vars::allVars<vars::Flag>(root, counter)) {
            if (!item) continue;
            const auto path = item->path();
            flag_on_values_[path] = item->on();
            flag_off_values_[path] = item->off();
            bool value = item->onOff();
            if (previous && preserve_typed_values) {
                if (const auto found = previous->flags.find(path);
                    found != previous->flags.end())
                    value = found->second;
            }
            typed_values_.flags[path] = value;
            typed_values_.flag_expansion[path] = item->expandSPChars();
            flags_[path] = VarValue{true, value ? item->on() : item->off(),
                                    item->expandSPChars()};
        }
        for (const auto& item : vars::allVars<vars::Step>(root, counter)) {
            if (!item) continue;
            const auto path = item->path();
            step_values_[path] = item->values();
            auto selected = item->select();
            if (previous && preserve_typed_values) {
                if (const auto found = previous->steps.find(path);
                    found != previous->steps.end() && found->second >= 0)
                    selected = static_cast<std::uint32_t>(found->second);
            }
            const auto& values = step_values_[path];
            selected = values.empty() ? 0U : std::min(selected, static_cast<std::uint32_t>(values.size() - 1));
            typed_values_.steps[path] = static_cast<std::int32_t>(selected);
            typed_values_.step_expansion[path] = item->expandSPChars();
            steps_[path] = VarValue{
                true, selected < values.size() ? values[selected] : item->value(),
                item->expandSPChars()};
        }
        for (const auto& item : vars::allVars<vars::Variant>(root, counter)) {
            if (!item) continue;
            const auto path = item->path();
            auto value = expression::VariantVal(*item);
            if (previous && preserve_typed_values) {
                if (const auto found = previous->variants.find(path);
                    found != previous->variants.end())
                    value = found->second;
            }
            typed_values_.variants[path] = value;
            variants_[path] = VarValue{value.valid, expression::variantValueToPreviewText(value), false};
        }
    refreshTable();
}

void PreviewValues::applySnapshot(const PreviewSnapshot& snapshot)
{
    cancelLiveVariantEdit(false);
    typed_values_ = snapshot;
    names_ = snapshot.names;
    flags_.clear();
    steps_.clear();
    variants_.clear();
    for (const auto& [name, value] : snapshot.flags) {
        const auto on = flag_on_values_.find(name);
        const auto off = flag_off_values_.find(name);
        flags_[name] = VarValue{
            true,
            value ? (on != flag_on_values_.end() ? on->second : "true")
                  : (off != flag_off_values_.end() ? off->second : "false"),
            typed_values_.flag_expansion[name]};
    }
    for (const auto& [name, value] : snapshot.steps) {
        const auto choices = step_values_.find(name);
        const auto index = static_cast<std::size_t>(std::max<std::int32_t>(0, value));
        steps_[name] = VarValue{
            true,
            choices != step_values_.end() && index < choices->second.size()
                ? choices->second[index]
                : std::to_string(value),
            typed_values_.step_expansion[name]};
    }
    for (const auto& [name, value] : snapshot.variants) {
        variants_[name] = VarValue{
            value.valid, expression::variantValueToPreviewText(value), false};
    }
    selected_player_card_number_ = snapshot.selected_player_card_number;
    player_card_names_ = snapshot.player_card_names;
    if (context_.props && include_player_values_) {
        sys_steps_[lowerAscii(
            context_.props->sys().selectedPlayerCardNumber())] =
            VarValue{true, std::to_string(selected_player_card_number_), false};
        for (std::size_t index = 0; index < player_card_names_.size(); ++index) {
            sys_steps_[lowerAscii(context_.props->sys().playerCardName(
                static_cast<std::uint32_t>(index + 1)))] =
                VarValue{true, player_card_names_[index], false};
        }
    }
    refreshTable();
}

void configureVariantValueEditor(const std::shared_ptr<lib::TextEdit>& editor,
                                 String hint, String invalid_format,
                                 lib::Action on_changed) {
    if (!editor) return;
    editor->set_tooltip(hint);
    editor->on_changed = [weak = std::weak_ptr<lib::TextEdit>(editor),
                          hint = std::move(hint), invalid_format = std::move(invalid_format),
                          on_changed = std::move(on_changed)](StringView text) {
        const auto current = weak.lock();
        if (!current) return;
        const auto parsed = expression::variantValueFromText(text);
        current->set_tooltip(parsed.valid ? hint : utils::tryFormat(invalid_format, hint));
        if (on_changed) on_changed();
    };
}

std::shared_ptr<lib::TextEdit> variantValueEditor(
    const MessageContext& context, const std::shared_ptr<lib::Container>& parent,
    String value)
{
    auto editor = std::make_shared<lib::TextEdit>(std::move(value));
    editor->multiline = false;
    configureVariantValueEditor(editor,
        context.props ? context.props->requiredMsgs().variantValueHint.value : String{"Variant value"},
        context.props ? context.props->requiredMsgs().warningInvalidVariantValue.value
                      : String{"Invalid variant value\n\n%1$s"}, context.on_changed);
    if (parent) parent->add(editor);
    return editor;
}

void getPreviewValues(const Props& prop, const Summary* summ,
                      const usecounter::UseCounter* uc,
                      std::span<const SPChar> target_chars, NameMap& names,
                      msgutils::VarGetter& flags, msgutils::VarGetter& steps,
                      msgutils::VarGetter& variants,
                      msgutils::VarGetter& sys_steps)
{
    (void)uc;
    for (const auto value : target_chars) {
        switch (value) {
        case SPChar::M: names['M'] = prop.var().messageVarSelected; break;
        case SPChar::U: names['U'] = prop.var().messageVarUnselected; break;
        case SPChar::R: names['R'] = prop.var().messageVarRandom; break;
        case SPChar::C: names['C'] = prop.var().messageVarCard; break;
        case SPChar::I: names['I'] = prop.var().messageVarRef; break;
        case SPChar::T: names['T'] = prop.var().messageVarTeam; break;
        case SPChar::Y: names['Y'] = prop.var().messageVarYado; break;
        }
    }
    if (!summ) {
        flags = [](const String&) { return VarValue{}; };
        steps = [](const String&) { return VarValue{}; };
        variants = [](const String&) { return VarValue{}; };
    } else {
        flags = [summ](const String& path) {
            const auto root = summ->flagDirRoot();
            if (!root) return VarValue{};
            const auto value = root->findFlag(path);
            return value ? VarValue{true, value->onOff() ? value->on() : value->off(),
                                    value->expandSPChars()}
                         : VarValue{};
        };
        steps = [summ](const String& path) {
            const auto root = summ->flagDirRoot();
            if (!root) return VarValue{};
            const auto value = root->findStep(path);
            return value ? VarValue{true, value->value(), value->expandSPChars()}
                         : VarValue{};
        };
        variants = [summ](const String& path) {
            const auto root = summ->flagDirRoot();
            if (!root) return VarValue{};
            const auto value = root->findVariant(path);
            return value
                       ? VarValue{true, expression::variantValueToPreviewText(*value),
                                  false}
                       : VarValue{};
        };
    }
    sys_steps = getPreviewSysSteps(prop, summ, true);
}

msgutils::VarGetter getPreviewSysSteps(const Props& prop, const Summary* summ,
                                       bool expand_sharps)
{
    return [&prop, summ, expand_sharps](const String& path) {
        if (!prop.isTargetVersion(summ, "2")) return VarValue{};
        const auto lower_path = lowerAscii(path);
        if (expand_sharps &&
            lowerAscii(prop.sys().selectedPlayerCardNumber()) == lower_path) {
            return VarValue{true, numberText(prop.var().messageVarSelectedPlayerCardNumber),
                            false};
        }
        const auto max = prop.looks().partyMax();
        for (std::size_t pcn = 1; pcn <= max; ++pcn) {
            if (lowerAscii(prop.sys().playerCardName(
                               static_cast<std::uint32_t>(pcn))) != lower_path) {
                continue;
            }
            const auto& configured = prop.var().messageVarPlayerCardName.value;
            if (pcn - 1 < configured.size()) {
                return VarValue{true, configured[pcn - 1], false};
            }
            lib::Dictionary<char, String> params;
            params.emplace('N', std::to_string(pcn));
            return VarValue{true, structs::parseDollarParams(
                                     prop.var().messageVarPlayerCardNameDefault.value, params),
                            false};
        }
        return VarValue{};
    };
}

MsgPreview::MsgPreview(MessageContext context)
    : context_(std::move(context)),
      force_skin_(context_.forced_skin),
      values_(std::make_shared<PreviewValues>(context_, true))
{
    values_->modEvent.push_back([this] { refresh(); });
}

Skin::Ptr MsgPreview::summSkin() const
{
    if (force_skin_) return force_skin_;
    if (context_.current_skin) return context_.current_skin();
    if (!context_.props) return {};
    if (context_.summary && !context_.summary->type().empty()) {
        if (auto value = dskin::findSkin2(*context_.props,
                                         context_.summary->type(),
                                         context_.summary->skinName()))
            return value;
    }
    return dskin::findSkin2(*context_.props,
                            context_.props->var().defaultSkin.value,
                            context_.props->var().defaultSkinName.value);
}

void MsgPreview::refImageScale()
{
    invalidateImageCache();
    refresh();
}

void MsgPreview::refImageScaleImpl()
{
    invalidateImageCache();
    refresh();
}

void MsgPreview::invalidateImageCache()
{
    image_cache_valid_ = false;
    decoded_image_paths_.clear();
    decoded_skin_.reset();
    decoded_scale_ = 0U;
}

void MsgPreview::text(const CardImageList& paths, bool single_line,
                      String message, bool center_x, bool center_y,
                      bool boundary_check, bool refresh_now)
{
    const bool same_inputs = paths == image_paths_ &&
        single_line == single_line_ && message == message_ &&
        center_x == center_x_ && center_y == center_y_ &&
        boundary_check == boundary_check_;
    // D版と同様、同一の入力は省略する。ただし非表示中の保留された編集は、 初回の表
    // 示更新まで変更済みとし、共通のC++ホストが最新の文字列を描けるようにする。
    if (same_inputs && (!refresh_now || !text_refresh_pending_)) return;

    if (!same_inputs) text_refresh_pending_ = true;
    image_paths_ = paths;
    single_line_ = single_line;
    message_ = std::move(message);
    center_x_ = center_x;
    center_y_ = center_y;
    boundary_check_ = boundary_check;
    if (refresh_now) refresh();
}

void MsgPreview::refresh()
{
    refreshImpl();
}

void MsgPreview::refreshImpl()
{
    // 閉じたイベントエディタが保持され、ImageSelect の更新を受ける場合
    // がある。 所有者を開き直すまで現在のスキンの提供処理は停止しているため、
    // 残ったコントロールのコールバックから閉じたプレビューをラスタライズしない。
    if (context_.current_skin && !context_.current_skin()) return;
    if (!context_.props) {
        image_ = {};
        return;
    }
    // 描画前に変更状態を消費する。再入した text() は、新しい入力を変更済みにできる。
    text_refresh_pending_ = false;
    try {
        refreshValues();
    } catch (...) {
        text_refresh_pending_ = true;
        throw;
    }
    if (!image_.valid()) text_refresh_pending_ = true;
}

bool MsgPreview::focusInValues() const
{
    return values_ && values_->focusInValues();
}

bool MsgPreview::undo()
{
    return values_ && values_->undo();
}

bool MsgPreview::redo()
{
    return values_ && values_->redo();
}

PreviewSnapshot MsgPreview::valuesSnapshot() const
{
    return values_ ? values_->getValues2() : PreviewSnapshot{};
}

void MsgPreview::valuesSnapshot(const PreviewSnapshot& value)
{
    if (values_) values_->setValues2(value);
}

std::shared_ptr<lib::TableView> MsgPreview::valuesControl()
{
    return values_ ? values_->control() : std::shared_ptr<lib::TableView>{};
}

lib::AudioHandle MsgPreview::playSound(StringView path, float volume)
{
    if (!context_.services.audio || path.empty()) return 0;
    std::string error;
    if (!context_.services.audio->initialize(error)) return 0;
    lib::AudioPlayOptions options;
    options.bus = lib::AudioBus::Preview;
    options.volume = std::clamp(volume, 0.0F, 1.0F);
    audio_handle_ = context_.services.audio->play(
        lib::AudioSource{utf8Path(path), {}, {}, {}}, options);
    return audio_handle_;
}

void MsgPreview::stopSound() noexcept
{
    if (context_.services.audio && audio_handle_ != 0) {
        context_.services.audio->stop(audio_handle_);
        audio_handle_ = 0;
    }
}

void MsgPreview::refreshValues()
{
    NameMap names;
    ValueMap flags;
    ValueMap steps;
    ValueMap variants;
    ValueMap sys_steps;
    if (values_) values_->getValues(names, flags, steps, variants, sys_steps);
    const auto active_skin = summSkin();
    const auto scale = scaleOrOne(*context_.props);
    const auto same_skin = [](const Skin::Ptr& left, const Skin::Ptr& right) {
        if (!left || !right) return !left && !right;
        return left->name() == right->name() &&
               left->type() == right->type() &&
               left->skinFile() == right->skinFile() &&
               left->path() == right->path() &&
               left->engine() == right->engine() &&
               left->legacyEngine() == right->legacyEngine() &&
               left->legacyDataPath() == right->legacyDataPath() &&
               left->sourceOfMaterialsIsClassicEngine() ==
                   right->sourceOfMaterialsIsClassicEngine();
    };
    const auto same_paths = [this] {
        if (decoded_image_paths_.size() != image_paths_.size()) return false;
        for (std::size_t index = 0; index < image_paths_.size(); ++index) {
            const auto& current = image_paths_[index];
            const auto& decoded = decoded_image_paths_[index];
            if (static_cast<bool>(current) != static_cast<bool>(decoded)) return false;
            if (current && decoded && !current->opEquals(*decoded)) return false;
        }
        return true;
    };
    if (!image_cache_valid_ || decoded_scale_ != scale ||
        !same_skin(decoded_skin_, active_skin) || !same_paths()) {
        image_positions_.clear();
        talker_images_.clear();
        for (const auto& image : image_paths_) {
            if (!image) continue;
            if (image->type == types::CardImageType::File) {
                talker_images_.push_back(
                    decodeImage(context_, image->path(), scale));
                image_positions_.push_back(image->positionType());
            } else if (image->type == types::CardImageType::Talker) {
                StringView resource;
                switch (image->talker()) {
                case types::Talker::Selected: resource = "talker_sel.png"; break;
                case types::Talker::Unselected: resource = "talker_unsel.png"; break;
                case types::Talker::Random: resource = "talker_random.png"; break;
                case types::Talker::Valued: resource = "talker_valued.png"; break;
                case types::Talker::Card:
                    if (active_skin) {
                        auto menu_card = dskin::menuCard(
                            *active_skin, context_.props->drawingScale());
                        if (menu_card.valid()) {
                            talker_images_.emplace_back(
                                std::move(menu_card.data), menu_card.source_scale,
                                context_.services.graphics);
                            image_positions_.push_back(
                                types::CardImagePosition::Default);
                        }
                    }
                    continue;
                }
                talker_images_.push_back(
                    decodeEditorResource(context_, resource, scale));
                image_positions_.push_back(types::CardImagePosition::Default);
            }
        }
        decoded_image_paths_.clear();
        decoded_image_paths_.reserve(image_paths_.size());
        for (const auto& image : image_paths_) {
            decoded_image_paths_.push_back(
                image ? std::make_shared<CardImage>(nullptr, *image) : CardImagePtr{});
        }
        decoded_skin_ = active_skin;
        decoded_scale_ = scale;
        image_cache_valid_ = true;
    }
    auto next_image = previewMessage(
        context_, *context_.props, context_.summary,
        [active_skin] { return active_skin; }, talker_images_, image_positions_,
        single_line_, message_, {}, names, flags, steps, variants, sys_steps,
        center_x_, center_y_, boundary_check_, false);
    // 文字の編集ではプレビューの再ラスタライズが必要だが、バックエンドの
    // リソースを作り直す必要はない。 IDを維持し、ネイティブ・SDLの
    // 画面で破棄と生成の間の空白を避け、アップロードの繰り返しも減らす。
    const auto& graphics = context_.services.graphics;
    if (graphics && image_.value && image_.value->graphics == graphics &&
        image_.value->id != 0 && next_image.valid()) {
        graphics->update_image(image_.value->id, *next_image.data());
        image_.value->data = std::move(next_image.value->data);
        image_.scale = next_image.scale;
    } else if (next_image.value) {
        image_ = ImageDataWithScale(std::move(next_image.value->data),
                                    next_image.scale, graphics);
    } else {
        image_ = {};
    }
}

ImageDataWithScale previewMessage(
    const MessageContext& context, const Props& prop, const Summary* summ,
    const std::function<Skin::Ptr()>& skin_getter,
    std::span<const ImageDataWithScale> talkers,
    std::span<const types::CardImagePosition> poses, bool single_line,
    StringView message, std::span<const String> selections,
    const NameMap& names, const ValueMap& flags, const ValueMap& steps,
    const ValueMap& variants, const ValueMap& sys_steps, bool center_x,
    bool center_y, bool boundary_check, bool upload)
{
    // D版と同様、一行プレビューでは縦の中央寄せと境界での折り返しを無効に
    // してから、 通常の messageLen の切り取り規則を適用する。
    boundary_check = boundary_check && !single_line;
    center_y = center_y && !single_line;
    auto active_skin = skin_getter ? skin_getter() : Skin::Ptr{};
    auto formatted = normalizeMessage(String(message));
    msgutils::FontMap fonts;
    msgutils::ColorMap colors;
    const msgutils::VarGetter get_flag = [&flags](const String& path) {
        return replacement(flags, path);
    };
    const msgutils::VarGetter get_step = [&steps, &sys_steps](const String& path) {
        const auto value = replacement(steps, path);
        return value.exists ? value : replacement(sys_steps, lowerAscii(path));
    };
    const msgutils::VarGetter get_variant = [&variants](const String& path) {
        return replacement(variants, path);
    };
    const msgutils::NameGetter get_name = [&names](char value) {
        return mapName(names, value);
    };
    const msgutils::VersionPredicate is_target =
        [&prop, summ](const String& version) {
            return prop.isTargetVersion(summ, version);
        };
    const msgutils::MaterialPredicate has_material =
        [&active_skin, summ](const String& path) {
            if (!active_skin) return false;
            const auto scenario_path = summ ? summ->scenarioPath() : String{};
            const auto data_version = summ ? summ->dataVersion() : String("1.50");
            return !active_skin->findImagePath(path, scenario_path, data_version).empty() ||
                   active_skin->spChars().contains(msgutils::decodeFontPath(path));
        };
    formatted = msgutils::formatMsg(
        formatted, get_flag, get_step, get_variant, get_name, is_target,
        prop.sys().prefixSystemVarName(), has_material, fonts, colors);

    auto bounds = single_line ? prop.looks().singleLineMessageBounds()
                             : prop.looks().messageBounds();
    bounds = prop.ds(bounds);
    auto button_height = prop.ds(prop.looks().messageButtonHeight());
    auto width = nonzero(bounds.width);
    auto height = nonzero(bounds.height);
    if (!single_line && center_y) height = std::max<std::uint32_t>(height, 1);
    const auto scale = scaleOrOne(prop);
    const auto line_height = nonzero(
        static_cast<std::int32_t>(prop.ds(prop.looks().messageLineHeight())));
    const auto char_width = nonzero(
        static_cast<std::int32_t>(prop.ds(prop.looks().messageCharWidth())));
    const auto max_columns = talkers.empty()
                                 ? prop.looks().messageLen()
                                 : prop.looks().messageImageLen();
    // ローカルの boundary_check は、上のD版の一行用リセットを反映済み。
    if (boundary_check && max_columns > 1) {
        const auto target_width = static_cast<std::size_t>(
            boundary_check ? max_columns - 1 : max_columns);
        std::vector<char32_t> open;
        std::vector<char32_t> close;
        for (const auto c : decodeUtf8(prop.looks().openChars())) open.push_back(c);
        for (const auto c : decodeUtf8(prop.looks().closeChars())) close.push_back(c);
        const auto width_getter = [&](const String& value) {
            if (value.empty()) return std::size_t{};
            // D版の日本語等幅フォントは、ASCIIを一セル、日本語を二セルとし
            // て描く。 特にWebの代替フォントはその比率を満たさない場合がある
            // ため、 フォント寸法とは独立してメッセージの論理セルを維持する。
            std::size_t width{};
            for (const auto c : decodeUtf8(value)) width += c < 0x80 || (c >= 0xff61 && c <= 0xff9f) ? 1 : 2;
            return std::max<std::size_t>(1, width);
        };
        formatted = msgutils::wrapMsg(
            formatted, target_width, width_getter, boundary_check, open, close,
            boundary_check ? decodeUtf8(prop.looks().wordRegex()) : msgutils::DString{},
            fonts, colors);
    }
    const auto selection_height = static_cast<std::uint32_t>(
        std::max<std::int32_t>(1, button_height));
    const auto output_height = height +
                               selection_height *
                                   static_cast<std::uint32_t>(selections.size());
    // 上の境界・行の寸法・原点には ds() を適用済み。描画倍率
    // は一度だけ適用し、 倍率未適用の元画像の寸法だけに乗算する。
    auto image = makeRaster(width, output_height,
                            rgbColor(prop.var().messageBackColor));
    auto& frame = image.frames.front();
    frame.logical_size = {static_cast<float>(width) / scale,
                          static_cast<float>(output_height) / scale};
    const auto background = rgbColor(prop.var().messageBackColor);
    const auto border_one = rgbColor(prop.var().messageLineColor1);
    const auto border_two = rgbColor(prop.var().messageLineColor2);
    const auto foreground = rgbColor(prop.var().messageForeColor);
    const auto hemming_color = rgbColor(prop.var().messageHemColor);
    fillRect(frame, 0, 0, static_cast<std::int32_t>(frame.pixel_width),
             static_cast<std::int32_t>(frame.pixel_height), background);
    const auto draw_border = [&](std::int32_t top, std::int32_t border_height) {
        for (std::int32_t pixel = prop.ds(0); pixel < prop.ds(1); ++pixel) {
            for (int band = 0; band < 3; ++band) {
                const auto inset = prop.ds(band) + pixel;
                strokeRect(frame, inset, top + inset,
                           static_cast<std::int32_t>(width) - 2 * inset,
                           border_height - 2 * inset,
                           band == 1 ? border_two : border_one);
            }
        }
    };
    draw_border(0, static_cast<std::int32_t>(height));
    for (std::size_t i = 0; i < talkers.size(); ++i) {
        if (!talkers[i].valid()) continue;
        const auto size = talkers[i].logicalSize();
        // D版と同様、倍率適用済みの本来の話者画像を描き、メッセージのラスター領域
        // で切り取る。 プレビューに収めてしまうと、大きな話者画像が縮小される。
        const auto dimension=[](const double value) {
            return !std::isfinite(value) || value<=0 ? 0 :
                static_cast<std::int32_t>(std::clamp(value,1.0,double(std::numeric_limits<std::int32_t>::max())));
        };
        const auto draw_width=dimension(double(size.width)*scale);
        const auto draw_height=dimension(double(size.height)*scale);
        if (!draw_width || !draw_height) continue;
        const auto talker_position = prop.ds(prop.looks().messageTalkerPos());
        const auto card_size = prop.ds(prop.looks().cardSize());
        auto left = static_cast<std::int32_t>(talker_position.x);
        auto top = static_cast<std::int32_t>(talker_position.y) +
                   (static_cast<std::int32_t>(card_size.height) -
                    draw_height) /
                       2;
        if (i < poses.size() && poses[i] == types::CardImagePosition::Center) {
            left = (static_cast<std::int32_t>(width) - draw_width) / 2;
            top = (static_cast<std::int32_t>(height) - draw_height) / 2;
        }
        blitPreviewImage(talkers[i], frame, left, top, draw_width, draw_height);
    }
    auto x = static_cast<std::int32_t>(prop.ds(
        prop.looks().messageStartPos(active_skin && active_skin->legacy(),
                                     !talkers.empty(), center_x)
            .x));
    auto y = static_cast<std::int32_t>(prop.ds(
        prop.looks().messageStartPos(active_skin && active_skin->legacy(),
                                     !talkers.empty(), center_x)
            .y));
    const auto scaled_char_width = static_cast<std::int32_t>(char_width);
    const auto scaled_line_height = static_cast<std::int32_t>(line_height);
    std::int32_t line_start = x;
    String visible;
    const auto formatted_chars = decodeUtf8(formatted);
    for (std::size_t index = 0; index < formatted_chars.size(); ++index) {
        const auto current = formatted_chars[index];
        if (colors.contains(index) && index + 1 < formatted_chars.size()) {
            ++index;
            continue;
        }
        if (current == '\n') {
            x = line_start;
            y += scaled_line_height;
            visible.push_back('\n');
            continue;
        }
        const auto unit_width =
            std::max<std::int32_t>(1, scaled_char_width / (current < 0x80 ? 2 : 1));
        if (x + unit_width >= static_cast<std::int32_t>(frame.pixel_width)) {
            x = line_start;
            y += scaled_line_height;
        }
        const auto glyph_height =
            std::max<std::int32_t>(1, scaled_line_height - static_cast<std::int32_t>(2 * scale));
        // 境界計算と代替描画のための配置走査は維持する。 Windows
        // では、下で仮の字形をネイティブの字形に置き換える。
        x += unit_width;
        visible += charText(current);
        if (y + glyph_height >= static_cast<std::int32_t>(height)) break;
    }
    bool drew_native_text = false;
    if (context.services.graphics) {
        String display;
        std::vector<lib::Color> display_colors;
        struct InlineImage {
            std::size_t offset{};
            ImageDataWithScale image;
            bool hem{};
        };
        std::vector<InlineImage> inline_images;
        auto current_color = foreground;
        display.reserve(formatted.size());
        for (std::size_t index = 0; index < formatted_chars.size(); ++index) {
            const auto color = colors.find(index);
            if (color != colors.end() && index + 1 < formatted_chars.size()) {
                switch (color->second) {
                case 'W': current_color = foreground; break;
                case 'R': current_color = {255,0,0,255}; break;
                case 'B': current_color = {0,255,255,255}; break;
                case 'G': current_color = {0,255,0,255}; break;
                case 'Y': current_color = {255,255,0,255}; break;
                case 'O': current_color = {255,165,0,255}; break;
                case 'P': current_color = {204,136,255,255}; break;
                case 'L': current_color = {169,169,169,255}; break;
                case 'D': current_color = {105,105,105,255}; break;
                default: break;
                }
                ++index;
                continue;
            }
            const auto font = fonts.find(index);
            if (font != fonts.end() && index + 1 < formatted_chars.size() && active_skin) {
                ImageDataWithScale glyph_image;
                bool hem_image = false;
                const auto path = active_skin->findImagePath(font->second,
                    summ ? summ->scenarioPath() : String{},
                    summ ? summ->dataVersion() : String("1.50"));
                if (!path.empty()) {
                    auto image_context = context;
                    image_context.forced_skin = active_skin;
                    glyph_image = decodeImage(image_context, path, scale);
                }
                if (const auto builtin = active_skin->spChars().find(
                               msgutils::decodeFontPath(font->second));
                    !glyph_image.valid() && builtin != active_skin->spChars().end()) {
                    auto builtin_image = dskin::spChar(*active_skin, scale,
                        msgutils::decodeFontPath(font->second), context.services);
                    if (builtin_image.valid())
                        glyph_image = ImageDataWithScale(std::move(builtin_image.data),
                                                        builtin_image.source_scale);
                    if (glyph_image.valid()) {
                        hem_image = true;
                        for (auto& pixels : glyph_image.value->data.frames) {
                            auto tinted = makeRaster(pixels.pixel_width, pixels.pixel_height,
                                                     {0,0,0,0});
                            auto& output = tinted.frames.front();
                            output.logical_size = pixels.logical_size;
                            // ネイティブのリソースはRGBAだけでなくRGB・BGRAの場合も
                            // ある。 共通の画素読み取りを使い、マスクのアルファを保持する。
                            for (std::uint32_t py = 0; py < pixels.pixel_height; ++py) {
                                for (std::uint32_t px = 0; px < pixels.pixel_width; ++px) {
                                    auto pixel = imagePixel(pixels, px, py);
                                    if (pixel.red == foreground.red && pixel.green == foreground.green &&
                                        pixel.blue == foreground.blue) {
                                        pixel.red = current_color.red;
                                        pixel.green = current_color.green;
                                        pixel.blue = current_color.blue;
                                    }
                                    setPixel(output, px, py, pixel);
                                }
                            }
                            pixels = std::move(output);
                        }
                    }
                }
                if (glyph_image.valid()) {
                    inline_images.push_back(
                        {display.size(), std::move(glyph_image), hem_image});
                    // 内部の仮文字一つで画像のセルを確保し、
                    // その文字はテキスト描画には渡さない。
                    display.push_back('\x01');
                    display_colors.push_back(current_color);
                    ++index;
                    continue;
                }
            }
            const auto glyph = charText(formatted_chars[index]);
            display += glyph;
            display_colors.insert(display_colors.end(), glyph.size(), current_color);
        }
        const auto adjusted = prop.ds(prop.adjustFont(
            prop.looks().messageFont(active_skin && active_skin->legacy())));
        const auto drawing_font = fontSpec(adjusted);
        const auto start = prop.ds(prop.looks().messageStartPos(
            active_skin && active_skin->legacy(), !talkers.empty(), center_x));
        struct TextCell {
            String text;
            std::size_t offset{};
            std::size_t row{};
            float x{}, y{}, width{}, height{};
            const InlineImage* picture{};
        };
        std::vector<TextCell> cells;
        std::vector<float> row_widths(1, 0.0F);
        float cursor_x = 0.0F;
        float cursor_y = static_cast<float>(start.y) - 1.0F;
        std::size_t row_index = 0, columns = 0, offset = 0;
        float occupied_top = std::numeric_limits<float>::max();
        float occupied_bottom = std::numeric_limits<float>::lowest();
        const auto new_line = [&] {
            cursor_x = 0;
            cursor_y += static_cast<float>(line_height);
            columns = 0;
            ++row_index;
            row_widths.push_back(0);
        };
        for (const auto& cluster : msgutils::toGraphemeArray(display)) {
            String glyph;
            for (const auto codepoint : cluster) glyph += charText(codepoint);
            const auto begin = offset;
            offset += glyph.size();
            if (!center_y && cursor_y + line_height > height - prop.ds(6)) break;
            if (glyph == "\n") { new_line(); continue; }
            const auto picture = std::find_if(inline_images.begin(), inline_images.end(),
                [&](const auto& entry) { return entry.offset == begin; });
            const bool has_picture = picture != inline_images.end();
            const auto length = has_picture ? 2U :
                (cluster.empty() || cluster.front() < 0x80 ||
                 (cluster.front() >= 0xff61 && cluster.front() <= 0xff9f) ? 1U : 2U);
            // D版と同様、末尾のASCII空白は行の端に収まるものとして扱う。
            const auto check_length = glyph == " " && length ? length - 1 : length;
            if (!boundary_check && columns + check_length > static_cast<std::size_t>(max_columns)) {
                new_line();
                if (!center_y && cursor_y + line_height > height - prop.ds(6)) break;
            }
            // D版の TGCDrawer と同様、折り返しと同じ論理セル
            // を使い、 messageCharWidth またはその半分
            // ずつ進める。 フォントの実測幅を積み上げると、代替字形が
            // セルより広い場合に画像付きメッセージの文字領域を超える。
            const auto advance = static_cast<float>(
                has_picture || length >= 2 ? char_width : char_width / 2);
            const auto picture_size = has_picture ? picture->image.logicalSize() : lib::Size{};
            const float occupied_width = has_picture ? picture_size.width * scale : advance;
            const float occupied_height = has_picture ? picture_size.height * scale : static_cast<float>(line_height);
            cells.push_back({std::move(glyph), begin, row_index, cursor_x, cursor_y,
                             occupied_width, occupied_height, has_picture ? &*picture : nullptr});
            row_widths.back() = std::max(row_widths.back(), cursor_x + occupied_width);
            occupied_top = std::min(occupied_top, cursor_y);
            occupied_bottom = std::max(occupied_bottom, cursor_y + occupied_height);
            cursor_x += advance;
            columns += length;
        }
        // 字形の送り幅がセルと完全に一致する場合だけ、まとめて描く
        // 。 D版は各書記素をセル位置に描くため、広い文字列断片と
        // して整形すると その間隔が崩れ、右端の欠けが再発する。
        std::vector<TextCell> runs;
        runs.reserve(cells.size());
        const auto same_color = [&](const std::size_t left, const std::size_t right) {
            if (left >= display_colors.size() || right >= display_colors.size()) return false;
            const auto a = display_colors[left], b = display_colors[right];
            return a.red == b.red && a.green == b.green && a.blue == b.blue &&
                   a.alpha == b.alpha;
        };
        for (auto& cell : cells) {
            if (!cell.picture && !runs.empty() && !runs.back().picture &&
                runs.back().row == cell.row && same_color(runs.back().offset, cell.offset) &&
                std::abs(context.services.graphics->measure_text(
                    cell.text, drawing_font, 0).width - cell.width) < 0.01F &&
                std::abs(context.services.graphics->measure_text(
                    runs.back().text, drawing_font, 0).width - runs.back().width) < 0.01F &&
                std::abs(context.services.graphics->measure_text(
                    runs.back().text + cell.text, drawing_font, 0).width -
                    (runs.back().width + cell.width)) < 0.01F &&
                std::abs((runs.back().x + runs.back().width) - cell.x) < 0.01F) {
                runs.back().text += cell.text;
                runs.back().width += cell.width;
                runs.back().height = std::max(runs.back().height, cell.height);
            } else {
                runs.push_back(std::move(cell));
            }
        }
        cells = std::move(runs);
        const float slide_y = center_y && !single_line && !cells.empty()
            ? (static_cast<float>(height) - (occupied_bottom - occupied_top)) / 2 - occupied_top : 0.0F;
        const auto draw_cells = [&](float dx, float dy, bool use_colors) {
            bool painted = true;
            for (const auto& cell : cells) {
                const float cell_x = (center_x
                    ? (static_cast<float>(width) - row_widths[cell.row]) / 2
                    : static_cast<float>(start.x) - 1) + cell.x + dx;
                const float cell_y = cell.y + slide_y + dy;
                if (cell.picture) {
                    if (use_colors || cell.picture->hem) blitPreviewImage(
                        cell.picture->image, frame,
                        static_cast<std::int32_t>(std::lround(cell_x)),
                        static_cast<std::int32_t>(std::lround(cell_y)),
                        static_cast<std::int32_t>(std::lround(cell.width)),
                        static_cast<std::int32_t>(std::lround(cell.height)),
                        use_colors ? std::optional<lib::Color>{}
                                   : std::optional<lib::Color>{hemming_color});
                    continue;
                }
                const auto extent = context.services.graphics->measure_text(cell.text, drawing_font, 0);
                const lib::Rect target{cell_x, cell_y, std::max(cell.width, extent.width) + 2,
                                       std::max(cell.height, extent.height)};
                const auto color = use_colors ? display_colors[cell.offset] : hemming_color;
                const auto paint = [&](float shift) {
                    auto area = target;
                    area.x += shift;
                    painted = context.services.graphics->draw_text_to_image(
                        image, 0U, cell.text, area, drawing_font, color) && painted;
                };
                if (cell.text == "―" || cell.text == "─" || cell.text == "＿" || cell.text == "￣") {
                    paint(-1); paint(0); paint(1);
                } else paint(0);
            }
            return painted;
        };
        bool hemmed = true;
        for (const auto [dx, dy] : {std::pair{-1.0F,-1.0F}, std::pair{0.0F,-1.0F},
                                    std::pair{1.0F,-1.0F}, std::pair{1.0F,0.0F},
                                    std::pair{1.0F,1.0F}, std::pair{0.0F,1.0F},
                                    std::pair{-1.0F,1.0F}, std::pair{-1.0F,0.0F}})
            hemmed = draw_cells(dx, dy, false) && hemmed;
        drew_native_text = draw_cells(0, 0, true) && hemmed;
    }
    if (!drew_native_text) {
        x = line_start;
        y = static_cast<std::int32_t>(prop.ds(
            prop.looks().messageStartPos(active_skin && active_skin->legacy(),
                                         !talkers.empty(), center_x).y));
        if (!single_line && center_y && !visible.empty()) {
            std::size_t lines = 1U;
            for (const auto value : visible)
                if (value == '\n') ++lines;
            const auto text_height = static_cast<std::int32_t>(
                lines * static_cast<std::size_t>(scaled_line_height));
            y = std::max<std::int32_t>(
                0, (static_cast<std::int32_t>(height) - text_height) / 2);
        }
        for (const auto current : decodeUtf8(visible)) {
            if (current == '\n') {
                x = line_start;
                y += scaled_line_height;
                continue;
            }
            const auto unit_width = std::max<std::int32_t>(
                1, scaled_char_width / (current < 0x80 ? 2 : 1));
            const auto glyph_width = std::max<std::int32_t>(1, unit_width - 1);
            const auto glyph_height = std::max<std::int32_t>(
                1, scaled_line_height - static_cast<std::int32_t>(2 * scale));
            fillRect(frame, x - static_cast<std::int32_t>(scale),
                     y - static_cast<std::int32_t>(scale),
                     glyph_width + static_cast<std::int32_t>(2 * scale),
                     glyph_height + static_cast<std::int32_t>(2 * scale),
                     hemming_color);
            fillRect(frame, x, y, glyph_width, glyph_height, foreground);
            x += unit_width;
        }
    }
    for (std::size_t i = 0; i < selections.size(); ++i) {
        const auto top = static_cast<std::int32_t>((height + selection_height * i));
        draw_border(top, static_cast<std::int32_t>(selection_height));
        const auto label_width = std::min<std::int32_t>(
            static_cast<std::int32_t>(selections[i].size() * scale),
            static_cast<std::int32_t>(width - 2 * scale));
        if (!context.services.graphics ||
            !context.services.graphics->draw_text_to_image(
                image, 0U, selections[i],
                {static_cast<float>(2 * scale), static_cast<float>(top + scale),
                 static_cast<float>(width - 4 * scale),
                 static_cast<float>(selection_height - 2 * scale)},
                fontSpec(prop.ds(prop.adjustFont(prop.looks().messageSelectFont(
                    active_skin && active_skin->legacy())))), foreground)) {
            fillRect(frame,
                     static_cast<std::int32_t>((width - label_width) / 2),
                     top + static_cast<std::int32_t>(scale), label_width,
                     std::max<std::int32_t>(1, static_cast<std::int32_t>(
                         selection_height - 2 * scale)), foreground);
        }
    }
    return ImageDataWithScale(
        std::move(image), scale,
        upload ? context.services.graphics : std::shared_ptr<lib::GraphicsBackend>{});
}

std::shared_ptr<lib::Menu> setupSPCharsMenu(
    const MessageContext& context, const Summary* summ,
    const std::function<Skin::Ptr()>& skin_getter,
    const usecounter::UseCounter* uc,
    const std::shared_ptr<lib::Menu>& parent_menu, bool full,
    bool expand_sharps, const std::function<bool()>& is_expand_sp_chars,
    const std::function<void(StringView)>& insert)
{
    auto menu = std::make_shared<lib::Menu>(true);
    menu->on_command = [insert](StringView command) {
        if (insert) insert(command);
    };
    if (parent_menu) {
        lib::MenuItem item;
        item.text = context.props ? context.props->buildMenu(types::MenuID::PutSPChar) : String("特殊文字の挿入");
        item.command = "put-special";
        item.icon = context.props ? context.props->images().menu(types::MenuID::PutSPChar)
                                  : lib::ResourceId{};
        item.enabled = !is_expand_sp_chars || is_expand_sp_chars();
        // D版のサブメニューの Show と同様、親を開くときは利用可否だけ
        // を更新し、 候補はこのサブメニュー自身を表示するときに組み立てる。
        item.open_children = [menu, context, summ, skin_getter, uc, full,
                              expand_sharps, is_expand_sp_chars, insert] {
            const auto fresh = setupSPCharsMenu(context, summ, skin_getter, uc, {}, full,
                expand_sharps, is_expand_sp_chars, insert);
            menu->items = std::move(fresh->items);
            ++menu->item_update_revision;
            return menu->items;
        };
        parent_menu->items.push_back(std::move(item));
        const auto previous_open = parent_menu->on_open;
        parent_menu->on_open = [parent = std::weak_ptr<lib::Menu>(parent_menu),
                               previous_open, is_expand_sp_chars] {
            if (previous_open) previous_open();
            if (const auto owner = parent.lock()) {
                for (auto& entry : owner->items)
                    if (entry.command == "put-special")
                        entry.enabled = !is_expand_sp_chars || is_expand_sp_chars();
                ++owner->item_update_revision;
            }
        };
        return menu;
    }
    const bool enabled = !is_expand_sp_chars || is_expand_sp_chars();
    const bool legacy = summ && summ->legacy();
    auto* prop = context.props;
    const auto group = [prop](types::MenuID id, String fallback) {
        lib::MenuItem result;
        result.text = prop ? prop->buildMenu(id) : std::move(fallback);
        result.icon = prop ? prop->images().menu(id) : lib::ResourceId{};
        return result;
    };
    if (expand_sharps && enabled) {
        const auto chars = full ? std::vector<SPChar>(SPCHAR_ALL.begin(), SPCHAR_ALL.end())
            : legacy ? std::vector<SPChar>(SPCHAR_TEXT_CLASSIC.begin(), SPCHAR_TEXT_CLASSIC.end())
                     : std::vector<SPChar>(SPCHAR_TEXT.begin(), SPCHAR_TEXT.end());
        for (const auto value : chars) {
            const auto command = String("#") + C_TBL[static_cast<std::size_t>(value)];
            String label = command;
            if (prop) switch (C_TBL[static_cast<std::size_t>(value)]) {
                case 'M': label = prop->requiredMsgs().scTalkerName(types::Talker::Selected); break;
                case 'U': label = prop->requiredMsgs().scTalkerName(types::Talker::Unselected); break;
                case 'R': label = prop->requiredMsgs().scTalkerName(types::Talker::Random); break;
                case 'C': label = prop->requiredMsgs().scTalkerName(types::Talker::Card); break;
                case 'I': label = prop->requiredMsgs().scRef.value; break;
                case 'T': label = prop->requiredMsgs().scTeam.value; break;
                case 'Y': label = prop->requiredMsgs().scYado.value; break;
            }
            const auto image = prop ? (C_TBL[static_cast<std::size_t>(value)] == 'I'
                ? prop->images().scRef()
                : C_TBL[static_cast<std::size_t>(value)] == 'T'
                    ? prop->images().scTeam()
                    : C_TBL[static_cast<std::size_t>(value)] == 'Y'
                        ? prop->images().scYado()
                        : prop->images().scTalker(value == SPChar::M ? types::Talker::Selected
                            : value == SPChar::U ? types::Talker::Unselected
                            : value == SPChar::R ? types::Talker::Random : types::Talker::Card))
                : lib::ResourceId{};
            menu->items.push_back({label, command, {}, image, true, false, false, false, {}});
        }
        if (!legacy && summ && context.props) {
            const auto selected_number =
                context.props->sys().selectedPlayerCardNumber();
            menu->items.push_back({context.props->requiredMsgs().selectedPlayerCardNumber.value, "$" + selected_number + "$",
                                   {}, context.props->images().selectedPlayerCardNumber(), true, false, false, false, {}});
        }
    }
    if (!legacy && context.props) {
        for (std::uint32_t number = 1; number <= context.props->looks().partyMax(); ++number) {
            menu->items.push_back({utils::tryFormat(context.props->requiredMsgs().playerCardName.value, number),
                "$" + context.props->sys().playerCardName(number) + "$", {},
                context.props->images().playerCardName()});
        }
    }
    menu->items.push_back({"", {}, {}, 0, true, false, false, true, {}});
    namespace vars = ::cwxeditor::cwx::flag;
    const auto counter = context.use_counter_owner ? context.use_counter_owner
        : usecounter::CounterOwner(const_cast<usecounter::UseCounter*>(uc), [](auto*) {});
    const auto root = summ ? summ->flagDirRoot() : vars::FlagDirPtr{};
    auto enabled_root = root;
    if (counter) {
        if (const auto local = std::dynamic_pointer_cast<vars::LocalVariableOwner>(counter->owner()))
            enabled_root = local->flagDirRoot();
    }
    const auto variable_group = [&](auto type, types::MenuID id, String fallback,
                                    const char marker, bool available, lib::ResourceId icon) {
        using Variable = std::remove_pointer_t<decltype(type)>;
        auto entry = group(id, std::move(fallback));
        entry.enabled = available;
        entry.open_children = [root, counter, prop, marker, insert, icon] {
            std::vector<lib::MenuItem> children;
            vars::sortedWithPath(vars::allVars<Variable>(root, counter),
                prop && prop->var().logicalSort.value, [&](const auto& value) {
                    if (!value) return;
                    const auto command = String(1U, marker) + value->path() + marker;
                    lib::MenuItem child;
                    child.text = value->path();
                    child.command = command;
                    child.icon = icon;
                    child.action = [insert, command] { if (insert) insert(command); };
                    children.push_back(std::move(child));
                });
            return children;
        };
        menu->items.push_back(std::move(entry));
    };
    variable_group(static_cast<vars::Flag*>(nullptr), types::MenuID::PutFlagValue, "フラグ値", '%',
                   enabled_root && enabled_root->hasFlag(), prop ? prop->images().flag() : lib::ResourceId{});
    variable_group(static_cast<vars::Step*>(nullptr), types::MenuID::PutStepValue, "ステップ値", '$',
                   enabled_root && enabled_root->hasStep(), prop ? prop->images().step() : lib::ResourceId{});
    variable_group(static_cast<vars::Variant*>(nullptr), types::MenuID::PutVariantValue, "コモン値", '@',
                   enabled_root && enabled_root->hasVariant(), prop ? prop->images().variant() : lib::ResourceId{});
    if (full) {
        menu->items.push_back({"", {}, {}, 0, true, false, false, true, {}});
        auto colors = group(types::MenuID::PutColor, "文字色");
        for (const auto value : std::array<char, 9>{'W', 'R', 'B', 'G', 'Y',
                                                     'O', 'P', 'L', 'D'}) {
            const auto command = itemCommand("&", value);
            String label = command;
            if (prop) switch (value) {
                case 'W': label = prop->requiredMsgs().colorW.value; break;
                case 'R': label = prop->requiredMsgs().colorR.value; break;
                case 'B': label = prop->requiredMsgs().colorB.value; break;
                case 'G': label = prop->requiredMsgs().colorG.value; break;
                case 'Y': label = prop->requiredMsgs().colorY.value; break;
                case 'O': label = prop->requiredMsgs().colorO.value; break;
                case 'P': label = prop->requiredMsgs().colorP.value; break;
                case 'L': label = prop->requiredMsgs().colorL.value; break;
                case 'D': label = prop->requiredMsgs().colorD.value; break;
            }
            colors.children.push_back({label, command, {}, prop ? prop->images().color(value)
                                                                  : lib::ResourceId{},
                                       true, false, false, false, {}});
        }
        menu->items.push_back(std::move(colors));
        const auto active_skin = skin_getter ? skin_getter() : Skin::Ptr{};
        auto skin_chars = group(types::MenuID::PutSkinSPChar, "スキンの特殊文字");
        menu->items.push_back({"", {}, {}, 0, true, false, false, true, {}});
        skin_chars.enabled = active_skin && !active_skin->spChars().empty();
        const auto services = context.services;
        skin_chars.open_children = [skin_getter, insert, prop, services] {
            std::vector<lib::MenuItem> children;
            const auto skin = skin_getter ? skin_getter() : Skin::Ptr{};
            if (skin) for (const auto& [value, path] : skin->spChars()) {
                (void)path;
                const auto image = dskin::spChar(*skin,
                    prop ? std::max<std::uint32_t>(1U, prop->var().imageScale.value) : 1U,
                    value, services);
                if (!image.valid()) continue;
                const auto suffix = charText(value);
                if (suffix.empty()) continue;
                const auto command = "#" + suffix;
                auto icon = image.resource;
                std::shared_ptr<lib::OwnedImageResource> image_owner;
                if (icon && image.owned_resource && services.graphics) {
                    image_owner = std::make_shared<lib::OwnedImageResource>(
                        services.graphics);
                    image_owner->id = icon;
                }
                auto icon_slot = std::make_shared<lib::ImageResourceSlot>();
                if (!icon && services.graphics && !image.data.frames.empty())
                    icon = icon_slot->publish(services.graphics, image.data);
                lib::MenuItem child;
                child.text = child.command = command;
                child.icon = icon;
                // dskin は ImageDataWithScale のリソースとデータの組を
                // 返す。 所有するリソースやメニュー用のアップロードは、項目とともに保持する。
                child.action = [insert, command,
                                image_owner = std::move(image_owner),
                                icon_slot = std::move(icon_slot)] {
                    (void)image_owner;
                    (void)icon_slot;
                    if (insert) insert(command);
                };
                children.push_back(std::move(child));
            }
            std::sort(children.begin(), children.end(), [](const auto& a, const auto& b) {
                return a.command < b.command;
            });
            return children;
        };
        menu->items.push_back(std::move(skin_chars));
        menu->items.push_back({"", {}, {}, 0, true, false, false, true, {}});
        auto fonts = group(types::MenuID::PutImageFont, "イメージ");
        const auto font_items = [summ, insert, skin_getter, prop](bool availability_only) {
            std::vector<lib::MenuItem> children;
            if (summ) {
                std::error_code error;
                for (const auto& entry : std::filesystem::directory_iterator(utf8Path(summ->scenarioPath()), error)) {
                    if (error) break;
                    if (!entry.is_regular_file(error)) continue;
                    const auto name = structs::pathToUtf8(entry.path().filename());
                    if (!msgutils::isSPFontFile(name)) continue;
                    lib::MenuItem child;
                    child.text = name;
                    child.command = "#" + charText(msgutils::decodeFontPath(name));
                    const auto image_path = structs::pathToUtf8(entry.path());
                    const auto active_skin = skin_getter ? skin_getter() : Skin::Ptr{};
                    child.icon = prop
                        ? (active_skin && active_skin->isCardImage(image_path, false, false)
                            ? prop->images().cards() : prop->images().backs())
                        : lib::ResourceId{};
                    child.action = [insert, command = child.command] { if (insert) insert(command); };
                    children.push_back(std::move(child));
                    if (availability_only) break; // D版と同様、表示時に存在確認してから一覧にする。
                }
            }
            if (!availability_only) std::sort(children.begin(), children.end(), [](const auto& a, const auto& b) {
                return lowerAscii(a.text) < lowerAscii(b.text);
            });
            return children;
        };
        fonts.enabled = !font_items(true).empty();
        fonts.open_children = [font_items] { return font_items(false); };
        menu->items.push_back(std::move(fonts));
    }
    // これらの項目は別のメニューにも複製されるため、各末端項目に独自の処理を
    // 持たせる。 テキスト編集メニュー全体の処理は、編集コマンドだけを扱う。
    const auto bind_actions = [&insert](auto&& self, std::vector<lib::MenuItem>& items) -> void {
      for (auto& child : items) {
        if (!child.children.empty()) self(self, child.children);
        else if (!child.open_children && !child.separator && !child.command.empty()) child.action = [insert, command = child.command] {
            if (insert) insert(command);
        };
      }
    };
    bind_actions(bind_actions, menu->items);
    return menu;
}

String createSPCharPreview(
    const Props& prop, const Summary* summ, const Skin::Ptr& active_skin,
    const usecounter::UseCounter* uc, String text, bool expand_sharps,
    const msgutils::VarGetter& override_flag_value,
    const msgutils::VarGetter& override_step_value)
{
    NameMap names;
    msgutils::VarGetter flags;
    msgutils::VarGetter steps;
    msgutils::VarGetter variants;
    msgutils::VarGetter sys_steps;
    getPreviewValues(prop, summ, uc, SPCHAR_ALL, names, flags, steps, variants,
                     sys_steps);
    // この公開ポインターは、同期プレビューの間だけ借用する。 別名のトークンで共通の
    // findVar API に適合させ、weak_from_this は変えない。
    const usecounter::CounterOwner counter=uc
        ? usecounter::CounterOwner(std::make_shared<int>(0),const_cast<usecounter::UseCounter*>(uc))
        : usecounter::CounterOwner{};
    const auto root=summ ? summ->flagDirRoot() : flag::FlagDirPtr{};
    flags=[root,counter,override_flag_value](const String& path) {
        if(override_flag_value){const auto v=override_flag_value(path);if(v.exists)return v;}
        const auto value=flag::findVar<flag::Flag>(root,counter,path);
        return value ? VarValue{true,value->onOff()?value->on():value->off(),value->expandSPChars()} : VarValue{};
    };
    sys_steps=getPreviewSysSteps(prop,summ,expand_sharps);
    steps=[root,counter,override_step_value,sys_steps](const String& path) {
        if(override_step_value){const auto v=override_step_value(path);if(v.exists)return v;}
        const auto value=flag::findVar<flag::Step>(root,counter,path);
        return value ? VarValue{true,value->value(),value->expandSPChars()} : sys_steps(lowerAscii(path));
    };
    variants=[root,counter](const String& path) {
        const auto value=flag::findVar<flag::Variant>(root,counter,path);
        return value ? VarValue{true,expression::variantValueToPreviewText(*value),false} : VarValue{};
    };
    msgutils::FontMap fonts;
    msgutils::ColorMap colors;
    const msgutils::MaterialPredicate has_material =
        [active_skin, summ,expand_sharps](const String& path) {
            // D版と同様、バージョン判定を無効にするのではなく、 イメージフ
            // ォントが存在すると扱うことで、#による置換をすべて抑止する。
            if(!expand_sharps)return true;
            if (!active_skin) return false;
            const auto character=msgutils::decodeFontPath(path);
            if(summ && summ->legacy() && !::cwxeditor::cwx::sjis::isSJIS1ByteChar(character))return false;
            return !active_skin->findImagePath(
                                      path, summ ? summ->scenarioPath() : String{},
                                      summ ? summ->dataVersion() : String("1.50"))
                        .empty() || active_skin->spChars().contains(character);
        };
    const auto is_target = [&prop, summ](const String& version) {
        return prop.isTargetVersion(summ, version);
    };
    return msgutils::formatMsg(
        std::move(text), flags, steps, variants,
        [&names](char value) { return mapName(names, value); }, is_target,
        prop.sys().prefixSystemVarName(), has_material, fonts, colors);
}

String getSPCharPreviewValue(const Props& prop, char name)
{
    switch (static_cast<char>(std::toupper(static_cast<unsigned char>(name)))) {
    case 'M': return prop.var().messageVarSelected;
    case 'U': return prop.var().messageVarUnselected;
    case 'R': return prop.var().messageVarRandom;
    case 'C': return prop.var().messageVarCard;
    case 'I': return prop.var().messageVarRef;
    case 'T': return prop.var().messageVarTeam;
    case 'Y': return prop.var().messageVarYado;
    default: return String{};
    }
}

}  // namespace cwxeditor::cwx::editor::gui::dwt::messageutils
