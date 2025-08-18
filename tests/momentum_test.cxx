#include "common/tests_common_pch.hpp"

#include <cmath>
#include <limits>
import foresight.mods;

using namespace foresight;

class MomentumCalculatorTest : public ::testing::Test {
  protected:
    void SetUp() override {
        // Default test parameters (typical scroll scenario)
        min_pos_   = 0.0f;
        max_pos_   = 1000.0f;
        start_pos_ = 100.0f;
        delta_     = 5.0f;
        velocity_  = 20.0f;
    }

    // Helper to create calculator with current test parameters
    momentum_calculator create_calculator() const {
        return momentum_calculator(min_pos_, max_pos_, start_pos_, delta_, velocity_);
    }

    float min_pos_;
    float max_pos_;
    float start_pos_;
    float delta_;
    float velocity_;
};

// Test prediction of final position
TEST_F(MomentumCalculatorTest, PredictsFinalPosition) {
    momentum_calculator mom  = create_calculator();
    float               dest = mom.pred_dest();

    // Verify within bounds
    EXPECT_GE(dest, min_pos_);
    EXPECT_LE(dest, max_pos_);

    // Verify prediction makes sense (should be further than start)
    EXPECT_GT(dest, start_pos_);

    // Verify calculation matches expected formula
    float expected = start_pos_ + 16.7f * delta_;
    EXPECT_FLOAT_EQ(dest, std::clamp(expected, min_pos_, max_pos_));
}

// Test boundary conditions for prediction
TEST_F(MomentumCalculatorTest, HandlesBoundaryPredictions) {
    // Test at minimum boundary
    start_pos_                  = min_pos_;
    momentum_calculator min_mom = create_calculator();
    EXPECT_EQ(min_mom.pred_dest(), min_pos_);

    // Test at maximum boundary
    start_pos_                  = max_pos_;
    momentum_calculator max_mom = create_calculator();
    EXPECT_EQ(max_mom.pred_dest(), max_pos_);

    // Test with negative delta (should not go below min)
    delta_                      = -5.0f;
    start_pos_                  = 50.0f;
    momentum_calculator neg_mom = create_calculator();
    EXPECT_EQ(neg_mom.pred_dest(), min_pos_);
}

// Test animation duration
TEST_F(MomentumCalculatorTest, HasCorrectDuration) {
    momentum_calculator mom = create_calculator();
    EXPECT_DOUBLE_EQ(mom.duration().count(), 1.0);
}

// Test position at key time points
TEST_F(MomentumCalculatorTest, PositionAtKeyTimes) {
    momentum_calculator mom = create_calculator();
    mom.set_target(200.0f); // Set reasonable target

    // Start position (t=0)
    EXPECT_FLOAT_EQ(mom.pos_at(fsecs(0.0)), start_pos_);

    // End position (t=1.0)
    EXPECT_FLOAT_EQ(mom.pos_at(fsecs(1.0)), 200.0f);

    // Position after duration should stay at target
    EXPECT_FLOAT_EQ(mom.pos_at(fsecs(1.5)), 200.0f);
}

// Test progress curve behavior
TEST_F(MomentumCalculatorTest, ProgressCurveMonotonic) {
    momentum_calculator mom = create_calculator();
    mom.set_target(200.0f);

    float prev_pos = mom.pos_at(fsecs(0.0));

    // Check that position increases monotonically
    for (float t = 0.1f; t <= 1.0f; t += 0.1f) {
        float pos = mom.pos_at(fsecs(t));
        EXPECT_GE(pos, prev_pos);
        prev_pos = pos;
    }
}

// Test different initial deltas
TEST_F(MomentumCalculatorTest, HandlesDifferentDeltas) {
    // Small delta (should have less momentum)
    delta_                          = 1.0f;
    momentum_calculator small_delta = create_calculator();
    small_delta.set_target(200.0f);
    float pos_small = small_delta.pos_at(fsecs(0.5));

    // Large delta (should have more momentum)
    delta_                          = 10.0f;
    momentum_calculator large_delta = create_calculator();
    large_delta.set_target(200.0f);
    float pos_large = large_delta.pos_at(fsecs(0.5));

    // Verify larger delta creates faster initial movement
    EXPECT_GT(pos_large, pos_small);
}

// Test curve parameter calculations
TEST_F(MomentumCalculatorTest, CurveParametersValid) {
    momentum_calculator mom = create_calculator();
    mom.set_target(200.0f);

    // Force initialization
    auto _ = mom.pos_at(fsecs(0.1));

    // Verify curve parameters are reasonable
    EXPECT_GT(mom.curve_magnitude(), 1.0f);
    EXPECT_GT(mom.decay(), 1.0f);
}

// Test with zero velocity
TEST_F(MomentumCalculatorTest, HandlesZeroVelocity) {
    velocity_               = 0.0f;
    momentum_calculator mom = create_calculator();
    mom.set_target(200.0f);

    // Should still animate to target, but with default curve
    EXPECT_FLOAT_EQ(mom.pos_at(fsecs(0.0)), start_pos_);
    EXPECT_FLOAT_EQ(mom.pos_at(fsecs(1.0)), 200.0f);
}

// Test with negative movement
TEST_F(MomentumCalculatorTest, HandlesNegativeMovement) {
    start_pos_ = 300.0f;
    delta_     = -5.0f;
    velocity_  = -20.0f;

    momentum_calculator mom = create_calculator();
    mom.set_target(200.0f); // Moving up

    // Verify positions decrease over time
    float prev = start_pos_;
    for (float t = 0.1f; t <= 1.0f; t += 0.1f) {
        float pos = mom.pos_at(fsecs(t));
        EXPECT_LT(pos, prev);
        prev = pos;
    }

    // Verify final position
    EXPECT_FLOAT_EQ(mom.pos_at(fsecs(1.0)), 200.0f);
}

// Test boundary clamping
TEST_F(MomentumCalculatorTest, ClampsToBoundaries) {
    // Set up scenario where prediction would exceed boundaries
    start_pos_ = 950.0f;
    delta_     = 10.0f;

    momentum_calculator mom = create_calculator();
    EXPECT_EQ(mom.pred_dest(), max_pos_);

    // Test with target beyond boundary
    mom.set_target(max_pos_ + 50.0f);

    // Should clamp to max position
    EXPECT_FLOAT_EQ(mom.pos_at(fsecs(1.0)), max_pos_);
}

// Test curve initialization with different progress values
// TEST_F(MomentumCalculatorTest, CurveInitialization) {
//     momentum_calculator mom = create_calculator();
//
//     // Test with various delta values to get different initial progress
//     for (float test_delta : {1.0f, 3.0f, 5.0f, 10.0f}) {
//         delta_ = test_delta;
//         mom    = create_calculator();
//         mom.set_target(start_pos_ + 50.0f);
//
//         // Force curve initialization
//         mom.pos_at(fsecs(0.1));
//
//         // Verify curve parameters make sense
//         EXPECT_GT(mom.curve_magnitude(), 1.0f);
//         EXPECT_GT(mom.decay(), 1.0f);
//
//         // Verify progress at first frame is within expected range
//         float progress = mom.progress_at(fsecs(1.0f / 60.0f));
//         EXPECT_GE(progress, 0.1f);
//         EXPECT_LE(progress, 0.5f);
//     }
// }

// Test linear interpolation fallback
TEST_F(MomentumCalculatorTest, UsesLinearInterpolationWhenAppropriate) {
    // Case 1: Small delta (should use linear)
    delta_                          = 0.5f;
    momentum_calculator small_delta = create_calculator();
    small_delta.set_target(200.0f);

    // Force initialization
    small_delta.pos_at(fsecs(0.1));

    // Should be using linear interpolation
    EXPECT_TRUE(small_delta.is_linear());

    // Case 2: Moving away from target (should use linear)
    start_pos_               = 100.0f;
    delta_                   = 5.0f; // Moving down
    momentum_calculator away = create_calculator();
    away.set_target(50.0f);          // Target is up

    // Force initialization
    away.pos_at(fsecs(0.1));

    // Should be using linear interpolation
    EXPECT_TRUE(away.is_linear());
}

// Parameterized test for different target distances
class MomentumCalculatorDistanceTest
  : public MomentumCalculatorTest,
    public ::testing::WithParamInterface<float> {};

INSTANTIATE_TEST_SUITE_P(TargetDistances,
                         MomentumCalculatorDistanceTest,
                         ::testing::Values(10.0f, 50.0f, 100.0f, 500.0f));

TEST_P(MomentumCalculatorDistanceTest, PositionAtHalfTime) {
    float               target_distance = GetParam();
    momentum_calculator mom             = create_calculator();
    mom.set_target(start_pos_ + target_distance);

    float pos = mom.pos_at(fsecs(0.5f));

    // Verify position is between start and target
    EXPECT_GT(pos, start_pos_);
    EXPECT_LT(pos, start_pos_ + target_distance);

    // For larger distances, should have covered more ground at half time
    // (due to higher initial velocity relative to distance)
    static float last_progress = 0.0f;
    float        progress      = (pos - start_pos_) / target_distance;

    if (last_progress > 0.0f) {
        EXPECT_GE(progress, last_progress);
    }
    last_progress = progress;
}

// Test numerical stability
TEST_F(MomentumCalculatorTest, NumericalStability) {
    // Extreme values test
    min_pos_   = 0.0f;
    max_pos_   = std::numeric_limits<float>::max() / 2.0f;
    start_pos_ = max_pos_ / 2.0f;
    delta_     = 1000000.0f;
    velocity_  = 10000000.0f;

    momentum_calculator mom = create_calculator();
    mom.set_target(max_pos_ - 100.0f);

    // Should not produce NaN or infinity
    float pos = mom.pos_at(fsecs(0.5f));
    EXPECT_FALSE(std::isnan(pos));
    EXPECT_FALSE(std::isinf(pos));
    EXPECT_GE(pos, start_pos_);
    EXPECT_LE(pos, max_pos_);
}

// Test curve parameter convergence
TEST_F(MomentumCalculatorTest, CurveParametersConverge) {
    momentum_calculator mom = create_calculator();
    mom.set_target(200.0f);

    // Force initialization
    mom.pos_at(fsecs(0.1));

    // Verify the iterative solution converged
    float exponent            = -60.0f * 1.0f;
    float calculated_progress = mom.curve_magnitude() * (1.0f - std::pow(mom.decay(), exponent));
    EXPECT_NEAR(calculated_progress, 1.0f, 0.01f);
}
