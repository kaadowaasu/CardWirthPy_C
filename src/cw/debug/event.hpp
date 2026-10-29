#pragma once

// UIの実装は一つに統一し、
// 別経路の実装を設けない。
#include "cw/lib/compat.hpp"
#include "cw/dialog/presentation.hpp"
#include "cw/fwd.hpp"
#include <cstdint>
#include <functional>

namespace cw::debug::event {
class EventUiBackend;
struct EventTreeNode {
    ::cw::lib::String label{}, fpath{};
    std::optional<std::shared_ptr<::cw::event::Event>> event{};
    std::vector<EventTreeNode> children{};
    ::cw::lib::String resource_kind{};
    ::cw::lib::Integer resource_id{};
    bool expanded{false}, native_expanded{false}, resource_root{false};
    // Py は各ルートに、リソース所有者に結び付いた getfpath メソッドを保存する。
    std::function<std::optional<::cw::lib::String>(::cw::lib::Integer)> get_fpath{};
};
// wx.TreeEvent.GetItem と MouseEvent の識別情報を運
// ぶ共通データ。すべてのホストが同じ値を共通のツリー・ダイアログのハンドラーへ渡す。
struct EventTreeUiEvent : ::cw::lib::Window {
    std::vector<std::size_t> item_path{};
    bool mouse_event{false};
};
class EventList : public ::cw::lib::Object {
public:
    bool m_showallcards{false};
    bool m_callbacks_bound{false};
    EventTreeNode m_root{};
    std::vector<std::size_t> m_selection_path{};
    ::cw::lib::String m_initial_fpath{};
    void initialize(std::shared_ptr<::cw::debug::event::EventListDialog> parent,
                    std::tuple<::cw::lib::Integer,::cw::lib::Integer> size,
                    ::cw::lib::String currentfpath,bool showhiddencards);
    void set_resources(std::vector<EventTreeNode> resources);
    void _bind();
    void OnTreeItemExpanded(std::shared_ptr<::cw::lib::Window> event);
    void OnTreeItemCollapsed(std::shared_ptr<::cw::lib::Window> event);
    void _expand_item(std::shared_ptr<::cw::lib::Window> item,bool py_virtual);
    void set_showallcards(bool value);
    std::optional<std::shared_ptr<::cw::event::Event>> get_selectedevent();
    ::cw::lib::String get_currentfpath();
};
class EventListDialog : public ::cw::lib::Dialog,
                        public ::cw::dialog::DialogPresentationProvider,
                        public std::enable_shared_from_this<EventListDialog> {
public:
    bool m_cwpy_debug{true},m_showhiddencards{false},m_start_event{false};
    bool m_open_enabled{false},m_start_enabled{false},m_modal_ok{false};
    bool m_presentation_finished{false};
    ::cw::lib::Integer m_presentation_scroll{};
    std::uintptr_t m_native_parent_handle{};
    std::shared_ptr<EventList> m_events{};
    std::shared_ptr<EventUiBackend> m_ui_backend{};
    void initialize(std::shared_ptr<::cw::lib::Window> parent,::cw::lib::String currentfpath,bool showhiddencards);
    void _do_layout();void _bind();void _changed_selection();
    void OnTreeSelChanged(std::shared_ptr<::cw::lib::Window> event);
    // チェック状態は切り替え命令ではなく入力値。共通バッ
    // クエンドやプログラムによる更新でも明示し、同じチェ
    // ック値を二度通知しても結果が変わらないようにする。
    void OnShowAllCards(bool checked);
    void OnStartBtn(std::shared_ptr<::cw::lib::Window> event);
    void OnOpenBtn(std::shared_ptr<::cw::lib::Window> event);
    bool show_modal();
    [[nodiscard]] ::cw::dialog::DialogPresentation dialog_presentation() override;
    void dialog_presentation_action(::cw::dialog::DialogPresentationAction action,
                                    int index) override;
    void dialog_presentation_key(::cw::dialog::DialogPresentationKey key,bool pressed,
                                 ::cw::dialog::DialogPresentationModifiers modifiers) override;
    void dialog_presentation_enter(::cw::dialog::DialogPresentationModifiers modifiers) override;
    void dialog_presentation_scroll(float x,float y,int lines) override;
    [[nodiscard]] bool dialog_presentation_finished() const noexcept override;
    void dialog_presentation_complete() override;
};
class EventUiBackend {
public:
    virtual ~EventUiBackend() = default;
    virtual void bind(EventListDialog& dialog) = 0;
    virtual void layout(EventListDialog& dialog) = 0;
    virtual bool show_modal(EventListDialog& dialog) = 0;
};
std::shared_ptr<EventUiBackend> create_default_ui_backend();
}
