#pragma once
#include<std.hpp>
#include<awaiter/task.hpp>

namespace  zh_async
{
    //返回一个不包含任何值的task，表示一个不需要返回值的异步操作
    inline Task<> just_void() { co_return; }

    //返回一个包含该值的Task，用于创建一个异步操作，该操作完成后将返回值 t
    template <class T>
    Task<T> just_value(T t) { co_return std::move(t); }

    //接受一个可调用对象和任意数量的参数，返回一个Task，值是调用该可调用对象的结果
    template<class F,class... Args>
    Task<std::invoke_result_t<F, Args...>> 
    just_invoke(F &&f, Args &&...args) {
    co_return std::invoke(std::forward<F>(f), std::forward<Args>(args)...);
}
}