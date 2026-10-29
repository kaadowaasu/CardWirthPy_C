#include "capabilities.hpp"

#include "services.hpp"

#include <utility>

namespace cwxeditor::lib {
namespace {

[[nodiscard]] Platform resolve_platform(Platform platform) noexcept {
    // 共通の方針からビルド対象を推測しない。製品の構成側が GuiBackend/SystemBackend::platform()
    // を渡す。 Automatic は、画面な
    // しのテストや不完全なホストで使う安全な値。
    return platform == Platform::Automatic ? Platform::Headless : platform;
}

[[nodiscard]] EditorCapabilityProfile make_default_profile(Platform requested) noexcept {
    EditorCapabilityProfile result;
    result.platform = resolve_platform(requested);

    switch (result.platform) {
    case Platform::Windows:
        result.external_process = true;
        result.multiple_windows = true;
        result.context_menu = true;
        result.drag_drop = true;
        result.file_picker = true;
        result.native_file_picker = true;
        result.clipboard_text = true;
        result.clipboard_custom_format = true;
        result.web_persistence = false;
        result.touch_navigation = false;
        result.long_press_context_menu = false;
        result.long_press_drag_drop = false;
        result.single_screen_navigation = false;
        result.context_menu_mode = EditorContextMenuMode::Native;
        result.drag_drop_mode = EditorDragDropMode::Native;
        result.file_picker_mode = EditorFilePickerMode::Native;
        result.clipboard_mode = EditorClipboardMode::NativeCustomFormat;
        result.persistence_mode = EditorPersistenceMode::NativeDurable;
        result.navigation_mode = EditorNavigationMode::Desktop;
        break;

    case Platform::MacOS:
    case Platform::Linux:
        result.external_process = true;
        result.multiple_windows = true;
        result.context_menu = true;
        result.drag_drop = true;
        result.file_picker = true;
        result.native_file_picker = true;
        result.clipboard_text = true;
        result.clipboard_internal_fallback = true;
        result.context_menu_mode = EditorContextMenuMode::Portable;
        result.drag_drop_mode = EditorDragDropMode::Internal;
        result.file_picker_mode = EditorFilePickerMode::Native;
        result.clipboard_mode = EditorClipboardMode::TextAndInternalFallback;
        result.persistence_mode = EditorPersistenceMode::NativeDurable;
        result.navigation_mode = EditorNavigationMode::Desktop;
        break;

    case Platform::Android:
    case Platform::IOS:
        // SDL は共通のポップアップを提供し、動かさない長押しを副ボタンの
        // 押下・解放へ変換する。 モバイルにはネイティブの右ボタンがないた
        // め、同じコマンドを使えるよう、この経路への対応を示す必要がある。
        result.multiple_windows = false;
        result.context_menu = true;
        result.drag_drop = true;
        result.file_picker = true;
        result.native_file_picker = true;
        result.clipboard_text = true;
        result.clipboard_internal_fallback = true;
        result.touch_navigation = true;
        result.long_press_context_menu = true;
        result.long_press_drag_drop = true;
        result.single_screen_navigation = true;
        result.context_menu_mode = EditorContextMenuMode::Portable;
        result.drag_drop_mode = EditorDragDropMode::Internal;
        result.file_picker_mode = EditorFilePickerMode::Native;
        result.clipboard_mode = EditorClipboardMode::TextAndInternalFallback;
        result.persistence_mode = EditorPersistenceMode::NativeDurable;
        result.navigation_mode = EditorNavigationMode::Touch;
        break;

    case Platform::Web:
        // ブラウザーのファイル入力とドラッグ・ドロップはプラットフォームの機能だ
        // が、 ネイティブのデスクトップダイアログや任意のプロセス起動ではない。
        result.context_menu = true;
        result.drag_drop = true;
        result.file_picker = true;
        result.browser_file_picker = true;
        result.clipboard_text = true;
        result.clipboard_internal_fallback = true;
        result.web_persistence = true;
        result.touch_navigation = true;
        result.long_press_context_menu = true;
        result.long_press_drag_drop = true;
        result.single_screen_navigation = true;
        result.context_menu_mode = EditorContextMenuMode::Portable;
        result.drag_drop_mode = EditorDragDropMode::Internal;
        result.file_picker_mode = EditorFilePickerMode::Browser;
        result.clipboard_mode = EditorClipboardMode::TextAndInternalFallback;
        result.persistence_mode = EditorPersistenceMode::WebIndexedDb;
        result.navigation_mode = EditorNavigationMode::Web;
        break;

    case Platform::Headless:
    case Platform::Automatic:
        // Automatic はこの分岐前に解決済み。
        // 将来 Platform に値が追加されても
        // 安全な代替方針を保てるよう、この分岐も残す。
        result.clipboard_text = false;
        result.clipboard_internal_fallback = true;
        result.persistence_mode = EditorPersistenceMode::Volatile;
        result.navigation_mode = EditorNavigationMode::Desktop;
        break;
    }

    return result;
}

[[nodiscard]] Platform service_platform(const Services& services,
                                        Platform requested) noexcept {
    if (services.gui) {
        const Platform value = services.gui->platform();
        if (value != Platform::Automatic) return value;
    }
    if (services.system) {
        const Platform value = services.system->platform();
        if (value != Platform::Automatic) return value;
    }
    return resolve_platform(requested);
}

[[nodiscard]] EditorCapabilityProfile make_backend_profile(
    const Services& services,
    Platform requested) noexcept {
    EditorCapabilityProfile result = make_default_profile(
        service_platform(services, requested));
    const bool has_gui = services.gui && result.platform != Platform::Headless;
    const bool has_graphics = services.graphics != nullptr;
    // DialogBackend は、メッセージ・色・フォントのダイアログだ
    // けを提供する場合があり、画面なしのバックエンドも意図的にそうしている。
    // ファイル選択を利用可能とするのは、選択・一時保存の寿命を管理する型付
    // きの提供側が登録され、 少なくとも一つの操作を公開している場合だけ。
    const bool has_file_picker = services.file_picker &&
        services.file_picker->capabilities() != FilePickerCapability::None;
    const bool has_clipboard = services.clipboard != nullptr;
    const bool has_drag_drop = services.drag_drop != nullptr;

    // ネイティブUIの各機能は、具体的なバックエンドの組み合
    // わせから求める。 デスクトップ向けのビルドでも、共通・
    // 画面なしのサービス構成なら意図的に未対応の扱いになる。
    result.multiple_windows = result.multiple_windows && has_gui && has_graphics;
    result.context_menu = result.context_menu && has_gui && has_graphics;
    result.drag_drop = result.drag_drop && has_gui && has_drag_drop;
    result.file_picker = result.file_picker && has_gui && has_file_picker;
    result.native_file_picker = result.native_file_picker && has_gui && has_file_picker;
    result.browser_file_picker = result.browser_file_picker && has_gui && has_file_picker;
    result.clipboard_text = result.clipboard_text && has_gui && has_clipboard;
    result.clipboard_custom_format = result.clipboard_custom_format && has_gui && has_clipboard;
    result.external_process = result.external_process && services.processes != nullptr;

    if (!result.context_menu)
        result.context_menu_mode = result.long_press_context_menu
            ? EditorContextMenuMode::LongPress : EditorContextMenuMode::Unsupported;
    if (!result.drag_drop)
        result.drag_drop_mode = result.long_press_drag_drop
            ? EditorDragDropMode::LongPress : EditorDragDropMode::Unsupported;
    result.file_picker_mode = result.native_file_picker
        ? EditorFilePickerMode::Native
        : (result.browser_file_picker && result.file_picker
               ? EditorFilePickerMode::Browser
               : EditorFilePickerMode::Unsupported);
    if (!result.clipboard_text && !result.clipboard_custom_format) {
        result.clipboard_mode = EditorClipboardMode::Unsupported;
    } else if (result.clipboard_custom_format) {
        result.clipboard_mode = EditorClipboardMode::NativeCustomFormat;
    } else if (result.clipboard_internal_fallback) {
        result.clipboard_mode = EditorClipboardMode::TextAndInternalFallback;
    } else {
        result.clipboard_mode = EditorClipboardMode::TextOnly;
    }
    if (result.platform == Platform::Headless) {
        result.persistence_mode = EditorPersistenceMode::Volatile;
        result.navigation_mode = EditorNavigationMode::Desktop;
    }
    return result;
}

template <typename T>
[[nodiscard]] bool override_or(const std::optional<bool>& value, T fallback) noexcept {
    return value.has_value() ? *value : static_cast<bool>(fallback);
}

}  // namespace

bool EditorCapabilityProfile::supports(EditorCapability capability) const noexcept {
    switch (capability) {
    case EditorCapability::ExternalProcess:
        return external_process;
    case EditorCapability::MultipleWindows:
        return multiple_windows;
    case EditorCapability::ContextMenu:
        return context_menu;
    case EditorCapability::DragDrop:
        return drag_drop;
    case EditorCapability::NativeFilePicker:
        return native_file_picker;
    case EditorCapability::ClipboardCustomFormat:
        return clipboard_custom_format;
    case EditorCapability::WebPersistence:
        return web_persistence;
    case EditorCapability::TouchNavigation:
        return touch_navigation;
    case EditorCapability::ClipboardInternalFallback:
        return clipboard_internal_fallback;
    case EditorCapability::LongPressContextMenu:
        return long_press_context_menu;
    case EditorCapability::LongPressDragDrop:
        return long_press_drag_drop;
    }
    return false;
}

CapabilitySupport EditorCapabilityProfile::support(EditorCapability capability) const noexcept {
    if (!supports(capability)) {
        return CapabilitySupport::Unsupported;
    }

    switch (capability) {
    case EditorCapability::ExternalProcess:
    case EditorCapability::MultipleWindows:
    case EditorCapability::ContextMenu:
    case EditorCapability::DragDrop:
    case EditorCapability::ClipboardCustomFormat:
        return CapabilitySupport::Native;

    case EditorCapability::NativeFilePicker:
        return native_file_picker ? CapabilitySupport::Native : CapabilitySupport::Unsupported;

    case EditorCapability::WebPersistence:
        return web_persistence ? CapabilitySupport::Native : CapabilitySupport::Unsupported;

    case EditorCapability::TouchNavigation:
        return navigation_mode == EditorNavigationMode::Touch
            ? CapabilitySupport::Native
            : (navigation_mode == EditorNavigationMode::Web
                   ? CapabilitySupport::Emulated
                   : CapabilitySupport::Unsupported);

    case EditorCapability::ClipboardInternalFallback:
    case EditorCapability::LongPressContextMenu:
    case EditorCapability::LongPressDragDrop:
        return CapabilitySupport::Emulated;
    }
    return CapabilitySupport::Unsupported;
}

EditorPlatformCapabilities::EditorPlatformCapabilities(Platform platform)
    : platform_(resolve_platform(platform)) {}

Platform EditorPlatformCapabilities::platform() const noexcept {
    return platform_;
}

EditorCapabilityProfile EditorPlatformCapabilities::profile() const noexcept {
    EditorCapabilityProfile result = make_default_profile(resolved_platform());

    result.external_process = supports_external_process();
    result.multiple_windows = supports_multiple_windows();
    result.context_menu = supports_context_menu();
    result.drag_drop = supports_drag_drop();
    result.file_picker = supports_file_picker();
    result.native_file_picker = supports_native_file_picker();
    result.browser_file_picker = supports_browser_file_picker();
    result.clipboard_text = supports_clipboard_text();
    result.clipboard_custom_format = supports_clipboard_custom_format();
    result.clipboard_internal_fallback = supports_clipboard_internal_fallback();
    result.web_persistence = supports_web_persistence();
    result.touch_navigation = supports_touch_navigation();
    result.long_press_context_menu = supports_long_press_context_menu();
    result.long_press_drag_drop = supports_long_press_drag_drop();
    result.single_screen_navigation = supports_single_screen_navigation();

    if (!result.context_menu)
        result.context_menu_mode = result.long_press_context_menu
            ? EditorContextMenuMode::LongPress : EditorContextMenuMode::Unsupported;
    else if (result.context_menu_mode == EditorContextMenuMode::Unsupported)
        result.context_menu_mode = EditorContextMenuMode::Native;
    if (!result.drag_drop)
        result.drag_drop_mode = result.long_press_drag_drop
            ? EditorDragDropMode::LongPress : EditorDragDropMode::Unsupported;
    else if (result.drag_drop_mode == EditorDragDropMode::Unsupported)
        result.drag_drop_mode = EditorDragDropMode::Native;
    result.file_picker_mode = result.native_file_picker
        ? EditorFilePickerMode::Native
        : (result.browser_file_picker && result.file_picker ? EditorFilePickerMode::Browser
                                                            : EditorFilePickerMode::Unsupported);
    if (!result.clipboard_text && !result.clipboard_custom_format) {
        result.clipboard_mode = EditorClipboardMode::Unsupported;
    } else if (result.clipboard_custom_format) {
        result.clipboard_mode = EditorClipboardMode::NativeCustomFormat;
    } else if (result.clipboard_internal_fallback) {
        result.clipboard_mode = EditorClipboardMode::TextAndInternalFallback;
    } else {
        result.clipboard_mode = EditorClipboardMode::TextOnly;
    }
    result.persistence_mode = result.web_persistence
        ? EditorPersistenceMode::WebIndexedDb
        : (resolved_platform() == Platform::Headless ? EditorPersistenceMode::Volatile
                                                      : EditorPersistenceMode::NativeDurable);
    result.navigation_mode = resolved_platform() == Platform::Web
        ? EditorNavigationMode::Web
        : (result.touch_navigation ? EditorNavigationMode::Touch : EditorNavigationMode::Desktop);

    return result;
}

Capabilities EditorPlatformCapabilities::capabilities() const noexcept {
    const EditorCapabilityProfile current = profile();
    Capabilities result = default_capabilities(current.platform);
    result.file_picker = current.file_picker;
    result.clipboard = current.clipboard_text;
    result.drag_drop = current.drag_drop;
    result.external_process = current.external_process;
    result.persistent_storage = current.persistence_mode != EditorPersistenceMode::Volatile;
    result.multiple_windows = current.multiple_windows;
    result.context_menu = current.context_menu;
    result.native_file_picker = current.native_file_picker;
    result.browser_file_picker = current.browser_file_picker;
    result.clipboard_custom_format = current.clipboard_custom_format;
    result.clipboard_internal_fallback = current.clipboard_internal_fallback;
    result.touch_navigation = current.touch_navigation;
    result.long_press_context_menu = current.long_press_context_menu;
    result.long_press_drag_drop = current.long_press_drag_drop;
    result.single_screen_navigation = current.single_screen_navigation;
    return result;
}

bool EditorPlatformCapabilities::supports_external_process() const noexcept {
    return make_default_profile(resolved_platform()).external_process;
}

bool EditorPlatformCapabilities::supports_multiple_windows() const noexcept {
    return make_default_profile(resolved_platform()).multiple_windows;
}

bool EditorPlatformCapabilities::supports_context_menu() const noexcept {
    return make_default_profile(resolved_platform()).context_menu;
}

bool EditorPlatformCapabilities::supports_drag_drop() const noexcept {
    return make_default_profile(resolved_platform()).drag_drop;
}

bool EditorPlatformCapabilities::supports_file_picker() const noexcept {
    return make_default_profile(resolved_platform()).file_picker;
}

bool EditorPlatformCapabilities::supports_native_file_picker() const noexcept {
    return make_default_profile(resolved_platform()).native_file_picker;
}

bool EditorPlatformCapabilities::supports_browser_file_picker() const noexcept {
    return make_default_profile(resolved_platform()).browser_file_picker;
}

bool EditorPlatformCapabilities::supports_clipboard_text() const noexcept {
    return make_default_profile(resolved_platform()).clipboard_text;
}

bool EditorPlatformCapabilities::supports_clipboard_custom_format() const noexcept {
    return make_default_profile(resolved_platform()).clipboard_custom_format;
}

bool EditorPlatformCapabilities::supports_clipboard_internal_fallback() const noexcept {
    return make_default_profile(resolved_platform()).clipboard_internal_fallback;
}

bool EditorPlatformCapabilities::supports_web_persistence() const noexcept {
    return make_default_profile(resolved_platform()).web_persistence;
}

bool EditorPlatformCapabilities::supports_touch_navigation() const noexcept {
    return make_default_profile(resolved_platform()).touch_navigation;
}

bool EditorPlatformCapabilities::supports_long_press_context_menu() const noexcept {
    return make_default_profile(resolved_platform()).long_press_context_menu;
}

bool EditorPlatformCapabilities::supports_long_press_drag_drop() const noexcept {
    return make_default_profile(resolved_platform()).long_press_drag_drop;
}

bool EditorPlatformCapabilities::supports_single_screen_navigation() const noexcept {
    return make_default_profile(resolved_platform()).single_screen_navigation;
}

bool EditorPlatformCapabilities::supports(EditorCapability capability) const noexcept {
    switch (capability) {
    case EditorCapability::ExternalProcess:
        return supports_external_process();
    case EditorCapability::MultipleWindows:
        return supports_multiple_windows();
    case EditorCapability::ContextMenu:
        return supports_context_menu();
    case EditorCapability::DragDrop:
        return supports_drag_drop();
    case EditorCapability::NativeFilePicker:
        return supports_native_file_picker();
    case EditorCapability::ClipboardCustomFormat:
        return supports_clipboard_custom_format();
    case EditorCapability::WebPersistence:
        return supports_web_persistence();
    case EditorCapability::TouchNavigation:
        return supports_touch_navigation();
    case EditorCapability::ClipboardInternalFallback:
        return supports_clipboard_internal_fallback();
    case EditorCapability::LongPressContextMenu:
        return supports_long_press_context_menu();
    case EditorCapability::LongPressDragDrop:
        return supports_long_press_drag_drop();
    }
    return false;
}

CapabilitySupport EditorPlatformCapabilities::support(EditorCapability capability) const noexcept {
    return profile().support(capability);
}

EditorContextMenuMode EditorPlatformCapabilities::context_menu_mode() const noexcept {
    return profile().context_menu_mode;
}

EditorDragDropMode EditorPlatformCapabilities::drag_drop_mode() const noexcept {
    return profile().drag_drop_mode;
}

EditorFilePickerMode EditorPlatformCapabilities::file_picker_mode() const noexcept {
    return profile().file_picker_mode;
}

EditorClipboardMode EditorPlatformCapabilities::clipboard_mode() const noexcept {
    return profile().clipboard_mode;
}

EditorPersistenceMode EditorPlatformCapabilities::persistence_mode() const noexcept {
    return profile().persistence_mode;
}

EditorNavigationMode EditorPlatformCapabilities::navigation_mode() const noexcept {
    return profile().navigation_mode;
}

OverridableEditorPlatformCapabilities::OverridableEditorPlatformCapabilities(
    Platform platform,
    EditorCapabilityOverrides overrides)
    : EditorPlatformCapabilities(platform), overrides_(std::move(overrides)) {}

bool OverridableEditorPlatformCapabilities::supports_external_process() const noexcept {
    return override_or(overrides_.external_process,
                       EditorPlatformCapabilities::supports_external_process());
}

bool OverridableEditorPlatformCapabilities::supports_multiple_windows() const noexcept {
    return override_or(overrides_.multiple_windows,
                       EditorPlatformCapabilities::supports_multiple_windows());
}

bool OverridableEditorPlatformCapabilities::supports_context_menu() const noexcept {
    return override_or(overrides_.context_menu,
                       EditorPlatformCapabilities::supports_context_menu());
}

bool OverridableEditorPlatformCapabilities::supports_drag_drop() const noexcept {
    return override_or(overrides_.drag_drop,
                       EditorPlatformCapabilities::supports_drag_drop());
}

bool OverridableEditorPlatformCapabilities::supports_file_picker() const noexcept {
    return override_or(overrides_.file_picker,
                       EditorPlatformCapabilities::supports_file_picker());
}

bool OverridableEditorPlatformCapabilities::supports_native_file_picker() const noexcept {
    return override_or(overrides_.native_file_picker,
                       EditorPlatformCapabilities::supports_native_file_picker());
}

bool OverridableEditorPlatformCapabilities::supports_browser_file_picker() const noexcept {
    return override_or(overrides_.browser_file_picker,
                       EditorPlatformCapabilities::supports_browser_file_picker());
}

bool OverridableEditorPlatformCapabilities::supports_clipboard_text() const noexcept {
    return override_or(overrides_.clipboard_text,
                       EditorPlatformCapabilities::supports_clipboard_text());
}

bool OverridableEditorPlatformCapabilities::supports_clipboard_custom_format() const noexcept {
    return override_or(overrides_.clipboard_custom_format,
                       EditorPlatformCapabilities::supports_clipboard_custom_format());
}

bool OverridableEditorPlatformCapabilities::supports_clipboard_internal_fallback() const noexcept {
    return override_or(overrides_.clipboard_internal_fallback,
                       EditorPlatformCapabilities::supports_clipboard_internal_fallback());
}

bool OverridableEditorPlatformCapabilities::supports_web_persistence() const noexcept {
    return override_or(overrides_.web_persistence,
                       EditorPlatformCapabilities::supports_web_persistence());
}

bool OverridableEditorPlatformCapabilities::supports_touch_navigation() const noexcept {
    return override_or(overrides_.touch_navigation,
                       EditorPlatformCapabilities::supports_touch_navigation());
}

bool OverridableEditorPlatformCapabilities::supports_long_press_context_menu() const noexcept {
    return override_or(overrides_.long_press_context_menu,
                       EditorPlatformCapabilities::supports_long_press_context_menu());
}

bool OverridableEditorPlatformCapabilities::supports_long_press_drag_drop() const noexcept {
    return override_or(overrides_.long_press_drag_drop,
                       EditorPlatformCapabilities::supports_long_press_drag_drop());
}

bool OverridableEditorPlatformCapabilities::supports_single_screen_navigation() const noexcept {
    return override_or(overrides_.single_screen_navigation,
                       EditorPlatformCapabilities::supports_single_screen_navigation());
}

EditorCapabilityProfile default_editor_capability_profile(Platform platform) noexcept {
    return make_default_profile(platform);
}

Capabilities default_capabilities(Platform platform) noexcept {
    const EditorCapabilityProfile profile = make_default_profile(platform);
    Capabilities result;

    result.audio = profile.platform != Platform::Headless;
    result.file_picker = profile.file_picker;
    result.clipboard = profile.clipboard_text;
    result.text_input = profile.platform != Platform::Headless;
    result.drag_drop = profile.drag_drop;
    result.dispatcher = profile.platform != Platform::Headless;
    result.tasks = profile.platform != Platform::Headless;
    result.external_process = profile.external_process;
    result.shell = profile.external_process;
    result.persistent_storage = profile.platform != Platform::Headless;

    result.multiple_windows = profile.multiple_windows;
    result.context_menu = profile.context_menu;
    result.native_file_picker = profile.native_file_picker;
    result.browser_file_picker = profile.browser_file_picker;
    result.clipboard_custom_format = profile.clipboard_custom_format;
    result.clipboard_internal_fallback = profile.clipboard_internal_fallback;
    result.touch_navigation = profile.touch_navigation;
    result.long_press_context_menu = profile.long_press_context_menu;
    result.long_press_drag_drop = profile.long_press_drag_drop;
    result.single_screen_navigation = profile.single_screen_navigation;
    return result;
}

EditorCapabilityProfile probe_editor_capability_profile(
    const Services& services,
    Platform platform) noexcept {
    return make_backend_profile(services, platform);
}

Capabilities probe_capabilities(const Services& services,
                                Platform platform) noexcept {
    const EditorCapabilityProfile profile =
        make_backend_profile(services, platform);
    Capabilities result = default_capabilities(profile.platform);
    result.audio = profile.platform != Platform::Headless && services.audio != nullptr;
    result.file_picker = profile.file_picker;
    result.clipboard = profile.clipboard_text;
    result.text_input = profile.platform != Platform::Headless && services.text != nullptr;
    result.drag_drop = profile.drag_drop;
    result.dispatcher = profile.platform != Platform::Headless && services.dispatcher != nullptr;
    result.tasks = profile.platform != Platform::Headless && services.tasks != nullptr;
    result.external_process = profile.external_process;
    result.shell = profile.external_process && services.shell != nullptr;
    result.persistent_storage = profile.platform != Platform::Headless;
    result.multiple_windows = profile.multiple_windows;
    result.context_menu = profile.context_menu;
    result.native_file_picker = profile.native_file_picker;
    result.browser_file_picker = profile.browser_file_picker;
    result.clipboard_custom_format = profile.clipboard_custom_format;
    result.clipboard_internal_fallback = profile.clipboard_internal_fallback;
    result.touch_navigation = profile.touch_navigation;
    result.long_press_context_menu = profile.long_press_context_menu;
    result.long_press_drag_drop = profile.long_press_drag_drop;
    result.single_screen_navigation = profile.single_screen_navigation;
    return result;
}

std::shared_ptr<EditorPlatformCapabilities> make_editor_platform_capabilities(
    Platform platform,
    EditorCapabilityOverrides overrides) {
    return std::make_shared<OverridableEditorPlatformCapabilities>(
        platform, std::move(overrides));
}

std::shared_ptr<EditorPlatformCapabilities> make_editor_capabilities(
    Platform platform,
    EditorCapabilityOverrides overrides) {
    return make_editor_platform_capabilities(platform, std::move(overrides));
}

}  // namespace cwxeditor::lib
