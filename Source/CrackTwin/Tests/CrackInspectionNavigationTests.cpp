#include "Misc/AutomationTest.h"
#include "CrackInspectionNavigation.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrackInspectionViewTest, "CrackTwin.Navigation.View",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCrackInspectionViewTest::RunTest(const FString& Parameters)
{
    for (const FRotator Rotation : {FRotator(0, 180, 0), FRotator(0, 90, 90), FRotator(20, 35, 0)})
    {
        const FTransform Decal(Rotation, FVector(-4305, 0, 1400), FVector(2, 3, 4));
        FTransform View;
        TestTrue(TEXT("valid decal creates a camera pose"), CrackInspection::BuildView(Decal, View));
        TestTrue(TEXT("camera stays 5m outside the wall regardless of decal scale"),
            FMath::IsNearlyEqual(FVector::Distance(View.GetLocation(), Decal.GetLocation()), 500.0, 0.001));
        const FVector TowardCrack = (Decal.GetLocation() - View.GetLocation()).GetSafeNormal();
        TestTrue(TEXT("camera looks at crack center"), View.GetUnitAxis(EAxis::X).Equals(TowardCrack, 0.001));
        TestTrue(TEXT("camera is on decal outward side"),
            FVector::DotProduct(View.GetLocation() - Decal.GetLocation(), Decal.GetUnitAxis(EAxis::X)) > 499.9);
    }
    FTransform Unchanged(FVector(123, 456, 789));
    TestFalse(TEXT("zero standoff cannot put camera in wall"), CrackInspection::BuildView(FTransform::Identity, Unchanged, 0));
    TestTrue(TEXT("invalid pose leaves destination untouched"), Unchanged.GetLocation().Equals(FVector(123, 456, 789)));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrackInspectionSlotTest, "CrackTwin.Navigation.Slots",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCrackInspectionSlotTest::RunTest(const FString& Parameters)
{
    TestTrue(TEXT("zero has no destination"), CrackInspection::SlotTag(0).IsNone());
    TestTrue(TEXT("ten has no destination"), CrackInspection::SlotTag(10).IsNone());
    TestTrue(TEXT("negative has no destination"), CrackInspection::SlotTag(-1).IsNone());
    for (int32 Slot = 1; Slot <= 9; ++Slot)
        TestEqual(TEXT("stable tag for each numbered destination"), CrackInspection::SlotTag(Slot),
            FName(*FString::Printf(TEXT("CrackInspection.%d"), Slot)));
    return true;
}
#endif

// Opt-in: run inside the demo level's PIE/game, never as a headless collision test.
#include "Misc/AutomationTest.h"
#include "CrackInspectionNavigation.h"
#include "CrackDronePawn.h"
#include "Camera/CameraComponent.h"
#include "Components/InputComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Controller.h"
#include "GameFramework/FloatingPawnMovement.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrackInspectionLiveSceneTest, "CrackTwin.Navigation.LiveScene",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::ProductFilter)
bool FCrackInspectionLiveSceneTest::RunTest(const FString& Parameters)
{
    ACrackDronePawn* Pawn = nullptr;
    for (const FWorldContext& Context : GEngine->GetWorldContexts())
    {
        if (Context.WorldType != EWorldType::PIE && Context.WorldType != EWorldType::Game) continue;
        for (TActorIterator<ACrackDronePawn> It(Context.World()); It; ++It) { Pawn = *It; break; }
        if (Pawn) break;
    }
    if (!Pawn || !Pawn->GetController())
    {
        AddError(TEXT("Navigation live scene test requires the demo level running in a rendered game."));
        return false;
    }
    UInputComponent* Input = Pawn->FindComponentByClass<UInputComponent>();
    if (!TestNotNull(TEXT("possessed drone input exists"), Input)) return false;
    const FTransform Original = Pawn->GetActorTransform();
    const FRotator OriginalControl = Pawn->GetController()->GetControlRotation();
    const FKey Keys[] = {EKeys::One,EKeys::Two,EKeys::Three,EKeys::Four,EKeys::Five,EKeys::Six,EKeys::Seven,EKeys::Eight,EKeys::Nine};
    const FKey Pad[] = {EKeys::NumPadOne,EKeys::NumPadTwo,EKeys::NumPadThree,EKeys::NumPadFour,EKeys::NumPadFive,EKeys::NumPadSix,EKeys::NumPadSeven,EKeys::NumPadEight,EKeys::NumPadNine};
    auto Press = [Input](const FKey& Key)
    {
        for (const FInputKeyBinding& Binding : Input->KeyBindings)
            if (Binding.Chord.Key == Key && Binding.KeyEvent == IE_Pressed) { Binding.KeyDelegate.Execute(Key); return true; }
        return false;
    };
    FTransform FirstView;
    for (int32 Slot = 1; Slot <= 9; ++Slot)
    {
        AActor* Target = nullptr;
        int32 Count = 0;
        for (TActorIterator<AActor> It(Pawn->GetWorld()); It; ++It)
            if (It->ActorHasTag(CrackInspection::SlotTag(Slot))) { Target = *It; ++Count; }
        TestEqual(TEXT("one decal per numbered shortcut"), Count, 1);
        if (!Target) continue;
        FTransform Expected;
        // The lower #2 facade is behind the podium; its nearest clear approach is 7.5m.
        CrackInspection::BuildView(Target->GetActorTransform(), Expected, Slot == 2 ? 750.0 : 500.0);
        if (Slot == 1) FirstView = Expected;
        Pawn->Movement->Velocity = FVector(100, 20, 50);
        TestTrue(TEXT("number key callback exists"), Press(Keys[Slot-1]));
        TestTrue(TEXT("number key reaches its real scene destination"), Pawn->Camera->GetComponentLocation().Equals(Expected.GetLocation(), 0.1));
        TestTrue(TEXT("camera faces crack center"), Pawn->GetController()->GetControlRotation().Equals(Expected.Rotator(), 0.1));
        TestTrue(TEXT("jump clears residual velocity"), Pawn->Movement->Velocity.IsNearlyZero());
        Pawn->Movement->Velocity = FVector(30, 0, 0);
        TestTrue(TEXT("numpad callback exists"), Press(Pad[Slot-1]));
        TestTrue(TEXT("repeated jump still clears velocity"), Pawn->Movement->Velocity.IsNearlyZero());
    }

    const FVector Before = Pawn->GetActorLocation();
    TestFalse(TEXT("locked navigation rejects movement"), CrackInspection::TryJump(*Pawn, 1, true));
    TestTrue(TEXT("locked pose preserved"), Pawn->GetActorLocation().Equals(Before));
    AActor* Blocker = Pawn->GetWorld()->SpawnActor<AActor>();
    UBoxComponent* Box = NewObject<UBoxComponent>(Blocker);
    Blocker->SetRootComponent(Box);
    Box->SetBoxExtent(FVector(1000));
    Box->SetCollisionProfileName(TEXT("BlockAll"));
    Box->RegisterComponent();
    Blocker->SetActorLocation(FirstView.GetLocation());
    TestFalse(TEXT("blocked destination cannot teleport drone into geometry"), CrackInspection::TryJump(*Pawn, 1, false));
    Blocker->Tags.Add(CrackInspection::SlotTag(1));
    TestFalse(TEXT("duplicate numbered destinations are rejected"), CrackInspection::TryJump(*Pawn, 1, false));
    Blocker->Destroy();
    Pawn->HandlePixelStreamingInput(TEXT("{\"type\":\"capture\",\"id\":\"navigation-input-lock\"}"));
    Press(Keys[0]);
    TestTrue(TEXT("capture lock also blocks the actual numeric key callback"), Pawn->GetActorLocation().Equals(Before));
    Pawn->SetActorTransform(Original);
    Pawn->GetController()->SetControlRotation(OriginalControl);
    return true;
}
#endif
