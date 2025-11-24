#pragma once 
#include<std.hpp> 
#include<utils/non_void_helper.hpp> 

namespace zh_async 
{
    // 定义一个名为 GeneratorResult 的模板结构体，接受两个模板参数 T 和 E（默认为 void）
    template<class T,class E = void>
    struct GeneratorResult
    {
        // 使用 std::variant 来存储 T 类型和 E 类型的值，mValue 是主要的数据成员
        std::variant<T,E> mValue;

        // 构造函数，接受 std::in_place_index_t<0>，表示要构造的是 T 类型的值
        explicit GeneratorResult(std::in_place_index_t<0>, auto &&...args)
        : mValue(std::in_place_index<0>, 
                 std::forward<decltype(args)>(args)...) {} // 完美转发参数

        // 构造函数，接受 std::in_place_index_t<1>，表示要构造的是 E 类型的值
        explicit GeneratorResult(std::in_place_index_t<1>, auto &&...args)
        : mValue(std::in_place_index<1>, 
                 std::forward<decltype(args)>(args)...) {} // 完美转发参数

        // 检查是否有 E 类型的结果，返回 mValue 的索引是否为 1
        bool has_result()const noexcept{ return mValue.index() == 1;}

        // 检查是否有 T 类型的值，返回 mValue 的索引是否为 0
        bool has_value()const noexcept{ return mValue.index() == 0;}

        // 显式转换为 bool 类型，如果有值则返回 true
        explicit operator bool() const noexcept { return has_value(); }

        // 解引用操作符，返回 T 类型的引用，支持左值
        T &operator*() & noexcept { return *std::get_if<0>(&mValue); }

        // 解引用操作符，返回 T 类型的右值引用，支持右值
        T &&operator*() && noexcept { return std::move(*std::get_if<0>(&mValue)); }

        // 解引用操作符，返回 T 类型的常量引用，支持左值
        T const &operator*() const & noexcept { return *std::get_if<0>(&mValue); }

        // 解引用操作符，返回 T 类型的常量右值引用，支持右值
        T const &&operator*() const && noexcept { return std::move(*std::get_if<0>(&mValue)); }
            
        // 访问操作符，返回 T 类型的引用
        T &operator->() noexcept { return std::get_if<0>(mValue); }

        // 获取 T 类型的值，返回 T 类型的引用，支持左值
        T &value() & {return std::get<0>(mValue); }

        // 获取 T 类型的值，返回 T 类型的右值引用，支持右值
        T const &&value() const && { return std::move(std::get<0>(mValue)); }

        // 获取 E 类型的值，返回 E 类型的引用，支持左值
        E &result_unsafe() & noexcept { return *std::get_if<1>(&mValue);}

        // 获取 E 类型的值，返回 E 类型的右值引用，支持右值
        E &&result_unsafe() && noexcept { return std::move(*std::get_if<1>(&mValue));}

        // 获取 E 类型的值，返回 E 类型的常量引用，支持左值
        E const &result_unsafe() const & noexcept { return *std::get_if<1>(&mValue); }

        // 获取 E 类型的值，返回 E 类型的常量右值引用，支持右值
        E const &&result_unsafe() const && noexcept { return std::move(*std::get_if<1>(&mValue));}

        // 获取 E 类型的值，返回 E 类型的引用，支持左值
        E &result() & { return std::get<1>(mValue);}

        // 获取 E 类型的值，返回 E 类型的右值引用，支持右值
        E &&result() && { return std::move(std::get<1>(mValue));}

        // 获取 E 类型的值，返回 E 类型的常量引用，支持左值
        E const &result() const & {  return std::get<1>(mValue);}

        // 获取 E 类型的值，返回 E 类型的常量右值引用，支持右值
        E const &&result() const && {return std::move(std::get<1>(mValue));}
    };

    // 特化 GeneratorResult，当 E 为 void 时，使用 GeneratorResult<T,Void> 作为基类
    template <class T>
    struct GeneratorResult<T,void> : GeneratorResult<T,Void>
    {
        using GeneratorResult<T,Void>::GeneratorResult;
    };
}