#pragma once

// cwx/editor/gui/dwt/couponview.d
// のC++20移植。 四つのモードをテンプ
// レートで扱う公開形式を保ち、共通のモデル
// ・編集処理はCouponViewBase
// へまとめる。 表示にはTableModel
// を使い、シナリオ・XML・クリップボード
// の状態は既存のcore/libで扱う。

#include "cwxeditor_src/core/cwx/coupon.hpp"
#include "cwxeditor_src/core/cwx/features.hpp"
#include "cwxeditor_src/core/cwx/race.hpp"
#include "cwxeditor_src/core/cwx/summary.hpp"
#include "cwxeditor_src/core/cwx/system.hpp"
#include "cwxeditor_src/core/cwx/usecounter.hpp"
#include "cwxeditor_src/core/cwx/versioninfo.hpp"
#include "cwxeditor_src/core/cwx/warning.hpp"
#include "cwxeditor_src/core/cwx/utils.hpp"
#include "cwxeditor_src/core/cwx/props.hpp"
#include "cwxeditor_src/core/cwx/msgs.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/commons.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/customtable.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dprops.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/undo.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/xmlbytestransfer.hpp"
#include "cwxeditor_src/lib/components.hpp"
#include "cwxeditor_src/lib/drag_drop.hpp"
#include "cwxeditor_src/lib/events.hpp"
#include "cwxeditor_src/lib/xml.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace cwxeditor::cwx::editor::gui::dwt::couponview {

namespace lib = ::cwxeditor::lib;
namespace commons = ::cwxeditor::cwx::editor::gui::dwt::commons;
namespace customtable = ::cwxeditor::cwx::editor::gui::dwt::customtable;
namespace dprops = ::cwxeditor::cwx::editor::gui::dwt::dprops;
namespace undo = ::cwxeditor::cwx::editor::gui::dwt::undo;
namespace xmlbytestransfer =
    ::cwxeditor::cwx::editor::gui::dwt::xmlbytestransfer;
namespace coupon = ::cwxeditor::cwx::coupon;
namespace features = ::cwxeditor::cwx::features;
namespace race = ::cwxeditor::cwx::race;
namespace summary = ::cwxeditor::cwx::summary;
namespace system = ::cwxeditor::cwx::system;
namespace usecounter = ::cwxeditor::cwx::usecounter;
namespace warning = ::cwxeditor::cwx::warning;
namespace xml = ::cwxeditor::lib::xml;
namespace types = ::cwxeditor::cwx::types;

using String = lib::String;
using StringView = lib::StringView;
using Parent = std::shared_ptr<lib::Container>;
using PanelPtr = std::shared_ptr<lib::Panel>;
using SummaryPtr = summary::Summary::Owner;
using UseCounterPtr = usecounter::CounterOwner;
using Coupon = coupon::Coupon;
using CouponPtr = lib::Owner<Coupon>;
using CouponArray = std::vector<CouponPtr>;
using CouponType = features::CouponType;
using ResourceId = lib::ResourceId;
using Table = customtable::TableModel;
using TablePtr = customtable::TableModelPtr;
using NameEditor = std::shared_ptr<lib::ComboBox>;
using ValueEditor = std::shared_ptr<lib::NumberEdit>;
using FeaturePeriod = features::Period;

enum class CVType {
    Cast,
    Valued,
    NoValued,
    Branch,
};

struct FeatureInfo {
    FeaturePeriod period{};
    std::shared_ptr<const race::Race> race;
};
using FeatureProvider = std::function<FeatureInfo()>;
using ImageProvider = std::function<ResourceId(StringView, std::int32_t)>;

/// Dの各CouponViewモードで共用するモデル・ビューの実装。
class CouponViewBase {
public:
    using ModifyHandler = lib::Action;

    class UndoCoupons;
    class CDropListener;
    class CDragListener;
    class CouponTCPD;
    class SelCoupon;
    class HTBTraverse;
    class HTBKeyDown;
    class Dispose;
    class KeyDownFilter;

    CouponViewBase(CVType type, commons::Commons& comm, SummaryPtr summary,
                   UseCounterPtr useCounter, Parent parent = {},
                   std::uint32_t style = 0U,
                   std::function<bool()> catchMod = {},
                   FeatureProvider feature = {});
    virtual ~CouponViewBase();

    CouponViewBase(const CouponViewBase&) = delete;
    CouponViewBase& operator=(const CouponViewBase&) = delete;

    [[nodiscard]] CVType type() const noexcept { return type_; }
    [[nodiscard]] const String& id() const noexcept { return id_; }
    [[nodiscard]] bool readOnly() const noexcept { return read_only_; }
    void setReadOnly(bool value);
    [[nodiscard]] bool isHistoryView() const noexcept { return is_history_view_; }

    std::vector<ModifyHandler> modEvent;
    ModifyHandler modified;
    void addModifyListener(ModifyHandler handler);
    void raiseModifyEvent();

    void storeCoupons();
    bool undoCoupons();
    bool redoCoupons();
    [[nodiscard]] bool canUndo() const noexcept;
    [[nodiscard]] bool canRedo() const noexcept;

    [[nodiscard]] ResourceId couponImage(StringView name, std::int32_t value) const;
    [[nodiscard]] CouponPtr appendCoupon(const Coupon& value, int index = -1,
                                         bool select = true);
    [[nodiscard]] CouponPtr appendCoupon(const CouponPtr& value, int index = -1,
                                         bool select = true);
    void addCoupon();
    void altCoupon();
    void addCoupon(const Coupon& value);
    void addCoupon(const CouponPtr& value);
    void delCoupon();
    void delCoupon(const std::vector<int>& indices);
    void delCoupon(const std::vector<std::size_t>& indices);
    void selCoupon();
    void updateCouponType();
    void uppdateCouponTypeHint();

    [[nodiscard]] bool canUp() const;
    void upCoupon();
    [[nodiscard]] bool canDown() const;
    void downCoupon();
    [[nodiscard]] bool canAddInitialCoupons() const;
    void addInitialCoupons();
    void reverseCoupons();
    [[nodiscard]] bool canReverseSignOfValues() const;
    void reverseSignOfValues();

    class CDropListener {
    public:
        explicit CDropListener(CouponViewBase& owner) : owner_(&owner) {}
        void dragEnter(lib::DragEvent& event);
        void dragOver(lib::DragEvent& event);
        void drop(lib::DragEvent& event);

    private:
        CouponViewBase* owner_{};
    };

    [[nodiscard]] bool appendFromNode(xml::XNode& node, int index, bool move);
    [[nodiscard]] bool appendFromNode(const xml::XNode& node, int index, bool move);

    class CDragListener {
    public:
        explicit CDragListener(CouponViewBase& owner) : owner_(&owner) {}
        void dragStart(bool& doit) const;
        void dragSetData(lib::ClipboardData& data);
        void dragFinished(lib::DropOperation operation);

    private:
        CouponViewBase* owner_{};
    };

    class CouponTCPD {
    public:
        explicit CouponTCPD(CouponViewBase& owner) : owner_(&owner) {}
        void cut();
        void copy();
        void paste();
        void del();
        void clone();
        [[nodiscard]] bool canDoTCPD() const noexcept { return true; }
        [[nodiscard]] bool canDoT() const;
        [[nodiscard]] bool canDoC() const;
        [[nodiscard]] bool canDoP() const;
        [[nodiscard]] bool canDoD() const;
        [[nodiscard]] bool canDoClone() const;

    private:
        CouponViewBase* owner_{};
    };

    class SelCoupon {
    public:
        explicit SelCoupon(CouponViewBase& owner) : owner_(&owner) {}
        void widgetSelected(const std::vector<std::size_t>&) const;

    private:
        CouponViewBase* owner_{};
    };

    class HTBTraverse {
    public:
        void handleEvent(bool& doit) const noexcept { doit = true; }
    };
    class HTBKeyDown {
    public:
        void handleEvent(bool& doit) const noexcept { doit = true; }
    };

    CouponViewBase::Dispose* disposeListener() noexcept { return dispose_.get(); }

    class Dispose {
    public:
        explicit Dispose(CouponViewBase& owner) : owner_(&owner) {}
        void widgetDisposed() noexcept;

    private:
        CouponViewBase* owner_{};
    };

    class KeyDownFilter {
    public:
        explicit KeyDownFilter(CouponViewBase& owner) : owner_(&owner) {}
        void handleEvent(lib::KeyEvent& event);

    private:
        CouponViewBase* owner_{};
    };

    void refMenu(int id);
    void refUndoMax();

    [[nodiscard]] CouponArray coupons() const;
    void coupons(const CouponArray& values);
    [[nodiscard]] std::vector<String> couponNames() const;
    void couponNames(const std::vector<String>& values);

    void enabled(bool value);
    [[nodiscard]] bool enabled() const;
    void toolTip(String value);
    [[nodiscard]] String toolTip() const;
    // アイコンだけの操作ボタンを拡大する。ALLのセリフ画面では1.3倍にする。
    void toolIconScale(float scale);
    [[nodiscard]] TablePtr widget() const noexcept { return coupons_; }
    [[nodiscard]] PanelPtr panel() const noexcept { return panel_; }
    [[nodiscard]] NameEditor mainNameEditor() const noexcept { return new_coupon_; }
    [[nodiscard]] std::shared_ptr<lib::ComboBox> couponTypeEditor() const noexcept {
        return coupon_type_;
    }
    [[nodiscard]] ValueEditor valueEditor() const noexcept { return coupon_value_; }

    [[nodiscard]] NameEditor nameCreateEditor(std::size_t row, std::size_t column);
    [[nodiscard]] std::vector<String> nameCandidates() const;
    void refreshNameCandidates();
    [[nodiscard]] std::shared_ptr<lib::Menu> contextMenu();
    void setupNameEditor(std::function<void(const NameEditor&)> handler);
    void setupInlineNameEditor(std::function<void(const std::shared_ptr<lib::TextEdit>&)> handler);
    void nameEditEnd(std::size_t row, std::size_t column, String text);
    [[nodiscard]] ValueEditor valueCreateEditor(std::size_t row, std::size_t column);
    void valueEditEnd(std::size_t row, std::size_t column, std::int32_t value);
    [[nodiscard]] std::vector<String> getWarnings(std::size_t row) const;
    void updateWarning(std::size_t row);
    void refDataVersion();
    [[nodiscard]] std::vector<String> warnings() const;
    [[nodiscard]] std::vector<String> warningsImpl(StringView name) const;

    [[nodiscard]] bool canSelectAll() const;
    void selectAll();
    [[nodiscard]] bool select(int index);

    [[nodiscard]] bool canConvType(CouponType target) const;
    void convType(CouponType target);
    [[nodiscard]] bool canCopyWith() const;
    void copyWithType(CouponType target);

    void setFeatureProvider(FeatureProvider provider);
    void setImageProvider(ImageProvider provider);
    [[nodiscard]] FeatureProvider featureProvider() const { return feature_provider_; }

    [[nodiscard]] const std::vector<std::size_t>& selection() const noexcept;

protected:
    [[nodiscard]] const CouponArray& records() const noexcept { return records_; }
    [[nodiscard]] std::int32_t valueMinimum() const noexcept { return value_minimum_; }
    [[nodiscard]] std::int32_t valueMaximum() const noexcept { return value_maximum_; }

private:
    friend class UndoCoupons;
    friend class CDropListener;
    friend class CDragListener;
    friend class CouponTCPD;
    friend class SelCoupon;
    friend class Dispose;
    friend class KeyDownFilter;

    CVType type_;
    commons::Commons* comm_{};
    std::weak_ptr<const int> commons_lifetime_;
    dprops::Props* prop_{};
    SummaryPtr summary_;
    UseCounterPtr use_counter_;
    Parent parent_;
    PanelPtr panel_;
    TablePtr coupons_;
    std::shared_ptr<const int> menu_lifetime_{std::make_shared<const int>(0)};
    std::shared_ptr<lib::ToolBar> toolbar_;
    std::weak_ptr<lib::Panel> toolbar_row_;
    NameEditor new_coupon_;
    std::shared_ptr<lib::ComboBox> coupon_type_;
    ValueEditor coupon_value_;
    CouponArray records_;
    std::vector<std::size_t> selected_;
    bool read_only_{};
    bool enabled_{true};
    std::uint64_t render_revision_{};
    bool is_history_view_{};
    String id_;
    std::function<bool()> catch_mod_;
    FeatureProvider feature_provider_;
    ImageProvider image_provider_;
    std::function<void(const NameEditor&)> setup_name_editor_;
    std::function<void(const std::shared_ptr<lib::TextEdit>&)> setup_inline_name_editor_;
    std::weak_ptr<lib::TextEdit> active_inline_name_editor_;
    std::uint64_t inline_name_generation_{};
    std::shared_ptr<undo::UndoManager> undo_manager_;
    std::unique_ptr<Dispose> dispose_;
    std::unique_ptr<KeyDownFilter> key_filter_;
    std::unique_ptr<CDropListener> drop_listener_;
    std::unique_ptr<CDragListener> drag_listener_;
    std::unique_ptr<CouponTCPD> clipboard_actions_;
    std::map<CouponType, std::size_t> coupon_type_table_;
    std::map<std::size_t, CouponType> coupon_type_table_reverse_;
    std::map<String, bool> sex_coupons_;
    std::map<String, bool> period_coupons_;
    std::map<String, bool> nature_coupons_;
    std::map<String, bool> making_coupons_;
    std::int32_t value_minimum_{};
    std::int32_t value_maximum_{999};
    std::int32_t drag_index_{-1};
    std::vector<std::size_t> drag_rows_;
    CouponArray drag_coupons_;
    int undo_accelerator_{};
    int redo_accelerator_{};
    commons::Dlg<>::Connection ref_skin_connection_{};
    commons::Dlg<>::Connection ref_coupons_connection_{};
    commons::Dlg<>::Connection ref_undo_connection_{};
    commons::Dlg<>::Connection ref_version_connection_{};
    commons::Dlg<types::MenuID>::Connection ref_menu_connection_{};

    static std::atomic<std::uint64_t> next_id_;

    void createControl(std::uint32_t style);
    // 一つの共有の行内編集部品は表示側が所有する。 モデルのコマ
    // ンドは、この表についてDと同じ確定・中止だけを要求する。
    [[nodiscard]] bool finishInlineEdit(bool commit);
    [[nodiscard]] NameEditor createNameEditor(String initial, lib::Container* parent);
    void refreshSkinCoupons();
    void rebuildTable(const std::vector<std::size_t>& selection = {}, bool revealSelection = false);
    void replaceRecords(CouponArray values, const std::vector<std::size_t>& selection,
                        bool notify);
    [[nodiscard]] CouponPtr rowCoupon(std::size_t row) const;
    [[nodiscard]] bool hasName(StringView name, const Coupon* except = nullptr) const;
    [[nodiscard]] String uniqueName(String base, const Coupon* except = nullptr) const;
    [[nodiscard]] std::vector<std::size_t> normalizedSelection() const;
    [[nodiscard]] CouponArray selectedCoupons() const;
    [[nodiscard]] std::vector<std::size_t> snapshotSelection() const;
    [[nodiscard]] CouponArray snapshotCoupons() const;
    void applySnapshot(const CouponArray& values, const std::vector<std::size_t>& selection);
    void updateTypeItems();
    void refreshToolbarState();
    void notifyRefresh();
    void notifyChanged();
    void buildDragPayload(lib::ClipboardData& data);
    bool pasteXML(StringView text, bool clone);
    [[nodiscard]] String makeXML(const CouponArray& values, bool pane_id) const;
    [[nodiscard]] std::vector<CouponPtr> couponsFromNode(const xml::XNode& node) const;
    [[nodiscard]] String formatTypeText(CouponType type, StringView text) const;
    [[nodiscard]] String typeTooltip(CouponType type) const;
};

template <CVType Type>
class CouponView final : public CouponViewBase {
public:
    CouponView(commons::Commons& comm, SummaryPtr summary, UseCounterPtr useCounter,
               Parent parent = {}, std::uint32_t style = 0U,
               std::function<bool()> catchMod = {},
               FeatureProvider feature = {})
        : CouponViewBase(Type, comm, std::move(summary), std::move(useCounter),
                         std::move(parent), style, std::move(catchMod),
                         std::move(feature)) {}

    template <CouponType Target>
    [[nodiscard]] bool canConvType() const { return CouponViewBase::canConvType(Target); }
    template <CouponType Target>
    void convType() { CouponViewBase::convType(Target); }
    template <CouponType Target>
    void copyWithType() { CouponViewBase::copyWithType(Target); }
};

}  // namespace cwxeditor::cwx::editor::gui::dwt::couponview
