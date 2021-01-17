#include <log.h>
#include <qsbr.h>
#include <mem_utils.h>
#include <observer_ptr.hpp>

static constexpr auto SPIN_COUNT = 40;

static constexpr auto LOG = 0;
static constexpr auto MAIN = 1;

static qsbr<2> qsbr_;

int main(int argc, char *argv[])
{
  logger l;
  std::atomic<bool> leave = false;
  std::thread t([&leave, &l]() {
    logger_loop loop;
    loop.register_logger(l);

    int spin_count = 0;
    while(!leave.load(std::memory_order_acquire))
    {
      if(loop())
      {
        qsbr_.quiescent_state(LOG);
        qsbr_.reclaim();
        continue;
      }

      if(UNLIKELY(++spin_count == SPIN_COUNT))
      {
        std::this_thread::yield();
        spin_count = 0;
      }
      else
        _mm_pause();
    }
    loop();
  });

  //  set_thread_affinity(1, false);

  //  ::freopen("/dev/null", "w", stderr);

  using namespace std::string_literals;
  using namespace std::chrono_literals;

  static auto static_string = "static string"s;
  auto string = "non static string"s;
  for(int i = 0; i < 500000; ++i)
  {
    l.log("pipo", 42, static_string.c_str(), std::cref(string));
    l.flush();

    ::timespec ts;
    ::clock_gettime(CLOCK_MONOTONIC_COARSE, &ts);
    l.log() << "[Timestamp] " << ts;
    l.flush();

    std::this_thread::sleep_for(100ns);
  }
  leave.store(true, std::memory_order_release);
  t.join();
}

