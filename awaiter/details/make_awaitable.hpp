#pragma once
#include<std.hpp>
#include<awaiter/concepts.hpp>
#include<awaiter/task.hpp>

namespace zh_async
{
    /*
        ensureAwaitable - 通用适配器
        这个函数的目标是：接受任何东西，并返回一个 Task
    */

    // 将可以使用awaiter的值转换成task
    template <Awaitable A>
    A ensureAwaitable(A a){ return std::move(a); }

    // 将不能使用awaiter的值通过 co_return 创建一个协程变成task
    template<class A>
        requires(!Awaitable<A>)
    Task<A> ensureAwaitable(A a){ co_return std::move(a); }

    // 一个 Awaitable 的对象，但它不是 Task，那就新建一个 task 去 co_await 这个 Awaitable
    template<Awaitable A>
    Task<typename AwaitableTraits<A>::RetType> 
    ensureAwaitable(A a){ co_return std::move(a); }

    /*
        这两个是 ensureAwaitable 的特化和增强版本，引入了惰性求值的特性
        
        惰性求值：一个表达式的值，只有在程序真正需要它的时候，才会被计算出来
    */
    template <class T>
    Task<T> ensureTask(Task<T> &&t) { return std::move(t); }

    template<class A>
        requires(!Awaitable<A> && std::invocable<A> &&      // 要求 A 不是 Awaitable，并且 A 是一个可调用对象
                Awaitable<std::invoke_result_t<A>>)         // 并且 A 调用后的返回类型是 Awaitable
        Task<typename AwaitableTraits<std::invoke_result_t<A>>::RetType>
    ensureTask(A a) {return ensureTask(std::invoke(std::move(a)));}; 
    /*
        当新的 Task 被执行时，它会调用 std::invoke 来执行 lambda，得到一个 Awaitable（比如一个 Task），
        然后把这个结果再交给 ensureTask 处理（此时会匹配到第一个重载，直接透传）。
    */
}