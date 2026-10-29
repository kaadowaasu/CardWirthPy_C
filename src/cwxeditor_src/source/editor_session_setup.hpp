#pragma once
#include "cwxeditor_src/core/cwx/props.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dprops.hpp"
#include "cwxeditor_src/lib/runtime_context.hpp"
#include <memory>
namespace cwxeditor::cwxeditor {
struct EditorModelSession {
    std::unique_ptr<::cwxeditor::cwx::props::CProps> core;
    std::unique_ptr<::cwxeditor::cwx::editor::gui::dwt::dprops::Props> properties;
};
void prepare_editor_resources();
[[nodiscard]] EditorModelSession make_editor_model_session(
    ::cwxeditor::lib::EditorRuntimeContext& runtime,
    const ::cwxeditor::cwx::system::System& system,
    ::cwxeditor::lib::String app_path,
    ::cwxeditor::lib::String config = {},
    ::cwxeditor::lib::String default_skin = {});
}
