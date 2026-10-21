#pragma once

// Functions exported by the ASA Permissions plugin (Permissions.dll), linked through lib\Permissions.lib.
// Signatures match the decorated names in that import library:
//   ?GetPlayerGroups@Permissions@@YA?AV?$TArray@VFString@@V?$TSizedDefaultAllocator@$0CA@@@@@AEBVFString@@@Z
//   ?SubscribePermissionGroupUpdatedCallback@Permissions@@YAXVFString@@AEBV?$function@$$A6AXAEBVFString@@H@Z@std@@@Z
//   ?UnSubscribePermissionGroupUpdatedCallback@Permissions@@YAXVFString@@@Z

#include <functional>

#include "API/ARK/Ark.h"

namespace Permissions
{
    // The player's permanent groups, their tribe's groups and any timed groups that are active right
    // now (Permissions itself drops timed groups that are still delayed or already expired).
    __declspec(dllimport) TArray<FString> GetPlayerGroups(const FString& eos_id);

    // Called by Permissions when a player's (eos_id) or a tribe's (tribe_id) groups change, e.g. on
    // permissions.addtimed. A timed group simply expiring is not a change it reports, so the periodic
    // recheck still covers expiry.
    __declspec(dllimport) void SubscribePermissionGroupUpdatedCallback(FString callback_name,
        const std::function<void(const FString& eos_id, int tribe_id)>& callback);
    __declspec(dllimport) void UnSubscribePermissionGroupUpdatedCallback(FString callback_name);
}
