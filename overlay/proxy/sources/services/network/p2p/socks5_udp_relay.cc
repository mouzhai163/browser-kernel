// MZ: WebRTC UDP 经 SOCKS5 UDP ASSOCIATE 转发，说明见头文件。

#include "services/network/p2p/socks5_udp_relay.h"

#include <utility>

#include "base/command_line.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "net/base/address_list.h"
#include "net/base/auth.h"
#include "net/base/io_buffer.h"
#include "net/base/net_errors.h"
#include "net/base/network_handle.h"
#include "net/base/proxy_chain.h"
#include "net/base/proxy_server.h"
#include "net/proxy_resolution/proxy_resolution_request.h"
#include "net/proxy_resolution/proxy_resolution_service.h"
#include "net/socket/socks5_auth_cache.h"
#include "net/socket/stream_socket.h"
#include "net/socket/tcp_client_socket.h"
#include "net/url_request/url_request_context.h"
#include "url/gurl.h"

namespace network {

namespace {

constexpr uint8_t kVersion = 0x05;
constexpr uint8_t kAuthNone = 0x00;
constexpr uint8_t kAuthPassword = 0x02;
constexpr uint8_t kCommandAssociate = 0x03;
constexpr uint8_t kAtypIPv4 = 0x01;
constexpr uint8_t kAtypDomain = 0x03;
constexpr uint8_t kAtypIPv6 = 0x04;
// 建立转发的总时限。正常经代理不到 1 秒；代理不支持 UDP 时网页的 WebRTC 要等这么久才放弃 UDP，
// 所以不能太长（STUN 自己的重传也只有几秒）
constexpr base::TimeDelta kSetupTimeout = base::Seconds(3);

// 代理按 URL 选择；WebRTC 没有 URL，用一个普通 https 地址取当前生效的代理
constexpr char kProbeUrl[] = "https://stun.l.google.com/";

}  // namespace

// static
bool Socks5UdpRelay::IsEnabled() {
  return base::CommandLine::ForCurrentProcess()->HasSwitch(kSwitch);
}

Socks5UdpRelay::Socks5UdpRelay(
    net::URLRequestContext* context,
    const net::NetworkAnonymizationKey& network_anonymization_key,
    const net::NetworkTrafficAnnotationTag& traffic_annotation,
    const net::NetLogWithSource& net_log)
    : context_(context),
      network_anonymization_key_(network_anonymization_key),
      traffic_annotation_(traffic_annotation),
      net_log_(net_log) {}

Socks5UdpRelay::~Socks5UdpRelay() = default;

void Socks5UdpRelay::Start(DoneCallback done, base::OnceClosure closed) {
  done_ = std::move(done);
  closed_ = std::move(closed);
  timeout_.Start(FROM_HERE, kSetupTimeout,
                 base::BindOnce(&Socks5UdpRelay::OnTimeout,
                                weak_factory_.GetWeakPtr()));
  if (!context_ || !context_->proxy_resolution_service()) {
    Finish(net::ERR_UNEXPECTED);
    return;
  }
  int result = context_->proxy_resolution_service()->ResolveProxy(
      GURL(kProbeUrl), "GET", network_anonymization_key_,
      net::handles::kInvalidNetworkHandle, &proxy_info_,
      base::BindOnce(&Socks5UdpRelay::OnProxyResolved,
                     weak_factory_.GetWeakPtr()),
      &proxy_request_, net_log_, net::MEDIUM);
  if (result != net::ERR_IO_PENDING) {
    OnProxyResolved(result);
  }
}

void Socks5UdpRelay::OnProxyResolved(int result) {
  proxy_request_.reset();
  if (result != net::OK) {
    Finish(result);
    return;
  }
  const net::ProxyChain& chain = proxy_info_.proxy_chain();
  if (proxy_info_.is_direct() || !chain.is_single_proxy() ||
      chain.First().scheme() != net::ProxyServer::SCHEME_SOCKS5) {
    LOG(ERROR) << "[MZ WebRTC] UDP relay requires a SOCKS5 proxy; current: "
               << proxy_info_.ToDebugString() << ". UDP stays blocked.";
    Finish(net::ERR_NOT_IMPLEMENTED);
    return;
  }
  proxy_host_port_ = chain.First().host_port_pair();
  host_request_ = context_->host_resolver()->CreateRequest(
      proxy_host_port_, network_anonymization_key_,
      net::handles::kInvalidNetworkHandle, net_log_, std::nullopt);
  int rv = host_request_->Start(base::BindOnce(
      &Socks5UdpRelay::OnHostResolved, weak_factory_.GetWeakPtr()));
  if (rv != net::ERR_IO_PENDING) {
    OnHostResolved(rv);
  }
}

void Socks5UdpRelay::OnHostResolved(int result) {
  if (result != net::OK) {
    Finish(result);
    return;
  }
  net::AddressList addresses = host_request_->GetAddressResults();
  host_request_.reset();
  if (addresses.empty()) {
    Finish(net::ERR_NAME_NOT_RESOLVED);
    return;
  }
  control_ = std::make_unique<net::TCPClientSocket>(
      addresses, nullptr, nullptr, net_log_.net_log(), net_log_.source(),
      net::handles::kInvalidNetworkHandle);
  int rv = control_->Connect(base::BindOnce(&Socks5UdpRelay::OnConnected,
                                            weak_factory_.GetWeakPtr()));
  if (rv != net::ERR_IO_PENDING) {
    OnConnected(rv);
  }
}

void Socks5UdpRelay::OnConnected(int result) {
  if (result != net::OK) {
    Finish(result);
    return;
  }
  SendGreeting();
}

void Socks5UdpRelay::SendGreeting() {
  std::optional<net::AuthCredentials> credentials =
      net::SOCKS5AuthCache::GetInstance().GetCredentials(proxy_host_port_);
  std::vector<uint8_t> greeting = {kVersion};
  if (credentials && !credentials->Empty()) {
    greeting.insert(greeting.end(), {2, kAuthNone, kAuthPassword});
  } else {
    greeting.insert(greeting.end(), {1, kAuthNone});
  }
  WriteAll(std::move(greeting),
           base::BindOnce(
               [](base::WeakPtr<Socks5UdpRelay> self, int rv) {
                 if (!self) {
                   return;
                 }
                 if (rv != net::OK) {
                   self->Finish(rv);
                   return;
                 }
                 self->ReadExactly(
                     2, base::BindOnce(&Socks5UdpRelay::OnGreetingReply, self));
               },
               weak_factory_.GetWeakPtr()));
}

void Socks5UdpRelay::OnGreetingReply(int result) {
  if (result != net::OK) {
    Finish(result);
    return;
  }
  if (reply_[0] != kVersion) {
    Finish(net::ERR_SOCKS_CONNECTION_FAILED);
    return;
  }
  if (reply_[1] == kAuthNone) {
    SendAssociate();
    return;
  }
  std::optional<net::AuthCredentials> credentials =
      net::SOCKS5AuthCache::GetInstance().GetCredentials(proxy_host_port_);
  if (reply_[1] != kAuthPassword || !credentials || credentials->Empty()) {
    Finish(net::ERR_PROXY_AUTH_UNSUPPORTED);
    return;
  }
  std::string username = base::UTF16ToUTF8(credentials->username());
  std::string password = base::UTF16ToUTF8(credentials->password());
  if (username.size() > 255 || password.size() > 255) {
    Finish(net::ERR_PROXY_AUTH_UNSUPPORTED);
    return;
  }
  std::vector<uint8_t> auth = {0x01, static_cast<uint8_t>(username.size())};
  auth.insert(auth.end(), username.begin(), username.end());
  auth.push_back(static_cast<uint8_t>(password.size()));
  auth.insert(auth.end(), password.begin(), password.end());
  WriteAll(std::move(auth),
           base::BindOnce(
               [](base::WeakPtr<Socks5UdpRelay> self, int rv) {
                 if (!self) {
                   return;
                 }
                 if (rv != net::OK) {
                   self->Finish(rv);
                   return;
                 }
                 self->ReadExactly(
                     2, base::BindOnce(&Socks5UdpRelay::OnAuthReply, self));
               },
               weak_factory_.GetWeakPtr()));
}

void Socks5UdpRelay::OnAuthReply(int result) {
  if (result != net::OK) {
    Finish(result);
    return;
  }
  if (reply_[1] != 0x00) {
    Finish(net::ERR_PROXY_AUTH_REQUESTED);
    return;
  }
  SendAssociate();
}

void Socks5UdpRelay::SendAssociate() {
  // 客户端地址填 0.0.0.0:0：本地 UDP 端口要等转发建好后才绑定，RFC 1928 允许这样填
  std::vector<uint8_t> request = {kVersion, kCommandAssociate, 0x00, kAtypIPv4,
                                  0,        0,                 0,    0,
                                  0,        0};
  WriteAll(std::move(request),
           base::BindOnce(
               [](base::WeakPtr<Socks5UdpRelay> self, int rv) {
                 if (!self) {
                   return;
                 }
                 if (rv != net::OK) {
                   self->Finish(rv);
                   return;
                 }
                 self->ReadExactly(
                     4, base::BindOnce(&Socks5UdpRelay::OnAssociateHead, self));
               },
               weak_factory_.GetWeakPtr()));
}

void Socks5UdpRelay::OnAssociateHead(int result) {
  if (result != net::OK) {
    Finish(result);
    return;
  }
  if (reply_[0] != kVersion || reply_[1] != 0x00) {
    LOG(ERROR) << "[MZ WebRTC] proxy refused UDP ASSOCIATE, reply="
               << static_cast<int>(reply_[1]) << ". UDP stays blocked.";
    Finish(net::ERR_SOCKS_CONNECTION_FAILED);
    return;
  }
  size_t address_size = 0;
  if (reply_[3] == kAtypIPv4) {
    address_size = net::IPAddress::kIPv4AddressSize;
  } else if (reply_[3] == kAtypIPv6) {
    address_size = net::IPAddress::kIPv6AddressSize;
  } else {
    // 转发地址是域名的代理没见过；不去再解析一次，直接按失败处理
    Finish(net::ERR_SOCKS_CONNECTION_FAILED);
    return;
  }
  ReadExactly(address_size + 2,
              base::BindOnce(&Socks5UdpRelay::OnAssociateAddress,
                             weak_factory_.GetWeakPtr()));
}

void Socks5UdpRelay::OnAssociateAddress(int result) {
  if (result != net::OK) {
    Finish(result);
    return;
  }
  base::span<const uint8_t> reply(reply_);
  net::IPAddress address(reply.first(reply.size() - 2));
  uint16_t port = static_cast<uint16_t>(reply[reply.size() - 2] << 8 |
                                        reply[reply.size() - 1]);
  if (address.IsZero()) {
    // 0.0.0.0 表示"和 TCP 连接同一个地址"
    net::IPEndPoint peer;
    if (control_->GetPeerAddress(&peer) != net::OK) {
      Finish(net::ERR_SOCKS_CONNECTION_FAILED);
      return;
    }
    address = peer.address();
  }
  if (!address.IsValid() || port == 0) {
    Finish(net::ERR_SOCKS_CONNECTION_FAILED);
    return;
  }
  relay_endpoint_ = net::IPEndPoint(address, port);
  ready_ = true;
  VLOG(1) << "[MZ WebRTC] UDP relay ready via " << proxy_host_port_.ToString()
          << " -> " << relay_endpoint_.ToString();
  // 先排队开始盯控制连接，再回调：回调里调用方可能直接销毁本对象，之后不能再碰成员
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&Socks5UdpRelay::WatchControlConnection,
                                weak_factory_.GetWeakPtr()));
  Finish(net::OK);
}

void Socks5UdpRelay::WatchControlConnection() {
  // RFC 1928：TCP 控制连接一断，UDP 转发就失效。这里一直读，读到结束或错误就通知
  if (!control_) {
    return;
  }
  watch_buffer_ = base::MakeRefCounted<net::IOBufferWithSize>(64);
  while (true) {
    int rv = control_->Read(watch_buffer_.get(), watch_buffer_->size(),
                            base::BindOnce(&Socks5UdpRelay::OnControlRead,
                                           weak_factory_.GetWeakPtr()));
    if (rv == net::ERR_IO_PENDING) {
      return;
    }
    if (rv <= 0) {
      OnControlRead(rv);
      return;
    }
    // 协议里代理不会再发数据；有也忽略，继续读
  }
}

void Socks5UdpRelay::OnControlRead(int result) {
  if (result > 0) {
    WatchControlConnection();
    return;
  }
  LOG(WARNING) << "[MZ WebRTC] SOCKS5 UDP control connection closed: "
               << result;
  ready_ = false;
  if (closed_) {
    std::move(closed_).Run();
  }
}

void Socks5UdpRelay::OnTimeout() {
  LOG(ERROR) << "[MZ WebRTC] SOCKS5 UDP relay setup timed out. UDP stays "
                "blocked.";
  Finish(net::ERR_TIMED_OUT);
}

void Socks5UdpRelay::Finish(int result) {
  timeout_.Stop();
  if (result != net::OK) {
    ready_ = false;
    proxy_request_.reset();
    host_request_.reset();
    control_.reset();
    closed_.Reset();
    // 失败后 weak 指针全部失效，避免还在路上的回调再推进状态
    weak_factory_.InvalidateWeakPtrs();
  }
  if (done_) {
    std::move(done_).Run(result);
  }
}

void Socks5UdpRelay::WriteAll(std::vector<uint8_t> data,
                              base::OnceCallback<void(int)> next) {
  size_t size = data.size();
  write_buffer_ = base::MakeRefCounted<net::DrainableIOBuffer>(
      base::MakeRefCounted<net::VectorIOBuffer>(std::move(data)), size);
  write_next_ = std::move(next);
  while (write_buffer_->BytesRemaining() > 0) {
    int rv = control_->Write(write_buffer_.get(),
                             write_buffer_->BytesRemaining(),
                             base::BindOnce(&Socks5UdpRelay::OnWrite,
                                            weak_factory_.GetWeakPtr()),
                             traffic_annotation_);
    if (rv == net::ERR_IO_PENDING) {
      return;
    }
    if (rv <= 0) {
      std::move(write_next_).Run(rv == 0 ? net::ERR_CONNECTION_CLOSED : rv);
      return;
    }
    write_buffer_->DidConsume(rv);
  }
  std::move(write_next_).Run(net::OK);
}

void Socks5UdpRelay::OnWrite(int result) {
  if (result <= 0) {
    std::move(write_next_)
        .Run(result == 0 ? net::ERR_CONNECTION_CLOSED : result);
    return;
  }
  write_buffer_->DidConsume(result);
  if (write_buffer_->BytesRemaining() > 0) {
    int rv = control_->Write(write_buffer_.get(),
                             write_buffer_->BytesRemaining(),
                             base::BindOnce(&Socks5UdpRelay::OnWrite,
                                            weak_factory_.GetWeakPtr()),
                             traffic_annotation_);
    if (rv != net::ERR_IO_PENDING) {
      OnWrite(rv);
    }
    return;
  }
  std::move(write_next_).Run(net::OK);
}

void Socks5UdpRelay::ReadExactly(size_t size,
                                 base::OnceCallback<void(int)> next) {
  reply_.assign(size, 0);
  read_buffer_ = base::MakeRefCounted<net::IOBufferWithSize>(size);
  read_wanted_ = size;
  read_done_ = 0;
  read_next_ = std::move(next);
  while (read_done_ < read_wanted_) {
    int rv = control_->Read(read_buffer_.get(),
                            static_cast<int>(read_wanted_ - read_done_),
                            base::BindOnce(&Socks5UdpRelay::OnRead,
                                           weak_factory_.GetWeakPtr()));
    if (rv == net::ERR_IO_PENDING) {
      return;
    }
    if (rv <= 0) {
      std::move(read_next_).Run(rv == 0 ? net::ERR_CONNECTION_CLOSED : rv);
      return;
    }
    base::span(reply_).subspan(read_done_, static_cast<size_t>(rv))
        .copy_from(read_buffer_->first(static_cast<size_t>(rv)));
    read_done_ += static_cast<size_t>(rv);
  }
  std::move(read_next_).Run(net::OK);
}

void Socks5UdpRelay::OnRead(int result) {
  if (result <= 0) {
    std::move(read_next_)
        .Run(result == 0 ? net::ERR_CONNECTION_CLOSED : result);
    return;
  }
  base::span(reply_).subspan(read_done_, static_cast<size_t>(result))
      .copy_from(read_buffer_->first(static_cast<size_t>(result)));
  read_done_ += static_cast<size_t>(result);
  if (read_done_ < read_wanted_) {
    int rv = control_->Read(read_buffer_.get(),
                            static_cast<int>(read_wanted_ - read_done_),
                            base::BindOnce(&Socks5UdpRelay::OnRead,
                                           weak_factory_.GetWeakPtr()));
    if (rv != net::ERR_IO_PENDING) {
      OnRead(rv);
    }
    return;
  }
  std::move(read_next_).Run(net::OK);
}

// static
std::vector<uint8_t> Socks5UdpRelay::EncodeHeader(const net::IPEndPoint& to) {
  std::vector<uint8_t> header = {0x00, 0x00, 0x00};
  header.push_back(to.address().IsIPv4() ? kAtypIPv4 : kAtypIPv6);
  std::vector<uint8_t> bytes = to.address().CopyBytesToVector();
  header.insert(header.end(), bytes.begin(), bytes.end());
  header.push_back(static_cast<uint8_t>(to.port() >> 8));
  header.push_back(static_cast<uint8_t>(to.port() & 0xff));
  return header;
}

// static
std::optional<size_t> Socks5UdpRelay::DecodeHeader(
    base::span<const uint8_t> datagram,
    net::IPEndPoint* source) {
  // RSV(2) FRAG(1) ATYP(1)；分片包（FRAG != 0）不支持，丢弃
  if (datagram.size() < 4 || datagram[0] != 0 || datagram[1] != 0 ||
      datagram[2] != 0) {
    return std::nullopt;
  }
  size_t address_size = 0;
  if (datagram[3] == kAtypIPv4) {
    address_size = net::IPAddress::kIPv4AddressSize;
  } else if (datagram[3] == kAtypIPv6) {
    address_size = net::IPAddress::kIPv6AddressSize;
  } else {
    // 域名来源地址交不给 WebRTC（它只认 IP），丢弃
    return std::nullopt;
  }
  size_t header_size = 4 + address_size + 2;
  if (datagram.size() < header_size) {
    return std::nullopt;
  }
  net::IPAddress address(datagram.subspan(size_t{4}, address_size));
  uint16_t port = static_cast<uint16_t>(datagram[4 + address_size] << 8 |
                                        datagram[5 + address_size]);
  *source = net::IPEndPoint(address, port);
  return header_size;
}

scoped_refptr<net::IOBufferWithSize> Socks5UdpRelay::Wrap(
    const net::IPEndPoint& to,
    base::span<const uint8_t> payload) const {
  std::vector<uint8_t> header = EncodeHeader(to);
  auto buffer =
      base::MakeRefCounted<net::IOBufferWithSize>(header.size() + payload.size());
  buffer->span().first(header.size()).copy_from(header);
  buffer->span().subspan(header.size()).copy_from(payload);
  return buffer;
}

bool Socks5UdpRelay::Unwrap(const net::IPEndPoint& from,
                            base::span<uint8_t> datagram,
                            base::span<uint8_t>* payload,
                            net::IPEndPoint* source) const {
  if (!ready_ || from != relay_endpoint_) {
    // 不是代理转发回来的包（有人直接往本地端口发），不交给 WebRTC
    return false;
  }
  std::optional<size_t> header_size = DecodeHeader(datagram, source);
  if (!header_size) {
    return false;
  }
  *payload = datagram.subspan(*header_size);
  return true;
}

}  // namespace network
