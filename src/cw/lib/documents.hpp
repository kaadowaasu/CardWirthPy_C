#pragma once
// ファイル取得・保存だけを担当する。Engine のダイアログは全 OS で一つの共通 Presentation を使う。
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace cw::lib::documents {
enum class Operation { OpenFiles, SaveFile, OpenFolder };
enum class Status { Success, Cancelled, Unsupported, Failed };
enum class LocationKind { NativePath, ProviderUri };
enum class FolderIntent { Read, Write };
struct Filter { std::string label, extensions; };
struct Request {
    Operation operation{Operation::OpenFiles};
    std::string title, initial_location;
    std::vector<Filter> filters;
    bool multiple{};
    std::uint32_t parent_window_id{};
    FolderIntent folder_intent{FolderIntent::Read};
    // 即時の読み取り・取り込みだけで明示的に有効にする。永続的
    // な素材参照を保存する設定や保存先に、一時パスを使わない。
    std::size_t transient_read_limit{};
};
struct Document {
    LocationKind kind{LocationKind::NativePath};
    std::string location;
    // OS アダプターは範囲付きアクセス・準備領域の所有権を付加できる。キュー内の
    // I/O もこれを保持し、パスだけを取り出して利用権を破棄してはならない。
    std::shared_ptr<void> access;
    // プロバイダー経由のディレクトリー出力は準備領域を使う
    // 。利用側が全体を書き終えて成功した後だけ公開する。
    std::function<void(bool,std::function<void(bool,std::string)>)> finalize;
};
struct Result {
    Status status{Status::Failed};
    std::vector<Document> documents;
    std::string error;
};
using Completion=std::function<void(Result)>;
// モデル所有者の UI キューへ送る。false は所有者がすでにないことを示す。
using Dispatch=std::function<bool(std::function<void()>)>;
using Cancelled=std::function<bool()>;
using Backend=std::function<void(const Request&,Completion,Cancelled)>;
using DocumentReader=std::function<std::vector<std::uint8_t>(const Document&,std::size_t,const Cancelled&)>;
// プロバイダー経由の読み取りをワーカー側で準備する。ネイティブパスは変更し
// ない。一時ファイルは Document::access と同じ寿命とし、
// 永続設定には残さない。非 GUI の検査用に読み取り処理を注入できる。
[[nodiscard]] Result prepare_local_read(Result result,std::size_t max_bytes,
    const Cancelled& cancelled,DocumentReader reader={});

class Ticket final {
public:
    enum class Phase { Waiting, Queued, Delivered, Cancelled };
    ~Ticket() { cancel(); }
    Ticket(const Ticket&)=delete;
    Ticket& operator=(const Ticket&)=delete;
    void cancel() noexcept;
    [[nodiscard]] bool pending() const noexcept;
private:
    struct State;
    explicit Ticket(std::shared_ptr<State> state):state_(std::move(state)) {}
    std::shared_ptr<State> state_;
    friend std::shared_ptr<Ticket> request(const Request&,Dispatch,Completion,Backend);
};
// 論理的な取り消しで未開始の要求と遅れたコールバックを抑制する。SDL
// は開いたネイティブ選択画面をプログラムから閉じる機能を公開していない。
[[nodiscard]] std::shared_ptr<Ticket> request(const Request& request,Dispatch dispatch,
    Completion completion,Backend backend={});
using DocumentWriter=std::function<Result(const Document&,const std::vector<std::uint8_t>&,const Cancelled&)>;
// 保存で明示的に選んだ対象だけに書く。取り消しは開く前なら阻止で
// きるが、開いた後は所有者が閉じても書き込み・終了を完了する。
// プロバイダーが不可分な置換や巻き戻しに対応するとは限らない。
[[nodiscard]] std::shared_ptr<Ticket> write(const Document& target,std::vector<std::uint8_t> bytes,
    Dispatch dispatch,Completion completion,DocumentWriter writer={});
// OS のコールバック結果の解析。非 GUI 検査でも使う。nullptr
// はエラー、空の配列は取り消し。URI を黙ってファイルパスへ変換しない。
[[nodiscard]] Result parse_selection(const char* const* paths,const std::string& error);
} // namespace cw::lib::documents
