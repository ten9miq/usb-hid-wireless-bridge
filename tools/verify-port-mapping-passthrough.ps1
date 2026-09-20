$ErrorActionPreference = 'Stop'

$sourcePath = Join-Path $PSScriptRoot '..\firmware\hid-remapper\firmware\src\remapper.cc'
$source = Get-Content -Raw $sourcePath

function Get-UnmappedLayers([byte] $passthrough, [byte] $globalMapped, [byte] $portMapped) {
    return [byte]($passthrough -band (-bnot ($globalMapped -bor $portMapped)))
}

function Get-PassthroughStatePort($specificPorts, [byte] $hubPort) {
    if ($null -eq $specificPorts) {
        return $hubPort
    }
    if (($hubPort -ge 1) -and ($hubPort -le 15) -and (($specificPorts -band (1 -shl $hubPort)) -ne 0)) {
        return $hubPort
    }
    return 255
}

function Assert-Equal($actual, $expected, [string] $message) {
    if ($actual -ne $expected) {
        throw "$message (expected $expected, got $actual)"
    }
}

# A mapping on hub port 4 suppresses only that port.  Port 3 and direct input
# retain their independent passthrough layer masks.
$passthrough = [byte]0x0f
Assert-Equal (Get-UnmappedLayers $passthrough 0 0x0f) 0 'port 4 mapping must suppress port 4'
Assert-Equal (Get-UnmappedLayers $passthrough 0 0) 0x0f 'port 3 must pass through when only port 4 is mapped'
Assert-Equal (Get-UnmappedLayers $passthrough 0 0) 0x0f 'direct input must pass through when only port 4 is mapped'
$port4Mask = [uint16](1 -shl 4)
Assert-Equal (Get-PassthroughStatePort $port4Mask 4) 4 'port 4 must update its specific state'
Assert-Equal (Get-PassthroughStatePort $port4Mask 3) 255 'port 3 must update the non-specific catch-all state'
Assert-Equal (Get-PassthroughStatePort $port4Mask 255) 255 'direct input must update the catch-all state'

# A port-0 mapping is global and suppresses every source state.
foreach ($portMapped in @(0, 0x0f)) {
    Assert-Equal (Get-UnmappedLayers $passthrough 0x0f $portMapped) 0 'port 0 mapping must suppress every port'
}

# Usages without a port-specific mapping keep the old aggregate port-0 state,
# so their per-port lookup remains unchanged and finds no extra state.
Assert-Equal (Get-PassthroughStatePort $null 3) 3 'usage without a specific mapping must retain its aggregate route'

foreach ($required in @(
    'inline uint64_t port_usage_key',
    'inline uint8_t unmapped_passthrough_layers',
    'inline uint8_t passthrough_state_port',
    'std::unordered_map<uint32_t, uint16_t> passthrough_specific_source_ports',
    'passthrough_specific_source_ports.clear()',
    'mapped_on_layers[port_usage_key(mapping.source_usage, source_port)] |= layer_mask',
    'std::unordered_set<uint32_t> seen_passthrough_usages',
    'if (specific_ports & (1u << hub_port))',
    'for (uint8_t hub_port = 1; hub_port <= NPORTS; hub_port++)',
    '.orig_source_port = orig_source_port',
    'assign_state_slot(usage, HUB_PORT_NONE, false)',
    'uint8_t state_hub_port = passthrough_state_port(usage, hub_port)',
    'set_input_state(usage, raw_val, scaled_val, passthrough_state_port(usage, hub_port))',
    'int32_t* state_ptr_n = get_state_ptr(actual_usage, passthrough_state_port(actual_usage, hub_port))',
    '*state_ptr_n |= 1u << interface_idx'
)) {
    if (-not $source.Contains($required)) {
        throw "Required port-specific passthrough implementation is missing: $required"
    }
}

if ($source.Contains('for (uint8_t hub_port = 1; hub_port <= NPORTS; hub_port++) {' + [Environment]::NewLine + '                add_port_passthrough(hub_port, hub_port);')) {
    throw 'Passthrough still allocates state for every hub port.'
}

Write-Host 'Port mapping passthrough static regression checks passed.'
