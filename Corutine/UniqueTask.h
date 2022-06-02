#pragma once

#include "BaseTask.h"

namespace Coroutine
{
	template <typename Return, typename Yield> class PromiseForUniqueTask;
	template <typename Return = void, typename Yield = void, typename PromiseType = PromiseForUniqueTask<Return, Yield>> class UniqueTask;

	template <typename Return, typename Yield>
	class PromiseForUniqueTask : public Promise<Return, Yield, PromiseForUniqueTask<Return, Yield>, UniqueTask<Return, Yield>>
	{};

	template <typename Return, typename Yield, typename PromiseType>
	class UniqueTask : public BaseTask<Return, Yield, PromiseType>
	{
		using Super = BaseTask<Return, Yield, PromiseType>;
		using HandleType = typename Super::HandleType;
		using Super::Handle;

		friend PromiseBase<Return, Yield, PromiseType, UniqueTask>;
		UniqueTask(HandleType InHandle) : Super(InHandle) {}

	public:
		template<typename R, typename Y> using GenericTask = UniqueTask<R, Y>;

		void Reset()
		{
			if (Handle)
			{
				Handle.destroy();
				Handle = nullptr;
			}
		}

		UniqueTask() = default;
		UniqueTask(UniqueTask&& Other) : Super(std::move(Other.Handle))
		{
			Other.Handle = nullptr;
		}
		UniqueTask& operator=(UniqueTask&& Other)
		{
			Reset();
			Handle = std::move(Other.Handle);
			Other.Handle = nullptr;
			return *this;
		}
		~UniqueTask()
		{
			Reset();
		}
		UniqueTask(const UniqueTask&) = delete;
		UniqueTask& operator=(const UniqueTask& Other) = delete;
	};
}