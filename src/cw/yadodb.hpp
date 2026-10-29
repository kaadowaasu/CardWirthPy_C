#pragma once

// cw/yadodb.pyの移植。共通のSQLite RAII接続を正式な管理元
// とする。 型付きの保持先はPythonのヘッダー・キャッシュの動作を保つため
// のもので、 SQLのトランザクション・スキーマ・エラーの意味は置き換えない。
#include "cw/lib/compat.hpp"
#include "cw/fwd.hpp"
#include "cw/lib/sqlite.hpp"

namespace cw::yadodb {
class YadoDB : public ::cw::lib::Object {
public:
    YadoDB() = default;
    virtual ~YadoDB();
    ::cw::lib::Value m_con{};
    ::cw::lib::Value m_cur{};
    ::cw::lib::Integer m_mode{};
    ::cw::lib::String m_name{};
    ::cw::lib::String m_ypath{};

    struct ImageRow {
        ::cw::lib::Integer numorder{};
        ::cw::lib::String imgpath{};
        std::optional<::cw::lib::String> postype{};
    };
    struct CardRow {
        ::cw::lib::String fpath{}; ::cw::lib::String type{}; ::cw::lib::Integer id{};
        ::cw::lib::String name{}; std::optional<::cw::lib::String> imgpath{};
        ::cw::lib::String desc{}; ::cw::lib::String scenario{}; ::cw::lib::String author{};
        ::cw::lib::String keycodes{}; ::cw::lib::Integer uselimit{}; ::cw::lib::String target{};
        bool allrange{}; ::cw::lib::String premium{}; ::cw::lib::String physical{}; ::cw::lib::String mental{};
        ::cw::lib::Integer level{}; ::cw::lib::Integer maxuselimit{}; ::cw::lib::Integer price{};
        bool hold{}; ::cw::lib::Integer enhance_avo{}; ::cw::lib::Integer enhance_res{}; ::cw::lib::Integer enhance_def{};
        ::cw::lib::Integer enhance_avo_used{}; ::cw::lib::Integer enhance_res_used{}; ::cw::lib::Integer enhance_def_used{};
        bool attachment{}; ::cw::lib::Integer moved{}; bool scenariocard{}; ::cw::lib::String versionhint{};
        ::cw::lib::String wsnversion{}; ::cw::lib::Integer star{}; bool resetvariables{};
        ::cw::lib::Real ctime{}; ::cw::lib::Real mtime{}; std::vector<ImageRow> images{};
    };
    struct AdventurerRow {
        ::cw::lib::String fpath{}; ::cw::lib::Integer level{}; ::cw::lib::String name{}; ::cw::lib::String desc{};
        std::optional<::cw::lib::String> imgpath{}; bool album{}; bool lost{}; ::cw::lib::String sex{}; ::cw::lib::String age{};
        ::cw::lib::Integer ep{}; bool leavenoalbum{}; ::cw::lib::String gene{}; ::cw::lib::String history{}; ::cw::lib::String race{};
        ::cw::lib::String versionhint{}; ::cw::lib::String wsnversion{}; ::cw::lib::Real ctime{}; ::cw::lib::Real mtime{};
        std::vector<ImageRow> images{};
    };
    struct PartyRow { ::cw::lib::String fpath{}; ::cw::lib::String name{}; ::cw::lib::Integer money{}; ::cw::lib::String members{}; ::cw::lib::Real ctime{}; ::cw::lib::Real mtime{}; };
    struct PartyRecordRow { ::cw::lib::String fpath{}; ::cw::lib::String name{}; ::cw::lib::Integer money{}; ::cw::lib::String members{}; ::cw::lib::String membernames{}; ::cw::lib::String backpack{}; ::cw::lib::Real ctime{}; ::cw::lib::Real mtime{}; };
    struct SavedImageRow { ::cw::lib::String fpath{}; ::cw::lib::String scenarioname{}; ::cw::lib::String scenarioauthor{}; ::cw::lib::String dpath{}; ::cw::lib::String fpaths{}; ::cw::lib::Real ctime{}; ::cw::lib::Real mtime{}; };

    // 型付きテーブルも、PRIMARY KEYとINSERT OR REPLACEの動作に合わせる。
    std::map<::cw::lib::String, CardRow> m_cards{};
    std::map<::cw::lib::String, ::cw::lib::Integer> m_cardorder{};
    std::map<::cw::lib::String, AdventurerRow> m_adventurers{};
    std::map<::cw::lib::String, ::cw::lib::Integer> m_adventurerorder{};
    std::map<::cw::lib::String, PartyRow> m_parties{};
    std::map<::cw::lib::String, ::cw::lib::Integer> m_partyorder{};
    std::map<::cw::lib::String, PartyRecordRow> m_partyrecords{};
    std::map<::cw::lib::String, SavedImageRow> m_savedjpdcimages{};
    bool m_open{};
    // Pythonのオブジェクトは、生存期間全体で一つのsqlite3
    // 接続を所有する。 ここでも同じ所有範囲とし、Connection
    // 自体は移動可能で、デストラクターがネイティブハンドルを閉じる。
    std::unique_ptr<::cw::lib::sqlite::Connection> m_sqlite{};
    bool m_transaction_open{};
    void initialize(::cw::lib::String arg_ypath, ::cw::lib::Integer arg_mode = 0);
    void update(::cw::lib::Value arg_cards, ::cw::lib::Value arg_adventurers, ::cw::lib::Value arg_parties, std::optional<std::map<::cw::lib::String, ::cw::lib::Integer>> arg_cardorder, std::optional<std::map<::cw::lib::String, ::cw::lib::Integer>> arg_adventurerorder, std::optional<std::map<::cw::lib::String, ::cw::lib::Integer>> arg_partyorder, ::cw::lib::Value arg_partyrecord, ::cw::lib::Value arg_savedjpdcimage, std::map<::cw::lib::String, std::shared_ptr<::cw::header::CardHeader>> arg_cardheaders = {}, std::map<::cw::lib::String, std::shared_ptr<::cw::header::AdventurerHeader>> arg_adventurerheaders = {}, std::map<::cw::lib::String, std::shared_ptr<::cw::header::PartyHeader>> arg_partyheaders = {}, std::map<::cw::lib::String, std::shared_ptr<::cw::header::PartyRecordHeader>> arg_partyrecordheaders = {}, std::map<::cw::lib::String, std::shared_ptr<::cw::header::SavedJPDCImageHeader>> arg_savedheaders = {});
    void vacuum(bool arg_commit = true);
    void _delete_card(::cw::lib::String arg_path, bool arg_commit);
    void _delete_adventurer(::cw::lib::String arg_path, bool arg_commit);
    void _delete_party(::cw::lib::String arg_path, bool arg_commit);
    void _delete_partyrecord(::cw::lib::String arg_path, bool arg_commit);
    void _delete_savedjpdcimage(::cw::lib::String arg_path, bool arg_commit);
    void delete_savedjpdcimage(::cw::lib::String arg_path, bool arg_commit = true);
    void insert_cardheader(std::shared_ptr<::cw::header::CardHeader> arg_header, bool arg_commit = true, ::cw::lib::Integer arg_cardorder = -1);
    void _insert_cardheader(std::shared_ptr<::cw::header::CardHeader> arg_header, bool arg_commit, ::cw::lib::Integer arg_cardorder);
    void insert_card(::cw::lib::String arg_path, bool arg_commit = true, ::cw::lib::Integer arg_cardorder = -1);
    void _insert_card(::cw::lib::String arg_path, bool arg_commit, ::cw::lib::Integer arg_cardorder);
    std::vector<std::shared_ptr<::cw::header::CardHeader>> get_cards();
    std::vector<::cw::lib::String> get_cardfpaths(bool arg_scenariocard = true);
    void insert_adventurerheader(std::shared_ptr<::cw::header::AdventurerHeader> arg_header, bool arg_commit = true, ::cw::lib::Integer arg_adventurerorder = -1);
    void _insert_adventurerheader(std::shared_ptr<::cw::header::AdventurerHeader> arg_header, bool arg_commit, ::cw::lib::Integer arg_adventurerorder);
    void insert_adventurer(::cw::lib::String arg_path, bool arg_album, bool arg_commit = true, ::cw::lib::Integer arg_adventurerorder = -1);
    void _insert_adventurer(::cw::lib::String arg_path, bool arg_album, bool arg_commit, ::cw::lib::Integer arg_adventurerorder);
    std::vector<std::shared_ptr<::cw::header::AdventurerHeader>> get_adventurers(bool arg_album);
    std::vector<std::shared_ptr<::cw::header::AdventurerHeader>> get_standbys();
    std::vector<::cw::lib::String> get_standbynames();
    std::vector<std::shared_ptr<::cw::header::AdventurerHeader>> get_album();
    void insert_partyheader(std::shared_ptr<::cw::header::PartyHeader> arg_header, bool arg_commit = true, ::cw::lib::Integer arg_partyorder = -1);
    void _insert_partyheader(std::shared_ptr<::cw::header::PartyHeader> arg_header, bool arg_commit, ::cw::lib::Integer arg_partyorder);
    void insert_party(::cw::lib::String arg_path, bool arg_commit = true, ::cw::lib::Integer arg_partyorder = -1);
    void _insert_party(::cw::lib::String arg_path, bool arg_commit, ::cw::lib::Integer arg_partyorder);
    std::vector<std::shared_ptr<::cw::header::PartyHeader>> get_parties();
    void insert_partyrecordheader(std::shared_ptr<::cw::header::PartyRecordHeader> arg_header, bool arg_commit = true);
    void _insert_partyrecordheader(std::shared_ptr<::cw::header::PartyRecordHeader> arg_header, bool arg_commit);
    void insert_partyrecord(::cw::lib::String arg_path, bool arg_commit = true);
    void _insert_partyrecord(::cw::lib::String arg_path, bool arg_commit);
    std::vector<std::shared_ptr<::cw::header::PartyRecordHeader>> get_partyrecord();
    void insert_savedjpdcimageheader(std::shared_ptr<::cw::header::SavedJPDCImageHeader> arg_header, bool arg_commit = true);
    void _insert_savedjpdcimageheader(std::shared_ptr<::cw::header::SavedJPDCImageHeader> arg_header, bool arg_commit);
    void insert_savedjpdcimage(::cw::lib::String arg_path, bool arg_commit = true);
    void _insert_savedjpdcimage(::cw::lib::String arg_path, bool arg_commit);
    std::map<std::tuple<::cw::lib::String, ::cw::lib::String>, std::shared_ptr<::cw::header::SavedJPDCImageHeader>> get_savedjpdcimage();
    void commit();
    void close();

private:
    std::vector<const CardRow*> ordered_card_rows() const;
    void require_open() const;
    void begin_mutation();
    void finish_mutation(bool requested_commit);
    void rollback_transaction() noexcept;
    void migrate_schema();
    void load_cache();
};

extern ::cw::lib::Integer g_PARTY;
extern ::cw::lib::Integer g_YADO;
extern ::cw::lib::Value g__lock;

void YadoDB_update_walk(::cw::lib::String arg_dpath, ::cw::lib::Value arg_headertable, ::cw::lib::String arg_xmlname, ::cw::lib::Callback arg_insert, ::cw::lib::Callback arg_insertheader, const ::cw::lib::Arguments& arg_args);
void YadoDB_update_walk(::cw::lib::String arg_dpath, const ::cw::lib::ValueMap& arg_headertable,
                        ::cw::lib::String arg_xmlname, ::cw::lib::Callback arg_insert,
                        ::cw::lib::Callback arg_insertheader, const ::cw::lib::Arguments& arg_args);
}

