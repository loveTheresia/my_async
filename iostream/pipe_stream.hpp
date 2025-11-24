#pragma once
#include <std.hpp>
#include <awaiter/task.hpp>
#include <iostream/stream_base.hpp>

namespace zh_async
{
    //创建一个数组用于管道操作
    std::array<OwningStream,2> pipe_stream();

    //异步地将输入流 in 的数据转发到输出流 out 中，返回一个 task 表示这个操作的未来结果
    Task<Expected<>> pipe_forward(BorrowedStream &in,BorrowedStream &out);

    template<class Func,class... Args>
        requires std::invocable<Func,Args..., OwningStream &>//确保func和args可以在OwningStream上被调用
    //用于将一个函数和一系列参数绑定到一个 OwningStream 上
    inline Task<Expected<>> pipe_bind(OwningStream w,Func &&func, Args &&...args)
    {
        return co_bind(
            [func = std::forward<decltype(func)>(func),
                w = std::move(w)]
            (auto &&...args)mutable -> Task<Expected<>>
            {
                auto e1 = co_await std::invoke(std::forward<decltype(func)>(func),
                                                std::forward<decltype(args)>(args)...,w);
                auto e2 = co_await w.flush();
                co_await w.close();
                co_await e1;
                co_await e2;
                co_return {};
            },
            std::forward<decltype(args)>(args)...);
        
    }
}