#import <Foundation/Foundation.h>
#import <AVFoundation/AVFoundation.h>
#include <stdlib.h>
#include <strings.h>

// Microphone access is only needed for TX (voice/SSB) and the space-key mic input.
// Requesting it makes macOS abort the process when the binary is launched outside a
// .app bundle (TCC: "attempted to access privacy-sensitive data without a usage
// description"), which breaks headless, CI and e2e runs. Set SDRPP_MIC_ACCESS=skip
// to leave the permission alone.
static int micAccessRequested() {
    const char* v = getenv("SDRPP_MIC_ACCESS");
    if (v == NULL || v[0] == '\0') { return 1; }
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
