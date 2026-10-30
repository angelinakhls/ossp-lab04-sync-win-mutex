#pragma once

#include <windows.h>
#include <cstdint>
#include <string>

struct SharedData {
    volatile LONG mutexOwner;
    volatile LONG sequenceNumber;
    volatile LONG valueA;
    volatile LONG valueB;
    volatile LONG valueC;
    volatile LONG corruptedFlag;
    volatile LONG lastWriter;
    volatile LONG lastOperationMs;
    volatile LONG activeReaders;
    volatile LONG lockState;
    volatile LONG version;
};

class SharedMemory {
public:
    SharedMemory(const std::wstring& name, size_t size);
    ~SharedMemory();

    bool Create();
    bool Open();
    void Close();
    void Reset();
    SharedData* Data();
    bool IsConsistent() const;
    bool MarkCorrupted();
    bool Recover();

private:
    std::wstring name_;
    size_t size_;
    HANDLE mapping_;
    SharedData* data_;
};
