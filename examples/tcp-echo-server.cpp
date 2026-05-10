#include <array>
#include <iostream>

#include "uvpp/uv.hpp"

namespace {

uvpp::buffer_view allocate(uvpp::tcp &, std::size_t suggested) {
  auto *data = new char[suggested];
  return uvpp::buffer_view{data, suggested};
}

}

int main() {
  uvpp::loop loop;
  uvpp::tcp server(loop);

  uvpp::ipv4 address{"0.0.0.0", 2345};
  server.bind(address);

  server.listen([&](uvpp::tcp &srv, uvpp::result status) {
    if (!status) {
      std::cerr << status.error_code().message() << '\n';
      return;
    }

    auto *client = new uvpp::tcp(loop);
    srv.accept(*client);

    client->read_start(allocate, [client](uvpp::tcp &stream, uvpp::read_result read) {
      auto storage = read.storage();

      if (read.eof()) {
        delete[] storage.data();
        stream.close([client](uvpp::tcp &) {
          delete client;
        });
        return;
      }

      if (!read.ok()) {
        delete[] storage.data();
        stream.close([client](uvpp::tcp &) {
          delete client;
        });
        return;
      }

      auto *request = new uvpp::write_request;
      auto bytes = read.bytes();

      stream.write(*request, bytes, [request, storage](uvpp::write_request &, uvpp::result status) {
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
