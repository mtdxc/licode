#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <stats/StatNode.h>

#include "../utils/Mocks.h"
#include "../utils/Tools.h"
#include "../utils/Matchers.h"

using ::testing::_;
using ::testing::IsNull;
using ::testing::Args;
using ::testing::AllArgs;
using ::testing::Not;
using ::testing::Return;
using ::testing::Eq;
using erizo::MovingAverageStat;

constexpr uint32_t kArbitraryWindowSize = 5;
constexpr uint32_t kArbitraryNumberToAdd = 5;

class MovingAverageStatTest : public ::testing::Test {
 public:
  MovingAverageStatTest(): moving_average_stat{kArbitraryWindowSize} {
  }

 protected:
  virtual void SetUp() {
  }

  virtual void TearDown() {
  }

  MovingAverageStat moving_average_stat;
};

TEST_F(MovingAverageStatTest, shouldCalculateAverageForLessSamplesThanWindowSize) {
  moving_average_stat+=kArbitraryNumberToAdd;
  moving_average_stat+=kArbitraryNumberToAdd + 1;
  moving_average_stat+=kArbitraryNumberToAdd + 2;
  uint32_t calculated_average = (3*kArbitraryNumberToAdd + 3)/3;
  EXPECT_EQ(moving_average_stat.value(), calculated_average);
}

TEST_F(MovingAverageStatTest, shouldCalculateAverageForLessSamplesThanWindowSize2) {
  moving_average_stat+=kArbitraryNumberToAdd;
  moving_average_stat+=kArbitraryNumberToAdd - 1;
  moving_average_stat+=kArbitraryNumberToAdd - 2;
  uint32_t calculated_average = (3*kArbitraryNumberToAdd - 3)/3;
  EXPECT_EQ(moving_average_stat.value(), calculated_average);
}

TEST_F(MovingAverageStatTest, shouldCalculateAverageForValuesInWindow) {
  for (int i = 0; i < kArbitraryWindowSize; i++) {
    moving_average_stat+=kArbitraryNumberToAdd;
  }
  for (int i = 0; i < kArbitraryWindowSize; i++) {
    moving_average_stat+=kArbitraryNumberToAdd + 1;
  }
  EXPECT_EQ(moving_average_stat.value(), kArbitraryNumberToAdd + 1);
}

TEST_F(MovingAverageStatTest, shouldCalculateAverageForValuesInWindow2) {
  for (int i = 0; i < kArbitraryWindowSize; i++) {
    moving_average_stat+=kArbitraryNumberToAdd;
  }
  for (int i = 0; i < kArbitraryWindowSize; i++) {
    moving_average_stat+=kArbitraryNumberToAdd - 1;
  }
  EXPECT_EQ(moving_average_stat.value(), kArbitraryNumberToAdd - 1);
}

TEST_F(MovingAverageStatTest, shouldCalculateAverageForGivenNumberOfSamples) {
  for (int i = 0; i < kArbitraryWindowSize; i++) {
    moving_average_stat+=i;
  }
  EXPECT_EQ(moving_average_stat.value(1), kArbitraryWindowSize - 1);
  EXPECT_EQ(moving_average_stat.value(2), ((kArbitraryWindowSize * 2 - 3) / 2));
}

TEST_F(MovingAverageStatTest, shouldCalculateAverageForGivenNumberOfSamples2) {
  for (int i = kArbitraryWindowSize; i > 0; i--) {
    moving_average_stat+=i;
  }
  EXPECT_EQ(moving_average_stat.value(1), 1u);
  EXPECT_EQ(moving_average_stat.value(2), ((1 + 2) / 2));
}
