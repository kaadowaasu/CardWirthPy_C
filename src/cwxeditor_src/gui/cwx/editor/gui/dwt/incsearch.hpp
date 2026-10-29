#pragma once

// cwx.editor.gui.dwt.incsearch
// の移植可能なC++20実装。 DWTのシェルと
// 部品を共通の部品・イベントで表す。検索判定は
// GUIなしでも使え、 バックエンドはモデル化
// されたウィンドウと部品を所有して表示できる。

#include "cwxeditor_src/core/cwx/utils.hpp"
#include "cwxeditor_src/lib/components.hpp"
#include "cwxeditor_src/lib/events.hpp"
#include "cwxeditor_src/lib/portable.hpp"
#include "cwxeditor_src/lib/services.hpp"
#include "cwxeditor_src/lib/text.hpp"

#include <any>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cwxeditor::cwx::editor::gui::dwt::incsearch {

using AdditionalData = std::any;
using AdditionalMatch = ::cwxeditor::lib::Callback<bool(const AdditionalData&)>;

/// 文字列条件に加えて検索結果を絞る、任意の述語。
struct AdditionMatcher {
    std::string name;
    AdditionalMatch match;
};

enum class SearchType : std::int32_t {
    Contains = 0,
    Wildcard = 1,
    Regex = 2,
};

/// DWT部品の構築時にホスト設定から読む初期値。長く残
/// るProps付きホストは、 現在の自動開始条件も渡せ
/// る。共通部品は生成されたプロパティ型へ依存しない。
struct IncSearchSettings {
    bool startIncrementalSearchWhenKeyDown{true};
    std::int32_t incrementalSearchBoxWidth{100};
    std::int32_t incrementalSearchType{0};
    std::string containsLabel{"名前の一部"};
    std::string wildcardLabel{"ワイルドカード"};
    std::string regexLabel{"正規表現"};
    std::string windowTitle{"インクリメンタルサーチ"};
    std::string closeLabel{"閉じる"};
    // ホストの生存中にもMainWindowの設定は変わり得る。 現在のProps
    // を持つホストはこの値を提供し、独立した部品は初期値を保つ。
    std::function<bool()> startIncrementalSearchWhenKeyDownProvider{};
};

/// 逐次検索の状態と共通の入力部品。
class IncSearch final {
public:
    using Services = ::cwxeditor::lib::Services;
    using Widget = ::cwxeditor::lib::Widget;
    using Window = ::cwxeditor::lib::Window;
    using TextEdit = ::cwxeditor::lib::TextEdit;
    using ComboBox = ::cwxeditor::lib::ComboBox;
    using CheckBox = ::cwxeditor::lib::CheckBox;
    using Enabled = ::cwxeditor::lib::Callback<bool()>;
    using ModCallback = ::cwxeditor::lib::Callback<void()>;
    using ModCallbackArray = ::cwxeditor::lib::Array<ModCallback>;

    /// 親は借用する。Servicesとポップアップ部品は値で保持す
    /// るため、 検索やヘッドレステストにネイティブのシェルは不要。
    IncSearch(Services services, Widget* parent, Enabled enabled = {},
              std::vector<AdditionMatcher> addition = {},
              IncSearchSettings settings = {});

    /// サービス群を明示しない部品向けのコンストラクター。
    explicit IncSearch(Widget* parent = nullptr, Enabled enabled = {},
                       std::vector<AdditionMatcher> addition = {},
                       IncSearchSettings settings = {}, Services services = {});

    IncSearch(const IncSearch&) = delete;
    IncSearch& operator=(const IncSearch&) = delete;
    IncSearch(IncSearch&&) = delete;
    IncSearch& operator=(IncSearch&&) = delete;
    ~IncSearch();

    /// 検索語・モード・追加フィルターが変わったときに呼ぶ。
    ModCallbackArray modEvent;
    // ホスト設定の保存は基本的なコールバックだけに限定する。 ポップ
    // アップが表の行内編集に属する場合も、モードの規則は共通にする。
    std::function<void(SearchType)> searchTypeChanged;

    /// 文字列と有効な追加条件がすべて一致したらtrueを返す。 追加デ
    /// ータがnullなら、Dの既定のnull Objectとして扱う。
    [[nodiscard]] bool match(std::string_view text,
                             const AdditionalData* additional_data = nullptr) const;
    [[nodiscard]] bool match(std::string_view text,
                             const AdditionalData& additional_data) const {
        return match(text, &additional_data);
    }
    [[nodiscard]] bool isSearching() const;
    // 不変の検索条件のスナップショット。バックグラウンドの素材列挙から部品を読まない。
    [[nodiscard]] std::function<bool(std::string_view)> textMatcher() const;
    [[nodiscard]] bool matchAdditional(
        const AdditionalData* additional_data = nullptr) const;
    [[nodiscard]] bool matchAdditional(const AdditionalData& additional_data) const {
        return matchAdditional(&additional_data);
    }

    [[nodiscard]] bool isOpen() const noexcept { return open_; }
    [[nodiscard]] bool initialized() const noexcept { return initialized_; }
    [[nodiscard]] bool regexError() const noexcept { return regex_error_; }
    [[nodiscard]] SearchType searchType() const noexcept { return search_type_; }
    [[nodiscard]] const IncSearchSettings& settings() const noexcept { return settings_; }
    void settings(IncSearchSettings value);

    [[nodiscard]] const std::string& searchText() const noexcept;
    /// ホストのキーイベントでキューに入ったが、UIへまだ反映されていない
    /// 文字列も含める。 選択部品の再構成で確定入力を失わないようにする。
    [[nodiscard]] std::string requestedSearchText() const;
    void setSearchText(std::string value);
    void setSearchType(SearchType type);

    [[nodiscard]] std::size_t additionCount() const noexcept { return addition_.size(); }
    [[nodiscard]] const std::vector<AdditionMatcher>& additionMatchers() const noexcept {
        return addition_;
    }
    [[nodiscard]] bool additionEnabled(std::size_t index) const;
    void setAdditionEnabled(std::size_t index, bool enabled);

    /// モデルのポップアップを初期化・表示し、必要なら検索語の初期値を渡す。
    void startIncSearch(std::string first = {});
    /// ポップアップを隠してフィルターをリセットし、状態が変わった場合だけ一度通知する。
    void close();
    /// ホストの範囲から、ポップアップの配置と幅を再計算する。
    void resize();
    /// DWTのツール管理で使う有効条件を再評価する。
    void checkEnabled();

    /// incsearch.dの表示キー・フォーカスフィルターに対応する共通処
    /// 理。 Widgetは親階層を持たないため、呼び出し元が操作元を渡す。
    [[nodiscard]] bool handleHostTextInput(
        const ::cwxeditor::lib::TextInputEvent& event,
        const Widget* source = nullptr);
    [[nodiscard]] bool handleHostKeyDown(
        const ::cwxeditor::lib::KeyEvent& event,
        const Widget* source = nullptr);
    void handleFocusChanged(const Widget* focused);
    /// 共通ホストのハンドラーから、一覧部品のキーをこの共通経路へ委譲できる。
    [[nodiscard]] bool ownsCommittedTextInputRegistration(const Widget* source) const;

    void setTextBackend(const ::cwxeditor::lib::TextBackend* backend) noexcept;
    [[nodiscard]] const ::cwxeditor::lib::TextBackend* textBackend() const noexcept {
        return text_backend_;
    }

    [[nodiscard]] Widget* parent() const noexcept { return parent_; }
    void parent(Widget* value);
    // DのIncSearchのシェル識別と同じく、この一時検索画面
    // へフォーカスがある間も、 表が所有する行内編集を維持する。
    void preserveInlineEditFocus(const ::cwxeditor::lib::WidgetPtr& owner);
    [[nodiscard]] const Services& services() const noexcept { return services_; }
    [[nodiscard]] const std::shared_ptr<Window>& window() const noexcept { return window_; }
    [[nodiscard]] const std::shared_ptr<TextEdit>& textEdit() const noexcept {
        return text_;
    }
    [[nodiscard]] const std::shared_ptr<ComboBox>& typeCombo() const noexcept {
        return type_;
    }
    [[nodiscard]] const std::vector<std::shared_ptr<CheckBox>>& additionControls() const noexcept {
        return addition_checkboxes_;
    }
    [[nodiscard]] const ::cwxeditor::lib::Rect& windowBounds() const noexcept {
        return window_bounds_;
    }

private:
    class Start {
    public:
        Start(IncSearch& owner, std::string text)
            : owner_(owner), text_(std::move(text)) {}

        void append(std::string_view text) { text_.append(text); }
        [[nodiscard]] const std::string& text() const noexcept { return text_; }
        void run();

    private:
        IncSearch& owner_;
        std::string text_;
    };
    struct OwnedCommittedTextInputRegistration {
        Widget* widget{};
        std::shared_ptr<int> lifetime;
        std::shared_ptr<::cwxeditor::lib::CommittedTextInputRegistration> registration;
    };

    void initialize();
    void modified();
    void checkClose();
    void notifyModified();
    void reposition();
    void requestStart(std::string_view text);
    void applyInlineEditFocusOwner();
    void registerCommittedTextInput();
    [[nodiscard]] bool automaticStartEnabled() const;
    [[nodiscard]] bool eligibleSource(const Widget* source) const;
    [[nodiscard]] std::string folded(std::string_view value) const;
    [[nodiscard]] bool validSearchType(SearchType type) const noexcept;
    [[nodiscard]] SearchType normalizedSearchType(std::int32_t value) const noexcept;
    [[nodiscard]] float textWidth(std::string_view value) const;

    Services services_{};
    Widget* parent_{};
    std::weak_ptr<const int> parent_lifetime_;
    std::vector<OwnedCommittedTextInputRegistration> committed_text_input_registrations_;
    std::weak_ptr<Widget> inline_edit_focus_owner_;
    Enabled enabled_{};
    IncSearchSettings settings_{};
    const ::cwxeditor::lib::TextBackend* text_backend_{};

    std::vector<AdditionMatcher> addition_;
    std::vector<bool> addition_enabled_;
    std::vector<std::shared_ptr<CheckBox>> addition_checkboxes_;

    std::shared_ptr<Window> window_;
    std::shared_ptr<TextEdit> text_;
    std::shared_ptr<ComboBox> type_;
    std::shared_ptr<::cwxeditor::lib::Button> close_button_;
    SearchType search_type_{SearchType::Contains};
    std::optional<::cwxeditor::cwx::utils::Wildcard> wildcard_;
    class UnicodePattern;
    std::shared_ptr<const UnicodePattern> regex_;
    bool regex_error_{};
    bool initialized_{};
    bool open_{};
    bool in_mod_{};
    std::uint64_t presentation_revision_{};
    std::shared_ptr<Start> start_;
    ::cwxeditor::lib::Rect window_bounds_{};
    std::uint64_t focus_observer_{};
    std::shared_ptr<int> callback_lifetime_{std::make_shared<int>(0)};
};

// DのcreateComboEditor相当。通常のComboと表内のCombo
// で一つの検索ポップアップ・フィルターを使う。 提供元は選択値の型を保持する。
using ComboSearchFilter = std::function<std::vector<::cwxeditor::lib::ChoiceItem>(const IncSearch&)>;
[[nodiscard]] std::shared_ptr<IncSearch> attachComboSearch(
    ::cwxeditor::lib::Services services,
    const std::shared_ptr<::cwxeditor::lib::ComboBox>& combo,
    const std::shared_ptr<::cwxeditor::lib::TextEdit>& projection,
    ComboSearchFilter filter, IncSearchSettings settings = {},
    std::function<bool()> current = {}, ::cwxeditor::lib::ResourceId image = 0,
    std::function<void(SearchType)> persist_type = {}, bool preserve_edit_text = false,
    std::shared_ptr<::cwxeditor::lib::Menu> context_menu = {});

}  // namespace cwxeditor::cwx::editor::gui::dwt::incsearch
