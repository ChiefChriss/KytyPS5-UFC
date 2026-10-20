#include "common/threads.h"

#include "common/assert.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>             // IWYU pragma: keep
#include <condition_variable> // IWYU pragma: keep
#include <mutex>

// macOS has no clock_nanosleep.
#if KYTY_PLATFORM != KYTY_PLATFORM_WINDOWS && !defined(__APPLE__)
#define KYTY_POSIX_HIGH_RES_SLEEP
#include <ctime>
#endif

#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>

#if KYTY_PLATFORM == KYTY_PLATFORM_WINDOWS
#include <windows.h> // IWYU pragma: keep
#include <tlhelp32.h> // IWYU pragma: keep
// IWYU pragma: no_include <winbase.h>
constexpr DWORD KYTY_CS_SPIN_COUNT = 4000;

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

static void SleepHighResolution100ns(uint64_t units_100ns) {
	if (units_100ns == 0) {
		return;
	}

	thread_local HANDLE timer = CreateWaitableTimerExW(
	    nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
	if (timer == nullptr) {
		thread_local HANDLE fallback_timer = CreateWaitableTimerW(nullptr, TRUE, nullptr);
		timer                              = fallback_timer;
	}

	if (timer != nullptr) {
		LARGE_INTEGER due_time {};
		due_time.QuadPart = -static_cast<LONGLONG>(units_100ns);
		if (SetWaitableTimerEx(timer, &due_time, 0, nullptr, nullptr, nullptr, 0) != 0) {
			WaitForSingleObject(timer, INFINITE);
			return;
		}
	}

	std::this_thread::sleep_for(std::chrono::nanoseconds(units_100ns * 100));
}

// IWYU pragma: no_include <minwindef.h>
// IWYU pragma: no_include <synchapi.h>
// IWYU pragma: no_include <minwinbase.h>
// IWYU pragma: no_include <__mutex_base>
// IWYU pragma: no_include <__threading_support>
// IWYU pragma: no_include <errhandlingapi.h>
// IWYU pragma: no_include <winerror.h>

using InitializeConditionVariable_func_t = /*WINBASEAPI*/ VOID WINAPI (*)(PCONDITION_VARIABLE);
using WakeConditionVariable_func_t    = /*WINBASEAPI*/ VOID       WINAPI (*)(PCONDITION_VARIABLE);
using WakeAllConditionVariable_func_t = /*WINBASEAPI*/ VOID    WINAPI (*)(PCONDITION_VARIABLE);
using SleepConditionVariableCS_func_t = /*WINBASEAPI*/ BOOL    WINAPI (*)(PCONDITION_VARIABLE,
                                                                          PCRITICAL_SECTION, DWORD);

static InitializeConditionVariable_func_t ResolveInitializeConditionVariable() {
	if (HMODULE h = GetModuleHandle("KernelBase"); h != nullptr) // @suppress("Invalid arguments")
	{
		return reinterpret_cast<InitializeConditionVariable_func_t>(
		    GetProcAddress(h, "InitializeConditionVariable"));
	}
	return nullptr;
}
static WakeConditionVariable_func_t ResolveWakeConditionVariable() {
	if (HMODULE h = GetModuleHandle("KernelBase"); h != nullptr) // @suppress("Invalid arguments")
	{
		return reinterpret_cast<WakeConditionVariable_func_t>(
		    GetProcAddress(h, "WakeConditionVariable"));
	}
	return nullptr;
}
static WakeAllConditionVariable_func_t ResolveWakeAllConditionVariable() {
	if (HMODULE h = GetModuleHandle("KernelBase"); h != nullptr) // @suppress("Invalid arguments")
	{
		return reinterpret_cast<WakeAllConditionVariable_func_t>(
		    GetProcAddress(h, "WakeAllConditionVariable"));
	}
	return nullptr;
}
static SleepConditionVariableCS_func_t ResolveSleepConditionVariableCS() {
	if (HMODULE h = GetModuleHandle("KernelBase"); h != nullptr) // @suppress("Invalid arguments")
	{
		return reinterpret_cast<SleepConditionVariableCS_func_t>(
		    GetProcAddress(h, "SleepConditionVariableCS"));
	}
	return nullptr;
}

#endif

#ifdef KYTY_POSIX_HIGH_RES_SLEEP
// An absolute deadline preserves the requested sleep across signal interruptions.
static void SleepHighResolutionNanos(uint64_t nanos) {
	if (nanos == 0) {
		return;
	}

	constexpr uint64_t NANOS_PER_SEC = 1000000000;

	timespec deadline {};
	if (clock_gettime(CLOCK_MONOTONIC, &deadline) != 0) {
		std::this_thread::sleep_for(std::chrono::nanoseconds(nanos));
		return;
	}

	auto target_nsec = static_cast<uint64_t>(deadline.tv_nsec) + nanos;
	deadline.tv_sec += static_cast<time_t>(target_nsec / NANOS_PER_SEC);
	deadline.tv_nsec = static_cast<long>(target_nsec % NANOS_PER_SEC);

	while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &deadline, nullptr) == EINTR) {
	}
}
#endif

namespace Common {

using thread_id_t = std::thread::id;

struct MutexPrivate {
#if KYTY_PLATFORM == KYTY_PLATFORM_WINDOWS
	MutexPrivate() { InitializeCriticalSectionAndSpinCount(&m_cs, KYTY_CS_SPIN_COUNT); }
	~MutexPrivate() { DeleteCriticalSection(&m_cs); }
	KYTY_CLASS_NO_COPY(MutexPrivate);
	CRITICAL_SECTION m_cs {};
#else
	std::recursive_mutex m_mutex;
#endif
};

struct CondVarPrivate {
#if KYTY_PLATFORM == KYTY_PLATFORM_WINDOWS
	CondVarPrivate() {
		static auto func = ResolveInitializeConditionVariable();
		EXIT_NOT_IMPLEMENTED(func == nullptr);
		func(&m_cv);
	}
	~CondVarPrivate() = default;
	KYTY_CLASS_NO_COPY(CondVarPrivate);
	CONDITION_VARIABLE m_cv {};
#else
	std::condition_variable_any m_cv;
#endif
};

static wait_poll_func_t g_cond_wait_poll_callback = nullptr;

struct ThreadPrivate {
	ThreadPrivate(thread_func_t f, void* a): func(f), arg(a), m_thread(&Run, this) {}

	static void Run(ThreadPrivate* t) {
		OptOutThreadPowerThrottling();
		t->unique_id = Thread::GetThreadIdUnique();
		t->started   = true;
		t->func(t->arg);
	}

	thread_func_t    func;
	void*            arg;
	std::atomic_bool finished    = false;
	std::atomic_bool auto_delete = false;
	std::atomic_bool started     = false;
	int              unique_id   = 0;
	std::thread      m_thread;
};

static thread_id_t      g_main_thread;
static int              g_main_thread_int;
static std::atomic<int> g_thread_counter = 0;

// Opt-in only (KYTY_UFC_POWER_THROTTLING=0): with the per-thread opt-out active, UFC hit an
// intermittent GPU device loss (WriteInvalid at page 0) in 2 of 4 sessions; the timing
// change likely exposes an existing race. No FPS benefit was demonstrated.
static bool PowerThrottlingOptOutEnabled() {
	static const bool enabled = [] {
		// Dev 094 all-on bisect: the opt-out is on by default; KYTY_UFC_POWER_THROTTLING=1 keeps
		// Windows power throttling (the old default).
		const char* setting = std::getenv("KYTY_UFC_POWER_THROTTLING");
		return setting == nullptr || std::strcmp(setting, "1") != 0;
	}();
	return enabled;
}

void OptOutThreadPowerThrottling() {
#if KYTY_PLATFORM == KYTY_PLATFORM_WINDOWS
	if (!PowerThrottlingOptOutEnabled()) {
		return;
	}
	THREAD_POWER_THROTTLING_STATE state {};
	state.Version     = THREAD_POWER_THROTTLING_CURRENT_VERSION;
	state.ControlMask = THREAD_POWER_THROTTLING_EXECUTION_SPEED;
	state.StateMask   = 0;
	(void)SetThreadInformation(GetCurrentThread(), ThreadPowerThrottling, &state, sizeof(state));
#endif
}

#if KYTY_PLATFORM == KYTY_PLATFORM_WINDOWS
// Windows 11 applies EcoQoS (efficiency cores, reduced clocks, coarse timers) to processes
// whose window is not in the foreground. The emulator's CPU-bound command, GPU and guest
// threads then run up to ~2x slower while the game window is behind another window.
// Opt the process out explicitly; KYTY_UFC_POWER_THROTTLING=1 keeps the system default.
static void DisableHostPowerThrottling() {
	if (!PowerThrottlingOptOutEnabled()) {
		std::printf("Host power throttling: system default (opt out with "
		            "KYTY_UFC_POWER_THROTTLING=0)\n");
		return;
	}
	PROCESS_POWER_THROTTLING_STATE state {};
	state.Version     = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
	state.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
	state.StateMask   = 0;
	const BOOL  speed       = SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling,
	                                                &state, sizeof(state));
	const DWORD speed_error = speed != FALSE ? 0 : GetLastError();
#ifdef PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION
	state.ControlMask       = PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION;
	const BOOL  timer       = SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling,
	                                                &state, sizeof(state));
	const DWORD timer_error = timer != FALSE ? 0 : GetLastError();
#else
	const BOOL  timer       = FALSE;
	const DWORD timer_error = 0xffffffffu;
#endif
	// The calling (main) thread also opts out; threads it creates inherit the process state.
	THREAD_POWER_THROTTLING_STATE thread_state {};
	thread_state.Version     = THREAD_POWER_THROTTLING_CURRENT_VERSION;
	thread_state.ControlMask = THREAD_POWER_THROTTLING_EXECUTION_SPEED;
	thread_state.StateMask   = 0;
	const BOOL thread_ok     = SetThreadInformation(GetCurrentThread(), ThreadPowerThrottling,
	                                                &thread_state, sizeof(thread_state));
	std::printf("Host power throttling: disabled (execution speed %s/%lu, timer resolution "
	            "%s/%lu, main thread %s/%lu, state size %zu)\n",
	            speed != FALSE ? "ok" : "failed", static_cast<unsigned long>(speed_error),
	            timer != FALSE ? "ok" : "failed", static_cast<unsigned long>(timer_error),
	            thread_ok != FALSE ? "ok" : "failed",
	            static_cast<unsigned long>(thread_ok != FALSE ? 0 : GetLastError()),
	            sizeof(state));
}
#endif

#if KYTY_PLATFORM == KYTY_PLATFORM_WINDOWS
// Hybrid CPUs (e.g. i7-14700K: 8 P-cores with SMT + 12 E-cores): the command processor and the
// guest's job threads are latency-bound, and benchmark runs were bimodal (about 10 vs 15 FPS at
// the same CPU-seconds per second), consistent with the scheduler placing them on E-cores. The
// PS5 has 8 SMT cores, so the process can be restricted to the highest-performance CPU set.
// Opt-in (KYTY_UFC_PCORES=1) until multi-run measurements show it helps.
struct PerformanceCpuSets {
	std::vector<ULONG> ids;
	uint32_t           total     = 0;
	BYTE               max_class = 0;
	[[nodiscard]] bool Hybrid() const { return max_class != 0 && !ids.empty() && ids.size() != total; }
};

static const PerformanceCpuSets& GetPerformanceCpuSets() {
	static const PerformanceCpuSets sets = [] {
		PerformanceCpuSets result;
		ULONG              length = 0;
		(void)GetSystemCpuSetInformation(nullptr, 0, &length, GetCurrentProcess(), 0);
		if (length == 0) {
			return result;
		}
		std::vector<uint8_t> storage(length);
		auto* info = reinterpret_cast<PSYSTEM_CPU_SET_INFORMATION>(storage.data());
		if (GetSystemCpuSetInformation(info, length, &length, GetCurrentProcess(), 0) == FALSE) {
			return result;
		}
		for (int pass = 0; pass < 2; pass++) {
			for (ULONG offset = 0; offset < length;) {
				const auto* entry = reinterpret_cast<const SYSTEM_CPU_SET_INFORMATION*>(
				    storage.data() + offset);
				if (entry->Size == 0) {
					break;
				}
				if (entry->Type == CpuSetInformation) {
					const BYTE cls = entry->CpuSet.EfficiencyClass;
					if (pass == 0) {
						result.max_class = cls > result.max_class ? cls : result.max_class;
						result.total++;
					} else if (cls == result.max_class) {
						result.ids.push_back(entry->CpuSet.Id);
					}
				}
				offset += entry->Size;
			}
		}
		return result;
	}();
	return sets;
}

static void PreferPerformanceCores() {
	const char* setting = std::getenv("KYTY_UFC_PCORES");
	if (setting == nullptr || std::strcmp(setting, "1") != 0) {
		std::printf("Host CPU sets: system default (opt in with KYTY_UFC_PCORES=1)\n");
		return;
	}
	const auto&    sets      = GetPerformanceCpuSets();
	const BYTE     max_class = sets.max_class;
	const uint32_t total     = sets.total;
	const auto&    ids       = sets.ids;
	if (!sets.Hybrid()) {
		std::printf("Host CPU sets: uniform cores (%u), no restriction\n", total);
		return;
	}
	const BOOL ok = SetProcessDefaultCpuSets(GetCurrentProcess(), ids.data(),
	                                         static_cast<ULONG>(ids.size()));
	std::printf("Host CPU sets: %s performance cores (%zu of %u logical, efficiency class %u)\n",
	            ok != FALSE ? "restricted to" : "failed to restrict to", ids.size(), total,
	            static_cast<unsigned>(max_class));
}
#endif

#if KYTY_PLATFORM == KYTY_PLATFORM_WINDOWS
// In-process tid -> role registry. SetThreadDescription/GetThreadDescription is the documented
// path, but it did not resolve on this host (the sampler printed every top tid as "-"), so the
// roles are also recorded in a process-local map that the sampler can read directly. Keyed by
// GetCurrentThreadId(), which is the same id CreateToolhelp32Snapshot reports.
namespace {

std::mutex                             g_thread_role_mutex;
std::unordered_map<DWORD, std::string> g_thread_roles;

void RegisterThreadRole(DWORD tid, const char* name) {
	if (name == nullptr || name[0] == '\0') {
		return;
	}
	std::scoped_lock lock(g_thread_role_mutex);
	g_thread_roles[tid] = name;
}

std::string LookupThreadRole(DWORD tid) {
	std::scoped_lock lock(g_thread_role_mutex);
	const auto       it = g_thread_roles.find(tid);
	return it == g_thread_roles.end() ? std::string {} : it->second;
}

} // namespace

// Sets the calling thread's Win32 description so the per-thread CPU sampler can name it.
static void SetCurrentThreadName(const char* name) {
	if (name == nullptr || name[0] == '\0') {
		return;
	}
	RegisterThreadRole(GetCurrentThreadId(), name);
	const int len = MultiByteToWideChar(CP_UTF8, 0, name, -1, nullptr, 0);
	if (len <= 0) {
		return;
	}
	std::wstring w(static_cast<size_t>(len), L'\0');
	(void)MultiByteToWideChar(CP_UTF8, 0, name, -1, w.data(), len);
	(void)SetThreadDescription(GetCurrentThread(), w.c_str());
}
#endif

// Pins the calling thread (the GPU command thread, the presenter) to the performance cores on
// hybrid CPUs, leaving guest job threads to the OS scheduler (the AnyPS5 WorkerAffinity design).
// Benchmark runs were bimodal at equal CPU time, consistent with the bottleneck command thread
// being scheduled on efficiency cores. KYTY_UFC_PIN_GPU=0 disables.
void PinThreadToPerformanceCores(const char* role) {
#if KYTY_PLATFORM == KYTY_PLATFORM_WINDOWS
	SetCurrentThreadName(role);
	const char* setting = std::getenv("KYTY_UFC_PIN_GPU");
	if (setting != nullptr && std::strcmp(setting, "0") == 0) {
		return;
	}
	const auto& sets = GetPerformanceCpuSets();
	if (!sets.Hybrid()) {
		return;
	}
	const BOOL ok = SetThreadSelectedCpuSets(GetCurrentThread(), sets.ids.data(),
	                                         static_cast<ULONG>(sets.ids.size()));
	std::printf("Host CPU sets: %s thread %s to %zu performance cores\n",
	            ok != FALSE ? "pinned" : "failed to pin", role, sets.ids.size());
#else
	(void)role;
#endif
}

#if KYTY_PLATFORM == KYTY_PLATFORM_WINDOWS
namespace {

uint64_t FileTimeTicks(const FILETIME& ft) {
	return (static_cast<uint64_t>(ft.dwHighDateTime) << 32u) | ft.dwLowDateTime;
}

std::string NarrowWide(const wchar_t* w) {
	if (w == nullptr) {
		return {};
	}
	const int len = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
	if (len <= 0) {
		return {};
	}
	std::string out(static_cast<size_t>(len - 1), '\0');
	(void)WideCharToMultiByte(CP_UTF8, 0, w, -1, out.data(), len, nullptr, nullptr);
	return out;
}

bool ThreadCpuSamplerEnabled() {
	static const bool enabled = [] {
		const char* value = std::getenv("KYTY_UFC_THREAD_CPU");
		return value != nullptr && std::strcmp(value, "0") != 0;
	}();
	return enabled;
}

// Opt-in (KYTY_UFC_THREAD_CPU=1): every 30 s print the per-thread host CPU time as busy cores.
// The aggregate zone profile cannot say which thread limits the frame rate; this can.
void ThreadCpuSamplerLoop() {
	struct Entry {
		DWORD    tid    = 0;
		uint64_t cpu    = 0; // kernel + user, in 100 ns units
		uint64_t kernel = 0;
	};
	std::vector<Entry> prev;
	LARGE_INTEGER      freq {};
	QueryPerformanceFrequency(&freq);
	LARGE_INTEGER prev_qpc {};
	QueryPerformanceCounter(&prev_qpc);
	for (;;) {
		SleepHighResolution100ns(30ull * 1000ull * 1000ull * 10ull); // 30 s in 100 ns units
		LARGE_INTEGER now_qpc {};
		QueryPerformanceCounter(&now_qpc);
		const double interval = static_cast<double>(now_qpc.QuadPart - prev_qpc.QuadPart) /
		                        static_cast<double>(freq.QuadPart);
		prev_qpc = now_qpc;
		if (interval <= 0.0) {
			continue;
		}

		std::vector<Entry> now;
		const HANDLE       snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
		if (snap != INVALID_HANDLE_VALUE) {
			THREADENTRY32 te {};
			te.dwSize     = sizeof(te);
			const DWORD pid = GetCurrentProcessId();
			if (Thread32First(snap, &te) != FALSE) {
				do {
					if (te.th32OwnerProcessID != pid) {
						continue;
					}
					const HANDLE h =
					    OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, te.th32ThreadID);
					if (h == nullptr) {
						continue;
					}
					FILETIME create {}, exit {}, kernel {}, user {};
					if (GetThreadTimes(h, &create, &exit, &kernel, &user) != FALSE) {
						Entry entry;
						entry.tid    = te.th32ThreadID;
						entry.kernel = FileTimeTicks(kernel);
						entry.cpu    = entry.kernel + FileTimeTicks(user);
						now.push_back(entry);
					}
					CloseHandle(h);
				} while (Thread32Next(snap, &te) != FALSE);
			}
			CloseHandle(snap);
		}

		struct Busy {
			DWORD  tid     = 0;
			double cores   = 0.0;
			double kernel_pct = 0.0; // share of this interval's CPU spent in kernel time
			double user_pct   = 0.0;
		};
		std::vector<Busy> busy;
		busy.reserve(now.size());
		double total = 0.0;
		if (prev.empty()) {
			// First sample has no baseline: publish it and wait for the next interval.
			prev = std::move(now);
			continue;
		}
		for (const auto& entry: now) {
			uint64_t before        = 0;
			uint64_t kernel_before = 0;
			for (const auto& p: prev) {
				if (p.tid == entry.tid) {
					before        = p.cpu;
					kernel_before = p.kernel;
					break;
				}
			}
			if (entry.cpu <= before) {
				continue;
			}
			const auto   delta_cpu    = entry.cpu - before;
			const auto   delta_kernel = entry.kernel > kernel_before
			                                ? entry.kernel - kernel_before
			                                : uint64_t {0};
			const double cores        = static_cast<double>(delta_cpu) / 1.0e7 / interval;
			Busy         item;
			item.tid        = entry.tid;
			item.cores      = cores;
			item.kernel_pct = 100.0 * static_cast<double>(delta_kernel) /
			                  static_cast<double>(delta_cpu);
			item.user_pct = 100.0 - item.kernel_pct;
			busy.push_back(item);
			total += cores;
		}
		prev = std::move(now);
		std::sort(busy.begin(), busy.end(),
		          [](const Busy& a, const Busy& b) { return a.cores > b.cores; });

		std::string line;
		char        header[96] {};
		std::snprintf(header, sizeof(header), " interval %.1fs: total=%.2f cores |", interval,
		              total);
		line = header;
		for (size_t i = 0; i < busy.size() && i < 8; i++) {
			std::string name = LookupThreadRole(busy[i].tid);
			if (name.empty()) {
				const HANDLE h =
				    OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, busy[i].tid);
				if (h != nullptr) {
					PWSTR desc = nullptr;
					if (GetThreadDescription(h, &desc) == S_OK && desc != nullptr) {
						name = NarrowWide(desc);
						LocalFree(desc);
					}
					CloseHandle(h);
				}
			}
			char entry[176] {};
			std::snprintf(entry, sizeof(entry), " tid=%lu %s %.2f (kernel=%.0f%%)",
			              static_cast<unsigned long>(busy[i].tid), name.empty() ? "-" : name.c_str(),
			              busy[i].cores, busy[i].kernel_pct);
			line += entry;
		}
		std::fputs("Thread CPU", stdout);
		std::fputs(line.c_str(), stdout);
		std::fputc('\n', stdout);
		std::fflush(stdout);
	}
}

void StartThreadCpuSampler() {
	if (!ThreadCpuSamplerEnabled()) {
		return;
	}
	static std::atomic_bool started {false};
	if (started.exchange(true)) {
		return;
	}
	std::thread(ThreadCpuSamplerLoop).detach();
	std::printf("Thread CPU sampler: on (KYTY_UFC_THREAD_CPU=1)\n");
}

} // namespace
#endif

bool UfcGpuPriorityEnabled() {
	static const bool enabled = [] {
		const char* setting = std::getenv("KYTY_UFC_GPU_PRIORITY"); // Dev 094 all-on bisect: on by default, =0 turns it off
		return setting == nullptr || std::strcmp(setting, "0") != 0;
	}();
	return enabled;
}

void RaiseThreadAboveGuest(const char* role) {
#if KYTY_PLATFORM == KYTY_PLATFORM_WINDOWS
	SetCurrentThreadName(role);
	if (!UfcGpuPriorityEnabled()) {
		return;
	}
	const BOOL ok = SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
	std::printf("Host thread priority: %s thread %s to above normal (KYTY_UFC_GPU_PRIORITY=1)\n",
	            ok != FALSE ? "raised" : "failed to raise", role);
#else
	(void)role;
#endif
}

void InitializeThreads() {
	g_main_thread     = std::this_thread::get_id();
	g_main_thread_int = Thread::GetThreadIdUnique();
#if KYTY_PLATFORM == KYTY_PLATFORM_WINDOWS
	DisableHostPowerThrottling();
	PreferPerformanceCores();
	std::printf("Host thread priority: %s\n",
	            UfcGpuPriorityEnabled()
	                ? "guest band <= normal, GPU thread above normal (KYTY_UFC_GPU_PRIORITY=1)"
	                : "default guest mapping (opt in with KYTY_UFC_GPU_PRIORITY=1)");
	SetCurrentThreadName("Main");
	StartThreadCpuSampler();
#endif
}

Thread::Thread(thread_func_t func, void* arg)
    : m_thread(std::make_unique<ThreadPrivate>(func, arg)) {
	while (!m_thread->started) {
		Common::Thread::SleepMicro(1000);
	}
}

Thread::~Thread() {
	EXIT_IF(!m_thread->finished && !m_thread->auto_delete);

	m_thread.reset();
}

void Thread::Join() {
	EXIT_IF(m_thread->finished || m_thread->auto_delete);

	m_thread->m_thread.join();

	m_thread->finished = true;
}

void Thread::Detach() {
	EXIT_IF(m_thread->finished || m_thread->auto_delete);

	m_thread->auto_delete = true;
	m_thread->m_thread.detach();
}

void Thread::SleepMicro(uint32_t micros) {
#if KYTY_PLATFORM == KYTY_PLATFORM_WINDOWS
	SleepHighResolution100ns(static_cast<uint64_t>(micros) * 10);
#elif defined(KYTY_POSIX_HIGH_RES_SLEEP)
	SleepHighResolutionNanos(static_cast<uint64_t>(micros) * 1000);
#else
	std::this_thread::sleep_for(std::chrono::microseconds(micros));
#endif
}

void Thread::SleepNano(uint64_t nanos) {
#if KYTY_PLATFORM == KYTY_PLATFORM_WINDOWS
	SleepHighResolution100ns((nanos + 99) / 100);
#elif defined(KYTY_POSIX_HIGH_RES_SLEEP)
	SleepHighResolutionNanos(nanos);
#else
	std::this_thread::sleep_for(std::chrono::nanoseconds(nanos));
#endif
}

bool Thread::IsMainThread() {
	return g_main_thread == std::this_thread::get_id();
}

std::string Thread::GetId() const {
	std::stringstream ss;
	ss << m_thread->m_thread.get_id();
	return ss.str();
}

int Thread::GetUniqueId() const {
	return m_thread->unique_id;
}

std::string Thread::GetThreadId() {
	std::stringstream ss;
	ss << std::this_thread::get_id();
	return ss.str();
}

Mutex::Mutex(): m_mutex(std::make_unique<MutexPrivate>()) {}

Mutex::~Mutex() {
	m_mutex.reset();
}

void Mutex::Lock() {
#if KYTY_PLATFORM == KYTY_PLATFORM_WINDOWS
	EnterCriticalSection(&m_mutex->m_cs);
#else
	m_mutex->m_mutex.lock();
#endif
}

void Mutex::Unlock() {
#if KYTY_PLATFORM == KYTY_PLATFORM_WINDOWS
	LeaveCriticalSection(&m_mutex->m_cs);
#else
	m_mutex->m_mutex.unlock();
#endif
}

bool Mutex::TryLock() {
#if KYTY_PLATFORM == KYTY_PLATFORM_WINDOWS
	return (TryEnterCriticalSection(&m_mutex->m_cs) != 0);
#else
	return m_mutex->m_mutex.try_lock();
#endif
}

CondVar::CondVar(): m_cond_var(std::make_unique<CondVarPrivate>()) {}

CondVar::~CondVar() {
	m_cond_var.reset();
}

void CondVar::Wait(Mutex* mutex) {
#if KYTY_PLATFORM != KYTY_PLATFORM_WINDOWS
	std::unique_lock<std::recursive_mutex> cpp_lock(mutex->m_mutex->m_mutex, std::adopt_lock_t());
#endif
	auto poll_callback = [&] {
		auto* callback = g_cond_wait_poll_callback;
		if (callback == nullptr) {
			return;
		}
#if KYTY_PLATFORM == KYTY_PLATFORM_WINDOWS
		LeaveCriticalSection(&mutex->m_mutex->m_cs);
		callback();
		EnterCriticalSection(&mutex->m_mutex->m_cs);
#else
		cpp_lock.unlock();
		callback();
		cpp_lock.lock();
#endif
	};
#if KYTY_PLATFORM == KYTY_PLATFORM_WINDOWS
	static auto func = ResolveSleepConditionVariableCS();
	EXIT_NOT_IMPLEMENTED(func == nullptr);
	if (g_cond_wait_poll_callback == nullptr) {
		func(&m_cond_var->m_cv, &mutex->m_mutex->m_cs, INFINITE);
	} else {
		if (func(&m_cond_var->m_cv, &mutex->m_mutex->m_cs, 10) == 0 &&
		    GetLastError() == ERROR_TIMEOUT) {
			poll_callback();
		}
	}
#else
	if (g_cond_wait_poll_callback == nullptr) {
		m_cond_var->m_cv.wait(cpp_lock);
	} else {
		if (m_cond_var->m_cv.wait_for(cpp_lock, std::chrono::microseconds(10000)) ==
		    std::cv_status::timeout) {
			poll_callback();
		}
	}
	cpp_lock.release();
#endif
}

void CondVar::SetWaitPollCallback(wait_poll_func_t callback) {
	g_cond_wait_poll_callback = callback;
}

bool CondVar::WaitFor(Mutex* mutex, uint32_t micros) {
	bool ok = false;
#if KYTY_PLATFORM != KYTY_PLATFORM_WINDOWS
	std::unique_lock<std::recursive_mutex> cpp_lock(mutex->m_mutex->m_mutex, std::adopt_lock_t());
#endif
#if KYTY_PLATFORM == KYTY_PLATFORM_WINDOWS
	// The kernel's timeouts are in microseconds (SceKernelUseconds; HR timers in nanoseconds),
	// but SleepConditionVariableCS counts whole milliseconds on a 1 ms tick: a wait ended up to
	// ~1.3 ms late, and never in less than 1 ms. The condition variable now sleeps only until
	// ~1 ms before the deadline; the rest is slept outside it with the high-resolution timer in
	// slices of at most 0.5 ms. Each slice returns as a spurious wakeup: every caller re-checks
	// its condition and its own deadline, so a signal in that stretch is seen within a slice.
	static auto func = ResolveSleepConditionVariableCS();
	EXIT_NOT_IMPLEMENTED(func == nullptr);
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::microseconds(micros);
	if (micros >= 2000) {
		if (func(&m_cond_var->m_cv, &mutex->m_mutex->m_cs, (micros - 1000) / 1000) != 0 ||
		    GetLastError() != ERROR_TIMEOUT) {
			return true;
		}
	}
	const auto remaining = std::chrono::duration_cast<std::chrono::microseconds>(
	    deadline - std::chrono::steady_clock::now());
	if (remaining.count() <= 0) {
		return false;
	}
	LeaveCriticalSection(&mutex->m_mutex->m_cs);
	SleepHighResolution100ns(static_cast<uint64_t>(std::min<int64_t>(remaining.count(), 500)) * 10u);
	EnterCriticalSection(&mutex->m_mutex->m_cs);
	ok = std::chrono::steady_clock::now() < deadline;
#else
	ok = (m_cond_var->m_cv.wait_for(cpp_lock, std::chrono::microseconds(micros)) ==
	      std::cv_status::no_timeout);
	cpp_lock.release();
#endif
	return ok;
}

void CondVar::Signal() {
#if KYTY_PLATFORM == KYTY_PLATFORM_WINDOWS
	static auto func = ResolveWakeConditionVariable();
	EXIT_NOT_IMPLEMENTED(func == nullptr);
	func(&m_cond_var->m_cv);
#else
	m_cond_var->m_cv.notify_one();
#endif
}

void CondVar::SignalAll() {
#if KYTY_PLATFORM == KYTY_PLATFORM_WINDOWS
	static auto func = ResolveWakeAllConditionVariable();
	EXIT_NOT_IMPLEMENTED(func == nullptr);
	func(&m_cond_var->m_cv);
#else
	m_cond_var->m_cv.notify_all();
#endif
}

int Thread::GetThreadIdUnique() {
	static thread_local int tid = ++g_thread_counter;
	return tid;
}

} // namespace Common
