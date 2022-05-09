# Corutine
Simple, cheap Task library. Created for learning purpose. Inspired by https://github.com/westquote/SquidTasks.

User API:

	template <typename Ret> class Task
	{
		// Obtains the return value. Returns the value only once after the task is done. 
		template <typename U = Ret, typename std::enable_if_t<!std::is_void<U>::value>* = nullptr>
		std::optional<Ret> Consume();
  
		void Reset()
		void Cancel();
		void Resume();
		EStatus Status() const;
	}

	Task<int> SampleUsage() 
	{ 
		co_await []() -> bool {...}
		co_await Task<void>{};
		std::optional<float> v1 = co_await std::future<float>{};
		std::optional<float> v2 = co_await CancelIf<float>(Task<float>{}, []() -> bool {...});
		co_return 32; 
	}

  
