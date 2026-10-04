param([string]$Exe = (Join-Path $PSScriptRoot '../release/CodexAccountSwitcher.exe'))
$ErrorActionPreference = 'Stop'
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class SwitcherWindowTest {
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr hwnd);
    [DllImport("user32.dll", EntryPoint="GetClassLongPtrW")] public static extern IntPtr GetClassLongPtr(IntPtr hwnd, int index);
    [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr hwnd, int id);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr hwnd, uint msg, IntPtr wp, IntPtr lp);
    [DllImport("user32.dll")] public static extern IntPtr SendMessageTimeout(IntPtr hwnd, uint msg, IntPtr wp, IntPtr lp, uint flags, uint timeout, out IntPtr result);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint pid);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string cls, string title);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hwnd, int cmd);
}
'@
function Send-TestMessage($taskHwnd, [uint32]$taskMsg, [int]$taskWp = 0) {
    $taskResult = [IntPtr]::Zero
    if ([SwitcherWindowTest]::SendMessageTimeout($taskHwnd, $taskMsg, [IntPtr]$taskWp, [IntPtr]::Zero, 2, 3000, [ref]$taskResult) -eq [IntPtr]::Zero) { throw 'UI message timed out.' }
    return $taskResult.ToInt64()
}
function Wait-TestCondition([scriptblock]$taskCondition) {
    for ($taskAttempt = 0; $taskAttempt -lt 50; $taskAttempt++) {
        if (& $taskCondition) { return }
        Start-Sleep -Milliseconds 100
    }
    throw "Window condition timed out: $taskCondition"
}
$taskExe = (Resolve-Path -LiteralPath $Exe).Path
$taskTemp = Join-Path ([IO.Path]::GetTempPath()) ('CodexSwitcher-window-' + [guid]::NewGuid())
New-Item -ItemType Directory -Path $taskTemp | Out-Null
$taskClass = 'CodexAccountSwitcher.' + (Split-Path $taskTemp -Leaf)
$taskRegistry = 'HKCU:\Software\CodexAccountSwitcher\UITests\' + (Split-Path $taskTemp -Leaf)
$taskProc = $null
$taskDuplicate = $null
function Start-TestProcess([string]$taskArguments) {
    $taskInfo = [Diagnostics.ProcessStartInfo]::new()
    $taskInfo.FileName = $taskExe; $taskInfo.Arguments = '--ui-test ' + $taskArguments
    $taskInfo.UseShellExecute = $false
    $taskInfo.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
    $taskInfo.Environment['LOCALAPPDATA'] = $taskTemp
    $taskInfo.Environment['USERPROFILE'] = $taskTemp
    return [Diagnostics.Process]::Start($taskInfo)
}
try {
    if ([SwitcherWindowTest]::FindWindow($taskClass, 'Codex 账户切换工具') -ne [IntPtr]::Zero) { throw 'Close the existing switcher before this isolated test.' }
    $taskProc = Start-TestProcess '--tray'
    $taskHwnd = [IntPtr]::Zero
    Wait-TestCondition { $script:taskHwnd = [SwitcherWindowTest]::FindWindow($taskClass, 'Codex 账户切换工具'); $script:taskHwnd -ne [IntPtr]::Zero }
    $taskPid = [uint32]0
    [void][SwitcherWindowTest]::GetWindowThreadProcessId($taskHwnd, [ref]$taskPid)
    if ($taskPid -ne $taskProc.Id) { throw 'Unexpected test window owner.' }
    Start-Sleep -Milliseconds 300
    if ([SwitcherWindowTest]::IsWindowVisible($taskHwnd)) { throw '--tray did not hide the window.' }
    # A second launch must reveal the existing instance and exit.
    $taskDuplicate = Start-TestProcess ''
    if (!$taskDuplicate.WaitForExit(5000)) { throw 'Second instance did not exit.' }
    Wait-TestCondition { [SwitcherWindowTest]::IsWindowVisible($taskHwnd) }
    $taskOption = [SwitcherWindowTest]::GetDlgItem($taskHwnd, 109)
    $taskStartup = [SwitcherWindowTest]::GetDlgItem($taskHwnd, 108)
    [void](Send-TestMessage $taskStartup 0xF5)
    $taskCommand = (Get-ItemProperty -LiteralPath ($taskRegistry + '\Run')).CodexAccountSwitcher
    if ($taskCommand -ne ('"' + $taskExe + '" --tray')) { throw 'Startup command is incorrect.' }
    if ((Send-TestMessage $taskStartup 0xF0) -ne 1) { throw 'Startup checkbox was not enabled.' }
    [void](Send-TestMessage $taskStartup 0xF5)
    if ((Get-Item -LiteralPath ($taskRegistry + '\Run')).GetValue('CodexAccountSwitcher', $null)) { throw 'Startup value was not removed.' }
    if ((Send-TestMessage $taskOption 0xF0) -ne 1) { throw 'Minimize-to-tray default is not enabled.' }
    if ([SwitcherWindowTest]::GetClassLongPtr($taskHwnd, -14) -eq [IntPtr]::Zero) { throw 'Application icon is missing.' }
    [void][SwitcherWindowTest]::ShowWindow($taskHwnd, 6)
    Wait-TestCondition { ![SwitcherWindowTest]::IsWindowVisible($taskHwnd) }
    [void](Send-TestMessage $taskHwnd 0x8003)
    Wait-TestCondition { [SwitcherWindowTest]::IsWindowVisible($taskHwnd) }
    [void](Send-TestMessage $taskHwnd 0x10)
    Wait-TestCondition { ![SwitcherWindowTest]::IsWindowVisible($taskHwnd) }
    if ($taskProc.HasExited) { throw 'Close exited instead of hiding to tray.' }
    [void](Send-TestMessage $taskHwnd 0x8003)
    Wait-TestCondition { [SwitcherWindowTest]::IsWindowVisible($taskHwnd) }
    [void](Send-TestMessage $taskOption 0xF5)
    $taskSettings = Join-Path $taskTemp 'CodexAccountSwitcher/profiles/settings.json'
    $taskSaved = Get-Content -LiteralPath $taskSettings -Raw | ConvertFrom-Json
    if ($taskSaved.minimize_to_tray -ne $false) { throw 'Tray option was not saved.' }
    [void][SwitcherWindowTest]::ShowWindow($taskHwnd, 6)
    Wait-TestCondition { [SwitcherWindowTest]::IsWindowVisible($taskHwnd) -and [SwitcherWindowTest]::IsIconic($taskHwnd) }
    [void](Send-TestMessage $taskHwnd 0x10)
    Wait-TestCondition { ![SwitcherWindowTest]::IsWindowVisible($taskHwnd) }
    if ($taskProc.HasExited) { throw 'Close must hide to tray even when minimize-to-tray is disabled.' }
    [void](Send-TestMessage $taskHwnd 0x111 111)
    if (!$taskProc.WaitForExit(5000)) { throw 'Tray exit did not exit.' }
    $taskProc.Dispose(); $taskProc = Start-TestProcess ''
    $taskHwnd = [IntPtr]::Zero
    Wait-TestCondition { $script:taskHwnd = [SwitcherWindowTest]::FindWindow($taskClass, 'Codex 账户切换工具'); $script:taskHwnd -ne [IntPtr]::Zero }
    $taskOption = [SwitcherWindowTest]::GetDlgItem($taskHwnd, 109)
    Wait-TestCondition { (Send-TestMessage $taskOption 0xF0) -eq 0 }
    [void](Send-TestMessage $taskHwnd 0x10)
    Wait-TestCondition { ![SwitcherWindowTest]::IsWindowVisible($taskHwnd) }
    if ($taskProc.HasExited) { throw 'Close exited after restarting with the saved preference.' }
    [void](Send-TestMessage $taskHwnd 0x111 111)
    if (!$taskProc.WaitForExit(5000)) { throw 'Final tray exit did not exit.' }
    Write-Host 'PASS: icon, --tray, second-instance restore, minimize behavior, close-to-tray with either preference, settings persistence, startup registration and removal, and tray exit. No credentials or real startup registry values changed.'
} finally {
    foreach ($taskProcess in @($taskDuplicate, $taskProc)) {
        if ($taskProcess) {
            if (!$taskProcess.HasExited) { $taskProcess.Kill(); $taskProcess.WaitForExit() }
            $taskProcess.Dispose()
        }
    }
    $taskResolved = [IO.Path]::GetFullPath($taskTemp)
    $taskExpectedRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
    if (!$taskResolved.StartsWith($taskExpectedRoot, [StringComparison]::OrdinalIgnoreCase) -or (Split-Path $taskResolved -Leaf) -notlike 'CodexSwitcher-window-*') { throw 'Unexpected cleanup path.' }
    Remove-Item -LiteralPath $taskResolved -Recurse -Force
    if (Test-Path -LiteralPath $taskRegistry) { Remove-Item -LiteralPath $taskRegistry -Recurse -Force }
}
