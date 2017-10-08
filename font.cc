#include "font.h"

#include <unordered_set>
#include <vector>

namespace {

// Helper to modify at attribute (size, weight, slant) of an FcPattern.
// The old value (or lack of value) is restored on destruction.
template <typename T>
class ScopedFontAttribute {
 public:
  ScopedFontAttribute(FcPattern* pattern, const char* attribute, T value)
      : pattern_(pattern), attribute_(attribute), had_value_(Get(&old_value_)) {
    if (had_value_) FcPatternDel(pattern_, attribute_);
    Set(value);
  }

  ~ScopedFontAttribute() {
    FcPatternDel(pattern_, attribute_);
    if (had_value_) Set(old_value_);
  }

 private:
  bool Get(T* value);
  void Set(T value);

  FcPattern* pattern_;
  const char* attribute_;
  bool had_value_;
  T old_value_;
};

template <>
bool ScopedFontAttribute<int>::Get(int* value) {
  return FcPatternGetInteger(pattern_, attribute_, 0, value) == FcResultMatch;
}
template <>
bool ScopedFontAttribute<double>::Get(double* value) {
  return FcPatternGetDouble(pattern_, attribute_, 0, value) == FcResultMatch;
}
template <>
void ScopedFontAttribute<int>::Set(int value) {
  FcPatternAddInteger(pattern_, attribute_, value);
}
template <>
void ScopedFontAttribute<double>::Set(double value) {
  FcPatternAddDouble(pattern_, attribute_, value);
}

struct FontSetDeleter {
  void operator()(FcFontSet* p) { FcFontSetDestroy(p); }
};
using ScopedFontSet = std::unique_ptr<FcFontSet, FontSetDeleter>;
struct CharSetDeleter {
  void operator()(FcCharSet* p) { FcCharSetDestroy(p); }
};
using ScopedCharSet = std::unique_ptr<FcCharSet, CharSetDeleter>;
struct FontDeleter {
  void operator()(XftFont* font) { XftFontClose(display, font); }
  Display* display;
};
using ScopedFont = std::unique_ptr<XftFont, FontDeleter>;

int DivCeiling(int n, int d) { return (n + d - 1) / d; }

}  // namespace

// Most of the hard work is done by the Variant, which handles one style.
class MTFont::Variant {
 public:
  Variant(FcPattern* pattern, Display* display, int screen)
      : pattern_(FcPatternDuplicate(pattern)), display_(display) {
    auto fail = [pattern] {
      fprintf(stderr, "mt: failed to load font %s\n", FcNameUnparse(pattern));
      exit(1);
    };
    if (pattern_ == nullptr) fail();
    FcConfigSubstitute(NULL, pattern_.get(), FcMatchPattern);
    XftDefaultSubstitute(display, screen, pattern_.get());
    FcResult unused;
    FcPattern* match = FcFontMatch(NULL, pattern_.get(), &unused);
    if (match == nullptr) fail();
    fonts_.emplace_back(XftFontOpenPattern(display, match),
                        FontDeleter{display});
    if (fonts_.back() == nullptr) fail();
  }

  Glyph FindGlyph(uint32_t rune) {
    // Lookup with default fonts and cached fallback fonts.
    for (const auto& font : fonts_) {
      if (FT_UInt index = XftCharIndex(display_, font.get(), rune)) {
        return {index, font.get()};
      }
    }

    // No glyph in cached fonts, check for a cached negative result.
    if (glyphless_runes_.count(rune)) return {0, fonts_.front().get()};

    // Missed the cache, so get the font from fontconfig.
    FcResult fcres;
    if (!set_)  // Initialized lazily for faster startup with no emoji.
      set_.reset(FcFontSort(0, pattern_.get(), 1, 0, &fcres));
    ScopedCharSet charset(FcCharSetCreate());
    FcCharSetAddChar(charset.get(), rune);
    ScopedPattern pattern(FcPatternDuplicate(pattern_.get()));
    FcPatternAddCharSet(pattern.get(), FC_CHARSET, charset.get());
    FcPatternAddBool(pattern.get(), FC_SCALABLE, 1);
    FcConfigSubstitute(0, pattern.get(), FcMatchPattern);
    FcDefaultSubstitute(pattern.get());
    FcFontSet* sets[] = {set_.get()};
    FcPattern* match = FcFontSetMatch(0, sets, 1, pattern.get(), &fcres);
    ScopedFont font(XftFontOpenPattern(display_, match), FontDeleter{display_});
    FT_UInt index = XftCharIndex(display_, font.get(), rune);

    // Cache and return.
    if (index == 0 /* unknown */) {
      glyphless_runes_.insert(rune);
      return {0, fonts_.front().get()};
    }
    fonts_.push_back(std::move(font));
    return {index, fonts_.back().get()};
  }

  Metrics Measure() {
    Metrics metrics;
    XftFont* font = fonts_.front().get();
    FcPatternGetDouble(font->pattern, FC_PIXEL_SIZE, 0, &metrics.pixel_size);
    metrics.height = font->ascent + font->descent;
    metrics.ascent = font->ascent;
    // We take font width as the average of these characters.
    static char ascii_printable[] =
        " !\"#$%&'()*+,-./0123456789:;<=>?"
        "@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_"
        "`abcdefghijklmnopqrstuvwxyz{|}~";
    XGlyphInfo extents;
    XftTextExtentsUtf8(display_, font, (const FcChar8*)ascii_printable,
                       strlen(ascii_printable), &extents);
    metrics.width = DivCeiling(extents.xOff, strlen(ascii_printable));
    return metrics;
  }

 private:
  std::vector<ScopedFont> fonts_;  // First is primary, rest are fallbacks.
  std::unordered_set<uint32_t> glyphless_runes_;
  ScopedPattern pattern_;
  Display* display_;
  ScopedFontSet set_;
};

// MTFont is responsible for setting up the base pattern, and loading variants.
MTFont::MTFont(const std::string& spec, Display* display, int screen)
    : pattern_(spec.c_str()[0] == '-' ? XftXlfdParse(spec.c_str(), 0, 0)
                                      : FcNameParse((FcChar8*)spec.c_str())),
      display_(display),
      screen_(screen) {
  if (pattern_ == nullptr) {
    fprintf(stderr, "mt: can't open font %s\n", spec.c_str());
    exit(1);
  }

  // If a size param exists, keep it for the initial load. We'll clear it later.
  // Otherwise, use a default pixel size of 12.
  double ignored;
  if (!FcPatternGetDouble(pattern_.get(), FC_PIXEL_SIZE, 0, &ignored) &&
      !FcPatternGetDouble(pattern_.get(), FC_SIZE, 0, &ignored)) {
    FcPatternAddDouble(pattern_.get(), FC_PIXEL_SIZE, 12);
  }
  Load();
  FcPatternDel(pattern_.get(), FC_PIXEL_SIZE);
  FcPatternDel(pattern_.get(), FC_SIZE);
}

MTFont::~MTFont() = default;

void MTFont::SetPixelSize(double new_size) {
  ScopedFontAttribute<double> size(pattern_.get(), FC_PIXEL_SIZE, new_size);
  Load();
}

void MTFont::Load() {
  auto load_variant = [this] {
    return std::unique_ptr<Variant>(
        new Variant(pattern_.get(), display_, screen_));
  };
  variants_[PLAIN] = load_variant();
  {
    ScopedFontAttribute<int> bold(pattern_.get(), FC_WEIGHT, FC_WEIGHT_BOLD);
    variants_[BOLD] = load_variant();
  }
  {
    ScopedFontAttribute<int> italic(pattern_.get(), FC_SLANT, FC_SLANT_ITALIC);
    variants_[ITALIC] = load_variant();
  }
  {
    ScopedFontAttribute<int> bold(pattern_.get(), FC_WEIGHT, FC_WEIGHT_BOLD);
    ScopedFontAttribute<int> italic(pattern_.get(), FC_SLANT, FC_SLANT_ITALIC);
    variants_[BOLD | ITALIC] = load_variant();
  }
  // We assume metrics are the same for all variants.
  metrics_ = variants_[PLAIN]->Measure();
}

MTFont::Glyph MTFont::FindGlyph(uint32_t rune, Style style) {
  return variants_[style]->FindGlyph(rune);
}
