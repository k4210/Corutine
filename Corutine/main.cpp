#include <coroutine>
#include <future>
#include <iostream>
#include <functional>

template <typename Ret> class Promise;

enum EPromiseStatus
{
	Suspended,
	Canceled,
	Reasuming,
	Done,
};

template <typename tFn> auto MakeFnGuard(tFn in_fn)
{
	template <typename Fn>
	class FunctionGuard
	{
	public:
		FunctionGuard(Fn in_fn) : m_fn(std::move(in_fn)) {}
		~FunctionGuard() { m_fn(); }
	private:
		Fn m_fn;
	};
	return FunctionGuard<tFn>(std::move(in_fn));
}


template <typename Ret = void>
class Task
{
public:
	using PromiseType = Promise<Ret>;
	using promise_type = PromiseType;
private:
	friend Promise<Ret>;
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
		return Handle ? Handle.promise().Status() : {};
	}

	template <typename std::enable_if_t<!std::is_void<Ret>::value>* = nullptr>
	std::optional<Ret> Consume()
	{
		return Handle ? Handle.promise().Consume() : {};
	}
};

template <typename Ret>
class PromiseBase
{
	int RefCount = 0;
	std::function<bool()> ReadyFunc;
	EPromiseStatus State = EPromiseStatus::Suspended;
	//std::optional<TaskBase> SubTask;

public:
	using PromiseType = Promise<Ret>;

	~PromiseBase()
	{
		assert(!RefCount);
	}

	std::suspend_always initial_suspend() { return {}; }
	std::suspend_always final_suspend() { return {}; }

	std::coroutine_handle<PromiseType> get_return_object()
	{
		return std::coroutine_handle<PromiseType>::from_promise(*static_cast<PromiseType*>(this));
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
	co_await std::future<void>{};
	//co_await WaitUntil(Fn);
	//co_await CancelIf(InnerSample(), Fn);
	//Task<>::Cancel();
}

////////////////////////////

int main()
{

	return 0;
}