#pragma once

// cw/binary/base.py を移植した宣言。
#include "cw/lib/compat.hpp"
#include "cw/fwd.hpp"

namespace cw::binary::base {
// 逆変換は実行中のエンジンの全体状態を変えず、選択した宿のルート
// を使う。変換ワーカー上のスコープに保持し、例外時にも復元する。
class ImageImportScope {
public:
    ImageImportScope(::cw::lib::String tempdir, ::cw::lib::String yadodir);
    ~ImageImportScope();
    ImageImportScope(const ImageImportScope&) = delete;
    ImageImportScope& operator=(const ImageImportScope&) = delete;
private:
    std::optional<std::vector<::cw::lib::String>> m_previous;
};

class CWBinaryBase : public ::cw::lib::Object,
                     public std::enable_shared_from_this<CWBinaryBase> {
public:
    CWBinaryBase() = default;
    virtual ~CWBinaryBase() = default;
    bool m__force_exportsetting{};
    bool m__image_export{true};
    ::cw::lib::String m__materialdir{"Material"};
    ::cw::lib::String m_dir{};
    ::cw::lib::String m_fpath{};
    ::cw::lib::Integer m_id{};
    std::optional<::cw::lib::Bytes> m_image{};
    ::cw::lib::String m_imgdir{};
    ::cw::lib::String m_materialbasedir{};
    ::cw::lib::String m_name{};
    std::weak_ptr<::cw::binary::base::CWBinaryBase> m_root{};
    ::cw::lib::String m_xmlpath{};
    ::cw::lib::String m_xmltype{};
    bool m_yadodata{};
    std::optional<std::shared_ptr<::cw::yadodb::YadoDB>> m_yadodb{};
    void initialize(std::optional<std::shared_ptr<::cw::binary::base::CWBinaryBase>> arg_parent,
                    std::optional<std::shared_ptr<::cw::binary::cwfile::CWFile>> arg_f,
                    bool arg_yadodata = false,
                    ::cw::lib::String arg_materialdir = "Material",
                    bool arg_image_export = true);
    void set_root(std::optional<std::shared_ptr<::cw::binary::base::CWBinaryBase>> arg_parent);
    std::shared_ptr<::cw::binary::base::CWBinaryBase> get_root();
    void set_dir(::cw::lib::String arg_path);
    ::cw::lib::String get_dir();
    void set_imgdir(::cw::lib::String arg_path);
    ::cw::lib::String get_imgdir();
    ::cw::lib::String get_fname();
    bool is_root();
    bool is_yadodata();
    void set_materialdir(::cw::lib::String arg_materialdir);
    ::cw::lib::String get_materialdir();
    void set_image_export(bool arg_image_export, bool arg_force = false);
    bool get_image_export();
    virtual ::cw::lib::String create_xml(::cw::lib::String arg_dpath);
    virtual ::cw::lib::String export_image();
    static void check_imgpath(std::shared_ptr<::cw::binary::cwfile::CWFileWriter> arg_f, std::shared_ptr<::cw::data::CWPyElement> arg_e_imgpath, ::cw::lib::String arg_defpostype);
    static void check_coupon(std::shared_ptr<::cw::binary::cwfile::CWFileWriter> arg_f, ::cw::lib::String arg_coupon);
    static std::optional<::cw::lib::Bytes> import_image(std::shared_ptr<::cw::binary::cwfile::CWFileWriter> arg_f, ::cw::lib::Value arg_imagepath, bool arg_convertbitmap = true, bool arg_fullpath = false, ::cw::lib::String arg_defpostype = "TopLeft");
    static std::optional<::cw::lib::Bytes> import_image(
        std::shared_ptr<::cw::binary::cwfile::CWFileWriter> arg_f,
        const ::cw::lib::String& arg_imagepath, bool arg_convertbitmap = true,
        bool arg_fullpath = false, ::cw::lib::String arg_defpostype = "TopLeft");
    static std::optional<::cw::lib::Bytes> import_image(
        std::shared_ptr<::cw::binary::cwfile::CWFileWriter> arg_f,
        const std::shared_ptr<::cw::data::CWPyElement>& arg_imagepath,
        bool arg_convertbitmap = true, bool arg_fullpath = false,
        ::cw::lib::String arg_defpostype = "TopLeft");
    virtual std::shared_ptr<::cw::data::CWPyElement> get_data();
    ::cw::lib::String get_materialpath(::cw::lib::String arg_path);
    static ::cw::lib::String materialpath(::cw::lib::String arg_path);
    ::cw::lib::String get_indent(::cw::lib::Integer arg_indent);
    ::cw::lib::String get_propertiestext(std::map<::cw::lib::String, ::cw::lib::String> arg_d);
    std::tuple<::cw::lib::String, ::cw::lib::String> conv_contenttype(::cw::lib::Integer arg_n);
    static ::cw::lib::Integer unconv_contenttype(::cw::lib::String arg_ctype, ::cw::lib::String arg_n, std::shared_ptr<::cw::binary::cwfile::CWFileWriter> arg_f);
    ::cw::lib::String conv_target_member(::cw::lib::Integer arg_n);
    static ::cw::lib::Integer unconv_target_member(::cw::lib::String arg_n, std::shared_ptr<::cw::binary::cwfile::CWFileWriter> arg_f, bool arg_effectcontent = false);
    ::cw::lib::String conv_target_member_dialog(::cw::lib::Integer arg_n);
    static ::cw::lib::Integer unconv_target_member_dialog(::cw::lib::String arg_n, std::shared_ptr<::cw::binary::cwfile::CWFileWriter> arg_f);
    ::cw::lib::String conv_target_scope(::cw::lib::Integer arg_n);
    static ::cw::lib::Integer unconv_target_scope(::cw::lib::String arg_n, std::shared_ptr<::cw::binary::cwfile::CWFileWriter> arg_f);
    ::cw::lib::String conv_target_scope_coupon(::cw::lib::Integer arg_n);
    static ::cw::lib::Integer unconv_target_scope_coupon(::cw::lib::String arg_n, std::shared_ptr<::cw::binary::cwfile::CWFileWriter> arg_f);
    std::set<::cw::lib::String> conv_castranges(::cw::lib::Integer arg_n);
    static ::cw::lib::Integer unconv_castranges(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    ::cw::lib::String conv_keycoderange(::cw::lib::Integer arg_n);
    static ::cw::lib::Integer unconv_keycoderange(::cw::lib::String arg_n, std::shared_ptr<::cw::binary::cwfile::CWFileWriter> arg_f);
    ::cw::lib::String conv_spreadtype(::cw::lib::Integer arg_n);
    static ::cw::lib::Integer unconv_spreadtype(::cw::lib::String arg_n);
    ::cw::lib::String conv_statustype(::cw::lib::Integer arg_n);
    static ::cw::lib::Integer unconv_statustype(::cw::lib::String arg_n, std::shared_ptr<::cw::binary::cwfile::CWFileWriter> arg_f);
    ::cw::lib::String conv_effectcardtype(::cw::lib::Integer arg_n);
    static ::cw::lib::Integer unconv_effectcardtype(::cw::lib::String arg_n);
    ::cw::lib::String conv_comparison4(::cw::lib::Integer arg_n);
    static ::cw::lib::Integer unconv_comparison4(::cw::lib::String arg_n);
    ::cw::lib::String conv_comparison3(::cw::lib::Integer arg_n);
    static ::cw::lib::Integer unconv_comparison3(::cw::lib::String arg_n);
    ::cw::lib::String conv_effectmotion_element(::cw::lib::Integer arg_n);
    static ::cw::lib::Integer unconv_effectmotion_element(::cw::lib::String arg_n);
    ::cw::lib::String conv_effectmotion_type(::cw::lib::Integer arg_tabn, ::cw::lib::Integer arg_n);
    static std::tuple<::cw::lib::Integer, ::cw::lib::Integer> unconv_effectmotion_type(::cw::lib::String arg_n, std::shared_ptr<::cw::binary::cwfile::CWFileWriter> arg_f);
    ::cw::lib::String conv_effectmotion_damagetype(::cw::lib::Integer arg_n);
    static ::cw::lib::Integer unconv_effectmotion_damagetype(::cw::lib::String arg_n);
    ::cw::lib::String conv_card_effecttype(::cw::lib::Integer arg_n);
    static ::cw::lib::Integer unconv_card_effecttype(::cw::lib::String arg_n);
    ::cw::lib::String conv_card_resisttype(::cw::lib::Integer arg_n);
    static ::cw::lib::Integer unconv_card_resisttype(::cw::lib::String arg_n);
    ::cw::lib::String conv_card_visualeffect(::cw::lib::Integer arg_n);
    static ::cw::lib::Integer unconv_card_visualeffect(::cw::lib::String arg_n);
    ::cw::lib::String conv_card_physicalability(::cw::lib::Integer arg_n);
    static ::cw::lib::Integer unconv_card_physicalability(::cw::lib::String arg_n);
    ::cw::lib::String conv_card_mentalability(::cw::lib::Integer arg_n);
    static ::cw::lib::Integer unconv_card_mentalability(::cw::lib::String arg_n);
    ::cw::lib::String conv_card_target(::cw::lib::Integer arg_n);
    static ::cw::lib::Integer unconv_card_target(::cw::lib::String arg_n);
    ::cw::lib::String conv_card_premium(::cw::lib::Integer arg_n);
    static ::cw::lib::Integer unconv_card_premium(::cw::lib::String arg_n);
    ::cw::lib::String conv_mentality(::cw::lib::Integer arg_n);
    static ::cw::lib::Integer unconv_mentality(::cw::lib::String arg_n);
    ::cw::lib::String conv_yadotype(::cw::lib::Integer arg_n);
    static ::cw::lib::Integer unconv_yadotype(::cw::lib::String arg_n);
    ::cw::lib::String conv_yado_summaryview(::cw::lib::Integer arg_n);
    static ::cw::lib::Integer unconv_yado_summaryview(::cw::lib::String arg_n);
    ::cw::lib::String conv_yado_bgchange(::cw::lib::Integer arg_n);
    static ::cw::lib::Integer unconv_yado_bgchange(::cw::lib::String arg_n);
    ::cw::lib::String conv_borderingtype(::cw::lib::Integer arg_n);
    static ::cw::lib::Integer unconv_borderingtype(::cw::lib::String arg_n);
    ::cw::lib::String conv_blendmode(::cw::lib::Integer arg_n);
    static ::cw::lib::Integer unconv_blendmode(::cw::lib::String arg_n);
    ::cw::lib::String conv_gradientdir(::cw::lib::Integer arg_n);
    static ::cw::lib::Integer unconv_gradientdir(::cw::lib::String arg_n);
};

void main();
}

