<#
  Проверки корректности (инварианты и аварийные сценарии).
      powershell -ExecutionPolicy Bypass -File tests\run_tests.ps1 -Exe build\Release\lab04_sync.exe
  Код возврата 0 - все проверки пройдены.
#>
param(
    [string]$Exe = "build\Release\lab04_sync.exe",
    [string]$Report = "data\test_results.txt"
)

$csv = Join-Path $env:TEMP "lab04_tests.csv"
if (Test-Path $csv) { Remove-Item $csv }
$failed = 0
$lines = @()

function Run-Case($name, $runArgs, $check) {
    & $Exe @runArgs --csv $csv --config $name --run 1 *> $null
    $row = Import-Csv $csv | Where-Object { $_.config -eq $name } | Select-Object -Last 1
    if ($null -eq $row) { $m = "FAIL  $name : no result row"; Write-Host $m; $script:lines += $m; $script:failed++; return }
    $ok = & $check $row
    $m = if ($ok) { "PASS  $name" } else { "FAIL  $name" }
    Write-Host $m; $script:lines += $m
    if (-not $ok) { $script:failed++ }
}

Run-Case "T1_timeout10"  @("--timeout","10","--ops","50")  { param($r) $r.violations -eq 0 -and $r.invariants_ok -eq 1 -and $r.w_abandoned -eq 0 }
Run-Case "T2_timeout100" @("--timeout","100","--ops","50") { param($r) $r.violations -eq 0 -and $r.invariants_ok -eq 1 -and $r.w_timeouts -eq 0 }
Run-Case "T3_crash_100"  @("--timeout","100","--ops","50","--crash") { param($r) $r.violations -eq 0 -and $r.w_abandoned -eq 1 -and $r.recoveries -eq 1 -and $r.invariants_ok -eq 1 }
Run-Case "T4_crash_10"   @("--timeout","10","--ops","50","--crash")  { param($r) $r.violations -eq 0 -and $r.w_abandoned -eq 1 -and $r.recoveries -eq 1 -and $r.invariants_ok -eq 1 }
Run-Case "T5_quarantine" @("--timeout","100","--ops","50","--crash","--no-recover") { param($r) $r.violations -eq 0 -and [int]$r.blocked -gt 0 -and $r.invariants_ok -eq 1 }
Run-Case "T6_nosync_must_fail" @("--timeout","100","--ops","20","--no-sync") { param($r) [int]$r.violations -gt 0 -and $r.invariants_ok -eq 0 }
Run-Case "T7_watchdog"   @("--timeout","100","--ops","100000","--max-time","300") { param($r) $r.max_time_exceeded -eq 1 }

$m = "Failed checks: $failed"
Write-Host $m; $lines += $m
New-Item -ItemType Directory -Force -Path (Split-Path $Report) | Out-Null
$lines | Out-File -Encoding utf8 $Report
exit $failed
