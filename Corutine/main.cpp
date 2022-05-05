#include <coroutine>
#include <future>
#include <iostream>
#include <functional>
#include <assert.h>

template <typename Ret> class PromiseBase;
template <typename Ret> class Promise;

enum EPromiseStatus
{
	Suspended,
	Canceled,
	Reasuming,
	Done,
};

template <typename Fn> auto MakeFnGuard(Fn in_fn)
{
	class FunctionGuard
	{
	public:
		FunctionGuard(Fn in_fn) : m_fn(std::move(in_fn)) {}
		~FunctionGuard() { m_fn(); }
	private:
		Fn m_fn;
	};

	return FunctionGuard<Fn>(std::move(in_fn));
}

template <typename Ret = void>
class Task
{
public:
	using PromiseType = Promise<Ret>;
	using promise_type = PromiseType;
private:
	friend PromiseBase<Ret>;
	std::coroutine_handle<PromiseType> Handle;

	Task(std::coroutine_handle<PromiseType>&& InHandle) : Handle(InHandle) {}
	void TryAddRef()
	{
		if (Handle)
		{
			Handle.promise().AddRef();
		}
	}
	void TryRemoveRef()
	{
		if (Handle)
		{
			const bool bDestroy = Handle.promise().RemoveRef();
			if (bDestroy)
			{
				Handle.destroy();
				Handle = nullptr;
			}
		}
	}
public:
	Task() = default;
	Task(const Task& Other) : Handle(Other.Handle)
	{
		TryAddRef();
	}
	Task(Task&& Other) : Handle(std::move(Other.Handle))
	{
		Other.Handle = nullptr;
	}
	~Task()
	{
		TryRemoveRef();
	}
	Task& operator=(const Task& Other)
	{
		TryRemoveRef();
		Handle = Other.Handle;
		TryAddRef();
		return *this;
	}
	Task& operator=(Task&& Other)
	{
		TryRemoveRef();
		Handle = std::move(Other.Handle);
		Other.Handle = nullptr;
		return *this;
	}

	void Reset()
	{
		TryRemoveRef();
		Handle = nullptr;
	}
	void Cancel()
	{
		if (Handle)
		{
			Handle.promise().Cancel();
		}
	}
	void Resume()
	{
		if (Handle)
		{
			Handle.promise().Resume();
		}
	}
	std::optional<EPromiseStatus> Status() const
	{
		return Handle ? Handle.promise().Status() : std::optional<EPromiseStatus>{};
	}

	template <typename U = Ret, typename std::enable_if_t<!std::is_void<U>::value>* = nullptr>
	std::optional<Ret> Consume()
	{
		return Handle ? Handle.promise().Consume() : std::optional<Ret>{};
	}
};

template <typename Ret>
class PromiseBase
{
	int RefCount = 0;
	EPromiseStatus State = EPromiseStatus::Suspended;
	std::function<bool()> ReadyFunc;
	//std::optional<TaskBase> SubTask;

public:
	using PromiseType = Promise<Ret>;

	~PromiseBase()
	{
		assert(!RefCount);
	}

	std::suspend_always initial_suspend() noexcept { return {}; }
	std::suspend_always final_suspend() noexcept { return {}; }

	std::coroutine_handle<PromiseType> GetHandle()
	{
		return std::coroutine_handle<PromiseType>::from_promise(*static_cast<PromiseType*>(this));
	}

	Task<Ret> get_return_object() noexcept
	{
		return Task<Ret>(GetHandle());
	}

	void unhandled_exception() {}

public:
	void AddRef() { ++RefCount; }
	bool RemoveRef() { --RefCount; return !RefCount; } //return if should be destroyed
	EPromiseStatus Status() const { return State; }
	void Cancel()
	{
		if (State != EPromiseStatus::Done)
		{
			State == EPromiseStatus::Cancel;
		}
	}
	void Resume()
	{
		assert(State != EPromiseStatus::Reasuming);
		if (State != EPromiseStatus::Suspended)
		{
			return;
		}

		if (ReadyFunc && !ReadyFunc())
		{
			return;
		}
		ReadyFunc = nullptr;

		std::coroutine_handle<PromiseType> Handle = GetHandle();
		State = EPromiseStatus::Reasuming;
		Handle.resume();
		if (Handle.done())
		{
			State = EPromiseStatus::Done;
		}
		else if (State == EPromiseStatus::Reasuming)
		{
			State = EPromiseStatus::Suspended;
		}
	}
};

template <typename Ret>
class Promise : public PromiseBase<Ret>
{
	std::optional<Ret> Value;
public:
	void return_value(const Ret& InValue) // Copy return value
	{
		assert(!Value);
		Value = InValue;
	}
	void return_value(Ret&& InValue) // Move return value
	{
		assert(!Value);
		Value = InValue;
	}
	std::optional<Ret> Consume()
	{
		auto Guard = MakeFnGuard([&]() { Value.reset(); });
		return std::move(Value);
	}
};

template <>
class Promise<void> : public PromiseBase<void>
{
public:
	void return_void()
	{
	}
};



//Task<> WaitUntil(Fn);
//Task<> CancelIf(Task<>&&, Fn);

//////////////////////

Task<> InnerSample()
{
	co_return;
}

Task<> Sample()
{
	co_await std::suspend_always();
	//co_await std::future<void>{};
	//co_await WaitUntil(Fn);
	//co_await CancelIf(InnerSample(), Fn);
	//Task<>::Cancel();
}

////////////////////////////

int main()
{
	Task<> task = Sample();
	task.Resume();
	return 0;
}