#include <iostream>
#include "uvpp/uv.hpp"

uv::fs::Rq open_req;
uv::fs::Rq read_req;
uv::fs::Rq write_req;

static char buffer[1024];
uv::Buffer iov(buffer, sizeof(buffer));
auto loop = uv::Loop::getDefault();


void on_read(uv::fs::Rq *req);
void on_eof(uv::fs::Rq *req);

void on_read(uv::fs::Rq *req) {
  iov.len = req->result;
  uv::fs::write<[](uv::fs::Rq *req) {
    iov.len = sizeof(buffer);
    uv::fs::read<on_read, on_eof>(uv::Loop::getDefault(), &read_req, open_req.result, &iov, 1, -1);
  }>(uv::Loop::getDefault(), &write_req, 1, &iov, 1, -1);
}

void on_eof(uv::fs::Rq *req) {
  uv::fs::Rq close_req;
  uv::fs::close(uv::Loop::getDefault(), &close_req, open_req.result);
}

int main(int argc, char **argv) {
  uv::fs::open<[](uv::fs::Rq *req) {
    uv::fs::read<on_read, on_eof>(loop, &read_req, req->result, &iov, 1, -1);
  }>(loop, &open_req, argv[1], O_RDONLY, 0);

  loop->run();

  open_req.cleanup();
  read_req.cleanup();
  write_req.cleanup();

  return 0;
}
