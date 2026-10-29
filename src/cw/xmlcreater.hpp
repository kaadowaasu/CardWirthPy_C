#pragma once

// cw/xmlcreater.py のXML生成処理を公開する。
#include "cw/lib/compat.hpp"
#include "cw/fwd.hpp"

namespace cw::xmlcreater {
void _create_xml(::cw::lib::String arg_name, ::cw::lib::String arg_path, std::map<::cw::lib::String, ::cw::lib::String> arg_d);
::cw::lib::String create_party(std::vector<std::shared_ptr<::cw::header::AdventurerHeader>> arg_headers, ::cw::lib::Integer arg_moneyamount = 0, std::optional<::cw::lib::String> arg_pname = std::nullopt, bool arg_is_suspendlevelup = false);
::cw::lib::String create_partyrecord(std::shared_ptr<::cw::thread::StoredParty> arg_party);
::cw::lib::String create_partyrecord(std::shared_ptr<::cw::thread::StoredParty> arg_party,
    const ::cw::lib::String& yadodir, const ::cw::lib::String& tempdir);
::cw::lib::String create_environment(::cw::lib::String arg_name, ::cw::lib::String arg_dpath, ::cw::lib::String arg_skindirname, bool arg_is_autoloadparty, std::vector<std::shared_ptr<::cw::image::ImageInfo>> arg_imgpaths);
void create_skinvariables(::cw::lib::String arg_path);
void copy_yadoimgpaths(::cw::lib::String arg_yadodir, std::vector<std::shared_ptr<::cw::image::ImageInfo>> arg_imgpaths);
::cw::lib::String create_settings(std::shared_ptr<::cw::setting::Setting> arg_setting, bool arg_writeplayingdata = true, ::cw::lib::String arg_fpath = "Settings.xml");
// create_settingsと同じXMLモデル・シリアライザーを使うが、ストレージの実装は選ばない。
::cw::lib::Bytes serialize_settings(std::shared_ptr<::cw::setting::Setting> arg_setting, bool arg_writeplayingdata = true);
std::shared_ptr<::cw::data::CWPyElement> settings_element(const ::cw::setting::Setting& setting, bool writeplayingdata = true);
void create_localsettings(std::shared_ptr<::cw::data::CWPyElement> arg_element, std::shared_ptr<::cw::setting::LocalSetting> arg_local);
::cw::lib::String create_albumpage(::cw::lib::String arg_path, bool arg_lost = false, bool arg_nocoupon = false);
::cw::lib::String create_adventurer(std::shared_ptr<::cw::dialog::create::AdventurerData> arg_data);
::cw::lib::String create_adventurer_get_coupon(::cw::lib::String arg_name, ::cw::lib::Integer arg_value);
std::vector<std::shared_ptr<::cw::image::ImageInfo>> write_castimagepath(::cw::lib::String arg_name, std::vector<std::shared_ptr<::cw::image::ImageInfo>> arg_paths, bool arg_can_loaded_scaledimage, bool arg_checkfilename = true);
::cw::lib::String create_scenariolog(std::shared_ptr<::cw::data::ScenarioData> arg_sdata, ::cw::lib::String arg_path, bool arg_recording, ::cw::lib::String arg_logfilepath);
std::shared_ptr<::cw::data::CWPyElement> create_scenariolog_make_colorelement(::cw::lib::String arg_name, ::cw::lib::Value arg_color);
void main();
}

