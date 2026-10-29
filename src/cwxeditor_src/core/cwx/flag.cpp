#include "cwxeditor_src/core/cwx/flag.hpp"
#include "cwxeditor_src/core/cwx/flag_test_access.hpp"
#include "cwxeditor_src/core/cwx/perf.hpp"
#include "cwxeditor_src/core/cwx/enumutils.hpp"

#include <atomic>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <new>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace cwxeditor::cwx::flag {

namespace {

using XmlNode = ::cwxeditor::cwx::xml::XNode;
using VariableInitialization = ::cwxeditor::cwx::types::VariableInitialization;
using VariantType = ::cwxeditor::cwx::types::VariantType;
using TextHolder = ::cwxeditor::cwx::textholder::TextHolder;

// Dのデリゲートのコピーは一つのクロージャー状態を共有する。ネイティブの呼び出し可能
// オブジェクトも共有し、状態の取得や子への伝播で可変のクロージャー状態を複製しない。
struct SharedChangeHandler {
    std::shared_ptr<ChangeHandler> target;

    void operator()() const {
        // コールバックは実行中に、最後の登録済みラッパーを置換する場合がある。
        const auto retained = target;
        (*retained)();
    }
};

ChangeHandler share_change_handler(ChangeHandler change) {
    // 保存したハンドラーは何度でも復元できる。復元ごと
    // に別のラッパーを増やさず、既存の状態を保持する。
    if (!change || change.target<SharedChangeHandler>()) return change;
    return SharedChangeHandler{std::make_shared<ChangeHandler>(std::move(change))};
}

FlagDirPtr retained_parent(const std::weak_ptr<FlagDir>& parent) {
    const auto value = parent.lock();
    if (!value) return nullptr;
    if (const auto owner = value->weak_from_this().lock()) return owner;
    // スタック所有のディレクトリは借用した親を公開する。通常の所有デ
    // ィレクトリは、コピー構築時のリンクも含め、実際の所有元を返す。
    return value;
}

FlagDirPtr required_parent(FlagDirPtr parent) {
    // Dはこれらの取得関数で親を参照する。null参照や勝手
    // な空結果ではなく、ネイティブの不変条件違反として扱う。
    if (!parent) throw std::logic_error("Variable has no parent directory");
    return parent;
}

std::atomic<std::uint64_t> next_directory_id{1};

String new_directory_id() {
    return "FlagDir-" + std::to_string(next_directory_id.fetch_add(1));
}

std::unique_ptr<TextHolder> make_text_holder(CwxPath* owner,
                                             const ChangeHandler& change) {
    // DのTextHolderは、Flag/Stepの構築中も
    // それをCWXPathの所有元として保持する。解放を行わ
    // ない所有元ハンドルで、所有権の循環を作らず同じ関係を表
    // す。資源が保持オブジェクトを所有し、逆には所有しない。
    CwxPathPtr owner_handle(owner, [](CwxPath*) {});
    auto holder = std::make_unique<TextHolder>(std::move(owner_handle));
    holder->changeHandler(change);
    return holder;
}

template <typename T>
void add_attr(XmlNode& node, StringView name, const T& value) {
    const auto text = ::cwxeditor::lib::detail::xml_to_string(value);
    node.new_attr(name, StringView(text));
}

String bool_text(const bool value) {
    return value ? "True" : "False";
}

double parse_double(StringView text) {
    std::size_t used = 0;
    const auto value = std::stod(String(text), &used);
    if (used != text.size()) {
        throw FlagException("Invalid number: " + String(text));
    }
    return value;
}

String format_number(const double value) {
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::fixed << std::setprecision(Variant::DECIMAL_PLACES) << value;
    auto result = stream.str();
    while (result.size() > 1 && result.back() == '0') result.pop_back();
    if (!result.empty() && result.back() == '.') result.pop_back();
    return result.empty() ? "0" : result;
}

String variant_preview(const Variant& value) {
    switch (value.type()) {
    case VariantType::Number:
        return format_number(value.numVal());
    case VariantType::String: {
        String result = value.strVal();
        return result;
    }
    case VariantType::Boolean:
        return bool_text(value.boolVal());
    case VariantType::List:
    case VariantType::Structure:
        throw FlagException("Can not initialize variant with list or structure.");
    }
    throw FlagException("Invalid variant type.");
}

bool variant_values_equal(const VariantValues& first, const VariantValues& second) {
    if (first.size() != second.size()) return false;
    for (std::size_t i = 0; i < first.size(); ++i) {
        const auto& lhs = first[i];
        const auto& rhs = second[i];
        if (lhs.valid != rhs.valid || lhs.type != rhs.type ||
            lhs.numVal != rhs.numVal || lhs.strVal != rhs.strVal || lhs.boolVal != rhs.boolVal ||
            lhs.structName != rhs.structName || !variant_values_equal(lhs.listVal, rhs.listVal) ||
            !variant_values_equal(lhs.structVal, rhs.structVal)) return false;
    }
    return true;
}

template <typename T>
void set_holder_counter(T& holder, const UseCounterPtr& counter,
                        const CwxPathPtr& counter_owner) {
    if (counter) {
        holder.setUseCounter(counter, counter_owner);
    } else {
        holder.removeUseCounter();
    }
}

String last_child_text(const XmlNode& node, StringView tag, String value = {}) {
    // Dの変数読み込みは子を順に処理する。他のXML利用箇所、特にFlagDir
    // の外側のパス取得は、引き続き意図的に最初の要素を使う。
    for (const auto& child : node.element_children())
        if (child.name() == tag) value = child.value();
    return value;
}

String child_name(const XmlNode& node, StringView tag, StringView error) {
    String value;
    bool present = false;
    for (const auto& child : node.element_children()) {
        if (child.name() == tag) {
            value = child.value();
            present = true;
        }
    }
    // DはNameの欠落と、存在する空のNameを区別する。後者はファク
    // トリーが子を解析した後、validNameで「_」に正規化する。
    if (!present) throw FlagException(String(error));
    return FlagDir::basename(value);
}


template <typename T>
std::ptrdiff_t pointer_index(const std::vector<std::shared_ptr<T>>& values,
                             const std::shared_ptr<T>& target) {
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (values[i] == target) return static_cast<std::ptrdiff_t>(i);
    }
    return -1;
}

template <typename T>
std::ptrdiff_t name_index(const std::vector<std::shared_ptr<T>>& values, StringView value) {
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (values[i] && values[i]->name() == value) return static_cast<std::ptrdiff_t>(i);
    }
    return -1;
}

template <typename T, typename Yield>
void sort_and_yield(const std::vector<std::shared_ptr<T>>& values, bool logical, Yield&& yield) {
    sortedWithPath(values, logical, std::forward<Yield>(yield));
}

}  // namespace

FlagException::FlagException(String message) : std::runtime_error(std::move(message)) {}

Flag::Flag(const Flag& copyBase) {
    initTextHolders();
    copyFrom(copyBase);
}

Flag::Flag(String name, String on, String off, const bool onOff) : on_off_(onOff) {
    initTextHolders();
    on_->text(on);
    off_->text(off);
    name_ = FlagDir::validName(std::move(name));
}

Flag::~Flag() = default;

void Flag::initTextHolders() {
    on_ = make_text_holder(this, [this] { changed(); });
    off_ = make_text_holder(this, [this] { changed(); });
}

bool Flag::operator==(const Flag& other) const {
    return name_ == other.name_ && on() == other.on() && off() == other.off() &&
           on_off_ == other.on_off_ && expand_sp_chars_ == other.expand_sp_chars_ &&
           initialization_ == other.initialization_ && comment_ == other.comment_;
}

int Flag::opCmp(const Flag& other) const { return name_.compare(other.name_); }

void Flag::copyFrom(const Flag& value) {
    // DのcopyFromはmixin(S_TRACE)で開始する。
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d176(__FILE__, __LINE__);
    name(value.name_);
    on(value.on());
    off(value.off());
    onOff(value.on_off_);
    expandSPChars(value.expand_sp_chars_);
    initialization(value.initialization_);
    comment(value.comment_);
}

FlagDirPtr Flag::parent() const {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d188(__FILE__, __LINE__);
    return retained_parent(parent_);
}

void Flag::parent(FlagDirPtr value) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d193(__FILE__, __LINE__);
    if (value && value->getFlag(name_)) {
        throw FlagException("Duplicate flag name: " + name_);
    }
    parent_ = std::move(value);
}

FlagDirPtr Flag::root() const {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d200(__FILE__, __LINE__);
    const auto p = parent();
    return required_parent(p)->root();
}

void Flag::changeHandler(ChangeHandler change) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d205(__FILE__, __LINE__);
    change_ = share_change_handler(std::move(change)); }

void Flag::useCounter(UseCounterPtr counter) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d211(__FILE__, __LINE__);
    use_counter_ = std::move(counter);
    if (expand_sp_chars_ && use_counter_) {
        ::cwxeditor::cwx::perf::TraceScope trace_flag_d213(__FILE__, __LINE__);
        const CwxPathPtr owner(this, [](CwxPath*) {});
        set_holder_counter(*on_, use_counter_, owner);
        set_holder_counter(*off_, use_counter_, owner);
    } else {
        ::cwxeditor::cwx::perf::TraceScope trace_flag_d216(__FILE__, __LINE__);
        on_->removeUseCounter();
        off_->removeUseCounter();
    }
}

bool Flag::name(String value) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d234(__FILE__, __LINE__);
    value = FlagDir::validName(std::move(value));
    const auto p = parent();
    if (p && !p->canAppend<Flag>(value) && value != name_) return false;
    if (name_ == value) return true;
    if (change_) change_();
    if (p) {
        p->flag_names_.erase(name_);
    }
    name_ = std::move(value);
    if (p) {
        try { p->flag_names_[name_] = shared_from_this(); }
        catch (const std::bad_weak_ptr&) {}
    }
    return true;
}

String Flag::on() const {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d251(__FILE__, __LINE__); return on_->text(); }

void Flag::on(String value) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d256(__FILE__, __LINE__);
    if (on() == value) return;
    {
        ::cwxeditor::cwx::perf::TraceScope trace_flag_d257(__FILE__, __LINE__);
    changed();
    on_->text(value);
    }
}

String Flag::off() const {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d265(__FILE__, __LINE__); return off_->text(); }

void Flag::off(String value) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d270(__FILE__, __LINE__);
    if (off() == value) return;
    {
        ::cwxeditor::cwx::perf::TraceScope trace_flag_d271(__FILE__, __LINE__);
    changed();
    off_->text(value);
    }
}

void Flag::onOff(const bool value) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d284(__FILE__, __LINE__);
    if (change_ && on_off_ != value) change_();
    on_off_ = value;
}

void Flag::expandSPChars(const bool value) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d295(__FILE__, __LINE__);
    if (value == expand_sp_chars_) return;
    {
        ::cwxeditor::cwx::perf::TraceScope trace_flag_d296(__FILE__, __LINE__);
    changed();
    expand_sp_chars_ = value;
    if (expand_sp_chars_ && use_counter_) {
        ::cwxeditor::cwx::perf::TraceScope trace_flag_d299(__FILE__, __LINE__);
        const CwxPathPtr owner(this, [](CwxPath*) {});
        set_holder_counter(*on_, use_counter_, owner);
        set_holder_counter(*off_, use_counter_, owner);
    } else {
        ::cwxeditor::cwx::perf::TraceScope trace_flag_d302(__FILE__, __LINE__);
        on_->removeUseCounter();
        off_->removeUseCounter();
    }
    }
}

void Flag::initialization(const VariableInitialization value) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d317(__FILE__, __LINE__);
    if (change_ && initialization_ != value) change_();
    initialization_ = value;
}

std::vector<String> Flag::flagsInText(const bool value) const {
    return value ? on_->flagsInText() : off_->flagsInText();
}

std::vector<String> Flag::stepsInText(const bool value) const {
    return value ? on_->stepsInText() : off_->stepsInText();
}

std::vector<String> Flag::variantsInText(const bool value) const {
    return value ? on_->variantsInText() : off_->variantsInText();
}

void Flag::comment(String value) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d338(__FILE__, __LINE__);
    if (comment_ == value) return;
    changed();
    comment_ = std::move(value);
}

String Flag::path() const {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d351(__FILE__, __LINE__);
    const auto p = parent();
    return required_parent(p)->path() + name_;
}

void Flag::changed() {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d355(__FILE__, __LINE__);
    if (change_) change_();
}

String Flag::toXml() const {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d361(__FILE__, __LINE__);
    auto document = XmlNode::create(XML_ROOT_FLAG_DIRECTORY);
    toNode(document);
    return document.text();
}

FlagPtr Flag::createFromNode(XmlNode& node, const ::cwxeditor::cwx::system::XMLInfo&) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d367(__FILE__, __LINE__);
    const auto on_off = ::cwxeditor::cwx::utils::parseBool(node.attr("default", true));
    auto result = std::make_shared<Flag>(child_name(node, "Name", "Flag name not found."),
                                         last_child_text(node, "True", "TRUE"),
                                         last_child_text(node, "False", "FALSE"), on_off);
    result->expandSPChars(node.attr<bool>("spchars", false, false));
    result->initialization(::cwxeditor::cwx::types::toVariableInitialization(
        node.attr("initialize", false, "Leave")));
    result->comment(node.attr("comment", false, ""));
    return result;
}

void Flag::toNode(XmlNode& node) const {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d385(__FILE__, __LINE__);
    auto element = node.newElement("Flag");
    add_attr(element, "default", ::cwxeditor::cwx::utils::fromBool(on_off_));
    if (expand_sp_chars_) add_attr(element, "spchars", expand_sp_chars_);
    if (!::cwxeditor::cwx::types::isDefaultVariableInitialization(initialization_)) {
        add_attr(element, "initialize", ::cwxeditor::cwx::types::fromVariableInitialization(initialization_));
    }
    element.newElement("Name", path());
    element.newElement("True", on());
    element.newElement("False", off());
    if (!comment_.empty()) add_attr(element, "comment", comment_);
}

String Flag::cwxPath(const bool id) {
    // 親の仮想関数呼び出しをまたいで、Dの暗黙のクラス受信先を生存させる。
    [[maybe_unused]] const auto self_owner = weak_from_this().lock();
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d396(__FILE__, __LINE__);
    const auto p = parent();
    const auto index = static_cast<std::size_t>(required_parent(p)->indexOf(*this));
    return ::cwxeditor::cwx::path::cpjoin(p, "flag", index, id);
}

CwxPathPtr Flag::findCWXPath(StringView value) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d399(__FILE__, __LINE__);
    if (::cwxeditor::cwx::path::cpempty(value)) {
        try { return std::static_pointer_cast<CwxPath>(shared_from_this()); }
        catch (const std::bad_weak_ptr&) { return {}; }
    }
    return {};
}

CwxPathObserver Flag::cwxParent() {
    const auto p = parent();
    return p ? std::static_pointer_cast<CwxPath>(p) : CwxPathObserver{};
}

Step::Step(const Step& copyBase) { copyFrom(copyBase); }

Step::Step(String name, std::vector<String> values, const std::uint32_t select) {
    setValues(::cwxeditor::lib::SharedSlice<const String>(std::move(values)), static_cast<std::int32_t>(select));
    name_ = FlagDir::validName(std::move(name));
}

Step::~Step() = default;

bool Step::operator==(const Step& other) const {
    return name_ == other.name_ && select_ == other.select_ && values() == other.values() &&
           expand_sp_chars_ == other.expand_sp_chars_ && initialization_ == other.initialization_ &&
           comment_ == other.comment_;
}

int Step::opCmp(const Step& other) const { return name_.compare(other.name_); }

void Step::copyFrom(const Step& value) {
    [[maybe_unused]] const auto self_owner = weak_from_this().lock();
    [[maybe_unused]] const auto source_owner = value.weak_from_this().lock();
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d451(__FILE__, __LINE__);
    name(value.name_);
    auto texts = value.values();
    const auto selection = value.select_;
    setValues(::cwxeditor::lib::SharedSlice<const String>(std::move(texts)), static_cast<std::int32_t>(selection));
    expandSPChars(value.expand_sp_chars_);
    initialization(value.initialization_);
    comment(value.comment_);
}

FlagDirPtr Step::parent() const {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d461(__FILE__, __LINE__);
    return retained_parent(parent_);
}

void Step::parent(FlagDirPtr value) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d466(__FILE__, __LINE__);
    if (value && value->getStep(name_)) throw FlagException("Duplicate step name: " + name_);
    parent_ = std::move(value);
}

FlagDirPtr Step::root() const {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d473(__FILE__, __LINE__);
    const auto p = parent();
    return required_parent(p)->root();
}

void Step::changeHandler(ChangeHandler change) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d478(__FILE__, __LINE__);
    change_ = share_change_handler(std::move(change)); }

bool Step::name(String value) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d490(__FILE__, __LINE__);
    value = FlagDir::validName(std::move(value));
    const auto p = parent();
    if (p && !p->canAppend<Step>(value) && value != name_) return false;
    if (name_ == value) return true;
    if (change_) change_();
    if (p) p->step_names_.erase(name_);
    name_ = std::move(value);
    if (p) {
        try { p->step_names_[name_] = shared_from_this(); }
        catch (const std::bad_weak_ptr&) {}
    }
    return true;
}

void Step::setValue(const std::uint32_t index, String value) {
    [[maybe_unused]] const auto self_owner = weak_from_this().lock();
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d506(__FILE__, __LINE__);
    if (index >= values_.size()) throw std::out_of_range("Step value index");
    if (getValue(index) == value) return;
    {
        ::cwxeditor::cwx::perf::TraceScope trace_flag_d507(__FILE__, __LINE__);
    changed();
    const auto holder = values_.at(index);
    if (!holder) throw std::logic_error("Null step text holder");
    holder->text(value);
    }
}

String Step::getValue(const std::uint32_t index) const {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d514(__FILE__, __LINE__);
    if (index >= values_.size()) throw std::out_of_range("Step value index");
    return values_[index]->text();
}

void Step::select(const std::uint32_t value) {
    [[maybe_unused]] const auto self_owner = weak_from_this().lock();
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d533(__FILE__, __LINE__);
    if (values_.empty()) throw std::logic_error("Step has no values");
    const auto change = change_;
    if (change && select_ != value) change();
    select_ = std::min<std::uint32_t>(value, static_cast<std::uint32_t>(values_.size() - 1));
}

String Step::value() const {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d541(__FILE__, __LINE__);
    if (values_.empty()) throw std::logic_error("Step has no values");
    return values_.at(select_)->text();
}

std::vector<String> Step::values() const {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d547(__FILE__, __LINE__);
    std::vector<String> result;
    result.reserve(values_.size());
    for (const auto& value : values_) result.push_back(value->text());
    return result;
}

void Step::rebuildTextHolders(const std::size_t count) {
    const auto previous = values_.size();
    if (count < previous) {
        // Dは古い末尾部分を前からたどり、その後で記述子を切り詰める。
        const std::vector<std::shared_ptr<TextHolder>> removed(values_.begin() + count, values_.end());
        for (const auto& holder : removed) holder->removeUseCounter();
        if (count > values_.size()) throw std::out_of_range("Step value slice");
        values_.resize(count);
    } else if (previous < count) {
        // Dのforeach(oldLength .. newLength)と同様に、範囲の境界を取得して固定する。
        for (auto i = previous; i < count; ++i) {
            std::shared_ptr<TextHolder> holder = make_text_holder(this, [this] { changed(); });
            if (expand_sp_chars_ && use_counter_) {
                const CwxPathPtr owner(this, [](CwxPath*) {});
                set_holder_counter(*holder, use_counter_, owner);
            }
            values_.push_back(std::move(holder));
        }
    }
}

void Step::setValues(std::span<const String> values, const std::int32_t select_value) {
    // ネイティブspanは呼び出し側のvector領域を保持できないため、このアダプター
    // 入力をコピーする。下記のD互換SharedSlice入力は共有する項目を保持する。
    std::vector<String> snapshot;
    if (!values.empty()) snapshot.assign(values.begin(), values.end());
    setValues(::cwxeditor::lib::SharedSlice<const String>(std::move(snapshot)), select_value);
}

void Step::setValues(::cwxeditor::lib::SharedSlice<const String> values, const std::int32_t select_value) {
    [[maybe_unused]] const auto self_owner = weak_from_this().lock();
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d551(__FILE__, __LINE__);
    // Dのselect < vals.lengthのアサートは、空の入力や負値も拒否する。
    if (select_value < 0 || static_cast<std::size_t>(select_value) >= values.size()) {
        throw std::out_of_range("Invalid step default value");
    }
    if (select_ != static_cast<std::uint32_t>(select_value)) {
        ::cwxeditor::cwx::perf::TraceScope trace_flag_d553(__FILE__, __LINE__);
        changed();
        select_ = static_cast<std::uint32_t>(select_value);
    }
    const auto current = this->values();
    bool different = current.size() != values.size();
    for (std::size_t i = 0; !different && i < current.size(); ++i) different = current[i] != values[i];
    if (!different) return;
    {
        ::cwxeditor::cwx::perf::TraceScope trace_flag_d557(__FILE__, __LINE__);
        changed();
        rebuildTextHolders(values.size());
        // 非公開の保持項目は追加・末尾切り詰めだけを行い、置換しない。所有元を保
        // 持し、入れ子の呼び出しでもDのforeachの取得済み状態を維持する。
        const auto holders = values_;
        for (std::size_t i = 0; i < holders.size(); ++i) {
            const String text = values[i];
            if (!holders[i]) throw std::logic_error("Null step text holder");
            holders[i]->text(text);
        }
    }
}

void Step::expandSPChars(const bool value) {
    [[maybe_unused]] const auto self_owner = weak_from_this().lock();
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d582(__FILE__, __LINE__);
    if (value == expand_sp_chars_) return;
    {
        ::cwxeditor::cwx::perf::TraceScope trace_flag_d583(__FILE__, __LINE__);
    changed();
    expand_sp_chars_ = value;
    const CwxPathPtr owner(this, [](CwxPath*) {});
    const auto holders = values_;
    for (const auto& holder : holders) {
        ::cwxeditor::cwx::perf::TraceScope trace_flag_d586(__FILE__, __LINE__);
        if (expand_sp_chars_ && use_counter_) {
            ::cwxeditor::cwx::perf::TraceScope trace_flag_d587(__FILE__, __LINE__);set_holder_counter(*holder, use_counter_, owner); }
        else {
            ::cwxeditor::cwx::perf::TraceScope trace_flag_d589(__FILE__, __LINE__);holder->removeUseCounter(); }
    }
    }
}

void Step::initialization(const VariableInitialization value) {
    [[maybe_unused]] const auto self_owner = weak_from_this().lock();
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d604(__FILE__, __LINE__);
    const auto change = change_;
    if (change && initialization_ != value) change();
    initialization_ = value;
}

std::vector<String> Step::flagsInText(const std::uint32_t index) const {
    if (index >= values_.size()) throw std::out_of_range("Step value index");
    return values_[index]->flagsInText();
}

std::vector<String> Step::stepsInText(const std::uint32_t index) const {
    if (index >= values_.size()) throw std::out_of_range("Step value index");
    return values_[index]->stepsInText();
}

std::vector<String> Step::variantsInText(const std::uint32_t index) const {
    if (index >= values_.size()) throw std::out_of_range("Step value index");
    return values_[index]->variantsInText();
}

void Step::comment(String value) {
    [[maybe_unused]] const auto self_owner = weak_from_this().lock();
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d625(__FILE__, __LINE__);
    if (comment_ == value) return;
    changed();
    comment_ = std::move(value);
}

String Step::path() const {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d639(__FILE__, __LINE__);
    const auto p = parent();
    return required_parent(p)->path() + name_;
}

void Step::changed() {
    [[maybe_unused]] const auto self_owner = weak_from_this().lock();
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d643(__FILE__, __LINE__);
    // Dはデリゲート値を評価してから呼ぶ。コールバックがメ
    // ンバーを置換しても、実行中のラッパーを破棄しない。
    const auto change = change_;
    if (change) change();
}

void Step::useCounter(UseCounterPtr counter) {
    [[maybe_unused]] const auto self_owner = weak_from_this().lock();
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d649(__FILE__, __LINE__);
    use_counter_ = counter;
    const CwxPathPtr owner(this, [](CwxPath*) {});
    const auto holders = values_;
    for (const auto& holder : holders) {
        ::cwxeditor::cwx::perf::TraceScope trace_flag_d651(__FILE__, __LINE__);
        if (expand_sp_chars_ && counter) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d652(__FILE__, __LINE__);set_holder_counter(*holder, counter, owner); }
        else {
            ::cwxeditor::cwx::perf::TraceScope trace_flag_d654(__FILE__, __LINE__);holder->removeUseCounter(); }
    }
}

String Step::toXml() const {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d666(__FILE__, __LINE__);
    auto document = XmlNode::create(XML_ROOT_FLAG_DIRECTORY);
    toNode(document);
    return document.text();
}

StepPtr Step::createFromNode(XmlNode& node, const ::cwxeditor::cwx::system::XMLInfo&) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d672(__FILE__, __LINE__);
    const auto default_value = node.attr<int>("default", true);
    const auto name = child_name(node, "Name", "Step name not found.");
    std::vector<String> values;
    for (const auto& child : node.element_children()) {
        if (child.name().starts_with("Value")) values.push_back(child.value());
    }
    if (default_value < 0 || static_cast<std::size_t>(default_value) >= values.size()) {
        ::cwxeditor::cwx::perf::TraceScope trace_flag_d684(__FILE__, __LINE__);
        throw FlagException("Step default value invalid. Count: " + std::to_string(values.size()) +
                            ", default: " + std::to_string(default_value));
    }
    auto result = std::make_shared<Step>(name, values, static_cast<std::uint32_t>(default_value));
    result->expandSPChars(node.attr<bool>("spchars", false, false));
    result->initialization(::cwxeditor::cwx::types::toVariableInitialization(
        node.attr("initialize", false, "Leave")));
    result->comment(node.attr("comment", false, ""));
    return result;
}

void Step::toNode(XmlNode& node) const {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d695(__FILE__, __LINE__);
    auto element = node.newElement("Step");
    add_attr(element, "default", select_);
    if (expand_sp_chars_) add_attr(element, "spchars", expand_sp_chars_);
    if (!::cwxeditor::cwx::types::isDefaultVariableInitialization(initialization_)) {
        add_attr(element, "initialize", ::cwxeditor::cwx::types::fromVariableInitialization(initialization_));
    }
    element.newElement("Name", path());
    for (const auto& value : values_) {
        ::cwxeditor::cwx::perf::TraceScope trace_flag_d701(__FILE__, __LINE__);element.newElement("Value", value->text()); }
    if (!comment_.empty()) add_attr(element, "comment", comment_);
}

String Step::cwxPath(const bool id) {
    [[maybe_unused]] const auto self_owner = weak_from_this().lock();
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d707(__FILE__, __LINE__);
    const auto p = parent();
    const auto index = static_cast<std::size_t>(required_parent(p)->indexOf(*this));
    return ::cwxeditor::cwx::path::cpjoin(p, "step", index, id);
}

CwxPathPtr Step::findCWXPath(StringView value) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d710(__FILE__, __LINE__);
    if (::cwxeditor::cwx::path::cpempty(value)) {
        try { return std::static_pointer_cast<CwxPath>(shared_from_this()); }
        catch (const std::bad_weak_ptr&) { return {}; }
    }
    return {};
}

CwxPathObserver Step::cwxParent() {
    const auto p = parent();
    return p ? std::static_pointer_cast<CwxPath>(p) : CwxPathObserver{};
}

Variant::Variant(const Variant& copyBase) { copyFrom(copyBase); }

Variant::Variant(String name, const double value) : name_(FlagDir::validName(std::move(name))),
                                                    type_(VariantType::Number),
                                                    num_value_(value) {}

Variant::Variant(String name, String value) : name_(FlagDir::validName(std::move(name))),
                                              type_(VariantType::String),
                                              str_value_(std::move(value)) {}

Variant::Variant(String name, const bool value) : name_(FlagDir::validName(std::move(name))),
                                                 type_(VariantType::Boolean),
                                                 bool_value_(value) {}

Variant::Variant(String name, VariantValues value) : name_(FlagDir::validName(std::move(name))),
                                                    type_(VariantType::List),
                                                    list_value_(std::move(value)) {}

Variant::Variant(String name, String structName, VariantValues value)
    : name_(FlagDir::validName(std::move(name))), type_(VariantType::Structure),
      struct_name_(std::move(structName)), struct_value_(std::move(value)) {}

bool Variant::operator==(const Variant& other) const {
    if (this == &other) return true;
    if (name_ != other.name_ || type_ != other.type_ || initialization_ != other.initialization_ ||
        comment_ != other.comment_) return false;
    switch (type_) {
    case VariantType::Number: {
        if (num_value_ == other.num_value_) return true;
        if (!std::isfinite(num_value_) || !std::isfinite(other.num_value_)) return false;
        const auto difference = std::abs(num_value_ - other.num_value_);
        // PhobosのisClose(double, double)はCommonDefaultForを使
        // う。double.digが15なら、10^-((15 + 1) / 2 + 1)は1e-9になる。
        constexpr double relative_tolerance = 1e-9;
        return difference <= relative_tolerance * std::abs(num_value_) ||
               difference <= relative_tolerance * std::abs(other.num_value_);
    }
    case VariantType::String: return str_value_ == other.str_value_;
    case VariantType::Boolean: return bool_value_ == other.bool_value_;
    case VariantType::List: return variant_values_equal(list_value_, other.list_value_);
    case VariantType::Structure:
        return struct_name_ == other.struct_name_ && variant_values_equal(struct_value_, other.struct_value_);
    }
    return false;
}

int Variant::opCmp(const Variant& other) const { return name_.compare(other.name_); }

void Variant::copyFrom(const Variant& value) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d807(__FILE__, __LINE__);
    name(value.name_);
    if (type_ != value.type_ || num_value_ != value.num_value_ || str_value_ != value.str_value_ ||
        bool_value_ != value.bool_value_ || !variant_values_equal(list_value_, value.list_value_) ||
        struct_name_ != value.struct_name_ || !variant_values_equal(struct_value_, value.struct_value_)) {
        changed();
        type_ = value.type_;
        num_value_ = value.num_value_;
        str_value_ = value.str_value_;
        bool_value_ = value.bool_value_;
        list_value_ = value.list_value_;
        struct_name_ = value.struct_name_;
        struct_value_ = value.struct_value_;
    }
    initialization(value.initialization_);
    comment(value.comment_);
}

FlagDirPtr Variant::parent() const {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d825(__FILE__, __LINE__);
    return retained_parent(parent_);
}

void Variant::parent(FlagDirPtr value) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d830(__FILE__, __LINE__);
    if (value && value->getVariant(name_)) throw FlagException("Duplicate variant name: " + name_);
    parent_ = std::move(value);
}

FlagDirPtr Variant::root() const {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d837(__FILE__, __LINE__);
    const auto p = parent();
    return required_parent(p)->root();
}

void Variant::changeHandler(ChangeHandler change) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d842(__FILE__, __LINE__);
    change_ = share_change_handler(std::move(change)); }

bool Variant::name(String value) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d854(__FILE__, __LINE__);
    value = FlagDir::validName(std::move(value));
    const auto p = parent();
    if (p && !p->canAppend<Variant>(value) && value != name_) return false;
    if (name_ == value) return true;
    changed();
    if (p) p->variant_names_.erase(name_);
    name_ = std::move(value);
    if (p) {
        try { p->variant_names_[name_] = shared_from_this(); }
        catch (const std::bad_weak_ptr&) {}
    }
    return true;
}

void Variant::value(const double value) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d871(__FILE__, __LINE__);
    if (type_ == VariantType::Number && num_value_ == value) return;
    {
        ::cwxeditor::cwx::perf::TraceScope trace_flag_d872(__FILE__, __LINE__);
    changed();
    type_ = VariantType::Number;
    num_value_ = value;
    }
}

void Variant::value(String value) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d880(__FILE__, __LINE__);
    if (type_ == VariantType::String && str_value_ == value) return;
    {
        ::cwxeditor::cwx::perf::TraceScope trace_flag_d881(__FILE__, __LINE__);
    changed();
    type_ = VariantType::String;
    str_value_ = std::move(value);
    }
}

void Variant::value(const bool value) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d889(__FILE__, __LINE__);
    if (type_ == VariantType::Boolean && bool_value_ == value) return;
    {
        ::cwxeditor::cwx::perf::TraceScope trace_flag_d890(__FILE__, __LINE__);
    changed();
    type_ = VariantType::Boolean;
    bool_value_ = value;
    }
}

void Variant::value(VariantValues value) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d898(__FILE__, __LINE__);
    if (type_ == VariantType::List && variant_values_equal(list_value_, value)) return;
    {
        ::cwxeditor::cwx::perf::TraceScope trace_flag_d899(__FILE__, __LINE__);
        changed();
        type_ = VariantType::List;
        list_value_ = std::move(value);
    }
}

void Variant::setStructValue(String structName, VariantValues value) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d906(__FILE__, __LINE__);
    if (type_ == VariantType::Structure && struct_name_ == structName &&
        variant_values_equal(struct_value_, value)) return;
    {
        ::cwxeditor::cwx::perf::TraceScope trace_flag_d907(__FILE__, __LINE__);
        changed();
        type_ = VariantType::Structure;
        struct_name_ = std::move(structName);
        struct_value_ = std::move(value);
    }
}

void Variant::initialization(const VariableInitialization value) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d953(__FILE__, __LINE__);
    if (change_ && initialization_ != value) change_();
    initialization_ = value;
}

void Variant::comment(String value) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d964(__FILE__, __LINE__);
    if (comment_ == value) return;
    changed();
    comment_ = std::move(value);
}

String Variant::path() const {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d978(__FILE__, __LINE__);
    const auto p = parent();
    return required_parent(p)->path() + name_;
}

void Variant::changed() {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d982(__FILE__, __LINE__);
    if (change_) change_();
}

String Variant::toXml() const {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d998(__FILE__, __LINE__);
    auto document = XmlNode::create(XML_ROOT_FLAG_DIRECTORY);
    toNode(document);
    return document.text();
}

VariantVal Variant::toVariantVal(XmlNode& node) {
    const auto type = types::toVariantType(node.attr<String>("type", true));
    const auto value = node.attr<String>("value", true);
    switch (type) {
    case VariantType::Number: return VariantVal::numValue(parse_double(value));
    case VariantType::String: return VariantVal::strValue(value);
    case VariantType::Boolean: return VariantVal::boolValue(utils::parseBool(value));
    default: throw FlagException("Can not initialize variant with list or structure.");
    }
}
void Variant::fromVariantVal(XmlNode& node, const VariantVal& value) {
    switch (value.type) {
    case VariantType::Number: case VariantType::String:
        node.newAttr("value", expression::variantValueToPreviewText(value)); break;
    case VariantType::Boolean: node.newAttr("value", utils::fromBool(value.boolVal)); break;
    default: throw FlagException("Can not initialize variant with list or structure.");
    }
}

VariantPtr Variant::createFromNode(XmlNode& node, const ::cwxeditor::cwx::system::XMLInfo&) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d1004(__FILE__, __LINE__);
    const auto type = ::cwxeditor::cwx::types::toVariantType(node.attr("defaulttype", true));
    const auto default_value = node.attr("defaultvalue", true);
    const auto name = child_name(node, "Name", "Variant name not found.");
    VariantPtr result;
    switch (type) {
    case VariantType::Number: result = std::make_shared<Variant>(name, parse_double(default_value)); break;
    case VariantType::String: result = std::make_shared<Variant>(name, default_value); break;
    case VariantType::Boolean:
        result = std::make_shared<Variant>(name, ::cwxeditor::cwx::utils::parseBool(default_value));
        break;
    case VariantType::List:
    case VariantType::Structure:
        throw FlagException("Can not initialize variant with list or structure.");
    }
    result->initialization(::cwxeditor::cwx::types::toVariableInitialization(
        node.attr("initialize", false, "Leave")));
    result->comment(node.attr("comment", false, ""));
    return result;
}

void Variant::toNode(XmlNode& node) const {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d1052(__FILE__, __LINE__);
    auto element = node.newElement("Variant");
    add_attr(element, "defaulttype", ::cwxeditor::cwx::types::fromVariantType(type_));
    if (!::cwxeditor::cwx::types::isDefaultVariableInitialization(initialization_)) {
        add_attr(element, "initialize", ::cwxeditor::cwx::types::fromVariableInitialization(initialization_));
    }
    add_attr(element, "defaultvalue", variant_preview(*this));
    element.newElement("Name", path());
    if (!comment_.empty()) add_attr(element, "comment", comment_);
}

String Variant::cwxPath(const bool id) {
    [[maybe_unused]] const auto self_owner = weak_from_this().lock();
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d1090(__FILE__, __LINE__);
    const auto p = parent();
    const auto index = static_cast<std::size_t>(required_parent(p)->indexOf(*this));
    return ::cwxeditor::cwx::path::cpjoin(p, "variant", index, id);
}

CwxPathPtr Variant::findCWXPath(StringView value) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d1093(__FILE__, __LINE__);
    if (::cwxeditor::cwx::path::cpempty(value)) {
        try { return std::static_pointer_cast<CwxPath>(shared_from_this()); }
        catch (const std::bad_weak_ptr&) { return {}; }
    }
    return {};
}

CwxPathObserver Variant::cwxParent() {
    const auto p = parent();
    return p ? std::static_pointer_cast<CwxPath>(p) : CwxPathObserver{};
}

FlagDir::FlagDir(String name) : name_(validName(std::move(name))), id_(new_directory_id()) {}

FlagDir::FlagDir(CwxPathPtr owner, String localVariablePrefix)
    : owner_(std::move(owner)), local_variable_prefix_(std::move(localVariablePrefix)), id_(new_directory_id()) {}

FlagDir::FlagDir(CwxPathPtr owner, const FlagDir& copyBase)
    : FlagDir(std::move(owner), copyBase.local_variable_prefix_) {
    if (!copyBase.name_.empty()) name_ = copyBase.name_;
    for (const auto& directory : copyBase.subdirs_) add(std::make_shared<FlagDir>(CwxPathPtr{}, *directory));
    for (const auto& value : copyBase.flags_) add(std::make_shared<Flag>(*value));
    for (const auto& value : copyBase.steps_) add(std::make_shared<Step>(*value));
    for (const auto& value : copyBase.variants_) add(std::make_shared<Variant>(*value));
}

CwxPathPtr FlagDir::localOwner() const { return owner_.lock(); }

std::vector<String> FlagDir::splitPath(StringView path) {
    // Phobosの文字列区切りのsplitは空の要素を保持する。ネイティ
    // ブの各部分は、一時的なパスの領域を借用せず、自身で文字列を所有する。
    std::vector<String> result;
    if (path.empty()) return result;
    std::size_t begin = 0;
    while (begin <= path.size()) {
        const auto end = path.find(FlagDir::SEPARATOR, begin);
        const auto count = end == StringView::npos ? path.size() - begin : end - begin;
        result.emplace_back(path.substr(begin, count));
        if (end == StringView::npos) break;
        begin = end + FlagDir::SEPARATOR.size();
    }
    return result;
}

String FlagDir::join(const StringView parent, const StringView path) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d1128_p30396(__FILE__, __LINE__);
    if (parent.empty()) { 
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d1129_p30421(__FILE__, __LINE__);return String(path); }
    if (parent.back() == SEPARATOR.front()) { 
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d1132_p30486(__FILE__, __LINE__);return String(parent) + String(path); }
    return String(parent) + String(SEPARATOR) + String(path);
}

bool FlagDir::operator==(const FlagDir& other) const {
    if (local_variable_prefix_ != other.local_variable_prefix_ || name_ != other.name_ ||
        flag_names_.size() != other.flag_names_.size() || step_names_.size() != other.step_names_.size() ||
        variant_names_.size() != other.variant_names_.size() || dir_names_.size() != other.dir_names_.size()) {
        return false;
    }
    for (const auto& [key, value] : flag_names_) {
        const auto found = other.flag_names_.find(key);
        if (found == other.flag_names_.end() || !value || !found->second || *value != *found->second) return false;
    }
    for (const auto& [key, value] : step_names_) {
        const auto found = other.step_names_.find(key);
        if (found == other.step_names_.end() || !value || !found->second || *value != *found->second) return false;
    }
    for (const auto& [key, value] : variant_names_) {
        const auto found = other.variant_names_.find(key);
        if (found == other.variant_names_.end() || !value || !found->second || *value != *found->second) return false;
    }
    for (const auto& [key, value] : dir_names_) {
        const auto found = other.dir_names_.find(key);
        if (found == other.dir_names_.end() || !value || !found->second || *value != *found->second) return false;
    }
    return true;
}

FlagDirPtr FlagDir::parent() const {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d1243_p31960(__FILE__, __LINE__);
    if (!owner_.expired()) return nullptr;
    return retained_parent(parent_);
}

void FlagDir::parent(FlagDirPtr value) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d1249_p32084(__FILE__, __LINE__);
    if (value && value->getSubDir(name_)) throw FlagException("Duplicate directory name: " + name_);
    parent_ = value;
    useCounter(value ? value->useCounter() : nullptr);
}

void FlagDir::changeHandler(ChangeHandler change) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d1256_p32316(__FILE__, __LINE__);
    change = share_change_handler(std::move(change));
    for (const auto& value : flags_) { 
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d1257_p32354(__FILE__, __LINE__);value->changeHandler(change); }
    for (const auto& value : steps_) { 
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d1260_p32421(__FILE__, __LINE__);value->changeHandler(change); }
    for (const auto& value : variants_) { 
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d1263_p32491(__FILE__, __LINE__);value->changeHandler(change); }
    for (const auto& value : subdirs_) { 
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d1266_p32560(__FILE__, __LINE__);value->changeHandler(change); }
    change_ = std::move(change);
}

void FlagDir::useCounter(UseCounterPtr counter) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d1280_p32675(__FILE__, __LINE__);
    for (const auto& value : flags_) { 
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d1281_p32713(__FILE__, __LINE__);value->useCounter(counter); }
    for (const auto& value : steps_) { 
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d1284_p32778(__FILE__, __LINE__);value->useCounter(counter); }
    for (const auto& value : variants_) { 
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d1287_p32846(__FILE__, __LINE__);value->useCounter(counter); }
    for (const auto& value : subdirs_) { 
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d1290_p32913(__FILE__, __LINE__);value->useCounter(counter); }
    use_counter_ = std::move(counter);
}

FlagDirPtr FlagDir::root() const {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d1308_p33017(__FILE__, __LINE__);
    auto current = const_cast<FlagDir*>(this)->self();
    while (current && current->parent()) { 
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d1312_p33114(__FILE__, __LINE__);current = current->parent(); }
    return current;
}

bool FlagDir::name(String value) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d1333_p33200(__FILE__, __LINE__);
    if (!owner_.expired()) throw FlagException("The local-variable root cannot be renamed");
    value = validName(std::move(value));
    const auto p = parent();
    if (p && !p->canAppendSub(value) && value != name_) return false;
    if (name_ == value) return true;
    if (change_ && !empty()) change_();
    if (p) p->dir_names_.erase(name_);
    name_ = std::move(value);
    if (p) {
        try { p->dir_names_[name_] = shared_from_this(); }
        catch (const std::bad_weak_ptr&) {}
    }
    return true;
}

bool FlagDir::canAppendSub(const StringView value) const {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d1394_p33780(__FILE__, __LINE__);
    const auto normalized = validName(String(value));
    return !normalized.empty() && !dir_names_.contains(normalized);
}

bool FlagDir::canAppendSub2(const FlagDir& value) const {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d1404_p33963(__FILE__, __LINE__);
    if (this == &value) return false;
    const auto own_parent = value.parent();
    if (own_parent && own_parent.get() == this) { 
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d1405_p34094(__FILE__, __LINE__);return true; }
    const FlagDir* current = this;
    while (current) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d1409_p34163(__FILE__, __LINE__);
        if (current == &value) { 
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d1410_p34195(__FILE__, __LINE__);return false; }
        const auto p = current->parent();
        current = p.get();
    }
    return canAppendSub(value.name_);
}

FlagDirPtr FlagDir::self() {
    try { return shared_from_this(); }
    catch (const std::bad_weak_ptr&) { return construction_anchor_; }
}

bool FlagDir::add(const FlagPtr& value, const bool rename_value) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d1452_p34532(__FILE__, __LINE__);
    return insert(-1, value, rename_value);
}

bool FlagDir::insert(std::ptrdiff_t index, const FlagPtr& input, const bool rename_value) {
    // 呼び出し元がこの配列の要素を渡したり、切り離しのコールバックで所有者をリセ
    // ットしたりする場合がある。 入力が無効になる前に、対象メンバーを保持する。
    const auto value = input;
    if (!value) return false;
    const auto keepAlive = weak_from_this().lock();
    const auto old_parent = value->parent();
    bool duplicate = old_parent.get() != this && !canAppend<Flag>(value->name());
    if (duplicate && rename_value) {
        // 名前を変える前に旧名で切り離す。Dは逆順に行うため、以前
        // の親の名前マップに移動済みの子への参照が残る場合がある。
        if (old_parent) old_parent->remove(value);
        value->name_ = createNewFlagName(value->name());
        duplicate = false;
    }
    if (duplicate) return false;
    if (old_parent.get() == this) {
        const auto current = pointer_index(flags_, value);
        if (current >= 0) {
            if (index >= 0 && index < static_cast<std::ptrdiff_t>(flags_.size()) &&
                current == index) return true;
            if (index < 0) index = static_cast<std::ptrdiff_t>(flags_.size());
            if (current < index) --index;
        }
    }
    // D版は切り離しと再接続で並べ替え、それぞれの状態を監視側へ通知する。
    if (old_parent) old_parent->remove(value);
    value->parent(self());
    const auto at = index < 0 ? flags_.size() : static_cast<std::size_t>(std::clamp<std::ptrdiff_t>(index, 0, static_cast<std::ptrdiff_t>(flags_.size())));
    flags_.insert(flags_.begin() + static_cast<std::ptrdiff_t>(at), value);
    value->changeHandler(change_);
    value->useCounter(use_counter_);
    flag_names_[value->name()] = value;
    if (change_) changed();
    return true;
}

bool FlagDir::add(const StepPtr& value, const bool rename_value) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d1460_p36282(__FILE__, __LINE__); return insert(-1, value, rename_value); }

bool FlagDir::insert(std::ptrdiff_t index, const StepPtr& input, const bool rename_value) {
    // 呼び出し元がこの配列の要素を渡したり、切り離しのコールバックで所有者をリセ
    // ットしたりする場合がある。 入力が無効になる前に、対象メンバーを保持する。
    const auto value = input;
    if (!value) return false;
    const auto keepAlive = weak_from_this().lock();
    const auto old_parent = value->parent();
    bool duplicate = old_parent.get() != this && !canAppend<Step>(value->name());
    if (duplicate && rename_value) {
        // 以前の親が旧マップキーを削除できるよう、名前変更前に切
        // り離す。Dは先に名前を変えるため、古いキーが残り得る。
        if (old_parent) old_parent->remove(value);
        value->name_ = createNewStepName(value->name());
        duplicate = false;
    }
    if (duplicate) return false;
    if (old_parent.get() == this) {
        const auto current = pointer_index(steps_, value);
        if (current >= 0) {
            if (index >= 0 && index < static_cast<std::ptrdiff_t>(steps_.size()) &&
                current == index) return true;
            if (index < 0) index = static_cast<std::ptrdiff_t>(steps_.size());
            if (current < index) --index;
        }
    }
    // D版は切り離しと再接続で並べ替え、それぞれの状態を監視側へ通知する。
    if (old_parent) old_parent->remove(value);
    value->parent(self());
    const auto at = index < 0 ? steps_.size() : static_cast<std::size_t>(std::clamp<std::ptrdiff_t>(index, 0, static_cast<std::ptrdiff_t>(steps_.size())));
    steps_.insert(steps_.begin() + static_cast<std::ptrdiff_t>(at), value);
    value->changeHandler(change_);
    value->useCounter(use_counter_);
    step_names_[value->name()] = value;
    if (change_) changed();
    return true;
}

bool FlagDir::add(const VariantPtr& value, const bool rename_value) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d1468_p38031(__FILE__, __LINE__); return insert(-1, value, rename_value); }

bool FlagDir::insert(std::ptrdiff_t index, const VariantPtr& input, const bool rename_value) {
    // 呼び出し元がこの配列の要素を渡したり、切り離しのコールバックで所有者をリセ
    // ットしたりする場合がある。 入力が無効になる前に、対象メンバーを保持する。
    const auto value = input;
    if (!value) return false;
    const auto keepAlive = weak_from_this().lock();
    const auto old_parent = value->parent();
    bool duplicate = old_parent.get() != this && !canAppend<Variant>(value->name());
    if (duplicate && rename_value) {
        // 以前の親が旧マップキーを削除できるよう、名前変更前に切
        // り離す。Dは先に名前を変えるため、古いキーが残り得る。
        if (old_parent) old_parent->remove(value);
        value->name_ = createNewVariantName(value->name());
        duplicate = false;
    }
    if (duplicate) return false;
    if (old_parent.get() == this) {
        const auto current = pointer_index(variants_, value);
        if (current >= 0) {
            if (index >= 0 && index < static_cast<std::ptrdiff_t>(variants_.size()) &&
                current == index) return true;
            if (index < 0) index = static_cast<std::ptrdiff_t>(variants_.size());
            if (current < index) --index;
        }
    }
    // D版は切り離しと再接続で並べ替え、それぞれの状態を監視側へ通知する。
    if (old_parent) old_parent->remove(value);
    value->parent(self());
    const auto at = index < 0 ? variants_.size() : static_cast<std::size_t>(std::clamp<std::ptrdiff_t>(index, 0, static_cast<std::ptrdiff_t>(variants_.size())));
    variants_.insert(variants_.begin() + static_cast<std::ptrdiff_t>(at), value);
    value->changeHandler(change_);
    value->useCounter(use_counter_);
    variant_names_[value->name()] = value;
    if (change_) changed();
    return true;
}

bool FlagDir::add(const FlagDirPtr& value, const bool rename_value) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d1476_p39813(__FILE__, __LINE__); return insert(-1, value, rename_value); }

bool FlagDir::insert(std::ptrdiff_t index, const FlagDirPtr& input, const bool rename_value) {
    // 呼び出し元がこの配列の要素を渡したり、切り離しのコールバックで所有者をリセ
    // ットしたりする場合がある。 入力が無効になる前に、対象メンバーを保持する。
    const auto value = input;
    if (!value) return false;
    const auto keepAlive = weak_from_this().lock();
    const auto old_parent = value->parent();
    bool duplicate = old_parent.get() != this && !canAppendSub2(*value);
    if (duplicate && rename_value) {
        // 以前の親が旧マップキーを削除できるよう、名前変更前に切
        // り離す。Dは先に名前を変えるため、古いキーが残り得る。
        if (old_parent) old_parent->remove(value);
        value->name_ = createNewDirName(value->name());
        duplicate = false;
    }
    if (duplicate) return false;
    if (old_parent.get() == this) {
        const auto current = pointer_index(subdirs_, value);
        if (current >= 0) {
            if (index >= 0 && index < static_cast<std::ptrdiff_t>(subdirs_.size()) &&
                current == index) return true;
            if (index < 0) index = static_cast<std::ptrdiff_t>(subdirs_.size());
            if (current < index) --index;
        }
    }
    // D版は切り離しと再接続で並べ替え、それぞれの状態を監視側へ通知する。
    if (old_parent) old_parent->remove(value);
    value->parent(self());
    const auto at = index < 0 ? subdirs_.size() : static_cast<std::size_t>(std::clamp<std::ptrdiff_t>(index, 0, static_cast<std::ptrdiff_t>(subdirs_.size())));
    subdirs_.insert(subdirs_.begin() + static_cast<std::ptrdiff_t>(at), value);
    value->changeHandler(change_);
    value->useCounter(use_counter_);
    dir_names_[value->name()] = value;
    if (change_ && !empty()) changed();
    return true;
}

void FlagDir::remove(const FlagPtr& value) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d1502_p41551(__FILE__, __LINE__);
    if (!value) return;
    const auto index = name_index(flags_, value->name());
    if (index < 0) return;
    auto removed = flags_[static_cast<std::size_t>(index)];
    removed->parent({});
    removed->changeHandler({});
    removed->useCounter({});
    flags_.erase(flags_.begin() + index);
    flag_names_.erase(value->name());
    if (change_) changed();
}

void FlagDir::remove(const StepPtr& value) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d1506_p41962(__FILE__, __LINE__);
    if (!value) return;
    const auto index = name_index(steps_, value->name());
    if (index < 0) return;
    auto removed = steps_[static_cast<std::size_t>(index)];
    removed->parent({});
    removed->changeHandler({});
    removed->useCounter({});
    steps_.erase(steps_.begin() + index);
    step_names_.erase(value->name());
    if (change_) changed();
}

void FlagDir::remove(const VariantPtr& value) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d1510_p42376(__FILE__, __LINE__);
    if (!value) return;
    const auto index = name_index(variants_, value->name());
    if (index < 0) return;
    auto removed = variants_[static_cast<std::size_t>(index)];
    removed->parent({});
    removed->changeHandler({});
    removed->useCounter({});
    variants_.erase(variants_.begin() + index);
    variant_names_.erase(value->name());
    if (change_) changed();
}

void FlagDir::remove(const FlagDirPtr& value) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d1514_p42805(__FILE__, __LINE__);
    if (!value) return;
    const auto index = name_index(subdirs_, value->name());
    if (index < 0) return;
    auto removed = subdirs_[static_cast<std::size_t>(index)];
    const bool had_values = !removed->empty();
    removed->parent({});
    removed->changeHandler({});
    removed->useCounter({});
    subdirs_.erase(subdirs_.begin() + index);
    dir_names_.erase(value->name());
    if (change_ && had_values) changed();
}

void FlagDir::removeAll() {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d1517_p43267(__FILE__, __LINE__);
    // DのGCはコールバックをまたいで受信先を生存させる。後続のカテゴリでも必
    // 要な間はネイティブの所有元を保持する。スタック所有のルートも有効に保つ。
    const auto keepAlive = weak_from_this().lock();
    const auto remove_category = [this](auto& values, auto& names) {
        const bool was_empty = empty();
        for (const auto& value : values) {
            value->parent({}); value->changeHandler({}); value->useCounter({});
        }
        values.clear();
        names.clear();
        // 今回の呼び出しだけの状態を取得する。コールバックがハンドラ
        // ーを置換すると、Dは次のカテゴリの境界で新しいものを使う。
        if (change_ && !was_empty) {
            const auto change = change_;
            change();
        }
    };
    remove_category(flags_, flag_names_);
    remove_category(steps_, step_names_);
    remove_category(variants_, variant_names_);
    remove_category(subdirs_, dir_names_);
}

std::ptrdiff_t FlagDir::indexOf(const StringView value) const {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d1593_p44016(__FILE__, __LINE__); return name_index(subdirs_, value); }
std::ptrdiff_t FlagDir::indexOf(const Flag& value) const {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d1598_p44113(__FILE__, __LINE__);
    for (std::size_t i = 0; i < flags_.size(); ++i) if (flags_[i].get() == &value) return static_cast<std::ptrdiff_t>(i);
    return -1;
}
std::ptrdiff_t FlagDir::indexOf(const Step& value) const {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d1603_p44311(__FILE__, __LINE__);
    for (std::size_t i = 0; i < steps_.size(); ++i) if (steps_[i].get() == &value) return static_cast<std::ptrdiff_t>(i);
    return -1;
}
std::ptrdiff_t FlagDir::indexOf(const Variant& value) const {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d1608_p44512(__FILE__, __LINE__);
    for (std::size_t i = 0; i < variants_.size(); ++i) if (variants_[i].get() == &value) return static_cast<std::ptrdiff_t>(i);
    return -1;
}
std::ptrdiff_t FlagDir::indexOf(const FlagDir& value) const {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d1613_p44719(__FILE__, __LINE__);
    for (std::size_t i = 0; i < subdirs_.size(); ++i) if (subdirs_[i].get() == &value) return static_cast<std::ptrdiff_t>(i);
    return -1;
}

FlagPtr FlagDir::getFlag(const StringView value) const {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d1620_p44920(__FILE__, __LINE__);
    const auto found = flag_names_.find(String(value));
    return found == flag_names_.end() ? nullptr : found->second;
}
StepPtr FlagDir::getStep(const StringView value) const {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d1626_p45100(__FILE__, __LINE__);
    const auto found = step_names_.find(String(value));
    return found == step_names_.end() ? nullptr : found->second;
}
VariantPtr FlagDir::getVariant(const StringView value) const {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d1632_p45286(__FILE__, __LINE__);
    const auto found = variant_names_.find(String(value));
    return found == variant_names_.end() ? nullptr : found->second;
}
FlagDirPtr FlagDir::getSubDir(const StringView value) const {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d1638_p45477(__FILE__, __LINE__);
    const auto found = dir_names_.find(String(value));
    return found == dir_names_.end() ? nullptr : found->second;
}

FlagList FlagDir::allFlags() const {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d1647_p45636(__FILE__, __LINE__);
    FlagList result = flags_;
    for (const auto& value : subdirs_) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d1652_p45707(__FILE__, __LINE__);
        const auto nested = value->allFlags();
        result.insert(result.end(), nested.begin(), nested.end());
    }
    return result;
}

StepList FlagDir::allSteps() const {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d1660_p45886(__FILE__, __LINE__);
    StepList result = steps_;
    for (const auto& value : subdirs_) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d1665_p45957(__FILE__, __LINE__);
        const auto nested = value->allSteps();
        result.insert(result.end(), nested.begin(), nested.end());
    }
    return result;
}

VariantList FlagDir::allVariants() const {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d1673_p46142(__FILE__, __LINE__);
    VariantList result = variants_;
    for (const auto& value : subdirs_) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d1678_p46219(__FILE__, __LINE__);
        const auto nested = value->allVariants();
        result.insert(result.end(), nested.begin(), nested.end());
    }
    return result;
}

FlagDirList FlagDir::allSubDirs() const {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d1686_p46406(__FILE__, __LINE__);
    FlagDirList result;
    for (const auto& value : subdirs_) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d1688_p46471(__FILE__, __LINE__);
        result.push_back(value);
        const auto nested = value->allSubDirs();
        result.insert(result.end(), nested.begin(), nested.end());
    }
    return result;
}

bool FlagDir::hasFlag() const {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d1698_p46680(__FILE__, __LINE__);
    if (!flags_.empty()) return true;
    for (const auto& value : subdirs_) { 
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d1700_p46758(__FILE__, __LINE__);if (value->hasFlag()) return true; }
    return false;
}
bool FlagDir::hasStep() const {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d1708_p46844(__FILE__, __LINE__);
    if (!steps_.empty()) return true;
    for (const auto& value : subdirs_) { 
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d1710_p46922(__FILE__, __LINE__);if (value->hasStep()) return true; }
    return false;
}
bool FlagDir::hasVariant() const {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d1718_p47011(__FILE__, __LINE__);
    if (!variants_.empty()) return true;
    for (const auto& value : subdirs_) { 
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d1720_p47092(__FILE__, __LINE__);if (value->hasVariant()) return true; }
    return false;
}
bool FlagDir::empty() const { return !hasFlag() && !hasStep() && !hasVariant(); }

String FlagDir::path() const {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d1729_p47263(__FILE__, __LINE__);
    if (const auto p = parent()) { 
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d1730_p47297(__FILE__, __LINE__);return p->path() + name_ + String(SEPARATOR); }
    return local_variable_prefix_.empty() ? String{} : String(SEPARATOR) + local_variable_prefix_ + String(SEPARATOR);
}

void FlagDir::changed() {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d1741_p47490(__FILE__, __LINE__);
    if (change_) change_();
}

void FlagDir::toNode(XmlNode& node, const bool createEmptyElement) const {
    // Dは直接の各項目のループ内で子ディレクトリを再帰的に出力する。
    // ここでは一度だけ平坦化し、空のルートでも子孫を残し、兄弟項目
    // で同じ子孫を繰り返さないようにする。元実装への意図的な修正。
    if (createEmptyElement || hasFlag()) {
        auto element = node.newElement("Flags");
        for (const auto& value : allFlags()) value->toNode(element);
    }
    if (createEmptyElement || hasStep()) {
        auto element = node.newElement("Steps");
        for (const auto& value : allSteps()) value->toNode(element);
    }
    if (createEmptyElement || hasVariant()) {
        auto element = node.newElement("Variants");
        for (const auto& value : allVariants()) value->toNode(element);
    }
}

String FlagDir::basename(String path) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d1791_p48379(__FILE__, __LINE__);
    if (path.size() < SEPARATOR.size()) { 
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d1793_p48420(__FILE__, __LINE__);return path; }
    if (path.ends_with(SEPARATOR)) { 
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d1796_p48468(__FILE__, __LINE__);path.erase(path.size() - SEPARATOR.size()); }
    const auto index = path.rfind(SEPARATOR);
    return index == String::npos ? path : path.substr(index + SEPARATOR.size());
}

bool FlagDir::has(String value) const {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d1817_p48681(__FILE__, __LINE__);
    value = localPathToPath(std::move(value));
    const auto current = path();
    return value.size() <= current.size() && current.compare(0, value.size(), value) == 0;
}

String FlagDir::up(String path) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d1882_p48889(__FILE__, __LINE__);
    if (path.empty()) { 
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d1884_p48912(__FILE__, __LINE__);return {}; }
    if (path.ends_with(SEPARATOR)) { 
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d1887_p48958(__FILE__, __LINE__);path.erase(path.size() - SEPARATOR.size()); }
    const auto index = path.rfind(SEPARATOR);
    return index == String::npos ? String{} : path.substr(0, index + SEPARATOR.size());
}

String FlagDir::validName(String name) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d2207_p49179(__FILE__, __LINE__);
    if (name.empty()) { 
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d2208_p49202(__FILE__, __LINE__);name = "_"; }
    String result;
    result.reserve(name.size());
    for (const char value : name) if (value != SEPARATOR.front()) result.push_back(value);
    return result;
}

String FlagDir::createNewFlagName(String base, String oldName) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d2219_p49443(__FILE__, __LINE__);
    base = validName(std::move(base));
    return ::cwxeditor::cwx::utils::createNewName(base, [this, &oldName](StringView value) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d2220_p49575(__FILE__, __LINE__);
        return (!oldName.empty() && oldName == value) || canAppend<Flag>(value);
    });
}

String FlagDir::createNewStepName(String base, String oldName) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d2226_p49732(__FILE__, __LINE__);
    base = validName(std::move(base));
    return ::cwxeditor::cwx::utils::createNewName(base, [this, &oldName](StringView value) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d2227_p49864(__FILE__, __LINE__);
        return (!oldName.empty() && oldName == value) || canAppend<Step>(value);
    });
}

String FlagDir::createNewVariantName(String base, String oldName) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d2233_p50024(__FILE__, __LINE__);
    base = validName(std::move(base));
    return ::cwxeditor::cwx::utils::createNewName(base, [this, &oldName](StringView value) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d2234_p50156(__FILE__, __LINE__);
        return (!oldName.empty() && oldName == value) || canAppend<Variant>(value);
    });
}

String FlagDir::createNewDirName(String base, String oldName) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d2240_p50315(__FILE__, __LINE__);
    base = validName(std::move(base));
    return ::cwxeditor::cwx::utils::createNewName(base, [this, &oldName](StringView value) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d2241_p50447(__FILE__, __LINE__);
        return (!oldName.empty() && oldName == value) || canAppendSub(value);
    });
}

template <typename T, typename Predicate>
std::vector<String> create_names_impl(String base, std::size_t count,
                                      std::span<const String> old_names, Predicate predicate) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d2263_p50744(__FILE__, __LINE__);
    std::unordered_set<String> old_set;
    for (const auto& name : old_names) {
        old_set.insert(::cwxeditor::cwx::enumutils::lowerCase(name));
    }
    std::unordered_set<String> used;
    std::vector<String> result;
    for (std::size_t i = 0; i < count; ++i) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d2268_p51086(__FILE__, __LINE__);
        auto value = ::cwxeditor::cwx::utils::createNewName(FlagDir::validName(base), [&](StringView candidate) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d2269_p51180(__FILE__, __LINE__);
            return (predicate(candidate) || old_set.contains(::cwxeditor::cwx::enumutils::lowerCase(candidate))) &&
                   !used.contains(::cwxeditor::cwx::enumutils::lowerCase(candidate));
        });
        used.insert(::cwxeditor::cwx::enumutils::lowerCase(value));
        result.push_back(std::move(value));
    }
    return result;
}

std::vector<String> FlagDir::createNewFlagNames(String base, const std::size_t count, std::span<const String> oldNames) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d2281_p51624(__FILE__, __LINE__);
    return create_names_impl<Flag>(std::move(base), count, oldNames,
                                    [this](StringView value) { return canAppend<Flag>(value); });
}
std::vector<String> FlagDir::createNewStepNames(String base, const std::size_t count, std::span<const String> oldNames) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d2288_p51915(__FILE__, __LINE__);
    return create_names_impl<Step>(std::move(base), count, oldNames,
                                   [this](StringView value) { return canAppend<Step>(value); });
}
std::vector<String> FlagDir::createNewVariantNames(String base, const std::size_t count, std::span<const String> oldNames) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d2295_p52208(__FILE__, __LINE__);
    return create_names_impl<Variant>(std::move(base), count, oldNames,
                                      [this](StringView value) { return canAppend<Variant>(value); });
}
std::vector<String> FlagDir::createNewDirNames(String base, const std::size_t count, std::span<const String> oldNames) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d2302_p52506(__FILE__, __LINE__);
    return create_names_impl<FlagDir>(std::move(base), count, oldNames,
                                      [this](StringView value) { return canAppendSub(value); });
}

String FlagDir::removePrefix(String value) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d2517_p52723(__FILE__, __LINE__);
    if (!value.starts_with(SEPARATOR)) return value;
    value.erase(0, SEPARATOR.size());
    const auto index = value.find(SEPARATOR);
    return index == String::npos ? value : value.substr(index + SEPARATOR.size());
}

String FlagDir::localPathToPath(String value) const {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d2471_p53000(__FILE__, __LINE__);
    const auto stripped = removePrefix(value);
    if (stripped == value) return value;
    if (value.find(SEPARATOR, SEPARATOR.size()) == String::npos) return {};
    if (value.size() <= stripped.size() + 2 * SEPARATOR.size()) return {};
    const auto prefix_begin = SEPARATOR.size();
    const auto prefix_length = value.size() - stripped.size() - 2 * SEPARATOR.size();
    const auto prefix = value.substr(prefix_begin, prefix_length);
    return prefix == root()->local_variable_prefix_ ? stripped : String{};
}

FlagDirPtr FlagDir::findPath(String value, const bool create) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d2311_p53582(__FILE__, __LINE__);
    value = localPathToPath(std::move(value));
    if (value.empty()) { 
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d2313_p53653(__FILE__, __LINE__);return root(); }
    auto parts = FlagDir::splitPath(value);
    if (!parts.empty()) parts.pop_back();
    // 単一の葉の親はルートとする。この場合、Dの再帰的なfindPathSub
    // は空の構成要素配列を参照するため、ネイティブ側で補正する。
    auto current = root();
    for (const auto& part : parts) {
        auto next = current->getSubDir(part);
        if (!next && create && current->canAppendSub(part)) {
            next = std::make_shared<FlagDir>(part);
            current->add(next);
        }
        if (!next) return nullptr;
        current = next;
    }
    return current;
}

FlagDirPtr FlagDir::findPath(String value) const {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d2325_p54247(__FILE__, __LINE__);
    return const_cast<FlagDir*>(this)->findPath(std::move(value), false);
}

FlagPtr FlagDir::findFlag(String value) {
    value = localPathToPath(std::move(value));
    if (value.empty()) return nullptr;
    const auto directory = findPath(up(value), false);
    return directory ? directory->getFlag(basename(value)) : nullptr;
}
FlagPtr FlagDir::findFlag(String value) const { return const_cast<FlagDir*>(this)->findFlag(std::move(value)); }
StepPtr FlagDir::findStep(String value) {
    value = localPathToPath(std::move(value));
    if (value.empty()) return nullptr;
    const auto directory = findPath(up(value), false);
    return directory ? directory->getStep(basename(value)) : nullptr;
}
StepPtr FlagDir::findStep(String value) const { return const_cast<FlagDir*>(this)->findStep(std::move(value)); }
VariantPtr FlagDir::findVariant(String value) {
    value = localPathToPath(std::move(value));
    if (value.empty()) return nullptr;
    const auto directory = findPath(up(value), false);
    return directory ? directory->getVariant(basename(value)) : nullptr;
}
VariantPtr FlagDir::findVariant(String value) const { return const_cast<FlagDir*>(this)->findVariant(std::move(value)); }

void FlagDir::toNodeAll(XmlNode& node, const bool logicalSort, const bool createEmptyElement) const {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d2482_p55549(__FILE__, __LINE__);
    if (createEmptyElement || hasFlag()) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d2483_p55592(__FILE__, __LINE__);
        auto element = node.newElement("Flags");
        sort_and_yield(allFlags(), logicalSort, [&element](const FlagPtr& value) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d2486_p55724(__FILE__, __LINE__); value->toNode(element); });
    }
    if (createEmptyElement || hasStep()) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d2490_p55801(__FILE__, __LINE__);
        auto element = node.newElement("Steps");
        sort_and_yield(allSteps(), logicalSort, [&element](const StepPtr& value) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d2492_p55933(__FILE__, __LINE__); value->toNode(element); });
    }
    if (createEmptyElement || hasVariant()) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d2496_p56013(__FILE__, __LINE__);
        auto element = node.newElement("Variants");
        sort_and_yield(allVariants(), logicalSort, [&element](const VariantPtr& value) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d2498_p56154(__FILE__, __LINE__); value->toNode(element); });
    }
}

void FlagDir::fromXmlNode(XmlNode& node, const ::cwxeditor::cwx::system::XMLInfo& version,
                          const bool startParse) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d2505_p56333(__FILE__, __LINE__);
    const auto origin = self();
    // Dは設定したハンドラー内でクラス引数を保持する。この三つのファクト
    // リーはXMLInfoを使わないため、ネイティブの値コピーを保持し
    // ても同等であり、呼び出し側のスタックオブジェクトの借用を避けられ
    // る。XMLInfo.sysは借用のままだが、いずれも参照しない。
    const auto context = std::make_shared<const ::cwxeditor::cwx::system::XMLInfo>(version);
    const auto parse_variables = [origin, context](XmlNode& container, String item, auto factory) {
        ::cwxeditor::cwx::perf::TraceScope trace_flag_d2528(__FILE__, __LINE__);
        container.onTag[item] = [origin, context, item, factory](XmlNode& element) {
            ::cwxeditor::cwx::perf::TraceScope trace_flag_d2529(__FILE__, __LINE__);
            const auto path = removePrefix(element.child_text("Name", false));
            bool name_present = false;
            for (const auto& child : element.element_children()) {
                if (child.name() == "Name") {
                    name_present = true;
                    break;
                }
            }
            if (!name_present) {
                ::cwxeditor::cwx::perf::TraceScope trace_flag_d2539(__FILE__, __LINE__);
                throw FlagException(item + " name not found.");
            }
            const auto parent_path = up(path);
            // Dでは空でないパスから得る親スライスは、up("A")が空文字列でも非
            // nullになる。findPath("", true)は絶対ルートを選
            // ぶため、空文字列で現在のディレクトリを選ぶと規則が変わる。存在する空
            // 文字列のupはDではnullとなり、受信先のディレクトリを維持する。
            const auto directory = path.empty() ? origin : origin->findPath(parent_path, true);
            const auto value = factory(element, *context);
            using Element = typename decltype(value)::element_type;
            if (!value || !required_parent(directory)->canAppend<Element>(value->name())) {
                throw FlagException(item + " parse error: " + path);
            }
            static_cast<void>(directory->add(value));
        };
        // Dは要素ごとのハンドラーを設定してparseを呼ぶ。他の
        // 登録済みハンドラーと、この区画の後続の再解析を維持する。
        container.parse();
    };
    node.onTag["Flags"] = [parse_variables](XmlNode& container) {
        ::cwxeditor::cwx::perf::TraceScope trace_flag_d2506(__FILE__, __LINE__);
        parse_variables(container, "Flag", [](XmlNode& element, const auto& context) {
            return Flag::createFromNode(element, context);
        });
    };
    node.onTag["Steps"] = [parse_variables](XmlNode& container) {
        ::cwxeditor::cwx::perf::TraceScope trace_flag_d2509(__FILE__, __LINE__);
        parse_variables(container, "Step", [](XmlNode& element, const auto& context) {
            return Step::createFromNode(element, context);
        });
    };
    node.onTag["Variants"] = [parse_variables](XmlNode& container) {
        ::cwxeditor::cwx::perf::TraceScope trace_flag_d2512(__FILE__, __LINE__);
        parse_variables(container, "Variant", [](XmlNode& element, const auto& context) {
            return Variant::createFromNode(element, context);
        });
    };
    if (startParse) node.parse();
}

bool FlagDir::readAtt(const XmlNode& node, String& rootId, String& path_value, bool& sameTree) const {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d2035_p58140(__FILE__, __LINE__);
    rootId = node.attr(XML_ATT_ROOT_ID, false, "");
    path_value = node.attr(XML_ATT_PATH, false, "");
    const auto top = root();
    sameTree = top && top->id_ == rootId;
    return true;
}

bool FlagDir::loadVariables(XmlNode& node, std::unordered_map<String, FlagPtr>& flags,
                            std::unordered_map<String, StepPtr>& steps,
                            std::unordered_map<String, VariantPtr>& variants, const bool copy,
                            const ::cwxeditor::cwx::system::XMLInfo& version) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d1939_p58745(__FILE__, __LINE__);
    try {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d1945_p58755(__FILE__, __LINE__);
        std::vector<std::pair<String, FlagPtr>> pending_flags;
        std::vector<std::pair<String, StepPtr>> pending_steps;
        std::vector<std::pair<String, VariantPtr>> pending_variants;
        std::unordered_map<String, std::size_t> pending_flag_index;
        std::unordered_map<String, std::size_t> pending_step_index;
        std::unordered_map<String, std::size_t> pending_variant_index;
        // Dは型ごとに、入力元パスをキーとする連想マップへ
        // 一時保存する。ここでは異なるパスの初出順を維持し
        // 、同じパスの後続XML項目で保留値を置き換える。
        const auto stage_by_path = [](auto& pending, auto& index, String path,
                                      auto value) {
            const auto [existing, inserted] = index.try_emplace(path, pending.size());
            if (inserted) {
                pending.emplace_back(std::move(path), std::move(value));
            } else {
                pending[existing->second].second = std::move(value);
            }
        };
        for (auto& container : node.find_all("Flags")) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d1923_p59007(__FILE__, __LINE__);
            for (auto& element : container.find_all("Flag")) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d1925_p59070(__FILE__, __LINE__);
                auto value = Flag::createFromNode(element, version);
                if (!copy && !canAppend<Flag>(value->name())) { 
                    flags.clear(); steps.clear(); variants.clear();
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d1928_p59202(__FILE__, __LINE__);return false; }
                stage_by_path(pending_flags, pending_flag_index,
                              removePrefix(element.child_text("Name", true)),
                              std::move(value));
            }
        }
        for (auto& container : node.find_all("Steps")) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d1923_p59406(__FILE__, __LINE__);
            for (auto& element : container.find_all("Step")) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d1925_p59469(__FILE__, __LINE__);
                auto value = Step::createFromNode(element, version);
                if (!copy && !canAppend<Step>(value->name())) { 
                    flags.clear(); steps.clear(); variants.clear();
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d1928_p59601(__FILE__, __LINE__);return false; }
                stage_by_path(pending_steps, pending_step_index,
                              removePrefix(element.child_text("Name", true)),
                              std::move(value));
            }
        }
        for (auto& container : node.find_all("Variants")) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d1923_p59808(__FILE__, __LINE__);
            for (auto& element : container.find_all("Variant")) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d1925_p59874(__FILE__, __LINE__);
                auto value = Variant::createFromNode(element, version);
                if (!copy && !canAppend<Variant>(value->name())) { 
                    flags.clear(); steps.clear(); variants.clear();
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d1928_p60012(__FILE__, __LINE__);return false; }
                stage_by_path(pending_variants, pending_variant_index,
                              removePrefix(element.child_text("Name", true)),
                              std::move(value));
            }
        }
        for (auto& [old_path, value] : pending_flags) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d1961_p60218(__FILE__, __LINE__);
            if (copy && !canAppend<Flag>(value->name())) { 
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d1962_p60276(__FILE__, __LINE__);value->name_ = createNewFlagName(value->name()); }
            if (!add(value)) {
                flags.clear(); steps.clear(); variants.clear();
                return false;
            }
            flags[old_path] = value;
        }
        for (auto& [old_path, value] : pending_steps) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d1969_p60566(__FILE__, __LINE__);
            if (copy && !canAppend<Step>(value->name())) { 
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d1970_p60624(__FILE__, __LINE__);value->name_ = createNewStepName(value->name()); }
            if (!add(value)) {
                flags.clear(); steps.clear(); variants.clear();
                return false;
            }
            steps[old_path] = value;
        }
        for (auto& [old_path, value] : pending_variants) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d1977_p60917(__FILE__, __LINE__);
            if (copy && !canAppend<Variant>(value->name())) { 
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d1978_p60978(__FILE__, __LINE__);value->name_ = createNewVariantName(value->name()); }
            if (!add(value)) {
                flags.clear(); steps.clear(); variants.clear();
                return false;
            }
            variants[old_path] = value;
        }
        return true;
    } catch (const std::exception&) {
        flags.clear(); steps.clear(); variants.clear();
        return false;
    }
}

bool FlagDirTestAccess::loadVariables(
    FlagDir& directory, ::cwxeditor::cwx::xml::XNode& node,
    std::unordered_map<String, FlagPtr>& flags,
    std::unordered_map<String, StepPtr>& steps,
    std::unordered_map<String, VariantPtr>& variants, const bool copy,
    const ::cwxeditor::cwx::system::XMLInfo& version) {
    return directory.loadVariables(node, flags, steps, variants, copy, version);
}

VariantVal VariantTestAccess::toVariantVal(XmlNode& node) {
    return Variant::toVariantVal(node);
}

void VariantTestAccess::fromVariantVal(XmlNode& node, const VariantVal& value) {
    Variant::fromVariantVal(node, value);
}

FlagDirPtr FlagDir::loadSubs(XmlNode& node, std::unordered_map<String, FlagPtr>& flags,
                             std::unordered_map<String, StepPtr>& steps,
                             std::unordered_map<String, VariantPtr>& variants, const bool copy,
                             const ::cwxeditor::cwx::system::XMLInfo& version) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d1993_p61702(__FILE__, __LINE__);
    try {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d1994_p61712(__FILE__, __LINE__);
        auto sub_name = basename(node.attr("path", true));
        if (sub_name.empty()) { 
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d1996_p61802(__FILE__, __LINE__);sub_name = validName(node.attr("rootName", true)); }
        if (!canAppendSub(sub_name)) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d1999_p61891(__FILE__, __LINE__);
            if (!copy) { 
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d2002_p61915(__FILE__, __LINE__);return nullptr; }
            sub_name = createNewDirName(sub_name);
        }
        auto sub = std::make_shared<FlagDir>(sub_name);
        if (!sub->loadVariables(node, flags, steps, variants, false, version)) { 
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d2007_p62127(__FILE__, __LINE__);return nullptr; }
        for (auto& directory : node.find_all("FlagDirectory")) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d2014_p62207(__FILE__, __LINE__);
            if (!sub->loadSubs(directory, flags, steps, variants, false, version)) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d2016_p62292(__FILE__, __LINE__);
                // DのloadSubsは、入れ子のディレクトリがfalseを返
                // した場合、例外がなくても三つの出力マップをすべて消去する。
                flags.clear(); steps.clear(); variants.clear();
                return nullptr;
            }
        }
        (void)add(sub);
        return sub;
    } catch (const std::exception&) {
        flags.clear(); steps.clear(); variants.clear();
        return nullptr;
    }
}

FlagDir::AppendXmlResult FlagDir::loadRootFlagDirectory(
    XmlNode& node, const ::cwxeditor::cwx::system::XMLInfo& version, const bool copy,
    std::unordered_map<String, FlagPtr>& flags, std::unordered_map<String, StepPtr>& steps,
    std::unordered_map<String, VariantPtr>& variants, String& newPath) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d2138_p63062(__FILE__, __LINE__);
    newPath.clear();
    String root_id, source_path;
    bool same_tree = false;
    if (!readAtt(node, root_id, source_path, same_tree)) return AppendXmlResult::FAIL;
    const auto dir_name = basename(source_path);
    if (!copy) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d2145_p63297(__FILE__, __LINE__);
        if (source_path.empty()) { 
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d2146_p63331(__FILE__, __LINE__);return AppendXmlResult::FAIL; }
        if (same_tree && path() == up(source_path)) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d2150_p63414(__FILE__, __LINE__);
            const auto moved = getSubDir(dir_name);
            if (!moved) return AppendXmlResult::FAIL;
            add(moved);
            newPath = moved->path();
            return AppendXmlResult::ON_DIR;
        }
        if (same_tree && has(source_path)) { 
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d2157_p63679(__FILE__, __LINE__);return AppendXmlResult::FAIL; }
        if (!canAppendSub(dir_name)) { 
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d2161_p63746(__FILE__, __LINE__);return AppendXmlResult::FAIL; }
    }
    const auto sub = loadSubs(node, flags, steps, variants, copy, version);
    if (!sub) { 
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d2169_p63872(__FILE__, __LINE__);return AppendXmlResult::FAIL; }
    newPath = sub->path();
    return AppendXmlResult::DIR_SUCCESS;
}

FlagDir::AppendXmlResult FlagDir::appendFromNode(
    XmlNode& node, const ::cwxeditor::cwx::system::XMLInfo& version, const bool copy,
    const bool dirMode, const bool local, std::unordered_map<String, FlagPtr>& flags,
    std::unordered_map<String, StepPtr>& steps, std::unordered_map<String, VariantPtr>& variants,
    String& newPath, String& rootId) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d2080_p64331(__FILE__, __LINE__);
    (void)local;
    flags.clear(); steps.clear(); variants.clear(); newPath.clear();
    rootId = node.attr(XML_ATT_ROOT_ID, false, "");
    if (node.name() == XML_ROOT_FLAGS_AND_STEPS) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d2095_p64520(__FILE__, __LINE__);
        String source_path;
        bool same_tree = false;
        if (!readAtt(node, rootId, source_path, same_tree)) return AppendXmlResult::FAIL;
        if (!copy && same_tree && path() == source_path) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d2099_p64729(__FILE__, __LINE__);
            for (auto& container : node.find_all("Flags")) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d2101_p64790(__FILE__, __LINE__);
                for (auto& element : container.find_all("Flag")) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d2102_p64857(__FILE__, __LINE__);
                    if (auto value = getFlag(removePrefix(element.child_text("Name", true)))) add(value);
                }
            }
            for (auto& container : node.find_all("Steps")) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d2107_p65056(__FILE__, __LINE__);
                for (auto& element : container.find_all("Step")) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d2108_p65123(__FILE__, __LINE__);
                    if (auto value = getStep(removePrefix(element.child_text("Name", true)))) add(value);
                }
            }
            for (auto& container : node.find_all("Variants")) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d2113_p65325(__FILE__, __LINE__);
                for (auto& element : container.find_all("Variant")) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d2114_p65395(__FILE__, __LINE__);
                    if (auto value = getVariant(removePrefix(element.child_text("Name", true)))) add(value);
                }
            }
            return AppendXmlResult::FLAG_STEP_ON_DIR;
        }
        return loadVariables(node, flags, steps, variants, copy, version)
                   ? AppendXmlResult::FLAG_STEP_SUCCESS : AppendXmlResult::FAIL;
    }
    if (dirMode && node.name() == XML_ROOT_FLAG_DIRECTORY) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d2129_p65822(__FILE__, __LINE__);
        return loadRootFlagDirectory(node, version, copy, flags, steps, variants, newPath);
    }
    return AppendXmlResult::FAIL;
}

FlagDir::AppendXmlResult FlagDir::appendFromXML(
    const StringView xml, const ::cwxeditor::cwx::system::XMLInfo& version, const bool copy,
    const bool dirMode, const bool local, std::unordered_map<String, FlagPtr>& flags,
    std::unordered_map<String, StepPtr>& steps, std::unordered_map<String, VariantPtr>& variants,
    String& newPath, String& rootId) {
    // xmlが出力文字列の一つと同じ領域を指す場合も、Dの入力記述子を維持する。
    const String xmlInput(xml);
    flags.clear(); steps.clear(); variants.clear(); newPath.clear(); rootId.clear();
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d2067_p66322(__FILE__, __LINE__);
    try {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d2068_p66332(__FILE__, __LINE__);
        auto document = XmlNode::parse(xmlInput);
        return appendFromNode(document, version, copy, dirMode, local, flags, steps, variants, newPath, rootId);
    } catch (const std::bad_alloc&) {
        // Dがここで捕捉するのはExceptionであり、メモリ確保のErrorではない。
        throw;
    } catch (const std::exception& error) {
        (void)::cwxeditor::cwx::utils::printStackTrace();
        ::cwxeditor::cwx::utils::debugln(error.what());
    }
    return AppendXmlResult::FAIL;
}

FlagDirPtr FlagDir::searchPath(const FlagDirPtr& root, String path) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d2179_p66645(__FILE__, __LINE__);
    if (!root || root->parent()) throw std::logic_error("Search root must have no parent");
    if (path.ends_with(SEPARATOR)) { 
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d2182_p66712(__FILE__, __LINE__);path.erase(path.size() - SEPARATOR.size()); }
    auto current = root;
    const auto parts = FlagDir::splitPath(path);
    for (const auto& part : parts) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d2188_p66833(__FILE__, __LINE__);
        FlagDirPtr next;
        for (const auto& sub : current->subDirs()) {
            if (!sub) throw std::logic_error("Null subdirectory");
            if (part == sub->name()) { next = sub; break; }
        }
        if (!next) return nullptr;
        current = std::move(next);
    }
    return parts.empty() ? nullptr : current;
}

bool FlagDir::rename(String value, UseCounterPtr counter) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d2549_p67032(__FILE__, __LINE__);
    // Dは名前変更・変更のコールバックをまたいで、三つの配列記述子をすべて生存させる。
    [[maybe_unused]] const auto self_owner = weak_from_this().lock();
    const auto flags = allFlags();
    std::vector<String> old_flag_paths;
    for (const auto& entry : flags) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d2552_p67114(__FILE__, __LINE__);old_flag_paths.push_back(entry->path()); }
    const auto steps = allSteps();
    std::vector<String> old_step_paths;
    for (const auto& entry : steps) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d2557_p67236(__FILE__, __LINE__);old_step_paths.push_back(entry->path()); }
    const auto variants = allVariants();
    std::vector<String> old_variant_paths;
    for (const auto& entry : variants) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d2562_p67364(__FILE__, __LINE__);old_variant_paths.push_back(entry->path()); }

    const auto old_path = path();
    auto prefix_length = old_path.size();
    if (!old_path.ends_with(SEPARATOR)) { 
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d2567_p67525(__FILE__, __LINE__);++prefix_length; }
    if (!name(std::move(value))) { 
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d2571_p67575(__FILE__, __LINE__);return false; }

    const auto new_path = path();
    for (const auto& old_value : old_flag_paths) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d2575_p67705(__FILE__, __LINE__);
        // Dは旧パスを切り出してuc.changeを呼ぶ。失敗を飛ばしてはならない。
        if (prefix_length > old_value.size()) throw std::out_of_range("Renamed variable path prefix");
        const auto replacement = FlagDir::join(new_path, old_value.substr(prefix_length));
        if (!counter) throw std::logic_error("Missing rename use counter");
        counter->change(::cwxeditor::cwx::usecounter::toFlagId(old_value),
                        ::cwxeditor::cwx::usecounter::toFlagId(replacement));
    }
    for (const auto& old_value : old_step_paths) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d2579_p68062(__FILE__, __LINE__);
        if (prefix_length > old_value.size()) throw std::out_of_range("Renamed variable path prefix");
        const auto replacement = FlagDir::join(new_path, old_value.substr(prefix_length));
        if (!counter) throw std::logic_error("Missing rename use counter");
        counter->change(::cwxeditor::cwx::usecounter::toStepId(old_value),
                        ::cwxeditor::cwx::usecounter::toStepId(replacement));
    }
    for (const auto& old_value : old_variant_paths) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d2583_p68422(__FILE__, __LINE__);
        if (prefix_length > old_value.size()) throw std::out_of_range("Renamed variable path prefix");
        const auto replacement = FlagDir::join(new_path, old_value.substr(prefix_length));
        if (!counter) throw std::logic_error("Missing rename use counter");
        counter->change(::cwxeditor::cwx::usecounter::toVariantId(old_value),
                        ::cwxeditor::cwx::usecounter::toVariantId(replacement));
    }
    return true;
}

String FlagDir::cwxPath(const bool id) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d1177_p68795(__FILE__, __LINE__);
    if (const auto owner = owner_.lock()) { 
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d1178_p68838(__FILE__, __LINE__);return ::cwxeditor::cwx::path::cpjoin(owner->cwxPath(id), "variable"); }
    if (const auto p = parent()) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d1180_p68943(__FILE__, __LINE__);
        return ::cwxeditor::cwx::path::cpjoin(p->cwxPath(id), "dir",
                                              static_cast<std::size_t>(p->indexOf(*this)));
    }
    return {};
}

CwxPathPtr FlagDir::findCWXPath(StringView value) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d1185_p69180(__FILE__, __LINE__);
    if (::cwxeditor::cwx::path::cpempty(value)) {
        return std::static_pointer_cast<CwxPath>(self());
    }
    const auto category = ::cwxeditor::cwx::path::cpcategory(value);
    const auto index = ::cwxeditor::cwx::path::cpindex(value);
    if (category == "flag" && index < flags_.size()) return flags_[index]->findCWXPath(::cwxeditor::cwx::path::cpbottom(value));
    if (category == "step" && index < steps_.size()) return steps_[index]->findCWXPath(::cwxeditor::cwx::path::cpbottom(value));
    if (category == "variant" && index < variants_.size()) return variants_[index]->findCWXPath(::cwxeditor::cwx::path::cpbottom(value));
    if (category == "dir" && index < subdirs_.size()) return subdirs_[index]->findCWXPath(::cwxeditor::cwx::path::cpbottom(value));
    return {};
}

std::vector<CwxPathObserver> FlagDir::cwxChilds() {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d1215_p70089(__FILE__, __LINE__);
    std::vector<CwxPathObserver> result;
    for (const auto& value : flags_) result.emplace_back(std::static_pointer_cast<CwxPath>(value));
    for (const auto& value : steps_) result.emplace_back(std::static_pointer_cast<CwxPath>(value));
    for (const auto& value : variants_) result.emplace_back(std::static_pointer_cast<CwxPath>(value));
    for (const auto& value : subdirs_) result.emplace_back(std::static_pointer_cast<CwxPath>(value));
    return result;
}

CwxPathObserver FlagDir::cwxParent() {
    if (const auto owner = owner_.lock()) return owner;
    const auto p = parent();
    return p ? std::static_pointer_cast<CwxPath>(p) : CwxPathObserver{};
}

void getNode(const FlagDirPtr& parent, std::span<const FlagPtr> flags,
             std::span<const StepPtr> steps, std::span<const VariantPtr> variants, XmlNode& node) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d61_p70928(__FILE__, __LINE__);
    if (!parent) throw FlagException("Flag parent is null.");
    node = XmlNode::create(XML_ROOT_FLAGS_AND_STEPS);
    const auto root = parent->root();
    add_attr(node, XML_ATT_PATH, parent->path());
    add_attr(node, XML_ATT_ROOT_ID, root ? root->id() : String{});
    auto flag_node = node.newElement("Flags");
    for (const auto& value : flags) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d67_p71284(__FILE__, __LINE__);
        if (!value || value->parent().get() != parent.get()) throw FlagException("Flag parent mismatch.");
        value->toNode(flag_node);
    }
    auto step_node = node.newElement("Steps");
    for (const auto& value : steps) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d73_p71516(__FILE__, __LINE__);
        if (!value || value->parent().get() != parent.get()) throw FlagException("Step parent mismatch.");
        value->toNode(step_node);
    }
    auto variant_node = node.newElement("Variants");
    for (const auto& value : variants) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d79_p71757(__FILE__, __LINE__);
        if (!value || value->parent().get() != parent.get()) throw FlagException("Variant parent mismatch.");
        value->toNode(variant_node);
    }
}

String getXML(const FlagDirPtr& parent, std::span<const FlagPtr> flags,
              std::span<const StepPtr> steps, std::span<const VariantPtr> variants) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d55_p72071(__FILE__, __LINE__);
    XmlNode node;
    getNode(parent, flags, steps, variants, node);
    return node.text();
}

void to_node_recursive(XmlNode& node, const FlagDirPtr& directory) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d101_p72236(__FILE__, __LINE__);
    add_attr(node, XML_ATT_PATH, directory->path());
    auto flags = node.newElement("Flags");
    for (const auto& value : directory->flags()) { 
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d104_p72382(__FILE__, __LINE__);value->toNode(flags); }
    auto steps = node.newElement("Steps");
    for (const auto& value : directory->steps()) { 
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d108_p72496(__FILE__, __LINE__);value->toNode(steps); }
    auto variants = node.newElement("Variants");
    for (const auto& value : directory->variants()) { 
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d112_p72619(__FILE__, __LINE__);value->toNode(variants); }
    for (const auto& value : directory->subDirs()) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d115_p72696(__FILE__, __LINE__);
        auto child = node.newElement(XML_ROOT_FLAG_DIRECTORY);
        to_node_recursive(child, value);
    }
}

XmlNode toNode(const StringView rootName, const FlagDirPtr& directory) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d91_p72882(__FILE__, __LINE__);
    if (!directory) throw FlagException("Flag directory is null.");
    auto result = XmlNode::create(XML_ROOT_FLAG_DIRECTORY);
    to_node_recursive(result, directory);
    const auto root = directory->root();
    add_attr(result, XML_ATT_ROOT_ID, root ? root->id() : String{});
    if (root && root.get() == directory.get()) { 
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d96_p73210(__FILE__, __LINE__);add_attr(result, XML_ATT_ROOT_NAME, rootName); }
    return result;
}

String getXML(const StringView rootName, const FlagDirPtr& directory) {
    ::cwxeditor::cwx::perf::TraceScope trace_flag_d87_p73350(__FILE__, __LINE__);
    return toNode(rootName, directory).text();
}

int pathCompare(const StringView first, const StringView second, const bool logicalSort) {
    const auto a = FlagDir::splitPath(first);
    const auto b = FlagDir::splitPath(second);
    return pathCompare(a, b, logicalSort);
}

int pathCompare(std::span<const String> a, std::span<const String> b, const bool logicalSort) {
    const auto count = std::max(a.size(), b.size());
    for (std::size_t i = 0; i < count; ++i) {
        if (i >= a.size()) return -1;
        if (i >= b.size()) return 1;
        const auto comparison = logicalSort ? ::cwxeditor::cwx::utils::ncmp(a[i], b[i]) : a[i].compare(b[i]);
        if (comparison != 0) return comparison;
    }
    return 0;
}

}  // namespace cwxeditor::cwx::flag
