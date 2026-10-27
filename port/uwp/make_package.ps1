# Packs the UWP build of khdays-native into an .appx, ready to install on the
# Xbox's Dev Mode through the Device Portal. Run it from the repository's root
# (paths in the README, port/uwp/README.md):
#
#   powershell -ExecutionPolicy Bypass -File port\uwp\make_package.ps1 `
#     -BuildDir build\port\obj-uwp\Release `
#     -Sdl3Dll C:\sdl3-uwp-install\bin\SDL3.dll `
#     [-DataDir data\extracted\<hash>] `
#     [-Out build\port\uwp\khdays-port.appx]
param(
    [Parameter(Mandatory = $true)][string]$BuildDir, # the build's folder, with khdays-native.exe
    [Parameter(Mandatory = $true)][string]$Sdl3Dll,  # SDL3.dll, from the SDL3-UWP build
    [string]$DataDir = "",                           # optional: the extracted game data, packaged inside
    [string]$Out = "build\port\uwp\khdays-port.appx",
    [string]$CertPfx = "build\port\uwp\khdays-dev.pfx"
)

$ErrorActionPreference = "Stop"

$layout = Join-Path $env:TEMP "khdays-uwp-package"
if (Test-Path $layout) { Remove-Item $layout -Recurse -Force }
New-Item $layout -ItemType Directory | Out-Null
New-Item (Join-Path $layout "Assets") -ItemType Directory | Out-Null

Copy-Item port\uwp\Package.appxmanifest $layout
Copy-Item port\uwp\Assets\* (Join-Path $layout "Assets")
Copy-Item (Join-Path $BuildDir "khdays-native.exe") $layout
Copy-Item $Sdl3Dll $layout
if ($DataDir -ne "") {
    New-Item (Join-Path $layout "data") -ItemType Directory | Out-Null
    Copy-Item $DataDir (Join-Path $layout "data\extracted") -Recurse
}

# A self-signed certificate, made once and kept: the console has to trust it
# before it installs the package (README, "O certificado").
if (-not (Test-Path $CertPfx)) {
    New-Item (Split-Path $CertPfx) -ItemType Directory -Force | Out-Null
    $cert = New-SelfSignedCertificate -Type Custom -Subject "CN=khdays-port" `
        -KeyUsage DigitalSignature -FriendlyName "khdays-port dev" `
        -CertStoreLocation "Cert:\CurrentUser\My" `
        -TextExtension @("2.5.29.37={text}1.3.6.1.5.5.7.3.3", "2.5.29.19={text}")
    $password = ConvertTo-SecureString -String "khdays" -Force -AsPlainText
    Export-PfxCertificate -Cert $cert -FilePath $CertPfx -Password $password | Out-Null
    Write-Host "Certificate written to $CertPfx (password: khdays) -- the console has to trust it (see port/uwp/README.md)"
}

$packed = Join-Path $env:TEMP "khdays-uwp-unsigned.appx"
if (Test-Path $packed) { Remove-Item $packed -Force }
MakeAppx pack /d $layout /p $packed /o
if ($LASTEXITCODE -ne 0) { throw "MakeAppx failed" }

$signtool = (Get-Command signtool.exe -ErrorAction SilentlyContinue).Source
if (-not $signtool) {
    $signtool = Get-ChildItem "C:\Program Files (x86)\Windows Kits\10\bin\*\x64\signtool.exe" |
        Sort-Object FullName -Descending | Select-Object -First 1 -ExpandProperty FullName
}
& $signtool sign /fd SHA256 /f $CertPfx /p khdays $packed
if ($LASTEXITCODE -ne 0) { throw "signtool failed" }

New-Item (Split-Path $Out) -ItemType Directory -Force | Out-Null
Copy-Item $packed $Out -Force
Write-Host "Package written: $Out"
Write-Host "Install it through the Device Portal (Apps -> Install app), with the certificate trusted first."
