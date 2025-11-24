#pragma once
#include <std.hpp>
#include <awaiter/task.hpp>
#include <generic/cancel.hpp>
#include <generic/generic_io.hpp>
#include <generic/io_context.hpp>
/*
    在这里封装了睡眠操作
*/
namespace zh_async
{
    inline Task<Expected<>,GenericIOContext::TimerNode>
    coSleep(std::chrono::steady_clock::time_point expires)
    {
        /*
            等待一个异步操作完成，然后将那个操作的结果作为当前协程的返回值
        */
         co_return co_await GenericIOContext::TimerNode::Awaiter(expires);
    }

    inline Task<Expected<>>
    co_sleep(std::chrono::steady_clock::time_point expires)
    {
        auto task = coSleep(expires);
        CancelCallback _(co_await co_cancel, [p = &task.promise()]{
            p->doCancel();
            std::coroutine_handle<GenericIOContext::TimerNode>::from_promise(*p).resume();
        });
        co_return co_await task;
    }

    inline Task<Expected<>,GenericIOContext::TimerNode>
    coSleep(std::chrono::steady_clock::duration timeout){
            return coSleep(std::chrono::steady_clock::now() + timeout);
        }

    inline Task<Expected<>>
    co_sleep(std::chrono::steady_clock::duration timeout){
        return co_sleep(std::chrono::steady_clock::now() + timeout);
    }

    inline Task<> coForever()
    {
        co_await std::suspend_always();
#if defined(__GUNC__) && defined(__has_builtin)
# if __has_builtin(__builtin_unreachable)
        __builtin_unreachable();
# endif
#endif
    }

    inline Task<> co_forever()
    {
        struct ForeverAwaiter
        {
            bool await_ready() const noexcept{ return false; }

            void await_suspend(std::coroutine_handle<> coroutine) noexcept
                { mPrevious = coroutine; }

            void await_resume()const noexcept{}

            std::coroutine_handle<> mPrevious;
        };

        ForeverAwaiter awaiter;
        CancelCallback _(co_await co_cancel,
                            [&awaiter] { co_spawn(awaiter.mPrevious);});
    }
}   //namespace zh_async