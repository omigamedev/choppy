#!/usr/bin/env pwsh
# Run commands and check if they succeed (PowerShell 7+)

param (
    [switch] $Build,  # switch parameters are true if provided
    [switch] $Release  # switch parameters are true if provided
)

function Run-Command
{
    param (
        [Parameter(Mandatory = $true)]
        [string] $Command,

        [string] $ErrorMessage = "Command failed"
    )

    Write-Host "Running: $Command" -ForegroundColor Cyan

    try
    {
        # Use call operator (&) so arguments are handled properly
        & powershell -Command $Command

        if ($LASTEXITCODE -ne 0)
        {
            throw "$ErrorMessage (Exit code: $LASTEXITCODE)"
        }

        Write-Host "✅ Success: $Command" -ForegroundColor Green
    }
    catch
    {
        Write-Host "❌ Failed: $_" -ForegroundColor Red
        exit 1
    }
}

if ($Build)
{
    if ($Release)
    {
        Run-Command "./gradlew installRelease" "Failed to build release"
    }
    else
    {
        Run-Command "./gradlew installDebug" "Failed to build debug"
    }
    adb shell pm grant com.omixlab.choppyengine android.permission.RECORD_AUDIO
}
adb shell am force-stop com.omixlab.choppyengine
adb shell am start -n com.omixlab.choppyengine/.MainActivity
sleep 1
adb logcat --pid $(adb shell pidof -s com.omixlab.choppyengine) ChoppyEngine:D *:S
