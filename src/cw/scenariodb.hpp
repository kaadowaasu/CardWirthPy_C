#pragma once

// cw/scenariodb.py から移植した型付き宣言。
#include "cw/lib/exception_origin.hpp"
#include "cw/lib/compat.hpp"
#include "cw/lib/sqlite.hpp"
#include "cw/fwd.hpp"

#include <atomic>
#include <exception>

namespace cw::scenariodb {
// キャッシュの絞り込みとDB検索で共用する、数値検索用の変換。
std::optional<::cw::lib::Integer> parse_level_search(const ::cw::lib::String& text);
class Scenariodb : public ::cw::lib::Object {
public:
    Scenariodb();
    virtual ~Scenariodb() = default;
    inline static ::cw::lib::String s_FETCH_SQL{};
    ::cw::lib::Value m_con{};
    ::cw::lib::Value m_cur{};
    // SQLite接続本体。m_con/m_curはPython互換の動的公開スロットとして残し、
    // 永続化・行取得は型付きRAII接続へ集約する。
    std::unique_ptr<::cw::lib::sqlite::Connection> m_sqlite{};
    bool m_transaction_open{};
    // DB行をScenarioHeaderへ変換した共有cache。永続データの正本はm_sqlite。
    std::vector<std::shared_ptr<::cw::header::ScenarioHeader>> m_headers{};
    using ImageRecord = std::tuple<std::optional<::cw::lib::Bytes>, std::shared_ptr<::cw::image::ImageInfo>, ::cw::lib::Integer>;
    // DB行が画像列を持たない場合も、scenarioimage相当の順序・scale・postypeを
    // 失わないように保持する。行取得時にSQLiteから再構成する。
    std::map<::cw::lib::String, std::vector<ImageRecord>> m_image_records{};
    ::cw::lib::String m_name{};
    void initialize();
    void update(::cw::lib::String arg_dpath, ::cw::lib::String arg_skintype, bool arg_commit, bool arg_update);
    void vacuum(bool arg_commit);
    void py_delete(::cw::lib::String arg_path, bool arg_commit);
    void delete_all(bool arg_commit);
    void insert(std::tuple<::cw::lib::String, ::cw::lib::Integer, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::String, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::String, ::cw::lib::Real, ::cw::lib::Real, ::cw::lib::String, std::optional<::cw::lib::Bytes>, std::optional<::cw::lib::String>> arg_t, std::vector<std::tuple<std::optional<::cw::lib::Bytes>, std::shared_ptr<::cw::image::ImageInfo>, ::cw::lib::Integer>> arg_images, bool arg_commit, ::cw::lib::String arg_skintype);
    void insert_scenario(::cw::lib::String arg_path, bool arg_commit, ::cw::lib::String arg_skintype);
    void register_scenario_type(const ::cw::lib::String& dpath, const ::cw::lib::String& fname,
                                const ::cw::lib::String& skintype, bool commit);
    std::optional<bool> _insert_scenario(::cw::lib::String arg_path, bool arg_commit, ::cw::lib::String arg_skintype);
    std::optional<std::shared_ptr<::cw::header::ScenarioHeader>> create_header(std::optional<::cw::lib::Value> arg_data, ::cw::lib::String arg_skintype, bool arg_update);
    std::tuple<std::vector<std::shared_ptr<::cw::header::ScenarioHeader>>, std::set<::cw::lib::String>> create_headers(std::vector<::cw::lib::Value> arg_data, ::cw::lib::String arg_skintype, bool arg_update);
    // Pythonと同様、呼び出し元の一覧を書き換える。ネイティブ側
    // には配列の戻り値も提供し、 右辺値も同じ並べ替え処理を通す。
    std::vector<std::shared_ptr<::cw::header::ScenarioHeader>> sort_headers(std::vector<std::shared_ptr<::cw::header::ScenarioHeader>>& arg_headers);
    std::vector<std::shared_ptr<::cw::header::ScenarioHeader>> sort_headers(std::vector<std::shared_ptr<::cw::header::ScenarioHeader>>&& arg_headers) { return sort_headers(arg_headers); }
    std::optional<std::shared_ptr<::cw::header::ScenarioHeader>> search_path(::cw::lib::String arg_path, ::cw::lib::String arg_skintype);
    std::optional<std::shared_ptr<::cw::header::ScenarioHeader>> _search_path(::cw::lib::String arg_path, ::cw::lib::String arg_skintype);
    void _fetch(::cw::lib::String arg_dpath, ::cw::lib::String arg_fname, ::cw::lib::String arg_skintype);
    void _fetch_from_name(::cw::lib::String arg_name, ::cw::lib::String arg_author, ::cw::lib::String arg_skintype);
    std::vector<std::shared_ptr<::cw::header::ScenarioHeader>> search_dpath(::cw::lib::String arg_dpath, bool arg_create, ::cw::lib::String arg_skintype, bool arg_update);
    std::optional<std::shared_ptr<::cw::header::ScenarioHeader>> get_header(::cw::lib::String arg_path, ::cw::lib::String arg_skintype);
    std::vector<std::shared_ptr<::cw::header::ScenarioHeader>> find_headers(std::vector<::cw::lib::Integer> arg_ftypes, ::cw::lib::String arg_value, ::cw::lib::String arg_skintype);
    std::vector<std::shared_ptr<::cw::header::ScenarioHeader>> find_scenario(::cw::lib::String arg_name, ::cw::lib::String arg_author, ::cw::lib::String arg_skintype, ::cw::lib::String arg_ignore_dpath, ::cw::lib::String arg_ignore_fname);
    void rename_dir(::cw::lib::String arg_before, ::cw::lib::String arg_after);
    void remove_dir(::cw::lib::String arg_dpath);
    void commit();
    void close();
};

class ScenariodbUpdatingThread : public ::cw::lib::Thread {
public:
    ScenariodbUpdatingThread() = default;
    virtual ~ScenariodbUpdatingThread();
    // PythonではGILで直列化されるクラス共有フラグ。C++で
    // はUIがワーカーと並行して読むため、 atomicで同じ可視
    // 性を保証する。Frame起動前には、ヘッドレスDB利用時も含
    // め更新ワーカーは存在しない。 start()は実行予約前にfalse
    // を公開し、未予約のワーカーを完了済みと誤認させない。
    inline static std::atomic_bool s__finished{true};
    ::cw::lib::String m__dpath{"Scenario"};
    ::cw::lib::String m__skintype{};
    bool m__vacuum{};
    std::shared_ptr<::cw::setting::Setting> m_setting{};
    void initialize(std::shared_ptr<::cw::setting::Setting> arg_setting, bool arg_vacuum = false, ::cw::lib::String arg_dpath = "Scenario", ::cw::lib::String arg_skintype = "");
    void start(bool retain_database = false);
    void wait();
    std::shared_ptr<Scenariodb> completed_database();
    void run();
    static bool is_finished();
private:
    std::vector<std::tuple<::cw::lib::String, ::cw::lib::String>> m_folders{};
    bool m_retain_database{};
    std::shared_ptr<Scenariodb> m_completed_database{};
    ::cw::lib::ExceptionPtr m_error{};
};

extern ::cw::lib::Integer g_DATA_AUTHOR;
extern ::cw::lib::Integer g_DATA_DESC;
extern ::cw::lib::Integer g_DATA_FNAME;
extern ::cw::lib::Integer g_DATA_LEVEL;
extern ::cw::lib::Integer g_DATA_TITLE;
extern ::cw::lib::Integer g_TYPE_CLASSIC;
extern ::cw::lib::Integer g_TYPE_NEXT;
extern ::cw::lib::Integer g_TYPE_WSN;
extern ::cw::lib::Value g__lock;
extern ::cw::lib::Value g_sqlite3_Row;

void Scenariodb_update_update_path(::cw::lib::Value arg_t, ::cw::lib::String arg_spath, ::cw::lib::String arg_path);
std::optional<std::shared_ptr<::cw::header::ScenarioHeader>> Scenariodb_create_header_func(::cw::lib::String arg_spath, std::shared_ptr<::cw::header::ScenarioHeader> arg_header);
::cw::lib::String Scenariodb_find_headers_encode_like(::cw::lib::String arg_value);
std::set<::cw::lib::String> find_alldirectories(::cw::lib::String arg_dpath, std::optional<::cw::lib::Callback> arg_is_cancel);
// 互換用の補助関数。result/excludeはスナップショットとし
// て扱い、呼び出し元の集合を変更しない。 見つかった集合を戻り値で受け
// 取る必要があればfind_alldirectories()を使う。
void _find_alldirectories(::cw::lib::String arg_dpath, std::set<::cw::lib::String> arg_result, std::set<::cw::lib::String> arg_exclude, std::optional<::cw::lib::Callback> arg_is_cancel);
bool is_scenario(::cw::lib::String arg_path);
std::tuple<std::optional<std::tuple<::cw::lib::String, ::cw::lib::Integer, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::String, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::String, ::cw::lib::Real, ::cw::lib::Real, ::cw::lib::String, std::optional<::cw::lib::Bytes>, std::optional<::cw::lib::String>>>, std::vector<std::tuple<std::optional<::cw::lib::Bytes>, std::shared_ptr<::cw::image::ImageInfo>, ::cw::lib::Integer>>> read_summary(::cw::lib::String arg_basepath);
std::tuple<std::tuple<::cw::lib::String, ::cw::lib::Integer, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::String, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::String, ::cw::lib::Real, ::cw::lib::Real, ::cw::lib::String, std::optional<::cw::lib::Bytes>, std::optional<::cw::lib::String>>, std::vector<std::tuple<std::optional<::cw::lib::Bytes>, std::shared_ptr<::cw::image::ImageInfo>, ::cw::lib::Integer>>> read_summary_imgbufs_to_result(std::tuple<::cw::lib::String, ::cw::lib::Integer, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::String, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::String, ::cw::lib::Real, ::cw::lib::Real, ::cw::lib::String> arg_summaryinfos, std::vector<std::tuple<std::optional<::cw::lib::Bytes>, std::shared_ptr<::cw::image::ImageInfo>, ::cw::lib::Integer>> arg_imgbufs);
std::tuple<std::vector<std::shared_ptr<::cw::image::ImageInfo>>, std::tuple<::cw::lib::String, ::cw::lib::Integer, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::String, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::String, ::cw::lib::Real, ::cw::lib::Real, ::cw::lib::String>> parse_summarydata(::cw::lib::String arg_basepath, std::shared_ptr<::cw::data::CWPyElement> arg_data, ::cw::lib::Integer arg_scetype, ::cw::lib::Real arg_mtime, std::map<::cw::lib::String, ::cw::lib::String> arg_rootattrs);
std::tuple<std::optional<std::tuple<::cw::lib::String, ::cw::lib::Integer, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::String, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::String, ::cw::lib::Real, ::cw::lib::Real, ::cw::lib::String, std::optional<::cw::lib::Bytes>, std::optional<::cw::lib::String>>>, std::vector<std::tuple<std::optional<::cw::lib::Bytes>, std::shared_ptr<::cw::image::ImageInfo>, ::cw::lib::Integer>>> read_summary_classic(::cw::lib::String arg_basepath, ::cw::lib::String arg_spath, std::optional<std::shared_ptr<::cw::binary::cwfile::CWFile>> arg_f);
std::vector<::cw::lib::String> get_scenariopaths(::cw::lib::String arg_path);
std::optional<std::shared_ptr<::cw::data::ScenarioData>> get_scenario(::cw::lib::String arg_fpath);
void main();
}

