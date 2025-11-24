#pragma once
#include <std.hpp>
#include <awaiter/task.hpp>

namespace zh_async 
{
    // ReturnPreviousPromise 用于协程的链式调用，允许在一个协程完成后返回另一个协程的 promise
struct ReturnPreviousPromise {
    //协程开始时默认调用，用于挂起线程
    auto initial_suspend() noexcept {
        return std::suspend_always();
    }

    //在协程结束时调用该函数，返回一个PreviousAwaiter对象，它在上一个协程准备好前挂起本对象
    auto final_suspend() noexcept {
        return PreviousAwaiter(mPrevious);
    }

    void unhandled_exception() {
        throw;
    }

    //在通过co_return返回值后调用该函数，将 previous 传入 mPrevious
    //previous代表时间上将要运行的下一个协程，逻辑上已经运行的上一个协程
    void return_value(std::coroutine_handle<> previous) noexcept {
        mPrevious = previous;
    }

    //该函数在协程创建时被调用，被处理返回一个ReturnPreviousPromise对象
    auto get_return_object() {
        return std::coroutine_handle<ReturnPreviousPromise>::from_promise(
            *this);
    }

    std::coroutine_handle<> mPrevious;
    ReturnPreviousPromise &operator=(ReturnPreviousPromise &&) = delete;
};

using ReturnPreviousTask = Task<void, ReturnPreviousPromise>;
} // namespace zh_async
