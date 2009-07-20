#include "Util/Stats.h"
#include "llvm/ADT/STLExtras.h"
#include "gtest/gtest.h"
#include <vector>

using llvm::array_endof;

TEST(Stats_Median, Odd)
{
    int values[] = {1, 7, 15};
    EXPECT_EQ(7, Median(std::vector<int>(values, array_endof(values))));
}

TEST(Stats_Median, Even)
{
    float values[] = {1, 7, 8, 15};
    EXPECT_EQ(7.5, Median(std::vector<float>(values, array_endof(values))));
}
