#include "gtest/gtest.h"
#include "uvpp/uv.hpp"


#ifdef _WIN32
# define BAD_PIPENAME "bad-pipe"
#else
# define BAD_PIPENAME "/path/to/unix/socket/that/really/should/not/be/there"
#endif


static int close_cb_called;
static int connect_cb_called;

uv::Loop *loop = uv::Loop::getDefault();

static uv::Prepare prepare(loop);
static uv::Pipe gpipe(loop);
static uv::Pipe::ConnectRq connectRq;


class PipeConnectPrepareTest : public testing::Test {
protected:
  void SetUp() {
    close_cb_called = 0;
    connect_cb_called = 0;
  }
};

TEST_F(PipeConnectPrepareTest, BadPipeName) {
  prepare.start([](uv::Prepare *_prepare) {
    ASSERT_EQ(&prepare, _prepare);

    gpipe.connect(&connectRq, BAD_PIPENAME, [](uv::Pipe::ConnectRq *req, int status) {
      ASSERT_EQ(UV_ENOENT, status);

      uv::Pipe *_pipe = req->getHandle();
      ASSERT_EQ(&gpipe, _pipe);

      connect_cb_called++;

      prepare.close([](uv::Prepare *_prepare) {
        ASSERT_EQ(&prepare, _prepare);
        close_cb_called++;
      });
      gpipe.close([](uv::Pipe *_pipe) {
        ASSERT_EQ(&gpipe, _pipe);
        close_cb_called++;
      });
    });
  });

  loop->run();

  ASSERT_EQ(2, close_cb_called);
  ASSERT_EQ(1, connect_cb_called);
}