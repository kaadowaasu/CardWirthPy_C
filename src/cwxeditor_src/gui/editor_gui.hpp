#pragma once

#include "../core/editor_core.hpp"
#include "../lib/services.hpp"

#include <memory>
#include <utility>

namespace cwxeditor::gui {

// 部品を組み合わせるGUIの構成元。HWND・DWT・SDL・Cocoa・GTK・DOM
// ・ブラウザーAPIは公開せず、 libのバックエンドがServicesを実装する。
class EditorGui {
public:
    EditorGui(std::shared_ptr<::cwxeditor::core::EditorCore> core,
              ::cwxeditor::lib::Services services)
        : core_(std::move(core)), services_(std::move(services)) {}

    [[nodiscard]] const std::shared_ptr<::cwxeditor::core::EditorCore>& core() const noexcept { return core_; }
    [[nodiscard]] const ::cwxeditor::lib::Services& services() const noexcept { return services_; }

private:
    std::shared_ptr<::cwxeditor::core::EditorCore> core_{};
    ::cwxeditor::lib::Services services_{};
};

}  // namespace cwxeditor::gui
