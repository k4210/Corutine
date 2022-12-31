#pragma once

#include <limits>
#include <atomic>
#include <thread>
#include <variant>
#include <array>
#include <coroutine>
#include "LockFreeQueue.h"

using ResourceId = uint16_t;
using TaskId = uint16_t;
using uint32 = uint32_t;

constexpr ResourceId kInvalidResourceId = std::numeric_limits<ResourceId>::max();
constexpr TaskId kInvalidTaskId = std::numeric_limits<TaskId>::max();

class ResourceBase;

class ResourceData
{
	ResourceBase* pointer = nullptr;
	std::atomic_uint16_t references = 0;
	std::atomic_uint16_t locks = 0;
	std::atomic<TaskId> blocked_head;

public:
	static constexpr uint32 kWriteLockMask = uint32{ 1 };
	static constexpr uint32 kReadLockMask = ~kWriteLockMask;

	uint32 GetReadLocks() const
	{
		return locks.load() & kReadLockMask;
	}

	uint32 GetWriteLock() const
	{
		return locks.load() & kWriteLockMask;
	}

	bool TryAddReadLock()
	{

	}

	bool TryAddWriteLock()
	{

	}

	void ReleaseReadLock()
	{

	}

	void ReleaseWriteLock()
	{

	}
};

class TaskData
{
	std::coroutine_handle<> coroutine;
	std::array<TaskId, 6> currently_required_locks;
	std::atomic<std::pair<TaskId, TaskId>> linked_list_node = std::pair<TaskId, TaskId>{ kInvalidTaskId, kInvalidTaskId };
};

class GlobalMap
{
public:
	static ResourceId AllocateResource();
	static void FreeResource(ResourceId id);
	static ResourceData& GetResource();

	static TaskId AllocateTask();
	static void FreeTask(TaskId id);
	static TaskData& GetTask();
};

class ResourceBase
{
	const ResourceId resource_id;

	ResourceBase()
		: resource_id(GlobalMap::AllocateResource())
	{}

	~ResourceBase()
	{
		GlobalMap::FreeResource(resource_id);
	}

	ResourceBase(const ResourceBase&) = delete;
	ResourceBase(ResourceBase&&) = delete;
	ResourceBase& operator=(const ResourceBase&) = delete;
	ResourceBase& operator=(ResourceBase&&) = delete;
};

class TaskExecutor
{
	std::atomic<TaskId> short_head;
	std::atomic<TaskId> long_head;

	std::array<std::thread, 8> Workers;
	std::atomic_flag bStopRequest;

public:

	TaskExecutor()
	{
		auto WorkerLoop = [this]()
		{
			auto PopTask = [&]() -> TaskId
			{
				// try short list
				// try long list
				return kInvalidTaskId;
			}

			while (!bStopRequest.test(std::memory_order::relaxed))
			{
				const TaskId task_id = PopTask();
				if (task_id != kInvalidTaskId)
				{
					// check required locks
					//	try lock
					//	  execute
					// else: add to resource block list
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

	~TaskExecutor()
	{
		bStopRequest.test_and_set();
		for (auto& Thread : Workers)
		{
			Thread.join();
		}
	}

	static TaskExecutor& Get()
	{
		static TaskExecutor Pool;
		return Pool;
	}
};