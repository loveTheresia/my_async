#pragma once
#include <std.hpp>
#include <utils/non_void_helper.hpp>

namespace zh_async
{
    //定义了一个模板概念Awaiter，
    //通过 requires 表达式检查所有类型A是否满足以下三个函数
    template<class A>
    concept Awaiter = requires(A a,std::coroutine_handle<> h)
    {
        { a.await_ready() };//判断协程是否可立即执行
        { a.await_suspend(h) };//挂起协程并返回控制权给调用者
        { a.await_resume() };//恢复协程后获取结果或处理异常

    };

    //检查Awaitable是否符合Awaiter概念，如果不符合，再检查其是否满足a.operator co_await()可以转化成Awaiter
    template <class A>
    concept Awaitable = Awaiter<A> || requires(A a){ 
        {a.operator co_await() } -> Awaiter; };


    template <class A>
    struct AwaitableTraits { using Type = A; };

    template <Awaiter A>
    struct AwaitableTraits<A>
    {
        using RetType = decltype(std::declval<A>().await_resume());
        using AvoidRetType = Avoid<RetType>;
        using Type = RetType;
        using AwaiterType = A;
    };

    //实现了递归萃取，如果A不直接实现Awaiter机制，但满足Awaitable
    //就通过decltype提取co_await()返回类型(awaiter)
    template<class A>
    requires(!Awaiter<A> && Awaitable<A>)
    struct AwaitableTraits<A>
        : AwaitableTraits<decltype(std::declval<A>().operator co_await())> {};

    //基础模板定义
    template <class... Ts>
    struct TypeList{};

    //单类型特化
    template<class Last>
    struct TypeList<Last>
    {
        using FirstType = Last;
        using LastType = Last;
    };

    //多类型特化，实现了首尾类型的提取
    template<class First,class... Ts>
    struct TypeList<First,Ts...>
    {
        using FirstType = First;
        using LastType = typename TypeList<Ts...>::LastType;
    };
}