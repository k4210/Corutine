#pragma once

#include <coroutine>
#include <future>
#include <functional>
#include <assert.h>
#include <optional>

namespace CoTask
{
template <typename Fn> auto MakeFnGuard(Fn InFn)
{
	class FunctionGuard
	{
		Fn Func;
	public:
		FunctionGuard(Fn InFn) : Func(std::move(InFn)) {}
		~FunctionGuard() { Func(); }
	};

	return FunctionGuard(InFn);
};

template <typename Ret, typename Yield> class PromiseForTask;
template <typename Ret = void, typename Yield = void, typename PromiseType = PromiseForTask<Ret, Yield>> class Task;

enum class EStatus
{
	Suspended,
	Resuming,
	Done,
	Disconnected
};

template <typename Ret, typename Yield, typename TaskType, typename PromiseType>
struct TaskAwaiter
{
	using HandleType = std::coroutine_handle<PromiseType>;

	TaskType InnerTask;

	bool await_ready() noexcept
	{
		const EStatus Status = InnerTask.Status();
		assert(Status != EStatus::Resuming);
		return Status != EStatus::Suspended;
	}
	bool await_suspend(HandleType Handle) noexcept
	{
		auto ResumeTask = [this]() -> bool
		{
			InnerTask.Resume();
			const EStatus Status = InnerTask.Status();
			assert(Status != EStatus::Resuming);
			return Status != EStatus::Suspended;
		};
		const bool bSuspend = !ResumeTask();
		if (bSuspend)
		{
			assert(Handle);
			Handle.promise().SetFunc(ResumeTask);
		}
		return bSuspend;
	}
	auto await_resume() noexcept
	{
		auto Guard = MakeFnGuard([&](){ InnerTask.Reset(); });
		if constexpr (!std::is_void_v<Ret>)
		{
			return InnerTask.Consume();
		}
	}
};

template <typename Ret, typename PromiseType>
struct FutureAwaiter
{
	using HandleType = std::coroutine_handle<PromiseType>;

	std::future<Ret> Future;

	bool IsReady() const
	{
		return !Future.valid()
			|| (Future.wait_for(std::chrono::seconds(0)) == 
				std::future_status::ready);
	}

	bool await_ready() noexcept
	{
		return IsReady();
	}
	bool await_suspend(HandleType Handle) noexcept
	{
		auto ReadyLambda = [this]() -> bool 
		{ 
			return IsReady(); 
		};
		assert(Handle);
		Handle.promise().SetFunc(ReadyLambda);
		return true;
	}
	auto await_resume() noexcept
	{
		if constexpr (std::is_void_v<Ret>)
		{
			Future.get();
			return;
		}
		else
		{
			if (Future.valid())
			{
				return std::optional<Ret>(Future.get());
			}
			else
			{
				return std::optional<Ret>{};
			}
		}
	}
};

template <typename Ret, typename Yield, typename PromiseType, typename TaskType> class PromiseBase
{
public:
	using HandleType = std::coroutine_handle<PromiseType>;

private:
	std::function<bool()> Func;
	EStatus State = EStatus::Suspended;

	HandleType GetHandle()
	{ 
		return HandleType::from_promise(
			*static_cast<PromiseType*>(this));
	}

public:
	std::suspend_always initial_suspend() noexcept { return {}; }
	std::suspend_always final_suspend() noexcept { return {}; }
	void unhandled_exception() {}
	TaskType get_return_object() noexcept
	{
		return TaskType(GetHandle());
	}

public:
	void SetFunc(std::function<bool()> InFunc)
	{
		assert(!Func);
		Func = std::move(InFunc);
	}
	EStatus Status() const { return State; }
	void Resume()
	{
		assert(State != EStatus::Resuming);
		if (State != EStatus::Suspended)
		{
			return;
		}

		if (Func && !Func())
		{
			return;
		}
		Func = nullptr;

		{
			HandleType LocalHandle = GetHandle();
			assert(LocalHandle);
			State = EStatus::Resuming;
			LocalHandle.resume();
			if (LocalHandle.done())
			{
				State = EStatus::Done;
			}
			else if (State == EStatus::Resuming)
			{
				State = EStatus::Suspended;
			}
		}
	}

public:
	auto await_transform(std::suspend_never in_awaiter)
	{
		return in_awaiter;
	}

	auto await_transform(std::suspend_always in_awaiter)
	{
		return in_awaiter;
	}

	struct SuspendIf
	{
		SuspendIf(bool bInSuspend) : bSuspend(bInSuspend) {}
		bool await_ready() noexcept { return !bSuspend; }
		void await_suspend(std::coroutine_handle<>) noexcept {}
		void await_resume() noexcept {}

	private:
		bool bSuspend;
	};

	auto await_transform(const std::function<bool()>& InFunc)
	{
		const bool bSuspend = !InFunc();
		if (bSuspend)
		{
			SetFunc(InFunc);
		}
		return SuspendIf(bSuspend);
	}

	auto await_transform(std::function<bool()>&& InFunc)
	{
		const bool bSuspend = !InFunc();
		if (bSuspend)
		{
			SetFunc(std::forward<std::function<bool()>>(InFunc));
		}
		return SuspendIf(bSuspend);
	}

	template <typename U, typename Y, typename P>
	auto await_transform(Task<U, Y, P>&& InTask)
	{
		return TaskAwaiter<U, Y, Task<U, Y, P>, PromiseType>{
			std::forward<Task<U, Y, P>>(InTask)};
	}

	template <typename U>
	auto await_transform(std::future<U>&& InReadyFunc)
	{
		return FutureAwaiter<U, PromiseType>{
			std::forward<std::future<U>>(InReadyFunc)};
	}
};

template <typename Ret, typename Yield, typename PromiseType, typename TaskType>
class PromiseRet : public PromiseBase<Ret, Yield, PromiseType, TaskType>
{
	std::optional<Ret> Value;

public:
	void return_value(const Ret& InValue)
	{
		Value = InValue;
	}
	void return_value(Ret&& InValue)
	{
		Value = std::forward<Ret>(InValue);
	}
	std::optional<Ret> Consume()
	{
		auto Guard = MakeFnGuard([&]() { Value.reset(); });
		return std::move(Value);
	}
};

template <typename Yield, typename PromiseType, typename TaskType>
class PromiseRet<void, Yield, PromiseType, TaskType> : public PromiseBase<void, Yield, PromiseType, TaskType>
{
public:
	void return_void() {}
};

template <typename Ret, typename Yield, typename PromiseType, typename TaskType>
class Promise : public PromiseRet<Ret, Yield, PromiseType, TaskType>
{
	std::optional<Yield> ValueYield;
public:
	std::suspend_always yield_value(const Yield& InValue)
	{
		ValueYield = InValue;
		return {};
	}
	std::suspend_always yield_value(Yield&& InValue)
	{
		ValueYield = std::forward<Yield>(InValue);
		return {};
	}
	std::optional<Yield> ConsumeYield()
	{
		auto Guard = MakeFnGuard([&]() { ValueYield.reset(); });
		return std::move(ValueYield);
	}
};

template <typename Ret, typename PromiseType, typename TaskType>
class Promise<Ret, void, PromiseType, TaskType> : public PromiseRet<Ret, void, PromiseType, TaskType>
{};

template <typename Ret, typename Yield>
class PromiseForTask : public Promise<Ret, Yield, PromiseForTask<Ret, Yield>, Task<Ret, Yield, PromiseForTask<Ret, Yield>>>
{};

template <typename Ret, typename Yield, typename PromiseType>
class Task
{
public:
	using promise_type = PromiseType;
	using HandleType = std::coroutine_handle<PromiseType>;
	using RetType = Ret;
	using YieldType = Yield;

protected:
	HandleType Handle;

	friend PromiseBase<Ret, Yield, PromiseType, Task<Ret, Yield, PromiseType>>;
	Task(HandleType InHandle) : Handle(InHandle) {}

	PromiseType* GetPromise() const
	{
		return Handle ? &Handle.promise() : nullptr;
	}

public:
	void Reset()
	{
		if (Handle)
		{
			Handle.destroy();
			Handle = nullptr;
		}
	}

	void Resume()
	{
		if (PromiseType* Promise = GetPromise())
		{
			Promise->Resume();
		}
	}

	EStatus Status() const
	{
		const PromiseType* Promise = GetPromise();
		return Promise 
			? Promise->Status() 
			: EStatus::Disconnected;
	}

	Task() {}
	Task(Task&& Other) : Handle(std::move(Other.Handle))
	{
		Other.Handle = nullptr;
	}
	Task& operator=(Task&& Other) noexcept
	{
		Reset();
		Handle = std::move(Other.Handle);
		Other.Handle = nullptr;
		return *this;
	}
	~Task()
	{
		Reset();
	}

	// Obtains the return value. 
	// Returns value only once after the task is done.
	// Any next call will return the empty value. 
	template <typename U = Ret
		, typename std::enable_if_t<!std::is_void<U>::value>* = nullptr>
	std::optional<Ret> Consume()
	{
		PromiseType* Promise = GetPromise();
		return Promise 
			? Promise->Consume() 
			: std::optional<Ret>{};
	}

	template <typename U = Yield
		, typename std::enable_if_t<!std::is_void<U>::value>* = nullptr>
	std::optional<Yield> ConsumeYield()
	{
		PromiseType* Promise = GetPromise();
		return Promise
			? Promise->ConsumeYield()
			: std::optional<Yield>{};
	}
};

template<typename TaskType, typename Func>
TaskType BreakIf(TaskType InnerTask, Func Fn)
{
	while (true)
	{
		if (Fn())
		{
			InnerTask.Reset();
			break;
		}
		InnerTask.Resume();
		if constexpr (!std::is_void_v<TaskType::YieldType>)
		{
			std::optional<TaskType::YieldType> Result = InnerTask.ConsumeYield();
			if (Result)
			{
				co_return std::move(Result.value());
			}
		}
		const EStatus Status = InnerTask.Status();
		if constexpr (!std::is_void_v<TaskType::RetType>)
		{
			if (Status == EStatus::Done)
			{
				std::optional<TaskType::RetType> Result = InnerTask.Consume();
				if (Result)
				{
					co_return std::move(Result.value());
				}
			}
		}
		if (Status != EStatus::Suspended)
		{
			break;
		}
		co_await std::suspend_always{};
	}
}

template <typename Yield> using Generator = Task<void, Yield>;
}