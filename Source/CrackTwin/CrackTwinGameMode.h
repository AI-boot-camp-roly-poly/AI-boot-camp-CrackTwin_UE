#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "CrackTwinGameMode.generated.h"

/** Spawns the inspection drone as the player pawn. */
UCLASS()
class CRACKTWIN_API ACrackTwinGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ACrackTwinGameMode();
};
