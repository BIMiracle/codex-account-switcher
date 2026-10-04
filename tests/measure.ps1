param([string]$Exe = (Join-Path $PSScriptRoot '../release/CodexAccountSwitcher.exe'))
$ErrorActionPreference = 'Stop'
$taskTemp = Join-Path ([IO.Path]::GetTempPath()) ('CodexSwitcher-measure-' + [guid]::NewGuid())
New-Item -ItemType Directory -Path $taskTemp | Out-Null
$taskProc = $null
try {
    $taskInfo = [Diagnostics.ProcessStartInfo]::new()
    $taskInfo.FileName = (Resolve-Path -LiteralPath $Exe).Path
    $taskInfo.UseShellExecute = $false
    $taskInfo.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
    $taskInfo.Environment['LOCALAPPDATA'] = $taskTemp
    $taskInfo.Environment['USERPROFILE'] = $taskTemp
    $taskProc = [Diagnostics.Process]::Start($taskInfo)
    Start-Sleep -Seconds 2
    $taskProc.Refresh()
    if ($taskProc.HasExited) { throw 'Isolated UI process exited unexpectedly.' }
    $taskCpuStart = $taskProc.TotalProcessorTime.TotalMilliseconds
    $taskWatch = [Diagnostics.Stopwatch]::StartNew()
    $taskPrivate = 0L
    $taskWorking = 0L
    1..20 | ForEach-Object {
        Start-Sleep -Milliseconds 500
        $taskProc.Refresh()
        $taskPrivate = [Math]::Max($taskPrivate, $taskProc.PrivateMemorySize64)
        $taskWorking = [Math]::Max($taskWorking, $taskProc.WorkingSet64)
    }
    $taskWatch.Stop()
    [pscustomobject]@{
        DurationSeconds = [Math]::Round($taskWatch.Elapsed.TotalSeconds, 3)
        PrivatePeakMiB = [Math]::Round($taskPrivate / 1MB, 3)
        WorkingSetPeakMiB = [Math]::Round($taskWorking / 1MB, 3)
        CpuDeltaMs = $taskProc.TotalProcessorTime.TotalMilliseconds - $taskCpuStart
        ExeBytes = (Get-Item -LiteralPath $taskInfo.FileName).Length
    } | ConvertTo-Json
}
finally {
    if ($taskProc -and !$taskProc.HasExited) { $taskProc.Kill(); $taskProc.WaitForExit() }
    if ($taskProc) { $taskProc.Dispose() }
    $taskResolved = [IO.Path]::GetFullPath($taskTemp)
    $taskExpectedRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
    if (!$taskResolved.StartsWith($taskExpectedRoot, [StringComparison]::OrdinalIgnoreCase) -or (Split-Path $taskResolved -Leaf) -notlike 'CodexSwitcher-measure-*') { throw 'Unexpected cleanup path.' }
    Remove-Item -LiteralPath $taskResolved -Recurse -Force
}
