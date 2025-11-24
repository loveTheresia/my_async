#include <std.hpp>
#include <generic/generic_io.hpp>
#include <generic/io_context.hpp>
#include <generic/io_context_mt.hpp>
#include <platform/platform_io.hpp>
#include <utils/cacheline.hpp>

namespace zh_async
{
    IOContextMT::IOContextMT()
    {
        if(IOContextMT::instance)[[unlikely]]
        throw std::logic_error("each process may contain only one IOContextMT");

        IOContextMT::instance = this;
    }

    IOContextMT::~IOContextMT(){
        IOContextMT::instance = nullptr;
    }

    void IOContextMT::run(std::size_t numWorkers)
    {
        instance->mWorkers = std::make_unique<IOContext[]>(numWorkers);
        instance->mNumWorkers = numWorkers;
        for(std::size_t i = 0; i < instance->mNumWorkers; i++)
            instance->mWorkers[i].run();
    }

    IOContextMT *IOContextMT::instance;
} //namespace zh_async