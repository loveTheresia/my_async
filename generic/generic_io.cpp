#include <std.hpp>
#include <generic/generic_io.hpp>

namespace zh_async
{

GenericIOContext::GenericIOContext() = default;
GenericIOContext::~GenericIOContext() = default;

std::optional<std::chrono::steady_clock::duration>
GenericIOContext::runDuration()
{
    /*
        无限循环直至红黑树为空
    */
    while(true)
    {
        if(!mTimers.empty())
        {
            auto &promise = mTimers.front();
            std::chrono::steady_clock::time_point now = 
            std::chrono::steady_clock::now();
            if(promise.mExpires <= now) //如果计时器到期
            {
                promise.mCancelled = false; //设置取消令牌为false
                promise.erase_from_parent();//从树上删除节点
                std::coroutine_handle<TimerNode>::from_promise(promise).resume();//恢复与该计时器关联的协程，使其继续执行
                continue;
            }
            else
                return promise.mExpires - now;//如果还未到期，返回时间差
        }
        else
            return std::nullopt;
    }
}

}