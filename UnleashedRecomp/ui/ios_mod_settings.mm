#include <stdafx.h>
#include "ios_mod_settings.h"
#include "ios_launcher_strings.h"
#include "ios_mod_manager.h"
#include <locale/locale.h>
#include <mod/ini_file.h>
#include <os/logger.h>
#include <cmath>
#include <fstream>
#include <set>

#if !__has_feature(objc_arc)
#error "ios_mod_settings.mm requires ARC."
#endif

static constexpr NSUInteger MaxFields = 256;

static NSString* L(const char* key)
{
    return [NSString stringWithUTF8String:launcher_strings::Get(key)] ?: @"";
}

static std::string Utf8(NSString* value)
{
    return value.UTF8String ?: "";
}

static NSString* StringValue(id value)
{
    if (value == nil || value == NSNull.null)
        return @"";
    if ([value isKindOfClass:NSString.class])
        return value;
    if ([value isKindOfClass:NSNumber.class])
    {
        NSNumber* number = value;
        // JSON true/false arrive as NSNumber booleans.
        if (CFGetTypeID((__bridge CFTypeRef)number) == CFBooleanGetTypeID())
            return number.boolValue ? @"true" : @"false";
        return number.stringValue;
    }
    return [value description];
}

@interface ModSettingsField : NSObject
@property (nonatomic, copy) NSString* section;
@property (nonatomic, copy) NSString* name;
@property (nonatomic, copy) NSString* displayName;
@property (nonatomic, copy) NSString* detail;
@property (nonatomic, copy) NSString* type;
@property (nonatomic, copy) NSString* value;
@property (nonatomic, strong) NSNumber* minimum;
@property (nonatomic, strong) NSNumber* maximum;
@property (nonatomic, strong) NSArray<NSArray<NSString*>*>* choices; // { label, value }
@end

@implementation ModSettingsField
@end

@interface ModSettingsGroup : NSObject
@property (nonatomic, copy) NSString* title;
@property (nonatomic, strong) NSMutableArray<ModSettingsField*>* fields;
@end

@implementation ModSettingsGroup
@end

@interface ModSettingsViewController : UITableViewController
- (instancetype)initWithTitle:(NSString*)title groups:(NSArray<ModSettingsGroup*>*)groups iniPath:(std::filesystem::path)iniPath;
@end

@implementation ModSettingsViewController
{
    NSArray<ModSettingsGroup*>* _groups;
    std::filesystem::path _iniPath;
}

- (instancetype)initWithTitle:(NSString*)title groups:(NSArray<ModSettingsGroup*>*)groups iniPath:(std::filesystem::path)iniPath
{
    if ((self = [super initWithStyle:UITableViewStyleInsetGrouped]))
    {
        self.title = title;
        _groups = groups;
        _iniPath = std::move(iniPath);
    }
    return self;
}

- (void)viewDidLoad
{
    [super viewDidLoad];
    self.navigationItem.leftBarButtonItem = [[UIBarButtonItem alloc] initWithBarButtonSystemItem:UIBarButtonSystemItemCancel target:self action:@selector(cancel)];
    self.navigationItem.rightBarButtonItem = [[UIBarButtonItem alloc] initWithBarButtonSystemItem:UIBarButtonSystemItemSave target:self action:@selector(save)];
    self.tableView.keyboardDismissMode = UIScrollViewKeyboardDismissModeOnDrag;
}

- (NSInteger)numberOfSectionsInTableView:(UITableView*)tableView
{
    return _groups.count;
}

- (NSInteger)tableView:(UITableView*)tableView numberOfRowsInSection:(NSInteger)section
{
    return _groups[section].fields.count;
}

- (NSString*)tableView:(UITableView*)tableView titleForHeaderInSection:(NSInteger)section
{
    return _groups[section].title;
}

- (UITableViewCell*)tableView:(UITableView*)tableView cellForRowAtIndexPath:(NSIndexPath*)indexPath
{
    ModSettingsField* field = _groups[indexPath.section].fields[indexPath.row];
    UITableViewCell* cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleDefault reuseIdentifier:nil];
    cell.selectionStyle = field.detail.length > 0 ? UITableViewCellSelectionStyleDefault : UITableViewCellSelectionStyleNone;

    UIListContentConfiguration* content = [UIListContentConfiguration subtitleCellConfiguration];
    content.text = field.displayName;
    if (field.detail.length > 0)
    {
        content.secondaryText = field.detail;
        content.secondaryTextProperties.numberOfLines = 2;
        content.secondaryTextProperties.color = UIColor.secondaryLabelColor;
        content.secondaryTextProperties.font = [UIFont preferredFontForTextStyle:UIFontTextStyleFootnote];
    }
    cell.contentConfiguration = content;

    if ([field.type isEqualToString:@"bool"])
    {
        UISwitch* toggle = [UISwitch new];
        toggle.on = [field.value caseInsensitiveCompare:@"true"] == NSOrderedSame || [field.value isEqualToString:@"1"];
        [toggle addAction:[UIAction actionWithHandler:^(UIAction* action)
        {
            field.value = ((UISwitch*)action.sender).isOn ? @"true" : @"false";
        }] forControlEvents:UIControlEventValueChanged];
        cell.accessoryView = toggle;
    }
    else if (field.choices != nil)
    {
        NSString* current = field.value;
        for (NSArray<NSString*>* choice in field.choices)
        {
            if ([choice[1] isEqualToString:field.value])
                current = choice[0];
        }
        UIButtonConfiguration* config = [UIButtonConfiguration plainButtonConfiguration];
        config.title = current;
        config.image = [UIImage systemImageNamed:@"chevron.up.chevron.down"];
        config.imagePlacement = NSDirectionalRectEdgeTrailing;
        config.imagePadding = 4;
        config.preferredSymbolConfigurationForImage = [UIImageSymbolConfiguration configurationWithScale:UIImageSymbolScaleSmall];
        UIButton* button = [UIButton buttonWithConfiguration:config primaryAction:nil];
        NSMutableArray<UIMenuElement*>* actions = [NSMutableArray array];
        __weak UIButton* weakButton = button;
        for (NSArray<NSString*>* choice in field.choices)
        {
            UIAction* action = [UIAction actionWithTitle:choice[0] image:nil identifier:nil handler:^(UIAction*)
            {
                field.value = choice[1];
                UIButtonConfiguration* updated = weakButton.configuration;
                updated.title = choice[0];
                weakButton.configuration = updated;
                [weakButton sizeToFit];
            }];
            [actions addObject:action];
        }
        button.menu = [UIMenu menuWithChildren:actions];
        button.showsMenuAsPrimaryAction = YES;
        button.changesSelectionAsPrimaryAction = YES;
        [button sizeToFit];
        cell.accessoryView = button;
    }
    else
    {
        UITextField* text = [[UITextField alloc] initWithFrame:CGRectMake(0, 0, 180, 32)];
        text.text = field.value;
        text.textAlignment = NSTextAlignmentRight;
        text.borderStyle = UITextBorderStyleRoundedRect;
        text.autocorrectionType = UITextAutocorrectionTypeNo;
        text.autocapitalizationType = UITextAutocapitalizationTypeNone;
        if ([field.type isEqualToString:@"int"])
            text.keyboardType = UIKeyboardTypeNumbersAndPunctuation;
        else if ([field.type isEqualToString:@"float"])
            text.keyboardType = UIKeyboardTypeNumbersAndPunctuation;
        [text addAction:[UIAction actionWithHandler:^(UIAction* action)
        {
            field.value = ((UITextField*)action.sender).text ?: @"";
        }] forControlEvents:UIControlEventEditingChanged];
        cell.accessoryView = text;
    }
    return cell;
}

- (void)tableView:(UITableView*)tableView didSelectRowAtIndexPath:(NSIndexPath*)indexPath
{
    [tableView deselectRowAtIndexPath:indexPath animated:YES];
    ModSettingsField* field = _groups[indexPath.section].fields[indexPath.row];
    if (field.detail.length == 0)
        return;
    UIAlertController* alert = [UIAlertController alertControllerWithTitle:field.displayName message:field.detail preferredStyle:UIAlertControllerStyleAlert];
    [alert addAction:[UIAlertAction actionWithTitle:L("IOSLauncher_OK") style:UIAlertActionStyleCancel handler:nil]];
    [self presentViewController:alert animated:YES completion:nil];
}

- (void)cancel
{
    [self dismissViewControllerAnimated:YES completion:nil];
}

- (NSString*)validate:(ModSettingsField*)field
{
    NSString* value = field.value;
    if ([value rangeOfCharacterFromSet:[NSCharacterSet characterSetWithCharactersInString:@"\"\r\n"]].location != NSNotFound)
        return [L("IOSLauncher_ModSettings_Invalid") stringByReplacingOccurrencesOfString:@"{}" withString:field.displayName];

    const bool isInt = [field.type isEqualToString:@"int"];
    if (isInt || [field.type isEqualToString:@"float"])
    {
        NSScanner* scanner = [NSScanner scannerWithString:value];
        scanner.locale = [NSLocale localeWithLocaleIdentifier:@"en_US_POSIX"];
        double number = 0;
        if (![scanner scanDouble:&number] || !scanner.isAtEnd || (isInt && number != std::floor(number)) ||
            (field.minimum && number < field.minimum.doubleValue) || (field.maximum && number > field.maximum.doubleValue))
        {
            return [L("IOSLauncher_ModSettings_Invalid") stringByReplacingOccurrencesOfString:@"{}" withString:field.displayName];
        }
        if (isInt)
            field.value = [NSString stringWithFormat:@"%lld", (long long)number];
    }
    return nil;
}

static std::string EncodeValue(ModSettingsField* field)
{
    std::string value = Utf8(field.value);
    const bool quote = [field.type isEqualToString:@"string"] || value.empty() ||
        value.find_first_of(" \t;=") != std::string::npos;
    return quote ? "\"" + value + "\"" : value;
}

- (void)save
{
    [self.view endEditing:YES];

    // section -> (name -> encoded value), in schema order.
    std::vector<std::pair<std::string, std::vector<std::pair<std::string, std::string>>>> changes;
    for (ModSettingsGroup* group in _groups)
    {
        for (ModSettingsField* field in group.fields)
        {
            if (NSString* problem = [self validate:field])
            {
                UIAlertController* alert = [UIAlertController alertControllerWithTitle:nil message:problem preferredStyle:UIAlertControllerStyleAlert];
                [alert addAction:[UIAlertAction actionWithTitle:L("IOSLauncher_OK") style:UIAlertActionStyleCancel handler:nil]];
                [self presentViewController:alert animated:YES completion:nil];
                return;
            }

            const std::string section = Utf8(field.section);
            auto found = std::find_if(changes.begin(), changes.end(), [&](const auto& item) { return item.first == section; });
            if (found == changes.end())
                found = changes.insert(changes.end(), { section, {} });
            found->second.emplace_back(Utf8(field.name), EncodeValue(field));
        }
    }

    // Rewrite matching keys in place and append the rest, keeping comments and
    // unknown keys untouched.
    std::vector<std::string> lines;
    {
        std::ifstream file(_iniPath, std::ios::binary);
        for (std::string line; std::getline(file, line);)
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            lines.push_back(line);
        }
    }

    std::set<std::pair<std::string, std::string>> written;
    std::string section;
    auto lookup = [&](const std::string& sectionName, const std::string& key) -> const std::string*
    {
        for (const auto& [name, values] : changes)
        {
            if (name != sectionName)
                continue;
            for (const auto& [valueName, value] : values)
            {
                if (valueName == key)
                    return &value;
            }
        }
        return nullptr;
    };
    auto trim = [](std::string value)
    {
        value.erase(0, value.find_first_not_of(" \t"));
        value.erase(value.find_last_not_of(" \t") + 1);
        return value;
    };

    std::vector<std::string> output;
    auto flushSection = [&](const std::string& name)
    {
        for (const auto& [sectionName, values] : changes)
        {
            if (sectionName != name)
                continue;
            for (const auto& [key, value] : values)
            {
                if (written.insert({ name, key }).second)
                    output.push_back(key + "=" + value);
            }
        }
    };

    bool inSection = false;
    for (const std::string& line : lines)
    {
        const std::string trimmed = trim(line);
        if (trimmed.size() > 1 && trimmed.front() == '[' && trimmed.back() == ']')
        {
            if (inSection)
                flushSection(section);
            section = trimmed.substr(1, trimmed.size() - 2);
            inSection = true;
            output.push_back(line);
            continue;
        }

        const size_t equals = trimmed.find('=');
        if (inSection && equals != std::string::npos && trimmed.front() != ';')
        {
            std::string key = trim(trimmed.substr(0, equals));
            if (key.size() > 1 && key.front() == '"' && key.back() == '"')
                key = key.substr(1, key.size() - 2);
            if (const std::string* value = lookup(section, key))
            {
                output.push_back(key + "=" + *value);
                written.insert({ section, key });
                continue;
            }
        }
        output.push_back(line);
    }
    if (inSection)
        flushSection(section);

    for (const auto& [name, values] : changes)
    {
        bool header = false;
        for (const auto& [key, value] : values)
        {
            if (written.contains({ name, key }))
                continue;
            if (!header)
            {
                if (!output.empty() && !output.back().empty())
                    output.push_back("");
                output.push_back("[" + name + "]");
                header = true;
            }
            output.push_back(key + "=" + value);
            written.insert({ name, key });
        }
    }

    std::string contents;
    for (const std::string& line : output)
        contents += line + "\n";

    auto temporary = _iniPath;
    temporary += ".tmp";
    bool saved;
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        file.write(contents.data(), contents.size());
        saved = file.good();
    }
    std::error_code ec;
    if (saved)
        std::filesystem::rename(temporary, _iniPath, ec);
    if (!saved || ec)
    {
        std::filesystem::remove(temporary, ec);
        UIAlertController* alert = [UIAlertController alertControllerWithTitle:nil message:L("IOSLauncher_ModSettings_SaveFailed") preferredStyle:UIAlertControllerStyleAlert];
        [alert addAction:[UIAlertAction actionWithTitle:L("IOSLauncher_OK") style:UIAlertActionStyleCancel handler:nil]];
        [self presentViewController:alert animated:YES completion:nil];
        return;
    }

    LOGN("Saved mod settings.");
    [self dismissViewControllerAnimated:YES completion:nil];
}

@end

static NSString* JoinDescription(id description)
{
    if ([description isKindOfClass:NSArray.class])
    {
        NSMutableArray<NSString*>* lines = [NSMutableArray array];
        for (id line in (NSArray*)description)
            [lines addObject:StringValue(line)];
        return [lines componentsJoinedByString:@" "];
    }
    return StringValue(description);
}

static bool IsCleanName(NSString* name)
{
    return name.length > 0 && [name rangeOfCharacterFromSet:[NSCharacterSet characterSetWithCharactersInString:@"[]=\"\r\n"]].location == NSNotFound;
}

UIViewController* CreateIOSModSettingsController(const IOSMod& mod, NSString** error)
{
    auto fail = [&](const char* key) -> UIViewController*
    {
        if (error)
            *error = L(key);
        return nil;
    };

    NSData* data = [NSData dataWithContentsOfFile:[NSString stringWithUTF8String:(const char*)mod.schemaPath.u8string().c_str()]];
    if (data == nil)
        return fail("IOSLauncher_ModSettings_Unreadable");

    // HMM schemas follow JSON.NET, which accepts comments and trailing commas.
    NSError* jsonError = nil;
    NSDictionary* schema = [NSJSONSerialization JSONObjectWithData:data options:NSJSONReadingJSON5Allowed error:&jsonError];
    if (![schema isKindOfClass:NSDictionary.class] || ![schema[@"Groups"] isKindOfClass:NSArray.class])
        return fail("IOSLauncher_ModSettings_Unreadable");

    const auto modRoot = mod.iniPath.parent_path().lexically_normal();
    NSString* iniName = [schema[@"IniFile"] isKindOfClass:NSString.class] ? schema[@"IniFile"] : @"config.ini";
    auto iniPath = (modRoot / std::filesystem::path(std::u8string_view((const char8_t*)iniName.UTF8String))).lexically_normal();
    const auto relative = iniPath.lexically_relative(modRoot);
    if (relative.empty() || *relative.begin() == "..")
        return fail("IOSLauncher_ModSettings_Unreadable");

    IniFile current;
    current.read(iniPath);
    NSDictionary* enums = [schema[@"Enums"] isKindOfClass:NSDictionary.class] ? schema[@"Enums"] : @{};

    NSMutableArray<ModSettingsGroup*>* groups = [NSMutableArray array];
    NSUInteger fieldCount = 0;
    for (NSDictionary* groupInfo in (NSArray*)schema[@"Groups"])
    {
        if (![groupInfo isKindOfClass:NSDictionary.class])
            continue;
        NSString* section = StringValue(groupInfo[@"Name"]);
        if (section.length == 0)
            section = @"ConfigGroup";
        if (!IsCleanName(section))
            return fail("IOSLauncher_ModSettings_Unreadable");

        ModSettingsGroup* group = [ModSettingsGroup new];
        group.title = [groupInfo[@"DisplayName"] isKindOfClass:NSString.class] ? groupInfo[@"DisplayName"] : section;
        group.fields = [NSMutableArray array];

        NSArray* elements = [groupInfo[@"Elements"] isKindOfClass:NSArray.class] ? groupInfo[@"Elements"] : @[];
        for (NSDictionary* element in elements)
        {
            if (![element isKindOfClass:NSDictionary.class])
                continue;
            if (++fieldCount > MaxFields)
                return fail("IOSLauncher_ModSettings_Unreadable");

            ModSettingsField* field = [ModSettingsField new];
            field.section = section;
            field.name = StringValue(element[@"Name"]);
            if (field.name.length == 0)
                field.name = @"ConfigName";
            if (!IsCleanName(field.name))
                return fail("IOSLauncher_ModSettings_Unreadable");
            field.displayName = [element[@"DisplayName"] isKindOfClass:NSString.class] ? element[@"DisplayName"] : field.name;
            field.detail = JoinDescription(element[@"Description"]);

            NSString* type = StringValue(element[@"Type"]);
            NSArray* enumValues = [enums[type] isKindOfClass:NSArray.class] ? enums[type] : nil;
            field.type = enumValues ? @"enum" : type.lowercaseString;
            if (!enumValues && ![@[ @"bool", @"int", @"float" ] containsObject:field.type])
                field.type = @"string";

            field.minimum = [element[@"MinValue"] isKindOfClass:NSNumber.class] ? element[@"MinValue"] : nil;
            field.maximum = [element[@"MaxValue"] isKindOfClass:NSNumber.class] ? element[@"MaxValue"] : nil;

            const std::string saved = current.getString(Utf8(section), Utf8(field.name), "\x01");
            field.value = saved != "\x01" ? [NSString stringWithUTF8String:saved.c_str()] : StringValue(element[@"DefaultValue"]);

            if (enumValues)
            {
                NSMutableArray<NSArray<NSString*>*>* choices = [NSMutableArray array];
                for (NSDictionary* choice in enumValues)
                {
                    if (![choice isKindOfClass:NSDictionary.class])
                        continue;
                    NSString* value = StringValue(choice[@"Value"]);
                    NSString* label = [choice[@"DisplayName"] isKindOfClass:NSString.class] ? choice[@"DisplayName"] : value;
                    [choices addObject:@[ label, value ]];
                }
                field.choices = choices;
            }
            [group.fields addObject:field];
        }
        [groups addObject:group];
    }

    if (fieldCount == 0)
        return fail("IOSLauncher_ModSettings_Empty");

    NSString* title = [NSString stringWithUTF8String:mod.title.c_str()] ?: @"";
    ModSettingsViewController* settings = [[ModSettingsViewController alloc] initWithTitle:title groups:groups iniPath:iniPath];
    UINavigationController* navigation = [[UINavigationController alloc] initWithRootViewController:settings];
    navigation.modalPresentationStyle = UIModalPresentationFormSheet;
    return navigation;
}
