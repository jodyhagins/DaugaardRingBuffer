// Copyright 2018 Kaspar Daugaard. For educational purposes only.
// See http://daugaard.org/blog/writing-a-fast-and-versatile-spsc-ring-buffer

#include <algorithm>
#include <atomic>
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

#if defined(__APPLE__)
    #include <sys/sysctl.h>

namespace daugaard::detail {
inline std::size_t
get_runtime_cache_line_size()
{
    std::int64_t line_size = -1;
    std::size_t size = sizeof(line_size);
    sysctlbyname("hw.cachelinesize", &line_size, &size, nullptr, 0);
    return static_cast<std::size_t>(line_size);
}
} // namespace daugaard::detail
#elif defined(__linux__)
    #include <unistd.h>

namespace daugaard::detail {
inline std::size_t
get_runtime_cache_line_size()
{
    long line_size = sysconf(_SC_LEVEL1_DCACHE_LINESIZE);
    if (line_size <= 0) {
        return static_cast<std::size_t>(-1);
    }
    return static_cast<std::size_t>(line_size);
}
} // namespace daugaard::detail
#else
namespace daugaard::detail {
inline std::size_t
get_runtime_cache_line_size()
{
    return static_cast<std::size_t>(-1);
}
} // namespace daugaard::detail
#endif


namespace daugaard {

class RingBuffer
{
public:
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
            new (static_cast<T *>(dest) + i) T(values[i]);
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
        return *static_cast<T *>(src);
    }

    // Read an array of elements from the buffer.
    template <typename T>
    DAUGAARD_RING_BUFFER_FORCE_INLINE const T * ReadArray(size_t count)
    {
        void * src = PrepareRead(sizeof(T) * count, alignof(T));
        return static_cast<T *>(src);
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
        m_Reader.buffer = m_Writer.buffer = static_cast<char *>(buffer);
        m_Reader.size = m_Writer.size = m_Writer.end = size;
    }

    void Reset()
    {
        m_Reader = m_Writer = LocalState();
        m_ReaderShared.pos = m_WriterShared.pos = 0;
    }

private:
    DAUGAARD_RING_BUFFER_FORCE_INLINE static size_t Align(
        size_t pos,
        size_t alignment)
    {
#ifdef RINGBUFFER_DO_NOT_ALIGN
        alignment = 1;
#endif
        return (pos + alignment - 1) & ~(alignment - 1);
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

} // namespace daugaard
