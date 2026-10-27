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

#import <QuartzCore/CATransaction.h>

namespace juce
{

//==============================================================================
/*  A layer a peer owns, which CoreAnimation draws off the message thread.

    On macOS 26 a view that draws in drawRect: hands AppKit a display list every frame:
    AppKit walks every list the layer keeps for HDR headroom at each commit, then replays
    the list on the message thread. Drawn through this plain sublayer instead, with
    drawsAsynchronously on, the frame is recorded by CoreAnimation and rasterised on its
    own queue, and the view itself draws nothing.

    - Unused: rects go to the view, which draws them in drawRect: as JUCE always has.
    - Used: rects go to the layer, which draws them through the callback in the view's
      flipped coordinates. The view's updateLayer keeps it the view's size and scale.
    - A view whose own layer is a CAMetalLayer keeps drawing as it does.
*/
class CoreAnimationLayer
{
public:
    using Draw = std::function<void (CGContextRef, CGRect)>;

    explicit CoreAnimationLayer (Draw drawIn) : draw (std::move (drawIn)) {}

    ~CoreAnimationLayer() { setUsed (false, nil); }

    /** Starts or stops drawing view's frames through the layer. Either way the view is redrawn whole. */
    void setUsed (bool shouldUse, NSView* view)
    {
        if (shouldUse == isUsed())
            return;

        if (layer != nullptr)
        {
            layer.get().delegate = nil;
            [layer.get() removeFromSuperlayer];
            layer.reset();
            delegate.reset();
        }

        if (shouldUse && view.layer != nil && ! [view.layer isKindOfClass: [CAMetalLayer class]])
        {
            delegate.reset (Delegate::construct (this));
            layer.reset ([[CALayer alloc] init]);
            layer.get().delegate = delegate.get();
            layer.get().drawsAsynchronously = YES;
            layer.get().needsDisplayOnBoundsChange = YES;
            layer.get().anchorPoint = CGPointZero;
            view.layer.contents = nil;
            [view.layer addSublayer: layer.get()];
            update (view);
        }

        [view setNeedsDisplay: YES];
    }

    bool isUsed() const { return layer != nullptr; }

    /** Asks for an area of view to be redrawn: by the layer when it is used, else by the view. */
    void setNeedsDisplayInRect (NSView* view, CGRect area)
    {
        if (layer == nullptr)
        {
            [view setNeedsDisplayInRect: area];
            return;
        }

        update (view);
        [layer.get() setNeedsDisplayInRect: area];
    }

    /** Keeps the layer view's size and scale, and redraws it whole when either changes. */
    void update (NSView* view)
    {
        auto* l = layer.get();

        if (l == nil)
            return;

        const auto scale = [view window] != nil ? [view window].backingScaleFactor : 1.0;

        if (CGRectEqualToRect (l.frame, view.bounds) && approximatelyEqual (l.contentsScale, scale))
            return;

        [CATransaction begin];
        [CATransaction setDisableActions: YES];
        l.frame = view.bounds;
        l.contentsScale = scale;
        l.opaque = view.isOpaque;
        [CATransaction commit];
        [l setNeedsDisplay];
    }

private:
    // Draws through the callback, in the view's flipped coordinates as drawRect: has them, and never animates.
    struct Delegate final : public ObjCClass<NSObject<CALayerDelegate>>
    {
        static NSObject<CALayerDelegate>* construct (CoreAnimationLayer* owner)
        {
            static Delegate cls;
            auto* result = cls.createInstance();
            object_setInstanceVariable (result, "owner", owner);
            return result;
        }

        Delegate() : ObjCClass ("JUCECoreAnimationLayerDelegate_")
        {
            addIvar<CoreAnimationLayer*> ("owner");

            addMethod (@selector (drawLayer:inContext:), [] (id self, SEL, CALayer* l, CGContextRef context)
            {
                if (auto* owner = getIvar<CoreAnimationLayer*> (self, "owner"))
                {
                    if (! l.contentsAreFlipped)
                    {
                        CGContextTranslateCTM (context, 0, l.bounds.size.height);
                        CGContextScaleCTM (context, 1, -1);
                    }

                    owner->draw (context, CGContextGetClipBoundingBox (context));
                }
            });

            addMethod (@selector (actionForLayer:forKey:), [] (id, SEL, CALayer*, NSString*) -> id { return [NSNull null]; });

            addProtocol (@protocol (CALayerDelegate));
            registerClass();
        }
    };

    Draw draw;
    NSUniquePtr<CALayer> layer;
    NSUniquePtr<NSObject<CALayerDelegate>> delegate;
};

} // namespace juce
// Minimal Audio modification end #48
