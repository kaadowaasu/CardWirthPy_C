#pragma once
#include <array>

// UIの実装は一つに統一し、
// 別経路の実装を設けない。

// cw/dialog/scenarioselect.py
// の C++ 移植。wx に直接対応するものがない SDL
// 固有の描画フックは、呼び出し箇所で違いを明示する。
#include "cw/lib/compat.hpp"
#include "cw/fwd.hpp"
#include "cw/dialog/presentation.hpp"
#include "cw/dialog/filter_input.hpp"
#include "cw/dialog/scenarioinstall.hpp"

namespace cw::dialog::etc { struct BookmarkPresentation; }

namespace cw::dialog::scenarioselect {
class FindResult : public ::cw::lib::Object {
public:
    FindResult() = default;
    virtual ~FindResult() = default;
    std::vector<::cw::lib::Value> m_headers{};
    std::vector<std::shared_ptr<::cw::header::ScenarioHeader>> m_typed_headers{};
    void initialize();
};

using ScenarioCandidate = std::variant<std::shared_ptr<::cw::header::ScenarioHeader>,
                                       ::cw::lib::String,
                                       std::shared_ptr<::cw::dialog::scenarioselect::FindResult>>;
using ScenarioDirectory = std::variant<::cw::lib::String,
                                       std::shared_ptr<::cw::dialog::scenarioselect::FindResult>>;

// シナリオモデルと共通 Presentation が、唯一の選択 UI を所有
// する。このコントローラーは状態を表示し、ホスト入力をコールバックに変換する
// 。OS 固有コードは受け渡しサービスだけを提供し、画面自体は置き換えない。
enum class ScenarioSelectPosterKind {
    empty,
    scenario,
    directory,
    find_result,
};

enum class ScenarioInstallChoice {
    install,
    view,
    cancel,
};

struct ScenarioSelectTreeNodeState {
    ::cw::lib::String label{};
    ::cw::lib::String path{};
    ::cw::lib::Integer icon_index{-1};
    ::cw::lib::Integer candidate_index{-1};
    ::cw::lib::Integer parent_index{-1};
    bool expanded{};
    bool placeholder{};
    friend bool operator==(const ScenarioSelectTreeNodeState& a, const ScenarioSelectTreeNodeState& b) {
        return a.label == b.label && a.path == b.path && a.icon_index == b.icon_index &&
               a.candidate_index == b.candidate_index && a.parent_index == b.parent_index &&
               a.expanded == b.expanded && a.placeholder == b.placeholder;
    }
};

enum class ScenarioSelectUiEventKind {
    select,
    tree_select,
    search,
    narrow,
    sort,
    previous,
    previous2,
    next,
    next2,
    confirm,
    tree_activate,
    cancel,
    no,
    close,
    toggle_tree,
    toggle_unfitness,
    toggle_completed,
    toggle_invisible,
    find,
    install,
    create_directory,
    move,
    remove,
    rename,
    add_bookmark,
    arrange_bookmark,
    open_bookmark,
    open_directory,
    open_editor,
    convert,
    info,
    copy_detail,
    tree_expand,
    poster_animate,
    poster_select,
    toggle_addctrl,
    create_link_scenario,
    create_link_directory,
};

struct ScenarioSelectUiEvent {
    ScenarioSelectUiEventKind kind{ScenarioSelectUiEventKind::select};
    ::cw::lib::Integer index{};
    ::cw::lib::Integer value{};
    ::cw::lib::String text{};
    std::vector<::cw::lib::String> paths{};
};

// Python はタイトルと小さな灰色の注記を、別々の文字列として描く。
struct ScenarioSelectContentsRow {
    ::cw::lib::String name{};
    ::cw::lib::String addition{};
    bool gray{};
    bool operator==(const ScenarioSelectContentsRow&) const = default;
};

struct ScenarioSelectPosterLayer {
    std::shared_ptr<::cw::lib::Bitmap> bitmap{};
    ::cw::lib::Integer x{};
    ::cw::lib::Integer y{};
    bool animated{};
    bool operator==(const ScenarioSelectPosterLayer&) const = default;
};

struct ScenarioSelectUiState {
    std::vector<::cw::lib::String> items{};
    std::vector<::cw::lib::String> paths{};
    // 絞り込み前の件数と表示行を分離して保持する。Python
    // は未導入と全行非表示を区別する。
    bool source_has_items{};
    std::vector<::cw::lib::String> text_names{};
    std::vector<::cw::lib::Bytes> text_contents{};
    std::shared_ptr<::cw::lib::Bitmap> background{};
    // 貼り紙は一枚のサムネイルではなく、順序付きの画像合成。各レイヤー
    // は同じ静止画・動画の読み込み規則を使い、それぞれの CardWirth
    // の基準カード位置を保つ。preview は旧フィールドだけ
    // を参照する利用側のために、先頭レイヤーの互換表示として残す。
    std::vector<ScenarioSelectPosterLayer> poster_layers{};
    std::shared_ptr<::cw::lib::Bitmap> preview{};
    // Python版のディレクトリ貼り紙に対応するFolder.bmp。BMPは
    // load_wxbmp(..., mask=true)で色キーをアルファへ変換して保持する。
    std::shared_ptr<::cw::lib::Bitmap> directory_preview{};
    ::cw::lib::Integer directory_preview_x{};
    ::cw::lib::Integer directory_preview_y{};
    bool directory_link{};
    ::cw::lib::Integer poster_offset_x{};
    ::cw::lib::Integer poster_offset_y{};
    bool preview_animated{};
    ::cw::lib::Integer index{};
    ::cw::lib::Integer narrow_type{};
    ::cw::lib::Integer sort_type{};
    ::cw::lib::String search{};
    ::cw::lib::String detail{};
    ::cw::lib::String page_label{};
    bool tree_visible{};
    bool paper_and_tree{};
    bool show_additional_controls{};
    bool processing{};
    bool can_confirm{};
    bool confirm_enabled{};
    bool can_view{};
    bool can_info{};
    bool can_install{};
    bool can_move{};
    bool can_remove{};
    bool can_rename{};
    bool can_open_directory{};
    bool can_open_editor{};
    bool can_convert{};
    bool can_create_directory{};
    bool show_unfitness{};
    bool show_completed{};
    bool show_invisible{};
    std::array<std::shared_ptr<::cw::lib::Surface>, 3> filter_icons{};
    bool use_return_label{};
    ScenarioSelectPosterKind poster_kind{ScenarioSelectPosterKind::empty};
    ::cw::lib::String poster_title{};
    ::cw::lib::String poster_description{};
    ::cw::lib::String poster_level_text{};
    ::cw::lib::String poster_author{};
    bool poster_show_playing{};
    bool poster_show_complete{};
    bool poster_show_invisible{};
    std::vector<ScenarioSelectContentsRow> directory_contents{};
    ::cw::lib::String window_title{};
    ::cw::lib::String window_author{};
    bool can_click_center{};
    bool nav_previous_enabled{};
    bool nav_previous2_enabled{};
    bool nav_next_enabled{};
    bool nav_next2_enabled{};
    std::vector<ScenarioSelectTreeNodeState> tree_nodes{};
    ::cw::lib::Integer tree_selected_node_index{-1};
    ::cw::lib::Integer tree_first_row{};
    std::vector<::cw::lib::String> bookmark_items{};
    std::vector<::cw::lib::String> bookmark_icons{};
    std::vector<bool> bookmark_enabled{};
    bool operator==(const ScenarioSelectUiState&) const = default;
};

// 一つの ScenarioSelect Presentation が使う、UI 以外の
// サービス。共通の子 Presentation を開いたり、OS のクリップボード・
// シェルを呼んだりできるが、入力欄、コントロール、イベントキュー、配置は所有しない。
class ScenarioSelectHostServices : public ::cw::lib::Object {
public:
    ScenarioSelectHostServices() = default;
    virtual ~ScenarioSelectHostServices() = default;
    virtual bool confirm(const ::cw::lib::String& arg_title, const ::cw::lib::String& arg_message) = 0;
    virtual std::optional<::cw::lib::String> request_text(const ::cw::lib::String& arg_title,
                                                           const ::cw::lib::String& arg_initial) = 0;
    virtual ScenarioInstallChoice choose_install(const ::cw::lib::String& arg_title,
                                                 const ::cw::lib::String& arg_message,
                                                 const ::cw::lib::String& arg_view_message) {
        (void)arg_view_message;
        return confirm(arg_title, arg_message) ? ScenarioInstallChoice::install : ScenarioInstallChoice::cancel;
    }
    virtual ScenarioInstallChoice choose_install_with_detail(const ::cw::lib::String& title,
        const ::cw::lib::String& message, const ::cw::lib::String& view_message,
        const ::cw::lib::String& description) {
        (void)description;
        return choose_install(title,message,view_message);
    }
    virtual void message(const ::cw::lib::String& arg_title, const ::cw::lib::String& arg_message) {
        (void)arg_title;
        (void)arg_message;
    }
    virtual void show_readme(const std::vector<::cw::lib::String>& arg_names,
                             const std::vector<::cw::lib::Bytes>& arg_contents) {
        (void)arg_names;
        (void)arg_contents;
    }
    virtual void clipboard(const ::cw::lib::String& arg_text) = 0;
};

struct ScenarioSelectMenuEntry {
    ::cw::lib::String label;
    int command{};
    bool enabled{true};
    ::cw::lib::String icon;
};

std::shared_ptr<ScenarioSelectHostServices> get_host_services();

class ScenarioSelect : public ::cw::lib::Object,
                       public std::enable_shared_from_this<ScenarioSelect>,
                       public ::cw::dialog::DialogPresentationProvider {
public:
    ScenarioSelect() = default;
    virtual ~ScenarioSelect();
    inline static std::shared_ptr<::cw::lib::Window> s__add_bookmark{};
    inline static std::shared_ptr<::cw::lib::Window> s__arrange_bookmark{};
    inline static ::cw::lib::Integer s__saved_index{};
    inline static ::cw::lib::ValueMap s_scetable{};
    inline static std::shared_ptr<::cw::lib::Panel> s_toppanel{};
    ::cw::lib::Value m__add_bookmark{};
    ::cw::lib::Value m__addctrlbg{};
    ::cw::lib::Value m__arrange_bookmark{};
    ::cw::lib::Value m__bg{};
    ::cw::lib::Value m__bg_scaled{};
    ::cw::lib::Value m__create_link_to_dir{};
    ::cw::lib::Value m__create_link_to_scenario{};
    ::cw::lib::Value m__createdir{};
    ::cw::lib::Value m__delete{};
    ::cw::lib::Value m__editor{};
    ::cw::lib::Integer m__gray_idx{};
    ::cw::lib::Value m__imgidx_complete{};
    ::cw::lib::Value m__imgidx_dir{};
    ::cw::lib::Value m__imgidx_findresult{};
    ::cw::lib::Value m__imgidx_invisible{};
    ::cw::lib::Value m__imgidx_playing{};
    ::cw::lib::Value m__imgidx_summary{};
    ::cw::lib::Value m__install{};
    ::cw::lib::Value m__last_narrowparams{};
    ::cw::lib::Value m__move{};
    bool m__no_treechangedsound{};
    ::cw::lib::Value m__opendir{};
    ::cw::lib::Value m__poster_anim_header{};
    ::cw::lib::String m__poster_anim_owner{};
    bool m__poster_anim_stopped{};
    std::map<::cw::lib::Integer, std::tuple<::cw::lib::String, ::cw::lib::Integer, ::cw::lib::Integer>> m__poster_animations{};
    ::cw::lib::Value m__poster_animtimer{};
    bool m__processing{};
    bool m__quit{};
    ::cw::lib::Value m__rename{};
    ::cw::lib::Value m__root{};
    ::cw::lib::Value m__saved_dirstack{};
    ::cw::lib::Value m__saved_index{};
    ::cw::lib::Value m__saved_list{};
    std::vector<::cw::lib::Value> m_additionals2{};
    ::cw::lib::Value m_addmenu{};
    ::cw::lib::Value m_addmenubtn{};
    ::cw::lib::Value m_bmp_completed{};
    ::cw::lib::Value m_bmp_invisible{};
    ::cw::lib::Value m_bmp_unfitness{};
    ::cw::lib::Value m_bookmark{};
    ::cw::lib::Value m_bookmarkmenu{};
    ::cw::lib::Value m_bookmarkpath{};
    ::cw::lib::Value m_completed{};
    ::cw::lib::Value m_coupons{};
    ::cw::lib::Value m_db{};
    ::cw::lib::Value m_dbmp_completed{};
    ::cw::lib::Value m_dbmp_invisible{};
    ::cw::lib::Value m_dbmp_unfitness{};
    std::vector<std::tuple<::cw::lib::Value, ::cw::lib::String>> m_dirstack{};
    ::cw::lib::Value m_find{};
    std::optional<std::shared_ptr<::cw::dialog::scenarioselect::FindResult>> m_find_result{};
    ::cw::lib::Integer m_index{};
    ::cw::lib::Value m_infobtn{};
    ::cw::lib::Value m_invisible{};
    std::vector<::cw::lib::Value> m_list{};
    std::vector<::cw::lib::Value> m_names{};
    std::vector<::cw::lib::Value> m_narrowkeydown{};
    ::cw::lib::Value m_nobtn{};
    ::cw::lib::Value m_nowdir{};
    ::cw::lib::Value m_nowplayingpaths{};
    ::cw::lib::Value m_outer{};
    ::cw::lib::Value m_pagelabel{};
    ::cw::lib::Value m_scedir{};
    ::cw::lib::ValueMap m_scetable{};
    ::cw::lib::Value m_sort{};
    ::cw::lib::Value m_sort_label{};
    std::vector<::cw::lib::Value> m_sortkeydown{};
    ::cw::lib::Value m_stamps{};
    std::vector<std::shared_ptr<::cw::dialog::text::ReadmeData>> m_texts{};
    ::cw::lib::Value m_toppanel{};
    ::cw::lib::Value m_topsizer{};
    ::cw::lib::Value m_tree{};
    ::cw::lib::Value m_unfitness{};
    std::optional<std::shared_ptr<::cw::dialog::scenarioselect::UpdateNamesThread>> m_updatenames_thr{};
    ::cw::lib::Value m_viewbtn{};
    ::cw::lib::Value m_yesbtn{};
    void initialize(std::shared_ptr<::cw::lib::Window> arg_parent, std::shared_ptr<::cw::scenariodb::Scenariodb> arg_db, std::vector<::cw::lib::String> arg_lastscenario, ::cw::lib::String arg_lastscenariopath, std::vector<::cw::lib::String> arg_lastfindresult);
    void set_scenario_directory(::cw::lib::String arg_scedir);
    void set_search(::cw::lib::String arg_text, ::cw::lib::Integer arg_narrow_type, bool user_action = false);
    void set_filter(bool arg_show_unfitness, bool arg_show_completed, bool arg_show_invisible, ::cw::lib::Integer arg_average_level);
    void set_sort(::cw::lib::Integer arg_sort_type, bool user_action = false);
    void set_completion_state(std::set<::cw::lib::String> arg_stamps,
                              std::set<::cw::lib::String> arg_nowplaying,
                              std::map<::cw::lib::String, ::cw::lib::Integer> arg_coupons);
    void set_debug_mode(bool arg_debug);
    bool select_index(::cw::lib::Integer arg_index);
    bool can_decide() const;
    bool confirm_selection();
    bool cancel_selection();
    ::cw::lib::Integer selected_index() const;
    ::cw::lib::Integer page_count() const;
    std::vector<::cw::lib::String> selected_paths() const;
    bool is_showingaddctrl() const;
    bool is_tree_visible() const;
    void update_additionals();
    void OnAdditionalControls(std::shared_ptr<::cw::lib::Window> arg_event);
    ::cw::lib::Value _get_linktarget(::cw::lib::Value arg_path);
    void OnCopyDetail(std::shared_ptr<::cw::lib::Window> arg_event);
    void copy_detail();
    [[nodiscard]] bool dialog_presentation_can_copy_detail() const noexcept override { return true; }
    void dialog_presentation_copy_detail() override { copy_detail(); }
    void OnDebugMode(std::shared_ptr<::cw::lib::Window> arg_event);
    void update_debug();
    void OnEscape(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnUnfitnessBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnCompletedBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnInvisibleBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnPrevButton(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnNextButton(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnMouseWheel(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnUpKeyDown(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnDownKeyDown(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnNumberKeyDown(std::shared_ptr<::cw::lib::Window> arg_event);
    void _do_layout();
    void _add_topsizer();
    std::shared_ptr<::cw::lib::Window> _sizer_top();
    std::shared_ptr<::cw::lib::Window> _sizer_find();
    ::cw::lib::Integer _get_findpanelheight();
    void OnFind(std::shared_ptr<::cw::lib::Window> arg_event);
    void _set_findresult(std::vector<::cw::lib::Value> arg_headers, bool arg_selfirstheader, bool arg_expand, ::cw::lib::Integer arg_selindex);
    void OnAdditionalMenu(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnAdditionalMenu2(std::shared_ptr<::cw::lib::Window> arg_event);
    void _create_addmenu();
    void OnBookmark(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnBookmark2(std::shared_ptr<::cw::lib::Window> arg_event);
    bool _is_specialselected();
    void create_bookmarkmenu();
    void OnAddBookmark(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnArrangeBookmark(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnOpenBookmark(::cw::lib::Integer arg_index);
    [[nodiscard]] ::cw::dialog::etc::BookmarkPresentation project_bookmark(
        const std::tuple<std::vector<::cw::lib::String>, ::cw::lib::String>& bookmark);
    void OnLeftDClick(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnLeftDClick2(std::shared_ptr<::cw::lib::Window> arg_event);
    void _tree_dclick();
    void OnKeyDown(std::shared_ptr<::cw::lib::Window> arg_event);
    bool can_clickcenter();
    std::tuple<std::vector<::cw::lib::String>, ::cw::lib::String> get_selected();
    std::vector<::cw::lib::String> get_findresult();
    std::vector<::cw::lib::Value> _get_nowlist(std::optional<::cw::lib::Value> arg_nowdir, bool arg_update);
    void set_selected(std::vector<::cw::lib::String> arg_spaths, ::cw::lib::String arg_fullpath, bool arg_opendir, bool arg_updatetree, std::optional<std::vector<::cw::lib::Value>> arg_findresults);
    void _update_saveddirstack();
    void index_changed();
    void OnDropFiles(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnInstallBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    enum class DocumentAction { Install, Move, Convert, LinkScenario, LinkDirectory };
    using DocumentRequest = std::function<std::shared_ptr<::cw::lib::documents::Ticket>(
        const ::cw::lib::documents::Request&, ::cw::lib::documents::Completion)>;
    bool request_document(DocumentAction action, DocumentRequest start = {});
    void cancel_document_request();
    [[nodiscard]] bool document_request_pending() const noexcept { return bool(m_document_token); }
    // 型付きの取得境界。Unix のファイル名には改行が含まれ得る。
    virtual void apply_document_selection(DocumentAction action, const std::vector<::cw::lib::String>& paths);
    void install_selected_paths(const std::vector<::cw::lib::String>& paths);
    std::tuple<::cw::lib::String, ::cw::lib::String> _get_installtarget();
    void _select_installedpaths(::cw::lib::String arg_firstpath, ::cw::lib::String arg_seldname, std::vector<::cw::lib::String> arg_paths);
    void OnCreateDirBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnMoveBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnDeleteBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    void _remove_treeitem(std::shared_ptr<::cw::lib::Window> arg_item);
    void OnRenameBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnCreateLinkToScenario(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnCreateLinkToDirectory(std::shared_ptr<::cw::lib::Window> arg_event);
    void create_scenario_links(const std::vector<::cw::lib::String>& paths);
    void create_directory_link(const ::cw::lib::String& path);
    void _update_nowdir(::cw::lib::String arg_dpath, ::cw::lib::String arg_selection);
    std::tuple<::cw::dialog::scenarioinstall::HeaderBuckets, ::cw::dialog::scenarioinstall::FileBuckets> _to_headers(std::vector<::cw::lib::String> arg_paths, bool arg_link);
    void _show_selectedscenario(::cw::dialog::scenarioinstall::HeaderBuckets arg_headers);
    void _install_scenario(::cw::dialog::scenarioinstall::HeaderBuckets arg_headers, ::cw::dialog::scenarioinstall::HeaderBuckets arg_headers_with_link, ::cw::dialog::scenarioinstall::FileBuckets arg_notscenariofiles);
    void OnClickInfoBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnClickConvBtn(std::shared_ptr<::cw::lib::Window> arg_evt);
    void OnClickYesBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    void BackPaper();
    void OnClickNoBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnClickViewBtn(::cw::lib::Value arg_event);
    bool _can_opendir();
    bool _can_editor();
    void open_directory();
    void open_with_editor();
    void convert_scenario();
    void OnSelect(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnCancel(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnDestroy(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnPosterAnimate(std::shared_ptr<::cw::lib::Window> arg_event);
    void _sync_poster_animation(std::optional<std::shared_ptr<::cw::header::ScenarioHeader>> arg_header);
    void _stop_poster_animation();
    void _on_narrowcondition();
    void draw(bool arg_update);
    void _update_pagelabel();
    std::shared_ptr<::cw::lib::Bitmap> _get_bg();
    std::shared_ptr<::cw::lib::Bitmap> _get_bg_scaled();
    ::cw::lib::String get_detailtext();
    ::cw::lib::String _get_paperdetailtext();
    ::cw::lib::String _get_treedetailtext();
    void _draw_impl(bool arg_update);
    void _enable_btn2(::cw::lib::Value arg_header, std::optional<std::shared_ptr<::cw::lib::DeviceContext>> arg_dc);
    bool is_playing(std::shared_ptr<::cw::header::ScenarioHeader> arg_header);
    bool is_complete(std::shared_ptr<::cw::header::ScenarioHeader> arg_header) const;
    bool is_invisible(std::shared_ptr<::cw::header::ScenarioHeader> arg_header) const;
    void update_narrowcondition();
    void _update_narrowcondition_impl();
    std::tuple<std::vector<std::shared_ptr<::cw::lib::Window>>, std::vector<::cw::lib::String>> create_treeitems(std::shared_ptr<::cw::lib::Window> arg_treeitem, bool arg_freeze);
    std::shared_ptr<::cw::lib::Window> _create_findresultitem(::cw::lib::Integer arg_index, std::shared_ptr<::cw::lib::Window> arg_treeitem, std::shared_ptr<::cw::dialog::scenarioselect::FindResult> arg_findresult);
    ::cw::lib::String _formatted_mtime(::cw::lib::Real arg_mtime, bool arg_showtime);
    std::shared_ptr<::cw::lib::Window> create_treeitem(::cw::lib::Integer arg_index, std::shared_ptr<::cw::lib::Window> arg_treeitem, std::shared_ptr<::cw::header::ScenarioHeader> arg_header);
    void show_tree(bool arg_freeze);
    void OnTreeItemExpanded(std::shared_ptr<::cw::lib::Window> arg_event);
    void _expand_tree(std::shared_ptr<::cw::lib::Window> arg_selitem);
    void _expandeditem(std::shared_ptr<::cw::lib::Window> arg_selitem, ::cw::lib::String arg_startdir, std::set<::cw::lib::String> arg_expandedset);
    void OnTreeItemCollapsed(std::shared_ptr<::cw::lib::Window> arg_event);
    void _collapse_tree(std::shared_ptr<::cw::lib::Window> arg_item);
    void OnTreeSelChanged(std::shared_ptr<::cw::lib::Window> arg_event);
    void _tree_selchanged();
    // この取得関数への受け渡し値。Window.id は表示中のツリーノード番
    // 号、Window.title はそのパスと一致させる。id=-1/title=m_scedir
    // は仮の非表示ルートを表し、空のスタックを返す。
    std::vector<std::tuple<::cw::lib::Value, ::cw::lib::String>> get_dirstack(std::shared_ptr<::cw::lib::Window> arg_paritem);
    void select_treeitem(::cw::lib::Integer arg_index);
    void updated_names(::cw::lib::String arg_dpath, std::vector<std::tuple<::cw::lib::Value, ::cw::lib::String>> arg_dirstack, ::cw::lib::String arg_startdir, std::set<::cw::lib::String> arg_expandedset);
    std::vector<::cw::lib::Value> _narrow_scenario(std::vector<::cw::lib::Value> arg_headers);
    bool _update_narrowparams();
    bool is_showing(std::shared_ptr<::cw::header::ScenarioHeader> arg_header);
    std::tuple<std::set<::cw::lib::Integer>, ::cw::lib::String, std::optional<::cw::lib::Integer>, bool, ::cw::lib::Integer, bool, bool, bool, ::cw::lib::Integer> _get_narrowparams() const;
    bool _is_showing(::cw::lib::Value arg_header, std::set<::cw::lib::Integer> arg_ntypes, ::cw::lib::String arg_narrow, std::optional<::cw::lib::Integer> arg_intnarrow, bool arg_donarrow, ::cw::lib::Integer arg_level) const;
    std::vector<std::shared_ptr<::cw::header::ScenarioHeader>> _sort_headers(std::vector<std::shared_ptr<::cw::header::ScenarioHeader>> arg_seq);
    void enable_btn();
    std::vector<std::shared_ptr<::cw::dialog::text::ReadmeData>> get_texts();
    void conv_scenario(::cw::lib::String arg_path);
    void OnOk(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnCancel2(::cw::lib::Value arg_event);

    // UI 以外のサービスを設定する。操作の制御は引き続き Presentation だけが行う。
    void connect_host_services();
    void set_host_services(std::shared_ptr<ScenarioSelectHostServices> services) { m_host_services=std::move(services); }

    // 共通 ScenarioSelect モデルは、完全な描画面と意味付きコ
    // ントロールを公開する。ホストはその表示の組み込みと倍率適用だけを行う。
    [[nodiscard]] ::cw::dialog::DialogPresentation dialog_presentation() override;
    void dialog_presentation_action(
        ::cw::dialog::DialogPresentationAction arg_action,
        int arg_index) override;
    void dialog_presentation_text(const std::string& arg_text,
                                  bool arg_composition) override;
    void dialog_presentation_text_event(const ::cw::dialog::DialogTextEditingEvent& event) override;
    void dialog_presentation_key(::cw::dialog::DialogPresentationKey key, bool pressed,
                                 ::cw::dialog::DialogPresentationModifiers modifiers) override;
    void dialog_presentation_enter(::cw::dialog::DialogPresentationModifiers modifiers) override {
        dialog_presentation_key(::cw::dialog::DialogPresentationKey::Enter, true, modifiers);
    }
    void dialog_presentation_scroll(float x, float y, int lines) override;
    void dialog_presentation_drop_files(const std::vector<std::string>& paths) override;
    void dialog_presentation_context(float x,float y) override;
    void dialog_presentation_hover(float x,float y) override;
    void dialog_presentation_modifiers(::cw::dialog::DialogPresentationModifiers mods) override {m_search_input.modifiers=mods;}
    void dialog_presentation_pointer_cancel() override {
        m_search_input.dragging=false;
        request_dialog_presentation_refresh();
        m_middle_down=false;
    }
    void dialog_presentation_escape() override { OnEscape(nullptr); }
    bool m_middle_down{};
    void dialog_presentation_middle(float x, float y, bool pressed) override;
    void dialog_presentation_pointer(::cw::dialog::DialogPresentationAction action,
                                     int index, float x, float y, bool pressed) override;
    [[nodiscard]] bool dialog_presentation_finished() const noexcept override;
    void dialog_presentation_complete() override;

    std::vector<::cw::dialog::scenarioselect::ScenarioCandidate> m_candidates_typed{};
    std::vector<::cw::dialog::scenarioselect::ScenarioCandidate> m_source_candidates_typed{};
    std::shared_ptr<::cw::scenariodb::Scenariodb> m_db_typed{};
    ::cw::dialog::scenarioselect::ScenarioDirectory m_nowdir_typed{::cw::lib::String("Scenario")};
    std::vector<std::tuple<::cw::dialog::scenarioselect::ScenarioDirectory, ::cw::lib::String>> m_dirstack_typed{};
    ::cw::lib::String m_scedir_typed{"Scenario"};
    ::cw::lib::String m_search_text{};
    ::cw::lib::Integer m_narrow_type_typed{0};
    ::cw::lib::Integer m_sort_type_typed{0};
    ::cw::lib::Integer m_average_level_typed{0};
    bool m_show_unfitness_typed{true};
    std::array<std::shared_ptr<::cw::lib::Surface>, 3> m_filter_icon_sources{};
    std::array<std::shared_ptr<::cw::lib::Surface>, 3> m_filter_disabled_icons{};
    bool m_show_completed_typed{true};
    bool m_show_invisible_typed{true};
    bool m_debug_mode_typed{false};
    bool m_decision_pending_typed{false};
    bool m_finished_typed{false};
    std::set<::cw::lib::String> m_stamps_typed{};
    std::set<::cw::lib::String> m_nowplaying_typed{};
    std::map<::cw::lib::String, ::cw::lib::Integer> m_coupons_typed{};

    std::map<::cw::lib::String, std::vector<::cw::dialog::scenarioselect::ScenarioCandidate> > m_scenario_cache_typed{};
    std::shared_ptr<::cw::dialog::scenarioselect::FindResult> m_find_result_typed{};
    std::vector<::cw::dialog::scenarioselect::ScenarioCandidate> m_saved_list_typed{};
    std::vector<std::tuple<::cw::dialog::scenarioselect::ScenarioDirectory, ::cw::lib::String>> m_saved_dirstack_typed{};
    std::vector<::cw::dialog::scenarioselect::ScenarioCandidate> m_names_typed{};
    ::cw::lib::String m_saved_fullpath_typed{};
    ::cw::lib::String m_detailtext_typed{};
    ::cw::lib::String m_last_narrow_signature_typed{};
    bool m_processing_typed{false};
    bool m_no_treechangedsound_typed{false};

protected:
    virtual std::string read_search_clipboard() const {return ::cw::dialog::presentation_clipboard_text();}
    virtual void write_search_clipboard(const std::string& value) {::cw::dialog::presentation_set_clipboard_text(value);}
private:
    friend class UpdateNamesThread;
    void sync_search_input();
    void publish_search_input();
    std::shared_ptr<int> m_document_token;
    std::shared_ptr<::cw::lib::documents::Ticket> m_document_ticket;
    std::vector<ScenarioSelectMenuEntry> presentation_menu_entries() const;
    std::vector<::cw::dialog::DialogPresentationControl> presentation_footer_buttons(
        const ::cw::dialog::DialogPresentation& view) const;
    void focus_footer_button(int direction, bool first = false);
    void focus_next_control(int direction);
    void change_page(int direction);
    ::cw::dialog::DialogSideNavigation m_side_navigation;
    // フォーカスは、全 OS の単体版・ALL で共有する UI の状態。
    int m_presentation_button_focus{-1};
    int m_presentation_auxiliary_focus{-1};
    bool m_show_additional_controls_typed{};
    // ポップアップの内容はこのモデルに属し、OS ごとのコントロールツリーには持たせない。
    int m_presentation_menu{};
    int m_presentation_menu_row{};
    int m_presentation_menu_first{};
    bool m_presentation_search_focus{};
    ::cw::dialog::textedit::FilterInput m_search_input;
    // 展開は UI の状態であり、ディレクトリー内容のキャッシュの有無ではない。
    std::set<::cw::lib::String> m_expanded_tree_paths;
    std::set<::cw::lib::String> m_loading_tree_paths;
    ::cw::lib::Integer m_tree_first_row{};
    ::cw::lib::String m_tree_selection_path;
    bool m_tree_ensure_selection{true};
    void rebuild_candidates(bool update_source = true);
    void sort_candidates(std::vector<ScenarioCandidate>& candidates) const;
    void load_directory(const ::cw::dialog::scenarioselect::ScenarioDirectory& arg_nowdir, bool arg_update);
    ::cw::lib::Value candidate_to_value(const ::cw::dialog::scenarioselect::ScenarioCandidate& arg_candidate) const;
    ::cw::lib::String candidate_path(const ::cw::dialog::scenarioselect::ScenarioCandidate& arg_candidate) const;
    bool candidate_is_directory(const ::cw::dialog::scenarioselect::ScenarioCandidate& arg_candidate) const;
    bool candidate_is_showing(const ::cw::dialog::scenarioselect::ScenarioCandidate& arg_candidate) const;
    void sync_legacy_list();
    ::cw::lib::String cache_key(const ::cw::dialog::scenarioselect::ScenarioDirectory& arg_dir) const;
    std::vector<::cw::dialog::scenarioselect::ScenarioCandidate> current_candidates(bool arg_update);
    std::vector<::cw::dialog::scenarioselect::ScenarioCandidate> directory_candidates(
        const ::cw::dialog::scenarioselect::ScenarioDirectory& directory, bool arg_update);
    bool is_scenario_candidate(const ::cw::dialog::scenarioselect::ScenarioCandidate& arg_candidate) const;
    static ::cw::lib::String candidate_name(const ::cw::dialog::scenarioselect::ScenarioCandidate& arg_candidate);
    static ::cw::lib::String lower_ascii(::cw::lib::String arg_value);
    bool select_tree_node(::cw::lib::Integer arg_node_index, bool play_sound = true,
                          bool redraw = true);
    void sync_ui();
    ::cw::dialog::DialogPresentation base_dialog_presentation();
    void populate_tree_ui_state(ScenarioSelectUiState& arg_state);
    void populate_poster_ui_state(ScenarioSelectUiState& arg_state);
    void populate_nav_ui_state(ScenarioSelectUiState& arg_state);
    std::shared_ptr<ScenarioSelectHostServices> m_host_services{};
    std::uintptr_t m_parent_native_handle{};
    bool m_ui_sync_suspended{false};
    std::shared_ptr<::cw::dialog::etc::BookmarkDialog> m_bookmark_dialog{};
    std::shared_ptr<::cw::dialog::etc::ErrorLogDialog> m_error_log_dialog{};
    std::vector<::cw::lib::String> m_tree_items_ui{};
    std::vector<::cw::lib::String> m_tree_paths_ui{};
    ScenarioSelectUiState m_presentation_state{};
    std::optional<ScenarioSelectUiState> m_rendered_presentation_state{};
    ::cw::dialog::DialogPresentation m_rendered_presentation{};
};

class ScenarioSelect_OpenBookmark : public ::cw::lib::Object {
public:
    ScenarioSelect_OpenBookmark() = default;
    virtual ~ScenarioSelect_OpenBookmark() = default;
    std::shared_ptr<::cw::dialog::scenarioselect::ScenarioSelect> m_outer{};
    std::vector<::cw::lib::String> m_bookmark{};
    ::cw::lib::String m_bookmarkpath{};
    void initialize(std::shared_ptr<::cw::dialog::scenarioselect::ScenarioSelect> arg_outer,
                    std::vector<::cw::lib::String> arg_bookmark,
                    ::cw::lib::String arg_bookmarkpath);
    void OnOpen(std::shared_ptr<::cw::lib::Window> arg_event);
};

class UpdateNamesThread : public ::cw::lib::Thread, public std::enable_shared_from_this<UpdateNamesThread> {
public:
    UpdateNamesThread() = default;
    virtual ~UpdateNamesThread() = default;
    ::cw::lib::Value m_dirstack{};
    // Python の循環 GC は dlg → worker → dlg を回収できる。逆参照
    // を弱参照にして、失敗・取り消したワーカーを保持し続けず、C++ でも同じ寿命を実現する。
    std::weak_ptr<ScenarioSelect> m_dlg{};
    ::cw::lib::Value m_dpath{};
    ::cw::lib::Value m_dpaths{};
    ::cw::lib::Value m_expandedset{};
    ::cw::lib::Value m_nowdir{};
    std::atomic<bool> m_quit{};
    std::vector<std::tuple<::cw::lib::Value,::cw::lib::String>> m_dirstack_typed{};
    std::vector<::cw::lib::String> m_dpaths_typed{};
    std::set<::cw::lib::String> m_expandedset_typed{};
    std::shared_ptr<int> m_callback_lifetime{std::make_shared<int>(0)};
    ::cw::lib::Value m_skintype{};
    ::cw::lib::Value m_startdir{};
    void initialize(std::shared_ptr<::cw::dialog::scenarioselect::ScenarioSelect> arg_dlg, ::cw::lib::String arg_nowdir, ::cw::lib::String arg_dpath, std::vector<std::tuple<::cw::lib::Value, ::cw::lib::String>> arg_dirstack, ::cw::lib::String arg_startdir, std::set<::cw::lib::String> arg_expandedset, ::cw::lib::String arg_skintype);
    void run();
    void _start();
};

extern ::cw::lib::Integer g__NARROW_ALL;
extern ::cw::lib::Integer g__NARROW_AUTHOR;
extern ::cw::lib::Integer g__NARROW_DESC;
extern ::cw::lib::Integer g__NARROW_FNAME;
extern ::cw::lib::Integer g__NARROW_LEVEL;
extern ::cw::lib::Integer g__NARROW_TITLE;
extern ::cw::lib::Value g__lockupdatescenario;

std::tuple<std::shared_ptr<::cw::lib::Window>, std::shared_ptr<::cw::lib::Bitmap>, std::shared_ptr<::cw::lib::Bitmap>> ScenarioSelect___init___create_btn(::cw::lib::String arg_msg, std::shared_ptr<::cw::lib::Bitmap> arg_bmp, bool arg_value);
void ScenarioSelect___init___OnMotion(std::shared_ptr<::cw::lib::Window> arg_event);
::cw::lib::Value ScenarioSelect___init___lambda_line_229();
::cw::lib::Value ScenarioSelect___init___lambda_line_230();
::cw::lib::Value ScenarioSelect___init___lambda_line_231();
::cw::lib::Value ScenarioSelect___init___lambda_line_232();
void ScenarioSelect_OnDebugMode_func(std::shared_ptr<::cw::dialog::scenarioselect::ScenarioSelect> arg_self);
void ScenarioSelect_OnDebugMode_func_func(std::shared_ptr<::cw::dialog::scenarioselect::ScenarioSelect> arg_self);
::cw::lib::Value ScenarioSelect__create_addmenu_lambda_line_867(::cw::lib::Value arg_event);
::cw::lib::Value ScenarioSelect__create_addmenu_lambda_line_869(::cw::lib::Value arg_event);
void ScenarioSelect_OpenBookmark_create_bookmarkmenu___init__(::cw::lib::Value arg_self, std::shared_ptr<::cw::dialog::scenarioselect::ScenarioSelect> arg_outer, std::vector<::cw::lib::String> arg_bookmark, ::cw::lib::String arg_bookmarkpath);
void ScenarioSelect_OpenBookmark_create_bookmarkmenu_OnOpen(::cw::lib::Value arg_self, std::shared_ptr<::cw::lib::Window> arg_event);
void ScenarioSelect_OnAddBookmark_func(std::shared_ptr<::cw::dialog::scenarioselect::ScenarioSelect> arg_panel, std::vector<::cw::lib::String> arg_selected, ::cw::lib::String arg_selectedpath);
void ScenarioSelect_OnAddBookmark_func_func(std::shared_ptr<::cw::dialog::scenarioselect::ScenarioSelect> arg_panel);
void ScenarioSelect_OnArrangeBookmark_func(std::shared_ptr<::cw::dialog::scenarioselect::ScenarioSelect> arg_panel);
void ScenarioSelect_OnArrangeBookmark_func_func(std::shared_ptr<::cw::dialog::scenarioselect::ScenarioSelect> arg_panel);
::cw::lib::Value ScenarioSelect_get_findresult_lambda_line_1159(::cw::lib::Value arg_header);
::cw::lib::Value ScenarioSelect_OnCreateLinkToScenario_lambda_line_1722(::cw::lib::Value arg_a, ::cw::lib::Value arg_b);
::cw::lib::Value ScenarioSelect__install_scenario_lambda_line_1805(::cw::lib::Value arg_a, ::cw::lib::Value arg_b);
void ScenarioSelect_open_directory_open_file(::cw::lib::String arg_fpath);
void ScenarioSelect_open_directory_open_dir(::cw::lib::String arg_dpath);
void ScenarioSelect__get_treedetailtext_recurse(std::shared_ptr<::cw::lib::Window> arg_parent, ::cw::lib::String arg_tab);
void ScenarioSelect__update_narrowcondition_impl_recurse(std::shared_ptr<::cw::lib::Window> arg_parent);
void ScenarioSelect_conv_scenario_progress();
void UpdateNamesThread__start_func();
void main();
}

