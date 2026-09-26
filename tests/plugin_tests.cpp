// Exercise the actual callbacks against libobs without launching the OBS UI.
#include "../src/plugin-main.cpp"

#include <limits>
#include <fstream>

#ifndef CLOCK_TEST_FIXTURE_DIR
#define CLOCK_TEST_FIXTURE_DIR "tests/fixtures"
#endif

namespace {
int failures = 0;
int text_updates = 0;

void check(bool condition, const char *message)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

void test_gray_rows()
{
    uint8_t pixels[] = {10, 20, 90, 91, 30, 40, 92, 93};
    obs_source_frame frame{};
    frame.width = frame.height = 2;
    frame.linesize[0] = 4;
    frame.data[0] = pixels;
    frame.format = VIDEO_FORMAT_Y800;
    uint8_t out[2]{};
    check(gray_row(&frame, 1, 0, 2, out) && out[0] == 30 && out[1] == 40,
          "padded grayscale row");
    frame.flip = true;
    check(gray_row(&frame, 0, 0, 2, out) && out[0] == 30 && out[1] == 40,
          "bottom-up frame uses display orientation");
    check(gray_row(&frame, 1, 1, 1, out) && out[0] == 20, "flipped cropped row");
    check(!gray_row(&frame, 2, 0, 2, out), "out-of-range row rejected");
    check(!gray_row(&frame, 0, 1, 2, out), "out-of-range crop rejected");
    frame.linesize[0] = 1;
    check(!gray_row(&frame, 0, 0, 2, out), "invalid grayscale stride rejected");

    uint8_t ayuv[] = {17, 88, 120, 255, 19, 99, 230, 255};
    frame.data[0] = ayuv;
    frame.height = 1;
    frame.flip = false;
    frame.linesize[0] = 8;
    frame.format = VIDEO_FORMAT_AYUV;
    check(gray_row(&frame, 0, 0, 2, out) && out[0] == 120 && out[1] == 230,
          "AYUV reads Y rather than U");

    uint8_t yuy2[] = {10, 128, 20, 128};
    frame.data[0] = yuy2;
    frame.linesize[0] = 4;
    frame.format = VIDEO_FORMAT_YUY2;
    check(gray_row(&frame, 0, 1, 1, out) && out[0] == 20, "odd x in packed YUY2");
    frame.format = VIDEO_FORMAT_UYVY;
    check(gray_row(&frame, 0, 0, 2, out) && out[0] == 128 && out[1] == 128,
          "UYVY reads alternate luma bytes");

    for (auto format : {VIDEO_FORMAT_I010, VIDEO_FORMAT_I210, VIDEO_FORMAT_P010,
                        VIDEO_FORMAT_I412, VIDEO_FORMAT_YA2L, VIDEO_FORMAT_P216,
                        VIDEO_FORMAT_P416}) {
        int shift = format == VIDEO_FORMAT_I010 || format == VIDEO_FORMAT_I210 ? 2
                    : format == VIDEO_FORMAT_I412 || format == VIDEO_FORMAT_YA2L ? 4 : 8;
        uint16_t sample = (uint16_t)(173 << shift);
        uint8_t word[] = {(uint8_t)sample, (uint8_t)(sample >> 8)};
        frame.data[0] = word;
        frame.width = 1;
        frame.linesize[0] = 2;
        frame.format = format;
        check(gray_row(&frame, 0, 0, 1, out) && out[0] == 173,
              "high-bit-depth luma conversion");
    }
    frame.format = VIDEO_FORMAT_NONE;
    check(!gray_row(&frame, 0, 0, 1, out), "unsupported format rejected");
}

void *fake_text_create(obs_data_t *, obs_source_t *) { return new int(0); }
void fake_text_destroy(void *data) { delete static_cast<int *>(data); }
void fake_text_update(void *, obs_data_t *) { ++text_updates; }
const char *fake_text_name(void *) { return "Test text"; }

void test_broadcast_publication(obs_source_t *text_source)
{
    auto load = [](const char *name) {
        std::ifstream file(std::string(CLOCK_TEST_FIXTURE_DIR) + "/" + name, std::ios::binary);
        std::string magic;
        int width = 0, height = 0, max_value = 0;
        file >> magic >> width >> height >> max_value;
        if (!file || magic != "P5" || width != 113 || height != 33 || max_value != 255)
            return std::vector<uint8_t>{};
        file.get();
        std::vector<uint8_t> pixels((size_t)width * height);
        file.read(reinterpret_cast<char *>(pixels.data()), pixels.size());
        if (!file)
            pixels.clear();
        return pixels;
    };
    auto pixels = load("clock-1-27.pgm");
    auto missing = load("no-clock.pgm");
    check(!pixels.empty() && !missing.empty(), "broadcast publication fixtures available");
    if (pixels.empty() || missing.empty())
        return;
    ClockFilter filter;
    filter.source = text_source;
    filter.target = "clock-output";
    filter.roi_w = 113;
    filter.roi_h = 33;
    obs_source_frame frame{};
    frame.width = frame.linesize[0] = 113;
    frame.height = 33;
    frame.format = VIDEO_FORMAT_Y800;
    frame.data[0] = pixels.data();
    const int before = text_updates;
    for (int i = 0; i < 3; ++i) {
        frame.timestamp = 1000000000ULL + i * 100000000ULL;
        clock_filter_video(&filter, &frame);
        clock_filter_tick(&filter, 0.1f);
        if (i < 2)
            check(text_updates == before, "unconfirmed first frames are not published");
    }
    check(text_updates == before + 1 && filter.last_text == "1:27",
          "actual broadcast clock reaches OBS text after confirmation");
    obs_data_t *settings = obs_source_get_settings(text_source);
    check(std::string(obs_data_get_string(settings, "text")) == "1:27",
          "OBS text contains the actual broadcast time");
    obs_data_release(settings);

    frame.data[0] = missing.data();
    frame.timestamp = 6000000000ULL;
    clock_filter_video(&filter, &frame);
    clock_filter_tick(&filter, 0.1f);
    check(text_updates == before + 1 && filter.last_text == "1:27",
          "lost clock preserves the last confirmed text");
    check(filter.status.find("Brak nowego odczytu") != std::string::npos &&
          filter.status.find("pewność") == std::string::npos,
          "held output does not present an old confidence as a current reading");
}

void test_obs_callbacks()
{
    check(obs_startup("pl-PL", nullptr, nullptr), "OBS startup");
    obs_source_info text_info{};
    text_info.id = "text_gdiplus";
    text_info.version = 2;
    text_info.type = OBS_SOURCE_TYPE_INPUT;
    text_info.get_name = fake_text_name;
    text_info.create = fake_text_create;
    text_info.destroy = fake_text_destroy;
    text_info.update = fake_text_update;
    obs_register_source(&text_info);

    obs_source_t *text_source = obs_source_create("text_gdiplus_v2", "clock-output", nullptr, nullptr);
    check(text_source != nullptr, "legacy versioned text source created");
    if (!text_source) {
        obs_shutdown();
        return;
    }
    obs_properties_t *properties = clock_filter_properties(nullptr);
    check(properties && !obs_property_enabled(obs_properties_get(properties, "select_roi")),
          "source-type properties allow null implementation data");
    check(obs_property_list_item_count(obs_properties_get(properties, S_TARGET)) == 2,
          "versioned GDI+ text source listed");
    obs_properties_destroy(properties);

    ClockFilter filter;
    filter.source = text_source;
    filter.target = "clock-output";
    filter.seconds_only = true;
    filter.last_display = "1:23.4";
    clock_filter_tick(&filter, 0.016f);
    check(text_updates == 1 && filter.last_text == "1:23", "first clock published");
    filter.last_display = "1:23.3";
    clock_filter_tick(&filter, 0.016f);
    check(text_updates == 1, "hidden tenths do not trigger redundant text updates");

    obs_data_t *settings = obs_data_create();
    obs_data_set_string(settings, S_TARGET, "clock-output");
    obs_data_set_bool(settings, S_SECONDS, false);
    clock_filter_update(&filter, settings);
    check(text_updates == 1, "settings callback does not publish stale cached text");
    clock_filter_tick(&filter, 0.016f);
    check(text_updates == 2 && filter.last_text == "1:23.3", "format changes republish latest reading");

    filter.last_display = "1:23.2";
    obs_source_set_enabled(text_source, false);
    clock_filter_tick(&filter, 0.016f);
    check(text_updates == 2, "disabled filter does not publish");
    obs_source_set_enabled(text_source, true);

    obs_source_remove(text_source);
    clock_filter_tick(&filter, 0.016f);
    check(filter.last_text.empty(), "removed target invalidates publication cache");
    obs_source_release(text_source);
    text_source = obs_source_create("text_gdiplus_v2", "clock-output", nullptr, nullptr);
    filter.source = text_source;
    clock_filter_tick(&filter, 0.016f);
    check(text_updates == 3 && filter.last_text == "1:23.2", "recreated target gets held clock");

    obs_source_remove(text_source);
    obs_source_release(text_source);
    text_source = obs_source_create("text_gdiplus_v2", "clock-output", nullptr, nullptr);
    filter.source = text_source;
    clock_filter_tick(&filter, 0.016f);
    check(text_updates == 4, "target replaced between ticks still receives unchanged text");

    obs_data_set_int(settings, S_ROI_X, std::numeric_limits<long long>::max());
    obs_data_set_int(settings, S_ROI_W, -100);
    obs_data_set_int(settings, S_DIRECTION, std::numeric_limits<long long>::min());
    clock_filter_update(&filter, settings);
    check(filter.roi_x == MAX_FRAME_SIZE && filter.roi_w == 0 && filter.direction == -1,
          "settings are bounded before narrowing to int");
    test_broadcast_publication(text_source);
    obs_data_release(settings);
    obs_source_release(text_source);
    obs_shutdown();
}
}

int main()
{
    test_gray_rows();
    test_obs_callbacks();
    if (failures)
        return 1;
    std::puts("OBS callback and frame conversion regressions passed.");
    return 0;
}
