#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// ============================================================================
// WX embedded typefaces (Poppins, SIL OFL 1.1 - see assets/Poppins-OFL.txt)
//
// Every piece of UI text goes through here. Nothing may reference a font by
// system name: "Source Sans 3" is not installed on customer machines, and the
// platform default monospaced font is Courier New on Windows and Menlo on
// macOS, so the UI rendered differently on every machine.
//
// Lifetime: the typefaces live in a juce::SharedResourcePointer, created on
// first use and destroyed when the last holder goes away. Any component that
// draws text keeps a WXFonts::KeepAlive member so the typefaces are not
// rebuilt on every paint - and, unlike a file-scope static, they are gone
// before JUCE shuts down when the plug-in binary is unloaded.
// ============================================================================

namespace WXFonts
{
    struct Holder
    {
        Holder();

        juce::Typeface::Ptr regular;
        juce::Typeface::Ptr medium;
        juce::Typeface::Ptr bold;
    };

    using KeepAlive = juce::SharedResourcePointer<Holder>;

    /** Poppins Regular, or Poppins Bold when styleFlags contains Font::bold. */
    juce::Font get (float height, int styleFlags = juce::Font::plain);

    /** Poppins Medium - for readouts and labels that want more weight than
        Regular without the density of Bold. */
    juce::Font medium (float height);
}

// ============================================================================
// Shared film-grain overlay.
//
// One 128x128 noise tile at full alpha, tinted down per call via the fill
// opacity. Previously each panel built its own copy in a file-scope static,
// which meant three identical images that were destroyed after JUCE had already
// shut down when the plug-in binary was unloaded.
// ============================================================================

namespace WXGrain
{
    struct Holder
    {
        Holder();
        juce::Image image;
    };

    using KeepAlive = juce::SharedResourcePointer<Holder>;

    void draw (juce::Graphics& g, juce::Rectangle<int> bounds, float opacity);
}
