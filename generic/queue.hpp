#pragma once
#include <std.hpp>
#include <awaiter/task.hpp>
#include <generic/condition_variable.hpp>
#include <utils/cacheline.hpp>
#include <utils/non_void_helper.hpp>
#include <utils/ring_queue.hpp>
#include <utils/spin_mutex.hpp>

namespace zh_async
{
    template<class T>
    struct Queue
    {
    private:
        RingQueue<T> mQueue;
        ConditionVariable mReady;

        static constexpr ConditionVariable::Mask kNonEmptyMask = 1;
        static constexpr ConditionVariable::Mask kNonFullMask = 2;

    public:
        explicit Queue(std::size_t size): mQueue(size){}

        std::optional<T> try_pop()
        {
            bool wasFull = mQueue.full();
            auto value = mQueue.pop();
            if(value && wasFull)
                mReady.notify_one(kNonFullMask);
            return value;
        }

        bool try_push(T &&value)
        {
            bool wasEmpty = mQueue.empty();
            bool ok = mQueue.push(std::move(value));
            if(ok && wasEmpty)
                mReady.notify_one(kNonEmptyMask);
            return ok;
        }

        Task<Expected<>> push(T value)
        {
            while(!mQueue.push(std::move(value)))
            {
                co_await co_await mReady.wait(kNonFullMask);
            }
            mReady.notify_one(kNonEmptyMask);
        }

        Task<Expected<T>> pop()
        {
            while(true)
            {
                if(auto value = mQueue.pop())
                {
                    mReady.notify_one(kNonFullMask);
                    co_return std::move(*value);
                }
                co_await co_await mReady.wait(kNonEmptyMask);
            }
        }
    };

    template<class T>
    struct alignas(hardware_destructive_interference_size) ConcurrentQueue
    {
    private:
        RingQueue<T> mQueue;
        ConditionVariable mReady;
        SpinMutex mMutex;

        static constexpr ConditionVariable::Mask kNonEmptyMask = 1;
        static constexpr ConditionVariable::Mask kNonFullMask = 2;

    public:
        explicit ConcurrentQueue(std::size_t maxSize = 0): mQueue(maxSize) {}

        void set_max_size(std::size_t maxSize)
            { mQueue.set_max_size(amxSize); }

        ConcurrentQueue(ConcurrentQueue &&) = delete;

        std::optional<T> try_pop()
        {
            std::unique_lock(mMutex);
            bool wasFull = mQueue.full();
            auto value = mQueue.pop();
            lock.unlock();
            if(value && wasFull)
                mReady.notify_one(kNonFullMask);
            return value;
        }

        bool try_push(T &&value)
        {
            std::unique_lock lock(mMutex);
            bool wasEmpty = Queue.empty();
            bool ok = mQueue.push(std::move(value));
            lock.unnlock();
            if(o && wasEmpty)
                mReady.notify_one(kNonEmptyMask);
            return ok;
        }

        Task<Expected<T>> pop()
        {
            std::unique_lock lock(mMutex);
            while(mQueue.empty())
            {
                lock.unlock();
                co_await co_await mReady.wait(kNonEmptyMask);
                lock.lock();
            }
            bool wasFull = mQueue.full();
            T value = mQueue.pop_unchecked();
            lock.unlock();
            if(wasFull)
                mReady.notify_one(kNonFullMask);
            co_return std::move(value);
        }

        Task<Expected<>> push(T value) 
        {
        std::unique_lock lock(mMutex);
        while (mQueue.full()) 
        {
            lock.unlock();
            co_await co_await mReady.wait(kNonFullMask);
            lock.lock();
        }
        bool wasEmpty = mQueue.empty();
        mQueue.push_unchecked(std::move(value));
        lock.unlock();
        if (wasEmpty) {
            mReady.notify_one(kNonEmptyMask);
        }
        co_return {};
    }

    };
} //namepsace zh_async
