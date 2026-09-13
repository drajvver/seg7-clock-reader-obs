#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace seg7 {

struct Rect {
    int x = 0, y = 0, w = 0, h = 0;
};

enum class GlyphKind { Digit, Dot, Reject };

struct Glyph {
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    int area = 0;
    GlyphKind kind = GlyphKind::Digit;
    int digit = -1;
    bool small = false;
    double scores[7] = {0, 0, 0, 0, 0, 0, 0};
    double margin = 0.0;
    Rect cell;
    double slant = 0.0;
    bool has_slant = false;

    int w() const { return x1 - x0; }
    int h() const { return y1 - y0; }
};

struct Reading {
    std::string raw;
    std::vector<int> digits;
    std::vector<char> dot_after;
    bool has_value = false;
    double value_seconds = 0.0;
    std::string display;
    double confidence = 0.0;
    bool valid = false;
    std::string reason;
    std::vector<Glyph> glyphs;
};

Reading decode(const uint8_t *gray, int width, int height, int stride);
Rect tighten_roi(const uint8_t *gray, int width, int height, int stride);

struct TrackState {
    bool has_value = false;
    double value = 0.0;
    std::string display;
    std::string raw;
    double timestamp = 0.0;
    double confidence = 0.0;
    bool valid = false;
    bool stale = false;
    bool changed = false;
    bool stopped = false;
};

class ClockTracker {
public:
    enum class Direction { Auto, Up, Down };

    explicit ClockTracker(Direction dir = Direction::Auto);

    TrackState update(const Reading &reading, double t);
    void reset();

    Direction direction() const { return direction_; }
    int rejected = 0;
    int resyncs = 0;
    int invalid_frames = 0;

    double tolerance = 0.3;
    double resync_seconds = 0.15;
    double resync_large_seconds = 0.4;
    double resync_frozen_seconds = 0.6;
    double resync_large_delta = 2.0;
    double stopped_after = 1.5;
    double direction_evidence = 0.1;
    double stale_after = 3.0;

private:
    bool plausible(double delta, double dt) const;
    void vote_direction(double delta);
    bool accept(double value, const Reading &reading, double t);
    bool is_stopped(double t) const;
    double resync_seconds_for(double candidate, double t) const;
    TrackState state(double t, bool changed) const;

    Direction direction_;
    int dir_ = 0;
    bool has_value_ = false;
    double value_ = 0.0;
    std::string display_;
    std::string raw_;
    double t_ = 0.0;
    double confidence_ = 0.0;
    int up_votes_ = 0;
    int down_votes_ = 0;
    bool has_pending_ = false;
    double pending_ = 0.0;
    double pending_start_ = 0.0;
    bool has_last_change_ = false;
    double last_change_t_ = 0.0;
};

std::string format_seconds_only(const std::string &display);

}
