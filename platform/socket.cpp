#include <arpa/inet.h>  
#include <generic/cancel.hpp>  // 取消功能
#include <platform/error_handling.hpp>  // 错误处理功能
#include <platform/platform_io.hpp>  // 平台输入输出功能
#include <platform/socket.hpp> 
#include <utils/finally.hpp>  //确保资源释放的工具
#include <utils/string_utils.hpp>  
#include <netdb.h> 
#include <netinet/in.h>  
#include <netinet/tcp.h>  
#include <sys/socket.h>  
#include <sys/types.h>  // 引入基本系统类型
#include <sys/un.h>  // 引入 UNIX 域套接字相关的结构
#include <unistd.h>  // 引入 POSIX 操作系统 API

namespace zh_async 
{
    // 获取地址信息错误类别
    std::error_category const &getAddrInfoCategory() {
        static struct : std::error_category {
            char const *name() const noexcept override {
                return "getaddrinfo";  
            }

            // 返回具体错误信息
            std::string message(int e) const override {
                return gai_strerror(e); 
            }
        } instance;  

        return instance;  
    }

    // 解析所有地址的函数，返回一个 Expected 类型的 ResolveResult
    auto AddressResolver::resolve_all() -> Expected<ResolveResult> {
        // 如果参数为空
        if (m_host.empty()) [[unlikely]] {
            return std::errc::invalid_argument;  
        }

        struct addrinfo *result;  // 定义 addrinfo 结构指针
        // 调用 getaddrinfo 函数解析主机地址
        int err = getaddrinfo(m_host.c_str(),
                              m_service.empty() ? nullptr : m_service.c_str(),
                              &m_hints, &result);

        if(err) [[unlikely]]  // 如果发生错误
        {
#if ZH_ASYNC_DEBUG
            std::cerr << m_host << ": " << gai_strerror(err) << '\n'; 
#endif
            return std::error_code(err,getAddrInfoCategory());  
        }

        Finally fin = [&] { freeaddrinfo(result); };  // 确保在退出时释放 addrinfo 结构

        ResolveResult res;  // 定义解析结果
        // 遍历 addrinfo 结构链表
        for (struct addrinfo *rp = result; rp != nullptr; rp = rp->ai_next) {
            // 将解析到的地址信息添加到结果中
            res.addrs
                .emplace_back(rp->ai_addr, rp->ai_addrlen, rp->ai_family,
                              rp->ai_socktype, rp->ai_protocol)
                .trySetPort(m_port);  
        }

        if(res.addrs.empty())[[unlikely]]  // 检查是否没有可用的地址
        {
#if ZH_ASYNC_DEBUG
            std::cerr << m_host << ": no matching host address\n";  
#endif
            return std::errc::bad_address;  
        }
        res.service = std::move(m_service);  // 移动服务名称到结果中
        return res;  // 返回解析结果
    }

    // 解析一个地址的函数
    Expected<SocketAddress> AddressResolver::resolve_one() {
        auto res = resolve_all();  // 调用解析所有地址函数
        if (res.has_error()) [[unlikely]] {  
            return res.error();  
        }
        return res->addrs.front();  // 返回第一个解析到的地址
    }

    // 解析一个地址并返回服务名称的函数
    Expected<SocketAddress> AddressResolver::resolve_one(std::string &service) {
        auto res = resolve_all(); 
        if (res.has_error()) [[unlikely]] { 
            return res.error(); 
        }
        service = std::move(res->service);  // 移动服务名称到参数中
        return res->addrs.front();  
    }

    // SocketAddress 构造函数
    SocketAddress::SocketAddress(struct sockaddr const *addr, socklen_t addrLen,
                                 sa_family_t family, int sockType, int protocol)
        : mSockType(sockType),  // 初始化套接字类型
          mProtocol(protocol) {  
        std::memcpy(&mAddr, addr, addrLen);  
        mAddr.ss_family = family;  
        mAddrLen = addrLen;  
    }

    // 获取主机名的函数
    std::string SocketAddress::host() const {
        if (family() == AF_INET) {  // 如果地址族是 IPv4
            auto &sin = reinterpret_cast<struct sockaddr_in const &>(mAddr).sin_addr; 
            char buf[INET_ADDRSTRLEN] = {};  // 定义用于存储地址字符串的缓冲区
            inet_ntop(family(), &sin, buf, sizeof(buf));  // 将地址转换为字符串格式
            return buf;  // 返回地址字符串
        } 
        else if (family() == AF_INET6) {  // 如果地址族是 IPv6
            auto &sin6 = reinterpret_cast<struct sockaddr_in6 const &>(mAddr).sin6_addr;  // 获取 IPv6 地址
            char buf[INET6_ADDRSTRLEN] = {};  // 定义用于存储地址字符串的缓冲区
            inet_ntop(AF_INET6, &sin6, buf, sizeof(buf));  // 将地址转换为字符串格式
            return buf;  // 返回地址字符串
        } else [[unlikely]] {  
            throw std::runtime_error("address family not ipv4 or ipv6");  
        }
    }

    // 获取端口号的函数
    int SocketAddress::port() const {
        if (family() == AF_INET) {  // 如果地址族是 IPv4
            auto port = reinterpret_cast<struct sockaddr_in const &>(mAddr).sin_port;  // 获取 IPv4 端口
            return ntohs(port);  // 转换并返回主机字节序端口
        } 
        else if (family() == AF_INET6) {  // 如果地址族是 IPv6
            auto port = reinterpret_cast<struct sockaddr_in6 const &>(mAddr).sin6_port;  // 获取 IPv6 端口
            return ntohs(port);  // 转换并返回主机字节序端口
        } 
        else [[unlikely]] {  
            throw std::runtime_error("address family not ipv4 or ipv6");  // 抛出异常
        }
    }

    // 尝试设置端口号的函数
    void SocketAddress::trySetPort(int port) {
        if (family() == AF_INET) {  // 如果地址族是 IPv4
            reinterpret_cast<struct sockaddr_in &>(mAddr).sin_port =
                htons(static_cast<uint16_t>(port));  // 设置 IPv4 端口
        } 
        else if (family() == AF_INET6) {  // 如果地址族是 IPv6
            reinterpret_cast<struct sockaddr_in6 &>(mAddr).sin6_port =
                htons(static_cast<uint16_t>(port));  // 设置 IPv6 端口
        }
    }

    String SocketAddress::toString()const {
        return host() + ':' + to_string(port());  
    }

    // 获取套接字地址的函数
    SocketAddress get_socket_address(SocketHandle &sock)
    {
        SocketAddress ska; 
        ska.mAddrLen = sizeof(ska.mAddr);  
        // 调用 getsockname 函数获取套接字的地址
        throwingErrorErrno(getsockname(
            sock.fileNo(),reinterpret_cast<struct sockaddr*>(&ska.mAddr),
            &ska.mAddrLen
        ));
        return ska;  
    }

    // 获取套接字对端地址的函数
    SocketAddress get_socket_peer_address(SocketHandle &sock)
    {
        SocketAddress ska;  
        ska.mAddrLen = sizeof(ska.mAddr);  
        // 调用 getpeername 函数获取套接字的对端地址
        throwingErrorErrno(getsockname(
            sock.fileNo(),reinterpret_cast<struct sockaddr*>(&ska.mAddr),
            &ska.mAddrLen
        ));
        return ska;  
    }

    // 创建套接字的异步函数
    Task<Expected<SocketHandle>> createSocket(int family,int type,int protocol)
    {
        // 创建异步套接字
        int fd = co_await expectError(
                co_await UringOp().prep_socket(family,type,protocol,0));
#if ZH_ASYNC_INVALFIX
                .or_else(std::errc::invalid_argument,
                               [&] { return socket(family, type, protocol); }) 
#endif
            SocketHandle sock(fd);  
            co_return sock;  
    }

    // 异步连接套接字的函数
    Task<Expected<SocketHandle>> socket_connect(SocketAddress const &addr) {
        SocketHandle sock = co_await co_await createSocket(
            addr.family(), addr.socktype(), addr.protocol());  // 创建套接字并连接
        // 连接套接字
        co_await expectError(co_await UringOp().prep_connect(
            sock.fileNo(), reinterpret_cast<const struct sockaddr *>(&addr.mAddr),
            addr.mAddrLen))
#if ZH_ASYNC_INVALFIX
                     .or_else(std::errc::invalid_argument, [&] { return connect(sock.fileNo(),
            reinterpret_cast<const struct sockaddr *>(&addr.mAddr), addr.mAddrLen); })  
#endif
            ;
        co_return sock;  // 返回套接字句柄
    }

    // 带超时的异步连接套接字的函数
    Task<Expected<SocketHandle>>
    socket_connect(SocketAddress const &addr,
                   std::chrono::steady_clock::duration timeout) {
        SocketHandle sock = co_await co_await createSocket(
            addr.family(), addr.socktype(), addr.protocol());
        auto ts = durationToKernelTimespec(timeout);  // 转换超时时间格式
        co_await expectError(co_await UringOp::link_ops(
            UringOp().prep_connect(
                sock.fileNo(),
                reinterpret_cast<const struct sockaddr *>(&addr.mAddr),
                addr.mAddrLen),
            UringOp().prep_link_timeout(&ts, IORING_TIMEOUT_BOOTTIME)))  // 使用链式操作处理连接和超时设置
#if ZH_ASYNC_INVALFIX
                     .or_else(std::errc::invalid_argument, [&] { return connect(sock.fileNo(),
            reinterpret_cast<const struct sockaddr *>(&addr.mAddr), addr.mAddrLen); })  
#endif
            ;
        co_return sock;  
    }

    // 带取消标记的异步连接套接字的函数
    Task<Expected<SocketHandle>> socket_connect(SocketAddress const &addr,
                                                CancelToken cancel) {
        SocketHandle sock =
            co_await co_await createSocket(addr.family(), SOCK_STREAM, 0);  
        if (cancel.is_canceled()) [[unlikely]] {  // 检查是否被取消
            co_return std::errc::operation_canceled; 
        }
        // 连接套接字并添加取消保护
        co_await expectError(
            co_await UringOp()
                .prep_connect(
                    sock.fileNo(),
                    reinterpret_cast<const struct sockaddr *>(&addr.mAddr),
                    addr.mAddrLen)
                .cancelGuard(cancel))  // 添加取消处理
#if ZH_ASYNC_INVALFIX
                     .or_else(std::errc::invalid_argument, [&] { return connect(sock.fileNo(),
            reinterpret_cast<const struct sockaddr *>(&addr.mAddr), addr.mAddrLen); })  // 如果存在无效参数，调用传统连接函数
#endif
                    ;
        co_return sock;  // 返回套接字句柄
    }

    // 异步绑定监听器的函数
    Task<Expected<SocketListener>> listener_bind(SocketAddress const &addr,
                                                 int backlog) {
        SocketHandle sock =
            co_await co_await createSocket(addr.family(), SOCK_STREAM, 0);  
        co_await socketSetOption(sock, SOL_SOCKET, SO_REUSEADDR, 1);  // 设置重用地址选项
        co_await socketSetOption(sock, SOL_SOCKET, SO_REUSEPORT, 1);  // 设置重用端口选项
        SocketListener serv(sock.releaseFile());  // 创建 SocketListener 对象

        co_await expectError(bind(
            serv.fileNo(), reinterpret_cast<struct sockaddr const *>(&addr.mAddr),
            addr.mAddrLen));
        co_await expectError(listen(serv.fileNo(), backlog));  // 开始监听
        co_return serv; 
    }

    // 异步接受连接的函数
    Task<Expected<SocketHandle>> listener_accept(SocketListener &listener) {
        int fd = co_await expectError(
            co_await UringOp().prep_accept(listener.fileNo(), nullptr, nullptr, 0));
        SocketHandle sock(fd); 
        co_return sock;  // 返回套接字句柄
    }

    // 带取消标记的异步接受连接的函数
    Task<Expected<SocketHandle>> listener_accept(SocketListener &listener,
                                                 CancelToken cancel) {
        int fd = co_await expectError(
            co_await UringOp()
                .prep_accept(listener.fileNo(), nullptr, nullptr, 0)
                .cancelGuard(cancel));  // 准备接受连接并添加取消保护
        SocketHandle sock(fd);
        co_return sock;  // 返回套接字句柄
    }

    // 接受连接并获取对端地址的函数
    Task<Expected<SocketHandle>> listener_accept(SocketListener &listener,
                                                 SocketAddress &peerAddr) {
        int fd = co_await expectError(co_await UringOp().prep_accept(
            listener.fileNo(), reinterpret_cast<struct sockaddr *>(&peerAddr.mAddr),
            &peerAddr.mAddrLen, 0))  // 准备接受连接并获取对端地址
#if ZH_ASYNC_INVALFIX
            .or_else(std::errc::invalid_argument, [&] { return expectError(accept4(
                             listener.fileNo(), reinterpret_cast<struct sockaddr *>(&peerAddr.mAddr),
                             &peerAddr.mAddrLen, 0)); })  
#endif
            ;
        SocketHandle sock(fd);  
        co_return sock; 
    }

    // 带取消标记的接受连接函数
    Task<Expected<SocketHandle>> listener_accept(SocketListener &listener,
                                                 SocketAddress &peerAddr,
                                                 CancelToken cancel) {
        int fd = co_await expectError(
            co_await UringOp()
                .prep_accept(listener.fileNo(),
                             reinterpret_cast<struct sockaddr *>(&peerAddr.mAddr),
                             &peerAddr.mAddrLen, 0)
                .cancelGuard(cancel))  // 准备接受连接并添加取消保护
#if ZH_ASYNC_INVALFIX
            .or_else(std::errc::invalid_argument, [&] { return expectError(accept4(
                             listener.fileNo(), reinterpret_cast<struct sockaddr *>(&peerAddr.mAddr),
                             &peerAddr.mAddrLen, 0)); })  
#endif
            ;
        SocketHandle sock(fd);  
        co_return sock;  // 返回套接字句柄
    }

    // 异步写入数据到套接字的函数
    Task<Expected<std::size_t>> socket_write(SocketHandle &sock,
                                             std::span<char const> buf) {
        co_return static_cast<std::size_t>(co_await expectError(
            co_await UringOp().prep_send(sock.fileNo(), buf, 0))  // 准备发送数据
#if ZH_ASYNC_INVALFIX
            .or_else(std::errc::invalid_argument, [&] { return expectError(send(sock.fileNo(), buf.data(), buf.size(), 0)); })  // 如果存在无效参数，调用传统发送函数
#endif
        );
    }

    // 使用零拷贝异步写入数据到套接字的函数
    Task<Expected<std::size_t>> socket_write_zc(SocketHandle &sock,
                                                std::span<char const> buf) {
        co_return static_cast<std::size_t>(co_await expectError(
            co_await UringOp().prep_send_zc(sock.fileNo(), buf, 0, 0))  // 准备发送数据使用零拷贝
#if ZH_ASYNC_INVALFIX
            .or_else(std::errc::invalid_argument, [&] { return expectError(send(sock.fileNo(), buf.data(), buf.size(), 0)); })  // 如果存在无效参数，调用传统发送函数
#endif
                                           );
    }

    // 异步从套接字读取数据的函数
    Task<Expected<std::size_t>> socket_read(SocketHandle &sock,
                                            std::span<char> buf) {
        co_return static_cast<std::size_t>(co_await expectError(
            co_await UringOp().prep_recv(sock.fileNo(), buf, 0))  // 准备接收数据
#if ZH_ASYNC_INVALFIX
            .or_else(std::errc::invalid_argument, [&] { return expectError(recv(sock.fileNo(), buf.data(), buf.size(), 0)); })  // 如果存在无效参数，调用传统接收函数
#endif
            );
    }

    // 带取消标记的异步写入数据的函数
    Task<Expected<std::size_t>> socket_write(SocketHandle &sock,
                                             std::span<char const> buf,
                                             CancelToken cancel) {
        co_return static_cast<std::size_t>(
            co_await expectError(co_await UringOp()
                                     .prep_send(sock.fileNo(), buf, 0)
                                     .cancelGuard(cancel))  // 准备发送数据并添加取消保护
#if ZH_ASYNC_INVALFIX
            .or_else(std::errc::invalid_argument, [&] { return expectError(send(sock.fileNo(), buf.data(), buf.size(), 0)); })  // 如果存在无效参数，调用传统发送函数
#endif
        );
    }

    // 带取消标记和零拷贝的异步写入数据函数
    Task<Expected<std::size_t>> socket_write_zc(SocketHandle &sock,
                                                std::span<char const> buf,
                                                CancelToken cancel) {
        co_return static_cast<std::size_t>(
            co_await expectError(co_await UringOp()
                                     .prep_send_zc(sock.fileNo(), buf, 0, 0)
                                     .cancelGuard(cancel))  // 准备发送数据使用零拷贝并添加取消保护
#if ZH_ASYNC_INVALFIX
            .or_else(std::errc::invalid_argument, [&] { return expectError(send(sock.fileNo(), buf.data(), buf.size(), 0)); })  // 如果存在无效参数，调用传统发送函数
#endif
        );
    }

    // 带取消标记的异步读取数据的函数
    Task<Expected<std::size_t>> socket_read(SocketHandle &sock, std::span<char> buf,
                                            CancelToken cancel) {
        co_return static_cast<std::size_t>(
            co_await expectError(co_await UringOp()
                                     .prep_recv(sock.fileNo(), buf, 0)
                                     .cancelGuard(cancel))  // 准备接收数据并添加取消保护
#if ZH_ASYNC_INVALFIX
            .or_else(std::errc::invalid_argument, [&] { return expectError(recv(sock.fileNo(), buf.data(), buf.size(), 0)); })  // 如果存在无效参数，调用传统接收函数
#endif
            );
    }

    // 带超时的异步写入数据的函数
    Task<Expected<std::size_t>>
    socket_write(SocketHandle &sock, std::span<char const> buf,
                 std::chrono::steady_clock::duration timeout) {
        auto ts = durationToKernelTimespec(timeout);  // 转换超时时间格式
        co_return static_cast<std::size_t>(
            co_await expectError(co_await UringOp::link_ops(
                UringOp().prep_send(sock.fileNo(), buf, 0),  // 准备发送数据
                UringOp().prep_link_timeout(&ts, IORING_TIMEOUT_BOOTTIME)))  // 设置超时
#if ZH_ASYNC_INVALFIX
            .or_else(std::errc::invalid_argument, [&] { return expectError(send(sock.fileNo(), buf.data(), buf.size(), 0)); })  // 如果存在无效参数，调用传统发送函数
#endif
            );
    }

    // 带超时的异步读取数据的函数
    Task<Expected<std::size_t>>
    socket_read(SocketHandle &sock, std::span<char> buf,
                std::chrono::steady_clock::duration timeout) {
        auto ts = durationToKernelTimespec(timeout);  // 转换超时时间格式
        co_return static_cast<std::size_t>(
            co_await expectError(co_await UringOp::link_ops(
                UringOp().prep_recv(sock.fileNo(), buf, 0),  // 准备接收数据
                UringOp().prep_link_timeout(&ts, IORING_TIMEOUT_BOOTTIME)))  // 设置超时
#if ZH_ASYNC_INVALFIX
            .or_else(std::errc::invalid_argument, [&] { return expectError(recv(sock.fileNo(), buf.data(), buf.size(), 0)); })  // 如果存在无效参数，调用传统接收函数
#endif
            );
    }

    // 带超时和取消标记的异步写入数据的函数
    Task<Expected<std::size_t>>
    socket_write(SocketHandle &sock, std::span<char const> buf,
                 std::chrono::steady_clock::duration timeout, CancelToken cancel) {
        auto ts = durationToKernelTimespec(timeout);  // 转换超时时间格式
        co_return static_cast<std::size_t>(co_await expectError(
            co_await UringOp::link_ops(
                UringOp().prep_send(sock.fileNo(), buf, 0),  // 准备发送数据
                UringOp().prep_link_timeout(&ts, IORING_TIMEOUT_BOOTTIME))  // 设置超时
                .cancelGuard(cancel))  // 添加取消保护
#if ZH_ASYNC_INVALFIX
            .or_else(std::errc::invalid_argument, [&] { return expectError(send(sock.fileNo(), buf.data(), buf.size(), 0)); })  // 如果存在无效参数，调用传统发送函数
#endif
        );
    }

    // 带超时和取消标记的异步读取数据的函数
    Task<Expected<std::size_t>>
    socket_read(SocketHandle &sock, std::span<char> buf,
                std::chrono::steady_clock::duration timeout, CancelToken cancel) {
        auto ts = durationToKernelTimespec(timeout);  // 转换超时时间格式
        co_return static_cast<std::size_t>(co_await expectError(
            co_await UringOp::link_ops(
                UringOp().prep_recv(sock.fileNo(), buf, 0),  // 准备接收数据
                UringOp().prep_link_timeout(&ts, IORING_TIMEOUT_BOOTTIME))  // 设置超时
                .cancelGuard(cancel))  // 添加取消保护
#if ZH_ASYNC_INVALFIX
            .or_else(std::errc::invalid_argument, [&] { return expectError(recv(sock.fileNo(), buf.data(), buf.size(), 0)); })  // 如果存在无效参数，调用传统接收函数
#endif
            );
    }

    // 异步关闭套接字的函数
    Task<Expected<>> socket_shutdown(SocketHandle &sock, int how) {
        co_return expectError(co_await UringOp().prep_shutdown(sock.fileNo(), how));  // 准备关闭操作
    }
}