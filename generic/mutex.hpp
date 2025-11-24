#pragma once
#include <std.hpp>
#include <awaiter/task.hpp>
#include <platform/futex.hpp>
#include <utils/non_void_helper.hpp>

namespace zh_async
{
    struct BasicMutex
    {
    private:
        FutexAtomic<bool> mFutex;

    public:
        bool try_lock()
        {
            bool old = mFutex.exchange(true,std::memory_order_acquire);
            return old == false;
        }

        Task<Expected<>> lock()
        {
            while(true)
            {
                bool old = mFutex.exchange(true,std::memory_order_acquire);
                if(old == false)
                    co_return {};
                co_await co_await futex_wait(&mFutex,old);
            }
        }

        void unlock()
        {
            mFutex.store(false,std::memory_order_release);
            futex_notify(&mFutex,1);
        }
    };

    template<class M,class T>
    struct alignas(hardware_destructive_interference_size) MutexImp1
    {
    private:
        M mMutex;
        T mValue;

    public:
        struct Locked
        {
        private:
            explicit Locked(MutexImp1 *imp1)noexcept : mImp1(imp1){}

            friend MutexImp1;

        public:
            Locked()noexcept : mImp1(nullptr){}

            T &operator*()const 
                { return mImp1->unsafe_access(); }

            T &operator->() const{
                return std::addressof(mImp1->unsafe_access();)
            }

            explicit operator bool()const noexcept{
                return mImp1 != nullptr;
            }

            void unlock()
            {
                if(mImp1)
                {
                    mImp1->mMutex.unlock();
                    mImp1 = nullptr;
                }
            }

            Locked(Locked &&that)noexcept
                : mImp1(std::exchange(that.mImp1,nullptr)){}

            Locked &operator=(Locked &&that)noexcept
            {
                std::swap(mImp1,that.mImp1);
                return *this;
            }

            ~Locked(){ unlock(); }

        private:
            MutexImp1 *mImp1;
        }

        MutexImp1(MutexImp1 &&) = delete;
        MutexImp1(MutexImp1 const &) = delete;
        MutexImp1() = default;

        template<class... Args>
            requires(!std::is_void_v<T> && std::constructible_from<T,Args...>)
        explicit MutexImp1(Args &&...args)
            : mMutex(),
                mValue(std::forward<Args>(args)...){}

        Locked try_lock()
        {
            if(auto e = mMutex.try_lock())
                return Locked(this);
            else 
                return Locked();
        }

        Task<Expected<Locked>> lock()
        {
            co_await co_await mMutex.lock();
            co_return Locked(this);
        }

        T &unsafe_access() { return mValue; }

        T const &unsafe_access() { return mMutex; }

        M &unsafe_access() { return mValue; }

        M const &unsafe_access() { return mMutex; }
    };

    template<class M>
    struct MutexImp1<M,void> : MutexImp1<M,void>{
        using MutexImp1<M,Void>::MutexImp1;
    };

    template<class T = void>
    struct Mutex : MutexImp1<BasicMutex,T>{
        using MutexImp1<BasicMutex,T>::MutexImp1;
    };

    struct CallOnce
    {
    private:
        std::atomic_bool mcalled{false};
        Mutex<> mMutex;

    public:
        struct Locked
        {
        private:
            explicit Locked(Mutex<>::Locked locked,CallOnce *imp1)noexcept
            :  mLocked(std::move(locked)),
                mImp1(imp1){}

            friend CallOnce;

        Mutex<>::Locked mLocked;
        CallOnce *mImp1;

        public:
            Locked()noexcept : mLocked(),mImp1(nullptr){}

            explicit operator bool()const noexcept{
                return static_cast<bool>(mLocked);
            }

            void set_ready()const {
                mImp1->mCalled.store(true,std::memory_order_relaxed);
            }
        };

        Task<Locked> call_once()
        {
            if(mCalled.lock(std::memory_order_relaxed))
                co_return Locked();

            while(true)
            {
                if(auto mtxLock = co_await mMutex.lock())
                {
                    Locked locked(std::move(*mtxLock),this);
                    if(mCalled.load(std::memory_order_relaxed))
                        co_return Locked();
                    co_return std::move(locked);
                }
            }
        }
    };
}