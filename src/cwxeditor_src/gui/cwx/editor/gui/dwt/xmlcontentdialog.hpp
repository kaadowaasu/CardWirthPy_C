#pragma once

// cwx/editor/gui/dwt/xmlcontentdialog.d
// の移植可能なC++20実装。 ダイアログがXML
// 編集部品のモデルを所有し、ネイティブ側は公
// 開lib部品へ接続する。 Editor層
// へSWT・Win32の型を持ち込まない。

#include "cwxeditor_src/core/cwx/event.hpp"
#include "cwxeditor_src/core/cwx/skin.hpp"
#include "cwxeditor_src/core/cwx/summary.hpp"
#include "cwxeditor_src/core/cwx/usecounter.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/commons.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dprops.hpp"
#include "cwxeditor_src/lib/clipboard.hpp"
#include "cwxeditor_src/lib/components.hpp"
#include "cwxeditor_src/lib/events.hpp"
#include "cwxeditor_src/lib/layout.hpp"
#include "cwxeditor_src/lib/xml.hpp"

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <utility>

namespace cwxeditor::cwx::editor::gui::dwt::eventdialog { class EventDialog; }

namespace cwxeditor::cwx::editor::gui::dwt::xmlcontentdialog {

namespace event = ::cwxeditor::cwx::event;
namespace lib = ::cwxeditor::lib;
namespace skin = ::cwxeditor::cwx::skin;
namespace usecounter = ::cwxeditor::cwx::usecounter;
namespace xml = ::cwxeditor::lib;
namespace commons = ::cwxeditor::cwx::editor::gui::dwt::commons;
namespace dprops = ::cwxeditor::cwx::editor::gui::dwt::dprops;

using String = lib::String;
using StringView = lib::StringView;
using Parent = std::shared_ptr<lib::Container>;
using PanelPtr = std::shared_ptr<lib::Panel>;
using Content = event::Content;
using ContentPtr = event::ContentPtr;
using Summary = ::cwxeditor::cwx::summary::Summary;
using SummaryPtr = Summary::Owner;
using Skin = ::cwxeditor::cwx::skin::Skin;
using SkinPtr = Skin::Ptr;
using UseCounter = ::cwxeditor::cwx::usecounter::UseCounter;
using UseCounterPtr = ::cwxeditor::lib::Owner<UseCounter>;

/// 参照テンプレートの表示画面で示すファイル。
struct ReferenceFile {
    String name;
    String path;
    String text;
    bool hidden{};
};

/// DのダイアログのXML構文検査結果。
struct XmlCheckResult {
    bool valid{};
    String checkedText;
    String message;
    std::vector<String> replacedVariables;
};

/// バックエンドに依存しない、独自XMLコンテントの具体的なダイアログ。
class XmlContentDialog final {
public:
    // DのMessageBoxのアイコン。情報・警告・エラーのいずれか。
    using ReportMessage = std::function<void(lib::MessageSeverity severity,
                                              StringView title, StringView message)>;
    using Confirm = std::function<bool(StringView title, StringView message)>;
    // 処理をブロックしないOK・キャンセルの確認。Web/ALLで入
    // 力通知内から 同期的なメッセージ画面を開くとイベントループへ再
    // 入して停止するため、 ボタンからの確認はすべてこの経路を使う。
    using AskAsync = std::function<void(lib::MessageSeverity severity, StringView title,
                                        StringView message, std::function<void(bool)> done)>;
    using ReferenceOpened = std::function<void(const std::vector<ReferenceFile>&)>;

    XmlContentDialog(commons::Commons& comm, dprops::Props& prop,
                     std::shared_ptr<lib::Window> shell, SummaryPtr summary,
                     SkinPtr forceSkin, UseCounterPtr useCounter,
                     ContentPtr parent, ContentPtr event = {});
    XmlContentDialog(commons::Commons& comm, dprops::Props& prop,
                     SummaryPtr summary = {}, ContentPtr parent = {},
                     ContentPtr event = {}, SkinPtr forceSkin = {},
                     UseCounterPtr useCounter = {});
    ~XmlContentDialog();

    XmlContentDialog(const XmlContentDialog&) = delete;
    XmlContentDialog& operator=(const XmlContentDialog&) = delete;

    [[nodiscard]] const commons::Commons& commons() const noexcept { return *comm_; }
    [[nodiscard]] const dprops::Props& props() const noexcept { return *prop_; }
    [[nodiscard]] const SummaryPtr& summary() const noexcept { return summary_; }
    [[nodiscard]] const SkinPtr& forceSkin() const noexcept { return force_skin_; }
    [[nodiscard]] const UseCounterPtr& useCounter() const noexcept { return use_counter_; }
    [[nodiscard]] const ContentPtr& parentContent() const noexcept { return parent_content_; }
    [[nodiscard]] const ContentPtr& event() const noexcept { return event_; }
    [[nodiscard]] const std::shared_ptr<lib::Window>& shell() const noexcept { return shell_; }

    [[nodiscard]] bool isOpen() const noexcept { return open_; }
    void open();
    // 共通のイベントホストが、参照・変数ダイアログの所有者を渡す。
    void bindEventShell(const std::shared_ptr<lib::Dialog>& shell) { dialog_ = shell; }
    void active();
    void close();
    void forceCancel() noexcept { open_ = false; }

    /// parent配下にすべての共通部品を作り、接続する。
    [[nodiscard]] PanelPtr createControl(const Parent& parent = {},
                                         bool includeFooter = true);
    /// Dのsetup()と同じく、既存パネルへ部品を接続し直す。
    [[nodiscard]] PanelPtr setup(const PanelPtr& area,
                                 bool includeFooter = true);

    [[nodiscard]] const PanelPtr& panel() const noexcept { return panel_; }
    [[nodiscard]] const std::shared_ptr<lib::TextEdit>& textEditor() const noexcept {
        return text_area_;
    }
    [[nodiscard]] const std::shared_ptr<lib::ComboBox>& xmlTypeCombo() const noexcept {
        return xml_type_combo_;
    }
    [[nodiscard]] const std::shared_ptr<lib::RadioButton>& assignOffButton() const noexcept {
        return assign_off_;
    }
    [[nodiscard]] const std::shared_ptr<lib::RadioButton>& assignOnButton() const noexcept {
        return assign_on_;
    }
    [[nodiscard]] const std::shared_ptr<lib::ListView>& referenceList() const noexcept {
        return reference_list_;
    }
    [[nodiscard]] const std::shared_ptr<lib::TextEdit>& referenceViewer() const noexcept {
        return reference_viewer_;
    }

    [[nodiscard]] String text() const;
    void setText(String value);
    [[nodiscard]] String selectedXmlType(
        const std::shared_ptr<lib::Window>& parent = {}) const;
    void selectXmlType(String label);
    [[nodiscard]] int textLength(const lib::TextEdit& text) const;
    void replaceTextByEdit(String value);
    void insertTextByEdit(String value);
    void insertVariantReference(StringView name);
    void insertVariantName(StringView name);
    /// 文字列メニューの操作は、このダイアログの作業コピー
    /// 内だけで行う。 Contentの変更やシナリオ全
    /// 体の変更通知は出さず、適用時にモデルへ反映する。
    [[nodiscard]] bool undoText();
    [[nodiscard]] bool redoText();
    [[nodiscard]] bool cutSelection();
    void copySelection() const;
    void pasteClipboard();
    void selectAllText();

    [[nodiscard]] const std::vector<String>& xmlTypeLabels() const noexcept {
        return xml_type_labels_;
    }
    [[nodiscard]] String labelToType(StringView label) const;
    [[nodiscard]] String typeToLabel(StringView type) const;
    [[nodiscard]] String templateDirectory(StringView xmlTypeLabel) const;
    [[nodiscard]] std::vector<String> templateFiles(StringView xmlTypeLabel,
                                                     bool includeHidden) const;
    [[nodiscard]] String getXmlTemplate(StringView xmlTypeLabel) const;

    /// 文字列・XML種別・代入フラグ・分岐の子の名前をイベントモデル
    /// へ適用する。 新規イベントはevent()で保持する。作業コピ
    /// ーと実際の適用で空白だけの正規化を共用し、実際の変更を返す。
    static std::vector<std::pair<ContentPtr, String>> applyBranchNames(
        const ContentPtr& event, const ::cwxeditor::cwx::props::CProps& prop);
    [[nodiscard]] bool apply();
    [[nodiscard]] bool initializeTemplate(bool force = false);
    [[nodiscard]] std::vector<ReferenceFile> openReference();
    [[nodiscard]] XmlCheckResult checkXml(bool substituteVariables) const;

    [[nodiscard]] int assignment() const noexcept { return assignment_; }
    void setAssignment(int value) noexcept;
    [[nodiscard]] bool dirty() const noexcept { return dirty_; }
    void clearDirty() noexcept { dirty_ = false; }
    [[nodiscard]] const String& selectedReference() const noexcept {
        return selected_reference_;
    }
    [[nodiscard]] const String& referenceText() const noexcept {
        return reference_text_;
    }

    Confirm confirmInitialize;
    Confirm confirmOverwrite;
    ReportMessage reportMessage;
    AskAsync askAsync;
    // DのinitBtnの順序に合わせ、まず確認し、文字列が
    // ある場合だけ上書きを確認してからテンプレートを入れる。
    // askAsyncを使い、呼び出し元をブロックしない。
    void initializeTemplateInteractive();
    ReferenceOpened referenceOpened;
    std::function<std::vector<String>()> variantChoices;
    lib::Action applied;
    lib::Action closed;
    lib::Action modified;
    lib::Callback<void(const ContentPtr&)> contentChanged;

private:
    struct TypeEntry {
        String label;
        String type;
    };

    commons::Commons* comm_{};
    dprops::Props* prop_{};
    std::shared_ptr<lib::Window> shell_{};
    SummaryPtr summary_{};
    SkinPtr force_skin_{};
    UseCounterPtr use_counter_{};
    ContentPtr parent_content_{};
    ContentPtr event_{};

    std::shared_ptr<eventdialog::EventDialog> event_host_{};
    PanelPtr panel_{};
    std::shared_ptr<lib::Dialog> dialog_{};
    std::shared_ptr<lib::Dialog> reference_dialog_{};
    std::shared_ptr<lib::Dialog> variant_dialog_{};
    std::shared_ptr<void> variant_chooser_{};
    std::shared_ptr<const int> reference_lifetime_{std::make_shared<const int>(0)};
    void showReferenceDialog();
    std::shared_ptr<lib::GroupBox> text_group_{};
    std::shared_ptr<lib::TextEdit> text_area_{};
    std::shared_ptr<lib::ComboBox> xml_type_combo_{};
    std::shared_ptr<lib::RadioButton> assign_off_{};
    std::shared_ptr<lib::RadioButton> assign_on_{};
    std::shared_ptr<lib::Button> initialize_button_{};
    std::shared_ptr<lib::Button> reference_button_{};
    std::shared_ptr<lib::Button> insert_reference_button_{};
    std::shared_ptr<lib::Button> insert_name_button_{};
    std::shared_ptr<lib::Button> check_button_{};
    std::shared_ptr<lib::ListView> reference_list_{};
    std::shared_ptr<lib::TextEdit> reference_viewer_{};
    std::shared_ptr<lib::Menu> text_menu_{};
    std::shared_ptr<lib::Button> ok_button_{};
    std::shared_ptr<lib::Button> cancel_button_{};

    std::vector<TypeEntry> type_entries_{};
    std::vector<String> xml_type_labels_{};
    std::vector<ReferenceFile> reference_files_{};
    String selected_type_label_{};
    String selected_reference_{};
    String reference_text_{};
    String draft_text_{};
    std::vector<String> text_history_{};
    std::size_t text_history_index_{};
    int assignment_{1};
    bool open_{};
    bool dirty_{};
    bool changing_text_{};

    void initializeState();
    void buildTypeEntries();
    void bindControls();
    void setTextInternal(String value, bool notify);
    void resetTextHistory();
    void recordTextHistory();
    [[nodiscard]] bool replaceSelectionByCommand(StringView value);
    void markChanged();
    [[nodiscard]] bool variableExists(StringView name) const;
    [[nodiscard]] static String lowerExtension(StringView value);
    [[nodiscard]] String message(StringView key, String fallback) const;
    [[nodiscard]] TypeEntry* findType(StringView label);
    [[nodiscard]] const TypeEntry* findType(StringView label) const;
    [[nodiscard]] bool selectReferenceFile(std::size_t index);
    void showVariantChooser(bool reference);
};

}  // namespace cwxeditor::cwx::editor::gui::dwt::xmlcontentdialog
