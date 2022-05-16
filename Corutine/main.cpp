#include "Task.h"
#include <iostream>

using namespace CoTask;

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
	Task<> t;
	Expect(EStatus::Disconnected, t.Status());
	t = []() -> Task<> { co_await std::suspend_always{}; }();
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
	t.Reset();
	Expect(-1, t.Consume().value_or(-1));
}

void RunTest_11()
{
	Log("TEST Reset");
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
	Log("TEST await lambda");
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
	Log("TEST await task");

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
	Log("TEST CancelIf 1");

	auto TestHelper = []() -> Task<int>
	{
		co_await std::suspend_always{};
		co_await std::suspend_always{};
		co_return 1;
	};

	bool bCancel = false;
	Task<int> t = BreakIf(TestHelper(), [&]() {return bCancel; });
	t.Resume();
	Expect(EStatus::Suspended, t.Status());
	bCancel = true;
	t.Resume();
	Expect(EStatus::Done, t.Status());
}

void RunTest_41()
{
	Log("TEST CancelIf 2");

	auto TestHelper = []() -> Task<int>
	{
		co_await std::suspend_always{};
		co_return 1;
	};

	bool bCancel = false;
	Task<int> t = BreakIf(TestHelper(), [&]() {return bCancel; });
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
	RunTest_20();
	RunTest_30();
	RunTest_40();
	RunTest_41();
	RunTest_50();
	return 0;
}