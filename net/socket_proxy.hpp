#pragma once
#include <std.hpp>
#include <awaiter/task.hpp>
#include <platform/socket.hpp>
#include <utils/expected.hpp>

namespace zh_async
{
    Task<Expected<SocketHandle>>
    socket_proxy_connect(char const *host,int port,
                        std::string_view proxy,std::chrono::steady_clock::duration timeout);
}