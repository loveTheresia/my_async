#set(CO_ASYNC_DEBUG ON)
#set(CO_ASYNC_SAFERET ON)
#set(CO_ASYNC_ALLOC ON)
#set(CO_ASYNC_PERF ON)
#set(CO_ASYNC_EXCEPT ON)
#set(CO_ASYNC_ZLIB ON)
#set(CO_ASYNC_STEAL ON)
#set(CO_ASYNC_CUDA ON)
#set(CO_ASYNC_DIRECT ON)
#set(CO_ASYNC_INVALFIX ON)
#set(CO_ASYNC_NATIVE ON)
# set(CO_ASYNC_WARN ON)
#set(CO_ASYNC_FIND_LIBURING ON)
#set(CO_ASYNC_FIND_BEARSSL ON)
#set(CO_ASYNC_JEMALLOC ON)

cmake_minimum_required(VERSION 3.16)

if (NOT CMAKE_BUILD_TYPE) # 如果没有指定构建类型
    set(CMAKE_BUILD_TYPE Release) # 默认设置为 Release 模式
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON) # 强制使用指定的 C++ 标准
set(CMAKE_CXX_EXTENSIONS OFF) # 禁用 C++ 扩展
set(CMAKE_EXPORT_COMPILE_COMMANDS ON) # 导出编译命令供其他工具使用

if (CMAKE_BUILD_TYPE MATCHES "Debug" AND NOT DEFINED CO_ASYNC_DEBUG)
    set(CO_ASYNC_DEBUG ON)
endif()

project(co_async LANGUAGES CXX)

if (CO_ASYNC_CUDA)
    enable_language(CUDA)
endif()

if (PROJECT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR) # 如果项目源目录与 CMake 源目录相同
    set(CXXFLAGS)
    if (MSVC)
        list(APPEND CXXFLAGS -utf-8 -Zc:preprocessor -permissive- -EHsc)
        add_definitions(-DNOMINMAX)
        if (CO_ASYNC_WARN)
            list(APPEND CXXFLAGS -W4)
        endif()
    else()
        if (CO_ASYNC_WARN)# 如果启用警告
            list(APPEND CXXFLAGS -Wall -Wextra -Wno-interference-size -Werror=return-type
            -Werror=unused-result -Werror=uninitialized -Werror=maybe-uninitialized
            -Wno-unused-parameter -Wunused-but-set-variable -Wunused-but-set-parameter -Wunused-function
            -Wunused-const-variable -Werror=use-after-free=3 -Werror=invalid-memory-model -Wunused-value
            -Wexceptions -Werror=missing-declarations -Werror=missing-field-initializers
            -Wparentheses -Wconversion -Werror=float-conversion -Wsign-conversion -Wsign-compare -Wno-terminate
            -Werror=delete-non-virtual-dtor -Werror=suggest-override -Wsign-promo -Wold-style-cast
            -Wrange-loop-construct -Werror=reorder -Werror=class-memaccess -Werror=narrowing
            -Wdeprecated-enum-enum-conversion -Werror=deprecated-copy-dtor -Werror=deprecated-copy
            -Wzero-as-null-pointer-constant -Wplacement-new=2 -Wsuggest-final-types -Wsuggest-final-methods
            -Werror=conversion-null -Werror=mismatched-new-delete -Werror=address -Wlogical-op -Wlogical-not-parentheses
            -Wattributes -Wmissing-declarations -Werror=multichar -Werror=overflow -Werror=restrict -Werror=vla
            -Wstrict-aliasing=1 -Werror=string-compare -Werror=stringop-overflow=2 -Werror=shift-overflow
            -Wsuggest-attribute=const -Wsuggest-attribute=pure -Wsuggest-attribute=noreturn -Werror=alloca
            -Werror=array-bounds -Werror=bool-compare -Werror=bool-operation -Werror=zero-length-bounds
            -Werror=div-by-zero -Wno-shadow -Werror=format -Werror=pointer-arith -Werror=write-strings
            -Werror=dangling-pointer=2 -Werror=return-local-addr -Wempty-body -Wimplicit-fallthrough
            -Wswitch -Wno-unknown-warning-option)# 添加多个编译器警告标志
        elseif(CO_ASYNC_DEBUG)# 如果启用了调试模式
            list(APPEND CXXFLAGS -Wall -Wextra -Wno-interference-size -Wreturn-type -Wno-unused-parameter )
        endif()# 添加调试相关的警告标志
        if (CO_ASYNC_DEBUG)
            list(APPEND CXXFLAGS -Wno-exceptions)
        else() # 如果不是调试模式
            if (CMAKE_BUILD_TYPE MATCHES "[Rr][Ee][Ll][Ee][Aa][Ss][Ee]")
                list(APPEND CXXFLAGS -flto)  # 启用链接时间优化
            endif()
        endif()
    endif()
    if (CMAKE_CXX_COMPILER_ID MATCHES "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 10.0.0 AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 11.0.0)
        #add_definitions(-D__cplusplus=202002L)
        list(APPEND CXXFLAGS -fcoroutines) # 启用协程支持
        if (IS_DIRECTORY /usr/include/c++/10) # 检查特定目录是否存在
            include_directories($<$<COMPILE_LANG_AND_ID:CXX,GNU>:/usr/include/c++/10>)
        endif()
        if (CO_ASYNC_WARN)
            list(APPEND CXXFLAGS -Wno-zero-as-null-pointer-constant -Wno-unused-const-variable)
        endif()
        list(APPEND CXXFLAGS -Wno-attributes)
        if (CO_ASYNC_DEBUG)
            list(APPEND CXXFLAGS -Wno-terminate)
        endif()
    else()
        if (CO_ASYNC_WARN)
            list(APPEND CXXFLAGS -Werror)
        endif()
    endif()
endif()

include(CheckCXXCompilerFlag)  # 引入检查 C++ 编译器标志的模块
foreach (flag IN LISTS CXXFLAGS)  # 遍历 CXXFLAGS 中的每个标志
    set(name ${flag})
    # replace - and = with _ and remove _ prefix and uppercase
    string(REGEX REPLACE "[-=:]" "_" name ${name})
    string(TOUPPER ${name} name)
    string(REGEX REPLACE "^_" "" name ${name})
    # check if the compiler supports the warning
    check_cxx_compiler_flag(${flag} HAS_${name})
    if (HAS_${name})
        add_compile_options(${flag})
    endif()
endforeach()

# 检查编译器是否能够编译示例源代码
include(CheckCXXSourceCompiles)
set(CMAKE_REQUIRED_FLAGS ${CXXFLAGS})
check_cxx_source_compiles("
#include <coroutine>

int main() {
    std::coroutine_handle<> c;
    (void)c;
    return 0;
}" HAS_COROUTINE)
if (NOT HAS_COROUTINE)
    message(FATAL_ERROR "Your compiler doesn't support coroutine")
endif()

# 检查编译器是否支持对称协程
check_cxx_source_compiles("
#include <coroutine>

struct awaiter {
    bool await_ready() const { return false; }
    std::coroutine_handle<> await_suspend(std::coroutine_handle<>) const {
        return std::noop_coroutine();
    }
    void await_resume() const {}
};

struct symmetric {
    struct promise_type {
        symmetric get_return_object() { return symmetric{}; }
        std::suspend_always initial_suspend() { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        void return_void() {}
        void unhandled_exception() {}
    };
};

symmetric task() {
    co_await awaiter{};
    co_return;
}

int main() {
    task();
    return 0;
}" HAS_SYMMETRIC_COROUTINE)
if (NOT HAS_SYMMETRIC_COROUTINE) # 如果不支持
    message(FATAL_ERROR "Your compiler doesn't support symmetric coroutine transfer")
endif()


add_library(co_async OBJECT) # 创建名为 co_async 的目标库
file(GLOB_RECURSE interface_sources co_async/*.hpp) # 递归搜索所有头文件
file(GLOB_RECURSE implementation_sources co_async/*.cpp) # 递归搜索所有源文件
target_include_directories(co_async PUBLIC .) # 设置公共包含目录为当前目录
target_sources(co_async PRIVATE ${implementation_sources}) # 将实现源文件设置为私有源
if (NOT implementation_sources) # 如果没有找到实现源文件
    target_sources(co_async PRIVATE scripts/dummy.cpp) # 添加一个虚拟源文件
endif()
if (CO_ASYNC_DEBUG)  # 如果启用了调试模式
     target_compile_definitions(co_async PUBLIC CO_ASYNC_DEBUG)  # 定义 CO_ASYNC_DEBUG
endif()
if (CO_ASYNC_SAFERET)  # 如果启用了安全返回特性
    target_compile_definitions(co_async PUBLIC CO_ASYNC_SAFERET)  # 定义 CO_ASYNC_SAFERET
endif()
if (CO_ASYNC_ALLOC)  # 如果启用了自定义内存分配特性
    target_compile_definitions(co_async PUBLIC CO_ASYNC_ALLOC)  # 定义 CO_ASYNC_ALLOC
endif()
if (CO_ASYNC_PERF)  # 如果启用了性能优化
    target_compile_definitions(co_async PUBLIC CO_ASYNC_PERF)  # 定义 CO_ASYNC_PERF
endif()
if (CO_ASYNC_EXCEPT)  # 如果启用了异常处理特性
    target_compile_definitions(co_async PUBLIC CO_ASYNC_EXCEPT)  # 定义 CO_ASYNC_EXCEPT
endif()
if (CO_ASYNC_ZLIB)  # 如果启用了 Zlib 支持
    target_compile_definitions(co_async PUBLIC CO_ASYNC_ZLIB)  # 定义 CO_ASYNC_ZLIB
    # target_link_libraries(co_async PUBLIC z)  # 链接 Zlib 库（此行被注释掉）
    find_package(ZLIB REQUIRED)  # 查找 Zlib 包，必需
    target_link_libraries(co_async PUBLIC ZLIB::ZLIB)  # 链接 Zlib 库
endif()
if (CO_ASYNC_STEAL)  # 如果启用了偷取特性
    target_compile_definitions(co_async PUBLIC CO_ASYNC_STEAL)  # 定义 CO_ASYNC_STEAL
endif()
if (CO_ASYNC_CUDA)  # 如果启用了 CUDA 支持
    target_compile_definitions(co_async PUBLIC CO_ASYNC_CUDA)  # 定义 CO_ASYNC_CUDA
    find_package(CUDA REQUIRED)  # 查找 CUDA 包，必需
    target_link_libraries(co_async PUBLIC CUDA::CUDA)  # 链接 CUDA 库
endif()
if (CO_ASYNC_DIRECT)  # 如果启用了直接特性
    target_compile_definitions(co_async PUBLIC CO_ASYNC_DIRECT)  # 定义 CO_ASYNC_DIRECT
endif()
if (CO_ASYNC_INVALFIX)  # 如果启用了无效修复特性
    target_compile_definitions(co_async PUBLIC CO_ASYNC_INVALFIX)  # 定义 CO_ASYNC_INVALFIX
endif()
if (CO_ASYNC_NATIVE)  # 如果启用了本地特性
    target_compile_definitions(co_async PUBLIC CO_ASYNC_NATIVE)  # 定义 CO_ASYNC_NATIVE
    target_compile_options(co_async PRIVATE -march=native)  # 添加本地架构优化选项
endif()

if (CMAKE_SYSTEM_NAME MATCHES "Linux")
    if (CO_ASYNC_FIND_LIBURING)
        include(FindPkgConfig)
        pkg_check_modules(LIBURING liburing)
        if (NOT LIBURING_FOUND)
            find_path(LIBURING_INCLUDE_DIR NAMES liburing.h)
            find_library(LIBURING_LIBRARY NAMES liburing.a)
            if (NOT LIBURING_INCLUDE_DIR OR NOT LIBURING_LIBRARY)
                message(FATAL_ERROR "liburing not found")
            endif()
            set(LIBURING_LIBRARIES ${LIBURING_LIBRARY})
            set(LIBURING_INCLUDE_DIRS ${LIBURING_INCLUDE_DIR})
        endif()
        target_link_libraries(co_async PUBLIC ${LIBURING_LIBRARIES})
        target_include_directories(co_async PUBLIC ${LIBURING_INCLUDE_DIRS})
    else()
        target_sources(co_async PRIVATE 3rd/liburing/liburing.cpp)
        target_include_directories(co_async PUBLIC 3rd/liburing/include)
    endif()
endif()
target_include_directories(co_async PUBLIC 3rd/hashlib/include)

if (CO_ASYNC_FIND_BEARSSL)
    find_package(BearSSL QUIET)
    if (TARGET BearSSL::bearssl)
        target_link_libraries(co_async PUBLIC BearSSL::bearssl)
    else()
        find_path(BEARSSL_INCLUDE_DIR NAMES bearssl.h)
        find_library(BEARSSL_LIBRARY NAMES libbearssl.so)
        if (NOT BEARSSL_INCLUDE_DIR OR NOT BEARSSL_LIBRARY)
            message(FATAL_ERROR "BearSSL not found")
        endif()
        target_link_libraries(co_async PUBLIC ${BEARSSL_LIBRARY})
        target_include_directories(co_async PUBLIC ${BEARSSL_INCLUDE_DIR})
    endif()
else()
    target_include_directories(co_async PUBLIC 3rd/bearssl/include)
endif()

find_package(Threads REQUIRED)
target_link_libraries(co_async PUBLIC Threads::Threads)

find_package(TBB REQUIRED COMPONENTS tbb REQUIRED)
target_link_libraries(co_async PUBLIC TBB::tbb)

if (CO_ASYNC_JEMALLOC)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(JEMALLOC jemalloc)
    pkg_search_module(JEMALLOC REQUIRED jemalloc)
    include_directories(${JEMALLOC_INCLUDE_DIRS})
    target_link_libraries(co_async PUBLIC ${JEMALLOC_LIBRARIES})
endif()

if (PROJECT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)# 如果项目源目录与 CMake 源目录相同
    file(GLOB example_sources examples/*.cpp)# 递归搜索示例源文件
    foreach(path ${example_sources})# 遍历每个示例源文件
        get_filename_component(name ${path} NAME_WE)# 获取文件名（不带扩展名）
        add_executable(${name} ${path})
        target_link_libraries(${name} PRIVATE co_async)# 链接 co_async 库
    endforeach()
endif()

add_custom_target(format# 定义一个格式化目标
    COMMAND clang-format -i ${implementation_sources} ${interface_sources}
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})# 设置工作目录为项目源目录
