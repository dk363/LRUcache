// min_test.cpp：最小化GTest验证程序
#include <gtest/gtest.h>

// 最简单的测试用例
TEST(MinTest, Basic) {
    int a = 1;
    EXPECT_TRUE(a == 1);  // 验证GTest宏是否正常
    EXPECT_EQ(a, 1);
}

int main(int argc, char **argv) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}