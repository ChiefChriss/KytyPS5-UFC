// Senaxx 886f53db: allocate writable code, then publish it read/execute only.
#include <cstddef>
#include <cstring>
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/mman.h>
#endif
namespace Libs::Graphics::ShaderRecompiler::IR {
void* SrtNativeAllocateCode(const void* code, size_t size) {
	if (size == 0) return nullptr;
#if defined(_WIN32)
	void* memory = VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	if (!memory) return nullptr;
	std::memcpy(memory, code, size);
	DWORD old = 0;
	if (!VirtualProtect(memory, size, PAGE_EXECUTE_READ, &old)) { VirtualFree(memory,0,MEM_RELEASE); return nullptr; }
	FlushInstructionCache(GetCurrentProcess(), memory, size);
#else
	void* memory = mmap(nullptr,size,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
	if (memory == MAP_FAILED) return nullptr;
	std::memcpy(memory,code,size);
	if (mprotect(memory,size,PROT_READ|PROT_EXEC)) { munmap(memory,size); return nullptr; }
#endif
	return memory;
}
void SrtNativeFreeCode(void* memory, size_t size) {
	if (!memory) return;
#if defined(_WIN32)
	(void)size; VirtualFree(memory,0,MEM_RELEASE);
#else
	munmap(memory,size);
#endif
}
} // namespace Libs::Graphics::ShaderRecompiler::IR
