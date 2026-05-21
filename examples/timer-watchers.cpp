#include <chrono>
#include <iostream>

#include "uvpp/uv.hpp"

using namespace std::chrono_literals;

int main() {
  uv::loop loop;
  uv::prepare prepare(loop);
  uv::check check(loop);
  uv::timer timer(loop);

  int prepare_count = 0;
  int check_count = 0;
  int ticks = 0;

  prepare.start([&](uv::prepare &) {
    ++prepare_count;
  });

  check.start([&](uv::check &) {
    ++check_count;
  });

  timer.start(100ms, 100ms, [&](uv::timer &self) {
    ++ticks;
    std::cout << "tick " << ticks
              << " prepare=" << prepare_count
              << " check=" << check_count << '\n';

    if (ticks == 5) {
      self.stop();
      prepare.stop();
      check.stop();

      self.close();
      prepare.close();
      check.close();
    }
  });

  loop.run();
  loop.close();
  return 0;
}
