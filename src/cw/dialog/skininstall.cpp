#include "cw/lib/exception_origin.hpp"
#include "cw/lib/runtime_access.hpp"
// UIの実装は一つに統一し、
// 別経路の実装を設けない。
#ifndef _SILENCE_CXX20_U8PATH_DEPRECATION_WARNING
#define _SILENCE_CXX20_U8PATH_DEPRECATION_WARNING
#endif
#include "cw/dialog/skininstall.hpp"
#include "cw/dialog/message.hpp"
#include "cw/dialog/presentation.hpp"
#include "cw/dialog/progress.hpp"
#include "cw/skin/util.hpp"
#include "cw/cw.hpp"
#include "cw/data.hpp"
#include "cw/frame.hpp"
#include "cw/lib/archive.hpp"
#include "cw/lib/encoding.hpp"
#include "cw/setting.hpp"
#include "cw/thread.hpp"
#include "cw/util.hpp"
#include "platform/storage/storage_mount.hpp"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <functional>
#include <exception>
#include <map>
#include <memory>
#include <system_error>
#include <thread>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace cw::dialog::skininstall {
namespace fs = std::filesystem;
using ::cw::lib::Arguments;
using ::cw::lib::KeywordArguments;
using ::cw::lib::String;
using ::cw::lib::Value;
namespace {
class ScopeExit {
public:
    explicit ScopeExit(std::function<void()> arg_function)
        : m_function(std::move(arg_function)) {}
    ~ScopeExit() { run(); }
    ScopeExit(const ScopeExit&)=delete;
    ScopeExit& operator=(const ScopeExit&)=delete;
    void run()
    {
        if (!m_function) return;
        auto function=std::move(m_function);
        function();
    }
private:
    std::function<void()> m_function;
};

String filename(const String& path) { return ::cw::lib::encoding::path_to_utf8(fs::u8path(path).filename()); }
bool is_archive(const String& path) { std::error_code ec; return fs::is_regular_file(fs::u8path(path), ec); }
fs::path canonical_or_input(const fs::path& path)
{
    std::error_code ec;
    const auto result=fs::weakly_canonical(path,ec);
    return ec?path:result;
}

bool is_inside_skin_directory(const String& path) {
    const auto parent=canonical_or_input(fs::u8path(path).parent_path());
    const auto skin=canonical_or_input(fs::u8path("Data/Skin"));
#ifdef _WIN32
    return ::cw::lib::encoding::path_to_utf8(parent)==::cw::lib::encoding::path_to_utf8(skin) ||
           ::cw::lib::encoding::lower(::cw::lib::encoding::path_to_utf8(parent))==
               ::cw::lib::encoding::lower(::cw::lib::encoding::path_to_utf8(skin));
#else
    return parent==skin;
#endif
}

struct InstallCallbacks {
    std::function<InstallOptions(const std::vector<SkinRecord>&, bool)> confirm{};
    std::function<void(const ProgressObj&)> progress{};
    std::function<bool()> cancelled{};
    std::function<void(const std::vector<String>&)> errors{};
    std::function<void(String, bool)> switch_skin{};
    std::function<void(String)> change_cursor{};
};
// インストール処理は固定の一時ディレクトリーを使い、使用中のスキンを一
// まとまりで更新する。 直列化することで、二つの UI 操作が同時に来
// ても、操作別のコールバックや進捗の所有関係が混ざらないようにする。
std::mutex g_install_mutex;

InstallOptions common_confirm(const std::shared_ptr<::cw::lib::Window>& parent,
                              const std::vector<SkinRecord>& records,
                              const bool canswitch)
{
    InstallOptions result{};
    result.accepted=false;
    String text;
    if(records.size()>=2){
        text=std::to_string(records.size())+"件のスキンをインストールします。よろしいですか？";
    }else{
        const auto&[name,author,type,path,archive]=records.front();
        (void)type;(void)path;(void)archive;
        auto display=name.empty()?String("(無名のスキン)"):name;
        if(!author.empty())display+="("+author+")";
        text="スキン「"+display+"」をインストールします。よろしいですか？";
    }
    auto dialog=std::make_shared<::cw::dialog::message::SysMessage>();
    std::vector<::cw::dialog::message::MessageCheckBox> checks{
        {"overwrite","インストール済みの同一スキンを置換する",true},
        {"remove_installed","インストール成功後にインストール元を削除する",true}};
    if(canswitch)checks.push_back(
        {"switch_skin","インストールしたスキンに切り替える",false});
    dialog->initialize(parent,"スキンのインストール",text,
        std::vector<::cw::dialog::message::MessageChoice>{
            {"はい(&Y)",::cw::dialog::message::kWxIdYes,105,{}},
            {"いいえ(&N)",::cw::dialog::message::kWxIdNo,105,{}}},
        std::move(checks));
    if(!::cw::dialog::show_common_modal_presentation(dialog) ||
       dialog->m_return_code!=::cw::dialog::message::kWxIdYes)return result;
    result.accepted=true;
    result.overwrite=dialog->get_check("overwrite");
    result.remove_installed=dialog->get_check("remove_installed");
    result.switch_skin=canswitch&&dialog->get_check("switch_skin");
    return result;
}

void common_errors(const std::vector<String>& errors)
{
    if(errors.empty())return;
    String text="スキンのインストール中にエラーが発生しました。\n\n";
    for(std::size_t index=0;index<errors.size();++index){
        if(index)text+="\n\n";
        text+=errors[index];
    }
    (void)::cw::dialog::message::show_common_modal_message(
        "スキンのインストール",text,false,true);
}

struct DefaultUiSession {
    std::shared_ptr<::cw::dialog::progress::SysProgressDialog> dialog{};
    std::shared_ptr<::cw::lib::Window> parent{};
    bool finished{};
    void open(const ProgressObj& progress)
    {
        if (dialog) return;
        String message;
        ::cw::lib::Integer value{},maximum{};
        progress.snapshot(message,value,maximum);
        dialog=std::make_shared<::cw::dialog::progress::SysProgressDialog>();
        dialog->initialize(parent,"スキンのインストール",message,maximum,0,false,420);
        dialog->UpdateProgress(value,message);
    }
    void update(const ProgressObj& progress)
    {
        if (!dialog) return;
        String message;
        ::cw::lib::Integer value{},maximum{};
        progress.snapshot(message,value,maximum);
        (void)maximum;
        dialog->UpdateProgress(value,message);
    }
    void close()
    {
        if (finished) return;
        finished=true;
        if (dialog) {
            // 完了・キャンセルのコールバックはこのセッションを所有する場合
            // がある。 表示の終了時は、すべての経路でその参照関係を切る。
            dialog->set_modal_callbacks({},{});
            dialog->close();
            dialog.reset();
        }
    }
    ~DefaultUiSession() { close(); }
};

void remove_path(const String& path, bool trashbox)
{
    if (path.empty()) return;
    ::cw::util::remove(path,trashbox);
}

void sync_files()
{
    if (::cw::g_fsync) ::cw::g_fsync->sync();
}

void report(ProgressObj& object, const InstallCallbacks& callbacks,
            const String& message, ::cw::lib::Integer amount=1)
{
    object.advance(message,amount);
    if (callbacks.progress) callbacks.progress(object);
    if (callbacks.cancelled && callbacks.cancelled()) object.cancel();
}

bool should_cancel(ProgressObj& object, const InstallCallbacks& callbacks)
{
    if (callbacks.cancelled && callbacks.cancelled()) object.cancel();
    return object.cancelled();
}

void update_yado_skin_references(const std::map<String,String>& rename_table)
{
    const fs::path yado_root=fs::u8path("Yado");
    const auto update_environment=[&](const fs::path& environment) {
        const auto tree=::cw::data::xml2etree(
            ::cw::lib::encoding::path_to_utf8(environment),"",std::nullopt,std::nullopt,false);
        if (!tree) throw std::runtime_error(
            "宿のスキン参照を読み込めません: "+::cw::lib::encoding::path_to_utf8(environment));
        const auto old_skin=tree->gettext("Property/Skin","");
        const auto found=rename_table.find(old_skin);
        if (found==rename_table.end() || found->second.empty()) return;
        tree->edit("Property/Skin",found->second,std::nullopt);
        tree->write_file(::cw::lib::encoding::path_to_utf8(environment));
    };

    // ALL/mobileの宿はネイティブfilesystemに存在しないことがある。
    // 旧スキンを削除する前に、使用中でない宿も同じStorage経路で全件更新する。
    if (::cardwirth::platform::is_mounted_user_path(yado_root)) {
        const auto root_status=::cardwirth::platform::stat_mounted_user_path(yado_root,true);
        if (!root_status || !root_status->is_directory()) return;
        const auto entries=::cardwirth::platform::list_mounted_user_path(yado_root,true);
        if (!entries) throw std::runtime_error("宿の一覧を取得できません。");
        for (const auto& yado:*entries) {
            if (!yado.stat.is_directory()) continue;
            const auto environment=yado.path/"Environment.xml";
            const auto status=::cardwirth::platform::stat_mounted_user_path(environment,true);
            if (status && status->is_file()) update_environment(environment);
        }
        return;
    }

    std::error_code ec;
    if (!fs::is_directory(yado_root,ec)) return;
    for (const auto& yado:fs::directory_iterator(yado_root,ec)) {
        if (ec) throw fs::filesystem_error("Yado directory iteration",yado_root,ec);
        const auto environment=yado.path()/"Environment.xml";
        if (!fs::is_regular_file(environment,ec)) continue;
        update_environment(environment);
    }
}

void remove_replaced_skins(ProgressObj& object)
{
    for (const auto& path:object.m_all_removes_repl) {
        try {
            remove_path(path,true);
        } catch (const std::exception& error) {
            object.m_errors.push_back(error.what());
        }
        // ファイルが使用中だと、古いディレクトリーの削除に失敗する場合がある。
        // Python と同様、上のディレクトリー移動が失敗しても Skin.xml
        // の名前を変え、古いスキンを無効にする代替処理を維持する。
        const auto skin_xml=fs::u8path(path)/"Skin.xml";
        const auto removed_xml=fs::u8path(path)/"Skin.xml_removed";
        std::error_code ec;
        if (fs::is_regular_file(skin_xml,ec)) {
            ec.clear();
            fs::rename(skin_xml,removed_xml,ec);
            if (ec) object.m_errors.push_back(
                "Skin.xmlの無効化に失敗しました: "+::cw::lib::encoding::path_to_utf8(skin_xml));
        }
    }
}

void remove_install_sources(ProgressObj& object)
{
    for (const auto& path:object.m_all_removes_base) {
        try { remove_path(path,true); }
        catch (const std::exception& error) { object.m_errors.push_back(error.what()); }
    }
}
}

void ProgressObj::advance(const String& msg, ::cw::lib::Integer amount) {
    std::lock_guard<std::mutex> guard(m_mutex);
    m_value += amount;
    m_msg = msg;
}

void ProgressObj::set_value(::cw::lib::Integer value)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    m_value=value;
}

void ProgressObj::snapshot(String& msg, ::cw::lib::Integer& value,
                           ::cw::lib::Integer& maximum) const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    msg=m_msg;
    value=m_value;
    maximum=m_maximum;
}

void ProgressObj::cancel()
{
    std::lock_guard<std::mutex> guard(m_mutex);
    m_cancelled=true;
}

bool ProgressObj::cancelled() const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    return m_cancelled;
}

std::vector<SkinRecord> install_skin(std::vector<String> paths,
                                     std::shared_ptr<::cw::lib::Window> parent,
                                     bool canswitch) {
    std::unique_lock install_lock(g_install_mutex);
    std::vector<SkinRecord> seq;
    ::cw::lib::Integer maximum = 3;
    for (const auto& path : paths) {
        if (is_inside_skin_directory(path)) continue;
        // Py と他のスキン処理と同じメタデータ検証を使い、
        // 拒否したアーカイブを別の緩いパーサーで受け入れない。
        const auto info = ::cw::skin::util::get_skininfo(path);
        if (!info) continue;
        const auto& [name, author, type] = *info;
        const bool archive = is_archive(path);
        seq.emplace_back(name, author, type, path, archive);
        maximum += archive ? ::cw::skin::util::g_INSTALL_PROGRESS_ARCHIVE
                           : ::cw::skin::util::g_INSTALL_PROGRESS;
    }
    if (seq.empty()) return {};

    const auto ui=std::make_shared<DefaultUiSession>();
    ui->parent=parent;
    InstallCallbacks callbacks{};
#ifdef _WIN32
    if (!callbacks.change_cursor) callbacks.change_cursor=[](const String& cursor) {
        ::SetCursor(::LoadCursorW(nullptr,MAKEINTRESOURCEW(cursor=="wait"?32514:32512)));
    };
#endif
    // カーソルの変更はホストのサービスとする。
    // 確認・エラー・進捗は全 Engine 製品
    // と OS で同じ共通 Presentation
    // を使い、UI の実装を一つに保つ。
    if (!callbacks.confirm)
        callbacks.confirm=[parent](const std::vector<SkinRecord>& records,
                                   bool arg_canswitch) {
            return common_confirm(parent,records,arg_canswitch);
        };
    if (!callbacks.errors)
        callbacks.errors=[](const std::vector<String>& errors) {
            common_errors(errors);
        };
    // 進捗コールバックはインストールワーカーから呼ばれる。そのワーカーか
    // らネイティブのダイアログを更新すると UI バックエンドと競合し、
    // Win32 では別スレッドから子 HWND の状態に触れることに
    // なる。Python の wx.CallAfter による進捗ループ
    // と同様、 下の呼び出し側の処理ループが既定の進捗表示を管理する。
    if (!callbacks.progress) callbacks.progress=[](const ProgressObj&) {};

    // 確認用の表示プロバイダーがないことを、インストールへの暗
    // 黙の「はい」として扱わない。 特にネイティブの確認ダイア
    // ログを登録していない移植用ビルドでも、この規則を守る。
    InstallOptions options{};
    options.accepted=false;
    if (callbacks.confirm) options=callbacks.confirm(seq,canswitch);
    if (!canswitch) options.switch_skin=false;
    if (!options.accepted) return {};

    if (options.overwrite) maximum+=2+static_cast<::cw::lib::Integer>(seq.size());
    if (options.remove_installed) ++maximum;

    ProgressObj progress;
    progress.m_maximum=maximum;
    progress.m_tempdir="Data/Temp/SkinInstall";
    ui->open(progress);
    const auto external_progress=callbacks.progress;
    callbacks.progress=[ui,external_progress](const ProgressObj& object) {
        ui->update(object);
        if(external_progress)external_progress(object);
    };
    const auto old_cursor=::cw::g_cwpy?::cw::g_cwpy->m_cursor:String{};
    std::mutex pending_cursor_mutex;
    std::optional<String> pending_cursor;
    const auto request_cursor=[&](const String& cursor) {
        if (::cw::g_cwpy) {
            const auto game=::cw::g_cwpy;
            game->exec_func([game,cursor](const Arguments&,const KeywordArguments&) {
                if (game) game->change_cursor(cursor,true);
                return Value{};
            });
        } else {
            std::lock_guard lock(pending_cursor_mutex);
            pending_cursor=cursor;
        }
    };
    const auto apply_pending_cursor=[&] {
        if (::cw::g_cwpy || !callbacks.change_cursor) return;
        std::optional<String> cursor;
        {
            std::lock_guard lock(pending_cursor_mutex);
            cursor=std::move(pending_cursor);
            pending_cursor.reset();
        }
        if (cursor) {
            try { callbacks.change_cursor(*cursor); }
            catch (...) {}
        }
    };
    request_cursor("wait");
    apply_pending_cursor();

    const String tempdir=progress.m_tempdir;
    std::error_code ec;
    fs::create_directories(fs::u8path(tempdir),ec);
    if (ec) {
        try { remove_path(tempdir,false); }
        catch (...) {}
        request_cursor(old_cursor);
        apply_pending_cursor();
        ui->close();
        throw fs::filesystem_error("SkinInstall temporary directory",fs::u8path(tempdir),ec);
    }
    bool install_finished=false;
    ScopeExit emergency([&] {
        if (install_finished) return;
        try { remove_path(tempdir,false); }
        catch (...) {}
        request_cursor(old_cursor);
        apply_pending_cursor();
        ui->close();
    });

    std::map<std::pair<String,String>,std::vector<String>> removes_table;
    std::map<String,String> rename_table;
    String installed_first;
    const ::cw::lib::Callback callback=[&](const Arguments& args,
                                           const KeywordArguments&) -> Value {
        String message;
        ::cw::lib::Integer amount=1;
        if (!args.empty()) if (const auto* p=std::get_if<String>(&args[0].storage())) message=*p;
        if (args.size()>=2) if (const auto* p=std::get_if<::cw::lib::Integer>(&args[1].storage())) amount=*p;
        report(progress,callbacks,message,amount);
        return {};
    };

    const auto run_install=[&] {
        ScopeExit cleanup([&] {
            try {
                remove_path(tempdir,false);
            } catch (const std::exception& error) {
                progress.m_errors.push_back(error.what());
            } catch (...) {
                progress.m_errors.push_back("一時ファイルの削除に失敗しました。");
            }
            progress.m_cleanup_done=true;
        });
        if (options.overwrite) {
            report(progress,callbacks,"インストール済みスキンの情報を収集しています...");
            for (const auto& record:seq) {
                const auto& [name,author,type,path,archive]=record;
                (void)type;(void)path;(void)archive;
                removes_table.emplace(std::make_pair(name,author),
                                       ::cw::skin::util::find_skin(name,author));
                if (should_cancel(progress,callbacks)) break;
            }
        }

        for (const auto& record:seq) {
            if (should_cancel(progress,callbacks)) break;
            const auto& [name,author,type,path,archive]=record;
            String unused_message;
            ::cw::lib::Integer old_value{},unused_maximum{};
            progress.snapshot(unused_message,old_value,unused_maximum);
            {
                std::vector<String> removes;
                if (options.overwrite) removes=removes_table[std::make_pair(name,author)];
                String installed;
                try {
                    installed=::cw::skin::util::install_skin(path,tempdir,callback);
                    if (installed.empty()) throw ::cw::skin::util::SkinInstallError(
                        filename(path)+" のインストールに失敗しました。");
                } catch (const ::cw::skin::util::SkinInstallError& error) {
                    progress.m_errors.push_back(error.m_message.empty()
                        ? String(error.what()) : error.m_message);
                    progress.set_value(old_value+
                        (archive ? ::cw::skin::util::g_INSTALL_PROGRESS_ARCHIVE
                                 : ::cw::skin::util::g_INSTALL_PROGRESS)+
                        (options.overwrite ? 1 : 0));
                    continue;
                }
                progress.m_installed_skindirnames.emplace_back(filename(installed),name,author,type,archive);
                if (installed_first.empty()) installed_first=filename(installed);

                String display=name.empty()?String("(無名のスキン)"):name;
                if (!author.empty()) display+="("+author+")";
                if (options.overwrite) report(progress,callbacks,
                    "「"+display+"」のFaceフォルダをコピーしています...");
                else if (!removes.empty()) {
                    String current_message;
                    ::cw::lib::Integer unused_value{},unused_maximum2{};
                    progress.snapshot(current_message,unused_value,unused_maximum2);
                    report(progress,callbacks,current_message);
                }

                for (const auto& remove_name:removes) {
                    const auto remove_path_string=::cw::lib::encoding::path_to_utf8(fs::u8path("Data")/"Skin"/remove_name);
                    const auto source_face=fs::u8path(remove_path_string)/"Face";
                    const auto destination_face=fs::u8path(installed)/"Face";
                    if (fs::is_directory(source_face,ec)) {
                        ::cw::util::copytree_overwrite(::cw::lib::encoding::path_to_utf8(source_face),::cw::lib::encoding::path_to_utf8(destination_face),2);
                    }
                    if (filename(installed)!=rename_table[remove_name]) rename_table[remove_name]=filename(installed);
                    progress.m_all_removes_repl.push_back(remove_path_string);
                }
                if (options.remove_installed) progress.m_all_removes_base.push_back(path);
            }}

        if (!progress.cancelled()) {
            report(progress,callbacks,"スキンの参照情報を更新してます...");
            update_yado_skin_references(rename_table);
            sync_files();

            String new_skin;
            bool restart=false;
            bool skin_switch_completed=true;
            {
            ::cw::lib::RuntimeAccess runtime_access;
            if (options.switch_skin && !installed_first.empty()) {
                new_skin=installed_first;
                if (::cw::g_cwpy && ::cw::g_cwpy->m_setting)
                    restart=::cw::g_cwpy->m_setting->m_skindirname!=new_skin;
            }
            report(progress,callbacks,"スキンの切り替えを行っています...");
            if (::cw::g_cwpy) {
                const auto game=::cw::g_cwpy;
                const auto table=rename_table;
                String target=new_skin;
                if (target.empty() && game->m_setting) {
                    const auto found=table.find(game->m_setting->m_skindirname);
                    if (found!=table.end()) target=found->second;
                }
                // 使用中でないスキンのインストールでは、実行状態の切り替え
                // は不要である。 単独の設定ホストなど、ゲームループが動い
                // ていないという理由だけで、この安全な経路を拒否しない。
                if (target.empty()) {
                    target.clear();
                } else if (!game->m__running.load(std::memory_order_acquire)) {
                    skin_switch_completed=false;
                    progress.m_errors.push_back("スキンの切り替え先ゲームスレッドが停止しています。旧スキンの削除を保留しました。");
                } else {
                // update_skin() を予約した直後に古
                // いスキンを削除すると、コールバックがまだそのパ
                // スから読み込み中の場合があり、 不完全な切り替
                // えやロック・削除されたリソースを生む。sync_exec()
                // でリソース再構築の完了を順序の区切り
                // とし、 完了後にだけ削除を許可する。
                    try {
                        game->sync_exec([game,target,restart](const Arguments&,const KeywordArguments&) {
                            if (!game || !game->m_setting) return Value{};
                            if (target.empty()) return Value{};
                            if (game->m_ydata && *game->m_ydata) (*game->m_ydata)->changed();
                            game->stop_allsounds(true);
                            // Python の install_skin() と同様、changearea=True
                            // 、switch_skin=True、switch_yado=False
                            // とする。 使用中スキンの置き換えで、衝突回避のディレク
                            // トリー名が変わったというだけでオープニングを再開しない。
                            game->update_skin(target,true,restart,std::nullopt,true,false);
                            // イベント実行中は update_skin() が
                            // 自身を遅延させる場合がある。 その場合、sync_exec()
                            // だけでは遅延の予約を待つだけで、リソース
                            // 再構築の完了は待てない。 設定から完了を確
                            // 認できるまで、古いツリーの削除を拒否する。
                            if (!game->m_setting ||
                                game->m_setting->m_skindirname!=target ||
                                game->m_is_updating_skin)
                                throw std::runtime_error("スキン切り替えが完了していません。");
                            return Value{};
                        });
                    } catch (const std::exception& error) {
                        skin_switch_completed=false;
                        progress.m_errors.push_back(
                            String("スキンの切り替えに失敗したため、旧スキンの削除を保留しました: ")+
                            error.what());
                    } catch (...) {
                        skin_switch_completed=false;
                        progress.m_errors.push_back("スキンの切り替えに失敗したため、旧スキンの削除を保留しました。");
                    }
                }
            } else if (!new_skin.empty()) {
                if (!callbacks.switch_skin) {
                    skin_switch_completed=false;
                    progress.m_errors.push_back("スキン切り替えUIが利用できないため、旧スキンの削除を保留しました。");
                } else {
                    try {
                        callbacks.switch_skin(new_skin,restart);
                    } catch (const std::exception& error) {
                        skin_switch_completed=false;
                        progress.m_errors.push_back(
                            String("スキンの切り替えに失敗したため、旧スキンの削除を保留しました: ")+
                            error.what());
                    } catch (...) {
                        skin_switch_completed=false;
                        progress.m_errors.push_back("スキンの切り替えに失敗したため、旧スキンの削除を保留しました。");
                    }
                }
            }
            }
            if (options.overwrite && skin_switch_completed) {
                report(progress,callbacks,"置換されたスキンを削除しています...");
                remove_replaced_skins(progress);
            } else if (options.overwrite) {
                report(progress,callbacks,"スキン切り替え未完了のため、置換元を削除せず保留しています...");
            }
            // Python は update_skin の例外時に入力元の後処理へ進まない。
            // ネイティブのエラー表示とカーソルの後処理は行いつつ、入力データもすべて保持する。
            if (options.remove_installed && skin_switch_completed) {
                report(progress,callbacks,"インストール済みのファイルを削除しています...");
                remove_install_sources(progress);
            } else if (options.remove_installed) {
                report(progress,callbacks,"スキン切り替え未完了のため、インストール元を削除せず保留しています...");
            }
            report(progress,callbacks,"スキンのインストールが完了しました。");
        } else {
            report(progress,callbacks,"スキンのインストールをキャンセルしました。");
        }
        progress.m_finished=true;
    };

    ::cw::lib::ExceptionPtr worker_error;
    std::atomic_bool worker_finished{false};
    std::thread worker([&] {
        try {
            run_install();
        } catch (...) {
            worker_error=::cw::lib::capture_exception();
        }
        worker_finished=true;
    });
    if(ui->dialog){
        const std::weak_ptr<DefaultUiSession> weak_ui=ui;
        ui->dialog->set_modal_callbacks([&worker_finished,&progress,&apply_pending_cursor,weak_ui] {
            if (const auto current=weak_ui.lock()) current->update(progress);
            apply_pending_cursor();
            return worker_finished.load();
        });
        (void)::cw::dialog::show_common_modal_presentation(ui->dialog);
    }else{
        // 画面なしの場合の安全な代替処理。ここでは別の UI を作らない。
        while (!worker_finished) {
            apply_pending_cursor();
            ::cw::lib::without_runtime_access([] {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            });
        }
    }
    ::cw::lib::without_runtime_access([&] { worker.join(); });
    ui->update(progress);
    apply_pending_cursor();
    request_cursor(old_cursor);
    apply_pending_cursor();
    ui->close();
    if (worker_error) {
        install_finished=true;
        ::cw::lib::rethrow_exception(worker_error);
    }
    if (callbacks.errors && !progress.m_errors.empty()) callbacks.errors(progress.m_errors);
    install_finished=true;
    return progress.m_installed_skindirnames;
}

void main() {}
}
