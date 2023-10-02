#import <Foundation/Foundation.h>
#import <AVFoundation/AVFoundation.h>

static doMacosInit() {
    @autoreleasepool {
        switch ([AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio]) {
            case AVAuthorizationStatusAuthorized:
                NSLog(@"Already authorized");
                break;

            case AVAuthorizationStatusNotDetermined:
            {
                [AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio completionHandler:^(BOOL granted) {
                    if (granted) {
                        NSLog(@"User has granted audio permission.");
                    } else {
                        NSLog(@"User has denied audio permission.");
                    }
                }];
                break;
            }

            case AVAuthorizationStatusDenied:
                NSLog(@"User has denied permission");
                break;

            case AVAuthorizationStatusRestricted:
                NSLog(@"User does not have access to media capture devices.");
                break;

            default:
                break;
        }

        // The rest of your code
        NSLog(@"Hello, World!");
    }
}

void macosInit() {
    doMacosInit();
}
