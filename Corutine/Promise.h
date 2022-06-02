#pragma once

#include <future>
#include <functional>
#include <assert.h>
#include <optional>

#if defined(__clang__)
#include "ClangCoroutine.h"
#else
#include <coroutine>
#endif

namespace Coroutine
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

	enum class EStatus
	{
		Suspended,
		Resuming,
		Done,
		Disconnected
	};

	class VeryBaseTask{};
	struct VeryBaseAsync
	{
		// ReturnType
		// Move ctor
		// void Start()
		// bool IsReady() const
		// [ReturnType != void] ReturnType ConsumeResult()
	};

	template <typename AsyncType, typename PromiseType>
	struct AsyncAwaiter
	{
		using HandleType = std::coroutine_handle<PromiseType>;
		using ReturnType = typename AsyncType::ReturnType;
		AsyncType Functor;

		AsyncAwaiter(AsyncType&& InFn) : Functor(std::forward<AsyncType>(InFn)) {}

		constexpr bool await_ready() const noexcept { return false; }

		void await_suspend(HandleType Handle) noexcept
		{
			Functor.Start();
			assert(Handle);
			Handle.promise().SetFunc([this]() -> bool { return Functor.IsReady(); });
		}

		auto await_resume() noexcept 
		{ 
			if constexpr (!std::is_void_v<ReturnType>)
			{
				return Functor.ConsumeResult();
			}
		}
	};

	template <typename TaskType, typename ReturnType, typename PromiseType>
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
			auto Guard = MakeFnGuard([&]() { InnerTask.Reset(); });
			if constexpr (!std::is_void_v<ReturnType>)
			{
				return InnerTask.Consume();
			}
		}
	};

	template <typename Return, typename PromiseType>
	struct FutureAwaiter
	{
		using HandleType = std::coroutine_handle<PromiseType>;

		std::future<Return> Future;

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
			if constexpr (std::is_void_v<Return>)
			{
				Future.get();
				return;
			}
			else
			{
				if (Future.valid())
				{
					return std::optional<Return>(Future.get());
				}
				else
				{
					return std::optional<Return>{};
				}
			}
		}
	};

	template <typename Return, typename Yield, typename PromiseType, typename TaskType> 
	class PromiseBase
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
		auto await_transform(std::suspend_never InAwaiter)
		{
			return InAwaiter;
		}

		auto await_transform(std::suspend_always InAwaiter)
		{
			return InAwaiter;
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

		template <typename AsyncType, std::enable_if_t<std::is_base_of_v<VeryBaseAsync, AsyncType>, int> = 0>
		auto await_transform(AsyncType&& InAsync)
		{
			return AsyncAwaiter<AsyncType, PromiseType>{std::forward<AsyncType>(InAsync)};
		}

		template <typename U>
		auto await_transform(std::future<U>&& InReadyFunc)
		{
			return FutureAwaiter<U, PromiseType>{
				std::forward<std::future<U>>(InReadyFunc)};
		}

		template <typename InnerTaskType, std::enable_if_t<std::is_base_of_v<VeryBaseTask, InnerTaskType>, int> = 0>
		auto await_transform(InnerTaskType&& InTask)
		{
			return TaskAwaiter<InnerTaskType, typename InnerTaskType::ReturnType, PromiseType>{
				std::forward<InnerTaskType>(InTask)};
		}
	};

	template <typename Return, typename Yield, typename PromiseType, typename TaskType>
	class PromiseReturn : public PromiseBase<Return, Yield, PromiseType, TaskType>
	{
		std::optional<Return> Value;

	public:
		void return_value(const Return& InValue)
		{
			Value = InValue;
		}
		void return_value(Return&& InValue)
		{
			Value = std::forward<Return>(InValue);
		}
		void return_value(std::optional<Return>&& InValue)
		{
			Value = std::forward<std::optional<Return>>(InValue);
		}
		std::optional<Return> Consume()
		{
			auto Guard = MakeFnGuard([&]() { Value.reset(); });
			return std::move(Value);
		}
	};

	template <typename Yield, typename PromiseType, typename TaskType>
	class PromiseReturn<void, Yield, PromiseType, TaskType> : public PromiseBase<void, Yield, PromiseType, TaskType>
	{
	public:
		void return_void() {}
	};

	template <typename Return, typename Yield, typename PromiseType, typename TaskType>
	class Promise : public PromiseReturn<Return, Yield, PromiseType, TaskType>
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

	template <typename Return, typename PromiseType, typename TaskType>
	class Promise<Return, void, PromiseType, TaskType> : public PromiseReturn<Return, void, PromiseType, TaskType> {};
}