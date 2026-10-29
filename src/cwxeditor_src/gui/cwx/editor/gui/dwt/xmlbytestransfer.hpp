#pragma once

// cwx/editor/gui/dwt/xmlbytestransfer.d
// のC++20移植。 SWTのArrayWrapperByte
// ・ByteArrayTransfer相当の内容
// をlib::ClipboardDataへ保持す
// る。 通常のXML文字列はtext、Editor
// 独自形式はformatsへ入れ、 SWT/DWT
// やOSのクリップボード型は公開しない。

#include "cwxeditor_src/lib/clipboard.hpp"
#include "cwxeditor_src/lib/drag_drop.hpp"
#include "cwxeditor_src/lib/portable.hpp"
#include "cwxeditor_src/lib/text.hpp"
#include "cwxeditor_src/lib/xml.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <string_view>
#include <type_traits>

namespace cwxeditor::cwx::editor::gui::dwt::xmlbytestransfer {

namespace lib = ::cwxeditor::lib;

using Byte = lib::Byte;
using ByteView = lib::ByteView;
using Bytes = lib::Bytes;
using String = lib::String;
using StringView = lib::StringView;
using ClipboardData = lib::ClipboardData;
using ClipboardBackend = lib::ClipboardBackend;

/// Dの形式判定と同じ、XMLの接頭文字列。
inline constexpr StringView XML_HEADER_S = "<?xml ";
inline constexpr std::array<Byte, 6> XML_HEADER{{
    static_cast<Byte>('<'), static_cast<Byte>('?'), static_cast<Byte>('x'),
    static_cast<Byte>('m'), static_cast<Byte>('l'), static_cast<Byte>(' ')}};

/// SWTの登録済み転送名。ClipboardData::formats
/// の正式キーにも使い、 クリップボードとドラッグ
/// の各実装でDの転送形式を識別できるようにする。
inline constexpr StringView XML_BYTES_FORMAT = "cwx.editor.XMLBytes";

/// OSのクリップボード実装が登録名をMIME
/// 形式へ変換する場合に、追加で認める表記。
inline constexpr StringView XML_BYTES_MIME =
    "application/x-cwxeditor-xmlbytes";

/// XMLtoCBが使う唯一の設定。直接渡すか、下のテンプ
/// レート版で xmlCopyまたはvar.etc.xmlCopy
/// を公開する具体的な設定オブジェクトを渡せる。
struct ClipboardOptions {
    bool xmlCopy{};
};
using Props = ClipboardOptions;

/// Dのメモリモードとヘッドレスの代替を持つクリップボードラッパー。
/// バックエンドがあればメモリ以外の操作をすべて委譲する。なければ
/// 最後に書いたClipboardDataを保持し、 OSなしでも
/// コピー・貼り付けと単体テストで一定の動作を得られるようにする。
class ClipData final {
public:
    explicit ClipData(std::shared_ptr<ClipboardBackend> clipboard = {});
    explicit ClipData(ClipboardBackend& clipboard) noexcept;

    ClipData(const ClipData&) = default;
    ClipData& operator=(const ClipData&) = default;
    ClipData(ClipData&&) noexcept = default;
    ClipData& operator=(ClipData&&) noexcept = default;
    ~ClipData() = default;

    [[nodiscard]] ClipboardBackend* backend() const noexcept { return backend_; }
    [[nodiscard]] const std::shared_ptr<ClipboardBackend>& backendOwner() const
        noexcept {
        return owner_;
    }

    [[nodiscard]] bool memoryMode() const noexcept { return memory_mode_; }
    void memoryMode(bool mode) noexcept;

    /// OSのクリップボードの詳細を公開せず、ヘッドレスか
    /// らメモリ内の表現を確認・初期設定するアクセサー。
    [[nodiscard]] const String& memory() const noexcept { return memory_; }
    void memory(String value);

    /// Clipboard.setContents/getContentsに対応する共通操作。
    void setContents(const ClipboardData& data);
    [[nodiscard]] ClipboardData getContents() const;

private:
    std::shared_ptr<ClipboardBackend> owner_;
    ClipboardBackend* backend_{};
    ClipboardData fallback_;
    String memory_;
    bool memory_mode_{};
};

/// クリップボードの文字列が、DWT転送と同じ
/// XML宣言・接頭辞を持つ場合にtrueを返
/// す。 Dと同じく接頭辞だけを検査し、XML
/// の解析は期待する要素を知る利用側へ任せる。
[[nodiscard]] bool isXMLText(StringView text) noexcept;
[[nodiscard]] bool isXMLBytes(ByteView bytes) noexcept;
[[nodiscard]] bool isXMLBytes(const Bytes& bytes) noexcept;
[[nodiscard]] bool isXMLBytes(const ClipboardData& data) noexcept;

/// UTF-8のXMLバイト列を、D互換の独自転送データへ格納する。
[[nodiscard]] Bytes bytesFromXML(StringView xml);
[[nodiscard]] String bytesToXML(ByteView bytes);
[[nodiscard]] String bytesToXML(const Bytes& bytes);

/// xmlCopyに応じて、メモリ領域・通常のクリップボ
/// ード文字列・独自バイト転送のいずれかへXMLを書く。
void XMLtoCB(const ClipboardOptions& options, ClipData& clipboard,
             StringView xml);
void XMLtoCB(bool xmlCopy, ClipData& clipboard, StringView xml);

/// 通常の共通文字列転送の表現を作る。ClipboardDataはLFへ正
/// 規化し、 OSの形式への変換は実際のクリップボードのアダプターが行う。
[[nodiscard]] ClipboardData makeTextPayload(StringView xml);
[[nodiscard]] bool setTextPayload(ClipboardData& data, StringView xml);

/// 有効なXMLBytesTransferを、その場で通常の
/// 文字列へ変換する。MainWindow.clipboardToXML()
/// 相当。 独自バイト列を復号し、独自形式を除いて、物
/// 理クリップボードへ公開できる状態にする。 有効な独
/// 自XMLがなければ、内容を変えずfalseを返す。
[[nodiscard]] bool convertXMLBytesToText(ClipboardData& data) noexcept;

namespace detail {

template <typename T>
[[nodiscard]] bool xmlCopySetting(const T& value) {
    if constexpr (requires { value.xmlCopy; }) {
        return static_cast<bool>(value.xmlCopy);
    } else if constexpr (requires { value.var.etc.xmlCopy; }) {
        return static_cast<bool>(value.var.etc.xmlCopy);
    } else if constexpr (requires { value.var().etc.xmlCopy; }) {
        return static_cast<bool>(value.var().etc.xmlCopy);
    } else if constexpr (requires { value.var().xmlCopy; }) {
        // C++のdprops::Propsは、Dのvar.etcと異なりFlexEtcPropsを直接公開する。
        return static_cast<bool>(value.var().xmlCopy);
    } else {
        return false;
    }
}

}  // namespace detail

template <typename PropsLike>
    requires (!std::is_same_v<std::remove_cvref_t<PropsLike>, ClipboardOptions> &&
              !std::is_same_v<std::remove_cvref_t<PropsLike>, bool>)
void XMLtoCB(const PropsLike& prop, ClipData& clipboard, StringView xml) {
    XMLtoCB(detail::xmlCopySetting(prop), clipboard, xml);
}

[[nodiscard]] String CBtoXML(const ClipData& clipboard);
[[nodiscard]] String CBtoXML(const ClipboardData& clipboard);
[[nodiscard]] String CBtoXML(const ClipboardBackend& clipboard);

[[nodiscard]] bool CBisXML(const ClipData& clipboard) noexcept;
[[nodiscard]] bool CBisXML(const ClipboardData& clipboard) noexcept;
[[nodiscard]] bool CBisXML(const ClipboardBackend& clipboard) noexcept;
[[nodiscard]] bool CBisXMLOnly(const ClipData& clipboard) noexcept;
[[nodiscard]] bool CBisXMLOnly(const ClipboardData& clipboard) noexcept;
[[nodiscard]] bool CBisXMLOnly(const ClipboardBackend& clipboard) noexcept;

/// 共通のドラッグ・クリップボード内容を構築・検査する。 SWTのDragEvent.data
/// でArrayWrapperByteを渡す操作に対応する。
[[nodiscard]] ClipboardData makeXMLPayload(StringView xml);
[[nodiscard]] bool setXMLPayload(ClipboardData& data, StringView xml);
[[nodiscard]] bool extractXML(const ClipboardData& data, String& xml);
[[nodiscard]] bool extractXML(const lib::DragEvent& event, String& xml);

/// 登録済み独自バイト転送の記述子。SWTのregisterType
/// をプロセス内の数値IDで置き換える。 固定の名前を、
/// すべての共通バックエンドの相互運用キーとして保つ。
class XMLBytesTransfer final {
public:
    static constexpr std::int32_t TYPE_ID = 0x43575801;
    static constexpr StringView TYPE_NAME = XML_BYTES_FORMAT;

    XMLBytesTransfer(const XMLBytesTransfer&) = delete;
    XMLBytesTransfer& operator=(const XMLBytesTransfer&) = delete;

    [[nodiscard]] static const XMLBytesTransfer& getInstance() noexcept;

    [[nodiscard]] std::int32_t typeId() const noexcept { return TYPE_ID; }
    [[nodiscard]] StringView typeName() const noexcept { return TYPE_NAME; }
    [[nodiscard]] lib::Array<std::int32_t> getTypeIds() const;
    [[nodiscard]] lib::Array<String> getTypeNames() const;
    [[nodiscard]] bool isSupportedType(std::int32_t id) const noexcept;
    [[nodiscard]] bool isSupportedType(StringView name) const noexcept;

    void put(ClipboardData& data, ByteView bytes) const;
    [[nodiscard]] bool get(const ClipboardData& data, Bytes& bytes) const;

private:
    XMLBytesTransfer() = default;
};

}  // namespace cwxeditor::cwx::editor::gui::dwt::xmlbytestransfer
