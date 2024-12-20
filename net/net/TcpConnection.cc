#include "net/TcpConnection.h"

#include "base/Logging.h"
#include "net/Channel.h"
#include "net/EventLoop.h"
#include "net/Socket.h"
#include "net/SocketOps.h"

#include <assert.h>
#include <unistd.h>

namespace bamboo {

static EventLoop *checkLoop(EventLoop *loop) {
  if (loop == nullptr) {
    LOG_FATAL << "loop is null";
  }
  return loop;
}

void defaultConnectionCallback(const TcpConnectionPtr &conn) {
  LOG_TRACE << conn->localAddress().toIpPort() << " -> "
            << conn->peerAddress().toIpPort() << " is "
            << (conn->connected() ? "UP" : "DOWN");
}

void defaultMessageCallback(const TcpConnectionPtr &conn, Buffer *buffer,
                            TimeStamp receiveTime) {
  buffer->retrieveAll();
}

TcpConnection::TcpConnection(EventLoop *loop, const std::string &name,
                             int sockfd, const InetAddress &localAddr,
                             const InetAddress &peerAddr)
    : loop_(checkLoop(loop)), name_(name), state_(kConnecting),
      socket_(new Socket(sockfd)), channel_(new Channel(loop, sockfd)),
      local_addr_(localAddr), peer_addr_(peerAddr),
      high_water_mark_(kHighWaterMark) {
  channel_->setReadCallback(
      std::bind(&TcpConnection::handleRead, this, std::placeholders::_1));
  channel_->setWriteCallback(std::bind(&TcpConnection::handleWrite, this));
  channel_->setCloseCallback(std::bind(&TcpConnection::handleClose, this));
  channel_->setCloseCallback(std::bind(&TcpConnection::handleError, this));
  LOG_DEBUG << "TcpConnection::ctor[" << name_ << "] at " << this
            << " fd=" << sockfd;
  socket_->setKeepAlive(true);
}

TcpConnection::~TcpConnection() {
  LOG_DEBUG << "TcpConnection::dtor[" << name_ << "] at " << this
            << " fd=" << channel_->fd() << " state=" << stateToString();
  assert(state_ == kDisconnected);
}

void TcpConnection::send(const std::string &buf) {
  if (state_ == kConnected) {
    if (loop_->isInLoopThread()) {
      sendInLoop(buf.c_str(), buf.size());
    } else {
      loop_->runInLoop(std::bind((void(TcpConnection::*)(const std::string &)) &
                                     TcpConnection::sendInLoop,
                                 this, buf));
    }
  }
}

void TcpConnection::send(Buffer *buf) {
  if (state_ == kConnected) {
    if (loop_->isInLoopThread()) {
      sendInLoop(buf->peek(), buf->readableBytes());
      buf->retrieveAll();
    } else {
      loop_->runInLoop(std::bind((void(TcpConnection::*)(const std::string &)) &
                                     TcpConnection::sendInLoop,
                                 this, buf->retrieveAllString()));
    }
  }
}

void TcpConnection::sendInLoop(const std::string &message) {
  sendInLoop(message.c_str(), message.size());
}

void TcpConnection::sendInLoop(const char *message, size_t len) {
  ssize_t wroten_bytes = 0;
  size_t remaining = len;
  bool fault_err = false;
  if (state_ == kDisconnected) {
    LOG_ERROR << "disconnected, give up writing";
  }

  if (!channel_->isWriting() && output_buffer_.readableBytes() == 0) {
    wroten_bytes = ::write(channel_->fd(), message, len);
    if (wroten_bytes >= 0) {
      remaining = len - wroten_bytes;
      if (remaining == 0 && write_complete_call_back_) {
        loop_->queueInLoop(
            std::bind(write_complete_call_back_, shared_from_this()));
      }
    } else {
      wroten_bytes = 0;
      if (errno != EWOULDBLOCK) {
        LOG_ERROR << "TcpConnection";
        if (errno == EPIPE || errno == ECONNRESET) {
          fault_err = true;
        }
      }
    }
  }

  if (!fault_err && remaining > 0) {
    auto old_len = output_buffer_.readableBytes();
    if (old_len + remaining >= high_water_mark_ && old_len < high_water_mark_ &&
        high_water_mark_call_back_) {
      auto func = std::bind(high_water_mark_call_back_, shared_from_this(),
                            old_len + remaining);
      loop_->queueInLoop([func]() { func(); });
    }
    output_buffer_.append((char *)message + wroten_bytes, remaining);
    if (!channel_->isWriting()) {
      channel_->enableWriting();
    }
  }
}

void TcpConnection::shutdown() {
  if (state_ == kConnected) {
    setState(kDisconnecting);
    loop_->runInLoop(std::bind(&TcpConnection::shutdownInLoop, this));
  }
}

void TcpConnection::forceClose() {
  if (state_ == kConnected || state_ == kDisconnecting) {
    setState(kDisconnecting);
    loop_->queueInLoop(std::bind(&TcpConnection::forceCloseInLoop, this));
  }
}

void TcpConnection::forceCloseInLoop() {
  loop_->assertInLoopThread();
  if (state_ == kConnected || state_ == kDisconnecting) {
    handleClose();
  }
}

void TcpConnection::shutdownInLoop() {
  if (!channel_->isWriting()) {
    socket_->shutdownWrite();
  }
}

void TcpConnection::connectEstablished() {
  loop_->assertInLoopThread();
  assert(state_ == kConnecting);
  setState(kConnected);
  channel_->tie(shared_from_this());
  channel_->enableReading();
  connection_call_back_(shared_from_this());
}

void TcpConnection::connectDestroyed() {
  loop_->assertInLoopThread();
  if (state_ == kConnected) {
    setState(kDisconnected);
    channel_->disableAll();

    connection_call_back_(shared_from_this());
  }
  channel_->remove();
}

const char *TcpConnection::stateToString() const {
  switch (state_.load()) {
  case kDisconnected:
    return "kDisconnected";
  case kConnecting:
    return "kConnecting";
  case kConnected:
    return "kConnected";
  case kDisconnecting:
    return "kDisconnecting";
  default:
    return "unknown state";
  }
}

void TcpConnection::handleRead(TimeStamp receive_time) {
  int saved_err = 0;
  auto n = input_buffer_.readFd(channel_->fd(), &saved_err);
  if (n > 0) {
    message_call_back_(shared_from_this(), &input_buffer_, receive_time);
  } else if (n == 0) {
    handleClose();
  } else {
    errno = saved_err;
    LOG_SYSERR << "read error";
    handleError();
  }
}

void TcpConnection::handleWrite() {
  loop_->assertInLoopThread();
  if (channel_->isWriting()) {
    auto n = sockets::write(channel_->fd(), output_buffer_.peek(),
                            output_buffer_.readableBytes());
    if (n > 0) {
      output_buffer_.retrieve(n);
      if (output_buffer_.readableBytes() == 0) {
        channel_->disableWriting();
        if (write_complete_call_back_) {
          loop_->queueInLoop(
              std::bind(write_complete_call_back_, shared_from_this()));
        }
        if (state_ == kDisconnecting) {
          shutdownInLoop();
        }
      }
    } else {
      LOG_ERROR << "write error";
    }
  } else {
    LOG_ERROR << "Tcp Connection fd " << channel_->fd()
              << " is down, no more writing";
  }
}

void TcpConnection::handleClose() {
  loop_->assertInLoopThread();
  LOG_TRACE << "fd " << channel_->fd() << " state = " << stateToString();
  setState(kDisconnected);
  channel_->disableAll();
  auto conn = shared_from_this();
  connection_call_back_(conn);
  close_callback_(conn);
}

void TcpConnection::handleError() {
  int err = sockets::getSocketError(channel_->fd());
  LOG_ERROR << "TcpConnection::handleError [" << name_
            << "] - SO_ERROR = " << err << " " << strerror_tl(err);
}

} // namespace bamboo