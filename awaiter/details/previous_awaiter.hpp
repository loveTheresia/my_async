#pragma once
#include<std.hpp>

namespace zh_async
{
    //用于挂起协程
    struct PreviousAwaiter
    {
        std::coroutine_handle<> mPrevious;

        bool await_ready() const noexcept{ return false; }

        std::coroutine_handle<>
        await_suspend(std::coroutine_handle<> coroutine)const noexcept
            { return mPrevious; }

        void await_resume() const noexcept {}
    };
} // namespace zh_async
