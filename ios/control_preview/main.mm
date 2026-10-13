#import <UIKit/UIKit.h>
#include <atomic>
#include <cstdio>
#include <hid/touch_gamepad.h>

namespace hid {
static XAMINPUT_GAMEPAD previewState{};
void SetTouchControllerState(bool, const XAMINPUT_GAMEPAD& state) { previewState = state; }
}

// Generated from the game's UIKit implementation by build.py, without the
// game-window bootstrap. This preview shares the actual drawing and input code.
#include "production_touch_view.inc"

@interface PreviewController : UIViewController
@end
@implementation PreviewController {
    UnleashedTouchControlsView* _controls;
    CADisplayLink* _link;
    UILabel* _readout;
    BOOL _exported;
}
- (BOOL)prefersStatusBarHidden { return YES; }
- (void)viewDidLoad {
    [super viewDidLoad];
    self.view.backgroundColor = [UIColor colorWithRed:0.10 green:0.16 blue:0.24 alpha:1];
    _readout = [[UILabel alloc] initWithFrame:CGRectZero];
    _readout.numberOfLines = 0;
    _readout.textAlignment = NSTextAlignmentCenter;
    _readout.textColor = UIColor.whiteColor;
    _readout.font = [UIFont monospacedSystemFontOfSize:16 weight:UIFontWeightRegular];
    [self.view addSubview:_readout];
    _controls = [[UnleashedTouchControlsView alloc] initWithFrame:self.view.bounds];
    [self.view addSubview:_controls];
    [_controls updateControls:0 enabled:YES];
    _link = [CADisplayLink displayLinkWithTarget:self selector:@selector(tick:)];
    [_link addToRunLoop:NSRunLoop.mainRunLoop forMode:NSRunLoopCommonModes];
}
- (void)viewDidLayoutSubviews {
    [super viewDidLayoutSubviews];
    _controls.frame = self.view.bounds;
    CGRect safe = self.view.safeAreaLayoutGuide.layoutFrame;
    _readout.frame = CGRectMake(CGRectGetMidX(safe)-230, CGRectGetMidY(safe)-50, 460, 100);
}
- (void)tick:(CADisplayLink*)link {
    [_controls updateControls:link.targetTimestamp-link.timestamp enabled:YES];
    UIEdgeInsets safe = self.view.safeAreaInsets;
    _readout.text = [NSString stringWithFormat:@"Touch controls preview\n%.0f × %.0f pt · safe %.0f/%.0f/%.0f/%.0f\nButtons %04x · L %d,%d · R %d,%d",
        self.view.bounds.size.width,self.view.bounds.size.height,safe.left,safe.top,safe.right,safe.bottom,
        hid::previewState.wButtons,hid::previewState.sThumbLX,hid::previewState.sThumbLY,
        hid::previewState.sThumbRX,hid::previewState.sThumbRY];
    if (!_exported && link.timestamp > 0 && self.view.window && self.view.bounds.size.width > self.view.bounds.size.height) {
        _exported = YES;
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 2*NSEC_PER_SEC), dispatch_get_main_queue(), ^{
            UIGraphicsImageRenderer* renderer = [[UIGraphicsImageRenderer alloc] initWithBounds:self.view.bounds];
            UIImage* snapshot = [renderer imageWithActions:^(UIGraphicsImageRendererContext* context) {
                [self.view drawViewHierarchyInRect:self.view.bounds afterScreenUpdates:YES];
            }];
            NSURL* documents = [NSFileManager.defaultManager URLsForDirectory:NSDocumentDirectory inDomains:NSUserDomainMask].firstObject;
            [UIImagePNGRepresentation(snapshot) writeToURL:[documents URLByAppendingPathComponent:@"controls-layout.png"] atomically:YES];
            NSDictionary* dimensions = @{@"width_pt":@(self.view.bounds.size.width), @"height_pt":@(self.view.bounds.size.height),
                @"safe_left":@(self.view.safeAreaInsets.left), @"safe_top":@(self.view.safeAreaInsets.top),
                @"safe_right":@(self.view.safeAreaInsets.right), @"safe_bottom":@(self.view.safeAreaInsets.bottom),
                @"source":@"Simulator UIKit view hierarchy, without device canvas"};
            [[NSJSONSerialization dataWithJSONObject:dimensions options:NSJSONWritingPrettyPrinted error:nil]
                writeToURL:[documents URLByAppendingPathComponent:@"controls-layout.json"] atomically:YES];
        });
    }

}
@end

@interface PreviewSceneDelegate : UIResponder <UIWindowSceneDelegate>
@property(strong, nonatomic) UIWindow* window;
@end
@implementation PreviewSceneDelegate
- (void)scene:(UIScene*)scene willConnectToSession:(UISceneSession*)session options:(UISceneConnectionOptions*)options {
    UIWindowScene* windowScene = (UIWindowScene*)scene;
    self.window = [[UIWindow alloc] initWithWindowScene:windowScene];
    self.window.rootViewController = [PreviewController new];
    [self.window makeKeyAndVisible];
    UIWindowSceneGeometryPreferencesIOS* geometry = [[UIWindowSceneGeometryPreferencesIOS alloc]
        initWithInterfaceOrientations:UIInterfaceOrientationMaskLandscapeRight];
    [windowScene requestGeometryUpdateWithPreferences:geometry errorHandler:^(NSError* error) {
        NSLog(@"Landscape request: %@", error);
    }];
}
@end
@interface PreviewDelegate : UIResponder <UIApplicationDelegate>
@end
@implementation PreviewDelegate
- (BOOL)application:(UIApplication*)app didFinishLaunchingWithOptions:(NSDictionary*)options { return YES; }
@end
int main(int argc,char**argv) {
    @autoreleasepool { return UIApplicationMain(argc,argv,nil,NSStringFromClass(PreviewDelegate.class)); }
}
