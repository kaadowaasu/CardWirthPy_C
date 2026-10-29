#pragma once

// cwx/editor/gui/dwt/smalldialogs.d
// の移植可能なC++20実装。 画面
// 状態をEditor共通の部品・サ
// ービスで表し、特定のウィンドウシ
// ステムに依存せず、 Dの操作部品
// ・編集処理・結果取得を維持する。

#include "cwxeditor_src/core/cwx/area.hpp"
#include "cwxeditor_src/core/cwx/card.hpp"
#include "cwxeditor_src/core/cwx/flag.hpp"
#include "cwxeditor_src/core/cwx/importutils.hpp"
#include "cwxeditor_src/core/cwx/script.hpp"
#include "cwxeditor_src/core/cwx/skin.hpp"
#include "cwxeditor_src/core/cwx/structs.hpp"
#include "cwxeditor_src/core/cwx/summary.hpp"
#include "cwxeditor_src/core/cwx/types.hpp"
#include "cwxeditor_src/core/cwx/usecounter.hpp"
#include "cwxeditor_src/core/cwx/utils.hpp"
#include "cwxeditor_src/core/cwx/versioninfo.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/absdialog.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/scripterrordialog.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dprops.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dmenu.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dutils.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dskin.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/incsearch.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/toolspane.hpp"
#include "cwxeditor_src/lib/components.hpp"
#include "cwxeditor_src/lib/events.hpp"
#include "cwxeditor_src/lib/services.hpp"
#include "cwxeditor_src/lib/window_host.hpp"

#include <cstddef>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cwxeditor::cwx::editor::gui::dwt::smalldialogs {

namespace lib = ::cwxeditor::lib;
namespace absdialog = ::cwxeditor::cwx::editor::gui::dwt::absdialog;
namespace dprops = ::cwxeditor::cwx::editor::gui::dwt::dprops;
namespace dskin = ::cwxeditor::cwx::editor::gui::dwt::dskin;
namespace incsearch = ::cwxeditor::cwx::editor::gui::dwt::incsearch;
namespace toolspane = ::cwxeditor::cwx::editor::gui::dwt::toolspane;
namespace importutils = ::cwxeditor::cwx::importutils;
namespace script = ::cwxeditor::cwx::script;
namespace skin = ::cwxeditor::cwx::skin;
namespace structs = ::cwxeditor::cwx::structs;
namespace summary = ::cwxeditor::cwx::summary;
namespace types = ::cwxeditor::cwx::types;
namespace usecounter = ::cwxeditor::cwx::usecounter;

using String = lib::String;
using StringView = lib::StringView;
using Props = dprops::Props;
using Parent = std::shared_ptr<lib::Window>;
using Services = lib::Services;
using Text = std::shared_ptr<lib::TextEdit>;
using Toggle = std::shared_ptr<lib::CheckBox>;
using Combo = std::shared_ptr<lib::ComboBox>;
using Table = std::shared_ptr<lib::TableView>;
using Spinner = std::shared_ptr<lib::NumberEdit>;
using Summary = summary::Summary;
using SummaryPtr = summary::Summary::Owner;
using SkinPtr = skin::Skin::Ptr;
using EvTemplate = structs::EvTemplate;
using ScTemplate = structs::ScTemplate;

/// Editor全体のイベント群に対応する任意のホスト操作。
/// 通知が一つもなくても、ダイアログを使えるようにする。
struct Callbacks {
    Props* prop{};
    Services services{};
    std::function<void(StringView)> openLink;
    std::function<void(StringView)> openPath;
    lib::Action shutdown;
    std::function<std::optional<String>(StringView, StringView)> selectDirectory;
    std::function<void(const script::CWXScriptException&)> scriptError;
    summary::FileSync* sync{};
    lib::Action scenarioNameChanged;
    std::function<void(Summary*)> scenarioChanged;
    std::function<lib::Action(lib::Action)> subscribeScenarioName;
    std::function<lib::Action(std::function<void(SummaryPtr)>)> subscribeScenario;
};

struct SkinChoice {
    String type;
    String name;
    String path;
};

class CreateScenarioDialog : public absdialog::AbsDialog {
private:
    Callbacks callbacks_{};
    Props* prop_{};
    Text name_{};
    Combo skin_combo_{};
    std::vector<SkinChoice> skin_types_{};
    std::vector<SkinChoice> all_skin_types_{};
    std::shared_ptr<incsearch::IncSearch> skin_inc_search_{};
    std::shared_ptr<lib::Menu> skin_menu_{};
    Combo template_combo_{};
    String name_value_{};
    SkinChoice selected_skin_{};
    std::shared_ptr<lib::RadioButton> base_skin_{};
    std::shared_ptr<lib::RadioButton> base_template_{};
    std::unordered_map<std::size_t, ScTemplate> template_table_{};
    std::unordered_map<String, bool> classic_templates_{};
    bool use_template_{};
    SummaryPtr from_template_{};
    Text directory_{};
    std::shared_ptr<lib::Button> directory_reference_{};
    std::shared_ptr<lib::Button> directory_open_{};
    Toggle create_scenario_directory_{};
    String directory_value_{};
    bool classic_{};
    bool initial_focus_done_{};

    void enabledClassicDir();
    void refSkinVal();
    void refClassic();
    void refreshSkins();
    void selectDirectory();
    void selectDirectoryAsync();

public:
    CreateScenarioDialog(Props& prop, Parent parent, bool current_window,
                          Callbacks callbacks = {}, Services services = {});
    CreateScenarioDialog(Callbacks callbacks, Props& prop, Parent parent,
                         bool current_window, Services services = {})
        : CreateScenarioDialog(prop, std::move(parent), current_window,
                               std::move(callbacks), std::move(services)) {}

    [[nodiscard]] bool noScenario() const noexcept { return true; }
    [[nodiscard]] String name() const;
    [[nodiscard]] String skinType() const;
    [[nodiscard]] String skinName() const;
    [[nodiscard]] SummaryPtr fromTemplate() const { return from_template_; }
    [[nodiscard]] bool legacy() const noexcept { return classic_; }
    [[nodiscard]] const std::shared_ptr<incsearch::IncSearch>& skinIncSearch()
        const noexcept { return skin_inc_search_; }
    [[nodiscard]] const std::shared_ptr<lib::Menu>& skinMenu() const noexcept {
        return skin_menu_;
    }
    [[nodiscard]] String classicDir() const;
    [[nodiscard]] bool classicDirEnabled() const noexcept { return classic_; }
    static String createClassicDir(Props& prop, Parent parent,
                                   Callbacks callbacks = {});

protected:
    void setup(Container& area) override;
    bool close(bool ok, bool& cancel) override;
};

class VersionDialog : public absdialog::AbsDialog {
private:
    Callbacks callbacks_{};
    Props* prop_{};
    dmenu::TextMenuModifyPtr text_menu_{};

public:
    VersionDialog(Props& prop, Parent parent, Callbacks callbacks = {},
                  Services services = {});
    VersionDialog(Callbacks callbacks, Props& prop, Parent parent,
                  Services services = {})
        : VersionDialog(prop, std::move(parent), std::move(callbacks),
                        std::move(services)) {}

    [[nodiscard]] bool noScenario() const noexcept { return true; }

protected:
    void setup(Container& area) override;
};

class ErrorDialog : public absdialog::AbsDialog {
private:
    Callbacks callbacks_{};
    Props* prop_{};
    String description_{};
    dmenu::TextMenuModifyPtr text_menu_{};

public:
    ErrorDialog(Props& prop, Parent parent, String description,
                Callbacks callbacks = {}, Services services = {});
    ErrorDialog(Callbacks callbacks, Props& prop, Parent parent,
                String description, Services services = {})
        : ErrorDialog(prop, std::move(parent), std::move(description),
                      std::move(callbacks), std::move(services)) {}
    [[nodiscard]] bool noScenario() const noexcept { return true; }

protected:
    void setup(Container& area) override;
};

template <typename Area>
class ReNumDialog : public absdialog::AbsDialog {
private:
    Props* prop_{};
    Summary* summary_{};
    const Area* area_{};
    std::uint64_t minimum_id_{};
    Spinner id_{};
    std::uint64_t new_id_{};

    static String areaName(const Area& value) {
        if constexpr (requires { value.name(); }) return value.name();
        else if constexpr (requires { value.name; }) return value.name;
        else return {};
    }

public:
    ReNumDialog(Props& prop, Parent parent, Summary* summary, const Area& area,
                std::uint64_t minimum_id, Services services = {})
        : absdialog::AbsDialog(prop, std::move(parent), true,
                               prop.requiredMsgs().dlgTitReNumbering.value,
                               prop.images().menu(types::MenuID::ReNumbering),
                               false, nullptr, false, true,
                               {}, false, std::move(services)),
          prop_(&prop), summary_(summary), area_(&area), minimum_id_(minimum_id) {
        enterClose(true);
    }
    ReNumDialog(Props& prop, Parent parent, Summary& summary, const Area& area,
                std::uint64_t minimum_id, Services services = {})
        : ReNumDialog(prop, std::move(parent), &summary, area, minimum_id,
                      std::move(services)) {}

    [[nodiscard]] std::uint64_t newId() const noexcept { return new_id_; }
    using absdialog::AbsDialog::services;
    [[nodiscard]] bool noScenario() const noexcept { return true; }

protected:
    void setup(Container& area) override {
        auto group = std::make_shared<lib::GroupBox>();
        group->title = prop_->requiredMsgs().reNumbering.value;
        auto layout = std::make_shared<lib::GridLayout>();
        layout->columns = 3U;
        group->set_layout(layout);
        String name = area_ ? areaName(*area_) : String{};
        if constexpr (std::is_base_of_v<card::Card, Area>) {
            if (const auto* effect = dynamic_cast<const card::EffectCard*>(area_);
                effect && effect->linkId() && summary_) {
                lib::Owner<card::Card> referenced;
                if (dynamic_cast<const card::SkillCard*>(effect)) {
                    referenced = summary_->skill(effect->linkId());
                    name = prop_->requiredMsgs().noSelectSkill.value;
                } else if (dynamic_cast<const card::ItemCard*>(effect)) {
                    referenced = summary_->item(effect->linkId());
                    name = prop_->requiredMsgs().noSelectItem.value;
                } else if (dynamic_cast<const card::BeastCard*>(effect)) {
                    referenced = summary_->beast(effect->linkId());
                    name = prop_->requiredMsgs().noSelectBeast.value;
                }
                if (referenced) name = referenced->name();
            }
        }
        // イベント所有者側のインターフェースを使い、共通
        // ラベル検索からオブジェクト種別名を取得する。
        const auto kind = [&] {
            if constexpr (std::is_base_of_v<::cwxeditor::cwx::area::AbstractArea, Area>)
                return dutils::objNameFrom(prop_,
                    static_cast<const ::cwxeditor::cwx::area::AbstractArea*>(area_));
            else
                return dutils::objNameFrom(prop_, area_);
        }();
        group->add(std::make_shared<lib::Label>(::cwxeditor::cwx::utils::tryFormat(
            prop_->requiredMsgs().reNumbering1.value, kind, name)));
        id_ = std::make_shared<lib::NumberEdit>();
        id_->minimum = static_cast<double>(minimum_id_);
        id_->maximum = static_cast<double>(prop_->looks().idMax());
        id_->value = static_cast<double>(minimum_id_);
        group->add(id_);
        group->add(std::make_shared<lib::Label>(::cwxeditor::cwx::utils::tryFormat(
            prop_->requiredMsgs().reNumbering2.value, kind, name)));
        area.add(group);
    }

    bool close(bool ok) override {
        if (ok && id_) {
            if (!std::isfinite(id_->value) || id_->value < id_->minimum ||
                id_->value > id_->maximum) return false;
            new_id_ = static_cast<std::uint64_t>(id_->value);
        }
        return ok;
    }
};

// 異なるのはOSの表示操作だけとし、検証・部品・受理したIDは
// 同期的なネイティブUIと非同期のWeb UIで共用する。
template <typename Object>
bool presentReNumbering(const std::shared_ptr<ReNumDialog<Object>>& dialog,
                       std::function<void(bool, std::uint64_t)> completed) {
    const auto gui = dialog->services().gui;
    if (gui && lib::editor_uses_async_dialog_flow(gui->platform()))
        return dialog->openAsync([dialog, completed](bool accepted) {
            completed(accepted, accepted ? dialog->newId() : 0U);
        });
    const bool accepted = dialog->open();
    completed(accepted, accepted ? dialog->newId() : 0U);
    return true;
}

class ScriptVarSetDialog : public absdialog::AbsDialog {
private:
    Callbacks callbacks_{};
    Props* prop_{};
    Summary* summary_{};
    std::shared_ptr<usecounter::UseCounter> use_counter_{};
    SkinPtr skin_{};
    lib::Array<String> variables_{};
    String script_text_{};
    String base_text_{};
    lib::Array<String> values_{};
    Table table_{};
    std::shared_ptr<int> editor_lifetime_{std::make_shared<int>(0)};
    std::uint64_t editor_generation_{};
    // 画面にはD互換の説明ラベルを表示し、スクリプトへは対応
    // するCWXScriptリテラルを挿入する。 パスや引
    // 用符付きの文字列IDを正しく扱うため、両者を分ける。
    lib::Array<std::pair<String, String>> editor_table_{};
    script::CompileOption option_{};
    script::Contents contents_{};
    std::vector<std::shared_ptr<scripterrordialog::ScriptErrorDialog>> diagnostics_{};
    void closeDiagnostics();

    void editEnd(std::size_t row, std::size_t column, String text);
    void bindEditors();

public:
    ScriptVarSetDialog(Props& prop, Summary* summary,
                       std::shared_ptr<usecounter::UseCounter> use_counter,
                       Parent parent, const lib::Array<String>& variables,
                       String script_text, String base_text,
                       const script::CompileOption& option,
                       Callbacks callbacks = {}, Services services = {},
                       SkinPtr skin = {});
    ScriptVarSetDialog(Callbacks callbacks, Props& prop, Summary* summary,
                       std::shared_ptr<usecounter::UseCounter> use_counter,
                       Parent parent, const lib::Array<String>& variables,
                       String script_text, String base_text,
                       const script::CompileOption& option,
                       Services services = {}, SkinPtr skin = {})
        : ScriptVarSetDialog(prop, summary, std::move(use_counter),
                             std::move(parent), variables,
                             std::move(script_text), std::move(base_text),
                             option, std::move(callbacks), std::move(services),
                             std::move(skin)) {}

    ~ScriptVarSetDialog() override;

    [[nodiscard]] const script::Contents& contents() const noexcept {
        return contents_;
    }
    [[nodiscard]] const Table& variableTable() const noexcept { return table_; }
    [[nodiscard]] bool noScenario() const noexcept { return true; }

protected:
    void setup(Container& area) override;
    bool close(bool ok, bool& cancel) override;
};

class EventTemplateDialog : public absdialog::AbsDialog,
                            public std::enable_shared_from_this<EventTemplateDialog> {
private:
    Callbacks callbacks_{};
    Props* prop_{};
    Summary* summary_{};
    lib::SharedSlice<EvTemplate> templates_{};
    std::shared_ptr<toolspane::ToolsPane<EvTemplate>> tools_pane_{};
    bool pane_needs_apply_{};
    lib::Action disconnect_scenario_name_{};
    lib::Action disconnect_scenario_{};
    std::shared_ptr<EventTemplateDialog> self_keep_alive_{};
    bool lifetime_hooks_installed_{};

    [[nodiscard]] String title() const;
    void refScenarioName();
    void refScenario(Summary* summary);
    void retainWhileOpen();

public:
    EventTemplateDialog(Props& prop, Summary* summary, Parent parent,
                        lib::SharedSlice<EvTemplate> templates,
                        Callbacks callbacks = {}, Services services = {});
    ~EventTemplateDialog() override;
    EventTemplateDialog(Callbacks callbacks, Props& prop, Summary* summary,
                        Parent parent, lib::SharedSlice<EvTemplate> templates,
                        Services services = {})
        : EventTemplateDialog(prop, summary, std::move(parent),
                              std::move(templates), std::move(callbacks),
                              std::move(services)) {}

    [[nodiscard]] bool noScenario() const noexcept { return summary_ == nullptr; }
    [[nodiscard]] lib::SharedSlice<EvTemplate> eventTemplates() noexcept {
        return templates_;
    }
    [[nodiscard]] lib::SharedSlice<const EvTemplate> eventTemplates() const noexcept {
        return templates_;
    }
    [[nodiscard]] bool open();
    void forceCancel();

protected:
    void setup(Container& area) override;
    bool apply() override;
};

class ImportOptionDialog : public absdialog::AbsDialog {
private:
    Callbacks callbacks_{};
    Props* prop_{};
    importutils::ImportOption option_{};
    bool can_include_image_{};
    Combo materials_{};
    Combo variables_{};
    Combo casts_{};
    Combo skills_{};
    Combo items_{};
    Combo beasts_{};
    Combo infos_{};
    Combo areas_{};
    Combo battles_{};
    Combo packages_{};
    Combo included_files_{};
    Combo hands_{};
    Combo beasts_in_motions_{};
    Toggle overwrite_scenario_info_{};

    template <typename Enum>
    void addEnumItems(const Combo& combo, Enum selected,
                      bool include_images);
    template <typename Enum>
    static Enum selectedEnum(const Combo& combo, Enum fallback,
                             bool include_images);

public:
    ImportOptionDialog(Props& prop, Parent parent, bool can_include_image,
                       Callbacks callbacks = {}, Services services = {});
    ImportOptionDialog(Callbacks callbacks, Parent parent, bool can_include_image,
                       Services services = {});

    [[nodiscard]] bool noScenario() const noexcept { return true; }
    [[nodiscard]] importutils::ImportOption option() const { return option_; }

protected:
    void setup(Container& area) override;
    bool close(bool ok) override;
};

class ImportResultDialog : public absdialog::AbsDialog {
private:
    enum class RowKind : std::uint8_t {
        Material, Flag, Step, Variant, Cast, Skill, Item, Beast, Info,
        Area, Battle, Package,
    };
    struct RowReference {
        RowKind kind{};
        String group;
        std::uint64_t id{};
        std::size_t source_index{};
    };

    Callbacks callbacks_{};
    Props* prop_{};
    Summary* summary_{};
    SkinPtr skin_{};
    importutils::ImportResult result_{};
    Table list_{};
    std::vector<RowReference> row_references_{};

    static String resourceName(const lib::Owner<area::AbstractArea>& value);
    static String cardName(const lib::Owner<card::Card>& value);
    void appendRow(String text, RowReference reference, lib::ResourceId icon = {});
    void retainCheckedRows(importutils::ImportResult& destination) const;

public:
    ImportResultDialog(Props& prop, Summary* summary, Parent parent,
                       importutils::ImportResult result,
                       Callbacks callbacks = {}, Services services = {},
                       SkinPtr skin = {});
    ImportResultDialog(Callbacks callbacks, Summary* summary, Parent parent,
                       importutils::ImportResult result,
                       Services services = {}, SkinPtr skin = {});

    [[nodiscard]] bool noScenario() const noexcept { return true; }
    [[nodiscard]] importutils::ImportResult checkedResult() const { return result_; }

protected:
    void setup(Container& area) override;
    bool close(bool ok) override;
};

}  // namespace cwxeditor::cwx::editor::gui::dwt::smalldialogs
