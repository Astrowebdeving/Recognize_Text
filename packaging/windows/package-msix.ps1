[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $BuildDirectory,

    [Parameter(Mandatory = $true)]
    [string] $StageDirectory,

    [Parameter(Mandatory = $true)]
    [string] $OutputDirectory,

    [ValidateSet('Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel')]
    [string] $Configuration = 'Release',

    [string] $CertificatePath = '',

    [string] $CertificatePassword = ''
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$sourceRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$resolvedBuild = (Resolve-Path -LiteralPath $BuildDirectory).Path
$resolvedStage = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath(
    $StageDirectory)
$resolvedOutput = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath(
    $OutputDirectory)
$manifest = Join-Path $resolvedBuild 'AppxManifest.xml'
if (-not (Test-Path -LiteralPath $manifest -PathType Leaf)) {
    throw "Missing configured manifest: $manifest"
}
if (Test-Path -LiteralPath $resolvedStage) {
    throw "StageDirectory already exists. Use a new empty path: $resolvedStage"
}

New-Item -ItemType Directory -Path $resolvedStage | Out-Null
New-Item -ItemType Directory -Path $resolvedOutput -Force | Out-Null

& cmake --install $resolvedBuild --config $Configuration --prefix $resolvedStage
if ($LASTEXITCODE -ne 0) { throw 'cmake --install failed' }

Copy-Item -LiteralPath $manifest -Destination (Join-Path $resolvedStage 'AppxManifest.xml')
Copy-Item -LiteralPath (Join-Path $sourceRoot 'packaging\windows\Assets') `
    -Destination (Join-Path $resolvedStage 'Assets') -Recurse

$kitsRoot = (Get-ItemProperty `
    'HKLM:\SOFTWARE\Microsoft\Windows Kits\Installed Roots').KitsRoot10
$makeAppx = Get-ChildItem -LiteralPath (Join-Path $kitsRoot 'bin') `
    -Filter 'makeappx.exe' -File -Recurse |
    Where-Object { $_.FullName -match '\\x64\\makeappx\.exe$' } |
    Sort-Object FullName -Descending |
    Select-Object -First 1
if ($null -eq $makeAppx) { throw 'MakeAppx.exe was not found in the Windows SDK' }

$architecture = if ($env:PROCESSOR_ARCHITECTURE -eq 'ARM64') { 'arm64' } else { 'x64' }
$packagePath = Join-Path $resolvedOutput "Document-Loupe-$architecture.msix"
& $makeAppx.FullName pack /d $resolvedStage /p $packagePath /o
if ($LASTEXITCODE -ne 0) { throw 'MakeAppx failed' }

if ($CertificatePath) {
    $resolvedCertificate = (Resolve-Path -LiteralPath $CertificatePath).Path
    $signTool = Get-ChildItem -LiteralPath (Join-Path $kitsRoot 'bin') `
        -Filter 'signtool.exe' -File -Recurse |
        Where-Object { $_.FullName -match '\\x64\\signtool\.exe$' } |
        Sort-Object FullName -Descending |
        Select-Object -First 1
    if ($null -eq $signTool) { throw 'SignTool.exe was not found in the Windows SDK' }
    & $signTool.FullName sign /fd SHA256 /f $resolvedCertificate `
        /p $CertificatePassword $packagePath
    if ($LASTEXITCODE -ne 0) { throw 'SignTool failed' }
}

Write-Host "Created $packagePath"
