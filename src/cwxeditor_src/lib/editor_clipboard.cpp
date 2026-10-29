#include "editor_clipboard.hpp"
#include "editor_diagnostics.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <exception>
#include <string>
#include <utility>

namespace cwxeditor::lib {

struct EditorClipboard::SharedState {
    mutable std::mutex mutex;
    // ClipboardBackend はインターフェースであり、古いプラットフォーム
    // 実装がすべてロックを持つとは限らない。 EditorClipboard をワー
    // カー・UIの両スレッドで使っても、一つのバックエンドへの呼び出しは直列化する。
    mutable std::mutex backend_mutex;
    std::shared_ptr<ClipboardBackend> backend_owner;
    ClipboardBackend* backend{};

    ClipboardData fallback_data;
    bool has_fallback{};
    ClipboardData expected_data;
    bool has_expected{};
    ClipboardData bridge_data;
    bool has_bridge_data{};

    WebClipboardCallbacks bridge;
    ClipboardPermission permission{ClipboardPermission::Unknown};
    std::optional<bool> custom_formats_override;
    bool custom_formats_supported{};
    ClipboardBridgeState bridge_state{ClipboardBridgeState::Unavailable};
    std::uint64_t next_operation{};
    std::uint64_t active_operation{};
    std::string error;
};

namespace {

constexpr std::string_view kTextMime = "text/plain";
constexpr std::string_view kXmlMime = "text/xml";
constexpr std::string_view kFileMime = "application/x-file-list";

[[nodiscard]] bool ascii_equal_fold(std::string_view left, std::string_view right) noexcept {
    if (left.size() != right.size()) return false;
    for (std::size_t i = 0; i < left.size(); ++i) {
        const auto lower = [](unsigned char value) noexcept {
            return value >= static_cast<unsigned char>('A') &&
                           value <= static_cast<unsigned char>('Z')
                       ? static_cast<unsigned char>(value + ('a' - 'A'))
                       : value;
        };
        if (lower(static_cast<unsigned char>(left[i])) !=
            lower(static_cast<unsigned char>(right[i]))) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool is_xml_mime(std::string_view mime) noexcept {
    return ascii_equal_fold(mime, kEditorClipboardXmlMimeType) ||
           ascii_equal_fold(mime, "application/x-cardwirth-editor+xml") ||
           ascii_equal_fold(mime, "application/x-cardwirth-xml") ||
           ascii_equal_fold(mime, "application/xml") || ascii_equal_fold(mime, kXmlMime);
}

[[nodiscard]] bool is_text_mime(std::string_view mime) noexcept {
    return ascii_equal_fold(mime, kTextMime) ||
           ascii_equal_fold(mime, "text/plain;charset=utf-8");
}

[[nodiscard]] bool is_file_mime(std::string_view mime) noexcept {
    return ascii_equal_fold(mime, kFileMime) || ascii_equal_fold(mime, "text/uri-list");
}

[[nodiscard]] bool empty_data(const ClipboardData& data) noexcept {
    return data.text.empty() && data.files.empty() && data.formats.empty();
}

[[nodiscard]] bool same_context(const ClipboardData& left, const ClipboardData& right) noexcept {
    // テキスト専用・ブラウザーのバックエンドでは提供されない
    // 空のフィールドを、任意一致として扱う。 空でない値は
    // 完全一致を要求し、別アプリで無関係な文字列をコピーし
    // た後に、 古い内部XMLデータを混ぜないようにする。
    const bool text_matches = left.text.empty() || right.text.empty() || left.text == right.text;
    const bool files_matches = left.files.empty() || right.files.empty() || left.files == right.files;
    return text_matches && files_matches;
}

void merge_missing_formats(ClipboardData& target, const ClipboardData& fallback) {
    for (const auto& [mime, bytes] : fallback.formats) {
        if (!target.formats.contains(mime)) target.formats.emplace(mime, bytes);
    }
}

[[nodiscard]] std::string bytes_to_string(const std::vector<std::byte>& bytes) {
    std::string result;
    result.reserve(bytes.size());
    for (const auto value : bytes) {
        result.push_back(static_cast<char>(std::to_integer<unsigned char>(value)));
    }
    return result;
}

[[nodiscard]] std::vector<std::byte> string_to_bytes(std::string_view value) {
    std::vector<std::byte> result;
    result.reserve(value.size());
    for (const unsigned char character : value) {
        result.push_back(static_cast<std::byte>(character));
    }
    return result;
}

[[nodiscard]] bool looks_like_xml(std::string_view value) noexcept {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return false;
    return value.substr(first).starts_with("<?xml") || value[first] == '<';
}

[[nodiscard]] const std::vector<std::byte>* find_xml_format(const ClipboardData& data) {
    for (const auto& [mime, bytes] : data.formats) {
        if (is_xml_mime(mime)) return &bytes;
    }
    return nullptr;
}

[[nodiscard]] ClipboardBridgeStatus status_locked(
    const EditorClipboard::SharedState& state) {
    ClipboardBridgeStatus result;
    result.permission = state.permission;
    result.state = state.bridge_state;
    result.operation = state.active_operation;
    result.custom_formats_supported = state.custom_formats_override.value_or(
        state.custom_formats_supported);
    result.error = state.error;
    return result;
}

[[nodiscard]] ClipboardBridgeResult make_result_locked(
    const EditorClipboard::SharedState& state, bool success, ClipboardData data = {},
    std::string error = {}) {
    ClipboardBridgeResult result;
    result.success = success;
    result.data = std::move(data);
    result.error = std::move(error);
    result.status = status_locked(state);
    if (!result.error.empty()) result.status.error = result.error;
    return result;
}

// EditorClipboard オブジェクトへ戻らずに操作を完了す
// る。 WebのPromiseはラッパーより長く存続する場合があるた
// め、 コールバックは生の this ではなく共有状態を保持する。
void finish_state(const std::shared_ptr<EditorClipboard::SharedState>& state,
                 std::uint64_t operation, ClipboardBridgeResult result,
                 ClipboardCompletion completion, bool is_read) {
    if (!state) return;

    {
        std::scoped_lock lock(state->mutex);
        if (operation == 0U || state->active_operation != operation) return;

        const ClipboardData previous_expected = state->expected_data;
        const bool had_expected = state->has_expected;
        const bool successful = result.success;
        if (is_read && successful) {
            // ラッパーが最後に書いたブラウザー・ネイティブの値とまだ同じ
            // 場合だけ、独自形式の代替データを残す。 それ以外のテキスト
            // 専用の読み取りは、新しいクリップボードの世代として扱う。
            if (!had_expected || !same_context(result.data, previous_expected)) {
                state->fallback_data = {};
                state->has_fallback = false;
            }
            state->bridge_data = result.data;
            state->has_bridge_data = true;
            state->expected_data = result.data;
            state->has_expected = true;
        }

        state->active_operation = 0U;
        state->bridge_state = successful ? ClipboardBridgeState::Idle
                                         : ClipboardBridgeState::Failed;
        if (!result.error.empty()) {
            state->error = result.error;
        } else if (!successful && state->error.empty()) {
            state->error = is_read ? "clipboard read failed" : "clipboard write failed";
        } else if (successful) {
            state->error.clear();
        }
        result.status = status_locked(*state);
        if (!result.error.empty()) result.status.error = result.error;
    }

    if (completion) {
        try {
            completion(std::move(result));
        } catch (...) {
            // 多くのホストでは、完了処理がブラウザー・イベントループのスレッドで
            // 動く。 利用側のコールバックの例外で、そのスレッドを終了させない。
        }
    }
}

}  // namespace

EditorClipboard::EditorClipboard(std::shared_ptr<ClipboardBackend> backend)
    : state_(std::make_shared<SharedState>()) {
    state_->backend_owner = std::move(backend);
    state_->backend = state_->backend_owner.get();
}

EditorClipboard::EditorClipboard(ClipboardBackend& backend) noexcept
    : state_(std::make_shared<SharedState>()) {
    state_->backend = &backend;
}

EditorClipboard::~EditorClipboard() = default;

void EditorClipboard::clear() {
    const auto state = state_;
    ClipboardBackend* backend = nullptr;
    std::shared_ptr<ClipboardBackend> backend_keepalive;
    {
        std::scoped_lock lock(state->mutex);
        backend_keepalive = state->backend_owner;
        backend = state->backend;
        if (backend_keepalive) backend = backend_keepalive.get();
        state->fallback_data = {};
        state->has_fallback = false;
        state->expected_data = {};
        state->has_expected = false;
        state->bridge_data = {};
        state->has_bridge_data = false;
        // ブラウザーのPromiseが保持する完了処理を無効化する。
        state->active_operation = 0U;
        state->next_operation++;
        state->bridge_state = state->bridge ? ClipboardBridgeState::Idle
                                            : ClipboardBridgeState::Unavailable;
        state->error.clear();
    }
    if (backend) {
        try {
            std::scoped_lock backend_lock(state->backend_mutex);
            backend->clear();
        } catch (...) {
            std::scoped_lock lock(state->mutex);
            state->error = "clipboard clear failed";
            state->bridge_state = state->bridge ? ClipboardBridgeState::Failed
                                                : ClipboardBridgeState::Unavailable;
        }
    }
}

void EditorClipboard::write(const ClipboardData& data) {
    const auto state = state_;
    ClipboardBackend* backend = nullptr;
    std::shared_ptr<ClipboardBackend> backend_keepalive;
    WebClipboardCallbacks bridge;
    std::optional<bool> custom_override;
    {
        std::scoped_lock lock(state->mutex);
        backend_keepalive = state->backend_owner;
        backend = state->backend;
        if (backend_keepalive) backend = backend_keepalive.get();
        bridge = state->bridge;
        custom_override = state->custom_formats_override;
        state->expected_data = data;
        state->has_expected = true;
        state->bridge_data = {};
        state->has_bridge_data = false;
        // 同期の書き込みは、保持中の非同期の読み書きより優先する。
        state->active_operation = 0U;
        state->next_operation++;
        state->error.clear();
        state->bridge_state = bridge ? ClipboardBridgeState::Idle
                                     : ClipboardBridgeState::Unavailable;
    }

    bool backend_ok = false;
    bool base_ok = true;
    ClipboardData observed;
    if (backend) {
        try {
            std::scoped_lock backend_lock(state->backend_mutex);
            backend->write(data);
            backend_ok = true;
            observed = backend->read();
            base_ok = (data.text.empty() || observed.text == data.text) &&
                      (data.files.empty() || observed.files == data.files);
        } catch (...) {
            backend_ok = false;
            base_ok = false;
        }
    }

    ClipboardData fallback;
    bool has_fallback = !backend_ok || !base_ok;
    if (has_fallback) fallback = data;

    for (const auto& [mime, bytes] : data.formats) {
        bool supported = false;
        if (custom_override.has_value()) {
            supported = *custom_override;
        } else if (bridge.supports_custom_format) {
            try {
                supported = bridge.supports_custom_format(mime);
            } catch (...) {
                supported = false;
            }
        } else if (backend_ok && backend) {
            try {
                std::scoped_lock backend_lock(state->backend_mutex);
                supported = backend->contains(mime) || observed.formats.contains(mime);
            } catch (...) {
                supported = observed.formats.contains(mime);
            }
        }

        if (!supported) {
            if (!has_fallback) {
                fallback.text = data.text;
                fallback.files = data.files;
                has_fallback = true;
            }
            fallback.formats[mime] = bytes;
        }
    }

    {
        std::scoped_lock lock(state->mutex);
        state->fallback_data = std::move(fallback);
        state->has_fallback = has_fallback;
        state->custom_formats_supported = !data.formats.empty() && !has_fallback;
        if (backend_ok) {
            state->error.clear();
        } else if (!backend) {
            state->error.clear();  // プロセス内の代替処理も、有効なバックエンドとして扱う。
        } else {
            state->error = "clipboard backend write failed; using internal fallback";
        }
    }
}

ClipboardData EditorClipboard::read() const {
    const EditorPerformanceScope performance("clipboard.read.all");
    const auto state = state_;
    ClipboardBackend* backend = nullptr;
    std::shared_ptr<ClipboardBackend> backend_keepalive;
    ClipboardData fallback;
    ClipboardData expected;
    ClipboardData bridge_data;
    bool has_fallback = false;
    bool has_expected = false;
    bool has_bridge_data = false;
    {
        std::scoped_lock lock(state->mutex);
        backend_keepalive = state->backend_owner;
        backend = state->backend;
        if (backend_keepalive) backend = backend_keepalive.get();
        fallback = state->fallback_data;
        expected = state->expected_data;
        bridge_data = state->bridge_data;
        has_fallback = state->has_fallback;
        has_expected = state->has_expected;
        has_bridge_data = state->has_bridge_data;
    }

    if (has_bridge_data) {
        if (has_fallback && has_expected && same_context(bridge_data, expected)) {
            merge_missing_formats(bridge_data, fallback);
        }
        return bridge_data;
    }

    ClipboardData result;
    bool backend_ok = false;
    if (backend) {
        try {
            std::scoped_lock backend_lock(state->backend_mutex);
            result = backend->read();
            backend_ok = true;
        } catch (...) {
            backend_ok = false;
        }
    }
    if (!backend_ok || (!backend && has_fallback) ||
        (empty_data(result) && has_fallback)) {
        return fallback;
    }
    if (has_fallback && has_expected && same_context(result, expected)) {
        merge_missing_formats(result, fallback);
    }
    return result;
}

ClipboardData EditorClipboard::read_formats(std::span<const std::string_view> requested) const {
    const EditorPerformanceScope performance("clipboard.read.requested");
    const auto filter = [&](const ClipboardData& source) {
        ClipboardData result;
        for (const auto mime : requested) {
            if (is_text_mime(mime)) result.text = source.text;
            if (is_file_mime(mime) || mime == "application/x-cwxeditor-file-list")
                result.files = source.files;
            for (const auto& [format, bytes] : source.formats)
                if (ascii_equal_fold(format, mime)) result.formats.emplace(format, bytes);
        }
        return result;
    };
    const auto state = state_;
    ClipboardBackend* backend = nullptr;
    std::shared_ptr<ClipboardBackend> keepalive;
    bool fallback = false;
    {
        std::scoped_lock lock(state->mutex);
        fallback = state->has_fallback;
        if (!fallback && state->has_bridge_data) return filter(state->bridge_data);
        keepalive = state->backend_owner;
        backend = keepalive ? keepalive.get() : state->backend;
    }
    // 内部の独自形式の代替データも、ネイティブの文字列・ファイルがコピー時の内
    // 容と一致するか検証する。 古いXMLを復活させず、この検査を維持する。
    if (fallback) return filter(read());
    if (!backend) return {};
    try {
        std::scoped_lock backend_lock(state->backend_mutex);
        return filter(backend->read_formats(requested));
    } catch (...) { return {}; }
}

bool EditorClipboard::contains(std::string_view mime_type) const {
    // 明示的に提供されるテキスト形式は、空の値も持てる。 旧式のテ
    // キスト専用データを確認した後、共通の形式の有無の判定を使う。
    if (is_text_mime(mime_type) && !read_text().empty()) return true;
    if (is_xml_mime(mime_type)) return xml().has_value();
    if (ascii_equal_fold(mime_type, "application/x-cwxeditor-file-list")) {
        // D版の FileTransfer と同様、提供される型から可否を
        // 判定する。 ファイルデータの復号は形式があっても遅延・拒否され
        // る場合があるため、先にネイティブのバックエンドへ問い合わせる。
        const auto state = state_;
        ClipboardBackend* backend = nullptr;
        std::shared_ptr<ClipboardBackend> backend_keepalive;
        {
            std::scoped_lock lock(state->mutex);
            backend_keepalive = state->backend_owner;
            backend = backend_keepalive ? backend_keepalive.get() : state->backend;
        }
        if (backend) {
            try {
                std::scoped_lock backend_lock(state->backend_mutex);
                if (backend->contains(mime_type)) return true;
            } catch (...) {
                // 下の代替経路でも、内部保持の形式を残す前に
                // 、現在のクリップボードの内容を確認する。
            }
        }

        // ネイティブ転送が使えない場合は、現在の選別済みデータを調
        // べる。 文字列・ファイルが書き込み時のクリップボード状
        // 態と一致する場合だけ、メモリ内の代替データを保持する。
        constexpr std::array<std::string_view, 3> requested{
            "application/x-cwxeditor-file-list", "text/plain;charset=utf-8", "text/plain"};
        const auto data = read_formats(requested);
        if (!data.files.empty()) return true;
        for (const auto& [format, unused] : data.formats) {
            (void)unused;
            if (ascii_equal_fold(format, mime_type)) return true;
        }
        return false;
    }
    if (is_file_mime(mime_type)) return !files().empty();

    const auto state = state_;
    ClipboardBackend* backend = nullptr;
    std::shared_ptr<ClipboardBackend> backend_keepalive;
    bool local = false;
    {
        std::scoped_lock lock(state->mutex);
        backend_keepalive = state->backend_owner;
        backend = state->backend;
        if (backend_keepalive) backend = backend_keepalive.get();
        if (state->has_fallback) {
            for (const auto& [mime, unused] : state->fallback_data.formats) {
                if (ascii_equal_fold(mime, mime_type)) {
                    local = true;
                    break;
                }
            }
        }
        if (!local && state->has_bridge_data) {
            for (const auto& [mime, unused] : state->bridge_data.formats) {
                if (ascii_equal_fold(mime, mime_type)) {
                    local = true;
                    break;
                }
            }
        }
    }
    if (local) return true;
    if (!backend) return false;
    try {
        std::scoped_lock backend_lock(state->backend_mutex);
        return backend->contains(mime_type);
    } catch (...) {
        return false;
    }
}

void EditorClipboard::set_backend(std::shared_ptr<ClipboardBackend> backend) {
    const auto state = state_;
    std::scoped_lock backend_lock(state->backend_mutex);
    std::scoped_lock lock(state->mutex);
    state->backend_owner = std::move(backend);
    state->backend = state->backend_owner.get();
}

void EditorClipboard::set_backend(ClipboardBackend& backend) noexcept {
    const auto state = state_;
    std::scoped_lock backend_lock(state->backend_mutex);
    std::scoped_lock lock(state->mutex);
    state->backend_owner.reset();
    state->backend = &backend;
}

std::shared_ptr<ClipboardBackend> EditorClipboard::backend() const {
    const auto state = state_;
    std::scoped_lock lock(state->mutex);
    return state->backend_owner;
}

void EditorClipboard::set_web_callbacks(WebClipboardCallbacks callbacks) {
    const auto state = state_;
    if (!callbacks.write) callbacks.write = std::move(callbacks.write_async);
    if (!callbacks.read) callbacks.read = std::move(callbacks.read_async);
    if (!callbacks.permission) callbacks.permission = std::move(callbacks.query_permission);
    if (!callbacks.supports_custom_format) {
        callbacks.supports_custom_format = std::move(callbacks.supports_custom_formats);
    }
    {
        std::scoped_lock lock(state->mutex);
        state->bridge = std::move(callbacks);
        state->bridge_state = state->bridge ? ClipboardBridgeState::Idle
                                            : ClipboardBridgeState::Unavailable;
        state->active_operation = 0U;
        state->next_operation++;
        state->error.clear();
    }
    (void)refresh_permission();
}

void EditorClipboard::clear_web_callbacks() {
    const auto state = state_;
    std::scoped_lock lock(state->mutex);
    state->bridge = {};
    state->active_operation = 0U;
    state->next_operation++;
    state->bridge_state = ClipboardBridgeState::Unavailable;
    state->error.clear();
}

WebClipboardCallbacks EditorClipboard::web_callbacks() const {
    const auto state = state_;
    std::scoped_lock lock(state->mutex);
    return state->bridge;
}

void EditorClipboard::set_web_write_callback(WebClipboardWriteCallback callback) {
    auto callbacks = web_callbacks();
    callbacks.write = std::move(callback);
    set_web_callbacks(std::move(callbacks));
}

void EditorClipboard::set_web_read_callback(WebClipboardReadCallback callback) {
    auto callbacks = web_callbacks();
    callbacks.read = std::move(callback);
    set_web_callbacks(std::move(callbacks));
}

void EditorClipboard::set_web_permission_callback(WebClipboardPermissionCallback callback) {
    auto callbacks = web_callbacks();
    callbacks.permission = std::move(callback);
    set_web_callbacks(std::move(callbacks));
}

void EditorClipboard::set_permission(ClipboardPermission permission) {
    const auto state = state_;
    std::scoped_lock lock(state->mutex);
    state->permission = permission;
    if (permission == ClipboardPermission::Denied && state->active_operation != 0U) {
        state->active_operation = 0U;
        state->bridge_state = ClipboardBridgeState::Failed;
        state->error = "clipboard permission denied";
    }
}

ClipboardPermission EditorClipboard::permission() const noexcept {
    const auto state = state_;
    std::scoped_lock lock(state->mutex);
    return state->permission;
}

ClipboardPermission EditorClipboard::refresh_permission() const {
    const auto state = state_;
    WebClipboardCallbacks::Permission callback;
    ClipboardPermission current;
    {
        std::scoped_lock lock(state->mutex);
        callback = state->bridge.permission;
        current = state->permission;
    }
    if (!callback) return current;

    ClipboardPermission updated = ClipboardPermission::Unknown;
    try {
        updated = callback();
    } catch (...) {
        std::scoped_lock lock(state->mutex);
        state->permission = ClipboardPermission::Unknown;
        state->error = "clipboard permission query failed";
        return state->permission;
    }
    {
        std::scoped_lock lock(state->mutex);
        state->permission = updated;
    }
    return updated;
}

void EditorClipboard::set_custom_formats_supported(bool supported) noexcept {
    const auto state = state_;
    std::scoped_lock lock(state->mutex);
    state->custom_formats_override = supported;
    state->custom_formats_supported = supported;
}

bool EditorClipboard::custom_formats_supported() const noexcept {
    const auto state = state_;
    std::scoped_lock lock(state->mutex);
    return state->custom_formats_override.value_or(state->custom_formats_supported);
}

ClipboardBridgeStatus EditorClipboard::bridge_status() const {
    const auto state = state_;
    std::scoped_lock lock(state->mutex);
    return status_locked(*state);
}

ClipboardBridgeState EditorClipboard::bridge_state() const noexcept {
    const auto state = state_;
    std::scoped_lock lock(state->mutex);
    return state->bridge_state;
}

std::uint64_t EditorClipboard::pending_operation() const noexcept {
    const auto state = state_;
    std::scoped_lock lock(state->mutex);
    return state->active_operation;
}

void EditorClipboard::write_async(const ClipboardData& data, ClipboardCompletion completion) {
    write(data);
    const auto state = state_;
    WebClipboardCallbacks::Write callback;
    ClipboardPermission current_permission;
    {
        std::scoped_lock lock(state->mutex);
        callback = state->bridge.write;
        current_permission = state->permission;
    }

    if (!callback) {
        if (completion) {
            ClipboardBridgeResult result;
            result.success = true;
            result.status = bridge_status();
            try {
                completion(std::move(result));
            } catch (...) {
            }
        }
        return;
    }
    if (current_permission == ClipboardPermission::Unknown) {
        current_permission = refresh_permission();
    }
    if (current_permission == ClipboardPermission::Denied) {
        ClipboardBridgeResult result;
        {
            std::scoped_lock lock(state->mutex);
            state->bridge_state = ClipboardBridgeState::Failed;
            state->error = "clipboard permission denied";
            result = make_result_locked(*state, false, {}, state->error);
        }
        if (completion) {
            try {
                completion(std::move(result));
            } catch (...) {
            }
        }
        return;
    }

    std::uint64_t operation = 0U;
    {
        std::scoped_lock lock(state->mutex);
        operation = ++state->next_operation;
        state->active_operation = operation;
        state->bridge_state = ClipboardBridgeState::PendingWrite;
        state->error.clear();
    }

    ClipboardCompletion bridge_completion =
        [state, operation, completion = std::move(completion)](ClipboardBridgeResult result) mutable {
            finish_state(state, operation, std::move(result), std::move(completion), false);
        };
    try {
        callback(data, std::move(bridge_completion));
    } catch (...) {
        ClipboardBridgeResult result;
        result.success = false;
        result.error = "clipboard bridge write threw an exception";
        finish_state(state, operation, std::move(result), {}, false);
    }
}

void EditorClipboard::read_async(ClipboardCompletion completion) {
    const auto state = state_;
    WebClipboardCallbacks::Read callback;
    ClipboardPermission current_permission;
    {
        std::scoped_lock lock(state->mutex);
        callback = state->bridge.read;
        current_permission = state->permission;
    }

    if (!callback) {
        ClipboardBridgeResult result;
        result.success = true;
        result.data = read();
        result.status = bridge_status();
        if (completion) {
            try {
                completion(std::move(result));
            } catch (...) {
            }
        }
        return;
    }
    if (current_permission == ClipboardPermission::Unknown) {
        current_permission = refresh_permission();
    }
    if (current_permission == ClipboardPermission::Denied) {
        ClipboardBridgeResult result;
        {
            std::scoped_lock lock(state->mutex);
            state->bridge_state = ClipboardBridgeState::Failed;
            state->error = "clipboard permission denied";
            result = make_result_locked(*state, false, {}, state->error);
        }
        if (completion) {
            try {
                completion(std::move(result));
            } catch (...) {
            }
        }
        return;
    }

    std::uint64_t operation = 0U;
    {
        std::scoped_lock lock(state->mutex);
        operation = ++state->next_operation;
        state->active_operation = operation;
        state->bridge_state = ClipboardBridgeState::PendingRead;
        state->error.clear();
    }
    ClipboardCompletion bridge_completion =
        [state, operation, completion = std::move(completion)](ClipboardBridgeResult result) mutable {
            finish_state(state, operation, std::move(result), std::move(completion), true);
        };
    try {
        callback(std::move(bridge_completion));
    } catch (...) {
        ClipboardBridgeResult result;
        result.success = false;
        result.error = "clipboard bridge read threw an exception";
        finish_state(state, operation, std::move(result), {}, true);
    }
}

void EditorClipboard::write_text(std::string text_value) {
    ClipboardData data;
    data.text = std::move(text_value);
    write(data);
}

void EditorClipboard::write_xml(std::string xml_value, std::string mime_type) {
    ClipboardData data;
    data.text = xml_value;
    if (mime_type.empty()) mime_type = std::string(kEditorClipboardXmlMimeType);
    data.formats.emplace(std::move(mime_type), string_to_bytes(xml_value));
    write(data);
}

void EditorClipboard::write_files(std::vector<std::filesystem::path> files_value) {
    ClipboardData data;
    data.files = std::move(files_value);
    write(data);
}

std::string EditorClipboard::text() const { return read_text(); }

std::optional<std::string> EditorClipboard::xml() const {
    const std::array<std::string_view,7> requested{kEditorClipboardXmlMimeType,
        "application/x-cardwirth-editor+xml", "application/x-cardwirth-xml",
        "application/xml", "text/xml", "text/plain;charset=utf-8", "text/plain"};
    const ClipboardData data = read_formats(requested);
    if (const auto* bytes = find_xml_format(data)) return bytes_to_string(*bytes);
    if (looks_like_xml(data.text)) return data.text;
    return std::nullopt;
}

std::vector<std::filesystem::path> EditorClipboard::files() const {
    constexpr std::array<std::string_view,3> requested{
        "application/x-file-list", "text/uri-list", "application/x-cwxeditor-file-list"};
    return read_formats(requested).files;
}

bool EditorClipboard::has_internal_fallback() const noexcept {
    const auto state = state_;
    std::scoped_lock lock(state->mutex);
    return state->has_fallback;
}

ClipboardData EditorClipboard::fallback() const {
    const auto state = state_;
    std::scoped_lock lock(state->mutex);
    return state->fallback_data;
}

void EditorClipboard::clear_internal_fallback() noexcept {
    const auto state = state_;
    std::scoped_lock lock(state->mutex);
    state->fallback_data = {};
    state->has_fallback = false;
}

ClipboardData EditorClipboard::read_locked_or_backend() const { return read(); }

void EditorClipboard::finish_async(std::uint64_t operation, ClipboardBridgeResult result,
                                   ClipboardCompletion completion, bool is_read) {
    finish_state(state_, operation, std::move(result), std::move(completion), is_read);
}

std::shared_ptr<ClipboardBackend> make_editor_clipboard(std::shared_ptr<ClipboardBackend> backend) {
    return std::make_shared<EditorClipboard>(std::move(backend));
}

std::shared_ptr<EditorClipboard> make_editor_clipboard_backend(
    std::shared_ptr<ClipboardBackend> backend) {
    return std::make_shared<EditorClipboard>(std::move(backend));
}

}  // namespace cwxeditor::lib
