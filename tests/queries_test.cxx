
#include "common/tests_common_pch.hpp"

#include <array>
#include <coroutine>
#include <print>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <vector>

import fs8.devices.queries;
import fs8.devices.evdev;
import fs8.devices.udev;

using namespace fs8;

// ===========================================================================
// to_string(matching_action_type) Tests
// ===========================================================================

TEST(MatchingActionTypeToString, AllKnownActionsReturnNonEmpty) {
    using enum query_target;
    for (auto const action :
         {match_subsystem, match_sysattr, match_property, tag, syspath, sysname, nomatch_subsystem, nomatch_sysattr, nomatch_property})
    {
        SCOPED_TRACE("action = " + std::to_string(static_cast<unsigned>(action)));
        EXPECT_FALSE(to_string(action).empty());
    }
}

TEST(MatchingActionTypeToString, EachActionProducesUniqueString) {
    using enum query_target;
    std::vector<std::string_view> results;
    for (auto const action :
         {match_subsystem, match_sysattr, match_property, tag, syspath, sysname, nomatch_subsystem, nomatch_sysattr, nomatch_property})
    {
        results.push_back(to_string(action));
    }
    for (std::size_t i = 0; i < results.size(); ++i) {
        for (std::size_t j = i + 1; j < results.size(); ++j) {
            SCOPED_TRACE("i = " + std::to_string(i) + ", j = " + std::to_string(j));
            EXPECT_NE(results[i], results[j]);
        }
    }
}

TEST(MatchingActionTypeToString, MatchAndNomatchDiffer) {
    using enum query_target;
    EXPECT_NE(to_string(match_subsystem), to_string(nomatch_subsystem));
    EXPECT_NE(to_string(match_sysattr), to_string(nomatch_sysattr));
    EXPECT_NE(to_string(match_property), to_string(nomatch_property));
}

TEST(MatchingActionTypeToString, UnknownValueReturnsNonEmpty) {
    auto const unknown = static_cast<query_target>(0xFF);
    EXPECT_FALSE(to_string(unknown).empty());
}

TEST(DeviceList, Basic) {
    int count = 0;
    for (auto dev : filter_devices(keyboard)) {
        EXPECT_TRUE(dev.device.is_valid());
        EXPECT_TRUE(matches(dev.device, keyboard));
        EXPECT_FALSE(matches(dev.device, mouse));
        // std::println("|||| {}", attr::name(dev.device));
        ++count;
    }
    EXPECT_NE(count, 0);
}

TEST(DeviceList, MultiQuery) {
    int count = 0;
    for (auto dev : filter_devices(keyboard, mouse)) {
        EXPECT_TRUE(dev.device.is_valid());
        EXPECT_TRUE(matches(dev.device, keyboard) || matches(dev.device, mouse));
        // std::println("Name {} {}", attr::name(dev.device), dev.device.sysname());
        ++count;
    }
    auto const vec = filter_devices(keyboard) | std::ranges::to<std::vector>();
    EXPECT_NE(count, vec.size());
}


TEST(ParseQueryTermTest, ValidTargetsNoKey) {
    auto q1 = parse_query_term("sub=input");
    EXPECT_EQ(q1.target, query_target::match_subsystem);
    EXPECT_TRUE(q1.key.empty());
    EXPECT_EQ(q1.value, "input");
    EXPECT_EQ(q1.percentage, 100);

    auto q2 = parse_query_term("name=event0");
    EXPECT_EQ(q2.target, query_target::sysname);
    EXPECT_TRUE(q2.key.empty());
    EXPECT_EQ(q2.value, "event0");
}

TEST(ParseQueryTermTest, ValidTargetsWithKey) {
    auto q1 = parse_query_term("attr:device/name=my_mouse");
    EXPECT_EQ(q1.target, query_target::match_sysattr);
    EXPECT_EQ(q1.key, "device/name");
    EXPECT_EQ(q1.value, "my_mouse");

    auto q2 = parse_query_term("prop:ID_INPUT_KEYBOARD=1");
    EXPECT_EQ(q2.target, query_target::match_property);
    EXPECT_EQ(q2.key, "ID_INPUT_KEYBOARD");
    EXPECT_EQ(q2.value, "1");
}

TEST(ParseQueryTermTest, InvertedMatches) {
    auto q1 = parse_query_term("!sub=input");
    EXPECT_EQ(q1.target, static_cast<query_target>(+query_target::match_subsystem | +query_target::nomatch_flag));
    EXPECT_TRUE(q1.key.empty());
    EXPECT_EQ(q1.value, "input");

    auto q2 = parse_query_term("!prop:ID_INPUT=1");
    EXPECT_EQ(q2.target, static_cast<query_target>(+query_target::match_property | +query_target::nomatch_flag));
    EXPECT_EQ(q2.key, "ID_INPUT");
    EXPECT_EQ(q2.value, "1");
}

TEST(ParseQueryTermTest, InvalidFormats) {
    auto expect_invalid = [](std::string_view str) {
        auto q = parse_query_term(str);
        EXPECT_EQ(q.target, invalid_field.target);
        EXPECT_EQ(q.key, invalid_field.key);
        EXPECT_EQ(q.value, invalid_field.value);
    };

    expect_invalid("sub_input");       // Missing '='
    expect_invalid("unknown=val");     // Unknown target
    expect_invalid("!badtarget=val");  // Unknown target with invert
    expect_invalid("");                // Empty string
}

TEST(ParseQueryTermTest, ValueContainsEquals) {
    // The first '=' should split target/key from value, allowing '=' inside the value itself
    auto q = parse_query_term("attr:test=a=b=c");
    EXPECT_EQ(q.target, query_target::match_sysattr);
    EXPECT_EQ(q.key, "test");
    EXPECT_EQ(q.value, "a=b=c");
}




class IsMatchedTest : public ::testing::Test {
protected:
    // Helper function to easily create a query_term for testing
    query_term create_term(std::string_view val, std::uint8_t percentage, query_target target = query_target::match_sysattr) {
        query_term term{};
        term.value = val; // Assuming 'value' is the target field string in query_term
        term.percentage = percentage;
        term.target = target;
        return term;
    }
};

// 1. Globe Search (101%)
TEST_F(IsMatchedTest, GlobeSearch) {
    auto term = create_term("usb*", 100);

    // Wildcard matches
    EXPECT_TRUE(is_matched(term, &query_term::value, "usb"));
    EXPECT_TRUE(is_matched(term, &query_term::value, "usb_device"));
    EXPECT_FALSE(is_matched(term, &query_term::value, "pci_usb"));

    // Multiple wildcards
    auto term2 = create_term("*usb*", 100);
    EXPECT_TRUE(is_matched(term2, &query_term::value, "pci_usb_device"));

    // No wildcards in target (fallback to exact match)
    auto term3 = create_term("usb_device", 100);
    EXPECT_TRUE(is_matched(term3, &query_term::value, "usb_device"));
    EXPECT_FALSE(is_matched(term3, &query_term::value, "usb_device_1"));
}

// 2. Exact Match (100%)
TEST_F(IsMatchedTest, ExactMatch) {
    auto term = create_term("exact_string", 100);

    EXPECT_TRUE(is_matched(term, &query_term::value, "exact_string"));
    EXPECT_FALSE(is_matched(term, &query_term::value, "exact_string_extra"));
    EXPECT_FALSE(is_matched(term, &query_term::value, "Exact_string")); // Case sensitive
}

// 3. Fuzzy Match (< 100%)
TEST_F(IsMatchedTest, FuzzyMatch) {
    // "hello" vs "helo" -> dist = 1, max_len = 5 -> sim = 100 - (1*100/5) = 80%
    auto term = create_term("hello", 80);

    EXPECT_TRUE(is_matched(term, &query_term::value, "helo"));   // Exactly 80%
    EXPECT_TRUE(is_matched(term, &query_term::value, "hello"));  // 100% (>= 80%)
    EXPECT_FALSE(is_matched(term, &query_term::value, "hero"));  // dist = 2 -> sim = 60% (< 80%)

    // "kitten" vs "sitting" -> dist = 3, max_len = 7 -> sim = 100 - 42 = 58%
    auto term2 = create_term("kitten", 50);
    EXPECT_TRUE(is_matched(term2, &query_term::value, "sitting")); // 58% >= 50%

    auto term3 = create_term("kitten", 60);
    EXPECT_FALSE(is_matched(term3, &query_term::value, "sitting")); // 58% < 60%

    // Edge cases (empty strings)
    auto empty_term = create_term("", 50);
    EXPECT_TRUE(is_matched(empty_term, &query_term::value, ""));    // Both empty = 100%
    EXPECT_FALSE(is_matched(empty_term, &query_term::value, "a"));  // One empty = 0%
}

// ===========================================================================
// QueryTerm bool semantics
// ===========================================================================

TEST(QueryTermBool, InvalidFieldIsFalse) {
    EXPECT_FALSE(static_cast<bool>(invalid_field));
}

TEST(QueryTermBool, EmptyKeyValidTermsAreTruthy) {
    using enum query_target;
    // These have empty keys but valid targets; they must not be treated as invalid.
    EXPECT_TRUE(static_cast<bool>(query_term{.key = {}, .value = "input", .target = match_subsystem}));
    EXPECT_TRUE(static_cast<bool>(query_term{.key = {}, .value = "event0", .target = sysname}));
    EXPECT_TRUE(static_cast<bool>(query_term{.key = {}, .value = "/sys/...", .target = syspath}));
    EXPECT_TRUE(static_cast<bool>(query_term{.key = {}, .value = "mytag", .target = tag}));
}

TEST(QueryTermBool, ParsedEmptyKeyTermsAreTruthy) {
    // sub=input, name=event0, path=... and tag=... previously produced empty keys
    // and were dropped by parse_device_query because operator bool checked !key.empty().
    using enum query_target;
    auto const q1 = parse_query_term("sub=input");
    auto const q2 = parse_query_term("name=event0");
    auto const q3 = parse_query_term("tag=special");
    EXPECT_TRUE(static_cast<bool>(q1));
    EXPECT_TRUE(static_cast<bool>(q2));
    EXPECT_TRUE(static_cast<bool>(q3));
    EXPECT_EQ(q1.target, match_subsystem);
    EXPECT_EQ(q2.target, sysname);
    EXPECT_EQ(q3.target, tag);
}

// ===========================================================================
// positive() / is_negated()
// ===========================================================================

TEST(QueryTargetPositive, StripsNomatchFlag) {
    using enum query_target;
    EXPECT_EQ(positive(match_subsystem), match_subsystem);
    EXPECT_EQ(positive(nomatch_subsystem), match_subsystem);
    EXPECT_EQ(positive(nomatch_property), match_property);
    EXPECT_EQ(positive(nomatch_sysattr), match_sysattr);
}

TEST(QueryTargetPositive, IsNegated) {
    using enum query_target;
    EXPECT_FALSE(is_negated(match_subsystem));
    EXPECT_FALSE(is_negated(sysname));
    EXPECT_TRUE(is_negated(nomatch_subsystem));
    EXPECT_TRUE(is_negated(nomatch_property));
    EXPECT_TRUE(is_negated(nomatch_sysattr));
}

// ============================================================================
// Masked vs. positive-only predicates
// ===========================================================================

TEST(QueryTargetPredicates, MaskedAcceptNegatedButPositiveDont) {
    using enum query_target;
    query_term const pos_sub{.key = "input", .value = {}, .target = match_subsystem};
    query_term const neg_sub{.key = "input", .value = {}, .target = nomatch_subsystem};
    query_term const pos_prop{.key = "ID_INPUT", .value = {}, .target = match_property};
    query_term const neg_prop{.key = "ID_INPUT", .value = {}, .target = nomatch_property};
    query_term const pos_attr{.key = "device/name", .value = {}, .target = match_sysattr};
    query_term const neg_attr{.key = "device/name", .value = {}, .target = nomatch_sysattr};

    EXPECT_TRUE(is_subsystem(pos_sub));
    EXPECT_TRUE(is_subsystem(neg_sub));   // masked accepts negated
    EXPECT_TRUE(is_positive_subsystem(pos_sub));
    EXPECT_FALSE(is_positive_subsystem(neg_sub));

    EXPECT_TRUE(is_property(pos_prop));
    EXPECT_TRUE(is_property(neg_prop));
    EXPECT_TRUE(is_positive_property(pos_prop));
    EXPECT_FALSE(is_positive_property(neg_prop));

    EXPECT_TRUE(is_sysattr(pos_attr));
    EXPECT_TRUE(is_sysattr(neg_attr));
    EXPECT_TRUE(is_positive_sysattr(pos_attr));
    EXPECT_FALSE(is_positive_sysattr(neg_attr));
}

// ============================================================================
// View filters must exclude negated terms
// ============================================================================

TEST(QueryViewFilters, ExcludeNegatedTerms) {
    using enum query_target;
    std::array<query_term, 6> terms = {
      query_term{.key = "input", .value = {}, .target = match_subsystem},
      query_term{.key = "input", .value = {}, .target = nomatch_subsystem},
      query_term{.key = "ID_INPUT", .value = "1", .target = match_property},
      query_term{.key = "ID_INPUT", .value = "1", .target = nomatch_property},
      query_term{.key = "device/name", .value = "kbd", .target = match_sysattr},
      query_term{.key = "device/name", .value = "kbd", .target = nomatch_sysattr},
    };
    basic_device_query<terms.size()> q{.fields = terms};

    auto const subs_out  = subsystems(q) | std::ranges::to<std::vector>();
    auto const props_out = properties(q) | std::ranges::to<std::vector>();
    auto const attrs_out = sysattrs(q) | std::ranges::to<std::vector>();

    ASSERT_EQ(subs_out.size(), 1U);
    EXPECT_EQ(subs_out.front().target, match_subsystem);

    ASSERT_EQ(props_out.size(), 1U);
    EXPECT_EQ(props_out.front().target, match_property);

    ASSERT_EQ(attrs_out.size(), 1U);
    EXPECT_EQ(attrs_out.front().target, match_sysattr);
}

// ============================================================================
// Negated terms in matches(): must not assert, and must invert correctly
// ============================================================================

TEST(DeviceMatches, NegatedSubsystemExcludesWithoutAsserting) {
    // A query that only negates the "input" subsystem. Filtering must not hit the
    // previously-unimplemented else/assert branch, and must exclude input devices.
    std::array<query_term, 1> neg = {query_term{.key = "input", .value = {}, .target = query_target::nomatch_subsystem}};
    device_query q{.fields = std::span<query_term const>{neg}};

    for (auto const& dev : filter_devices(q)) {
        EXPECT_TRUE(dev.device.is_valid());
        EXPECT_NE(dev.device.subsystem(), "input");
    }
}

// ============================================================================
// matches(evdev, device_query): fields expressible through evdev
// ============================================================================

TEST(EvdevMatches, NameSysattrFieldMatches) {
    for (auto pick : filter_devices(keyboard)) {
        auto edev = initialize(query, pick.device);
        if (!edev.is_ok()) [[unlikely]] {
            continue;
        }
        // A query matching the device's name must match the opened evdev.
        std::array<query_term, 1> fields = {match_sysattr("device/name", edev.device_name())};
        device_query              q{.fields = std::span<query_term const>{fields}, .caps = {}};
        EXPECT_TRUE(matches(edev, q));
        return;
    }
    GTEST_SKIP() << "No input devices found.";
}

TEST(EvdevMatches, UnverifiableFieldsDontExclude) {
    // Properties/tags are not exposed by evdev; they must not make an evdev
    // device fail the query.
    for (auto pick : filter_devices(keyboard)) {
        auto edev = initialize(query, pick.device);
        if (!edev.is_ok()) [[unlikely]] {
            continue;
        }
        std::array<query_term, 1> fields = {match_property("ID_INPUT", "1")};
        device_query              q{.fields = std::span<query_term const>{fields}, .caps = {}};
        EXPECT_TRUE(matches(edev, q));
        return;
    }
    GTEST_SKIP() << "No input devices found.";
}

TEST(EvdevMatches, PhysAndUniqSysattrFieldsMatch) {
    // A query matching the device's physical location and unique id must match
    // the opened evdev.
    for (auto pick : filter_devices(keyboard)) {
        auto edev = initialize(query, pick.device);
        if (!edev.is_ok()) [[unlikely]] {
            continue;
        }
        std::array<query_term, 2> fields = {
          match_sysattr("device/phys", edev.physical_location()),
          match_sysattr("device/uniq", edev.unique_identifier()),
        };
        device_query q{.fields = std::span<query_term const>{fields}, .caps = {}};
        EXPECT_TRUE(matches(edev, q));
        return;
    }
    GTEST_SKIP() << "No input devices found.";
}

TEST(EvdevMatches, NegatedNameFieldExcludes) {
    // `!attr:device/name=...` must invert the match on the evdev path.
    for (auto pick : filter_devices(keyboard)) {
        auto edev = initialize(query, pick.device);
        if (!edev.is_ok()) [[unlikely]] {
            continue;
        }
        query_term term = match_sysattr("device/name", edev.device_name());
        term.target     = query_target::nomatch_sysattr;
        std::array<query_term, 1> fields = {term};
        device_query              q{.fields = std::span<query_term const>{fields}, .caps = {}};
        EXPECT_FALSE(matches(edev, q));

        query_term term2 = match_sysattr("device/name", "definitely-not-this-device");
        term2.target     = query_target::nomatch_sysattr;
        std::array<query_term, 1> fields2 = {term2};
        device_query              q2{.fields = std::span<query_term const>{fields2}, .caps = {}};
        EXPECT_TRUE(matches(edev, q2));
        return;
    }
    GTEST_SKIP() << "No input devices found.";
}

TEST(EvdevMatches, NegatedSubsystemExcludes) {
    // All evdev devices belong to the "input" subsystem, so `!sub=input` must
    // never match one.
    for (auto pick : filter_devices(keyboard)) {
        auto edev = initialize(query, pick.device);
        if (!edev.is_ok()) [[unlikely]] {
            continue;
        }
        query_term term = subsystem("input");
        term.target     = query_target::nomatch_subsystem;
        std::array<query_term, 1> fields = {term};
        device_query              q{.fields = std::span<query_term const>{fields}, .caps = {}};
        EXPECT_FALSE(matches(edev, q));
        return;
    }
    GTEST_SKIP() << "No input devices found.";
}

TEST(EvdevMatches, WrongNameDoesNotMatch) {
    for (auto pick : filter_devices(keyboard)) {
        auto edev = initialize(query, pick.device);
        if (!edev.is_ok()) [[unlikely]] {
            continue;
        }
        std::array<query_term, 1> fields = {match_sysname("nonexistent-device-xyz")};
        device_query              q{.fields = std::span<query_term const>{fields}, .caps = {}};
        EXPECT_FALSE(matches(edev, q));
        return;
    }
    GTEST_SKIP() << "No input devices found.";
}

// ============================================================================
// matches_full(evdev, device_query): the device is looked up in udev and all
// query fields (including udev metadata) are verified.
// ============================================================================

TEST(EvdevMatchesFull, PropertyFieldVerifiedViaUdev) {
    for (auto pick : filter_devices(keyboard)) {
        auto edev = initialize(query, pick.device);
        if (!edev.is_ok()) [[unlikely]] {
            continue;
        }
        // filter_devices(keyboard) guarantees ID_INPUT=1 on the udev device.
        std::array<query_term, 1> fields = {match_property("ID_INPUT", "1")};
        device_query              q{.fields = std::span<query_term const>{fields}, .caps = {}};
        EXPECT_TRUE(matches_full(edev, q));
        return;
    }
    GTEST_SKIP() << "No input devices found.";
}

TEST(EvdevMatchesFull, WrongPropertyDoesNotMatch) {
    for (auto pick : filter_devices(keyboard)) {
        auto edev = initialize(query, pick.device);
        if (!edev.is_ok()) [[unlikely]] {
            continue;
        }
        std::array<query_term, 1> fields = {match_property("ID_INPUT", "999")};
        device_query              q{.fields = std::span<query_term const>{fields}, .caps = {}};
        // The full match reconstructs the udev device and rejects it...
        EXPECT_FALSE(matches_full(edev, q));
        // ...while the partial match (which assumes udev filtering happened) ignores the property.
        EXPECT_TRUE(matches(edev, q));
        return;
    }
    GTEST_SKIP() << "No input devices found.";
}

TEST(EvdevMatchesFull, WrongNameDoesNotMatch) {
    for (auto pick : filter_devices(keyboard)) {
        auto edev = initialize(query, pick.device);
        if (!edev.is_ok()) [[unlikely]] {
            continue;
        }
        std::array<query_term, 1> fields = {match_sysname("nonexistent-device-xyz")};
        device_query              q{.fields = std::span<query_term const>{fields}, .caps = {}};
        EXPECT_FALSE(matches_full(edev, q));
        return;
    }
    GTEST_SKIP() << "No input devices found.";
}

// ============================================================================
// query_from / to_queries / tags on string ranges
// ============================================================================

TEST(QueryFrom, EmptyStringIsEmptyQuery) {
    auto q = query_from("");
    EXPECT_EQ(q.count, 0U);
    EXPECT_TRUE(q.value.fields.empty());
    EXPECT_EQ(q.value.caps, +caps::nothing);
}

TEST(QueryFrom, CapabilitiesName) {
    auto q = query_from("keyboard");
    EXPECT_EQ(q.count, 0U);
    EXPECT_EQ(q.value.caps, caps_of("keyboard"));
}

TEST(QueryFrom, PathBecomesSubsystemAndSysname) {
    auto q = query_from("/dev/input/event10");
    ASSERT_EQ(q.count, 2U);
    EXPECT_EQ(q.value.fields[0], subsystem("input"));
    EXPECT_EQ(q.value.fields[1], match_sysname("event10"));
}

TEST(QueryFrom, QueryTerm) {
    auto q = query_from("name=event0");
    ASSERT_EQ(q.count, 1U);
    EXPECT_EQ(q.value.fields[0], parse_query_term("name=event0"));
}

TEST(QueryFrom, FuzzyDeviceNameFallback) {
    auto q = query_from("my_mouse");
    ASSERT_EQ(q.count, 1U);
    query_term expected = match_sysattr("device/name", "my_mouse");
    expected.percentage = 60;
    EXPECT_EQ(q.value.fields[0], expected);
}

TEST(OwnedQuery, CopyRepointsIntoOwnStorage) {
    auto source = query_from("/dev/input/event10");
    std::vector<owned_query> vec;
    vec.push_back(source);
    ASSERT_EQ(vec[0].count, 2U);
    EXPECT_EQ(vec[0].value.fields[0], subsystem("input"));
    EXPECT_EQ(vec[0].value.fields[1], match_sysname("event10"));
}

TEST(ToQueries, MapsStringsAndSkipsEmpty) {
    std::array<std::string_view, 3> strs{"keyboard", "", "name=event0"};
    auto queries = (strs | to_queries) | std::ranges::to<std::vector<owned_query>>();
    ASSERT_EQ(queries.size(), 2U);
    EXPECT_EQ(queries[0].value.caps, caps_of("keyboard"));
    ASSERT_EQ(queries[1].value.fields.size(), 1U);
    EXPECT_EQ(queries[1].value.fields[0], parse_query_term("name=event0"));
}

TEST(TaggedQueries, ApplyTagsToStringRange) {
    std::array<std::string_view, 2> strs{"keyboard", "name=event0"};
    auto queries = (strs | grab | fail_on_no_match) | std::ranges::to<std::vector<owned_query>>();
    ASSERT_EQ(queries.size(), 2U);
    for (auto const& q : queries) {
        EXPECT_TRUE(q.value.grab);
        EXPECT_TRUE(q.value.fail_on_no_match);
    }
}

TEST(TaggedQueries, ApplyTagsToOwnedQueryRange) {
    std::array<std::string_view, 1> strs{"keyboard"};
    std::vector<owned_query>        vec;
    for (auto q : (strs | to_queries | grab)) {
        EXPECT_TRUE(q.value.grab);
        EXPECT_FALSE(q.value.fail_on_no_match);
        vec.push_back(q);
    }
    ASSERT_EQ(vec.size(), 1U);
    EXPECT_TRUE(vec[0].value.grab);
}

TEST(TaggedQueries, SingleQueryPipeStillWorks) {
    auto q = (keyboard | grab | fail_on_no_match);
    EXPECT_TRUE(q.grab);
    EXPECT_TRUE(q.fail_on_no_match);
}

TEST(FindDevices, OverQueriesYieldsOkDevices) {
    std::array<std::string_view, 1> strs{"keyboard"};
    int                             count = 0;
    for (auto dev : (strs | to_queries | find_devices)) {
        EXPECT_TRUE(dev.is_ok());
        ++count;
    }
    if (count == 0) {
        GTEST_SKIP() << "No keyboard devices found.";
    }
}