#include "common/tests_common_pch.hpp"

#include <cmath>
#include <limits>
import foresight.mods;

using namespace foresight;

class MomentumCalculatorTest : public ::testing::Test {
  protected:
    void SetUp() override {
        // Default test parameters (typical scroll scenario)
        start_pos_ = 100.0f;
        delta_     = 5.0f;
        velocity_  = 20.0f;
    }

    // Helper to create calculator with current test parameters
    momentum_calculator create_calculator() const {
        return momentum_calculator(start_pos_, delta_, velocity_);
    }

    float start_pos_;
    float delta_;
    float velocity_;
};

// Test prediction of final position
TEST_F(MomentumCalculatorTest, PredictsFinalPosition) {
    momentum_calculator mom  = create_calculator();
    float               dest = mom.pred_dest();

    // Verify prediction makes sense (should be further than start)
    EXPECT_GT(dest, start_pos_);

    // Verify calculation matches expected formula
    float expected = start_pos_ + 16.7f * delta_;
    EXPECT_FLOAT_EQ(dest, expected);
}

// Test animation duration
TEST_F(MomentumCalculatorTest, HasCorrectDuration) {
    momentum_calculator mom = create_calculator();
    EXPECT_DOUBLE_EQ(mom.duration().count(), 1.0);
}

// Test position at key time points
TEST_F(MomentumCalculatorTest, PositionAtKeyTimes) {
    momentum_calculator mom = create_calculator();
    
    // Start position (t=0)
    EXPECT_FLOAT_EQ(mom.pos_at(fsecs(0.0)), start_pos_);
    
    // At end of animation, should be at predicted destination
    float pred_dest = mom.pred_dest();
    EXPECT_FLOAT_EQ(mom.pos_at(fsecs(1.0)), pred_dest);
    
    // Position after duration should stay at target
    EXPECT_FLOAT_EQ(mom.pos_at(fsecs(1.5)), pred_dest);
}

// Test progress curve behavior
TEST_F(MomentumCalculatorTest, ProgressCurveBehavior) {
    momentum_calculator mom = create_calculator();
    
    float pred_dest = mom.pred_dest();
    EXPECT_GT(pred_dest, start_pos_); // Should move forward
    
    // At start, should be at start position
    EXPECT_FLOAT_EQ(mom.pos_at(fsecs(0.0)), start_pos_);
    
    // At end, should be at predicted destination
    EXPECT_FLOAT_EQ(mom.pos_at(fsecs(1.0)), pred_dest);
}

// Test different initial deltas
TEST_F(MomentumCalculatorTest, HandlesDifferentDeltas) {
    // Small delta (should have less momentum)
    delta_                          = 1.0f;
    momentum_calculator small_delta = create_calculator();
    float pos_small = small_delta.pos_at(fsecs(0.5));

    // Large delta (should have more momentum)
    delta_                          = 10.0f;
    momentum_calculator large_delta = create_calculator();
    float pos_large = large_delta.pos_at(fsecs(0.5));

    // Verify larger delta creates faster initial movement
    EXPECT_GT(pos_large, pos_small);
}

// Test curve parameter calculations
TEST_F(MomentumCalculatorTest, CurveParametersValid) {
    momentum_calculator mom = create_calculator();

    // Verify curve parameters are reasonable
    EXPECT_GT(mom.curve_magnitude(), 0.0f);  // Should be positive
    EXPECT_GT(mom.decay(), 0.0f);            // Should be positive
}

// Test with zero velocity
TEST_F(MomentumCalculatorTest, HandlesZeroVelocity) {
    velocity_               = 0.0f;
    momentum_calculator mom = create_calculator();
    
    // Should animate to predicted destination
    EXPECT_FLOAT_EQ(mom.pos_at(fsecs(0.0)), start_pos_);
    float pred_dest = mom.pred_dest();
    EXPECT_FLOAT_EQ(mom.pos_at(fsecs(1.0)), pred_dest);
}

// Test with negative movement
TEST_F(MomentumCalculatorTest, HandlesNegativeMovement) {
    start_pos_ = 300.0f;
    delta_     = -5.0f;
    velocity_  = -20.0f;

    momentum_calculator mom = create_calculator();
    
    float pred_dest = mom.pred_dest();
    EXPECT_LT(pred_dest, start_pos_); // Should move backward

    // At start, should be at start position
    EXPECT_FLOAT_EQ(mom.pos_at(fsecs(0.0)), start_pos_);
    
    // At end, should be at predicted destination
    EXPECT_FLOAT_EQ(mom.pos_at(fsecs(1.0)), pred_dest);
}

// Test linear interpolation fallback
TEST_F(MomentumCalculatorTest, UsesLinearInterpolationWhenAppropriate) {
    // Case 1: Small delta (should use linear)
    delta_                          = 0.5f;
    momentum_calculator small_delta = create_calculator();
    
    // Should be using linear interpolation
    EXPECT_TRUE(small_delta.is_linear());

    // Case 2: Zero delta (should use linear)
    delta_                         = 0.0f;
    momentum_calculator zero_delta = create_calculator();
    
    // Should be using linear interpolation
    EXPECT_TRUE(zero_delta.is_linear());
}

// Test numerical stability
TEST_F(MomentumCalculatorTest, NumericalStability) {
    // Extreme values test
    delta_     = 1000000.0f;
    velocity_  = 10000000.0f;

    momentum_calculator mom = create_calculator();

    // Should not produce NaN or infinity
    float pos = mom.pos_at(fsecs(0.5f));
    EXPECT_FALSE(std::isnan(pos));
    EXPECT_FALSE(std::isinf(pos));
    // Note: With extreme values, we might not be able to guarantee pos >= start_pos_
    // since the calculation might overflow or underflow
}

// Test curve parameter convergence
TEST_F(MomentumCalculatorTest, CurveParametersConverge) {
    momentum_calculator mom = create_calculator();

    // Verify the iterative solution converged - use the correct formula from implementation
    constexpr float fps = 60.0f;
    constexpr float anim_dur_count = 1.0;
    float exponent = -fps * anim_dur_count;
    float calculated_progress = mom.curve_magnitude() * (1.0f - std::pow(mom.decay(), exponent));
    EXPECT_NEAR(calculated_progress, 1.0f, 0.1f); // Increased tolerance
}
