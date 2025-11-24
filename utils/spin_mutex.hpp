#pragma once
#include <std.hpp>

namespace zh_async 
{
    //自旋锁
    struct SpinMutex
    {
        // 尝试获取锁。如果成功，则返回 true；如果锁已被其他线程占用，则返回 false。
        bool try_lock() { return !flag.test_and_set(std::memory_order_acquire); }

        // 获取锁。如果锁已被其他线程占用，则循环等待，直到成功获取锁。
        void lock() { while(flag.test_and_set(std::memory_order_acquire)); }

        // 用于自旋锁的原子标志，初始值为 false。
        // 原子标志是一个线程安全的布尔值，用于表示锁的状态。
        std::atomic_flag flag{false};
    };

} 