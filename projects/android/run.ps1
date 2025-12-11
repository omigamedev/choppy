#!/usr/bin/env pwsh
# Run commands and check if they succeed (PowerShell 7+)

param (
    [switch] $Build,
    [switch] $Release,
    [switch] $Deploy,
    [switch] $Stop
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

if($Deploy)
{
    # Read existing
    $counter = [int]([System.Environment]::GetEnvironmentVariable(
        "CUBEY_BUILD_COUNTER",
        [System.EnvironmentVariableTarget]::User
    ) ?? 0)

    # Increment
    $counter++

    Write-Host "New CUBEY_BUILD_COUNTER = $counter"
    Run-Command "./gradlew buildCMakeRelWithDebInfo[arm64-v8a][main]"  "Failed to build native release"
    Run-Command "./gradlew deployAlpha -PversionCode=$counter" "Failed to build release"

    # Save it back
    [System.Environment]::SetEnvironmentVariable(
        "CUBEY_BUILD_COUNTER",
        $counter,
        [System.EnvironmentVariableTarget]::User
    )
}
elseif($Stop)
{
    adb shell am force-stop com.omixlab.cubey
}
else
{
    if ($Build)
    {
        if ($Release)
        {
            Run-Command "./gradlew buildCMakeRelWithDebInfo[arm64-v8a][main]"  "Failed to build native release"
            Run-Command "./gradlew installRelease" "Failed to build release"
        }
        else
        {
            Run-Command "./gradlew buildCMakeDebug[arm64-v8a][main]"  "Failed to build native debug"
            Run-Command "./gradlew installDebug" "Failed to build debug"
        }
        # adb shell pm grant com.omixlab.cubey android.permission.RECORD_AUDIO
    }
    adb shell am force-stop com.omixlab.cubey
    adb shell am start -n com.omixlab.cubey/.MainActivity
    sleep 1
    try
    {
        adb logcat --pid $(adb shell pidof -s com.omixlab.cubey) ChoppyEngine:D *:S
    }
    catch
    {
    }
    finally
    {
        adb shell am force-stop com.omixlab.cubey
    }
}
