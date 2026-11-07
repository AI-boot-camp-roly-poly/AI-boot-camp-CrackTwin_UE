#include "Modules/ModuleManager.h"
#include "Containers/Ticker.h"
#include "Editor.h"
#include "Framework/Docking/TabManager.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/AutomationTest.h"
#include "Styling/AppStyle.h"
#include "SocketSubsystem.h"
#include "Sockets.h"
#include "StreamingProcess.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "CrackTwinStreamingEditor"
DEFINE_LOG_CATEGORY_STATIC(LogCrackStreamingPanel, Log, All);

namespace CrackStreamingPanel
{
const FName TabName(TEXT("CrackTwinStreaming"));

bool NormalizeServerIp(const FString& Input, FString& Result)
{
    Result.Reset();
    if (Input.Contains(TEXT("\n")) || Input.Contains(TEXT("\r")) || Input.Contains(TEXT("\t"))) return false;
    TArray<FString> Parts;
    Input.TrimStartAndEnd().ParseIntoArray(Parts, TEXT("."), false);
    if (Parts.Num() != 4) return false;
    uint32 Octets[4];
    for (int32 Index = 0; Index < 4; ++Index)
    {
        if (Parts[Index].IsEmpty() || Parts[Index].Len() > 3) return false;
        for (TCHAR Ch : Parts[Index]) if (Ch < TEXT('0') || Ch > TEXT('9')) return false;
        if (!LexTryParseString(Octets[Index], *Parts[Index]) || Octets[Index] > 255) return false;
    }
    if (Octets[0] == 0 || Octets[0] >= 224) return false;
    Result = FString::Printf(TEXT("%u.%u.%u.%u"), Octets[0], Octets[1], Octets[2], Octets[3]);
    return true;
}

FString ServerArguments(const FString& Script, const FString& Log, const FString& Ip, int32 PlayerPort = 80, int32 StreamerPort = 8888)
{
    FString Address;
    if (!NormalizeServerIp(Ip, Address) || PlayerPort < 1 || PlayerPort > 65535 || StreamerPort < 1 || StreamerPort > 65535) return FString();
    return FString::Printf(TEXT("/d /s /c \"\"%s\" --publicip %s -- --player_port %d --streamer_port %d > \"%s\" 2>&1 < NUL\""),
        *Script, *Address, PlayerPort, StreamerPort, *Log);
}

FString DetectServerIp()
{
    ISocketSubsystem* Subsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
    if (!Subsystem) return TEXT("127.0.0.1");
    // UDP connect selects the default route's interface without sending a packet.
    FSocket* Probe = Subsystem->CreateSocket(NAME_DGram, TEXT("CrackTwin LAN address"), ESocketProtocolFamily::IPv4);
    if (!Probe) return TEXT("127.0.0.1");
    auto Destination = Subsystem->CreateInternetAddr();
    Destination->SetIp(0x01010101);
    Destination->SetPort(9);
    FString Address;
    if (Probe->Connect(*Destination))
    {
        auto Local = Subsystem->CreateInternetAddr();
        Probe->GetAddress(*Local);
        NormalizeServerIp(Local->ToString(false), Address);
    }
    Probe->Close();
    Subsystem->DestroySocket(Probe);
    return Address.IsEmpty() ? TEXT("127.0.0.1") : Address;
}

FString& PublicIp()
{
    static FString Address = DetectServerIp();
    return Address;
}

// Read only a bounded tail, including while the server/game is writing to the file.
bool ReadLogTail(const FString& Path, FString& Text)
{
    Text.Reset();
    TUniquePtr<FArchive> Reader(IFileManager::Get().CreateFileReader(*Path, FILEREAD_AllowWrite | FILEREAD_Silent));
    if (!Reader) return false;
    constexpr int64 MaxBytes = 64 * 1024;
    const int64 Size = Reader->TotalSize();
    const int64 Offset = FMath::Max<int64>(0, Size - MaxBytes);
    Reader->Seek(Offset);
    TArray<uint8> Bytes;
    Bytes.SetNumUninitialized(static_cast<int32>(Size - Offset));
    if (!Bytes.IsEmpty()) Reader->Serialize(Bytes.GetData(), Bytes.Num());
    if (Reader->IsError()) return false;
    int32 Start = 0;
    // A tail can begin midway through a UTF-8 code point. Omit that partial prefix.
    if (Offset > 0)
        while (Start < Bytes.Num() && (Bytes[Start] & 0xc0) == 0x80) ++Start;
    if (Start < Bytes.Num()) FFileHelper::BufferToString(Text, Bytes.GetData() + Start, Bytes.Num() - Start);
    if (Offset > 0)
    {
        int32 Newline;
        if (Text.FindChar(TEXT('\n'), Newline)) Text.RightChopInline(Newline + 1);
        Text.InsertAt(0, TEXT("[이전 내용 생략 · 최근 64KB]\n"));
    }
    return true;
}

class SLogViewer : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SLogViewer) {}
        SLATE_ATTRIBUTE(FString, ServerPath)
        SLATE_ATTRIBUTE(FString, GamePath)
    SLATE_END_ARGS()

    void Construct(const FArguments& Args)
    {
        ServerPath = Args._ServerPath;
        GamePath = Args._GamePath;
        ChildSlot
        [ SNew(SVerticalBox)
          + SVerticalBox::Slot().AutoHeight().Padding(0, 4)
          [ SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
            [ SNew(SButton).Text(LOCTEXT("ServerView", "서버 로그"))
                .OnClicked_Lambda([this] { bGame = false; Refresh(true); return FReply::Handled(); }) ]
            + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 12, 0)
            [ SNew(SButton).Text(LOCTEXT("GameView", "게임 로그"))
                .OnClicked_Lambda([this] { bGame = true; Refresh(true); return FReply::Handled(); }) ]
            + SHorizontalBox::Slot().AutoWidth()
            [ SNew(SCheckBox).IsChecked_Lambda([this] { return bLive ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
                .OnCheckStateChanged_Lambda([this](ECheckBoxState State) { bLive = State == ECheckBoxState::Checked; if (bLive) Refresh(true); })
                [ SNew(STextBlock).Text(LOCTEXT("LiveLog", "실시간 갱신·맨 아래 따라가기")) ] ]
          ]
          + SVerticalBox::Slot().AutoHeight().Padding(0, 4)
          [ SNew(STextBlock).AutoWrapText(true).Text_Lambda([this] {
              return FText::FromString(FString(bGame ? TEXT("게임 로그: ") : TEXT("서버 로그: ")) + (bGame ? GamePath.Get() : ServerPath.Get()));
          }) ]
          + SVerticalBox::Slot().AutoHeight()
          [ SNew(SBox).HeightOverride(300)
            [ SAssignNew(LogText, SMultiLineEditableTextBox)
                .IsReadOnly(true).AutoWrapText(false).AlwaysShowScrollbars(true)
                .Font(FAppStyle::GetFontStyle(TEXT("Log.Normal"))) ] ]
          + SVerticalBox::Slot().AutoHeight().Padding(0, 4)
          [ SNew(STextBlock).AutoWrapText(true).Text(LOCTEXT("LogHelp", "이전 로그를 읽으려면 실시간 갱신을 끄세요. 세로·가로 스크롤 및 텍스트 복사가 가능합니다.")) ]
        ];
        Refresh(true);
        RegisterActiveTimer(1.0f, FWidgetActiveTimerDelegate::CreateLambda([this](double, float) {
            if (bLive) Refresh(false);
            return EActiveTimerReturnType::Continue;
        }));
    }

private:
    TAttribute<FString> ServerPath, GamePath;
    TSharedPtr<SMultiLineEditableTextBox> LogText;
    FString PreviousText;
    bool bGame = false;
    bool bLive = true;

    void Refresh(bool bForce)
    {
        const FString Path = bGame ? GamePath.Get() : ServerPath.Get();
        FString Text;
        if (Path.IsEmpty()) Text = TEXT("이 패널에서 실행하면 로그가 여기에 표시됩니다.");
        else if (!ReadLogTail(Path, Text)) Text = TEXT("로그 파일을 아직 읽을 수 없습니다. 생성되면 자동으로 표시됩니다.");
        else if (Text.IsEmpty()) Text = TEXT("로그 파일이 비어 있습니다. 출력을 기다리는 중입니다.");
        if (bForce || Text != PreviousText)
        {
            PreviousText = Text;
            LogText->SetText(FText::FromString(Text));
            if (bLive) LogText->GoTo(ETextLocation::EndOfDocument);
        }
    }
};

bool PortResponds(int32 Port)
{
    ISocketSubsystem* Subsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
    if (!Subsystem) return false;
    FSocket* Socket = Subsystem->CreateSocket(NAME_Stream, TEXT("CrackTwin local status"), false);
    if (!Socket) return false;
    TSharedRef<FInternetAddr> Address = Subsystem->CreateInternetAddr();
    Address->SetIp(0x7f000001);
    Address->SetPort(Port);
    Socket->SetNonBlocking(true);
    Socket->Connect(*Address);
    Socket->Wait(ESocketWaitConditions::WaitForWrite, FTimespan::FromMilliseconds(10));
    const bool Connected = Socket->GetConnectionState() == SCS_Connected;
    Socket->Close();
    Subsystem->DestroySocket(Socket);
    return Connected;
}
}

class FCrackTwinStreamingEditorModule : public IModuleInterface
{
public:
    virtual void StartupModule() override
    {
        if (IsRunningCommandlet()) return;
        FParse::Value(FCommandLine::Get(), TEXT("CrackApiUrl="), ApiUrl);
        FGlobalTabmanager::Get()->RegisterNomadTabSpawner(CrackStreamingPanel::TabName,
            FOnSpawnTab::CreateRaw(this, &FCrackTwinStreamingEditorModule::SpawnTab))
            .SetDisplayName(LOCTEXT("Title", "CrackTwin Pixel Streaming"));
        UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(
            this, &FCrackTwinStreamingEditorModule::RegisterMenu));
        Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(
            this, &FCrackTwinStreamingEditorModule::Poll), 1.0f);
    }

    virtual void ShutdownModule() override
    {
        if (IsRunningCommandlet()) return;
        FTSTicker::GetCoreTicker().RemoveTicker(Ticker);
        StopOwned();
        if (PanelTab.IsValid()) PanelTab.Pin()->RequestCloseTab();
        UToolMenus::UnRegisterStartupCallback(this);
        UToolMenus::UnregisterOwner(this);
        FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(CrackStreamingPanel::TabName);
    }

private:
    FCrackStreamingProcess Server;
    FCrackStreamingProcess Game;
    FTSTicker::FDelegateHandle Ticker;
    TWeakPtr<SDockTab> PanelTab;
    bool bPlayerPort = false;
    bool bStreamerPort = false;
    bool bServerWasRunning = false;
    bool bGameWasRunning = false;
    FString LastMessage = TEXT("서버 시작 → 포트 응답 확인 → 드론 실행 → 브라우저 열기 순서입니다.");
    FString ServerLog;
    FString GameLog;
    FString ApiUrl;

    FString ProjectRoot() const { return FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()); }
    bool IsPlaying() const { return GEditor && GEditor->PlayWorld; }

    void Report(const FString& Message)
    {
        LastMessage = Message;
        UE_LOG(LogCrackStreamingPanel, Display, TEXT("%s"), *Message);
    }

    bool Poll(float)
    {
        bPlayerPort = CrackStreamingPanel::PortResponds(80);
        bStreamerPort = CrackStreamingPanel::PortResponds(8888);
        const bool ServerRunning = Server.IsRunning();
        const bool GameRunning = Game.IsRunning();
        if (bServerWasRunning && !ServerRunning) Report(TEXT("시그널링 서버 프로세스가 종료됐습니다. 서버 로그를 확인하세요."));
        if (bGameWasRunning && !GameRunning) Report(TEXT("드론 게임 프로세스가 종료됐습니다. 게임 로그를 확인하세요."));
        bServerWasRunning = ServerRunning;
        bGameWasRunning = GameRunning;
        return true;
    }

    void RegisterMenu()
    {
        FToolMenuOwnerScoped Owner(this);
        UToolMenu* Menu = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.MainMenu.Tools"));
        Menu->FindOrAddSection(TEXT("CrackTwin")).AddMenuEntry(
            TEXT("CrackTwinStreamingPanel"), LOCTEXT("Menu", "CrackTwin Pixel Streaming"),
            LOCTEXT("MenuTip", "같은 PC·팀원 LAN용 시그널링 서버와 가상 드론을 실행합니다."), FSlateIcon(),
            FUIAction(FExecuteAction::CreateLambda([] {
                FGlobalTabmanager::Get()->TryInvokeTab(CrackStreamingPanel::TabName);
            })));
    }

    FReply StartServer()
    {
        if (IsPlaying() || Server.IsRunning()) return FReply::Handled();
        FString Address;
        if (!CrackStreamingPanel::NormalizeServerIp(CrackStreamingPanel::PublicIp(), Address))
        {
            Report(TEXT("서버 공개 IPv4 주소를 확인하세요. 예: 172.40.200.119 (URL·공백·명령어는 입력하지 않습니다)."));
            return FReply::Handled();
        }
        CrackStreamingPanel::PublicIp() = Address;
        Poll(0);
        if (bPlayerPort || bStreamerPort)
        {
            Report(TEXT("포트 80 또는 8888이 이미 사용 중입니다. 중복 서버를 실행하지 않습니다. 외부 프로세스는 종료하지 않습니다."));
            return FReply::Handled();
        }
        FString Script = FPaths::Combine(ProjectRoot(), TEXT("Tools/PixelStreaming/SignallingWebServer/platform_scripts/cmd/start.bat"));
        FPaths::MakePlatformFilename(Script);
        if (!FPaths::FileExists(Script))
        {
            Report(TEXT("start.bat가 없습니다. Tools/PIXEL_STREAMING_LOCAL.md의 UE5.8 설치 절차를 확인하세요."));
            return FReply::Handled();
        }
        const FString LogDir = FPaths::Combine(ProjectRoot(), TEXT("Tools/StreamingPanelLogs"));
        if (!IFileManager::Get().MakeDirectory(*LogDir, true))
        {
            Report(TEXT("로그 폴더를 만들 수 없습니다."));
            return FReply::Handled();
        }
        ServerLog = FPaths::Combine(LogDir, FString::Printf(TEXT("server-%s.log"), *FGuid::NewGuid().ToString()));
        FPaths::MakePlatformFilename(ServerLog);
        const FString Cmd = FPaths::Combine(FPlatformMisc::GetEnvironmentVariable(TEXT("SystemRoot")), TEXT("System32/cmd.exe"));
        // The outer quote pair is required by cmd /s /c for a quoted batch path.
        const FString Args = CrackStreamingPanel::ServerArguments(Script, ServerLog, Address);
        const bool Started = Server.Start(Cmd, Args, FPaths::GetPath(Script));
        bServerWasRunning = Started;
        Report(Started ? FString::Printf(TEXT("서버 실행 요청: 팀원 접속 http://%s · 포트 80/8888 응답을 기다리세요."), *Address)
                       : TEXT("서버 프로세스를 실행할 수 없습니다."));
        return FReply::Handled();
    }

    FReply StartGame()
    {
        if (IsPlaying() || Game.IsRunning()) return FReply::Handled();
        ApiUrl.TrimStartAndEndInline();
        if ((!ApiUrl.StartsWith(TEXT("http://")) && !ApiUrl.StartsWith(TEXT("https://")))
            || ApiUrl.Contains(TEXT("\"")) || ApiUrl.Contains(TEXT("\\")) || ApiUrl.Contains(TEXT(" "))
            || ApiUrl.Contains(TEXT("\r")) || ApiUrl.Contains(TEXT("\n")) || ApiUrl.Contains(TEXT("\t")))
        {
            Report(TEXT("점검 API의 HTTP(S) 주소를 입력하세요. 공백과 따옴표는 사용할 수 없습니다."));
            return FReply::Handled();
        }
        Poll(0);
        if (!bPlayerPort || !bStreamerPort)
        {
            Report(TEXT("포트 80/8888 응답이 모두 필요합니다. 서버 시작 상태와 로그를 확인하세요."));
            return FReply::Handled();
        }
        const FString Executable = FPaths::ConvertRelativePathToFull(FPaths::Combine(
            FPaths::EngineDir(), TEXT("Binaries/Win64/UnrealEditor.exe")));
        const FString LogDir = FPaths::Combine(ProjectRoot(), TEXT("Tools/StreamingPanelLogs"));
        if (!IFileManager::Get().MakeDirectory(*LogDir, true))
        {
            Report(TEXT("로그 폴더를 만들 수 없습니다."));
            return FReply::Handled();
        }
        GameLog = FPaths::Combine(LogDir, FString::Printf(TEXT("game-%s.log"), *FGuid::NewGuid().ToString()));
        const FString Project = FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath());
        const FString Args = FString::Printf(TEXT("\"%s\" /Game/DynamicMeshCreator/Map/Main -game -windowed -ResX=1920 -ResY=1080 -PixelStreamingURL=ws://127.0.0.1:8888 -CrackApiUrl=\"%s\" -abslog=\"%s\""), *Project, *ApiUrl, *GameLog);
        const bool Started = Game.Start(Executable, Args, ProjectRoot(), false);
        bGameWasRunning = Started;
        Report(Started ? TEXT("1920×1080 드론을 실행했습니다. 브라우저에서 영상·조작을 확인하세요.")
                       : TEXT("드론 프로세스를 실행할 수 없습니다. UE5.8 실행 파일을 확인하세요."));
        return FReply::Handled();
    }

    FReply OpenBrowser()
    {
        FString Error;
        FPlatformProcess::LaunchURL(TEXT("http://localhost"), nullptr, &Error);
        Report(Error.IsEmpty() ? TEXT("기본 브라우저에 localhost 열기를 요청했습니다. 영상 연결은 직접 확인하세요.") : Error);
        return FReply::Handled();
    }

    FReply StopOwned()
    {
        const bool GameStopped = Game.Stop();
        const bool ServerStopped = Server.Stop();
        bGameWasRunning = Game.IsRunning();
        bServerWasRunning = Server.IsRunning();
        Report(GameStopped && ServerStopped ? TEXT("이 패널이 실행한 드론·서버를 종료했습니다. 외부 프로세스는 유지됩니다.")
                                            : TEXT("일부 소유 프로세스가 종료되지 않았습니다. 다시 상태를 확인하세요."));
        return FReply::Handled();
    }

    TSharedRef<SDockTab> SpawnTab(const FSpawnTabArgs&)
    {
        Poll(0);
        TSharedRef<SDockTab> Tab = SNew(SDockTab).TabRole(ETabRole::NomadTab);
        PanelTab = Tab;
        Tab->SetContent(
            SNew(SBox).MinDesiredWidth(640).Padding(16)
            [ SNew(SScrollBox)
              + SScrollBox::Slot()
              [ SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)
                [ SNew(STextBlock).Text(LOCTEXT("Intro", "같은 PC·팀원 LAN 접속 · UE5.8 · 1920×1080\n서버 주소 확인 → 서버 시작 → 드론 실행 → 브라우저 열기")) ]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 4)
                [ SNew(STextBlock).Text(LOCTEXT("PublicIpLabel", "서버 공개 IPv4 (이 UE PC의 Wi-Fi/유선 IP)")) ]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 4)
                [ SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().FillWidth(1)
                    [ SNew(SEditableTextBox).Text_Lambda([] { return FText::FromString(CrackStreamingPanel::PublicIp()); })
                        .IsEnabled_Lambda([this] { return !Server.IsRunning() && !IsPlaying(); })
                        .OnTextChanged_Lambda([](const FText& Text) { CrackStreamingPanel::PublicIp() = Text.ToString(); }) ]
                    + SHorizontalBox::Slot().AutoWidth().Padding(8, 0, 0, 0)
                    [ SNew(SButton).Text(LOCTEXT("DetectIp", "내 PC IP 찾기"))
                        .IsEnabled_Lambda([this] { return !Server.IsRunning() && !IsPlaying(); })
                        .OnClicked_Lambda([] { CrackStreamingPanel::PublicIp() = CrackStreamingPanel::DetectServerIp(); return FReply::Handled(); }) ]
                ]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 4)
                [ SNew(SEditableTextBox).IsReadOnly(true).Text_Lambda([] {
                    FString Ip;
                    return FText::FromString(CrackStreamingPanel::NormalizeServerIp(CrackStreamingPanel::PublicIp(), Ip)
                        ? FString::Printf(TEXT("팀원 접속: http://%s"), *Ip) : TEXT("올바른 IPv4 주소를 입력하세요."));
                }) ]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 4)
                [ SNew(STextBlock).AutoWrapText(true).Text(LOCTEXT("LanHelp", "같은 PC는 localhost, 팀원은 위 주소로 접속합니다. 127.0.0.1은 같은 PC 전용입니다. 주소 변경은 서버 재시작 후 반영됩니다.")) ]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 4)
                [ SNew(STextBlock).Text(LOCTEXT("ApiLabel", "점검 API 주소 (드론 실행 전에 입력)")) ]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 4)
                [ SNew(SEditableTextBox).Text(FText::FromString(ApiUrl))
                    .HintText(LOCTEXT("ApiHint", "백엔드 담당자가 제공한 HTTP(S) 주소"))
                    .IsEnabled_Lambda([this] { return !Game.IsRunning(); })
                    .OnTextChanged_Lambda([this](const FText& Text) { ApiUrl = Text.ToString(); }) ]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 4)
                [ SNew(SButton).Text(LOCTEXT("Server", "1. 서버 시작"))
                    .IsEnabled_Lambda([this] { return !IsPlaying() && !Server.IsRunning(); })
                    .OnClicked_Raw(this, &FCrackTwinStreamingEditorModule::StartServer) ]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 4)
                [ SNew(SButton).Text(LOCTEXT("Game", "2. 드론 실행"))
                    .IsEnabled_Lambda([this] { return !IsPlaying() && !Game.IsRunning() && bPlayerPort && bStreamerPort; })
                    .OnClicked_Raw(this, &FCrackTwinStreamingEditorModule::StartGame) ]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 4)
                [ SNew(SButton).Text(LOCTEXT("Browser", "3. 브라우저 열기"))
                    .OnClicked_Raw(this, &FCrackTwinStreamingEditorModule::OpenBrowser) ]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 4)
                [ SNew(SButton).Text(LOCTEXT("Stop", "드론·서버 종료 (이 패널에서 실행한 것만)"))
                    .OnClicked_Raw(this, &FCrackTwinStreamingEditorModule::StopOwned) ]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 12)
                [ SNew(STextBlock).AutoWrapText(true).Text_Lambda([this] {
                    return FText::FromString(FString::Printf(TEXT("도구 소유 서버: %s / 드론: %s\n포트 80: %s / 8888: %s\n포트 응답은 영상 연결 성공을 뜻하지 않습니다.\nPIE 중에는 시작 버튼을 사용할 수 없습니다."),
                        Server.IsRunning() ? TEXT("실행 중") : TEXT("없음"), Game.IsRunning() ? TEXT("실행 중") : TEXT("없음"),
                        bPlayerPort ? TEXT("응답 있음") : TEXT("응답 없음"), bStreamerPort ? TEXT("응답 있음") : TEXT("응답 없음")));
                }) ]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 4)
                [ SNew(STextBlock).AutoWrapText(true).Text_Lambda([this] { return FText::FromString(LastMessage); }) ]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 4)
                [ SNew(CrackStreamingPanel::SLogViewer)
                    .ServerPath_Lambda([this] { return ServerLog; })
                    .GamePath_Lambda([this] { return GameLog; }) ]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 8)
                [ SNew(STextBlock).AutoWrapText(true).Text(LOCTEXT("Lifecycle", "패널을 닫아도 실행은 유지됩니다. 에디터 종료 시 도구 소유 드론·서버도 종료됩니다.")) ]
              ]
            ]);
        return Tab;
    }
};

IMPLEMENT_MODULE(FCrackTwinStreamingEditorModule, CrackTwinStreamingEditor)

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrackStreamingLanAddressTest, "CrackTwin.Streaming.LanAddress",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCrackStreamingLanAddressTest::RunTest(const FString& Parameters)
{
    FString Ip;
    TestTrue(TEXT("LAN address accepted"), CrackStreamingPanel::NormalizeServerIp(TEXT(" 172.40.200.119 "), Ip));
    TestEqual(TEXT("address normalized"), Ip, FString(TEXT("172.40.200.119")));
    TestTrue(TEXT("local testing still supported"), CrackStreamingPanel::NormalizeServerIp(TEXT("127.0.0.1"), Ip));
    for (const FString Bad : {TEXT(""), TEXT("172.40.200"), TEXT("172.40.200.256"), TEXT("http://172.40.200.119"),
        TEXT("172.40.200.119 & calc"), TEXT("172.40.200.119\n"), TEXT("0.0.0.0"), TEXT("255.255.255.255"), TEXT("224.1.1.1")})
        TestFalse(TEXT("invalid address or command injection rejected"), CrackStreamingPanel::NormalizeServerIp(Bad, Ip));
    const FString Args = CrackStreamingPanel::ServerArguments(TEXT("C:\\project\\start.bat"), TEXT("C:\\logs\\server.log"), TEXT("172.40.200.119"));
    TestTrue(TEXT("server advertises LAN IP"), Args.Contains(TEXT("--publicip 172.40.200.119")));
    TestFalse(TEXT("server does not advertise loopback"), Args.Contains(TEXT("--publicip 127.0.0.1")));
    TestTrue(TEXT("UE5.8 player/streamer ports preserved"), Args.Contains(TEXT("-- --player_port 80 --streamer_port 8888")));
    TestTrue(TEXT("invalid IP cannot produce executable command"), CrackStreamingPanel::ServerArguments(TEXT("x"), TEXT("y"), TEXT("1.2.3.4 & calc")).IsEmpty());
    const FString Detected = CrackStreamingPanel::DetectServerIp();
    TestTrue(TEXT("detected address is a usable IPv4"), CrackStreamingPanel::NormalizeServerIp(Detected, Ip));
    AddInfo(FString::Printf(TEXT("Detected LAN server address: %s"), *Detected));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrackStreamingLanLaunchTest, "CrackTwin.Streaming.LanServerLaunch",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCrackStreamingLanLaunchTest::RunTest(const FString& Parameters)
{
    // Separate ports preserve the user's live LAN session on 80/8888.
    constexpr int32 PlayerPort = 18081, StreamerPort = 18889;
    if (CrackStreamingPanel::PortResponds(PlayerPort) || CrackStreamingPanel::PortResponds(StreamerPort)
        || CrackStreamingPanel::PortResponds(18890))
    {
        AddError(TEXT("LAN launch verification requires free test ports 18081/18889; existing servers are preserved."));
        return false;
    }
    const FString Ip = CrackStreamingPanel::DetectServerIp();
    if (!TestFalse(TEXT("LAN route detected rather than localhost"), Ip.StartsWith(TEXT("127.")))) return false;
    FString Script = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("Tools/PixelStreaming/SignallingWebServer/platform_scripts/cmd/start.bat")));
    FString Log = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("Tools/StreamingPanelLogs/lan-panel-probe-") + FGuid::NewGuid().ToString() + TEXT(".log")));
    FPaths::MakePlatformFilename(Script);
    FPaths::MakePlatformFilename(Log);
    const FString Cmd = FPaths::Combine(FPlatformMisc::GetEnvironmentVariable(TEXT("SystemRoot")), TEXT("System32/cmd.exe"));
    FCrackStreamingProcess Probe;
    const FString Args = CrackStreamingPanel::ServerArguments(Script, Log, Ip, PlayerPort, StreamerPort)
        .Replace(TEXT("-- --player_port"), TEXT("-- --sfu_port 18890 --player_port"));
    if (!TestTrue(TEXT("official UE5.8 server launches with panel arguments"), Probe.Start(Cmd, Args, FPaths::GetPath(Script)))) return false;
    const double Deadline = FPlatformTime::Seconds() + 15.0;
    FString Text;
    bool Ready = false;
    while (Probe.IsRunning() && FPlatformTime::Seconds() < Deadline)
    {
        CrackStreamingPanel::ReadLogTail(Log, Text);
        Ready = CrackStreamingPanel::PortResponds(PlayerPort) && CrackStreamingPanel::PortResponds(StreamerPort)
            && Text.Contains(TEXT("turn:") + Ip + TEXT(":19303"));
        if (Ready) break;
        FPlatformProcess::Sleep(0.05f);
    }
    TestTrue(TEXT("both ports respond and WebRTC options advertise LAN address"), Ready);
    AddInfo(FString::Printf(TEXT("LAN URL http://%s:%d, verification log %s"), *Ip, PlayerPort, *Log));
    TestTrue(TEXT("verification server cleaned up"), Probe.Stop());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrackStreamingLogTailTest, "CrackTwin.Streaming.LogTail",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCrackStreamingLogTailTest::RunTest(const FString& Parameters)
{
    const FString Directory = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("Tools/StreamingPanelLogs")));
    IFileManager::Get().MakeDirectory(*Directory, true);
    const FString Path = FPaths::Combine(Directory, FGuid::NewGuid().ToString() + TEXT("-test.log"));
    FString Text;
    TestFalse(TEXT("Missing log returns unavailable"), CrackStreamingPanel::ReadLogTail(Path, Text));
    const FString Unicode = TEXT("서버 시작\n게임 연결됨\n");
    TestTrue(TEXT("Write UTF-8 log"), FFileHelper::SaveStringToFile(Unicode, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
    TestTrue(TEXT("Read UTF-8 log"), CrackStreamingPanel::ReadLogTail(Path, Text));
    TestEqual(TEXT("Korean preserved"), Text, Unicode);
    TestTrue(TEXT("Append while reading"), FFileHelper::SaveStringToFile(TEXT("next\n"), *Path,
        FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM, &IFileManager::Get(), FILEWRITE_Append));
    TestTrue(TEXT("Read appended log"), CrackStreamingPanel::ReadLogTail(Path, Text));
    TestTrue(TEXT("New output included"), Text.EndsWith(TEXT("next\n")));
    const FString Large = FString::ChrN(100000, TEXT('x')) + TEXT("\nlatest line\n");
    FFileHelper::SaveStringToFile(Large, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    TestTrue(TEXT("Read oversized log"), CrackStreamingPanel::ReadLogTail(Path, Text));
    TestTrue(TEXT("Latest line preserved"), Text.EndsWith(TEXT("latest line\n")));
    TestTrue(TEXT("Old output bounded and marked"), Text.Len() < 66000 && Text.StartsWith(TEXT("[이전 내용 생략")));
    FFileHelper::SaveStringToFile(TEXT(""), *Path);
    TestTrue(TEXT("Truncated log readable"), CrackStreamingPanel::ReadLogTail(Path, Text));
    TestTrue(TEXT("Truncated log has no stale contents"), Text.IsEmpty());
    IFileManager::Get().Delete(*Path);
    return true;
}
#endif
#undef LOCTEXT_NAMESPACE
