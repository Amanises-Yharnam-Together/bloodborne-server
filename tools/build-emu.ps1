param([string]$Type = "RelWithDebInfo", [string]$Dir = "build")
. "$PSScriptRoot\env.ps1"
Set-Location (Join-Path $env:BB_ROOT "shadp2p")
cmake -G Ninja -B $Dir -DCMAKE_BUILD_TYPE=$Type -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl -DENABLE_TESTS=ON 2>&1 | Select-Object -Last 3
cmake --build $Dir --parallel 24 2>&1 | Tee-Object -FilePath (Join-Path $env:BB_ROOT "emu-build.log") | Select-String -Pattern " error |error:|FAILED" | Select-Object -First 20
"exit=$LASTEXITCODE"
Get-ChildItem "$Dir\shadPS4.exe" -ErrorAction SilentlyContinue | Select-Object FullName,Length



