#include "gtest/gtest.h"
#include "uvpp/uv.hpp"

class SignalTest : public testing::Test {
protected:
  static constexpr int NSIGNALS = 10;

  uv::Loop *loop = uv::Loop::getDefault();

  struct TimerCtx : public uv::Timer {
    unsigned int ncalls;
    int signum;

    TimerCtx(uv::Loop *loop, int signum) : uv::Timer(loop), ncalls{0}, signum{signum} {
      start(timerCb, 5, 5);
    }

    static void timerCb(uv::Timer *timer) {
      TimerCtx *ctx = static_cast<TimerCtx *>(timer);

      raise(ctx->signum);

      if (++ctx->ncalls == NSIGNALS) {
        timer->close();
      }
    }
  };

  struct SignalCtx : public uv::Signal {
    enum class Action { Stop, Close, NoOp } action;
    unsigned int ncalls;
    int signum;
    bool one_shot;

    SignalCtx(uv::Loop *loop, int signum, bool one_shot) : uv::Signal(loop), action{Action::Close}, ncalls{0}, signum{signum}, one_shot{one_shot} {
      if (one_shot)
        start_oneshot(signalCbOneShot, signum);
      else
        start(signalCb, signum);
    }

    static void signalCb(uv::Signal *signal, int signum) {
      SignalCtx *ctx = static_cast<SignalCtx *>(signal);
      ASSERT_EQ(ctx->signum, signum);
      ASSERT_FALSE(ctx->one_shot);

      if (++ctx->ncalls == NSIGNALS) {
        switch (ctx->action) {
        case Action::Stop:
          signal->stop();
          break;
        case Action::Close:
          signal->close();
          break;
        case Action::NoOp:
          break;
        }
      }
    }

    static void signalCbOneShot(uv::Signal *signal, int signum) {
      SignalCtx *ctx = static_cast<SignalCtx *>(signal);
      ASSERT_EQ(ctx->signum, signum);
      ASSERT_TRUE(ctx->one_shot);

      ASSERT_EQ(++ctx->ncalls, 1);
      if (ctx->action == SignalCtx::Action::Close)
        signal->close();
    }

  };
};

TEST_F(SignalTest, killInvalidSignum) {
  uv_pid_t pid = uv::os_getpid();

  try {
    uv::Process::kill(pid, -1);
    FAIL();
  } catch (const uv::Error &e) {
    ASSERT_EQ(e.code, UV_EINVAL);
  } catch (...) {
    FAIL();
  }

  try {
    uv::Process::kill(pid, 4096);
    FAIL();
  } catch (const uv::Error &e) {
    ASSERT_EQ(e.code, UV_EINVAL);
  } catch (...) {
    FAIL();
  }
}

TEST_F(SignalTest, signal) {
  TimerCtx tc(loop, SIGCHLD);
  SignalCtx sc(loop, SIGCHLD, false);

  sc.action = SignalCtx::Action::Stop;
  ASSERT_FALSE(loop->run());
  ASSERT_EQ(sc.ncalls, NSIGNALS);
  ASSERT_EQ(tc.ncalls, NSIGNALS);

  TimerCtx tc2(loop, SIGCHLD);
  ASSERT_FALSE(loop->run());
  ASSERT_EQ(tc2.ncalls, NSIGNALS);

  sc.ncalls = 0;
  sc.action = SignalCtx::Action::Close;
  sc.start(SignalCtx::signalCb, SIGCHLD);
  TimerCtx tc3(loop, SIGCHLD);
  ASSERT_FALSE(loop->run());
  ASSERT_EQ(sc.ncalls, NSIGNALS);
  ASSERT_EQ(tc3.ncalls, NSIGNALS);
}

TEST_F(SignalTest, signals) {
  std::array<SignalCtx, 4> scs = {
    SignalCtx(loop, SIGUSR1, false),
    SignalCtx(loop, SIGUSR1, false),
    SignalCtx(loop, SIGUSR2, false),
    SignalCtx(loop, SIGUSR2, false),
  };
  std::array<TimerCtx, 2> tcs = {
    TimerCtx(loop, SIGUSR1),
    TimerCtx(loop, SIGUSR2),
  };
  ASSERT_FALSE(loop->run());

  for (auto &sc : scs) {
    ASSERT_EQ(sc.ncalls, NSIGNALS);
  }
  for (auto &tc : tcs) {
    ASSERT_EQ(tc.ncalls, NSIGNALS);
  }
}

TEST_F(SignalTest, signalOneShot) {
  SignalCtx sc(loop, SIGCHLD, true);
  TimerCtx tc(loop, SIGCHLD);

  sc.action = SignalCtx::Action::NoOp;
  ASSERT_FALSE(loop->run());
  ASSERT_EQ(sc.ncalls, 1);
  ASSERT_EQ(tc.ncalls, NSIGNALS);

  TimerCtx tc2(loop, SIGCHLD);
  ASSERT_FALSE(loop->run());
  ASSERT_EQ(sc.ncalls, 1);
  ASSERT_EQ(tc2.ncalls, NSIGNALS);

  sc.ncalls = 0;
  sc.action = SignalCtx::Action::Close;
  sc.start_oneshot(SignalCtx::signalCbOneShot, SIGCHLD);
  TimerCtx tc3(loop, SIGCHLD);
  ASSERT_FALSE(loop->run());
  ASSERT_EQ(sc.ncalls, 1);
  ASSERT_EQ(tc3.ncalls, NSIGNALS);
}

TEST_F(SignalTest, mixed2OneShot) {
  std::array<SignalCtx, 2> scs = {
    SignalCtx(loop, SIGCHLD, true),
    SignalCtx(loop, SIGCHLD, true),
  };
  TimerCtx tc(loop, SIGCHLD);

  ASSERT_FALSE(loop->run());

  for (auto &sc : scs) {
    ASSERT_EQ(sc.ncalls, 1);
  }
  ASSERT_EQ(tc.ncalls, NSIGNALS);
}

TEST_F(SignalTest, mixed2OneShot1NormalRemoveNormal) {
  std::array<SignalCtx, 3> scs = {
    SignalCtx(loop, SIGCHLD, true),
    SignalCtx(loop, SIGCHLD, true),
    SignalCtx(loop, SIGCHLD, false),
  };
  TimerCtx tc(loop, SIGCHLD);

  scs[2].close();
  ASSERT_FALSE(loop->run());

  ASSERT_EQ(scs[0].ncalls, 1);
  ASSERT_EQ(scs[1].ncalls, 1);
  ASSERT_EQ(scs[2].ncalls, 0);
  ASSERT_EQ(tc.ncalls, NSIGNALS);
}

TEST_F(SignalTest, mixed2Normal1OneShotRemoveOneShot) {
  std::array<SignalCtx, 3> scs = {
    SignalCtx(loop, SIGCHLD, false),
    SignalCtx(loop, SIGCHLD, false),
    SignalCtx(loop, SIGCHLD, true),
  };
  TimerCtx tc(loop, SIGCHLD);

  scs[2].close();
  ASSERT_FALSE(loop->run());

  ASSERT_EQ(scs[0].ncalls, NSIGNALS);
  ASSERT_EQ(scs[1].ncalls, NSIGNALS);
  ASSERT_EQ(scs[2].ncalls, 0);
  ASSERT_EQ(tc.ncalls, NSIGNALS);
}

TEST_F(SignalTest, mixed2Normal2OneShotRemoveNormal) {
  std::array<SignalCtx, 4> scs = {
    SignalCtx(loop, SIGCHLD, false),
    SignalCtx(loop, SIGCHLD, false),
    SignalCtx(loop, SIGCHLD, true),
    SignalCtx(loop, SIGCHLD, true),
  };
  TimerCtx tc(loop, SIGCHLD);

  scs[0].close();
  scs[1].close();
  ASSERT_FALSE(loop->run());

  ASSERT_EQ(scs[0].ncalls, 0);
  ASSERT_EQ(scs[1].ncalls, 0);
  ASSERT_EQ(scs[2].ncalls, 1);
  ASSERT_EQ(scs[3].ncalls, 1);
  ASSERT_EQ(tc.ncalls, NSIGNALS);
}

TEST_F(SignalTest, mixed1Normal1OneShot2NormalRemoveNormal) {
  std::array<SignalCtx, 4> scs = {
    SignalCtx(loop, SIGCHLD, false),
    SignalCtx(loop, SIGCHLD, true),
    SignalCtx(loop, SIGCHLD, false),
    SignalCtx(loop, SIGCHLD, false),
  };
  TimerCtx tc(loop, SIGCHLD);

  scs[0].close();
  scs[2].close();
  ASSERT_FALSE(loop->run());

  ASSERT_EQ(scs[0].ncalls, 0);
  ASSERT_EQ(scs[1].ncalls, 1);
  ASSERT_EQ(scs[2].ncalls, 0);
  ASSERT_EQ(scs[3].ncalls, NSIGNALS);
  ASSERT_EQ(tc.ncalls, NSIGNALS);
}