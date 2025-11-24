#pragma once
#include <std.hpp>

namespace zh_async {
template <class T>
//环形队列
struct RingQueue {
    std::unique_ptr<T[]> mHead;// 使用智能指针创建一个动态数组，用于存储队列元素
    T *mTail;// 指向队列尾部的指针
    T *mRead;// 读取指针，指向当前读取的位置
    T *mWrite;// 写入指针，指向下一个写入的位置

    explicit RingQueue(std::size_t maxSize = 0)
        : mHead(maxSize ? std::make_unique<T[]>(maxSize) : nullptr),
          mTail(maxSize ? mHead.get() + maxSize : nullptr),
          mRead(mHead.get()),
          mWrite(mHead.get()) {}

          //重置所有指针
    void set_max_size(std::size_t maxSize) {
        mHead = maxSize ? std::make_unique<T[]>(maxSize) : nullptr;
        mTail = maxSize ? mHead.get() + maxSize : nullptr;
        mRead = mHead.get();
        mWrite = mHead.get();
    }

    //指针偏移运算，返回队列的最大大小
    [[nodiscard]] std::size_t max_size() const noexcept {
        return mTail - mHead.get();
    }

    //返回当前队列的大小
    [[nodiscard]] std::size_t size() const noexcept {
        return static_cast<std::size_t>(mWrite - mRead + max_size()) %
               max_size();
    }

    [[nodiscard]] bool empty() const noexcept {
        return mRead == mWrite;
    }

    [[nodiscard]] bool full() const noexcept {
        T *nextWrite = mWrite == mTail ? mHead.get() : mWrite + 1;
        return nextWrite == mRead;// 如果下一个写入位置等于读取位置，则队列满
    }

    [[nodiscard]] std::optional<T> pop() {
        if (mRead == mWrite) {
            return std::nullopt;
        }
        T p = std::move(*mRead);
        mRead = mRead == mTail ? mHead.get() : mRead + 1;
        return p;
    }

    [[nodiscard]] T pop_unchecked() {
        T p = std::move(*mRead);
        mRead = mRead == mTail ? mHead.get() : mRead + 1;
        return p;
    }

    [[nodiscard]] bool push(T &&value) {
        T *nextWrite = mWrite == mTail ? mHead.get() : mWrite + 1;
        if (nextWrite == mRead) {
            return false;
        }
        *mWrite = std::move(value);
        mWrite = nextWrite;
        return true;
    }

    void push_unchecked(T &&value) {
        T *nextWrite = mWrite == mTail ? mHead.get() : mWrite + 1;
        *mWrite = std::move(value);
        mWrite = nextWrite;
    }
};

//无限队列
template <class T>
struct InfinityQueue {
    [[nodiscard]] std::optional<T> pop() {
        if (mQueue.empty()) {
            return std::nullopt;
        }
        T value = std::move(mQueue.front());
        mQueue.pop_front();
        return value;
    }

    [[nodiscard]] T pop_unchecked() {
        T value = std::move(mQueue.front());
        mQueue.pop_front();
        return value;
    }

    void push(T &&value) {
        mQueue.push_back(std::move(value));
    }

private:
    std::deque<T> mQueue;
};
} // namespace zh_async
