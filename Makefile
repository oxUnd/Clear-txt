CXX = g++
CXXFLAGS = -Wall -std=c++11

# Use fltk-config to get FLTK flags
FLTK_CXXFLAGS = `fltk-config --cxxflags`
FLTK_LDFLAGS = `fltk-config --ldflags`
FLTK_LDSTATICFLAGS = `fltk-config --ldstaticflags`

TARGET = clear
TARGET_STATIC = clear-static
SOURCE = clear.cc

all: $(TARGET)

static: $(TARGET_STATIC)

$(TARGET): $(SOURCE)
	$(CXX) $(CXXFLAGS) $(FLTK_CXXFLAGS) -o $(TARGET) $(SOURCE) $(FLTK_LDFLAGS)

# Static build - links FLTK statically
$(TARGET_STATIC): $(SOURCE)
	@echo "Building static version..."
	$(CXX) $(CXXFLAGS) $(FLTK_CXXFLAGS) -o $(TARGET_STATIC) $(SOURCE) $(FLTK_LDSTATICFLAGS)

icon: Clear.icns

Clear.icns:
	@if [ ! -f create_icon.sh ]; then \
		echo "Error: create_icon.sh not found"; \
		exit 1; \
	fi
	@./create_icon.sh

app: $(TARGET_STATIC) Clear.icns
	@echo "Creating macOS .app bundle..."
	@rm -rf Clear-txt.app
	@mkdir -p Clear-txt.app/Contents/MacOS
	@mkdir -p Clear-txt.app/Contents/Resources
	@cp $(TARGET_STATIC) Clear-txt.app/Contents/MacOS/Clear-txt
	@cp Info.plist Clear-txt.app/Contents/Info.plist
	@cp Clear.icns Clear-txt.app/Contents/Resources/
	@chmod +x Clear-txt.app/Contents/MacOS/Clear-txt
	@echo "Clear.app bundle created successfully!"

clean:
	rm -f $(TARGET) $(TARGET_STATIC)
	rm -rf Clear-txt.app
	rm -f Clear.icns

.PHONY: all static app icon clean

