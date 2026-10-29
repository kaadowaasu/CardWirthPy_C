#include "cw/binary/event.hpp"
#include "cw/lib/python_integer.hpp"

#include "cw/binary/content.hpp"
#include "cw/binary/cwfile.hpp"
#include "cw/data.hpp"
#include "cw/util.hpp"

#include <cctype>
#include <stdexcept>

namespace cw::binary::event {
namespace {

using Element = ::cw::data::CWPyElement;
using ElementPtr = std::shared_ptr<Element>;
using Integer = ::cw::lib::Integer;
using String = ::cw::lib::String;

std::shared_ptr<::cw::binary::base::CWBinaryBase> parent_from_value(
    const ::cw::lib::Value& value)
{
    if (const auto* object = std::get_if<::cw::lib::ObjectPtr>(&value.storage())) {
        return std::dynamic_pointer_cast<::cw::binary::base::CWBinaryBase>(*object);
    }
    return nullptr;
}

::cw::lib::Value self_value(::cw::binary::base::CWBinaryBase& value)
{
    try {
        return ::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(value.shared_from_this()));
    } catch (const std::bad_weak_ptr&) {
        return {};
    }
}

Integer parse_integer(const String& text) {
    // Python版と同様、バイナリの固定幅へ詰めるまで任意精度の整数の意味を保
    // つ。 書き込み専用の共通番兵値で、失敗時点と先行する出力バイトを維持する。
    return ::cw::lib::require_python_integer_for_cardwirth_pack(text);
}

void write_contents(const std::shared_ptr<::cw::binary::cwfile::CWFileWriter>& file,
                    const ElementPtr& contents)
{
    if (!contents) {
        file->write_dword(0);
        return;
    }
    const auto children = contents->iterate();
    file->write_dword(static_cast<Integer>(children.size()));
    for (const auto& child : children) {
        ::cw::binary::content::Content::unconv(file, child);
    }
}

void initialize_base(::cw::binary::base::CWBinaryBase& event,
                     const ::cw::lib::Value& parent,
                     const std::shared_ptr<::cw::binary::cwfile::CWFile>& file,
                     bool yadodata)
{
    const auto parent_object = parent_from_value(parent);
    std::optional<std::shared_ptr<::cw::binary::base::CWBinaryBase>> parent_value;
    if (parent_object) {
        parent_value = parent_object;
    }
    event.CWBinaryBase::initialize(parent_value, file, yadodata);
}

}  // namespace

void Event::initialize(::cw::lib::Value arg_parent,
                       std::shared_ptr<::cw::binary::cwfile::CWFile> arg_f,
                       bool arg_yadodata)
{
    if (!arg_f) {
        throw std::invalid_argument("Event requires binary input");
    }
    initialize_base(*this, arg_parent, arg_f, arg_yadodata);
    m_xmltype = "Event";
    m_contents.clear();
    m_ignitions.clear();
    m_keycodes.clear();
    m_data.reset();

    const auto contents_count = arg_f->dword();
    const auto parent = self_value(*this);
    for (Integer index = 0; index < contents_count; ++index) {
        auto content = std::make_shared<::cw::binary::content::Content>();
        content->initialize(parent, arg_f, 0, true);
        m_contents.push_back(std::move(content));
    }

    const auto ignition_count = arg_f->dword();
    for (Integer index = 0; index < ignition_count; ++index) {
        m_ignitions.push_back(arg_f->dword());
    }
    m_keycodes = arg_f->string(true);
}

// Event はローカルの直列化が成功してから m_data
// を公開する。 Python版の早い self.data
// 代入とは異なり、不完全なツリーを次回用にキャッシュしない。
std::shared_ptr<Element> Event::get_data()
{
    if (m_data && *m_data) {
        return *m_data;
    }

    auto data = ::cw::data::make_element("Event");
    auto ignitions = ::cw::data::make_element("Ignitions");
    std::vector<String> number_values;
    number_values.reserve(m_ignitions.size());
    for (const auto number : m_ignitions) {
        number_values.push_back(std::to_string(number));
    }
    ignitions->append(::cw::data::make_element(
        "Number", number_values.empty() ? "" : ::cw::util::encodetextlist(number_values)));

    auto keycode_values = ::cw::util::decodetextlist(m_keycodes);
    String matching = "Or";
    if (!keycode_values.empty() && keycode_values.front() == "MatchingType=All") {
        matching = "And";
        keycode_values.erase(keycode_values.begin());
    }
    ignitions->set("keyCodeMatchingType", matching);
    ignitions->append(::cw::data::make_element(
        "KeyCodes", ::cw::util::encodetextlist(keycode_values)));
    data->append(ignitions);

    auto contents = ::cw::data::make_element("Contents");
    for (const auto& content : m_contents) {
        if (!content) {
            throw std::invalid_argument("null Content in Event");
        }
        contents->append(content->get_data());
    }
    data->append(contents);
    m_data = data;
    return data;
}

void Event::unconv(std::shared_ptr<::cw::binary::cwfile::CWFileWriter> arg_f,
                   std::shared_ptr<Element> arg_data)
{
    if (!arg_f || !arg_data) {
        throw std::invalid_argument("Event::unconv requires file and data");
    }

    ElementPtr contents;
    std::vector<Integer> ignitions;
    String keycodes;
    for (const auto& element : arg_data->iterate()) {
        if (!element) {
            continue;
        }
        if (element->get_tag() == "Ignitions") {
            const auto matching = element->get("keyCodeMatchingType", "Or");
            for (const auto& ignition : element->iterate()) {
                if (!ignition) {
                    continue;
                }
                if (ignition->get_tag() == "Number") {
                    for (const auto& number : ::cw::util::decodetextlist(ignition->get_text())) {
                        const auto value = parse_integer(number);
                        if (value == 4) {
                            arg_f->check_version(1.50, "毎ラウンド発火条件");
                        } else if (value == 5) {
                            arg_f->check_version(1.50, "バトル開始発火条件");
                        } else if (value == 6) {
                            arg_f->check_wsnversion("4", "ラウンド終了発火条件");
                        }
                        ignitions.push_back(value);
                    }
                } else if (ignition->get_tag() == "KeyCodes") {
                    if (matching == "And") {
                        arg_f->check_version(1.50, "キーコード全てに一致で発火");
                        auto values = ::cw::util::decodetextlist(ignition->get_text());
                        values.insert(values.begin(), "MatchingType=All");
                        keycodes = ::cw::util::encodetextlist(values);
                    } else {
                        keycodes = ignition->get_text();
                    }
                }
            }
        } else if (element->get_tag() == "Contents") {
            contents = element;
        }
    }

    write_contents(arg_f, contents);
    arg_f->write_dword(static_cast<Integer>(ignitions.size()));
    for (const auto ignition : ignitions) {
        arg_f->write_dword(ignition);
    }
    arg_f->write_string(keycodes, true);
}

void SimpleEvent::initialize(::cw::lib::Value arg_parent,
                             std::shared_ptr<::cw::binary::cwfile::CWFile> arg_f,
                             bool arg_yadodata)
{
    if (!arg_f) {
        throw std::invalid_argument("SimpleEvent requires binary input");
    }
    initialize_base(*this, arg_parent, arg_f, arg_yadodata);
    m_xmltype = "SimpleEvent";
    m_contents.clear();
    m_data.reset();

    const auto contents_count = arg_f->dword();
    const auto parent = self_value(*this);
    for (Integer index = 0; index < contents_count; ++index) {
        auto content = std::make_shared<::cw::binary::content::Content>();
        content->initialize(parent, arg_f, 0, true);
        m_contents.push_back(std::move(content));
    }
}

// SimpleEvent はローカルの直列化が成功してから m_data
// を公開する。 Python版の早い self.data
// 代入とは異なり、不完全なツリーを次回用にキャッシュしない。
std::shared_ptr<Element> SimpleEvent::get_data()
{
    if (m_data && *m_data) {
        return *m_data;
    }
    auto data = ::cw::data::make_element("Event");
    auto contents = ::cw::data::make_element("Contents");
    for (const auto& content : m_contents) {
        if (!content) {
            throw std::invalid_argument("null Content in SimpleEvent");
        }
        contents->append(content->get_data());
    }
    data->append(contents);
    m_data = data;
    return data;
}

void SimpleEvent::unconv(std::shared_ptr<::cw::binary::cwfile::CWFileWriter> arg_f,
                         std::shared_ptr<Element> arg_data)
{
    if (!arg_f || !arg_data) {
        throw std::invalid_argument("SimpleEvent::unconv requires file and data");
    }
    ElementPtr contents;
    for (const auto& element : arg_data->iterate()) {
        if (element && element->get_tag() == "Contents") {
            contents = element;
        }
    }
    write_contents(arg_f, contents);
}

void main()
{
}

}  // namespace cw::binary::event
