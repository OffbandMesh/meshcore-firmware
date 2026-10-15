#include "GuiFonts.h"

LV_FONT_DECLARE(ob_font_title_18)
LV_FONT_DECLARE(ob_font_body_14)
LV_FONT_DECLARE(ob_font_body_12)
LV_FONT_DECLARE(ob_font_label_9)
LV_FONT_DECLARE(ob_font_data_14)
LV_FONT_DECLARE(ob_font_meta_10)
LV_FONT_DECLARE(ob_font_clock_46)

namespace offband {
namespace gui {

const lv_font_t* font(FontRole role) {
  switch (role) {
    case FontRole::Title: return &ob_font_title_18;
    case FontRole::Body: return &ob_font_body_14;
    case FontRole::BodySmall: return &ob_font_body_12;
    case FontRole::Label: return &ob_font_label_9;
    case FontRole::Data: return &ob_font_data_14;
    case FontRole::Meta: return &ob_font_meta_10;
    case FontRole::Clock: return &ob_font_clock_46;
  }
  return &ob_font_body_14;
}

}  // namespace gui
}  // namespace offband
