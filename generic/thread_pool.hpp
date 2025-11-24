#pragma once
#include <std.hpp>
#include <awaiter/task.hpp>
#include <generic/condition_variable.hpp>
#include <generic/io_context.hpp>
#include <utils/spin_mutex.hpp>

namespace zh_async
{
    struct Thread_pool
    {
    private:
        struct Thread;

        SpinMutex mWorkingMutex;
        std::list<Thread *> mWorkingThreads;    // 工作线程列表
        SpinMutex mFreeMutex;
        std::list<Thread *> mFreeThreads;       // 空闲线程列表
        SpinMutex mThreadsMutex;
        std::list<Thread> mThreads;              // 线程存储池

        Thread *submitJob(std::function<void()> func);

    public:
        Task<Expected<>> rawRun(std::function<void()> func);
        Task<Expected<>> rawRun(std::function<void(std::stop_token)> func,CancelToken cancel);

        // run() 无取消版本
        auto run(std::invocable auto func)
            -> Task<Expected<std::invoke_result_t<decltype(func)>>>
            requires(!std::invocable<decltype(func),std::stop_token>)
        {
            // 储存函数执行结果
            std::optional<Avoid<std::invoke_result_t<decltype(func)>>> res;
            // 将函数打爆成lambda，传递给rawRun,并等待co_await完成
            co_await co_await rawRun([&res,func = std::move(func)]() mutable{
                res = (func(),Void());
            });
            // 如果 res 为空，说明任务取消了，否则返回结果
            if(!res)[[unlikely]] co_return std::errc::operation_canceled;

            co_return std::move(*res);
        }

        // 显式取消版本
        auto run(std::invocable<std::stop_token> auto func,
             CancelToken cancel) 
        -> Task<
            Expected<std::invoke_result_t<decltype(func), std::stop_token>>> 
        {
        std::optional<Avoid<std::invoke_result_t<decltype(func), std::stop_token>>> res;
        auto e = co_await rawRun(
            [&res, func = std::move(func)](std::stop_token stop) mutable {
                res = (func(stop), Void());
            });
        if (e.has_error()) 
            co_return CO_ASYNC_ERROR_FORWARD(e);
        
        if (!res) 
            co_return std::errc::operation_canceled;
        
        co_return std::move(*res);
        }

        // 自动取消传播版本
        auto run(std::invocable<std::stop_token> auto func) 
        -> Task<
            Expected<std::invoke_result_t<decltype(func), std::stop_token>>> {
        co_return co_await run(func, co_await co_cancel);
    }

        std::size_t threads_count() ;
        std::size_t working_threads_count() ;

        Thread_pool();
        ~Thread_pool();
        Thread_pool &operator=(Thread_pool &&) = delete;
    };
}