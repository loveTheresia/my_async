#include <chrono>
#include <coroutine>
#include <deque>
#include <thread>
#include <queue>
#include "debug.hpp"

  struct PreviousAwaiter
  {
    std::coroutine_handle<> mPrevious;

    bool await_ready() const noexcept { return false; }

    std::coroutine_handle<> await_suspend(std::coroutine_handle<> coroutine) const noexcept {
        if (mPrevious)
            return mPrevious;
        else
            return std::noop_coroutine();
    }

    void await_resume() const noexcept {}
  };

  //相当于协程的原始指针，可以直接操作协程
struct RepeatAwaiter
{
    // await_ready() 方法用于检查协程是否已经准备好。
    // 这里返回 false，意味着协程不能立即继续执行。
    bool await_ready()const noexcept {return false; }

    // await_suspend() 方法在协程挂起时调用。
    // 它接受一个协程句柄作为参数，表示当前协程的状态。
    // 如果协程已经完成（done() 返回 true），则返回一个无操作的协程句柄。
    // 否则，返回传入的协程句柄，表示协程可以继续执行。
    std::coroutine_handle<> await_suspend(std::coroutine_handle<> coroutine)const noexcept
    {
        // 检查协程是否已完成
        if(coroutine.done())
            // 返回一个无操作的协程句柄，表示协程不需要恢复
            return std::noop_coroutine();
        else
            // 返回原始的协程句柄，表示协程可以继续
            return coroutine;
    }

    // await_resume() 方法在协程恢复时调用。
    // 这里没有任何操作，通常用于返回协程的结果。
    // 在此示例中，它是空的，代表不需要返回值。
    void await_resume() const noexcept {}
};

//相当于协程的智能指针，提供协程的高级管理，重载运算符解引用可以得到原始指针
struct RepeatAwaitable
{
    // 定义一个操作符重载函数 co_await，表示该结构体可以被用作协程的等待对象。
    RepeatAwaiter operator co_await()
    {
        // 当使用 co_await 操作符时，返回一个 RepeatAwaiter 对象实例。
        return RepeatAwaiter();
    }
};

template<class T>
struct Promise
{
    // 定义一个自动返回的函数 initial_suspend，用于在协程开始时决定是否挂起
    auto initial_suspend()
    {
        // 返回一个始终挂起的状态
        return std::suspend_always();
    }

    // 定义一个自动返回的函数 final_suspend，用于在协程结束时决定是否挂起
    auto final_suspend() noexcept {

        return PreviousAwaiter(mPrevious);
    }

    // 当协程中出现未处理的异常时调用此函数
    void unhandled_exception() { 
        debug(),"unhandled_exception";
        // 抛出当前异常
        mExceptionPtr = std::current_exception();
    }

    // 定义 yield_value 函数，用于协程中返回一个值
    auto yield_value(T ret)
    {
        new (&mResult) T(std::move(ret));
        // 返回一个 RepeatAwaiter 对象，可能用于后续处理
        return std::suspend_always();
    }

    void return_value(T ret)
    {
       new (&mResult) T(std::move(ret));
    }

    std::coroutine_handle<Promise> get_return_object()
    {
        return std::coroutine_handle<Promise>::from_promise(*this);
    }

    T result()
    {
        if(mExceptionPtr) [[unlikely]] { std::rethrow_exception(mExceptionPtr); }

        T ret = std::move(mResult);//将返回值移动到内存栈上
        mResult.~T();
        return ret;
    }


    std::coroutine_handle<> mPrevious{};
    std::exception_ptr mExceptionPtr{};
    union { T mResult; };//另一个小技巧，避免 mResult 被初始化
                         //用来存储 Promise 返回值，在这块内存上进行只修改

    Promise() noexcept {}
    Promise(Promise &&) = delete;
    ~Promise() {}
};

template<>
struct Promise<void>
{
    // 定义一个自动返回的函数 initial_suspend，用于在协程开始时决定是否挂起
    auto initial_suspend() noexcept
    {
        // 返回一个始终挂起的状态
        return std::suspend_always();
    }

    // 定义一个自动返回的函数 final_suspend，用于在协程结束时决定是否挂起
    auto final_suspend() noexcept {

        return PreviousAwaiter(mPrevious);
    }

    // 当协程中出现未处理的异常时调用此函数
    void unhandled_exception()noexcept { 
        debug(),"unhandled_exception";
        // 抛出当前异常
        mExceptionPtr = std::current_exception();
    }

    void return_void() noexcept {}
    

    std::coroutine_handle<Promise> get_return_object()
    {
        return std::coroutine_handle<Promise>::from_promise(*this);
    }

    void result()
    {
        if(mExceptionPtr) [[unlikely]] { std::rethrow_exception(mExceptionPtr); }

    }


    std::coroutine_handle<> mPrevious{};
    std::exception_ptr mExceptionPtr{};

    Promise() = default;
    Promise(Promise &&) = delete;
    ~Promise() = default;
};

template<class T = void>
struct Task
{
    using promise_type = Promise<T>;

    Task(std::coroutine_handle<promise_type> coroutine) noexcept
        : mCoroutine(coroutine) {}

    Task(Task &&) = delete;

    ~Task(){ mCoroutine.destroy(); }

    struct Awaiter
    {
         bool await_ready() const noexcept { return false; }

        std::coroutine_handle<promise_type> await_suspend(std::coroutine_handle<> coroutine) const 
        {
        mCoroutine.promise().mPrevious = coroutine;
        return mCoroutine;
        }

        T await_resume() const { return mCoroutine.promise().result(); }
    
        std::coroutine_handle<promise_type> mCoroutine;
    };

    auto operator co_await()const noexcept { return Awaiter(mCoroutine); }

    operator std::coroutine_handle<>()const noexcept{ return mCoroutine; }

    std::coroutine_handle<promise_type> mCoroutine;
};

struct Loop
{
    std::deque<std::coroutine_handle<>> mReadyQueue;

    struct TimerEntry
    {
        std::chrono::system_clock::time_point expireTime;
        std::coroutine_handle<> coroutine;

        bool operator<(TimerEntry const &that)const noexcept
        { return expireTime > that.expireTime; }
    };

    std::priority_queue<TimerEntry> mTimerHeap;

    void addTask(std::coroutine_handle<> coroutine)
    { mReadyQueue.push_front(coroutine); }

    void addTimer(std::chrono::system_clock::time_point expireTime,std::coroutine_handle<> coroutine)
    { mTimerHeap.push({expireTime,coroutine}); }

    void runAll()
    {
        while(!mTimerHeap.empty() || !mReadyQueue.empty())
        {
            while(!mReadyQueue.empty())
            {
                auto coroutine = mReadyQueue.front();
                mReadyQueue.pop_front();
                coroutine.resume();
            }
            if(!mTimerHeap.empty())
            {
                auto mowTime = std::chrono::system_clock::now();
                auto timer = std::move(mTimerHeap.top());
                if(timer.expireTime < mowTime)
                {
                    mTimerHeap.pop();
                    timer.coroutine.resume();
                }
                else {
                    std::this_thread::sleep_until(timer.expireTime);
                }
            }
        }
    }

    Loop &operator=(Loop &&) = delete;
};

Loop &getLoop()
{
    static Loop loop;
    return loop;
};

struct SleepAwaiter {
    bool await_ready() const noexcept {
        return false;
    }

    void await_suspend(std::coroutine_handle<> coroutine) const {
        getLoop().addTimer(mExpireTime, coroutine);
    }

    void await_resume() const noexcept {
    }

    std::chrono::system_clock::time_point mExpireTime;
};

Task<void> sleep_until(std::chrono::system_clock::time_point expireTime)
{
    co_await SleepAwaiter(expireTime);
    co_return;
}

Task<void> sleep_for(std::chrono::system_clock::duration duration)
{
    co_await SleepAwaiter(std::chrono::system_clock::now() + duration);
    co_return;
}

Task<int> hello1()
{
    debug(),"hello1 开始睡1秒";
    co_await sleep_for(std::chrono::seconds(1));
    debug(),"hello1 睡醒了";
    co_return 1;
}

Task<int> hello2()
{
    debug(),"hello2 开始睡2秒";
    co_await sleep_for(std::chrono::seconds(2));
    debug(),"hello2 睡醒了";
    co_return 2;
}

Task<int> hello()
{
    debug(),"hello 开始等待任务1 和 任务2 ";
    //co_await when_all(hello1,hello2);
    debug(),"hello2 睡醒了";
    co_return 2;
}

int main()
{
    auto t1 = hello1();
    auto t2 = hello2();
    getLoop().addTask(t1);
    getLoop().addTask(t2);
    getLoop().runAll();
    debug(),"main get a result in hello1: ",t1.mCoroutine.promise().result();
    debug(),"main get a result in hello2: ",t2.mCoroutine.promise().result();



    return 0;

}