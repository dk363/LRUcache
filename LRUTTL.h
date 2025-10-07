#include <ctime>
#include <unordered_map>

const int ttl = 5; // 过期时间间隔
class LRUCacheWithTTL {
private:
    struct Node {
        int key;
        int value;
        time_t expireTime; // 过期时间
        Node *pre, *next;
        Node(int k, int v, time_t expireTime) {
            this->key = k;
            this->value = v;
            this->expireTime = expireTime;
            pre = nullptr;
            next = nullptr;
        }
    };

    int n;
    unordered_map<int, Node*> hash;
    // L作为表头是不经常访问的 是最经常访问的R是表尾
    // 防止操作过程中内存溢出
    // 这里表述的不是很准确
    // 应该是在达到缓存容量时方便移除和加入节点时用 O(1) 的时间加入
    Node*L, *R;

    void remove(Node *node) {
        Node *pre_node = node->pre;
        Node *nxt_node = node->next;
        pre_node->next = nxt_node;
        nxt_node->pre = pre_node;

        hash.erase(node->key);
    }

    void insert(int key, int value) {
        time_t curTime = time(nullptr);
        Node *node = new Node(key, value, curTime) + ttl;
        // r 是最经常访问的
        Node *second = R->pre;
        
        second->next = node;
        R->pre = node;

        node->next = R;
        node->pre = second;

        hash[key] = node;
    }

public:
    LRUCache(int capacity) {
        n = capacity;
        L = new Node(-1, -1, 0);
        R = new Node(-1, -1, 0);
        L->next = R;
        R->pre = L;
    }

    int get(int key) {
        if (hash.find(key) != hash.end()) {
            Node *node = hash[key];
            time_t curTime = time(nullptr);

            if (difftime(node->expireTime, curTime) <= 0) {
                remove(node);
                return -1;
            } else {
                remove(node);
                insert(node->key, node->value);
                return node->value;
            }
        } else {
            // 这里隐形规定了 value != -1
            // 如果 value == 1
            return -1;
        }
    }

    void put(int key, int value) {
        if (hash.find(key) != hash.end()) {
            // 如果缓存中存在 key
            // 那么我们更新 对应节点的值
            Node *node = hash[key];
            remove(node);
            insert(key, value);
        } else {
            if (hash.size() == n) {
                Node *node = L->next;
                remove(node);
            }
            insert(key, value);
        }
    }
}