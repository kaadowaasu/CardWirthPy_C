#include "platform_transfer_apple.hpp"
#include "app/platform_apple.hpp"
#include "platform/bounded_stream_copy.hpp"
#include "platform/document_import_leases.hpp"

#include <SDL3/SDL.h>

#import <Foundation/Foundation.h>
#import <TargetConditionals.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#if TARGET_OS_IOS
#import <UIKit/UIKit.h>
#import <AVFoundation/AVFoundation.h>
#import <AVKit/AVKit.h>
#else
#import <AppKit/AppKit.h>
#endif

#include <map>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace cardwirth::explorer::apple {
namespace {

enum class ExportMode {
    ExactFile,
    NamedFileInFolder,
    NamedDirectoryInFolder,
    ContentsIntoFolder,
};

struct PendingExport {
    NSURL* __strong target{};
    ExportMode mode{ExportMode::ExactFile};
};

class SecurityScopedAccess final {
public:
    explicit SecurityScopedAccess(NSURL* url) : url_(url) {
        active_ = url_ && [url_ startAccessingSecurityScopedResource];
    }
    ~SecurityScopedAccess() {
        if (active_) [url_ stopAccessingSecurityScopedResource];
    }
    SecurityScopedAccess(const SecurityScopedAccess&) = delete;
    SecurityScopedAccess& operator=(const SecurityScopedAccess&) = delete;
private:
    NSURL* __strong url_{};
    bool active_{};
};

std::mutex pending_exports_mutex;
std::map<std::string, PendingExport> pending_exports;
cardwirth::platform::DocumentImportLeases pending_imports;

void run_on_apple_main(std::function<void()> action) {
    if ([NSThread isMainThread]) {
        action();
        return;
    }
    auto shared_action = std::make_shared<std::function<void()>>(std::move(action));
    dispatch_async(dispatch_get_main_queue(), ^{
        (*shared_action)();
    });
}

std::string path_to_utf8(const std::filesystem::path& value) {
    const auto encoded = value.u8string();
    return {reinterpret_cast<const char*>(encoded.data()), encoded.size()};
}

std::filesystem::path filesystem_path(NSURL* url) {
    const char* value = url.path.fileSystemRepresentation;
    return value && *value ? std::filesystem::path(value) : std::filesystem::path{};
}

std::string error_text(NSError* error, const char* fallback) {
    if (!error) return fallback;
    const char* value = error.localizedDescription.UTF8String;
    return value && *value ? std::string(value) : std::string(fallback);
}

void complete_on_main(NativePathSelectionCallback callback, NativePathSelection selection) {
    dispatch_async(dispatch_get_main_queue(), ^{
        callback(std::move(selection));
    });
}

NSArray<UTType*>* import_types(const ImportOptions& options) {
    // Swift の UTType.item/UTType.folder に対応するものは、このソースが使う
    // Objective-C API では UTTypeItem/UTTypeFolder として公開される。
    if (options.extensions.empty()) return @[UTTypeItem];
    NSMutableArray<UTType*>* types = [NSMutableArray array];
    for (const auto& extension : options.extensions) {
        UTType* type = [UTType typeWithFilenameExtension:
            [NSString stringWithUTF8String:extension.c_str()]];
        // これらの拡張子は表示候補の和集合を表す。未解決の型を
        // 一つ省くと、ほかの型を選択可能なまま、許可された文書
        // を黙って除外してしまう。制限なしの選択画面に切り替
        // え、選択後の取り込み・読み取り検証は引き続き行う。
        if (!type) return @[UTTypeItem];
        [types addObject:type];
    }
    return types.count ? types : @[UTTypeItem];
}

NSURL* new_staging_root(NSError** error) {
    NSURL* cache = [NSFileManager.defaultManager URLForDirectory:NSCachesDirectory
                                                        inDomain:NSUserDomainMask
                                               appropriateForURL:nil
                                                          create:YES
                                                           error:error];
    if (!cache) return nil;
    NSURL* root = [[[cache URLByAppendingPathComponent:@"CardWirthCTransfer" isDirectory:YES]
        URLByAppendingPathComponent:NSUUID.UUID.UUIDString isDirectory:YES] URLByStandardizingPath];
    if (![NSFileManager.defaultManager createDirectoryAtURL:root
                                withIntermediateDirectories:YES
                                                 attributes:nil
                                                      error:error]) return nil;
    NSError* backup_error = nil;
    if (![root setResourceValue:@YES forKey:NSURLIsExcludedFromBackupKey error:&backup_error])
        NSLog(@"CardWirthPy_C: transfer staging backup exclusion failed for %@: %@",
              root.path, backup_error);
    return root;
}

NSURL* unique_child(NSURL* root, NSString* name, bool directory) {
    NSFileManager* files = NSFileManager.defaultManager;
    NSString* safe_name = name.length ? name : @"item";
    NSURL* candidate = [root URLByAppendingPathComponent:safe_name isDirectory:directory];
    if (![files fileExistsAtPath:candidate.path]) return candidate;
    NSString* stem = safe_name.stringByDeletingPathExtension;
    NSString* extension = safe_name.pathExtension;
    for (NSUInteger suffix = 2; suffix < 10000; ++suffix) {
        NSString* numbered = [NSString stringWithFormat:@"%@ (%lu)", stem,
                               static_cast<unsigned long>(suffix)];
        if (extension.length) numbered = [numbered stringByAppendingPathExtension:extension];
        candidate = [root URLByAppendingPathComponent:numbered isDirectory:directory];
        if (![files fileExistsAtPath:candidate.path]) return candidate;
    }
    return [root URLByAppendingPathComponent:NSUUID.UUID.UUIDString isDirectory:directory];
}

bool coordinated_copy(NSURL* source, NSURL* destination, NSError** output_error,
                      const ImportOptions& options = {}) {
    __block NSError* copy_error = nil;
    NSFileCoordinator* coordinator = [[NSFileCoordinator alloc] initWithFilePresenter:nil];
    NSError* coordination_error = nil;
    [coordinator coordinateReadingItemAtURL:source
                                    options:0
                                      error:&coordination_error
                                 byAccessor:^(NSURL* readable) {
        if (options.max_file_bytes) {
            // 容量制限付きの即時取り込みは通常の文書だけを受け入れる。読み取
            // り・終了まで、セキュリティスコープとファイル調停を保持する。
            try {
                if (!std::filesystem::is_regular_file(filesystem_path(readable)))
                    throw std::runtime_error("通常ファイルを選択してください");
                std::ifstream input(filesystem_path(readable), std::ios::binary);
                std::ofstream output(filesystem_path(destination), std::ios::binary | std::ios::trunc);
                if (!input || !output) throw std::runtime_error("文書の一時ファイルを開けません");
                if (!cardwirth::platform::copy_bounded_stream(input, output,
                        options.max_file_bytes, options.cancelled))
                    throw std::runtime_error("文書の読み込みを取り消しました");
                output.close();
                if (!output) throw std::runtime_error("文書の一時ファイルを保存できません");
            } catch (const std::exception& error) {
                copy_error = [NSError errorWithDomain:@"CardWirthCTransfer" code:1
                    userInfo:@{NSLocalizedDescriptionKey:
                        [NSString stringWithUTF8String:error.what()] ?: @"文書を読み込めません"}];
            }
            return;
        }
        NSFileManager* files = NSFileManager.defaultManager;
        [files removeItemAtURL:destination error:nil];
        if (![files copyItemAtURL:readable toURL:destination error:&copy_error]) return;
    }];
    NSError* error = coordination_error ?: copy_error;
    if (error && output_error) *output_error = error;
    return error == nil;
}

void stage_imports(NSArray<NSURL*>* urls, NativePathSelectionCallback callback,
                   ImportOptions options = {}) {
    NSArray<NSURL*>* selected = [urls copy];
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        @autoreleasepool {
        NativePathSelection result;
        if (options.cancelled && options.cancelled()) {
            result.status = TransferStatus::Cancelled;
            result.cancelled = true;
            complete_on_main(std::move(callback), std::move(result));
            return;
        }
        NSError* error = nil;
        NSURL* root = new_staging_root(&error);
        if (!root) {
            result.status = TransferStatus::Failed;
            result.error = error_text(error, "一時領域を作成できません");
            complete_on_main(std::move(callback), std::move(result));
            return;
        }

        for (NSURL* source in selected) {
            if (options.cancelled && options.cancelled()) {
                result.status = TransferStatus::Cancelled;
                result.cancelled = true;
                result.paths.clear();
                [NSFileManager.defaultManager removeItemAtURL:root error:nil];
                complete_on_main(std::move(callback), std::move(result));
                return;
            }
            SecurityScopedAccess scoped(source);
            NSNumber* directory_value = nil;
            [source getResourceValue:&directory_value forKey:NSURLIsDirectoryKey error:nil];
            NSURL* destination = unique_child(root, source.lastPathComponent,
                                               directory_value.boolValue);
            NSError* copy_error = nil;
            const bool copied = coordinated_copy(source, destination, &copy_error, options);
            if (!copied) {
                result.cancelled = options.cancelled && options.cancelled();
                result.status = result.cancelled ? TransferStatus::Cancelled : TransferStatus::Failed;
                result.paths.clear();
                result.error = error_text(copy_error, "選択した項目を読み込めません");
                [NSFileManager.defaultManager removeItemAtURL:root error:nil];
                complete_on_main(std::move(callback), std::move(result));
                return;
            }
            result.paths.push_back(filesystem_path(destination));
        }
        result.status = result.paths.empty() ? TransferStatus::Failed : TransferStatus::Success;
        if (result.paths.empty()) {
            result.error = "選択した項目を読み込めません";
            [NSFileManager.defaultManager removeItemAtURL:root error:nil];
        } else {
            try {
                pending_imports.register_paths(filesystem_path(root), result.paths);
            } catch (const std::exception& failure) {
                result.status = TransferStatus::Failed;
                result.paths.clear();
                result.error = failure.what();
                [NSFileManager.defaultManager removeItemAtURL:root error:nil];
            }
        }
        complete_on_main(std::move(callback), std::move(result));
        }
    });
}

void cancel(NativePathSelectionCallback callback) {
    NativePathSelection result;
    result.status = TransferStatus::Cancelled;
    result.cancelled = true;
    callback(std::move(result));
}

void fail(NativePathSelectionCallback callback, std::string message) {
    NativePathSelection result;
    result.status = TransferStatus::Failed;
    result.error = std::move(message);
    callback(std::move(result));
}

void prepare_export(NSURL* target, ExportMode mode,
                    const std::filesystem::path& suggested_name,
                    NativePathSelectionCallback callback) {
    NSError* error = nil;
    NSURL* root = new_staging_root(&error);
    if (!root) {
        fail(std::move(callback), error_text(error, "書き出し用の一時領域を作成できません"));
        return;
    }
    NSString* name = [NSString stringWithUTF8String:path_to_utf8(suggested_name.filename()).c_str()];
    if (!name.length) name = @"download";
    NSURL* staging = nil;
    if (mode == ExportMode::ContentsIntoFolder) {
        staging = [root URLByAppendingPathComponent:@"contents" isDirectory:YES];
        if (![NSFileManager.defaultManager createDirectoryAtURL:staging
                                    withIntermediateDirectories:YES
                                                     attributes:nil
                                                          error:&error]) {
            fail(std::move(callback), error_text(error, "書き出し用フォルダを作成できません"));
            return;
        }
    } else {
        staging = [root URLByAppendingPathComponent:name
                                         isDirectory:mode == ExportMode::NamedDirectoryInFolder];
    }
    const auto key = path_to_utf8(filesystem_path(staging));
    {
        std::lock_guard lock(pending_exports_mutex);
        pending_exports[key] = PendingExport{target, mode};
    }
    NativePathSelection result;
    result.status = TransferStatus::Success;
    result.paths.push_back(filesystem_path(staging));
    callback(std::move(result));
}

bool copy_export_item(NSURL* source, NSURL* destination, NSError** output_error) {
    __block NSError* write_error = nil;
    NSFileCoordinator* coordinator = [[NSFileCoordinator alloc] initWithFilePresenter:nil];
    NSError* coordination_error = nil;
    [coordinator coordinateWritingItemAtURL:destination
                                    options:NSFileCoordinatorWritingForReplacing
                                      error:&coordination_error
                                 byAccessor:^(NSURL* writable) {
        NSFileManager* files = NSFileManager.defaultManager;
        NSURL* parent = writable.URLByDeletingLastPathComponent;
        NSString* temporary_name = [@".cardwirth-export-"
            stringByAppendingString:NSUUID.UUID.UUIDString];
        NSURL* temporary = [parent URLByAppendingPathComponent:temporary_name
                                                   isDirectory:NO];
        [files removeItemAtURL:temporary error:nil];
        if (![files copyItemAtURL:source toURL:temporary error:&write_error]) return;

        if ([files fileExistsAtPath:writable.path]) {
            NSURL* resulting = nil;
            if (![files replaceItemAtURL:writable
                           withItemAtURL:temporary
                          backupItemName:nil
                                 options:0
                        resultingItemURL:&resulting
                                   error:&write_error]) {
                [files removeItemAtURL:temporary error:nil];
            }
        } else if (![files moveItemAtURL:temporary toURL:writable error:&write_error]) {
            [files removeItemAtURL:temporary error:nil];
        }
    }];
    NSError* error = coordination_error ?: write_error;
    if (error && output_error) *output_error = error;
    return error == nil;
}

#if !TARGET_OS_IOS
NSWindow* parent_window(SDL_Window* window) {
    if (!window) return nil;
    return (__bridge NSWindow*)SDL_GetPointerProperty(
        SDL_GetWindowProperties(window), SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
}

void finish_panel(NSModalResponse response, NSArray<NSURL*>* urls,
                  NativePathSelectionCallback callback, ImportOptions options = {}) {
    if (response != NSModalResponseOK) {
        cancel(std::move(callback));
        return;
    }
    stage_imports(urls, std::move(callback), std::move(options));
}

void show_open_panel(SDL_Window* parent, bool directories,
                     NativePathSelectionCallback callback, ImportOptions options = {}) {
    NSCAssert([NSThread isMainThread], @"Apple UI must run on the main thread");
    if (options.cancelled && options.cancelled()) { cancel(std::move(callback)); return; }
    NSOpenPanel* panel = NSOpenPanel.openPanel;
    panel.canChooseFiles = !directories;
    panel.canChooseDirectories = directories;
    panel.allowsMultipleSelection = options.multiple;
    panel.resolvesAliases = YES;
    if (!directories && !options.extensions.empty()) panel.allowedContentTypes = import_types(options);
    panel.title = directories ? @"アップロードするフォルダを選択"
                              : @"アップロードするファイルを選択";
    if (!options.title.empty()) panel.title = [NSString stringWithUTF8String:options.title.c_str()];
    void (^completion)(NSModalResponse) = ^(NSModalResponse response) {
        finish_panel(response, panel.URLs, std::move(callback), options);
    };
    if (NSWindow* owner = parent_window(parent)) [panel beginSheetModalForWindow:owner completionHandler:completion];
    else [panel beginWithCompletionHandler:completion];
}

void show_folder_panel(SDL_Window* parent, const std::filesystem::path& suggested,
                       ExportMode mode, NativePathSelectionCallback callback) {
    NSCAssert([NSThread isMainThread], @"Apple UI must run on the main thread");
    NSOpenPanel* panel = NSOpenPanel.openPanel;
    panel.canChooseFiles = NO;
    panel.canChooseDirectories = YES;
    panel.allowsMultipleSelection = NO;
    panel.canCreateDirectories = YES;
    panel.title = @"ダウンロード先のフォルダを選択";
    void (^completion)(NSModalResponse) = ^(NSModalResponse response) {
        if (response != NSModalResponseOK || !panel.URL) {
            cancel(std::move(callback));
            return;
        }
        prepare_export(panel.URL, mode, suggested, std::move(callback));
    };
    if (NSWindow* owner = parent_window(parent)) [panel beginSheetModalForWindow:owner completionHandler:completion];
    else [panel beginWithCompletionHandler:completion];
}
#endif

}  // namespace

#if TARGET_OS_IOS

}  // namespace cardwirth::explorer::apple

namespace {

enum class PickerRequest { UploadFiles, UploadFolders, SaveFile, SaveDirectory, DownloadFolder };

UIViewController* presenting_controller() {
    NSCAssert([NSThread isMainThread], @"Apple UI must run on the main thread");
    UIWindow* window = nil;
    for (UIScene* scene in UIApplication.sharedApplication.connectedScenes) {
        if (scene.activationState != UISceneActivationStateForegroundActive ||
            ![scene isKindOfClass:UIWindowScene.class]) continue;
        for (UIWindow* candidate in ((UIWindowScene*)scene).windows) {
            if (candidate.isKeyWindow) { window = candidate; break; }
        }
        if (!window) {
            for (UIWindow* candidate in ((UIWindowScene*)scene).windows) {
                if (!candidate.isHidden && candidate.windowLevel == UIWindowLevelNormal) {
                    window = candidate;
                    break;
                }
            }
        }
        if (window) break;
    }
    UIViewController* controller = window.rootViewController;
    for (;;) {
        if (controller.presentedViewController) {
            controller = controller.presentedViewController;
            continue;
        }
        if ([controller isKindOfClass:UINavigationController.class]) {
            controller = ((UINavigationController*)controller).visibleViewController;
            continue;
        }
        if ([controller isKindOfClass:UITabBarController.class]) {
            controller = ((UITabBarController*)controller).selectedViewController;
            continue;
        }
        break;
    }
    return controller;
}

AVPlayerViewController* active_video_controller;

}  // namespace

@interface CardWirthDocumentPicker : NSObject <UIDocumentPickerDelegate> {
@private
    PickerRequest _request;
    std::filesystem::path _suggested;
    cardwirth::explorer::apple::ImportOptions _options;
    cardwirth::explorer::NativePathSelectionCallback _callback;
    BOOL _active;
}
- (void)begin:(PickerRequest)request
     suggested:(std::filesystem::path)suggested
       options:(cardwirth::explorer::apple::ImportOptions)options
      callback:(cardwirth::explorer::NativePathSelectionCallback)callback;
- (BOOL)takeCallback:(cardwirth::explorer::NativePathSelectionCallback*)callback;
@end

@implementation CardWirthDocumentPicker

+ (instancetype)sharedPicker {
    static CardWirthDocumentPicker* picker;
    static dispatch_once_t once;
    dispatch_once(&once, ^{ picker = [[CardWirthDocumentPicker alloc] init]; });
    return picker;
}

- (void)begin:(PickerRequest)request
     suggested:(std::filesystem::path)suggested
       options:(cardwirth::explorer::apple::ImportOptions)options
      callback:(cardwirth::explorer::NativePathSelectionCallback)callback {
    if (options.cancelled && options.cancelled()) {
        cardwirth::explorer::apple::cancel(std::move(callback));
        return;
    }
    if (_active) {
        cardwirth::explorer::apple::fail(
            std::move(callback), "別のファイル選択が開いています");
        return;
    }
    UIViewController* presenter = presenting_controller();
    if (!presenter) {
        cardwirth::explorer::apple::fail(
            std::move(callback), "iOSファイル選択を表示できません");
        return;
    }
    _active = YES;
    _request = request;
    _suggested = std::move(suggested);
    _options = std::move(options);
    _callback = std::move(callback);
    const bool folders = request != PickerRequest::UploadFiles;
    UIDocumentPickerViewController* picker = [[UIDocumentPickerViewController alloc]
        initForOpeningContentTypes:(folders ? @[UTTypeFolder] :
            cardwirth::explorer::apple::import_types(_options)) asCopy:NO];
    picker.delegate = self;
    picker.allowsMultipleSelection = _options.multiple &&
        (request == PickerRequest::UploadFiles || request == PickerRequest::UploadFolders);
    if (!_options.title.empty()) picker.title = [NSString stringWithUTF8String:_options.title.c_str()];
    picker.modalPresentationStyle = UIModalPresentationFormSheet;
    [presenter presentViewController:picker animated:YES completion:nil];
}

- (BOOL)takeCallback:(cardwirth::explorer::NativePathSelectionCallback*)callback {
    if (!_active || !callback) return NO;
    _active = NO;
    *callback = std::move(_callback);
    _callback = {};
    _suggested.clear();
    _options = {};
    return YES;
}

- (void)documentPickerWasCancelled:(UIDocumentPickerViewController*)controller {
    (void)controller;
    cardwirth::explorer::NativePathSelectionCallback callback;
    if ([self takeCallback:&callback] && callback)
        cardwirth::explorer::apple::cancel(std::move(callback));
}

- (void)documentPicker:(UIDocumentPickerViewController*)controller
    didPickDocumentsAtURLs:(NSArray<NSURL*>*)urls {
    (void)controller;
    if (!_active) return;
    const auto request = _request;
    auto suggested = std::move(_suggested);
    auto options = std::move(_options);
    cardwirth::explorer::NativePathSelectionCallback callback;
    if (![self takeCallback:&callback] || !callback) return;
    if (!urls.count) {
        cardwirth::explorer::apple::cancel(std::move(callback));
        return;
    }
    if (request == PickerRequest::UploadFiles || request == PickerRequest::UploadFolders) {
        cardwirth::explorer::apple::stage_imports(urls, std::move(callback), std::move(options));
        return;
    }
    auto mode = cardwirth::explorer::apple::ExportMode::ContentsIntoFolder;
    if (request == PickerRequest::SaveFile)
        mode = cardwirth::explorer::apple::ExportMode::NamedFileInFolder;
    else if (request == PickerRequest::SaveDirectory)
        mode = cardwirth::explorer::apple::ExportMode::NamedDirectoryInFolder;
    cardwirth::explorer::apple::prepare_export(
        urls.firstObject, mode, suggested, std::move(callback));
}

@end

namespace cardwirth::explorer::apple {
namespace {

void begin_ios(PickerRequest request, const std::filesystem::path& suggested,
               NativePathSelectionCallback callback, ImportOptions options = {}) {
    run_on_apple_main([request, suggested, callback = std::move(callback), options = std::move(options)]() mutable {
        [[CardWirthDocumentPicker sharedPicker] begin:request suggested:suggested
                                            options:std::move(options)
                                           callback:std::move(callback)];
    });
}

}  // namespace

#endif

void choose_import_files(SDL_Window* parent, ImportOptions options,
                         NativePathSelectionCallback callback) {
#if TARGET_OS_IOS
    (void)parent;
    begin_ios(PickerRequest::UploadFiles, {}, std::move(callback), std::move(options));
#else
    if (![NSThread isMainThread]) {
        run_on_apple_main([parent, callback = std::move(callback), options = std::move(options)]() mutable {
            choose_import_files(parent, std::move(options), std::move(callback));
        });
        return;
    }
    show_open_panel(parent, false, std::move(callback), std::move(options));
#endif
}

void choose_upload_files(SDL_Window* parent, NativePathSelectionCallback callback) {
    choose_import_files(parent, {}, std::move(callback));
}

bool play_preview_video(const std::filesystem::path& path) {
#if TARGET_OS_IOS
    if (![NSThread isMainThread]) {
        run_on_apple_main([path] { (void)play_preview_video(path); });
        return true;
    }
    stop_preview_video();
    UIViewController* presenter = presenting_controller();
    if (!presenter || path.empty()) return false;
    NSString* value = [NSString stringWithUTF8String:path_to_utf8(path).c_str()];
    if (!value) return false;
    NSURL* url = [NSURL fileURLWithPath:value];
    if (!url) return false;
    AVPlayerViewController* controller = [[AVPlayerViewController alloc] init];
    controller.player = [AVPlayer playerWithURL:url];
    controller.modalPresentationStyle = UIModalPresentationFullScreen;
    active_video_controller = controller;
    [presenter presentViewController:controller animated:YES completion:^{
        // 表示アニメーションの途中に、置換プレビューが
        // このコントローラーを閉じている場合がある。
        if (active_video_controller == controller) [controller.player play];
    }];
    return true;
#else
    (void)path;
    return false;
#endif
}

void stop_preview_video() noexcept {
#if TARGET_OS_IOS
    if (![NSThread isMainThread]) {
        run_on_apple_main([] { stop_preview_video(); });
        return;
    }
    AVPlayerViewController* controller = active_video_controller;
    active_video_controller = nil;
    if (!controller) return;
    [controller.player pause];
    controller.player = nil;
    if (controller.presentingViewController)
        [controller dismissViewControllerAnimated:YES completion:nil];
#endif
}

void choose_upload_folders(SDL_Window* parent, NativePathSelectionCallback callback) {
#if TARGET_OS_IOS
    (void)parent;
    begin_ios(PickerRequest::UploadFolders, {}, std::move(callback));
#else
    if (![NSThread isMainThread]) {
        run_on_apple_main([parent, callback = std::move(callback)]() mutable {
            choose_upload_folders(parent, std::move(callback));
        });
        return;
    }
    show_open_panel(parent, true, std::move(callback));
#endif
}

void choose_download_path(SDL_Window* parent, const std::filesystem::path& suggested_name,
                          bool directory, NativePathSelectionCallback callback) {
#if TARGET_OS_IOS
    (void)parent;
    begin_ios(directory ? PickerRequest::SaveDirectory : PickerRequest::SaveFile,
              suggested_name, std::move(callback));
#else
    if (![NSThread isMainThread]) {
        run_on_apple_main([parent, suggested_name, directory,
                           callback = std::move(callback)]() mutable {
            choose_download_path(parent, suggested_name, directory, std::move(callback));
        });
        return;
    }
    if (directory) {
        show_folder_panel(parent, suggested_name, ExportMode::NamedDirectoryInFolder,
                          std::move(callback));
        return;
    }
    NSCAssert([NSThread isMainThread], @"Apple UI must run on the main thread");
    NSSavePanel* panel = NSSavePanel.savePanel;
    panel.canCreateDirectories = YES;
    panel.nameFieldStringValue = [NSString stringWithUTF8String:
        path_to_utf8(suggested_name.filename()).c_str()];
    panel.title = @"ファイルをダウンロード";
    void (^completion)(NSModalResponse) = ^(NSModalResponse response) {
        if (response != NSModalResponseOK || !panel.URL) {
            cancel(std::move(callback));
            return;
        }
        prepare_export(panel.URL, ExportMode::ExactFile, suggested_name, std::move(callback));
    };
    if (NSWindow* owner = parent_window(parent)) [panel beginSheetModalForWindow:owner completionHandler:completion];
    else [panel beginWithCompletionHandler:completion];
#endif
}

void choose_download_folder(SDL_Window* parent, NativePathSelectionCallback callback) {
#if TARGET_OS_IOS
    (void)parent;
    begin_ios(PickerRequest::DownloadFolder, {}, std::move(callback));
#else
    if (![NSThread isMainThread]) {
        run_on_apple_main([parent, callback = std::move(callback)]() mutable {
            choose_download_folder(parent, std::move(callback));
        });
        return;
    }
    show_folder_panel(parent, {}, ExportMode::ContentsIntoFolder, std::move(callback));
#endif
}

void finalize_download_path(const std::filesystem::path& path, bool success,
                            FinalizeDownloadCallback callback) {
    const auto key = path_to_utf8(path);
    PendingExport pending;
    {
        std::lock_guard lock(pending_exports_mutex);
        const auto found = pending_exports.find(key);
        if (found == pending_exports.end()) {
            callback(false, "Apple書き出しの一時情報が見つかりません");
            return;
        }
        pending = found->second;
        pending_exports.erase(found);
    }
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        @autoreleasepool {
        NSURL* staging = [NSURL fileURLWithPath:
            [NSString stringWithUTF8String:path_to_utf8(path).c_str()]];
        NSURL* staging_root = staging.URLByDeletingLastPathComponent;
        if (!success) {
            [NSFileManager.defaultManager removeItemAtURL:staging_root error:nil];
            dispatch_async(dispatch_get_main_queue(), ^{ callback(true, {}); });
            return;
        }

        SecurityScopedAccess scoped(pending.target);
        NSError* error = nil;
        bool copied = true;
        bool recovery_failed = false;
        std::string recovery_errors;
        if (pending.mode == ExportMode::ContentsIntoFolder) {
            struct PublishedChild {
                NSURL* __strong destination{};
                NSURL* __strong backup{};
            };
            std::vector<PublishedChild> published;
            NSURL* rollback_root = [staging_root URLByAppendingPathComponent:@"rollback"
                                                                  isDirectory:YES];
            if (![NSFileManager.defaultManager createDirectoryAtURL:rollback_root
                                         withIntermediateDirectories:YES
                                                          attributes:nil
                                                               error:&error]) copied = false;
            NSArray<NSURL*>* children = [NSFileManager.defaultManager
                contentsOfDirectoryAtURL:staging
               includingPropertiesForKeys:nil
                                  options:0
                                    error:&error];
            if (children && copied) {
                for (NSURL* child in children) {
                    NSURL* destination = [pending.target
                        URLByAppendingPathComponent:child.lastPathComponent
                                       isDirectory:NO];
                    NSURL* backup = nil;
                    if ([NSFileManager.defaultManager fileExistsAtPath:destination.path]) {
                        backup = unique_child(rollback_root, child.lastPathComponent, false);
                        if (!coordinated_copy(destination, backup, &error)) {
                            copied = false;
                            break;
                        }
                    }
                    published.push_back(PublishedChild{destination, backup});
                    // プロバイダーは出力先を変えた後に失敗し得る。先に公開
                    // できた項目だけでなく、試行した項目も対象に含める。
                    if (!copy_export_item(child, destination, &error)) { copied = false; break; }
                }
            } else copied = false;
            if (!copied) {
                // プロバイダーがオフラインになると、公開も復元も失敗
                // し得る。巻き戻しが不完全なら元のデータを保持する。
                for (auto item = published.rbegin(); item != published.rend(); ++item) {
                    NSError* rollback_error = nil;
                    if (item->backup) {
                        if (!copy_export_item(item->backup, item->destination, &rollback_error))
                            recovery_failed = true;
                    } else {
                        [NSFileManager.defaultManager removeItemAtURL:item->destination
                                                                error:&rollback_error];
                        if ([rollback_error.domain isEqualToString:NSCocoaErrorDomain] &&
                            rollback_error.code == NSFileNoSuchFileError) rollback_error = nil;
                        if (rollback_error) recovery_failed = true;
                    }
                    if (rollback_error) {
                        recovery_errors += "\n" + path_to_utf8(filesystem_path(item->destination)) +
                            ": " + error_text(rollback_error, "復元できません");
                    }
                }
            }
        } else {
            NSURL* destination = pending.target;
            if (pending.mode != ExportMode::ExactFile) {
                destination = [pending.target URLByAppendingPathComponent:staging.lastPathComponent
                                                              isDirectory:pending.mode == ExportMode::NamedDirectoryInFolder];
            }
            copied = copy_export_item(staging, destination, &error);
        }
        std::string message = copied ? std::string{} :
            error_text(error, "選択した保存先へ書き込めません");
        if (recovery_failed) {
            // キャッシュは OS に削除され得るため、永続的な UserData 領域を優
            // 先する。移動に失敗した場合はキャッシュのルートを残し、下の整理から除外する。
            const auto user_root = cardwirth::app::apple::writable_user_root();
            NSURL* retained = staging_root;
            if (!user_root.empty()) {
                const auto recovery_path = user_root / "Backup" / "Recovery";
                NSURL* recovery = [NSURL fileURLWithPath:
                    [NSString stringWithUTF8String:path_to_utf8(recovery_path).c_str()]];
                NSError* retention_error = nil;
                if ([NSFileManager.defaultManager createDirectoryAtURL:recovery
                                             withIntermediateDirectories:YES
                                                              attributes:nil
                                                                   error:&retention_error]) {
                    NSURL* destination = [recovery URLByAppendingPathComponent:NSUUID.UUID.UUIDString
                                                                  isDirectory:YES];
                    if ([NSFileManager.defaultManager moveItemAtURL:staging_root
                                                             toURL:destination error:&retention_error])
                        retained = destination;
                }
                if (retention_error) recovery_errors += "\n" +
                    error_text(retention_error, "復旧データの移動に失敗しました");
            }
            message += "\n一部の復元に失敗しました。復旧データは削除していません: " +
                path_to_utf8(filesystem_path(retained)) + recovery_errors;
        } else {
            [NSFileManager.defaultManager removeItemAtURL:staging_root error:nil];
        }
        dispatch_async(dispatch_get_main_queue(), ^{ callback(copied, message); });
        }
    });
}

void release_upload_paths(const std::vector<std::filesystem::path>& paths) {
    std::set<std::filesystem::path> roots;
    for (const auto& path : paths) {
        if (const auto root = pending_imports.release(path)) roots.insert(*root);
    }
    if (roots.empty()) return;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), ^{
        @autoreleasepool {
        for (const auto& root : roots) {
            NSString* value = [NSString stringWithUTF8String:path_to_utf8(root).c_str()];
            if (value) [NSFileManager.defaultManager removeItemAtPath:value error:nil];
        }
        }
    });
}

void cleanup_stale_transfers() {
    // ファイルプロバイダーのコールバックは OS に終了される場合がある。実行中の書き出し
    // の UUID は保持し、放置された一日以上前のキャッシュルートを起動時に回収する。
    std::set<std::filesystem::path> active_roots = pending_imports.active_roots();
    {
        std::lock_guard lock(pending_exports_mutex);
        for (const auto& [path, pending] : pending_exports) {
            (void)pending;
            active_roots.insert(std::filesystem::path(path).parent_path().lexically_normal());
        }
    }
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), ^{
        @autoreleasepool {
            NSFileManager* files = NSFileManager.defaultManager;
            NSURL* cache = [files URLForDirectory:NSCachesDirectory
                                          inDomain:NSUserDomainMask
                                 appropriateForURL:nil
                                            create:NO
                                             error:nil];
            if (!cache) return;
            NSURL* transfer = [cache URLByAppendingPathComponent:@"CardWirthCTransfer"
                                                     isDirectory:YES];
            NSArray<NSURL*>* children = [files contentsOfDirectoryAtURL:transfer
                                             includingPropertiesForKeys:@[NSURLContentModificationDateKey]
                                                                options:NSDirectoryEnumerationSkipsHiddenFiles
                                                                  error:nil];
            NSDate* cutoff = [NSDate dateWithTimeIntervalSinceNow:-(24.0 * 60.0 * 60.0)];
            for (NSURL* child in children) {
                const auto root = filesystem_path(child).lexically_normal();
                if (active_roots.contains(root)) continue;
                // 中断された書き出しと、復旧用の移動に失敗したものも含める。
                if ([files fileExistsAtPath:[[child URLByAppendingPathComponent:@"rollback"
                                                                      isDirectory:YES] path]]) continue;
                NSDate* modified = nil;
                [child getResourceValue:&modified forKey:NSURLContentModificationDateKey error:nil];
                if (modified && [modified compare:cutoff] == NSOrderedAscending)
                    [files removeItemAtURL:child error:nil];
            }
        }
    });
}

}  // namespace cardwirth::explorer::apple
