#include "cwxeditor_src/core/cwx/textholder.hpp"
#include "cwxeditor_src/core/cwx/motion.hpp"
#include "cwxeditor_src/core/cwx/jpy.hpp"
#include "cwxeditor_src/core/cwx/flag.hpp"
#include "cwxeditor_src/core/cwx/expression.hpp"
#include "cwxeditor_src/core/cwx/warning.hpp"
#include "cwxeditor_src/lib/ui_item_presentation.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dutils.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/cardlist.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dmenu.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/standalone_webp.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/sound.hpp"
#include "cwxeditor_src/lib/image_mask.hpp"
#include "cwxeditor_src/lib/text.hpp"
#include "cwxeditor_src/lib/editor_diagnostics.hpp"
#include "cwxeditor_src/lib/window_host.hpp"
#include "cwxeditor_src/lib/ui_actions.hpp"
#include "cwxeditor_src/lib/ui_metrics.hpp"
#include "cwxeditor_src/lib/ui_text_layout.hpp"
#include "cwxeditor_src/lib/image_text.hpp"
#include "cwxeditor_src/lib/image_composition.hpp"

#include "cwxeditor_src/core/cwx/area.hpp"
#include "cwxeditor_src/core/cwx/background.hpp"
#include "cwxeditor_src/core/cwx/imagesize.hpp"
#include "cwxeditor_src/core/cwx/msgs.hpp"
#include "cwxeditor_src/core/cwx/utils.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/xmlbytestransfer.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/jpyimage.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dskin.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/centerlayout.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/areaviewutils.hpp"
#include <stdexcept>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <charconv>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <mutex>
#include <numeric>
#include <sstream>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>

namespace cwxeditor::cwx::editor::gui::dwt::dutils {

std::filesystem::path filesystemPath(const StringView value) {
    return structs::pathFromUtf8(value);
}

String pathString(const std::filesystem::path& value) {
    return structs::pathToUtf8(value);
}

namespace {

std::weak_ptr<lib::Widget> g_focus;
constexpr std::array<std::string_view, 2> kClipboardTextTypes{
    "text/plain", "text/plain;charset=utf-8"};
// SWTの公開GCフラグはDRAW_DELIMITER=1<<1、DRAW_TAB=1<<2。
constexpr int kSwtDrawDelimiter = 1 << 1;
constexpr int kSwtDrawTab = 1 << 2;

struct OpenButtonEnabledState {
    std::weak_ptr<lib::Button> button;
    std::weak_ptr<lib::TextEdit> text;
    std::weak_ptr<lib::ComboBox> combo;
    std::function<String()> getText;
    std::function<bool()> additionalEnabled;

    void refresh() const {
        const auto target = button.lock();
        if (!target) return;
        bool nonempty{};
        if (getText) nonempty = !getText().empty();
        else if (const auto field = text.lock()) nonempty = !field->text.empty();
        else if (const auto field = combo.lock()) nonempty = !field->edit_text.empty();
        target->set_enabled(nonempty && (!additionalEnabled || additionalEnabled()));
    }
};

[[nodiscard]] bool selectContainingTabPath(
    const WidgetPtr& root, const WidgetPtr& target,
    std::unordered_set<const lib::Widget*>& active_path) {
    if (!root || !target || !active_path.insert(root.get()).second) return false;
    if (root == target) {
        active_path.erase(root.get());
        return true;
    }

    bool found = false;
    if (const auto tabs = std::dynamic_pointer_cast<lib::TabView>(root)) {
        for (std::size_t index = 0U; index < tabs->pages.size(); ++index) {
            const auto page = tabs->pages[index].content;
            if (selectContainingTabPath(page, target, active_path)) {
                (void)lib::select_tab_page(*tabs, index);
                found = true;
                break;
            }
        }
    } else if (const auto container =
                   std::dynamic_pointer_cast<lib::Container>(root)) {
        for (const auto& child : container->children()) {
            if (selectContainingTabPath(child, target, active_path)) {
                found = true;
                break;
            }
        }
    }
    active_path.erase(root.get());
    return found;
}

[[nodiscard]] std::size_t bytesPerPixel(const lib::PixelFormat format) noexcept {
    switch (format) {
    case lib::PixelFormat::Gray8: return 1U;
    case lib::PixelFormat::RGB24:
    case lib::PixelFormat::BGR24: return 3U;
    case lib::PixelFormat::RGBA32:
    case lib::PixelFormat::BGRA32: return 4U;
    default: return 0U;
    }
}

[[nodiscard]] bool isJpyImage(const StringView path) {
    auto value = structs::pathToUtf8(filesystemPath(path).extension());
    std::ranges::transform(value, value.begin(), [](const unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value == ".jpy1" || value == ".jptx" || value == ".jpdc";
}

[[nodiscard]] String resolveImagePath(const Skin* skin, const Summary* summ,
                                      const StringView path) {
    if (path.empty() || utils::isBinImg(path)) return String(path);

    const auto native = filesystemPath(path);
    if (native.is_absolute()) return String(path);

    // DはSkin・Engineの代替より先に、シナリオ素材を解決する
    // 。Editorの作業ディレクトリはシナリオと同じではないため、
    // Material/Foo.bmpのようなXML値で必要になる。
    if (skin) {
        const auto resolved = skin->findImagePath(
            path, summ ? StringView(summ->scenarioPath()) : StringView{},
            summ ? StringView(summ->dataVersion()) : StringView{});
        if (!resolved.empty()) return resolved;
    }
    if (summ && !summ->scenarioPath().empty()) {
        const auto candidate = filesystemPath(summ->scenarioPath()) / native;
        std::error_code error;
        if (std::filesystem::is_regular_file(candidate, error))
            return pathString(candidate);
    }
    return String(path);
}

[[nodiscard]] String imageExtension(const StringView path) {
    if (utils::isBinImg(path)) {
        try { return imagesize::imageType(utils::strToBImg(path)); }
        catch (...) { return {}; }
    }
    String value = structs::pathToUtf8(filesystemPath(path).extension());
    std::ranges::transform(value, value.begin(), [](const unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

[[nodiscard]] lib::Bytes readImageBytes(const StringView path) {
    if (utils::isBinImg(path)) return utils::strToBImg(path);
    std::ifstream stream(filesystemPath(path), std::ios::binary | std::ios::ate);
    if (!stream) return {};
    const auto end = stream.tellg();
    if (end <= 0) return {};
    lib::Bytes bytes(static_cast<std::size_t>(end));
    stream.seekg(0, std::ios::beg);
    stream.read(reinterpret_cast<char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
    if (!stream) return {};
    return bytes;
}

void ignoreGifTransparency(lib::ImageData& image) {
    for (auto& frame : image.frames) {
        if (frame.pixel_format != lib::PixelFormat::RGBA32 &&
            frame.pixel_format != lib::PixelFormat::BGRA32) continue;
        const auto pixels = static_cast<std::size_t>(frame.pixel_width) *
                            frame.pixel_height;
        if (frame.pixels.size() < pixels * 4U) continue;
        for (std::size_t i = 0; i < pixels; ++i)
            frame.pixels[i * 4U + 3U] = std::byte{0xff};
    }
}

void applyColorKey(lib::ImageData& image, int maskX, int maskY) {
    lib::apply_image_color_key(image, maskX, maskY);
}

// DはBMPの元の第四バイトが一つでも0以外なら、完全に不透明な
// 面も含めalphaDataを作る。デコーダーが展開したRGBA
// だけでは、通常の24ビットBMPや0の埋め草と区別できない。
[[nodiscard]] bool bitmapSourceHasAlpha(const lib::ByteView bytes) noexcept {
        if (bytes.size() < 54U || bytes[0] != 'B' || bytes[1] != 'M') return false;
        const auto u16 = [&](const std::size_t at) {
            return std::uint32_t(bytes[at]) | (std::uint32_t(bytes[at + 1U]) << 8U);
        };
        const auto u32 = [&](const std::size_t at) {
            return std::uint32_t(bytes[at]) | (std::uint32_t(bytes[at + 1U]) << 8U) |
                   (std::uint32_t(bytes[at + 2U]) << 16U) | (std::uint32_t(bytes[at + 3U]) << 24U);
        };
        const auto header = u32(14U), offset = u32(10U), compression = u32(30U);
        if (header < 40U || header > bytes.size() - 14U ||
            offset < 14ULL + header || offset > bytes.size() ||
            u16(26U) != 1U || u16(28U) != 32U ||
            (compression != 0U && compression != 3U && compression != 6U)) return false;
        const auto width = static_cast<std::int32_t>(u32(18U));
        const auto signedHeight = static_cast<std::int32_t>(u32(22U));
        if (width <= 0 || signedHeight == 0) return false;
        const auto height = signedHeight < 0 ? -std::int64_t(signedHeight) : std::int64_t(signedHeight);
        const auto count = std::uint64_t(width) * std::uint64_t(height);
        if (count > (bytes.size() - offset) / 4U) return false;
        for (std::uint64_t i = 0; i < count; ++i)
            if (bytes[static_cast<std::size_t>(offset + i * 4U + 3U)] != 0U) return true;
    return false;
}

void repairBitmapFillerAlpha(lib::ImageData& image) {
    // 一部のデコーダーはパレット・旧BMPをRGBA/BGRAへ
    // 展開し、未使用アルファを0に残す。DのImageData
    // はCardWirthの透過色を適用する前にそれを不透明と
    // 扱う。補正しないと、画像なしでは見えるカードも、その画像
    // をMenuCardへ追加した途端に素材全体が透明になる。
    for (auto& frame : image.frames) {
        if (frame.pixel_format != lib::PixelFormat::RGBA32 &&
            frame.pixel_format != lib::PixelFormat::BGRA32) continue;
        if (frame.pixel_width == 0U || frame.pixel_height == 0U ||
            frame.pixel_width > std::numeric_limits<std::size_t>::max() /
                                    frame.pixel_height) continue;
        const auto count = static_cast<std::size_t>(frame.pixel_width) *
                           frame.pixel_height;
        if (count > frame.pixels.size() / 4U) continue;
        bool filler_alpha = true;
        for (std::size_t index = 0; index < count; ++index) {
            if (frame.pixels[index * 4U + 3U] != std::byte{}) {
                filler_alpha = false;
                break;
            }
        }
        if (!filler_alpha) continue;
        for (std::size_t index = 0; index < count; ++index)
            frame.pixels[index * 4U + 3U] = std::byte{0xff};
    }
}

[[nodiscard]] lib::ImageData resizeImage(const lib::ImageData& source,
                                         std::uint32_t width,
                                         std::uint32_t height) {
    lib::ImageData result = source;
    for (auto& frame : result.frames) {
        if (frame.pixel_width == 0U || frame.pixel_height == 0U) continue;
        const auto bpp = bytesPerPixel(frame.pixel_format);
        if (bpp == 0U || frame.pixels.size() <
                              static_cast<std::size_t>(frame.pixel_width) *
                                  frame.pixel_height * bpp) {
            frame.pixel_width = width;
            frame.pixel_height = height;
            frame.logical_size = {static_cast<float>(width), static_cast<float>(height)};
            continue;
        }
        std::vector<std::byte> pixels(static_cast<std::size_t>(width) * height * bpp);
        for (std::uint32_t y = 0; y < height; ++y) {
            const auto sy = std::min(frame.pixel_height - 1U,
                                     static_cast<std::uint32_t>(
                                         static_cast<std::uint64_t>(y) * frame.pixel_height / height));
            for (std::uint32_t x = 0; x < width; ++x) {
                const auto sx = std::min(frame.pixel_width - 1U,
                                         static_cast<std::uint32_t>(
                                             static_cast<std::uint64_t>(x) * frame.pixel_width / width));
                const auto sourceOffset =
                    (static_cast<std::size_t>(sy) * frame.pixel_width + sx) * bpp;
                const auto targetOffset =
                    (static_cast<std::size_t>(y) * width + x) * bpp;
                std::copy_n(frame.pixels.data() + sourceOffset, bpp,
                            pixels.data() + targetOffset);
            }
        }
        frame.pixels = std::move(pixels);
        frame.pixel_width = width;
        frame.pixel_height = height;
        frame.logical_size = {static_cast<float>(width), static_cast<float>(height)};
    }
    return result;
}

[[nodiscard]] std::uint32_t imageWidth(const lib::ImageData& image) noexcept {
    if (image.frames.empty()) return 0U;
    return image.frames.front().pixel_width;
}

[[nodiscard]] std::uint32_t imageHeight(const lib::ImageData& image) noexcept {
    if (image.frames.empty()) return 0U;
    return image.frames.front().pixel_height;
}

[[nodiscard]] std::size_t utf8CodePointCount(const StringView text) noexcept {
    std::size_t count = 0U;
    for (std::size_t index = 0U; index < text.size();) {
        const auto byte = static_cast<unsigned char>(text[index]);
        std::size_t length = 1U;
        if ((byte & 0xE0U) == 0xC0U) length = 2U;
        else if ((byte & 0xF0U) == 0xE0U) length = 3U;
        else if ((byte & 0xF8U) == 0xF0U) length = 4U;
        if (length > 1U && index + length <= text.size()) {
            bool valid = true;
            for (std::size_t offset = 1U; offset < length; ++offset) {
                if ((static_cast<unsigned char>(text[index + offset]) & 0xC0U) != 0x80U) {
                    valid = false;
                    break;
                }
            }
            if (valid) index += length;
            else ++index;
        } else {
            ++index;
        }
        ++count;
    }
    return count;
}

void eraseLastUtf8CodePoint(String& text) noexcept {
    if (text.empty()) return;
    auto index = text.size() - 1U;
    while (index > 0U &&
           (static_cast<unsigned char>(text[index]) & 0xC0U) == 0x80U)
        --index;
    text.erase(index);
}

[[nodiscard]] lib::TreeNode* findNode(const TreePtr& tree,
                                      const lib::TreeItemId id) {
    if (!tree) return nullptr;
    const auto found = std::find_if(tree->nodes.begin(), tree->nodes.end(),
                                    [id](const lib::TreeNode& node) { return node.id == id; });
    return found == tree->nodes.end() ? nullptr : &*found;
}

[[nodiscard]] lib::TreeItemId nextTreeId(const TreePtr& tree);

[[nodiscard]] std::vector<lib::TreeNode*> children(const TreePtr& tree,
                                                   const lib::TreeItemId parent) {
    std::vector<lib::TreeNode*> result;
    if (!tree) return result;
    for (auto& node : tree->nodes)
        if (node.parent == parent) result.push_back(&node);
    return result;
}

[[nodiscard]] bool isTreeDescendant(const TreePtr& tree,
                                    const lib::TreeItemId candidate,
                                    const lib::TreeItemId ancestor) {
    auto* node = findNode(tree, candidate);
    while (node && node->parent != 0U) {
        if (node->parent == ancestor) return true;
        node = findNode(tree, node->parent);
    }
    return false;
}

[[nodiscard]] std::vector<lib::TreeItemId> subtreeIds(
    const TreePtr& tree, const lib::TreeItemId root) {
    std::vector<lib::TreeItemId> result;
    if (!tree || !findNode(tree, root)) return result;
    for (const auto& node : tree->nodes)
        if (node.id == root || isTreeDescendant(tree, node.id, root)) result.push_back(node.id);
    return result;
}

[[nodiscard]] std::size_t nodeIndex(const TreePtr& tree, const lib::TreeItemId id) {
    if (!tree) return static_cast<std::size_t>(-1);
    const auto found = std::find_if(tree->nodes.begin(), tree->nodes.end(),
                                    [id](const lib::TreeNode& node) { return node.id == id; });
    return found == tree->nodes.end()
               ? static_cast<std::size_t>(-1)
               : static_cast<std::size_t>(std::distance(tree->nodes.begin(), found));
}

[[nodiscard]] std::size_t siblingInsertPosition(const TreePtr& tree,
                                                const lib::TreeItemId parent,
                                                const std::size_t index) {
    if (!tree) return 0U;
    const auto siblings = children(tree, parent);
    if (index < siblings.size()) return nodeIndex(tree, siblings[index]->id);
    if (parent != 0U) {
        const auto parentPosition = nodeIndex(tree, parent);
        if (parentPosition != static_cast<std::size_t>(-1)) {
            std::size_t result = parentPosition + 1U;
            while (result < tree->nodes.size() &&
                   (tree->nodes[result].id == parent ||
                    isTreeDescendant(tree, tree->nodes[result].id, parent)))
                ++result;
            return result;
        }
    }
    return tree->nodes.size();
}

[[nodiscard]] lib::TreeItemId insertTreeNode(
    const TreePtr& tree, lib::TreeNode node, const std::size_t index) {
    if (!tree) return 0U;
    node.id = nextTreeId(tree);
    const auto id = node.id;
    const auto position = siblingInsertPosition(tree, node.parent, index);
    auto nodes = tree->nodes;
    nodes.insert(nodes.begin() + static_cast<std::ptrdiff_t>(position),
                 std::move(node));
    if (!lib::replace_tree_nodes(tree, std::move(nodes), tree->selection)) return 0U;
    return id;
}

void swapTreeSubtrees(const TreePtr& tree, const lib::TreeItemId first,
                      const lib::TreeItemId second) {
    if (!tree || first == second) return;
    const auto firstIds = subtreeIds(tree, first);
    const auto secondIds = subtreeIds(tree, second);
    if (firstIds.empty() || secondIds.empty()) return;
    std::unordered_set<lib::TreeItemId> removed(firstIds.begin(), firstIds.end());
    removed.insert(secondIds.begin(), secondIds.end());
    const auto firstPosition = nodeIndex(tree, first);
    const auto secondPosition = nodeIndex(tree, second);
    if (firstPosition == static_cast<std::size_t>(-1) ||
        secondPosition == static_cast<std::size_t>(-1)) return;
    std::vector<lib::TreeNode> firstBlock;
    std::vector<lib::TreeNode> secondBlock;
    for (const auto& node : tree->nodes) {
        if (std::find(firstIds.begin(), firstIds.end(), node.id) != firstIds.end())
            firstBlock.push_back(node);
        else if (std::find(secondIds.begin(), secondIds.end(), node.id) != secondIds.end())
            secondBlock.push_back(node);
    }
    // Dは二つの兄弟の位置を交換する。両方の部分木を前
    // の位置へ詰めず、間にある兄弟はそのままにする。
    std::vector<lib::TreeNode> replacement;
    replacement.reserve(tree->nodes.size());
    for (const auto& node : tree->nodes) {
        if (node.id == first)
            replacement.insert(replacement.end(), secondBlock.begin(), secondBlock.end());
        else if (node.id == second)
            replacement.insert(replacement.end(), firstBlock.begin(), firstBlock.end());
        else if (!removed.contains(node.id))
            replacement.push_back(node);
    }
    (void)lib::replace_tree_nodes(tree, std::move(replacement), tree->selection);
}

[[nodiscard]] lib::TreeItemId nextTreeId(const TreePtr& tree) {
    lib::TreeItemId result = 1U;
    if (tree) {
        for (const auto& node : tree->nodes) result = std::max(result, node.id + 1U);
    }
    return result;
}

[[nodiscard]] bool containsWidget(const ContainerPtr& parent,
                                  const WidgetPtr& target) {
    if (!parent || !target) return false;
    for (const auto& child : parent->children()) {
        if (child == target) return true;
        if (const auto childContainer = std::dynamic_pointer_cast<lib::Container>(child))
            if (containsWidget(childContainer, target)) return true;
    }
    return false;
}

[[nodiscard]] String percentFormat(StringView format, StringView value) {
    String result(format);
    const auto pos = result.find("%s");
    if (pos != String::npos) result.replace(pos, 2U, value);
    return result;
}

[[nodiscard]] String enumName(const types::CType type) {
    switch (type) {
    case types::CType::Start: return "Start";
    case types::CType::StartBattle: return "StartBattle";
    case types::CType::End: return "End";
    case types::CType::EndBadEnd: return "EndBadEnd";
    case types::CType::ChangeArea: return "ChangeArea";
    case types::CType::ChangeBgImage: return "ChangeBgImage";
    case types::CType::Effect: return "Effect";
    case types::CType::EffectBreak: return "EffectBreak";
    case types::CType::LinkStart: return "LinkStart";
    case types::CType::LinkPackage: return "LinkPackage";
    case types::CType::TalkMessage: return "TalkMessage";
    case types::CType::TalkDialog: return "TalkDialog";
    case types::CType::PlayBgm: return "PlayBgm";
    case types::CType::PlaySound: return "PlaySound";
    case types::CType::Wait: return "Wait";
    default: return "Content";
    }
}

[[nodiscard]] String contentName(const Props* prop, const types::CType type) {
    if (prop) {
        try {
            const auto value = prop->requiredMsgs().contentName(type);
            if (!value.empty()) return value;
        } catch (...) {
            // スキンが一部だけ読み込まれていても、イベントツリーの描画を失敗させない。
        }
    }
    return enumName(type);
}

[[nodiscard]] String variableName(const Summary* summ, const Content* content,
                                  const StringView id, const int kind) {
    if (id.empty()) return "(none)";
    if (!summ) return String(id);
    const auto root = summ->flagDirRoot();
    const auto counter = content ? content->useCounter() : nullptr;
    if (kind == 0) {
        if (const auto value = flag::findVar<flag::Flag>(root, counter, id)) return value->name();
    } else if (kind == 1) {
        if (const auto value = flag::findVar<flag::Step>(root, counter, id)) return value->name();
    } else if (const auto value = flag::findVar<flag::Variant>(root, counter, id)) {
        return value->name();
    }
    return String(id);
}

[[nodiscard]] String joinValues(const lib::Array<String>& values) {
    String result;
    for (const auto& value : values) {
        if (value.empty()) continue;
        if (!result.empty()) result += " ";
        result += value;
    }
    return result;
}

}  // namespace

ImageDataWithScale::ImageDataWithScale(lib::ImageData data,
                                       const std::uint32_t scale)
    // 追加の公開APIでの倍率0の境界は意図的に正規化し、DのgetWidth/getHeight
    // のゼロ除算を再現しない。
    : data_(std::move(data)), scale_(std::max(1U, scale)) {
    scaled_.emplace(scale_, data_);
}

lib::ImageData ImageDataWithScale::scaled(const std::uint32_t targetScale) {
    const auto target = std::max(1U, targetScale);
    if (const auto found = scaled_.find(target); found != scaled_.end())
        return found->second;
    const auto oldSize = bufferSize();
    const auto result = resizeImage(data_, static_cast<std::uint32_t>(getWidth(target)),
                                    static_cast<std::uint32_t>(getHeight(target)));
    scaled_.emplace(target, result);
    const auto newSize = bufferSize();
    for (const auto& callback : updateSizeEvent)
        if (callback) callback(oldSize, newSize);
    return result;
}

lib::ImageData ImageDataWithScale::scaledPercent(const std::uint32_t targetPercent) {
    const auto percent = std::clamp(targetPercent, 100U, 1600U);
    if (percent % 100U == 0U) return scaled(percent / 100U);
    if (const auto found = scaled_percent_.find(percent); found != scaled_percent_.end())
        return found->second;
    const auto oldSize = bufferSize();
    const auto width = std::max(1U, static_cast<std::uint32_t>(
        std::lround(static_cast<double>(imageWidth(data_)) * percent /
                    (100.0 * scale_))));
    const auto height = std::max(1U, static_cast<std::uint32_t>(
        std::lround(static_cast<double>(imageHeight(data_)) * percent /
                    (100.0 * scale_))));
    const auto result = resizeImage(data_, width, height);
    scaled_percent_.emplace(percent, result);
    const auto newSize = bufferSize();
    for (const auto& callback : updateSizeEvent)
        if (callback) callback(oldSize, newSize);
    return result;
}

bool ImageDataWithScale::valid() const noexcept {
    return imageWidth(data_) > 1U && imageHeight(data_) > 1U;
}

int ImageDataWithScale::getWidth(const std::uint32_t targetScale) const noexcept {
    if (imageWidth(data_) == 0U) return 1;
    const auto value = static_cast<std::uint64_t>(imageWidth(data_)) *
                       std::max(1U, targetScale) / std::max(1U, scale_);
    return std::max(1, static_cast<int>(value));
}

int ImageDataWithScale::getHeight(const std::uint32_t targetScale) const noexcept {
    if (imageHeight(data_) == 0U) return 1;
    const auto value = static_cast<std::uint64_t>(imageHeight(data_)) *
                       std::max(1U, targetScale) / std::max(1U, scale_);
    return std::max(1, static_cast<int>(value));
}

std::vector<lib::ImageData> ImageDataWithScale::allData() const {
    std::vector<lib::ImageData> result;
    result.reserve(scaled_.size() + scaled_percent_.size());
    for (const auto& [scale, data] : scaled_) {
        (void)scale;
        result.push_back(data);
    }
    for (const auto& [percent, data] : scaled_percent_) {
        (void)percent;
        result.push_back(data);
    }
    return result;
}

std::size_t ImageDataWithScale::bufferSize() const noexcept {
    std::size_t result = 0;
    for (const auto& [scale, data] : scaled_) {
        (void)scale;
        for (const auto& frame : data.frames) result += frame.pixels.size();
    }
    for (const auto& [percent, data] : scaled_percent_) {
        (void)percent;
        for (const auto& frame : data.frames) result += frame.pixels.size();
    }
    return result;
}

void ImageDataWithScale::destroyAllData() noexcept {
    scaled_.clear();
    scaled_percent_.clear();
    data_ = {};
}

bool dwtImageSize(const Props* prop, const Skin* skin, const Summary* summ,
                  const StringView path, std::uint32_t& width,
                  std::uint32_t& height, lib::Services services) {
    width = 0U;
    height = 0U;
    if (path.empty()) return false;
    if (services.image_codecs && utils::isBinImg(path)) {
        try {
            const auto bytes = utils::strToBImg(path);
            if (bytes.empty()) return false;
            std::string error;
            const auto data = services.image_codecs->decode(
                {{}, std::span<const std::byte>(
                    reinterpret_cast<const std::byte*>(bytes.data()), bytes.size()),
                 lib::ImageFormat::Unknown, {}}, error);
            width = imageWidth(data);
            height = imageHeight(data);
            return width > 0U && height > 0U;
        } catch (...) {
            return false;
        }
    }
    if (prop && isJpyImage(path)) {
        bool resizable{};
        auto rendered = jpyimage::loadJPYImageWithScale(
            *const_cast<Props*>(prop), skin, summ, String(path), 1U, {}, width, height,
            resizable);
        return rendered.valid();
    }
    // D との意図的な差。D は拡張子と実形式が異なる画像でサイズ取得に失敗する（gui/cwx/editor/gui/dwt/dutils.d:160-170
    // 、core/cwx/imagesize.d:398-411）。C++ では実形式から取得し、この失敗を再現しない。
    if (services.image_codecs) {
        try {
            std::string error;
            const auto data = services.image_codecs->decode(
                {filesystemPath(path), {}, lib::ImageFormat::Unknown, {}}, error);
            width = imageWidth(data);
            height = imageHeight(data);
            return width > 0U && height > 0U;
        } catch (...) {
        }
    }
    std::error_code error;
    if (!std::filesystem::is_regular_file(filesystemPath(path), error)) return false;
    return false;
}

lib::ImageData loadImageWithServices(StringView path,
                                     const std::vector<String>& stratum,
                                     lib::Services services, bool* decoded = nullptr,
                                     bool* bitmap_alpha = nullptr) {
    if (bitmap_alpha) *bitmap_alpha = false;
    if(decoded)*decoded=false;
    if (path.empty() || std::find(stratum.begin(), stratum.end(), String(path)) != stratum.end())
        return blankImage();
    if (!services.image_codecs) return blankImage();
    try {
        std::string error;
        auto bytes = readImageBytes(path);
        if (!bytes.empty()) {
            auto native_webp = standalone_webp::decode({reinterpret_cast<const std::byte*>(bytes.data()), bytes.size()});
            if (!native_webp.frames.empty()) { if (decoded) *decoded = true; return native_webp; }
            // CardWirthNextが出力した二種類の不正BMPは、バ
            // イト列を補正してからプラットフォームのデコーダーへ渡す。その
            // 後の16ビット修復も、DのEditorと同じ代替順で使う。
            imagesize::fixCWNext32BitBitmap(bytes);
            lib::DecodeImageRequest request;
            request.encoded_data = std::span<const std::byte>(
                reinterpret_cast<const std::byte*>(bytes.data()), bytes.size());
            auto image = services.image_codecs->decode(request, error);
            if (!image.frames.empty()) {if(decoded)*decoded=true;
                if(bitmap_alpha)*bitmap_alpha=bitmapSourceHasAlpha(bytes);return image;}
            imagesize::fixCWNext16BitBitmap(bytes);
            error.clear();
            request.encoded_data = std::span<const std::byte>(
                reinterpret_cast<const std::byte*>(bytes.data()), bytes.size());
            image = services.image_codecs->decode(request, error);
            if (!image.frames.empty()) {if(decoded)*decoded=true;
                if(bitmap_alpha)*bitmap_alpha=bitmapSourceHasAlpha(bytes);return image;}
        }
        auto image = services.image_codecs->decode(
            {filesystemPath(path), {}, lib::ImageFormat::Unknown, {}}, error);
        if (!image.frames.empty()) {if(decoded)*decoded=true;return image;}
    } catch (...) {
    }
    return blankImage();
}

ImageDataWithScale loadImageWithScale(const Props* prop, const Skin* skin,
                                      const Summary* summ, const StringView path,
                                      const std::uint32_t targetScale,
                                      const bool mask, const bool isImageCell,
                                      const int maskX, const int maskY,
                                      const std::vector<String>& stratum,
                                      lib::Services services, bool* decoded) {
    if(decoded)*decoded=false;
    if (!services.image_codecs && prop) services = prop->images().services();
    if (prop && isJpyImage(path)) {
        bool resizable{};
        auto rendered = jpyimage::loadJPYImageWithScale(
            *const_cast<Props*>(prop), skin, summ, String(path), targetScale,
            stratum, resizable);
        auto data = rendered.data;
        if(decoded)*decoded=!data.frames.empty();
        if (mask) applyColorKey(data, maskX, maskY);
        return ImageDataWithScale(std::move(data),
                                  std::max<std::uint32_t>(1U, rendered.source_scale));
    }
    const auto resolved = resolveImagePath(skin, summ, path);
    // binaryimage:// URIはデータでありファイルシステムのパスではない。
    // generic_stringへ変換すると、スキームの二つのスラッシュが一つになる。
    const bool embedded = utils::isBinImg(resolved);
    const auto scaled = embedded ? utils::ScaledPathInfo{{}, 1U}
        : utils::findScaledImage(filesystemPath(resolved), targetScale);
    const auto source = embedded || scaled.path.empty() ? resolved : pathString(scaled.path);
    bool source_bitmap_alpha = false;
    auto image = loadImageWithServices(source, stratum, services, decoded,
        lib::editor_window_host_options().all_mode ? nullptr : &source_bitmap_alpha);
    const auto extension = imageExtension(source);
    if (extension == ".gif" && image.format != lib::ImageFormat::Png) ignoreGifTransparency(image);
    const auto& transparency=image.source_transparency;
    if(image.format==lib::ImageFormat::Png && transparency.kind!=lib::ImageTransparency::Unknown) {
        // Dのdutils.d:284-307に対応。復号後の全アルファが255でも
        // 、alphaDataは入力元の属性。APNGの合成済みRGBAもalphaData
        // とする。CardWirth互換のため、パレット分岐はmask/isImageCell
        // より先に置き、復号後も元のパレットRGBを保持する。
        if(transparency.kind==lib::ImageTransparency::PaletteKey) {
            lib::set_image_alpha(image,255);
            if(isImageCell && transparency.key)lib::apply_image_color_key(image,*transparency.key);
            else if(mask)applyColorKey(image,maskX,maskY);
        } else if(transparency.kind==lib::ImageTransparency::DirectKey) {
            // DWTは16ビットtRNSのtransparentPixelをRGB
            // の上位8ビットへ縮める。SDLは16ビット除去前にtRNSを展
            // 開するため、追加マスクなしでも同じ共通の透過キー正規化が必要。
            if(transparency.key) {
                lib::set_image_alpha(image,255);
                lib::apply_image_color_key(image,*transparency.key);
            }
            if(mask && isImageCell)lib::set_image_alpha(image,255);
            else if(mask) {
                // Dのto24は、隅の透過色マスクの前に不透明な黒のCanvas
                // へ描く。共通の非乗算アルファの合成処理を再利用する。
                for(auto& frame:image.frames) {
                    auto canvas=frame;
                    std::fill(canvas.pixels.begin(),canvas.pixels.end(),std::byte{});
                    lib::ImageData opaque;opaque.frames.push_back(std::move(canvas));
                    lib::set_image_alpha(opaque,255);
                    auto& destination=opaque.frames.front();
                    if(lib::composite_image_pixels(destination,frame,{},
                        {0,0,float(frame.pixel_width),float(frame.pixel_height)}))
                        frame=std::move(destination);
                }
                applyColorKey(image,maskX,maskY);
            }
        } else if(mask && transparency.kind==lib::ImageTransparency::None) {
            applyColorKey(image,maskX,maskY);
        }
    } else {
        // 既存のPNG以外の規則と、不明・独自コーデックへの代替処理。
        const bool image_cell_ignores_mask =
            isImageCell && (extension == ".jpg" || extension == ".jpeg" || extension == ".png");
        if(mask && !image_cell_ignores_mask) {
            const bool bitmap = extension == ".bmp" || extension == ".dib";
            const bool explicit_bitmap_alpha = bitmap &&
                !lib::editor_window_host_options().all_mode && source_bitmap_alpha;
            if (!explicit_bitmap_alpha) {
                if (bitmap) repairBitmapFillerAlpha(image);
                applyColorKey(image,maskX,maskY);
            }
        }
    }
    return ImageDataWithScale(std::move(image),
                              std::max<std::uint32_t>(1U, scaled.scale));
}

ImageDataWithScale loadImageWithScale(const StringView path,
                                      const std::uint32_t targetScale,
                                      const bool mask, const bool isImageCell,
                                      const int maskX, const int maskY,
                                      lib::Services services) {
    return loadImageWithScale(nullptr, nullptr, nullptr, path, targetScale,
                              mask, isImageCell, maskX, maskY, {}, services);
}

lib::ImageData loadImage(const Props* prop, const Skin* skin,
                         const Summary* summ, const StringView path,
                         const bool mask, const bool isImageCell, const int maskX,
                         const int maskY, const std::vector<String>& stratum,
                         lib::Services services) {
    auto image = loadImageWithScale(prop, skin, summ, path, 1U, mask,
                                    isImageCell, maskX, maskY, stratum,
                                    std::move(services));
    return image.baseData();
}

lib::ImageData loadImage(const StringView path, const bool mask,
                         const bool isImageCell, const int maskX, const int maskY,
                         lib::Services services) {
    return loadImage(nullptr, nullptr, nullptr, path, mask, isImageCell, maskX,
                     maskY, {}, services);
}

lib::ImageData blankImage(const int width, const int height) {
    lib::ImageData image;
    lib::ImageFrame frame;
    frame.pixel_width = static_cast<std::uint32_t>(std::max(1, width));
    frame.pixel_height = static_cast<std::uint32_t>(std::max(1, height));
    frame.logical_size = {static_cast<float>(frame.pixel_width),
                          static_cast<float>(frame.pixel_height)};
    frame.pixel_format = lib::PixelFormat::RGBA32;
    frame.pixels.resize(static_cast<std::size_t>(frame.pixel_width) *
                        frame.pixel_height * 4U);
    image.format = lib::ImageFormat::Png;
    image.frames.push_back(std::move(frame));
    return image;
}

Listener listener(const WidgetPtr& widget, const int type,
                  EventHandler callback) {
    auto object = std::make_shared<ListenerObject>(std::move(callback));
    if (widget) {
        const std::weak_ptr<lib::Widget> weak = widget;
        const auto invoke = [weak, object, type](Event& event) {
            event.widget = weak.lock();
            if (!event.widget) return;
            event.type = type;
            object->handle(event);
        };
        if (type == 1) {
            const auto previous = widget->on_key_down;
            widget->on_key_down = [invoke, previous](lib::KeyEvent& key) {
                if (previous) previous(key);
                Event event;
                event.key = key;
                invoke(event);
                key.handled = key.handled || event.handled;
            };
        } else if (type == 2) {
            const auto previous = widget->on_key_up;
            widget->on_key_up = [invoke, previous](lib::KeyEvent& key) {
                if (previous) previous(key);
                Event event;
                event.key = key;
                invoke(event);
                key.handled = key.handled || event.handled;
            };
        } else {
            // DはSWTのMouseDownからMouseDoubleClickまでを、それぞれ別のイベント種別として登録する。
            const auto pointer_action = [type]() -> std::optional<lib::PointerAction> {
                switch (type) {
                case 3: return lib::PointerAction::Down;
                case 4: return lib::PointerAction::Up;
                case 5: return lib::PointerAction::Move;
                case 6: return lib::PointerAction::Enter;
                case 7: return lib::PointerAction::Leave;
                case 8: return lib::PointerAction::DoubleClick;
                default: return std::nullopt;
                }
            }();
            if (!pointer_action) return object;
            const auto previous = widget->on_pointer;
            widget->on_pointer = [invoke, previous,
                                  expected_action = *pointer_action](lib::PointerEvent& pointer) {
                const auto native_action = pointer.action;
                if (previous) previous(pointer);
                if (native_action != expected_action) return;
                Event event;
                event.pointer = pointer;
                invoke(event);
                pointer.handled = pointer.handled || event.handled;
            };
        }
    }
    return object;
}

Listener listener(const WidgetPtr& widget, const int type, lib::Action callback) {
    return listener(widget, type, EventHandler([callback = std::move(callback)](Event&) {
        if (callback) callback();
    }));
}

Listener listener(EventHandler callback) {
    return std::make_shared<ListenerObject>(std::move(callback));
}

Listener listener(lib::Action callback) {
    return std::make_shared<ListenerObject>(std::move(callback));
}

SpinnerEdit::SpinnerEdit(NumberEditPtr spinner, std::function<void(int)> enter,
                         std::function<void(int)> edit,
                         std::function<int(int)> cancel)
    : spinner_(std::move(spinner)), edit_(std::move(edit)),
      enter_(std::move(enter)), cancel_(std::move(cancel)) {
    const auto control = spinner_.lock();
    if (!control) return;
    oldValue_ = static_cast<int>(std::lround(control->value));
    oldEditing_ = oldValue_;
}

void SpinnerEdit::bindCallbacks() {
    const auto control = spinner_.lock();
    if (!control) return;
    const auto self = shared_from_this();
    control->on_changed = [self](double) { self->valueChanged(); };
    control->on_focus = [self] { self->focusGained(); };
    control->on_blur = [self] { self->focusLost(); };
    control->on_key_down = [self](lib::KeyEvent& event) { self->keyPressed(event); };
}

void SpinnerEdit::enter() {
    const auto spinner = spinner_.lock();
    if (!spinner) return;
    const std::weak_ptr<int> lifetime = lifetime_;
    const auto value = static_cast<int>(std::lround(spinner->value));
    // Dは数値の選択が0かではなく、Spinnerの生の文字列を調べる。表示側
    // を伴わないAPI呼び出しは編集中の値を持たず、数値だけの動作を維持する。
    const auto draft = spinner->edit_text_snapshot();
    if ((!draft.text || !draft.text->empty()) && value != oldValue_) {
        const auto callback = enter_;
        if (callback) callback(value);
        if (lifetime.expired()) return;
    } else {
        suppressEdit_ = true;
        struct Reset {
            bool* flag; std::weak_ptr<int> lifetime;
            ~Reset() { if (!lifetime.expired()) *flag = false; }
        } reset{&suppressEdit_, lifetime};
        const auto callback = cancel_;
        const auto value = callback ? callback(oldValue_) : oldValue_;
        if (lifetime.expired()) return;
        spinner->value = value;
    }
    draft.accepted();
    oldValue_ = static_cast<int>(std::lround(spinner->value));
    oldEditing_ = oldValue_;
}

void SpinnerEdit::keyPressed(lib::KeyEvent& event) {
    if (event.key == lib::Key::Enter) {
        enter();
        event.handled = true;
    } else if (event.key == lib::Key::Escape) {
        const auto spinner = spinner_.lock();
        if (!spinner) return;
        const std::weak_ptr<int> lifetime = lifetime_;
        suppressEdit_ = true;
        struct Reset {
            bool* flag; std::weak_ptr<int> lifetime;
            ~Reset() { if (!lifetime.expired()) *flag = false; }
        } reset{&suppressEdit_, lifetime};
        const auto callback = cancel_;
        const auto value = callback ? callback(oldValue_) : oldValue_;
        if (lifetime.expired()) return;
        spinner->value = value;
        oldValue_ = static_cast<int>(std::lround(spinner->value));
        oldEditing_ = oldValue_;
        event.handled = true;
    }
}

void SpinnerEdit::focusGained() {
    if (const auto spinner = spinner_.lock()) {
        oldValue_ = static_cast<int>(std::lround(spinner->value));
        oldEditing_ = oldValue_;
    }
}

void SpinnerEdit::focusLost() { enter(); }

void SpinnerEdit::valueChanged() {
    const auto spinner = spinner_.lock();
    if (!spinner) return;
    const auto value = static_cast<int>(std::lround(spinner->value));
    const std::weak_ptr<int> lifetime = lifetime_;
    const auto callback = edit_;
    if (!suppressEdit_ && callback && value != oldEditing_ && spinner->visible()) callback(value);
    if (!lifetime.expired()) oldEditing_ = value;
}

TextEditMFListener::TextEditMFListener(
    EditorContext* context, WidgetPtr control, std::function<void(Item)> startEdit,
    std::function<Item()> selection,
    std::function<Item(float, float)> selectionM)
    : context_(context), control_(std::move(control)), selection_(std::move(selection)),
      selectionM_(std::move(selectionM)), startEdit_(std::move(startEdit)) {}

EditStartType TextEditMFListener::quickStart(const EditStartType value) noexcept {
    quickStart_ = value;
    return quickStart_;
}

void TextEditMFListener::widgetSelected() {
    if (quickStart_ == EditStartType::Quick) {
        if (selection_ && startEdit_) startEdit_(selection_());
        return;
    }
    oldSelection2_ = oldSelection_;
    oldSelection_ = selection_ ? selection_() : Item{};
    if (context_ && context_->refreshToolbar) context_->refreshToolbar();
}

void TextEditMFListener::widgetDefaultSelected() {}

void TextEditMFListener::focusGained() {
    hasFocus_ = true;
    g_focus = control_;
    if (quickStart_ == EditStartType::Quick && selection_ && startEdit_)
        startEdit_(selection_());
}

void TextEditMFListener::focusLost() {
    hasFocus_ = false;
    startPending_ = false;
    oldSelection_.reset();
    oldSelection2_.reset();
}

void TextEditMFListener::mouseDoubleClick() { startPending_ = false; }

void TextEditMFListener::mouseUp(lib::PointerEvent& event) {
    if (event.button != lib::PointerButton::Primary || !selectionM_) return;
    const auto item = selectionM_(event.position.x, event.position.y);
    if (!item || !startEdit_) return;
    if (quickStart_ == EditStartType::SingleClick) {
        startEdit_(item);
    } else if (oldSelection2_ && oldSelection2_ == item && hasFocus_) {
        startEdit_(item);
    }
}

namespace {
bool selectedTreeTextHit(const lib::TreeView& tree, const lib::PointerEvent& event) {
    if (lib::editor_window_host_options().all_mode) return true;
    if (!event.item_hit_test_available || event.item_index < 0) return false;
    const auto id = static_cast<lib::TreeItemId>(event.item_index);
    const auto node = std::find_if(tree.nodes.begin(), tree.nodes.end(),
                                   [id](const lib::TreeNode& value) {
                                       return value.id == id;
                                   });
    if (node == tree.nodes.end()) return false;
    const auto position = lib::tree_visible_position(tree, id);
    const auto row_height = lib::item_row_height(
        tree, lib::scaled_ui_metric(lib::kEditorUiMetrics.row_height));
    if (!position || *position < tree.top_index || row_height <= 0.0F) return false;
    const auto scroll = lib::item_scroll_geometry(tree, row_height);
    const auto row_y = scroll.viewport.y +
        static_cast<float>(*position - tree.top_index) * row_height;
    const auto model_index = static_cast<std::size_t>(node - tree.nodes.begin());
    const auto geometry = lib::tree_row_geometry(
        tree, tree.bounds(), model_index, row_y, row_height);
    if (!geometry) return false;
    const auto font = tree.font();
    const auto label_width = lib::ui_text_width(
        node->text, 0.0F, font ? &*font : nullptr);
    const auto label_right = std::min(geometry->text.x + geometry->text.width,
                                      geometry->text.x + label_width);
    return event.position.x >= geometry->text.x && event.position.x < label_right &&
           event.position.y >= geometry->text.y &&
           event.position.y < geometry->text.y + geometry->text.height;
}


bool selectedTableTextHit(const lib::TableView& table,const lib::PointerEvent& event,
                          std::optional<std::size_t> required_column) {
    const auto column=lib::hit_test_table_column(table,event.position.x);
    if (!column || (required_column && column!=required_column)) return false;
    if (!event.item_hit_test_available || event.item_index<0) return false;
    const auto borrowed=std::shared_ptr<lib::TableView>(const_cast<lib::TableView*>(&table),[](lib::TableView*){});
    const auto paint=lib::prepare_item_view(borrowed,lib::scaled_ui_metric(lib::kEditorUiMetrics.row_height));
    for (const auto& row:paint.rows) if(row.model_index==std::size_t(event.item_index)) {
        if (*column>=row.cells.size()) return false;
        const auto& cell=row.cells[*column];
        const auto in=[&](lib::Rect b){return event.position.x>=b.x && event.position.y>=b.y && event.position.x<b.x+b.width && event.position.y<b.y+b.height;};
        return !(cell.icon && in(cell.icon_bounds)) && !(cell.check_box && in(*cell.check_box));
    }
    return false;
}
template <typename View, typename Key, typename Selected, typename Begin>
void installClickSelectedEditImpl(const std::shared_ptr<View>& view,
                                  lib::Services services,
                                  std::function<bool()> quick,
                                  std::function<bool(Key)> editable,
                                  Selected selected, Begin begin,
                                  std::function<bool(const View&,const lib::PointerEvent&)> hit_allowed={},
                                  std::function<std::optional<Key>(const View&,const lib::PointerEvent&)> resolve_hit={},
                                  bool selection_before_down=false) {
    if (!view) return;
    struct State {
        std::optional<Key> pending;
        lib::Point down{};
        std::uint64_t request{};
        std::optional<lib::TimerId> timer;
    };
    auto state = std::make_shared<State>();
    const auto cancel_timer = [state, services] {
        if (state->timer && services.dispatcher)
            services.dispatcher->cancel_timer(*state->timer);
        state->timer.reset();
    };
    const auto previous_blur=view->on_blur;
    view->on_blur=[state,cancel_timer,previous_blur] {
        ++state->request;
        state->pending.reset();
        cancel_timer();
        if (previous_blur) previous_blur();
    };
    const auto previous = view->on_pointer;
    const std::weak_ptr<View> weak = view;
    view->on_pointer = [weak, state, services, quick, editable, selected, begin,
                        cancel_timer, previous, hit_allowed, resolve_hit,
                        selection_before_down](lib::PointerEvent& event) {
        const auto current = weak.lock();
        if (!current) return;
        // DのSlowリスナーは、すでに選択された項目を待つ。CardList
        // は押下で選択するため、Slowは所有元のコールバック前に状態
        // を取得し、QuickはDと同様にコールバック後の選択を使う。
        std::optional<Key> hit;
        if (resolve_hit) {
            hit = resolve_hit(*current, event);
        } else if constexpr (std::is_integral_v<Key>) {
            // Table/Treeのキーは整数の項目インデックスから得る。CardListは型付きの解決処理を使う。
            if (event.item_hit_test_available && event.item_index >= 0)
                hit = static_cast<Key>(event.item_index);
        }
        const auto selected_before = selected(*current);
        if (previous) previous(event);
        if (event.button != lib::PointerButton::Primary) return;
        if ((event.action==lib::PointerAction::Down || event.action==lib::PointerAction::Up) &&
            ((hit_allowed && !hit_allowed(*current,event)) ||
             (resolve_hit && !hit))) {
            ++state->request; cancel_timer(); state->pending.reset(); return;
        }
        switch (event.action) {
        case lib::PointerAction::Down: {
            ++state->request;
            cancel_timer();
            state->down = event.position;
            const auto selected_for_down = selection_before_down && !(quick && quick())
                ? selected_before : selected(*current);
            state->pending = hit && selected_for_down == hit &&
                    (!editable || editable(*hit)) ? hit : std::nullopt;
            if (state->pending && selected(*current) != hit)
                state->pending.reset();
            break;
        }
        case lib::PointerAction::DoubleClick:
        case lib::PointerAction::Cancel:
        case lib::PointerAction::LongPress:
            ++state->request;
            cancel_timer();
            state->pending.reset();
            break;
        case lib::PointerAction::Up: {
            const auto pending = std::exchange(state->pending, std::nullopt);
            if (!pending || (hit && *hit != *pending)) return;
            const float slop = lib::scaled_ui_metric(
                event.pointer_type == lib::PointerType::Touch ? 12.0F : 5.0F);
            if (std::hypot(event.position.x - state->down.x,
                           event.position.y - state->down.y) > slop) return;
            const auto request = ++state->request;
            const auto key = *pending;
            const auto start = [weak, state, request, key, selected, begin, gui=services.gui] {
                if (state->request != request) return;
                state->timer.reset();
                const auto target = weak.lock();
                if (!target || !target->visible() || !target->enabled() ||
                    selected(*target) != std::optional<Key>(key)) return;
                if (gui && gui->focused_widget().get()!=target.get()) return;
                begin(target, key);
            };
            if ((quick && quick()) || !services.dispatcher) start();
            // DのStarterはDisplay.getDoubleClickTime
            // だけ待ってから選択・フォーカスを確認し、Slowの名前変更を始める。
            else state->timer = services.dispatcher->post_delayed(
                services.system ? services.system->double_click_time()
                                : lib::kDefaultDoubleClickTime,
                start);
            break;
        }
        default:
            break;
        }
    };
}

void attachDefaultInlineTextMenu(const lib::Services& services,
                                 const std::shared_ptr<lib::TextEdit>& editor) {
    if (!editor) return;
    // DのTableTextEdit/TreeEditのcreateTextEditor
    // は、このメニューを付ける。共通InlineEditStateMachineはコント
    // ロールを直接作るため、既存の公開の編集部作成コールバックで同じメニューを接続する。
    dmenu::Commons menu_comm;
    menu_comm.services = services;
    dmenu::installMissingTextMenus(editor, menu_comm);
}
}  // namespace

void installClickSelectedEdit(const std::shared_ptr<lib::TableView>& table,
                              lib::Services services, lib::WidgetPtr focus_scope,
                              std::function<bool()> quick,
                              std::function<bool(std::size_t)> editable) {
    if (!table) return;
    const auto gui = services.gui;
    const std::weak_ptr<lib::Widget> scope = focus_scope;
    installClickSelectedEditImpl<lib::TableView, std::size_t>(
        table, services, std::move(quick), std::move(editable),
        [](const lib::TableView& view) -> std::optional<std::size_t> {
            if (view.selection.size() != 1U) return std::nullopt;
            return view.selection.front();
        },
        [gui, scope](const std::shared_ptr<lib::TableView>& view, const std::size_t row) {
            if (!gui || !view->inline_edit_column || !view->on_cell_edited) return;
            lib::ProgrammaticInlineEditRequest request;
            request.table = view;
            request.focus_scope = scope.lock();
            request.row_height = lib::scaled_ui_metric(lib::kEditorUiMetrics.row_height);
            request.matches_stable_row = [row](const std::size_t index) { return index == row; };
            (void)gui->request_programmatic_inline_edit(request);
        }, [](const lib::TableView& view,const lib::PointerEvent& event) {
            return view.inline_edit_column && selectedTableTextHit(view,event,view.inline_edit_column);
        });
    const auto previous_editor_created = table->on_inline_editor_created;
    table->on_inline_editor_created = [services, previous_editor_created](
        const std::size_t row, const std::size_t column,
        const std::shared_ptr<lib::TextEdit>& editor,
        const std::shared_ptr<lib::ComboBox>& combo) {
        if (previous_editor_created)
            previous_editor_created(row, column, editor, combo);
        attachDefaultInlineTextMenu(services, editor);
    };
}

void installClickSelectedAction(const std::shared_ptr<lib::TableView>& table,
                                lib::Services services,
                                std::function<void(std::size_t)> begin,
                                std::function<bool()> quick,
                                std::function<bool(std::size_t)> editable,
                                std::optional<std::size_t> edit_column) {
    if (!begin) return;
    installClickSelectedEditImpl<lib::TableView, std::size_t>(
        table, services, std::move(quick), std::move(editable),
        [](const lib::TableView& view) -> std::optional<std::size_t> {
            if (view.selection.size() != 1U) return std::nullopt;
            return view.selection.front();
        },
        [begin = std::move(begin)](const std::shared_ptr<lib::TableView>&,
                                   const std::size_t row) { begin(row); },
        [edit_column](const lib::TableView& view,const lib::PointerEvent& event) {
            return selectedTableTextHit(view,event,edit_column);
        });
}

void installClickSelectedEdit(const std::shared_ptr<lib::TreeView>& tree,
                              lib::Services services, lib::WidgetPtr focus_scope,
                              std::function<bool()> quick,
                              std::function<bool(lib::TreeItemId)> editable) {
    if (!tree) return;
    const auto gui = services.gui;
    const std::weak_ptr<lib::Widget> scope = focus_scope;
    installClickSelectedEditImpl<lib::TreeView, lib::TreeItemId>(
        tree, services, std::move(quick), std::move(editable),
        [](const lib::TreeView& view) -> std::optional<lib::TreeItemId> {
            if (view.selection.size() != 1U) return std::nullopt;
            return view.selection.front();
        },
        [gui, scope](const std::shared_ptr<lib::TreeView>& view, const lib::TreeItemId id) {
            if (!gui || !view->on_item_edited) return;
            lib::ProgrammaticInlineEditRequest request;
            request.tree = view;
            request.focus_scope = scope.lock();
            request.row_height = lib::scaled_ui_metric(lib::kEditorUiMetrics.row_height);
            request.matches_stable_tree_node = [id](const lib::TreeNode& node) {
                return node.id == id;
            };
            (void)gui->request_programmatic_inline_edit(request);
        }, [](const lib::TreeView& view, const lib::PointerEvent& event) {
            return selectedTreeTextHit(view, event);
        });
    const auto previous_editor_created = tree->on_inline_editor_created;
    tree->on_inline_editor_created = [services, previous_editor_created](
        const std::shared_ptr<lib::TextEdit>& editor) {
        if (previous_editor_created) previous_editor_created(editor);
        attachDefaultInlineTextMenu(services, editor);
    };
}

void installClickSelectedEdit(const CardListPtr& list, lib::Services services,
                              lib::WidgetPtr focus_scope,
                              std::function<bool()> quick,
                              std::function<bool(const CardListCard&)> editable,
                              std::function<void(const CardListCard&)> begin) {
    if (!list || !begin) return;
    const auto resolve_title_hit = [](const cardlist::CardList<CardListCard>& view,
                                      const lib::PointerEvent& event)
        -> std::optional<CardListCard> {
        const auto bounds = view.bounds();
        const float x = event.position.x - bounds.x;
        const float y = event.position.y - bounds.y;
        const int index = view.searchIndex(static_cast<int>(std::lround(x)),
                                           static_cast<int>(std::lround(y)));
        if (index < 0) return {};
        const auto title = view.getTitleBounds(index);
        if (title.width <= 0.0F || title.height <= 0.0F ||
            x < title.x || y < title.y ||
            x >= title.x + title.width || y >= title.y + title.height)
            return {};
        auto value = view.card(index);
        return value ? std::optional<CardListCard>(std::move(value)) : std::nullopt;
    };
    const auto gui = services.gui;
    const std::weak_ptr<lib::Widget> scope = focus_scope;
    std::function<bool(CardListCard)> can_edit;
    if (editable) {
        can_edit = [editable = std::move(editable)](CardListCard value) {
            return editable(value);
        };
    }
    installClickSelectedEditImpl<cardlist::CardList<CardListCard>, CardListCard>(
        list, services, std::move(quick), std::move(can_edit),
        [](const cardlist::CardList<CardListCard>& view)
            -> std::optional<CardListCard> {
            if (view.selectionCount() != 1) return {};
            auto value = view.selectionCard();
            return value ? std::optional<CardListCard>(std::move(value)) : std::nullopt;
        },
        [gui, scope, begin = std::move(begin)](
            const std::shared_ptr<cardlist::CardList<CardListCard>>& view,
            const CardListCard& value) {
            const auto host = scope.lock();
            if (gui && host && lib::widget_in_input_scope(host, view)) begin(value);
        }, {}, resolve_title_hit, true);
}

TextEditKListener::TextEditKListener(std::function<void(Item)> startEdit,
                                     std::function<Item()> selection)
    : selection_(std::move(selection)), startEdit_(std::move(startEdit)) {}

void TextEditKListener::keyPressed(lib::KeyEvent& event) {
    if (event.key == lib::Key::F2 && selection_ && startEdit_) {
        startEdit_(selection_());
        event.handled = true;
    }
}

EditEnd::EditEnd(EditorContext* context, ContainerPtr parent, WidgetPtr control,
                 std::function<void(WidgetPtr)> end, const bool canOpenCombo)
    : context_(context), parent_(std::move(parent)), control_(std::move(control)),
      end_(std::move(end)), canOpenCombo_(canOpenCombo) {}

void EditEnd::setFocus() {
    if (!control_) return;
    control_->set_visible(true);
    g_focus = control_;
    if (context_ && context_->comboListVisible) {
        if (const auto combo = std::dynamic_pointer_cast<lib::ComboBox>(control_))
            combo->set_enabled(true);
    }
}

void EditEnd::focusGained() { g_focus = control_; }
void EditEnd::focusLost() { focusOut(); }

void EditEnd::focusOut() {
    if (g_focus.lock() == control_) return;
    enter();
}

void EditEnd::enter() {
    if (exited_ || entering_ || !control_) return;
    const std::weak_ptr<int> lifetime = lifetime_;
    const auto control = control_;
    const auto parent = parent_;
    const auto end = end_;
    const auto callbacks = exitEvent;
    entering_ = true;
    struct Reset {
        bool* flag; std::weak_ptr<int> lifetime;
        ~Reset() { if (!lifetime.expired()) *flag = false; }
    } reset{&entering_, lifetime};
    try {
        if (end) end(control);
    } catch (const std::exception& error) {
        // Dは通常の編集コールバックの例外を記録し、それでも編集部を破棄する。
        lib::editor_action_log(error.what());
    }
    const bool restoreParentFocus = g_focus.lock() == control;
    if (parent) parent->remove(control);
    control->set_visible(false);
    // DのEditEndは、フォーカス中の編集部を破棄した後にフォーカスを戻す。
    if (restoreParentFocus) g_focus = parent;
    if (lifetime.expired()) return;
    exited_ = true;
    for (const auto& callback : callbacks) {
        if (callback) callback(false);
        if (lifetime.expired()) return;
    }
}

void EditEnd::cancel() {
    if (exited_) return;
    if (parent_ && control_) parent_->remove(control_);
    if (control_) control_->set_visible(false);
    exited_ = true;
    for (const auto& callback : exitEvent) if (callback) callback(true);
}

TextEditPtr createTextEditor(EditorContext* context, Props* prop,
                             ContainerPtr parent, const StringView text) {
    auto editor = std::make_shared<lib::TextEdit>(String(text));
    editor->selection_start = 0U;
    editor->selection_length = editor->text.size();
    if (parent) parent->add(editor);
    if (context) {
        dmenu::Commons menu_comm;
        menu_comm.services = context->services;
        dmenu::installMissingTextMenus(editor, menu_comm);
    }
    (void)prop;
    return editor;
}

String searchResultScenarioRelativeLabel(const StringView format,
                                         const StringView resourcePath,
                                         const StringView scenarioPath) {
    const auto absolute_resource = ::cwxeditor::cwx::utils::nabs(resourcePath);
    const auto absolute_scenario = ::cwxeditor::cwx::utils::nabs(scenarioPath);
    const auto relative = ::cwxeditor::cwx::utils::abs2rel(
        StringView(absolute_resource), StringView(absolute_scenario));
    return ::cwxeditor::cwx::utils::tryFormat(
        format, ::cwxeditor::cwx::utils::encodePath(relative));
}

AbstractTableEdit::AbstractTableEdit(
    EditorContext* context, TablePtr table, const int editColumn,
    std::function<bool(TableItem, int)> canEdit)
    : context_(context), table_(std::move(table)), editColumn_(editColumn),
      canEdit_(std::move(canEdit)) {
    if (!table_) return;
    const std::weak_ptr<int> lifetime = lifetime_;
    table_->on_selection_changed = [this, lifetime](const std::vector<std::size_t>&) {
        if (lifetime.expired() || !isEditing()) return;
        // Dは編集前の選択に対して確定してから新しい選択を戻す。編集コール
        // バックが例外を出した場合も同様。入れ子のイベントは通知しない。
        const auto target = table_;
        struct RestoreSelection {
            TablePtr table;
            std::vector<std::size_t> selection;
            ~RestoreSelection() { table->selection = std::move(selection); }
        } restore{target, target->selection};
        target->selection = selectionsBeforeEdit_;
        enter();
    };
    table_->on_cell_activate = [this, lifetime](std::size_t row, std::size_t column) {
        if (lifetime.expired()) return;
        if (static_cast<int>(column) == editColumn_) startEdit(row);
    };
}

void AbstractTableEdit::startEdit() {
    if (table_ && table_->selection.size() == 1U) startEdit(table_->selection.front());
}

EditStartType AbstractTableEdit::quickStart(const EditStartType value) noexcept {
    quickStart_ = value;
    return quickStart_;
}

void AbstractTableEdit::startEdit(const TableItem item) {
    if (!table_ || item >= table_->rows.size() ||
        (canEdit_ && !canEdit_(item, editColumn_))) return;
    const std::weak_ptr<int> lifetime = lifetime_;
    if (isEditing()) enter();
    if (lifetime.expired()) return;
    auto control = createEditor(item, editColumn_);
    if (lifetime.expired() || !control) return;
    selectionsBeforeEdit_ = table_->selection;
    editingItem_ = item;
    editEnd_ = std::make_shared<EditEnd>(context_, ContainerPtr{}, control,
                                         [this, lifetime](WidgetPtr value) {
                                             if (!lifetime.expired()) end(value);
                                         },
                                         quickStart_ != EditStartType::Quick);
    editEnd_->exitEvent.push_back([this, lifetime](bool cancel) {
        // DのGC所有元と異なり、この補助オブジェクトは、ローカルで保持したEditEnd
        // が終了処理中でも、編集・終了のコールバックで削除され得る。
        if (lifetime.expired()) return;
        const auto callbacks = exitEvent;
        for (const auto& callback : callbacks) {
            if (callback) callback(cancel);
            if (lifetime.expired()) return;
        }
        if (cancel || (editEnd_ && editEnd_->isExit())) editingItem_.reset();
    });
    editEnd_->setFocus();
}

WidgetPtr AbstractTableEdit::editor() const noexcept {
    return editEnd_ ? editEnd_->editor() : WidgetPtr{};
}

void AbstractTableEdit::enter() {
    if (editEnd_) {
        const std::weak_ptr<int> lifetime = lifetime_;
        auto end = editEnd_;
        end->enter();
        if (lifetime.expired()) return;
        editEnd_.reset();
        editingItem_.reset();
    }
}

void AbstractTableEdit::cancel() {
    if (editEnd_) {
        auto end = editEnd_;
        end->cancel();
        editEnd_.reset();
        editingItem_.reset();
    }
}

WidgetPtr AbstractTableEdit::createEditor(const TableItem item, const int column) {
    if (!table_ || item >= table_->rows.size() || column < 0 ||
        static_cast<std::size_t>(column) >= table_->rows[item].cells.size()) return {};
    auto editor = std::make_shared<lib::TextEdit>(table_->rows[item].cells[column].text);
    return editor;
}

void AbstractTableEdit::end(WidgetPtr) {}

TableTextEdit::TableTextEdit(
    EditorContext* context, Props* prop, TablePtr table, const int editColumn,
    std::function<void(TableItem, int, String)> editEnd,
    std::function<bool(TableItem, int)> canEdit,
    std::function<WidgetPtr(TableItem, int)> createEditor)
    : AbstractTableEdit(context, std::move(table), editColumn, std::move(canEdit)),
      prop_(prop), editEnd_(std::move(editEnd)), createEditor_(std::move(createEditor)) {}

TextEditPtr TableTextEdit::editor() const noexcept {
    return std::dynamic_pointer_cast<lib::TextEdit>(AbstractTableEdit::editor());
}

WidgetPtr TableTextEdit::createEditor(const TableItem item, const int column) {
    if (createEditor_) return createEditor_(item, column);
    auto tableView = table();
    if (!tableView || item >= tableView->rows.size() || column < 0 ||
        static_cast<std::size_t>(column) >= tableView->rows[item].cells.size()) return {};
    return createTextEditor(context(), prop_, {}, tableView->rows[item].cells[column].text);
}

void TableTextEdit::end(WidgetPtr control) {
    const auto text = [&]() -> String {
        if (const auto editor = std::dynamic_pointer_cast<lib::TextEdit>(control))
            return editor->text;
        if (const auto combo = std::dynamic_pointer_cast<lib::ComboBox>(control))
            return combo->edit_text;
        return {};
    }();
    auto* item = editingItem();
    if (!item) return;
    const auto callback = editEnd_;
    if (callback) callback(*item, editColumn(), text);
    else if (!text.empty() && table() && *item < table()->rows.size() && editColumn() >= 0 &&
             static_cast<std::size_t>(editColumn()) < table()->rows[*item].cells.size())
        (void)lib::publish_table_cell_text(
            *table(), *item, static_cast<std::size_t>(editColumn()), text);
}

TableTCEdit::TableTCEdit(EditorContext* context, TablePtr table, const int editColumn,
                         CreateEditor createEditor, EditCallback editEnd,
                         std::function<bool(TableItem, int)> canEdit)
    : AbstractTableEdit(context, std::move(table), editColumn, std::move(canEdit)),
      createEditor_(std::move(createEditor)), editEnd_(std::move(editEnd)) {}

WidgetPtr TableTCEdit::createEditor(const TableItem item, const int column) {
    return createEditor_ ? createEditor_(item, column) : WidgetPtr{};
}

void TableTCEdit::end(WidgetPtr control) {
    auto* item = editingItem();
    if (!item || !control) return;
    const auto callback = editEnd_;
    if (callback) callback(*item, editColumn(), control);
    else if (table() && *item < table()->rows.size() && editColumn() >= 0 &&
             static_cast<std::size_t>(editColumn()) < table()->rows[*item].cells.size()) {
        String value;
        if (const auto text = std::dynamic_pointer_cast<lib::TextEdit>(control)) value = text->text;
        if (const auto combo = std::dynamic_pointer_cast<lib::ComboBox>(control)) value = combo->edit_text;
        if (const auto number = std::dynamic_pointer_cast<lib::NumberEdit>(control)) value = std::to_string(static_cast<int>(number->value));
        (void)lib::publish_table_cell_text(
            *table(), *item, static_cast<std::size_t>(editColumn()),
            std::move(value));
    }
}

TreeEdit::TreeEdit(EditorContext* context, TreePtr tree,
                   std::function<void(lib::TreeItemId, WidgetPtr)> editEnd,
                   std::function<WidgetPtr(lib::TreeItemId)> createEditor)
    : context_(context), tree_(std::move(tree)), editCallback_(std::move(editEnd)),
      createEditor_(std::move(createEditor)) {
    if (tree_) {
        tree_->on_item_edited = [this](lib::TreeItemId id, String text) {
            if (!tree_) return;
            auto nodes = tree_->nodes;
            const auto found = std::find_if(nodes.begin(), nodes.end(),
                                            [id](const lib::TreeNode& node) {
                                                return node.id == id;
                                            });
            if (found == nodes.end()) return;
            found->text = std::move(text);
            (void)lib::replace_tree_nodes(tree_, std::move(nodes), tree_->selection);
        };
        tree_->on_activate = [this](lib::TreeItemId id) { startEdit(id); };
    }
}

void TreeEdit::startEdit() {
    if (tree_ && tree_->selection.size() == 1U) startEdit(tree_->selection.front());
}

void TreeEdit::startEdit(const lib::TreeItemId item) {
    if (!tree_ || !findNode(tree_, item)) return;
    if (editEnd_) enter();
    const auto* node = findNode(tree_, item);
    if (!node || !lib::tree_inline_edit_admitted(*tree_, *node)) return;
    auto control = createEditor_ ? createEditor_(item) :
        createTextEditor(context_, nullptr, {}, node->text);
    if (!control) return;
    editingItem_ = item;
    editEnd_ = std::make_shared<EditEnd>(context_, ContainerPtr{}, control,
                                         [this](WidgetPtr value) {
                                             if (editCallback_) editCallback_(editingItem_, value);
                                         });
    editEnd_->setFocus();
}

void TreeEdit::cancel() {
    if (editEnd_) {
        editEnd_->cancel();
        editEnd_.reset();
        editingItem_ = 0U;
    }
}

void TreeEdit::enter() {
    if (editEnd_) {
        editEnd_->enter();
        editEnd_.reset();
        editingItem_ = 0U;
    }
}

bool hasFocus(const WidgetPtr& widget) {
    const auto focused = g_focus.lock();
    if (!widget || !focused) return false;
    // Dはコントロールの親をたどる。C++の共有表示は実際の包含
    // 親を複数持ち得るため、Containerの子配列を木に作
    // り変えず、それらの接続とWindowの所有元を巡回する。
    std::vector<const lib::Widget*> pending{focused.get()};
    std::unordered_set<const lib::Widget*> visited;
    std::vector<WindowPtr> owners;
    while (!pending.empty()) {
        const auto* current = pending.back();
        pending.pop_back();
        if (!current || !visited.insert(current).second) continue;
        if (current == widget.get()) return true;
        for (const auto* parent : current->containing_parents()) pending.push_back(parent);
        if (const auto shell = dynamic_cast<const lib::Window*>(current)) {
            if (auto owner = shell->owner.lock()) {
                pending.push_back(owner.get());
                owners.push_back(std::move(owner));
            }
        }
    }
    return false;
}

WindowPtr topShell(WindowPtr shell) {
    std::unordered_set<const lib::Window*> visited;
    while (shell && visited.insert(shell.get()).second) {
        const auto parent = shell->owner.lock();
        if (!parent || visited.contains(parent.get())) break;
        shell = parent;
    }
    return shell;
}

bool isDescendant(const WindowPtr& shell1, const WindowPtr& shell2) {
    auto current = shell2;
    std::unordered_set<const lib::Window*> visited;
    while (current != shell1) {
        if (!current || !visited.insert(current.get()).second) return false;
        current = current->owner.lock();
    }
    return true;
}

bool isDescendant(const ContainerPtr& container, const WidgetPtr& widget) {
    return container && (container == widget || containsWidget(container, widget));
}

TreeItemRef createTreeItem(const TreePtr& tree, const lib::TreeItemId parent,
                           const StringView text, const ResourceId image,
                           const std::size_t index, const StringView data) {
    if (!tree) return {};
    lib::TreeNode node;
    node.parent = parent;
    node.text = String(text);
    node.value = String(data);
    node.icon = image;
    return {tree, insertTreeNode(tree, std::move(node), index)};
}

TreeItemRef topItem(TreeItemRef item) {
    if (!item.tree) return {};
    auto* node = findNode(item.tree, item.id);
    while (node && node->parent != 0U) {
        item.id = node->parent;
        node = findNode(item.tree, item.id);
    }
    return item;
}

int treeItemUD(TreeItemRef item, const bool down) {
    if (!item.tree) return -1;
    const auto* node = findNode(item.tree, item.id);
    if (!node) return -1;
    const auto siblings = children(item.tree, node->parent);
    const auto position = std::find_if(siblings.begin(), siblings.end(),
                                       [item](const lib::TreeNode* value) { return value->id == item.id; });
    if (position == siblings.end()) return -1;
    const auto index = static_cast<int>(std::distance(siblings.begin(), position));
    const auto target = down ? index + 1 : index - 1;
    if (target < 0 || target >= static_cast<int>(siblings.size())) return -1;
    swapTreeSubtrees(item.tree, item.id, siblings[static_cast<std::size_t>(target)]->id);
    return index;
}

int treeItemUp(TreeItemRef item) { return treeItemUD(std::move(item), false); }
int treeItemDown(TreeItemRef item) { return treeItemUD(std::move(item), true); }

int treeItemUD2(const TreePtr& tree, TreeItemRef item, const bool down) {
    if (tree && !item.tree) item.tree = tree;
    return treeItemUD(std::move(item), down);
}

TreeItemRef cloneItem(const TreePtr& tree, TreeItemRef old, const std::size_t index) {
    if (!tree || !old.tree) return {};
    const auto sourceNodes = subtreeIds(old.tree, old.id);
    if (sourceNodes.empty()) return {};
    std::vector<lib::TreeNode> snapshot;
    snapshot.reserve(sourceNodes.size());
    for (const auto& sourceId : sourceNodes)
        if (const auto* source = findNode(old.tree, sourceId)) snapshot.push_back(*source);
    if (snapshot.empty()) return {};
    const auto sourceRootParent = snapshot.front().parent;
    const auto targetParent = findNode(tree, sourceRootParent) ? sourceRootParent : 0U;
    std::unordered_map<lib::TreeItemId, lib::TreeItemId> ids;
    TreeItemRef result;
    for (std::size_t i = 0; i < snapshot.size(); ++i) {
        auto node = snapshot[i];
        const auto oldId = node.id;
        if (oldId == old.id) {
            node.parent = targetParent;
            node.id = 0U;
            const auto newId = insertTreeNode(tree, std::move(node), index);
            result = {tree, newId};
            ids.emplace(oldId, newId);
        } else {
            const auto parent = ids.find(node.parent);
            node.parent = parent == ids.end() ? targetParent : parent->second;
            node.id = 0U;
            const auto newId = insertTreeNode(tree, std::move(node), static_cast<std::size_t>(-1));
            ids.emplace(oldId, newId);
        }
    }
    return result;
}

void swapTreeItem(const TreePtr& tree, const std::size_t index1,
                  const std::size_t index2, const lib::TreeItemId parent) {
    if (!tree || index1 == index2) return;
    auto siblings = children(tree, parent);
    if (index1 >= siblings.size() || index2 >= siblings.size()) return;
    swapTreeSubtrees(tree, siblings[index1]->id, siblings[index2]->id);
}

void treeExpandedAll(TreeItemRef item) {
    if (!item.tree) return;
    if (!findNode(item.tree, item.id)) return;
    auto nodes = item.tree->nodes;
    for (auto& node : nodes)
        if (node.id == item.id || isTreeDescendant(item.tree, node.id, item.id))
            node.expanded = true;
    (void)lib::replace_tree_nodes(item.tree, std::move(nodes), item.tree->selection);
}

void treeExpandedAll(const TreePtr& tree) {
    if (!tree) return;
    auto nodes = tree->nodes;
    for (auto& node : nodes) node.expanded = true;
    (void)lib::replace_tree_nodes(tree, std::move(nodes), tree->selection);
}

void treeUnexpandedAll(TreeItemRef item) {
    if (!item.tree) return;
    if (!findNode(item.tree, item.id)) return;
    auto nodes = item.tree->nodes;
    for (auto& node : nodes)
        if (node.id == item.id || isTreeDescendant(item.tree, node.id, item.id))
            node.expanded = false;
    (void)lib::replace_tree_nodes(item.tree, std::move(nodes), item.tree->selection);
}

void treeUnexpandedAll(const TreePtr& tree) {
    if (!tree) return;
    auto nodes = tree->nodes;
    for (auto& node : nodes) node.expanded = false;
    (void)lib::replace_tree_nodes(tree, std::move(nodes), tree->selection);
}

TreeItemRef anotherTreeItem(const TreePtr& tree, TreeItemRef item) {
    if (!tree || !item.tree) return {};
    if (tree == item.tree) return item;
    return fromTreePath(tree, toTreePath(std::move(item)));
}

std::vector<int> toTreePath(TreeItemRef item) {
    std::vector<int> result;
    if (!item.tree) return result;
    auto* node = findNode(item.tree, item.id);
    while (node) {
        const auto siblings = children(item.tree, node->parent);
        const auto position = std::find_if(siblings.begin(), siblings.end(),
                                           [node](const lib::TreeNode* value) { return value->id == node->id; });
        result.push_back(position == siblings.end() ? 0 : static_cast<int>(std::distance(siblings.begin(), position)));
        if (node->parent == 0U) break;
        node = findNode(item.tree, node->parent);
    }
    std::reverse(result.begin(), result.end());
    return result;
}

TreeItemRef fromTreePath(const TreePtr& tree, const std::vector<int>& pathValue) {
    if (!tree || pathValue.empty()) return {};
    lib::TreeItemId parent = 0U;
    TreeItemRef result;
    for (const auto index : pathValue) {
        const auto siblings = children(tree, parent);
        if (index < 0 || index >= static_cast<int>(siblings.size())) return {};
        result = {tree, siblings[static_cast<std::size_t>(index)]->id};
        parent = result.id;
    }
    return result;
}

WidgetPtr createToolItemC(const std::shared_ptr<lib::ToolBar>& bar,
                          WidgetPtr child, const int rightSpace) {
    if (bar && child) child->set_layout_item({0U, 0U, 1U, 1U, lib::Align::Stretch,
                                               lib::Align::Stretch, {}, {}, {},
                                               static_cast<float>(rightSpace)});
    return child;
}

std::shared_ptr<lib::Label> createLabel(const std::shared_ptr<lib::ToolBar>& bar,
                                        const StringView label, const int rightSpace) {
    auto result = std::make_shared<lib::Label>(String(label));
    createToolItemC(bar, result, rightSpace);
    return result;
}

NumberEditPtr createSpinner(const std::shared_ptr<lib::ToolBar>& bar,
                            const StringView label, const int maximum,
                            const int minimum, const int selection,
                            std::function<void(int)> edit,
                            std::function<void(int)> enter,
                            std::function<int(int)> cancel,
                            const bool readOnly) {
    (void)label;
    auto result = std::make_shared<lib::NumberEdit>();
    result->maximum = maximum;
    result->minimum = minimum;
    result->value = selection;
    result->read_only = readOnly;
    createToolItemC(bar, result, 0);
    if (edit || enter || cancel) {
        auto state = std::make_shared<SpinnerEdit>(
            result, std::move(enter), std::move(edit), std::move(cancel));
        state->bindCallbacks();
    }
    return result;
}

bool openFolder(const StringView path, lib::Services services) {
    if (services.shell) {
        std::string error;
        return services.shell->open_path(filesystemPath(path), error);
    }
    std::error_code error;
    return std::filesystem::exists(filesystemPath(path), error);
}

bool openFolderWithFile(const StringView path, lib::Services services) {
    // Dはパスを解決してから親を取得する。一部の共通Shellバックエン
    // ドは、単独の相対ファイル名の表示要求をファイル自身として扱うため。
    const auto target = utils::nabs(filesystemPath(path));
    if (services.shell) {
        std::string error;
        if (services.shell->reveal_path(target, error)) return true;
    }
    // 未対応・失敗した選択表示の基本処理も、全Shellバックエ
    // ンドで同じ親フォルダー表示の代替規則を使えるようにする。
    const auto parent = target.parent_path();
    return openFolder(pathString(parent.empty() ? target : parent), services);
}

void drawCenterTextImpl(lib::DrawingContext& gc, const lib::FontSpec& font,
                        const lib::Rect area, const StringView text) {
    const auto size = gc.measure_text(text, font, 0.0F);
    gc.draw_text(text, {area.x + (area.width - size.width) / 2.0F,
                        area.y + (area.height - size.height) / 2.0F}, font, {});
}

void drawCenterText(lib::DrawingContext& gc, const lib::FontSpec& font,
                    const lib::Rect area, const StringView text) {
    drawCenterTextImpl(gc, font, area, text);
}

void drawCenterText(lib::DrawingContext& gc, const structs::CFont& font,
                    const lib::Rect area, const StringView text) {
    drawCenterTextImpl(gc, createFontFromPixels(font), area, text);
}

void hemming(lib::DrawingContext& gc, const StringView text, const int x,
             const int y, const lib::Color color, const lib::FontSpec& font) {
    const lib::Color previous{0, 0, 0, color.alpha};
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx)
            if (dx != 0 || dy != 0) gc.draw_text(text, {static_cast<float>(x + dx), static_cast<float>(y + dy)}, font, previous);
    gc.draw_text(text, {static_cast<float>(x), static_cast<float>(y)}, font, color);
}

namespace {

struct Rgba {
    std::uint8_t r{}, g{}, b{}, a{255U};
};

[[nodiscard]] ImageDataWithScale fromSkinImage(
    dskin::ImageDataWithScale value) {
    return ImageDataWithScale(std::move(value.data),
                              std::max(1U, value.source_scale));
}

[[nodiscard]] Rgba pixelAt(const lib::ImageFrame& frame, const int x,
                           const int y) noexcept {
    if (x < 0 || y < 0 || x >= static_cast<int>(frame.pixel_width) ||
        y >= static_cast<int>(frame.pixel_height)) return {0U, 0U, 0U, 0U};
    const auto channels = frame.pixel_format == lib::PixelFormat::Gray8 ? 1U
                         : frame.pixel_format == lib::PixelFormat::RGB24 ||
                           frame.pixel_format == lib::PixelFormat::BGR24 ? 3U
                         : frame.pixel_format == lib::PixelFormat::RGBA32 ||
                           frame.pixel_format == lib::PixelFormat::BGRA32 ? 4U : 0U;
    if (channels == 0U) return {0U, 0U, 0U, 0U};
    const auto offset = (static_cast<std::size_t>(y) * frame.pixel_width +
                         static_cast<std::size_t>(x)) * channels;
    if (offset + channels > frame.pixels.size()) return {0U, 0U, 0U, 0U};
    const auto byte = [&](const std::size_t index) {
        return static_cast<std::uint8_t>(std::to_integer<unsigned int>(
            frame.pixels[offset + index]));
    };
    switch (frame.pixel_format) {
    case lib::PixelFormat::Gray8: return {byte(0U), byte(0U), byte(0U), 255U};
    case lib::PixelFormat::RGB24: return {byte(0U), byte(1U), byte(2U), 255U};
    case lib::PixelFormat::BGR24: return {byte(2U), byte(1U), byte(0U), 255U};
    case lib::PixelFormat::RGBA32: return {byte(0U), byte(1U), byte(2U), byte(3U)};
    case lib::PixelFormat::BGRA32: return {byte(2U), byte(1U), byte(0U), byte(3U)};
    default: return {0U, 0U, 0U, 0U};
    }
}

void setPixel(lib::ImageFrame& frame, const int x, const int y,
              const Rgba value) noexcept {
    if (x < 0 || y < 0 || x >= static_cast<int>(frame.pixel_width) ||
        y >= static_cast<int>(frame.pixel_height)) return;
    const auto offset = (static_cast<std::size_t>(y) * frame.pixel_width +
                         static_cast<std::size_t>(x)) * 4U;
    if (offset + 4U > frame.pixels.size()) return;
    frame.pixels[offset + 0U] = static_cast<std::byte>(value.r);
    frame.pixels[offset + 1U] = static_cast<std::byte>(value.g);
    frame.pixels[offset + 2U] = static_cast<std::byte>(value.b);
    frame.pixels[offset + 3U] = static_cast<std::byte>(value.a);
}

void blendImage(lib::ImageFrame& target, const lib::ImageFrame& source,
                 const int left, const int top, int width, int height) {
    if (source.pixel_width == 0U || source.pixel_height == 0U) return;
    width = std::max(1, width);
    height = std::max(1, height);
    for (int y = 0; y < height; ++y) {
        const auto sy = std::min(static_cast<int>(source.pixel_height) - 1,
                                 y * static_cast<int>(source.pixel_height) / height);
        for (int x = 0; x < width; ++x) {
            const auto sx = std::min(static_cast<int>(source.pixel_width) - 1,
                                     x * static_cast<int>(source.pixel_width) / width);
            const auto src = pixelAt(source, sx, sy);
            if (src.a == 0U) continue;
            auto dst = pixelAt(target, left + x, top + y);
            const auto alpha = static_cast<unsigned int>(src.a);
            const auto inverse = 255U - alpha;
            dst.r = static_cast<std::uint8_t>((src.r * alpha + dst.r * inverse) / 255U);
            dst.g = static_cast<std::uint8_t>((src.g * alpha + dst.g * inverse) / 255U);
            dst.b = static_cast<std::uint8_t>((src.b * alpha + dst.b * inverse) / 255U);
            dst.a = static_cast<std::uint8_t>(std::min(255U, alpha +
                static_cast<unsigned int>(dst.a) * inverse / 255U));
            setPixel(target, left + x, top + y, dst);
        }
    }
}

void appendImage(lib::ImageFrame& target, ImageDataWithScale image,
                 const std::uint32_t scale, const int logicalX,
                 const int logicalY) {
    if (!image.valid()) return;
    auto data = image.scaled(scale);
    if (data.frames.empty()) return;
    const auto& source = data.frames.front();
    blendImage(target, source, logicalX * static_cast<int>(scale),
               logicalY * static_cast<int>(scale),
               static_cast<int>(source.pixel_width),
               static_cast<int>(source.pixel_height));
}

void rasterCaption(lib::ImageFrame& canvas, std::shared_ptr<lib::GraphicsBackend> graphics,
    String text, lib::FontSpec font, lib::Color foreground, lib::Color outline,
    float x, float y, float shrink = 0, bool smoothing = false) {
    if (!graphics || text.empty()) return;
    lib::ImageTextStyle style{std::move(font),foreground,outline,shrink,smoothing};
    style.preserve_font_antialias=true;
    const auto image=lib::rasterize_image_text(graphics,std::move(text),std::move(style));
    if (image.pixel_width) (void)lib::composite_image_pixels(canvas,image,{x-1,y-1},
        {0,0,float(canvas.pixel_width),float(canvas.pixel_height)});
}

ImageDataWithScale nonFileCardLayer(const card::CardImage& image,
    const Props* prop, const Skin* skin, const Summary* summ,
    const structs::CInsets padding, const lib::ImageScaleMode mode,
    const int width, const int height, lib::Services services) {
    if (!prop || !skin || image.type == types::CardImageType::File) return {};
    if (!services.graphics) services.graphics=prop->images().services().graphics;
    const auto scale=std::max(1U,prop->drawingScale());
    auto data=blankImage(width,height);auto& canvas=data.frames.front();
    canvas.logical_size={float(width)/scale,float(height)/scale};
    if (image.type == types::CardImageType::PCNumber) {
        if (!image.pcNumber()) return {};
        const auto insets=prop->looks().menuCardInsets();
        auto font=prop->adjustFont(prop->looks().pcNumberFont(skin->legacy()));
        font.point/=2;
        auto spec=fontSpecFromDwtData(font,false);spec.points*=scale;
        const auto text=std::to_string(image.pcNumber());
        if (!services.graphics) return {};
        const auto extent=services.graphics->measure_text(text,spec,0);
        const auto x=float(insets.w*int(scale))+(width-(insets.w+insets.e)*int(scale)-extent.width)/2;
        const auto y=float(insets.n*int(scale))+(height-(insets.n+insets.s)*int(scale)-extent.height)/2;
        rasterCaption(canvas,services.graphics,text,spec,{0,0,0,255},{0,0,0,0},x,y);
    } else {
        auto loaded=areaviewutils::detail::detailTalkerImage(
            *const_cast<Props*>(prop),*skin,summ,image.talker());
        ImageDataWithScale layer(std::move(loaded.data),std::max(1U,loaded.source_scale));
        if (!layer.valid()) return {};
        auto scaled=layer.scaled(scale);if(scaled.frames.empty())return {};
        auto insets=padding;
        if (image.talker()==types::Talker::Card) {
            // Dは先にカードサイズの画像を準備し、Centerも
            // 含め、内側余白0で要求された重ね配置を適用する。
            const auto size=prop->looks().cardSize();
            scaled=resizeImage(scaled,std::max(1U,size.width*scale),std::max(1U,size.height*scale));
            insets={};
        }
        const auto& frame=scaled.frames.front();
        int x=insets.w*int(scale),y=insets.n*int(scale);
        int w=int(frame.pixel_width),h=int(frame.pixel_height);
        if (mode==lib::ImageScaleMode::Stretch) {
            w=std::max(1,width-(insets.w+insets.e)*int(scale));
            h=std::max(1,height-(insets.n+insets.s)*int(scale));
        } else if (mode==lib::ImageScaleMode::Center) {
            x=(width-w)/2;y=(height-h)/2;
        }
        // NoneはDのCutに対応する。他の共通モードはここでは自然なサイズを
        // 保つ。Fit/Fill/TileがDのScaleと同等とはみなさない。
        blendImage(canvas,frame,x,y,w,h);
    }
    return ImageDataWithScale(std::move(data),scale);
}

void paintStatusBackground(lib::ImageFrame& frame,
                           const structs::CRGB& color) {
    // Dは元のアイコンの背後に、指定された不透明RGBの矩形を描く。透
    // 明なアイコン画素では背景が見え、不透明画素は元のRGBを保つ。
    const auto icon=frame;
    const Rgba background{
        static_cast<std::uint8_t>(std::min<std::uint32_t>(255U,color.r)),
        static_cast<std::uint8_t>(std::min<std::uint32_t>(255U,color.g)),
        static_cast<std::uint8_t>(std::min<std::uint32_t>(255U,color.b)),255};
    for (int y=0;y<static_cast<int>(frame.pixel_height);++y)
        for (int x=0;x<static_cast<int>(frame.pixel_width);++x)
            setPixel(frame,x,y,background);
    blendImage(frame,icon,0,0,static_cast<int>(icon.pixel_width),static_cast<int>(icon.pixel_height));
}

[[nodiscard]] const structs::CRGB& enhanceDisplayColor(
    const Props& prop, const int value) noexcept {
    const auto& etc = prop.var();
    const auto magnitude = static_cast<std::uint32_t>(std::abs(value));
    const bool positive = value > 0;
    if (magnitude >= etc.enhanceMaxVal.value)
        return positive ? etc.enhanceColorMax.value : etc.penaltyColorMax.value;
    if (magnitude >= etc.enhanceHighVal.value)
        return positive ? etc.enhanceColorHigh.value : etc.penaltyColorHigh.value;
    if (magnitude >= etc.enhanceMiddleVal.value)
        return positive ? etc.enhanceColorMiddle.value : etc.penaltyColorMiddle.value;
    return positive ? etc.enhanceColorLow.value : etc.penaltyColorLow.value;
}

void appendCastCardStatus(const Props& prop, const Skin& skin,
                          const Summary* summ, const card::CastCard& card,
                          const bool debugMode, lib::ImageFrame& canvas,
                          ImageDataWithScale& cardBackground) {
    const auto scale = std::max(1U, prop.drawingScale());
    auto statusPoint = prop.looks().castLifeBarPoint();
    auto statusRows = static_cast<int>(prop.looks().statusVerMax());

    // DはカードがEditorで見える場合だけ生命力ゲージを表示する。
    // 初期FaceUpはデバッグ表示が無効でもプレビューに影響する。
    if ((debugMode || card.faceUpRound() > 0U) && card.life() > 0U &&
        card.lifeMax() > 0U) {
        auto gauge = fromSkinImage(dskin::lifeGuage2(skin, scale));
        auto mask = fromSkinImage(dskin::lifeGuage2Mask(skin, scale));
        const bool masked = gauge.valid() && mask.valid() &&
            gauge.getWidth(scale) == mask.getWidth(scale) &&
            gauge.getHeight(scale) == mask.getHeight(scale);
        if (!masked) {
            gauge = fromSkinImage(dskin::lifeGuage(skin, scale));
            // LIFEGUAGEは汎用の透過色なしで読み込む。内部の
            // 透過色の位置は(5,5)で、左上の枠・背景ではない。
            auto keyed = gauge.baseData();
            lib::apply_image_color_key(keyed, 5, 5, false);
            gauge = ImageDataWithScale(std::move(keyed), gauge.scale());
        }
        auto bar = fromSkinImage(dskin::lifeBar(skin, scale));
        if (gauge.valid()) {
            auto gaugeData = gauge.scaled(scale);
            if (!gaugeData.frames.empty()) {
                const auto& gaugeFrame = gaugeData.frames.front();
                lib::ImageFrame life;
                life.pixel_width = gaugeFrame.pixel_width;
                life.pixel_height = gaugeFrame.pixel_height;
                life.logical_size = gaugeFrame.logical_size;
                life.pixel_format = lib::PixelFormat::RGBA32;
                life.pixels.assign(static_cast<std::size_t>(life.pixel_width) *
                                       life.pixel_height * 4U, std::byte{255});
                if (masked) {
                    // 別のマスクで、動くバーも含め完成したゲージを切
                    // り抜く。HPが空の部分には下のカード枠を使う。
                    auto base = cardBackground.scaled(scale);
                    if (!base.frames.empty()) {
                        const auto& frame = base.frames.front();
                        for (int y = 0; y < static_cast<int>(life.pixel_height); ++y)
                            for (int x = 0; x < static_cast<int>(life.pixel_width); ++x)
                                setPixel(life, x, y, pixelAt(frame,
                                    statusPoint.x * static_cast<int>(scale) + x,
                                    statusPoint.y * static_cast<int>(scale) + y));
                    }
                }
                if (bar.valid()) {
                    auto barData = bar.scaled(scale);
                    if (!barData.frames.empty()) {
                        const auto& barFrame = barData.frames.front();
                        const auto ratio = std::clamp(
                            static_cast<double>(card.life()) /
                                static_cast<double>(card.lifeMax()),
                            0.0, 1.0);
                        const auto travel = static_cast<int>(gaugeFrame.pixel_width + scale);
                        const auto left = static_cast<int>(std::lround(travel * ratio)) - travel;
                        // バーは移動・切り抜きで扱う。拡大縮小すると目盛り
                        // が歪み、カードへ直接描くとマスクからはみ出す。
                        blendImage(life, barFrame, left, static_cast<int>(scale),
                                   static_cast<int>(barFrame.pixel_width),
                                   static_cast<int>(barFrame.pixel_height));
                    }
                }
                blendImage(life, gaugeFrame, 0, 0,
                           static_cast<int>(gaugeFrame.pixel_width),
                           static_cast<int>(gaugeFrame.pixel_height));
                if (masked) {
                    auto maskData = mask.scaled(scale);
                    const auto& maskFrame = maskData.frames.front();
                    for (int y = 0; y < static_cast<int>(life.pixel_height); ++y)
                        for (int x = 0; x < static_cast<int>(life.pixel_width); ++x) {
                            auto pixel = pixelAt(life, x, y);
                            pixel.a = pixelAt(maskFrame, x, y).a;
                            setPixel(life, x, y, pixel);
                        }
                } else {
                    lib::apply_image_color_key(life, 0, 0, false);
                }
                blendImage(canvas, life,
                           statusPoint.x * static_cast<int>(scale),
                           statusPoint.y * static_cast<int>(scale),
                           static_cast<int>(life.pixel_width),
                           static_cast<int>(life.pixel_height));
                statusPoint.y -= gauge.getHeight(1U) + 2;
                --statusRows;
            }
        }
    }

    statusPoint.x = prop.looks().statusX();
    statusPoint.y -= 3;
    const auto firstY = statusPoint.y;
    int row = 0;
    const auto showMode = structs::toShowStatusTime(
        prop.var().showStatusTime.value);
    const bool showSleepAndBind = showMode == structs::ShowStatusTime::Always ||
        (showMode == structs::ShowStatusTime::WithSkin && !skin.legacy());
    const bool showStatusTime=showSleepAndBind && (debugMode || card.faceUpRound()>0U);
    const auto appendStatus = [&](ImageDataWithScale image, const std::uint32_t number,
                                  const structs::CRGB* color = nullptr, const bool always=false) {
        if (!image.valid()) return;
        if (color) {
            auto data = image.scaled(scale);
            if (!data.frames.empty()) {
                paintStatusBackground(data.frames.front(), *color);
                image = ImageDataWithScale(std::move(data), scale);
            }
        }
        if (showStatusTime || always) {
            auto data=image.scaled(scale);
            const auto graphics=prop.images().services().graphics;
            if (!data.frames.empty() && graphics) {
                auto font=createFontFromPixels(prop.adjustFont(prop.looks().statusTimeFont(skin.legacy(),number)),scale>=2);
                font.points*=scale;
                const auto text=std::to_string(number);
                const auto extent=graphics->measure_text(text,font,0);
                const auto metrics=graphics->font_metrics(font);
                auto& frame=data.frames.front();
                rasterCaption(frame,graphics,text,font,{255,255,255,255},{0,0,0,255},
                    float(frame.pixel_width)-extent.width-scale,
                    float(frame.pixel_height)-metrics.ascent-2*scale);
                image=ImageDataWithScale(std::move(data),scale);
            }
        }
        const auto logicalWidth = image.getWidth(1U);
        const auto logicalHeight = image.getHeight(1U);
        appendImage(canvas, std::move(image), scale, statusPoint.x, statusPoint.y);
        ++row;
        if (row >= std::max(1, statusRows)) {
            statusPoint.x += logicalWidth + 1;
            statusPoint.y = firstY;
            row = 0;
        } else {
            statusPoint.y -= logicalHeight + 1;
        }
    };

    if (card.mentalityRound() > 0U) {
        const auto mentality = card.mentality();
        if (mentality != types::Mentality::Normal &&
            (mentality != types::Mentality::Sleep || showSleepAndBind)) {
            appendStatus(fromSkinImage(dskin::mentality(skin, scale, mentality)), card.mentalityRound());
        }
    }
    if (card.poison() > 0U)
        appendStatus(fromSkinImage(dskin::poison(skin, scale)), card.poison());
    if (card.paralyze() > 0U && showSleepAndBind)
        appendStatus(fromSkinImage(dskin::paralyze(skin, scale)), card.paralyze());
    if (card.bindRound() > 0U && showSleepAndBind)
        appendStatus(fromSkinImage(dskin::bind(skin, scale)), card.bindRound());
    if (card.silenceRound() > 0U)
        appendStatus(fromSkinImage(dskin::silence(skin, scale)), card.silenceRound());
    if (card.faceUpRound() > 0U)
        appendStatus(fromSkinImage(dskin::faceUp(skin, scale)), card.faceUpRound());
    if (card.antiMagicRound() > 0U)
        appendStatus(fromSkinImage(dskin::antiMagic(skin, scale)), card.antiMagicRound());

    for (const auto enhance : {types::Enhance::Action, types::Enhance::Avoid,
                               types::Enhance::Resist, types::Enhance::Defense}) {
        const auto value = card.enhance(enhance);
        if (card.enhanceRound(enhance) == 0U || value == 0) continue;
        const auto& color = enhanceDisplayColor(prop, value);
        appendStatus(fromSkinImage(value > 0
            ? dskin::enhanceUp(skin, scale, enhance)
            : dskin::enhanceDown(skin, scale, enhance)), card.enhanceRound(enhance), &color);
    }

    int beastCount = 0;
    const auto beastMax = static_cast<int>(prop.looks().beastCardMaxNum(card.level()));
    for (const auto& beast : card.beasts()) {
        if (!beast) continue;
        auto useLimit = beast->useLimit();
        if (summ && beast->linkId() != 0U) {
            if (const auto linked = summ->beast(beast->linkId()))
                useLimit = linked->useLimit();
            else
                continue;
        }
        if (useLimit > 0U && ++beastCount >= beastMax) break;
    }
    if (beastCount > 0)
        appendStatus(fromSkinImage(dskin::summon(skin, scale)), static_cast<std::uint32_t>(beastCount), nullptr, true);
}

[[nodiscard]] ImageDataWithScale composeCard(
    const Props* prop, const Skin* skin, const Summary* summ,
    ImageDataWithScale background,
    const lib::Array<lib::Owner<card::CardImage>>& paths,
    const structs::CInsets padding, const bool centerDefault,
    lib::Services services,
    const types::Premium premium = types::Premium::Normal,
    const card::SkillCard* skillLevel = nullptr) {
    const auto scale = std::max(1U, prop ? prop->drawingScale() : 1U);
    const auto cardSize = prop ? prop->looks().cardSize() : structs::CSize{64U, 96U};
    const auto width = std::max(1, static_cast<int>(cardSize.width + padding.e + padding.w) *
                                  static_cast<int>(scale));
    const auto height = std::max(1, static_cast<int>(cardSize.height + padding.n + padding.s) *
                                   static_cast<int>(scale));
    lib::ImageData result;
    result.format = lib::ImageFormat::Bitmap;
    lib::ImageFrame canvas;
    canvas.pixel_width = static_cast<std::uint32_t>(width);
    canvas.pixel_height = static_cast<std::uint32_t>(height);
    canvas.logical_size = {static_cast<float>(width) / scale,
                           static_cast<float>(height) / scale};
    canvas.pixel_format = lib::PixelFormat::RGBA32;
    canvas.pixels.assign(static_cast<std::size_t>(width) * height * 4U,
                         std::byte{0});

    const auto areaLeft = padding.w * static_cast<int>(scale);
    const auto areaTop = padding.n * static_cast<int>(scale);
    const auto areaWidth = std::max(1, static_cast<int>(cardSize.width) *
                                      static_cast<int>(scale));
    const auto areaHeight = std::max(1, static_cast<int>(cardSize.height) *
                                       static_cast<int>(scale));
    if (background.valid()) {
        auto data = background.scaled(scale);
        if (!data.frames.empty())
            // DのPileImageはカード背景を全サイズの基底
            // 画像に使う。下記の内側余白で制限するのは顔・素材の
            // レイヤーだけ。枠をその内側へ縮めると、95×130
            // のキャストカード背景が74×94になってしまう。
            blendImage(canvas, data.frames.front(), 0, 0, width, height);
    }
    // Dのdutils.cardImageは、希少・プレミアカードにスキンのSIGN_RARE/SIGN_PREMIER
    // を自然サイズで描く。素材レイヤーより前に、左上からlooks.premiumXY
    // の位置と、右下から対称の位置へ置く。
    if (prop && skin && premium != types::Premium::Normal) {
        auto sign = fromSkinImage(premium == types::Premium::Premium
            ? dskin::premier(*skin, scale) : dskin::rare(*skin, scale));
        if (sign.valid()) {
            auto data = sign.scaled(scale);
            if (!data.frames.empty()) {
                const auto& frame = data.frames.front();
                const int signWidth = static_cast<int>(frame.pixel_width);
                const int signHeight = static_cast<int>(frame.pixel_height);
                const auto pp = prop->looks().premiumXY();
                const int px = static_cast<int>(pp.x) * static_cast<int>(scale);
                const int py = static_cast<int>(pp.y) * static_cast<int>(scale);
                blendImage(canvas, frame, px, py, signWidth, signHeight);
                blendImage(canvas, frame, width - px - signWidth,
                           height - py - signHeight, signWidth, signHeight);
            }
        }
    }
    // DのcardImageは、プレミアの印の後、素材レイヤーの前にレベルを描く。
    if (prop && skin && skillLevel && prop->var().showSkillCardLevel.value) {
        const auto graphics = services.graphics ? services.graphics : prop->images().services().graphics;
        if (graphics) {
            auto font = createFontFromPixels(
                prop->adjustFont(prop->looks().skillCardLevelFont(skin->legacy())), true);
            font.points *= scale;
            const auto text = std::to_string(skillLevel->level());
            const auto extent = graphics->measure_text(text, font, 0);
            const auto padding = prop->looks().skillCardLevelInsets();
            const auto color = prop->looks().skillCardLevelColor();
            rasterCaption(canvas, graphics, text, font,
                {lib::Byte(color.r), lib::Byte(color.g), lib::Byte(color.b), lib::Byte(color.a)},
                {0, 0, 0, 0}, float(width - padding.e * int(scale)) - extent.width,
                float(padding.n * int(scale)));
        }
    }
    for (const auto& path : paths) {
        if (!path) continue;
        if (path->type != types::CardImageType::File) {
            auto layer=nonFileCardLayer(*path,prop,skin,summ,padding,
                centerDefault ? lib::ImageScaleMode::Center : lib::ImageScaleMode::None,
                width,height,services);
            if (layer.valid() && !layer.baseData().frames.empty())
                blendImage(canvas,layer.baseData().frames.front(),0,0,width,height);
            continue;
        }
        if (path->path().empty()) continue;
        auto material = loadImageWithScale(prop, skin, summ, path->path(), scale,
                                           true, false, 0, 0, {}, services);
        if (!material.valid()) continue;
        auto data = material.scaled(scale);
        if (data.frames.empty()) continue;
        const auto& source = data.frames.front();
        int drawWidth = static_cast<int>(source.pixel_width);
        int drawHeight = static_cast<int>(source.pixel_height);
        // カード素材は拡大しない。大きすぎる場
        // 合は縦横比を保って縮小し、カード面
        // の内側に顔全体が見えるようにする。
        if (drawWidth > areaWidth || drawHeight > areaHeight) {
            const auto ratio = std::min(static_cast<double>(areaWidth) / drawWidth,
                                        static_cast<double>(areaHeight) / drawHeight);
            drawWidth = std::max(1, static_cast<int>(std::lround(drawWidth * ratio)));
            drawHeight = std::max(1, static_cast<int>(std::lround(drawHeight * ratio)));
        }
        int x = areaLeft;
        int y = areaTop;
        if (path->positionType() == types::CardImagePosition::Center ||
            (path->positionType() == types::CardImagePosition::Default && centerDefault)) {
            x += (areaWidth - drawWidth) / 2;
            y += (areaHeight - drawHeight) / 2;
        }
        blendImage(canvas, source, x, y, drawWidth, drawHeight);
    }
    result.frames.push_back(std::move(canvas));
    return ImageDataWithScale(std::move(result), scale);
}

}  // namespace

ImageDataWithScale castCardImage(const Props* prop, const Skin* skin,
                                 const Summary* summ, const card::CastCard* card,
                                 const bool debugMode, const bool overrideName,
                                 const StringView name, const bool overrideImage,
                                 const lib::Array<lib::Owner<card::CardImage>>& images,
                                 lib::Services services) {
    if (prop && card) {
        ImageDataWithScale background;
        if (skin) {
            const auto scale = prop->drawingScale();
            if (card->life() == 0U)
                background = fromSkinImage(dskin::castCardFaint(*skin, scale));
            else if (card->paralyze() > prop->looks().stoneBorder())
                background = fromSkinImage(dskin::castCardPetrif(*skin, scale));
            else if (card->paralyze() > 0U)
                background = fromSkinImage(dskin::castCardParaly(*skin, scale));
            else if (card->bindRound() > 0U)
                background = fromSkinImage(dskin::castCardBind(*skin, scale));
            else if (card->mentality() == types::Mentality::Sleep &&
                     card->mentalityRound() > 0U)
                background = fromSkinImage(dskin::castCardSleep(*skin, scale));
            else if (card->lifeMax() > 0U &&
                     card->life() <= card->lifeMax() / 5U)
                background = fromSkinImage(dskin::castCardDanger(*skin, scale));
            else if (card->life() < card->lifeMax())
                background = fromSkinImage(dskin::castCardInjury(*skin, scale));
            else
                background = fromSkinImage(dskin::castCard(*skin, scale));
        }
        // Skin未選択でも、Dはカードサイズの透明Canvas
        // へ全顔画像レイヤーを合成する。最初の生画像だけを返す
        // と配置を失い、未解決の相対パスが白く見えてしまう。
        const auto paths = overrideImage ? images : card->paths();
        const auto captionGraphics=services.graphics ? services.graphics : prop->images().services().graphics;
        auto result = composeCard(prop, skin, summ, background, paths,
                           prop->looks().castCardInsets(), true,
                           std::move(services));
        if (skin && !result.baseData().frames.empty()) {
            appendCastCardStatus(*prop, *skin, summ, *card, debugMode,
                                 result.baseData().frames.front(), background);
            // composeCardは元画像をdrawingScaleでキャッシュしている。MainWindow
            // がCardList用にscaled(drawingScale)を要求した
            // 際は、HP・状態を加える前のキャッシュではなく、装飾後の画像を公開する。
            const auto resultScale = result.scale();
            result = ImageDataWithScale(std::move(result.baseData()), resultScale);
        }
        if(skin && !result.baseData().frames.empty() && captionGraphics) {
            const auto scale=result.scale();
        auto font=createFontFromPixels(prop->adjustFont(prop->looks().castCardNameFont(skin->legacy())),false);
            font.points*=scale;
            lib::Color foreground{0,0,0,255},outline{0,0,0,0};
            if(skin->legacy()) {
                // Dの旧形式の白い名前の状態は選択した枠に従
                // う。通常の麻痺は束縛・睡眠より優先する。
                const bool white=card->life()==0 || card->paralyze()>prop->looks().stoneBorder() ||
                    (card->paralyze()==0 && (card->bindRound()>0 ||
                     (card->mentality()==types::Mentality::Sleep && card->mentalityRound()>0)));
                if(white)foreground={255,255,255,255};
            } else {
                const auto a=prop->looks().castCardNameArea();
                const structs::CRect pixels{a.x*int(background.scale()),a.y*int(background.scale()),a.width*int(background.scale()),a.height*int(background.scale())};
                const bool dark=getRGBAverage(background.baseData(),pixels)<prop->var().negativeCardNameBorder.value;
                if(dark)foreground={255,255,255,255};
                const auto color=dark ? prop->var().cardNameBorderingColorForWhite.value : prop->var().cardNameBorderingColorForBlack.value;
                outline={static_cast<lib::Byte>(color.r),static_cast<lib::Byte>(color.g),static_cast<lib::Byte>(color.b),static_cast<lib::Byte>(color.a)};
            }
            const auto point=prop->looks().castCardNamePoint();
            const auto shrink=skin->legacy() ? 0 : std::max(0,int(result.getWidth(1U))-point.x*2);
            auto caption=lib::rasterize_image_text(captionGraphics,overrideName ? String(name) : card->name(),
                {font,foreground,outline,float(shrink)*scale,!skin->legacy()});
            auto& frame=result.baseData().frames.front();
            if(caption.pixel_width && caption.pixel_height)
                (void)lib::composite_image_pixels(frame,caption,{float(point.x)*scale-1,float(point.y)*scale-1},
                    {0,0,float(frame.pixel_width),float(frame.pixel_height)});
            // 名前の画素を、状態表示と同じ倍率キャッシュへ公開する。
            result=ImageDataWithScale(std::move(result.baseData()),scale);
        }
        return result;
    }
    (void)debugMode;
    (void)overrideName;
    (void)name;
    (void)overrideImage;
    const auto& paths = images;
    if (!paths.empty() && paths.front()) {
        const auto image = loadImageWithScale(prop, skin, summ, paths.front()->path(),
                                              prop ? prop->drawingScale() : 1U,
                                              true, false, 0, 0, {}, services);
        if (image.valid()) return image;
    }
    if (card) {
        const auto cardPaths = card->paths();
        if (!cardPaths.empty() && cardPaths.front()) {
            const auto image = loadImageWithScale(prop, skin, summ,
                                                  cardPaths.front()->path(),
                                                  prop ? prop->drawingScale() : 1U,
                                                  true, false, 0, 0, {}, services);
            if (image.valid()) return image;
        }
    }
    const auto size = prop ? prop->looks().cardSize() : structs::CSize{64U, 96U};
    return ImageDataWithScale(blankImage(static_cast<int>(size.width),
                                         static_cast<int>(size.height)),
                              prop ? prop->drawingScale() : 1U);
}

ImageDataWithScale skillCardImage(const Props* prop, const Skin* skin,
                                  const Summary* summ,
                                  const card::SkillCard& value,
                                  lib::Services services) {
    if (!prop || !skin) return {};
    // Dは外観に所有シナリオのリンク先カードを使い、枠の規則（特
    // にBeastのisOption）には元のカードを保持する。
    const auto resolved = value.linkId() && summ ? summ->skill(value.linkId())
        : lib::Owner<const card::SkillCard>{};
    const auto* appearance = value.linkId() ? resolved.get() : &value;
    const auto paths = appearance ? appearance->paths()
        : lib::Array<lib::Owner<card::CardImage>>{};
    return composeCard(prop, skin, summ,
                       fromSkinImage(dskin::skillCard(*skin, prop->drawingScale())), paths,
                       prop->looks().cardInsets(), false, std::move(services),
                       (appearance ? appearance->premium() : types::Premium::Normal), appearance);
}

ImageDataWithScale itemCardImage(const Props* prop, const Skin* skin,
                                 const Summary* summ,
                                 const card::ItemCard& value,
                                 lib::Services services) {
    if (!prop || !skin) return {};
    // Dは外観に所有シナリオのリンク先カードを使い、枠の規則（特
    // にBeastのisOption）には元のカードを保持する。
    const auto resolved = value.linkId() && summ ? summ->item(value.linkId())
        : lib::Owner<const card::ItemCard>{};
    const auto* appearance = value.linkId() ? resolved.get() : &value;
    const auto paths = appearance ? appearance->paths()
        : lib::Array<lib::Owner<card::CardImage>>{};
    return composeCard(prop, skin, summ,
                       fromSkinImage(dskin::itemCard(*skin, prop->drawingScale())), paths,
                       prop->looks().cardInsets(), false, std::move(services),
                       (appearance ? appearance->premium() : types::Premium::Normal));
}

ImageDataWithScale beastCardImage(const Props* prop, const Skin* skin,
                                  const Summary* summ,
                                  const card::BeastCard& value,
                                  lib::Services services) {
    if (!prop || !skin) return {};
    // Dは外観に所有シナリオのリンク先カードを使い、枠の規則（特
    // にBeastのisOption）には元のカードを保持する。
    const auto resolved = value.linkId() && summ ? summ->beast(value.linkId())
        : lib::Owner<const card::BeastCard>{};
    const auto* appearance = value.linkId() ? resolved.get() : &value;
    const auto paths = appearance ? appearance->paths()
        : lib::Array<lib::Owner<card::CardImage>>{};
    auto background = fromSkinImage(value.isOption()
        ? dskin::optionCard(*skin, prop->drawingScale())
        : dskin::beastCard(*skin, prop->drawingScale()));
    return composeCard(prop, skin, summ, std::move(background), paths,
                       prop->looks().cardInsets(), false, std::move(services),
                       (appearance ? appearance->premium() : types::Premium::Normal));
}

ImageDataWithScale infoCardImage(const Props* prop, const Skin* skin,
                                 const Summary* summ,
                                 const card::InfoCard& value,
                                 lib::Services services) {
    if (!prop || !skin) return {};
    return composeCard(prop, skin, summ,
                       fromSkinImage(dskin::infoCard(*skin, prop->drawingScale())), value.paths(),
                       prop->looks().cardInsets(), false, std::move(services));
}

namespace {
ImageDataWithScale cardCaptionBackground(const Props* prop, const Skin* skin,
                                         const card::Card& base) {
    const auto scale = std::max(1U, prop->drawingScale());
    if (dynamic_cast<const card::SkillCard*>(&base))
        return fromSkinImage(dskin::skillCard(*skin, scale));
    if (dynamic_cast<const card::ItemCard*>(&base))
        return fromSkinImage(dskin::itemCard(*skin, scale));
    if (const auto* beast = dynamic_cast<const card::BeastCard*>(&base))
        return fromSkinImage(beast->isOption() ? dskin::optionCard(*skin, scale)
                                               : dskin::beastCard(*skin, scale));
    if (dynamic_cast<const card::InfoCard*>(&base))
        return fromSkinImage(dskin::infoCard(*skin, scale));
    return {};
}

void paintCardName(lib::ImageFrame& canvas, const ImageDataWithScale& background,
                   const Props* prop, const Skin* skin, const String& name,
                   const unsigned scale, const int logicalWidth,
                   const std::shared_ptr<lib::GraphicsBackend>& graphics) {
    auto font=createFontFromPixels(prop->adjustFont(prop->looks().cardNameFont(skin->legacy())),false);font.points*=scale;
    lib::Color foreground{0,0,0,255},outline{0,0,0,0};
    if(!skin->legacy()){
        const auto area=prop->looks().cardNameArea();
        const structs::CRect pixels{area.x*int(background.scale()),area.y*int(background.scale()),area.width*int(background.scale()),area.height*int(background.scale())};
        const bool dark=getRGBAverage(background.baseData(),pixels)<prop->var().negativeCardNameBorder.value;
        if(dark)foreground={255,255,255,255};
        const auto c=dark?prop->var().cardNameBorderingColorForWhite.value:prop->var().cardNameBorderingColorForBlack.value;
        outline={lib::Byte(c.r),lib::Byte(c.g),lib::Byte(c.b),lib::Byte(c.a)};
    }
    const auto point=prop->looks().cardNamePoint();
    rasterCaption(canvas,graphics,name,font,foreground,outline,
        float(point.x*int(scale)),float(point.y*int(scale)),
        skin->legacy()?0:float(std::max(0,logicalWidth-point.x*2))*scale,!skin->legacy());
}
} // namespace

bool addCardName(ImageDataWithScale& image, const Props* prop, const Skin* skin,
                 const card::Card& base, lib::Services services) {
    if (!prop || !skin || !image.valid() || image.baseData().frames.empty()) return false;
    // リンクカード表示では、マスクと名前の独立した所有関係を維持する。
    if (const auto* card = dynamic_cast<const card::SkillCard*>(&base); card && card->linkId()) return false;
    if (const auto* card = dynamic_cast<const card::ItemCard*>(&base); card && card->linkId()) return false;
    if (const auto* card = dynamic_cast<const card::BeastCard*>(&base); card && card->linkId()) return false;
    if (!services.graphics) services.graphics = prop->images().services().graphics;
    if (!services.graphics) return false;
    const auto background = cardCaptionBackground(prop, skin, base);
    if (!background.valid()) return false;
    const auto scale = image.scale();
    paintCardName(image.baseData().frames.front(), background, prop, skin,
                  base.name(), scale, image.getWidth(1U), services.graphics);
    // 完全な名前を公開した後、名前描画前の倍率別キャッシュ画像を捨てる。
    image = ImageDataWithScale(std::move(image.baseData()), scale);
    return true;
}

ImageDataWithScale renderCardImage(const Props* prop, const Skin* skin,
    const Summary* summ, const card::Card& base, const card::Card* resolved,
    const card::CastCard* owner, const bool linked, const bool detail,
    lib::Services services) {
    if (!prop || !skin) return {};
    if (!services.graphics) services.graphics=prop->images().services().graphics;
    const auto graphics=services.graphics;
    const auto scale=std::max(1U,prop->drawingScale());
    auto background = cardCaptionBackground(prop, skin, base);
    const auto* skill=dynamic_cast<const card::SkillCard*>(&base);
    const auto* item=dynamic_cast<const card::ItemCard*>(&base);
    const auto* beast=dynamic_cast<const card::BeastCard*>(&base);
    if (!skill && !item && !beast && !dynamic_cast<const card::InfoCard*>(&base)) return {};
    const auto* effect=dynamic_cast<const card::EffectCard*>(resolved);
    const auto paths=resolved?resolved->paths():lib::Array<lib::Owner<card::CardImage>>{};
    auto result=composeCard(prop,skin,summ,background,paths,prop->looks().cardInsets(),false,services,
        effect?effect->premium():types::Premium::Normal,
        dynamic_cast<const card::SkillCard*>(resolved));
    if(result.baseData().frames.empty())return result;
    auto& canvas=result.baseData().frames.front();
    const auto stamp=[&](ImageDataWithScale icon,const int x,const int y,bool centerMask=false){
        if(centerMask&&!icon.baseData().frames.empty()){
            auto data=icon.baseData();auto& frame=data.frames.front();
            lib::apply_image_color_key(data,int(frame.pixel_width/2),int(frame.pixel_height/2),false);
            icon=ImageDataWithScale(std::move(data),icon.scale());
        }
        appendImage(canvas,std::move(icon),scale,x,y);
    };
    if(linked){
        const auto color=prop->var().linkCardMaskColor.value;
        auto mask=blankImage(int(canvas.pixel_width),int(canvas.pixel_height));
        auto& frame=mask.frames.front();
        for(int y=0;y<int(frame.pixel_height);++y)for(int x=0;x<int(frame.pixel_width);++x)
            setPixel(frame,x,y,{lib::Byte(color.r),lib::Byte(color.g),lib::Byte(color.b),lib::Byte(color.a)});
        blendImage(canvas,frame,0,0,int(frame.pixel_width),int(frame.pixel_height));
    }
    if(effect && prop->sys().isPenalty(effect->keyCodes()))
        stamp(fromSkinImage(dskin::cardPenalty(*skin,scale)),0,0,true);
    if((skill&&skill->hold())||(item&&item->hold()))
        stamp(fromSkinImage(dskin::cardHold(*skin,scale)),0,0,true);
    if(detail&&owner&&effect){
        const auto aptitude=owner->aptitude(effect->physical(),effect->mental());
        auto icon=fromSkinImage(aptitude>=prop->looks().aptVeryHigh()?dskin::aptVeryHigh(*skin,scale):
            aptitude>=prop->looks().aptHigh()?dskin::aptHigh(*skin,scale):
            aptitude>=prop->looks().aptNormal()?dskin::aptNormal(*skin,scale):dskin::aptLow(*skin,scale));
        const auto point=prop->looks().aptStoneXY();
        const auto x=result.getWidth(1U)-point.x-icon.getWidth(1U);
        stamp(std::move(icon),x,point.y);
        if(skill){
            auto use=fromSkinImage(dskin::use4(*skin,scale));const auto p=prop->looks().useStoneXY();
            const auto ux=result.getWidth(1U)-p.x-use.getWidth(1U);stamp(std::move(use),ux,p.y);
        }
    }
    if(graphics){
        paintCardName(canvas, background, prop, skin,
                      resolved ? resolved->name() : String{}, scale,
                      result.getWidth(1U), graphics);
        std::optional<std::uint32_t> count;
        if(const auto* value=dynamic_cast<const card::ItemCard*>(resolved))count=value->useLimit();
        else if(const auto* value=dynamic_cast<const card::BeastCard*>(resolved))count=value->useLimit();
        const bool recycle=effect&&prop->sys().isRecycle(effect->keyCodes());
        if(count&&(*count>0||recycle)){
            auto numberFont=createFontFromPixels(prop->adjustFont(prop->looks().useCountFont(skin->legacy())),scale>=2);numberFont.points*=scale;
            auto c=prop->looks().recycleNumColor();
            const lib::Color color=recycle?lib::Color{lib::Byte(c.r),lib::Byte(c.g),lib::Byte(c.b),lib::Byte(c.a)}:lib::Color{255,255,255,255};
            const auto p=prop->looks().useCountPoint();
            rasterCaption(canvas,graphics,std::to_string(*count),numberFont,color,{0,0,0,255},float(p.x*int(scale)),float(p.y*int(scale)));
        }
    }
    // composeCardの未装飾キャッシュではなく、装飾後の画像を公開する。
    return ImageDataWithScale(std::move(result.baseData()),scale);
}

void addToPileImage(const card::CardImage& image, PileImage& pile,
                    const Props* prop, const Skin* skin, const Summary* summ,
                    const structs::CInsets& materialPadding,
                    const lib::ImageScaleMode mode, lib::Services services) {
    if (image.type != types::CardImageType::File) {
        const auto scale=std::max(1U,prop ? prop->drawingScale() : 1U);
        const auto size=prop ? prop->looks().cardSize() : structs::CSize{64,96};
        const auto width=pile.base.valid() ? pile.base.getWidth(scale) : int(size.width+materialPadding.w+materialPadding.e)*int(scale);
        const auto height=pile.base.valid() ? pile.base.getHeight(scale) : int(size.height+materialPadding.n+materialPadding.s)*int(scale);
        auto layer=nonFileCardLayer(image,prop,skin,summ,materialPadding,mode,width,height,services);
        if(layer.valid())pile.append(std::move(layer));
        return;
    }
    // 通常のファイルレイヤー読み込みは維持し、欠落した型付き経路だけを変える。
    const auto value = loadImageWithScale(prop, skin, summ, image.path(),
                                          prop ? prop->drawingScale() : 1U,
                                          true, false, 0, 0, {}, services);
    if (value.valid()) pile.append(value);
}

std::uint8_t getRGBAverage(const lib::ImageData& image,
                           const structs::CRect area) {
    if (image.frames.empty() || area.width <= 0 || area.height <= 0) return 255U;
    const auto& frame = image.frames.front();
    const auto bpp = bytesPerPixel(frame.pixel_format);
    if (bpp == 0U || area.x < 0 || area.y < 0 ||
        area.x + area.width > static_cast<int>(frame.pixel_width) ||
        area.y + area.height > static_cast<int>(frame.pixel_height)) return 255U;
    std::uint64_t sum = 0U;
    std::uint64_t count = 0U;
    for (int y = area.y; y < area.y + area.height; ++y) {
        for (int x = area.x; x < area.x + area.width; ++x) {
            const auto offset = (static_cast<std::size_t>(y) * frame.pixel_width + x) * bpp;
            const auto get = [&](std::size_t index) {
                return static_cast<std::uint64_t>(std::to_integer<unsigned int>(frame.pixels[offset + index]));
            };
            if (frame.pixel_format == lib::PixelFormat::Gray8) {
                sum += get(0U) * 3U;
            } else if (frame.pixel_format == lib::PixelFormat::BGR24 ||
                       frame.pixel_format == lib::PixelFormat::BGRA32) {
                sum += get(0U) + get(1U) + get(2U);
            } else {
                sum += get(0U) + get(1U) + get(2U);
            }
            count += 3U;
        }
    }
    return count == 0U ? 255U : static_cast<std::uint8_t>(sum / count);
}

void putHand(const std::shared_ptr<lib::Canvas>& canvas, lib::DrawingContext& gc,
             const Props* prop, const bool detail, const card::CastCard* card,
             const lib::Rect bounds) {
    (void)canvas;
    (void)prop;
    if (!detail || !card) return;
    gc.draw_rect({bounds.x + 2.0F, bounds.y + 2.0F, 12.0F, 12.0F},
                 {224, 224, 224, 220}, 1.0F, lib::StrokeStyle::Solid);
}

lib::Rect handMarkRect(const Props* prop, const bool showMark, const int left,
                       const int top, const card::CastCard* card, const bool detail) {
    if (!showMark || !detail || !prop || !card) return {};
    return {static_cast<float>(left + 2), static_cast<float>(top + 2), 12.0F, 12.0F};
}

std::vector<String> castCoupons(const EditorContext& context, const bool talker,
                                const StringView legacyName, const bool getLose,
                                const bool forValued) {
    std::vector<String> result;
    const auto* prop = context.prop;
    const auto* skin = context.skin;
    if (!skin) return legacyName.empty() ? result : std::vector<String>{String(legacyName)};
    const auto append = [&result](String value) {
        // Dは指定順、重複項目、空の区切りを維持する。選択部が除
        // 外するのは、後から加える使用済みクーポンの部分だけ。
        result.push_back(std::move(value));
    };
    if (!talker && prop) {
        if (!forValued) {
            for (const auto& coupon : prop->var().standardCoupons.value)
                append(skin->replaceSystemCouponName(prop->sys(), coupon));
        }
        if (prop->isTargetVersion(context.summary, "2")) {
            if (!getLose) {
                append(prop->sys().userCoupon());
                append(prop->sys().eventTargetCoupon());
            }
            append(prop->sys().effectTargetCoupon());
            if (!getLose) append(prop->sys().effectOutOfTargetCoupon());
        }
    }
    for (const auto value : skin->allSexes()) append(skin->sexCoupon(value));
    for (const auto value : skin->allPeriods()) append(skin->periodCoupon(value));
    const auto natures = prop && prop->var().showSpNature.value
                             ? skin->allNatures()
                             : skin->normalNatures();
    for (const auto value : natures) append(skin->natureCoupon(value));
    for (const auto value : skin->leftMakings()) {
        append(skin->makingsCoupon(value));
        append(skin->makingsCoupon(skin->reverseMakings(value)));
    }
    if (!talker && forValued && prop) {
        for (const auto& coupon : prop->var().standardCouponsForValued.value)
            append(skin->replaceSystemCouponName(prop->sys(), coupon));
    }
    return result;
}

// Dのdutils.qMaterialCopyに合わせ、PathIdごとに一度確認・コピーしてから全
// 利用者を書き換える。参照関係を扱うこの経路は、従来の生ファイルシステム用オーバーロードと分ける。
bool qGraphMaterialCopy(const EditorContext& context,
                        const usecounter::CounterOwner& counter,
                        const StringView toPath, const StringView fromPath,
                        bool& copied, const bool toIsLegacy,
                        std::function<String(const usecounter::PathUser&)> exportedImageName,
                        std::function<bool()> current) {
    copied = false;
    if (!counter || !context.prop || (current && !current())) return false;
    const auto dialogs = context.services.dialogs;
    const auto target = filesystemPath(toPath), source = filesystemPath(fromPath);
    const String material = context.skin ? context.skin->materialPath() : String{"Material"};
    const auto& messages = context.prop->requiredMsgs();
    const String questionTitle = messages.dlgTitQuestion.value;
    const String warningTitle = messages.dlgTitWarning.value;
    const String copyError = messages.dlgMsgCopyError.value;
    const String binaryQuestion = toIsLegacy ? messages.dlgMsgCopyMaterial1.value
                                            : messages.dlgMsgExcludeImage.value;
    const String oneQuestion = messages.dlgMsgCopyMaterial2.value;
    const String manyQuestion = messages.dlgMsgCopyMaterial3.value;
    const bool askBinary = !toIsLegacy || context.prop->var().saveInnerImagePath.value;
    std::vector<usecounter::PathId> paths;
    std::vector<String> sourceNames;
    std::size_t binaryCount{};
    for (const auto& key : counter->keys<usecounter::PathId>()) {
        const String value = key;
        std::error_code error;
        if (!key.isBinData() && !std::filesystem::exists(source / filesystemPath(value), error)) continue;
        paths.push_back(key);
        if (utils::isBinImg(value)) ++binaryCount;
        else sourceNames.push_back(pathString(source / filesystemPath(value)));
    }
    if (paths.empty()) return true;
    bool cancelled{};
    const auto question = [&](const String& message) {
        if (!dialogs || (current && !current())) { cancelled = true; return false; }
        lib::MessageDialogRequest request;
        request.title = questionTitle;
        request.message = message;
        request.severity = lib::MessageSeverity::Question;
        request.buttons = lib::MessageButtons::YesNoCancel;
        const auto answer = dialogs->message(request);
        if ((current && !current()) || answer == lib::MessageResult::Cancel ||
            answer == lib::MessageResult::None) { cancelled = true; return false; }
        return answer == lib::MessageResult::Yes;
    };
    const bool externalize = binaryCount && askBinary ? question(binaryQuestion) : false;
    if (cancelled) return false;
    const bool copyReferences = !sourceNames.empty()
        ? question(sourceNames.size() == 1U ? utils::tryFormat(oneQuestion, sourceNames.front())
                                           : utils::tryFormat(manyQuestion, sourceNames.size()))
        : false;
    if (cancelled || (current && !current())) return false;
    bool error{};
    for (const auto& key : paths) {
        const String oldPath = key;
        const bool binary = utils::isBinImg(oldPath);
        if ((binary && !externalize) || (!binary && !copyReferences)) continue;
        try {
            String exported;
            if (externalize && exportedImageName) {
                auto users = counter->values<usecounter::PathId>(key);
                std::sort(users.begin(), users.end(), [](const auto& a, const auto& b) {
                    return path::cpcmp(path::cpsplit(a->cwxPath(true)), path::cpsplit(b->cwxPath(true))) < 0;
                });
                if (!users.empty()) exported = exportedImageName(*users.front());
            }
            const auto from = binary ? utils::Path(oldPath) : source / filesystemPath(oldPath);
            const auto next = utils::copyTo(target, from, filesystemPath(material), externalize, exported);
            const auto value = pathString(next);
            if (value.empty()) { error = true; continue; }
            if (!utils::isBinImg(value) && value != oldPath)
                counter->change(key, usecounter::toPathId(value));
            copied = true;
        } catch (...) { error = true; } // Dは個別ファイルの失敗後も継続する。
    }
    if (!toIsLegacy)
        for (const auto& key : counter->keys<usecounter::PathId>())
            if (key.isBinData()) counter->change(key, usecounter::toPathId(""), true);
    if (error && dialogs && (!current || current())) {
        lib::MessageDialogRequest request;
        request.title = warningTitle;
        request.message = copyError;
        request.severity = lib::MessageSeverity::Warning;
        request.buttons = lib::MessageButtons::OK;
        dialogs->message(request);
    }
    return !current || current();
}

bool qMaterialCopy(const EditorContext& context, const StringView toPath,
                   const StringView fromPath, bool& copied, const bool toIsLegacy,
                   std::function<String(const PathUser&)> exportedImageName) {
    (void)context;
    (void)toIsLegacy;
    (void)exportedImageName;
    copied = false;
    std::error_code error;
    const auto source = filesystemPath(fromPath);
    const auto target = filesystemPath(toPath);
    if (!std::filesystem::exists(source, error)) return true;
    if (std::filesystem::is_directory(source, error)) {
        std::filesystem::create_directories(target, error);
        std::filesystem::copy(source, target,
                              std::filesystem::copy_options::recursive |
                                  std::filesystem::copy_options::overwrite_existing,
                              error);
    } else {
        std::filesystem::create_directories(target.parent_path(), error);
        std::filesystem::copy_file(source, target,
                                   std::filesystem::copy_options::overwrite_existing,
                                   error);
    }
    copied = !error;
    return !error;
}

void intoDisplay(int& x, int& y, const int width, const int height,
                 const lib::Rect display) {
    if (display.x + display.width < static_cast<float>(x + width))
        x = static_cast<int>(display.x + display.width - width);
    if (display.y + display.height < static_cast<float>(y + height))
        y = static_cast<int>(display.y + display.height - height);
    x = std::max(x, static_cast<int>(display.x));
    y = std::max(y, static_cast<int>(display.y));
}

lib::ImageData skeletonImage(const lib::ImageData& source, const bool mask) {
    auto result = source;
    // Dは左上の透過マスクが未指定でも、常にアルファ127で描く。共通
    // の非乗算アルファ画素を維持し、ネイティブの新規ビットマップのRGB
    // 背景は未規定なので、白や黒へ合成する処理を勝手に加えない。
    lib::apply_image_opacity(result, 127U);
    if (mask) lib::apply_image_color_key(result, 0, 0, false);
    return result;
}

std::shared_ptr<lib::GroupBox> createSuccessRateScale(
    const Props* prop, ContainerPtr parent, std::shared_ptr<lib::Slider>& successRate) {
    auto group = std::make_shared<lib::GroupBox>();
    group->title = prop ? "Success rate" : "Success rate";
    successRate = std::make_shared<lib::Slider>();
    successRate->minimum = 0.0;
    successRate->maximum = 100.0;
    successRate->value = 50.0;
    group->add(successRate);
    if (parent) parent->add(group);
    return group;
}

void forceFocus(const WidgetPtr& widget, const bool shellActivate,
                const lib::Services& services) {
    if (!widget || !services.gui) return;
    if (const auto shell = std::dynamic_pointer_cast<lib::Window>(widget)) {
        if (shellActivate) services.gui->restore_window(shell);
        return;
    }

    if (services.gui->focused_widget() == widget) {
        g_focus = widget;
        return;
    }
    const auto owner = services.gui->window_owner(widget);
    if (owner) {
        std::unordered_set<const lib::Widget*> active_path;
        (void)selectContainingTabPath(owner, widget, active_path);
        // タブ選択はアプリケーションへ通知し、対象を終了・再接続し得
        // る。同じWindowが引き続き所有する場合だけ続行する。
        if (services.gui->window_owner(widget) != owner) return;
    }
    if (!lib::accepts_keyboard_focus(widget)) return;
    if (services.gui->focus_control(widget, shellActivate)) g_focus = widget;
}

void forceFocus(const std::shared_ptr<lib::TableView>& table,
                const std::size_t row, const bool shellActivate,
                const lib::Services& services) {
    if (!table || row >= table->rows.size()) return;
    const auto owner = services.gui ? services.gui->window_owner(table) : nullptr;
    const auto row_order_revision = table->row_order_revision;
    forceFocus(std::static_pointer_cast<lib::Widget>(table), shellActivate, services);
    // フォーカス操作はホストのコールバックを実行し得る。そこで切
    // り離し・置換・並べ替えされた表示へ、古い行を適用しない。
    if (row_order_revision != table->row_order_revision ||
        row >= table->rows.size() ||
        (owner && services.gui->window_owner(table) != owner)) return;
    (void)lib::select_table_rows(*table, {row}, false, false);
    (void)lib::reveal_table_row(*table, row);
    // DのshowSelectionは、スクロール不要でも選択行を更新する。
    if (services.gui) services.gui->request_redraw(table);
}

void forceFocus(const std::shared_ptr<lib::TreeView>& tree,
                const lib::TreeItemId item, const bool shellActivate,
                const lib::Services& services) {
    if (!tree || std::ranges::none_of(tree->nodes, [item](const auto& node) {
            return node.id == item;
        })) return;
    const auto owner = services.gui ? services.gui->window_owner(tree) : nullptr;
    forceFocus(std::static_pointer_cast<lib::Widget>(tree), shellActivate, services);
    // Dの親の表示を先にする順序に合わせつつ、再入したフォーカスコー
    // ルバックが対象項目や所有Windowを削除する場合を保護する。
    if (std::ranges::none_of(tree->nodes, [item](const auto& node) {
            return node.id == item;
        }) || (owner && services.gui->window_owner(tree) != owner)) return;
    (void)lib::select_tree_items(*tree, {item}, false, false);
    // DのshowSelectionは、行がすでに表
    // 示領域内でも祖先を展開し、選択項目を更新する。
    (void)lib::reveal_tree_item_with_ancestors(*tree, item);
    if (services.gui) services.gui->request_redraw(tree);
}

String writeRec(const WidgetPtr& control, const StringView indent) {
    if (!control) return {};
    std::ostringstream result;
    result << indent << static_cast<int>(control->kind()) << "\n";
    if (const auto container = std::dynamic_pointer_cast<lib::Container>(control)) {
        for (const auto& child : container->children()) result << writeRec(child, String(indent) + "  ");
    }
    return result.str();
}

std::shared_ptr<splitpane::SplitPane> changeVHSide(
    const std::shared_ptr<splitpane::SplitPane>& sash) {
    if (!sash) return {};
    const auto oldStyle = sash->getStyle();
    const auto newStyle = (oldStyle & splitpane::Vertical) ? splitpane::Horizontal : splitpane::Vertical;
    auto result = std::make_shared<splitpane::SplitPane>(newStyle);
    result->resizeControl1(sash->resizeControl1());
    result->canMinimized1(sash->canMinimized1());
    result->canMinimized2(sash->canMinimized2());
    result->setWeights(sash->getWeights());
    for (const auto& child : sash->children()) result->add(child);
    return result;
}

void drawWallpaper(lib::DrawingContext& gc, const ResourceId image,
                   const lib::Rect rect, const structs::WallpaperStyle style,
                   const lib::Size imageSize) {
    if (image == 0U || rect.width <= 0.0F || rect.height <= 0.0F) return;
    const auto width = imageSize.width > 0.0F ? imageSize.width : rect.width;
    const auto height = imageSize.height > 0.0F ? imageSize.height : rect.height;
    if (style == structs::WallpaperStyle::Tile) {
        for (float y = rect.y; y < rect.y + rect.height; y += height) {
            for (float x = rect.x; x < rect.x + rect.width; x += width) {
                const auto tileWidth = std::min(width, rect.x + rect.width - x);
                const auto tileHeight = std::min(height, rect.y + rect.height - y);
                // Dは最後の部分的なタイルでは、実際に見える入力画素だけを描く。
                gc.draw_image(image, {0.0F, 0.0F, tileWidth, tileHeight},
                              {x, y, tileWidth, tileHeight},
                              lib::Interpolation::Nearest);
            }
        }
        return;
    }
    if (style == structs::WallpaperStyle::Center) {
        gc.draw_image(image, {0.0F, 0.0F, width, height},
                      {rect.x + (rect.width - width) / 2.0F,
                       rect.y + (rect.height - height) / 2.0F, width, height},
                      lib::Interpolation::Linear);
        return;
    }
    const auto scaleX = rect.width / width;
    const auto scaleY = rect.height / height;
    const auto scale = style == structs::WallpaperStyle::ExpandFull
                           ? std::max(scaleX, scaleY)
                           : std::min(scaleX, scaleY);
    const auto drawW = width * scale;
    const auto drawH = height * scale;
    gc.draw_image(image, {0.0F, 0.0F, width, height},
                  {rect.x + (rect.width - drawW) / 2.0F,
                   rect.y + (rect.height - drawH) / 2.0F, drawW, drawH},
                  lib::Interpolation::Linear);
}

void initTree(EditorContext* context, const TreePtr& tree, const bool eventTree,
              const bool hideRootLine, std::function<bool()> classicStyleTree) {
    if (!tree) return;
    tree->checkable = true;
    if (eventTree) {
        treeExpandedAll(tree);
        tree->on_expansion_changed = [context](lib::TreeItemId, bool) {
            if (context && context->refreshToolbar) context->refreshToolbar();
        };
    } else if (hideRootLine) {
        treeUnexpandedAll(tree);
    }
    (void)classicStyleTree;
}

void initSpinner(const NumberEditPtr& spinner) {
    if (!spinner) return;
    if (spinner->read_only) spinner->set_enabled(false);
    // Dは旧Wineの表示不具合対策で選択を非同期再試行する。共通
    // NumberEditは現在値を直接描くため、再試行は不要。
}

String wrapReturnCode(String value) {
    return lib::normalize_editor_control_newlines(std::move(value));
}

int dpiMuls() {
    // モデルの寸法は常に論理画素。デバイス・出力倍率は共通の表示
    // 側が管理し、ダイアログ側でOSのDPI係数を適用しない。
    return 1;
}
int ppis(const int value) { return value * dpiMuls(); }

GridLayoutSpec zeroGridLayout(const int columns, const bool equalWidth) {
    GridLayoutSpec result;
    result.columns = std::max(1, columns);
    result.equalWidth = equalWidth;
    return result;
}

GridLayoutSpec zeroMarginGridLayout(const int columns, const bool equalWidth) {
    auto result = normalGridLayout(columns, equalWidth);
    result.marginWidth = 0;
    result.marginHeight = 0;
    return result;
}

GridLayoutSpec normalGridLayout(const int columns, const bool equalWidth) {
    auto result = zeroGridLayout(columns, equalWidth);
    result.horizontalSpacing = ppis(5);
    result.verticalSpacing = ppis(5);
    result.marginWidth = ppis(5);
    result.marginHeight = ppis(5);
    return result;
}

GridLayoutSpec windowGridLayout(const int columns, const bool equalWidth) {
    auto result = zeroGridLayout(columns, equalWidth);
    result.horizontalSpacing = ppis(2);
    result.verticalSpacing = ppis(2);
    result.marginWidth = ppis(2);
    result.marginHeight = ppis(2);
    return result;
}

void setGridMinW(const WidgetPtr& control, const int minimumWidth, const int gridStyle) {
    (void)gridStyle;
    if (!control) return;
    auto bounds = control->bounds();
    bounds.width = std::max(bounds.width, static_cast<float>(minimumWidth));
    control->set_bounds(bounds);
}

ContainerPtr centerGroup(const ContainerPtr& parent, const StringView text,
                         const bool fillH, const bool fillV) {
    auto group = std::make_shared<lib::GroupBox>();
    group->title = String(text);
    // Dは返すCompositeを余白0で中央配置する。FillLayout
    // でグループを引き伸ばすと、縦方向の中央配置の契約が失われる。
    auto centered = std::make_shared<centerlayout::CenterLayout>(
        centerlayout::Both, 0);
    centered->fillHorizontal = fillH;
    centered->fillVertical = fillV;
    group->set_layout(centered);
    auto content = std::make_shared<lib::Panel>();
    group->add(content);
    if (parent) parent->add(group);
    return content;
}

bool playBGMCW(const Props* prop, const StringView path,
               const std::uint32_t fadeIn, const std::uint32_t volume,
               const std::uint32_t loopCount, const bool legacy,
               lib::Services services) {
    namespace sound = ::cwxeditor::cwx::editor::gui::sound;
    const auto bound = sound::services();
    // MainWindowがこの接続を一度設定する。古いバックエンド
    // や独立に渡されたバックエンドに、二つ目の再生経路を作らない。
    if ((services.audio && services.audio != bound.audio) ||
        (services.shell && services.shell != bound.shell)) return false;
    const auto selected_type = prop ? prop->var().soundPlayType.value
                                    : sound::SOUND_TYPE_AUTO;
    const auto global_volume = prop ? prop->var().bgmVolume.value : 100;
    return sound::playMaterialPreviewWithDwtPolicy(
        lib::AudioBus::Bgm, path, fadeIn, loopCount, selected_type,
        selected_type, legacy, global_volume, volume);
}

void playSECW(const Props* prop, const StringView path,
              const std::uint32_t fadeIn, const std::uint32_t volume,
              const std::uint32_t loopCount, const bool legacy,
              lib::Services services) {
    namespace sound = ::cwxeditor::cwx::editor::gui::sound;
    const auto bound = sound::services();
    if ((services.audio && services.audio != bound.audio) ||
        (services.shell && services.shell != bound.shell)) return;
    const auto requested_type = prop ? prop->var().soundEffectPlayType.value
                                     : sound::SOUND_TYPE_AUTO;
    const auto saved_bgm_type = prop ? prop->var().soundPlayType.value
                                     : sound::SOUND_TYPE_AUTO;
    const auto global_volume = prop ? prop->var().seVolume.value : 100;
    (void)sound::playMaterialPreviewWithDwtPolicy(
        lib::AudioBus::SoundEffect, path, fadeIn, loopCount, requested_type,
        saved_bgm_type, legacy, global_volume, volume);
}

int textWidth(const Props* prop, const WidgetPtr& control, const StringView text,
              lib::Services services) {
    (void)control;
    lib::FontSpec font;
    if (prop) font = fontSpecFromDwtData(prop->looks().monospace(), 10);
    if (services.graphics) {
        // DWTは画素ではなく等幅文字セル数の幅を返す。GC.textExtent(text).x
        // を空白のGC.textExtentの幅で割る。
        // 同じ契約を維持し、日本語などの複数バイト文字も
        // UTF-8バイト数ではなく描画側で計測する。
        const auto measured = services.graphics->measure_text(text, font, 0.0F);
        const auto space = services.graphics->measure_text(" ", font, 0.0F);
        if (space.width > 0.0F && measured.width > 0.0F)
            return std::max(0, static_cast<int>(measured.width / space.width));
    }
    return static_cast<int>(utf8CodePointCount(text));
}

std::unordered_map<WindowPtr, lib::CursorKind> setWaitCursors(const WindowPtr& shell) {
    std::unordered_map<WindowPtr, lib::CursorKind> result;
    if (!shell) return result;
    if (shell->cursor() == lib::CursorKind::Wait) return result;
    result.emplace(shell, shell->cursor());
    shell->set_cursor(lib::CursorKind::Wait);
    // Dは全子ShellにもCURSOR_WAITを設定する。ホス
    // トのカーソルはプロセス全体で一つなので、共通の待機要求で全
    // 部を覆い、同期処理がUIスレッドを塞ぐ前に直ちに反映する。
    lib::begin_wait_cursor();
    return result;
}

void resetCursors(const std::unordered_map<WindowPtr, lib::CursorKind>& cursors) {
    WindowPtr root;
    for (const auto& [window, unused] : cursors) {
        (void)unused;
        if (window && window->owner.expired()) {
            root = window;
            break;
        }
    }
    if (!root) {
        for (const auto& [window, unused] : cursors) {
            (void)unused;
            if (window) {
                root = window;
                break;
            }
        }
    }
    resetCursors(cursors, root);
}

void resetCursors(const std::unordered_map<WindowPtr, lib::CursorKind>& cursors,
                  const WindowPtr& realizerTarget) {
    for (const auto& [window, cursor] : cursors) {
        if (window) window->set_cursor(cursor);
    }
    // setWaitCursorsの各呼び出しは、Dの保存マップに複数の子Shell
    // があっても一つの共通待機要求を追加する。復元カーソルは指定Shell
    // から一つ選び、unordered_mapの巡回順で子を選ばない。
    if (cursors.empty()) return;
    auto target = cursors.find(realizerTarget);
    if (target == cursors.end() || !target->first) {
        target = cursors.end();
        for (auto it = cursors.begin(); it != cursors.end(); ++it) {
            if (it->first && it->first->owner.expired()) {
                target = it;
                break;
            }
        }
    }
    if (target == cursors.end()) {
        for (auto it = cursors.begin(); it != cursors.end(); ++it) {
            if (it->first) {
                target = it;
                break;
            }
        }
    }
    if (target != cursors.end()) lib::end_wait_cursor(target->second);
    else lib::end_wait_cursor();
}

lib::Color alphaColor(const lib::Color& foreground, const lib::Color& background,
                      int alpha) {
    alpha = std::clamp(alpha, 0, 255);
    const auto blend = [alpha](const std::uint8_t first, const std::uint8_t second) {
        return static_cast<std::uint8_t>((static_cast<int>(first) * alpha +
                                          static_cast<int>(second) * (255 - alpha)) /
                                         255);
    };
    return {blend(foreground.red, background.red),
            blend(foreground.green, background.green),
            blend(foreground.blue, background.blue), 255};
}

structs::CRGB alphaColor(const structs::CRGB& foreground,
                         const structs::CRGB& background, int alpha) {
    const lib::Color result = alphaColor(
        lib::Color{static_cast<std::uint8_t>(foreground.r),
                   static_cast<std::uint8_t>(foreground.g),
                   static_cast<std::uint8_t>(foreground.b),
                   static_cast<std::uint8_t>(foreground.a)},
        lib::Color{static_cast<std::uint8_t>(background.r),
                   static_cast<std::uint8_t>(background.g),
                   static_cast<std::uint8_t>(background.b),
                   static_cast<std::uint8_t>(background.a)}, alpha);
    return {result.red, result.green, result.blue, result.alpha};
}

String objNameFrom(const Props* prop, const CWXPath* object) {
    if (!object) return {};
    // DはこれらのラベルをMsgsから取得する。英語の型
    // 名を返すと、選択した日本語・独自言語を失い、ツリー
    // や状態欄がEditorの他の表示と不一致になる。
    if (prop) {
        const auto& msgs = prop->requiredMsgs();
        if (dynamic_cast<const ::cwxeditor::cwx::area::Area*>(object)) return msgs.area.value;
        if (dynamic_cast<const ::cwxeditor::cwx::area::Battle*>(object)) return msgs.battle.value;
        if (dynamic_cast<const ::cwxeditor::cwx::area::Package*>(object)) return msgs.cwPackage.value;
        if (dynamic_cast<const card::CastCard*>(object)) return msgs.cwCast.value;
        if (dynamic_cast<const card::SkillCard*>(object)) return msgs.skill.value;
        if (dynamic_cast<const card::ItemCard*>(object)) return msgs.item.value;
        if (dynamic_cast<const card::BeastCard*>(object)) return msgs.beast.value;
        if (dynamic_cast<const card::InfoCard*>(object)) return msgs.info.value;
        if (dynamic_cast<const flag::Flag*>(object)) return msgs.flag.value;
        if (dynamic_cast<const flag::Step*>(object)) return msgs.step.value;
        if (dynamic_cast<const flag::Variant*>(object)) return msgs.variant.value;
    }
    if (dynamic_cast<const card::CastCard*>(object)) return "Cast";
    if (dynamic_cast<const card::SkillCard*>(object)) return "Skill";
    if (dynamic_cast<const card::ItemCard*>(object)) return "Item";
    if (dynamic_cast<const card::BeastCard*>(object)) return "Beast";
    if (dynamic_cast<const card::InfoCard*>(object)) return "Info";
    if (dynamic_cast<const Content*>(object)) return "Content";
    return "Object";
}

String rangeName(const Props* prop, const types::Range range, const StringView coupon) {
    if (range == types::Range::CouponHolder) {
        if (prop) return coupon.empty() ? prop->requiredMsgs().rangeWithNoCoupon.value
            : ::cwxeditor::cwx::utils::tryFormat(prop->requiredMsgs().rangeWithCoupon.value,coupon);
        return coupon.empty() ? "Coupon holder" : "Coupon holder: " + String(coupon);
    }
    if (prop) {
        try { return prop->requiredMsgs().rangeName(static_cast<int>(range)); }
        catch (...) { }
    }
    static constexpr std::array<StringView, 10> names = {
        "Selected", "Random", "Party", "Backpack", "Party and backpack",
        "Field", "Coupon holder", "Card target", "Selected card", "NPC"};
    const auto index = static_cast<std::size_t>(range);
    return index < names.size() ? String(names[index]) : String("Range");
}

String transitionText(const Props* prop, const types::Transition transition,
                      const std::uint32_t transitionSpeed) {
    String name;
    if (prop) {
        try { name = prop->requiredMsgs().transitionName(static_cast<int>(transition)); }
        catch (...) { }
    }
    if (name.empty()) {
        static constexpr std::array<StringView, 5> names = {
            "Default", "None", "Blinds", "Pixel dissolve", "Fade"};
        const auto index = static_cast<std::size_t>(transition);
        name = index < names.size() ? String(names[index]) : String("Transition");
    }
    if (prop) return transition == types::Transition::None
        ? utils::tryFormat(prop->requiredMsgs().transitionNoSpeed.value, name)
        : utils::tryFormat(prop->requiredMsgs().transitionWithSpeed.value, name, transitionSpeed);
    if (transition == types::Transition::None) return name;
    return name + " (" + std::to_string(transitionSpeed) + ")";
}

String msgAttrText(const Props* prop, const Content* content) {
    if (!content || !prop) return {};
    const auto& m = prop->requiredMsgs();
    std::vector<String> values;
    const auto add = [&](const String& value) { if (!value.empty()) values.push_back(value); };
    if (content->centeringX() && content->centeringY() && !content->singleLine()) add(m.centeringXYOn.value);
    else if (content->centeringX()) add(m.centeringXOn.value);
    else if (content->centeringY() && !content->singleLine()) add(m.centeringYOn.value);
    if (content->boundaryCheck() && !content->singleLine()) add(m.boundaryCheckOn.value);
    if (content->selectionColumns() != 1U && !m.ctColumns.value.empty())
        add(utils::tryFormat(m.ctColumns.value, content->selectionColumns()));
    if (content->selectTalker() && warning::hasCharacterTalker(*content) && !content->singleLine()) add(m.ctSelectTalker.value);
    String result;
    for (const auto& value : values) { if (!result.empty()) result += ' '; result += value; }
    return result;
}

String cardSpeedAttrText(const EditorContext& context, const Content* content) {
    if (!content) return {};
    if (context.prop) {
        const auto& m = context.prop->requiredMsgs();
        if (content->cardSpeed() == -1) return utils::tryFormat(m.ctCardSpeed.value, m.ctCardSpeedDefault.value);
        return content->overrideCardSpeed() ? utils::tryFormat(m.ctOverrideCardSpeed.value, content->cardSpeed())
            : utils::tryFormat(m.ctCardSpeed.value, content->cardSpeed());
    }
    return "card speed: " + std::to_string(content->cardSpeed());
}

String getStepValue(const Props* prop, const flag::Step* step, const int value) {
    if (step && value >= 0 && static_cast<std::uint32_t>(value) < step->count())
        return step->getValue(static_cast<std::uint32_t>(value));
    if (prop) return utils::tryFormat(prop->requiredMsgs().unknownStepValue.value, value);
    return "Unknown (" + std::to_string(value) + ")";
}

String castRangesName(const Props* prop, const std::vector<types::CastRange>& ranges) {
    if (prop) {
        const auto& m=prop->requiredMsgs();
        const auto name=[&](const std::size_t i){return m.castRangeName(ranges[i]);};
        if (ranges.size()>=3U) return ::cwxeditor::cwx::utils::tryFormat(m.castRange3.value,name(0),name(1),name(2));
        if (ranges.size()==2U) return ::cwxeditor::cwx::utils::tryFormat(m.castRange2.value,name(0),name(1));
        if (ranges.size()==1U) return ::cwxeditor::cwx::utils::tryFormat(m.castRange1.value,name(0));
        return m.castRange0.value;
    }

    std::vector<String> values;
    for (const auto range : ranges) {
        (void)prop;
        switch (range) {
        case types::CastRange::Party: values.emplace_back("Party"); break;
        case types::CastRange::Enemy: values.emplace_back("Enemy"); break;
        case types::CastRange::Npc: values.emplace_back("NPC"); break;
        }
    }
    if (values.empty()) return "None";
    String result;
    for (const auto& value : values) {
        if (!result.empty()) result += ' ';
        result += value;
    }
    return result;
}

String exprStr(const StringView expression) {
    // DのsplitLinesは不正UTF-8にも寛容。入力全体を検証
    // せず改行区切りを認識し、その後ASCIIだけの空白除去を行う。
    String result;
    const auto append_line = [&](std::size_t first, std::size_t last) {
        result += ::cwxeditor::cwx::utils::astrip(String(expression.substr(first, last - first)));
    };
    std::size_t first = 0;
    for (std::size_t i = 0; i < expression.size();) {
        const auto byte = static_cast<unsigned char>(expression[i]);
        std::size_t length = 0;
        if (byte == '\r') length = i + 1 < expression.size() && expression[i + 1] == '\n' ? 2 : 1;
        else if (byte == '\n' || byte == '\v' || byte == '\f') length = 1;
        else if (byte == 0xc2 && i + 1 < expression.size() &&
                 static_cast<unsigned char>(expression[i + 1]) == 0x85) length = 2;
        else if (byte == 0xe2 && i + 2 < expression.size() &&
                 static_cast<unsigned char>(expression[i + 1]) == 0x80 &&
                 (static_cast<unsigned char>(expression[i + 2]) == 0xa8 ||
                  static_cast<unsigned char>(expression[i + 2]) == 0xa9)) length = 3;
        if (!length) { ++i; continue; }
        append_line(first, i);
        i += length;
        first = i;
    }
    append_line(first, expression.size());
    return result;
}

// C++の意図的な互換拡張として、共通EditorはCBisTextから構造化
// XMLも受け付ける。DはCBisXMLを別に調べる。この追加経路はeditor_gui_clipboard_empty_text_tests
// で検証する。
bool CBisText(const lib::ClipboardData& clipboard) {
    // Dは空文字列も含め、TextTransfer形式の利用可否を調べる。
    return !clipboard.text.empty() ||
           std::ranges::any_of(kClipboardTextTypes, [&](const auto type) {
               return clipboard.formats.contains(String(type));
           }) ||
           xmlbytestransfer::CBisXML(clipboard);
}
bool CBisFile(const lib::ClipboardData& clipboard) {
    return !clipboard.files.empty() ||
           clipboard.formats.contains("application/x-cwxeditor-file-list");
}
bool CBisText(const lib::ClipboardBackend& clipboard) {
    return std::ranges::any_of(kClipboardTextTypes, [&](const auto type) {
               return clipboard.contains(type);
           }) || !clipboard.read_text().empty() || xmlbytestransfer::CBisXMLOnly(clipboard);
}
bool CBisFile(const lib::ClipboardBackend& clipboard) {
    // Dはパスを取得する前にFileTransferが提供されている
    // か調べる。SDLなどは有効な形式でも復号後のファイル一覧が空
    // の場合があるため、データ読み取り前に利用可否を問い合わせる。
    return clipboard.contains("application/x-cwxeditor-file-list");
}

DropFiles::DropFiles(std::shared_ptr<lib::TextEdit> text,
                     std::function<String(const FileNames&)> drop,
                     std::function<void(String)> dropPath)
    : text_(std::move(text)), drop_(std::move(drop)), dropPath_(std::move(dropPath)) {}

void DropFiles::dragEnter(lib::DragEvent& event) {
    event.selected = text_ && text_->enabled() ? lib::DropOperation::Link : lib::DropOperation::None;
    event.handled = true;
}

void DropFiles::dragOver(lib::DragEvent& event) { dragEnter(event); }

void DropFiles::drop(lib::DragEvent& event) {
    event.selected = lib::DropOperation::None;
    if (!text_ || !text_->enabled()) return;
    // Dの転送先はLinkだけを受け付ける。操作の合意時だけでなく配送時にも検査し
    // 、Copy/Moveのコールバックが転送拒否前に項目を変更しないようにする。
    if ((static_cast<std::uint32_t>(event.allowed) &
         static_cast<std::uint32_t>(lib::DropOperation::Link)) == 0U) return;
    FileNames files;
    files.reserve(event.data.files.size());
    for (const auto& file : event.data.files) files.push_back(pathString(file));
    auto value = drop_ ? drop_(files) : String{};
    if (value.empty() || value == text_->text) return;
    const auto target = text_;
    const auto dropped = dropPath_;
    // DのText.setTextはModifyを通知する。入力と同じネイティブの変更
    // 経路を使い、通知が所有元を終了させる前にコントロールとコールバックを保持する。
    (void)lib::set_text_value(*target, value);
    if (target->text != value) return; // Modify中に行われた新しい編集を優先する。
    target->selection_start = 0U;
    target->selection_length = value.size();
    if (dropped) dropped(value);
    event.selected = lib::DropOperation::Link;
    event.handled = true;
}

void setupDropFile(const WidgetPtr& control, const std::shared_ptr<lib::TextEdit>& text,
                   std::function<String(const FileNames&)> drop,
                   std::function<void(String)> dropPath, lib::Services services) {
    if (!control || !text || !services.drag_drop) return;
    if (!drop) drop = dropDefault;
    const auto target = std::make_shared<DropFiles>(
        text, std::move(drop), std::move(dropPath));
    lib::DropTargetBinding binding;
    binding.target = control;
    binding.accepted_formats = {"text/uri-list"};
    binding.on_enter = [target](lib::DragEvent& event) { target->dragEnter(event); };
    binding.on_over = [target](lib::DragEvent& event) { target->dragOver(event); };
    binding.on_drop = [target](lib::DragEvent& event) { target->drop(event); };
    services.drag_drop->bind_target(std::move(binding));
}

String dropDir(const FileNames& files) {
    if (files.empty()) return {};
    const auto pathValue = filesystemPath(files.front());
    std::error_code error;
    if (!std::filesystem::exists(pathValue, error) || error) return {};
    const bool directory = std::filesystem::is_directory(pathValue, error);
    if (error) return {};
    return pathString(directory ? pathValue : pathValue.parent_path());
}

String dropDefault(const FileNames& files) { return files.empty() ? String{} : files.front(); }

String selectFile(const std::shared_ptr<lib::TextEdit>& file,
                  const std::vector<String>& names,
                  const std::vector<String>& extensions, const StringView fileName,
                  const StringView title, const StringView path,
                  lib::Services services) {
    lib::FilePickerRequest request;
    request.title = String(title);
    request.initial_directory = utils::nabs(filesystemPath(path)).parent_path();
    request.initial_name = String(fileName);
    request.allow_multiple = false;
    for (std::size_t i = 0; i < names.size(); ++i) {
        lib::FileFilter filter;
        filter.label = names[i];
        if (i < extensions.size()) {
            const auto extension = extensions[i];
            filter.patterns.push_back(extension == "*" ? extension
                                                        : (extension.empty() || extension.front() == '*'
                                                               ? extension
                                                               : "*" + extension));
        }
        request.filters.push_back(std::move(filter));
    }
    String result;
    if (services.file_picker) {
        const auto selected = services.file_picker->open_file(request);
        if (selected.succeeded() && !selected.selections.empty())
            if (const auto path=selected.selections.front().native_path()) result = pathString(*path);
    }
    if (file && !result.empty()) {
        file->text = result;
        // 通知で終了・置換されても、コールバックを生存させる。
        const auto notify = file->on_changed;
        if (notify) notify(result);
    }
    return result;
}

std::filesystem::path pickerInitialDirectory(const Props* prop, StringView value,
                                               bool appPath) {
    auto initial = filesystemPath(value);
    if (appPath && initial.is_relative() && prop)
        initial = filesystemPath(prop->parent().appPath()).parent_path() / initial;
    return utils::nabs(initial);
}

String selectFile(const WindowPtr& shell, const std::vector<String>& names,
                  const std::vector<String>& extensions, const StringView fileName,
                  const StringView title, const StringView path,
                  lib::Services services) {
    (void)shell;
    return selectFile(std::shared_ptr<lib::TextEdit>{}, names, extensions,
                      fileName, title, path, services);
}

std::shared_ptr<lib::Button> createOpenButton(EditorContext* context,
                                               const ContainerPtr& parent,
                                               std::function<String()> getText,
                                               const bool directory) {
    auto button = std::make_shared<lib::Button>();
    if (context && context->prop) {
        button->icon = context->prop->images().menu(types::MenuID::OpenDir);
        button->set_tooltip(context->prop->requiredMsgs().menuText(
            directory ? types::MenuID::OpenDir : types::MenuID::OpenPlace));
    }
    // Dは両方のツールチップの選択肢にOpenDirを使う。bool
    // 値で、渡されたパスの解決方法や開くディレクトリは変わらない。
    button->on_click = [context, getText = std::move(getText)] {
        OpenDir(context, getText).widgetSelected();
    };
    if (parent) parent->add(button);
    return button;
}

lib::Action bindOpenButtonEnabledToNonEmptyText(
    const std::shared_ptr<lib::Button>& button,
    const std::shared_ptr<lib::TextEdit>& text,
    std::function<bool()> additionalEnabled) {
    if (!button || !text) return {};
    auto state = std::make_shared<OpenButtonEnabledState>();
    state->button = button;
    state->text = text;
    state->additionalEnabled = std::move(additionalEnabled);
    const std::weak_ptr<OpenButtonEnabledState> weak_state = state;
    const auto previous = text->on_changed;
    text->on_changed = [previous, weak_state](const std::string_view value) {
        if (previous) previous(value);
        // 既存のコールバック後にウィジェットを読み直す。値が解除・
        // 置換され得るため、入れ子の編集をこの通知より優先する。
        if (const auto current = weak_state.lock()) current->refresh();
    };
    state->refresh();
    return [state] { state->refresh(); };
}

lib::Action bindOpenButtonEnabledToNonEmptyText(
    const std::shared_ptr<lib::Button>& button,
    const std::shared_ptr<lib::ComboBox>& combo,
    std::function<bool()> additionalEnabled) {
    if (!combo) return {};
    const std::weak_ptr<lib::ComboBox> weak_combo = combo;
    return bindOpenButtonEnabledToTextGetter(
        button, combo,
        [weak_combo] {
            if (const auto field = weak_combo.lock()) return field->edit_text;
            return String{};
        }, std::move(additionalEnabled));
}

lib::Action bindOpenButtonEnabledToTextGetter(
    const std::shared_ptr<lib::Button>& button,
    const std::shared_ptr<lib::ComboBox>& combo,
    std::function<String()> getText,
    std::function<bool()> additionalEnabled) {
    if (!button || !combo || !getText) return {};
    auto state = std::make_shared<OpenButtonEnabledState>();
    state->button = button;
    state->combo = combo;
    state->getText = std::move(getText);
    state->additionalEnabled = std::move(additionalEnabled);
    const std::weak_ptr<OpenButtonEnabledState> weak_state = state;
    const auto previousText = combo->on_text_changed;
    combo->on_text_changed = [previousText, weak_state](const std::string_view value) {
        if (previousText) previousText(value);
        if (const auto current = weak_state.lock()) current->refresh();
    };
    const auto previousSelection = combo->on_selection_changed;
    combo->on_selection_changed = [previousSelection, weak_state](
                                      const std::optional<std::size_t> value) {
        if (previousSelection) previousSelection(value);
        if (const auto current = weak_state.lock()) current->refresh();
    };
    state->refresh();
    return [state] { state->refresh(); };
}

String openButtonText(const StringView appPath, const StringView value) {
    if (utils::pathIsAbsolute(value)) return utils::nabs(value);
    const auto path = utils::buildPath(utils::pathDirName(appPath), value);
    return utils::nabs(StringView(path));
}

String openButtonText(const Props* prop, const StringView value) {
    return prop ? openButtonText(prop->parent().appPath(), value)
                : String(value);
}

std::optional<String> resolveOpenDirectory(const StringView value) {
    auto target = filesystemPath(value);
    std::error_code error;
    if (!std::filesystem::is_directory(target, error))
        target = target.parent_path();
    if (target.empty()) return {};
    error.clear();
    if (!std::filesystem::exists(target, error)) return {};
    return pathString(target.lexically_normal());
}

std::optional<String> openButtonDirectory(const Props* prop,
                                          const StringView value) {
    return resolveOpenDirectory(openButtonText(prop, value));
}

std::optional<String> openButtonDirectory(const StringView appPath,
                                          const StringView value) {
    return resolveOpenDirectory(openButtonText(appPath, value));
}

OpenDir::OpenDir(EditorContext* context, std::function<String()> text)
    : context_(context), text_(std::move(text)) {}

void OpenDir::widgetSelected() {
    if (!context_ || !text_) return;
    const auto directory = openButtonDirectory(context_->prop, text_());
    if (directory) openFolder(*directory, context_->services);
}

bool updateChecked(lib::TableView& table, const TableItem item, const bool checked,
                   const bool checkEvent) {
    if (!checkEvent || item >= table.rows.size()) return false;
    table.rows[item].checked = checked;
    if (std::find(table.selection.begin(), table.selection.end(), item) == table.selection.end())
        return false;
    for (const auto selected : table.selection)
        if (selected < table.rows.size()) table.rows[selected].checked = checked;
    return true;
}

bool updateChecked(lib::TreeView& tree, const lib::TreeItemId item, const bool checked,
                   const bool checkEvent) {
    if (!checkEvent) return false;
    auto* node = findNode(std::shared_ptr<lib::TreeView>(&tree, [](lib::TreeView*) {}), item);
    if (!node) return false;
    node->checked = checked;
    if (std::find(tree.selection.begin(), tree.selection.end(), item) == tree.selection.end())
        return false;
    for (const auto selected : tree.selection)
        if (auto* selectedNode = findNode(std::shared_ptr<lib::TreeView>(&tree, [](lib::TreeView*) {}), selected)) selectedNode->checked = checked;
    return true;
}

int getAllItemCount(const TreePtr& tree) {
    return tree ? static_cast<int>(tree->nodes.size()) : 0;
}

ResourceId fimage(const Props* prop, const StringView file, const Skin* skin,
                  lib::Services services,
                  const std::vector<String>* plainTextExtensions) {
    (void)services;
    if (!prop) return 0U;
    auto& images = const_cast<Props*>(prop)->images();
    try {
        if (utils::isBinImg(file)) {
            if (skin && skin->isCardImage(file, false, false)) return images.cards();
            if (skin && skin->isBgImage(file)) return images.backs();
        } else {
            const auto path = filesystemPath(file);
            std::error_code error;
            if (std::filesystem::exists(path, error) && !error) {
                const bool directory = std::filesystem::is_directory(path, error);
                if (!error && directory) return images.folder();
                if (skin && skin->isCardImage(file, false, false)) return images.cards();
                if (skin && skin->isBgImage(file)) return images.backs();
                if (skin && skin->isBGM(file)) return images.bgm();
                if (skin && skin->isSE(file)) return images.se();
            }
        }

        const auto extension = pathString(filesystemPath(file).extension());
        const auto& textExtensions = plainTextExtensions
            ? *plainTextExtensions : prop->var().plainTextFileExtensions.value;
        for (const auto& pattern : textExtensions) {
            if (utils::cfnmatch(extension, pattern)) return images.text();
        }
    } catch (const std::exception&) {
        // Dのfimageは、ファイル分類に失敗した場合に不明アイコンを維持する。
    }
    return images.unknown();
}

// DのgetSymbolsは検索・置換・取り込み結果の行で共有す
// る。Dの出力値とともに成功可否を返し、呼び出し側が番兵値を推測
// したり、別の資源種別の変換を作ったりしなくてよいようにする。
bool getSymbols(const EditorContext& context, const Summary* summ,
                const CWXPath* object, const bool desc, String& text,
                ResourceId& image) {
    namespace m = ::cwxeditor::cwx;
    using ::cwxeditor::cwx::utils::tryFormat;
    using ::cwxeditor::cwx::utils::singleLine;
    image = 0U;
    text = "*Error*";
    if (!object || !context.prop) return false;
    const auto& msgs = context.prop->requiredMsgs();
    auto* images = &const_cast<Props*>(context.prop)->images().standardRegistry();
    const auto* path = object;
    if (const auto* sum = dynamic_cast<const m::summary::Summary*>(path)) {
        image = images->summary();
        text = desc ? tryFormat(msgs.searchResultSummary.value, singleLine(sum->desc()))
                    : sum->scenarioName();
        return true;
    }
    std::shared_ptr<m::path::CWXPath> holder_owner;
    if (const auto* holder = dynamic_cast<const m::textholder::SimpleTextHolder*>(path)) {
        holder_owner = std::dynamic_pointer_cast<m::path::CWXPath>(holder->owner());
        if (holder_owner) path = holder_owner.get();
    }
    const auto ids = [&](const String& kind, std::uint64_t id, const String& name,
                         const String& description) {
        text = !desc || description.empty()
            ? tryFormat(msgs.searchResultIds.value, kind, id, name)
            : tryFormat(msgs.searchResultIdsWithDesc.value, kind, id, name,
                        singleLine(description));
    };
    if (const auto* ic = dynamic_cast<const m::background::ImageCell*>(path)) {
        image = images->backs();
        text = tryFormat(msgs.searchResultImageCell.value, ic->name(context.prop->parent()));
    } else if (const auto* tc = dynamic_cast<const m::background::TextCell*>(path)) {
        image = images->textCell();
        text = tryFormat(msgs.searchResultTextCell.value, tc->name(context.prop->parent()));
    } else if (const auto* cc = dynamic_cast<const m::background::ColorCell*>(path)) {
        image = images->colorCell();
        text = tryFormat(msgs.searchResultColorCell.value, cc->name(context.prop->parent()));
    } else if (const auto* pc = dynamic_cast<const m::background::PCCell*>(path)) {
        image = images->pcCell();
        text = tryFormat(msgs.searchResultPCCell.value, pc->name(context.prop->parent()));
    } else if (const auto* area = dynamic_cast<const m::area::Area*>(path)) {
        image = images->area();
        text = tryFormat(msgs.searchResultIds.value, msgs.area.value, area->id(), area->name());
    } else if (const auto* battle = dynamic_cast<const m::area::Battle*>(path)) {
        image = images->battle();
        text = tryFormat(msgs.searchResultIds.value, msgs.battle.value, battle->id(), battle->name());
    } else if (const auto* package = dynamic_cast<const m::area::Package*>(path)) {
        image = images->packages();
        text = tryFormat(msgs.searchResultIds.value, msgs.cwPackage.value, package->id(), package->name());
    } else if (dynamic_cast<const m::area::PlayerCardEvents*>(path)) {
        image = images->playerCard();
        text = msgs.playerCard.value;
    } else if (const auto* cast = dynamic_cast<const m::card::CastCard*>(path)) {
        image = images->casts();
        ids(msgs.cwCast.value, cast->id(), cast->name(), cast->desc());
    } else if (const auto* skill = dynamic_cast<const m::card::SkillCard*>(path)) {
        std::shared_ptr<const m::card::SkillCard> linked;
        if (skill->linkId() != 0U) {
            if (!summ || !(linked = summ->skill(skill->linkId()))) return false;
            skill = linked.get();
        }
        image = images->skill();
        ids(msgs.skill.value, skill->id(), skill->name(), skill->desc());
    } else if (const auto* item = dynamic_cast<const m::card::ItemCard*>(path)) {
        std::shared_ptr<const m::card::ItemCard> linked;
        if (item->linkId() != 0U) {
            if (!summ || !(linked = summ->item(item->linkId()))) return false;
            item = linked.get();
        }
        image = images->item();
        ids(msgs.item.value, item->id(), item->name(), item->desc());
    } else if (const auto* beast = dynamic_cast<const m::card::BeastCard*>(path)) {
        std::shared_ptr<const m::card::BeastCard> linked;
        if (beast->linkId() != 0U) {
            if (!summ || !(linked = summ->beast(beast->linkId()))) return false;
            beast = linked.get();
        }
        image = images->beast();
        ids(msgs.beast.value, beast->id(), beast->name(), beast->desc());
    } else if (const auto* info = dynamic_cast<const m::card::InfoCard*>(path)) {
        image = images->info();
        ids(msgs.info.value, info->id(), info->name(), info->desc());
    } else if (const auto* content = dynamic_cast<const m::event::Content*>(path)) {
        image = images->content(content->type());
        text = dutils::contentText(context, content, summ);
    } else if (const auto* expr = dynamic_cast<const m::expression::Expression*>(path)) {
        const auto owner = std::dynamic_pointer_cast<m::event::Content>(expr->owner());
        if (!owner) return false;
        image = images->content(owner->type());
        text = dutils::contentText(context, owner.get(), summ);
    } else if (const auto* dialog = dynamic_cast<const m::event::SDialog*>(path)) {
        const auto parent = dialog->parent();
        image = images->content(types::CType::TalkDialog);
        text = dutils::talkDialogText(context, parent.get(), dialog);
    } else if (const auto* flag = dynamic_cast<const m::flag::Flag*>(path)) {
        image = images->flag();
        text = tryFormat(msgs.searchResultFlag.value, flag->path());
    } else if (const auto* step = dynamic_cast<const m::flag::Step*>(path)) {
        image = images->step();
        text = tryFormat(msgs.searchResultStep.value, step->path());
    } else if (const auto* variant = dynamic_cast<const m::flag::Variant*>(path)) {
        image = images->variant();
        text = tryFormat(msgs.searchResultVariant.value, variant->path());
    } else if (const auto* dir = dynamic_cast<const m::flag::FlagDir*>(path)) {
        image = images->flagDir();
        const auto dir_path = dir->path();
        text = dir_path.empty() ? msgs.flagsAndSteps.value
                                : tryFormat(msgs.searchResultFlagDir.value, dir_path);
    } else if (const auto* tree = dynamic_cast<const m::event::EventTree*>(path)) {
        image = images->eventTree();
        text = tryFormat(msgs.searchResultEventTree.value, tree->name());
    } else if (const auto* menu = dynamic_cast<const m::area::MenuCard*>(path)) {
        image = images->cards();
        text = !desc || menu->desc().empty()
            ? tryFormat(msgs.searchResultMenuCard.value, menu->name())
            : tryFormat(msgs.searchResultMenuCardWithDesc.value, menu->name(),
                        singleLine(menu->desc()));
    } else if (const auto* enemy = dynamic_cast<const m::area::EnemyCard*>(path)) {
        image = images->cards();
        String name = msgs.noSelectCast.value;
        if (enemy->isOverrideName()) {
            name = enemy->overrideName();
        } else if (enemy->id() != 0U) {
            const auto cast = summ ? summ->cwCast(enemy->id()) : nullptr;
            name = cast ? cast->name() : tryFormat(msgs.noCast.value, enemy->id());
        }
        text = tryFormat(msgs.searchResultEnemyCard.value, name);
    } else if (const auto* section = dynamic_cast<const m::jpy::Jpy1Sec*>(path)) {
        if (!summ) return false;
        image = images->backs();
        text = dutils::searchResultScenarioRelativeLabel(
            msgs.searchResultJpy1.value, section->fPath, summ->scenarioPath());
    } else if (const auto* palette = dynamic_cast<const m::jpy::Jpdc*>(path)) {
        if (!summ) return false;
        image = images->backs();
        text = dutils::searchResultScenarioRelativeLabel(
            msgs.searchResultJpdc.value, palette->jpdcPath, summ->scenarioPath());
    } else if (const auto* motion = dynamic_cast<const m::motion::Motion*>(path)) {
        image = images->motion(motion->type());
        text = msgs.motionName(motion->type());
    } else {
        return false;
    }
    return true;
}

String talkDialogText(const EditorContext& context, const Content* content,
                      const SDialog* dialog) {
    if (!dialog) return {};
    if (!context.prop) return utils::singleLine(dialog->text());
    const auto& messages = context.prop->requiredMsgs();
    const auto format = [&](const auto& pattern, const auto&... values) {
        return utils::tryFormat(pattern.value, values...);
    };
    const auto talker = format(
        messages.ctTalkMessageImage,
        content ? messages.talkerName(content->talkerNC())
                : messages.noInformation.value);
    auto text = utils::singleLine(dialog->text());
    if (dialog->text().empty()) text = messages.noText.value;

    String coupons;
    for (const auto& coupon : dialog->rCoupons()) {
        if (!coupons.empty()) coupons += ' ';
        coupons += coupon;
    }
    const auto attrs = msgAttrText(context.prop, content);
    if (!coupons.empty())
        return attrs.empty()
                   ? format(messages.ctTalkDialog, talker, coupons, text)
                   : format(messages.ctTalkDialogWithAttrs, talker, coupons,
                            text, attrs);
    return attrs.empty()
               ? format(messages.ctTalkDialogNoCoupon, talker, text)
               : format(messages.ctTalkDialogNoCouponWithAttrs, talker, text,
                        attrs);
}

ResourceId cardIcon(const Props* prop, const Skin* skin, const Summary* summ,
                     const ::cwxeditor::cwx::area::AbstractSpCard* card,
                     const bool eventView) {
    (void)skin;
    (void)summ;
    (void)eventView;
    if (!prop || !card) return 0U;
    auto& images = const_cast<Props*>(prop)->images();
    const bool has_flag = !card->flag().empty();
    if (const auto* enemy =
            dynamic_cast<const ::cwxeditor::cwx::area::EnemyCard*>(card);
        enemy && enemy->action(types::ActionCardType::RunAway)) {
        return images.load(has_flag ? StringView("escape_flag.png")
                                    : StringView("escape.png"));
    }
    return images.load(has_flag ? StringView("cards_flag.png")
                                : StringView("cards.png"));
}

void wDrawText(lib::DrawingContext& gc, const StringView text, const int x,
               const int y, const lib::FontSpec& font, const lib::Color color) {
    // SWTのGCは背景色を保持する。共通描画側では、この
    // グリフ描画の前に呼び出し側の表示処理が背景を描く。
    const auto layout = lib::layout_gc_text(gc, text, font);
    lib::draw_gc_text(gc, layout,
        {static_cast<float>(x), static_cast<float>(y)}, font, color);
}

void wDrawText(lib::DrawingContext& gc, const StringView text, const int x,
               const int y, const bool transparent, const lib::FontSpec& font,
               const lib::Color color) {
    // 移植したDの呼び出し側はtransparent=trueを使う。背景の塗り
    // は、DrawingContextの暗黙状態ではなく所有する表示側に任せる。
    (void)transparent;
    wDrawText(gc, text, x, y, font, color);
}

void wDrawText(lib::DrawingContext& gc, const StringView text, const int x,
               const int y, const int flags, const lib::FontSpec& font,
               const lib::Color color) {
    const lib::GcTextOptions options{
        (flags & kSwtDrawDelimiter) != 0,
        (flags & kSwtDrawTab) != 0};
    const auto layout = lib::layout_gc_text(gc, text, font, options);
    lib::draw_gc_text(gc, layout,
        {static_cast<float>(x), static_cast<float>(y)}, font, color);
}

lib::Point wTextExtent(lib::DrawingContext& gc, const StringView text,
                       const lib::FontSpec& font) {
    const auto layout = lib::layout_gc_text(gc, text, font);
    return {layout.extent.width, layout.extent.height};
}

bool isEnableGdipAndNotTrueTypeFont(const lib::DrawingContext& gc) {
    (void)gc;
    return false;
}

void shrinkDrawText(lib::DrawingContext& gc, std::shared_ptr<lib::GraphicsBackend> graphics,
                    const lib::FontSpec& font, const lib::Color foreground,
                    const StringView title, const int x, const int y,
                    const lib::Point size, const bool smoothing,
                    const structs::CRGB hemmingColor) {
    if (size.x <= 0 || size.y <= 0 || title.empty()) return;
    if (!graphics) throw std::invalid_argument("shrinkDrawText requires a GraphicsBackend");
    lib::ImageTextStyle style{font, foreground,
        {static_cast<lib::Byte>(hemmingColor.r),static_cast<lib::Byte>(hemmingColor.g),
         static_cast<lib::Byte>(hemmingColor.b),static_cast<lib::Byte>(hemmingColor.a)},0,smoothing};
    style.target_size = lib::Size{size.x,size.y};
    style.preserve_font_antialias = true;
    auto raster = lib::rasterize_image_text(graphics,String(title),std::move(style));
    if (!raster.pixel_width || !raster.pixel_height) return;
    lib::ImageData data;data.frames.push_back(std::move(raster));
    auto resource=std::make_shared<lib::OwnedImageResource>(graphics);
    resource->id=graphics->create_image(data);
    if (resource->id) gc.draw_image(resource->id,
        {0,0,float(data.frames[0].pixel_width),float(data.frames[0].pixel_height)},
        {float(x)-1,float(y)-1,float(data.frames[0].pixel_width),float(data.frames[0].pixel_height)},
        lib::Interpolation::Nearest);
}

void shrinkDrawText(lib::DrawingContext&, const StringView, const int,
                    const int, const lib::Point, const bool,
                    const structs::CRGB) {
    // DrawingContextには、DのGCの現在のフォント・前景色・
    // デバイスの状態がない。この互換入口で状態を黙って省略・捏造しない。
    throw std::logic_error("shrinkDrawText requires the GraphicsBackend + FontSpec + foreground overload");
}

void asyncExec(lib::Services services, lib::Action callback) {
    if (!callback) return;
    if (services.dispatcher) services.dispatcher->post(std::move(callback));
    else callback();
}

void setupWeights(const std::shared_ptr<splitpane::SplitPane>& sash,
                  settings::Prop<int, false>& left,
                  settings::Prop<int, false>& right, const EditorContext& context) {
    if (!sash) return;
    sash->setWeights({left.value, right.value});
    const std::weak_ptr<splitpane::SplitPane> weak = sash;
    (void)sash->addSelectionListener(
        [weak, &left, &right, save = context.saveWindow](splitpane::SelectionEvent&) {
            const auto current = weak.lock();
            if (!current) return;
            const auto weights = current->getWeights();
            if (weights.size() < 2) return;
            left.value = weights[0];
            right.value = weights[1];
            // DはShell.dataからAbsDialog.saveWinを得る。共通
            // ホストは、既存のEditorContextを通してその所有元の操作を渡す。
            if (save) save();
        });
}

void setupToggle(const std::shared_ptr<lib::ToggleButton>& toggle,
                 settings::Prop<bool, false>& value) {
    if (!toggle) return;
    toggle->checked = value.value;
    toggle->on_changed = [&value](bool checked) { value.value = checked; };
}

void setupSpinner(const NumberEditPtr& spinner, settings::Prop<int, false>& value) {
    if (!spinner) return;
    spinner->value = value.value;
    spinner->on_changed = [&value](double next) { value.value = static_cast<int>(std::lround(next)); };
}

lib::Rect setupWindow(const WindowPtr& window, WindowSettings& settings,
                      const EditorContext& context) {
    return setupWindow(window, settings, nullptr, true, true, context);
}

lib::Rect setupWindow(const WindowPtr& window, WindowSettings& settings,
                      std::function<void(bool)>* clearSetup,
                      const bool setLocation, const bool setSize,
                      const EditorContext& context) {
    if (!window) return {};
    auto bounds = window->bounds();
    const auto parent = window->owner.lock();
    const auto parentBounds = parent ? parent->bounds() : lib::Rect{};
    if (setLocation || setSize) {
        if (setSize) {
            const auto preferred = lib::window_content_preferred_size(*window);
            bounds.width = settings.width != -1 ? static_cast<float>(settings.width)
                : preferred.width > 0 ? preferred.width : bounds.width;
            bounds.height = settings.height != -1 ? static_cast<float>(settings.height)
                : preferred.height > 0 ? preferred.height : bounds.height;
            bounds.width = std::max(1.0F, bounds.width);
            bounds.height = std::max(1.0F, bounds.height);
            window->bounds_ui_text_scale = 1.0F;
        }
        if (setLocation) {
            bounds.x = settings.x != -1 ? parentBounds.x + settings.x
                : parent ? parentBounds.x + (parentBounds.width - bounds.width) / 2 : bounds.x;
            bounds.y = settings.y != -1 ? parentBounds.y + settings.y
                : parent ? parentBounds.y + (parentBounds.height - bounds.height) / 2 : bounds.y;
        }
        window->maximized = settings.maximized;
        if (context.services.gui) {
            const auto screens = context.services.gui->screens();
            if (!screens.empty()) {
                const auto x = bounds.x + bounds.width / 2, y = bounds.y + bounds.height / 2;
                const auto found = std::find_if(screens.begin(), screens.end(), [x,y](const auto& screen) {
                    return x >= screen.work_area.x && x < screen.work_area.x + screen.work_area.width &&
                           y >= screen.work_area.y && y < screen.work_area.y + screen.work_area.height;
                });
                const auto display = (found == screens.end() ? screens.front() : *found).work_area;
                int px = static_cast<int>(bounds.x), py = static_cast<int>(bounds.y);
                intoDisplay(px, py, static_cast<int>(bounds.width), static_cast<int>(bounds.height), display);
                bounds.x = static_cast<float>(px); bounds.y = static_cast<float>(py);
            }
        }
        window->set_bounds(bounds);
    }
    // 一つの共通Windowの矩形通知で、ネイティブの移動とサイズ変更を扱う。
    // 借用した設定の寿命はclearSetupまたはウィンドウまでに限定する。
    const auto active = std::make_shared<bool>(true);
    const std::weak_ptr<lib::Window> weak = window;
    const auto prior = window->on_bounds_changed;
    const auto save = [weak, active, &settings, dispatcher = context.services.dispatcher](bool deferred, bool notifySplits) {
        const auto current = weak.lock();
        if (!*active || !current) return;
        settings.maximized = current->maximized;
        if (current->maximized) return; // 最後の通常状態の座標を保持する。
        const auto owner = current->owner.lock();
        const auto origin = owner ? owner->bounds() : lib::Rect{};
        settings.x = static_cast<int>(current->bounds().x - origin.x);
        settings.y = static_cast<int>(current->bounds().y - origin.y);
        const auto saveSize = [weak, active, &settings, notifySplits] {
            const auto current = weak.lock();
            if (!*active || !current || current->maximized) return;
            const auto authored = lib::authored_window_bounds(*current);
            const auto width = static_cast<int>(authored.width), height = static_cast<int>(authored.height);
            if (settings.width == width && settings.height == height) return;
            settings.width = width; settings.height = height;
            if (!notifySplits) return;
            // Dは子の分割部のサイズ変更に伴う配置が完了するまで遅らせる。
            const auto visit = [&](auto&& self, const lib::WidgetPtr& widget) -> void {
                if (!*active || !widget) return;
                if (const auto split = std::dynamic_pointer_cast<splitpane::SplitPane>(widget))
                    split->notifySelectionListeners();
                if (!*active) return;
                if (const auto tabs = std::dynamic_pointer_cast<lib::TabView>(widget)) {
                    const auto pages = tabs->pages;
                    for (const auto& page : pages) self(self, page.content);
                }
                if (const auto container = std::dynamic_pointer_cast<lib::Container>(widget)) {
                    const auto children = container->children();
                    for (const auto& child : children) self(self, child);
                }
            };
            visit(visit, current);
        };
        if (deferred && dispatcher) dispatcher->post(saveSize); else saveSize();
    };
    struct BoundsListener {
        std::shared_ptr<bool> active;
        std::function<void(lib::Rect)> prior;
        std::function<void(bool,bool)> save;
        void operator()(lib::Rect value) const {
            if (prior) prior(value);
            if (*active) save(true, true);
        }
    };
    window->on_bounds_changed = BoundsListener{active, prior, save};
    if (clearSetup) *clearSetup = [weak, active, prior, save](const bool finalSave) {
        if (!*active) return;
        // このAPIの明示的な最終保存の選択肢を維持する。接続解除で待機中
        // のサイズ変更処理を取り消し、借用したpropsより長く残さない。
        if (finalSave) save(false, false);
        *active = false;
        if (const auto current = weak.lock())
            if (const auto* own = current->on_bounds_changed.target<BoundsListener>(); own && own->active == active)
                current->on_bounds_changed = prior;
    };
    return window->bounds();
}

lib::FontSpec CreateFontW(const int height, const int width, const int escapement,
                          const int orientation, const int weight,
                          const std::uint32_t italic, const std::uint32_t underline,
                          const std::uint32_t strikeout, const std::uint32_t charset,
                          const std::uint32_t outputPrecision,
                          const std::uint32_t clipPrecision,
                          const std::uint32_t quality,
                          const std::uint32_t pitchAndFamily,
                          const StringView face) {
    (void)width;
    (void)escapement;
    (void)orientation;
    (void)charset;
    (void)outputPrecision;
    (void)clipPrecision;
    (void)quality;
    (void)pitchAndFamily;
    // GDIの符号付き高さを維持する。正はセル高、負は
    // 文字高、0はプラットフォームの既定高を要求する。
    return createFontFromPixels(face, height, weight >= 700,
                                italic != 0U, underline != 0U, strikeout != 0U,
                                quality == ANTIALIASED_QUALITY);
}

lib::FontSpec createFontFromPixels(const structs::CFont& font, const bool antialias) {
    const auto size = static_cast<int>(std::min<std::uint32_t>(
        font.point, static_cast<std::uint32_t>(std::numeric_limits<int>::max())));
    return createFontFromPixels(font.name, size, font.bold, font.italic,
                                false, false, antialias);
}

lib::FontSpec fontSpecFromDwtData(const structs::CFont& font, const bool antialias) {
    auto result = createFontFromPixels(font, antialias);
    // 同じCFontでも、DがdwtDataで変換する場合
    // はポイント単位。指定高0は既定高の要求として残す。
    if (font.point != 0U) result.size_mode = lib::FontSizeMode::TypographicPoints;
    return result;
}

lib::FontSpec fontSpecFromDwtData(const StringView face, const int points,
                                 const bool antialias) {
    auto result = createFontFromPixels(face, points, false, false, false, false,
                                       antialias);
    // Dのnew FontData(face, points, style)は、組版上のポイント単位を使う。
    if (points != 0) result.size_mode = lib::FontSizeMode::TypographicPoints;
    return result;
}

lib::FontSpec createFontFromPixels(const StringView face, const int pixels,
                                   const bool bold, const bool italic,
                                   const bool underline, const bool strike,
                                   const bool antialias) {
    lib::FontSpec result;
    result.family = String(face);
    // DのWindows用補助処理は、この符号付き論理高をCreateFontWへ渡す。
    // 共通プロバイダーはポイント以外のモードを、対応するネイティブの計測目標へ合わせる。
    result.points = static_cast<float>(std::fabs(static_cast<double>(pixels)));
    result.weight = static_cast<std::uint16_t>(bold ? 700 : 400);
    result.italic = italic;
    result.underline = underline;
    result.strikeout = strike;
    result.antialias = antialias;
    result.size_mode = pixels > 0 ? lib::FontSizeMode::CellPixels
        : (pixels < 0 ? lib::FontSizeMode::CharacterPixels
                      : lib::FontSizeMode::DefaultHeight);
    return result;
}

bool isEnterKey(const int keyCode) { return keyCode == 13 || keyCode == 0x1000050; }

ImageDataWithScale warningImage(const Props* prop, int width,
                                lib::Services services) {
    if (width < 0) width = 32;
    auto image = blankImage(std::max(1, width), 1);
    auto& frame = image.frames.front();
    std::uint8_t red = 220U;
    std::uint8_t green = 80U;
    std::uint8_t blue = 40U;
    if (prop) {
        const auto color = prop->var().warningImageColor.value;
        red = static_cast<std::uint8_t>(std::min<std::uint32_t>(255U, color.r));
        green = static_cast<std::uint8_t>(std::min<std::uint32_t>(255U, color.g));
        blue = static_cast<std::uint8_t>(std::min<std::uint32_t>(255U, color.b));
    }
    for (int x = 0; x < width; ++x) {
        const auto alpha = static_cast<std::uint8_t>(255U * x / std::max(1, width));
        const auto offset = static_cast<std::size_t>(x) * 4U;
        frame.pixels[offset] = static_cast<std::byte>(red);
        frame.pixels[offset + 1U] = static_cast<std::byte>(green);
        frame.pixels[offset + 2U] = static_cast<std::byte>(blue);
        frame.pixels[offset + 3U] = static_cast<std::byte>(alpha);
    }
    (void)services;
    return ImageDataWithScale(std::move(image), prop ? prop->drawingScale() : 1U);
}

void procShowingTreeItem(const TreePtr& tree,
                         std::function<bool(lib::TreeItemId)> proc) {
    if (!tree || !proc) return;
    const auto order = lib::visible_tree_indices(*tree);
    const auto rowHeight = lib::item_row_height(*tree,
        lib::scaled_ui_metric(lib::kEditorUiMetrics.row_height));
    const auto geometry = lib::item_scroll_geometry(*tree, rowHeight);
    if (!(rowHeight > 0) || !std::isfinite(rowHeight) ||
        !(geometry.viewport.height > 0) || !std::isfinite(geometry.viewport.height)) return;
    const auto count = static_cast<std::size_t>(std::min<double>(order.size(),
        std::ceil(static_cast<double>(geometry.viewport.height) / rowHeight)));
    std::vector<lib::TreeItemId> visible;
    for (std::size_t index = tree->top_index; index < order.size() && visible.size() < count; ++index)
        visible.push_back(tree->nodes[order[index]].id);
    // コールバック前にIDを所有する。訪問処理がツリーを再構築したり行を破棄したりし得るため。
    for (const auto id : visible) if (findNode(tree, id) && !proc(id)) break;
}

String contentTextResourceID(const EditorContext& context, const Summary* summ,
                             const CIDKind kind, const StringView idText,
                             const StringView pattern) {
    if (!context.prop) return utils::tryFormat(pattern, idText.empty() ? String("(none)") : String(idText));
    const auto& messages = context.prop->requiredMsgs();
    String noSelect, noID, name;
    bool used = !idText.empty(), exists = false;
    if (kind == CIDKind::Image) {
        name = utils::encodePath(String(idText));
        if (used && context.skin) exists = !context.skin->findImagePath(idText,
            summ ? summ->scenarioPath() : String{}, summ ? summ->dataVersion() : versioninfo::LATEST_VERSION).empty();
        return utils::tryFormat(pattern, !used ? messages.noSelectImage.value : exists ? name : utils::tryFormat(messages.noImage.value, name));
    }
    if (kind == CIDKind::BGM || kind == CIDKind::SE) {
        const bool bgm = kind == CIDKind::BGM;
        noSelect = bgm ? messages.noSelectBGM.value : messages.noSelectSE.value;
        noID = bgm ? messages.noBGM.value : messages.noSE.value;
        name = utils::encodePath(String(idText));
        if (used && context.skin) {
            const auto version = summ ? summ->dataVersion() : versioninfo::LATEST_VERSION;
            const auto scenario = summ ? summ->scenarioPath() : String{};
            exists = !context.skin->findPath(idText,
                bgm ? context.skin->extBgm() : context.skin->extSound(),
                bgm ? context.skin->bgmDirs() : context.skin->seDirs(), scenario, version,
                bgm ? context.skin->wsnMusicDirs(version) : context.skin->wsnSoundDirs(version)).empty();
        }
        return utils::tryFormat(pattern, !used ? noSelect : exists ? name : utils::tryFormat(noID, name));
    }
    std::uint64_t id{};
    const auto [end, error] = std::from_chars(idText.data(), idText.data()+idText.size(), id);
    used = error == std::errc{} && end == idText.data()+idText.size() && id != 0U;
    const auto resolved = [&](const auto& object) {
        exists = static_cast<bool>(object);
        if (object) name = utils::tryFormat(messages.nameWithID.value, id, object->name());
    };
    switch (kind) {
    case CIDKind::Area:
        noSelect = messages.noSelectArea.value; noID = messages.noArea.value;
        if (summ && used) resolved(summ->area(id));
        break;
    case CIDKind::Battle:
        noSelect = messages.noSelectBattle.value; noID = messages.noBattle.value;
        if (summ && used) resolved(summ->battle(id));
        break;
    case CIDKind::Package:
        noSelect = messages.noSelectPackage.value; noID = messages.noPackage.value;
        if (summ && used) resolved(summ->cwPackage(id));
        break;
    case CIDKind::Cast:
        noSelect = messages.noSelectCast.value; noID = messages.noCast.value;
        if (summ && used) resolved(summ->cwCast(id));
        break;
    case CIDKind::Skill:
        noSelect = messages.noSelectSkill.value; noID = messages.noSkill.value;
        if (summ && used) resolved(summ->skill(id));
        break;
    case CIDKind::Item:
        noSelect = messages.noSelectItem.value; noID = messages.noItem.value;
        if (summ && used) resolved(summ->item(id));
        break;
    case CIDKind::Beast:
        noSelect = messages.noSelectBeast.value; noID = messages.noBeast.value;
        if (summ && used) resolved(summ->beast(id));
        break;
    case CIDKind::Info:
        noSelect = messages.noSelectInfo.value; noID = messages.noInfo.value;
        if (summ && used) resolved(summ->info(id));
        break;
    default: return utils::tryFormat(pattern, idText);
    }
    return utils::tryFormat(pattern, !used ? noSelect : exists ? name : utils::tryFormat(noID, id));
}

String contentText(const EditorContext& context, const Content* content,
                   const Summary* summ) {
    if (!content) return {};
    const auto* prop = context.prop;
    const auto title = contentName(prop, content->type());
    const auto fallback = [&] {
        auto result = title;
        if (!content->name().empty()) result += ": " + content->name();
        if (!content->text().empty()) result += " " + exprStr(content->text());
        return result;
    };
    if (!prop) return fallback();

    try {
        const auto& msgs = prop->requiredMsgs();
        const auto format = [&](const auto& pattern, const auto&... values) {
            return utils::tryFormat(pattern.value, values...);
        };
        const auto variable = [&](const String& id, const int kind) {
            if (id.empty()) return kind == 0 ? msgs.noSelectFlag.value : kind == 1 ? msgs.noSelectStep.value : msgs.noSelectVariant.value;
            const auto root = summ ? summ->flagDirRoot() : nullptr;
            const auto counter = content->useCounter();
            const bool exists = kind == 0 ? bool(flag::findVar<flag::Flag>(root, counter, id))
                : kind == 1 ? bool(flag::findVar<flag::Step>(root, counter, id)) : bool(flag::findVar<flag::Variant>(root, counter, id));
            return exists ? id : format(kind == 0 ? msgs.noFlag : kind == 1 ? msgs.noStep : msgs.noVariant, id);
        };
        const auto flag = [&] { return variable(content->flag(), 0); };
        const auto step = [&] { return variable(content->step(), 1); };
        const auto variant = [&] { return variable(content->variant(), 2); };
        const auto start = [&] {
            const auto tree = content->weak_from_this().expired()
                ? decltype(content->tree()){} : content->tree();
            const auto id = content->start();
            bool used = !id.empty();
            if (!used && tree) for (const auto& root : tree->starts()) if (root && root->name().empty()) { used = true; break; }
            return !used ? msgs.noSelectStart.value : tree && tree->hasStart(id) ? id : format(msgs.noStart, id);
        };
        const auto text = [&] {
            return content->text().empty() ? msgs.noText.value : utils::singleLine(content->text());
        };

        const auto backgrounds = [&] {
            String value;
            for (const auto& cell : content->backgroundImages()) {
                if (!cell) continue;
                if (!value.empty()) value += " ";
                if (const auto image = dynamic_cast<const background::ImageCell*>(cell.get()))
                    value += contentTextResourceID(context, summ, CIDKind::Image, image->path(), msgs.ctChangeBgImageFile.value);
                else value += format(msgs.ctChangeBgImageFile, cell->name(prop->parent()));
            }
            if (value.empty()) for (const auto& path : content->backs()) {
                if (!value.empty()) value += " ";
                value += contentTextResourceID(context, summ, CIDKind::Image, path, msgs.ctChangeBgImageFile.value);
            }
            return value.empty() ? msgs.cellNoSet.value : value;
        };
        switch (content->type()) {
        case types::CType::Start:
            return format(msgs.ctStart, content->name());
        case types::CType::StartBattle:
            return format(msgs.ctStartBattle, contentTextUseID<CIDKind::Battle>(context, summ, content->battle(), "%s", content));
        case types::CType::End:
            return content->complete() ? msgs.ctEndComplete.value : msgs.ctEndNoComplete.value;
        case types::CType::EndBadEnd:
            return msgs.ctGameOver.value;
        case types::CType::ChangeArea: {
            const auto area = contentTextUseID<CIDKind::Area>(context, summ, content->area(), "%s", content);
            return content->transition() == types::Transition::Default
                ? format(msgs.ctChangeAreaClassic, area)
                : format(msgs.ctChangeAreaWithTransition, area,
                         transitionText(prop, content->transition(), content->transitionSpeed()));
        }
        case types::CType::ChangeBgImage: {
            const auto paths = backgrounds();
            return content->transition() == types::Transition::Default
                ? format(msgs.ctChangeBgImageClassic, paths)
                : format(msgs.ctChangeBgImageWithTransition, paths,
                         transitionText(prop, content->transition(), content->transitionSpeed()));
        }
        case types::CType::Effect: {
            String motions;
            for (const auto& motion : content->motionsRef()) {
                if (!motion) continue;
                if (!motions.empty()) motions += " ";
                motions += format(msgs.ctEffectMotion, msgs.motionName(motion->type()));
            }
            if (motions.empty()) motions = msgs.noEffect.value;
            lib::Array<String> args;
            if (!content->soundPath().empty())
                args.push_back(contentTextUseID<CIDKind::SE>(context, summ,
                    content->soundPath(), msgs.ctEffectSound.value, content));
            if (content->cardVisual() != types::CardVisual::None) {
                const auto visual = msgs.cardVisualName(content->cardVisual());
                args.push_back(content->cardSpeed() == -1 ? visual :
                    format(content->overrideCardSpeed() ? msgs.cardVisualWithOverrideSpeed : msgs.cardVisualWithSpeed,
                           visual, content->cardSpeed()));
            }
            if (content->initialEffect()) {
                args.push_back(content->initialSoundPath().empty() ? msgs.ctInitialEffect.value :
                    format(msgs.ctInitialEffectWithSound, contentTextUseID<CIDKind::SE>(context, summ,
                        content->initialSoundPath(), msgs.ctEffectSound.value, content)));
            }
            if (content->ignite()) {
                args.push_back(content->keyCodes().empty() ? msgs.ctIgnite.value :
                    format(msgs.ctIgniteWithKeyCode, format(msgs.ctKeyCodes, [&] { String codes; bool first=true; for (const auto& code : content->keyCodes()) { if (!first) codes += " "; first=false; codes += code; } return codes; }())));
            }
            if (content->refAbility())
                args.push_back(format(msgs.ctRefAbility, msgs.physicalName(content->physical()), msgs.mentalName(content->mental())));
            if (content->absorbTo() == types::AbsorbTo::Selected) args.push_back(msgs.ctAbsorbToSelected.value);
            const auto success = std::abs(content->successRate());
            const auto sign = content->successRate() >= 0 ? "+" : "-";
            const auto range = rangeName(prop, content->range(), content->holdingCoupon());
            if (!args.empty()) return format(msgs.ctEffectWith, range, content->signedLevel(),
                msgs.effectTypeName(content->effectType()), msgs.resistName(content->resist()), sign, success, motions, joinValues(args));
            return format(msgs.ctEffectSimple, range, content->signedLevel(),
                msgs.effectTypeName(content->effectType()), msgs.resistName(content->resist()), sign, success, motions);
        }
        case types::CType::EffectBreak:
            return content->consumeCard() ? msgs.ctEffectBreakConsumeCard.value
                                          : msgs.ctEffectBreakNoConsumeCard.value;
        case types::CType::LinkStart:
            return format(msgs.ctLinkStart, start());
        case types::CType::LinkPackage:
            return format(msgs.ctLinkPackage, contentTextUseID<CIDKind::Package>(context, summ, content->packages(), "%s", content));
        case types::CType::TalkMessage: {
            const auto attrs = msgAttrText(prop, content);
            if (content->singleLine())
                return attrs.empty() ? format(msgs.ctTalkMessageSingleLine, text())
                                     : format(msgs.ctTalkMessageSingleLineWithAttrs, text(), attrs);
            if (!content->cardImages().empty() || !content->cardPaths().empty()) {
                String images;
                bool used = false;
                const auto add = [&](const String& value) { if (!images.empty()) images += " "; images += value; };
                if (!content->cardImages().empty()) {
                    for (const auto& image : content->cardImages()) {
                        if (!image) continue;
                        if (image->type == types::CardImageType::Talker) {
                            add(msgs.talkerName(image->talker())); used = true;
                        } else {
                            add(contentTextResourceID(context, summ, CIDKind::Image, image->path(), "%s"));
                            // Dは配列のuseフラグではPCNumberを未対応として扱う。
                            if (image->type == types::CardImageType::File) used = used || !image->path().empty();
                        }
                    }
                } else for (const auto& path : content->cardPaths()) {
                    add(contentTextResourceID(context, summ, CIDKind::Image, path, "%s")); used = used || !path.empty();
                }
                const auto speaker = format(msgs.ctTalkMessageImage, used ? images : msgs.noSelectImage.value);
                return attrs.empty() ? format(msgs.ctTalkMessage, speaker, text())
                                     : format(msgs.ctTalkMessageWithAttrs, speaker, text(), attrs);
            }
            return attrs.empty() ? format(msgs.ctTalkMessageNarration, text())
                                 : format(msgs.ctTalkMessageNarrationWithAttrs, text(), attrs);
        }
        case types::CType::TalkDialog:
            if (!content->dialogs().empty()) {
                const auto& dialogs = content->dialogs();
                const SDialog* displayed = dialogs.front().get();
                switch (structs::toDialogStatus(prop->var().dialogStatus.value)) {
                case structs::DialogStatus::Under:
                    displayed = dialogs.back().get();
                    break;
                case structs::DialogStatus::UnderWithCoupon:
                    displayed = dialogs.back().get();
                    for (auto current = dialogs.rbegin(); current != dialogs.rend();
                         ++current) {
                        if (*current && !(*current)->rCoupons().empty()) {
                            displayed = current->get();
                            break;
                        }
                    }
                    break;
                case structs::DialogStatus::Top:
                    break;
                }
                return talkDialogText(context, content, displayed);
            }
            return fallback();
        case types::CType::PlayBgm: {
            const auto channel = content->bgmChannel() == 0U
                ? msgs.mainChannel.value : msgs.subChannel.value;
            const auto fade = utils::formatReal(
                static_cast<double>(content->bgmFadeIn()) / 100.0, 3U);
            if (content->bgmPath().empty()) return format(msgs.ctStopBGM, channel, fade);
            const auto path = contentTextUseID<CIDKind::BGM>(
                context, summ, content->bgmPath(), "%s", content);
            const auto loops = content->bgmLoopCount() == 0U
                ? msgs.infinity.value : std::to_string(content->bgmLoopCount());
            return format(msgs.ctPlayBGM, path, channel, fade,
                          content->bgmVolume(), loops);
        }
        case types::CType::PlaySound: {
            const auto channel = content->soundChannel() == 0U
                ? msgs.mainChannel.value : msgs.subChannel.value;
            const auto fade = utils::formatReal(
                static_cast<double>(content->soundFadeIn()) / 100.0, 3U);
            const auto path = contentTextUseID<CIDKind::SE>(
                context, summ, content->soundPath(), "%s", content);
            const auto loops = content->soundLoopCount() == 0U
                ? msgs.infinity.value : std::to_string(content->soundLoopCount());
            return format(msgs.ctPlaySound, path, channel, fade,
                          content->soundVolume(), loops);
        }
        case types::CType::Wait:
            return format(msgs.ctWait, content->wait());
        case types::CType::ElapseTime:
            return msgs.ctElapseTime.value;
        case types::CType::CallStart:
            return format(msgs.ctCallStart, start());
        case types::CType::CallPackage:
            return format(msgs.ctCallPackage, contentTextUseID<CIDKind::Package>(context, summ, content->packages(), "%s", content));
        case types::CType::BranchFlag:
            return format(msgs.ctBranchFlag, flag());
        case types::CType::BranchMultiStep:
            return format(msgs.ctBranchMultiStep, step());
        case types::CType::BranchStep: {
            const auto found = flag::findVar<flag::Step>(summ ? summ->flagDirRoot() : nullptr, content->useCounter(), content->step());
            const auto value = found ? getStepValue(prop, found.get(), content->stepValue())
                : structs::parseDollarParams(prop->var().stepValueName.value, {{'N', std::to_string(content->stepValue())}});
            return format(msgs.ctBranchStep, step(), value);
        }
        // Dのdutils.contentTextの詳細は、コンテントの意
        // 味上の引数。汎用の代替が使うユーザー指定のコンテント名ではない。
        case types::CType::BranchSelect: {
            String method;
            switch (content->selectionMethod()) {
            case types::SelectionMethod::Manual: method = msgs.ctBranchSelectManual.value; break;
            case types::SelectionMethod::Random: method = msgs.ctBranchSelectAuto.value; break;
            case types::SelectionMethod::Valued: {
                String values = format(msgs.initialValue, content->initValue());
                for (const auto& coupon : content->coupons())
                    if (coupon) values += ", " + format(msgs.couponValues, coupon->name(), coupon->value());
                method = format(msgs.ctBranchSelectValued, values);
                break;
            }
            }
            return format(msgs.ctBranchSelect,
                (content->targetAll() ? msgs.ctBranchSelectAll : msgs.ctBranchSelectActive).value, method);
        }
        case types::CType::BranchAbility:
            return format(msgs.ctBranchAbility, msgs.targetName(content->targetS().m),
                (content->targetS().sleep ? msgs.sleepEnabled : msgs.sleepDisabled).value,
                msgs.physicalName(content->physical()), msgs.mentalName(content->mental()),
                (content->invertResult() ? msgs.abilityLowest : msgs.abilityHighest).value,
                content->signedLevel());
        case types::CType::BranchLevel:
            return format(msgs.ctBranchLevel,
                (content->average() ? msgs.ctBranchLevelAverage : msgs.ctBranchLevelSelected).value,
                content->unsignedLevel());
        case types::CType::BranchStatus:
            return format(msgs.ctBranchStatus,
                rangeName(prop, content->range(), content->holdingCoupon()),
                msgs.statusName(content->status()),
                (content->invertResult() ? msgs.haveNotForStatus : msgs.haveForStatus).value);
        case types::CType::BranchPartyNumber:
            return format(msgs.ctBranchPartyNumber, content->partyNumber());
        case types::CType::BranchCast:
            return contentTextUseID<CIDKind::Cast>(context, summ, content->casts(), msgs.ctBranchCast.value, content);
        case types::CType::BranchInfo:
            return contentTextUseID<CIDKind::Info>(context, summ, content->info(), msgs.ctBranchInfo.value, content);
        case types::CType::BranchMoney:
            return format(msgs.ctBranchMoney, content->money());
        case types::CType::BranchItem: {
            const auto name = contentTextUseID<CIDKind::Item>(context, summ, content->item(), "%s", content);
            if (content->range() == types::Range::SelectedCard)
                return format(msgs.ctBranchItemForSelectedCard, name,
                    (content->invertResult() ? msgs.notMatchCard : msgs.matchCard).value);
            return format(msgs.ctBranchItem, name,
                (content->invertResult() ? msgs.haveNot : msgs.have).value,
                rangeName(prop, content->range(), content->holdingCoupon()), content->cardNumber(),
                (content->selectCard() ? msgs.ctSelectFoundCard : msgs.noSelectFoundCard).value);
        }
        case types::CType::BranchSkill: {
            const auto name = contentTextUseID<CIDKind::Skill>(context, summ, content->skill(), "%s", content);
            if (content->range() == types::Range::SelectedCard)
                return format(msgs.ctBranchSkillForSelectedCard, name,
                    (content->invertResult() ? msgs.notMatchCard : msgs.matchCard).value);
            return format(msgs.ctBranchSkill, name,
                (content->invertResult() ? msgs.haveNot : msgs.have).value,
                rangeName(prop, content->range(), content->holdingCoupon()), content->cardNumber(),
                (content->selectCard() ? msgs.ctSelectFoundCard : msgs.noSelectFoundCard).value);
        }
        case types::CType::BranchBeast: {
            const auto name = contentTextUseID<CIDKind::Beast>(context, summ, content->beast(), "%s", content);
            if (content->range() == types::Range::SelectedCard)
                return format(msgs.ctBranchBeastForSelectedCard, name,
                    (content->invertResult() ? msgs.notMatchCard : msgs.matchCard).value);
            return format(msgs.ctBranchBeast, name,
                (content->invertResult() ? msgs.haveNot : msgs.have).value,
                rangeName(prop, content->range(), content->holdingCoupon()), content->cardNumber(),
                (content->selectCard() ? msgs.ctSelectFoundCard : msgs.noSelectFoundCard).value);
        }
        case types::CType::GetCast:
            return format(msgs.ctGetCast,
                contentTextUseID<CIDKind::Cast>(context, summ, content->casts(), "%s", content),
                msgs.startActionName(content->startAction()));
        case types::CType::LoseCast:
            return contentTextUseID<CIDKind::Cast>(context, summ, content->casts(), msgs.ctLoseCast.value, content);
        case types::CType::GetInfo:
            return contentTextUseID<CIDKind::Info>(context, summ, content->info(), msgs.ctGetInfo.value, content);
        case types::CType::LoseInfo:
            return contentTextUseID<CIDKind::Info>(context, summ, content->info(), msgs.ctLoseInfo.value, content);
        case types::CType::GetMoney:
            return format(msgs.ctGetMoney, content->money());
        case types::CType::LoseMoney:
            return format(msgs.ctLoseMoney, content->money());
        case types::CType::GetCoupon: {
            const auto name = content->coupon().empty() ? msgs.noSelectCoupon.value : content->coupon();
            const auto value = (content->couponValue() >= 0 ? "+" : "") + std::to_string(content->couponValue());
            return format(content->expandSPChars() ? msgs.ctGetCouponExpandSPChars : msgs.ctGetCoupon,
                          name, value, rangeName(prop, content->range(), content->holdingCoupon()));
        }
        case types::CType::LoseCoupon:
            return format(content->expandSPChars() ? msgs.ctLoseCouponExpandSPChars : msgs.ctLoseCoupon,
                content->coupon().empty() ? msgs.noSelectCoupon.value : content->coupon(),
                rangeName(prop, content->range(), content->holdingCoupon()));
        case types::CType::GetItem: {
            const auto name = contentTextUseID<CIDKind::Item>(context, summ, content->item(), "%s", content);
            if (content->range() == types::Range::SelectedCard)
                return format(msgs.ctGetItemForSelectedCard, name);
            return format(msgs.ctGetItem, name,
                rangeName(prop, content->range(), content->holdingCoupon()), content->cardNumber());
        }
        case types::CType::GetSkill: {
            const auto name = contentTextUseID<CIDKind::Skill>(context, summ, content->skill(), "%s", content);
            if (content->range() == types::Range::SelectedCard)
                return format(msgs.ctGetSkillForSelectedCard, name);
            return format(msgs.ctGetSkill, name,
                rangeName(prop, content->range(), content->holdingCoupon()), content->cardNumber());
        }
        case types::CType::GetBeast: {
            const auto name = contentTextUseID<CIDKind::Beast>(context, summ, content->beast(), "%s", content);
            if (content->range() == types::Range::SelectedCard)
                return format(msgs.ctGetBeastForSelectedCard, name);
            return format(msgs.ctGetBeast, name,
                rangeName(prop, content->range(), content->holdingCoupon()), content->cardNumber());
        }
        case types::CType::GetCompleteStamp:
            return format(msgs.ctGetCompleteStamp, content->completeStamp().empty() ? msgs.noSelectCompleteStamp.value : content->completeStamp());
        case types::CType::GetGossip:
            return format(content->expandSPChars() ? msgs.ctGetGossipExpandSPChars : msgs.ctGetGossip, content->gossip().empty() ? msgs.noSelectGossip.value : content->gossip());
        case types::CType::LoseItem: {
            if (content->range() == types::Range::SelectedCard) return msgs.ctLoseItemForSelectedCard.value;
            const auto name = contentTextUseID<CIDKind::Item>(context, summ, content->item(), "%s", content);
            const auto count = content->cardNumber() == 0 ? msgs.ctLoseCardAll.value
                : format(msgs.ctLoseCardCount, content->cardNumber());
            return format(msgs.ctLoseItem, name,
                rangeName(prop, content->range(), content->holdingCoupon()), count);
        }
        case types::CType::LoseSkill: {
            if (content->range() == types::Range::SelectedCard) return msgs.ctLoseSkillForSelectedCard.value;
            const auto name = contentTextUseID<CIDKind::Skill>(context, summ, content->skill(), "%s", content);
            const auto count = content->cardNumber() == 0 ? msgs.ctLoseCardAll.value
                : format(msgs.ctLoseCardCount, content->cardNumber());
            return format(msgs.ctLoseSkill, name,
                rangeName(prop, content->range(), content->holdingCoupon()), count);
        }
        case types::CType::LoseBeast: {
            if (content->range() == types::Range::SelectedCard) return msgs.ctLoseBeastForSelectedCard.value;
            const auto name = contentTextUseID<CIDKind::Beast>(context, summ, content->beast(), "%s", content);
            const auto count = content->cardNumber() == 0 ? msgs.ctLoseCardAll.value
                : format(msgs.ctLoseCardCount, content->cardNumber());
            return format(msgs.ctLoseBeast, name,
                rangeName(prop, content->range(), content->holdingCoupon()), count);
        }
        case types::CType::LoseCompleteStamp:
            return format(msgs.ctLoseCompleteStamp, content->completeStamp().empty() ? msgs.noSelectCompleteStamp.value : content->completeStamp());
        case types::CType::LoseGossip:
            return format(content->expandSPChars() ? msgs.ctLoseGossipExpandSPChars : msgs.ctLoseGossip, content->gossip().empty() ? msgs.noSelectGossip.value : content->gossip());
        case types::CType::ShowParty:
            return format(msgs.ctShowParty, cardSpeedAttrText(context, content));
        case types::CType::HideParty:
            return format(msgs.ctHideParty, cardSpeedAttrText(context, content));
        case types::CType::Redisplay:
            return content->transition() == types::Transition::Default ? msgs.ctRedisplayClassic.value
                : format(msgs.ctRedisplayWithTransition, transitionText(prop, content->transition(), content->transitionSpeed()));
        case types::CType::BranchStepCmp:
            return format(msgs.ctBranchStepCmp, step(), variable(content->step2(), 1));
        case types::CType::BranchFlagCmp:
            return format(msgs.ctBranchFlagCmp, flag(), variable(content->flag2(), 0));
        case types::CType::BranchRound:
            return format(msgs.ctBranchRound, content->round(), msgs.comparison3Name(content->comparison3()));
        case types::CType::BranchMultiCoupon:
            return format(content->expandSPChars() ? msgs.ctBranchMultiCouponExpandSPChars : msgs.ctBranchMultiCoupon,
                rangeName(prop, content->range(), content->holdingCoupon()));
        case types::CType::BranchMultiRandom:
            return msgs.ctBranchMultiRandom.value;
        case types::CType::BranchRandomSelect: {
            const auto range = castRangesName(prop, content->castRange());
            const bool level = content->levelMax() > 0;
            const bool status = content->status() != types::Status::None;
            if (!level && !status) return format(msgs.ctRandomSelectN, range);
            const auto name = msgs.statusName(content->status());
            const auto condition = level && status
                ? format(msgs.randomSelectCondition3, content->levelMin(), content->levelMax(), name)
                : level ? format(msgs.randomSelectCondition1, content->levelMin(), content->levelMax())
                        : format(msgs.randomSelectCondition2, name);
            return format(content->invertResult() ? msgs.ctRandomSelectInvert : msgs.ctRandomSelect, range, condition);
        }
        case types::CType::BranchKeyCode: {
            const auto name = content->keyCode().empty() ? msgs.noKeyCode.value : content->keyCode();
            const bool selected = content->keyCodeRange() == types::Range::SelectedCard;
            const auto result = selected
                ? (content->invertResult() ? msgs.notExists : msgs.exists).value
                : (content->invertResult() ? msgs.haveNot : msgs.have).value;
            const auto condition = (content->matchingCondition() == types::MatchingCondition::Has
                ? msgs.contains : msgs.notContains).value;
            const auto select = (content->selectCard() ? msgs.ctSelectFoundCard : msgs.noSelectFoundCard).value;
            const auto range = rangeName(prop, content->keyCodeRange(), content->holdingCoupon());
            if (content->targetIsSkill() && content->targetIsItem() && content->targetIsBeast() && content->targetIsHand())
                return selected ? format(msgs.ctBranchKeyCodeWithConditionAllTypeForSelectedCard, name, condition, result)
                    : format(msgs.ctBranchKeyCodeWithConditionAllType, name, condition, result, range, select);
            String targets;
            const auto add = [&](const String& target) {
                if (!targets.empty()) targets += msgs.targetSeparator.value;
                targets += target;
            };
            if (content->targetIsSkill()) add(msgs.targetIsSkill.value);
            if (content->targetIsItem()) add(msgs.targetIsItem.value);
            if (content->targetIsBeast()) add(msgs.targetIsBeast.value);
            if (content->targetIsHand()) add(msgs.targetIsHand.value);
            if (targets.empty()) targets = msgs.noSelectTarget.value;
            return selected ? format(msgs.ctBranchKeyCodeWithConditionForSelectedCard, name, condition, targets, result)
                : format(msgs.ctBranchKeyCodeWithCondition, name, condition, targets, result, range, select);
        }
        case types::CType::CheckStep: {
            const auto found = flag::findVar<flag::Step>(summ ? summ->flagDirRoot() : nullptr,
                content->useCounter(), content->step());
            const auto value = found ? getStepValue(prop, found.get(), content->stepValue())
                : structs::parseDollarParams(prop->var().stepValueName.value, {{'N', std::to_string(content->stepValue())}});
            return format(msgs.ctCheckStep, step(), value, msgs.comparison4Name(content->comparison4()));
        }
        case types::CType::SubstituteStep: {
            const auto destination = variable(content->step2(), 1);
            const auto found = flag::findVar<flag::Step>(summ ? summ->flagDirRoot() : nullptr,
                content->useCounter(), content->step());
            const auto source = utils::caseFold(content->step());
            if (!found && source == utils::caseFold(prop->sys().randomValue()))
                return format(msgs.ctSubstituteStepFromRandom, destination);
            if (!found && source == utils::caseFold(prop->sys().selectedPlayerCardNumber()))
                return format(msgs.ctSubstituteStepFromSelectedPlayer, destination);
            return format(msgs.ctSubstituteStep, step(), destination);
        }
        case types::CType::SubstituteFlag: {
            const auto destination = variable(content->flag2(), 0);
            const auto found = flag::findVar<flag::Flag>(summ ? summ->flagDirRoot() : nullptr,
                content->useCounter(), content->flag());
            if (summ && !found && utils::caseFold(content->flag()) == utils::caseFold(prop->sys().randomValue()))
                return format(msgs.ctSubstituteFlagFromRandom, destination, cardSpeedAttrText(context, content));
            return format(msgs.ctSubstituteFlag, flag(), destination, cardSpeedAttrText(context, content));
        }
        case types::CType::ChangeEnvironment: {
            String values;
            const auto add = [&](const String& value) { if (!values.empty()) values += " "; values += value; };
            if (content->backpackEnabled() != types::EnvironmentStatus::NotSet)
                add(format(msgs.ctEnvironmentEnabled, msgs.backpack.value, msgs.environmentStatusName(content->backpackEnabled())));
            if (content->gameOverEnabled() != types::EnvironmentStatus::NotSet)
                add(format(msgs.ctEnvironmentEnabled, msgs.gameOver.value, msgs.environmentStatusNameEnabled(content->gameOverEnabled())));
            if (content->runAwayEnabled() != types::EnvironmentStatus::NotSet)
                add(format(msgs.ctEnvironmentEnabled, msgs.runAway.value, msgs.environmentStatusNameEnabled(content->runAwayEnabled())));
            return values.empty() ? msgs.ctChangeEnvironmentNotSet.value : format(msgs.ctChangeEnvironment, values);
        }
        case types::CType::MoveCard: {
            const auto group = content->cardGroup().empty() ? msgs.noSelectCardGroup.value : content->cardGroup();
            const auto scale = content->scale() == -1 ? msgs.noChangeScale.value : std::to_string(content->scale()) + "%";
            const auto layer = content->layer() == -1 ? msgs.noChangeLayer.value : std::to_string(content->layer());
            if (content->positionType() == types::CoordinateType::None)
                return format(msgs.ctMoveCardNoSet, group, scale, layer, cardSpeedAttrText(context, content));
            return format(msgs.ctMoveCard, group, msgs.coordinateTypeName(content->positionType()),
                content->x(), content->y(), scale, layer, cardSpeedAttrText(context, content));
        }
        case types::CType::MoveBgImage: {
            const auto cell = content->cellName().empty() ? msgs.noSelectCellName.value : content->cellName();
            const bool position = content->positionType() != types::CoordinateType::None;
            const bool size = content->sizeType() != types::CoordinateType::None;
            const auto ptype = msgs.coordinateTypeName(content->positionType());
            const auto stype = msgs.coordinateTypeName(content->sizeType());
            const bool classic = content->transition() == types::Transition::Default;
            const auto transition = classic ? String{} : transitionText(prop, content->transition(), content->transitionSpeed());
            if (position && size)
                return classic ? format(msgs.ctMoveAndResizeBgImageClassic, cell, ptype, content->x(), content->y(), stype, content->width(), content->height())
                    : format(msgs.ctMoveAndResizeBgImageWithTransition, cell, ptype, content->x(), content->y(), stype, content->width(), content->height(), transition);
            if (position)
                return classic ? format(msgs.ctMoveBgImageClassic, cell, ptype, content->x(), content->y())
                    : format(msgs.ctMoveBgImageWithTransition, cell, ptype, content->x(), content->y(), transition);
            if (size)
                return classic ? format(msgs.ctResizeBgImageClassic, cell, stype, content->width(), content->height())
                    : format(msgs.ctResizeBgImageWithTransition, cell, stype, content->width(), content->height(), transition);
            return format(msgs.ctMoveBgImageNoSet, cell);
        }
        case types::CType::ReplaceBgImage: {
            const auto cell = content->cellName().empty() ? msgs.noSelectCellName.value : content->cellName();
            return content->transition() == types::Transition::Default
                ? format(msgs.ctReplaceBgImageClassic, cell, backgrounds())
                : format(msgs.ctReplaceBgImageWithTransition, cell, backgrounds(), transitionText(prop, content->transition(), content->transitionSpeed()));
        }
        case types::CType::LoseBgImage: {
            const auto cell = content->cellName().empty() ? msgs.noSelectCellName.value : content->cellName();
            return content->transition() == types::Transition::Default ? format(msgs.ctLoseBgImageClassic, cell)
                : format(msgs.ctLoseBgImageWithTransition, cell, transitionText(prop, content->transition(), content->transitionSpeed()));
        }
        case types::CType::CustomXmlContent: {
            const auto type = content->xmlType().empty() ? msgs.contentNameCustomXmlContent.value : content->xmlType();
            const auto xml = utils::singleLine(content->xmlStr());
            return xml.empty() ? type : type + ": " + xml;
        }
        case types::CType::BranchRandom:
            return format(msgs.ctBranchRandom, content->percent());
        case types::CType::BranchArea:
            return msgs.ctBranchArea.value;
        case types::CType::BranchBattle:
            return msgs.ctBranchBattle.value;
        case types::CType::BranchIsBattle:
            return msgs.ctBranchIsBattle.value;
        case types::CType::BranchCoupon: {
            const auto& names = content->couponNames();
            const auto result = content->invertResult() ? msgs.haveNot.value : msgs.have.value;
            const auto range = rangeName(prop, content->range(), content->holdingCoupon());
            if (names.size() < 2U) {
                const auto name = names.empty() || names.front().empty() ? msgs.noSelectCoupon.value : names.front();
                return format(content->expandSPChars() ? msgs.ctBranchCouponExpandSPChars : msgs.ctBranchCoupon,
                              name, result, range);
            }
            String formattedNames;
            bool first = true;
            for (const auto& name : names) {
                if (!first) formattedNames += msgs.couponNamesSeparator.value;
                first = false;
                formattedNames += format(msgs.couponNames, name);
            }
            return format(content->expandSPChars() ? msgs.ctBranchCouponMultiExpandSPChars : msgs.ctBranchCouponMulti,
                          formattedNames, content->matchingType() == types::MatchingType::Or ? msgs.matchingTypeOr.value : msgs.matchingTypeAnd.value,
                          result, range);
        }
        case types::CType::BranchCompleteStamp:
            return format(msgs.ctBranchCompleteStamp, content->completeStamp().empty()
                          ? msgs.noSelectCompleteStamp.value : content->completeStamp());
        case types::CType::BranchGossip:
            return format(content->expandSPChars() ? msgs.ctBranchGossipExpandSPChars : msgs.ctBranchGossip,
                          content->gossip().empty() ? msgs.noSelectGossip.value : content->gossip());
        case types::CType::SetFlag: {
            const auto found = flag::findVar<flag::Flag>(summ ? summ->flagDirRoot() : nullptr, content->useCounter(), content->flag());
            const auto value = content->flagValue() ? (found ? found->on() : msgs.flagOn.value) : (found ? found->off() : msgs.flagOff.value);
            return format(msgs.ctSetFlag, flag(), value, cardSpeedAttrText(context, content));
        }
        case types::CType::SetStep: {
            const auto found = flag::findVar<flag::Step>(summ ? summ->flagDirRoot() : nullptr, content->useCounter(), content->step());
            const auto value = found ? getStepValue(prop, found.get(), content->stepValue())
                : structs::parseDollarParams(prop->var().stepValueName.value, {{'N', std::to_string(content->stepValue())}});
            return format(msgs.ctSetStep, step(), value);
        }
        case types::CType::SetStepUp:
            return format(msgs.ctSetStepUp, step());
        case types::CType::SetStepDown:
            return format(msgs.ctSetStepDown, step());
        case types::CType::ReverseFlag:
            return format(msgs.ctReverseFlag, flag(), cardSpeedAttrText(context, content));
        case types::CType::CheckFlag: {
            const auto found = flag::findVar<flag::Flag>(summ ? summ->flagDirRoot() : nullptr, content->useCounter(), content->flag());
            return format(msgs.ctCheckFlag, flag(), found ? found->on() : msgs.flagOn.value);
        }
        case types::CType::BranchVariant:
            return format(msgs.ctBranchVariant, content->expression().empty() ? msgs.noExpression.value : exprStr(content->expression()));
        case types::CType::SetVariant: {
            const auto name = !content->step().empty() ? format(msgs.ctStepName, step())
                : !content->flag().empty() ? format(msgs.ctFlagName, flag()) : format(msgs.ctVariantName, variant());
            return format(msgs.ctSetVariant, name, content->expression().empty() ? msgs.noExpression.value : exprStr(content->expression()));
        }
        case types::CType::CheckVariant:
            return format(msgs.ctCheckVariant, content->expression().empty() ? msgs.noExpression.value : exprStr(content->expression()));
        default:
            return fallback();
        }
    } catch (...) {
        return fallback();
    }
}

String commentText(const CWXPath* object, const bool eventView) {
    if (!object) return {};
    if (eventView) {
        if (const auto owner = dynamic_cast<const event::EventTreeOwner*>(object))
            return owner->commentForEvents();
    }
    if (const auto commentable = dynamic_cast<const path::Commentable*>(object))
        return commentable->comment();
    return {};
}

}  // namespace cwxeditor::cwx::editor::gui::dwt::dutils
