#include <FL/Fl.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Window.H>
#include <FL/fl_draw.H>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>
#ifdef _WIN32
#include <shlobj.h>
#include <windows.h>
#else
#include <pwd.h>
#endif

struct TodoItem {
  std::string text;
  bool completed;
  int y_position;
  int swipe_offset; // Horizontal offset for swipe gesture (positive = right,
                    // negative = left)

  TodoItem(const std::string &t)
      : text(t), completed(false), y_position(0), swipe_offset(0) {}
};

class ClearApp : public Fl_Window {
private:
  std::vector<TodoItem> items;
  int selected_index;
  int drag_start_y;
  int drag_start_x;
  bool is_dragging;
  bool is_swiping;
  bool is_pulling_down; // Pull down to add new item
  int pull_down_offset; // Offset for pull-down animation
  int drag_offset;
  int item_height;
  std::string data_file;
  int editing_index;        // Index of item being edited
  std::string editing_text; // Text being edited
  int pending_click_index;  // Index of item pending click (to handle
                            // double-click)
  bool can_reorder;         // Whether reordering is allowed (after long press)
  Fl_Input *input_widget;   // Input widget for editing items
  int scroll_offset;        // Vertical scroll offset (positive = scrolled down)

  // Error message display
  struct ErrorDisplay {
    std::string message;
    bool is_visible;

    ErrorDisplay() : message(""), is_visible(false) {}
  } error_display;

  // Calculate luminance of a color (0-255)
  double get_luminance(Fl_Color color) {
    unsigned char r, g, b;
    Fl::get_color(color, r, g, b);
    // Use relative luminance formula (ITU-R BT.709)
    return 0.2126 * r + 0.7152 * g + 0.0722 * b;
  }

  // Get appropriate text color based on background color
  Fl_Color get_text_color(Fl_Color bg_color) {
    double luminance = get_luminance(bg_color);
    // If background is bright (luminance > 128), use black text
    // Otherwise use white text
    return (luminance > 128) ? FL_BLACK : FL_WHITE;
  }

  // Get color based on position in list (red -> orange -> yellow gradient)
  Fl_Color get_color_by_position(int position, int total_items) {
    if (total_items <= 1) {
      return FL_RED;
    }

    // Calculate ratio from 0.0 (top, red) to 1.0 (bottom, yellow)
    double ratio = (double)position / (total_items - 1);

    // Interpolate from red (255,0,0) -> orange (255,165,0) -> yellow
    // (255,255,0)
    unsigned char r, g, b;

    if (ratio < 0.5) {
      // Red to Orange (0.0 to 0.5)
      double local_ratio = ratio * 2.0; // 0.0 to 1.0
      r = 255;
      g = (unsigned char)(165 * local_ratio);
      b = 0;
    } else {
      // Orange to Yellow (0.5 to 1.0)
      double local_ratio = (ratio - 0.5) * 2.0; // 0.0 to 1.0
      r = 255;
      g = (unsigned char)(165 + (255 - 165) * local_ratio);
      b = 0;
    }

    return fl_rgb_color(r, g, b);
  }

  // Get application data directory path
  static std::string get_data_directory() {
    std::string home_dir;
    std::string data_dir;

#ifdef _WIN32
    // Windows: Use %APPDATA%\Clear
    char appdata_path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL,
                                   SHGFP_TYPE_CURRENT, appdata_path))) {
      home_dir = appdata_path;
      data_dir = home_dir + "\\Clear";
    } else {
      // Fallback to current directory
      data_dir = ".";
    }
#else
    // Unix-like systems (macOS, Linux)
    const char *home = getenv("HOME");
    if (!home) {
      // Fallback to getpwuid
      struct passwd *pw = getpwuid(getuid());
      if (pw) {
        home = pw->pw_dir;
      }
    }

    if (home) {
      home_dir = home;
#ifdef __APPLE__
      // macOS: ~/Library/Application Support/Clear
      data_dir = home_dir + "/Library/Application Support/Clear";
#else
      // Linux: ~/.config/Clear (or ~/.local/share/Clear)
      // Using XDG_CONFIG_HOME if set, otherwise ~/.config
      const char *xdg_config = getenv("XDG_CONFIG_HOME");
      if (xdg_config) {
        data_dir = std::string(xdg_config) + "/Clear";
      } else {
        data_dir = home_dir + "/.config/Clear";
      }
#endif
    } else {
      // Fallback to current directory
      data_dir = ".";
    }
#endif

    // Create directory if it doesn't exist (recursively)
    if (data_dir != ".") {
#ifdef _WIN32
      // Create all parent directories recursively on Windows
      std::string path = data_dir;
      size_t pos = 0;
      while ((pos = path.find_first_of("\\/", pos + 1)) != std::string::npos) {
        std::string dir = path.substr(0, pos);
        CreateDirectoryA(dir.c_str(), NULL);
      }
      // Create the final directory
      CreateDirectoryA(data_dir.c_str(), NULL);
#else
      // Create all parent directories recursively
      std::string path = data_dir;
      size_t pos = 0;
      while ((pos = path.find_first_of('/', pos + 1)) != std::string::npos) {
        std::string dir = path.substr(0, pos);
        mkdir(dir.c_str(), 0755);
      }
      // Create the final directory
      mkdir(data_dir.c_str(), 0755);
#endif
    }

    return data_dir;
  }

  // Escape/unescape text for file storage
  std::string escape_text(const std::string &text) {
    std::string result;
    for (char c : text) {
      if (c == '\n') {
        result += "\\n";
      } else if (c == '\\') {
        result += "\\\\";
      } else {
        result += c;
      }
    }
    return result;
  }

  std::string unescape_text(const std::string &text) {
    std::string result;
    for (size_t i = 0; i < text.length(); i++) {
      if (text[i] == '\\' && i + 1 < text.length()) {
        if (text[i + 1] == 'n') {
          result += '\n';
          i++;
        } else if (text[i + 1] == '\\') {
          result += '\\';
          i++;
        } else {
          result += text[i];
        }
      } else {
        result += text[i];
      }
    }
    return result;
  }

  void show_error(const std::string &message) {
    // Cancel any existing timeout to prevent multiple timers
    Fl::remove_timeout(hide_error_cb, this);

    error_display.message = message;
    error_display.is_visible = true;

    // Auto-hide after 3 seconds
    Fl::add_timeout(3.0, hide_error_cb, this);
    redraw();
  }

  void hide_error() {
    error_display.is_visible = false;
    error_display.message = "";
    Fl::remove_timeout(hide_error_cb, this);
    redraw();
  }

  static void hide_error_cb(void *data) {
    ClearApp *app = static_cast<ClearApp *>(data);
    app->hide_error();
  }

  // Calculate text dimensions more accurately
  void measure_text(const std::string &text, int &width, int &height,
                    int font_size = 14) {
    fl_font(FL_HELVETICA_BOLD, font_size);
    width = (int)fl_width(text.c_str());
    height = (int)fl_height();
  }

  // Draw rounded rectangle with specified color and corner radius
  void draw_rounded_rect(int x, int y, int w, int h, int radius) {
    // Draw four rounded corners using pie slices
    // Top-left corner
    fl_pie(x, y, radius * 2, radius * 2, 90, 180);
    // Top-right corner
    fl_pie(x + w - radius * 2, y, radius * 2, radius * 2, 0, 90);
    // Bottom-right corner
    fl_pie(x + w - radius * 2, y + h - radius * 2, radius * 2, radius * 2, 270,
           360);
    // Bottom-left corner
    fl_pie(x, y + h - radius * 2, radius * 2, radius * 2, 180, 270);

    // Draw rectangular parts (top, middle, bottom)
    fl_rectf(x + radius, y, w - radius * 2, h);      // Middle vertical strip
    fl_rectf(x, y + radius, radius, h - radius * 2); // Left strip
    fl_rectf(x + w - radius, y + radius, radius, h - radius * 2); // Right strip
  }

  // Draw rounded rectangle border
  void draw_rounded_rect_border(int x, int y, int w, int h, int radius) {
    // Draw four corner arcs
    fl_arc(x, y, radius * 2, radius * 2, 90, 180);                // Top-left
    fl_arc(x + w - radius * 2, y, radius * 2, radius * 2, 0, 90); // Top-right
    fl_arc(x + w - radius * 2, y + h - radius * 2, radius * 2, radius * 2, 270,
           360); // Bottom-right
    fl_arc(x, y + h - radius * 2, radius * 2, radius * 2, 180,
           270); // Bottom-left

    // Draw straight edges
    fl_line(x + radius, y, x + w - radius, y);         // Top
    fl_line(x + w, y + radius, x + w, y + h - radius); // Right
    fl_line(x + w - radius, y + h, x + radius, y + h); // Bottom
    fl_line(x, y + h - radius, x, y + radius);         // Left
  }

  void save_to_file() {
    std::ofstream file(data_file);
    if (!file.is_open()) {
      show_error("Failed to save file: " + data_file);
      return;
    }

    for (const auto &item : items) {
      // Save with color index 0 for backward compatibility (color is now
      // position-based)
      file << "0|" << (item.completed ? "1" : "0") << "|"
           << escape_text(item.text) << "\n";
    }

    file.close();

    // Check if write failed
    if (file.fail()) {
      show_error("Error saving file: " + data_file);
    }
  }

  bool load_from_file() {
    std::ifstream file(data_file);
    if (!file.is_open()) {
      // File doesn't exist or can't be opened
      // This is normal for first run, so don't show error
      return false;
    }

    items.clear();
    std::string line;
    bool loaded_any = false;
    while (std::getline(file, line)) {
      if (line.empty())
        continue;

      size_t pos1 = line.find('|');
      if (pos1 == std::string::npos)
        continue;

      size_t pos2 = line.find('|', pos1 + 1);
      if (pos2 == std::string::npos)
        continue;

      // Color index is stored but not used (for backward compatibility)
      bool completed = (line.substr(pos1 + 1, pos2 - pos1 - 1) == "1");
      std::string text = unescape_text(line.substr(pos2 + 1));

      TodoItem item(text);
      item.completed = completed;
      items.push_back(item);
      loaded_any = true;
    }

    file.close();

    // Check if read failed
    if (file.fail() && !file.eof()) {
      show_error("Error reading file: " + data_file);
    }

    return loaded_any; // Return true if we loaded at least one item
  }

  void add_sample_items() {
    // Add sample items for first-time users
    items.push_back(TodoItem("Welcome to Clear"));
    items.push_back(TodoItem("Pull down to add new task"));
    items.push_back(TodoItem("Click to edit task"));
    items.push_back(TodoItem("Double-click to complete"));
    items.push_back(TodoItem("Swipe right to delete"));
    items.push_back(TodoItem("Long press to reorder"));
  }

  void draw_item(int index, int y, bool is_editing = false, int visual_position = -1, int total_visual_items = -1) {
    if (index < 0 || index >= (int)items.size())
      return;

    TodoItem &item = items[index];
    item.y_position = y;

    int x_offset = item.swipe_offset;
    int abs_offset = abs(x_offset);

    // Get color based on position in list (not item's stored color)
    // For completed items, use dark gray instead
    Fl_Color item_color;
    if (item.completed) {
      item_color = fl_rgb_color(64, 64, 64); // Dark gray for completed items
    } else {
      // Use visual position if provided (for sorted display), otherwise use actual index
      if (visual_position >= 0 && total_visual_items > 0) {
        item_color = get_color_by_position(visual_position, total_visual_items);
      } else {
        item_color = get_color_by_position(index, items.size());
      }
    }

    // Draw swipe background based on direction
    if (x_offset > 0) {
      // Swiped right (finger moves right) - item moves right, show complete
      // background (green) on left
      int right_offset = abs_offset;
      if (right_offset > w())
        right_offset = w();
      fl_color(FL_GREEN);
      fl_rectf(0, y, right_offset, item_height);
      fl_color(FL_WHITE);
      fl_font(FL_HELVETICA_BOLD, 16);
      fl_draw("COMPLETE", right_offset / 2 - 40, y + item_height / 2 + 5);
    } else if (x_offset < 0) {
      // Swiped left (finger moves left) - item moves left, show delete
      // background (red) on right
      int left_offset = abs_offset;
      if (left_offset > w())
        left_offset = w();
      fl_color(FL_RED);
      fl_rectf(w() - left_offset, y, left_offset, item_height);
      fl_color(FL_WHITE);
      fl_font(FL_HELVETICA_BOLD, 16);
      fl_draw("DELETE", w() - left_offset / 2 - 30, y + item_height / 2 + 5);
    }

    // Draw colored background (shifted by swipe)
    int bg_x = (x_offset > 0) ? abs_offset : 0;
    int bg_w = w() - abs_offset;
    if (bg_w < 0)
      bg_w = 0;

    if (is_editing) {
      // When editing, only draw background in the left 20px padding area
      // Fl_Input widget will handle the rest
      fl_color(item_color);
      fl_rectf(bg_x, y, 20, item_height);
    } else {
      // When not editing, draw full background and text
      fl_color(item_color);
      fl_rectf(bg_x, y, bg_w, item_height);

      // Draw text with appropriate color based on background
      Fl_Color text_color = get_text_color(item_color);
      fl_color(text_color);
      fl_font(FL_HELVETICA_BOLD, 18);

      std::string display_text = item.text;
      if (item.completed) {
        // display_text = "✓ " + display_text;
      }

      int text_x = (x_offset > 0) ? (abs_offset + 20) : (bg_x + 20);
      int text_y = y + item_height / 2 + 6;
      
      // Draw text
      fl_draw(display_text.c_str(), text_x, text_y);
      
      // Draw strikethrough for completed items
      if (item.completed) {
        int text_w, text_h;
        measure_text(display_text, text_w, text_h, 18);
        // Draw strikethrough line with same color as text
        fl_line(text_x, text_y - text_h / 2, text_x + text_w, text_y - text_h / 2);
      }
    }
  }

  // Get sorted indices (incomplete first, then completed)
  std::vector<int> get_sorted_indices() {
    std::vector<int> indices;
    // First add incomplete items
    for (size_t i = 0; i < items.size(); i++) {
      if (!items[i].completed) {
        indices.push_back(i);
      }
    }
    // Then add completed items
    for (size_t i = 0; i < items.size(); i++) {
      if (items[i].completed) {
        indices.push_back(i);
      }
    }
    return indices;
  }

  int get_item_at_y(int y) {
    int start_y = 0; // Start from top
    // Adjust y coordinate for scroll offset
    int adjusted_y = y + scroll_offset;
    if (adjusted_y < start_y)
      return -1; // Above first item
    int visual_index = (adjusted_y - start_y) / item_height;
    
    // Get sorted indices to map visual position to actual index
    std::vector<int> sorted_indices = get_sorted_indices();
    if (visual_index >= 0 && visual_index < (int)sorted_indices.size()) {
      return sorted_indices[visual_index];
    }
    return -1;
  }

  // Get maximum scroll offset (how far we can scroll down)
  int get_max_scroll_offset() {
    int total_height = items.size() * item_height;
    int visible_height = h() - 40; // Subtract space for instructions at bottom
    int max_scroll = total_height - visible_height;
    return (max_scroll > 0) ? max_scroll : 0;
  }

  // Clamp scroll offset to valid range
  void clamp_scroll_offset() {
    int max_scroll = get_max_scroll_offset();
    if (scroll_offset < 0) {
      scroll_offset = 0;
    } else if (scroll_offset > max_scroll) {
      scroll_offset = max_scroll;
    }
  }

  void reorder_items(int from_index, int to_index) {
    if (from_index < 0 || from_index >= (int)items.size() || to_index < 0 ||
        to_index >= (int)items.size() || from_index == to_index) {
      return;
    }

    TodoItem item = items[from_index];
    items.erase(items.begin() + from_index);
    items.insert(items.begin() + to_index, item);
    save_to_file();
    redraw();
  }

public:
  ClearApp(int W, int H, const char *title)
      : Fl_Window(W, H, title), selected_index(-1), is_dragging(false),
        is_swiping(false), is_pulling_down(false), pull_down_offset(0),
        drag_offset(0), item_height(60), data_file(""), editing_index(-1),
        pending_click_index(-1), can_reorder(false), input_widget(nullptr),
        scroll_offset(0) {

    // Initialize data file path to application data directory
    std::string data_dir = get_data_directory();
    if (data_dir == ".") {
      data_file = "todos.txt"; // Fallback to current directory
    } else {
#ifdef _WIN32
      data_file = data_dir + "\\todos.txt";
#else
      data_file = data_dir + "/todos.txt";
#endif
    }

    color(fl_rgb_color(64, 64, 64));  // deep gray

    // Create input widget (initially hidden)
    input_widget = new Fl_Input(0, 0, w(), item_height);
    input_widget->callback(input_callback, this);
    input_widget->when(FL_WHEN_CHANGED | FL_WHEN_ENTER_KEY | FL_WHEN_RELEASE);
    input_widget->box(FL_FLAT_BOX); // Flat box (background but no border)
    input_widget->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    // Ensure the widget can receive focus and display properly
    input_widget->set_visible_focus();
    input_widget->hide();

    // Load items from file
    bool loaded = load_from_file();

    // If no items loaded (first run), add sample items
    if (!loaded || items.empty()) {
      add_sample_items();
      save_to_file(); // Save sample items to file
    }

    end();
  }

  ~ClearApp() { save_to_file(); }

  void add_item(const std::string &text = "") {
    // Finish any existing editing first
    if (editing_index >= 0) {
      finish_editing();
    }

    // Reset pull down state to avoid position calculation issues
    is_pulling_down = false;
    pull_down_offset = 0;

    // Reset scroll to top when adding new item at the beginning
    scroll_offset = 0;

    // Insert new item at the beginning
    items.insert(items.begin(), TodoItem(text));

    // Start editing the new item
    editing_index = 0;
    editing_text = text;
    start_editing(0);

    save_to_file();
  }

  void start_editing(int index) {
    if (index < 0 || index >= (int)items.size()) {
      return;
    }

    // Finish any existing editing first
    if (editing_index >= 0 && editing_index != index) {
      finish_editing();
    }

    editing_index = index;
    editing_text = items[index].text;

    // Calculate position for input widget
    int start_y = 0;
    int item_y = start_y + index * item_height - scroll_offset;
    if (is_pulling_down && pull_down_offset > 0) {
      item_y += pull_down_offset;
    }

    // Adjust for swipe offset
    int x_offset = items[index].swipe_offset;
    int abs_offset = abs(x_offset);
    int bg_x = (x_offset > 0) ? abs_offset : 0;
    // Add 20 pixels left padding to match non-editing text position
    int input_x = bg_x + 20;
    int input_w = w() - abs_offset - 20;
    if (input_w < 0)
      input_w = 0;

    // Position and show input widget
    // Make it cover the full item area, with left padding for text alignment
    input_widget->resize(input_x, item_y, input_w, item_height);

    // Set colors based on item position
    // For completed items, use dark gray; otherwise use visual position in sorted list
    Fl_Color item_color;
    if (items[index].completed) {
      item_color = fl_rgb_color(64, 64, 64); // Dark gray for completed items
    } else {
      // Find visual position in sorted list
      std::vector<int> sorted_indices = get_sorted_indices();
      int visual_pos = -1;
      for (size_t i = 0; i < sorted_indices.size(); i++) {
        if (sorted_indices[i] == index) {
          visual_pos = i;
          break;
        }
      }
      if (visual_pos >= 0) {
        item_color = get_color_by_position(visual_pos, sorted_indices.size());
      } else {
        item_color = get_color_by_position(index, items.size());
      }
    }
    Fl_Color text_color = get_text_color(item_color);

    // Set background color
    input_widget->color(item_color);

    // Set text color - ensure high contrast and visibility
    input_widget->textcolor(text_color);
    input_widget->textfont(FL_HELVETICA_BOLD);
    input_widget->textsize(18);

    // Set selection color for cursor visibility (use contrasting color)
    Fl_Color selection_color = (text_color == FL_BLACK) ? FL_BLUE : FL_YELLOW;
    input_widget->selection_color(selection_color);

    // Set value - ensure it's set before showing
    std::string text_value = items[index].text;
    input_widget->value(text_value.c_str());

    // Show and activate
    input_widget->show();
    input_widget->activate();
    input_widget->set_visible_focus();

    // Set cursor to end of text
    int text_len = input_widget->size();
    if (text_len > 0) {
      input_widget->insert_position(text_len);
      input_widget->mark(text_len); // Clear any selection
    } else {
      input_widget->insert_position(0);
      input_widget->mark(0);
    }

    // Take focus to ensure input works
    input_widget->take_focus();

    // Force complete redraw
    input_widget->damage(FL_DAMAGE_ALL);
    input_widget->redraw();

    // Process events to ensure everything is updated
    Fl::check();
    Fl::flush();
    redraw();
  }

  void finish_editing() {
    if (editing_index >= 0 && editing_index < (int)items.size()) {
      // Get text from input widget
      if (input_widget && input_widget->visible()) {
        editing_text = input_widget->value() ? input_widget->value() : "";
      }

      input_widget->hide();

      int old_editing_index = editing_index;
      std::string old_editing_text = editing_text;

      if (old_editing_text.empty()) {
        // Remove empty item, but keep at least one empty item if list becomes
        // empty
        items.erase(items.begin() + old_editing_index);
        if (items.empty()) {
          items.push_back(TodoItem(""));
          editing_index = -1; // Reset first to avoid recursion
          editing_text = "";
          redraw();
          start_editing(0);
          return;
        } else {
          editing_index = -1;
          editing_text = "";
        }
      } else {
        items[old_editing_index].text = old_editing_text;
        editing_index = -1;
        editing_text = "";
        save_to_file();
      }
      redraw();
    }
  }

  void delete_item(int index) {
    if (index >= 0 && index < (int)items.size()) {
      items.erase(items.begin() + index);
      if (selected_index >= (int)items.size()) {
        selected_index = -1;
      }
      // Reset swipe offsets for all items
      for (auto &item : items) {
        item.swipe_offset = 0;
      }

      // Clamp scroll offset after deletion
      clamp_scroll_offset();

      // If all items are deleted, add an empty item for input
      if (items.empty()) {
        items.push_back(TodoItem(""));
        editing_index = 0;
        editing_text = "";
        scroll_offset = 0;
        start_editing(0);
      }

      save_to_file();
      redraw();
    }
  }

  void toggle_complete(int index) {
    if (index >= 0 && index < (int)items.size()) {
      items[index].completed = !items[index].completed;
      save_to_file();
      redraw();
    }
  }

  int handle(int event) override {
    int mx = Fl::event_x();
    int my = Fl::event_y();
    int start_y = 0;

    switch (event) {
    case FL_PUSH: {
      // Finish editing if clicking elsewhere
      if (editing_index >= 0 && Fl::event_button() == FL_LEFT_MOUSE) {
        int clicked_index = get_item_at_y(my);
        if (clicked_index != editing_index) {
          finish_editing();
        }
      }

      // Reset all swipe offsets when starting new interaction
      for (auto &item : items) {
        item.swipe_offset = 0;
      }

      int index = get_item_at_y(my);

      // Check if we're in the pull-down zone (above all items, not on first
      // item) Pull zone is only above the first item, not including the first
      // item itself
      bool in_pull_zone = (my < start_y && index < 0);

      if (index >= 0) {
        // Clicked on an item (including first item)
        if (Fl::event_button() == FL_LEFT_MOUSE) {
          // Start potential drag - but need long press for reordering
          selected_index = index;
          is_dragging = false; // Don't allow dragging yet
          is_swiping = false;
          is_pulling_down = false;
          can_reorder = false; // Need long press first
          drag_start_y = my;
          drag_start_x = mx;
          drag_offset = my - (start_y + index * item_height);
          pending_click_index = -1; // Reset pending click

          // Start long press timer (0.3 seconds) for reordering
          Fl::add_timeout(0.3, long_press_timeout_cb, this);
          redraw();
        } else if (Fl::event_button() == FL_RIGHT_MOUSE) {
          // Right-click to delete
          if (editing_index >= 0) {
            finish_editing();
          }
          delete_item(index);
        }
      } else if (in_pull_zone && Fl::event_button() == FL_LEFT_MOUSE) {
        // Click in pull-down zone (above all items) - start pull down
        is_dragging = true;
        is_pulling_down = true;
        drag_start_y = my;
        drag_start_x = mx;
        pull_down_offset = 0;
        selected_index = -1;
        redraw();
      }
      return 1;
    }

    case FL_DRAG: {
      int dx = mx - drag_start_x;
      int dy = my - drag_start_y;

      if (is_pulling_down) {
        // Pull down gesture - immediate response, no long press needed
        if (dy > 0) {
          pull_down_offset = dy;
          if (pull_down_offset > item_height * 1.5) {
            pull_down_offset = item_height * 1.5;
          }
          redraw();
        } else if (dy < -5) {
          // Pulled back up significantly, cancel
          pull_down_offset = 0;
          redraw();
        }
      } else if (selected_index >= 0) {
        // Check if dragging down to add new item (immediate, no long press
        // needed) Only allow this if dragging down significantly and not
        // swiping horizontally
        if (!can_reorder && dy > 20 && abs(dx) < 30) {
          // Dragging down - switch to pull-down mode to add new item
          Fl::remove_timeout(long_press_timeout_cb, this);
          is_pulling_down = true;
          is_dragging = true;
          selected_index = -1;
          pull_down_offset = dy;
          redraw();
        } else if (!can_reorder) {
          // Check if this is a horizontal swipe (for complete or delete)
          if (abs(dx) > 10 && abs(dx) > abs(dy)) {
            // Horizontal swipe - allow it immediately
            is_swiping = true;
            is_dragging = true;
            items[selected_index].swipe_offset =
                dx; // Can be positive (right) or negative (left)
            redraw();
          } else if (abs(dx) > 5 || abs(dy) > 5) {
            // Moved but not dragging down or swiping - cancel long press timer
            Fl::remove_timeout(long_press_timeout_cb, this);
            can_reorder = false;
            // Don't allow dragging yet
            return 1;
          } else {
            // Small movement, wait for long press
            return 1;
          }
        }

        // Only allow reordering after long press
        if (!can_reorder && !is_swiping) {
          return 1;
        }

        // Now we can drag for reordering or swiping
        if (!is_swiping) {
          is_dragging = true;
        }

        // Determine if this is a horizontal swipe or vertical drag
        if (!is_swiping && abs(dx) > 10) {
          is_swiping = true;
        }

        if (is_swiping) {
          // Horizontal swipe - can be right (complete) or left (delete)
          items[selected_index].swipe_offset = dx;
          redraw();
        } else if (can_reorder && abs(dy) > 10) {
          // Vertical drag for reordering (only after long press)
          int new_index = get_item_at_y(my);
          if (new_index >= 0 && new_index != selected_index) {
            reorder_items(selected_index, new_index);
            selected_index = new_index;
          }
          redraw();
        }
      }
      return 1;
    }

    case FL_RELEASE: {
      // Cancel long press timer if still waiting
      Fl::remove_timeout(long_press_timeout_cb, this);

      if (is_pulling_down) {
        // If pulled down enough, create new item
        if (pull_down_offset > item_height * 0.6) {
          // Add new item (this will reset pull down state and start editing)
          add_item("");
        } else {
          pull_down_offset = 0;
          is_pulling_down = false;
        }
      } else if (is_dragging && selected_index >= 0 && can_reorder) {
        if (is_swiping) {
          // Handle swipe based on direction
          int swipe_offset = items[selected_index].swipe_offset;
          if (swipe_offset < -w() * 0.3) {
            // Swiped left far enough - delete
            if (editing_index == selected_index) {
              editing_index = -1;
              editing_text = "";
            }
            delete_item(selected_index);
          } else if (swipe_offset > w() * 0.3) {
            // Swiped right far enough - complete
            toggle_complete(selected_index);
            items[selected_index].swipe_offset = 0;
          } else {
            // Snap back
            items[selected_index].swipe_offset = 0;
          }
        }
      } else if (selected_index >= 0) {
        // Released without long press or without dragging
        int dx = mx - drag_start_x;
        int dy = my - drag_start_y;

        if (is_swiping) {
          // Handle swipe based on direction
          int swipe_offset = items[selected_index].swipe_offset;
          if (swipe_offset < -w() * 0.3) {
            // Swiped left far enough - delete
            if (editing_index == selected_index) {
              editing_index = -1;
              editing_text = "";
            }
            delete_item(selected_index);
          } else if (swipe_offset > w() * 0.3) {
            // Swiped right far enough - complete
            toggle_complete(selected_index);
            items[selected_index].swipe_offset = 0;
          } else {
            // Snap back
            items[selected_index].swipe_offset = 0;
          }
        } else if (abs(dx) < 5 && abs(dy) < 5 && !can_reorder) {
          // Small movement - treat as click
          // Check if this is a double-click
          if (Fl::event_clicks() > 0) {
            // Double-click - toggle complete
            // Cancel any pending single click
            if (pending_click_index >= 0) {
              Fl::remove_timeout(click_timeout_cb, this);
              pending_click_index = -1;
            }
            if (editing_index < 0) {
              toggle_complete(selected_index);
            }
          } else {
            // Single click - delay to check for double-click
            // Cancel previous pending click if any
            if (pending_click_index >= 0) {
              Fl::remove_timeout(click_timeout_cb, this);
            }
            pending_click_index = selected_index;
            Fl::add_timeout(0.3, click_timeout_cb, this);
          }
        }
      }

      is_dragging = false;
      is_swiping = false;
      can_reorder = false;
      redraw();
      return 1;
    }

    case FL_MOUSEWHEEL: {
      // Handle mouse wheel scrolling
      int dy = Fl::event_dy();
      if (dy != 0) {
        scroll_offset -= dy * item_height; // Negative because scrolling down
                                           // should increase offset
        clamp_scroll_offset();
        redraw();
        return 1;
      }
      break;
    }

    case FL_KEYBOARD: {
      // If editing, only handle Escape key, let Fl_Input handle everything else
      if (editing_index >= 0 && input_widget && input_widget->visible()) {
        int key = Fl::event_key();
        if (key == FL_Escape) {
          // Cancel editing
          std::string current_text =
              input_widget->value() ? input_widget->value() : "";
          if (current_text.empty() && editing_index < (int)items.size()) {
            items.erase(items.begin() + editing_index);
            if (items.empty()) {
              items.push_back(TodoItem(""));
              editing_index = 0;
              editing_text = "";
              start_editing(0);
              return 1;
            }
          }
          input_widget->hide();
          editing_index = -1;
          editing_text = "";
          save_to_file();
          redraw();
          return 1;
        }
        // For all other keys, don't intercept - let Fl_Input handle them
        // Call the parent handle to let event propagate naturally
        break; // Don't handle, let it fall through to parent or Fl_Input
      } else if (Fl::event_key() == FL_Delete && selected_index >= 0) {
        delete_item(selected_index);
        selected_index = -1;
        return 1;
      }
      break;
    }
    }

    return Fl_Window::handle(event);
  }

  void draw() override {
    Fl_Window::draw();

    int start_y = 0;
    int y = start_y;

    // Draw pull-down new item if pulling
    if (is_pulling_down && pull_down_offset > 0) {
      int new_item_y = start_y - item_height + pull_down_offset - scroll_offset;
      // Only draw if visible
      if (new_item_y + item_height > 0 && new_item_y < h()) {
        // Draw the new item being pulled down
        // New item will be at position 0, so use red color
        Fl_Color new_color = get_color_by_position(0, items.size() + 1);
        fl_color(new_color);
        fl_rectf(0, new_item_y, w(), item_height);

        // Use appropriate text color based on background
        Fl_Color text_color = get_text_color(new_color);
        fl_color(text_color);
        fl_font(FL_HELVETICA_BOLD, 18);
        if (pull_down_offset > item_height * 0.6) {
          fl_draw("Release to add...", 20, new_item_y + item_height / 2 + 6);
        } else {
          fl_draw("Pull down to add...", 20, new_item_y + item_height / 2 + 6);
        }
      }
    }

    // Get sorted indices (incomplete first, then completed)
    std::vector<int> sorted_indices = get_sorted_indices();
    
    // Draw all items (shift down when pulling, adjust for scroll)
    // Use sorted indices so completed items appear at bottom
    for (size_t visual_pos = 0; visual_pos < sorted_indices.size(); visual_pos++) {
      int actual_index = sorted_indices[visual_pos];
      int item_y = y - scroll_offset;
      if (is_pulling_down && pull_down_offset > 0) {
        // Shift all items down when pulling
        item_y += pull_down_offset;
      }
      // Only draw if item is visible (optimization)
      if (item_y + item_height > 0 && item_y < h()) {
        // Pass visual position for color calculation, but use actual index for item data
        draw_item(actual_index, item_y, (actual_index == editing_index), visual_pos, sorted_indices.size());
      }
      y += item_height;
    }

    // Update Fl_Input position if editing
    if (editing_index >= 0 && editing_index < (int)items.size() &&
        input_widget && input_widget->visible()) {
      int start_y = 0;
      // Find visual position of editing item in sorted list
      int visual_pos = -1;
      for (size_t i = 0; i < sorted_indices.size(); i++) {
        if (sorted_indices[i] == editing_index) {
          visual_pos = i;
          break;
        }
      }
      int item_y = start_y;
      if (visual_pos >= 0) {
        item_y = start_y + visual_pos * item_height - scroll_offset;
      } else {
        // Fallback to old calculation if not found in sorted list
        item_y = start_y + editing_index * item_height - scroll_offset;
      }
      if (is_pulling_down && pull_down_offset > 0) {
        item_y += pull_down_offset;
      }

      // Ensure editing item is visible by scrolling if needed
      if (item_y < 0) {
        scroll_offset += item_y;
        clamp_scroll_offset();
        if (visual_pos >= 0) {
          item_y = start_y + visual_pos * item_height - scroll_offset;
        } else {
          item_y = start_y + editing_index * item_height - scroll_offset;
        }
        if (is_pulling_down && pull_down_offset > 0) {
          item_y += pull_down_offset;
        }
      } else if (item_y + item_height > h() - 40) {
        scroll_offset += (item_y + item_height) - (h() - 40);
        clamp_scroll_offset();
        if (visual_pos >= 0) {
          item_y = start_y + visual_pos * item_height - scroll_offset;
        } else {
          item_y = start_y + editing_index * item_height - scroll_offset;
        }
        if (is_pulling_down && pull_down_offset > 0) {
          item_y += pull_down_offset;
        }
      }

      // Adjust for swipe offset
      int x_offset = items[editing_index].swipe_offset;
      int abs_offset = abs(x_offset);
      int bg_x = (x_offset > 0) ? abs_offset : 0;
      // Add 20 pixels left padding to match non-editing text position
      int input_x = bg_x + 20;
      int input_w = w() - abs_offset - 20;
      if (input_w < 0)
        input_w = 0;

      // Update position and colors
      input_widget->resize(input_x, item_y, input_w, item_height);

      // Use visual position for color if available, otherwise use actual index
      Fl_Color item_color;
      if (items[editing_index].completed) {
        item_color = fl_rgb_color(64, 64, 64); // Dark gray for completed items
      } else if (visual_pos >= 0) {
        item_color = get_color_by_position(visual_pos, sorted_indices.size());
      } else {
        item_color = get_color_by_position(editing_index, items.size());
      }
      Fl_Color text_color = get_text_color(item_color);
      input_widget->color(item_color);
      input_widget->textcolor(text_color);
      // Set selection color for cursor visibility
      Fl_Color selection_color = (text_color == FL_BLACK) ? FL_BLUE : FL_YELLOW;
      input_widget->selection_color(selection_color);
      // Force redraw
      input_widget->redraw();
    }

    // Draw instructions at bottom
    fl_color(FL_WHITE);
    fl_font(FL_HELVETICA, 12);
    fl_draw("Pull down to add | Click to edit | Double-click to complete | "
            "Swipe right to delete",
            10, h() - 20);

    // Draw error message in bottom right corner
    if (error_display.is_visible && !error_display.message.empty()) {
      const int font_size = 14;
      const int padding = 12;
      const int margin = 10;
      const int corner_radius = 8;

      fl_font(FL_HELVETICA_BOLD, font_size);

      // Measure text accurately
      int text_w, text_h;
      measure_text(error_display.message, text_w, text_h, font_size);

      // Calculate box dimensions with padding
      int box_w = text_w + padding * 2;
      int box_h = text_h + padding * 2;

      // Ensure minimum width for better appearance
      if (box_w < 200)
        box_w = 200;

      // Position in bottom right corner (above instructions)
      int box_x = w() - box_w - margin;
      int box_y = h() - box_h - 35; // Above the instruction text

      // Save current drawing state
      fl_push_clip(box_x, box_y, box_w, box_h);

      // Draw shadow for depth (offset by 2 pixels) with rounded corners
      fl_color(fl_rgb_color(20, 20, 20)); // Dark gray for shadow effect
      draw_rounded_rect(box_x + 2, box_y + 2, box_w, box_h, corner_radius);

      // Draw rounded rectangle background with 90% opacity (10% transparency)
      // For 90% opacity on white background (255,255,255):
      // Target color = base_color * 0.9 + white * 0.1
      // If we want final color around (60,60,60):
      // base_color = (60 - 255*0.1) / 0.9 ≈ 38
      // Final = 38*0.9 + 255*0.1 = 34.2 + 25.5 ≈ 60
      // Using base color (40,40,40) for better visibility:
      // Final = 40*0.9 + 255*0.1 = 36 + 25.5 ≈ 62
      fl_color(fl_rgb_color(62, 62, 62));
      draw_rounded_rect(box_x, box_y, box_w, box_h, corner_radius);

      // Restore clipping
      fl_pop_clip();

      // Draw error text in white
      fl_color(FL_WHITE);
      fl_draw(error_display.message.c_str(), box_x + padding,
              box_y + padding + text_h - 4);
    }
  }

  void handle_single_click(int index) {
    if (index >= 0 && index < (int)items.size() &&
        pending_click_index == index) {
      // This is a single click (not a double-click), start editing
      if (editing_index != index) {
        start_editing(index);
      }
      pending_click_index = -1;
    }
  }

  void enable_reorder() {
    if (selected_index >= 0) {
      can_reorder = true;
      is_dragging = true;
      redraw();
    }
  }

  static void input_callback(Fl_Widget *widget, void *data) {
    ClearApp *app = static_cast<ClearApp *>(data);
    Fl_Input *input = app->input_widget;
    if (!input || !input->visible())
      return;

    // Check if Enter was pressed
    int key = Fl::event_key();
    if (key == FL_Enter || key == FL_KP_Enter) {
      app->finish_editing();
      return;
    }

    // Text changed - force immediate redraw to show input in real-time
    input->damage(FL_DAMAGE_ALL);
    input->redraw();
    // Process events immediately to update display
    Fl::check();
    Fl::flush();
  }

  static void click_timeout_cb(void *data) {
    ClearApp *app = static_cast<ClearApp *>(data);
    if (app->pending_click_index >= 0) {
      app->handle_single_click(app->pending_click_index);
    }
  }

  static void long_press_timeout_cb(void *data) {
    ClearApp *app = static_cast<ClearApp *>(data);
    app->enable_reorder();
  }
};

int main(int argc, char **argv) {
  ClearApp *app =
      new ClearApp(600, 800, "Clear-txt - Todo List with .txt file.");
  app->show(argc, argv);
  return Fl::run();
}
