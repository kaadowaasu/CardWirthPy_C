#include "cwxeditor_src/gui/cwx/editor/gui/dwt/xmlbytestransfer.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>

namespace cwxeditor::cwx::editor::gui::dwt::xmlbytestransfer {

namespace {

ClipboardData readXMLFormats(const ClipboardBackend& clipboard, bool include_text) {
    const std::array<StringView,4> requested{XML_BYTES_FORMAT, XML_BYTES_MIME,
        "text/plain;charset=utf-8", "text/plain"};
    return clipboard.read_formats(std::span<const StringView>(requested.data(), include_text ? 4U : 2U));
}

[[nodiscard]] bool startsWith(StringView value, StringView prefix) noexcept {
    return value.size() >= prefix.size() &&
           value.compare(0, prefix.size(), prefix) == 0;
}

[[nodiscard]] bool startsWith(ByteView value, StringView prefix) noexcept {
    if (value.size() < prefix.size()) return false;
    for (std::size_t i = 0; i < prefix.size(); ++i) {
        if (value[i] != static_cast<Byte>(
                            static_cast<unsigned char>(prefix[i]))) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool startsWithStorage(const std::vector<std::byte>& value,
                                     StringView prefix) noexcept {
    if (value.size() < prefix.size()) return false;
    for (std::size_t i = 0; i < prefix.size(); ++i) {
        if (std::to_integer<unsigned int>(value[i]) !=
            static_cast<unsigned int>(
                static_cast<unsigned char>(prefix[i]))) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::vector<std::byte> toStorage(ByteView bytes) {
    std::vector<std::byte> result;
    result.reserve(bytes.size());
    for (const auto value : bytes) {
        result.push_back(static_cast<std::byte>(value));
    }
    return result;
}

[[nodiscard]] Bytes fromStorage(const std::vector<std::byte>& bytes) {
    Bytes result;
    result.reserve(bytes.size());
    for (const auto value : bytes) {
        result.push_back(static_cast<Byte>(std::to_integer<unsigned int>(value)));
    }
    return result;
}

[[nodiscard]] const std::vector<std::byte>* findXMLFormat(
    const ClipboardData& data) noexcept {
    auto found = data.formats.find(String(XML_BYTES_FORMAT));
    if (found != data.formats.end() &&
        startsWithStorage(found->second, XML_HEADER_S)) {
        return &found->second;
    }
    found = data.formats.find(String(XML_BYTES_MIME));
    if (found != data.formats.end() &&
        startsWithStorage(found->second, XML_HEADER_S)) {
        return &found->second;
    }
    return nullptr;
}

}  // namespace

ClipData::ClipData(std::shared_ptr<ClipboardBackend> clipboard)
    : owner_(std::move(clipboard)), backend_(owner_.get()) {}

ClipData::ClipData(ClipboardBackend& clipboard) noexcept : backend_(&clipboard) {}

void ClipData::memoryMode(const bool mode) noexcept {
    memory_mode_ = mode;
    if (!mode) memory_.clear();
}

void ClipData::memory(String value) { memory_ = std::move(value); }

void ClipData::setContents(const ClipboardData& data) {
    if (backend_) {
        backend_->write(data);
    } else {
        fallback_ = data;
    }
}

ClipboardData ClipData::getContents() const {
    if (backend_) return readXMLFormats(*backend_, true);
    return fallback_;
}

bool isXMLText(const StringView text) noexcept {
    return startsWith(text, XML_HEADER_S);
}

bool isXMLBytes(const ByteView bytes) noexcept {
    return startsWith(bytes, XML_HEADER_S);
}

bool isXMLBytes(const Bytes& bytes) noexcept {
    return isXMLBytes(ByteView(bytes));
}

bool isXMLBytes(const ClipboardData& data) noexcept {
    const auto* bytes = findXMLFormat(data);
    if (!bytes) return false;
    return startsWithStorage(*bytes, XML_HEADER_S);
}

Bytes bytesFromXML(const StringView xml) {
    Bytes result;
    result.reserve(xml.size());
    for (const char value : xml) {
        result.push_back(static_cast<Byte>(static_cast<unsigned char>(value)));
    }
    return result;
}

String bytesToXML(const ByteView bytes) {
    if (!isXMLBytes(bytes)) {
        throw std::invalid_argument("Clipboard payload is not XML bytes");
    }
    String result;
    result.reserve(bytes.size());
    for (const auto value : bytes) result.push_back(static_cast<char>(value));
    return result;
}

String bytesToXML(const Bytes& bytes) { return bytesToXML(ByteView(bytes)); }

void XMLtoCB(const ClipboardOptions& options, ClipData& clipboard,
             const StringView xml) {
    if (clipboard.memoryMode()) {
        clipboard.memory(String(xml));
        return;
    }

    if (options.xmlCopy) {
        clipboard.setContents(makeTextPayload(xml));
        return;
    }

    clipboard.setContents(makeXMLPayload(xml));
}

void XMLtoCB(const bool xmlCopy, ClipData& clipboard, const StringView xml) {
    XMLtoCB(ClipboardOptions{xmlCopy}, clipboard, xml);
}

ClipboardData makeTextPayload(const StringView xml) {
    ClipboardData result;
    (void)setTextPayload(result, xml);
    return result;
}

bool setTextPayload(ClipboardData& data, const StringView xml) {
    if (!isXMLText(xml)) return false;
    ClipboardData replacement;
    replacement.text = String(xml);
    data = std::move(replacement);
    return true;
}

bool convertXMLBytesToText(ClipboardData& data) noexcept {
    try {
        const auto* storage = findXMLFormat(data);
        if (storage == nullptr) return false;
        const auto bytes = fromStorage(*storage);
        if (!isXMLBytes(bytes)) return false;
        const auto text = bytesToXML(bytes);
        data = {};
        data.text = std::move(text);
        return true;
    } catch (...) {
        return false;
    }
}

String CBtoXML(const ClipboardData& clipboard) {
    const auto* storage = findXMLFormat(clipboard);
    if (storage) {
        const auto bytes = fromStorage(*storage);
        if (isXMLBytes(bytes)) return bytesToXML(bytes);
    }

    if (isXMLText(clipboard.text)) {
        return clipboard.text;
    }
    return {};
}

String CBtoXML(const ClipData& clipboard) {
    if (clipboard.memoryMode()) return clipboard.memory();
    return CBtoXML(clipboard.getContents());
}

String CBtoXML(const ClipboardBackend& clipboard) {
    return CBtoXML(readXMLFormats(clipboard, true));
}

bool CBisXML(const ClipboardData& clipboard) noexcept {
    return CBisXMLOnly(clipboard) || isXMLText(clipboard.text);
}

bool CBisXML(const ClipData& clipboard) noexcept {
    try {
        return CBisXML(clipboard.getContents());
    } catch (...) {
        return false;
    }
}

bool CBisXML(const ClipboardBackend& clipboard) noexcept {
    try {
        return CBisXMLOnly(clipboard) || isXMLText(clipboard.read_text());
    } catch (...) {
        return false;
    }
}

bool CBisXMLOnly(const ClipboardData& clipboard) noexcept {
    return isXMLBytes(clipboard);
}

bool CBisXMLOnly(const ClipData& clipboard) noexcept {
    try {
        if (clipboard.backend()) return CBisXMLOnly(*clipboard.backend());
        return CBisXMLOnly(clipboard.getContents());
    } catch (...) {
        return false;
    }
}

bool CBisXMLOnly(const ClipboardBackend& clipboard) noexcept {
    try {
        return CBisXMLOnly(readXMLFormats(clipboard, false));
    } catch (...) {
        return false;
    }
}

ClipboardData makeXMLPayload(const StringView xml) {
    ClipboardData result;
    (void)setXMLPayload(result, xml);
    return result;
}

bool setXMLPayload(ClipboardData& data, const StringView xml) {
    if (!isXMLText(xml)) return false;
    ClipboardData replacement;
    XMLBytesTransfer::getInstance().put(replacement, ByteView(bytesFromXML(xml)));
    data = std::move(replacement);
    return true;
}

bool extractXML(const ClipboardData& data, String& xml) {
    xml.clear();
    if (!CBisXML(data)) return false;
    xml = CBtoXML(data);
    return !xml.empty();
}

bool extractXML(const lib::DragEvent& event, String& xml) {
    return extractXML(event.data, xml);
}

const XMLBytesTransfer& XMLBytesTransfer::getInstance() noexcept {
    static const XMLBytesTransfer instance;
    return instance;
}

lib::Array<std::int32_t> XMLBytesTransfer::getTypeIds() const {
    return {TYPE_ID};
}

lib::Array<String> XMLBytesTransfer::getTypeNames() const {
    return {String(TYPE_NAME)};
}

bool XMLBytesTransfer::isSupportedType(const std::int32_t id) const noexcept {
    return id == TYPE_ID;
}

bool XMLBytesTransfer::isSupportedType(const StringView name) const noexcept {
    return name == TYPE_NAME || name == XML_BYTES_MIME;
}

void XMLBytesTransfer::put(ClipboardData& data, const ByteView bytes) const {
    data.formats[String(TYPE_NAME)] = toStorage(bytes);
}

bool XMLBytesTransfer::get(const ClipboardData& data, Bytes& bytes) const {
    const auto* storage = findXMLFormat(data);
    if (!storage) {
        bytes.clear();
        return false;
    }
    bytes = fromStorage(*storage);
    if (isXMLBytes(bytes)) return true;
    bytes.clear();
    return false;
}

}  // namespace cwxeditor::cwx::editor::gui::dwt::xmlbytestransfer
