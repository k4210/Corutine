template<typename Ret>
Task<Ret> CancelIf(Task<Ret> InnerTask, std::function<bool()> Fn)
{
	while (true)
	{
		if (Fn())
		{
			InnerTask.Cancel();
			co_await CancelTask{};
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

void TaskBase::TryAddRef()
{
	if (PromiseBase* Promise = GetBasePromise())
	{
		Promise->AddRef();
	}
}
void TaskBase::TryRemoveRef()
{
	if (PromiseBase* Promise = GetBasePromise())
	{
		const bool bDestroy = Promise->RemoveRef();
		if (bDestroy)
		{
			TypelessHandle.destroy();
			InnerClear();
		}
	}
}
void TaskBase::Cancel()
{
	if (PromiseBase* Promise = GetBasePromise())
	{
		Promise->Cancel();
	}
}
void TaskBase::Resume()
{
	if (PromiseBase* Promise = GetBasePromise())
	{
		Promise->Resume();
	}
}
EStatus TaskBase::Status() const
{
	const PromiseBase* Promise = GetBasePromise();
	return Promise ? Promise->Status() : EStatus::Disconnected;
}

void PromiseBase::Cancel()
{
	if (State != EStatus::Done)
	{
		State = EStatus::Canceled;
		ReadyFunc = nullptr;
		if (SubTask)
		{
			SubTask->Cancel();
			SubTask.reset();
		}
	}
}
void PromiseBase::Resume()
{
	assert(State != EStatus::Resuming);
	if (State != EStatus::Suspended)
	{
		return;
	}

	if (ReadyFunc && !ReadyFunc())
	{
		return;
	}
	ReadyFunc = nullptr;

	if (SubTask)
	{
		SubTask->Resume();
		if (SubTask->Status() != EStatus::Suspended)
		{
			SubTask.reset();
		}
		else
		{
			return;
		}
	}

	{
		assert(TypelessHandle);
		State = EStatus::Resuming;
		TypelessHandle.resume();
		if (TypelessHandle.done())
		{
			State = EStatus::Done;
		}
		else if (State == EStatus::Resuming)
		{
			State = EStatus::Suspended;
		}
	}
}

template <typename Ret, typename PromiseType>
struct TaskAwaiter
{
	Task<Ret> InnerTask;
	bool await_ready() noexcept
	{
		const EStatus Status = InnerTask.Status();
		assert(Status != EStatus::Resuming);
		return Status != EStatus::Suspended;
	}
	bool await_suspend(std::coroutine_handle<PromiseType> Handle) noexcept
	{
		InnerTask.Resume();
		const EStatus Status = InnerTask.Status();
		assert(Status != EStatus::Resuming);
		const bool bSuspend = Status == EStatus::Suspended;
		if (bSuspend)
		{
			assert(Handle);
			PromiseType& Promise = Handle.promise();
			Promise.SetSubTask(InnerTask);
		}
		return bSuspend;
	}
	auto await_resume() noexcept
	{
		if constexpr (!std::is_void_v<Ret>)
		{
			return InnerTask.Consume();
		}
	}
};

template <typename Ret, typename PromiseType>
struct FutureAwaiter
{
	std::future<Ret> Future;

	bool IsReady() const
	{
		return !Future.valid()
			|| (Future.wait_for(std::chrono::seconds(0)) == std::future_status::ready);
	}

	bool await_ready() noexcept
	{
		return IsReady();
	}

	bool await_suspend(std::coroutine_handle<PromiseType> Handle) noexcept
	{
		auto ReadyLambda = [this]() -> bool { return IsReady(); };
		assert(Handle);
		Handle.promise().SetReadyFunc(ReadyLambda);
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

template <typename Ret>
class TPromiseBase : public PromiseBase
{
	Promise<Ret>* GetPromise() { return static_cast<Promise<Ret>*>(this); }
public:
	std::coroutine_handle<Promise<Ret>> GetHandle()
	{
		return std::coroutine_handle<Promise<Ret>>::from_promise(*GetPromise());
	}

	TPromiseBase()
	{
		TypelessHandle = GetHandle();
	}

	Task<Ret> get_return_object() noexcept
	{
		return Task<Ret>(GetHandle(), GetPromise());
	}

	template <typename U>
	auto await_transform(Task<U>&& InTask)
	{
		return TaskAwaiter<U, Promise<Ret>>{std::forward<Task<U>>(InTask)};
	}

	struct SuspendIf
	{
		SuspendIf(bool in_suspend) : m_suspend(in_suspend) {}
		bool await_ready() noexcept { return !m_suspend; }
		void await_suspend(std::coroutine_handle<>) noexcept {}
		void await_resume() noexcept {}

	private:
		bool m_suspend;
	};

	auto await_transform(std::suspend_never in_awaiter)
	{
		return in_awaiter;
	}

	auto await_transform(std::suspend_always in_awaiter)
	{
		return in_awaiter;
	}

	auto await_transform(CancelTask)
	{
		Cancel();
		return std::suspend_always{};
	}

	auto await_transform(const std::function<bool()>& InReadyFunc)
	{
		const bool bReady = InReadyFunc();
		if (!bReady)
		{
			assert(!ReadyFunc);
			ReadyFunc = InReadyFunc;
		}
		return SuspendIf(!bReady); // Suspend if the function isn't already ready
	}

	auto await_transform(std::function<bool()>&& InReadyFunc)
	{
		const bool bReady = InReadyFunc();
		if (!bReady)
		{
			assert(!ReadyFunc);
			SetReadyFunc(InReadyFunc);
		}
		return SuspendIf(!bReady); // Suspend if the function isn't already ready
	}

	template <typename U>
	auto await_transform(std::future<U()>&& InReadyFunc)
	{
		return FutureAwaiter<U, Promise<Ret>>{std::forward(InReadyFunc)};
	}
};

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
}

template <typename Ret>
class Promise : public TPromiseBase<Ret>
{
	std::optional<Ret> Value;
public:
	void return_value(const Ret& InValue)
	{
		assert(!Value);
		Value = InValue;
	}
	void return_value(Ret&& InValue)
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
class Promise<void> : public TPromiseBase<void>
{
public:
	void return_void() {}
};

Task<void> WaitFor(std::function<bool()> Fn)
{
	co_await Fn;
}