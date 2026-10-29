#pragma once

// cwx/editor/gui/dwt/autokeycode.d
// の移植可能なC++20実装。 元のSWT
// の表をlib::TableViewで
// 表し、ハンドルを公開せず、 同じ編
// 集値・XML表現・コールバックをネ
// イティブ・ヘッドレスの実装へ渡す。

#include "cwxeditor_src/core/cwx/motion.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/editablelistview.hpp"
#include "cwxeditor_src/core/cwx/structs.hpp"
#include "cwxeditor_src/core/cwx/types.hpp"
#include "cwxeditor_src/core/cwx/utils.hpp"
#include "cwxeditor_src/core/cwx/xml.hpp"
#include "cwxeditor_src/lib/components.hpp"
#include "cwxeditor_src/lib/events.hpp"
#include "cwxeditor_src/lib/portable.hpp"
#include "cwxeditor_src/lib/services.hpp"
#include "cwxeditor_src/lib/text.hpp"
#include "cwxeditor_src/lib/ui_actions.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cwxeditor::cwx::editor::gui::dwt::autokeycode {

using String = ::cwxeditor::lib::String;
using StringView = ::cwxeditor::lib::StringView;
using XNode = ::cwxeditor::cwx::xml::XNode;
using KeyCodeByFeature = ::cwxeditor::cwx::structs::KeyCodeByFeature;
using KeyCodeByMotion = ::cwxeditor::cwx::structs::KeyCodeByMotion;
using ElementOverride = ::cwxeditor::cwx::structs::ElementOverride;
using Element = ::cwxeditor::cwx::types::Element;
using MType = ::cwxeditor::cwx::types::MType;
using Services = ::cwxeditor::lib::Services;
using Parent = ::cwxeditor::lib::Container*;
using StandardKeyCodes = ::cwxeditor::lib::Callback<::cwxeditor::lib::Array<String>()>;
using ElementOverrides = ::cwxeditor::lib::Callback<::cwxeditor::lib::Array<ElementOverride>()>;
using CatchModifier = ::cwxeditor::lib::Callback<bool()>;
using ModCallback = ::cwxeditor::lib::Action;
using SelectionCallback =
    ::cwxeditor::lib::Callback<void(const ::cwxeditor::lib::Array<std::size_t>&)>;
using TableView = ::cwxeditor::lib::TableView;
using ComboBox = ::cwxeditor::lib::ComboBox;
using TextEdit = ::cwxeditor::lib::TextEdit;

/// このモジュールが使うCommons/Propsの一部
/// に対応する共通型。 SWT/DWT型をコアへ公開せず
/// 、翻訳ラベル・アイコン・バックエンドを指定できる。
struct Messages {
    String cardFeature{"カード機能"};
    String keyCode{"キーコード"};
    String motion{"モーション"};
    String motionElement{"モーション要素"};
    String noElement{"要素なし"};
    ::cwxeditor::lib::ResourceId cards{};
    ::cwxeditor::lib::ResourceId keyCodeImage{};
    ::cwxeditor::lib::ResourceId emptyIcon{};
    ::cwxeditor::lib::Callback<String(MType)> motionName;
    ::cwxeditor::lib::Callback<String(Element)> elementName;
    ::cwxeditor::lib::Callback<::cwxeditor::lib::ResourceId(MType)> motionIcon;
    ::cwxeditor::lib::Callback<::cwxeditor::lib::ResourceId(Element)> elementIcon;

    [[nodiscard]] String motionLabel(MType value) const;
    [[nodiscard]] String elementLabel(Element value) const;
    [[nodiscard]] ::cwxeditor::lib::ResourceId motionImage(MType value) const;
    [[nodiscard]] ::cwxeditor::lib::ResourceId elementImage(Element value) const;
};

struct Commons {
    Messages messages{};
    editablelistview::Commons listCommons{};
    ::cwxeditor::lib::Callback<::cwxeditor::lib::ResourceId(const ElementOverride&)> overrideImage;
    Services services{};
    std::function<std::shared_ptr<void>(const std::shared_ptr<TextEdit>&,
        const std::shared_ptr<ComboBox>&, StandardKeyCodes, CatchModifier,
        std::function<bool()>)> setupKeyCodeEditor;

    ModCallback refreshToolBar;
    float featureColumnWidth{240.0F};
    float featureKeyCodeColumnWidth{220.0F};
    float motionColumnWidth{220.0F};
    float motionElementColumnWidth{180.0F};
    float motionKeyCodeColumnWidth{220.0F};
    std::function<void(std::size_t, float)> storeFeatureColumn;
    std::function<void(std::size_t, float)> storeMotionColumn;

    Commons() = default;
    explicit Commons(Messages value) : messages(std::move(value)) {}
    Commons(Messages value, Services service, ModCallback refresh = {})
        : messages(std::move(value)), services(std::move(service)),
          refreshToolBar(std::move(refresh)) {}
};

/// SWT.READ_ONLYに対応する共通のスタイルビ
/// ット。 モデルはその他のスタイルビットを無視する。
inline constexpr int READ_ONLY_STYLE = 1;

namespace detail {

template <typename Component>
[[nodiscard]] std::shared_ptr<Component> createComponent(
    const Services& services, ::cwxeditor::lib::ComponentKind kind) {
    if (services.gui) {
        auto candidate = services.gui->create_component(kind);
        if (auto result = std::dynamic_pointer_cast<Component>(candidate)) return result;
    }
    return std::make_shared<Component>();
}

[[nodiscard]] inline bool containsFolded(StringView text, StringView needle) {
    if (needle.empty()) return true;
    const auto folded_text = ::cwxeditor::cwx::utils::caseFold(text);
    const auto folded_needle = ::cwxeditor::cwx::utils::caseFold(needle);
    return folded_text.find(folded_needle) != String::npos;
}

// DのビューはAbstractEditableListViewを継承する。 どの呼び出し元・
// ビルド・バックエンドでも、履歴・選択・クリップボード・メニュー・ドラッグの実装を共有する。
inline editablelistview::Commons listCommons(const Commons& comm) {
    auto list=comm.listCommons;
    list.services=comm.services;
    list.refreshToolBar=comm.refreshToolBar;
    return list;
}

}  // namespace detail

class KeyCodeByFeatureView final
    : public editablelistview::AbstractEditableListView<KeyCodeByFeature> {
public:
    inline static constexpr StringView XML_NAME = "keyCodeByFeature";
    inline static constexpr StringView XML_CONTAINER_NAME = "keyCodesByFeatures";

    KeyCodeByFeatureView(Commons comm, Parent parent, int style,
                         StandardKeyCodes standardKeyCodes = {},
                         CatchModifier catchModifier = {});
    ~KeyCodeByFeatureView() override;

    [[nodiscard]] const Commons& commons() const noexcept { return commons_; }
    void addModificationListener(ModCallback callback) { modEvent.push_back(std::move(callback)); }
    void select(std::size_t index) { setSelection({index}); }
    void clearSelection() { setSelection({}); }
    bool removeIndices(::cwxeditor::lib::Array<std::size_t> indices) { return delImpl(std::move(indices)); }
    bool removeSelected() { return del(); }
    bool moveUp() { return up(); }
    bool moveDown() { return down(); }
    [[nodiscard]] const std::shared_ptr<::cwxeditor::lib::Panel>& panel() const noexcept { return rootWidget(); }
    [[nodiscard]] const StandardKeyCodes& standardKeyCodes() const noexcept {
        return standard_key_codes_;
    }
    [[nodiscard]] bool catchModifier() const;
    [[nodiscard]] std::shared_ptr<TextEdit> createFeatureEditor(std::size_t index);
    [[nodiscard]] std::shared_ptr<ComboBox> createKeyCodeEditor(std::size_t index);
    [[nodiscard]] ::cwxeditor::lib::Array<String> keyCodeCandidates(
        StringView filter = {}) const;

    bool featureEditEnd(std::size_t index, String newText);
    bool keyCodeEditEnd(std::size_t index, String newText);
    bool append(const KeyCodeByFeature& value, std::size_t index = static_cast<std::size_t>(-1));
    [[nodiscard]] bool isValidValue(const KeyCodeByFeature& value) const {
        return !value.feature.empty();
    }

    [[nodiscard]] String xmlName() const noexcept { return String(XML_CONTAINER_NAME); }
    [[nodiscard]] ::cwxeditor::lib::Array<KeyCodeByFeature> createFromNode(
        const XNode& node);
    [[nodiscard]] XNode toNode(std::span<const std::size_t> indices) const;
    [[nodiscard]] XNode toNode(const ::cwxeditor::lib::Array<std::size_t>& indices) const {
        return toNode(std::span<const std::size_t>(indices));
    }
    [[nodiscard]] XNode toNode() const;

    void initColumns();
    void initEditors();
    void updateItem(const KeyCodeByFeature& value, std::size_t index);
    [[nodiscard]] std::vector<String> editorFeatures() const;

    [[nodiscard]] const std::shared_ptr<TableView>& widget() const noexcept { return table(); }

private:
    [[nodiscard]] ::cwxeditor::lib::TableRow prepareItem(const KeyCodeByFeature& value) const;
    void initColumns(TableView&) override { initColumns(); }
    void initEditors(TableView&) override { initEditors(); }
    bool nativeInlineEditing() const noexcept override { return true; }
    void cellEdit(std::size_t row, std::size_t column, String text) override;
    void updateItem(const KeyCodeByFeature& value, ::cwxeditor::lib::TableRow& row) override { row=prepareItem(value); }
    void rememberKeyCode(StringView value);

    Commons commons_{};
    Parent parent_{};
    StandardKeyCodes standard_key_codes_{};
    CatchModifier catch_modifier_{};
    std::vector<String> key_code_history_;
    std::shared_ptr<void> inline_key_code_binding_;
};

class KeyCodeByMotionView final
    : public editablelistview::AbstractEditableListView<KeyCodeByMotion> {
public:
    inline static constexpr StringView XML_NAME = "keyCodeByMotion";
    inline static constexpr StringView XML_CONTAINER_NAME = "keyCodesByMotions";

    KeyCodeByMotionView(Commons comm, Parent parent, int style,
                        StandardKeyCodes standardKeyCodes = {},
                        ElementOverrides elementOverrides = {},
                        CatchModifier catchModifier = {});
    ~KeyCodeByMotionView() override;

    [[nodiscard]] const Commons& commons() const noexcept { return commons_; }
    void addModificationListener(ModCallback callback) { modEvent.push_back(std::move(callback)); }
    void select(std::size_t index) { setSelection({index}); }
    void clearSelection() { setSelection({}); }
    bool removeIndices(::cwxeditor::lib::Array<std::size_t> indices) { return delImpl(std::move(indices)); }
    bool removeSelected() { return del(); }
    bool moveUp() { return up(); }
    bool moveDown() { return down(); }
    [[nodiscard]] const std::shared_ptr<::cwxeditor::lib::Panel>& panel() const noexcept { return rootWidget(); }
    [[nodiscard]] const ElementOverrides& elementOverrides() const noexcept {
        return element_overrides_;
    }
    [[nodiscard]] bool catchModifier() const;
    [[nodiscard]] std::shared_ptr<ComboBox> createMotionEditor(std::size_t index);
    [[nodiscard]] std::shared_ptr<ComboBox> createElementEditor(std::size_t index);
    [[nodiscard]] std::shared_ptr<ComboBox> createKeyCodeEditor(std::size_t index);
    [[nodiscard]] ::cwxeditor::lib::Array<String> keyCodeCandidates(
        StringView filter = {}) const;
    [[nodiscard]] std::vector<String> motionChoices(std::size_t index) const;
    [[nodiscard]] std::vector<String> elementChoices(std::size_t index) const;

    bool motionEditEnd(std::size_t index, std::size_t choice);
    bool elementEditEnd(std::size_t index, std::size_t choice);
    bool keyCodeEditEnd(std::size_t index, String newText);
    bool append(const KeyCodeByMotion& value, std::size_t index = static_cast<std::size_t>(-1));

    [[nodiscard]] String xmlName() const noexcept { return String(XML_CONTAINER_NAME); }
    [[nodiscard]] ::cwxeditor::lib::Array<KeyCodeByMotion> createFromNode(
        const XNode& node);
    [[nodiscard]] XNode toNode(std::span<const std::size_t> indices) const;
    [[nodiscard]] XNode toNode(const ::cwxeditor::lib::Array<std::size_t>& indices) const {
        return toNode(std::span<const std::size_t>(indices));
    }
    [[nodiscard]] XNode toNode() const;

    void initColumns();
    void initEditors();
    void updateItem(const KeyCodeByMotion& value, std::size_t index);

    struct ElementPresentation {
        ::cwxeditor::lib::ResourceId image{};
        String path;
        String name;
        String targetType;
    };
    [[nodiscard]] ElementPresentation elementPresentation(Element value) const;

    [[nodiscard]] const std::shared_ptr<TableView>& widget() const noexcept { return table(); }

private:
    [[nodiscard]] ::cwxeditor::lib::TableRow prepareItem(const KeyCodeByMotion& value) const;
    void initColumns(TableView&) override { initColumns(); }
    void initEditors(TableView&) override { initEditors(); }
    bool nativeInlineEditing() const noexcept override { return true; }
    void cellEdit(std::size_t row, std::size_t column, String text) override;
    void updateItem(const KeyCodeByMotion& value, ::cwxeditor::lib::TableRow& row) override { row=prepareItem(value); }
    void rememberKeyCode(StringView value);
    [[nodiscard]] ::cwxeditor::lib::Array<ElementOverride> currentElementOverrides() const;

    Commons commons_{};
    Parent parent_{};
    StandardKeyCodes standard_key_codes_{};
    ElementOverrides element_overrides_{};
    CatchModifier catch_modifier_{};
    std::vector<String> key_code_history_;
    std::shared_ptr<void> inline_key_code_binding_;
};

}  // namespace cwxeditor::cwx::editor::gui::dwt::autokeycode
