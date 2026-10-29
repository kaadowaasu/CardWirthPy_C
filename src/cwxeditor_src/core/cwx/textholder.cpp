#include "cwxeditor_src/core/cwx/textholder.hpp"
#include "cwxeditor_src/core/cwx/usecounter.hpp"

#include <stdexcept>

namespace cwxeditor::cwx::textholder {
namespace {

// 入れ子のテキスト記号には所有するUserと移動先のルートが必要だが、CouponNamesUser
// の生成元の利用権を延長してはならない。直接のCouponUserハンドルは別途UserObserver::lock()
// を使い、その生成元の参照関係を保持する。
[[nodiscard]] Owner retainHolderLocalOwner(const Owner& candidate) {
    if (!candidate) return {};
    if (const auto user =
            std::dynamic_pointer_cast<usecounter::CouponUser>(candidate)) {
        const auto canonical_observer =
            static_cast<lib::Observer<usecounter::CouponUser>>(
                user->weak_from_this());
        if (const auto canonical = canonical_observer.lock()) {
            return std::static_pointer_cast<
                ::cwxeditor::cwx::path::CWXPath>(canonical);
        }
    }
    return path::retainCallbackOwner(candidate);
}

struct HolderOwnerLeaseBundle {
    std::shared_ptr<SimpleTextHolder> holder;
    Owner local_owner;
    Owner navigation_owner;
};

template <typename User>
class TextUserOwnerLease final : public usecounter::UserLeaseState<User> {
    struct Bundle {
        lib::Owner<User> user;
        Owner local_owner;
        Owner navigation_owner;
    };

public:
    TextUserOwnerLease(const Owner& local_owner, const Observer& navigation_owner)
        : local_owner_(local_owner), navigation_owner_(navigation_owner) {}

    void navigationOwner(const Observer& owner) { navigation_owner_ = owner; }
    [[nodiscard]] Owner localOwner() const {
        // 弱いUserObserverを保持するが、ここでは正規の所有元だけを取得する
        // 。直接のCouponUserハンドルは自身のAPIで生成元の利用権を得る。
        const auto canonical =
            static_cast<lib::Observer<usecounter::CouponUser>>(coupon_owner_);
        if (auto user = canonical.lock())
            return std::static_pointer_cast<::cwxeditor::cwx::path::CWXPath>(
                std::move(user));
        return retainHolderLocalOwner(local_owner_.lock());
    }
    void localOwner(const Owner& owner) const {
        if (!owner) return;
        if (const auto user = std::dynamic_pointer_cast<usecounter::CouponUser>(owner)) {
            auto observer = user->weak_from_this();
            if (!observer.expired()) {
                coupon_owner_ = std::move(observer);
                local_owner_.reset();
                return;
            }
        }
        local_owner_ = owner;
    }

    [[nodiscard]] lib::Owner<User> leaseOwner(
        const lib::Owner<User>& canonical) const override {
        if (!canonical) return {};
        auto local = localOwner();
        if (local) localOwner(local);
        const auto navigation = path::retainCallbackOwner(navigation_owner_.lock());
        if (!local && !navigation) return canonical;
        auto bundle = std::make_shared<Bundle>(
            Bundle{canonical, local, navigation});
        return lib::Owner<User>(std::move(bundle), canonical.get());
    }

private:
    mutable Observer local_owner_;
    mutable usecounter::UserObserver<usecounter::CouponUser> coupon_owner_;
    Observer navigation_owner_;
};

} // namespace

SimpleTextHolder::SimpleTextHolder(Owner owner,
                                   const TextHolderType type,
                                   const std::string_view cwx_path_category,
                                   FindIndex find_index)
    : type_(type),
      owner_(owner),
      local_owner_(std::move(owner)),
      cwx_path_category_(cwx_path_category),
      find_index_(std::move(find_index)) {
    text_user_owner_lease_states_[0] =
        std::make_shared<TextUserOwnerLease<usecounter::FlagUser>>(local_owner_, owner_);
    text_user_owner_lease_states_[1] =
        std::make_shared<TextUserOwnerLease<usecounter::StepUser>>(local_owner_, owner_);
    text_user_owner_lease_states_[2] =
        std::make_shared<TextUserOwnerLease<usecounter::VariantUser>>(local_owner_, owner_);
}

Owner SimpleTextHolder::retainForCallback() {
    const auto self = weak_from_this().lock();
    if (!self) return {};
    auto local = retainHolderLocalOwner(local_owner_);
    if (!local && text_user_owner_lease_states_[0])
        local = std::static_pointer_cast<TextUserOwnerLease<usecounter::FlagUser>>(
            text_user_owner_lease_states_[0])->localOwner();
    if (!local && text_user_owner_lease_states_[1])
        local = std::static_pointer_cast<TextUserOwnerLease<usecounter::StepUser>>(
            text_user_owner_lease_states_[1])->localOwner();
    if (!local && text_user_owner_lease_states_[2])
        local = std::static_pointer_cast<TextUserOwnerLease<usecounter::VariantUser>>(
            text_user_owner_lease_states_[2])->localOwner();
    if (local) {
        if (text_user_owner_lease_states_[0])
            std::static_pointer_cast<TextUserOwnerLease<usecounter::FlagUser>>(
                text_user_owner_lease_states_[0])->localOwner(local);
        if (text_user_owner_lease_states_[1])
            std::static_pointer_cast<TextUserOwnerLease<usecounter::StepUser>>(
                text_user_owner_lease_states_[1])->localOwner(local);
        if (text_user_owner_lease_states_[2])
            std::static_pointer_cast<TextUserOwnerLease<usecounter::VariantUser>>(
                text_user_owner_lease_states_[2])->localOwner(local);
    }
    auto navigation = path::retainCallbackOwner(owner_.lock());
    if (!local && !navigation)
        return std::static_pointer_cast<::cwxeditor::cwx::path::CWXPath>(self);
    auto bundle = std::make_shared<HolderOwnerLeaseBundle>(
        HolderOwnerLeaseBundle{self, std::move(local), std::move(navigation)});
    return Owner(std::move(bundle),
                 static_cast<::cwxeditor::cwx::path::CWXPath*>(this));
}

void SimpleTextHolder::owner(Owner value) {
    owner_ = std::move(value);
    if (text_user_owner_lease_states_[0])
        std::static_pointer_cast<TextUserOwnerLease<usecounter::FlagUser>>(
            text_user_owner_lease_states_[0])->navigationOwner(owner_);
    if (text_user_owner_lease_states_[1])
        std::static_pointer_cast<TextUserOwnerLease<usecounter::StepUser>>(
            text_user_owner_lease_states_[1])->navigationOwner(owner_);
    if (text_user_owner_lease_states_[2])
        std::static_pointer_cast<TextUserOwnerLease<usecounter::VariantUser>>(
            text_user_owner_lease_states_[2])->navigationOwner(owner_);
}

void SimpleTextHolder::retire(const bool preserveNavigation) noexcept {
    const usecounter::detail::NativeCounterRetirement native_retirement;
    removeUseCounter();
    // Dの古いCouponUserのデリゲートは、一覧の置換後もchanged()
    // をパス所有元へ転送する。破棄済みのネイティブCouponUserを参照する
    // 関数を残さず、その観測可能な通知を維持する。グループやGCによる寿命の保
    // 持は別扱いであり、この監視参照が破棄済みの所有元を復活させることはない。
    retired_change_owner_ = preserveNavigation ? owner_ : Observer{};
    changed_ = {};
    local_owner_.reset();
    if (!preserveNavigation) {
        owner_.reset();
        find_index_ = {};
    }
}

void SimpleTextHolder::notifyChange() {
    if (changed_) changed_();
    else if (const auto owner = retired_change_owner_.lock()) owner->changed();
}

void SimpleTextHolder::indexText() {
    std::vector<std::string> fonts;
    std::vector<char> colors;
    flags_.clear();
    steps_.clear();
    variants_.clear();
    msgutils::textUseItems(text_, flags_, steps_, variants_, fonts, colors);
    rebuildUseCounterUsers();
}

void SimpleTextHolder::text(const std::string_view value) {
    ::cwxeditor::cwx::perf::TraceScope trace_textholder_d190(__FILE__, __LINE__);
    if (text_ == value) return;
    ::cwxeditor::cwx::perf::TraceScope trace_textholder_d191(__FILE__, __LINE__);
    // Dと同様に、現在の利用者を解除する前に解析する。不正な新
    // しいテキストでは、以前のテキストと参照登録を維持する。
    std::vector<std::string> flags, steps, variants, fonts;
    std::vector<char> colors;
    msgutils::textUseItems(value, flags, steps, variants, fonts, colors);
    std::string next(value);
    removeTextUseCounter();
    text_ = std::move(next);
    flags_ = std::move(flags);
    steps_ = std::move(steps);
    variants_ = std::move(variants);
    rebuildUseCounterUsers();
}

bool SimpleTextHolder::changeCallback(const usecounter::FlagId& old_value,
                                      const usecounter::FlagId& new_value) {
    return changeFlagCallback(static_cast<String>(old_value),
                              static_cast<String>(new_value));
}

bool SimpleTextHolder::changeCallback(const usecounter::StepId& old_value,
                                      const usecounter::StepId& new_value) {
    return changeStepCallback(static_cast<String>(old_value),
                              static_cast<String>(new_value));
}

bool SimpleTextHolder::changeCallback(const usecounter::VariantId& old_value,
                                      const usecounter::VariantId& new_value) {
    return changeVariantCallback(static_cast<String>(old_value),
                                 static_cast<String>(new_value));
}

std::vector<char> SimpleTextHolder::namesInText() const {
    ::cwxeditor::cwx::perf::TraceScope trace_textholder_d258(__FILE__, __LINE__);
    return msgutils::namesInText(text_, false);
}

void SimpleTextHolder::setUseCounter(const UseCounterOwner& counter,
                                     const Owner& counter_owner) {
    const auto desiredCounter = counter;
    const auto desiredOwner = counter_owner;
    noteUseCounterBindingChange();
    setUseCounterImpl(desiredCounter, desiredOwner);
}

void SimpleTextHolder::noteUseCounterBindingChange() noexcept {
    ++use_counter_revision_;
    ++use_counter_binding_revision_;
}

void SimpleTextHolder::setUseCounterImpl(const UseCounterOwner& counter,
                                         const Owner& counter_owner) {
    ::cwxeditor::cwx::perf::TraceScope trace_textholder_d274(__FILE__, __LINE__);
    for (const auto& user : flag_users_) {
        ::cwxeditor::cwx::perf::TraceScope trace_textholder_d275(__FILE__, __LINE__);
        if (user) user->setUseCounter(counter);
    }
    for (const auto& user : step_users_) {
        ::cwxeditor::cwx::perf::TraceScope trace_textholder_d278(__FILE__, __LINE__);
        if (user) user->setUseCounter(counter);
    }
    for (const auto& user : variant_users_) {
        ::cwxeditor::cwx::perf::TraceScope trace_textholder_d281(__FILE__, __LINE__);
        if (user) user->setUseCounter(counter);
    }
    use_counter_ = counter;
    uc_owner_ = counter_owner;
}

void SimpleTextHolder::removeTextUseCounter() noexcept {
    ::cwxeditor::cwx::perf::TraceScope trace_textholder_d288(__FILE__, __LINE__);
    for (const auto& user : flag_users_) if (user) user->removeUseCounter();
    for (const auto& user : step_users_) if (user) user->removeUseCounter();
    for (const auto& user : variant_users_) if (user) user->removeUseCounter();
}

void SimpleTextHolder::rebuildUseCounterUsers() {
    ++use_counter_revision_;
    flag_users_.clear();
    step_users_.clear();
    variant_users_.clear();
    const auto self = selfOwner();
    for (const auto& id : flags_) {
        auto user = std::make_shared<usecounter::FlagUser>(self);
        user->setUserLeaseState(
            std::static_pointer_cast<TextUserOwnerLease<usecounter::FlagUser>>(
                text_user_owner_lease_states_[0]));
        if (use_counter_) user->setUseCounter(use_counter_);
        user->flag(id);
        flag_users_.push_back(std::move(user));
    }
    for (const auto& id : steps_) {
        auto user = std::make_shared<usecounter::StepUser>(self);
        user->setUserLeaseState(
            std::static_pointer_cast<TextUserOwnerLease<usecounter::StepUser>>(
                text_user_owner_lease_states_[1]));
        if (use_counter_) user->setUseCounter(use_counter_);
        user->step(id);
        step_users_.push_back(std::move(user));
    }
    for (const auto& id : variants_) {
        auto user = std::make_shared<usecounter::VariantUser>(self);
        user->setUserLeaseState(
            std::static_pointer_cast<TextUserOwnerLease<usecounter::VariantUser>>(
                text_user_owner_lease_states_[2]));
        if (use_counter_) user->setUseCounter(use_counter_);
        user->variant(id);
        variant_users_.push_back(std::move(user));
    }
}

std::vector<std::string> SimpleTextHolder::flagsInText() const {
    ::cwxeditor::cwx::perf::TraceScope trace_textholder_d227(__FILE__, __LINE__);
    std::vector<std::string> result;
    result.reserve(flag_users_.size());
    for (const auto& user : flag_users_) {
        ::cwxeditor::cwx::perf::TraceScope trace_textholder_d229(__FILE__, __LINE__);
        if (user) result.push_back(user->flag());
    }
    return result;
}

std::vector<std::string> SimpleTextHolder::stepsInText() const {
    ::cwxeditor::cwx::perf::TraceScope trace_textholder_d237(__FILE__, __LINE__);
    std::vector<std::string> result;
    result.reserve(step_users_.size());
    for (const auto& user : step_users_) {
        ::cwxeditor::cwx::perf::TraceScope trace_textholder_d239(__FILE__, __LINE__);
        if (user) result.push_back(user->step());
    }
    return result;
}

std::vector<std::string> SimpleTextHolder::variantsInText() const {
    ::cwxeditor::cwx::perf::TraceScope trace_textholder_d247(__FILE__, __LINE__);
    std::vector<std::string> result;
    result.reserve(variant_users_.size());
    for (const auto& user : variant_users_) {
        ::cwxeditor::cwx::perf::TraceScope trace_textholder_d249(__FILE__, __LINE__);
        if (user) result.push_back(user->variant());
    }
    return result;
}

void SimpleTextHolder::removeUseCounter() noexcept {
    ::cwxeditor::cwx::perf::TraceScope trace_textholder_d302(__FILE__, __LINE__);
    noteUseCounterBindingChange();
    removeTextUseCounter();
    use_counter_.reset();
    uc_owner_.reset();
}

void SimpleTextHolder::changeInText(const std::size_t index, const std::string_view id) {
    changeFlagInText(index, id);
}

void SimpleTextHolder::changeFlagInText(const std::size_t index, const std::string_view id) {
    ::cwxeditor::cwx::perf::TraceScope trace_textholder_d308(__FILE__, __LINE__);
    if (index >= flag_users_.size() || !flag_users_[index]) {
        throw std::out_of_range("SimpleTextHolder flag index is out of range");
    }
    (void)flag_users_[index]->change(usecounter::toFlagId(id));
}

void SimpleTextHolder::changeStepInText(const std::size_t index, const std::string_view id) {
    ::cwxeditor::cwx::perf::TraceScope trace_textholder_d312(__FILE__, __LINE__);
    if (index >= step_users_.size() || !step_users_[index]) {
        throw std::out_of_range("SimpleTextHolder step index is out of range");
    }
    (void)step_users_[index]->change(usecounter::toStepId(id));
}

void SimpleTextHolder::changeVariantInText(const std::size_t index, const std::string_view id) {
    ::cwxeditor::cwx::perf::TraceScope trace_textholder_d316(__FILE__, __LINE__);
    if (index >= variant_users_.size() || !variant_users_[index]) {
        throw std::out_of_range("SimpleTextHolder variant index is out of range");
    }
    (void)variant_users_[index]->change(usecounter::toVariantId(id));
}

void SimpleTextHolder::changePathInText(const std::size_t, const std::string_view) {
    throw std::invalid_argument("SimpleTextHolder does not contain path markers");
}

bool SimpleTextHolder::changeCallback(const std::string_view old_value,
                                      const std::string_view new_value) {
    return changeFlagCallback(old_value, new_value);
}

bool SimpleTextHolder::changeFlagCallback(const std::string_view old_value,
                                          const std::string_view new_value) {
    ::cwxeditor::cwx::perf::TraceScope trace_textholder_d319(__FILE__, __LINE__);
    updateIdentityUseCounter(text_, false);
    text_ = msgutils::replTextUseFlag(text_, old_value, new_value);
    updateIdentityUseCounter(text_, true);
    notifyChange();
    return true;
}

bool SimpleTextHolder::changeStepCallback(const std::string_view old_value,
                                          const std::string_view new_value) {
    ::cwxeditor::cwx::perf::TraceScope trace_textholder_d326(__FILE__, __LINE__);
    updateIdentityUseCounter(text_, false);
    text_ = msgutils::replTextUseStep(text_, old_value, new_value);
    updateIdentityUseCounter(text_, true);
    notifyChange();
    return true;
}

bool SimpleTextHolder::changeVariantCallback(const std::string_view old_value,
                                             const std::string_view new_value) {
    ::cwxeditor::cwx::perf::TraceScope trace_textholder_d333(__FILE__, __LINE__);
    updateIdentityUseCounter(text_, false);
    text_ = msgutils::replTextUseVariant(text_, old_value, new_value);
    updateIdentityUseCounter(text_, true);
    notifyChange();
    return true;
}

bool SimpleTextHolder::changePathCallback(const std::string_view,
                                          const std::string_view) {
    return false;
}

Owner SimpleTextHolder::selfOwner() {
    try {
        const auto self = shared_from_this();
        return std::static_pointer_cast<::cwxeditor::cwx::path::CWXPath>(self);
    } catch (const std::bad_weak_ptr&) {
        // unique_ptrで組み込まれたテキスト保持部（特にAreaカ
        // ード）やスタック上の保持部も、DのUseCounterの名前変更
        // 経路に参加する。安定した解放なしの制御ブロックで、所有権を取らず
        // 、保持部の寿命中だけUserBaseの弱い所有元を有効に保つ。
        if (!self_anchor_) {
            self_anchor_ = Owner(
                static_cast<::cwxeditor::cwx::path::CWXPath*>(this),
                [](::cwxeditor::cwx::path::CWXPath*) noexcept {});
        }
        return self_anchor_;
    }
}

void SimpleTextHolder::updateIdentityUseCounter(const std::string_view value,
                                                const bool add) {
    ::cwxeditor::cwx::perf::TraceScope trace_textholder_d341_359(__FILE__, __LINE__);
    if (!use_counter_ || !local_owner_) return;
    if (type_ == TextHolderType::Coupon) {
        auto user = std::dynamic_pointer_cast<usecounter::CouponUser>(local_owner_);
        // Dは二つのカウンター呼び出しの前に、この型付き所有元の不変条件をア
        // サートする。同一性の追跡を黙って省いたり、Releaseで無効に
        // なるassertに頼ったりせず、ネイティブの不変条件例外を使う。
        if (!user) throw std::logic_error("Coupon text holder requires a CouponUser owner");
        // 構築中・スタック用のアダプターを代替として残しつつ、カウンターの取
        // 得状態と後の取り消しでは、実際の利用者の制御ブロックを保持する。
        if (auto shared = user->weak_from_this().lock()) user = std::move(shared);
        if (add) use_counter_->add(usecounter::CouponId(value), user);
        else use_counter_->remove(usecounter::CouponId(value), user);
    } else if (type_ == TextHolderType::Gossip) {
        auto user = std::dynamic_pointer_cast<usecounter::GossipUser>(local_owner_);
        if (!user) throw std::logic_error("Gossip text holder requires a GossipUser owner");
        // 構築中は局所的な非所有アダプターが必要だが、実際の共有
        // 利用者ブロックができた後は、カウンター項目もそれを使う
        // 。ローカル所有元を無関係なContentの移動先所有元
        // へ置き換えず、既存のスタック・共有前の代替を維持する。
        if (auto shared = user->weak_from_this().lock()) user = std::move(shared);
        if (add) use_counter_->add(usecounter::GossipId(value), user);
        else use_counter_->remove(usecounter::GossipId(value), user);
    }
}

String SimpleTextHolder::cwxPath(const bool id) {
    ::cwxeditor::cwx::perf::TraceScope trace_textholder_d387(__FILE__, __LINE__);
    const auto parent = owner_.lock();
    if (!parent) return {};
    ::cwxeditor::cwx::perf::TraceScope trace_textholder_d388(__FILE__, __LINE__);
    if (find_index_) {
        ::cwxeditor::cwx::perf::TraceScope trace_textholder_d389(__FILE__, __LINE__);
        return path::cpjoin(parent, cwx_path_category_, find_index_(text_), id);
    }
    ::cwxeditor::cwx::perf::TraceScope trace_textholder_d391(__FILE__, __LINE__);
    return path::cpjoin(parent, cwx_path_category_, id);
}

Owner SimpleTextHolder::findCWXPath(const std::string_view path_value) {
    ::cwxeditor::cwx::perf::TraceScope trace_textholder_d397(__FILE__, __LINE__);
    if (!path::cpempty(path_value)) return {};
    return selfOwner();
}

Observer SimpleTextHolder::cwxParent() {
    return owner_;
}

void TextHolder::text(const std::string_view value) {
    ::cwxeditor::cwx::perf::TraceScope trace_textholder_d31(__FILE__, __LINE__);
    if (text_ == value) return;
    ::cwxeditor::cwx::perf::TraceScope trace_textholder_d32(__FILE__, __LINE__);
    // Dはメンバーの色一覧をoutで渡すため、解析が例外にな
    // ってもパーサー開始時にリセットする。他の配列はDでは
    // ローカルなので、解析成功までは旧値と登録を保持する。
    colors_.clear();
    std::vector<std::string> flags, steps, variants, fonts;
    std::vector<char> colors;
    msgutils::textUseItems(value, flags, steps, variants, fonts, colors);
    std::string next(value);
    removeTextUseCounter();
    text_ = std::move(next);
    flags_ = std::move(flags);
    steps_ = std::move(steps);
    variants_ = std::move(variants);
    fonts_ = std::move(fonts);
    colors_ = std::move(colors);
    rebuildUseCounterUsers();
    rebuildPathUsers();
}

std::vector<char> TextHolder::namesInText() const {
    ::cwxeditor::cwx::perf::TraceScope trace_textholder_d86(__FILE__, __LINE__);
    return msgutils::namesInText(text_, true);
}

void TextHolder::reindexMessageText() {
    flags_.clear();
    steps_.clear();
    variants_.clear();
    fonts_.clear();
    colors_.clear();
    msgutils::textUseItems(text_, flags_, steps_, variants_, fonts_, colors_);
    rebuildUseCounterUsers();
    rebuildPathUsers();
}

void TextHolder::setUseCounter(const UseCounterOwner& counter,
                               const Owner& counter_owner) {
    const auto desiredCounter = counter;
    const auto desiredOwner = counter_owner;
    ::cwxeditor::cwx::perf::TraceScope trace_textholder_d103(__FILE__, __LINE__);
    noteUseCounterBindingChange();
    for (const auto& user : path_users_) {
        ::cwxeditor::cwx::perf::TraceScope trace_textholder_d104(__FILE__, __LINE__);
        if (user) user->setUseCounter(desiredCounter);
    }
    setUseCounterImpl(desiredCounter, desiredOwner);
}

void TextHolder::removeTextUseCounter() noexcept {
    ::cwxeditor::cwx::perf::TraceScope trace_textholder_d113(__FILE__, __LINE__);
    for (const auto& user : path_users_) if (user) user->removeUseCounter();
    SimpleTextHolder::removeTextUseCounter();
}

void TextHolder::rebuildPathUsers() {
    path_users_.clear();
    const auto self = selfOwner();
    for (const auto& font_path : fonts_) {
        auto user = std::make_shared<usecounter::PathUser>(self);
        if (use_counter_) user->setUseCounter(use_counter_);
        user->path(font_path);
        path_users_.push_back(std::move(user));
    }
}

std::vector<std::string> TextHolder::fontsInText() const {
    ::cwxeditor::cwx::perf::TraceScope trace_textholder_d75(__FILE__, __LINE__);
    std::vector<std::string> result;
    result.reserve(path_users_.size());
    for (const auto& user : path_users_) {
        ::cwxeditor::cwx::perf::TraceScope trace_textholder_d77(__FILE__, __LINE__);
        if (user) result.push_back(user->path());
    }
    return result;
}

void TextHolder::changeInText(const std::size_t index, const std::string_view id) {
    changePathInText(index, id);
}

void TextHolder::changeInText(const std::size_t index,
                              const usecounter::PathId& id) {
    ::cwxeditor::cwx::perf::TraceScope trace_textholder_d124(__FILE__, __LINE__);
    if (index >= path_users_.size() || !path_users_[index]) {
        throw std::out_of_range("TextHolder font index is out of range");
    }
    (void)path_users_[index]->change(id);
}

void TextHolder::changeFlagInText(const std::size_t index, const std::string_view id) {
    SimpleTextHolder::changeFlagInText(index, id);
}

void TextHolder::changeStepInText(const std::size_t index, const std::string_view id) {
    SimpleTextHolder::changeStepInText(index, id);
}

void TextHolder::changeVariantInText(const std::size_t index, const std::string_view id) {
    SimpleTextHolder::changeVariantInText(index, id);
}

void TextHolder::changePathInText(const std::size_t index, const std::string_view id) {
    ::cwxeditor::cwx::perf::TraceScope trace_textholder_d124(__FILE__, __LINE__);
    if (index >= path_users_.size() || !path_users_[index]) {
        throw std::out_of_range("TextHolder font index is out of range");
    }
    (void)path_users_[index]->change(usecounter::toPathId(id));
}

bool TextHolder::changeCallback(const std::string_view old_value,
                                 const std::string_view new_value) {
    return changePathCallback(old_value, new_value);
}

bool TextHolder::changeCallback(const usecounter::PathId& old_value,
                                const usecounter::PathId& new_value) {
    return changePathCallback(static_cast<String>(old_value),
                              static_cast<String>(new_value));
}

bool TextHolder::changeFlagCallback(const std::string_view old_value,
                                    const std::string_view new_value) {
    return SimpleTextHolder::changeFlagCallback(old_value, new_value);
}

bool TextHolder::changeStepCallback(const std::string_view old_value,
                                    const std::string_view new_value) {
    return SimpleTextHolder::changeStepCallback(old_value, new_value);
}

bool TextHolder::changeVariantCallback(const std::string_view old_value,
                                       const std::string_view new_value) {
    return SimpleTextHolder::changeVariantCallback(old_value, new_value);
}

bool TextHolder::changePathCallback(const std::string_view old_value,
                                    const std::string_view new_value) {
    ::cwxeditor::cwx::perf::TraceScope trace_textholder_d128(__FILE__, __LINE__);
    if (!msgutils::isSPFontFile(new_value)) {
        return false;
    }
    updateIdentityUseCounter(text_, false);
    text_ = msgutils::replTextUseFont(text_, old_value, new_value);
    updateIdentityUseCounter(text_, true);
    notifyChange();
    return true;
}

}  // namespace cwxeditor::cwx::textholder
