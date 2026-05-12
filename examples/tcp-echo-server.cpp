#include <array>
#include <iostream>

#include "uvpp/uv.hpp"

namespace {

uv::buffer_view allocate(uv::tcp &, std::size_t suggested) {
  auto *data = new char[suggested];
  return uv::buffer_view{data, suggested};
}

}

int main() {
  uv::loop loop;
  uv::tcp server(loop);

  uv::ipv4 address{"0.0.0.0", 2345};
  server.bind(address);

  server.listen([&](uv::tcp &srv, uv::result status) {
    if (!status) {
      std::cerr << status.error_code().message() << '\n';
      return;
    }

    auto *client = new uv::tcp(loop);
    srv.accept(*client);

    client->read_start(allocate, [client](uv::tcp &stream, uv::read_result read) {
      auto storage = read.storage();

      if (read.eof()) {
        delete[] storage.data();
        stream.close([client](uv::tcp &) {
          delete client;
        });
        return;
      }

      if (!read.ok()) {
        delete[] storage.data();
        stream.close([client](uv::tcp &) {
          delete client;
        });
        return;
      }

      auto *request = new uv::write_request;
      auto bytes = read.bytes();

      stream.write(*request, bytes, [request, storage](uv::write_request &, uv::result status) {
        if (!status) {
          std::cerr << status.error_code().message() << '\n';
        }
        delete[] storage.data();
        delete request;
      });
    });
  });

  loop.run();
  return 0;
}
