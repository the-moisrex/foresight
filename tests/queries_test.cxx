
#include "common/tests_common_pch.hpp"

#include <limits>
#include <string>

import fs8.devices.queries;

using namespace fs8;

// Ensure you are importing the real concepts/types at the top:
// import fs8.devices.device_query;
// import fs8.devices.classification;

struct MockClassification {
    int  id                                          = 0;
    bool operator==(MockClassification const&) const = default;
};

std::string to_string(MockClassification const& snap) {
    return " [MockClass=" + std::to_string(snap.id) + "]";
}

fs8::classify::classification_snapshot snapshot(MockClassification const&) {
    // Default construct the real snapshot type so queries.ixx can use it
    return {};
}

// ---------------------------------------------------------
// Tests for Query Tags and Pipeline Operators
// ---------------------------------------------------------

class DeviceQueryTest : public ::testing::Test {
  protected:
    MockClassification dummy_class{.id = 42};
};

TEST_F(DeviceQueryTest, DefaultInitialization) {
    fs8::device_query<fs8::classify::classification_snapshot> q{.classification = {}};

    EXPECT_EQ(q.name, "");
    EXPECT_FALSE(q.grab);
    EXPECT_EQ(q.matches_limit, 1);
    EXPECT_FALSE(q.fail_on_no_match);
}

TEST_F(DeviceQueryTest, PipelineGrabTag) {
    auto q = dummy_class | grab;
    EXPECT_TRUE(q.grab);
    EXPECT_EQ(q.matches_limit, 1); // Defaults should remain untouched
}

TEST_F(DeviceQueryTest, PipelineAllowMultipleMatchesTag) {
    auto q = dummy_class | allow_multiple_matches;
    EXPECT_EQ(q.matches_limit, std::numeric_limits<std::uint8_t>::max());
}

TEST_F(DeviceQueryTest, PipelineMatchesLimitTag) {
    auto q = dummy_class | matches_limit(5);
    EXPECT_EQ(q.matches_limit, 5);
}

TEST_F(DeviceQueryTest, PipelineFailOnNoMatchTag) {
    auto q = dummy_class | fail_on_no_match;
    EXPECT_TRUE(q.fail_on_no_match);
}

TEST_F(DeviceQueryTest, PipelineNameTag) {
    auto q = dummy_class | name("TestDevice");
    EXPECT_EQ(q.name, "TestDevice");
}

TEST_F(DeviceQueryTest, PipelineChainingMultipleTags) {
    auto q = dummy_class | name("Keyboard") | grab | matches_limit(2) | fail_on_no_match;

    EXPECT_EQ(q.name, "Keyboard");
    EXPECT_TRUE(q.grab);
    EXPECT_EQ(q.matches_limit, 2);
    EXPECT_TRUE(q.fail_on_no_match);
    EXPECT_EQ(q.classification.id, 42);
}

// ---------------------------------------------------------
// Tests for Equality Operator
// ---------------------------------------------------------
TEST_F(DeviceQueryTest, EqualityOperator) {
    auto q1 = dummy_class | name("Mouse") | grab;
    auto q2 = dummy_class | name("Mouse") | grab;
    auto q3 = dummy_class | name("Keyboard") | grab;
    auto q4 = dummy_class | name("Mouse"); // no grab

    EXPECT_TRUE(q1 == q2);
    EXPECT_FALSE(q1 == q3);
    EXPECT_FALSE(q1 == q4);
}

// ---------------------------------------------------------
// Tests for Snapshot functionality
// ---------------------------------------------------------
TEST_F(DeviceQueryTest, SnapshotCreation) {
    auto original = dummy_class | name("Webcam") | matches_limit(3) | fail_on_no_match;
    auto snap     = snapshot(original);

    EXPECT_EQ(snap.name, "Webcam");
    EXPECT_EQ(snap.matches_limit, 3);
    EXPECT_TRUE(snap.fail_on_no_match);
    EXPECT_FALSE(snap.grab);
    // EXPECT_EQ(snap.classification.id, 42);
}

// ---------------------------------------------------------
// Tests for to_string Serialization
// ---------------------------------------------------------
TEST_F(DeviceQueryTest, ToStringEmptyNameDefaultState) {
    auto q    = dummy_class | matches_limit(0); // clear default match limit for pure string test
    auto snap = snapshot(q);

    std::string result = to_string(snap);
    EXPECT_EQ(result, "no-name input");
}

TEST_F(DeviceQueryTest, ToStringWithNameAndOptions) {
    auto q = dummy_class | name("Gamepad") | grab | matches_limit(4) | fail_on_no_match;

    auto        snap   = snapshot(q);
    std::string result = to_string(snap);

    // Based on the provided implementation, the format is:
    // {name}{class_str} [Fail on no match] [grab] [{limit} match limit]
    EXPECT_NE(result.find("Gamepad"), std::string::npos);
    EXPECT_NE(result.find("input"), std::string::npos);
    EXPECT_NE(result.find("[Fail on no match]"), std::string::npos);
    EXPECT_NE(result.find("[grab]"), std::string::npos);
    EXPECT_NE(result.find("[4 match limit]"), std::string::npos);
}

TEST_F(DeviceQueryTest, ToStringEdgeCases) {
    // Just the grab flag
    auto q1 = dummy_class | grab | matches_limit(0);
    EXPECT_EQ(to_string(snapshot(q1)), "no-name input [grab]");

    // Just fail_on_no_match
    auto q2 = dummy_class | fail_on_no_match | matches_limit(0);
    EXPECT_EQ(to_string(snapshot(q2)), "no-name input [Fail on no match]");
}
