#pragma once

#include <unordered_map> 
#include <map>
#include <mutex>
#include <list>

#include "ArcCacheNode.h"

namespace Cache
{

template<typename Key, typename Value>
class ArcLfuPart
{
public:
    using ArcNodeType = ArcNode<Key, Value>;
    using NodePtr = std::shared_ptr<ArcNodeType>;
    using NodeMap = std::unordered_map<Key, NodePtr>;
    using FreqMap = std::map<size_t, std::list<NodePtr>>;

    explicit ArcLfuPart(size_t capacity, size_t transformThreshold)
        : capacity_(capacity)
        , ghostCapacity_(capacity)
        , transformThreshold_(transformThreshold)
        , minFreq_(0)
    {
        initializeLists();
    }

    // 将 node 放入 mainCache
    bool put(Key key, Value value)
    {
        if (capacity_ == 0) return false;
        
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = mainCache_.find(key);
        if (it != mainCache_.end())
        {
            return updateExistingNode(it->second, value);
        }
        return addNewNodeToMainCache (key, value);
    }

    // 从 mainCache 中 get(Key) 将其值赋给 value 
    // 这里不同于 lruPart 增加 freq 即可
    bool get(Key key, Value& value)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = mainCache_.find(key);
        if (it != mainCache_.end())
        {
            updateNodeFrequency(it->second);
            value = it->second->getValue();
            return true;
        }
        return false;
    }

    // 检查 mainCache_ 中是否有这个 key
    bool contain(Key key)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        return mainCache_.find(key) != mainCache_.end();
    }

    // 检查 ghost 中是否有这个 key 
    // 配合 ArcCache 的put get 函数使用 不需要担心 节点的转移问题 
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

    // 增加 mainCache 的 capacity_
    void increaseCapacity() 
    { 
        std::lock_guard<std::mutex> lock(mutex_);
        ++capacity_; 
    }

    // 减少 mainCache 的 capacity_ 
    // 如果 mainCache_.size() == capacity_ 那么驱逐 mainCache 中频率最少的节点
    bool decreaseCapacity()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (capacity_ <= 0) return false;
        if (mainCache_.size() == capacity_) evictLeastFrequent();
        --capacity_;
        return true;
    }

private:
    void initializeLists();

    bool updateExistingNode(NodePtr node,const Value& value);
    
    bool addNewNodeToMainCache (const Key& key, const Value value);
    void addToGhost(NodePtr node);

    void updateNodeFrequency(NodePtr node);
    
    void evictLeastFrequent();
    void removeNode(NodePtr node);
    void removeOldestElementInGhost();


private:
    size_t      capacity_;
    size_t      ghostCapacity_;
    size_t      transformThreshold_; // 向 lru 转变的阈值
    size_t      minFreq_; // 进入 lfu 之后的最低频率
    std::mutex  mutex_;

    NodeMap     mainCache_;
    NodeMap     ghostCache_;
    FreqMap     freqMap_;

    NodePtr     ghostHead_;
    NodePtr     ghostTail_;
};

// 初始化列表
template<typename Key, typename Value>
void ArcLfuPart<Key, Value>::initializeLists()
{
    ghostHead_ = std::make_shared<ArcNodeType>();
    ghostTail_ = std::make_shared<ArcNodeType>();

    ghostHead_->next_ = ghostTail_;
    ghostTail_->prev_ = ghostHead_;
}

// 更新 node 的 freq 并且将其加入新的 freqlist
template<typename Key, typename Value>
void ArcLfuPart<Key, Value>::updateNodeFrequency(NodePtr node)
{
    size_t oldFreq = node->getAccessCount();
    node->incrementAccessCount();
    size_t newFreq = node->getAccessCount();

    auto& oldList = freqMap_[oldFreq];
    oldList.remove(node);
    if (oldList.empty())
    {
        freqMap_.erase(oldFreq);
        if (oldFreq == minFreq_)
        {
            minFreq_ = newFreq;
        }
    }

    if (freqMap_.find(newFreq) == freqMap_.end())
    {
        freqMap_[newFreq] = std::list<NodePtr>();
    } 

    freqMap_[newFreq].push_back(node);
}

/** 
    更新已经存在的节点的值 
    如果更新过后节点所在的旧列表为空 将其除掉
*/ 
template<typename Key, typename Value>
bool ArcLfuPart<Key, Value>::updateExistingNode(NodePtr node, const Value& value)
{
    node->setValue(value);
    updateNodeFrequency(node);
    return true;
}

// 向 mainCache 中添加 node
template<typename Key, typename Value>
bool ArcLfuPart<Key, Value>::addNewNodeToMainCache(const Key& key, const Value value)
{
    if (mainCache_.size() >= capacity_)
    {
        evictLeastFrequent();
    }

    auto newNode = std::make_shared<ArcNodeType>(key, value);
    mainCache_[key] = newNode;

    // 因为新加入的节点的 accessCount 全部被重置为 1
    if (freqMap_.find(1) == freqMap_.end())
    {
        freqMap_[1] = std::list<NodePtr>();
    }
    freqMap_[1].push_back(newNode);
    minFreq_ = 1;

    return true;
}


// 驱逐 mianCache中最少频率的元素 加入 ghostCache 中
// 同时更新 ghostCache 并驱逐元素
template<typename Key, typename Value>
void ArcLfuPart<Key, Value>::evictLeastFrequent()
{
    if (freqMap_.empty()) return;

    // 最少的频率列表
    auto& minFreqList = freqMap_[minFreq_];
    if (minFreqList.empty()) return;

    // 注意到之前加入列表的方法都是 push_back() 因此这里最前面的是最久没有访问的
    NodePtr leastNode = minFreqList.front();
    minFreqList.pop_front();

    if (minFreqList.empty())
    {
        freqMap_.erase(minFreq_);
        if (!freqMap_.empty())
        {
            minFreq_ = freqMap_.begin()->first;
        }
    }

    if (ghostCache_.size() >= ghostCapacity_)
    {
        removeOldestElementInGhost();
    }
    addToGhost(leastNode);

    mainCache_.erase(leastNode->getKey());
}

// 移除节点
template<typename Key, typename Value>
void ArcLfuPart<Key, Value>::removeNode(NodePtr node) 
{
    if (!node->prev_.expired() && node->next_)
    {
        auto prev = node->prev_.lock();
        prev->next_ = node->next_;
        node->next_->prev_ = prev;

        node->next_.reset();
        node->prev_.reset();
    }
}

// 将节点加入到 ghost 中
template<typename Key, typename Value>
void ArcLfuPart<Key, Value>::addToGhost(NodePtr node)
{
    if (ghostCache_.size() >= ghostCapacity_)
    {
        removeOldestElementInGhost();
    }

    ghostCache_[node->getKey()] = node;

    node->next_ = ghostHead_->next_;
    node->prev_ = ghostHead_;

    ghostHead_->next_->prev_ = node;
    ghostHead_->next_ = node;
}

// 去除 ghost 中时间最老的元素
template<typename Key, typename Value>
void ArcLfuPart<Key, Value>::removeOldestElementInGhost()
{   
    // 调用时 ghostCache_.size() >= ghostCapacity_
    // 所以这里 empty 应该是不应该发生的
    if (ghostCache_.empty()) 
    {
        LOG_ERROR_CACHE("removeOldestElementInGhost failed: ghostCache_ is empty()");
    }

    // 注意到上面这里我们是在头节点加入的 所以这里最老的应该是尾节点
    NodePtr oldestGhost = ghostTail_->prev_.lock();
    if (oldestGhost && !oldestGhost->prev_.expired()) {
        auto prev = oldestGhost->prev_.lock();
        prev->next_ = ghostTail_;
        ghostTail_->prev_ = prev;
        ghostCache_.erase(oldestGhost->getKey());

        oldestGhost->next_.reset();
        oldestGhost->prev_.reset();
    }
    
}

} // namespace Cache