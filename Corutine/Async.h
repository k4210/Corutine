#pragma once

#include <thread>
#include <variant>
#include <utility>
#include <iostream>
#include <array>
#include <deque>

#include "Promise.h"

namespace Coroutine
{
	class ThreadPool
	{
		std::deque<std::function<void()>> Messages;
		std::array<std::thread, 8> Workers;
		std::mutex Mutex;
		std::atomic_flag bStopRequest;

		std::optional<std::function<void()>> Pop()
		{
			std::lock_guard<std::mutex> Lock(Mutex);
			if (Messages.empty())
			{
				return std::optional<std::function<void()>>{};
			}
			std::optional<std::function<void()>> Result{ std::move(Messages.front()) };
			Messages.pop_front();
			return Result;
		}
	public:
		void Push(std::function<void()> Fn)
		{
			std::lock_guard<std::mutex> Lock(Mutex);
			Messages.push_back(std::move(Fn));
		}

		ThreadPool()
		{
			for (auto& Thread : Workers)
			{
				Thread = std::thread([this]()
				{
					while (!bStopRequest.test(std::memory_order::relaxed))
					{
						std::optional<std::function<void()>> Msg = Pop();
						if (Msg.has_value())
						{
							Msg.value()();
						}
						else
						{
							std::this_thread::yield();
						}
					}
				});
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

	template <typename Func> struct Async : public VeryBaseAsync
	{
		using ReturnType = decltype((*(Func*)0)());
		using ValueType = std::conditional_t<!std::is_void_v<ReturnType>, std::optional<ReturnType>, std::monostate>;

		Func Functor;
		[[no_unique_address]] ValueType Result;
		enum class EState
		{
			NotStarted,
			Running,
			Done
		};
		std::atomic<EState> State = EState::NotStarted;

		Async(Func&& Fn) : Functor(std::forward<Func>(Fn)) {}

		Async(Async&& Other)
		{
			assert(Other.State == EState::NotStarted);
			Functor = std::move(Functor);
		}

		void Start()
		{
			assert(State == EState::NotStarted);
			State = EState::Running;
			ThreadPool::Get().Push([this]()
				{
					if constexpr (std::is_void_v<ReturnType>) { Functor(); }
					else { Result = Functor(); }
					State = EState::Done;
				});
			std::cout << "Size of std::mutex: " << sizeof(std::mutex) << std::endl;
		}
		bool IsReady() const { return State == EState::Done; }

		template <typename U = ReturnType, typename std::enable_if_t<!std::is_void<U>::value>* = nullptr>
		std::optional<ReturnType> ConsumeResult()
		{
			auto Guard = MakeFnGuard([&]() { Result.reset(); });
			return std::move(Result);
		}
	};
}