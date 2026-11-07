// Inspection drone for the crack digital twin demo.
// Controls (contract PDF 2-7): W/S forward/back, A/D right/left, E up, Q down, mouse look.
// Handles the Pixel Streaming `capture` message and replies with `capture_result` (PDF 1-4 ①, 2-6).

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "CrackCaptureResult.h"
#include "CrackDronePawn.generated.h"

class UCameraComponent;
class UFloatingPawnMovement;
class UInputAction;
class UInputMappingContext;
class USphereComponent;
struct FInputActionValue;
class IHttpRequest;

DECLARE_LOG_CATEGORY_EXTERN(LogCrackDrone, Log, All);

UCLASS()
class CRACKTWIN_API ACrackDronePawn : public APawn
{
	GENERATED_BODY()

public:
	ACrackDronePawn();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Drone")
	TObjectPtr<USphereComponent> Collision;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Drone")
	TObjectPtr<UCameraComponent> Camera;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Drone")
	TObjectPtr<UFloatingPawnMovement> Movement;

	/** Structure id reported in capture_result (demo structure = 1). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inspection")
	int32 StructureId = 1;

	/** Camera-forward trace length. Contract: 100 m. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inspection", meta = (ClampMin = "100", Units = "cm"))
	float TraceLengthCm = 10000.f;

	/** Movement input is ignored for this long after a capture so the web frame matches the reported pose. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inspection", meta = (ClampMin = "0", Units = "s"))
	float CaptureInputLockSeconds = 0.5f;

	/** Mouse look multiplier. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Input", meta = (ClampMin = "0.05"))
	float LookSensitivity = 0.5f;

	/** Runs the camera trace and returns the capture_result JSON for RequestId. Does not send it. */
	UFUNCTION(BlueprintCallable, Category = "Inspection")
	FString BuildCaptureResultJson(const FString& RequestId);

	/** Bound to Pixel Streaming 2 Input's OnInputEvent (UIInteraction descriptors from the web). */
	UFUNCTION()
	void HandlePixelStreamingInput(const FString& Descriptor);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

private:
	FCrackCaptureResult MakeCaptureResult(const FString& RequestId) const;
	void ProcessCaptureRequest(const FString& RequestId, bool bSendToWeb, int32 InspectionId = 0);
	void RequestUploadScreenshot(const FString& RequestId, const FString& Meta, const FString& Url);
	void UploadScreenshot(const FString& RequestId, const FString& Meta, const FString& Url, int32 Width, int32 Height, const TArray<FColor>& Pixels);
	void FinishUpload(const FString& Message);
	void ReleaseScreenshot();
	bool bCaptureBusy = false;
	bool bAwaitingScreenshot = false;
	FString OwnedScreenshotName;
	FDelegateHandle ScreenshotHandle;
	FTimerHandle ScreenshotTimeout;
	TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> UploadRequest;

	void BindPixelStreaming();
	void SendPixelStreamingResponse(const FString& Descriptor);

	void CreateInputObjects();
	bool IsInputLocked() const;
	FRotator GetYawRotation() const;

	void OnMoveForward(const FInputActionValue& Value);
	void OnMoveRight(const FInputActionValue& Value);
	void OnLift(const FInputActionValue& Value);
	void OnLook(const FInputActionValue& Value);
	void OnDebugCapture(const FInputActionValue& Value);

	/** Instance of /Script/PixelStreaming2.PixelStreaming2Input, created via reflection
	 *  (its header lives in the plugin's Internal folder, which project modules cannot include). */
	UPROPERTY(Transient)
	TObjectPtr<UActorComponent> PixelStreamingInput;

	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> MappingContext;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> MoveForwardAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> MoveRightAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> LiftAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> LookAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> DebugCaptureAction;

	double InputLockedUntilSeconds = 0.0;
	int32 DebugCaptureCount = 0;
};
