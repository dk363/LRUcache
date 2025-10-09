#pragma once

#include <cstring>
#include <list>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <optional>
#include <vector>
#include <cmath>

#include "CachePolicy.h"

namespace Cache
{

// 前向声明
template<typename Key, typename Value> class LruCache;

template<typename Key, typename Value> 
class LruNode 
{
private:
    Key key_;
    Value value_;
    // 因为这里一开始的时候 prev_ next_是相互指的
    // 在双向链表中最好有一边使用 weak_ptr
    std::weak_ptr<LruNode<Key, Value>> prev_; // weakptr 防止循环引用
    std::shared_ptr<LruNode<Key, Value>> next_;
public:
    LruNode(Key key, Value value)
        : key_(key)
        , value_(value)
    {}

    // 提供必要的访问器
    Key getKey() const { return key_; }
    Value getValue() const { return value_; }
    void setValue(const Value& another_value) { value_ = another_value; }

    // 友元声明 LruCache 可以访问 LruNode 的 private 成员
    friend class LruCache<Key, Value>;
};

template<typename Key, typename Value>
class LruCache : public CachePolicy<Key, Value> 
{
public:
    using LruNodeType = LruNode<Key, Value>;
    using NodePtr = std::shared_ptr<LruNodeType>;
    using NodeMap = std::unordered_map<Key, NodePtr>;

    LruCache(int capacity) 
        : capacity_(capacity) 
    {
        if (capacity_ > 0) {
            initializeList();
        } else {
            std::invalid_argument("capacity must greater than 0");
        }
    }

    ~LruCache() override = default;

    void put(Key key, Value value) override
    {
        // 将互斥量上锁 再访问同一个缓存对象时 就会被阻塞 直到锁被释放
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = nodeMap_.find(key);
        if (it != nodeMap_.end()) {
            updateExistingNode(it->second, value);
            return;
        }

        addNewNode(key, value);
    }

    bool get(Key key, Value& value) override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = nodeMap_.find(key);
        if (it != nodeMap_.end()) {
            moveToMostRecent(it->second);
            // 这里 it->second 是 NodePtr 所以不可以更改
            // 只是需要输出 node->value 即可
            value = it->second->getValue();
            return true;
        }
        return false;
    }

    // get key 相对应 value
    // 如果编译不通过 这里更改了
    std::optional<Value> get(Key key) override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = nodeMap_.find(key);
        if (it != nodeMap_.end()) {
            moveToMostRecent(it->second);  // 标记为最近访问
            return it->second->getValue(); // 返回节点值
        }
        return std::nullopt; // key 不存在
    }

    // 删除指定元素
    void remove(Key key)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = nodeMap_.find(key);

        if (it == nodeMap_.end()) {
            throw std::out_of_range("Key not found in LruCache");
        }

        // 从链表中移除节点
        removeNode(it->second);

        // 从哈希表中删除
        nodeMap_.erase(it);
    }

private:
    // 初始化双向链表
    void initializeList()
    {
        // 这里用自己类型默认的构造参数
        // 比如 Key 的类型是 int 那么这里就是 0
        // 如果是 string 那么这里就是 ""
        dummyHead_ = std::make_shared<LruNodeType>(Key(), Value());
        dummyTail_ = std::make_shared<LruNodeType>(Key(), Value());

        dummyHead_->next_ = dummyTail_;
        dummyTail_->prev_ = dummyHead_;
    }

    // 更新已经存在的节点的值 并标记为最近访问 也就是将其移动到尾部
    void updateExistingNode(NodePtr node, const Value& value)
    {
        node->setValue(value);
        moveToMostRecent(node);
    }

    // 将节点移动到最新的位置
    void moveToMostRecent(NodePtr node) 
    {
        removeNode(node);
        insertNode(node);
    }

    // 添加一个新节点
    void addNewNode(Key key, Value value) 
    {
        // 如果缓存已满 将最久未访问的节点驱逐
        if (nodeMap_.size() == capacity_) 
        {
            evictLeastRecent();
        }

        NodePtr newNode = std::make_shared<LruNodeType>(key, value);
        insertNode(newNode);
        nodeMap_[key] = newNode;
    }
    
    // 驱逐最少访问的节点
    void evictLeastRecent() 
    {
        NodePtr leastRecent = dummyHead_->next_;
        removeNode(leastRecent);
        // 不要忘记移除 哈希表中的
        nodeMap_.erase(leastRecent->getKey());
    }


    // 移除节点
    void removeNode(NodePtr node) 
    {
        // prev_ 是弱引用（weak_ptr），不能直接用 prev_->next_。
        // 所以必须先 .lock() 得到一个 shared_ptr 再解引用
        if (!node->prev_.expired() && node->next_)
        {
            auto prev = node->prev_.lock();
            prev->next_ = node->next_;
            node->next_->prev_ = prev;
            node->next_.reset();
            node->prev_ .reset();
        }
    }

    // 将节点插入到链表尾部（最近访问位置）
    void insertNode(NodePtr node) 
    {
        node->next_ = dummyTail_;
        node->prev_ = dummyTail_->prev_;
        dummyTail_->prev_.lock()->next_ = node;
        dummyTail_->prev_ = node;
    }

private:
    int         capacity_;
    NodeMap     nodeMap_;
    std::mutex  mutex_;
    NodePtr     dummyHead_;
    NodePtr     dummyTail_;
};

// 如果有偶发性的批量操作 那么会将之前的 热点数据 挤出缓存之中
// 因此 这里进行改进 将进入缓存队列的评判标准从 一次 变为 k次 
template<typename Key, typename Value>
class LruKCache : public LruCache<Key, Value>
{
public:
    LruKCache(int capacity, int historyListCapacity, int k) 
        : LruCache<Key, Value>(capacity)
        , historyList_(std::make_unique<LruCache<Key, size_t>>(historyListCapacity))
        , k_(k)
    {
        if (k_ <= 0) {
            throw std::invalid_argument("k must greater than 0");
        }
    }

    std::optional<Value> get(Key key)
    {
        // 默认构造函数
        // 改为 Value value; 也可以
        // get(key, value) 这里本来就是取 key 对应的 value 的值
        // value 的值是无所谓的
        Value value{};
        bool inMainCache = LruCache<Key, Value>::get(key, value);
        
        if (inMainCache)
        {
            return value;
        }

        // 因为这里 value 是 private 的 所以这里不可以 &
        // 因为 historyList_ 维护的是一个二级缓存
        // 二级缓存也是有大小的的 所以这里 不可以直接更改 value 的值
        // 而需要 put 更新
        size_t historyCount = 0;
        if(auto val = historyList_->get(key)) historyCount = *val;
        ++historyCount;
        historyList_->put(key, historyCount);


        auto itValue = historyValueMap_.find(key);
        if (itValue == historyValueMap_.end())
        {
            return std::nullopt;
        }

        if (historyCount >= k_)
        {
            Value storedValue = itValue->second;
            moveToLruCache(key, storedValue);
            return storedValue;
        }

        return itValue->second;
    }

    bool get(Key key, Value& value) override
    {
        if (auto opt_val = this->get(key)) {
            value = *opt_val;
            return true;
        }
        return false;
    }


    void put(Key key, Value value)
    {
        // 这个调用了默认的构造函数 更安全
        Value existingValue{};
        bool inMainCache = LruCache<Key, Value>::get(key, existingValue);

        if (inMainCache)
        {
            LruCache<Key, Value>::put(key, value);
            return;
        }

        size_t historyCount = 0;
        if(auto val = historyList_->get(key)) historyCount = *val;
        ++historyCount;
        historyList_->put(key, historyCount);

        historyValueMap_[key] = value;
        
        if (historyCount >= k_) 
        {
            moveToLruCache(key, value);
        }
    }

private:
    void moveToLruCache(Key key, Value value) 
    {
        historyList_->remove(key);
        historyValueMap_.erase(key);
        LruCache<Key, Value>::put(key, value);
    }
private:
/**
    LRU-K算法有两个队列，一个是缓存队列，一个是数据访问历史队列。
    当访问一个数据时，首先先在访问历史队列中累加访问次数，
    当历史访问记录超过K次后，才将数据缓存至缓存队列，
    从而避免缓存队列被污染。
    这个算法有一点像是 cpu 中的二级缓存 historyList_ 实质上是保存的另一个 LruCache
*/
    int                                     k_; // 进入缓存队列的次数
    // unique_ptr 独占资源所有权 不可复制
    std::unique_ptr<LruCache<Key, size_t>>  historyList_; // 访问数据历史记录
    std::unordered_map<Key, Value>          historyValueMap_; // 存储没有达到 k 次的数据值

};

/**
    临界区：多个线程可能同时访问（读写）同一份共享资源的那段代码。
    因为在Lru中有加锁解锁的操作，所以在高并发的环境中 大量的时间消耗在等待的过程中
    而且这还是在LruK中没有锁操作的情况下 如果LruK中也加上锁的操作 那么还会增加时间
    而 HashLruCache 的做法是：

    1. 把全量数据分成 N 份（称为分片，或 shard）；
    2. 每个分片都有自己的一份 LruCache 和独立的 mutex；
    3. 用哈希函数决定某个 key 属于哪个分片。
*/

template<typename Key, typename Value>
class HashLruCaches
{
public:
    HashLruCaches(size_t capacity, size_t sliceNum)
        : capacity_(capacity)
        , sliceNum_(sliceNum)
    {
        if (sliceNum_ <= 0) {
            throw std::invalid_argument("sliceNum must be greater than 0");
        }
        size_t sliceSize = std::ceil(static_cast<double>(capacity_) / sliceNum_);
        for (size_t i = 0; i < sliceNum_; ++i) {
            lruSliceCaches_.emplace_back(std::make_unique<LruCache<Key, Value>>(sliceSize));
        }
    }


    void put(Key key, Value value)
    {
        size_t sliceIndex = Hash(key) % sliceNum_;
        lruSliceCaches_[sliceIndex]->put(key, value);
    }

    bool get(Key key, Value& value) 
    {
        size_t sliceIndex = Hash(key) % sliceNum_;
        return lruSliceCaches_[sliceIndex]->get(key, value);
    }

    Value get(Key key)
    {
        Value value{};
        get(key, value);
        return value;
    }


private:
    size_t Hash(Key key) 
    {
        std::hash<Key> hashFunc;
        return hashFunc(key);
    }

private:
    size_t                                              capacity_;
    int                                                 sliceNum_; // 切片的数量
    // 为了实现 O(1) 的访问速度 那么我们有两种选择 map 和 vactor 而且 sliceNum 是固定的
    // 所以这里直接可以用 vector
    std::vector<std::unique_ptr<LruCache<Key, Value>>>  lruSliceCaches_;
};

} // namespace Cache