param(
    [string]$ReleaseDirectory = "$(Join-Path $PSScriptRoot '..\build\bin\Release')",
    [ValidateRange(5, 200)]
    [int]$TrialCount = 30,
    [ValidateRange(1, 300)]
    [int]$IdleSeconds = 60,
    [ValidateRange(100, 10000)]
    [int]$TrialTimeoutMilliseconds = 2000
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;

public static class QingYingAcceptanceNative
{
    private delegate bool EnumWindowsCallback(IntPtr window, IntPtr context);

    [DllImport("user32.dll")]
    private static extern bool EnumWindows(EnumWindowsCallback callback,
                                            IntPtr context);

    [DllImport("user32.dll")]
    private static extern uint GetWindowThreadProcessId(IntPtr window,
                                                        out uint processId);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern int GetClassNameW(IntPtr window, StringBuilder name,
                                            int maximumCount);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern IntPtr SendMessageTimeoutW(
        IntPtr window, uint message, UIntPtr wParam, IntPtr lParam,
        uint flags, uint timeoutMilliseconds, out UIntPtr result);

    [DllImport("user32.dll")]
    private static extern bool PostMessageW(IntPtr window, uint message,
                                            UIntPtr wParam, IntPtr lParam);

    [DllImport("user32.dll")]
    public static extern uint GetGuiResources(IntPtr process, uint flags);

    public static IntPtr FindWindow(uint processId, string className)
    {
        IntPtr found = IntPtr.Zero;
        EnumWindows(delegate(IntPtr window, IntPtr context)
        {
            uint candidateProcessId;
            GetWindowThreadProcessId(window, out candidateProcessId);
            if (candidateProcessId != processId)
            {
                return true;
            }
            StringBuilder name = new StringBuilder(128);
            GetClassNameW(window, name, name.Capacity);
            if (String.Equals(name.ToString(), className,
                              StringComparison.Ordinal))
            {
                found = window;
                return false;
            }
            return true;
        }, IntPtr.Zero);
        return found;
    }

    public static bool PostCaptureHotkey(IntPtr trayWindow)
    {
        const uint wmHotkey = 0x0312;
        const uint captureHotkeyId = 1;
        return PostMessageW(trayWindow, wmHotkey,
                            new UIntPtr(captureHotkeyId), IntPtr.Zero);
    }

    public static bool QueryFirstFrame(IntPtr window, uint message,
                                       uint timeoutMilliseconds)
    {
        UIntPtr result;
        IntPtr sent = SendMessageTimeoutW(
            window, message, UIntPtr.Zero, IntPtr.Zero, 0x0002,
            timeoutMilliseconds, out result);
        return sent != IntPtr.Zero && result.ToUInt64() == 1;
    }

    public static bool CloseWindow(IntPtr window)
    {
        return PostMessageW(window, 0x0010, UIntPtr.Zero, IntPtr.Zero);
    }
}
'@

function Wait-ForWindow
{
    param(
        [uint32]$ProcessId,
        [string]$ClassName,
        [int]$TimeoutMilliseconds
    )

    $timer = [System.Diagnostics.Stopwatch]::StartNew()
    while ($timer.ElapsedMilliseconds -lt $TimeoutMilliseconds)
    {
        $window = [QingYingAcceptanceNative]::FindWindow($ProcessId,
                                                         $ClassName)
        if ($window -ne [IntPtr]::Zero)
        {
            return $window
        }
        Start-Sleep -Milliseconds 5
    }
    return [IntPtr]::Zero
}

function Wait-ForWindowClosed
{
    param(
        [uint32]$ProcessId,
        [string]$ClassName,
        [int]$TimeoutMilliseconds
    )

    $timer = [System.Diagnostics.Stopwatch]::StartNew()
    while ($timer.ElapsedMilliseconds -lt $TimeoutMilliseconds)
    {
        if ([QingYingAcceptanceNative]::FindWindow($ProcessId, $ClassName) -eq
            [IntPtr]::Zero)
        {
            return $true
        }
        Start-Sleep -Milliseconds 5
    }
    return $false
}

function Invoke-OverlayTrial
{
    param(
        [uint32]$ProcessId,
        [IntPtr]$TrayWindow,
        [int]$TimeoutMilliseconds
    )

    $overlayClass = 'QingYingSelectionOverlay'
    # Keep in sync with WM_QINGYING_SELECTION_OVERLAY_FIRST_FRAME_QUERY in
    # include/qingying/app/app_messages.hpp (WM_APP + 12).
    $firstFrameQuery = 0x800C
    $timer = [System.Diagnostics.Stopwatch]::StartNew()
    if (-not [QingYingAcceptanceNative]::PostCaptureHotkey($TrayWindow))
    {
        throw 'Failed to post the capture hotkey message.'
    }

    $overlay = [IntPtr]::Zero
    $overlayObserved = $false
    while ($timer.ElapsedMilliseconds -lt $TimeoutMilliseconds)
    {
        $overlay = [QingYingAcceptanceNative]::FindWindow($ProcessId,
                                                          $overlayClass)
        if ($overlay -ne [IntPtr]::Zero)
        {
            $overlayObserved = $true
            if ([QingYingAcceptanceNative]::QueryFirstFrame(
                    $overlay, $firstFrameQuery, 50))
            {
                $elapsed = $timer.Elapsed.TotalMilliseconds
                if (-not [QingYingAcceptanceNative]::CloseWindow($overlay))
                {
                    throw 'Failed to close the measured Overlay window.'
                }
                if (-not (Wait-ForWindowClosed -ProcessId $ProcessId `
                        -ClassName $overlayClass `
                        -TimeoutMilliseconds $TimeoutMilliseconds))
                {
                    throw 'Measured Overlay did not close before timeout.'
                }
                return $elapsed
            }
        }
        Start-Sleep -Milliseconds 1
    }
    throw "Overlay first frame exceeded ${TimeoutMilliseconds} ms " +
          "(overlayObserved=$overlayObserved)."
}

$releaseRoot = (Resolve-Path -LiteralPath $ReleaseDirectory).Path
$productExecutable = Join-Path $releaseRoot 'qingying.exe'
$fixtureExecutable = Join-Path $releaseRoot 'qingying_process_fixture.exe'
if (-not (Test-Path -LiteralPath $productExecutable -PathType Leaf) -or
    -not (Test-Path -LiteralPath $fixtureExecutable -PathType Leaf))
{
    throw 'Release binaries are missing. Build the Release configuration first.'
}

$deliveryRelativePaths = @(
    'qingying.exe',
    'plugins\longshot\qingying_browser_longshot_plugin.dll',
    'plugins\longshot\qingying_explorer_longshot_plugin.dll',
    'plugins\longshot\qingying_notepad_longshot_plugin.dll'
)
$packageFiles = @()
foreach ($relativePath in $deliveryRelativePaths)
{
    $deliveryPath = Join-Path $releaseRoot $relativePath
    if (-not (Test-Path -LiteralPath $deliveryPath -PathType Leaf))
    {
        throw "Architecture-defined delivery file is missing: $relativePath"
    }
    $packageFiles += Get-Item -LiteralPath $deliveryPath
}
$packageBytes = ($packageFiles | Measure-Object -Property Length -Sum).Sum

$scope = 'acceptance_{0}_{1}' -f $PID, [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()
$startInfo = New-Object System.Diagnostics.ProcessStartInfo
$startInfo.FileName = $fixtureExecutable
$startInfo.Arguments = "--test-scope=$scope"
$startInfo.UseShellExecute = $false
$startInfo.CreateNoWindow = $true
$fixture = [System.Diagnostics.Process]::Start($startInfo)

try
{
    $tray = Wait-ForWindow -ProcessId ([uint32]$fixture.Id) `
                           -ClassName 'QingYing.TrayHiddenWindow' `
                           -TimeoutMilliseconds 5000
    if ($tray -eq [IntPtr]::Zero)
    {
        throw 'Release acceptance fixture did not initialize its tray window.'
    }
    if ($fixture.HasExited)
    {
        throw "Release acceptance fixture exited with code $($fixture.ExitCode)."
    }

    # Warm up capture allocation and the persistent UIA worker before taking
    # idle/leak baselines. Warm-up latency is intentionally excluded from P95.
    [void](Invoke-OverlayTrial -ProcessId ([uint32]$fixture.Id) `
                              -TrayWindow $tray `
                              -TimeoutMilliseconds $TrialTimeoutMilliseconds)
    Start-Sleep -Seconds $IdleSeconds
    $fixture.Refresh()

    $idleWorkingSetBytes = $fixture.WorkingSet64
    $baselineHandles = $fixture.HandleCount
    $baselineThreads = $fixture.Threads.Count
    $baselineGdi = [QingYingAcceptanceNative]::GetGuiResources(
        $fixture.Handle, 0)
    $baselineUser = [QingYingAcceptanceNative]::GetGuiResources(
        $fixture.Handle, 1)
    if ($baselineGdi -eq 0 -or $baselineUser -eq 0)
    {
        throw 'GetGuiResources failed while collecting the baseline.'
    }

    $latencies = New-Object 'System.Collections.Generic.List[double]'
    for ($trial = 0; $trial -lt $TrialCount; ++$trial)
    {
        $latencies.Add((Invoke-OverlayTrial `
            -ProcessId ([uint32]$fixture.Id) `
            -TrayWindow $tray `
            -TimeoutMilliseconds $TrialTimeoutMilliseconds))
    }

    Start-Sleep -Seconds 2
    $fixture.Refresh()
    $finalHandles = $fixture.HandleCount
    $finalThreads = $fixture.Threads.Count
    $finalGdi = [QingYingAcceptanceNative]::GetGuiResources($fixture.Handle, 0)
    $finalUser = [QingYingAcceptanceNative]::GetGuiResources($fixture.Handle, 1)
    if ($finalGdi -eq 0 -or $finalUser -eq 0)
    {
        throw 'GetGuiResources failed while collecting the final sample.'
    }

    $sorted = @($latencies | Sort-Object)
    $p95Index = [Math]::Ceiling($sorted.Count * 0.95) - 1
    $p95Milliseconds = $sorted[$p95Index]
    $maximumMilliseconds = $sorted[$sorted.Count - 1]
    $averageMilliseconds = ($latencies | Measure-Object -Average).Average

    $result = [pscustomobject]@{
        TrialCount = $TrialCount
        PackageDefinition = 'qingying.exe + three built-in long-shot plugins'
        PackageFileCount = $packageFiles.Count
        PackageBytes = $packageBytes
        PackageMiB = [Math]::Round($packageBytes / 1MB, 3)
        IdleWorkingSetBytes = $idleWorkingSetBytes
        IdleWorkingSetMiB = [Math]::Round($idleWorkingSetBytes / 1MB, 3)
        HotkeyMessageToFirstFrameAverageMs =
            [Math]::Round($averageMilliseconds, 3)
        HotkeyMessageToFirstFrameP95Ms = [Math]::Round($p95Milliseconds, 3)
        HotkeyMessageToFirstFrameMaximumMs =
            [Math]::Round($maximumMilliseconds, 3)
        HandleDelta = $finalHandles - $baselineHandles
        ThreadDelta = $finalThreads - $baselineThreads
        GdiObjectDelta = [int]$finalGdi - [int]$baselineGdi
        UserObjectDelta = [int]$finalUser - [int]$baselineUser
        PackagePass = $packageBytes -le 20MB
        IdleWorkingSetPass = $idleWorkingSetBytes -le 40MB
        FirstFrameP95Pass = $p95Milliseconds -le 300
        ResourceStabilityPass =
            $finalHandles -eq $baselineHandles -and
            $finalThreads -eq $baselineThreads -and
            $finalGdi -eq $baselineGdi -and
            $finalUser -eq $baselineUser
    }
    $result | Format-List

    if (-not ($result.PackagePass -and $result.IdleWorkingSetPass -and
              $result.FirstFrameP95Pass -and
              $result.ResourceStabilityPass))
    {
        exit 1
    }
}
finally
{
    if ($fixture -ne $null -and -not $fixture.HasExited)
    {
        $fixture.Kill()
        [void]$fixture.WaitForExit(5000)
    }
    if ($fixture -ne $null)
    {
        $fixture.Dispose()
    }
}
