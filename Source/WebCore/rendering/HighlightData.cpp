/*
 * Copyright (C) 2014 Igalia S.L.
 * Copyright (C) 2015-2017 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDER “AS IS” AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
 * TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "config.h"
#include "HighlightData.h"

#include "Document.h"
#include "FrameSelection.h"
#include "HighlightRangeGroup.h"
#include "Logging.h"
#include "Position.h"
#include "Range.h"
#include "RenderLayer.h"
#include "RenderMultiColumnSpannerPlaceholder.h"
#include "RenderObject.h"
#include "RenderView.h"
#include "VisibleSelection.h"
#include <wtf/text/TextStream.h>

namespace WebCore {



RenderRangeIterator::RenderRangeIterator(RenderObject* start)
    : m_current(start)
{
    checkForSpanner();
}

RenderObject* RenderRangeIterator::current() const
{
    return m_current;
}

RenderObject* RenderRangeIterator::next()
{
    RenderObject* currentSpan = m_spannerStack.isEmpty() ? nullptr : m_spannerStack.last()->spanner();
    m_current = m_current->nextInPreOrder(currentSpan);
    checkForSpanner();
    if (!m_current && currentSpan) {
        RenderObject* placeholder = m_spannerStack.last();
        m_spannerStack.removeLast();
        m_current = placeholder->nextInPreOrder();
        checkForSpanner();
    }
    return m_current;
}

void RenderRangeIterator::checkForSpanner()
{
    if (!is<RenderMultiColumnSpannerPlaceholder>(m_current))
        return;
    auto& placeholder = downcast<RenderMultiColumnSpannerPlaceholder>(*m_current);
    m_spannerStack.append(&placeholder);
    m_current = placeholder.spanner();
}

static RenderObject* rendererAfterOffset(const RenderObject& renderer, unsigned offset) // <MMG> used in both, might should not be static
{
    auto* child = renderer.childAt(offset);
    return child ? child : renderer.nextInPreOrderAfterChildren();
}

void HighlightData::setRenderRange(const RenderRange& renderRange)
{
    ASSERT(renderRange.start() && renderRange.end());
    m_renderRange = renderRange;
}

bool HighlightData::setRenderRange(const HighlightRangeData& rangeData)
{
    if (!rangeData.startPosition || !rangeData.endPosition)
        return false;
    
    auto startPosition = rangeData.startPosition.value();
    auto endPosition = rangeData.endPosition.value();
    
    if (!startPosition.containerNode() || !endPosition.containerNode())
        return false;
    
    auto* startRenderer = startPosition.containerNode()->renderer();
    auto* endRenderer = endPosition.containerNode()->renderer();
    
    if (!startRenderer || !endRenderer)
        return false;
    
    unsigned startOffset = startPosition.computeOffsetInContainerNode();
    unsigned endOffset = endPosition.computeOffsetInContainerNode();

    setRenderRange({ startRenderer, endRenderer, startOffset, endOffset });
    return true;
}

RenderObject::HighlightState HighlightData::highlightStateForRenderer(const RenderObject& renderer)
{
    if (&renderer == m_renderRange.start()) {
        if (m_renderRange.start() && m_renderRange.end() && m_renderRange.start() == m_renderRange.end())
            return RenderObject::HighlightState::Both;
        if (m_renderRange.start())
            return RenderObject::HighlightState::Start;
    }
    if (&renderer == m_renderRange.end())
        return RenderObject::HighlightState::End;

    auto* highlightEnd = rendererAfterOffset(*m_renderRange.end(), m_renderRange.endOffset());
    
    RenderRangeIterator highlightIterator(m_renderRange.start());
    for (auto* currentRenderer = m_renderRange.start(); currentRenderer && currentRenderer != highlightEnd; currentRenderer = highlightIterator.next()) {
        if (currentRenderer == m_renderRange.start())
            continue;
        if (!currentRenderer->canBeSelectionLeaf())
            continue;
        if (&renderer == currentRenderer)
            return RenderObject::HighlightState::Inside;
    }
    return RenderObject::HighlightState::None;
}

} // namespace WebCore
