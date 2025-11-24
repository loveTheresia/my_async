#pragma once
#include <std.hpp>
#include <utils/non_void_helper.hpp>

namespace zh_async
{
    template<class T>
    struct uninitialized
    {
        union {  T mValue; };

#if  ZH_ASYNC_DEBUG
        bool mHasValue = false;
#endif
        uninitialized() noexcept {}

        uninitialized(uninitialized &&) = delete;

        ~uninitialized()
        {
#if ZH_ASYNC_DEBUG
        if (mHasValue) [[unlikely]] {
            throw std::logic_error("Uninitialized destroyed with value");
        }
#endif
        }

        T &ref() noexcept
        {
#if ZH_ASYNC_DEBUG
        if (!mHasValue) [[unlikely]] {
            throw std::logic_error(
                "Uninitialized::ref called in an unvalued slot");
        }
#endif
        return mValue;
        }

        void destory()
        {
#if ZH_ASYNC_DEBUG
        if (!mHasValue) [[unlikely]] {
            throw std::logic_error(
                "Uninitialized::destroyValue called in an unvalued slot");
        }
#endif
        mValue.~T();
#if ZH_ASYNC_DEBUG
        mHasValue = false;
#endif
        }

        T move()
        {
#if ZH_ASYNC_DEBUG
        if (!mHasValue) [[unlikely]] {
            throw std::logic_error(
                "Uninitialized::move called in an unvalued slot");
        }
#endif
        T ret(std::move(mValue));
        mValue.~T();
#if ZH_ASYNC_DEBUG
        mHasValue = false;
#endif
        return ret;
        }

        template<class... Ts>
            requires std::constructible_from<T,Ts...>
        void emplace(Ts &&...args)
        {
#if ZH_ASYNC_DEBUG
        if (mHasValue) [[unlikely]] {
            throw std::logic_error(
                "Uninitialized::emplace with value already exist");
        }
#endif
        std::construct_at(std::addressof(mValue), std::forward<Ts>(args)...);
#if ZH_ASYNC_DEBUG
        mHasValue = true;
#endif
        }
    };

    template<>
    struct uninitialized<void>
    {
        void ref() const noexcept {}

        void destory() {}

        Void move(){ return Void(); }

        void emplace(Void) {}

        void emplace() {}
    };

    template<>
    struct uninitialized<Void> : uninitialized<void> {};

    template<class T>
    struct uninitialized<T const> : uninitialized<T> {};

    template<class T>
    struct uninitialized<T &> : uninitialized<std::reference_wrapper<T>> 
    {
    private:
        using Base = uninitialized<std::reference_wrapper<T>>;

    public:
        T const &ref()const noexcept { return Base::ref().get(); }

        T &ref() noexcept { return Base::ref().get(); }

        T &move() { return Base::move().get(); }
    };

} // namespace zh_async
