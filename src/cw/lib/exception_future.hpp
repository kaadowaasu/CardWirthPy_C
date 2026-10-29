#pragma once

#include "cw/lib/exception_origin.hpp"
#include <future>
#include <optional>
#include <type_traits>
#include <functional>

namespace cw::lib {

// std::promise は std::exception_ptr だけ
// を渡すが、MSVC の複製では元の例外の識別情報が失われる。共通の診断情
// 報を通常の結果として運び、ほかの遅延例外と同じ API で再送出する。
template<class T> struct FutureResult {
    std::optional<T> value;
    ExceptionPtr error;
};
template<> struct FutureResult<void> {
    ExceptionPtr error;
};

template<class T> class Future {
    std::future<FutureResult<T>> future_;
public:
    Future() noexcept=default;
    explicit Future(std::future<FutureResult<T>> future):future_(std::move(future)){}
    bool valid() const noexcept{return future_.valid();}
    void wait() const{future_.wait();}
    template<class Rep,class Period>
    std::future_status wait_for(const std::chrono::duration<Rep,Period>& duration) const {
        return future_.wait_for(duration);
    }
    template<class Clock,class Duration>
    std::future_status wait_until(const std::chrono::time_point<Clock,Duration>& time) const {
        return future_.wait_until(time);
    }
    T get() {
        auto result=future_.get();
        if(result.error)rethrow_exception(result.error);
        if constexpr(!std::is_void_v<T>)return std::move(*result.value);
    }
};

template<class T> class Promise {
    std::promise<FutureResult<T>> promise_;
public:
    Future<T> get_future(){return Future<T>(promise_.get_future());}
    template<class U=T> requires (!std::is_void_v<U>)
    void set_value(U&& value) {
        promise_.set_value(FutureResult<T>{std::optional<T>(std::forward<U>(value)),{}});
    }
    void set_value() requires std::is_void_v<T> {promise_.set_value({});}
    void set_exception(ExceptionPtr error) {
        FutureResult<T> result;
        result.error=std::move(error);
        promise_.set_value(std::move(result));
    }
};

template<class Function> auto async(std::launch policy,Function&& function) {
    using T=std::invoke_result_t<std::decay_t<Function>&>;
    return Future<T>(std::async(policy,[fn=std::forward<Function>(function)]() mutable {
        FutureResult<T> result;
        try {
            if constexpr(std::is_void_v<T>)std::invoke(fn);
            else result.value.emplace(std::invoke(fn));
        } catch (...) {result.error=capture_exception();}
        return result;
    }));
}

}
