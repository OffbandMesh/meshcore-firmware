#pragma once

// The boot screen (Epic G G6, #1571): full-bleed over the first screen for
// a moment after the GUI starts, or until a button or touch. As drawn
// (ObScreen.dc.html, boot; mockup 18): the mark, the name, the versions, the
// checks setup left behind, the bar, and a line only after an unclean reset.
// The bar is "offband" in Morse, lighting as the hold runs (mockup 18b).

#include <lvgl.h>

#include <helpers/gui/BootViews.h>
#include <helpers/gui/GuiTheme.h>

class BootScreen {
public:
  static constexpr uint32_t kHoldMs = 2500;
  static constexpr uint32_t kSpelledMs = 400;   // the whole word shows this long at the end

  // version: bootVersionLine(); reset_line: empty after a clean start.
  void show(const offband::gui::ThemeTokens& t, const char* version, const char* reset_line, uint32_t now_ms);
  // The rows as they stand (GPS may be heard from while it's up) and the bar.
  void update(const offband::gui::BootFacts& f, uint32_t now_ms);
  void tick(uint32_t now_ms);   // the Morse bar only: cheap, every loop pass
  bool open() const { return _root != nullptr; }
  bool expired(uint32_t now_ms) const { return open() && now_ms - _shown_at >= kHoldMs; }
  void close();

private:
  offband::gui::ThemeTokens _t{};
  lv_obj_t* _root = nullptr;
  lv_obj_t* _checks = nullptr;
  static constexpr size_t kMarksMax = 32;
  lv_obj_t* _marks[kMarksMax] = {};   // the Morse bar's dots and dashes, in order
  size_t _nmarks = 0;
  size_t _lit = 0;
  offband::gui::BootRow _shown[offband::gui::kBootRowsMax] = {};   // the rows drawn now
  size_t _nshown = 0;
  uint32_t _shown_at = 0;
};
