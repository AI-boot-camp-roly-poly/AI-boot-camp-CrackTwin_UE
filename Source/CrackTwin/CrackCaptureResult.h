// Pure (engine-runtime-free) model for the web <-> UE5 inspection messages.
// Contract: 디지털트윈팀_개발.md / 시스템팀_개발.md, section 1-4 ① (Pixel Streaming messages).
// Field names must match the contract exactly; numbers are unquoted and rounded to 1 decimal.

#pragma once

#include "CoreMinimal.h"

/** Values reported to the web for one `capture` request. All positions are UE world space (cm). */
struct FCrackCaptureResult
{
	/** Request id sent by the web; echoed back unchanged. */
	FString Id;
	int32 StructureId = 1;

	/** Camera component world location (reported under "drone" per the contract). */
	FVector CameraLocation = FVector::ZeroVector;
	/** Camera world yaw / pitch in degrees, normalized to (-180, 180]. */
	double Yaw = 0.0;
	double Pitch = 0.0;

	/** True when the camera-forward line trace hit something. When false, hit and distanceCm are sent as 0. */
	bool bHitOk = false;
	FVector HitLocation = FVector::ZeroVector;
	double DistanceCm = 0.0;

	/** Horizontal field of view in degrees (camera is locked to Maintain X-Axis FOV). */
	double FovDeg = 90.0;
};

namespace CrackCapture
{
	/** Builds the `capture_result` JSON string. */
	CRACKTWIN_API FString ToJson(const FCrackCaptureResult& Result);

	/**
	 * Parses a web -> UE descriptor.
	 * @return true only when the descriptor is a JSON object with "type": "capture".
	 * OutId receives "id" as a string (numeric ids are converted); empty if missing.
	 * Optional OutInspectionId receives a positive int32, or 0 when absent/invalid.
	 */
	CRACKTWIN_API bool ParseCaptureRequest(const FString& Descriptor, FString& OutId, int32* OutInspectionId = nullptr);

	/** Empty for invalid base URL / inspection. No default server address. */
	CRACKTWIN_API FString BuildDetectUrl(const FString& ApiUrl, int32 InspectionId);
	CRACKTWIN_API FString DetectError(const FString& Id, int32 Status, const FString& Message);
	/** Fixed transport filename; request ids are metadata, never HTTP headers. */
	CRACKTWIN_API TArray<uint8> BuildMultipart(const FString& Meta, const TArray64<uint8>& Jpg, const FString& Boundary);

	/** Formats a number with exactly one decimal (half away from zero); never emits "-0.0". */
	CRACKTWIN_API FString FormatNumber1(double Value);

	/** Escapes a string for use inside a JSON string literal (without surrounding quotes). */
	CRACKTWIN_API FString EscapeForJson(const FString& In);
}
