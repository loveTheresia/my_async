#pragma once
#include <std.hpp>
#include <awaiter/just.hpp>
#include <awaiter/task.hpp>
#include <awaiter/when_all.hpp>
#include <utils/ilist.hpp>

namespace zh_async {
/*
    提供协程的取消保护
    取消保护用于防止协程在执行到关键操作时被意外取消，
    采用的是协作式的取消，cancelsource只是改变协程内的变量，具体取消逻辑由协程读取变量后执行
    一旦一个协程因为取消信号而执行了 co_return 并结束，它就无法被“恢复”或“重启”
*/
/*
    使用方式：
    1、创建源头：
            auto main_source = zh_async::CancelSource();
    2、分发令牌：
            auto token = main_source.token();
    3、启动协程并传递令牌：
            auto my_task = my_heavy_computation(some_data, token);
    4、在协程内传递取消：
            zh_async::Task<> my_heavy_computation(Data data, zh_async::CancelToken token) {
        for (int i = 0; i < 100000; ++i) {
            // ... do some work ...

            // 取消点：主动检查令牌状态
            if (token.is_canceled()) {
                std::cout << "Computation canceled!" << std::endl;
                co_return; // 优雅退出
            }

            // 或者使用更现代的方式
            // co_await token.as_expect(); // 如果已取消，会直接返回错误
        }
        std::cout << "Computation finished!" << std::endl;
    }
    5、触发取消（在代码的另一个地方）：
        co_await main_source.cancel();
*/

/*
    实现取消源：允许注册取消回调来定制取消行为
*/

struct CancelSourceImpl {

    struct CancellerBase : IntrusiveList<CancellerBase>::NodeType {
        virtual Task<> doCancel() = 0;

        CancellerBase &operator=(CancellerBase &&) = delete;


        bool operator<(CancellerBase const &that) const noexcept {
            return this < &that;
        }

    };


    IntrusiveList<CancellerBase> mCancellers;   // 一个侵入式链表，用来存储所有注册的取消回调
    bool mCanceled;                             // 记录取消源是否已被触发

    //遍历调用所有的取消回调
    Task<> doCancel() {
        if (mCanceled) {
            co_return;
        }
        mCanceled = true;
        if (!mCancellers.empty()) {
            std::vector<Task<>> tasks;
            // 遍历链表调用每个回调的 doCancel 方法
            for (auto &canceller: mCancellers) {
                tasks.push_back(canceller.doCancel());
            }
            // 并发的等待所有异步回调完成
            co_await when_all(tasks);
            mCancellers.clear();
        }
    }

    bool doIsCanceled() const noexcept {
        return mCanceled;
    }

    void doRegister(CancellerBase &canceller) {
        mCancellers.push_front(canceller);
    }

};

struct CancelToken;

struct [[nodiscard]] CancelSourceBase {
protected:
    std::unique_ptr<CancelSourceImpl> mImpl =
        std::make_unique<CancelSourceImpl>();

    friend CancelToken;

    template <class Callback>
    friend struct CancelCallback;

public:
    Task<> cancel() const {
        return mImpl->doCancel();
    }

    inline CancelToken token() const;   // 提供一个可以分发给协程的 CancelToken
    CancelSourceBase() = default;
    CancelSourceBase(CancelSourceBase &&) = default;
    CancelSourceBase &operator=(CancelSourceBase &&) = default;
};

/*
    CancelToken类是协程持有的对象，作为协程任务的观察端
*/
struct [[nodiscard(
    "did you forget to capture or co_await the cancel token?")]] CancelToken {
private:

    CancelSourceImpl *mImpl;    // 一个指向核心实现的裸指针

    explicit CancelToken(CancelSourceImpl *impl) noexcept : mImpl(impl) {}

public:
    CancelToken() noexcept : mImpl(nullptr) {}

    CancelToken(CancelSourceBase const &that) noexcept
        : mImpl(that.mImpl.get()) {}

    Task<> cancel() const {
        //调用关联了取消源，就采用取消回调，否则返回一个空的Task<>
        return mImpl ? mImpl->doCancel() : just_void();
    }

    //令牌是否可以被取消
    [[nodiscard]] bool is_cancel_possible() const noexcept {
        return mImpl;
    }

    //令牌是否被取消
    [[nodiscard]] bool is_canceled() const noexcept {
        return mImpl && mImpl->doIsCanceled();
    }

    [[nodiscard]] operator bool() const noexcept {
        return is_canceled();
    }

    // 布尔状态转换为一个 Expected<> 类型，与 task 的错误处理集成
    Expected<> as_expect() {
        if (mImpl->doIsCanceled()) [[unlikely]] {
            return std::errc::operation_canceled;
        }
        return {};
    }

    void *address() const noexcept {
        return mImpl;
    }

    //类型转换
    static CancelToken from_address(void *impl) noexcept {
        return CancelToken(static_cast<CancelSourceImpl *>(impl));
    }

    auto repr() const {
        return mImpl;
    }


    // 重载()运算符允许CancelToken作为回调函数使用，可以在TaskPromise对象上设置取消状态
    // 即可以使用 co_await a_cancel_token; 来检查取消
    template <class T>
    Expected<> operator()(TaskPromise<T> &promise) const {
        if (is_canceled()) [[unlikely]] {
            return std::errc::operation_canceled;
        }
        return {};
    }

    friend struct CancelSource;

    template <class Callback>
    friend struct CancelCallback;
};

/*
    实现了 取消的级联和组合，
    当父 cancel 被触发时，这个新创建的 CancelSource 的 doCancel() 会被调用，进而触发它自己的取消
*/
struct CancelSource : private CancelSourceImpl::CancellerBase,
                      public CancelSourceBase {
private:
    Task<> doCancel() override {
        return cancel();
    }

public:
    CancelSource() = default;

    // 创造一个新取消源，并将它注册到另一个 cancel 令牌的回调函数中
    explicit CancelSource(CancelToken cancel) {
        if (cancel.mImpl) {
            cancel.mImpl->doRegister(*this);
        }
    }

    auto repr() {
        return mImpl.get();
    }
};

inline CancelToken CancelSourceBase::token() const {
    return *this;
}

/*
    模板类，用于在取消发生时执行自定义逻辑

*/
template <class Callback>
struct [[nodiscard]] CancelCallback : private CancelSourceImpl::CancellerBase {
    explicit CancelCallback(CancelToken cancel, Callback callback)
        : mCallback(std::move(callback)) {
        if (cancel.mImpl) {
            cancel.mImpl->doRegister(*this);
        }
    }

private:
    Task<> doCancel() override {
        std::invoke(std::move(mCallback));
        co_return;
    }

    Callback mCallback;
};

// 如果回调函数返回一个 Awaitable， doCancel就会 co_await 这个回调，这允许在取消时执行清理操作
template <class Callback>
    requires Awaitable<std::invoke_result_t<Callback>>
struct [[nodiscard]] CancelCallback<Callback>
    : private CancelSourceImpl::CancellerBase {
    explicit CancelCallback(CancelToken cancel, Callback callback)
        : mCallback(std::move(callback)) {
        if (cancel.mImpl) {
            cancel.mImpl->doRegister(*this);
        }
    }

private:
    Task<> doCancel() override {
        co_await std::invoke(std::move(mCallback));
    }

    Callback mCallback;
};

template <class Callback>
CancelCallback(CancelToken, Callback) -> CancelCallback<Callback>;

/*
     GetThisCancel = co_cancel
     co_cancel(promise): 从当前协程的 promise 中获取其关联的 CancelToken
     co_cancel.bind(cancel, task): 这是一个静态函数，用于将一个 CancelToken 绑定到一个子任务上。
                                    这是传播取消的关键。
                                    父协程可以把自己的取消令牌（或一个新的）传递给子协程，从而控制子协程的生命周期
    co_cancel.cancel(): 返回一个可调用对象，在协程内部调用时，可以取消当前协程自身
*/
struct GetThisCancel {
    template <class T>
    ValueAwaiter<CancelToken> operator()(TaskPromise<T> &promise) const {
        return ValueAwaiter<CancelToken>(
            CancelToken::from_address(promise.mLocals.mCancelToken));
    }

    template <class T>
    static T &&bind(CancelToken cancel, T &&task) {
        task.promise().mLocals.mCancelToken = cancel.address();
        return std::forward<T>(task);
    }

    struct DoCancelThis {
        template <class T>
        Task<> operator()(TaskPromise<T> &promise) const {
            co_return co_await CancelToken::from_address(
                promise.mLocals.mCancelToken)
                .cancel();
        }
    };

    static DoCancelThis cancel() {
        return {};
    }

};

/*

*/
inline constexpr GetThisCancel co_cancel;

} // namespace zh_async
