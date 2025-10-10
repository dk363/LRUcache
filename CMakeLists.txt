cmake_minimum_required(VERSION 3.10)
project(testLfuCache)

# 设置C++标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 查找GTest库
find_package(GTest REQUIRED)
include_directories(${GTEST_INCLUDE_DIRS})

# 源文件（缓存实现+日志+测试）
set(SOURCES
    LfuCache.h     
    Log.h              
    testLfuCache.cpp
)

# 生成测试可执行文件
add_executable(test_lfu_cache ${SOURCES})

# 链接依赖库（GTest+线程库）
target_link_libraries(test_lfu_cache
    ${GTEST_LIBRARIES}
    ${GTEST_MAIN_LIBRARIES}
    pthread  # 多线程测试需链接线程库
)

# 注册测试（可选，便于用ctest运行）
enable_testing()
add_test(NAME testLfuCache COMMAND test_lfu_cache)