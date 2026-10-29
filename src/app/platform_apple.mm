#include "platform_apple.hpp"

#import <Foundation/Foundation.h>

namespace cardwirth::app::apple {
namespace {

std::filesystem::path filesystem_path(NSString* value) {
    if (!value) return {};
    const char* encoded = value.fileSystemRepresentation;
    return encoded && *encoded ? std::filesystem::path(encoded) : std::filesystem::path{};
}

}  // namespace

std::filesystem::path bundle_resource_root() {
    @autoreleasepool {
        return filesystem_path(NSBundle.mainBundle.resourcePath);
    }
}

std::filesystem::path writable_user_root() {
    @autoreleasepool {
        NSFileManager* files = NSFileManager.defaultManager;
        NSURL* support = [files URLsForDirectory:NSApplicationSupportDirectory
                                       inDomains:NSUserDomainMask].firstObject;
        if (!support) return {};
        NSURL* root = [[support URLByAppendingPathComponent:@"CardWirthC" isDirectory:YES]
            URLByAppendingPathComponent:@"UserData" isDirectory:YES];
        NSError* error = nil;
        if (![files createDirectoryAtURL:root
             withIntermediateDirectories:YES
                              attributes:nil
                                   error:&error]) {
            NSLog(@"CardWirthPy_C: UserData directory creation failed: %@", error);
            return {};
        }
        return filesystem_path(root.path);
    }
}

bool exclude_from_backup(const std::filesystem::path& path) {
    @autoreleasepool {
        if (path.empty()) return false;
        const auto encoded = path.u8string();
        NSURL* url = [NSURL fileURLWithPath:
            [NSString stringWithUTF8String:
                reinterpret_cast<const char*>(encoded.c_str())]];
        if (!url) return false;
        NSError* error = nil;
        const bool ok = [url setResourceValue:@YES
                                       forKey:NSURLIsExcludedFromBackupKey
                                        error:&error];
        if (!ok) NSLog(@"CardWirthPy_C: backup exclusion failed for %@: %@", url.path, error);
        return ok;
    }
}

bool download_https_file(const std::string& address,
                         const std::filesystem::path& destination,
                         std::uint64_t limit) {
    @autoreleasepool {
        NSString* text = [NSString stringWithUTF8String:address.c_str()];
        NSURL* url = text ? [NSURL URLWithString:text] : nil;
        if (!url || ![url.scheme.lowercaseString isEqualToString:@"https"] ||
            ![url.host.lowercaseString isEqualToString:@"webcardwirth.net"]) return false;
        NSMutableURLRequest* request = [NSMutableURLRequest requestWithURL:url];
        request.timeoutInterval = 60;
        dispatch_semaphore_t finished = dispatch_semaphore_create(0);
        __block NSData* data = nil;
        __block NSHTTPURLResponse* response = nil;
        __block NSError* failure = nil;
        NSURLSessionConfiguration* configuration = NSURLSessionConfiguration.ephemeralSessionConfiguration;
        configuration.HTTPMaximumConnectionsPerHost = 2;
        NSURLSession* session = [NSURLSession sessionWithConfiguration:configuration];
        [[session dataTaskWithRequest:request completionHandler:^(NSData* body, NSURLResponse* reply, NSError* error) {
            data = body;
            response = [reply isKindOfClass:NSHTTPURLResponse.class] ? (NSHTTPURLResponse*)reply : nil;
            failure = error;
            dispatch_semaphore_signal(finished);
        }] resume];
        dispatch_semaphore_wait(finished, DISPATCH_TIME_FOREVER);
        [session finishTasksAndInvalidate];
        if (failure || response.statusCode != 200 || !data || data.length == 0 || data.length > limit ||
            ![response.URL.host.lowercaseString isEqualToString:@"webcardwirth.net"] ||
            ![response.URL.scheme.lowercaseString isEqualToString:@"https"]) return false;
        const auto encoded = destination.u8string();
        NSString* path = [NSString stringWithUTF8String:reinterpret_cast<const char*>(encoded.c_str())];
        return path && [data writeToFile:path atomically:YES];
    }
}

}  // namespace cardwirth::app::apple
