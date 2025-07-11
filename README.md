
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
If not specified, `CMAKE_BUILD_TYPE` will default to `Debug` and
`CMAKE_CXX_STANDARD` will default to `20`.

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
