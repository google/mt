#include "font.h"

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

}  // namespace

FontSet::FontSet(const std::string& spec, Display* display, int screen)
    : pattern_(spec[0] == '-' ? XftXlfdParse(spec.c_str(), 0, 0)
                              : FcNameParse((FcChar8*)spec.c_str()),
               FcPatternDestroy),
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

void FontSet::SetPixelSize(double new_size) {
  ScopedFontAttribute<double> size(pattern_.get(), FC_PIXEL_SIZE, new_size);
  Load();
}

void FontSet::Load() {
  auto load_variant = [this] {
    return std::unique_ptr<Variant>(
        new Variant(pattern_.get(), display_, screen_));
  };
  plain_ = load_variant();
  {
    ScopedFontAttribute<int> bold(pattern_.get(), FC_WEIGHT, FC_WEIGHT_BOLD);
    bold_ = load_variant();
  }
  {
    ScopedFontAttribute<int> italic(pattern_.get(), FC_SLANT, FC_SLANT_ITALIC);
    italic_ = load_variant();
  }
  {
    ScopedFontAttribute<int> bold(pattern_.get(), FC_WEIGHT, FC_WEIGHT_BOLD);
    ScopedFontAttribute<int> italic(pattern_.get(), FC_SLANT, FC_SLANT_ITALIC);
    bold_italic_ = load_variant();
  }
  // We assume metrics are the same for all variants.
  metrics_.reset(new Metrics(plain_->font(), display_));
}

FontSet::Variant::Variant(FcPattern* pattern, Display* display, int screen)
    : pattern_(FcPatternDuplicate(pattern), FcPatternDestroy),
      font_(nullptr, {display}),
      set(nullptr, &FcFontSetDestroy) {
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
  font_.reset(XftFontOpenPattern(display, match));
  if (font_ == nullptr) fail();
}

static int DivCeiling(int n, int d) { return (n + d - 1) / d; }

FontSet::Metrics::Metrics(XftFont* font, Display* display) {
  FcPatternGetDouble(font->pattern, FC_PIXEL_SIZE, 0, &size);
  height = font->ascent + font->descent;
  ascent = font->ascent;
  // We take font width as the average of these characters.
  static char ascii_printable[] =
      " !\"#$%&'()*+,-./0123456789:;<=>?"
      "@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_"
      "`abcdefghijklmnopqrstuvwxyz{|}~";
  XGlyphInfo extents;
  XftTextExtentsUtf8(display, font, (const FcChar8*)ascii_printable,
                     strlen(ascii_printable), &extents);
  width = DivCeiling(extents.xOff, strlen(ascii_printable));
}

