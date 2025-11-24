#pragma once 
#include <std.hpp> 

namespace zh_async 
{
    #if __cpp_lib_hardware_interference_size // 检查是否支持硬件干扰大小的宏
    using std::hardware_constructive_interference_size; // 使用标准库中定义的硬件建设性干扰大小
    using std::hardware_destructive_interference_size; // 使用标准库中定义的硬件破坏性干扰大小
    #else // 如果不支持上述宏
    constexpr std::size_t hardware_constructive_interference_size = 64; // 定义一个常量，表示建设性干扰大小为 64 字节
    constexpr std::size_t hardware_destructive_interference_size = 64; // 定义一个常量，表示破坏性干扰大小为 64 字节
    #endif 
} // 结束 zh_async 命名空间