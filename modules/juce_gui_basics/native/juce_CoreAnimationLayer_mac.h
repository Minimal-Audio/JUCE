// Minimal Audio modification start #48
//
//  juce_CoreAnimationLayer_mac.h
//  Minimal Audio's addition to JUCE: not part of upstream JUCE, and maintained by Minimal Audio.
//
//  A CALayer a peer owns, which CoreAnimation draws off the message thread. On macOS 26 a view that draws in
//  drawRect: hands AppKit a recorded display list every frame; at each commit AppKit walks every list the layer keeps
//  for HDR headroom, then replays it on the message thread. In a sample of a playing Current that walk and replay
//  were about half of the message thread. Drawn through this layer, AppKit records, walks and replays nothing.
//
//  Documented in https://github.com/Minimal-Audio/minimal_core/issues/6713.
//  Opt-in: a peer draws the stock way until ComponentPeer::setUsesCoreAnimationLayer (true) is called.
//

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
