#include <cstring>
#include <iostream>

#include <sys/mman.h>

namespace
{
__attribute__((constructor)) void init()
{
  if(::mlockall(MCL_CURRENT | MCL_FUTURE))
  {
    std::clog << "::mlockall ["
              << "\x1b[91;1m"
              << "Failed - " << std::strerror(errno) << "(" << errno << ")"
              << "\x1b[0m"
              << "]" << std::endl;
    std::exit(1);
  }
  std::clog << "::mlockall ["
            << "\x1b[92;1m"
            << "OK"
            << "\x1b[0m"
            << "]" << std::endl;
}
} // namespace
