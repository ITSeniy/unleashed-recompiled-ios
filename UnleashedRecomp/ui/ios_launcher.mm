#include <stdafx.h>
#include "ios_launcher.h"
#include "ios_launcher_strings.h"
#include "ios_data_folder.h"
#include "ios_mod_manager.h"
#include "ios_mod_settings.h"
#include "touch_controls.h"
#include <hid/touch_gamepad.h>
#include <install/installer.h>
#include <locale/locale.h>
#include <os/logger.h>
#include <user/config.h>
#include <user/paths.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#import <UIKit/UIKit.h>

#if !__has_feature(objc_arc)
#error "ios_launcher.mm requires ARC."
#endif

// Launcher-only switches that the renderer reads from the environment once at
// startup. They persist in NSUserDefaults and never override a variable that was
// already set (for example in an Xcode scheme).
struct EnvironmentSwitch
{
    const char* variable;
    const char* labelKey;
};

static const EnvironmentSwitch g_environmentSwitches[] =
{
    { "UNLEASHED_IOS_FRAME_DIAGNOSTICS", "IOSLauncher_FrameLog" },
    { "UNLEASHED_IOS_GPU_PASS_DIAGNOSTICS", "IOSLauncher_PassLog" },
    { "UNLEASHED_IOS_RESOLVE_DIAGNOSTICS", "IOSLauncher_ResolveLog" },
};

static NSString* DefaultsKey(const char* variable)
{
    return [@"launcher.env." stringByAppendingString:@(variable)];
}

static void ApplyEnvironmentSwitches()
{
    for (const auto& item : g_environmentSwitches)
    {
        if (std::getenv(item.variable) == nullptr && [NSUserDefaults.standardUserDefaults boolForKey:DefaultsKey(item.variable)])
        {
            setenv(item.variable, "1", 1);
            LOGFN("Launcher enabled {} for this session.", item.variable);
        }
    }
}

static NSString* NS(const std::string& value)
{
    return [NSString stringWithUTF8String:value.c_str()] ?: @"";
}

static NSString* L(const char* key)
{
    return NS(launcher_strings::Get(key));
}

// Game text marks Japanese readings as [漢字:かな] for the in-game ruby renderer.
static std::string StripFurigana(std::string text)
{
    std::string result;
    for (size_t i = 0; i < text.size(); i++)
    {
        if (text[i] == '[')
        {
            const size_t colon = text.find(':', i);
            const size_t close = text.find(']', i);
            if (colon != std::string::npos && close != std::string::npos && colon < close)
            {
                result.append(text, i + 1, colon - i - 1);
                i = close;
                continue;
            }
        }
        result += text[i];
    }
    return result;
}

// Config text in the launcher language: launcher overrides first (Portuguese,
// Russian and settings the game has only in English), then the game's own text.
static NSString* ConfigName(const IConfigDef& def)
{
    if (const char* text = launcher_strings::GetConfigOverride("Name." + std::string(def.GetName())))
        return NS(text);
    return NS(StripFurigana(def.GetNameLocalised(launcher_strings::GetGameLanguage())));
}

static std::string ConfigDescription(const IConfigDef& def)
{
    if (const char* text = launcher_strings::GetConfigOverride("Desc." + std::string(def.GetName())))
        return text;
    return StripFurigana(def.GetDescription(launcher_strings::GetGameLanguage()));
}

static NSString* PathString(const std::filesystem::path& path)
{
    return [NSString stringWithUTF8String:(const char*)path.u8string().c_str()] ?: @"";
}

static NSString* MenuLabel(const std::string& text);

template<typename T>
static NSString* EnumLabel(const ConfigDef<T>& def, T value)
{
    const auto name = def.EnumTemplateReverse.find(value);
    if (name != def.EnumTemplateReverse.end())
    {
        if (const char* text = launcher_strings::GetConfigOverride("Value." + def.Name + "." + name->second))
            return NS(text);
    }
    // The game only exposes the localised name of the current value.
    auto& mutableDef = const_cast<ConfigDef<T>&>(def);
    const T current = mutableDef.Value;
    mutableDef.Value = value;
    NSString* label = MenuLabel(StripFurigana(mutableDef.GetValueLocalised(launcher_strings::GetGameLanguage())));
    mutableDef.Value = current;
    return label;
}

// Game option values are upper case ("ULTRA PERFORMANCE"); iOS menus use sentence
// case. Mixed-case and caseless (Japanese) labels are left alone.
static NSString* MenuLabel(const std::string& text)
{
    NSString* label = NS(text);
    if (![label isEqualToString:label.uppercaseString] || [label isEqualToString:label.lowercaseString])
        return label;

    NSMutableArray<NSString*>* words = [NSMutableArray array];
    for (NSString* word in [label componentsSeparatedByString:@" "])
    {
        static NSSet<NSString*>* const acronyms = [NSSet setWithArray:@[ @"MSAA", @"FXAA", @"TAA", @"FPS", @"HUD", @"DLC", @"GI", @"UI", @"HD" ]];
        [words addObject:[acronyms containsObject:word] ? word : word.lowercaseString];
    }
    NSString* result = [words componentsJoinedByString:@" "];
    return [[result substringToIndex:1].uppercaseString stringByAppendingString:[result substringFromIndex:1]];
}

static NSString* OneLine(const std::string& text)
{
    return [NS(text) stringByReplacingOccurrencesOfString:@"\n" withString:@" "];
}

extern NSString* const IOSDataFolderBookmarkKey;

// The port's releases are tagged ios-vX.Y.Z on the fork.
static NSString* const ReleasesAPI = @"https://api.github.com/repos/ITSeniy/unleashed-recompiled-ios/releases/latest";
static NSString* const ReleasesPage = @"https://github.com/ITSeniy/unleashed-recompiled-ios/releases";
static constexpr NSTimeInterval UpdateCheckInterval = 6 * 60 * 60;

static NSString* BundleVersion()
{
    return [NSBundle.mainBundle objectForInfoDictionaryKey:@"CFBundleShortVersionString"] ?: @"0";
}

static NSArray<NSNumber*>* VersionParts(NSString* version)
{
    const NSRange digit = [version rangeOfCharacterFromSet:NSCharacterSet.decimalDigitCharacterSet];
    if (digit.location == NSNotFound)
        return @[];
    NSMutableArray<NSNumber*>* parts = [NSMutableArray array];
    for (NSString* part in [[version substringFromIndex:digit.location] componentsSeparatedByString:@"."])
        [parts addObject:@(part.integerValue)];
    return parts;
}

static bool IsNewerVersion(NSString* candidate, NSString* current)
{
    NSArray<NSNumber*>* a = VersionParts(candidate);
    NSArray<NSNumber*>* b = VersionParts(current);
    for (NSUInteger i = 0; i < std::max(a.count, b.count); i++)
    {
        const NSInteger x = i < a.count ? a[i].integerValue : 0;
        const NSInteger y = i < b.count ? b[i].integerValue : 0;
        if (x != y)
            return x > y;
    }
    return false;
}

typedef NS_ENUM(NSInteger, LauncherRowKind)
{
    LauncherRowInfo,
    LauncherRowToggle,
    LauncherRowMenu,
    LauncherRowSlider,
    LauncherRowAction
};

@interface LauncherRow : NSObject
@property (nonatomic) LauncherRowKind kind;
@property (nonatomic, copy) NSString* title;
@property (nonatomic, copy) NSString* detail;
@property (nonatomic, copy) NSString* symbol;
@property (nonatomic, strong) UIColor* symbolColor;
@property (nonatomic, copy) NSString* (^value)(void);
@property (nonatomic, copy) BOOL (^isOn)(void);
@property (nonatomic, copy) void (^setOn)(BOOL on);
@property (nonatomic, copy) NSArray<UIMenuElement*>* (^menu)(void (^changed)(void));
@property (nonatomic) float minimum;
@property (nonatomic) float maximum;
@property (nonatomic, copy) float (^floatValue)(void);
@property (nonatomic, copy) void (^setFloatValue)(float value);
@property (nonatomic, copy) NSString* (^formatFloat)(float value);
@property (nonatomic, copy) void (^action)(UIView* source);
@property (nonatomic) BOOL movable;
@end

@implementation LauncherRow
@end

@interface LauncherGroup : NSObject
@property (nonatomic, copy) NSString* header;
@property (nonatomic, copy) NSString* footer;
@property (nonatomic, strong) NSMutableArray<LauncherRow*>* rows;
@end

@implementation LauncherGroup
+ (instancetype)groupWithHeader:(NSString*)header footer:(NSString*)footer
{
    LauncherGroup* group = [LauncherGroup new];
    group.header = header;
    group.footer = footer;
    group.rows = [NSMutableArray array];
    return group;
}
@end

template<bool isHidden>
static LauncherRow* ToggleRow(ConfigDef<bool, isHidden>& def, NSString* title = nil)
{
    ConfigDef<bool, isHidden>* config = &def;
    LauncherRow* row = [LauncherRow new];
    row.kind = LauncherRowToggle;
    row.title = title ?: ConfigName(def);
    if (title == nil)
        row.detail = OneLine(ConfigDescription(def));
    row.isOn = ^BOOL { return config->Value; };
    row.setOn = ^(BOOL on)
    {
        config->Value = on;
        if (config->Callback)
            config->Callback(config);
        // Hidden values are only saved once they appear in the config file.
        if constexpr (isHidden)
            config->IsLoadedFromConfig = true;
    };
    return row;
}

template<typename T>
static LauncherRow* EnumRow(ConfigDef<T>& def)
{
    ConfigDef<T>* config = &def;
    LauncherRow* row = [LauncherRow new];
    row.kind = LauncherRowMenu;
    row.title = ConfigName(def);
    row.detail = OneLine(ConfigDescription(def));
    row.value = ^NSString* { return EnumLabel(*config, config->Value); };
    row.menu = ^NSArray<UIMenuElement*>* (void (^changed)(void))
    {
        NSMutableArray<UIMenuElement*>* actions = [NSMutableArray array];
        const T current = config->Value;
        for (const auto& [value, name] : config->EnumTemplateReverse)
        {
            if (config->InaccessibleValues.contains(value))
                continue;

            NSString* label = EnumLabel(*config, value);

            const T choice = value;
            UIAction* action = [UIAction actionWithTitle:label image:nil identifier:nil handler:^(UIAction*)
            {
                config->Value = choice;
                // Linked settings follow, e.g. a MetalFX preset sets the resolution.
                if (config->Callback)
                    config->Callback(config);
                changed();
            }];
            action.state = value == current ? UIMenuElementStateOn : UIMenuElementStateOff;
            [actions addObject:action];
        }
        return actions;
    };
    return row;
}

struct InstallStatus
{
    bool ready{};
    bool hasGame{};
    bool hasUpdate{};
    int dlcCount{};
    std::vector<bool> dlc;
};

static const char* const g_dlcNames[] =
{
    "Spagonia", "Chun-nan", "Mazuri", "Holoska", "Apotos & Shamar", "Empire City & Adabat"
};

static InstallStatus InspectInstallation()
{
    InstallStatus status;
    const std::filesystem::path root = GetGamePath();
    std::filesystem::path modulePath;
    std::error_code ec;
    status.ready = Installer::checkGameInstall(root, modulePath);
    status.hasGame = std::filesystem::is_regular_file(root / "game" / "default.xex", ec);
    status.hasUpdate = std::filesystem::is_regular_file(root / "update" / "default.xexp", ec);
    for (int i = 1; i <= int(DLC::Count); i++)
    {
        const bool installed = Installer::checkDLCInstall(root, DLC(i));
        status.dlc.push_back(installed);
        status.dlcCount += installed;
    }
    return status;
}

@interface LauncherViewController : UIViewController <UITableViewDataSource, UITableViewDelegate, UIDocumentPickerDelegate>
@property (nonatomic, copy) void (^completion)(IOSLauncherAction action);
@end

@implementation LauncherViewController
{
    InstallStatus _status;
    std::vector<IOSMod> _mods;
    BOOL _reordering;
    BOOL _checkingUpdates;
    BOOL _pickingDataFolder;
    NSString* _updateError;
    NSInteger _section;
    NSArray<NSString*>* _sectionTitles;
    NSArray<NSString*>* _sectionSymbols;
    NSMutableArray<UIButton*>* _railButtons;
    NSArray<LauncherGroup*>* _groups;
    UITableView* _table;
    UILabel* _titleLabel;
    UIStackView* _chips;
    UILabel* _summary;
    UIButton* _playButton;
}

- (UIInterfaceOrientationMask)supportedInterfaceOrientations
{
    return UIInterfaceOrientationMaskLandscape;
}

- (BOOL)prefersStatusBarHidden
{
    return YES;
}

- (BOOL)prefersHomeIndicatorAutoHidden
{
    return YES;
}

- (void)viewDidLoad
{
    [super viewDidLoad];
    self.view.backgroundColor = UIColor.systemGroupedBackgroundColor;
    _sectionTitles = @[ L("IOSLauncher_Section_Game"), L("IOSLauncher_Section_Graphics"), L("IOSLauncher_Section_Controls"),
        L("IOSLauncher_Section_Mods"), L("IOSLauncher_Section_Updates"), L("IOSLauncher_Section_Advanced") ];
    _sectionSymbols = @[ @"gamecontroller", @"display", @"hand.tap", @"puzzlepiece.extension", @"arrow.down.circle", @"wrench.and.screwdriver" ];
    _section = [NSUserDefaults.standardUserDefaults integerForKey:@"launcher.section"];
    if (_section < 0 || _section >= (NSInteger)_sectionTitles.count)
        _section = 0;

    UIView* home = [self buildHomePane];
    UIView* rail = [self buildRail];
    _table = [[UITableView alloc] initWithFrame:CGRectZero style:UITableViewStyleInsetGrouped];
    _table.dataSource = self;
    _table.delegate = self;
    // The Done row ends reordering, so rows stay tappable in edit mode.
    _table.allowsSelectionDuringEditing = YES;
    _table.backgroundColor = UIColor.systemGroupedBackgroundColor;
    _table.translatesAutoresizingMaskIntoConstraints = NO;

    UIView* homeDivider = [self divider];
    UIView* railDivider = [self divider];
    for (UIView* view in @[ home, homeDivider, rail, railDivider, _table ])
        [self.view addSubview:view];

    UILayoutGuide* safe = self.view.safeAreaLayoutGuide;
    const CGFloat hairline = 1.0 / UIScreen.mainScreen.scale;
    [NSLayoutConstraint activateConstraints:@[
        [home.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor],
        [home.topAnchor constraintEqualToAnchor:self.view.topAnchor],
        [home.bottomAnchor constraintEqualToAnchor:self.view.bottomAnchor],
        [home.trailingAnchor constraintEqualToAnchor:safe.leadingAnchor constant:280],
        [homeDivider.leadingAnchor constraintEqualToAnchor:home.trailingAnchor],
        [homeDivider.topAnchor constraintEqualToAnchor:self.view.topAnchor],
        [homeDivider.bottomAnchor constraintEqualToAnchor:self.view.bottomAnchor],
        [homeDivider.widthAnchor constraintEqualToConstant:hairline],
        [rail.leadingAnchor constraintEqualToAnchor:homeDivider.trailingAnchor],
        [rail.topAnchor constraintEqualToAnchor:safe.topAnchor],
        [rail.bottomAnchor constraintEqualToAnchor:self.view.bottomAnchor],
        [rail.widthAnchor constraintEqualToConstant:150],
        [railDivider.leadingAnchor constraintEqualToAnchor:rail.trailingAnchor],
        [railDivider.topAnchor constraintEqualToAnchor:self.view.topAnchor],
        [railDivider.bottomAnchor constraintEqualToAnchor:self.view.bottomAnchor],
        [railDivider.widthAnchor constraintEqualToConstant:hairline],
        [_table.leadingAnchor constraintEqualToAnchor:railDivider.trailingAnchor],
        [_table.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor],
        [_table.topAnchor constraintEqualToAnchor:self.view.topAnchor],
        [_table.bottomAnchor constraintEqualToAnchor:self.view.bottomAnchor],
    ]];

    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(refresh)
        name:UIApplicationDidBecomeActiveNotification object:nil];
    [self refresh];

    const NSTimeInterval lastCheck = [NSUserDefaults.standardUserDefaults doubleForKey:@"launcher.update.checked"];
    if (NSDate.date.timeIntervalSince1970 - lastCheck > UpdateCheckInterval)
        [self checkForUpdates];
}

- (UIView*)divider
{
    UIView* view = [UIView new];
    view.backgroundColor = UIColor.separatorColor;
    view.translatesAutoresizingMaskIntoConstraints = NO;
    return view;
}

- (UIView*)buildHomePane
{
    UIView* pane = [UIView new];
    pane.backgroundColor = UIColor.secondarySystemGroupedBackgroundColor;
    pane.translatesAutoresizingMaskIntoConstraints = NO;

    _titleLabel = [UILabel new];
    _titleLabel.text = @"Unleashed Recompiled";
    _titleLabel.font = [UIFont systemFontOfSize:24 weight:UIFontWeightBold];
    _titleLabel.adjustsFontSizeToFitWidth = YES;
    _titleLabel.minimumScaleFactor = 0.7;

    UILabel* version = [UILabel new];
    NSString* bundleVersion = [NSBundle.mainBundle objectForInfoDictionaryKey:@"CFBundleShortVersionString"] ?: @"";
    version.text = [NSString stringWithFormat:@"%@ %@", L("IOSLauncher_Version"), bundleVersion];
    version.font = [UIFont preferredFontForTextStyle:UIFontTextStyleFootnote];
    version.textColor = UIColor.secondaryLabelColor;

    _chips = [UIStackView new];
    _chips.axis = UILayoutConstraintAxisHorizontal;
    _chips.spacing = 6;
    _chips.alignment = UIStackViewAlignmentLeading;

    _summary = [UILabel new];
    _summary.font = [UIFont preferredFontForTextStyle:UIFontTextStyleFootnote];
    _summary.textColor = UIColor.secondaryLabelColor;
    _summary.numberOfLines = 2;

    UIView* spacer = [UIView new];
    [spacer setContentHuggingPriority:UILayoutPriorityDefaultLow - 1 forAxis:UILayoutConstraintAxisVertical];

    UIButtonConfiguration* playConfig = [UIButtonConfiguration filledButtonConfiguration];
    playConfig.cornerStyle = UIButtonConfigurationCornerStyleLarge;
    playConfig.buttonSize = UIButtonConfigurationSizeLarge;
    playConfig.imagePadding = 8;
    _playButton = [UIButton buttonWithConfiguration:playConfig primaryAction:nil];
    [_playButton addTarget:self action:@selector(play) forControlEvents:UIControlEventPrimaryActionTriggered];
    [_playButton.heightAnchor constraintEqualToConstant:52].active = YES;

    UIButton* controls = [self quickButton:L("IOSLauncher_Section_Controls") symbol:@"hand.tap" action:@selector(showControls)];
    UIButton* mods = [self quickButton:L("IOSLauncher_Section_Mods") symbol:@"puzzlepiece.extension" action:@selector(showMods)];
    UIButton* files = [self quickButton:L("IOSLauncher_Files") symbol:@"folder" action:@selector(openFiles:)];
    UIStackView* quick = [[UIStackView alloc] initWithArrangedSubviews:@[ controls, mods, files ]];
    quick.axis = UILayoutConstraintAxisHorizontal;
    quick.distribution = UIStackViewDistributionFillEqually;
    quick.spacing = 8;

    UIStackView* stack = [[UIStackView alloc] initWithArrangedSubviews:@[ _titleLabel, version, _chips, _summary, spacer, _playButton, quick ]];
    stack.axis = UILayoutConstraintAxisVertical;
    stack.spacing = 6;
    [stack setCustomSpacing:14 afterView:version];
    [stack setCustomSpacing:10 afterView:_chips];
    [stack setCustomSpacing:10 afterView:_playButton];
    stack.translatesAutoresizingMaskIntoConstraints = NO;
    [pane addSubview:stack];

    UILayoutGuide* safe = pane.safeAreaLayoutGuide;
    [NSLayoutConstraint activateConstraints:@[
        [stack.leadingAnchor constraintEqualToAnchor:safe.leadingAnchor constant:20],
        [stack.trailingAnchor constraintEqualToAnchor:pane.trailingAnchor constant:-20],
        [stack.topAnchor constraintEqualToAnchor:safe.topAnchor constant:20],
        [stack.bottomAnchor constraintEqualToAnchor:safe.bottomAnchor constant:-16],
    ]];
    return pane;
}

- (UIButton*)quickButton:(NSString*)title symbol:(NSString*)symbol action:(SEL)selector
{
    UIButtonConfiguration* config = [UIButtonConfiguration grayButtonConfiguration];
    config.title = title;
    config.image = [UIImage systemImageNamed:symbol];
    config.imagePlacement = NSDirectionalRectEdgeTop;
    config.imagePadding = 4;
    config.cornerStyle = UIButtonConfigurationCornerStyleLarge;
    config.titleTextAttributesTransformer = ^NSDictionary* (NSDictionary* attributes)
    {
        NSMutableDictionary* result = [attributes mutableCopy];
        result[NSFontAttributeName] = [UIFont preferredFontForTextStyle:UIFontTextStyleCaption1];
        return result;
    };
    UIButton* button = [UIButton buttonWithConfiguration:config primaryAction:nil];
    [button addTarget:self action:selector forControlEvents:UIControlEventPrimaryActionTriggered];
    return button;
}

- (UIView*)buildRail
{
    _railButtons = [NSMutableArray array];
    UIStackView* stack = [UIStackView new];
    stack.axis = UILayoutConstraintAxisVertical;
    stack.spacing = 2;
    stack.translatesAutoresizingMaskIntoConstraints = NO;
    for (NSUInteger i = 0; i < _sectionTitles.count; i++)
    {
        UIButtonConfiguration* config = [UIButtonConfiguration plainButtonConfiguration];
        config.title = _sectionTitles[i];
        config.image = [UIImage systemImageNamed:_sectionSymbols[i]];
        config.imagePadding = 8;
        config.contentInsets = NSDirectionalEdgeInsetsMake(10, 10, 10, 10);
        config.cornerStyle = UIButtonConfigurationCornerStyleMedium;
        UIButton* button = [UIButton buttonWithConfiguration:config primaryAction:nil];
        button.contentHorizontalAlignment = UIControlContentHorizontalAlignmentLeading;
        button.tag = i;
        [button addTarget:self action:@selector(selectSectionButton:) forControlEvents:UIControlEventPrimaryActionTriggered];
        [stack addArrangedSubview:button];
        [_railButtons addObject:button];
    }

    UIView* rail = [UIView new];
    rail.translatesAutoresizingMaskIntoConstraints = NO;
    [rail addSubview:stack];
    [NSLayoutConstraint activateConstraints:@[
        [stack.leadingAnchor constraintEqualToAnchor:rail.leadingAnchor constant:8],
        [stack.trailingAnchor constraintEqualToAnchor:rail.trailingAnchor constant:-8],
        [stack.topAnchor constraintEqualToAnchor:rail.topAnchor constant:16],
    ]];
    return rail;
}

- (void)showControls
{
    [self selectSection:2];
}

- (void)showMods
{
    [self selectSection:3];
}

- (void)showUpdates
{
    [self selectSection:4];
}

- (void)selectSectionButton:(UIButton*)sender
{
    [self selectSection:sender.tag];
}

- (void)selectSection:(NSInteger)section
{
    _section = section;
    _reordering = NO;
    [_table setEditing:NO animated:NO];
    [NSUserDefaults.standardUserDefaults setInteger:_section forKey:@"launcher.section"];
    [self rebuildGroups];
    [_table setContentOffset:CGPointMake(0, -_table.adjustedContentInset.top) animated:NO];
}

- (void)updateRail
{
    for (UIButton* button in _railButtons)
    {
        const BOOL selected = button.tag == _section;
        UIButtonConfiguration* config = button.configuration;
        config.baseForegroundColor = selected ? self.view.tintColor : UIColor.labelColor;
        config.background.backgroundColor = selected ? [self.view.tintColor colorWithAlphaComponent:0.18] : UIColor.clearColor;
        button.configuration = config;
    }
}

- (UIButton*)chip:(NSString*)title color:(UIColor*)color
{
    UIButtonConfiguration* config = [UIButtonConfiguration tintedButtonConfiguration];
    config.title = title;
    config.baseForegroundColor = color;
    config.baseBackgroundColor = color;
    config.cornerStyle = UIButtonConfigurationCornerStyleCapsule;
    config.buttonSize = UIButtonConfigurationSizeMini;
    UIButton* button = [UIButton buttonWithConfiguration:config primaryAction:nil];
    button.userInteractionEnabled = NO;
    return button;
}

- (void)refresh
{
    _status = InspectInstallation();
    _mods = ios_mods::Load();
    [self updateHome];
    [self rebuildGroups];
}

- (void)updateHome
{
    for (UIView* view in _chips.arrangedSubviews)
        [view removeFromSuperview];
    [_chips addArrangedSubview:_status.ready
        ? [self chip:L("IOSLauncher_Status_Ready") color:UIColor.systemGreenColor]
        : [self chip:L("IOSLauncher_Status_Missing") color:UIColor.systemOrangeColor]];
    if (_status.ready)
    {
        NSString* dlc = [NSString stringWithFormat:@"DLC %d/%d", _status.dlcCount, int(DLC::Count)];
        [_chips addArrangedSubview:[self chip:dlc color:UIColor.systemBlueColor]];
    }
    if ([self isUpdateAvailable])
    {
        UIButton* update = [self chip:L("IOSLauncher_Chip_Update") color:UIColor.systemOrangeColor];
        update.userInteractionEnabled = YES;
        [update addTarget:self action:@selector(showUpdates) forControlEvents:UIControlEventPrimaryActionTriggered];
        [_chips addArrangedSubview:update];
    }
    const auto activeMods = std::count_if(_mods.begin(), _mods.end(), [](const IOSMod& mod) { return mod.enabled; });
    if (activeMods > 0)
    {
        NSString* mods = [L("IOSLauncher_Chip_Mods") stringByReplacingOccurrencesOfString:@"{}" withString:@(activeMods).stringValue];
        [_chips addArrangedSubview:[self chip:mods color:UIColor.systemPurpleColor]];
    }

    NSMutableArray<NSString*>* parts = [NSMutableArray array];
    if (Config::MetalFXQuality != EMetalFXQuality::Off && Config::ResolutionScale < 1.0f)
        [parts addObject:[@"MetalFX " stringByAppendingString:EnumLabel(Config::MetalFXQuality, Config::MetalFXQuality.Value).lowercaseString]];
    [parts addObject:[NSString stringWithFormat:@"%d%%", int(std::lround(Config::ResolutionScale * 100.0f))]];
    [parts addObject:[self frameRateLabel:int32_t(Config::FPS)]];
    _summary.text = [parts componentsJoinedByString:@" · "];

    UIButtonConfiguration* config = _playButton.configuration;
    config.title = _status.ready ? L("IOSLauncher_Play") : L("IOSLauncher_InstallGame");
    config.image = [UIImage systemImageNamed:_status.ready ? @"play.fill" : @"square.and.arrow.down"];
    _playButton.configuration = config;
}

- (NSString*)frameRateLabel:(int32_t)fps
{
    if (fps >= FPS_MAX)
        return L("IOSLauncher_Unlimited");
    return [NSString stringWithFormat:@"%d FPS", fps];
}

- (void)settingsChanged
{
    Config::Save();
    [self updateHome];
    // The MetalFX preset and the resolution scale change each other.
    if (_section == 1)
        [_table reloadSections:[NSIndexSet indexSetWithIndex:0] withRowAnimation:UITableViewRowAnimationNone];
}

- (void)rebuildGroups
{
    switch (_section)
    {
    case 1: _groups = [self graphicsGroups]; break;
    case 2: _groups = [self controlsGroups]; break;
    case 3: _groups = [self modsGroups]; break;
    case 4: _groups = [self updatesGroups]; break;
    case 5: _groups = [self advancedGroups]; break;
    default: _groups = [self gameGroups]; break;
    }
    [self updateRail];

    UILabel* header = [UILabel new];
    header.text = _sectionTitles[_section];
    header.font = [UIFont systemFontOfSize:22 weight:UIFontWeightBold];
    UIView* headerView = [[UIView alloc] initWithFrame:CGRectMake(0, 0, 0, 48)];
    header.translatesAutoresizingMaskIntoConstraints = NO;
    [headerView addSubview:header];
    [NSLayoutConstraint activateConstraints:@[
        [header.leadingAnchor constraintEqualToAnchor:headerView.layoutMarginsGuide.leadingAnchor],
        [header.bottomAnchor constraintEqualToAnchor:headerView.bottomAnchor],
    ]];
    _table.tableHeaderView = headerView;
    [_table reloadData];
}

// MARK: Sections

- (NSArray<LauncherGroup*>*)gameGroups
{
    __weak LauncherViewController* weakSelf = self;
    NSString* yes = L("IOSLauncher_Installed");
    NSString* no = L("IOSLauncher_NotInstalled");
    UIColor* good = UIColor.systemGreenColor;
    UIColor* missing = UIColor.systemOrangeColor;

    LauncherGroup* files = [LauncherGroup groupWithHeader:L("IOSLauncher_Group_Installation") footer:L("IOSLauncher_Game_Footer")];
    const bool hasGame = _status.ready || _status.hasGame;
    const bool hasUpdate = _status.ready || _status.hasUpdate;
    [files.rows addObject:[self infoRow:L("IOSLauncher_Game_Base") value:hasGame ? yes : no symbol:hasGame ? @"checkmark.circle.fill" : @"xmark.circle" color:hasGame ? good : missing]];
    [files.rows addObject:[self infoRow:L("IOSLauncher_Game_Update") value:hasUpdate ? yes : no symbol:hasUpdate ? @"checkmark.circle.fill" : @"xmark.circle" color:hasUpdate ? good : missing]];
    NSString* place = [UIDevice.currentDevice.userInterfaceIdiom == UIUserInterfaceIdiomPad ? L("IOSLauncher_OnMyiPad") : L("IOSLauncher_OnMyiPhone")
        stringByAppendingString:@" › Unleashed Recompiled › UnleashedRecomp"];
    LauncherRow* folder = [self infoRow:L("IOSLauncher_Game_Folder") value:place symbol:@"folder" color:UIColor.systemBlueColor];
    folder.kind = LauncherRowAction;
    folder.action = ^(UIView* source) { [weakSelf openFiles:source]; };
    [files.rows addObject:folder];

    LauncherGroup* dlc = [LauncherGroup groupWithHeader:L("IOSLauncher_Group_DLC") footer:nil];
    for (size_t i = 0; i < _status.dlc.size(); i++)
    {
        const bool installed = _status.dlc[i];
        [dlc.rows addObject:[self infoRow:@(g_dlcNames[i]) value:installed ? yes : no symbol:installed ? @"checkmark.circle.fill" : @"circle" color:installed ? good : UIColor.tertiaryLabelColor]];
    }

    LauncherGroup* actions = [LauncherGroup groupWithHeader:nil footer:L("IOSLauncher_Install_Footer")];
    [actions.rows addObject:[self actionRow:L("IOSLauncher_InstallGame") symbol:@"square.and.arrow.down" action:^(UIView*) { [weakSelf finish:IOSLauncherAction::Install]; }]];
    if (_status.ready)
        [actions.rows addObject:[self actionRow:L("IOSLauncher_InstallDLC") symbol:@"plus.circle" action:^(UIView*) { [weakSelf finish:IOSLauncherAction::InstallDLC]; }]];
    [actions.rows addObject:[self actionRow:L("IOSLauncher_Recheck") symbol:@"arrow.clockwise" action:^(UIView*) { [weakSelf refresh]; }]];

    LauncherGroup* data = [self dataFolderGroup];
    return _status.ready ? @[ files, dlc, actions, data ] : @[ files, actions, data ];
}

- (LauncherGroup*)dataFolderGroup
{
    __weak LauncherViewController* weakSelf = self;
    const BOOL custom = [NSUserDefaults.standardUserDefaults dataForKey:IOSDataFolderBookmarkKey] != nil;
    LauncherGroup* group = [LauncherGroup groupWithHeader:L("IOSLauncher_Group_DataFolder") footer:L("IOSLauncher_DataFolder_Footer")];
    if (ios_data_folder::IsUnavailable())
        [group.rows addObject:[self infoRow:L("IOSLauncher_DataFolder_Unavailable") value:@"" symbol:@"exclamationmark.triangle" color:UIColor.systemOrangeColor]];

    NSString* location = custom && !ios_data_folder::IsUnavailable() ? PathString(GetUserPath()).lastPathComponent : L("IOSLauncher_DataFolder_InApp");
    [group.rows addObject:[self infoRow:L("IOSLauncher_DataFolder_Location") value:location symbol:@"externaldrive" color:UIColor.systemBlueColor]];
    [group.rows addObject:[self actionRow:L("IOSLauncher_DataFolder_Choose") symbol:@"folder.badge.gearshape" action:^(UIView*) { [weakSelf pickDataFolder]; }]];
    if (custom)
        [group.rows addObject:[self actionRow:L("IOSLauncher_DataFolder_UseApp") symbol:@"arrow.uturn.backward" action:^(UIView*) { [weakSelf resetDataFolder]; }]];
    return group;
}

- (void)pickDataFolder
{
    UIDocumentPickerViewController* picker = [[UIDocumentPickerViewController alloc]
        initWithDocumentTypes:@[ @"public.folder" ] inMode:UIDocumentPickerModeOpen];
    picker.delegate = self;
    _pickingDataFolder = YES;
    [self presentViewController:picker animated:YES completion:nil];
}

- (void)documentPickerWasCancelled:(UIDocumentPickerViewController*)controller
{
    _pickingDataFolder = NO;
}

static NSArray<NSString*>* FolderEntries(NSString* path)
{
    NSMutableArray<NSString*>* entries = [NSMutableArray array];
    for (NSString* name in [NSFileManager.defaultManager contentsOfDirectoryAtPath:path error:nil] ?: @[])
    {
        if (![name isEqualToString:@".DS_Store"] && ![name hasPrefix:@".Trash"])
            [entries addObject:name];
    }
    return entries;
}

- (void)handleDataFolder:(NSURL*)url
{
    if (![url startAccessingSecurityScopedResource])
    {
        [self showMessage:L("IOSLauncher_DataFolder_NotWritable")];
        return;
    }

    // The folder must outlive the app, so it cannot be inside the app's container.
    NSString* path = url.URLByResolvingSymlinksInPath.path;
    NSString* home = NSHomeDirectory().stringByResolvingSymlinksInPath;
    NSNumber* ubiquitous = nil;
    [url getResourceValue:&ubiquitous forKey:NSURLIsUbiquitousItemKey error:nil];
    NSString* probe = [path stringByAppendingPathComponent:@".unleashed_write_probe"];
    NSString* problem = nil;
    if ([path isEqualToString:home] || [path hasPrefix:[home stringByAppendingString:@"/"]])
        problem = L("IOSLauncher_DataFolder_InsideApp");
    else if (ubiquitous.boolValue)
        problem = L("IOSLauncher_DataFolder_iCloud");
    else if (![NSFileManager.defaultManager createFileAtPath:probe contents:[NSData data] attributes:nil])
        problem = L("IOSLauncher_DataFolder_NotWritable");
    [NSFileManager.defaultManager removeItemAtPath:probe error:nil];
    NSData* bookmark = problem ? nil : [url bookmarkDataWithOptions:0 includingResourceValuesForKeys:nil relativeToURL:nil error:nil];
    if (problem == nil && bookmark == nil)
        problem = L("IOSLauncher_DataFolder_NotWritable");
    if (problem)
    {
        [url stopAccessingSecurityScopedResource];
        [self showMessage:problem];
        return;
    }

    NSString* current = PathString(GetUserPath()).stringByResolvingSymlinksInPath;
    if ([current isEqualToString:path])
    {
        [url stopAccessingSecurityScopedResource];
        return;
    }

    NSArray<NSString*>* destination = FolderEntries(path);
    NSMutableArray<NSString*>* source = [FolderEntries(current) mutableCopy];
    [source removeObject:@"unleashedrecomp.log"];
    const BOOL destinationHasData = [destination containsObject:@"game"] || [destination containsObject:@"config.toml"];

    __weak LauncherViewController* weakSelf = self;
    UIAlertController* alert;
    if (destinationHasData)
    {
        // A folder used before, e.g. after reinstalling the app.
        alert = [UIAlertController alertControllerWithTitle:L("IOSLauncher_DataFolder_UseExisting_Title")
            message:L("IOSLauncher_DataFolder_UseExisting") preferredStyle:UIAlertControllerStyleAlert];
        [alert addAction:[UIAlertAction actionWithTitle:L("IOSLauncher_DataFolder_Use") style:UIAlertActionStyleDefault handler:^(UIAlertAction*)
        {
            [weakSelf saveDataFolderBookmark:bookmark];
        }]];
    }
    else if (source.count > 0)
    {
        if (destination.count > 0)
        {
            [url stopAccessingSecurityScopedResource];
            [self showMessage:L("IOSLauncher_DataFolder_NotEmpty")];
            return;
        }
        alert = [UIAlertController alertControllerWithTitle:L("IOSLauncher_DataFolder_Move_Title")
            message:L("IOSLauncher_DataFolder_Move") preferredStyle:UIAlertControllerStyleAlert];
        [alert addAction:[UIAlertAction actionWithTitle:L("IOSLauncher_DataFolder_MoveButton") style:UIAlertActionStyleDefault handler:^(UIAlertAction*)
        {
            [weakSelf moveData:source from:current to:path bookmark:bookmark];
        }]];
    }
    else
    {
        alert = [UIAlertController alertControllerWithTitle:L("IOSLauncher_DataFolder_UseEmpty_Title")
            message:L("IOSLauncher_DataFolder_UseEmpty") preferredStyle:UIAlertControllerStyleAlert];
        [alert addAction:[UIAlertAction actionWithTitle:L("IOSLauncher_DataFolder_Use") style:UIAlertActionStyleDefault handler:^(UIAlertAction*)
        {
            [weakSelf saveDataFolderBookmark:bookmark];
        }]];
    }
    [alert addAction:[UIAlertAction actionWithTitle:L("IOSLauncher_Cancel") style:UIAlertActionStyleCancel handler:^(UIAlertAction*)
    {
        [url stopAccessingSecurityScopedResource];
    }]];
    [self presentViewController:alert animated:YES completion:nil];
}

- (void)moveData:(NSArray<NSString*>*)names from:(NSString*)source to:(NSString*)destination bookmark:(NSData*)bookmark
{
    UIAlertController* progress = [UIAlertController alertControllerWithTitle:L("IOSLauncher_DataFolder_Moving") message:nil preferredStyle:UIAlertControllerStyleAlert];
    [self presentViewController:progress animated:YES completion:nil];

    __weak LauncherViewController* weakSelf = self;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^
    {
        // Same volume, so each move is a rename; undo the finished ones on failure.
        NSFileManager* files = NSFileManager.defaultManager;
        NSMutableArray<NSString*>* moved = [NSMutableArray array];
        NSError* error = nil;
        for (NSString* name in names)
        {
            if (![files moveItemAtPath:[source stringByAppendingPathComponent:name] toPath:[destination stringByAppendingPathComponent:name] error:&error])
                break;
            [moved addObject:name];
        }
        if (error != nil)
        {
            for (NSString* name in moved)
                [files moveItemAtPath:[destination stringByAppendingPathComponent:name] toPath:[source stringByAppendingPathComponent:name] error:nil];
        }
        LOGFN("Moved {} of {} data items to the chosen folder.", moved.count, names.count);
        dispatch_async(dispatch_get_main_queue(), ^
        {
            [progress dismissViewControllerAnimated:YES completion:^
            {
                if (error != nil)
                    [weakSelf showMessage:[L("IOSLauncher_DataFolder_MoveFailed") stringByReplacingOccurrencesOfString:@"{}" withString:error.localizedDescription]];
                else
                    [weakSelf saveDataFolderBookmark:bookmark];
            }];
        });
    });
}

- (void)saveDataFolderBookmark:(NSData*)bookmark
{
    NSUserDefaults* defaults = NSUserDefaults.standardUserDefaults;
    if (bookmark)
        [defaults setObject:bookmark forKey:IOSDataFolderBookmarkKey];
    else
        [defaults removeObjectForKey:IOSDataFolderBookmarkKey];
    [defaults synchronize];
    LOGN(bookmark ? "Data folder changed; restarting." : "Data folder reset to the app folder; restarting.");

    // The data path is fixed at startup, before the config and log are opened.
    UIAlertController* alert = [UIAlertController alertControllerWithTitle:L("IOSLauncher_DataFolder_Restart_Title")
        message:L("IOSLauncher_DataFolder_Restart") preferredStyle:UIAlertControllerStyleAlert];
    [alert addAction:[UIAlertAction actionWithTitle:L("IOSLauncher_OK") style:UIAlertActionStyleDefault handler:^(UIAlertAction*)
    {
        std::_Exit(0);
    }]];
    [self presentViewController:alert animated:YES completion:nil];
}

- (void)resetDataFolder
{
    __weak LauncherViewController* weakSelf = self;
    UIAlertController* alert = [UIAlertController alertControllerWithTitle:L("IOSLauncher_DataFolder_UseApp")
        message:L("IOSLauncher_DataFolder_UseApp_Message") preferredStyle:UIAlertControllerStyleAlert];
    [alert addAction:[UIAlertAction actionWithTitle:L("IOSLauncher_DataFolder_UseApp") style:UIAlertActionStyleDestructive handler:^(UIAlertAction*)
    {
        [weakSelf saveDataFolderBookmark:nil];
    }]];
    [alert addAction:[UIAlertAction actionWithTitle:L("IOSLauncher_Cancel") style:UIAlertActionStyleCancel handler:nil]];
    [self presentViewController:alert animated:YES completion:nil];
}

- (NSArray<LauncherGroup*>*)graphicsGroups
{
    LauncherGroup* resolution = [LauncherGroup groupWithHeader:L("IOSLauncher_Group_Resolution") footer:L("IOSLauncher_Resolution_Footer")];
    LauncherRow* scale = [LauncherRow new];
    scale.kind = LauncherRowSlider;
    scale.title = ConfigName(Config::ResolutionScale);
    scale.minimum = 0.25f;
    scale.maximum = std::max(1.0f, float(Config::ResolutionScale));
    scale.floatValue = ^float { return Config::ResolutionScale; };
    scale.setFloatValue = ^(float value)
    {
        Config::ResolutionScale = std::round(value * 100.0f) / 100.0f;
        Config::ResolutionScale.Callback(&Config::ResolutionScale);
    };
    scale.formatFloat = ^NSString* (float value) { return [NSString stringWithFormat:@"%d%%", int(std::lround(value * 100.0f))]; };
    [resolution.rows addObject:EnumRow(Config::MetalFXQuality)];
    [resolution.rows addObject:scale];

    LauncherGroup* frameRate = [LauncherGroup groupWithHeader:L("IOSLauncher_Group_FrameRate") footer:nil];
    LauncherRow* fps = [LauncherRow new];
    fps.kind = LauncherRowMenu;
    fps.title = ConfigName(Config::FPS);
    fps.detail = OneLine(ConfigDescription(Config::FPS));
    __weak LauncherViewController* weakSelf = self;
    fps.value = ^NSString* { return [weakSelf frameRateLabel:int32_t(Config::FPS)]; };
    fps.menu = ^NSArray<UIMenuElement*>* (void (^changed)(void))
    {
        std::vector<int32_t> rates = { 30, 60 };
        if (UIScreen.mainScreen.maximumFramesPerSecond >= 120)
            rates.push_back(120);
        rates.push_back(FPS_MAX);
        if (std::find(rates.begin(), rates.end(), int32_t(Config::FPS)) == rates.end())
            rates.insert(rates.begin(), Config::FPS);

        NSMutableArray<UIMenuElement*>* actions = [NSMutableArray array];
        for (int32_t rate : rates)
        {
            UIAction* action = [UIAction actionWithTitle:[weakSelf frameRateLabel:rate] image:nil identifier:nil handler:^(UIAction*)
            {
                Config::FPS = rate;
                changed();
            }];
            action.state = rate == int32_t(Config::FPS) ? UIMenuElementStateOn : UIMenuElementStateOff;
            [actions addObject:action];
        }
        return actions;
    };
    [frameRate.rows addObject:fps];

    LauncherGroup* quality = [LauncherGroup groupWithHeader:L("IOSLauncher_Group_Quality") footer:nil];
    [quality.rows addObject:EnumRow(Config::AntiAliasing)];
    [quality.rows addObject:ToggleRow(Config::TransparencyAntiAliasing)];
    [quality.rows addObject:EnumRow(Config::ShadowResolution)];
    [quality.rows addObject:EnumRow(Config::TextureQuality)];
    [quality.rows addObject:EnumRow(Config::GITextureFiltering)];
    [quality.rows addObject:ToggleRow(Config::PlanarReflections)];
    [quality.rows addObject:EnumRow(Config::MotionBlur)];

    LauncherGroup* optimizations = [LauncherGroup groupWithHeader:L("IOSLauncher_Group_Optimizations") footer:nil];
    [optimizations.rows addObject:ToggleRow(Config::SkipUnusedCopies)];
    [optimizations.rows addObject:ToggleRow(Config::ReuseRenderTargets)];

    LauncherGroup* overlays = [LauncherGroup groupWithHeader:L("IOSLauncher_Group_Overlays") footer:L("IOSLauncher_Overlays_Footer")];
    [overlays.rows addObject:ToggleRow(Config::ShowFPS)];
    [overlays.rows addObject:ToggleRow(Config::ShowProfiler)];
    [overlays.rows addObject:ToggleRow(Config::MetalHUD, @"Metal HUD")];

    return @[ resolution, frameRate, quality, optimizations, overlays ];
}

- (NSArray<LauncherGroup*>*)controlsGroups
{
    __weak LauncherViewController* weakSelf = self;
    LauncherGroup* touch = [LauncherGroup groupWithHeader:L("IOSLauncher_Group_Touch") footer:nil];
    [touch.rows addObject:EnumRow(Config::TouchControls)];
    [touch.rows addObject:EnumRow(Config::TouchCamera)];
    [touch.rows addObject:EnumRow(Config::TouchStickMode)];

    LauncherRow* sensitivity = [LauncherRow new];
    sensitivity.kind = LauncherRowSlider;
    sensitivity.title = ConfigName(Config::TouchCameraSensitivity);
    sensitivity.minimum = hid::TouchGamepad::MinCameraSensitivity;
    sensitivity.maximum = hid::TouchGamepad::MaxCameraSensitivity;
    sensitivity.floatValue = ^float { return Config::TouchCameraSensitivity; };
    sensitivity.setFloatValue = ^(float value) { Config::TouchCameraSensitivity = hid::TouchGamepad::ClampCameraSensitivity(value); };
    sensitivity.formatFloat = ^NSString* (float value) { return [NSString stringWithFormat:@"%.2f×", value]; };
    [touch.rows addObject:sensitivity];

    LauncherGroup* layout = [LauncherGroup groupWithHeader:nil footer:L("IOSLauncher_ArrangeControls_Footer")];
    [layout.rows addObject:[self actionRow:L("IOSLauncher_ArrangeControls") symbol:@"rectangle.and.hand.point.up.left" action:^(UIView*)
    {
        LauncherViewController* strongSelf = weakSelf;
        if (strongSelf == nil)
            return;
        if (!strongSelf->_status.ready)
        {
            [strongSelf showMessage:L("IOSLauncher_ArrangeControls_NeedsGame")];
            return;
        }
        TouchControls::RequestLayoutEditor();
        [strongSelf finish:IOSLauncherAction::Play];
    }]];

    return @[ touch, layout ];
}

- (NSArray<LauncherGroup*>*)modsGroups
{
    __weak LauncherViewController* weakSelf = self;
    LauncherGroup* list = [LauncherGroup groupWithHeader:L("IOSLauncher_Group_Mods") footer:L("IOSLauncher_Mods_Footer")];
    if (_mods.empty())
        [list.rows addObject:[self infoRow:L("IOSLauncher_Mods_Empty") value:@"" symbol:@"puzzlepiece.extension" color:UIColor.tertiaryLabelColor]];

    for (size_t i = 0; i < _mods.size(); i++)
    {
        const IOSMod& mod = _mods[i];
        LauncherRow* row = [LauncherRow new];
        row.kind = LauncherRowToggle;
        row.title = NS(mod.title);
        NSMutableArray<NSString*>* details = [NSMutableArray array];
        if (!mod.author.empty())
            [details addObject:NS(mod.author)];
        if (!mod.version.empty())
            [details addObject:NS(mod.version)];
        if (!mod.schemaPath.empty())
            [details addObject:L("IOSLauncher_Mods_HasSettings")];
        row.detail = [details componentsJoinedByString:@" · "];
        row.movable = YES;
        row.isOn = ^BOOL { LauncherViewController* strongSelf = weakSelf; return strongSelf && i < strongSelf->_mods.size() && strongSelf->_mods[i].enabled; };
        row.setOn = ^(BOOL on)
        {
            LauncherViewController* strongSelf = weakSelf;
            if (strongSelf && i < strongSelf->_mods.size())
            {
                strongSelf->_mods[i].enabled = on;
                [strongSelf saveMods];
            }
        };
        if (!mod.schemaPath.empty())
            row.action = ^(UIView*) { [weakSelf openModSettings:i]; };
        [list.rows addObject:row];
    }

    LauncherGroup* actions = [LauncherGroup groupWithHeader:nil footer:nil];
    [actions.rows addObject:[self actionRow:L("IOSLauncher_Mods_Install") symbol:@"square.and.arrow.down" action:^(UIView*) { [weakSelf pickMod]; }]];
    if (_mods.size() > 1)
    {
        NSString* title = _reordering ? L("IOSLauncher_Mods_DoneReordering") : L("IOSLauncher_Mods_Reorder");
        [actions.rows addObject:[self actionRow:title symbol:@"arrow.up.arrow.down" action:^(UIView*) { [weakSelf toggleReordering]; }]];
    }
    [actions.rows addObject:[self actionRow:L("IOSLauncher_Mods_OpenFolder") symbol:@"folder" action:^(UIView*) { [weakSelf openModsFolder]; }]];
    [actions.rows addObject:[self actionRow:L("IOSLauncher_Recheck") symbol:@"arrow.clockwise" action:^(UIView*) { [weakSelf refresh]; }]];

    // Built-in game patches, toggled like the desktop mod manager's codes.
    static const struct { ConfigDef<bool, true>* def; const char* key; } codes[] =
    {
        { &Config::AllowCancellingUnleash, "IOSLauncher_Code_AllowCancellingUnleash" },
        { &Config::DisableAutoSaveWarning, "IOSLauncher_Code_DisableAutoSaveWarning" },
        { &Config::DisableBoostFilter, "IOSLauncher_Code_DisableBoostFilter" },
        { &Config::DisableDLCIcon, "IOSLauncher_Code_DisableDLCIcon" },
        { &Config::DisableDPadMovement, "IOSLauncher_Code_DisableDPadMovement" },
        { &Config::FixEggmanlandUsingEventGalleryTransition, "IOSLauncher_Code_FixEggmanland" },
        { &Config::FixUnleashOutOfControlDrain, "IOSLauncher_Code_FixUnleashDrain" },
        { &Config::HomingAttackOnJump, "IOSLauncher_Code_HomingAttackOnJump" },
        { &Config::SaveScoreAtCheckpoints, "IOSLauncher_Code_SaveScoreAtCheckpoints" },
        { &Config::UseAlternateTitle, "IOSLauncher_Code_UseAlternateTitle" },
        { &Config::UseArrowsForTimeOfDayTransition, "IOSLauncher_Code_TimeOfDayArrows" },
    };
    LauncherGroup* codeGroup = [LauncherGroup groupWithHeader:L("IOSLauncher_Group_Codes") footer:L("IOSLauncher_Codes_Footer")];
    for (const auto& code : codes)
        [codeGroup.rows addObject:ToggleRow(*code.def, L(code.key))];

    return @[ list, actions, codeGroup ];
}

- (void)saveMods
{
    std::string error;
    if (!ios_mods::Save(_mods, error))
        [self showMessage:NS(error)];
    [self updateHome];
}

- (void)showMessage:(NSString*)message
{
    UIAlertController* alert = [UIAlertController alertControllerWithTitle:nil message:message preferredStyle:UIAlertControllerStyleAlert];
    [alert addAction:[UIAlertAction actionWithTitle:L("IOSLauncher_OK") style:UIAlertActionStyleCancel handler:nil]];
    [self presentViewController:alert animated:YES completion:nil];
}

- (void)toggleReordering
{
    _reordering = !_reordering;
    [_table setEditing:_reordering animated:YES];
    [self rebuildGroups];
}

- (void)openModSettings:(size_t)index
{
    if (index >= _mods.size())
        return;
    NSString* error = nil;
    UIViewController* settings = CreateIOSModSettingsController(_mods[index], &error);
    if (settings == nil)
    {
        [self showMessage:error];
        return;
    }
    settings.overrideUserInterfaceStyle = UIUserInterfaceStyleDark;
    [self presentViewController:settings animated:YES completion:nil];
}

- (void)openModsFolder
{
    std::error_code ec;
    std::filesystem::create_directories(ios_mods::GetModsDirectory(), ec);
    [self openFolder:ios_mods::GetModsDirectory()];
}

- (void)pickMod
{
    // Same picker API as the installer; a folder or a ZIP archive.
    UIDocumentPickerViewController* picker = [[UIDocumentPickerViewController alloc]
        initWithDocumentTypes:@[ @"public.folder", @"public.zip-archive" ] inMode:UIDocumentPickerModeOpen];
    picker.delegate = self;
    picker.allowsMultipleSelection = NO;
    [self presentViewController:picker animated:YES completion:nil];
}

- (void)documentPicker:(UIDocumentPickerViewController*)controller didPickDocumentsAtURLs:(NSArray<NSURL*>*)urls
{
    NSURL* url = urls.firstObject;
    if (url == nil)
        return;
    if (_pickingDataFolder)
    {
        _pickingDataFolder = NO;
        [self handleDataFolder:url];
        return;
    }

    const BOOL scoped = [url startAccessingSecurityScopedResource];
    UIAlertController* progress = [UIAlertController alertControllerWithTitle:L("IOSLauncher_Mods_Installing") message:nil preferredStyle:UIAlertControllerStyleAlert];
    [self presentViewController:progress animated:YES completion:nil];

    __weak LauncherViewController* weakSelf = self;
    const std::filesystem::path source(std::u8string_view((const char8_t*)url.fileSystemRepresentation));
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^
    {
        std::string error;
        const int installed = ios_mods::Install(source, error);
        dispatch_async(dispatch_get_main_queue(), ^
        {
            if (scoped)
                [url stopAccessingSecurityScopedResource];
            [progress dismissViewControllerAnimated:YES completion:^
            {
                LauncherViewController* strongSelf = weakSelf;
                if (strongSelf == nil)
                    return;
                [strongSelf refresh];
                if (installed > 0)
                    [strongSelf showMessage:[L("IOSLauncher_Mods_Installed") stringByReplacingOccurrencesOfString:@"{}" withString:@(installed).stringValue]];
                else
                    [strongSelf showMessage:NS(error)];
            }];
        });
    });
}

- (NSString*)latestVersion
{
    return [NSUserDefaults.standardUserDefaults stringForKey:@"launcher.update.tag"];
}

- (BOOL)isUpdateAvailable
{
    NSString* latest = [self latestVersion];
    return latest != nil && IsNewerVersion(latest, BundleVersion());
}

- (void)checkForUpdates
{
    if (_checkingUpdates)
        return;
    _checkingUpdates = YES;
    _updateError = nil;
    if (_section == 4)
        [self rebuildGroups];

    NSMutableURLRequest* request = [NSMutableURLRequest requestWithURL:[NSURL URLWithString:ReleasesAPI]];
    [request setValue:@"application/vnd.github+json" forHTTPHeaderField:@"Accept"];
    request.timeoutInterval = 15;
    __weak LauncherViewController* weakSelf = self;
    [[NSURLSession.sharedSession dataTaskWithRequest:request completionHandler:^(NSData* data, NSURLResponse* response, NSError* error)
    {
        const NSInteger status = [response isKindOfClass:NSHTTPURLResponse.class] ? ((NSHTTPURLResponse*)response).statusCode : 0;
        dispatch_async(dispatch_get_main_queue(), ^{ [weakSelf finishUpdateCheck:data status:status error:error]; });
    }] resume];
}

- (void)finishUpdateCheck:(NSData*)data status:(NSInteger)status error:(NSError*)error
{
    _checkingUpdates = NO;
    NSUserDefaults* defaults = NSUserDefaults.standardUserDefaults;
    NSDictionary* release = data ? [NSJSONSerialization JSONObjectWithData:data options:0 error:nil] : nil;
    if (error != nil)
    {
        _updateError = error.localizedDescription;
    }
    else if (status == 404)
    {
        // No release has been published yet.
        for (NSString* key in @[ @"launcher.update.tag", @"launcher.update.url", @"launcher.update.notes" ])
            [defaults removeObjectForKey:key];
        [defaults setDouble:NSDate.date.timeIntervalSince1970 forKey:@"launcher.update.checked"];
    }
    else if (status == 200 && [release isKindOfClass:NSDictionary.class] && [release[@"tag_name"] isKindOfClass:NSString.class])
    {
        [defaults setObject:release[@"tag_name"] forKey:@"launcher.update.tag"];
        if ([release[@"html_url"] isKindOfClass:NSString.class])
            [defaults setObject:release[@"html_url"] forKey:@"launcher.update.url"];
        if ([release[@"body"] isKindOfClass:NSString.class])
            [defaults setObject:release[@"body"] forKey:@"launcher.update.notes"];
        else
            [defaults removeObjectForKey:@"launcher.update.notes"];
        [defaults setDouble:NSDate.date.timeIntervalSince1970 forKey:@"launcher.update.checked"];
    }
    else
    {
        _updateError = [NSString stringWithFormat:@"HTTP %ld", (long)status];
    }
    LOGFN("Update check finished: status {}, latest {}.", (long)status, std::string(([self latestVersion] ?: @"none").UTF8String));
    [self updateHome];
    if (_section == 4)
        [self rebuildGroups];
}

- (NSArray<LauncherGroup*>*)updatesGroups
{
    __weak LauncherViewController* weakSelf = self;
    NSUserDefaults* defaults = NSUserDefaults.standardUserDefaults;
    NSString* latest = [self latestVersion];
    const NSTimeInterval checked = [defaults doubleForKey:@"launcher.update.checked"];

    NSString* footer;
    if (_checkingUpdates)
        footer = L("IOSLauncher_Update_Checking");
    else if (_updateError != nil)
        footer = [L("IOSLauncher_Update_Failed") stringByReplacingOccurrencesOfString:@"{}" withString:_updateError];
    else if ([self isUpdateAvailable])
        footer = L("IOSLauncher_Update_Available");
    else if (checked > 0)
        footer = latest ? L("IOSLauncher_Update_Current") : L("IOSLauncher_Update_NoReleases");
    else
        footer = nil;

    LauncherGroup* version = [LauncherGroup groupWithHeader:L("IOSLauncher_Group_Version") footer:footer];
    [version.rows addObject:[self infoRow:L("IOSLauncher_Update_Installed") value:BundleVersion() symbol:@"iphone" color:UIColor.systemBlueColor]];
    NSString* latestText = latest ? [latest stringByReplacingOccurrencesOfString:@"ios-v" withString:@""] : @"—";
    [version.rows addObject:[self infoRow:L("IOSLauncher_Update_Latest") value:latestText symbol:@"shippingbox"
        color:[self isUpdateAvailable] ? UIColor.systemOrangeColor : UIColor.systemBlueColor]];
    NSString* checkedText = L("IOSLauncher_Update_Never");
    if (checked > 0)
    {
        NSRelativeDateTimeFormatter* formatter = [NSRelativeDateTimeFormatter new];
        checkedText = [formatter localizedStringForDate:[NSDate dateWithTimeIntervalSince1970:checked] relativeToDate:NSDate.date];
    }
    [version.rows addObject:[self infoRow:L("IOSLauncher_Update_LastChecked") value:checkedText symbol:@"clock" color:UIColor.systemBlueColor]];

    LauncherGroup* actions = [LauncherGroup groupWithHeader:nil footer:L("IOSLauncher_Update_Footer")];
    [actions.rows addObject:[self actionRow:_checkingUpdates ? L("IOSLauncher_Update_Checking") : L("IOSLauncher_Update_Check")
        symbol:@"arrow.clockwise" action:^(UIView*) { [weakSelf checkForUpdates]; }]];
    NSString* page = [defaults stringForKey:@"launcher.update.url"] ?: ReleasesPage;
    [actions.rows addObject:[self actionRow:L("IOSLauncher_Update_OpenPage") symbol:@"safari" action:^(UIView*)
    {
        [UIApplication.sharedApplication openURL:[NSURL URLWithString:page] options:@{} completionHandler:nil];
    }]];

    NSMutableArray<LauncherGroup*>* groups = [NSMutableArray arrayWithObjects:version, actions, nil];
    NSString* notes = [defaults stringForKey:@"launcher.update.notes"];
    if ([self isUpdateAvailable] && notes.length > 0)
    {
        if (notes.length > 1200)
            notes = [[notes substringToIndex:1200] stringByAppendingString:@"…"];
        [groups addObject:[LauncherGroup groupWithHeader:L("IOSLauncher_Update_Notes") footer:notes]];
    }
    return groups;
}

- (NSArray<LauncherGroup*>*)advancedGroups
{
    __weak LauncherViewController* weakSelf = self;
    LauncherGroup* gameplay = [LauncherGroup groupWithHeader:L("IOSLauncher_Group_Startup") footer:nil];
    [gameplay.rows addObject:ToggleRow(Config::SkipIntroLogos, L("IOSLauncher_SkipIntro"))];

    LauncherGroup* diagnostics = [LauncherGroup groupWithHeader:L("IOSLauncher_Group_Diagnostics") footer:L("IOSLauncher_Diagnostics_Footer")];
    for (const auto& item : g_environmentSwitches)
        [diagnostics.rows addObject:[self environmentRow:item]];

    LauncherGroup* log = [LauncherGroup groupWithHeader:nil footer:nil];
    [log.rows addObject:[self actionRow:L("IOSLauncher_ShareLog") symbol:@"square.and.arrow.up" action:^(UIView* source) { [weakSelf shareLog:source]; }]];

    return @[ gameplay, diagnostics, log ];
}

- (LauncherRow*)environmentRow:(const EnvironmentSwitch&)item
{
    NSString* key = DefaultsKey(item.variable);
    const char* variable = item.variable;
    LauncherRow* row = [LauncherRow new];
    row.kind = LauncherRowToggle;
    row.title = L(item.labelKey);
    row.isOn = ^BOOL { return [NSUserDefaults.standardUserDefaults boolForKey:key]; };
    row.setOn = ^(BOOL on)
    {
        [NSUserDefaults.standardUserDefaults setBool:on forKey:key];
        if (on)
            setenv(variable, "1", 1);
        else
            unsetenv(variable);
    };
    return row;
}

- (LauncherRow*)infoRow:(NSString*)title value:(NSString*)value symbol:(NSString*)symbol color:(UIColor*)color
{
    LauncherRow* row = [LauncherRow new];
    row.kind = LauncherRowInfo;
    row.title = title;
    row.symbol = symbol;
    row.symbolColor = color;
    row.value = ^NSString* { return value; };
    return row;
}

- (LauncherRow*)actionRow:(NSString*)title symbol:(NSString*)symbol action:(void (^)(UIView*))action
{
    LauncherRow* row = [LauncherRow new];
    row.kind = LauncherRowAction;
    row.title = title;
    row.symbol = symbol;
    row.action = action;
    return row;
}

// MARK: Table

- (NSInteger)numberOfSectionsInTableView:(UITableView*)tableView
{
    return _groups.count;
}

- (NSInteger)tableView:(UITableView*)tableView numberOfRowsInSection:(NSInteger)section
{
    return _groups[section].rows.count;
}

- (NSString*)tableView:(UITableView*)tableView titleForHeaderInSection:(NSInteger)section
{
    return _groups[section].header;
}

- (NSString*)tableView:(UITableView*)tableView titleForFooterInSection:(NSInteger)section
{
    return _groups[section].footer;
}

- (UITableViewCell*)tableView:(UITableView*)tableView cellForRowAtIndexPath:(NSIndexPath*)indexPath
{
    LauncherRow* row = _groups[indexPath.section].rows[indexPath.row];
    UITableViewCell* cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleDefault reuseIdentifier:nil];
    UIListContentConfiguration* content = row.kind == LauncherRowInfo || (row.kind == LauncherRowAction && row.value)
        ? [UIListContentConfiguration valueCellConfiguration]
        : [UIListContentConfiguration subtitleCellConfiguration];
    content.text = row.title;
    if (row.symbol)
    {
        content.image = [UIImage systemImageNamed:row.symbol];
        content.imageProperties.tintColor = row.symbolColor ?: self.view.tintColor;
    }

    __weak LauncherViewController* weakSelf = self;
    cell.selectionStyle = UITableViewCellSelectionStyleNone;
    switch (row.kind)
    {
    case LauncherRowInfo:
        content.secondaryText = row.value();
        break;

    case LauncherRowToggle:
    {
        UISwitch* toggle = [UISwitch new];
        toggle.on = row.isOn();
        [toggle addAction:[UIAction actionWithHandler:^(UIAction* action)
        {
            row.setOn(((UISwitch*)action.sender).isOn);
            [weakSelf settingsChanged];
        }] forControlEvents:UIControlEventValueChanged];
        cell.accessoryView = toggle;
        break;
    }

    case LauncherRowMenu:
    {
        // As in Settings: the title gets the full width, the current value sits
        // below it, and a tap anywhere in the row opens the menu. The narrow
        // landscape list leaves no room for both side by side.
        content.secondaryText = row.value();
        content.secondaryTextProperties.color = self.view.tintColor;
        content.secondaryTextProperties.numberOfLines = 1;
        UIImageView* chevron = [[UIImageView alloc] initWithImage:[UIImage systemImageNamed:@"chevron.up.chevron.down"
            withConfiguration:[UIImageSymbolConfiguration configurationWithScale:UIImageSymbolScaleSmall]]];
        chevron.tintColor = UIColor.tertiaryLabelColor;
        cell.accessoryView = chevron;

        UIButton* button = [UIButton buttonWithType:UIButtonTypeCustom];
        button.frame = cell.bounds;
        button.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
        __weak UITableView* weakTable = tableView;
        void (^changed)(void) = ^
        {
            [weakSelf settingsChanged];
            [weakTable reloadRowsAtIndexPaths:@[ indexPath ] withRowAnimation:UITableViewRowAnimationNone];
        };
        // Rebuilt on every open so the checkmark follows the current value; the
        // description replaces the row's second line as the menu title.
        button.menu = [UIMenu menuWithTitle:row.detail ?: @"" children:@[ [UIDeferredMenuElement elementWithUncachedProvider:^(void (^completion)(NSArray<UIMenuElement*>*))
        {
            completion(row.menu(changed));
        }] ]];
        button.showsMenuAsPrimaryAction = YES;
        [cell addSubview:button];
        cell.contentConfiguration = content;
        return cell;
    }

    case LauncherRowSlider:
    {
        // Title and value on one line, a full-width slider below, as in Settings.
        UILabel* title = [UILabel new];
        title.text = row.title;
        title.font = [UIFont preferredFontForTextStyle:UIFontTextStyleBody];
        UILabel* label = [UILabel new];
        label.font = [UIFont monospacedDigitSystemFontOfSize:17 weight:UIFontWeightRegular];
        label.textColor = UIColor.secondaryLabelColor;
        label.textAlignment = NSTextAlignmentRight;
        [label setContentHuggingPriority:UILayoutPriorityRequired forAxis:UILayoutConstraintAxisHorizontal];
        UIStackView* line = [[UIStackView alloc] initWithArrangedSubviews:@[ title, label ]];
        line.spacing = 8;

        UISlider* slider = [UISlider new];
        slider.minimumValue = row.minimum;
        slider.maximumValue = row.maximum;
        slider.value = row.floatValue();
        label.text = row.formatFloat(slider.value);
        __weak UILabel* weakLabel = label;
        [slider addAction:[UIAction actionWithHandler:^(UIAction* action)
        {
            UISlider* sender = (UISlider*)action.sender;
            sender.value = std::round(sender.value * 100.0f) / 100.0f;
            weakLabel.text = row.formatFloat(sender.value);
            row.setFloatValue(sender.value);
            [weakSelf updateHome];
        }] forControlEvents:UIControlEventValueChanged];
        [slider addAction:[UIAction actionWithHandler:^(UIAction*) { [weakSelf settingsChanged]; }]
            forControlEvents:UIControlEventTouchUpInside | UIControlEventTouchUpOutside];

        UIStackView* stack = [[UIStackView alloc] initWithArrangedSubviews:@[ line, slider ]];
        stack.axis = UILayoutConstraintAxisVertical;
        stack.spacing = 6;
        stack.translatesAutoresizingMaskIntoConstraints = NO;
        [cell.contentView addSubview:stack];
        UILayoutGuide* margins = cell.contentView.layoutMarginsGuide;
        [NSLayoutConstraint activateConstraints:@[
            [stack.leadingAnchor constraintEqualToAnchor:margins.leadingAnchor],
            [stack.trailingAnchor constraintEqualToAnchor:margins.trailingAnchor],
            [stack.topAnchor constraintEqualToAnchor:margins.topAnchor],
            [stack.bottomAnchor constraintEqualToAnchor:margins.bottomAnchor],
        ]];
        return cell;
    }

    case LauncherRowAction:
        if (row.value)
            content.secondaryText = row.value();
        else
            content.textProperties.color = self.view.tintColor;
        cell.selectionStyle = UITableViewCellSelectionStyleDefault;
        break;
    }

    if (row.detail.length > 0)
    {
        content.secondaryText = row.detail;
        content.secondaryTextProperties.numberOfLines = 2;
        content.secondaryTextProperties.color = UIColor.secondaryLabelColor;
        content.secondaryTextProperties.font = [UIFont preferredFontForTextStyle:UIFontTextStyleFootnote];
        if (row.kind != LauncherRowAction)
            cell.selectionStyle = UITableViewCellSelectionStyleDefault;
    }
    if (row.action)
        cell.selectionStyle = UITableViewCellSelectionStyleDefault;
    cell.contentConfiguration = content;
    return cell;
}

- (BOOL)tableView:(UITableView*)tableView canMoveRowAtIndexPath:(NSIndexPath*)indexPath
{
    return _reordering && _groups[indexPath.section].rows[indexPath.row].movable;
}

- (UITableViewCellEditingStyle)tableView:(UITableView*)tableView editingStyleForRowAtIndexPath:(NSIndexPath*)indexPath
{
    return UITableViewCellEditingStyleNone;
}

- (BOOL)tableView:(UITableView*)tableView shouldIndentWhileEditingRowAtIndexPath:(NSIndexPath*)indexPath
{
    return NO;
}

- (NSIndexPath*)tableView:(UITableView*)tableView targetIndexPathForMoveFromRowAtIndexPath:(NSIndexPath*)source toProposedIndexPath:(NSIndexPath*)proposed
{
    // Mods only move within their own list.
    if (proposed.section == source.section)
        return proposed;
    const NSInteger last = (NSInteger)_groups[source.section].rows.count - 1;
    return [NSIndexPath indexPathForRow:proposed.section < source.section ? 0 : last inSection:source.section];
}

- (void)tableView:(UITableView*)tableView moveRowAtIndexPath:(NSIndexPath*)source toIndexPath:(NSIndexPath*)destination
{
    const size_t from = source.row, to = destination.row;
    if (from >= _mods.size() || to >= _mods.size() || from == to)
        return;
    IOSMod mod = std::move(_mods[from]);
    _mods.erase(_mods.begin() + from);
    _mods.insert(_mods.begin() + to, std::move(mod));
    [self saveMods];
    // Row blocks capture mod indices; rebuild them once the move animation ends.
    dispatch_async(dispatch_get_main_queue(), ^{ [self rebuildGroups]; });
}

- (void)tableView:(UITableView*)tableView didSelectRowAtIndexPath:(NSIndexPath*)indexPath
{
    [tableView deselectRowAtIndexPath:indexPath animated:YES];
    LauncherRow* row = _groups[indexPath.section].rows[indexPath.row];
    if (_reordering && row.movable)
        return;
    if (row.action)
    {
        row.action([tableView cellForRowAtIndexPath:indexPath]);
    }
    else if (row.detail.length > 0)
    {
        // Descriptions are cut to two lines in the list.
        UIAlertController* alert = [UIAlertController alertControllerWithTitle:row.title message:row.detail preferredStyle:UIAlertControllerStyleAlert];
        [alert addAction:[UIAlertAction actionWithTitle:L("IOSLauncher_OK") style:UIAlertActionStyleCancel handler:nil]];
        [self presentViewController:alert animated:YES completion:nil];
    }
}

// MARK: Actions

- (void)play
{
    [self finish:_status.ready ? IOSLauncherAction::Play : IOSLauncherAction::Install];
}

- (void)finish:(IOSLauncherAction)action
{
    [NSNotificationCenter.defaultCenter removeObserver:self];
    if (self.completion)
        self.completion(action);
}

- (void)openFiles:(id)sender
{
    std::error_code ec;
    std::filesystem::create_directories(GetGamePath(), ec);
    [self openFolder:GetGamePath()];
}

- (void)openFolder:(const std::filesystem::path&)folder
{
    NSString* path = [PathString(folder) stringByAddingPercentEncodingWithAllowedCharacters:NSCharacterSet.URLPathAllowedCharacterSet];
    NSURL* url = [NSURL URLWithString:[@"shareddocuments://" stringByAppendingString:path]];
    [UIApplication.sharedApplication openURL:url options:@{} completionHandler:^(BOOL success)
    {
        if (!success)
            LOGN_WARNING("Could not open the game folder in Files.");
    }];
}

- (void)shareLog:(id)sender
{
    NSURL* log = [NSURL fileURLWithPath:PathString(GetUserPath() / "unleashedrecomp.log")];
    UIActivityViewController* share = [[UIActivityViewController alloc] initWithActivityItems:@[ log ] applicationActivities:nil];
    UIView* source = [sender isKindOfClass:UIView.class] ? (UIView*)sender : self.view;
    share.popoverPresentationController.sourceView = source;
    share.popoverPresentationController.sourceRect = source.bounds;
    [self presentViewController:share animated:YES completion:nil];
}

@end

static UIWindowScene* WaitForWindowScene()
{
    // SDL calls main() from didFinishLaunching; the scene connects shortly after.
    NSDate* deadline = [NSDate dateWithTimeIntervalSinceNow:5.0];
    while (true)
    {
        for (UIScene* scene in UIApplication.sharedApplication.connectedScenes)
        {
            if ([scene isKindOfClass:UIWindowScene.class])
                return (UIWindowScene*)scene;
        }
        if (deadline.timeIntervalSinceNow <= 0)
            return nil;
        [NSRunLoop.currentRunLoop runMode:NSDefaultRunLoopMode beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.02]];
    }
}

IOSLauncherAction RunIOSLauncher()
{
    assert(NSThread.isMainThread);
    ApplyEnvironmentSwitches();
    // The system language, like the Android launcher, not the game's language setting.
    launcher_strings::SetLanguage((NSLocale.preferredLanguages.firstObject ?: @"en").UTF8String);

    __block bool finished = false;
    __block IOSLauncherAction result = IOSLauncherAction::Play;
    @autoreleasepool
    {
        UIWindowScene* scene = WaitForWindowScene();
        UIWindow* window = scene != nil
            ? [[UIWindow alloc] initWithWindowScene:scene]
            : [[UIWindow alloc] initWithFrame:UIScreen.mainScreen.bounds];
        window.overrideUserInterfaceStyle = UIUserInterfaceStyleDark;

        LauncherViewController* launcher = [LauncherViewController new];
        launcher.completion = ^(IOSLauncherAction action)
        {
            result = action;
            finished = true;
        };
        window.rootViewController = launcher;
        [window makeKeyAndVisible];
        LOGN("iOS launcher shown.");

        while (!finished)
        {
            @autoreleasepool
            {
                [NSRunLoop.currentRunLoop runMode:NSDefaultRunLoopMode beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.1]];
            }
        }

        Config::Save();
        window.hidden = YES;
        window.rootViewController = nil;
    }

    LOGFN("iOS launcher finished with action {}.", int(result));
    return result;
}
