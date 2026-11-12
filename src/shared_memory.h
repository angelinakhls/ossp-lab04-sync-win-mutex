// shared_memory.h - обёртка над File Mapping (Shared Memory).
#pragma once
#include "common.h"

namespace lab {

class SharedMemory {
public:
    SharedMemory() : mapping_(nullptr), data_(nullptr) {}
    ~SharedMemory() { Close(); }
    SharedMemory(const SharedMemory&) = delete;
    SharedMemory& operator=(const SharedMemory&) = delete;

    bool Create(const std::string& name);   // создаёт и инициализирует (родитель)
    bool Open(const std::string& name);     // подключается к существующей (потомки)
    void Close();
    SharedData* Data() { return data_; }

private:
    HANDLE mapping_;
    SharedData* data_;
};

} // namespace lab
