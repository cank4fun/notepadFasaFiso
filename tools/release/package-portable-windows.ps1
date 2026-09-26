param(
    [string]$BuildDir = (Join-Path $PSScriptRoot "..\..\build\windows-portable"),
    [string]$OutputDir = (Join-Path $PSScriptRoot "..\..\dist\windows-portable"),
    [string]$Dumpbin = "dumpbin.exe"
)
$ErrorActionPreference = "Stop"
$root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$buildFull = [IO.Path]::GetFullPath($BuildDir).TrimEnd('\', '/')
$outputFull = [IO.Path]::GetFullPath($OutputDir).TrimEnd('\', '/')
$source = Join-Path $buildFull "notepadFasaFiso_gui.exe"
$projectLicense = Join-Path $root "LICENSE"
$thirdPartyNotices = Join-Path $root "THIRD_PARTY_NOTICES.txt"
$privacyNotice = Join-Path $root "PRIVACY.md"
$thirdPartyLicenseSource = Join-Path $root "third_party\licenses"
if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { throw "Portable GUI executable not found: $source" }
if (-not (Test-Path -LiteralPath $projectLicense -PathType Leaf)) { throw "Project LICENSE not found: $projectLicense" }
if (-not (Test-Path -LiteralPath $thirdPartyNotices -PathType Leaf)) { throw "Third-party notice index not found: $thirdPartyNotices" }
if (-not (Test-Path -LiteralPath $privacyNotice -PathType Leaf)) { throw "Privacy notice not found: $privacyNotice" }
if (-not (Test-Path -LiteralPath $thirdPartyLicenseSource -PathType Container)) { throw "Third-party license directory not found: $thirdPartyLicenseSource" }
& (Join-Path $PSScriptRoot "verify-portable-windows.ps1") -Executable $source -Dumpbin $Dumpbin | Out-Null

$marker = "$outputFull.nff-portable-output-managed"
$markerValid = $false
if (Test-Path -LiteralPath $marker -PathType Leaf) {
    $markerValue = (Get-Content -LiteralPath $marker -Raw).Trim()
    $markerValid = [string]::Equals($markerValue, $outputFull, [StringComparison]::OrdinalIgnoreCase)
}

if (Test-Path -LiteralPath $outputFull) {
    $existing = @(Get-ChildItem -LiteralPath $outputFull -Force -ErrorAction Stop)
    if ($existing.Count -gt 0 -and -not $markerValid) {
        throw "Refusing to recursively delete an unmanaged portable output directory: $outputFull"
    }
    if ($existing.Count -eq 0 -or $markerValid) {
        Remove-Item -LiteralPath $outputFull -Recurse -Force
    }
}

New-Item -ItemType Directory -Path $outputFull | Out-Null
Set-Content -LiteralPath $marker -Value $outputFull -NoNewline -Encoding utf8
$target = Join-Path $outputFull "notepadFasaFiso.exe"
Copy-Item -LiteralPath $source -Destination $target
Copy-Item -LiteralPath $projectLicense -Destination (Join-Path $outputFull "LICENSE")
Copy-Item -LiteralPath $thirdPartyNotices -Destination (Join-Path $outputFull "THIRD_PARTY_NOTICES.txt")
Copy-Item -LiteralPath $privacyNotice -Destination (Join-Path $outputFull "PRIVACY.md")

$thirdPartyTarget = Join-Path $outputFull "third_party\licenses"
New-Item -ItemType Directory -Path $thirdPartyTarget -Force | Out-Null
Get-ChildItem -LiteralPath $thirdPartyLicenseSource -File -Force | ForEach-Object {
    Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $thirdPartyTarget $_.Name)
}

$expectedTopFiles = @('LICENSE', 'PRIVACY.md', 'THIRD_PARTY_NOTICES.txt', 'notepadFasaFiso.exe') | Sort-Object
$actualTopFiles = @(Get-ChildItem -LiteralPath $outputFull -File -Force | ForEach-Object Name | Sort-Object)
if ((Compare-Object -ReferenceObject $expectedTopFiles -DifferenceObject $actualTopFiles).Count -ne 0) {
    throw "Portable output top-level files do not match the legal distribution contract."
}
$directories = @(Get-ChildItem -LiteralPath $outputFull -Directory -Force)
if ($directories.Count -ne 1 -or $directories[0].Name -ne 'third_party') {
    throw "Portable output must contain only the third_party notice directory in addition to top-level release files."
}
$requiredLicenses = @('wxwidgets.txt', 'scintilla-lexilla.txt', 'expat.txt', 'libjpeg-turbo.txt', 'liblzma.txt', 'libpng.txt', 'libwebp.txt', 'nanosvg.txt', 'pcre2.txt', 'tiff.txt', 'zlib.txt')
foreach ($licenseName in $requiredLicenses) {
    if (-not (Test-Path -LiteralPath (Join-Path $thirdPartyTarget $licenseName) -PathType Leaf)) {
        throw "Portable output is missing third-party license notice: $licenseName"
    }
}
Get-FileHash -Algorithm SHA256 -LiteralPath $target
