# I/O向量表征与操作模块

This is a module for iovec , which features:
1. useful functions and operators;
* intuitive and easy-to-use interface;
* efficient design and implementation;
* fixed-capacity after construction;
* stack-allocation, as well as heap-allocation, for the iovec array itself;
* flexible buffer space memory management for the `iovec::iov_base` part;
* correct buffer space de-allocation, even in cases of:
    * modification to iov_bases and iov_lens;
    * inserting stack-allocated buffers


## iovector_view
represents the view of an existing iovec[], providing lots of operations
```cpp
struct iovector_view
{
    iovec* iov;
    int iovcnt;

    // default constructor
    constexpr iovector_view() : iov(0), iovcnt(0) { }

    // construct with iovec*, and # of elements
    explicit constexpr
    iovector_view(iovec* iov, int iovcnt) : iov(iov), iovcnt(iovcnt) { }

    // construct with an iovec[]
    template<int N> explicit constexpr
    iovector_view(iovec (&iov)[N]) : iov(iov), iovcnt(N) { }

    // assign by iovec*, and # of elements
    void assign(iovec* iov, int iovcnt);

    // assign by iovec[]
    template<int N>
    void assign(iovec (&iov)[N]);

    bool empty();       // is empty ?
    iovec* begin();     // get a pointer to the 1st element
    iovec& front();     // get a ref to the 1st element
    iovec* end();       // get a pointer to an conceptual "guard" element next to the last one
    iovec& back();      // get a ref to the last elemtn
    void pop_front();   // pop the 1st element
    void pop_back();    // pop the last element
    iovec& operator[](size_t i);    // get the ref to the i-th element
    size_t elements_count() const;  // get # of the element(s)
    const iovec* begin() const;     // const edition of begin()
    const iovec& front() const;     // const edition of front()
    const iovec* end() const;       // const edition of end()
    const iovec& back() const;      // const edition of back()
    const iovec& operator[](size_t i);  // const edition of operator []

    bool operator==(const iovector_view& rhs) const;

    // returns total bytes summed through iov[0..iovcnt)
    size_t size() const;

    // try to extract some bytes from back, to shrink size to `size`
    // return shrinked size, which may be equal to oringinal size
    size_t shrink_to(size_t size);

    // try to extract `bytes` bytes from the front
    // return # of bytes actually extracted
    size_t extract_front(size_t bytes);

    // try to extract `bytes` bytes from front elemnt (iov[0])
    // return the pointer if succeeded, or nullptr otherwise
    void* extract_front_continuous(size_t bytes);

    // try to `extract_front(bytes)` and copy the extracted bytes to `buf`
    // return # of bytes actually extracted
    size_t extract_front(size_t bytes, void* buf);

    // try to extract `bytes` bytes from front in the form of
    // iovector_view stored in `iov`, without copying data
    // return # of bytes actually extracted, or -1 if `iov` has not enough iovs[] space
    ssize_t extract_front(size_t bytes, iovector_view* iov);

    // try to extract `bytes` bytes from the back
    // return # of bytes actually extracted
    size_t extract_back(size_t bytes);

    // try to extract `bytes` bytes from the back elemnt (iov[n-1])
    // return the pointer if succeeded, or nullptr otherwise
    void* extract_back_continuous(size_t bytes);

    // try to `extract_back(bytes)` and copy the extracted bytes to `buf`
    // return # of bytes actually extracted
    size_t extract_back(size_t bytes, void* buf);

    // try to extract `bytes` bytes from the back in the form of
    // iovector_view stored in `iov`, without copying data
    // return # of bytes actually extracted, or -1 if `iov` has not enough iovs[] space
    ssize_t extract_back(size_t bytes, iovector_view* iov);

    // copy data to a buffer of size `size`,
    // return # of bytes actually copied
    size_t memcpy_to(void* buf, size_t size);

    // copy data from a buffer of size `size`,
    // return # of bytes actually copied
    size_t memcpy_from(void* buf, size_t size);
};
```

## memory allocation
```cpp
struct IOVAllocation
{
    struct RangeSize { int min, max;};

    // allocate a memory buffer of size within [size.min, size.max],
    // the more the better, returning the size actually allocated
    // or 0 indicating failrue
    typedef Callback<RangeSize, void**> Allocator;
    static int default_allocator(void*, RangeSize size, void** ptr);
    Allocator allocate {nullptr, &default_allocator};

    // de-allocate a memory buffer
    typedef Callback<void*> Deallocator;
    static int default_deallocator(void*, void* ptr);
    Deallocator deallocate {nullptr, &default_deallocator};

    // reset to default_allocator and default_deallocator
    void reset();
};
```

## iovector API
To support both stack (static) and heap (dynamic) allocation of iovector of
**arbitrary** capacity, the implementation is split into 2 parts: (1) a base class
`iovector` that provides everything *but* the storage field of iovec[] itself; and
(2) a templated derived class `IOVectorEntity` that provides a iovec[N], where N
is an argument of the template.

```cpp
class iovector
{
protected:
    uint16_t capacity;              // total capacity
    uint16_t iov_begin, iov_end;    // iovs[iov_begin, iov_end)
    uint16_t nbases;                // # of allocated pointers
    struct iovec iovs[0];           // no real storage, must be the *last* data field

    // not allowed to freely construct / destruct
    iovector(uint16_t capacity, uint16_t preserve);
    ~iovector();

public:
    size_t sum() const          // summed size of the elements
    struct iovec& front()       // get ref of the 1st element
    struct iovec& back()        // get ref of the last element
    struct iovec* iovec()       // get ptr of the 1st element
    uint16_t iovcnt() const     // get # of element(s)
    bool empty() const          // is empty?
    struct iovec* begin()       // get ptr of the 1st element
    struct iovec* end()         // get a pointer to an conceptual "guard" element next to the last one
    struct iovec& operator[] (int64_t i)    // get ref of i-th element

    // the const editions of above functions
    const struct iovec& front() const
    const struct iovec& back() const
    const struct iovec* iovec() const
    const struct iovec* begin() const
    const struct iovec* end() const
    const struct iovec& operator[] (int64_t i) const

    // get # of free (reserved) space before the front
    uint16_t front_free_iovcnt() const
    
    // get # of free (reserved) space after the back
    uint16_t back_free_iovcnt() const

    // set # of elements (by letting iov_end = iov_begin + nn)
    void resize(uint16_t nn)

    // push some stuff to the front, and
    // return # of bytes actually pushed
    size_t push_front(struct iovec iov)
    size_t push_front(void* buf, size_t size)

    // alloc an buffer with IOVAllocation, and push the buffer to the front;
    // note that the buffer may be constituted by multiple iovec elements;
    size_t push_front(size_t bytes)


    // push some stuff to the back, and
    // return # of bytes actually pushed
    size_t push_back(struct iovec iov)
    size_t push_back(void* buf, size_t size)

    // alloc an buffer with IOVAllocation, and push the buffer to the back;
    // note that the buffer may be constituted by multiple iovec elements;
    size_t push_back(size_t bytes)

    // pop an struct iovec element, and
    // return the # of bytes actually popped
    size_t pop_front()
    size_t pop_back()

    // doesn't dispose()
    void clear()

    // clear and dispose internally allocated buffers
    void clear_dispose()

    // try to extract some bytes from back, to shrink size to `size`
    // return shrinked size, which may be equal to oringinal size
    size_t shrink_to(size_t size)

    // resize the # of bytes, by either poping-back or pushing-back
    size_t truncate(size_t size)

    // try to extract `bytes` bytes from the front
    // return # of bytes actually extracted
    size_t extract_front(size_t bytes)

    // try to `extract_front(bytes)` and copy the extracted bytes to `buf`
    // return # of bytes actually extracted
    size_t extract_front(size_t bytes, void* buf)

    // try to extract `bytes` bytes from the front in the form of
    // iovector_view stored in `iov`, without copying data;
    // `iov` should be empty when calling;
    // return # of bytes actually extracted, or -1 if internal alloc failed
    ssize_t extract_front(size_t bytes, iovector_view* /*OUT*/ iov)

    // try to extract an object of type `T` from front, possibly copying
    template<typename T>
    T* extract_front();

    // try to extract `bytes` bytes from front elemnt (iov[0])
    // return the pointer if succeeded, or nullptr otherwise (alloc
    // failed, or not enough data, etc.)
    void* extract_front_continuous(size_t bytes)

    // try to extract `bytes` bytes from the back
    // return # of bytes actually extracted
    size_t extract_back(size_t bytes)

    // try to extract `bytes` bytes from back in the form of
    // iovector_view stored in `iov`, without copying data;
    // `iov` should be empty when calling;
    // return # of bytes actually extracted, or -1 if internal alloc failed
    size_t extract_back(size_t bytes, void* buf)

    // extract some `bytes` from the back without copying the data, in the
    // form of `iovec_array`, which is internally malloced, and will be freed
    // when *this destructs, or disposes.
    // return # of bytes actually extracted, may be < `bytes` if not enough in *this
    // return -1 if malloc failed
    ssize_t extract_back(size_t bytes, iovector_view* iov)

    // try to extract an object of type `T` from back, possibly copying
    template<typename T>
    T* extract_back()

    // `extract_back(bytes)` and return the starting address, guaranteeing it's a
    // continuous buffer, possiblily by memcpying to a internal allocated new buffer
    // return nullptr if not enough data in the iovector
    void* extract_back_continuous(size_t bytes)

    // copy data to a buffer of size `size`,
    // return # of bytes actually copied
    size_t memcpy_to(void* buf, size_t size)

    // copy data from a buffer of size `size`,
    // return # of bytes actually copied
    size_t memcpy_from(void* buf, size_t size)


    void operator = (const iovector& rhs) = delete;
    void operator = (iovector&& rhs)

    // allocate a buffer with the internal allocator (IOVAllocation)
    void* malloc(size_t size)

    iovector_view view() const
};
```
Then we can dynamically create / delete an iovector object with:
```cpp
// create an object of iovector, with total capacity of `capacity`,
// and preserve `preserve` elements in the front;
inline iovector* new_iovector(uint16_t capacity, uint16_t preserve);

// delete an iovector object that was created with new_iovector()
inline void delete_iovector(iovector* ptr);
```
For stack (static) allocation, we have:
```cpp
template<uint16_t CAPACITY, uint16_t DEFAULT_PRESERVE>
class IOVectorEntity : public iovector
{
public:
    struct iovec iovs[CAPACITY]; // same address with iovector::iovs
    struct IOVAllocation allocator;
    void* bases[CAPACITY];       // same address with IOVAllocation::bases

    enum { capacity = CAPACITY };
    enum { default_preserve = DEFAULT_PRESERVE };

    // default constructor
    explicit IOVectorEntity(uint16_t preserve = DEFAULT_PRESERVE);

    // copy construct from iovec* and iovcnt
    explicit IOVectorEntity(const struct iovec* iov,
        int iovcnt, uint16_t preserve = DEFAULT_PRESERVE);

    // default constructor with specified allocator
    explicit IOVectorEntity(IOVAllocation allocator,
        uint16_t preserve = DEFAULT_PRESERVE);

    // allow move construct and assign from IOVectorEntity or iovector
    IOVectorEntity(IOVectorEntity&& rhs);
    IOVectorEntity(iovector&& rhs);
    void operator = (IOVectorEntity&& rhs);
    void operator = (iovector&& rhs);

    // disallow copy construct and assign from IOVectorEntity or iovector
    IOVectorEntity(const IOVectorEntity& rhs) = delete;
    IOVectorEntity(const iovector& rhs) = delete;
    void operator=(const IOVectorEntity& rhs) = delete;
    void operator=(const iovector& rhs) = delete;
};
```
