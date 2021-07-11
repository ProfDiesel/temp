#include <feed/feedlibpp.hpp>

#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
  const int fd = ::open(argv[1], O_RDONLY);

  struct stat s;
  ::fstat(fd, &s);
  const std::size_t buffer_size = s.st_size;

  void *const buffer = ::mmap(0, buffer_size, PROT_READ, MAP_PRIVATE, fd, 0);
  if(buffer == MAP_FAILED)
  {
    ::perror("mmap");
    ::exit(EXIT_FAILURE);
  }

  up::future result;
  up::server server("0.0.0.0", "4400", "0.0.0.0", "4401", result);

  while(!result.is_set())
    server.poll();
  if(!result)
    ::fprintf(stderr, "%s\n", result.message().data());

  result = server.replay(buffer, buffer_size);

  while(!result.is_set())
    server.poll();
  if(!result)
    ::fprintf(stderr, "%s\n", result.message().data());

  return static_cast<bool>(result);
}
