#pragma once

#include "ui_actions.hpp"
#include <chrono>
#include <functional>
#include <memory>
#include <optional>

namespace cwxeditor::lib {

struct ExternalDialogOperation {
    std::function<void()> start;
    std::function<bool()> completed;
    std::function<void()> cancel;
    // ネイティブの実行ループの処理だけを担当する。共通の入力と描画はiterateで進める。
    std::function<void()> platform_poll;
    std::chrono::milliseconds timeout{std::chrono::minutes(10)};
};

struct ModalOperationContext {
    ModalStateMachine& modal;
    WidgetPtr focused;
    std::function<void()> suspend_input;
    std::function<bool()> iterate;
    // アダプターは開始前に所有元のネイティブ資源の世代を取得する。同じモ
    // デルを閉じて再作成した場合も、この呼び出しは以後ずっと無効とする。
    std::function<bool()> live;
    // 表示側との互換性のために残すセッション終了通知。フォー
    // カス先の選択と復元は共通のモーダル実行処理が担当する。
    std::function<void(const ModalStateMachine::Session&)> restore;
    std::function<WidgetPtr()> current_focus;
    std::function<void(const WidgetPtr&)> set_focus;
    std::function<bool(const std::shared_ptr<Window>&)> surface_available;
    // 既存のFocusStateMachineのリビジョン。終了コールバックが同じコントロールのフ
    // ォーカスを一度解除して再設定する場合があり、ポインター比較だけではその意図を検出できない。
    std::function<std::uint64_t()> focus_revision;
};

struct ModalDialogOperation {
    // 表示側はネイティブ描画面の生成・解放と、ネイティブフォーカスの反映を担
    // 当する。共通の実行処理はモーダル制御、完了、後処理の順序を管理する。
    std::function<void()> present;
    std::function<void()> dismiss;
};

// 入れ子のイベントループに入らずにモーダルダイアログを開始する
// 。返したセッションはfinish_modal_dialog_operation()
// が呼ばれるまで有効。初回描画後にブラウザのイベ
// ントループへ制御を戻す必要があるホストで使う。
[[nodiscard]] std::optional<ModalStateMachine::SessionId>
begin_modal_dialog_operation(
    ModalOperationContext context, std::shared_ptr<Dialog> dialog,
    std::shared_ptr<Window> owner, ModalDialogOperation operation);

// 開始済みの非ブロック型モーダルセッションを一つ終了する。操
// 作は一度だけ実行されるため、表示側は終了要求と破棄の両方か
// ら呼べる。公開前に所有元が消失した場合はfalseを返す。
[[nodiscard]] bool finish_modal_dialog_operation(
    ModalOperationContext context, ModalStateMachine::SessionId id,
    ModalDialogOperation operation = {});

[[nodiscard]] DialogResult run_modal_dialog_operation(
    ModalOperationContext context, std::shared_ptr<Dialog> dialog,
    std::shared_ptr<Window> owner, ModalDialogOperation operation);

// 非同期OS選択画面の同期的な窓口。OSやレンダラーのコードは含めない
// 。所有元やホストが消失した場合は完了より取消を優先する。遅れて届くOS
// コールバックは自身の状態を保持し、operation.cancel
// に従う。実行処理はコールバックをまたいでモデル引数を所有する。プロ
// バイダーへの問い合わせと最終的なフォーカス公開の後に完了を再検証し
// 、完了結果の所有元が失われた場合も含め、取消通知は最大一度とする。
[[nodiscard]] bool run_external_dialog_operation(
    ModalOperationContext context, std::shared_ptr<Window> owner,
    ExternalDialogOperation operation);

} // namespace cwxeditor::lib
