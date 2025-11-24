#pragma once
#include<std.hpp>
#include<awaiter/concepts.hpp>
#include<awaiter/task.hpp>

namespace zh_async
{
    /*
        Awaitable：这是一个concept，它确保传递给and_then的参数A F可以是协程或返回Task对象的函数
        require:   C++20的概念约束，它确保 F 不是一个可以直接调用的函数，也不是一个可以接受 A 的返回类型作为参数的函数
                    这意味着 F 必须是一个异步操作，不能直接在当前上下文中执行
    */
    template <Awaitable A,Awaitable F>
        requires(!std::invocable<F> &&
                 !std::invocable<F,typename AwaitableTraits<A>::retType>)
    Task<typename AwaitableTraits<F>::RetType> and_then(A a,F f){
    {
        co_await std::move(a);          //启动异步操作a，并等待它返回
        co_return co_await std::move(f);//启动异步操作f，并将结果作为and_then函数的返回值
    };
}
} // namespace zh_async
