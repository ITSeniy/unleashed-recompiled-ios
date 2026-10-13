#pragma once

#import <UIKit/UIKit.h>

struct IOSMod;

// Editor for an HMM ConfigSchemaFile, writing the mod's own ini file.
// Returns nil and sets error when the schema cannot be read.
UIViewController* CreateIOSModSettingsController(const IOSMod& mod, NSString** error);
