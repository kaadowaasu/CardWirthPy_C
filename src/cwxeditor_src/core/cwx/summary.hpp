#pragma once

// Summary
// はエリア等の所
// 有者インターフ
// ェースを実装す
// る。必要な型の
// 宣言は対応する
// モデルヘッダー
// から取り込む。

#include "cwxeditor_src/core/cwx/archive.hpp"
#include "cwxeditor_src/core/cwx/area.hpp"
#include "cwxeditor_src/core/cwx/cab.hpp"
#include "cwxeditor_src/core/cwx/card.hpp"
#include "cwxeditor_src/core/cwx/flag.hpp"
#include "cwxeditor_src/core/cwx/filesync.hpp"
#include "cwxeditor_src/core/cwx/jpy.hpp"
#include "cwxeditor_src/core/cwx/path.hpp"
#include "cwxeditor_src/core/cwx/props.hpp"
#include "cwxeditor_src/core/cwx/skin.hpp"
#include "cwxeditor_src/core/cwx/structs.hpp"
#include "cwxeditor_src/core/cwx/system.hpp"
#include "cwxeditor_src/core/cwx/utils.hpp"
#include "cwxeditor_src/core/cwx/versioninfo.hpp"
#include "cwxeditor_src/core/cwx/xml.hpp"
#include "cwxeditor_src/lib/portable.hpp"
#include "cwxeditor_src/lib/array_slice.hpp"
#include "platform/storage/storage.hpp"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <memory>
#include <limits>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cwxeditor::cwx::summary {

using String = lib::String;
using StringView = lib::StringView;
using Path = std::filesystem::path;
using XNode = xml::XNode;
using System = system::System;
using XMLInfo = system::XMLInfo;
using CProps = props::CProps;
using Skin = skin::Skin;
using FileSync = filesync::FileSync;
using CardImage = card::CardImage;
using CastCard = card::CastCard;
using SkillCard = card::SkillCard;
using ItemCard = card::ItemCard;
using BeastCard = card::BeastCard;
using InfoCard = card::InfoCard;
using Area = area::Area;
using Battle = area::Battle;
using Package = area::Package;
using AbstractArea = area::AbstractArea;
using FlagDir = flag::FlagDir;
using UseCounter = usecounter::UseCounter;
using Jpy1 = jpy::Jpy1;
using Jpdc = jpy::Jpdc;
using BgImageS = structs::BgImageS;
using ClassicEngine = structs::ClassicEngine;
using EvTemplate = structs::EvTemplate;
using Storage = ::cardwirth::platform::Storage;

/// シナリオを読み込めない、またはSummaryではない場合の例外。
class SummaryException : public std::runtime_error {
public:
    explicit SummaryException(String message)
        : std::runtime_error(std::move(message)) {}
};

inline constexpr StringView PATH_AREA = "Area";
inline constexpr StringView PATH_PACKAGE = "Package";
inline constexpr StringView PATH_BATTLE = "Battle";
inline constexpr StringView PATH_CAST = "CastCard";
inline constexpr StringView PATH_ITEM = "ItemCard";
inline constexpr StringView PATH_SKILL = "SkillCard";
inline constexpr StringView PATH_BEAST = "BeastCard";
inline constexpr StringView PATH_INFO = "InfoCard";

class ScenarioLoadCancelled final : public std::exception {
public:
    const char* what() const noexcept override { return "scenario load cancelled"; }
};

struct LoadOption {
    bool doubleIO = false;
    bool cardOnly = false;
    bool textOnly = false;
    bool expandXMLs = true;
    bool summaryOnly = false;
    lib::Callback<void(String, String)> processFunc;
    // メンバーの読み込みで致命的でない失敗が起きたときの、任意
    // の診断通知先。 Dと同じくパスを通知して続行し、C++で
    // は捕捉した例外の本文も保持して省略理由を警告に含める。
    lib::Callback<void(String, String)> issueFunc;
    std::uint32_t numberStepToVariantThreshold = 0;
    lib::Callback<bool()> cancelled;
    [[nodiscard]] bool isCancelled() const { return cancelled && cancelled(); }
    void checkCancelled() const { if (isCancelled()) throw ScenarioLoadCancelled{}; }
    // 診断情報の通知用。D版のメンバーコールバックの動作は変えない。
    lib::Callback<void(String, String)> progressDetail;
    void reportProgress(String stage, String path) const {
        checkCancelled();
        if (progressDetail) progressDetail(std::move(stage), std::move(path));
    }
};

struct SaveOption {
    bool doubleIO = false;
    bool saveInnerImagePath = false;
    bool logicalSort = false;
    bool saveChangedOnly = false;
    bool backup = false;
    String backupDir;
    bool archiveInNewThread = false;
    bool xmlFileNameIsIDOnly = false;
    bool autoUpdateJpy1File = false;
    bool saveSkinName = true;
    std::uint32_t dataVersion = 4;
    lib::Callback<void()> savedCallback;
    bool replaceClassicResourceExtension = false;
    String tableExtension = ".bmp";
    String midiExtension = ".mid";
    String waveExtension = ".wav";
    // 実行側は、戻る場合も例外を投げる場合も、分離したI/Oを完了・joinさせ
    // る。 保存形式への変換とモデルの変更は、常に呼び出し元のスレッドで行う。
    lib::Callback<void(lib::Callback<void()>)> ioExecutor;
    // MainWindowのホスト側の保存処理は、永続化の確認まで保存地点を保持す
    // る。 Summaryを直接使う呼び出し元では、従来のリセット動作を維持する。
    bool deferChangedReset = false;
    // 名前を付けて保存する際の変換では、バイト列化の前にモデルの
    // パスを変える場合がある。 ここで識別するのは今から書く内容
    // の版であり、I/Oや永続化の後に見えた新しい版ではない。
    lib::Callback<void()> beforeSerialization;
    void runIo(lib::Callback<void()> work) const {
        if (ioExecutor) ioExecutor(std::move(work));
        else if (work) work();
    }
    void runArchive(lib::Callback<void()> work) const {
        if (archiveInNewThread) runIo(std::move(work));
        else if (work) work();
    }
};

using AreaList = lib::Array<lib::Owner<Area>>;
using BattleList = lib::Array<lib::Owner<Battle>>;
using PackageList = lib::Array<lib::Owner<Package>>;
// 読み込みの準備用にはネイティブのvectorも残す。 Summary
// の正式なカード格納先は、下のSharedSliceフィールドとする。
using CastList = lib::Array<lib::Owner<CastCard>>;
using SkillList = lib::Array<lib::Owner<SkillCard>>;
using ItemList = lib::Array<lib::Owner<ItemCard>>;
using BeastList = lib::Array<lib::Owner<BeastCard>>;
using InfoList = lib::Array<lib::Owner<InfoCard>>;
using XmlFileMap = lib::OrderedDictionary<String, String>;
using XmlFileTree = lib::OrderedDictionary<String, XmlFileMap>;
using ChangedResourceSet = std::unordered_set<const path::CWXPath*>;

/// シナリオのSummaryと、カード資源が使う所有者のインターフェース。
class Summary final : public area::AreaOwner,
                      public area::BattleOwner,
                      public area::PackageOwner,
                      public card::CastOwner,
                      public card::SkillOwner,
                      public card::ItemOwner,
                      public card::BeastOwner,
                      public card::InfoOwner,
                      public std::enable_shared_from_this<Summary> {
public:
    using ChangeHandler = lib::Callback<void()>;
    using Owner = lib::Owner<Summary>;

    explicit Summary(String sPath = {});
    Summary(String sname, String type, String skinName, String sPath,
            bool temp, bool legacy, Storage* storage = nullptr);
    // DのSummaryは固有の識別を持つクラス参照。ネイティブの値コピー
    // では 借用アドレスの代理参照が共有され、別のモデルの寿命トークンを
    // 無効化するため、 明示的なモデルのコピー・再読み込みAPIを使う。
    Summary(const Summary&) = delete;
    Summary& operator=(const Summary&) = delete;
    Summary(Summary&&) = delete;
    Summary& operator=(Summary&&) = delete;
    ~Summary() override;

    [[nodiscard]] lib::Owner<path::CWXPath> retainForCallback() override {
        // 公開するパスハンドルは、Summaryの本来の制御ブロックを使う。
        // スタック上のSummaryには、共有の親として表せるハンドルはない。
        const auto self = weak_from_this().lock();
        return self ? lib::Owner<path::CWXPath>(self,
            static_cast<path::CWXPath*>(static_cast<card::CastOwner*>(this)))
            : lib::Owner<path::CWXPath>{};
    }

    [[nodiscard]] bool expandXMLs() const noexcept { return expand_xmls_; }
    [[nodiscard]] bool useTemp() const noexcept { return use_temp_; }
    [[nodiscard]] const String& zipName() const noexcept { return zip_name_; }
    void zipName(String value) { zip_name_ = std::move(value); }
    [[nodiscard]] const String& origZipName() const noexcept { return orig_zip_name_; }
    [[nodiscard]] const String& readOnlyPath() const noexcept { return read_only_path_; }
    [[nodiscard]] bool legacy() const noexcept { return legacy_; }
    [[nodiscard]] bool toWsnProcessing() const noexcept { return to_x_; }

    static String createTempDirFromName(StringView tempPath, StringView name);
    static String createTempDir(StringView tempPath, StringView name,
                                bool createLockFile = true,
                                Storage* storage = nullptr);
    static void createLockFile(StringView temp, Storage* storage = nullptr);

    static Owner createScenario(const CProps& prop, StringView tempPath,
                                StringView name, const Skin& skin,
                                bool createStartArea, StringView newAreaName,
                                const lib::Array<BgImageS>& bgImagesDefault,
                                bool saveSkinName, FileSync& sync);
    static void createStartArea(
        Summary& summ, const CProps& prop, bool createStartArea,
        StringView newAreaName, const lib::Array<BgImageS>& bgImagesDefault,
        const Skin* skin = nullptr, StringView enginePath = {},
        StringView classicEngineRegex = {}, StringView classicDataDirRegex = {},
        StringView classicMatchKey = {},
        const lib::Array<ClassicEngine>& classicEngines = {},
        StringView defaultSkin = {});

    using ClassicDirCallback = lib::Callback<String()>;
    using ProgressCallback = lib::Callback<void(std::uint32_t)>;
    static Owner loadScenarioFromFile(
        const CProps& prop, const LoadOption& opt,
        lib::Array<String>& errorFiles, StringView fname, StringView tempPath,
        const Skin* defSkin = nullptr, ClassicDirCallback classicDir = {},
        const Owner& old = {}, ProgressCallback setMax = {},
        ProgressCallback worked = {}, StringView newName = {},
        Storage* storage = nullptr);

    void delTemp(bool fileOnly = false);
    /// 異常終了時に完全な復元スナップショットを作れな
    /// かった場合、 アーカイブの作業ツリーを残す。
    [[nodiscard]] String retainTempForRecovery() noexcept;

    /// シナリオの変更通知。最初の一覧は変更状態への遷移時に、
    /// 2番目はDのデリゲートと同じく、すべての変更で呼ぶ。
    std::vector<ChangeHandler> changedEvent;
    std::vector<ChangeHandler> changedEventForce;
    // 通知中に接続先を変えられる、安定した解除可能な監視登録
    // 。 返された購読を保持する間は有効とし、解放時に登録を
    // 外す。 実行中のコールバックやその状態は無効化しない。
    using ChangeSubscription = std::shared_ptr<const int>;
    [[nodiscard]] ChangeSubscription observeChanges(ChangeHandler callback);
private:
    struct ChangeObserver {
        std::weak_ptr<const int> subscription;
        std::shared_ptr<ChangeHandler> callback;
    };
    std::vector<ChangeObserver> change_observers_;
public:

    [[nodiscard]] String cwxPath(bool id) override;
    [[nodiscard]] lib::Owner<path::CWXPath> findCWXPath(StringView value) override;
    [[nodiscard]] lib::Array<lib::Observer<path::CWXPath>> cwxChilds() override;
    [[nodiscard]] lib::Observer<path::CWXPath> cwxParent() override;

    [[nodiscard]] const String& id() const noexcept { return id_; }
    [[nodiscard]] bool isChanged() const noexcept { return changed_; }
    void resetChanged();
    void changed() override;
    void changedAll();
    [[nodiscard]] ChangedResourceSet changedResources() const;
    [[nodiscard]] bool isIncluded(StringView value) const;
    [[nodiscard]] lib::Owner<path::CWXPath> isIncludedImpl(StringView value) const;
    [[nodiscard]] lib::Owner<UseCounter> useCounter() const noexcept { return use_counter_; }

    [[nodiscard]] bool isSystemFile(StringView value) const;
    [[nodiscard]] bool isSystemFile(StringView value, bool isDirectory) const;
    [[nodiscard]] lib::OrderedDictionary<String, std::filesystem::file_time_type>
    allPaths() const;
    void checkPathsIsChanged(bool saveInnerImagePath);

    [[nodiscard]] const String& dataVersion() const noexcept { return data_version_; }
    void dataVersion(String value);
    [[nodiscard]] bool loadScaledImage() const noexcept { return load_scaled_image_; }
    void loadScaledImage(bool value);
    [[nodiscard]] bool isTargetVersion(StringView value) const;
    static bool isTargetVersionWith(bool legacy, StringView dataVersion,
                                    StringView value);

    void setBaseParams(String newScenarioName, String newAuthor);
    [[nodiscard]] const String& author() const noexcept { return author_; }
    void author(String value);
    [[nodiscard]] const String& type() const noexcept { return type_; }
    void type(String value);
    [[nodiscard]] const String& skinName() const noexcept { return skin_name_; }
    void skinName(String value);
    [[nodiscard]] lib::Array<lib::Owner<CardImage>> imagePaths() const;
    void imagePaths(const lib::Array<lib::Owner<CardImage>>& paths);
    void desc(String value);
    [[nodiscard]] const String& desc() const noexcept { return description_; }
    void levelMin(std::uint32_t value);
    [[nodiscard]] std::uint32_t levelMin() const noexcept { return level_min_; }
    void levelMax(std::uint32_t value);
    [[nodiscard]] std::uint32_t levelMax() const noexcept { return level_max_; }
    void rCouponNum(std::uint32_t value);
    [[nodiscard]] std::uint32_t rCouponNum() const noexcept { return required_coupon_num_; }
    void rCoupons(lib::SharedSlice<String> values);
    // ネイティブのvector/list変換も、Dの記述子を設定する規則を共用する。
    void rCoupons(lib::Array<String> values) {
        rCoupons(lib::SharedSlice<String>(std::move(values)));
    }
    void rCoupons(std::initializer_list<String> values) {
        rCoupons(lib::SharedSlice<String>(values));
    }
    [[nodiscard]] lib::Array<String> rCoupons() const;
    void startArea(std::uint64_t value);
    [[nodiscard]] std::uint64_t startArea() const noexcept;

    // Dは同じ格納先を指す独立した記述子を返す。 これら三つの取得関数はinout
    // ではないが、ネイティブのconst版の要素は読み取り専用に保つ。
    [[nodiscard]] lib::SharedSlice<lib::Owner<Area>> areas() noexcept override { return areas_; }
    [[nodiscard]] lib::SharedSlice<const lib::Owner<Area>> areas() const noexcept override { return areas_; }
    [[nodiscard]] lib::SharedSlice<lib::Owner<Package>> packages() noexcept override { return packages_; }
    [[nodiscard]] lib::SharedSlice<const lib::Owner<Package>> packages() const noexcept override { return packages_; }
    [[nodiscard]] lib::SharedSlice<lib::Owner<Battle>> battles() noexcept override { return battles_; }
    [[nodiscard]] lib::SharedSlice<const lib::Owner<Battle>> battles() const noexcept override { return battles_; }
    [[nodiscard]] lib::SharedSlice<lib::Owner<CastCard>> casts() override { return casts_; }
    [[nodiscard]] lib::ConstOwnerSlice<CastCard> casts() const override { return lib::ConstOwnerSlice<CastCard>(casts_); }
    [[nodiscard]] lib::SharedSlice<lib::Owner<SkillCard>> skills() override { return skills_; }
    [[nodiscard]] lib::ConstOwnerSlice<SkillCard> skills() const override { return lib::ConstOwnerSlice<SkillCard>(skills_); }
    [[nodiscard]] lib::SharedSlice<lib::Owner<ItemCard>> items() override { return items_; }
    [[nodiscard]] lib::ConstOwnerSlice<ItemCard> items() const override { return lib::ConstOwnerSlice<ItemCard>(items_); }
    [[nodiscard]] lib::SharedSlice<lib::Owner<BeastCard>> beasts() override { return beasts_; }
    [[nodiscard]] lib::ConstOwnerSlice<BeastCard> beasts() const override { return lib::ConstOwnerSlice<BeastCard>(beasts_); }
    [[nodiscard]] lib::SharedSlice<lib::Owner<InfoCard>> infos() override { return infos_; }
    [[nodiscard]] lib::ConstOwnerSlice<InfoCard> infos() const override { return lib::ConstOwnerSlice<InfoCard>(infos_); }
    [[nodiscard]] lib::Owner<CastCard> cwCast(std::uint64_t id) override;
    [[nodiscard]] lib::Owner<SkillCard> skill(std::uint64_t id) override;
    [[nodiscard]] lib::Owner<ItemCard> item(std::uint64_t id) override;
    [[nodiscard]] lib::Owner<BeastCard> beast(std::uint64_t id) override;
    [[nodiscard]] lib::Owner<InfoCard> info(std::uint64_t id) override;
    [[nodiscard]] lib::Owner<const CastCard> cwCast(std::uint64_t id) const override;
    [[nodiscard]] lib::Owner<const SkillCard> skill(std::uint64_t id) const override;
    [[nodiscard]] lib::Owner<const ItemCard> item(std::uint64_t id) const override;
    [[nodiscard]] lib::Owner<const BeastCard> beast(std::uint64_t id) const override;
    [[nodiscard]] lib::Owner<const InfoCard> info(std::uint64_t id) const override;

    template <typename T>
    requires (!std::is_const_v<T>)
    [[nodiscard]] lib::Owner<T> baseCard(lib::Owner<T> value) {
        return baseCardImpl(std::move(value), [this](std::uint64_t id) {
            using CardType = std::remove_const_t<T>;
            if constexpr (std::is_same_v<CardType, SkillCard>) return skill(id);
            else if constexpr (std::is_same_v<CardType, ItemCard>) return item(id);
            else return beast(id);
        });
    }

    template <typename T>
    [[nodiscard]] lib::Owner<const T> baseCard(lib::Owner<T> value) const {
        return baseCardImpl(lib::Owner<const T>(std::move(value)), [this](std::uint64_t id) {
            using CardType = std::remove_const_t<T>;
            if constexpr (std::is_same_v<CardType, SkillCard>) return skill(id);
            else if constexpr (std::is_same_v<CardType, ItemCard>) return item(id);
            else return beast(id);
        });
    }

    [[nodiscard]] lib::Owner<Area> area(std::uint64_t id) const;
    [[nodiscard]] lib::Owner<Battle> battle(std::uint64_t id) const;
    [[nodiscard]] lib::Owner<Package> cwPackage(std::uint64_t id) const;
    [[nodiscard]] bool hasAreaId(std::uint64_t id) const;
    [[nodiscard]] bool hasBattleId(std::uint64_t id) const;
    [[nodiscard]] bool hasPackageId(std::uint64_t id) const;
    [[nodiscard]] lib::Owner<BeastCard> findSameBeast(
        const lib::Owner<BeastCard>& value) const;
    [[nodiscard]] lib::Owner<BeastCard> findSameBeast(
        const BeastCard& value) const;

    template <typename T>
    [[nodiscard]] std::uint64_t newId() const;
    [[nodiscard]] std::uint64_t newAreaId() const;
    [[nodiscard]] std::uint64_t newBattleId() const;
    [[nodiscard]] std::uint64_t newPackageId() const;

    template <typename T>
    [[nodiscard]] std::ptrdiff_t indexOf(const lib::Owner<T>& value) const;

    std::uint64_t insert(std::ptrdiff_t index, const lib::Owner<CastCard>& value);
    std::uint64_t insert(std::ptrdiff_t index, const lib::Owner<SkillCard>& value);
    std::uint64_t insert(std::ptrdiff_t index, const lib::Owner<ItemCard>& value);
    std::uint64_t insert(std::ptrdiff_t index, const lib::Owner<BeastCard>& value);
    std::uint64_t insert(std::ptrdiff_t index, const lib::Owner<InfoCard>& value);
    std::uint64_t insert(std::ptrdiff_t index, const lib::Owner<Area>& value);
    std::uint64_t insert(std::ptrdiff_t index, const lib::Owner<Battle>& value);
    std::uint64_t insert(std::ptrdiff_t index, const lib::Owner<Package>& value);

    std::uint64_t add(const lib::Owner<Area>& value, bool forceNewId = true);
    std::uint64_t add(const lib::Owner<Battle>& value, bool forceNewId = true);
    std::uint64_t add(const lib::Owner<Package>& value, bool forceNewId = true);
    std::uint64_t add(const lib::Owner<CastCard>& value, bool forceNewId = true);
    std::uint64_t add(const lib::Owner<SkillCard>& value, bool forceNewId = true);
    std::uint64_t add(const lib::Owner<ItemCard>& value, bool forceNewId = true);
    std::uint64_t add(const lib::Owner<BeastCard>& value, bool forceNewId = true);
    std::uint64_t add(const lib::Owner<InfoCard>& value, bool forceNewId = true);

    void remove(const lib::Owner<CastCard>& value);
    void remove(const lib::Owner<SkillCard>& value);
    void remove(const lib::Owner<ItemCard>& value);
    void remove(const lib::Owner<BeastCard>& value);
    void remove(const lib::Owner<InfoCard>& value);
    void remove(const lib::Owner<Area>& value);
    void remove(const lib::Owner<Battle>& value);
    void remove(const lib::Owner<Package>& value);
    void remove(const lib::Owner<AbstractArea>& value);

    template <typename T>
    void swap(std::ptrdiff_t first, std::ptrdiff_t second);
    void swapCast(std::ptrdiff_t first, std::ptrdiff_t second) { swap<CastCard>(first, second); }
    void swapSkill(std::ptrdiff_t first, std::ptrdiff_t second) { swap<SkillCard>(first, second); }
    void swapItem(std::ptrdiff_t first, std::ptrdiff_t second) { swap<ItemCard>(first, second); }
    void swapBeast(std::ptrdiff_t first, std::ptrdiff_t second) { swap<BeastCard>(first, second); }
    void swapInfo(std::ptrdiff_t first, std::ptrdiff_t second) { swap<InfoCard>(first, second); }
    void swapArea(std::ptrdiff_t first, std::ptrdiff_t second) { swap<Area>(first, second); }
    void swapBattle(std::ptrdiff_t first, std::ptrdiff_t second) { swap<Battle>(first, second); }
    void swapPackage(std::ptrdiff_t first, std::ptrdiff_t second) { swap<Package>(first, second); }

    void eventTemplates(lib::SharedSlice<EvTemplate> value);
    [[nodiscard]] lib::SharedSlice<EvTemplate> eventTemplates() noexcept {
        return event_templates_;
    }
    [[nodiscard]] lib::SharedSlice<const EvTemplate> eventTemplates() const noexcept {
        return event_templates_;
    }
    void useKeyInAction(bool value);
    [[nodiscard]] bool useKeyInAction() const noexcept { return use_key_in_action_; }
    void noConsumeUnmatchedMenuCard(bool value);
    [[nodiscard]] bool noConsumeUnmatchedMenuCard() const noexcept { return no_consume_unmatched_menu_card_; }
    void numericKeycodes(bool value);
    [[nodiscard]] bool numericKeycodes() const noexcept { return numeric_keycodes_; }

    [[nodiscard]] String summaryToXML(const card::XMLOption& option) const;
    [[nodiscard]] XmlFileTree toXMLs(const System& sys,
                                     const SaveOption& saveOpt) const;
    void saveXMLs(StringView path, const System& sys, const SaveOption& opt,
                  FileSync& sync);
    void saveXMLs(const System& sys, const SaveOption& opt, FileSync& sync);
    static Owner summaryFromXML(const System& sys, StringView sPath,
                                 StringView xml);
    void checkStartArea();
    static Owner fromXMLs(const CProps& prop, StringView sPath,
                          const XmlFileTree& xmls, const LoadOption& opt,
                          lib::Array<String>& errorFiles);
    static void fromXMLs(const CProps& prop, const Owner& summ,
                         const LoadOption& opt, lib::Array<String>& errorFiles);
    static Owner fromXMLs(const CProps& prop, StringView summaryPath,
                          const LoadOption& opt,
                          lib::Array<String>& errorFiles);
    Owner reloadXMLs(const CProps& prop, const LoadOption& opt,
                     lib::Array<String>& errorFiles) const;
    using ReloadOperation = lib::Callback<Owner(const CProps&, const LoadOption&, lib::Array<String>&)>;
    // 再読み込み用の識別情報だけを取得し、実行中の編集可能なカード・エリアの構造は共有しない。
    [[nodiscard]] ReloadOperation prepareReload() const;

    [[nodiscard]] lib::Owner<FlagDir> flagDirRoot() const noexcept { return flag_root_; }
    [[nodiscard]] const String& scenarioPath() const noexcept { return scenario_path_; }
    void scenarioPath(String value);
    // 一時シナリオ・バックアップ・生成XMLに使う製品のStorage
    // を接続する。 配下のパスにはStorageのルート保護付き操作
    // を使い、外側では従来のネイティブの「名前を付けて保存」を保つ。
    void setStorage(Storage* storage) noexcept;
    [[nodiscard]] Storage* storage() const noexcept { return storage_; }
    [[nodiscard]] const String& scenarioName() const noexcept { return scenario_name_; }
    void scenarioName(String value);

    [[nodiscard]] String classicToX(const CProps& prop, StringView temp,
                                    StringView tempPath, const Skin& toSkin,
                                    lib::Array<String>& copyFail);
    [[nodiscard]] bool isSaved() const noexcept;
    void saveOverwrite(const CProps& prop, const Skin& skin,
                       const SaveOption& opt, FileSync& sync);
    struct SaveAsCheckpoint {
        String scenarioPath;
        String tempPath;
        String zipName;
        String originalZipName;
        bool useTemp{};
        bool legacy{};
        bool expandXmls{};
        bool convertingToWsn{};
        bool lockOpen{};
        std::shared_ptr<void> tempLifetime;
        XmlFileTree retainedXmls;
        String readOnlyPath;
        String type;
        String skinName;
        String dataVersion;
        struct JpySectionState { String filename; bool needUpdate{}; };
        lib::Array<lib::Array<JpySectionState>> jpySectionStates;
        lib::Array<jpy::Jpy1> jpyData;
        lib::Array<lib::Owner<jpy::Jpdc>> jpdcData;
    };
    [[nodiscard]] SaveAsCheckpoint checkpointSaveAs() const;
    void rollbackSaveAs(const SaveAsCheckpoint& checkpoint);
    void commitSaveAs(const SaveAsCheckpoint& checkpoint) noexcept;
    void saveWithName(const CProps& prop, const Skin& skin,
                      const SaveOption& opt, StringView fname,
                      StringView tempPath, bool defExpandXMLs,
                      const Skin* defSkin, lib::Callback<void(String)> showWarn,
                      bool classic, FileSync& sync);
    void exportClassicScenario(const CProps& prop, const Skin& skin,
                               const SaveOption& opt, StringView fname,
                               StringView tempPath,
                               lib::Callback<void(String)> showWarn,
                               FileSync& sync);
    [[nodiscard]] archive::ZipArchive createZipData(
        const lib::Array<String>& ignorePaths, bool useSysEnc, bool isWsn,
        lib::Array<lib::Bytes>& data, FileSync& sync);
    void createZip(StringView zipName, const lib::Array<String>& ignorePaths,
                   bool useSysEnc, FileSync& sync, const SaveOption& option = {});
    void createCab(StringView cabName, const lib::Array<String>& ignorePaths,
                   const SaveOption& option = {});
    [[nodiscard]] lib::Array<String> notUsedFiles(
        const Skin& skin, const lib::Array<String>& ignorePaths,
        bool logicalSort) const;
    [[nodiscard]] bool hasMaterial(StringView value,
                                    const lib::Array<String>& ignorePaths) const;
    [[nodiscard]] lib::Array<String> allMaterials(
        const Skin& skin, const lib::Array<String>& ignorePaths,
        bool logicalSort, bool scenarioOnly) const;
    void updateJpy1List(const CProps& prop, const LoadOption* option = nullptr);
    bool updateJpy1Files(const CProps& prop, bool autoUpdateJpy1File,
                         FileSync& sync);
    void renameFile(StringView from, StringView to);
    void attachCard(const lib::Owner<card::Card>& value);
    void detachCard(const lib::Owner<card::Card>& value);
    void attachArea(const lib::Owner<Area>& value);
    void attachBattle(const lib::Owner<Battle>& value);
    void attachPackage(const lib::Owner<Package>& value);
    [[nodiscard]] ChangeHandler guardedChangeHandler();
    void detachArea(const lib::Owner<Area>& value);
    void detachBattle(const lib::Owner<Battle>& value);
    void detachPackage(const lib::Owner<Package>& value);

private:
    template <typename T, typename Lookup>
    [[nodiscard]] static lib::Owner<T> baseCardImpl(lib::Owner<T> value,
                                                    Lookup&& lookup) {
        using CardType = std::remove_const_t<T>;
        static_assert(std::is_same_v<CardType, SkillCard> ||
                      std::is_same_v<CardType, ItemCard> ||
                      std::is_same_v<CardType, BeastCard>,
                      "baseCard requires a reference-capable card");
        // Dはクラス引数を値で保持し、nullでも参照をたどる。 null
        // の結果を返して省略せず、ネイティブ側でも失敗にする。
        if (!value) throw std::invalid_argument("null base card");
        if (value->linkId() != 0) {
            const auto replacement = lookup(value->linkId());
            if (replacement) return replacement;
        }
        return value;
    }

    struct Lock {
        std::unique_ptr<std::ofstream> stream;
        void close() noexcept;
        [[nodiscard]] bool open() const noexcept { return stream != nullptr; }
    };
    struct TempLifetime;
    struct SaveOperationScope {
        explicit SaveOperationScope(Summary& owner);
        ~SaveOperationScope();
        SaveOperationScope(const SaveOperationScope&) = delete;
        SaveOperationScope& operator=(const SaveOperationScope&) = delete;
        Summary& owner;
    };
    struct XmlSaveScope {
        explicit XmlSaveScope(Summary& owner);
        ~XmlSaveScope();
        void release();
        XmlSaveScope(const XmlSaveScope&) = delete;
        XmlSaveScope& operator=(const XmlSaveScope&) = delete;
        Summary& owner;
        bool active{true};
    };

    void changeHandler();
    void activateTempLifetime(bool createLock = true);
    void refCheckPaths();
    void saveXMLsImpl(StringView path, const System& sys,
                      const SaveOption& opt, FileSync& sync);
    friend struct SummaryTestAccessor;
    void repairId0();
    [[nodiscard]] bool needCheckPaths() const noexcept { return true; }
    void toArchive(String zipName, String scenarioPath, bool expandXMLs);
    static String fileNameFor(std::uint64_t id, StringView name,
                              const SaveOption& option);
    void saveXMLSet(const Path& directory, StringView category,
                    const SaveOption& option, const card::XMLOption& xopt,
                    const ChangedResourceSet* changed, FileSync& sync,
                    lib::Array<Path>& staleFiles);
    [[nodiscard]] std::vector<lib::Owner<path::CWXPath>> cardChildren() const;

    String id_;
    bool expand_xmls_{};
    bool use_temp_{true};
    std::shared_ptr<TempLifetime> temp_lifetime_;
    String zip_name_;
    String orig_zip_name_;
    String temp_path_;
    String read_only_path_;
    bool legacy_{};
    bool to_x_{};
    std::mutex save_state_mutex_;
    std::size_t save_state_depth_{};
    bool in_xml_save_{};
    bool deferred_temp_cleanup_{};
    lib::OrderedDictionary<String, std::filesystem::file_time_type> check_paths_;
    lib::OrderedDictionary<String, std::filesystem::file_time_type> no_save_check_paths_;
    lib::Array<Jpy1> jpy_data_;
    lib::Array<lib::Owner<Jpdc>> jpdc_data_;
    String scenario_path_;
    String scenario_name_;
    String author_;
    String description_;
    lib::Array<lib::Owner<CardImage>> image_paths_;
    std::uint32_t level_min_{};
    std::uint32_t level_max_{};
    std::uint32_t required_coupon_num_{};
    lib::Array<lib::Owner<usecounter::CouponUser>> required_coupons_;
    std::uint64_t start_area_id_{};
    String type_;
    String skin_name_;
    String data_version_ = versioninfo::DEFAULT_VERSION;
    bool load_scaled_image_{};
    bool use_key_in_action_{};
    bool no_consume_unmatched_menu_card_{};
    bool numeric_keycodes_{};
    lib::Owner<FlagDir> flag_root_;
    lib::Owner<UseCounter> use_counter_;
    lib::Owner<usecounter::AreaUser> start_area_user_;
    std::shared_ptr<path::OwnerLifetimeToken> owner_lifetime_{
        std::make_shared<path::OwnerLifetimeToken>()};
    lib::Owner<path::CWXPath> owner_proxy_;
    lib::SharedSlice<lib::Owner<Area>> areas_;
    lib::SharedSlice<lib::Owner<Package>> packages_;
    lib::SharedSlice<lib::Owner<Battle>> battles_;
    lib::SharedSlice<lib::Owner<CastCard>> casts_;
    lib::SharedSlice<lib::Owner<SkillCard>> skills_;
    lib::SharedSlice<lib::Owner<ItemCard>> items_;
    lib::SharedSlice<lib::Owner<BeastCard>> beasts_;
    lib::SharedSlice<lib::Owner<InfoCard>> infos_;
    lib::SharedSlice<EvTemplate> event_templates_;
    bool changed_{};
    Storage* storage_{};
    XmlFileTree old_xmls_;
};

class FileLoadException : public std::runtime_error {
public:
    FileLoadException(String path, std::exception_ptr error);
    FileLoadException(String path, const std::exception& error);
    [[nodiscard]] const String& path() const noexcept { return path_; }
    [[nodiscard]] const std::exception_ptr& e() const noexcept { return error_; }

private:
    String path_;
    std::exception_ptr error_;
};

[[nodiscard]] bool isScenarioSystemDir(StringView dir);
void decScenarioPath(String& scenarioPath, lib::Array<String>& openPaths,
                     bool eventPriority);
[[nodiscard]] lib::Owner<path::CWXPath> cwxPlace(
    lib::Owner<path::CWXPath> value);
[[nodiscard]] String pathUserToExportedImageName(
    const CProps& prop, StringView scenarioName, StringView author,
    const usecounter::PathUser& value);
[[nodiscard]] String toExportedImageNameWithoutCardName(
    const CProps& prop, StringView scenarioName, StringView author);
[[nodiscard]] String toExportedImageNameWithCardName(
    const CProps& prop, StringView scenarioName, StringView author,
    StringView cardName);
[[nodiscard]] XNode areasToNode(
    StringView parentPath, StringView cutPath,
    lib::SharedSlice<const lib::Owner<AbstractArea>> areas,
    const card::XMLOption& option, const Summary* summ = nullptr);
[[nodiscard]] XNode areasToNode(
    StringView parentPath, StringView cutPath,
    const lib::Array<lib::Owner<AbstractArea>>& areas,
    const card::XMLOption& option, const Summary* summ = nullptr);
[[nodiscard]] inline XNode areasToNode(
    StringView parentPath, StringView cutPath,
    std::initializer_list<lib::Owner<AbstractArea>> areas,
    const card::XMLOption& option, const Summary* summ = nullptr) {
    return areasToNode(parentPath, cutPath,
        lib::SharedSlice<const lib::Owner<AbstractArea>>(areas), option, summ);
}

template <typename T>
std::uint64_t Summary::newId() const {
    if constexpr (std::is_same_v<T, CastCard>) {
        return casts_.empty() ? 1U : casts_.back()->id() + 1U;
    } else if constexpr (std::is_same_v<T, SkillCard>) {
        return skills_.empty() ? 1U : skills_.back()->id() + 1U;
    } else if constexpr (std::is_same_v<T, ItemCard>) {
        return items_.empty() ? 1U : items_.back()->id() + 1U;
    } else if constexpr (std::is_same_v<T, BeastCard>) {
        return beasts_.empty() ? 1U : beasts_.back()->id() + 1U;
    } else if constexpr (std::is_same_v<T, InfoCard>) {
        return infos_.empty() ? 1U : infos_.back()->id() + 1U;
    } else if constexpr (std::is_same_v<T, Area>) {
        return newAreaId();
    } else if constexpr (std::is_same_v<T, Battle>) {
        return newBattleId();
    } else if constexpr (std::is_same_v<T, Package>) {
        return newPackageId();
    } else {
        static_assert(sizeof(T) == 0, "Summary::newId unsupported type");
    }
}

template <typename T>
std::ptrdiff_t Summary::indexOf(const lib::Owner<T>& value) const {
    const auto find = [&value](const auto& values) -> std::ptrdiff_t {
        for (std::ptrdiff_t i = 0; i < static_cast<std::ptrdiff_t>(values.size()); ++i) {
            if (values[static_cast<std::size_t>(i)].get() == value.get()) return i;
        }
        return -1;
    };
    using U = std::remove_const_t<T>;
    if constexpr (std::is_same_v<U, CastCard>) return find(casts_);
    else if constexpr (std::is_same_v<U, SkillCard>) return find(skills_);
    else if constexpr (std::is_same_v<U, ItemCard>) return find(items_);
    else if constexpr (std::is_same_v<U, BeastCard>) return find(beasts_);
    else if constexpr (std::is_same_v<U, InfoCard>) return find(infos_);
    else if constexpr (std::is_same_v<U, Area>) return find(areas_);
    else if constexpr (std::is_same_v<U, Battle>) return find(battles_);
    else if constexpr (std::is_same_v<U, Package>) return find(packages_);
    else {
        static_assert(sizeof(T) == 0, "Summary::indexOf unsupported type");
    }
}

template <typename T>
void Summary::swap(std::ptrdiff_t first, std::ptrdiff_t second) {
    if (first == second) return;
    if (first < 0 || second < 0) throw std::out_of_range("Summary::swap index");
    if constexpr (std::is_same_v<T, CastCard>) {
        if (static_cast<std::size_t>(first) >= casts_.size() || static_cast<std::size_t>(second) >= casts_.size())
            throw std::out_of_range("Summary::swap cast index");
        const auto id1 = casts_[first]->id(), id2 = casts_[second]->id();
        std::swap(casts_[first], casts_[second]);
        casts_[first]->id(id1); casts_[second]->id(id2);
        // 配置が入れ替わっても、元のオブジェクトへの参照を保つ。
        const auto temporary = usecounter::toCastId(std::numeric_limits<std::uint64_t>::max());
        use_counter_->change(usecounter::toCastId(id1), temporary);
        use_counter_->change(usecounter::toCastId(id2), usecounter::toCastId(id1));
        use_counter_->change(temporary, usecounter::toCastId(id2));
    } else if constexpr (std::is_same_v<T, SkillCard>) {
        if (static_cast<std::size_t>(first) >= skills_.size() || static_cast<std::size_t>(second) >= skills_.size())
            throw std::out_of_range("Summary::swap skill index");
        const auto id1 = skills_[first]->id(), id2 = skills_[second]->id();
        std::swap(skills_[first], skills_[second]);
        skills_[first]->id(id1); skills_[second]->id(id2);
        // 配置が入れ替わっても、元のオブジェクトへの参照を保つ。
        const auto temporary = usecounter::toSkillId(std::numeric_limits<std::uint64_t>::max());
        use_counter_->change(usecounter::toSkillId(id1), temporary);
        use_counter_->change(usecounter::toSkillId(id2), usecounter::toSkillId(id1));
        use_counter_->change(temporary, usecounter::toSkillId(id2));
    } else if constexpr (std::is_same_v<T, ItemCard>) {
        if (static_cast<std::size_t>(first) >= items_.size() || static_cast<std::size_t>(second) >= items_.size())
            throw std::out_of_range("Summary::swap item index");
        const auto id1 = items_[first]->id(), id2 = items_[second]->id();
        std::swap(items_[first], items_[second]);
        items_[first]->id(id1); items_[second]->id(id2);
        // 配置が入れ替わっても、元のオブジェクトへの参照を保つ。
        const auto temporary = usecounter::toItemId(std::numeric_limits<std::uint64_t>::max());
        use_counter_->change(usecounter::toItemId(id1), temporary);
        use_counter_->change(usecounter::toItemId(id2), usecounter::toItemId(id1));
        use_counter_->change(temporary, usecounter::toItemId(id2));
    } else if constexpr (std::is_same_v<T, BeastCard>) {
        if (static_cast<std::size_t>(first) >= beasts_.size() || static_cast<std::size_t>(second) >= beasts_.size())
            throw std::out_of_range("Summary::swap beast index");
        const auto id1 = beasts_[first]->id(), id2 = beasts_[second]->id();
        std::swap(beasts_[first], beasts_[second]);
        beasts_[first]->id(id1); beasts_[second]->id(id2);
        // 配置が入れ替わっても、元のオブジェクトへの参照を保つ。
        const auto temporary = usecounter::toBeastId(std::numeric_limits<std::uint64_t>::max());
        use_counter_->change(usecounter::toBeastId(id1), temporary);
        use_counter_->change(usecounter::toBeastId(id2), usecounter::toBeastId(id1));
        use_counter_->change(temporary, usecounter::toBeastId(id2));
    } else if constexpr (std::is_same_v<T, InfoCard>) {
        if (static_cast<std::size_t>(first) >= infos_.size() || static_cast<std::size_t>(second) >= infos_.size())
            throw std::out_of_range("Summary::swap info index");
        const auto id1 = infos_[first]->id(), id2 = infos_[second]->id();
        std::swap(infos_[first], infos_[second]);
        infos_[first]->id(id1); infos_[second]->id(id2);
        // 配置が入れ替わっても、元のオブジェクトへの参照を保つ。
        const auto temporary = usecounter::toInfoId(std::numeric_limits<std::uint64_t>::max());
        use_counter_->change(usecounter::toInfoId(id1), temporary);
        use_counter_->change(usecounter::toInfoId(id2), usecounter::toInfoId(id1));
        use_counter_->change(temporary, usecounter::toInfoId(id2));
    } else if constexpr (std::is_same_v<T, Area>) {
        if (static_cast<std::size_t>(first) >= areas_.size() || static_cast<std::size_t>(second) >= areas_.size())
            throw std::out_of_range("Summary::swap area index");
        const auto id1 = areas_[first]->id(), id2 = areas_[second]->id();
        std::swap(areas_[first], areas_[second]);
        areas_[first]->id(id1); areas_[second]->id(id2);
        // 配置が入れ替わっても、元のオブジェクトへの参照を保つ。
        const auto temporary = usecounter::toAreaId(std::numeric_limits<std::uint64_t>::max());
        use_counter_->change(usecounter::toAreaId(id1), temporary);
        use_counter_->change(usecounter::toAreaId(id2), usecounter::toAreaId(id1));
        use_counter_->change(temporary, usecounter::toAreaId(id2));
    } else if constexpr (std::is_same_v<T, Battle>) {
        if (static_cast<std::size_t>(first) >= battles_.size() || static_cast<std::size_t>(second) >= battles_.size())
            throw std::out_of_range("Summary::swap battle index");
        const auto id1 = battles_[first]->id(), id2 = battles_[second]->id();
        std::swap(battles_[first], battles_[second]);
        battles_[first]->id(id1); battles_[second]->id(id2);
        // 配置が入れ替わっても、元のオブジェクトへの参照を保つ。
        const auto temporary = usecounter::toBattleId(std::numeric_limits<std::uint64_t>::max());
        use_counter_->change(usecounter::toBattleId(id1), temporary);
        use_counter_->change(usecounter::toBattleId(id2), usecounter::toBattleId(id1));
        use_counter_->change(temporary, usecounter::toBattleId(id2));
    } else if constexpr (std::is_same_v<T, Package>) {
        if (static_cast<std::size_t>(first) >= packages_.size() || static_cast<std::size_t>(second) >= packages_.size())
            throw std::out_of_range("Summary::swap package index");
        const auto id1 = packages_[first]->id(), id2 = packages_[second]->id();
        std::swap(packages_[first], packages_[second]);
        packages_[first]->id(id1); packages_[second]->id(id2);
        // 配置が入れ替わっても、元のオブジェクトへの参照を保つ。
        const auto temporary = usecounter::toPackageId(std::numeric_limits<std::uint64_t>::max());
        use_counter_->change(usecounter::toPackageId(id1), temporary);
        use_counter_->change(usecounter::toPackageId(id2), usecounter::toPackageId(id1));
        use_counter_->change(temporary, usecounter::toPackageId(id2));
    } else {
        static_assert(sizeof(T) == 0, "Summary::swap unsupported type");
    }
    changed();
}

}  // namespace cwxeditor::cwx::summary
