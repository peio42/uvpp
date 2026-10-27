#include <uvpp/uv.hpp>

int main() {
  uv::loop loop;
  loop.run();
  loop.close();
  return 0;
}
