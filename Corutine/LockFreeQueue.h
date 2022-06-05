#pragma once

#include<atomic>
#include<optional>
#include<assert.h>

template<typename T, uint32_t kSize> 
class LockFreeQueue
{
	LockFreeQueue(const LockFreeQueue&) = delete;
	LockFreeQueue& operator=(const LockFreeQueue&) = delete;
	LockFreeQueue(const LockFreeQueue&&) = delete;
	LockFreeQueue& operator=(const LockFreeQueue&&) = delete;

	struct Block
	{
		std::aligned_storage_t<sizeof(T), alignof(T)> data[kSize];
		std::atomic_flag written[kSize];
		Block* next = nullptr;
		std::atomic<uint32_t> written_num = 0;
	};

	struct alignas(8) State
	{
		Block* block = nullptr;
		uint16_t first = 0;
		uint16_t count = 0;
#ifndef NDEBUG
		uint16_t num_blocks = 0;
#endif
	};

	std::atomic<State> state_;
	std::atomic<Block*> free_list_head_ = nullptr;
	std::atomic_flag releasing_;

	void MoveToFreeList(Block* block)
	{
		//no need to check if all data were already read. It's single consumer queue
		assert(block);
		assert(!block->written_num);
		assert([&]() -> bool 
		{
			for (auto& written : block->written)
			{
				if (written.test()) 
					return false;
			}
			return true; 
		}());
		assert(!block->next);
		block->next = free_list_head_.load(std::memory_order_relaxed);
		while (!free_list_head_.compare_exchange_weak(block->next, block));
	}

	Block* GetBlockFromFreeList()
	{
		//no need to check if all data were already read. It's single consumer queue
		Block* local_head = free_list_head_.load(std::memory_order_relaxed);
		do
		{
			if (!local_head)
				return nullptr;
		} 
		while (!free_list_head_.compare_exchange_weak(local_head, local_head->next));
		local_head->next = nullptr;
		assert(!local_head->written_num);
		assert([&]() -> bool
		{
			for (auto& written : local_head->written)
			{
				if (written.test()) 
					return false;
			}
			return true;
		}());
		return local_head;
	}

	Block* GetOrAllocateFreeBlock()
	{
		Block* block = GetBlockFromFreeList();
		return block ? block : new Block();
	}

	State PrepareSpaceForNewElement()
	{
		State next_state;
		Block* new_block = nullptr;
		State prev_state = state_.load(std::memory_order_relaxed);
		auto manage_block = [&]() -> Block*
		{
			assert(prev_state.count > 0 || !prev_state.block || !prev_state.block->next);

			const bool no_room_in_prev = !prev_state.first && !!prev_state.count;
			if (no_room_in_prev || !prev_state.block)
			{
				if (!new_block)
				{
					new_block = GetOrAllocateFreeBlock();
					assert(new_block);
				}
				new_block->next = prev_state.block;
				return new_block;
			}

			//We don't need a new block, but we already allocated one.
			if (new_block)
			{	
				new_block->next = nullptr;
				MoveToFreeList(new_block);
				new_block = nullptr;
			}
			return prev_state.block;
		};
		do
		{
			next_state.block = manage_block();
			next_state.first = (prev_state.first - 1) % kSize;
			next_state.count = prev_state.count + 1;
#ifndef NDEBUG
			next_state.num_blocks = prev_state.num_blocks + (new_block ? 1 : 0);
#endif	
		} while (!state_.compare_exchange_weak(prev_state, next_state));
		return next_state;
	}

	std::optional<State> TryDecrement()
	{
		State new_state;
		State prev_state = state_.load(std::memory_order_relaxed);
		do
		{
			if (!prev_state.count)
				return {};
			new_state = prev_state;
			new_state.count = prev_state.count - 1;
		} while (!state_.compare_exchange_weak(prev_state, new_state));
		assert(new_state.first < kSize);
		return new_state;
	}

	uint32_t TryToReleaseRecursive(const uint32_t last_used_index, const uint32_t block_idx, Block*& block)
	{
		if (!block)
			return 0;
		uint32_t removed = TryToReleaseRecursive(last_used_index, block_idx + 1, block->next);
		const uint32_t first_index_in_block = block_idx * kSize;
		if ((first_index_in_block > last_used_index) &&
			!block->next &&
			!block->written_num.load(std::memory_order_relaxed))
		{
			MoveToFreeList(block);
			block = nullptr;
			removed++;
		}
		return removed;
	}

	void ReleaseEmptyBlocks()
	{
		if (releasing_.test_and_set())
			return;
		State state = state_.load(std::memory_order_relaxed);
		assert(state.block);
		const uint32_t last_used_index = state.first + state.count;
		assert(last_used_index < state.num_blocks* kSize);
		const uint32_t removed = TryToReleaseRecursive(last_used_index, 1, state.block->next);
#ifndef NDEBUG
		if (removed)
		{
			State next_state;
			do
			{
				next_state = state;
				assert(next_state.num_blocks > removed);
				next_state.num_blocks -= removed;
			} while (!state_.compare_exchange_weak(state, next_state));
		}
#endif	
		releasing_.clear();
	}

	static std::pair<Block*, uint32_t> FindPopElement(const State state)
	{
		Block* block = state.block;
		assert(block);
		const uint32_t consecutive_index = state.first + state.count;
		assert(consecutive_index < state.num_blocks* kSize);
		for (uint32_t it = kSize; it < consecutive_index; it += kSize)
		{
			block = block->next;
			assert(block);
		}

		const uint32_t index_in_block = consecutive_index % kSize;
		return std::pair<Block*, uint32_t>{block, index_in_block};
	}

	void ClearBlockElement(Block* block, uint32_t index_in_block)
	{
		block->written[index_in_block].clear();
		assert(block->written_num);
		const bool empty_block = !(--block->written_num);
		if (empty_block)
		{
			ReleaseEmptyBlocks();
		}
	}

public:
	LockFreeQueue(uint32_t initial_blocks = 3)
	{
		for (uint32_t i = 0; i < initial_blocks; i++)
		{
			MoveToFreeList(new Block());
		}
	}

	~LockFreeQueue()
	{
		for (Block* it = state_.load().block; it;)
		{
			Block* to_delete = it;
			it = it->next;
			delete to_delete;
		}

		for (Block* it = free_list_head_; it;)
		{
			Block* to_delete = it;
			it = it->next;
			delete to_delete;
		}
	}

	template<typename ...Args>
	void Enqueue(Args&&... args)
	{
		const State local_state = PrepareSpaceForNewElement();

		//WRITE DATA. Note this can be long operation and consumer may be blocked waiting for it.
		auto space = &(local_state.block->data[local_state.first]);
		T* result = ::new(space) T(std::forward<Args>(args)...);

		//MARK THE ITEM AS WRITTEN
		assert(local_state.first < kSize);
		local_state.block->written_num++;
		const bool was_set = local_state.block->written[local_state.first].test_and_set();
		assert(!was_set);
		local_state.block->written[local_state.first].notify_one();
	}

	// Use to store immovable objects
	template<typename Transform, typename ReturnType = decltype((*(Transform*)0)(*(T*)0))>
	std::optional<ReturnType> Pop(Transform func = [](T& t) -> T { return std::move(t); })
	{
		const std::optional<State> local_state = TryDecrement();
		if (!local_state)
			return {};

		auto [block, index_in_block] = FindPopElement(*local_state);

		block->written[index_in_block].wait(false);
		T& transofmable = *std::launder(reinterpret_cast<T*>(&(block->data[index_in_block])));
		std::optional<ReturnType> result(func(transofmable));
		transofmable.~T();

		ClearBlockElement(block, index_in_block);
		return result;
	}

	std::optional<T> Pop()
	{
		auto JustMove = [](T& t) -> T { return std::move(t); };
		return Pop(JustMove);
	}

	uint32_t Num() const { return state_.load(std::memory_order_relaxed).count; }

	void DeleteFreeList()
	{
		for (Block* block = GetBlockFromFreeList(); block; block = GetBlockFromFreeList())
		{
			delete block;
		}
	}
};
