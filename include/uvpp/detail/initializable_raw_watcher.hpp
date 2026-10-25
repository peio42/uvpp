#pragma once

#include "uvpp/detail/initializable_handle.hpp"
#include "uvpp/detail/raw_watcher.hpp"

namespace uv::detail {

  template<class Derived, class Native, class... Args>
  class initializable_raw_watcher_handle
    : public initializable_handle<Derived, Native, raw_watcher_handle<Derived, Native, Args...>> {};

}
