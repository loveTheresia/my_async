#pragma once
#include<std.hpp>

namespace zh_async
{
#if ZH_ASYNC_ALLOC
    using String = std::pmr::string;
#else
    using String = std::string;
#endif

    //用户定义的字符串字面量运算符，实现了字面量向字符串的转换，例如：“hello”_s 会创建一个String("hello")
    inline String operator""_s(char const *str,size_t len)
    { return String(str,len); }

    //std::pmr::memory_resource: C++17新增的内存管理的抽象基类，允许自定义内存分配策略
    //currentAllocator 指向当前使用的内存资源
    extern thread_local std::pmr::memory_resource *currentAllocator;

    /*
        当ReplaceAllocator对象被创建时，它会保存当前的currentAllocator，
        并用传入的allocator替代currentAllocator
        当ReplaceAllocator被销毁时，会恢复之前的currentAllocator。
        者允许在特定的作用域内使用自定义的内存分配策略
    */
    struct ReplaceAllocator
    {
        ReplaceAllocator(std::pmr::memory_resource *allocator)
        {
            lastAllocator = currentAllocator;
            currentAllocator = allocator;
        }

        ReplaceAllocator(ReplaceAllocator &&) = delete;

        ~ReplaceAllocator() { currentAllocator = lastAllocator; }

    private:
        std::pmr::memory_resource *lastAllocator;
    };

}