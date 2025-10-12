#include <gtest/gtest.h>

using namespace testing;

#include "ArcLruPart.h"

TEST(ArcLruPartTest, BasicPutAndGet) {
    ArcLruPart::ArcLruPart<int, std::string> cache(2, 3);
    std::string value;

    cache.put(1, "value1");
    EXPECT_TRUE(cache.get(1, value));
    EXPECT_EQ(value, "value1");
    cache.put(2, "value2");
    EXPECT_TRUE(cache.get(2, value));
    EXPECT_EQ(value, "value2");
}

