#pragma once

// Functions exported by the ASA Permissions plugin (Permissions.dll), linked through lib\Permissions.lib.
// Signature matches the decorated name in that import library:
//   ?GetPlayerGroups@Permissions@@YA?AV?$TArray@VFString@@V?$TSizedDefaultAllocator@$0CA@@@@@AEBVFString@@@Z
//
// Returns the player's permanent groups, their tribe's groups and any timed groups that are active
// right now (Permissions itself drops timed groups that are still delayed or already expired).

#include "API/ARK/Ark.h"

namespace Permissions
{
    __declspec(dllimport) TArray<FString> GetPlayerGroups(const FString& eos_id);
}
