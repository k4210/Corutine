#pragma once

#include <coroutine>
#include <future>
#include <functional>
#include <assert.h>
#include <optional>

class TaskBase;
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

	PromiseBase* GetBasePromise() { return PromiseBasePtr; }
	const PromiseBase* GetBasePromise() const { return PromiseBasePtr; }
	std::coroutine_handle<void> GetTypelessHandle() { return TypelessHandle; }

public:
	void Reset()
	{
		TryRemoveRef();
		InnerClear();
	}
	void Cancel();
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
	std::coroutine_handle<void> GetTypelessHandle() const { return TypelessHandle; }
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

// usage: "co_await CancelTask{}" to cancel the task.
struct CancelTask {};

Task<void> WaitFor(std::function<bool()> Fn);

template<typename Ret>
Task<Ret> CancelIf(Task<Ret> InnerTask, std::function<bool()> Fn);

#include "TaskDetails.inl"