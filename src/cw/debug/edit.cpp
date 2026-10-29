#include "cw/debug/edit.hpp"
#include "cw/debug/retained_presentation.hpp"
#include "cw/character.hpp"
#include "cw/content.hpp"
#include "cw/cw.hpp"
#include "cw/calculator.hpp"
#include "cw/data.hpp"
#include "cw/debug/debugger.hpp"
#include "cw/dialog/edit.hpp"
#include "cw/dialog/message.hpp"
#include "cw/dialog/presentation.hpp"
#include "cw/dialog/textedit_input.hpp"
#include "cw/dialog/textedit_render.hpp"
#include "cw/event.hpp"
#include "cw/frame.hpp"
#include "cw/header.hpp"
#include "cw/lib/encoding.hpp"
#include "cw/lib/python_integer.hpp"
#include "cw/lib/ui_surface.hpp"
#include "cw/scenariodb.hpp"
#include "cw/setting.hpp"
#include "cw/sprite/card.hpp"
#include "cw/thread.hpp"
#include "cw/util.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <condition_variable>
#include <iterator>
#include <iomanip>
#include <limits>
#include <sstream>
#include <numeric>
#include <stdexcept>
#include <string_view>

#ifdef _MSC_VER
#pragma warning(disable: 4996)
#pragma warning(disable: 4456 4100)
#endif

namespace {

using Integer = cw::lib::Integer;
using Real = cw::lib::Real;
using String = cw::lib::String;
using Value = cw::lib::Value;
using Coupon = cw::debug::edit::Coupon;

void draw_debug_edit_icon(const std::shared_ptr<::cw::lib::Surface>& surface,
                          const String& name, const Integer x, const Integer y)
{
    if(name.empty())return;
    if (name.ends_with("_dbg")) {
        // Py の称号行と同様、スキンの dialogs にある STATUS*_dbg リソースを使う。
        if (const auto icon = ::cw::lib::ui::resource_dialog_surface(name))
            ::cw::lib::ui::blend_surface(surface,icon,x,y,14,14);
        return;
    }
    static std::map<String,std::shared_ptr<::cw::lib::Surface>> cache;
    auto& icon=cache[name];
    if(!icon)icon=::cw::util::load_wxbmp("Data/Debugger/"+name+".png",true,
        {},{},std::nullopt,true,false,false,::cw::lib::Value(1));
    if(icon)::cw::lib::ui::blend_surface(surface,icon,x,y,16,16);
}
constexpr std::uint32_t k_shift_modifier = 1u;

String lower_text(String value) { return ::cw::lib::encoding::python_lower(value); }
bool valid_index(Integer index, std::size_t size) { return index >= 0 && static_cast<std::size_t>(index) < size; }
// 選択位置は行の更新後も残り得るため、無効な位置を除去し、並べ替えて重複を除く
// 。 一括操作では表示順に各行を一度だけ処理する。共通 Presentation
// は選択位置を値として保持するが、 wx の GetNextItem は有
// 効な行を昇順で重複なく列挙する。このヘルパーで取得・削除・先頭移動・末尾移
// 動の規則を揃える。 ListEditDialog の一段ずつの上下移動は
// UI 側の選択条件を前提とし、手動で渡された各位置を個別には検証しない。
template<class T> std::vector<Integer> clean_indexes(const std::vector<Integer>& indexes, std::size_t size) { std::vector<Integer> out; for (auto i:indexes) if(valid_index(i,size)) out.push_back(i); std::sort(out.begin(),out.end()); out.erase(std::unique(out.begin(),out.end()),out.end()); return out; }
template<class T> void move_to_top(std::vector<T>& sequence,std::vector<Integer>& indexes) { indexes=clean_indexes<T>(indexes,sequence.size()); if(indexes.empty())return; std::vector<T> selected; for(auto it=indexes.rbegin();it!=indexes.rend();++it){selected.push_back(std::move(sequence[*it]));sequence.erase(sequence.begin()+*it);}std::reverse(selected.begin(),selected.end());sequence.insert(sequence.begin(),std::make_move_iterator(selected.begin()),std::make_move_iterator(selected.end()));indexes.resize(selected.size());std::iota(indexes.begin(),indexes.end(),0); }
template<class T> void move_to_bottom(std::vector<T>& sequence,std::vector<Integer>& indexes) { indexes=clean_indexes<T>(indexes,sequence.size()); if(indexes.empty())return; std::vector<T> selected; for(auto it=indexes.rbegin();it!=indexes.rend();++it){selected.push_back(std::move(sequence[*it]));sequence.erase(sequence.begin()+*it);}std::reverse(selected.begin(),selected.end());Integer first=static_cast<Integer>(sequence.size());sequence.insert(sequence.end(),std::make_move_iterator(selected.begin()),std::make_move_iterator(selected.end()));indexes.resize(selected.size());for(std::size_t i=0;i<indexes.size();++i)indexes[i]=first+static_cast<Integer>(i); }
std::optional<Integer> python_integer_value(const String& text)
{
    const auto normalized=::cw::lib::normalize_python_integer(text);
    if(!normalized)return std::nullopt;
    try{return ::cw::lib::Decimal::parse(*normalized).to_integer(::cw::lib::DecimalRounding::Down);}
    catch(const ::cw::lib::DecimalError&){return std::nullopt;}
}
// Python の称号値は任意精度整数だが、ネイティブの Integer に
// は範囲制限がある。 アンダースコアや Unicode 数字を含む Python
// の構文を維持し、範囲外の値は桁あふれさせず拒否する。 必須の称号変換ア
// ダプターは、その失敗を invalid_argument で通知する。
Integer required_python_integer(const String& text,const char* context)
{
    const auto value=python_integer_value(text);
    if(!value)throw std::invalid_argument(String(context)+text);
    return *value;
}
std::optional<Integer> value_integer(const Value& value) { if(auto p=std::get_if<Integer>(&value.storage()))return *p;if(auto p=std::get_if<Real>(&value.storage()))return static_cast<Integer>(*p);if(auto p=std::get_if<String>(&value.storage()))return python_integer_value(*p);return std::nullopt; }
String scenario_display_label(const String& name, const String& author)
{
    return author.empty() ? name : name + "(" + author + ")";
}
void fill_rows(const std::shared_ptr<cw::debug::edit::AutoWidthListCtrl>& values,const std::vector<std::tuple<String,String>>& list){if(!values)return;values->m_rows.clear();values->m_images.clear();for(const auto&[a,b]:list){values->m_rows.push_back({scenario_display_label(a,b)});values->m_images.push_back(0);}}
[[maybe_unused]] void fill_rows(const std::shared_ptr<cw::debug::edit::AutoWidthListCtrl>& values,const std::vector<String>& list){if(!values)return;values->m_rows.clear();values->m_images.clear();for(const auto&a:list){values->m_rows.push_back({a});values->m_images.push_back(0);}}
void fill_rows(const std::shared_ptr<cw::debug::edit::EditableListCtrl>& values,const std::vector<String>& list){if(!values)return;values->m_rows.clear();values->m_images.clear();for(const auto&a:list){values->m_rows.push_back({a});values->m_images.push_back(0);}}
void set_selected(std::vector<Integer>& selected,std::size_t size){selected=clean_indexes<Integer>(selected,size);}
template<class T> void remove_selected_rows(std::vector<T>& rows,
                                           std::vector<Integer>& selected,
                                           std::vector<T>& removed)
{
    set_selected(selected, rows.size());
    // Py は選択行の先頭を繰り返し削除する。vector の位
    // 置は後ろから削除しつつ、削除バッファには表示順で格納する。
    for (const auto index : selected) removed.push_back(rows[index]);
    for (auto it = selected.rbegin(); it != selected.rend(); ++it)
        rows.erase(rows.begin() + *it);
    selected.clear();
}
struct FindRows {
    std::vector<std::vector<String>>* rows{};
    std::vector<Integer>* selected{};
};
FindRows find_rows(const Value& value)
{
    const auto object = std::get_if<::cw::lib::ObjectPtr>(&value.storage());
    if (!object || !*object)
        return {};
    if (const auto values = std::dynamic_pointer_cast<cw::debug::edit::AutoWidthListCtrl>(*object))
        return {&values->m_rows, &values->m_selected};
    if (const auto values = std::dynamic_pointer_cast<cw::debug::edit::EditableListCtrl>(*object))
        return {&values->m_rows, &values->m_selected};
    return {};
}
int natural_compare_text(const String& lhs, const String& rhs)
{
    return static_cast<int>(::cw::util::logical_compare(lhs, rhs));
}
bool natural_less(const String& lhs, const String& rhs)
{
    return natural_compare_text(lhs, rhs) < 0;
}
bool natural_tuple_less(const std::tuple<String, String>& lhs,
                        const std::tuple<String, String>& rhs)
{
    const auto first = natural_compare_text(std::get<0>(lhs), std::get<0>(rhs));
    return first != 0 ? first < 0 : natural_less(std::get<1>(lhs), std::get<1>(rhs));
}
template<class Exception>
String variant_error_text(const Exception& exception)
{
    return ::cw::content::variant_error_msg(
        std::make_shared<Exception>(exception));
}

// Py はダイアログごとに一つのプレビュー処理を持ち、キー入力ごとにスレッ
// ドを作らない。 ネイティブ版では一つの共通タイマーワーカーと、各ダイアロ
// グ一つの保留要求を使う。 不変なコールバックだけを送り、評価はゲームの
// 所有スレッドで行う。保留要求の置き換えで 500ms の待機をやり直し
// 、 pthread を使う Web 実行環境でもスレッドを増やさない。
class VariantPreviewTimer {
    using Clock = std::chrono::steady_clock;
    struct Pending { Clock::time_point due; std::function<void()> post; };
    std::mutex mutex;
    std::condition_variable changed;
    std::map<const void*, Pending> pending;
    bool stopping{};
    std::thread worker;
    void run() {
        std::unique_lock lock(mutex);
        while (!stopping) {
            if (pending.empty()) {
                changed.wait(lock, [this] { return stopping || !pending.empty(); });
                continue;
            }
            const auto next = std::min_element(pending.begin(), pending.end(),
                [](const auto& a, const auto& b) { return a.second.due < b.second.due; });
            const auto due = next->second.due;
            if (Clock::now() < due) {
                changed.wait_until(lock, due);
                continue; // 要求の置き換えにより、最も早い実行時刻が変わった可能性がある。
            }
            auto post = std::move(next->second.post);
            pending.erase(next);
            lock.unlock();
            post(); // このコールバックは Frame へ処理を送るだけで、評価や UI フィールドの所有は行わない。
            lock.lock();
        }
    }
public:
    VariantPreviewTimer() : worker([this] { run(); }) {}
    ~VariantPreviewTimer() {
        { std::scoped_lock lock(mutex); stopping = true; pending.clear(); }
        changed.notify_one();
        worker.join();
    }
    void schedule(const void* key, std::function<void()> post) {
        {
            std::scoped_lock lock(mutex);
            pending.insert_or_assign(key, Pending{Clock::now() + std::chrono::milliseconds(500), std::move(post)});
        }
        changed.notify_one();
    }
};

VariantPreviewTimer& variant_preview_timer() {
    static VariantPreviewTimer timer;
    return timer;
}

struct VariantEvalResult {
    std::optional<Value> value{};
    String description{};
};

// 計算式の評価はダイアログや UI の状態から分離する。
// 呼び出し側が不変な式のスナップショットを渡し、 世代を
// 確認してから UI ディスパッチャーで結果を反映する。
VariantEvalResult evaluate_variant_expression(const String& expression,
                                              bool different_scenario)
{
    VariantEvalResult result;
    try {
        const auto parsed = ::cw::calculator::parse(expression);
        auto option = std::make_shared<::cw::calculator::CalcOption>();
        option->initialize("Debugger", different_scenario);
        const auto evaluated = ::cw::calculator::eval_expr(parsed, option);
        if (!evaluated)
            throw std::runtime_error("empty expression result");
        result.value = evaluated->m_value;
    } catch (const ::cw::calculator::TokanizeException& exception) {
        result.description = variant_error_text(exception);
    } catch (const ::cw::calculator::SemanticsException& exception) {
        result.description = variant_error_text(exception);
    } catch (const ::cw::calculator::FunctionIsNotDefinedException& exception) {
        result.description = variant_error_text(exception);
    } catch (const ::cw::calculator::ArgumentIsNotDecimalException& exception) {
        result.description = variant_error_text(exception);
    } catch (const ::cw::calculator::ArgumentIsNotStringException& exception) {
        result.description = variant_error_text(exception);
    } catch (const ::cw::calculator::ArgumentIsNotBooleanException& exception) {
        result.description = variant_error_text(exception);
    } catch (const ::cw::calculator::ArgumentsCountException& exception) {
        result.description = variant_error_text(exception);
    } catch (const ::cw::calculator::InvalidArgumentException& exception) {
        result.description = variant_error_text(exception);
    } catch (const ::cw::calculator::InvalidStepValueException& exception) {
        result.description = variant_error_text(exception);
    } catch (const ::cw::calculator::VariantNotFoundException& exception) {
        result.description = variant_error_text(exception);
    } catch (const ::cw::calculator::FlagNotFoundException& exception) {
        result.description = variant_error_text(exception);
    } catch (const ::cw::calculator::StepNotFoundException& exception) {
        result.description = variant_error_text(exception);
    } catch (const ::cw::calculator::DifferentScenarioException& exception) {
        result.description = variant_error_text(exception);
    } catch (const ::cw::calculator::ListIndexOutOfRangeException& exception) {
        result.description = variant_error_text(exception);
    } catch (const ::cw::calculator::ArgumentIsNotListException& exception) {
        result.description = variant_error_text(exception);
    } catch (const ::cw::calculator::ArgumentIsNotStructureException& exception) {
        result.description = variant_error_text(exception);
    } catch (const ::cw::calculator::ExprPermissionError& exception) {
        result.description = variant_error_text(exception);
    } catch (const ::cw::calculator::DifferentStructureException& exception) {
        result.description = variant_error_text(exception);
    } catch (const ::cw::calculator::ZeroDivisionException& exception) {
        result.description = variant_error_text(exception);
    } catch (const ::cw::calculator::ComputeException& exception) {
        result.description = exception.m_msg.empty() ? "式を評価できません" : exception.m_msg;
    } catch (...) {
        // Py と同様、式のエラーとして扱うのは ComputeException
        // だけとする。 それ以外の失敗は診断情報を残して呼び出し元の
        // イベント境界へ再送出し、通常の不正な式の説明に置き換えない。
        ::cw::util::print_ex();
        throw;
    }
    return result;
}

}


namespace cw::debug::edit {

namespace {

// デバッガーの全編集ダイアログは、一つの共通 Presentation
// を公開する。 ネイティブホストはファイル選
// 択や入力の受け渡しを提供するが、画面そのも
// のを別実装せず、UI の実装を共通化する。
class CommonEditPresentation final : public ::cw::lib::Object,
                                     public ::cw::dialog::DialogPresentationProvider,
                                     public std::enable_shared_from_this<CommonEditPresentation> {
public:
    explicit CommonEditPresentation(::cw::lib::Object& object) : m_object(object) {
        if(const auto* variant=dynamic_cast<const VariantEditDialog*>(&object))m_input_focus=variant->m_value_kind;
        else if(auto* selected=selection();selected&&!selected->empty()){
            m_list_cursor=selected->front();m_selection_anchor=m_list_cursor;
        }
    }

    [[nodiscard]] ::cw::dialog::DialogPresentation dialog_presentation() override;
    void dialog_presentation_action(::cw::dialog::DialogPresentationAction action,
                                    int index) override;
    void dialog_presentation_modifiers(
        ::cw::dialog::DialogPresentationModifiers modifiers) override { m_modifiers=modifiers; }
    void dialog_presentation_text(const std::string& text, bool composition) override;
    void dialog_presentation_pointer(::cw::dialog::DialogPresentationAction action,
        int index,float x,float y,bool pressed) override;
    void dialog_presentation_focus(bool focused) override {
        if(!focused && !m_finished)close_cell_editor();
    }
    void dialog_presentation_key(::cw::dialog::DialogPresentationKey key, bool pressed,
                                 ::cw::dialog::DialogPresentationModifiers modifiers) override;
    void dialog_presentation_enter(::cw::dialog::DialogPresentationModifiers modifiers) override;
    void dialog_presentation_scroll(float, float, int lines) override {
        if (m_finished || m_operation_pending || m_document_token) return;
        close_cell_editor();
        m_scroll = std::max<Integer>(0, m_scroll + lines);
    }
    [[nodiscard]] bool dialog_presentation_finished() const noexcept override {
        return m_finished;
    }
    void dialog_presentation_complete() override {
        m_finished = true;
        cancel_document_request();
        if (auto* variant = dynamic_cast<VariantEditDialog*>(&m_object))
            variant->m_eval_generation->fetch_add(1, std::memory_order_acq_rel);
    }

private:
    // 行に対する共通操作は維持しつつ、名前と点数のセルを区別する。
    static constexpr int coupon_value_row_offset = 1000000;
    enum Command : int {
        Add, Remove, MoveUp, MoveDown, MoveTop, MoveBottom, Update,
        Copy, ValueEdit, PreviousTarget, NextTarget, EditEntry,
        FindUp, FindDown, ToggleAdjust, TypeNumber, TypeString,
        TypeBoolean, TypeExpression, ToggleBoolean,
        FocusNumber, FocusString, FocusBoolean, FocusExpression
    };

    ::cw::lib::Object& m_object;
    Integer m_scroll{};
    bool m_finished{};
    bool m_operation_pending{};
    Integer m_cell_row{-1};
    bool m_cell_score{};
    ::cw::dialog::textedit::Buffer m_cell_edit{};
    int m_input_focus{-1};
    bool m_select_all{};
    String m_composition{};
    Integer m_list_cursor{};
    Integer m_selection_anchor{-1};
    int m_list_focus{11};
    bool m_find_select_all{};
    String m_find_composition{};
    ::cw::dialog::DialogPresentationModifiers m_modifiers{};
    std::shared_ptr<int> m_document_token{};
    std::shared_ptr<::cw::lib::documents::Ticket> m_document_ticket{};

    [[nodiscard]] String title() const;
    [[nodiscard]] std::vector<std::vector<String>> rows() const;
    [[nodiscard]] std::vector<Integer>* selection();
    [[nodiscard]] FindPanel* find_panel();
    void select_row(int index);
    void selection_changed();
    void accept();
    void command(int index);
    static constexpr Integer k_visible_rows = 10;
    void ensure_visible(Integer row) {
        if (row < m_scroll) m_scroll = row;
        else if (row >= m_scroll + k_visible_rows) m_scroll = row - k_visible_rows + 1;
    }
    void edit_selected(bool coupon_score = false);
    void close_cell_editor();
    void request_bookmark_document(Integer row, String initial);
    void finish_bookmark_document(const std::shared_ptr<int>& token, Integer row,
                                  ::cw::lib::documents::Result result);
    void cancel_document_request() noexcept;
    void update_variant_inline(VariantEditDialog& dialog);
};

String CommonEditPresentation::title() const
{
    if (dynamic_cast<const CouponEditDialog*>(&m_object))
        return "キャラクターの経歴の編集";
    if (const auto* dialog = dynamic_cast<const ListEditDialog*>(&m_object))
        return dialog->m_dialog_title.empty() ? String("リストの編集") : dialog->m_dialog_title;
    if (dynamic_cast<const EditBookmarksForCardEditDialog*>(&m_object)) return "ブックマーク";
    if (dynamic_cast<const BreakpointEditDialog*>(&m_object)) return "ブレークポイントを設定したシナリオ";
    if (dynamic_cast<const SavedJPDCImageEditDialog*>(&m_object)) return "JPDCイメージを保存したシナリオ";
    if (dynamic_cast<const SavedVariablesEditDialog*>(&m_object)) return "状態変数を保存したシナリオ";
    if (const auto* dialog = dynamic_cast<const VariantEditDialog*>(&m_object))
        return dialog->m_dialog_title.empty() ? String("値の編集") : dialog->m_dialog_title;
    return "デバッグ編集";
}

std::vector<std::vector<String>> CommonEditPresentation::rows() const
{
    if (const auto* dialog = dynamic_cast<const CouponEditDialog*>(&m_object))
        return dialog->m_values ? dialog->m_values->m_rows : std::vector<std::vector<String>>{};
    if (const auto* dialog = dynamic_cast<const ListEditDialog*>(&m_object)) {
        std::vector<std::vector<String>> out;
        for (const auto& value : dialog->m_list) out.push_back({value});
        return out;
    }
    const auto bookmarks = [](const BookmarkList& list) {
        std::vector<std::vector<String>> out;
        for (const auto& [path, name] : list) out.push_back({name, path});
        return out;
    };
    if (const auto* dialog = dynamic_cast<const EditBookmarksForCardEditDialog*>(&m_object)) {
        auto out = bookmarks(dialog->m_list);
        for (std::size_t i=0;i<out.size() && i<dialog->m_update_failed.size();++i)
            if (dialog->m_update_failed[i]) out[i][0] = "*読込失敗*";
        return out;
    }
    const auto combined = [](const BookmarkList& list) {
        std::vector<std::vector<String>> out;
        for (const auto& [name, author] : list)
            out.push_back({scenario_display_label(name, author)});
        return out;
    };
    if (const auto* dialog = dynamic_cast<const BreakpointEditDialog*>(&m_object))
        return combined(dialog->m_list);
    if (const auto* dialog = dynamic_cast<const SavedJPDCImageEditDialog*>(&m_object))
        return combined(dialog->m_list);
    if (const auto* dialog = dynamic_cast<const SavedVariablesEditDialog*>(&m_object))
        return combined(dialog->m_list);
    return {};
}

std::vector<Integer>* CommonEditPresentation::selection()
{
    if (auto* dialog = dynamic_cast<CouponEditDialog*>(&m_object)) return &dialog->m_selected;
    if (auto* dialog = dynamic_cast<ListEditDialog*>(&m_object)) return &dialog->m_selected;
    if (auto* dialog = dynamic_cast<EditBookmarksForCardEditDialog*>(&m_object)) return &dialog->m_selected;
    if (auto* dialog = dynamic_cast<BreakpointEditDialog*>(&m_object)) return &dialog->m_selected;
    if (auto* dialog = dynamic_cast<SavedJPDCImageEditDialog*>(&m_object)) return &dialog->m_selected;
    if (auto* dialog = dynamic_cast<SavedVariablesEditDialog*>(&m_object)) return &dialog->m_selected;
    return nullptr;
}

FindPanel* CommonEditPresentation::find_panel()
{
    if (auto* dialog = dynamic_cast<CouponEditDialog*>(&m_object)) return dialog->m_find.get();
    if (auto* dialog = dynamic_cast<ListEditDialog*>(&m_object)) return dialog->m_find.get();
    if (auto* dialog = dynamic_cast<BreakpointEditDialog*>(&m_object)) return dialog->m_find.get();
    if (auto* dialog = dynamic_cast<SavedJPDCImageEditDialog*>(&m_object)) return dialog->m_find.get();
    if (auto* dialog = dynamic_cast<SavedVariablesEditDialog*>(&m_object)) return dialog->m_find.get();
    return nullptr;
}

void CommonEditPresentation::select_row(const int index)
{
    const auto data = rows();
    if (index < 0 || static_cast<std::size_t>(index) >= data.size()) return;
    m_list_cursor = index;
    m_list_focus = 11;
    if (auto* selected = selection()) {
        const bool shift=(m_modifiers&::cw::dialog::DialogPresentationModifierShift)!=0;
        const bool control=(m_modifiers&::cw::dialog::DialogPresentationModifierControl)!=0;
        if(shift&&m_selection_anchor>=0){
            selected->clear();const auto first=std::min<Integer>(m_selection_anchor,index),last=std::max<Integer>(m_selection_anchor,index);
            for(Integer i=first;i<=last;++i)selected->push_back(i);
        }else if(control){
            const auto it=std::find(selected->begin(),selected->end(),index);
            if(it==selected->end())selected->push_back(index);else selected->erase(it);
            m_selection_anchor=index;
        }else{*selected={index};m_selection_anchor=index;}
        std::sort(selected->begin(), selected->end());


    }
    selection_changed();
}

void CommonEditPresentation::selection_changed()
{
    if (const auto* selected = selection()) {
        // wx は一覧コントロールで選択を保持してダイアログへ通知す
        // る。共通 Presentation はキーやポインターによ
        // る選択を 先にダイアログへ格納するため、同じ項目選択コール
        // バックを呼ぶ前に一覧モデルへ反映する。 FindPanel
        // は逆に一覧モデルから処理するが、どちらも Ctrl/Shift
        // による複数選択を保ったまま同じ状態に揃える。
        if (auto* dialog = dynamic_cast<CouponEditDialog*>(&m_object); dialog && dialog->m_values)
            dialog->m_values->m_selected = *selected;
        else if (auto* dialog = dynamic_cast<ListEditDialog*>(&m_object); dialog && dialog->m_values)
            dialog->m_values->m_selected = *selected;
        else if (auto* dialog = dynamic_cast<BreakpointEditDialog*>(&m_object); dialog && dialog->m_values)
            dialog->m_values->m_selected = *selected;
        else if (auto* dialog = dynamic_cast<SavedJPDCImageEditDialog*>(&m_object); dialog && dialog->m_values)
            dialog->m_values->m_selected = *selected;
        else if (auto* dialog = dynamic_cast<SavedVariablesEditDialog*>(&m_object); dialog && dialog->m_values)
            dialog->m_values->m_selected = *selected;
    }
    if (auto* dialog = dynamic_cast<CouponEditDialog*>(&m_object)) dialog->OnItemSelected(nullptr);
    else if (auto* dialog = dynamic_cast<ListEditDialog*>(&m_object)) dialog->OnItemSelected(nullptr);
    else if (auto* dialog = dynamic_cast<EditBookmarksForCardEditDialog*>(&m_object)) dialog->OnItemSelected(nullptr);
    else if (auto* dialog = dynamic_cast<BreakpointEditDialog*>(&m_object)) dialog->OnItemSelected(nullptr);
    else if (auto* dialog = dynamic_cast<SavedJPDCImageEditDialog*>(&m_object)) dialog->OnItemSelected(nullptr);
    else if (auto* dialog = dynamic_cast<SavedVariablesEditDialog*>(&m_object)) dialog->OnItemSelected(nullptr);
}

void CommonEditPresentation::accept()
{
    if (auto* dialog = dynamic_cast<GossipEditDialog*>(&m_object)) dialog->OnOkBtn(nullptr);
    else if (auto* dialog = dynamic_cast<CompStampEditDialog*>(&m_object)) dialog->OnOkBtn(nullptr);
    else if (auto* dialog = dynamic_cast<CouponEditDialog*>(&m_object)) dialog->OnOkBtn(nullptr);
    else if (auto* dialog = dynamic_cast<VariantEditDialog*>(&m_object)) {
        dialog->OnOkBtn(nullptr);
        if (!dialog->m_modal_accepted) return;
    } else if (auto* dialog = dynamic_cast<EditBookmarksForCardEditDialog*>(&m_object)) dialog->OnOkBtn(nullptr);
    else if (auto* dialog = dynamic_cast<BreakpointEditDialog*>(&m_object)) dialog->OnOkBtn(nullptr);
    else if (auto* dialog = dynamic_cast<SavedJPDCImageEditDialog*>(&m_object)) dialog->OnOkBtn(nullptr);
    else if (auto* dialog = dynamic_cast<SavedVariablesEditDialog*>(&m_object)) dialog->OnOkBtn(nullptr);
    else return;
    m_finished = true;
}

void CommonEditPresentation::edit_selected(const bool coupon_score)
{
    const auto* selected = selection();
    if (!selected || selected->empty()) return;
    const Integer row = selected->front();
    const auto data = rows();
    if (!valid_index(row, data.size()) || data[static_cast<std::size_t>(row)].empty()) return;
    auto initial = data[static_cast<std::size_t>(row)].front();
    if (coupon_score) {
        if (!dynamic_cast<CouponEditDialog*>(&m_object) || data[static_cast<std::size_t>(row)].size()<2)return;
        initial=data[static_cast<std::size_t>(row)][1];
    }
    if (auto* dialog = dynamic_cast<EditBookmarksForCardEditDialog*>(&m_object)) {
        initial = std::get<0>(dialog->m_list[static_cast<std::size_t>(row)]);
        request_bookmark_document(row, std::move(initial));
        return;
    }
    // Py の TextEditMixin.OpenEditor と同様、操作した一覧セルをその場で編集する。
    if (!dynamic_cast<CouponEditDialog*>(&m_object) && !dynamic_cast<ListEditDialog*>(&m_object)) return;
    m_cell_row=row;
    m_cell_score=coupon_score;
    m_cell_edit={};
    m_cell_edit.text=std::move(initial);
    m_cell_edit.anchor=0;
    m_cell_edit.caret=m_cell_edit.text.size();
    m_list_focus=11;
    request_dialog_presentation_refresh();
}

void CommonEditPresentation::close_cell_editor()
{
    if(m_cell_row<0)return;
    const auto row=m_cell_row;
    const auto text=m_cell_edit.text;
    const bool score=m_cell_score;
    m_cell_row=-1;
    m_cell_edit.composition.clear();
    // Py の TextEditMixin.CloseEditor と同様、Enter、Escape、フ
    // ォーカス喪失時に編集を確定する。 不正または重複したラベルと不正な整数では、変更前の値を維持する。
    if(score){
        if(auto* dialog=dynamic_cast<CouponEditDialog*>(&m_object)){
            if(const auto value=value_integer(Value(text)))dialog->_set_value(row,*value);
            dialog->_item_selected();
        }
    }else{
        auto event=std::make_shared<::cw::lib::Window>();
        event->id=row;event->title=text;
        if(auto* dialog=dynamic_cast<CouponEditDialog*>(&m_object))dialog->OnEndLabelEdit(event);
        else if(auto* dialog=dynamic_cast<ListEditDialog*>(&m_object))dialog->OnEndLabelEdit(event);
    }
    request_dialog_presentation_refresh();
}

void CommonEditPresentation::request_bookmark_document(const Integer row, String initial)
{
    namespace doc = ::cw::lib::documents;
    if (m_finished || m_document_token || weak_from_this().expired()) return;
    doc::Request request;
    request.operation = doc::Operation::OpenFiles;
    request.title = "シナリオファイルを選択";
    request.initial_location = std::move(initial);
    request.filters = {{"シナリオファイル", "wsn;wsm;zip;lzh;cab;xml"}};
    const auto token = std::make_shared<int>(0);
    m_document_token = token;
    request_dialog_presentation_refresh();
    const auto weak = weak_from_this();
    try {
        auto ticket = ::cw::dialog::request_presentation_document(
            request, [weak, token, row](doc::Result result) {
                if (const auto self = weak.lock())
                    self->finish_bookmark_document(token, row, std::move(result));
            });
        if (m_document_token == token) {
            m_document_ticket = std::move(ticket);
            if (!m_document_ticket || !m_document_ticket->pending())
                finish_bookmark_document(token, row, {doc::Status::Failed, {},
                    "ファイル選択を開始できませんでした。"});
        }
    } catch (const std::exception& error) {
        finish_bookmark_document(token, row, {doc::Status::Failed, {}, error.what()});
    } catch (...) {
        finish_bookmark_document(token, row, {doc::Status::Failed, {},
            "ファイル選択に失敗しました。"});
    }
}

void CommonEditPresentation::finish_bookmark_document(
    const std::shared_ptr<int>& token, const Integer row,
    ::cw::lib::documents::Result result)
{
    namespace doc = ::cw::lib::documents;
    if (m_document_token != token || m_finished) return;
    cancel_document_request();
    if (result.status == doc::Status::Cancelled) return;
    if (result.status != doc::Status::Success || result.documents.size() != 1) {
        (void)::cw::dialog::message::show_common_modal_message(
            "シナリオファイルを選択", result.error.empty()
                ? "シナリオファイルを選択できませんでした。" : result.error, false, true);
        return;
    }
    const auto& document = result.documents.front();
    auto* dialog = dynamic_cast<EditBookmarksForCardEditDialog*>(&m_object);
    if (!dialog || !valid_index(row, dialog->m_list.size())) return;
    if (document.kind != doc::LocationKind::NativePath || document.location.empty()) {
        (void)::cw::dialog::message::show_common_modal_message(
            "シナリオファイルを選択",
            "選択したシナリオを永続的なローカルファイルとして参照できません。",
            false, true);
        return;
    }
    std::get<0>(dialog->m_list[static_cast<std::size_t>(row)]) = document.location;
    request_dialog_presentation_refresh();
}

void CommonEditPresentation::cancel_document_request() noexcept
{
    if (m_document_ticket) m_document_ticket->cancel();
    m_document_ticket.reset();
    m_document_token.reset();
    request_dialog_presentation_refresh();
}

void CommonEditPresentation::command(const int index)
{
    auto* coupon = dynamic_cast<CouponEditDialog*>(&m_object);
    auto* list = dynamic_cast<ListEditDialog*>(&m_object);
    auto* bookmark = dynamic_cast<EditBookmarksForCardEditDialog*>(&m_object);
    const auto reveal_selection = [&](bool last) {
        const auto* selected = selection();
        if (selected && !selected->empty()) ensure_visible(last ? selected->back() : selected->front());
    };
    switch (index) {
    case Add:
        if (coupon) coupon->OnAddBtn(nullptr);
        else if (list) list->OnAddBtn(nullptr);
        else return;
        // Py の OnAddBtn と同様、新しい行を追加した直後に名前セルの編集を開始する。
        m_scroll = 0;
        select_row(0);
        edit_selected();
        request_dialog_presentation_refresh();
        return;
    case Remove:
        if (coupon) coupon->OnRemoveBtn(nullptr); else if (list) list->OnRemoveBtn(nullptr);
        else if (bookmark) bookmark->OnRemoveBtn(nullptr);
        else if (auto* d=dynamic_cast<BreakpointEditDialog*>(&m_object)) d->OnRemoveBtn(nullptr);
        else if (auto* d=dynamic_cast<SavedJPDCImageEditDialog*>(&m_object)) d->OnRemoveBtn(nullptr);
        else if (auto* d=dynamic_cast<SavedVariablesEditDialog*>(&m_object)) d->OnRemoveBtn(nullptr);
        return;
    case MoveUp: if(coupon)coupon->OnUpBtn(nullptr);else if(list)list->OnUpBtn(nullptr);else if(bookmark)bookmark->OnUpBtn(nullptr);reveal_selection(false);return;
    case MoveDown: if(coupon)coupon->OnDownBtn(nullptr);else if(list)list->OnDownBtn(nullptr);else if(bookmark)bookmark->OnDownBtn(nullptr);reveal_selection(true);return;
    case MoveTop: if(coupon)coupon->OnUp2Btn(nullptr);else if(list)list->OnUp2Btn(nullptr);else if(bookmark)bookmark->OnUp2Btn(nullptr);reveal_selection(false);return;
    case MoveBottom: if(coupon)coupon->OnDown2Btn(nullptr);else if(list)list->OnDown2Btn(nullptr);else if(bookmark)bookmark->OnDown2Btn(nullptr);reveal_selection(true);return;
    case Update: if(bookmark)bookmark->OnUpdateBtn(nullptr);return;
    case PreviousTarget: if(coupon)coupon->OnLeftBtn(nullptr);return;
    case NextTarget: if(coupon)coupon->OnRightBtn(nullptr);return;
    case EditEntry:
        edit_selected();
        return;
    case ValueEdit:
        if(coupon) coupon->OnValueBtn(nullptr);
        return;
    case Copy:
        if(coupon && !coupon->m_coupons.empty()) {
            std::vector<String> choices;
            for(std::size_t i=1;i<coupon->m_targets.size();++i)choices.push_back(coupon->m_targets[i]);
            // Python と同様、上書きの説明を含む ComboEditDialog2
            // を使う。 共通ダイアログを一つに保ち、説明を省いた別の選択画面は作らない。
            std::optional<Integer> chosen;
            ::cw::dialog::edit::ComboEditDialog2 dialog;
            dialog.m_on_accept=[&](Integer index){chosen=index;};
            dialog.initialize(nullptr,"全て複製",
                "選択したメンバの全ての称号を編集中の称号に上書きコピーします。\nコピー元を選択してください。",
                std::move(choices));
            ::cw::dialog::edit::run_edit_ui_modal(coupon->m_native_parent_handle);
            if(chosen) {
                auto event=std::make_shared<::cw::lib::Window>();event->id=*chosen;coupon->OnCopyBtn(event);
            }
        } return;
    case ToggleAdjust: if(coupon)coupon->m_adjust_level_value=!coupon->m_adjust_level_value;return;
    case FindUp:
    case FindDown: {
        auto* panel=find_panel();
        if(!panel)return;
        if(index==FindUp)panel->find_up();else panel->find_down();
        if(panel->m_found && panel->m_cursor>=0){m_list_cursor=panel->m_cursor;m_scroll=std::max<Integer>(0,m_list_cursor-5);}
        m_list_focus=10;
        request_dialog_presentation_refresh();
        return;
    }
    default: break;
    }
    if (auto* variant=dynamic_cast<VariantEditDialog*>(&m_object)) {
        if(index>=TypeNumber&&index<=TypeExpression){
            variant->m_value_kind=index-TypeNumber;m_input_focus=variant->m_value_kind;m_select_all=true;
            update_variant_inline(*variant);
        } else if(index==ToggleBoolean || index==FocusBoolean){
            m_input_focus=2;variant->m_value_bool_value=!variant->m_value_bool_value;update_variant_inline(*variant);
        } else if(index>=FocusNumber&&index<=FocusExpression){
            m_input_focus=index-FocusNumber;m_select_all=true;
        }
        request_dialog_presentation_refresh();
    }
}

void CommonEditPresentation::update_variant_inline(VariantEditDialog& dialog)
{
    // 共通入力もダイアログ API と同じ OnValue の遅延処理を通す。
    // Py は最後の式編集から 500ms 待つ。 ここでキー入力のたびに評価
    // すると待機が無効になり、共通 UI スレッドを停止させるおそれがある。
    dialog.OnValue(nullptr);
}

::cw::dialog::DialogPresentation CommonEditPresentation::dialog_presentation()
{
    using Action=::cw::dialog::DialogPresentationAction;
    namespace retained_ui=::cw::debug::retained_presentation;
    constexpr Integer width=560;
    const Integer coupon_extra=dynamic_cast<CouponEditDialog*>(&m_object)?28:0;
    const Integer height=430+coupon_extra;
    const auto* owner_dialog=dynamic_cast<const ::cw::lib::Dialog*>(&m_object);
    const bool game_scale=owner_dialog&&owner_dialog->game_window_scale;
    auto& retained=dialog_retained_surface();
    ::cw::lib::ui::ScaledRetainedCanvas canvas(
        retained,{width,height},retained_ui::presentation_scale(game_scale),{245,245,245,255});
    std::uint64_t body_signature{};retained_ui::mix(body_signature,title());
    retained_ui::mix(body_signature,static_cast<std::uint64_t>(m_input_focus+1));
    retained_ui::mix(body_signature,static_cast<std::uint64_t>(m_list_focus+1));
    retained_ui::mix(body_signature,static_cast<std::uint64_t>(m_list_cursor+1));
    retained_ui::mix(body_signature,static_cast<std::uint64_t>(m_scroll+1));
    retained_ui::mix(body_signature,static_cast<std::uint64_t>(m_cell_row+1));
    retained_ui::mix(body_signature,static_cast<std::uint64_t>(m_cell_score));
    retained_ui::mix(body_signature,m_cell_edit.text);
    retained_ui::mix(body_signature,m_cell_edit.composition);
    retained_ui::mix(body_signature,static_cast<std::uint64_t>(m_cell_edit.caret));
    retained_ui::mix(body_signature,static_cast<std::uint64_t>(m_cell_edit.anchor));
    retained_ui::mix(body_signature,static_cast<std::uint64_t>(m_cell_edit.composition_start+1));
    retained_ui::mix(body_signature,static_cast<std::uint64_t>(m_cell_edit.composition_length));
    retained_ui::mix(body_signature,m_composition);retained_ui::mix(body_signature,m_find_composition);
    if(auto* variant=dynamic_cast<VariantEditDialog*>(&m_object)){
        retained_ui::mix(body_signature,variant->m_dialog_label);retained_ui::mix(body_signature,variant->m_value_num_text);
        retained_ui::mix(body_signature,variant->m_value_str_text);retained_ui::mix(body_signature,variant->m_value_expr_text);
        retained_ui::mix(body_signature,variant->m_error_text);retained_ui::mix(body_signature,static_cast<std::uint64_t>(variant->m_value_kind+1));
        retained_ui::mix(body_signature,static_cast<std::uint64_t>(variant->m_value_bool_value));
        retained_ui::mix(body_signature,static_cast<std::uint64_t>(variant->m_value_valid));
    }else{
        if (const auto* coupon=dynamic_cast<CouponEditDialog*>(&m_object)) {
            retained_ui::mix(body_signature,static_cast<std::uint64_t>(coupon->m_target_index+1));
            if (valid_index(coupon->m_target_index,coupon->m_targets.size()))
                retained_ui::mix(body_signature,coupon->m_targets[static_cast<std::size_t>(coupon->m_target_index)]);
            retained_ui::mix(body_signature,coupon->score_summary());
        }
        for(const auto& row:rows())for(const auto& value:row)retained_ui::mix(body_signature,value);
        if(const auto* selected=selection())for(const auto value:*selected)retained_ui::mix(body_signature,static_cast<std::uint64_t>(value+1));
        if(auto* panel=find_panel()){retained_ui::mix(body_signature,panel->m_query);
            retained_ui::mix(body_signature,static_cast<std::uint64_t>(panel->m_found));}
    }
    std::uint64_t command_signature=body_signature;
    if(auto* coupon=dynamic_cast<CouponEditDialog*>(&m_object))
        retained_ui::mix(command_signature,static_cast<std::uint64_t>(coupon->m_adjust_level_value));
    const auto* value_dialog=dynamic_cast<VariantEditDialog*>(&m_object);
    const std::uint64_t footer_signature=value_dialog?static_cast<std::uint64_t>(value_dialog->m_okbtn.as_bool()):1U;
    const ::cw::lib::Rect body_region{0,0,width,350+coupon_extra},command_region{0,350+coupon_extra,width,40},footer_region{0,390+coupon_extra,width,40};
    const bool paint_body=canvas.component_changed("debug.edit.body",body_region,body_signature);
    const bool paint_commands=canvas.component_changed("debug.edit.commands",command_region,command_signature);
    const bool paint_footer=canvas.component_changed("debug.edit.footer",footer_region,footer_signature);
    canvas.capture_base_and_restore();
    // 出力解像度のフレーム。文字は最終表示サイズでラスタライズする。
    auto surface=canvas.layer({0,0,width,height},{245,245,245,255});
    ::cw::dialog::DialogPresentation out;out.route="DEBUG_EDIT";out.title=title();
    if(auto* variant=dynamic_cast<VariantEditDialog*>(&m_object)){
        ::cw::lib::ui::draw_label(surface,variant->m_dialog_label,18,18,15,false,{0,0,0});
        const std::array<String,4> kinds={"数値","文字列","真偽値","式から設定"};
        const std::array<::cw::lib::Rect,4> fields={::cw::lib::Rect{165,55,375,32},
            ::cw::lib::Rect{165,97,375,32},::cw::lib::Rect{165,139,160,32},::cw::lib::Rect{165,181,375,112}};
        const std::array<String,4> values={variant->m_value_num_text,variant->m_value_str_text,
            variant->m_value_bool_value?String("TRUE"):String("FALSE"),variant->m_value_expr_text};
        for(int i=0;i<4;++i){
            const ::cw::lib::Rect choice{18,55+i*42,130,32};
            ::cw::lib::ui::draw_radio(surface,{choice.x,choice.y+9,14,14},i==variant->m_value_kind,true);
            ::cw::lib::ui::draw_label(surface,kinds[i],choice.x+20,choice.y+8,14,false,{0,0,0});
            out.controls.push_back({Action::Auxiliary,TypeNumber+i,static_cast<float>(choice.x),
                static_cast<float>(choice.y),static_cast<float>(choice.width),static_cast<float>(choice.height),
                true,false,"",kinds[i]});
            const bool active=i==variant->m_value_kind;
            auto colour=active ? ::cw::lib::Color{255,255,255,255}
                               : ::cw::lib::Color{232,232,232,255};
            if(active&&((i==0&&!variant->m_value_valid)||(i==3&&variant->m__value_expr_colour.as_bool())))colour={255,255,0,255};
            ::cw::lib::ui::fill_rect(surface,fields[i],colour);::cw::lib::ui::draw_sunken_border(surface,fields[i]);
            if(m_input_focus==i){
                ::cw::lib::ui::fill_rect(surface,{fields[i].x,fields[i].y,fields[i].width,1},{0,120,215,255});
                ::cw::lib::ui::fill_rect(surface,{fields[i].x,fields[i].y+fields[i].height-1,fields[i].width,1},{0,120,215,255});
                ::cw::lib::ui::fill_rect(surface,{fields[i].x,fields[i].y,1,fields[i].height},{0,120,215,255});
                ::cw::lib::ui::fill_rect(surface,{fields[i].x+fields[i].width-1,fields[i].y,1,fields[i].height},{0,120,215,255});
            }
            String display=values[i]+(m_input_focus==i?m_composition:String{});
            if(i==3){
                std::istringstream lines(display);String line;Integer y=fields[i].y+6;
                while(y+16<fields[i].y+fields[i].height&&std::getline(lines,line)){::cw::lib::ui::draw_label(surface,line,fields[i].x+6,y,13,false,{0,0,0});y+=18;}
            }else ::cw::lib::ui::draw_label(surface,display,fields[i].x+6,fields[i].y+8,13,false,{0,0,0});
            out.controls.push_back({Action::Auxiliary,FocusNumber+i,static_cast<float>(fields[i].x),static_cast<float>(fields[i].y),
                static_cast<float>(fields[i].width),static_cast<float>(fields[i].height),active,false,"",kinds[i]});
        }
        if(!variant->m_error_text.empty())::cw::lib::ui::draw_label(surface,variant->m_error_text,18,308,13,false,{180,0,0});
        out.wants_text_input=m_input_focus>=0&&m_input_focus!=2&&m_input_focus==variant->m_value_kind;
        out.input_text=m_input_focus==0?variant->m_value_num_text:m_input_focus==1?variant->m_value_str_text:
            m_input_focus==3?variant->m_value_expr_text:String{};
        out.composition=m_composition;
    } else {
        if(auto* coupon=dynamic_cast<CouponEditDialog*>(&m_object)){
            const String target=valid_index(coupon->m_target_index,coupon->m_targets.size())?coupon->m_targets[coupon->m_target_index]:String{};
            ::cw::dialog::add_presentation_control(out,surface,"◀",{10,10,38,30},Action::Auxiliary,PreviousTarget,true,"button",true);
            ::cw::lib::ui::draw_label(surface,target,60,17,14,false,{0,0,0});
            ::cw::lib::ui::draw_label(surface,coupon->score_summary(),18,329,12,false,{0,0,0});
            ::cw::dialog::add_presentation_control(out,surface,"▶",{500,10,38,30},Action::Auxiliary,NextTarget,true,"button",true);
        }
        const auto data=rows();constexpr Integer row_height=24,visible_rows=k_visible_rows;
        const Integer top=dynamic_cast<CouponEditDialog*>(&m_object)?50:12;
        const bool show_header=dynamic_cast<CouponEditDialog*>(&m_object)||
            dynamic_cast<EditBookmarksForCardEditDialog*>(&m_object);
        const Integer header_height=show_header?24:0;
        const Integer max_scroll=std::max<Integer>(0,static_cast<Integer>(data.size())-visible_rows);
        std::shared_ptr<EditableListCtrl> editable;
        if (const auto* dialog = dynamic_cast<CouponEditDialog*>(&m_object)) editable = dialog->m_values;
        else if (const auto* dialog = dynamic_cast<ListEditDialog*>(&m_object)) editable = dialog->m_values;
        if (editable && editable->m_ensure_visible_row) {
            if (auto* selected = selection()) *selected = editable->m_selected;
            ensure_visible(*editable->m_ensure_visible_row);
            editable->m_ensure_visible_row.reset();
        }
        m_scroll=std::clamp<Integer>(m_scroll,0,max_scroll);
        const ::cw::lib::Rect list_rect{10,top,540,header_height+visible_rows*row_height+4};
        ::cw::lib::ui::fill_rect(surface,list_rect,{255,255,255,255});::cw::lib::ui::draw_sunken_border(surface,list_rect);
        if(show_header){
            ::cw::lib::ui::fill_rect(surface,{12,top+2,536,22},{235,235,235,255});
            if(dynamic_cast<CouponEditDialog*>(&m_object)){
                ::cw::lib::ui::draw_label(surface,"名称",18,top+6,13,true,{0,0,0});
                ::cw::lib::ui::draw_label(surface,"得点",430,top+6,13,true,{0,0,0});
                ::cw::lib::ui::fill_rect(surface,{420,top+2,1,22},{170,170,170,255});
            }else{
                ::cw::lib::ui::draw_label(surface,"シナリオ名",18,top+6,13,true,{0,0,0});
                ::cw::lib::ui::draw_label(surface,"場所",238,top+6,13,true,{0,0,0});
                ::cw::lib::ui::fill_rect(surface,{228,top+2,1,22},{170,170,170,255});
            }
        }
        const auto* selected=selection();
        for(Integer row=0;row<visible_rows;++row){const Integer absolute=m_scroll+row;if(!valid_index(absolute,data.size()))break;
            const ::cw::lib::Rect rect{12,top+2+header_height+row*row_height,536,row_height};
            if(selected&&std::find(selected->begin(),selected->end(),absolute)!=selected->end())::cw::lib::ui::fill_rect(surface,rect,{210,225,248,255});
            if(m_list_focus==11&&absolute==m_list_cursor){
                ::cw::lib::ui::fill_rect(surface,{rect.x,rect.y,rect.width,1},{0,120,215,255});
                ::cw::lib::ui::fill_rect(surface,{rect.x,rect.y+rect.height-1,rect.width,1},{0,120,215,255});
            }
            String row_icon;
            if (const auto* coupon = dynamic_cast<CouponEditDialog*>(&m_object)) {
                const auto image = coupon->m_values && valid_index(absolute, coupon->m_values->m_images.size())
                    ? coupon->m_values->m_images[static_cast<std::size_t>(absolute)] : coupon->m_imgidx_0;
                if (image == coupon->m_imgidx_2) row_icon = "STATUS3_dbg";
                else if (image == coupon->m_imgidx_1) row_icon = "STATUS2_dbg";
                else if (image == coupon->m_imgidx_0) row_icon = "STATUS1_dbg";
                else row_icon = "STATUS0_dbg";
            }
            else if(const auto* d=dynamic_cast<ListEditDialog*>(&m_object))row_icon=d->m_dialog_title.find("ゴシップ")!=String::npos?"GOSSIP":"EVT_GET_COMPLETESTAMP";
            else if(dynamic_cast<BreakpointEditDialog*>(&m_object))row_icon="BREAK";
            else if(dynamic_cast<SavedJPDCImageEditDialog*>(&m_object))row_icon="JPDCIMAGE";
            else if(dynamic_cast<SavedVariablesEditDialog*>(&m_object))row_icon="VARIABLES";
            else if(dynamic_cast<EditBookmarksForCardEditDialog*>(&m_object))row_icon="PACK";
            const Integer icon_y=retained_ui::centered_y(rect,row_icon.ends_with("_dbg")?14:16);
            const Integer text_y=retained_ui::centered_y(rect,13);
            draw_debug_edit_icon(surface,row_icon,rect.x+4,icon_y);
            const Integer text_x=rect.x+(row_icon.empty()?6:24);
            if(data[absolute].size()>1){
                const Integer split=dynamic_cast<CouponEditDialog*>(&m_object)?408:216;
                ::cw::lib::ui::draw_label(surface,data[absolute][0],text_x,text_y,13,false,{0,0,0});
                ::cw::lib::ui::draw_label(surface,data[absolute][1],rect.x+split,text_y,13,false,{0,0,0});
            }else if(!data[absolute].empty())::cw::lib::ui::draw_label(surface,data[absolute][0],text_x,text_y,13,false,{0,0,0});
            if(absolute==m_cell_row){
                const Integer left=m_cell_score?rect.x+408:rect.x;
                const Integer right=m_cell_score||!dynamic_cast<CouponEditDialog*>(&m_object)?rect.x+rect.width:rect.x+408;
                const ::cw::lib::Rect field{left,rect.y,right-left,rect.height};
                auto clip=std::make_shared<::cw::lib::Surface>();
                // 他の一覧テキストと同様、キャンバスの拡大率で描画する。等倍の文字
                // を後から拡大したり、入力座標に二重に拡大率を適用したりしない。
                clip->draw_scale=surface->draw_scale;
                clip->size={static_cast<Integer>(std::lround(field.width*clip->draw_scale)),
                            static_cast<Integer>(std::lround(field.height*clip->draw_scale))};
                clip->rgba.assign(static_cast<std::size_t>(clip->size.width*clip->size.height)*4U,255);
                ::cw::dialog::textedit::Preedit preedit;
                preedit.set(m_cell_edit.composition,m_cell_edit.composition_start,m_cell_edit.composition_length);
                const auto display=::cw::dialog::textedit::display_text(m_cell_edit.text,m_cell_edit.caret,m_cell_edit.anchor,preedit);
                ::cw::dialog::textedit::draw_single_line(clip,display,
                    ::cw::lib::ui::configured_font("debug",13),
                    static_cast<int>((field.height-13)/2),true,m_cell_edit.horizontal);
                ::cw::lib::ui::blend_surface(surface,clip,field.x,field.y);
            }
            if(dynamic_cast<CouponEditDialog*>(&m_object)) {
                out.controls.push_back({Action::Select,static_cast<int>(absolute),static_cast<float>(rect.x),static_cast<float>(rect.y),408.0F,static_cast<float>(rect.height),true,false});
                out.controls.push_back({Action::Select,coupon_value_row_offset+static_cast<int>(absolute),static_cast<float>(rect.x+408),static_cast<float>(rect.y),static_cast<float>(rect.width-408),static_cast<float>(rect.height),true,false});
            } else {
                out.controls.push_back({Action::Select,static_cast<int>(absolute),static_cast<float>(rect.x),static_cast<float>(rect.y),static_cast<float>(rect.width),static_cast<float>(rect.height),true,false});
            }
        }
        if(auto* panel=find_panel()){
            ::cw::lib::ui::draw_label(surface,"検索",10,326+coupon_extra,13,false,{0,0,0});
            const ::cw::lib::Rect field{52,318+coupon_extra,350,28};
            ::cw::lib::ui::fill_rect(surface,field,
                panel->m_found ? ::cw::lib::Color{255,255,255,255}
                               : ::cw::lib::Color{255,128,128,255});
            ::cw::lib::ui::draw_sunken_border(surface,field);
            ::cw::lib::ui::draw_label(surface,panel->m_query+(m_list_focus==10?m_find_composition:String{}),field.x+5,field.y+6,13,false,{0,0,0});
            out.controls.push_back({Action::Auxiliary,1000,static_cast<float>(field.x),static_cast<float>(field.y),static_cast<float>(field.width),static_cast<float>(field.height),true,false});
            ::cw::dialog::add_presentation_control(out,surface,"▲",{408,318+coupon_extra,60,28},Action::Auxiliary,FindUp,!panel->m_query.empty(),"button",true);
            ::cw::dialog::add_presentation_control(out,surface,"▼",{474,318+coupon_extra,60,28},Action::Auxiliary,FindDown,!panel->m_query.empty(),"button",true);
            out.wants_text_input=m_list_focus==10;out.input_text=panel->m_query;out.composition=m_find_composition;
        }
    }
    const auto button=[&](const String& label,Integer x,Integer y,Integer w,int command,bool enabled=true){::cw::dialog::add_presentation_control(out,surface,label,{x,y,w,28},Action::Auxiliary,command,enabled,"button",true);};
    if(!dynamic_cast<VariantEditDialog*>(&m_object)){
        const bool editable=dynamic_cast<CouponEditDialog*>(&m_object)||dynamic_cast<ListEditDialog*>(&m_object);
        if(auto* coupon=dynamic_cast<CouponEditDialog*>(&m_object)) {
            const ::cw::lib::Rect rect{360,10,130,28};
            ::cw::lib::ui::draw_checkbox(surface,{rect.x,rect.y+7,14,14},coupon->m_adjust_level_value);
            ::cw::lib::ui::draw_label(surface,"レベル調整",rect.x+20,rect.y+7,13,false,{0,0,0});
            out.controls.push_back({Action::Auxiliary,ToggleAdjust,static_cast<float>(rect.x),
                static_cast<float>(rect.y),static_cast<float>(rect.width),static_cast<float>(rect.height),
                true,false,"レベル調整",{}});
        }
        if(editable)button("追加",10,354+coupon_extra,58,Add);
        button("削除",editable?72:10,354+coupon_extra,58,Remove,selection()&&!selection()->empty());
        if(dynamic_cast<CouponEditDialog*>(&m_object)||dynamic_cast<ListEditDialog*>(&m_object)||dynamic_cast<EditBookmarksForCardEditDialog*>(&m_object)){
            button("▲",136,354+coupon_extra,45,MoveUp);button("▼",185,354+coupon_extra,45,MoveDown);button("最上",234,354+coupon_extra,58,MoveTop);button("最下",296,354+coupon_extra,58,MoveBottom);
        }
        if(dynamic_cast<EditBookmarksForCardEditDialog*>(&m_object)){button("更新",358,354+coupon_extra,58,Update);button("ファイル",420,354+coupon_extra,70,EditEntry);}
        else if(dynamic_cast<CouponEditDialog*>(&m_object)){button("得点",420,354+coupon_extra,58,ValueEdit);button("複製",482,354+coupon_extra,58,Copy);}
    }
    const auto* variant=dynamic_cast<VariantEditDialog*>(&m_object);
    ::cw::dialog::add_presentation_control(out,surface,"決定",{376,394+coupon_extra,78,28},Action::Accept,-1,
        !variant||variant->m_okbtn.as_bool(),"button",true);
    ::cw::dialog::add_presentation_control(out,surface,"中止",{462,394+coupon_extra,78,28},Action::Cancel,-1,true,"button",true);
    if(m_cell_row>=0){
        out.wants_text_input=true;out.input_text=m_cell_edit.text;
        out.composition=m_cell_edit.composition;
        out.composition_start=m_cell_edit.composition_start;
        out.composition_length=m_cell_edit.composition_length;
    }
    retained_ui::composite_changed(canvas,surface,body_region,paint_body);
    retained_ui::composite_changed(canvas,surface,command_region,paint_commands);
    retained_ui::composite_changed(canvas,surface,footer_region,paint_footer);
    retained_ui::finish(out,canvas,game_scale);return out;
}

void CommonEditPresentation::dialog_presentation_text(const std::string& text,const bool composition)
{
    if(m_cell_row>=0){
        if(m_finished||m_operation_pending||m_document_token)return;
        if(composition){
            const auto& event=last_dialog_text_event();
            m_cell_edit.set_composition(text,event.start,event.length);
        }else m_cell_edit.insert(text,0,false);
        request_dialog_presentation_refresh();return;
    }
    auto* variant=dynamic_cast<VariantEditDialog*>(&m_object);
    if(!variant){
        auto* panel=find_panel();if(!panel||m_finished||m_operation_pending||m_document_token||m_list_focus!=10)return;
        if(composition){m_find_composition=text;request_dialog_presentation_refresh();return;}
        m_find_composition.clear();if(m_find_select_all)panel->m_query.clear();m_find_select_all=false;
        if(text=="\b"){
            if(!panel->m_query.empty()){auto pos=panel->m_query.size()-1;while(pos>0&&(static_cast<unsigned char>(panel->m_query[pos])&0xc0)==0x80)--pos;panel->m_query.erase(pos);}
        }else panel->m_query+=text;
        panel->OnTextChanged(nullptr);request_dialog_presentation_refresh();return;
    }
    if(m_finished||m_operation_pending||m_document_token||m_input_focus<0||m_input_focus==2||m_input_focus!=variant->m_value_kind)return;
    if(composition){m_composition=text;request_dialog_presentation_refresh();return;}
    m_composition.clear();String* draft=m_input_focus==0?&variant->m_value_num_text:
        m_input_focus==1?&variant->m_value_str_text:&variant->m_value_expr_text;
    if(m_select_all)draft->clear();m_select_all=false;
    if(text=="\b"){
        if(!draft->empty()){auto pos=draft->size()-1;while(pos>0&&(static_cast<unsigned char>((*draft)[pos])&0xc0)==0x80)--pos;draft->erase(pos);}
    }else{
        // Py の TextCtrl と同様、Unicode の十進数字や不正な数値文字列
        // も含め、入力・貼り付けした文字を保持する。 妥当性は下の共通 Decimal
        // パーサーで判断し、入力を黙って捨てたり、UI 側でバイト数を制限したりしない。
        *draft+=text;
    }
    update_variant_inline(*variant);request_dialog_presentation_refresh();
}

void CommonEditPresentation::dialog_presentation_key(const ::cw::dialog::DialogPresentationKey key,const bool pressed,
    const ::cw::dialog::DialogPresentationModifiers modifiers)
{
    using Key=::cw::dialog::DialogPresentationKey;
    if(!pressed||m_finished||m_document_token)return;
    m_modifiers=modifiers;
    if(m_cell_row>=0){
        if(!m_cell_edit.composition.empty())return;
        if(key==Key::Enter||key==Key::Escape||key==Key::Tab||key==Key::Up||key==Key::Down){
            const auto row=m_cell_row;const bool score=m_cell_score;
            close_cell_editor();
            if(key==Key::Tab && dynamic_cast<CouponEditDialog*>(&m_object) &&
                ((modifiers&::cw::dialog::DialogPresentationModifierShift)?score:!score))edit_selected(!score);
            else if(key==Key::Up||key==Key::Down){
                const auto next=row+(key==Key::Up?-1:1);
                if(valid_index(next,rows().size())){select_row(static_cast<int>(next));m_scroll=std::clamp<Integer>(m_scroll,std::max<Integer>(0,next-9),next);edit_selected(score);}
            }
        }else ::cw::dialog::textedit::single_line_key(m_cell_edit,key,modifiers,0,
            []{return ::cw::dialog::presentation_clipboard_text();},
            [](const std::string& text){::cw::dialog::presentation_set_clipboard_text(text);});
        request_dialog_presentation_refresh();return;
    }
    if(key==Key::Escape){dialog_presentation_action(::cw::dialog::DialogPresentationAction::Cancel,-1);return;}
    const bool shift=(modifiers&::cw::dialog::DialogPresentationModifierShift)!=0;
    const bool control=(modifiers&::cw::dialog::DialogPresentationModifierControl)!=0;
    auto* variant=dynamic_cast<VariantEditDialog*>(&m_object);
    if(!variant){
        const auto data=rows();auto* selected=selection();auto* panel=find_panel();
        if(control&&key==Key::A&&m_list_focus==10&&panel){m_find_select_all=true;request_dialog_presentation_refresh();return;}
        if((key==Key::Backspace||key==Key::Delete)&&m_list_focus==10){dialog_presentation_text("\b",false);return;}
        if(key==Key::Tab){
            std::vector<int> order{11};if(panel)order.push_back(10);
            if(dynamic_cast<CouponEditDialog*>(&m_object)||dynamic_cast<ListEditDialog*>(&m_object))order.push_back(100+Add);
            order.push_back(100+Remove);
            if(dynamic_cast<CouponEditDialog*>(&m_object)||dynamic_cast<ListEditDialog*>(&m_object)||dynamic_cast<EditBookmarksForCardEditDialog*>(&m_object)){
                order.push_back(100+MoveUp);order.push_back(100+MoveDown);order.push_back(100+MoveTop);order.push_back(100+MoveBottom);
            }
            order.push_back(200);order.push_back(201);auto it=std::find(order.begin(),order.end(),m_list_focus);
            auto pos=it==order.end()?std::size_t{0}:static_cast<std::size_t>(std::distance(order.begin(),it));
            pos=(pos+(shift?order.size()-1:1))%order.size();m_list_focus=order[pos];m_find_select_all=m_list_focus==10;
            request_dialog_presentation_refresh();return;
        }
        if(m_list_focus==10&&panel){
            if(key==Key::Enter){
                if(shift)panel->find_up();else panel->find_down();
                if(panel->m_found && panel->m_cursor>=0){
                    m_list_cursor=panel->m_cursor;m_scroll=std::max<Integer>(0,m_list_cursor-5);
                    if(selected){*selected={m_list_cursor};m_selection_anchor=m_list_cursor;selection_changed();}
                }
                request_dialog_presentation_refresh();
            }
            return;
        }
        if(m_list_focus==11&&!data.empty()){
            Integer next=m_list_cursor;
            if(key==Key::Up)--next;else if(key==Key::Down)++next;else if(key==Key::Home)next=0;
            else if(key==Key::End)next=static_cast<Integer>(data.size())-1;else if(key==Key::PageUp)next-=10;else if(key==Key::PageDown)next+=10;
            else if(key==Key::Space){select_row(m_list_cursor);return;}
            else if(key==Key::Enter){edit_selected();return;}else if(key==Key::Delete){command(Remove);return;}else return;
            next=std::clamp<Integer>(next,0,static_cast<Integer>(data.size())-1);m_list_cursor=next;
            if(selected&&!control){
                if(shift&&m_selection_anchor>=0){selected->clear();const auto a=std::min<Integer>(m_selection_anchor,next),b=std::max<Integer>(m_selection_anchor,next);for(Integer i=a;i<=b;++i)selected->push_back(i);}
                else{*selected={next};m_selection_anchor=next;}selection_changed();
            }
            m_scroll=std::clamp<Integer>(m_scroll,std::max<Integer>(0,next-9),next);request_dialog_presentation_refresh();return;
        }
        if(key==Key::Enter||key==Key::Space){
            if(m_list_focus>=100&&m_list_focus<200)command(m_list_focus-100);
            else if(m_list_focus==200)dialog_presentation_action(::cw::dialog::DialogPresentationAction::Accept,-1);
            else if(m_list_focus==201)dialog_presentation_action(::cw::dialog::DialogPresentationAction::Cancel,-1);
        }
        return;
    }
    if(key==Key::Tab){m_input_focus=(m_input_focus+(shift?3:1)+4)%4;variant->m_value_kind=m_input_focus;m_select_all=true;update_variant_inline(*variant);request_dialog_presentation_refresh();return;}
    if(control&&key==Key::A&&m_input_focus!=2){m_select_all=true;request_dialog_presentation_refresh();return;}
    if(key==Key::Backspace||key==Key::Delete){dialog_presentation_text("\b",false);return;}
    if(key==Key::Space&&m_input_focus==2){variant->m_value_bool_value=!variant->m_value_bool_value;update_variant_inline(*variant);request_dialog_presentation_refresh();return;}
    if(key==Key::Enter&&m_input_focus==3&&shift){dialog_presentation_text("\n",false);return;}
    if(key==Key::Enter)dialog_presentation_enter(modifiers);
}

void CommonEditPresentation::dialog_presentation_enter(::cw::dialog::DialogPresentationModifiers)
{
    if(m_cell_row>=0){if(m_cell_edit.composition.empty())close_cell_editor();return;}
    dialog_presentation_action(::cw::dialog::DialogPresentationAction::Accept,-1);
}

void CommonEditPresentation::dialog_presentation_pointer(
    const ::cw::dialog::DialogPresentationAction action,const int index,
    const float x,const float y,const bool pressed)
{
    (void)y;
    using Action=::cw::dialog::DialogPresentationAction;
    if(m_cell_row>=0 && action==Action::Select &&
       index==static_cast<int>(m_cell_row)+(m_cell_score?coupon_value_row_offset:0)){
        if(!pressed && m_cell_edit.composition.empty()){
            const auto* owner=dynamic_cast<const ::cw::lib::Dialog*>(&m_object);
            const auto scale=::cw::debug::retained_presentation::presentation_scale(owner&&owner->game_window_scale);
            const auto logical_x=x/scale;
            const Integer left=m_cell_score?420:12;
            const auto target=logical_x-left+m_cell_edit.horizontal;
            const auto font=::cw::lib::ui::configured_font("debug",13);
            std::size_t pos=0;Integer previous_width=0;
            while(pos<m_cell_edit.text.size()){
                const auto next=::cw::dialog::textedit::next(m_cell_edit.text,pos);
                const auto width=::cw::lib::ui::label_width(m_cell_edit.text.substr(0,next),font.pixels,font.bold,font.face,font.italic);
                if(target<(previous_width+width)/2.0)break;
                pos=next;previous_width=width;
            }
            m_cell_edit.move(pos,(m_modifiers&::cw::dialog::DialogPresentationModifierShift)!=0);
            request_dialog_presentation_refresh();
        }
        return;
    }
    if(!pressed)dialog_presentation_action(action,index);
}

void CommonEditPresentation::dialog_presentation_action(
    const ::cw::dialog::DialogPresentationAction action,const int index)
{
    using Action=::cw::dialog::DialogPresentationAction;
    if (m_finished || m_operation_pending || m_document_token) return;
    struct PendingScope {
        bool& pending;
        explicit PendingScope(bool& value) : pending(value) { pending = true; }
        ~PendingScope() { pending = false; }
    } operation(m_operation_pending);
    if(m_cell_row>=0 && action==Action::Select &&
        index==static_cast<int>(m_cell_row)+(m_cell_score?coupon_value_row_offset:0))return;
    if(m_cell_row>=0)close_cell_editor();
    if(action==Action::Cancel){dialog_presentation_complete();return;}
    if(action==Action::Accept){accept();return;}
    if(action==Action::Select || action==Action::Activate){
        const bool coupon = dynamic_cast<CouponEditDialog*>(&m_object) != nullptr;
        const bool editable = coupon || dynamic_cast<ListEditDialog*>(&m_object) != nullptr;
        if(action==Action::Activate&&!editable)return;
        const bool score_cell = coupon && index >= coupon_value_row_offset;
        const int row = score_cell ? index - coupon_value_row_offset : index;
        const auto data = rows();
        if(!valid_index(row,data.size()))return;
        const auto* selected=selection();
        const bool already_selected=selected&&
            std::find(selected->begin(),selected->end(),row)!=selected->end()&&m_list_cursor==row;
        const bool modified=(m_modifiers&(::cw::dialog::DialogPresentationModifierShift|
            ::cw::dialog::DialogPresentationModifierControl))!=0;
        select_row(row);
        // Python の TextEditMixin.OnLeftDown と同様
        // 、現在行を再度押すと、その列の編集を開始する。 Ctrl/Shift では
        // 複数選択を維持する。ALL と単体版で同じインラインセルエディターを使う。
        if(editable && !modified && (already_selected||action==Action::Activate)) {
            edit_selected(score_cell);
        }
        request_dialog_presentation_refresh();
        return;
    }
    if(action==Action::Previous){m_scroll=std::max<Integer>(0,m_scroll-6);return;}
    if(action==Action::Next){m_scroll+=6;return;}
    if(action==Action::Auxiliary){
        if(index==1000){m_list_focus=10;m_find_select_all=true;request_dialog_presentation_refresh();return;}
        if(!dynamic_cast<VariantEditDialog*>(&m_object))m_list_focus=100+index;
        command(index);
    }
}

} // namespace

void AutoWidthListCtrl::initialize(std::shared_ptr<BreakpointEditDialog> p,Integer cid,std::tuple<Integer,Integer> size,Integer style){(void)p;(void)cid;(void)size;(void)style;m_rows.clear();m_images.clear();m_selected.clear();}

void BreakpointEditDialog::initialize(std::shared_ptr<::cw::debug::debugger::Debugger> parent,std::map<std::tuple<String,String>,std::set<String>> table,std::uintptr_t native_parent_handle)
{
    m_native_parent_handle = native_parent_handle;
    if (!m_native_parent_handle && parent) {
        if (const auto frame = parent->m_parent.lock())
            m_native_parent_handle = frame->m_native_window_handle;
    }
    m_cwpy_debug = true;
    m_modal_accepted = false;
    m__removed.clear();
    m_selected.clear();
    m_list.clear();
    for (const auto& [key, unused] : table) {
        (void)unused;
        m_list.push_back(key);
    }
    std::stable_sort(m_list.begin(), m_list.end(), natural_tuple_less);
    m_values = std::make_shared<AutoWidthListCtrl>();
    m_values->initialize(nullptr, -1, {250, 300}, 0);
    fill_rows(m_values, m_list);
    m_find = std::make_shared<FindPanel>();
    m_find->initialize(nullptr, m_values,
                       [this](const auto&, const auto&) { _item_selected(); return Value{}; }, 0);
    _bind();
    _do_layout();
}
void BreakpointEditDialog::_bind(){}
void BreakpointEditDialog::_do_layout(){}
void BreakpointEditDialog::OnRemoveBtn(std::shared_ptr<::cw::lib::Window> event)
{
    (void)event;
    remove_selected_rows(m_list, m_selected, m__removed);
    fill_rows(m_values, m_list);
    if (m_values) m_values->m_selected.clear();
}
void BreakpointEditDialog::OnOkBtn(std::shared_ptr<::cw::lib::Window> event)
{
    // ネイティブ版は、確定した削除キーの vector をモーダル破棄後
    // も保持する。Py は既存のリストを渡すが、 共通実装のコールバック
    // はダイアログの記憶領域を借りず、値のスナップショットを所有する。
    (void)event;
    m_modal_accepted = true;
    const auto removed = m__removed;
    if (::cw::g_cwpy) {
        ::cw::g_cwpy->exec_func([removed](const auto&, const auto&) {
            BreakpointEditDialog_OnOkBtn_func(removed);
            return Value{};
        });
    } else {
        BreakpointEditDialog_OnOkBtn_func(removed);
    }
}
void BreakpointEditDialog::OnItemSelected(std::shared_ptr<::cw::lib::Window> event){(void)event;_item_selected();}
// Py は実体の _removed リストを返す。ネイティブの値返却 API は独立した vector
// を返し、 呼び出し元や予約済み処理が破棄されたモーダルのデータを参照しないようにする。
std::vector<std::tuple<String,String>> BreakpointEditDialog::get_removed(){return m__removed;}
std::vector<Integer> BreakpointEditDialog::get_selectedindexes(){set_selected(m_selected,m_list.size());return m_selected;}
void BreakpointEditDialog::_item_selected()
{
    if (m_values) m_selected = m_values->m_selected;
    set_selected(m_selected,m_list.size());
}

// 共通モデルは wx のグローバル状態とは独立して初期化する。実行環境がない場合
// 、対象一覧は All のみとし、 Character 以外のカードは除外する
// 。メッセージ表がない場合は、通常の先頭番号の称号表記を使う。 これらはネイテ
// ィブ版の状態欠落時の既定動作であり、Py のアサートを再現するものではない。
void CouponEditDialog::initialize(std::shared_ptr<::cw::lib::Window> parent,Integer selected)
{
    m_native_parent_handle = parent ? parent->native_handle : 0;
    m_cwpy_debug = true;
    m_modal_accepted = false;
    m__processing = false;
    m_target_index = std::max<Integer>(0, selected + 1);
    m_adjust_level_value = false;
    // Py と同じ順序で画像一覧へ追加し、点数の区分を区別する。
    m_imgidx_2 = 0; m_imgidx_1 = 1; m_imgidx_0 = 2; m_imgidx_m1 = 3;
    m_selected.clear();
    m_coupons.clear();
    m_pcards.clear();
    m_targets = {"全員"};
    m_syscoupons.clear();
    for (const auto& name : ::cw::setting::Setting::s_sexcoupons) m_syscoupons.insert(name);
    for (const auto& name : ::cw::setting::Setting::s_periodcoupons) m_syscoupons.insert(name);
    for (const auto& name : ::cw::setting::Setting::s_naturecoupons) m_syscoupons.insert(name);
    for (const auto& name : ::cw::setting::Setting::s_makingcoupons) m_syscoupons.insert(name);
    const auto number_1_coupon = ::cw::g_cwpy && ::cw::g_cwpy->m_setting &&
        ::cw::g_cwpy->m_setting->m_msgs
        ? ::cw::g_cwpy->m_setting->m_msgs->get("number_1_coupon", "＿１")
        : String("＿１");
    for (const auto& name : {number_1_coupon, String("＿２"), String("＿３"),
                             String("＿４"), String("＿５"), String("＿６")})
        m_syscoupons.insert(name);
    if (::cw::g_cwpy) {
        for (const auto& pcard : ::cw::g_cwpy->get_pcards({})) {
            auto character = std::dynamic_pointer_cast<::cw::character::Character>(pcard);
            if (!character) continue;
            m_pcards.push_back(character);
            auto coupons = _get_coupons(std::dynamic_pointer_cast<::cw::character::Player>(character));
            std::reverse(coupons.begin(), coupons.end());
            m_coupons.push_back(std::move(coupons));
            m_targets.push_back(character->m_name);
        }
    }
    m_target_index = std::min<Integer>(m_target_index, static_cast<Integer>(m_pcards.size()));
    m_values = std::make_shared<EditableListCtrl>();
    m_values->initialize(nullptr, -1, {250, 300}, 0);
    m_find = std::make_shared<FindPanel>();
    m_find->initialize(nullptr, m_values,
                       [this](const auto&, const auto&) { _item_selected(); return Value{}; }, 0);
    _select_target();
    _bind();
    _do_layout();
}
void CouponEditDialog::_bind(){}
void CouponEditDialog::_do_layout(){}
void CouponEditDialog::OnSelectTarget(std::shared_ptr<::cw::lib::Window> event){(void)event;_select_target();}
void CouponEditDialog::OnLeftBtn(std::shared_ptr<::cw::lib::Window> event){(void)event;if(m_target_index<=0)m_target_index=static_cast<Integer>(m_pcards.size());else--m_target_index;_select_target();}
void CouponEditDialog::OnRightBtn(std::shared_ptr<::cw::lib::Window> event){(void)event;if(m_target_index>=static_cast<Integer>(m_pcards.size()))m_target_index=0;else++m_target_index;_select_target();}
void CouponEditDialog::OnItemSelected(std::shared_ptr<::cw::lib::Window> event){(void)event;if(!m__processing)_item_selected();}
// 共通の行モデルで、wx の ListCtrl/Tree によるフォーカス
// と編集を置き換える。 追加時は行を再構築して共通の名前編集を開き、削除や
// 点数操作は行の値と正規化済み選択を使う。 Presentation は切
// り離され得るため、存在しないコントロールや無効なメンバー位置を確認する。
void CouponEditDialog::OnAddBtn(std::shared_ptr<::cw::lib::Window> event)
{
    (void)event;
    std::set<String> names;
    if (m_values) {
        for (const auto& row : m_values->m_rows)
            if (!row.empty()) names.insert(row.front());
    }
    Integer number = 1;
    String name;
    do name = "新規項目 (" + std::to_string(number++) + ")";
    while (names.contains(name));
    if (m_target_index == 0) {
        for (auto& sequence : m_coupons)
            sequence.insert(sequence.begin(), {name, 0});
    } else if (valid_index(m_target_index - 1, m_coupons.size())) {
        auto& sequence = m_coupons[static_cast<std::size_t>(m_target_index - 1)];
        sequence.insert(sequence.begin(), {name, 0});
    }
    _select_target();
    m_selected = {0};
    if (m_values && m_values->m_rows.size() > 0)
        m_values->OpenEditor(0, 0);
}
void CouponEditDialog::OnRemoveBtn(std::shared_ptr<::cw::lib::Window> event)
{
    (void)event;
    set_selected(m_selected, m_values ? m_values->m_rows.size() : 0);
    if (m_target_index == 0) {
        std::vector<String> names;
        for (const auto index : m_selected)
            if (m_values && valid_index(index, m_values->m_rows.size()) &&
                !m_values->m_rows[static_cast<std::size_t>(index)].empty())
                names.push_back(m_values->m_rows[static_cast<std::size_t>(index)].front());
        for (auto& sequence : m_coupons) {
            for (const auto& name : names) {
                const auto found = std::find_if(sequence.begin(), sequence.end(),
                    [&](const Coupon& coupon) { return std::get<0>(coupon) == name; });
                if (found != sequence.end())
                    sequence.erase(found);
            }
        }
    } else if (valid_index(m_target_index - 1, m_coupons.size())) {
        auto& sequence = m_coupons[static_cast<std::size_t>(m_target_index - 1)];
        for (auto it = m_selected.rbegin(); it != m_selected.rend(); ++it)
            if (valid_index(*it, sequence.size()))
                sequence.erase(sequence.begin() + *it);
    }
    m_selected.clear();
    _select_target();
}
void CouponEditDialog::_remove_coupon(Integer index){m_selected={index};OnRemoveBtn(nullptr);}
void CouponEditDialog::OnCopyBtn(std::shared_ptr<::cw::lib::Window> event)
{
    // Python のダイアログはコピー元のメンバーをユーザーに選ば
    // せる。明示的な選択のないモデルコールバックで、 二人目を自動的
    // に選ばない。暗黙に選ぶと全メンバーを上書きするおそれがある。
    if (!event || !valid_index(event->id, m_coupons.size()))
        return;
    const auto source = static_cast<std::size_t>(event->id);
    if (m_target_index == 0) {
        for (auto& sequence : m_coupons)
            sequence = m_coupons[source];
    } else if (valid_index(m_target_index - 1, m_coupons.size())) {
        m_coupons[static_cast<std::size_t>(m_target_index - 1)] = m_coupons[source];
    }
    _select_target();
}
void CouponEditDialog::OnValueBtn(std::shared_ptr<::cw::lib::Window> event)
{
    (void)event;
    const auto indexes = get_selectedindexes();
    if (indexes.empty() || !m_values)
        return;
    const auto& row = m_values->m_rows[static_cast<std::size_t>(indexes.front())];
    if (row.size() < 2)
        return;
    const auto value = value_integer(Value(row[1]));
    if (!value) return;
    const auto accepted = ::cw::dialog::edit::show_common_modal_number_input(
        "得点の設定", *value, -9, 9, 1);
    if (accepted) {
        for (const auto index : indexes) _set_value(index, *accepted);
        _item_selected();
    }
}
void CouponEditDialog::OnUpBtn(std::shared_ptr<::cw::lib::Window> event){(void)event;if(m_target_index==0||!valid_index(m_target_index-1,m_coupons.size()))return;set_selected(m_selected,m_coupons[m_target_index-1].size());if(m_selected.empty()||m_selected.front()<1)return;for(auto i:m_selected)_swap(i,i-1);}
void CouponEditDialog::OnDownBtn(std::shared_ptr<::cw::lib::Window> event){(void)event;if(m_target_index==0||!valid_index(m_target_index-1,m_coupons.size()))return;auto&seq=m_coupons[m_target_index-1];set_selected(m_selected,seq.size());if(m_selected.empty()||m_selected.back()+1>=static_cast<Integer>(seq.size()))return;for(auto it=m_selected.rbegin();it!=m_selected.rend();++it)_swap(*it,*it+1);}
// 入れ替えでは wx の項目設定や EnsureVisible の代
// わりに、共通の行・画像・選択位置を直接更新する。 両方の元データ位
// 置を検証するが、ここでは wx のスクロール動作までは再現しない。
void CouponEditDialog::_swap(Integer a,Integer b)
{
    if (m_target_index == 0 || !valid_index(m_target_index - 1, m_coupons.size()))
        return;
    auto& sequence = m_coupons[static_cast<std::size_t>(m_target_index - 1)];
    if (!valid_index(a, sequence.size()) || !valid_index(b, sequence.size()))
        throw std::out_of_range("coupon index");
    std::swap(sequence[static_cast<std::size_t>(a)], sequence[static_cast<std::size_t>(b)]);
    if (m_values && valid_index(a, m_values->m_rows.size()) &&
        valid_index(b, m_values->m_rows.size())) {
        const auto update_row = [&](Integer index) {
            const auto& coupon = sequence[static_cast<std::size_t>(index)];
            m_values->m_rows[static_cast<std::size_t>(index)] = {
                std::get<0>(coupon), std::to_string(std::get<1>(coupon))};
            if (valid_index(index, m_values->m_images.size()))
                m_values->m_images[static_cast<std::size_t>(index)] =
                    _get_valueimage(std::get<1>(coupon));
        };
        update_row(a);
        update_row(b);
    }
    for (auto& index : m_selected)
        if (index == a) index = b;
        else if (index == b) index = a;
}
void CouponEditDialog::OnUp2Btn(std::shared_ptr<::cw::lib::Window> event)
{
    (void)event;
    if (m_target_index == 0 || !valid_index(m_target_index - 1, m_coupons.size())) return;
    m__processing = true;
    auto selected = get_selectedindexes();
    move_to_top(m_coupons[static_cast<std::size_t>(m_target_index - 1)], selected);
    _select_target();
    if (m_values) m_values->m_selected = selected;
    m__processing = false;
    _item_selected();
}
void CouponEditDialog::OnDown2Btn(std::shared_ptr<::cw::lib::Window> event)
{
    (void)event;
    if (m_target_index == 0 || !valid_index(m_target_index - 1, m_coupons.size())) return;
    m__processing = true;
    auto selected = get_selectedindexes();
    move_to_bottom(m_coupons[static_cast<std::size_t>(m_target_index - 1)], selected);
    _select_target();
    if (m_values) m_values->m_selected = selected;
    m__processing = false;
    _item_selected();
}
void CouponEditDialog::OnEndLabelEdit(std::shared_ptr<::cw::lib::Window> event)
{
    if (!event || !m_values || !valid_index(event->id, m_values->m_rows.size()))
        return;
    const auto index = static_cast<std::size_t>(event->id);
    const auto duplicate = std::any_of(m_values->m_rows.begin(), m_values->m_rows.end(),
        [&](const auto& row) {
            return !row.empty() && row.front() == event->title;
        });
    if (!event->title.empty() && !duplicate && !m_values->m_rows[index].empty())
        _set_name(event->id, m_values->m_rows[index].front(), event->title);
    // Python と同様、名前の編集が拒否された場合も選択と点数集計を更新する。
    _item_selected();
}
// ゲームワーカーへの書き込みを予約する前に、確定した編集値をコピーする。 コールバッ
// クは保持中のダイアログモデルに依存せず、メンバー・称号・システム称号の値をスナップ
// ショットで渡す。 ゲーム側が処理する前に共通モーダルが破棄される可能性がある。Py
// はダイアログやリストをクロージャーに保持する。 任意精度整数の Py と異なる
// ネイティブの点数・レベルの範囲制限は、score_summary に記載する。
void CouponEditDialog::OnOkBtn(std::shared_ptr<::cw::lib::Window> event)
{
    (void)event;
    m_modal_accepted = true;
    const auto pcards = m_pcards;
    const auto coupons = m_coupons;
    const auto syscoupons = m_syscoupons;
    const auto cindex = m_target_index - 1;
    const auto adjust_level = m_adjust_level_value;
    if (::cw::g_cwpy) {
        ::cw::g_cwpy->exec_func(
            [pcards, coupons, syscoupons, cindex, adjust_level](const auto&, const auto&) {
                CouponEditDialog_OnOkBtn_func(pcards, coupons, syscoupons, cindex, adjust_level);
                return Value{};
            });
    } else {
        CouponEditDialog_OnOkBtn_func(pcards, coupons, syscoupons, cindex, adjust_level);
    }
}
std::vector<Integer> CouponEditDialog::get_selectedindexes(){if(m_values)m_selected=m_values->m_selected;set_selected(m_selected,m_values?m_values->m_rows.size():0);return m_selected;}
Integer CouponEditDialog::_get_valueimage(Integer value){if(value>=2)return m_imgidx_2;if(value>=1)return m_imgidx_1;if(value>=0)return m_imgidx_0;return m_imgidx_m1;}
void CouponEditDialog::_append_couponlist(String name,Integer value){if(m_values){m_values->m_rows.push_back({std::move(name),std::to_string(value)});m_values->m_images.push_back(_get_valueimage(value));}}
void CouponEditDialog::_select_target(){if(!m_values)return;m_values->m_rows.clear();m_values->m_images.clear();m_values->m_selected.clear();m_selected.clear();if(m_target_index==0){std::set<String>seen;for(const auto&seq:m_coupons)for(const auto&[name,value]:seq)if(seen.insert(name).second)_append_couponlist(name,value);}else if(valid_index(m_target_index-1,m_coupons.size()))for(const auto&[name,value]:m_coupons[m_target_index-1])_append_couponlist(name,value);}
// ネイティブのモデル境界では、Player/XML/Coupons の欠落や
// null・空の称号ノードを行にしない。 Py は Player のデー
// タへ直接アクセスし、空の名前も保持する。空でない通常値は Python
// の整数構文で厳密に解析し、 不正または欠落した点数を黙ってゼロにしない。
std::vector<Coupon> CouponEditDialog::_get_coupons(std::shared_ptr<::cw::character::Player> pcard)
{
    std::vector<Coupon> result;
    if (!pcard || !pcard->m_data_typed)
        return result;
    const auto coupons = pcard->m_data_typed->find("Property/Coupons");
    if (!coupons || !*coupons)
        return result;
    for (const auto& coupon : (*coupons)->m_children) {
        if (!coupon || coupon->m_text.empty())
            continue;
        const auto& name = coupon->m_text;
        if (name.rfind("＠", 0) == 0 || m_syscoupons.contains(name))
            continue;
        // Python の debug/edit.py はここで int(e.get("value"))
        // を呼ぶ。 不正または欠落したキャラクターデータを、有効なゼロ点の称号に変えない。
        const auto value = required_python_integer(
            coupon->get("value", ""), "invalid Coupon value: ");
        result.emplace_back(name, value);
    }
    return result;
}
// Python の整数は任意精度なので、ネイティブの Integer の範囲外となる合計点やレベルは拒否し、集計値を桁あふれさせない。
String CouponEditDialog::score_summary() const
{
    Integer total=0;
    const bool selected_only=m_selected.size()>=2;
    if(m_values) for(std::size_t i=0;i<m_values->m_rows.size();++i) {
        if(selected_only && std::find(m_selected.begin(),m_selected.end(),static_cast<Integer>(i))==m_selected.end())continue;
        const auto& row=m_values->m_rows[i];if(row.size()<2)continue;
        const auto parsed=::cw::lib::parse_python_integer(row[1]);
        if(!parsed)throw std::invalid_argument("coupon score is not an integer");
        if((*parsed>0 && total>std::numeric_limits<Integer>::max()-*parsed) ||
           (*parsed<0 && total<std::numeric_limits<Integer>::min()-*parsed))
            throw std::overflow_error("coupon total outside C++ integer range");
        total+=*parsed;
    }
    Real coefficient=1.0;
    if(m_target_index>0 && valid_index(m_target_index-1,m_pcards.size()) && m_pcards[m_target_index-1])
        coefficient=m_pcards[m_target_index-1]->get_levelcoeff();
    // 不正な拡張値に対する保護。係数がゼロ以下だと Python では無
    // 限ループになるが、通常の Character の係数は正である。
    if(!std::isfinite(coefficient) || coefficient<=0)throw std::invalid_argument("invalid level coefficient");
    const auto threshold=[&](Integer level) {
        std::ostringstream number;number<<std::fixed<<std::setprecision(0)
            <<std::floor(static_cast<Real>(level)*(static_cast<Real>(level)+1)*coefficient);
        return ::cw::lib::Decimal(number.str());
    };
    const ::cw::lib::Decimal points(total);
    // 合計点を閾値が初めて上回るレベルを求める。大きく
    // なり得る点数に比例した回数のループは使わない。
    Integer first=1,last=std::numeric_limits<Integer>::max();
    while(first<last){const auto mid=first+(last-first)/2;if(points.compare(threshold(mid))<0)last=mid;else first=mid+1;}
    if(first==std::numeric_limits<Integer>::max())throw std::overflow_error("coupon level outside C++ integer range");
    ::cw::lib::DecimalContext exact;exact.precision=0;
    const auto remaining=threshold(first).subtract(points,exact).to_string();
    return String(selected_only?"選択中の合計: ":"合計: ")+std::to_string(total)+"点(レベル"+std::to_string(first)+
        "相当 レベル"+std::to_string(first+1)+"まで"+remaining+"点)";
}
void CouponEditDialog::_item_selected(){if(m_values)m_selected=m_values->m_selected;set_selected(m_selected,m_values?m_values->m_rows.size():0);m_total=score_summary();}
// 名前・点数の設定は wx のセルではなく共通の行モデルを更新する。無効な
// 行・メンバー位置に対する更新は省く。 全メンバーへの編集では、変更前ま
// たは現在の名前に一致する最初の称号を対象とする。 ネイティブの名前直接
// 設定は、Py の _set_name とは異なり、空の名前を拒否する。
void CouponEditDialog::_set_name(Integer index,String oldname,String newname)
{
    if (newname.empty())
        throw std::invalid_argument("coupon name is empty");
    if (m_values && valid_index(index, m_values->m_rows.size()) &&
        m_values->m_rows[static_cast<std::size_t>(index)].size() > 0)
        m_values->m_rows[static_cast<std::size_t>(index)][0] = newname;
    if (m_target_index == 0) {
        for (auto& sequence : m_coupons)
            for (auto& coupon : sequence)
                if (std::get<0>(coupon) == oldname) {
                    std::get<0>(coupon) = newname;
                    break;
                }
    } else if (valid_index(m_target_index - 1, m_coupons.size()) &&
               valid_index(index, m_coupons[static_cast<std::size_t>(m_target_index - 1)].size())) {
        std::get<0>(m_coupons[static_cast<std::size_t>(m_target_index - 1)]
                                      [static_cast<std::size_t>(index)]) = newname;
    }
}
void CouponEditDialog::_set_value(Integer index,Integer value)
{
    if (m_values && valid_index(index, m_values->m_rows.size())) {
        auto& row = m_values->m_rows[static_cast<std::size_t>(index)];
        if (row.size() < 2) row.resize(2);
        row[1] = std::to_string(value);
        if (valid_index(index, m_values->m_images.size()))
            m_values->m_images[static_cast<std::size_t>(index)] = _get_valueimage(value);
    }
    const String name = m_values && valid_index(index, m_values->m_rows.size()) &&
                               !m_values->m_rows[static_cast<std::size_t>(index)].empty()
                           ? m_values->m_rows[static_cast<std::size_t>(index)].front()
                           : String{};
    if (m_target_index == 0) {
        for (auto& sequence : m_coupons) {
            const auto found = std::find_if(sequence.begin(), sequence.end(),
                [&](const Coupon& coupon) { return std::get<0>(coupon) == name; });
            if (found != sequence.end()) std::get<1>(*found) = value;
        }
    } else if (valid_index(m_target_index - 1, m_coupons.size()) &&
               valid_index(index, m_coupons[static_cast<std::size_t>(m_target_index - 1)].size())) {
        std::get<1>(m_coupons[static_cast<std::size_t>(m_target_index - 1)]
                                  [static_cast<std::size_t>(index)]) = value;
    }
}

void ListEditDialog::initialize(std::shared_ptr<::cw::lib::Window> parent,String dialog_title,std::vector<String> list,std::shared_ptr<::cw::lib::Bitmap> image)
{
    (void)image;
    m_native_parent_handle = parent ? parent->native_handle : 0;
    m_dialog_title = std::move(dialog_title);
    m_cwpy_debug = true;
    m_modal_accepted = false;
    m__processing = false;
    // Py は渡されたリストを参照で保持するが、このネイティブ API は vector
    // を値で受け取る。 共通ダイアログが行のスナップショットを所有し、呼び出し元の
    // 終了後も無効な行参照が残らないようにする。 そのため、後から呼び出し元の vector
    // を変更してもダイアログは変わらず、受け取った順序と値を維持する。
    m_list = std::move(list);
    m_selected.clear();
    m_values = std::make_shared<EditableListCtrl>();
    m_values->initialize(nullptr, -1, {250, 300}, 0);
    fill_rows(m_values, m_list);
    m_find = std::make_shared<FindPanel>();
    m_find->initialize(nullptr, m_values,
                       [this](const auto&, const auto&) { _item_selected(); return Value{}; }, 0);
    _bind();
    _do_layout();
}
void ListEditDialog::_bind(){}
void ListEditDialog::_do_layout(){}
// 共通の一覧編集では、wx の項目・フォーカス操作の代わりに、所有する行
// データへ挿入し、 共通表示を再構築してインラインセルを選択する。削除時
// は保存済みの選択を正規化する。 一段ずつの移動では一律の検査を追加せず
// 、clean_indexes に記した UI 側の選択条件を維持する。
void ListEditDialog::OnAddBtn(std::shared_ptr<::cw::lib::Window> event)
{
    (void)event;
    std::set<String> names(m_list.begin(), m_list.end());
    Integer number = 1;
    String name;
    do name = "新規項目 (" + std::to_string(number++) + ")";
    while (names.contains(name));
    m_list.insert(m_list.begin(), name);
    fill_rows(m_values, m_list);
    m_selected = {0};
    if (m_values)
        m_values->OpenEditor(0, 0);
}
void ListEditDialog::OnRemoveBtn(std::shared_ptr<::cw::lib::Window> event){(void)event;set_selected(m_selected,m_list.size());for(auto it=m_selected.rbegin();it!=m_selected.rend();++it)m_list.erase(m_list.begin()+*it);m_selected.clear();fill_rows(m_values,m_list);}
void ListEditDialog::OnUpBtn(std::shared_ptr<::cw::lib::Window> event){(void)event;if(!m_selected.empty()&&m_selected.front()>0){for(auto i:m_selected)std::swap(m_list[i],m_list[i-1]);for(auto&i:m_selected)--i;fill_rows(m_values,m_list);if(m_values)m_values->m_selected=m_selected;}}
void ListEditDialog::OnDownBtn(std::shared_ptr<::cw::lib::Window> event){(void)event;if(!m_selected.empty()&&m_selected.back()+1<static_cast<Integer>(m_list.size())){for(auto it=m_selected.rbegin();it!=m_selected.rend();++it)std::swap(m_list[*it],m_list[*it+1]);for(auto&i:m_selected)++i;fill_rows(m_values,m_list);if(m_values)m_values->m_selected=m_selected;}}
void ListEditDialog::_swap(Integer a,Integer b){if(!valid_index(a,m_list.size())||!valid_index(b,m_list.size()))throw std::out_of_range("list index");std::swap(m_list[a],m_list[b]);}
void ListEditDialog::OnUp2Btn(std::shared_ptr<::cw::lib::Window> event){(void)event;move_to_top(m_list,m_selected);fill_rows(m_values,m_list);if(m_values)m_values->m_selected=m_selected;}
void ListEditDialog::OnDown2Btn(std::shared_ptr<::cw::lib::Window> event){(void)event;move_to_bottom(m_list,m_selected);fill_rows(m_values,m_list);if(m_values)m_values->m_selected=m_selected;}
// 共通のインライン編集イベントが存在しない場合や、削除済みの行を指す場合
// は確定を無視する。 空でなく重複のない文字列で行のスナップショットを更
// 新し、wx のラベル編集イベントやコントロールのフォーカスは使わない。
void ListEditDialog::OnEndLabelEdit(std::shared_ptr<::cw::lib::Window> event){if(!event)return;const auto index=event->id;const auto text=event->title;if(valid_index(index,m_list.size())&&!text.empty()&&std::find(m_list.begin(),m_list.end(),text)==m_list.end()){m_list[index]=text;fill_rows(m_values,m_list);}}
// ネイティブの基底クラスの確定処理は、処理を停止してモーダルの結果を記録
// する。Py の基底 OnOk は何もしない。 モデルの公開は派生ハンド
// ラーや呼び出し側が担当し、この基底メソッドは公開も巻き戻しも行わない。
void ListEditDialog::OnOkBtn(std::shared_ptr<::cw::lib::Window> event){(void)event;m__processing=false;m_modal_accepted=true;}
void ListEditDialog::OnItemSelected(std::shared_ptr<::cw::lib::Window> event){(void)event;if(!m__processing)_item_selected();}
std::vector<Integer> ListEditDialog::get_selectedindexes(){set_selected(m_selected,m_list.size());return m_selected;}
void ListEditDialog::_item_selected()
{
    if (m_values) m_selected = m_values->m_selected;
    set_selected(m_selected,m_list.size());
}
// 共通ダイアログは宿の破棄後に初期化される場合がある。ydata がな
// ければ、Py のようにグローバルを参照したりアサートしたりせず、空の
// 一覧を作る。 ゴシップと終了印の公開用ヘルパーも有効な宿を必要とす
// る。確定一覧のスナップショットについては各 OnOk に記載する。
void GossipEditDialog::initialize(std::shared_ptr<::cw::lib::Window> parent){std::vector<String> list;if(::cw::g_cwpy&&::cw::g_cwpy->m_ydata&&*::cw::g_cwpy->m_ydata)list=(**::cw::g_cwpy->m_ydata).get_gossiplist();ListEditDialog::initialize(parent,"ゴシップの編集",std::move(list),nullptr);}
void GossipEditDialog::OnOkBtn(std::shared_ptr<::cw::lib::Window> event)
{
    // Py はリストそのものを予約処理へ渡すが、ネイティブ版ではゲームの所
    // 有スレッドが処理する前にモーダルが破棄され得るため、 値のスナップシ
    // ョットを渡す。その後 UI で変更しても、確定済みの行は変わらない。
    (void)event;
    m_modal_accepted = true;
    const auto list = m_list;
    if (::cw::g_cwpy) {
        ::cw::g_cwpy->exec_func([list](const auto&, const auto&) {
            GossipEditDialog_OnOkBtn_func(list);
            return Value{};
        });
    } else {
        GossipEditDialog_OnOkBtn_func(list);
    }
}
void CompStampEditDialog::initialize(std::shared_ptr<::cw::lib::Window> parent){std::vector<String> list;if(::cw::g_cwpy&&::cw::g_cwpy->m_ydata&&*::cw::g_cwpy->m_ydata)list=(**::cw::g_cwpy->m_ydata).get_compstamplist();ListEditDialog::initialize(parent,"終了印の編集",std::move(list),nullptr);}
void CompStampEditDialog::OnOkBtn(std::shared_ptr<::cw::lib::Window> event)
{
    // Py はリストそのものを予約処理へ渡すが、ネイティブ版ではゲームの所
    // 有スレッドが処理する前にモーダルが破棄され得るため、 値のスナップシ
    // ョットを渡す。その後 UI で変更しても、確定済みの行は変わらない。
    (void)event;
    m_modal_accepted = true;
    const auto list = m_list;
    if (::cw::g_cwpy) {
        ::cw::g_cwpy->exec_func([list](const auto&, const auto&) {
            CompStampEditDialog_OnOkBtn_func(list);
            return Value{};
        });
    } else {
        CompStampEditDialog_OnOkBtn_func(list);
    }
}
void EditableListCtrl::initialize(std::shared_ptr<CouponEditDialog> parent,Integer cid,std::tuple<Integer,Integer> size,Integer style){(void)parent;(void)cid;(void)size;(void)style;m_rows.clear();m_images.clear();m_selected.clear();m_col_locs.clear();}
// ネイティブ版ではここで行と列を検証して行を選択する。インラインセル編集は CommonEditPresentation
// が担当する。 Py は wx の TextEditMixin エディターを生成して開く。
void EditableListCtrl::OpenEditor(Integer row,Integer col){if(!valid_index(row,m_rows.size()))return;if(col<0||static_cast<std::size_t>(col)>=m_rows[static_cast<std::size_t>(row)].size())return;m_selected={row};}

void SavedJPDCImageEditDialog::initialize(std::shared_ptr<::cw::lib::Window> parent,std::map<std::tuple<String,String>,std::shared_ptr<::cw::header::SavedJPDCImageHeader>> table)
{
    m_native_parent_handle = parent ? parent->native_handle : 0;
    m_cwpy_debug = true;
    m_modal_accepted = false;
    m__removed.clear();
    m_selected.clear();
    m_list.clear();
    for (const auto& [key, unused] : table) {
        (void)unused;
        m_list.push_back(key);
    }
    std::stable_sort(m_list.begin(), m_list.end(), natural_tuple_less);
    m_values = std::make_shared<AutoWidthListCtrl>();
    m_values->initialize(nullptr, -1, {250, 300}, 0);
    fill_rows(m_values, m_list);
    m_find = std::make_shared<FindPanel>();
    m_find->initialize(nullptr, m_values,
                       [this](const auto&, const auto&) { _item_selected(); return Value{}; }, 0);
    _bind();
    _do_layout();
}
void SavedJPDCImageEditDialog::_bind(){}
void SavedJPDCImageEditDialog::_do_layout(){}
void SavedJPDCImageEditDialog::OnRemoveBtn(std::shared_ptr<::cw::lib::Window> event)
{
    (void)event;
    remove_selected_rows(m_list, m_selected, m__removed);
    fill_rows(m_values, m_list);
    if (m_values) m_values->m_selected.clear();
}
void SavedJPDCImageEditDialog::OnOkBtn(std::shared_ptr<::cw::lib::Window> event)
{
    // ネイティブ版は、確定した削除キーの vector をモーダル破棄後
    // も保持する。Py は既存のリストを渡すが、 共通実装のコールバック
    // はダイアログの記憶領域を借りず、値のスナップショットを所有する。
    (void)event;
    m_modal_accepted = true;
    const auto removed = m__removed;
    if (::cw::g_cwpy) {
        ::cw::g_cwpy->exec_func([removed](const auto&, const auto&) {
            SavedJPDCImageEditDialog_OnOkBtn_func(removed);
            return Value{};
        });
    } else {
        SavedJPDCImageEditDialog_OnOkBtn_func(removed);
    }
}
void SavedJPDCImageEditDialog::OnItemSelected(std::shared_ptr<::cw::lib::Window> event){(void)event;_item_selected();}
std::vector<Integer> SavedJPDCImageEditDialog::get_selectedindexes(){set_selected(m_selected,m_list.size());return m_selected;}
void SavedJPDCImageEditDialog::_item_selected()
{
    if (m_values) m_selected = m_values->m_selected;
    set_selected(m_selected,m_list.size());
}
void SavedVariablesEditDialog::initialize(std::shared_ptr<::cw::lib::Window> parent,std::map<std::tuple<String,String>,std::tuple<std::shared_ptr<::cw::data::CWPyElement>,std::map<String,bool>,std::map<String,Integer>,std::map<String,Value>>> table)
{
    m_native_parent_handle = parent ? parent->native_handle : 0;
    m_cwpy_debug = true;
    m_modal_accepted = false;
    m__removed.clear();
    m_selected.clear();
    m_list.clear();
    for (const auto& [key, unused] : table) {
        (void)unused;
        m_list.push_back(key);
    }
    std::stable_sort(m_list.begin(), m_list.end(), natural_tuple_less);
    m_values = std::make_shared<AutoWidthListCtrl>();
    m_values->initialize(nullptr, -1, {250, 300}, 0);
    fill_rows(m_values, m_list);
    m_find = std::make_shared<FindPanel>();
    m_find->initialize(nullptr, m_values,
                       [this](const auto&, const auto&) { _item_selected(); return Value{}; }, 0);
    _bind();
    _do_layout();
}
void SavedVariablesEditDialog::_bind(){}
void SavedVariablesEditDialog::_do_layout(){}
void SavedVariablesEditDialog::OnRemoveBtn(std::shared_ptr<::cw::lib::Window> event)
{
    (void)event;
    remove_selected_rows(m_list, m_selected, m__removed);
    fill_rows(m_values, m_list);
    if (m_values) m_values->m_selected.clear();
}
void SavedVariablesEditDialog::OnOkBtn(std::shared_ptr<::cw::lib::Window> event)
{
    // ネイティブ版は、確定した削除キーの vector をモーダル破棄後
    // も保持する。Py は既存のリストを渡すが、 共通実装のコールバック
    // はダイアログの記憶領域を借りず、値のスナップショットを所有する。
    (void)event;
    m_modal_accepted = true;
    // ワーカーへの書き込みを予約する前に、確定した削除一覧をコピーする。コールバックの実行前にモーダルのモデルが閉じられる可能性がある。
    const auto removed = m__removed;
    if (::cw::g_cwpy) {
        ::cw::g_cwpy->exec_func([removed](const auto&, const auto&) {
            SavedVariablesEditDialog_OnOkBtn_func(removed);
            return Value{};
        });
    } else {
        SavedVariablesEditDialog_OnOkBtn_func(removed);
    }
}
void SavedVariablesEditDialog::OnItemSelected(std::shared_ptr<::cw::lib::Window> event){(void)event;_item_selected();}
std::vector<Integer> SavedVariablesEditDialog::get_selectedindexes(){set_selected(m_selected,m_list.size());return m_selected;}
void SavedVariablesEditDialog::_item_selected()
{
    if (m_values) m_selected = m_values->m_selected;
    set_selected(m_selected,m_list.size());
}

void FindPanel::initialize(std::shared_ptr<::cw::lib::Object> parent,std::shared_ptr<::cw::lib::Object> values,::cw::lib::Callback item_selected,Integer style){(void)parent;(void)style;m_query.clear();m_cursor=-1;m_found=true;m_item_selected_callback=std::move(item_selected);m_values=values?Value(std::static_pointer_cast<::cw::lib::Object>(values)):Value{};_do_layout();}
void FindPanel::_bind(){}
void FindPanel::_do_layout(){}
void FindPanel::find_up()
{
    if (m_query.empty()) return; // Py と同様、検索文字列が空なら前の状態を維持する。
    const auto values = find_rows(m_values);
    if (!values.rows || !values.selected || values.rows->empty()) {
        m_found = false;
        return;
    }
    const auto& rows = *values.rows;
    const Integer start = values.selected->empty() ? 0 : values.selected->front();
    const auto query = lower_text(m_query);
    values.selected->clear();
    for (std::size_t n = 0; n < rows.size(); ++n) {
        const auto index = (start - static_cast<Integer>(n) - 1 +
                            static_cast<Integer>(rows.size()) * 2) %
                           static_cast<Integer>(rows.size());
        if (!rows[static_cast<std::size_t>(index)].empty() &&
            lower_text(rows[static_cast<std::size_t>(index)].front()).find(query) != String::npos) {
            *values.selected = {index};
            m_cursor = index;
            m_found = true;
            if (m_item_selected_callback)
                m_item_selected_callback({}, {});
            return;
        }
    }
    m_found = false;
    m_cursor = -1;
    if (m_item_selected_callback) m_item_selected_callback({}, {});
}
void FindPanel::find_down()
{
    if (m_query.empty()) return; // Py と同様、検索文字列が空なら前の状態を維持する。
    const auto values = find_rows(m_values);
    if (!values.rows || !values.selected || values.rows->empty()) {
        m_found = false;
        return;
    }
    const auto& rows = *values.rows;
    const Integer start = values.selected->empty() ? static_cast<Integer>(rows.size())-1 : values.selected->front();
    const auto query = lower_text(m_query);
    values.selected->clear();
    for (std::size_t n = 0; n < rows.size(); ++n) {
        const auto index = (start + static_cast<Integer>(n) + 1 +
                            static_cast<Integer>(rows.size()) * 2) %
                           static_cast<Integer>(rows.size());
        if (!rows[static_cast<std::size_t>(index)].empty() &&
            lower_text(rows[static_cast<std::size_t>(index)].front()).find(query) != String::npos) {
            *values.selected = {index};
            m_cursor = index;
            m_found = true;
            if (m_item_selected_callback)
                m_item_selected_callback({}, {});
            return;
        }
    }
    m_found = false;
    m_cursor = -1;
    if (m_item_selected_callback) m_item_selected_callback({}, {});
}
void FindPanel::OnFindUp(std::shared_ptr<::cw::lib::Window> event){(void)event;find_up();}
void FindPanel::OnFindDown(std::shared_ptr<::cw::lib::Window> event){(void)event;find_down();}
// Py と同様、空でない検索文字列の編集では、次の検索まで前回の検索
// 結果の色を維持する。 検索文字列を空にすると通常の背景色へ戻す。
void FindPanel::OnTextChanged(std::shared_ptr<::cw::lib::Window> event){if(event)m_query=event->title;m_cursor=-1;if(m_query.empty())m_found=true;}
void FindPanel::OnEnter(std::shared_ptr<::cw::lib::Window> event)
{
    if (event && (event->modifiers & k_shift_modifier))
        find_up();
    else
        find_down();
}

void EditBookmarksForCardEditDialog::initialize(std::shared_ptr<::cw::lib::Window> parent,BookmarkList bookmarks){m_native_parent_handle=parent?parent->native_handle:0;m_cwpy_debug=true;m__processing=false;m_modal_accepted=false;m_list=std::move(bookmarks);m_update_failed.assign(m_list.size(),false);m__removed.clear();m_selected.clear();_bind();_do_layout();}
void EditBookmarksForCardEditDialog::_bind(){}
void EditBookmarksForCardEditDialog::_do_layout(){}
bool EditBookmarksForCardEditDialog::show_modal()
{
    return ::cw::debug::edit::show_modal(*this);
}

std::shared_ptr<::cw::dialog::DialogPresentationProvider>
create_common_edit_presentation(::cw::lib::Object& object)
{
    return std::make_shared<CommonEditPresentation>(object);
}

bool show_modal(::cw::lib::Object& object)
{
    auto presentation = create_common_edit_presentation(object);
    (void)::cw::dialog::show_common_modal_presentation(
        std::dynamic_pointer_cast<::cw::lib::Object>(presentation));
    // ホストは描画面の生成前に失敗する場合がある。その場合も通常のモーダル
    // 終了時と同様、保留中の評価とドキュメントのコールバックを取り消す。
    presentation->dialog_presentation_complete();
    if (const auto dialog = dynamic_cast<BreakpointEditDialog*>(&object)) return dialog->m_modal_accepted;
    if (const auto dialog = dynamic_cast<CouponEditDialog*>(&object)) return dialog->m_modal_accepted;
    if (const auto dialog = dynamic_cast<ListEditDialog*>(&object)) return dialog->m_modal_accepted;
    if (const auto dialog = dynamic_cast<SavedJPDCImageEditDialog*>(&object)) return dialog->m_modal_accepted;
    if (const auto dialog = dynamic_cast<SavedVariablesEditDialog*>(&object)) return dialog->m_modal_accepted;
    if (const auto dialog = dynamic_cast<VariantEditDialog*>(&object)) return dialog->m_modal_accepted;
    if (const auto dialog = dynamic_cast<EditBookmarksForCardEditDialog*>(&object)) return dialog->m_modal_accepted;
    return false;
}

void EditBookmarksForCardEditDialog::OnUpdateBtn(std::shared_ptr<::cw::lib::Window> event){(void)event;if(m_update_failed.size()!=m_list.size())m_update_failed.assign(m_list.size(),false);const auto indexes=get_selectedindexes();for(std::size_t i=0;i<m_list.size();++i){if(!indexes.empty()&&std::find(indexes.begin(),indexes.end(),static_cast<Integer>(i))==indexes.end())continue;m_update_failed[i]=false;try{const auto scenario=::cw::scenariodb::get_scenario(std::get<0>(m_list[i]));if(scenario&&*scenario){std::get<1>(m_list[i])=(*scenario)->m_name;}else m_update_failed[i]=true;}catch(...){m_update_failed[i]=true;}}}
void EditBookmarksForCardEditDialog::OnRemoveBtn(std::shared_ptr<::cw::lib::Window> event)
{
    (void)event;
    set_selected(m_selected,m_list.size());
    m_update_failed.resize(m_list.size(),false);
    for(auto it=m_selected.rbegin();it!=m_selected.rend();++it) {
        m__removed.push_back(m_list[*it]);
        m_list.erase(m_list.begin()+*it);
        m_update_failed.erase(m_update_failed.begin()+*it);
    }
    m_selected.clear();
}
void EditBookmarksForCardEditDialog::OnUpBtn(std::shared_ptr<::cw::lib::Window> event)
{
    (void)event;
    const auto indexes=get_selectedindexes();
    if(indexes.empty()||indexes.front()<1)return;
    for(auto i:indexes)_swap(i,i-1);
}
void EditBookmarksForCardEditDialog::OnDownBtn(std::shared_ptr<::cw::lib::Window> event)
{
    (void)event;
    const auto indexes=get_selectedindexes();
    if(indexes.empty()||indexes.back()+1>=static_cast<Integer>(m_list.size()))return;
    for(auto it=indexes.rbegin();it!=indexes.rend();++it)_swap(*it,*it+1);
}
void EditBookmarksForCardEditDialog::OnUp2Btn(std::shared_ptr<::cw::lib::Window> event)
{
    (void)event;
    m_update_failed.resize(m_list.size(),false);
    auto indexes=get_selectedindexes();
    move_to_top(m_update_failed,indexes);
    move_to_top(m_list,m_selected);
}
void EditBookmarksForCardEditDialog::OnDown2Btn(std::shared_ptr<::cw::lib::Window> event)
{
    (void)event;
    m_update_failed.resize(m_list.size(),false);
    auto indexes=get_selectedindexes();
    move_to_bottom(m_update_failed,indexes);
    move_to_bottom(m_list,m_selected);
}
void EditBookmarksForCardEditDialog::_swap(Integer a,Integer b)
{
    if(!valid_index(a,m_list.size())||!valid_index(b,m_list.size()))throw std::out_of_range("bookmark index");
    m_update_failed.resize(m_list.size(),false);
    std::swap(m_list[a],m_list[b]);
    // Py は保存済みシナリオ名とは独立して、表示中の失敗メッセージと
    // 選択を行と一緒に移動する。 共通 UI でもこの状態を維持する。
    const bool failed=m_update_failed[a];
    m_update_failed[a]=m_update_failed[b];
    m_update_failed[b]=failed;
    for(auto& i:m_selected) {
        if(i==a)i=b;
        else if(i==b)i=a;
    }
    set_selected(m_selected,m_list.size());
}
void EditBookmarksForCardEditDialog::OnOkBtn(std::shared_ptr<::cw::lib::Window> event){(void)event;m__processing=false;m_modal_accepted=true;}
void EditBookmarksForCardEditDialog::OnItemSelected(std::shared_ptr<::cw::lib::Window> event){(void)event;_item_selected();}
std::vector<Integer> EditBookmarksForCardEditDialog::get_selectedindexes(){set_selected(m_selected,m_list.size());return m_selected;}
void EditBookmarksForCardEditDialog::_item_selected(){set_selected(m_selected,m_list.size());}

// 共通一覧の行と画像は値として保存済みなので、wx の GetItem で各セル
// や画像を読み出す代わりにコピーする。 ネイティブのコントロールが null な
// ら空の vector を返す。Py はウィジェットが存在することを前提とする。
std::tuple<std::vector<std::vector<String>>,std::vector<Integer>> _get_iteminfos(std::shared_ptr<EditableListCtrl> values){if(!values)return {std::vector<std::vector<String>>{},std::vector<Integer>{}};return {values->m_rows,values->m_images};}
namespace {
void move_list_boundary(const std::shared_ptr<EditableListCtrl>& values,
                        std::vector<Value>& seq, const Value& indexes, bool bottom)
{
    const auto* object = std::get_if<::cw::lib::ObjectPtr>(&indexes.storage());
    const auto list = object ? std::dynamic_pointer_cast<::cw::data::VariantSequence>(*object) : nullptr;
    if (!list) return;
    std::vector<Integer> selected;
    for (const auto& item : list->m_values)
        if (const auto index = value_integer(item)) selected.push_back(*index);
    selected = clean_indexes<Value>(selected, seq.size());
    if (selected.empty()) return;
    if (values && values->m_rows.size() != seq.size())
        throw std::invalid_argument("list rows and values differ in size");
    const auto move = [&](auto& rows) {
        auto order = selected;
        if (bottom) move_to_bottom(rows, order);
        else move_to_top(rows, order);
        return order;
    };
    if (values) {
        values->m_images.resize(values->m_rows.size(), -1);
        move(values->m_rows);
        move(values->m_images);
    }
    const auto result = move(seq);
    if (values) {
        values->m_selected = result;
        values->m_ensure_visible_row = bottom ? static_cast<Integer>(seq.size() - 1) : 0;
    }
}
}

void up_to_top(std::shared_ptr<EditableListCtrl> values, std::vector<Value>& seq, Value indexes)
{
    move_list_boundary(values, seq, indexes, false);
}

void down_to_bottom(std::shared_ptr<EditableListCtrl> values, std::vector<Value>& seq, Value indexes)
{
    move_list_boundary(values, seq, indexes, true);
}

void VariantEditDialog::initialize(std::shared_ptr<::cw::lib::Window> parent,String dialog_title,String label,Value value,bool different)
{
    m_native_parent_handle = parent ? parent->native_handle : 0;
    m_dialog_title = std::move(dialog_title);
    m_dialog_label = std::move(label);
    m_cwpy_debug = true;
    m_modal_accepted = false;
    m_eval_generation->fetch_add(1, std::memory_order_acq_rel);
    m_is_differentscenario = Value(different);
    m_value = value;
    m_value_valid = true;
    // 共通キャンバスは、式エラーの強調表示をフラグで保持する。
    // Py と同様、初期状態は通常色とし、評価後にだけ変更する。
    m__value_expr_colour = Value(false);
    m_error_text.clear();
    // Python の無効なコントロールは、別の型を選択してもこの既定値を保持する。 そのた
    // め型の切り替え時は、空文字列や FALSE ではなく 0 と TRUE から始める。
    m_value_num_text = "0";
    m_value_str_text.clear();
    m_value_expr_text.clear();
    m_value_bool_value = true;
    if (const auto bool_value = std::get_if<bool>(&value.storage())) {
        m_value_kind = 2;
        m_value_bool_value = *bool_value;
    } else if (const auto integer_value = std::get_if<Integer>(&value.storage())) {
        // ネイティブの共通 Value は Integer、Real、null
        // も扱うが、Py のコモン値は代わりに Decimal を使う。 これ
        // らの受け渡し用の値も同じ数値・式コントロールで編集できるようにする。
        m_value_kind = 0;
        m_value_num_text = std::to_string(*integer_value);
    } else if (const auto real_value = std::get_if<Real>(&value.storage())) {
        m_value_kind = 0;
        m_value_num_text = std::to_string(*real_value);
    } else if (const auto decimal_value = std::get_if<::cw::lib::Decimal>(&value.storage())) {
        m_value_kind = 0;
        // Py はこのフィールドを "%.3f" % Decimal で初期化する。
        // 通常の Decimal の ROUND_HALF_UP による入力更新より
        // 先に、 二進浮動小数点への変換と小数点以下 3 桁への丸めが行われる。ここ
        // で Decimal の全桁を保持すると、4 桁目の境界で初期値が変わる。
        try {
            std::array<char, 512> buffer{}; // 有限の double はすべて、小数点以下 3 桁の固定小数点表記で格納できる。
            const auto formatted = std::to_chars(buffer.data(), buffer.data() + buffer.size(),
                decimal_value->to_real(), std::chars_format::fixed, 3);
            if (formatted.ec != std::errc{}) throw std::runtime_error("initial number formatting failed");
            m_value_num_text.assign(buffer.data(), formatted.ptr);
            while (!m_value_num_text.empty() && m_value_num_text.back() == '0')
                m_value_num_text.pop_back();
            if (!m_value_num_text.empty() && m_value_num_text.back() == '.')
                m_value_num_text.pop_back();
            if (m_value_num_text.empty()) m_value_num_text = "0";
        } catch (...) {
            m_value_num_text = "0";
        }
    } else if (const auto string_value = std::get_if<String>(&value.storage());
               string_value && string_value->find('\n') == String::npos) {
        m_value_kind = 1;
        // このフィールドでは一行の String 値をそのまま表示し、式の値だけに下の value_to_str を使う。
        m_value_str_text = *string_value;
    } else {
        m_value_kind = 3;
        m_value_expr_text = ::cw::data::Variant::value_to_str(value, true);
    }
    m__expression = m_value_expr_text;
    _update_value();
    // Py と同様、初期値の更新後に一度だけ OK を明示的に有効にする
    // 。 その後の編集とプレビュー結果では、値に応じた有効状態に戻す。
    m_okbtn = Value(true);
    _bind();
    _do_layout();
    _eval_thr();
}
void VariantEditDialog::_bind(){}
void VariantEditDialog::_do_layout(){}
void VariantEditDialog::OnOkBtn(std::shared_ptr<::cw::lib::Window> event)
{
    (void)event;
    if (m_value_kind == 3) {
        // Python と同様、確定時に再評価する。デバッガーの
        // 式が参照する状態は、入力時のプレビュー後に変わり得る。
        m_eval_generation->fetch_add(1, std::memory_order_acq_rel);
        // ここではゲームキューの完了を同期的に待たない。ホスト内のモーダルは、
        // このダイアログが戻るのを待つゲーム操作を持つ場合がある。 Python
        // も EndModal の前に OnOkBtn で直接評価する。
        auto result = evaluate_variant_expression(
            m_value_expr_text, m_is_differentscenario.as_bool());
        m_value = std::move(result.value);
        m_error_text = std::move(result.description);
        m_value_valid = m_value.has_value();
        m_okbtn = Value(m_value_valid);
        m__value_expr_colour = Value(!m_value_valid);
    }
    // Py の OnOkBtn は、式の再評価に失敗した場合だけ確定
    // を拒否する。 式以外では数値が None でも確定して閉じる。
    if (m_value_kind != 3 || (m_value_valid && m_value)) {
        m_modal_accepted = true;
        m__time_modified = 0.0;
    }
}
void VariantEditDialog::OnType(std::shared_ptr<::cw::lib::Window> event)
{
    (void)event;
    _update_value();
    if (m_value_kind == 3)
        _eval_thr();
    else
        m_eval_generation->fetch_add(1, std::memory_order_acq_rel);
}
void VariantEditDialog::OnValue(std::shared_ptr<::cw::lib::Window> event)
{
    (void)event;
    _update_value();
    if (m_value_kind == 3)
        _eval_thr();
    else
        m_eval_generation->fetch_add(1, std::memory_order_acq_rel);
}
void VariantEditDialog::_update_value()
{
    try {
        if (m_value_kind == 2) {
            m_value = Value(m_value_bool_value);
            m_value_valid = true;
            m_error_text.clear();
        } else if (m_value_kind == 0) {
            if (m_value_num_text.empty())
                throw std::invalid_argument("empty number");
            const auto number = ::cw::lib::Decimal::parse(m_value_num_text);
            // Py の quantize は Decimal コンテキストの 28
            // 桁精度を使う。 小数第 3 位に揃えた係数がこの精度を超える値は不
            // 正入力とし、無制限のネイティブ Decimal として確定しない。
            const auto rounded = number.quantize(::cw::lib::Decimal::parse("0.001"),
                                                 ::cw::lib::DecimalRounding::HalfUp,
                                                 ::cw::lib::DecimalContext::default_context().precision).normalize();
            m_value = Value(rounded);
            m_value_valid = true;
            m_error_text.clear();
        } else if (m_value_kind == 1) {
            m_value = Value(m_value_str_text);
            m_value_valid = true;
            m_error_text.clear();
        } else {
            m_value.reset();
            m_value_valid = false;
            // Py と同様、式の評価待ち中は前のエラー文を
            // 維持し、プレビュー完了時にだけ置き換える。
            m__expression = m_value_expr_text;
        }
    } catch (...) {
        m_value.reset();
        m_value_valid = false;
        m_error_text = "入力値を数値に変換できません";
    }
    if (m_value_kind != 3) m__value_expr_colour = Value(false);
    m_okbtn = Value(m_value_valid);
}
void VariantEditDialog::_eval_thr()
{
    const auto generation_state = m_eval_generation;
    const auto generation = generation_state->fetch_add(1, std::memory_order_acq_rel) + 1;
    if (m_value_kind != 3 || m_value_expr_text.empty()) {
        if (m_value_kind == 3) {
            m_value.reset();
            m_value_valid = false;
            // Py では空の式にプレビュー待ちは発生しない。入
            // 力エラーを新たに作らず、既存の説明を維持する。
        }
        return;
    }

    const auto expression = m_value_expr_text;
    const bool different_scenario = m_is_differentscenario.as_bool();
    std::weak_ptr<VariantEditDialog> weak_self;
    try {
        weak_self = weak_from_this();
    } catch (const std::bad_weak_ptr&) {
        // スタック上に作られた互換用インスタンスでは、非同期コールバ
        // ックを安全に受け取れないため、同期的で確定した動作にする。
    }

    const auto apply_result = [this, generation_state, generation](VariantEvalResult result) {
        if (generation_state->load(std::memory_order_acquire) != generation)
            return;
        m_value = std::move(result.value);
        m_error_text = std::move(result.description);
        m_value_valid = m_value.has_value();
        m_okbtn = Value(m_value_valid);
        m__value_expr_colour = Value(!m_value_valid);
    };

    if (weak_self.expired()) {
        // ネイティブ版の生存期間への対応として、共有所有が成立するまではプレビュー
        // を即時評価する。 通常の呼び出し側は make_shared 後に初期化
        // するため、最初のプレビューから遅延処理を使う。 共有所有なしでは、遅延
        // コールバックがオブジェクトの破棄後に実行され得る。共有所有の成立後は、
        // 下の OnValue/OnType で 500ms の待機をやり直す
        // 。OnOkBtn は別途、Python と同様に確定時に即時評価する。
        apply_result(evaluate_variant_expression(expression, different_scenario));
        return;
    }

    std::weak_ptr<::cw::frame::Frame> weak_frame;
    if (::cw::g_cwpy) {
        if (const auto object = std::get_if<::cw::lib::ObjectPtr>(
                &::cw::g_cwpy->m_frame.storage()); object && *object)
            weak_frame = std::dynamic_pointer_cast<::cw::frame::Frame>(*object);
    }

    variant_preview_timer().schedule(generation_state.get(),
        [weak_self, weak_frame, generation_state, generation, expression,
         different_scenario]() mutable {
        if (generation_state->load(std::memory_order_acquire) != generation)
            return;
        const auto frame = weak_frame.lock();
        if (!frame) return;
        // 遅延処理のワーカーは Engine の状態を所有せず、UI ディ
        // スパッチャーを起こすだけとする。 UI からゲームキューへ式の評
        // 価を送り、C++ のスレッド所有規則を守る。 結果は Frame
        // を経由して受け取り、その後でダイアログのフィールドに触れる。
        try {
        frame->exec_func([weak_self, weak_frame, generation_state, generation,
                          expression, different_scenario](const auto&, const auto&) {
            const auto self = weak_self.lock();
            if (!self || generation_state->load(std::memory_order_acquire) != generation ||
                self->m_value_kind != 3 || self->m_value_expr_text != expression ||
                !::cw::g_cwpy)
                return ::cw::lib::Value{};
            ::cw::g_cwpy->exec_func(
                [weak_self, weak_frame, generation_state, generation, expression,
                 different_scenario](const auto&, const auto&) mutable {
                    if (generation_state->load(std::memory_order_acquire) != generation)
                        return ::cw::lib::Value{};
                    auto result = evaluate_variant_expression(expression, different_scenario);
                    const auto frame = weak_frame.lock();
                    if (!frame) return ::cw::lib::Value{};
                    frame->exec_func(
                        [weak_self, generation_state, generation, expression,
                         result = std::move(result)](const auto&, const auto&) mutable {
                            const auto self = weak_self.lock();
                            if (!self || generation_state->load(std::memory_order_acquire) != generation ||
                                self->m_value_kind != 3 || self->m_value_expr_text != expression)
                                return ::cw::lib::Value{};
                            self->m_value = std::move(result.value);
                            self->m_error_text = std::move(result.description);
                            self->m_value_valid = self->m_value.has_value();
                            self->m_okbtn = Value(self->m_value_valid);
                            self->m__value_expr_colour = Value(!self->m_value_valid);
                            return ::cw::lib::Value{};
                        });
                    return ::cw::lib::Value{};
                });
            return ::cw::lib::Value{};
        });
        } catch (...) {
            // 遅延待機中に終了処理で UI ディスパッチャーが破棄される場合がある。
        }
    });
}

void CouponEditDialog_OnOkBtn_func(std::vector<std::shared_ptr<::cw::character::Character>> pcards,std::vector<std::vector<Coupon>> coupons,std::set<String> syscoupons,Integer cindex,bool adjust)
{
    auto system = std::make_shared<::cw::data::VariantSequence>();
    for (const auto& name : syscoupons) system->m_values.emplace_back(name);
    Value system_value(std::static_pointer_cast<::cw::lib::Object>(system));
    bool updated = false;
    for (std::size_t i = 0; i < pcards.size() && i < coupons.size(); ++i) {
        // ダイアログ表示中の称号変更は、全メンバーについて集める
        // 。選択メンバーは任意のレベル調整の対象だけを決める。
        // ここで選択メンバーに絞ると、対象を切り替えてから
        // OK を押した際、以前のメンバーへの編集が失われる。
        if (!pcards[i])
            continue;
        auto sequence = coupons[i];
        std::reverse(sequence.begin(), sequence.end());
        const bool replaced = pcards[i]->replace_allcoupons(sequence, system_value);
        if (adjust && (cindex < 0 || static_cast<Integer>(i) == cindex || replaced)) {
            if (const auto player_card = std::dynamic_pointer_cast<::cw::sprite::card::PlayerCard>(pcards[i]))
                updated = player_card->adjust_level(false) || updated;
        }
    }
    if (!updated && ::cw::g_cwpy)
        ::cw::g_cwpy->play_sound("harvest", false, 100, 1, 0, 0, false);
}
// Py は宿が存在することをアサートするが、ネイティブの省略可能なホストでは、確定コー
// ルバックの実行前に宿が破棄され得る。 その場合は効果音や書き込みの前に処理を省く。
void GossipEditDialog_OnOkBtn_func(std::vector<String> seq){if(!::cw::g_cwpy||!::cw::g_cwpy->m_ydata||!*::cw::g_cwpy->m_ydata)return;::cw::g_cwpy->play_sound("harvest",false, 100, 1, 0,0,false);auto&yado=**::cw::g_cwpy->m_ydata;yado.clear_gossips();for(auto&name:seq)yado.set_gossip(std::move(name));}
// Py は宿が存在することをアサートするが、ネイティブの省略可能なホストでは、確定コー
// ルバックの実行前に宿が破棄され得る。 その場合は効果音や書き込みの前に処理を省く。
void CompStampEditDialog_OnOkBtn_func(std::vector<String> seq){if(!::cw::g_cwpy||!::cw::g_cwpy->m_ydata||!*::cw::g_cwpy->m_ydata)return;::cw::g_cwpy->play_sound("harvest",false, 100, 1, 0,0,false);auto&yado=**::cw::g_cwpy->m_ydata;yado.clear_compstamps();for(auto&name:seq)yado.set_compstamp(std::move(name));}
void SavedJPDCImageEditDialog_OnOkBtn_func(std::vector<std::tuple<String,String>> removed)
{
    // Py は ydata をアサートする。ネイティブ版で任意の宿が破棄済みなら、
    // 効果音・ファイル削除・更新の前に完了処理を省き、保存成功として通知しない。
    if (!::cw::g_cwpy || !::cw::g_cwpy->m_ydata ||
        !*::cw::g_cwpy->m_ydata)
        return;
    ::cw::g_cwpy->play_sound("harvest", false, 100, 1, 0, 0, false);
    auto& savedjpdcimage = (**::cw::g_cwpy->m_ydata).m_savedjpdcimage;
    for (const auto& key : removed) {
        auto it = savedjpdcimage.find(key);
        if (it != savedjpdcimage.end()) {
            if (it->second) it->second->remove_all();
            savedjpdcimage.erase(it);
        }
    }
    if (::cw::g_cwpy->m_event) ::cw::g_cwpy->m_event->refresh_tools();
}
void SavedVariablesEditDialog_OnOkBtn_func(std::vector<std::tuple<String,String>> removed)
{
    // Py は効果音や更新の前に宿の存在をアサートする。ネイティブ版の
    // 予約済みモーダル完了処理は、宿の破棄後に届く場合がある。 その場
    // 合は完了処理全体を省き、効果音や更新によって編集成功を示さない。
    if (!::cw::g_cwpy || !::cw::g_cwpy->m_ydata || !*::cw::g_cwpy->m_ydata) return;
    auto& yado = **::cw::g_cwpy->m_ydata;
    ::cw::g_cwpy->play_sound("harvest", false, 100, 1, 0, 0, false);
    for (const auto& key : removed) {
        // Py と同様、削除 API を呼ぶ前に所属を確認する。
        if (yado.m_saved_variables.contains(key))
            yado.remove_savedvariables(std::get<0>(key), std::get<1>(key));
    }
    if (::cw::g_cwpy->m_event) ::cw::g_cwpy->m_event->refresh_tools();
}
void BreakpointEditDialog_OnOkBtn_func(std::vector<std::tuple<String,String>> removed)
{
    if (!::cw::g_cwpy) return;
    ::cw::g_cwpy->play_sound("harvest", false, 100, 1, 0, 0, false);
    for (const auto& key : removed) ::cw::g_cwpy->m_breakpoint_table.erase(key);
    if (::cw::g_cwpy->m_event) ::cw::g_cwpy->m_event->refresh_tools();
}
void CouponEditDialog__swap_set_item(std::shared_ptr<CouponEditDialog> self, Integer index)
{
    if (!self || !self->m_values || !valid_index(index, self->m_values->m_rows.size()))
        return;
    self->m_values->m_selected = {index};
}
void CouponEditDialog__swap_set_item(Integer index)
{
    // 生成済み ABI との互換性のために残す。旧シグネチャには所
    // 有者の情報がなく、プロセス全体の生ポインターしか参照できなか
    // った。 別のダイアログを操作したり解放済みメモリを参照したり
    // するより、遅延コールバックを処理せず破棄する方が安全である。
    (void)index;
}
void EditBookmarksForCardEditDialog__swap_set_item(
    std::shared_ptr<EditBookmarksForCardEditDialog> self, Integer index,
    std::tuple<String, String> value, Integer image)
{
    if (!self || !valid_index(index, self->m_list.size()))
        return;
    self->m_list[static_cast<std::size_t>(index)] = std::move(value);
    self->m_imgidx = Value(image);
}
void EditBookmarksForCardEditDialog__swap_set_item(
    Integer index, std::tuple<String, String> value, Integer image)
{
    (void)index;
    (void)value;
    (void)image;
}
void VariantEditDialog__eval_thr_func(std::shared_ptr<VariantEditDialog> self,std::optional<Value> value,String desc){if(self){self->m_value=value;self->m_error_text=desc;self->m_value_valid=value.has_value();self->m_okbtn=Value(self->m_value_valid);self->m__value_expr_colour=Value(!self->m_value_valid);}}
void main(){/* ライブラリビルドでは、Python の単体実行用エントリーポイントによる副作用は発生しない。 */}
}
