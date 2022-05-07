#include "Task.h"
#include <iostream>

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
	std::cout << '\t' << std::forward<Arg>(arg);
	std::cout << std::endl;
}

template <typename Arg, typename... Args>
void Log(Arg&& arg, Args&&... args)
{
	std::cout << '\t' << std::forward<Arg>(arg);
	((std::cout << ", " << std::forward<Args>(args)), ...);
	std::cout << std::endl;
}


Task<> Test0()
{
	std::cout << "start\n";
	auto Guard = MakeFnGuard([]() { std::cout << "end\n"; });
	co_await std::suspend_always{};
	std::cout << "inside\n";
}

void RunTest_0()
{
	std::cout << "TEST 0" << std::endl;
	Task<> t;
	t = Test0();
	Log(StatusToStr(t.Status()));
	t.Resume();
	Log(StatusToStr(t.Status()));
	t.Resume();
	Log(StatusToStr(t.Status()));
}

Task<int> Test1()
{
	std::cout << "start\n";
	auto Guard = MakeFnGuard([]() { std::cout << "end\n"; });
	co_await std::suspend_always{};
	std::cout << "inside\n";
	co_return 32;
}

void RunTest_10()
{
	std::cout << "TEST 1.0" << std::endl;
	Task<int> t = Test1();
	Log(StatusToStr(t.Status()));
	t.Resume();
	Log(StatusToStr(t.Status()));
	t.Resume();
	Log(StatusToStr(t.Status()));
	Log(t.Consume().value_or(-1));
	Log(t.Consume().value_or(-1));
}

void RunTest_11()
{
	std::cout << "TEST 1.1" << std::endl;
	Task<int> t = Test1();
	Log(StatusToStr(t.Status()));
	t.Resume();
	Log(StatusToStr(t.Status()));
	t.Cancel();
	Log(StatusToStr(t.Status()));
	Log(t.Consume().value_or(-1));
}

void RunTest_12()
{
	std::cout << "TEST 1.2" << std::endl;
	Task<int> t = Test1();
	Log(StatusToStr(t.Status()));
	t.Resume();
	Log(StatusToStr(t.Status()));
	t.Reset();
	Log(StatusToStr(t.Status()));
	Log(t.Consume().value_or(-1));
}

int Test2GlobalVar = 0;
Task<> Test2()
{
	std::cout << "start\n";
	auto Guard = MakeFnGuard([]() { std::cout << "end\n"; });
	co_await[&]() { return Test2GlobalVar == 1; };
	std::cout << "inside\n";
}

void RunTest_20()
{
	std::cout << "TEST 2.0" << std::endl;
	Test2GlobalVar = 0;
	Task<> t = Test2();
	t.Resume();
	Log(StatusToStr(t.Status()));
	t.Resume();
	Log(StatusToStr(t.Status()));
	Test2GlobalVar = 1;
	t.Resume();
	Log(StatusToStr(t.Status()));
}

Task<int> TestHelper()
{
	co_await std::suspend_always{};
	std::cout << "TestHelper inside 1\n";
	co_await std::suspend_always{};
	std::cout << "TestHelper inside 2\n";
	co_return 32;
}

Task<> Test3()
{
	std::cout << "start\n";
	auto Guard = MakeFnGuard([]() { std::cout << "end\n"; });
	std::optional<int> val = co_await TestHelper();
	std::cout << "Value from TestHelper: " << *val << std::endl;
}

void RunTest_30()
{
	std::cout << "TEST 3.0" << std::endl;

	Task<> t = Test3();
	t.Resume();
	Log(StatusToStr(t.Status()));
	t.Resume();
	Log(StatusToStr(t.Status()));
	t.Resume();
	Log(StatusToStr(t.Status()));
}

void RunTest_40()
{
	std::cout << "TEST 3.0" << std::endl;
	bool bCancel = false;
	Task<int> t = CancelIf(TestHelper(), [&]() {return bCancel; });
	t.Resume();
	Log(StatusToStr(t.Status()));
	bCancel = true;
	t.Resume();
	Log(StatusToStr(t.Status()));
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
	return 0;
}