#pragma once

// CardWirthPy_C 固有で CardWirthPy に
// はない、PC の技能・召喚獣・個人カードの順序編集。宿やシナリ
// オ中のキャンプで、ポケットに 11 枚以上あるときに「順番入
// 替」から開く。最大 50 枚を 5 行×10 列で表示し、ド
// ラッグで並べ替える。OK で適用、Cancel で破棄する。
#include "cw/lib/compat.hpp"
#include "cw/dialog/presentation.hpp"

#include <memory>
#include <optional>
#include <vector>

namespace cw::header { class CardHeader; }

namespace cw::dialog::cardreorder {

struct ReorderCard {
    std::shared_ptr<::cw::header::CardHeader> header;
    // カードポケットのダイアログと同じ描画のカード画像。ウィンドウ倍率を適用した画素単位。
    std::shared_ptr<::cw::lib::Surface> image;
};

class CardReorderDialog final : public ::cw::lib::Dialog,
                                public ::cw::dialog::DialogPresentationProvider {
public:
    static constexpr std::size_t kMaxCards = 50;

    // scale は論理一画素あたりの出力画素数で、カードダイアログの UI 倍率を表す。
    void initialize(std::vector<ReorderCard> cards, ::cw::lib::Color background,
                    ::cw::lib::Real scale);
    [[nodiscard]] bool accepted() const noexcept { return m_accepted; }
    // 編集後の順序のヘッダー。initialize() に渡したものと同じ集合を保持する。
    [[nodiscard]] std::vector<std::shared_ptr<::cw::header::CardHeader>> order() const;

    [[nodiscard]] ::cw::dialog::DialogPresentation dialog_presentation() override;
    void dialog_presentation_action(::cw::dialog::DialogPresentationAction action, int index) override;
    void dialog_presentation_pointer(::cw::dialog::DialogPresentationAction action, int index,
                                     float x, float y, bool pressed) override;
    void dialog_presentation_pointer_cancel() override;
    void dialog_presentation_enter(::cw::dialog::DialogPresentationModifiers modifiers) override;
    [[nodiscard]] bool dialog_presentation_finished() const noexcept override { return m_finished; }
    void dialog_presentation_complete() override { finish(false); }

private:
    struct Entry {
        std::shared_ptr<::cw::header::CardHeader> header;
        std::shared_ptr<::cw::lib::Surface> thumbnail;
    };
    std::vector<Entry> m_entries;
    ::cw::lib::Color m_background{0, 0, 0, 255};
    ::cw::lib::Real m_scale{1.0};
    bool m_finished{};
    bool m_accepted{};
    // ドラッグ状態。元の枠、現在の移動先の枠、画素単位のポインター位置。
    std::optional<std::size_t> m_drag;
    std::size_t m_target{};
    float m_pointer_x{}, m_pointer_y{};
    bool m_moved{};
    // false はドラッグ元と移動先の交換（既定）。true
    // は移動先への挿入で、間のカードをずらす。
    bool m_insert_mode{};

    [[nodiscard]] ::cw::lib::Integer px(::cw::lib::Integer value) const;
    [[nodiscard]] std::size_t slot_at(float x, float y) const;
    [[nodiscard]] std::vector<std::size_t> display_order() const;
    void finish(bool accept);
};

}  // namespace cw::dialog::cardreorder
