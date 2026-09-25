#include "common/tests_common_pch.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <linux/input-event-codes.h>
#include <span>
#include <string>
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

    // The param-less palettes (FM, chiptune, piano, marimba, wavetable)
    // have no parameter table to verify the duration formula against, but
    // they must still fit the player's 150 ms slot budget for every
    // keycode, on both press and release.
    template <sound_generator Gen>
    void check_slot_duration(Gen const& gen) {
        for (uint16_t code = 0; code < 256; ++code) {
            for (int const value : {0, 1}) {
                auto const ev     = make_key_event(code, value);
                auto const frames = gen.duration_frames(ev, fmt);
                EXPECT_GT(frames, 0U) << "code 0x" << std::hex << code << " value " << value;
                EXPECT_LE(frames, max_slot_frames) << "code 0x" << std::hex << code << " value " << value;
                EXPECT_LE(frames * fmt.channels, max_slot_samples) << "code 0x" << std::hex << code << " value " << value;
            }
        }
    }

} // namespace

TEST(SoundTest, ClickDurationCoversSevenTauRing) {
    check_click_duration(bucklespring_synth{});
    check_click_duration(modelf_synth{});
    check_click_duration(linear_synth{});
    check_click_duration(topre_synth{});
    check_click_duration(typewriter_synth{});
    check_click_duration(mx_blue_synth{});
    check_click_duration(alps_synth{});
}

TEST(SoundTest, SynthPalettesFitSlotBudget) {
    check_slot_duration(fm_synth{});
    check_slot_duration(chiptune_synth{});
    check_slot_duration(piano_synth{});
    check_slot_duration(marimba_synth{});
    check_slot_duration(wavetable_synth{});
}

// Every generator plays through the player, which fades the last
// slot_fade_frames of each slot: the buffer must end at exactly 0 with the
// body untouched.
TEST(SoundTest, FadedGeneratorsEndAtSilence) {
    bucklespring_synth const buckle;
    modelf_synth const       modelf;
    linear_synth const       linear;
    topre_synth const        topre;
    typewriter_synth const   typewriter;
    mx_blue_synth const      mx_blue;
    alps_synth const         alps;
    chime_synth const        chime;
    basic_synth const        basic;
    fm_synth const           fm;
    chiptune_synth const     chiptune;
    piano_synth const        piano;
    marimba_synth const      marimba;
    wavetable_synth const    wavetable;

    for (uint16_t const code : test_keys) {
        for (int const value : {0, 1}) {
            auto const ev = make_key_event(code, value);

            std::array<std::vector<float>, 14> bufs{
              render(buckle, ev),
              render(modelf, ev),
              render(linear, ev),
              render(topre, ev),
              render(typewriter, ev),
              render(mx_blue, ev),
              render(alps, ev),
              render(chime, ev),
              render(basic, ev),
              render(fm, ev),
              render(chiptune, ev),
              render(piano, ev),
              render(marimba, ev),
              render(wavetable, ev)};
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

// ---------------------------------------------------------------------------
// Development aid: render every profile to WAVs and print FNV-1a hashes.
//
//   FS8_DUMP_DIR=/tmp/dump ./test-sound --gtest_filter='*DumpProfiles*'
//
// used to (a) compare profiles objectively (tools/profile-matrix.py over the
// WAVs) and (b) prove which profiles an engine change touched (hash lines).
// ---------------------------------------------------------------------------
namespace {

    std::uint64_t fnv1a(std::span<float const> samples) {
        std::uint64_t h = 14'695'981'039'346'656'037ULL;
        for (float const s : samples) {
            std::uint32_t bits;
            std::memcpy(&bits, &s, sizeof(bits));
            for (int b = 0; b < 4; ++b) {
                h ^= (bits >> (8 * b)) & 0xFFU;
                h *= 1'099'511'628'211ULL;
            }
        }
        return h;
    }

    void write_wav(std::string const& path, std::span<float const> mono, std::uint32_t const sample_rate) {
        std::FILE* f = std::fopen(path.c_str(), "wb");
        if (f == nullptr) {
            ADD_FAILURE() << "cannot open " << path;
            return;
        }
        auto const put32 = [f](std::uint32_t v) {
            std::uint8_t const b[4]{std::uint8_t(v), std::uint8_t(v >> 8), std::uint8_t(v >> 16), std::uint8_t(v >> 24)};
            std::fwrite(b, 1, 4, f);
        };
        auto const put16 = [f](std::uint16_t v) {
            std::uint8_t const b[2]{std::uint8_t(v), std::uint8_t(v >> 8)};
            std::fwrite(b, 1, 2, f);
        };
        std::uint32_t const data_bytes = static_cast<std::uint32_t>(mono.size() * 2);
        std::fwrite("RIFF", 1, 4, f);
        put32(36 + data_bytes);
        std::fwrite("WAVEfmt ", 1, 8, f);
        put32(16);
        put16(1); // PCM
        put16(1); // mono
        put32(sample_rate);
        put32(sample_rate * 2);
        put16(2);
        put16(16);
        std::fwrite("data", 1, 4, f);
        put32(data_bytes);
        for (float const s : mono) {
            float const c = std::clamp(s, -1.0f, 1.0f);
            put16(static_cast<std::uint16_t>(std::int16_t(std::lrintf(c * 32767.0f))));
        }
        std::fclose(f);
    }

    template <sound_generator Gen>
    void dump_synth(char const* dir, char const* name, Gen const& gen) {
        for (uint16_t const code : test_keys) {
            for (int const value : {0, 1}) {
                auto const         ev  = make_key_event(code, value);
                auto const         buf = render(gen, ev);
                std::vector<float> mono(buf.size() / fmt.channels);
                for (std::size_t i = 0; i < mono.size(); ++i) {
                    mono[i] = buf[i * fmt.channels];
                }
                std::printf("HASH %-13s 0x%02X %d %zu %016llX\n",
                            name,
                            code,
                            value,
                            mono.size(),
                            static_cast<unsigned long long>(fnv1a(mono)));
                std::string const path = std::string(dir) + "/" + name + "_" + std::to_string(code) + "_" + std::to_string(value) + ".wav";
                write_wav(path, mono, fmt.sample_rate);
            }
        }
    }

} // namespace

TEST(SoundTest, DumpProfilesWhenRequested) {
    char const* dir = std::getenv("FS8_DUMP_DIR");
    if (dir == nullptr) {
        GTEST_SKIP() << "FS8_DUMP_DIR not set";
    }

    bucklespring_synth const buckle;
    modelf_synth const       modelf;
    linear_synth const       linear;
    topre_synth const        topre;
    typewriter_synth const   typewriter;
    mx_blue_synth const      mx_blue;
    alps_synth const         alps;
    chime_synth const        chime;
    basic_synth const        basic;
    fm_synth const           fm;
    chiptune_synth const     chiptune;
    piano_synth const        piano;
    marimba_synth const      marimba;
    wavetable_synth const    wavetable;

    dump_synth(dir, "bucklespring", buckle);
    dump_synth(dir, "modelf", modelf);
    dump_synth(dir, "linear", linear);
    dump_synth(dir, "topre", topre);
    dump_synth(dir, "typewriter", typewriter);
    dump_synth(dir, "mx_blue", mx_blue);
    dump_synth(dir, "alps", alps);
    dump_synth(dir, "chime", chime);
    dump_synth(dir, "basic", basic);
    dump_synth(dir, "fm", fm);
    dump_synth(dir, "chiptune", chiptune);
    dump_synth(dir, "piano", piano);
    dump_synth(dir, "marimba", marimba);
    dump_synth(dir, "wavetable", wavetable);
}
