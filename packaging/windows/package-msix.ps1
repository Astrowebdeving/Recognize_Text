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
$manifest = Join-Path $resolvedBuild 'AppxManifest.xml'
if (-not (Test-Path -LiteralPath $manifest -PathType Leaf)) {
    throw "Missing configured manifest: $manifest"
}
if (Test-Path -LiteralPath $StageDirectory) {
    throw "StageDirectory already exists. Use a new empty path: $StageDirectory"
}

New-Item -ItemType Directory -Path $StageDirectory | Out-Null
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null

& cmake --install $resolvedBuild --config $Configuration --prefix $StageDirectory
if ($LASTEXITCODE -ne 0) { throw 'cmake --install failed' }

Copy-Item -LiteralPath $manifest -Destination (Join-Path $StageDirectory 'AppxManifest.xml')
Copy-Item -LiteralPath (Join-Path $sourceRoot 'packaging\windows\Assets') `
    -Destination (Join-Path $StageDirectory 'Assets') -Recurse

$kitsRoot = (Get-ItemProperty `
    'HKLM:\SOFTWARE\Microsoft\Windows Kits\Installed Roots').KitsRoot10
$makeAppx = Get-ChildItem -LiteralPath (Join-Path $kitsRoot 'bin') `
    -Filter 'makeappx.exe' -File -Recurse |
    Where-Object { $_.FullName -match '\\x64\\makeappx\.exe$' } |
    Sort-Object FullName -Descending |
    Select-Object -First 1
if ($null -eq $makeAppx) { throw 'MakeAppx.exe was not found in the Windows SDK' }

$architecture = if ($env:PROCESSOR_ARCHITECTURE -eq 'ARM64') { 'arm64' } else { 'x64' }
$packagePath = Join-Path $OutputDirectory "Document-Loupe-$architecture.msix"
& $makeAppx.FullName pack /d $StageDirectory /p $packagePath /o
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
