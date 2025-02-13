/*
 * Copyright (C) 2019 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "LayoutIntegrationLineLayout.h"

#if ENABLE(LAYOUT_FORMATTING_CONTEXT)

#include "EventRegion.h"
#include "FloatingState.h"
#include "HitTestLocation.h"
#include "HitTestRequest.h"
#include "HitTestResult.h"
#include "InlineFormattingContext.h"
#include "InlineFormattingState.h"
#include "InvalidationState.h"
#include "LayoutBoxGeometry.h"
#include "LayoutIntegrationCoverage.h"
#include "LayoutIntegrationPagination.h"
#include "LayoutTreeBuilder.h"
#include "PaintInfo.h"
#include "RenderBlockFlow.h"
#include "RenderChildIterator.h"
#include "RenderDescendantIterator.h"
#include "RenderLineBreak.h"
#include "RenderView.h"
#include "RuntimeEnabledFeatures.h"
#include "Settings.h"
#include "TextDecorationPainter.h"
#include "TextPainter.h"

namespace WebCore {
namespace LayoutIntegration {

LineLayout::LineLayout(const RenderBlockFlow& flow)
    : m_flow(flow)
    , m_boxTree(flow)
    , m_layoutState(m_flow.document(), rootLayoutBox())
    , m_inlineFormattingState(m_layoutState.ensureInlineFormattingState(rootLayoutBox()))
{
    m_layoutState.setIsIntegratedRootBoxFirstChild(m_flow.parent()->firstChild() == &m_flow);
}

LineLayout::~LineLayout() = default;

bool LineLayout::isEnabled()
{
    return RuntimeEnabledFeatures::sharedFeatures().layoutFormattingContextIntegrationEnabled();
}

bool LineLayout::canUseFor(const RenderBlockFlow& flow)
{
    if (!isEnabled())
        return false;

    return canUseForLineLayout(flow);
}

bool LineLayout::canUseForAfterStyleChange(const RenderBlockFlow& flow, StyleDifference diff)
{
    ASSERT(isEnabled());
    return canUseForLineLayoutAfterStyleChange(flow, diff);
}

void LineLayout::updateReplacedDimensions(const RenderBox& replaced)
{
    auto& layoutBox = *m_boxTree.layoutBoxForRenderer(replaced);
    auto& replacedBox = const_cast<Layout::ReplacedBox&>(downcast<Layout::ReplacedBox>(layoutBox));

    replacedBox.setContentSizeForIntegration({ replaced.contentLogicalWidth(), replaced.contentLogicalHeight() });
}

void LineLayout::updateStyle()
{
    auto& root = rootLayoutBox();

    // FIXME: Encapsulate style updates better.
    root.updateStyle(m_flow.style());

    for (auto* child = root.firstChild(); child; child = child->nextSibling()) {
        if (child->isAnonymous())
            child->updateStyle(RenderStyle::createAnonymousStyleWithDisplay(root.style(), DisplayType::Inline));
    }
}

void LineLayout::layout()
{
    if (!rootLayoutBox().hasInFlowOrFloatingChild())
        return;

    prepareLayoutState();
    prepareFloatingState();

    m_inlineContent = nullptr;
    auto inlineFormattingContext = Layout::InlineFormattingContext { rootLayoutBox(), m_inlineFormattingState };

    auto invalidationState = Layout::InvalidationState { };
    auto horizontalConstraints = Layout::HorizontalConstraints { m_flow.borderAndPaddingStart(), m_flow.contentSize().width() };
    auto verticalConstraints = Layout::VerticalConstraints { m_flow.borderAndPaddingBefore(), { } };

    inlineFormattingContext.layoutInFlowContent(invalidationState, { horizontalConstraints, verticalConstraints });
    constructContent();
}

void LineLayout::constructContent()
{
    auto& displayInlineContent = ensureInlineContent();

    auto constructDisplayLineRuns = [&] {
        auto initialContaingBlockSize = m_layoutState.viewportSize();
        for (auto& lineRun : m_inlineFormattingState.lineRuns()) {
            auto& layoutBox = lineRun.layoutBox();
            auto computedInkOverflow = [&] (auto runRect) {
                // FIXME: Add support for non-text ink overflow.
                if (!lineRun.text())
                    return runRect;
                auto& style = layoutBox.style();
                auto inkOverflow = runRect;
                auto strokeOverflow = std::ceil(style.computedStrokeWidth(ceiledIntSize(initialContaingBlockSize)));
                inkOverflow.inflate(strokeOverflow);

                auto letterSpacing = style.fontCascade().letterSpacing();
                if (letterSpacing < 0) {
                    // Last letter's negative spacing shrinks logical rect. Push it to ink overflow.
                    inkOverflow.expand(-letterSpacing, { });
                }
                return inkOverflow;
            };
            auto runRect = FloatRect { lineRun.logicalRect() };
            // Inline boxes are relative to the line box while final Runs need to be relative to the parent Box
            // FIXME: Shouldn't we just leave them be relative to the line box?
            auto lineIndex = lineRun.lineIndex();
            auto lineBoxLogicalRect = m_inlineFormattingState.lines()[lineIndex].lineBoxLogicalRect();
            runRect.moveBy({ lineBoxLogicalRect.left(), lineBoxLogicalRect.top() });
            // InlineTree rounds y position to integral value, see InlineFlowBox::placeBoxesInBlockDirection.
            auto needsLegacyIntegralPosition = !layoutBox.isReplacedBox();
            if (needsLegacyIntegralPosition)
                runRect.setY(roundToInt(runRect.y()));

            WTF::Optional<Run::TextContent> textContent;
            if (auto text = lineRun.text())
                textContent = Run::TextContent { text->start(), text->length(), text->content(), text->needsHyphen() };
            auto expansion = Run::Expansion { lineRun.expansion().behavior, lineRun.expansion().horizontalExpansion };
            auto displayRun = Run { lineIndex, layoutBox, runRect, computedInkOverflow(runRect), expansion, textContent };
            displayInlineContent.runs.append(displayRun);

            if (layoutBox.isReplacedBox()) {
                auto& renderer = downcast<RenderBox>(*rendererForLayoutBox(layoutBox));
                auto borderBoxLocation = FloatPoint { runRect.x(), runRect.y() + m_layoutState.geometryForBox(layoutBox).marginBefore() };
                const_cast<RenderBox&>(renderer).setLocation(flooredLayoutPoint(borderBoxLocation));
            }
        }
    };
    constructDisplayLineRuns();

    auto constructDisplayLine = [&] {
        auto& lines = m_inlineFormattingState.lines();
        auto& runs = displayInlineContent.runs;
        size_t runIndex = 0;
        for (size_t lineIndex = 0; lineIndex < lines.size(); ++lineIndex) {
            auto& line = lines[lineIndex];
            auto lineBoxLogicalRect = line.lineBoxLogicalRect();
            // FIXME: This is where the logical to physical translate should happen.
            auto overflowWidth = [&] {
                // FIXME: It's the copy of the lets-adjust-overflow-for-the-caret behavior from ComplexLineLayout::addOverflowFromInlineChildren.
                auto endPadding = m_flow.hasOverflowClip() ? m_flow.paddingEnd() : 0_lu;
                if (!endPadding)
                    endPadding = m_flow.endPaddingWidthForCaret();
                if (m_flow.hasOverflowClip() && !endPadding && m_flow.element() && m_flow.element()->isRootEditableElement())
                    endPadding = 1;
                auto lineBoxLogicalWidth = lineBoxLogicalRect.width() + endPadding;
                return std::max(line.logicalWidth(), lineBoxLogicalWidth);
            };
            auto lineBoxLogicalBottom = (lineBoxLogicalRect.top() - line.logicalTop()) + lineBoxLogicalRect.height();
            auto overflowHeight = std::max(line.logicalHeight(), lineBoxLogicalBottom);
            auto scrollableOverflowRect = FloatRect { line.logicalLeft(), line.logicalTop(), overflowWidth(), overflowHeight };

            auto firstRunIndex = runIndex;
            auto lineInkOverflowRect = scrollableOverflowRect;
            while (runIndex < runs.size() && runs[runIndex].lineIndex() == lineIndex)
                lineInkOverflowRect.unite(runs[runIndex++].inkOverflow());
            auto runCount = runIndex - firstRunIndex;
            auto lineRect = FloatRect { line.logicalRect() };
            // Painting code (specifically TextRun's xPos) needs the line box offset to be able to compute tab positions.
            lineRect.setX(lineBoxLogicalRect.left());
            // InlineTree rounds y position to integral value, see InlineFlowBox::placeBoxesInBlockDirection.
            lineRect.setY(roundToInt(lineRect.y()));
            displayInlineContent.lines.append({ firstRunIndex, runCount, lineRect, scrollableOverflowRect, lineInkOverflowRect, line.baseline(), line.horizontalAlignmentOffset() });
        }
    };
    constructDisplayLine();
    displayInlineContent.clearGapAfterLastLine = m_inlineFormattingState.clearGapAfterLastLine();
    displayInlineContent.shrinkToFit();
    m_inlineFormattingState.shrinkToFit();
}

void LineLayout::prepareLayoutState()
{
    m_layoutState.setViewportSize(m_flow.frame().view()->size());

    auto& rootGeometry = m_layoutState.ensureGeometryForBox(rootLayoutBox());
    rootGeometry.setContentBoxWidth(m_flow.contentSize().width());
    rootGeometry.setPadding({ { } });
    rootGeometry.setBorder({ });
    rootGeometry.setHorizontalMargin({ });
    rootGeometry.setVerticalMargin({ });
}

void LineLayout::prepareFloatingState()
{
    auto& floatingState = m_inlineFormattingState.floatingState();
    floatingState.clear();

    if (!m_flow.containsFloats())
        return;

    for (auto& floatingObject : *m_flow.floatingObjectSet()) {
        auto& rect = floatingObject->frameRect();
        auto position = floatingObject->type() == FloatingObject::FloatRight
            ? Layout::FloatingState::FloatItem::Position::Right
            : Layout::FloatingState::FloatItem::Position::Left;
        auto boxGeometry = Layout::BoxGeometry { };
        // FIXME: We are flooring here for legacy compatibility.
        //        See FloatingObjects::intervalForFloatingObject and RenderBlockFlow::clearFloats.
        auto y = rect.y().floor();
        auto maxY = rect.maxY().floor();
        boxGeometry.setLogicalTopLeft({ rect.x(), y });
        boxGeometry.setContentBoxWidth(rect.width());
        boxGeometry.setContentBoxHeight(maxY - y);
        boxGeometry.setBorder({ });
        boxGeometry.setPadding({ });
        boxGeometry.setHorizontalMargin({ });
        boxGeometry.setVerticalMargin({ });
        floatingState.append({ position, boxGeometry });
    }
}

LayoutUnit LineLayout::contentLogicalHeight() const
{
    if (m_paginatedHeight)
        return *m_paginatedHeight;
    if (!m_inlineContent)
        return { };

    auto& lines = m_inlineContent->lines;
    return LayoutUnit { lines.last().rect().maxY() - lines.first().rect().y() + m_inlineContent->clearGapAfterLastLine };
}

size_t LineLayout::lineCount() const
{
    if (!m_inlineContent)
        return 0;
    if (m_inlineContent->runs.isEmpty())
        return 0;

    return m_inlineContent->lines.size();
}

LayoutUnit LineLayout::firstLineBaseline() const
{
    if (!m_inlineContent || m_inlineContent->lines.isEmpty()) {
        ASSERT_NOT_REACHED();
        return { };
    }

    auto& firstLine = m_inlineContent->lines.first();
    return LayoutUnit { firstLine.rect().y() + firstLine.baseline() };
}

LayoutUnit LineLayout::lastLineBaseline() const
{
    if (!m_inlineContent || m_inlineContent->lines.isEmpty()) {
        ASSERT_NOT_REACHED();
        return { };
    }

    auto& lastLine = m_inlineContent->lines.last();
    return LayoutUnit { lastLine.rect().y() + lastLine.baseline() };
}

void LineLayout::adjustForPagination(RenderBlockFlow& flow)
{
    ASSERT(&flow == &m_flow);
    auto paginedInlineContent = adjustLinePositionsForPagination(*m_inlineContent, flow);
    if (paginedInlineContent.ptr() == m_inlineContent) {
        m_paginatedHeight = { };
        return;
    }

    auto& lines = paginedInlineContent->lines;
    m_paginatedHeight = LayoutUnit { lines.last().rect().maxY() - lines.first().rect().y() };

    m_inlineContent = WTFMove(paginedInlineContent);
}

void LineLayout::collectOverflow(RenderBlockFlow& flow)
{
    ASSERT(&flow == &m_flow);

    for (auto& line : inlineContent()->lines) {
        flow.addLayoutOverflow(Layout::toLayoutRect(line.scrollableOverflow()));
        if (!flow.hasOverflowClip())
            flow.addVisualOverflow(Layout::toLayoutRect(line.inkOverflow()));
    }
}

InlineContent& LineLayout::ensureInlineContent()
{
    if (!m_inlineContent)
        m_inlineContent = InlineContent::create(*this);
    return *m_inlineContent;
}

TextRunIterator LineLayout::textRunsFor(const RenderText& renderText) const
{
    if (!m_inlineContent)
        return { };
    auto* layoutBox = m_boxTree.layoutBoxForRenderer(renderText);
    ASSERT(layoutBox);

    auto firstIndex = [&]() -> Optional<size_t> {
        for (size_t i = 0; i < m_inlineContent->runs.size(); ++i) {
            if (&m_inlineContent->runs[i].layoutBox() == layoutBox)
                return i;
        }
        return { };
    }();

    if (!firstIndex)
        return { };

    return { RunIteratorModernPath(*m_inlineContent, *firstIndex) };
}

RunIterator LineLayout::runFor(const RenderElement& renderElement) const
{
    if (!m_inlineContent)
        return { };
    auto* layoutBox = m_boxTree.layoutBoxForRenderer(renderElement);
    ASSERT(layoutBox);

    for (size_t i = 0; i < m_inlineContent->runs.size(); ++i) {
        auto& run =  m_inlineContent->runs[i];
        if (&run.layoutBox() == layoutBox)
            return { RunIteratorModernPath(*m_inlineContent, i) };
    }

    return { };
}

const RenderObject* LineLayout::rendererForLayoutBox(const Layout::Box& layoutBox) const
{
    return m_boxTree.rendererForLayoutBox(layoutBox);
}

const Layout::ContainerBox& LineLayout::rootLayoutBox() const
{
    return m_boxTree.rootLayoutBox();
}

Layout::ContainerBox& LineLayout::rootLayoutBox()
{
    return m_boxTree.rootLayoutBox();
}

void LineLayout::paint(PaintInfo& paintInfo, const LayoutPoint& paintOffset)
{
    if (!m_inlineContent)
        return;

    if (paintInfo.phase != PaintPhase::Foreground && paintInfo.phase != PaintPhase::EventRegion)
        return;

    auto& inlineContent = *m_inlineContent;
    float deviceScaleFactor = m_flow.document().deviceScaleFactor();

    auto paintRect = paintInfo.rect;
    paintRect.moveBy(-paintOffset);

    for (auto& run : inlineContent.runsForRect(paintRect)) {
        if (!run.textContent()) {
            auto* renderer = m_boxTree.rendererForLayoutBox(run.layoutBox());
            if (renderer && renderer->isReplaced() && is<RenderBox>(*renderer)) {
                auto& renderBox = const_cast<RenderBox&>(downcast<RenderBox>(*renderer));
                if (renderBox.hasSelfPaintingLayer())
                    continue;
                if (!paintInfo.shouldPaintWithinRoot(renderBox))
                    continue;
                renderBox.paintAsInlineBlock(paintInfo, paintOffset);
            }
            continue;
        }

        auto& textContent = *run.textContent();
        if (!textContent.length())
            continue;

        auto& style = run.style();
        if (style.visibility() != Visibility::Visible)
            continue;

        auto rect = FloatRect { run.rect() };
        auto visualOverflowRect = FloatRect { run.inkOverflow() };
        if (paintRect.y() > visualOverflowRect.maxY() || paintRect.maxY() < visualOverflowRect.y())
            continue;

        if (paintInfo.eventRegionContext) {
            if (style.pointerEvents() != PointerEvents::None)
                paintInfo.eventRegionContext->unite(enclosingIntRect(visualOverflowRect), style);
            continue;
        }

        auto& line = inlineContent.lineForRun(run);
        auto baseline = paintOffset.y() + line.rect().y() + line.baseline();
        auto expansion = run.expansion();

        String textWithHyphen;
        if (textContent.needsHyphen())
            textWithHyphen = makeString(textContent.content(), style.hyphenString());
        // TextRun expects the xPos to be adjusted with the aligment offset (e.g. when the line is center aligned
        // and the run starts at 100px, due to the horizontal aligment, the xpos is supposed to be at 0px).
        auto xPos = rect.x() - (line.rect().x() + line.horizontalAlignmentOffset());
        WebCore::TextRun textRun { !textWithHyphen.isEmpty() ? textWithHyphen : textContent.content(), xPos, expansion.horizontalExpansion, expansion.behavior };
        textRun.setTabSize(!style.collapseWhiteSpace(), style.tabSize());
        FloatPoint textOrigin { rect.x() + paintOffset.x(), roundToDevicePixel(baseline, deviceScaleFactor) };

        TextPainter textPainter(paintInfo.context());
        textPainter.setFont(style.fontCascade());
        textPainter.setStyle(computeTextPaintStyle(m_flow.frame(), style, paintInfo));
        if (auto* debugShadow = debugTextShadow())
            textPainter.setShadow(debugShadow);

        textPainter.setGlyphDisplayListIfNeeded(run, paintInfo, style.fontCascade(), paintInfo.context(), textRun);
        textPainter.paint(textRun, rect, textOrigin);

        if (!style.textDecorationsInEffect().isEmpty()) {
            // FIXME: Use correct RenderText.
            if (auto* textRenderer = childrenOfType<RenderText>(m_flow).first()) {
                auto painter = TextDecorationPainter { paintInfo.context(), style.textDecorationsInEffect(), *textRenderer, false, style.fontCascade() };
                painter.setWidth(rect.width());
                painter.paintTextDecoration(textRun, textOrigin, rect.location() + paintOffset);
            }
        }
    }
}

bool LineLayout::hitTest(const HitTestRequest& request, HitTestResult& result, const HitTestLocation& locationInContainer, const LayoutPoint& accumulatedOffset, HitTestAction hitTestAction)
{
    if (hitTestAction != HitTestForeground)
        return false;

    if (!m_inlineContent)
        return false;

    auto& inlineContent = *m_inlineContent;

    // FIXME: This should do something efficient to find the run range.
    for (auto& run : inlineContent.runs) {
        auto runRect = Layout::toLayoutRect(run.rect());
        runRect.moveBy(accumulatedOffset);

        if (!locationInContainer.intersects(runRect))
            continue;

        auto& style = run.style();
        if (style.visibility() != Visibility::Visible || style.pointerEvents() == PointerEvents::None)
            continue;

        auto& renderer = const_cast<RenderObject&>(*m_boxTree.rendererForLayoutBox(run.layoutBox()));

        renderer.updateHitTestResult(result, locationInContainer.point() - toLayoutSize(accumulatedOffset));
        if (result.addNodeToListBasedTestResult(renderer.nodeForHitTest(), request, locationInContainer, runRect) == HitTestProgress::Stop)
            return true;
    }

    return false;
}

ShadowData* LineLayout::debugTextShadow()
{
    if (!m_flow.settings().simpleLineLayoutDebugBordersEnabled())
        return nullptr;

    static NeverDestroyed<ShadowData> debugTextShadow(IntPoint(0, 0), 10, 20, ShadowStyle::Normal, true, SRGBA<uint8_t> { 0, 0, 150, 150 });
    return &debugTextShadow.get();
}

void LineLayout::releaseCaches(RenderView& view)
{
    if (!RuntimeEnabledFeatures::sharedFeatures().layoutFormattingContextIntegrationEnabled())
        return;

    for (auto& renderer : descendantsOfType<RenderBlockFlow>(view)) {
        if (auto* lineLayout = renderer.layoutFormattingContextLineLayout())
            lineLayout->releaseInlineItemCache();
    }
}

void LineLayout::releaseInlineItemCache()
{
    m_inlineFormattingState.inlineItems().clear();
}


}
}

#endif
