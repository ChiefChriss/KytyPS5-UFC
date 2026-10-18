#ifndef KYTY_COMMON_THREADS_H_
#define KYTY_COMMON_THREADS_H_

#include "common/common.h"

#include <memory>
#include <string>

namespace Common {

void InitializeThreads();
// Opts the calling host thread out of Windows power throttling (EcoQoS) so a background
// game window does not move emulator threads to efficiency cores; no-op elsewhere or with
// KYTY_UFC_POWER_THROTTLING=1.
void OptOutThreadPowerThrottling();
// Pins the calling host thread to the performance cores on hybrid CPUs (Windows); no-op elsewhere
// or with KYTY_UFC_PIN_GPU=0.
void PinThreadToPerformanceCores(const char* role);
// KYTY_UFC_GPU_PRIORITY=1 (opt-in, Windows): guest threads run in a host priority band at or below
// THREAD_PRIORITY_NORMAL (core-pinned workers one step lower) and the GPU command thread runs at
// THREAD_PRIORITY_ABOVE_NORMAL, so spinning guest job workers cannot starve the command processor.
[[nodiscard]] bool UfcGpuPriorityEnabled();
// Raises the calling host thread to THREAD_PRIORITY_ABOVE_NORMAL when UfcGpuPriorityEnabled();
// no-op otherwise and on non-Windows hosts.
void RaiseThreadAboveGuest(const char* role);

using thread_func_t    = void (*)(void*);
using wait_poll_func_t = void (*)();

struct ThreadPrivate;
struct MutexPrivate;
struct CondVarPrivate;

class Thread {
public:
	Thread(thread_func_t func, void* arg);
	~Thread();

	void Join();
	void Detach();

	// Once a thread has finished, the id may be reused by another thread.
	[[nodiscard]] std::string GetId() const;

	// The id is unique and can't be reused by another thread.
	[[nodiscard]] int GetUniqueId() const;

	static void SleepMicro(uint32_t micros);
	static void SleepNano(uint64_t nanos);
	static bool IsMainThread();

	// Get current thread id
	// Once a thread has finished, the id may be reused by another thread.
	static std::string GetThreadId();

	// Get current thread id
	// The id is unique and can't be reused by another thread.
	static int GetThreadIdUnique();

	KYTY_CLASS_NO_COPY(Thread);

private:
	std::unique_ptr<ThreadPrivate> m_thread;
};

class Mutex {
public:
	Mutex();
	~Mutex();

	void Lock();
	void Unlock();
	bool TryLock();

	friend class CondVar;

	KYTY_CLASS_NO_COPY(Mutex);

private:
	std::unique_ptr<MutexPrivate> m_mutex;
};

class CondVar {
public:
	CondVar();
	~CondVar();

	void Wait(Mutex* mutex);
	bool WaitFor(Mutex* mutex, uint32_t micros);
	void Signal();
	void SignalAll();

	static void SetWaitPollCallback(wait_poll_func_t callback);

	KYTY_CLASS_NO_COPY(CondVar);

private:
	std::unique_ptr<CondVarPrivate> m_cond_var;
};

class LockGuard {
public:
	using mutex_type = Mutex;

	// NOLINTNEXTLINE(google-runtime-references)
	explicit LockGuard(mutex_type& m): m_mutex(m) { m_mutex.Lock(); }

	~LockGuard() { m_mutex.Unlock(); }

	KYTY_CLASS_NO_COPY(LockGuard);

private:
	mutex_type& m_mutex;
};

} // namespace Common

#endif /* KYTY_COMMON_THREADS_H_ */
