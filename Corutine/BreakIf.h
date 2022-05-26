#pragma once

#include "Promise.h"

namespace Coroutine
{
	template<typename TaskType, typename Func, typename YieldType = TaskType::YieldType, typename ReturnType = TaskType::ReturnType>
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
			if constexpr (!std::is_void_v<YieldType>)
			{
				std::optional<YieldType> Result = InnerTask.ConsumeYield();
				if (Result)
				{
					co_return std::move(Result.value());
				}
			}
			const EStatus Status = InnerTask.Status();
			if constexpr (!std::is_void_v<ReturnType>)
			{
				if (Status == EStatus::Done)
				{
					std::optional<ReturnType> Result = InnerTask.Consume();
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
}