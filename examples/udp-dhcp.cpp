// Correctif: buffer.base[4] (XID) n'est rempli qu'avec un octet
#include <cstdlib>
#include <fmt/core.h>
#include "uv/uv.hpp"

uv::Loop *loop;
uv::Udp send_socket;
uv::Udp recv_socket;

uv::Buffer make_discover_msg() {
  uv::Buffer buffer;
  buffer.allocate(256);
  memset(buffer.base, 0, buffer.len);

  // BOOTREQUEST
  buffer.base[0] = 0x1;
  // HTYPE ethernet
  buffer.base[1] = 0x1;
  // HLEN
  buffer.base[2] = 0x6;
  // HOPS
  buffer.base[3] = 0x0;
  // XID 4 bytes
  buffer.base[4] = rand();
  buffer.base[5] = rand();
  buffer.base[6] = rand();
  buffer.base[7] = rand();
  // SECS
  buffer.base[8] = 0x0;
  // FLAGS
  buffer.base[10] = 0x80;
  // CIADDR 12-15 is all zeros
  // YIADDR 16-19 is all zeros
  // SIADDR 20-23 is all zeros
  // GIADDR 24-27 is all zeros
  // CHADDR 28-43 is the MAC address, use your own
  buffer.base[28] = 0xe4;
  buffer.base[29] = 0xce;
  buffer.base[30] = 0x8f;
  buffer.base[31] = 0x13;
  buffer.base[32] = 0xf6;
  buffer.base[33] = 0xd4;
  // SNAME 64 bytes zero
  // FILE 128 bytes zero
  // OPTIONS
  // - magic cookie
  buffer.base[236] = 99;
  buffer.base[237] = -126;  // was (unsigned) : 130;
  buffer.base[238] = 83;
  buffer.base[239] = 99;

  // DHCP Message type
  buffer.base[240] = 53;
  buffer.base[241] = 1;
  buffer.base[242] = 1; // DHCPDISCOVER

  // DHCP Parameter request list
  buffer.base[243] = 55;
  buffer.base[244] = 4;
  buffer.base[245] = 1;
  buffer.base[246] = 3;
  buffer.base[247] = 15;
  buffer.base[248] = 6;

  return buffer;
}

int main() {
  loop = uv::Loop::getDefault();

  try {
    recv_socket.init(loop);
    uv::IPv4 recv_addr("0.0.0.0", 1068);
    recv_socket.bind(&recv_addr, uv::Udp::Flags::ReuseAddr);
    recv_socket.recv_start([](uv::Udp *udp, size_t suggested_size, uv::Buffer *buf) {
        buf->allocate(suggested_size);
      }, [](uv::Udp *udp, ssize_t nread, const uv::Buffer *buf, const uv::SockAddr *addr, unsigned int flags) {
        if (nread < 0) {
          fmt::print(stderr, "Read error {}\n", uv::Error(nread).message());
          udp->close();
          free(buf->base);
          return ;
        }

        char sender[17];
        addr->name(sender, 16);
        fmt::print(stderr, "Recv from {}\n", sender);

        // ... DHCP specific code
        unsigned char ip[4] = { 0 };
        for (int i = 0; i < 4; i++)
          ip[i] = rand() & 0xff;
        fmt::print(stderr, "Offered IP {}.{}.{}.{}\n", ip[3], ip[2], ip[1], ip[0]);

        buf->cleanup();
        udp->recv_stop();
      });

    send_socket.init(loop);
    uv::IPv4 broadcast_addr("0.0.0.0", 0);
    send_socket.bind(&broadcast_addr);
    send_socket.set_broadcast(true);

    uv::Udp::SendRq send_req;
    uv::Buffer discover_msg = make_discover_msg();
    uv::IPv4 send_addr("255.255.255.255", 1067);
    send_socket.send(&send_req, &discover_msg, 1, &send_addr, [](uv::Udp::SendRq *req, int status) {
        if (status)
          fmt::print(stderr, "Send error {}\n", uv::Error(status).message());
      });

    loop->run();

  } catch (uv::Error &e) {
    fmt::print(stderr, "Error: {}\n", e.message());
    return 1;
  }

  return 0;
}
