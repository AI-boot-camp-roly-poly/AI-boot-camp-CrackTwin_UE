#pragma once

#include "CoreMinimal.h"

class APawn;

namespace CrackInspection
{
FName SlotTag(int32 Slot);
bool BuildView(const FTransform& Decal, FTransform& View, double StandOffCm = 500.0);
bool TryJump(APawn& Pawn, int32 Slot, bool bLocked);
}
