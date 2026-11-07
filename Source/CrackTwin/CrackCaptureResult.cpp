#include "CrackCaptureResult.h"

#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace CrackCapture
{
	FString FormatNumber1(double Value)
	{
		if (!FMath::IsFinite(Value))
		{
			Value = 0.0;
		}
		double Rounded = FMath::RoundHalfFromZero(Value * 10.0) / 10.0;
		if (Rounded == 0.0)
		{
			Rounded = 0.0; // collapse -0.0
		}
		return FString::Printf(TEXT("%.1f"), Rounded);
	}

	FString EscapeForJson(const FString& In)
	{
		FString Out;
		Out.Reserve(In.Len() + 8);
		for (const TCHAR C : In)
		{
			switch (C)
			{
			case TEXT('"'):  Out += TEXT("\\\""); break;
			case TEXT('\\'): Out += TEXT("\\\\"); break;
			case TEXT('\b'): Out += TEXT("\\b"); break;
			case TEXT('\f'): Out += TEXT("\\f"); break;
			case TEXT('\n'): Out += TEXT("\\n"); break;
			case TEXT('\r'): Out += TEXT("\\r"); break;
			case TEXT('\t'): Out += TEXT("\\t"); break;
			default:
				if (C < 0x20)
				{
					Out += FString::Printf(TEXT("\\u%04x"), static_cast<uint32>(C));
				}
				else
				{
					Out.AppendChar(C);
				}
			}
		}
		return Out;
	}

	FString ToJson(const FCrackCaptureResult& R)
	{
		const FVector Hit = R.bHitOk ? R.HitLocation : FVector::ZeroVector;
		const double Distance = R.bHitOk ? R.DistanceCm : 0.0;

		return FString::Printf(
			TEXT("{\"type\":\"capture_result\",\"id\":\"%s\",\"structureId\":%d,")
			TEXT("\"drone\":{\"x\":%s,\"y\":%s,\"z\":%s,\"yaw\":%s,\"pitch\":%s},")
			TEXT("\"hit\":{\"x\":%s,\"y\":%s,\"z\":%s},\"hitOk\":%s,")
			TEXT("\"distanceCm\":%s,\"fovDeg\":%s}"),
			*EscapeForJson(R.Id), R.StructureId,
			*FormatNumber1(R.CameraLocation.X), *FormatNumber1(R.CameraLocation.Y), *FormatNumber1(R.CameraLocation.Z),
			*FormatNumber1(R.Yaw), *FormatNumber1(R.Pitch),
			*FormatNumber1(Hit.X), *FormatNumber1(Hit.Y), *FormatNumber1(Hit.Z),
			R.bHitOk ? TEXT("true") : TEXT("false"),
			*FormatNumber1(Distance), *FormatNumber1(R.FovDeg));
	}

	FString BuildDetectUrl(const FString& ApiUrl, int32 InspectionId)
	{
		FString Base = ApiUrl.TrimStartAndEnd();
		const int32 SchemeLength = Base.StartsWith(TEXT("https://")) ? 8 : Base.StartsWith(TEXT("http://")) ? 7 : 0;
		if (!SchemeLength || InspectionId <= 0) return {};
		for (const TCHAR C : Base)
		{
			if (C <= 0x20 || C == TEXT('"') || C == TEXT('\\') || C == TEXT('?') || C == TEXT('#') || C == TEXT('@')) return {};
		}
		while (Base.EndsWith(TEXT("/"))) Base.LeftChopInline(1);
		if (Base.Len() <= SchemeLength || Base[SchemeLength] == TEXT('/')) return {};
		return FString::Printf(TEXT("%s/inspections/%d/detect"), *Base, InspectionId);
	}

	FString DetectError(const FString& Id, int32 Status, const FString& Message)
	{
		return FString::Printf(TEXT("{\"type\":\"detect_error\",\"id\":\"%s\",\"status\":%d,\"message\":\"%s\"}"), *EscapeForJson(Id), Status, *EscapeForJson(Message));
	}

	TArray<uint8> BuildMultipart(const FString& Meta, const TArray64<uint8>& Jpg, const FString& Boundary)
	{
		TArray<uint8> Body;
		if (Jpg.Num() > MAX_int32 - 65536 || Boundary.IsEmpty() || Boundary.Contains(TEXT("\r")) || Boundary.Contains(TEXT("\n"))) return Body;
		auto AppendText = [&Body](const FString& Text)
		{
			FTCHARToUTF8 Utf8(*Text);
			Body.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
		};
		AppendText(FString::Printf(TEXT("--%s\r\nContent-Disposition: form-data; name=\"meta\"\r\n\r\n%s\r\n"), *Boundary, *Meta));
		AppendText(FString::Printf(TEXT("--%s\r\nContent-Disposition: form-data; name=\"image\"; filename=\"capture.jpg\"\r\nContent-Type: image/jpeg\r\n\r\n"), *Boundary));
		Body.Append(Jpg.GetData(), static_cast<int32>(Jpg.Num()));
		AppendText(FString::Printf(TEXT("\r\n--%s--\r\n"), *Boundary));
		return Body;
	}

	bool ParseCaptureRequest(const FString& Descriptor, FString& OutId, int32* OutInspectionId)
	{
		OutId.Reset();
		if (OutInspectionId) *OutInspectionId = 0;

		TSharedPtr<FJsonObject> Object;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Descriptor);
		if (!FJsonSerializer::Deserialize(Reader, Object) || !Object.IsValid())
		{
			return false;
		}

		FString Type;
		if (!Object->TryGetStringField(TEXT("type"), Type) || Type != TEXT("capture"))
		{
			return false;
		}

		if (!Object->TryGetStringField(TEXT("id"), OutId))
		{
			double NumericId = 0.0;
			if (Object->TryGetNumberField(TEXT("id"), NumericId))
			{
				OutId = FString::Printf(TEXT("%lld"), static_cast<int64>(NumericId));
			}
		}
		if (OutInspectionId)
		{
			const TSharedPtr<FJsonValue> Value = Object->TryGetField(TEXT("inspectionId"));
			double Number = 0;
			if (Value.IsValid() && Value->Type == EJson::Number && Value->TryGetNumber(Number)
				&& FMath::IsFinite(Number) && Number >= 1 && Number <= MAX_int32 && FMath::FloorToDouble(Number) == Number)
			{
				*OutInspectionId = static_cast<int32>(Number);
			}
		}
		return true;
	}
}
