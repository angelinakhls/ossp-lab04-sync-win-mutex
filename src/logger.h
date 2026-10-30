#include "shared_memory.h"

#include <iostream>

SharedMemory::SharedMemory(const std::wstring& name, size_t size)
    : name_(name), size_(size), mapping_(nullptr), data_(nullptr) {}

SharedMemory::~SharedMemory() {
    Close();
}

bool SharedMemory::Create() {
    mapping_ = CreateFileMappingW(
        INVALID_HANDLE_VALUE,
        NULL,
        PAGE_READWRITE,
        0,
        static_cast<DWORD>(size_),
        name_.c_str());

    if (mapping_ == nullptr) {
        std::wcerr << L"CreateFileMappingW failed: " << GetLastError() << std::endl;
        return false;
    }

    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(mapping_);
        mapping_ = nullptr;
        return false;
    }

    data_ = reinterpret_cast<SharedData*>(MapViewOfFile(mapping_, FILE_MAP_ALL_ACCESS, 0, 0, size_));
    if (data_ == nullptr) {
        std::wcerr << L"MapViewOfFile failed: " << GetLastError() << std::endl;
        return false;
    }

    Reset();
    return true;
}

bool SharedMemory::Open() {
    mapping_ = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, name_.c_str());
    if (mapping_ == nullptr) {
        std::wcerr << L"OpenFileMappingW failed: " << GetLastError() << std::endl;
        return false;
    }

    data_ = reinterpret_cast<SharedData*>(MapViewOfFile(mapping_, FILE_MAP_ALL_ACCESS, 0, 0, size_));
    if (data_ == nullptr) {
        std::wcerr << L"MapViewOfFile failed: " << GetLastError() << std::endl;
        return false;
    }

    return true;
}

void SharedMemory::Close() {
    if (data_ != nullptr) {
        UnmapViewOfFile(data_);
        data_ = nullptr;
    }

    if (mapping_ != nullptr) {
        CloseHandle(mapping_);
        mapping_ = nullptr;
    }
}

void SharedMemory::Reset() {
    if (data_ == nullptr) return;

    ZeroMemory(data_, size_);
    data_->mutexOwner = 0;
    data_->sequenceNumber = 0;
    data_->valueA = 0;
    data_->valueB = 0;
    data_->valueC = 0;
    data_->corruptedFlag = 0;
    data_->lastWriter = 0;
    data_->lastOperationMs = 0;
    data_->activeReaders = 0;
    data_->lockState = 0;
    data_->version = 1;
}

SharedData* SharedMemory::Data() {
    return data_;
}

bool SharedMemory::IsConsistent() const {
    if (data_ == nullptr) return false;
    if (data_->corruptedFlag != 0) return false;
    if (data_->valueA == data_->valueB && data_->valueB == data_->valueC) return true;
    return ((data_->valueA + data_->valueB + data_->valueC) % 3 == 0);
}

bool SharedMemory::MarkCorrupted() {
    if (data_ == nullptr) return false;
    InterlockedExchange(&data_->corruptedFlag, 1);
    return true;
}

bool SharedMemory::Recover() {
    if (data_ == nullptr) return false;
    InterlockedExchange(&data_->corruptedFlag, 0);
    data_->valueA = 0;
    data_->valueB = 0;
    data_->valueC = 0;
    data_->version++;
    return true;
}
