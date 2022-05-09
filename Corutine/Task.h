#pragma once

#include <coroutine>
#include <future>
#include <functional>
#include <assert.h>
#include <optional>

namespace CoTask
{

class PromiseBase;
template <typename Ret> class TPromiseBase;
template <typename Ret> class Promise;

enum class EStatus
{
	Suspended,
	Canceled,
	Resuming,
	Done,
	Disconnected
};

class TaskBase
{
	std::coroutine_handle<void> TypelessHandle;
	// The promise could be retrieved from the handle, but the type would not match, 
	// so that could be an undefined behaviour.
	PromiseBase* PromiseBasePtr = nullptr;

	void InnerClear()
	{
		TypelessHandle = nullptr;
		PromiseBasePtr = nullptr;
	}

protected:
	void TryAddRef();
	void TryRemoveRef();

	TaskBase(std::coroutine_handle<void> InHandle, PromiseBase* InPromise)
		: TypelessHandle(InHandle), PromiseBasePtr(InPromise)
	{
		TryAddRef();
	}

	PromiseBase* GetBasePromise() 
	{ 
#if 0 
		// If we don't want to cache PromiseBasePtr, this code could be use. 
		// It could result in undefined behaviour, because different promise type.
		void* HandleAdress = TypelessHandle.address();
		std::coroutine_handle<PromiseBase> Handle =
			std::coroutine_handle<PromiseBase>::from_address(HandleAdress);
		return Handle ? &Handle.promise() : nullptr;
#endif

		return TypelessHandle ? PromiseBasePtr : nullptr;

	}
	const PromiseBase* GetBasePromise() const 
	{ 
		return TypelessHandle ? PromiseBasePtr : nullptr;;
	}
	std::coroutine_handle<void> GetTypelessHandle() { return TypelessHandle; }

public:
	// Detach the task from corutine. 
	// If the corutine is referenced by other task it remains alive. 
	void Reset()
	{
		TryRemoveRef();
		InnerClear();
	}

	// Cancels the corutine without detaching.
	void Cancel();

	// Resume execution (if the connected corutine is suspended).
	void Resume();

	EStatus Status() const;

	TaskBase() {}
	TaskBase(const TaskBase& Other)
		: TypelessHandle(Other.TypelessHandle)
		, PromiseBasePtr(Other.PromiseBasePtr)
	{
		TryAddRef();
	}
	TaskBase(TaskBase&& Other) noexcept
		: TypelessHandle(std::move(Other.TypelessHandle))
		, PromiseBasePtr(std::move(Other.PromiseBasePtr))
	{
		Other.InnerClear();
	}
	~TaskBase()
	{
		TryRemoveRef();
	}
	TaskBase& operator=(const TaskBase& Other)
	{
		TryRemoveRef();
		TypelessHandle = Other.TypelessHandle;
		PromiseBasePtr = Other.PromiseBasePtr;
		TryAddRef();
		return *this;
	}
	TaskBase& operator=(TaskBase&& Other) noexcept
	{
		TryRemoveRef();
		TypelessHandle = std::move(Other.TypelessHandle);
		PromiseBasePtr = std::move(Other.PromiseBasePtr);
		Other.InnerClear();
		return *this;
	}
};

class PromiseBase
{
protected:
	int RefCount = 0;
	EStatus State = EStatus::Suspended;
	std::function<bool()> ReadyFunc;
	std::optional<TaskBase> SubTask;
	// The handle could be retrieved from the promise, but the type would not match, 
	// so that could be an undefined behaviour.
	std::coroutine_handle<void> TypelessHandle;

public:
	~PromiseBase() { assert(!RefCount); }
	std::coroutine_handle<void> GetTypelessHandle() 
	{ 
#if 0
		// If we don't want to cache TypelessHandle, this code could be use. 
		// It could result in undefined behaviour, because different promise type.
		return std::coroutine_handle<PromiseBase>::from_promise(*this);
#endif
		return TypelessHandle; 
	}
	std::suspend_always initial_suspend() noexcept { return {}; }
	std::suspend_always final_suspend() noexcept { return {}; }
	void unhandled_exception() {}

public:
	void SetSubTask(TaskBase InTask)
	{
		assert(!SubTask);
		SubTask = std::move(InTask);
	}
	void SetReadyFunc(std::function<bool()> InReady)
	{
		assert(!ReadyFunc);
		ReadyFunc = std::move(InReady);
	}
	void AddRef()
	{
		++RefCount;
	}
	bool RemoveRef() //return if should be destroyed
	{
		--RefCount;
		return !RefCount;
	}
	EStatus Status() const { return State; }
	void Cancel();
	void Resume();
};

template <typename Ret = void>
class Task : public TaskBase
{
public:
	using PromiseType = Promise<Ret>;
	using promise_type = PromiseType;

private:
	friend TPromiseBase<Ret>;
	Task(std::coroutine_handle<PromiseType> InHandle, PromiseType* InPromise)
		: TaskBase(InHandle, InPromise) {}

public:
	Task() : TaskBase() {}
	Task(const Task& Other) : TaskBase(Other) {}
	Task(Task&& Other) : TaskBase(std::forward<TaskBase>(Other)) {}
	Task& operator=(const Task& Other) { TaskBase::operator=(Other); return *this; }
	Task& operator=(Task&& Other) 
	{ 
		TaskBase::operator=(std::forward<TaskBase>(Other)); 
		return *this; 
	}

	// Obtains the return value. Returns value only once after the task is done.
	// Any next call will return the empty value. 
	template <typename U = Ret
		, typename std::enable_if_t<!std::is_void<U>::value>* = nullptr>
	std::optional<Ret> Consume()
	{
		void* HandleAdress = GetTypelessHandle().address();
		std::coroutine_handle<PromiseType> Handle = 
			std::coroutine_handle<PromiseType>::from_address(HandleAdress);
		return Handle ? Handle.promise().Consume() : std::optional<Ret>{};
	}
};

struct CancelTask {}; // use "co_await CancelTask{};" to cancel task from inside.

template<typename Ret>
Task<Ret> CancelIf(Task<Ret> InnerTask, std::function<bool()> Fn);

}

#include "TaskDetails.inl"