#include "Task.h"
#include <iostream>

using namespace CoTask;

const char* StatusToStr(EStatus status)
{
	switch (status)
	{
		case EStatus::Canceled:		return "Canceled";
		case EStatus::Suspended:	return "Suspended";
		case EStatus::Resuming:		return "Reasuming";
		case EStatus::Done:			return "Done";
		case EStatus::Disconnected: return "Disconnected";
	}
	return "UNKNOWN";
}

template <typename Arg>
void Log(Arg&& arg)
{
	std::cout << std::forward<Arg>(arg) << std::endl;
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
	Log("TEST 0");
	Task<> t;
	Expect(EStatus::Disconnected, t.Status());
	t = []() -> Task<> { co_await std::suspend_always{}; }();
	Expect(EStatus::Suspended, t.Status());
	t.Resume();
	Expect(EStatus::Suspended, t.Status());
	t.Resume();
	Expect(EStatus::Done, t.Status());
}

void RunTest_10()
{
	Log("TEST 1.0");
	Task<int> t = []() -> Task<int> 
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
}

void RunTest_11()
{
	Log("TEST 1.1");
	Task<int> t = []() -> Task<int>
	{
		co_await std::suspend_always{};
		co_return 1;
	}();
	t.Resume();
	t.Cancel();
	Expect(EStatus::Canceled, t.Status());
	Expect(-1, t.Consume().value_or(-1));
}

void RunTest_12()
{
	Log("TEST 1.2");
	Task<int> t = []() -> Task<int>
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
	Log("TEST 2.0");
	int Test2Var = 0;
	Task<int> t = [&]() -> Task<int>
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
	Log("TEST 3.0");

	auto Test3 = []() -> Task<>
	{
		std::optional<int> val = co_await[]() -> Task<int>
		{
			co_await std::suspend_always{};
			co_await std::suspend_always{};
			co_return 1;
		}();
		Expect(1, *val);
	};

	Task<> t = Test3();
	t.Resume();
	Expect(EStatus::Suspended, t.Status());
	t.Resume();
	Expect(EStatus::Suspended, t.Status());
	t.Resume();
	Expect(EStatus::Done, t.Status());
}

void RunTest_40()
{
	Log("TEST 4.0");

	auto TestHelper = []() -> Task<int>
	{
		co_await std::suspend_always{};
		co_await std::suspend_always{};
		co_return 1;
	};

	bool bCancel = false;
	Task<int> t = CancelIf(TestHelper(), [&]() {return bCancel; });
	t.Resume();
	Expect(EStatus::Suspended, t.Status());
	bCancel = true;
	t.Resume();
	Expect(EStatus::Canceled, t.Status());
}

void RunTest_41()
{
	Log("TEST 4.1");

	auto TestHelper = []() -> Task<int>
	{
		co_await std::suspend_always{};
		co_return 1;
	};

	bool bCancel = false;
	Task<int> t = CancelIf(TestHelper(), [&]() {return bCancel; });
	t.Resume();
	Expect(EStatus::Suspended, t.Status());
	t.Resume();
	Expect(EStatus::Done, t.Status());
	Expect(1, t.Consume().value_or(-1));
}

void RunTest_50()
{
	Log("TEST 5.0");

	std::promise<int> p;
	auto TestHelper = [&]() -> Task<>
	{
		const std::optional<int> val = co_await p.get_future();
		Expect(1, val.value_or(-1));
	};

	Task<> t = TestHelper();
	t.Resume();
	Expect(EStatus::Suspended, t.Status());
	t.Resume();
	Expect(EStatus::Suspended, t.Status());
	p.set_value(1);
	t.Resume();
	Expect(EStatus::Done, t.Status());
	
}

int main()
{
	RunTest_0();
	RunTest_10();
	RunTest_11();
	RunTest_12();
	RunTest_20();
	RunTest_30();
	RunTest_40();
	RunTest_41();
	RunTest_50();
	return 0;
}