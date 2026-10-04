$ErrorActionPreference = 'Stop'
$taskRoot = $PSScriptRoot
& (Join-Path $taskRoot 'build.ps1')
& (Join-Path $taskRoot 'tests/window.ps1')
$taskRelease = Join-Path $taskRoot 'release'
$taskExe = Join-Path $taskRelease 'CodexAccountSwitcher.exe'
$taskVersion = (Get-Item -LiteralPath $taskExe).VersionInfo.ProductVersion
if ($taskVersion -notmatch '^\d+\.\d+\.\d+$') { throw 'Invalid executable version.' }
Copy-Item -LiteralPath (Join-Path $taskRoot 'CHANGELOG.md') -Destination $taskRelease
$taskFiles = @('CodexAccountSwitcher.exe', 'README.md', 'README_EN.md', 'VALIDATION.md', 'CHANGELOG.md', 'LICENSE-json.txt')
$taskPaths = $taskFiles | ForEach-Object { Join-Path $taskRelease $_ }
$taskZip = Join-Path $taskRelease "CodexAccountSwitcher-v$taskVersion-windows-x64.zip"
Compress-Archive -LiteralPath $taskPaths -DestinationPath $taskZip -Force
$taskHashes = @($taskExe, $taskZip) | ForEach-Object {
    $taskHash = Get-FileHash -LiteralPath $_ -Algorithm SHA256
    '{0}  {1}' -f $taskHash.Hash.ToLowerInvariant(), (Split-Path $_ -Leaf)
}
[IO.File]::WriteAllLines((Join-Path $taskRelease 'SHA256SUMS.txt'), $taskHashes, [Text.ASCIIEncoding]::new())
Write-Host "Packaged: $taskZip"
