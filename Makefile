CXX ?= g++
CXXFLAGS ?= -O2 -g -Wall -std=c++17
PKG_CONFIG ?= pkg-config

CPPFLAGS += -Isrc -Isrc/Engine -Isrc/Importing -Isrc/Input -Isrc/Scripting -Isrc/Scene

ifeq ($(OS),Windows_NT)
MINGW_BIN ?= /mingw64/bin
export PATH := $(MINGW_BIN):$(PATH)
ifeq ($(origin CXX),default)
CXX := $(MINGW_BIN)/g++.exe
endif
ifeq ($(origin PKG_CONFIG),file)
PKG_CONFIG := $(MINGW_BIN)/pkg-config.exe
endif
OUTPUT_DIR = export/windows/bin
BUILD_DIR = build/windows
TARGET = $(OUTPUT_DIR)/Puppy3D.exe

# Definición robusta de flags para Windows evitando fallos de pkg-config vacíos
CPPFLAGS += -mwindows -IC:/msys64/mingw64/include -IC:/msys64/mingw64/include/gtk-3.0 -IC:/msys64/mingw64/include/glib-2.0 -IC:/msys64/mingw64/lib/glib-2.0/include -IC:/msys64/mingw64/include/pango-1.0 -IC:/msys64/mingw64/include/cairo -IC:/msys64/mingw64/include/gdk-pixbuf-2.0 -IC:/msys64/mingw64/include/atk-1.0 -IC:/msys64/mingw64/include/SDL2 -IC:/msys64/mingw64/include/lua5.4
LDLIBS += -LC:/msys64/mingw64/lib -lgtk-3 -lgdk-3 -lgdi32 -limm32 -lshell32 -lole32 -luuid -lwinmm -ldwmapi -lsetupapi -lcfgmgr32 -lhid -lwinspool -lcomctl32 -lcomdlg32 -lpangocairo-1.0 -lpangowin32-1.0 -lpango-1.0 -lcairo-gobject -lcairo -lharfbuzz -lgdk_pixbuf-2.0 -latk-1.0 -lgio-2.0 -lgobject-2.0 -lglib-2.0 -lintl -lz -lSDL2_mixer -lmingw32 -lSDL2main -lSDL2 -llua5.4 -lm -lepoxy -lopengl32
else
OUTPUT_DIR = export/linux/bin
BUILD_DIR = build/linux
TARGET = $(OUTPUT_DIR)/Puppy3D
CPPFLAGS += $(shell $(PKG_CONFIG) --cflags gtk+-3.0 SDL2_mixer lua5.4 epoxy)
LDLIBS += $(shell $(PKG_CONFIG) --libs gtk+-3.0 SDL2_mixer lua5.4 epoxy gl)
endif

# Búsqueda de archivos recursiva compatible con Windows/MSYS o Linux
SRCS := $(shell find src -type f -name '*.cpp')

.PHONY: all clean export-resources test run
all: $(TARGET) export-resources

run: all
	$(TARGET)

OBJS := $(patsubst src/%.cpp,$(BUILD_DIR)/%.o,$(SRCS))
DEPFILES := $(OBJS:.o=.d)

$(TARGET): $(OBJS)
	@mkdir -p $(OUTPUT_DIR)
	$(CXX) -o $(TARGET) $(OBJS) $(LDLIBS)

$(BUILD_DIR)/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@

-include $(DEPFILES)

TEST_TARGET = $(BUILD_DIR)/scene_document_test
TEST_OBJECT = $(BUILD_DIR)/SceneDocumentTest.o
TEST_SUPPORT_OBJECTS = $(filter-out $(BUILD_DIR)/Editor/main.o $(BUILD_DIR)/Input/InputService.o $(BUILD_DIR)/Scripting/LuaRuntime.o,$(OBJS))

$(TEST_OBJECT): tests/SceneDocumentTest.cpp
	@mkdir -p $(BUILD_DIR)
	$(CXX) -Isrc $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(TEST_TARGET): $(TEST_SUPPORT_OBJECTS) $(TEST_OBJECT)
	$(CXX) -o $@ $^ $(LDLIBS)

-include $(TEST_OBJECT:.o=.d)

test: $(TEST_TARGET)
	./$(TEST_TARGET)

export-resources:
	@mkdir -p $(OUTPUT_DIR)/res
	@if [ -d "res" ]; then cp -r res/. $(OUTPUT_DIR)/res/; fi

clean:
	rm -rf build
	rm -rf $(OUTPUT_DIR)/res
	rm -f $(TARGET)