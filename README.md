![C/C++ CI](https://github.com/peio42/uvpp/workflows/C/C++%20CI/badge.svg) [![Quality Gate Status](https://sonarcloud.io/api/project_badges/measure?project=org.blutch%3Auvpp&metric=alert_status)](https://sonarcloud.io/dashboard?id=org.blutch%3Auvpp)

# uvpp
uvpp is a Modern C++ wrapper for the wonderful libuv library.

## Why uvpp
libuv has been developed with object oriented design. When you use the
library into your C project, its class hierarchy is clear.
But if you want to use it inside a C++ project, you will actually have
to deal with a big amount of reinterpret_cast everywhere.
uvw might be an option, but it's more an event library, based on libuv
than a simple wrapper, and the overhead isn't so negligible.

uvpp wants to be a simple wrapper over libuv, with limited overhead,
proposing a clean Modern C++ interface with exception management.

## Usage
uvpp is a header-only C++ library. You will need a C++17 compiler.
Just include "uvpp/uv.hpp".

    #include <uvpp/uv.hpp>

For everything else, you can stick to libuv engine for the
implementation. So you'll mainly need a Loop object, the Reactor, from
which the handles will be attached. A default loop is available.

    auto loop = uv::Loop::getDefault();

or you can create one, by just instanciate a new Loop object, for
example with

    uv::Loop loop();

Then create Handles object as needed, and you can directly call the
methods from the objects.
For example, an Idle is a simple handle that will call its callback
on each loop iteration. To instanciate one in a local variable, use

    uv::Idle idle(loop);

To allocate a new object instead of using a local variable, use

    auto idle = new uv::Idle(loop);

###### Memory management
_uvpp doesn't allocate any object for you. You can allocate them
yourself or use local variable, but it's your programmer's duty to
correctly manage memory. Don't hesitate to use smart-pointers if
you want._

###### Callbacks
You can use Modern C++ lambda function, but then be careful you can't
capture any variable, as the compiler will have to translate it into
a simple function pointer.

    idle.start([](uv::Idle *idle) {
      static int64_t counter = 0;

      counter++;
      if (counter >= 1000000)
        idle->close();
    });

Of course, you can also reference any function.

_When you need to pass a callback to a function, you usually have
two options. You can pass it as a function parameter, or you can ask
uvpp to take care or errors and raise an exception for you if needed.
This way, you don't need to manage with libuv status inside your
callbacks (but still need to manage exceptions in C++ way)_

This safe way require you to use template version of the functions.
Even if not every callback will be given a status error code, any
callback can be written in a template-style to keep consistency, as
a general rule.

To be able to use a lambda as a template parameter, you might have to
use a C++20 compiler. As of 2022, g++ still has bug, even compiling
with "std=c++20", as it considers the lambda has no linkage. But
clang manages very well.

    Loop *loop = Loop::getDefault();

    Tcp server(loop);

    try {
      IPv4 addr("0.0.0.0", 2345);
      server.bind(&addr);

      server.listen<[](Tcp *server) {
        // ... Some useful stuff
        //
        // No need to check status. If there is an error during the
        // listen, an exception should raise during the loop->run()
      }>();

      loop->run();
    } catch (uv::Error &e) {
      // ... An error occured
    }

###### Under development
_You can notice the sockaddr helper `IPv4`. The library has some of them.
Expect to see more in the future, as well as more C++ oriented types,
like `std::string` for example._

###### Differences with libuv
The same objects and Handles have been mirrored. But the first noticeable
difference, aside of object methods instead of global functions, is that
the handles keep their types inside the callbacks. No need to cast
anything.

Also, the `read` method (or `start_read`) has an additional callback, for
EoF event, in its template form:

    uv::fs::read<on_read, on_eof>(loop, &req, fd, &iov, 1, -1);

# Example

This is a simple TCP echo server using uvpp

    #include <fmt/core.h>

    #include "uvpp/uv.hpp"

    using namespace uv;

    int main(int ac, char* av[]) {
      Loop *loop = Loop::getDefault();

      Tcp server(loop);

      try {
        IPv4 addr("0.0.0.0", 2345);
        server.bind(&addr);

        server.listen<[](Tcp *server) {
          fmt::print("New connection\n");

          auto client = new Tcp(server->getLoop());
          server->accept(client);

          client->read_start<[](Tcp *client, size_t suggested_size, Buffer *buf) {
            buf->allocate(suggested_size);
          }, [](Tcp *client, ssize_t nread, const Buffer *buf) {
            auto req = new Tcp::WriteRq;
            auto buf2 = new Buffer(buf->base, nread);
            req->set(buf2);

            client->write<[](Tcp::WriteRq *req) {
              auto buf2 = req->get<Buffer>();
              delete buf2->base;
              delete buf2;
              delete req;
            }>(req, buf2, 1);
          }, [](Tcp *client, const Buffer *buf) {
            fmt::print("Disconnection\n");

            client->close();
            delete buf->base;
          }>();
        }>();

        loop->run();
      } catch (uv::Error &e) {
        fmt::print(stderr, "Error {}\n", e.message());
      }

      return 0;
    }

# Testing process

Install [Clang](https://clang.llvm.org/) compiler, [Google Test](https://github.com/google/googletest), [fmt](https://github.com/fmtlib/fmt) library and [libuv](https://github.com/libuv/libuv) version to test.

On Ubuntu:

```shell
sudo apt install clang libgtest-dev libfmt-dev libuv1-dev
```

And run from working directory:

```shell
make
```
