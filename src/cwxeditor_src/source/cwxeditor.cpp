#include "cwxeditor_src/source/cwxeditor.hpp"

#include "cwxeditor_src/core/cwx/msgs.hpp"
#include "cwxeditor_src/core/cwx/filesync.hpp"
#include "cwxeditor_src/core/cwx/props.hpp"
#include "cwxeditor_src/core/cwx/system.hpp"
#include "cwxeditor_src/core/cwx/utils.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/absdialog.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/commons.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dprops.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/mainwindow.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/textdialog.hpp"
#include "cwxeditor_src/lib/lifecycle.hpp"
#include "cwxeditor_src/lib/sdl_ui.hpp"
#include "cwxeditor_src/lib/editor_diagnostics.hpp"
#include "cwxeditor_src/lib/ui_actions.hpp"
#include "cwxeditor_src/lib/ui_metrics.hpp"
#include "cwxeditor_src/source/editor_control_server.hpp"
#include "cwxeditor_src/source/editor_session_setup.hpp"
#include "cw/lib/archive.hpp"

#include <algorithm>
#include <chrono>
#include <charconv>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <stdexcept>
#include <utility>
#include <SDL3/SDL.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#endif

namespace cwxeditor::cwxeditor {

std::vector<String> rt_options{"scanDataSeg=precise"};

namespace {

using CProps = ::cwxeditor::cwx::props::CProps;
using System = ::cwxeditor::cwx::system::System;
using Props = ::cwxeditor::cwx::editor::gui::dwt::dprops::Props;
using Services = ::cwxeditor::lib::Services;
using Path = std::filesystem::path;
namespace commons = ::cwxeditor::cwx::editor::gui::dwt::commons;
namespace dprops = ::cwxeditor::cwx::editor::gui::dwt::dprops;
namespace mainwindow = ::cwxeditor::cwx::editor::gui::dwt::mainwindow;
namespace textdialog = ::cwxeditor::cwx::editor::gui::dwt::textdialog;
namespace lib = ::cwxeditor::lib;


std::mutex main_window_runner_mutex;
MainWindowRunner main_window_runner;

String trim(String value) {
    const auto nonspace = [](const unsigned char ch) { return std::isspace(ch) == 0; };
    const auto first = std::find_if(value.begin(), value.end(), nonspace);
    const auto last = std::find_if(value.rbegin(), value.rend(), nonspace).base();
    if (first >= last) return {};
    return {first, last};
}

String upperAscii(String value) {
    std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    return value;
}

bool environmentFlag(const char* name) {
    const auto* raw = std::getenv(name);
    if (!raw) return false;
    const auto value = upperAscii(trim(raw));
    return value == "1" || value == "TRUE" || value == "YES" || value == "ON";
}

unsigned short editorControlPort(const unsigned short fallback) {
    const auto* raw = std::getenv("CWXEDITOR_CONTROL_PORT");
    if (!raw || !*raw) return fallback;
    const std::string_view value(raw);
    unsigned int port{};
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), port);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() ||
        port == 0U || port > 65535U)
        throw std::invalid_argument("CWXEDITOR_CONTROL_PORT must be between 1 and 65535");
    return static_cast<unsigned short>(port);
}

lib::PointerButton pointerButton(String value) {
    value = upperAscii(trim(std::move(value)));
    if (value.empty() || value == "LEFT" || value == "PRIMARY")
        return lib::PointerButton::Primary;
    if (value == "RIGHT" || value == "SECONDARY")
        return lib::PointerButton::Secondary;
    if (value == "MIDDLE") return lib::PointerButton::Middle;
    if (value == "BACK" || value == "X1") return lib::PointerButton::Back;
    if (value == "FORWARD" || value == "X2")
        return lib::PointerButton::Forward;
    throw std::invalid_argument(
        "Pointer button must be LEFT, RIGHT, MIDDLE, BACK, or FORWARD");
}

lib::Key commandKey(String value) {
    value = upperAscii(trim(std::move(value)));
    if (value.size() == 1U && value.front() >= 'A' && value.front() <= 'Z')
        return static_cast<lib::Key>(static_cast<int>(lib::Key::A) +
                                     value.front() - 'A');
    if (value == "DELETE" || value == "DEL") return lib::Key::Delete;
    if (value == "ENTER" || value == "RETURN") return lib::Key::Enter;
    if (value == "ESCAPE" || value == "ESC") return lib::Key::Escape;
    throw std::invalid_argument("Key must be A-Z, DELETE, ENTER, or ESCAPE");
}

struct PointerCommand {
    float x{};
    float y{};
    lib::PointerButton button{lib::PointerButton::Primary};
};

PointerCommand parsePointerCommand(const String& arguments,
                                   const bool with_button) {
    std::istringstream input(arguments);
    PointerCommand result;
    if (!(input >> result.x >> result.y) || !std::isfinite(result.x) ||
        !std::isfinite(result.y))
        throw std::invalid_argument("Pointer coordinates must be finite numbers");
    String button;
    if (input >> button) {
        if (!with_button)
            throw std::invalid_argument("MOUSEMOVE accepts only x and y");
        result.button = pointerButton(std::move(button));
    }
    String extra;
    if (input >> extra)
        throw std::invalid_argument("Too many pointer command arguments");
    return result;
}

std::pair<String, String> splitCommand(String request) {
    request = trim(std::move(request));
    const auto separator = request.find_first_of(" \t");
    if (separator == String::npos) return {upperAscii(std::move(request)), {}};
    auto operation = upperAscii(request.substr(0, separator));
    return {std::move(operation), trim(request.substr(separator + 1U))};
}

template <typename Action>
String invokeEditor(const Services& services, Action action) {
    if (!services.dispatcher) throw std::runtime_error("Editor dispatcher is unavailable");
    auto promise = std::make_shared<std::promise<String>>();
    auto future = promise->get_future();
    services.dispatcher->post([promise, action = std::move(action)]() mutable {
        try {
            promise->set_value(action());
        } catch (...) {
            try { promise->set_exception(std::current_exception()); } catch (...) {}
        }
    });
    if (future.wait_for(std::chrono::seconds(10)) != std::future_status::ready)
        throw std::runtime_error("Editor main thread did not respond within 10 seconds");
    return future.get();
}

void collectTabs(const lib::WidgetPtr& widget, std::vector<std::shared_ptr<lib::TabView>>& out) {
    if (!widget) return;
    if (const auto tabs = std::dynamic_pointer_cast<lib::TabView>(widget)) {
        out.push_back(tabs);
        for (const auto& page : tabs->pages) collectTabs(page.content, out);
    }
    if (const auto container = std::dynamic_pointer_cast<lib::Container>(widget))
        for (const auto& child : container->children()) collectTabs(child, out);
}

std::shared_ptr<lib::Menu> findMenuBar(const lib::WidgetPtr& widget) {
    if (!widget) return {};
    if (const auto menu = std::dynamic_pointer_cast<lib::Menu>(widget);
        menu && menu->kind() == lib::ComponentKind::MenuBar)
        return menu;
    if (const auto container = std::dynamic_pointer_cast<lib::Container>(widget)) {
        for (const auto& child : container->children())
            if (const auto menu = findMenuBar(child)) return menu;
    }
    return {};
}

String currentDialogState(const mainwindow::MainWindow& window) {
    const auto editor_dialog = window.debugDialogState();
    if (editor_dialog != "unavailable") return editor_dialog;

    const auto settings = window.debugSettingsState();
    if (settings.starts_with("visible=1")) return "settings " + settings;

    String result;
    std::size_t visible{};
    for (auto* controller :
         ::cwxeditor::cwx::editor::gui::dwt::absdialog::AbsDialog::liveDialogs()) {
        if (!controller) continue;
        const auto dialog = controller->dialog();
        if (!dialog || !dialog->visible()) continue;
        const auto bounds = dialog->bounds();
        if (visible++ == 0U) result = "live";
        result += ";" + std::to_string(visible - 1U) + "=\"" +
                  dialog->title + "\" bounds=" +
                  std::to_string(static_cast<int>(bounds.x)) + "," +
                  std::to_string(static_cast<int>(bounds.y)) + "," +
                  std::to_string(static_cast<int>(bounds.width)) + "x" +
                  std::to_string(static_cast<int>(bounds.height));
    }
    if (visible != 0U)
        return result + " visible=" + std::to_string(visible);
    return "unavailable";
}

std::shared_ptr<lib::Window> uniqueVisibleDialog() {
    std::shared_ptr<lib::Window> result;
    for (auto* controller :
         ::cwxeditor::cwx::editor::gui::dwt::absdialog::AbsDialog::liveDialogs()) {
        if (!controller) continue;
        const auto dialog = controller->dialog();
        if (!dialog || !dialog->visible()) continue;
        if (result && result != dialog) return {};
        result = dialog;
    }
    return result;
}

String editorCommand(mainwindow::MainWindow& window, const Services& services,
                     String request) {
    const auto [operation, arguments] = splitCommand(std::move(request));
    if (operation == "PING") return "PONG";
    if (operation == "NATIVEMOUSEBURST") {
        // 検証専用の制御コマンド。エディタの当たり判定を複製せず
        // 、実際のマウス入力と同じネイティブSDLキューへ送る。
        std::istringstream input(arguments);
        unsigned count{};
        float x{}, y{};
        if (!(input >> count >> x >> y) || !(input >> std::ws).eof() ||
            count == 0U || count > 10000U || !std::isfinite(x) || !std::isfinite(y))
            throw std::invalid_argument("Usage: NATIVEMOUSEBURST count(1..10000) x y");
        auto* native_window = SDL_GetKeyboardFocus();
        if (!native_window) {
            // デスクトップのフォーカスがツール側に残る場合がある。 無関係な
            // ウィンドウを前面化せず、このプロセス自身の主画面を取得する。
            const auto shell = std::dynamic_pointer_cast<lib::Window>(window.shell());
            int windows_count{};
            SDL_Window** native_windows = SDL_GetWindows(&windows_count);
            if (shell && native_windows)
                for (int i = 0; i < windows_count; ++i)
                    if (shell->title == SDL_GetWindowTitle(native_windows[i])) {
                        native_window = native_windows[i];
                        break;
                    }
            SDL_free(native_windows);
        }
        if (!native_window) throw std::runtime_error("Native Editor surface is unavailable");
        const auto id = SDL_GetWindowID(native_window);
        unsigned queued{};
        for (; queued < count; ++queued) {
            SDL_Event event{};
            event.type = SDL_EVENT_MOUSE_MOTION;
            event.motion.timestamp = SDL_GetTicksNS();
            event.motion.windowID = id;
            event.motion.x = x + static_cast<float>(queued % 4U);
            event.motion.y = y;
            event.motion.xrel = queued % 4U == 0U ? -3.0F : 1.0F;
            if (!SDL_PushEvent(&event)) break;
        }
        return "native mouse events queued=" + std::to_string(queued);
    }
    if (operation == "MOUSEMOVE" || operation == "CLICK" ||
        operation == "DBLCLICK" || operation == "MOUSEDOWN" ||
        operation == "MOUSEUP" || operation == "LEFTCLICK" ||
        operation == "RIGHTCLICK" || operation == "MIDDLECLICK" ||
        operation == "BACKCLICK" || operation == "FORWARDCLICK" ||
        operation == "LEFTDOWN" || operation == "LEFTUP" ||
        operation == "RIGHTDOWN" || operation == "RIGHTUP" ||
        operation == "MIDDLEDOWN" || operation == "MIDDLEUP") {
        if (!services.gui)
            throw std::runtime_error("Editor GUI backend is unavailable");
        const bool generic_button = operation == "CLICK" ||
            operation == "DBLCLICK" || operation == "MOUSEDOWN" ||
            operation == "MOUSEUP";
        auto pointer = parsePointerCommand(arguments, generic_button);
        if (operation.starts_with("RIGHT"))
            pointer.button = lib::PointerButton::Secondary;
        else if (operation.starts_with("MIDDLE"))
            pointer.button = lib::PointerButton::Middle;
        else if (operation.starts_with("BACK"))
            pointer.button = lib::PointerButton::Back;
        else if (operation.starts_with("FORWARD"))
            pointer.button = lib::PointerButton::Forward;
        else if (operation.starts_with("LEFT"))
            pointer.button = lib::PointerButton::Primary;

        auto scope = services.gui->active_input_window();
        if (!scope)
            scope = std::dynamic_pointer_cast<lib::Window>(window.shell());
        if (!scope)
            throw std::runtime_error("Editor input window is unavailable");

        bool handled{};
        const auto send = [&](const lib::PointerAction action,
                              const lib::PointerButton button,
                              const bool primary_down,
                              const unsigned clicks = 1U) {
            lib::PointerEvent event;
            event.pointer_id = 1U;
            event.pointer_type = lib::PointerType::Mouse;
            event.action = action;
            event.button = button;
            event.position = {pointer.x, pointer.y};
            event.modifiers = services.gui->current_modifiers();
            lib::editor_action_log("Control pointer dispatch begin command=" + operation +
                " action=" + std::to_string(static_cast<unsigned>(action)));
            handled = services.gui->dispatch_pointer_event(
                scope, std::move(event), primary_down, clicks) || handled;
            lib::editor_action_log("Control pointer dispatch end command=" + operation +
                " action=" + std::to_string(static_cast<unsigned>(action)));
        };
        if (operation == "MOUSEMOVE") {
            send(lib::PointerAction::Move, lib::PointerButton::None, false);
        } else {
            const bool down_only = operation == "MOUSEDOWN" ||
                operation.ends_with("DOWN");
            const bool up_only = operation == "MOUSEUP" ||
                operation.ends_with("UP");
            const unsigned clicks = operation == "DBLCLICK" ? 2U : 1U;
            if (!up_only) {
                send(lib::PointerAction::Move, lib::PointerButton::None,
                     false, clicks);
                send(lib::PointerAction::Down, pointer.button,
                     pointer.button == lib::PointerButton::Primary, clicks);
            }
            if (!down_only)
                send(lib::PointerAction::Up, pointer.button, false, clicks);
        }
        return operation + " dispatched x=" + std::to_string(pointer.x) +
               " y=" + std::to_string(pointer.y) + " handled=" +
               (handled ? "1" : "0");
    }
    if (operation == "UITEXTSCALE") {
        if (arguments.empty() || !services.gui)
            throw std::invalid_argument("Usage: UITEXTSCALE factor (1..2)");
        std::size_t consumed{};
        const auto factor = std::stof(arguments, &consumed);
        if (consumed != arguments.size() || !(factor >= 1.0F && factor <= 2.0F))
            throw std::invalid_argument("UI text scale must be between 1 and 2");
        // 描画の検証専用。ユーザー設定には保存しない。
        services.gui->set_ui_text_scale(factor);
        return "scale=" + std::to_string(factor);
    }
    if (operation == "STATE") {
        const auto summary = window.summary();
        return "title=\"" + window.title() + "\" scenario=\"" +
               (summary ? summary->scenarioName() : String{}) + "\" path=\"" +
               (summary ? summary->scenarioPath() : String{}) + "\" changed=" +
               (summary && summary->isChanged() ? "1" : "0") + " saving=" +
               (window.saveInProgress() ? "1" : "0");
    }
    if (operation == "MENUS") {
        const auto menu = findMenuBar(window.shell());
        if (!menu) throw std::runtime_error("Editor menu bar is unavailable");
        String result = "count=" + std::to_string(menu->items.size());
        for (std::size_t index = 0; index < menu->items.size(); ++index) {
            const auto& item = menu->items[index];
            result += ";" + std::to_string(index) + "=\"" + item.text +
                      "\" children=" + std::to_string(item.children.size());
        }
        return result;
    }
    if (operation == "MENU") {
        const auto menu = findMenuBar(window.shell());
        if (!menu) throw std::runtime_error("Editor menu bar is unavailable");
        if (arguments.empty()) throw std::invalid_argument("Usage: MENU index");
        std::size_t consumed{};
        const auto index = std::stoull(arguments, &consumed);
        if (consumed != arguments.size() || index >= menu->items.size())
            throw std::invalid_argument("Menu index is invalid");
        const auto& top = menu->items[index];
        String result = "title=\"" + top.text + "\" count=" +
                        std::to_string(top.children.size());
        for (std::size_t child = 0; child < top.children.size(); ++child) {
            if (top.children[child].separator) continue;
            result += ";" + std::to_string(child) + "=\"" +
                      top.children[child].text + "\"";
        }
        return result;
    }
    if (operation == "SHOWMENU") {
        const auto menu = findMenuBar(window.shell());
        if (!menu || !services.gui) throw std::runtime_error("Editor menu bar is unavailable");
        if (arguments.empty()) throw std::invalid_argument("Usage: SHOWMENU index");
        std::size_t consumed{};
        const auto index = std::stoull(arguments, &consumed);
        if (consumed != arguments.size()) throw std::invalid_argument("Menu index is invalid");
        if (menu->on_open) menu->on_open();
        if (index >= menu->items.size()) throw std::invalid_argument("Menu index is invalid");
        const auto& item = menu->items[index];
        if (!item.enabled || item.separator || item.children.empty())
            throw std::invalid_argument("Menu has no available popup");
        const auto geometry = lib::menu_bar_item_geometry(*menu);
        auto popup = std::make_shared<lib::Menu>(true);
        popup->items = item.children;
        popup->on_command = menu->on_command;
        services.gui->show_context_menu(popup, window.shell(),
            {geometry[index].x, geometry[index].y + geometry[index].height});
        return "shown \"" + item.text + "\"";
    }
    if (operation == "INVOKEMENU") {
        const auto menu = findMenuBar(window.shell());
        if (!menu) throw std::runtime_error("Editor menu bar is unavailable");
        // ネイティブでメニューを開くと、動的な有効・チェック状態
        // をすべて更新する。 遠隔検証でも同じ処理を行い、シナリ
        // オを開いた後も起動時の無効な状態が残らないようにする。
        if (menu->on_open) menu->on_open();
        if (arguments.empty())
            throw std::invalid_argument("Usage: INVOKEMENU top-index child-index [child-index ...]");
        std::istringstream input(arguments);
        std::vector<std::size_t> path;
        std::size_t index{};
        while (input >> index) path.push_back(index);
        if (!input.eof() || path.size() < 2U)
            throw std::invalid_argument("Menu index path is invalid");
        const lib::MenuItem* item{};
        const std::vector<lib::MenuItem>* items = &menu->items;
        for (const auto part : path) {
            if (part >= items->size())
                throw std::invalid_argument("Menu index is out of range");
            item = &(*items)[part];
            items = &item->children;
        }
        if (!item || item->separator ||
            (!item->action && item->command.empty()))
            throw std::runtime_error("Requested menu item is not executable");
        // 制御要求自体がGUIの通知処理上で動くため、操作をもう
        // 一度キューへ入れる。 モーダルのメニュー操作が、入れ子
        // のダイアログループへ入る前に応答を返せるようにする。
        const auto action = item->action;
        const auto command = item->command;
        services.dispatcher->post([menu, action, command] {
            if (action) action();
            else if (menu->on_command) menu->on_command(command);
        });
        return "queued \"" + item->text + "\"";
    }
    if (operation == "INVOKEMENUID") {
        if (arguments.empty())
            throw std::invalid_argument("Usage: INVOKEMENUID numeric-menu-id");
        std::size_t consumed{};
        const auto raw = std::stoll(arguments, &consumed);
        if (consumed != arguments.size())
            throw std::invalid_argument("Menu ID is invalid");
        if (const auto menu = findMenuBar(window.shell()); menu && menu->on_open)
            menu->on_open();
        const auto id = static_cast<::cwxeditor::cwx::types::MenuID>(raw);
        const auto item = window.getMenu(id);
        if (!item)
            throw std::runtime_error("Requested MenuID is not present");
        services.dispatcher->post([item] { item.trigger(); });
        return "queued id=" + std::to_string(raw);
    }
    if (operation == "KEY") {
        const auto [modifier, key_name] = splitCommand(arguments);
        const bool escape = (modifier == "ESC" || modifier == "ESCAPE") && key_name.empty();
        if (!escape && (modifier != "CTRL" || key_name.empty()))
            throw std::invalid_argument("Usage: KEY CTRL C|V|X|Z|Y or KEY ESC");
        const auto shell = std::dynamic_pointer_cast<lib::Window>(window.shell());
        if (!services.gui || !shell ||
            !services.gui->dispatch_key_event(
                shell, {escape ? lib::Key::Escape : commandKey(key_name),
                    escape ? lib::Modifiers::None : lib::Modifiers::Control}, true))
            throw std::runtime_error("Key was not handled by the focused editor control");
        return escape ? "handled ESC" : "handled CTRL+" + upperAscii(key_name);
    }
    if (operation == "WINDOWSTATE") {
        const auto shell = std::dynamic_pointer_cast<lib::Window>(window.shell());
        if (!shell) throw std::runtime_error("Main window is unavailable");
        const auto bounds = services.gui ? services.gui->window_bounds(shell)
                                         : shell->bounds();
        return "x=" + std::to_string(static_cast<int>(std::lround(bounds.x))) +
               " y=" + std::to_string(static_cast<int>(std::lround(bounds.y))) +
               " width=" + std::to_string(static_cast<int>(std::lround(bounds.width))) +
               " height=" + std::to_string(static_cast<int>(std::lround(bounds.height))) +
               " maximized=" + std::to_string(shell->maximized ? 1 : 0);
    }
    if (operation == "SETWINDOWBOUNDS") {
        std::istringstream input(arguments);
        lib::Rect bounds{};
        if (!(input >> bounds.x >> bounds.y >> bounds.width >> bounds.height) ||
            !(input >> std::ws).eof() || bounds.width <= 0.0F || bounds.height <= 0.0F)
            throw std::invalid_argument("Usage: SETWINDOWBOUNDS x y width height");
        const auto shell = std::dynamic_pointer_cast<lib::Window>(window.shell());
        if (!services.gui || !shell)
            throw std::runtime_error("Main window is unavailable");
        shell->maximized = false;
        services.gui->set_window_bounds(shell, bounds);
        return "window bounds queued";
    }
    if (operation == "TABS") {
        std::vector<std::shared_ptr<lib::TabView>> tabs;
        collectTabs(window.shell(), tabs);
        String result = "count=" + std::to_string(tabs.size());
        for (std::size_t i = 0; i < tabs.size(); ++i) {
            result += ";" + std::to_string(i) + "[selected=" +
                      std::to_string(tabs[i]->selected_index.value_or(0U)) + "]=";
            for (std::size_t p = 0; p < tabs[i]->pages.size(); ++p) {
                if (p != 0U) result += '|';
                result += tabs[i]->pages[p].title;
            }
        }
        return result;
    }
    if (operation == "SELECTTAB") {
        const auto [groupText, targetText] = splitCommand(arguments);
        const auto group = upperAscii(groupText);
        if (targetText.empty()) throw std::invalid_argument("Usage: SELECTTAB LEFT|RIGHT index|title");
        const std::vector<String> expected = group == "LEFT"
            ? std::vector<String>{"テーブル", "状態変数", "ファイル", "クーポン"}
            : group == "RIGHT"
                ? std::vector<String>{"キャスト", "特殊技能", "アイテム", "召喚獣", "情報"}
                : std::vector<String>{};
        if (expected.empty()) throw std::invalid_argument("Unknown tab group: " + groupText);
        std::vector<std::shared_ptr<lib::TabView>> tabs;
        collectTabs(window.shell(), tabs);
        const auto found = std::find_if(tabs.begin(), tabs.end(), [&expected](const auto& value) {
            if (!value || value->pages.size() != expected.size()) return false;
            for (std::size_t i = 0; i < expected.size(); ++i)
                if (value->pages[i].title != expected[i]) return false;
            return true;
        });
        if (found == tabs.end()) throw std::runtime_error("Requested tab group is unavailable");
        std::size_t selected = expected.size();
        try {
            std::size_t consumed{};
            selected = std::stoull(targetText, &consumed);
            if (consumed != targetText.size()) selected = expected.size();
        } catch (...) {
            selected = expected.size();
        }
        if (selected >= expected.size()) {
            const auto byTitle = std::find(expected.begin(), expected.end(), targetText);
            if (byTitle == expected.end()) throw std::invalid_argument("Unknown tab: " + targetText);
            selected = static_cast<std::size_t>(std::distance(expected.begin(), byTitle));
        }
        if ((*found)->selected_index.value_or(0U) != selected &&
            !lib::select_tab_page(**found, selected))
            throw std::runtime_error("Requested tab selection was rejected");
        return group + " selected=" + std::to_string(selected) + " title=\"" + expected[selected] + "\"";
    }
    if (operation == "SELECTTABINDEX") {
        std::istringstream input(arguments);
        std::size_t tabIndex{};
        std::size_t pageIndex{};
        if (!(input >> tabIndex >> pageIndex))
            throw std::invalid_argument("Usage: SELECTTABINDEX tab-index page-index");
        String extra;
        if (input >> extra)
            throw std::invalid_argument("Usage: SELECTTABINDEX tab-index page-index");
        std::vector<std::shared_ptr<lib::TabView>> tabs;
        collectTabs(window.shell(), tabs);
        if (tabIndex >= tabs.size() || !tabs[tabIndex] ||
            pageIndex >= tabs[tabIndex]->pages.size())
            throw std::invalid_argument("Tab or page index is out of range");
        const auto before = tabs[tabIndex]->selected_index.value_or(0U);
        if (before != pageIndex && !lib::select_tab_page(*tabs[tabIndex], pageIndex))
            throw std::runtime_error("Requested tab selection was rejected");
        return "tab=" + std::to_string(tabIndex) + " selected=" +
               std::to_string(pageIndex) + " title=\"" +
               tabs[tabIndex]->pages[pageIndex].title + "\"";
    }
    if (operation == "SELECTCARD") {
        const auto [type, indexText] = splitCommand(arguments);
        if (type.empty() || indexText.empty())
            throw std::invalid_argument("Usage: SELECTCARD CAST|SKILL|ITEM|BEAST|INFO index");
        std::size_t consumed{};
        const auto index = std::stoull(indexText, &consumed);
        if (consumed != indexText.size()) throw std::invalid_argument("Card index is invalid");
        if (!window.debugSelectCard(type, index)) throw std::runtime_error("Card selection failed");
        return upperAscii(type) + " " + window.debugCardState(type);
    }
    if (operation == "SELECTVARIABLE") {
        const auto [type, indexText] = splitCommand(arguments);
        if (type.empty() || indexText.empty())
            throw std::invalid_argument("Usage: SELECTVARIABLE FLAG|STEP|VARIANT index");
        std::size_t consumed{};
        const auto index = std::stoull(indexText, &consumed);
        if (consumed != indexText.size())
            throw std::invalid_argument("Variable index is invalid");
        if (!window.debugSelectVariable(type, index))
            throw std::runtime_error("Variable selection failed");
        return upperAscii(type) + " " + window.debugVariableState();
    }
    if (operation == "VARIABLESTATE") return window.debugVariableState();
    if (operation == "TOUCHCARD") {
        const auto [type, indexText] = splitCommand(arguments);
        if (type.empty() || indexText.empty())
            throw std::invalid_argument("Usage: TOUCHCARD CAST|SKILL|ITEM|BEAST|INFO index");
        std::size_t consumed{};
        const auto index = std::stoull(indexText, &consumed);
        if (consumed != indexText.size()) throw std::invalid_argument("Card index is invalid");
        if (!window.debugTouchCard(type, index)) throw std::runtime_error("Card pointer operation failed");
        return upperAscii(type) + " " + window.debugCardState(type);
    }
    if (operation == "OPENCARD") {
        const auto [type, indexText] = splitCommand(arguments);
        if (type.empty() || indexText.empty())
            throw std::invalid_argument("Usage: OPENCARD CAST|SKILL|ITEM|BEAST|INFO index");
        std::size_t consumed{};
        const auto index = std::stoull(indexText, &consumed);
        if (consumed != indexText.size()) throw std::invalid_argument("Card index is invalid");
        if (!window.debugOpenCard(type, index)) throw std::runtime_error("Card dialog operation failed");
        return upperAscii(type) + " dialog-opened index=" + std::to_string(index);
    }
    if (operation == "OPENAREA") {
        if (arguments.empty()) throw std::invalid_argument("Usage: OPENAREA index");
        std::size_t consumed{};
        const auto index = std::stoull(arguments, &consumed);
        if (consumed != arguments.size()) throw std::invalid_argument("Area index is invalid");
        if (!window.debugOpenArea(index)) throw std::runtime_error("Area scene operation failed");
        return "AREA scene-opened index=" + std::to_string(index);
    }
    if (operation == "OPENBATTLE") {
        if (arguments.empty()) throw std::invalid_argument("Usage: OPENBATTLE index");
        std::size_t consumed{};
        const auto index = std::stoull(arguments, &consumed);
        if (consumed != arguments.size()) throw std::invalid_argument("Battle index is invalid");
        if (!window.debugOpenBattle(index)) throw std::runtime_error("Battle scene operation failed");
        return "BATTLE scene-opened index=" + std::to_string(index);
    }
    if (operation == "OPENAREAEVENT") {
        if (arguments.empty()) throw std::invalid_argument("Usage: OPENAREAEVENT index");
        std::size_t consumed{};
        const auto index = std::stoull(arguments, &consumed);
        if (consumed != arguments.size()) throw std::invalid_argument("Area index is invalid");
        if (!window.debugOpenAreaEvent(index))
            throw std::runtime_error("Area event operation failed");
        return "AREA event-opened index=" + std::to_string(index);
    }
    if (operation == "OPENBATTLEEVENT") {
        if (arguments.empty()) throw std::invalid_argument("Usage: OPENBATTLEEVENT index");
        std::size_t consumed{};
        const auto index = std::stoull(arguments, &consumed);
        if (consumed != arguments.size()) throw std::invalid_argument("Battle index is invalid");
        if (!window.debugOpenBattleEvent(index))
            throw std::runtime_error("Battle event operation failed");
        return "BATTLE event-opened index=" + std::to_string(index);
    }
    if (operation == "IMPORTSCENARIO") {
        if (arguments.empty())
            throw std::invalid_argument("Usage: IMPORTSCENARIO scenario-path");
        if (!window.debugImportScenario(arguments))
            throw std::runtime_error("Scenario import operation failed");
        return "import-started path=\"" + arguments + "\"";
    }
    if (operation == "OPENUSEEVENT") {
        const auto [type, indexText] = splitCommand(arguments);
        if (type.empty() || indexText.empty())
            throw std::invalid_argument(
                "Usage: OPENUSEEVENT SKILL|ITEM|BEAST index");
        std::size_t consumed{};
        const auto index = std::stoull(indexText, &consumed);
        if (consumed != indexText.size())
            throw std::invalid_argument("Card index is invalid");
        if (!window.debugOpenUseEvent(type, index))
            throw std::runtime_error("Card use-event operation failed");
        return upperAscii(type) + " use-event-opened index=" +
               std::to_string(index);
    }
    if (operation == "OPENPACKAGE") {
        if (arguments.empty()) throw std::invalid_argument("Usage: OPENPACKAGE index");
        std::size_t consumed{};
        const auto index = std::stoull(arguments, &consumed);
        if (consumed != arguments.size()) throw std::invalid_argument("Package index is invalid");
        if (!window.debugOpenPackage(index))
            throw std::runtime_error("Package event operation failed");
        return "PACKAGE event-opened index=" + std::to_string(index);
    }
    if (operation == "OPENEVENTCONTENT") {
        if (arguments.empty())
            throw std::invalid_argument("Usage: OPENEVENTCONTENT visible-index");
        std::size_t consumed{};
        const auto index = std::stoull(arguments, &consumed);
        if (consumed != arguments.size())
            throw std::invalid_argument("Event content index is invalid");
        if (!window.debugOpenEventContent(index))
            throw std::runtime_error("Event content dialog operation failed");
        return "EVENT content-dialog-opened index=" + std::to_string(index);
    }
    if (operation == "PLACECONTENT") {
        std::istringstream input(arguments);
        std::int32_t rawType{};
        int autoOpen{};
        std::size_t visibleIndex{};
        if (!(input >> rawType))
            throw std::invalid_argument(
                "Usage: PLACECONTENT ctype-number [auto-open-0-or-1] [visible-index]");
        if (!(input >> autoOpen)) {
            input.clear();
            autoOpen = 0;
        } else if (autoOpen != 0 && autoOpen != 1) {
            throw std::invalid_argument("auto-open must be 0 or 1");
        }
        if (!(input >> visibleIndex)) {
            input.clear();
            visibleIndex = 0U;
        }
        String extra;
        if (input >> extra)
            throw std::invalid_argument(
                "Usage: PLACECONTENT ctype-number [auto-open-0-or-1] [visible-index]");
        using CType = ::cwxeditor::cwx::types::CType;
        if (rawType < static_cast<std::int32_t>(CType::Start) ||
            rawType > static_cast<std::int32_t>(CType::CustomXmlContent))
            throw std::invalid_argument("CType is out of range");
        if (!window.debugPlaceEventContent(static_cast<CType>(rawType),
                                           autoOpen != 0, visibleIndex))
            throw std::runtime_error("Event content placement failed");
        return "EVENT content-placed type=" + std::to_string(rawType) +
               " visible-index=" + std::to_string(visibleIndex) +
               " auto-open=" + std::to_string(autoOpen);
    }
    if (operation == "APPLYEVENTCONTENT") {
        if (!arguments.empty())
            throw std::invalid_argument("Usage: APPLYEVENTCONTENT");
        if (!window.debugApplyEventContentDialog())
            throw std::runtime_error("Event content apply failed");
        return "event-content-applied";
    }
    if (operation == "CREATESCENECARD") {
        if (!arguments.empty())
            throw std::invalid_argument("Usage: CREATESCENECARD");
        if (!window.debugCreateSceneCard())
            throw std::runtime_error("Scene card creation failed");
        return "scene-card-dialog-opened";
    }
    if (operation == "APPLYSCENECARD") {
        if (!arguments.empty())
            throw std::invalid_argument("Usage: APPLYSCENECARD");
        if (!window.debugApplySceneCardDialog())
            throw std::runtime_error("Scene card apply failed");
        return "scene-card-applied";
    }
    if (operation == "CARDSTATE") {
        if (arguments.empty()) throw std::invalid_argument("Usage: CARDSTATE CAST|SKILL|ITEM|BEAST|INFO");
        return upperAscii(arguments) + " " + window.debugCardState(arguments);
    }
    if (operation == "TOOLBARSTATE")
        return window.debugToolbarCustomizerState();
    if (operation == "OPENSETTINGS") {
        window.debugOpenSettings();
        return window.debugSettingsState();
    }
    if (operation == "SETTINGSSTATE") return window.debugSettingsState();
    if (operation == "SELECTSETTINGSTAB") {
        if (arguments.empty()) throw std::invalid_argument("Usage: SELECTSETTINGSTAB index");
        std::size_t consumed{};
        const auto index = std::stoull(arguments, &consumed);
        if (consumed != arguments.size() || !window.debugSelectSettingsTab(index))
            throw std::runtime_error("Settings tab selection failed");
        return window.debugSettingsState();
    }
    if (operation == "CLOSESETTINGS") {
        if (!window.debugCloseSettings())
            throw std::runtime_error("Settings dialog close failed");
        return window.debugSettingsState();
    }
    if (operation == "OPENDIALOG") {
        if (arguments.empty())
            throw std::invalid_argument(
                "Usage: OPENDIALOG SCENARIO|EFFECT|MESSAGE|EVENT:<ctype-number>");
        if (!window.debugOpenDialog(arguments))
            throw std::runtime_error("Editor dialog open failed");
        return window.debugDialogState();
    }
    if (operation == "DIALOGSTATE") return currentDialogState(window);
    if (operation == "LIVEDIALOGS") {
        std::size_t visible{};
        std::size_t unapplied{};
        for (auto* dialog :
             ::cwxeditor::cwx::editor::gui::dwt::absdialog::AbsDialog::liveDialogs()) {
            if (!dialog || !dialog->dialog() || !dialog->dialog()->visible()) continue;
            ++visible;
            if (dialog->noApply()) ++unapplied;
        }
        return "visible=" + std::to_string(visible) +
               " unapplied=" + std::to_string(unapplied);
    }
    if (operation == "CANCELDIALOGS") {
        std::size_t closed{};
        // forceCancel() はダイアログの登録を外すため
        // 、閉じる前に一覧を取得する。 前面化しないメニューの自動
        // 操作でも、実際のキャンセルと同じ共通の寿命処理を使う。
        const auto dialogs =
            ::cwxeditor::cwx::editor::gui::dwt::absdialog::AbsDialog::liveDialogs();
        for (auto* dialog : dialogs) {
            if (!dialog || !dialog->dialog() || !dialog->dialog()->visible()) continue;
            dialog->forceCancel();
            ++closed;
        }
        return "closed=" + std::to_string(closed);
    }
    if (operation == "SELECTDIALOGTAB") {
        if (arguments.empty()) throw std::invalid_argument("Usage: SELECTDIALOGTAB index");
        std::size_t consumed{};
        const auto index = std::stoull(arguments, &consumed);
        if (consumed != arguments.size() || !window.debugSelectDialogTab(index))
            throw std::runtime_error("Dialog tab selection failed");
        return window.debugDialogState();
    }
    if (operation == "SETCASTFIELD") {
        const auto [field, value] = splitCommand(arguments);
        if (field.empty())
            throw std::invalid_argument("Usage: SETCASTFIELD NAME|DESC|LEVEL|LIFEMAX value");
        if (!window.debugSetCastField(field, value))
            throw std::runtime_error("Cast dialog field update failed");
        return "cast-field-updated " + upperAscii(field);
    }
    if (operation == "APPLYCASTDIALOG") {
        if (!window.debugApplyCastDialog())
            throw std::runtime_error("Cast dialog apply failed");
        return "cast-dialog-applied";
    }
    if (operation == "CAPTUREFRAME" || operation == "CAPTUREWINDOW" ||
        operation == "CAPTUREDIALOG" || operation == "CAPTURESETTINGS") {
        if (arguments.empty() || !services.gui || !services.image_codecs)
            throw std::invalid_argument("Usage: " + operation + " output.bmp (or output.png)");
        auto target = operation == "CAPTUREDIALOG" ? window.debugDialogWindow() :
            operation == "CAPTURESETTINGS" ? window.debugSettingsWindow() :
            std::dynamic_pointer_cast<lib::Window>(window.shell());
        if (!target && operation == "CAPTUREDIALOG") target = uniqueVisibleDialog();
        if (!target) throw std::runtime_error("The requested editor window is unavailable or ambiguous");
        auto frame = services.gui->completed_frame(target);
        if (!frame) throw std::runtime_error("A completed client frame is unavailable; wait for presentation");
        const Path output = std::filesystem::absolute(std::filesystem::u8path(arguments));
        const auto extension = upperAscii(output.extension().string());
        if (extension != ".BMP" && extension != ".PNG")
            throw std::invalid_argument("Frame capture requires a .bmp or .png path");
        std::filesystem::create_directories(output.parent_path());
        lib::ImageData image;
        image.frames.push_back(std::move(*frame));
        std::string error;
        const auto format = extension == ".BMP" ? lib::ImageFormat::Bitmap : lib::ImageFormat::Png;
        if (!services.image_codecs->encode(image, {output, format}, error))
            throw std::runtime_error("Editor frame capture failed: " + error);
        return String(operation == "CAPTUREFRAME" ? "captured-frame " : "captured ") + output.generic_string();
    }
    if (operation == "CLOSEDIALOG") {
        if (!window.debugCloseDialog())
            throw std::runtime_error("Editor dialog close failed");
        return window.debugDialogState();
    }
    if (operation == "MENUCLOSE") {
        window.raiseEvent(::cwxeditor::cwx::types::MenuID::Close,
                          ::cwxeditor::cwx::editor::gui::dwt::dmenu::SelectionEvent{});
        return "closing";
    }
    if (operation == "SAVE") {
        if (!window.debugSave()) throw std::runtime_error("Scenario save failed or is unavailable");
        return "saved";
    }
    if (operation == "OPEN") {
        if (arguments.empty()) throw std::invalid_argument("Usage: OPEN scenario-path");
        // シナリオ読み込みは非同期なので、ここでは要求の受け付けを報告し、
        // STATEで完了を確認する。 コールバックにスタック上のフラグを
        // 残すと、後の読み込み失敗で解放済みメモリへ書くため保持しない。
        window.openScenario(arguments, [] {
            lib::editor_action_log("Control scenario open failed");
        });
        return "open-started path=\"" + arguments + "\"";
    }
    if (operation == "CLOSE") {
        // ネイティブの閉じるボタンと同じ、拒否可能な終了経路を検証する。 quit_
        // の直接設定では MainWindow::exitAll()
        // を通らず、実行ファイルの検査で配置を保存せず終了してしまう。
        const auto shell = std::dynamic_pointer_cast<lib::Window>(window.shell());
        if (!shell || !shell->on_close_requested)
            throw std::runtime_error("Main window close handler is unavailable");
        shell->on_close_requested();
        return "closing";
    }
    throw std::invalid_argument("Unknown editor control command: " + operation);
}

String pathText(const Path& path) {
    const auto value = path.u8string();
    return String(reinterpret_cast<const char*>(value.data()), value.size());
}

Path pathFromText(const String& value) {
    return ::cwxeditor::cwx::structs::pathFromUtf8(value);
}

String absolutePathText(const Path& path) {
    return pathText(::cwxeditor::cwx::utils::nabs(path));
}

#if defined(_WIN32)
String utf8FromWide(const wchar_t* value) {
    if (!value || *value == L'\0') return {};
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, -1,
                                         nullptr, 0, nullptr, nullptr);
    if (size <= 1) return {};
    String result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, -1,
                        result.data(), size, nullptr, nullptr);
    result.pop_back();
    return result;
}
#endif

Services resolveServices(Services supplied) {
    Services result = ::cwxeditor::lib::make_platform_services();
    if (supplied.gui) result.gui = std::move(supplied.gui);
    if (supplied.graphics) result.graphics = std::move(supplied.graphics);
    if (supplied.image_codecs) result.image_codecs = std::move(supplied.image_codecs);
    if (supplied.dialogs) result.dialogs = std::move(supplied.dialogs);
    if (supplied.file_picker) result.file_picker = std::move(supplied.file_picker);
    if (supplied.clipboard) result.clipboard = std::move(supplied.clipboard);
    if (supplied.drag_drop) result.drag_drop = std::move(supplied.drag_drop);
    if (supplied.audio) result.audio = std::move(supplied.audio);
    if (supplied.filesystem) result.filesystem = std::move(supplied.filesystem);
    if (supplied.processes) result.processes = std::move(supplied.processes);
    if (supplied.shell) result.shell = std::move(supplied.shell);
    if (supplied.ipc) result.ipc = std::move(supplied.ipc);
    if (supplied.archives) result.archives = std::move(supplied.archives);
    if (supplied.system) result.system = std::move(supplied.system);
    if (supplied.text) result.text = std::move(supplied.text);
    if (supplied.dispatcher) result.dispatcher = std::move(supplied.dispatcher);
    if (supplied.tasks) result.tasks = std::move(supplied.tasks);
    return result;
}

class Application final {
public:
    Application(Arguments args, Services services)
        : args_(std::move(args)), services_(resolveServices(std::move(services))) {}

    ~Application() {
        lib::set_sdl_application_lifecycle_handler(services_, {});
        if (services_.dispatcher) services_.dispatcher->shutdown();
        if (runtime_context_) {
            runtime_context_->lifecycle().dispatch(lib::LifecycleEvent::StopRequested);
            runtime_context_->shutdown();
            runtime_context_->lifecycle().dispatch(lib::LifecycleEvent::Stopped);
        }
    }

    void execute() {
        initialize();
        if (options_.help) {
            showHelp();
            return;
        }
        if (!options_.putlangfile.empty()) {
            writeLanguageFile();
            return;
        }
        runEditor();
    }

private:
    Arguments args_;
    Services services_;
    String app_path_;
    System system_;
    structs::LaunchOption options_;
    EditorControlLaunchOptions control_options_;
    std::unique_ptr<CProps> cprops_;
    std::unique_ptr<Props> properties_;
    std::unique_ptr<lib::EditorRuntimeContext> runtime_context_;

    void initialize() {
        prepare_editor_resources();
        const Path argumentPath =
            args_.empty() ? Path{} : pathFromText(args_.front());
        Path executable = ::cwxeditor::cwx::utils::exeName(argumentPath);
        if (executable.empty() && !argumentPath.empty()) executable = argumentPath;
        if (executable.empty() && services_.system)
            executable = services_.system->executable_path();
        if (executable.empty())
            throw std::runtime_error("The editor executable path is unavailable.");
        app_path_ = absolutePathText(executable);
        if (app_path_.empty()) app_path_ = "cwxeditor";

        const Path applicationDirectory = pathFromText(app_path_).parent_path();
        lib::EditorRuntimeContext::StandaloneOptions runtimeOptions;
        runtimeOptions.app_root = applicationDirectory;
        runtimeOptions.capabilities = lib::probe_capabilities(
            services_, services_.gui ? services_.gui->platform()
                                     : lib::Platform::Automatic);
        runtimeOptions.services_factory = [services = services_]() mutable {
            return services;
        };
        runtime_context_ = std::make_unique<lib::EditorRuntimeContext>(
            lib::make_standalone_editor_runtime(std::move(runtimeOptions)));
        const auto validation = runtime_context_->validate();
        if (!validation.ok) {
            throw std::runtime_error(validation.errors.empty()
                ? "The editor runtime context is invalid."
                : validation.errors.front());
        }
        // validate() はサービス構成の完全性を調べ
        // る。書き込み可能な保存領域の初期化は別の実行結果
        // なので、 こちらも起動の条件にし、作れなかったパ
        // スのまま続行して起動成功と誤報しないようにする。
        if (runtime_context_->state() == lib::RuntimeState::StorageError) {
            const auto error = runtime_context_->last_error();
            throw std::runtime_error(error.empty()
                ? "The editor writable storage could not be initialized."
                : error);
        }
        services_ = runtime_context_->services();
        const auto userDirectory = runtime_context_->paths().user_root;
        ::cwxeditor::cwx::utils::debugLog =
            pathText(userDirectory / Path("Log") / Path("cwxeditor_error.log"));

        options_.conf = pathText(runtime_context_->storage_path(
            lib::LogicalRoot::Config, Path("cwxeditor.config")));
        Arguments optionArguments;
        if (args_.size() > 1U) {
            optionArguments.assign(args_.begin() + 1, args_.end());
        }
        control_options_ = editor_control_launch_options(optionArguments);
        control_options_.port = editorControlPort(control_options_.port);
        optionArguments = control_options_.remaining_arguments;
        options_.parseStrings(optionArguments);

        runtime_context_->lifecycle().on_persist([this] {
            if (!runtime_context_ || runtime_context_->is_shutdown()) return;
            (void)runtime_context_->persist_storage();
        });
        lib::set_sdl_application_lifecycle_handler(
            services_, [this](const lib::LifecycleEvent event) {
                if (services_.audio) {
                    if (event == lib::LifecycleEvent::EnterBackground)
                        services_.audio->set_suspended(true);
                    else if (event == lib::LifecycleEvent::EnterForeground)
                        services_.audio->set_suspended(false);
                }
                if (runtime_context_ && !runtime_context_->is_shutdown())
                    runtime_context_->lifecycle().dispatch(event);
            });
        runtime_context_->lifecycle().dispatch(lib::LifecycleEvent::Ready);

        auto model = make_editor_model_session(*runtime_context_, system_, app_path_, options_.conf);
        cprops_ = std::move(model.core);
        properties_ = std::move(model.properties);
    }

    void showHelp() {
        auto& property = *properties_;
        commons::Commons common(&property, nullptr);
        try {
            const auto icon = property.images().load(lib::StringView{"icon.png"});
            textdialog::TextDialog dialog(
                services_, {}, static_cast<String>(property.requiredMsgs().dlgTitUsage),
                icon, static_cast<String>(property.requiredMsgs().usage), true, {}, &property);
            dialog.setImages({icon});
            (void)dialog.open();
            (void)dialog.close();
        } catch (const std::exception& error) {
            ::cwxeditor::cwx::utils::debugln(
                "help dialog: ", error.what());
        } catch (...) {
            ::cwxeditor::cwx::utils::debugln("help dialog failed");
        }
        if (!services_.gui) std::cout << static_cast<String>(property.requiredMsgs().usage) << '\n';
        property.images().disposeImages();
        common.dispose();
    }

    void writeLanguageFile() {
        try {
            Path file = pathFromText(options_.putlangfile);
            file = ::cwxeditor::cwx::utils::nabs(std::move(file));
            const auto xml = propertyMessages().toXML(true);
            const auto* data = reinterpret_cast<const std::uint8_t*>(xml.data());
            ::cwxeditor::cwx::filesync::writeFile(
                file, ::cwxeditor::lib::ByteView(data, xml.size()), {}, {},
                runtime_context_ ? runtime_context_->storage_ptr() : nullptr);
        } catch (const std::exception& error) {
            ::cwxeditor::cwx::utils::debugln(
                "language file: ", error.what());
        } catch (...) {
            ::cwxeditor::cwx::utils::debugln("language file failed");
        }
    }

    const ::cwxeditor::cwx::msgs::Msgs& propertyMessages() const {
        return properties_->requiredMsgs();
    }

    void runEditor() {
        MainWindowStartup startup;
        startup.appPath = app_path_;
        startup.system = &system_;
        startup.properties = properties_.get();
        startup.options = options_;
        startup.services = services_;
        startup.runtimeContext = runtime_context_.get();

        const auto runner = mainWindowRunner();
        if (runner) {
            runner(startup);
            return;
        }

        lib::editor_action_log("Editor startup stage=main-window-context");
        mainwindow::MainWindowContext mainWindowContext;
        mainWindowContext.runtimeContext = runtime_context_.get();
        lib::editor_action_log("Editor startup stage=main-window-construct-begin");
        mainwindow::MainWindow window(app_path_, system_, *properties_,
                                      options_, services_,
                                      std::move(mainWindowContext));
        if (window.startupForwarded()) {
            lib::editor_action_log(
                "Editor startup forwarded to an existing scenario window");
            return;
        }
        struct SessionPersistenceGuard final {
            mainwindow::MainWindow& window;
            bool normal_exit{};
            ~SessionPersistenceGuard() {
                if (!normal_exit) window.suppressSessionStatePersistence();
            }
        } persistence{window};
        lib::editor_action_log("Editor startup stage=main-window-construct-end");
        startup.window = std::dynamic_pointer_cast<lib::Window>(window.shell());
        if (services_.gui && startup.window) {
            startup.window->title = window.title();
            startup.window->initially_hidden = environmentFlag("CWXEDITOR_CONTROL_HIDDEN");
            startup.window->activate_on_show =
                !startup.window->initially_hidden &&
                !environmentFlag("CWXEDITOR_CONTROL_NO_ACTIVATE");
            lib::editor_action_log("Editor startup stage=do-cwx-begin");
            window.doCWX();
            lib::editor_action_log("Editor startup stage=do-cwx-end");
            lib::editor_action_log("Editor startup stage=native-show-begin");
            services_.gui->show(startup.window);
            lib::editor_action_log("Editor startup stage=native-show-end");
            EditorControlServer control_server;
            if (control_options_.enabled) {
                try {
                    control_server.start([this, &window](String request) {
                        return invokeEditor(services_, [this, &window, request = std::move(request)]() mutable {
                            return editorCommand(window, services_, std::move(request));
                        });
                    }, control_options_.port);
                } catch (const std::exception& error) {
                    lib::editor_action_log(String("Editor control server disabled: ") + error.what());
                }
            } else {
                lib::editor_action_log("Editor control server disabled by command line");
            }
            // 表示状態は終了の合図ではない。GUIの自動検証はユーザーの
            // 前面フォーカスを奪わないよう、 意図的にネイティブウィンド
            // ウを隠して開始する。 通常の終了コールバックが設定する quitRequested()
            // を、寿命の正式な判定に使う。
            lib::editor_action_log(
                "Editor event loop begin quit=" +
                std::to_string(window.quitRequested() ? 1 : 0));
            bool fatal_loop_error{};
            String fatal_loop_message;
            String storage_error_reported;
            while (!window.quitRequested()) {
                try {
                    const lib::EditorPerformanceScope iteration_performance(
                        "editor.loop-iteration", 100000);
                    window.pollBackgroundLoads();
                    if (runtime_context_) {
                        (void)runtime_context_->poll_storage();
                        if (runtime_context_->state() == lib::RuntimeState::StorageError) {
                            const auto diagnostic = runtime_context_->last_error();
                            if (diagnostic != storage_error_reported) {
                                storage_error_reported = diagnostic;
                                window.setStatusLine(
                                    "保存領域への反映に失敗しました: " + diagnostic);
                            }
                        } else if (runtime_context_->state() ==
                                   lib::RuntimeState::Ready) {
                            storage_error_reported.clear();
                        }
                        window.pollStoragePersistence();
                    }
                    window.backupThr();
                    bool event_loop_alive = true;
                    window.dispatchUiEvent([&] {
                        event_loop_alive = services_.gui->pump_events(true);
                    });
                    if (!event_loop_alive) {
                        // 最後のネイティブウィンドウを閉じると、イベント処理も終
                        // 了する。 受理済みの終了なしに尽きた場合だけを異常とし
                        // 、終了処理は quitRequested を設定する。
                        fatal_loop_error = !window.quitRequested();
                        if (fatal_loop_error) fatal_loop_message =
                            "the platform event loop stopped before Editor closed";
                        lib::editor_action_log(
                            "Editor event loop stopped: platform pump returned false");
                        break;
                    }
                    window.uiDispatchComplete();
                } catch (const lib::RecoverableUiError& error) {
                    lib::editor_action_log(String("Editor operation rolled back: ") + error.what());
                    try { window.setStatusLine(String("操作を取り消しました: ") + error.what()); }
                    catch (...) {}
                } catch (const std::exception& error) {
                    fatal_loop_error = true;
                    fatal_loop_message = error.what();
                    lib::editor_action_log(
                        String("Editor event loop exception: ") + error.what());
                    ::cwxeditor::cwx::utils::debugln(
                        "editor event loop: ", error.what());
                    break;
                } catch (...) {
                    fatal_loop_error = true;
                    fatal_loop_message = "unknown exception";
                    lib::editor_action_log(
                        "Editor event loop exception: unknown exception");
                    ::cwxeditor::cwx::utils::debugln(
                        "editor event loop: unknown exception");
                    break;
                }
            }
            lib::editor_action_log(
                "Editor event loop end quit=" +
                std::to_string(window.quitRequested() ? 1 : 0));
            // ローダー・制御要求が、このループを同期的に待っている場合がある
            // 。 サービスの終了待ちやモデルの破棄より先にキャンセルする。
            if (services_.dispatcher) services_.dispatcher->shutdown();
            control_server.stop();
            services_.gui->close(startup.window);
            // MainWindow の終了処理が完了せずにOSのイベント処理が
            // 止まった場合も、異常なセッション終了とする。 境界・ドッキング・
            // 設定を確定できるのは、受理済みの exitAll() 要求だけ。
            if (!fatal_loop_error && window.quitRequested())
                persistence.normal_exit = true;
            if (fatal_loop_error) {
                // エディタループの異常終了を、プロセスの正常終了
                // に見せない。 この経路では、保存のガードが最
                // 後の正常なセッション設定を意図的に維持する。
                const auto recovery = window.preserveFatalRecoverySnapshot();
                const auto message = String("The editor event loop terminated unexpectedly: ") +
                    fatal_loop_message + (recovery.empty()
                        ? String(". No complete recovery snapshot was created.")
                        : String(". Recovery snapshot: ") + recovery);
                ::cwxeditor::cwx::utils::fdebugln(message);
                throw std::runtime_error(message);
            }
        } else if (!services_.gui) {
            throw std::runtime_error(
                "The editor requires a GUI platform service.");
        } else {
            throw std::runtime_error(
                "The editor platform backend is unavailable; headless services "
                "are test-only.");
        }
    }
};

}  // namespace

void setMainWindowRunner(MainWindowRunner runner) {
    std::scoped_lock lock(main_window_runner_mutex);
    main_window_runner = std::move(runner);
}

MainWindowRunner mainWindowRunner() {
    std::scoped_lock lock(main_window_runner_mutex);
    return main_window_runner;
}

void main(const Arguments& args) {
    mainImpl(args);
}

void mainImpl(const Arguments& args) {
    try {
        Application application(args, {});
        application.execute();
    } catch (const std::exception& error) {
        ::cwxeditor::cwx::utils::fdebugln(
            "cwxeditor startup failure: ", error);
        throw;
    } catch (...) {
        ::cwxeditor::cwx::utils::fdebugln(
            "cwxeditor startup failure: unknown exception");
        throw;
    }
}

int run(const int argc, char* const* argv) {
    Arguments args;
#if defined(_WIN32)
    int wideArgc = 0;
    if (wchar_t** wideArgv = CommandLineToArgvW(GetCommandLineW(), &wideArgc)) {
        args.reserve(static_cast<std::size_t>(wideArgc));
        for (int index = 0; index < wideArgc; ++index)
            args.push_back(utf8FromWide(wideArgv[index]));
        LocalFree(wideArgv);
    }
    if (args.empty()) {
#endif
    if (argc > 0 && argv != nullptr) {
        args.reserve(static_cast<std::size_t>(argc));
        for (int index = 0; index < argc; ++index) {
            if (argv[index] != nullptr) args.emplace_back(argv[index]);
            else args.emplace_back();
        }
    }
#if defined(_WIN32)
    }
#endif

    try {
        mainImpl(args);
        return 0;
    } catch (...) {
        return -1;
    }
}

}  // namespace cwxeditor::cwxeditor
