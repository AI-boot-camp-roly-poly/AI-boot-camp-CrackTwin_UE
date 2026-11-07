#include "CrackInspectionNavigation.h"

#include "CollisionQueryParams.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PawnMovementComponent.h"

FName CrackInspection::SlotTag(int32 Slot)
{
    return Slot >= 1 && Slot <= 9 ? FName(*FString::Printf(TEXT("CrackInspection.%d"), Slot)) : NAME_None;
}

bool CrackInspection::BuildView(const FTransform& Decal, FTransform& View, double StandOffCm)
{
    if (!Decal.IsValid() || !FMath::IsFinite(StandOffCm) || StandOffCm < 100.0) return false;
    const FVector Outward = Decal.GetUnitAxis(EAxis::X);
    View = FTransform((-Outward).Rotation(), Decal.GetLocation() + Outward * StandOffCm);
    return true;
}

bool CrackInspection::TryJump(APawn& Pawn, int32 Slot, bool bLocked)
{
    UWorld* World = Pawn.GetWorld();
    const FName Tag = SlotTag(Slot);
    if (bLocked || !World || Tag.IsNone() || !Pawn.GetController()) return false;

    AActor* Target = nullptr;
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        if (!It->ActorHasTag(Tag)) continue;
        if (Target) return false; // Ambiguous tags must never move to an arbitrary actor.
        Target = *It;
    }
    if (!Target) return false;
    FTransform View;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(CrackInspectionJump), false, &Pawn);
    bool bClear = false;
    // A lower facade can sit behind a projecting podium: keep the camera outside it.
    for (double Distance : {500.0, 750.0, 1000.0})
    {
        if (!BuildView(Target->GetActorTransform(), View, Distance)) return false;
        if (!World->OverlapBlockingTestByChannel(View.GetLocation(), FQuat::Identity, ECC_Pawn,
            FCollisionShape::MakeSphere(FMath::Max(1.f, Pawn.GetSimpleCollisionRadius())), Query))
        {
            bClear = true;
            break;
        }
    }
    if (!bClear) return false;

    if (!Pawn.GetActorLocation().Equals(View.GetLocation(), 0.01)
        && !Pawn.SetActorLocation(View.GetLocation(), false, nullptr, ETeleportType::TeleportPhysics)) return false;
    if (UPawnMovementComponent* Movement = Pawn.GetMovementComponent()) Movement->StopMovementImmediately();
    Pawn.ConsumeMovementInputVector();
    Pawn.SetActorRotation(FRotator(0, View.Rotator().Yaw, 0));
    Pawn.GetController()->SetControlRotation(View.Rotator());
    return true;
}
