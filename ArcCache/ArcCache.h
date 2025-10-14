#pragma once

#include <memory>

#include "../CachePolicy.h"
#include "ArcLruPart.h"
#include "ArcLfuPart.h"

namespace Cache
{

template<typename Key, typename Value>
class ArcCache : public CachePolicy<Key, Value>
{
public:
    explicit ArcCache(size_t capacity, size_t transformThreshold)
        : capacity_(capacity)
        , transformThreshold_(transformThreshold)
        , lruPart_(std::make_unique<ArcLruPart<Key, Value>>(capacity, transformThreshold))
        , lfuPart_(std::make_unique<ArcLfuPart<Key, Value>>(capacity, transformThreshold))
    {}

    ~ArcCache override = default;

    void put(Key key, Value value) override
    {
        checkGhostCaches(key);
        
        bool inLfu = lfuPart_->contain(key);
        if(inLfu)
        {
            lfu->put(key, value);
        }
        else
        {
            // 改进热点数据访问测试 和 工作负载剧烈拜变化测试
            lruPart_->put(key, value);
        }       
    }

    bool get(Key key, Value& value) override
    {
        checkGhost(key);

        bool shouldTransform = false;
        if (lruPart_->get(key, value, shouldTransform))
        {
            if (shouldTransform)
            {
                // 改进循环扫描测试部分
                lruPart_->remove(key);
                // 这里是不是应该加上从lrupart移除的操作
                lfuPart_->put(key, value);
            }
            return true;
        }
        return lfuPart_->get(key, value);
    }

    std::optional<Value> get(Key key) override
    {
        Value value{};
        if (get(key, value)) 
        {
            return value;
        }
        return std::nullopt;
    }

private:
    checkGhostCaches(Key key)
    {
        bool inGhost = false;
        if (lruPart_->checkGhost(key))
        {
            if (lfuPart_->decreaseCapacity())
            {
                lruPart->increaseCapacity();
            }
            inGhost = true;
        }
        else if (lfuPart_->checkGhost(key))
        {
            if (lruPart_->decreaseCapacity())
            {
                lfuPart_->increaseCapacity();
            }
            inGhost = true;
        }
        return inGhost;
    }

private:
    size_t capacity_;
    size_t transformThreshold_;
    std::unique_ptr<ArcLruPart<Key, Value>> lruPart_;
    std::unique_ptr<ArcLfuPart<Key, Value>> lfuPart_;
};

} // namespace Cache