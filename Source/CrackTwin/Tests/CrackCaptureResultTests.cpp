// Edit-mode unit tests for the capture message model (no world / streaming needed).
// Run: Session Frontend > Automation, filter "CrackTwin.Capture".

#include "Misc/AutomationTest.h"
#include "CrackCaptureResult.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrackCaptureFormatNumberTest, "CrackTwin.Capture.FormatNumber1",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCrackCaptureFormatNumberTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("integer gets .0"), CrackCapture::FormatNumber1(820.0), FString(TEXT("820.0")));
	TestEqual(TEXT("rounds half away from zero"), CrackCapture::FormatNumber1(1.25), FString(TEXT("1.3")));
	TestEqual(TEXT("negative half away from zero"), CrackCapture::FormatNumber1(-1.25), FString(TEXT("-1.3")));
	TestEqual(TEXT("rounds down"), CrackCapture::FormatNumber1(430.04), FString(TEXT("430.0")));
	TestEqual(TEXT("no negative zero"), CrackCapture::FormatNumber1(-0.04), FString(TEXT("0.0")));
	TestEqual(TEXT("NaN becomes 0.0"), CrackCapture::FormatNumber1(std::numeric_limits<double>::quiet_NaN()), FString(TEXT("0.0")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrackCaptureToJsonTest, "CrackTwin.Capture.ToJson",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCrackCaptureToJsonTest::RunTest(const FString& Parameters)
{
	// Contract example (PDF 1-4 ①).
	FCrackCaptureResult R;
	R.Id = TEXT("c-0001");
	R.StructureId = 1;
	R.CameraLocation = FVector(820.0, -150.0, 400.0);
	R.Yaw = 90.0;
	R.Pitch = -5.0;
	R.bHitOk = true;
	R.HitLocation = FVector(1250.0, -150.0, 380.0);
	R.DistanceCm = 430.0;
	R.FovDeg = 90.0;

	TestEqual(TEXT("matches contract example"), CrackCapture::ToJson(R), FString(
		TEXT("{\"type\":\"capture_result\",\"id\":\"c-0001\",\"structureId\":1,")
		TEXT("\"drone\":{\"x\":820.0,\"y\":-150.0,\"z\":400.0,\"yaw\":90.0,\"pitch\":-5.0},")
		TEXT("\"hit\":{\"x\":1250.0,\"y\":-150.0,\"z\":380.0},\"hitOk\":true,")
		TEXT("\"distanceCm\":430.0,\"fovDeg\":90.0}")));

	// Miss: hit and distance are zeroed even if stale values are present.
	R.bHitOk = false;
	const FString Miss = CrackCapture::ToJson(R);
	TestTrue(TEXT("miss has hitOk false"), Miss.Contains(TEXT("\"hitOk\":false")));
	TestTrue(TEXT("miss zeroes hit"), Miss.Contains(TEXT("\"hit\":{\"x\":0.0,\"y\":0.0,\"z\":0.0}")));
	TestTrue(TEXT("miss zeroes distance"), Miss.Contains(TEXT("\"distanceCm\":0.0")));

	// Id with quotes must stay valid JSON.
	R.Id = TEXT("a\"b\\c");
	TestTrue(TEXT("id escaped"), CrackCapture::ToJson(R).Contains(TEXT("\"id\":\"a\\\"b\\\\c\"")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrackCaptureParseTest, "CrackTwin.Capture.ParseRequest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCrackCaptureParseTest::RunTest(const FString& Parameters)
{
	FString Id;
	TestTrue(TEXT("capture accepted"), CrackCapture::ParseCaptureRequest(TEXT("{\"type\":\"capture\",\"id\":\"c-0001\"}"), Id));
	TestEqual(TEXT("id read"), Id, FString(TEXT("c-0001")));

	TestTrue(TEXT("numeric id accepted"), CrackCapture::ParseCaptureRequest(TEXT("{\"type\":\"capture\",\"id\":7}"), Id));
	TestEqual(TEXT("numeric id converted"), Id, FString(TEXT("7")));

	TestTrue(TEXT("missing id still a capture"), CrackCapture::ParseCaptureRequest(TEXT("{\"type\":\"capture\"}"), Id));
	TestTrue(TEXT("missing id is empty"), Id.IsEmpty());

	TestFalse(TEXT("goto ignored"), CrackCapture::ParseCaptureRequest(TEXT("{\"type\":\"goto\",\"id\":\"x\"}"), Id));
	TestFalse(TEXT("non-JSON ignored"), CrackCapture::ParseCaptureRequest(TEXT("capture"), Id));
	TestFalse(TEXT("empty ignored"), CrackCapture::ParseCaptureRequest(TEXT(""), Id));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrackUploadContractTest, "CrackTwin.Capture.UploadContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCrackUploadContractTest::RunTest(const FString& Parameters)
{
	FString Id;
	int32 InspectionId = 99;
	TestTrue(TEXT("new capture accepted"), CrackCapture::ParseCaptureRequest(TEXT("{\"type\":\"capture\",\"id\":\"c-1\",\"inspectionId\":3}"), Id, &InspectionId));
	TestEqual(TEXT("inspection read"), InspectionId, 3);
	for (const FString& Value : { FString(TEXT("null")), FString(TEXT("3.5")), FString(TEXT("\"3\"")), FString(TEXT("true")), FString(TEXT("0")), FString(TEXT("-1")), FString(TEXT("2147483648")) })
	{
		InspectionId = 99;
		TestTrue(TEXT("invalid inspection remains a capture"), CrackCapture::ParseCaptureRequest(TEXT("{\"type\":\"capture\",\"inspectionId\":") + Value + TEXT("}"), Id, &InspectionId));
		TestEqual(TEXT("invalid inspection sentinel"), InspectionId, 0);
	}
	CrackCapture::ParseCaptureRequest(TEXT("{\"type\":\"capture\"}"), Id, &InspectionId);
	TestEqual(TEXT("missing inspection sentinel"), InspectionId, 0);
	TestEqual(TEXT("base url"), CrackCapture::BuildDetectUrl(TEXT("http://localhost:18080"), 3), FString(TEXT("http://localhost:18080/inspections/3/detect")));
	TestEqual(TEXT("trailing slash"), CrackCapture::BuildDetectUrl(TEXT("http://localhost:18080///"), 3), CrackCapture::BuildDetectUrl(TEXT("http://localhost:18080"), 3));
	TestTrue(TEXT("missing url"), CrackCapture::BuildDetectUrl(TEXT(""), 3).IsEmpty());
	TestTrue(TEXT("quote in url refused"), CrackCapture::BuildDetectUrl(TEXT("http://localhost/\" -bad"), 3).IsEmpty());
	TestTrue(TEXT("bad scheme"), CrackCapture::BuildDetectUrl(TEXT("file://localhost"), 3).IsEmpty());
	TestTrue(TEXT("bad inspection"), CrackCapture::BuildDetectUrl(TEXT("http://localhost"), 0).IsEmpty());
	TestEqual(TEXT("error escaped"), CrackCapture::DetectError(TEXT("a\"b"), 422, TEXT("서버\n\"오류\"\\")), FString(TEXT("{\"type\":\"detect_error\",\"id\":\"a\\\"b\",\"status\":422,\"message\":\"서버\\n\\\"오류\\\"\\\\\"}")));
	const TArray64<uint8> Image = { 0xff, 0xd8, 0, 0x80, 0xff, 0xd9 };
	const FString Meta = TEXT("{\"id\":\"한글\"}");
	const TArray<uint8> Body = CrackCapture::BuildMultipart(Meta, Image, TEXT("test-boundary"));
	const FString Prefix = TEXT("--test-boundary\r\nContent-Disposition: form-data; name=\"meta\"\r\n\r\n") + Meta + TEXT("\r\n--test-boundary\r\nContent-Disposition: form-data; name=\"image\"; filename=\"capture.jpg\"\r\nContent-Type: image/jpeg\r\n\r\n");
	FTCHARToUTF8 Utf8(*Prefix);
	TestTrue(TEXT("meta bytes preserved"), Body.Num() > Utf8.Length() && FMemory::Memcmp(Body.GetData(), Utf8.Get(), Utf8.Length()) == 0);
	TestTrue(TEXT("binary preserved"), Body.Num() >= Utf8.Length() + Image.Num() && FMemory::Memcmp(Body.GetData() + Utf8.Length(), Image.GetData(), Image.Num()) == 0);
	const FString End = TEXT("\r\n--test-boundary--\r\n");
	FTCHARToUTF8 EndUtf8(*End);
	TestEqual(TEXT("exact body length"), Body.Num(), Utf8.Length() + static_cast<int32>(Image.Num()) + EndUtf8.Length());
	TestTrue(TEXT("closing boundary"), Body.Num() >= EndUtf8.Length() && FMemory::Memcmp(Body.GetData() + Body.Num() - EndUtf8.Length(), EndUtf8.Get(), EndUtf8.Length()) == 0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
