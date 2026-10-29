#include "file_picker_composition.hpp"

#include <utility>

namespace cwxeditor::lib {
namespace {

[[nodiscard]] bool web_capabilities_match(const FilePickerCapabilities capabilities,
                                          const WebFilePickerCallbacks& callbacks) noexcept {
    const auto pick_operations = FilePickerCapability::OpenFile |
                                 FilePickerCapability::OpenFiles |
                                 FilePickerCapability::SelectDirectory |
                                 FilePickerCapability::Import;
    const auto export_operations = FilePickerCapability::SaveFile |
                                   FilePickerCapability::Export;
    return (!has_capability(capabilities & pick_operations, FilePickerCapability::OpenFile) &&
            !has_capability(capabilities & pick_operations, FilePickerCapability::OpenFiles) &&
            !has_capability(capabilities & pick_operations, FilePickerCapability::SelectDirectory) &&
            !has_capability(capabilities & pick_operations, FilePickerCapability::Import) ||
            static_cast<bool>(callbacks.pick)) &&
           (!has_capability(capabilities & export_operations, FilePickerCapability::SaveFile) &&
            !has_capability(capabilities & export_operations, FilePickerCapability::Export) ||
            static_cast<bool>(callbacks.export_blob));
}

[[nodiscard]] FilePickerCapabilities derived_web_capabilities(
    const WebFilePickerCallbacks& callbacks) noexcept {
    FilePickerCapabilities result{FilePickerCapability::None};
    if (callbacks.pick) result |= FilePickerCapability::OpenFile |
                                  FilePickerCapability::OpenFiles |
                                  FilePickerCapability::SelectDirectory |
                                  FilePickerCapability::Import;
    if (callbacks.export_blob) result |= FilePickerCapability::SaveFile |
                                         FilePickerCapability::Export;
    return result;
}

}  // namespace

std::shared_ptr<FilePicker> make_file_picker(FilePickerComposition composition) {
    switch (composition.mode) {
    case EditorFilePickerMode::Native:
        if (!composition.native_dialogs ||
            composition.native_capabilities == FilePickerCapability::None)
            return {};
        return std::make_shared<NativeFilePicker>(std::move(composition.native_dialogs),
                                                  composition.native_capabilities);
    case EditorFilePickerMode::Browser: {
        const auto capabilities = composition.web_capabilities == FilePickerCapability::None
            ? derived_web_capabilities(composition.web_callbacks)
            : composition.web_capabilities;
        if (capabilities == FilePickerCapability::None ||
            !web_capabilities_match(capabilities, composition.web_callbacks))
            return {};
        return std::make_shared<WebFilePicker>(std::move(composition.web_callbacks), capabilities);
    }
    case EditorFilePickerMode::Unsupported:
        return {};
    }
    return {};
}

}  // namespace cwxeditor::lib
