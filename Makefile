CXX ?= clang++
.DEFAULT_GOAL := all
ARCHES ?= arm64
ARCHFLAGS := $(foreach a,$(ARCHES),-arch $(a))

PLUGIN := seg7-clock-reader
DEPS := .deps
OBS_APP := /Applications/OBS.app/Contents/Frameworks
OBS_SRC := $(DEPS)/obs-studio-32.2.2
BUILD := build
BUNDLE := $(BUILD)/$(PLUGIN).plugin
BIN := $(BUNDLE)/Contents/MacOS/$(PLUGIN)

SRCS := src/plugin-main.cpp src/roi-picker.cpp src/clock_core.cpp
OBJS := $(addprefix $(BUILD)/,$(addsuffix .o,$(notdir $(basename $(SRCS)))))

CXXFLAGS := -std=c++17 -O2 -fPIC $(ARCHFLAGS) -mmacosx-version-min=13.0 \
	-include mac/arch-shim.h \
	-I$(OBS_SRC)/libobs -I$(DEPS)/include -Isrc -I../cpp/src \
	-F$(DEPS)/lib -F$(OBS_APP) \
	-I$(DEPS)/lib/QtWidgets.framework/Headers \
	-I$(DEPS)/lib/QtGui.framework/Headers \
	-I$(DEPS)/lib/QtCore.framework/Headers \
	-Wall -Wno-unused-parameter -Wno-deprecated-declarations -MMD -MP

LDFLAGS := -bundle $(ARCHFLAGS) -mmacosx-version-min=13.0 \
	-F$(OBS_APP) -framework libobs -framework QtWidgets -framework QtGui -framework QtCore \
	-Wl,-rpath,@executable_path/../Frameworks \
	-Wl,-rpath,@loader_path/../../Frameworks

.PHONY: all clean install test test-obs

TEST_CXXFLAGS ?= -std=c++17 -O1 -g -Wall -Wextra -fsanitize=address,undefined -fno-omit-frame-pointer

-include $(OBJS:.o=.d)

all: $(BIN)

test: $(BUILD)/clock_core_tests
	$(BUILD)/clock_core_tests

$(BUILD)/clock_core_tests: tests/clock_core_tests.cpp src/clock_core.cpp src/clock_core.h | $(BUILD)
	$(CXX) $(TEST_CXXFLAGS) -Isrc tests/clock_core_tests.cpp src/clock_core.cpp -o $@

test-obs: $(BUILD)/plugin_tests
	$(BUILD)/plugin_tests

$(BUILD)/plugin_tests: tests/plugin_tests.cpp $(SRCS) src/clock_core.h src/roi-picker.hpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $(TEST_CXXFLAGS) tests/plugin_tests.cpp src/clock_core.cpp src/roi-picker.cpp \
		-F$(OBS_APP) -framework libobs -framework QtWidgets -framework QtGui -framework QtCore \
		-Wl,-rpath,$(OBS_APP) -o $@

$(BUILD)/%.o: src/%.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BIN): $(OBJS) mac/Info.plist
	mkdir -p $(BUNDLE)/Contents/MacOS $(BUNDLE)/Contents/Resources
	$(CXX) $(OBJS) $(LDFLAGS) -o $(BIN)
	cp mac/Info.plist $(BUNDLE)/Contents/Info.plist
	codesign --force --sign - $(BUNDLE)
	@echo "Zbudowano $(BUNDLE)"

$(BUILD):
	mkdir -p $(BUILD)

install: all
	mkdir -p "$(HOME)/Library/Application Support/obs-studio/plugins"
	rm -rf "$(HOME)/Library/Application Support/obs-studio/plugins/$(PLUGIN).plugin"
	cp -R $(BUNDLE) "$(HOME)/Library/Application Support/obs-studio/plugins/"
	codesign --force --sign - "$(HOME)/Library/Application Support/obs-studio/plugins/$(PLUGIN).plugin"
	@echo "Zainstalowano $(PLUGIN). Uruchom ponownie OBS."

clean:
	rm -rf $(BUILD)
