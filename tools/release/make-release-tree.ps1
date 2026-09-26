param(
    [Parameter(Mandatory = $true)][string]$Destination,
    [switch]$Force
)
$ErrorActionPreference = "Stop"
$root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$rootFull = [IO.Path]::GetFullPath($root).TrimEnd('\', '/')
$destinationFull = [IO.Path]::GetFullPath($Destination).TrimEnd('\', '/')
$comparison = [StringComparison]::OrdinalIgnoreCase

if ([string]::Equals($rootFull, $destinationFull, $comparison) -or
    $rootFull.StartsWith($destinationFull + [IO.Path]::DirectorySeparatorChar, $comparison)) {
    throw "Destination must not be the source tree or one of its parent directories: $Destination"
}

$marker = "$destinationFull.nff-release-tree-managed"
$markerValid = $false
if (Test-Path -LiteralPath $marker -PathType Leaf) {
    $markerValue = (Get-Content -LiteralPath $marker -Raw).Trim()
    $markerValid = [string]::Equals($markerValue, $destinationFull, $comparison)
}

if (Test-Path -LiteralPath $destinationFull) {
    $existing = @(Get-ChildItem -LiteralPath $destinationFull -Force -ErrorAction Stop)
    if ($existing.Count -gt 0) {
        if (-not $Force) {
            throw "Destination is not empty: $destinationFull (use -Force to replace a previously managed tree)"
        }
        if (-not $markerValid) {
            throw "Refusing to recursively delete an unmanaged destination: $destinationFull"
        }
    }
    if ($Force -and ($existing.Count -eq 0 -or $markerValid)) {
        Remove-Item -LiteralPath $destinationFull -Recurse -Force
    }
}

New-Item -ItemType Directory -Path $destinationFull -Force | Out-Null
Set-Content -LiteralPath $marker -Value $destinationFull -NoNewline -Encoding utf8

$allow = @(
    '.clang-format', '.clang-tidy', '.gitattributes', '.gitignore',
    'CMakeLists.txt', 'CMakePresets.json',
    'README.md', 'BUILDING.md', 'RELEASING.md', 'DEPENDENCIES.md', 'LICENSE',
    'THIRD_PARTY_NOTICES.txt', 'PRIVACY.md',
    'src', 'include', 'tests', 'benchmarks', 'assets', 'tools', 'packaging', 'third_party'
)
foreach ($name in $allow) {
    $source = Join-Path $root $name
    if (-not (Test-Path -LiteralPath $source)) { throw "Required canonical source item is missing: $name" }
    Copy-Item -LiteralPath $source -Destination (Join-Path $destinationFull $name) -Recurse -Force
}
& (Join-Path $PSScriptRoot 'verify-release-tree.ps1') -Tree $destinationFull
