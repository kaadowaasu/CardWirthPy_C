#pragma once

#include "types.hpp"

#include <cstdint>
#include <memory>
#include <optional>

namespace cwxeditor::lib {

struct Services;

// 能力情報は、Editorがホストへ要求できる操作
// を表す。ネイティブ機能だけでなく、 デスクトップ
// のドラッグを長押しで代替するなどの方式も区別する
// 。 Editor・コアをHWND・Cocoa・
// Android・ブラウザーの型から独立させる。
enum class CapabilitySupport : std::uint8_t {
    Unsupported,
    Emulated,
    Native,
};

enum class EditorCapability : std::uint8_t {
    ExternalProcess,
    MultipleWindows,
    ContextMenu,
    DragDrop,
    NativeFilePicker,
    ClipboardCustomFormat,
    WebPersistence,
    TouchNavigation,
    ClipboardInternalFallback,
    LongPressContextMenu,
    LongPressDragDrop,
};

enum class EditorContextMenuMode : std::uint8_t {
    Unsupported,
    Native,
    Portable,
    LongPress,
};

enum class EditorDragDropMode : std::uint8_t {
    Unsupported,
    Native,
    Internal,
    LongPress,
};

enum class EditorFilePickerMode : std::uint8_t {
    Unsupported,
    Native,
    Browser,
};

enum class EditorClipboardMode : std::uint8_t {
    Unsupported,
    TextOnly,
    NativeCustomFormat,
    TextAndInternalFallback,
};

enum class EditorPersistenceMode : std::uint8_t {
    Volatile,
    NativeDurable,
    WebIndexedDb,
};

enum class EditorNavigationMode : std::uint8_t {
    Desktop,
    Touch,
    Web,
};

// EditorRuntimeContextが使
// う、データだけの小さな能力設定。 実行環境の
// 構築にはコピー可能な設定、GUIには仮想の機
// 能別問い合わせと代替方式を用意する。 同じヘ
// ッダーに置き、埋め込みEditorがホスト実
// 装を知らずに一つの設定を渡せるようにする。
struct Capabilities {
    bool audio{true};
    bool file_picker{true};
    bool clipboard{true};
    bool text_input{true};
    bool drag_drop{true};
    bool dispatcher{true};
    bool tasks{true};
    bool external_process{};
    bool shell{};
    bool persistent_storage{true};

    bool multiple_windows{true};
    bool context_menu{true};
    bool native_file_picker{true};
    bool browser_file_picker{};
    bool clipboard_custom_format{true};
    bool clipboard_internal_fallback{};
    bool touch_navigation{};
    bool long_press_context_menu{};
    bool long_press_drag_drop{};
    bool single_screen_navigation{};

    [[nodiscard]] static Capabilities desktop() noexcept {
        Capabilities result;
        result.external_process = true;
        result.shell = true;
        return result;
    }

    [[nodiscard]] static Capabilities mobile() noexcept {
        Capabilities result;
        result.external_process = false;
        result.shell = false;
        // SDLのUIは内部ドラッグと共通のコンテキストメニューに
        // 対応する。 タッチでは動かさない長押しでメニューを開く。
        result.drag_drop = true;
        result.context_menu = true;
        result.multiple_windows = false;
        result.clipboard_custom_format = false;
        result.clipboard_internal_fallback = true;
        result.touch_navigation = true;
        result.long_press_context_menu = true;
        result.long_press_drag_drop = true;
        result.single_screen_navigation = true;
        return result;
    }

    [[nodiscard]] static Capabilities web() noexcept {
        Capabilities result;
        result.external_process = false;
        result.shell = false;
        result.multiple_windows = false;
        result.native_file_picker = false;
        result.browser_file_picker = true;
        result.clipboard_custom_format = false;
        result.clipboard_internal_fallback = true;
        result.touch_navigation = true;
        result.long_press_context_menu = true;
        result.single_screen_navigation = true;
        return result;
    }

    [[nodiscard]] static Capabilities headless() noexcept {
        Capabilities result;
        result.audio = false;
        result.file_picker = false;
        result.clipboard = false;
        result.text_input = false;
        result.drag_drop = false;
        result.dispatcher = false;
        result.tasks = false;
        result.external_process = false;
        result.shell = false;
        result.persistent_storage = false;
        result.multiple_windows = false;
        result.context_menu = false;
        result.native_file_picker = false;
        result.browser_file_picker = false;
        result.clipboard_custom_format = false;
        result.clipboard_internal_fallback = true;
        return result;
    }

    [[nodiscard]] bool supports_audio() const noexcept { return audio; }
    [[nodiscard]] bool supports_file_picker() const noexcept { return file_picker; }
    [[nodiscard]] bool supports_clipboard() const noexcept { return clipboard; }
    [[nodiscard]] bool supports_text_input() const noexcept { return text_input; }
    [[nodiscard]] bool supports_drag_drop() const noexcept { return drag_drop; }
    [[nodiscard]] bool supports_dispatcher() const noexcept { return dispatcher; }
    [[nodiscard]] bool supports_tasks() const noexcept { return tasks; }
    [[nodiscard]] bool supports_external_process() const noexcept {
        return external_process;
    }
    [[nodiscard]] bool supports_shell() const noexcept { return shell; }
    [[nodiscard]] bool supports_persistent_storage() const noexcept {
        return persistent_storage;
    }
    [[nodiscard]] bool supports_multiple_windows() const noexcept {
        return multiple_windows;
    }
    [[nodiscard]] bool supports_context_menu() const noexcept { return context_menu; }
    [[nodiscard]] bool supports_native_file_picker() const noexcept {
        return native_file_picker;
    }
    [[nodiscard]] bool supports_browser_file_picker() const noexcept {
        return browser_file_picker;
    }
    [[nodiscard]] bool supports_clipboard_custom_format() const noexcept {
        return clipboard_custom_format;
    }
    [[nodiscard]] bool supports_clipboard_internal_fallback() const noexcept {
        return clipboard_internal_fallback;
    }
    [[nodiscard]] bool supports_touch_navigation() const noexcept {
        return touch_navigation;
    }
    [[nodiscard]] bool supports_long_press_context_menu() const noexcept {
        return long_press_context_menu;
    }
    [[nodiscard]] bool supports_long_press_drag_drop() const noexcept {
        return long_press_drag_drop;
    }
    [[nodiscard]] bool supports_single_screen_navigation() const noexcept {
        return single_screen_navigation;
    }
};

// 一フレームの判断をキャッシュしたり、ホストの機能
// 一覧を診断へ公開したりするための値。 boolの
// 名前を下の公開問い合わせと揃え、内部のコールバッ
// クを公開せずに保存形式へ変換できるようにする。
struct EditorCapabilityProfile {
    Platform platform{Platform::Headless};

    bool external_process{};
    bool multiple_windows{};
    bool context_menu{};
    bool drag_drop{};
    bool file_picker{};
    bool native_file_picker{};
    bool browser_file_picker{};
    bool clipboard_text{};
    bool clipboard_custom_format{};
    bool clipboard_internal_fallback{};
    bool web_persistence{};
    bool touch_navigation{};
    bool long_press_context_menu{};
    bool long_press_drag_drop{};
    bool single_screen_navigation{};

    EditorContextMenuMode context_menu_mode{EditorContextMenuMode::Unsupported};
    EditorDragDropMode drag_drop_mode{EditorDragDropMode::Unsupported};
    EditorFilePickerMode file_picker_mode{EditorFilePickerMode::Unsupported};
    EditorClipboardMode clipboard_mode{EditorClipboardMode::Unsupported};
    EditorPersistenceMode persistence_mode{EditorPersistenceMode::Volatile};
    EditorNavigationMode navigation_mode{EditorNavigationMode::Desktop};

    [[nodiscard]] bool supports(EditorCapability capability) const noexcept;
    [[nodiscard]] CapabilitySupport support(EditorCapability capability) const noexcept;
};

// 各任意設定は独立させる。例えばブラウザーのファ
// イル選択を残して外部プロセスを禁止したり、 デ
// スクトップのメニューを有効にしたまま独自の長押
// しを使ったりできる。 テストや、ネイティブ実装
// が揃っていない埋め込み環境の設定にも使える。
struct EditorCapabilityOverrides {
    std::optional<bool> external_process;
    std::optional<bool> multiple_windows;
    std::optional<bool> context_menu;
    std::optional<bool> drag_drop;
    std::optional<bool> file_picker;
    std::optional<bool> native_file_picker;
    std::optional<bool> browser_file_picker;
    std::optional<bool> clipboard_text;
    std::optional<bool> clipboard_custom_format;
    std::optional<bool> clipboard_internal_fallback;
    std::optional<bool> web_persistence;
    std::optional<bool> touch_navigation;
    std::optional<bool> long_press_context_menu;
    std::optional<bool> long_press_drag_drop;
    std::optional<bool> single_screen_navigation;
};

// プラットフォームに依存しない仮想の能力提供元。全メソ
// ッドに選択Platform由来の既定値を持たせ、
// 直接使用・一部だけ派生で変更・下の上書き用ファクト
// リーのいずれも使えるようにする。 実際の処理は既存
// のProcess/Dialog/Clipboard/DragDropBackend
// が行い、 ここではEditorから
// その操作を提供するかだけを決める。
class EditorPlatformCapabilities {
public:
    explicit EditorPlatformCapabilities(Platform platform = Platform::Automatic);
    virtual ~EditorPlatformCapabilities() = default;

    [[nodiscard]] virtual Platform platform() const noexcept;
    [[nodiscard]] virtual EditorCapabilityProfile profile() const noexcept;
    [[nodiscard]] virtual Capabilities capabilities() const noexcept;

    // 既定ではデスクトップ専用。falseの場合、Web・
    // モバイルはコマンドを隠すかアプリ内の代替を提供する。
    [[nodiscard]] virtual bool supports_external_process() const noexcept;

    // 複数のネイティブウィンドウはデスクトップの機能。 モバ
    // イル・Webは一つのEditor画面と画面遷移を使う。
    [[nodiscard]] virtual bool supports_multiple_windows() const noexcept;

    // ネイティブの右クリック・コンテキストメニューとデスクトップのド
    // ラッグ。 モバイルの代替は、下の長押しの問い合わせで公開する。
    [[nodiscard]] virtual bool supports_context_menu() const noexcept;
    [[nodiscard]] virtual bool supports_drag_drop() const noexcept;

    // ブラウザーのファイル選択にも対応するが、デスクトップのネイティブ画
    // 面とは区別する。 取り込み・書き出しの説明文などでこの違いを使う。
    [[nodiscard]] virtual bool supports_file_picker() const noexcept;
    [[nodiscard]] virtual bool supports_native_file_picker() const noexcept;
    [[nodiscard]] virtual bool supports_browser_file_picker() const noexcept;

    // 独自クリップボード形式はデスクトップでネイティブ対応
    // する。 Web・モバイルの既定では文字列を使え、XML
    // ・バイナリーにはEditor内部の代替を使う。
    [[nodiscard]] virtual bool supports_clipboard_text() const noexcept;
    [[nodiscard]] virtual bool supports_clipboard_custom_format() const noexcept;
    [[nodiscard]] virtual bool supports_clipboard_internal_fallback() const noexcept;

    // WebはIndexedDBを使うStorageで永
    // 続化し、ネイティブは各環境の永続保存実装を使う。
    [[nodiscard]] virtual bool supports_web_persistence() const noexcept;

    // タッチ・モバイルの画面遷移と、デスクトップ機能に対するモバイルの代替。
    [[nodiscard]] virtual bool supports_touch_navigation() const noexcept;
    [[nodiscard]] virtual bool supports_long_press_context_menu() const noexcept;
    [[nodiscard]] virtual bool supports_long_press_drag_drop() const noexcept;
    [[nodiscard]] virtual bool supports_single_screen_navigation() const noexcept;

    [[nodiscard]] virtual bool supports(EditorCapability capability) const noexcept;
    [[nodiscard]] virtual CapabilitySupport support(EditorCapability capability) const noexcept;

    [[nodiscard]] EditorContextMenuMode context_menu_mode() const noexcept;
    [[nodiscard]] EditorDragDropMode drag_drop_mode() const noexcept;
    [[nodiscard]] EditorFilePickerMode file_picker_mode() const noexcept;
    [[nodiscard]] EditorClipboardMode clipboard_mode() const noexcept;
    [[nodiscard]] EditorPersistenceMode persistence_mode() const noexcept;
    [[nodiscard]] EditorNavigationMode navigation_mode() const noexcept;

protected:
    // 解決済みの値はprotectedで持ち、専用の提供
    // 元がOS固有の実装に依存せず利用できるようにする。
    [[nodiscard]] Platform resolved_platform() const noexcept { return platform_; }

private:
    Platform platform_;
};

// ホスト統合・テスト用の小さく確保しやすい提供元。
// finalにはせず、 一つの仮想問い合わせだけを
// 派生で変え、他の上書き値は維持できるようにする。
class OverridableEditorPlatformCapabilities : public EditorPlatformCapabilities {
public:
    explicit OverridableEditorPlatformCapabilities(
        Platform platform = Platform::Automatic,
        EditorCapabilityOverrides overrides = {});

    [[nodiscard]] const EditorCapabilityOverrides& overrides() const noexcept {
        return overrides_;
    }

    [[nodiscard]] bool supports_external_process() const noexcept override;
    [[nodiscard]] bool supports_multiple_windows() const noexcept override;
    [[nodiscard]] bool supports_context_menu() const noexcept override;
    [[nodiscard]] bool supports_drag_drop() const noexcept override;
    [[nodiscard]] bool supports_file_picker() const noexcept override;
    [[nodiscard]] bool supports_native_file_picker() const noexcept override;
    [[nodiscard]] bool supports_browser_file_picker() const noexcept override;
    [[nodiscard]] bool supports_clipboard_text() const noexcept override;
    [[nodiscard]] bool supports_clipboard_custom_format() const noexcept override;
    [[nodiscard]] bool supports_clipboard_internal_fallback() const noexcept override;
    [[nodiscard]] bool supports_web_persistence() const noexcept override;
    [[nodiscard]] bool supports_touch_navigation() const noexcept override;
    [[nodiscard]] bool supports_long_press_context_menu() const noexcept override;
    [[nodiscard]] bool supports_long_press_drag_drop() const noexcept override;
    [[nodiscard]] bool supports_single_screen_navigation() const noexcept override;

private:
    EditorCapabilityOverrides overrides_;
};

[[nodiscard]] EditorCapabilityProfile default_editor_capability_profile(
    Platform platform = Platform::Automatic) noexcept;

// プラットフォームの特性を、EditorRuntimeContext
// 用の軽量な設定へコピーする。 単独起動
// ではこの入口を使い、仮想関数の上書きが
// 必要なら下の提供元ファクトリーを使う。
[[nodiscard]] Capabilities default_capabilities(
    Platform platform = Platform::Automatic) noexcept;

// ホストへ実際に接続されたサービスから、公開する能力を
// 決める。 対象OSの既定値だけでは製品起動の根拠にな
// らない。デスクトップ向けにコンパイルしたという理由で
// 、 ヘッドレスの代替がネイティブの画面・クリップボー
// ド・ドラッグ・プロセス対応を宣言しないようにする。
[[nodiscard]] EditorCapabilityProfile probe_editor_capability_profile(
    const Services& services,
    Platform platform = Platform::Automatic) noexcept;

[[nodiscard]] Capabilities probe_capabilities(
    const Services& services,
    Platform platform = Platform::Automatic) noexcept;

[[nodiscard]] std::shared_ptr<EditorPlatformCapabilities>
make_editor_platform_capabilities(
    Platform platform = Platform::Automatic,
    EditorCapabilityOverrides overrides = {});

// 構成元でプラットフォーム提供元の実装
// 名まで示す必要がない場合の短い別名。
[[nodiscard]] std::shared_ptr<EditorPlatformCapabilities>
make_editor_capabilities(
    Platform platform = Platform::Automatic,
    EditorCapabilityOverrides overrides = {});

using EditorCapabilities = EditorPlatformCapabilities;
using EditorCapabilityProvider = EditorPlatformCapabilities;

}  // namespace cwxeditor::lib
