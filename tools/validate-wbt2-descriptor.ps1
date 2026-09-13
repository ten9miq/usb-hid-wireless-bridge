[CmdletBinding()]
param(
    [string]$SourcePath = (Join-Path $PSScriptRoot '..\firmware\hid-remapper\firmware\src\our_descriptor.cc')
)

$source = Get-Content -LiteralPath $SourcePath -Raw
$match = [regex]::Match(
    $source,
    'const\s+uint8_t\s+our_report_descriptor_kb_mouse\[\]\s*=\s*\{(?<body>.*?)\n\};',
    [Text.RegularExpressions.RegexOptions]::Singleline
)
if (-not $match.Success) {
    throw 'our_report_descriptor_kb_mouse was not found'
}

$withoutComments = ($match.Groups['body'].Value -split "`r?`n" | ForEach-Object {
    $_ -replace '//.*$', ''
}) -join "`n"

$reportIds = @{
    REPORT_ID_MOUSE = 1
    REPORT_ID_KEYBOARD = 2
    REPORT_ID_CONSUMER = 3
    REPORT_ID_LEDS = 98
    REPORT_ID_MULTIPLIER = 99
}

$bytes = [Collections.Generic.List[byte]]::new()
foreach ($tokenMatch in [regex]::Matches($withoutComments, '0x[0-9A-Fa-f]+|REPORT_ID_[A-Z_]+')) {
    $token = $tokenMatch.Value
    if ($token.StartsWith('0x')) {
        $bytes.Add([Convert]::ToByte($token.Substring(2), 16))
    } elseif ($reportIds.ContainsKey($token)) {
        $bytes.Add([byte]$reportIds[$token])
    } else {
        throw "Unknown descriptor token: $token"
    }
}

$reportId = 0
$reportSize = 0
$reportCount = 0
$inputBits = @{}
$outputBits = @{}
$featureBits = @{}
$keyboardArray = $null
$usagePage = 0
$usageMinimum = 0
$usageMaximum = 0
$logicalMinimum = 0
$logicalMaximum = 0

for ($index = 0; $index -lt $bytes.Count;) {
    $prefix = $bytes[$index++]
    if ($prefix -eq 0xFE) {
        throw 'Long HID items are not supported by this validator'
    }

    $itemSize = $prefix -band 0x03
    if ($itemSize -eq 3) {
        $itemSize = 4
    }
    if (($index + $itemSize) -gt $bytes.Count) {
        throw 'Truncated HID item'
    }

    $value = 0
    for ($byteIndex = 0; $byteIndex -lt $itemSize; $byteIndex++) {
        $value = $value -bor ([int]$bytes[$index++] -shl (8 * $byteIndex))
    }

    switch ($prefix -band 0xFC) {
        0x04 { $usagePage = $value }
        0x14 { $logicalMinimum = $value }
        0x24 { $logicalMaximum = $value }
        0x18 { $usageMinimum = $value }
        0x28 { $usageMaximum = $value }
        0x74 { $reportSize = $value }
        0x84 { $reportId = $value }
        0x94 { $reportCount = $value }
        0x80 {
            $inputBits[$reportId] = [int]$inputBits[$reportId] + $reportSize * $reportCount
            if (($reportId -eq 2) -and (($value -band 0x03) -eq 0) -and ($reportSize -eq 8) -and ($reportCount -eq 6)) {
                $keyboardArray = [pscustomobject]@{
                    InputFlags = $value
                    UsagePage = $usagePage
                    UsageMinimum = $usageMinimum
                    UsageMaximum = $usageMaximum
                    LogicalMinimum = $logicalMinimum
                    LogicalMaximum = $logicalMaximum
                }
            }
            $usageMinimum = 0
            $usageMaximum = 0
        }
        0x90 { $outputBits[$reportId] = [int]$outputBits[$reportId] + $reportSize * $reportCount }
        0xB0 { $featureBits[$reportId] = [int]$featureBits[$reportId] + $reportSize * $reportCount }
    }
}

$expectedInputBits = @{ 1 = 32; 2 = 64; 3 = 8 }
foreach ($id in $expectedInputBits.Keys) {
    if ($inputBits[$id] -ne $expectedInputBits[$id]) {
        throw "Report ID $id input size is $($inputBits[$id]) bits; expected $($expectedInputBits[$id])"
    }
}
if ($inputBits.Count -ne $expectedInputBits.Count) {
    throw "Unexpected input report IDs: $($inputBits.Keys -join ', ')"
}
if (($outputBits.Count -ne 1) -or ($outputBits[98] -ne 8)) {
    throw 'LED output report must be exactly 8 bits on Report ID 98'
}
if ($featureBits.Count -ne 0) {
    throw "The simplified descriptor must not contain feature reports: $($featureBits.Keys -join ', ')"
}
if ($null -eq $keyboardArray) {
    throw 'The six-key keyboard array was not found on Report ID 2'
}
if (($keyboardArray.InputFlags -ne 0x40) -or
    ($keyboardArray.UsagePage -ne 0x07) -or
    ($keyboardArray.UsageMinimum -ne 0x04) -or
    ($keyboardArray.UsageMaximum -lt 0x63) -or
    ($keyboardArray.LogicalMinimum -ne $keyboardArray.UsageMinimum) -or
    ($keyboardArray.LogicalMaximum -ne $keyboardArray.UsageMaximum)) {
    throw "Keyboard array range is not a direct usage mapping: $($keyboardArray | ConvertTo-Json -Compress)"
}

[pscustomobject]@{
    DescriptorBytes = $bytes.Count
    Keyboard6KROBytes = $inputBits[2] / 8
    SimpleMouseBytes = $inputBits[1] / 8
    ConsumerBytes = $inputBits[3] / 8
    LedOutputBytes = $outputBits[98] / 8
    KeyboardUsageMinimum = ('0x{0:X2}' -f $keyboardArray.UsageMinimum)
    KeyboardUsageMaximum = ('0x{0:X2}' -f $keyboardArray.UsageMaximum)
    MouseWireBytes = 1 + $inputBits[1] / 8
}
