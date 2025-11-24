#pragma once  
#include<std.hpp>  

namespace zh_async  
{
    /*
        一个掌握自身协程的控制器，可以让协程将自己“注册”到一个外部的调度器或事件系统中
        TaskAwaiter 等待另一个协程，
        TimerAwaiter 等待一个时间点，
        而 CurrentCoroutineAwaiter 等待它自己，目的是为了暴露自己的身份
    */
    struct CurrentCoroutineAwaiter  
    {
        // 检查协程是否准备好执行
        bool await_ready() const noexcept 
        { 
            return false;  
        }

        // 当协程挂起时调用此函数
        std::coroutine_handle<> 
        await_suspend(std::coroutine_handle<> coroutine) noexcept
        {
            mCurrent = coroutine;  
            return coroutine;  // 返回传入的协程句柄,这让协程在挂起的瞬间又恢复，这几乎是零开销
        }

        std::coroutine_handle<> mCurrent;  //用于存储当前协程的句柄
    };

}  