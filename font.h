#ifndef MT_FONT_H
#define MT_FONT_H

#include <X11/Xft/Xft.h>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

// A loaded font including variants (bold/italic).
// Errors are not recoverable: exit() is called if font loading fails.
class MTFont {
 public:
  // Syntax: https://freedesktop.org/software/fontconfig/fontconfig-user.html
  // Initial size is 12px if neither 'size' nor 'pixelsize' are given.
  MTFont(const std::string& fontconfig_spec, Display* display, int screen);
  ~MTFont();

  // Reloads the font at a different size. Glyphs and Metrics are invalidated.
  void SetPixelSize(double);

  // Style variants are a bitfield: BOLD | ITALIC is also valid.
  enum Style : char { PLAIN = 0, BOLD = 1, ITALIC = 2 };
  struct Glyph {
    FT_UInt index;
    XftFont* font;
  };
  Glyph FindGlyph(uint32_t rune, Style style);

  // Metrics describe the dimensions of the font's glyph.
  struct Metrics {
    double pixel_size;
    int width, height, ascent;
  };
  const Metrics& metrics() const { return metrics_; }

 private:
  void Load();

  // The font pattern is preserved so that we can load different font sizes.
  struct PatternDeleter {
    void operator()(FcPattern* p) { FcPatternDestroy(p); }
  };
  using ScopedPattern = std::unique_ptr<FcPattern, PatternDeleter>;
  ScopedPattern pattern_;

  Display* display_;
  int screen_;
  Metrics metrics_;

  // We have a Variant for each Style, with their own resources, cache, etc.
  class Variant;
  std::unique_ptr<Variant> variants_[(BOLD | ITALIC) + 1];
};

#endif
