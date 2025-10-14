#include <iostream>
#include <string>
#include <chrono>
#include <vector>
#include <iomanip>
#include <random>
#include <algorithm>
#include <array>
#include <optional>

#include "CachePolicy.h"
#include "LfuCache.h"
#include "LruCache.h"
#include "ArcCache/ArcCache.h"

class Timer 
{
public:
    // 起始时刻
    Timer() : start_(std::chrono::high_resolution_clock::now()) {}
    
    double elapsed() 
    {
        auto now = std::chrono::high_resolution_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(now - start_).count();
    }

private:
    std::chrono::time_point<std::chrono::high_resolution_clock> start_;
};


void testHotDataAccess() {
    std::cout << "\n=== 测试场景1：热点数据访问测试（改进版） ===" << std::endl;

    const size_t CAPACITY = 20;
    const int OPERATIONS = 500000;
    const int HOT_KEYS = 20;
    const int COLD_KEYS = 5000;
    const int WRITE_PROB = 10;  // 10%写操作

    std::vector<std::string> names = {"LRU", "LFU", "ARC", "LRU-K", "LFU-Aging"};
    std::vector<int> hits(names.size(), 0);
    std::vector<int> get_ops(names.size(), 0);
    std::vector<double> times(names.size(), 0.0);

    std::mt19937 gen(42);  // 固定种子，保证访问模式一致

    for (int i = 0; i < names.size(); ++i) {
        // === 1️⃣ 独立实例化每个缓存 ===
        std::unique_ptr<Cache::CachePolicy<int, std::string>> cache;

        if (i == 0)
            cache = std::make_unique<Cache::LruCache<int, std::string>>(CAPACITY);
        else if (i == 1)
            cache = std::make_unique<Cache::LfuCache<int, std::string>>(CAPACITY);
        else if (i == 2)
            cache = std::make_unique<Cache::ArcCache<int, std::string>>(CAPACITY / 2); // ARC一分为二
        else if (i == 3)
            cache = std::make_unique<Cache::LruKCache<int, std::string>>(CAPACITY, HOT_KEYS + COLD_KEYS, 2);
        else if (i == 4)
            cache = std::make_unique<Cache::LfuCache<int, std::string>>(CAPACITY, 20000); // aging版本

        // === 2️⃣ 热数据预热 ===
        for (int key = 0; key < HOT_KEYS; ++key) {
            cache->put(key, "value" + std::to_string(key));
        }

        Timer timer;  // 计时器

        // === 3️⃣ 模拟真实访问 ===
        for (int op = 0; op < OPERATIONS; ++op) {
            bool isPut = (gen() % 100 < WRITE_PROB);
            int key;

            // 80%访问热点，20%访问冷数据
            if (gen() % 100 < 80)
                key = gen() % HOT_KEYS;
            else
                key = HOT_KEYS + (gen() % COLD_KEYS);

            if (isPut) {
                cache->put(key, "value" + std::to_string(key) + "_v" + std::to_string(op % 100));
            } else {
                std::string result;
                get_ops[i]++;
                if (cache->get(key, result))
                    hits[i]++;
            }
        }

        // === 4️⃣ 记录耗时 ===
        times[i] = timer.elapsed();
    }

    // === 5️⃣ 输出结果 ===
    std::cout << "\n📊 [热点数据访问测试结果]" << std::endl;
    std::cout << "----------------------------------------------\n";
    std::cout << "算法\t\t命中率(%)\t平均访问时间(ms)\n";
    std::cout << "----------------------------------------------\n";
    for (int i = 0; i < names.size(); ++i) {
        double hitRate = 100.0 * hits[i] / std::max(1, get_ops[i]);
        double avgTime = times[i] / OPERATIONS;
        std::cout << names[i] << "\t\t"
                  << std::fixed << std::setprecision(2) << hitRate << "\t\t"
                  << std::setprecision(4) << avgTime << std::endl;
    }
    std::cout << "----------------------------------------------\n";
}

void testLoopPattern() 
{
    std::cout << "\n=== 测试场景2: 循环扫描测试 ===" << std::endl;

    const size_t CAPACITY = 50; // 缓存容量
    const int LOOP_SIZE = 500; // 循环范围
    const int OPERATIONS = 200000; // 总操作次数
    const int WRITE_PROB = 5;  // 5%写操作

    std::vector<std::string> names = {"LRU", "LFU", "ARC", "LRU-K", "LFU-Aging"};
    std::vector<int> hits(names.size(), 0);
    std::vector<int> get_ops(names.size(), 0);
    std::vector<double> times(names.size(), 0.0);

    std::mt19937 gen(42); 

    for (int i = 0; i < names.size(); ++i) 
    {
        // ==== 为每种算法创建全新的实例 ====
        std::unique_ptr<Cache::CachePolicy<int, std::string>> cache;

        if (i == 0)
            cache = std::make_unique<Cache::LruCache<int, std::string>>(CAPACITY);
        else if (i == 1)
            cache = std::make_unique<Cache::LfuCache<int, std::string>>(CAPACITY);
        else if (i == 2)
            // 这里除以2以保持公平
            cache = std::make_unique<Cache::ArcCache<int, std::string>>(CAPACITY / 2);
        else if (i == 3)
            cache = std::make_unique<Cache::LruKCache<int, std::string>>(CAPACITY, LOOP_SIZE * 2, 2);
        else if (i == 4)
            cache = std::make_unique<Cache::LfuCache<int, std::string>>(CAPACITY, 3000);  // aging

        // ==== 预热缓存（填充20%）====
        for (int key = 0; key < LOOP_SIZE / 5; ++key) 
        {
            cache->put(key, "loop" + std::to_string(key));
        }

        int current_pos = 0;
        Timer timer;  // ⏱️ 开始计时

        // ==== 模拟循环扫描访问 ====
        for (int op = 0; op < OPERATIONS; ++op) 
        {
            // 5% write operation
            bool isPut = (gen() % 100 < WRITE_PROB);
            int key;

            // 混合访问模式：60%顺序 + 30%随机 + 10%越界
            int mode = op % 100;
            if (mode < 60) 
            {
                key = current_pos;
                current_pos = (current_pos + 1) % LOOP_SIZE;
            } 
            else if (mode < 90) 
            {
                key = gen() % LOOP_SIZE;
            } 
            else 
            {
                key = LOOP_SIZE + (gen() % LOOP_SIZE);
            }

            if (isPut) 
            {
                // 写操作
                cache->put(key, "loop" + std::to_string(key) + "_v" + std::to_string(op % 100));
            } 
            else 
            {
                // 读操作
                std::string result;
                get_ops[i]++;
                if (cache->get(key, result)) 
                {
                    hits[i]++;
                }
            }
        }

        // ====  记录耗时 ====
        times[i] = timer.elapsed();
    }

    // ====  输出结果 ====
    std::cout << "\n📊 [循环扫描测试结果]" << std::endl;
    std::cout << "----------------------------------------------\n";
    std::cout << "算法\t\t命中率(%)\t平均访问时间(ms)\n";
    std::cout << "----------------------------------------------\n";
    for (int i = 0; i < names.size(); ++i) 
    {
        double hitRate = 100.0 * hits[i] / std::max(1, get_ops[i]);
        double avgTime = times[i] / OPERATIONS;
        std::cout << names[i] << "\t\t" 
                    // std::fixed 控制浮点数输出形式 std::setprecision 设置精度
                  << std::fixed << std::setprecision(2) << hitRate << "\t\t"
                  << std::setprecision(4) << avgTime << std::endl;
    }
    std::cout << "----------------------------------------------\n";
}

void testWorkloadShift() {
    std::cout << "\n=== 测试场景3：工作负载剧烈变化测试（改进版） ===" << std::endl;

    const size_t CAPACITY = 30;
    const int OPERATIONS = 80000;
    const int PHASES = 5;
    const int PHASE_LENGTH = OPERATIONS / PHASES;

    std::vector<std::string> names = {"LRU", "LFU", "ARC", "LRU-K", "LFU-Aging"};
    std::vector<int> hits(names.size(), 0);
    std::vector<int> get_ops(names.size(), 0);
    std::vector<double> times(names.size(), 0.0);

    std::mt19937 gen(42); // 固定种子

    for (int i = 0; i < names.size(); ++i) {
        // === 1️⃣ 独立实例化缓存 ===
        std::unique_ptr<Cache::CachePolicy<int, std::string>> cache;

        if (i == 0)
            cache = std::make_unique<Cache::LruCache<int, std::string>>(CAPACITY);
        else if (i == 1)
            cache = std::make_unique<Cache::LfuCache<int, std::string>>(CAPACITY);
        else if (i == 2)
            cache = std::make_unique<Cache::ArcCache<int, std::string>>(CAPACITY / 2); // ARC 一分为二
        else if (i == 3)
            cache = std::make_unique<Cache::LruKCache<int, std::string>>(CAPACITY, 500, 2);
        else if (i == 4)
            cache = std::make_unique<Cache::LfuCache<int, std::string>>(CAPACITY, 10000);

        // === 2️⃣ 初始预热 ===
        for (int key = 0; key < CAPACITY; ++key) {
            cache->put(key, "init" + std::to_string(key));
        }

        Timer timer;

        // === 3️⃣ 多阶段访问 ===
        for (int op = 0; op < OPERATIONS; ++op) {
            int phase = op / PHASE_LENGTH;

            int putProbability;
            switch (phase) {
                case 0: putProbability = 15; break;  // 热点访问
                case 1: putProbability = 30; break;  // 大范围随机
                case 2: putProbability = 10; break;  // 顺序扫描
                case 3: putProbability = 25; break;  // 局部性随机
                case 4: putProbability = 20; break;  // 混合访问
                default: putProbability = 20;
            }

            bool isPut = (gen() % 100 < putProbability);

            int key;
            switch (phase) {
                case 0: // 阶段1: 热点 10
                    key = gen() % 10;
                    break;
                case 1: // 阶段2: 大范围随机
                    key = gen() % 120;
                    break;
                case 2: // 阶段3: 顺序扫描
                    key = (op - PHASE_LENGTH * 2) % 60;
                    break;
                case 3: { // 阶段4: 局部性随机
                    int locality = (op / 400) % 5;
                    key = locality * 10 + (gen() % 10);
                    break;
                }
                case 4: { // 阶段5: 混合访问
                    int r = gen() % 100;
                    if (r < 30) key = gen() % 10;        // 热点
                    else if (r < 60) key = 10 + (gen() % 30); // 中等
                    else key = 40 + (gen() % 80);        // 大范围
                    break;
                }
                default:
                    key = gen() % 100;
            }

            if (isPut) {
                cache->put(key, "value" + std::to_string(key) + "_p" + std::to_string(phase));
            } else {
                std::string result;
                get_ops[i]++;
                if (cache->get(key, result))
                    hits[i]++;
            }
        }

        times[i] = timer.elapsed();
    }

    // === 4️⃣ 输出结果 ===
    std::cout << "\n📊 [工作负载剧烈变化测试结果]" << std::endl;
    std::cout << "----------------------------------------------\n";
    std::cout << "算法\t\t命中率(%)\t总耗时(ms)\n";
    std::cout << "----------------------------------------------\n";
    for (int i = 0; i < names.size(); ++i) {
        double hitRate = 100.0 * hits[i] / std::max(1, get_ops[i]);
        std::cout << names[i] << "\t\t"
                  << std::fixed << std::setprecision(2) << hitRate << "\t\t"
                  << std::setprecision(4) << times[i] << std::endl;
    }
    std::cout << "----------------------------------------------\n";
}

int main() {
    testHotDataAccess();
    testLoopPattern();
    testWorkloadShift();

    return 0;
}
