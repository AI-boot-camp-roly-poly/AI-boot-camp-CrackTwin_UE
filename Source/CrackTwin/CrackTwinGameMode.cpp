#include "CrackTwinGameMode.h"

#include "CrackDronePawn.h"

ACrackTwinGameMode::ACrackTwinGameMode()
{
	DefaultPawnClass = ACrackDronePawn::StaticClass();
}
