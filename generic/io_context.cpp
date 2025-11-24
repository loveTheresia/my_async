#include <std.hpp>
#include <awaiter/task.hpp>
#include <generic/generic_io.hpp>
#include <generic/io_context.hpp>
#include <platform/futex.hpp>
#include <platform/platform_io.hpp>
#include <utils/cacheline.hpp>

namespace zh_async
{
    IOContext::IOContext(IOContextOptions options)
    {
        if(instance)
        throw std::logic_error("each thread may create only one IOContext");
        // 设置线程局部实例
        instance = this;
        // 在其他IO处注册这个IOContext
        GenericIOContext::instance = &mGenericIO;
        PlatformIOContext::instance = &mPlatformIO;
        // 性能优化
        if(options.threadAffinity)
            PlatformIOContext::schedSetThreadAffinity(*options.threadAffinity);

        mPlatformIO.setup(options.queueEntries);
        mMaxSleep = options.maxSleep;
    }

    IOContext::~IOContext()
    {
        IOContext::instance = nullptr;
        GenericIOContext::instance = nullptr;
        PlatformIOContext::instance = nullptr;
    }

    void IOContext::run()
    {
        /*
            线程会一直在此循环
            以处理所有的IO事件，恢复相应的协程
            同时会执行所有到期的定时器任务
        */
        while(runOnce());
    }
    
    //处理IO上下文事件
    bool IOContext::runOnce()
    {
        auto duration = mGenericIO.runDuration();
        // 检查定时器任务和底层IO事件
        if(!duration && !mPlatformIO.hasPendingEvents())
        [[unlikely]]{ return false; }

        if(!duration || *duration > mMaxSleep)
            duration = mMaxSleep;

        mPlatformIO.waitEventsFor(duration);
        /*
            该函数会阻塞，直到：
            1、有IO事件发生
            2、超时
            3、有新的协程任务提交
        */
        return true;
    }

    //通过thread_local关键字保证每个线程只有一个实例
    thread_local IOContext *IOContext::instance;

} //namespace zh_async