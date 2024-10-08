#include "base/LogFile.h"

#include "base/FileUtil.h"

#include <cstdio>
#include <ctime>

namespace bamboo {
LogFile::LogFile(const std::string &basename, size_t roll_size,
                 int flush_interval, int check_every_n)
    : basename_(basename), roll_size_(roll_size),
      flush_interval_(flush_interval), check_every_n_(check_every_n),
      count_(0) {
  rollFile();
}

void LogFile::append(const char *logline, size_t len) {
  file_->append(logline, len);

  if (file_->writtenBytes() > roll_size_) {
    rollFile();
  } else {
    ++count_;
    if (count_ >= check_every_n_) {
      count_ = 0;
      auto now = ::time(nullptr);
      auto this_period = now / kRollPerSeconds_ * kRollPerSeconds_;
      if (this_period != start_) {
        rollFile();
      } else if (now - last_flush_ > flush_interval_) {
        last_flush_ = now;
        file_->flush();
      }
    }
  }
}

void LogFile::flush() { file_->flush(); }

void LogFile::rollFile() {
  auto filename = getLogFileName(basename_);
  file_.reset(new AppendFile(filename.c_str()));
}

std::string LogFile::getLogFileName(const std::string &basename) {
  std::string filename;
  filename.reserve(basename.size() + 4);
  filename = basename;

  filename += ".log";

  return filename;
}

} // namespace bamboo