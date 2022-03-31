# 通用资源池（ease/identity-pool.h）

## 资源池接口定义，与堆构造
```cpp
template<typename T>
class IdentityPool0
{
public:
    // 获取一个对象；若池内没有空闲对象，则通过Constructor构造一个新对象；
    T* get();

    // 偿还一个对象；若池已满，则通过Destructor析构该对象；
    void put(T* obj);

    // 定义构造和析构函数的回调函数原型
    typedef Callback<T**> Constructor;
    typedef Callback<T*>  Destructor;

    // 在堆中动态分配内存并构造指定容量的资源池，可选设置构造、析构函数
    static IdentityPool0* new_identity_pool(uint32_t capacity);
    static IdentityPool0* new_identity_pool(uint32_t capacity, Constructor ctor, Destructor dtor);
    static void delete_identity_pool(IdentityPool0* p);

protected:
    // 不许直接构造或析构资源池
    IdentityPool0();
    ~IdentityPool0();
};

// 只是IdentityPool0中的相应函数的全局跳板，便于使用
template<typename T> inline
IdentityPool0<T>* new_identity_pool(uint32_t capacity)
{
    return IdentityPool0<T>::new_identity_pool(capacity);
}

// 只是IdentityPool0中的相应函数的全局跳板，便于使用
template<typename T> inline
IdentityPool0<T>* new_identity_pool(uint32_t capacity,
    typename IdentityPool0<T>::Constructor ctor,
    typename IdentityPool0<T>::Destructor dtor)
{
    return IdentityPool0<T>::new_identity_pool(capacity, ctor, dtor);
}

// 只是IdentityPool0中的相应函数的全局跳板，便于使用
template<typename T> inline
void delete_identity_pool(IdentityPool0<T>* p)
{
    IdentityPool0<T>::delete_identity_pool(p);
}
```

## 资源池的栈（静态）构造
在模板参数中设置资源池容量常数：
```cpp
template<typename T, uint32_t CAPACITY>
class IdentityPool : public IdentityPool0<T>
{
public:
    using typename IdentityPool0<T>::Constructor;
    using typename IdentityPool0<T>::Destructor;

    IdentityPool();
    IdentityPool(Constructor ctor, Destructor dtor);
};
```
