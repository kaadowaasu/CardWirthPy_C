#include "cwxeditor_src/gui/cwx/editor/gui/dwt/chooser.hpp"
#include "cwxeditor_src/core/cwx/perf.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/commons.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/messageutils.hpp"
#include "cwxeditor_src/lib/ui_actions.hpp"
// UIの実装は一つに統一し、
// 別経路の実装を設けない。

#include <any>
#include <cctype>
#include <cmath>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>

namespace cwxeditor::cwx::editor::gui::dwt::chooser {

namespace {

// DはこれらのCommonsのマップを参照で公開する。C++では各
// 選択部の現在の展開状態をインスタンスごとに保持し、対応する更新
// 処理の間だけ状態を公開してから、他の選択部の保存マップを戻す。
class ExpandedMapSnapshot final {
public:
    ExpandedMapSnapshot(ExpandedMap& shared, std::weak_ptr<const int> lifetime)
        : shared_(&shared), previous_(shared), lifetime_(std::move(lifetime)) {}
    ExpandedMapSnapshot(const ExpandedMapSnapshot&) = delete;
    ExpandedMapSnapshot& operator=(const ExpandedMapSnapshot&) = delete;
    ~ExpandedMapSnapshot() noexcept {
        const auto lifetime = lifetime_.lock();
        if (lifetime) *shared_ = std::move(previous_);
    }

    void publish(const ExpandedMap& current) { *shared_ = current; }

private:
    ExpandedMap* shared_;
    ExpandedMap previous_;
    std::weak_ptr<const int> lifetime_;
};

void attachChoiceActions(const Commons& comm, const WidgetPtr& widget,
                         const std::shared_ptr<incsearch::IncSearch>& search,
                         const std::weak_ptr<int>& lifetime,
                         ::cwxeditor::cwx::types::MenuID view_id,
                         std::function<bool()> can_search,
                         std::function<bool()> can_open,
                         lib::Action open) {
    using MenuID = ::cwxeditor::cwx::types::MenuID;
    const auto menu = std::make_shared<lib::Menu>(true);
    menu->items.push_back({comm.localizedMenuText(MenuID::IncSearch, "絞り込み検索"),
        "search", {}, comm.localizedMenuImage(MenuID::IncSearch)});
    menu->items.push_back({.separator = true});
    menu->items.push_back({comm.localizedMenuText(view_id, "ビューを開く"),
        "open", {}, comm.localizedMenuImage(view_id)});
    const std::weak_ptr<lib::Menu> weak_menu = menu;
    const std::weak_ptr<Widget> weak_widget = widget;
    const std::weak_ptr<incsearch::IncSearch> weak_search = search;
    const std::weak_ptr<lib::GuiBackend> weak_gui = comm.services.gui;
    menu->on_open = [weak_menu, lifetime, can_search, can_open] {
        const auto current = weak_menu.lock();
        if (!current || lifetime.expired()) return;
        current->items[0].enabled = can_search();
        current->items[2].enabled = can_open();
    };
    menu->on_command = [weak_search, weak_widget, weak_gui, lifetime, can_search, can_open, open](StringView command) {
        if (lifetime.expired()) return;
        if (command == "search" && can_search()) {
            const auto target = weak_widget.lock();
            const auto current = weak_search.lock();
            if (!target || !current) return;
            // DのAreaChooser/FlagChooserは
            // 、逐次検索を開く前に一覧・ツリーへ強制フォーカスする
            // 。同じ共通フォーカス順を維持し、検索ポップアップが
            // 復帰先を記録する前に、他の編集中の内容を確定する。
            if (const auto gui = weak_gui.lock()) (void)gui->focus_control(target, true);
            if (lifetime.expired() || weak_widget.lock() != target ||
                weak_search.lock() != current || !can_search()) return;
            current->startIncSearch();
        } else if (command == "open" && can_open()) open();
    };
    const auto previous_pointer = widget->on_pointer;
    widget->on_pointer = [previous_pointer, menu, weak_widget, lifetime, gui = comm.services.gui](lib::PointerEvent& event) {
        if (previous_pointer) previous_pointer(event);
        if (event.handled || lifetime.expired() || !gui ||
            event.action != lib::PointerAction::Down || event.button != lib::PointerButton::Secondary) return;
        const auto target = weak_widget.lock();
        if (!target || !target->enabled()) return;
        menu->on_open();
        event.handled = true;
        gui->show_context_menu(menu, target, event.position);
    };
    const auto previous_text = widget->on_text_input;
    widget->on_text_input = [previous_text, weak_search, weak_widget, lifetime](lib::TextInputEvent& event) {
        if (event.handled || lifetime.expired()) return;
        const auto current = weak_search.lock();
        const auto target = weak_widget.lock();
        if (current && target && target->enabled() && current->handleHostTextInput(event, target.get())) {
            event.handled = true;
            return;
        }
        // 逐次検索が印字可能な確定入力を扱う。拒否した入力だけをネイ
        // ティブのツリー・表の先頭文字検索へ渡す。そうしないと、検
        // 索ポップアップが見る前に旧ハンドラーが文字列を消費する。
        if (previous_text) previous_text(event);
    };
    const auto previous_key = widget->on_key_down;
    widget->on_key_down = [previous_key, weak_search, weak_widget, lifetime](lib::KeyEvent& event) {
        if (previous_key) previous_key(event);
        if (event.handled || lifetime.expired()) return;
        const auto current = weak_search.lock();
        const auto target = weak_widget.lock();
        if (current && target && target->enabled() && current->handleHostKeyDown(event, target.get()))
            event.handled = true;
    };
    const auto previous_focus = widget->on_focus;
    widget->on_focus = [previous_focus, weak_search, weak_widget, lifetime] {
        if (previous_focus) previous_focus();
        if (lifetime.expired()) return;
        const auto current = weak_search.lock();
        const auto target = weak_widget.lock();
        if (current && target) {
            current->checkEnabled();
            current->handleFocusChanged(target.get());
        }
    };
    const auto previous_blur = widget->on_blur;
    widget->on_blur = [previous_blur, weak_search, lifetime,
                       gui = comm.services.gui] {
        if (previous_blur) previous_blur();
        if (lifetime.expired() || !gui) return;
        if (const auto current = weak_search.lock())
            current->handleFocusChanged(gui->focused_widget().get());
    };
    const auto previous_visibility = widget->on_visibility_changed;
    widget->on_visibility_changed = [previous_visibility, weak_search, lifetime](
                                        const bool visible) {
        if (previous_visibility) previous_visibility(visible);
        if (lifetime.expired()) return;
        if (const auto current = weak_search.lock()) {
            if (!visible) current->close();
            else current->checkEnabled();
        }
    };
    if (comm.services.gui) {
        if (const auto owner = comm.services.gui->window_owner(widget)) {
            const auto previous_bounds = owner->on_bounds_changed;
            owner->on_bounds_changed = [previous_bounds, weak_search, lifetime](
                                           const lib::Rect bounds) {
                if (previous_bounds) previous_bounds(bounds);
                if (!lifetime.expired())
                    if (const auto current = weak_search.lock()) current->resize();
            };
        }
    }
}

// 行の付随データは行とともに移動するが、visible_choices_への古いインデックスは
// 追従しない。全バックエンドとDWTの並べ替えで、TableModelの公開処理を共有する。
template <typename Choice>
const Choice* rowChoice(const customtable::TableModelPtr& table, std::size_t index) {
    if (!table || index >= table->rows.size()) return nullptr;
    return std::any_cast<Choice>(&std::as_const(*table).rowState(index).data);
}

[[nodiscard]] bool logicalLess(const StringView left, const StringView right,
                                const bool logical) {
    if (logical) return ::cwxeditor::cwx::utils::ncmp(left, right) < 0;
    return left < right;
}

void sortStrings(std::vector<String>& values, const bool logical) {
    std::stable_sort(values.begin(), values.end(),
                     [logical](const String& left, const String& right) {
                         // DのCoupon/Gossip/Stamp/KeyCode/Cell/Group
                         // の選択候補はicmp/incmpを使う。大文字小
                         // 文字を区別する資源・変数の選択順とは独立させる。
                         const auto a = ::cwxeditor::cwx::utils::caseFold(left);
                         const auto b = ::cwxeditor::cwx::utils::caseFold(right);
                         return logicalLess(a, b, logical);
                     });
}

void appendUnique(std::vector<String>& values, String value) {
    if (std::find(values.begin(), values.end(), value) == values.end())
        values.push_back(std::move(value));
}

template <typename ID>
std::vector<String> counterKeys(const UseCounterPtr& counter) {
    std::vector<String> values;
    if (!counter) return values;
    for (const auto& key : counter->template keys<ID>())
        values.push_back(static_cast<String>(key));
    return values;
}

[[nodiscard]] std::vector<String> sourceKeys(const Commons& comm,
                                             const UseCounterPtr& counter,
                                             const std::function<std::vector<String>(
                                                 const UseCounterPtr&)>& typed) {
    if (counter) return typed(counter);
    // C++ホストの意図的な拡張。Dのシナリオ専用選択部と
    // 異なり、単体EditorにSummaryがなくても、
    // 入力元アダプターが有用なキーを提供する場合がある。
    // その環境では空一覧を返さず候補を使えるようにする。
    if (comm.source.keys) return comm.source.keys();
    return {};
}

[[nodiscard]] String lowerAscii(String value) {
    for (auto& character : value)
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    return value;
}

[[nodiscard]] String normalizedSystemValue(String value) {
    value = lowerAscii(std::move(value));
    if (value.starts_with("??")) value.erase(0U, 2U);
    return value;
}

[[nodiscard]] std::vector<String> splitPath(StringView value) {
    std::vector<String> result;
    std::size_t begin = 0U;
    while (begin <= value.size()) {
        const auto end = value.find('\\', begin);
        result.emplace_back(value.substr(begin, end == StringView::npos ? StringView::npos
                                                                         : end - begin));
        if (end == StringView::npos) break;
        begin = end + 1U;
    }
    return result;
}

[[nodiscard]] String joinPath(const std::vector<String>& parts, const std::size_t count) {
    String result;
    for (std::size_t index = 0U; index < count && index < parts.size(); ++index) {
        if (!result.empty()) result.push_back('\\');
        result += parts[index];
    }
    return result;
}

[[nodiscard]] String couponConversion(const Commons& comm,
                                      const ChooserProperties& properties,
                                      const StringView value, const CouponType type) {
    if (properties.convertCoupon) return properties.convertCoupon(value, type, false);
    if (comm.convertCoupon) return comm.convertCoupon(value, type, false);
    if (comm.source.convertCoupon) return comm.source.convertCoupon(value, type, false);
    if (properties.editor_props) return properties.editor_props->parent().sys().convCoupon(value, type, false);
    return String(value);
}

[[nodiscard]] String keyCodeConversion(const Commons& comm,
                                       const ChooserProperties& properties,
                                       const StringView value, const FKCKind kind) {
    if (properties.convertKeyCode) return properties.convertKeyCode(value, kind);
    if (comm.convertKeyCode) return comm.convertKeyCode(value, kind);
    if (comm.source.convertKeyCode) return comm.source.convertKeyCode(value, kind);
    // Dはcomm.prop.sysを直接呼ぶ。アダプターがなければ同じSystemを使う。
    if (properties.editor_props)
        return properties.editor_props->parent().sys().convFireKeyCode(value, kind);
    return String(value);
}

void attachComboMenuState(
    const std::shared_ptr<lib::ComboBox>& combo, const std::shared_ptr<lib::Menu>& menu,
    std::function<bool(std::vector<lib::MenuItem>&, StringView)> updateItems) {
    if (!combo || !menu || !updateItems) return;
    // DのMenuItemの有効判定デリゲートは、メニューのShow境界で実行する。
    const std::weak_ptr<lib::ComboBox> weak_combo = combo;
    const std::weak_ptr<lib::Menu> weak_menu = menu;
    const auto refresh = [weak_combo, weak_menu, updateItems] {
        const auto target = weak_combo.lock();
        if (!target) return;
        const auto update = [&](const std::shared_ptr<lib::Menu>& current) {
            if (!current || !updateItems(current->items, target->edit_text)) return false;
            ++current->item_update_revision;
            return true;
        };
        const auto chooser = std::dynamic_pointer_cast<ChooserCombo>(target);
        const auto current_menu = chooser ? chooser->menu : nullptr;
        if (update(current_menu)) return;
        if (const auto original = weak_menu.lock(); original != current_menu)
            (void)update(original);
    };
    const auto previous_open = menu->on_open;
    menu->on_open = [previous_open, refresh] {
        if (previous_open) previous_open();
        refresh();
    };
    const auto previous_text = combo->on_text_changed;
    combo->on_text_changed = [previous_text, refresh](const StringView value) {
        if (previous_text) previous_text(value);
        refresh();
    };
    refresh();
}

void writeClipboard(const Commons& comm, const StringView value) {
    if (const auto clipboard = comm.clipboardBackend()) {
        lib::ClipboardData data;
        data.text = String(value);
        clipboard->write(data);
    }
}

template <typename T>
[[nodiscard]] String variablePath(const std::shared_ptr<T>& value) {
    return value ? value->path() : String{};
}

template <typename T>
[[nodiscard]] String variableName(const std::shared_ptr<T>& value) {
    return value ? value->name() : String{};
}

template <typename T>
[[nodiscard]] String variableCwxPath(const std::shared_ptr<T>& value) {
    return value ? value->cwxPath(true) : String{};
}

[[nodiscard]] ResourceId variableImage(const ChooserProperties& properties,
                                       const VariableKind kind, const bool local) {
    switch (kind) {
    case VariableKind::Flag: return local ? properties.images.localFlag : properties.images.flag;
    case VariableKind::Step: return local ? properties.images.localStep : properties.images.step;
    case VariableKind::Variant:
        return local ? properties.images.localVariant : properties.images.variant;
    }
    return {};
}

[[nodiscard]] String areaDisplayName(const std::uint64_t id, StringView base) {
    return std::to_string(id) + "." + String(base);
}

template <typename T>
[[nodiscard]] String objectPath(const std::shared_ptr<T>& value) {
    return value ? value->cwxPath(true) : String{};
}

}  // namespace

namespace detail {

ChooserProperties chooserProperties(const ChooserProperties& properties) {
    return properties;
}

incsearch::IncSearchSettings searchSettings(const ChooserProperties& properties) {
    incsearch::IncSearchSettings result;
    const auto* prop = properties.editor_commons ? properties.editor_commons->prop() : nullptr;
    if (!prop) return result;
    const auto* common = properties.editor_commons;
    const auto common_lifetime = common->lifetimeToken();
    result.startIncrementalSearchWhenKeyDown = prop->var().startIncrementalSearchWhenKeyDown.value;
    result.startIncrementalSearchWhenKeyDownProvider = [common, common_lifetime] {
        if (!common || common_lifetime.expired()) return false;
        const auto* live_prop = common->prop();
        return live_prop && live_prop->var().startIncrementalSearchWhenKeyDown.value;
    };
    result.incrementalSearchBoxWidth = prop->var().incrementalSearchBoxWidth.value;
    result.incrementalSearchType = prop->var().incrementalSearchType.value;
    result.containsLabel = prop->requiredMsgs().incSearchContains.value;
    result.wildcardLabel = prop->requiredMsgs().incSearchWildcard.value;
    result.regexLabel = prop->requiredMsgs().incSearchRegex.value;
    result.windowTitle = prop->requiredMsgs().menuText(::cwxeditor::cwx::types::MenuID::IncSearch);
    result.closeLabel = prop->requiredMsgs().menuText(::cwxeditor::cwx::types::MenuID::CloseIncSearch);
    return result;
}

void storeSearchType(const ChooserProperties& properties, incsearch::SearchType type) {
    auto* prop = properties.editor_commons ? properties.editor_commons->prop() : nullptr;
    if (prop) prop->var().incrementalSearchType.value = static_cast<std::int32_t>(type);
}

// Dのデリゲートは通知元を生存させる。ネイティブの選択ウィジェット
// はMainWindowより長く残り得るため、保持する全更新接続で
// 同じ弱い通知元の検査を使い、破棄時にCommonsから切り離す。
struct ObserverPublisherLifetime {
    commons::Commons* common{};
    std::weak_ptr<const int> lifetime;
    explicit ObserverPublisherLifetime(commons::Commons* publisher)
        : common(publisher), lifetime(publisher ? publisher->lifetimeToken()
                                               : std::weak_ptr<const int>{}) {}
    [[nodiscard]] bool publisherAlive() const noexcept {
        return common && !lifetime.expired();
    }
};

[[nodiscard]] std::shared_ptr<void> subscribeCouponRefresh(
    commons::Commons* common, lib::Action refresh) {
    struct Subscription final : ObserverPublisherLifetime {
        using ObserverPublisherLifetime::ObserverPublisherLifetime;
        std::uint64_t coupons{}, uses{}, skin{}, dataVersion{}, text{};
        ~Subscription() {
            if (!publisherAlive()) return;
            common->refCoupons.remove(coupons);
            common->refUseCount.remove(uses);
            common->refSkin.remove(skin);
            common->refDataVersion.remove(dataVersion);
            common->replText.remove(text);
        }
    };
    if (!common || !refresh) return {};
    auto subscription = std::make_shared<Subscription>(common);
    subscription->coupons = common->refCoupons.add(refresh);
    subscription->uses = common->refUseCount.add(refresh);
    subscription->skin = common->refSkin.add(refresh);
    subscription->dataVersion = common->refDataVersion.add(refresh);
    subscription->text = common->replText.add(std::move(refresh));
    return subscription;
}

[[nodiscard]] std::shared_ptr<void> subscribeCardGroupRefresh(
    commons::Commons* common, lib::Action refresh) {
    struct Subscription final : ObserverPublisherLifetime {
        using ObserverPublisherLifetime::ObserverPublisherLifetime;
        std::uint64_t cards{}, text{};
        ~Subscription() {
            if (!publisherAlive()) return;
            common->refCardGroups.remove(cards);
            common->replText.remove(text);
        }
    };
    if (!common || !refresh) return {};
    auto subscription = std::make_shared<Subscription>(common);
    subscription->cards = subscription->common->refCardGroups.add(refresh);
    subscription->text = subscription->common->replText.add(std::move(refresh));
    return subscription;
}

[[nodiscard]] std::shared_ptr<void> subscribeSelectionRefresh(
    commons::Commons* common, lib::Action refresh) {
    struct Subscription final : ObserverPublisherLifetime {
        using ObserverPublisherLifetime::ObserverPublisherLifetime;
        std::uint64_t skin{}, standards{};
        ~Subscription() {
            if (!publisherAlive()) return;
            common->refSkin.remove(skin);
            common->refStandardSelections.remove(standards);
        }
    };
    if (!common || !refresh) return {};
    auto subscription = std::make_shared<Subscription>(common);
    subscription->skin = subscription->common->refSkin.add(refresh);
    subscription->standards = subscription->common->refStandardSelections.add(
        std::move(refresh));
    return subscription;
}

[[nodiscard]] std::shared_ptr<void> subscribeVariableRefresh(
    commons::Commons* common, const VariableKind kind, lib::Action refresh) {
    struct Subscription final : ObserverPublisherLifetime {
        using ObserverPublisherLifetime::ObserverPublisherLifetime;
        std::uint64_t variables{};
        ~Subscription() {
            if (publisherAlive()) common->refFlagAndStep.remove(variables);
        }
    };
    if (!common || !refresh) return {};
    auto subscription = std::make_shared<Subscription>(common);
    subscription->variables = common->refFlagAndStep.add(
        [kind, refresh = std::move(refresh)](const auto& flags, const auto& steps,
                                             const auto& variants) {
            const bool relevant = kind == VariableKind::Flag ? !flags.empty()
                : kind == VariableKind::Step ? !steps.empty() : !variants.empty();
            if (relevant) refresh();
        });
    return subscription;
}

void attachCouponRefresh(const ComboPtr& combo,
                         const ChooserProperties& properties) {
    if (!combo || !properties.editor_commons) return;
    const std::weak_ptr<ChooserCombo> weak = combo;
    combo->notificationBinding = subscribeCouponRefresh(
        properties.editor_commons, [weak] {
        if (const auto target = weak.lock(); target && target->refresh) target->refresh();
    });
}

void attachCouponSpecialChars(const ComboPtr& combo, const Commons& comm,
                              const SummaryPtr& summ, const UseCounterPtr& uc,
                              const ChooserProperties& properties) {
    if (!combo) return;
    auto* common = properties.editor_commons;
    auto* prop = properties.editor_props ? properties.editor_props
                                        : (common ? common->prop() : nullptr);
    const auto skin = properties.previewSkin ? properties.previewSkin
        : std::function<::cwxeditor::cwx::skin::Skin::Ptr()>([common] {
              return common ? common->skin() : ::cwxeditor::cwx::skin::Skin::Ptr{};
          });
    messageutils::MessageContext context;
    context.services = comm.services;
    context.props = prop;
    context.summary = summ.get();
    context.summary_owner = summ;
    context.editor_commons = common;
    context.use_counter = uc.get();
    context.use_counter_owner = uc;
    const std::weak_ptr<ChooserCombo> weak = combo;
    const auto update = [weak, summ, uc, prop, skin] {
        const auto target = weak.lock();
        if (!target) return;
        String preview = prop ? messageutils::createSPCharPreview(
            *prop, summ.get(), skin(), uc.get(), target->edit_text, true)
            : target->edit_text;
        // Dはツールチップのアクセスキー描画用にアンパサンドをエスケープする。
        String escaped;
        for (const auto character : preview) {
            escaped += character;
            if (character == '&') escaped += '&';
        }
        if (target->tooltip() != escaped) target->set_tooltip(std::move(escaped));
    };
    (void)messageutils::setupSPCharsMenu(context, summ.get(), skin, uc.get(),
        combo->menu, false, true, [] { return true; }, [weak, update](StringView value) {
            const auto target = weak.lock();
            if (!target || !target->textInputBinding) return;
            // TMMオーバーロードが接続を置き換え得るため、現在の接続を解決する。
            auto text = target->textInputBinding->text();
            auto next = text.getText();
            const auto selection = text.getSelection();
            const auto start = std::min(next.size(), static_cast<std::size_t>(std::max(0.0F, selection.x)));
            const auto end = std::clamp(static_cast<std::size_t>(std::max(0.0F, selection.y)), start, next.size());
            next.replace(start, end - start, value);
            text.setText(next);
            const auto caret = static_cast<float>(start + value.size());
            text.setSelection({caret, caret});
            update();
        });
    const auto previous = combo->on_text_changed;
    combo->on_text_changed = [previous, update](StringView value) {
        if (previous) previous(value);
        update();
    };
    struct Subscription final : ObserverPublisherLifetime {
        using ObserverPublisherLifetime::ObserverPublisherLifetime;
        std::uint64_t preview{}, flags{}, deleted{}, path{}, paths{}, text{};
        ~Subscription() {
            if (!publisherAlive()) return;
            common->refPreviewValues.remove(preview);
            common->refFlagAndStep.remove(flags);
            common->delFlagAndStep.remove(deleted);
            common->refPath.remove(path);
            common->refPaths.remove(paths);
            common->replText.remove(text);
        }
    };
    if (common) {
        auto subscription = std::make_shared<Subscription>(common);
        subscription->preview = common->refPreviewValues.add(update);
        const auto variables = [update](auto, auto, auto) { update(); };
        subscription->flags = common->refFlagAndStep.add(variables);
        subscription->deleted = common->delFlagAndStep.add(variables);
        subscription->path = common->refPath.add([update](auto, auto, bool) { update(); });
        subscription->paths = common->refPaths.add([update](auto) { update(); });
        subscription->text = common->replText.add(update);
        combo->couponPreviewBinding = std::move(subscription);
    }
    update();
}

void attachNameRefresh(const ComboPtr& combo,
                       const ChooserProperties& properties,
                       const NameRefreshKind kind) {
    struct Subscription final : ObserverPublisherLifetime {
        using ObserverPublisherLifetime::ObserverPublisherLifetime;
        NameRefreshKind kind{NameRefreshKind::Gossip};
        std::uint64_t names{}, text{}, skin{}, keys{};
        ~Subscription() {
            if (!publisherAlive()) return;
            if (kind == NameRefreshKind::Gossip) common->refGossips.remove(names);
            else if (kind == NameRefreshKind::CompleteStamp) common->refCompleteStamps.remove(names);
            else if (kind == NameRefreshKind::KeyCode) {
                common->refStandardKeyCodes.remove(names);
                common->refSkin.remove(skin);
                common->refKeyCodes.remove(keys);
            } else common->refCellNames.remove(names);
            common->replText.remove(text);
        }
    };
    if (!combo || !properties.editor_commons) return;
    auto subscription = std::make_shared<Subscription>(properties.editor_commons);
    subscription->kind = kind;
    const std::weak_ptr<ChooserCombo> weak = combo;
    const auto refresh = [weak] {
        if (const auto target = weak.lock(); target && target->refresh)
            target->refresh();
    };
    if (kind == NameRefreshKind::Gossip)
        subscription->names = subscription->common->refGossips.add(refresh);
    else if (kind == NameRefreshKind::CompleteStamp)
        subscription->names = subscription->common->refCompleteStamps.add(refresh);
    else if (kind == NameRefreshKind::KeyCode) {
        subscription->names = subscription->common->refStandardKeyCodes.add(refresh);
        subscription->skin = subscription->common->refSkin.add(refresh);
        subscription->keys = subscription->common->refKeyCodes.add(refresh);
    } else subscription->names = subscription->common->refCellNames.add(refresh);
    subscription->text = subscription->common->replText.add(refresh);
    combo->notificationBinding = std::move(subscription);
}

ComboPtr asChooserCombo(const std::shared_ptr<lib::ComboBox>& combo) {
    return std::dynamic_pointer_cast<ChooserCombo>(combo);
}

void populateCombo(const std::shared_ptr<lib::ComboBox>& combo,
                   const std::shared_ptr<ComboState>& state,
                   const StringView initValue) {
    if (!combo || !state) return;
    const auto current = combo->edit_text;
    const auto order_revision = combo->item_order_revision;
    auto values = state->values ? state->values() : std::vector<String>{};
    if (state->separateInitial && !state->initial.empty()) {
        values.erase(std::remove(values.begin(), values.end(), state->initial), values.end());
        values.erase(std::remove(values.begin(), values.end(), String{}), values.end());
        if (state->firstPopulation) {
            values.insert(values.begin(), String{});
            values.insert(values.begin(), state->initial);
        } else if (!current.empty()) {
            values.insert(values.begin(), state->initial);
        }
    }
    const bool has_item = !values.empty();
    std::vector<lib::ChoiceItem> items;
    items.reserve(values.size());
    std::optional<std::size_t> selected;
    for (const auto& value : values) {
        if (state->search && !state->search->match(value)) continue;
        if (value == current) selected = items.size();
        items.push_back({value, value, {}, true});
    }
    // 初期化は最初の更新前にファクトリーで行う。その後の空
    // 入力は意図的な編集であり、初期値の復元要求ではない。
    (void)initValue;
    const auto edit_text = current;
    const std::weak_ptr<lib::ComboBox> weak = combo;
    if (!lib::replace_combo_items(combo, std::move(items), selected, edit_text,
            [weak, order_revision] {
                const auto target = weak.lock();
                return target && target->item_order_revision == order_revision;
            })) return;
    state->hasItem = has_item;
    state->firstPopulation = false;
    if (const auto chooser = asChooserCombo(combo)) {
        chooser->hasItem = has_item;
        if (chooser->menu) {
            const auto item = std::find_if(chooser->menu->items.begin(), chooser->menu->items.end(),
                [](const auto& value) { return value.command == "incremental_search"; });
            if (item != chooser->menu->items.end() && item->enabled != has_item) {
                item->enabled = has_item;
                ++chooser->menu->item_update_revision;
            }
        }
    }
}

void attachSearchMenu(const std::shared_ptr<lib::ComboBox>& combo,
                      const std::shared_ptr<ComboState>& state,
                      const std::weak_ptr<lib::GuiBackend> gui,
                      const bool allowSearch, const StringView label) {
    const auto chooser = asChooserCombo(combo);
    if (!chooser || !chooser->menu) return;
    lib::MenuItem item{String(label), "incremental_search"};
    item.enabled = state && state->hasItem;
    chooser->menu->items.push_back(std::move(item));
    if (allowSearch) {
        const std::weak_ptr<lib::ComboBox> weak_combo = combo;
        const std::weak_ptr<ComboState> weak_state = state;
        chooser->menu->on_command = [weak_combo, weak_state, gui](
            const StringView command) {
            if (command != "incremental_search") return;
            const auto target = weak_combo.lock();
            const auto current = weak_state.lock();
            const auto current_chooser = asChooserCombo(target);
            if (!target || !current || !current_chooser || !current_chooser->menu ||
                !current->hasItem || !current->search)
                return;
            // DのCombo.forceFocusはIncSearch.startIncSearchよ
            // り先に行う。ポップアップへフォーカスを移す前に、共通フォーカス状態へコンボを公開する。
            if (const auto backend = gui.lock())
                (void)backend->focus_control(target, true);
            if (weak_combo.lock() != target || weak_state.lock() != current ||
                !current->hasItem || !current->search) return;
            const auto live_chooser = asChooserCombo(target);
            if (!live_chooser || !live_chooser->menu) return;
            current->search->startIncSearch();
        };
    }
}

void attachTextMenu(const Commons& comm, const std::shared_ptr<lib::ComboBox>& combo,
                    const std::function<bool()>& catchMod, TextMenuModify* out) {
    if (!combo) return;
    dmenu::Commons text_comm;
    text_comm.services = comm.services;
    text_comm.services.clipboard = comm.clipboardBackend();
    text_comm.refreshToolBar = comm.refreshToolBar;
    text_comm.presentation.menuText = comm.menuText;
    text_comm.presentation.menuImage = comm.menuImage;
    auto binding = dmenu::createTextMenu(text_comm, customtext::TMM(combo), catchMod);
    auto controller = binding->customText();
    if (out) {
        out->text = binding->text();
        out->controller = controller;
        out->inputBinding = binding;
    }
    if (const auto chooser = asChooserCombo(combo)) {
        const auto old_menu = chooser->menu;
        const auto editing = binding->menu()->on_command;
        if (old_menu) {
            binding->menu()->items.insert(binding->menu()->items.end(), old_menu->items.begin(), old_menu->items.end());
            const auto search = old_menu->on_command;
            binding->menu()->on_command = [editing, search](StringView command) {
                if (command == "incremental_search") { if (search) search(command); }
                else if (editing) editing(command);
            };
            // 項目の結合後も、旧メニューのDのShow時の有効判定を維持する。
            const auto editing_open = binding->menu()->on_open;
            const auto chooser_open = old_menu->on_open;
            if (editing_open || chooser_open)
                binding->menu()->on_open = [editing_open, chooser_open] {
                    if (editing_open) editing_open();
                    if (chooser_open) chooser_open();
                };
        }
        chooser->menu = binding->menu();
        chooser->textMenu = controller;
        chooser->textInputBinding = std::move(binding);
    }
}

}  // namespace detail

std::vector<String> selectionStandards(const ChooserProperties& properties) {
    String skin_type;
    auto skin = properties.previewSkin ? properties.previewSkin()
        : properties.editor_commons ? properties.editor_commons->skin()
                                   : ::cwxeditor::cwx::skin::Skin::Ptr{};
    if (skin) skin_type = skin->type();
    if (properties.standardSelectionsForSkin)
        return properties.standardSelectionsForSkin(skin_type);
    if (properties.editor_props) {
        const auto values = properties.editor_props->standardSelections(skin_type);
        return {values.begin(), values.end()};
    }
    return properties.standardSelections;
}

std::shared_ptr<void> watchCardGroupChanges(
    ::cwxeditor::cwx::editor::gui::dwt::commons::Commons* publisher,
    lib::Action refresh) {
    return detail::subscribeCardGroupRefresh(publisher, std::move(refresh));
}

std::shared_ptr<void> watchSelectionChanges(
    ::cwxeditor::cwx::editor::gui::dwt::commons::Commons* publisher,
    lib::Action refresh) {
    return detail::subscribeSelectionRefresh(publisher, std::move(refresh));
}

std::shared_ptr<void> watchVariableChanges(
    ::cwxeditor::cwx::editor::gui::dwt::commons::Commons* publisher,
    const VariableKind kind, lib::Action refresh) {
    return detail::subscribeVariableRefresh(publisher, kind, std::move(refresh));
}

std::vector<String> addInitValue(const Commons& comm, std::vector<String> values,
                                 const StringView initValue,
                                 const ChooserProperties& properties) {
    ::cwxeditor::cwx::perf::TraceScope trace_addInitValue(__FILE__, __LINE__);
    if (initValue.empty()) return values;
    const auto present = std::find(values.begin(), values.end(), initValue) != values.end();
    if (!properties.showCurrentValueOnTopAlways && present) return values;
    values.erase(std::remove(values.begin(), values.end(), initValue), values.end());
    if (!values.empty() && !values.front().empty()) values.insert(values.begin(), String{});
    values.insert(values.begin(), String(initValue));
    (void)comm;
    return values;
}

std::vector<String> allCoupons(const Commons& comm, const SummaryPtr& summ,
                               const CouponComboType type,
                               const ChooserProperties& properties) {
    ::cwxeditor::cwx::perf::TraceScope trace_allCoupons(__FILE__, __LINE__);
    std::vector<String> standard = properties.couponStandards
        ? properties.couponStandards(type)
        : type == CouponComboType::Cast
                                       ? std::vector<String>{}
                                       : (type == CouponComboType::Valued
                                              ? properties.standardCouponsForValued
                                              : properties.standardCoupons);
    std::vector<String> used;
    const auto counter = summ ? summ->useCounter() : UseCounterPtr{};
    if (properties.usedCouponToCombo) {
        if (counter) used = counterKeys<CouponId>(counter);
        else if (comm.source.keys) used = comm.source.keys();
    }
    sortStrings(used, properties.logicalSort);
    // Dで指定された標準・スキンの選択肢。区切りと重複項目も含む。重
    // 複除去は、後から付ける使用済みクーポンの部分だけに適用する。
    std::vector<String> names = std::move(standard);
    std::vector<String> extra;
    for (const auto& value : used) {
        if (std::find(names.begin(), names.end(), value) != names.end()) continue;
        if (properties.couponFilter && !properties.couponFilter(value, type)) continue;
        extra.push_back(value);
    }
    if (properties.useNamesAfterStandard) {
        ::cwxeditor::cwx::perf::TraceScope trace_coupon_standard_first(__FILE__, __LINE__);
        if (!names.empty() && !extra.empty()) { ::cwxeditor::cwx::perf::TraceScope trace_coupon_standard_separator(__FILE__, __LINE__); names.push_back({}); }
        names.insert(names.end(), extra.begin(), extra.end());
    } else {
        ::cwxeditor::cwx::perf::TraceScope trace_coupon_used_first(__FILE__, __LINE__);
        if (!names.empty() && !extra.empty()) { ::cwxeditor::cwx::perf::TraceScope trace_coupon_used_separator(__FILE__, __LINE__); extra.push_back({}); }
        extra.insert(extra.end(), names.begin(), names.end());
        names = std::move(extra);
    }
    return names;
}

void setCouponFilter(ChooserProperties& properties,
                     std::function<bool(StringView)> isSystemCoupon,
                     std::vector<String> excluded) {
    properties.couponFilter = [isSystemCoupon = std::move(isSystemCoupon),
                               excluded = std::move(excluded)](
                                  const StringView value, CouponComboType) {
        if (isSystemCoupon && isSystemCoupon(value)) return false;
        return std::find(excluded.begin(), excluded.end(), value) == excluded.end();
    };
}

std::shared_ptr<void> watchCouponChanges(
    ::cwxeditor::cwx::editor::gui::dwt::commons::Commons* publisher,
    lib::Action refresh) {
    return detail::subscribeCouponRefresh(publisher, std::move(refresh));
}

std::vector<String> allGossips(const Commons& comm, const SummaryPtr& summ,
                               const ChooserProperties& properties) {
    const auto counter = summ ? summ->useCounter() : UseCounterPtr{};
    auto result = sourceKeys(comm, counter, [](const UseCounterPtr& value) {
        return counterKeys<GossipId>(value);
    });
    sortStrings(result, properties.logicalSort);
    return result;
}

std::vector<String> allCompleteStamps(const Commons& comm, const SummaryPtr& summ,
                                      const ChooserProperties& properties) {
    const auto counter = summ ? summ->useCounter() : UseCounterPtr{};
    auto result = sourceKeys(comm, counter, [](const UseCounterPtr& value) {
        return counterKeys<CompleteStampId>(value);
    });
    sortStrings(result, properties.logicalSort);
    return result;
}

static std::vector<String> collectKeyCodes(const Commons& comm, const SummaryPtr& summ,
                                           const ChooserProperties& properties,
                                           const bool factoryByteOrder) {
    std::vector<String> standard = properties.standardKeyCodes;
    std::vector<String> used;
    const auto counter = summ ? summ->useCounter() : UseCounterPtr{};
    if (counter) used = counterKeys<KeyCodeId>(counter);
    else if (comm.source.keys) used = comm.source.keys();
    if (factoryByteOrder) std::sort(used.begin(), used.end());
    else sortStrings(used, properties.logicalSort);
    used.erase(std::remove_if(used.begin(), used.end(), [&standard](const String& value) {
                   return std::find(standard.begin(), standard.end(), value) != standard.end();
               }),
               used.end());
    if (properties.useNamesAfterStandard) {
        ::cwxeditor::cwx::perf::TraceScope trace_key_standard_first(__FILE__, __LINE__);
        if (!standard.empty() && !used.empty()) { ::cwxeditor::cwx::perf::TraceScope trace_key_standard_separator(__FILE__, __LINE__); standard.push_back({}); }
        standard.insert(standard.end(), used.begin(), used.end());
        return standard;
    }
    { ::cwxeditor::cwx::perf::TraceScope trace_key_used_first(__FILE__, __LINE__);
    if (!used.empty()) { ::cwxeditor::cwx::perf::TraceScope trace_key_used_separator(__FILE__, __LINE__); used.push_back({}); }
    used.insert(used.end(), standard.begin(), standard.end());
    return used;
    }
}

std::vector<String> allKeyCodes(const Commons& comm, const SummaryPtr& summ,
                                const ChooserProperties& properties) {
    return collectKeyCodes(comm, summ, properties, false);
}

std::vector<String> detail::keyCodeFactoryValues(
    const Commons& comm, const SummaryPtr& summ, const ChooserProperties& properties) {
    return collectKeyCodes(comm, summ, properties, true);
}

std::vector<String> allCellNames(const Commons& comm, const SummaryPtr& summ,
                                 const ChooserProperties& properties) {
    const auto counter = summ ? summ->useCounter() : UseCounterPtr{};
    auto result = sourceKeys(comm, counter, [](const UseCounterPtr& value) {
        return counterKeys<CellNameId>(value);
    });
    sortStrings(result, properties.logicalSort);
    return result;
}

std::vector<String> allCardGroups(const Commons& comm, const SummaryPtr& summ,
                                  const ChooserProperties& properties) {
    ::cwxeditor::cwx::perf::TraceScope trace_allCardGroups(__FILE__, __LINE__);
    const auto counter = summ ? summ->useCounter() : UseCounterPtr{};
    auto result = sourceKeys(comm, counter, [](const UseCounterPtr& value) {
        return counterKeys<CardGroupId>(value);
    });
    sortStrings(result, properties.logicalSort);
    return result;
}

void createCouponTypeMenu(const Commons& comm, const ComboPtr& combo,
                          const bool convert, const bool copy,
                          const ChooserProperties& properties) {
    if (!combo) return;
    createCouponTypeMenu(comm, std::static_pointer_cast<lib::ComboBox>(combo),
                         combo->menu, convert, copy, properties);
}

void createCouponTypeMenu(const Commons& comm, const std::shared_ptr<lib::ComboBox>& combo,
                          const std::shared_ptr<lib::Menu>& menu,
                          const bool convert, const bool copy,
                          const ChooserProperties& properties) {
    ::cwxeditor::cwx::perf::TraceScope trace_createCouponTypeMenu(__FILE__, __LINE__);
    if (!combo || !menu) return;
    const auto types = std::vector<std::pair<String, CouponType>>{
        {"normal", CouponType::Normal}, {"hide", CouponType::Hide},
        {"dur", CouponType::Dur}, {"dur_battle", CouponType::DurBattle},
        {"system", CouponType::System}};
    if (convert) {
        lib::MenuItem parent{properties.messages.convertCouponType, "coupon.convert"};
        for (const auto& [name, type] : types)
            parent.children.push_back({name, "coupon.convert." + name});
        menu->items.push_back(std::move(parent));
    }
    if (copy) {
        lib::MenuItem parent{properties.messages.copyConvertedCoupon, "coupon.copy"};
        for (const auto& [name, type] : types)
            parent.children.push_back({name, "coupon.copy." + name});
        menu->items.push_back(std::move(parent));
    }
    const auto oldHandler = menu->on_command;
    menu->on_command = [weak = std::weak_ptr<lib::ComboBox>(combo), comm, properties, types, convert, copy,
                               oldHandler](const StringView command) {
        if (oldHandler) oldHandler(command);
        const auto combo = weak.lock();
        if (!combo) return;
        const auto prefix = command.substr(0U, command.find_last_of('.'));
        const auto name = command.substr(command.find_last_of('.') + 1U);
        const auto it = std::find_if(types.begin(), types.end(), [&name](const auto& value) {
            return value.first == name;
        });
        if (it == types.end()) return;
        if (prefix == "coupon.convert" && convert) {
            ::cwxeditor::cwx::perf::TraceScope trace_coupon_convert(__FILE__, __LINE__);
            if (combo->edit_text.empty()) return;
            const auto value = couponConversion(comm, properties, combo->edit_text, it->second);
            if (value == combo->edit_text) return;
            customtext::TMM(combo).setText(value);
            combo->selection_start = value.size();
            combo->selection_length = 0U;
            if (const auto chooser = std::dynamic_pointer_cast<ChooserCombo>(combo))
                chooser->edit_selection = std::pair<std::size_t, std::size_t>{value.size(), value.size()};
            if (comm.refreshToolBar) comm.refreshToolBar();
        } else if (prefix == "coupon.copy" && copy && !combo->edit_text.empty()) {
            ::cwxeditor::cwx::perf::TraceScope trace_coupon_copy(__FILE__, __LINE__);
            const auto value = couponConversion(comm, properties, combo->edit_text, it->second);
            writeClipboard(comm, value);
            if (comm.refreshToolBar) comm.refreshToolBar();
        }
    };
    attachComboMenuState(combo, menu,
        [comm, properties, types, convert, copy](std::vector<lib::MenuItem>& items,
                                                  const StringView text) {
            bool found = false;
            for (auto& item : items) {
                if (item.command == "coupon.convert") {
                    item.enabled = convert && !text.empty();
                    found = true;
                } else if (item.command == "coupon.copy") {
                    item.enabled = copy && !text.empty();
                    found = true;
                }
                for (auto& child : item.children) {
                    const auto separator = child.command.find_last_of('.');
                    if (separator == String::npos) continue;
                    const auto prefix = StringView(child.command).substr(0U, separator);
                    const auto name = StringView(child.command).substr(separator + 1U);
                    if (prefix != "coupon.convert" && prefix != "coupon.copy") continue;
                    const auto type = std::find_if(types.begin(), types.end(), [name](const auto& value) {
                        return value.first == name;
                    });
                    if (type == types.end()) continue;
                    child.enabled = !text.empty() &&
                        (prefix == "coupon.copy" || couponConversion(comm, properties, text,
                                                                       type->second) != text);
                    found = true;
                }
            }
            return found;
        });
}

void createKeyCodeTimingMenu(const Commons& comm, const ComboPtr& combo,
                             const bool convert, const bool copy,
                             const ChooserProperties& properties) {
    if (!combo) return;
    createKeyCodeTimingMenu(comm, std::static_pointer_cast<lib::ComboBox>(combo),
                            combo->menu, convert, copy, properties);
}

void createKeyCodeTimingMenu(const Commons& comm,
    const std::shared_ptr<lib::ComboBox>& combo, const std::shared_ptr<lib::Menu>& menu,
    const bool convert, const bool copy, const ChooserProperties& properties) {
    ::cwxeditor::cwx::perf::TraceScope trace_createKeyCodeTimingMenu(__FILE__, __LINE__);
    if (!combo || !menu) return;
    const auto kinds = std::vector<std::pair<String, FKCKind>>{
        {"use", FKCKind::Use}, {"success", FKCKind::Success},
        {"failure", FKCKind::Failure}, {"has_not", FKCKind::HasNot}};
    if (convert) {
        lib::MenuItem parent{properties.messages.keyCodeTiming, "keycode.convert"};
        for (const auto& [name, kind] : kinds)
            parent.children.push_back({name, "keycode.convert." + name});
        menu->items.push_back(std::move(parent));
    }
    if (copy) {
        lib::MenuItem parent{properties.messages.copyConvertedKeyCode, "keycode.copy"};
        for (const auto& [name, kind] : kinds)
            parent.children.push_back({name, "keycode.copy." + name});
        menu->items.push_back(std::move(parent));
    }
    const auto oldHandler = menu->on_command;
    menu->on_command = [weak = std::weak_ptr<lib::ComboBox>(combo), comm, properties, kinds, convert, copy,
                               oldHandler](const StringView command) {
        if (oldHandler) oldHandler(command);
        const auto combo = weak.lock();
        if (!combo) return;
        const auto separator = command.find_last_of('.');
        if (separator == StringView::npos) return;
        const auto prefix = command.substr(0U, separator);
        const auto name = command.substr(separator + 1U);
        const auto it = std::find_if(kinds.begin(), kinds.end(), [&name](const auto& value) {
            return value.first == name;
        });
        if (it == kinds.end()) return;
        if (prefix == "keycode.convert" && convert) {
            ::cwxeditor::cwx::perf::TraceScope trace_keycode_convert(__FILE__, __LINE__);
            if (combo->edit_text.empty()) return;
            const auto value = keyCodeConversion(comm, properties, combo->edit_text, it->second);
            if (value == combo->edit_text) return;
            customtext::TMM(combo).setText(value);
            combo->selection_start = value.size();
            combo->selection_length = 0U;
            if (const auto chooser = std::dynamic_pointer_cast<ChooserCombo>(combo))
                chooser->edit_selection = std::pair<std::size_t, std::size_t>{value.size(), value.size()};
            if (comm.refreshToolBar) comm.refreshToolBar();
        } else if (prefix == "keycode.copy" && copy && !combo->edit_text.empty()) {
            ::cwxeditor::cwx::perf::TraceScope trace_keycode_copy(__FILE__, __LINE__);
            writeClipboard(comm, keyCodeConversion(comm, properties, combo->edit_text, it->second));
            if (comm.refreshToolBar) comm.refreshToolBar();
        }
    };
    attachComboMenuState(combo, menu,
        [comm, properties, kinds, convert, copy](std::vector<lib::MenuItem>& items,
                                                  const StringView text) {
            bool found = false;
            for (auto& item : items) {
                if (item.command == "keycode.convert") {
                    item.enabled = convert && !text.empty();
                    found = true;
                } else if (item.command == "keycode.copy") {
                    item.enabled = copy && !text.empty();
                    found = true;
                }
                for (auto& child : item.children) {
                    const auto separator = child.command.find_last_of('.');
                    if (separator == String::npos) continue;
                    const auto prefix = StringView(child.command).substr(0U, separator);
                    const auto name = StringView(child.command).substr(separator + 1U);
                    if (prefix != "keycode.convert" && prefix != "keycode.copy") continue;
                    const auto kind = std::find_if(kinds.begin(), kinds.end(), [name](const auto& value) {
                        return value.first == name;
                    });
                    if (kind == kinds.end()) continue;
                    child.enabled = !text.empty() &&
                        (prefix == "keycode.copy" || keyCodeConversion(comm, properties, text,
                                                                         kind->second) != text);
                    found = true;
                }
            }
            return found;
        });
}

namespace {

template <typename T>
void appendVariableChoices(std::vector<FlagChooserBase::Choice>& result,
                           const flag::FlagDirPtr& dir, const ChooserProperties& properties,
                           const VariableKind kind, const bool local, const bool recursive) {
    if (!dir) return;
    if (recursive) {
        std::vector<flag::FlagDirPtr> dirs = dir->subDirs();
        std::stable_sort(dirs.begin(), dirs.end(), [&properties](const auto& left, const auto& right) {
            return logicalLess(left->name(), right->name(), properties.logicalSort);
        });
        for (const auto& child : dirs)
            appendVariableChoices<T>(result, child, properties, kind, local, true);
    }
    std::vector<std::shared_ptr<T>> values;
    if constexpr (std::is_same_v<T, flag::Flag>) values = dir->flags();
    else if constexpr (std::is_same_v<T, flag::Step>) values = dir->steps();
    else values = dir->variants();
    std::stable_sort(values.begin(), values.end(), [&properties](const auto& left, const auto& right) {
        return logicalLess(left->name(), right->name(), properties.logicalSort);
    });
    for (const auto& value : values) {
        if (!value) continue;
        FlagChooserBase::Choice choice;
        choice.path = value->path();
        choice.name = value->name();
        choice.directory = dir->path();
        choice.cwx_path = value->cwxPath(true);
        choice.image = variableImage(properties, kind, local);
        result.push_back(std::move(choice));
    }
}

}  // namespace

FlagChooserBase::FlagChooserBase(Commons comm, SummaryPtr summ, UseCounterPtr uc,
                                 Parent parent, const bool saveExpanded,
                                 const bool canSelectNothing, const bool random,
                                 const VariableKind kind, ChooserProperties properties)
    : comm_(std::move(comm)), summ_(std::move(summ)), uc_(std::move(uc)),
      properties_(std::move(properties)), kind_(kind),
       can_select_nothing_(canSelectNothing), random_(random), parent_(parent),
       save_expanded_(saveExpanded) {
    editor_commons_lifetime_ = properties_.editor_commons
        ? properties_.editor_commons->lifetimeToken() : std::weak_ptr<const int>{};
    if (save_expanded_ && liveEditorCommons()) {
        const auto& common = *liveEditorCommons();
        expanded_ = kind_ == VariableKind::Flag ? common.flagDirExpanded :
                    kind_ == VariableKind::Step ? common.stepDirExpanded : common.variantDirExpanded;
    }
    initControl();
    connectObservers();
}

FlagChooserBase::~FlagChooserBase() {
    if (save_expanded_) {
        try {
            saveExpanded();
        } catch (...) {
            // デストラクターでもCommonsの監視接続を必ず解除する。
        }
    }
    disconnectObservers();
}

void FlagChooserBase::connectObservers() {
    auto* const common = liveEditorCommons();
    // Dは読み込み済みのシナリオSummaryにだけ、FlagChooserの監視先を設定する。
    if (!common || !summ_ || summ_->scenarioPath().empty() || ref_flag_connection_) return;
    ref_flag_connection_ = common->refFlagAndStep.add(
        [this](const flag::FlagList& flags, const flag::StepList& steps,
               const flag::VariantList& variants) { refFlags(flags, steps, variants); });
    del_flag_connection_ = common->delFlagAndStep.add(
        [this](const flag::FlagList& flags, const flag::StepList& steps,
               const flag::VariantList& variants) { delFlags(flags, steps, variants); });
    // ディレクトリ変更も意図的に更新する。Dは個々の変数だけを監
    // 視するため、古いディレクトリ名や選択肢が残る場合がある。
    ref_flag_dir_connection_ = common->refFlagDir.add(
        [this](const commons::FlagList&) { refreshFlags(); });
    del_flag_dir_connection_ = common->delFlagDir.add(
        [this](const commons::FlagList&) { refreshFlags(); });
    ref_skin_connection_ = common->refSkin.add([this] { refreshFlags(); });
    ref_data_version_connection_ = common->refDataVersion.add([this] { refreshFlags(); });
    repl_text_connection_ = common->replText.add([this] { refreshFlags(); });
    repl_id_connection_ = common->replID.add([this] { refreshFlags(); });
    select_style_connection_ = common->refVarSelectStyle.add([this] {
        const auto* prop = liveEditorCommons()->prop();
        if (!prop) return;
        const bool tree = prop->var().selectVariableWithTree.value;
        if (tree == properties_.selectVariableWithTree) return;
        properties_.selectVariableWithTree = tree;
        initControl();
    });
}

void FlagChooserBase::disconnectObservers() noexcept {
    auto* const common = liveEditorCommons();
    if (!common) return;
    common->refFlagAndStep.remove(ref_flag_connection_);
    common->delFlagAndStep.remove(del_flag_connection_);
    common->refFlagDir.remove(ref_flag_dir_connection_);
    common->delFlagDir.remove(del_flag_dir_connection_);
    common->refSkin.remove(ref_skin_connection_);
    common->refDataVersion.remove(ref_data_version_connection_);
    common->replText.remove(repl_text_connection_);
    common->replID.remove(repl_id_connection_);
    common->refVarSelectStyle.remove(select_style_connection_);
    select_style_connection_ = 0U;
    ref_flag_connection_ = del_flag_connection_ = ref_flag_dir_connection_ = 0U;
    del_flag_dir_connection_ = ref_skin_connection_ = ref_data_version_connection_ = 0U;
    repl_text_connection_ = repl_id_connection_ = 0U;
}

WidgetPtr FlagChooserBase::widget() const noexcept {
    // Dの実装自身がCompositeであり、GridData
    // は内部のTree/Tableだけでなく選択部
    // 全体へ適用する。ここで子を返すとパネルの要求
    // 寸法がなくなり、メニューカード編集などでツリ
    // ーと「すべて展開」が潰れたり重なったりする。
    return panel_;
}

void FlagChooserBase::flagIncSearch() {
    ::cwxeditor::cwx::perf::TraceScope trace_FlagChooserBase_flagIncSearch(__FILE__, __LINE__);
    if (flagIncSearch_) flagIncSearch_->startIncSearch();
}

void FlagChooserBase::refFlags(const flag::FlagList& flags, const flag::StepList& steps,
                               const flag::VariantList& variants) {
    const auto changed = !((kind_ == VariableKind::Flag && flags.empty()) ||
                           (kind_ == VariableKind::Step && steps.empty()) ||
                           (kind_ == VariableKind::Variant && variants.empty()));
    if (!changed) return;
    auto* const common = liveEditorCommons();
    if (!common) {
        refreshFlags();
        return;
    }
    auto& shared = kind_ == VariableKind::Flag ? common->flagDirExpanded :
                   kind_ == VariableKind::Step ? common->stepDirExpanded : common->variantDirExpanded;
    ExpandedMapSnapshot snapshot(shared, editor_commons_lifetime_);
    saveExpanded();
    snapshot.publish(expanded_);
    refreshFlags();
}

void FlagChooserBase::delFlags(const flag::FlagList& flags, const flag::StepList& steps,
                               const flag::VariantList& variants) {
    refFlags(flags, steps, variants);
}

std::vector<std::shared_ptr<flag::Flag>> FlagChooserBase::flags(
    const flag::FlagDirPtr& dir) const {
    return dir ? dir->flags() : std::vector<std::shared_ptr<flag::Flag>>{};
}

std::vector<std::shared_ptr<flag::Flag>> FlagChooserBase::allFlags(
    const flag::FlagDirPtr& dir) const {
    return dir ? dir->allFlags() : std::vector<std::shared_ptr<flag::Flag>>{};
}

bool FlagChooserBase::hasVar(const flag::FlagDirPtr& dir) const {
    return dir && dir->hasFlag();
}

std::vector<std::shared_ptr<flag::Step>> FlagChooserBase::steps(
    const flag::FlagDirPtr& dir) const {
    return dir ? dir->steps() : std::vector<std::shared_ptr<flag::Step>>{};
}

std::vector<std::shared_ptr<flag::Step>> FlagChooserBase::allSteps(
    const flag::FlagDirPtr& dir) const {
    return dir ? dir->allSteps() : std::vector<std::shared_ptr<flag::Step>>{};
}

bool FlagChooserBase::hasStep(const flag::FlagDirPtr& dir) const {
    return dir && dir->hasStep();
}

std::vector<std::shared_ptr<flag::Variant>> FlagChooserBase::variants(
    const flag::FlagDirPtr& dir) const {
    return dir ? dir->variants() : std::vector<std::shared_ptr<flag::Variant>>{};
}

std::vector<std::shared_ptr<flag::Variant>> FlagChooserBase::allVariants(
    const flag::FlagDirPtr& dir) const {
    return dir ? dir->allVariants() : std::vector<std::shared_ptr<flag::Variant>>{};
}

bool FlagChooserBase::hasVariant(const flag::FlagDirPtr& dir) const {
    return dir && dir->hasVariant();
}

std::vector<FlagChooserBase::Choice> FlagChooserBase::choices() const {
    std::vector<Choice> result;
    if (can_select_nothing_) {
        result.push_back({{}, properties_.messages.noFlagRef, {}, {},
                          properties_.images.emptyIcon, false, true, false, false});
    }
    if (random_) {
        result.push_back({properties_.randomValue, properties_.messages.randomValue, {}, {},
                          properties_.images.randomValue, false, true, false, true});
        if (kind_ == VariableKind::Step && (!summ_ || !summ_->legacy())) {
            result.push_back({properties_.selectedPlayerCardNumber,
                              properties_.messages.selectedPlayerValue, {}, {},
                              properties_.images.selectedPlayerCardNumber, false, true, true,
                              false});
        }
    }
    std::vector<flag::FlagDirPtr> roots;
    flag::FlagDirPtr local_root;
    if (uc_) {
        ::cwxeditor::cwx::perf::TraceScope trace_flag_has_counter(__FILE__, __LINE__);
        if (const auto owner = std::dynamic_pointer_cast<flag::LocalVariableOwner>(uc_->owner())) {
            ::cwxeditor::cwx::perf::TraceScope trace_flag_local_owner(__FILE__, __LINE__);
            if (const auto local = owner->flagDirRoot()) {
                local_root = local;
                roots.push_back(local);
            }
        }
    }
    if (summ_) {
        if (const auto global = summ_->flagDirRoot()) {
            if (std::find(roots.begin(), roots.end(), global) == roots.end()) roots.push_back(global);
        }
    }
    for (std::size_t index = 0U; index < roots.size(); ++index) {
        const bool local = roots[index] == local_root;
        switch (kind_) {
        case VariableKind::Flag:
            appendVariableChoices<flag::Flag>(result, roots[index], properties_, kind_, local,
                                              true);
            break;
        case VariableKind::Step:
            appendVariableChoices<flag::Step>(result, roots[index], properties_, kind_, local,
                                              true);
            break;
        case VariableKind::Variant:
            appendVariableChoices<flag::Variant>(result, roots[index], properties_, kind_, local,
                                                 true);
            break;
        }
    }
    return result;
}

bool FlagChooserBase::matches(const StringView value) const {
    return !flagIncSearch_ || flagIncSearch_->match(value);
}

void FlagChooserBase::notifySelection(const StringView old) {
    // 最初の通知がこの制御部を解放したり、ハンドラーを置換したりする場合がある。
    const auto callbacks = old != selected_ ? modEvent : std::vector<Callback>{};
    const auto sameCallbacks = modEventWithSame;
    for (const auto& callback : callbacks) if (callback) callback();
    for (const auto& callback : sameCallbacks) if (callback) callback();
}

void FlagChooserBase::selectChoice(const Choice& value) {
    const auto old = selected_;
    selected_ = value.path;
    selected_with_dir_ = value.directory_item ? value.path : value.path;
    notifySelection(old);
}

void FlagChooserBase::setSelectedFromTree(const std::vector<lib::TreeItemId>& values) {
    if (values.empty()) return;
    const auto it = tree_choices_.find(values.front());
    if (it == tree_choices_.end()) return;
    selected_with_dir_ = it->second.path;
    if (!it->second.directory_item) selectChoice(it->second);
    else notifySelection(selected_); // Dは葉を維持するが、WithSameは常に通知する。
}

void FlagChooserBase::setSelectedFromList(const std::vector<std::size_t>& values) {
    if (values.empty()) return;
    if (const auto value = rowChoice<Choice>(list_, values.front())) selectChoice(*value);
}

FlagChooserBase::PreparedTreeChoices FlagChooserBase::prepareTreeChoices(const std::vector<Choice>& values) const {
    PreparedTreeChoices prepared;
    prepared.next_id = next_tree_id_;
    std::unordered_map<String, lib::TreeItemId> previous_ids;
    for (const auto& [id, choice] : tree_choices_)
        previous_ids.emplace((choice.directory_item ? "D:" : "V:") + choice.path, id);
    const auto allocate = [&](const Choice& choice) {
        const auto found = previous_ids.find((choice.directory_item ? "D:" : "V:") + choice.path);
        if (found != previous_ids.end()) return found->second;
        if (!prepared.next_id || prepared.next_id == std::numeric_limits<lib::TreeItemId>::max())
            throw std::overflow_error("Chooser tree IDs exhausted");
        return prepared.next_id++;
    };
    std::unordered_map<String, lib::TreeItemId> directory_ids;
    String local_directory;
    String local_heading;
    String local_root_path;
    String local_root_cwx_path;
    if (uc_) {
        if (const auto owner = std::dynamic_pointer_cast<flag::LocalVariableOwner>(uc_->owner())) {
            if (const auto root = owner->flagDirRoot()) {
                local_root_path = root->path();
                local_root_cwx_path = root->cwxPath(true);
                for (const auto& part : splitPath(root->path())) {
                    if (part.empty()) continue;
                    if (!local_directory.empty()) local_directory += '\\';
                    local_directory += part;
                }
                const auto* prop = liveEditorCommons() ? liveEditorCommons()->prop() : nullptr;
                const auto format = prop ? prop->requiredMsgs().localVariablesOwner.value : properties_.messages.localVariablesOwner;
                local_heading = ::cwxeditor::cwx::utils::tryFormat(format, owner->id(), owner->name());
            }
        }
    }
    auto addDirectory = [&](const String& directory, const String& cwxDirectoryPath) {
        if (directory.empty()) {
            // カード内の変数ルートは、参照パスが空の場合が多い。Dは
            // それでも[id.name]の所有元ノードを表示する。こ
            // の見出しは表示専用で、保存する変数パスには加えない。
            if (local_heading.empty()) return lib::TreeItemId{};
            const auto existing = directory_ids.find(directory);
            if (existing != directory_ids.end()) return existing->second;
            Choice choice;
            choice.name = local_heading;
            choice.directory = directory;
            choice.cwx_path = cwxDirectoryPath;
            choice.directory_item = true;
            choice.image = properties_.images.flagDir;
            const auto id = allocate(choice);
            directory_ids.emplace(directory, id);
            prepared.choices.emplace(id, choice);
            prepared.nodes.push_back({id, {}, choice.name, {}, choice.image,
                                      properties_.expandChooserItems, false, false});
            return id;
        }
        const auto existing = directory_ids.find(directory);
        if (existing != directory_ids.end()) return existing->second;
        std::vector<String> parts;
        for (const auto& part : splitPath(directory))
            if (!part.empty()) parts.push_back(part);
        std::vector<String> directoryCwxPaths;
        auto currentCwxPath = cwxDirectoryPath;
        for (std::size_t index = 0U; index < parts.size(); ++index) {
            directoryCwxPaths.push_back(currentCwxPath);
            currentCwxPath = ::cwxeditor::cwx::path::cpparent(currentCwxPath);
        }
        std::reverse(directoryCwxPaths.begin(), directoryCwxPaths.end());
        const bool localPath = directory.starts_with('\\');
        lib::TreeItemId parentId{};
        String accumulated;
        for (std::size_t index = 0U; index < parts.size(); ++index) {
            const auto& part = parts[index];
            // Dのパス分割は、先頭・末尾の区切りに表示ノード
            // を作らない。共通の分割は空要素を保持するため、
            // 末尾に区切りがあるパスで、フォルダーとフラグ
            // の間に空のディレクトリを作らないようにする。
            if (part.empty()) continue;
            if (!accumulated.empty()) accumulated.push_back('\\');
            accumulated += part;
            auto found = directory_ids.find(accumulated);
            if (found != directory_ids.end()) {
                parentId = found->second;
                continue;
            }
            Choice choice;
            choice.path = accumulated;
            choice.name = accumulated == local_directory && !local_heading.empty() ? local_heading : part;
            choice.directory = localPath ? String("\\") + accumulated + "\\"
                                         : accumulated + "\\";
            choice.directory_item = true;
            if (index < directoryCwxPaths.size()) choice.cwx_path = directoryCwxPaths[index];
            choice.image = properties_.images.flagDir;
            const auto id = allocate(choice);
            directory_ids.emplace(accumulated, id);
            prepared.choices.emplace(id, choice);
            prepared.nodes.push_back({id, parentId, choice.name, accumulated, choice.image,
                                    expanded_.contains(choice.directory)
                                        ? expanded_.at(choice.directory)
                                        : properties_.expandChooserItems,
                                    false, false});
            parentId = id;
        }
        return parentId;
    };
    // Dはフラグのディレクトリを再帰処理する前に、
    // 「参照なし」やランダムのシステム項目を作る
    // 。末尾に置くと表示順と既定の選択が変わる。
    for (const auto& value : values) {
        if (value.special && value.directory.empty()) {
            const auto id = allocate(value);
            prepared.choices.emplace(id, value);
            prepared.nodes.push_back({id, {}, value.name, value.path, value.image, false, false,
                                    false});
        }
    }
    // Dは逐次検索で配下の全変数が隠れても、空でないロー
    // カル所有元のツリーを追加する。単体版では同じ見出し
    // を維持し、ALLは既存の絞り込み表示の形を保つ。
    bool hasLocalValues = false;
    if (uc_) {
        if (const auto owner = std::dynamic_pointer_cast<flag::LocalVariableOwner>(uc_->owner())) {
            if (const auto root = owner->flagDirRoot()) {
                switch (kind_) {
                case VariableKind::Flag: hasLocalValues = root->hasFlag(); break;
                case VariableKind::Step: hasLocalValues = root->hasStep(); break;
                case VariableKind::Variant: hasLocalValues = root->hasVariant(); break;
                }
            }
        }
    }
    if (hasLocalValues && !lib::editor_window_host_options().all_mode)
        (void)addDirectory(local_root_path, local_root_cwx_path);
    for (const auto& value : values) {
        if (value.directory_item || value.special) continue;
        const auto parentId = addDirectory(
            value.directory, ::cwxeditor::cwx::path::cpparent(value.cwx_path));
        const auto id = allocate(value);
        prepared.choices.emplace(id, value);
        prepared.nodes.push_back({id, parentId, value.name, value.path, value.image, false, false,
                                false});
    }
    return prepared;
}

void FlagChooserBase::refreshFlags() {
    const auto revision = ++refresh_revision_;
    const auto old = selected_;
    const auto all = choices();
    std::vector<Choice> visible;
    for (const auto& value : all) {
        if (value.special || matches(value.path)) visible.push_back(value);
    }
    if (list_) {
        // Dはグローバル・ローカル変数を完全パスでまとめて
        // 並べ替える。再帰の巡回順が適切なのはツリーだけ。
        std::stable_sort(visible.begin(), visible.end(), [this](const Choice& a, const Choice& b) {
            if (a.special != b.special) return a.special;
            if (a.special) return false;
            return logicalLess(a.path, b.path, properties_.logicalSort);
        });
    }
    const bool searchable = std::any_of(all.begin(), all.end(), [](const Choice& value) {
        return !value.special;
    });
    if (!tree_ && list_) {
        auto table = list_;
        std::vector<lib::TableRow> rows;
        std::vector<customtable::TableRowState> states;
        std::vector<std::size_t> selection;
        String selected;
        const auto same = [&old](const Choice& value) {
            return value.random || value.selected_player
                ? normalizedSystemValue(value.path) == normalizedSystemValue(old)
                : value.path == old;
        };
        const bool exists = std::any_of(all.begin(), all.end(), same);
        rows.reserve(visible.size());
        states.reserve(visible.size());
        for (std::size_t index = 0; index < visible.size(); ++index) {
            const auto& value = visible[index];
            lib::TableRow row;
            row.cells.push_back({value.special || value.path.empty() ? value.name : value.path,
                                 value.path, value.image, false, true});
            rows.push_back(std::move(row));
            customtable::TableRowState state;
            state.data = value;
            states.push_back(std::move(state));
            if (selection.empty() && same(value)) {
                selection = {index};
                selected = value.random || value.selected_player ? old : value.path;
            }
        }
        // Dは削除と、検索で現在値が隠れた状態を区別する。選択対
        // 象が削除済み・存在しない場合だけ最初の行へ切り替える。
        if (selection.empty() && !exists && !visible.empty()) {
            selection = {0};
            selected = visible.front().path;
        }
        const auto callbacks = old != selected ? modEvent : std::vector<Callback>{};
        const std::weak_ptr<int> lifetime=callback_lifetime_;
        const auto current=[this,lifetime,revision] {
            return !lifetime.expired() && refresh_revision_==revision;
        };
        if(!table->replaceRows(std::move(rows), std::move(selection), std::move(states), {}, current)) return;
        visible_choices_.swap(visible);
        selected_.swap(selected);
        selected_with_dir_.clear();
        can_inc_search_ = searchable;
        table->set_enabled(searchable);
        // コールバックに空の表や対応のずれたモデルを見せない。通知は
        // 選択部を閉じ得るため、通知後はthisへアクセスしない。
        for (const auto& callback : callbacks) if (callback) callback();
        table->redraw();
        return;
    }
    if (!tree_) return;
    auto tree = tree_;
    const std::weak_ptr<int> lifetime = callback_lifetime_;
    auto prepared = prepareTreeChoices(visible);
    std::vector<lib::TreeItemId> selection;
    String selected;
    const auto same = [&old](const Choice& value) {
        return value.random || value.selected_player
            ? normalizedSystemValue(value.path) == normalizedSystemValue(old)
            : value.path == old;
    };
    const bool exists = std::any_of(all.begin(), all.end(), same);
    // Dは代替選択を探す際、折りたたまれたディレクトリよ
    // り先に、展開済みのディレクトリ内の葉を優先する。
    std::optional<lib::TreeItemId> fallback;
    if (!exists) {
        lib::TreeView candidate;
        candidate.nodes = prepared.nodes;
        for (const auto index : lib::visible_tree_indices(candidate)) {
            const auto id = candidate.nodes[index].id;
            if (!prepared.choices.at(id).directory_item) { fallback = id; break; }
        }
    }
    for (const auto& node : prepared.nodes) {
        const auto& value = prepared.choices.at(node.id);
        if (value.directory_item || (exists && !same(value)) || (fallback && node.id != *fallback)) continue;
        selection = {node.id};
        selected = exists && (value.random || value.selected_player) ? old : value.path;
        break;
    }
    const auto callbacks = old != selected ? modEvent : std::vector<Callback>{};
    const auto reveal = selection.empty() ? std::optional<lib::TreeItemId>{} : selection.front();
    if (!lib::replace_tree_nodes(tree, std::move(prepared.nodes), std::move(selection), reveal,
                                 [this, lifetime, revision] {
                                     return !lifetime.expired() && refresh_revision_ == revision;
                                 })) return;
    // 共通状態の公開とモデルの対応表の交換の間で、プロバイダーを呼ばない。
    tree_choices_.swap(prepared.choices);
    next_tree_id_ = prepared.next_id;
    visible_choices_.swap(visible);
    selected_.swap(selected);
    selected_with_dir_.clear();
    can_inc_search_ = searchable;
    tree->set_enabled(searchable);
    if (all_expanded_) {
        all_expanded_->set_enabled(searchable);
        all_expanded_->checked = std::all_of(tree->nodes.begin(), tree->nodes.end(), [&](const auto& node) {
            return node.expanded || std::none_of(tree->nodes.begin(), tree->nodes.end(),
                [&](const auto& child) { return child.parent == node.id; });
        });
    }
    for (const auto& callback : callbacks) if (callback) callback();
}

void FlagChooserBase::openFlagView() {
    String value;
    if (tree_ && !tree_->selection.empty()) {
        const auto it = tree_choices_.find(tree_->selection.front());
        if (it != tree_choices_.end()) value = it->second.cwx_path;
    } else if (list_ && !list_->selection.empty()) {
        if (const auto choice = rowChoice<Choice>(list_, list_->selection.front()))
            value = choice->cwx_path;
    }
    const auto open = properties_.openCWXPath;
    try {
        if (!value.empty() && open) open(path::cpaddattr(value, "shallow"), false);
    } catch (const std::exception& error) {
        // Dは選択操作の境界でExceptionを捕捉して記録する。コールバックが選択
        // 部を終了してから例外を出す場合があるため、コピーした呼び出し先を保持する。
        (void)::cwxeditor::cwx::utils::printStackTrace();
        ::cwxeditor::cwx::utils::debugln(error.what());
    }
}

bool FlagChooserBase::canOpenView() const {
    if (tree_ && !tree_->selection.empty()) {
        const auto it = tree_choices_.find(tree_->selection.front());
        return it != tree_choices_.end() && !it->second.cwx_path.empty();
    }
    if (list_ && !list_->selection.empty())
        if (const auto choice = rowChoice<Choice>(list_, list_->selection.front()))
            return !choice->cwx_path.empty();
    return false;
}

void FlagChooserBase::saveExpanded() {
    if (!tree_) return;
    if (!save_expanded_) {
        // この制御部のその場での再構築状態を、Commonsへ公開せずに保持する。
        for (const auto& node : tree_->nodes) {
            const auto it = tree_choices_.find(node.id);
            if (it != tree_choices_.end() && it->second.directory_item &&
                !it->second.directory.empty())
                expanded_[it->second.directory] = node.expanded;
        }
        return;
    }
    const auto localKey = String("\\") + properties_.localVariablePrefix + "\\";
    const auto local = expanded_.find(localKey);
    const bool localExpanded = local == expanded_.end() ? true : local->second;
    ExpandedMap current;
    for (const auto& node : tree_->nodes) {
        const auto it = tree_choices_.find(node.id);
        if (it != tree_choices_.end() && it->second.directory_item &&
            !it->second.directory.empty())
            current[it->second.directory] = node.expanded;
    }
    if (!current.contains(localKey)) current[localKey] = localExpanded;
    expanded_.swap(current);
    if (auto* const common = liveEditorCommons()) {
        auto& shared = kind_ == VariableKind::Flag ? common->flagDirExpanded :
                       kind_ == VariableKind::Step ? common->stepDirExpanded : common->variantDirExpanded;
        shared = expanded_;
    }
}

void FlagChooserBase::initControl() {
    auto query = flagIncSearch_ ? flagIncSearch_->requestedSearchText() : String{};
    if (query.empty()) query = search_query_;
    const bool searching = flagIncSearch_ && (flagIncSearch_->isOpen() || !query.empty());
    const auto mode = flagIncSearch_ ? flagIncSearch_->searchType() : incsearch::SearchType::Contains;
    if (panel_) {
        saveExpanded();
        callback_lifetime_ = std::make_shared<int>(0);
        flagIncSearch_.reset();
        const auto old_children = panel_->children();
        for (const auto& child : old_children) panel_->remove(child);
    }
    const std::weak_ptr<int> lifetime = callback_lifetime_;
    const bool first = !panel_;
    if (first) panel_ = std::make_shared<lib::Panel>();
    auto layout = std::make_shared<lib::LinearLayout>(lib::Orientation::Vertical);
    layout->spacing = 5.0F;
    panel_->set_layout(layout);
    if (first && parent_) parent_->add(panel_);
    tree_.reset();
    list_.reset();
    all_expanded_.reset();
    if (properties_.selectVariableWithTree) {
        tree_ = std::make_shared<lib::TreeView>();
        tree_->multi_select = false;
        tree_->set_layout_item({.horizontal = lib::Align::Stretch,
                                .vertical = lib::Align::Stretch,
                                .minimum = {
                                    static_cast<float>(properties_.flagsWidth),
                                    static_cast<float>(properties_.flagsHeight)},
                                .preferred = {
                                    static_cast<float>(properties_.flagsWidth),
                                    static_cast<float>(properties_.flagsHeight)},
                                .grow = 1.0F});
        panel_->add(tree_);
        all_expanded_ = createAllExpandedButton(properties_, panel_.get(), tree_);
        tree_->on_selection_changed = [this, lifetime](const auto& values) {
            if (!lifetime.expired()) setSelectedFromTree(values);
        };
        tree_->on_expansion_changed = [this, lifetime](const auto id, const bool expanded) {
            if (lifetime.expired()) return;
            const auto it = tree_choices_.find(id);
            if (it != tree_choices_.end() && it->second.directory_item &&
                !it->second.directory.empty())
                expanded_[it->second.directory] = expanded;
            checkAllExpanded(all_expanded_, tree_);
        };
        tree_->on_activate = [this, lifetime](const auto) { if (!lifetime.expired()) openFlagView(); };
        flagIncSearch_ = std::make_shared<incsearch::IncSearch>(
            comm_.services, tree_.get(), [this, lifetime] { return !lifetime.expired() && can_inc_search_; });
    } else {
        list_ = customtable::rangeSelectableTable(panel_.get(), customtable::STYLE_NONE,
                                                  comm_.services);
        list_->set_layout_item({.horizontal = lib::Align::Stretch,
                                .vertical = lib::Align::Stretch,
                                .minimum = {
                                    static_cast<float>(properties_.flagsWidth),
                                    static_cast<float>(properties_.flagsHeight)},
                                .preferred = {
                                    static_cast<float>(properties_.flagsWidth),
                                    static_cast<float>(properties_.flagsHeight)},
                                .grow = 1.0F});
        (void)list_->addColumn("", static_cast<float>(properties_.flagsWidth));
        list_->on_selection_changed = [this, lifetime](const auto& values) {
            if (!lifetime.expired()) setSelectedFromList(values);
        };
        list_->on_cell_activate = [this, lifetime](const auto, const auto) {
            if (!lifetime.expired()) openFlagView();
        };
        flagIncSearch_ = std::make_shared<incsearch::IncSearch>(
            comm_.services, list_.get(), [this, lifetime] { return !lifetime.expired() && can_inc_search_; });
    }
    flagIncSearch_->settings(detail::searchSettings(properties_));
    const auto choice_widget = tree_ ? std::static_pointer_cast<Widget>(tree_) :
        std::static_pointer_cast<Widget>(list_);
    attachChoiceActions(comm_, choice_widget, flagIncSearch_, lifetime,
        ::cwxeditor::cwx::types::MenuID::OpenAtVarView,
        [this] { return can_inc_search_; },
        [this] { return properties_.openCWXPath && canOpenView(); },
        [this] { openFlagView(); });
    const auto input = choice_widget->on_text_input;
    choice_widget->on_text_input = [this, lifetime, input](lib::TextInputEvent& event) {
        if (input) input(event);
        if (!lifetime.expired() && event.handled && !event.composing && !event.text.empty())
            search_query_ += event.text;
    };
    if (searching) {
        flagIncSearch_->setSearchType(mode);
        flagIncSearch_->startIncSearch(query);
    }
    flagIncSearch_->modEvent.push_back([this, lifetime] {
        if (lifetime.expired()) return;
        search_query_ = flagIncSearch_->searchText();
        detail::storeSearchType(properties_, flagIncSearch_->searchType());
        refreshFlags();
    });
    refreshFlags();
}

void FlagChooserBase::selected(String pathValue) {
    selected_ = std::move(pathValue);
    refreshFlags();
}

String FlagChooserBase::selectedWithDir() const {
    if (tree_ && !tree_->selection.empty()) {
        const auto it = tree_choices_.find(tree_->selection.front());
        if (it != tree_choices_.end()) return it->second.path;
    }
    return selected_;
}

AreaChooserBase::AreaChooserBase(Commons comm, SummaryPtr summ, Parent parent,
                                 const AreaChoiceKind kind, const bool startArea,
                                 ChooserProperties properties)
    : comm_(std::move(comm)), summ_(std::move(summ)), parent_(parent), kind_(kind),
      properties_(std::move(properties)), start_area_enabled_(startArea) {
    editor_commons_lifetime_ = properties_.editor_commons
        ? properties_.editor_commons->lifetimeToken() : std::weak_ptr<const int>{};
    if (start_area_enabled_ && kind_ != AreaChoiceKind::Area)
        throw std::invalid_argument("Only Area supports a start-area chooser");
    if (start_area_enabled_ && summ_) start_area_ = summ_->startArea();
    if (const auto* common = liveEditorCommons()) {
        if (kind_ == AreaChoiceKind::Area) expanded_ = common->flagAreaExpanded;
        else if (kind_ == AreaChoiceKind::Battle) expanded_ = common->flagBattleExpanded;
        else if (kind_ == AreaChoiceKind::Package) expanded_ = common->flagPackageExpanded;
    }
    initControl();
    connectObservers();
}

AreaChooserBase::~AreaChooserBase() {
    // Dは選択部の破棄時に、現在表示しているフォルダーの展
    // 開状態を保存する。この公開規則を全ホストで共有する。
    try {
        saveExpanded();
        if (auto* const common = liveEditorCommons()) {
            switch (kind_) {
            case AreaChoiceKind::Area: common->flagAreaExpanded = expanded_; break;
            case AreaChoiceKind::Battle: common->flagBattleExpanded = expanded_; break;
            case AreaChoiceKind::Package: common->flagPackageExpanded = expanded_; break;
            case AreaChoiceKind::Cast:
            case AreaChoiceKind::Info: break;
            }
        }
    } catch (...) {
        // デストラクターでもCommonsの監視接続を必ず解除する。
    }
    disconnectObservers();
}

void AreaChooserBase::connectObservers() {
    auto* const common = liveEditorCommons();
    if (!common || ref_connection_) return;
    switch (kind_) {
    case AreaChoiceKind::Area:
        ref_connection_ = common->refArea.add([this](const commons::AreaPtr&) { refA(); });
        del_connection_ = common->delArea.add([this](const commons::AreaPtr&) { refA(); });
        break;
    case AreaChoiceKind::Battle:
        ref_connection_ = common->refBattle.add([this](const commons::BattlePtr&) { refA(); });
        del_connection_ = common->delBattle.add([this](const commons::BattlePtr&) { refA(); });
        break;
    case AreaChoiceKind::Package:
        ref_connection_ = common->refPackage.add([this](const commons::PackagePtr&) { refA(); });
        del_connection_ = common->delPackage.add([this](const commons::PackagePtr&) { refA(); });
        break;
    case AreaChoiceKind::Cast:
        ref_connection_ = common->refCast.add([this](const commons::CastCardPtr&) { refA(); });
        del_connection_ = common->delCast.add([this](const commons::CastCardPtr&) { refA(); });
        break;
    case AreaChoiceKind::Info:
        ref_connection_ = common->refInfo.add([this](const commons::InfoCardPtr&) { refA(); });
        del_connection_ = common->delInfo.add([this](const commons::InfoCardPtr&) { refA(); });
        break;
    }
    ref_area_table_connection_ = common->refAreaTable.add([this] { refA(); });
    table_style_connection_ = common->refTableViewStyle.add([this] {
        const auto* prop = liveEditorCommons()->prop();
        if (!prop) return;
        const bool tree = prop->var().showAreaDirTree.value;
        if (tree == properties_.showAreaDirTree) return;
        properties_.showAreaDirTree = tree;
        initControl();
    });
    ref_skin_connection_ = common->refSkin.add([this] { refA(); });
    ref_data_version_connection_ = common->refDataVersion.add([this] { refA(); });
    repl_text_connection_ = common->replText.add([this] { refA(); });
    repl_id_connection_ = common->replID.add([this] { refA(); });
}

void AreaChooserBase::disconnectObservers() noexcept {
    auto* const common = liveEditorCommons();
    if (!common) return;
    switch (kind_) {
    case AreaChoiceKind::Area:
        common->refArea.remove(ref_connection_); common->delArea.remove(del_connection_); break;
    case AreaChoiceKind::Battle:
        common->refBattle.remove(ref_connection_); common->delBattle.remove(del_connection_); break;
    case AreaChoiceKind::Package:
        common->refPackage.remove(ref_connection_); common->delPackage.remove(del_connection_); break;
    case AreaChoiceKind::Cast:
        common->refCast.remove(ref_connection_); common->delCast.remove(del_connection_); break;
    case AreaChoiceKind::Info:
        common->refInfo.remove(ref_connection_); common->delInfo.remove(del_connection_); break;
    }
    common->refAreaTable.remove(ref_area_table_connection_);
    common->refTableViewStyle.remove(table_style_connection_);
    table_style_connection_ = 0U;
    common->refSkin.remove(ref_skin_connection_);
    common->refDataVersion.remove(ref_data_version_connection_);
    common->replText.remove(repl_text_connection_);
    common->replID.remove(repl_id_connection_);
    ref_connection_ = del_connection_ = ref_area_table_connection_ = 0U;
    ref_skin_connection_ = ref_data_version_connection_ = 0U;
    repl_text_connection_ = repl_id_connection_ = 0U;
}

WidgetPtr AreaChooserBase::widget() const noexcept {
    return panel_;
}

void AreaChooserBase::areaIncSearch() {
    ::cwxeditor::cwx::perf::TraceScope trace_AreaChooserBase_areaIncSearch(__FILE__, __LINE__);
    if (areaIncSearch_) areaIncSearch_->startIncSearch();
}

void AreaChooserBase::refA() {
    ::cwxeditor::cwx::perf::TraceScope trace_AreaChooserBase_refA(__FILE__, __LINE__);
    if (kind_ != AreaChoiceKind::Area && kind_ != AreaChoiceKind::Battle &&
        kind_ != AreaChoiceKind::Package) {
        refreshAreas();
        return;
    }
    // ツリーを置換する前に、表示位置調整や既定で展開される祖先を保持する。
    saveExpanded();
    if (auto* const common = liveEditorCommons()) {
        auto& shared = kind_ == AreaChoiceKind::Area ? common->flagAreaExpanded :
                       kind_ == AreaChoiceKind::Battle ? common->flagBattleExpanded :
                                                         common->flagPackageExpanded;
        ExpandedMapSnapshot snapshot(shared, editor_commons_lifetime_);
        snapshot.publish(expanded_);
        refreshAreas();
        return;
    }
    refreshAreas();
}

namespace {

template <typename Collection>
void appendAreaChoices(std::vector<AreaChooserBase::Choice>& result,
                       const Collection& values,
                       const ChooserProperties& properties,
                       const AreaChoiceKind kind) {
    using T = typename Collection::value_type::element_type;
    for (const auto& value : values) {
        if (!value) continue;
        AreaChooserBase::Choice choice;
        choice.id = value->id();
        choice.name = value->name();
        if constexpr (std::is_base_of_v<area::AbstractArea, T>) {
            choice.base_name = value->baseName();
            choice.directory = value->dirName();
            // Dの検索・表の文字列用には対象名を保持する。ツリーはIDを別に追加する。
        } else {
            choice.base_name = choice.name;
        }
        choice.cwx_path = objectPath(value);
        switch (kind) {
        case AreaChoiceKind::Area: choice.image = properties.images.area; break;
        case AreaChoiceKind::Battle: choice.image = properties.images.battle; break;
        case AreaChoiceKind::Package: choice.image = properties.images.packages; break;
        case AreaChoiceKind::Cast: choice.image = properties.images.casts; break;
        case AreaChoiceKind::Info: choice.image = properties.images.info; break;
        }
        result.push_back(std::move(choice));
    }
}

}  // namespace

std::vector<AreaChooserBase::Choice> AreaChooserBase::choices() const {
    std::vector<Choice> result;
    if (!summ_) return result;
    switch (kind_) {
    case AreaChoiceKind::Area: appendAreaChoices(result, summ_->areas(), properties_, kind_); break;
    case AreaChoiceKind::Battle: appendAreaChoices(result, summ_->battles(), properties_, kind_); break;
    case AreaChoiceKind::Package: appendAreaChoices(result, summ_->packages(), properties_, kind_); break;
    case AreaChoiceKind::Cast: appendAreaChoices(result, summ_->casts(), properties_, kind_); break;
    case AreaChoiceKind::Info: appendAreaChoices(result, summ_->infos(), properties_, kind_); break;
    }
    // Dは名前順に別途並べた資源一覧ではなく、Summaryの配列を巡回する。並
    // べ替えるのはディレクトリだけで、最初の配列要素を開始時の代替選択に使う。
    return result;
}

AreaChooserBase::PreparedTreeChoices AreaChooserBase::prepareTreeChoices(
    const std::vector<Choice>& values, std::uint64_t start) const {
    PreparedTreeChoices result;
    result.next_id = next_tree_id_;
    std::unordered_map<String, lib::TreeItemId> old_ids;
    const auto key = [](const Choice& value) {
        return value.id ? "R:"+std::to_string(value.id) : "D:"+lowerAscii(value.directory);
    };
    for (const auto& [id,value] : tree_choices_) old_ids.emplace(key(value),id);
    const auto allocate = [&](const Choice& value) {
        if (const auto it=old_ids.find(key(value)); it!=old_ids.end()) return it->second;
        if (!result.next_id || result.next_id==std::numeric_limits<lib::TreeItemId>::max())
            throw std::overflow_error("Chooser tree IDs exhausted");
        return result.next_id++;
    };
    std::vector<String> directories;
    for (const auto& value:values) if (!value.directory.empty()) directories.push_back(value.directory);
    std::stable_sort(directories.begin(),directories.end(),[&](const auto& a,const auto& b) {
        return logicalLess(lowerAscii(a),lowerAscii(b),properties_.logicalSort);
    });
    std::unordered_map<String,lib::TreeItemId> directory_ids;
    for (const auto& directory:directories) {
        lib::TreeItemId parent{};
        String accumulated;
        for (const auto& part:splitPath(directory)) {
            if (part.empty()) continue;
            if (!accumulated.empty()) accumulated.push_back('\\');
            accumulated+=part;
            const auto folded=lowerAscii(accumulated);
            if (const auto it=directory_ids.find(folded); it!=directory_ids.end()) { parent=it->second; continue; }
            Choice value;
            value.directory=accumulated;
            value.name=value.base_name=part;
            value.image=properties_.images.areaDir;
            const auto id=allocate(value);
            directory_ids.emplace(folded,id);
            result.choices.emplace(id,value);
            result.nodes.push_back({id,parent,part,accumulated,value.image,
                expanded_.contains(folded)?expanded_.at(folded):properties_.expandChooserItems});
            parent=id;
        }
        directory_ids[lowerAscii(directory)]=parent;
    }
    for (const auto& value:values) {
        const auto id=allocate(value);
        const auto found=directory_ids.find(lowerAscii(value.directory));
        const auto parent=found==directory_ids.end()?lib::TreeItemId{}:found->second;
        const auto icon=start_area_enabled_ && value.id==start ? properties_.images.startArea:value.image;
        result.choices.emplace(id,value);
        result.nodes.push_back({id,parent,areaDisplayName(value.id,value.base_name),std::to_string(value.id),icon});
    }
    // Dは整列したサブディレクトリを先に挿入し、Summary
    // 順で各資源を親へ追加する。その構成を表示順に平坦化する。
    std::unordered_map<lib::TreeItemId,std::vector<std::size_t>> children;
    for(std::size_t i=0;i<result.nodes.size();++i) children[result.nodes[i].parent].push_back(i);
    std::vector<std::size_t> pending;
    const auto append = [&](lib::TreeItemId parent) {
        if (const auto it=children.find(parent); it!=children.end())
            pending.insert(pending.end(),it->second.rbegin(),it->second.rend());
    };
    append(0);
    std::vector<lib::TreeNode> ordered;
    ordered.reserve(result.nodes.size());
    while(!pending.empty()) {
        const auto index=pending.back(); pending.pop_back();
        const auto id=result.nodes[index].id;
        ordered.push_back(std::move(result.nodes[index]));
        append(id);
    }
    result.nodes.swap(ordered);
    return result;
}

bool AreaChooserBase::matches(const StringView value) const {
    return !areaIncSearch_ || areaIncSearch_->match(value);
}

void AreaChooserBase::notifySelection(const std::uint64_t old, const bool selection_event) {
    if (start_area_enabled_ || (!selection_event && old == selected_)) return;
    const auto callbacks = modEvent;
    for (const auto& callback : callbacks) if (callback) callback();
}

void AreaChooserBase::setSelectedFromTree(const std::vector<lib::TreeItemId>& values) {
    const auto old = selected_;
    const std::weak_ptr<int> lifetime = callback_lifetime_;
    if (!values.empty()) {
        const auto it = tree_choices_.find(values.front());
        if (it != tree_choices_.end() && it->second.id != 0U) {
            ++refresh_revision_;
            selected_ = it->second.id;
            updateImages();
            if (lifetime.expired()) return;
        }
    }
    // Dは同じ葉の再選択やディレクトリにも、このStartArea以外のイベントを通知する。
    notifySelection(old, true);
}

void AreaChooserBase::setSelectedFromList(const std::vector<std::size_t>& values) {
    const auto old = selected_;
    const std::weak_ptr<int> lifetime = callback_lifetime_;
    if (!values.empty()) {
        if (const auto value = rowChoice<Choice>(list_, values.front())) {
            ++refresh_revision_;
            selected_ = value->id;
            updateImages();
            if (lifetime.expired()) return;
        }
    }
    notifySelection(old, true);
}

void AreaChooserBase::refreshAreas() {
    const auto revision=++refresh_revision_;
    const std::weak_ptr<int> lifetime=callback_lifetime_;
    const auto old=selected_;
    const auto all=choices();
    const bool searchable=!all.empty();
    const bool exists=std::any_of(all.begin(),all.end(),[&](const auto& value){return value.id==old;});
    auto start=start_area_;
    if(start_area_enabled_ && std::none_of(all.begin(),all.end(),[&](const auto& value){return value.id==start;}))
        start=all.empty()?0:all.front().id;
    const bool start_changed=start!=start_area_;
    std::vector<Choice> visible;
    for(const auto& value:all) if(matches(value.name)) visible.push_back(value);
    const auto current=[this,lifetime,revision] { return !lifetime.expired() && refresh_revision_==revision; };
    if (!tree_ && list_) {
        auto table=list_;
        std::vector<lib::TableRow> rows;
        std::vector<customtable::TableRowState> states;
        std::vector<std::size_t> selection;
        std::uint64_t selected{};
        rows.reserve(visible.size()); states.reserve(visible.size());
        for(std::size_t i=0;i<visible.size();++i) {
            const auto& value=visible[i];
            const auto icon=start_area_enabled_ && value.id==start?properties_.images.startArea:value.image;
            lib::TableRow row;
            row.cells.push_back({std::to_string(value.id),std::to_string(value.id),{},false,true});
            row.cells.push_back({value.name,value.name,icon,false,true});
            rows.push_back(std::move(row));
            customtable::TableRowState state; state.data=value;
            states.push_back(std::move(state));
            if(selection.empty() && value.id==old) {
                ::cwxeditor::cwx::perf::TraceScope trace_area_restore_selection(__FILE__, __LINE__); selection={i}; selected=value.id; }
        }
        if(selection.empty() && !exists && !visible.empty()) {selection={0}; selected=visible.front().id;}
        const auto callbacks=(start_area_enabled_?start_changed:old!=selected)?modEvent:std::vector<lib::Action>{};
        const auto reveal=selection.empty()?std::optional<std::size_t>{}:selection.front();
        if(!table->replaceRows(std::move(rows),std::move(selection),std::move(states),reveal,current)) return;
        visible_choices_.swap(visible);
        selected_=selected; start_area_=start; can_inc_search_=searchable;
        table->set_enabled(searchable);
        for(const auto& callback:callbacks) if(callback) callback();
        table->redraw();
        return;
    }
    if(!tree_) return;
    auto tree=tree_;
    auto prepared=prepareTreeChoices(visible,start);
    std::optional<lib::TreeItemId> fallback;
    if(!exists) {
        lib::TreeView candidate; candidate.nodes=prepared.nodes;
        for(auto index:lib::visible_tree_indices(candidate)) {
            const auto id=candidate.nodes[index].id;
            if(prepared.choices.at(id).id) {fallback=id; break;}
        }
    }
    std::uint64_t selected{};
    std::vector<lib::TreeItemId> selection;
    for(const auto& node:prepared.nodes) {
        const auto& value=prepared.choices.at(node.id);
        if(!value.id || (exists && value.id!=old) || (fallback && node.id!=*fallback)) continue;
        selection={node.id}; selected=value.id; break;
    }
    const auto callbacks=(start_area_enabled_?start_changed:old!=selected)?modEvent:std::vector<lib::Action>{};
    const auto reveal=selection.empty()?std::optional<lib::TreeItemId>{}:selection.front();
    if(!lib::replace_tree_nodes(tree,std::move(prepared.nodes),std::move(selection),reveal,current)) return;
    tree_choices_.swap(prepared.choices); next_tree_id_=prepared.next_id;
    visible_choices_.swap(visible);
    selected_=selected; start_area_=start; can_inc_search_=searchable;
    tree->set_enabled(searchable);
    if(all_expanded_) {
        all_expanded_->set_enabled(searchable);
        all_expanded_->checked=std::all_of(tree->nodes.begin(),tree->nodes.end(),[&](const auto& node) {
            return node.expanded || std::none_of(tree->nodes.begin(),tree->nodes.end(),
                [&](const auto& child){return child.parent==node.id;});
        });
    }
    for(const auto& callback:callbacks) if(callback) callback();
}

void AreaChooserBase::updateImages() {
    updateImages(start_area_);
}

void AreaChooserBase::updateImages(std::uint64_t start) {
    if (!start_area_enabled_) return;
    if (tree_) {
        // Dはモデルデータを持つ資源項目だけを再帰処理する。ディレクト
        // リはデータを持たないため、その部分木全体を意図的に飛ばす。
        std::unordered_map<lib::TreeItemId, std::vector<std::size_t>> children;
        for (std::size_t index = 0; index < tree_->nodes.size(); ++index)
            children[tree_->nodes[index].parent].push_back(index);
        std::vector<std::size_t> pending = children[0];
        while (!pending.empty()) {
            const auto index = pending.back(); pending.pop_back();
            auto& node = tree_->nodes[index];
            const auto it = tree_choices_.find(node.id);
            if (it == tree_choices_.end() || !it->second.id) continue;
            node.icon = it->second.id == start ? properties_.images.startArea : it->second.image;
            if (const auto found = children.find(node.id); found != children.end())
                pending.insert(pending.end(), found->second.begin(), found->second.end());
        }
    }
    if(list_) {
        auto table=list_;
        const auto revision=table->row_order_revision;
        std::vector<customtable::TableCellDisplayUpdate> updates;
        const auto& states=table->rowStates();
        for(std::size_t i=0;i<table->rows.size();++i) {
            const auto* value=std::any_cast<Choice>(&states[i].data);
            if(!value || table->rows[i].cells.size()<2) continue;
            const auto icon=start_area_enabled_ && value->id==start?properties_.images.startArea:value->image;
            if(table->rows[i].cells[1].icon!=icon)
                updates.push_back({i,1,{},icon});
        }
        (void)table->updateCellDisplays(updates,revision);
    }
}

void AreaChooserBase::activateChoice(std::uint64_t id) {
    if(!start_area_enabled_) { openAreaView(); return; }
    if(!id || id==start_area_) return;
    const auto callbacks=modEvent;
    startArea(id);
    for(const auto& callback:callbacks) if(callback) callback();
}

ResourceId AreaChooserBase::image(const lib::TreeItemId item) const {
    const auto it = tree_choices_.find(item);
    if (it == tree_choices_.end()) return {};
    return it->second.image;
}

void AreaChooserBase::openAreaView() {
    String value;
    if (tree_ && !tree_->selection.empty()) {
        const auto it = tree_choices_.find(tree_->selection.front());
        if (it != tree_choices_.end()) value = it->second.cwx_path;
    } else if (list_ && !list_->selection.empty()) {
        if (const auto choice = rowChoice<Choice>(list_, list_->selection.front()))
            value = choice->cwx_path;
    }
    const auto open = properties_.openCWXPath;
    try {
        if (!value.empty() && open) open(path::cpaddattr(value, "shallow"), false);
    } catch (const std::exception& error) {
        // Dは選択操作の境界でExceptionを捕捉して記録する。コールバックが選択
        // 部を終了してから例外を出す場合があるため、コピーした呼び出し先を保持する。
        (void)::cwxeditor::cwx::utils::printStackTrace();
        ::cwxeditor::cwx::utils::debugln(error.what());
    }
}

bool AreaChooserBase::canOpenView() const {
    if (tree_ && !tree_->selection.empty()) {
        const auto it = tree_choices_.find(tree_->selection.front());
        return it != tree_choices_.end() && !it->second.cwx_path.empty();
    }
    if (list_ && !list_->selection.empty())
        if (const auto choice = rowChoice<Choice>(list_, list_->selection.front()))
            return !choice->cwx_path.empty();
    return false;
}

void AreaChooserBase::saveExpanded() {
    if (!tree_) return;
    // DのAreaChooser.saveExpandedは、表示ツリーから保存するマップを作り直す。
    expanded_.clear();
    for (const auto& node : tree_->nodes) {
        const auto it = tree_choices_.find(node.id);
        if (it != tree_choices_.end() && it->second.id == 0U)
            expanded_[lowerAscii(it->second.directory)] = node.expanded;
    }
}

void AreaChooserBase::initControl() {
    const bool treeMode=(kind_==AreaChoiceKind::Area || kind_==AreaChoiceKind::Battle ||
        kind_==AreaChoiceKind::Package) && properties_.showAreaDirTree;
    if (panel_ && ((treeMode && tree_) || (!treeMode && list_))) {
        // 同じスタイルの再適用で、呼び出し側が組み込んだ表示を無効
        // にしない。Dはコントロールを保持してモデルをその場で再
        // 公開する。再構築前に、表示ツリーの展開状態を取得する。
        saveExpanded();
        refreshAreas();
        return;
    }
    const bool searching = areaIncSearch_ && areaIncSearch_->isOpen();
    const auto query = areaIncSearch_ ? areaIncSearch_->searchText() : String{};
    const auto mode = areaIncSearch_ ? areaIncSearch_->searchType() : incsearch::SearchType::Contains;
    if (panel_) {
        saveExpanded();
        callback_lifetime_ = std::make_shared<int>(0);
        areaIncSearch_.reset();
        const auto old_children = panel_->children();
        for (const auto& child : old_children) panel_->remove(child);
        tree_.reset();
        list_.reset();
        all_expanded_.reset();
    }
    const std::weak_ptr<int> lifetime=callback_lifetime_;
    const bool first = !panel_;
    if (first) panel_=std::make_shared<lib::Panel>();
    auto layout=std::make_shared<lib::LinearLayout>(lib::Orientation::Vertical);
    layout->spacing=5;
    panel_->set_layout(layout);
    if (first) panel_->set_layout_item({.horizontal=lib::Align::Stretch,.vertical=lib::Align::Stretch,.grow=1});
    const lib::LayoutItem item{.horizontal=lib::Align::Stretch,.vertical=lib::Align::Stretch,
        .preferred={static_cast<float>(properties_.nameTableWidth),static_cast<float>(properties_.nameTableHeight)},.grow=1};
    if(treeMode) {
        tree_=std::make_shared<lib::TreeView>();
        tree_->set_layout_item(item);
        panel_->add(tree_);
        all_expanded_=createAllExpandedButton(properties_,panel_.get(),tree_);
        tree_->on_selection_changed=[this,lifetime](const auto& values) {
            if(!lifetime.expired()) setSelectedFromTree(values);
        };
        tree_->on_expansion_changed=[this,lifetime](auto id,bool expanded) {
            if(lifetime.expired()) return;
            const auto it=tree_choices_.find(id);
            if(it!=tree_choices_.end() && !it->second.id) expanded_[lowerAscii(it->second.directory)]=expanded;
            checkAllExpanded(all_expanded_,tree_);
        };
        tree_->on_activate=[this,lifetime](auto id) {
            if(lifetime.expired() || !panel_->enabled() || !tree_->enabled()) return;
            const auto it=tree_choices_.find(id);
            if(it!=tree_choices_.end() && it->second.id) activateChoice(it->second.id);
        };
        areaIncSearch_=std::make_shared<incsearch::IncSearch>(
            comm_.services,tree_.get(),[this,lifetime]{return !lifetime.expired() && can_inc_search_;});
    } else {
        list_=customtable::rangeSelectableTable(panel_.get(),customtable::STYLE_NONE,comm_.services);
        list_->multi_select=false;
        list_->header_visible=false;
        list_->edit_on_double_click=false;
        list_->set_layout_item(item);
        (void)list_->addColumn("",static_cast<float>(properties_.idColumn));
        (void)list_->addColumn("",static_cast<float>(properties_.nameTableWidth));
        list_->on_column_resized=[this,lifetime](const std::size_t column,
                                                 const float width) {
            if(lifetime.expired() || column!=0U || !properties_.storeIdColumn) return;
            properties_.storeIdColumn(static_cast<std::int32_t>(std::lround(width)));
        };
        list_->on_selection_changed=[this,lifetime](const auto& values) {
            if(!lifetime.expired()) setSelectedFromList(values);
        };
        list_->on_cell_activate=[this,lifetime](auto row,auto) {
            if(lifetime.expired() || !panel_->enabled() || !list_->enabled()) return;
            if(const auto* value=rowChoice<Choice>(list_,row)) activateChoice(value->id);
        };
        areaIncSearch_=std::make_shared<incsearch::IncSearch>(
            comm_.services,list_.get(),[this,lifetime]{return !lifetime.expired() && can_inc_search_;});
    }
    areaIncSearch_->settings(detail::searchSettings(properties_));
    attachChoiceActions(comm_, tree_ ? std::static_pointer_cast<Widget>(tree_) :
        std::static_pointer_cast<Widget>(list_), areaIncSearch_, lifetime,
        kind_ == AreaChoiceKind::Cast || kind_ == AreaChoiceKind::Info
            ? ::cwxeditor::cwx::types::MenuID::OpenAtCardView
            : ::cwxeditor::cwx::types::MenuID::OpenAtTableView,
        [this] { return can_inc_search_; },
        [this] { return properties_.openCWXPath && canOpenView(); },
        [this] { openAreaView(); });
    if (searching) {
        areaIncSearch_->setSearchType(mode);
        areaIncSearch_->startIncSearch(query);
    }
    areaIncSearch_->modEvent.push_back([this,lifetime]{
        if(lifetime.expired()) return;
        detail::storeSearchType(properties_, areaIncSearch_->searchType());
        refreshAreas();
    });
    refreshAreas();
    if(first && parent_) parent_->add(panel_);
}

void AreaChooserBase::selected(const std::uint64_t id) {
    selected_ = id;
    refreshAreas();
}

void AreaChooserBase::startArea(const std::uint64_t id) {
    if(!start_area_enabled_ || start_area_==id) return;
    updateImages(id);
    ++refresh_revision_;
    start_area_=id;
    // Dの設定関数は値とアイコンだけを変更する。ユーザーの実行操作ではmodEventを通知する。
}

void checkAllExpanded(const std::shared_ptr<lib::CheckBox>& button,
                      const std::shared_ptr<lib::TreeView>& tree) {
    if (!button || !tree) return;
    std::unordered_map<lib::TreeItemId, std::size_t> childCount;
    for (const auto& node : tree->nodes) ++childCount[node.parent];
    bool expanded = true;
    for (const auto& node : tree->nodes) {
        if (childCount[node.id] != 0U && !node.expanded) {
            expanded = false;
            break;
        }
    }
    button->checked = expanded;
}

std::shared_ptr<lib::CheckBox> createAllExpandedButton(
    const ChooserProperties& properties, Parent parent,
    const std::shared_ptr<lib::TreeView>& tree) {
    auto button = std::make_shared<lib::CheckBox>(properties.messages.allExpanded);
    if (parent) parent->add(button);
    button->on_changed = [tree](const bool expanded) {
        if (!tree) return;
        std::unordered_map<lib::TreeItemId, std::vector<lib::TreeItemId>> children;
        for (const auto& node : tree->nodes) children[node.parent].push_back(node.id);
        std::vector<lib::TreeItemId> changed;
        for (auto& node : tree->nodes) {
            if (!children[node.id].empty()) {
                node.expanded = expanded;
                changed.push_back(node.id);
            }
        }
        // 所有元へ通知する前に、展開状態全体を公開する。
        // 所有元は個別の展開操作と同じマップを記録する。
        const auto notify = tree->on_expansion_changed;
        if (notify) for (const auto id : changed) notify(id, expanded);
    };
    checkAllExpanded(button, tree);
    return button;
}

template class FlagChooser<flag::Flag, false, false>;
template class FlagChooser<flag::Flag, true, false>;
template class FlagChooser<flag::Step, false, false>;
template class FlagChooser<flag::Step, true, true>;
template class FlagChooser<flag::Variant, false, false>;
template class AreaChooser<area::Area, false>;
template class AreaChooser<area::Area, true>;
template class AreaChooser<area::Battle, false>;
template class AreaChooser<area::Package, false>;
template class AreaChooser<card::CastCard, false>;
template class AreaChooser<card::InfoCard, false>;

namespace {

// この翻訳単位でDの全テンプレート入口の型検査を行う。呼び出し側
// は引き続き、ヘッダーで別のComboBox派生部品を選べる。
[[maybe_unused]] void instantiateChooserTemplates() {
    Commons comm;
    SummaryPtr summary;
    UseCounterPtr counter;
    (void)createCouponCombo<>(comm, summary, counter, nullptr, {},
                              CouponComboType::AllCoupons, {});
    (void)createGossipCombo<>(comm, summary, nullptr, {}, {});
    (void)createCompleteStampCombo<>(comm, summary, nullptr, {}, {});
    (void)createKeyCodeCombo<>(comm, summary, nullptr, {}, {}, true);
    (void)createCellNameCombo<>(comm, summary, nullptr, {}, {});
    (void)createCardGroupCombo<>(comm, summary, nullptr, {}, {});
    (void)createSelectionCombo<>(comm, nullptr, {}, {});
    (void)createVariableCombo<>(comm, summary, counter, nullptr, {}, {});
    (void)createVariableCombo<ChooserCombo, flag::Step>(comm, summary, counter,
                                                        nullptr, {}, {});
}

}  // namespace

}  // namespace cwxeditor::cwx::editor::gui::dwt::chooser
