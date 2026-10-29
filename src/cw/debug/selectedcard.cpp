#include "cw/debug/selectedcard.hpp"
#include "cw/debug/retained_presentation.hpp"
#include "cw/header.hpp"
#include "cw/lib/ui_surface.hpp"
#include "cw/util.hpp"

#include <algorithm>
#include <map>
#include <memory>

namespace cw::debug::selectedcard {
void SelectedCardUiBackend::show(SelectedCardDialog*){} void SelectedCardUiBackend::layout(SelectedCardDialog*){} void SelectedCardUiBackend::refresh(SelectedCardDialog*){} void SelectedCardUiBackend::close(SelectedCardDialog*){}
namespace { class DefaultBackend final:public SelectedCardUiBackend{public:
void show(SelectedCardDialog*)override{}void layout(SelectedCardDialog*)override{}
void refresh(SelectedCardDialog*)override{}void close(SelectedCardDialog*)override{} };
// UIの実装は一つに統一し、
// 別経路の実装を設けない。
std::vector<std::size_t> visible_nodes(const SelectedCardDialog& dialog){
    std::vector<std::size_t> visible;
    for(std::size_t i=1;i<dialog.m_tree.size();++i){const auto& node=dialog.m_tree[i];
        if(node.kind==SelectedCardDialog::NodeKind::Category)visible.push_back(i);
        else if(node.kind==SelectedCardDialog::NodeKind::Card&&node.parent<dialog.m_tree.size()&&dialog.m_tree[node.parent].expanded)visible.push_back(i);
    }return visible;
}
void draw_icon(const std::shared_ptr<::cw::lib::Surface>& surface,const ::cw::lib::String& name,
    const ::cw::lib::Integer x,const ::cw::lib::Integer y){
    static std::map<::cw::lib::String,std::shared_ptr<::cw::lib::Bitmap>> cache;
    if(name.empty()||name=="UNKNOWN")return;auto& icon=cache[name];
    if(!icon)icon=::cw::util::load_wxbmp("Data/Debugger/"+name+".png",true,{}, {},std::nullopt,true,false,false,::cw::lib::Value(1));
    if(icon)::cw::lib::ui::blend_surface(surface,icon,x,y,16,16);
}
}
std::shared_ptr<SelectedCardUiBackend> get_ui_backend(){static const auto backend=std::make_shared<DefaultBackend>();return backend;}

SelectedCardDialog::~SelectedCardDialog()
{
    // モデルを破棄する前に、注入されたホストアダプ
    // ターが表示状態を解放する最後の機会を設ける。
    try {
        if (const auto backend = get_ui_backend()) backend->close(this);
    } catch (...) {
    }
}

void SelectedCardDialog::initialize(
    std::shared_ptr<::cw::lib::Window> arg_parent,
    std::vector<std::tuple<::cw::lib::String, std::vector<std::shared_ptr<::cw::header::CardHeader>>>> arg_ccards,
    std::optional<std::shared_ptr<::cw::header::CardHeader>> arg_selectedcard)
{
    title = "選択カードの変更";
    m_native_parent_handle = arg_parent ? arg_parent->native_handle : 0;
    m_cwpy_debug = true;
    m__selectedcard = std::move(arg_selectedcard);
    m_tree.clear();
    m_selection.reset();
    m_scroll_index.reset();
    m_ok_enabled = false;
    m_modal_result = -1;
    m_presentation_finished = false;
    m_presentation_scroll = 0;

    m_tree.push_back({{}, {}, std::nullopt, 0, NodeKind::Root, true});
    for (std::size_t i = 0; i < arg_ccards.size(); ++i) {
        const auto category_index = m_tree.size();
        m_tree.push_back({std::get<0>(arg_ccards[i]), i == 0 ? "SACK" : "EVT_GET_CAST",
                          std::nullopt, 0, NodeKind::Category, 0 < i});

        for (const auto& header : std::get<1>(arg_ccards[i])) {
            if (!header) {
                m_tree.push_back({"(不明なカード)", "UNKNOWN", std::nullopt,
                                  category_index, NodeKind::Card, false, false});
                continue;
            }
            ::cw::lib::String icon;
            if (header->m_type == "ActionCard") icon = "CARD";
            else if (header->m_type == "SkillCard") icon = "EVT_GET_SKILL";
            else if (header->m_type == "ItemCard") icon = "EVT_GET_ITEM";
            else if (header->m_type == "BeastCard") icon = "EVT_GET_BEAST";
            else icon = "UNKNOWN";

            const auto card_index = m_tree.size();
            const bool selectable = icon != "UNKNOWN";
            m_tree.push_back({header->m_name, icon, header, category_index,
                              NodeKind::Card, false, selectable});
            if (selectable && m__selectedcard && *m__selectedcard == header)
                m_selection = card_index;
        }

        // wx.TreeCtrl と同様、カードがまだ選択されていなければカテゴリーを選択する。
        if (!m_selection) m_selection = category_index;
        if(m_selection&&*m_selection>category_index&&m_tree[*m_selection].parent==category_index)
            m_tree[category_index].expanded=true;
    }
    m_scroll_index = m_selection;
    _changed_selection();
    _bind();
    _do_layout();
}

bool SelectedCardDialog::show_modal()
{
    const auto backend = get_ui_backend();
    if (!backend) return false;
    try {
        backend->show(this);
        m_presentation_finished=false;
        (void)::cw::dialog::show_common_modal_presentation(shared_from_this());
        dialog_presentation_complete();
    } catch (...) {
        m_modal_result = kIdCancel;
        m_presentation_finished = true;
        visible = false;
        try { backend->close(this); } catch (...) {}
        return false;
    }
    return m_modal_result == kIdOk;
}

void SelectedCardDialog::_changed_selection()
{
    if (!m_selection || *m_selection >= m_tree.size() ||
        m_tree[*m_selection].kind != NodeKind::Card ||
        !m_tree[*m_selection].selectable || !m_tree[*m_selection].header) {
        m__selectedcard.reset();
        m_ok_enabled = false;
        get_ui_backend()->refresh(this);
        return;
    }
    m__selectedcard = *m_tree[*m_selection].header;
    m_ok_enabled = true;
    get_ui_backend()->refresh(this);
}

void SelectedCardDialog::set_selection_index(std::optional<::cw::lib::Integer> arg_index)
{
    if (!arg_index || *arg_index < 0 || static_cast<std::size_t>(*arg_index) >= m_tree.size())
        m_selection.reset();
    else
        m_selection = static_cast<std::size_t>(*arg_index);
    m_scroll_index = m_selection;
    _changed_selection();
}

void SelectedCardDialog::OnTreeSelChanged(std::shared_ptr<::cw::lib::Window> /*arg_event*/)
{
    _changed_selection();
}

void SelectedCardDialog::OnTreeDClick(std::shared_ptr<::cw::lib::Window> /*arg_event*/)
{
    if (m_presentation_finished) return;
    if (m_ok_enabled) {
        m_modal_result = kIdOk;
        m_presentation_finished = true;
        visible = false;
        if (m_on_ok) m_on_ok(m__selectedcard);
        get_ui_backend()->close(this);
    }
}

void SelectedCardDialog::OnOkBtn(std::shared_ptr<::cw::lib::Window> /*arg_event*/)
{
    if (m_presentation_finished) return;
    // Python のハンドラーは常に EndModal(ID_OK) を呼ぶ。
    // カテゴリーやルートの選択時は _changed_selection がボタ
    // ンを無効にするが、 直接呼び出した場合も同じモーダル結果の扱いを維持する。
    m_modal_result = kIdOk;
    m_presentation_finished = true;
    visible = false;
    if (m_on_ok) m_on_ok(m__selectedcard);
    get_ui_backend()->close(this);
}

std::optional<std::shared_ptr<::cw::header::CardHeader>> SelectedCardDialog::get_selectedcard()
{
    return m__selectedcard;
}

::cw::lib::Integer SelectedCardDialog::get_modal_result() const
{
    return m_modal_result;
}

void SelectedCardDialog::_do_layout()
{
    bounds = {0, 0, 360, 340};
    get_ui_backend()->layout(this);
}

void SelectedCardDialog::_bind()
{
    // 選択と実行は、一つの共通 Presentation を通して処理する。
}

::cw::dialog::DialogPresentation SelectedCardDialog::dialog_presentation()
{
    using Action=::cw::dialog::DialogPresentationAction;
    namespace retained_ui=::cw::debug::retained_presentation;
    const auto row_height=retained_ui::button_extent(22);
    const auto extra=(row_height-22)*12;
    const ::cw::lib::Integer width=360,height=340+extra;
    auto& retained=dialog_retained_surface();
    ::cw::lib::ui::ScaledRetainedCanvas canvas(
        retained,{width,height},retained_ui::dpi_scale(),{245,245,245,255});
    ::cw::dialog::DialogPresentation out;out.route="DEBUG_SELECTED_CARD";
    out.title=title.empty()?"カードの選択":title;
    const ::cw::lib::Rect tree{8,8,344,280+extra};
    const auto visible=visible_nodes(*this);
    constexpr ::cw::lib::Integer rows=12;
    const auto max_scroll=std::max<::cw::lib::Integer>(0,
        static_cast<::cw::lib::Integer>(visible.size())-rows);
    if(m_scroll_index){const auto it=std::find(visible.begin(),visible.end(),*m_scroll_index);
        if(it!=visible.end()){const auto pos=static_cast<::cw::lib::Integer>(std::distance(visible.begin(),it));
            m_presentation_scroll=std::clamp(m_presentation_scroll,pos-rows+1,pos);}m_scroll_index.reset();}
    m_presentation_scroll=std::clamp<::cw::lib::Integer>(m_presentation_scroll,0,max_scroll);
    std::uint64_t tree_signature{};
    retained_ui::mix(tree_signature,static_cast<std::uint64_t>(m_presentation_scroll+1));
    retained_ui::mix(tree_signature,static_cast<std::uint64_t>(m_selection.value_or(m_tree.size())+1));
    for(const auto index:visible){const auto& node=m_tree[index];
        retained_ui::mix(tree_signature,static_cast<std::uint64_t>(index+1));
        retained_ui::mix(tree_signature,node.label);retained_ui::mix(tree_signature,node.icon);
        retained_ui::mix(tree_signature,static_cast<std::uint64_t>(node.kind));
        retained_ui::mix(tree_signature,static_cast<std::uint64_t>(node.expanded));
        retained_ui::mix(tree_signature,static_cast<std::uint64_t>(node.selectable));}
    const ::cw::lib::Rect footer{180,292+extra,180,48};
    const bool paint_tree=canvas.component_changed("debug.selectedcard.tree",tree,tree_signature);
    const bool paint_footer=canvas.component_changed("debug.selectedcard.footer",footer,
        static_cast<std::uint64_t>(m_ok_enabled));
    canvas.capture_base_and_restore();
    auto tree_layer=paint_tree?canvas.layer(tree,{255,255,255,255}):nullptr;
    if(tree_layer)::cw::lib::ui::draw_sunken_border(tree_layer,{0,0,tree.width,tree.height});
    for(::cw::lib::Integer row=0;row<rows;++row){
        const auto pos=m_presentation_scroll+row;if(pos>=static_cast<::cw::lib::Integer>(visible.size()))break;
        const auto index=visible[static_cast<std::size_t>(pos)];const auto& node=m_tree[index];
        const ::cw::lib::Rect rect{tree.x+2,tree.y+2+row*row_height,tree.width-4,row_height};
        const ::cw::lib::Rect local{rect.x-tree.x,rect.y-tree.y,rect.width,rect.height};
        if(tree_layer&&m_selection&&*m_selection==index)::cw::lib::ui::fill_rect(tree_layer,local,{210,225,248,255});
        out.controls.push_back({Action::Select,static_cast<int>(index),
            static_cast<float>(rect.x),static_cast<float>(rect.y),
            static_cast<float>(rect.width),static_cast<float>(rect.height),node.selectable,false});
        const auto indent=local.x+4+(node.kind==NodeKind::Card?16:0);
        if(node.kind==NodeKind::Category){
            if(tree_layer)::cw::lib::ui::draw_label(tree_layer,node.expanded?"▼":"▶",indent,
                retained_ui::centered_y(local,12),12,false,{0,0,0});
            out.controls.push_back({Action::Auxiliary,10000+static_cast<int>(index),static_cast<float>(tree.x+indent),
                static_cast<float>(rect.y),static_cast<float>(retained_ui::button_extent(14)),static_cast<float>(rect.height),true,false,"",node.expanded?"折りたたむ":"展開する"});
        }
        if(tree_layer){draw_icon(tree_layer,node.icon,indent+retained_ui::button_extent(14)+2,retained_ui::centered_y(local,16));
            ::cw::lib::ui::draw_label(tree_layer,node.label,indent+retained_ui::button_extent(14)+21,retained_ui::centered_y(local,13),13,false,
                node.selectable ? std::tuple<::cw::lib::Integer,::cw::lib::Integer,::cw::lib::Integer>{0,0,0}
                                : std::tuple<::cw::lib::Integer,::cw::lib::Integer,::cw::lib::Integer>{128,128,128});}
    }
    if(tree_layer)canvas.composite(tree,tree_layer);
    auto footer_layer=paint_footer?canvas.layer(footer,{245,245,245,255}):nullptr;
    const auto add_button=[&](const ::cw::lib::String& text,const ::cw::lib::Rect& rect,Action action,bool enabled){
        if(footer_layer){::cw::dialog::DialogPresentation ignored;
            ::cw::dialog::add_presentation_control(ignored,footer_layer,text,
                {rect.x-footer.x,rect.y-footer.y,rect.width,rect.height},action,-1,enabled,"button",true);}
        out.controls.push_back({action,-1,static_cast<float>(rect.x),static_cast<float>(rect.y),
            static_cast<float>(rect.width),static_cast<float>(rect.height),enabled,false});};
    add_button("決定",{188,300+extra,78,28},Action::Accept,m_ok_enabled);
    add_button("中止",{274,300+extra,78,28},Action::Cancel,true);
    if(footer_layer)canvas.composite(footer,footer_layer);
    retained_ui::finish(out,canvas);return out;
}

void SelectedCardDialog::dialog_presentation_action(
    ::cw::dialog::DialogPresentationAction action,int index)
{
    using Action=::cw::dialog::DialogPresentationAction;
    if(m_presentation_finished)return;
    if(action==Action::Cancel){m_modal_result=kIdCancel;m_presentation_finished=true;visible=false;return;}
    if(action==Action::Accept){if(m_ok_enabled)OnOkBtn(nullptr);return;}
    if(action==Action::Previous){m_presentation_scroll=std::max<::cw::lib::Integer>(0,m_presentation_scroll-6);return;}
    if(action==Action::Next){m_presentation_scroll+=6;return;}
    if(action==Action::Auxiliary&&index>=10000){index-=10000;if(index<0||index>=static_cast<int>(m_tree.size()))return;
        auto& node=m_tree[static_cast<std::size_t>(index)];if(node.kind==NodeKind::Category){node.expanded=!node.expanded;m_selection=static_cast<std::size_t>(index);m_scroll_index=m_selection;OnTreeSelChanged(nullptr);}return;}
    if((action!=Action::Select&&action!=Action::Activate)||index<0||index>=static_cast<int>(m_tree.size()))return;
    auto& node=m_tree[static_cast<std::size_t>(index)];
    if(node.kind==NodeKind::Category)m_selection=static_cast<std::size_t>(index);
    else if(node.kind==NodeKind::Card)m_selection=static_cast<std::size_t>(index);
    m_scroll_index=m_selection;OnTreeSelChanged(nullptr);
    if(action==Action::Activate){if(node.kind==NodeKind::Card&&node.selectable)OnTreeDClick(nullptr);
        else if(node.kind==NodeKind::Category){node.expanded=!node.expanded;}}
}

void SelectedCardDialog::dialog_presentation_key(const ::cw::dialog::DialogPresentationKey key,const bool pressed,
    const ::cw::dialog::DialogPresentationModifiers modifiers){
    using Key=::cw::dialog::DialogPresentationKey;using Action=::cw::dialog::DialogPresentationAction;
    if(!pressed||m_presentation_finished)return;if(key==Key::Escape){dialog_presentation_action(Action::Cancel,-1);return;}
    if(key==Key::Enter){dialog_presentation_enter(modifiers);return;}const auto visible=visible_nodes(*this);if(visible.empty())return;
    auto it=m_selection?std::find(visible.begin(),visible.end(),*m_selection):visible.end();int pos=it==visible.end()?0:static_cast<int>(std::distance(visible.begin(),it));int next=pos;
    if(key==Key::Up)next=pos-1;else if(key==Key::Down)next=pos+1;else if(key==Key::Home)next=0;
    else if(key==Key::End)next=static_cast<int>(visible.size())-1;else if(key==Key::PageUp)next=pos-12;else if(key==Key::PageDown)next=pos+12;
    else {auto& node=m_tree[visible[pos]];if(key==Key::Right&&node.kind==NodeKind::Category&&!node.expanded){dialog_presentation_action(Action::Auxiliary,10000+static_cast<int>(visible[pos]));return;}
        if(key==Key::Left&&node.kind==NodeKind::Category&&node.expanded){dialog_presentation_action(Action::Auxiliary,10000+static_cast<int>(visible[pos]));return;}
        if(key==Key::Left&&node.kind==NodeKind::Card){m_selection=node.parent;m_scroll_index=m_selection;OnTreeSelChanged(nullptr);return;}return;}
    next=std::clamp(next,0,static_cast<int>(visible.size())-1);m_selection=visible[next];m_scroll_index=m_selection;OnTreeSelChanged(nullptr);
}

void SelectedCardDialog::dialog_presentation_enter(::cw::dialog::DialogPresentationModifiers){
    if(m_selection)dialog_presentation_action(::cw::dialog::DialogPresentationAction::Activate,static_cast<int>(*m_selection));
}

void SelectedCardDialog::dialog_presentation_scroll(float,float,int lines)
{
    m_presentation_scroll=std::max<::cw::lib::Integer>(0,m_presentation_scroll+lines);
}

bool SelectedCardDialog::dialog_presentation_finished() const noexcept
{
    return m_presentation_finished;
}

void SelectedCardDialog::dialog_presentation_complete()
{
    // ホスト側で閉じた場合は、開けなかった場合も含め、ユーザー操作で結果が
    // 確定済みでない限りキャンセルとする。確定済みの OK は維持する。
    if (!m_presentation_finished) m_modal_result = kIdCancel;
    m_presentation_finished = true;
    visible = false;
}

void main() {}

}
