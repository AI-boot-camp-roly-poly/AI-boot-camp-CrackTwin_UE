#include "CrackDronePawn.h"
#include "CrackInspectionNavigation.h"

#include "Async/Async.h"
#include "Camera/CameraComponent.h"
#include "Components/SphereComponent.h"
#include "CollisionQueryParams.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/FloatingPawnMovement.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"
#include "Engine/GameViewportClient.h"
#include "UnrealClient.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Modules/ModuleManager.h"
#include "TimerManager.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY(LogCrackDrone);

namespace
{
	const TCHAR* const PixelStreamingInputClassPath = TEXT("/Script/PixelStreaming2.PixelStreaming2Input");
	const FName PixelStreamingInputEventName(TEXT("OnInputEvent"));
	const FName PixelStreamingSendResponseName(TEXT("SendPixelStreaming2Response"));
}

ACrackDronePawn::ACrackDronePawn()
{
	PrimaryActorTick.bCanEverTick = false;

	Collision = CreateDefaultSubobject<USphereComponent>(TEXT("Collision"));
	Collision->InitSphereRadius(35.f);
	Collision->SetCollisionProfileName(UCollisionProfile::Pawn_ProfileName);
	Collision->SetCanEverAffectNavigation(false);
	RootComponent = Collision;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(Collision);
	Camera->bUsePawnControlRotation = true;
	Camera->SetFieldOfView(90.f);
	// fovDeg in capture_result is the horizontal FOV; force that interpretation regardless of viewport aspect.
	Camera->bOverrideAspectRatioAxisConstraint = true;
	Camera->AspectRatioAxisConstraint = EAspectRatioAxisConstraint::AspectRatio_MaintainXFOV;

	Movement = CreateDefaultSubobject<UFloatingPawnMovement>(TEXT("Movement"));
	Movement->UpdatedComponent = Collision;
	Movement->MaxSpeed = 400.f;      // 4 m/s (PDF 2-7: 3~5 m/s)
	Movement->Acceleration = 1000.f;
	Movement->Deceleration = 2000.f;
	Movement->TurningBoost = 8.f;

	bUseControllerRotationYaw = true;
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;

	AutoPossessPlayer = EAutoReceiveInput::Disabled;
}

void ACrackDronePawn::BeginPlay()
{
	Super::BeginPlay();
	BindPixelStreaming();
}

// ---------------------------------------------------------------------------
// Pixel Streaming 2 (reflection bridge)
// ---------------------------------------------------------------------------

void ACrackDronePawn::BindPixelStreaming()
{
	UClass* InputClass = FindObject<UClass>(nullptr, PixelStreamingInputClassPath);
	if (!InputClass)
	{
		UE_LOG(LogCrackDrone, Warning, TEXT("PixelStreaming2 plugin not loaded (%s missing); capture messages from the web are disabled."), PixelStreamingInputClassPath);
		return;
	}

	FMulticastDelegateProperty* EventProperty = FindFProperty<FMulticastDelegateProperty>(InputClass, PixelStreamingInputEventName);
	if (!EventProperty || !InputClass->FindFunctionByName(PixelStreamingSendResponseName))
	{
		UE_LOG(LogCrackDrone, Error, TEXT("PixelStreaming2Input API changed (OnInputEvent / SendPixelStreaming2Response not found). Update CrackDronePawn."));
		return;
	}

	UActorComponent* Component = NewObject<UActorComponent>(this, InputClass, TEXT("PixelStreaming2Input"));
	AddInstanceComponent(Component);
	Component->RegisterComponent();

	FScriptDelegate Delegate;
	Delegate.BindUFunction(this, GET_FUNCTION_NAME_CHECKED(ACrackDronePawn, HandlePixelStreamingInput));
	EventProperty->AddDelegate(MoveTemp(Delegate), Component);

	PixelStreamingInput = Component;
	UE_LOG(LogCrackDrone, Log, TEXT("Pixel Streaming 2 input bound; waiting for {\"type\":\"capture\"} messages."));
}

void ACrackDronePawn::SendPixelStreamingResponse(const FString& Descriptor)
{
	UE_LOG(LogCrackDrone, Log, TEXT("inspection_response: %s"), *Descriptor);
	if (!PixelStreamingInput)
	{
		return;
	}

	UFunction* SendFunction = PixelStreamingInput->FindFunction(PixelStreamingSendResponseName);
	if (!SendFunction)
	{
		return;
	}

	struct FSendParams
	{
		FString Descriptor;
	};
	FSendParams Params{ Descriptor };
	check(SendFunction->ParmsSize == sizeof(FSendParams));
	PixelStreamingInput->ProcessEvent(SendFunction, &Params);
}

void ACrackDronePawn::HandlePixelStreamingInput(const FString& Descriptor)
{
	if (!IsInGameThread())
	{
		TWeakObjectPtr<ACrackDronePawn> WeakThis(this);
		AsyncTask(ENamedThreads::GameThread, [WeakThis, Descriptor]()
		{
			if (ACrackDronePawn* Pawn = WeakThis.Get())
			{
				Pawn->HandlePixelStreamingInput(Descriptor);
			}
		});
		return;
	}

	FString RequestId;
	int32 InspectionId = 0;
	if (!CrackCapture::ParseCaptureRequest(Descriptor, RequestId, &InspectionId))
	{
		UE_LOG(LogCrackDrone, Verbose, TEXT("Ignoring UI interaction: %s"), *Descriptor);
		return;
	}

	ProcessCaptureRequest(RequestId, /*bSendToWeb*/ true, InspectionId);
}

// ---------------------------------------------------------------------------
// Capture
// ---------------------------------------------------------------------------

FCrackCaptureResult ACrackDronePawn::MakeCaptureResult(const FString& RequestId) const
{
	FCrackCaptureResult Result;
	Result.Id = RequestId;
	Result.StructureId = StructureId;

	const FVector Start = Camera->GetComponentLocation();
	const FRotator Rotation = Camera->GetComponentRotation().GetNormalized();
	Result.CameraLocation = Start;
	Result.Yaw = Rotation.Yaw;
	Result.Pitch = Rotation.Pitch;
	Result.FovDeg = Camera->FieldOfView;

	if (UWorld* World = GetWorld())
	{
		const FVector End = Start + Rotation.Vector() * TraceLengthCm;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(CrackDroneCapture), /*bTraceComplex*/ true, this);
		FHitResult Hit;
		if (World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params) && Hit.bBlockingHit)
		{
			Result.bHitOk = true;
			Result.HitLocation = Hit.Location;
			Result.DistanceCm = Hit.Distance;
		}
	}
	return Result;
}

FString ACrackDronePawn::BuildCaptureResultJson(const FString& RequestId)
{
	return CrackCapture::ToJson(MakeCaptureResult(RequestId));
}

void ACrackDronePawn::ProcessCaptureRequest(const FString& RequestId, bool bSendToWeb, int32 InspectionId)
{
	if (bCaptureBusy)
	{
		if (bSendToWeb) SendPixelStreamingResponse(CrackCapture::DetectError(RequestId, 409, TEXT("이전 점검을 처리 중입니다")));
		return;
	}
	// Keep the reported camera pose until the screenshot callback and at least 0.5s.
	Movement->StopMovementImmediately();
	ConsumeMovementInputVector();
	if (const UWorld* World = GetWorld())
	{
		InputLockedUntilSeconds = World->GetTimeSeconds() + CaptureInputLockSeconds;
	}

	const FCrackCaptureResult Result = MakeCaptureResult(RequestId);
	const FString Json = CrackCapture::ToJson(Result);
	UE_LOG(LogCrackDrone, Log, TEXT("capture_result%s: %s"), bSendToWeb ? TEXT("") : TEXT(" (local, not sent)"), *Json);

	if (bSendToWeb)
	{
		SendPixelStreamingResponse(Json);
		if (!Result.bHitOk) return;
		FString ApiUrl;
		FParse::Value(FCommandLine::Get(), TEXT("CrackApiUrl="), ApiUrl);
		const FString Url = CrackCapture::BuildDetectUrl(ApiUrl, InspectionId);
		if (Url.IsEmpty())
		{
			const FString Error = InspectionId <= 0 ? TEXT("inspectionId는 양의 정수여야 합니다") : TEXT("-CrackApiUrl 실행 인자가 없거나 올바른 HTTP(S) 주소가 아닙니다");
			SendPixelStreamingResponse(CrackCapture::DetectError(RequestId, 0, Error));
			return;
		}
		RequestUploadScreenshot(RequestId, Json, Url);
	}
}

void ACrackDronePawn::ReleaseScreenshot()
{
	UGameViewportClient::OnScreenshotCaptured().Remove(ScreenshotHandle);
	ScreenshotHandle.Reset();
	if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(ScreenshotTimeout);
	if (!OwnedScreenshotName.IsEmpty() && FScreenshotRequest::GetFilename() == OwnedScreenshotName)
	{
		FScreenshotRequest::Reset();
		// Reset() alone does not undo RequestScreenshot(false)'s global UI change.
		GAreScreenMessagesEnabled = GScreenMessagesRestoreState;
	}
	OwnedScreenshotName.Reset();
	bAwaitingScreenshot = false;
}

void ACrackDronePawn::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ReleaseScreenshot();
	if (UploadRequest)
	{
		UploadRequest->OnProcessRequestComplete().Unbind();
		UploadRequest->CancelRequest();
		UploadRequest.Reset();
	}
	bCaptureBusy = false;
	Super::EndPlay(EndPlayReason);
}

void ACrackDronePawn::FinishUpload(const FString& Message)
{
	check(IsInGameThread());
	ReleaseScreenshot();
	UploadRequest.Reset();
	bCaptureBusy = false;
	SendPixelStreamingResponse(Message);
}

void ACrackDronePawn::RequestUploadScreenshot(const FString& RequestId, const FString& Meta, const FString& Url)
{
	const IConsoleVariable* ScreenshotDelegate = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenshotDelegate"));
	if (!GetWorld() || !GetWorld()->GetGameViewport() || !GetWorld()->GetGameViewport()->Viewport
		|| FScreenshotRequest::IsScreenshotRequested() || GIsHighResScreenshot || GIsDumpingMovie
		|| UGameViewportClient::OnScreenshotCaptured().IsBound() || !ScreenshotDelegate || ScreenshotDelegate->GetInt() == 0)
	{
		SendPixelStreamingResponse(CrackCapture::DetectError(RequestId, 0, TEXT("스크린샷을 요청할 수 없습니다: 뷰포트 또는 다른 촬영 요청을 확인하세요")));
		return;
	}
	bCaptureBusy = true;
	bAwaitingScreenshot = true;
	ScreenshotHandle = UGameViewportClient::OnScreenshotCaptured().AddWeakLambda(this,
		[this, RequestId, Meta, Url](int32 Width, int32 Height, const TArray<FColor>& Pixels)
		{
			// The delegate is global. Reject a screenshot substituted by another request.
			if (FScreenshotRequest::GetFilename() != OwnedScreenshotName)
			{
				FinishUpload(CrackCapture::DetectError(RequestId, 0, TEXT("다른 스크린샷 요청으로 촬영이 취소됐습니다")));
				return;
			}
			// Removing the delegate destroys its lambda captures. Copy before removal.
			ACrackDronePawn* Pawn = this;
			const FString CapturedId = RequestId;
			const FString CapturedMeta = Meta;
			const FString CapturedUrl = Url;
			Pawn->ReleaseScreenshot();
			Pawn->UploadScreenshot(CapturedId, CapturedMeta, CapturedUrl, Width, Height, Pixels);
		});
	GetWorld()->GetTimerManager().SetTimer(ScreenshotTimeout, FTimerDelegate::CreateWeakLambda(this, [this, RequestId]
	{
		FinishUpload(CrackCapture::DetectError(RequestId, 0, TEXT("스크린샷 대기 시간이 초과됐습니다")));
	}), 10.f, false);
	// Unique name is ownership metadata, not a file written by the screenshot delegate.
	FScreenshotRequest::RequestScreenshot(TEXT("CrackTwin-") + FGuid::NewGuid().ToString(), false, false);
	OwnedScreenshotName = FScreenshotRequest::GetFilename();
}

void ACrackDronePawn::UploadScreenshot(const FString& RequestId, const FString& Meta, const FString& Url,
	int32 Width, int32 Height, const TArray<FColor>& Pixels)
{
	IImageWrapperModule& Module = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	TSharedPtr<IImageWrapper> Jpeg = Module.CreateImageWrapper(EImageFormat::JPEG);
	if (Width <= 0 || Height <= 0 || static_cast<int64>(Width) * Height != Pixels.Num() || !Jpeg
		|| !Jpeg->SetRaw(Pixels.GetData(), static_cast<int64>(Pixels.Num()) * sizeof(FColor), Width, Height, ERGBFormat::BGRA, 8))
	{
		FinishUpload(CrackCapture::DetectError(RequestId, 0, TEXT("스크린샷을 JPG로 변환하지 못했습니다")));
		return;
	}
	const TArray64<uint8> Jpg = Jpeg->GetCompressed(95);
	if (Jpg.IsEmpty())
	{
		FinishUpload(CrackCapture::DetectError(RequestId, 0, TEXT("JPG 변환 결과가 비어 있습니다")));
		return;
	}
	const FString Boundary = TEXT("----CrackTwin") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(Url);
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("multipart/form-data; boundary=") + Boundary);
	Request->SetContent(CrackCapture::BuildMultipart(Meta, Jpg, Boundary));
	Request->SetTimeout(60.f);
	Request->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnGameThread);
	Request->OnProcessRequestComplete().BindWeakLambda(this, [this, RequestId](FHttpRequestPtr, FHttpResponsePtr Response, bool bOk)
	{
		check(IsInGameThread());
		const bool bReceived = bOk && Response.IsValid();
		const int32 Status = bReceived ? Response->GetResponseCode() : 0;
		const FString Body = bReceived ? Response->GetContentAsString() : TEXT("서버 연결 또는 응답 수신에 실패했습니다");
		FinishUpload(bOk && Status == 200 ? Body : CrackCapture::DetectError(RequestId, Status, Body));
	});
	UploadRequest = Request;
	UE_LOG(LogCrackDrone, Log, TEXT("Uploading capture %s: %dx%d JPG, %lld bytes"), *RequestId, Width, Height, Jpg.Num());
	if (!Request->ProcessRequest() && UploadRequest.IsValid())
	{
		Request->OnProcessRequestComplete().Unbind();
		FinishUpload(CrackCapture::DetectError(RequestId, 0, TEXT("HTTP 요청을 시작하지 못했습니다")));
	}
}

// ---------------------------------------------------------------------------
// Input (created in code; no input assets needed)
// ---------------------------------------------------------------------------

void ACrackDronePawn::CreateInputObjects()
{
	if (MappingContext)
	{
		return;
	}

	auto MakeAction = [this](const TCHAR* Name, EInputActionValueType Type)
	{
		UInputAction* Action = NewObject<UInputAction>(this, Name);
		Action->ValueType = Type;
		return Action;
	};

	MoveForwardAction = MakeAction(TEXT("IA_MoveForward"), EInputActionValueType::Axis1D);
	MoveRightAction = MakeAction(TEXT("IA_MoveRight"), EInputActionValueType::Axis1D);
	LiftAction = MakeAction(TEXT("IA_Lift"), EInputActionValueType::Axis1D);
	LookAction = MakeAction(TEXT("IA_Look"), EInputActionValueType::Axis2D);
	DebugCaptureAction = MakeAction(TEXT("IA_DebugCapture"), EInputActionValueType::Boolean);

	MappingContext = NewObject<UInputMappingContext>(this, TEXT("IMC_Drone"));

	auto MapNegated = [this](const UInputAction* Action, const FKey& Key)
	{
		FEnhancedActionKeyMapping& Mapping = MappingContext->MapKey(Action, Key);
		Mapping.Modifiers.Add(NewObject<UInputModifierNegate>(MappingContext));
	};

	MappingContext->MapKey(MoveForwardAction, EKeys::W);
	MapNegated(MoveForwardAction, EKeys::S);
	MappingContext->MapKey(MoveRightAction, EKeys::D);
	MapNegated(MoveRightAction, EKeys::A);
	MappingContext->MapKey(LiftAction, EKeys::E);
	MapNegated(LiftAction, EKeys::Q);

	FEnhancedActionKeyMapping& LookMapping = MappingContext->MapKey(LookAction, EKeys::Mouse2D);
	UInputModifierNegate* InvertY = NewObject<UInputModifierNegate>(MappingContext);
	InvertY->bX = false;
	InvertY->bY = true;
	InvertY->bZ = false;
	LookMapping.Modifiers.Add(InvertY);

	// Dev only: logs a capture_result without sending it (test the trace without the web page).
	MappingContext->MapKey(DebugCaptureAction, EKeys::C);
}

void ACrackDronePawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	CreateInputObjects();

	if (const APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
		{
			Subsystem->AddMappingContext(MappingContext, 0);
		}
	}

	UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (!Input)
	{
		UE_LOG(LogCrackDrone, Error, TEXT("Enhanced Input component missing; check DefaultInputComponentClass in DefaultInput.ini."));
		return;
	}

	Input->BindAction(MoveForwardAction, ETriggerEvent::Triggered, this, &ACrackDronePawn::OnMoveForward);
	Input->BindAction(MoveRightAction, ETriggerEvent::Triggered, this, &ACrackDronePawn::OnMoveRight);
	Input->BindAction(LiftAction, ETriggerEvent::Triggered, this, &ACrackDronePawn::OnLift);
	Input->BindAction(LookAction, ETriggerEvent::Triggered, this, &ACrackDronePawn::OnLook);
	Input->BindAction(DebugCaptureAction, ETriggerEvent::Started, this, &ACrackDronePawn::OnDebugCapture);

	// Local and Pixel Streaming keyboard input use the same numbered inspection views.
	const FKey NumberKeys[] = { EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five,
		EKeys::Six, EKeys::Seven, EKeys::Eight, EKeys::Nine };
	const FKey PadKeys[] = { EKeys::NumPadOne, EKeys::NumPadTwo, EKeys::NumPadThree, EKeys::NumPadFour,
		EKeys::NumPadFive, EKeys::NumPadSix, EKeys::NumPadSeven, EKeys::NumPadEight, EKeys::NumPadNine };
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(NumberKeys); ++Index)
	{
		for (const FKey& Key : { NumberKeys[Index], PadKeys[Index] })
		{
			FInputKeyBinding Binding(FInputChord(Key), IE_Pressed);
			Binding.KeyDelegate.GetDelegateForManualSet().BindWeakLambda(this, [this, Slot = Index + 1]
			{
				const bool bMoved = CrackInspection::TryJump(*this, Slot, bCaptureBusy || IsInputLocked());
				UE_LOG(LogCrackDrone, Log, TEXT("Inspection view %d: %s"), Slot, bMoved ? TEXT("moved") : TEXT("blocked or unavailable"));
			});
			Input->KeyBindings.Add(MoveTemp(Binding));
		}
	}
}

bool ACrackDronePawn::IsInputLocked() const
{
	const UWorld* World = GetWorld();
	return bAwaitingScreenshot || (World && World->GetTimeSeconds() < InputLockedUntilSeconds);
}

FRotator ACrackDronePawn::GetYawRotation() const
{
	const FRotator Control = Controller ? Controller->GetControlRotation() : GetActorRotation();
	return FRotator(0.0, Control.Yaw, 0.0);
}

void ACrackDronePawn::OnMoveForward(const FInputActionValue& Value)
{
	if (!IsInputLocked())
	{
		AddMovementInput(GetYawRotation().Vector(), Value.Get<float>());
	}
}

void ACrackDronePawn::OnMoveRight(const FInputActionValue& Value)
{
	if (!IsInputLocked())
	{
		AddMovementInput(FRotationMatrix(GetYawRotation()).GetUnitAxis(EAxis::Y), Value.Get<float>());
	}
}

void ACrackDronePawn::OnLift(const FInputActionValue& Value)
{
	if (!IsInputLocked())
	{
		AddMovementInput(FVector::UpVector, Value.Get<float>());
	}
}

void ACrackDronePawn::OnLook(const FInputActionValue& Value)
{
	if (IsInputLocked())
	{
		return;
	}
	const FVector2D Axis = Value.Get<FVector2D>();
	AddControllerYawInput(Axis.X * LookSensitivity);
	AddControllerPitchInput(Axis.Y * LookSensitivity);
}

void ACrackDronePawn::OnDebugCapture(const FInputActionValue& Value)
{
	ProcessCaptureRequest(FString::Printf(TEXT("local-%04d"), ++DebugCaptureCount), /*bSendToWeb*/ false);
}
