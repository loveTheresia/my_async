#include <awaiter/task.hpp>
#include <generic/thread_pool.hpp>
#include <platform/futex.hpp>

namespace zh_async
{
   /*
      线程池的核心，封装了一个 std::jthread 和与之配套的同步机制
   */
    struct Thread_pool::Thread
    {
        std::function<void()> mTask;   // 任务队列
        std::condition_variable mCV;   // 条件变量
        std::mutex mMutex;             // 互斥锁
        Thread_pool *mPool;            // 指向父线程池的回指针
        std::jthread mThread;          // C++20引入的现代线程类

         ~Thread()
         {
            mThread.request_stop();
            mCV.notify_one();
            mThread.join();
         }

         /*
            线程的主循环
         */
         void startMain(std::stop_token stop)
         {
            // 1、循环条件
            // 当线程池析构时，这个循环就会自动退出
            while(!stop.stop_requested())[[likely]]
            {
               // 2、常规流程：锁住
                std::unique_lock lock(mMutex);
                mCV.wait(lock,[&]{return mTask != nullptr || stop.stop_requested();});
                // 3、检查是否需要退出
                if(stop.stop_requested())[[unlikely]] { return; }
               // 4、取出任务并执行
                auto task = std::exchange(mTask,nullptr);
                lock.unlock();   // 执行任务前释放锁
                task();
                lock.lock();     // 任务完成，更新线程池状态
                mTask = nullptr;
                std::unique_lock workingLock(mPool->mWorkingMutex);
                // 从工作表中移除
                mPool->mWorkingThreads.remove(this);
                workingLock.unlock();
                std::lock_guard freeLock(mPool->mFreeMutex);
                mPool->mFreeThreads.push_back(this);
            }
         }

         explicit Thread(Thread_pool *pool)
         {
            mPool = pool;
            mThread = std::jthread([this](std::stop_token stop){ startMain(stop); });
         }

         Thread(Thread &&) = delete;

         void workOn(std::function<void()> func)
         {
            std::lock_guard lock(mMutex);
            mTask =std::move(func);
            mCV.notify_one();
         }
    };

    /*
         这个函数负责将任务func分配给一个线程
         通过分别锁定 mFreeMutex, mThreadsMutex, mWorkingMutex，最大限度地减少了锁竞争，提高了并发性能
    */
    Thread_pool::Thread *Thread_pool::submitJob(std::function<void()> func)
    {
        std::unique_lock freeLock(mFreeMutex);
        // 如果没有空闲线程
        if(mFreeThreads.empty())
        {
            freeLock.unlock();
            std::unique_lock threadsLock(mThreadsMutex);
            //list 保证插入后元素的指针和引用永远不会失效
            //所以在这里创建线程并存入其他链表是安全的
            Thread *newthread = &mThreads.emplace_back(this);
            threadsLock.unlock();
            newthread->workOn(std::move(func));
            std::lock_guard workingLock(mWorkingMutex);
            mWorkingThreads.push_back(newthread);
            return newthread;
        }
        // 如果由空闲线程
        else
        {
            Thread *FreeThread = std::move(mFreeThreads.front());
            mFreeThreads.pop_front();
            freeLock.unlock();
            FreeThread->workOn(std::move(func));
            std::lock_guard workingLock(mWorkingMutex);
            mWorkingThreads.push_back(FreeThread);
            return FreeThread;
        }
    }

    /*
         RawRun：桥接了同步的线程和异步的协程
    */

    // 无取消版本
    Task<Expected<>> Thread_pool::rawRun(std::function<void()> func)
    {
      auto ready = std::make_shared<FutexAtomic<bool>>(false);
      std::exception_ptr ep;
      submitJob([ready,func = std::move(func),&ep]() mutable{
         try{
            func();
         }catch(...){
            ep = std::current_exception();
         }
         // 释放
         ready->store(true,std::memory_order_release);
         (void)futex_notify_sync(ready.get());
      });
      // 获取
      while(ready->load(std::memory_order_acquire) == false){
         // 使用 co_await futex_wait 挂起直到工作线程完成任务并唤醒它
         (void)co_await futex_wait(ready.get(),false);
      }

      if(ep)[[unlikely]]
         std::rethrow_exception(ep);
      co_return {};
    };

    // 带取消版本
    Task<Expected<>> Thread_pool::rawRun(std::function<void(std::stop_token)> func,
                                    CancelToken cancel)
      {
         auto ready = std::make_shared<FutexAtomic<bool>>(false);
         std::stop_source stop;
         bool stopped = false;
         std::exception_ptr ep;
         submitJob([ready, func = std::move(func), stop = stop.get_token(),
               &ep]() mutable {
        try {
            func(stop);
        } catch (...) {
            ep = std::current_exception();
        }
        ready->store(true, std::memory_order_release);
        (void)futex_notify_sync(ready.get());
    });

    {
      CancelCallback _(cancel,[&]{
         stopped = true;
         stop.request_stop();
      });
      while(ready->load(std::memory_order_acquire) == false){
         (void)co_await futex_wait(ready.get(),false);
      }
    }

    if(ep)[[unlikely]]
      std::rethrow_exception(ep);

      if(stopped)
         co_return std::errc::operation_canceled;

      co_return {};
      }

   std::size_t Thread_pool::threads_count()
   {
      std::lock_guard lock(mThreadsMutex);
      return mThreads.size();
   }

   std::size_t Thread_pool::threads_count()
   {
      std::lock_guard lock(mWorkingMutex);
      return mWorkingThreads.size();
   }

   Thread_pool::Thread_pool() = default;
   Thread_pool::~Thread_pool() = default;

} //namespace zh_async