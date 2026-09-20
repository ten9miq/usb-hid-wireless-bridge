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

$expectedInputBits = @{ 1 = 32; 2 = 64; 3 = 16 }
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

function Get-IdlessDescriptorBytes([string]$Name) {
    $descriptorMatch = [regex]::Match(
        $source,
        "(?:const\s+uint8_t|uint8_t\s+const)\s+$Name\[\]\s*=\s*\{(?<body>.*?)\n\};",
        [Text.RegularExpressions.RegexOptions]::Singleline
    )
    if (-not $descriptorMatch.Success) {
        throw "$Name was not found"
    }

    $descriptorBody = ($descriptorMatch.Groups['body'].Value -split "`r?`n" | ForEach-Object {
        $_ -replace '//.*$', ''
    }) -join "`n"
    if ($descriptorBody -match 'REPORT_ID_|0x85\s*,') {
        throw "$Name must not contain a Report ID item"
    }

    $descriptorBytes = [Collections.Generic.List[byte]]::new()
    foreach ($hexMatch in [regex]::Matches($descriptorBody, '0x[0-9A-Fa-f]+')) {
        $descriptorBytes.Add([Convert]::ToByte($hexMatch.Value.Substring(2), 16))
    }
    return $descriptorBytes.ToArray()
}

function ConvertTo-HexSequence([byte[]]$DescriptorBytes) {
    return (($DescriptorBytes | ForEach-Object { '{0:X2}' -f $_ }) -join ' ')
}

$bootKeyboardBytes = Get-IdlessDescriptorBytes 'boot_kb_report_descriptor'
$bootKeyboardHex = ConvertTo-HexSequence $bootKeyboardBytes
$expectedKeyboardArray = '05 07 19 00 29 65 15 00 25 65 75 08 95 06 81 00'
if (-not $bootKeyboardHex.Contains($expectedKeyboardArray)) {
    throw 'Boot Keyboard array must use matching Usage/Logical ranges 0x00-0x65 and six 8-bit slots'
}
$expectedKeyboardLeds = '05 08 19 01 29 05 15 00 25 01 75 01 95 05 91 02 95 01 75 03 91 03'
if (-not $bootKeyboardHex.Contains($expectedKeyboardLeds)) {
    throw 'Boot Keyboard output must expose five LED bits followed by three padding bits'
}

$bootMouseBytes = Get-IdlessDescriptorBytes 'boot_mouse_report_descriptor'
$bootMouseHex = ConvertTo-HexSequence $bootMouseBytes
$expectedMouseButtons = '05 09 19 01 29 05 15 00 25 01 95 05 75 01 81 02 95 01 75 03 81 03'
if (-not $bootMouseHex.Contains($expectedMouseButtons)) {
    throw 'Boot Mouse interface must expose Buttons 1-5 followed by three padding bits'
}
$expectedMouseAxes = '09 30 09 31 09 38 15 81 25 7F 75 08 95 03 81 06'
if (-not $bootMouseHex.Contains($expectedMouseAxes)) {
    throw 'Boot Mouse interface must expose signed 8-bit X, Y, and Wheel fields'
}

$consumerBytes = Get-IdlessDescriptorBytes 'consumer_report_descriptor'
$consumerHex = ConvertTo-HexSequence $consumerBytes
foreach ($requiredUsage in @('09 B1', '0A 92 01', '0A 23 02', '0A 8A 01', '0A 83 01')) {
    if (-not $consumerHex.Contains($requiredUsage)) {
        throw "Consumer interface must expose usage $requiredUsage"
    }
}
if (-not $consumerHex.Contains('75 01 95 0C 81 02')) {
    throw 'Consumer interface must expose twelve one-bit Consumer usages'
}
if (-not $consumerHex.Contains('05 0B 09 2F 95 01 81 02 95 03 81 03')) {
    throw 'Consumer interface must retain a 16-bit payload with three padding bits'
}

$combinedHex = ConvertTo-HexSequence $bytes.ToArray()
if (-not $combinedHex.Contains($expectedMouseButtons)) {
    throw 'Combined keyboard/mouse descriptor must expose Buttons 1-5 followed by three padding bits'
}
foreach ($requiredUsage in @('0A 23 02', '0A 8A 01', '0A 83 01')) {
    if (-not $combinedHex.Contains($requiredUsage)) {
        throw "Combined keyboard/mouse descriptor must expose usage $requiredUsage"
    }
}
if (-not $combinedHex.Contains('0A 83 01 75 01 95 0C 81 02')) {
    throw 'Combined keyboard/mouse descriptor must expose twelve one-bit Consumer usages'
}
if (-not $combinedHex.Contains('05 0B 09 2F 95 01 81 02 95 03 81 03')) {
    throw 'Combined keyboard/mouse descriptor must retain a 16-bit Consumer payload'
}

$mainSource = Get-Content -LiteralPath (Join-Path (Split-Path $SourcePath) 'main.cc') -Raw
if ($mainSource -notmatch 'tud_hid_n_report\(interface,\s*0,\s*report_with_id\s*\+\s*1,\s*len\s*-\s*1\)') {
    throw 'The split interfaces must send their complete payload without a Report ID'
}

$tinyUsbSource = Get-Content -LiteralPath (Join-Path (Split-Path $SourcePath) 'tinyusb_stuff.cc') -Raw
if (($tinyUsbSource -notmatch 'keyboard_led_state\s*=\s*buffer\[0\]\s*&\s*0x1F') -or
    ($tinyUsbSource -notmatch 'handle_set_report0\(REPORT_ID_LEDS,\s*&keyboard_led_state,\s*1\)') -or
    ($tinyUsbSource -notmatch 'buffer\[0\]\s*=\s*keyboard_led_state')) {
    throw 'The A-side keyboard interface must cache, normalize, forward, and return its LED state'
}

$descriptorParserSource = Get-Content -LiteralPath (Join-Path (Split-Path $SourcePath) 'descriptor_parser.cc') -Raw
if (($descriptorParserSource -notmatch 'malformed_one_bit_array') -or
    ($descriptorParserSource -notmatch 'report_size\s*==\s*1') -or
    ($descriptorParserSource -notmatch 'unsigned_logical_maximum\s*==\s*1') -or
    ($descriptorParserSource -notmatch 'usage_maximum\s*-\s*usage_minimum\s*\+\s*1\s*==\s*report_count')) {
    throw 'Malformed one-bit NKRO Array inputs must be normalized from bitmap structure'
}

$remapperSource = Get-Content -LiteralPath (Join-Path (Split-Path $SourcePath) 'remapper.cc') -Raw
if (($remapperSource -notmatch 'NumLockSyncStage::WAIT_KEYPAD') -or
    ($remapperSource -notmatch 'NumLockSyncStage::KEYPAD_ACTIVE') -or
    ($remapperSource -notmatch 'NumLockSyncStage::WAIT_TRAILING_DOWN') -or
    ($remapperSource -notmatch 'NumLockSyncStage::TRAILING_DOWN') -or
    ($remapperSource -notmatch 'replay_first_num_lock_tap') -or
    ($remapperSource -notmatch 'clear_report_usage\(filtered_report,\s*len,\s*usage_map,\s*NUM_LOCK_USAGE\)')) {
    throw 'The input path must suppress only complete NumLock/Keypad/NumLock synchronization sequences'
}

$dualBSource = Get-Content -LiteralPath (Join-Path (Split-Path $SourcePath) 'remapper_dual_b.cc') -Raw
if ($dualBSource -notmatch 'tuh_hid_set_default_protocol\(HID_PROTOCOL_REPORT\);\s*tusb_init\(\);') {
    throw 'The B-side host must select HID Report protocol before TinyUSB initialization'
}

[pscustomobject]@{
    DescriptorBytes = $bytes.Count
    Keyboard6KROBytes = $inputBits[2] / 8
    SimpleMouseBytes = $inputBits[1] / 8
    ConsumerBytes = $inputBits[3] / 8
    LedOutputBytes = $outputBits[98] / 8
    KeyboardUsageMinimum = ('0x{0:X2}' -f $keyboardArray.UsageMinimum)
    KeyboardUsageMaximum = ('0x{0:X2}' -f $keyboardArray.UsageMaximum)
    CompositeMouseWireBytes = 1 + $inputBits[1] / 8
    BootKeyboardWireBytes = 8
    BootKeyboardUsageMaximum = '0x65'
    BootMouseWireBytes = 4
    BootMouseHasWheel = $true
    MouseButtons = 5
    MousePaddingBits = 3
    InputHostProtocol = 'Report'
    KeyboardLedBits = 5
    KeyboardLedStateCached = $true
    MalformedNkroBitmapNormalized = $true
    NumLockKeypadWrapperFiltered = $true
}
