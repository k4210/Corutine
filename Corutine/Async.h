#pragma once

#include <thread>
#include <variant>
#include <utility>
#include <iostream>
#include <array>
#include <deque>

#include "Promise.h"
#include "LockFreeQueue.h"

namespace MultiThread
{
	//Sync 2 unmoveable objects, so they can communicate without shader state
	class base_sync_primitive
	{
		std::atomic_flag is_locked_;

		bool inner_try_lock()
		{
			return !is_locked_.test_and_set();
		}

		void inner_unlock()
		{
			assert(is_locked_.test());
			is_locked_.clear();
		}

		base_sync_primitive(const base_sync_primitive&) = delete;
		base_sync_primitive(base_sync_primitive&&) = delete;
	public:
		base_sync_primitive() = default;

		template<typename Other>
		bool lock(Other*& other)
		{
			if (!other)
				return false;
			while (true)
			{
				if (inner_try_lock())
				{
					if (!other)
					{
						inner_unlock();
						return false;
					}

					if (other->inner_try_lock())
					{
						return true;
					}

					inner_unlock();
				}
				std::this_thread::sleep_for(0ms);//yield?
			}
		}

		void unlock(base_sync_primitive* const other)
		{
			if (other)
			{
				other->inner_unlock();
				inner_unlock();
			}
		}
	};

	class sync_guard
	{
		base_sync_primitive& sync;
		base_sync_primitive* other = nullptr;
	public:
		sync_guard(base_sync_primitive& in_sync) : sync(in_sync) {}

		template<typename Other>
		bool lock(Other*& ref_ptr_other)
		{
			if (sync.lock(ref_ptr_other))
			{
				other = ref_ptr_other;
				return true;
			}
			return false;
		}

		~sync_guard()
		{
			sync.unlock(other);
		}
	};

	// Interface for MT. 
	// Lifetime of this object should be not longer, that lifetime of objects required for the represented call. 
	// On destruction will safely handle the requested call.
	class AsyncTaskRequester : public base_sync_primitive
	{
	public:
		enum class EState
		{
			NotStarted,
			Requested,
			Done,
			Cancelled
		};

	private:
		friend class AsyncTask;

		AsyncTask* async_task = nullptr;
		std::atomic<EState> State = EState::NotStarted;

		void InitUnsafe(AsyncTask* in_task)
		{
			assert(!async_task);
			assert(State == EState::Requested);
			async_task = in_task;
		}

		void ReleaseUnsafe()
		{
			assert(async_task);
			assert(State == EState::Requested);
			async_task = nullptr;
		}

	public:
		EState GetState() const
		{
			return State.load(std::memory_order_relaxed);
		}

		template<typename Func> void Start(Func&& fn);

		bool TryCancel();

		~AsyncTaskRequester();
	};

	//
	class AsyncTask : public base_sync_primitive
	{
		std::function<void()> call;
		AsyncTaskRequester* requester = nullptr;

	public:
		AsyncTask(std::function<void()>&& in_func, AsyncTaskRequester& in_requester)
			: call(in_func), requester(&in_requester)
		{
			assert(call);
			requester->InitUnsafe(this);
		}

		bool WasForwardedUnsafe() const { return !call; }
		void ReleaseUnsafe() { requester = nullptr; }

		std::function<void()> ForwardFunction()
		{
			sync_guard guard(*this);
			if (guard.lock(requester))
			{
				std::function<void()> result;
				call.swap(result);
				return result;
			}
			return std::function<void()>{};
		}

		~AsyncTask()
		{
			sync_guard guard(*this);
			if (guard.lock(requester))
			{
				requester->ReleaseUnsafe();
			}
		}
	};

	class ThreadPool
	{
		LockFreeQueue<AsyncTask, 64> Messages;
		std::array<std::thread, 8> Workers;
		std::atomic_flag bStopRequest;

	public:
		template<typename ...Args>
		void Push(Args&&... args)
		{
			Messages.Enqueue(std::forward<Args>(args)...);
		}

		ThreadPool()
		{
			auto WorkerLoop = [this]()
			{
				while (!bStopRequest.test(std::memory_order::relaxed))
				{
					auto GetFunc = [](AsyncTask& task) { return task.ForwardFunction(); };
					std::optional<std::function<void()>> Msg = Messages.Pop(GetFunc);
					if (Msg.has_value())
					{
						if (Msg.value())
						{
							Msg.value()();
						}
					}
					else
					{
						std::this_thread::yield();
					}
				}
			};

			for (auto& Thread : Workers)
			{
				Thread = std::thread(WorkerLoop);
			}
		}

		~ThreadPool()
		{
			bStopRequest.test_and_set();
			for (auto& Thread : Workers)
			{
				Thread.join();
			}
		}

		static ThreadPool& Get()
		{
			static ThreadPool Pool;
			return Pool;
		}
	};

	template<typename Func> void AsyncTaskRequester::Start(Func&& fn)
	{
		assert(State == EState::NotStarted);
		State = EState::Requested;
		ThreadPool::Get().Push([this, Functor = std::forward<Func>(fn)]()
		{
			Functor();
			State = EState::Done;
			State.notify_one();
		}, * this);
	}

	bool AsyncTaskRequester::TryCancel()
	{
		sync_guard guard(*this);
		if (guard.lock(async_task))
		{
			if (!async_task->WasForwardedUnsafe())
			{
				async_task->ReleaseUnsafe();
				ReleaseUnsafe();
				State = EState::Cancelled;
				return true;
			}
		}
		return false;
	}

	AsyncTaskRequester::~AsyncTaskRequester()
	{
		{
			sync_guard guard(*this);
			if (guard.lock(async_task))
			{
				assert(State == EState::Requested);
				async_task->ReleaseUnsafe();
				if (!async_task->WasForwardedUnsafe())
				{
					return;
				}
			}
		}

		State.wait(EState::Requested);
	}
}

namespace Coroutine
{
	template <typename Func> struct Async : public VeryBaseAsync
	{
		using ReturnType = decltype((*(Func*)0)());
		using ValueType = std::conditional_t<!std::is_void_v<ReturnType>, std::optional<ReturnType>, std::monostate>;

		Func Functor;
		MultiThread::AsyncTaskRequester TaskSync;
		[[no_unique_address]] ValueType Result;

		Async(Func&& Fn) : Functor(std::forward<Func>(Fn)) {}

		Async(Async&& Other)
		{
			assert(Other.TaskSync.GetState() == MultiThread::AsyncTaskRequester::EState::NotStarted);
			Functor = std::move(Functor);
		}

		Async(const Async& Other) = delete;

		void Start()
		{
			TaskSync.Start([this]()
				{
					if constexpr (std::is_void_v<ReturnType>) { Functor(); }
					else { Result = Functor(); }
				});
		}
		bool IsReady() const 
		{ 
			MultiThread::AsyncTaskRequester::EState State = TaskSync.GetState();
			return State == MultiThread::AsyncTaskRequester::EState::Done
				|| State == MultiThread::AsyncTaskRequester::EState::Cancelled;
		}

		template <typename U = ReturnType, typename std::enable_if_t<!std::is_void<U>::value>* = nullptr>
		std::optional<ReturnType> ConsumeResult()
		{
			auto Guard = MakeFnGuard([&]() { Result.reset(); });
			return std::move(Result);
		}
	};
}