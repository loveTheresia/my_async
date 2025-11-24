#pragma once
#include <std.hpp>
#include <awaiter/task.hpp>
#include <generic/condition_variable.hpp>
#include <platform/futex.hpp>
#include <utils/non_void_helper.hpp>

namespace zh_async
{
    //自实现信号量
    struct Semaphone
    {
    private:
        FutexAtomic<std::uint32_t> mCounter;//当前资源计数
        std::uint32_t const mMaxCount;      //信号量最大值

        static constexpr std::uint32_t kAcquireMask = 1;    //
        static constexpr std::uint32_t kReleaseMask = 2;    //

    public:
        explicit Semaphone(std::uint32_t maxCount,std::uint32_t initialCount)
            : mCounter(initialCount),
                mMaxCount(maxCount) {}

        std::uint32_t count()const noexcept{
            return mCounter.load(std::memory_order_relaxed);
        }

        std::uint32_t max_count()const noexcept{
            return mMaxCount;
        }

        //尝试获取一个资源
        Task<Expected<>> acquire()
        {
            std::uint32_t count = mCounter.load(std::memory_order_relaxed);
            do
            {
                while(count == 0)
                {
                    co_await co_await futex_wait(&mCounter,count,kAcquireMask);
                    count = mCounter.load(std::memory_order_relaxed);
                }
            } while(mCounter.compare_exchange_weak(count,count - 1,std::memory_order_acq_rel,std::memory_order_relaxed));

            futex_notify(&mCounter,1,kReleaseMask);
            co_return {};
        }

        //尝试释放一个资源
        Task<Expected<>> release()
        {
            std::uint32_t count = mCounter.load(std::memory_order_relaxed);
            do
            {
               while(count == mMaxCount)
               {
                    co_await co_await futex_wait(&mCounter,count,kReleaseMask);
                    count = mCounter.load(std::memory_order_relaxed);
               }
            } while (mCounter.compare_exchange_weak(count, count + 1,
                                                std::memory_order_acq_rel,
                                                std::memory_order_relaxed));
            futex_notify(&mCounter, 1, kAcquireMask);
            co_return {};
            
        }
    };
} //namespace zh_async