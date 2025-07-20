#ifndef DAUGAARD_RING_BUFFER_f7dd9731f3e947a1a2b8a17ac2296854
#define DAUGAARD_RING_BUFFER_f7dd9731f3e947a1a2b8a17ac2296854

// Copyright 2018 Kaspar Daugaard. For educational purposes only.
// See http://daugaard.org/blog/writing-a-fast-and-versatile-spsc-ring-buffer

// The code presented here is the same as on the website, with the following
// changes.
//
// 1. Reformatted using clang-format so I can keep changes easy to manage.
//
// 2. Macros changed to be prefixed with DAUGAARD_RING_BUFFER_
//
// 3. DAUGAARD_RING_BUFFER_CACHE_LINE_SIZE can be computed by the cmake
//    because std::hardware_destructive_interference_size is wrong on some
//    macOS/clang compilers.  The actual cache line is queried and checked
//    in the Initialize member function.  In addition, buffer size and
//    alignment are also checked in Initialize.
//
// 4. The RingBuffer class is inside the daugaard namespace.
//
// 5. major/minor/patch static constexpr values have been added.
//
// 6. Assert that alignments are a power of two and reads/writes greater
//    than the buffer size aren't attempted.
//
// 7. ReattachReader and ReattachWriter have been added to allow the reader
//    and writer to separately set the pointer to the queue memory.  This
//    allows setting the value when the queue memory is shared between
//    processes.
//
// 8. Turn the class into a template, parameterized on the atomic type,
//    because std::atomic is not an implicit lifetime type as of C++20.
//    Also, removed the default ctor on LocalState, as it did nothing
//    more than a zero-initilized construction would do, and having it
//    meant that the class was not trivially default constructible.
//    This is not absolutely necessary, but means that it can only
//    combine with other things that are trivially move/copy.

#include <algorithm>
#include <atomic>
#include <cassert>
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

#ifndef DAUGAARD_RING_BUFFER_CACHE_LINE_SIZE
    #if defined(__APPLE__) && defined(__aarch64__)
        #define DAUGAARD_RING_BUFFER_CACHE_LINE_SIZE 128
    #else
        #define DAUGAARD_RING_BUFFER_CACHE_LINE_SIZE 64
    #endif
#endif

#ifndef DAUGAARD_RING_BUFFER_NAMESPACE
    #define DAUGAARD_RING_BUFFER_NAMESPACE daugaard
#endif

#include <cstdio>
#include <cstdlib>

#if defined(__APPLE__)
    #include <sys/sysctl.h>

namespace DAUGAARD_RING_BUFFER_NAMESPACE::rb::detail {
inline std::size_t
get_runtime_cache_line_size()
{
    std::int64_t line_size = -1;
    std::size_t size = sizeof(line_size);
    sysctlbyname("hw.cachelinesize", &line_size, &size, nullptr, 0);
    return static_cast<std::size_t>(line_size);
}
} // namespace DAUGAARD_RING_BUFFER_NAMESPACE::rb::detail
#elif defined(__linux__)
    #include <unistd.h>

namespace DAUGAARD_RING_BUFFER_NAMESPACE::rb::detail {
inline std::size_t
get_runtime_cache_line_size()
{
    long line_size = sysconf(_SC_LEVEL1_DCACHE_LINESIZE);
    if (line_size <= 0) {
        return static_cast<std::size_t>(-1);
    }
    return static_cast<std::size_t>(line_size);
}
} // namespace DAUGAARD_RING_BUFFER_NAMESPACE::rb::detail
#else
namespace DAUGAARD_RING_BUFFER_NAMESPACE::rb::detail {
inline std::size_t
get_runtime_cache_line_size()
{
    return static_cast<std::size_t>(-1);
}
} // namespace DAUGAARD_RING_BUFFER_NAMESPACE::rb::detail
#endif


namespace DAUGAARD_RING_BUFFER_NAMESPACE::rb {

template <typename AtomicT>
class TRingBuffer
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
        if (reinterpret_cast<std::uintptr_t>(buffer) %
                DAUGAARD_RING_BUFFER_CACHE_LINE_SIZE !=
            0)
        {
            throw std::runtime_error("buffer is not aligned on cache line");
        }
        if (not is_power_of_two(size)) {
            throw std::runtime_error("size must be a power of two");
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
        m_ReaderShared.pos.store(0, std::memory_order_seq_cst);
        m_WriterShared.pos.store(0, std::memory_order_seq_cst);
    }

private:
    template <typename T>
    static constexpr bool is_power_of_two(T t)
    {
        return t > 0 && (t & (t - 1)) == 0;
    }

    DAUGAARD_RING_BUFFER_FORCE_INLINE static size_t Align(
        size_t pos,
        [[maybe_unused]] size_t alignment)
    {
#ifdef DAUGAARD_RING_BUFFER_DO_NOT_ALIGN
        return pos;
#else
        assert(is_power_of_two(alignment));
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
        AtomicT pos;
    };

    SharedState m_WriterShared;
    SharedState m_ReaderShared;
};

template <typename AtomicT>
void *
TRingBuffer<AtomicT>::
PrepareWrite(size_t size, size_t alignment)
{
    size_t pos = Align(m_Writer.pos, alignment);
    size_t end = pos + size;
    assert(end - m_Writer.pos <= m_Writer.size);
    if (end > m_Writer.end) {
        GetBufferSpaceToWriteTo(pos, end);
    }
    m_Writer.pos = end;
    return m_Writer.buffer + pos;
}

template <typename AtomicT>
void
TRingBuffer<AtomicT>::
FinishWrite()
{
    m_WriterShared.pos.store(
        m_Writer.base + m_Writer.pos,
        std::memory_order_release);
}

template <typename AtomicT>
void *
TRingBuffer<AtomicT>::
PrepareRead(size_t size, size_t alignment)
{
    size_t pos = Align(m_Reader.pos, alignment);
    size_t end = pos + size;
    assert(end - m_Reader.pos <= m_Reader.size);
    if (end > m_Reader.end) {
        GetBufferSpaceToReadFrom(pos, end);
    }
    m_Reader.pos = end;
    return m_Reader.buffer + pos;
}

template <typename AtomicT>
void
TRingBuffer<AtomicT>::
FinishRead()
{
    m_ReaderShared.pos.store(
        m_Reader.base + m_Reader.pos,
        std::memory_order_release);
}

template <typename AtomicT>
void
TRingBuffer<AtomicT>::
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

template <typename AtomicT>
void
TRingBuffer<AtomicT>::
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

struct RingBuffer
: TRingBuffer<std::atomic<size_t>>
{ };

} // namespace DAUGAARD_RING_BUFFER_NAMESPACE::rb

namespace DAUGAARD_RING_BUFFER_NAMESPACE {
using rb::RingBuffer;
using rb::TRingBuffer;
} // namespace DAUGAARD_RING_BUFFER_NAMESPACE

#endif // DAUGAARD_RING_BUFFER_f7dd9731f3e947a1a2b8a17ac2296854
