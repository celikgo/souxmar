# SPDX-License-Identifier: Apache-2.0
#
# Sprint 10 push 8 — Windows EV code-signing automation.
#
# Wraps signtool against an EV certificate. The cert lives in a
# hardware token (eToken / YubiKey HSM-like) per ADR-0013's "keys
# live in an HSM offline from the build infrastructure" risk
# mitigation. The build agent calls into the token via the CSP
# (Cryptographic Service Provider) the cert was provisioned with;
# unattended signing in CI requires the token's PIN cached in a
# privileged secret (CI variable, never echoed).
#
# Why EV (Extended Validation):
#   * Standard code signing now requires SmartScreen reputation
#     building from scratch on each new cert — users see "unverified
#     publisher" warnings until enough downloads accrue. EV signing
#     bypasses this entirely from day one because Microsoft pre-trusts
#     EV-verified publishers.
#   * The Sectigo / DigiCert / GlobalSign EV certs cost more (~$500/yr
#     vs. $200/yr for standard) but the user-experience delta is
#     load-bearing — an "unverified publisher" warning on the
#     installer is a conversion killer.
#
# Required env vars (caller sets these in CI; the script reads them
# from the environment so the PIN never appears in the command line):
#   SOUXMAR_EV_THUMBPRINT  - sha1 thumbprint of the cert in the store
#                            (`certutil -store My` to list)
#   SOUXMAR_EV_TIMESTAMP   - RFC 3161 timestamp URL (e.g. Sectigo's
#                            http://timestamp.sectigo.com)
#   SOUXMAR_EV_DESCRIPTION - human-readable signing description
#                            (default: "souxmar release build")
#
# Usage:
#   pwsh scripts/release/sign-windows.ps1 <path\to\binary.exe>
#   pwsh scripts/release/sign-windows.ps1 <path\to\installer.msi>

param(
  [Parameter(Mandatory=$true)]
  [string]$Target
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path $Target)) {
  Write-Error "target not found: $Target"
  exit 64
}

$thumb = $env:SOUXMAR_EV_THUMBPRINT
if ([string]::IsNullOrWhiteSpace($thumb)) {
  Write-Error "SOUXMAR_EV_THUMBPRINT not set"
  exit 64
}
$tsUrl = $env:SOUXMAR_EV_TIMESTAMP
if ([string]::IsNullOrWhiteSpace($tsUrl)) {
  $tsUrl = "http://timestamp.sectigo.com"
}
$desc = $env:SOUXMAR_EV_DESCRIPTION
if ([string]::IsNullOrWhiteSpace($desc)) {
  $desc = "souxmar release build"
}

# Find signtool. The Windows SDK puts it under Program Files\Windows
# Kits\10\bin\<sdk-version>\<arch>\signtool.exe; we pick the
# latest installed x64 version. Falling back to PATH lookup if the
# SDK isn't present at the expected location (e.g., CI runner with
# a custom toolchain layout).
$signtool = $null
$kitsRoot = "${env:ProgramFiles(x86)}\Windows Kits\10\bin"
if (Test-Path $kitsRoot) {
  $signtool = Get-ChildItem -Path $kitsRoot -Recurse -Filter 'signtool.exe' `
    | Where-Object { $_.FullName -match '\\x64\\signtool\.exe$' } `
    | Sort-Object FullName -Descending `
    | Select-Object -First 1 -ExpandProperty FullName
}
if (-not $signtool) {
  $signtool = (Get-Command signtool.exe -ErrorAction SilentlyContinue).Source
}
if (-not $signtool) {
  Write-Error "signtool.exe not found; install the Windows 10/11 SDK"
  exit 64
}

Write-Host "[sign-windows] tool: $signtool"
Write-Host "[sign-windows] target: $Target"
Write-Host "[sign-windows] thumbprint: $thumb"
Write-Host "[sign-windows] timestamp: $tsUrl"

# Sign with /fd SHA256 (required since 2016), append the RFC 3161
# timestamp (/tr + /td SHA256), reference the cert by thumbprint
# (/sha1), and include a description (/d).
&$signtool sign `
  /fd      SHA256 `
  /td      SHA256 `
  /tr      $tsUrl `
  /sha1    $thumb `
  /d       $desc `
  /v `
  $Target

if ($LASTEXITCODE -ne 0) {
  Write-Error "signtool sign failed (exit $LASTEXITCODE)"
  exit $LASTEXITCODE
}

# Verify. /pa = check using the default Authenticode policy; failure
# here means the signature embedded successfully but Windows can't
# chain it — typically because the cert chain isn't installed on the
# runner.
&$signtool verify /pa /v $Target
if ($LASTEXITCODE -ne 0) {
  Write-Error "signtool verify failed (exit $LASTEXITCODE)"
  exit $LASTEXITCODE
}

Write-Host "[sign-windows] ok"
