#ifndef DAUGAARD_RING_BUFFER_f7dd9731f3e947a1a2b8a17ac2296854
#define DAUGAARD_RING_BUFFER_f7dd9731f3e947a1a2b8a17ac2296854

// Copyright 2018 Kaspar Daugaard. For educational purposes only.
// See http://daugaard.org/blog/writing-a-fast-and-versatile-spsc-ring-buffer

#include <algorithm>
#include <atomic>
#include <memory>
#include <new>
#include <stdexcept>

#ifndef DAUGAARD_RING_BUFFER_FORCE_INLINE
    #if defined(_MSC_VER)
        #define DAUGAARD_RING_BUFFER_FORCE_INLINE __forceinline
    #elif defined(__GNUC__)
        #define DAUGAARD_RING_BUFFER_FORCE_INLINE \
            inline __attribute__((always_inline))
    #else
        #define DAUGAARD_RING_BUFFER_FORCE_INLINE inline
    #endif
#endif

#if not defined(DAUGAARD_RING_BUFFER_CACHE_LINE_SIZE)
    #if defined(__cpp_lib_hardware_interference_size)
        #define DAUGAARD_RING_BUFFER_CACHE_LINE_SIZE \
            (std::hardware_destructive_interference_size)
    #else
        #error "cache line size is not defined"
    #endif
#endif

#include <cstdio>
#include <cstdlib>

namespace daugaard::rb::detail {
/**
 * Try to get a pointer to an active lifetime object.
 *
 * This doesn't really matter within the same process, because the lifetime was
 * started with placement new, but it doesn't cost anything in those cases
 * either.
 */
template <typename T>
[[nodiscard]]
T *
start_shm_lifetime(void * p) noexcept
{
#if defined(__cpp_lib_start_lifetime_as)
    // The right thing to do...
    return std::start_lifetime_as<T>(p);
#else
    #if (__cplusplus >= 201703L)
    // This is NOT right, but, it's something.  Assume its lifetime has been
    // implicitly started, which may or may not be true, but the compiler
    // may or may not be able to prove it (e.g., bytes mmap'd into the
    // address space).
    return std::launder(reinterpret_cast<T *>(p));
    #else
    // Even more not right than the previous one...
    return reinterpret_cast<T *>(p);
    #endif
#endif
}

template <typename T>
[[nodiscard]]
T *
start_shm_lifetime(void * p, [[maybe_unused]] std::size_t n) noexcept
{
#if defined(__cpp_lib_start_lifetime_as)
    return std::start_lifetime_as_array<T>(p, n);
#else
    return start_shm_lifetime(p);
#endif
}
} // namespace daugaard::rb::detail
  //
#if defined(__APPLE__)
    #include <sys/sysctl.h>

namespace daugaard::rb::detail {
inline std::size_t
get_runtime_cache_line_size()
{
    std::int64_t line_size = -1;
    std::size_t size = sizeof(line_size);
    sysctlbyname("hw.cachelinesize", &line_size, &size, nullptr, 0);
    return static_cast<std::size_t>(line_size);
}
} // namespace daugaard::rb::detail
#elif defined(__linux__)
    #include <unistd.h>

namespace daugaard::rb::detail {
inline std::size_t
get_runtime_cache_line_size()
{
    long line_size = sysconf(_SC_LEVEL1_DCACHE_LINESIZE);
    if (line_size <= 0) {
        return static_cast<std::size_t>(-1);
    }
    return static_cast<std::size_t>(line_size);
}
} // namespace daugaard::rb::detail
#else
namespace daugaard::rb::detail {
inline std::size_t
get_runtime_cache_line_size()
{
    return static_cast<std::size_t>(-1);
}
} // namespace daugaard::rb::detail
#endif


namespace daugaard::rb {

class RingBuffer
{
public:
    inline static constexpr int major = 1;
    inline static constexpr int minor = 0;
    inline static constexpr int patch = 0;

    // Allocate buffer space for writing.
    DAUGAARD_RING_BUFFER_FORCE_INLINE void * PrepareWrite(
        size_t size,
        size_t alignment);

    // Publish written data.
    DAUGAARD_RING_BUFFER_FORCE_INLINE void FinishWrite();

    // Write an element to the buffer.
    template <typename T>
    DAUGAARD_RING_BUFFER_FORCE_INLINE void Write(T const & value)
    {
        void * dest = PrepareWrite(sizeof(T), alignof(T));
        new (dest) T(value);
    }

    // Write an array of elements to the buffer.
    template <typename T>
    DAUGAARD_RING_BUFFER_FORCE_INLINE void WriteArray(
        T const * values,
        size_t count)
    {
        void * dest = PrepareWrite(sizeof(T) * count, alignof(T));
        for (size_t i = 0; i < count; i++) {
            new (static_cast<void *>(static_cast<T *>(dest) + i)) T(values[i]);
        }
    }

    // Get read pointer. Size and alignment should match written data.
    DAUGAARD_RING_BUFFER_FORCE_INLINE void * PrepareRead(
        size_t size,
        size_t alignment);

    // Finish and make buffer space available to writer.
    DAUGAARD_RING_BUFFER_FORCE_INLINE void FinishRead();

    // Read an element from the buffer.
    template <typename T>
    DAUGAARD_RING_BUFFER_FORCE_INLINE const T & Read()
    {
        void * src = PrepareRead(sizeof(T), alignof(T));
        return *detail::start_shm_lifetime<T>(src);
    }

    // Read an array of elements from the buffer.
    template <typename T>
    DAUGAARD_RING_BUFFER_FORCE_INLINE const T * ReadArray(size_t count)
    {
        void * src = PrepareRead(sizeof(T) * count, alignof(T));
        return detail::start_shm_lifetime<T>(src, count);
    }

    // Initialize. Buffer must have required alignment. Size must be a power of
    // two.
    void Initialize(void * buffer, size_t size)
    {
        if (detail::get_runtime_cache_line_size() !=
            DAUGAARD_RING_BUFFER_CACHE_LINE_SIZE)
        {
            throw std::runtime_error("wrong cache line size");
        }
        Reset();
        ReattachReader(buffer);
        ReattachWriter(buffer);
        m_Reader.size = m_Writer.size = m_Writer.end = size;
    }

    void ReattachReader(void * buffer)
    {
        m_Reader.buffer = static_cast<char *>(buffer);
    }

    void ReattachWriter(void * buffer)
    {
        m_Writer.buffer = static_cast<char *>(buffer);
    }

    void Reset()
    {
        m_Reader = m_Writer = LocalState();
        m_ReaderShared.pos = m_WriterShared.pos = 0;
    }

private:
    DAUGAARD_RING_BUFFER_FORCE_INLINE static size_t Align(
        size_t pos,
        [[maybe_unused]] size_t alignment)
    {
#ifdef DAUGAARD_RING_BUFFER_DO_NOT_ALIGN
        // If you disable this, then all bets are off for treating the objects
        // in the queue as implicit lifetime, because the standard requires
        // properly aligned objects in these cases.
        return pos;
#else
        return (pos + alignment - 1) & ~(alignment - 1);
#endif
    }

    DAUGAARD_RING_BUFFER_FORCE_INLINE void GetBufferSpaceToWriteTo(
        size_t & pos,
        size_t & end);
    DAUGAARD_RING_BUFFER_FORCE_INLINE void GetBufferSpaceToReadFrom(
        size_t & pos,
        size_t & end);

    // Writer and reader's local state.
    struct alignas(DAUGAARD_RING_BUFFER_CACHE_LINE_SIZE) LocalState
    {
        LocalState()
        : buffer(nullptr)
        , pos(0)
        , end(0)
        , base(0)
        , size(0)
        { }

        char * buffer;
        size_t pos;
        size_t end;
        size_t base;
        size_t size;
    };

    LocalState m_Writer;
    LocalState m_Reader;

    // Writer and reader's shared positions.
    struct alignas(DAUGAARD_RING_BUFFER_CACHE_LINE_SIZE) SharedState
    {
        std::atomic<size_t> pos;
    };

    SharedState m_WriterShared;
    SharedState m_ReaderShared;
};

void *
RingBuffer::
PrepareWrite(size_t size, size_t alignment)
{
    size_t pos = Align(m_Writer.pos, alignment);
    size_t end = pos + size;
    if (end > m_Writer.end) {
        GetBufferSpaceToWriteTo(pos, end);
    }
    m_Writer.pos = end;
    return m_Writer.buffer + pos;
}

void
RingBuffer::
FinishWrite()
{
    m_WriterShared.pos.store(
        m_Writer.base + m_Writer.pos,
        std::memory_order_release);
}

void *
RingBuffer::
PrepareRead(size_t size, size_t alignment)
{
    size_t pos = Align(m_Reader.pos, alignment);
    size_t end = pos + size;
    if (end > m_Reader.end) {
        GetBufferSpaceToReadFrom(pos, end);
    }
    m_Reader.pos = end;
    return m_Reader.buffer + pos;
}

void
RingBuffer::
FinishRead()
{
    m_ReaderShared.pos.store(
        m_Reader.base + m_Reader.pos,
        std::memory_order_release);
}

void
RingBuffer::
GetBufferSpaceToWriteTo(size_t & pos, size_t & end)
{
    if (end > m_Writer.size) {
        end -= pos;
        pos = 0;
        m_Writer.base += m_Writer.size;
    }
    for (;;) {
        size_t readerPos = m_ReaderShared.pos.load(std::memory_order_acquire);
        size_t available = readerPos - m_Writer.base + m_Writer.size;
        // Signed comparison (available can be negative)
        if (static_cast<ptrdiff_t>(available) >= static_cast<ptrdiff_t>(end)) {
            m_Writer.end = std::min(available, m_Writer.size);
            break;
        }
    }
}

void
RingBuffer::
GetBufferSpaceToReadFrom(size_t & pos, size_t & end)
{
    if (end > m_Reader.size) {
        end -= pos;
        pos = 0;
        m_Reader.base += m_Reader.size;
    }
    for (;;) {
        size_t writerPos = m_WriterShared.pos.load(std::memory_order_acquire);
        size_t available = writerPos - m_Reader.base;
        // Signed comparison (available can be negative)
        if (static_cast<ptrdiff_t>(available) >= static_cast<ptrdiff_t>(end)) {
            m_Reader.end = std::min(available, m_Reader.size);
            break;
        }
    }
}

} // namespace daugaard::rb

namespace daugaard {
using rb::RingBuffer;
} // namespace daugaard

#endif // DAUGAARD_RING_BUFFER_f7dd9731f3e947a1a2b8a17ac2296854
