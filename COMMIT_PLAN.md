# Как зафиксировать изменения в Git (реальная история, без подделки)
Новые файлы нужно коммитить в порядке, соответствующем этапам. Пример:
1. `git rm` старых перепутанных файлов (src/*, tests/*, scripts/generate_report_data.py, run_tests.bat, report/appendix_ai.md) – "Удалён неработоспособный каркас: файлы в src содержали чужой код"
2. src/common.h, shared_memory.*, logger.*, diagrams/ – "Этап 1-2: структура данных, Shared Memory, журнал, диаграммы"
3. src/sync.*, participant.*, parent.*, main.cpp, CMakeLists.txt – "Этап 3: Mutex, WAIT_TIMEOUT/WAIT_ABANDONED, инварианты, тест аварии"
4. tests/run_tests.ps1 – "Добавлены проверки корректности и контрольный режим без Mutex"
5. scripts/*, data/* (после запуска на Windows), report/* – "Этап 4: эксперименты, графики, отчёт"
Историю опубликованных коммитов не переписывать (force push запрещён).
