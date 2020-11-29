#include "gtest/gtest.h"
#include "uvpp/uv.hpp"

static int read_cb_called = 0;

uv::Tcp *server_ptr;


class TcpReadStopStartTest : public testing::Test {
protected:
  uv::Loop *loop = uv::Loop::getDefault();

  void SetUp() {
    read_cb_called = 0;
  }
};


static void do_write(uv::Tcp *stream, uv::Tcp::WriteCb cb) {
  static char data[] = "12345678";

  auto req = new uv::Tcp::WriteRq;
  uv::Buffer buf(data, 8);
  stream->write(req, &buf, 1, cb);
}

static void do_alloc(uv::Tcp *connection, size_t suggested_size, uv::Buffer *buf) {
  static char slab[65536];
  buf->init(slab, sizeof(slab));
}


TEST_F(TcpReadStopStartTest, tcp_read_stop_start) {
  /* Server */
  uv::IPv4 saddr("0.0.0.0", 2345);

  uv::Tcp server(loop);
  server_ptr = &server;
  server.bind(&saddr);
  server.listen([](uv::Tcp *server, int status) {
      ASSERT_EQ(status, 0);

      auto connection = new uv::Tcp(server->getLoop());
      server->accept(connection);
      connection->read_start(do_alloc, [](uv::Tcp *connection, ssize_t nread, const uv::Buffer *buf) {
          ASSERT_GT(nread, 0);

          do_write(connection, [](uv::Tcp::WriteRq *req, int status) {
              ASSERT_EQ(status, 0);
              delete req;
            });

          connection->read_stop();
          connection->read_start(do_alloc, [](uv::Tcp *connection, ssize_t nread, const uv::Buffer *buf) {
              ASSERT_LT(nread, 0);

              connection->close();
              server_ptr->close();

              read_cb_called++;
            });

          read_cb_called++;
        });
    });

  /* Client */
  uv::IPv4 caddr("0.0.0.0", 2345);

  uv::Tcp client(loop);
  uv::Tcp::ConnectRq req;
  client.connect(&req, &caddr, [](uv::Tcp::ConnectRq *req, int status) {
      ASSERT_EQ(status, 0);

      do_write(req->getHandle(), [](uv::Tcp::WriteRq *req, int status) {
          ASSERT_EQ(status, 0);

          req->getHandle()->close();
        });
    });

  loop->run();

  ASSERT_GE(read_cb_called, 2);
}
