# Лабораторная работа 4. Вариант 28: тайм-аут и аварийное освобождение Mutex

Три процесса (Holder, Worker, Worker) работают с общей структурой в Shared Memory через именованный Mutex.
Holder периодически удерживает Mutex 50 мс, Workers ждут с тайм-аутом (10 и 100 мс). Тестовая авария Holder
(`TerminateProcess` при удержании) обрабатывается через `WAIT_ABANDONED`: структура объявляется непригодной
и откатывается к последнему согласованному состоянию.

STUDENT_ID = 114, SEED = 1000 + 114·97 = 12058, OPERATIONS = BASE_OPERATIONS(100) + 114·5 = 670.

## Требования
Windows 10/11, MSVC (Visual Studio Build Tools) или MinGW-w64, CMake ≥ 3.16, Python 3 + `matplotlib`, `python-docx`, `Pillow`
(для графиков и отчёта), PlantUML (только для перерисовки диаграмм).

## Сборка
```
cmake -S . -B build
cmake --build build --config Release
```
MinGW: `g++ -std=c++17 -O2 -static -Isrc src/*.cpp -o build/lab04_sync.exe -lwinmm`

## Запуск
```
build\Release\lab04_sync.exe --timeout 10  --ops 100            # конфигурация А
build\Release\lab04_sync.exe --timeout 100 --ops 100            # конфигурация Б
build\Release\lab04_sync.exe --timeout 100 --ops 100 --crash    # конфигурация В (авария)
build\Release\lab04_sync.exe --timeout 100 --crash --no-recover # карантин
build\Release\lab04_sync.exe --timeout 100 --no-sync --ops 20   # без Mutex: нарушения обязательны
build\Release\lab04_sync.exe --help                             # все параметры (--verbose - подробный журнал)
```

## Запуск из CLion без терминала
Run | Edit Configurations → lab04_sync → Program arguments:
`--suite tests` (проверки, пишет data/test_results.txt), затем `--suite experiments` (эксперименты, пишет data/experiment_results.csv
и data/logs). Пути определяются автоматически по CMakeLists.txt. Графики и отчёт: открыть `scripts/plot_results.py`,
затем `report/build_report.py` и запустить через Run (контекстное меню файла).

## Воспроизведение экспериментов и отчёта (PowerShell-вариант)
```
powershell -ExecutionPolicy Bypass -File tests\run_tests.ps1       -Exe build\Release\lab04_sync.exe
powershell -ExecutionPolicy Bypass -File scripts\run_experiments.ps1 -Exe build\Release\lab04_sync.exe
python scripts\plot_results.py          # графики -> report/figures
python report\build_report.py           # report/Lab4_Report.docx и .pdf (нужен LibreOffice для PDF)
```
Перед сборкой отчёта заполните `report/meta.json` (кафедра, группа, ФИО, преподаватель, хеш коммита, ОС/CPU/компилятор)
и создайте `report/appendix_ai.txt` (Приложение А, пишется самостоятельно).

## Структура
`src` – код, `tests` – проверки, `data` – CSV и журналы, `scripts` – эксперименты и графики,
`diagrams` – исходники PlantUML (+ PNG), `report` – генератор и отчёт.
