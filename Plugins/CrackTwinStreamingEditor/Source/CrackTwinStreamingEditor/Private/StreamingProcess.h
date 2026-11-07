#pragma once

#include "CoreMinimal.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"

// Only handles created by this instance are ever terminated. Never adopt a PID by name/port.
class FCrackStreamingProcess
{
public:
    FCrackStreamingProcess() = default;
    FCrackStreamingProcess(const FCrackStreamingProcess&) = delete;
    FCrackStreamingProcess& operator=(const FCrackStreamingProcess&) = delete;
    ~FCrackStreamingProcess() { Stop(); }

    bool IsRunning() { return Handle.IsValid() && FPlatformProcess::IsProcRunning(Handle); }

    bool Start(const FString& Executable, const FString& Arguments, const FString& Directory, bool bHidden = true)
    {
        if (IsRunning() || !FPaths::FileExists(Executable)) return false;
        if (Handle.IsValid()) FPlatformProcess::CloseProc(Handle);
        // cmd.exe scans its own command line for /c. A URL-style /cmd.exe path
        // can be parsed as that switch, turning the remaining "md.exe ..." into mkdir.
        FString NativeExecutable = Executable;
        FString NativeDirectory = Directory;
        FPaths::MakePlatformFilename(NativeExecutable);
        FPaths::MakePlatformFilename(NativeDirectory);
        Handle = FPlatformProcess::CreateProc(*NativeExecutable, *Arguments, false, bHidden, bHidden,
            nullptr, 0, NativeDirectory.IsEmpty() ? nullptr : *NativeDirectory, nullptr);
        return Handle.IsValid();
    }

    bool Stop()
    {
        if (!Handle.IsValid()) return true;
        if (IsRunning())
        {
            FPlatformProcess::TerminateProc(Handle, true);
            // Windows termination is asynchronous; allow a bounded interval to complete.
            const double Deadline = FPlatformTime::Seconds() + 0.25;
            while (IsRunning() && FPlatformTime::Seconds() < Deadline) FPlatformProcess::Sleep(0.01f);
            if (IsRunning()) return false;
        }
        FPlatformProcess::CloseProc(Handle);
        return true;
    }

private:
    FProcHandle Handle;
};
