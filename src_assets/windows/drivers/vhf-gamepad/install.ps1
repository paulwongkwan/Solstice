# Installs or removes the pinned libvirtualgamepad virtual gamepad driver.
#
# The driver package is copied verbatim from Nonary/libvirtualgamepad through
# Nonary/vibeshine and is verified against manifest.json before anything is
# staged. The catalog ships unsigned: Solstice must sign it with its own
# code-signing identity before the driver can be installed on a normal machine.
#
#   .\install.ps1                 install the driver package
#   .\install.ps1 -Uninstall      remove the device node and package
#   .\install.ps1 -AllowUnsigned  install an unsigned catalog (test signing only)
#
# Run it from an elevated PowerShell prompt.

[CmdletBinding()]
param(
    [switch] $Uninstall,
    [switch] $AllowUnsigned
)

$ErrorActionPreference = 'Stop'

$packageRoot = $PSScriptRoot
$manifestPath = Join-Path $packageRoot 'manifest.json'
$catalogPath = Join-Path $packageRoot 'driver\VibeshineVhfGamepad.cat'
$infPath = Join-Path $packageRoot 'driver\VibeshineVhfGamepad.inf'
$setupPath = Join-Path $packageRoot 'tools\VibeshineVhfGamepadDeviceSetup.exe'

if (-not (Test-Path -LiteralPath $manifestPath)) {
    throw "The driver manifest is missing: $manifestPath"
}

$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json

foreach ($file in $manifest.files) {
    $path = Join-Path $packageRoot $file.path
    if (-not (Test-Path -LiteralPath $path)) {
        throw "The driver package is incomplete: ${($file.path)} is missing."
    }

    $actual = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actual -ne $file.sha256) {
        throw "The driver package does not match its manifest: ${($file.path)} hashes to $actual."
    }
}

if ($Uninstall) {
    & $setupPath remove
    if ($LASTEXITCODE -ne 0) {
        throw "The device setup tool failed to remove the virtual gamepad node (exit $LASTEXITCODE)."
    }

    & pnputil.exe /delete-driver (Split-Path -Leaf $infPath) /uninstall /force
    & $setupPath status
    exit 0
}

if (-not (Test-Path -LiteralPath $setupPath)) {
    throw "The device setup tool is missing: $setupPath"
}

$signature = Get-AuthenticodeSignature -FilePath $catalogPath
if ($signature.Status -ne 'Valid' -and -not $AllowUnsigned) {
    throw @"
The bundled driver catalog is not signed for distribution ($($signature.Status)).
Solstice has to sign driver/VibeshineVhfGamepad.cat and
tools/VibeshineVhfGamepadDeviceSetup.exe with its own code-signing identity
before the driver can be installed on a normal machine. Use -AllowUnsigned only
on a host where Windows test signing is enabled.
"@
}

& pnputil.exe /add-driver $infPath /install
if ($LASTEXITCODE -ne 0) {
    throw "pnputil failed to stage the driver package (exit $LASTEXITCODE)."
}

& $setupPath install --inf $infPath
if ($LASTEXITCODE -ne 0) {
    throw "The device setup tool could not create the virtual gamepad node (exit $LASTEXITCODE). Reboot and run the script again."
}

& $setupPath status
