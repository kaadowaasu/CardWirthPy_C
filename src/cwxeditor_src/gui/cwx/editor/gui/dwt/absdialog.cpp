#include "cwxeditor_src/gui/cwx/editor/gui/dwt/absdialog.hpp"
// UIの実装は一つに統一し、
// 別経路の実装を設けない。

#include "cwxeditor_src/core/cwx/msgs.hpp"
#include "cwxeditor_src/lib/ui_metrics.hpp"
#include "cwxeditor_src/lib/ui_window_lifecycle.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>

namespace cwxeditor::cwx::editor::gui::dwt::absdialog {
namespace {

constexpr std::uint32_t kReadOnly =
    static_cast<std::uint32_t>(StyleFlag::ReadOnly);
constexpr float kDefaultWidth = 480.0F;
constexpr float kDefaultHeight = 320.0F;
constexpr float kMinimumSize = 1.0F;

template <typename T>
std::shared_ptr<T> makeComponent(const lib::Services& services,
                                 lib::ComponentKind kind) {
    // コンポーネントは共通モデルであり、ネイティブコントロールは完成し
    // たダイアログツリーを組み込むときに作る。メニュー操作からモーダル
    // ダイアログを構築中にモデルごとにネイティブバックエンドへ問い合わ
    // せると、再入によって最初の子を接続する前にデッドロックし得る。
    (void)services;
    (void)kind;
    return std::make_shared<T>();
}

template <typename T>
void invokeCopy(const std::vector<T>& callbacks) {
    const auto copy = callbacks;
    for (const auto& callback : copy)
        if (callback) callback();
}

[[nodiscard]] float positiveOr(float value, float fallback) noexcept {
    return value > 0.0F && std::isfinite(value) ? value : fallback;
}

void mergeMissingServices(lib::Services& target, const lib::Services& source) {
    if (!target.gui) target.gui = source.gui;
    if (!target.graphics) target.graphics = source.graphics;
    if (!target.image_codecs) target.image_codecs = source.image_codecs;
    if (!target.dialogs) target.dialogs = source.dialogs;
    if (!target.clipboard) target.clipboard = source.clipboard;
    if (!target.drag_drop) target.drag_drop = source.drag_drop;
    if (!target.audio) target.audio = source.audio;
    if (!target.filesystem) target.filesystem = source.filesystem;
    if (!target.processes) target.processes = source.processes;
    if (!target.shell) target.shell = source.shell;
    if (!target.ipc) target.ipc = source.ipc;
    if (!target.archives) target.archives = source.archives;
    if (!target.system) target.system = source.system;
    if (!target.text) target.text = source.text;
    if (!target.dispatcher) target.dispatcher = source.dispatcher;
    if (!target.tasks) target.tasks = source.tasks;
}

std::mutex& liveDialogsMutex() {
    static std::mutex value;
    return value;
}

std::vector<AbsDialog*>& liveDialogStorage() {
    static std::vector<AbsDialog*> value;
    return value;
}

}  // namespace

AbsDialog::AbsDialog(Props& prop, std::shared_ptr<Window> parent,
                     lib::String text, ResourceId image, bool resizable,
                     DSize* size, bool apply, bool cancel,
                     std::vector<ButtonInfo> buttons, lib::Services services)
    : AbsDialog(prop, std::move(parent), true, std::move(text), image,
                resizable, size, apply, cancel, std::move(buttons), false,
                std::move(services)) {}

AbsDialog::AbsDialog(Props& prop, std::shared_ptr<Window> parent, bool modal,
                     lib::String text, ResourceId image, bool resizable,
                     DSize* size, bool apply, bool cancel,
                     std::vector<ButtonInfo> buttons, bool right_group,
                     lib::Services services)
    : AbsDialog(prop, std::move(parent), 0U, modal, std::move(text), image,
                resizable, size, apply, cancel, std::move(buttons),
                right_group, std::move(services)) {}

AbsDialog::AbsDialog(Props& prop, std::shared_ptr<Window> parent,
    std::uint32_t style_flags, bool modal, lib::String text,
                     ResourceId image, bool resizable, DSize* size,
                     bool apply, bool cancel, std::vector<ButtonInfo> buttons,
                     bool right_group, lib::Services services) {
    prop_ = &prop;
    parent_ = std::move(parent);
    services_ = std::move(services);
    initialize(style_flags, modal, std::move(text), image, resizable, size,
               apply, cancel, std::move(buttons), right_group);
    std::scoped_lock lock(liveDialogsMutex());
    liveDialogStorage().push_back(this);
}

AbsDialog::AbsDialog(Props& prop, std::shared_ptr<Window> parent,
                     lib::String text, ResourceId image, bool resizable,
                     std::shared_ptr<DSize> size, bool apply, bool cancel,
                     std::vector<ButtonInfo> buttons, lib::Services services)
    : AbsDialog(prop, std::move(parent), true, std::move(text), image,
                resizable, size ? size.get() : nullptr, apply, cancel,
                std::move(buttons), false, std::move(services)) {
    size_owner_ = std::move(size);
    size_ = size_owner_.get();
}

AbsDialog::AbsDialog(Props& prop, std::shared_ptr<Window> parent, bool modal,
                     lib::String text, ResourceId image, bool resizable,
                     std::shared_ptr<DSize> size, bool apply, bool cancel,
                     std::vector<ButtonInfo> buttons, bool right_group,
                     lib::Services services)
    : AbsDialog(prop, std::move(parent), 0U, modal, std::move(text), image,
                resizable, size ? size.get() : nullptr, apply, cancel,
                std::move(buttons), right_group, std::move(services)) {
    size_owner_ = std::move(size);
    size_ = size_owner_.get();
}

AbsDialog::AbsDialog(Props& prop, std::shared_ptr<Window> parent,
                     std::uint32_t style_flags, bool modal, lib::String text,
                     ResourceId image, bool resizable,
                     std::shared_ptr<DSize> size, bool apply, bool cancel,
                     std::vector<ButtonInfo> buttons, bool right_group,
                     lib::Services services)
    : AbsDialog(prop, std::move(parent), style_flags, modal, std::move(text),
                image, resizable, size ? size.get() : nullptr, apply, cancel,
                std::move(buttons), right_group, std::move(services)) {
    size_owner_ = std::move(size);
    size_ = size_owner_.get();
}

AbsDialog::~AbsDialog() {
    lifetime_.reset();
    if (async_open_ && services_.gui && window_) {
        try {
            services_.gui->end_modal(window_);
        } catch (...) {
            // 破棄は最後の後処理の境界。表示側もすでに終了
            // 中なので、この後処理から例外を送出しない。
        }
    }
    async_open_ = false;
    async_completion_ = {};
    {
        std::scoped_lock lock(liveDialogsMutex());
        std::erase(liveDialogStorage(), this);
    }
    clearWindowSizeInfo();
    if (window_) {
        window_->on_close_requested = {};
        window_->on_key_down = {};
        window_->on_activated = {};
        window_->on_visibility_changed = {};
    }
}

void AbsDialog::initialize(std::uint32_t style_flags, bool modal,
                           lib::String text, ResourceId image, bool resizable,
                           DSize* size, bool apply, bool cancel,
                           std::vector<ButtonInfo> button_info,
                           bool right_group) {
    if (!prop_) throw std::invalid_argument("AbsDialog requires properties");

    // DWTの複数のダイアログは意図的にサービス引
    // 数を省略する。SWTではアプリケーションの共
    // 有Displayを使うため、共通版もProps
    // のサービスを継承し、黙ってヘッドレスへ切り替
    // えない。残る未設定項目には一定の代替を補い
    // 、明示的なテスト用バックエンドは維持する。
    if (prop_) mergeMissingServices(services_, prop_->images().services());
    const auto fallback = lib::make_headless_services();
    mergeMissingServices(services_, fallback);

    if (services_.gui) window_ = services_.gui->create_dialog();
    if (!window_) window_ = std::make_shared<Dialog>();

    image_ = image;
    window_->icon = image_;
    style_flags_ = style_flags;
    modal_ = modal;
    has_apply_ = apply && (style_flags_ & kReadOnly) == 0U;
    size_ = size;

    window_->title = std::move(text);
    window_->resizable = resizable;
    // DWTはサイズ変更可能なダイアログにSWT.SHELL_TRIM
    // を使う。これはMINとMAXを両方含む。MAXだけではEventTemplate
    // やTextCellなどの見た目がD版と異なる。
    window_->minimizable = resizable;
    window_->maximizable = resizable;
    lib::configure_owned_window(window_, parent_, modal_
        ? lib::OwnedWindowKind::Modal
        : lib::OwnedWindowKind::Modeless);
    // Dは未表示のShellを作る。GuiBackendの未接続の汎用
    // モデルはWidgetのvisible=trueを既定とするため、
    // コールバックやネイティブ表示が始まる前に正規化する。全バックエン
    // ドで初期非表示のモデルから、最初の表示の可視性遷移を公開する。
    window_->set_visible(false);
    window_->result = lib::DialogResult::None;
    // ネイティブのWM_CLOSEやShellの終了イベント
    // は要求にすぎない。SWTではShellListener
    // がevent.doitを制御し、受理後にSWTが閉じる
    // 。共通コールバックには戻り値がないため、受理した終了を
    // 明示的に実行する必要がある。そうしないと閉じるボタンで
    // も表示が残り、モーダルのイベントループも待ち続ける。
    main_panel_ = makeComponent<Panel>(services_, lib::ComponentKind::Panel);
    area_ = makeComponent<Panel>(services_, lib::ComponentKind::Panel);
    addition_ = makeComponent<Panel>(services_, lib::ComponentKind::Panel);
    buttons_panel_ =
        makeComponent<Panel>(services_, lib::ComponentKind::Panel);

    auto root_layout = std::make_shared<lib::GridLayout>();
    root_layout->columns = right_group ? 2U : 1U;
    root_layout->equal_width = false;
    root_layout->spacing = 0.0F;
    // Editorダイアログの共通余白。右側にプレビューがある
    // 場合も、最後の操作行を枠から離す。本体の余白は維持する。
    root_layout->padding.bottom = 6.0F;
    window_->set_layout(root_layout);

    auto main_layout = std::make_shared<lib::GridLayout>();
    main_layout->columns = 1U;
    main_layout->equal_width = false;
    main_layout->spacing = 4.0F;
    main_panel_->set_layout(main_layout);
    main_panel_->set_layout_item(
        lib::LayoutItem{0U, 0U, 1U, 1U, lib::Align::Stretch,
                        lib::Align::Stretch, {}, {}, {}, 1.0F});

    area_->set_layout_item(
        lib::LayoutItem{0U, 0U, 1U, 1U, lib::Align::Stretch,
                        lib::Align::Stretch, {}, {}, {}, 1.0F});
    main_panel_->add(area_);

    auto separator =
        makeComponent<lib::Separator>(services_, lib::ComponentKind::Separator);
    separator->orientation = lib::Orientation::Horizontal;
    separator->set_layout_item(
        lib::LayoutItem{1U, 0U, 1U, 1U, lib::Align::Stretch,
                        lib::Align::Center, {}, {0.0F, 1.0F}, {}, 0.0F});
    main_panel_->add(separator);

    // 伸縮可能な追加領域と自然幅のボタン群を専用のフ
    // ッター行へ置く。列をまたぐ本体と同じグリッド
    // にすると、本体幅が両フッター列へ加わり、幅広
    // のダイアログでボタンが伸びたりずれたりする。
    auto footer = makeComponent<Panel>(services_, lib::ComponentKind::Panel);
    auto footer_layout = std::make_shared<lib::GridLayout>();
    footer_layout->columns = 2U;
    footer_layout->equal_width = false;
    footer_layout->spacing = 4.0F;
    footer->set_layout(footer_layout);
    footer->set_layout_item(
        lib::LayoutItem{2U, 0U, 1U, 1U, lib::Align::Stretch,
                        lib::Align::Center, {}, {0.0F, 32.0F}, {}, 0.0F});

    auto addition_layout = std::make_shared<lib::GridLayout>();
    addition_layout->columns = 1U;
    addition_layout->equal_width = true;
    addition_->set_layout(addition_layout);
    addition_->set_layout_item(
        lib::LayoutItem{0U, 0U, 1U, 1U, lib::Align::Stretch,
                        lib::Align::Center, {}, {0.0F, 32.0F}, {}, 1.0F});
    footer->add(addition_);

    auto button_layout = std::make_shared<lib::LinearLayout>(
        lib::Orientation::Horizontal);
    button_layout->cross_axis = lib::Align::Center;
    button_layout->spacing = 4.0F;
    buttons_panel_->set_layout(button_layout);
    buttons_panel_->set_layout_item(
        lib::LayoutItem{0U, 1U, 1U, 1U, lib::Align::End,
                        lib::Align::Center, {}, {0.0F, 32.0F}, {}, 0.0F});
    footer->add(buttons_panel_);
    main_panel_->add(footer);
    window_->add(main_panel_);

    pending_read_only_ = (style_flags & kReadOnly) != 0U;
    pending_cancel_ = cancel;
    pending_button_info_ = std::move(button_info);
    // open()前に未接続のコンポーネントツリーを完成させる。メ
    // ニュー操作がネイティブ配送経路へ入った後の確保は、UIとメモ
    // リ確保のロック順を逆転させ得る。この段階はモデルだけで、HWND
    // はshow()/show_modal()まで作らない。
    initializeButtons();

    useRightGroupImpl(right_group);
}

void AbsDialog::initializeButtons() {
    if (buttons_initialized_) return;
    buttons_initialized_ = true;
    const auto enlarge_all_action_button = [](const std::shared_ptr<Button>& button) {
        if (!button || !lib::editor_window_host_options().all_mode) return;
        auto item = button->layout_item();
        item.minimum.height = lib::kEditorUiMetrics.control_height + 4.0F;
        button->set_layout_item(item);
    };
    if (pending_read_only_) {
        ok_button_ = createButton(
            buttons_panel_, prop_->requiredMsgs().dlgTextClose.value,
            [this] { this->cancel(); });
        ok_button_->cancel_button = true;
        buttons_.push_back(ok_button_);
    } else {
        ok_button_ = createButton(
            buttons_panel_, prop_->requiredMsgs().dlgTextOK.value,
            [this] { this->ok(); });
        enlarge_all_action_button(ok_button_);
        buttons_.push_back(ok_button_);
        if (pending_cancel_) {
            auto cancel_button = createButton(
                buttons_panel_, prop_->requiredMsgs().dlgTextCancel.value,
                [this] { this->cancel(); });
            enlarge_all_action_button(cancel_button);
            cancel_button->cancel_button = true;
            buttons_.push_back(cancel_button);
        }
        if (has_apply_) {
            apply_button_ = createButton(
                buttons_panel_, prop_->requiredMsgs().dlgTextApply.value,
                [this] { forceApply(); });
            enlarge_all_action_button(apply_button_);
            buttons_.push_back(apply_button_);
        }
    }

    for (auto& info : pending_button_info_) {
        auto button = createButton(buttons_panel_, std::move(info.name),
                                   std::move(info.func));
        buttons_.push_back(std::move(button));
    }
    pending_button_info_.clear();
}

void AbsDialog::rightGroupSize(int width, int height) {
    if (!right_group_)
        throw std::logic_error("rightGroupSize requires an active right group");

    auto bounds = right_group_->bounds();
    bounds.width = static_cast<float>(std::max(0, width));
    bounds.height = static_cast<float>(std::max(0, height));
    right_group_->set_bounds(bounds);

    // 右グループ作成時の三行・第二列の配置を維持する
    // 。LayoutItem全体を既定の項目で置換
    // すると、グループが行0・列0へ移ってしまう。
    right_group_->set_layout_item(
        lib::LayoutItem{0U, 1U, 3U, 1U, lib::Align::Stretch,
                        lib::Align::Stretch, {},
                        {bounds.width, bounds.height}, {}, 0.0F});
}

void AbsDialog::useRightGroup(bool right_group) {
    if (right_group == static_cast<bool>(right_group_)) return;
    useRightGroupImpl(right_group);
}

void AbsDialog::useRightGroupImpl(bool right_group) {
    auto* root_layout =
        dynamic_cast<lib::GridLayout*>(window_->layout().get());
    if (!root_layout) {
        auto layout = std::make_shared<lib::GridLayout>();
        window_->set_layout(layout);
        root_layout = layout.get();
    }

    if (right_group) {
        root_layout->columns = 2U;
        if (!right_group_) {
            right_group_ =
                makeComponent<Panel>(services_, lib::ComponentKind::Panel);
            right_group_->set_layout(std::make_shared<lib::FillLayout>());
            right_group_->set_layout_item(
                lib::LayoutItem{0U, 1U, 3U, 1U, lib::Align::Stretch,
                                lib::Align::Stretch, {}, {}, {}, 0.0F});
            window_->add(right_group_);
        }
    } else {
        root_layout->columns = 1U;
        if (right_group_) {
            window_->remove(right_group_);
            right_group_.reset();
        }
    }
}

void AbsDialog::setImages(std::vector<ResourceId> images) {
    // Dは複数サイズのネイティブアイコンを渡せる。共通Windowモデルは
    // 主アイコンを一つ公開するため、一覧を保持し、その主項目を全バックエン
    // ドで同じモデルから公開する。ネイティブ専用のアイコン経路は作らない。
    images_ = std::move(images);
    image_ = images_.empty() ? 0U : images_.front();
    if (window_) window_->icon = image_;
}

std::shared_ptr<AbsDialog::Button> AbsDialog::createButton(
    const std::shared_ptr<Panel>& parent, lib::String text, lib::Action push) {
    auto button = makeComponent<Button>(services_, lib::ComponentKind::Button);
    button->text = std::move(text);
    button->on_click = std::move(push);

    int button_width = 80;
    if (prop_) {
        const auto configured = prop_->var().buttonWidth.value;
        if (configured > 0) button_width = configured;
    }
    button->set_layout_item(lib::LayoutItem{
        0U, buttons_.size(), 1U, 1U, lib::Align::Stretch, lib::Align::Center,
        {}, {static_cast<float>(button_width), 0.0F}, {}, 0.0F});
    parent->add(button);
    return button;
}

void AbsDialog::windowSizeInfo(DSize* size) {
    clearWindowSizeInfo();

    auto* old_window_size = dynamic_cast<WSize*>(size_);
    auto* new_window_size = dynamic_cast<WSize*>(size);
    if (old_window_size && new_window_size) {
        new_window_size->x(old_window_size->x());
        new_window_size->y(old_window_size->y());
    }

    size_owner_.reset();
    size_ = size;
    calculateBounds(false);
}

void AbsDialog::windowSizeInfo(std::shared_ptr<DSize> size) {
    const auto raw = size.get();
    windowSizeInfo(raw);
    size_owner_ = std::move(size);
    size_ = size_owner_.get();
}

void AbsDialog::clearWindowSizeInfo() {
    auto clear = std::move(clear_setup_window_);
    if (clear) clear(true);
    clear_setup_window_ = {};
}

void AbsDialog::saveWin() {
    if (!size_) return;
    const auto bounds = window_->bounds();

    // 最小化・最大化中は通常状態の
    // 位置とサイズを上書きしない。
    if (!window_->maximized && !window_->minimized) {
        const float scale = std::max(1.0F, window_->bounds_ui_text_scale);
        size_->width(rounded(bounds.width / scale));
        size_->height(rounded(bounds.height / scale));

        if (auto* window_size = dynamic_cast<WSize*>(size_)) {
            const auto parent_bounds = parent_ ? parent_->bounds() : lib::Rect{};
            window_size->x(rounded(bounds.x - parent_bounds.x));
            window_size->y(rounded(bounds.y - parent_bounds.y));
        }
    }
    if (auto* window_size = dynamic_cast<WSize*>(size_))
        window_size->maximized(window_->maximized);
}

void AbsDialog::title(lib::String value) {
    window_->title = std::move(value);
}

void AbsDialog::enterClose(bool value) {
    enter_close_ = value;
    if (ok_button_) ok_button_->default_button = value;
}

void AbsDialog::firstFocusIsOK(bool value) {
    // Dは設定引数を無視してtrueを強制していたが、ここでは意図的にfalseも反映する。
    first_focus_is_ok_ = value;
    if (value && window_ && window_->visible() && ok_button_ && services_.gui)
        services_.gui->focus(ok_button_);
}

void AbsDialog::forceCancel() {
    if (in_close_event_) return;
    force_cancel_ = true;
    if (window_ && window_->visible()) (void)close();
}

void AbsDialog::modal(const bool value) noexcept {
    modal_ = value;
    if (window_) window_->modal = value;
}

bool AbsDialog::open() {
    return openImpl({}, false);
}

bool AbsDialog::openAsync(std::function<void(bool)> completed) {
    if (!modal_ || !services_.gui || !completed) return false;
    return openImpl(std::move(completed), true);
}

bool AbsDialog::openImpl(std::function<void(bool)> completed,
                         const bool asynchronous) {
    if (!window_ || in_close_event_ || force_applying_ ||
        (window_->visible() && !closed_) || (async_open_ && !closed_)) return false;
    if (asynchronous && (!modal_ || !services_.gui || !completed)) return false;
    const std::weak_ptr<int> alive = lifetime_;
    // コンストラクターの警告も初回表示に属する。初期化
    // 済みダイアログを再度開く場合だけ、世代を進める。
    const auto generation = setup_done_ ? ++presentation_generation_ : presentation_generation_;
    closed_ = false;
    const auto target = window_;
    const auto gui = services_.gui;
    const auto parent = parent_;
    initializeButtons();

    async_open_ = asynchronous;
    async_finishing_ = false;
    async_completion_ = std::move(completed);

    // 最派生のダイアログの構築が完了してからコールバックを接続する
    // 。基底コンストラクターから未完成のオブジェクトをキャプチャ
    // して公開すると、メニューによるダイアログ作成へ再入し得る。
    window_->on_close_requested = [this, alive, generation] {
        if (!alive.expired() && generation == presentation_generation_) (void)close();
    };
    window_->on_key_down = [this, alive, generation](lib::KeyEvent& event) {
        if (alive.expired() || generation != presentation_generation_) return;
        if (enter_close_ && event.key == lib::Key::Enter && !event.handled) {
            ok();
            event.handled = true;
        }
    };

    if (!setup_done_) {
        setup(*area_);
        if (alive.expired() || generation != presentation_generation_) return false;
        setup_done_ = true;
    }
    if (ok_button_) ok_button_->default_button = enter_close_;
    calculateBounds(true);
    if (alive.expired() || generation != presentation_generation_) return false;

    if (apply_button_ && !force_apply_enabled_)
        apply_button_->set_enabled(false);
    force_apply_enabled_ = false;

    return_value_ = false;
    applied_ = false;
    force_cancel_ = false;
    closed_ = false;
    window_->result = lib::DialogResult::None;
    // Dはフォーカス・openedの通知前にShellを開く。同期
    // モーダルループも含め、全表示側で同じ可視性の遷移を公開する。
    // 外部コールバックの前に、この一度限りの通知を消費済みにする。
    auto notified = std::make_shared<bool>(false);
    window_->on_visibility_changed = [this, alive, generation, notified](bool visible) {
        if (!visible || *notified || alive.expired() || closed_ ||
            generation != presentation_generation_) return;
        *notified = true;
        const auto listeners = openedEvent;
        const auto gui = services_.gui;
        const auto first = first_focus_is_ok_ ? ok_button_ : std::shared_ptr<Button>{};
        if (gui && first) gui->focus(first);
        if (alive.expired() || generation != presentation_generation_ || closed_) return;
        opened();
        if (alive.expired() || generation != presentation_generation_ || closed_) return;
        for (const auto& callback : listeners) {
            if (callback) callback();
            if (alive.expired() || generation != presentation_generation_ || closed_) return;
        }
    };
    if (!gui) target->set_visible(true);
    if (alive.expired() || generation != presentation_generation_ || closed_) return false;

    if (!modal_) {
        if (gui) gui->show(target);
        if (alive.expired()) { if (gui) gui->close(target); else target->set_visible(false); return false; }
        if (alive.expired() || generation != presentation_generation_ || closed_) return false;
        async_open_ = false;
        async_completion_ = {};
        return false;
    }

    if (asynchronous) {
        try {
            const bool presented = gui->begin_modal(target, parent);
            if (alive.expired() || generation != presentation_generation_ || closed_) return false;
            if (!presented) {
                async_open_ = false;
                async_completion_ = {};
                return false;
            }
        } catch (...) {
            if (!alive.expired() && generation == presentation_generation_) {
                async_open_ = false;
                async_completion_ = {};
            }
            throw;
        }
        return true;
    }

    const auto retireSurface = [target, gui] {
        if (!target->visible()) return;
        if (gui) gui->close(target);
        else target->set_visible(false);
    };
    try {
        const auto result = gui ? gui->show_modal(target, parent) : lib::DialogResult::None;
        if (alive.expired() || generation != presentation_generation_) {
            retireSurface();
            return false;
        }
        if (!closed_) {
            return_value_ = result == lib::DialogResult::OK || result == lib::DialogResult::Apply;
            force_cancel_ = !return_value_;
            const bool finished = processCloseRequest();
            if (alive.expired() || generation != presentation_generation_) { retireSurface(); return false; }
            if (!finished) {
                force_cancel_ = true;
                (void)processCloseRequest();
                if (alive.expired() || generation != presentation_generation_) { retireSurface(); return false; }
            }
            if (result == lib::DialogResult::Apply && return_value_) {
                applied_ = true;
                target->result = lib::DialogResult::Apply;
            }
        }
        retireSurface();
    } catch (...) {
        if (!alive.expired() && generation == presentation_generation_) {
            force_cancel_ = true;
            try { (void)processCloseRequest(); } catch (...) {}
        }
        try { retireSurface(); } catch (...) {}
        throw;
    }
    if (alive.expired() || generation != presentation_generation_) return false;
    return return_value_ || applied_;
}

void AbsDialog::active() {
    if (!window_ || !window_->visible()) return;
    const auto target = window_;
    const auto gui = services_.gui;
    const std::weak_ptr<int> alive = lifetime_;
    // DのShell.setActiveは既存ウィンドウを前面に出す。共通表示側の
    // showも、本体を開き直さず既存のスタック・ネイティブ描画面を前面化する。
    if (gui) gui->show(target);
    if (alive.expired() || !target->visible()) return;
    if (gui) gui->focus(target);
    if (alive.expired() || !target->visible()) return;
    const auto activated = target->on_activated;
    if (activated) activated();
}

void AbsDialog::withRedrawSuspended(const lib::Action& update) {
    if (!update) return;
    if (!services_.gui || !window_ || !window_->visible()) {
        update();
        return;
    }

    const auto gui = services_.gui;
    const auto target = std::static_pointer_cast<lib::Widget>(window_);
    gui->set_redraw(target, false);
    struct ResumeRedraw final {
        std::shared_ptr<lib::GuiBackend> gui;
        lib::WidgetPtr target;
        ~ResumeRedraw() {
            if (gui && target) gui->set_redraw(target, true);
        }
    } resume{gui, target};
    update();
}

void AbsDialog::finishPresentedSurface() {
    const std::weak_ptr<int> alive = lifetime_;
    const auto target = window_;
    const auto gui = services_.gui;
    if (async_open_) {
        if (async_finishing_) return;
        const bool accepted = return_value_ || applied_;
        auto completed = std::move(async_completion_);
        // 表示側や利用側への通知の前に、終了を確定する。通知がこの所有元
        // を破棄したり、終了トランザクションへ再入したりする場合がある。
        async_open_ = false;
        async_finishing_ = false;
        if (gui) gui->end_modal(target);
        else if (target) target->set_visible(false);
        if (gui && target && target->visible()) gui->close(target);
        if (!alive.expired() && completed) completed(accepted);
        return;
    }
    if (gui) gui->close(target);
    else if (target) target->set_visible(false);
}

bool AbsDialog::close() {
    if (in_close_event_) return false;
    if (!window_ || (!window_->visible() && !async_open_)) return true;
    const std::weak_ptr<int> alive = lifetime_;
    const auto generation = presentation_generation_;
    const auto target = window_;
    try {
        if (!processCloseRequest()) return false;
    } catch (...) {
        if (!alive.expired() && generation == presentation_generation_ && async_open_) {
            force_cancel_ = true;
            try { if (!closed_) (void)processCloseRequest(); } catch (...) {}
            try {
                if (!alive.expired() && generation == presentation_generation_ && async_open_)
                    finishPresentedSurface();
            } catch (...) {}
        }
        throw;
    }
    if (alive.expired() || generation != presentation_generation_) return true;
    finishPresentedSurface();
    return !target->visible();
}

bool AbsDialog::noApply() const noexcept {
    return !closed_ && apply_button_ && apply_button_->enabled();
}

std::vector<AbsDialog*> AbsDialog::liveDialogs() {
    std::scoped_lock lock(liveDialogsMutex());
    return liveDialogStorage();
}

void AbsDialog::forceApply() {
    // SWTは旧ウィンドウを破棄するため、適用ボタンの通知は再度届かな
    // い。共通表示側では共有ウィジェットが残る場合があるため、派生apply()
    // がモデルを書き換える前に終了済みの表示を拒否する。
    if (in_close_event_ || closed_) return;
    const std::weak_ptr<int> alive = lifetime_;
    force_applying_ = true;
    struct Reset {
        AbsDialog* owner;
        std::weak_ptr<int> alive;
        ~Reset() { if (!alive.expired()) owner->force_applying_ = false; }
    } reset{this, alive};
    const auto generation = presentation_generation_;
    const auto current = [&] { return !alive.expired() && generation == presentation_generation_; };
    for (const auto& callback : std::vector<lib::Action>(applyEvent)) {
        if (callback) callback();
        if (!current()) return;
    }
    const bool accepted = apply();
    if (!current() || !accepted) return;
    for (const auto& callback : std::vector<lib::Action>(appliedEvent)) {
        if (callback) callback();
        if (!current()) return;
    }
    if (apply_button_) apply_button_->set_enabled(false);
    applied_ = true;
}

void AbsDialog::check() {
    bool enabled = true;
    for (const auto& entry : combo_checks_) {
        if (!enabled || !entry.widget) break;
        enabled = entry.check ? entry.check(*entry.widget)
                              : !comboText(*entry.widget).empty();
    }
    for (const auto& entry : text_checks_) {
        if (!enabled || !entry.widget) break;
        enabled = entry.check ? entry.check(*entry.widget)
                              : !entry.widget->text.empty();
    }

    if (ok_button_) ok_button_->set_enabled(enabled);
    if (apply_button_)
        apply_button_->set_enabled(apply_button_->enabled() && enabled);
}

void AbsDialog::applyEnabled() {
    applyEnabled(false);
}

void AbsDialog::applyEnabled(bool force) {
    check();
    if (apply_button_) {
        apply_button_->set_enabled(ok_button_ && ok_button_->enabled());
        force_apply_enabled_ = force_apply_enabled_ || force;
    }
}

void AbsDialog::warning(std::vector<lib::String> warnings) {
    const std::weak_ptr<int> alive = lifetime_;
    const auto generation = presentation_generation_;
    auto show = [this, alive, generation, warnings = std::move(warnings)] {
        // 送信済みのコールバックは、ダイアログより長く残ったり以前の表示に属したり
        // する場合がある。Dの破棄済みボタンの検査も、同じ終了判定の役割を持つ。
        if (alive.expired() || closed_ || generation != presentation_generation_) return;
        warningImpl(warnings);
    };
    if (services_.dispatcher) services_.dispatcher->post(std::move(show));
    else show();
}

void AbsDialog::warningImpl(const std::vector<lib::String>& warnings) {
    if (!ok_button_) return;

    // DWTは共有Images一覧から必要時に画
    // 像を取得する。全ダイアログ派生クラスに資
    // 源名を知らせず、同じ遅延取得を維持する。
    if (!warnings.empty() && !warning_icon_ && prop_)
        warning_icon_ = prop_->images().load(lib::StringView{"warning.png"});
    if (!warnings.empty() && warning_icon_)
        ok_button_->icon = warning_icon_;
    else if (warnings.empty())
        ok_button_->icon = 0U;

    lib::String tooltip;
    for (std::size_t index = 0; index < warnings.size(); ++index) {
        if (index != 0U) tooltip.push_back('\n');
        for (const char character : warnings[index]) {
            if (character == '&') tooltip += "&&";
            else tooltip.push_back(character);
        }
    }
    ok_button_->set_tooltip(std::move(tooltip));
    // ツールチップはモデルだけでなくネイティブ側にも状態を持
    // つ。ボタンだけを同期する。入力ごとに検証警告が変わる項
    // 目で、ここで全ダイアログを再構築すると白くちらつく。
    if (services_.gui) services_.gui->request_redraw(ok_button_);
}

void AbsDialog::checker(const std::shared_ptr<ComboBox>& text,
                        ComboCheck check) {
    if (text) checker(*text, std::move(check));
}

void AbsDialog::checker(ComboBox& text, ComboCheck check) {
    combo_checks_.push_back({&text, std::move(check)});
    checkerImpl(text);
}

void AbsDialog::checker(const std::shared_ptr<TextEdit>& text,
                        TextCheck check) {
    if (text) checker(*text, std::move(check));
}

void AbsDialog::checker(TextEdit& text, TextCheck check) {
    text_checks_.push_back({&text, std::move(check)});
    checkerImpl(text);
}

void AbsDialog::checkerImpl(ComboBox& text) {
    const std::weak_ptr<int> alive = lifetime_;
    check();
    auto previous = text.on_selection_changed;
    text.on_selection_changed =
        [this, previous, alive](std::optional<std::size_t> index) {
            if (alive.expired()) return;
            if (previous) previous(index);
            if (!alive.expired()) check();
        };
    auto previous_text = text.on_text_changed;
    text.on_text_changed = [this, previous_text, alive](std::string_view value) {
        if (alive.expired()) return;
        if (previous_text) previous_text(value);
        if (!alive.expired()) check();
    };
}

void AbsDialog::checkerImpl(TextEdit& text) {
    const std::weak_ptr<int> alive = lifetime_;
    check();
    auto previous = text.on_changed;
    text.on_changed = [this, previous, alive](std::string_view value) {
        if (alive.expired()) return;
        if (previous) previous(value);
        if (!alive.expired()) check();
    };
}

void AbsDialog::onMod() {
    if (!ignore_mod_) applyEnabled();
}

bool AbsDialog::processCloseRequest() {
    if (closed_ || in_close_event_) return true;
    const std::weak_ptr<int> alive = lifetime_;
    const auto generation = presentation_generation_;
    const auto current = [&] { return !alive.expired() && generation == presentation_generation_; };
    in_close_event_ = true;
    struct Reset {
        AbsDialog* owner;
        std::weak_ptr<int> alive;
        ~Reset() { if (!alive.expired()) owner->in_close_event_ = false; }
    } reset{this, alive};
    const auto notify = [&](std::vector<lib::Action> callbacks) {
        for (const auto& callback : callbacks) {
            if (callback) callback();
            if (!current()) return false;
        }
        return true;
    };
    if (force_cancel_) {
        return_value_ = false;
        closed_ = true;
        window_->result = lib::DialogResult::Cancel;
        (void)notify(closeEvent);
        return true;
    }
    if (return_value_ && !notify(applyEvent)) return true;
    bool cancel = false;
    const bool accepted = close(return_value_, cancel);
    if (!current()) return true;
    return_value_ = accepted;
    if (cancel) return false;
    closed_ = true;
    window_->result = return_value_ ? (applied_ ? lib::DialogResult::Apply : lib::DialogResult::OK)
                                  : lib::DialogResult::Cancel;
    // 通知前に終了状態を確定する。破棄済みの所有元へ後続のコール
    // バックを送ったり、スコープ終了処理から書き込んだりしない。
    std::exception_ptr error;
    try { if (return_value_ && !notify(appliedEvent)) return true; }
    catch (...) { error = std::current_exception(); }
    if (current()) {
        try { (void)notify(closeEvent); }
        catch (...) { if (!error) error = std::current_exception(); }
    }
    if (error) std::rethrow_exception(error);
    return true;
}

void AbsDialog::ok() {
    if (in_close_event_) return;
    return_value_ = true;
    (void)close();
}

void AbsDialog::cancel() {
    if (in_close_event_) return;
    // 以前のOKが受理後に派生クラスで拒否された状態を、意図的に解除する。
    // Dは_retを保持するため、取消ボタンで再び適用を試みる場合がある。
    return_value_ = false;
    (void)close();
}

bool AbsDialog::apply() {
    return true;
}

bool AbsDialog::close(bool ok, bool& cancel) {
    if (has_apply_) {
        if (ok) {
            ok = apply();
            if (!ok) cancel = true;
        }
        return ok;
    }
    cancel = false;
    return close(ok);
}

bool AbsDialog::close(bool ok) {
    return ok;
}

void AbsDialog::opened() {}

void AbsDialog::calculateBounds(bool set_location) {
    if (clear_setup_window_) {
        auto clear = std::move(clear_setup_window_);
        clear(false);
    }

    auto current = window_->bounds();
    const float previous_scale = std::max(1.0F, window_->bounds_ui_text_scale);
    current.width /= previous_scale;
    current.height /= previous_scale;
    const auto preferred = preferredSize();
    float width = positiveOr(current.width, preferred.width);
    float height = positiveOr(current.height, preferred.height);

    if (size_) {
        if (size_->width() > 0) width = static_cast<float>(size_->width());
        if (size_->height() > 0) height = static_cast<float>(size_->height());
    }
    width = std::max(kMinimumSize, width);
    height = std::max(kMinimumSize, height);
    // 保存済みの高さで、計測したフッターや下側余白を使い切らないようにする。
    height = std::max(height, preferred.height);
    if (window_->minimum_size) {
        width = std::max(width, window_->minimum_size->width);
        height = std::max(height, window_->minimum_size->height);
    }

    float x = current.x;
    float y = current.y;
    const auto parent_bounds = parent_ ? parent_->bounds() : lib::Rect{};
    auto* window_size = dynamic_cast<WSize*>(size_);

    if (set_location) {
        if (parent_) {
            x = (window_size && window_size->x() >= 0)
                    ? parent_bounds.x + static_cast<float>(window_size->x())
                    : parent_bounds.x +
                          (parent_bounds.width - width) / 2.0F;
            y = (window_size && window_size->y() >= 0)
                    ? parent_bounds.y + static_cast<float>(window_size->y())
                    : parent_bounds.y +
                          (parent_bounds.height - height) / 2.0F;
        } else {
            x = (window_size && window_size->x() >= 0)
                    ? static_cast<float>(window_size->x())
                    : current.x;
            y = (window_size && window_size->y() >= 0)
                    ? static_cast<float>(window_size->y())
                    : current.y;
        }
    }

    if (window_size) window_->maximized = window_size->maximized();

    if (services_.gui) {
        const auto screens = services_.gui->screens();
        if (!screens.empty()) {
            const auto parent_center_x = parent_bounds.x + parent_bounds.width / 2.0F;
            const auto parent_center_y = parent_bounds.y + parent_bounds.height / 2.0F;
            const auto selected = std::find_if(
                screens.begin(), screens.end(),
                [parent_center_x, parent_center_y](const lib::ScreenInfo& screen) {
                    const auto& area = screen.work_area;
                    return parent_center_x >= area.x &&
                           parent_center_x < area.x + area.width &&
                           parent_center_y >= area.y &&
                           parent_center_y < area.y + area.height;
                });
            const auto& work = (selected == screens.end() ? screens.front() : *selected).work_area;
            if (work.width > 0.0F) width = std::min(width, work.width);
            if (work.height > 0.0F) height = std::min(height, work.height);
            if (work.width > 0.0F)
                x = std::clamp(x, work.x, work.x + work.width - width);
            if (work.height > 0.0F)
                y = std::clamp(y, work.y, work.y + work.height - height);
        }
    }

    window_->bounds_ui_text_scale = 1.0F;
    window_->set_bounds({x, y, width, height});
    if (!lib::editor_window_host_options().all_mode && size_) {
        // 移動・サイズ変更時にも、通常状態の位置とサイズを保存する。
        const auto active = std::make_shared<bool>(true);
        const std::weak_ptr<int> alive = lifetime_;
        const std::weak_ptr<lib::Window> weak_window = window_;
        const auto prior = window_->on_bounds_changed;
        struct BoundsListener {
            std::shared_ptr<bool> active;
            std::weak_ptr<int> alive;
            std::function<void(lib::Rect)> prior;
            std::function<void()> save;
            void operator()(lib::Rect value) const {
                if (prior) prior(value);
                if (*active && !alive.expired()) save();
            }
        };
        window_->on_bounds_changed = BoundsListener{active, alive, prior, [this] { saveWin(); }};
        clear_setup_window_ = [this, active, weak_window, prior](bool save) {
            if (!*active) return;
            *active = false;
            if (save) saveWin();
            if (const auto current = weak_window.lock()) {
                const auto* own = current->on_bounds_changed.target<BoundsListener>();
                if (own && own->active == active) current->on_bounds_changed = prior;
            }
        };
    } else {
        clear_setup_window_ = [this](bool save) {
            if (save) saveWin();
        };
    }

    const auto bounds = window_->bounds();
    for (const auto& callback : calcBoundsEvent) {
        if (callback) callback(rounded(bounds.x), rounded(bounds.y),
                               rounded(bounds.width), rounded(bounds.height));
    }
}

lib::Size AbsDialog::preferredSize() const {
    lib::Size result{kDefaultWidth, kDefaultHeight};
    const float scale = window_ ? std::max(1.0F, window_->bounds_ui_text_scale) : 1.0F;
    if (window_ && window_->bounds().width > 0.0F)
        result.width = window_->bounds().width / scale;
    if (window_ && window_->bounds().height > 0.0F)
        result.height = window_->bounds().height / scale;

    if (window_) {
        const auto measured = lib::minimum_ui_size(window_);
        result.width = std::max(result.width, measured.width / lib::editor_ui_text_scale());
        result.height = std::max(result.height, measured.height / lib::editor_ui_text_scale());
    }
    return result;
}

int AbsDialog::rounded(float value) noexcept {
    if (!std::isfinite(value)) return 0;
    const auto rounded_value = std::lround(value);
    if (rounded_value < static_cast<long>(std::numeric_limits<int>::min()))
        return std::numeric_limits<int>::min();
    if (rounded_value > static_cast<long>(std::numeric_limits<int>::max()))
        return std::numeric_limits<int>::max();
    return static_cast<int>(rounded_value);
}

lib::String AbsDialog::comboText(const ComboBox& combo) {
    if (combo.editable && !combo.edit_text.empty()) return combo.edit_text;
    if (combo.selected_index &&
        *combo.selected_index < combo.items.size())
        return combo.items[*combo.selected_index].text;
    return combo.edit_text;
}

}  // namespace cwxeditor::cwx::editor::gui::dwt::absdialog
