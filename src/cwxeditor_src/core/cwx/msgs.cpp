#include "cwxeditor_src/core/cwx/msgs.hpp"
#include "cwxeditor_src/core/cwx/utils.hpp"

namespace cwxeditor::cwx::msgs {

String Msgs::toXML() const {
    return toXML(false);
}

String Msgs::toXML(bool writeAll) const {
    auto node = XmlNode::create("message");
    toNodeImpl(node, writeAll);
    return node.text();
}

Msgs Msgs::fromXML(const String& xml, std::uint64_t dataVersion) {
    try {
        return fromNode(XmlNode::parse(xml), dataVersion);
    } catch (const std::exception& error) {
        ::cwxeditor::cwx::utils::debugln("message catalog XML: ", error.what());
        // Dは失敗時にnullのクラスを返す。このAPIはC++の値を返
        // すため、有効な既定メッセージ一覧に見せかけず、失敗を伝播する
        // 。CPropsのファイルごとの処理で、失敗した一覧を飛ばす。
        throw;
    }
}

String Msgs::defaultSelection(const String& s) const {
    return detail::concat("[", s, "]");
}

}  // namespace cwxeditor::cwx::msgs
