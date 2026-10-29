#include "cwxeditor_src/lib/ui_window_stack.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/incsearch.hpp"
#include "cwxeditor_src/core/cwx/perf.hpp"
#include "cwxeditor_src/core/cwx/enumutils.hpp"
#include "cwxeditor_src/lib/d_regex.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/timebar.hpp"
#include "cwxeditor_src/lib/ui_actions.hpp"
#include "cwxeditor_src/lib/ui_metrics.hpp"
#include "cwxeditor_src/lib/ui_window_lifecycle.hpp"
#include "3rdparty/srell/srell.hpp"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <exception>
#include <limits>
#include <utility>

namespace cwxeditor::cwx::editor::gui::dwt::incsearch {

namespace {

[[nodiscard]] std::size_t typeIndex(const SearchType type) noexcept {
    return static_cast<std::size_t>(static_cast<std::int32_t>(type));
}

[[nodiscard]] bool containsGraphical(std::string_view text) {
    // D版の std.uni.isGraphical と同様、L、M、N、P、S、Zs（空白を含
    // む）を使う。 ネイティブの文字入力は複数のスカラー値を一度に確定できるため、バッファ全体を
    // 受け付けるか拒否する 既存の方針を維持し、選別・分割してD版のキーイベントに変換しない。
    static const srell::u8cregex graphical(R"([\p{L}\p{M}\p{N}\p{P}\p{S}\p{Zs}])");
    return srell::regex_search(text.begin(), text.end(), graphical);
}

[[nodiscard]] bool hasModifier(const ::cwxeditor::lib::Modifiers value,
                                const ::cwxeditor::lib::Modifiers modifier) noexcept {
    return (static_cast<std::uint32_t>(value) &
            static_cast<std::uint32_t>(modifier)) != 0U;
}

[[nodiscard]] bool widget_is_in_parent_tree(const ::cwxeditor::lib::Widget* root,
                                            const ::cwxeditor::lib::Widget* child) {
    if (!root || !child) return false;
    if (root == child) return true;
    std::vector<const ::cwxeditor::lib::Widget*> pending;
    for (const auto* parent : child->containing_parents()) pending.push_back(parent);
    std::vector<const ::cwxeditor::lib::Widget*> visited;
    while (!pending.empty()) {
        const auto* current = pending.back(); pending.pop_back();
        if (!current || std::find(visited.begin(), visited.end(), current) != visited.end()) continue;
        if (current == root) return true;
        visited.push_back(current);
        for (const auto* parent : current->containing_parents()) pending.push_back(parent);
    }
    return false;
}

}  // namespace

// D版互換の共通UTF-32正規表現ラッパーは、公開GUIヘッダーには含
// めない。 UTF-8の各バイトを別々の文字として照合することを防ぐ。
class IncSearch::UnicodePattern {
public:
    // D版は regex(to!dstring(_text.getText()), "i")
    // を呼ぶため、 パターンが (?-i) を指定しない限り、大文字小文字を区別せず照合する。
    explicit UnicodePattern(const std::string& query)
        : pattern_(::cwxeditor::cwx::enumutils::toUtf32(query)) {}

    bool matches(std::string_view value) const {
        try {
            return !pattern_.matchAll(
                ::cwxeditor::cwx::enumutils::toUtf32(value)).empty();
        } catch (const std::exception&) {
            return false;
        }
    }
private:
    ::cwxeditor::lib::DRegex pattern_;
};

IncSearch::IncSearch(Services services, Widget* parent, Enabled enabled,
                     std::vector<AdditionMatcher> addition,
                     IncSearchSettings settings)
    : services_(std::move(services)), parent_(parent), enabled_(std::move(enabled)),
      settings_(std::move(settings)), text_backend_(services_.text.get()),
      addition_(std::move(addition)), addition_enabled_(addition_.size(), true) {
    search_type_ = normalizedSearchType(settings_.incrementalSearchType);
    settings_.incrementalSearchType = static_cast<std::int32_t>(search_type_);
    if (parent_) parent_lifetime_ = parent_->committed_text_input.lifetime;
    const std::weak_ptr<int> life = callback_lifetime_;
    focus_observer_ = lib::add_focus_observer([this, life](const lib::WidgetPtr& target) {
        if (life.expired()) return;
        auto* const observed_parent = parent_;
        const auto observed_parent_lifetime = parent_lifetime_;
        registerCommittedTextInput();
        if (life.expired()) return;
        if (parent_ != observed_parent ||
            (observed_parent && (observed_parent_lifetime.expired() ||
             parent_lifetime_.lock() != observed_parent_lifetime.lock()))) return;
        if (!services_.gui || services_.gui->focused_widget() != target) return;
        handleFocusChanged(target.get());
    });
    registerCommittedTextInput();
}

IncSearch::IncSearch(Widget* parent, Enabled enabled,
                     std::vector<AdditionMatcher> addition,
                     IncSearchSettings settings, Services services)
    : IncSearch(std::move(services), parent, std::move(enabled),
                std::move(addition), std::move(settings)) {}

IncSearch::~IncSearch() {
    callback_lifetime_.reset();
    for (auto& owned : committed_text_input_registrations_) owned.lifetime.reset();
    committed_text_input_registrations_.clear();
    lib::remove_focus_observer(focus_observer_);
    focus_observer_ = 0;
    // 登録済みの Start タスクは weak_ptr だけを保持する。ここで強参照
    // の所有者を解放し、 破棄済みの IncSearch を呼び出さないようにする。
    start_.reset();
    // バックエンドや開いているポップアップのスナップショットは、所有者より長くコントロールを保
    // 持する場合がある。 そのコールバックに、破棄済みの検索オブジェクトへの参照を残さない。
    if (text_) {
        text_->on_changed = {};
        text_->on_key_down = {};
    }
    if (type_) type_->on_selection_changed = {};
    if (close_button_) close_button_->on_click = {};
    for (const auto& check : addition_checkboxes_)
        if (check) check->on_changed = {};
    if (window_) {
        window_->on_close_requested = {};
        const auto window=window_;const auto gui=services_.gui;
        window->set_visible(false);
        if(gui)gui->close(window);
    }
}

bool IncSearch::match(const std::string_view value,
                      const AdditionalData* const additional_data) const {
    ::cwxeditor::cwx::perf::TraceScope trace_entry_1(__FILE__, __LINE__);
    // D版のコントロールと同様、非表示・未初期化の検索は、 IncSearch
    // を絞り込み条件として使う呼び出し元に影響を与えない。
    if (!window_ || !window_->visible()) return true;

    const auto query = searchText();
    if (query.empty()) return matchAdditional(additional_data);
    if (!wildcard_) return matchAdditional(additional_data);

    bool matched = false;
    switch (search_type_) {
    case SearchType::Contains:
        matched = folded(value).find(folded(query)) != std::string::npos;
        break;
    case SearchType::Wildcard:
        matched = wildcard_->match(value);
        break;
    case SearchType::Regex:
        if (!regex_error_ && regex_) matched = regex_->matches(value);
        break;
    }
    return matched && matchAdditional(additional_data);
}

bool IncSearch::isSearching() const {
    ::cwxeditor::cwx::perf::TraceScope trace_entry_2(__FILE__, __LINE__);
    if (!window_ || !window_->visible()) return false;
    for (const auto enabled : addition_enabled_) {
        ::cwxeditor::cwx::perf::TraceScope trace_nested_54(__FILE__, __LINE__);
        if (!enabled) return true;
    }
    return !searchText().empty();
}

bool IncSearch::matchAdditional(const AdditionalData* const additional_data) const {
    ::cwxeditor::cwx::perf::TraceScope trace_entry_3(__FILE__, __LINE__);
    if (additional_data == nullptr) return true;
    for (std::size_t index = 0; index < addition_.size(); ++index) {
        ::cwxeditor::cwx::perf::TraceScope trace_nested_61(__FILE__, __LINE__);
        if (addition_enabled_[index]) continue;
        const auto& predicate = addition_[index].match;
        if (predicate && predicate(*additional_data)) {
            ::cwxeditor::cwx::perf::TraceScope trace_nested_62(__FILE__, __LINE__);
            return false;
        }
    }
    return true;
}

void IncSearch::settings(IncSearchSettings value) {
    settings_ = std::move(value);
    search_type_ = normalizedSearchType(settings_.incrementalSearchType);
    settings_.incrementalSearchType = static_cast<std::int32_t>(search_type_);
    if (!type_) {
        if (window_) window_->title = settings_.windowTitle;
        return;
    }

    std::vector<::cwxeditor::lib::ChoiceItem> items{
        {settings_.containsLabel, {}, 0U, true},
        {settings_.wildcardLabel, {}, 0U, true},
        {settings_.regexLabel, {}, 0U, true},
    };
    (void)::cwxeditor::lib::replace_combo_items(
        type_, std::move(items), typeIndex(search_type_));
    if (window_) window_->title = settings_.windowTitle;
    resize();
}

const std::string& IncSearch::searchText() const noexcept {
    static const std::string empty;
    return text_ ? text_->text : empty;
}

std::string IncSearch::requestedSearchText() const {
    if (start_) return start_->text();
    return searchText();
}

void IncSearch::setSearchText(std::string value) {
    if (!initialized_) initialize();
    if (!open_ || !window_ || !window_->visible()) {
        startIncSearch(std::move(value));
        return;
    }
    if (!text_) return;
    const bool changed=text_->text!=value;
    text_->text = std::move(value);
    text_->selection_start = text_->text.size();
    text_->selection_length = 0U;
    if (changed) modified();
}

void IncSearch::setSearchType(const SearchType type) {
    search_type_ = validSearchType(type) ? type : SearchType::Contains;
    settings_.incrementalSearchType = static_cast<std::int32_t>(search_type_);
    if (type_)
        (void)::cwxeditor::lib::select_combo_item(
            *type_, typeIndex(search_type_), false);
    const auto selected=search_type_;
    const std::weak_ptr<int> life=callback_lifetime_;
    const auto persist=searchTypeChanged;
    if(persist)persist(selected);
    if(life.expired() || search_type_!=selected)return;
    if (open_ && window_ && window_->visible()) notifyModified();
}

bool IncSearch::additionEnabled(const std::size_t index) const {
    return addition_enabled_.at(index);
}

void IncSearch::setAdditionEnabled(const std::size_t index, const bool enabled) {
    addition_enabled_.at(index) = enabled;
    if (index < addition_checkboxes_.size() && addition_checkboxes_[index]) {
        addition_checkboxes_[index]->checked = enabled;
    }
    if (open_ && window_ && window_->visible()) notifyModified();
}

void IncSearch::startIncSearch(std::string first) {
    const std::weak_ptr<int> life=callback_lifetime_;
    if (!initialized_) initialize();
    if (life.expired() || !window_ || !text_) return;
    const auto revision=++presentation_revision_;
    const auto window=window_;const auto text=text_;const auto gui=services_.gui;
    if (parent_ && gui) {
        const auto current_focus = gui->focused_widget();
        if (current_focus.get() == parent_) {
            // D版の focusLost と同様、一時的な IncSearch ウ
            // ィンドウをまたいで、編集中の表に追従する。 表示用のコントロールで
            // は、直接の論理的な所有者情報を通じて、その表を公開する場合がある。
            const auto logical_owner = current_focus->focus_transaction_owner.lock();
            inline_edit_focus_owner_ = logical_owner ? logical_owner : current_focus;
        }
    }
    applyInlineEditFocusOwner();
    const bool opening=!window->visible();
    if(opening) {
        std::shared_ptr<Window> owner;
        if(gui && parent_)owner=gui->window_owner(std::shared_ptr<Widget>(parent_,[](Widget*){}));
        if(life.expired() || presentation_revision_!=revision)return;
        // WindowStack は論理的に関連するウィンドウを閉じると親の検索
        // 情報を消す。 再設定前にポップアップの許可済みの所有者を保持する。 最
        // 初から所有者のない表示側は有効だが、以前の所有者が失効した場合は、 保
        // 持されたコールバックが所有者なしで Show を実行しないようにする。
        if(!owner) {
            owner=window->owner.lock();
            const std::weak_ptr<Window> empty_owner;
            const bool previously_owned=window->owner.owner_before(empty_owner) ||
                empty_owner.owner_before(window->owner);
            if(!owner && previously_owned)return;
        }
        // この所有者を開き直す操作や、新しい有効な所有者を解決する操作は引き続き許可する。
        if(owner && !owner->visible())return;
        // 借用した親の寸法を読む前に、表示を許可できるか確認する。 以前の所有者・
        // コントロールが失効した表示側は、reposition() より前に戻る。
        text->text=std::move(first);text->selection_start=text->text.size();text->selection_length=0;
        reposition();
        if(life.expired() || presentation_revision_!=revision)return;
        if(owner && !owner->visible())return;
        // 単独版と ALL で、一つの表示処理により既存の検索コントロールを実体化する。 単
        // 独版ではポップアップの配置を維持し、ALL ではホストによる中央配置を維持する。
        lib::configure_owned_window(window,owner,lib::OwnedWindowKind::Modeless,
            lib::editor_window_host_options().all_mode ? lib::OwnedWindowCenter::OwnerOrHost : lib::OwnedWindowCenter::Never);
        open_=true;
        window->set_visible(true);
        if(life.expired() || presentation_revision_!=revision)return;
        if(owner && !owner->visible()) {
            open_=false;
            window->set_visible(false);
            return;
        }
        if(gui)gui->show(window);
        if(life.expired() || presentation_revision_!=revision || !open_ || !window->visible())return;
    } else open_=true;
    if(gui)gui->focus(text);
    if(life.expired() || presentation_revision_!=revision || !open_ || !window->visible())return;
    if(opening && !text->text.empty())modified();
}

void IncSearch::close() {
    ::cwxeditor::cwx::perf::TraceScope trace_entry_5(__FILE__, __LINE__);
    if (!initialized_) initialize();
    if (!window_ || !window_->visible()) return;

    ::cwxeditor::cwx::perf::TraceScope trace_nested_353(__FILE__, __LINE__);
    const std::weak_ptr<int> close_life=callback_lifetime_;
    const auto close_revision=++presentation_revision_;
    const auto window=window_;const auto gui=services_.gui;
    // 非表示にする前に例外扱いを解除し、一時的な検索ウィンドウからフォーカ
    // スが離れる際、 通常のフォーカス処理で編集を確定できるようにする。
    preserveInlineEditFocus({});
    if(gui)gui->hide(window);
    else window->set_visible(false);
    if(close_life.expired() || presentation_revision_!=close_revision)return;
    wildcard_.reset();
    regex_.reset();
    regex_error_ = false;

    bool changed = false;
    for (std::size_t index = 0; index < addition_enabled_.size(); ++index) {
        ::cwxeditor::cwx::perf::TraceScope trace_nested_357(__FILE__, __LINE__);
        if (!addition_enabled_[index]) changed = true;
        addition_enabled_[index] = true;
        if (index < addition_checkboxes_.size() && addition_checkboxes_[index]) {
            addition_checkboxes_[index]->checked = true;
        }
    }
    if (text_ && !text_->text.empty()) {
        ::cwxeditor::cwx::perf::TraceScope trace_nested_363(__FILE__, __LINE__);
        text_->text.clear();
        text_->selection_start = 0U;
        text_->selection_length = 0U;
        changed = true;
    }
    // D版と同様、_open を変える前に modEvent を呼び、 最後
    // のリセット通知でもコールバックが有効な検索状態を参照できるようにする。
    const std::weak_ptr<int> lifetime = callback_lifetime_;
    if (changed) {
        ::cwxeditor::cwx::perf::TraceScope trace_nested_367(__FILE__, __LINE__);
        notifyModified();
    }
    if (lifetime.expired() || presentation_revision_!=close_revision) return;
    open_ = false;
}

void IncSearch::resize() {
    if (!initialized_) initialize();
    if (!window_) return;
    reposition();
}

void IncSearch::checkEnabled() {
    checkClose();
}

bool IncSearch::automaticStartEnabled() const {
    // 提供処理の実行中に設定が置き換わる場合があるため、 このコールバックの寿
    // 命を、取得元の std::function オブジェクトから独立させる。
    const auto provider = settings_.startIncrementalSearchWhenKeyDownProvider;
    return provider ? provider() : settings_.startIncrementalSearchWhenKeyDown;
}

void IncSearch::registerCommittedTextInput() {
    // 親の構築時や再接続時に、呼び出し元がこの弱参照の所有者
    // を設定する。 失効していたら、借用した親を参照しない。
    const std::weak_ptr<int> search_lifetime = callback_lifetime_;
    auto* const root_parent = parent_;
    const std::weak_ptr<const int> root_lifetime = parent_lifetime_;
    if (!root_parent || root_lifetime.expired()) return;
    if (root_lifetime.lock() != root_parent->committed_text_input.lifetime) return;
    const auto root_is_current = [this, search_lifetime, root_parent, root_lifetime] {
        return !search_lifetime.expired() && !root_lifetime.expired() &&
            parent_ == root_parent && parent_lifetime_.lock() == root_lifetime.lock();
    };

    const auto eligible_view = [](const Widget* value) {
        return dynamic_cast<const ::cwxeditor::lib::TableView*>(value) ||
            dynamic_cast<const ::cwxeditor::lib::TreeView*>(value) ||
            dynamic_cast<const ::cwxeditor::lib::ListView*>(value) ||
            value->supports_item_view_text_input();
    };
    std::vector<Widget*> views;
    if (eligible_view(root_parent)) {
        if (!root_is_current()) return;
        views.push_back(root_parent);
    } else if (auto* container = dynamic_cast<::cwxeditor::lib::Container*>(root_parent)) {
        std::vector<Widget*> pending;
        for (const auto& child : container->children())
            if (child && !std::dynamic_pointer_cast<::cwxeditor::lib::Window>(child))
                pending.push_back(child.get());
        std::vector<Widget*> visited;
        while (!pending.empty()) {
            auto* current = pending.back(); pending.pop_back();
            if (!current || std::find(visited.begin(), visited.end(), current) != visited.end()) continue;
            visited.push_back(current);
            const bool is_eligible = eligible_view(current);
            if (!root_is_current()) return;
            if (is_eligible) views.push_back(current);
            if (auto* nested = dynamic_cast<::cwxeditor::lib::Container*>(current)) {
                for (const auto& child : nested->children())
                    if (child && !std::dynamic_pointer_cast<::cwxeditor::lib::Window>(child))
                        pending.push_back(child.get());
            }
        }
    }

    struct ViewCandidate {
        Widget* widget{};
        std::weak_ptr<const int> lifetime;
    };
    std::vector<ViewCandidate> candidates;
    candidates.reserve(views.size());
    for (auto* view : views) {
        if (!view) continue;
        candidates.push_back({view, view->committed_text_input.lifetime});
    }

    // 公開の current() コールバックは、この IncSearch を破棄したり親を付け替え
    // たりする場合がある。 入れ子の変更でイテレーターが無効にならないよう、スナップショットを使う。
    auto owned_snapshot = committed_text_input_registrations_;
    std::vector<OwnedCommittedTextInputRegistration> retained;
    retained.reserve(committed_text_input_registrations_.size());
    for (auto& owned : owned_snapshot) {
        if (!root_is_current()) return;
        const auto candidate = std::find_if(candidates.begin(), candidates.end(),
            [&owned](const auto& value) { return value.widget == owned.widget; });
        if (!owned.widget || !owned.lifetime || !owned.registration ||
            candidate == candidates.end() || candidate->lifetime.expired()) {
            owned.lifetime.reset();
            continue;
        }
        const auto registration = owned.registration;
        const auto view_lifetime = candidate->lifetime;
        const auto parent_lifetime = owned.registration->parent_lifetime.lock();
        const auto owner_lifetime = owned.registration->owner_lifetime.lock();
        const auto current = registration->current;
        const bool is_current = !current || current();
        if (!root_is_current()) return;
        if (view_lifetime.expired()) {
            owned.lifetime.reset();
            continue;
        }
        if (!parent_lifetime || parent_lifetime != owned.widget->committed_text_input.lifetime ||
            owner_lifetime != owned.lifetime ||
            owned.widget->committed_text_input.registration != owned.registration ||
            !is_current) {
            owned.lifetime.reset();
            continue;
        }
        retained.push_back(owned);
    }
    if (!root_is_current()) return;
    committed_text_input_registrations_ = std::move(retained);

    for (const auto& candidate : candidates) {
        if (!root_is_current()) return;
        if (candidate.lifetime.expired()) continue;
        auto* const registered_parent = candidate.widget;
        if (!registered_parent) continue;
        auto& state = registered_parent->committed_text_input;
        const auto owned = std::find_if(committed_text_input_registrations_.begin(),
            committed_text_input_registrations_.end(), [registered_parent](const auto& value) {
                return value.widget == registered_parent;
            });
        if (owned != committed_text_input_registrations_.end()) continue;
        const auto existing = state.registration;
        if (existing) {
            const bool owner_is_live = !existing->owner_lifetime.expired();
            const bool parent_is_live = !existing->parent_lifetime.expired();
            bool is_current = false;
            if (owner_is_live && parent_is_live) {
                const auto current = existing->current;
                is_current = !current || current();
                if (!root_is_current()) return;
                if (candidate.lifetime.expired()) continue;
                if (state.registration != existing) continue;
            }
            if (owner_is_live && parent_is_live && is_current) continue;
        }
        auto registration_lifetime = std::make_shared<int>(0);
        const std::weak_ptr<int> registration_lifetime_weak = registration_lifetime;
        const std::weak_ptr<const int> view_lifetime = state.lifetime;
        auto registration = std::make_shared<::cwxeditor::lib::CommittedTextInputRegistration>();
        registration->owner_lifetime = registration_lifetime;
        registration->parent_lifetime = state.lifetime;
        registration->current = [root_parent, registered_parent, root_lifetime,
                                 view_lifetime, registration_lifetime_weak] {
            return !root_lifetime.expired() && !view_lifetime.expired() &&
                !registration_lifetime_weak.expired() &&
                widget_is_in_parent_tree(root_parent, registered_parent);
        };
        registration->anchor = [this, root_parent, registered_parent, search_lifetime,
                                 registration_lifetime_weak, root_lifetime, view_lifetime]()
            -> std::optional<::cwxeditor::lib::Rect> {
            if (search_lifetime.expired() || registration_lifetime_weak.expired() ||
                root_lifetime.expired() || view_lifetime.expired() ||
                !widget_is_in_parent_tree(root_parent, registered_parent)) return {};
            const bool enabled = automaticStartEnabled();
            // 設定のコールバックが所有者を再構築する場合がある。 借用したどち
            // らのオブジェクトも、読む前にすべての弱参照の寿命を再確認する。
            if (search_lifetime.expired() || registration_lifetime_weak.expired() ||
                root_lifetime.expired() || view_lifetime.expired()) return {};
            if (!enabled || open_ || !registered_parent->visible() ||
                !registered_parent->enabled()) return {};
            // この同期呼び出し用の別名により、共通の位置計算処理は、
            // 表全体の下端ではなく表示中の主選択行を基準にできる。
            const ::cwxeditor::lib::WidgetPtr view(
                registered_parent, [](::cwxeditor::lib::Widget*) {});
            return ::cwxeditor::lib::item_view_cursor_geometry(
                view, ::cwxeditor::lib::scaled_ui_metric(
                    ::cwxeditor::lib::kEditorUiMetrics.row_height));
        };
        registration->on_text_input = [this, root_parent, registered_parent, search_lifetime,
                                       registration_lifetime_weak, root_lifetime, view_lifetime](
            ::cwxeditor::lib::TextInputEvent& event) {
            if (search_lifetime.expired() || registration_lifetime_weak.expired() ||
                root_lifetime.expired() || view_lifetime.expired() ||
                !widget_is_in_parent_tree(root_parent, registered_parent)) return false;
            return handleHostTextInput(event, registered_parent);
        };
        registration->on_key_down = [this, root_parent, registered_parent, search_lifetime,
                                     registration_lifetime_weak, root_lifetime, view_lifetime](
            ::cwxeditor::lib::KeyEvent& event) {
            if (search_lifetime.expired() || registration_lifetime_weak.expired() ||
                root_lifetime.expired() || view_lifetime.expired() ||
                !widget_is_in_parent_tree(root_parent, registered_parent)) return false;
            return handleHostKeyDown(event, registered_parent);
        };
        state.registration = registration;
        committed_text_input_registrations_.push_back(
            {registered_parent, std::move(registration_lifetime), std::move(registration)});
    }
}

bool IncSearch::ownsCommittedTextInputRegistration(const Widget* source) const {
    const std::weak_ptr<int> search_lifetime = callback_lifetime_;
    auto* const root_parent = parent_;
    const std::weak_ptr<const int> root_lifetime = parent_lifetime_;
    if (!source || root_lifetime.expired() ||
        !widget_is_in_parent_tree(root_parent, source)) return false;
    const std::weak_ptr<const int> source_lifetime =
        source->committed_text_input.lifetime;
    const auto registration = source->committed_text_input.registration;
    if (!registration || registration->owner_lifetime.expired() ||
        registration->parent_lifetime.expired() ||
        registration->parent_lifetime.lock() != source_lifetime.lock()) return false;
    const auto current = registration->current;
    const bool is_current = !current || current();
    if (search_lifetime.expired() || root_lifetime.expired() ||
        source_lifetime.expired() || parent_ != root_parent ||
        parent_lifetime_.lock() != root_lifetime.lock()) return false;
    if (!is_current || !widget_is_in_parent_tree(root_parent, source) ||
        source->committed_text_input.registration != registration ||
        registration->parent_lifetime.lock() != source->committed_text_input.lifetime) return false;
    return std::any_of(committed_text_input_registrations_.begin(),
        committed_text_input_registrations_.end(), [&](const auto& owned) {
            return owned.widget == source && owned.registration == registration &&
                owned.lifetime && registration->owner_lifetime.lock() == owned.lifetime;
        });
}

bool IncSearch::handleHostTextInput(
    const ::cwxeditor::lib::TextInputEvent& event, const Widget* source) {
    const std::weak_ptr<int> search_lifetime = callback_lifetime_;
    if (!eligibleSource(source)) return false;
    if (parent_ && (parent_lifetime_.expired() || !parent_->visible())) return false;
    registerCommittedTextInput();
    if (search_lifetime.expired()) return false;
    if (parent_ && source && !widget_is_in_parent_tree(parent_, source)) return false;
    if (event.composing) {
        // D版には未確定入力用のコントロールがない。 この IncSearch
        // が登録した共通のネイティブ経路だけで消費し、 編集可能な入
        // 力欄や ComboBox の動作は既存の入力管理側に任せる。
        return ownsCommittedTextInputRegistration(source);
    }
    const bool start_enabled = automaticStartEnabled();
    if (search_lifetime.expired()) return false;
    if (!start_enabled || open_ || event.text.empty()) return false;

    if (!containsGraphical(event.text)) return false;
    ::cwxeditor::cwx::perf::TraceScope trace_nested_134(__FILE__, __LINE__);
    requestStart(event.text);
    return true;
}

bool IncSearch::handleHostKeyDown(const ::cwxeditor::lib::KeyEvent& event,
                                  const Widget* source) {
    const std::weak_ptr<int> search_lifetime = callback_lifetime_;
    if (parent_ && (parent_lifetime_.expired() || !parent_->visible())) return false;
    registerCommittedTextInput();
    if (search_lifetime.expired()) return false;
    if (parent_ && source && source != text_.get() &&
        !widget_is_in_parent_tree(parent_, source)) return false;
    const auto printable = event.key == ::cwxeditor::lib::Key::Space ||
        (event.key >= ::cwxeditor::lib::Key::A &&
         event.key <= ::cwxeditor::lib::Key::Digit9) || !event.printable.empty() ||
        !event.key_character.empty();
    const bool ordinary_character_key = !open_ && printable &&
        !hasModifier(event.modifiers, ::cwxeditor::lib::Modifiers::Control) &&
        !hasModifier(event.modifiers, ::cwxeditor::lib::Modifiers::Alt) &&
        !hasModifier(event.modifiers, ::cwxeditor::lib::Modifiers::Meta);
    const auto published_registration = source
        ? source->committed_text_input.registration : nullptr;
    const std::weak_ptr<const int> source_lifetime = source
        ? std::weak_ptr<const int>(source->committed_text_input.lifetime)
        : std::weak_ptr<const int>{};
    bool has_current_published_text_route = false;
    if (ordinary_character_key && published_registration &&
        source->committed_text_input.published()) {
        const auto owner_lifetime = published_registration->owner_lifetime;
        const auto parent_lifetime = published_registration->parent_lifetime;
        if (!owner_lifetime.expired() && !parent_lifetime.expired() &&
            parent_lifetime.lock() == source->committed_text_input.lifetime) {
            // 公開コールバックは呼び出し前に複製する。 current() の提
            // 供処理は、実行中にどちらの端点も付け替え・無効化する可能性がある。
            auto* const route_parent = parent_;
            const std::weak_ptr<const int> route_parent_lifetime = parent_lifetime_;
            const auto current = published_registration->current;
            const bool route_current = !current || current();
            if (search_lifetime.expired() || source_lifetime.expired() ||
                (route_parent && route_parent_lifetime.expired()) ||
                parent_ != route_parent ||
                parent_lifetime_.lock() != route_parent_lifetime.lock()) return false;
            if (published_registration->owner_lifetime.expired() ||
                published_registration->parent_lifetime.expired() ||
                published_registration->owner_lifetime.owner_before(owner_lifetime) ||
                owner_lifetime.owner_before(published_registration->owner_lifetime) ||
                published_registration->parent_lifetime.owner_before(parent_lifetime) ||
                parent_lifetime.owner_before(published_registration->parent_lifetime) ||
                published_registration->parent_lifetime.lock() !=
                    source->committed_text_input.lifetime ||
                source->committed_text_input.registration != published_registration ||
                !source->committed_text_input.published()) return false;
            has_current_published_text_route = route_current;
        }
    }
    if (has_current_published_text_route) {
        // D版と同様、表示フィルターからは Event.doit を変えずに検索を予約
        // する。 有効なネイティブ経路が文字を届ける場合は、この監視側の代替処理を使わ
        // ない。 ただし KeyDown は消費せず、コントロールの標準処理に渡す。
        return false;
    }
    if (source == text_.get() && open_) {
        const bool start_enabled = automaticStartEnabled();
        if (search_lifetime.expired()) return false;
        if (start_enabled &&
            (event.key == ::cwxeditor::lib::Key::Backspace ||
             event.key == ::cwxeditor::lib::Key::Delete) && searchText().empty()) {
            close();
            return true;
        }
    }
    const bool start_enabled = automaticStartEnabled();
    if (search_lifetime.expired()) return false;
    if (!start_enabled || open_ ||
        hasModifier(event.modifiers, ::cwxeditor::lib::Modifiers::Control) ||
        hasModifier(event.modifiers, ::cwxeditor::lib::Modifiers::Alt) ||
        hasModifier(event.modifiers, ::cwxeditor::lib::Modifiers::Meta) ||
        !eligibleSource(source)) {
        return false;
    }

    if (!event.key_character.empty() && containsGraphical(event.key_character)) {
        // 登録済みのネイティブ文字入力先は上で処理済み。 この経路は、共通
        // の監視側の代替処理に、キーボード配列から直接得た文字だけを渡す。
        const ::cwxeditor::lib::TextInputEvent input{event.key_character, false};
        return handleHostTextInput(input, source);
    }

    char character = '\0';
    switch (event.key) {
    case ::cwxeditor::lib::Key::A: character = 'a'; break;
    case ::cwxeditor::lib::Key::B: character = 'b'; break;
    case ::cwxeditor::lib::Key::C: character = 'c'; break;
    case ::cwxeditor::lib::Key::D: character = 'd'; break;
    case ::cwxeditor::lib::Key::E: character = 'e'; break;
    case ::cwxeditor::lib::Key::F: character = 'f'; break;
    case ::cwxeditor::lib::Key::G: character = 'g'; break;
    case ::cwxeditor::lib::Key::H: character = 'h'; break;
    case ::cwxeditor::lib::Key::I: character = 'i'; break;
    case ::cwxeditor::lib::Key::J: character = 'j'; break;
    case ::cwxeditor::lib::Key::K: character = 'k'; break;
    case ::cwxeditor::lib::Key::L: character = 'l'; break;
    case ::cwxeditor::lib::Key::M: character = 'm'; break;
    case ::cwxeditor::lib::Key::N: character = 'n'; break;
    case ::cwxeditor::lib::Key::O: character = 'o'; break;
    case ::cwxeditor::lib::Key::P: character = 'p'; break;
    case ::cwxeditor::lib::Key::Q: character = 'q'; break;
    case ::cwxeditor::lib::Key::R: character = 'r'; break;
    case ::cwxeditor::lib::Key::S: character = 's'; break;
    case ::cwxeditor::lib::Key::T: character = 't'; break;
    case ::cwxeditor::lib::Key::U: character = 'u'; break;
    case ::cwxeditor::lib::Key::V: character = 'v'; break;
    case ::cwxeditor::lib::Key::W: character = 'w'; break;
    case ::cwxeditor::lib::Key::X: character = 'x'; break;
    case ::cwxeditor::lib::Key::Y: character = 'y'; break;
    case ::cwxeditor::lib::Key::Z: character = 'z'; break;
    case ::cwxeditor::lib::Key::Digit0: character = '0'; break;
    case ::cwxeditor::lib::Key::Digit1: character = '1'; break;
    case ::cwxeditor::lib::Key::Digit2: character = '2'; break;
    case ::cwxeditor::lib::Key::Digit3: character = '3'; break;
    case ::cwxeditor::lib::Key::Digit4: character = '4'; break;
    case ::cwxeditor::lib::Key::Digit5: character = '5'; break;
    case ::cwxeditor::lib::Key::Digit6: character = '6'; break;
    case ::cwxeditor::lib::Key::Digit7: character = '7'; break;
    case ::cwxeditor::lib::Key::Digit8: character = '8'; break;
    case ::cwxeditor::lib::Key::Digit9: character = '9'; break;
    case ::cwxeditor::lib::Key::Space: character = ' '; break;
    default: break;
    }
    if (character == '\0') return false;
    if (hasModifier(event.modifiers, ::cwxeditor::lib::Modifiers::Shift) &&
        character >= 'a' && character <= 'z') {
        character = static_cast<char>(character - 'a' + 'A');
    }
    const ::cwxeditor::lib::TextInputEvent input{{character}, false};
    return handleHostTextInput(input, source);
}

void IncSearch::handleFocusChanged(const Widget* focused) {
    ::cwxeditor::cwx::perf::TraceScope trace_focus_entry(__FILE__, __LINE__);
    const std::weak_ptr<int> search_lifetime = callback_lifetime_;
    auto* const observed_parent = parent_;
    const std::weak_ptr<const int> observed_parent_lifetime = parent_lifetime_;
    registerCommittedTextInput();
    if (search_lifetime.expired() || parent_ != observed_parent ||
        (observed_parent && (observed_parent_lifetime.expired() ||
         parent_lifetime_.lock() != observed_parent_lifetime.lock()))) return;
    if (!open_ || !window_ || !window_->visible() || !focused) return;
    if (lib::window_contains_widget(window_, focused)) return;
    // ネイティブのウィンドウスタックがないバックエンドでも、実際のウィジェ
    // ットの親子関係は取得できる。 同期的な祖先比較の間だけ借用し、ポッ
    // プアップの弱参照の所有者欄のために 一時的な共有所有者を作らない。
    const auto ancestorShell=[](const Widget* widget)->const Window* {
        std::vector<const Widget*> pending{widget}, visited;
        while (!pending.empty()) {
            const auto* current=pending.back(); pending.pop_back();
            if (!current || std::find(visited.begin(),visited.end(),current)!=visited.end()) continue;
            visited.push_back(current);
            if (const auto* shell=dynamic_cast<const Window*>(current)) return shell;
            for (const auto* parent:current->containing_parents()) pending.push_back(parent);
        }
        return nullptr;
    };
    const auto owner=window_->owner.lock();
    const auto* parent_shell=owner ? owner.get() : ancestorShell(parent_);
    if (focused==parent_shell || focused==parent_) return;
    const auto parent=std::shared_ptr<Widget>(parent_,[](Widget*){});
    if (parent && lib::contains_widget(parent,focused)) return;
    const Window* target_shell=nullptr;
    std::shared_ptr<Window> backend_owner;
    const std::weak_ptr<int> life=callback_lifetime_;
    if (services_.gui) {
        const auto target=std::shared_ptr<Widget>(const_cast<Widget*>(focused),[](Widget*){});
        backend_owner=services_.gui->window_owner(target);
        if (life.expired()) return;
        target_shell=backend_owner.get();
    }
    if (!target_shell) target_shell=ancestorShell(focused);
    if (!parent_shell || target_shell!=parent_shell) return;
    // D版の rmFocus に対応する、対象となる親の種類の分岐と、フォーカスが親の外にある分岐。
    ::cwxeditor::cwx::perf::TraceScope trace_focus_parent_branch(__FILE__, __LINE__);
    ::cwxeditor::cwx::perf::TraceScope trace_focus_outside_branch(__FILE__, __LINE__);
    close();
}

void IncSearch::setTextBackend(const ::cwxeditor::lib::TextBackend* backend) noexcept {
    text_backend_ = backend;
}

void IncSearch::parent(Widget* value) {
    const std::weak_ptr<int> search_lifetime = callback_lifetime_;
    if (parent_ != value) {
        for (auto& owned : committed_text_input_registrations_) owned.lifetime.reset();
        committed_text_input_registrations_.clear();
        parent_lifetime_.reset();
        parent_ = value;
        if (parent_) parent_lifetime_ = parent_->committed_text_input.lifetime;
        registerCommittedTextInput();
        if (search_lifetime.expired()) return;
    }
    if (initialized_ && window_ && window_->visible()) reposition();
}

void IncSearch::preserveInlineEditFocus(const ::cwxeditor::lib::WidgetPtr& owner) {
    inline_edit_focus_owner_ = owner;
    applyInlineEditFocusOwner();
}

void IncSearch::applyInlineEditFocusOwner() {
    const auto owner = inline_edit_focus_owner_.lock();
    const auto assign = [&owner](const auto& widget) {
        if (widget) widget->focus_transaction_owner = owner;
    };
    assign(window_);
    assign(text_);
    assign(type_);
    assign(close_button_);
    for (const auto& checkbox : addition_checkboxes_) assign(checkbox);
}

void IncSearch::Start::run() {
    ::cwxeditor::cwx::perf::TraceScope trace_entry_6(__FILE__, __LINE__);
    const std::weak_ptr<int> lifetime=owner_.callback_lifetime_;
    const auto revision=owner_.presentation_revision_;
    const auto enabled=owner_.enabled_;
    auto text=std::move(text_);
    if (owner_.start_.get() == this) owner_.start_.reset();
    const bool accepted=!enabled || enabled();
    if (lifetime.expired() || owner_.presentation_revision_!=revision || owner_.start_) return;
    if (accepted) {
        ::cwxeditor::cwx::perf::TraceScope trace_nested_88(__FILE__, __LINE__);
        owner_.startIncSearch(std::move(text));
    }
}

void IncSearch::initialize() {
    ::cwxeditor::cwx::perf::TraceScope trace_entry_7(__FILE__, __LINE__);
    if (initialized_) return;

    // ALL ではインクリメンタル検索を埋め込みのモードレスダイアログとして表示す
    // る。 ALL のホストは、一覧の上に置く通常のポップアップウィンドウを実体化
    // しない。 単独版では、D版と同じ枠なしのポップアップを一覧の上に表示する。
    if (::cwxeditor::lib::editor_window_host_options().all_mode) {
        auto dialog = std::make_shared<::cwxeditor::lib::Dialog>();
        dialog->resizable = false;
        dialog->maximizable = false;
        window_ = std::move(dialog);
    } else if (services_.gui) {
        window_ = services_.gui->create_window();
    }
    if (!window_) window_ = std::make_shared<Window>();
    window_->title = settings_.windowTitle;
    window_->set_visible(false);
    window_->set_layout(std::make_shared<::cwxeditor::lib::GridLayout>());
    if (auto* grid = dynamic_cast<::cwxeditor::lib::GridLayout*>(
            window_->layout().get())) {
        grid->columns = 3U;
        grid->equal_width = false;
        // incsearch.d と同様、ウィンドウのグリッドは余白・間隔をゼロに
        // する。 各コントロールには、共通の倍率適用済み文字寸法を引き続き使う。
        grid->spacing = 0.0F;
        grid->padding = {};
    }

    auto makeTextEdit = [this]() {
        if (services_.gui) {
            auto widget = services_.gui->create_component(
                ::cwxeditor::lib::ComponentKind::TextEdit);
            if (auto result = std::dynamic_pointer_cast<TextEdit>(std::move(widget))) {
                return result;
            }
        }
        return std::make_shared<TextEdit>();
    };
    auto makeCombo = [this]() {
        if (services_.gui) {
            auto widget = services_.gui->create_component(
                ::cwxeditor::lib::ComponentKind::ComboBox);
            if (auto result = std::dynamic_pointer_cast<ComboBox>(std::move(widget))) {
                return result;
            }
        }
        return std::make_shared<ComboBox>();
    };
    auto makeButton = [this]() {
        if (services_.gui) {
            auto widget = services_.gui->create_component(
                ::cwxeditor::lib::ComponentKind::Button);
            if (auto result = std::dynamic_pointer_cast<::cwxeditor::lib::Button>(
                    std::move(widget))) {
                return result;
            }
        }
        return std::make_shared<::cwxeditor::lib::Button>();
    };
    auto makeCheckBox = [this]() {
        if (services_.gui) {
            auto widget = services_.gui->create_component(
                ::cwxeditor::lib::ComponentKind::CheckBox);
            if (auto result = std::dynamic_pointer_cast<CheckBox>(std::move(widget))) {
                return result;
            }
        }
        return std::make_shared<CheckBox>();
    };

    text_ = makeTextEdit();
    text_->multiline = false;
    text_->read_only = false;
    text_->wrap = false;
    text_->on_changed = [this,life=std::weak_ptr<int>(callback_lifetime_)](const std::string_view value) {
        if(life.expired())return;
        if (!text_) return;
        text_->text.assign(value.data(), value.size());
        modified();
    };
    text_->on_key_down = [this,life=std::weak_ptr<int>(callback_lifetime_)](::cwxeditor::lib::KeyEvent& event) {
        if(life.expired())return;
        ::cwxeditor::cwx::perf::TraceScope trace_nested_213(__FILE__, __LINE__);
        if(!open_)return;
        if(event.key==lib::Key::Escape) {close();event.handled=true;return;}
        const bool start_enabled = automaticStartEnabled();
        if (life.expired()) return;
        if(!start_enabled || !text_)return;
        if ((event.key == ::cwxeditor::lib::Key::Backspace ||
             event.key == ::cwxeditor::lib::Key::Delete) && text_->text.empty()) {
            ::cwxeditor::cwx::perf::TraceScope trace_nested_217(__FILE__, __LINE__);
            close();
            event.handled = true;
        }
    };
    auto text_item = ::cwxeditor::lib::LayoutItem{};
    text_item.row = 0U;
    text_item.column = 0U;
    text_item.column_span = 1U;
    text_item.horizontal = ::cwxeditor::lib::Align::Stretch;
    // D版の GridData の widthHint は物理画素ではなく、設計上のUI
    // 単位。 共通の LayoutItem 経路で扱い、文字倍率を一度だけ適用する。
    text_item.minimum.width = static_cast<float>(std::max<std::int32_t>(0,
        settings_.incrementalSearchBoxWidth));
    text_item.preferred.width = text_item.minimum.width;
    text_->set_layout_item(text_item);

    type_ = makeCombo();
    type_->editable = false;
    type_->items = {
        {settings_.containsLabel, {}, 0U, true},
        {settings_.wildcardLabel, {}, 0U, true},
        {settings_.regexLabel, {}, 0U, true},
    };
    type_->selected_index = typeIndex(search_type_);
    type_->on_selection_changed = [this,life=std::weak_ptr<int>(callback_lifetime_)](const std::optional<std::size_t> index) {
        if(life.expired())return;
        ::cwxeditor::cwx::perf::TraceScope trace_nested_181(__FILE__, __LINE__);
        if (!index || *index >= 3U) return;
        setSearchType(static_cast<SearchType>(*index));
    };
    auto type_item = ::cwxeditor::lib::LayoutItem{};
    type_item.row = 0U;
    type_item.column = 1U;
    type_item.horizontal = ::cwxeditor::lib::Align::Start;
    type_->set_layout_item(type_item);

    close_button_ = makeButton();
    close_button_->text = settings_.closeLabel;
    close_button_->cancel_button = true;
    close_button_->on_click = [this,life=std::weak_ptr<int>(callback_lifetime_)] { if(!life.expired())close(); };
    auto close_item = ::cwxeditor::lib::LayoutItem{};
    close_item.row = 0U;
    close_item.column = 2U;
    close_item.horizontal = ::cwxeditor::lib::Align::End;
    close_button_->set_layout_item(close_item);

    window_->add(text_);
    window_->add(type_);
    window_->add(close_button_);

    addition_checkboxes_.clear();
    addition_checkboxes_.reserve(addition_.size());
    for (std::size_t index = 0; index < addition_.size(); ++index) {
        auto check = makeCheckBox();
        check->text = addition_[index].name;
        check->checked = addition_enabled_[index];
        check->on_changed = [this,index,life=std::weak_ptr<int>(callback_lifetime_)](const bool checked) {
        if(life.expired())return;
            ::cwxeditor::cwx::perf::TraceScope trace_nested_203(__FILE__, __LINE__);
            if (index >= addition_enabled_.size()) return;
            addition_enabled_[index] = checked;
            if (open_ && window_ && window_->visible()) notifyModified();
        };
        auto item = ::cwxeditor::lib::LayoutItem{};
        item.row = 1U;
        item.column = index;
        item.horizontal = ::cwxeditor::lib::Align::Start;
        check->set_layout_item(item);
        window_->add(check);
        addition_checkboxes_.push_back(std::move(check));
    }

    window_->on_close_requested = [this,life=std::weak_ptr<int>(callback_lifetime_)] {
        if(life.expired())return;
        ::cwxeditor::cwx::perf::TraceScope trace_nested_268(__FILE__, __LINE__);
        close();
    };
    initialized_ = true;
    reposition();
}

void IncSearch::modified() {
    ::cwxeditor::cwx::perf::TraceScope trace_entry_8(__FILE__, __LINE__);
    if (in_mod_ || !text_ || !open_ || !window_ || !window_->visible()) return;
    in_mod_ = true;
    const std::weak_ptr<int> lifetime = callback_lifetime_;
    // D版の scope(exit) と同様、購読先が例外を出しても再入防止ガードを解除する
    // 。 購読先がC++版の受け手を破棄する場合もあるため、破棄済みのものはリセットしない。
    struct Reset {
        IncSearch* owner;
        std::weak_ptr<int> lifetime;
        ~Reset() { if (!lifetime.expired()) owner->in_mod_ = false; }
    } reset{this, lifetime};

    const auto query = text_->text;
    wildcard_.emplace(query);
    try {
        ::cwxeditor::cwx::perf::TraceScope trace_nested_296(__FILE__, __LINE__);
        regex_ = std::make_shared<UnicodePattern>(query);
        regex_error_ = false;
    } catch (const std::exception&) {
        regex_.reset();
        regex_error_ = true;
    }

    if (open_ && window_ && window_->visible()) reposition();
    if (lifetime.expired()) return;
    notifyModified();
}

void IncSearch::checkClose() {
    ::cwxeditor::cwx::perf::TraceScope trace_entry_9(__FILE__, __LINE__);
    if (!open_ || !enabled_) return;
    const std::weak_ptr<int> lifetime=callback_lifetime_;
    const auto revision=presentation_revision_;
    const auto enabled=enabled_; // 提供処理がこのコントローラーを無効化する場合がある。
    const bool accepted=enabled();
    if (lifetime.expired() || presentation_revision_!=revision) return;
    if (!accepted) {
        ::cwxeditor::cwx::perf::TraceScope trace_nested_323(__FILE__, __LINE__);
        close();
    }
}

void IncSearch::notifyModified() {
    const std::weak_ptr<int> lifetime = callback_lifetime_;
    const auto callbacks = modEvent;
    for (const auto& callback : callbacks) {
        ::cwxeditor::cwx::perf::TraceScope trace_nested_185_204_318_368(__FILE__, __LINE__);
        if (lifetime.expired()) return;
        if (callback) callback();
    }
}

void IncSearch::reposition() {
    if (!window_) return;

    const auto natural = ::cwxeditor::lib::preferred_ui_size(window_);
    const auto width_hint = ::cwxeditor::lib::scaled_ui_metric(
        static_cast<float>(std::max<std::int32_t>(0, settings_.incrementalSearchBoxWidth)));
    const auto width = std::max({width_hint, textWidth(searchText()), natural.width});
    // 100%で4単位の小さな外枠余白をD版と同様に確保し、各内容行の寸法は共通
    // コントロールから求める。 従来の 28 + N*22 では、拡大した TextEdit/CheckBox
    // が欠け、実際のグリッド構造も表せない。
    const auto height = natural.height + ::cwxeditor::lib::scaled_ui_metric(4.0F);
    ::cwxeditor::lib::Rect bounds = window_->bounds();
    bounds.width = width;
    bounds.height = height;
    if (::cwxeditor::lib::editor_window_host_options().all_mode) {
        // ALL のダイアログはサイズだけを指定し、位置は中央にする。
    } else if (parent_) {
        const auto parent_bounds = parent_->bounds();
        bounds.x = parent_bounds.x;
        bounds.y = parent_bounds.y - height;
    } else {
        bounds.x = 0.0F;
        bounds.y = -height;
    }
    window_bounds_ = bounds;
    window_->set_bounds(bounds);
}

void IncSearch::requestStart(const std::string_view text) {
    if (start_) {
        ::cwxeditor::cwx::perf::TraceScope trace_nested_139(__FILE__, __LINE__);
        start_->append(text);
        return;
    }
    ::cwxeditor::cwx::perf::TraceScope trace_nested_141(__FILE__, __LINE__);
    start_ = std::make_shared<Start>(*this, std::string(text));
    const std::weak_ptr<Start> pending = start_;
    const auto dispatcher=services_.dispatcher;
    if (dispatcher) {
        dispatcher->post([pending] {
            if (const auto start = pending.lock()) start->run();
        });
    } else {
        const auto start=start_;
        start->run();
    }
}

bool IncSearch::eligibleSource(const Widget* source) const {
    if (source == nullptr) return true;
    if (!source->visible()) return false;
    if (source == window_.get() || source == text_.get() || source == type_.get() ||
        source == close_button_.get()) {
        return false;
    }
    if (dynamic_cast<const ::cwxeditor::cwx::editor::gui::dwt::timebar::TimeBar*>(source) != nullptr) {
        return false;
    }
    for (const auto& check : addition_checkboxes_) {
        if (source == check.get()) return false;
    }

    switch (source->kind()) {
    case ::cwxeditor::lib::ComponentKind::TextEdit:
    case ::cwxeditor::lib::ComponentKind::NumberEdit:
        return false;
    case ::cwxeditor::lib::ComponentKind::ComboBox: {
        const auto* combo = dynamic_cast<const ComboBox*>(source);
        return combo == nullptr || !combo->editable;
    }
    default:
        return true;
    }
}

std::string IncSearch::folded(const std::string_view value) const {
    // D版の indexOf(CaseSensitive.no) と同様、全プラットフォームでスカ
    // ラー単位の toLower を使う。 互換性のため任意の TextBackend API
    // は残すが、バックエンドやOSの正規化で 検索条件を変えたり、スカラー値を展開したりしない。
    return ::cwxeditor::cwx::enumutils::simpleLowerCase(value);
}

bool IncSearch::validSearchType(const SearchType type) const noexcept {
    return type == SearchType::Contains || type == SearchType::Wildcard ||
           type == SearchType::Regex;
}

SearchType IncSearch::normalizedSearchType(const std::int32_t value) const noexcept {
    if (value <= static_cast<std::int32_t>(SearchType::Contains)) {
        return SearchType::Contains;
    }
    if (value >= static_cast<std::int32_t>(SearchType::Regex)) {
        return SearchType::Regex;
    }
    return static_cast<SearchType>(value);
}

float IncSearch::textWidth(const std::string_view value) const {
    return ::cwxeditor::lib::ui_text_width(value, 0.0F);
}

std::function<bool(std::string_view)> IncSearch::textMatcher() const {
    if (!window_ || !window_->visible() || searchText().empty() || !wildcard_)
        return [](std::string_view) { return true; };
    return [query = folded(searchText()), mode = search_type_, wildcard = wildcard_,
            regex = regex_, invalid = regex_error_](std::string_view value) {
        if (mode == SearchType::Wildcard) return wildcard->match(value);
        if (mode == SearchType::Regex) {
            return !invalid && regex && regex->matches(value);
        }
        const auto lower = ::cwxeditor::cwx::enumutils::simpleLowerCase(value);
        return lower.find(query) != std::string::npos;
    };
}

std::shared_ptr<IncSearch> attachComboSearch(
    ::cwxeditor::lib::Services services,
    const std::shared_ptr<::cwxeditor::lib::ComboBox>& combo,
    const std::shared_ptr<::cwxeditor::lib::TextEdit>& projection,
    ComboSearchFilter filter, IncSearchSettings settings,
    std::function<bool()> current, ::cwxeditor::lib::ResourceId image,
    std::function<void(SearchType)> persist_type, const bool preserve_edit_text,
    std::shared_ptr<::cwxeditor::lib::Menu> context_menu) {
    namespace lib = ::cwxeditor::lib;
    if (!combo || !filter) return {};
    const std::weak_ptr<lib::ComboBox> weak = combo;
    const std::weak_ptr<lib::TextEdit> weak_projection = projection;
    const auto valid = [weak, current] {
        if (current && !current()) return false;
        const auto control = weak.lock();
        return control && control->visible() && control->enabled();
    };
    const auto original_value = combo->selected_index && *combo->selected_index < combo->items.size()
        ? combo->items[*combo->selected_index].value : combo->edit_text;
    const auto search = std::make_shared<IncSearch>(services, combo.get(), valid,
        std::vector<AdditionMatcher>{}, settings);
    const std::weak_ptr<IncSearch> weak_search = search;
    search->searchTypeChanged=[valid,persist_type](SearchType type){if(valid() && persist_type)persist_type(type);};
    search->modEvent.push_back([weak, weak_projection, weak_search, filter, valid, original_value, preserve_edit_text] {
        const auto control = weak.lock();
        const auto active = weak_search.lock();
        if (!control || !active || !valid()) return;
        const auto typed = control->edit_text;
        const auto previous = control->selected_index && *control->selected_index < control->items.size()
            ? control->items[*control->selected_index].value : control->edit_text;
        auto items = filter(*active);
        if (!valid()) return;
        std::optional<std::size_t> selected, initial;
        for (std::size_t i=0;i<items.size();++i) {
            if (items[i].value == previous && !selected) selected=i;
            if (items[i].value == original_value && !initial) initial=i;
        }
        if (!selected && !preserve_edit_text) selected=initial;
        // D版と同様、現在の選択が残っていれば維持し、なければ絞り込み後
        // も残る元の選択を使う。 自動で先頭行を選ばない。変数IDの選
        // 択欄では、絞り込みで見えなくても入力済みの文字列を保持する。
        const auto displayed=preserve_edit_text ? typed : (selected ? items[*selected].text : std::string{});
        if (!lib::replace_combo_items(control,std::move(items),selected,displayed,valid)) return;
        if (!valid()) return;
        if (const auto edit=weak_projection.lock())
            (void)lib::set_text_value(*edit,displayed,lib::TextValueOrigin::Programmatic);
    });
    const auto previous_text=combo->on_text_input;
    combo->on_text_input=[search, weak, valid, previous_text](lib::TextInputEvent& event) {
        if (!valid() || event.handled || event.composing) return;
        const auto control=weak.lock();
        if (search->handleHostTextInput(event,control.get())) {event.handled=true;return;}
        if (valid() && previous_text) previous_text(event);
    };
    const auto previous_key=combo->on_key_down;
    combo->on_key_down=[search,weak,valid,previous_key](lib::KeyEvent& event) {
        if (!valid() || event.handled) return;
        const auto control=weak.lock();
        if (search->handleHostKeyDown(event,control.get())) {event.handled=true;return;}
        if (valid() && previous_key) previous_key(event);
    };
    const auto menu=context_menu ? std::move(context_menu) : std::make_shared<lib::Menu>(true);
    menu->items.push_back({settings.windowTitle,"incremental-search",{},image});
    const auto previous_command = menu->on_command;
    menu->on_command=[weak_search,valid,previous_command](std::string_view command) {
        if (!valid()) return;
        if (command=="incremental-search") {
            if (const auto active=weak_search.lock()) active->startIncSearch();
        } else if (previous_command) previous_command(command);
    };
    const auto previous_pointer=combo->on_pointer;
    combo->on_pointer=[menu,weak,valid,previous_pointer,gui=services.gui](lib::PointerEvent& event) {
        if (previous_pointer) previous_pointer(event);
        if (!valid() || event.handled || !gui || event.button!=lib::PointerButton::Secondary ||
            event.action!=lib::PointerAction::Down) return;
        event.handled=true;
        gui->show_context_menu(menu,weak.lock(),event.position);
    };
    if (projection) {
        const auto previous=projection->on_pointer;
        projection->on_pointer=[previous,weak,valid](lib::PointerEvent& event) {
            if(previous)previous(event);
            if(event.handled || !valid())return;
            if(const auto control=weak.lock();control && control->on_pointer) {
                const auto notify=control->on_pointer;notify(event);
            }
        };
    }
    const auto previous_blur=combo->on_blur;
    combo->on_blur=[search,valid,previous_blur,gui=services.gui] {
        if(previous_blur)previous_blur();
        if(valid() && gui)search->handleFocusChanged(gui->focused_widget().get());
    };
    return search;
}

}  // namespace cwxeditor::cwx::editor::gui::dwt::incsearch
