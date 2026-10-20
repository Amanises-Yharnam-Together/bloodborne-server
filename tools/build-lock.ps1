# The PC's build lock (tools\rig\build_lock.py): one build at a time, never beside games (Kaniel
# 2026-10-20: builds beside rig games, worst of all several at once, froze his PC). A build script
# dot-sources this after its param() block and calls
#   Invoke-UnderBuildLock $PSCommandPath $PSBoundParameters "<what it builds>"
# which runs the script again inside the lock (it waits for the build before it and for every
# game) and exits with its code. Inside the lock (BB_BUILD_LOCK_HELD=1) it returns at once.
function Invoke-UnderBuildLock([string]$Script, $Bound, [string]$What) {
    if ($env:BB_BUILD_LOCK_HELD -eq "1") { return }
    $arguments = @()
    foreach ($name in $Bound.Keys) {
        $value = $Bound[$name]
        if ($value -is [System.Management.Automation.SwitchParameter]) {
            if ($value.IsPresent) { $arguments += "-$name" }
        } else {
            $arguments += "-$name"; $arguments += "$value"
        }
    }
    $owner = if ($env:BB_RIG_OWNER) { "$($env:BB_RIG_OWNER): $What" } else { $What }
    & py -3.12 (Join-Path $PSScriptRoot "rig\build_lock.py") run --owner $owner -- `
        powershell -NoProfile -ExecutionPolicy Bypass -File $Script @arguments
    exit $LASTEXITCODE
}
