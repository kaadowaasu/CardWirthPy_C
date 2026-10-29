#define _SILENCE_CXX20_U8PATH_DEPRECATION_WARNING
#include "cw/yadodb.hpp"
#include "cw/cw.hpp"
#include "cw/data.hpp"
#include "cw/header.hpp"
#include "cw/image.hpp"
#include "cw/util.hpp"
#include "cw/lib/encoding.hpp"
#include "cw/lib/python_text.hpp"
#include "platform/storage/storage_mount.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <type_traits>

namespace cw::yadodb {
namespace {
using String = ::cw::lib::String;
using Integer = ::cw::lib::Integer;
using Value = ::cw::lib::Value;
String path_utf8(const std::filesystem::path& path) { return ::cw::lib::encoding::path_to_utf8(path); }
std::recursive_mutex s_db_mutex;
struct DbGuard { std::lock_guard<std::recursive_mutex> lock{s_db_mutex}; };

bool mounted_path(const std::filesystem::path& path)
{
    return ::cardwirth::platform::is_mounted_user_path(path);
}

std::optional<::cardwirth::platform::StorageStat> path_status(
    const std::filesystem::path& path)
{
    if (mounted_path(path)) {
        const auto status = ::cardwirth::platform::stat_mounted_user_path(path, true);
        return status;
    }
    std::error_code ec;
    const auto status = std::filesystem::symlink_status(path, ec);
    if (ec == std::errc::no_such_file_or_directory || status.type() == std::filesystem::file_type::not_found)
        return ::cardwirth::platform::StorageStat{};
    if (ec) throw std::filesystem::filesystem_error("cannot stat indexed file", path, ec);
    ::cardwirth::platform::StorageStat result;
    result.type = std::filesystem::is_regular_file(status)
        ? ::cardwirth::platform::StorageEntryType::File
        : std::filesystem::is_directory(status)
            ? ::cardwirth::platform::StorageEntryType::Directory
            : std::filesystem::is_symlink(status)
                ? ::cardwirth::platform::StorageEntryType::Symlink
                : ::cardwirth::platform::StorageEntryType::Other;
    if (result.type == ::cardwirth::platform::StorageEntryType::File)
        result.size = std::filesystem::file_size(path, ec);
    if (ec) throw std::filesystem::filesystem_error("cannot read indexed file size", path, ec);
    return result;
}

bool path_is_file(const std::filesystem::path& path)
{
    const auto status = path_status(path);
    return status && status->is_file();
}

bool path_is_directory(const std::filesystem::path& path)
{
    const auto status = path_status(path);
    return status && status->is_directory();
}

std::vector<std::filesystem::path> list_directory(const std::filesystem::path& path)
{
    if (mounted_path(path)) {
        std::vector<std::filesystem::path> result;
        if (const auto entries = ::cardwirth::platform::list_mounted_user_path(path, true)) {
            result.reserve(entries->size());
            for (const auto& entry : *entries) result.push_back(entry.path);
        } else {
            const auto status = path_status(path);
            if (status && status->exists())
                throw std::runtime_error("cannot enumerate indexed directory: " + path_utf8(path));
        }
        return result;
    }
    std::vector<std::filesystem::path> result;
    std::error_code ec;
    for (std::filesystem::directory_iterator it(path, ec), end; !ec && it != end;
         it.increment(ec)) result.push_back(it->path());
    if (ec && ec != std::errc::no_such_file_or_directory)
        throw std::filesystem::filesystem_error("cannot enumerate indexed directory", path, ec);
    return result;
}

bool ensure_directory(const std::filesystem::path& path)
{
    if (mounted_path(path))
        return ::cardwirth::platform::make_mounted_user_directory(path, true);
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    return !ec;
}

// yadodb.pyと同じテーブル・列の定義。compat::Database
// が実行できない段階でもSQL文字列として保持し、実際のSQLite
// カーソルを導入した際に移行処理や列順序が変わらないようにする。
const std::vector<std::string_view> k_schema_sql{
    "CREATE TABLE card (fpath TEXT, type INTEGER, id INTEGER, name TEXT, imgpath TEXT, desc TEXT, scenario TEXT, author TEXT, keycodes TEXT, uselimit INTEGER, target TEXT, allrange INTEGER, premium TEXT, physical TEXT, mental TEXT, level INTEGER, maxuselimit INTEGER, price INTEGER, hold INTEGER, enhance_avo INTEGER, enhance_res INTEGER, enhance_def INTEGER, enhance_avo_used INTEGER, enhance_res_used INTEGER, enhance_def_used INTEGER, attachment INTEGER, moved INTEGER, scenariocard INTEGER, versionhint TEXT, wsnversion TEXT, star INTEGER, resetvariables INTEGER, ctime INTEGER, mtime INTEGER, PRIMARY KEY (fpath))",
    "CREATE TABLE cardimage (fpath TEXT, numorder INTEGER, imgpath TEXT, postype TEXT, PRIMARY KEY (fpath, numorder))",
    "CREATE TABLE cardorder (fpath TEXT, numorder INTEGER, PRIMARY KEY (fpath))",
    "CREATE TABLE adventurer (fpath TEXT, level INTEGER, name TEXT, desc TEXT, imgpath TEXT, album INTEGER, lost INTEGER, sex TEXT, age TEXT, ep INTEGER, leavenoalbum INTEGER, gene TEXT, history TEXT, race TEXT, versionhint TEXT, wsnversion TEXT, ctime INTEGER, mtime INTEGER, PRIMARY KEY (fpath))",
    "CREATE TABLE adventurerimage (fpath TEXT, numorder INTEGER, imgpath TEXT, postype TEXT, PRIMARY KEY (fpath, numorder))",
    "CREATE TABLE adventurerorder (fpath TEXT, numorder INTEGER, PRIMARY KEY (fpath))",
    "CREATE TABLE party (fpath TEXT, name TEXT, money INTEGER, members TEXT, ctime INTEGER, mtime INTEGER, PRIMARY KEY (fpath))",
    "CREATE TABLE partyorder (fpath TEXT, numorder INTEGER, PRIMARY KEY (fpath))",
    "CREATE TABLE partyrecord (fpath TEXT, name TEXT, money INTEGER, members TEXT, membernames TEXT, backpack TEXT, ctime INTEGER, mtime INTEGER, PRIMARY KEY (fpath))",
    "CREATE TABLE savedjpdcimage (fpath TEXT, scenarioname TEXT, scenarioauthor TEXT, dpath TEXT, fpaths TEXT, ctime INTEGER, mtime INTEGER, PRIMARY KEY (fpath))"
};
const std::vector<std::string_view> k_migration_sql{
    "CREATE TABLE cardorder (fpath TEXT, numorder INTEGER, PRIMARY KEY (fpath))",
    "CREATE TABLE cardimage (fpath TEXT, numorder INTEGER, imgpath TEXT, postype TEXT, PRIMARY KEY (fpath, numorder))",
    "ALTER TABLE cardimage ADD COLUMN postype TEXT",
    "CREATE TABLE adventurerorder (fpath TEXT, numorder INTEGER, PRIMARY KEY (fpath))",
    "CREATE TABLE adventurerimage (fpath TEXT, numorder INTEGER, imgpath TEXT, postype TEXT, PRIMARY KEY (fpath, numorder))",
    "ALTER TABLE adventurerimage ADD COLUMN postype TEXT",
    "CREATE TABLE partyorder (fpath TEXT, numorder INTEGER, PRIMARY KEY (fpath))",
    "ALTER TABLE card ADD COLUMN moved INTEGER", "UPDATE card SET moved=?",
    "ALTER TABLE card ADD COLUMN scenariocard INTEGER", "UPDATE card SET scenariocard=?",
    "ALTER TABLE card ADD COLUMN versionhint TEXT", "UPDATE card SET versionhint=?",
    "ALTER TABLE card ADD COLUMN wsnversion TEXT", "ALTER TABLE card ADD COLUMN star INTEGER", "UPDATE card SET star=?",
    "ALTER TABLE card ADD COLUMN resetvariables INTEGER", "UPDATE card SET resetvariables=?",
    "ALTER TABLE adventurer ADD COLUMN desc TEXT", "UPDATE adventurer SET mtime=?",
    "ALTER TABLE adventurer ADD COLUMN versionhint TEXT", "UPDATE adventurer SET versionhint=?",
    "ALTER TABLE adventurer ADD COLUMN wsnversion TEXT", "ALTER TABLE partyrecord ADD COLUMN membernames TEXT", "UPDATE partyrecord SET membernames=?",
    "CREATE TABLE partyrecord (fpath TEXT, name TEXT, money INTEGER, members TEXT, membernames TEXT, backpack TEXT, ctime INTEGER, mtime INTEGER, PRIMARY KEY (fpath))",
    "CREATE TABLE savedjpdcimage (fpath TEXT, scenarioname TEXT, scenarioauthor TEXT, dpath TEXT, fpaths TEXT, ctime INTEGER, mtime INTEGER, PRIMARY KEY (fpath))"
};
const std::vector<std::string_view> k_query_sql{
    "SELECT fpath,mtime FROM card", "SELECT fpath,mtime,album FROM adventurer", "SELECT fpath,mtime FROM party", "SELECT fpath,mtime FROM partyrecord", "SELECT fpath,mtime FROM savedjpdcimage",
    "DELETE FROM card WHERE fpath=?", "DELETE FROM cardimage WHERE fpath=?", "DELETE FROM cardorder WHERE fpath=?",
    "DELETE FROM adventurer WHERE fpath=?", "DELETE FROM adventurerimage WHERE fpath=?", "DELETE FROM adventurerorder WHERE fpath=?",
    "DELETE FROM party WHERE fpath=?", "DELETE FROM partyorder WHERE fpath=?", "DELETE FROM partyrecord WHERE fpath=?", "DELETE FROM savedjpdcimage WHERE fpath=?",
    "SELECT card.fpath,type,id,name,imgpath,desc,scenario,author,keycodes,uselimit,target,allrange,premium,physical,mental,level,maxuselimit,price,hold,enhance_avo,enhance_res,enhance_def,enhance_avo_used,enhance_res_used,enhance_def_used,attachment,moved,scenariocard,versionhint,wsnversion,star,resetvariables,ctime,mtime,numorder FROM card LEFT OUTER JOIN cardorder ON card.fpath=cardorder.fpath ORDER BY numorder,name",
    "SELECT imgpath,postype FROM cardimage WHERE fpath=? ORDER BY numorder", "SELECT card.fpath FROM card LEFT OUTER JOIN cardorder ON card.fpath=cardorder.fpath WHERE scenariocard=? ORDER BY numorder,name",
    "SELECT * FROM adventurer WHERE album=? ORDER BY name", "SELECT * FROM adventurer LEFT OUTER JOIN adventurerorder ON adventurer.fpath=adventurerorder.fpath WHERE lost=0 AND album=? ORDER BY numorder,name",
    "SELECT imgpath,postype FROM adventurerimage WHERE fpath=? ORDER BY numorder", "SELECT name,fpath FROM adventurer WHERE lost=0 AND album=? ORDER BY name",
    "SELECT * FROM party LEFT OUTER JOIN partyorder ON party.fpath=partyorder.fpath ORDER BY numorder,name", "SELECT * FROM partyrecord ORDER BY name", "SELECT * FROM savedjpdcimage"
};

bool value_bool(const Value& value, bool fallback)
{
    if (const auto* b = std::get_if<bool>(&value.storage())) return *b;
    if (const auto* i = std::get_if<Integer>(&value.storage())) return *i != 0;
    if (const auto* r = std::get_if<::cw::lib::Real>(&value.storage())) return *r != 0.0;
    if (const auto* decimal = std::get_if<::cw::lib::Decimal>(&value.storage())) return !decimal->is_zero();
    return fallback;
}

String value_string(const Value& value)
{
    if (const auto* s = std::get_if<String>(&value.storage())) return *s;
    if (const auto* decimal = std::get_if<::cw::lib::Decimal>(&value.storage())) return decimal->to_string(true);
    return {};
}

Integer value_integer(const Value& value, Integer fallback = 0)
{
    if (const auto* i = std::get_if<Integer>(&value.storage())) return *i;
    if (const auto* b = std::get_if<bool>(&value.storage())) return *b ? 1 : 0;
    if (const auto* decimal = std::get_if<::cw::lib::Decimal>(&value.storage())) {
        try { return decimal->to_integer(::cw::lib::DecimalRounding::Down); }
        catch (const ::cw::lib::DecimalError&) { return fallback; }
    }
    if (const auto* r = std::get_if<::cw::lib::Real>(&value.storage())) {
        if (!std::isfinite(*r)) return fallback;
        const auto truncated = std::trunc(*r);
        // この範囲外の浮動小数点から整数へのキャストは未定義。上のDecimal
        // 経路は正確であり、この条件付きの経路は旧呼び出し元から
        // 既に二進浮動小数点のRealが渡された場合だけに使う。
        const auto max_exclusive = static_cast<::cw::lib::Real>(std::numeric_limits<Integer>::max()) + 1.0;
        const auto min_inclusive = static_cast<::cw::lib::Real>(std::numeric_limits<Integer>::min());
        if (truncated >= max_exclusive || truncated < min_inclusive) return fallback;
        return static_cast<Integer>(truncated);
    }
    return fallback;
}

::cw::lib::Real file_mtime(const String& path)
{
    const auto unix_seconds = [](auto timestamp) {
        // PythonはUnix時刻の整数秒と秒未満を組み合わせる。
        // 全刻み数を先にdoubleへ変換すると下位ビットを失う。
        const auto whole = std::chrono::floor<std::chrono::seconds>(timestamp);
        return std::chrono::duration<::cw::lib::Real>(whole.time_since_epoch()).count() +
               static_cast<::cw::lib::Real>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                   timestamp - whole).count()) * 1e-9;
    };
    const auto native = std::filesystem::u8path(path);
    if (mounted_path(native)) {
        const auto status = ::cardwirth::platform::stat_mounted_user_path(native);
        if (!status)
            throw std::runtime_error("file timestamp unavailable: " + path);
        return unix_seconds(std::chrono::sys_time<std::chrono::nanoseconds>{
            std::chrono::nanoseconds{status->modified}});
    }
    const auto t = std::filesystem::last_write_time(native);
    // int64のUnixナノ秒で表せない日時でも、ファイル時計の精度を保つ。
    return unix_seconds(::cardwirth::platform::storage_file_time_to_system(t));
}

::cw::lib::Real now_seconds()
{
    return std::chrono::duration<::cw::lib::Real>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

String relative_path(const String& path, const String& base)
{
    auto relative = ::cw::util::relpath(path, base);
    std::replace(relative.begin(), relative.end(), '\\', '/');
    while (!relative.empty() && relative.back() == '/') relative.pop_back();
    return relative;
}

String path_key(String path)
{
    std::replace(path.begin(), path.end(), '\\', '/');
#if defined(_WIN32)
    // os.path.normcaseはUnicodeテキストに作用する。ASCIIバイトだけを小文
    // 字化すると、Windowsで大小文字を区別しない一つのパスがデータベース上で別のものになる。
    path = ::cw::lib::encoding::lower(path);
#endif
    return path;
}

String full_path(const String& base, const String& path)
{
    return ::cw::util::join_paths({Value(base), Value(path)});
}

std::vector<String> split_lines(const String& value)
{
    return ::cw::lib::python_split_newlines(value);
}

String join_lines(const std::vector<String>& values)
{
    std::ostringstream out;
    for (std::size_t i = 0; i < values.size(); ++i) { if (i) out << '\n'; out << values[i]; }
    return out.str();
}

std::vector<String> norm_unique_paths(const std::vector<String>& paths)
{
    std::vector<String> result;
    std::set<String> seen;
    for (const auto& path : paths) {
        if (seen.insert(path_key(path)).second) result.push_back(path);
    }
    return result;
}

std::vector<std::shared_ptr<::cw::image::ImageInfo>> header_images(const Value& paths)
{
    std::vector<std::shared_ptr<::cw::image::ImageInfo>> result;
    const auto* object = std::get_if<::cw::lib::ObjectPtr>(&paths.storage());
    if (!object || !*object) return result;
    const auto sequence = std::dynamic_pointer_cast<::cw::data::VariantSequence>(*object);
    if (!sequence) return result;
    for (const auto& item : sequence->m_values) {
        const auto* image = std::get_if<::cw::lib::ObjectPtr>(&item.storage());
        if (image && *image) {
            if (auto info = std::dynamic_pointer_cast<::cw::image::ImageInfo>(*image)) result.push_back(info);
        }
    }
    return result;
}

Value make_image_sequence(const std::vector<YadoDB::ImageRow>& images, const std::optional<String>& single)
{
    auto sequence = std::make_shared<::cw::data::VariantSequence>();
    if (single && !single->empty()) {
        auto info = std::make_shared<::cw::image::ImageInfo>();
        info->initialize(*single, 0, std::nullopt, "Default", std::nullopt);
        sequence->m_values.emplace_back(Value(std::static_pointer_cast<::cw::lib::Object>(info)));
    } else if (!single) {
        for (const auto& image : images) {
            auto info = std::make_shared<::cw::image::ImageInfo>();
            info->initialize(image.imgpath, 0, std::nullopt, image.postype.value_or("Default"), std::nullopt);
            sequence->m_values.emplace_back(Value(std::static_pointer_cast<::cw::lib::Object>(info)));
        }
    }
    return Value(std::static_pointer_cast<::cw::lib::Object>(sequence));
}

Value make_string_sequence(const std::vector<String>& values)
{
    auto sequence = std::make_shared<::cw::data::VariantSequence>();
    for (const auto& value : values) sequence->m_values.emplace_back(value);
    return Value(std::static_pointer_cast<::cw::lib::Object>(sequence));
}

void set_card_images(YadoDB::CardRow& row, const std::shared_ptr<::cw::header::CardHeader>& header)
{
    const auto images = header_images(header->m_imgpaths);
    row.images.clear(); row.imgpath.reset();
    if (images.size() == 1 && images.front() && images.front()->m_postype == "Default") row.imgpath = images.front()->m_path;
    else if (images.empty()) row.imgpath = String{};
    else {
        for (std::size_t i = 0; i < images.size(); ++i) {
            if (!images[i]) continue;
            YadoDB::ImageRow image; image.numorder = static_cast<Integer>(i); image.imgpath = images[i]->m_path;
            if (images[i]->m_postype != "Default") image.postype = images[i]->m_postype;
            row.images.push_back(std::move(image));
        }
    }
}

void set_adventurer_images(YadoDB::AdventurerRow& row, const std::shared_ptr<::cw::header::AdventurerHeader>& header)
{
    const auto images = header_images(header->m_imgpaths);
    row.images.clear(); row.imgpath.reset();
    if (images.size() == 1 && images.front() && images.front()->m_postype == "Default") row.imgpath = images.front()->m_path;
    else if (images.empty()) row.imgpath = String{};
    else {
        for (std::size_t i = 0; i < images.size(); ++i) {
            if (!images[i]) continue;
            YadoDB::ImageRow image; image.numorder = static_cast<Integer>(i); image.imgpath = images[i]->m_path;
            if (images[i]->m_postype != "Default") image.postype = images[i]->m_postype;
            row.images.push_back(std::move(image));
        }
    }
}

std::shared_ptr<::cw::header::CardHeader> card_header(const YadoDB::CardRow& row, const String& ypath, Integer order, const String& owner)
{
    auto header = std::make_shared<::cw::header::CardHeader>();
    header->initialize(std::nullopt, std::nullopt, std::nullopt, row.scenariocard, {}, false, std::nullopt, std::nullopt, owner, {});
    header->m_fpath = full_path(ypath, row.fpath); header->m_type = row.type; header->m_id = row.id;
    header->m_name = row.name; header->m_desc = row.desc; header->m_scenario = row.scenario; header->m_author = row.author;
    // XMLとは異なり、CardHeaderの公開dbrec読み込みと同じ生のデータベース形式。
    header->m_keycodes = ::cw::lib::python_split_newlines(row.keycodes);
    header->m_uselimit = row.uselimit; header->m_target = row.target; header->m_allrange = row.allrange; header->m_premium = row.premium;
    header->m_physical = row.physical; header->m_mental = row.mental; header->m_level = row.level; header->m_maxuselimit = row.maxuselimit;
    header->m_price = row.price; header->m_hold = row.hold; header->m_enhance_avo = row.enhance_avo; header->m_enhance_res = row.enhance_res;
    header->m_enhance_def = row.enhance_def; header->m_enhance_avo_used = row.enhance_avo_used; header->m_enhance_res_used = row.enhance_res_used;
    header->m_enhance_def_used = row.enhance_def_used; header->m_attachment = row.attachment; header->m_moved = row.moved;
    header->m_scenariocard = row.scenariocard; header->m_versionhint = row.versionhint; header->m_wsnversion = row.wsnversion;
    header->m_star = row.star; header->m_need_resetvariables = row.resetvariables; header->m_order = order;
    header->m_imgpaths = make_image_sequence(row.images, row.imgpath);
    header->finalize_loaded_fields();
    return header;
}

std::shared_ptr<::cw::header::AdventurerHeader> adventurer_header(const YadoDB::AdventurerRow& row, const String& ypath, Integer order)
{
    // 選択されたデータベース行を情報源とする。そのパスから、消失または後に編集さ
    // れた可能性のあるXMLを開き直さず、同じ公開のデータベース読み込みを使う。
    auto record = std::make_shared<::cw::header::DbRecord>();
    record->fields = {{"fpath", full_path(ypath, row.fpath)}, {"level", row.level},
        {"name", row.name}, {"desc", row.desc}, {"album", row.album}, {"lost", row.lost},
        {"sex", row.sex}, {"age", row.age}, {"ep", row.ep}, {"leavenoalbum", row.leavenoalbum},
        {"race", row.race}, {"versionhint", row.versionhint}, {"wsnversion", row.wsnversion},
        {"history", row.history}, {"gene", row.gene}, {"order", order}};
    auto header = std::make_shared<::cw::header::AdventurerHeader>();
    header->initialize(std::nullopt, row.album,
        Value(std::static_pointer_cast<::cw::lib::Object>(record)), std::nullopt, {}, std::nullopt);
    header->m_imgpaths = make_image_sequence(row.images, row.imgpath);
    return header;
}

std::shared_ptr<::cw::header::PartyHeader> party_header(const YadoDB::PartyRow& row, const String& ypath, Integer order)
{
    auto header = std::make_shared<::cw::header::PartyHeader>(); header->initialize(std::nullopt, std::nullopt);
    header->m_fpath = full_path(ypath, row.fpath); header->m_name = row.name; header->m_money = row.money; header->m_order = order;
    header->m_members.clear(); for (const auto& value : split_lines(row.members)) header->m_members.emplace_back(value); return header;
}

std::shared_ptr<::cw::header::PartyRecordHeader> partyrecord_header(const YadoDB::PartyRecordRow& row, const String& ypath)
{
    auto record = std::make_shared<::cw::header::DbRecord>();
    record->fields = {{"fpath", full_path(ypath, row.fpath)}, {"name", row.name},
        {"money", row.money}, {"members", row.members}, {"membernames", row.membernames},
        {"backpack", row.backpack}};
    auto header = std::make_shared<::cw::header::PartyRecordHeader>();
    header->initialize(std::nullopt, Value(std::static_pointer_cast<::cw::lib::Object>(record)), std::nullopt);
    header->m_backpack = make_string_sequence(header->m_backpack_names);
    return header;
}

std::shared_ptr<::cw::header::SavedJPDCImageHeader> saved_header(const YadoDB::SavedImageRow& row, const String& ypath)
{
    // 型付きSQLite行もPythonの行アダプタと同じ公開初期化処
    // 理へ渡す。空や末尾のパスフィールドも、別の規則を作らず保持する。
    auto record = std::make_shared<::cw::header::DbRecord>();
    record->fields = {{"fpath", full_path(ypath, row.fpath)}, {"scenarioname", row.scenarioname},
        {"scenarioauthor", row.scenarioauthor}, {"dpath", row.dpath}, {"fpaths", row.fpaths}};
    auto header = std::make_shared<::cw::header::SavedJPDCImageHeader>();
    header->initialize(std::nullopt, Value(std::static_pointer_cast<::cw::lib::Object>(record)));
    return header;
}

template<typename Map>
bool contains_path(const Map& table, const String& path)
{
    if (table.contains(path)) return true;
    const auto key = path_key(path);
    for (const auto& [existing, unused] : table) {
        (void)unused;
        if (path_key(existing) == key) return true;
    }
    return false;
}

template<typename Header>
std::shared_ptr<Header> find_update_header(const std::map<String, std::shared_ptr<Header>>& table,
                                           const String& key)
{
    // Pyは呼び出し元が指定した辞書キーをそのまま使う。新しい走査項目
    // は相対パス、古いカード・冒険者・パーティ項目は絶対パスを使う。ヘ
    // ッダ自身のfpathや大小文字を統一した別名は検索キーではない。
    const auto found = table.find(key);
    return found == table.end() ? std::shared_ptr<Header>{} : found->second;
}

template<typename T>
bool newer_than(const T& row, const String& ypath)
{
    return file_mtime(full_path(ypath, row.fpath)) > row.mtime;
}

bool adventurer_images_valid(const YadoDB::AdventurerRow& row, const String& ypath)
{
    const auto exists = [&ypath](const String& stored) {
        if (stored.empty()) return false;
        auto path = std::filesystem::u8path(stored);
        if (!path.is_absolute()) path = std::filesystem::u8path(ypath) / path;
        return path_is_file(path);
    };

    // NULLでない空のimgpathは、顔画像のないキャラクターの通
    // 常表現。imgpathがNULLで画像行がある場合は、重ね合わせ
    // や位置指定付きの顔画像を表し、すべて実ファイルを指す必要がある。
    if (row.imgpath) return row.imgpath->empty() || exists(*row.imgpath);
    if (row.images.empty()) return true;
    return std::all_of(row.images.begin(), row.images.end(),
                       [&exists](const YadoDB::ImageRow& image) {
                           return exists(image.imgpath);
                       });
}

template<typename Map>
void remove_missing(Map& table, const String& ypath)
{
    for (auto it = table.begin(); it != table.end();) {
        if (!path_is_file(std::filesystem::u8path(full_path(ypath, it->first)))) it = table.erase(it); else ++it;
    }
}

template<typename Fn>
void scan_xml_directory(const String& base, const String& rel, Fn&& fn)
{
    const auto dir = full_path(base, rel);
    const auto directory = std::filesystem::u8path(dir);
    if (!path_is_directory(directory)) return;
    for (const auto& entry : list_directory(directory)) {
        if (!path_is_file(entry)) continue;
        auto ext = path_utf8(entry.extension()); std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
        if (ext != ".xml") continue;
        fn(::cw::util::join_paths({Value(rel), Value(path_utf8(entry.filename()))}));
    }
}

using SqliteConnection = ::cw::lib::sqlite::Connection;
using SqliteBind = ::cw::lib::sqlite::BindValue;
using SqliteBinds = ::cw::lib::sqlite::BindParameters;

SqliteBind bind_text(const String& value) { return SqliteBind(value); }
SqliteBind bind_int(Integer value) { return SqliteBind(value); }
SqliteBind bind_real(::cw::lib::Real value) { return SqliteBind(value); }
SqliteBind bind_bool(bool value) { return SqliteBind(static_cast<Integer>(value ? 1 : 0)); }
SqliteBind bind_optional(const std::optional<String>& value)
{
    return value ? SqliteBind(*value) : SqliteBind(std::monostate{});
}
void execute_sql(SqliteConnection& db, const String& sql, const SqliteBinds& params = {})
{
    (void)db.execute(sql, params);
}

String column_text(const ::cw::lib::sqlite::Row& row, std::size_t index)
{
    const auto& value = row.at(index);
    if (value.is_null()) return {};
    if (value.type() == ::cw::lib::sqlite::ColumnType::Text) return value.as_text();
    if (value.type() == ::cw::lib::sqlite::ColumnType::Integer) return std::to_string(value.as_integer());
    // テキスト列のREALは不正な旧データ。二進浮動小数点経由で文
    // 字列化し、Decimalの小数桁数や数字を黙って失わない。
    return {};
}
std::optional<String> column_optional_text(const ::cw::lib::sqlite::Row& row, std::size_t index)
{
    const auto& value = row.at(index);
    if (value.is_null()) return std::nullopt;
    return value.as_text();
}
Integer column_int(const ::cw::lib::sqlite::Row& row, std::size_t index)
{
    const auto& value = row.at(index);
    if (value.is_null()) return {};
    if (value.type() == ::cw::lib::sqlite::ColumnType::Integer) return value.as_integer();
    if (value.type() == ::cw::lib::sqlite::ColumnType::Real) {
        const auto real = value.as_real();
        if (!std::isfinite(real)) return {};
        const auto truncated = std::trunc(real);
        const auto max_exclusive = static_cast<::cw::lib::Real>(std::numeric_limits<Integer>::max()) + 1.0;
        const auto min_inclusive = static_cast<::cw::lib::Real>(std::numeric_limits<Integer>::min());
        return (truncated >= max_exclusive || truncated < min_inclusive) ? Integer{} : static_cast<Integer>(truncated);
    }
    if (value.type() == ::cw::lib::sqlite::ColumnType::Text) {
        try { return ::cw::lib::Decimal::parse(value.as_text()).to_integer(::cw::lib::DecimalRounding::Down); }
        catch (const ::cw::lib::DecimalError&) { return {}; }
    }
    return {};
}
::cw::lib::Real column_real(const ::cw::lib::sqlite::Row& row, std::size_t index)
{
    const auto& value = row.at(index);
    if (value.is_null()) return 0.0;
    if (value.type() == ::cw::lib::sqlite::ColumnType::Real) {
        const auto result = value.as_real();
        return std::isfinite(result) ? result : 0.0;
    }
    if (value.type() == ::cw::lib::sqlite::ColumnType::Integer)
        return static_cast<::cw::lib::Real>(value.as_integer());
    return 0.0;
}
bool column_bool(const ::cw::lib::sqlite::Row& row, std::size_t index)
{
    return column_int(row, index) != 0;
}

void persist_card(SqliteConnection& db, const YadoDB::CardRow& row)
{
    execute_sql(db,
        "INSERT OR REPLACE INTO card(fpath,type,id,name,imgpath,desc,scenario,author,keycodes,uselimit,target,allrange,premium,physical,mental,level,maxuselimit,price,hold,enhance_avo,enhance_res,enhance_def,enhance_avo_used,enhance_res_used,enhance_def_used,attachment,moved,scenariocard,versionhint,wsnversion,star,resetvariables,ctime,mtime) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
        {bind_text(row.fpath), bind_text(row.type), bind_int(row.id), bind_text(row.name), bind_optional(row.imgpath),
         bind_text(row.desc), bind_text(row.scenario), bind_text(row.author), bind_text(row.keycodes), bind_int(row.uselimit),
         bind_text(row.target), bind_bool(row.allrange), bind_text(row.premium), bind_text(row.physical), bind_text(row.mental),
         bind_int(row.level), bind_int(row.maxuselimit), bind_int(row.price), bind_bool(row.hold), bind_int(row.enhance_avo),
         bind_int(row.enhance_res), bind_int(row.enhance_def), bind_int(row.enhance_avo_used), bind_int(row.enhance_res_used),
         bind_int(row.enhance_def_used), bind_bool(row.attachment), bind_int(row.moved), bind_bool(row.scenariocard),
         bind_text(row.versionhint), bind_text(row.wsnversion), bind_int(row.star), bind_bool(row.resetvariables),
         bind_real(row.ctime), bind_real(row.mtime)});
}

void persist_adventurer(SqliteConnection& db, const YadoDB::AdventurerRow& row)
{
    execute_sql(db,
        "INSERT OR REPLACE INTO adventurer(fpath,level,name,desc,imgpath,album,lost,sex,age,ep,leavenoalbum,gene,history,race,versionhint,wsnversion,ctime,mtime) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
        {bind_text(row.fpath), bind_int(row.level), bind_text(row.name), bind_text(row.desc), bind_optional(row.imgpath),
         bind_bool(row.album), bind_bool(row.lost), bind_text(row.sex), bind_text(row.age), bind_int(row.ep), bind_bool(row.leavenoalbum),
         bind_text(row.gene), bind_text(row.history), bind_text(row.race), bind_text(row.versionhint), bind_text(row.wsnversion),
         bind_real(row.ctime), bind_real(row.mtime)});
}

void persist_party(SqliteConnection& db, const YadoDB::PartyRow& row)
{
    execute_sql(db, "INSERT OR REPLACE INTO party(fpath,name,money,members,ctime,mtime) VALUES(?,?,?,?,?,?)",
                {bind_text(row.fpath), bind_text(row.name), bind_int(row.money), bind_text(row.members), bind_real(row.ctime), bind_real(row.mtime)});
}

void persist_partyrecord(SqliteConnection& db, const YadoDB::PartyRecordRow& row)
{
    execute_sql(db, "INSERT OR REPLACE INTO partyrecord(fpath,name,money,members,membernames,backpack,ctime,mtime) VALUES(?,?,?,?,?,?,?,?)",
                {bind_text(row.fpath), bind_text(row.name), bind_int(row.money), bind_text(row.members), bind_text(row.membernames),
                 bind_text(row.backpack), bind_real(row.ctime), bind_real(row.mtime)});
}

void persist_saved(SqliteConnection& db, const YadoDB::SavedImageRow& row)
{
    execute_sql(db, "INSERT OR REPLACE INTO savedjpdcimage(fpath,scenarioname,scenarioauthor,dpath,fpaths,ctime,mtime) VALUES(?,?,?,?,?,?,?)",
                {bind_text(row.fpath), bind_text(row.scenarioname), bind_text(row.scenarioauthor), bind_text(row.dpath), bind_text(row.fpaths),
                 bind_real(row.ctime), bind_real(row.mtime)});
}

void persist_images(SqliteConnection& db, const String& table, const String& fpath, const std::vector<YadoDB::ImageRow>& images)
{
    execute_sql(db, "DELETE FROM " + table + " WHERE fpath=?", {bind_text(fpath)});
    const auto sql = "INSERT OR REPLACE INTO " + table + "(fpath,numorder,imgpath,postype) VALUES(?,?,?,?)";
    for (const auto& image : images) {
        execute_sql(db, sql, {bind_text(fpath), bind_int(image.numorder), bind_text(image.imgpath), bind_optional(image.postype)});
    }
}

void persist_order(SqliteConnection& db, const String& table, const String& fpath, Integer order)
{
    execute_sql(db, "INSERT OR REPLACE INTO " + table + "(fpath,numorder) VALUES(?,?)", {bind_text(fpath), bind_int(order)});
}

void delete_row(SqliteConnection& db, const String& table, const String& fpath)
{
    execute_sql(db, "DELETE FROM " + table + " WHERE fpath=?", {bind_text(fpath)});
}

bool has_column(const std::vector<::cw::lib::sqlite::ColumnInfo>& columns, const String& name)
{
    return std::any_of(columns.begin(), columns.end(), [&](const auto& column) { return column.name == name; });
}

void create_table_if_missing(SqliteConnection& db, const String& table, const String& sql)
{
    if (db.table_columns(table).empty()) execute_sql(db, sql);
}

void migrate_connection(SqliteConnection& db, Integer mode)
{
    // Pythonのコンストラクタは意図的にPRAGMA table_info
    // を先に調べる。CREATE IF NOT EXISTSだけに頼らず同
    // じ順序にし、旧ファイルに下の列単位の移行と既定値の補充を適用する。
    const bool yado = mode == g_YADO;
    create_table_if_missing(db, "card", String(k_schema_sql[0]));
    create_table_if_missing(db, "cardimage", String(k_schema_sql[1]));
    auto cardimage = db.table_columns("cardimage");
    if (!has_column(cardimage, "postype")) execute_sql(db, "ALTER TABLE cardimage ADD COLUMN postype TEXT");
    create_table_if_missing(db, "cardorder", String(k_schema_sql[2]));

    auto card = db.table_columns("card");
    if (!has_column(card, "moved")) { execute_sql(db, "ALTER TABLE card ADD COLUMN moved INTEGER"); execute_sql(db, "UPDATE card SET moved=?", {bind_int(0)}); }
    if (!has_column(card, "scenariocard")) { execute_sql(db, "ALTER TABLE card ADD COLUMN scenariocard INTEGER"); execute_sql(db, "UPDATE card SET scenariocard=?", {bind_int(0)}); }
    if (!has_column(card, "versionhint")) { execute_sql(db, "ALTER TABLE card ADD COLUMN versionhint TEXT"); execute_sql(db, "UPDATE card SET versionhint=?", {bind_text("")}); }
    if (!has_column(card, "wsnversion")) execute_sql(db, "ALTER TABLE card ADD COLUMN wsnversion TEXT");
    if (!has_column(card, "star")) { execute_sql(db, "ALTER TABLE card ADD COLUMN star INTEGER"); execute_sql(db, "UPDATE card SET star=?", {bind_int(0)}); }
    if (!has_column(card, "resetvariables")) { execute_sql(db, "ALTER TABLE card ADD COLUMN resetvariables INTEGER"); execute_sql(db, "UPDATE card SET resetvariables=?", {bind_int(0)}); }

    if (!yado) return;
    create_table_if_missing(db, "adventurer", String(k_schema_sql[3]));
    create_table_if_missing(db, "adventurerimage", String(k_schema_sql[4]));
    auto adventurerimage = db.table_columns("adventurerimage");
    if (!has_column(adventurerimage, "postype")) execute_sql(db, "ALTER TABLE adventurerimage ADD COLUMN postype TEXT");
    create_table_if_missing(db, "adventurerorder", String(k_schema_sql[5]));
    auto adventurer = db.table_columns("adventurer");
    if (!has_column(adventurer, "desc")) { execute_sql(db, "ALTER TABLE adventurer ADD COLUMN desc TEXT"); execute_sql(db, "UPDATE adventurer SET mtime=?", {bind_int(0)}); }
    if (!has_column(adventurer, "versionhint")) { execute_sql(db, "ALTER TABLE adventurer ADD COLUMN versionhint TEXT"); execute_sql(db, "UPDATE adventurer SET versionhint=?", {bind_text("")}); }
    if (!has_column(adventurer, "wsnversion")) execute_sql(db, "ALTER TABLE adventurer ADD COLUMN wsnversion TEXT");
    create_table_if_missing(db, "party", String(k_schema_sql[6]));
    create_table_if_missing(db, "partyorder", String(k_schema_sql[7]));
    create_table_if_missing(db, "partyrecord", String(k_schema_sql[8]));
    auto partyrecord = db.table_columns("partyrecord");
    if (!has_column(partyrecord, "membernames")) { execute_sql(db, "ALTER TABLE partyrecord ADD COLUMN membernames TEXT"); execute_sql(db, "UPDATE partyrecord SET membernames=?", {bind_text("")}); }
    create_table_if_missing(db, "savedjpdcimage", String(k_schema_sql[9]));
}
}
YadoDB::~YadoDB()
{
    close();
}
void YadoDB::initialize(::cw::lib::String arg_ypath, ::cw::lib::Integer arg_mode)
{
    DbGuard guard;
    if (m_sqlite || m_open) close();
    m_ypath = std::move(arg_ypath);
    m_mode = arg_mode;
    m_name = full_path(m_ypath, m_mode == g_YADO ? "Yado.db" : "Card.db");
    // PythonはSQLiteを開く前にos.makedirsの失敗を伝える。マ
    // ウントされたデータでも同様に扱い、:memory:によって失敗を隠さない。
    if (!ensure_directory(std::filesystem::u8path(m_ypath)))
        throw std::runtime_error("database directory cannot be created: " + m_ypath);
    // SQLiteが使えない場合は旧メモリ内の複製へ切り替えず、sqlite::SqliteError(k_backend_unavailable)
    // を呼び出し元へ返す。
    const auto index_capabilities = ::cardwirth::platform::mounted_user_storage_capabilities(
        std::filesystem::u8path(m_ypath));
    const bool native_indexes = !index_capabilities ||
        index_capabilities->requires_native_secondary_indexes;
    const String sqlite_name = native_indexes ? m_name : String(":memory:");
    // XMLを正とする仮想・暗号化セッションは、平文SQLiteファイルを
    // 作らず、意図的にメモリ内の索引を保持する。ネイティブ索引を要求する提
    // 供側は、マウント時もPythonの永続データベース経路を使う。cardorder
    // は保存すべき状態で、カードXMLからは復元できない。Pythonは
    // ロック待機を30秒と記載する一方、sqlite3.connectへ
    // 30000秒を渡す。ネイティブSQLite APIはミリ秒単位な
    // ので、30000は8時間の待機ではなく記載どおりの30秒となる。
    m_sqlite = std::make_unique<SqliteConnection>(SqliteConnection::open(sqlite_name, 30000));
    m_transaction_open = false;
    try {
        m_sqlite->begin("DEFERRED");
        m_transaction_open = true;
        migrate_schema();
        m_sqlite->commit();
        m_transaction_open = false;
    } catch (...) {
        rollback_transaction();
        m_sqlite.reset();
        throw;
    }
    try {
        ::cw::lib::Database database;
        const auto connection = database.open(std::filesystem::u8path(sqlite_name));
        m_con = Value(std::static_pointer_cast<::cw::lib::Object>(connection));
        m_cur = Value(std::static_pointer_cast<::cw::lib::Object>(std::make_shared<::cw::lib::SqlCursor>()));
        m_open = m_sqlite && m_sqlite->is_open();
        g__lock = Value(std::static_pointer_cast<::cw::lib::Object>(std::make_shared<::cw::lib::Mutex>()));
        load_cache();
    } catch (...) {
        // キャッシュへの読み込みも初期化の一部であり、接続を開けただけで
        // は成功としない。不完全な接続を解放する際も元のエラーを保つ。
        close();
        throw;
    }
}

void YadoDB::require_open() const
{
    if (!m_sqlite || !m_sqlite->is_open())
        throw ::cw::lib::sqlite::SqliteError(::cw::lib::sqlite::k_closed_connection,
                                            ::cw::lib::sqlite::k_closed_connection,
                                            "YadoDB SQLite connection is not open", m_name);
}

void YadoDB::migrate_schema()
{
    if (!m_sqlite) throw ::cw::lib::sqlite::SqliteError(::cw::lib::sqlite::k_closed_connection,
                                                         ::cw::lib::sqlite::k_closed_connection,
                                                         "YadoDB SQLite connection is not open", m_name);
    migrate_connection(*m_sqlite, m_mode);
}

void YadoDB::load_cache()
{
    if (!m_sqlite) return;
    m_cards.clear(); m_cardorder.clear(); m_adventurers.clear(); m_adventurerorder.clear();
    m_parties.clear(); m_partyorder.clear(); m_partyrecords.clear(); m_savedjpdcimages.clear();
    const auto cards = m_sqlite->execute("SELECT fpath,type,id,name,imgpath,desc,scenario,author,keycodes,uselimit,target,allrange,premium,physical,mental,level,maxuselimit,price,hold,enhance_avo,enhance_res,enhance_def,enhance_avo_used,enhance_res_used,enhance_def_used,attachment,moved,scenariocard,versionhint,wsnversion,star,resetvariables,ctime,mtime FROM card");
    for (const auto& record : cards) {
        CardRow row; row.fpath = column_text(record, 0); row.type = column_text(record, 1); row.id = column_int(record, 2); row.name = column_text(record, 3);
        row.imgpath = column_optional_text(record, 4); row.desc = column_text(record, 5); row.scenario = column_text(record, 6); row.author = column_text(record, 7);
        row.keycodes = column_text(record, 8); row.uselimit = column_int(record, 9); row.target = column_text(record, 10); row.allrange = column_bool(record, 11);
        row.premium = column_text(record, 12); row.physical = column_text(record, 13); row.mental = column_text(record, 14); row.level = column_int(record, 15);
        row.maxuselimit = column_int(record, 16); row.price = column_int(record, 17); row.hold = column_bool(record, 18); row.enhance_avo = column_int(record, 19);
        row.enhance_res = column_int(record, 20); row.enhance_def = column_int(record, 21); row.enhance_avo_used = column_int(record, 22); row.enhance_res_used = column_int(record, 23);
        row.enhance_def_used = column_int(record, 24); row.attachment = column_bool(record, 25); row.moved = column_int(record, 26); row.scenariocard = column_bool(record, 27);
        row.versionhint = column_text(record, 28); row.wsnversion = column_text(record, 29); row.star = column_int(record, 30); row.resetvariables = column_bool(record, 31);
        row.ctime = column_real(record, 32); row.mtime = column_real(record, 33); m_cards[row.fpath] = std::move(row);
    }
    for (const auto& record : m_sqlite->execute("SELECT fpath,numorder FROM cardorder")) m_cardorder[column_text(record, 0)] = column_int(record, 1);
    for (const auto& record : m_sqlite->execute("SELECT fpath,numorder,imgpath,postype FROM cardimage ORDER BY fpath,numorder")) {
        auto it = m_cards.find(column_text(record, 0)); if (it == m_cards.end()) continue;
        ImageRow image; image.numorder = column_int(record, 1); image.imgpath = column_text(record, 2); image.postype = column_optional_text(record, 3); it->second.images.push_back(std::move(image));
    }
    if (m_mode != g_YADO) return;
    for (const auto& record : m_sqlite->execute("SELECT fpath,level,name,desc,imgpath,album,lost,sex,age,ep,leavenoalbum,gene,history,race,versionhint,wsnversion,ctime,mtime FROM adventurer")) {
        AdventurerRow row; row.fpath = column_text(record, 0); row.level = column_int(record, 1); row.name = column_text(record, 2); row.desc = column_text(record, 3);
        row.imgpath = column_optional_text(record, 4); row.album = column_bool(record, 5); row.lost = column_bool(record, 6); row.sex = column_text(record, 7); row.age = column_text(record, 8);
        row.ep = column_int(record, 9); row.leavenoalbum = column_bool(record, 10); row.gene = column_text(record, 11); row.history = column_text(record, 12); row.race = column_text(record, 13);
        row.versionhint = column_text(record, 14); row.wsnversion = column_text(record, 15); row.ctime = column_real(record, 16); row.mtime = column_real(record, 17); m_adventurers[row.fpath] = std::move(row);
    }
    for (const auto& record : m_sqlite->execute("SELECT fpath,numorder FROM adventurerorder")) m_adventurerorder[column_text(record, 0)] = column_int(record, 1);
    for (const auto& record : m_sqlite->execute("SELECT fpath,numorder,imgpath,postype FROM adventurerimage ORDER BY fpath,numorder")) {
        auto it = m_adventurers.find(column_text(record, 0)); if (it == m_adventurers.end()) continue;
        ImageRow image; image.numorder = column_int(record, 1); image.imgpath = column_text(record, 2); image.postype = column_optional_text(record, 3); it->second.images.push_back(std::move(image));
    }
    for (const auto& record : m_sqlite->execute("SELECT fpath,name,money,members,ctime,mtime FROM party")) {
        PartyRow row; row.fpath = column_text(record, 0); row.name = column_text(record, 1); row.money = column_int(record, 2); row.members = column_text(record, 3); row.ctime = column_real(record, 4); row.mtime = column_real(record, 5); m_parties[row.fpath] = std::move(row);
    }
    for (const auto& record : m_sqlite->execute("SELECT fpath,numorder FROM partyorder")) m_partyorder[column_text(record, 0)] = column_int(record, 1);
    for (const auto& record : m_sqlite->execute("SELECT fpath,name,money,members,membernames,backpack,ctime,mtime FROM partyrecord")) {
        PartyRecordRow row; row.fpath = column_text(record, 0); row.name = column_text(record, 1); row.money = column_int(record, 2); row.members = column_text(record, 3); row.membernames = column_text(record, 4); row.backpack = column_text(record, 5); row.ctime = column_real(record, 6); row.mtime = column_real(record, 7); m_partyrecords[row.fpath] = std::move(row);
    }
    for (const auto& record : m_sqlite->execute("SELECT fpath,scenarioname,scenarioauthor,dpath,fpaths,ctime,mtime FROM savedjpdcimage")) {
        SavedImageRow row; row.fpath = column_text(record, 0); row.scenarioname = column_text(record, 1); row.scenarioauthor = column_text(record, 2); row.dpath = column_text(record, 3); row.fpaths = column_text(record, 4); row.ctime = column_real(record, 5); row.mtime = column_real(record, 6); m_savedjpdcimages[row.fpath] = std::move(row);
    }
}

void YadoDB::begin_mutation()
{
    if (!m_sqlite) throw ::cw::lib::sqlite::SqliteError(::cw::lib::sqlite::k_closed_connection,
                                                         ::cw::lib::sqlite::k_closed_connection,
                                                         "YadoDB SQLite connection is not open", m_name);
    if (!m_transaction_open) { m_sqlite->begin("DEFERRED"); m_transaction_open = true; }
}

void YadoDB::finish_mutation(bool requested_commit)
{
    if (requested_commit) commit();
}

void YadoDB::rollback_transaction() noexcept
{
    if (!m_transaction_open || !m_sqlite) return;
    try { m_sqlite->rollback(); } catch (...) {}
    m_transaction_open = false;
}

void YadoDB::update(::cw::lib::Value arg_cards, ::cw::lib::Value arg_adventurers, ::cw::lib::Value arg_parties, std::optional<std::map<::cw::lib::String, ::cw::lib::Integer>> arg_cardorder, std::optional<std::map<::cw::lib::String, ::cw::lib::Integer>> arg_adventurerorder, std::optional<std::map<::cw::lib::String, ::cw::lib::Integer>> arg_partyorder, ::cw::lib::Value arg_partyrecord, ::cw::lib::Value arg_savedjpdcimage, std::map<::cw::lib::String, std::shared_ptr<::cw::header::CardHeader>> arg_cardheaders, std::map<::cw::lib::String, std::shared_ptr<::cw::header::AdventurerHeader>> arg_adventurerheaders, std::map<::cw::lib::String, std::shared_ptr<::cw::header::PartyHeader>> arg_partyheaders, std::map<::cw::lib::String, std::shared_ptr<::cw::header::PartyRecordHeader>> arg_partyrecordheaders, std::map<::cw::lib::String, std::shared_ptr<::cw::header::SavedJPDCImageHeader>> arg_savedheaders)
{
    DbGuard guard;
    {
    if (::cw::g_fsync) ::cw::g_fsync->sync();
    const auto cards = value_bool(arg_cards, true) || !arg_cardheaders.empty();
    const auto adventurers = value_bool(arg_adventurers, true) || !arg_adventurerheaders.empty();
    const auto parties = value_bool(arg_parties, true) || !arg_partyheaders.empty();
    const auto records = value_bool(arg_partyrecord, true) || !arg_partyrecordheaders.empty();
    const auto saved = value_bool(arg_savedjpdcimage, true) || !arg_savedheaders.empty();
    if (cards) {
        for (auto it = m_cards.begin(); it != m_cards.end();) {
            if (!path_is_file(std::filesystem::u8path(full_path(m_ypath, it->first)))) { const auto path = it->first; _delete_card(path, false); it = m_cards.begin(); }
            else ++it;
        }
        std::vector<String> stale;
        for (const auto& [path, row] : m_cards) if (newer_than(row, m_ypath)) stale.push_back(path);
        for (const auto& path : stale) {
            const auto absolute = full_path(m_ypath, path);
            if (auto header = find_update_header(arg_cardheaders, absolute)) _insert_cardheader(header, false, -1);
            else _insert_card(absolute, false, -1);
        }
        const auto consider_card = [&](const String& p) {
            if (contains_path(m_cards, p)) return;
            const auto absolute = full_path(m_ypath, p);
            if (auto header = find_update_header(arg_cardheaders, p)) _insert_cardheader(header, false, -1);
            else _insert_card(absolute, false, -1);
        };
        scan_xml_directory(m_ypath, "SkillCard", consider_card);
        scan_xml_directory(m_ypath, "ItemCard", consider_card);
        scan_xml_directory(m_ypath, "BeastCard", consider_card);
        if (arg_cardorder && !arg_cardorder->empty()) {
            begin_mutation(); execute_sql(*m_sqlite, "DELETE FROM cardorder"); m_cardorder.clear();
            for (const auto& [p, order] : *arg_cardorder) { m_cardorder[p] = order; persist_order(*m_sqlite, "cardorder", p, order); }
        }
    }
    if (m_mode == g_YADO && adventurers) {
        for (auto it = m_adventurers.begin(); it != m_adventurers.end();) {
            if (!path_is_file(std::filesystem::u8path(full_path(m_ypath, it->first)))) { const auto path = it->first; _delete_adventurer(path, false); it = m_adventurers.begin(); }
            else ++it;
        }
        std::vector<std::pair<String, bool>> stale;
        for (const auto& [path, row] : m_adventurers)
            if (newer_than(row, m_ypath) || row.name.empty() ||
                !adventurer_images_valid(row, m_ypath))
                stale.emplace_back(path, row.album);
        for (const auto& [path, album] : stale) {
            const auto absolute = full_path(m_ypath, path);
            if (auto header = find_update_header(arg_adventurerheaders, absolute)) _insert_adventurerheader(header, false, -1);
            else _insert_adventurer(absolute, album, false, -1);
        }
        const auto consider_adventurer = [&](const String& p, bool album) {
            if (contains_path(m_adventurers, p)) return;
            const auto absolute = full_path(m_ypath, p);
            // Pythonのwalk("Album", {}, ...)は常にアルバムのXMLを再解析する。
            if (!album) if (auto header = find_update_header(arg_adventurerheaders, p)) {
                _insert_adventurerheader(header, false, -1);
                return;
            }
            _insert_adventurer(absolute, album, false, -1);
        };
        scan_xml_directory(m_ypath, "Adventurer", [&](const String& p){ consider_adventurer(p, false); });
        scan_xml_directory(m_ypath, "Album", [&](const String& p){ consider_adventurer(p, true); });
        if (arg_adventurerorder && !arg_adventurerorder->empty()) {
            begin_mutation(); execute_sql(*m_sqlite, "DELETE FROM adventurerorder"); m_adventurerorder.clear();
            for (const auto& [p, order] : *arg_adventurerorder) { m_adventurerorder[p] = order; persist_order(*m_sqlite, "adventurerorder", p, order); }
        }
    }
    if (m_mode == g_YADO && parties) {
        for (auto it = m_parties.begin(); it != m_parties.end();) {
            if (!path_is_file(std::filesystem::u8path(full_path(m_ypath, it->first)))) { const auto path = it->first; _delete_party(path, false); it = m_parties.begin(); }
            else ++it;
        }
        std::vector<String> stale;
        for (const auto& [path, row] : m_parties) if (newer_than(row, m_ypath)) stale.push_back(path);
        for (const auto& path : stale) {
            const auto absolute = full_path(m_ypath, path);
            if (auto header = find_update_header(arg_partyheaders, absolute)) _insert_partyheader(header, false, -1);
            else _insert_party(absolute, false, -1);
        }
        const auto party_dir = full_path(m_ypath, "Party");
        if (path_is_directory(std::filesystem::u8path(party_dir))) for (const auto& entry : list_directory(std::filesystem::u8path(party_dir))) {
            if (path_is_directory(entry)) scan_xml_directory(m_ypath, ::cw::util::join_paths({Value("Party"), Value(path_utf8(entry.filename()))}), [&](const String& p){
                if (contains_path(m_parties, p)) return;
                const auto absolute = full_path(m_ypath, p);
                if (auto header = find_update_header(arg_partyheaders, p)) _insert_partyheader(header, false, -1);
                else _insert_party(absolute, false, -1);
            });
        }
        if (arg_partyorder && !arg_partyorder->empty()) {
            begin_mutation(); execute_sql(*m_sqlite, "DELETE FROM partyorder"); m_partyorder.clear();
            for (const auto& [p, order] : *arg_partyorder) { m_partyorder[p] = order; persist_order(*m_sqlite, "partyorder", p, order); }
        }
    }
    if (m_mode == g_YADO && records) {
        for (auto it = m_partyrecords.begin(); it != m_partyrecords.end();) {
            if (!path_is_file(std::filesystem::u8path(full_path(m_ypath, it->first)))) { const auto path = it->first; _delete_partyrecord(path, false); it = m_partyrecords.begin(); }
            else ++it;
        }
        std::vector<String> stale;
        for (const auto& [path, row] : m_partyrecords) if (newer_than(row, m_ypath)) stale.push_back(path);
        for (const auto& path : stale) {
            const auto absolute = full_path(m_ypath, path);
            if (auto header = find_update_header(arg_partyrecordheaders, path)) _insert_partyrecordheader(header, false);
            else _insert_partyrecord(absolute, false);
        }
        scan_xml_directory(m_ypath, "PartyRecord", [&](const String& p){
            if (contains_path(m_partyrecords, p)) return;
            const auto absolute = full_path(m_ypath, p);
            if (auto header = find_update_header(arg_partyrecordheaders, p)) _insert_partyrecordheader(header, false);
            else _insert_partyrecord(absolute, false);
        });
    }
    if (m_mode == g_YADO && saved) {
        for (auto it = m_savedjpdcimages.begin(); it != m_savedjpdcimages.end();) {
            if (!path_is_file(std::filesystem::u8path(full_path(m_ypath, it->first)))) { const auto path = it->first; _delete_savedjpdcimage(path, false); it = m_savedjpdcimages.begin(); }
            else ++it;
        }
        std::vector<String> stale;
        for (const auto& [path, row] : m_savedjpdcimages) if (newer_than(row, m_ypath)) stale.push_back(path);
        for (const auto& path : stale) {
            const auto absolute = full_path(m_ypath, path);
            if (auto header = find_update_header(arg_savedheaders, path)) _insert_savedjpdcimageheader(header, false);
            else _insert_savedjpdcimage(absolute, false);
        }
        const auto root = full_path(m_ypath, "SavedJPDCImage");
        if (path_is_directory(std::filesystem::u8path(root))) for (const auto& entry : list_directory(std::filesystem::u8path(root))) {
            if (!path_is_directory(entry)) continue;
            const auto rel = ::cw::util::join_paths({Value("SavedJPDCImage"), Value(path_utf8(entry.filename())), Value("SavedJPDCImage.xml")});
            const auto path = full_path(m_ypath, rel);
            if (path_is_file(std::filesystem::u8path(path)) && !contains_path(m_savedjpdcimages, rel)) {
                if (auto header = find_update_header(arg_savedheaders, rel)) _insert_savedjpdcimageheader(header, false);
                else _insert_savedjpdcimage(path, false);
            }
        }
    }
    commit();
    }}

void YadoDB::vacuum(bool arg_commit)
{
    DbGuard guard;
    if (!m_sqlite || !m_sqlite->is_open())
        throw ::cw::lib::sqlite::SqliteError(::cw::lib::sqlite::k_closed_connection,
                                            ::cw::lib::sqlite::k_closed_connection,
                                            "YadoDB SQLite connection is not open", m_name);
    // PythonのSQL文とエラー通知を保つ。特にVACUUM
    // を成功させるためだけに保留中の編集を確定しない。
    m_sqlite->execute("VACUUM card");
    if (m_mode == g_YADO) {
        for (const auto* table : {"adventurer", "party", "partyrecord", "savedjpdcimage"})
            m_sqlite->execute(String("VACUUM ") + table);
    }
    if (arg_commit) commit();
}

void YadoDB::_delete_card(::cw::lib::String arg_path, bool arg_commit)
{
    {
        begin_mutation();
        delete_row(*m_sqlite, "card", arg_path); delete_row(*m_sqlite, "cardimage", arg_path); delete_row(*m_sqlite, "cardorder", arg_path);
        m_cards.erase(arg_path); m_cardorder.erase(arg_path);
        finish_mutation(arg_commit);
    }}

void YadoDB::_delete_adventurer(::cw::lib::String arg_path, bool arg_commit)
{
    {
        begin_mutation();
        delete_row(*m_sqlite, "adventurer", arg_path); delete_row(*m_sqlite, "adventurerimage", arg_path); delete_row(*m_sqlite, "adventurerorder", arg_path);
        m_adventurers.erase(arg_path); m_adventurerorder.erase(arg_path); finish_mutation(arg_commit);
    }}

void YadoDB::_delete_party(::cw::lib::String arg_path, bool arg_commit)
{
    {
        begin_mutation(); delete_row(*m_sqlite, "party", arg_path); delete_row(*m_sqlite, "partyorder", arg_path);
        m_parties.erase(arg_path); m_partyorder.erase(arg_path); finish_mutation(arg_commit);
    }}

void YadoDB::_delete_partyrecord(::cw::lib::String arg_path, bool arg_commit)
{
    { begin_mutation(); delete_row(*m_sqlite, "partyrecord", arg_path); m_partyrecords.erase(arg_path); finish_mutation(arg_commit); }}

void YadoDB::_delete_savedjpdcimage(::cw::lib::String arg_path, bool arg_commit)
{
    { begin_mutation(); delete_row(*m_sqlite, "savedjpdcimage", arg_path); m_savedjpdcimages.erase(arg_path); finish_mutation(arg_commit); }}

void YadoDB::delete_savedjpdcimage(::cw::lib::String arg_path, bool arg_commit)
{
    DbGuard guard;
    _delete_savedjpdcimage(std::move(arg_path), arg_commit);
}

void YadoDB::insert_cardheader(std::shared_ptr<::cw::header::CardHeader> arg_header, bool arg_commit, ::cw::lib::Integer arg_cardorder)
{
    DbGuard guard;
    _insert_cardheader(std::move(arg_header), arg_commit, arg_cardorder);
}

void YadoDB::_insert_cardheader(std::shared_ptr<::cw::header::CardHeader> arg_header, bool arg_commit, ::cw::lib::Integer arg_cardorder)
{
    if (!arg_header) return;
    const auto absolute = value_string(arg_header->m_fpath); if (absolute.empty()) return;
    CardRow row; row.fpath = relative_path(absolute, m_ypath); row.type = arg_header->m_type; row.id = arg_header->m_id; row.name = arg_header->m_name;
    row.desc = arg_header->m_desc; row.scenario = arg_header->m_scenario; row.author = arg_header->m_author; row.uselimit = arg_header->m_uselimit;
    row.target = arg_header->m_target; row.allrange = arg_header->m_allrange; row.premium = arg_header->m_premium; row.physical = value_string(arg_header->m_physical);
    row.mental = value_string(arg_header->m_mental); row.level = arg_header->m_level; row.maxuselimit = arg_header->m_maxuselimit; row.price = arg_header->m_price;
    row.hold = arg_header->m_hold; row.enhance_avo = arg_header->m_enhance_avo; row.enhance_res = arg_header->m_enhance_res; row.enhance_def = arg_header->m_enhance_def;
    row.enhance_avo_used = arg_header->m_enhance_avo_used; row.enhance_res_used = arg_header->m_enhance_res_used; row.enhance_def_used = arg_header->m_enhance_def_used;
    row.attachment = arg_header->m_attachment; row.moved = arg_header->m_moved; row.scenariocard = arg_header->m_scenariocard; row.versionhint = value_string(arg_header->m_versionhint);
    row.wsnversion = value_string(arg_header->m_wsnversion); row.star = arg_header->m_star; row.resetvariables = arg_header->m_need_resetvariables;
    // CardHeaderは明示的なキーコードがない場合も、最後のキーコ
    // ードとして名前を追加する。保存するのは元の明示的なキーコードだけ。
    const auto keycodes = arg_header->m_keycodes.empty() ? std::vector<String>{} : std::vector<String>(arg_header->m_keycodes.begin(), arg_header->m_keycodes.end() - 1);
    row.keycodes = join_lines(keycodes); row.ctime = now_seconds(); row.mtime = file_mtime(absolute); set_card_images(row, arg_header);
    {
        begin_mutation();
        persist_card(*m_sqlite, row);
        if (arg_cardorder > -1) persist_order(*m_sqlite, "cardorder", row.fpath, arg_cardorder);
        if (!row.images.empty()) persist_images(*m_sqlite, "cardimage", row.fpath, row.images);
        m_cards[row.fpath] = row;
        if (arg_cardorder > -1) m_cardorder[row.fpath] = arg_cardorder;
        finish_mutation(arg_commit);
    }}

void YadoDB::insert_card(::cw::lib::String arg_path, bool arg_commit, ::cw::lib::Integer arg_cardorder)
{
    DbGuard guard;
    _insert_card(std::move(arg_path), arg_commit, arg_cardorder);
}

void YadoDB::_insert_card(::cw::lib::String arg_path, bool arg_commit, ::cw::lib::Integer arg_cardorder)
{
    try {
        // 元の不具合に対する意図的な補正。バイナリ変換はこの行の挿入直前
        // にXMLをFileSyncへ積む。Pythonのシナリオ外のxml2element
        // は書き込みと競合し、通知なくカードを索引から失う場合
        // がある。共通のディスク読み込みの境界で完了を待つ。
        if (::cw::g_fsync && ::cw::g_fsync->is_waiting(arg_path)) ::cw::g_fsync->sync();
        const auto data = ::cw::data::xml2element(arg_path, "", std::nullopt, std::nullopt, false, std::nullopt);
        if (!data) return;
        auto header = std::make_shared<::cw::header::CardHeader>();
        header->initialize(std::nullopt, std::nullopt, data, false, {}, false, std::nullopt, std::nullopt, {}, {}); header->m_fpath = arg_path;
        _insert_cardheader(std::move(header), arg_commit, arg_cardorder);
    } catch (...) {
        // Pythonは解析・入出力・データベースのエラーを捕捉し
        // 、処理中の例外を報告して残りのファイルの走査を続ける。
        ::cw::util::print_ex(Value(arg_path));
    }
}

std::vector<const YadoDB::CardRow*> YadoDB::ordered_card_rows() const
{
    std::vector<const CardRow*> rows; rows.reserve(m_cards.size()); for (const auto& [path, row] : m_cards) rows.push_back(&row);
    std::stable_sort(rows.begin(), rows.end(), [&](const CardRow* a, const CardRow* b) {
        const auto ia = m_cardorder.find(a->fpath), ib = m_cardorder.find(b->fpath);
        if ((ia == m_cardorder.end()) != (ib == m_cardorder.end())) return ia == m_cardorder.end();
        if (ia != m_cardorder.end() && ia->second != ib->second) return ia->second < ib->second;
        return a->name < b->name;
    });
    return rows;
}

std::vector<std::shared_ptr<::cw::header::CardHeader>> YadoDB::get_cards()
{
    DbGuard guard;
    require_open();
    const auto rows = ordered_card_rows();
    std::vector<std::shared_ptr<::cw::header::CardHeader>> result; std::set<String> seen; Integer order = 0;
    const auto owner = m_mode == g_YADO ? "STOREHOUSE" : "BACKPACK";
    // Pyはnormcaseで重複排除する前にデータベース行を列挙する。飛ばし
    // た別名も行番号を消費するため、header.orderには欠番ができる。
    for (const auto* row : rows) {
        const auto row_order = order++;
        if (!seen.insert(path_key(row->fpath)).second) continue;
        result.push_back(card_header(*row, m_ypath, row_order, owner));
    }
    return result;
}

std::vector<::cw::lib::String> YadoDB::get_cardfpaths(bool arg_scenariocard)
{
    DbGuard guard;
    require_open();
    std::vector<String> result;
    std::set<String> seen;
    // Pythonはnormcaseによる重複排除より前にSQL行を絞り込む。通常
    // カードの大小文字違いの別名によって、ここで要求したシナリオの行を除外しない。
    for (const auto* row : ordered_card_rows()) {
        if (row->scenariocard != arg_scenariocard) continue;
        if (seen.insert(path_key(row->fpath)).second) result.push_back(row->fpath);
    }
    return result;
}

void YadoDB::insert_adventurerheader(std::shared_ptr<::cw::header::AdventurerHeader> arg_header, bool arg_commit, ::cw::lib::Integer arg_adventurerorder)
{
    DbGuard guard;
    _insert_adventurerheader(std::move(arg_header), arg_commit, arg_adventurerorder);
}

void YadoDB::_insert_adventurerheader(std::shared_ptr<::cw::header::AdventurerHeader> arg_header, bool arg_commit, ::cw::lib::Integer arg_adventurerorder)
{
    if (!arg_header) return;
    const auto absolute = value_string(arg_header->m_fpath); if (absolute.empty()) return;
    AdventurerRow row; row.fpath = relative_path(absolute, m_ypath); row.level = value_integer(arg_header->m_level); row.name = arg_header->m_name;
    row.desc = value_string(arg_header->m_desc); row.album = arg_header->m_album; row.lost = arg_header->m_lost; row.sex = value_string(arg_header->m_sex);
    row.age = value_string(arg_header->m_age); row.ep = arg_header->m_ep; row.leavenoalbum = arg_header->m_leavenoalbum; row.race = arg_header->m_race;
    row.versionhint = value_string(arg_header->m_versionhint); row.wsnversion = arg_header->m_wsnversion;
    std::vector<String> history; for (const auto& value : arg_header->m_history) history.push_back(value_string(value));
    row.history = join_lines(history);
    row.gene = arg_header->m_gene ? arg_header->m_gene->get_str() : String{}; row.ctime = now_seconds(); row.mtime = file_mtime(absolute); set_adventurer_images(row, arg_header);
    {
        begin_mutation();
        persist_adventurer(*m_sqlite, row);
        if (arg_adventurerorder > -1) persist_order(*m_sqlite, "adventurerorder", row.fpath, arg_adventurerorder);
        if (!row.images.empty()) persist_images(*m_sqlite, "adventurerimage", row.fpath, row.images);
        m_adventurers[row.fpath] = row; if (arg_adventurerorder > -1) m_adventurerorder[row.fpath] = arg_adventurerorder; finish_mutation(arg_commit);
    }}

void YadoDB::insert_adventurer(::cw::lib::String arg_path, bool arg_album, bool arg_commit, ::cw::lib::Integer arg_adventurerorder)
{
    DbGuard guard;
    _insert_adventurer(std::move(arg_path), arg_album, arg_commit, arg_adventurerorder);
}

void YadoDB::_insert_adventurer(::cw::lib::String arg_path, bool arg_album, bool arg_commit, ::cw::lib::Integer arg_adventurerorder)
{
    try {
        // Pythonのファイルのみのヘッダ経路へ入る際も、既存の
        // 書き込み完了待ちを保つ。上の_insert_cardと同
        // 様に、元の非同期XML・DB競合に対する意図的な補正。
        if (::cw::g_fsync && ::cw::g_fsync->is_waiting(arg_path)) ::cw::g_fsync->sync();
        auto header = std::make_shared<::cw::header::AdventurerHeader>();
        header->initialize(std::nullopt, arg_album, std::nullopt, std::nullopt, arg_path, std::nullopt);
        _insert_adventurerheader(std::move(header), arg_commit, arg_adventurerorder);
    } catch (...) {
        ::cw::util::print_ex(Value(arg_path));
    }
}

std::vector<std::shared_ptr<::cw::header::AdventurerHeader>> YadoDB::get_adventurers(bool arg_album)
{
    DbGuard guard;
    require_open();
    std::vector<const AdventurerRow*> rows; for (const auto& [path, row] : m_adventurers) if (row.album == arg_album && (arg_album || !row.lost)) rows.push_back(&row);
    std::sort(rows.begin(), rows.end(), [&](const AdventurerRow* a, const AdventurerRow* b) {
        if (!arg_album) { const auto ia = m_adventurerorder.find(a->fpath), ib = m_adventurerorder.find(b->fpath); if ((ia == m_adventurerorder.end()) != (ib == m_adventurerorder.end())) return ia == m_adventurerorder.end(); if (ia != m_adventurerorder.end() && ia->second != ib->second) return ia->second < ib->second; }
        return a->name < b->name;
    });
    std::vector<std::shared_ptr<::cw::header::AdventurerHeader>> result; std::set<String> seen; Integer order = 0;
    // Pyのenumerate(recs)と同様に、重複排除前の行番号を保つ。
    for (const auto* row : rows) {
        const auto row_order = order++;
        if (!seen.insert(path_key(row->fpath)).second) continue;
        result.push_back(adventurer_header(*row, m_ypath, row_order));
    }
    return result;
}

std::vector<std::shared_ptr<::cw::header::AdventurerHeader>> YadoDB::get_standbys()
{
    return get_adventurers(false);
}

std::vector<::cw::lib::String> YadoDB::get_standbynames()
{
    DbGuard guard;
    require_open();
    std::vector<const AdventurerRow*> rows; for (const auto& [path, row] : m_adventurers) if (!row.lost && !row.album) rows.push_back(&row);
    std::sort(rows.begin(), rows.end(), [](const AdventurerRow* a, const AdventurerRow* b){ return a->name < b->name; }); std::vector<String> result; std::set<String> seen;
    for (const auto* row : rows) { if (seen.insert(path_key(row->fpath)).second) result.push_back(row->name); }
    return result;
}

std::vector<std::shared_ptr<::cw::header::AdventurerHeader>> YadoDB::get_album()
{
    return get_adventurers(true);
}

void YadoDB::insert_partyheader(std::shared_ptr<::cw::header::PartyHeader> arg_header, bool arg_commit, ::cw::lib::Integer arg_partyorder)
{
    DbGuard guard;
    _insert_partyheader(std::move(arg_header), arg_commit, arg_partyorder);
}

void YadoDB::_insert_partyheader(std::shared_ptr<::cw::header::PartyHeader> arg_header, bool arg_commit, ::cw::lib::Integer arg_partyorder)
{
    if (!arg_header) return;
    const auto absolute = value_string(arg_header->m_fpath); if (absolute.empty()) return;
    PartyRow row; row.fpath = relative_path(absolute, m_ypath); row.name = arg_header->m_name; row.money = arg_header->m_money; row.ctime = now_seconds(); row.mtime = file_mtime(absolute);
    std::vector<String> members; for (const auto& value : arg_header->m_members) members.push_back(value_string(value)); row.members = join_lines(members);
    {
        begin_mutation(); persist_party(*m_sqlite, row); if (arg_partyorder > -1) persist_order(*m_sqlite, "partyorder", row.fpath, arg_partyorder);
        m_parties[row.fpath] = row; if (arg_partyorder > -1) m_partyorder[row.fpath] = arg_partyorder; finish_mutation(arg_commit);
    }}

void YadoDB::insert_party(::cw::lib::String arg_path, bool arg_commit, ::cw::lib::Integer arg_partyorder)
{
    DbGuard guard;
    _insert_party(std::move(arg_path), arg_commit, arg_partyorder);
}

void YadoDB::_insert_party(::cw::lib::String arg_path, bool arg_commit, ::cw::lib::Integer arg_partyorder)
{
    try {
        // カード・冒険者の挿入と同じく、元の不具合に対する意図的な補正。変換処
        // 理はこのファイル読み込みの前にParty XMLをキューへ積む。Py
        // はシナリオ外のXMLで待たず、古いファイルを索引化したり行を見落とし
        // たりする場合がある。非同期の競合を残さず、ここで書き込み完了を待つ。
        if (::cw::g_fsync && ::cw::g_fsync->is_waiting(arg_path)) ::cw::g_fsync->sync();
        const auto tree = ::cw::data::xml2etree(arg_path, "", std::nullopt, std::nullopt, false); if (!tree) return;
        const auto property = tree->find("Property"); if (!property || !*property) return;
        auto header = std::make_shared<::cw::header::PartyHeader>(); header->initialize(*property, std::nullopt); header->m_fpath = arg_path;
        _insert_partyheader(std::move(header), arg_commit, arg_partyorder);
    } catch (...) {
        ::cw::util::print_ex(Value(arg_path));
    }
}

std::vector<std::shared_ptr<::cw::header::PartyHeader>> YadoDB::get_parties()
{
    DbGuard guard;
    require_open();
    std::vector<const PartyRow*> rows; for (const auto& [path, row] : m_parties) rows.push_back(&row);
    std::sort(rows.begin(), rows.end(), [&](const PartyRow* a, const PartyRow* b){ const auto ia = m_partyorder.find(a->fpath), ib = m_partyorder.find(b->fpath); if ((ia == m_partyorder.end()) != (ib == m_partyorder.end())) return ia == m_partyorder.end(); if (ia != m_partyorder.end() && ia->second != ib->second) return ia->second < ib->second; return a->name < b->name; });
    std::vector<std::shared_ptr<::cw::header::PartyHeader>> result; std::set<String> seen; Integer order = 0;
    // Pyのenumerate(recs)と同様に、重複排除前の行番号を保つ。
    for (const auto* row : rows) {
        const auto row_order = order++;
        if (!seen.insert(path_key(row->fpath)).second) continue;
        result.push_back(party_header(*row, m_ypath, row_order));
    }
    return result;
}

void YadoDB::insert_partyrecordheader(std::shared_ptr<::cw::header::PartyRecordHeader> arg_header, bool arg_commit)
{
    DbGuard guard;
    _insert_partyrecordheader(std::move(arg_header), arg_commit);
}

void YadoDB::_insert_partyrecordheader(std::shared_ptr<::cw::header::PartyRecordHeader> arg_header, bool arg_commit)
{
    if (!arg_header) return;
    const auto absolute = value_string(arg_header->m_fpath); if (absolute.empty()) return;
    PartyRecordRow row; row.fpath = relative_path(absolute, m_ypath); row.name = arg_header->m_name; row.money = arg_header->m_money; row.ctime = now_seconds(); row.mtime = file_mtime(absolute);
    std::vector<String> members, names; for (const auto& value : arg_header->m_members) members.push_back(value_string(value)); for (const auto& value : arg_header->m_membernames) names.push_back(value_string(value));
    row.members = join_lines(members); row.membernames = join_lines(names); row.backpack = join_lines(arg_header->m_backpack_names);
    { begin_mutation(); persist_partyrecord(*m_sqlite, row); m_partyrecords[row.fpath] = row; finish_mutation(arg_commit); }}

void YadoDB::insert_partyrecord(::cw::lib::String arg_path, bool arg_commit)
{
    DbGuard guard;
    _insert_partyrecord(std::move(arg_path), arg_commit);
}

void YadoDB::_insert_partyrecord(::cw::lib::String arg_path, bool arg_commit)
{
    try { auto header = std::make_shared<::cw::header::PartyRecordHeader>(); header->initialize(arg_path, std::nullopt, std::nullopt); _insert_partyrecordheader(std::move(header), arg_commit); }
    catch (...) {
        ::cw::util::print_ex(Value(arg_path));
    }
}

std::vector<std::shared_ptr<::cw::header::PartyRecordHeader>> YadoDB::get_partyrecord()
{
    DbGuard guard;
    require_open();
    std::vector<const PartyRecordRow*> rows; for (const auto& [path, row] : m_partyrecords) rows.push_back(&row); std::sort(rows.begin(), rows.end(), [](const PartyRecordRow* a, const PartyRecordRow* b){ return a->name < b->name; });
    std::vector<std::shared_ptr<::cw::header::PartyRecordHeader>> result; std::set<String> seen; for (const auto* row : rows) { if (seen.insert(path_key(row->fpath)).second) result.push_back(partyrecord_header(*row, m_ypath)); } return result;
}

void YadoDB::insert_savedjpdcimageheader(std::shared_ptr<::cw::header::SavedJPDCImageHeader> arg_header, bool arg_commit)
{
    DbGuard guard;
    _insert_savedjpdcimageheader(std::move(arg_header), arg_commit);
}

void YadoDB::_insert_savedjpdcimageheader(std::shared_ptr<::cw::header::SavedJPDCImageHeader> arg_header, bool arg_commit)
{
    if (!arg_header) return;
    const auto absolute = value_string(arg_header->m_fpath); if (absolute.empty()) return;
    SavedImageRow row; row.fpath = relative_path(absolute, m_ypath); row.scenarioname = arg_header->m_scenarioname; row.scenarioauthor = arg_header->m_scenarioauthor; row.dpath = value_string(arg_header->m_dpath); row.ctime = now_seconds(); row.mtime = file_mtime(absolute);
    std::vector<String> paths; for (const auto& value : arg_header->m_fpaths) paths.push_back(value_string(value)); if (paths.empty()) paths = arg_header->m_fpaths_typed; row.fpaths = join_lines(paths);
    { begin_mutation(); persist_saved(*m_sqlite, row); m_savedjpdcimages[row.fpath] = row; finish_mutation(arg_commit); }}

void YadoDB::insert_savedjpdcimage(::cw::lib::String arg_path, bool arg_commit)
{
    DbGuard guard;
    _insert_savedjpdcimage(std::move(arg_path), arg_commit);
}

void YadoDB::_insert_savedjpdcimage(::cw::lib::String arg_path, bool arg_commit)
{
    try { auto header = std::make_shared<::cw::header::SavedJPDCImageHeader>(); header->initialize(arg_path, std::nullopt); _insert_savedjpdcimageheader(std::move(header), arg_commit); }
    catch (...) {
        ::cw::util::print_ex(Value(arg_path));
    }
}

std::map<std::tuple<::cw::lib::String, ::cw::lib::String>, std::shared_ptr<::cw::header::SavedJPDCImageHeader>> YadoDB::get_savedjpdcimage()
{
    DbGuard guard;
    require_open();
    std::map<std::tuple<String, String>, std::shared_ptr<::cw::header::SavedJPDCImageHeader>> result;
    std::set<String> seen;
    // Pyの実際のSELECTの走査順を保つ。パスをソートしたキャッシュでは
    // 、同じシナリオ・作者の論理キーを持つ複数パスで、選ばれる行が変わる。
    for (const auto& record : m_sqlite->execute("SELECT * FROM savedjpdcimage")) {
        const auto text = [&](const char* name) {
            const auto& names = record.column_names();
            const auto found = std::find(names.begin(), names.end(), name);
            if (found == names.end()) throw std::out_of_range("savedjpdcimage column is missing");
            return column_text(record, static_cast<std::size_t>(std::distance(names.begin(), found)));
        };
        SavedImageRow row;
        row.fpath = text("fpath");
        if (!seen.insert(path_key(row.fpath)).second) continue;
        row.scenarioname = text("scenarioname");
        row.scenarioauthor = text("scenarioauthor");
        row.dpath = text("dpath");
        row.fpaths = text("fpaths");
        auto header = saved_header(row, m_ypath);
        result[{header->m_scenarioname, header->m_scenarioauthor}] = std::move(header);
    }
    return result;
}

void YadoDB::commit()
{
    DbGuard guard;
    // Pythonのsqlite3.Connection.commit
    // は、保留中のトランザクションがなくても閉じた接続を拒否する。
    if (!m_sqlite || !m_sqlite->is_open())
        throw ::cw::lib::sqlite::SqliteError(::cw::lib::sqlite::k_closed_connection,
                                            ::cw::lib::sqlite::k_closed_connection,
                                            "YadoDB SQLite connection is not open", m_name);
    if (!m_transaction_open) return;
    m_sqlite->commit();
    m_transaction_open = false;
}

void YadoDB::close()
{
    DbGuard guard;
    rollback_transaction();
    if (m_sqlite) { { m_sqlite->close(); }}
    m_sqlite.reset();
    if (const auto* object = std::get_if<::cw::lib::ObjectPtr>(&m_con.storage()); object && *object) if (auto connection = std::dynamic_pointer_cast<::cw::lib::SqlConnection>(*object)) connection->open = false;
    m_open = false; m_cur = Value{};
}

::cw::lib::Integer g_PARTY{1};
::cw::lib::Integer g_YADO{0};
::cw::lib::Value g__lock{};

namespace {
void update_walk_impl(const ::cw::lib::String& arg_dpath,
                      const ::cw::lib::ValueMap* arg_headertable,
                      const ::cw::lib::String& arg_xmlname,
                      const ::cw::lib::Callback& arg_insert,
                      const ::cw::lib::Callback& arg_insertheader,
                      const ::cw::lib::Arguments& arg_args)
{
    const auto directory = std::filesystem::u8path(arg_dpath);
    if (!path_is_directory(directory)) return;
    for (const auto& entry : list_directory(directory)) {
        String path;
        if (arg_xmlname.empty()) { if (!path_is_file(entry)) continue; auto ext = path_utf8(entry.extension()); std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c){ return static_cast<char>(std::tolower(c)); }); if (ext != ".xml") continue; path = path_utf8(entry); }
        else { if (!path_is_directory(entry)) continue; const auto candidate = entry / std::filesystem::u8path(arg_xmlname); if (!path_is_file(candidate)) continue; path = path_utf8(candidate); }
        const auto generic_path = path_utf8(std::filesystem::u8path(path));
        const auto relative_key = arg_xmlname.empty()
            ? path_utf8(directory.filename() / entry.filename())
            : path_utf8(directory.filename() / entry.filename() / std::filesystem::u8path(arg_xmlname));
        const ::cw::lib::Value* header = nullptr;
        if (arg_headertable) {
            for (const auto& key : {path, generic_path, relative_key}) {
                if (const auto found = arg_headertable->find(key); found != arg_headertable->end()) {
                    header = &found->second;
                    break;
                }
            }
        }
        ::cw::lib::Arguments args;
        if (header && arg_insertheader) args.push_back(*header);
        else args.emplace_back(path);
        args.insert(args.end(), arg_args.begin(), arg_args.end());
        if (header && arg_insertheader) arg_insertheader(args, {});
        else if (arg_insert) arg_insert(args, {});
    }
}
} // namespace

void YadoDB_update_walk(::cw::lib::String arg_dpath, ::cw::lib::Value arg_headertable, ::cw::lib::String arg_xmlname, ::cw::lib::Callback arg_insert, ::cw::lib::Callback arg_insertheader, const ::cw::lib::Arguments& arg_args)
{
    // 単一のbool/Noneは、Pythonのキャッシュ済みヘッダを使わ
    // ない指定。compat::Valueは意図的に再帰的なコンテナを格納
    // しないため、辞書を渡す呼び出し元は下の型付きオーバーロードを使う。
    (void)arg_headertable;
    update_walk_impl(arg_dpath, nullptr, arg_xmlname, arg_insert, arg_insertheader, arg_args);
}

void YadoDB_update_walk(::cw::lib::String arg_dpath, const ::cw::lib::ValueMap& arg_headertable,
                        ::cw::lib::String arg_xmlname, ::cw::lib::Callback arg_insert,
                        ::cw::lib::Callback arg_insertheader, const ::cw::lib::Arguments& arg_args)
{
    update_walk_impl(arg_dpath, &arg_headertable, arg_xmlname, arg_insert, arg_insertheader, arg_args);
}

}

