#pragma once
#include<std.hpp> 
#include<utils/generator_result.hpp>  
#include<utils/expected.hpp>  
#include<utils/uninitialized.hpp>  // 未初始化内存
#if ZH_ASYNC_PERF
#include<utils/perf.hpp>  // 包含性能测量
#endif
#include<awaiter/concepts.hpp>  // 协程概念
#include<awaiter/details/previous_awaiter.hpp>  // 包含之前的awaiter相关实现
#include<awaiter/details/value_awaiter.hpp>  

namespace zh_async  
{
#if ZH_ASYNC_DEBUG
    inline struct CoroDb {
        std::unordered_set<void*> Coros;  // 存储协程指针的无序集合
    }
#endif

#if ZH_ASYNC_ALLOC
// 任务等待器的内存分配状态
struct TaskAwaiterAllocState {
    std::pmr::memory_resource *mLastAllocator;  // 保存上一个内存分配器

    // 将当前分配器压入栈中
    void push() noexcept {
        mLastAllocator = currentAllocator;  // 记录当前分配器
    }

    // 弹出栈中的分配器
    void pop() noexcept {
        currentAllocator = mLastAllocator;  // 恢复到上一个分配器
    }
};
#endif

    /*
        核心对象：连接两个协程状态的桥梁，协程A挂起时唤醒协程B并将该对象交给协程B
                 协程B将结果写入对象中，最后唤醒协程A时便拿到结果
        在协程中间使用，用于等待另一个协程
    */
    template <class T>
    struct TaskAwaiter
    {
        // 判断是否已经准备就绪
        bool await_ready() const noexcept
        {
#if ZH_ASYNC_SAFERET
# if ZH_ASYNC_EXCEPT
        if (mException) [[unlikely]] {  // 如果存在异常
            return true;  // 返回 true，表示无法继续
        }
# endif
        return mResult.has_value();  // 如果有结果，返回 true
#else
        return false;  
#endif
        }

        // 在协程暂停时被调用
        std::coroutine_handle<> 
        await_suspend(std::coroutine_handle<> coroutine) noexcept {
#if ZH_ASYNC_ALLOC
        mAllocState.push();  // 在分配状态中压入当前分配器
#endif
        mCallerCoroutine = coroutine;  
        return mCalleeCoroutine;  // 表示立刻开始被调用者的协程
    }

    // 恢复协程时调用
        T await_resume() {
#if ZH_ASYNC_ALLOC
        mAllocState.pop();  // 恢复内存分配状态
#endif
#if ZH_ASYNC_EXCEPT
        if (mException) [[unlikely]] {  
            std::rethrow_exception(mException);
        }
#endif
        if constexpr (!std::is_void_v<T>) {  // 如果 T 不是 void
#if ZH_ASYNC_SAFERET
# if ZH_ASYNC_DEBUG
            if constexpr (std::same_as<Expected<>, T>) {}  // 如果 T 是 Expected 类型
            return std::move(mResult.value());  // 移动结果返回
# else
            return std::move(*mResult);  // 移动结果
# endif
#else
            return mResult.move();  // 如果没有异常，直接返回结果
#endif
        }
    }

    // 构造函数，接收协程句柄并初始化
    template <class P>
    explicit TaskAwaiter(std::coroutine_handle<P> coroutine)
        : mCalleeCoroutine(coroutine) { 
        coroutine.promise().mAwaiter = this;  // promise对象可以访问到当前 TaskAwaiter 对象
    }

    TaskAwaiter(TaskAwaiter &&that) = delete;  // 禁止移动构造函数

#if ZH_ASYNC_DEBUG
    // 在调试模式下返回值，包含位置信息
    template <class U, class Loc>
    void returnValue(U &&result, Loc &&loc) {
        mResult.emplace(std::forward<U>(result), std::forward<Loc>(loc));  // 存储结果
    }
#endif

    /*
        用于与 promise_type 协作
    */
    template <class U>
    void returnValue(U &&result) {
        mResult.emplace(std::forward<U>(result));  
    }

    void returnVoid() {
        mResult.emplace();  
    }

    void unhandleException() noexcept {
#if ZH_ASYNC_EXCEPT
        mException = std::current_exception();  // 保存当前异常
#else
        std::terminate();  // 如果没有异常处理，终止程序
#endif  
    }

    // 获取调用者协程的句柄
    std::coroutine_handle<> callerCoroutine() const noexcept { 
        return mCallerCoroutine;  
    }

protected:
    std::coroutine_handle<> mCallerCoroutine;  // 调用协程的句柄（等待者）
    std::coroutine_handle<> mCalleeCoroutine;  // 被调用协程的句柄（被等待者）

#if ZH_ASYNC_SAFERET
    std::optional<Avoid<T>> mResult;  // 可选结果，表示可能没有值
#else
    Uninitialized<Avoid<T>> mResult;  // 存储返回值
#endif  
#if ZH_ASYNC_EXCEPT
    std::exception_ptr mException;  // 异常指针，用于保存异常
#endif
#if ZH_ASYNC_ALLOC
    TaskAwaiterAllocState mAllocState;  // 内存分配状态
#endif
};

// 通常用于等待协程的最终结果，确保协程在完成所有工作后正确返回
// TaskFinalAwaiter 不包含任何信息，只负责协程的结束和资源回收
struct TaskFinalAwaiter
{
    // 判断是否已经准备就绪
    bool await_ready() const noexcept { 
        return false;  
    }

    template<class P> 
    std::coroutine_handle<>  
    await_suspend(std::coroutine_handle<P> coroutine) const noexcept {  
        /*
            coroutine.promise(): 获取这个协程的 promise_type 对象（inner_task 的“内部经理”）
            .mAwaiter: 访问 promise_type 中保存的 TaskAwaiter 指针
            ->callerCoroutine(): 通过这个 TaskAwaiter 指针，获取到它保存的调用者协程的句柄（outer_task 的句柄）。
        */
        return coroutine.promise().mAwaiter->callerCoroutine();  
    } 

    void await_resume() const noexcept {}
};

// 针对生成器 GeneratorResult 结果的等待
template <class T, class E>
struct TaskAwaiter<GeneratorResult<T, E>> {

    bool await_ready() const noexcept {
#if CO_ASYNC_SAFERET
    # if CO_ASYNC_EXCEPT
        // 如果存在异常，返回 true
        if (mException) [[unlikely]] {
            return true;  // 返回 true，表示无法继续
        }
    # endif
        // 如果有有效的结果，返回 true
        return mResult.has_value();
#else
        // 如果未启用异步安全返回，则始终返回 false
        return false;
#endif
    }

    std::coroutine_handle<>
    await_suspend(std::coroutine_handle<> coroutine) noexcept {
#if ZH_ASYNC_ALLOC
        mAllocState.push();  // 在分配状态中压入当前分配器
#endif
        mCallerCoroutine = coroutine;  // 保存当前调用协程的句柄
        return mCalleeCoroutine;  // 返回被调用协程的句柄
    }

    GeneratorResult<T, E> await_resume() {
#if ZH_ASYNC_ALLOC
        mAllocState.pop();  // 恢复分配状态
#endif
#if _AZHSYNC_EXCEPT
        // 如果存在异常，抛出它
        if (mException) [[unlikely]] {
            std::rethrow_exception(mException);
        }
#endif
#if ZH_ASYNC_SAFERET
# if CO_ASYNC_DEBUG
        //通过移动获取结果值
        GeneratorResult<T, E> ret = std::move(mResult.value());
# else
        //通过移动获取结果
        GeneratorResult<T, E> ret = std::move(*mResult);
# endif
        // 重置结果以准备下次使用
        mResult.reset();  
        return ret;  
#else
        // 直接移动结果并返回
        return mResult.move();  
#endif
    }

    // 接收协程句柄并初始化
    template <class P>
    explicit TaskAwaiter(std::coroutine_handle<P> coroutine)
        : mCalleeCoroutine(coroutine) {
        coroutine.promise().mAwaiter = this;  
    }


    TaskAwaiter(TaskAwaiter &&that) = delete;

    // 用于生成器的 yield 操作
    template <class U>
    TaskYieldAwaiter yieldValue(U &&value) {

        mResult.emplace(std::in_place_index<0>, std::forward<U>(value));

        return TaskYieldAwaiter();  
    }

#if ZH_ASYNC_DEBUG
    // 在调试模式下返回值，包含位置信息
    template <class U, class Loc>
    void returnValue(U &&result, Loc &&loc) {
        mResult.emplace(std::in_place_index<1>, std::forward<U>(result),
                        std::forward<Loc>(loc));  // 存储结果
    }
#endif
    // 用于存储生成器的返回值
    template <class U>
    void returnValue(U &&result) {

        mResult.emplace(std::in_place_index<1>, std::forward<U>(result));  
    }

    void returnVoid() {
        mResult.emplace(std::in_place_index<1>); 
    }

    void unhandledException() noexcept {
#if ZH_ASYNC_EXCEPT
        // 将当前异常保存
        mException = std::current_exception();  
#else
        //终止程序
        std::terminate();  
#endif
    }

    // 获取调用者协程的句柄
    std::coroutine_handle<> callerCoroutine() const noexcept {
        return mCallerCoroutine;  
    }

protected:
    // 存储调用者的协程句柄（调用者：发起等待的代码位置）
    std::coroutine_handle<> mCallerCoroutine;  
    // 存储被调用者的协程句柄（被调用者：实际执行任务的实体）
    std::coroutine_handle<> mCalleeCoroutine;  
    
#if ZH_ASYNC_SAFERET
    // 用于存储结果的可选对象
    std::optional<GeneratorResult<T, E>> mResult;  
#else
    // 用于存储未初始化的结果对象
    Uninitialized<GeneratorResult<T, E>> mResult;  
#endif
#if ZH_ASYNC_EXCEPT
    std::exception_ptr mException;  // 存储异常指针
#endif
#if ZH_ASYNC_ALLOC
    // 存储内存分配状态
    TaskAwaiterAllocState mAllocState;  
#endif
};

// 用于销毁被调用者协程
template <class T>
struct TaskOwnedAwaiter : TaskAwaiter<T> {
    using TaskAwaiter<T>::TaskAwaiter;  // 继承构造函数

    // 析构函数，销毁被调用的协程
    ~TaskOwnedAwaiter() {
        TaskAwaiter<T>::mCalleeCoroutine.destroy();  
    }
};

// 前向声明
template <class T>
struct TaskPromise;

/*
    Task 核心数据结构：协程的可操作对象
    用户代码不直接与 promise_type 或 coroutine_handle 打交道，而是通过 Task 类提供的干净接口
    
*/
template <class T = void, class P = TaskPromise<T>>
struct [[nodiscard("did you forgot to co_await?")]] Task { 
    /*
        C++20协程的入口
        它告诉编译器：“当一个函数返回 Task 类型时，
            请使用 P（通常是 TaskPromise<T>）作为它的 promise_type”。
        这是连接用户代码和协程框架的桥梁。
    */
    using promise_type = P; 

    // 默认构造函数
    Task(std::coroutine_handle<promise_type> coroutine = nullptr) noexcept  
        : mCoroutine(coroutine) {}  
    // 移动构造
    Task(Task &&that) noexcept : mCoroutine(that.mCoroutine) {  
        that.mCoroutine = nullptr; 
    }

    Task &operator=(Task &&that) noexcept {  
        std::swap(mCoroutine, that.mCoroutine);  
        return *this; 
    }

    ~Task() {  
        if (mCoroutine) { 
            mCoroutine.destroy();
        }
    }

    /*
        当你 co_await 一个已存在的 Task 变量时，会调用这个版本
        它创建一个 TaskAwaiter，让调用者协程等待这个 Task 完成。Task 对象本身在等待期间继续存在
    */
    auto operator co_await() const & noexcept {  
        return TaskAwaiter<T>(mCoroutine);  
    }

    /*
        当你 co_await 一个临时的 Task 对象（例如 co_await some_async_func()）时，会调用这个版本
        使用 std::exchange Task 对象把自己拥有的协程句柄“交”给了 TaskOwnedAwaiter，然后自己变成了一个空壳。
    */
    auto operator co_await() && noexcept {  
        return TaskOwnedAwaiter<T>(std::exchange(mCoroutine, nullptr));  
    }

    std::coroutine_handle<promise_type> get() const noexcept { 
        return mCoroutine; 
    }

    std::coroutine_handle<promise_type> release() noexcept {  
        return std::exchange(mCoroutine, nullptr);  // 交换并释放 mCoroutine
    }

    promise_type &promise() const { 
        return mCoroutine.promise();  
    }

private:
    /*
        存储协程句柄
        * 它通过移动语义 (Task(Task&&)) 确保一个协程在任何时候只被一个 Task 对象所拥有
        * 在它的析构函数 ~Task() 中，如果 Task 对象被销毁时它所拥有的协程还没有执行完，
            它会自动调用 mCoroutine.destroy() 来回收协程占用的内存（协程帧）
        * 
    */
    std::coroutine_handle<promise_type> mCoroutine;  
};

struct TaskPromiseLocal { 
    // 取消命令的令牌
    void *mCancelToken = nullptr;  
};

/*
    promise_type 不是 C++ 的一个具体类，而是一个“契约”或“接口规范”，
    是协程的“内部控制器”或“灵魂”。它决定了协程从创建、执行、暂停、返回到销毁的整个生命周期的行为。
    当你定义一个返回类型为 Task 的协程函数时，C++ 编译器会寻找 Task 内部定义的 promise_type。
    这个 promise_type 必须提供几个特定的成员函数（如 initial_suspend, final_suspend, get_return_object 等），
    编译器会根据这些函数来生成协程的状态机和控制逻辑

    手写这两个类是典型的面向对象设计模式
*/
struct TaskPromiseCommonBase {
    // 协程初始挂起
    auto initial_suspend() noexcept {
        return std::suspend_always(); 
        /*
            当一个协程函数被调用时，它创建的协程不会立即开始执行，而是立即被挂起。
            这给了调用者完全的控制权。调用者拿到 Task 对象后，可以决定何时（通过 co_await）真正启动这个协程
        */
    }

    // 协程结束时的挂起
    auto final_suspend() noexcept {
        return TaskFinalAwaiter();  // 协程在结束时应该等待 TaskFinalAwaiter
    }

    TaskPromiseCommonBase() = default;  
    TaskPromiseCommonBase(TaskPromiseCommonBase &&) = delete;  
#if ZH_ASYNC_ALLOC
    // 重载 new 操作符，支持自定义内存资源
    void *operator new(std::size_t size) {
        return std::pmr::get_default_resource()->allocate(size);  // 使用默认内存资源分配内存
    }

    // 重载 delete 操作符
    void operator delete(void *ptr, std::size_t size) noexcept {
        std::pmr::get_default_resource()->deallocate(ptr, size);  // 释放内存
    }
#endif
};

/*
    提供与派生类交互的桥梁
    它
*/
template <class TaskPromise>
struct TaskPromiseCommon : TaskPromiseCommonBase {

    TaskPromise &self() noexcept { 
        /*
            使用了 C++ 中一个非常强大的设计模式——CRTP (Curiously Recurring Template Pattern，奇异递归模板模式)
            让基类 TaskPromiseCommon 获取到其派生类（例如 TaskPromise<int>）的引用

        */
        return static_cast<TaskPromise &>(*this);  
    }

    TaskPromise const &self() const noexcept { 
        return static_cast<TaskPromise const &>(*this); 
    }

    void unhandled_exception() noexcept {
        /*
            作为基类 TaskPromiseCommon 并不知道mAwaiter是什么
            但可以通过这种设计模型调用派生类的 mAwaiter 方法
        */
        self().mAwaiter->unhandledException();  
    }

    // 协程完成时调用，获取与当前 promise 对象关联的 std::coroutine_handle<TaskPromise> 对象
    auto get_return_object() {
        return std::coroutine_handle<TaskPromise>::from_promise(self());
    }
};

/*
    用于处理类型转换
    它解决的是一个本身就非常复杂的问题：如何让错误处理在异步代码中变得既强大又自然
    也就是所有的报错都不需要调用 co_await 的线程去处理
        于是，我们就必须处理 Expected 类型的所有可能形式，包括：
        单个值 Expected<T>
        void 值 Expected<void>
        一组值 std::vector<Expected<T>>
        一组不同类型的值 std::tuple<Expected<T1>, Expected<T2>, ...>
        调试信息（错误发生的位置）
    每一种情况都需要一个专门的重载来处理，这就是代码看起来复杂的原因。
    这种复杂性被封装在框架内部，是为了换取用户代码的极致简洁
*/
template <class TaskPromise>
struct TaskPromiseExpectedTransforms {
    TaskPromise &self() noexcept {
        return static_cast<TaskPromise &>(*this);  // 向下转换为 TaskPromise 类型
    }

    TaskPromise const &self() const noexcept {
        return static_cast<TaskPromise const &>(*this);  // 向下转换为 TaskPromise const 类型
    }

#if ZH_ASYNC_DEBUG
# define ZH_ASYNC_EXPECTED_LOCATION_FORWARD(e) , (e).mErrorLocation  // 调试模式下的错误来源信息
#else
# define ZH_ASYNC_EXPECTED_LOCATION_FORWARD(e)  // 非调试模式下的宏定义
#endif

    /*
        那么实现思路是什么呢？ ---await_transform + ValueOrReturnAwaiter 的双簧
        1. await_transform：协程的“海关”
            当一个协程执行 co_await some_expression 时，
            协程框架会先检查它的 promise_type 是否有一个名为 await_transform 的函数，
            并且其参数能匹配 some_expression 的类型。
                如果有：框架会调用 promise.await_transform(some_expression)，然后对返回的结果执行 co_await。
                如果没有：框架直接对 some_expression 本身执行 co_await。
            await_transform 就像一个“海关”，允许 promise_type 在“货物”（some_expression）进入协程之前，
            对其进行“检查”和“改装”。
        2. ValueOrReturnAwaiter：决策者
            await_transform 的所有重载都返回同一个类型的对象：ValueOrReturnAwaiter。
            这个 Awaiter 的名字已经揭示了它的使命：要么返回一个值，要么让协程提前返回。让后续代码永远不执行
    */
    template <class T2>  // 用于处理期待的右值引用类型
    ValueOrReturnAwaiter<T2> await_transform(Expected<T2> &&e) noexcept {
        if (e.has_error()) [[unlikely]] {  // 如果有错误，就调用 returnValue 返回
            self().mAwaiter->returnValue(
                std::move(e).error() ZH_ASYNC_EXPECTED_LOCATION_FORWARD(e));  
            return {self().mAwaiter->callerCoroutine()};  // 返回调用者协程
        }
        if constexpr (std::is_void_v<T2>) {  // 如果 T2 是 void 类型
            return {std::in_place};  // 返回一个不包含值的 ValueOrReturnAwaiter
        } else {
            return {std::in_place, *std::move(e)};  // 否则返回一个带有值的 ValueOrReturnAwaiter
        }
    }

    template <class T2>
    ValueOrReturnAwaiter<T2> await_transform(Expected<T2> &e) noexcept {
        return await_transform(std::move(e));  //右值返回
    }

    // 处理 GeneratorResult 的变换
    template <class T2, class E2>
    ValueOrReturnAwaiter<GeneratorResult<T2, E2>>
    await_transform(GeneratorResult<T2, Expected<E2>> &&g) noexcept {
        if (g.has_value()) {  // 如果有值
            if constexpr (std::is_void_v<T2>) {
                return {std::in_place, std::in_place_index<0>};  // 返回一个不含值的結果
            } else {
                return {std::in_place, std::in_place_index<0>, std::move(*g)};  // 返回一个带有值的结果
            }
        } else {
            auto e = g.result_unsafe();  
            if (e.has_error()) [[unlikely]] {  
                self().mAwaiter->returnValue(
                    std::move(e).error() CO_ASYNC_EXPECTED_LOCATION_FORWARD(e)); 
                return {self().mAwaiter->callerCoroutine()};  // 返回调用者协程
            } else {
                if constexpr (std::is_void_v<E2>) {
                    return {std::in_place, std::in_place_index<1>, std::move(*e)};  // 返回一个不含值的结果
                } else {
                    return {std::in_place, std::in_place_index<1>};  // 返回另一个含值的结果
                }
            }
        }
    }

    template <class T2, class E2>
    ValueOrReturnAwaiter<GeneratorResult<T2, E2>>
    await_transform(GeneratorResult<T2, Expected<E2>> &g) noexcept {
        return await_transform(std::move(g));  // 转发给右值版本
    }

    // 处理 Expected<void> 的向量
    ValueOrReturnAwaiter<void>
    await_transform(std::vector<Expected<void>> &&e) noexcept {
        for (std::size_t i = 0; i < e.size(); ++i) {  
            if (e[i].has_error()) [[unlikely]] {  
                self().mAwaiter->returnValue(
                    std::move(e[i]).error() 
                        CO_ASYNC_EXPECTED_LOCATION_FORWARD(e[i]));  // 返回错误信息
                return {self().mAwaiter->callerCoroutine()};  // 返回调用者协程
            }
        }
        return {std::in_place};  // 如果没有错误，返回无值
    }

    ValueOrReturnAwaiter<void>
    await_transform(std::vector<Expected<void>> &e) noexcept {
        return await_transform(std::move(e)); 
    }

    template <class T2, class E2>
    ValueOrReturnAwaiter<std::vector<T2>>
    await_transform(std::vector<Expected<T2>> &&e) noexcept {
        for (std::size_t i = 0; i < e.size(); ++i) { 
            if (e[i].has_error()) [[unlikely]] {  
                self().mAwaiter->returnValue(
                    std::move(e[i]).error() 
                        CO_ASYNC_EXPECTED_LOCATION_FORWARD(e[i]));  
                return {self().mAwaiter->callerCoroutine()};  // 返回调用者协程
            }
        }
        std::vector<T2> ret;  // 创建返回值 vector
        ret.reserve(e.size());  // 预留空间
        for (std::size_t i = 0; i < e.size(); ++i) {
            ret.emplace_back(*std::move(e[i]));  // 移动元素到返回 vector
        }
        return {std::in_place, std::move(ret)};  // 返回包含值的 Awaiter
    }

    template <class T2>
    ValueOrReturnAwaiter<std::vector<T2>>
    await_transform(std::vector<Expected<T2>> &e) noexcept {
        return await_transform(std::move(e)); 
    }

    template <class... Ts>
    ValueOrReturnAwaiter<std::tuple<Avoid<Ts>...>>
    await_transform(std::tuple<Expected<Ts>...> &&e) noexcept {
        return [&]<std::size_t... Is>(std::index_sequence<Is...>) 
                   -> ValueOrReturnAwaiter<std::tuple<Avoid<Ts>...>> {
            if (!([&]() -> bool {  // 检查每个元素是否有错误
                    if (std::get<Is>(e).has_error()) [[unlikely]] {
                        self().mAwaiter->returnValue(
                            std::move(std::get<Is>(e)).error() 
                            CO_ASYNC_EXPECTED_LOCATION_FORWARD(std::get<Is>(e)));
                        return false;  // 返回 false 表示发生错误
                    }
                    return true;  
                }() && ...)) {
                return {self().mAwaiter->callerCoroutine()};  // 返回调用者协程
            }
            return {std::in_place, [&]() -> decltype(auto) {  
                        return *std::move(std::get<Is>(e)), Void();  // 返回元组中的值
                    }()...};  
        }(std::make_index_sequence<sizeof...(Ts)>());  // 生成索引序列
    }

    template <class... Ts>
    ValueOrReturnAwaiter<std::tuple<Avoid<Ts>...>>
    await_transform(std::tuple<Expected<Ts>...> &e) noexcept {
        return await_transform(std::move(e));  
    }
};

// 用于实现协程中 Task 的转换

template <class TaskPromise>
struct TaskPromiseTransforms {
    TaskPromise &self() noexcept {  
        return static_cast<TaskPromise &>(*this);  // 向下转换为 TaskPromise 类型
    }

    TaskPromise const &self() const noexcept {  
        return static_cast<TaskPromise const &>(*this);  // 向下转换为 TaskPromise const 类型
    }

    //将当前 promise 的 mLocals 设置到任务的 promise 中
    template <class U>
    Task<U> &&await_transform(Task<U> &&u) noexcept {
        u.promise().mLocals = self().mLocals;  
        return std::move(u);  // 返回移动后的任务
    }

    template <class U>
    Task<U> const &await_transform(Task<U> const &u) noexcept {
        u.promise().mLocals = self().mLocals;  
        return u;  
    }

    // 用于处理可调用的类型 U，返回
    template <std::invocable<TaskPromise &> U>
    auto await_transform(U &&u) noexcept(noexcept(u(self()))) {
        return self().await_transform(u(self())); 
    }

    // 用于处理不可调用的类型 U，直接转发出去
    template <class U>
        requires(!std::invocable<U, TaskPromise &>)
    U &&await_transform(U &&u) noexcept {
        return std::forward<U>(u);   
    }
};

// 定义 TaskPromise 的实现
template <class TaskPromise, class T>
struct TaskPromiseImpl : TaskPromiseCommon<TaskPromise>,
                         TaskPromiseTransforms<TaskPromise> {};

template <class TaskPromise, class T>
struct TaskPromiseImpl<TaskPromise, Expected<T>>
    : TaskPromiseCommon<TaskPromise>,
      TaskPromiseTransforms<TaskPromise>,
      TaskPromiseExpectedTransforms<TaskPromise> {
    using TaskPromiseTransforms<TaskPromise>::await_transform;
    using TaskPromiseExpectedTransforms<TaskPromise>::await_transform;
    static_assert(std::is_void_v<std::void_t<T>>);
};

    /*
        核心数据结构 TaskPromise 
        将复杂的逻辑都放在基类中，而本身只关注处理与返回值类型 T 相关的具体事务。
    */
template <class T>
struct TaskPromise : TaskPromiseImpl<TaskPromise<T>, T> {
    // 定义了返回值的“接收器”
    // 当执行 co_return 123 时，实际上是在调用 promise.return_value(123)
    void return_value(T &&ret) {
        mAwaiter->returnValue(std::move(ret));  
    }

    void return_value(T const &ret) {
        mAwaiter->returnValue(ret); 
    }

#if ZH_ASYNC_DEBUG
    // 在调试模式下返回值，包含位置信息
    void
    return_value(std::convertible_to<T> auto &&ret,
                 std::source_location loc = std::source_location::current())
        requires std::constructible_from<T, decltype(ret), std::source_location>
    {
        mAwaiter->returnValue(std::forward<decltype(ret)>(ret), loc);  // 存储返回值和位置信息
    }

    void return_value(std::convertible_to<T> auto &&ret)
        requires(
            !std::constructible_from<T, decltype(ret), std::source_location>)
    {
        mAwaiter->returnValue(std::forward<decltype(ret)>(ret));  // 存储返回值
    }
#else
    // 设置协程的返回值
    void return_value(std::convertible_to<T> auto &&ret) {
        mAwaiter->returnValue(std::forward<decltype(ret)>(ret)); 
    }
#endif
    /*
        关键枢纽。
        当一个 TaskAwaiter 被创建时（在 co_await 时），它会把自己的地址设置到这个 mAwaiter 指针中
        当协程执行 co_return 时，return_value 函数通过这个 mAwaiter 指针，
        调用 mAwaiter->returnValue(...)，将结果传递出去
        当协程发生未捕获的异常时，基类的 unhandled_exception 也是通过这个指针调用 mAwaiter->unhandledException()
    */
    TaskAwaiter<T> *mAwaiter{};  
    TaskPromiseLocal mLocals{};  // 存储与这个协程实例相关的本地数据。

#if ZH_ASYNC_PERF
    Perf mPerf;  // 性能追踪
    // 使用构造函数记录对象创建时间
    TaskPromise(std::source_location loc = std::source_location::current())
        : mPerf(loc) {}  // 初始化性能追踪
#endif
};

template <>
struct TaskPromise<void> : TaskPromiseImpl<TaskPromise<void>, void> 
{
    void return_void() {
        mAwaiter->returnVoid();
    }

    TaskPromise() = default;
    TaskPromise(TaskPromise &&) = delete;

    TaskAwaiter<void> *mAwaiter{};
    TaskPromiseLocal mLocals{};

#if ZH_ASYNC_PERF
    Perf mPerf;  // 性能追踪
    // 使用构造函数记录对象创建时间
    TaskPromise(std::source_location loc = std::source_location::current())
        : mPerf(loc) {}  // 初始化性能追踪
#endif
};

// 针对生成器的 TaskPromise 实现
template <class T, class E>
struct TaskPromise<GeneratorResult<T, E>> : TaskPromiseImpl<TaskPromise<GeneratorResult<T, E>>, E> {
    auto yield_value(T &&ret) {
        return mAwaiter->yieldValue(std::move(ret));  
    }

    auto yield_value(T const &ret) {
        return mAwaiter->yieldValue(ret);  
    }

    auto yield_value(std::convertible_to<T> auto &&ret) {
        return mAwaiter->yieldValue(std::forward<decltype(ret)>(ret));  // 存储 yield 的值
    }

    void return_value(E &&ret) {
        mAwaiter->returnValue(std::move(ret)); 
    }

    void return_value(E const &ret) {
        mAwaiter->returnValue(ret); 
    }

    void return_value(std::convertible_to<E> auto &&ret) {
        mAwaiter->returnValue(std::forward<decltype(ret)>(ret)); 
    }

    TaskAwaiter<GeneratorResult<T, E>> *mAwaiter{};  // 存储任务等待器
    TaskPromiseLocal mLocals{};  // 包含取消命令的令牌

#if ZH_ASYNC_PERF
    Perf mPerf;  // 性能追踪

    TaskPromise(std::source_location loc = std::source_location::current())
        : mPerf(loc) {}  // 初始化性能追踪
#endif
};

// 针对没有返回值的生成器的 TaskPromise 实现
template <class T>
struct TaskPromise<GeneratorResult<T, void>> 
    : TaskPromiseImpl<TaskPromise<GeneratorResult<T, void>>, void> {
    auto yield_value(T &&ret) {
        return mAwaiter->yieldValue(std::move(ret)); 
    }

    auto yield_value(T const &ret) {
        return mAwaiter->yieldValue(ret); 
    }

    auto yield_value(std::convertible_to<T> auto &&ret) {
        return mAwaiter->yieldValue(std::forward<decltype(ret)>(ret)); 
    }

    void return_void() {
        mAwaiter->returnVoid();  
    }

    TaskAwaiter<GeneratorResult<T, void>> *mAwaiter{};  
    TaskPromiseLocal mLocals{}; 

#if CO_ASYNC_PERF
    Perf mPerf;  // 性能追踪

    TaskPromise(std::source_location loc = std::source_location::current())
        : mPerf(loc) {}  // 初始化性能追踪
#endif
};

// 自定义 promise 的实现
template <class T, class P>
struct CustomPromise : TaskPromise<T> {
    // 创建协程的返回对象
    auto get_return_object() {
        static_assert(std::is_base_of_v<CustomPromise, P>);  // 确保 P 是 CustomPromise 的派生类
        return std::coroutine_handle<P>::from_promise(static_cast<P &>(*this));  // 返回 promise 的句柄
    }
};

// 绑定可调用对象并返回
template <class F, class... Args>
    requires(Awaitable<std::invoke_result_t<F, Args...>>)
inline std::invoke_result_t<F, Args...> co_bind(F f, Args... args) {
    co_return co_await std::move(f)(std::move(args)...);  
}

} // namespace zh_async

#define co_awaits co_await co_await  
#define co_returns \
    co_return {}  