#include "ios_data_folder.h"
#import <Foundation/Foundation.h>

#if !__has_feature(objc_arc)
#error "ios_data_folder.mm requires ARC."
#endif

// Shared with the launcher, which writes the bookmark.
extern NSString* const IOSDataFolderBookmarkKey = @"data.folder.bookmark";

static bool g_unavailable;

std::filesystem::path ios_data_folder::Resolve()
{
    NSUserDefaults* defaults = NSUserDefaults.standardUserDefaults;
    NSData* bookmark = [defaults dataForKey:IOSDataFolderBookmarkKey];
    if (bookmark == nil)
        return {};

    BOOL stale = NO;
    NSError* error = nil;
    NSURL* url = [NSURL URLByResolvingBookmarkData:bookmark options:0 relativeToURL:nil bookmarkDataIsStale:&stale error:&error];
    // Access stays open for the process: the game reads and writes there until exit.
    if (url == nil || ![url startAccessingSecurityScopedResource])
    {
        g_unavailable = true;
        return {};
    }

    if (stale)
    {
        if (NSData* refreshed = [url bookmarkDataWithOptions:0 includingResourceValuesForKeys:nil relativeToURL:nil error:nil])
            [defaults setObject:refreshed forKey:IOSDataFolderBookmarkKey];
    }

    BOOL isDirectory = NO;
    if (![NSFileManager.defaultManager fileExistsAtPath:url.path isDirectory:&isDirectory] || !isDirectory)
    {
        g_unavailable = true;
        return {};
    }
    return std::filesystem::path(std::u8string_view((const char8_t*)url.fileSystemRepresentation));
}

bool ios_data_folder::IsUnavailable()
{
    return g_unavailable;
}
