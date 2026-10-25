#include <array>
#include <chrono>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>

#include "platform-support.hpp"
#include "gtest/gtest.h"
#include "uvpp/handles/pipe.hpp"
#include "uvpp/handles/tcp.hpp"

namespace {
using pipe = uv::raw::pipe;
struct completion {
  int calls = 0;
  uv::status outcome;
};
template<class Request>
void record(Request &request, uv::status status) {
  EXPECT_FALSE(request.pending());
  auto &state = *request.template user_data<completion>();
  ++state.calls;
  state.outcome = status;
}
void closed(pipe &self) { ++*self.user_data<int>(); }

class RawPipe : public ::testing::Test {
protected:
  uv::loop loop;
  uv::loop export_loop;
  pipe server, client, peer;
  uv::raw::tcp exported, received;
  uv::raw::connect_request connecting;
  uv::raw::write_request writing;
  std::string path = uvpp::test::pipe_path("raw-migration");
  std::array<char, 32> storage{};
  bool accepted = false, connected = false;

  RawPipe() { std::error_code ignored; std::filesystem::remove(path, ignored); }
  ~RawPipe() override {
    for (auto *handle : {&server, &client, &peer}) handle->close();
    received.close();
    loop.run(); // Finish pipe requests before releasing any exported handle.
    exported.close();
    loop.run();
    export_loop.run();
    EXPECT_FALSE(connecting.pending());
    EXPECT_FALSE(writing.pending());
    EXPECT_NO_THROW(loop.close());
    EXPECT_NO_THROW(export_loop.close());
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
  }
  template<class Predicate>
  void until(Predicate done) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds{5};
    while (!done() && std::chrono::steady_clock::now() < end) {
      loop.run(uv::run_mode::nowait);
      std::this_thread::yield();
    }
    EXPECT_TRUE(done());
  }
  void listen(bool ipc) {
    server.init(loop).throw_if_error();
    client.init(loop, ipc).throw_if_error();
    peer.init(loop, ipc).throw_if_error();
    server.bind(path).throw_if_error();
    server.listen([this](pipe &self, uv::status status) {
      EXPECT_TRUE(status);
      EXPECT_TRUE(self.accept(peer));
      accepted = true;
    }).throw_if_error();
  }
  void pair(bool ipc = false) {
    listen(ipc);
    client.connect(connecting, path, [this](uv::raw::connect_request &request, uv::status status) {
      EXPECT_TRUE(status);
      EXPECT_FALSE(request.pending());
      connected = true;
    }).throw_if_error();
    until([&] { return accepted && connected; });
  }
  auto allocator() { return [this](pipe&, std::size_t) { return uv::buffer_view{storage.data(), storage.size()}; }; }
};

TEST_F(RawPipe, defaultStorageGuardsOperationsAndPreservesTypedObservations) {
  static_assert(std::is_nothrow_default_constructible_v<pipe>);
  static_assert(!std::is_copy_constructible_v<pipe>);
  static_assert(!std::is_move_constructible_v<pipe>);
  static_assert(!std::is_convertible_v<pipe&, uv_pipe_t*>);
  static_assert(!std::is_convertible_v<pipe&, uv::handle_view>);
  static_assert(std::is_same_v<decltype(client.sockname()), uv::result<std::string>>);
  static_assert(std::is_same_v<decltype(client.peername()), uv::result<std::string>>);
  static_assert(std::is_same_v<decltype(client.bind(path)), uv::status>);
  const auto badf = uv::make_error_code(UV_EBADF);
  EXPECT_FALSE(client.initialized());
  EXPECT_FALSE(client.active()); EXPECT_FALSE(client.closing());
  EXPECT_FALSE(client.has_ref());
  client.ref(); client.unref(); client.close();
  EXPECT_EQ(client.bind(path).error(), badf);
  EXPECT_EQ(client.sockname().error(), badf);
  EXPECT_EQ(client.peername().error(), badf);
  EXPECT_EQ(client.open(-1).error(), badf);
  EXPECT_EQ(client.chmod(uv::pipe_chmod_flag::readable).error(), badf);
  EXPECT_EQ(client.pending_instances(1).error(), badf);
  EXPECT_EQ(client.pending_count(), 0);
  EXPECT_EQ(client.pending_type(), uv::handle_type::unknown);
  EXPECT_EQ(client.connect(connecting, path, record<uv::raw::connect_request>).error(), badf);
  EXPECT_EQ(client.connect_static<record<uv::raw::connect_request>>(connecting, path).error(), badf);
  EXPECT_EQ(client.write_with_handle(writing, uv::buffer_view{storage.data(), 1}, exported,
                                     record<uv::raw::write_request>).error(), badf);
  EXPECT_EQ(client.write_with_handle_static<record<uv::raw::write_request>>(writing, uv::buffer_view{}, exported).error(), badf);
  EXPECT_EQ(client.write_with_handle_now(uv::buffer_view{}, exported).error_code(), badf);
  EXPECT_EQ(client.fileno().error(), badf);
}

TEST_F(RawPipe, explicitInitAndThrowingConstructionShareNativeLifetime) {
  int marker = 42;
  client.user_data(marker);
  auto *address = client.native();
  EXPECT_TRUE(client.init(loop.view(), true));
  EXPECT_TRUE(client.initialized());
  EXPECT_EQ(client.native()->ipc, 1);
  EXPECT_EQ(client.native()->loop, loop.native());
  EXPECT_EQ(client.user_data<int>(), &marker);
  EXPECT_EQ(client.native(), address);
  EXPECT_EQ(client.init(loop, false).error(), uv::make_error_code(UV_EALREADY));
  EXPECT_EQ(client.native()->ipc, 1);
  EXPECT_EQ(client.user_data<int>(), &marker);
  client.close_static<closed>();
  EXPECT_EQ(client.init(loop).error(), uv::make_error_code(UV_EALREADY));
  loop.run();
  EXPECT_EQ(marker, 43);
  EXPECT_TRUE(client.initialized());
  EXPECT_EQ(client.init(loop).error(), uv::make_error_code(UV_EALREADY));
  EXPECT_EQ(client.bind(path).error(), uv::make_error_code(UV_EBADF));
  EXPECT_EQ(client.sockname().error(), uv::make_error_code(UV_EBADF));
  EXPECT_EQ(client.pending_instances(1).error(), uv::make_error_code(UV_EBADF));
  EXPECT_EQ(client.connect_static<record<uv::raw::connect_request>>(connecting, path).error(), uv::make_error_code(UV_EBADF));
  client.close_static<closed>();
  EXPECT_EQ(marker, 43);
  pipe constructed{loop.view()};
  EXPECT_TRUE(constructed.initialized());
  EXPECT_EQ(constructed.native()->ipc, 0);
  bool finished = false;
  constructed.close([&](pipe&) { finished = true; });
  until([&] { return finished; });
}

TEST_F(RawPipe, bindAndNamesKeepNativeErrorsAndStringPayload) {
  ASSERT_TRUE(server.init(loop));
  EXPECT_EQ(server.bind({}).error(), uv::make_error_code(UV_EINVAL));
  EXPECT_FALSE(server.sockname());
  EXPECT_FALSE(server.peername());
  EXPECT_EQ(server.pending_instances(0).error(), uv::make_error_code(UV_EINVAL));
  EXPECT_EQ(server.pending_instances(-1).error(), uv::make_error_code(UV_EINVAL));
  EXPECT_TRUE(server.pending_instances(1));
  ASSERT_TRUE(server.bind(path));
  ASSERT_TRUE(server.sockname());
  EXPECT_EQ(server.sockname().value(), path);
  EXPECT_FALSE(server.bind(path));
  EXPECT_FALSE(server.chmod(0));
  EXPECT_TRUE(server.initialized());
  EXPECT_FALSE(server.closing());
}

TEST_F(RawPipe, openAdoptsNativePipeDescriptorsAndReadWriteStillUseRequests) {
  uv_file descriptors[2]{};
  ASSERT_EQ(uv_pipe(descriptors, UV_NONBLOCK_PIPE, UV_NONBLOCK_PIPE), 0);
  ASSERT_TRUE(client.init(loop));
  ASSERT_TRUE(peer.init(loop));
  const auto opened_read = client.open(descriptors[0]);
  const auto opened_write = peer.open(descriptors[1]);
  if (!opened_read) uvpp::test::close_file(descriptors[0]);
  if (!opened_write) uvpp::test::close_file(descriptors[1]);
  ASSERT_TRUE(opened_read);
  ASSERT_TRUE(opened_write);
  EXPECT_TRUE(client.fileno());
  EXPECT_TRUE(peer.fileno());
  bool read = false;
  ASSERT_TRUE(client.read_start(allocator(), [&](pipe &self, uv::read_result result) {
    ASSERT_TRUE(result);
    if (result.count() <= 0) return;
    EXPECT_EQ(result.storage()[0], 'x');
    EXPECT_TRUE(self.read_stop());
    read = true;
  }));
  completion sent;
  writing.user_data(sent);
  char byte = 'x';
  ASSERT_TRUE(peer.write_static<record<uv::raw::write_request>>(writing, uv::buffer_view{&byte, 1}));
  until([&] { return read && sent.calls == 1; });
  EXPECT_TRUE(sent.outcome);
}

TEST_F(RawPipe, connectBusyPreservesOriginalCallbackAndNamesAfterCompletion) {
  listen(false);
  completion state;
  connecting.user_data(state);
  ASSERT_TRUE(client.connect_static<record<uv::raw::connect_request>>(connecting, path));
  EXPECT_TRUE(connecting.pending());
  EXPECT_EQ(client.connect(connecting, {}, record<uv::raw::connect_request>).error(), uv::make_error_code(UV_EBUSY));
  EXPECT_EQ(client.connect_static<record<uv::raw::connect_request>>(connecting, {}).error(), uv::make_error_code(UV_EBUSY));
  until([&] { return state.calls == 1 && accepted; });
  EXPECT_TRUE(state.outcome);
  ASSERT_TRUE(client.peername());
  EXPECT_EQ(client.peername().value(), path);
  EXPECT_TRUE(peer.sockname());
  std::array<char, 256> native_name{};
  auto native_size = native_name.size();
  ASSERT_EQ(uv_pipe_getsockname(peer.native(), native_name.data(), &native_size), 0);
  EXPECT_EQ(peer.sockname().value(), (std::string{native_name.data(), native_size}));
}

TEST_F(RawPipe, missingEndpointCompletesWithNativeFailureAndClearsPending) {
  ASSERT_TRUE(client.init(loop));
  completion state;
  connecting.user_data(state);
  ASSERT_TRUE(client.connect_static<record<uv::raw::connect_request>>(connecting, path));
  EXPECT_TRUE(connecting.pending());
  until([&] { return state.calls == 1; });
  EXPECT_FALSE(state.outcome);
  EXPECT_FALSE(connecting.pending());
}

TEST_F(RawPipe, invalidConnectDropsPreparedCaptureWithoutRegistering) {
  ASSERT_TRUE(client.init(loop));
  auto token = std::make_shared<int>(1);
  std::weak_ptr<int> weak = token;
  EXPECT_EQ(client.connect(connecting, {}, [token](uv::raw::connect_request&, uv::status) {}).error(),
            uv::make_error_code(UV_EINVAL));
#if UVPP_HAS_PIPE_CONNECT2
  EXPECT_EQ(client.connect(connecting, path, 1u << 8, [token](uv::raw::connect_request&, uv::status) {}).error(),
            uv::make_error_code(UV_EINVAL));
  EXPECT_EQ(client.connect_static<record<uv::raw::connect_request>>(connecting, path, 1u << 8).error(),
            uv::make_error_code(UV_EINVAL));
#endif
  token.reset();
  EXPECT_TRUE(weak.expired());
  EXPECT_FALSE(connecting.pending());
}

TEST_F(RawPipe, ipcValidationPreservesPendingAndReleasesRejectedCaptures) {
  ASSERT_TRUE(client.init(loop, true));
  ASSERT_TRUE(exported.init(loop));
  auto token = std::make_shared<int>(0);
  std::weak_ptr<int> weak = token;
  const auto payload = uv::buffer_view{storage.data(), 1};
  EXPECT_FALSE(client.write_with_handle(writing, payload, exported, [token](uv::raw::write_request&, uv::status) {}));
  EXPECT_FALSE(writing.pending());
  EXPECT_EQ(client.write_with_handle(writing, std::span<const uv::buffer_view>{}, exported,
                                     [token](uv::raw::write_request&, uv::status) {}).error(), uv::make_error_code(UV_EINVAL));
  uv::raw::tcp uninitialized;
  EXPECT_EQ(client.write_with_handle(writing, payload, uninitialized, record<uv::raw::write_request>).error(),
            uv::make_error_code(UV_EBADF));
  exported.close();
  EXPECT_EQ(client.write_with_handle_now(payload, exported).error_code(), uv::make_error_code(UV_EBADF));
  token.reset();
  EXPECT_TRUE(weak.expired());
  EXPECT_FALSE(writing.pending());
  EXPECT_TRUE(server.init(loop));
  EXPECT_EQ(server.write_with_handle_now(payload, exported).error_code(), uv::make_error_code(UV_EINVAL));
}

class RawPipeExport : public RawPipe, public ::testing::WithParamInterface<bool> {};
TEST_P(RawPipeExport, nativeTransferRetainsPayloadAndSupportsRuntimeStaticRequestReuse) {
  pair(true);
  // Export borrows a descriptor; unlike accept it does not register the exported
  // handle on the pipe's loop. Both loops are accessed on this same thread.
  ASSERT_TRUE(exported.init(export_loop));
  ASSERT_TRUE(exported.bind(uv::ipv4{"127.0.0.1", 0}));
  ASSERT_TRUE(received.init(loop));
  bool imported = false;
  ASSERT_TRUE(peer.read_start(allocator(), [&](pipe &self, uv::read_result read) {
    ASSERT_TRUE(read);
    if (read.count() <= 0) return;
    EXPECT_EQ(read.storage()[0], 'h');
    ASSERT_GT(self.pending_count(), 0);
    EXPECT_EQ(self.pending_type(), uv::handle_type::tcp);
    EXPECT_TRUE(self.accept(received));
    EXPECT_EQ(self.pending_count(), 0);
    EXPECT_EQ(self.pending_type(), uv::handle_type::unknown);
    EXPECT_TRUE(self.read_stop());
    imported = true;
  }));
  auto payload = std::make_shared<std::array<char, 1>>(std::array{'h'});
  std::weak_ptr<std::array<char, 1>> weak = payload;
  completion state;
  writing.user_data(state);
  const auto view = uv::buffer_view{payload->data(), payload->size()};
  const auto submitted = GetParam()
    ? client.write_with_handle_static<record<uv::raw::write_request>>(writing, view, exported)
    : client.write_with_handle(writing, view, exported,
        [payload](uv::raw::write_request &request, uv::status status) { record(request, status); });
  ASSERT_TRUE(submitted);
  EXPECT_TRUE(writing.pending());
  if (!GetParam()) payload.reset(); // Static delivery borrows the external owner.
  EXPECT_FALSE(weak.expired());
  EXPECT_EQ(client.write_with_handle_static<record<uv::raw::write_request>>(writing, view, exported).error(),
            uv::make_error_code(UV_EBUSY));
  EXPECT_EQ(client.write_with_handle(writing, view, exported, record<uv::raw::write_request>).error(),
            uv::make_error_code(UV_EBUSY));
  until([&] { return state.calls == 1 && imported; });
  EXPECT_TRUE(state.outcome);
  if (!GetParam()) { EXPECT_TRUE(weak.expired()); }
  EXPECT_EQ(received.sockname().value().port(), exported.sockname().value().port());
  char byte = 'r';
  ASSERT_TRUE(client.write(writing, uv::buffer_view{&byte, 1}, record<uv::raw::write_request>));
  until([&] { return state.calls == 2; });
}
INSTANTIATE_TEST_SUITE_P(CallbackModes, RawPipeExport, ::testing::Values(false, true));

#if defined(__linux__) && UVPP_HAS_PIPE_BIND2 && UVPP_HAS_PIPE_CONNECT2
TEST_F(RawPipe, abstractNamesPreserveInitialNulByte) {
  path = std::string(1, '\0') + "uvpp-" + std::to_string(uv_os_getpid()) + "-raw-abstract";
  pair();
  EXPECT_EQ(server.sockname().value(), path);
  EXPECT_EQ(client.peername().value(), path);
}

#if UVPP_HAS_PIPE_ABSTRACT_NUL_NAMES
TEST_F(RawPipe, abstractNamesPreserveEmbeddedAndTrailingNulBytes) {
  path = std::string(1, '\0') + "uvpp-" + std::to_string(uv_os_getpid()) + "-raw-abstract";
  path.append("\0inside", 7);
  path.push_back('\0');
  pair();
  EXPECT_EQ(server.sockname().value(), path);
  EXPECT_EQ(client.peername().value(), path);
}
#else
TEST_F(RawPipe, olderNativeAbstractNamesRejectAdditionalNulBytesWithoutSubmission) {
  path = std::string(1, '\0') + "uvpp-" + std::to_string(uv_os_getpid()) + "-raw-abstract";
  path.append("\0inside", 7);
  path.push_back('\0');
  ASSERT_TRUE(server.init(loop));
  ASSERT_TRUE(client.init(loop));
  EXPECT_EQ(server.bind(path).error(), uv::make_error_code(UV_EINVAL));
  auto token = std::make_shared<int>(0);
  std::weak_ptr<int> weak = token;
  EXPECT_EQ(client.connect(connecting, path, [token](uv::raw::connect_request&, uv::status) { ADD_FAILURE(); }).error(),
            uv::make_error_code(UV_EINVAL));
  EXPECT_EQ(client.connect_static<record<uv::raw::connect_request>>(connecting, path).error(),
            uv::make_error_code(UV_EINVAL));
  token.reset();
  EXPECT_TRUE(weak.expired());
  EXPECT_FALSE(connecting.pending());
  EXPECT_FALSE(server.active());
  EXPECT_FALSE(client.active());
}
#endif
#endif

struct throwing_connect {
  throwing_connect() = default;
  throwing_connect(const throwing_connect&) { throw std::runtime_error{"prepare connect"}; }
  void operator()(uv::raw::connect_request&, uv::status) const {}
};
TEST_F(RawPipe, callbackPreparationFailureLeavesHandleAndRequestIdle) {
  ASSERT_TRUE(client.init(loop));
  throwing_connect callback;
  EXPECT_THROW(client.connect(connecting, path, callback), std::runtime_error);
  EXPECT_FALSE(connecting.pending());
  EXPECT_FALSE(client.active());
  EXPECT_FALSE(client.closing());
}
}
