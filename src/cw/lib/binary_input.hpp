#pragma once

#include "cw/lib/read_stream.hpp"

namespace cw::lib {

// Python のファイル風 read() に対応するネイティブ API。
// Object 自体は変えず、XML などが Value 経由で受け取り
// 、OS 固有のストリーム方針や所有権の取得を追加せずに済むようにする。
class BinaryInput : public Object {
public:
    virtual Bytes read() = 0;
};

// 意図的な言語境界。std::istream は Python の Object で
// はないため、現在位置から EOF まで読む同じ規則へ適合させる。破棄・読み込みで
// 呼び出し元のストリームを閉じない。読み取りエラーは部分データを返さず伝播させる。
class IstreamBinaryInput final : public BinaryInput {
public:
    explicit IstreamBinaryInput(std::istream& input) : input_(input) {}

    Bytes read() override { return read_stream_bytes(input_); }

private:
    std::istream& input_;
};

inline std::optional<Bytes> read_binary_input(const Value& input) {
    // 既存のネイティブ XML の String/Bytes ストリーム拡張を保つ。
    // Python は通常 BinaryIO を受け取るが、ネイティブの型付きデー
    // タはすでに read() の結果。すべての利用側で同じ受け渡しの変換を使う。
    if (const auto* bytes = std::get_if<Bytes>(&input.storage())) return *bytes;
    if (const auto* text = std::get_if<String>(&input.storage()))
        return Bytes(text->begin(), text->end());
    if (const auto* object = std::get_if<ObjectPtr>(&input.storage())) {
        if (const auto stream = std::dynamic_pointer_cast<BinaryInput>(*object))
            return stream->read();
    }
    // 無関係な Object を文字列化しても、read() の機能を持つ
    // とは見なさない。元のファイル名に戻すかどうかは呼び出し側が決める。
    return std::nullopt;
}

} // namespace cw::lib
