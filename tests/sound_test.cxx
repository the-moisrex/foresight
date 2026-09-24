#include "common/tests_common_pch.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <linux/input-event-codes.h>
#include <span>
#include <vector>
import fs8.mods;
import fs8.sound;

using namespace fs8;

namespace {

    constexpr sound_format fmt{.sample_rate = 48'000, .channels = 2};

    constexpr std::array<uint16_t, 5> test_keys{0x01, 0x02, 0x0F, 0x1E, 0x29}; // ESC, 1, TAB, A, SPACE

    event_type make_key_event(uint16_t const code, int const value) {
        event_type ev;
        ev.set(EV_KEY, code, value);
        return ev;
    }

    template <sound_generator Gen>
    std::vector<float> render(Gen const& gen, event_type const& ev) {
        auto const frames = gen.duration_frames(ev, fmt);
        EXPECT_GT(frames, 0U);
        std::vector<float> buf(frames * fmt.channels, 0.0f);
        gen.render(ev, fmt, buf);
        return buf;
    }

    // The click-engine duration must cover ~7 envelope time-constants so the
    // ring decays to ≈ -60 dB on its own; none of the 512 parameter rows may
    // hit the 150 ms slot cap (which would truncate mid-ring).
    template <sound_generator Gen>
    void check_click_duration(Gen const& gen) {
        for (uint16_t code = 0; code < 256; ++code) {
            for (bool const pressed : {false, true}) {
                auto const& v        = gen.params(static_cast<uint8_t>(code), pressed);
                float const total_ms = v.snap_ms + v.ring_ms * 7.0f + v.contact_ms + 1.0f;

                ASSERT_LE(total_ms, 150.0f) << "code 0x" << std::hex << code;

                auto const frames = gen.duration_frames(static_cast<uint8_t>(code), pressed, fmt.sample_rate);
                EXPECT_EQ(frames, static_cast<std::size_t>(static_cast<float>(fmt.sample_rate) * total_ms / 1000.0f));
                ASSERT_LE(frames * fmt.channels, max_slot_samples);

                // Envelope level at the cut: elapsed time since attack_end is
                // snap_ms + 7*ring_ms + 0.8 ms, so exp(-elapsed/ring) <= e^-7.
                float const elapsed_ms = total_ms - (v.contact_ms + 0.2f);
                EXPECT_LE(std::exp(-elapsed_ms / v.ring_ms), 1.1e-3f);
            }
        }
    }

} // namespace

TEST(SoundTest, ClickDurationCoversSevenTauRing) {
    check_click_duration(bucklespring_synth{});
    check_click_duration(modelf_synth{});
}

// Every generator plays through the player, which fades the last
// slot_fade_frames of each slot: the buffer must end at exactly 0 with the
// body untouched.
TEST(SoundTest, FadedGeneratorsEndAtSilence) {
    bucklespring_synth const buckle;
    modelf_synth const       modelf;
    chime_synth const        chime;
    basic_synth const        basic;

    for (uint16_t const code : test_keys) {
        for (int const value : {0, 1}) {
            auto const ev = make_key_event(code, value);

            std::array<std::vector<float>, 4> bufs{render(buckle, ev), render(modelf, ev), render(chime, ev),
                                                   render(basic, ev)};
            for (auto& buf : bufs) {
                ASSERT_FALSE(buf.empty());
                auto const before = buf;

                apply_slot_fade(buf);

                EXPECT_FLOAT_EQ(buf[buf.size() - 1], 0.0f);
                EXPECT_FLOAT_EQ(buf[buf.size() - 2], 0.0f);

                std::size_t const frames      = buf.size() / fmt.channels;
                std::size_t const fade_frames = std::min(slot_fade_frames, frames);
                std::size_t const kept        = (frames - fade_frames) * fmt.channels;
                EXPECT_TRUE(std::equal(before.begin(), before.begin() + static_cast<std::ptrdiff_t>(kept), buf.begin()));
            }
        }
    }
}

// The fade is a per-frame raised cosine: 1 before the region, decreasing
// inside it, exactly 0 on the last frame.
TEST(SoundTest, SlotFadeIsMonotoneRaisedCosine) {
    std::size_t const  frames = slot_fade_frames + 64;
    std::vector<float> buf(frames * fmt.channels, 1.0f);

    apply_slot_fade(buf);

    std::size_t const body = (frames - slot_fade_frames) * fmt.channels;
    for (std::size_t i = 0; i < body; ++i) {
        ASSERT_FLOAT_EQ(buf[i], 1.0f);
    }

    for (std::size_t f = 0; f < slot_fade_frames; ++f) {
        float const gain = buf[(frames - slot_fade_frames + f) * fmt.channels];
        EXPECT_LE(gain, 1.0f);
        EXPECT_GE(gain, 0.0f);
        if (f > 0) {
            float const prev = buf[(frames - slot_fade_frames + f - 1) * fmt.channels];
            EXPECT_LE(gain, prev);
        }
    }

    EXPECT_FLOAT_EQ(buf[buf.size() - 1], 0.0f);
    EXPECT_FLOAT_EQ(buf[buf.size() - 2], 0.0f);
}

// Buffers shorter than the fade (or empty) must not crash and must still end
// at silence.
TEST(SoundTest, SlotFadeHandlesShortBuffers) {
    std::vector<float> short_buf(10, 1.0f); // 5 frames < slot_fade_frames
    apply_slot_fade(short_buf);
    EXPECT_FLOAT_EQ(short_buf.back(), 0.0f);

    apply_slot_fade({});
}
