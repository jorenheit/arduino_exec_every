#pragma once
#include <new>

namespace exec {
  namespace detail {
    template <typename T>                       struct IsRvalueReference           { enum { value = 0 }; };
    template <typename T>                       struct IsRvalueReference<T&&>      { enum { value = 1 }; };
    template <typename T>                       struct IsReference                 { enum { value = 0 }; };
    template <typename T>                       struct IsReference<T&>             { enum { value = 1 }; };
    template <typename T>                       struct RemoveReference_            { using type = T;     };
    template <typename T>                       struct RemoveReference_<T&>        { using type = T;     };
    template <typename T>                       struct RemoveReference_<T&&>       { using type = T;     };
    template <typename T>                       struct RemoveConst_                { using type = T;     };
    template <typename T>                       struct RemoveConst_<T const>       { using type = T;     };
    template <typename T>                       struct RemoveVolatile_             { using type = T;     };
    template <typename T>                       struct RemoveVolatile_<T volatile> { using type = T;     };
    template <bool B, typename T1, typename T2> struct Conditional_                { using type = T1;    };
    template <typename T1, typename T2>         struct Conditional_<false, T1, T2> { using type = T2;    };
    template <bool B, typename T = void>        struct EnableIf_                   {                     };
    template <typename T>                       struct EnableIf_<true, T>          { using type = T;     };
    template <typename T> struct DecayFunction_                                    { using type = T;                  };
    template <typename T, typename... Args> struct DecayFunction_<T(Args...)>      { using type = T(*)(Args...);      };
    template <typename T, typename... Args> struct DecayFunction_<T(Args..., ...)> { using type = T(*)(Args..., ...); };


    template <typename T> using RemoveReference = typename RemoveReference_<T>::type;
    template <typename T> using RemoveConst = typename RemoveConst_<T>::type;
    template <typename T> using RemoveVolatile = typename RemoveVolatile_<T>::type;
    template <typename T> using BareType = RemoveReference<RemoveConst<RemoveVolatile<T>>>;
    template <bool B, typename T = void> using EnableIf = typename EnableIf_<B, T>::type;
    template <bool B, typename T1, typename T2> using Conditional = typename Conditional_<B, T1, T2>::type;
    template <typename T> using DecayFunction = typename DecayFunction_<T>::type;
    
    template <typename T> T&& declval();

    template <typename T>
    static inline RemoveReference<T> &&move(T&& value) { 
      return static_cast<RemoveReference<T>&&>(value); 
    } 

    template <typename T>
    static inline constexpr T&& forward(RemoveReference<T>& t) {
      return static_cast<T&&>(t);
    }

    template <typename T>
    static inline constexpr T&& forward(RemoveReference<T>&& t) {
      static_assert(!IsReference<T>::value, "forward<T>(x): T is an lvalue reference");
      return static_cast<T&&>(t);
    }

    template <typename T>
    class IsPrintable {
      template <typename U> static char test(decltype(declval<Print&>().print(declval<BareType<U> const &>()))* = 0);
      template <typename>   static long test(...);
    public:
      static constexpr bool value = (sizeof(test<T>(0)) == sizeof(char));
    };

    template <typename Derived, bool CanPrint>
    struct MaybePrintable: Printable {
      virtual size_t printTo(Print &p) const override {
        Derived const *self = static_cast<Derived const *>(this);
        return self->valid() ? p.print(self->value()) : p.print("<empty>");
      }
    };

    template <typename Base> struct MaybePrintable<Base, false> {};

    class HandleBase {
      friend void reset(HandleBase*);
      virtual void reset() = 0;
    };
    inline void reset(HandleBase *h) { if (h) h->reset(); }
    struct MaybeFactory;
  } // namespace detail


  template <typename T>
  class Maybe: public detail::MaybePrintable<Maybe<T>, detail::IsPrintable<T>::value> {
    static_assert(!detail::IsRvalueReference<T>::value, "Maybe<T&&> not supported");
    using ValueType    = detail::RemoveReference<T>;
    using BareType     = detail::BareType<T>;
    using StorageType  = detail::Conditional<detail::IsReference<T>::value, ValueType*, ValueType>;
    
    template <typename R> friend detail::HandleBase *getHandle(Maybe<R>&);
    template <typename R> friend detail::HandleBase *getHandle(Maybe<R>&&);
    friend struct detail::MaybeFactory;

    bool has = false;
    detail::HandleBase* handle;
    struct StorageBox { StorageType obj; };
    union { StorageBox box; };

    Maybe(detail::HandleBase* handle) : has(false), handle(handle) {}
    
    Maybe(detail::HandleBase* handle, ValueType&& obj) : has(true), handle(handle) {
      new (&box) StorageBox{ detail::move(obj) };
    }

    template <typename R = T, detail::EnableIf<!detail::IsReference<R>::value, int> = 0>
    Maybe(detail::HandleBase* handle, ValueType const &obj) : has(true), handle(handle) {
      new (&box) StorageBox{ obj };
    }

    template <typename R = T, detail::EnableIf<detail::IsReference<R>::value, int> = 0>
    Maybe(detail::HandleBase* handle, ValueType& obj) : has(true), handle(handle) {
      new (&box) StorageBox{ &obj };
    }
    
  public:
    Maybe(Maybe const &other) : has(other.has), handle(other.handle) {
      if (has) new (&box) StorageBox{ other.box.obj };
    }

    Maybe(Maybe&& other) : has(other.has), handle(other.handle) {
      if (has) new (&box) StorageBox{ detail::move(other.box.obj) };      
      other.reset();
    }

    Maybe &operator=(Maybe const &other) {
      if (this == &other) return *this;
      if (has) reset();
      has = other.has;
      handle = other.handle;
      if (has) new (&box) StorageBox{ other.box.obj };
      return *this;
    }

    Maybe &operator=(Maybe&& other) {
      if (this == &other) return *this;
      if (has) reset();
      has = other.has;
      handle = other.handle;
      if (has) new (&box) StorageBox{ detail::move(other.box.obj) };
      other.reset();
      return *this;
    }
    
    ~Maybe() { reset(); }

    explicit operator bool() const      { return valid(); }
    bool valid() const                  { return has; }
    ValueType &value()                  { return value_impl(box.obj); }
    ValueType const &value() const      { return value_impl(box.obj); }
    ValueType &operator*()              { return value(); }
    ValueType const &operator*() const  { return value(); }
    ValueType *operator->()             { return &value(); }
    ValueType const *operator->() const { return &value(); }

    ValueType &force(uint32_t dt = 0);

  private:
    ValueType &value_impl(ValueType&)                     { return   box.obj; }
    ValueType &value_impl(ValueType*)                     { return  *box.obj; }
    ValueType const &value_impl(ValueType const &) const  { return   box.obj; }
    ValueType const &value_impl(ValueType const *) const  { return  *box.obj; }

    void reset() {
      if (has) {
          box.~StorageBox();
          has = false;
      }
    }
  };

  template <>
  class Maybe<void> {
    template <typename R> friend detail::HandleBase* getHandle(Maybe<R>&);
    template <typename R> friend detail::HandleBase* getHandle(Maybe<R>&&);
    friend struct detail::MaybeFactory;
    bool has;
    detail::HandleBase* handle;
    Maybe(detail::HandleBase* handle, bool ran = false): has(ran), handle(handle) {}
  public:
    Maybe(Maybe&&) = default;
    Maybe(Maybe const &) = default;
    Maybe &operator=(Maybe const &) = default;
    Maybe &operator=(Maybe&&) = default;
    bool valid() const { return has; }
    operator bool() const { return valid(); }
    void force(uint32_t dt = 0);
  };

  namespace detail {
    template <typename F> inline auto invoke(F&& f, uint32_t const dt, int) -> decltype(f(dt)) { return f(dt); }
    template <typename F> inline auto invoke(F&& f, uint32_t const, long)   -> decltype(f())   { return f();   }
    template <typename C> inline auto eval_(C&& c, uint32_t dt, int) -> decltype((bool)c(dt))  { return c(dt); }
    template <typename C> inline auto eval_(C&& c, uint32_t, long)   -> decltype((bool)c())    { return c();   }
    template <typename C> inline bool eval_(C&& c, uint32_t, ...) { return c; }
    template <typename C> inline bool eval(C&& c, uint32_t dt)    { return eval_(static_cast<C&&>(c), dt, 0); }

    struct MaybeFactory {
      template <typename T> static Maybe<T> empty(HandleBase* h) { return Maybe<T>{h}; }
      template <typename T> static Maybe<T> value(HandleBase* h, T&& v) { return Maybe<T>{h, forward<T>(v)}; }
      static Maybe<void> voidValue(HandleBase *h) { return Maybe<void>{h, true}; }
    };

    template <typename Ret>
    struct RunAndReturn {
      template <typename F>
      inline static Maybe<Ret> run(F&& f, uint32_t dt, HandleBase* handle) {
        return MaybeFactory::value<Ret>(handle, invoke(f, dt, 0));
      }
    };

    template <>
    struct RunAndReturn<void> {
      template <typename F>
      inline static Maybe<void> run(F&& f, uint32_t dt, HandleBase* handle) {
        invoke(f, dt, 0);
        return MaybeFactory::voidValue(handle);
      }
    };

    using MillisFunc = uint32_t(*)();

    template <typename T>
    class LocalHandle: public HandleBase {
      uint32_t last;
      MillisFunc millis;
    public:
      LocalHandle(MillisFunc millis): last(millis()), millis(millis) {}
      uint32_t dt(uint32_t now) const { return now - last; }
      void reset(uint32_t now) { last = now; }
      virtual void reset() override { reset(millis()); }
      virtual Maybe<T> exec(uint32_t) = 0;
    };

    
  } // namespace detail

  using Handle = detail::HandleBase*;
  template <typename T> inline Handle getHandle(Maybe<T> &&maybe) { return maybe.handle; }
  template <typename T> inline Handle getHandle(Maybe<T> &maybe)  { return maybe.handle; }
  inline void reset(Handle h) { detail::reset(h); }

  namespace detail {
    
    template <typename R> struct ForceReturn {
      template <typename T> static R get(Maybe<T> &maybe) { return maybe.value(); }
    };

    template <> struct ForceReturn<void> {
      static void get(Maybe<void> &) {}
    };

    template <typename R, typename T>
    inline R force(Maybe<T> &maybe, uint32_t dt) {
      if (maybe) return ForceReturn<R>::get(maybe);
      maybe = static_cast<LocalHandle<T>*>(getHandle(maybe))->exec(dt);
      return ForceReturn<R>::get(maybe);
    }
  }

  template <typename T> inline typename Maybe<T>::ValueType &Maybe<T>::force(uint32_t dt) { 
    return detail::force<typename Maybe<T>::ValueType &>(*this, dt); 
  }
  inline void Maybe<void>::force(uint32_t dt) { 
    detail::force<void>(*this, dt); 
  }

  template <int Tag, detail::MillisFunc millis, typename RunCondition, typename ThrottleCondition, typename F>
  inline auto every_if_throttled_impl(uint32_t const interval, 
                                      RunCondition &&runCondition, 
                                      ThrottleCondition &&throttleCondition, 
                                      F&& f) -> Maybe<decltype(detail::invoke(f, 0, 0))> {
    
    using Ret = decltype(detail::invoke(f, 0, 0));
    using Callback = detail::DecayFunction<detail::BareType<F>>;

    static struct LocalHandle: public detail::LocalHandle<Ret> {
      Callback f;
      LocalHandle(Callback f): detail::LocalHandle<Ret>(millis), f(detail::move(f)) {}
      virtual Maybe<Ret> exec(uint32_t dt) override {
        return detail::RunAndReturn<Ret>::run(f, dt, this);
      }
    } handle(Callback(detail::move(f)));

    uint32_t const now = millis();
    uint32_t const dt  = handle.dt(now);
    if (dt >= interval && detail::eval(throttleCondition, dt)) {
      handle.reset(now);
      if (detail::eval(runCondition, dt)) return handle.exec(dt);
    }
    return detail::MaybeFactory::empty<Ret>(&handle);
  }  

  // Run at every interval regardless of any conditions.
  template <int Tag, detail::MillisFunc millis, typename F>
  inline auto every_impl(uint32_t const interval, F&& f) -> Maybe<decltype(detail::invoke(f, 0, 0))> {
    return every_if_throttled_impl<Tag, millis>(interval, true, true, f);
  }
  
  // Run only when the interval expires and the condition is met at that moment in time. If the interval
  // expires and the condition is not met, the timer is reset and the condition is checked when it 
  // expires again.
  template <int Tag, detail::MillisFunc millis, typename C, typename F>
  inline auto every_if_impl(uint32_t const interval, C&& c, F&& f) -> Maybe<decltype(detail::invoke(f, 0, 0))> {
    return every_if_throttled_impl<Tag, millis>(interval, c, true, f);
  }

  // Run as soon as the interval has expired and the condition is met. If the condition is not met, 
  // the timer keeps running and the condition is checked each subsequent time until it evaluates 
  // to true.
  template <int Tag, detail::MillisFunc millis = ::millis, typename C, typename F>
  inline auto throttled_impl(uint32_t const interval, C&& c, F&& f) -> Maybe<decltype(detail::invoke(f, 0, 0))> {
    return every_if_throttled_impl<Tag, millis>(interval, true, c, f);
  }  

} // namespace exec


#ifndef __COUNTER__
#define __COUNTER__ __LINE__
#endif

#define exec_every_with(millisFunc, interval, ...) exec::every_impl<__COUNTER__, (millisFunc)>((interval), __VA_ARGS__)
#define exec_every_if_with(millisFunc, interval, condition, ...) exec::every_if_impl<__COUNTER__, (millisFunc)>((interval), (condition), __VA_ARGS__)
#define exec_throttled_with(millisFunc, interval, condition, ...) exec::throttled_impl<__COUNTER__, (millisFunc)>((interval), (condition), __VA_ARGS__)

#define exec_every(interval, ...) exec_every_with(::millis, (interval), __VA_ARGS__)
#define exec_every_if(interval, condition, ...) exec_every_if_with(::millis, (interval), (condition), __VA_ARGS__)
#define exec_throttled(interval, condition, ...) exec_throttled_with(::millis, (interval), (condition), __VA_ARGS__)