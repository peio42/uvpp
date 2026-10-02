#include <array>
#include <iostream>
#include <memory>
#include <thread>

#include "uvpp/loop_posting.hpp"

int main() {
  uv::loop loop;
  uv::loop_posting posting(loop, 8, 2);
  auto endpoint = posting.endpoint();
  std::array<uv::status, 3> submitted;
  std::exception_ptr preparation_failure;
  std::thread producer([endpoint, &submitted, &preparation_failure] {
    try {
      for (int i = 0; i != 3; ++i) {
        submitted[i] = uv::ops::post(endpoint, [value = std::make_unique<int>(i)] {
          std::cout << "loop received " << *value << '\n';
        });
      }
    } catch (...) {
      preparation_failure = std::current_exception();
    }
  });
  producer.join();
  posting.request_close();
  loop.run(); // drains every accepted post and completes native close
  loop.close();
  if (preparation_failure) std::rethrow_exception(preparation_failure);
  for (const auto &result : submitted) result.value();
}
