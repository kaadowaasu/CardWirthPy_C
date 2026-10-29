#include "text.hpp"

namespace cwxeditor::lib {

std::string normalize_editor_control_newlines(std::string text) {
    // 避けられないネイティブ操作欄の改行規則は、共通テキストアダプターの境
    // 界で扱う。 エディタの全GUI呼び出し元が、この一つのAPIを使う。
#ifdef _WIN32
    std::size_t offset = 0U;
    while ((offset = text.find("\r\n", offset)) != std::string::npos) {
        text.replace(offset, 2U, "\n");
        ++offset;
    }
#endif
    return text;
}

}  // namespace cwxeditor::lib
