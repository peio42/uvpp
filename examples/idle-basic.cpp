// Correctif: idle->close() à la place de idle->stop()
//  => Sinon loop->close() renvoie EBUSY car il reste un handler actif

#include <fmt/core.h>
#include "uv/uv.hpp"

int main(int ac, char* av[]) {
  uv::Loop *loop = uv::Loop::getDefault();
  uv::Idle idle(loop);

  idle.start([](uv::Idle *idle) {
      static int64_t counter = 0;

      counter++;
      if (counter >= 1000000)
        idle->close();
    });

  // fmt::print("Idling..\n");
  loop->run();

  try {
    loop->close();
  } catch(uv::Error &e) {
    fmt::print("Error: {}\n", e.message());
  }
  return 0;
}
