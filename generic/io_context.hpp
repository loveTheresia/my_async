#pragma once
#include <std.hpp>
#include <awaiter/task.hpp>
#include <generic/generic_io.hpp>
#include <platform/platform_io.hpp>
#include <utils/cacheline.hpp>

namespace zh_async
{
    /*
        一个单线程的异步 I/O 事件循环和所有协程调度器
            实现的是一个单线程的 Reactor 模型
        它在一个线程里持续不断地工作，主要职责是：
            监听 I/O 事件：比如网络数据是否到达、文件是否读写完成
            调度协程：当一个协程因为等待 I/O 而暂停（co_await）时，IOContext 会注册它
                    当 I/O 事件完成后，IOContext 负责唤醒这个协程，让它继续执行，保证了线程亲和度
    */

    struct IOContextOptions
    {
        std::chrono::steady_clock::duration maxSleep = std::chrono::milliseconds(114);
        std::optional<std::size_t> threadAffinity = std::nullopt;
        std::size_t queueEntries = 512; 
    };

    /*
        定义一个结构体 IOContext，使用硬件干扰大小进行对齐
    */
    struct alignas(hardware_destructive_interference_size) IOContext
    {
    private:
        GenericIOContext mGenericIO;                    // 定时器任务
        PlatformIOContext mPlatformIO;                  // 底层IO事件，直接与操作系统交互
        std::chrono::steady_clock::duration mMaxSleep;  // 最大休眠时间

    public:
        explicit IOContext(IOContextOptions options = {});
        IOContext(IOContext &&) = delete;
        ~IOContext();
        //标记为热点函数，采用更积极的优化
        [[gnu::hot]] void run();
        [[gnu::hot]] bool runOnce();    

        static thread_local IOContext *instance;
    };

    // 用于将一个 Task 提交给当前线程的 IOContext
    inline Task<> co_catch(Task<Expected<>> task)
    {
        auto res = co_await task;
        if(res.has_error())
            std::cerr << res.error().category().name() << " error: " << res.error().message() << " (" << res.error().value() << ")\n";
        co_return;
    }

    //使用co_spawn启动协程任务
    //创建IOContext实例并启动事件循环
    inline void co_main(Task<Expected<>> main)
    {
        IOContext ctx;
        co_spawn(co_catch(std::move(main)));
        ctx.run();
    }

    // 简洁启动
    inline void co_main(Task<> main)
    {
        IOContext ctx;
        co_spawn(std::move(main));
        ctx.run();
    }
 
}
