#include "cwxeditor_src/gui/cwx/editor/gui/dwt/autokeycode.hpp"
#include "cwxeditor_src/core/cwx/perf.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace cwxeditor::cwx::editor::gui::dwt::autokeycode {
namespace {

using ::cwxeditor::lib::ChoiceItem;
using ::cwxeditor::lib::ComponentKind;
using ::cwxeditor::lib::ResourceId;

// 編集可能なキーコード欄は、文字入力後に候補を再
// 構築する。共通の準備済み状態の公開経路を使う
// 。再入したリスナーが候補配列の完成前に編集部
// や値を変更した場合、古い候補で上書きしない。
void publishKeyCodeCandidates(const std::shared_ptr<ComboBox>& editor,
                              std::vector<ChoiceItem> items,
                              String text) {
    if (!editor) return;
    const auto selected = editor->selected_index;
    const std::weak_ptr<ComboBox> weak = editor;
    (void)::cwxeditor::lib::replace_combo_items(editor, std::move(items), selected,
        std::move(text), [weak] { return !weak.expired(); });
}

// DのEnumMembers!MType/Elementは安
// 定した宣言順の範囲。範囲を明示し、呼び出し箇所で列挙値の
// 連続性に依存せず、保存する選択インデックスも安定させる。
constexpr std::array<MType, 41> motion_types{
    MType::Heal,
    MType::Damage,
    MType::Absorb,
    MType::Paralyze,
    MType::DisParalyze,
    MType::Poison,
    MType::DisPoison,
    MType::GetSkillPower,
    MType::LoseSkillPower,
    MType::Sleep,
    MType::Confuse,
    MType::Overheat,
    MType::Brave,
    MType::Panic,
    MType::Normal,
    MType::Bind,
    MType::DisBind,
    MType::Silence,
    MType::DisSilence,
    MType::FaceUp,
    MType::FaceDown,
    MType::AntiMagic,
    MType::DisAntiMagic,
    MType::EnhanceAction,
    MType::EnhanceAvoid,
    MType::EnhanceResist,
    MType::EnhanceDefense,
    MType::VanishTarget,
    MType::VanishCard,
    MType::VanishBeast,
    MType::DealAttackCard,
    MType::DealPowerfulAttackCard,
    MType::DealCriticalAttackCard,
    MType::DealFeintCard,
    MType::DealDefenseCard,
    MType::DealDistanceCard,
    MType::DealConfuseCard,
    MType::DealSkillCard,
    MType::SummonBeast,
    MType::CancelAction,
    MType::NoEffect,
};

constexpr std::array<Element, 7> elements{
    Element::All,
    Element::Health,
    Element::Mind,
    Element::Miracle,
    Element::Magic,
    Element::Fire,
    Element::Ice,
};

template <typename T>
void appendUnique(std::vector<T>& destination, const T& value) {
    if (std::find(destination.begin(), destination.end(), value) == destination.end())
        destination.push_back(value);
}

std::vector<ChoiceItem> choiceItems(const std::vector<String>& labels) {
    std::vector<ChoiceItem> result;
    result.reserve(labels.size());
    for (const auto& label : labels) result.push_back(ChoiceItem{label, label});
    return result;
}

std::vector<String> motionLabels(Messages messages) {
    std::vector<String> labels;
    for(const auto type:motion_types)labels.push_back(messages.motionLabel(type));
    return labels;
}
std::vector<String> elementLabels(Messages messages,const ::cwxeditor::lib::Array<ElementOverride>& overrides) {
    std::vector<String> labels{messages.noElement};
    for(const auto element:elements) {
        String name;
        for(const auto& item:overrides)if(item.element==element)name=item.name;
        labels.push_back(name.empty()?messages.elementLabel(element):name);
    }
    return labels;
}
KeyCodeByMotionView::ElementPresentation resolvedPresentation(Commons comm,
    const ::cwxeditor::lib::Array<ElementOverride>& overrides,Element element) {
    KeyCodeByMotionView::ElementPresentation result;
    std::optional<ElementOverride> selected;
    for(const auto& item:overrides)if(item.element==element)selected=item;
    if(selected) {
        result.name=selected->name;result.path=selected->icon;result.targetType=selected->targetType;
        if(comm.overrideImage)result.image=comm.overrideImage(*selected);
    }
    if(result.name.empty())result.name=comm.messages.elementLabel(element);
    if(!result.image)result.image=comm.messages.elementImage(element);
    return result;
}
template<class Value>
std::vector<String> codesForRows(::cwxeditor::lib::Array<Value> values,std::vector<String> history,StandardKeyCodes standard) {
    std::vector<String> result;
    if(standard)for(const auto& code:standard())if(!code.empty())appendUnique(result,code);
    for(const auto& code:history)if(!code.empty())appendUnique(result,code);
    for(const auto& row:values)if(!row.keyCode.empty())appendUnique(result,row.keyCode);
    return result;
}
}  // namespace

String Messages::motionLabel(MType value) const {
    return motionName ? motionName(value) : motion::mTypeToName(value);
}

String Messages::elementLabel(Element value) const {
    return elementName ? elementName(value) : types::fromElement(value);
}

ResourceId Messages::motionImage(MType value) const {
    if (motionIcon) return motionIcon(value);
    return static_cast<ResourceId>(static_cast<std::int32_t>(value) + 1);
}

ResourceId Messages::elementImage(Element value) const {
    if (elementIcon) return elementIcon(value);
    return static_cast<ResourceId>(static_cast<std::int32_t>(value) + 1);
}

KeyCodeByFeatureView::KeyCodeByFeatureView(Commons comm, Parent parent, int style,
                                           StandardKeyCodes standardKeyCodes,
                                           CatchModifier catchModifier)
    : editablelistview::AbstractEditableListView<KeyCodeByFeature>(detail::listCommons(comm),parent,style,true),
      commons_(std::move(comm)),
      parent_(parent),
      standard_key_codes_(std::move(standardKeyCodes)),
      catch_modifier_(std::move(catchModifier)) {
    // 派生側のプロバイダーを保存してから初期化する。共通基底が通常のツー
    // ルバー・コンテキストメニューと、追加待ちのトランザクションを作る。
    (void)table();
}

KeyCodeByFeatureView::~KeyCodeByFeatureView() {
    retireCallbacks();
    if (table()) {
        table()->on_selection_changed = {};
        table()->on_cell_activate = {};
        table()->on_cell_edited = {};
        table()->on_inline_editor_created = {};
        table()->on_column_resized = {};
        table()->on_column_resized = {};
    }
}

bool KeyCodeByFeatureView::catchModifier() const {
    return catch_modifier_ ? catch_modifier_() : false;
}

std::shared_ptr<TextEdit> KeyCodeByFeatureView::createFeatureEditor(std::size_t index) {
    const auto life=weakLifetime();
    const String initial = index < valueCount() ? value(index).feature : String{};
    auto editor = detail::createComponent<TextEdit>(commons_.services, ComponentKind::TextEdit);
    editor->text = initial;
    if(life.expired())return editor;
    editor->read_only = readOnly() || !enabled();
    editor->on_changed = [this, index,life](StringView text) {
        if(life.expired())return;
        featureEditEnd(index, String(text));
    };
    return editor;
}

std::shared_ptr<ComboBox> KeyCodeByFeatureView::createKeyCodeEditor(std::size_t index) {
    const auto life=weakLifetime();
    const String initial = index < valueCount() ? value(index).keyCode : String{};
    auto editor = detail::createComponent<ComboBox>(commons_.services, ComponentKind::ComboBox);
    editor->editable = true;
    editor->edit_text = initial;
    // 初期値は逐次検索の問い合わせではない。D
    // は標準・履歴の全キーを表示し、現在値を
    // 先に消さなくても選び直せるようにする。
    editor->items = choiceItems(keyCodeCandidates());
    if(life.expired())return editor;
    editor->set_enabled(!readOnly() && enabled());
    editor->on_selection_changed = [this,weak=std::weak_ptr<ComboBox>(editor),index,life](std::optional<std::size_t> selected) {
        if(life.expired())return;
        const auto editor=weak.lock();
        if(!editor)return;
        if (!selected || *selected >= editor->items.size()) return;
        editor->edit_text = editor->items[*selected].text;
        keyCodeEditEnd(index, editor->edit_text);
    };
    editor->on_text_changed = [this,weak=std::weak_ptr<ComboBox>(editor),life](const StringView value) {
        if(life.expired())return;
        const auto editor=weak.lock();
        if(!editor)return;
        const String text(value);
        publishKeyCodeCandidates(editor, choiceItems(keyCodeCandidates(value)), text);
    };
    return editor;
}

::cwxeditor::lib::Array<String> KeyCodeByFeatureView::keyCodeCandidates(StringView filter) const {
    auto source=values();
    auto history=key_code_history_;
    auto standard=standard_key_codes_;
    String query(filter);
    std::vector<String> candidates;
    if(standard)for(const auto& code:standard())if(!code.empty())appendUnique(candidates,code);
    for(const auto& code:history)if(!code.empty())appendUnique(candidates,code);
    for(const auto& item:source)if(!item.keyCode.empty())appendUnique(candidates,item.keyCode);
    if(!query.empty())std::erase_if(candidates,[&](const auto& value){return !detail::containsFolded(value,query);});
    return candidates;
}

bool KeyCodeByFeatureView::featureEditEnd(std::size_t index, String newText) {
    if (index >= valueCount()) return false;
    
    auto updated = value(index);
    updated.feature = std::move(newText);
    return mainValueEditEnd(index,updated);
}

bool KeyCodeByFeatureView::keyCodeEditEnd(std::size_t index, String newText) {
    if (index >= valueCount()) return false;
    rememberKeyCode(newText);
    auto updated = value(index);
    updated.keyCode = std::move(newText);
    return subValueEditEnd(index,updated);
}

bool KeyCodeByFeatureView::append(const KeyCodeByFeature& value, std::size_t index) {
    return isValidValue(value) && addValue(value,index);
}

::cwxeditor::lib::Array<KeyCodeByFeature> KeyCodeByFeatureView::createFromNode(
    const XNode& node) {
    ::cwxeditor::cwx::perf::TraceScope trace_createFromNode(__FILE__, __LINE__);
    ::cwxeditor::lib::Array<KeyCodeByFeature> result;
    for (auto child : node.element_children()) {
        if (child.name() != String(XML_NAME)) continue;
        KeyCodeByFeature value;
        value.fromNode(child);
        result.push_back(std::move(value));
    }
    return result;
}

XNode KeyCodeByFeatureView::toNode(std::span<const std::size_t> indices) const {
    ::cwxeditor::cwx::perf::TraceScope trace_toNode(__FILE__, __LINE__);
    auto node = XNode::create(XML_CONTAINER_NAME);
    for (const auto index : indices)
        if (index < valueCount()) value(index).toNode(node);
    return node;
}

XNode KeyCodeByFeatureView::toNode() const {
    auto node = XNode::create(XML_CONTAINER_NAME);
    for (const auto& item : values()) item.toNode(node);
    return node;
}

void KeyCodeByFeatureView::initColumns() {
    if (!table()) return;
    table()->columns = {
        {commons_.messages.cardFeature, commons_.featureColumnWidth},
        {commons_.messages.keyCode, commons_.featureKeyCodeColumnWidth},
    };
    table()->on_column_resized = [this,life=weakLifetime()](const std::size_t column,
                                         const float width) {
        if(life.expired())return;
        if (column < 2U && commons_.storeFeatureColumn)
            commons_.storeFeatureColumn(column, width);
    };
}

void KeyCodeByFeatureView::initEditors() {
    table()->inline_edit_column = 0U;
    table()->commit_unchanged_inline_edits = true;
    table()->on_inline_editor_created = [this, life=weakLifetime()](
        std::size_t, std::size_t column, const std::shared_ptr<TextEdit>& editor,
        const std::shared_ptr<ComboBox>& choice) {
        if (life.expired()) return;
        inline_key_code_binding_.reset();
        if (column != 1U || !choice || !commons_.setupKeyCodeEditor) return;
        const auto setup = commons_.setupKeyCodeEditor;
        auto binding = setup(editor, choice,
            [this, life] { return life.expired() ? lib::Array<String>{} : keyCodeCandidates(); },
            catch_modifier_, [life] { return !life.expired(); });
        if (!life.expired()) inline_key_code_binding_ = std::move(binding);
    };
}

void KeyCodeByFeatureView::cellEdit(std::size_t row, std::size_t column, String text) {
        if (row >= valueCount()) return;
        if (column == 0U) featureEditEnd(row, std::move(text));
        else if (column == 1U) keyCodeEditEnd(row, std::move(text));
}

void KeyCodeByFeatureView::updateItem(const KeyCodeByFeature& value, std::size_t index) {
    setValue(index,value);
}

::cwxeditor::lib::TableRow KeyCodeByFeatureView::prepareItem(const KeyCodeByFeature& value) const {
    ::cwxeditor::lib::TableRow row;
    auto& cells = row.cells;
    cells.resize(2U);
    const bool editable = !readOnly() && enabled();
    cells[0].icon = commons_.messages.cards;
    cells[0].text = value.feature;
    cells[0].value = value.feature;
    cells[0].editor = lib::CellEditorKind::Text;
    cells[0].enabled = editable;
    cells[1].icon = commons_.messages.keyCodeImage;
    cells[1].text = value.keyCode;
    cells[1].value = value.keyCode;
    // DのTableTextEditはcreateKeyCodeComboを作る。キーコー
    // ドの文字入力を受け付けつつ、標準・履歴候補を逐次絞り込みの選択一覧として提供する。
    cells[1].editor = lib::CellEditorKind::EditableChoice;
    cells[1].editor_choices = keyCodeCandidates();
    cells[1].enabled = editable;
    return row;
}

std::vector<String> KeyCodeByFeatureView::editorFeatures() const {
    std::vector<String> result;
    for (const auto& item : values())
        if (!item.feature.empty()) appendUnique(result, item.feature);
    return result;
}


void KeyCodeByFeatureView::rememberKeyCode(StringView value) {
    if (value.empty()) return;
    const auto found = std::find(key_code_history_.begin(), key_code_history_.end(), value);
    if (found != key_code_history_.end()) key_code_history_.erase(found);
    key_code_history_.emplace_back(value);
    constexpr std::size_t maxHistory = 128U;
    if (key_code_history_.size() > maxHistory) key_code_history_.erase(key_code_history_.begin());
}

KeyCodeByMotionView::KeyCodeByMotionView(Commons comm, Parent parent, int style,
                                         StandardKeyCodes standardKeyCodes,
                                         ElementOverrides elementOverrides,
                                         CatchModifier catchModifier)
    : editablelistview::AbstractEditableListView<KeyCodeByMotion>(detail::listCommons(comm),parent,style,true),
      commons_(std::move(comm)),
      parent_(parent),
      standard_key_codes_(std::move(standardKeyCodes)),
      element_overrides_(std::move(elementOverrides)),
      catch_modifier_(std::move(catchModifier)) {
    // 派生側のプロバイダーを保存してから初期化する。共通基底が通常のツー
    // ルバー・コンテキストメニューと、追加待ちのトランザクションを作る。
    (void)table();
}

KeyCodeByMotionView::~KeyCodeByMotionView() {
    retireCallbacks();
    if (table()) {
        table()->on_selection_changed = {};
        table()->on_cell_activate = {};
        table()->on_cell_edited = {};
        table()->on_inline_editor_created = {};
    }
}

bool KeyCodeByMotionView::catchModifier() const {
    return catch_modifier_ ? catch_modifier_() : false;
}

::cwxeditor::lib::Array<String> KeyCodeByMotionView::keyCodeCandidates(StringView filter) const {
    auto source=values();
    auto history=key_code_history_;
    auto standard=standard_key_codes_;
    String query(filter);
    std::vector<String> candidates;
    if(standard)for(const auto& code:standard())if(!code.empty())appendUnique(candidates,code);
    for(const auto& code:history)if(!code.empty())appendUnique(candidates,code);
    for(const auto& item:source)if(!item.keyCode.empty())appendUnique(candidates,item.keyCode);
    if(!query.empty())std::erase_if(candidates,[&](const auto& value){return !detail::containsFolded(value,query);});
    return candidates;
}

std::vector<String> KeyCodeByMotionView::motionChoices(std::size_t) const {
    return motionLabels(commons_.messages);
}

::cwxeditor::lib::Array<ElementOverride> KeyCodeByMotionView::currentElementOverrides() const {
    return element_overrides_ ? element_overrides_() : ::cwxeditor::lib::Array<ElementOverride>{};
}

std::vector<String> KeyCodeByMotionView::elementChoices(std::size_t) const {
    const auto messages=commons_.messages;
    const auto resolver=element_overrides_;
    return elementLabels(messages,resolver?resolver(): ::cwxeditor::lib::Array<ElementOverride>{});
}

std::shared_ptr<ComboBox> KeyCodeByMotionView::createMotionEditor(std::size_t index) {
    const auto life=weakLifetime();
    auto editor = detail::createComponent<ComboBox>(commons_.services, ComponentKind::ComboBox);
    const auto labels = motionChoices(index);
    if(life.expired())return editor;
    editor->items = choiceItems(labels);
    editor->editable = false;
    if (index < valueCount()) {
        const auto current = value(index).type;
        for (std::size_t choice = 0; choice < motion_types.size(); ++choice) {
            if (motion_types[choice] == current) {
                editor->selected_index = choice;
                break;
            }
        }
    }
    editor->set_enabled(!readOnly() && enabled());
    editor->on_selection_changed = [this,index,life](std::optional<std::size_t> choice) {
        if(life.expired())return;
        if (choice) motionEditEnd(index, *choice);
    };
    return editor;
}

std::shared_ptr<ComboBox> KeyCodeByMotionView::createElementEditor(std::size_t index) {
    const auto life=weakLifetime();
    auto editor = detail::createComponent<ComboBox>(commons_.services, ComponentKind::ComboBox);
    const auto labels = elementChoices(index);
    if(life.expired())return editor;
    editor->items = choiceItems(labels);
    editor->editable = false;
    editor->selected_index = 0U;
    if (index < valueCount() && value(index).hasElement) {
        for (std::size_t choice = 0; choice < elements.size(); ++choice) {
            if (elements[choice] == value(index).element) {
                editor->selected_index = choice + 1U;
                break;
            }
        }
    }
    editor->set_enabled(!readOnly() && enabled());
    editor->on_selection_changed = [this,index,life](std::optional<std::size_t> choice) {
        if(life.expired())return;
        if (choice) elementEditEnd(index, *choice);
    };
    return editor;
}

std::shared_ptr<ComboBox> KeyCodeByMotionView::createKeyCodeEditor(std::size_t index) {
    const auto life=weakLifetime();
    const String initial = index < valueCount() ? value(index).keyCode : String{};
    auto editor = detail::createComponent<ComboBox>(commons_.services, ComponentKind::ComboBox);
    editor->editable = true;
    editor->edit_text = initial;
    editor->items = choiceItems(keyCodeCandidates());
    if(life.expired())return editor;
    editor->set_enabled(!readOnly() && enabled());
    editor->on_selection_changed = [this,weak=std::weak_ptr<ComboBox>(editor),index,life](std::optional<std::size_t> selected) {
        if(life.expired())return;
        const auto editor=weak.lock();
        if(!editor)return;
        if (!selected || *selected >= editor->items.size()) return;
        editor->edit_text = editor->items[*selected].text;
        keyCodeEditEnd(index, editor->edit_text);
    };
    editor->on_text_changed = [this,weak=std::weak_ptr<ComboBox>(editor),life](const StringView value) {
        if(life.expired())return;
        const auto editor=weak.lock();
        if(!editor)return;
        const String text(value);
        publishKeyCodeCandidates(editor, choiceItems(keyCodeCandidates(value)), text);
    };
    return editor;
}

bool KeyCodeByMotionView::motionEditEnd(std::size_t index, std::size_t choice) {
    if (index >= valueCount() || choice >= motion_types.size()) return false;
    auto updated = value(index);
    updated.type = motion_types[choice];
    return mainValueEditEnd(index,updated);
}

bool KeyCodeByMotionView::elementEditEnd(std::size_t index, std::size_t choice) {
    if (index >= valueCount() || choice > elements.size()) return false;
    auto updated = value(index);
    if (choice == 0U) {
        updated.hasElement = false;
        updated.element = Element::All;
    } else {
        updated.hasElement = true;
        updated.element = elements[choice - 1U];
    }
    return subValueEditEnd(index,updated);
}

bool KeyCodeByMotionView::keyCodeEditEnd(std::size_t index, String newText) {
    if (index >= valueCount()) return false;
    rememberKeyCode(newText);
    auto updated = value(index);
    updated.keyCode = std::move(newText);
    return subValueEditEnd(index,updated);
}

bool KeyCodeByMotionView::append(const KeyCodeByMotion& value, std::size_t index) {
    return isValidValue(value) && addValue(value,index);
}

::cwxeditor::lib::Array<KeyCodeByMotion> KeyCodeByMotionView::createFromNode(const XNode& node) {
    ::cwxeditor::cwx::perf::TraceScope trace_createFromNode(__FILE__, __LINE__);
    ::cwxeditor::lib::Array<KeyCodeByMotion> result;
    for (auto child : node.element_children()) {
        if (child.name() != String(XML_NAME)) continue;
        KeyCodeByMotion value;
        value.fromNode(child);
        result.push_back(std::move(value));
    }
    return result;
}

XNode KeyCodeByMotionView::toNode(std::span<const std::size_t> indices) const {
    ::cwxeditor::cwx::perf::TraceScope trace_toNode(__FILE__, __LINE__);
    auto node = XNode::create(XML_CONTAINER_NAME);
    for (const auto index : indices)
        if (index < valueCount()) value(index).toNode(node);
    return node;
}

XNode KeyCodeByMotionView::toNode() const {
    auto node = XNode::create(XML_CONTAINER_NAME);
    for (const auto& item : values()) item.toNode(node);
    return node;
}

void KeyCodeByMotionView::initColumns() {
    if (!table()) return;
    table()->columns = {
        {commons_.messages.motion, commons_.motionColumnWidth},
        {commons_.messages.motionElement, commons_.motionElementColumnWidth},
        {commons_.messages.keyCode, commons_.motionKeyCodeColumnWidth},
    };
    table()->on_column_resized = [this,life=weakLifetime()](const std::size_t column,
                                         const float width) {
        if(life.expired())return;
        if (column < 3U && commons_.storeMotionColumn)
            commons_.storeMotionColumn(column, width);
    };
}

void KeyCodeByMotionView::initEditors() {
    // Dは効果内容の列で追加を始め、初期の選択も確定する。
    table()->inline_edit_column = 0U;
    table()->commit_unchanged_inline_edits = true;
    table()->on_inline_editor_created = [this, life=weakLifetime()](
        std::size_t, std::size_t column, const std::shared_ptr<TextEdit>& editor,
        const std::shared_ptr<ComboBox>& choice) {
        if (life.expired()) return;
        inline_key_code_binding_.reset();
        if (column != 2U || !choice || !commons_.setupKeyCodeEditor) return;
        const auto setup = commons_.setupKeyCodeEditor;
        auto binding = setup(editor, choice,
            [this, life] { return life.expired() ? lib::Array<String>{} : keyCodeCandidates(); },
            catch_modifier_, [life] { return !life.expired(); });
        if (!life.expired()) inline_key_code_binding_ = std::move(binding);
    };
}

void KeyCodeByMotionView::cellEdit(std::size_t row, std::size_t column, String text) {
        const auto life=weakLifetime();
    if (row >= valueCount()) return;
        if (column == 2U) {
            keyCodeEditEnd(row, std::move(text));
            return;
        }
        if (column == 0U) {
            const auto labels = motionChoices(row);
            if(life.expired())return;
            const auto found = std::find(labels.begin(), labels.end(), text);
            if (found != labels.end())
                motionEditEnd(row, static_cast<std::size_t>(found - labels.begin()));
            return;
        }
        if (column == 1U) {
            const auto labels = elementChoices(row);
            if(life.expired())return;
            // ネイティブのセル編集は、重複し得るElementOverride
            // の表示ラベルではなく、安定した意味上の値を公開する。
            std::vector<String> values{"element:none"};
            for (const auto element : elements)
                values.push_back("element:" + types::fromElement(element));
            if (const auto semantic = std::find(values.begin(), values.end(), text);
                semantic != values.end()) {
                elementEditEnd(row, static_cast<std::size_t>(semantic - values.begin()));
                return;
            }
            // 従来のラベルを直接使うアダプターは、曖昧でない場合だけ維持する。
            const auto found = std::find(labels.begin(), labels.end(), text);
            if (found != labels.end() && std::count(labels.begin(), labels.end(), text) == 1)
                elementEditEnd(row, static_cast<std::size_t>(found - labels.begin()));
        }
}
KeyCodeByMotionView::ElementPresentation
KeyCodeByMotionView::elementPresentation(Element value) const {
    const auto comm=commons_;
    const auto resolver=element_overrides_;
    return resolvedPresentation(comm,resolver?resolver(): ::cwxeditor::lib::Array<ElementOverride>{},value);
}

void KeyCodeByMotionView::updateItem(const KeyCodeByMotion& value, std::size_t index) {
    setValue(index,value);
}

::cwxeditor::lib::TableRow KeyCodeByMotionView::prepareItem(const KeyCodeByMotion& value) const {
    // 外部コードを呼ぶ前に、必要な全所有元・プロバイダーを保持する。
    const auto comm=commons_;
    const auto resolver=element_overrides_;
    const auto standard=standard_key_codes_;
    const auto history=key_code_history_;
    const auto source=values();
    const bool editable = !readOnly() && enabled();
    const auto overrides=resolver?resolver(): ::cwxeditor::lib::Array<ElementOverride>{};
    ::cwxeditor::lib::TableRow row;
    auto& cells = row.cells;
    cells.resize(3U);
    cells[0].icon = comm.messages.motionImage(value.type);
    cells[0].text = comm.messages.motionLabel(value.type);
    cells[0].value = motion::mTypeToName(value.type);
    cells[0].editor = lib::CellEditorKind::Choice;
    cells[0].editor_choices = motionLabels(comm.messages);
    cells[0].enabled = editable;

    if (value.hasElement) {
        const auto presentation = resolvedPresentation(comm,overrides,value.element);
        cells[1].icon = presentation.image;
        cells[1].text = presentation.name;
        cells[1].value = presentation.path.empty() ? types::fromElement(value.element)
                                                    : presentation.path;
    } else {
        cells[1].icon = comm.messages.emptyIcon;
        cells[1].text = comm.messages.noElement;
        cells[1].value.clear();
    }
    cells[1].editor = lib::CellEditorKind::Choice;
    cells[1].editor_choices = elementLabels(comm.messages,overrides);
    cells[1].editor_choice_values = {"element:none"};
    for (const auto element : elements)
        cells[1].editor_choice_values.push_back("element:" + types::fromElement(element));
    cells[1].editor_value = value.hasElement
        ? "element:" + types::fromElement(value.element) : "element:none";
    cells[1].enabled = editable;

    cells[2].icon = comm.messages.keyCodeImage;
    cells[2].text = value.keyCode;
    cells[2].value = value.keyCode;
    cells[2].editor = lib::CellEditorKind::EditableChoice;
    cells[2].editor_choices = codesForRows(source,history,standard);
    cells[2].enabled = editable;
    return row;
}


void KeyCodeByMotionView::rememberKeyCode(StringView value) {
    if (value.empty()) return;
    const auto found = std::find(key_code_history_.begin(), key_code_history_.end(), value);
    if (found != key_code_history_.end()) key_code_history_.erase(found);
    key_code_history_.emplace_back(value);
    constexpr std::size_t maxHistory = 128U;
    if (key_code_history_.size() > maxHistory) key_code_history_.erase(key_code_history_.begin());
}

}  // namespace cwxeditor::cwx::editor::gui::dwt::autokeycode
