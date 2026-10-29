#pragma once

// cwx/flag.dのC++20移植。

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "cwxeditor_src/core/cwx/expression.hpp"
#include "cwxeditor_src/core/cwx/path.hpp"
#include "cwxeditor_src/core/cwx/system.hpp"
#include "cwxeditor_src/core/cwx/textholder.hpp"
#include "cwxeditor_src/core/cwx/types.hpp"
#include "cwxeditor_src/core/cwx/usecounter.hpp"
#include "cwxeditor_src/core/cwx/utils.hpp"
#include "cwxeditor_src/core/cwx/xml.hpp"
#include "cwxeditor_src/lib/portable.hpp"
#include "cwxeditor_src/lib/array_slice.hpp"

namespace cwxeditor::cwx::flag {

using String = ::cwxeditor::lib::String;
using StringView = ::cwxeditor::lib::StringView;
using ChangeHandler = std::function<void()>;
using VariantVal = ::cwxeditor::cwx::expression::VariantVal;
using VariantValues = std::vector<VariantVal>;
using UseCounter = ::cwxeditor::cwx::usecounter::UseCounter;
using UseCounterPtr = ::cwxeditor::lib::Owner<UseCounter>;
using CwxPath = ::cwxeditor::cwx::path::CWXPath;
using CwxPathPtr = ::cwxeditor::lib::Owner<CwxPath>;
using CwxPathObserver = ::cwxeditor::lib::Observer<CwxPath>;

inline constexpr StringView XML_ROOT_FLAGS_AND_STEPS = "FlagsAndSteps";
inline constexpr StringView XML_ROOT_FLAG_DIRECTORY = "FlagDirectory";
inline constexpr StringView XML_ATT_ROOT_ID = "rootId";
inline constexpr StringView XML_ATT_PATH = "path";
inline constexpr StringView XML_ATT_ROOT_NAME = "rootName";

class FlagException : public std::runtime_error {
public:
    explicit FlagException(String message);
};

class Flag;
class Step;
class Variant;
class FlagDir;
struct VariantTestAccess;

using FlagPtr = ::cwxeditor::lib::Owner<Flag>;
using StepPtr = ::cwxeditor::lib::Owner<Step>;
using VariantPtr = ::cwxeditor::lib::Owner<Variant>;
using FlagDirPtr = ::cwxeditor::lib::Owner<FlagDir>;
using FlagList = std::vector<FlagPtr>;
using StepList = std::vector<StepPtr>;
using VariantList = std::vector<VariantPtr>;
using FlagDirList = std::vector<FlagDirPtr>;

/// ローカル変数のツリーを所有する資源。
class LocalVariableOwner : public virtual CwxPath {
public:
    ~LocalVariableOwner() override = default;
    [[nodiscard]] virtual std::uint64_t id() const = 0;
    [[nodiscard]] virtual String name() const = 0;
    [[nodiscard]] virtual FlagDirPtr flagDirRoot() const = 0;
    [[nodiscard]] virtual UseCounterPtr useCounter() const = 0;
};

class Flag : public CwxPath,
             public ::cwxeditor::cwx::path::Commentable,
             public std::enable_shared_from_this<Flag> {
public:
    using toID = ::cwxeditor::cwx::usecounter::FlagId;

    Flag(const Flag& copyBase);
    Flag(String name, String on, String off, bool onOff);
    ~Flag() override;

    [[nodiscard]] bool operator==(const Flag& other) const;
    [[nodiscard]] bool operator!=(const Flag& other) const { return !(*this == other); }
    [[nodiscard]] int opCmp(const Flag& other) const;

    void copyFrom(const Flag& value);

    [[nodiscard]] FlagDirPtr parent() const;
    [[nodiscard]] FlagDirPtr root() const;
    void changeHandler(ChangeHandler change);
    [[nodiscard]] const ChangeHandler& changeHandler() const noexcept { return change_; }
    void useCounter(UseCounterPtr counter);
    [[nodiscard]] UseCounterPtr useCounter() const { return use_counter_; }

    [[nodiscard]] String name() const { return name_; }
    bool name(String value);
    [[nodiscard]] String on() const;
    void on(String value);
    [[nodiscard]] String off() const;
    void off(String value);
    [[nodiscard]] bool onOff() const noexcept { return on_off_; }
    void onOff(bool value);
    [[nodiscard]] bool expandSPChars() const noexcept { return expand_sp_chars_; }
    void expandSPChars(bool value);
    [[nodiscard]] ::cwxeditor::cwx::types::VariableInitialization initialization() const noexcept {
        return initialization_;
    }
    void initialization(::cwxeditor::cwx::types::VariableInitialization value);
    [[nodiscard]] std::vector<String> flagsInText(bool value) const;
    [[nodiscard]] std::vector<String> stepsInText(bool value) const;
    [[nodiscard]] std::vector<String> variantsInText(bool value) const;

    [[nodiscard]] String comment() const override { return comment_; }
    void comment(String value) override;
    [[nodiscard]] String path() const;
    void changed() override;

    [[nodiscard]] String toXml() const;
    static FlagPtr createFromNode(::cwxeditor::cwx::xml::XNode& node,
                                  const ::cwxeditor::cwx::system::XMLInfo& version);
    void toNode(::cwxeditor::cwx::xml::XNode& node) const;

    [[nodiscard]] String cwxPath(bool id) override;
    [[nodiscard]] CwxPathPtr findCWXPath(StringView path) override;
    [[nodiscard]] std::vector<CwxPathObserver> cwxChilds() override { return {}; }
    [[nodiscard]] CwxPathObserver cwxParent() override;

private:
    friend class FlagDir;
    void parent(FlagDirPtr value);
    void initTextHolders();

    String name_;
    std::unique_ptr<::cwxeditor::cwx::textholder::TextHolder> on_;
    std::unique_ptr<::cwxeditor::cwx::textholder::TextHolder> off_;
    bool on_off_{false};
    bool expand_sp_chars_{false};
    ::cwxeditor::cwx::types::VariableInitialization initialization_{
        ::cwxeditor::cwx::types::VariableInitialization::Leave};
    std::weak_ptr<FlagDir> parent_;
    ChangeHandler change_;
    UseCounterPtr use_counter_;
    String comment_;
};

class Step : public CwxPath,
             public ::cwxeditor::cwx::path::Commentable,
             public std::enable_shared_from_this<Step> {
public:
    using toID = ::cwxeditor::cwx::usecounter::StepId;

    Step(const Step& copyBase);
    Step(String name, std::vector<String> values, std::uint32_t select);
    ~Step() override;

    [[nodiscard]] bool operator==(const Step& other) const;
    [[nodiscard]] bool operator!=(const Step& other) const { return !(*this == other); }
    [[nodiscard]] int opCmp(const Step& other) const;

    void copyFrom(const Step& value);
    [[nodiscard]] FlagDirPtr parent() const;
    [[nodiscard]] FlagDirPtr root() const;
    void changeHandler(ChangeHandler change);
    [[nodiscard]] const ChangeHandler& changeHandler() const noexcept { return change_; }

    [[nodiscard]] String name() const { return name_; }
    bool name(String value);
    void setValue(std::uint32_t index, String value);
    [[nodiscard]] String getValue(std::uint32_t index) const;
    [[nodiscard]] std::uint32_t count() const noexcept {
        return static_cast<std::uint32_t>(values_.size());
    }
    [[nodiscard]] std::uint32_t select() const noexcept { return select_; }
    void select(std::uint32_t value);
    [[nodiscard]] String value() const;
    [[nodiscard]] std::vector<String> values() const;
    void setValues(::cwxeditor::lib::SharedSlice<const String> values, std::int32_t select);
    void setValues(::cwxeditor::lib::SharedSlice<String> values, std::int32_t select) {
        setValues(::cwxeditor::lib::SharedSlice<const String>(values), select);
    }
    void setValues(std::initializer_list<String> values, std::int32_t select) {
        setValues(::cwxeditor::lib::SharedSlice<const String>(values), select);
    }
    void setValues(std::span<const String> values, std::int32_t select);
    void setValues(const std::vector<String>& values, std::int32_t select) {
        setValues(std::span<const String>(values), select);
    }
    [[nodiscard]] bool expandSPChars() const noexcept { return expand_sp_chars_; }
    void expandSPChars(bool value);
    [[nodiscard]] ::cwxeditor::cwx::types::VariableInitialization initialization() const noexcept {
        return initialization_;
    }
    void initialization(::cwxeditor::cwx::types::VariableInitialization value);
    [[nodiscard]] std::vector<String> flagsInText(std::uint32_t value) const;
    [[nodiscard]] std::vector<String> stepsInText(std::uint32_t value) const;
    [[nodiscard]] std::vector<String> variantsInText(std::uint32_t value) const;
    [[nodiscard]] String comment() const override { return comment_; }
    void comment(String value) override;
    [[nodiscard]] String path() const;
    void changed() override;
    void useCounter(UseCounterPtr counter);
    [[nodiscard]] UseCounterPtr useCounter() const { return use_counter_; }

    [[nodiscard]] String toXml() const;
    static StepPtr createFromNode(::cwxeditor::cwx::xml::XNode& node,
                                  const ::cwxeditor::cwx::system::XMLInfo& version);
    void toNode(::cwxeditor::cwx::xml::XNode& node) const;

    [[nodiscard]] String cwxPath(bool id) override;
    [[nodiscard]] CwxPathPtr findCWXPath(StringView path) override;
    [[nodiscard]] std::vector<CwxPathObserver> cwxChilds() override { return {}; }
    [[nodiscard]] CwxPathObserver cwxParent() override;

private:
    friend class FlagDir;
    void parent(FlagDirPtr value);
    void rebuildTextHolders(std::size_t count);

    String name_;
    // Dのforeachの記述子は、入れ子の更新中もTextHolderを保持する。
    std::vector<std::shared_ptr<::cwxeditor::cwx::textholder::TextHolder>> values_;
    std::uint32_t select_{0};
    bool expand_sp_chars_{false};
    ::cwxeditor::cwx::types::VariableInitialization initialization_{
        ::cwxeditor::cwx::types::VariableInitialization::Leave};
    std::weak_ptr<FlagDir> parent_;
    ChangeHandler change_;
    UseCounterPtr use_counter_;
    String comment_;
};

class Variant : public CwxPath,
                public ::cwxeditor::cwx::path::Commentable,
                public std::enable_shared_from_this<Variant> {
public:
    using toID = ::cwxeditor::cwx::usecounter::VariantId;
    static constexpr std::size_t DECIMAL_PLACES = 8;

    Variant(const Variant& copyBase);
    Variant(String name, double value);
    Variant(String name, String value);
    Variant(String name, bool value);
    Variant(String name, VariantValues value);
    Variant(String name, String structName, VariantValues value);
    ~Variant() override = default;

    [[nodiscard]] bool operator==(const Variant& other) const;
    [[nodiscard]] bool operator!=(const Variant& other) const { return !(*this == other); }
    [[nodiscard]] int opCmp(const Variant& other) const;

    void copyFrom(const Variant& value);
    [[nodiscard]] FlagDirPtr parent() const;
    [[nodiscard]] FlagDirPtr root() const;
    void changeHandler(ChangeHandler change);
    [[nodiscard]] const ChangeHandler& changeHandler() const noexcept { return change_; }
    [[nodiscard]] String name() const { return name_; }
    bool name(String value);
    void value(double value);
    void value(String value);
    void value(bool value);
    void value(VariantValues value);
    void setStructValue(String structName, VariantValues value);
    [[nodiscard]] ::cwxeditor::cwx::types::VariantType type() const noexcept { return type_; }
    [[nodiscard]] double numVal() const noexcept { return num_value_; }
    [[nodiscard]] String strVal() const { return str_value_; }
    [[nodiscard]] bool boolVal() const noexcept { return bool_value_; }
    [[nodiscard]] const VariantValues& listVal() const noexcept { return list_value_; }
    [[nodiscard]] String structName() const { return struct_name_; }
    [[nodiscard]] const VariantValues& structVal() const noexcept { return struct_value_; }
    [[nodiscard]] ::cwxeditor::cwx::types::VariableInitialization initialization() const noexcept {
        return initialization_;
    }
    void initialization(::cwxeditor::cwx::types::VariableInitialization value);
    [[nodiscard]] String comment() const override { return comment_; }
    void comment(String value) override;
    [[nodiscard]] String path() const;
    void changed() override;
    void useCounter(UseCounterPtr counter) { use_counter_ = std::move(counter); }
    [[nodiscard]] UseCounterPtr useCounter() const { return use_counter_; }

    [[nodiscard]] String toXml() const;
    static VariantPtr createFromNode(::cwxeditor::cwx::xml::XNode& node,
                                     const ::cwxeditor::cwx::system::XMLInfo& version);
    void toNode(::cwxeditor::cwx::xml::XNode& node) const;

    [[nodiscard]] String cwxPath(bool id) override;
    [[nodiscard]] CwxPathPtr findCWXPath(StringView path) override;
    [[nodiscard]] std::vector<CwxPathObserver> cwxChilds() override { return {}; }
    [[nodiscard]] CwxPathObserver cwxParent() override;

private:
    friend class FlagDir;
    friend struct VariantTestAccess;
    void parent(FlagDirPtr value);

    String name_;
    static VariantVal toVariantVal(::cwxeditor::cwx::xml::XNode& node);
    static void fromVariantVal(::cwxeditor::cwx::xml::XNode& node, const VariantVal& value);
    ::cwxeditor::cwx::types::VariantType type_{
        ::cwxeditor::cwx::types::VariantType::Number};
    double num_value_{0.0};
    String str_value_;
    bool bool_value_{true};
    VariantValues list_value_;
    String struct_name_;
    VariantValues struct_value_;
    ::cwxeditor::cwx::types::VariableInitialization initialization_{
        ::cwxeditor::cwx::types::VariableInitialization::Leave};
    std::weak_ptr<FlagDir> parent_;
    ChangeHandler change_;
    UseCounterPtr use_counter_;
    String comment_;
};

class FlagDir : public CwxPath,
                public std::enable_shared_from_this<FlagDir> {
public:
    static constexpr StringView SEPARATOR = "\\";
    static constexpr StringView SEPARATOR_REGEX = "\\\\";

    static String join(StringView parent, StringView path);
    explicit FlagDir(String name);
    FlagDir(CwxPathPtr owner, String localVariablePrefix);
    FlagDir(CwxPathPtr owner, const FlagDir& copyBase);
    ~FlagDir() override = default;

    [[nodiscard]] CwxPathPtr localOwner() const;
    // ローカルルートは、それを所有する共有カードの準備前に構築される。
    // 実際の所有者だけを設定し、この参照自体ではカードを保持しない。
    void localOwner(const CwxPathPtr& value) { owner_ = value; }
    [[nodiscard]] bool operator==(const FlagDir& other) const;
    [[nodiscard]] bool operator!=(const FlagDir& other) const { return !(*this == other); }
    [[nodiscard]] FlagDirPtr parent() const;
    void changeHandler(ChangeHandler change);
    [[nodiscard]] const ChangeHandler& changeHandler() const noexcept { return change_; }
    void useCounter(UseCounterPtr counter);
    [[nodiscard]] UseCounterPtr useCounter() const { return use_counter_; }
    [[nodiscard]] FlagDirPtr root() const;
    [[nodiscard]] String id() const { return id_; }
    [[nodiscard]] String name() const { return name_; }
    bool name(String value);

    template <typename T>
    [[nodiscard]] auto& names() {
        if constexpr (std::is_same_v<T, Flag>) return flag_names_;
        else if constexpr (std::is_same_v<T, Step>) return step_names_;
        else if constexpr (std::is_same_v<T, Variant>) return variant_names_;
        else return dir_names_;
    }
    template <typename T>
    [[nodiscard]] const auto& names() const {
        if constexpr (std::is_same_v<T, Flag>) return flag_names_;
        else if constexpr (std::is_same_v<T, Step>) return step_names_;
        else if constexpr (std::is_same_v<T, Variant>) return variant_names_;
        else return dir_names_;
    }
    template <typename T>
    [[nodiscard]] bool canAppend(StringView value) const {
        const auto normalized = validName(String(value));
        if (normalized.empty()) return false;
        return !names<T>().contains(normalized);
    }

    [[nodiscard]] bool canAppendSub(StringView value) const;
    [[nodiscard]] bool canAppendSub2(const FlagDir& value) const;

    bool add(const FlagPtr& value, bool rename = false);
    bool insert(std::ptrdiff_t index, const FlagPtr& value, bool rename = false);
    bool add(const StepPtr& value, bool rename = false);
    bool insert(std::ptrdiff_t index, const StepPtr& value, bool rename = false);
    bool add(const VariantPtr& value, bool rename = false);
    bool insert(std::ptrdiff_t index, const VariantPtr& value, bool rename = false);
    bool add(const FlagDirPtr& value, bool rename = false);
    bool insert(std::ptrdiff_t index, const FlagDirPtr& value, bool rename = false);
    void remove(const FlagPtr& value);
    void remove(const StepPtr& value);
    void remove(const VariantPtr& value);
    void remove(const FlagDirPtr& value);
    void removeAll();

    [[nodiscard]] const FlagDirList& subDirs() const noexcept { return subdirs_; }
    [[nodiscard]] const FlagList& flags() const noexcept { return flags_; }
    [[nodiscard]] const StepList& steps() const noexcept { return steps_; }
    [[nodiscard]] const VariantList& variants() const noexcept { return variants_; }
    [[nodiscard]] bool containsFlag(StringView value) const { return flag_names_.contains(String(value)); }
    [[nodiscard]] bool containsStep(StringView value) const { return step_names_.contains(String(value)); }
    [[nodiscard]] bool containsVariant(StringView value) const { return variant_names_.contains(String(value)); }
    [[nodiscard]] bool containsSubDir(StringView value) const { return dir_names_.contains(String(value)); }

    template <typename T>
    [[nodiscard]] bool contains(StringView value) const {
        if constexpr (std::is_same_v<T, Flag>) return containsFlag(value);
        else if constexpr (std::is_same_v<T, Step>) return containsStep(value);
        else if constexpr (std::is_same_v<T, Variant>) return containsVariant(value);
        else return containsSubDir(value);
    }

    [[nodiscard]] std::ptrdiff_t indexOf(StringView value) const;
    [[nodiscard]] std::ptrdiff_t indexOf(const Flag& value) const;
    [[nodiscard]] std::ptrdiff_t indexOf(const Step& value) const;
    [[nodiscard]] std::ptrdiff_t indexOf(const Variant& value) const;
    [[nodiscard]] std::ptrdiff_t indexOf(const FlagDir& value) const;
    [[nodiscard]] FlagPtr getFlag(StringView value) const;
    [[nodiscard]] StepPtr getStep(StringView value) const;
    [[nodiscard]] VariantPtr getVariant(StringView value) const;
    [[nodiscard]] FlagDirPtr getSubDir(StringView value) const;

    [[nodiscard]] FlagList allFlags() const;
    [[nodiscard]] StepList allSteps() const;
    [[nodiscard]] VariantList allVariants() const;
    [[nodiscard]] FlagDirList allSubDirs() const;
    [[nodiscard]] bool hasFlag() const;
    [[nodiscard]] bool hasStep() const;
    [[nodiscard]] bool hasVariant() const;
    [[nodiscard]] String path() const;
    void changed() override;

    void toNode(::cwxeditor::cwx::xml::XNode& node, bool createEmptyElement = true) const;
    static String basename(String path);
    [[nodiscard]] static std::vector<String> splitPath(StringView path);
    [[nodiscard]] bool has(String path) const;
    static String up(String path);

    enum class AppendXmlResult {
        DIR_SUCCESS,
        FLAG_STEP_SUCCESS,
        FAIL,
        FLAG_STEP_ON_DIR,
        ON_DIR,
    };

    AppendXmlResult appendFromXML(
        StringView xml, const ::cwxeditor::cwx::system::XMLInfo& version, bool copy,
        bool dirMode, bool local, std::unordered_map<String, FlagPtr>& flags,
        std::unordered_map<String, StepPtr>& steps,
        std::unordered_map<String, VariantPtr>& variants, String& newPath,
        String& rootId);
    AppendXmlResult appendFromNode(
        ::cwxeditor::cwx::xml::XNode& node, const ::cwxeditor::cwx::system::XMLInfo& version,
        bool copy, bool dirMode, bool local,
        std::unordered_map<String, FlagPtr>& flags,
        std::unordered_map<String, StepPtr>& steps,
        std::unordered_map<String, VariantPtr>& variants, String& newPath,
        String& rootId);

    static FlagDirPtr searchPath(const FlagDirPtr& root, String path);
    static String validName(String name);
    String createNewFlagName(String base, String oldName = {});
    String createNewStepName(String base, String oldName = {});
    String createNewVariantName(String base, String oldName = {});
    String createNewDirName(String base, String oldName = {});

    template <typename T>
    String createNewName(String base, String oldName = {}) {
        if constexpr (std::is_same_v<T, Flag>) return createNewFlagName(std::move(base), std::move(oldName));
        else if constexpr (std::is_same_v<T, Step>) return createNewStepName(std::move(base), std::move(oldName));
        else if constexpr (std::is_same_v<T, Variant>) return createNewVariantName(std::move(base), std::move(oldName));
        else return createNewDirName(std::move(base), std::move(oldName));
    }

    std::vector<String> createNewFlagNames(String base, std::size_t count,
                                           std::span<const String> oldNames = {});
    std::vector<String> createNewStepNames(String base, std::size_t count,
                                           std::span<const String> oldNames = {});
    std::vector<String> createNewVariantNames(String base, std::size_t count,
                                               std::span<const String> oldNames = {});
    std::vector<String> createNewDirNames(String base, std::size_t count,
                                          std::span<const String> oldNames = {});

    FlagDirPtr findPath(String path, bool create);
    [[nodiscard]] FlagDirPtr findPath(String path) const;
    FlagPtr findFlag(String path);
    [[nodiscard]] FlagPtr findFlag(String path) const;
    StepPtr findStep(String path);
    [[nodiscard]] StepPtr findStep(String path) const;
    VariantPtr findVariant(String path);
    [[nodiscard]] VariantPtr findVariant(String path) const;

    template <typename T>
    auto find(String value) {
        if constexpr (std::is_same_v<T, Flag>) return findFlag(std::move(value));
        else if constexpr (std::is_same_v<T, Step>) return findStep(std::move(value));
        else return findVariant(std::move(value));
    }
    template <typename T>
    auto find(String value) const {
        if constexpr (std::is_same_v<T, Flag>) return findFlag(std::move(value));
        else if constexpr (std::is_same_v<T, Step>) return findStep(std::move(value));
        else return findVariant(std::move(value));
    }

    void toNodeAll(::cwxeditor::cwx::xml::XNode& node, bool logicalSort,
                   bool createEmptyElement = true) const;
    void fromXmlNode(::cwxeditor::cwx::xml::XNode& node,
                     const ::cwxeditor::cwx::system::XMLInfo& version,
                     bool startParse = true);
    bool rename(String value, UseCounterPtr counter);

    [[nodiscard]] String cwxPath(bool id) override;
    [[nodiscard]] CwxPathPtr findCWXPath(StringView value) override;
    [[nodiscard]] std::vector<CwxPathObserver> cwxChilds() override;
    [[nodiscard]] CwxPathObserver cwxParent() override;

private:
    friend struct FlagDirTestAccess;
    friend class Flag;
    friend class Step;
    friend class Variant;
    void parent(FlagDirPtr value);
    [[nodiscard]] bool empty() const;
    [[nodiscard]] FlagDirPtr self();
    // C++のコンストラクター中はshared_from_thisを使えない。ディレ
    // クトリとともに参照の基点を保ち、 コピーされた子も参照を維持できるようにする。
    // 別名共有を使い、make_sharedが本来の所有者を設定する前に 借用の所有
    // 者でenable_shared_from_thisを初期化しないようにする。
    FlagDirPtr construction_anchor_{std::make_shared<int>(0), this};
    [[nodiscard]] String localPathToPath(String value) const;
    static String removePrefix(String value);
    bool readAtt(const ::cwxeditor::cwx::xml::XNode& node,
                 String& rootId, String& path, bool& sameTree) const;
    bool loadVariables(::cwxeditor::cwx::xml::XNode& node,
                       std::unordered_map<String, FlagPtr>& flags,
                       std::unordered_map<String, StepPtr>& steps,
                       std::unordered_map<String, VariantPtr>& variants, bool copy,
                       const ::cwxeditor::cwx::system::XMLInfo& version);
    FlagDirPtr loadSubs(::cwxeditor::cwx::xml::XNode& node,
                        std::unordered_map<String, FlagPtr>& flags,
                        std::unordered_map<String, StepPtr>& steps,
                        std::unordered_map<String, VariantPtr>& variants, bool copy,
                        const ::cwxeditor::cwx::system::XMLInfo& version);
    AppendXmlResult loadRootFlagDirectory(
        ::cwxeditor::cwx::xml::XNode& node, const ::cwxeditor::cwx::system::XMLInfo& version,
        bool copy, std::unordered_map<String, FlagPtr>& flags,
        std::unordered_map<String, StepPtr>& steps,
        std::unordered_map<String, VariantPtr>& variants, String& newPath);

    String name_;
    std::weak_ptr<CwxPath> owner_;
    String local_variable_prefix_;
    std::weak_ptr<FlagDir> parent_;
    FlagDirList subdirs_;
    FlagList flags_;
    StepList steps_;
    VariantList variants_;
    std::unordered_map<String, FlagPtr> flag_names_;
    std::unordered_map<String, StepPtr> step_names_;
    std::unordered_map<String, VariantPtr> variant_names_;
    std::unordered_map<String, FlagDirPtr> dir_names_;
    String id_;
    ChangeHandler change_;
    UseCounterPtr use_counter_;
};

String getXML(const FlagDirPtr& parent, std::span<const FlagPtr> flags,
              std::span<const StepPtr> steps, std::span<const VariantPtr> variants);
void getNode(const FlagDirPtr& parent, std::span<const FlagPtr> flags,
             std::span<const StepPtr> steps, std::span<const VariantPtr> variants,
             ::cwxeditor::cwx::xml::XNode& node);
String getXML(StringView rootName, const FlagDirPtr& dir);
::cwxeditor::cwx::xml::XNode toNode(StringView rootName, const FlagDirPtr& dir);

int pathCompare(StringView first, StringView second, bool logicalSort);
int pathCompare(std::span<const String> first, std::span<const String> second, bool logicalSort);

template <typename T, typename Yield>
void sortedWithName(const std::vector<std::shared_ptr<T>>& values, bool logicalSort, Yield&& yield) {
    auto sorted = values;
    std::stable_sort(sorted.begin(), sorted.end(), [logicalSort](const auto& first, const auto& second) {
        if (logicalSort) {
            const auto a = ::cwxeditor::cwx::utils::ncmp(first->name(), second->name());
            return a < 0;
        }
        return first->name() < second->name();
    });
    for (const auto& value : sorted) yield(value);
}

template <typename T, typename Yield>
void sortedWithPath(const std::vector<std::shared_ptr<T>>& values, bool logicalSort, Yield&& yield) {
    // yieldのコールバックが呼び出し元の関数を置き換えても、Dのデリゲ
    // ート値を保持する。 言語の境界で外側のラッパーを一度だけコピーする。
    auto emit = std::forward<Yield>(yield);
    auto sorted = values;
    // Dの既定の並べ替えは不安定で、同値の順序を保証しない。 許される順
    // 序の一つとして、従来のネイティブ実装の安定した同値順序を維持する。
    std::stable_sort(sorted.begin(), sorted.end(), [logicalSort](const auto& first, const auto& second) {
        if (!first) throw std::logic_error("Null variable path owner");
        const auto a = FlagDir::splitPath(std::as_const(*first).path());
        if (!second) throw std::logic_error("Null variable path owner");
        const auto b = FlagDir::splitPath(std::as_const(*second).path());
        return ::cwxeditor::cwx::flag::pathCompare(a, b, logicalSort) < 0;
    });
    for (const auto& value : sorted) {
        if constexpr (std::is_pointer_v<decltype(emit)>) {
            if (!emit) throw std::logic_error("Missing path yield callback");
        }
        emit(value);
    }
}

template <typename T>
std::shared_ptr<T> findVar(const FlagDirPtr& root, const UseCounterPtr& counter,
                           StringView value) {
    if (counter) {
        const auto owner = counter->owner();
        if (const auto local_owner = std::dynamic_pointer_cast<LocalVariableOwner>(owner)) {
            const auto local_root = local_owner->flagDirRoot();
            if (!local_root) throw std::invalid_argument("Null local variable root");
            if (const auto local_value = local_root->find<T>(String(value)))
                return local_value;
        }
    }
    return root ? root->find<T>(String(value)) : nullptr;
}

template <typename T>
std::vector<std::shared_ptr<T>> allVars(const FlagDirPtr& root,
                                        const UseCounterPtr& counter) {
    std::vector<std::shared_ptr<T>> result;
    if (counter) {
        const auto owner = counter->owner();
        if (const auto local_owner = std::dynamic_pointer_cast<LocalVariableOwner>(owner)) {
            if (const auto local_root = local_owner->flagDirRoot()) {
                if constexpr (std::is_same_v<T, Flag>) result = local_root->allFlags();
                else if constexpr (std::is_same_v<T, Step>) result = local_root->allSteps();
                else if constexpr (std::is_same_v<T, Variant>) result = local_root->allVariants();
            }
        }
    }
    if (root) {
        std::vector<std::shared_ptr<T>> global;
        if constexpr (std::is_same_v<T, Flag>) global = root->allFlags();
        else if constexpr (std::is_same_v<T, Step>) global = root->allSteps();
        else if constexpr (std::is_same_v<T, Variant>) global = root->allVariants();
        result.insert(result.end(), global.begin(), global.end());
    }
    return result;
}

}  // namespace cwxeditor::cwx::flag
