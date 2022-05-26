#pragma once

#include "Promise.h"

namespace Coroutine
{
	template <typename Return, typename Yield, typename PromiseType>
	class BaseTask
	{
	public:
		using promise_type = PromiseType;
		using HandleType = std::coroutine_handle<PromiseType>;
		using ReturnType = Return;
		using YieldType = Yield;

	protected:
		HandleType Handle;

		PromiseType* GetPromise() const
		{
			return Handle ? &Handle.promise() : nullptr;
		}

		BaseTask() = default;
		BaseTask(HandleType InHandle) : Handle(InHandle) {}

	public:
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
			return Promise ? Promise->Status() : EStatus::Disconnected;
		}

		// Obtains the return value. 
		// Returns value only once after the task is done.
		// Any next call will return the empty value. 
		template <typename U = Return, typename std::enable_if_t<!std::is_void<U>::value>* = nullptr>
		std::optional<Return> Consume()
		{
			PromiseType* Promise = GetPromise();
			return Promise ? Promise->Consume() : std::optional<Return>{};
		}

		template <typename U = Yield, typename std::enable_if_t<!std::is_void<U>::value>* = nullptr>
		std::optional<Yield> ConsumeYield()
		{
			PromiseType* Promise = GetPromise();
			return Promise ? Promise->ConsumeYield() : std::optional<Yield>{};
		}
	};
}