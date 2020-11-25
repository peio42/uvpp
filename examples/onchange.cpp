#include <stdlib.h>
#include <iostream>
#include <uv/uv.hpp>

char *command;
uv::Loop *loop;

int main(int argc, char *argv[]) {
  if (argc <= 2) {
    std::cerr << "Usage: " << argv[0] << " <command> <file1> [file2...]" << std::endl;
    return 1;
  }

  loop = uv::Loop::getDefault();
  command = argv[1];

  while (argc-- > 2) {
    std::cerr << "Adding watch on " << argv[argc] << std::endl;
    auto fs_event = new uv::FsEvent(loop);
    fs_event->start<[](uv::FsEvent *fs_event, const char *filename, uv::FsEvent::Event events) {
      char path[1024];
      size_t size = 1023;
      fs_event->getpath(path, &size);
      path[size] = '\0';

      std::cerr << "Change detected in " << path << "\n  " << (filename ?: "");
      if (events % uv::FsEvent::Event::Rename) {
        std::cerr << " renamed";
      }
      if (events % uv::FsEvent::Event::Change) {
        std::cerr << " changed";
      }
      std::cerr << std::endl;

      system(command);
    }>(argv[argc], uv::FsEvent::EventFlags::Recursive);
  }

  loop->run();

  return 0;
}
