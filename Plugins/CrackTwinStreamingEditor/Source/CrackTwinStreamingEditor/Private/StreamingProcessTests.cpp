#include "Misc/AutomationTest.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"
#include "StreamingProcess.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrackStreamingOwnershipTest,
    "CrackTwin.Streaming.OwnedProcess", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrackStreamingOwnershipTest::RunTest(const FString& Parameters)
{
    FCrackStreamingProcess Owned;
    FCrackStreamingProcess Other;
    const FString Cmd = FPaths::Combine(FPlatformMisc::GetEnvironmentVariable(TEXT("SystemRoot")), TEXT("System32/cmd.exe"));
    const FString Args = TEXT("/d /c ping -t 127.0.0.1");
    TestFalse(TEXT("Initially stopped"), Owned.IsRunning());
    TestFalse(TEXT("Missing executable rejected"), Owned.Start(TEXT("Z:/CrackTwinMissing.exe"), TEXT(""), TEXT("")));
    TestTrue(TEXT("Start owned process"), Owned.Start(Cmd, Args, FPaths::ProjectDir()));
    TestTrue(TEXT("Start independent process"), Other.Start(Cmd, Args, FPaths::ProjectDir()));
    TestFalse(TEXT("Duplicate start rejected"), Owned.Start(Cmd, Args, FPaths::ProjectDir()));
    TestTrue(TEXT("Owned process stopped"), Owned.Stop());
    TestFalse(TEXT("Owned process no longer running"), Owned.IsRunning());
    TestTrue(TEXT("Independent process preserved"), Other.IsRunning());
    TestTrue(TEXT("Repeated stop harmless"), Owned.Stop());
    TestTrue(TEXT("Cleanup independent process"), Other.Stop());
    return true;
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrackStreamingBatchLaunchTest,
    "CrackTwin.Streaming.BatchLaunch", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrackStreamingBatchLaunchTest::RunTest(const FString& Parameters)
{
    // Match the panel's CreateProc + cmd /s /c path, including forward slashes from FPaths.
    FString Directory = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("Tools/StreamingPanelLogs")));
    IFileManager::Get().MakeDirectory(*Directory, true);
    const FString Token = FGuid::NewGuid().ToString();
    FString Script = FPaths::Combine(Directory, Token + TEXT(" launch probe.bat"));
    FString Output = FPaths::Combine(Directory, Token + TEXT(" probe output.log"));
    FFileHelper::SaveStringToFile(TEXT("@echo off\r\necho CRACKTWIN_BATCH_OK\r\n"), *Script,
        FFileHelper::EEncodingOptions::ForceAnsi);
    FPaths::MakePlatformFilename(Script);
    FPaths::MakePlatformFilename(Output);
    const FString Cmd = FPaths::Combine(FPlatformMisc::GetEnvironmentVariable(TEXT("SystemRoot")), TEXT("System32/cmd.exe"));
    const FString Args = FString::Printf(TEXT("/d /s /c \"\"%s\" > \"%s\" 2>&1 < NUL\""), *Script, *Output);
    FCrackStreamingProcess Process;
    TestTrue(TEXT("Launch batch via panel command form"), Process.Start(Cmd, Args, Directory));
    const double Deadline = FPlatformTime::Seconds() + 2.0;
    while (Process.IsRunning() && FPlatformTime::Seconds() < Deadline) FPlatformProcess::Sleep(0.01f);
    TestFalse(TEXT("Probe completes"), Process.IsRunning());
    Process.Stop();
    FString Text;
    FFileHelper::LoadFileToString(Text, *Output);
    TestTrue(TEXT("Batch executed and redirected output, including paths with spaces"), Text.Contains(TEXT("CRACKTWIN_BATCH_OK")));
    IFileManager::Get().Delete(*Script);
    IFileManager::Get().Delete(*Output);
    return true;
}
#endif
