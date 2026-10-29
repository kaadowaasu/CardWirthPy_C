#include "cw/lib/runtime_access.hpp"
#include "debug_control_server.hpp"
#include "cw/cw.hpp"
#include "cw/battle.hpp"
#include "cw/debug/logging.hpp"
#include "cw/frame.hpp"
#include "cw/dialog/presentation.hpp"
#include "cw/eventrelay.hpp"
#include "cw/scenariodb.hpp"
#include "cw/setting.hpp"
#include "cw/lib/imagecodec.hpp"
#include "cw/lib/encoding.hpp"
#include "cw/sprite/base.hpp"
#include "cw/sprite/card.hpp"
#include "cw/sprite/statusbar.hpp"
#include "cw/thread.hpp"
#include "platform/storage/storage_mount.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <climits>
#include <cmath>
#include <ctime>
#include <cstring>
#include <cwchar>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <map>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <commctrl.h>
#include <dbghelp.h>
#if defined(_DEBUG)
#include <crtdbg.h>
#endif
#endif

namespace cardwirth::debug {
namespace {

std::string json_escape(std::string_view value)
{
    std::ostringstream output;
    for (const unsigned char c : value) {
        switch (c) {
            case '"': output << "\\\""; break;
            case '\\': output << "\\\\"; break;
            case '\b': output << "\\b"; break;
            case '\f': output << "\\f"; break;
            case '\n': output << "\\n"; break;
            case '\r': output << "\\r"; break;
            case '\t': output << "\\t"; break;
            default:
                if (c < 0x20) output << "\\u" << std::hex << std::setw(4)
                                     << std::setfill('0') << static_cast<int>(c) << std::dec;
                else output << static_cast<char>(c);
        }
    }
    return output.str();
}

std::optional<std::string> json_string_at(std::string_view input, std::size_t& offset)
{
    while (offset < input.size() && std::isspace(static_cast<unsigned char>(input[offset]))) ++offset;
    if (offset >= input.size() || input[offset] != '"') return std::nullopt;
    ++offset;
    std::string result;
    while (offset < input.size()) {
        const char c = input[offset++];
        if (c == '"') return result;
        if (c != '\\') { result.push_back(c); continue; }
        if (offset >= input.size()) return std::nullopt;
        const char escaped = input[offset++];
        switch (escaped) {
            case '"': case '\\': case '/': result.push_back(escaped); break;
            case 'b': result.push_back('\b'); break;
            case 'f': result.push_back('\f'); break;
            case 'n': result.push_back('\n'); break;
            case 'r': result.push_back('\r'); break;
            case 't': result.push_back('\t'); break;
            default: return std::nullopt;
        }
    }
    return std::nullopt;
}

std::optional<std::string> json_string_field(std::string_view input, std::string_view field)
{
    const std::string needle = "\"" + std::string(field) + "\"";
    auto offset = input.find(needle);
    if (offset == std::string_view::npos) return std::nullopt;
    offset = input.find(':', offset + needle.size());
    if (offset == std::string_view::npos) return std::nullopt;
    ++offset;
    return json_string_at(input, offset);
}

template<typename T>
std::shared_ptr<T> debug_object_value(const ::cw::lib::Value& value)
{
    const auto* object = std::get_if<::cw::lib::ObjectPtr>(&value.storage());
    return object && *object ? std::dynamic_pointer_cast<T>(*object) : nullptr;
}

std::mutex g_log_mutex;
std::filesystem::path g_log_path;
bool g_log_mounted = false;
std::string g_mounted_log_buffer;
std::mutex g_slow_log_mutex;
std::unordered_map<std::string, std::chrono::steady_clock::time_point> g_last_slow_logs;
std::atomic_bool g_nonactive_automation{false};
#if defined(CARDWIRTH_LOG_ENABLED)
constexpr std::uintmax_t k_max_log_bytes = 1024U * 1024U;
std::uintmax_t g_log_bytes = 0;
bool g_log_limit_reported = false;
#if defined(_WIN32)
HANDLE g_log_console = nullptr;
bool g_log_console_owned = false;

void initialize_log_console()
{
    if (!GetConsoleWindow()) {
        if (!AllocConsole()) return;
        g_log_console_owned = true;
        SetConsoleTitleW(L"CardWirthPy_C log");
    }
    SetConsoleOutputCP(CP_UTF8);
    g_log_console = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!g_log_console || g_log_console == INVALID_HANDLE_VALUE) {
        g_log_console = CreateFileW(L"CONOUT$", GENERIC_WRITE | GENERIC_READ,
                                    FILE_SHARE_WRITE | FILE_SHARE_READ, nullptr,
                                    OPEN_EXISTING, 0, nullptr);
    }
}

void write_log_console(const std::string& text)
{
    if (!g_log_console || g_log_console == INVALID_HANDLE_VALUE || text.empty()) return;
    DWORD written = 0;
    WriteFile(g_log_console, text.data(),
              static_cast<DWORD>(std::min<std::size_t>(text.size(), 0xFFFFFFFFU)),
              &written, nullptr);
}
#endif
#endif

#if defined(_WIN32)
std::wstring wide(const std::string& value)
{
    if (value.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                         value.data(), static_cast<int>(value.size()),
                                         nullptr, 0);
    if (size <= 0) throw std::invalid_argument("Input text is not valid UTF-8");
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                        static_cast<int>(value.size()), result.data(), size);
    return result;
}

std::string narrow(const std::wstring& value)
{
    if (value.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                                         nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                        result.data(), size, nullptr, nullptr);
    return result;
}

std::vector<std::string> split_words(const std::string& command)
{
    std::istringstream input(command);
    std::vector<std::string> result;
    for (std::string word; input >> word;) result.push_back(std::move(word));
    return result;
}

int parse_integer(const std::string& value)
{
    std::size_t consumed = 0;
    const int result = std::stoi(value, &consumed, 0);
    if (consumed != value.size()) throw std::invalid_argument("Invalid integer: " + value);
    return result;
}

WPARAM parse_key(std::string value)
{
    for (char& c : value) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    static const std::map<std::string, WPARAM> keys{
        {"BACKSPACE", VK_BACK}, {"TAB", VK_TAB}, {"ENTER", VK_RETURN},
        {"ESC", VK_ESCAPE}, {"ESCAPE", VK_ESCAPE}, {"SPACE", VK_SPACE},
        {"PAGEUP", VK_PRIOR}, {"PAGEDOWN", VK_NEXT}, {"END", VK_END},
        {"HOME", VK_HOME}, {"LEFT", VK_LEFT}, {"UP", VK_UP},
        {"RIGHT", VK_RIGHT}, {"DOWN", VK_DOWN}, {"INSERT", VK_INSERT},
        {"DELETE", VK_DELETE}, {"SHIFT", VK_SHIFT}, {"CTRL", VK_CONTROL},
        {"ALT", VK_MENU},
    };
    if (const auto found = keys.find(value); found != keys.end()) return found->second;
    if (value.size() >= 2 && value[0] == 'F') {
        const int index = parse_integer(value.substr(1));
        if (index >= 1 && index <= 24) return static_cast<WPARAM>(VK_F1 + index - 1);
    }
    if (value.size() == 1 && std::isalnum(static_cast<unsigned char>(value[0])))
        return static_cast<WPARAM>(value[0]);
    const int numeric = parse_integer(value);
    if (numeric < 0 || numeric > 0xFF) throw std::invalid_argument("Virtual key is out of range");
    return static_cast<WPARAM>(numeric);
}

HWND game_window()
{
    return FindWindowW(L"CardWirthSDLFrame", nullptr);
}

HWND process_window_by_class(const std::wstring& class_name)
{
    struct Search {
        const std::wstring* class_name{};
        HWND visible{};
        HWND any{};
    } search{&class_name, nullptr, nullptr};
    EnumWindows([](HWND window, LPARAM parameter) -> BOOL {
        auto& state = *reinterpret_cast<Search*>(parameter);
        DWORD process_id = 0;
        GetWindowThreadProcessId(window, &process_id);
        if (process_id != GetCurrentProcessId()) return TRUE;
        wchar_t actual_class[256]{};
        GetClassNameW(window, actual_class, static_cast<int>(std::size(actual_class)));
        if (*state.class_name != actual_class) return TRUE;
        if (!state.any) state.any = window;
        if (!state.visible && IsWindowVisible(window)) state.visible = window;
        return TRUE;
    }, reinterpret_cast<LPARAM>(&search));
    return search.visible ? search.visible : search.any;
}

HWND require_process_window(const std::string& class_name)
{
    const HWND window = process_window_by_class(wide(class_name));
    if (!window) throw std::runtime_error("Window class not found: " + class_name);
    return window;
}

HWND child_by_text(HWND parent, const std::wstring& expected)
{
    struct Search {
        const std::wstring* expected{};
        HWND enabled{};
        HWND any{};
    } search{&expected, nullptr, nullptr};
    EnumChildWindows(parent, [](HWND child, LPARAM parameter) -> BOOL {
        auto& state = *reinterpret_cast<Search*>(parameter);
        const int length = GetWindowTextLengthW(child);
        const auto text_length = static_cast<std::size_t>(length > 0 ? length : 0);
        std::wstring actual(text_length + 1U, L'\0');
        if (length > 0) GetWindowTextW(child, actual.data(), length + 1);
        actual.resize(text_length);
        if (actual != *state.expected) return TRUE;
        if (!state.any) state.any = child;
        if (!state.enabled && IsWindowEnabled(child)) state.enabled = child;
        return TRUE;
    }, reinterpret_cast<LPARAM>(&search));
    return search.enabled ? search.enabled : search.any;
}

HWND require_control_id(HWND parent, int control_id)
{
    const HWND control = GetDlgItem(parent, control_id);
    if (!control) throw std::runtime_error("Control ID not found: " + std::to_string(control_id));
    return control;
}

HWND require_control_text(HWND parent, const std::string& text)
{
    const HWND control = child_by_text(parent, wide(text));
    if (!control) throw std::runtime_error("Control text not found: " + text);
    return control;
}

std::shared_ptr<::cw::frame::Frame> game_frame()
{
    ::cw::lib::RuntimeAccess runtime_access;
    if (!::cw::g_cwpy) return nullptr;
    const auto* object = std::get_if<::cw::lib::ObjectPtr>(&::cw::g_cwpy->m_frame.storage());
    return object && *object ? std::dynamic_pointer_cast<::cw::frame::Frame>(*object) : nullptr;
}

std::string process_windows()
{
    struct WindowInfo {
        HWND handle{};
        bool visible{};
        bool enabled{};
        bool foreground{};
        DWORD thread_id{};
        std::wstring klass{};
        std::wstring title{};
        std::vector<std::wstring> child_text{};
    };
    std::vector<WindowInfo> windows;
    EnumWindows([](HWND handle, LPARAM parameter) -> BOOL {
        DWORD process_id = 0;
        const DWORD thread_id = GetWindowThreadProcessId(handle, &process_id);
        if (process_id != GetCurrentProcessId()) return TRUE;
        wchar_t klass[256]{};
        wchar_t title[512]{};
        GetClassNameW(handle, klass, static_cast<int>(std::size(klass)));
        GetWindowTextW(handle, title, static_cast<int>(std::size(title)));
        auto* result = reinterpret_cast<std::vector<WindowInfo>*>(parameter);
        const auto active_window = GetForegroundWindow();
        WindowInfo info{handle, IsWindowVisible(handle) != FALSE, IsWindowEnabled(handle) != FALSE,
                        handle == active_window, thread_id, klass, title, {}};
        EnumChildWindows(handle, [](HWND child, LPARAM child_parameter) -> BOOL {
            const int length = GetWindowTextLengthW(child);
            if (length <= 0) return TRUE;
            std::wstring text(static_cast<std::size_t>(length) + 1U, L'\0');
            GetWindowTextW(child, text.data(), length + 1);
            text.resize(static_cast<std::size_t>(length));
            reinterpret_cast<std::vector<std::wstring>*>(child_parameter)->push_back(std::move(text));
            return TRUE;
        }, reinterpret_cast<LPARAM>(&info.child_text));
        result->push_back(std::move(info));
        return TRUE;
    }, reinterpret_cast<LPARAM>(&windows));
    std::ostringstream output;
    output << "windows=" << windows.size();
    for (const auto& item : windows) {
        output << ";0x" << std::hex << reinterpret_cast<std::uintptr_t>(item.handle) << std::dec
               << ",visible=" << (item.visible ? 1 : 0)
               << ",enabled=" << (item.enabled ? 1 : 0)
               << ",foreground=" << (item.foreground ? 1 : 0)
               << ",thread=" << item.thread_id
               << ",class=" << narrow(item.klass)
               << ",title=" << narrow(item.title);
        for (const auto& text : item.child_text) output << ",child=" << narrow(text);
    }
    return output.str();
}

std::string process_controls(HWND parent)
{
    struct ControlInfo {
        HWND handle{};
        int id{};
        bool visible{};
        bool enabled{};
        std::wstring klass{};
        std::wstring text{};
    };
    std::vector<ControlInfo> controls;
    EnumChildWindows(parent, [](HWND child, LPARAM parameter) -> BOOL {
        wchar_t klass[256]{};
        GetClassNameW(child, klass, static_cast<int>(std::size(klass)));
        const int length = GetWindowTextLengthW(child);
        std::wstring text(static_cast<std::size_t>(length > 0 ? length : 0) + 1U, L'\0');
        if (length > 0) GetWindowTextW(child, text.data(), length + 1);
        text.resize(static_cast<std::size_t>(length > 0 ? length : 0));
        reinterpret_cast<std::vector<ControlInfo>*>(parameter)->push_back(
            {child, GetDlgCtrlID(child), IsWindowVisible(child) != FALSE,
             IsWindowEnabled(child) != FALSE, klass, std::move(text)});
        return TRUE;
    }, reinterpret_cast<LPARAM>(&controls));
    std::ostringstream output;
    output << "controls=" << controls.size();
    for (const auto& item : controls) {
        output << ";0x" << std::hex << reinterpret_cast<std::uintptr_t>(item.handle) << std::dec
               << ",id=" << item.id
               << ",visible=" << (item.visible ? 1 : 0)
               << ",enabled=" << (item.enabled ? 1 : 0)
               << ",class=" << narrow(item.klass)
               << ",text=" << narrow(item.text);
    }
    return output.str();
}

std::filesystem::path capture_active_window(const std::filesystem::path& path,
                                            HWND requested_target = nullptr)
{
    const auto usable = [](HWND candidate) {
        if (!candidate || !IsWindowVisible(candidate)) return false;
        wchar_t klass[128]{};
        GetClassNameW(candidate, klass, static_cast<int>(std::size(klass)));
        // デバッグコンソールは、このプロセスが所有する別ウィンド
        // ウ。 診断情報では、ゲーム画面の代わりとして扱わない。
        return std::wcscmp(klass, L"ConsoleWindowClass") != 0;
    };
    HWND target = requested_target;
    if (!target) target = game_window();
    if (!requested_target && usable(target)) {
        const HWND popup = GetLastActivePopup(target);
        if (usable(popup)) target = popup;
    } else if (!requested_target) {
        target = nullptr;
    }
    if (!target) EnumWindows([](HWND candidate, LPARAM parameter) -> BOOL {
        DWORD process_id = 0;
        GetWindowThreadProcessId(candidate, &process_id);
        if (process_id != GetCurrentProcessId() || !IsWindowVisible(candidate)) return TRUE;
        wchar_t klass[128]{};
        GetClassNameW(candidate, klass, static_cast<int>(std::size(klass)));
        if (std::wcscmp(klass, L"ConsoleWindowClass") == 0) return TRUE;
        *reinterpret_cast<HWND*>(parameter) = candidate;
        return FALSE;
    }, reinterpret_cast<LPARAM>(&target));
    if (!target) throw std::runtime_error("CardWirthPy_C window is not ready");
    if (target == game_window()) {
        if (const auto frame = game_frame()) {
            std::vector<std::uint8_t> bgra;
            ::cw::lib::Integer width = 0;
            ::cw::lib::Integer height = 0;
            {
                std::scoped_lock lock(frame->m_present_mutex);
                width = frame->m_present_width;
                height = frame->m_present_height;
                bgra = frame->m_present_bgra;
            }
            const auto expected = width > 0 && height > 0
                ? static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) * 4ULL : 0ULL;
            if (expected > 0 && expected == bgra.size()) {
                ::cw::lib::imagecodec::RGBAImage image;
                image.width = width;
                image.height = height;
                image.pixels.resize(bgra.size());
                for (std::size_t offset = 0; offset < bgra.size(); offset += 4U) {
                    image.pixels[offset] = bgra[offset + 2U];
                    image.pixels[offset + 1U] = bgra[offset + 1U];
                    image.pixels[offset + 2U] = bgra[offset];
                    image.pixels[offset + 3U] = 255U;
                }
                const auto output = path.is_absolute() ? path : std::filesystem::current_path() / path;
                std::filesystem::create_directories(output.parent_path());
                ::cw::lib::imagecodec::encode_png_file(image, output);
                return output;
            }
        }
    }
    RECT rect{};
    if (!GetWindowRect(target, &rect)) throw std::runtime_error("GetWindowRect failed");
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    if (width <= 0 || height <= 0) throw std::runtime_error("Window has an invalid size");
    HDC screen = GetDC(nullptr);
    HDC memory = CreateCompatibleDC(screen);
    HBITMAP dib = CreateCompatibleBitmap(screen, width, height);
    HGDIOBJ old = dib ? SelectObject(memory, dib) : nullptr;
    const bool printed = dib && PrintWindow(target, memory, PW_RENDERFULLCONTENT) != FALSE;
    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    std::vector<std::uint8_t> bgra(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U);
    const bool copied = printed && GetDIBits(memory, dib, 0, static_cast<UINT>(height),
                                             bgra.data(), &bmi, DIB_RGB_COLORS) != 0;
    if (old) SelectObject(memory, old);
    if (dib) DeleteObject(dib);
    DeleteDC(memory);
    ReleaseDC(nullptr, screen);
    if (!copied) throw std::runtime_error("PrintWindow failed");
    ::cw::lib::imagecodec::RGBAImage image;
    image.width = width;
    image.height = height;
    image.pixels.resize(bgra.size());
    for (std::size_t offset = 0; offset < bgra.size(); offset += 4U) {
        image.pixels[offset] = bgra[offset + 2U];
        image.pixels[offset + 1U] = bgra[offset + 1U];
        image.pixels[offset + 2U] = bgra[offset];
        image.pixels[offset + 3U] = 255U;
    }
    const auto output = path.is_absolute() ? path : std::filesystem::current_path() / path;
    std::filesystem::create_directories(output.parent_path());
    ::cw::lib::imagecodec::encode_png_file(image, output);
    return output;
}

std::string current_stacktrace()
{
    static std::mutex symbols_mutex;
    std::scoped_lock lock(symbols_mutex);
    HANDLE process = GetCurrentProcess();
    SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME | SYMOPT_LOAD_LINES);
    SymInitialize(process, nullptr, TRUE);
    void* frames[64]{};
    const USHORT count = CaptureStackBackTrace(1, static_cast<DWORD>(std::size(frames)), frames, nullptr);
    std::vector<unsigned char> storage(sizeof(SYMBOL_INFO) + MAX_SYM_NAME);
    auto* symbol = reinterpret_cast<SYMBOL_INFO*>(storage.data());
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen = MAX_SYM_NAME;
    std::ostringstream output;
    for (USHORT index = 0; index < count; ++index) {
        const auto address = reinterpret_cast<DWORD64>(frames[index]);
        DWORD64 displacement = 0;
        output << "#" << index << " 0x" << std::hex << address << std::dec;
        if (SymFromAddr(process, address, &displacement, symbol))
            output << " " << symbol->Name << "+0x" << std::hex << displacement << std::dec;
        IMAGEHLP_LINE64 line{};
        line.SizeOfStruct = sizeof(line);
        DWORD line_displacement = 0;
        if (SymGetLineFromAddr64(process, address, &line_displacement, &line))
            output << " (" << line.FileName << ":" << line.LineNumber << ")";
        output << '\n';
    }
    return output.str();
}

std::filesystem::path write_minidump()
{
    std::filesystem::path directory;
    {
        std::scoped_lock lock(g_log_mutex);
        directory = g_log_path.parent_path();
    }
    if (directory.empty()) directory = std::filesystem::current_path();
    const auto path = directory / "CardWirthPy_C_debug.dmp";
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) throw std::runtime_error("Could not create debug dump");
    const BOOL written = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file,
                                           MiniDumpWithThreadInfo, nullptr, nullptr, nullptr);
    const DWORD error = written ? ERROR_SUCCESS : GetLastError();
    CloseHandle(file);
    if (!written) throw std::runtime_error("MiniDumpWriteDump failed: " + std::to_string(error));
    return path;
}

void require_post(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (!PostMessageW(window, message, wparam, lparam))
        throw std::runtime_error("PostMessage failed");
}

template<typename T>
std::shared_ptr<T> object_value(const ::cw::lib::Value& value)
{
    const auto* object = std::get_if<::cw::lib::ObjectPtr>(&value.storage());
    return object && *object ? std::dynamic_pointer_cast<T>(*object) : nullptr;
}

std::string command_argument(const std::string& command)
{
    const auto separator = command.find_first_of(" \t");
    if (separator == std::string::npos) return {};
    auto argument = command.substr(separator + 1);
    while (!argument.empty() && (argument.front() == ' ' || argument.front() == '\t'))
        argument.erase(argument.begin());
    return argument;
}

bool equals_ascii_insensitive(const ::cw::lib::String& lhs, const std::string& rhs)
{
    if (lhs.size() != rhs.size()) return false;
    for (std::size_t index = 0; index < lhs.size(); ++index) {
        if (std::tolower(static_cast<unsigned char>(lhs[index])) !=
            std::tolower(static_cast<unsigned char>(rhs[index])))
            return false;
    }
    return true;
}

bool menu_card_visible(const ::cw::sprite::card::MenuCard& card)
{
    return card.m_status != "hidden" && card.m_rect.width > 0 && card.m_rect.height > 0;
}

bool menu_card_matches(const ::cw::sprite::card::MenuCard& card, const std::string& query)
{
    if (query.size() >= 4 && query.compare(0, 4, "arg:") == 0)
        return equals_ascii_insensitive(card.m_arg, query.substr(4));
    if (card.m_name == query) return true;
    return card.m__name == query;
}

std::shared_ptr<::cw::sprite::card::MenuCard> find_menu_card(const std::string& query)
{
    ::cw::lib::RuntimeAccess runtime_access;
    if (!::cw::g_cwpy) throw std::runtime_error("Game state is not ready");
    if (query.empty()) throw std::invalid_argument("Menu card name is required");
    std::shared_ptr<::cw::sprite::card::MenuCard> match;
    for (const auto& value : ::cw::g_cwpy->m_mcards) {
        const auto card = object_value<::cw::sprite::card::MenuCard>(value);
        if (!card || !menu_card_visible(*card) || !menu_card_matches(*card, query)) continue;
        if (match) throw std::runtime_error("Multiple menu cards match: " + query);
        match = card;
    }
    if (!match) throw std::runtime_error("Menu card not found: " + query);
    return match;
}

std::pair<int, int> menu_card_click_position(const ::cw::sprite::card::MenuCard& card)
{
    return {card.m_rect.x + card.m_rect.width / 2, card.m_rect.y + card.m_rect.height / 2};
}

bool is_game_frame(HWND window)
{
    wchar_t klass[64]{};
    return window && GetClassNameW(window, klass, static_cast<int>(std::size(klass))) > 0 &&
           std::wcscmp(klass, L"CardWirthSDLFrame") == 0;
}

void queue_pointer_motion(HWND window, LPARAM position)
{
    if (nonactive_automation_enabled() && is_game_frame(window))
        require_post(window, nonactive_pointer_input_message,
                     static_cast<WPARAM>(NonactivePointerInput::motion), position);
    else
        require_post(window, WM_MOUSEMOVE, 0, position);
}

void queue_pointer_button(HWND window, NonactivePointerInput synthetic,
                          UINT native, WPARAM state, LPARAM position)
{
    if (nonactive_automation_enabled() && is_game_frame(window))
        require_post(window, nonactive_pointer_input_message,
                     static_cast<WPARAM>(synthetic), position);
    else
        require_post(window, native, state, position);
}

void queue_click(HWND window, int x, int y, const std::string& button = "LEFT")
{
    std::string normalized = button;
    for (char& c : normalized) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    UINT down = WM_LBUTTONDOWN;
    UINT up = WM_LBUTTONUP;
    WPARAM state = MK_LBUTTON;
    NonactivePointerInput synthetic_down = NonactivePointerInput::left_down;
    NonactivePointerInput synthetic_up = NonactivePointerInput::left_up;
    if (normalized == "RIGHT") {
        down = WM_RBUTTONDOWN; up = WM_RBUTTONUP; state = MK_RBUTTON;
        synthetic_down = NonactivePointerInput::right_down;
        synthetic_up = NonactivePointerInput::right_up;
    }
    else if (normalized == "MIDDLE") {
        down = WM_MBUTTONDOWN; up = WM_MBUTTONUP; state = MK_MBUTTON;
        synthetic_down = NonactivePointerInput::middle_down;
        synthetic_up = NonactivePointerInput::middle_up;
    }
    else if (normalized != "LEFT") throw std::invalid_argument("Unknown mouse button: " + button);
    // 自動テストでユーザーの最前面ウィンドウを奪わない。 SDL/Win32
    // では同じクライアントメッセージを直接送ればよく、 実際
    // のマウス位置やキーボードフォーカスも変更せずに済む。
    const LPARAM position = MAKELPARAM(x, y);
    queue_pointer_motion(window, position);
    queue_pointer_button(window, synthetic_down, down, state, position);
    queue_pointer_button(window, synthetic_up, up, 0, position);
}

void queue_button_transition(HWND window, int x, int y, const std::string& button,
                             bool pressed)
{
    std::string normalized = button;
    for (char& c : normalized) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    UINT down = WM_LBUTTONDOWN;
    UINT up = WM_LBUTTONUP;
    WPARAM state = MK_LBUTTON;
    NonactivePointerInput synthetic_down = NonactivePointerInput::left_down;
    NonactivePointerInput synthetic_up = NonactivePointerInput::left_up;
    if (normalized == "RIGHT") {
        down = WM_RBUTTONDOWN; up = WM_RBUTTONUP; state = MK_RBUTTON;
        synthetic_down = NonactivePointerInput::right_down;
        synthetic_up = NonactivePointerInput::right_up;
    }
    else if (normalized == "MIDDLE") {
        down = WM_MBUTTONDOWN; up = WM_MBUTTONUP; state = MK_MBUTTON;
        synthetic_down = NonactivePointerInput::middle_down;
        synthetic_up = NonactivePointerInput::middle_up;
    }
    else if (normalized != "LEFT") throw std::invalid_argument("Unknown mouse button: " + button);
    const LPARAM position = MAKELPARAM(x, y);
    queue_pointer_motion(window, position);
    queue_pointer_button(window,
                         pressed ? synthetic_down : synthetic_up,
                         pressed ? down : up,
                         pressed ? state : 0,
                         position);
}

void queue_key(HWND target, WPARAM key, bool key_down, bool key_up)
{
    if (key_down) require_post(target, WM_KEYDOWN, key, 0);
    if (key_up) require_post(target, WM_KEYUP, key, 0);
}

void queue_text(HWND target, const std::wstring& value)
{
    for (const wchar_t character : value) require_post(target, WM_CHAR, character, 0);
}

void queue_control_click(HWND control)
{
    if (!IsWindowEnabled(control)) throw std::runtime_error("Control is disabled");
    wchar_t klass[64]{};
    GetClassNameW(control, klass, static_cast<int>(std::size(klass)));
    const auto button_type = static_cast<DWORD>(GetWindowLongPtrW(control, GWL_STYLE)) & BS_TYPEMASK;
    const bool pushbutton = std::wcscmp(klass, L"Button") == 0 &&
        (button_type == BS_PUSHBUTTON || button_type == BS_DEFPUSHBUTTON ||
         button_type == BS_OWNERDRAW);
    if (pushbutton && nonactive_automation_enabled()) {
        // BM_CLICKはボタンへキーボードフォーカスを移し、WS_EX_NOACTIVATE
        // があっても 最上位の親ウィンドウをアクティブ化する場合がある。
        // BN_CLICKEDなら、フォーカスを変える押下アニメーショ
        // ンを伴わずに、 実際のダイアログ処理へ操作通知を届けられる。
        const HWND parent = GetParent(control);
        if (!parent) throw std::runtime_error("Button has no parent window");
        DWORD_PTR result = 0;
        if (!SendMessageTimeoutW(parent, WM_COMMAND,
                                 MAKEWPARAM(GetDlgCtrlID(control), BN_CLICKED),
                                 reinterpret_cast<LPARAM>(control),
                                 SMTO_ABORTIFHUNG | SMTO_BLOCK, 2000, &result))
            throw std::runtime_error("Button notification timed out");
    } else if (pushbutton) {
        // BM_CLICKは、アプリをアクティブ化せずにWin32
        // の通常のボタン処理 （押下表示、解放、BN_CLICKED
        // ）を通す。共通のダイアログ処理も省略しない。
        DWORD_PTR result = 0;
        if (!SendMessageTimeoutW(control, BM_CLICK, 0, 0,
                                 SMTO_ABORTIFHUNG | SMTO_BLOCK, 2000, &result))
            throw std::runtime_error("Button click timed out");
    } else {
        DWORD_PTR result = 0;
        if (!SendMessageTimeoutW(control, BM_CLICK, 0, 0,
                                 SMTO_ABORTIFHUNG | SMTO_BLOCK, 2000, &result))
            throw std::runtime_error("Control click timed out");
    }
}

void queue_control_click_async(HWND control)
{
    if (!IsWindowEnabled(control)) throw std::runtime_error("Control is disabled");
    // モーダルダイアログを同期的に開くと、閉じるまで処理が戻らない。 BM_CLICK
    // をポストして制御サーバーをすぐに応答可能にし、別の接続から 入れ
    // 子のウィンドウをアクティブ化せずに調査・終了できるようにする。
    wchar_t klass[64]{};
    GetClassNameW(control, klass, static_cast<int>(std::size(klass)));
    const auto button_type = static_cast<DWORD>(GetWindowLongPtrW(control, GWL_STYLE)) & BS_TYPEMASK;
    const bool pushbutton = std::wcscmp(klass, L"Button") == 0 &&
        (button_type == BS_PUSHBUTTON || button_type == BS_DEFPUSHBUTTON ||
         button_type == BS_OWNERDRAW);
    if (pushbutton && nonactive_automation_enabled()) {
        const HWND parent = GetParent(control);
        if (!parent) throw std::runtime_error("Button has no parent window");
        require_post(parent, WM_COMMAND,
                     MAKEWPARAM(GetDlgCtrlID(control), BN_CLICKED),
                     reinterpret_cast<LPARAM>(control));
    } else {
        require_post(control, BM_CLICK, 0, 0);
    }
}

void set_control_text(HWND control, const std::wstring& value)
{
    DWORD_PTR result = 0;
    if (!SendMessageTimeoutW(control, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(value.c_str()),
                             SMTO_ABORTIFHUNG | SMTO_BLOCK, 2000, &result))
        throw std::runtime_error("WM_SETTEXT timed out");
    const HWND parent = GetParent(control);
    if (parent) {
        const int control_id = GetDlgCtrlID(control);
        // 独自の実装では、WM_SETTEXTでEN_CHANGEが通知され
        // るとは限らない。 ネイティブの編集操作と同じ通知を明示的に送る。
        DWORD_PTR notification_result = 0;
        if (!SendMessageTimeoutW(parent, WM_COMMAND, MAKEWPARAM(control_id, EN_CHANGE),
                                 reinterpret_cast<LPARAM>(control),
                                 SMTO_ABORTIFHUNG | SMTO_BLOCK, 2000,
                                 &notification_result))
            throw std::runtime_error("EN_CHANGE notification timed out");
    }
    const int length = GetWindowTextLengthW(control);
    std::wstring actual(static_cast<std::size_t>(length > 0 ? length : 0) + 1U, L'\0');
    if (length > 0) GetWindowTextW(control, actual.data(), length + 1);
    actual.resize(static_cast<std::size_t>(length > 0 ? length : 0));
    if (actual != value)
        throw std::runtime_error("Control text was overwritten during EN_CHANGE");
}

std::string process_menu_cards()
{
    ::cw::lib::RuntimeAccess runtime_access;
    if (!::cw::g_cwpy) throw std::runtime_error("Game state is not ready");
    std::ostringstream output;
    int count = 0;
    for (const auto& value : ::cw::g_cwpy->m_mcards) {
        const auto card = object_value<::cw::sprite::card::MenuCard>(value);
        if (!card || !menu_card_visible(*card)) continue;
        const auto [x, y] = menu_card_click_position(*card);
        if (count > 0) output << ';';
        output << card->get_showingname() << '@' << x << ',' << y;
        if (!card->m_arg.empty()) output << ",arg=" << card->m_arg;
        ++count;
    }
    return "menucards=" + std::to_string(count) + (count ? ";" : "") + output.str();
}

std::string dispatch_command(const std::string& command)
{
    const auto words = split_words(command);
    if (words.empty()) throw std::invalid_argument("Empty command");
    std::string operation = words.front();
    for (char& c : operation) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (operation == "PING") return "PONG";
    if (operation == "LOGTAIL") {
        if (!nonactive_automation_enabled())
            throw std::runtime_error("LOGTAIL requires --nonactive-debug");
        std::scoped_lock lock(g_log_mutex);
        constexpr std::size_t k_tail_bytes = 16000;
        const auto offset = g_mounted_log_buffer.size() > k_tail_bytes
            ? g_mounted_log_buffer.size() - k_tail_bytes : 0;
        std::string result = g_mounted_log_buffer.substr(offset);
        std::string escaped;
        escaped.reserve(result.size());
        for (const char value : result) {
            if (value == '\r') continue;
            if (value == '\n') escaped += "\\n";
            else escaped.push_back(value);
        }
        return escaped;
    }
    if (operation == "KEYSTATE") {
        ::cw::lib::RuntimeAccess runtime_access;
        if (!::cw::g_cwpy || !::cw::g_cwpy->m_keyevent)
            throw std::runtime_error("Game key state is not ready");
        const auto pressed = ::cw::g_cwpy->m_keyevent->get_pressed();
        std::ostringstream output;
        output << "keys=";
        bool any_key = false;
        for (std::size_t key = 0; key < pressed.size(); ++key) {
            if (pressed[key] <= 0) continue;
            if (any_key) output << ',';
            output << key << ':' << pressed[key];
            any_key = true;
        }
        if (!any_key) output << '-';
        return output.str();
    }
    if (operation == "WINDOWS") return process_windows();
    if (operation == "DEBUGLOGTEST") {
        ::cw::lib::RuntimeAccess runtime_access;
        if (!nonactive_automation_enabled())
            throw std::runtime_error("DEBUGLOGTEST requires --nonactive-debug");
        if (!::cw::g_cwpy) throw std::runtime_error("Game state is not ready");
        const auto runtime = ::cw::g_cwpy;
        const auto* object = std::get_if<::cw::lib::ObjectPtr>(&runtime->m_frame.storage());
        const auto frame = object
            ? std::dynamic_pointer_cast<::cw::frame::Frame>(*object) : nullptr;
        if (!frame) throw std::runtime_error("Frame is not ready");
        frame->exec_func_11([frame](const ::cw::lib::Arguments&,
                                   const ::cw::lib::KeywordArguments&) {
            auto result = std::make_shared<::cw::debug::logging::DebugLog>();
            result->initialize("nonactive debug-log close test");
            result->set_money(100, 100);
            auto parent = std::make_shared<::cw::lib::Window>();
            parent->native_handle = frame->m_native_window_handle;
            auto dialog = std::make_shared<::cw::debug::logging::DebugLogDialog>();
            dialog->initialize(parent, result);
            return ::cw::lib::Value{};
        }, {}, {});
        return "DEBUGLOGTEST queued";
    }
    if (operation == "DIALOGACCEPT") {
        ::cw::lib::RuntimeAccess runtime_access;
        if (!nonactive_automation_enabled())
            throw std::runtime_error("DIALOGACCEPT requires --nonactive-debug");
        if (!::cw::g_cwpy) throw std::runtime_error("Game state is not ready");
        const auto runtime = ::cw::g_cwpy;
        const auto* object = std::get_if<::cw::lib::ObjectPtr>(&runtime->m_frame.storage());
        const auto frame = object
            ? std::dynamic_pointer_cast<::cw::frame::Frame>(*object) : nullptr;
        if (!frame) throw std::runtime_error("Frame is not ready");
        // ゲーム側のワーカーがshow_common_modal_presentation()
        // で同期的に待っている場合、 exec_func_11
        // へ処理を積んでもMessageの確認操作まで到達しな
        // い。 Frameの同期付きアクセサーからモデルを取得
        // し、テスト用の表示を直接完了させる。 UIループがfinished()
        // を検知して、通常の終了処理を行う。
        const auto provider = std::dynamic_pointer_cast<
            ::cw::dialog::DialogPresentationProvider>(frame->dialog_presentation_model());
        if (!provider)
            throw std::runtime_error("Active dialog has no presentation provider");
        frame->post_func([frame, provider](const auto&, const auto&) {
            if (frame->dialog_presentation_model() ==
                std::dynamic_pointer_cast<::cw::lib::Object>(provider))
                provider->dialog_presentation_enter({});
            return ::cw::lib::Value{};
        });
        return "DIALOGACCEPT queued";
    }
    if (operation == "WINDOWSHOW") {
        if (words.size() != 2) throw std::invalid_argument("Usage: WINDOWSHOW window-class");
        const HWND window = require_process_window(words[1]);
        // 最小化されたテストウィンドウを、最前面のフォーカスを奪わずに復
        // 元する。 マウス・キーのポストには描画可能なクライアント領域
        // が必要だが、 ユーザーが操作中のアプリを妨げないようにする。
        ShowWindow(window, SW_SHOWNOACTIVATE);
        SetWindowPos(window, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        return "window shown without activation";
    }
    if (operation == "CAPTURE") {
        if (words.size() != 2 && words.size() != 3)
            throw std::invalid_argument("Usage: CAPTURE output.png [window-class]");
        const HWND target = words.size() == 3 ? require_process_window(words[2]) : nullptr;
        const auto path = capture_active_window(std::filesystem::u8path(words[1]), target);
        return "captured " + ::cw::lib::encoding::path_to_utf8(path);
    }
    if (operation == "STACK") {
        const auto trace = current_stacktrace();
        log("Current debug-server thread stack:\n" + trace);
        return "stack written to CardWirthPy_C_debug.log";
    }
    if (operation == "DUMP") {
        const auto path = write_minidump();
        log("Full-process minidump written: " + ::cw::lib::encoding::path_to_utf8(path));
        return "dump written to " + ::cw::lib::encoding::path_to_utf8(path);
    }
    if (operation == "YADO_OK") {
        const HWND dialog = FindWindowW(L"CardWirthSelectDialog", nullptr);
        if (!dialog) throw std::runtime_error("Yado selection window is not ready");
        const HWND button = GetDlgItem(dialog, 10109);
        if (!button) throw std::runtime_error("Yado selection OK button is not ready");
        queue_control_click(button);
        return "Yado OK clicked";
    }
    if (operation == "CONTROLS") {
        if (words.size() != 2) throw std::invalid_argument("Usage: CONTROLS window-class");
        return process_controls(require_process_window(words[1]));
    }
    if (operation == "WINDOWCOMMANDID") {
        if (words.size() != 3)
            throw std::invalid_argument("Usage: WINDOWCOMMANDID window-class command-id");
        const HWND window = require_process_window(words[1]);
        const int id = parse_integer(words[2]);
        require_post(window, WM_COMMAND, MAKEWPARAM(id, BN_CLICKED), 0);
        return "WINDOWCOMMANDID queued";
    }
    if (operation == "DEBUGGERCOMMANDID") {
        if (words.size() != 3)
            throw std::invalid_argument("Usage: DEBUGGERCOMMANDID window-class command-id");
        const HWND window = require_process_window(words[1]);
        require_post(window, nonactive_debugger_command_message,
                     static_cast<WPARAM>(parse_integer(words[2])), 0);
        return "DEBUGGERCOMMANDID queued";
    }
    if (operation == "LISTVIEWDBLCLICKID") {
        if (words.size() != 4)
            throw std::invalid_argument(
                "Usage: LISTVIEWDBLCLICKID window-class control-id row");
        const HWND window = require_process_window(words[1]);
        const HWND control = require_control_id(window, parse_integer(words[2]));
        wchar_t klass[64]{};
        GetClassNameW(control, klass, static_cast<int>(std::size(klass)));
        if (std::wcscmp(klass, WC_LISTVIEWW) != 0)
            throw std::invalid_argument("Target control is not a list view");
        const int row = parse_integer(words[3]);
        if (row < 0 || row >= ListView_GetItemCount(control))
            throw std::out_of_range("List-view row is out of range");
        ListView_SetItemState(control, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
        ListView_SetItemState(control, row, LVIS_SELECTED | LVIS_FOCUSED,
                              LVIS_SELECTED | LVIS_FOCUSED);
        require_post(window, nonactive_listview_double_click_message,
                     reinterpret_cast<WPARAM>(control), static_cast<LPARAM>(row));
        return "LISTVIEWDBLCLICKID queued";
    }
    if (operation == "CONTROLCLICK" || operation == "CONTROLCLICKID" ||
        operation == "CONTROLCLICKASYNC" || operation == "CONTROLCLICKASYNCID") {
        if (words.size() < 3)
            throw std::invalid_argument(
                "Usage: CONTROLCLICK[ASYNC] window-class control-text | "
                "CONTROLCLICK[ASYNC]ID window-class control-id");
        const HWND dialog = require_process_window(words[1]);
        HWND control = nullptr;
        const bool by_id = operation == "CONTROLCLICKID" || operation == "CONTROLCLICKASYNCID";
        const bool asynchronous = operation == "CONTROLCLICKASYNC" ||
                                  operation == "CONTROLCLICKASYNCID";
        if (by_id) {
            if (words.size() != 3)
                throw std::invalid_argument("Usage: CONTROLCLICK[ASYNC]ID window-class control-id");
            control = require_control_id(dialog, parse_integer(words[2]));
        } else {
            const auto class_offset = command.find(words[1]);
            const auto text_offset = command.find_first_not_of(" \t", class_offset + words[1].size());
            if (text_offset == std::string::npos)
                throw std::invalid_argument("Usage: CONTROLCLICK[ASYNC] window-class control-text");
            control = require_control_text(dialog, command.substr(text_offset));
        }
        if (asynchronous) queue_control_click_async(control);
        else queue_control_click(control);
        std::ostringstream result;
        result << operation << " queued hwnd=0x" << std::hex
               << reinterpret_cast<std::uintptr_t>(control);
        return result.str();
    }
    if (operation == "WINDOWKEY" || operation == "WINDOWKEYDOWN" || operation == "WINDOWKEYUP" ||
        operation == "CONTROLKEY" || operation == "CONTROLKEYDOWN" || operation == "CONTROLKEYUP") {
        const bool control_target = operation.compare(0, 7, "CONTROL") == 0;
        const std::size_t expected = control_target ? 4U : 3U;
        if (words.size() != expected)
            throw std::invalid_argument(control_target
                ? "Usage: CONTROLKEY window-class control-id virtual-key"
                : "Usage: WINDOWKEY window-class virtual-key");
        const HWND dialog = require_process_window(words[1]);
        const HWND target = control_target ? require_control_id(dialog, parse_integer(words[2])) : dialog;
        const bool key_down = operation.find("KEYUP") == std::string::npos;
        const bool key_up = operation.find("KEYDOWN") == std::string::npos;
        queue_key(target, parse_key(words[control_target ? 3U : 2U]), key_down, key_up);
        return operation + " queued";
    }
    if (operation == "CONTROLTEXT" || operation == "CONTROLTYPE") {
        if (words.size() < 4)
            throw std::invalid_argument("Usage: CONTROLTEXT window-class control-id utf8-text");
        const HWND dialog = require_process_window(words[1]);
        const HWND control = require_control_id(dialog, parse_integer(words[2]));
        const auto id_offset = command.find(words[2], command.find(words[1]) + words[1].size());
        const auto text_offset = command.find_first_not_of(" \t", id_offset + words[2].size());
        if (text_offset == std::string::npos)
            throw std::invalid_argument("Usage: CONTROLTEXT window-class control-id utf8-text");
        const auto value = wide(command.substr(text_offset));
        if (operation == "CONTROLTEXT") set_control_text(control, value);
        else queue_text(control, value);
        return operation + (operation == "CONTROLTEXT" ? " applied" : " queued");
    }
    if (operation == "MENUCARDS") return process_menu_cards();
    if (operation == "STARTSCENARIO") {
        ::cw::lib::RuntimeAccess runtime_access;
        if (!nonactive_automation_enabled())
            throw std::runtime_error("STARTSCENARIO requires --nonactive-debug");
        if (!::cw::g_cwpy || !::cw::g_cwpy->m_setting)
            throw std::runtime_error("Game state is not ready");
        if (::cw::g_cwpy->is_modal_input_blocked() || ::cw::g_cwpy->is_showingdlg())
            throw std::runtime_error("STARTSCENARIO is blocked by an active modal dialog");
        const auto path = command_argument(command);
        if (path.empty())
            throw std::invalid_argument("Usage: STARTSCENARIO scenario-directory");
        auto database = std::make_shared<::cw::scenariodb::Scenariodb>();
        const auto skin_type = ::cw::g_cwpy->m_setting->m_skintype;
        database->insert_scenario(path, true, skin_type);
        const auto header = database->get_header(path, skin_type);
        if (!header || !*header)
            throw std::runtime_error("Scenario header could not be created: " + path);
        const auto runtime = ::cw::g_cwpy;
        runtime->exec_func([runtime, selected = *header](const ::cw::lib::Arguments&,
                                                        const ::cw::lib::KeywordArguments&) {
            runtime->set_scenario(selected, std::nullopt, {}, false, true);
            return ::cw::lib::Value{};
        });
        return "STARTSCENARIO queued path=" + path;
    }

    const HWND window = game_window();
    if (!window) throw std::runtime_error("CardWirthPy_C window is not ready");

    if (operation == "CLICKMENU" || operation == "CLICKMENUCARD") {
        ::cw::lib::RuntimeAccess runtime_access;
        if (::cw::g_cwpy && (::cw::g_cwpy->is_modal_input_blocked() ||
                             ::cw::g_cwpy->is_showingdlg()))
            throw std::runtime_error("Menu-card input is blocked by an active modal dialog");
        const auto argument = command_argument(command);
        if (argument.empty()) throw std::invalid_argument("Usage: CLICKMENU menu-card-name [LEFT|RIGHT|MIDDLE]");
        std::string menu_name = argument;
        std::string button = "LEFT";
        static constexpr const char* k_buttons[] = {"LEFT", "RIGHT", "MIDDLE"};
        for (const char* candidate : k_buttons) {
            const auto suffix = std::string{" "} + candidate;
            if (argument.size() > suffix.size() &&
                argument.compare(argument.size() - suffix.size(), suffix.size(), suffix) == 0) {
                menu_name = argument.substr(0, argument.size() - suffix.size());
                button = candidate;
                break;
            }
        }
        const auto card = find_menu_card(menu_name);
        const auto [x, y] = menu_card_click_position(*card);
        if ((button == "LEFT" || button == "RIGHT") && ::cw::g_cwpy) {
            // メニューカードのテストでは、最前面や最小化状態のHWNDが疑似マウスメ
            // ッセージを 受け取れるかに依存せず、同じスプライト操作を直接呼び出す。
            // exec_funcによりCardWirthのUI・ゲームスレッドで実
            // 行し、 frame.cppのUIスレッドに関するアサーションも満たす。
            ::cw::g_cwpy->exec_func([card, button](const ::cw::lib::Arguments&,
                                                   const ::cw::lib::KeywordArguments&) {
                // コマンドの受理後から実行までにモ
                // ーダル画面が開く場合があるため、
                // ゲームスレッドでも再確認する。
                if (!::cw::g_cwpy || ::cw::g_cwpy->is_modal_input_blocked() ||
                    ::cw::g_cwpy->is_showingdlg()) return ::cw::lib::Value{};
                if (button == "RIGHT") card->rclick_event();
                else card->lclick_event();
                return ::cw::lib::Value{};
            });
            return "CLICKMENU action queued name=" + card->get_showingname();
        }
        queue_click(window, x, y, button);
        return "CLICKMENU queued name=" + card->get_showingname() + " x=" + std::to_string(x) +
               " y=" + std::to_string(y);
    }
    if (operation == "CLICK") {
        if (words.size() < 3 || words.size() > 4)
            throw std::invalid_argument("Usage: CLICK x y [LEFT|RIGHT|MIDDLE]");
        const int x = parse_integer(words[1]);
        const int y = parse_integer(words[2]);
        const std::string button = words.size() == 4 ? words[3] : "LEFT";
        queue_click(window, x, y, button);
        return "CLICK queued";
    }
    if (operation == "MOUSEDOWN" || operation == "MOUSEUP") {
        if (words.size() < 3 || words.size() > 4)
            throw std::invalid_argument("Usage: MOUSEDOWN|MOUSEUP x y [LEFT|RIGHT|MIDDLE]");
        const int x = parse_integer(words[1]);
        const int y = parse_integer(words[2]);
        const std::string button = words.size() == 4 ? words[3] : "LEFT";
        queue_button_transition(window, x, y, button, operation == "MOUSEDOWN");
        return operation + " queued";
    }
    if (operation == "WINDOWCLICK") {
        if (words.size() < 4 || words.size() > 5)
            throw std::invalid_argument("Usage: WINDOWCLICK window-class x y [LEFT|RIGHT|MIDDLE]");
        const HWND target = require_process_window(words[1]);
        queue_click(target, parse_integer(words[2]), parse_integer(words[3]),
                    words.size() == 5 ? words[4] : "LEFT");
        return "WINDOWCLICK queued";
    }
    if (operation == "MOUSEMOVE") {
        if (words.size() != 3) throw std::invalid_argument("Usage: MOUSEMOVE x y");
        queue_pointer_motion(window,
            MAKELPARAM(parse_integer(words[1]), parse_integer(words[2])));
        return "MOUSEMOVE queued";
    }
    if (operation == "WINDOWMOUSEMOVE") {
        if (words.size() != 4)
            throw std::invalid_argument("Usage: WINDOWMOUSEMOVE window-class x y");
        queue_pointer_motion(require_process_window(words[1]),
            MAKELPARAM(parse_integer(words[2]), parse_integer(words[3])));
        return "WINDOWMOUSEMOVE queued";
    }
    if (operation == "KEY" || operation == "KEYDOWN" || operation == "KEYUP") {
        if (words.size() != 2) throw std::invalid_argument("Usage: KEY|KEYDOWN|KEYUP virtual-key");
        const WPARAM key = parse_key(words[1]);
        queue_key(window, key, operation != "KEYUP", operation != "KEYDOWN");
        return operation + " queued";
    }
    if (operation == "TEXT") {
        const auto separator = command.find_first_of(" \t");
        if (separator == std::string::npos) throw std::invalid_argument("Usage: TEXT utf8-text");
        const auto text = wide(command.substr(separator + 1));
        queue_text(window, text);
        return "TEXT queued";
    }
    throw std::invalid_argument("Unknown command: " + operation);
}
#endif

}  // namespace

std::vector<std::string> debug_control_receive_channels(std::string_view hello_json)
{
    const auto field = hello_json.find("\"receive\"");
    if (field == std::string_view::npos) return {};
    auto offset = hello_json.find('[', field + 9);
    if (offset == std::string_view::npos) throw std::invalid_argument("JSON hello receive must be an array");
    ++offset;
    std::vector<std::string> result;
    for (;;) {
        while (offset < hello_json.size() && std::isspace(static_cast<unsigned char>(hello_json[offset]))) ++offset;
        if (offset >= hello_json.size()) throw std::invalid_argument("JSON hello receive array is unterminated");
        if (hello_json[offset] == ']') break;
        auto value = json_string_at(hello_json, offset);
        if (!value) throw std::invalid_argument("JSON hello receive entries must be strings");
        if (*value == "clickable_areas" && std::find(result.begin(), result.end(), *value) == result.end())
            result.push_back(*value);
        while (offset < hello_json.size() && std::isspace(static_cast<unsigned char>(hello_json[offset]))) ++offset;
        if (offset >= hello_json.size()) throw std::invalid_argument("JSON hello receive array is unterminated");
        if (hello_json[offset] == ']') break;
        if (hello_json[offset++] != ',') throw std::invalid_argument("JSON hello receive array is invalid");
    }
    return result;
}

std::string debug_control_hello_json(const std::vector<std::string>& receiving)
{
    std::ostringstream output;
    output << "{\"type\":\"hello\",\"protocol\":\"cardwirth-debug/1\","
              "\"available\":[\"clickable_areas\"],\"receiving\":[";
    for (std::size_t index = 0; index < receiving.size(); ++index) {
        if (index) output << ',';
        output << '"' << json_escape(receiving[index]) << '"';
    }
    output << "]}";
    return output.str();
}

DebugControlLaunchOptions debug_control_launch_options(
    const std::vector<std::string>& arguments, const bool debug_build)
{
    DebugControlLaunchOptions result;
    result.enabled = debug_build;
    for (std::size_t index = 0; index < arguments.size(); ++index) {
        const auto& argument = arguments[index];
        if (argument == "--communication-debug" || argument == "--nonactive-debug")
            result.enabled = true;
        std::string value;
        if (argument == "--communication-debug-port") {
            if (index + 1 >= arguments.size())
                throw std::invalid_argument("--communication-debug-port requires a value");
            value = arguments[++index];
        } else if (argument.starts_with("--communication-debug-port=")) {
            value = argument.substr(std::string("--communication-debug-port=").size());
        }
        if (value.empty()) continue;
        std::size_t consumed{};
        unsigned long parsed{};
        try { parsed = std::stoul(value, &consumed, 10); }
        catch (...) { throw std::invalid_argument("Invalid communication debug port: " + value); }
        if (consumed != value.size() || parsed == 0 || parsed > 65535)
            throw std::invalid_argument("Invalid communication debug port: " + value);
        result.port = static_cast<unsigned short>(parsed);
    }
    return result;
}

void initialize_logger(const std::filesystem::path& application_directory)
{
#if defined(CARDWIRTH_LOG_ENABLED)
    std::scoped_lock lock(g_log_mutex);
#if defined(__EMSCRIPTEN__)
    // ブラウザー版では、永続化されるUserDataへ診断ログを作らない。 起動
    // のたびにログが増え続けないよう、エラーはUIまたはコンソールへ出力する。
    g_log_path.clear();
    g_log_mounted = false;
    g_mounted_log_buffer.clear();
    g_log_bytes = 0;
    g_log_limit_reported = false;
    (void)application_directory;
    return;
#endif
#if defined(_DEBUG)
    g_log_path = application_directory / "CardWirthPy_C_debug.log";
#else
    g_log_path = application_directory / "CardWirthPy_C_release.log";
#endif
    g_log_mounted = ::cardwirth::platform::is_mounted_user_path(g_log_path);
    g_mounted_log_buffer.clear();
    if (!g_log_mounted)
        std::ofstream(g_log_path, std::ios::binary | std::ios::trunc);
    g_log_bytes = 0;
    g_log_limit_reported = false;
#if defined(_WIN32)
    if (!nonactive_automation_enabled()) initialize_log_console();
#endif
#else
    (void)application_directory;
#endif
}

void shutdown_logger()
{
#if defined(CARDWIRTH_LOG_ENABLED)
    std::scoped_lock lock(g_log_mutex);
#if defined(_WIN32)
    if (g_log_console_owned) {
        FreeConsole();
        g_log_console_owned = false;
    }
    g_log_console = nullptr;
#endif
    if (g_log_mounted && !g_log_path.empty()) {
        std::vector<std::byte> bytes(g_mounted_log_buffer.size());
        if (!bytes.empty())
            std::memcpy(bytes.data(), g_mounted_log_buffer.data(), bytes.size());
        (void)::cardwirth::platform::write_mounted_user_file(g_log_path, bytes, true);
    }
    g_mounted_log_buffer.clear();
    g_log_mounted = false;
#endif
}

void log(const std::string& message)
{
#if defined(CARDWIRTH_LOG_ENABLED)
    std::scoped_lock lock(g_log_mutex);
    if (g_log_path.empty()) return;
    if (g_log_bytes >= k_max_log_bytes) {
        if (!g_log_limit_reported) {
#if defined(_DEBUG)
            OutputDebugStringW(L"CardWirthPy_C_debug.log reached its 1 MiB limit; further file logging is suppressed.\n");
#endif
            g_log_limit_reported = true;
        }
        return;
    }
    const auto now = std::chrono::system_clock::now();
    const std::time_t value = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &value);
#else
    localtime_r(&value, &local);
#endif
    std::ostringstream line;
    line << std::put_time(&local, "%Y-%m-%d %H:%M:%S") << " ["
         << std::this_thread::get_id() << "] " << message << '\n';
    const auto text = line.str();
    const auto remaining = k_max_log_bytes - g_log_bytes;
    const auto write_size = std::min<std::uintmax_t>(remaining, text.size());
    if (g_log_mounted) {
        g_mounted_log_buffer.append(text.data(), static_cast<std::size_t>(write_size));
    } else {
        std::ofstream output(g_log_path, std::ios::binary | std::ios::app);
        output.write(text.data(), static_cast<std::streamsize>(write_size));
    }
    g_log_bytes += write_size;
#if defined(_WIN32)
    write_log_console(text.substr(0, static_cast<std::size_t>(write_size)));
#endif
#if defined(_DEBUG)
    const auto debugger_text = wide(message + "\n");
    OutputDebugStringW(debugger_text.c_str());
#endif
#else
    (void)message;
#endif
}

void log_slow(const std::string& key,
              std::chrono::steady_clock::duration elapsed,
              const std::string& details)
{
#if defined(CARDWIRTH_LOG_ENABLED)
    constexpr auto threshold = std::chrono::milliseconds(3);
    if (elapsed < threshold) return;

    const auto now = std::chrono::steady_clock::now();
    {
        std::scoped_lock lock(g_slow_log_mutex);
        const auto it = g_last_slow_logs.find(key);
        if (it != g_last_slow_logs.end() && now - it->second < std::chrono::seconds(1)) return;
        g_last_slow_logs[key] = now;
    }

    const auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
    log("Slow " + key + ": elapsed_us=" + std::to_string(elapsed_us) +
        (details.empty() ? std::string{} : " " + details));
#else
    (void)key;
    (void)elapsed;
    (void)details;
#endif
}

void log_current_stacktrace(const std::string& reason)
{
#if defined(_WIN32) && defined(_DEBUG)
    log(reason + "\n" + current_stacktrace());
#else
    (void)reason;
#endif
}

void log_crash_diagnostics(const std::string& reason)
{
#if defined(_WIN32) && defined(_DEBUG)
    // スタックとダンプは同じ箇所で生成するが、個々の診断処理
    // の失敗は許容する。 ログがロックされている場合などに、
    // 診断の失敗で致命的エラー処理へ再帰しないようにする。
    try {
        log(reason);
        log_current_stacktrace(reason + " stack");
    } catch (...) {
        OutputDebugStringW(L"CardWirthPy_C crash diagnostics: stack logging failed\n");
    }
    try {
        const auto dump = write_minidump();
        log("Crash minidump: " + ::cw::lib::encoding::path_to_utf8(dump));
    } catch (const std::exception& error) {
        log(std::string("Crash minidump failed: ") + error.what());
    } catch (...) {
        log("Crash minidump failed: unknown error");
    }
#else
    (void)reason;
#endif
}

void install_crash_handlers()
{
#if defined(_WIN32) && defined(_DEBUG)
    // Debug CRTはSIGABRTがハンドラーへ届く前にabort()
    // のモーダル警告を表示する。 SIGABRTの通知を維持したま
    // ま警告画面だけを無効にし、ネイティブの診断記録を確実に残す。
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    _set_purecall_handler([] {
        log_crash_diagnostics("Pure virtual function call");
        std::_Exit(3);
    });
    _set_invalid_parameter_handler([](const wchar_t* expression,
                                      const wchar_t* function,
                                      const wchar_t* file,
                                      unsigned line,
                                      uintptr_t) {
        std::ostringstream reason;
        reason << "Invalid parameter";
        if (expression) reason << " expression=" << narrow(expression);
        if (function) reason << " function=" << narrow(function);
        if (file) reason << " file=" << narrow(file);
        reason << " line=" << line;
        log_crash_diagnostics(reason.str());
        std::_Exit(3);
    });
#endif
}

class DebugControlServer::Impl {
public:
#if defined(_WIN32)
    struct ClientWorker {
        std::thread thread{};
        std::shared_ptr<std::atomic_bool> done{};
    };
    std::atomic_bool stopping{false};
    SOCKET listener{INVALID_SOCKET};
    std::thread worker{};
    std::vector<ClientWorker> clients{};
    bool winsock_started{};
    bool owns_nonactive_mode{};
    std::atomic_uint subscribers{};
    unsigned short bound_port{};
    std::mutex snapshot_mutex{};
    std::string snapshot_json{};
    std::uint64_t snapshot_generation{};
#endif
};

namespace {
std::atomic<DebugControlServer*> g_active_debug_server{};

#if defined(_WIN32)
bool send_all(const SOCKET socket, std::string_view value)
{
    std::size_t offset = 0;
    while (offset < value.size()) {
        const int sent = send(socket, value.data() + offset,
                              static_cast<int>((std::min)(value.size() - offset,
                                                          static_cast<std::size_t>(INT_MAX))), 0);
        if (sent <= 0) return false;
        offset += static_cast<std::size_t>(sent);
    }
    return true;
}

std::string json_response(const bool ok, std::string_view result)
{
    return std::string{"{\"type\":\"response\",\"ok\":"} + (ok ? "true" : "false") +
        (ok ? ",\"result\":\"" : ",\"error\":\"") + json_escape(result) + "\"}\n";
}

std::string dispatch_json_command(std::string_view request)
{
    const auto command = json_string_field(request, "command");
    if (!command || command->empty()) throw std::invalid_argument("JSON command requires command string");
    if (*command == "PING" || *command == "ping") return "PONG";
    return dispatch_command(*command);
}
#endif
}

DebugControlServer::DebugControlServer() : m_impl(std::make_unique<Impl>()) {}
DebugControlServer::~DebugControlServer() { stop(); }

void DebugControlServer::start(unsigned short port, bool nonactive_automation)
{
#if defined(_WIN32)
    if (m_impl->worker.joinable()) return;
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) throw std::runtime_error("WSAStartup failed");
    m_impl->winsock_started = true;
    m_impl->listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (m_impl->listener == INVALID_SOCKET) throw std::runtime_error("Debug server socket creation failed");
    BOOL exclusive = TRUE;
    setsockopt(m_impl->listener, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
               reinterpret_cast<const char*>(&exclusive), sizeof(exclusive));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(m_impl->listener, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR ||
        listen(m_impl->listener, 4) == SOCKET_ERROR)
        throw std::runtime_error("Debug server could not listen on 127.0.0.1:" + std::to_string(port));
    sockaddr_in bound{};
    int bound_size = sizeof(bound);
    if (getsockname(m_impl->listener, reinterpret_cast<sockaddr*>(&bound), &bound_size) == SOCKET_ERROR)
        throw std::runtime_error("Debug server could not read its bound port");
    m_impl->bound_port = ntohs(bound.sin_port);
    m_impl->stopping = false;
    m_impl->owns_nonactive_mode = nonactive_automation;
    g_nonactive_automation.store(nonactive_automation, std::memory_order_release);
    g_active_debug_server.store(this, std::memory_order_release);
    log("Debug input server listening on 127.0.0.1:" + std::to_string(m_impl->bound_port));
    m_impl->worker = std::thread([impl = m_impl.get()] {
        while (!impl->stopping) {
            const SOCKET client = accept(impl->listener, nullptr, nullptr);
            if (client == INVALID_SOCKET) {
                if (impl->stopping) break;
                continue;
            }
            // stop()でjoinされるまで、この配列はaccept
            // スレッドだけが操作する。 完了した接続をここで回
            // 収し、長時間動作や再接続で過去の接続数だけ ネイ
            // ティブのスレッドハンドルが残らないようにする。
            impl->clients.erase(std::remove_if(impl->clients.begin(), impl->clients.end(),
                [](Impl::ClientWorker& worker) {
                    if (!worker.done || !worker.done->load(std::memory_order_acquire)) return false;
                    if (worker.thread.joinable()) worker.thread.join();
                    return true;
                }), impl->clients.end());
            auto done = std::make_shared<std::atomic_bool>(false);
            impl->clients.push_back({std::thread([impl, client, done] {
                struct Completion final {
                    std::shared_ptr<std::atomic_bool> value;
                    ~Completion() { value->store(true, std::memory_order_release); }
                } completion{done};
                DWORD timeout = 50;
                setsockopt(client, SOL_SOCKET, SO_RCVTIMEO,
                           reinterpret_cast<const char*>(&timeout), sizeof(timeout));
                std::string input;
                bool json_session = false;
                bool subscribed = false;
                std::uint64_t sent_generation = 0;
                const auto publish_latest = [&] {
                    if (!subscribed) return true;
                    std::string snapshot;
                    std::uint64_t generation{};
                    {
                        std::scoped_lock lock(impl->snapshot_mutex);
                        generation = impl->snapshot_generation;
                        if (generation > sent_generation) snapshot = impl->snapshot_json;
                    }
                    if (snapshot.empty()) return true;
                    if (!send_all(client, snapshot + "\n")) return false;
                    sent_generation = generation;
                    return true;
                };
                while (!impl->stopping) {
                    char buffer[2048];
                    const int count = recv(client, buffer, static_cast<int>(sizeof(buffer)), 0);
                    if (count == 0) break;
                    if (count == SOCKET_ERROR) {
                        const int error = WSAGetLastError();
                        if (error != WSAETIMEDOUT && error != WSAEWOULDBLOCK) break;
                    } else {
                        input.append(buffer, buffer + count);
                    }
                    for (;;) {
                        const auto newline = input.find('\n');
                        if (newline == std::string::npos) break;
                        std::string request = input.substr(0, newline);
                        input.erase(0, newline + 1);
                        if (!request.empty() && request.back() == '\r') request.pop_back();
                        if (request.empty()) continue;
                        if (!json_session && request.front() != '{') {
                            std::string response;
                            try { response = "OK " + dispatch_command(request) + "\n"; }
                            catch (const std::exception& error) { response = std::string("ERR ") + error.what() + "\n"; }
                            send_all(client, response);
                            shutdown(client, SD_BOTH);
                            closesocket(client);
                            return;
                        }
                        try {
                            const auto type = json_string_field(request, "type");
                            if (!json_session) {
                                if (!type || *type != "hello") throw std::invalid_argument("First JSON packet must be hello");
                                const auto channels = debug_control_receive_channels(request);
                                subscribed = std::find(channels.begin(), channels.end(), "clickable_areas") != channels.end();
                                if (subscribed) {
                                    std::scoped_lock lock(impl->snapshot_mutex);
                                    const auto previous = impl->subscribers.fetch_add(1, std::memory_order_acq_rel);
                                    if (previous == 0) {
                                        // 購読者がいない間はUI情報が更新されないため
                                        // 、古いキャッシュを 新規接続の初期状態として
                                        // 送らず、次のUIスレッドからの通知を待つ。
                                        impl->snapshot_json.clear();
                                        ++impl->snapshot_generation;
                                    }
                                }
                                json_session = true;
                                if (!send_all(client, debug_control_hello_json(channels) + "\n")) break;
                            } else if (type && *type == "command") {
                                try { if (!send_all(client, json_response(true, dispatch_json_command(request)))) break; }
                                catch (const std::exception& error) { if (!send_all(client, json_response(false, error.what()))) break; }
                            } else if (type && *type == "subscribe") {
                                const auto channels = debug_control_receive_channels(request);
                                const bool next = std::find(channels.begin(), channels.end(), "clickable_areas") != channels.end();
                                if (next != subscribed) {
                                    if (next) {
                                        std::scoped_lock lock(impl->snapshot_mutex);
                                        const auto previous = impl->subscribers.fetch_add(1, std::memory_order_acq_rel);
                                        if (previous == 0) {
                                            impl->snapshot_json.clear();
                                            ++impl->snapshot_generation;
                                        }
                                    } else impl->subscribers.fetch_sub(1, std::memory_order_acq_rel);
                                    subscribed = next;
                                }
                                if (!send_all(client, debug_control_hello_json(channels) + "\n")) break;
                            } else {
                                if (!send_all(client, json_response(false, "Unsupported JSON packet type"))) break;
                            }
                        } catch (const std::exception& error) {
                            if (!send_all(client, json_response(false, error.what()))) break;
                        }
                    }
                    if (!publish_latest()) break;
                }
                if (subscribed) impl->subscribers.fetch_sub(1, std::memory_order_acq_rel);
                shutdown(client, SD_BOTH);
                closesocket(client);
            }), std::move(done)});
        }
    });
#else
    (void)port;
    (void)nonactive_automation;
#endif
}

void DebugControlServer::stop()
{
#if defined(_WIN32)
    if (!m_impl) return;
    m_impl->stopping = true;
    DebugControlServer* expected = this;
    g_active_debug_server.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel);
    if (m_impl->listener != INVALID_SOCKET) {
        shutdown(m_impl->listener, SD_BOTH);
        closesocket(m_impl->listener);
        m_impl->listener = INVALID_SOCKET;
    }
    if (m_impl->worker.joinable()) m_impl->worker.join();
    for (auto& client : m_impl->clients) if (client.thread.joinable()) client.thread.join();
    m_impl->clients.clear();
    if (m_impl->winsock_started) {
        WSACleanup();
        m_impl->winsock_started = false;
    }
    if (m_impl->owns_nonactive_mode) {
        g_nonactive_automation.store(false, std::memory_order_release);
        m_impl->owns_nonactive_mode = false;
    }
#endif
}

unsigned short DebugControlServer::port() const noexcept
{
#if defined(_WIN32)
    return m_impl ? m_impl->bound_port : 0;
#else
    return 0;
#endif
}

void DebugControlServer::publish_clickable_areas(std::string json)
{
#if defined(_WIN32)
    if (!m_impl || json.empty()) return;
    std::scoped_lock lock(m_impl->snapshot_mutex);
    if (m_impl->snapshot_json == json) return;
    m_impl->snapshot_json = std::move(json);
    ++m_impl->snapshot_generation;
#else
    (void)json;
#endif
}

bool DebugControlServer::wants_clickable_areas() const noexcept
{
#if defined(_WIN32)
    return m_impl && m_impl->subscribers.load(std::memory_order_acquire) != 0;
#else
    return false;
#endif
}

class DebugControlClient::Impl {
public:
#if defined(_WIN32)
    SOCKET socket{INVALID_SOCKET};
    bool winsock_started{};
    std::string input{};
#endif
};

DebugControlClient::DebugControlClient() : m_impl(std::make_unique<Impl>()) {}
DebugControlClient::~DebugControlClient() { close(); }

void DebugControlClient::connect(const unsigned short port,
                                 std::vector<std::string> receive)
{
#if defined(_WIN32)
    close();
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) throw std::runtime_error("WSAStartup failed");
    m_impl->winsock_started = true;
    m_impl->socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (m_impl->socket == INVALID_SOCKET) throw std::runtime_error("Debug client socket creation failed");
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::connect(m_impl->socket, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR)
        throw std::runtime_error("Debug client could not connect to 127.0.0.1:" + std::to_string(port));
    std::ostringstream hello;
    hello << "{\"type\":\"hello\",\"receive\":[";
    for (std::size_t index = 0; index < receive.size(); ++index) {
        if (index) hello << ',';
        hello << '"' << json_escape(receive[index]) << '"';
    }
    hello << "]}\n";
    if (!send_all(m_impl->socket, hello.str())) throw std::runtime_error("Debug client hello send failed");
    const auto response = receive_json(std::chrono::seconds(2));
    if (!json_string_field(response, "type") || *json_string_field(response, "type") != "hello")
        throw std::runtime_error("Debug server did not return a hello packet");
#else
    (void)port; (void)receive;
    throw std::runtime_error("Debug control sockets are not available on this platform build");
#endif
}

void DebugControlClient::close()
{
#if defined(_WIN32)
    if (!m_impl) return;
    if (m_impl->socket != INVALID_SOCKET) {
        shutdown(m_impl->socket, SD_BOTH);
        closesocket(m_impl->socket);
        m_impl->socket = INVALID_SOCKET;
    }
    m_impl->input.clear();
    if (m_impl->winsock_started) { WSACleanup(); m_impl->winsock_started = false; }
#endif
}

void DebugControlClient::send_command(std::string_view command)
{
#if defined(_WIN32)
    if (!m_impl || m_impl->socket == INVALID_SOCKET) throw std::runtime_error("Debug client is not connected");
    const std::string packet = "{\"type\":\"command\",\"command\":\"" +
        json_escape(command) + "\"}\n";
    if (!send_all(m_impl->socket, packet)) throw std::runtime_error("Debug client command send failed");
#else
    (void)command;
    throw std::runtime_error("Debug control sockets are not available on this platform build");
#endif
}

std::string DebugControlClient::receive_json(const std::chrono::milliseconds timeout)
{
#if defined(_WIN32)
    if (!m_impl || m_impl->socket == INVALID_SOCKET) throw std::runtime_error("Debug client is not connected");
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    for (;;) {
        const auto newline = m_impl->input.find('\n');
        if (newline != std::string::npos) {
            auto result = m_impl->input.substr(0, newline);
            m_impl->input.erase(0, newline + 1);
            if (!result.empty() && result.back() == '\r') result.pop_back();
            return result;
        }
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) throw std::runtime_error("Debug client receive timed out");
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
        fd_set readable;
        FD_ZERO(&readable);
        FD_SET(m_impl->socket, &readable);
        timeval wait{static_cast<long>(remaining.count() / 1000),
                     static_cast<long>((remaining.count() % 1000) * 1000)};
        const int ready = select(0, &readable, nullptr, nullptr, &wait);
        if (ready == 0) throw std::runtime_error("Debug client receive timed out");
        if (ready == SOCKET_ERROR) throw std::runtime_error("Debug client select failed");
        char buffer[2048];
        const int count = recv(m_impl->socket, buffer, static_cast<int>(sizeof(buffer)), 0);
        if (count <= 0) throw std::runtime_error("Debug server closed the connection");
        m_impl->input.append(buffer, buffer + count);
    }
#else
    (void)timeout;
    throw std::runtime_error("Debug control sockets are not available on this platform build");
#endif
}

namespace {
const char* dialog_action_name(const ::cw::dialog::DialogPresentationAction action) noexcept
{
    using A = ::cw::dialog::DialogPresentationAction;
    switch (action) {
        case A::Accept: return "accept"; case A::Cancel: return "cancel";
        case A::Select: return "select"; case A::Previous: return "previous";
        case A::Next: return "next"; case A::PreviousGroup: return "previous_group";
        case A::NextGroup: return "next_group"; case A::Toggle: return "toggle";
        case A::Create: return "create"; case A::Auxiliary: return "auxiliary";
        case A::Screenshot: return "screenshot"; case A::ScreenshotCards: return "screenshot_cards";
        case A::CopyDetail: return "copy_detail"; case A::Activate: return "activate";
    }
    return "unknown";
}

template<class Sprite>
bool game_sprite_clickable(const std::shared_ptr<Sprite>& sprite)
{
    if (!sprite || !::cw::g_cwpy || ::cw::g_cwpy->is_modal_input_blocked()) return false;
    const auto& selectable = static_cast<const ::cw::sprite::base::SelectableSprite&>(*sprite);
    if (selectable.m_rect.width <= 0 || selectable.m_rect.height <= 0 || !selectable.m_image) return false;
    if (selectable.m_status != "normal" && selectable.m_status != "reversed" &&
        selectable.m_status != "click") return false;
    if (::cw::g_cwpy->is_dealing() && !selectable.m_selectable_on_event) return false;
    if (::cw::g_cwpy->is_runningevent() && !selectable.m_selectable_on_event) return false;
    if (::cw::g_cwpy->m_battle && *::cw::g_cwpy->m_battle &&
        !(*::cw::g_cwpy->m_battle)->is_ready() && !selectable.m_selectable_on_event) return false;
    return !::cw::g_cwpy->is_lockmenucards(
        std::static_pointer_cast<::cw::sprite::base::SelectableSprite>(sprite));
}

void append_area(std::ostringstream& output, bool& first, std::string_view category,
                 std::string_view id, std::string_view label, const ::cw::lib::Rect& rect,
                 std::string_view extra = {})
{
    if (!first) output << ',';
    first = false;
    output << "{\"category\":\"" << json_escape(category) << "\",\"id\":\""
           << json_escape(id) << "\",\"label\":\"" << json_escape(label)
           << "\",\"x\":" << rect.x << ",\"y\":" << rect.y
           << ",\"width\":" << rect.width << ",\"height\":" << rect.height;
    if (!extra.empty()) output << ',' << extra;
    output << '}';
}
}

void refresh_debug_control_clickable_areas()
{
    ::cw::lib::RuntimeAccess runtime_access;
    auto* server = g_active_debug_server.load(std::memory_order_acquire);
    if (!server || !server->wants_clickable_areas() || !::cw::g_cwpy) return;
    std::ostringstream output;
    output << "{\"type\":\"clickable_areas\",\"areas\":[";
    bool first = true;
    const auto runtime = ::cw::g_cwpy;
    const auto* frame_object = std::get_if<::cw::lib::ObjectPtr>(&runtime->m_frame.storage());
    const auto frame = frame_object && *frame_object
        ? std::dynamic_pointer_cast<::cw::frame::Frame>(*frame_object) : nullptr;
    const auto active_model = frame ? frame->dialog_presentation_model() : nullptr;
    // 単独起動の入れ子のイベント処理がモーダル深度を切り替える間も、共通Presentation
    // の状態を優先する。 ダイアログが入力を受け付けている間は
    // 、その背後のゲームスプライトを操作対象として公開しない。
    const bool modal = runtime->is_modal_input_blocked() || static_cast<bool>(active_model);
    if (!modal) {
        std::size_t index = 0;
        for (const auto& value : runtime->m_mcards) {
            const auto card = debug_object_value<::cw::sprite::card::MenuCard>(value);
            if (card && game_sprite_clickable(card) && card->is_flagtrue())
                append_area(output, first, "menu_card", "menu:" + std::to_string(index),
                            card->get_showingname(), card->m_rect, "\"space\":\"game\"");
            ++index;
        }
        for (index = 0; index < runtime->m_pcards.size(); ++index) {
            const auto& card = runtime->m_pcards[index];
            if (card && game_sprite_clickable(card))
                append_area(output, first, "status_card", "player:" + std::to_string(index),
                            card->get_showingname(), card->m_rect, "\"space\":\"game\"");
        }
        if (runtime->m_statusbar) {
            index = 0;
            for (const auto& item : runtime->m_statusbar->m_status_items) {
                const auto button = std::dynamic_pointer_cast<::cw::sprite::statusbar::StatusBarButton>(item);
                if (button && button->can_activate())
                    append_area(output, first, "status", "status:" + std::to_string(index),
                                button->m_name_typed,
                                static_cast<const ::cw::sprite::base::SelectableSprite&>(*button).m_rect,
                                "\"space\":\"game\"");
                ++index;
            }
        }
    }
    const auto debugger_owner = frame && frame->m_debugger
        ? reinterpret_cast<std::uintptr_t>((*frame->m_debugger).get()) : 0;
    for (const auto& snapshot : ::cw::dialog::dialog_debug_snapshots()) {
        const bool active = active_model && snapshot.owner == reinterpret_cast<std::uintptr_t>(active_model.get());
        const bool debugger = !active_model && debugger_owner != 0 && snapshot.owner == debugger_owner;
        if (!active && !debugger) continue;
        for (std::size_t index = 0; index < snapshot.controls.size(); ++index) {
            const auto& control = snapshot.controls[index];
            if (!control.enabled || control.width <= 0 || control.height <= 0) continue;
            const ::cw::lib::Rect rect{static_cast<::cw::lib::Integer>(std::lround(control.x)),
                static_cast<::cw::lib::Integer>(std::lround(control.y)),
                static_cast<::cw::lib::Integer>(std::lround(control.width)),
                static_cast<::cw::lib::Integer>(std::lround(control.height))};
            const auto id = "dialog:" + std::to_string(snapshot.owner) + ":" + std::to_string(index);
            const auto extra = "\"space\":\"dialog\",\"route\":\"" +
                json_escape(snapshot.route) + "\",\"title\":\"" + json_escape(snapshot.title) +
                "\",\"action\":\"" + dialog_action_name(control.action) +
                "\",\"index\":" + std::to_string(control.index);
            append_area(output, first, "dialog_control", id, control.label, rect, extra);
        }
    }
    output << "]}";
    server->publish_clickable_areas(output.str());
}

void set_nonactive_automation_enabled(bool enabled) noexcept
{
    g_nonactive_automation.store(enabled, std::memory_order_release);
}

bool nonactive_automation_enabled() noexcept
{
    return g_nonactive_automation.load(std::memory_order_acquire);
}

void prepare_nonactive_window(std::uintptr_t native_handle) noexcept
{
#ifdef _WIN32
    if (!nonactive_automation_enabled() || native_handle == 0) return;
    const auto window = reinterpret_cast<HWND>(native_handle);
    if (!IsWindow(window)) return;
    SetWindowLongPtrW(window, GWL_EXSTYLE,
                      GetWindowLongPtrW(window, GWL_EXSTYLE) | WS_EX_NOACTIVATE);
    // 所有される最上位ウィンドウを表示すると、WS_EX_NOACTIVATE
    // があっても 所有者が最前面になる場合がある。自動テスト
    // では中央配置と結果通知のための 所有者ハンドルは内部に
    // 保持するが、OSへネイティブの所有関係を登録しない。
    if ((GetWindowLongPtrW(window, GWL_STYLE) & WS_CHILD) == 0)
        SetWindowLongPtrW(window, GWLP_HWNDPARENT, 0);
#else
    (void)native_handle;
#endif
}

}  // namespace cardwirth::debug
