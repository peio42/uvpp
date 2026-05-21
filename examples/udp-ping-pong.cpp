#include <cstddef>
#include <cstring>
#include <iostream>
#include <string>
#include <string_view>

#include "uvpp/uv.hpp"

namespace {

uv::buffer_view allocate(uv::udp &, std::size_t suggested) {
  auto *data = new char[suggested];
  return uv::buffer_view{data, suggested};
}

struct send_context {
  uv::udp_send_request request;
  std::string payload;
  sockaddr_storage address{};

  uv::buffer_view view() noexcept {
    return uv::buffer_view{payload.data(), payload.size()};
  }

  const sockaddr *native_address() const noexcept {
    return reinterpret_cast<const sockaddr *>(&address);
  }
};

std::string_view packet_text(uv::udp_receive_result packet) {
  auto bytes = packet.bytes();
  return {
    reinterpret_cast<const char *>(bytes.data()),
    bytes.size()
  };
}

void copy_address(sockaddr_storage &storage, const sockaddr *address) {
  auto size = address->sa_family == AF_INET ? sizeof(sockaddr_in) : sizeof(sockaddr_in6);
  std::memcpy(&storage, address, size);
}

}

int main() {
  uv::loop loop;
  uv::udp server(loop);
  uv::udp client(loop);

  server.bind(uv::ipv4{"127.0.0.1", 0});
  auto server_address = server.sockname().to_v4();

  client.receive_start(allocate, [&](uv::udp &handle, uv::udp_receive_result packet) {
    auto storage = packet.storage();

    if (packet.empty_event()) {
      delete[] storage.data();
      return;
    }

    if (!packet) {
      std::cerr << packet.status().error_code().message() << '\n';
      delete[] storage.data();
      handle.close();
      return;
    }

    std::cout << "client received: " << packet_text(packet) << '\n';
    delete[] storage.data();

    handle.receive_stop();
    handle.close();
  });

  server.receive_start(allocate, [&](uv::udp &handle, uv::udp_receive_result packet) {
    auto storage = packet.storage();

    if (packet.empty_event()) {
      delete[] storage.data();
      return;
    }

    if (!packet) {
      std::cerr << packet.status().error_code().message() << '\n';
      delete[] storage.data();
      handle.close();
      return;
    }

    std::cout << "server received: " << packet_text(packet) << '\n';

    if (packet.address() != nullptr && packet_text(packet) == "ping") {
      auto *reply = new send_context;
      reply->payload = "pong";
      copy_address(reply->address, packet.address());

      auto view = reply->view();
      handle.send(reply->request, view, reply->native_address(),
        [&handle, reply](uv::udp_send_request &, uv::result status) {
          if (!status) {
            std::cerr << status.error_code().message() << '\n';
          }

          delete reply;
          handle.close();
        });
    }

    delete[] storage.data();
    handle.receive_stop();
  });

  auto *ping = new send_context;
  ping->payload = "ping";

  auto view = ping->view();
  client.send(ping->request, view, server_address,
    [ping](uv::udp_send_request &, uv::result status) {
      if (!status) {
        std::cerr << status.error_code().message() << '\n';
      }

      delete ping;
    });

  loop.run();
  loop.close();
  return 0;
}
