#pragma once

// UIの実装は一つに統一し、
// 別経路の実装を設けない。

// cw/dialog/scenarioinstall.py の移植。描画・イベ
// ント接続は SDL のダイアログ層が担当する。下のクラスは型付きの導入状態も
// 公開し、非 GUI の呼び出し側も同じパス・コピーの規則を使えるようにする。
#include "cw/lib/compat.hpp"
#include "cw/fwd.hpp"
#include "cw/dialog/presentation.hpp"
#include "cw/dialog/incrementalsearch.hpp"

#include <atomic>
#include <filesystem>

namespace cw::dialog::scenarioinstall {
// wx.TreeItemId の項目データと親関係を渡す型付き
// 表現。ツリーの親はホスト Window の親やファイルパスと
// は独立し、ショートカットの対象がシナリオルート外でも表示上の
// 祖先を保つ。各表示側と公開 API は同じ走査処理を使う。
struct DirectoryTreeItem : ::cw::lib::Window {
    ::cw::lib::String path{};
    std::weak_ptr<DirectoryTreeItem> tree_parent{};
    std::vector<std::shared_ptr<DirectoryTreeItem>> children{};
    bool children_loaded{};
    bool valid{true};
};
using BucketKey=std::tuple<::cw::lib::String,::cw::lib::String>;
// Python の dict の処理群は、収集・選択・コピーを
// 通じて最初の挿入順を保持する。ソート済みの std::map
// ではこの順序が失われる。旧 map 入力は自身の反復順を
// 保つが、API 呼び出し前に失った挿入順は復元できない。
template<class T> class OrderedScenarioBuckets {
    using Storage = std::vector<std::pair<BucketKey,T>>;
    Storage m_entries;
public:
    using iterator=typename Storage::iterator;
    using const_iterator=typename Storage::const_iterator;
    OrderedScenarioBuckets()=default;
    OrderedScenarioBuckets(std::initializer_list<typename Storage::value_type> entries) {
        for(const auto& [key,value]:entries) (*this)[key]=value;
    }
    template<class Compare,class Allocator>
    OrderedScenarioBuckets(const std::map<BucketKey,T,Compare,Allocator>& entries) {
        for(const auto& [key,value]:entries) (*this)[key]=value;
    }
    iterator begin() noexcept {return m_entries.begin();}
    iterator end() noexcept {return m_entries.end();}
    const_iterator begin() const noexcept {return m_entries.begin();}
    const_iterator end() const noexcept {return m_entries.end();}
    bool empty() const noexcept {return m_entries.empty();}
    std::size_t size() const noexcept {return m_entries.size();}
    void clear() noexcept {m_entries.clear();}
    iterator find(const BucketKey& key) {return std::find_if(begin(),end(),[&](const auto& e){return e.first==key;});}
    const_iterator find(const BucketKey& key) const {return std::find_if(begin(),end(),[&](const auto& e){return e.first==key;});}
    bool contains(const BucketKey& key) const {return find(key)!=end();}
    T& operator[](const BucketKey& key) {
        if(auto i=find(key);i!=end())return i->second;
        m_entries.emplace_back(key,T{});return m_entries.back().second;
    }
    T& at(const BucketKey& key) {if(auto i=find(key);i!=end())return i->second;throw std::out_of_range("scenario bucket");}
    const T& at(const BucketKey& key) const {if(auto i=find(key);i!=end())return i->second;throw std::out_of_range("scenario bucket");}
};
using HeaderBuckets=OrderedScenarioBuckets<std::vector<std::shared_ptr<::cw::header::ScenarioHeader>>>;
using FileBuckets=OrderedScenarioBuckets<std::vector<::cw::lib::String>>;
// OS 固有コードはファイルシステム・シェルのサービスだけに限定する。確認画面、コントロ
// ール、配置、進捗表示は一つの共通 DialogPresentation 経路を使う。
class ScenarioInstallHostServices {
public:
    virtual ~ScenarioInstallHostServices() = default;
    virtual ::cw::lib::String resolve_shortcut(const ::cw::lib::String& arg_path) = 0;
    virtual bool retarget_shortcut(const ::cw::lib::String& arg_path, const ::cw::lib::String& arg_target) = 0;
    virtual bool move_to_recycle_bin(const ::cw::lib::String& arg_path) = 0;
};
std::shared_ptr<ScenarioInstallHostServices> get_host_services();

class InstallThread : public ::cw::lib::Thread {
public:
    InstallThread() = default;
    virtual ~InstallThread() = default;
    HeaderBuckets m_headers{};
    FileBuckets m_notscenariofiles{};
    ::cw::lib::String m_dstpath{},m_msg{};
    std::map<::cw::lib::String,std::vector<::cw::lib::String>> m_db_repls{};
    ::cw::lib::Integer m_num{0};
    ::cw::lib::Integer m_total{0};
    std::optional<std::shared_ptr<::cw::header::ScenarioHeader>> m_failed{};
    std::set<::cw::lib::String> m_updates{};
    std::vector<::cw::lib::String> m_paths{},m_filepaths{},m_source_links{};
    std::map<::cw::lib::String,::cw::lib::String> m_repl_links{};
    std::shared_ptr<::cw::scenariodb::Scenariodb> m_db{};
    ::cw::lib::String m_skintype{};
    bool m_delete_sourceafterinstalled{};
    bool m_install_notscenariofiles{true};
    std::atomic_bool m_cancelled{false};
    std::atomic_bool m_finished{false};
    ::cw::lib::String m_error{};
    // 共通の移動・コピー経路用の、任意の名前変更処理。ファイル
    // 権限を変えずに、確定的な I/O 失敗の検査も行える。
    std::function<void(const std::filesystem::path&,const std::filesystem::path&,std::error_code&)> m_native_rename{};
    // ネイティブ Windows/SDL は、このコールバックで wx 相当の上書
    // き確認を提供できる。直接のヘッドレスワーカーには Py のモーダルホストがな
    // いため、コールバックがなければ重複しない名前を選び、両方のファイルを保つ。
    std::optional<::cw::lib::Callback> m_overwrite_callback{};
    std::optional<::cw::lib::Integer> m_allret{};
    std::shared_ptr<::cw::dialog::progress::ProgressDialog> m_progress_dialog{};
    void initialize_typed(HeaderBuckets arg_headers, FileBuckets arg_notscenariofiles,
                          ::cw::lib::String arg_dstpath,
                          std::map<::cw::lib::String, std::vector<::cw::lib::String>> arg_db_repls,
                          std::shared_ptr<::cw::scenariodb::Scenariodb> arg_db,
                          ::cw::lib::String arg_skintype);
    void run_typed();
};

class OverwriteScenarioDialog : public ::cw::lib::Dialog,
                                public ::cw::dialog::DialogPresentationProvider,
                                public std::enable_shared_from_this<OverwriteScenarioDialog> {
public:
    OverwriteScenarioDialog() = default;
    ~OverwriteScenarioDialog() override;
    ::cw::lib::Value m__textheight{};
    ::cw::lib::Value m__wrapped_text{};
    ::cw::lib::Value m_cnclbtn{};
    bool m_cwpy_debug{};
    ::cw::lib::Value m_db_exists{};
    std::map<::cw::lib::String, std::vector<::cw::lib::String>> m_db_repls{};
    std::map<::cw::lib::String, std::vector<std::shared_ptr<::cw::header::ScenarioHeader>>> m_db_exists_typed{};
    std::vector<::cw::lib::Value> m_keys{};
    bool m_accepted{};
    bool m_cancelled{};
    std::vector<::cw::lib::String> m_row_sources{};
    std::vector<::cw::lib::String> m_row_paths{};
    std::vector<::cw::lib::String> m_row_labels{};
    std::vector<bool> m_row_checked{};
    std::set<::cw::lib::Integer> m_row_selected{};
    ::cw::lib::Integer m_selection_anchor{-1};
    ::cw::dialog::DialogPresentationModifiers m_selection_modifiers{};
    ::cw::lib::Integer m_presentation_scroll{};
    ::cw::lib::Integer m_presentation_horizontal{};
    ::cw::lib::Integer m_presentation_focus{};
    ::cw::lib::Size m_presentation_size{500,400};
    ::cw::dialog::IncrementalSearch m_presentation_search{};
    bool m_presentation_finished{};
    ::cw::lib::Value m_okbtn{};
    ::cw::lib::Value m_scedir{};
    ::cw::lib::Value m_text{};
    void initialize(std::shared_ptr<::cw::dialog::scenarioinstall::ScenarioInstall> arg_parent, ::cw::lib::String arg_scedir, std::map<::cw::lib::String, std::vector<std::shared_ptr<::cw::header::ScenarioHeader>>> arg_db_exists);
    void initialize_rows(::cw::lib::String arg_scedir, ::cw::lib::String arg_message,
                         const std::map<::cw::lib::String, std::vector<::cw::lib::String>>& arg_rows,
                         const std::map<::cw::lib::String, ::cw::lib::String>& arg_labels);
    void OnResize(std::shared_ptr<::cw::lib::Window> arg_event);
    void _resize();
    void OnOk(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnCancel(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnPaint(std::shared_ptr<::cw::lib::Window> arg_evt);
    void _do_layout();
    [[nodiscard]] ::cw::dialog::DialogPresentation dialog_presentation() override;
    void dialog_presentation_action(::cw::dialog::DialogPresentationAction arg_action,
                                    int arg_index) override;
    void dialog_presentation_key(::cw::dialog::DialogPresentationKey key, bool pressed,
                                 ::cw::dialog::DialogPresentationModifiers modifiers) override;
    void dialog_presentation_modifiers(::cw::dialog::DialogPresentationModifiers modifiers) override { m_selection_modifiers=modifiers; }
    void dialog_presentation_text_event(const ::cw::dialog::DialogTextEditingEvent& event) override;
    void dialog_presentation_scroll(float x,float y,int lines) override;
    void dialog_presentation_resize(::cw::lib::Size size) override;
    [[nodiscard]] bool dialog_presentation_finished() const noexcept override;
    void dialog_presentation_complete() override;
private:
    void select_row(::cw::lib::Integer row, ::cw::dialog::DialogPresentationModifiers modifiers);
    [[nodiscard]] ::cw::lib::Integer visible_presentation_rows() const;
    [[nodiscard]] ::cw::lib::String wrapped_presentation_message() const;
    [[nodiscard]] ::cw::lib::Integer presentation_list_top() const;
    [[nodiscard]] ::cw::lib::Integer maximum_horizontal_scroll() const;
};

class SelectScenarioDirectory : public ::cw::lib::Dialog,
                                public ::cw::dialog::DialogPresentationProvider {
public:
    SelectScenarioDirectory() = default;
    virtual ~SelectScenarioDirectory() = default;
    ::cw::lib::Value m__imgidx_dir{};
    ::cw::lib::Integer m__textheight{};
    ::cw::lib::Value m__wrapped_text{};
    std::tuple<::cw::lib::Value, ::cw::lib::Value> m_buttons{};
    ::cw::lib::Value m_createdirbtn{};
    bool m_cwpy_debug{};
    ::cw::lib::Value m_db{};
    std::vector<::cw::lib::String> m_dirstack{};
    ::cw::lib::Value m_nobtn{};
    ::cw::lib::String m_path{};
    ::cw::lib::String m_selected_path{};
    std::shared_ptr<DirectoryTreeItem> m_root_item{};
    std::vector<std::shared_ptr<DirectoryTreeItem>> m_presentation_items{};
    ::cw::lib::String m_scedir{},m_skintype{},m_text{};
    std::shared_ptr<::cw::scenariodb::Scenariodb> m_db_typed{};
    ::cw::lib::Value m_tree{};
    ::cw::lib::Value m_yesbtn{};
    std::vector<::cw::lib::String> m_presentation_paths{};
    std::vector<::cw::lib::Integer> m_presentation_depths{};
    std::vector<bool> m_presentation_has_children{};
    std::set<::cw::lib::String> m_presentation_expanded{};
    ::cw::lib::Integer m_presentation_scroll{};
    ::cw::lib::Integer m_presentation_focus{};
    ::cw::lib::Size m_presentation_size{420,400};
    ::cw::dialog::IncrementalSearch m_presentation_search{};
    bool m_presentation_finished{};
    bool m_presentation_accepted{};
    void initialize(std::shared_ptr<::cw::lib::Window> arg_parent, ::cw::lib::String arg_title, ::cw::lib::String arg_text, std::shared_ptr<::cw::scenariodb::Scenariodb> arg_db, ::cw::lib::String arg_skintype, ::cw::lib::String arg_scedir);
    virtual void create_buttons();
    void _do_layout();
    void _create_treeitems(std::shared_ptr<::cw::lib::Window> arg_treeitem, std::optional<std::vector<::cw::lib::String>> arg_dirstack);
    void OnTreeItemExpanded(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnTreeItemCollapsed(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnOk(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnCancel(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnResize(std::shared_ptr<::cw::lib::Window> arg_event);
    virtual void _resize();
    void OnPaint(std::shared_ptr<::cw::lib::Window> arg_evt);
    void OnCreateDirBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    std::vector<::cw::lib::String> get_dirstack(std::shared_ptr<::cw::lib::Window> arg_paritem);
    [[nodiscard]] ::cw::dialog::DialogPresentation dialog_presentation() override;
    void dialog_presentation_action(::cw::dialog::DialogPresentationAction arg_action,
                                    int arg_index) override;
    void dialog_presentation_key(::cw::dialog::DialogPresentationKey key, bool pressed,
                                 ::cw::dialog::DialogPresentationModifiers modifiers) override;
    void dialog_presentation_text_event(const ::cw::dialog::DialogTextEditingEvent& event) override;
    void dialog_presentation_scroll(float x,float y,int lines) override;
    void dialog_presentation_resize(::cw::lib::Size size) override;
    [[nodiscard]] bool dialog_presentation_finished() const noexcept override;
    void dialog_presentation_complete() override;
protected:
    void rebuild_presentation_tree();
    [[nodiscard]] ::cw::lib::Integer visible_presentation_rows() const;
    [[nodiscard]] ::cw::lib::Integer selected_presentation_row() const;
    void select_presentation_row(::cw::lib::Integer row);
    virtual void accept_directory() { OnOk(nullptr); }
    virtual ::cw::lib::String directory_accept_label() const;
    virtual ::cw::lib::String directory_accept_tooltip() const { return {}; }

};

class ScenarioInstall : public ::cw::dialog::scenarioinstall::SelectScenarioDirectory,
                        public std::enable_shared_from_this<ScenarioInstall> {
public:
    ScenarioInstall() = default;
    ~ScenarioInstall() override;
    HeaderBuckets m_headers{};
    FileBuckets m_notscenariofiles{};
    std::optional<::cw::lib::Callback> m_overwrite_callback{};
    ::cw::lib::String m_failed_name{};
    void initialize(std::shared_ptr<::cw::lib::Window> arg_parent, std::shared_ptr<::cw::scenariodb::Scenariodb> arg_db, HeaderBuckets arg_headers, FileBuckets arg_notscenariofiles, ::cw::lib::String arg_skintype, ::cw::lib::String arg_scedir);
    void _resize() override;
    void create_buttons() override;
    void OnInstallBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    void accept_directory() override { OnInstallBtn(nullptr); }
    ::cw::lib::String directory_accept_label() const override;
    ::cw::lib::String directory_accept_tooltip() const override;

};

::cw::lib::Integer SelectScenarioDirectory__resize_extent_w(::cw::lib::String arg_s);
::cw::lib::Value ScenarioInstall___init___lambda_line_273(::cw::lib::Value arg_a, ::cw::lib::Value arg_b);
::cw::lib::Value ScenarioInstall_create_buttons_lambda_line_293(::cw::lib::Value arg_a, ::cw::lib::Value arg_b);
std::vector<::cw::lib::String> get_dpaths(::cw::lib::String arg_dpath);
bool is_listitem(::cw::lib::String arg_path);
std::tuple<HeaderBuckets, FileBuckets> to_scenarioheaders(std::vector<::cw::lib::String> arg_paths, std::shared_ptr<::cw::scenariodb::Scenariodb> arg_db, ::cw::lib::String arg_skintype, bool arg_link);
bool to_scenarioheaders_recurse(::cw::lib::String arg_parent, ::cw::lib::String arg_path, std::map<std::tuple<::cw::lib::String, ::cw::lib::String>, std::vector<::cw::lib::String>> arg_notscenariofiles2);
// 入力の受け渡しは ScenarioSelect のホストサービスから
// 提供できる。ファイル名検査、作成、戻りパスには常に共通実装を使う。
using DirectoryNameInput = std::function<std::optional<::cw::lib::String>(
    const ::cw::lib::String&, const ::cw::lib::String&)>;
::cw::lib::String create_dir(std::shared_ptr<::cw::dialog::scenarioinstall::ScenarioInstall> arg_parentdialog, ::cw::lib::String arg_dpath, const DirectoryNameInput& arg_input = {});
std::tuple<bool, std::vector<::cw::lib::String>, std::vector<::cw::lib::String>, bool> install_scenario(std::shared_ptr<::cw::dialog::scenarioinstall::ScenarioInstall> arg_parentdialog, HeaderBuckets arg_headers, FileBuckets arg_notscenariofiles, ::cw::lib::String arg_scedir, ::cw::lib::String arg_dstpath, std::shared_ptr<::cw::scenariodb::Scenariodb> arg_db, ::cw::lib::String arg_skintype);
void InstallThread_install_scenario___init__(::cw::lib::Value arg_self, std::map<std::tuple<::cw::lib::String, ::cw::lib::String>, std::vector<std::shared_ptr<::cw::header::ScenarioHeader>>> arg_headers, std::map<std::tuple<::cw::lib::String, ::cw::lib::String>, std::vector<::cw::lib::String>> arg_notscenariofiles, ::cw::lib::String arg_dstpath, std::map<::cw::lib::String, std::vector<::cw::lib::String>> arg_db_repls);
void InstallThread_install_scenario_run(::cw::lib::Value arg_self);
::cw::lib::Integer InstallThread_install_scenario__confirm_overwrite(::cw::lib::Value arg_self, std::shared_ptr<::cw::dialog::progress::ProgressDialog> arg_dlg, ::cw::lib::String arg_s, std::vector<std::optional<::cw::lib::Integer>> arg_allret);
::cw::lib::Integer InstallThread_install_scenario__confirm_overwrite_func();
void InstallThread_install_scenario__install(::cw::lib::Value arg_self, ::cw::lib::String arg_parent, std::vector<std::shared_ptr<::cw::header::ScenarioHeader>> arg_headers_seq, ::cw::lib::String arg_dstpath, std::vector<std::optional<::cw::lib::Integer>> arg_allret);
void InstallThread_install_scenario__install_files(::cw::lib::Value arg_self, ::cw::lib::String arg_parent, std::vector<::cw::lib::String> arg_files_seq, ::cw::lib::String arg_dstpath, std::vector<std::optional<::cw::lib::Integer>> arg_allret);
void install_scenario_progress();
void update_scenariolog(::cw::lib::String arg_normpath, ::cw::lib::String arg_dst, bool arg_dstisfile);
void update_scenariolog2(::cw::lib::String arg_normpath, ::cw::lib::String arg_dst, bool arg_dstisfile);
::cw::lib::Integer OverwriteScenarioDialog__resize_extent_w(::cw::lib::String arg_s);
::cw::lib::String create_installdesc(std::vector<std::shared_ptr<::cw::header::ScenarioHeader>> arg_headers_seq);
void main();
}

