#pragma once

// Functions exported by the ASE Permissions plugin that ships with ArkServerApi (Permissions.dll),
// linked through lib\Permissions.lib. Declaration matches Permissions/Public/ArkPermissions.h:
//   ?GetPlayerGroups@Permissions@@YA?AV?$TArray@VFString@@VFDefaultAllocator@@@@_K@Z
//
// Returns the player's permanent groups, their tribe's groups (plus "TribeSize:N"/"TribeOnline:N"
// for an online player) and any timed groups that are active right now. Unlike the ASA build, the
// ASE Permissions plugin has no group-change notification, so changes are found by polling.

#include "API/ARK/Ark.h"

namespace Permissions
{
    __declspec(dllimport) TArray<FString> GetPlayerGroups(uint64 steam_id);
}
