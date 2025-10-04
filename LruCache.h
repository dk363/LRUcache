#pragma once

#include <cstirng>
#include <list>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <optional>

#include "CachePolicy"

namespace Cache
{

// 前向声明
template<typename Key, typename Value> LruCache;

template<typename Key, typename Value> 
class LruNode 
{
private:
    Key key_;
    Value value_;
    size_t accessCount_; // 访问次数
    // 因为这里一开始的时候 prev_ next_是相互指的
    // 在双向链表中最好有一边使用 weak_ptr
    std::weak_ptr<LruNode<Key, Value>> prev_; // weakptr 防止循环引用
    std::shared_ptr<LruNode<Key, Value>> next_;
public:
    LruNode(Key key, Value value)
        : key_(key)
        , value_(value)
        , accessCount_(1)
    {}

    // 提供必要的访问器
    Key getKey() const { return key_; }
    Value getValue() const { return value_; }
    void setValue(const Value& another_value) { value_ = another_value; }
    size_t getAccessCount() const { return accessCount_; }
    void incrementAccessCount() { ++accessCount_; }

    // 友元声明 LruCache 可以访问 LruNode 的 private 成员
    friend class LruCache<Key, Value>;
};

template<typename Key, typename Value>
class LruCache : public CachePolicy<Key, Value> 
{
public:
    using LurNodeType = LruNode<Key, Value>;
    using NodePtr = std::shared_ptr<LruNodeType>;
    using NodeMap = std::unordered_map<Key, NodePtr>;

    LruCache(int capacity) 
        : capacity_(capacity) 
    {
        if (capacity_ <= 0) {
            throw std::invalid_argument("Capacity must greater than 0");
        }
        initializeList();
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
            throw std::out_of_range("Key not found in KLruCache");
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
        dummyHead_ = std::make_shared<NodePtr>(Key(), Value());
        dummyTail_ = std::make_shared<NodePtr>;

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
        // 检查前向和后向是否已经被销毁
        assert(!node->prev_.expired() && node->next_);

        // prev_ 是弱引用（weak_ptr），不能直接用 prev_->next_。
        // 所以必须先 .lock() 得到一个 shared_ptr 再解引用
        auto prev = node->prev_.lock();
        prev->next_ = node->next_;
        node->next->prev_ = prev;
        node->next_ = nullptr;
    }

    // 将节点插入到链表尾部（最近访问位置）
    void insertNode(NodePtr node) 
    {
        node->next_ = dummyTail_;
        node->prev_ = dummyTail_->prev_;
        dummyTail_->prev_.lock()->next = node;
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
    LruKCache(int capacity, int historyCapacity, int k) 
        : LruCache<Key, Value>(capacity)
        , historyList_(std::make_unique<LruCache<Key, size_t>>(historyCapacity));
        , k_(k);
private:
/**
    LRU-K算法有两个队列，一个是缓存队列，一个是数据访问历史队列。
    当访问一个数据时，首先先在访问历史队列中累加访问次数，
    当历史访问记录超过K次后，才将数据缓存至缓存队列，
    从而避免缓存队列被污染。
*/
    int                                     k_; // 进入缓存队列的次数
    std::unique_ptr<KLruCache<Key, size_t>> historyList_; // 访问数据历史记录
    std::unordered_map<Key, Value>          historyValueMap_; // 存储没有达到 k 次的数据值

}

}