#pragma once
#include <std.hpp>
#include <utils/uninitialized.hpp>

namespace zh_async {
    /*
        ValueAwaiter：一个即时完成的包装器，不会挂起不会等待，只会一直执行
        作用是将一个同步的、已经存在的值，包装成一个可以被 co_await 的对象
        将同步世界与异步世界无缝对接起来
    */
template <class T = void>
struct ValueAwaiter {
private:
    Avoid<T> mValue;

public:
    ValueAwaiter(Avoid<T> value) : mValue(std::move(value)) {}

    bool await_ready() const noexcept {
        return true;
    }

    void await_suspend(std::coroutine_handle<>) const noexcept {}

    T await_resume() {
        if constexpr (!std::is_void_v<T>) {
            return std::move(mValue);
        }
    }
};

    /*
        ValueOrReturnAwaiter：高级决策者：要么返回一个值，要么让协程提前返回。
        其行为完全取决于构造过程：
            模式A：返回一个值
            模式B：提前返回
        ValueOrReturnAwaiter 是实现 await_transform 中自动错误处理的核心引擎
            当 Expected 包含错误时，await_transform 使用模式 B 构造 ValueOrReturnAwaiter
            当 Expected 包含值时，await_transform 使用模式 A 构造 ValueOrReturnAwaiter
        通过这种机制，它将复杂的if/else逻辑封装1在一个优雅的awaiter里
    */
template <class T = void>
struct ValueOrReturnAwaiter {
private:
    std::coroutine_handle<> mPrevious;
    Uninitialized<T> mValue;

public:

    // A：和 ValueAwaiter 完全一样！协程不会挂起，await_resume() 返回构造时存入的值
    template <class... Args>
    ValueOrReturnAwaiter(std::in_place_t, Args &&...args) : mPrevious() {
        mValue.emplace(std::forward<Args>(args)...);
    }

    // B：当前协程的执行流程在此处被中断，控制权直接交还给调用者。这相当于在同步代码中执行了 return 语句
    ValueOrReturnAwaiter(std::coroutine_handle<> previous)
        : mPrevious(previous) {}

    bool await_ready() const noexcept {
        return !mPrevious;
    }

    std::coroutine_handle<>
    await_suspend(std::coroutine_handle<> coroutine) const noexcept {
        // 这里返回了调用者的协程，意味着当前协程将被挂起而且永不恢复
        return mPrevious;
    }

    T await_resume() noexcept {
        if constexpr (!std::is_void_v<T>) {
            return mValue.move();
        }
    }
};
} // namespace zh_async
