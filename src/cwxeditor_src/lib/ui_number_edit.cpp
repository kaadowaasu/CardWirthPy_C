#include "ui_number_edit.hpp"
#include "ui_metrics.hpp"
#include "window_host.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>

namespace cwxeditor::lib {
namespace {
bool contains(Rect r, Point p) noexcept {
    return p.x >= r.x && p.y >= r.y && p.x < r.x + r.width && p.y < r.y + r.height;
}
bool numeric_draft(std::string_view value, unsigned decimals,
                   std::size_t max_digits = 0U) {
    if (value.size() > 64) return false;
    bool dot{};
    unsigned fraction{};
    std::size_t digits{};
    for (std::size_t i = 0; i < value.size(); ++i) {
        const auto ch = value[i];
        if (ch == '-' && i == 0) continue;
        if (ch == '.' && decimals && !dot) { dot = true; continue; }
        if (ch < '0' || ch > '9') return false;
        if (dot && ++fraction > decimals) return false;
        // input_max_length は数字の桁数を制限し、先頭のマイナスは
        // 数えない。 ALL のX/Yは -999～999、W/Hは0～999。
        if (max_digits != 0U && ++digits > max_digits) return false;
    }
    return true;
}
std::optional<double> parse(std::string_view text) {
    double value{};
    // 全対象で一つのロケール非依存の解析処理を使う。Androidのlibc++には浮動小
    // 数点の from_chars がない版があり、 条件分岐で使い分けると、NumberEdit
    // の受け付ける表記が対象ごとに変わる。 classicロケールのストリームで、
    // format_number_value() が出す十進表記を正確に受け付ける。
    std::istringstream stream{std::string(text)};
    stream.imbue(std::locale::classic());
    if (!(stream >> value)) return {};
    char trailing{};
    if (stream >> trailing) return {};
    if (!std::isfinite(value)) return {};
    return value;
}
}

int NumberEditLayout::step_at(Point point) const noexcept {
    return contains(increase, point) ? 1 : contains(decrease, point) ? -1 : 0;
}
NumberEditLayout layout_number_edit(const NumberEdit& number) noexcept {
    const auto r = number.bounds();
    if (editor_window_host_options().all_mode) {
        // 入力欄の右に独立した「＋」「－」を横並びにする。
        const float available = std::max(0.0F, r.width);
        // ALL仕様: 入力欄・項目名・＋・－の間の余白は0。
        const float gap = 0.0F;
        // ALL のコンパクトなシーン操作は34px
        // の増減ボタンを使う。 全体を指定の狭い幅に
        // 保ち、文字セルを意図した三桁分の幅にする。
        const float label = number.inline_label.empty()
            ? 0.0F : std::min(scaled_ui_metric(14), available / 4);
        // 最小限の文字セルが残る限り、ボタンは34px
        // 、または部品自体の寸法を維持する。
        const float button_extent = number.step_button_extent > 0.0F
            ? number.step_button_extent : 34.0F;
        const float button = std::min(scaled_ui_metric(button_extent),
            std::max(0.0F, available - label - 2 * gap - scaled_ui_metric(18)) / 2);
        const float text = std::max(0.0F, available - label - 2 * (button + gap));
        const float height = std::max(0.0F, r.height);
        // ALL仕様: 入力欄, 項目名(X等), ＋, － の順。－を入力欄から離し、
        // 押している指で入力欄が隠れないようにする。
        return {{r.x, r.y, text, height},
                {r.x + text + label + gap, r.y, button, height},
                {r.x + text + label + button + 2 * gap, r.y, button, height}, true,
                {r.x + text, r.y, label, height}};
    }
    const float width = std::min(std::max(0.0F, r.width) * .4F, scaled_ui_metric(16));
    const float text_width = std::max(0.0F, r.width - width);
    const float height = std::max(0.0F, r.height);
    return {{r.x, r.y, text_width, height},
            {r.x + text_width, r.y, width, height * .5F},
            {r.x + text_width, r.y + height * .5F, width, height * .5F}};
}
std::string format_number_value(const NumberEdit& number) {
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::fixed << std::setprecision(std::min(number.decimals, 20U))
           << (std::isfinite(number.value) ? number.value : 0.0);
    return stream.str();
}
void NumberEditSession::reset_text() {
    if (const auto number = target_.lock()) {
        editor_->text = format_number_value(*number);
        observed_value_ = number->value;
        observed_decimals_ = number->decimals;
        if (draft_) {
            ++draft_->revision;
            draft_->editor = editor_;
            draft_->finished_text.reset();
            draft_->observed_value = observed_value_;
            draft_->observed_decimals = observed_decimals_;
        }
        state_ = {};
        (void)select_all_text(*editor_, state_);
    }
}
void NumberEditSession::begin(const std::shared_ptr<NumberEdit>& number) {
    if (target_.lock() != number) {
        const auto expected = generation_ + 1U;
        finish();
        if (generation_ != expected) return;
        target_ = number;
        if (draft_) draft_->owner.reset();
        draft_ = number ? std::make_shared<NumberEdit::DraftState>() : nullptr;
        if (number) { draft_->owner = number; draft_->session_lifetime = lifetime_; number->edit_draft_ = draft_; }
        ++generation_;
        baseline_value_=number?number->value:0.0;
        preview_started_=false;
        reset_text();
    }
    synchronize();
}
void NumberEditSession::repair_after_failure(std::uint64_t generation) noexcept {
    if (generation_ != generation) return;
    try { reset_text(); } catch (...) { /* 元の通知失敗を維持する。 */ }
}
void NumberEditSession::project(const std::shared_ptr<NumberEdit>& number) {
    if (!active_for(number)) {
        const auto expected=generation_+1U;
        discard();
        if(generation_!=expected)return;
    }
    begin(number);
}
void NumberEditSession::synchronize() {
    if (const auto number = target_.lock()) {
        // 未完成の入力は保持するが、標準値ボタン
        // など、別の部品が行った変更を隠さない。
        if (number->value != observed_value_ || number->decimals != observed_decimals_) {
            baseline_value_=number->value;preview_started_=false;
            reset_text();
        }
        editor_->set_bounds(layout_number_edit(*number).text);
        editor_->set_font(number->font());
        editor_->alignment = TextAlignment::End;
        editor_->zero_text_padding = number->zero_text_padding;
        // 数字の桁数は numeric_draft() が制限
        // するため、生の文字列長では符号と小数部分も許可する。
        editor_->max_length = number->input_max_length == 0U ? 0U
            : number->input_max_length + 1U +
              (number->decimals ? number->decimals + 1U : 0U);
        editor_->read_only = number->read_only;
        editor_->set_enabled(number->enabled());
    }
}
bool NumberEditSession::publish() {
    const auto number = target_.lock();
    if (!number || !number->enabled() || number->read_only) return false;
    const auto value = parse(editor_->text);
    // 未完成・範囲外の入力も編集可能に保つ。例えば最小値10で10を入力する途中も許す。
    if (!value || *value < number->minimum || *value > number->maximum) return false;
    // 通知前に観測値を反映する。 コールバックが新しい編集を開
    // 始・値を上書きした場合、その後でどちらも上書きしない。
    const auto generation = generation_;
    observed_value_ = *value;
    if (draft_) draft_->observed_value = observed_value_;
    if(number->on_preview)preview_started_=true;
    bool changed{};
    try { changed = set_number_value(*number, *value,NumberValueOrigin::Preview); }
    catch (...) { repair_after_failure(generation); throw; }
    if (generation_ == generation) synchronize();
    return changed;
}
void NumberEditSession::commit() {
    // 前の通知によるものも含め、モデルの変更を古い未確定入力
    // より優先する。 次フレームの準備時と同じ規則にする。
    synchronize();
    const auto generation = generation_;
    try {
        if (const auto number = target_.lock(); number && number->enabled() && !number->read_only) {
            const auto value=parse(editor_->text);
            // D版の SpinnerEdit.enter と同様、空・
            // 無変更の選択はキャンセルする。 混在した選択では、共通の
            // 表示既定値と確定入力が同じでも、各元の座標を保持する。
            if(number->on_preview && (!value || *value==baseline_value_)) {cancel();return;}
            if(value)(void)set_number_value(*number,*value);
        }
    } catch (...) { repair_after_failure(generation); throw; }
    if (generation_ == generation) {
        if(const auto number=target_.lock())baseline_value_=number->value;
        preview_started_=false;reset_text();
    }
}
void NumberEditSession::finish() {
    ++generation_;
    // on_changed の中で、外側の増減・確定処理が表示を更新す
    // る前にフォーカスが外れる場合がある。 通知前の値を書き戻さない。
    synchronize();
    const auto number = target_.lock();
    const auto value = parse(editor_->text);
    const bool cancel_preview=number && number->on_preview && (!value || *value==baseline_value_);
    const auto cancel_callback=number?number->on_edit_cancelled:decltype(number->on_edit_cancelled){};
    const auto baseline=baseline_value_;
    const bool had_preview=preview_started_;
    // SpinnerEdit の後のフォーカス喪失コールバック用に、実際
    // の生の文字列を保持する。 終了・通知の順序や数値モデルは変えない。
    const auto raw_text = editor_->text;
    const auto finished_draft = draft_;
    // フォーカス喪失時と同様、対象を外す前に未完成の入力を捨てる。
    reset_text();
    if (number && finished_draft) {
        ++finished_draft->revision;
        finished_draft->editor.reset();
        finished_draft->finished_text = raw_text;
    }
    // モデルへの通知前に、この編集状態を解放する。 コールバックが別の数
    // 値欄へフォーカスを移し、直ちに新しい編集を始める場合があるため。
    target_.reset();
    preview_started_=false;
    if(cancel_preview) {
        if(had_preview)number->value=baseline;
        if(cancel_callback)cancel_callback();
    } else if (number && number->enabled() && !number->read_only && value)
        (void)set_number_value(*number, *value);
}
void NumberEditSession::cancel() {
    const auto generation=++generation_;
    // 確定時と同様、Escapeや表示側の終了でも、 外部
    // のモデル同期を古いプレビューの基準値より優先する。
    synchronize();
    const auto number=target_.lock();
    const auto cancel_callback=number?number->on_edit_cancelled:decltype(number->on_edit_cancelled){};
    if(number && number->on_preview && preview_started_)number->value=baseline_value_;
    preview_started_=false;
    if(cancel_callback)cancel_callback();
    if(generation_==generation)reset_text();
}
void NumberEditSession::discard() {
    const auto expected=generation_+1U;cancel();
    if(generation_==expected) {
        target_.reset();
        if (draft_) draft_->owner.reset();
        draft_.reset();
    }
}
bool NumberEditSession::insert(std::string_view value) {
    const auto number = target_.lock();
    if (!number || !number->enabled() || number->read_only) return false;
    synchronize();
    const auto generation = ++generation_; // 同じ対象への再入も、古い操作を置き換える。
    const auto old = editor_->text;
    const auto old_state = state_;
    const auto old_value = number->value;
    const auto old_decimals = number->decimals;
    const bool changed = replace_text_selection(*editor_, state_, value);
    if (generation_ != generation || target_.lock() != number) return changed;
    if (number->value != old_value || number->decimals != old_decimals) {synchronize(); return changed;}
    if (!numeric_draft(editor_->text, number->decimals, number->input_max_length)) {
        editor_->text = old; state_ = old_state;
        commit_text_edit_state(*editor_, state_);
        return false;
    }
    (void)publish();
    return changed;
}
bool NumberEditSession::step(const std::shared_ptr<NumberEdit>& number, double amount) {
    begin(number);
    if (!active_for(number)) return false;
    const auto generation = generation_;
    const bool preview=number && bool(number->on_preview);
    if(!preview)commit();
    if (generation_ != generation) return false;
    bool changed{};
    try {
        if(preview && number->enabled() && !number->read_only && std::isfinite(amount)) {
            const double increment=std::isfinite(number->step) && number->step>0?number->step:1.0;
            const auto next=validated_number_value(*number,number->value+increment*amount);
            if(!next)return false;
            observed_value_=*next;
            if (draft_) draft_->observed_value = observed_value_;
            preview_started_=true;
            changed=set_number_value(*number,observed_value_,NumberValueOrigin::Preview);
        } else changed=number && step_number_value(*number,amount);
    }
    catch (...) { repair_after_failure(generation); throw; }
    if (generation_ == generation) reset_text();
    return changed;
}
bool NumberEditSession::key(Key key, Modifiers modifiers, float line_height,
                            const TextWidthMeasure& measure, const TextClipboardAccess& clipboard,
                            const std::function<bool()>& live) {
    if (live && !live()) return true;
    const auto number = target_.lock();
    if (!number || !number->enabled()) return false;
    synchronize();
    if (key == Key::Enter) { commit(); return false; } // ダイアログの既定操作を許可する。
    if (key == Key::Escape) { cancel(); return true; }
    if (key == Key::Up || key == Key::Down || key == Key::PageUp || key == Key::PageDown) {
        (void)step(number, key == Key::Up ? 1 : key == Key::Down ? -1 : key == Key::PageUp ? 10 : -10);
        return true;
    }
    const auto generation = ++generation_;
    const auto value = number->value, minimum = number->minimum, maximum = number->maximum;
    const auto decimals = number->decimals;
    const auto max_digits = number->input_max_length;
    const auto read_only = number->read_only;
    const auto same_number = [](double a, double b) {return a == b || (std::isnan(a) && std::isnan(b));};
    const auto current = [&] {
        return (!live || live()) && generation_ == generation && target_.lock() == number &&
            number->enabled() && number->read_only == read_only && number->decimals == decimals &&
            same_number(number->value, value) && same_number(number->minimum, minimum) &&
            same_number(number->maximum, maximum);
    };
    bool handled{}, committed{};
    try {
        handled = dispatch_text_edit_key(*editor_, state_, key, modifiers,
            text_edit_inner_bounds(*editor_), line_height, measure, clipboard,
            {current, [&](std::string_view text) {return numeric_draft(text, decimals, max_digits);}, &committed});
    } catch (...) {
        // 提供処理の失敗では未確定入力を変えないが、通知の失敗時は表示が確
        // 定済みの場合がある。 外部のモデル・編集状態の変更を保持する。
        if (generation_ == generation) try { synchronize(); } catch (...) {}
        throw;
    }
    if (!current()) {
        if (generation_ == generation) synchronize();
        return true; // 古いキー入力を、新しいダイアログの処理へ通さない。
    }
    if (!handled) return false;
    if (committed) (void)publish();
    return handled;
}

}  // namespace cwxeditor::lib
