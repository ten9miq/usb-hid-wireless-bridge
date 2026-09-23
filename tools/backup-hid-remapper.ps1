[CmdletBinding()]
param(
    [Parameter(Mandatory = $false)]
    [string]$OutputDirectory = (Join-Path (Join-Path (Get-Location) 'backups') ('backups_' + (Get-Date -Format 'yyyyMMdd_HHmmss')))
)

$ErrorActionPreference = 'Stop'

$localPicotool = Join-Path $PSScriptRoot 'picotool\picotool.exe'
$picotool = if (Test-Path -LiteralPath $localPicotool) {
    Get-Item -LiteralPath $localPicotool
} else {
    Get-Command picotool -ErrorAction SilentlyContinue
}
if ($null -eq $picotool) {
    throw 'tools\picotool\picotool.exe または PATH 上の picotool が見つかりません。'
}

if (Test-Path -LiteralPath $OutputDirectory) {
    throw "バックアップ先は既に存在します: $OutputDirectory"
}
New-Item -ItemType Directory -Path $OutputDirectory | Out-Null
$prefix = Join-Path $OutputDirectory 'hid-remapper-v5.1-original'

& $picotool.FullName info -a | Tee-Object -FilePath "$prefix-info.txt"
if ($LASTEXITCODE -ne 0) { throw "picotool info failed with exit code $LASTEXITCODE" }

& $picotool.FullName save -a -v "$prefix.uf2"
if ($LASTEXITCODE -ne 0) { throw "picotool save (UF2) failed with exit code $LASTEXITCODE" }

& $picotool.FullName save -a -v "$prefix.bin"
if ($LASTEXITCODE -ne 0) { throw "picotool save (BIN) failed with exit code $LASTEXITCODE" }

Write-Host "バックアップを保存しました: $OutputDirectory"
