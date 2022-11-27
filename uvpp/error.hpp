#pragma once

#include "uv.h"
namespace uv {

  struct Error {
    int code;

    Error() = default;
    Error(int _code) : code{_code} { }

    constexpr operator bool() const { return code < 0; }
    constexpr bool operator ! () const { return code >= 0; }

    constexpr bool operator == (const Error &other) const { return code == other.code; }
    constexpr bool operator != (const Error &other) const { return code != other.code; }


    static int safe(int res) {
      if (res < 0)
        throw Error(res);

      return res;
    }

#if 0
    template<typename... Args, void (*cb)(Args... args)>
    static void safeCb(Args... args, int status) {
      safe(status);
      cb(args);
    }
#else
    template<class T, void (*cb)(T *)>
    static void safeCb(T *prm, int status) {
      safe(status);
      cb(prm);
    }
#endif

    template<class T, void (*cb)(T *)>
    static void safeReqCb(T *req) {
      safe(req->result);
      cb(req);
    }


    const char *message() const {
      return uv_strerror(code);
    }

#ifdef __cpp_lib_string_view
    std::string str() const {
      return uv_strerror(code);
    }
#endif

#if UV_VERSION_MAJOR >= 1 && UV_VERSION_MINOR >= 22
    char *message(char *buf, size_t buflen) const {
      return uv_strerror_r(code, buf, buflen);
    }

# ifdef __cpp_lib_string_view
    std::string &message(std::string &buf) const {
      uv_strerror_r(code, &buf.front(), buf.capacity());
      return buf;
    }
# endif
#endif

    const char *name() const {
      return uv_err_name(code);
    }

#if UV_VERSION_MAJOR >= 1 && UV_VERSION_MINOR >=  22
    char *name(char *buf, size_t buflen) const {
      return uv_err_name_r(code, buf, buflen);
    }

# ifdef __cpp_lib_string_view
    std::string &name(std::string &buf) const {
      uv_err_name_r(code, &buf.front(), buf.capacity());
      return buf;
    }
# endif
#endif

    int translate() {
      return uv_translate_sys_error(code);
    }

  };

}
