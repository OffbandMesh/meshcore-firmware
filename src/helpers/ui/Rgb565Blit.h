#pragma once

#include <stdint.h>
#include <stddef.h>

// #749: the geometry behind the native-resolution colour splash path.
//
// WHY THIS IS ITS OWN HEADER
//   NV3001BDisplay is the only caller, but it is welded to SPI and board pins and
//   cannot be constructed off-target, so none of this arithmetic would ever be
//   exercised by a test if it lived in the driver. The clipping is the part that
//   breaks: unlike a solid fill (fillPhysicalRect), a clipped IMAGE has to advance
//   its source pointer to match the clip and keep using the ORIGINAL image width as
//   the row stride. Getting that wrong shears every row after the first, and looks
//   like a "corrupt asset" rather than a blit bug. Tests: test/test_rgb565_blit.
//   Same extract-for-testability rationale as helpers/BleFrameSizing.h (#712).
//
// COORDINATES
//   Everything here is PHYSICAL panel pixels. That is the whole point of the path:
//   the 128x64 logical UI canvas is stretched 1.72x horizontally and 2.0x vertically
//   on this panel, so an image routed through scaleX()/scaleY() comes out
//   non-uniformly distorted. #747's back buffer is already indexed in physical
//   pixels, so writing into it directly is what "native resolution" means here.

namespace rgb565 {

// Copy a src_w x src_h RGB565 image into a dst_w x dst_h framebuffer at (x, y),
// clipped to the destination.
//
// Source pixels are ordinary host-order RGB565 (the same encoding as ColorVal), so
// a generated asset is driver-agnostic. They are stored BYTE-SWAPPED, because
// #747's buffer is handed to the panel as a raw writeBytes() and the panel wants
// RGB565 most-significant-byte first -- the identical convention fillPhysicalRect()
// applies to its fill colour.
//
// The visible region of a placement: where it lands in the destination (dx, dy),
// which source pixel is the new top-left (sx, sy), and how much survives (w, h).
struct Clip {
  int dx, dy;     // destination origin, clamped into the buffer
  int sx, sy;     // first source column/row still visible
  int w, h;       // visible extent
  bool visible;   // false => nothing to draw; the other fields are meaningless
};

// Clip a src_w x src_h image placed at (x, y) against a dst_w x dst_h destination.
//
// Exposed rather than kept private because the driver has TWO consumers: the
// buffered path (blitSwapped, below) and the unbuffered fallback that talks to the
// panel directly when #747's back-buffer allocation failed. Both need the identical
// result, and two hand-rolled copies of this arithmetic is how the rare fallback
// path ends up shearing rows the common path renders correctly.
inline Clip clip(int dst_w, int dst_h, int x, int y, int src_w, int src_h) {
  Clip c{x, y, 0, 0, src_w, src_h, false};
  if (dst_w <= 0 || dst_h <= 0 || src_w <= 0 || src_h <= 0) return c;

  if (c.dx < 0) { c.sx = -c.dx; c.w += c.dx; c.dx = 0; }
  if (c.dy < 0) { c.sy = -c.dy; c.h += c.dy; c.dy = 0; }
  if (c.dx + c.w > dst_w) c.w = dst_w - c.dx;
  if (c.dy + c.h > dst_h) c.h = dst_h - c.dy;

  c.visible = (c.w > 0 && c.h > 0);
  return c;
}

// Out-of-range placements, null pointers and non-positive dimensions write nothing.
inline void blitSwapped(uint16_t* dst, int dst_w, int dst_h,
                        int x, int y,
                        const uint16_t* src, int src_w, int src_h) {
  if (!dst || !src) return;

  const Clip c = clip(dst_w, dst_h, x, y, src_w, src_h);
  if (!c.visible) return;

  // The row stride of the SOURCE never changes, however much we clip. Clipping
  // narrows what we copy, not how the image is laid out.
  const int stride = src_w;

  for (int row = 0; row < c.h; row++) {
    const uint16_t* s = src + (size_t)(c.sy + row) * stride + c.sx;
    uint16_t* d = dst + (size_t)(c.dy + row) * dst_w + c.dx;
    for (int col = 0; col < c.w; col++) {
      const uint16_t v = s[col];
      d[col] = (uint16_t)((v >> 8) | (v << 8));
    }
  }
}

}  // namespace rgb565
