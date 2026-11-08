#import <Foundation/Foundation.h>
#import <AVFoundation/AVFoundation.h>
#include <stdlib.h>
#include <strings.h>

// Microphone access is only needed for TX (voice/SSB) and the space-key mic input.
// Requesting it makes macOS abort the process when the binary is launched outside a
// .app bundle (TCC: "attempted to access privacy-sensitive data without a usage
// description"), which breaks headless, CI and e2e runs. Set SDRPP_MIC_ACCESS=skip
// to leave the permission alone.
//
// An unbundled binary has no Info.plist, so it has no NSMicrophoneUsageDescription.
// Any TCC request from it aborts the whole process in a background tccd callback
// (SIGABRT with no message). Therefore the default is "skip" unless we run from a
// bundle; SDRPP_MIC_ACCESS=grant forces the request anyway.
static int runningFromBundle() {
    NSBundle* bundle = [NSBundle mainBundle];
    if (bundle == nil) { return 0; }
    NSString* path = [bundle bundlePath];
    if (path == nil) { return 0; }
    return [[path pathExtension] isEqualToString:@"app"] ? 1 : 0;
}

static int micAccessRequested() {
    const char* v = getenv("SDRPP_MIC_ACCESS");
    if (v == NULL || v[0] == '\0') {
        if (!runningFromBundle()) {
            NSLog(@"MACOS: microphone access skipped: not running from an .app bundle, "
                   "a TCC request would abort the process. Set SDRPP_MIC_ACCESS=grant to force it.");
            return 0;
        }
        return 1;
    }
    if (!strcasecmp(v, "skip") || !strcasecmp(v, "none") || !strcasecmp(v, "off") ||
        !strcasecmp(v, "no") || !strcasecmp(v, "0") || !strcasecmp(v, "false")) {
        return 0;
    }
    if (!strcasecmp(v, "grant") || !strcasecmp(v, "request") || !strcasecmp(v, "on") ||
        !strcasecmp(v, "yes") || !strcasecmp(v, "1") || !strcasecmp(v, "true")) {
        return 1;
    }
    NSLog(@"MACOS: unknown SDRPP_MIC_ACCESS value '%s', requesting audio permission", v);
    return 1;
}

static void doMacosInit() {
    @autoreleasepool {
        if (!micAccessRequested()) {
            NSLog(@"MACOS: microphone access request skipped (SDRPP_MIC_ACCESS)");
            return;
        }

        switch ([AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio]) {
            case AVAuthorizationStatusAuthorized:
                NSLog(@"MACOS: Application already has been granted audio recording permission!");
                break;

            case AVAuthorizationStatusNotDetermined:
            {
                if (!runningFromBundle()) {
                    NSLog(@"MACOS: microphone permission not determined and not running from an "
                           ".app bundle, not requesting it (TCC would abort the process).");
                    break;
                }
                [AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio completionHandler:^(BOOL granted) {
                    if (granted) {
                        NSLog(@"User has granted audio permission to the application: requestAccessForMediaType:true");
                    } else {
                        NSLog(@"User has denied audio permission to the application: : requestAccessForMediaType:false");
                    }
                }];
                break;
            }

            case AVAuthorizationStatusDenied:
                NSLog(@"User has just denied audio permission to the application: AVAuthorizationStatusDenied");
                break;

            case AVAuthorizationStatusRestricted:
                NSLog(@"Apppication does not have access to media capture devices: AVAuthorizationStatusRestricted");
                break;

            default:
                break;
        }

        // The rest of your code
//        NSLog(@"Hello, World!");
    }
}

void macosInit() {
    doMacosInit();
}
