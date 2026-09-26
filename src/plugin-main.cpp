#include <obs-module.h>

#include <QApplication>
#include <QAbstractButton>
#include <QImage>
#include <QMessageBox>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <memory>
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
constexpr int MAX_FRAME_SIZE = 16384;

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
    std::vector<uint8_t> crop_gray;
    int frame_w = 0, frame_h = 0;
    unsigned frame_counter = 0;
    std::string last_text;
    std::string last_target_uuid;
    std::string last_display;
    std::string status = "Wybierz źródło tekstu i zaznacz zegar na obrazie.";
    double last_timestamp = -1.0;
    std::chrono::steady_clock::time_point last_frame_wall;

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

int bounded_setting(obs_data_t *settings, const char *name, int minimum = 0,
                    int maximum = MAX_FRAME_SIZE)
{
    return (int)std::clamp(obs_data_get_int(settings, name), (long long)minimum,
                          (long long)maximum);
}

bool is_text_source_id(const char *id)
{
    if (!id)
        return false;
    return std::strcmp(id, "text_ft2_source") == 0 ||
           std::strcmp(id, "text_gdiplus") == 0;
}

bool enum_text_sources(void *param, obs_source_t *source)
{
    auto *names = static_cast<std::vector<std::string> *>(param);
    if (is_text_source_id(obs_source_get_unversioned_id(source))) {
        const char *name = obs_source_get_name(source);
        if (name)
            names->push_back(name);
    }
    return true;
}

bool update_text_source(const std::string &name, const std::string &text, bool publish,
                        std::string &target_uuid)
{
    if (name.empty())
        return false;
    obs_source_t *source = obs_get_source_by_name(name.c_str());
    if (!source)
        return false;
    bool available = !obs_source_removed(source) &&
                     is_text_source_id(obs_source_get_unversioned_id(source));
    std::string current_uuid = obs_source_get_uuid(source);
    if (available && (publish || current_uuid != target_uuid)) {
        obs_data_t *settings = obs_data_create();
        obs_data_set_string(settings, "text", text.c_str());
        obs_source_update(source, settings);
        obs_data_release(settings);
    }
    target_uuid = current_uuid;
    obs_source_release(source);
    return available;
}

// Convert only the requested rows. Planar 8-bit formats can use their Y plane directly.
bool gray_row(const obs_source_frame *frame, int y, int x, int count, uint8_t *out)
{
    if (!frame || !frame->data[0] || !out || frame->width > MAX_FRAME_SIZE ||
        frame->height > MAX_FRAME_SIZE || y < 0 || y >= (int)frame->height || x < 0 ||
        count <= 0 || x >= (int)frame->width || count > (int)frame->width - x)
        return false;
    const size_t stride = frame->linesize[0];
    const int stored_y = frame->flip ? (int)frame->height - 1 - y : y;
    const uint8_t *row = frame->data[0] + (size_t)stored_y * stride;
    switch (frame->format) {
    case VIDEO_FORMAT_I420:
    case VIDEO_FORMAT_NV12:
    case VIDEO_FORMAT_I444:
    case VIDEO_FORMAT_I422:
    case VIDEO_FORMAT_I40A:
    case VIDEO_FORMAT_I42A:
    case VIDEO_FORMAT_YUVA:
    case VIDEO_FORMAT_Y800:
        if (stride < frame->width)
            return false;
        std::memcpy(out, row + x, count);
        return true;
    case VIDEO_FORMAT_YUY2:
    case VIDEO_FORMAT_YVYU:
    case VIDEO_FORMAT_UYVY:
        if (stride < (size_t)frame->width * 2)
            return false;
        for (int i = 0; i < count; ++i)
            out[i] = row[2 * (x + i) + (frame->format == VIDEO_FORMAT_UYVY ? 1 : 0)];
        return true;
    case VIDEO_FORMAT_BGRA:
    case VIDEO_FORMAT_BGRX:
    case VIDEO_FORMAT_RGBA:
    case VIDEO_FORMAT_BGR3: {
        const int channels = frame->format == VIDEO_FORMAT_BGR3 ? 3 : 4;
        if (stride < (size_t)frame->width * channels)
            return false;
        for (int i = 0; i < count; ++i) {
            const uint8_t *p = row + (size_t)(x + i) * channels;
            int r = frame->format == VIDEO_FORMAT_RGBA ? p[0] : p[2];
            int g = p[1];
            int b = frame->format == VIDEO_FORMAT_RGBA ? p[2] : p[0];
            out[i] = (uint8_t)((77 * r + 150 * g + 29 * b + 128) >> 8);
        }
        return true;
    }
    case VIDEO_FORMAT_AYUV:
        if (stride < (size_t)frame->width * 4)
            return false;
        for (int i = 0; i < count; ++i)
            // OBS uploads AYUV as BGRA: bytes in memory are V, U, Y, A.
            out[i] = row[4 * (x + i) + 2];
        return true;
    case VIDEO_FORMAT_I010:
    case VIDEO_FORMAT_P010:
    case VIDEO_FORMAT_I210:
    case VIDEO_FORMAT_I412:
    case VIDEO_FORMAT_YA2L:
    case VIDEO_FORMAT_P216:
    case VIDEO_FORMAT_P416:
        if (stride < (size_t)frame->width * 2)
            return false;
        for (int i = 0; i < count; ++i) {
            const uint8_t *p = row + 2 * (x + i);
            uint16_t sample = (uint16_t)p[0] | ((uint16_t)p[1] << 8);
            out[i] = (frame->format == VIDEO_FORMAT_P010 || frame->format == VIDEO_FORMAT_P216 ||
                      frame->format == VIDEO_FORMAT_P416) ? (uint8_t)(sample >> 8)
                     : (frame->format == VIDEO_FORMAT_I412 || frame->format == VIDEO_FORMAT_YA2L)
                         ? (uint8_t)(sample >> 4)
                     : (uint8_t)(sample >> 2);
        }
        return true;
    default:
        return false;
    }
}

void process_frame(ClockFilter *f, const struct obs_source_frame *frame)
{
    if (frame->width < 16 || frame->height < 8 || frame->width > MAX_FRAME_SIZE ||
        frame->height > MAX_FRAME_SIZE || !frame->data[0] || frame->linesize[0] == 0)
        return;
    const int fw = (int)frame->width;
    const int fh = (int)frame->height;
    std::lock_guard<std::mutex> lock(f->mutex);
    f->last_frame_wall = std::chrono::steady_clock::now();
    bool snapshot_due = f->frame_counter == 0 || f->frame_w != fw || f->frame_h != fh;
    f->frame_counter = (f->frame_counter + 1) % 15;
    if (snapshot_due) {
        f->frame_gray.resize((size_t)fw * fh);
        bool captured = true;
        for (int y = 0; y < fh; ++y) {
            if (!gray_row(frame, y, 0, fw, f->frame_gray.data() + (size_t)y * fw)) {
                captured = false;
                break;
            }
        }
        if (captured) {
            f->frame_w = fw;
            f->frame_h = fh;
        } else {
            f->frame_gray.clear();
        }
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
    if (rw < 16 || rh < 8) {
        f->status = "Zaznacz cały zegar przyciskiem „Zaznacz zegar na obrazie…”.";
        return;
    }

    f->crop_gray.resize((size_t)rw * rh);
    for (int y = 0; y < rh; ++y) {
        if (!gray_row(frame, ry + y, rx, rw, f->crop_gray.data() + (size_t)y * rw)) {
            f->status = "Nie można odczytać obrazu. Sprawdź źródło wideo; spróbuj formatu NV12, I420 lub BGRA.";
            return;
        }
    }
    seg7::Reading reading = seg7::decode(f->crop_gray.data(), rw, rh, rw);
    double t = (double)frame->timestamp / 1e9;
    if (f->last_timestamp >= 0.0 && t + 0.001 < f->last_timestamp)
        f->tracker.reset();
    f->last_timestamp = t;
    seg7::TrackState state = f->tracker.update(reading, t);
    if (state.has_value) {
        f->last_display = state.display;
        std::string text = f->seconds_only ? seg7::format_seconds_only(state.display) : state.display;
        if (!reading.valid) {
            f->status = "Brak nowego odczytu. Zachowano poprzedni czas: " + text +
                        ". Powód: " + reading.reason + ".";
        } else if (state.display != reading.display) {
            f->status = "Potwierdzam nowy odczyt: " + reading.display +
                        ". Zachowano poprzedni czas: " + text + ".";
        } else {
            char buf[320];
            std::snprintf(buf, sizeof(buf), "Odczyt: %s  pewność: %.0f%%%s", text.c_str(),
                          state.confidence * 100.0,
                          state.stopped ? "  (zegar zatrzymany)" : "");
            f->status = buf;
        }
    } else {
        f->status = reading.valid ? "Czekam na stabilny odczyt zegara."
                                   : "Nie udało się odczytać zegara: " + reading.reason;
    }
}

// Publish from one OBS video-thread callback. Settings changes and incoming
// frames only change the desired display; they cannot race to publish old text.
void clock_filter_tick(void *data, float)
{
    auto *f = static_cast<ClockFilter *>(data);
    if (!obs_source_enabled(f->source))
        return;
    std::string target, display, text, target_uuid;
    bool seconds_only, publish;
    {
        std::lock_guard<std::mutex> lock(f->mutex);
        if (f->last_display.empty())
            return;
        target = f->target;
        target_uuid = f->last_target_uuid;
        display = f->last_display;
        seconds_only = f->seconds_only;
        text = seconds_only ? seg7::format_seconds_only(display) : display;
        publish = text != f->last_text;
    }
    bool available = update_text_source(target, text, publish, target_uuid);
    std::lock_guard<std::mutex> lock(f->mutex);
    if (f->target != target || f->seconds_only != seconds_only || f->last_display != display)
        return;
    if (available) {
        f->last_text = text;
        f->last_target_uuid = target_uuid;
    } else {
        f->last_text.clear();
        f->last_target_uuid.clear();
        f->status = target.empty() ? "Wybierz źródło tekstu, w którym ma się pojawić czas."
                                   : "Nie znaleziono źródła tekstu „" + target + "”. Wybierz je ponownie z listy.";
    }
}

const char *clock_filter_name(void *)
{
    return "Czytnik zegara 7-segmentowego";
}

void *clock_filter_create(obs_data_t *settings, obs_source_t *source)
{
    auto *f = new ClockFilter();
    f->source = source;
    f->roi_x = bounded_setting(settings, S_ROI_X);
    f->roi_y = bounded_setting(settings, S_ROI_Y);
    f->roi_w = bounded_setting(settings, S_ROI_W);
    f->roi_h = bounded_setting(settings, S_ROI_H);
    f->roi_frame_w = bounded_setting(settings, S_ROI_FW);
    f->roi_frame_h = bounded_setting(settings, S_ROI_FH);
    f->tighten = obs_data_get_bool(settings, S_TIGHTEN);
    f->seconds_only = obs_data_get_bool(settings, S_SECONDS);
    f->direction = bounded_setting(settings, S_DIRECTION, -1, 1);
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
    int roi_x = bounded_setting(settings, S_ROI_X);
    int roi_y = bounded_setting(settings, S_ROI_Y);
    int roi_w = bounded_setting(settings, S_ROI_W);
    int roi_h = bounded_setting(settings, S_ROI_H);
    int frame_w = bounded_setting(settings, S_ROI_FW);
    int frame_h = bounded_setting(settings, S_ROI_FH);
    int direction = bounded_setting(settings, S_DIRECTION, -1, 1);
    bool seconds_only = obs_data_get_bool(settings, S_SECONDS);
    bool tighten = obs_data_get_bool(settings, S_TIGHTEN);
    const char *target = obs_data_get_string(settings, S_TARGET);
    std::string target_name = target ? target : "";
    {
        std::lock_guard<std::mutex> lock(f->mutex);

        bool roi_changed = roi_x != f->roi_x || roi_y != f->roi_y || roi_w != f->roi_w ||
                           roi_h != f->roi_h || frame_w != f->roi_frame_w ||
                           frame_h != f->roi_frame_h;
        bool direction_changed = direction != f->direction;
        bool target_changed = f->target != target_name;
        bool format_changed = seconds_only != f->seconds_only;

        f->roi_x = roi_x;
        f->roi_y = roi_y;
        f->roi_w = roi_w;
        f->roi_h = roi_h;
        f->roi_frame_w = frame_w;
        f->roi_frame_h = frame_h;
        f->direction = direction;
        f->seconds_only = seconds_only;
        f->tighten = tighten;
        f->target = target_name;

        if (roi_changed || direction_changed) {
            f->tracker = seg7::ClockTracker(tracker_direction(direction));
            f->last_text.clear();
            f->last_display.clear();
            f->last_timestamp = -1.0;
        }
        if (target_changed || format_changed)
            f->last_text.clear();
    }
}

void clock_filter_defaults(obs_data_t *settings)
{
    obs_data_set_default_int(settings, S_DIRECTION, 0);
    obs_data_set_default_bool(settings, S_SECONDS, true);
    obs_data_set_default_bool(settings, S_TIGHTEN, true);
    obs_data_set_default_string(settings, S_TARGET, "");
}

void show_warning(const char *message)
{
    QMessageBox box(QMessageBox::Warning, QString::fromUtf8("Czytnik zegara"),
                     QString::fromUtf8(message), QMessageBox::Ok, QApplication::activeWindow());
    box.button(QMessageBox::Ok)->setText(QString::fromUtf8("Rozumiem"));
    box.exec();
}

bool on_select_roi(obs_properties_t *props, obs_property_t *property, void *data)
{
    (void)property;
    (void)props;
    auto *f = static_cast<ClockFilter *>(data);
    if (!f)
        return false;
    // A modal Qt dialog runs a nested event loop: the filter can be removed
    // while it is open, so keep its OBS source (and implementation) alive.
    std::unique_ptr<obs_source_t, decltype(&obs_source_release)> source_ref(
        obs_source_get_ref(f->source), obs_source_release);
    if (!source_ref)
        return false;
    QImage image;
    int frame_w = 0, frame_h = 0;
    QRect current;
    bool tighten = false;
    {
        std::lock_guard<std::mutex> lock(f->mutex);
        if (!f->frame_gray.empty()) {
            image = QImage(f->frame_gray.data(), f->frame_w, f->frame_h, f->frame_w,
                           QImage::Format_Grayscale8)
                        .copy();
            frame_w = f->frame_w;
            frame_h = f->frame_h;
        }
        current = QRect(f->roi_x, f->roi_y, f->roi_w, f->roi_h);
        if (f->roi_frame_w > 0 && f->roi_frame_h > 0 &&
            (f->roi_frame_w != frame_w || f->roi_frame_h != frame_h)) {
            current = QRect((int)((double)current.x() * frame_w / f->roi_frame_w),
                            (int)((double)current.y() * frame_h / f->roi_frame_h),
                            (int)((double)current.width() * frame_w / f->roi_frame_w),
                            (int)((double)current.height() * frame_h / f->roi_frame_h));
        }
        tighten = f->tighten;
    }
    if (image.isNull()) {
        show_warning("Nie ma jeszcze obrazu do zaznaczenia.\n\n"
                     "Włącz źródło wideo i sprawdź, czy pokazuje obraz w OBS. "
                     "Jeśli korzystasz z pliku wideo, uruchom odtwarzanie. Następnie spróbuj ponownie.\n\n"
                     "Użyj źródła „Multimedia” (plik wideo) lub „Urządzenie do przechwytywania wideo”. "
                     "Ten filtr nie odczytuje bezpośrednio przechwytywania ekranu, okna ani gry.");
        return false;
    }
    RoiPickerDialog dialog(image, current, QApplication::activeWindow());
    if (dialog.exec() != QDialog::Accepted)
        return false;
    QRect rect = dialog.selected();
    rect = rect.intersected(QRect(0, 0, image.width(), image.height()));
    if (rect.width() < 16 || rect.height() < 8) {
        show_warning("Zaznaczenie jest za małe.\n\n"
                     "Przeciągnij ramkę wokół całego zegara, razem z minutami i sekundami.");
        return false;
    }
    if (tighten && rect.x() >= 0 && rect.y() >= 0 && rect.right() < image.width() &&
        rect.bottom() < image.height()) {
        seg7::Rect tight = seg7::tighten_roi(image.constBits() + (size_t)rect.y() * image.bytesPerLine() + rect.x(),
                                             rect.width(), rect.height(), image.bytesPerLine());
        if (tight.w > 0 && tight.h > 0)
            rect = QRect(rect.x() + tight.x, rect.y() + tight.y, tight.w, tight.h);
    }
    seg7::Reading preview = seg7::decode(
        image.constBits() + (size_t)rect.y() * image.bytesPerLine() + rect.x(),
        rect.width(), rect.height(), image.bytesPerLine());
    if (!preview.digits.empty() && preview.digits.size() < 3) {
        show_warning("Zaznaczono tylko część zegara.\n\n"
                     "Zaznacz cały czas, np. 1:23 lub 1:23.4, razem z dwukropkiem. "
                     "Samo 5.2 nie wystarczy.");
        return false;
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

    obs_property_t *target = obs_properties_add_list(props, S_TARGET, "Gdzie wyświetlić czas?",
                                                     OBS_COMBO_TYPE_LIST,
                                                     OBS_COMBO_FORMAT_STRING);
    obs_property_list_add_string(target, "— wybierz źródło tekstu —", "");
    obs_property_set_long_description(target,
        "Najpierw dodaj do sceny źródło „Tekst (GDI+)” lub „Tekst (FreeType 2)”. "
        "Tutaj wybierz jego nazwę. W źródle tekstowym wyłącz czytanie z pliku.");
    std::vector<std::string> names;
    obs_enum_sources(enum_text_sources, &names);
    std::sort(names.begin(), names.end());
    std::string current_target;
    if (f) {
        std::lock_guard<std::mutex> lock(f->mutex);
        current_target = f->target;
    }
    bool found = false;
    for (auto &name : names) {
        obs_property_list_add_string(target, name.c_str(), name.c_str());
        if (name == current_target)
            found = true;
    }
    if (!current_target.empty() && !found)
        obs_property_list_add_string(target, current_target.c_str(), current_target.c_str());

    obs_property_t *select_roi = obs_properties_add_button2(
        props, "select_roi", "Zaznacz zegar na obrazie…", on_select_roi, f);
    obs_property_set_enabled(select_roi, f != nullptr);
    obs_property_set_long_description(select_roi,
        "Zaznacz sam wyświetlacz zegara: wszystkie minuty, sekundy i separatory. "
        "Pozostaw mały margines wokół cyfr. Nie zaznaczaj wyniku meczu ani całego obrazu.");

    obs_properties_add_bool(props, S_SECONDS, "Ukryj dziesiąte części sekundy (1:23.4 → 1:23)");
    obs_property_t *direction = obs_properties_add_list(
        props, S_DIRECTION, "Jak zmienia się czas?", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
    obs_property_list_add_int(direction, "Automatycznie (zalecane)", 0);
    obs_property_list_add_int(direction, "Czas rośnie (0:00 → 0:01)", 1);
    obs_property_list_add_int(direction, "Czas maleje (1:00 → 0:59)", -1);
    obs_property_t *tighten = obs_properties_add_bool(props, S_TIGHTEN,
        "Dopasuj zaznaczenie do ciemnego tła zegara");
    obs_property_set_long_description(tighten,
        "Działa przy zatwierdzaniu zaznaczenia. Jeśli obcina cyfry, wyłącz tę opcję "
        "i ponownie zaznacz zegar.");

    obs_properties_t *manual = obs_properties_create();
    obs_properties_add_int(manual, S_ROI_X, "Od lewej krawędzi (piksele)", 0, MAX_FRAME_SIZE, 1);
    obs_properties_add_int(manual, S_ROI_Y, "Od górnej krawędzi (piksele)", 0, MAX_FRAME_SIZE, 1);
    obs_properties_add_int(manual, S_ROI_W, "Szerokość obszaru (piksele)", 16, MAX_FRAME_SIZE, 1);
    obs_properties_add_int(manual, S_ROI_H, "Wysokość obszaru (piksele)", 8, MAX_FRAME_SIZE, 1);
    obs_properties_add_group(props, "manual_roi", "Ręczne ustawienie obszaru (opcjonalne)",
                              OBS_GROUP_NORMAL, manual);

    std::string status = "Wybierz źródło tekstu i zaznacz zegar na obrazie.";
    if (f) {
        std::lock_guard<std::mutex> lock(f->mutex);
        status = f->status;
        if (f->last_frame_wall.time_since_epoch().count() != 0 &&
            std::chrono::steady_clock::now() - f->last_frame_wall > std::chrono::seconds(3))
            status = "Od ponad 3 sekund nie ma nowego obrazu. Zachowano ostatni odczyt. Sprawdź źródło wideo.";
    }
    obs_property_t *info = obs_properties_add_text(props, "status", status.c_str(),
                                                   OBS_TEXT_INFO);
    (void)info;
    return props;
}

struct obs_source_frame *clock_filter_video(void *data, struct obs_source_frame *frame)
{
    auto *f = static_cast<ClockFilter *>(data);
    if (frame && frame->data[0]) {
        try {
            process_frame(f, frame);
        } catch (const std::exception &error) {
            // Never unwind through libobs' C callbacks or discard video on a
            // decoder allocation failure. The text keeps its previous value.
            blog(LOG_WARNING, "[seg7-clock-reader] Nie udało się odczytać klatki: %s", error.what());
        }
    }
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
    clock_filter_info.video_tick = clock_filter_tick;
}

}

OBS_DECLARE_MODULE()

extern "C" bool obs_module_load(void)
{
    init_clock_filter_info();
    obs_register_source(&clock_filter_info);
    blog(LOG_INFO, "[seg7-clock-reader] Wczytano czytnik zegara");
    return true;
}

extern "C" void obs_module_unload(void)
{
    blog(LOG_INFO, "[seg7-clock-reader] Wyłączono czytnik zegara");
}
