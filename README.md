# Corutine
Simple, cheap Task library. Created for learning purpose. Inspired by https://github.com/westquote/SquidTasks.

User API:

	enum class EStatus
	{
		Suspended,
		Resuming,
		Done,
		Disconnected
	};

	template <typename Ret> class Task
	{
		...
	public:
		// Obtains the return value. Returns value only once, after the task is done. 
		// Any next call will return the empty value. 
		template <typename U = Ret, typename std::enable_if_t<!std::is_void<U>::value>* = nullptr>
		std::optional<Ret> Consume();
  
		// Detach the task from corutine. If the corutine is referenced by other task it remains alive. 
		void Reset();
		
		// Resume execution (if the connected corutine is suspended).
		void Resume();
		
		EStatus Status() const;
	}

	Task<int> SampleUsage() 
	{ 
		co_await []() -> bool {...};
		co_await Task<void>{};
		std::optional<float> v1 = co_await std::future<float>{};
		std::optional<float> v2 = co_await BreakIf<float>(Task<float>{}, []() -> bool {...});
		co_return 32; 
	}

  
