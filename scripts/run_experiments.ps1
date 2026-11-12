<#
  Запуск всех экспериментов лабораторной работы 4 (вариант 28) и сбор данных в CSV.
  Использование (из корня репозитория, после сборки):
      powershell -ExecutionPolicy Bypass -File scripts\run_experiments.ps1 -Exe build\Release\lab04_sync.exe
  Скрипт ничего не подставляет вручную: все числа берутся из вывода программы.
#>
param(
    [string]$Exe  = "build\Release\lab04_sync.exe",
    [string]$Out  = "data\experiment_results.csv",
    [string]$Logs = "data\logs",
    [int]$Runs    = 3
)

$ErrorActionPreference = "Stop"
if (-not (Test-Path $Exe)) { throw "Executable not found: $Exe (build the project first)" }

$StudentId  = 114
$Seed       = 1000 + $StudentId * 97          # 12058
$BaseOps    = 100                             # стандартное число операций (принято в работе)
$IndOps     = $BaseOps + $StudentId * 5       # 670
$StdSeed    = 1

New-Item -ItemType Directory -Force -Path (Split-Path $Out) | Out-Null
New-Item -ItemType Directory -Force -Path $Logs | Out-Null
if (Test-Path $Out) { Remove-Item $Out }
Get-ChildItem $Logs -Filter *.log -ErrorAction SilentlyContinue | Remove-Item

# имя; аргументы
$experiments = @()
# 1. Стандартные параметры: конфигурации А, Б, В
$experiments += ,@("A_std", "--timeout 10  --ops $BaseOps --seed $StdSeed")
$experiments += ,@("B_std", "--timeout 100 --ops $BaseOps --seed $StdSeed")
$experiments += ,@("C_std", "--timeout 100 --ops $BaseOps --seed $StdSeed --crash --crash-at 3")
# 2. Индивидуальные параметры: SEED = 12058, OPERATIONS = 670
$experiments += ,@("A_ind", "--timeout 10  --ops $IndOps --seed $Seed")
$experiments += ,@("B_ind", "--timeout 100 --ops $IndOps --seed $Seed")
$experiments += ,@("C_ind", "--timeout 100 --ops $IndOps --seed $Seed --crash --crash-at 3")
# 3. Серия для графиков: зависимость от тайм-аута (стандартное число операций)
foreach ($t in 5,10,20,40,50,60,100,200) {
    $experiments += ,@("S_t$t", "--timeout $t --ops $BaseOps --seed $StdSeed")
}
# 4. Дополнительно: тест карантина и контрольный режим без Mutex (ожидаются нарушения)
$experiments += ,@("Q_norecover", "--timeout 100 --ops $BaseOps --seed $StdSeed --crash --no-recover")
$experiments += ,@("N_nosync",    "--timeout 100 --ops 20 --seed $StdSeed --no-sync")

foreach ($e in $experiments) {
    $name = $e[0]; $argStr = $e[1]
    for ($run = 1; $run -le $Runs; $run++) {
        $log = Join-Path $Logs ("{0}_run{1}.log" -f $name, $run)
        Write-Host ("[{0}] run {1}: {2}" -f $name, $run, $argStr)
        $argList = ($argStr -split "\s+" | Where-Object { $_ }) + @("--csv", $Out, "--config", $name, "--run", $run)
        & $Exe @argList *> $log          # код возврата 1 допустим для N_nosync
    }
}
Write-Host "Done. Results: $Out"
