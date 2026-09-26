param(
    [Parameter(Mandatory = $true)][string]$Executable,
    [string]$Dumpbin = "dumpbin.exe"
)
$ErrorActionPreference = "Stop"
if (-not (Test-Path -LiteralPath $Executable -PathType Leaf)) { throw "Executable not found: $Executable" }

$output = & $Dumpbin /DEPENDENTS $Executable 2>&1
if ($LASTEXITCODE -ne 0) { throw "dumpbin /DEPENDENTS failed: $($output -join [Environment]::NewLine)" }
$allowed = @(
    'ADVAPI32.DLL','COMCTL32.DLL','COMDLG32.DLL','DBGHELP.DLL','DWMAPI.DLL','DWRITE.DLL',
    'GDI32.DLL','GDIPLUS.DLL','IMM32.DLL','KERNEL32.DLL','MSIMG32.DLL','OLE32.DLL','OLEACC.DLL',
    'OLEAUT32.DLL','RPCRT4.DLL','SHELL32.DLL','SHLWAPI.DLL','USER32.DLL','UXTHEME.DLL','VERSION.DLL',
    'WINMM.DLL','WINSPOOL.DRV'
)
$imports = @()
foreach ($line in $output) {
    if ($line -match '^\s+([^\s]+\.(?:dll|drv))\s*$') { $imports += $Matches[1].ToUpperInvariant() }
}
if ($imports.Count -eq 0) { throw "No PE imports found; dumpbin /DEPENDENTS output format was not recognized." }
$unexpected = $imports | Where-Object { $_ -notin $allowed } | Sort-Object -Unique
if ($unexpected) { throw "Unexpected non-system runtime dependencies: $($unexpected -join ', ')" }

$loadConfig = & $Dumpbin /LOADCONFIG $Executable 2>&1
if ($LASTEXITCODE -ne 0) { throw "dumpbin /LOADCONFIG failed: $($loadConfig -join [Environment]::NewLine)" }
$loadConfigText = $loadConfig -join [Environment]::NewLine
$dependentLoadFlagPattern = '(?im)(?:\b0800\s+Dependent\s+Load\s+Flag(?:s)?\b|\b(?:0x)?0*800\s+Dependent\s+Load\s+Flag(?:s)?\b|\bDependent\s+Load\s+Flag(?:s)?\s*:?\s*(?:0x)?0*800\b)'
if ($loadConfigText -notmatch $dependentLoadFlagPattern) {
    throw "Portable executable is missing DEPENDENTLOADFLAG 0x800 (System32-only dependent DLL search)."
}

$imports | Sort-Object -Unique
"DEPENDENTLOADFLAG 0x800 verified."
