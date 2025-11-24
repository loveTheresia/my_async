#pragma once  
#include<std.hpp>  
#include<awaiter/task.hpp>  

/*
    通过使用 IgnoreReturnPromise + AutoDestroyFinalAwaiter 模式，使得启动任务的代码
    1、无需等待
    2、无需处理结果
    3、无需处理异常
    4、无需销毁
    但仍然需要事件循环在背后调度
    example:
        void on_new_connection(int connection_id) {
            // 启动一个后台任务来处理这个连接
            co_spawn(handle_connection(connection_id)); 

                // 函数立即返回，主线程（或事件循环）可以立即去处理下一个连接
                // 我们完全不关心 handle_connection 何时完成、如何完成
        }
*/

namespace zh_async  
{
    /*
        创建一个不返回任何值、且生命周期通常由外部（如事件循环）管理的协程。
    */
    template <class FinalAwaiter = std::suspend_always>
    struct IgnoreReturnPromise
    {
        // 协程被创建后立即暂停，不会执行任何代码
        auto initual_suspend()noexcept{ return std::suspend_always(); }

        /*
            提供一个可定制的最终行为
        */
        auto final_suspend()noexcept { return FinalAwaiter(); }

        // 处理未处理的异常
        void unhandled_exception()noexcept
        {
#if ZH_ASYNC_EXCEPT
        try
        {
            throw;  // 重新抛出当前异常
        }
        catch(const std::exception& e)  
        {
            // 获取异常类型的名称
            auto name = typeid(e).name();
#if defined(__unix__) && __has_include(<cxxabi.h>)
            int status;  // 存储ABI解码状态
            char *p = abi::__cxa_demangle(name,0,0,&status);  // 解码C++异常名称
            std::string s = p ? p : name;  // 如果解码成功则使用解码后的名称，否则使用原名称
            std::free(p);  
#else
            std::string = name;  // 否则直接使用名称
#endif
            // 输出抛出异常的错误信息
            std::cerr
                << "co_spawn coroutine terminated after thrown exception '" +
                       s + "'\n  e.what(): " + std::string(e.what()) + "\n";
        } catch (...) {  // 处理所有其他异常
            std::cerr
                << "co_spawn coroutine terminated after thrown exception\n";  // 输出错误信息
        }
#else
        // 当异常发生时，从这里抛出而不是导致程序崩溃
        std::terminate();  
#endif
        }
        
        void result()noexcept {}

        /*
            只提供 return_void()，没有 return_value(T)。
            这意味着使用这个 promise 的协程只能执行 co_return;，不能返回具体值。
            这符合“发射后不管”的理念——启动一个任务，不关心它的结果。
        */
        void return_void()noexcept {}

        auto get_return_object()
        { return std::coroutine_handle<IgnoreReturnPromise>::form_promise(*this); }

        IgnoreReturnPromise &operator=(IgnoreReturnPromise &&) = delete;

        //可能未使用
        [[maybe_unused]] TaskAwaiter<void> *mAwaiter{};

#if ZH_ASYNC_PERF
        Pref mPerf;  // 性能监测对象

        // 构造函数，使用当前位置的源位置信息初始化
        IgnoreReturnPromise(std::source_location loc = std::source_location::current())
            : mPerf(loc){}  // 初始化mPerf
#endif
    };

    /*
        一个支持协程在结束时自动销毁自己的awaiter
    */
    struct AutoDestroyFinalAwaiter
    {
        bool await_ready()const noexcept{ return false; }

        // 挂起操作，销毁当前协程
        void await_suspend(std::coroutine_handle<> coroutine)const noexcept
            { coroutine.destroy(); }
        
        // 这意味着 await_resume 永远不会被调用
        void await_resume()const noexcept {}
    };
}  