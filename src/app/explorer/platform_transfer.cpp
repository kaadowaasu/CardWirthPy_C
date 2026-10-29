#include "platform_transfer.hpp"
#if defined(__APPLE__)
#include "platform_transfer_apple.hpp"
#endif

#include <SDL3/SDL.h>

#include <algorithm>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#if defined(CARDWIRTH_WEB_PTHREADS) && CARDWIRTH_WEB_PTHREADS
#include <emscripten/threading_legacy.h>
#include <pthread.h>
#endif
#endif

namespace cardwirth::explorer {
namespace {

[[maybe_unused]] bool confirm_overwrite_with_common_sdl(
    const std::vector<std::string>& conflicts) {
    std::string body = "保存先に同名の項目があります。上書きしますか？\n\n";
    const auto visible = (std::min)(conflicts.size(), std::size_t{8});
    for (std::size_t index = 0; index < visible; ++index)
        body += "・" + conflicts[index] + "\n";
    if (conflicts.size() > visible)
        body += "ほか " + std::to_string(conflicts.size() - visible) + " 項目";
    const SDL_MessageBoxButtonData buttons[] = {
        {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "上書き"},
        {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "キャンセル"},
    };
    SDL_MessageBoxData data{};
    data.flags = SDL_MESSAGEBOX_WARNING;
    data.window = SDL_GetKeyboardFocus();
    data.title = "上書き確認";
    data.message = body.c_str();
    data.numbuttons = static_cast<int>(std::size(buttons));
    data.buttons = buttons;
    int selected = 0;
    return SDL_ShowMessageBox(&data, &selected) && selected == 1;
}

}  // namespace
}  // namespace cardwirth::explorer

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shobjidl.h>

namespace cardwirth::explorer {
namespace {

class ComScope {
public:
    ComScope() : result_(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)) {}
    ~ComScope() { if (SUCCEEDED(result_)) CoUninitialize(); }
    [[nodiscard]] bool available() const noexcept {
        return SUCCEEDED(result_) || result_ == RPC_E_CHANGED_MODE;
    }
private:
    HRESULT result_;
};

HWND parent_handle(SDL_Window* window) {
    if (!window) return nullptr;
    return static_cast<HWND>(SDL_GetPointerProperty(
        SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
}

TransferResult select_paths(SDL_Window* parent, bool folders,
                            bool multiple, const wchar_t* title,
                            const std::vector<std::string>& extensions = {}) {
    TransferResult result;
    // COMオブジェクトを取得する前に、メモリ確保が必要な文字列を準備する。
    std::wstring pattern;
    if (!folders) {
        for (const auto& extension : extensions) {
            if (!pattern.empty()) pattern += L";";
            pattern += L"*." + std::filesystem::u8path(extension).native();
        }
    }
    ComScope com;
    if (!com.available()) {
        result.status = TransferStatus::Failed;
        result.error = "Windowsファイル選択を初期化できません";
        return result;
    }
    IFileOpenDialog* dialog = nullptr;
    const auto created = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                           IID_PPV_ARGS(&dialog));
    if (FAILED(created) || !dialog) {
        result.status = TransferStatus::Failed;
        result.error = "Windowsファイル選択を開けません";
        return result;
    }
    DWORD options{};
    (void)dialog->GetOptions(&options);
    options |= FOS_FORCEFILESYSTEM | FOS_NOCHANGEDIR | FOS_PATHMUSTEXIST;
    if (folders) options |= FOS_PICKFOLDERS;
    else options |= FOS_FILEMUSTEXIST;
    if (multiple) options |= FOS_ALLOWMULTISELECT;
    (void)dialog->SetOptions(options);
    (void)dialog->SetTitle(title);
    if (!pattern.empty()) {
        const COMDLG_FILTERSPEC filter{L"ファイル", pattern.c_str()};
        if (FAILED(dialog->SetFileTypes(1, &filter))) {
            dialog->Release();
            return {TransferStatus::Failed, {}, "ファイルの種類を設定できません"};
        }
    }

    const auto shown = dialog->Show(parent_handle(parent));
    if (shown == HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
        result.status = TransferStatus::Cancelled;
        dialog->Release();
        return result;
    }
    if (FAILED(shown)) {
        result.status = TransferStatus::Failed;
        result.error = "Windowsファイル選択でエラーが発生しました";
        dialog->Release();
        return result;
    }

    bool selection_failed = false;
    const auto append_item = [&result, &selection_failed](IShellItem* item) {
        if (!item) { selection_failed = true; return; }
        PWSTR path = nullptr;
        if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
            ExternalDocument document;
            document.native_path = std::filesystem::path(path);
            const auto encoded_name = document.native_path.filename().u8string();
            document.display_name = std::string(
                reinterpret_cast<const char*>(encoded_name.data()), encoded_name.size());
            result.documents.push_back(std::move(document));
        } else selection_failed = true;
        if (path) CoTaskMemFree(path);
    };
    if (multiple) {
        IShellItemArray* items = nullptr;
        if (FAILED(dialog->GetResults(&items)) || !items) {
            result.status = TransferStatus::Failed;
            result.error = "選択したパスを取得できません";
            dialog->Release();
            return result;
        }
        DWORD count{};
        if (FAILED(items->GetCount(&count))) selection_failed = true;
        for (DWORD index = 0; index < count; ++index) {
            IShellItem* item = nullptr;
            if (SUCCEEDED(items->GetItemAt(index, &item)) && item) {
                append_item(item);
                item->Release();
            } else selection_failed = true;
        }
        items->Release();
    } else {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item)) && item) {
            append_item(item);
            item->Release();
        }
    }
    dialog->Release();
    if (selection_failed || result.documents.empty()) {
        result.documents.clear(); // 選択結果が不完全なまま、通知せずに取り込まない。
        result.status = TransferStatus::Failed;
        result.error = "選択したパスを取得できません";
    } else {
        result.status = TransferStatus::Success;
    }
    return result;
}

TransferResult select_save_path(SDL_Window* parent,
                                const std::filesystem::path& suggested_name,
                                bool directory) {
    TransferResult result;
    ComScope com;
    if (!com.available()) {
        result.status = TransferStatus::Failed;
        result.error = "Windows保存ダイアログを初期化できません";
        return result;
    }
    IFileSaveDialog* dialog = nullptr;
    const auto created = CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER,
                                           IID_PPV_ARGS(&dialog));
    if (FAILED(created) || !dialog) {
        result.status = TransferStatus::Failed;
        result.error = "Windows保存ダイアログを開けません";
        return result;
    }
    DWORD options{};
    (void)dialog->GetOptions(&options);
    options |= FOS_FORCEFILESYSTEM | FOS_NOCHANGEDIR | FOS_PATHMUSTEXIST |
               FOS_OVERWRITEPROMPT;
    (void)dialog->SetOptions(options);
    (void)dialog->SetTitle(directory ? L"フォルダをダウンロード"
                                     : L"ファイルをダウンロード");
    const auto suggested_filename = suggested_name.filename().wstring();
    if (!suggested_filename.empty())
        (void)dialog->SetFileName(suggested_filename.c_str());

    const auto shown = dialog->Show(parent_handle(parent));
    if (shown == HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
        result.status = TransferStatus::Cancelled;
        dialog->Release();
        return result;
    }
    if (FAILED(shown)) {
        result.status = TransferStatus::Failed;
        result.error = "保存先の選択でエラーが発生しました";
        dialog->Release();
        return result;
    }
    IShellItem* item = nullptr;
    if (SUCCEEDED(dialog->GetResult(&item)) && item) {
        PWSTR path = nullptr;
        if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
            ExternalDocument document;
            document.native_path = std::filesystem::path(path);
            const auto encoded_name = document.native_path.filename().u8string();
            document.display_name = std::string(
                reinterpret_cast<const char*>(encoded_name.data()), encoded_name.size());
            result.documents.push_back(std::move(document));
            CoTaskMemFree(path);
        }
        item->Release();
    }
    dialog->Release();
    if (result.documents.empty()) {
        result.status = TransferStatus::Failed;
        result.error = "保存先のパスを取得できません";
    } else {
        result.status = TransferStatus::Success;
    }
    return result;
}

}  // namespace

}  // namespace cardwirth::explorer

#elif defined(__ANDROID__)

#include <SDL3/SDL_system.h>
#include <jni.h>

namespace cardwirth::explorer {
namespace {

constexpr jint kPickUploadFiles = 0;
constexpr jint kPickUploadFolder = 1;
constexpr jint kPickSaveFile = 2;
constexpr jint kPickSaveNamedFolder = 3;
constexpr jint kPickDownloadFolder = 4;

std::string path_to_utf8(const std::filesystem::path& value) {
    const auto encoded = value.u8string();
    return {reinterpret_cast<const char*>(encoded.data()), encoded.size()};
}

std::u16string utf16_from_utf8(std::string_view value) {
    std::u16string result;
    result.reserve(value.size());
    for (std::size_t index = 0; index < value.size();) {
        const auto first = static_cast<unsigned char>(value[index++]);
        char32_t codepoint{};
        std::size_t continuation{};
        if (first < 0x80) codepoint = first;
        else if ((first & 0xE0) == 0xC0) { codepoint = first & 0x1F; continuation = 1; }
        else if ((first & 0xF0) == 0xE0) { codepoint = first & 0x0F; continuation = 2; }
        else if ((first & 0xF8) == 0xF0) { codepoint = first & 0x07; continuation = 3; }
        else { result.push_back(u'\uFFFD'); continue; }
        bool valid = index + continuation <= value.size();
        for (std::size_t count = 0; valid && count < continuation; ++count) {
            const auto next = static_cast<unsigned char>(value[index++]);
            if ((next & 0xC0) != 0x80) { valid = false; break; }
            codepoint = (codepoint << 6) | (next & 0x3F);
        }
        const char32_t minimum = continuation == 1 ? 0x80 : continuation == 2 ? 0x800 :
                                   continuation == 3 ? 0x10000 : 0;
        if (!valid || codepoint < minimum || codepoint > 0x10FFFF ||
            (codepoint >= 0xD800 && codepoint <= 0xDFFF)) {
            result.push_back(u'\uFFFD');
        } else if (codepoint <= 0xFFFF) {
            result.push_back(static_cast<char16_t>(codepoint));
        } else {
            codepoint -= 0x10000;
            result.push_back(static_cast<char16_t>(0xD800 + (codepoint >> 10)));
            result.push_back(static_cast<char16_t>(0xDC00 + (codepoint & 0x3FF)));
        }
    }
    return result;
}

void append_utf8(std::string& output, char32_t codepoint) {
    if (codepoint <= 0x7F) output.push_back(static_cast<char>(codepoint));
    else if (codepoint <= 0x7FF) {
        output.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
        output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else if (codepoint <= 0xFFFF) {
        output.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
        output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else {
        output.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
        output.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
}

jstring java_string(JNIEnv* environment, const std::string& value) {
    const auto converted = utf16_from_utf8(value);
    return environment->NewString(reinterpret_cast<const jchar*>(converted.data()),
                                  static_cast<jsize>(converted.size()));
}

std::string native_string(JNIEnv* environment, jstring value) {
    if (!value) return {};
    const auto size = environment->GetStringLength(value);
    if (environment->ExceptionCheck()) return {};
    // C++側でメモリを確保する間にJavaのメモリを固定しないよう、所有
    // する領域へコピーする。 保留中のJNIエラーは呼び出し元で変換する。
    std::u16string converted(static_cast<std::size_t>(size), u'\0');
    if (size != 0)
        environment->GetStringRegion(value, 0, size, reinterpret_cast<jchar*>(converted.data()));
    if (environment->ExceptionCheck()) return {};
    std::string result;
    result.reserve(converted.size());
    for (std::size_t index = 0; index < converted.size(); ++index) {
        char32_t codepoint = converted[index];
        if (codepoint >= 0xD800 && codepoint <= 0xDBFF && index + 1 < converted.size()) {
            const char32_t low = converted[index + 1];
            if (low >= 0xDC00 && low <= 0xDFFF) {
                codepoint = 0x10000 + ((codepoint - 0xD800) << 10) + (low - 0xDC00);
                ++index;
            } else codepoint = 0xFFFD;
        } else if (codepoint >= 0xD800 && codepoint <= 0xDFFF) {
            codepoint = 0xFFFD;
        }
        append_utf8(result, codepoint);
    }
    return result;
}

bool clear_java_exception(JNIEnv* environment) {
    if (!environment->ExceptionCheck()) return false;
    environment->ExceptionDescribe();
    environment->ExceptionClear();
    return true;
}

struct AndroidActivity {
    JNIEnv* environment{static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv())};
    jobject activity{static_cast<jobject>(SDL_GetAndroidActivity())};
    jclass type{};

    AndroidActivity() {
        if (environment && activity) type = environment->GetObjectClass(activity);
    }
    ~AndroidActivity() {
        if (!environment) return;
        if (type) environment->DeleteLocalRef(type);
        if (activity) environment->DeleteLocalRef(activity);
    }
    [[nodiscard]] bool valid() const noexcept {
        return environment && activity && type;
    }
};

std::string android_error(AndroidActivity& bridge) {
    if (!bridge.valid()) return "Androidファイル選択を初期化できません";
    const auto method = bridge.environment->GetMethodID(
        bridge.type, "cardwirthDocumentPickerError", "()Ljava/lang/String;");
    if (!method || clear_java_exception(bridge.environment))
        return "Androidファイル選択のエラーを取得できません";
    auto message = static_cast<jstring>(
        bridge.environment->CallObjectMethod(bridge.activity, method));
    if (clear_java_exception(bridge.environment)) return "Androidファイル選択でエラーが発生しました";
    const auto result = native_string(bridge.environment, message);
    if (message) bridge.environment->DeleteLocalRef(message);
    if (clear_java_exception(bridge.environment)) return "Androidファイル選択のエラーを変換できません";
    return result.empty() ? "Androidファイル選択でエラーが発生しました" : result;
}

TransferResult pick_documents(jint kind, const std::filesystem::path& suggested = {},
                              bool multiple = true,
                              const std::vector<std::string>& extensions = {}) {
    TransferResult result;
    AndroidActivity bridge;
    if (!bridge.valid()) {
        result.status = TransferStatus::Failed;
        result.error = "Androidファイル選択を初期化できません";
        return result;
    }
    // UTF変換やメモリ確保が例外を投げても、JNIのローカル参照はスコープ終了時に解放
    // する。 配列の各要素は直ちに解放し、要素数に比例して参照容量が増えないようにする。
    if (bridge.environment->PushLocalFrame(16) < 0) {
        (void)clear_java_exception(bridge.environment);
        return {TransferStatus::Failed, {}, "Androidファイル選択の引数を確保できません"};
    }
    struct LocalFrame {
        JNIEnv* environment;
        ~LocalFrame() { environment->PopLocalFrame(nullptr); }
    } frame{bridge.environment};
    const auto method = bridge.environment->GetMethodID(
        bridge.type, "cardwirthPickDocuments", "(ILjava/lang/String;Z[Ljava/lang/String;)[Ljava/lang/String;");
    if (clear_java_exception(bridge.environment) || !method) {
        result.status = TransferStatus::Failed;
        result.error = "Androidファイル選択のJavaブリッジが見つかりません";
        return result;
    }
    if (extensions.size() > static_cast<std::size_t>((std::numeric_limits<jsize>::max)()))
        return {TransferStatus::Failed, {}, "ファイルの種類が多すぎます"};
    const auto string_type = bridge.environment->FindClass("java/lang/String");
    if (clear_java_exception(bridge.environment) || !string_type)
        return {TransferStatus::Failed, {}, "Androidファイル選択の文字列型を取得できません"};
    auto native_extensions = bridge.environment->NewObjectArray(
        static_cast<jsize>(extensions.size()), string_type, nullptr);
    if (clear_java_exception(bridge.environment) || !native_extensions)
        return {TransferStatus::Failed, {}, "ファイルの種類を確保できません"};
    for (std::size_t index = 0; index < extensions.size(); ++index) {
        auto extension = java_string(bridge.environment, extensions[index]);
        if (clear_java_exception(bridge.environment) || !extension)
            return {TransferStatus::Failed, {}, "ファイルの種類を変換できません"};
        bridge.environment->SetObjectArrayElement(native_extensions, static_cast<jsize>(index), extension);
        bridge.environment->DeleteLocalRef(extension);
        if (clear_java_exception(bridge.environment))
            return {TransferStatus::Failed, {}, "ファイルの種類を渡せません"};
    }
    auto name = java_string(bridge.environment, path_to_utf8(suggested.filename()));
    if (clear_java_exception(bridge.environment) || !name)
        return {TransferStatus::Failed, {}, "保存名を変換できません"};
    auto paths = static_cast<jobjectArray>(bridge.environment->CallObjectMethod(
        bridge.activity, method, kind, name, multiple ? JNI_TRUE : JNI_FALSE, native_extensions));
    if (name) bridge.environment->DeleteLocalRef(name);
    if (clear_java_exception(bridge.environment) || !paths) {
        result.status = TransferStatus::Failed;
        result.error = android_error(bridge);
        return result;
    }
    const auto count = bridge.environment->GetArrayLength(paths);
    if (clear_java_exception(bridge.environment))
        return {TransferStatus::Failed, {}, "選択結果の件数を取得できません"};
    if (count == 0) {
        result.status = TransferStatus::Cancelled;
        bridge.environment->DeleteLocalRef(paths);
        return result;
    }
    result.documents.reserve(static_cast<std::size_t>(count));
    for (jsize index = 0; index < count; ++index) {
        auto path = static_cast<jstring>(bridge.environment->GetObjectArrayElement(paths, index));
        if (clear_java_exception(bridge.environment))
            return {TransferStatus::Failed, {}, "選択結果を取得できません"};
        const auto encoded = native_string(bridge.environment, path);
        if (path) bridge.environment->DeleteLocalRef(path);
        if (clear_java_exception(bridge.environment) || encoded.empty() || encoded.find('\0') != std::string::npos)
            return {TransferStatus::Failed, {}, "選択結果のパスが不正です"};
        ExternalDocument document;
        // AndroidのパスはUTF-8のバイト列なので、非推奨のu8pathは使わない。
        document.native_path = std::filesystem::path(encoded);
        document.display_name = path_to_utf8(document.native_path.filename());
        result.documents.push_back(std::move(document));
    }
    bridge.environment->DeleteLocalRef(paths);
    if (result.documents.empty()) {
        result.status = TransferStatus::Failed;
        result.error = "選択した項目を一時領域へ読み込めませんでした";
    } else {
        result.status = TransferStatus::Success;
    }
    return result;
}

struct AndroidSelectionDispatch {
    NativePathSelectionCallback callback;
    NativePathSelection selection;
};

void SDLCALL dispatch_android_selection(void* userdata) noexcept {
    std::unique_ptr<AndroidSelectionDispatch> dispatch(
        static_cast<AndroidSelectionDispatch*>(userdata));
    if (!dispatch || !dispatch->callback) return;
    try {
        dispatch->callback(std::move(dispatch->selection));
    } catch (const std::exception& error) {
        SDL_Log("Android document picker callback failed: %s", error.what());
    } catch (...) {
        SDL_Log("Android document picker callback failed with an unknown exception");
    }
}

void choose_android_async(std::function<NativePathSelection()> picker,
                          NativePathSelectionCallback callback) {
    try {
        std::thread([picker = std::move(picker), callback = std::move(callback)]() mutable {
            NativePathSelection selection;
            try {
                selection = picker();
            } catch (const std::exception& error) {
                selection.status = TransferStatus::Failed;
                selection.error = std::string("Androidファイル選択で例外が発生しました: ") +
                                  error.what();
            } catch (...) {
                selection.status = TransferStatus::Failed;
                selection.error = "Androidファイル選択で不明な例外が発生しました";
            }

            auto dispatch = std::make_unique<AndroidSelectionDispatch>();
            dispatch->callback = std::move(callback);
            dispatch->selection = std::move(selection);
            auto* raw_dispatch = dispatch.release();
            if (!SDL_RunOnMainThread(dispatch_android_selection, raw_dispatch, false)) {
                delete raw_dispatch;
                SDL_Log("Android document picker result could not return to the SDL main thread: %s",
                        SDL_GetError());
            }
        }).detach();
    } catch (const std::exception& error) {
        NativePathSelection selection;
        selection.status = TransferStatus::Failed;
        selection.error = std::string("Androidファイル選択を開始できません: ") + error.what();
        callback(std::move(selection));
    } catch (...) {
        NativePathSelection selection;
        selection.status = TransferStatus::Failed;
        selection.error = "Androidファイル選択を開始できません";
        callback(std::move(selection));
    }
}

}  // namespace

}  // namespace cardwirth::explorer

#elif defined(__EMSCRIPTEN__)

namespace cardwirth::explorer {
namespace {

enum class WebTransferKind : int {
    Files = 0,
    Folder = 1,
    Save = 2,
    Stored = 3,
    ExportFolder = 4,
};

struct WebTransferState {
    NativePathSelectionCallback callback;
    std::vector<std::filesystem::path> paths;
    std::string suggested_name;
    std::string error;
    std::optional<std::size_t> selected_filter;
    TransferStatus status{TransferStatus::Failed};
    WebTransferKind kind{WebTransferKind::Files};
#if defined(CARDWIRTH_WEB_PTHREADS) && CARDWIRTH_WEB_PTHREADS
    pthread_t owner{};
#endif
};

struct WebFinalizeState {
    FinalizeDownloadCallback callback;
    std::string path;
    std::string error;
    bool success{};
#if defined(CARDWIRTH_WEB_PTHREADS) && CARDWIRTH_WEB_PTHREADS
    pthread_t owner{};
#endif
};

struct WebImportLease {
    std::filesystem::path root;
};

std::mutex web_transfer_mutex;
std::map<int, std::shared_ptr<WebTransferState>> web_transfers;
std::map<int, std::shared_ptr<WebFinalizeState>> web_finalizations;
std::map<std::string, WebImportLease> web_import_leases;
std::atomic_int web_transfer_sequence{};

std::string path_to_utf8(const std::filesystem::path& value) {
    const auto encoded = value.u8string();
    return {reinterpret_cast<const char*>(encoded.data()), encoded.size()};
}

// web_transfer.jsがブラウザーのスレッド
// でDOMを操作する。 これらのインポート関数はすぐに
// 戻り、下のエクスポート関数を通じて完了を通知する。
extern "C" void cardwirth_web_choose_document(int request_id, int mode,
                                                const char* suggested_name, int multiple, const char* accept,
                                                const char* filter_labels,const char* filter_accepts);
extern "C" void cardwirth_web_finalize_download(int request_id, const char* path,
                                                  int success);
extern "C" void cardwirth_web_choose_stored_document(int request_id, int mode,
    const char* initial, const char* title, const char* extensions,
    int multiple, int confirm_overwrite, const char* filter_labels,
    const char* filter_extensions, int selected_filter);

void complete_web_transfer_on_owner(int request_id) noexcept {
    std::shared_ptr<WebTransferState> state;
    {
        std::scoped_lock lock(web_transfer_mutex);
        const auto found = web_transfers.find(request_id);
        if (found == web_transfers.end()) return;
        state = std::move(found->second);
        web_transfers.erase(found);
        if (state->status == TransferStatus::Success &&
            (state->kind == WebTransferKind::Files || state->kind == WebTransferKind::Folder)) {
            const auto root = std::filesystem::path(
                "/tmp/cardwirth-import-" + std::to_string(request_id));
            for (const auto& path : state->paths)
                web_import_leases[path_to_utf8(path)] = {root};
        }
    }
    NativePathSelection selection;
    selection.status = state->status;
    selection.paths = std::move(state->paths);
    selection.selected_filter = state->selected_filter;
    selection.error = std::move(state->error);
    selection.cancelled = selection.status == TransferStatus::Cancelled;
    try {
        if (state->callback) state->callback(std::move(selection));
    } catch (const std::exception& error) {
        SDL_Log("Browser document picker callback failed: %s", error.what());
    } catch (...) {
        SDL_Log("Browser document picker callback failed with an unknown exception");
    }
}

void complete_web_finalization_on_owner(int request_id) noexcept {
    std::shared_ptr<WebFinalizeState> state;
    {
        std::scoped_lock lock(web_transfer_mutex);
        const auto found = web_finalizations.find(request_id);
        if (found == web_finalizations.end()) return;
        state = std::move(found->second);
        web_finalizations.erase(found);
    }
    try {
        if (state->callback) state->callback(state->success, std::move(state->error));
    } catch (const std::exception& error) {
        SDL_Log("Browser download callback failed: %s", error.what());
    } catch (...) {
        SDL_Log("Browser download callback failed with an unknown exception");
    }
}

void dispatch_web_transfer_completion(const int request_id,
                                      const std::shared_ptr<WebTransferState>& state) noexcept {
#if defined(CARDWIRTH_WEB_PTHREADS) && CARDWIRTH_WEB_PTHREADS
    (void)emscripten_dispatch_to_thread_async(
        state->owner, EM_FUNC_SIG_VI, complete_web_transfer_on_owner, nullptr, request_id);
#else
    (void)state;
    complete_web_transfer_on_owner(request_id);
#endif
}

void dispatch_web_finalization_completion(
    const int request_id, const std::shared_ptr<WebFinalizeState>& state) noexcept {
#if defined(CARDWIRTH_WEB_PTHREADS) && CARDWIRTH_WEB_PTHREADS
    (void)emscripten_dispatch_to_thread_async(
        state->owner, EM_FUNC_SIG_VI, complete_web_finalization_on_owner, nullptr, request_id);
#else
    (void)state;
    complete_web_finalization_on_owner(request_id);
#endif
}

void choose_web_async(WebTransferKind kind, std::filesystem::path suggested_name,
                      NativePathSelectionCallback callback,
                      bool multiple = true, const std::vector<std::string>& extensions = {},
                      const std::vector<std::pair<std::string,std::vector<std::string>>>& filters = {}) {
    const int request_id = ++web_transfer_sequence;
    auto state = std::make_shared<WebTransferState>();
    state->callback = std::move(callback);
    state->kind = kind;
    state->suggested_name = path_to_utf8(suggested_name.filename());
#if defined(CARDWIRTH_WEB_PTHREADS) && CARDWIRTH_WEB_PTHREADS
    state->owner = pthread_self();
#endif
    {
        std::scoped_lock lock(web_transfer_mutex);
        web_transfers.emplace(request_id, state);
    }
    const auto to_accept=[](const std::vector<std::string>& suffixes){std::string result;for(const auto& suffix:suffixes){if(!result.empty())result+=",";result+="."+suffix;}return result;};
    const auto accept=to_accept(extensions);std::string labels,accepts;bool first=true;
    // 改行は選択画面のラベル・グループの区切りなので、ラベルだけを1行化し、ファイル名は変更しない。
    for(const auto& filter:filters){auto label=filter.first;std::replace(label.begin(),label.end(),'\n',' ');std::replace(label.begin(),label.end(),'\r',' ');if(!first){labels+='\n';accepts+='\n';}first=false;labels+=label;accepts+=to_accept(filter.second);}
    cardwirth_web_choose_document(request_id, static_cast<int>(kind),
                                  state->suggested_name.c_str(),multiple?1:0,accept.c_str(),labels.c_str(),accepts.c_str());
}

void finalize_web_async(const std::filesystem::path& path, bool success,
                        FinalizeDownloadCallback callback) {
    const int request_id = ++web_transfer_sequence;
    auto state = std::make_shared<WebFinalizeState>();
    state->callback = std::move(callback);
    state->path = path_to_utf8(path);
#if defined(CARDWIRTH_WEB_PTHREADS) && CARDWIRTH_WEB_PTHREADS
    state->owner = pthread_self();
#endif
    {
        std::scoped_lock lock(web_transfer_mutex);
        web_finalizations.emplace(request_id, state);
    }
    cardwirth_web_finalize_download(request_id, state->path.c_str(), success ? 1 : 0);
}

}  // namespace

extern "C" EMSCRIPTEN_KEEPALIVE void cardwirth_web_transfer_item(
    int request_id, const char* path) noexcept {
    if (!path || !*path) return;
    try {
        std::scoped_lock lock(web_transfer_mutex);
        const auto found = web_transfers.find(request_id);
        if (found != web_transfers.end())
            found->second->paths.emplace_back(std::filesystem::u8path(path));
    } catch (...) {}
}

extern "C" EMSCRIPTEN_KEEPALIVE void cardwirth_web_transfer_filter(
    int request_id, int selected_filter) noexcept {
    std::scoped_lock lock(web_transfer_mutex);
    const auto found = web_transfers.find(request_id);
    if (found != web_transfers.end() && selected_filter >= 0)
        found->second->selected_filter = static_cast<std::size_t>(selected_filter);
}

extern "C" EMSCRIPTEN_KEEPALIVE void cardwirth_web_transfer_complete(
    int request_id, int status, const char* message) noexcept {
    std::shared_ptr<WebTransferState> state;
    try {
        {
            std::scoped_lock lock(web_transfer_mutex);
            const auto found = web_transfers.find(request_id);
            if (found == web_transfers.end()) return;
            state = found->second;
            state->status = status == 0 ? TransferStatus::Success :
                            status == 1 ? TransferStatus::Cancelled : TransferStatus::Failed;
            state->error = message ? message : "";
            if (state->status == TransferStatus::Success && state->paths.empty()) {
                state->status = TransferStatus::Failed;
                state->error = "ブラウザのファイル選択結果が空です";
            }
        }
        dispatch_web_transfer_completion(request_id, state);
    } catch (...) {
        if (state) {
            state->status = TransferStatus::Failed;
            state->error = "ブラウザのファイル選択結果を処理できません";
            dispatch_web_transfer_completion(request_id, state);
        }
    }
}

extern "C" EMSCRIPTEN_KEEPALIVE void cardwirth_web_download_complete(
    int request_id, int success, const char* message) noexcept {
    std::shared_ptr<WebFinalizeState> state;
    try {
        {
            std::scoped_lock lock(web_transfer_mutex);
            const auto found = web_finalizations.find(request_id);
            if (found == web_finalizations.end()) return;
            state = found->second;
            state->success = success != 0;
            state->error = message ? message : "";
        }
        dispatch_web_finalization_completion(request_id, state);
    } catch (...) {
        if (state) {
            state->success = false;
            state->error = "ブラウザへの保存結果を処理できません";
            dispatch_web_finalization_completion(request_id, state);
        }
    }
}

}  // namespace cardwirth::explorer

#elif defined(__APPLE__)

#else

namespace cardwirth::explorer {
namespace {

struct SdlTransferDialogState {
    std::string filter_extensions;
    SDL_DialogFileFilter filter{};
    NativePathSelectionCallback callback;
    SDL_PropertiesID properties{};

    ~SdlTransferDialogState() {
        if (properties) SDL_DestroyProperties(properties);
    }
};

struct SdlTransferCompletion {
    NativePathSelectionCallback callback;
    NativePathSelection result;
};

void SDLCALL dispatch_sdl_transfer_completion(void* userdata) noexcept {
    std::unique_ptr<SdlTransferCompletion> completion(
        static_cast<SdlTransferCompletion*>(userdata));
    if (!completion || !completion->callback) return;
    try {
        completion->callback(std::move(completion->result));
    } catch (const std::exception& error) {
        SDL_Log("File dialog callback failed: %s", error.what());
    } catch (...) {
        SDL_Log("File dialog callback failed with an unknown exception");
    }
}

void SDLCALL complete_sdl_transfer_dialog(void* userdata,
                                          const char* const* files,
                                          int) noexcept {
    std::unique_ptr<std::shared_ptr<SdlTransferDialogState>> holder(
        static_cast<std::shared_ptr<SdlTransferDialogState>*>(userdata));
    const auto state = *holder;
    NativePathSelection result;
    try {
        if (!files) {
            result.status = TransferStatus::Failed;
            result.error = std::string("ファイル選択に失敗しました: ") + SDL_GetError();
        } else if (!files[0]) {
            result.status = TransferStatus::Cancelled;
            result.cancelled = true;
        } else {
            result.status = TransferStatus::Success;
            for (std::size_t index = 0; files[index]; ++index) {
                if (!files[index][0])
                    throw std::runtime_error("ファイル選択から空のパスが返されました");
                result.paths.push_back(std::filesystem::u8path(files[index]));
            }
        }
    } catch (const std::exception& error) {
        result = {};
        result.status = TransferStatus::Failed;
        result.error = error.what();
    } catch (...) {
        result = {};
        result.status = TransferStatus::Failed;
        result.error = "ファイル選択の結果を処理できません";
    }
    auto completion = std::make_unique<SdlTransferCompletion>();
    completion->callback = std::move(state->callback);
    completion->result = std::move(result);
    auto* raw = completion.release();
    if (SDL_IsMainThread()) {
        dispatch_sdl_transfer_completion(raw);
    } else if (!SDL_RunOnMainThread(dispatch_sdl_transfer_completion, raw, false)) {
        delete raw;
        SDL_Log("File dialog result could not return to the SDL main thread: %s",
                SDL_GetError());
    }
}

void show_sdl_transfer_dialog(SDL_FileDialogType type, SDL_Window* parent,
                              std::string title, bool multiple,
                              std::filesystem::path suggested_name,
                              NativePathSelectionCallback callback,
                              const std::vector<std::string>& extensions = {}) {
    auto state = std::make_shared<SdlTransferDialogState>();
    state->callback = std::move(callback);
    state->properties = SDL_CreateProperties();
    if(!extensions.empty()){
        for(const auto& extension:extensions){if(!state->filter_extensions.empty())state->filter_extensions+=";";state->filter_extensions+=extension;}
        state->filter={"ファイル",state->filter_extensions.c_str()};
        SDL_SetPointerProperty(state->properties,SDL_PROP_FILE_DIALOG_FILTERS_POINTER,&state->filter);
        SDL_SetNumberProperty(state->properties,SDL_PROP_FILE_DIALOG_NFILTERS_NUMBER,1);
    }
    const auto failed = [&state](std::string message) {
        NativePathSelection result;
        result.status = TransferStatus::Failed;
        result.error = std::move(message);
        state->callback(std::move(result));
    };
    if (!state->properties) {
        failed(std::string("ファイル選択を初期化できません: ") + SDL_GetError());
        return;
    }
    const auto set = [&failed](const bool ok) {
        if (!ok) failed(std::string("ファイル選択を初期化できません: ") + SDL_GetError());
        return ok;
    };
    if (!title.empty() && !set(SDL_SetStringProperty(
            state->properties, SDL_PROP_FILE_DIALOG_TITLE_STRING, title.c_str()))) return;
    if (parent && !set(SDL_SetPointerProperty(
            state->properties, SDL_PROP_FILE_DIALOG_WINDOW_POINTER, parent))) return;
    if (!set(SDL_SetBooleanProperty(
            state->properties, SDL_PROP_FILE_DIALOG_MANY_BOOLEAN, multiple))) return;
    if (!suggested_name.empty()) {
        const auto encoded = suggested_name.u8string();
        const std::string location(encoded.begin(), encoded.end());
        if (!set(SDL_SetStringProperty(state->properties,
                SDL_PROP_FILE_DIALOG_LOCATION_STRING, location.c_str()))) return;
    }
    auto* callback_state = new std::shared_ptr<SdlTransferDialogState>(state);
    SDL_ShowFileDialogWithProperties(
        type, complete_sdl_transfer_dialog, callback_state, state->properties);
}

}
}  // namespace cardwirth::explorer

#endif

namespace cardwirth::explorer {
namespace {

NativePathSelection to_native_selection(const TransferResult& result) {
    NativePathSelection selection;
    selection.status = result.status;
    selection.error = result.error;
    selection.cancelled = result.cancelled();
    selection.paths.reserve(result.documents.size());
    for (const auto& document : result.documents) {
        if (!document.native_path.empty()) selection.paths.push_back(document.native_path);
    }
    if (selection.status == TransferStatus::Success && selection.paths.empty()) {
        selection.status = TransferStatus::Failed;
        selection.error = "選択結果に互換性のあるnative pathがありません";
    }
    return selection;
}

NativePathSelectionCallback cancellation_aware(
    const TransferRequest& request, NativePathSelectionCallback callback,
    bool exporting = false) {
    const auto cancelled = request.cancellation_state();
    return [cancelled, callback = std::move(callback), exporting](NativePathSelection result) mutable {
        if (cancelled->load(std::memory_order_acquire)) {
            const auto abandoned = result.paths;
            result = {};
            result.status = TransferStatus::Cancelled;
            result.cancelled = true;
            try {
                if (exporting) {
                    for (const auto& path : abandoned)
                        finalize_download_path_async(path, false, [](bool, std::string) {});
                } else {
                    release_upload_paths(abandoned);
                }
            } catch (...) {}
        }
        callback(std::move(result));
    };
}

}  // namespace

bool supports_direct_folder_download() noexcept {
#if defined(__EMSCRIPTEN__) || defined(__ANDROID__)
    // WebのダウンロードとAndroidの文書選択は単一ファイルを出力する。 選
    // 択されたフォルダーは、Explorer共通の書き出し処理でZIPにまとめる。
    return false;
#elif defined(_WIN32) || defined(__APPLE__)
    return true;
#else
    // デスクトップのSDL選択画面はパスを返し、提供元で別途確定するツリー
    // を持たない。 ディレクトリへの直接書き込みにはネイティブパスを使う。
    return false;
#endif
}

TransferRequest choose_stored_paths_async(const StoredPathRequest& options,
                                         NativePathSelectionCallback callback) {
    TransferRequest request;
#if defined(__EMSCRIPTEN__)
    const int request_id = ++web_transfer_sequence;
    auto state = std::make_shared<WebTransferState>();
    state->kind = WebTransferKind::Stored;
    state->callback = cancellation_aware(request, std::move(callback));
#if defined(CARDWIRTH_WEB_PTHREADS) && CARDWIRTH_WEB_PTHREADS
    state->owner = pthread_self();
#endif
    {
        std::scoped_lock lock(web_transfer_mutex);
        web_transfers.emplace(request_id, state);
    }
    // 改行はラベル・パターングループの区切りであり、ファイルパスの区切りではない。
    // 翻訳されたラベルを1行化し、ラベル中の改行が架空の形式を作らないようにする。
    const auto pack = [](const std::vector<std::string>& values) {
        std::string packed;
        bool first = true;
        for (auto value : values) {
            std::replace(value.begin(), value.end(), '\n', ' ');
            std::replace(value.begin(), value.end(), '\r', ' ');
            if (!first) packed += '\n';
            first = false;
            packed += value;
        }
        return packed;
    };
    const auto labels = pack(options.filter_labels);
    const auto patterns = pack(options.filter_extensions);
    cardwirth_web_choose_stored_document(request_id,
        options.save ? 2 : options.directory ? 1 : 0,
        options.initial_location.c_str(), options.title.c_str(), options.extensions.c_str(),
        options.allow_multiple ? 1 : 0, options.confirm_overwrite ? 1 : 0,
        labels.c_str(), patterns.c_str(), options.selected_filter
            ? static_cast<int>(*options.selected_filter) : -1);
#else
    (void)options;
    NativePathSelection result;
    result.status = TransferStatus::Unsupported;
    result.error = "Stored browser paths require Web";
    callback(std::move(result));
#endif
    return request;
}

namespace {
bool finalize_download_path_platform(const std::filesystem::path& path, bool success,
                                     std::string& error) {
#if defined(__ANDROID__)
    AndroidActivity bridge;
    if (!bridge.valid()) {
        error = "Android書き出しを初期化できません";
        return false;
    }
    auto staging = java_string(bridge.environment, path_to_utf8(path));
    bool cleanup_only = !success;
    bool overwrite_accepted = true;
    std::string conflict_error;
    if (success) {
        const auto conflict_method = bridge.environment->GetMethodID(
            bridge.type, "cardwirthDocumentExportConflicts",
            "(Ljava/lang/String;)[Ljava/lang/String;");
        if (!conflict_method || clear_java_exception(bridge.environment)) {
            conflict_error = "Android保存先の競合確認ブリッジが見つかりません";
            cleanup_only = true;
        } else {
            auto values = static_cast<jobjectArray>(bridge.environment->CallObjectMethod(
                bridge.activity, conflict_method, staging));
            if (clear_java_exception(bridge.environment) || !values) {
                conflict_error = android_error(bridge);
                cleanup_only = true;
            } else {
                std::vector<std::string> conflicts;
                const auto count = bridge.environment->GetArrayLength(values);
                conflicts.reserve(static_cast<std::size_t>(count));
                for (jsize index = 0; index < count; ++index) {
                    auto value = static_cast<jstring>(
                        bridge.environment->GetObjectArrayElement(values, index));
                    conflicts.push_back(native_string(bridge.environment, value));
                    if (value) bridge.environment->DeleteLocalRef(value);
                }
                bridge.environment->DeleteLocalRef(values);
                if (!conflicts.empty())
                    overwrite_accepted = confirm_overwrite_with_common_sdl(conflicts);
            }
        }
    }
    const auto method = bridge.environment->GetMethodID(
        bridge.type, "cardwirthFinalizeDocumentExport", "(Ljava/lang/String;ZZ)Z");
    if (!method || clear_java_exception(bridge.environment)) {
        if (staging) bridge.environment->DeleteLocalRef(staging);
        error = "Android書き出しのJavaブリッジが見つかりません";
        return false;
    }
    const auto completed = bridge.environment->CallBooleanMethod(
        bridge.activity, method, staging, cleanup_only ? JNI_FALSE : JNI_TRUE,
        overwrite_accepted ? JNI_TRUE : JNI_FALSE);
    if (staging) bridge.environment->DeleteLocalRef(staging);
    if (!conflict_error.empty()) {
        (void)clear_java_exception(bridge.environment);
        error = std::move(conflict_error);
        return false;
    }
    if (clear_java_exception(bridge.environment) || completed != JNI_TRUE) {
        error = android_error(bridge);
        return false;
    }
    error.clear();
    return true;
#else
    (void)path;
    (void)success;
    error.clear();
    return true;
#endif
}
}  // namespace

TransferRequest choose_upload_files_async(SDL_Window* parent,
                                          NativePathSelectionCallback callback,
                                          const UploadFileOptions& upload_options) {
    TransferRequest request;
    callback = cancellation_aware(request, std::move(callback));
#if defined(__APPLE__)
    apple::ImportOptions options;
    options.multiple=upload_options.multiple;options.extensions=upload_options.extensions;
    options.cancelled = [cancelled = request.cancellation_state()] {
        return cancelled->load(std::memory_order_acquire);
    };
    apple::choose_import_files(parent, std::move(options), std::move(callback));
#elif defined(__ANDROID__)
    choose_android_async(
        [parent,upload_options] { return to_native_selection(pick_documents(kPickUploadFiles,{},upload_options.multiple,upload_options.extensions)); },
        std::move(callback));
#elif defined(__EMSCRIPTEN__)
    choose_web_async(WebTransferKind::Files, {}, std::move(callback),upload_options.multiple,upload_options.extensions,upload_options.filters);
#elif defined(_WIN32)
    callback(to_native_selection(select_paths(
        parent, false, upload_options.multiple, L"アップロードするファイルを選択",upload_options.extensions)));
#else
    show_sdl_transfer_dialog(SDL_FILEDIALOG_OPENFILE, parent,
                             "アップロードするファイルを選択", upload_options.multiple, {},
                             std::move(callback),upload_options.extensions);
#endif
    return request;
}

TransferRequest choose_upload_folders_async(SDL_Window* parent,
                                            NativePathSelectionCallback callback) {
    TransferRequest request;
    callback = cancellation_aware(request, std::move(callback));
#if defined(__APPLE__)
    apple::choose_upload_folders(parent, std::move(callback));
#elif defined(__ANDROID__)
    choose_android_async(
        [parent] { return to_native_selection(pick_documents(kPickUploadFolder)); },
        std::move(callback));
#elif defined(__EMSCRIPTEN__)
    choose_web_async(WebTransferKind::Folder, {}, std::move(callback));
#elif defined(_WIN32)
    callback(to_native_selection(select_paths(
        parent, true, true, L"アップロードするフォルダを選択")));
#else
    show_sdl_transfer_dialog(SDL_FILEDIALOG_OPENFOLDER, parent,
                             "アップロードするフォルダを選択", true, {},
                             std::move(callback));
#endif
    return request;
}

TransferRequest choose_download_path_async(SDL_Window* parent,
                                           const std::filesystem::path& suggested_name,
                                           bool directory,
                                           NativePathSelectionCallback callback) {
    TransferRequest request;
    callback = cancellation_aware(request, std::move(callback), true);
#if defined(__APPLE__)
    apple::choose_download_path(parent, suggested_name, directory, std::move(callback));
#elif defined(__ANDROID__)
    choose_android_async(
        [suggested_name, directory] {
            return to_native_selection(pick_documents(
                directory ? kPickSaveNamedFolder : kPickSaveFile, suggested_name));
        }, std::move(callback));
#elif defined(__EMSCRIPTEN__)
    if (directory) {
        NativePathSelection result;
        result.status = TransferStatus::Unsupported;
        result.error = "ブラウザではフォルダをZIPとしてダウンロードしてください";
        callback(std::move(result));
    } else {
        choose_web_async(WebTransferKind::Save, suggested_name, std::move(callback));
    }
#elif defined(_WIN32)
    callback(to_native_selection(select_save_path(parent, suggested_name, directory)));
#else
    show_sdl_transfer_dialog(directory ? SDL_FILEDIALOG_OPENFOLDER
                                       : SDL_FILEDIALOG_SAVEFILE,
                             parent, directory ? "ダウンロード先を選択"
                                               : "保存先を選択",
                             false, directory ? std::filesystem::path{}
                                              : suggested_name,
                             std::move(callback));
#endif
    return request;
}

TransferRequest choose_download_folder_async(SDL_Window* parent,
                                             NativePathSelectionCallback callback) {
    TransferRequest request;
    callback = cancellation_aware(request, std::move(callback));
#if defined(__APPLE__)
    apple::choose_download_folder(parent, std::move(callback));
#elif defined(__ANDROID__)
    choose_android_async(
        [parent] { return to_native_selection(pick_documents(kPickDownloadFolder)); },
        std::move(callback));
#elif defined(__EMSCRIPTEN__)
    choose_web_async(WebTransferKind::ExportFolder, {}, std::move(callback));
#elif defined(_WIN32)
    callback(to_native_selection(select_paths(
        parent, true, false, L"ダウンロード先のフォルダを選択")));
#else
    show_sdl_transfer_dialog(SDL_FILEDIALOG_OPENFOLDER, parent,
                             "ダウンロード先のフォルダを選択", false, {},
                             std::move(callback));
#endif
    return request;
}

void finalize_download_path_async(const std::filesystem::path& path, bool success,
                                  FinalizeDownloadCallback callback) {
#if defined(__APPLE__)
    apple::finalize_download_path(path, success, std::move(callback));
#elif defined(__EMSCRIPTEN__)
    finalize_web_async(path, success, std::move(callback));
#else
    std::string error;
    const bool completed = finalize_download_path_platform(path, success, error);
    callback(completed, std::move(error));
#endif
}

void release_upload_paths(const std::vector<std::filesystem::path>& paths) {
#if defined(__APPLE__)
    apple::release_upload_paths(paths);
#elif defined(__ANDROID__)
    if (paths.empty()) return;
    AndroidActivity bridge;
    if (!bridge.valid()) throw std::runtime_error("Android import cleanup is unavailable");
    const auto method = bridge.environment->GetMethodID(
        bridge.type, "cardwirthReleaseDocumentImport", "(Ljava/lang/String;)V");
    const bool lookup_failed = clear_java_exception(bridge.environment);
    if (!method || lookup_failed) throw std::runtime_error("Android import cleanup bridge is missing");
    bool failed{};
    for (const auto& path : paths) {
        auto value = java_string(bridge.environment, path_to_utf8(path));
        const bool string_failed = clear_java_exception(bridge.environment);
        if (value && !string_failed) {
            bridge.environment->CallVoidMethod(bridge.activity, method, value);
            failed = clear_java_exception(bridge.environment) || failed;
        } else failed = true;
        if (value) bridge.environment->DeleteLocalRef(value);
    }
    if (failed) throw std::runtime_error("Android import staging could not be released");
#elif defined(__EMSCRIPTEN__)
    std::vector<std::filesystem::path> roots;
    {
        std::scoped_lock lock(web_transfer_mutex);
        for (const auto& path : paths) {
            const auto found = web_import_leases.find(path_to_utf8(path));
            if (found == web_import_leases.end()) continue;
            roots.push_back(found->second.root);
            web_import_leases.erase(found);
        }
        for (auto iterator = roots.begin(); iterator != roots.end();) {
            const bool still_used = std::any_of(
                web_import_leases.begin(), web_import_leases.end(),
                [&iterator](const auto& entry) { return entry.second.root == *iterator; });
            if (still_used) iterator = roots.erase(iterator);
            else ++iterator;
        }
    }
    std::sort(roots.begin(), roots.end());
    roots.erase(std::unique(roots.begin(), roots.end()), roots.end());
    for (const auto& root : roots) {
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
    }
#else
    (void)paths;
#endif
}

void cleanup_stale_transfers() {
#if defined(__APPLE__)
    apple::cleanup_stale_transfers();
#elif defined(__EMSCRIPTEN__)
    std::error_code ignored;
    const std::filesystem::path temporary_root = "/tmp";
    for (const auto& entry : std::filesystem::directory_iterator(temporary_root, ignored)) {
        if (ignored) break;
        const auto name = path_to_utf8(entry.path().filename());
        if (name.starts_with("cardwirth-import-") ||
            name.starts_with("cardwirth-export-"))
            std::filesystem::remove_all(entry.path(), ignored);
    }
#endif
}

}  // namespace cardwirth::explorer
