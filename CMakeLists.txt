#set(ZH_ASYNC_DEBUG ON)
#set(ZH_ASYNC_SAFERET ON)
#set(ZH_ASYNC_ALLOC ON)
#set(ZH_ASYNC_PERF ON)
set(ZH_ASYNC_EXCEPT ON)
#set(ZH_ASYNC_ZLIB ON)
#set(ZH_ASYNC_STEAL ON)
#set(ZH_ASYNC_CUDA ON)
#set(ZH_ASYNC_DIRECT ON)
#set(ZH_ASYNC_INVALFIX ON)
#set(ZH_ASYNC_NATIVE ON)
#set(ZH_ASYNC_WARN ON)
#set(ZH_ASYNC_FIND_LIBURING ON)
#set(ZH_ASYNC_FIND_BEARSSL ON)
#set(ZH_ASYNC_JEMALLOC ON)

cmake_minimum_required(VERSION 3.16)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Dubug)
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (CMAKE_BUILD_TYPE MATCHES "Debug" AND NOT DEFINED ZH_ASYNC_DEBUG)
    set(ZH_ASYNC_DEBUG ON)
endif()

project(my_async LANGUAGES CXX)

if(PROJECT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    set(CXXFLAGS)
endif()

# 检查编译器是否能编译源代码
include(CheckCXXSourceCompiles)
set(CMAKE_REQUIRED_FLAGS ${CXXFLAGS})
check_cxx_source_compiles("
#include <coroutine>

int main()
{
    std::coroutine_handle<> c;
    (void)c;
    return 0;
}" HAS_COROUTINE)

check_cxx_source_compiles("
#include <coroutine>

struct awaiter
{
    bool await_ready()const { return false;}
    std::coroutine_handle<> await_suspend(std::coroutine_)const
    {
        return std::noop_coroutine();
    }
    void await_resume() const {}
};

struct symmetric
{
    struct promise_type
    {
        symmetric get_return_object() { return symmetric{}; }
        std::suspend_always initial_suspend() { return {}; }
        std::suspend_always final_suspend()noexcept { return {}; }
        void return_void() {}
        void unhandled_exception(){}
    };
};

symmetric task()
{
    co_await awaiter{};
    co_return;
}

int main()
{
    task();
    return 0;

}" HAS_SYMMETRIC_COROUTINE)
if (NOT HAS_SYMMETRIC_COROUTINE) # 如果不支持
    # message(FATAL_ERROR "Your compiler doesn't support symmetric coroutine transfer")
endif()

file(GLOB_RECURSE interface_sources my_async/*.hpp)         # 递归搜索所有头文件
file(GLOB_RECURSE implementation_sources my_async/*.cpp)    # 递归搜索所有源文件
add_library(my_async OBJECT)                                # 创建名为 my_async 的目标库

target_include_directories(my_async PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}) # 指定当前目录为头文件目录
target_sources(my_async PRIVATE ${implementation_sources})              # 将源文件添加到库中
if(NOT implementation_sources)
    target_sources(my_async PRIVATE script/dummy.cpp)
endif()
if (ZH_ASYNC_DEBUG)  # 如果启用了调试模式
     target_compile_definitions(my_async PUBLIC ZH_ASYNC_DEBUG)  # 定义 ZH_ASYNC_DEBUG
endif()
if (ZH_ASYNC_SAFERET)  # 如果启用了安全返回特性
    target_compile_definitions(my_async PUBLIC ZH_ASYNC_SAFERET)  # 定义 ZH_ASYNC_SAFERET
endif()
if (ZH_ASYNC_ALLOC)  # 如果启用了自定义内存分配特性
    target_compile_definitions(my_async PUBLIC ZH_ASYNC_ALLOC)  # 定义 ZH_ASYNC_ALLOC
endif()
if (ZH_ASYNC_PERF)  # 如果启用了性能优化
    target_compile_definitions(my_async PUBLIC ZH_ASYNC_PERF)  # 定义 ZH_ASYNC_PERF
endif()
if (ZH_ASYNC_EXCEPT)  # 如果启用了异常处理特性
    target_compile_definitions(my_async PUBLIC ZH_ASYNC_EXCEPT)  # 定义 ZH_ASYNC_EXCEPT
endif()
if (ZH_ASYNC_ZLIB)  # 如果启用了 Zlib 支持
    target_compile_definitions(my_async PUBLIC ZH_ASYNC_ZLIB)  # 定义 ZH_ASYNC_ZLIB
    # target_link_libraries(co_async PUBLIC z)  # 链接 Zlib 库（此行被注释掉）
    find_package(ZLIB REQUIRED)  # 查找 Zlib 包，必需
    target_link_libraries(my_async PUBLIC ZLIB::ZLIB)  # 链接 Zlib 库
endif()
if (ZH_ASYNC_STEAL)  # 如果启用了偷取特性
    target_compile_definitions(my_async PUBLIC ZH_ASYNC_STEAL)  # 定义 ZH_ASYNC_STEAL
endif()
if (ZH_ASYNC_CUDA)  # 如果启用了 CUDA 支持
    target_compile_definitions(my_async PUBLIC ZH_ASYNC_CUDA)  # 定义 ZH_ASYNC_CUDA
    find_package(CUDA REQUIRED)  # 查找 CUDA 包，必需
    target_link_libraries(my_async PUBLIC CUDA::CUDA)  # 链接 CUDA 库
endif()
if (ZH_ASYNC_DIRECT)  # 如果启用了直接特性
    target_compile_definitions(my_async PUBLIC ZH_ASYNC_DIRECT)  # 定义 ZH_ASYNC_DIRECT
endif()
if (ZH_ASYNC_INVALFIX)  # 如果启用了无效修复特性
    target_compile_definitions(my_async PUBLIC ZH_ASYNC_INVALFIX)  # 定义 ZH_ASYNC_INVALFIX
endif()
if (ZH_ASYNC_NATIVE)  # 如果启用了本地特性
    target_compile_definitions(my_async PUBLIC ZH_ASYNC_NATIVE)  # 定义 ZH_ASYNC_NATIVE
    target_compile_options(my_async PRIVATE -march=native)  # 添加本地架构优化选项
endif()

if(CMAKE_SYSTEM_NAME MATCHES "Linux")
    if(ZH_ASYNC_FIND_LIBURING) # 搜索liburing库
        include(FindPkgConfig)
        pkg_check_modules(LIBURING liburing)
        if(NOT LIBURING_FOUND)  #如果没找到，则手动添加
            find_path(LIBURING_INCLUDE_DIR NAMES liburing.h)
            find_library(LIBURING_LIBRARY NAMES liburing.a)
            if(NOT LIBURING_INCLUDE_DIR OR NOT LIBURING_LIBRARY) #如果依然没找到，则报错
                message(FATAL_ERROR "liburing not found")
            endif()
            set(LIBURING_LIBRARIES ${LIBURING_LIBRARY})
            set(LIBURING_INCLUDE_DIR ${LIBURING_INCLUDE_DIR})
        endif()
        target_link_libraries(my_async PUBLIC ${LIBURING_LIBRARY})
        target_link_libraries(my_async PUBLIC ${LIBURING_INCLUDE_DIR})
    else()
    endif()
endif()






