#include "net/EventLoopThread.h"

#include "net/EventLoop.h"

namespace bamboo {

EventLoopThread::EventLoopThread(const ThreadInitCallback &cb,
                                 const std::string &name)
    : thread_(std::bind(&EventLoopThread::threadFunc, this), name),
      callback_(cb) {}

EventLoopThread::~EventLoopThread() {
  exiting_ = true;
  if (loop_ != nullptr) {
    loop_->quit();
    thread_.join();
  }
}

EventLoop *EventLoopThread::startLoop() {
  thread_.start();
  EventLoop *loop = nullptr;

  {
    std::unique_lock<std::mutex> lock(mutex_);
    while (loop_ == nullptr) {
      cond_.wait(lock);
    }
    loop = loop_;
  }
  return loop;
}

void EventLoopThread::threadFunc() {
  EventLoop loop;
  if (callback_ != nullptr) {
    callback_(&loop);
  }

  {
    std::unique_lock<std::mutex> lock(mutex_);
    loop_ = &loop;
    cond_.notify_one();
  }

  loop.loop();
  std::unique_lock<std::mutex> lock(mutex_);
  loop_ = nullptr;
}

} // namespace bamboo