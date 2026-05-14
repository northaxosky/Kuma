// Integration tests for the audio module's runtime playback path.
// Drives a real ma_engine via kuma::audio::init - if no audio
// device is available (rare on a dev machine, common on headless
// CI), init returns false and the suite SKIPs cleanly via GTEST.

#include <cmath>
#include <cstdint>
#include <fstream>
#include <vector>

#include <gtest/gtest.h>

#include <kuma/audio.h>
#include <kuma/ecs.h>
#include <kuma/transform.h>

#include "test_helpers.h"

namespace {

// Build a minimal mono WAV (8 kHz, 16-bit, 100 sine frames at 1 kHz)
// inside the test process, then run kuma-bake to convert it into
// a real .ksound the engine can load. Mirrors the helper in the
// Rust sound_test.rs so both sides exercise the same byte layout.
void write_test_wav(const std::filesystem::path& path) {
    constexpr uint32_t sample_rate = 8000;
    constexpr uint16_t channels    = 1;
    constexpr uint16_t bits        = 16;
    constexpr uint32_t frame_count = 100;
    const uint32_t byte_rate   = sample_rate * channels * bits / 8;
    const uint16_t block_align = channels * bits / 8;
    const uint32_t data_size   = frame_count * block_align;
    const uint32_t chunk_size  = 36 + data_size;

    std::ofstream f(path, std::ios::binary);
    auto write = [&](const void* p, std::streamsize n) {
        f.write(reinterpret_cast<const char*>(p), n);
    };
    write("RIFF", 4);
    write(&chunk_size, 4);
    write("WAVE", 4);
    write("fmt ", 4);
    const uint32_t fmt_size = 16;
    write(&fmt_size, 4);
    const uint16_t pcm_tag = 1;
    write(&pcm_tag, 2);
    write(&channels, 2);
    write(&sample_rate, 4);
    write(&byte_rate, 4);
    write(&block_align, 2);
    write(&bits, 2);
    write("data", 4);
    write(&data_size, 4);

    for (uint32_t n = 0; n < frame_count; ++n) {
        const float t = static_cast<float>(n) / sample_rate;
        const float s = std::sin(t * 6.2831853f * 1000.0f) * 0.8f;
        const int16_t i = static_cast<int16_t>(s * 32767.0f);
        write(&i, 2);
    }
}

// Each test gets a fresh init/shutdown pair. If init fails (no
// device), the test logs and SKIPs - we never want CI failures
// to stem from missing audio hardware.
class IntegrationAudio : public ::testing::Test {
protected:
    void SetUp() override {
        if (!kuma::audio::init()) {
            GTEST_SKIP() << "no audio device available - skipping playback test";
        }
    }
    void TearDown() override {
        kuma::audio::shutdown();
    }

    // Build a fresh .ksound on disk via a real kuma-bake invocation.
    // Returns the path so each test gets its own file under its own
    // tempdir, matching the asset_pipeline_test convention.
    std::filesystem::path bake_test_sound(const std::string& name) {
        const auto tmp = std::filesystem::temp_directory_path() /
                         ("kuma_audio_test_" + name);
        std::filesystem::create_directories(tmp);
        const auto wav_path = tmp / (name + ".wav");
        const auto ksound_path = tmp / (name + ".ksound");
        write_test_wav(wav_path);

        const int rc = kuma::integration::run_kuma_bake(
            {"sound", wav_path.string(), ksound_path.string()});
        EXPECT_EQ(rc, 0) << "kuma-bake sound exited " << rc;
        return ksound_path;
    }
};

}  // namespace

TEST_F(IntegrationAudio, PlayingCountStartsZero) {
    EXPECT_EQ(kuma::audio::playing_count(), 0u);
}

TEST_F(IntegrationAudio, LoadSoundReturnsNonNullForBakedAsset) {
    const auto path = bake_test_sound("load_basic");
    const auto* sound = kuma::audio::load_sound(path.string().c_str());
    EXPECT_NE(sound, nullptr);
}

TEST_F(IntegrationAudio, LoadSoundReturnsNullForMissingFile) {
    const auto* sound = kuma::audio::load_sound("/this/path/does/not/exist.ksound");
    EXPECT_EQ(sound, nullptr);
}

TEST_F(IntegrationAudio, LoadSoundReturnsCachedPointerOnRepeatLoad) {
    const auto path = bake_test_sound("cache_test");
    const auto* a = kuma::audio::load_sound(path.string().c_str());
    const auto* b = kuma::audio::load_sound(path.string().c_str());
    EXPECT_NE(a, nullptr);
    EXPECT_EQ(a, b) << "second load_sound should return cached pointer";
}

TEST_F(IntegrationAudio, PlaySoundReturnsValidHandle) {
    const auto path = bake_test_sound("play_basic");
    const auto* sound = kuma::audio::load_sound(path.string().c_str());
    ASSERT_NE(sound, nullptr);

    auto handle = kuma::audio::play_sound(sound);
    EXPECT_TRUE(handle.valid());
    EXPECT_GE(kuma::audio::playing_count(), 1u);
}

TEST_F(IntegrationAudio, PlaySoundAtAlsoReturnsValidHandle) {
    const auto path = bake_test_sound("play_at");
    const auto* sound = kuma::audio::load_sound(path.string().c_str());
    ASSERT_NE(sound, nullptr);

    auto handle = kuma::audio::play_sound_at(sound, kuma::Vec3{1.0f, 0.0f, 5.0f});
    EXPECT_TRUE(handle.valid());
}

TEST_F(IntegrationAudio, StopSoundDecrementsPlayingCount) {
    const auto path = bake_test_sound("stop_basic");
    const auto* sound = kuma::audio::load_sound(path.string().c_str());
    ASSERT_NE(sound, nullptr);

    auto handle = kuma::audio::play_sound(sound);
    const uint32_t before = kuma::audio::playing_count();
    kuma::audio::stop_sound(handle);
    EXPECT_EQ(kuma::audio::playing_count(), before - 1);
}

TEST_F(IntegrationAudio, StopOnInvalidHandleIsNoop) {
    kuma::audio::SoundHandle bogus{};
    kuma::audio::stop_sound(bogus);  // must not crash
    SUCCEED();
}

TEST_F(IntegrationAudio, StopOnAlreadyStoppedHandleIsNoop) {
    const auto path = bake_test_sound("stop_twice");
    const auto* sound = kuma::audio::load_sound(path.string().c_str());
    ASSERT_NE(sound, nullptr);

    auto handle = kuma::audio::play_sound(sound);
    kuma::audio::stop_sound(handle);
    kuma::audio::stop_sound(handle);  // generation mismatch - safe no-op
    SUCCEED();
}

TEST_F(IntegrationAudio, MasterVolumeAcceptsClampedRange) {
    kuma::audio::set_master_volume(-1.0f);
    kuma::audio::set_master_volume(2.0f);
    kuma::audio::set_master_volume(0.5f);
    SUCCEED();
}

TEST_F(IntegrationAudio, ListenerPoseUpdatesWithoutCrash) {
    kuma::audio::set_listener_pose(
        {0.0f, 1.6f, 0.0f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f});
    SUCCEED();
}

TEST_F(IntegrationAudio, AudioSourceComponentLazyCreatesOnSimulate) {
    const auto path = bake_test_sound("component_create");
    const auto* sound = kuma::audio::load_sound(path.string().c_str());
    ASSERT_NE(sound, nullptr);

    kuma::Registry registry;
    kuma::EntityID e = registry.create_entity();
    kuma::Transform t;
    t.set_position({0.0f, 0.0f, 0.0f});
    registry.add(e, t);
    kuma::AudioSource src;
    src.sound = sound;
    src.spatial = false;
    src.play_on_create = true;
    registry.add(e, src);

    const uint32_t before = kuma::audio::playing_count();
    kuma::audio::simulate(registry);
    const uint32_t after = kuma::audio::playing_count();

    EXPECT_GT(after, before);
    const auto& src_after = registry.get<kuma::AudioSource>(e);
    EXPECT_TRUE(src_after.created);
    EXPECT_TRUE(src_after.handle.valid());
}

TEST_F(IntegrationAudio, RemoveSourceCleansUpInstance) {
    const auto path = bake_test_sound("component_remove");
    const auto* sound = kuma::audio::load_sound(path.string().c_str());
    ASSERT_NE(sound, nullptr);

    kuma::Registry registry;
    kuma::EntityID e = registry.create_entity();
    kuma::Transform t;
    registry.add(e, t);
    kuma::AudioSource src;
    src.sound = sound;
    src.spatial = false;
    src.looping = true;  // long-lived so it doesn't auto-finish during test
    registry.add(e, src);

    kuma::audio::simulate(registry);
    const uint32_t with_source = kuma::audio::playing_count();
    EXPECT_GT(with_source, 0u);

    kuma::audio::remove_source(registry, e);
    EXPECT_LT(kuma::audio::playing_count(), with_source);

    const auto& src_after = registry.get<kuma::AudioSource>(e);
    EXPECT_FALSE(src_after.created);
}

TEST_F(IntegrationAudio, ValidateAliveSweepCatchesLeakedLoopingInstance) {
    const auto path = bake_test_sound("validate_alive");
    const auto* sound = kuma::audio::load_sound(path.string().c_str());
    ASSERT_NE(sound, nullptr);

    kuma::Registry registry;
    kuma::EntityID e = registry.create_entity();
    kuma::AudioSource src;
    src.sound = sound;
    src.spatial = false;
    src.looping = true;
    registry.add(e, src);

    kuma::audio::simulate(registry);
    const uint32_t with_source = kuma::audio::playing_count();
    EXPECT_GT(with_source, 0u);

    // Direct destroy_entity bypasses audio::remove_source. The
    // validate-alive sweep at the top of simulate should free the
    // looping instance - without the sweep it would play forever
    // because ma_sound_at_end never fires for looping sounds.
    registry.destroy_entity(e);
    kuma::audio::simulate(registry);
    EXPECT_LT(kuma::audio::playing_count(), with_source);
}
