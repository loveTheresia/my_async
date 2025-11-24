#pragma once
#include <std.hpp>
#include <generic/generic_io.hpp>
#include <generic/io_context.hpp>
#include <platform/platform_io.hpp>
#include <utils/cacheline.hpp>

namespace zh_async
{
    /*
        IOContext 实现的是一个单线程的 Reactor 模型：一个线程，一个事件循环，处理所有 I/O 和协程
        IOContextMT 则将其扩展为多线程的 Reactor 池模型：
        它创建并管理一个线程池，池中的每个线程都运行一个独立的 IOContext 事件循环
    */
    struct IOContextMT
    {
    private:
        std::unique_ptr<IOContext[]> mWorkers;  // 管理多个 IOContext
        std::size_t mNumWorkers = 0;

    public:
        IOContextMT();
        IOContextMT(IOContext &&) = delete;
        ~IOContextMT();

        static std::size_t get_worker_id(IOContext const &context)noexcept
        {
          return static_cast<std::size_t>(&context - instance->mWorkers.get());
        }

        static std::size_t this_worker_id()noexcept
        {
            return get_worker_id(*IOContext::instance);
        }

        static IOContext &nth_worker(std::size_t index)noexcept
        {
            return instance->mWorkers[index];
        }

        static std::size_t num_workers()noexcept
        {
            return instance->mNumWorkers;
        }

        static void run(std::size_t numWorkers = 0);

        static IOContextMT *instance;
    };
}