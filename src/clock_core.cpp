#include "clock_core.h"

#include <algorithm>
#include <functional>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace seg7 {
namespace {

constexpr double SEGMENT_THRESHOLD = 0.60;
constexpr double MIN_MARGIN = 0.15;
constexpr double NARROW_MIN_MARGIN = 0.08;
constexpr double REFINE_MARGIN = 0.25;
constexpr double OFFSET_PENALTY = 0.02;
constexpr int SAMPLES_PER_AXIS = 40;

const double AXES[7][4] = {
    {0.18, 0.09, 0.82, 0.09},
    {0.89, 0.10, 0.89, 0.48},
    {0.89, 0.52, 0.89, 0.90},
    {0.18, 0.91, 0.82, 0.91},
    {0.11, 0.52, 0.11, 0.90},
    {0.11, 0.10, 0.11, 0.48},
    {0.18, 0.50, 0.82, 0.50},
};

const char SEG_NAMES[7] = {'a', 'b', 'c', 'd', 'e', 'f', 'g'};

struct Comp {
    int x0, y0, x1, y1, area;
};

std::vector<Comp> label_components(const uint8_t *mask, int w, int h, bool eight) {
    std::vector<Comp> comps;
    std::vector<uint8_t> seen((size_t)w * h, 0);
    std::vector<int> stack;
    const int dx4[4] = {1, -1, 0, 0};
    const int dy4[4] = {0, 0, 1, -1};
    const int dx8[8] = {1, -1, 0, 0, 1, 1, -1, -1};
    const int dy8[8] = {0, 0, 1, -1, 1, -1, 1, -1};
    const int *dx = eight ? dx8 : dx4;
    const int *dy = eight ? dy8 : dy4;
    const int n = eight ? 8 : 4;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            size_t idx = (size_t)y * w + x;
            if (!mask[idx] || seen[idx])
                continue;
            Comp c{x, y, x + 1, y + 1, 0};
            stack.clear();
            stack.push_back((int)idx);
            seen[idx] = 1;
            while (!stack.empty()) {
                int cur = stack.back();
                stack.pop_back();
                int cx = cur % w, cy = cur / w;
                c.area++;
                c.x0 = std::min(c.x0, cx);
                c.y0 = std::min(c.y0, cy);
                c.x1 = std::max(c.x1, cx + 1);
                c.y1 = std::max(c.y1, cy + 1);
                for (int k = 0; k < n; ++k) {
                    int nx = cx + dx[k], ny = cy + dy[k];
                    if (nx < 0 || ny < 0 || nx >= w || ny >= h)
                        continue;
                    size_t nidx = (size_t)ny * w + nx;
                    if (mask[nidx] && !seen[nidx]) {
                        seen[nidx] = 1;
                        stack.push_back((int)nidx);
                    }
                }
            }
            comps.push_back(c);
        }
    }
    return comps;
}

double percentile_hist(const uint64_t *hist, uint64_t n, double q) {
    if (n == 0)
        return 0.0;
    double rank = q / 100.0 * (double)(n - 1);
    uint64_t lo = (uint64_t)std::floor(rank);
    uint64_t hi = (uint64_t)std::ceil(rank);
    auto value_at = [&](uint64_t idx) {
        uint64_t cum = 0;
        for (int v = 0; v < 256; ++v) {
            cum += hist[v];
            if (cum > idx)
                return (double)v;
        }
        return 255.0;
    };
    double vlo = value_at(lo), vhi = value_at(hi);
    return vlo + (vhi - vlo) * (rank - (double)lo);
}

bool preprocess(const uint8_t *gray, int width, int height, int stride, std::vector<uint8_t> &mask) {
    uint64_t hist[256] = {0};
    for (int y = 0; y < height; ++y) {
        const uint8_t *row = gray + (size_t)y * stride;
        for (int x = 0; x < width; ++x)
            hist[row[x]]++;
    }
    uint64_t n = (uint64_t)width * height;
    double lo = percentile_hist(hist, n, 40.0);
    double hi = percentile_hist(hist, n, 99.5);
    if (hi - lo < 20.0)
        return false;
    mask.assign((size_t)width * height, 0);
    for (int y = 0; y < height; ++y) {
        const uint8_t *row = gray + (size_t)y * stride;
        uint8_t *out = mask.data() + (size_t)y * width;
        for (int x = 0; x < width; ++x) {
            double v = ((double)row[x] - lo) / (hi - lo);
            v = std::min(1.0, std::max(0.0, v));
            out[x] = v > 0.5 ? 1 : 0;
        }
    }
    return true;
}

std::vector<uint8_t> dilate3x3(const std::vector<uint8_t> &mask, int w, int h) {
    std::vector<uint8_t> out(mask.size(), 0);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            uint8_t best = 0;
            for (int dy = -1; dy <= 1 && !best; ++dy) {
                int ny = y + dy;
                if (ny < 0 || ny >= h)
                    continue;
                for (int dx = -1; dx <= 1; ++dx) {
                    int nx = x + dx;
                    if (nx < 0 || nx >= w)
                        continue;
                    if (mask[(size_t)ny * w + nx]) {
                        best = 1;
                        break;
                    }
                }
            }
            out[(size_t)y * w + x] = best;
        }
    }
    return out;
}

int digit_from_segments(uint32_t segmask) {
    switch (segmask) {
    case 63: return 0;
    case 6: return 1;
    case 91: return 2;
    case 79: return 3;
    case 102: return 4;
    case 109: return 5;
    case 125: return 6;
    case 7: return 7;
    case 127: return 8;
    case 111: return 9;
    default: return -1;
    }
}

double fit_slope(std::vector<std::pair<double, double>> pts) {
    double slope = 0.0;
    for (int iter = 0; iter < 2; ++iter) {
        size_t n = pts.size();
        double sy = 0, sx = 0, syy = 0, sxy = 0;
        for (auto &p : pts) {
            sy += p.first;
            sx += p.second;
            syy += p.first * p.first;
            sxy += p.first * p.second;
        }
        double denom = (double)n * syy - sy * sy;
        if (std::abs(denom) < 1e-9)
            return 0.0;
        slope = ((double)n * sxy - sy * sx) / denom;
        double intercept = (sx - slope * sy) / (double)n;
        if (n <= 5)
            break;
        std::vector<std::pair<double, double>> kept;
        for (auto &p : pts) {
            if (std::abs(p.second - (slope * p.first + intercept)) <= 1.5)
                kept.push_back(p);
        }
        if (kept.size() == pts.size() || kept.size() < 4)
            break;
        pts = std::move(kept);
    }
    return slope;
}

bool slant_of(const std::vector<uint8_t> &mask, int w, int h, const Rect &cell, double &out_slant) {
    int x0 = cell.x, y0 = cell.y, cw = cell.w, ch = cell.h;
    if (x0 < 0 || y0 < 0 || x0 + cw > w || y0 + ch > h || cw <= 0 || ch <= 0)
        return false;
    std::vector<std::pair<double, double>> all_pts;
    std::vector<std::vector<std::pair<double, double>>> band_pts;
    const double bands[2][2] = {{0.10, 0.46}, {0.54, 0.90}};
    for (auto &band : bands) {
        std::vector<std::pair<double, double>> pts;
        int start = (int)std::ceil(band[0] * ch);
        int end = (int)std::floor(band[1] * ch);
        for (int yy = y0 + start; yy <= y0 + end; ++yy) {
            int rightmost = -1;
            for (int xx = x0 + cw - 1; xx >= x0; --xx) {
                if (mask[(size_t)yy * w + xx]) {
                    rightmost = xx;
                    break;
                }
            }
            if (rightmost >= 0)
                pts.push_back({(double)(yy - y0), (double)(rightmost - x0)});
        }
        if (pts.size() >= 4) {
            double xmin = pts[0].second, xmax = pts[0].second;
            for (auto &p : pts) {
                xmin = std::min(xmin, p.second);
                xmax = std::max(xmax, p.second);
            }
            if (xmax - xmin >= 2.0)
                band_pts.push_back(pts);
        }
        all_pts.insert(all_pts.end(), pts.begin(), pts.end());
    }
    if (all_pts.size() < 8)
        return false;
    double slope = fit_slope(all_pts);
    if (slope < -0.6 || slope > -0.03)
        return false;
    for (auto &pts : band_pts) {
        double band_slope = fit_slope(pts);
        if (band_slope < -0.6 || band_slope > -0.03)
            return false;
    }
    out_slant = slope;
    return true;
}

void axis_points(const Rect &cell, bool has_slant, double slant,
                 double xs[7][SAMPLES_PER_AXIS], double ys[7][SAMPLES_PER_AXIS]) {
    double x0 = cell.x, y0 = cell.y, w = cell.w, h = cell.h;
    double ycenter = y0 + 0.5 * h;
    for (int s = 0; s < 7; ++s) {
        double u1 = AXES[s][0], v1 = AXES[s][1];
        double u2 = AXES[s][2], v2 = AXES[s][3];
        for (int i = 0; i < SAMPLES_PER_AXIS; ++i) {
            double t = (double)i / (SAMPLES_PER_AXIS - 1);
            double u = u1 + (u2 - u1) * t;
            double v = v1 + (v2 - v1) * t;
            double x = x0 + u * w;
            double y = y0 + v * h;
            if (has_slant)
                x += slant * (y - ycenter);
            xs[s][i] = x;
            ys[s][i] = y;
        }
    }
}

double run_score(const std::vector<uint8_t> &dilated, int w, int h,
                 const double *xs, const double *ys) {
    int best = 0, cur = 0;
    for (int i = 0; i < SAMPLES_PER_AXIS; ++i) {
        int xi = (int)std::nearbyint(xs[i]);
        int yi = (int)std::nearbyint(ys[i]);
        bool lit = xi >= 0 && yi >= 0 && xi < w && yi < h && dilated[(size_t)yi * w + xi];
        cur = lit ? cur + 1 : 0;
        best = std::max(best, cur);
    }
    return (double)best / SAMPLES_PER_AXIS;
}

int score_digit(const std::vector<uint8_t> &dilated, int w, int h, const Rect &cell,
                bool has_slant, double slant, double scores[7], double &margin) {
    double xs[7][SAMPLES_PER_AXIS], ys[7][SAMPLES_PER_AXIS];
    axis_points(cell, has_slant, slant, xs, ys);
    uint32_t on_mask = 0;
    for (int s = 0; s < 7; ++s) {
        scores[s] = run_score(dilated, w, h, xs[s], ys[s]);
        if (scores[s] >= SEGMENT_THRESHOLD)
            on_mask |= (1u << s);
    }
    int digit = digit_from_segments(on_mask);
    double min_on = 1.0, max_off = 0.0;
    bool any_on = false;
    for (int s = 0; s < 7; ++s) {
        if (scores[s] >= SEGMENT_THRESHOLD) {
            min_on = std::min(min_on, scores[s]);
            any_on = true;
        } else {
            max_off = std::max(max_off, scores[s]);
        }
    }
    if (!any_on)
        min_on = 0.0;
    margin = digit >= 0 ? min_on - max_off : 0.0;
    return digit;
}

int score_digit_best(const std::vector<uint8_t> &dilated, int w, int h, const Rect &cell,
                     bool has_slant, double slant, double scores[7], double &margin, Rect &out_cell) {
    int digit = score_digit(dilated, w, h, cell, has_slant, slant, scores, margin);
    out_cell = cell;
    if (digit >= 0 && margin >= REFINE_MARGIN)
        return digit;
    int search = std::max(2, (int)std::lround(0.14 * cell.h));
    double best_key = margin;
    int best_digit = digit;
    double best_scores[7];
    std::memcpy(best_scores, scores, sizeof(best_scores));
    Rect best_cell = cell;
    for (int dx = -search; dx <= search; ++dx) {
        for (int dy = -search; dy <= search; ++dy) {
            if (dx == 0 && dy == 0)
                continue;
            Rect shifted{cell.x + dx, cell.y + dy, cell.w, cell.h};
            double s2[7], m2;
            int d2 = score_digit(dilated, w, h, shifted, has_slant, slant, s2, m2);
            if (d2 < 0)
                continue;
            double key = m2 - OFFSET_PENALTY * (std::abs(dx) + std::abs(dy));
            if (key > best_key) {
                best_key = key;
                best_digit = d2;
                std::memcpy(best_scores, s2, sizeof(best_scores));
                best_cell = shifted;
            }
        }
    }
    if (best_digit >= 0) {
        double min_on = 1.0, max_off = 0.0;
        bool any_on = false;
        for (int s = 0; s < 7; ++s) {
            if (best_scores[s] >= SEGMENT_THRESHOLD) {
                min_on = std::min(min_on, best_scores[s]);
                any_on = true;
            } else {
                max_off = std::max(max_off, best_scores[s]);
            }
        }
        if (!any_on)
            min_on = 0.0;
        margin = min_on - max_off;
    } else {
        margin = 0.0;
    }
    std::memcpy(scores, best_scores, sizeof(best_scores));
    out_cell = best_cell;
    return best_digit;
}

std::vector<Comp> filter_components(const std::vector<uint8_t> &mask, int w, int h) {
    std::vector<Comp> comps = label_components(mask.data(), w, h, true);
    std::sort(comps.begin(), comps.end(), [](const Comp &a, const Comp &b) { return a.area > b.area; });
    std::vector<Comp> kept;
    for (auto &c : comps) {
        int cw = c.x1 - c.x0, ch = c.y1 - c.y0;
        if (cw > 0.85 * w && ch > 0.85 * h && c.area < 0.5 * cw * ch)
            continue;
        if (ch > 0.7 * h && cw <= std::max(2, (int)(0.07 * w)) &&
            (c.x0 <= 0.08 * w || c.x1 >= 0.92 * w))
            continue;
        kept.push_back(c);
    }
    if (kept.empty())
        return kept;
    int max_h = 0;
    for (auto &c : kept)
        max_h = std::max(max_h, c.y1 - c.y0);
    double min_area = std::max(6.0, 0.008 * max_h * max_h);
    std::vector<Comp> area_kept;
    for (auto &c : kept) {
        if (c.area >= min_area || (c.y1 - c.y0) >= 0.2 * max_h)
            area_kept.push_back(c);
    }
    std::vector<Comp> filtered;
    for (auto &c : area_kept) {
        int cw = c.x1 - c.x0, ch = c.y1 - c.y0;
        bool contained = false;
        for (auto &o : area_kept) {
            int ow = o.x1 - o.x0, oh = o.y1 - o.y0;
            if (ow * oh <= cw * ch)
                continue;
            if (c.x0 >= o.x0 - 1 && c.y0 >= o.y0 - 1 && c.x1 <= o.x1 + 1 && c.y1 <= o.y1 + 1) {
                contained = true;
                break;
            }
        }
        if (!contained)
            filtered.push_back(c);
    }
    return filtered;
}

bool rects_mergeable(const Rect &a, const Rect &b, int max_gap) {
    int vgap = std::max(a.y, b.y) - std::min(a.y + a.h, b.y + b.h);
    int hover = std::min(a.x + a.w, b.x + b.w) - std::max(a.x, b.x);
    int aw = a.w, bw = b.w;
    int wider = std::max(aw, bw), narrower = std::min(aw, bw);
    return vgap <= max_gap && hover >= 0.4 * narrower && narrower >= 0.4 * wider;
}

std::vector<Rect> merge_glyphs(std::vector<Rect> glyphs, int max_gap = 2) {
    bool merged = true;
    while (merged) {
        merged = false;
        std::vector<Rect> out;
        while (!glyphs.empty()) {
            Rect g = glyphs.back();
            glyphs.pop_back();
            bool did_merge = false;
            for (auto &o : out) {
                if (rects_mergeable(g, o, max_gap)) {
                    int x1 = std::max(o.x + o.w, g.x + g.w);
                    int y1 = std::max(o.y + o.h, g.y + g.h);
                    o.x = std::min(o.x, g.x);
                    o.y = std::min(o.y, g.y);
                    o.w = x1 - o.x;
                    o.h = y1 - o.y;
                    merged = true;
                    did_merge = true;
                    break;
                }
            }
            if (!did_merge)
                out.push_back(g);
        }
        glyphs = out;
    }
    return glyphs;
}

bool split_once(const std::vector<uint8_t> &mask, int w, const Rect &g, double W,
                std::vector<Rect> &out) {
    int width = g.w;
    int k = (int)std::lround((double)width / W);
    if (k < 2 || std::abs(width - k * W) > 0.4 * W)
        return false;
    int lo = g.x + (int)(0.30 * width);
    int hi = g.x + (int)(0.70 * width);
    if (hi > g.x + width - 1)
        hi = g.x + width - 1;
    int colmin = -1, colmax = 0;
    for (int x = lo; x <= hi; ++x) {
        int sum = 0;
        for (int y = g.y; y < g.y + g.h; ++y)
            sum += mask[(size_t)y * w + x];
        if (colmin < 0 || sum < colmin)
            colmin = sum;
        colmax = std::max(colmax, sum);
    }
    if (colmax > 0 && colmin <= 0.15 * colmax && colmin >= 0) {
        int xm = lo;
        int best = colmax + 1;
        for (int x = lo; x <= hi; ++x) {
            int sum = 0;
            for (int y = g.y; y < g.y + g.h; ++y)
                sum += mask[(size_t)y * w + x];
            if (sum < best) {
                best = sum;
                xm = x;
            }
        }
        if (xm - g.x >= 0.25 * W && g.x + g.w - xm >= 0.25 * W) {
            out.push_back({g.x, g.y, xm - g.x, g.h});
            out.push_back({xm, g.y, g.x + g.w - xm, g.h});
            return true;
        }
    }
    int edges[16];
    for (int i = 0; i <= k; ++i)
        edges[i] = g.x + (int)std::lround((double)i * width / k);
    for (int i = 0; i < k; ++i)
        out.push_back({edges[i], g.y, edges[i + 1] - edges[i], g.h});
    return true;
}

std::vector<Rect> split_wide(const std::vector<uint8_t> &mask, int w, std::vector<Rect> glyphs, double W) {
    for (int iter = 0; iter < 4; ++iter) {
        bool changed = false;
        std::vector<Rect> out;
        for (auto &g : glyphs) {
            if (g.w > 1.35 * W) {
                std::vector<Rect> pieces;
                if (split_once(mask, w, g, W, pieces)) {
                    out.insert(out.end(), pieces.begin(), pieces.end());
                    changed = true;
                    continue;
                }
            }
            out.push_back(g);
        }
        glyphs = out;
        if (!changed)
            break;
    }
    return glyphs;
}

double median(std::vector<double> v) {
    if (v.empty())
        return 0.0;
    std::sort(v.begin(), v.end());
    size_t n = v.size();
    if (n % 2)
        return v[n / 2];
    return 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

bool parse_time(const std::vector<int> &digits, bool last_small,
                double &value, std::string &display) {
    if (digits.empty())
        return false;
    if (last_small && digits.size() >= 3) {
        int tenths = digits.back();
        int secs = digits[digits.size() - 3] * 10 + digits[digits.size() - 2];
        int mins = 0;
        for (size_t i = 0; i + 3 < digits.size(); ++i)
            mins = mins * 10 + digits[i];
        if (secs >= 60)
            return false;
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%d:%02d.%d", mins, secs, tenths);
        display = buf;
        value = mins * 60 + secs + tenths / 10.0;
        return true;
    }
    if (digits.size() == 4) {
        int mm = digits[0] * 10 + digits[1];
        int ss = digits[2] * 10 + digits[3];
        if (ss >= 60)
            return false;
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%d:%02d", mm, ss);
        display = buf;
        value = mm * 60 + ss;
        return true;
    }
    if (digits.size() == 3) {
        int secs = digits[1] * 10 + digits[2];
        if (secs >= 60)
            return false;
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%d:%02d", digits[0], secs);
        display = buf;
        value = digits[0] * 60 + secs;
        return true;
    }
    if (digits.size() == 2) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%d.%d", digits[0], digits[1]);
        display = buf;
        value = digits[0] + digits[1] / 10.0;
        return true;
    }
    return false;
}

}

Rect tighten_roi(const uint8_t *gray, int width, int height, int stride) {
    std::vector<uint8_t> mask;
    if (!preprocess(gray, width, height, stride, mask))
        return {};
    std::vector<uint8_t> dark(mask.size());
    for (size_t i = 0; i < mask.size(); ++i)
        dark[i] = mask[i] ? 0 : 1;
    std::vector<Comp> comps = label_components(dark.data(), width, height, false);
    const Comp *best = nullptr;
    for (auto &c : comps) {
        int cw = c.x1 - c.x0, ch = c.y1 - c.y0;
        if (c.x0 <= 0 || c.y0 <= 0 || c.x1 >= width || c.y1 >= height)
            continue;
        if (c.area < 0.05 * width * height || c.area < 0.4 * cw * ch)
            continue;
        if (!best || c.area > best->area)
            best = &c;
    }
    if (!best)
        return {};
    int x0 = std::max(0, best->x0 + 1);
    int y0 = std::max(0, best->y0 + 1);
    int x1 = std::min(width, best->x1 - 1);
    int y1 = std::min(height, best->y1 - 1);
    if (x1 - x0 < 24 || y1 - y0 < 12)
        return {};
    return {x0, y0, x1 - x0, y1 - y0};
}

Reading decode(const uint8_t *gray, int width, int height, int stride) {
    Reading r;
    std::vector<uint8_t> mask;
    if (!preprocess(gray, width, height, stride, mask)) {
        r.reason = "no display contrast";
        return r;
    }
    std::vector<Comp> comps = filter_components(mask, width, height);
    if (comps.empty()) {
        r.reason = "no glyphs";
        return r;
    }
    std::vector<Rect> rects;
    for (auto &c : comps)
        rects.push_back({c.x0, c.y0, c.x1 - c.x0, c.y1 - c.y0});
    std::vector<Rect> glyphs = merge_glyphs(rects);
    std::sort(glyphs.begin(), glyphs.end(),
              [](const Rect &a, const Rect &b) { return a.x < b.x; });
    int max_h = 0;
    for (auto &g : glyphs)
        max_h = std::max(max_h, g.h);
    double W = 0.75 * max_h;
    glyphs = split_wide(mask, width, glyphs, W);
    std::sort(glyphs.begin(), glyphs.end(),
              [](const Rect &a, const Rect &b) { return a.x < b.x; });

    std::vector<Glyph> objects;
    for (auto &g : glyphs) {
        Glyph gl;
        gl.x0 = g.x;
        gl.y0 = g.y;
        gl.x1 = g.x + g.w;
        gl.y1 = g.y + g.h;
        gl.area = 0;
        for (int y = g.y; y < g.y + g.h; ++y)
            for (int x = g.x; x < g.x + g.w; ++x)
                gl.area += mask[(size_t)y * width + x];
        bool is_dot = gl.h() < 0.55 * max_h && gl.w() < 0.6 * W;
        gl.kind = is_dot ? GlyphKind::Dot : GlyphKind::Digit;
        gl.small = gl.h() < 0.8 * max_h;
        objects.push_back(gl);
    }

    int baseline = 0;
    for (auto &o : objects)
        if (o.kind == GlyphKind::Digit)
            baseline = std::max(baseline, o.y1);
    for (auto &o : objects) {
        if (o.kind == GlyphKind::Dot && std::abs(o.y1 - baseline) > 0.35 * max_h)
            o.kind = GlyphKind::Reject;
    }

    std::vector<double> slants;
    for (auto &gl : objects) {
        if (gl.kind != GlyphKind::Digit)
            continue;
        int wg = std::max((int)std::lround(0.75 * gl.h()), 8);
        if (gl.w() < 0.6 * wg) {
            int inset = std::max(1, (int)std::lround(0.08 * wg));
            gl.cell = {gl.x1 - inset - wg, gl.y0, wg, gl.h()};
        } else {
            int wc = std::min(gl.w(), wg);
            gl.cell = {gl.x0 + (gl.w() - wc) / 2, gl.y0, wc, gl.h()};
        }
        double s = 0.0;
        if (slant_of(mask, width, height, gl.cell, s)) {
            gl.slant = s;
            gl.has_slant = true;
            slants.push_back(s);
        }
    }
    double global_slant = median(slants);

    std::vector<uint8_t> dilated = dilate3x3(mask, width, height);
    std::vector<Glyph> final_objects;
    std::vector<int> digits;
    std::vector<char> dot_after;
    std::vector<double> confidences;
    std::string reason;
    int last_digit_pos = -1;
    for (size_t i = 0; i < objects.size(); ++i)
        if (objects[i].kind == GlyphKind::Digit)
            last_digit_pos = (int)i;

    for (size_t i = 0; i < objects.size(); ++i) {
        Glyph gl = objects[i];
        if (gl.kind == GlyphKind::Dot) {
            if (!digits.empty() && (int)i < last_digit_pos)
                dot_after.back() = 1;
            final_objects.push_back(gl);
            continue;
        }
        if (gl.kind != GlyphKind::Digit) {
            final_objects.push_back(gl);
            continue;
        }
        if (!gl.has_slant && !slants.empty()) {
            gl.slant = global_slant;
            gl.has_slant = true;
        }
        double scores[7], margin;
        Rect cell;
        int digit = score_digit_best(dilated, width, height, gl.cell, gl.has_slant, gl.slant,
                                     scores, margin, cell);
        gl.cell = cell;
        std::memcpy(gl.scores, scores, sizeof(scores));
        int wg = std::max((int)std::lround(0.75 * gl.h()), 8);
        bool narrow_ok = false;
        if (digit < 0 && gl.w() < 0.6 * wg) {
            double ordered[7];
            std::memcpy(ordered, scores, sizeof(ordered));
            std::sort(ordered, ordered + 7, std::greater<double>());
            if (scores[1] >= 0.45 && scores[2] >= 0.45 &&
                std::min(scores[1], scores[2]) >= ordered[1] &&
                std::max(scores[1], scores[2]) >= ordered[0]) {
                digit = 1;
                double max_off = 0.0;
                for (int s = 0; s < 7; ++s)
                    if (s != 1 && s != 2)
                        max_off = std::max(max_off, scores[s]);
                margin = std::min(scores[1], scores[2]) - max_off;
                narrow_ok = true;
            }
        }
        gl.digit = digit;
        gl.margin = digit >= 0 ? margin : 0.0;
        if (digit < 0)
            reason = "unknown segment pattern";
        else if (margin < (narrow_ok ? NARROW_MIN_MARGIN : MIN_MARGIN))
            reason = "low segment margin";
        digits.push_back(digit);
        dot_after.push_back(0);
        confidences.push_back(std::max(margin, 0.0));
        final_objects.push_back(gl);
    }

    r.glyphs = final_objects;
    r.digits = digits;
    r.dot_after = dot_after;
    for (size_t i = 0; i < digits.size(); ++i) {
        r.raw += digits[i] < 0 ? "?" : std::to_string(digits[i]);
        if (dot_after[i])
            r.raw += ".";
    }
    if (digits.empty()) {
        r.reason = "no digits";
        return r;
    }
    double conf = 0.0;
    for (double c : confidences)
        conf += c;
    r.confidence = confidences.empty() ? 0.0 : conf / confidences.size();
    for (int d : digits) {
        if (d < 0) {
            r.reason = reason;
            return r;
        }
    }
    bool last_small = false;
    for (auto it = final_objects.rbegin(); it != final_objects.rend(); ++it) {
        if (it->kind == GlyphKind::Digit) {
            last_small = it->small;
            break;
        }
    }
    double value;
    std::string display;
    if (!parse_time(digits, last_small, value, display)) {
        r.reason = "implausible time";
        return r;
    }
    r.value_seconds = value;
    r.display = display;
    r.has_value = true;
    r.valid = reason.empty() && r.confidence >= MIN_MARGIN;
    r.reason = reason;
    return r;
}

std::string format_seconds_only(const std::string &display) {
    size_t dot = display.find('.');
    if (dot == std::string::npos)
        return display;
    return display.substr(0, dot);
}

ClockTracker::ClockTracker(Direction dir) : direction_(dir) {
    if (dir == Direction::Up)
        dir_ = 1;
    else if (dir == Direction::Down)
        dir_ = -1;
}

void ClockTracker::reset() {
    has_value_ = false;
    value_ = 0.0;
    display_.clear();
    raw_.clear();
    t_ = 0.0;
    confidence_ = 0.0;
    has_pending_ = false;
    has_last_change_ = false;
    up_votes_ = down_votes_ = 0;
    dir_ = direction_ == Direction::Up ? 1 : direction_ == Direction::Down ? -1 : 0;
}

bool ClockTracker::plausible(double delta, double dt) const {
    double max_step = dt + tolerance;
    if (delta == 0.0)
        return true;
    if (dir_ > 0)
        return delta >= -0.05 && delta <= max_step;
    if (dir_ < 0)
        return delta >= -max_step && delta <= 0.05;
    return std::abs(delta) <= max_step;
}

void ClockTracker::vote_direction(double delta) {
    if (direction_ != Direction::Auto)
        return;
    if (delta >= direction_evidence) {
        up_votes_++;
        down_votes_ = 0;
        if (up_votes_ >= 2)
            dir_ = 1;
    } else if (delta <= -direction_evidence) {
        down_votes_++;
        up_votes_ = 0;
        if (down_votes_ >= 2)
            dir_ = -1;
    }
    if (dir_ != 0)
        up_votes_ = down_votes_ = 0;
}

bool ClockTracker::accept(double value, const Reading &reading, double t) {
    bool changed = !has_value_ || value != value_;
    if (changed)
        last_change_t_ = t;
    has_value_ = true;
    value_ = value;
    display_ = reading.display;
    raw_ = reading.raw;
    t_ = t;
    confidence_ = reading.confidence;
    has_pending_ = false;
    return changed;
}

bool ClockTracker::is_stopped(double t) const {
    return has_last_change_ && (t - last_change_t_) >= stopped_after;
}

double ClockTracker::resync_seconds_for(double candidate, double t) const {
    double seconds = std::abs(candidate - value_) > resync_large_delta ? resync_large_seconds
                                                                       : resync_seconds;
    if (is_stopped(t))
        seconds = std::max(seconds, resync_frozen_seconds);
    return seconds;
}

TrackState ClockTracker::state(double t, bool changed) const {
    TrackState s;
    s.timestamp = t;
    s.confidence = confidence_;
    if (!has_value_) {
        s.stale = true;
        return s;
    }
    s.has_value = true;
    s.value = value_;
    s.display = display_;
    s.raw = raw_;
    s.valid = true;
    s.stale = (t - t_) > stale_after;
    s.stopped = !s.stale && is_stopped(t);
    s.changed = changed;
    return s;
}

TrackState ClockTracker::update(const Reading &reading, double t) {
    if (!reading.valid || !reading.has_value) {
        invalid_frames++;
        return state(t, false);
    }
    double value = std::round(reading.value_seconds * 1000.0) / 1000.0;
    if (!has_value_) {
        bool changed = accept(value, reading, t);
        return state(t, changed);
    }
    double dt = std::max(t - t_, 0.001);
    double delta = value - value_;
    if (plausible(delta, dt)) {
        vote_direction(delta);
        bool changed = accept(value, reading, t);
        if (!has_last_change_)
            last_change_t_ = t;
        return state(t, changed);
    }
    rejected++;
    if (!has_pending_ || std::abs(value - pending_) > 0.05) {
        pending_ = value;
        pending_start_ = t;
        has_pending_ = true;
    }
    if (has_pending_ && (t - pending_start_) >= resync_seconds_for(pending_, t)) {
        resyncs++;
        dir_ = direction_ == Direction::Up ? 1 : direction_ == Direction::Down ? -1 : 0;
        up_votes_ = down_votes_ = 0;
        double pending = pending_;
        bool changed = accept(pending, reading, t);
        return state(t, changed);
    }
    return state(t, false);
}

}
