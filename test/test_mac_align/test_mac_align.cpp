// #1586: MACThenDecrypt must reject non-block-aligned ciphertext BEFORE handing it
// to decrypt(), which steps in CIPHER_BLOCK_SIZE blocks and rounds the block count
// UP -- so a 24-byte ciphertext made it write 32 bytes, 8 past the caller's buffer.
//
// The two tests that matter here forge a VALID 2-byte HMAC over a deliberately
// non-aligned payload. That is the point: an attacker who knows the channel secret
// does not have to win the 1-in-65536 HMAC guess, so the HMAC check cannot be what
// stops this. Only the alignment check can.

#include <gtest/gtest.h>
#include <SHA256.h>
#include "Utils.h"

using namespace mesh;

namespace {

const uint8_t kSecret[PUB_KEY_SIZE] = {
  0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
  0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10,
  0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
  0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20,
};

// The same HMAC the real sender computes in Utils::encryptThenMAC -- over the
// ciphertext only, truncated to CIPHER_MAC_SIZE. Lets a test build a frame whose
// MAC passes while its length does not.
void macOver(const uint8_t* ciphertext, int len, uint8_t* out_mac) {
  SHA256 sha;
  sha.resetHMAC(kSecret, PUB_KEY_SIZE);
  sha.update(ciphertext, len);
  sha.finalizeHMAC(kSecret, PUB_KEY_SIZE, out_mac, CIPHER_MAC_SIZE);
}

}  // namespace

// ---------------------------------------------------------------------------
// Conforming traffic is untouched. encrypt() zero-pads a partial block up to
// CIPHER_BLOCK_SIZE, so encryptThenMAC NEVER emits a non-aligned ciphertext --
// which is why rejecting non-aligned input costs real senders nothing.
// ---------------------------------------------------------------------------

TEST(MacAlign, ExactBlockRoundTripsUnchanged) {
  const uint8_t plain[CIPHER_BLOCK_SIZE] = {
    'o','f','f','b','a','n','d',' ','1','6','b','y','t','e','s','!'
  };
  uint8_t frame[CIPHER_MAC_SIZE + CIPHER_BLOCK_SIZE * 2];
  const int frame_len = Utils::encryptThenMAC(kSecret, frame, plain, sizeof(plain));

  ASSERT_EQ(CIPHER_MAC_SIZE + CIPHER_BLOCK_SIZE, frame_len);

  uint8_t out[CIPHER_BLOCK_SIZE * 2] = {0};
  EXPECT_EQ(CIPHER_BLOCK_SIZE, Utils::MACThenDecrypt(kSecret, out, frame, frame_len));
  EXPECT_EQ(0, memcmp(plain, out, sizeof(plain)));
}

TEST(MacAlign, ShortPlaintextIsPaddedAndStillRoundTrips) {
  const uint8_t plain[5] = { 'h','e','l','l','o' };
  uint8_t frame[CIPHER_MAC_SIZE + CIPHER_BLOCK_SIZE * 2];
  const int frame_len = Utils::encryptThenMAC(kSecret, frame, plain, sizeof(plain));

  // Padded up to one whole block, so the frame is aligned even though the
  // plaintext was not.
  ASSERT_EQ(CIPHER_MAC_SIZE + CIPHER_BLOCK_SIZE, frame_len);

  uint8_t out[CIPHER_BLOCK_SIZE * 2] = {0};
  EXPECT_EQ(CIPHER_BLOCK_SIZE, Utils::MACThenDecrypt(kSecret, out, frame, frame_len));
  EXPECT_EQ(0, memcmp(plain, out, sizeof(plain)));
}

TEST(MacAlign, MultiBlockRoundTripsUnchanged) {
  uint8_t plain[CIPHER_BLOCK_SIZE * 3];
  for (size_t i = 0; i < sizeof(plain); i++) plain[i] = (uint8_t)(i * 7 + 1);

  uint8_t frame[CIPHER_MAC_SIZE + CIPHER_BLOCK_SIZE * 4];
  const int frame_len = Utils::encryptThenMAC(kSecret, frame, plain, sizeof(plain));
  ASSERT_EQ(CIPHER_MAC_SIZE + (int)sizeof(plain), frame_len);

  uint8_t out[CIPHER_BLOCK_SIZE * 4] = {0};
  EXPECT_EQ((int)sizeof(plain), Utils::MACThenDecrypt(kSecret, out, frame, frame_len));
  EXPECT_EQ(0, memcmp(plain, out, sizeof(plain)));
}

// ---------------------------------------------------------------------------
// The defect. Every frame below carries a MAC that VALIDATES, so reaching
// decrypt() is gated only by the alignment check.
// ---------------------------------------------------------------------------

TEST(MacAlign, ValidMacButNonAlignedCiphertextIsRejected) {
  // 24 bytes of "ciphertext": one whole block plus half of another.
  const int kEncLen = CIPHER_BLOCK_SIZE + CIPHER_BLOCK_SIZE / 2;
  ASSERT_NE(0, kEncLen % CIPHER_BLOCK_SIZE) << "test payload must be non-aligned";

  uint8_t frame[CIPHER_MAC_SIZE + kEncLen];
  for (int i = 0; i < kEncLen; i++) frame[CIPHER_MAC_SIZE + i] = (uint8_t)(0xA0 + i);
  macOver(frame + CIPHER_MAC_SIZE, kEncLen, frame);  // forge a MAC that passes

  uint8_t out[CIPHER_BLOCK_SIZE * 4] = {0};
  EXPECT_EQ(0, Utils::MACThenDecrypt(kSecret, out, frame, sizeof(frame)));
}

TEST(MacAlign, EveryNonAlignedLengthIsRejected) {
  for (int rem = 1; rem < CIPHER_BLOCK_SIZE; rem++) {
    const int enc_len = CIPHER_BLOCK_SIZE + rem;
    uint8_t frame[CIPHER_MAC_SIZE + CIPHER_BLOCK_SIZE * 2];
    for (int i = 0; i < enc_len; i++) frame[CIPHER_MAC_SIZE + i] = (uint8_t)(i ^ rem);
    macOver(frame + CIPHER_MAC_SIZE, enc_len, frame);

    uint8_t out[CIPHER_BLOCK_SIZE * 4] = {0};
    EXPECT_EQ(0, Utils::MACThenDecrypt(kSecret, out, frame, CIPHER_MAC_SIZE + enc_len))
        << "non-aligned ciphertext accepted at remainder " << rem;
  }
}

// The regression test proper: not "does it return 0" but "does it stay inside the
// buffer". decrypt() would write ceil(24/16)*16 = 32 bytes for a 24-byte input, so
// before the fix the 8 bytes after a 24-byte destination were clobbered.
TEST(MacAlign, NonAlignedCiphertextDoesNotWritePastTheDestination) {
  const int kEncLen = CIPHER_BLOCK_SIZE + CIPHER_BLOCK_SIZE / 2;   // 24

  struct Guarded {
    uint8_t dest[kEncLen];                       // what a caller sized for 24 bytes
    uint8_t canary[CIPHER_BLOCK_SIZE];           // the 8+ bytes decrypt() overran into
  } buf;
  memset(buf.dest, 0, sizeof(buf.dest));
  memset(buf.canary, 0x5A, sizeof(buf.canary));

  uint8_t frame[CIPHER_MAC_SIZE + kEncLen];
  for (int i = 0; i < kEncLen; i++) frame[CIPHER_MAC_SIZE + i] = (uint8_t)(0x30 + i);
  macOver(frame + CIPHER_MAC_SIZE, kEncLen, frame);

  EXPECT_EQ(0, Utils::MACThenDecrypt(kSecret, buf.dest, frame, sizeof(frame)));

  for (size_t i = 0; i < sizeof(buf.canary); i++) {
    EXPECT_EQ(0x5A, buf.canary[i]) << "decrypt() wrote past dest at canary byte " << i;
  }
}

// ---------------------------------------------------------------------------
// The pre-existing length guard still holds, and the MAC check still rejects.
// ---------------------------------------------------------------------------

TEST(MacAlign, FrameNoLongerThanTheMacIsRejected) {
  uint8_t frame[CIPHER_MAC_SIZE] = {0};
  uint8_t out[CIPHER_BLOCK_SIZE] = {0};

  EXPECT_EQ(0, Utils::MACThenDecrypt(kSecret, out, frame, 0));
  EXPECT_EQ(0, Utils::MACThenDecrypt(kSecret, out, frame, CIPHER_MAC_SIZE));
}

TEST(MacAlign, AlignedCiphertextWithABadMacIsStillRejected) {
  const uint8_t plain[CIPHER_BLOCK_SIZE] = {
    'n','o','t',' ','t','a','m','p','e','r','e','d',' ','y','e','t'
  };
  uint8_t frame[CIPHER_MAC_SIZE + CIPHER_BLOCK_SIZE * 2];
  const int frame_len = Utils::encryptThenMAC(kSecret, frame, plain, sizeof(plain));

  frame[0] ^= 0xFF;  // break the MAC, leave the length alone

  uint8_t out[CIPHER_BLOCK_SIZE * 2] = {0};
  EXPECT_EQ(0, Utils::MACThenDecrypt(kSecret, out, frame, frame_len));
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
