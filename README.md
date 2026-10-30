# OSSP Lab 04 - Windows synchronization

This repository contains a Windows API modeling solution for Lab 04.

Variant: 28. Timeout and emergency release of Mutex.

## Purpose
The solution models three processes sharing a common memory block protected by a named Mutex. One process temporarily owns the mutex for 50 ms, while the others attempt acquisition with timeouts.

## Features
- Shared Memory
- Named Mutex
- WaitForSingleObject with timeout handling
- Detection of WAIT_TIMEOUT and WAIT_ABANDONED
- Shared data consistency checks
- Recovery after abandoned ownership
- Event logging and statistical summary

## Build
Use Visual Studio 2022 or CMake:

```powershell
cmake -S . -B build
cmake --build build --config Release
```

## Run
```powershell
./build/Release/lab04_sync.exe --timeout 10
./build/Release/lab04_sync.exe --timeout 100
```

## Structure
- src/ source code
- tests/ invariant checks and execution helpers
- data/ experiment logs
- scripts/ plotting scripts
- diagrams/ diagrams in PlantUML
- report/ final report text
