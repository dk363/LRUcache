// 1. 先引入系统/GTest头文件
#include <gtest/gtest.h>
#include <vector>
#include <thread>
#include <string>

// 2. 关键：添加GTest命名空间，后续无需写testing::
using namespace testing;

// 3. 最后引入自定义头文件（避免宏冲突）
#include "Log.h"
#include "LfuCache.h"

// 4. 测试用例：全部移除testing::前缀
TEST(LfuCacheTest, BasicPutAndGet) {
    Cache::LfuCache<int, std::string> cache(2);
    std::string value;

    cache.put(1, "value1");
    EXPECT_TRUE(cache.get(1, value));  // 移除testing::
    EXPECT_EQ(value, "value1");
    cache.put(2, "value2");
    EXPECT_TRUE(cache.get(2, value));
    EXPECT_EQ(value, "value2");
    EXPECT_TRUE(cache.get(1, value));
    EXPECT_EQ(value, "value1");
}

TEST(LfuCacheTest, EvictLeastFrequentlyUsed) {
    Cache::LfuCache<int, std::string> cache(2);
    std::string value;

    cache.put(1, "v1");
    cache.put(2, "v2");
    cache.put(3, "v3");
    EXPECT_FALSE(cache.get(1, value));
    EXPECT_TRUE(cache.get(2, value));
    EXPECT_EQ(value, "v2");
    EXPECT_TRUE(cache.get(3, value));
    EXPECT_EQ(value, "v3");

    cache.get(2, value);
    cache.get(2, value);
    cache.put(4, "v4");
    EXPECT_FALSE(cache.get(3, value));
    EXPECT_TRUE(cache.get(2, value));
    EXPECT_TRUE(cache.get(4, value));
    EXPECT_EQ(value, "v4");
}

TEST(LfuCacheTest, UpdateExistingKey) {
    Cache::LfuCache<int, std::string> cache(2);
    std::string value;

    cache.put(1, "v1");
    EXPECT_TRUE(cache.get(1, value));
    EXPECT_EQ(value, "v1");
    cache.put(1, "v1_updated");
    EXPECT_TRUE(cache.get(1, value));
    EXPECT_EQ(value, "v1_updated");
    cache.put(2, "v2");
    EXPECT_TRUE(cache.get(1, value));
    EXPECT_TRUE(cache.get(2, value));
}

TEST(LfuCacheTest, PurgeCache) {
    Cache::LfuCache<int, std::string> cache(3);
    std::string temp;
    std::string value;

    cache.put(1, "v1");
    cache.put(2, "v2");
    cache.put(3, "v3");
    EXPECT_TRUE(cache.get(1, temp));  // 修复右值问题
    cache.purge();

    EXPECT_FALSE(cache.get(1, value));
    EXPECT_FALSE(cache.get(2, value));
    EXPECT_FALSE(cache.get(3, value));
    cache.put(4, "v4");
    EXPECT_TRUE(cache.get(4, value));
    EXPECT_EQ(value, "v4");
}

TEST(LfuCacheTest, InvalidCapacityThrows) {
    // 修复：用括号包裹表达式，避免逗号拆分参数
    EXPECT_THROW((Cache::LfuCache<int, std::string>(0)), std::invalid_argument);
    EXPECT_THROW((Cache::LfuCache<int, std::string>(-5)), std::invalid_argument);
}

TEST(LfuCacheTest, HandleOverMaxAverageNum) {
    Cache::LfuCache<int, std::string> cache(3, 2);
    std::string value;

    cache.put(1, "v1");
    cache.put(2, "v2");
    cache.put(3, "v3");
    cache.get(1, value);
    cache.get(1, value);
    cache.get(2, value);
    cache.get(3, value);
    cache.get(3, value);
    cache.get(1, value);
    cache.get(2, value);

    EXPECT_TRUE(cache.get(1, value));
    EXPECT_EQ(value, "v1");
    EXPECT_TRUE(cache.get(2, value));
    EXPECT_EQ(value, "v2");
    cache.put(4, "v4");
    EXPECT_FALSE(cache.get(3, value));
    EXPECT_TRUE(cache.get(4, value));
    EXPECT_EQ(value, "v4");
}

TEST(LfuCacheTest, ThreadSafeAccess) {
    Cache::LfuCache<int, std::string> cache(100);  // 增大容量，减少淘汰干扰
    const int threadNum = 8;    // 增加线程数
    const int opsPerThread = 100;  // 增加每个线程的操作数

    auto threadFunc = [&cache](int threadId) {
        for (int i = 0; i < opsPerThread; ++i) {
            int key = threadId * opsPerThread + i;
            std::string value = "thread" + std::to_string(threadId) + "_val" + std::to_string(i);
            
            // 随机读写，模拟真实场景
            if (rand() % 2 == 0) {
                cache.put(key, value);
            } else {
                std::string result;
                cache.get(key, result);
            }
            
            // 随机延迟，放大竞态条件
            std::this_thread::sleep_for(std::chrono::microseconds(rand() % 10));
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < threadNum; ++i) {
        threads.emplace_back(threadFunc, i);
    }
    for (auto& t : threads) {
        t.join();
    }

    // 验证最终数据一致性
    for (int tid = 0; tid < threadNum; ++tid) {
        for (int i = 0; i < opsPerThread; ++i) {
            int key = tid * opsPerThread + i;
            std::string expected = "thread" + std::to_string(tid) + "_val" + std::to_string(i);
            std::string result;
            EXPECT_TRUE(cache.get(key, result)) << "Key " << key << " not found";
            EXPECT_EQ(result, expected) << "Key " << key << " value mismatch";
        }
    }
}

int main(int argc, char **argv) {
    InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}