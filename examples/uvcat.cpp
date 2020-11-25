// Correctif: Ajout de iov.len = sizeof(buffer) dans le on_write
//   => Sinon les lectures ne se font pas plus qu'avec la plus petite taille de buffer

#include <iostream>
#include "uv/uv.hpp"

uv::fs::Rq open_req;
uv::fs::Rq read_req;
uv::fs::Rq write_req;

static char buffer[1024];
uv::Buffer iov(buffer, sizeof(buffer));


void on_read(uv::fs::Rq *req) {
  // on read

  if (req->result < 0) {
    std::cerr << "Read error: " << uv::Error(req->result).message() << std::endl;
  } else if (req->result == 0) {
    uv::fs::Rq close_req;
    uv::fs::close(uv::Loop::getDefault(), &close_req, open_req.result);   // synchronous
  } else if (req->result > 0) {
    // std::cout << "Blutch: " << req->result << std::endl;
    iov.len = req->result;
    uv::fs::write(uv::Loop::getDefault(), &write_req, 1, &iov, 1, -1, [](uv::fs::Rq *req) {
        // on write

        if (req->result < 0) {
          std::cerr << "Write error: " << uv::Error(req->result).message() << std::endl;
        } else {
          iov.len = sizeof(buffer);
          uv::fs::read(uv::Loop::getDefault(), &read_req, open_req.result, &iov, 1, -1, on_read);
        }
      });
  }
}

int main(int argc, char **argv) {
  uv::fs::open(uv::Loop::getDefault(), &open_req, argv[1], O_RDONLY, 0, [](uv::fs::Rq *req) {
      // on open

      if (req->result >= 0) {
        uv::fs::read(uv::Loop::getDefault(), &read_req, req->result, &iov, 1, -1, on_read);
      } else {
        std::cerr << "error opening file: " << uv::Error(req->result).message() << std::endl;
      }
    });

  uv::Loop::getDefault()->run();

  open_req.cleanup();
  read_req.cleanup();
  write_req.cleanup();

  return 0;
}
