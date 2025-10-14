#pragma once

#include <climits>
#include <mutex>       
#include <optional>    
#include <unordered_map> 
#include <climits>     
#include <stdexcept>   
#include <memory>

#include "CachePolicy.h"
#include "Log.h"

namespace Cache
{
template<typename Key, typename Value> class LfuCache;

template<typename Key, typename Value>
class FreqList
{
private:
    struct Node
    {
        int freq;
        Key key;
        Value value;
        std::weak_ptr<Node> prev;
        std::shared_ptr<Node> next;

        Node()
            : freq(1), next(nullptr) {}
        Node(Key key, Value value)
            : freq(1), key(key), value(value), prev(), next(nullptr) {}
    };

    int freq_;
    std::shared_ptr<Node> dummyHead_;
    std::shared_ptr<Node> dummyTail_;

public:
    using Node = typename FreqList<Key, Value>::Node;
    using NodePtr = std::shared_ptr<Node>;
    using NodeMap = std::unordered_map<Key, NodePtr>;
    
    // 对于单参数构造函数，尽量使用 explicit，除非明确需要隐式转换的场景（如智能指针的设计）。
    explicit FreqList(int n)
        : freq_(n)
    {
        dummyHead_ = std::make_shared<Node>();
        dummyTail_ = std::make_shared<Node>();
        dummyHead_->next = dummyTail_;
        dummyTail_->prev = dummyHead_;
    }

    bool isEmpty() const
    {
        return dummyHead_->next == dummyTail_;
    }
    
    // 将节点添至尾部
    void addNode(NodePtr node) 
    {
        if (!node || !dummyHead_ || !dummyTail_) 
        {
            LOG_ERROR_CACHE("addNode failed: invalid node or empty list");
            return;
        }

        node->prev = dummyTail_->prev;
        node->next = dummyTail_;
        dummyTail_->prev.lock()->next = node;
        dummyTail_->prev = node;
    }

    void removeNode(NodePtr node)
    {
        if (!node || !dummyHead_ || !dummyTail_) 
        {
            LOG_ERROR_CACHE("removeNode failed: invalid node or empty list");
            return;
        }

        if (node == dummyHead_ || node == dummyTail_)
        {
            LOG_ERROR_CACHE("removeNode failed: cannot remove sentinal node");
            return;
        }

        auto prevNode = node->prev.lock();
        if (!prevNode)
        {
            LOG_ERROR_CACHE("removeNode failed: node's prev is expired");
            return;
        }

        prevNode->next = node->next;
        node->next->prev = node->prev;

        node->next.reset();
        node->prev.reset();
    }

    NodePtr getFirstNode() const 
    {
        return dummyHead_->next;
    }
    
    friend class LfuCache<Key, Value>;
};

template <typename Key, typename Value>
class LfuCache : public CachePolicy<Key, Value>
{
public:
    using Node = typename FreqList<Key, Value>::Node;
    using NodePtr = std::shared_ptr<Node>;
    using NodeMap = std::unordered_map<Key, NodePtr>;

    explicit LfuCache(int capacity, int maxAverageNum = 1000000) 
        : capacity_(capacity)
        , minFreq_(INT_MAX) // 这里应该是后面会用 min 函数 实时更新
        , maxAverageNum_(maxAverageNum)
        , curAverageNum_(0)
        , curTotalNum_(0)
    {
        if (capacity_ <= 0)
        {
            throw std::invalid_argument("capacity should be greater than 0");
        }

    }

    ~LfuCache() override = default;

    void put(Key key, Value value) override
    {
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = nodeMap_.find(key);

        if (it != nodeMap_.end())
        {
            it->second->value = value;
            getInternal(it->second, value);
            return;
        }

        putInternal(key, value);
    }

    bool get(Key key, Value& value) override
    {
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = nodeMap_.find(key);
        if (it != nodeMap_.end())
        {
            getInternal(it->second, value);
            return true;
        }

        return false;
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

    // 清除数据 但是保留缓存实例
    void purge()
    {
        // 对于锁来说 可以阻塞两个操作 读 写
        // 只要有这两个操作 就应该对相应的共享资源加锁
        // 这里清除也是写的一种操作 应该加锁
        std::lock_guard<std::mutex> lock(mutex_);

        nodeMap_.clear();
        freqToFreqList_.clear();
        curTotalNum_ = 0;
        curAverageNum_ = 0;
        minFreq_ = INT_MAX;
    }

private:
    void putInternal(Key key, Value value); // 添加缓存
    void getInternal(NodePtr node, Value& value); // 获取缓存

    void kickOut(); // 移除缓存中的过期数据

    void removeFromFreqList(NodePtr node); // 从频率列表中移除节点
    void addToFreqList(NodePtr node); // 添加到频率列表

    void addFreqNum(); // 增加平均访问等频率
    void decreaseFreqNum(int num); // 减少平均访问等频率
    void handleOverMaxAverageNum(); // 处理当前平均访问频率超过上限的情况
    void updateMinFreq();

private:
    int                                                             capacity_; // 缓存容量
    int                                                             minFreq_; // 最小访问频次 (用于找到最小访问频次结点)
    int                                                             maxAverageNum_; // 最大平均访问频次
    int                                                             curAverageNum_; // 当前平均访问频次
    int                                                             curTotalNum_; // 当前访问所有缓存次数总数 
    mutable std::mutex                                              mutex_; // 互斥锁
    NodeMap                                                         nodeMap_; // key 到 缓存节点的映射
    std::unordered_map<int, std::unique_ptr<FreqList<Key, Value>>>  freqToFreqList_;// 访问频次到该频次链表的映射
};

// get list 内部的节点 更新访问次数
template<typename Key, typename Value>
void LfuCache<Key, Value>::getInternal(NodePtr node, Value& value)
{
    if (!node)
    {
        LOG_ERROR_CACHE("getInternal failed: invalid node");
        return;
    }

    // 这里将读取的操作放在一个地方 
    // 更新的操作放在一个地方
    value = node->value;
    // 调用 getInternal 的地方也保证了 node 一定是有效的
    removeFromFreqList(node);
    ++node->freq;
    // 这里注意要先 ++freq 然后再更新 list
    addToFreqList(node);
    addFreqNum();
    
    // 如果当前node的访问频次如果等于minFreq+1，并且其前驱链表为空，则说明
    // freqToFreqList_[node->freq - 1]链表因node的迁移已经空了，需要更新最小访问频次
    if (node->freq - 1 == minFreq_ && freqToFreqList_[minFreq_]->isEmpty())
    {
        ++minFreq_;
    }

}

// 将节点放入对应频率的 list
template<typename Key, typename Value>
void LfuCache<Key, Value>::putInternal(Key key, Value value)
{
    // 缓存满了 这个时候需要清除一些数据
    if (nodeMap_.size() == capacity_)
    {
        kickOut();
    }

    NodePtr node = std::make_shared<Node>(key, value);
    nodeMap_[key] = node;
    addToFreqList(node);
    addFreqNum(); // 增加总访问次数 如果总访问次数超出上线 然后淘汰非热点数据
    minFreq_ = std::min(minFreq_, 1);
}

// 移除缓存中的过期数据 并且更新 FreqNum
template<typename Key, typename Value>
void LfuCache<Key, Value>::kickOut() 
{
    auto it = freqToFreqList_.find(minFreq_);
    if (it == freqToFreqList_.end() || it->second->isEmpty())
    {
        throw std::invalid_argument("kickOut failed: freqToFreqList_[minFreq_] is empty"); // 或者 throw
    }
    NodePtr node = it->second->getFirstNode();

    // 检查节点是否是哨兵节点
    if (node == it->second->dummyTail_ || !node)
    {
        LOG_ERROR_CACHE("kickOut failed: no vaild node to remove");
        return;
    }

    removeFromFreqList(node);
    nodeMap_.erase(node->key);
    decreaseFreqNum(node->freq);
}

// 将节点移出 list
template<typename Key, typename Value>
void LfuCache<Key, Value>::removeFromFreqList(NodePtr node)
{
    if (!node)
    {
        LOG_ERROR_CACHE("removeNodeFromFreqList failed: invalid node");
        return;
    } 

    auto freq = node->freq;
    freqToFreqList_[freq]->removeNode(node);
}

// 在移除节点时 减少总访问次数
template<typename Key, typename Value>
void LfuCache<Key, Value>::decreaseFreqNum(int num)
{
    if (num <= 0) 
    {
        LOG_ERROR_CACHE("decreaseFreqNum failed: decrease num should be greater than 0");
        return;
    }

    curTotalNum_ -= num;
    if (nodeMap_.size() == 0)
    {
        curAverageNum_ = 0;
    } 
    else
    {
        curAverageNum_ = curTotalNum_ / nodeMap_.size();
    }
}

// 增加总访问次数 并且更新平均访问次数 处理超出平均最大访问次数的情况
template<typename Key, typename Value>
void LfuCache<Key, Value>::addFreqNum()
{
    // 每次访问恰好是 1
    ++curTotalNum_;
    if (nodeMap_.empty())
    {
        curAverageNum_ = 0;
    }
    else
    {
        // 这里先转换在变成除法 （将除法结果进行转换是没有意义的）
        // 这里也可以写 * 1.0 但是这种显示的转换可读性更好
        curAverageNum_ = curTotalNum_ / nodeMap_.size();
        if (curAverageNum_ > maxAverageNum_)
        {
            handleOverMaxAverageNum();
        }
    }
}

// 处理超出平均最大访问次数的情况
template<typename Key, typename Value>
void LfuCache<Key, Value>::handleOverMaxAverageNum() 
{
    if (nodeMap_.empty())
    {
        return;
    }

    for (auto it = nodeMap_.begin(); it != nodeMap_.end(); ++it)
    {
        if (!it->second)
        {
            continue;
        }

        NodePtr node = it->second;

        removeFromFreqList(node);

        // 遍历节点 减去最大平均访问次数的 1/2 将之前的热点数据也同步更新 防止旧热点数据污染缓存
        node->freq -= maxAverageNum_ / 2;
        
        if (node->freq < 1)
        {
            node->freq = 1;
        }

        addToFreqList(node);
    }

    updateMinFreq();
}

// 将节点加入对应 freq 的 list 中
template<typename Key, typename Value>
void LfuCache<Key, Value>::addToFreqList(NodePtr node)
{
    if (!node) 
    {
        LOG_ERROR_CACHE("addToFreqList failed: invalid node");
        return;
    }
    // 两处调用 addToFreqList 的地方已经保证了 node 一定是有效的

    // 不存在 如此 freq 的list
    auto freq = node->freq;
    if (freqToFreqList_.find(node->freq) == freqToFreqList_.end())
    {
        // 不存在则创建
        freqToFreqList_[freq] = std::make_unique<FreqList<Key, Value>>(freq);
    }

    freqToFreqList_[freq]->addNode(node);
}

template<typename Key, typename Value>
void LfuCache<Key, Value>::updateMinFreq()
{
    // NOTE: caller must hold mutex_ before calling this function.
    // 本函数假定调用方已上锁（public API 如 put/get 已加锁），
    // 为避免在同一线程对非递归 mutex 重复加锁导致死锁，这里不再内部加锁。
    minFreq_ = INT_MAX;
    for (const auto& pair : freqToFreqList_)
    {
        if (pair.second && !pair.second->isEmpty())
        {
            minFreq_ = std::min(minFreq_, pair.first);
        }
    }

    // 当前缓存中没有任何元素，或者
    // 所有 FreqList 都为空；
    if (minFreq_ == INT_MAX || minFreq_ < 1)
    {
        minFreq_ = 1;
    }
}

template<typename Key, typename Value>
class HashLfuCache 
{
public:
    HashLfuCache(size_t capacity, size_t sliceNum)
        : capacity_(capacity)
        , sliceNum_(sliceNum)
    {
        if (sliceNum_ <= 0)
        {
            throw std::invalid_argument("sliceNum must be bigger than 0");
        }

        size_t sliceSize = std::ceil(static_cast<double>(capacity_) / sliceNum_);
        for (size_t i = 0; i < sliceNum_; ++i)
        {
            LfuSliceCaches_.emplace_back(std::make_unique<LfuCache<Key, Value>>>(sliceSize));
        }
    }

    void put(Key key, Value value)
    {
        size_t sliceIndex = Hash(key) % sliceNum_;
        LfuSliceCaches_[sliceIndex]->put(key, value);
    }

    bool get(Key key, Value& value)
    {
        size_t sliceIndex = Hash(key) % sliceNum_;
        LfuSliceCaches_[sliceIndex]->get(key, value);
        return true;
    }

    Value get(Key key)
    {
        Value value{};
        get(key, value);
        return value;
    }

    void purge()
    {
        for (auto& LfuSliceCache : LfuSliceCaches_)
        {
            LfuSliceCache->purge();
        }
        
    }
private:
    size_t Hash(Key key) 
    {
        std::hash<Key> Hashfunc;
        return hashFunc(key);
    }

private:
    size_t                                                  capacity_;
    int                                                     sliceNum_;
    std::vector<std::unique_ptr<LfuCache<Key, Value>>>      LfuSliceCaches_;
};

}
