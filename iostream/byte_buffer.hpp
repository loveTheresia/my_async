#pragma once

#include <std.hpp>
#include <generic/allocator.hpp>

#if ZH_ASYNC_ALLOC
    struct ByteBuffer
    {
    private:
        struct Deleter
        {
            std::size_t mSize;
            std::pmr::memory_resource *mResource;

            void operator()(char *p)noexcept{
                mResource->deallocate(p,mSize);
            }
        };

        std::unique_ptr<char[],Deleter> mData;

    public:
        ByteBuffer() noexcept = default;

        explicit ByteBuffer(std::size_t size,
                            std::pmr::polymorphic_allocator<> alloc = {})
                :mData(reinterpret_cast<char*>(alloc.resource()->allocate(size)),
                    Deleter{size,alloc.resource()}) {}

        void allocate(std::size_t size,
                      std::pmr::polymorphic_allocator<> alloc = {})
            {
                std::pmr::memory_resource *resource = alloc.resource();
                mData.reset(reinterpret_cast<char*>(resource->allocate(size)));
                mData.get_deleter() = {size,resource};
            }

        char *data()const noexcept{
            return mData.get();
        }

        std::size_t size()const noexcept{
            return mData.get_deleter().mSize;
        }

        explicit operator bool()const noexcept{
            return (bool)mData;
        }

        char &operator[](std::size_t index)const noexcept{
            return mData[index];
        }

        operator std::span<char>()const noexcept{
            return {data(),size()};
        }
    };

#else

struct ByteBuffer
{
private:
    char *mData;
    std::size_t mSize;

    void *pageAlignedAlloc(size_t n){
        return malloc(n);
    }

    void pageAlignedFree(void* p,size_t){
        free(p);
    }

public:
    ByteBuffer()noexcept: mData(nullptr),mSize(0) {}

    explicit ByteBuffer(std::size_t size)
            : mData(static_cast<char*>(pageAlignedAlloc(size))),
              mSize(size) {}

    ByteBuffer(ByteBuffer &&that)noexcept
            : mData(that.mData),
              mSize(that.mSize)
        {
            that.mData = nullptr;
            that.mSize = 0;
        }

    ByteBuffer& operator=(ByteBuffer &&that) noexcept{
        if(this != &that)
        {
            pageAlignedFree(mData,mSize);
            mData = that.mData;
            mSize = that.mSize;
            that.mData = nullptr;
            that.mSize = 0;
        }
        return *this;
    }

    ~ByteBuffer()noexcept{
        pageAlignedFree(mData,mSize);
    }

    void allocate(std::size_t size){
        mData = static_cast<char*>(pageAlignedAlloc(size));
        mSize = size;
    }

    char *data()const noexcept{
        return mData;
    }

    std::size_t size()const noexcept{
        return mSize;
    }

    explicit operator bool()const noexcept{
        return static_cast<bool>(mData);
    }

    char &operator[](std::size_t index)const noexcept{
        return mData[index];
    }

    operator std::span<char>()const noexcept{
        return {data(),size()};
    }
};

#endif