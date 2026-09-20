[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateScript({ Test-Path -LiteralPath $_ -PathType Leaf })]
    [string]$InputConfig,

    [Parameter(Mandatory = $true)]
    [ValidateScript({ Test-Path -LiteralPath (Split-Path -Parent $_) -PathType Container })]
    [string]$OutputConfig,

    [ValidateScript({ Test-Path -LiteralPath $_ -PathType Leaf })]
    [string]$Preset = (Join-Path $PSScriptRoot '..\presets\wbt2-consumer-to-rshift-fkeys.json')
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (Test-Path -LiteralPath $OutputConfig) {
    throw "OutputConfig already exists: $OutputConfig"
}

$config = Get-Content -LiteralPath $InputConfig -Raw | ConvertFrom-Json
$presetConfig = Get-Content -LiteralPath $Preset -Raw | ConvertFrom-Json

if ($null -eq $config.mappings) {
    throw 'InputConfig does not contain a mappings array.'
}
if ($null -eq $presetConfig.mappings) {
    throw 'Preset does not contain a mappings array.'
}

function Get-MappingKey($mapping) {
    $layers = @($mapping.layers) -join ','
    return @(
        [string]$mapping.target_usage,
        [string]$mapping.source_usage,
        [string]$mapping.scaling,
        $layers,
        [string]$mapping.sticky,
        [string]$mapping.tap,
        [string]$mapping.hold,
        [string]$mapping.source_port,
        [string]$mapping.target_port
    ) -join '|'
}

$mappings = @($config.mappings)
$existing = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
foreach ($mapping in $mappings) {
    [void]$existing.Add((Get-MappingKey $mapping))
}

$added = 0
foreach ($mapping in @($presetConfig.mappings)) {
    $key = Get-MappingKey $mapping
    if ($existing.Add($key)) {
        $mappings += $mapping
        $added++
    }
}

$config.mappings = @($mappings)
$json = $config | ConvertTo-Json -Depth 16
[System.IO.File]::WriteAllText(
    [System.IO.Path]::GetFullPath($OutputConfig),
    $json + [System.Environment]::NewLine,
    [System.Text.UTF8Encoding]::new($false)
)

Write-Host "Wrote $OutputConfig ($added mapping(s) added; $($mappings.Count) total)."
