<#
.SYNOPSIS
    Switch libdragon's RSP/RDP profiler (RSPQ_PROFILE) on or off.

.DESCRIPTION
    RSPQ_PROFILE is a hard #define in libdragon/include/rspq_constants.h, so
    RSP/RDP timings (the overlay's RSP page, RSP rows in the CSV dump) need a
    libdragon build with it set to 1. "on" applies tools/patches/rspq_profile.patch
    to the submodule; "off" reverts it. Either way libdragon is rebuilt and
    reinstalled into the Docker container, then the project is rebuilt clean.

    The patch also drops libdragon's H.264 video decoder: with profiling hooks
    compiled in, its RSP microcode overflows IMEM by 96 bytes. The engine does
    not play video. Because libdragon's examples need H.264, this script builds
    only the library (make install) instead of `libdragon install` (build.sh).

    The submodule is left modified while profiling is on: do not commit the
    submodule in that state. Run "off" to return to the pristine build.
    See docs/PROFILING.md.

.EXAMPLE
    ./tools/rspq_profile.ps1 on
    ./tools/rspq_profile.ps1 off
    ./tools/rspq_profile.ps1 status
#>
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('on', 'off', 'status')]
    [string]$Mode
)

# Native tools (libdragon CLI, git) write progress to stderr; Windows PowerShell 5.1
# would turn that into errors under 'Stop'. Failures are detected via exit codes.
$ErrorActionPreference = 'Continue'
$root  = Split-Path -Parent $PSScriptRoot
$patch = Join-Path $root 'tools/patches/rspq_profile.patch'
$lib   = Join-Path $root 'libdragon'
$files = @('include/rspq_constants.h', 'src/video/libdragon.mk')

function Get-ProfileState {
    $line = Select-String -Path (Join-Path $lib 'include/rspq_constants.h') -Pattern '^#define RSPQ_PROFILE\s+(\d)' | Select-Object -First 1
    if (-not $line) { throw "RSPQ_PROFILE define not found in rspq_constants.h" }
    return [int]$line.Matches[0].Groups[1].Value
}

function Invoke-Checked([string]$what, [scriptblock]$cmd) {
    Write-Host "==> $what"
    & $cmd
    if ($LASTEXITCODE -ne 0) { throw "$what failed (exit $LASTEXITCODE)" }
}

$state = Get-ProfileState
if ($Mode -eq 'status') {
    Write-Host ("RSPQ_PROFILE = {0} ({1})" -f $state, $(if ($state -eq 1) { 'RSP/RDP profiling ON' } else { 'off, pristine' }))
    exit 0
}

Push-Location $root
try {
    if ($Mode -eq 'on') {
        if ($state -eq 1) { Write-Host 'Patch already applied.' }
        else { Invoke-Checked 'Apply rspq_profile.patch to libdragon' { git -C $lib apply $patch } }
    } else {
        Invoke-Checked 'Revert the patched files in libdragon' { git -C $lib checkout -- @files }
    }

    # Library only (build.sh would also build examples that need H.264)
    Invoke-Checked 'Rebuild + install libdragon in the container (~1 min)' {
        libdragon exec bash -c "cd libdragon && make clobber >/dev/null && make -j`$(nproc) libdragon && make install"
    }
    Invoke-Checked 'libdragon make clean' { libdragon make clean }
    Invoke-Checked 'libdragon make (debug)' { libdragon make }

    Write-Host ("Done. RSPQ_PROFILE = {0}. Upload with: sc64deployer upload engine-debug.z64" -f (Get-ProfileState))
} finally {
    Pop-Location
}
