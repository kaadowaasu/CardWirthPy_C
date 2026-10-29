#include "cw/debug/event.hpp"
#include "cw/debug/retained_presentation.hpp"
#include "cw/lib/encoding.hpp"
#include "cw/lib/python_integer.hpp"
#include "cw/lib/ui_surface.hpp"

#include "cw/cw.hpp"
#include "cw/data.hpp"
#include "cw/event.hpp"
#include "cw/frame.hpp"
#include "cw/setting.hpp"
#include "cw/sprite/card.hpp"
#include "cw/thread.hpp"
#include "cw/util.hpp"
#include "app/debug_control_server.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>
#include <stdexcept>
#include <string>
#include <memory>

namespace cw::debug::event {
namespace {

using ElementPtr = std::shared_ptr<::cw::data::CWPyElement>;
using Integer = ::cw::lib::Integer;
using String = ::cw::lib::String;

String value_string(const ::cw::lib::Value& value)
{
    if (const auto* string = std::get_if<String>(&value.storage())) return *string;
    return {};
}

EventTreeNode* selected(EventList& list)
{
    auto* node = &list.m_root;
    for (const auto index : list.m_selection_path) {
        if (index >= node->children.size()) return nullptr;
        node = &node->children[index];
    }
    return node;
}

String pathkey(String value)
{
#ifdef _WIN32
    std::replace(value.begin(), value.end(), '/', '\\');
    value = ::cw::lib::encoding::lower(value);
#endif
    // POSIX の os.path.normcase() と同様、ここでは大文字小文
    // 字を変換しない。 小文字化すると、大文字小文字を区別するファイルシステム上の Case.wsm
    // と case.wsm などが、同じ初期選択キーになってしまう。
    return value;
}

Integer python_integer(const String& text, const String& context)
{
    const auto normalized = ::cw::lib::normalize_python_integer(text);
    if (!normalized) throw std::invalid_argument(context + text);
    try {
        return ::cw::lib::Decimal::parse(*normalized).to_integer(::cw::lib::DecimalRounding::Down);
    } catch (const ::cw::lib::DecimalError&) {
        throw std::out_of_range(context + text);
    }
}

void log_model_error(const String& context) noexcept
{
    try {
        if (::cw::g_cwpy && ::cw::g_cwpy->m_backend)
            ::cw::g_cwpy->m_backend->log_error(*::cw::g_cwpy, context);
    } catch (...) {
        // 診断コールバックから別の例外を発生させ、不正なシナ
        // リオデータのエラーを UI 境界へ波及させない。
    }
    try {
        ::cw::util::print_ex(std::nullopt);
    } catch (...) {
    }
}

void append_error_node(EventTreeNode& parent)
{
    EventTreeNode error;
    error.label = "読込に失敗しました";
    error.resource_kind = "Error";
    error.expanded = true;
    parent.children.push_back(std::move(error));
}

std::shared_ptr<::cw::data::ScenarioData> current_scenario()
{
    if (!::cw::g_cwpy) return {};
    const auto* object = std::get_if<::cw::lib::ObjectPtr>(&::cw::g_cwpy->m_sdata.storage());
    if (!object || !*object) return {};
    return std::dynamic_pointer_cast<::cw::data::ScenarioData>(*object);
}

ElementPtr child(const ElementPtr& parent, const String& tag)
{
    if (!parent) return {};
    for (const auto& node : parent->m_children) {
        if (node && node->m_tag == tag) return node;
    }
    return {};
}

String decode_wrap(const String& value)
{
    String result;
    bool escaped = false;
    for (const auto c : value) {
        if (escaped) {
            if (c == 'n') result.push_back('\n');
            else if (c == '\\') result.push_back('\\');
            else result.push_back(c);
            escaped = false;
        } else if (c == '\\') {
            escaped = true;
        } else {
            result.push_back(c);
        }
    }
    // Pythonのdecodewrap()は末尾に単独で残ったバックスラッシュを
    // 破棄する。XMLの不完全なエスケープも同じ結果にそろえる。
    return result;
}

std::vector<String> decode_text_list(const String& value)
{
    std::vector<String> result;
    if (value.empty()) return result;
    const auto decoded = decode_wrap(value);
    String current;
    for (const auto c : decoded) {
        if (c == '\n') {
            result.push_back(std::move(current));
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    result.push_back(std::move(current));
    return result;
}

void initialize_event_metadata(const std::shared_ptr<::cw::event::Event>& event,
                               const ElementPtr& data)
{
    if (!event) return;
    event->m_is_active = false;
    if (!data) return;

    event->m__event = ::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(data));

    if (::cw::g_cwpy && ::cw::g_cwpy->is_playingscenario()) {
        const auto scenario = current_scenario();
        if (scenario) {
            event->m_scenario = value_string(scenario->m_name);
            event->m_author = value_string(scenario->m_author);
        }
    }

    const auto ignitions = child(data, "Ignitions");
    if (ignitions) {
        if (const auto number = child(ignitions, "Number")) {
            event->m_keynums.clear();
            for (const auto& value : decode_text_list(number->get_text())) {
                if (value.empty()) continue;
                event->m_keynums.push_back(
                    python_integer(value, "invalid event ignition number: "));
            }
        }
        if (const auto keycodes = child(ignitions, "KeyCodes")) {
            event->m_keycodes = decode_text_list(keycodes->get_text());
            event->m_keycodes.erase(std::remove_if(event->m_keycodes.begin(), event->m_keycodes.end(),
                                                    [](const String& value) { return value.empty(); }),
                                    event->m_keycodes.end());
        }
        event->m_keycode_matching = ignitions->get("keyCodeMatchingType", "Or");
    }

    const auto contents = child(data, "Contents");
    if (!contents) throw std::invalid_argument("event XML has no Contents element");
    for (const auto& content : contents->m_children) {
        if (!content) continue;
        String name;
        if (content->m_tag == "ContentsLine" && !content->m_children.empty() && content->m_children.front())
            name = content->m_children.front()->get("name", "");
        else
            name = content->get("name", "");
        if (!event->m_trees.contains(name)) {
            event->m_trees[name] = content;
            event->m_treekeys.push_back(name);
        }
        if (!event->m_starttree) {
            event->m_starttree = content;
            event->m_cur_content = content;
        }
    }
}

std::shared_ptr<::cw::event::Event> make_event(const ElementPtr& data,
                                               const ElementPtr& flags,
                                               const ElementPtr& steps,
                                               const ElementPtr& variants)
{
    if (!data) return {};
    auto event = std::make_shared<::cw::event::Event>();
    // リソース XML を厳密に検証し、メタデータだけを持つ非実行状態のデバッガー
    // 用イベントを作る。 その後、下で選択リソースの変数を関連付ける。実行時の Event::initialize
    // も decodetextlist を使うため、 このメタデ
    // ータ処理は発火条件の分割方法の違いを回避するものではない。
    event->initialize(std::nullopt);
    initialize_event_metadata(event, data);

    const auto parent_value = [](const ElementPtr& element) -> std::optional<::cw::lib::Value> {
        if (!element) return std::nullopt;
        if (element->m_cwxparent && *element->m_cwxparent)
            return ::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(*element->m_cwxparent));
        if (const auto parent = element->m_parent.lock())
            return ::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(parent));
        return std::nullopt;
    };
    if (flags) {
        if (const auto value = parent_value(flags)) event->m_flags = ::cw::data::init_flags(*value, false);
        else throw std::logic_error("event Flags has no XML parent");
    }
    if (steps) {
        if (const auto value = parent_value(steps)) event->m_steps = ::cw::data::init_steps(*value, false);
        else throw std::logic_error("event Steps has no XML parent");
    }
    if (variants) {
        if (const auto value = parent_value(variants)) event->m_variants = ::cw::data::init_variants(*value, false);
        else throw std::logic_error("event Variants has no XML parent");
    }
    return event;
}

void append_ignition_nodes(EventTreeNode& parent,
                           const std::shared_ptr<::cw::event::Event>& event,
                           const String& tag)
{
    if (!event) return;
    for (const auto keynum : event->m_keynums) {
        String name;
        if (tag == "Area") {
            if (keynum == 1) name = "到着";
        } else if (tag == "Battle") {
            if (keynum == 1) name = "勝利";
            else if (keynum == 2) name = "逃走";
            else if (keynum == 3) name = "敗北";
            else if (keynum == 4) name = "毎ラウンド";
            else if (keynum == 5) name = "バトル開始";
        } else if (tag == "MenuCard" || tag == "LargeMenuCard") {
            if (keynum == 1) name = "クリック";
        } else if (tag == "EnemyCard" || tag == "PlayerCardEvents") {
            if (keynum == 1) name = "死亡";
        }
        if (name.empty()) continue;
        EventTreeNode node;
        node.label = std::move(name);
        node.event = event;
        node.resource_kind = "Ignition";
        node.expanded = true;
        parent.children.push_back(std::move(node));
    }
    for (const auto& keycode : event->m_keycodes) {
        if (keycode == "MatchingType=All") continue;
        EventTreeNode node;
        node.label = keycode;
        node.event = event;
        node.resource_kind = "KeyCode";
        node.expanded = true;
        parent.children.push_back(std::move(node));
    }
    for (const auto keynum : event->m_keynums) {
        if (0 <= keynum) continue;
        EventTreeNode node;
        node.label = "ラウンド " + std::to_string(-keynum);
        node.event = event;
        node.resource_kind = "Round";
        node.expanded = true;
        parent.children.push_back(std::move(node));
    }
}

void append_event_node(EventTreeNode& parent,
                       const ElementPtr& data,
                       const String& tag,
                       const ElementPtr& flags,
                       const ElementPtr& steps,
                       const ElementPtr& variants)
{
    const auto event = make_event(data, flags, steps, variants);
    if (!event || event->m_treekeys.empty()) return;

    event->m_is_active = false;
    EventTreeNode node;
    node.label = event->m_treekeys.front();
    node.event = event;
    node.resource_kind = "Event";
    node.expanded = true;
    append_ignition_nodes(node, event, tag);
    parent.children.push_back(std::move(node));
}

void append_event_node_safe(EventTreeNode& parent,
                            const ElementPtr& data,
                            const String& tag,
                            const ElementPtr& flags,
                            const ElementPtr& steps,
                            const ElementPtr& variants)
{
    try {
        append_event_node(parent, data, tag, flags, steps, variants);
    } catch (...) {
        log_model_error("debug event: event entry could not be loaded");
        append_error_node(parent);
    }
}

void append_events(EventTreeNode& parent,
                   const ElementPtr& events,
                   const String& tag,
                   const ElementPtr& flags,
                   const ElementPtr& steps,
                   const ElementPtr& variants,
                   bool virtual_mode)
{
    if (!events) return;
    for (const auto& event : events->m_children) {
        append_event_node_safe(parent, event, tag, flags, steps, variants);
        if (virtual_mode) break;
    }
}

void append_player_card_events(EventTreeNode& parent, const ElementPtr& data)
{
    const auto player_events = child(data, "PlayerCardEvents");
    if (!player_events) return;
    for (const auto& event_group : player_events->m_children) {
        if (!event_group) continue;
        EventTreeNode group;
        group.label = "プレイヤーカード";
        group.resource_kind = "Card";
        group.expanded = true;
        for (const auto& event : event_group->m_children)
            append_event_node_safe(group, event, event_group->m_tag, {}, {}, {});
        parent.children.push_back(std::move(group));
    }
}

String card_name(const ElementPtr& card)
{
    if (!card) return {};
    if (card->m_tag == "EnemyCard") {
        const auto property = child(card, "Property");
        const auto id = property ? child(property, "Id") : ElementPtr{};
        Integer card_id = 0;
        if (id && !id->get_text().empty()) {
            const auto text = id->get_text();
            card_id = python_integer(text, "invalid EnemyCard Property/Id: ");
        }
        if (const auto scenario = current_scenario()) {
            const auto name = scenario->get_castname(card_id);
            if (name) return *name;
        }
        return "(未設定)";
    }
    const auto property = child(card, "Property");
    const auto name = property ? child(property, "Name") : ElementPtr{};
    return name ? name->get_text() : String{};
}

void append_card_events(EventTreeNode& parent,
                        const ElementPtr& data,
                        bool show_all_cards)
{
    for (const auto& container_tag : {String("MenuCards"), String("EnemyCards")}) {
        const auto cards = child(data, container_tag);
        if (!cards) continue;
        for (const auto& card : cards->m_children) {
            if (!card) continue;
            if (!show_all_cards) {
                try {
                    if (!::cw::sprite::card::CWPyCard::is_flagtrue_static(card)) continue;
                } catch (...) {
                    log_model_error("debug event: card visibility flag is invalid");
                    append_error_node(parent);
                    continue;
                }
            }
            try {
                EventTreeNode card_node;
                card_node.label = card_name(card);
                card_node.resource_kind = "Card";
                card_node.expanded = true;
                const auto events = child(card, "Events");
                if (!events) throw std::invalid_argument("card XML has no Events element");
                for (const auto& event : events->m_children)
                    // Pythonのappend(item, ee, ce.tag)と同じく、カードイベント
                    // にはシナリオ側のFlags/Steps/Variantsを渡さない。
                    append_event_node_safe(card_node, event, card->m_tag, {}, {}, {});
                parent.children.push_back(std::move(card_node));
            } catch (...) {
                log_model_error("debug event: card entry could not be loaded");
                append_error_node(parent);
            }
        }
    }
}

ElementPtr load_resource(EventTreeNode& node)
{
    const auto scenario = current_scenario();
    if (!scenario) return {};

    std::optional<ElementPtr> data;
    if (node.resource_kind == "Area") data = scenario->get_areadata(node.resource_id, "", false, std::nullopt);
    else if (node.resource_kind == "Battle") data = scenario->get_battledata(node.resource_id, "", false, std::nullopt);
    else if (node.resource_kind == "Package") data = scenario->get_packagedata(node.resource_id, "", false, std::nullopt);
    else if (node.resource_kind == "Skill") data = scenario->get_skilldata(node.resource_id, "", false, std::nullopt);
    else if (node.resource_kind == "Item") data = scenario->get_itemdata(node.resource_id, "", false, std::nullopt);
    else if (node.resource_kind == "Beast") data = scenario->get_beastdata(node.resource_id, "", false, std::nullopt);

    return data ? *data : ElementPtr{};
}

void expand_node(EventList& list, EventTreeNode& node, bool virtual_mode)
{
    try {
        if (node.expanded && !virtual_mode) return;
        node.children.clear();

        const auto data = load_resource(node);
        if (!data) {
            append_error_node(node);
            node.expanded = true;
            return;
        }

        const auto flags = child(data, "Flags");
        const auto steps = child(data, "Steps");
        const auto variants = child(data, "Variants");
        const auto events = child(data, "Events");
        if (!events) throw std::invalid_argument("resource XML has no Events element");
        append_events(node, events, data->m_tag, flags, steps, variants, virtual_mode);
        if (!virtual_mode) {
            append_player_card_events(node, data);
            append_card_events(node, data, list.m_showallcards);
        }
        node.expanded = !virtual_mode;
        if (!virtual_mode) node.native_expanded = true;
        if (node.children.empty()) {
            EventTreeNode loading;
            loading.label = "読込中...";
            node.children.push_back(std::move(loading));
        }
    } catch (...) {
        // ツリー通知は OS のコールバックである。不正な XML や
        // ID のエラーは該当リソース内で処理し、 WndProc
        // の外へ例外を出したり、ツリーの残りの処理を中断したりしない。
        log_model_error("debug event: resource could not be expanded");
        node.children.clear();
        append_error_node(node);
        node.expanded = true;
    }
}

std::optional<std::shared_ptr<::cw::event::Event>> direct_first_event(EventTreeNode& node)
{
    if (node.event) return node.event;
    if (!node.children.empty() && node.children.front().event) return node.children.front().event;
    return std::nullopt;
}

struct VisibleEventRow {
    EventTreeNode* node{};
    std::vector<std::size_t> path{};
    int depth{};
};

std::vector<VisibleEventRow> visible_event_rows(EventList& list)
{
    std::vector<VisibleEventRow> rows;
    std::vector<std::size_t> path;
    const auto walk=[&](const auto& self,EventTreeNode& node,const int depth)->void{
        rows.push_back({&node,path,depth});
        if(node.resource_root&&!node.native_expanded)return;
        for(std::size_t i=0;i<node.children.size();++i){path.push_back(i);self(self,node.children[i],depth+1);path.pop_back();}
    };
    for(std::size_t i=0;i<list.m_root.children.size();++i){path={i};walk(walk,list.m_root.children[i],0);}
    return rows;
}

String event_icon_name(const EventTreeNode& node)
{
    if(node.resource_kind=="Package")return "PACK";
    if(node.resource_kind=="Skill")return "EVT_GET_SKILL";
    if(node.resource_kind=="Item")return "EVT_GET_ITEM";
    if(node.resource_kind=="Beast")return "EVT_GET_BEAST";
    if(node.resource_kind=="Card")return "CARD";
    if(node.resource_kind=="Event")return "EVENT";
    if(node.resource_kind=="Ignition")return "IGNITION";
    if(node.resource_kind=="KeyCode")return "KEYCODE";
    if(node.resource_kind=="Round")return "ROUND";
    if(node.resource_kind=="Error")return "WARNING";
    return node.resource_kind;
}

void draw_event_icon(const std::shared_ptr<::cw::lib::Surface>& surface,const EventTreeNode& node,const Integer x,const Integer y)
{
    static std::map<String,std::shared_ptr<::cw::lib::Surface>> cache;
    const auto name=event_icon_name(node);if(name.empty())return;
    auto& icon=cache[name];
    if(!icon)icon=::cw::util::load_wxbmp("Data/Debugger/"+name+".png",true,{}, {},std::nullopt,true,false,false,::cw::lib::Value(1));
    if(icon)::cw::lib::ui::blend_surface(surface,icon,x,y,16,16);
}

} // namespace

namespace {

class CommonEventUiBackend final : public EventUiBackend {
public:
    void bind(EventListDialog&) override {}
    void layout(EventListDialog&) override {}
    bool show_modal(EventListDialog&) override { return false; }
};

} // namespace

std::shared_ptr<EventUiBackend> create_default_ui_backend()
{
    // UIの実装は一つに統一し、
    // 別経路の実装を設けない。
    return std::make_shared<CommonEventUiBackend>();
}

void EventList::initialize(std::shared_ptr<EventListDialog>,
                           std::tuple<Integer, Integer>,
                           String current,
                           bool show)
{
    m_showallcards = show;
    m_initial_fpath = std::move(current);
    m_root = EventTreeNode{};
    if (const auto scenario = current_scenario()) m_root.label = value_string(scenario->m_name);
    if (m_root.label.empty()) m_root.label = "Scenario";
    m_selection_path.clear();

    std::vector<EventTreeNode> resources;
    if (const auto scenario = current_scenario()) {
        auto append = [&](const String& kind,
                          std::vector<Integer> ids,
                          auto get_name,
                          auto get_fpath) {
            std::sort(ids.begin(), ids.end());
            for (const auto resid : ids) {
                if (resid < 0) continue;
                std::optional<String> name;
                std::optional<String> fpath;
                try {
                    name = get_name(resid);
                    fpath = get_fpath(resid);
                } catch (...) {
                    log_model_error("debug event: resource entry could not be read");
                    continue;
                }
                if (!name || !fpath) continue;
                EventTreeNode node;
                node.label = std::to_string(resid) + "." + *name;
                node.fpath = *fpath;
                node.resource_kind = kind;
                node.resource_id = resid;
                node.get_fpath = get_fpath;
                node.resource_root = true;
                resources.push_back(std::move(node));
            }
        };
        append("Area", scenario->get_areaids(),
               [&](Integer id) { return scenario->get_areaname(id); },
               [scenario](Integer id) { return scenario->get_areafpath(id); });
        append("Battle", scenario->get_battleids(),
               [&](Integer id) { return scenario->get_battlename(id); },
               [scenario](Integer id) { return scenario->get_battlefpath(id); });
        append("Package", scenario->get_packageids(),
               [&](Integer id) { return scenario->get_packagename(id); },
               [scenario](Integer id) { return scenario->get_packagefpath(id); });
        append("Skill", scenario->get_skillids(),
               [&](Integer id) { return scenario->get_skillname(id); },
               [scenario](Integer id) { return scenario->get_skillfpath(id); });
        append("Item", scenario->get_itemids(),
               [&](Integer id) { return scenario->get_itemname(id); },
               [scenario](Integer id) { return scenario->get_itemfpath(id); });
        append("Beast", scenario->get_beastids(),
               [&](Integer id) { return scenario->get_beastname(id); },
               [scenario](Integer id) { return scenario->get_beastfpath(id); });
    }
    set_resources(std::move(resources));
    _bind();
}

void EventList::set_resources(std::vector<EventTreeNode> resources)
{
    m_root.children = std::move(resources);
    m_selection_path.clear();
    const auto initial = pathkey(m_initial_fpath);
    for (std::size_t i = 0; i < m_root.children.size(); ++i) {
        auto& resource = m_root.children[i];
        resource.resource_root = true;
        if (!initial.empty() && pathkey(resource.fpath) == initial) {
            m_selection_path = {i};
            // Py は XML を読み込めなくても選択ルートを明示的に
            // 展開するため、エラーを示す子項目も表示したままにする。
            resource.native_expanded = true;
            expand_node(*this, resource, false);
        } else if (resource.children.empty()) {
            resource.children.push_back(EventTreeNode{"読込中..."});
        }
    }
}

void EventList::_bind()
{
    // ネイティブツリーの通知は所有するEventListDialogのbackendから各On*へ転送される。
    m_callbacks_bound = true;
}

void EventList::OnTreeItemExpanded(std::shared_ptr<::cw::lib::Window> event)
{
    const auto notification = std::dynamic_pointer_cast<EventTreeUiEvent>(event);
    const auto& path = notification ? notification->item_path : m_selection_path;
    if (path.size() != 1 || path.front() >= m_root.children.size()) return;
    auto& node = m_root.children[path.front()];
    if (!node.resource_root) return;
    node.native_expanded = true;
    _expand_item(event, false);
}

void EventList::OnTreeItemCollapsed(std::shared_ptr<::cw::lib::Window>)
{
    if (auto* node = selected(*this); node && node->resource_root)
        node->native_expanded = false;
}

void EventList::_expand_item(std::shared_ptr<::cw::lib::Window> item, bool py_virtual)
{
    const auto notification = std::dynamic_pointer_cast<EventTreeUiEvent>(item);
    const auto& path = notification ? notification->item_path : m_selection_path;
    if (path.size() != 1 || path.front() >= m_root.children.size()) return;
    expand_node(*this, m_root.children[path.front()], py_virtual);
}

void EventList::set_showallcards(bool value)
{
    if (m_showallcards == value) return;
    m_showallcards = value;
    for (auto& resource : m_root.children) {
        const bool was_native_expanded = resource.native_expanded;
        resource.expanded = false;
        resource.native_expanded = was_native_expanded;
        if (was_native_expanded) {
            expand_node(*this, resource, false);
        } else {
            resource.children.clear();
            resource.children.push_back(EventTreeNode{"読込中..."});
        }
    }
}

std::optional<std::shared_ptr<::cw::event::Event>> EventList::get_selectedevent()
{
    auto* node = selected(*this);
    if (!node) return std::nullopt;
    if (node->event) return node->event;
    if (node->resource_root && !node->expanded) _expand_item(nullptr, true);
    return direct_first_event(*node);
}

String EventList::get_currentfpath()
{
    if (m_selection_path.empty()) return {};
    const auto root_index = m_selection_path.front();
    if (root_index >= m_root.children.size()) return {};
    const auto& resource = m_root.children[root_index];
    if (resource.get_fpath)
        return resource.get_fpath(resource.resource_id).value_or(String{});
    // ネイティブの set_resources は、合成ツリーの利用側が渡す明示
    // 的な静的ノードも受け付ける。 通常のルートは必ず取得関数を関連付けて持つ。
    return resource.fpath;
}

void EventListDialog::initialize(std::shared_ptr<::cw::lib::Window> parent,
                                 String current,
                                 bool show)
{
    title = "イベントの選択";
    m_cwpy_debug = true;
    m_showhiddencards = show;
    m_start_event = false;
    m_modal_ok = false;
    m_presentation_finished = false;
    m_native_parent_handle = parent ? parent->native_handle : 0;
    m_events = std::make_shared<EventList>();
    m_events->initialize(nullptr, {250, 300}, std::move(current), show);
    m_presentation_scroll = 0;
    // Py の初期 ScrollTo(selected) と同様、先行する展開済
    // みリソースの子も含め、表示行の中から選択ルートの位置を求める。 共通 Presentation
    // はこのオフセットを実際の表示領域の範囲に収める。
    const auto rows = visible_event_rows(*m_events);
    const auto selected_row = std::find_if(rows.begin(), rows.end(), [&](const auto& row) {
        return row.path == m_events->m_selection_path;
    });
    if (selected_row != rows.end())
        m_presentation_scroll = static_cast<Integer>(std::distance(rows.begin(), selected_row));
    m_ui_backend = create_default_ui_backend();
    _changed_selection();
    _bind();
    _do_layout();
}

void EventListDialog::_do_layout()
{
    if (m_ui_backend) m_ui_backend->layout(*this);
}

void EventListDialog::_bind()
{
    if (m_ui_backend) m_ui_backend->bind(*this);
}

void EventListDialog::_changed_selection()
{
    const bool enabled = m_events && static_cast<bool>(m_events->get_selectedevent());
    m_open_enabled = enabled;
    m_start_enabled = enabled;
}

void EventListDialog::OnTreeSelChanged(std::shared_ptr<::cw::lib::Window>)
{
    _changed_selection();
}

void EventListDialog::OnShowAllCards(bool checked)
{
    m_showhiddencards = checked;
    if (m_events) m_events->set_showallcards(m_showhiddencards);
    _changed_selection();
}

void EventListDialog::OnStartBtn(std::shared_ptr<::cw::lib::Window>)
{
    if (m_events && m_events->get_selectedevent()) {
        if (::cw::g_cwpy)
            ::cw::g_cwpy->play_sound("signal", false, 100, 1, 0, 0, false);
        m_start_event = true;
        m_modal_ok = true;
    }
}

void EventListDialog::OnOpenBtn(std::shared_ptr<::cw::lib::Window> event)
{
    if (m_events && m_events->get_selectedevent()) {
        if (::cw::g_cwpy)
            ::cw::g_cwpy->play_sound("signal", false, 100, 1, 0, 0, false);
        m_start_event = false;
        m_modal_ok = true;
    } else if (m_events) {
        const auto pointer = std::dynamic_pointer_cast<EventTreeUiEvent>(event);
        // Py と同様、選択中の空リソースを展開するのはマウ
        // ス操作時だけとし、「開く」コマンドでは展開しない。
        if (pointer && pointer->mouse_event) {
            auto* node = selected(*m_events);
            if (node && node->resource_root && !node->native_expanded) {
                auto notification = std::make_shared<EventTreeUiEvent>();
                notification->item_path = m_events->m_selection_path;
                m_events->OnTreeItemExpanded(notification);
            }
        }
    }
}

bool EventListDialog::show_modal()
{
    if (!m_ui_backend) m_ui_backend = create_default_ui_backend();
    if (!m_ui_backend) return false;
    try {
        m_presentation_finished=false;
        (void)::cw::dialog::show_common_modal_presentation(shared_from_this());
        return m_modal_ok;
    } catch (...) {
        m_modal_ok = false;
        log_model_error("debug event: modal backend failed");
        return false;
    }
}

::cw::dialog::DialogPresentation EventListDialog::dialog_presentation()
{
    using Action=::cw::dialog::DialogPresentationAction;
    namespace retained_ui=::cw::debug::retained_presentation;
    const auto row_height=retained_ui::button_extent(22);
    const auto extra=(row_height-22)*16;
    const Integer width=360,height=430+extra;
    auto& retained=dialog_retained_surface();
    ::cw::lib::ui::ScaledRetainedCanvas canvas(
        retained,{width,height},retained_ui::dpi_scale(),{245,245,245,255});
    ::cw::dialog::DialogPresentation out;
    out.route="DEBUG_EVENT_SELECT";out.title=title;
    const ::cw::lib::Rect tree{8,8,270,370+extra};
    auto rows=m_events?visible_event_rows(*m_events):std::vector<VisibleEventRow>{};
    constexpr Integer visible_rows=16;
    const Integer max_scroll=std::max<Integer>(0,static_cast<Integer>(rows.size())-visible_rows);
    m_presentation_scroll=std::clamp<Integer>(m_presentation_scroll,0,max_scroll);
    std::uint64_t tree_signature=static_cast<std::uint64_t>(m_presentation_scroll+1);
    if(m_events)for(const auto part:m_events->m_selection_path)
        retained_ui::mix(tree_signature,static_cast<std::uint64_t>(part+1));
    for(const auto& item:rows){retained_ui::mix(tree_signature,static_cast<std::uint64_t>(item.depth+1));
        if(item.node){retained_ui::mix(tree_signature,item.node->label);
            retained_ui::mix(tree_signature,static_cast<std::uint64_t>(item.node->native_expanded));}
        for(const auto part:item.path)retained_ui::mix(tree_signature,static_cast<std::uint64_t>(part+1));}
    const ::cw::lib::Rect side{278,0,82,378+extra},footer{0,378+extra,360,52};
    const bool paint_tree=canvas.component_changed("debug.event.tree",tree,tree_signature);
    const bool paint_side=canvas.component_changed("debug.event.commands",side,
        static_cast<std::uint64_t>(m_open_enabled)|(static_cast<std::uint64_t>(m_start_enabled)<<1U));
    const bool paint_footer=canvas.component_changed("debug.event.footer",footer,
        static_cast<std::uint64_t>(m_showhiddencards));
    canvas.capture_base_and_restore();
    auto tree_layer=paint_tree?canvas.layer(tree,{255,255,255,255}):nullptr;
    if(tree_layer)::cw::lib::ui::draw_sunken_border(tree_layer,{0,0,tree.width,tree.height});
    for(Integer row=0;row<visible_rows;++row){
        const Integer absolute=m_presentation_scroll+row;
        if(absolute<0||absolute>=static_cast<Integer>(rows.size()))break;
        const auto& item=rows[static_cast<std::size_t>(absolute)];
        const bool selected_row=m_events&&item.path==m_events->m_selection_path;
        const ::cw::lib::Rect rect{tree.x+2,tree.y+2+row*row_height,
                                   tree.width-4,row_height};
        const ::cw::lib::Rect local{rect.x-tree.x,rect.y-tree.y,rect.width,rect.height};
        if(tree_layer&&selected_row)::cw::lib::ui::fill_rect(tree_layer,local,{210,225,248,255});
        out.controls.push_back({Action::Select,static_cast<int>(absolute),
            static_cast<float>(rect.x),static_cast<float>(rect.y),
            static_cast<float>(rect.width),static_cast<float>(rect.height),true,false});
        const Integer indent=local.x+4+item.depth*16;
        if(item.node&&item.node->resource_root){
            if(tree_layer)::cw::lib::ui::draw_label(tree_layer,item.node->native_expanded?"▼":"▶",indent,
                retained_ui::centered_y(local,12),12,false,{0,0,0});
            out.controls.push_back({Action::Auxiliary,10000+static_cast<int>(absolute),static_cast<float>(tree.x+indent),
                static_cast<float>(rect.y),static_cast<float>(retained_ui::button_extent(14)),static_cast<float>(rect.height),true,false,"",item.node->native_expanded?"折りたたむ":"展開する"});
        }
        if(tree_layer&&item.node)draw_event_icon(tree_layer,*item.node,indent+retained_ui::button_extent(14)+2,retained_ui::centered_y(local,16));
        if(tree_layer)::cw::lib::ui::draw_label(tree_layer,item.node?item.node->label:String{},indent+retained_ui::button_extent(14)+21,
            retained_ui::centered_y(local,13),13,false,{0,0,0});
    }
    if(tree_layer)canvas.composite(tree,tree_layer);
    auto side_layer=paint_side?canvas.layer(side,{245,245,245,255}):nullptr;
    const auto button=[&](const String& text,const ::cw::lib::Rect& rect,Action action,int index,bool enabled){
        if(side_layer){::cw::dialog::DialogPresentation ignored;
            ::cw::dialog::add_presentation_control(ignored,side_layer,text,
                {rect.x-side.x,rect.y-side.y,rect.width,rect.height},action,index,enabled,"button",true);}
        out.controls.push_back({action,index,static_cast<float>(rect.x),static_cast<float>(rect.y),
            static_cast<float>(rect.width),static_cast<float>(rect.height),enabled,false});};
    if(max_scroll>0){
        button("▲",{282,8,30,25},Action::Previous,-1,true);
        button("▼",{282,353+extra,30,25},Action::Next,-1,true);
    }
    button("開く",{282,45,70,28},Action::Auxiliary,0,m_open_enabled);
    button("実行",{282,80,70,28},Action::Auxiliary,1,m_start_enabled);
    if(side_layer)canvas.composite(side,side_layer);
    const ::cw::lib::Rect show_hidden{8,385+extra,270,28};
    auto footer_layer=paint_footer?canvas.layer(footer,{245,245,245,255}):nullptr;
    if(footer_layer){::cw::lib::ui::draw_checkbox(footer_layer,
        {show_hidden.x-footer.x,show_hidden.y-footer.y+7,14,14},m_showhiddencards);
        ::cw::lib::ui::draw_label(footer_layer,"表示フラグがオフのカードも表示する",
            show_hidden.x-footer.x+20,show_hidden.y-footer.y+7,13,false,{0,0,0});
        ::cw::dialog::DialogPresentation ignored;
        ::cw::dialog::add_presentation_control(ignored,footer_layer,"キャンセル",
            {282-footer.x,385+extra-footer.y,70,28},Action::Cancel,-1,true,"button",true);}
    out.controls.push_back({Action::Toggle,0,static_cast<float>(show_hidden.x),
        static_cast<float>(show_hidden.y),static_cast<float>(show_hidden.width),
        static_cast<float>(show_hidden.height),true,false,"表示フラグがオフのカードも表示する",{}});
    out.controls.push_back({Action::Cancel,-1,282.0F,static_cast<float>(385+extra),70.0F,28.0F,true,false});
    if(footer_layer)canvas.composite(footer,footer_layer);
    retained_ui::finish(out,canvas);
    return out;
}

void EventListDialog::dialog_presentation_action(
    ::cw::dialog::DialogPresentationAction action,int index)
{
    using Action=::cw::dialog::DialogPresentationAction;
    if(m_presentation_finished)return;
    if(action==Action::Cancel){m_modal_ok=false;m_presentation_finished=true;visible=false;return;}
    if(action==Action::Previous){m_presentation_scroll=std::max<Integer>(0,m_presentation_scroll-8);return;}
    if(action==Action::Next){m_presentation_scroll+=8;return;}
    if(action==Action::Toggle){OnShowAllCards(!m_showhiddencards);return;}
    if(action==Action::Auxiliary){
        if(index>=10000&&m_events){
            const auto rows=visible_event_rows(*m_events);const int row=index-10000;
            if(row>=0&&row<static_cast<int>(rows.size())&&rows[row].node&&rows[row].node->resource_root){
                m_events->m_selection_path=rows[row].path;auto& node=*rows[row].node;
                node.native_expanded=!node.native_expanded;if(node.native_expanded&&!node.expanded)expand_node(*m_events,node,false);
                OnTreeSelChanged(nullptr);
            }
            return;
        }
        if(index==0)OnOpenBtn(nullptr);else if(index==1)OnStartBtn(nullptr);
        if(m_modal_ok){m_presentation_finished=true;visible=false;}
        return;
    }
    if(!m_events || (action!=Action::Select && action!=Action::Activate))return;
    const auto rows=visible_event_rows(*m_events);
    if(index<0||index>=static_cast<int>(rows.size()))return;
    m_events->m_selection_path=rows[index].path;OnTreeSelChanged(nullptr);
    if(action==Action::Activate){
        auto pointer = std::make_shared<EventTreeUiEvent>();
        pointer->mouse_event = true;
        pointer->item_path = m_events->m_selection_path;
        OnOpenBtn(pointer);
        if(m_modal_ok){m_presentation_finished=true;visible=false;}
    }
}

void EventListDialog::dialog_presentation_key(const ::cw::dialog::DialogPresentationKey key,const bool pressed,
    const ::cw::dialog::DialogPresentationModifiers modifiers)
{
    using Key=::cw::dialog::DialogPresentationKey;using Action=::cw::dialog::DialogPresentationAction;
    if(!pressed||m_presentation_finished||!m_events)return;
    if(key==Key::Escape){dialog_presentation_action(Action::Cancel,-1);return;}
    if(key==Key::Enter){dialog_presentation_enter(modifiers);return;}
    auto rows=visible_event_rows(*m_events);if(rows.empty())return;
    auto it=std::find_if(rows.begin(),rows.end(),[&](const auto& row){return row.path==m_events->m_selection_path;});
    int current=it==rows.end()?0:static_cast<int>(std::distance(rows.begin(),it));
    int next=current;
    if(key==Key::Up)next=current-1;else if(key==Key::Down)next=current+1;
    else if(key==Key::Home)next=0;else if(key==Key::End)next=static_cast<int>(rows.size())-1;
    else if(key==Key::PageUp)next=current-16;else if(key==Key::PageDown)next=current+16;
    else if(key==Key::Right&&rows[current].node&&rows[current].node->resource_root&&!rows[current].node->native_expanded){
        dialog_presentation_action(Action::Auxiliary,10000+current);return;
    }else if(key==Key::Left&&rows[current].node&&rows[current].node->resource_root&&rows[current].node->native_expanded){
        dialog_presentation_action(Action::Auxiliary,10000+current);return;
    }else if(key==Key::Left&&!rows[current].path.empty()){
        auto parent=rows[current].path;parent.pop_back();const auto parent_it=std::find_if(rows.begin(),rows.end(),[&](const auto& row){return row.path==parent;});
        if(parent_it!=rows.end())next=static_cast<int>(std::distance(rows.begin(),parent_it));else return;
    }else return;
    next=std::clamp(next,0,static_cast<int>(rows.size())-1);m_events->m_selection_path=rows[next].path;
    m_presentation_scroll=std::clamp<::cw::lib::Integer>(m_presentation_scroll,
        static_cast<::cw::lib::Integer>(next-15),static_cast<::cw::lib::Integer>(next));
    OnTreeSelChanged(nullptr);
}

void EventListDialog::dialog_presentation_enter(::cw::dialog::DialogPresentationModifiers)
{
    // Enter は「開く」コマンドを実行し、wx.MouseEvent の代替動作である展開は行わない。
    dialog_presentation_action(::cw::dialog::DialogPresentationAction::Auxiliary, 0);
}

void EventListDialog::dialog_presentation_scroll(float,float,int lines)
{
    m_presentation_scroll=std::max<Integer>(0,m_presentation_scroll+lines);
}

bool EventListDialog::dialog_presentation_finished() const noexcept
{
    return m_presentation_finished;
}

void EventListDialog::dialog_presentation_complete() {}

} // namespace cw::debug::event
