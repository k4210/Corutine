#pragma once

#include "Task.h"

namespace CoTask
{
	template <typename Ret, typename Yield> class PromiseForSharedTask;

	template <typename Ret = void, typename Yield = void, typename PromiseType = PromiseForSharedTask<Ret, Yield>> class SharedTask;

	template <typename Ret, typename Yield>
	class PromiseForSharedTask : public Promise<Ret, Yield, PromiseForSharedTask<Ret, Yield>, SharedTask<Ret, Yield>>
	{
	private:
		uint32_t RefCount = 0;

	public:
		void AddRef() { RefCount++; }
		bool RemoveRef() { RefCount--; return !RefCount; }

		template <typename U, typename Y, typename P>
		auto await_transform(SharedTask<U, Y, P>&& InTask)
		{
			return TaskAwaiter<U, Y, SharedTask<U, Y, P>, PromiseForSharedTask<Ret, Yield>>{
				std::forward<SharedTask<U, Y, P>>(InTask)};
		}

		using Super = Promise<Ret, Yield, PromiseForSharedTask<Ret, Yield>, SharedTask<Ret, Yield>>;
		using Super::await_transform;
	};

	template <typename Ret, typename Yield, typename PromiseType>
	class SharedTask : public Task<Ret, Yield, PromiseType>
	{
	public:
		using Super = Task<Ret, Yield, PromiseType>;
		using HandleType = Super::HandleType;

	protected:
		void AddRef() 
		{  
			if (PromiseType* P = Super::GetPromise())
			{
				P->AddRef();
			}
		}
		void RemoveRef()
		{
			if (PromiseType* P = Super::GetPromise())
			{
				if (P->RemoveRef())
				{
					Task<Ret, Yield, PromiseType>::Reset();
				}
			}
		}

		friend PromiseBase<Ret, Yield, PromiseType, SharedTask<Ret, Yield, PromiseType>>;
		SharedTask(HandleType InHandle) : Super(InHandle) { AddRef(); }

	public:
		void Reset()
		{
			RemoveRef();
			Super::Handle = nullptr;
		}

		SharedTask() {}
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
			RemoveRef();
		}
	};
}