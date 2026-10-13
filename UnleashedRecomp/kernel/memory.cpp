#include <stdafx.h>
#include "memory.h"
#include <os/logger.h>

#include <atomic>
#include <cerrno>
#include <cstdio>

Memory::Memory()
{
#ifdef _WIN32
    base = (uint8_t*)VirtualAlloc((void*)0x100000000ull, PPC_MEMORY_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);

    if (base == nullptr)
        base = (uint8_t*)VirtualAlloc(nullptr, PPC_MEMORY_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);

    if (base == nullptr)
    {
        initializationFailureStage = "VirtualAlloc";
        initializationNativeError = GetLastError();
        std::fprintf(stderr, "Failed to reserve %llu bytes of guest memory (error %u).\n", static_cast<unsigned long long>(PPC_MEMORY_SIZE), initializationNativeError);
        return;
    }

    SYSTEM_INFO systemInfo;
    GetSystemInfo(&systemInfo);
    guardPageSize = systemInfo.dwPageSize;
    DWORD oldProtect;
    if (!VirtualProtect(base, systemInfo.dwPageSize, PAGE_NOACCESS, &oldProtect))
    {
        initializationFailureStage = "VirtualProtect";
        initializationNativeError = GetLastError();
        std::fprintf(stderr, "Guest memory guard page failed (error %u).\n", initializationNativeError);
        VirtualFree(base, 0, MEM_RELEASE);
        base = nullptr;
        guardPageSize = 0;
        return;
    }
#else
    base = (uint8_t*)mmap((void*)0x100000000ull, PPC_MEMORY_SIZE, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);

    if (base == (uint8_t*)MAP_FAILED)
        base = (uint8_t*)mmap(NULL, PPC_MEMORY_SIZE, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);

    if (base == (uint8_t*)MAP_FAILED)
    {
        initializationFailureStage = "mmap";
        initializationNativeError = static_cast<uint32_t>(errno);
        std::fprintf(stderr, "Failed to reserve %llu bytes of guest memory (errno %u).\n", static_cast<unsigned long long>(PPC_MEMORY_SIZE), initializationNativeError);
        base = nullptr;
        return;
    }

#ifdef __ANDROID__
    // Keep the guest null page readable and writable. On some devices the game
    // evaluates a half-constructed animation subtree whose data pointers are 0/-1
    // (issue #27, 100% on Snapdragon 8 Gen 2 handhelds); testers confirmed the
    // reads are benign (zeros) with no visual artifacts, while faulting made the
    // game unplayable there. Indirect calls fetched through such zeros are guarded
    // separately in ppc_detail.h. Desktop builds keep the trap to catch new bugs.
#else
    // Use the host page size explicitly (Apple devices use 16 KiB pages).
    errno = 0;
    const long pageSize = sysconf(_SC_PAGESIZE);
    if (pageSize <= 0 || static_cast<size_t>(pageSize) > PPC_MEMORY_SIZE)
    {
        initializationFailureStage = "sysconf(_SC_PAGESIZE)";
        initializationNativeError = static_cast<uint32_t>(errno);
        std::fprintf(stderr, "Guest memory page size query failed (page size %ld, errno %u).\n", pageSize, initializationNativeError);
        munmap(base, PPC_MEMORY_SIZE);
        base = nullptr;
        return;
    }
    if (mprotect(base, static_cast<size_t>(pageSize), PROT_NONE) != 0)
    {
        initializationFailureStage = "mprotect";
        initializationNativeError = static_cast<uint32_t>(errno);
        std::fprintf(stderr, "Guest memory guard page failed (page size %ld, errno %u).\n", pageSize, initializationNativeError);
        munmap(base, PPC_MEMORY_SIZE);
        base = nullptr;
        return;
    }
    guardPageSize = static_cast<size_t>(pageSize);
#endif
#endif

    for (size_t i = 0; PPCFuncMappings[i].guest != 0; i++)
    {
        if (PPCFuncMappings[i].host != nullptr)
            InsertFunction(PPCFuncMappings[i].guest, PPCFuncMappings[i].host);
    }
}

void* MmGetHostAddress(uint32_t ptr)
{
    return g_memory.Translate(ptr);
}

// Called from the hardened PPC_CALL_INDIRECT_FUNC (UnleashedRecompLib/ppc/ppc_detail.h)
// when an indirect call's target is outside the recompiled code range or resolves to no
// host function - a wild jump the process could never survive. Skipping the call keeps
// the half-constructed-state races of issue #27 non-fatal; the log keeps them visible.
extern "C" void PPCIndirectCallMissing(PPCContext& ctx, uint8_t* base, uint32_t target)
{
    static std::atomic<uint32_t> s_reportCount{ 0 };
    if (s_reportCount.fetch_add(1, std::memory_order_relaxed) < 16)
    {
        LOGF_ERROR("Indirect call to unmapped guest address {:08X} skipped (r3={:08X}).",
            target, ctx.r3.u32);
    }
}
