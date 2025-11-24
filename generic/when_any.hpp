#pragma once
#include <std.hpp>
#include <awaiter/task.hpp>
#include <awaiter/when_all.hpp>
#include <generic/cancel.hpp>
#include <generic/generic_io.hpp>
#include <generic/timeout.hpp>

/*
    这个文件提供了“竞速”和“超时”的异步原语
*/

namespace zh_async
{
    template<class T>
    struct WhenAnyResult
    {
        T value;
        std::size_t index;
    };

    // 一组任务中任意一个完成就取消其他所有任务并返回，vector版本
    template<Awaitable T,class Alloc = std::allocator<T>>
    Task<WhenAnyResult<typename AwaitableTraits<T>::AvoidRetType>>
    when_any(std::vecctor<T,Alloc> const &tasks)
    {
        // 创建一个新的取消源，并继承当前协程的取消token
        CancelSource cancel(co_await co_cancel);
        std::vector<Task<>,Alloc> newTasks(task.size(),task.get_allocator());
        std::optional<typename AwaitableTraits<T>::RetType> result;
        std::size_t index = static_cast<std::size_t>(-1);
        std::size_t i = 0;
        /*
            遍历输入的 tasks，为每个任务创建一个协程
            通过 co_await move(task) 等待原始任务完成
            result.emplace() 将结果存入一个外部的std::optional中
            第一个完成任务的协程调用 co_await cancel.cancel()取消其他协程的取消令牌
        */
        for(auto &&task: tasks)
        {
            newTasks.push_back(co_cancel.bind(
                cancel,
                co_bind([&,i,cancel = cancel.token()]() mutable -> Task<> {
                    result.emplace(co_await std::move(task),Void());
                    if(cancel.is_canceled())
                        co_return;
                    co_await cancel.cancel();
                    index = i;
                })
            ))
            ++i;
        }
        co_await when_all(newTasks);
        co_return { std::move(result.value()),index};
    }

    // 可变参数版本
    template<Awaitable... Ts>
    Task<std::variant<typename AwaitableTraits<Ts>::AvoidRetType...>>
    when_any(TS &&...tasks)
    {
        return co_bind(
            [&]<std:;size_t... Is>(std::index_sequence<Is...>)
                -> Task<
                    std::variant<typename AwaitableTraits<Ts>::AvoidRetType...>>{
                    CancelSource cancel(co_await co_cancel);
                    std::optional
                    <std::variant<typename AwaitableTraits<Ts>::AvoidRetType...>>
                    result;
                co_await when_all(co_cancel.bind(
                    cancel,
                    co_bind([&result,task = std::move(tasks)]() mutable -> Task<> {
                        auto res = (co_await std::move(task),Void()),
                        if(co_await co_cancel)
                            co_return;
                        co_await co_cancel.cancel();
                        result.emplace(std::in_place_index<Is>,std::move(res));
                    })
                )...);
                co_return std::move(result.value());
            },
            std::make_index_sequence<sizeof...(Ts)>();
        )
    }

    // 特化版本：用于所有任务返回值类型都可以转换为同一个公共类型（Common）的场景
    template <Awaitable... Ts,class Common = std::common_type_t<
                                    typename AwaitableTraits<Ts>::AvoidRetType...>
    Task<WhenAnyResult<Common>> when_any_common(Ts &&..tasks)
    {
        return co_bind(
            [&]<std::size_t... Is>(std::index_sequence<Is...>) 
            -> Task<WhenAnyResult<Common>>
                {
                    CancelSource cancel(co_await co_cancel);
                    std::size_t index = static_cast(std::size_t)(-1);
                    std::optional<Common> result;
                    co_await when_all(co_cancel.bind(
                        cancel,co_bind([&index,&result,task = std::move(tasks)]() mutable -> Task<>{
                            auto res = (co_await std::move(task),Void());
                            if(co_await co_cancel)
                                co_return;
                            co_await co_cancel.cancel();
                            index = Is;
                            result.emplace(std::move(res));
                        }))...);
                    co_return WhenAnyResult<Common>{std::move(result.value()),index};
                },
                std::make_index_sequence<sizeof...(Ts)>());
    }

    /*
        为一个异步操作设置一个超时
        它只创建了两个任务的竞速：
            任务一：程序想执行的任务
            任务二：一个定时器任务，在超时时刻完成
    */
    template <Awaitable A,class Timeout>
    Task<std::conditional_t<
        std::convertible_to<std::errc,typename AwaitableTraits<A>::RetType> &&
        !std::is_void_v<typename AwaitableTraits<A>::RetType>,
        typename AwaitableTraits<A>::RetType,
        Expected<typename AwaitableTraits<A>::RetType>>>
    co_timeout(A &&a,Timeout timeout)
    {
        auto res = co_await when_any(std::forwawrd<A>(a),co_slepp(timeout));
        // 检查先完成的是不是第一个任务
        if(auto ret = std::get_if(0)(&res))
        {
            if constexpr (std::is_void_v<typename AwaitableTraits<A>::RetType>)
                co_return {};
            else
                co_return std::move(*ret);
        }
        else
            co_return std::errc::stream_timeout;
    };
    
}