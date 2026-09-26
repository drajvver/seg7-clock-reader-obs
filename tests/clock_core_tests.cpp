#include "clock_core.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

namespace {
int failures = 0;

void check(bool condition, const char *message)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

seg7::Reading reading(double value)
{
    seg7::Reading r;
    r.has_value = r.valid = true;
    r.value_seconds = value;
    r.confidence = 1.0;
    r.display = std::to_string(value);
    r.raw = r.display;
    return r;
}

void initialize(seg7::ClockTracker &tracker, double value, double timestamp = 0.0)
{
    tracker.update(reading(value), timestamp - 0.2);
    tracker.update(reading(value), timestamp - 0.1);
    check(tracker.update(reading(value), timestamp).has_value, "initial clock confirmed");
}

#ifndef CLOCK_TEST_FIXTURE_DIR
#define CLOCK_TEST_FIXTURE_DIR "tests/fixtures"
#endif

void test_broadcast_frames()
{
    struct Example { const char *file; const char *expected; };
    const Example examples[] = {
        {"clock-0-58.pgm", "0:58"}, {"clock-1-02.pgm", "1:02"},
        {"clock-1-07.pgm", "1:07"}, {"clock-1-12.pgm", "1:12"},
        {"clock-1-17.pgm", "1:17"}, {"clock-1-22.pgm", "1:22"},
        {"clock-1-27.pgm", "1:27"}, {"clock-1-31.pgm", "1:31"},
        {"clock-1-36.pgm", "1:36"}, {"clock-1-39.pgm", "1:39"},
        {"clock-1-41.pgm", "1:41"}, {"clock-1-44.pgm", "1:44"},
        {"clock-1-49.pgm", "1:49"}, {"clock-1-54.pgm", "1:54"},
        {"clock-1-59.pgm", "1:59"}, {"clock-2-04.pgm", "2:04"},
        {"clock-3-55.pgm", "3:55"}, {"clock-6-24.pgm", "6:24"},
        {"clock-9-11.pgm", "9:11"}, {"clock-11-03.pgm", "11:03"},
        {"clock-12-36.pgm", "12:36"}, {"clock-14-44.pgm", "14:44"},
        {"clock-17-05.pgm", "17:05"}, {"clock-0-42.pgm", "0:42"},
        {"clock-6-32.pgm", "6:32"}, {"clock-10-58.pgm", "10:58"},
        {"clock-15-28.pgm", "15:28"},
        {"no-clock.pgm", ""}, {"blank.pgm", ""},
    };
    for (const auto &example : examples) {
        std::ifstream file(std::string(CLOCK_TEST_FIXTURE_DIR) + "/" + example.file,
                           std::ios::binary);
        std::string magic;
        int width = 0, height = 0, max_value = 0;
        file >> magic >> width >> height >> max_value;
        if (!file || magic != "P5" || width != 113 || height != 33 || max_value != 255) {
            check(false, example.file);
            continue;
        }
        file.get();
        std::vector<uint8_t> pixels((size_t)width * height);
        file.read(reinterpret_cast<char *>(pixels.data()), pixels.size());
        check((bool)file, "complete broadcast fixture");
        auto result = seg7::decode(pixels.data(), width, height, width);
        bool correct = *example.expected ? result.valid && result.display == example.expected
                                          : !result.valid;
        if (!correct)
            std::fprintf(stderr, "%s: expected %s, got %s (%s)\n", example.file,
                         example.expected, result.raw.c_str(), result.reason.c_str());
        check(correct, example.file);
        if (*example.expected) {
            std::vector<uint8_t> padded((size_t)(width + 4) * (height + 4), pixels[width * 16]);
            for (int y = 0; y < height; ++y)
                std::copy_n(pixels.data() + (size_t)y * width, width,
                            padded.data() + (size_t)(y + 2) * (width + 4) + 2);
            auto padded_result = seg7::decode(padded.data(), width + 4, height + 4, width + 4);
            check(padded_result.valid && padded_result.display == example.expected,
                  (std::string(example.file) + " with a small selection margin").c_str());
            auto borderless = seg7::decode(pixels.data() + width, width, height - 3, width);
            check(borderless.valid && borderless.display == example.expected,
                  (std::string(example.file) + " without the overlay border").c_str());
        }
    }
}

// Draw connected, rectangular LED segments, including the shorter lit bounds
// of 1, 4 and 7. Padding in each scanline also exercises the stride contract.
seg7::Reading decode_clock(const std::string &text, bool small_tenths = true, double slant = 0.0)
{
    const int width = 300, height = 70, stride = width + 13;
    std::vector<uint8_t> gray(stride * height, 0);
    auto rect = [&](int x, int y, int w, int h) {
        for (int yy = y; yy < y + h; ++yy)
            for (int xx = x; xx < x + w; ++xx)
                gray[yy * stride + xx] = 255;
    };
    const int masks[] = {63, 6, 91, 79, 102, 109, 125, 7, 127, 111};
    int x = 10;
    bool small = false;
    for (char c : text) {
        if (c == ':' || c == '.') {
            if (c == ':')
                rect(x, 24, 4, 4);
            rect(x, 41, 4, 4);
            x += 10;
            small = c == '.' && small_tenths;
            continue;
        }
        const int w = small ? 21 : 30, h = small ? 28 : 40, t = small ? 4 : 5;
        const int y = 55 - h, seg = masks[c - '0'];
        if (seg & 1) rect(x + t - 1, y, w - 2 * t + 2, t);
        if (seg & 2) rect(x + w - t, y + t - 1, t, h / 2 - t + 2);
        if (seg & 4) rect(x + w - t, y + h / 2 - 1, t, h / 2 - t + 2);
        if (seg & 8) rect(x + t - 1, y + h - t, w - 2 * t + 2, t);
        if (seg & 16) rect(x, y + h / 2 - 1, t, h / 2 - t + 2);
        if (seg & 32) rect(x, y + t - 1, t, h / 2 - t + 2);
        if (seg & 64) rect(x + t - 1, y + h / 2 - t / 2, w - 2 * t + 2, t);
        x += w + 10;
    }
    if (slant != 0.0) {
        auto original = gray;
        for (int y = 0; y < height; ++y) {
            int shift = (int)std::lround(slant * (y - 35));
            for (int xx = 0; xx < width; ++xx) {
                int source_x = xx - shift;
                gray[y * stride + xx] = source_x >= 0 && source_x < width
                                           ? original[y * stride + source_x] : 0;
            }
        }
    }
    return seg7::decode(gray.data(), width, height, stride);
}

void test_decoder()
{
    for (const char *text : {"2:35", "12:35", "4:44", "2:34", "1:23.4", "12:35.6"}) {
        auto r = decode_clock(text);
        check(r.valid && r.display == text, text);
    }
    auto r = decode_clock("1:23.5", false);
    check(r.valid && r.display == "1:23.5", "full-size tenths with explicit separators");
    check(!decode_clock("5.2").valid, "partial clock rejected");
    check(!decode_clock("6:78").valid, "seconds over 59 rejected");
    check(!decode_clock("1:2:35").valid, "unexpected separator rejected");
    check(seg7::format_seconds_only("1:23.4") == "1:23", "drop tenths");

    for (double slant : {0.0, -0.15, -0.3}) {
        for (int seconds = 0; seconds < 60; ++seconds) {
            char text[32];
            std::snprintf(text, sizeof(text), "2:%02d", seconds);
            auto decoded = decode_clock(text, true, slant);
            if (!decoded.valid || decoded.display != text) {
                std::fprintf(stderr, "slant %.2f: %s became %s (%s)\n", slant, text,
                             decoded.raw.c_str(), decoded.reason.c_str());
                check(false, "all second digits in upright and slanted displays");
            }
        }
    }

    std::vector<uint8_t> stripe(800 * 64, 0);
    for (int y = 30; y < 34; ++y)
        for (int x = 40; x < 640; ++x)
            stripe[y * 800 + x] = 255;
    check(!seg7::decode(stripe.data(), 800, 64, 800).valid,
          "wide stripe does not overflow the glyph splitter");
    check(!seg7::decode(nullptr, 32, 32, 32).valid, "null image rejected");
    check(!seg7::decode(stripe.data(), 800, 64, 10).valid, "short stride rejected");
    check(!seg7::decode(stripe.data(), -1, 64, 800).valid, "negative width rejected");
    check(!seg7::decode(stripe.data(), 800, 0, 800).valid, "empty image rejected");
    check(seg7::tighten_roi(nullptr, 32, 32, 32).w == 0, "null ROI rejected");
    check(seg7::tighten_roi(stripe.data(), 800, 64, 10).w == 0, "short ROI stride rejected");

    std::vector<uint8_t> noise(300 * 300, 0);
    for (int y = 0; y < 300; y += 4)
        for (int x = 0; x < 300; x += 4)
            for (int yy = y; yy < y + 2; ++yy)
                for (int xx = x; xx < x + 2; ++xx)
                    noise[yy * 300 + xx] = 255;
    check(!seg7::decode(noise.data(), 300, 300, 300).valid,
          "thousands of noise components are rejected before glyph fitting");
}

void test_stopped_clock()
{
    seg7::ClockTracker tracker(seg7::ClockTracker::Direction::Down);
    initialize(tracker, 60);
    check(!tracker.update(reading(60), 1).stopped, "running before stop interval");
    check(tracker.update(reading(60), 2).stopped, "stopped clock reported");
    auto s = tracker.update({}, 6);
    check(s.stale && !s.stopped && s.value == 60, "lost clock is stale and holds value");
    tracker.reset();
    initialize(tracker, 60);
    tracker.update(reading(60), 2);
    tracker.update(reading(59), 2.01);
    s = tracker.update(reading(59), 2.2);
    check(s.value == 60 && s.stopped, "stopped clock requires longer confirmation");
    s = tracker.update(reading(59), 2.65);
    check(s.value == 59 && !s.stopped, "stopped clock resumes after confirmation");
}

void test_resync()
{
    seg7::ClockTracker tracker;
    initialize(tracker, 60);
    tracker.update(reading(500), 0.01);
    tracker.update({}, 0.2);
    auto s = tracker.update(reading(500), 0.5);
    check(s.value == 60, "invalid frames break outlier confirmation");
    s = tracker.update(reading(500), 0.95);
    check(s.value == 500, "continuous stable outlier can resync");

    for (const auto dir : {seg7::ClockTracker::Direction::Auto,
                           seg7::ClockTracker::Direction::Down,
                           seg7::ClockTracker::Direction::Up}) {
        seg7::ClockTracker moving(dir);
        initialize(moving, 300);
        const double sign = dir == seg7::ClockTracker::Direction::Up ? 1.0 : -1.0;
        for (int i = 1; i <= 10; ++i)
            s = moving.update(reading(60 + sign * i / 10.0), i / 10.0);
        check(std::abs(s.value - (60 + sign)) < 0.001,
              "moving tenths clock recovers after a large cut");
        check(moving.resyncs == 1, "one resync for a coherent moving clock");
    }

    seg7::ClockTracker noisy;
    initialize(noisy, 60);
    for (int i = 1; i <= 30; ++i)
        s = noisy.update(reading(i % 2 ? 500 : 800), i / 10.0);
    check(s.value == 60, "inconsistent outliers never resync");

    seg7::ClockTracker drifting;
    initialize(drifting, 60);
    for (int i = 1; i <= 60; ++i)
        s = drifting.update(reading(500 + i * 0.2), i / 60.0);
    check(s.value == 60, "candidate cannot accumulate implausibly fast motion");
}

void test_invalid_readings()
{
    seg7::ClockTracker tracker;
    initialize(tracker, 60);
    auto s = tracker.update(reading(std::numeric_limits<double>::quiet_NaN()), 0.5);
    check(s.value == 60, "NaN cannot replace the held clock");
    s = tracker.update(reading(-1), 1);
    check(s.value == 60, "negative clock cannot replace the held clock");
    tracker.reset();
    check(!tracker.update(reading(std::numeric_limits<double>::infinity()), 0).has_value,
          "infinite clock cannot initialize the tracker");
}

void test_initial_confirmation()
{
    seg7::ClockTracker tracker;
    check(!tracker.update(reading(80 * 60 + 53), 0).has_value,
          "a single false clock cannot initialize the output");
    check(!tracker.update(reading(87), 0.1).has_value, "different candidate restarts confirmation");
    check(!tracker.update(reading(87), 0.2).has_value, "two frames are not sufficient");
    auto state = tracker.update(reading(87), 0.3);
    check(state.has_value && state.value == 87, "consistent real clock initializes the output");
    check(tracker.resyncs == 0, "initial confirmation is not a resync");

    tracker.reset();
    tracker.update(reading(87), 0);
    tracker.update(reading(87), 0.1);
    tracker.update({}, 0.2);
    check(!tracker.update(reading(87), 0.3).has_value,
          "invalid frame breaks initial confirmation");
    tracker.reset();
    tracker.update(reading(87), 0);
    tracker.update(reading(87), 0);
    check(!tracker.update(reading(87), 0).has_value, "duplicate timestamp cannot confirm a clock");
    tracker.reset();
    tracker.update(reading(87), 0);
    tracker.update(reading(86.9), 0.1);
    state = tracker.update(reading(86.8), 0.2);
    check(state.has_value && std::abs(state.value - 86.8) < 0.001,
          "moving tenths clock can initialize");
}
}

int main()
{
    test_decoder();
    test_broadcast_frames();
    test_initial_confirmation();
    test_stopped_clock();
    test_resync();
    test_invalid_readings();
    if (failures)
        return 1;
    std::puts("Clock decoder and tracker regressions passed.");
    return 0;
}
