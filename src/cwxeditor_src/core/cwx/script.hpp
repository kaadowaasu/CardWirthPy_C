#pragma once

// cwx/script.dのC++20移
// 植。スクリプトコンパイラーは生成済みの
// D互換層に依存しない。 イベント・コン
// テントは情報を失わないScriptContent
// で表し、event::Content
// の構築に 必要な情報をすべて保持す
// る。未知の属性や子ブロックも残す。

#include "cwxeditor_src/core/cwx/background.hpp"
#include "cwxeditor_src/core/cwx/motion.hpp"
#include "cwxeditor_src/core/cwx/types.hpp"
#include "cwxeditor_src/lib/portable.hpp"
#include "cwxeditor_src/lib/real80.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <source_location>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cwxeditor::cwx::props {
class CProps;
}
namespace cwxeditor::cwx::summary {
class Summary;
}
namespace cwxeditor::cwx::card { class CardImage; }
namespace cwxeditor::cwx::event { class SDialog; }
namespace cwxeditor::cwx::coupon { class Coupon; }

namespace cwxeditor::cwx::script {

using String = ::cwxeditor::lib::String;
using StringView = ::cwxeditor::lib::StringView;
using Props = ::cwxeditor::cwx::props::CProps;
using Summary = ::cwxeditor::cwx::summary::Summary;
using CType = ::cwxeditor::cwx::types::CType;
using CArg = ::cwxeditor::cwx::types::CArg;

/// スクリプトの字句解析・構文解析・評価中に発生するエラー。
struct CWXSError {
    String message;
    std::size_t errLine = 0;
    std::size_t errPos = 0;
    String file;
    std::size_t line = 0;
};

class CWXScriptException final : public std::runtime_error {
public:
    CWXScriptException(String file, std::size_t line, String text,
                       std::vector<CWXSError> errors, bool over100 = false);
    explicit CWXScriptException(String text, std::vector<CWXSError> errors,
                                bool over100 = false);

    [[nodiscard]] const String& text() const noexcept { return text_; }
    [[nodiscard]] const std::vector<CWXSError>& errors() const noexcept {
        return errors_;
    }
    [[nodiscard]] bool over100() const noexcept { return over100_; }
    [[nodiscard]] const String& sourceFile() const noexcept { return file_; }
    [[nodiscard]] std::size_t sourceLine() const noexcept { return line_; }

private:
    String text_;
    std::vector<CWXSError> errors_;
    bool over100_ = false;
    String file_;
    std::size_t line_{};
};

struct VarSet {
    String var;
    String value;
};

struct CompileOption {
    bool linkId = false;
    std::ptrdiff_t startLine = 0;
    std::ptrdiff_t startPos = 0;
    std::ptrdiff_t addLines = 0;
};

/// 構文に依存しないコンテントの記録。属性・項目・子の情
/// 報を失わず保持し、 イベント変換側がソースを再解析
/// せずに型付きのメンバーへ対応付けられるようにする。
struct ScriptContent {
    CType type = CType::Start;
    String keyword;
    String name;
    String comment;
    String condition;
    std::vector<String> arguments;
    std::vector<CArg> argumentKinds;
    std::vector<std::pair<String, String>> attributes;
    std::vector<String> cardImages;
    std::vector<String> motionValues;
    std::vector<String> backgroundValues;
    std::vector<String> areaValues;
    // 準備済みのD形式の型付き引数。GUIは情報が欠け
    // る上の文字列表現を再解析せず、これらを直接使う。
    std::optional<lib::Array<lib::Owner<card::CardImage>>> typedCardImages;
    std::optional<lib::Array<lib::Owner<event::SDialog>>> dialogs;
    std::optional<lib::Array<lib::Owner<coupon::Coupon>>> coupons;
    std::optional<lib::Array<String>> keyCodes;
    std::optional<lib::Array<String>> couponNames;
    std::optional<lib::Array<types::CastRange>> castRanges;
    // コンパイラーの引数が省略された場合も存在する。 構築時の既定
    // 値を保つ必要がある、従来の直接作成の記録だけは値を持たない。
    std::optional<lib::Array<types::EffectCardType>> effectCardTypes;
    ::cwxeditor::lib::Array<
        ::cwxeditor::cwx::background::BgImagePtr> backgrounds;
    ::cwxeditor::lib::Array<
        ::cwxeditor::lib::Owner<const ::cwxeditor::cwx::motion::Motion>> motions;
    std::vector<ScriptContent> children;
    std::size_t sourceLine = 0;
    std::size_t sourcePos = 0;
    bool linkCardIds = false;
};

using Content = ScriptContent;
using Contents = std::vector<Content>;

/// DSLの効果モーションを保存する共通処理。コンテキスト付きの書き出しではSummary
/// から召喚獣IDを解決し、 コンテキストがない呼び出しではD版と同じく0を使う。
[[nodiscard]] String motionToScript(const ::cwxeditor::cwx::motion::Motion& value,
                                    StringView beastId = "0");

[[nodiscard]] Contents compile(const Props* prop, const Summary* summ,
                               StringView script,
                               const CompileOption& opt = {});
[[nodiscard]] inline Contents compile(const Props& prop, const Summary& summ,
                                      StringView script,
                                      const CompileOption& opt = {}) {
    return compile(&prop, &summ, script, opt);
}

[[nodiscard]] CType firstContentType(const Props* prop, const Summary* summ,
                                     StringView script);
[[nodiscard]] inline CType firstContentType(const Props& prop,
                                            const Summary& summ,
                                            StringView script) {
    return firstContentType(&prop, &summ, script);
}

class CWXScript final {
    friend struct ScriptTestAccessor;
public:
    enum class Kind {
        START,
        IF,
        FI,
        ELIF,
        SIF,
        O_BRA,
        C_BRA,
        SYMBOL,
        NUMBER,
        VAR_NAME,
        EQ,
        COMMA,
        STRING,
        PLU,
        MIN,
        MUL,
        DIV,
        RES,
        CAT,
        O_PAR,
        C_PAR,
        COMMENT,
    };

    struct Token {
        std::size_t line = 0;
        std::size_t pos = 0;
        std::size_t index = 0;
        Kind kind = Kind::SYMBOL;
        String value;
        String comment;

        [[nodiscard]] String toString() const;
        [[nodiscard]] bool opEquals(const Token& other) const noexcept;
        [[nodiscard]] bool operator==(const Token& other) const noexcept {
            return opEquals(other);
        }
        [[nodiscard]] bool operator!=(const Token& other) const noexcept {
            return !opEquals(other);
        }
    };

    enum class CRKind { STR, INT, REAL };

    class CalcResult {
    public:
        CRKind kind = CRKind::INT;
        String str;
        std::int64_t numInt = 0;
        lib::Real80 numReal;

        CalcResult() = default;
        explicit CalcResult(std::int64_t value) : kind(CRKind::INT), numInt(value) {}
        explicit CalcResult(double value) : kind(CRKind::REAL), numReal(value) {}
        explicit CalcResult(lib::Real80 value) : kind(CRKind::REAL), numReal(value) {}
        explicit CalcResult(String value)
            : kind(CRKind::STR), str(std::move(value)) {}

        void cat(const Props* prop, const Token& tok, const CalcResult& rhs);
        void add(const Props* prop, const Token& tok, const CalcResult& rhs);
        void min(const Props* prop, const Token& tok, const CalcResult& rhs);
        void mul(const Props* prop, const Token& tok, const CalcResult& rhs);
        void div(const Props* prop, const Token& tok, const CalcResult& rhs);
        void res(const Props* prop, const Token& tok, const CalcResult& rhs);
        [[nodiscard]] bool opEquals(const CalcResult& value) const noexcept;
        [[nodiscard]] bool opEquals(std::int64_t value) const noexcept;
        [[nodiscard]] bool opEquals(double value) const noexcept;
        [[nodiscard]] bool opEquals(lib::Real80 value) const noexcept;
        [[nodiscard]] bool opEquals(StringView value) const noexcept;
        [[nodiscard]] String toString() const;
    };

    enum class NodeType { VAR_SET, START, COMMAND, ARRAY, VALUES, VALUE };

    struct Node {
        NodeType type = NodeType::VALUE;
        Token token;
        std::vector<Node> texts;
        std::vector<Node> attr;
        std::vector<Node> childs;
        bool nextIsChild = false;
        std::vector<Token> calc;
        std::vector<Node> beforeVars;

        [[nodiscard]] std::vector<Node>& values() noexcept { return childs; }
        [[nodiscard]] const std::vector<Node>& values() const noexcept {
            return childs;
        }
        [[nodiscard]] std::vector<Token>& var() noexcept { return calc; }
        [[nodiscard]] const std::vector<Token>& var() const noexcept {
            return calc;
        }
        [[nodiscard]] std::vector<Node>& value() noexcept { return texts; }
        [[nodiscard]] const std::vector<Node>& value() const noexcept {
            return texts;
        }

        [[nodiscard]] bool opEquals(const Node& other) const noexcept;
        [[nodiscard]] bool operator==(const Node& other) const noexcept {
            return opEquals(other);
        }
        [[nodiscard]] bool operator!=(const Node& other) const noexcept {
            return !opEquals(other);
        }
        [[nodiscard]] Node dup() const;
        [[nodiscard]] String toString() const;
        [[nodiscard]] static String code(StringView indent,
                                         const std::vector<Node>& nodes);
    };

    using NodeTable = std::unordered_map<String, std::vector<Node>>;

    struct Symbol {
        String symbol;
    };

    class VarTable {
    public:
        bool useVar = true;
        bool useCenter = true;

        [[nodiscard]] Symbol id(String value, std::uint64_t id);
        [[nodiscard]] Symbol id(StringView category, StringView name, std::uint64_t id);
        [[nodiscard]] std::vector<String> vars() const;
        void clear();

    private:
        std::unordered_map<String, std::uint64_t> ids_;
        std::unordered_map<String, String> identities_;
        std::unordered_map<String, String> categories_;
    };

    explicit CWXScript(const Props* prop = nullptr, const Summary* summ = nullptr,
                       String text = {}, std::size_t maxError = 100,
                       std::size_t autoWrap = 250);
    CWXScript(const Props& prop, const Summary& summ, String text = {},
              std::size_t maxError = 100, std::size_t autoWrap = 250)
        : CWXScript(&prop, &summ, std::move(text), maxError, autoWrap) {}

    [[nodiscard]] const std::vector<CWXSError>& errors() const noexcept {
        return errors_;
    }
    [[nodiscard]] static String createString(StringView value,
                                              bool useSingleQuote = false);

    [[nodiscard]] std::vector<String> eatEmptyVars(String& text,
                                                    CompileOption& opt);
    [[nodiscard]] static String pushVars(String text,
                                         const std::vector<VarSet>& variables,
                                         CompileOption& opt);
    [[nodiscard]] std::vector<Token> tokenize(StringView text,
                                              const CompileOption& opt = {});
    [[nodiscard]] std::vector<Node> analyzeSyntax(
        const std::vector<Token>& tokens);
    [[nodiscard]] Contents analyzeSemantics(const std::vector<Node>& nodes,
                                             const CompileOption& opt = {});

    [[nodiscard]] CalcResult calc(const std::vector<Token>& tokens,
                                  std::size_t& index, const NodeTable& vars,
                                  std::size_t strWidth = 0);

    [[nodiscard]] std::vector<String> toScript(const Contents& contents,
                                               StringView evtChildOK = {},
                                               bool legacy = false,
                                               StringView indent = "\t") const;
    [[nodiscard]] String toScriptText(const Contents& contents,
                                      StringView evtChildOK = {},
                                      bool legacy = false,
                                      StringView indent = "\t") const;

    [[nodiscard]] const Props* prop() const noexcept { return prop_; }
    [[nodiscard]] const Summary* summary() const noexcept { return summ_; }

private:
    const Props* prop_ = nullptr;
    const Summary* summ_ = nullptr;
    String text_;
    std::size_t maxError_ = 100;
    std::size_t autoWrap_ = 250;
    std::vector<CWXSError> errors_;
    // C++のスクリプト書き出しは、実際の型付き読み込みで得た値を再利用
    // する。 ソース表記を整えるだけのために、パーサーを再実行しない。
    std::function<void(const Node&, const String&, bool, std::size_t)> attribute_read_;

    void throwError(String message, const Token& token,
                    std::source_location origin = std::source_location::current());
    void throwError(String message, std::size_t line, std::size_t pos,
                    StringView value,
                    std::source_location origin = std::source_location::current());
    [[nodiscard]] static std::vector<String> wrap(StringView line,
                                                   std::size_t width);
    [[nodiscard]] std::size_t stringCenter(const std::vector<String>& lines,
                                           std::size_t width) const;
    [[nodiscard]] String stringValue(const Token& token,
                                     std::size_t width = 0);
    [[nodiscard]] std::vector<Token> tokenizeImpl(String& text,
                                                   bool eatEmptyVarMode,
                                                   const CompileOption& opt);
    [[nodiscard]] CalcResult calcNum(const std::vector<Token>& tokens,
                                     std::size_t& index, const NodeTable& vars,
                                     std::size_t strWidth);
    [[nodiscard]] CalcResult calcPar(const std::vector<Token>& tokens,
                                     std::size_t& index, const NodeTable& vars,
                                     std::size_t strWidth);
    [[nodiscard]] CalcResult calcImpl(std::size_t level,
                                      const std::vector<Token>& tokens,
                                      std::size_t& index, const NodeTable& vars,
                                      std::size_t strWidth);
    [[nodiscard]] std::vector<Node> analyzeSyntaxBranch(
        const std::vector<Token>& tokens, std::size_t& index,
        std::vector<Node>& vars);
    [[nodiscard]] std::vector<Node> eatVarSet(const std::vector<Token>& tokens,
                                              std::size_t& index);
    [[nodiscard]] std::vector<Node> analyzeSyntaxStatement(
        const std::vector<Token>& tokens, std::size_t& index,
        std::vector<Node>& vars);
    [[nodiscard]] std::vector<Node> analyzeSyntaxAttr(
        const std::vector<Token>& tokens, std::size_t& index);
    [[nodiscard]] Node analyzeSyntaxBrackets(const std::vector<Token>& tokens,
                                             std::size_t& index);
    [[nodiscard]] Node analyzeSyntaxVar(const std::vector<Token>& tokens,
                                        std::size_t& index);
    [[nodiscard]] std::vector<Token> analyzeSyntaxValue(
        const std::vector<Token>& tokens, std::size_t& index);
    [[nodiscard]] String attrValue(const Node& node, const NodeTable& vars,
                                   std::size_t strWidth);
    [[nodiscard]] std::vector<Node> varValue(const Node& node,
                                              const std::vector<Node>& values,
                                              const NodeTable& vars,
                                              std::size_t strWidth);
    [[nodiscard]] std::vector<Node> var(const std::vector<Node>& nodes,
                                        const NodeTable& vars);
    [[nodiscard]] std::vector<Node> var(const Node& node,
                                        const NodeTable& vars);
    [[nodiscard]] Token var(const Token& token, const NodeTable& vars);
    [[nodiscard]] Content makeContent(const Node& node, const NodeTable& vars,
                                      const CompileOption& opt,
                                      std::size_t depth);
    [[nodiscard]] String nodeValue(const Node& node, const NodeTable& vars,
                                   std::size_t width);
    void collectChildren(const Node& node, Content& output,
                         const NodeTable& vars, const CompileOption& opt,
                         std::size_t depth);
    [[nodiscard]] Contents analyzeSemanticsImpl(
        const std::vector<Node>& nodes, NodeTable& vars,
        const CompileOption& opt, std::size_t depth,
        std::vector<String>& startNames);
};

[[nodiscard]] bool validVarName(StringView name);

struct CWXScriptKeyword {
    String keyword;
    CType type = CType::Start;
    String name;
    String category; // Dの翻訳済みの型名。CTypeはコマンドAPIとの互換用に残す。
};

[[nodiscard]] std::vector<CWXScriptKeyword> keywordInfos(const Props* prop,
    const std::unordered_map<types::Element, structs::ElementOverride>& overrides = {});
[[nodiscard]] const std::vector<CWXScriptKeyword>& keywordInfos();

}  // namespace cwxeditor::cwx::script
