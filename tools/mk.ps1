# mk.ps1: runs make in MSYS2 mingw64 against the Rack SDK, from the repo root. Exit code is make's.
# Usage: pwsh -File tools/mk.ps1 test    |    pwsh -File tools/mk.ps1    |    pwsh -File tools/mk.ps1 dist
param([Parameter(ValueFromRemainingArguments)][string[]]$Targets = @())
$repo = (Split-Path $PSScriptRoot) -replace '\\', '/'
$env:MSYSTEM = 'MINGW64'
& C:\msys64\usr\bin\bash.exe -lc "cd '$repo' && export RACK_DIR=/c/dev/Rack-SDK && make -j8 $($Targets -join ' ')"
exit $LASTEXITCODE
