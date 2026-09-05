# Tutorial

This tutorial presents uvpp as a C++20 library built on top of libuv. It does
not assume that you already know event loops, handles, requests, borrowed views,
or asynchronous lifetime rules.

The pages are meant to be read in order:

1. [Understanding the execution model](01-event-loop.md)
2. [Memory, owners, and lifetimes](02-lifetime-and-memory.md)
3. [Callbacks, results, and errors](03-callbacks-and-errors.md)
4. [Streams, reads, writes, and buffers](04-streams-and-buffers.md)
5. [Filesystem and next steps](05-filesystem-and-next-steps.md)

## What uvpp Keeps Visible

uvpp is a C++ wrapper around libuv, but it is not a layer that hides libuv's
model. Asynchronous operations still require the objects used by libuv to remain
alive for as long as libuv may call back into them. The tutorial therefore keeps
coming back to three questions in each example:

- which object really owns the memory?
- which operation is synchronous, and which operation continues after the
  function returns?
- which callback tells us that libuv is done with a handle, request, or buffer?

Those questions matter more than the syntax. Once they become familiar, the rest
of the uvpp API becomes much more predictable.

