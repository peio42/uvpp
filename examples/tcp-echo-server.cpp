#include <array>
#include <iostream>

#include "uvpp/uv.hpp"

namespace {

uv::buffer_view allocate(uv::raw::tcp &, std::size_t suggested) {
  auto *data = new char[suggested];
  return uv::buffer_view{data, suggested};
}

}

int main() {
  uv::loop loop;
  uv::raw::tcp server(loop);

  uv::ipv4 address{"0.0.0.0", 2345};
  server.bind(address).throw_if_error();

  server.listen([&](uv::raw::tcp &srv, uv::status status) {
    if (!status) {
      std::cerr << status.error().message() << '\n';
      return;
    }

    auto *client = new uv::raw::tcp(loop);
    srv.accept(*client).throw_if_error();

    client->read_start(allocate, [client](uv::raw::tcp &stream, uv::read_result read) {
      auto storage = read.storage();

      if (read.eof()) {
        delete[] storage.data();
        stream.close([client](uv::raw::tcp &) {
          delete client;
        });
        return;
      }

      if (!read.ok()) {
        delete[] storage.data();
        stream.close([client](uv::raw::tcp &) {
          delete client;
        });
        return;
      }

      auto *request = new uv::write_request;
      auto bytes = read.bytes();

      stream.write(*request, bytes, [request, storage](uv::write_request &, uv::status status) {
        if (!status) {
          std::cerr << status.error().message() << '\n';
        }
        delete[] storage.data();
        delete request;
      });
    }).throw_if_error();
  }).throw_if_error();

  loop.run();
  return 0;
}
