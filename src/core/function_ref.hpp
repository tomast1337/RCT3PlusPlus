// SPDX-License-Identifier: GPL-3.0-or-later
//
// `std::function_ref` for standard libraries that do not ship it yet (libc++, which the Windows build uses).
// Include this instead of <functional> wherever a function takes one.
// shortcut: only what our code uses (a callable or function called through a non-owning reference, no const or
// noexcept signatures, no nontype_t); delete this file once libc++ has <functional>'s own.
#pragma once

#include <functional>

#ifndef __cpp_lib_function_ref
    #include <memory>
    #include <type_traits>
    #include <utility>

// NOLINTBEGIN(cert-dcl58-cpp): standing in for the missing standard type is the point.
namespace std
{
    template<typename Signature> class function_ref;

    template<typename R, typename... Args> class function_ref<R(Args...)>
    {
    public:
        template<typename F>
            requires(!is_same_v<remove_cvref_t<F>, function_ref> && is_invocable_r_v<R, F&, Args...>)
        // NOLINTNEXTLINE(google-explicit-constructor,bugprone-forwarding-reference-overload): as the standard's
        function_ref(F&& callable) noexcept
            // A function is kept as its address, an object by a pointer to it; the cast back below undoes this one.
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast,cppcoreguidelines-pro-type-const-cast)
            : target_(const_cast<void*>(reinterpret_cast<const void*>(addressof(callable))))
            , call_([](void* target, Args... args) -> R {
                // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
                return invoke(*reinterpret_cast<add_pointer_t<remove_reference_t<F>>>(target), std::forward<Args>(args)...);
            })
        {
        }

        R operator()(Args... args) const
        {
            return call_(target_, std::forward<Args>(args)...);
        }

    private:
        void* target_;
        R (*call_)(void*, Args...);
    };
} // namespace std
// NOLINTEND(cert-dcl58-cpp)
#endif
