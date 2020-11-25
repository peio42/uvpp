// Correctif: mode création du fichier - 0644 à la plce de 644
#include <cstring>
#include <uvpp/uv.hpp>

struct WriteRequest : uv::Pipe::WriteRq {
  uv::Buffer buf;
};

uv::Loop *loop;
uv::Pipe stdin_pipe;
uv::Pipe stdout_pipe;
uv::Pipe file_pipe;

void write_data(uv::Pipe &dest, size_t size, const uv::Buffer *buf) {
  auto req = new WriteRequest;

  req->buf.allocate(size);
  memcpy(req->buf.base, buf->base, size);
  dest.write(req, &req->buf, 1, [](uv::Pipe::WriteRq *req, int status) {
      static_cast<WriteRequest *>(req)->buf.cleanup();
      delete req;
    });
}

int main(int argc, char **argv) {
  loop = uv::Loop::getDefault();

  stdin_pipe.init(loop);
  stdin_pipe.open(0);

  stdout_pipe.init(loop);
  stdout_pipe.open(1);

  file_pipe.init(loop);
  uv::fs::Rq file_req;
  file_pipe.open(uv::fs::open(loop, &file_req, argv[1], O_CREAT | O_RDWR, 0644, nullptr));

  stdin_pipe.read_start([](uv::Pipe *pipe, size_t suggested_size, uv::Buffer *buf) {
      buf->allocate(suggested_size);
    }, [](uv::Pipe *pipe, ssize_t nread, const uv::Buffer *buf) {
      if (nread < 0) {
        if (nread == UV_EOF) {
          stdin_pipe.close();
          stdout_pipe.close();
          file_pipe.close();
        } else
          uv::_safe(nread);
      } else if (nread > 0) {
        write_data(stdout_pipe, nread, buf);
        write_data(file_pipe, nread, buf);
      }

      buf->cleanup();
    });

  loop->run();
  return 0;
}
