#include "BootScreen.h"

#include "ShellWidgets.h"

using namespace offband::gui;
using namespace shell;

extern "C" const lv_img_dsc_t ob_logo_64;

void BootScreen::show(const ThemeTokens& t, const char* version, const char* reset_line, uint32_t now_ms,
                      bool landscape) {
  close();
  _t = t;
  _shown_at = now_ms;
  _root = box(lv_layer_top(), LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_opa(_root, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(_root, c(t.bg), 0);
  lv_obj_set_flex_flow(_root, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(_root, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(_root, 6, 0);
  // A touch ends it, as a button does; deleted after the event, not in it.
  lv_obj_add_flag(_root, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(
      _root, [](lv_event_t* e) { static_cast<BootScreen*>(lv_event_get_user_data(e))->close(); }, LV_EVENT_CLICKED,
      this);

  lv_obj_t* id = _root;          // the mark, name and version
  lv_obj_t* checks_at = _root;   // the checks
  if (landscape) {
    lv_obj_t* pair = box(_root, 290, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(pair, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(pair, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(pair, 14, 0);
    id = box(pair, 120, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(id, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(id, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(id, 4, 0);
    checks_at = pair;
  }
  lv_obj_t* logo = lv_img_create(id);
  lv_img_set_src(logo, &ob_logo_64);
  lv_obj_set_style_img_recolor(logo, c(t.sig), 0);
  lv_obj_set_style_img_recolor_opa(logo, LV_OPA_COVER, 0);
  if (landscape) {
    // 40 px: the 64 px mark at 160/256, its box cut to the drawn size.
    lv_img_set_zoom(logo, 160);
    lv_img_set_size_mode(logo, LV_IMG_SIZE_MODE_REAL);
  }
  label(id, FontRole::Title, t.ink, "offband");
  lv_obj_t* ver = label(id, FontRole::Meta, t.dim, version);
  if (landscape) {
    lv_obj_set_width(ver, LV_PCT(100));
    lv_label_set_long_mode(ver, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(ver, LV_TEXT_ALIGN_CENTER, 0);
  }

  _checks = box(checks_at, landscape ? 156 : 200, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(_checks, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(_checks, 1, 0);
  lv_obj_set_style_pad_top(_checks, 4, 0);

  // The Morse bar: letters spread across 200 px, marks 3 px tall, a dot
  // 4 px and a dash 9 px wide, 3 px apart within a letter.
  lv_obj_t* bar = box(_root, 200, 3);
  lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  _nmarks = 0;
  _lit = 0;
  lv_obj_t* letter = nullptr;
  for (const char* p = kOffbandMorse; *p; p++) {
    if (*p == ' ') {
      letter = nullptr;
      continue;
    }
    if (!letter) {
      letter = box(bar, LV_SIZE_CONTENT, 3);
      lv_obj_set_flex_flow(letter, LV_FLEX_FLOW_ROW);
      lv_obj_set_style_pad_column(letter, 3, 0);
    }
    if (_nmarks >= kMarksMax) break;
    lv_obj_t* m = box(letter, *p == '-' ? 9 : 4, 3);
    lv_obj_set_style_bg_opa(m, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(m, c(t.line), 0);
    _marks[_nmarks++] = m;
  }

  if (reset_line && reset_line[0]) label(_root, FontRole::Meta, t.warn, reset_line);
}

void BootScreen::update(const BootFacts& f, uint32_t now_ms) {
  if (!_root) return;
  BootRow rows[kBootRowsMax] = {};
  const size_t n = bootRows(f, rows, kBootRowsMax);
  if (n == _nshown && memcmp(rows, _shown, sizeof rows) == 0) {   // nothing changed
    tick(now_ms);
    return;
  }
  memcpy(_shown, rows, sizeof rows);
  _nshown = n;
  lv_obj_clean(_checks);
  for (size_t i = 0; i < n; i++) {
    lv_obj_t* r = box(_checks, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(r, 6, 0);
    const uint32_t tag = rows[i].state == Check::Ok ? _t.sig : rows[i].state == Check::Pending ? _t.warn : _t.dim;
    label(r, FontRole::Meta, tag, checkTag(rows[i].state));
    label(r, FontRole::Meta, _t.ink2, rows[i].text);
  }
  tick(now_ms);
}

void BootScreen::tick(uint32_t now_ms) {
  if (!_root) return;
  // Light the marks the hold has reached; each lights once. The word is
  // complete kSpelledMs before the screen closes, so it's seen whole.
  const size_t lit = morseLit(bootProgress(now_ms - _shown_at, kHoldMs - kSpelledMs), _nmarks);
  for (; _lit < lit; _lit++) lv_obj_set_style_bg_color(_marks[_lit], c(_t.sig), 0);
}

void BootScreen::close() {
  if (!_root) return;
  lv_obj_del_async(_root);
  _root = _checks = nullptr;
  for (auto& m : _marks) m = nullptr;
  _nmarks = _lit = _nshown = 0;
}
