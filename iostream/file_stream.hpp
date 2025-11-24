#pragma once
#include <std.hpp>
#include <awaiter/task.hpp>
#include <generic/allocator.hpp>
#include <iostream/stream_base.hpp>
#include <platform/fs.hpp>

namespace zh_async
{
    Task<Expected<OwningStream>> file_open(std::filesystem::path path,OpenMode mode);

    OwningStream file_from_handle(file_handle handle);

    Task<Expected<String>> file_read(std::filesystem::path path);

    Task<Expected<>> file_write(std::filesystem::path path,std::string_view content);

    Task<Expected<>> file_append(std::filesystem::path path,std::string_view content);
}