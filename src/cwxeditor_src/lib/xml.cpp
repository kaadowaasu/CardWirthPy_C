#include "cwxeditor_src/lib/xml.hpp"

#include <algorithm>
#include <cctype>
#include <locale>
#include <string>

namespace cwxeditor::lib {

namespace {

bool is_name_start(const unsigned char value) {
    return (value >= static_cast<unsigned char>('A') && value <= static_cast<unsigned char>('Z')) ||
           (value >= static_cast<unsigned char>('a') && value <= static_cast<unsigned char>('z')) ||
           value == static_cast<unsigned char>('_') || value == static_cast<unsigned char>(':') || value >= 0x80U;
}

bool is_name_char(const unsigned char value) {
    return is_name_start(value) || (value >= static_cast<unsigned char>('0') &&
                                    value <= static_cast<unsigned char>('9')) ||
           value == static_cast<unsigned char>('-') || value == static_cast<unsigned char>('.');
}

bool is_xml_char(const std::uint32_t value) {
    return value == 0x9U || value == 0xAU || value == 0xDU ||
           (value >= 0x20U && value <= 0xD7FFU) ||
           (value >= 0xE000U && value <= 0xFFFDU) ||
           (value >= 0x10000U && value <= 0x10FFFFU);
}

std::string normalize_newlines(std::string_view input) {
    if (input.find('\r') == std::string_view::npos) return std::string(input);
    std::string result;
    result.reserve(input.size());
    for (std::size_t i = 0; i < input.size(); ++i) {
        if (input[i] == '\r') {
            if (i + 1 < input.size() && input[i + 1] == '\n') ++i;
            result.push_back('\n');
        } else {
            result.push_back(input[i]);
        }
    }
    return result;
}

bool is_xml_whitespace_only(std::string_view input) {
    return std::all_of(input.begin(), input.end(), [](const char value) {
        return value == ' ' || value == '\t' || value == '\n' || value == '\r';
    });
}

void append_utf8(std::string& output, std::uint32_t value) {
    if (!is_xml_char(value) || (value >= 0xD800U && value <= 0xDFFFU)) {
        throw XmlValueError("Invalid XML character reference");
    }
    // 共通のUTF-8処理はUnicodeスカラーの妥当性を検証する。 XML
    // 解析の境界では、さらに狭いXML 1.0の文字集合も検査する。
    utf8::append_scalar(output, value);
}

std::string decode_entities(std::string_view input) {
    std::string output;
    output.reserve(input.size());
    for (std::size_t i = 0; i < input.size(); ++i) {
        const char value = input[i];
        if (value != '&') {
            const auto byte = static_cast<unsigned char>(value);
            if (byte < 0x20U && byte != '\t' && byte != '\n' && byte != '\r') {
                throw XmlValueError("Invalid control character in XML");
            }
            output.push_back(value);
            continue;
        }
        const auto end = input.find(';', i + 1);
        if (end == std::string_view::npos) throw XmlValueError("Unterminated XML entity");
        const auto entity = input.substr(i + 1, end - i - 1);
        if (entity == "amp") {
            output.push_back('&');
        } else if (entity == "lt") {
            output.push_back('<');
        } else if (entity == "gt") {
            output.push_back('>');
        } else if (entity == "quot") {
            output.push_back('"');
        } else if (entity == "apos") {
            output.push_back('\'');
        } else if (entity.size() >= 2U && entity.front() == '#') {
            std::uint32_t code_point{};
            const auto digits = entity.substr(1);
            int base = 10;
            std::size_t begin = 0;
            if (!digits.empty() && (digits.front() == 'x' || digits.front() == 'X')) {
                base = 16;
                begin = 1;
            }
            if (begin == digits.size()) throw XmlValueError("Empty XML character reference");
            for (std::size_t j = begin; j < digits.size(); ++j) {
                const unsigned char digit = static_cast<unsigned char>(digits[j]);
                unsigned int number = 0;
                if (digit >= static_cast<unsigned char>('0') && digit <= static_cast<unsigned char>('9')) {
                    number = digit - static_cast<unsigned char>('0');
                } else if (base == 16 && digit >= static_cast<unsigned char>('a') &&
                           digit <= static_cast<unsigned char>('f')) {
                    number = digit - static_cast<unsigned char>('a') + 10U;
                } else if (base == 16 && digit >= static_cast<unsigned char>('A') &&
                           digit <= static_cast<unsigned char>('F')) {
                    number = digit - static_cast<unsigned char>('A') + 10U;
                } else {
                    throw XmlValueError("Invalid XML character reference");
                }
                if (number >= static_cast<unsigned int>(base)) {
                    throw XmlValueError("Invalid XML character reference");
                }
                if (code_point > (0x10FFFFU - number) / static_cast<unsigned int>(base)) {
                    throw XmlValueError("XML character reference is out of range");
                }
                code_point = code_point * static_cast<unsigned int>(base) + number;
            }
            append_utf8(output, code_point);
        } else {
            throw XmlValueError("Unknown XML entity: &" + std::string(entity) + ";");
        }
        i = end;
    }
    return output;
}

std::string make_name_error(std::string_view name) {
    return "Invalid XML name: " + std::string(name);
}

void require_name(std::string_view name) {
    if (name.empty() || !is_name_start(static_cast<unsigned char>(name.front()))) {
        throw std::invalid_argument(make_name_error(name));
    }
    for (const unsigned char value : std::string(name).substr(1)) {
        if (!is_name_char(value)) throw std::invalid_argument(make_name_error(name));
    }
}

}  // namespace

struct XmlNode::Impl {
    XmlNodeType type{XmlNodeType::Element};
    std::string name;
    std::string data;
    std::vector<XmlAttribute> attributes;
    std::vector<std::shared_ptr<Impl>> children;
    std::weak_ptr<Impl> parent;
};

namespace {

std::shared_ptr<XmlNode::Impl> make_impl(XmlNodeType type, std::string_view name = {},
                                         std::string_view data = {}) {
    auto result = std::make_shared<XmlNode::Impl>();
    result->type = type;
    result->name = name;
    result->data = data;
    return result;
}

std::shared_ptr<XmlNode::Impl> clone_impl(const std::shared_ptr<XmlNode::Impl>& source,
                                          const std::shared_ptr<XmlNode::Impl>& parent = {}) {
    if (!source) return {};
    auto result = make_impl(source->type, source->name, source->data);
    result->attributes = source->attributes;
    result->parent = parent;
    result->children.reserve(source->children.size());
    for (const auto& child : source->children) {
        result->children.push_back(clone_impl(child, result));
    }
    return result;
}

void require_node(const std::shared_ptr<XmlNode::Impl>& node) {
    if (!node) throw std::logic_error("Invalid XML node");
}

void require_element(const std::shared_ptr<XmlNode::Impl>& node) {
    require_node(node);
    if (node->type != XmlNodeType::Element) throw std::logic_error("XML operation requires an element");
}

std::string direct_text(const std::shared_ptr<XmlNode::Impl>& node) {
    if (!node) return {};
    std::string result;
    for (const auto& child : node->children) {
        if (child && child->type == XmlNodeType::Text) result += child->data;
    }
    return result;
}

void set_direct_text(const std::shared_ptr<XmlNode::Impl>& node, std::string_view value) {
    require_element(node);
    std::vector<std::shared_ptr<XmlNode::Impl>> retained;
    retained.reserve(node->children.size() + (value.empty() ? 0U : 1U));
    if (!value.empty()) {
        auto text = make_impl(XmlNodeType::Text, {}, value);
        text->parent = node;
        retained.push_back(std::move(text));
    }
    for (const auto& child : node->children) {
        if (child && child->type == XmlNodeType::Text) continue;
        // XNodeで代入した値は、要素・その他の子より前
        // の直接のテキストとして扱い、子の順序は維持する。
        retained.push_back(child);
    }
    node->children = std::move(retained);
}

}  // namespace

XmlParseError::XmlParseError(std::string message, const std::size_t offset,
                             const std::size_t line, const std::size_t column)
    : std::runtime_error(std::move(message) + " at line " + std::to_string(line) + ", column " +
                         std::to_string(column)),
      offset_(offset),
      line_(line),
      column_(column) {}

XmlNode::XmlNode(std::shared_ptr<Impl> impl, const bool root, const bool dxml_compat)
    : impl_(std::move(impl)), root_(root), dxml_compat_(dxml_compat) {}

XmlNode::~XmlNode() = default;

XmlNode XmlNode::create(const std::string_view root_name, const std::string_view value) {
    require_name(root_name);
    auto node = make_impl(XmlNodeType::Element, root_name);
    XmlNode result(std::move(node), true, true);
    result.value(value);
    return result;
}

XmlNode XmlNode::parse(const std::string_view xml, const bool allow_single_element) {
    XmlParseOptions options;
    options.allow_single_element = allow_single_element;
    // DXMLは空白だけのテキストイベントを除き、XNode
    // が残りを親の値へ追加する。 互換表示では children()
    // からもテキストノードを隠す。
    options.ignore_whitespace_only_text = true;
    auto result = XmlDocument::parse(xml, options).root();
    result.dxml_compat_ = true;
    return result;
}

XmlNode XmlNode::parse(const std::string_view xml, const XmlParseOptions& options) {
    auto result = XmlDocument::parse(xml, options).root();
    result.dxml_compat_ = true;
    return result;
}

XmlNodeType XmlNode::type() const noexcept {
    return impl_ ? impl_->type : XmlNodeType::Element;
}

std::string XmlNode::name() const {
    if (impl_ && impl_->type == XmlNodeType::ProcessingInstruction) return {};
    return impl_ ? impl_->name : std::string{};
}

std::string XmlNode::value() const {
    if (!impl_) return {};
    if (impl_->type == XmlNodeType::Element) {
        const auto result = direct_text(impl_);
        // cwx.xml.XNode は、dxmlが子の周囲に出力する整
        // 形用の一改行を意図的に表示から隠すが、直列化用には保持する。
        return result == "\n" ? std::string{} : result;
    }
    if (impl_->type == XmlNodeType::ProcessingInstruction) {
        auto result = impl_->name;
        if (!impl_->data.empty()) {
            const auto first = impl_->data.front();
            if (first != ' ' && first != '\t' && first != '\r' && first != '\n') result.push_back(' ');
            result += impl_->data;
        }
        return result;
    }
    return impl_->data == "\n" ? std::string{} : impl_->data;
}

void XmlNode::value(const std::string_view text) {
    if (impl_ && impl_->type != XmlNodeType::Element) {
        if (impl_->type == XmlNodeType::ProcessingInstruction) {
            const auto split = text.find_first_of(" \t\r\n");
            impl_->name = text.substr(0, split);
            impl_->data = split == std::string_view::npos ? std::string{} : std::string(text.substr(split));
        } else impl_->data = text;
        return;
    }
    set_direct_text(impl_, text);
}

std::string XmlNode::text_content() const {
    if (!impl_) return {};
    if (impl_->type != XmlNodeType::Element) return impl_->data;
    return direct_text(impl_);
}

std::string XmlNode::data() const {
    return impl_ ? impl_->data : std::string{};
}

XmlNode XmlNode::new_element(const std::string_view child_name) {
    return new_element_with_value(child_name, {});
}

XmlNode XmlNode::new_element_with_value(const std::string_view child_name,
                                        const std::string_view child_value) {
    require_element(impl_);
    require_name(child_name);
    auto child = make_impl(XmlNodeType::Element, child_name);
    child->parent = impl_;
    impl_->children.push_back(child);
    XmlNode result(std::move(child), false, dxml_compat_);
    result.value(child_value);
    return result;
}

void XmlNode::new_attr(const std::string_view attribute_name, const std::string_view attribute_value) {
    require_element(impl_);
    require_name(attribute_name);
    const auto duplicate = std::find_if(impl_->attributes.begin(), impl_->attributes.end(),
                                        [&](const XmlAttribute& attribute) {
                                            return attribute.name == attribute_name;
                                        });
    if (duplicate != impl_->attributes.end()) {
        throw std::invalid_argument("XML attribute is duplicated: " + std::string(attribute_name));
    }
    impl_->attributes.push_back({std::string(attribute_name), std::string(attribute_value)});
}

std::vector<XmlAttribute> XmlNode::attributes() const {
    return impl_ && impl_->type == XmlNodeType::Element ? impl_->attributes : std::vector<XmlAttribute>{};
}

std::optional<std::string> XmlNode::attribute(const std::string_view attribute_name) const {
    if (!impl_ || impl_->type != XmlNodeType::Element) return std::nullopt;
    const auto found = std::find_if(impl_->attributes.begin(), impl_->attributes.end(),
                                    [&](const XmlAttribute& attribute) {
                                        return attribute.name == attribute_name;
                                    });
    if (found == impl_->attributes.end()) return std::nullopt;
    return found->value;
}

std::string XmlNode::attr(const std::string_view attribute_name, const bool nothing_is_error,
                          const std::string_view default_value) const {
    const auto found = attribute(attribute_name);
    if (!found.has_value()) {
        if (nothing_is_error) {
            throw XmlValueError("XML attribute is not found: " + std::string(attribute_name));
        }
        return std::string(default_value);
    }
    return *found;
}

bool XmlNode::has_attr(const std::string_view attribute_name) const {
    require_node(impl_);
    return std::any_of(impl_->attributes.begin(), impl_->attributes.end(),
        [&](const XmlAttribute& attribute) { return attribute.name == attribute_name; });
}

void XmlNode::remove_attr(const std::string_view attribute_name) {
    require_element(impl_);
    impl_->attributes.erase(std::remove_if(impl_->attributes.begin(), impl_->attributes.end(),
                                           [&](const XmlAttribute& attribute) {
                                               return attribute.name == attribute_name;
                                           }),
                            impl_->attributes.end());
}

void XmlNode::remove_attr_recursive(const std::string_view attribute_name) {
    // D版の取得した文字列は、削除する属性と同じ領域でも保存先を保持する。
    // ネイティブの string_view も、走査全体で所有して保持する。
    const std::string name(attribute_name);
    std::function<void(const std::shared_ptr<Impl>&)> remove_from = [&](const std::shared_ptr<Impl>& node) {
        require_node(node);
        node->attributes.erase(std::remove_if(node->attributes.begin(), node->attributes.end(),
                                              [&](const XmlAttribute& attribute) {
                                                  return attribute.name == name;
                                              }),
                              node->attributes.end());
        for (const auto& child : node->children) remove_from(child);
    };
    remove_from(impl_);
}

XmlNode XmlNode::child(const std::string_view child_name, const bool nothing_is_error) const {
    // D版の XNode と同様、要素であることを要求せず、現在ノードの子
    // 一覧を検索する。 有効なその他のノードには、一致する子がないだけ。
    require_node(impl_);
    for (const auto& child_node : impl_->children) {
        if (child_node && child_node->type != XmlNodeType::Text &&
            (child_node->type == XmlNodeType::Element ? std::string_view(child_node->name) : std::string_view{}) == child_name) {
            return XmlNode(child_node, false, dxml_compat_);
        }
    }
    if (nothing_is_error) {
        throw XmlValueError("XML child is not found: " + std::string(child_name));
    }
    return {};
}

std::vector<XmlNode> XmlNode::children() const {
    std::vector<XmlNode> result;
    if (!impl_ || impl_->type != XmlNodeType::Element) return result;
    result.reserve(impl_->children.size());
    for (const auto& child_node : impl_->children) {
        if (dxml_compat_ && child_node && child_node->type == XmlNodeType::Text) continue;
        // 子ハンドルを保存するときも、ハンドルごとのD版互換の表示を維持する。
        const XmlNode child_handle(child_node, false, dxml_compat_);
        result.push_back(child_handle);
    }
    return result;
}

std::vector<XmlNode> XmlNode::element_children() const {
    std::vector<XmlNode> result;
    if (!impl_ || impl_->type != XmlNodeType::Element) return result;
    for (const auto& child_node : impl_->children) {
        if (child_node && child_node->type == XmlNodeType::Element) {
            // 子ハンドルを保存するときも、ハンドルごとのD版互換の表示を維持する。
            const XmlNode child_handle(child_node, false, dxml_compat_);
            result.push_back(child_handle);
        }
    }
    return result;
}

std::vector<XmlNode> XmlNode::find_all(const std::string_view child_name) const {
    std::vector<XmlNode> result;
    if (!impl_ || impl_->type != XmlNodeType::Element) return result;
    for (const auto& child_node : impl_->children) {
        if (child_node && child_node->type == XmlNodeType::Element && child_node->name == child_name) {
            result.push_back(XmlNode(child_node, false, dxml_compat_));
        }
    }
    return result;
}

std::string XmlNode::child_text(const std::string_view child_name, const bool nothing_is_error,
                                const std::string_view default_value) const {
    const auto found = child(child_name, nothing_is_error);
    return found.valid() ? found.value() : std::string(default_value);
}

void XmlNode::parse() {
    if (!impl_ || impl_->type != XmlNodeType::Element) return;
    std::map<std::string, Handler> handlers = onTag;
    for (const auto& [name_key, handler] : on_tag) {
        if (!handlers.contains(name_key)) handlers.emplace(name_key, handler);
    }
    if (handlers.empty()) return;
    for (const auto& child_node : impl_->children) {
        if (!child_node || child_node->type == XmlNodeType::Text) continue;
        // D版と同様、通常テキストは親の値に保存する。 その他のノードは標準
        // の振り分け処理を使い、対象名が要素タグではない処理命令も含める。
        auto found = handlers.find(child_node->type == XmlNodeType::Element ? child_node->name : "");
        if (found == handlers.end()) found = handlers.find("");
        if (found == handlers.end()) found = handlers.find("*");
        if (found != handlers.end() && found->second) {
            XmlNode child_handle(child_node, false, dxml_compat_);
            found->second(child_handle);
        }
    }
    // D版と同様、処理関数が例外を出しても、再試行用の登録を残す。
    onTag.clear();
    on_tag.clear();
}

void XmlNode::parseDCompatible() {
    if (!impl_ || impl_->type != XmlNodeType::Element || onTag.empty()) return;
    for (const auto& child_node : impl_->children) {
        if (!child_node || child_node->type == XmlNodeType::Text) continue;
        const auto name = child_node->type == XmlNodeType::Element
            ? child_node->name : std::string{};
        auto handler_entry = onTag.find(name);
        if (handler_entry == onTag.end()) handler_entry = onTag.find("");
        // 空の std::function が登録されていても、正確なキーの項目として
        // 扱う。 D版のキー検索と同様、nullキーの代替処理を抑止する必要がある。
        const auto handler = handler_entry == onTag.end() ? Handler{} : handler_entry->second;
        // D版のnull可能なデリゲートと異なり、空の std::function
        // は std::bad_function_call を投げる。
        // 登録済みの空関数は、代替処理を隠す効果を保ったうえで何もしない
        // ものとして扱う。 D版の XNode.parse と同様、不一
        // 致の子名にはnullキー（ここでは空キー）だけを使う。 従来の
        // C++版の * の代替処理で、D版が処理しない子まで呼ばない。
        if (handler) {
            XmlNode child_handle(child_node, false, dxml_compat_);
            handler(child_handle);
        }
    }
    // D版と同様、コールバックが例外を出したら、再試行用に処理関数を残す。
    onTag.clear();
    on_tag.clear();
}

void XmlNode::setLiveHandler(std::string name, Handler handler) {
    if (!live_handlers_) live_handlers_ = std::make_shared<std::map<std::string, Handler>>();
    (*live_handlers_)[std::move(name)] = std::move(handler);
}

void XmlNode::parseLive() {
    if (!impl_ || impl_->type != XmlNodeType::Element ||
        !live_handlers_ || live_handlers_->empty()) return;
    for (const auto& child_node : impl_->children) {
        if (!child_node || child_node->type == XmlNodeType::Text) continue;
        Handler handler;
        // 子ごとにこのハンドルを読み直す。 コールバックが登録の置換・切り離
        // しを行えるため、イテレーターや関数への参照をまたいで保持しない。
        if (live_handlers_) {
            auto found = live_handlers_->find(
                child_node->type == XmlNodeType::Element ? child_node->name : "");
            if (found == live_handlers_->end()) found = live_handlers_->find("");
            if (found != live_handlers_->end()) handler = found->second;
        }
        if (handler) {
            XmlNode child_handle(child_node, false, dxml_compat_);
            handler(child_handle);
        }
    }
    // コールバックの例外時は、再試行用の登録を維持する。 clear()
    // ではなくリセットを使い、値のある複製ハンドルの登録を消さない。
    live_handlers_.reset();
}

void XmlNode::set_handler(std::string name, Handler handler) {
    onTag[name] = handler;
    on_tag[std::move(name)] = std::move(handler);
}

void XmlNode::traverse(const Handler& handler) {
    if (!impl_ || impl_->type != XmlNodeType::Element || !handler) return;
    handler(*this);
    for (const auto& child_node : impl_->children) {
        if (!child_node || child_node->type != XmlNodeType::Element) continue;
        XmlNode child_handle(child_node, false, dxml_compat_);
        child_handle.traverse(handler);
    }
}

XmlNode XmlNode::append(XmlNode child_node) {
    require_element(impl_);
    if (!child_node.impl_) throw std::invalid_argument("Cannot append an invalid XML node");
    auto child = clone_impl(child_node.impl_, impl_);
    impl_->children.push_back(child);
    return XmlNode(std::move(child), false, dxml_compat_);
}

XmlNode XmlNode::append_text(const std::string_view text_value) {
    require_element(impl_);
    auto child = make_impl(XmlNodeType::Text, {}, text_value);
    child->parent = impl_;
    impl_->children.push_back(child);
    return XmlNode(std::move(child), false, dxml_compat_);
}

XmlNode XmlNode::append_cdata(const std::string_view text_value) {
    require_element(impl_);
    auto child = make_impl(XmlNodeType::CData, {}, text_value);
    child->parent = impl_;
    impl_->children.push_back(child);
    return XmlNode(std::move(child), false, dxml_compat_);
}

XmlNode XmlNode::append_comment(const std::string_view text_value) {
    require_element(impl_);
    auto child = make_impl(XmlNodeType::Comment, {}, text_value);
    child->parent = impl_;
    impl_->children.push_back(child);
    return XmlNode(std::move(child), false, dxml_compat_);
}

XmlNode XmlNode::append_processing_instruction(const std::string_view target,
                                               const std::string_view text_value) {
    require_element(impl_);
    require_name(target);
    auto child = make_impl(XmlNodeType::ProcessingInstruction, target, text_value);
    child->parent = impl_;
    impl_->children.push_back(child);
    return XmlNode(std::move(child), false, dxml_compat_);
}

void XmlNode::clear_children() {
    require_element(impl_);
    impl_->children.clear();
}

XmlNode XmlNode::clone(const bool as_root) const {
    if (!impl_) return {};
    return XmlNode(clone_impl(impl_), as_root, dxml_compat_);
}

std::string XmlNode::serialize(const XmlWriterOptions& options) const {
    return XmlWriter::write(*this, options);
}

XmlDocument::XmlDocument(XmlNode root) {
    set_root(std::move(root));
}

XmlDocument XmlDocument::create(const std::string_view root_name, const std::string_view value) {
    return XmlDocument(XmlNode::create(root_name, value));
}

void XmlDocument::set_root(XmlNode root) {
    if (!root.valid() || !root.is_element()) throw std::invalid_argument("XML document root must be an element");
    // XmlDocument は情報を失わないDOMの窓口であり、D版互換のXNode
    // と異なり、 children() に通常のテキストノードも含む。
    root_ = XmlNode(root.impl_, true, false);
}

std::string XmlDocument::serialize(const XmlWriterOptions& options) const {
    return root_.serialize(options);
}

}  // namespace cwxeditor::lib

namespace cwxeditor::lib {

class XmlDomParser {
public:
    explicit XmlDomParser(const std::string_view input, const XmlParseOptions& options)
        : input_(input), options_(options) {}

    XmlNode parse_document() {
        skip_bom();
        skip_space();
        // 唯一の文書要素の前には、XML宣言・コメント・処
        // 理命令・文書型宣言を置ける。 文書の付加情報なの
        // で、XNodeのルートには意図的に接続しない。
        while (!empty()) {
            if (starts_with("<?")) {
                const auto pi = parse_pi();
                if (root_seen_) fail("Unexpected processing instruction after the document element");
                if (pi.first != "xml") {
                    // 元のXNodeモデルには文書レベルのその他の
                    // ノードを置く場所がないため、ここでは飛ばす。
                }
                skip_space();
                continue;
            }
            if (starts_with("<!--")) {
                (void)parse_comment();
                skip_space();
                continue;
            }
            if (starts_with("<!DOCTYPE")) {
                skip_doctype();
                skip_space();
                continue;
            }
            break;
        }
        if (empty() || peek() != '<') fail("XML document has no root element");
        auto root = parse_element();
        root_seen_ = true;
        skip_space();
        while (!empty()) {
            if (starts_with("<!--")) {
                (void)parse_comment();
                skip_space();
            } else if (starts_with("<?")) {
                (void)parse_pi();
                skip_space();
            } else {
                fail("Unexpected data after the document element");
            }
        }
        return XmlNode(std::move(root), true);
    }

private:
    struct ParsedPI {
        std::string first;
        std::string second;
    };

    std::string_view input_;
    XmlParseOptions options_;
    std::size_t position_{0};
    std::size_t line_{1};
    std::size_t column_{1};
    bool root_seen_{false};

    [[nodiscard]] bool empty() const noexcept { return position_ >= input_.size(); }
    [[nodiscard]] char peek(const std::size_t offset = 0) const noexcept {
        const auto index = position_ + offset;
        return index < input_.size() ? input_[index] : '\0';
    }
    [[nodiscard]] bool starts_with(const std::string_view text) const noexcept {
        return input_.substr(position_, text.size()) == text;
    }

    [[noreturn]] void fail(const std::string& message) const {
        throw XmlParseError(message, position_, line_, column_);
    }

    void advance() {
        if (empty()) fail("Unexpected end of XML");
        const char value = input_[position_++];
        if (value == '\n' || (value == '\r' && (position_ >= input_.size() || input_[position_] != '\n'))) {
            ++line_;
            column_ = 1;
        } else {
            ++column_;
        }
    }

    void consume(const std::size_t count) {
        for (std::size_t i = 0; i < count; ++i) advance();
    }

    void skip_bom() {
        if (input_.size() >= 3U && static_cast<unsigned char>(input_[0]) == 0xEFU &&
            static_cast<unsigned char>(input_[1]) == 0xBBU && static_cast<unsigned char>(input_[2]) == 0xBFU) {
            consume(3);
        }
    }

    void skip_space() {
        while (!empty() && (peek() == ' ' || peek() == '\t' || peek() == '\n' || peek() == '\r')) advance();
    }

    std::string parse_name() {
        if (empty() || !is_name_start(static_cast<unsigned char>(peek()))) fail("Expected an XML name");
        const auto begin = position_;
        advance();
        while (!empty() && is_name_char(static_cast<unsigned char>(peek()))) advance();
        return std::string(input_.substr(begin, position_ - begin));
    }

    std::string normalize_parsed_character_data(std::string_view raw) const {
        return options_.normalize_newlines ? normalize_newlines(raw) : std::string(raw);
    }

    std::string decode_parsed_value(std::string_view raw) const {
        // 既定ではdxmlのXML改行処理に合わせる。C++側で normalize_newlines
        // を無効にすれば保存済みCRを保持できる。 &#xD; は文
        // 字データなので、参照を復号する前にリテラル入力を正規化する。
        return decode_entities(normalize_parsed_character_data(raw));
    }

    std::string parse_quoted() {
        if (empty() || (peek() != '\'' && peek() != '"')) fail("Expected a quoted XML attribute value");
        const char quote = peek();
        advance();
        const auto begin = position_;
        while (!empty() && peek() != quote) advance();
        if (empty()) fail("Unterminated XML attribute value");
        auto raw = input_.substr(begin, position_ - begin);
        advance();
        try {
            return decode_parsed_value(raw);
        } catch (const XmlValueError& error) {
            fail(error.what());
        }
    }

    ParsedPI parse_pi() {
        if (!starts_with("<?")) fail("Expected processing instruction");
        consume(2);
        const auto target = parse_name();
        const auto body_begin = position_;
        const auto end = input_.find("?>", position_);
        if (end == std::string_view::npos) fail("Unterminated processing instruction");
        while (position_ < end) advance();
        auto body = std::string(input_.substr(body_begin, end - body_begin));
        consume(2);
        if (target == "xml" && body.find("?>") != std::string::npos) fail("Invalid XML declaration");
        return {target, body};
    }

    std::string parse_comment() {
        if (!starts_with("<!--")) fail("Expected XML comment");
        consume(4);
        const auto begin = position_;
        const auto end = input_.find("-->", position_);
        if (end == std::string_view::npos) fail("Unterminated XML comment");
        while (position_ < end) advance();
        const auto value = std::string(input_.substr(begin, end - begin));
        if (value.find("--") != std::string::npos || (!value.empty() && value.back() == '-')) {
            fail("Invalid XML comment");
        }
        consume(3);
        return value;
    }

    std::string parse_cdata() {
        if (!starts_with("<![CDATA[")) fail("Expected CDATA section");
        consume(9);
        const auto begin = position_;
        const auto end = input_.find("]]>", position_);
        if (end == std::string_view::npos) fail("Unterminated CDATA section");
        while (position_ < end) advance();
        const auto value = normalize_parsed_character_data(input_.substr(begin, end - begin));
        consume(3);
        return value;
    }

    void skip_doctype() {
        if (!starts_with("<!DOCTYPE")) fail("Expected doctype");
        consume(9);
        int subset_depth = 0;
        char quote = '\0';
        while (!empty()) {
            const char value = peek();
            if (quote != '\0') {
                advance();
                if (value == quote) quote = '\0';
                continue;
            }
            if (value == '\'' || value == '"') {
                quote = value;
                advance();
            } else if (value == '[') {
                ++subset_depth;
                advance();
            } else if (value == ']') {
                if (subset_depth > 0) --subset_depth;
                advance();
            } else if (value == '>' && subset_depth == 0) {
                advance();
                return;
            } else {
                advance();
            }
        }
        fail("Unterminated doctype");
    }

    std::shared_ptr<XmlNode::Impl> parse_element() {
        if (empty() || peek() != '<') fail("Expected an XML element");
        advance();
        if (!empty() && peek() == '/') fail("Unexpected XML end tag");
        const auto element_name = parse_name();
        auto element = make_impl(XmlNodeType::Element, element_name);
        while (!empty()) {
            skip_space();
            if (starts_with("/>") || peek() == '>') break;
            const auto attribute_name = parse_name();
            skip_space();
            if (empty() || peek() != '=') fail("Expected '=' after XML attribute name");
            advance();
            skip_space();
            auto attribute_value = parse_quoted();
            const auto duplicate = std::find_if(element->attributes.begin(), element->attributes.end(),
                                                [&](const XmlAttribute& attribute) {
                                                    return attribute.name == attribute_name;
                                                });
            if (duplicate != element->attributes.end()) {
                fail("XML attribute is duplicated: " + attribute_name);
            }
            element->attributes.push_back({attribute_name, std::move(attribute_value)});
        }
        if (empty()) fail("Unterminated XML start tag: <" + element_name + ">");
        if (starts_with("/>") ) {
            consume(2);
            return element;
        }
        if (peek() != '>') fail("Expected '>' after XML start tag");
        advance();

        while (!empty()) {
            if (starts_with("</")) {
                consume(2);
                const auto closing_name = parse_name();
                skip_space();
                if (empty() || peek() != '>') fail("Expected '>' after XML end tag");
                advance();
                if (closing_name != element_name) {
                    fail("Mismatched XML end tag: expected </" + element_name + ">, got </" + closing_name + ">");
                }
                return element;
            }
            if (peek() != '<') {
                const auto begin = position_;
                while (!empty() && peek() != '<') advance();
                auto raw = input_.substr(begin, position_ - begin);
                try {
                    auto decoded = decode_parsed_value(raw);
                    // dxmlと同様、XNodeが実体参照を復号する前
                    // に空白だけのイベントを除く。 そのため、参照で符
                    // 号化された空白文字はテキストデータとして残す。
                    if (!raw.empty() &&
                        !(options_.ignore_whitespace_only_text && is_xml_whitespace_only(raw))) {
                        auto text = make_impl(XmlNodeType::Text, {}, decoded);
                        text->parent = element;
                        element->children.push_back(std::move(text));
                    }
                } catch (const XmlValueError& error) {
                    fail(error.what());
                }
                continue;
            }
            if (starts_with("<!--")) {
                auto comment = parse_comment();
                if (options_.preserve_misc) {
                    auto child = make_impl(XmlNodeType::Comment, {}, comment);
                    child->parent = element;
                    element->children.push_back(std::move(child));
                }
                continue;
            }
            if (starts_with("<![CDATA[")) {
                auto cdata = parse_cdata();
                if (options_.preserve_misc) {
                    auto child = make_impl(XmlNodeType::CData, {}, cdata);
                    child->parent = element;
                    element->children.push_back(std::move(child));
                } else if (!cdata.empty()) {
                    // CDATAというノード種類を捨てても、文
                    // 字データは捨てない。 その他のノードを要
                    // 求しない場合は、通常テキストとして扱う。
                    auto child = make_impl(XmlNodeType::Text, {}, cdata);
                    child->parent = element;
                    element->children.push_back(std::move(child));
                }
                continue;
            }
            if (starts_with("<?")) {
                auto pi = parse_pi();
                if (options_.preserve_misc) {
                    auto child = make_impl(XmlNodeType::ProcessingInstruction, pi.first, pi.second);
                    child->parent = element;
                    element->children.push_back(std::move(child));
                }
                continue;
            }
            if (starts_with("<!DOCTYPE")) fail("DOCTYPE is only allowed before the document element");
            if (starts_with("<!")) fail("Unknown XML declaration");
            auto child = parse_element();
            child->parent = element;
            element->children.push_back(std::move(child));
        }
        fail("End tag not found: <" + element_name + ">");
    }
};

XmlDocument XmlDocument::parse(const std::string_view xml, const XmlParseOptions& options) {
    if (xml.empty()) throw XmlParseError("XML document is empty", 0, 1, 1);
    // この境界のXML 1.0入力はUTF-8。 不正な
    // バイト列を受け付けると往復変換不能な文字列になり
    // 、名前・属性へ不正な生バイトが届くため拒否する。
    for (std::size_t index = 0; index < xml.size();) {
        const auto first = static_cast<unsigned char>(xml[index]);
        std::size_t count = 1U;
        std::uint32_t codepoint = first;
        if (first < 0x80U) {
            ++index;
            continue;
        } else if (first >= 0xC2U && first <= 0xDFU) {
            count = 2U; codepoint = first & 0x1FU;
        } else if (first >= 0xE0U && first <= 0xEFU) {
            count = 3U; codepoint = first & 0x0FU;
        } else if (first >= 0xF0U && first <= 0xF4U) {
            count = 4U; codepoint = first & 0x07U;
        } else {
            throw XmlParseError("Invalid UTF-8 sequence", index, 1, index + 1U);
        }
        if (index + count > xml.size())
            throw XmlParseError("Truncated UTF-8 sequence", index, 1, index + 1U);
        for (std::size_t offset = 1U; offset < count; ++offset) {
            const auto next = static_cast<unsigned char>(xml[index + offset]);
            if ((next & 0xC0U) != 0x80U)
                throw XmlParseError("Invalid UTF-8 continuation byte", index + offset,
                                    1, index + offset + 1U);
            codepoint = (codepoint << 6U) | (next & 0x3FU);
        }
        const bool overlong = (count == 2U && codepoint < 0x80U) ||
                              (count == 3U && codepoint < 0x800U) ||
                              (count == 4U && codepoint < 0x10000U);
        if (overlong || (codepoint >= 0xD800U && codepoint <= 0xDFFFU) ||
            codepoint > 0x10FFFFU)
            throw XmlParseError("Invalid UTF-8 code point", index, 1, index + 1U);
        index += count;
    }
    XmlDomParser parser(xml, options);
    return XmlDocument(parser.parse_document());
}

}  // namespace cwxeditor::lib

namespace cwxeditor::lib {

std::string XmlWriter::escape_text(const std::string_view text) {
    std::string output;
    output.reserve(text.size());
    for (const char value : text) {
        switch (value) {
        case '&': output += "&amp;"; break;
        case '<': output += "&lt;"; break;
        case '>': output += "&gt;"; break;
        // 解析側のリテラル改行の方針を通じて、保存されたCRを保持する。
        case '\r': output += "&#xD;"; break;
        default: output.push_back(value); break;
        }
    }
    return output;
}

std::string XmlWriter::escape_attribute(const std::string_view text) {
    std::string output;
    output.reserve(text.size());
    for (const char value : text) {
        switch (value) {
        case '&': output += "&amp;"; break;
        case '<': output += "&lt;"; break;
        case '"': output += "&quot;"; break;
        case '\t': output += "&#x9;"; break;
        case '\n': output += "&#xA;"; break;
        case '\r': output += "&#xD;"; break;
        default: output.push_back(value); break;
        }
    }
    return output;
}

std::string XmlWriter::write(const XmlNode& node) const {
    return write(node, options_);
}

std::string XmlWriter::write(const XmlNode& node, const XmlWriterOptions& options) {
    if (!node.impl_) throw std::invalid_argument("Cannot serialize an invalid XML node");
    std::string output;
    const std::string& line_ending = options.line_ending.empty() ? std::string("\n") : options.line_ending;

    auto append_line_break = [&](const std::size_t depth) {
        if (!options.pretty) return;
        output += line_ending;
        if (!options.indent.empty()) {
            for (std::size_t i = 0; i < depth; ++i) output += options.indent;
        }
    };

    std::function<void(const std::shared_ptr<XmlNode::Impl>&, bool, std::size_t)> emit;
    emit = [&](const std::shared_ptr<XmlNode::Impl>& current, const bool is_root,
               const std::size_t depth) {
        if (!current) return;
        switch (current->type) {
        case XmlNodeType::Text:
            output += escape_text(current->data);
            return;
        case XmlNodeType::CData: {
            // 各 ]]> は、先行する二つの ] の後で区画
            // を閉じ、次の区画を > から始めて分割する。
            // 最初の開始・最後の終了はループ外に置き、終端記
            // 号が連続してもCDATA構文を重複挿入しない。
            output += "<![CDATA[";
            std::size_t begin = 0;
            while (true) {
                const auto end = current->data.find("]]>", begin);
                if (end == std::string::npos) break;
                output.append(current->data, begin, end - begin);
                output += "]]]]><![CDATA[>";
                begin = end + 3;
            }
            output.append(current->data, begin, std::string::npos);
            output += "]]>";
            return;
        }
        case XmlNodeType::Comment:
            if (current->data.find("--") != std::string::npos ||
                (!current->data.empty() && current->data.back() == '-')) {
                throw std::invalid_argument("Invalid XML comment");
            }
            output += "<!--" + current->data + "-->";
            return;
        case XmlNodeType::ProcessingInstruction:
            require_name(current->name);
            if (current->data.find("?>") != std::string::npos) {
                throw std::invalid_argument("Invalid XML processing instruction");
            }
            output += "<?" + current->name;
            if (!current->data.empty()) {
                // 解析済みの処理命令のデータは構文上の区切りを含むため、そのま
                // ま保持する。 プログラムから設定した値には区切りを追加する。
                const auto first = current->data.front();
                if (first != ' ' && first != '\t' && first != '\r' && first != '\n')
                    output.push_back(' ');
                output += current->data;
            }
            output += "?>";
            return;
        case XmlNodeType::Element:
            break;
        }

        require_name(current->name);
        output.push_back('<');
        output += current->name;
        for (const auto& attribute : current->attributes) {
            require_name(attribute.name);
            output.push_back(' ');
            output += attribute.name;
            output += "=\"";
            output += escape_attribute(attribute.value);
            output.push_back('"');
        }
        const bool has_children = !current->children.empty();
        const bool has_value = std::any_of(current->children.begin(), current->children.end(),
                                           [](const std::shared_ptr<XmlNode::Impl>& child) {
                                               return child && child->type == XmlNodeType::Text && !child->data.empty();
                                           });
        if (!has_children && !has_value && options.self_close_empty && !(is_root && options.expand_empty_root)) {
            output += "/>";
            return;
        }
        output.push_back('>');

        // D版の XNode と同様、直接の文字データを、子より前の一つの値として保存する。
        for (const auto& child : current->children)
            if (child && child->type == XmlNodeType::Text) emit(child, false, depth + 1);
        bool has_markup_child = false;
        for (std::size_t index = 0; index < current->children.size(); ++index) {
            const auto& child = current->children[index];
            if (!child) continue;
            if (child->type == XmlNodeType::Text) continue;
            const bool markup = child->type != XmlNodeType::Text;
            if (markup) {
                has_markup_child = true;
                if (options.pretty && !output.empty() && output.back() != '\n') {
                    append_line_break(depth + 1);
                }
            }
            emit(child, false, depth + 1);
        }
        if (options.pretty && has_markup_child && !current->children.empty() &&
            current->children.back() && current->children.back()->type != XmlNodeType::Text &&
            !output.empty() && output.back() != '\n') {
            append_line_break(depth);
        }
        output += "</";
        output += current->name;
        output.push_back('>');
    };

    const bool root = node.root_;
    if (root && options.declaration) output += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>" + line_ending;
    emit(node.impl_, root, 0);
    return output;
}

}  // namespace cwxeditor::lib
