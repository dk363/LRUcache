#pragma once

#include <unordered_map> 
#include <mutex>
#include <memory>

#include "ArcCacheNode.h"

namespace Cache
{

template<typename Key, typename Value>
class ArcLruPart
{
public:
    using ArcNodeType = ArcNode<Key, Value>;
    using NodePtr = std::shared_ptr<ArcNodeType>;
    using NodeMap = std::unordered_map<Key, NodePtr>;

    explicit ArcLruPart(size_t capacity, size_t transformThreshold)
        : capacity_(capacity)
        , ghostCapacity_(capacity)
        , transformThreshold_(transformThreshold)
    {
        initializeLists();
    }

    // 将 key value 放入 mainCache 中
    bool put(Key key, Value value)
    {
        if (capacity_ == 0) return false;

        std::lock_guard<std::mutex> lock(mutex_);

        auto it = mainCache_.find(key);
        if (it != mainCache_.end())
        {
            return updateExistingNode(it->second, value);
        }
        return addNewNode(key, value);
    }

    // 在mianCache 中查找 并且检查是否需要 在lru和lfu之间转变
    bool get(Key key, Value& value, bool& shouldTransform)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = mainCache_.find(key);
        if (it != mainCache_.end())
        {
            // 在更新访问次数的同时 查看是否需要将lru中的数据缓存到lfu中
            shouldTransform = updateNodeAccess(it->second);
            value = it->second->getValue();
            return true;
        }
        return false;
    }

    // 查看数据是否在 ghost 列表中 
    // 如果在就将其移出
    // 这里通常和put get 函数一起调用 因此这里确实需要移除
    bool checkGhost(Key key)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = ghostCache_.find(key);
        if (it != ghostCache_.end())
        {
            removeNode(it->second);
            ghostCache_.erase(key);
            return true;
        }
        return false;
    }

    // 增加容量
    void increaseCapacity() 
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ++capacity_; 
    }

    // 减少容量
    bool decreaseCapacity()
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (capacity_ <= 0) return false;
        if (mainCache_.size() == capacity_) evictLeastRecent();
        --capacity_;
        return true;
    }

    // 移除key对应的node
    void remove(Key key)
    {
        auto it = mainCache_.find(key);
        if (it == mainCache_.end())
        {
            LOG_ERROR_CACHE("remove failed: there is not such key");
        }
        removeNode(it->second);
    }

private:
    void initializeLists();

    bool updateExistingNode(NodePtr node, const Value& value);
    bool addNewNode(const Key& key, const Value value);

    void evictLeastRecent();
    bool updateNodeAccess(NodePtr node);

    void moveToFront(NodePtr node);

    void removeNode(NodePtr node);
    void addToFront(NodePtr node);

    void addToGhost(NodePtr node);
    void removeOldestGhost();

    void resetAccessCount(NodePtr node);
private:
    size_t      capacity_;
    size_t      ghostCapacity_;
    size_t      transformThreshold_; // 转换阈值
    std::mutex  mutex_;
    
    NodeMap     mainCache_;
    NodeMap     ghostCache_;

    NodePtr     mainHead_;
    NodePtr     mainTail_;

    NodePtr     ghostHead_;
    NodePtr     ghostTail_;
};

// 初始化列表
template<typename Key, typename Value>
void ArcLruPart<Key, Value>::initializeLists()
{
    mainHead_ = std::make_shared<ArcNodeType>();
    mainTail_ = std::make_shared<ArcNodeType>();

    mainHead_->next_ = mainTail_;
    mainTail_->prev_ = mainHead_;

    ghostHead_ = std::make_shared<ArcNodeType>();
    ghostTail_ = std::make_shared<ArcNodeType>();

    ghostHead_->next_ = ghostTail_;
    ghostTail_->prev_ = ghostHead_;
}

// 更新已有结点的值
template<typename Key, typename Value>
bool ArcLruPart<Key, Value>::updateExistingNode(NodePtr node, const Value& value)
{
    node->setValue(value);
    moveToFront(node);
    return true;
}

// 将已有节点移至头部 最近访问
template<typename Key, typename Value>
void ArcLruPart<Key, Value>::moveToFront(NodePtr node)
{
    removeNode(node);
    addToFront(node);
}

// 移除节点
template<typename Key, typename Value>
void ArcLruPart<Key, Value>::removeNode(NodePtr node)
{
    if (!node->prev_.expired() && node->next_)
    {
        auto prev = node->prev_.lock();
        prev->next_ = node->next_;
        node->next_->prev_ = node->prev_;
        
        node->next_.reset();
        node->prev_.reset();

    }
}

// 加入一个新节点 加入到 mainCache 中
template<typename Key, typename Value>
bool ArcLruPart<Key, Value>::addNewNode(const Key& key, const Value value)
{
    if (mainCache_.size() >= capacity_)
    {
        evictLeastRecent(); // 驱逐冷数据
    }

    auto node = std::make_shared<ArcNodeType>(key, value);
    addToFront(node);
    mainCache_[key] = node;
    return true;
}

// 将节点放到最前端
template<typename Key, typename Value>
void ArcLruPart<Key, Value>::addToFront(NodePtr node)
{   
    node->next_ = mainHead_->next_;
    node->prev_ = mainHead_;

    mainHead_->next_->prev_ = node;
    mainHead_->next_ = node;
}

// 驱逐冷数据
template<typename Key, typename Value>
void ArcLruPart<Key, Value>::evictLeastRecent() 
{
    NodePtr leastRecent = mainTail_->prev_.lock();
    if (!leastRecent || leastRecent == mainHead_) return;

    removeNode(leastRecent);
    mainCache_.erase(leastRecent->getKey());

    if (ghostCache_.size() >= ghostCapacity_) 
    {
        removeOldestGhost();
    }

    addToGhost(leastRecent);

}

// 驱逐ghost 列表中的冷数据
template<typename Key, typename Value>
void ArcLruPart<Key, Value>::removeOldestGhost()
{
    NodePtr oldestGhost = ghostTail_->prev_.lock();
    if (!oldestGhost || oldestGhost == ghostHead_) return;

    removeNode(oldestGhost);
    ghostCache_.erase(oldestGhost->getKey());
}

// 将节点加入 ghost 列表
template<typename Key, typename Value>
void ArcLruPart<Key, Value>::addToGhost(NodePtr node)
{
    // 重置计数
    // TODO: 这里是为什么？
    resetAccessCount(node);

    node->prev_ = ghostHead_;
    node->next_ = ghostHead_->next_;

    ghostHead_->next_->prev_ = node;
    ghostHead_->next_ = node;

    ghostCache_[node->getKey()] = node;
}

// 更新 node 状态 并且判断是否到了可以将其加入 lfu 的阈值
template<typename Key, typename Value>
bool ArcLruPart<Key, Value>::updateNodeAccess(NodePtr node)
{
    moveToFront(node);
    node->incrementAccessCount();
    return node->getAccessCount() >= transformThreshold_;    
}

// 重置node的访问次数
template<typename Key, typename Value>
void ArcLruPart<Key, Value>::resetAccessCount(NodePtr node)
{
    node->accessCount_ = 1;
}

} // namespace Cache