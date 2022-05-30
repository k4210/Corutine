#include "UniqueTask.h"
#include "SharedTask.h"
#include "BreakIf.h"

#include <iostream>
#include <chrono>

using namespace Coroutine;
using namespace std::literals;

const char* StatusToStr(EStatus status)
{
	switch (status)
	{
		case EStatus::Suspended:	return "Suspended";
		case EStatus::Resuming:		return "Reasuming";
		case EStatus::Done:			return "Done";
		case EStatus::Disconnected: return "Disconnected";
	}
	return "UNKNOWN";
}

template <typename... Args>
void Log(Args&&... args)
{
	((std::cout << std::forward<Args>(args)), ...);
	std::cout << std::endl;
}

void Expect(EStatus Expected, EStatus Actual)
{
	if (Expected != Actual)
	{
		Log("ERROR: Expected status: ", StatusToStr(Expected), " actual status: ", StatusToStr(Actual));
		assert(false);
	}
}

void Expect(int Expected, int Actual)
{
	if (Expected != Actual)
	{
		Log("ERROR: Expected value: ", Expected, " actual value: ", Actual);
		assert(false);
	}
}

void RunTest_0()
{
	Log("TEST basic");
	UniqueTask<> t;
	Expect(EStatus::Disconnected, t.Status());
	t = []() -> UniqueTask<> { co_await std::suspend_always{}; }();
	Expect(EStatus::Suspended, t.Status());
	t.Resume();
	Expect(EStatus::Suspended, t.Status());
	t.Resume();
	Expect(EStatus::Done, t.Status());
	t.Reset();
	Expect(EStatus::Disconnected, t.Status());
	t.Resume();
	Expect(EStatus::Disconnected, t.Status());
}

void RunTest_10()
{
	Log("TEST return value");
	UniqueTask<int> t = []() -> UniqueTask<int> 
	{ 
		co_await std::suspend_always{}; 
		co_return 1;
	}();
	t.Resume();
	Expect(EStatus::Suspended, t.Status());
	t.Resume();
	Expect(EStatus::Done, t.Status());
	Expect(1, t.Consume().value_or(-1));
	Expect(-1, t.Consume().value_or(-1));
	t.Reset();
	Expect(-1, t.Consume().value_or(-1));
}

void RunTest_11()
{
	Log("TEST Reset");
	UniqueTask<int> t = []() -> UniqueTask<int>
	{
		co_await std::suspend_always{};
		co_return 1;
	}();
	t.Resume();
	t.Reset();
	Expect(EStatus::Disconnected, t.Status());
	Expect(-1, t.Consume().value_or(-1));
}

void RunTest_20()
{
	Log("TEST await lambda");
	int Test2Var = 0;
	UniqueTask<int> t = [&]() -> UniqueTask<int>
	{
		co_await[&]() { return Test2Var == 1; };
		co_return 1;
	}();
	t.Resume();
	Expect(EStatus::Suspended, t.Status());
	t.Resume();
	Expect(EStatus::Suspended, t.Status());
	Test2Var = 1;
	t.Resume();
	Expect(EStatus::Done, t.Status());
	Expect(1, t.Consume().value_or(-1));
}

void RunTest_30()
{
	Log("TEST await task");

	auto Test3 = []() -> UniqueTask<>
	{
		std::optional<int> val = co_await[]() -> UniqueTask<int>
		{
			co_await std::suspend_always{};
			co_await std::suspend_always{};
			co_return 1;
		}();
		Expect(1, *val);
	};

	UniqueTask<> t = Test3();
	t.Resume();
	Expect(EStatus::Suspended, t.Status());
	t.Resume();
	Expect(EStatus::Suspended, t.Status());
	t.Resume();
	Expect(EStatus::Done, t.Status());
}

void RunTest_40()
{
	Log("TEST CancelIf 1");

	auto TestHelper = []() -> UniqueTask<int>
	{
		co_await std::suspend_always{};
		co_await std::suspend_always{};
		co_return 1;
	};

	bool bCancel = false;
	UniqueTask<int> t = BreakIf(TestHelper(), [&]() {return bCancel; });
	t.Resume();
	Expect(EStatus::Suspended, t.Status());
	bCancel = true;
	t.Resume();
	Expect(EStatus::Done, t.Status());
}

void RunTest_41()
{
	Log("TEST CancelIf 2");

	auto TestHelper = []() -> UniqueTask<int>
	{
		co_await std::suspend_always{};
		co_return 1;
	};

	bool bCancel = false;
	UniqueTask<int> t = BreakIf(TestHelper(), [&]() {return bCancel; });
	t.Resume();
	Expect(EStatus::Suspended, t.Status());
	t.Resume();
	Expect(EStatus::Done, t.Status());
	Expect(1, t.Consume().value_or(-1));
}

void RunTest_50()
{
	Log("TEST await future");

	std::promise<int> p;
	auto TestHelper = [&]() -> UniqueTask<>
	{
		const std::optional<int> val = co_await p.get_future();
		Expect(1, val.value_or(-1));
	};

	UniqueTask<> t = TestHelper();
	t.Resume();
	Expect(EStatus::Suspended, t.Status());
	t.Resume();
	Expect(EStatus::Suspended, t.Status());
	p.set_value(1);
	t.Resume();
	Expect(EStatus::Done, t.Status());
}

void RunTest_60()
{
	Log("TEST yield");

	auto Fibonacci = [](int n) -> UniqueTask<const char*, int>
	{
		if (n == 0)
			co_return "none";

		co_yield 0;

		if (n == 1)
			co_return "just 1";

		co_yield 1;

		if (n == 2)
			co_return "only 2";

		int a = 0;
		int b = 1;

		for (int i = 2; i < n; i++)
		{
			int s = a + b;
			co_yield s;
			a = b;
			b = s;
		}
		co_return "Many!";
	};

	UniqueTask<const char*, int> t = Fibonacci(12);
	while (t.Status() == EStatus::Suspended)
	{
		t.Resume();
		std::optional<int> val = t.ConsumeYield();
		Log(val.value_or(-1));
	}
	std::optional<const char*> str = t.Consume();
	Log(str.value_or("Error"));
}

void RunTest_61()
{
	Log("TEST yield 1");

	auto Fibonacci = [](int n) -> UniqueTask<void, int>
	{
		if (n == 0)
			co_return;

		co_yield 0;

		if (n == 1)
			co_return;

		co_yield 1;

		if (n == 2)
			co_return;

		int a = 0;
		int b = 1;

		for (int i = 2; i < n; i++)
		{
			int s = a + b;
			co_yield s;
			a = b;
			b = s;
		}
		co_return;
	};

	UniqueTask<void, int> t = Fibonacci(12);
	bool bPrint = false;
	while (t.Status() == EStatus::Suspended)
	{
		t.Resume();
		bPrint = !bPrint;
		if (bPrint)
		{
			std::optional<int> val = t.ConsumeYield();
			Log(val.value_or(-1));
		}
	}
}

void RunTest_70()
{
	Log("TEST SharedTask");

	auto TestHelper = []() -> SharedTask<int>
	{
		co_await std::suspend_always{};
		co_return 1;
	};

	bool bCancel = false;
	SharedTask<int> t = BreakIf(TestHelper(), [&]() {return bCancel; });
	t.Resume();
	Expect(EStatus::Suspended, t.Status());
	SharedTask<int> t2 = std::move(t);
	Expect(EStatus::Disconnected, t.Status());
	Expect(EStatus::Suspended, t2.Status());
	{
		SharedTask<int> t3 = t2;
		Expect(EStatus::Suspended, t3.Status());
	}
	Expect(EStatus::Suspended, t2.Status());
	t2.Resume();
	Expect(EStatus::Done, t2.Status());
	Expect(1, t2.Consume().value_or(-1));
}

void RunTest_80()
{
	Log("TEST Async");

	UniqueTask<int> t = [&]() -> UniqueTask<int>
	{
		std::optional<int> Result = co_await Async([]() -> int
			{
				std::this_thread::sleep_for(500ms);
				return 1;
			});
		co_return Result.value_or(-1);
	}();
	t.Resume();

	Log("Start");
	while (t.Status() == EStatus::Suspended)
	{
		Log("Waiting");
		std::this_thread::sleep_for(50ms);
		t.Resume();
	}
	Log("Done");
	Expect(1, t.Consume().value_or(-1));
}

void RunTest_81()
{
	Log("TEST Async void");

	UniqueTask<> t = [&]() -> UniqueTask<>
	{
		co_await Async([]() { std::this_thread::sleep_for(500ms); });
	}();
	t.Resume();

	Log("Start");
	while (t.Status() == EStatus::Suspended)
	{
		Log("Waiting");
		std::this_thread::sleep_for(50ms);
		t.Resume();
	}
	Log("Done");
}

int main()
{
	RunTest_0();
	RunTest_10();
	RunTest_11();
	RunTest_20();
	RunTest_30();
	RunTest_40();
	RunTest_41();
	RunTest_50();
	RunTest_60();
	RunTest_61();
	RunTest_70();
	RunTest_80();
	RunTest_81();
	return 0;
}