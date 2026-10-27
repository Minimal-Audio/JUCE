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
/*  Covering rectangles: a few boxes for a repaint that costs something per rectangle
    as well as per pixel, rather than every rectangle of a region, or one box round
    rectangles far apart. Pure functions of their arguments.
*/

/** The area the bounding box of a and b covers that neither does. */
template <typename ValueType>
ValueType getAreaAddedByJoining (Rectangle<ValueType> a, Rectangle<ValueType> b)
{
    const auto areaOf = [] (Rectangle<ValueType> r) { return r.getWidth() * r.getHeight(); };
    return areaOf (a.getUnion (b)) - areaOf (a) - areaOf (b);
}

/** The next pair of rects to join, if any must be: a pair that overlaps, since a repaint
    draws an overlap twice; else, with too many left or a pair whose box adds nothing,
    the pair whose box adds least.
*/
template <typename ValueType>
std::optional<std::pair<int, int>> findRectanglesToJoin (const Array<Rectangle<ValueType>>& rects, int maxNumRectangles)
{
    std::optional<std::pair<int, int>> cheapest;
    auto leastAdded = std::numeric_limits<ValueType>::max();

    for (int a = 0; a < rects.size() - 1; ++a)
    {
        for (int b = a + 1; b < rects.size(); ++b)
        {
            if (rects.getReference (a).intersects (rects.getReference (b)))
                return std::pair { a, b };

            const auto added = getAreaAddedByJoining (rects.getReference (a), rects.getReference (b));

            if (added < leastAdded)
            {
                leastAdded = added;
                cheapest = std::pair { a, b };
            }
        }
    }

    if (rects.size() > maxNumRectangles || leastAdded <= 0)
        return cheapest;

    return {};
}

/** No more than maxNumRectangles rectangles, none overlapping, that together cover region. */
template <typename ValueType>
Array<Rectangle<ValueType>> getCoveringRectangles (const RectangleList<ValueType>& region, int maxNumRectangles)
{
    jassert (maxNumRectangles > 0);

    Array<Rectangle<ValueType>> rects;

    for (const auto& r : region)
        rects.add (r);

    while (const auto pair = findRectanglesToJoin (rects, maxNumRectangles))
    {
        rects.getReference (pair->first) = rects.getReference (pair->first).getUnion (rects.getReference (pair->second));
        rects.remove (pair->second);
    }

    return rects;
}

} // namespace juce
// Minimal Audio modification end #48
