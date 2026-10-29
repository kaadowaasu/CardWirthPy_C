#pragma once

// UIの実装は一つに統一し、
// 別経路の実装を設けない。

// cw/dialog/editscenariodb.py から移植した、シナリオ DB の構築状態と処理手順。
#include "cw/lib/compat.hpp"
#include "cw/dialog/presentation.hpp"
#include "cw/fwd.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace cw::dialog::editscenariodb {
using ScenarioDbPaths = std::vector<std::pair<::cw::lib::String, std::vector<::cw::lib::String>>>;

class ConstructScenarioDB : public ::cw::lib::Dialog,
                            public ::cw::dialog::DialogPresentationProvider,
                            public std::enable_shared_from_this<ConstructScenarioDB> {
public:
    ConstructScenarioDB() = default;
    virtual ~ConstructScenarioDB();
    std::atomic_bool m__cancel{};
    bool m__clear{};
    std::atomic_bool m__complete{};
    std::atomic_bool m__failed{};
    std::atomic<::cw::lib::Integer> m__curnum{};
    ::cw::lib::String m__error_message{};
    ::cw::lib::String m__message{};
    ::cw::lib::Value m_clear{};
    ::cw::lib::Value m_cnclbtn{};
    bool m_cwpy_debug{};
    ScenarioDbPaths m_dpaths{};
    ::cw::lib::Value m_okbtn{};
    ::cw::lib::Value m_text{};
    // Py と同じ順序で、更新パスを見つけた後に遅延して開く。フ
    // ァクトリーは共通の Frame の DB API を使う。
    std::shared_ptr<::cw::scenariodb::Scenariodb> m_database{};
    std::function<std::shared_ptr<::cw::scenariodb::Scenariodb>()> m_open_database{};
    mutable std::mutex m_state_mutex{};
    std::thread m_worker{};
    bool m_started{};
    bool m_closed{};
    bool m_modal_active{};
    int m_focus_index{1}; // チェックボックス、既定の開始ボタン、取り消しボタン。
    void initialize(std::shared_ptr<::cw::lib::Window> arg_parent, ScenarioDbPaths arg_dpaths);
    void _bind();
    void _do_layout();
    virtual void construct_scenariodb();
    void OnClickOkBtn(std::shared_ptr<::cw::lib::Window> arg_event);
    void set_clear(bool arg_clear);
    void set_cancel(bool arg_cancel);
    bool is_complete() const;
    bool is_cancelled() const;
    ::cw::lib::Integer current_progress() const;
    ::cw::lib::String message() const;
    ::cw::lib::String error_message() const;
    void poll_ui();
    [[nodiscard]] ::cw::dialog::DialogPresentation dialog_presentation() override;
    void dialog_presentation_action(::cw::dialog::DialogPresentationAction action,
                                    int index) override;
    void dialog_presentation_key(::cw::dialog::DialogPresentationKey key, bool pressed,
                                 ::cw::dialog::DialogPresentationModifiers modifiers) override;
    void dialog_presentation_enter(::cw::dialog::DialogPresentationModifiers modifiers) override;
    // システム形式の wx ダイアログは、通常の右クリックでは閉じない。
    void dialog_presentation_context(float, float) override {}
    [[nodiscard]] bool dialog_presentation_finished() const noexcept override;
    void dialog_presentation_complete() override;
private:
    void start_worker();
    void run_construction();
    void reset_prompt();
    bool show_result(bool failed);
};

::cw::lib::Value ConstructScenarioDB_construct_scenariodb_lambda_line_93();
void ConstructScenarioDB_OnClickOkBtn_progress();
void main();
}

