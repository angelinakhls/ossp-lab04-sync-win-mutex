#include "shared_memory.h"
#include <cstdio>

namespace lab {

std::string ObjectName(const char* kind, DWORD ns) {
    char buf[96];
    snprintf(buf, sizeof(buf), "Lab04_V28_%s_%lu", kind, ns);
    return buf;
}

bool SharedMemory::Create(const std::string& name) {
    mapping_ = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                  0, (DWORD)sizeof(SharedData), name.c_str());
    if (mapping_ == nullptr) {
        fprintf(stderr, "CreateFileMapping failed: %lu\n", GetLastError());
        return false;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        fprintf(stderr, "Shared memory '%s' already exists\n", name.c_str());
        Close();
        return false;
    }
    data_ = (SharedData*)MapViewOfFile(mapping_, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedData));
    if (data_ == nullptr) {
        fprintf(stderr, "MapViewOfFile failed: %lu\n", GetLastError());
        Close();
        return false;
    }
    ZeroMemory((PVOID)data_, sizeof(SharedData));   // страницы и так нулевые, но делаем явно
    data_->magic = kMagic;
    data_->state = STATE_OK;
    return true;
}

bool SharedMemory::Open(const std::string& name) {
    mapping_ = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, name.c_str());
    if (mapping_ == nullptr) {
        fprintf(stderr, "OpenFileMapping failed: %lu\n", GetLastError());
        return false;
    }
    data_ = (SharedData*)MapViewOfFile(mapping_, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedData));
    if (data_ == nullptr) {
        fprintf(stderr, "MapViewOfFile failed: %lu\n", GetLastError());
        Close();
        return false;
    }
    if (data_->magic != kMagic) {
        fprintf(stderr, "Shared memory has wrong magic\n");
        Close();
        return false;
    }
    return true;
}

void SharedMemory::Close() {
    if (data_ != nullptr)    { UnmapViewOfFile((LPCVOID)data_); data_ = nullptr; }
    if (mapping_ != nullptr) { CloseHandle(mapping_); mapping_ = nullptr; }
}

} // namespace lab
