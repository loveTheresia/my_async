#pragma once 
#include<std.hpp> 

/*
    解决 C++ 中 void 类型无法实例化、不能作为模板参数直接使用的问题，
    通过引入一个占位类型 Void 和配套的类型萃取机制，实现 void 与非 void 类型的统一处理
*/
namespace zh_async 
{
    struct Void final 
    {
        explicit Void() = default; // 显式构造函数，使用默认构造函数

        template<class T>
        friend constexpr T &&operator,(T &&t,Void){ 
            //允许对 T 类型的参数进行完美转发
            return std::forward<T>(t); 
        }

        template<class T>
        friend constexpr T &&operator|(T &&t,Void){ 
            //允许对 T 类型的参数进行完美转发
            return std::forward<T>(t); 
        }

        // 友元函数定义：重载了按位或运算符，两个 Void 参数
        friend constexpr void operator|(Void,Void){
        }

        //repr返回字符串 "void"
        char constexpr *repr()const noexcept{ 
            return "void"; 
        }
    };

    // 模板结构体 AvoidVoidTrait，默认模板参数为 void
    template<class T = void>
    struct AvoidVoidTrait
    {
        using Type = T; 
        using RefType = std::reference_wrapper<T>; //RefType 等于 T 的引用包装器
        using CRefType = std::reference_wrapper<T const>; //CRefType 等于 T 常量的引用包装器
    };

    // 针对 void 类型的特化版本
    template<>
    struct AvoidVoidTrait<void>
    {
        using Type = Void; 
        using RefType = Void; 
        using CRefType = Void; 
    };

    //获取 AvoidVoidTrait<T> 中的 Type
    template<class T>
    using Avoid = typename AvoidVoidTrait<T>::Type;

    //获取 AvoidVoidTrait<T> 中的 RefType
    template<class T>
    using AvoidRef = typename AvoidVoidTrait<T>::RefType;

    // 获取 AvoidVoidTrait<T> 中的 CRefType
    template<class T>
    using AvoidCRef = typename AvoidVoidTrait<T>::CRefType;

} 