#pragma once
#include <new>

namespace exec {
  namespace detail {
    template <typename R>                       struct IsRvalueReference          { enum { value = 0 }; };
    template <typename R>                       struct IsRvalueReference<R&&>     { enum { value = 1 }; };
    template <typename R>                       struct IsReference                { enum { value = 0 }; };
    template <typename R>                       struct IsReference<R&>            { enum { value = 1 }; };
    template <typename R>                       struct RemoveReference            { using type = R;     };
    template <typename R>                       struct RemoveReference<R&>        { using type = R;     };
    template <typename R>                       struct RemoveReference<R&&>       { using type = R;     };
    template <bool B, typename T1, typename T2> struct Conditional                { using type = T1;    };
    template <typename T1, typename T2>         struct Conditional<false, T1, T2> { using type = T2;    };
    template <bool B, typename R = void>        struct EnableIf                   {                     };
    template <typename R>                       struct EnableIf<true, R>          { using type = R;     };
    template <typename R> 
    
    static inline typename RemoveReference<R>::type &&move(R&& value) { 
      return static_cast<typename RemoveReference<R>::type&&>(value); 
    } 
  } // namespace detail

  template <typename T>
  class Maybe {
    static_assert(!detail::IsRvalueReference<T>::value, "Maybe<T&&> not supported");
    using BareType    = typename detail::RemoveReference<T>::type;
    using StorageType = typename detail::Conditional<detail::IsReference<T>::value, BareType*, BareType>::type;

    bool has = false;
    struct StorageBox { StorageType obj; };
    union { StorageBox box; };

  public:
    Maybe() : has(false) {}
    
    Maybe(BareType&& obj) : has(true) {
      new (&box) StorageBox{ detail::move(obj) };
    }

    template <typename R = T, typename detail::EnableIf<!detail::IsReference<R>::value, int>::type = 0>
    Maybe(BareType const& obj) : has(true) {
      new (&box) StorageBox{ obj };
    }

    template <typename R = T, typename detail::EnableIf<detail::IsReference<R>::value, int>::type = 0>
    Maybe(BareType& obj) : has(true) {
      new (&box) StorageBox{ &obj };
    }
    
    Maybe(Maybe const &other) : has(other.has) {
      if (has) new (&box) StorageBox{ other.box.obj };
    }

    Maybe(Maybe&& other) : has(other.has) {
      if (has) new (&box) StorageBox{ detail::move(other.box.obj) };      
      other.reset();
    }

    Maybe &operator=(Maybe const &other) {
      if (this == &other) return *this;
      if (has) reset();
      has = other.has;
      if (has) new (&box) StorageBox{ other.box.obj };
      return *this;
    }

    Maybe &operator=(Maybe&& other) {
      if (this == &other) return *this;
      if (has) reset();
      has = other.has;
      if (has) new (&box) StorageBox{ detail::move(other.box.obj) };
      other.reset();
      return *this;
    }
    
    ~Maybe() { reset(); }

    operator bool() const             { return valid(); }
    bool valid() const                { return has; }
    BareType &value()                 { return value_impl(box.obj); }
    BareType const &value() const     { return value_impl(box.obj); }
    BareType &operator*()             { return value(); }
    BareType const &operator*() const { return value(); }

  private:
    BareType &value_impl(BareType&)                     { return   box.obj; }
    BareType &value_impl(BareType*)                     { return  *box.obj; }
    BareType const &value_impl(BareType const &) const  { return   box.obj; }
    BareType const &value_impl(BareType const *) const  { return  *box.obj; }

    void reset() {
      if (has) {
          box.~StorageBox();
          has = false;
      }
    }
  };

  template <>
  class Maybe<void> {
    bool has;
  public:
    Maybe(bool ran = false): has(ran) {}
    Maybe(Maybe&&) = default;
    Maybe(Maybe const &) = delete;
    operator bool() const { return valid(); }
    Maybe &operator=(Maybe const &) = delete;
    bool valid() const { return has; }
  };

  namespace detail {
    template <typename F> inline auto invoke(F&& f, uint32_t const dt, int) -> decltype(f(dt)) { return f(dt); }
    template <typename F> inline auto invoke(F&& f, uint32_t const, long)   -> decltype(f())   { return f();   }
    template <typename C> inline auto eval_(C&& c, uint32_t dt, int) -> decltype((bool)c(dt)) { return c(dt); }
    template <typename C> inline auto eval_(C&& c, uint32_t, long)   -> decltype((bool)c())   { return c();   }
    template <typename C> inline bool eval_(C&& c, uint32_t, ...) { return c; }
    template <typename C> inline bool eval(C&& c, uint32_t dt)    { return eval_(static_cast<C&&>(c), dt, 0); }

    template <typename Ret>
    struct RunAndReturn {
      template <typename F>
      inline static Maybe<Ret> run(F&& f, uint32_t dt) {
        return invoke(f, dt, 0);
      }
    };

    template <>
    struct RunAndReturn<void> {
      template <typename F>
      inline static Maybe<void> run(F&& f, uint32_t dt) {
        invoke(f, dt, 0);
        return {true};
      }
    };
  } // namespace detail

  template <int Tag, typename F>
  inline auto every_impl(uint32_t const interval, F&& f) -> Maybe<decltype(detail::invoke(f, 0, 0))> {
    static uint32_t last = 0;
    const uint32_t now = ::millis();
    const uint32_t dt  = now - last;
    if (dt >= interval) {
      last = now;
      return detail::RunAndReturn<decltype(detail::invoke(f, 0, 0))>::run(f, dt);
    }
    return {};
  }

  template <int Tag, typename C, typename F>
  inline auto every_if_impl(uint32_t const interval, C&& c, F&& f) -> Maybe<decltype(detail::invoke(f, 0, 0))> {
    static uint32_t last = 0;
    const uint32_t now = ::millis();
    const uint32_t dt  = now - last;
    if (dt >= interval && detail::eval(c, dt)) {
      last = now;
      return detail::RunAndReturn<decltype(detail::invoke(f, 0, 0))>::run(f, dt);
    }
    return {};
  }  
} // namespace exec

#define exec_every(interval, callable) every_impl<__COUNTER__>((interval), (callable))
#define exec_every_if(interval, condition, callable) every_if_impl<__COUNTER__>((interval), (condition), (callable))