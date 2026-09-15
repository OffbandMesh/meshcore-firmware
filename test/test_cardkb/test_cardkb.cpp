#include <gtest/gtest.h>
#include <string>
#include <helpers/ui/CardKbKeys.h>

namespace {

// M5Stack CardKB key map, transcribed from M5's keyboard firmware
// (m5stack/M5-ProductExampleCodes, Unit/CARDKB/firmware_328p/CardKeyBoard.ino).
// Columns: normal, shift, long_shift, sym, long_sym, fn, long_fn.
const uint8_t kM5KeyMap[48][7] = {
  {27, 27, 27, 27, 27, 128, 128},          // esc
  {'1', '1', '1', '!', '!', 129, 129},
  {'2', '2', '2', '@', '@', 130, 130},
  {'3', '3', '3', '#', '#', 131, 131},
  {'4', '4', '4', '$', '$', 132, 132},
  {'5', '5', '5', '%', '%', 133, 133},
  {'6', '6', '6', '^', '^', 134, 134},
  {'7', '7', '7', '&', '&', 135, 135},
  {'8', '8', '8', '*', '*', 136, 136},
  {'9', '9', '9', '(', '(', 137, 137},
  {'0', '0', '0', ')', ')', 138, 138},
  {8, 127, 127, 8, 8, 139, 139},           // del
  {9, 9, 9, 9, 9, 140, 140},               // tab
  {'q', 'Q', 'Q', '{', '{', 141, 141},
  {'w', 'W', 'W', '}', '}', 142, 142},
  {'e', 'E', 'E', '[', '[', 143, 143},
  {'r', 'R', 'R', ']', ']', 144, 144},
  {'t', 'T', 'T', '/', '/', 145, 145},
  {'y', 'Y', 'Y', '\\', '\\', 146, 146},
  {'u', 'U', 'U', '|', '|', 147, 147},
  {'i', 'I', 'I', '~', '~', 148, 148},
  {'o', 'O', 'O', '\'', '\'', 149, 149},
  {'p', 'P', 'P', '"', '"', 150, 150},
  {0, 0, 0, 0, 0, 0, 0},                   // no key
  {180, 180, 180, 180, 180, 152, 152},     // left
  {181, 181, 181, 181, 181, 153, 153},     // up
  {'a', 'A', 'A', ';', ';', 154, 154},
  {'s', 'S', 'S', ':', ':', 155, 155},
  {'d', 'D', 'D', '`', '`', 156, 156},
  {'f', 'F', 'F', '+', '+', 157, 157},
  {'g', 'G', 'G', '-', '-', 158, 158},
  {'h', 'H', 'H', '_', '_', 159, 159},
  {'j', 'J', 'J', '=', '=', 160, 160},
  {'k', 'K', 'K', '?', '?', 161, 161},
  {'l', 'L', 'L', 0, 0, 162, 162},
  {13, 13, 13, 13, 13, 163, 163},          // enter
  {182, 182, 182, 182, 182, 164, 164},     // down
  {183, 183, 183, 183, 183, 165, 165},     // right
  {'z', 'Z', 'Z', 0, 0, 166, 166},
  {'x', 'X', 'X', 0, 0, 167, 167},
  {'c', 'C', 'C', 0, 0, 168, 168},
  {'v', 'V', 'V', 0, 0, 169, 169},
  {'b', 'B', 'B', 0, 0, 170, 170},
  {'n', 'N', 'N', 0, 0, 171, 171},
  {'m', 'M', 'M', 0, 0, 172, 172},
  {',', ',', ',', '<', '<', 173, 173},
  {'.', '.', '.', '>', '>', 174, 174},
  {' ', ' ', ' ', ' ', ' ', 175, 175},     // space
};
constexpr int kFnColumn = 5;

std::string name(uint8_t raw) {
  char buf[16];
  cardkb::keyName(raw, buf, sizeof buf);
  return buf;
}

}  // namespace

TEST(CardKb, EveryKeyInM5sMapReachesTheUiAsTheRightKey) {
  for (int row = 0; row < 48; row++) {
    for (int col = 0; col < 7; col++) {
      const uint8_t raw = kM5KeyMap[row][col];
      const uint8_t ui = cardkb::toUiKey(raw);
      SCOPED_TRACE(testing::Message() << "row " << row << " col " << col << " raw " << int(raw));
      if (raw == 0) {
        EXPECT_EQ(0, ui);                          // no key
      } else if (col >= kFnColumn) {
        EXPECT_EQ(0, ui);                          // the Fn layer is not used yet
      } else if (raw == 8 || raw == 127) {
        EXPECT_EQ(KEY_BACKSPACE, ui);              // Del, and Shift+Del
      } else if (raw == 9) {
        EXPECT_EQ(KEY_TAB, ui);
      } else if (raw == 13) {
        EXPECT_EQ(KEY_ENTER, ui);
      } else if (raw == 27) {
        EXPECT_EQ(KEY_CANCEL, ui);
      } else if (raw >= 180 && raw <= 183) {
        EXPECT_EQ(raw, ui);                        // KEY_LEFT..KEY_RIGHT
      } else {
        EXPECT_GE(raw, 0x20);                      // everything else is printable ASCII
        EXPECT_LE(raw, 0x7E);
        EXPECT_EQ(raw, ui);
      }
    }
  }
}

TEST(CardKb, ArrowsAreTheUisArrowKeys) {
  EXPECT_EQ(KEY_LEFT, cardkb::toUiKey(180));
  EXPECT_EQ(KEY_UP, cardkb::toUiKey(181));
  EXPECT_EQ(KEY_DOWN, cardkb::toUiKey(182));
  EXPECT_EQ(KEY_RIGHT, cardkb::toUiKey(183));
}

TEST(CardKb, IdleAndMissingKeyboardDeliverNothing) {
  EXPECT_EQ(0, cardkb::toUiKey(0x00));   // an idle read: the keyboard had no key waiting
  EXPECT_EQ(0, cardkb::toUiKey(0xFF));   // what a bus with no device on it reads as
}

TEST(CardKb, NothingOutsideTheKeyMapGetsThrough) {
  for (int raw = 1; raw < 256; raw++) {
    const bool known = (raw >= 0x20 && raw <= 0x7E) || raw == 8 || raw == 9 || raw == 13 ||
                       raw == 27 || raw == 127 || (raw >= 180 && raw <= 183);
    if (!known) EXPECT_EQ(0, cardkb::toUiKey((uint8_t)raw)) << "raw " << raw;
  }
}

TEST(CardKb, NamesTheSpecialKeys) {
  EXPECT_EQ("ESC", name(27));
  EXPECT_EQ("DEL", name(8));
  EXPECT_EQ("DEL", name(127));
  EXPECT_EQ("TAB", name(9));
  EXPECT_EQ("ENTER", name(13));
  EXPECT_EQ("SPACE", name(32));
  EXPECT_EQ("LEFT", name(180));
  EXPECT_EQ("UP", name(181));
  EXPECT_EQ("DOWN", name(182));
  EXPECT_EQ("RIGHT", name(183));
  EXPECT_EQ("'a'", name('a'));
  EXPECT_EQ("'{'", name('{'));
  EXPECT_EQ("", name(0));
}

TEST(CardKb, NamesAnFnCodeByTheKeyUnderIt) {
  // The Fn layer sends 128 + the key's position in M5's map.
  for (int row = 0; row < 48; row++) {
    const uint8_t fn = kM5KeyMap[row][kFnColumn];
    const uint8_t base = kM5KeyMap[row][0];
    if (base == 0) continue;                       // the map's empty slot
    EXPECT_EQ(128 + row, fn);
    // A printable key is named by its bare character (FN+1), anything else by its name.
    const std::string key = (base > 0x20 && base <= 0x7E) ? std::string(1, (char)base) : name(base);
    EXPECT_EQ("FN+" + key, name(fn)) << "row " << row;
  }
}

// #1233: Fn+S opens Settings from anywhere (design 3a), so the key under an Fn code
// has to be recoverable.
TEST(CardKb, FnBaseNamesTheKeyUnderAnFnCode) {
  EXPECT_EQ('s', cardkb::fnBase(155));   // 128 + the index of 's'
  EXPECT_EQ('1', cardkb::fnBase(129));
  EXPECT_EQ(0, cardkb::fnBase('s'));     // a plain key is not an Fn code
  EXPECT_EQ(0, cardkb::fnBase(0));
  EXPECT_EQ(0, cardkb::fnBase(151));     // the map's one empty slot
}

TEST(CardKb, NamesFitAShortBufferAndStayTerminated) {
  char buf[6];
  cardkb::keyName(183, buf, sizeof buf);          // "RIGHT" exactly fills it
  EXPECT_STREQ("RIGHT", buf);
  cardkb::keyName(128 + 35, buf, sizeof buf);     // "FN+ENTER" is truncated
  EXPECT_STREQ("FN+EN", buf);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
