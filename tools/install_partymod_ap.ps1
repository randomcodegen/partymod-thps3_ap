[CmdletBinding()]
param(
    [string]$GameDirectory = "C:\Program Files (x86)\Activision\Thps3",
    [string]$BuiltDll = "",
    [switch]$Restore
)

$ErrorActionPreference = "Stop"

if (-not $BuiltDll) {
    $BuiltDll = Join-Path $PSScriptRoot "..\build\partymod-x86\Release\partymod.dll"
}

$expectedExeHash = "1b67409414fc37a406d288098232b9947b21cdc16c80e014e580aa386bd57fef"
$expectedOriginalDllHash = "846f2d5abe82c9781e6c2d5cd6cbe12b930286ee0912dd01b25ad5f017848f5a"

$gameExe = Join-Path $GameDirectory "THPS3.exe"
$targetDll = Join-Path $GameDirectory "partymod.dll"
$backupDll = Join-Path $GameDirectory "partymod.1.1.6.original.dll"
$resolvedBuiltDll = [IO.Path]::GetFullPath($BuiltDll)

function Get-Sha256([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Assert-X86Pe([string]$Path) {
    $bytes = [IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 64 -or $bytes[0] -ne 0x4d -or $bytes[1] -ne 0x5a) {
        throw "Not a valid PE file: $Path"
    }
    $peOffset = [BitConverter]::ToInt32($bytes, 0x3c)
    if ($peOffset -lt 0 -or $peOffset + 6 -gt $bytes.Length) {
        throw "Invalid PE header offset: $Path"
    }
    $machine = [BitConverter]::ToUInt16($bytes, $peOffset + 4)
    if ($machine -ne 0x014c) {
        throw ("Expected an x86 DLL, but machine type was 0x{0:x4}: {1}" -f $machine, $Path)
    }
}

if (-not (Test-Path -LiteralPath $gameExe -PathType Leaf)) {
    throw "THPS3.exe was not found in $GameDirectory"
}
if ((Get-Sha256 $gameExe) -ne $expectedExeHash) {
    throw "Refusing unknown THPS3.exe build. Expected the recorded PartyMod 1.1.6 executable."
}

if ($Restore) {
    if (-not (Test-Path -LiteralPath $backupDll -PathType Leaf)) {
        throw "No original PartyMod backup exists at $backupDll"
    }
    if ((Get-Sha256 $backupDll) -ne $expectedOriginalDllHash) {
        throw "The PartyMod backup hash is not the recorded 1.1.6 DLL; refusing to restore it."
    }
    Copy-Item -LiteralPath $backupDll -Destination $targetDll -Force
    Write-Output "Restored the original PartyMod 1.1.6 DLL."
    exit 0
}

if (-not (Test-Path -LiteralPath $resolvedBuiltDll -PathType Leaf)) {
    throw "The combined PartyMod/AP DLL has not been built: $resolvedBuiltDll"
}
Assert-X86Pe $resolvedBuiltDll

if (Test-Path -LiteralPath $backupDll -PathType Leaf) {
    if ((Get-Sha256 $backupDll) -ne $expectedOriginalDllHash) {
        throw "The existing PartyMod backup has an unexpected hash; refusing to overwrite anything."
    }
} else {
    if (-not (Test-Path -LiteralPath $targetDll -PathType Leaf)) {
        throw "partymod.dll was not found in $GameDirectory"
    }
    if ((Get-Sha256 $targetDll) -ne $expectedOriginalDllHash) {
        throw "The installed PartyMod DLL is not the recorded 1.1.6 build and no trusted backup exists."
    }
    Copy-Item -LiteralPath $targetDll -Destination $backupDll
}

Copy-Item -LiteralPath $resolvedBuiltDll -Destination $targetDll -Force
if ((Get-Sha256 $targetDll) -ne (Get-Sha256 $resolvedBuiltDll)) {
    throw "The installed DLL failed post-copy hash verification."
}

$builtHash = Get-Sha256 $resolvedBuiltDll
$symbolDirectory = Join-Path $PSScriptRoot "..\build\symbols\$builtHash"
New-Item -ItemType Directory -Path $symbolDirectory -Force | Out-Null
Copy-Item -LiteralPath $resolvedBuiltDll -Destination $symbolDirectory -Force
$builtPdb = [IO.Path]::ChangeExtension($resolvedBuiltDll, ".pdb")
if (Test-Path -LiteralPath $builtPdb -PathType Leaf) {
    Copy-Item -LiteralPath $builtPdb -Destination $symbolDirectory -Force
}
@(
    "dll_sha256=$builtHash"
    "pdb_sha256=$(if (Test-Path -LiteralPath $builtPdb) { Get-Sha256 $builtPdb } else { 'missing' })"
    "installed_utc=$([DateTime]::UtcNow.ToString('o'))"
) | Set-Content -LiteralPath (Join-Path $symbolDirectory "build.txt")

Write-Output "Installed the combined PartyMod/AP DLL."
Write-Output "Symbols archived by DLL hash: $symbolDirectory"
Write-Output "Original backup: $backupDll"
Write-Output "Restore with: $PSCommandPath -Restore"
