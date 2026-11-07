// Opt-in rendered game test. Requires Tools/mock_detect_server.py and -CrackApiUrl.
// This observes the PS2 send path in-process; browser receipt is a separate check.
#include "Misc/AutomationTest.h"
#include "Misc/OutputDevice.h"
#include "Misc/OutputDeviceRedirector.h"
#include "CrackDronePawn.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"
#include "Engine/GameViewportClient.h"
#include "UnrealClient.h"
#include "CoreGlobals.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace CrackUploadTest
{
class FResponses : public FOutputDevice
{
public:
	TArray<FString> Values;
	bool bUploading = false;
	FResponses() { GLog->AddOutputDevice(this); }
	~FResponses() { GLog->RemoveOutputDevice(this); }
	// Receive synchronously at the caller, rather than on UE's logging thread.
	// Serialize ignores all non-game threads, so only the game thread touches Values.
	virtual bool CanBeUsedOnAnyThread() const override { return true; }
	virtual bool CanBeUsedOnMultipleThreads() const override { return true; }
	void Serialize(const TCHAR* Text, ELogVerbosity::Type, const FName& Category) override
	{
		if (Category != FName(TEXT("LogCrackDrone")) || !IsInGameThread()) return;
		const FString Line(Text);
		if (Line.StartsWith(TEXT("inspection_response: "))) Values.Add(Line.Mid(21));
		if (Line.StartsWith(TEXT("Uploading capture "))) bUploading = true;
	}
};

class FRun : public IAutomationLatentCommand
{
	FAutomationTestBase* Test;
	TUniquePtr<FResponses> Responses = MakeUnique<FResponses>();
	TWeakObjectPtr<ACrackDronePawn> Pawn;
	double Deadline = FPlatformTime::Seconds() + 30;
	double Next = 0;
	int32 Step = 0;
	bool bHttpBusyChecked = false;

	void Send(const FString& Id, const FString& Inspection = TEXT("3"))
	{
		Pawn->HandlePixelStreamingInput(TEXT("{\"type\":\"capture\",\"id\":\"") + Id + TEXT("\",\"inspectionId\":") + Inspection + TEXT("}"));
	}
	void Pose(float Pitch)
	{
		Pawn->SetActorLocation(FVector(-1500, 0, 400));
		Pawn->GetController()->SetControlRotation(FRotator(Pitch, 0, 0));
	}
	TSharedPtr<FJsonObject> Json(int32 Index)
	{
		TSharedPtr<FJsonObject> Object;
		if (Responses->Values.IsValidIndex(Index)) FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Responses->Values[Index]), Object);
		Test->TestTrue(TEXT("valid response JSON"), Object.IsValid());
		return Object;
	}
	void Error(int32 Index, const FString& Id, int32 Status)
	{
		const auto Object = Json(Index);
		if (!Object) return;
		Test->TestEqual(TEXT("error type"), Object->GetStringField(TEXT("type")), FString(TEXT("detect_error")));
		Test->TestEqual(TEXT("error id"), Object->GetStringField(TEXT("id")), Id);
		Test->TestEqual(TEXT("error status"), Object->GetIntegerField(TEXT("status")), Status);
		if (Status == 409) Test->TestEqual(TEXT("busy contract message"), Object->GetStringField(TEXT("message")), FString(TEXT("이전 점검을 처리 중입니다")));
	}
	void Clear() { Responses->Values.Reset(); Responses->bUploading = false; }
public:
	explicit FRun(FAutomationTestBase* InTest) : Test(InTest) {}
	virtual bool Update() override
	{
		const double Now = FPlatformTime::Seconds();
		if (Now > Deadline) { Test->AddError(FString::Printf(TEXT("Upload integration timeout at step %d, responses %d"), Step, Responses->Values.Num())); return true; }
		if (Now < Next) return false;
		if (Step == 0)
		{
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.WorldType != EWorldType::Game) continue;
				for (TActorIterator<ACrackDronePawn> It(Context.World()); It; ++It) { Pawn = *It; break; }
			}
			if (!Pawn.IsValid() || !Pawn->GetController()) return false;
			Pose(0); Next = Now + 0.15; Step = 1;
		}
		else if (Step == 1)
		{
			Send(TEXT("upload-wall")); Send(TEXT("busy-screenshot"));
			Test->TestEqual(TEXT("one capture and one busy error"), Responses->Values.Num(), 2);
			Error(1, TEXT("busy-screenshot"), 409); Step = 2;
		}
		else if (Step == 2)
		{
			if (Responses->bUploading && !bHttpBusyChecked)
			{
				Send(TEXT("busy-http")); Error(2, TEXT("busy-http"), 409); bHttpBusyChecked = true;
			}
			if (Responses->Values.Num() < 4) return false;
			Test->TestTrue(TEXT("HTTP busy exercised"), bHttpBusyChecked);
			Test->TestEqual(TEXT("200 body forwarded byte for byte"), Responses->Values.Last(), FString(TEXT(" {\"type\": \"detect_result\", \"inspectionId\": 3, \"captureId\": \"upload-wall\", \"cracks\": []}\n")));
			Clear(); Pose(89.9f); Next = Now + 0.15; Step = 3;
		}
		else if (Step == 3) { Send(TEXT("upload-sky")); Next = Now + 0.3; Step = 4; }
		else if (Step == 4)
		{
			Test->TestEqual(TEXT("sky only capture_result"), Responses->Values.Num(), 1);
			const auto Object = Json(0);
			if (Object) { Test->TestFalse(TEXT("sky miss"), Object->GetBoolField(TEXT("hitOk"))); Test->TestEqual(TEXT("sky distance"), Object->GetNumberField(TEXT("distanceCm")), 0.0); }
			Clear(); Pose(0); Next = Now + 0.15; Step = 5;
		}
		else if (Step == 5)
		{
			for (const FString& Invalid : { FString(TEXT("null")), FString(TEXT("3.5")), FString(TEXT("\"3\"")) })
			{
				Send(TEXT("invalid"), Invalid);
				Test->TestEqual(TEXT("invalid gives capture then error"), Responses->Values.Num(), 2);
				Error(1, TEXT("invalid"), 0); Clear();
			}
			// An existing listener must not have its screenshot stolen or be removed.
			const FDelegateHandle Other = UGameViewportClient::OnScreenshotCaptured().AddLambda([](int32, int32, const TArray<FColor>&) {});
			Send(TEXT("other-screenshot")); Error(1, TEXT("other-screenshot"), 0);
			Test->TestTrue(TEXT("other listener preserved"), UGameViewportClient::OnScreenshotCaptured().IsBound());
			UGameViewportClient::OnScreenshotCaptured().Remove(Other);
			Clear(); Send(TEXT("upload-error")); Step = 6;
		}
		else if (Step == 6)
		{
			if (Responses->Values.Num() < 2) return false;
			Error(1, TEXT("upload-error"), 422);
			const auto Object = Json(1);
			if (Object) Test->TestTrue(TEXT("server body intact in error"), Object->GetStringField(TEXT("message")).Contains(TEXT("검증")));
			Clear(); Send(TEXT("upload-truncated")); Step = 10;
		}
		else if (Step == 7)
		{
			if (Responses->Values.Num() < 2) return false;
			const auto Object = Json(1);
			if (Object) Test->TestEqual(TEXT("stop response id preserved"), Object->GetStringField(TEXT("captureId")), FString(TEXT("upload-stop")));
			Clear(); Next = Now + 0.4; Step = 8;
		}
		else if (Step == 8) { Send(TEXT("upload-offline")); Step = 9; }
		else if (Step == 9)
		{
			if (Responses->Values.Num() < 2) return false;
			Error(1, TEXT("upload-offline"), 0);
			// Destroy before the screenshot frame: delegates and global UI state must recover.
			const bool BeforeMessages = GAreScreenMessagesEnabled;
			const bool BeforeRestore = GScreenMessagesRestoreState;
			GAreScreenMessagesEnabled = true;
			ACrackDronePawn* Temporary = Pawn->GetWorld()->SpawnActor<ACrackDronePawn>(FVector(-1500, 0, 400), FRotator::ZeroRotator);
			Temporary->HandlePixelStreamingInput(TEXT("{\"type\":\"capture\",\"id\":\"destroy-pending\",\"inspectionId\":3}"));
			Test->TestTrue(TEXT("temporary screenshot pending"), FScreenshotRequest::IsScreenshotRequested());
			Temporary->Destroy();
			Test->TestFalse(TEXT("pending screenshot cancelled on EndPlay"), FScreenshotRequest::IsScreenshotRequested());
			Test->TestFalse(TEXT("delegate removed on EndPlay"), UGameViewportClient::OnScreenshotCaptured().IsBound());
			Test->TestTrue(TEXT("screen messages restored after cancellation"), GAreScreenMessagesEnabled);
			GAreScreenMessagesEnabled = BeforeMessages;
			GScreenMessagesRestoreState = BeforeRestore;
			return true;
		}
		else if (Step == 10)
		{
			if (Responses->Values.Num() < 2) return false;
			Error(1, TEXT("upload-truncated"), 0);
			Clear(); Send(TEXT("upload-stop")); Step = 7;
		}
		return false;
	}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrackUploadIntegrationTest, "CrackTwin.UploadIntegration.RenderedGame",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::ProductFilter)
bool FCrackUploadIntegrationTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(CrackUploadTest::FRun(this));
	return true;
}
#endif
