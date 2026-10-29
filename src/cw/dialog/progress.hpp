#pragma once

// UIの実装は一つに統一し、
// 別経路の実装を設けない。

// cw/dialog/progress.py に対応する共通表示。
#include "cw/lib/compat.hpp"
#include "cw/dialog/presentation.hpp"
#include "cw/fwd.hpp"
#include "cw/lib/ui_surface.hpp"
#include <atomic>

namespace cw::dialog::progress {
struct ProgressSnapshot {
    ::cw::lib::Integer minimum{}, maximum{100}, value{};
    ::cw::lib::String message{};
    bool cancel{}, cancelable{};
    ::cw::lib::Integer range{};
    bool closed{};
    ::cw::lib::Integer pulse{};
};
class ProgressDialog : public ::cw::lib::Dialog,
                       public ::cw::dialog::DialogPresentationProvider,
                       public std::enable_shared_from_this<ProgressDialog> {
public:
    ProgressDialog() = default;
    virtual ~ProgressDialog();
    std::shared_ptr<::cw::lib::Control> m_btn_cncl{};
    bool m_cancel{};
    bool m_cwpy_debug{};
    std::shared_ptr<::cw::lib::Control> m_gauge{};
    ::cw::lib::Integer m_maximum{100},m_minimum{},m_gauge_value{},m_gauge_range{},m_width_noscale{300},m_pulse_count{};
    ::cw::lib::String m_text{},m_title{}; bool m_cancelable{};
    bool m_closed{};
    std::uintptr_t m_native_parent_handle{};
    mutable std::mutex m_state_mutex{};
    std::function<bool()> m_completion_predicate{};
    std::function<void()> m_cancel_action{};
    std::atomic<std::uint64_t> m_state_revision{1};
    std::atomic<std::uint64_t> m_presented_revision{0};
    void initialize(std::shared_ptr<::cw::lib::Window> arg_parent, ::cw::lib::String arg_title, ::cw::lib::String arg_message, ::cw::lib::Integer arg_maximum=100, ::cw::lib::Integer arg_minimum=0, bool arg_cancelable=false, ::cw::lib::Integer arg_width_noscale=300);
    void UpdateProgress(::cw::lib::Integer arg_value, ::cw::lib::String arg_message);
    void OnPaint(std::shared_ptr<::cw::lib::Window> arg_event);
    void OnClickCancelBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    void _do_layout();
    ProgressSnapshot snapshot() const;
    void request_cancel();
    void pulse();
    void close();
    bool is_closed() const;
    void bind_ui();
    void sync_ui();
    void set_modal_callbacks(std::function<bool()> arg_completion,
                             std::function<void()> arg_cancel);
    [[nodiscard]] ::cw::dialog::DialogPresentation dialog_presentation() override;
    void dialog_presentation_action(::cw::dialog::DialogPresentationAction action,
                                    int index) override;
    void dialog_presentation_key(::cw::dialog::DialogPresentationKey key,bool pressed,
                                 ::cw::dialog::DialogPresentationModifiers modifiers) override;
    void dialog_presentation_enter(::cw::dialog::DialogPresentationModifiers modifiers) override;
    void dialog_presentation_context(float,float) override {}
    [[nodiscard]] bool dialog_presentation_finished() const noexcept override;
    [[nodiscard]] bool dialog_presentation_needs_refresh() const noexcept override;
    void dialog_presentation_complete() override;
};

class SysProgressDialog : public ::cw::lib::Dialog,
                          public ::cw::dialog::DialogPresentationProvider,
                          public std::enable_shared_from_this<SysProgressDialog> {
public:
    SysProgressDialog() = default;
    virtual ::cw::lib::ui::SystemDialogMetrics system_metrics() const;
    virtual ~SysProgressDialog();
    std::shared_ptr<::cw::lib::Control> m_btn_cncl{};
    bool m_cancel{};
    bool m_cwpy_debug{};
    std::shared_ptr<::cw::lib::Control> m_gauge{};
    ::cw::lib::Integer m_maximum{100};
    std::shared_ptr<::cw::lib::Control> m_message{};
    ::cw::lib::Integer m_minimum{},m_gauge_value{},m_gauge_range{},m_width{380},m_pulse_count{};
    ::cw::lib::String m_text{},m_title{}; bool m_cancelable{};
    bool m_closed{};
    std::uintptr_t m_native_parent_handle{};
    mutable std::mutex m_state_mutex{};
    std::function<bool()> m_completion_predicate{};
    std::function<void()> m_cancel_action{};
    std::atomic<std::uint64_t> m_state_revision{1};
    std::atomic<std::uint64_t> m_presented_revision{0};
    void initialize(std::shared_ptr<::cw::lib::Window> arg_parent, ::cw::lib::String arg_title, ::cw::lib::String arg_message, ::cw::lib::Integer arg_maximum=100, ::cw::lib::Integer arg_minimum=0, bool arg_cancelable=false, ::cw::lib::Integer arg_width=380);
    void UpdateProgress(::cw::lib::Integer arg_value, ::cw::lib::String arg_message);
    void OnClickCancelBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    void _bind();
    void _do_layout();
    ProgressSnapshot snapshot() const;
    void request_cancel();
    void pulse();
    void close();
    bool is_closed() const;
    void sync_ui();
    void set_modal_callbacks(std::function<bool()> arg_completion,
                             std::function<void()> arg_cancel = {});
    [[nodiscard]] ::cw::dialog::DialogPresentation dialog_presentation() override;
    void dialog_presentation_action(::cw::dialog::DialogPresentationAction action,
                                    int index) override;
    void dialog_presentation_key(::cw::dialog::DialogPresentationKey key,bool pressed,
                                 ::cw::dialog::DialogPresentationModifiers modifiers) override;
    void dialog_presentation_enter(::cw::dialog::DialogPresentationModifiers modifiers) override;
    void dialog_presentation_context(float,float) override {}
    [[nodiscard]] bool dialog_presentation_finished() const noexcept override;
    [[nodiscard]] bool dialog_presentation_needs_refresh() const noexcept override;
    void dialog_presentation_complete() override;
};

// 取り消せない処理は、モーダルホストが設定済みの表示を確認してから開始する
// 。ワーカーは同期されたモデル状態だけを更新し、ウィジェットには触れない。
using ProgressUpdate = std::function<void(::cw::lib::Integer, ::cw::lib::String)>;
using ProgressWork = std::function<void(const ProgressUpdate&)>;
bool run_modal_task(const std::shared_ptr<ProgressDialog>& dialog, ProgressWork work);
bool run_modal_task(const std::shared_ptr<SysProgressDialog>& dialog, ProgressWork work);

void main();
}

