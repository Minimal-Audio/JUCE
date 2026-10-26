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
/*  A peer's pixels, drawn by CoreGraphics into IOSurfaces it owns.

    A layer-backed view that draws in drawRect: hands AppKit a recorded display list
    each frame, and on macOS 26 AppKit walks every list a layer keeps on every commit
    to find its content headroom. Here a frame is drawn straight into pixels and the
    finished surface becomes the layer's contents, so there is no list to record,
    replay or walk.

    - Surfaces: up to three, so the one the window server is showing is never drawn into.
    - Dirty areas: only the areas asked for are drawn; the rest of a surface is copied
      from the newest one.
    - Scale or size change: every surface is dropped and the next frame is drawn whole.
*/
class CoreGraphicsOwnedLayer
{
public:
    void markDirty (Rectangle<float> area)
    {
        pending.add (area);
    }

    /*  Draws the pending areas into a free surface and makes it the layer's contents.
        paint draws one area into a context in the view's flipped coordinates, in points.
    */
    template <typename Paint>
    void update (CALayer* layer, CGSize sizeInPoints, float scale, bool opaque, Paint&& paint)
    {
        const auto width = (size_t) roundToInt ((float) sizeInPoints.width * scale);
        const auto height = (size_t) roundToInt ((float) sizeInPoints.height * scale);

        if (width == 0 || height == 0)
            return;

        const Rectangle<float> bounds { (float) sizeInPoints.width, (float) sizeInPoints.height };

        if (width != surfaceWidth || height != surfaceHeight || ! approximatelyEqual (scale, surfaceScale))
        {
            surfaces.clear();
            newest = -1;
            surfaceWidth = width;
            surfaceHeight = height;
            surfaceScale = scale;
            pending = bounds;
        }

        // Asked to display with nothing pending: the system wants the whole view.
        if (pending.isEmpty())
            pending = bounds;

        pending.clipTo (bounds);

        auto& target = takeFreeSurface (bounds);

        IOSurfaceLock (target.surface.get(), 0, nullptr);

        if (newest >= 0 && &surfaces[(size_t) newest] != &target)
        {
            auto stale = target.stale;
            stale.subtract (pending);
            copyFrom (surfaces[(size_t) newest], target, stale);
        }

        drawInto (target, paint);
        IOSurfaceUnlock (target.surface.get(), 0, nullptr);

        for (auto& s : surfaces)
            if (&s != &target)
                s.stale.add (pending);

        target.stale.clear();
        pending.clear();
        newest = (int) (&target - surfaces.data());

        [CATransaction begin];
        [CATransaction setDisableActions: YES];
        layer.contentsScale = scale;
        layer.contentsGravity = kCAGravityTopLeft;
        layer.opaque = opaque;
        layer.contents = (__bridge id) target.surface.get();
        [CATransaction commit];
    }

    /*  Drops every surface and clears the layer's contents, so drawRect: draws the layer again.
        The next update draws the whole view.
    */
    void release (CALayer* layer)
    {
        surfaces.clear();
        pending.clear();
        newest = -1;
        surfaceWidth = surfaceHeight = 0;
        layer.contents = nil;
    }

private:
    struct SurfaceDeleter { void operator() (IOSurfaceRef s) const { CFRelease (s); } };

    struct Surface
    {
        std::unique_ptr<std::remove_pointer_t<IOSurfaceRef>, SurfaceDeleter> surface;
        RectangleList<float> stale;
    };

    static constexpr size_t maxSurfaces = 3;

    Surface& takeFreeSurface (Rectangle<float> bounds)
    {
        for (size_t i = 0; i < surfaces.size(); ++i)
            if ((int) i != newest && ! IOSurfaceIsInUse (surfaces[i].surface.get()))
                return surfaces[i];

        if (surfaces.size() < maxSurfaces)
        {
            surfaces.reserve (maxSurfaces);
            Surface fresh;
            fresh.surface.reset (createSurface());
            fresh.stale = bounds;
            surfaces.push_back (std::move (fresh));
            return surfaces.back();
        }

        // Every surface is still on screen: draw into the oldest rather than wait.
        return surfaces[(size_t) (newest + 1) % surfaces.size()];
    }

    IOSurfaceRef createSurface() const
    {
        const auto bytesPerRow = IOSurfaceAlignProperty (kIOSurfaceBytesPerRow, surfaceWidth * 4);
        NSDictionary* properties = @{ (id) kIOSurfaceWidth:           @(surfaceWidth),
                                      (id) kIOSurfaceHeight:          @(surfaceHeight),
                                      (id) kIOSurfaceBytesPerElement: @4,
                                      (id) kIOSurfaceBytesPerRow:     @(bytesPerRow),
                                      (id) kIOSurfacePixelFormat:     @((uint32) 'BGRA') };

        auto* surface = IOSurfaceCreate ((CFDictionaryRef) properties);
        detail::ColorSpacePtr space { CGColorSpaceCreateWithName (kCGColorSpaceSRGB) };

        if (auto* propertyList = CGColorSpaceCopyPropertyList (space.get()))
        {
            IOSurfaceSetValue (surface, kIOSurfaceColorSpace, propertyList);
            CFRelease (propertyList);
        }

        return surface;
    }

    Rectangle<int> toPixels (Rectangle<float> area) const
    {
        return (area * surfaceScale).getSmallestIntegerContainer()
                                    .getIntersection ({ (int) surfaceWidth, (int) surfaceHeight });
    }

    void copyFrom (const Surface& source, const Surface& target, const RectangleList<float>& areas) const
    {
        if (areas.isEmpty())
            return;

        IOSurfaceLock (source.surface.get(), kIOSurfaceLockReadOnly, nullptr);

        const auto* from = static_cast<const uint8*> (IOSurfaceGetBaseAddress (source.surface.get()));
        auto* to = static_cast<uint8*> (IOSurfaceGetBaseAddress (target.surface.get()));
        const auto fromStride = IOSurfaceGetBytesPerRow (source.surface.get());
        const auto toStride = IOSurfaceGetBytesPerRow (target.surface.get());

        for (const auto& area : areas)
        {
            const auto px = toPixels (area);

            for (auto y = px.getY(); y < px.getBottom(); ++y)
                memcpy (to + (size_t) y * toStride + (size_t) px.getX() * 4,
                        from + (size_t) y * fromStride + (size_t) px.getX() * 4,
                        (size_t) px.getWidth() * 4);
        }

        IOSurfaceUnlock (source.surface.get(), kIOSurfaceLockReadOnly, nullptr);
    }

    template <typename Paint>
    void drawInto (const Surface& target, Paint&& paint) const
    {
        // JUCE's own image layout (premultiplied BGRA) even when opaque: any other layout makes CoreGraphics
        // permute channels on every image it blends in.
        detail::ColorSpacePtr space { CGColorSpaceCreateWithName (kCGColorSpaceSRGB) };
        const auto alpha = kCGImageAlphaPremultipliedFirst;
        detail::ContextPtr context { CGBitmapContextCreate (IOSurfaceGetBaseAddress (target.surface.get()),
                                                            surfaceWidth,
                                                            surfaceHeight,
                                                            8,
                                                            IOSurfaceGetBytesPerRow (target.surface.get()),
                                                            space.get(),
                                                            (uint32_t) alpha | (uint32_t) kCGBitmapByteOrder32Little) };

        // The view's flipped coordinates, in points.
        CGContextTranslateCTM (context.get(), 0, (CGFloat) surfaceHeight);
        CGContextScaleCTM (context.get(), surfaceScale, -surfaceScale);

        for (const auto& area : pending)
        {
            const auto rect = toPixels (area).toFloat() / surfaceScale;
            const auto nsRect = NSMakeRect (rect.getX(), rect.getY(), rect.getWidth(), rect.getHeight());

            CGContextSaveGState (context.get());
            CGContextClipToRect (context.get(), nsRect);
            paint (context.get(), nsRect);
            CGContextRestoreGState (context.get());
        }
    }

    std::vector<Surface> surfaces;
    RectangleList<float> pending;
    int newest = -1;
    size_t surfaceWidth = 0, surfaceHeight = 0;
    float surfaceScale = 0.0f;
};

} // namespace juce
