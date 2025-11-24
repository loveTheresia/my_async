#pragma once
#include<std.hpp>
#include<awaiter/concepts.hpp>
#include<awaiter/task.hpp>
#include<utils/uninitialized.hpp>
#include<awaiter/details/return_previous.hpp>

/*
    when_all.hpp：解决一个最复杂的问题：如何高效、安全地并行等待多个异步任务，并收集它们的所有结果
    原理跳转 when_all

    优点：
        1、真正的并行（逻辑并行）：所有子任务几乎同时被启动，而不是串行等待
        2、极高的性能：
            通过“协程栈窃取”优化，减少了不必要的上下文切换
            使用 uninitialized 避免了结果的默认构造开销
            整个过程基于计数器，没有使用昂贵的锁或原子操作（假设在单线程事件循环中运行）
        3、健壮的异常安全：“快速失败”机制确保了错误能被及时传播，避免了资源浪费
        4、泛用性强：通过模板和重载，它既能处理固定数量的任务（元组），也能处理动态数量的任务
        5、清晰的职责分离：CtlBlock 负责状态，Helper 负责单个任务，Awaiter 负责调度，Impl 负责组装
            这种设计使得代码虽然复杂，但逻辑清晰，易于理解和维护

*/
namespace zh_async
{
    /*
        共享的指挥中心

    */
    struct WhenAllCtlBlock
    {
        std::size_t mCount;                 //追踪还有多少任务需要完成
        std::coroutine_handle<> mPrevious{};//指向调用when_all的那个协程
#if ZH_ASYNC_EXCEPT
        std::exception_ptr Exception{};
#endif
    };

    /*
        总导演：负责启动所有子任务
    */
    struct WhenAllAwaiter
    {
        bool await_ready()const noexcept { return false; }

        std::coroutine_handle<>
        await_suspend(std::coroutine_handle<> coroutine)const   //负责 resume 所有任务，并挂起当前协程
        {
            if(mTasks.empty()) { return coroutine; }
            mControl.mPrevious = coroutine;             // 保留主任务的句柄
            // 启动除了最后一个之外的所有子任务
            for(auto const &t: mTasks.subspan(0,mTasks.size() - 1))
                t.get().resume();

            return mTasks.back().get();
        }

        void await_resume() const   //在所有任务完成后，如果定义了ZH_ASYNC_EXCEPT，他会重新抛出所有异常
        {
#if ZH_ASYNC_EXCEPT
            if(mControl.Exception)[[unlikely]]
                std::rethrow_exception(mControl.mException);
#endif
        }

        WhenAllCtlBlock &mControl;                  //主任务的句柄
        std::span<ReturnPreviousTask const> mTasks; //存储所有要等待的任务
    };

    /*
        单个子任务的代理，生命周期就是一个子任务的完整生命周期
    */
    template<class T>
    ReturnPreviousTask whenAllHelper(auto &&t,WhenAllCtlBlock &control,uninitialized<T> &result)
    {
#if ZH_ASYNC_EXCEPT
    try {
#endif
        // 等待真正的任务完成
        result.emplace(co_await std::forward<decltype(t)>(t));
#if ZH_ASYNC_EXCEPT
    } catch (...) {
        control.mException = std::current_exception();
        co_return control.mPrevious;    // 如果发生异常，立刻恢复主任务
    }
#endif
    --control.mCount;
    if (control.mCount == 0) 
        co_return control.mPrevious;    // 如果是最后一个任务，就恢复主任务
    
    co_return std::noop_coroutine();    // 如果不是，就安静的结束
    }

    template<class = void>
    ReturnPreviousTask whenAllHelper(auto &&t, WhenAllCtlBlock &control,
                                 uninitialized<void> &) {
#if ZH_ASYNC_EXCEPT
    try {
#endif
        co_await std::forward<decltype(t)>(t);
#if ZH_ASYNC_EXCEPT
    } catch (...) {
        control.mException = std::current_exception();
        co_return control.mPrevious;
    }
#endif
    --control.mCount;
    if (control.mCount == 0) {
        co_return control.mPrevious;
    }
    co_return std::noop_coroutine();
}

    /*
        执行器
    */
    template <std::size_t... Is, class... Ts>
    Task<std::tuple<typename AwaitableTraits<Ts>::AvoidRetType...>>
    whenAllImpl(std::index_sequence<Is...>, Ts &&...ts) 
    {
        // 创建 WhenAllCtlBlock
        WhenAllCtlBlock control{sizeof...(Ts)};
        // 创建元组 result
        std::tuple<Uninitialized<typename AwaitableTraits<Ts>::RetType>...> result;

        ReturnPreviousTask taskArray[]{whenAllHelper(ts, control, std::get<Is>(result))...};
        // 执行任务
        co_await WhenAllAwaiter(control, taskArray);
        co_return std::tuple<typename AwaitableTraits<Ts>::AvoidRetType...>(std::get<Is>(result).move()...);
    }

    template <Awaitable... Ts>
    requires(sizeof...(Ts) != 0)
    auto when_all(Ts &&...ts) {
    return whenAllImpl(std::make_index_sequence<sizeof...(Ts)>{},
                       std::forward<Ts>(ts)...);
}

/*
    完整的执行流程 (when_all(task1, task2))
	1、co_await when_all(task1, task2) 被调用。
	2、when_all -> whenAllImpl 开始执行：
		创建 WhenAllCtlBlock control{mCount: 2}。
		创建 std::tuple<Uninitialized<T1>, Uninitialized<T2>> result。
		创建两个 whenAllHelper 协程 h1 和 h2，并让它们分别代理 task1 和 task2。
	3、whenAllImpl 执行 co_await WhenAllAwaiter(...)。
	4、主任务挂起，WhenAllAwaiter::await_suspend 被调用。
	5、h1.resume() 被调用，task1 开始在后台执行。
	6、await_suspend 返回 h2 的句柄。主任务的执行流现在开始执行 h2，task2 开始执行。
	7、假设 task1 先完成：
		h1 恢复，将结果存入 result 的第一个元素。
		control.mCount 减为 1。
		因为 mCount 不为 0，h1 执行 co_return std::noop_coroutine()，然后 h1 销毁自己。
	8、假设 task2 接着完成：
		h2 恢复，将结果存入 result 的第二个元素。
		control.mCount 减为 0。
		因为 mCount 为 0，h2 执行 co_return control.mPrevious，即恢复主任务。
	9、主任务从挂起点恢复，WhenAllAwaiter::await_resume() 被调用。
	10】whenAllImpl 继续执行，将 uninitialized 的结果移动构造成一个真正的 std::tuple 并 co_return。
*/
template <Awaitable T, class Alloc = std::allocator<T>>
Task<std::conditional_t<
    !std::is_void_v<typename AwaitableTraits<T>::RetType>,
    std::vector<typename AwaitableTraits<T>::RetType,
                typename std::allocator_traits<Alloc>::template rebind_alloc<
                    typename AwaitableTraits<T>::RetType>>, void>>
when_all(std::vector<T, Alloc> const &tasks) {
    WhenAllCtlBlock control{tasks.size()};
    Alloc alloc = tasks.get_allocator();
    std::vector<Uninitialized<typename AwaitableTraits<T>::RetType>,
                typename std::allocator_traits<Alloc>::template rebind_alloc<
                    Uninitialized<typename AwaitableTraits<T>::RetType>>>
        result(tasks.size(), alloc);
    {
        std::vector<ReturnPreviousTask,
                    typename std::allocator_traits<
                        Alloc>::template rebind_alloc<ReturnPreviousTask>>
            taskArray(alloc);
        taskArray.reserve(tasks.size());
        for (std::size_t i = 0; i < tasks.size(); ++i) {
            taskArray.push_back(whenAllHelper(tasks[i], control, result[i]));
        }
        co_await WhenAllAwaiter(control, taskArray);
    }
    if constexpr (!std::is_void_v<typename AwaitableTraits<T>::RetType>) {
        std::vector<
            typename AwaitableTraits<T>::RetType,
            typename std::allocator_traits<Alloc>::template rebind_alloc<
                typename AwaitableTraits<T>::RetType>>
            res(alloc);
        res.reserve(tasks.size());
        for (auto &r: result) {
            res.push_back(r.move());
        }
        co_return res;
    }
}

}