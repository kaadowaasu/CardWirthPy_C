#include "apple_accessibility.hpp"
#include "ui_actions.hpp"

#include <SDL3/SDL.h>

#import <Foundation/Foundation.h>
#import <TargetConditionals.h>

#if TARGET_OS_IOS
#import <UIKit/UIKit.h>
#else
#import <AppKit/AppKit.h>
#endif

#include <string>
#include <algorithm>
#include <mutex>
#include <unordered_map>

// これらの要素は論理 Widget を所有せず、SDL の
// 描画面トークンだけで識別する。操作を送る直前に登録表から
// トークンを共通の弱参照に解決する。Objective-C
// の宣言は Objective-C++ でも大域スコー
// プに置く必要があるため、C++ 型を明示的に修飾する。
#if TARGET_OS_IOS
@interface CWXUIKitAccessibilityElement : UIAccessibilityElement {
@public
    std::weak_ptr<cwxeditor::lib::PlatformAccessibilityBackend> backend;
    std::uint64_t surface;
}
@end

@implementation CWXUIKitAccessibilityElement
- (BOOL)accessibilityActivate {
    const auto transport=backend.lock();
    cwxeditor::lib::AccessibilityActionRequest request;
    request.action=cwxeditor::lib::AccessibilityAction::Invoke;
    return transport && transport->request(surface, request);
}
@end
#else
@interface CWXAppKitAccessibilityElement : NSAccessibilityElement {
@public
    std::weak_ptr<cwxeditor::lib::PlatformAccessibilityBackend> backend;
    std::uint64_t surface;
}
@end

@implementation CWXAppKitAccessibilityElement
- (BOOL)accessibilityPerformPress {
    const auto transport=backend.lock();
    cwxeditor::lib::AccessibilityActionRequest request;
    request.action=cwxeditor::lib::AccessibilityAction::Invoke;
    return transport && transport->request(surface, request);
}
@end
#endif

namespace cwxeditor::lib {
namespace {

NSString* native_text(std::string_view value) {
    if (value.empty()) return @"";
    return [[NSString alloc]
        initWithBytes:value.data()
               length:value.size()
             encoding:NSUTF8StringEncoding] ?: @"";
}

void detach_element(id host,id element) {
    if (!host || !element) return;
#if TARGET_OS_IOS
    // iOS ホストの型は UIView とする。型なしの id では、Objective-C++
    // から UIKit の accessibilityElements プロパティを参照できない。
    UIView* view=(UIView*)host;
    NSMutableArray* children=[view.accessibilityElements mutableCopy] ?: [NSMutableArray array];
    [children removeObjectIdenticalTo:element];
    view.accessibilityElements=children.count ? children : nil;
#else
    NSMutableArray* children=[host accessibilityChildren]
        ? [[host accessibilityChildren] mutableCopy] : [NSMutableArray array];
    [children removeObjectIdenticalTo:element];
    [host setAccessibilityChildren:(children.count ? children : nil)];
#endif
}

}  // namespace

class AppleAccessibilityBackend final : public PlatformAccessibilityBackend,
                                        public std::enable_shared_from_this<AppleAccessibilityBackend> {
public:
    void publish(const std::uint64_t surface, void* native_surface, const WidgetPtr& root,
                 const WidgetPtr& focused,
                 const AccessibilityPresentation& presentation) override {
        if (!surface || !native_surface || !root || !focused || !presentation.visible) {
            retire(surface);
            return;
        }
#if (TARGET_OS_IOS && !defined(SDL_PROP_WINDOW_UIKIT_WINDOW_POINTER)) || \
    (!TARGET_OS_IOS && !defined(SDL_PROP_WINDOW_COCOA_WINDOW_POINTER))
        // 古い SDL ヘッダーには公開された Apple のウィンドウプ
        // ロパティがない。内部構造を推測せず、操作連携が可能とも扱わない。
        retire(surface);
        return;
#else
        id host=nil;
#if TARGET_OS_IOS
        auto* window=(__bridge UIWindow*)SDL_GetPointerProperty(
            SDL_GetWindowProperties(static_cast<SDL_Window*>(native_surface)),
            SDL_PROP_WINDOW_UIKIT_WINDOW_POINTER,nullptr);
        host=window.rootViewController.view;
#else
        auto* window=(__bridge NSWindow*)SDL_GetPointerProperty(
            SDL_GetWindowProperties(static_cast<SDL_Window*>(native_surface)),
            SDL_PROP_WINDOW_COCOA_WINDOW_POINTER,nullptr);
        host=window.contentView;
#endif
        if (!host) { retire(surface); return; }
        Entry previous;
        {
            std::scoped_lock lock(mutex_);
            if (const auto found=entries_.find(surface); found!=entries_.end()) previous=found->second;
            entries_[surface] = {root, focused, presentation};
        }
        if (previous.host && previous.element && previous.host != (__bridge void*)host)
            detach_element((__bridge id)previous.host,(__bridge id)previous.element);
        Entry attached;
        attached.host=(__bridge void*)host;
#if TARGET_OS_IOS
        auto* element=[[CWXUIKitAccessibilityElement alloc] initWithAccessibilityContainer:host];
        element->backend=shared_from_this(); element->surface=surface;
        element.accessibilityLabel=native_text(presentation.name);
        element.accessibilityHint=native_text(presentation.description);
        // ここでも同じプロパティ取得に、具象 UIKit 型を使う。
        UIView* view=(UIView*)host;
        NSMutableArray* children=[view.accessibilityElements mutableCopy] ?: [NSMutableArray array];
        if (previous.element) [children removeObjectIdenticalTo:(__bridge id)previous.element];
        [children addObject:element]; view.accessibilityElements=children;
        attached.element=(__bridge void*)element;
#else
        auto* element=[CWXAppKitAccessibilityElement new];
        element->backend=shared_from_this(); element->surface=surface;
        element.accessibilityLabel=native_text(presentation.name);
        element.accessibilityHelp=native_text(presentation.description);
        element.accessibilityRole=NSAccessibilityButtonRole;
        NSMutableArray* children=[host accessibilityChildren]
            ? [[host accessibilityChildren] mutableCopy] : [NSMutableArray array];
        if (previous.element) [children removeObjectIdenticalTo:(__bridge id)previous.element];
        [children addObject:element]; [host setAccessibilityChildren:children];
        attached.element=(__bridge void*)element;
#endif
        {
            std::scoped_lock lock(mutex_);
            if (const auto found=entries_.find(surface); found!=entries_.end()) {
                found->second.host=attached.host; found->second.element=attached.element;
            }
        }
        // ネイティブ要素が見えていても、共通ホストが
        // その描画面トークンを受理するまでは操作可
        // 能とはしない。Apple の実要素が成功
        // を返すのに操作の送り先がない期間を防ぐ。
        if (!activate_host_action_surface(surface)) {
            retire(surface);
            return;
        }
        {
            std::scoped_lock lock(mutex_);
            if (const auto found=entries_.find(surface); found!=entries_.end())
                found->second.action_bridge=true;
        }
        // 実要素はこのトークンだけを保持する。ネイティブの
        // Press/Activate は、合成マウスイベ
        // ントではなく request() を通じて戻る。
        announce_apple_accessibility(presentation.name, presentation.description);
#endif
    }

    void retire(const std::uint64_t surface) noexcept override {
        if (!surface) return;
        retire_host_action_surface(surface);
        Entry entry;
        {
            std::scoped_lock lock(mutex_);
            const auto found=entries_.find(surface);
            if(found==entries_.end()) return;
            entry=found->second; entries_.erase(found);
        }
        detach_element((__bridge id)entry.host,(__bridge id)entry.element);
    }

    bool request(const std::uint64_t surface,
                 const AccessibilityActionRequest& request) override {
        Entry entry;
        {
            std::scoped_lock lock(mutex_);
            const auto found=entries_.find(surface);
            if (found==entries_.end() || !found->second.host || !found->second.element)
                return false;
            entry=found->second;
        }
        const auto root=entry.root.lock();
        const auto focused=entry.focused.lock();
        // ネイティブ要素は自身の現在の論理対象だけを操作でき
        // る。置換済みの描画面を復活させたり、古いトークンで
        // 同じルートの別 Widget を操作したりしない。
        if (!root || !focused || request.target ||
            !widget_in_input_scope(root,focused)) return false;
        auto normalized=request;
        normalized.target=focused;
        // ネイティブの受け渡し層は選択・操作の状態を所有しない。ホス
        // トのディスパッチャーを通し、SdlGuiBackend の
        // 既存の操作処理、スナップショット再公開、再描画に接続する。
        return dispatch_host_action(surface,normalized);
    }
    [[nodiscard]] bool action_bridge_available() const noexcept override {
        // 読み上げ専用、または登録解除済みのネイティブ要
        // 素は操作可能とはしない。実際に接続され、この登
        // 録表経由で要求を返せる間だけ対応を報告する。
        std::scoped_lock lock(mutex_);
        return std::any_of(entries_.begin(), entries_.end(), [](const auto& value) {
            return value.second.host != nullptr && value.second.element != nullptr &&
                   value.second.action_bridge;
        });
    }

private:
    struct Entry {
        std::weak_ptr<Widget> root;
        std::weak_ptr<Widget> focused;
        AccessibilityPresentation presentation;
        void* host{};
        void* element{};
        bool action_bridge{};
    };
    // action_bridge_available() は const だが、現在の登録表を調べる必要がある。
    mutable std::mutex mutex_;
    std::unordered_map<std::uint64_t,Entry> entries_;
};

void announce_apple_accessibility(std::string_view name,
                                  std::string_view description) {
    NSString* label = [native_text(name)
        stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet];
    NSString* detail = [native_text(description)
        stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet];
    NSString* message = @"";
    if (label.length && detail.length)
        message = [NSString stringWithFormat:@"%@. %@", label, detail];
    else if (label.length)
        message = label;
    else
        message = detail;
    if (!message.length) return;

    dispatch_async(dispatch_get_main_queue(), ^{
#if TARGET_OS_IOS
        UIAccessibilityPostNotification(UIAccessibilityAnnouncementNotification, message);
#else
        NSAccessibilityPostNotificationWithUserInfo(
            NSApp, NSAccessibilityAnnouncementRequestedNotification,
            @{NSAccessibilityAnnouncementKey: message,
              NSAccessibilityPriorityKey: @(NSAccessibilityPriorityMedium)});
#endif
    });
}

std::shared_ptr<PlatformAccessibilityBackend> make_apple_accessibility_backend() {
    return std::make_shared<AppleAccessibilityBackend>();
}

}  // namespace cwxeditor::lib
