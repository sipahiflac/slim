#include "Fonts.h"
#include "BinaryData.h"

namespace WXFonts
{

Holder::Holder()
{
    regular = juce::Typeface::createSystemTypefaceFor (BinaryData::PoppinsRegular_ttf,
                                                       (size_t) BinaryData::PoppinsRegular_ttfSize);
    medium  = juce::Typeface::createSystemTypefaceFor (BinaryData::PoppinsMedium_ttf,
                                                       (size_t) BinaryData::PoppinsMedium_ttfSize);
    bold    = juce::Typeface::createSystemTypefaceFor (BinaryData::PoppinsBold_ttf,
                                                       (size_t) BinaryData::PoppinsBold_ttfSize);
}

static juce::Font make (const juce::Typeface::Ptr& face, float height, int styleFlags)
{
    // Fall back to the platform default rather than drawing nothing if a
    // typeface somehow failed to load from the embedded data.
    if (face == nullptr)
        return juce::Font (juce::Font::getDefaultSansSerifFontName(), height, styleFlags);

    juce::Font f (face);
    f.setHeight (height);

    if ((styleFlags & juce::Font::italic) != 0)     f.setItalic (true);
    if ((styleFlags & juce::Font::underlined) != 0) f.setUnderline (true);

    return f;
}

juce::Font get (float height, int styleFlags)
{
    KeepAlive fonts;
    const bool wantBold = (styleFlags & juce::Font::bold) != 0;
    return make (wantBold ? fonts->bold : fonts->regular, height, styleFlags);
}

juce::Font medium (float height)
{
    KeepAlive fonts;
    return make (fonts->medium, height, juce::Font::plain);
}

} // namespace WXFonts

namespace WXGrain
{

Holder::Holder()
    : image (juce::Image::SingleChannel, 128, 128, true)
{
    juce::Image::BitmapData pixels (image, juce::Image::BitmapData::writeOnly);
    juce::Random r (0x57584A);

    for (int y = 0; y < pixels.height; ++y)
        for (int x = 0; x < pixels.width; ++x)
            *pixels.getPixelPointer (x, y) = (juce::uint8) r.nextInt (256);
}

void draw (juce::Graphics& g, juce::Rectangle<int> bounds, float opacity)
{
    KeepAlive grain;
    g.setTiledImageFill (grain->image, 0, 0, opacity);
    g.fillRect (bounds);
}

} // namespace WXGrain
