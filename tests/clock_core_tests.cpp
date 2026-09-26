#include "clock_core.h"

#include <cmath>
#include <cstdio>
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
    tracker.update(reading(60), 0);
    check(!tracker.update(reading(60), 1).stopped, "running before stop interval");
    check(tracker.update(reading(60), 2).stopped, "stopped clock reported");
    auto s = tracker.update({}, 6);
    check(s.stale && !s.stopped && s.value == 60, "lost clock is stale and holds value");
    tracker.reset();
    tracker.update(reading(60), 0);
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
    tracker.update(reading(60), 0);
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
        moving.update(reading(300), 0);
        const double sign = dir == seg7::ClockTracker::Direction::Up ? 1.0 : -1.0;
        for (int i = 1; i <= 10; ++i)
            s = moving.update(reading(60 + sign * i / 10.0), i / 10.0);
        check(std::abs(s.value - (60 + sign)) < 0.001,
              "moving tenths clock recovers after a large cut");
        check(moving.resyncs == 1, "one resync for a coherent moving clock");
    }

    seg7::ClockTracker noisy;
    noisy.update(reading(60), 0);
    for (int i = 1; i <= 30; ++i)
        s = noisy.update(reading(i % 2 ? 500 : 800), i / 10.0);
    check(s.value == 60, "inconsistent outliers never resync");

    seg7::ClockTracker drifting;
    drifting.update(reading(60), 0);
    for (int i = 1; i <= 60; ++i)
        s = drifting.update(reading(500 + i * 0.2), i / 60.0);
    check(s.value == 60, "candidate cannot accumulate implausibly fast motion");
}

void test_invalid_readings()
{
    seg7::ClockTracker tracker;
    tracker.update(reading(60), 0);
    auto s = tracker.update(reading(std::numeric_limits<double>::quiet_NaN()), 0.5);
    check(s.value == 60, "NaN cannot replace the held clock");
    s = tracker.update(reading(-1), 1);
    check(s.value == 60, "negative clock cannot replace the held clock");
    tracker.reset();
    check(!tracker.update(reading(std::numeric_limits<double>::infinity()), 0).has_value,
          "infinite clock cannot initialize the tracker");
}
}

int main()
{
    test_decoder();
    test_stopped_clock();
    test_resync();
    test_invalid_readings();
    if (failures)
        return 1;
    std::puts("Clock decoder and tracker regressions passed.");
    return 0;
}
