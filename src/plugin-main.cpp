#include <obs-module.h>

#include <QApplication>
#include <QImage>
#include <QMessageBox>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include "clock_core.h"
#include "roi-picker.hpp"

namespace {

constexpr const char *S_ROI_X = "roi_x";
constexpr const char *S_ROI_Y = "roi_y";
constexpr const char *S_ROI_W = "roi_w";
constexpr const char *S_ROI_H = "roi_h";
constexpr const char *S_ROI_FW = "roi_frame_w";
constexpr const char *S_ROI_FH = "roi_frame_h";
constexpr const char *S_TIGHTEN = "tighten";
constexpr const char *S_SECONDS = "seconds_only";
constexpr const char *S_DIRECTION = "direction";
constexpr const char *S_TARGET = "target_source";

struct ClockFilter {
    obs_source_t *source = nullptr;

    int roi_x = 0, roi_y = 0, roi_w = 0, roi_h = 0;
    int roi_frame_w = 0, roi_frame_h = 0;
    bool tighten = true;
    bool seconds_only = false;
    int direction = 0;
    std::string target;

    seg7::ClockTracker tracker;
    std::mutex mutex;
    std::vector<uint8_t> frame_gray;
    int frame_w = 0, frame_h = 0;
    int frame_counter = 1000;
    std::string last_text;
    std::string status = "No reading yet.";

    ClockFilter() : tracker(seg7::ClockTracker::Direction::Auto) {}
};

seg7::ClockTracker::Direction tracker_direction(int value)
{
    if (value > 0)
        return seg7::ClockTracker::Direction::Up;
    if (value < 0)
        return seg7::ClockTracker::Direction::Down;
    return seg7::ClockTracker::Direction::Auto;
}

bool is_text_source_id(const char *id)
{
    if (!id)
        return false;
    return std::strcmp(id, "text_ft2_source_v2") == 0 ||
           std::strcmp(id, "text_ft2_source") == 0 ||
           std::strcmp(id, "text_gdiplus") == 0 ||
           std::strcmp(id, "text_gdiplus_v3") == 0;
}

bool enum_text_sources(void *param, obs_source_t *source)
{
    auto *names = static_cast<std::vector<std::string> *>(param);
    if (is_text_source_id(obs_source_get_id(source))) {
        const char *name = obs_source_get_name(source);
        if (name)
            names->push_back(name);
    }
    return true;
}

void set_text_source(const std::string &name, const std::string &text)
{
    if (name.empty())
        return;
    obs_source_t *source = obs_get_source_by_name(name.c_str());
    if (!source)
        return;
    if (is_text_source_id(obs_source_get_id(source))) {
        obs_data_t *settings = obs_data_create();
        obs_data_set_string(settings, "text", text.c_str());
        obs_source_update(source, settings);
        obs_data_release(settings);
    }
    obs_source_release(source);
}

void process_frame(ClockFilter *f, const struct obs_source_frame *frame)
{
    if (frame->width < 16 || frame->height < 8 || !frame->data[0])
        return;
    switch (frame->format) {
    case VIDEO_FORMAT_I420:
    case VIDEO_FORMAT_NV12:
    case VIDEO_FORMAT_I444:
    case VIDEO_FORMAT_I422:
    case VIDEO_FORMAT_Y800:
        break;
    default:
        return;
    }

    const uint8_t *plane = frame->data[0];
    const int stride = (int)frame->linesize[0];
    const int fw = (int)frame->width;
    const int fh = (int)frame->height;

    if ((f->frame_counter++ % 15) == 0) {
        std::lock_guard<std::mutex> lock(f->mutex);
        f->frame_gray.resize((size_t)fw * fh);
        for (int y = 0; y < fh; ++y)
            std::memcpy(f->frame_gray.data() + (size_t)y * fw, plane + (size_t)y * stride, fw);
        f->frame_w = fw;
        f->frame_h = fh;
    }

    int rx = f->roi_x, ry = f->roi_y, rw = f->roi_w, rh = f->roi_h;
    if (f->roi_frame_w > 0 && f->roi_frame_h > 0 &&
        (f->roi_frame_w != fw || f->roi_frame_h != fh)) {
        double sx = (double)fw / f->roi_frame_w;
        double sy = (double)fh / f->roi_frame_h;
        rx = (int)(rx * sx);
        ry = (int)(ry * sy);
        rw = std::max(8, (int)(rw * sx));
        rh = std::max(8, (int)(rh * sy));
    }
    rx = std::clamp(rx, 0, fw - 1);
    ry = std::clamp(ry, 0, fh - 1);
    rw = std::clamp(rw, 1, fw - rx);
    rh = std::clamp(rh, 1, fh - ry);
    if (rw < 16 || rh < 8)
        return;

    const uint8_t *crop = plane + (size_t)ry * stride + rx;
    seg7::Reading reading = seg7::decode(crop, rw, rh, stride);
    double t = (double)frame->timestamp / 1e9;
    seg7::TrackState state = f->tracker.update(reading, t);

    if (state.changed && state.has_value) {
        std::string text = f->seconds_only ? seg7::format_seconds_only(state.display)
                                           : state.display;
        {
            std::lock_guard<std::mutex> lock(f->mutex);
            f->last_text = text;
            char buf[160];
            std::snprintf(buf, sizeof(buf), "Reading: %s  confidence %.2f%s",
                          text.c_str(), state.confidence,
                          state.stale ? "  (display lost)" : state.stopped ? "  (clock stopped)" : "");
            f->status = buf;
        }
        set_text_source(f->target, text);
    }
}

const char *clock_filter_name(void *)
{
    return "7-Segment Clock Reader";
}

void *clock_filter_create(obs_data_t *settings, obs_source_t *source)
{
    auto *f = new ClockFilter();
    f->source = source;
    f->roi_x = (int)obs_data_get_int(settings, S_ROI_X);
    f->roi_y = (int)obs_data_get_int(settings, S_ROI_Y);
    f->roi_w = (int)obs_data_get_int(settings, S_ROI_W);
    f->roi_h = (int)obs_data_get_int(settings, S_ROI_H);
    f->roi_frame_w = (int)obs_data_get_int(settings, S_ROI_FW);
    f->roi_frame_h = (int)obs_data_get_int(settings, S_ROI_FH);
    f->tighten = obs_data_get_bool(settings, S_TIGHTEN);
    f->seconds_only = obs_data_get_bool(settings, S_SECONDS);
    f->direction = (int)obs_data_get_int(settings, S_DIRECTION);
    f->target = obs_data_get_string(settings, S_TARGET);
    f->tracker = seg7::ClockTracker(tracker_direction(f->direction));
    return f;
}

void clock_filter_destroy(void *data)
{
    delete static_cast<ClockFilter *>(data);
}

void clock_filter_update(void *data, obs_data_t *settings)
{
    auto *f = static_cast<ClockFilter *>(data);
    int roi_x = (int)obs_data_get_int(settings, S_ROI_X);
    int roi_y = (int)obs_data_get_int(settings, S_ROI_Y);
    int roi_w = (int)obs_data_get_int(settings, S_ROI_W);
    int roi_h = (int)obs_data_get_int(settings, S_ROI_H);
    int frame_w = (int)obs_data_get_int(settings, S_ROI_FW);
    int frame_h = (int)obs_data_get_int(settings, S_ROI_FH);
    int direction = (int)obs_data_get_int(settings, S_DIRECTION);
    bool seconds_only = obs_data_get_bool(settings, S_SECONDS);
    bool tighten = obs_data_get_bool(settings, S_TIGHTEN);
    const char *target = obs_data_get_string(settings, S_TARGET);

    bool roi_changed = roi_x != f->roi_x || roi_y != f->roi_y || roi_w != f->roi_w ||
                       roi_h != f->roi_h || frame_w != f->roi_frame_w;
    bool direction_changed = direction != f->direction;
    bool target_changed = f->target != (target ? target : "");

    f->roi_x = roi_x;
    f->roi_y = roi_y;
    f->roi_w = roi_w;
    f->roi_h = roi_h;
    f->roi_frame_w = frame_w;
    f->roi_frame_h = frame_h;
    f->direction = direction;
    f->seconds_only = seconds_only;
    f->tighten = tighten;
    f->target = target ? target : "";

    if (roi_changed || direction_changed)
        f->tracker = seg7::ClockTracker(tracker_direction(direction));
    if (target_changed && !f->last_text.empty())
        set_text_source(f->target, f->last_text);
}

void clock_filter_defaults(obs_data_t *settings)
{
    obs_data_set_default_int(settings, S_DIRECTION, 0);
    obs_data_set_default_bool(settings, S_SECONDS, true);
    obs_data_set_default_bool(settings, S_TIGHTEN, true);
    obs_data_set_default_string(settings, S_TARGET, "");
}

bool on_select_roi(obs_properties_t *props, obs_property_t *property, void *data)
{
    (void)property;
    (void)props;
    auto *f = static_cast<ClockFilter *>(data);
    QImage image;
    int frame_w = 0, frame_h = 0;
    {
        std::lock_guard<std::mutex> lock(f->mutex);
        if (!f->frame_gray.empty()) {
            image = QImage(f->frame_gray.data(), f->frame_w, f->frame_h, f->frame_w,
                           QImage::Format_Grayscale8)
                        .copy();
            frame_w = f->frame_w;
            frame_h = f->frame_h;
        }
    }
    if (image.isNull()) {
        QMessageBox::warning(QApplication::activeWindow(), "Clock Reader",
                             "No video frame captured yet.\n\n"
                             "Make sure the source this filter is attached to is active "
                             "and showing video, then try again.");
        return false;
    }
    QRect current(f->roi_x, f->roi_y, f->roi_w, f->roi_h);
    RoiPickerDialog dialog(image, current, QApplication::activeWindow());
    if (dialog.exec() != QDialog::Accepted)
        return false;
    QRect rect = dialog.selected();
    if (rect.width() < 8 || rect.height() < 8)
        return false;
    if (f->tighten && rect.width() > 0 && rect.height() > 0) {
        std::lock_guard<std::mutex> lock(f->mutex);
        if (!f->frame_gray.empty()) {
            seg7::Rect tight = seg7::tighten_roi(
                f->frame_gray.data() + (size_t)rect.y() * f->frame_w + rect.x(),
                rect.width(), rect.height(), f->frame_w);
            if (tight.w > 0 && tight.h > 0)
                rect = QRect(rect.x() + tight.x, rect.y() + tight.y, tight.w, tight.h);
        }
    }
    obs_data_t *settings = obs_source_get_settings(f->source);
    obs_data_set_int(settings, S_ROI_X, rect.x());
    obs_data_set_int(settings, S_ROI_Y, rect.y());
    obs_data_set_int(settings, S_ROI_W, rect.width());
    obs_data_set_int(settings, S_ROI_H, rect.height());
    obs_data_set_int(settings, S_ROI_FW, frame_w);
    obs_data_set_int(settings, S_ROI_FH, frame_h);
    obs_source_update(f->source, settings);
    obs_data_release(settings);
    return true;
}

obs_properties_t *clock_filter_properties(void *data)
{
    auto *f = static_cast<ClockFilter *>(data);
    obs_properties_t *props = obs_properties_create();

    obs_properties_add_int(props, S_ROI_X, "ROI X", 0, 16384, 1);
    obs_properties_add_int(props, S_ROI_Y, "ROI Y", 0, 16384, 1);
    obs_properties_add_int(props, S_ROI_W, "ROI width", 8, 16384, 1);
    obs_properties_add_int(props, S_ROI_H, "ROI height", 8, 16384, 1);
    obs_properties_add_button2(props, "select_roi", "Select ROI from video...", on_select_roi,
                               f);

    obs_property_t *target = obs_properties_add_list(props, S_TARGET, "Text source",
                                                     OBS_COMBO_TYPE_LIST,
                                                     OBS_COMBO_FORMAT_STRING);
    obs_property_list_add_string(target, "(none)", "");
    std::vector<std::string> names;
    obs_enum_sources(enum_text_sources, &names);
    std::sort(names.begin(), names.end());
    bool found = false;
    for (auto &name : names) {
        obs_property_list_add_string(target, name.c_str(), name.c_str());
        if (name == f->target)
            found = true;
    }
    if (!f->target.empty() && !found)
        obs_property_list_add_string(target, f->target.c_str(), f->target.c_str());

    obs_properties_add_bool(props, S_SECONDS, "Show seconds only (drop tenths)");
    obs_property_t *direction = obs_properties_add_list(
        props, S_DIRECTION, "Clock direction", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
    obs_property_list_add_int(direction, "Auto detect", 0);
    obs_property_list_add_int(direction, "Counting up", 1);
    obs_property_list_add_int(direction, "Counting down", -1);
    obs_properties_add_bool(props, S_TIGHTEN, "Auto-tighten ROI to display panel");

    std::string status;
    {
        std::lock_guard<std::mutex> lock(f->mutex);
        status = f->status;
    }
    obs_property_t *info = obs_properties_add_text(props, "status", status.c_str(),
                                                   OBS_TEXT_INFO);
    (void)info;
    return props;
}

struct obs_source_frame *clock_filter_video(void *data, struct obs_source_frame *frame)
{
    auto *f = static_cast<ClockFilter *>(data);
    if (frame && frame->data[0])
        process_frame(f, frame);
    return frame;
}

struct obs_source_info clock_filter_info = {};

void init_clock_filter_info()
{
    clock_filter_info.id = "seg7_clock_reader_filter";
    clock_filter_info.type = OBS_SOURCE_TYPE_FILTER;
    clock_filter_info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_ASYNC;
    clock_filter_info.get_name = clock_filter_name;
    clock_filter_info.create = clock_filter_create;
    clock_filter_info.destroy = clock_filter_destroy;
    clock_filter_info.get_defaults = clock_filter_defaults;
    clock_filter_info.get_properties = clock_filter_properties;
    clock_filter_info.update = clock_filter_update;
    clock_filter_info.filter_video = clock_filter_video;
}

}

OBS_DECLARE_MODULE()

extern "C" bool obs_module_load(void)
{
    init_clock_filter_info();
    obs_register_source(&clock_filter_info);
    blog(LOG_INFO, "[seg7-clock-reader] loaded");
    return true;
}

extern "C" void obs_module_unload(void)
{
    blog(LOG_INFO, "[seg7-clock-reader] unloaded");
}
