// Decoder for the basketball ("Kosz") scoreboard clock: MM:SS with a colon, an
// optional small tenths digit, possibly tilted and built from LED dot matrices.
// Kept separate from clock_core.cpp (Futsal) so the two can be tuned independently.
#include "kosz_core.h"

#include <algorithm>
#include <functional>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace kosz {
using seg7::Glyph;
using seg7::GlyphKind;
using seg7::Reading;
using seg7::Rect;

namespace {

constexpr double SEGMENT_THRESHOLD = 0.60;
constexpr double MIN_MARGIN = 0.15;
constexpr double NARROW_MIN_MARGIN = 0.08;
// Higher than Futsal: a decimal point touching a digit shifts its cell while
// still producing a valid but wrong pattern (9 -> 3), so search more often.
constexpr double REFINE_MARGIN = 0.6;
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
    if (slope < -0.6 || slope > 0.2)
        return false;
    for (auto &pts : band_pts) {
        double band_slope = fit_slope(pts);
        if (band_slope < -0.6 || band_slope > 0.2)
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

bool dot_sized(const Rect &r, int max_h) {
    return r.h < 0.3 * max_h && r.w < 0.45 * max_h;
}

bool rects_mergeable(const Rect &a, const Rect &b, int max_gap, int max_h) {
    // Never fuse the two dots of the colon into one glyph.
    if (dot_sized(a, max_h) && dot_sized(b, max_h))
        return false;
    int vgap = std::max(a.y, b.y) - std::min(a.y + a.h, b.y + b.h);
    int hover = std::min(a.x + a.w, b.x + b.w) - std::max(a.x, b.x);
    int aw = a.w, bw = b.w;
    int wider = std::max(aw, bw), narrower = std::min(aw, bw);
    return vgap <= max_gap && hover >= 0.4 * narrower && narrower >= 0.4 * wider;
}

std::vector<Rect> merge_glyphs(std::vector<Rect> glyphs, int max_h, int max_gap = 2) {
    bool merged = true;
    while (merged) {
        merged = false;
        std::vector<Rect> out;
        while (!glyphs.empty()) {
            Rect g = glyphs.back();
            glyphs.pop_back();
            bool did_merge = false;
            for (auto &o : out) {
                if (rects_mergeable(g, o, max_gap, max_h)) {
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

// Board tilt (dy/dx) from the vertical centres of the full-height glyphs
// (tops alone are biased: 1, 4 and 7 lack segment a). Theil-Sen keeps a glyph
// with an attached decimal point or a stray blob from skewing it.
double estimate_tilt(const std::vector<uint8_t> &mask, int w, int h) {
    std::vector<Comp> comps = filter_components(mask, w, h);
    int max_h = 0;
    for (auto &c : comps)
        max_h = std::max(max_h, c.y1 - c.y0);
    std::vector<std::pair<double, double>> centres;
    for (auto &c : comps)
        if (c.y1 - c.y0 >= 0.6 * max_h)
            centres.push_back({0.5 * (c.x0 + c.x1), 0.5 * (c.y0 + c.y1)});
    std::vector<double> slopes;
    for (size_t i = 0; i < centres.size(); ++i)
        for (size_t j = i + 1; j < centres.size(); ++j) {
            double dx = centres[j].first - centres[i].first;
            if (std::abs(dx) >= 0.5 * max_h)
                slopes.push_back((centres[j].second - centres[i].second) / dx);
        }
    if (slopes.empty())
        return 0.0;
    return std::clamp(median(slopes), -0.2, 0.2);
}

// Vertical shear that levels the board; uncovered corners get the median grey.
std::vector<uint8_t> shear_gray(const uint8_t *gray, int w, int h, int stride, double tilt) {
    uint64_t hist[256] = {0};
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            hist[gray[(size_t)y * stride + x]]++;
    uint8_t fill = (uint8_t)percentile_hist(hist, (uint64_t)w * h, 50.0);
    std::vector<uint8_t> out((size_t)w * h, fill);
    double cx = 0.5 * w;
    for (int x = 0; x < w; ++x) {
        int shift = (int)std::lround(tilt * (x - cx));
        for (int y = 0; y < h; ++y) {
            int sy = y + shift;
            if (sy >= 0 && sy < h)
                out[(size_t)y * w + x] = gray[(size_t)sy * stride + x];
        }
    }
    return out;
}

std::vector<uint8_t> erode(const std::vector<uint8_t> &mask, int w, int h, int r) {
    std::vector<uint8_t> rows(mask.size(), 0), out(mask.size(), 0);
    for (int y = 0; y < h; ++y)
        for (int x = r; x < w - r; ++x) {
            bool all = true;
            for (int k = -r; k <= r && all; ++k)
                all = mask[(size_t)y * w + x + k] != 0;
            rows[(size_t)y * w + x] = all;
        }
    for (int y = r; y < h - r; ++y)
        for (int x = 0; x < w; ++x) {
            bool all = true;
            for (int k = -r; k <= r && all; ++k)
                all = rows[(size_t)(y + k) * w + x] != 0;
            out[(size_t)y * w + x] = all;
        }
    return out;
}

// Decimal points touch neighbouring digits through narrow contacts. Erode the
// mask so those contacts break, group the eroded pieces of each glyph into
// horizontally disjoint runs, and cut the glyph at the weakest column between
// runs.
std::vector<Rect> split_touching(const std::vector<uint8_t> &mask, int w, int h,
                                 const std::vector<Rect> &glyphs, int max_h, int r) {
    std::vector<uint8_t> eroded = erode(mask, w, h, r);
    std::vector<Comp> pieces = label_components(eroded.data(), w, h, true);
    std::vector<Rect> out;
    for (const Rect &g : glyphs) {
        std::vector<std::pair<int, int>> runs;
        for (auto &p : pieces) {
            int cx = (p.x0 + p.x1) / 2, cy = (p.y0 + p.y1) / 2;
            if (p.area >= 2 && cx >= g.x && cx < g.x + g.w && cy >= g.y && cy < g.y + g.h)
                runs.push_back({p.x0, p.x1});
        }
        std::sort(runs.begin(), runs.end());
        std::vector<std::pair<int, int>> merged;
        for (auto &run : runs) {
            if (!merged.empty() && run.first <= merged.back().second)
                merged.back().second = std::max(merged.back().second, run.second);
            else
                merged.push_back(run);
        }
        if (merged.size() < 2) {
            out.push_back(g);
            continue;
        }
        std::vector<int> cuts{g.x};
        for (size_t i = 0; i + 1 < merged.size(); ++i) {
            int best_x = merged[i].second, best = -1;
            for (int x = merged[i].second; x <= merged[i + 1].first; ++x) {
                int sum = 0;
                for (int y = g.y; y < g.y + g.h; ++y)
                    sum += mask[(size_t)y * w + x];
                if (best < 0 || sum < best) {
                    best = sum;
                    best_x = x;
                }
            }
            cuts.push_back(best_x);
        }
        cuts.push_back(g.x + g.w);
        std::vector<Rect> parts;
        bool plausible = true;
        for (size_t i = 0; i + 1 < cuts.size(); ++i) {
            int x0 = cuts[i], x1 = cuts[i + 1], y0 = -1, y1 = -1;
            for (int y = g.y; y < g.y + g.h; ++y)
                for (int x = x0; x < x1; ++x)
                    if (mask[(size_t)y * w + x]) {
                        if (y0 < 0)
                            y0 = y;
                        y1 = y + 1;
                        break;
                    }
            if (y0 < 0 || x1 <= x0)
                continue;
            Rect part{x0, y0, x1 - x0, y1 - y0};
            // Only digits and dots are legitimate pieces; halves of a slanted
            // stroke mean the erosion cut through a digit.
            plausible = plausible && (part.h >= 0.6 * max_h || dot_sized(part, max_h));
            parts.push_back(part);
        }
        if (plausible && parts.size() >= 2)
            out.insert(out.end(), parts.begin(), parts.end());
        else
            out.push_back(g);
    }
    return out;
}

// True when the band looks like unlit panel (same brightness and texture as
// the gaps between digits) rather than a player or object covering digits.
bool panel_region_clean(const std::vector<uint8_t> &gray, const std::vector<uint8_t> &dilated,
                        int w, int x0, int x1, int y0, int y1, double bg_mean, double bg_sd) {
    double sum = 0.0, sq = 0.0;
    int n = 0, lit = 0;
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x) {
            double v = gray[(size_t)y * w + x];
            sum += v;
            sq += v * v;
            lit += dilated[(size_t)y * w + x];
            ++n;
        }
    if (n == 0)
        return true;
    double mean = sum / n;
    double sd = std::sqrt(std::max(0.0, sq / n - mean * mean));
    return lit <= 0.02 * n && std::abs(mean - bg_mean) <= std::max(8.0, bg_sd) &&
           sd <= std::max(8.0, 1.5 * bg_sd);
}

// Digit cell placed from the upper 60% of the glyph, where a touching decimal
// point never reaches; the slant shifts that band's centre relative to the
// glyph's vertical centre, and its width includes |slant| * band height.
Rect upright_cell(const std::vector<uint8_t> &mask, int w, const Glyph &g, double slant) {
    int band_end = g.y0 + (int)(0.6 * g.h());
    int x0 = g.x1, x1 = g.x0;
    for (int y = g.y0; y < band_end; ++y)
        for (int x = g.x0; x < g.x1; ++x)
            if (mask[(size_t)y * w + x]) {
                x0 = std::min(x0, x);
                x1 = std::max(x1, x + 1);
            }
    if (x1 <= x0)
        return g.cell;
    double band_mid = g.y0 + 0.3 * g.h();
    double centre = 0.5 * (x0 + x1) - slant * (band_mid - (g.y0 + 0.5 * g.h()));
    int wc = (int)std::lround((x1 - x0) - std::abs(slant) * 0.6 * g.h());
    wc = std::clamp(wc, (int)(0.35 * g.h()), (int)(0.75 * g.h()));
    return {(int)std::lround(centre - 0.5 * wc), g.y0, wc, g.h()};
}

// Accepted layouts (big digits, optional small or dot-separated tenths):
//   MM:SS[.t]   M:SS[.t]   SS.t   S.t
bool parse_time(const std::vector<int> &digits, const std::vector<char> &small,
                const std::vector<char> &sep_after, double &value, std::string &display) {
    size_t n = digits.size();
    if (n == 0)
        return false;
    size_t nb = n;
    bool has_tenths = false;
    int tenths = 0;
    if (n >= 2 && small[n - 1]) {
        has_tenths = true;
        tenths = digits[n - 1];
        nb = n - 1;
    } else if (n >= 2 && n <= 3 && sep_after[n - 2] == '.') {
        has_tenths = true;
        tenths = digits[n - 1];
        nb = n - 1;
    }
    if (nb > 4)
        return false;
    for (size_t i = 0; i < nb; ++i) {
        if (small[i])
            return false;
        bool minutes_sep = nb >= 3 && i == nb - 3;
        bool decimal_sep = has_tenths && i == nb - 1;
        if (sep_after[i] && !minutes_sep && !(decimal_sep && sep_after[i] == '.'))
            return false;
    }
    if (nb <= 2 && !has_tenths)
        return false;
    int mins = 0;
    for (size_t i = 0; i + 2 < nb; ++i)
        mins = mins * 10 + digits[i];
    int secs = nb >= 2 ? digits[nb - 2] * 10 + digits[nb - 1] : digits[0];
    if (secs >= 60)
        return false;
    char buf[32];
    if (nb >= 3)
        std::snprintf(buf, sizeof(buf), "%d:%02d", mins, secs);
    else
        std::snprintf(buf, sizeof(buf), nb == 2 ? "%02d" : "%d", secs);
    display = buf;
    if (has_tenths) {
        std::snprintf(buf, sizeof(buf), ".%d", tenths);
        display += buf;
    }
    value = mins * 60 + secs + (has_tenths ? tenths / 10.0 : 0.0);
    return true;
}

}

Reading decode(const uint8_t *gray, int width, int height, int stride) {
    Reading r;
    std::vector<uint8_t> mask;
    if (!preprocess(gray, width, height, stride, mask)) {
        r.reason = "no display contrast";
        return r;
    }
    // Work on a levelled copy of the ROI.
    double tilt = estimate_tilt(mask, width, height);
    std::vector<uint8_t> level = shear_gray(gray, width, height, stride,
                                            std::abs(tilt) > 0.012 ? tilt : 0.0);
    if (!preprocess(level.data(), width, height, width, mask)) {
        r.reason = "no display contrast";
        return r;
    }
    std::vector<Comp> comps = filter_components(mask, width, height);
    if (comps.empty()) {
        r.reason = "no glyphs";
        return r;
    }
    std::vector<Rect> rects;
    int max_h = 0;
    for (auto &c : comps) {
        rects.push_back({c.x0, c.y0, c.x1 - c.x0, c.y1 - c.y0});
        max_h = std::max(max_h, c.y1 - c.y0);
    }
    std::vector<Rect> glyphs = merge_glyphs(rects, max_h);
    int erode_r = (int)(0.03 * max_h);
    if (erode_r >= 2)
        glyphs = split_touching(mask, width, height, glyphs, max_h, erode_r);
    max_h = 0;
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
        for (int y = g.y; y < g.y + g.h; ++y)
            for (int x = g.x; x < g.x + g.w; ++x)
                gl.area += mask[(size_t)y * width + x];
        bool is_dot = gl.h() < 0.55 * max_h && gl.w() < 0.6 * W;
        gl.kind = is_dot ? GlyphKind::Dot : GlyphKind::Digit;
        gl.small = gl.h() < 0.8 * max_h;
        objects.push_back(gl);
    }

    // Reference digit size from the full-height digits; the median ignores a
    // digit enlarged by an attached decimal point.
    std::vector<double> big_h, big_top, big_bottom;
    for (auto &o : objects)
        if (o.kind == GlyphKind::Digit && !o.small) {
            big_h.push_back(o.h());
            big_top.push_back(o.y0);
            big_bottom.push_back(o.y1);
        }
    int ref_h = big_h.empty() ? max_h : (int)std::lround(median(big_h));
    double top = big_top.empty() ? 0.0 : median(big_top);
    double baseline = big_bottom.empty() ? max_h : median(big_bottom);
    std::vector<double> big_w;
    for (auto &o : objects)
        if (o.kind == GlyphKind::Digit && !o.small && o.w() >= 0.5 * o.h())
            big_w.push_back(o.w());
    // Only 1s visible (e.g. 11:11): fall back to this font's width/height ratio.
    double ref_w = big_w.empty() ? 0.65 * ref_h : median(big_w);

    // Separator per object: ':' for colon dots (upper and lower dot stacked),
    // '.' for a decimal point on the baseline.
    std::vector<char> sep(objects.size(), 0);
    for (size_t i = 0; i < objects.size(); ++i) {
        Glyph &a = objects[i];
        if (a.kind != GlyphKind::Dot || sep[i])
            continue;
        for (size_t j = i + 1; j < objects.size(); ++j) {
            Glyph &b = objects[j];
            if (b.kind != GlyphKind::Dot || sep[j])
                continue;
            double dx = std::abs(0.5 * (a.x0 + a.x1) - 0.5 * (b.x0 + b.x1));
            double dy = std::abs(0.5 * (a.y0 + a.y1) - 0.5 * (b.y0 + b.y1));
            bool inside = std::min(a.y0, b.y0) >= top - 0.15 * ref_h &&
                          std::max(a.y1, b.y1) <= baseline + 0.1 * ref_h;
            if (dx <= 0.3 * W && dy >= 0.2 * ref_h && dy <= 0.8 * ref_h && inside) {
                sep[i] = ':';
                sep[j] = ':';
                b.kind = GlyphKind::Reject;
                break;
            }
        }
        if (!sep[i]) {
            if (std::abs(a.y1 - baseline) <= 0.35 * ref_h)
                sep[i] = '.';
            else
                a.kind = GlyphKind::Reject;
        }
    }

    std::vector<double> slants;
    for (auto &gl : objects) {
        if (gl.kind != GlyphKind::Digit)
            continue;
        int wg = std::max((int)std::lround(0.75 * std::min(gl.h(), ref_h)), 8);
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
    std::vector<char> small;
    std::vector<char> sep_after;
    std::vector<double> confidences;
    std::string reason;
    int last_digit_pos = -1;
    for (size_t i = 0; i < objects.size(); ++i)
        if (objects[i].kind == GlyphKind::Digit)
            last_digit_pos = (int)i;

    for (size_t i = 0; i < objects.size(); ++i) {
        Glyph gl = objects[i];
        if (gl.kind == GlyphKind::Dot) {
            // A colon wins over a decimal point competing for the same gap.
            if (!digits.empty() && (int)i < last_digit_pos && sep_after.back() != ':')
                sep_after.back() = sep[i];
            final_objects.push_back(gl);
            continue;
        }
        if (gl.kind != GlyphKind::Digit) {
            final_objects.push_back(gl);
            continue;
        }
        // The font slant is the same for every digit; per-glyph estimates get
        // skewed by a decimal point touching the digit's bottom-right corner.
        if (!slants.empty()) {
            gl.slant = global_slant;
            gl.has_slant = true;
        }
        if (gl.w() >= 0.6 * gl.cell.w)
            gl.cell = upright_cell(mask, width, gl, gl.has_slant ? gl.slant : 0.0);
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
        // Wider than tall: neighbouring digits fused (motion blur, glare).
        if (!gl.small && gl.w() > 0.95 * gl.h())
            reason = "merged digits";
        else if (digit < 0)
            reason = "unknown segment pattern";
        else if (margin < (narrow_ok ? NARROW_MIN_MARGIN : MIN_MARGIN))
            reason = "low segment margin";
        digits.push_back(digit);
        small.push_back(gl.small ? 1 : 0);
        sep_after.push_back(0);
        confidences.push_back(std::max(margin, 0.0));
        final_objects.push_back(gl);
    }

    r.glyphs = final_objects;
    r.digits = digits;
    r.dot_after = sep_after;
    for (size_t i = 0; i < digits.size(); ++i) {
        r.raw += digits[i] < 0 ? "?" : std::to_string(digits[i]);
        if (sep_after[i])
            r.raw += sep_after[i];
    }
    if (digits.empty()) {
        r.reason = "no digits";
        return r;
    }
    double conf = 0.0;
    for (double c : confidences)
        conf += c;
    r.confidence = conf / confidences.size();
    // A digit cut by the ROI edge can still look like a valid digit (a
    // clipped 2 reads as 1), so require a margin around the clock.
    for (auto &g : final_objects)
        if (g.kind == GlyphKind::Digit && (g.x0 <= 0 || g.x1 >= width)) {
            r.reason = "clock touches ROI edge; enlarge ROI";
            return r;
        }
    for (int d : digits) {
        if (d < 0 || reason == "merged digits") {
            r.reason = reason;
            return r;
        }
    }
    double value;
    std::string display;
    if (!parse_time(digits, small, sep_after, value, display)) {
        r.reason = digits.size() < 3 ? "incomplete clock; enlarge ROI" : "implausible time";
        return r;
    }
    // A layout shorter than MM:SS.t is either a blank leading digit / missing
    // tenths, or someone covering part of the clock. Accept it only if the
    // empty slot at the ROI edge looks like unlit panel.
    const Glyph *first = nullptr, *last = nullptr;
    int big = 0;
    for (auto &g : final_objects)
        if (g.kind == GlyphKind::Digit) {
            first = first ? first : &g;
            last = &g;
            big += g.small ? 0 : 1;
        }
    if (big < 4 || !last->small) {
        int y0 = std::max(0, (int)top), y1 = std::min(height, (int)baseline);
        double sum = 0.0, sq = 0.0;
        int n = 0;
        for (int y = y0; y < y1; ++y)
            for (int x = first->x0; x < last->x1; ++x)
                if (!dilated[(size_t)y * width + x]) {
                    double v = level[(size_t)y * width + x];
                    sum += v;
                    sq += v * v;
                    ++n;
                }
        double bg_mean = n ? sum / n : 0.0;
        double bg_sd = n ? std::sqrt(std::max(0.0, sq / n - bg_mean * bg_mean)) : 0.0;
        bool left_ok = first->x0 <= 0.6 * ref_w ||
                       panel_region_clean(level, dilated, width, 0, first->x0 - 2, y0, y1,
                                          bg_mean, bg_sd);
        bool right_ok = width - last->x1 <= 0.5 * ref_w ||
                        panel_region_clean(level, dilated, width, last->x1 + 2, width, y0, y1,
                                           bg_mean, bg_sd);
        // A digit hidden between visible ones leaves a gap of a whole digit
        // pitch. A narrow 1 sits at the right of its slot, so centre it there.
        bool spacing_ok = true;
        double prev_centre = -1.0;
        bool colon_between = false;
        for (auto &g : final_objects) {
            if (g.kind == GlyphKind::Dot && g.y1 < baseline - 0.2 * ref_h)
                colon_between = true;
            if (g.kind != GlyphKind::Digit || g.small)
                continue;
            double centre = g.w() < 0.6 * ref_w ? g.x1 - 0.5 * ref_w : 0.5 * (g.x0 + g.x1);
            if (prev_centre >= 0.0 &&
                centre - prev_centre > (colon_between ? 1.9 : 1.45) * ref_w)
                spacing_ok = false;
            prev_centre = centre;
            colon_between = false;
        }
        if (!left_ok || !right_ok || !spacing_ok) {
            r.reason = "clock partly covered";
            return r;
        }
    }
    r.value_seconds = value;
    r.display = display;
    r.has_value = true;
    r.valid = reason.empty() && r.confidence >= MIN_MARGIN;
    r.reason = reason;
    return r;
}

}
