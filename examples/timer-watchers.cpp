#include <chrono>
#include <iostream>

#include "uvpp/uv.hpp"

using namespace std::chrono_literals;

int main() {
  uv::loop loop;
  uv::raw::prepare prepare(loop);
  uv::raw::check check(loop);
  uv::raw::timer timer(loop);

  int prepare_count = 0;
  int check_count = 0;
  int ticks = 0;

  prepare.start([&](uv::raw::prepare &) {
    ++prepare_count;
  }).throw_if_error();

  check.start([&](uv::raw::check &) {
    ++check_count;
  }).throw_if_error();

  timer.start(100ms, 100ms, [&](uv::raw::timer &self) {
    ++ticks;
    std::cout << "tick " << ticks
              << " prepare=" << prepare_count
              << " check=" << check_count << '\n';

    if (ticks == 5) {
      self.stop().throw_if_error();
      prepare.stop().throw_if_error();
      check.stop().throw_if_error();

      self.close();
      prepare.close();
      check.close();
    }
  }).throw_if_error();

  loop.run();
  loop.close();
  return 0;
}
