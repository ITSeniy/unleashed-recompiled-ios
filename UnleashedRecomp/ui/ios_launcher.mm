#include <stdafx.h>
#include "ios_launcher.h"
#include "ios_mod_manager.h"
#include "ios_mod_settings.h"
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
    { "UNLEASHED_IOS_FORCE_NO_BC", "IOSLauncher_ForceETC2" },
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
    return NS(Localise(key));
}

static NSString* PathString(const std::filesystem::path& path)
{
    return [NSString stringWithUTF8String:(const char*)path.u8string().c_str()] ?: @"";
}

static NSString* OneLine(const std::string& text)
{
    return [NS(text) stringByReplacingOccurrencesOfString:@"\n" withString:@" "];
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
    row.title = title ?: NS(def.GetNameLocalised(Config::Language));
    if (title == nil)
        row.detail = OneLine(def.GetDescription(Config::Language));
    row.isOn = ^BOOL { return config->Value; };
    row.setOn = ^(BOOL on)
    {
        config->Value = on;
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
    row.title = NS(def.GetNameLocalised(Config::Language));
    row.detail = OneLine(def.GetDescription(Config::Language));
    row.value = ^NSString* { return NS(config->GetValueLocalised(Config::Language)); };
    row.menu = ^NSArray<UIMenuElement*>* (void (^changed)(void))
    {
        NSMutableArray<UIMenuElement*>* actions = [NSMutableArray array];
        const T current = config->Value;
        for (const auto& [value, name] : config->EnumTemplateReverse)
        {
            if (config->InaccessibleValues.contains(value))
                continue;

            // The localised name is only exposed for the current value.
            config->Value = value;
            NSString* label = NS(config->GetValueLocalised(Config::Language));
            config->Value = current;

            const T choice = value;
            UIAction* action = [UIAction actionWithTitle:label image:nil identifier:nil handler:^(UIAction*)
            {
                config->Value = choice;
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
    _sectionTitles = @[ L("IOSLauncher_Section_Game"), L("IOSLauncher_Section_Graphics"), L("IOSLauncher_Section_Mods"), L("IOSLauncher_Section_Advanced") ];
    _sectionSymbols = @[ @"gamecontroller", @"display", @"puzzlepiece.extension", @"wrench.and.screwdriver" ];
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

    UIButton* mods = [self quickButton:L("IOSLauncher_Section_Mods") symbol:@"puzzlepiece.extension" action:@selector(showMods)];
    UIButton* files = [self quickButton:L("IOSLauncher_Files") symbol:@"folder" action:@selector(openFiles:)];
    UIButton* log = [self quickButton:L("IOSLauncher_ShareLog") symbol:@"doc.text" action:@selector(shareLog:)];
    UIStackView* quick = [[UIStackView alloc] initWithArrangedSubviews:@[ mods, files, log ]];
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

- (void)showMods
{
    [self selectSection:2];
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
    const auto activeMods = std::count_if(_mods.begin(), _mods.end(), [](const IOSMod& mod) { return mod.enabled; });
    if (activeMods > 0)
    {
        NSString* mods = [L("IOSLauncher_Chip_Mods") stringByReplacingOccurrencesOfString:@"{}" withString:@(activeMods).stringValue];
        [_chips addArrangedSubview:[self chip:mods color:UIColor.systemPurpleColor]];
    }

    NSMutableArray<NSString*>* parts = [NSMutableArray array];
    [parts addObject:[NSString stringWithFormat:@"%d%%", int(std::lround(Config::ResolutionScale * 100.0f))]];
    if (Config::MetalFXUpscaling && Config::ResolutionScale < 1.0f)
        [parts addObject:@"MetalFX"];
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
}

- (void)rebuildGroups
{
    switch (_section)
    {
    case 1: _groups = [self graphicsGroups]; break;
    case 2: _groups = [self modsGroups]; break;
    case 3: _groups = [self advancedGroups]; break;
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

    return _status.ready ? @[ files, dlc, actions ] : @[ files, actions ];
}

- (NSArray<LauncherGroup*>*)graphicsGroups
{
    LauncherGroup* resolution = [LauncherGroup groupWithHeader:L("IOSLauncher_Group_Resolution") footer:L("IOSLauncher_Resolution_Footer")];
    LauncherRow* scale = [LauncherRow new];
    scale.kind = LauncherRowSlider;
    scale.title = NS(Config::ResolutionScale.GetNameLocalised(Config::Language));
    scale.minimum = 0.25f;
    scale.maximum = std::max(1.0f, float(Config::ResolutionScale));
    scale.floatValue = ^float { return Config::ResolutionScale; };
    scale.setFloatValue = ^(float value) { Config::ResolutionScale = std::round(value * 100.0f) / 100.0f; };
    scale.formatFloat = ^NSString* (float value) { return [NSString stringWithFormat:@"%d%%", int(std::lround(value * 100.0f))]; };
    [resolution.rows addObject:scale];
    [resolution.rows addObject:ToggleRow(Config::MetalFXUpscaling)];

    LauncherGroup* frameRate = [LauncherGroup groupWithHeader:L("IOSLauncher_Group_FrameRate") footer:nil];
    LauncherRow* fps = [LauncherRow new];
    fps.kind = LauncherRowMenu;
    fps.title = NS(Config::FPS.GetNameLocalised(Config::Language));
    fps.detail = OneLine(Config::FPS.GetDescription(Config::Language));
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
    [frameRate.rows addObject:ToggleRow(Config::VSync)];

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
    [overlays.rows addObject:ToggleRow(Config::MetalHUD, @"Metal HUD")];

    return @[ resolution, frameRate, quality, optimizations, overlays ];
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

- (NSArray<LauncherGroup*>*)advancedGroups
{
    __weak LauncherViewController* weakSelf = self;
    LauncherGroup* gameplay = [LauncherGroup groupWithHeader:L("IOSLauncher_Group_Startup") footer:nil];
    [gameplay.rows addObject:ToggleRow(Config::SkipIntroLogos, L("IOSLauncher_SkipIntro"))];

    LauncherGroup* testing = [LauncherGroup groupWithHeader:L("IOSLauncher_Group_Testing") footer:L("IOSLauncher_Testing_Footer")];
    [testing.rows addObject:[self environmentRow:g_environmentSwitches[0]]];

    LauncherGroup* diagnostics = [LauncherGroup groupWithHeader:L("IOSLauncher_Group_Diagnostics") footer:L("IOSLauncher_Diagnostics_Footer")];
    for (size_t i = 1; i < std::size(g_environmentSwitches); i++)
        [diagnostics.rows addObject:[self environmentRow:g_environmentSwitches[i]]];

    LauncherGroup* log = [LauncherGroup groupWithHeader:nil footer:nil];
    [log.rows addObject:[self actionRow:L("IOSLauncher_ShareLog") symbol:@"square.and.arrow.up" action:^(UIView* source) { [weakSelf shareLog:source]; }]];

    return @[ gameplay, testing, diagnostics, log ];
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
        UIButtonConfiguration* config = [UIButtonConfiguration plainButtonConfiguration];
        config.title = row.value();
        config.image = [UIImage systemImageNamed:@"chevron.up.chevron.down"];
        config.imagePlacement = NSDirectionalRectEdgeTrailing;
        config.imagePadding = 4;
        config.preferredSymbolConfigurationForImage = [UIImageSymbolConfiguration configurationWithScale:UIImageSymbolScaleSmall];
        UIButton* button = [UIButton buttonWithConfiguration:config primaryAction:nil];
        __weak UIButton* weakButton = button;
        void (^changed)(void) = ^
        {
            UIButtonConfiguration* updated = weakButton.configuration;
            updated.title = row.value();
            weakButton.configuration = updated;
            [weakButton sizeToFit];
            [weakSelf settingsChanged];
        };
        // Rebuild on every open so the checkmark follows the current value.
        button.menu = [UIMenu menuWithChildren:@[ [UIDeferredMenuElement elementWithUncachedProvider:^(void (^completion)(NSArray<UIMenuElement*>*))
        {
            completion(row.menu(changed));
        }] ]];
        button.showsMenuAsPrimaryAction = YES;
        [button sizeToFit];
        cell.accessoryView = button;
        break;
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
