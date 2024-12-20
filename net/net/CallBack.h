#pragma once

#include <functional>
#include <memory>

namespace bamboo {

class Buffer;
class TcpConnection;
class TimeStamp;

using TcpConnectionPtr = std::shared_ptr<TcpConnection>;

using TimerCallback = std::function<void()>;

using ConnectionCallback = std::function<void(const TcpConnectionPtr &)>;

using CloseCallback = std::function<void(const TcpConnectionPtr &)>;

using WriteCompleteCallback = std::function<void(const TcpConnectionPtr &)>;

using MessageCallback =
    std::function<void(const TcpConnectionPtr &, Buffer *, TimeStamp)>;

using HighWaterMarkCallback =
    std::function<void(const TcpConnectionPtr &, size_t)>;

void defaultConnectionCallback(const TcpConnectionPtr &conn);
void defaultMessageCallback(const TcpConnectionPtr &conn, Buffer *buffer,
                            TimeStamp receiveTime);

} // namespace bamboo