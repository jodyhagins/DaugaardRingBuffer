
## Summary

This repo contains Kaspar Daugaard's RingBuffer as published on his blog 
(https://daugaard.org/blog/writing-a-fast-and-versatile-spsc-ring-buffer).

The original source code file, as copied from the blog, is in
`src/daugaard/original.hpp`.

The file `src/daugaard/ring_buffer.hpp` remains as close as possible to the
original, with some minor changes to address some issues as they come up
(like cache line size changes).

Other derivatives of the work will be in different files.

## License

The original code has the following comment.

```c++
// Copyright 2018 Kaspar Daugaard. For educational purposes only.
// See http://daugaard.org/blog/writing-a-fast-and-versatile-spsc-ring-buffer
```

I was unsure as to the license terms and what usage limitations may exist,
so I reached out to Kaspar and asked.  His response is below.

> Hi Jody,
> 
> Thanks for reaching out.
> 
> It was more of a liability waiver, and because I felt the API probably needed
> some more work. I am happy for you to treat it as MIT licensed, if that works
> for you, and I will look into releasing it properly at some point.
> 
> All the best
> Kaspar

The repo needed a license file, and so I provided the MIT license as per
Kaspar's email.

However, note his comment about the API.


## Using the Library

The library is header-only.

There are currently no tests, but I will add some over time.

Builds support cmake, and you should be able to simply grab the code and run cmake.

You can just copy the files or use FetchContent.

Here is how I do it.

```cmake
FetchContent_Declare(
    DaugaardRingBuffer
    GIT_REPOSITORY https://github.com/jodyhagins/DaugaardRingBuffer.git
    GIT_TAG main # or better, a specific tag/SHA
    SYSTEM
    )
FetchContent_MakeAvailable(DaugaardRingBuffer)

# Then, when referencing it
target_link_libraries(my_target PRIVATE daugaard::ring_buffer)
```

And then, in your C++ file:

```c++
#include <daugaard/ring_buffer.hpp>
```

### Advanced Options

#### DAUGAARD_RING_BUFFER_NAMESPACE

If defined, this will be used as the outer namespace.

If not defined, the outer namespace is `daugaard`.

#### DAUGAARD_RING_BUFFER_CACHE_LINE_SIZE

If defined, this value will be used as the cache line size.

If not defined, `std::hardware_destructive_interference_size` will be used,
if `__cpp_lib_hardware_interference_size` is defined.

Otherwise, a compiler error will be generated.

#### DAUGAARD_RING_BUFFER_DETERMINE_CACHE_LINE_SIZE

This option is default OFF on all platforms except macOS with Clang, which
has a bug that sets std::hardware_destructive_interference_size to the
wrong value on some hardware.  It is default ON for macOS/Clang.

When ON, and `DAUGAARD_RING_BUFFER_CACHE_LINE_SIZE` is not defined, the
cmake process will run a command to determine the cache line size on that
particular machine, and then set `DAUGAARD_RING_BUFFER_CACHE_LINE_SIZE`
to the resulting value.

Due to this known bug, there is a check in `RingBuffer::Initialize` that
will do a runtime check, and throws an exception if the cache line
determined at runtime is different from `DAUGAARD_RING_BUFFER_CACHE_LINE_SIZE`.

#### DAUGAARD_RING_BUFFER_FORCE_INLINE

This macro is auto-determined at compile time, but it can be provided on
the command line.

#### DAUGAARD_RING_BUFFER_DO_NOT_ALIGN

By default, all writes into the ring buffer are properly aligned based on
the type written.  If `DAUGAARD_RING_BUFFER_DO_NOT_ALIGN` is defined,
then items written into the ring buffer will not be aligned.


## Differences From The Original

The following are the major differences from the original source code.

    + Reformatted using clang-format so I can keep changes easy to manage.

    + Macros changed to be prefixed with DAUGAARD_RING_BUFFER_

    + DAUGAARD_RING_BUFFER_CACHE_LINE_SIZE can be computed by the cmake
      because std::hardware_destructive_interference_size is wrong on some
      macOS/clang compilers.  The actual cache line is queried and checked
      in the Initialize member function.  In addition, buffer size and
      alignment are also checked in Initialize.


    + The RingBuffer class is inside the daugaard namespace.

    + major/minor/patch static constexpr values have been added.

    + Assert that alignments are a power of two and reads/writes greater
      than the buffer size aren't attempted.

    + ReattachReader and ReattachWriter have been added to allow the reader
      and writer to separately set the pointer to the queue memory.  This
      allows setting the value when the queue memory is shared between
      processes.

