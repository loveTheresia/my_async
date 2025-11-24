
#include<std.hpp>
#include<generic/allocator.hpp>

namespace zh_async
{
    /*
        为 currentAllocator 初始化
        new_delete_resource()返回一个memory_resource对象，使用全局的 new 和 delete 来操作内存
        通过将 currentAllocator 初始化为 new_delete_resource() 确保了zh_async中的所有内存分配都使用默认的new和delete
        使用 thread_pool 意味着 currentAllocator 对于每个线程是独立的，这允许每个线程使用不同的内存分配策略
    */
    thread_local std::pmr::memory_resource *currentAllocator =
    std::pmr::new_delete_resource();

#if ZH_ASYNC_ALLOC
namespace
{
    inline struct DefaultResource : std::pmr::memory_resource
    {
        void *do_allocate(size_t size, size_t align) override {
        return currentAllocator->allocate(size, align);
    }

    void do_deallocate(void *p, size_t size, size_t align) override {
        return currentAllocator->deallocate(p, size, align);
    }

    bool do_is_equal(
        std::pmr::memory_resource const &other) const noexcept override {
        return currentAllocator->is_equal(other);
    }

    DefaultResource() noexcept {
        std::pmr::set_default_resource(this);
    }

    DefaultResource(DefaultResource &&) = delete;
    }defaultResource;
}
#endif
    
}