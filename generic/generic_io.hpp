#pragma once
#include <std.hpp>
#include <awaiter/concepts.hpp>
#include <awaiter/details/ignore_return_promise.hpp>
#include <awaiter/task.hpp>
#include <generic/cancel.hpp>
#include <utils/cacheline.hpp>
#include <utils/concurrent_queue.hpp>
#include <utils/non_void_helper.hpp>
#include <utils/rbtree.hpp>
#include <utils/ring_queue.hpp>
#include <utils/spin_mutex.hpp>
#include <utils/uninitialized.hpp>
#include <generic/thread_pool.hpp>

/*
    使用流程
    1、发起请求：用户代码执行 co_await coSleep(std::chrono::nanoseconds(500))。
    2、创建Awaiter：coSleep 函数创建一个 TimerNode::Awaiter，并设置 mExpires 为 now + 500ns
    3、协程挂起：co_await 触发 Awaiter::await_suspend()
    4、插入红黑树：await_suspend 将代表这个协程的 TimerNode 插入到当前线程的 
                  GenericIOContext 的 mTimers 红黑树中。协程暂停。
    5、事件循环运行: 线程的事件循环开始下一轮迭代
            它调用 runDuration()，获取到下一个计时器（就是我们刚刚插入的那个）的剩余时间（比如 499ns）
            它调用 epoll_wait(timeout=499ns)，让操作系统在最多 499ns 后唤醒它，或者如果有其他 I/O 事件则立即唤醒
    6、计时器到期: 500ns 后，epoll_wait 因为超时而返回
    7、处理到期事件: 事件循环检查红黑树，发现最左边的 TimerNode 已经到期
    8、恢复协程: 事件循环从 TimerNode 中取出之前保存的协程句柄，并调用 coroutine.resume()
    9、协程继续执行: 协程在 await_resume() 处恢复执行。由于 mCancelled 为 false，它返回一个成功的结果，用户协程继续执行
*/

namespace zh_async
{
    struct IOContext;
    struct GenericIOContext
    {
        /*
            使用红黑树管理计时器节点
            并且结合C++20协程的暂停/恢复能力，实现非阻塞的高精度计时
        */
        struct TimerNode : 
        CustomPromise<Expected<>,TimerNode>,RbTree<TimerNode>::NodeType
        {
            using RbTree<TimerNode>::NodeType::erase_from_parent;
            std::chrono::steady_clock::time_point mExpires; // 到期时间点

            CancelToken mCancelToken;   //取消源
            bool mCancelled = false;

            void doCancel() //从红黑树上取消计时器
            {
                mCancelled = true;
                erase_from_parent();// O(log N)的复杂度
            }

            bool operator<(TimerNode const &that)const
            { return mExpires < that.mExpires; }

            // Awaiter 定义了协程在 co_await 一个计时器时的行为
            struct Awaiter
            {
                std::chrono::steady_clock::time_point mExpires;
                TimerNode *mPromise = nullptr;

                bool await_ready()const noexcept    // 表示协程总是需要暂停
                    { return false; }
                // 当协程挂起时调用，函数执行完进入挂起状态
                inline void await_suspend(std::coroutine_handle<TimerNode> coroutine);
                // 当协程恢复时调用
                Expected<> await_resume() const {
                    if(!mPromise->mCancelled)
                     return {};
                    else 
                        return std::errc::operation_canceled;
                }
            };
        };

        // 事件循环的“心跳”：计算并返回距离下一个最近的计时器到期还有多长时间
        [[gnu::hot]] std::optional<std::chrono::steady_clock::duration>
        runDuration();

        [[gnu::hot]] void enqueueTimerNode(TimerNode &promise){
            mTimers.insert(promise);
        }
        GenericIOContext();
        ~GenericIOContext();

        GenericIOContext(GenericIOContext &&) = delete;

        /*
            使用静态变量使每个线程都有自己的 GenericIOContext 实例
            使得：
                无锁操作：在单个线程内操作红黑树（插入、删除、查找）完全不需要锁，性能极高。
                亲和性：协程总是在其被创建的线程上恢复，避免了线程间切换和同步的开销。
                这是高性能网络库（如 Netty、libuv）中常见的“Per-Thread Event Loop”模式。
        */
        static inline thread_local GenericIOContext *instance;

    private:
        RbTree<TimerNode> mTimers;  // 存储所有活跃计时器
    };

inline void GenericIOContext::TimerNode::Awaiter::await_suspend
    (std::coroutine_handle<GenericIOContext::TimerNode> coroutine)
    {
        mPromise = &coroutine.promise();    // 获取并保存协程的 Promise 对象（也就是 TimerNode 本身）的指针。
        mPromise->mExpires = mExpires;      // 将到期时间设置到 TimerNode 中。
        GenericIOContext::instance->enqueueTimerNode(*mPromise);    // 将这个 TimerNode 插入到全局的红黑树中
    }

    //用于启动一个协程
    template<class A>
    inline Task<void,IgnoreReturnPromise<AutoDestroyFinalAwaiter>>
    coSpawnStarter(A awaitable){
        (void)co_await std::move(awaitable);
    }
    
    //用于将可等待对象包装成协程并调用resume启动
    template<Awaitable A>
    inline void co_spawn(A awaitable)
    {
        auto wrapped = coSpawnStarter(std::move(awaitable));
        auto coroutine = wrapped.release();
        coroutine.resume();
    }

    inline void co_spawn(std::coroutine_handle<> coroutine)
    { coroutine.resume(); }

    //返回一个可等待对象
    inline auto co_resume()
    {
        struct ResumeAwaiter
        {
            bool await_ready()const noexcept{
                return false;
            }

            void await_suspend(std::coroutine_handle<> coroutine)const{
                co_spawn(coroutine);
            }

            void await_resume() const noexcept{}
        };

        return ResumeAwaiter();
    }

}