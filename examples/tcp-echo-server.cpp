#include <fmt/core.h>

#include "uvpp/uv.hpp"

using namespace uv;

int main(int ac, char* av[]) {
  Loop *loop = Loop::getDefault();

  Tcp server(loop);

  IPv4 addr("0.0.0.0", 2345);
  server.bind(&addr);

  server.listen<[](Tcp *server) {
    fmt::print("New connection\n");

    auto client = new Tcp(server->getLoop());
    server->accept(client);

    client->read_start([](Tcp *client, size_t suggested_size, Buffer *buf) {
        buf->allocate(suggested_size);
      }, [](Tcp *client, ssize_t nread, const Buffer *buf) {
        if (nread > 0) {
          auto req = new Tcp::WriteRq;
          auto buf2 = new Buffer(buf->base, nread);
          req->set(buf2);

          client->write(req, buf2, 1, [](Tcp::WriteRq *req, int status) {
              _safe(status);

              auto buf2 = req->get<Buffer>();
              delete buf2->base;
              delete buf2;
              delete req;
            });

          return ;
        }
        if (nread < 0) {
          if (nread != UV_EOF)
            _safe(nread);

          fmt::print("Disconnection\n");

          client->close();
        }

        delete buf->base;
      });
  }>();

  loop->run();

  return 0;
}
