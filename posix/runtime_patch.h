// Copyright 2014 Louix Gu
// Author: gzc9047@gmail.com (Louix Gu)

// CppFreeMock: a tool for mock global function, member function, class static function.
//
// Implement runtime patch by using mprotect.

#ifndef CPP_FREE_MOCK_POSIX_RUNTIME_PATCH_H_
#define CPP_FREE_MOCK_POSIX_RUNTIME_PATCH_H_

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <unistd.h>

#include <sys/mman.h>

namespace CppFreeMock {

struct RuntimePatcher {
    static std::size_t PageSize;

    static std::size_t AlignAddress(const std::size_t address, const std::size_t page_size) {
        return address & (~(page_size - 1));
    }

    static int UnprotectMemory(const void* const address, const size_t length) {
        // Here we assume that page size is power of 2.
        void* const page = reinterpret_cast<void*>(AlignAddress(reinterpret_cast<std::size_t>(address), PageSize));
        return mprotect(page, length, PROT_READ | PROT_WRITE | PROT_EXEC);
    }

    static int UnprotectMemoryForOnePage(void* const address) {
        return UnprotectMemory(address, PageSize);
    }

    static void BackupBinary(const char* const function, std::vector<char>& binary_backup, const std::size_t size) {
        binary_backup = std::vector<char>(function, function + size);
    }

    static bool IsDistanceOverflow(const std::size_t distance) {
        return distance > INT32_MAX && distance < ((long long)INT32_MIN);
    }

    static std::size_t CalculateDistance(const void* const address, const void* const destination) {
        std::size_t distance = reinterpret_cast<std::size_t>(destination)
            - reinterpret_cast<std::size_t>(address)
            - 5; // For jmp instruction;
        return distance;
    }

    static void PatchFunction(char* const function, const std::size_t distance) {
        const char* const distance_bytes = reinterpret_cast<const char*>(&distance);
        // instruction: jmp 0x********;
        function[0] = 0xE9; // jmp
        std::copy(distance_bytes, distance_bytes + 4, function + 1);
    }

    static void PatchFunction64bitAddress(char* const function, const void* const destination) {
        const char* const distance_bytes = reinterpret_cast<const char*>(&destination);
        // instruction:
        // push (low 32bit)
        // mov [rsp + 4] (high 32bit)
        // ret
        function[0] = 0x68; // push
        std::copy(distance_bytes, distance_bytes + 4, function + 1);
        // C7442404 is prefix of mov dword [rsp+4], *
        function[5] = 0xC7;
        function[6] = 0x44;
        function[7] = 0x24;
        function[8] = 0x04;
        std::copy(distance_bytes + 4, distance_bytes + 8, function + 9);
        function[13] = 0xC3; // ret
    }

    static int SetJump(void* const address, const void* const destination, std::vector<char>& binary_backup) {
        char* const function = reinterpret_cast<char*>(address);
        std::size_t distance = CalculateDistance(address, destination);
        if (IsDistanceOverflow(distance)) {
            BackupBinary(function, binary_backup, 14); // long jmp.
            PatchFunction64bitAddress(function, destination);
        } else {
            BackupBinary(function, binary_backup, 5); // short jmp.
            PatchFunction(function, distance);
        }
        return 0;
    }

    static void RevertPatch(void* address, const std::vector<char>& binary_backup) {
        std::copy(binary_backup.begin(), binary_backup.end(), reinterpret_cast<char*>(address));
    }

    template < typename F1, typename F2 >
    static int SetFunctionJump(F1 address, F2 destination, std::vector<char>& binary_backup) {
        void* function = reinterpret_cast<void*>((std::size_t&)address);
        if (0 != UnprotectMemoryForOnePage(function)) {
            int err = errno;
            std::cerr << "Unprotect memory meet errno: " << err << std::endl;
            std::abort();
        }
        return SetJump(function, reinterpret_cast<void*>((std::size_t&)destination), binary_backup);
    }

    template < typename F >
    static void RestoreJump(F address, const std::vector<char>& binary_backup) {
        RevertPatch(reinterpret_cast<void*>((std::size_t&)address), binary_backup);
    }
};

// To be simple, I don't create .cpp for this static value, so you can't include this file in 2 or more cpp file.
std::size_t RuntimePatcher::PageSize = getpagesize();

} // namespace CppFreeMock

#endif // CPP_FREE_MOCK_POSIX_RUNTIME_PATCH_H_
