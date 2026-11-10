// MZ: WebRTC UDP 经 SOCKS5 UDP ASSOCIATE 转发。
//
// Chromium 的 SOCKS5 只转发 TCP，WebRTC 的 UDP 要么直连（泄露真实 IP），要么在
// disable_non_proxied_udp 策略下完全不用。软件确认代理支持 UDP 后会给浏览器加
// --mz-webrtc-udp-relay，浏览器把它传给网络服务和渲染进程：
//   - 渲染进程据此放开 UDP/STUN（仍然不枚举网卡、不给默认本地候选）；
//   - 网络服务里每个 P2P UDP 套接字都先通过这个类和代理建立 UDP 转发，之后所有
//     数据报都发给代理的转发地址，并带上 SOCKS5 UDP 头。
// 转发建立失败、控制连接断开、代理不是 SOCKS5，一律让套接字报错，绝不改成直连。

#ifndef SERVICES_NETWORK_P2P_SOCKS5_UDP_RELAY_H_
#define SERVICES_NETWORK_P2P_SOCKS5_UDP_RELAY_H_

#include <memory>
#include <optional>
#include <vector>

#include "base/component_export.h"
#include "base/containers/span.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/timer/timer.h"
#include "net/base/host_port_pair.h"
#include "net/base/ip_endpoint.h"
#include "net/base/network_anonymization_key.h"
#include "net/dns/host_resolver.h"
#include "net/log/net_log_with_source.h"
#include "net/proxy_resolution/proxy_info.h"
#include "net/traffic_annotation/network_traffic_annotation.h"

namespace net {
class DrainableIOBuffer;
class IOBuffer;
class IOBufferWithSize;
class ProxyResolutionRequest;
class StreamSocket;
class URLRequestContext;
}  // namespace net

namespace network {

class COMPONENT_EXPORT(NETWORK_SERVICE) Socks5UdpRelay {
 public:
  // 浏览器命令行（由浏览器复制到网络服务和渲染进程）
  static constexpr char kSwitch[] = "mz-webrtc-udp-relay";
  static bool IsEnabled();

  // 转发头最长：RSV(2) FRAG(1) ATYP(1) IPv6(16) PORT(2)
  static constexpr size_t kMaxHeaderSize = 22;

  using DoneCallback = base::OnceCallback<void(int net_error)>;

  Socks5UdpRelay(net::URLRequestContext* context,
                 const net::NetworkAnonymizationKey& network_anonymization_key,
                 const net::NetworkTrafficAnnotationTag& traffic_annotation,
                 const net::NetLogWithSource& net_log);
  Socks5UdpRelay(const Socks5UdpRelay&) = delete;
  Socks5UdpRelay& operator=(const Socks5UdpRelay&) = delete;
  ~Socks5UdpRelay();

  // 解析代理、连接、认证、UDP ASSOCIATE。`done` 只调用一次；成功后
  // relay_endpoint() 可用。`closed` 在之后控制连接断开时调用（可能不调用）。
  void Start(DoneCallback done, base::OnceClosure closed);

  bool ready() const { return ready_; }
  const net::IPEndPoint& relay_endpoint() const { return relay_endpoint_; }

  // 给发往 `to` 的数据加上 SOCKS5 UDP 头，返回整个数据报。
  scoped_refptr<net::IOBufferWithSize> Wrap(
      const net::IPEndPoint& to,
      base::span<const uint8_t> payload) const;

  // 去掉 SOCKS5 UDP 头。`from` 不是转发地址、头不完整或是分片包时返回 false（丢弃）。
  bool Unwrap(const net::IPEndPoint& from,
              base::span<uint8_t> datagram,
              base::span<uint8_t>* payload,
              net::IPEndPoint* source) const;

  // 单元测试用：直接解析/编码头
  static std::vector<uint8_t> EncodeHeader(const net::IPEndPoint& to);
  static std::optional<size_t> DecodeHeader(base::span<const uint8_t> datagram,
                                            net::IPEndPoint* source);

 private:
  void OnProxyResolved(int result);
  void OnHostResolved(int result);
  void OnConnected(int result);
  void SendGreeting();
  void OnGreetingReply(int result);
  void OnAuthReply(int result);
  void SendAssociate();
  void OnAssociateHead(int result);
  void OnAssociateAddress(int result);
  void WatchControlConnection();
  void OnControlRead(int result);
  void OnTimeout();
  void Finish(int result);

  // 写完整个缓冲区 / 读满 `size` 字节后回调（结果是 net 错误码或 OK）
  void WriteAll(std::vector<uint8_t> data, base::OnceCallback<void(int)> next);
  void OnWrite(int result);
  void ReadExactly(size_t size, base::OnceCallback<void(int)> next);
  void OnRead(int result);

  raw_ptr<net::URLRequestContext> context_;
  const net::NetworkAnonymizationKey network_anonymization_key_;
  const net::NetworkTrafficAnnotationTag traffic_annotation_;
  net::NetLogWithSource net_log_;

  DoneCallback done_;
  base::OnceClosure closed_;
  base::OneShotTimer timeout_;

  net::ProxyInfo proxy_info_;
  std::unique_ptr<net::ProxyResolutionRequest> proxy_request_;
  net::HostPortPair proxy_host_port_;
  std::unique_ptr<net::HostResolver::ResolveHostRequest> host_request_;
  std::unique_ptr<net::StreamSocket> control_;

  scoped_refptr<net::DrainableIOBuffer> write_buffer_;
  base::OnceCallback<void(int)> write_next_;
  scoped_refptr<net::IOBufferWithSize> read_buffer_;
  size_t read_wanted_ = 0;
  size_t read_done_ = 0;
  base::OnceCallback<void(int)> read_next_;
  std::vector<uint8_t> reply_;
  scoped_refptr<net::IOBufferWithSize> watch_buffer_;

  net::IPEndPoint relay_endpoint_;
  bool ready_ = false;

  base::WeakPtrFactory<Socks5UdpRelay> weak_factory_{this};
};

}  // namespace network

#endif  // SERVICES_NETWORK_P2P_SOCKS5_UDP_RELAY_H_
