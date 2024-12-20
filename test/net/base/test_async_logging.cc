#include "base/AsyncLogging.h"
#include "base/Logging.h"
#include "base/TimeStamp.h"

#include <stdio.h>
#include <sys/resource.h>
#include <unistd.h>

using namespace bamboo;
using namespace std;

off_t kRollSize = 500 * 1000 * 1000;

AsyncLogging *g_asyncLog = nullptr;

void asyncOutput(const char *msg, size_t len) { g_asyncLog->append(msg, len); }

void bench(bool longLog) {
  Logger::setOutput(asyncOutput);

  int cnt = 0;
  const int kBatch = 1000;
  string empty = " ";
  string longStr(3000, 'X');
  longStr += " ";

  for (int t = 0; t < 30; ++t) {
    TimeStamp start = TimeStamp::now();
    for (int i = 0; i < kBatch; ++i) {
      LOG_INFO << "Hello 0123456789"
               << " abcdefghijklmnopqrstuvwxyz " << (longLog ? longStr : empty)
               << cnt;
      ++cnt;
    }
    auto end = TimeStamp::now();
  }
}

int main(int argc, char *argv[]) {
  {
    // set max virtual memory to 2GB.
    size_t kOneGB = 1000 * 1024 * 1024;
    rlimit rl = {2 * kOneGB, 2 * kOneGB};
    setrlimit(RLIMIT_AS, &rl);
  }

  printf("pid = %d\n", getpid());

  char name[256] = {'\0'};
  strncpy(name, argv[0], sizeof name - 1);
  AsyncLogging log(::basename(name), kRollSize);
  log.start();
  g_asyncLog = &log;

  bool longLog = argc > 1;
  bench(longLog);
}
