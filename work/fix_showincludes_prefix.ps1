# CMake records cl.exe's /showIncludes marker after decoding the compiler output
# with the 936 ANSI codepage, while cl.exe actually prints it as UTF-8. Ninja then
# never matches its msvc_deps_prefix, every dependency file comes out empty, and
# header edits silently re-use objects built against the old struct layout.
#
# This rewrites the recorded value with the exact bytes the compiler prints, so the
# next configure writes a matching prefix into CMakeFiles/rules.ninja. It prints
# OK when nothing needed changing and PATCHED when it rewrote a file (the caller
# should then clean once, because the existing dependency database was recorded
# with the broken prefix).
#
# The script is deliberately ASCII-only and builds the prefix from UTF-8 bytes:
# Windows PowerShell 5.1 reads BOM-less scripts with the ANSI codepage, so a
# literal here would be mangled before it ran.
param(
    [Parameter(Mandatory = $true, Position = 0)][string]$BuildDir
)

$bytes = [byte[]](0xE6, 0xB3, 0xA8, 0xE6, 0x84, 0x8F, 0x3A, 0x20, 0xE5, 0x8C,
                  0x85, 0xE5, 0x90, 0xAB, 0xE6, 0x96, 0x87, 0xE4, 0xBB, 0xB6,
                  0x3A, 0x20, 0x20)
$prefix = [System.Text.Encoding]::UTF8.GetString($bytes)
$utf8 = New-Object System.Text.UTF8Encoding($false)
$state = "OK"

$files = @()

$cmakeFiles = Join-Path $BuildDir "CMakeFiles"

if (Test-Path -LiteralPath $cmakeFiles)
{
    $files = Get-ChildItem -LiteralPath $cmakeFiles -Directory |
             ForEach-Object { Join-Path $_.FullName "CMakeCXXCompiler.cmake" } |
             Where-Object { Test-Path -LiteralPath $_ }
}

$wanted = 'set(CMAKE_CXX_CL_SHOWINCLUDES_PREFIX "' + $prefix + '")'

foreach ($file in $files)
{
    $text = [System.IO.File]::ReadAllText($file, $utf8)
    # The replacement has no '$', so the plain string overload is enough.
    $replaced = [regex]::Replace(
        $text,
        '(?m)^set\(CMAKE_CXX_CL_SHOWINCLUDES_PREFIX "[^"]*"\)',
        $wanted)

    if ($replaced -ne $text)
    {
        [System.IO.File]::WriteAllText($file, $replaced, $utf8)
        $state = "PATCHED"
    }
}

Write-Output $state
