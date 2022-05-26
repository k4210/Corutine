#pragma once

#include "BaseTask.h"

namespace Coroutine
{
	template <typename Return, typename Yield> class PromiseForSharedTask;
	template <typename Return = void, typename Yield = void, typename PromiseType = PromiseForSharedTask<Return, Yield>> class SharedTask;

	template <typename Return, typename Yield>
	class PromiseForSharedTask : public Promise<Return, Yield, PromiseForSharedTask<Return, Yield>, SharedTask<Return, Yield>>
	{
		uint32_t RefCount = 0;

	public:
		void AddRef() { RefCount++; }
		bool RemoveRef() 
		{ 
			assert(RefCount);
			RefCount--; 
			return !RefCount; 
		}

		~PromiseForSharedTask()
		{
			assert(!RefCount);
		}
	};

	template <typename Return, typename Yield, typename PromiseType>
	class SharedTask : public BaseTask<Return, Yield, PromiseType>
	{
		using Super = BaseTask<Return, Yield, PromiseType>;
		using Super::HandleType;
		using Super::Handle;
		using Super::GetPromise;

		void AddRef() 
		{  
			if (PromiseType* P = GetPromise())
			{
				P->AddRef();
			}
		}
		void RemoveRef()
		{
			PromiseType* P = GetPromise();
			if (P && P->RemoveRef())
			{
				Handle.destroy();
				Handle = nullptr;
			}
		}

		friend PromiseBase;
		SharedTask(HandleType InHandle) : Super(InHandle) { AddRef(); }

	public:
		void Reset()
		{
			RemoveRef();
			Super::Handle = nullptr;
		}

		SharedTask() = default;
		SharedTask(SharedTask&& Other) : Super(std::move(Other.Handle))
		{
			Other.Handle = nullptr;
		}
		SharedTask& operator=(SharedTask&& Other)
		{
			RemoveRef();
			Super::Handle = std::move(Other.Handle);
			Other.Handle = nullptr;
			return *this;
		}
		SharedTask(const SharedTask& Other) : Super(Other.Handle)
		{
			AddRef();
		}
		SharedTask& operator=(const SharedTask& Other)
		{
			RemoveRef();
			Super::Handle = Other.Handle;
			AddRef();
			return *this;
		}
		~SharedTask()
		{
			Reset();
		}
	};
}