param([Parameter(Mandatory = $true)][string]$Tree)
$ErrorActionPreference = "Stop"
$resolved = (Resolve-Path -LiteralPath $Tree).Path
$allowedRoot = @(
    '.clang-format', '.clang-tidy', '.gitattributes', '.gitignore',
    'CMakeLists.txt', 'CMakePresets.json',
    'README.md', 'BUILDING.md', 'RELEASING.md', 'DEPENDENCIES.md', 'LICENSE',
    'THIRD_PARTY_NOTICES.txt', 'PRIVACY.md',
    'src', 'include', 'tests', 'benchmarks', 'assets', 'tools', 'packaging', 'third_party'
)
foreach ($required in $allowedRoot) {
    if (-not (Test-Path -LiteralPath (Join-Path $resolved $required))) { throw "Canonical tree is missing: $required" }
}
$unexpectedRoot = @(Get-ChildItem -LiteralPath $resolved -Force | Where-Object { $_.Name -notin $allowedRoot })
if ($unexpectedRoot) { throw "Unexpected canonical root items: $($unexpectedRoot.Name -join ', ')" }
$forbiddenDirs = @('build','.vs','.git','.worktrees','artifacts','vcpkg_installed','__pycache__')
$badDirs = @(Get-ChildItem -LiteralPath $resolved -Directory -Force -Recurse | Where-Object { $_.Name -in $forbiddenDirs })
if ($badDirs) { throw "Forbidden directories in canonical tree: $($badDirs.FullName -join ', ')" }
$forbiddenExt = @('.exe','.dll','.pdb','.obj','.lib','.zip','.7z','.dmp','.patch','.log','.bundle')
$badFiles = @(Get-ChildItem -LiteralPath $resolved -File -Force -Recurse | Where-Object { $_.Extension.ToLowerInvariant() -in $forbiddenExt })
if ($badFiles) { throw "Forbidden generated/binary files in canonical tree: $($badFiles.FullName -join ', ')" }
$requiredReleaseFiles = @(
    'packaging\vcpkg\vcpkg.json', 'packaging\vcpkg\vcpkg-configuration.json',
    'tools\release\package-portable-windows.ps1', 'tools\release\verify-portable-windows.ps1',
    'tools\release\package-appimage.sh', 'tools\release\verify-appimage.sh',
    'tools\release\collect-appimage-licenses.sh', 'tools\release\collect-appimage-sources.sh',
    'tools\release\collect-appimage-runtime-sources.sh',
    'tools\release\make-release-tree.ps1', 'tools\release\verify-release-tree.ps1',
    'third_party\licenses\expat.txt',
    'third_party\licenses\libjpeg-turbo.txt',
    'third_party\licenses\liblzma.txt',
    'third_party\licenses\libpng.txt',
    'third_party\licenses\libwebp.txt',
    'third_party\licenses\nanosvg.txt',
    'third_party\licenses\pcre2.txt',
    'third_party\licenses\scintilla-lexilla.txt',
    'third_party\licenses\tiff.txt',
    'third_party\licenses\wxwidgets.txt',
    'third_party\licenses\zlib.txt',
    'third_party\licenses\appimage-runtime\LICENSE.txt',
    'third_party\licenses\appimage-runtime\musl.txt',
    'third_party\licenses\appimage-runtime\mimalloc.txt',
    'third_party\licenses\appimage-runtime\squashfuse.txt',
    'third_party\licenses\appimage-runtime\zstd.txt',
    'THIRD_PARTY_NOTICES.txt', 'PRIVACY.md'
)
foreach ($relative in $requiredReleaseFiles) {
    if (-not (Test-Path -LiteralPath (Join-Path $resolved $relative))) { throw "Release infrastructure missing: $relative" }
}
"Canonical release tree verified: $resolved"
