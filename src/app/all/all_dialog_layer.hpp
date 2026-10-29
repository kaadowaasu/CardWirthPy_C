#pragma once

#include "app/all/all_layout.hpp"

#include <cstdint>
#include <memory>
#include <optional>

namespace cardwirth::app::all {

// Windows版ALLでは、Engineの既存ダイアログを枠なし
// の子ウィンドウとして表示する。 サイズ変更後の表示領域と配置は
// 共通のALLレイアウトで決め、ホストを一つに保つ。 OS標準の
// ファイル選択ダイアログは独立した最上位ウィンドウのままにする。
class AllDialogLayer final {
public:
    AllDialogLayer();
    ~AllDialogLayer();
    AllDialogLayer(const AllDialogLayer&) = delete;
    AllDialogLayer& operator=(const AllDialogLayer&) = delete;

    void update(std::uintptr_t native_host_window, Rect engine_viewport,
                Rect debugger_viewport = {}, float engine_scale = 1.0F);
    void clear() noexcept;
    [[nodiscard]] bool native_engine_dialog_active() const noexcept;
    // ALLがネイティブの枠を外す前に記録した論理サイズ。 次のレイアウ
    // ト更新でこのサイズを使い、ネイティブとホスト内描画のダイアログに
    // 共通のEngine/Debug表示領域への収まり判定を適用する。
    [[nodiscard]] std::optional<Size>
    native_engine_dialog_intrinsic_size() const noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace cardwirth::app::all
