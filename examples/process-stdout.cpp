#include <cstddef>
#include <iostream>

#include "uvpp/uv.hpp"

namespace {

uv::buffer_view allocate(uv::pipe &, std::size_t suggested) {
  auto *data = new char[suggested];
  return uv::buffer_view{data, suggested};
}

}

int main() {
  uv::loop loop;
  uv::pipe stdout_pipe(loop);

  auto options = uv::process_options::make("/bin/sh")
    .args({"-c", "printf 'hello from child process\\n'"})
    .ignore_stdin()
    .pipe_stdout(stdout_pipe)
    .inherit_stderr();

  uv::process child(loop, options, [](uv::process &process, uv::process_exit exit) {
    std::cout << "child exited status=" << exit.status
              << " signal=" << exit.signal << '\n';
    process.close();
  });

  stdout_pipe.read_start(allocate, [](uv::pipe &stream, uv::read_result read) {
    auto storage = read.storage();

    if (read.eof()) {
      delete[] storage.data();
      stream.close();
      return;
    }

    if (!read) {
      std::cerr << read.status().error_code().message() << '\n';
      delete[] storage.data();
      stream.close();
      return;
    }

    auto bytes = read.bytes();
    std::cout.write(reinterpret_cast<const char *>(bytes.data()),
                    static_cast<std::streamsize>(bytes.size()));
    delete[] storage.data();
  });

  loop.run();
  loop.close();
  return 0;
}
