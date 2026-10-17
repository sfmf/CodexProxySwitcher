$ErrorActionPreference = 'Stop'
Push-Location $PSScriptRoot
try {
    New-Item -ItemType Directory -Force -Path 'dist' | Out-Null
    New-Item -ItemType Directory -Force -Path '.build' | Out-Null
    & windres 'app.rc' '-O' 'coff' '-o' '.build/app.res'
    if ($LASTEXITCODE -ne 0) { throw 'Resource compilation failed.' }
    & g++ 'main.cpp' '.build/app.res' '-o' 'dist/CodexProxySwitcher.exe' '-std=c++17' '-Os' '-s' '-static' '-static-libgcc' '-static-libstdc++' '-municode' '-mwindows' '-ffunction-sections' '-fdata-sections' '-Wl,--gc-sections' '-Wall' '-Wextra' '-lole32' '-luuid' '-lshell32' '-lcomdlg32' '-ladvapi32' '-lgdi32' '-luser32'
    if ($LASTEXITCODE -ne 0) { throw 'Compilation failed.' }
    if (!(Test-Path -LiteralPath 'dist/CodexProxySwitcher.ini')) {
        [IO.File]::WriteAllText((Join-Path $PSScriptRoot 'dist/CodexProxySwitcher.ini'), "[Launcher]`r`nProxyPort=7897`r`nCodexPath=`r`n", [Text.Encoding]::Unicode)
    }
    Copy-Item -LiteralPath 'THIRD-PARTY-NOTICES.txt' -Destination 'dist/THIRD-PARTY-NOTICES.txt' -Force
    Get-Item -LiteralPath 'dist/CodexProxySwitcher.exe' | Select-Object FullName, Length
} finally { Pop-Location }
