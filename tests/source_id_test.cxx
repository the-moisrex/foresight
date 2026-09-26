// Created by moisrex on 9/25/26.

#include "./common/tests_common_pch.hpp"

import fs8.mods;

using namespace fs8;

namespace {

    constexpr std::uint32_t base_id = make_source_id(0x123, 7);

    static_assert((base_id & source_id_owned) == 0);
    static_assert((base_id & source_id_chained) == 0);

} // namespace

TEST(SourceIdTest, OriginBitsAreFlagsNotIdentity) {
    constexpr auto owned = with_origin(base_id, source_id_owned);
    static_assert(is_owned_source(owned));
    static_assert(!is_chained_source(owned));
    static_assert(identity_of(owned) == base_id);
    static_assert(mod_id(owned) == mod_id(base_id));
    static_assert(source_index(owned) == source_index(base_id));
    EXPECT_EQ(identity_of(owned), base_id);

    constexpr auto chained = with_origin(base_id, source_id_chained);
    static_assert(is_chained_source(chained));
    static_assert(!is_owned_source(chained));
    EXPECT_EQ(identity_of(chained), base_id);

    // Both bits can be set at once (an owned uinput device whose phys is
    // "foresight:..."), they are independent flags, not an enum.
    constexpr auto both = with_origin(base_id, source_id_owned | source_id_chained);
    static_assert(is_owned_source(both) && is_chained_source(both));
    static_assert(identity_of(both) == base_id);
    EXPECT_EQ(identity_of(both), base_id);
}

TEST(SourceIdTest, MakeSourceIdNeverCarriesOriginBits) {
    constexpr auto id = make_source_id(0xFFFFu, 0x1234u);
    static_assert((id & ~source_id_payload_mask) == 0);
    EXPECT_EQ(source_index(id), 0x1234u);
    EXPECT_EQ(mod_id(id), 0xFFFFu & (source_id_mod_id_mask >> 16u));

    // A mod id with its top bits set still fits into the 14-bit mod space.
    constexpr auto masked = make_source_id(0xFFFFu, 0u);
    static_assert((masked & (source_id_owned | source_id_chained)) == 0);
    EXPECT_EQ(mod_id(masked), source_id_mod_id_mask >> 16u);
}

TEST(SourceIdTest, SidNeverCarriesOriginBits) {
    constexpr auto id = sid(from_input);
    static_assert((id & source_id_owned) == 0);
    static_assert((id & source_id_chained) == 0);
    EXPECT_FALSE(is_owned_source(id));
    EXPECT_FALSE(is_chained_source(id));
    EXPECT_EQ(identity_of(id), id);
}

TEST(SourceIdTest, StringificationShowsOriginFlags) {
    auto const plain   = std::string{to_source_string(base_id)};
    auto const owned   = std::string{to_source_string(with_origin(base_id, source_id_owned))};
    auto const chained = std::string{to_source_string(with_origin(base_id, source_id_chained))};

    EXPECT_EQ(plain, owned.substr(0, plain.size()));
    EXPECT_NE(owned, plain);
    EXPECT_NE(chained, plain);
    EXPECT_NE(owned, chained);
}
