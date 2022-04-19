<!--$$PHOTON_UNPUBLISHED_FILE$$-->
# callback机制（ease/callback.h）
This is a functor class that represents either a plain function of
`int (*)(void*, ...)`, or a member function of class U
`int (U::*)(...)`.

std::function<void(Ts...)> is not used, as it's less efficient.

The first parameter of type void* (in the case of plain function) or
the object pointer (in the case of member function) is stored within
the callback itself. Other parameters, if any, are provided when
invoking the callback.


```cpp
struct Callback_Base { };

template<typename...Ts>
struct Callback : public Callback_Base
{
    // default constructor that does nothing
    Callback() = default;

    // prototype in form of plain function, with 1st arg being void*
    using Func = int (*)(void*, Ts...)

    // prototype in form of plain function, with 1st arg being U*
    template<typename U> using UFunc  = int (*)(U*, Ts...);

    // prototype in form of member function (of class U)
    template<typename U> using UMFunc = int (U::*)(Ts...);

    // prototype in form of const member function (of class U)
    template<typename U> using UCMFunc = int (U::*)(Ts...) const;


    // construct with void* and Func
    Callback(void* obj, Func func);

    // construct with U* and a plain function with 1st arg being U*
    template<typename U> Callback(U* obj, UFunc<U> func);

    // construct with U* and U's member function
    template<typename U> Callback(U* obj, UMFunc<U> func);

    // construct with U* and U's const member function
    template<typename U> Callback(U* obj, UCMFunc<U> func);

    // construct with T* and U's member function (U must be base of T)
    template<typename T, typename U, ENABLE_IF_BASE_OF(U, T)>
    Callback(T* obj, UMFunc<U> func);

    // construct with T* and U's const member function (U must be base of T)
    template<typename T, typename U, ENABLE_IF_BASE_OF(U, T)>
    Callback(T* obj, UCMFunc<U> func);

    // construct from a functor U, which must NOT be a callback
    template<typename U, ENABLE_IF_NOT_CB(U)> Callback(U& obj);


    // the member function bind() editions of the constructors above
    void bind(void* obj, Func func);
    template<typename U> void bind(U* obj, UFunc<U> func);
    template<typename U> void bind(U* obj, UCMFunc<U> func);
    template<typename U> void bind(U* obj, UMFunc<U> func);
    template<typename T, typename U, ENABLE_IF_BASE_OF(U, T)>
    void bind(T* obj, UMFunc<U> func);
    template<typename T, typename U, ENABLE_IF_BASE_OF(U, T)>
    void bind(T* obj, UCMFunc<U> func);
    template<typename U, ENABLE_IF_NOT_CB(U)> void bind(U& obj);

    // bind() edition of copy assignment
    void bind(const Callback& rhs);

    // not allow to bind to a rvalue (temp) object
    template<typename U, ENABLE_IF_NOT_CB(U)> void bind(U&& obj) = delete;

    // unbind the callback
    void clear();

    // is it bound to some function?
    operator bool() const;

    // invoke the callback, with args, if any
    int operator()(const Ts&...args);
    int fire(const Ts&...args) const;

    // invoke the callback once, andl clear() it
    int fire_once(const Ts&...args);
};
```
