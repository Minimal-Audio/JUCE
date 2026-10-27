// Minimal Audio modification start #48
/*
  ==============================================================================

   This file is part of the JUCE framework.
   Copyright (c) Raw Material Software Limited

   JUCE is an open source framework subject to commercial or open source
   licensing.

   By downloading, installing, or using the JUCE framework, or combining the
   JUCE framework with any other source code, object code, content or any other
   copyrightable work, you agree to the terms of the JUCE End User Licence
   Agreement, and all incorporated terms including the JUCE Privacy Policy and
   the JUCE Website Terms of Service, as applicable, which will bind you. If you
   do not agree to the terms of these agreements, we will not license the JUCE
   framework to you, and you must discontinue the installation or download
   process and cease use of the JUCE framework.

   JUCE End User Licence Agreement: https://juce.com/legal/juce-8-licence/
   JUCE Privacy Policy: https://juce.com/juce-privacy-policy
   JUCE Website Terms of Service: https://juce.com/juce-website-terms-of-service/

   Or:

   You may also use this code under the terms of the AGPLv3:
   https://www.gnu.org/licenses/agpl-3.0.en.html

   THE JUCE FRAMEWORK IS PROVIDED "AS IS" WITHOUT ANY WARRANTY, AND ALL
   WARRANTIES, WHETHER EXPRESSED OR IMPLIED, INCLUDING WARRANTY OF
   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, ARE DISCLAIMED.

  ==============================================================================
*/

namespace juce
{

//==============================================================================
/** A copy of these pixels as a CGImage, in the layout CoreGraphics reads for their
    format: JUCE's own copy, for pixels that have no CoreGraphics context of their own.
*/
inline CFUniquePtr<CGImageRef> copyToCGImage (const Image::BitmapData& pixels, CGColorSpaceRef colourSpace)
{
    const auto bitmapInfo = pixels.pixelFormat == Image::ARGB
                          ? (CGBitmapInfo) ((uint32_t) kCGImageAlphaPremultipliedFirst | (uint32_t) kCGBitmapByteOrder32Host)
                          : (CGBitmapInfo) kCGImageByteOrderDefault;

    CFUniquePtr<CFDataRef> data (CFDataCreate (nullptr, (const UInt8*) pixels.data, (CFIndex) pixels.size));
    detail::DataProviderPtr provider { CGDataProviderCreateWithCFData (data.get()) };

    return CFUniquePtr<CGImageRef> { CGImageCreate ((size_t) pixels.width, (size_t) pixels.height, 8,
                                                    (size_t) pixels.pixelStride * 8, (size_t) pixels.lineStride,
                                                    colourSpace, bitmapInfo, provider.get(), nullptr, true,
                                                    kCGRenderingIntentDefault) };
}

/** A context's pixels as a CGImage that shares them until either is written, so a picture
    is copied only when it changes while still in use, rather than each time it changes.
    Null when colourSpace is not the context's, which needs a copy that converts.
*/
inline CFUniquePtr<CGImageRef> getSnapshotOfContext (CGContextRef context, CGColorSpaceRef colourSpace)
{
    if (! CFEqual (colourSpace, CGBitmapContextGetColorSpace (context)))
        return {};

    return CFUniquePtr<CGImageRef> { CGBitmapContextCreateImage (context) };
}

//==============================================================================
/** One CGImage of a software image's pixels, kept until they change, so a picture drawn
    by CoreGraphics every frame is copied once rather than on every draw. Owned by the
    image's pixel data, and forgotten when that data says its pixels changed.
*/
class SoftwareImageCGImage final : private ImagePixelData::Listener
{
public:
    explicit SoftwareImageCGImage (ImagePixelData& pixelDataIn) : pixelData (pixelDataIn) { pixelData.listeners.add (this); }
    ~SoftwareImageCGImage() override { pixelData.listeners.remove (this); }

    /** The pixel data's native extensions: CoreGraphics draws this kept image. */
    ImagePixelData::NativeExtensions getNativeExtensions()
    {
        struct Extensions
        {
            CGContextRef getCGContext() const { return {}; }
            CFUniquePtr<CGImageRef> getCGImage (CGColorSpaceRef colourSpace) const { return owner->get (colourSpace); }
            Point<int> getTopLeft() const { return {}; }

            ImagePixelData::Ptr keepAlive;
            SoftwareImageCGImage* owner;
        };

        return ImagePixelData::NativeExtensions { Extensions { &pixelData, this } };
    }

    CFUniquePtr<CGImageRef> get (CGColorSpaceRef colourSpace)
    {
        if (image == nullptr)
            image = copyToCGImage (Image::BitmapData (Image (&pixelData), Image::BitmapData::readOnly), colourSpace);

        return CFUniquePtr<CGImageRef> { CGImageRetain (image.get()) };
    }

private:
    void imageDataChanged (ImagePixelData*) override { image.reset(); }
    void imageDataBeingDeleted (ImagePixelData*) override {}

    ImagePixelData& pixelData;
    CFUniquePtr<CGImageRef> image;
};

} // namespace juce
// Minimal Audio modification end #48
