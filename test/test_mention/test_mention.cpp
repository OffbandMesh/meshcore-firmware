// #510/#524: @[name] mention matcher — the cross-repo contract with the client.
//
// Two rules, deliberately asymmetric (be strict in what you send, liberal in what
// you accept):
//   SENDER   emits the advert name byte-for-byte, unnormalised   (client#497)
//   RECEIVER accepts the raw name OR its whitespace-trimmed form (#524)
//
// The device's real advert name contains a multi-byte emoji AND a variation
// selector: "Strycher T1000\xF0\x9F\x9B\xB0\xEF\xB8\x8F" (U+1F6F0 + U+FE0F), and in
// the field it also carried a trailing space that the client trimmed out of the
// mention token. Both cases are covered below.
#include <gtest/gtest.h>
#include <string.h>
#include <stddef.h>

// Mirror of the shipped implementation in MyMesh.cpp — kept logically identical so
// this suite exercises the real algorithm.

// #524: byte-explicit, NOT isspace(). isspace() on a plain (signed) char >= 0x80 is
// undefined behaviour, and every byte of a multi-byte UTF-8 sequence is >= 0x80.
static inline bool isAsciiSpace(char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static void trimView(const char* s, size_t len, const char** out_begin, size_t* out_len) {
  size_t a = 0, b = len;
  while (a < b && isAsciiSpace(s[a])) a++;
  while (b > a && isAsciiSpace(s[b - 1])) b--;
  *out_begin = s + a;
  *out_len   = b - a;
}

static bool mentions(const char* text, const char* node_name) {
  if (text == NULL || node_name[0] == 0) return false;
  const size_t name_len = strlen(node_name);
  if (name_len == 0) return false;

  const char* tname; size_t tname_len;
  trimView(node_name, name_len, &tname, &tname_len);

  for (const char* p = text; *p != 0; p++) {
    if (p[0] != '@' || p[1] != '[') continue;
    const char* cand = p + 2;
    const char* close = strchr(cand, ']');
    if (close == NULL) continue;
    const size_t cand_len = (size_t)(close - cand);

    if (cand_len == name_len && strncasecmp(cand, node_name, name_len) == 0) return true;

    const char* tcand; size_t tcand_len;
    trimView(cand, cand_len, &tcand, &tcand_len);
    if (tname_len != 0 && tcand_len == tname_len &&
        strncasecmp(tcand, tname, tname_len) == 0) return true;
  }
  return false;
}

#define EMOJI_NAME "Strycher T1000\xF0\x9F\x9B\xB0\xEF\xB8\x8F"
#define NO_VS_NAME "Strycher T1000\xF0\x9F\x9B\xB0"

// ---------------------------------------------------------------- exact matching
TEST(Mention, PlainAsciiMatches)       { EXPECT_TRUE (mentions("@[Bench] hi", "Bench")); }
TEST(Mention, CaseInsensitiveAscii)    { EXPECT_TRUE (mentions("@[BENCH] hi", "bench")); }
TEST(Mention, BareAtDoesNotMatch)      { EXPECT_FALSE(mentions("@Bench hi",   "Bench")); }
TEST(Mention, WrongNameDoesNotMatch)   { EXPECT_FALSE(mentions("@[Other] hi", "Bench")); }
TEST(Mention, PrefixOnlyDoesNotMatch)  { EXPECT_FALSE(mentions("@[Ben] hi",   "Bench")); }
TEST(Mention, SuffixExtraDoesNotMatch) { EXPECT_FALSE(mentions("@[Benchx] hi","Bench")); }

// ------------------------------------------------------------------ emoji names
TEST(Mention, EmojiNameExactMatches) {
  EXPECT_TRUE(mentions("@[" EMOJI_NAME "] Test message 2.", EMOJI_NAME));
}
TEST(Mention, EmojiNameMidSentence) {
  EXPECT_TRUE(mentions("hey @[" EMOJI_NAME "] you there", EMOJI_NAME));
}
TEST(Mention, EmojiNameCaseInsensitiveAsciiPart) {
  EXPECT_TRUE(mentions("@[strycher t1000\xF0\x9F\x9B\xB0\xEF\xB8\x8F] yo", EMOJI_NAME));
}
// Visually identical, three bytes apart. Must NOT match — a real codepoint
// difference, not edge whitespace, so #524 tolerance does not apply.
TEST(Mention, EmojiMissingVariationSelector_STILL_NO_MATCH) {
  EXPECT_FALSE(mentions("@[" NO_VS_NAME "] hi", EMOJI_NAME));
}
TEST(Mention, EmojiExtraVariationSelector_STILL_NO_MATCH) {
  EXPECT_FALSE(mentions("@[" EMOJI_NAME "] hi", NO_VS_NAME));
}

// ------------------------------------------------- #524 whitespace tolerance
// THE FIELD CASE: device name had a trailing space; the client trimmed it out of
// the token. This is exactly what silently failed on the T1000-E.
TEST(Mention, NameTrailingSpace_TokenWithout_MATCHES) {
  EXPECT_TRUE(mentions("@[" EMOJI_NAME "] Test message 16.", EMOJI_NAME " "));
}
TEST(Mention, NameLeadingSpace_TokenWithout_MATCHES) {
  EXPECT_TRUE(mentions("@[Bench] hi", " Bench"));
}
// Reverse: our name is clean, some other client padded the token.
TEST(Mention, TokenTrailingSpace_NameWithout_MATCHES) {
  EXPECT_TRUE(mentions("@[Bench ] hi", "Bench"));
}
TEST(Mention, TokenLeadingSpace_NameWithout_MATCHES) {
  EXPECT_TRUE(mentions("@[ Bench] hi", "Bench"));
}
TEST(Mention, BothPaddedDifferently_MATCHES) {
  EXPECT_TRUE(mentions("@[  Bench ] hi", " Bench  "));
}
TEST(Mention, TabAndNewlineTreatedAsEdgeWhitespace) {
  EXPECT_TRUE(mentions("@[\tBench\r\n] hi", "Bench"));
}

// -------------------------------------------- tolerance must NOT become laxity
// Interior whitespace is part of the name, not an edge — never forgiven.
TEST(Mention, InteriorSpaceDifference_NO_MATCH) {
  EXPECT_FALSE(mentions("@[Ben ch] hi", "Bench"));
}
TEST(Mention, InteriorDoubleSpace_NO_MATCH) {
  EXPECT_FALSE(mentions("@[Ben  ch] hi", "Ben ch"));
}
// A non-whitespace difference still fails even with padding present.
TEST(Mention, NonWhitespaceDifferenceWithPadding_NO_MATCH) {
  EXPECT_FALSE(mentions("@[ Bencj ] hi", "Bench "));
}

// -------------------------------------------------------------- edges / safety
TEST(Mention, EmptyNameNeverMatches) { EXPECT_FALSE(mentions("@[] hi", "")); }
// An all-whitespace name trims to EMPTY. Without a guard the trimmed tier would
// then match every token in existence — the worst possible false positive. The
// `tname_len != 0` guard blocks that, and these pin the behaviour down.
TEST(Mention, AllWhitespaceName_DoesNotMatchArbitraryToken) {
  EXPECT_FALSE(mentions("@[Bench] hi", "   "));   // the dangerous case
  EXPECT_FALSE(mentions("@[x] hi",     "   "));
  EXPECT_FALSE(mentions("@[] hi",      "   "));   // empty token, empty trimmed name
}
TEST(Mention, AllWhitespaceName_DifferentLengthWhitespace_NoMatch) {
  EXPECT_FALSE(mentions("@[ ] hi", "   "));       // exact fails on length, trimmed guarded
}
// Exact byte-for-byte still wins: if the name really is three spaces and the sender
// reproduced it exactly, that is a correct mention. Tier 1 is content-agnostic by
// design — it compares bytes, it does not judge them.
TEST(Mention, AllWhitespaceName_ExactTokenMatches) {
  EXPECT_TRUE(mentions("@[   ] hi", "   "));
}
TEST(Mention, UnterminatedBracket) { EXPECT_FALSE(mentions("@[Bench hi", "Bench")); }
TEST(Mention, EmptyToken_NoMatch)  { EXPECT_FALSE(mentions("@[] hi", "Bench")); }
TEST(Mention, SecondTokenMatches) {
  EXPECT_TRUE(mentions("@[Someone] and @[Bench] hi", "Bench"));
}

int main(int argc, char **argv) { ::testing::InitGoogleTest(&argc, argv); return RUN_ALL_TESTS(); }
