#include <unordered_map>

class LRU {
private:
    struct Node {
        int key;
        int value;
        Node *pre, *next;
        Node(int key, int value) {
            this->key = key;
            this->value = value;
            pre = nullptr;
            next = nullptr;
        }
    }

    unordered_map<int, Node*> hash;
    int n; // 缓存容量
    Node *L, *R;

    void remove(Node *node) {
        Node *pre = node->pre;
        Node *next = node->next;

        pre->next = next;
        next->pre = pre;

        hash.erase(node->value);
    }

    void insert(int key, int value) {
        Node *newNode = new Node(key, value);

        Node *pre = R->pre;
        Node *next = R;
        
        pre->next = newNode;
        newNode->next = next;
        next->pre = newNode;
        newNode->pre = pre;

        hash[key] = newNode;
    }

public:
    LRUCache(int capacity) {
        n = capacity;
        L = new Node(-1, -1);
        R = new Node(-1, -1);

        L->next = R;
        R->pre = L;
    }

    int get(int key) {
        if (hash.find(key) != hash.end()) {
            Node *node = hash[key];
            remove(node);
            insert(node->key, node->value);
            return node->value;
        } else {
            return -1;
        }
    }

    void put(int key, int value) {
        if (hash.find(key) != hash.end()) {
            Node *node = hash[key];
            remove(node);
            insert(node);
        } else {
            if (hash.size() == n) {
                
            }
            Node *newNode = new Node(key, value);
        }
    }
};