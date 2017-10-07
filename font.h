#ifndef MT_FONT_H
#define MT_FONT_H

#include <X11/Xft/Xft.h>
#include <memory>
#include <string>

// A loaded font including variants (bold/italic).
// Errors are not recoverable: exit() is called if font loading fails.
class FontSet {
  using ScopedPattern = std::unique_ptr<FcPattern, decltype(&FcPatternDestroy)>;
  using ScopedFontSet = std::unique_ptr<FcFontSet, decltype(&FcFontSetDestroy)>;
  struct FontDeleter {
    void operator()(XftFont* font) { XftFontClose(display, font); }
    Display* display;
  };
  using ScopedFont = std::unique_ptr<XftFont, FontDeleter>;

 public:
  // Syntax: https://freedesktop.org/software/fontconfig/fontconfig-user.html
  // Initial size is 12px if neither 'size' nor 'pixelsize' are given.
  FontSet(const std::string& fontconfig_spec, Display* display, int screen);

  // Reloads the font at a different size. Current variants are invalidated.
  void SetPixelSize(double);

  struct Metrics {
    Metrics(XftFont* font, Display* display);
    double size;
    int width, height, ascent;
  };
  const Metrics& metrics() const { return *metrics_; }

  class Variant {
   public:
    Variant(FcPattern* pattern, Display* display, int screen);
    XftFont* font() const { return font_.get(); }

   public: // XXX
    ScopedPattern pattern_;
   private:
    ScopedFont font_;
   public: // XXX
    ScopedFontSet set;
  };

 private:
  void Load();

  ScopedPattern pattern_;
  Display* display_;
  int screen_;
  std::unique_ptr<Metrics> metrics_;
 public: // XXX
  std::unique_ptr<Variant> plain_, bold_, italic_, bold_italic_;
};

#endif
