#pragma once

#include <coroutine>
#include <future>
#include <functional>
#include <assert.h>
#include <optional>
#include <source_location>

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

template <typename Ret, typename Yield> class Promise;
template <typename Ret, typename Yield> class Task;

enum class EStatus
{
	Suspended,
	Resuming,
	Done,
	Disconnected
};

template <typename Ret, typename Yield, typename PromiseType>
struct TaskAwaiter
{
	using HandleType = std::coroutine_handle<PromiseType>;

	Task<Ret, Yield> InnerTask;

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

template <typename Ret, typename Yield> class PromiseBase
{
public:
	using PromiseType = Promise<Ret, Yield>;
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
	std::source_location Location;
	std::suspend_always initial_suspend() noexcept { return {}; }
	std::suspend_always final_suspend() noexcept { return {}; }
	void unhandled_exception() {}
	Task<Ret, Yield> get_return_object() noexcept
	{
		return Task<Ret, Yield>(GetHandle());
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
	auto await_transform(std::source_location InLocation)
	{
		Location = std::move(InLocation);
		return std::suspend_never{};
	}

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

	template <typename U, typename Y>
	auto await_transform(Task<U, Y>&& InTask)
	{
		return TaskAwaiter<U, Y, PromiseType>{
			std::forward<Task<U, Y>>(InTask)};
	}

	template <typename U>
	auto await_transform(std::future<U>&& InReadyFunc)
	{
		return FutureAwaiter<U, PromiseType>{
			std::forward<std::future<U>>(InReadyFunc)};
	}
};

template <typename Ret, typename Yield>
class PromiseRet : public PromiseBase<Ret, Yield>
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

template <typename Yield>
class PromiseRet<void, Yield> : public PromiseBase<void, Yield>
{
public:
	void return_void() {}
};

template <typename Ret, typename Yield>
class Promise : public PromiseRet<Ret, Yield>
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

template <typename Ret>
class Promise<Ret, void> : public PromiseRet<Ret, void>
{};

template <typename Ret = void, typename Yield = void>
class Task
{
public:
	using PromiseType = Promise<Ret, Yield>;
	using promise_type = PromiseType;
	using HandleType = std::coroutine_handle<PromiseType>;

private:
	HandleType Handle;
	std::optional<std::source_location> Location;

	friend PromiseBase<Ret, Yield>;
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
	Task(Task&& Other, std::source_location InLocation = std::source_location::current()) : Handle(std::move(Other.Handle))
	{
		Other.Handle = nullptr;
		if (Other.Location)
		{
			Location = std::move(Other.Location);
		}
		else
		{
			Location = std::move(InLocation);
		}
	}

	firend Task& operator<<(Task&& Other, std::source_location InLocation = std::source_location::current())
	{
		Reset();
		Handle = std::move(Other.Handle);
		Other.Handle = nullptr;
		if (Other.Location)
		{
			Location = std::forward(Other.Location);
		}
		else
		{
			Location = std::move(InLocation);
		}
		return *this;
	}

	Task& operator=(const Task& Other) = delete;

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

template<typename Ret, typename Yield, typename Func>
Task<Ret, Yield> BreakIf(Task<Ret, Yield> InnerTask, Func Fn)
{
	while (true)
	{
		if (Fn())
		{
			InnerTask.Reset();
			break;
		}
		InnerTask.Resume();
		const EStatus Status = InnerTask.Status();
		if constexpr (!std::is_void_v<Ret>)
		{
			if (Status == EStatus::Done)
			{
				std::optional<Ret> Result = InnerTask.Consume();
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