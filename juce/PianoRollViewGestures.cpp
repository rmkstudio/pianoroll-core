// PianoRollView mouse gestures, one block per tool. mouseDown chooses the tool
// from the pressed region, button and modifiers; mouseDrag and mouseUp forward
// to the active gesture. Every edit goes through the host's beginEdit(),
// editChanged() and commitEdit(); none of these functions publishes on its own.
#include <PianoRollCore/PianoRollView.h>
using namespace juce;
namespace pianoroll
{
namespace
{
Rect coreRect(Rectangle<float> r)
{
    return {r.getX(), r.getY(), r.getWidth(), r.getHeight()};
}
bool crosses(Rectangle<float> r, Line<float> l)
{
    juce::Point<float> out;
    return r.contains(l.getStart()) || r.contains(l.getEnd()) ||
           l.intersects(Line<float>(r.getTopLeft(), r.getTopRight()), out) ||
           l.intersects(Line<float>(r.getTopRight(), r.getBottomRight()), out) ||
           l.intersects(Line<float>(r.getBottomRight(), r.getBottomLeft()), out) ||
           l.intersects(Line<float>(r.getBottomLeft(), r.getTopLeft()), out);
}
}

void PianoRollView::captureBaseline()
{
    before.clear();
    before.reserve(host.noteCount());
    for (std::size_t i = 0; i < host.noteCount(); ++i)
        before.push_back(host.note(i));
    host.baselineCaptured();
}
void PianoRollView::audition(int pitch, int velocity)
{
    if (host.auditionNote(pitch, velocity))
        lastAudition = pitch;
}

// ---- Shared mouse-down capture and dispatch ----

void PianoRollView::mouseDown(const MouseEvent& e)
{
    grabKeyboardFocus();
    anchor = e.position;
    host.gestureStarted(jmax(0.0, xToTime(e.position.x)));
    before.clear();
    for (std::size_t i = 0; i < host.noteCount(); ++i)
        before.push_back(host.note(i));
    lastAudition = -1;
    shiftAtDown = e.mods.isShiftDown();
    if (e.mods.isMiddleButtonDown())
    {
        beginPan(e);
        return;
    }
    if (rulerArea.contains(e.position))
    {
        beginRuler(e);
        return;
    }
    if (showVelocity && velocityDivider.contains(e.position))
    {
        gesture = gesture::LaneSplit{true};
        return;
    }
    if (showCC && ccDivider.contains(e.position))
    {
        gesture = gesture::LaneSplit{false};
        return;
    }
    if (keysArea.contains(e.position))
    {
        host.pianoKeyDown(yToPitch(e.y));
        return;
    }
    if ((!host.hasDocument() && !host.canCreateNote()) || host.busy())
        return;
    if (e.mods.isCtrlDown() && e.mods.isAltDown() && host.hasSelection())
    {
        gesture = gesture::ExportNotes{};
        return;
    }
    if (ccArea.contains(e.position))
    {
        beginControllerStroke(e);
        return;
    }
    if (notesArea.contains(e.position))
    {
        if (!beginNoteGesture(e))
            return;
    }
    else if (velocityArea.contains(e.position))
    {
        if (!beginVelocityStroke(e))
            return;
    }
    repaint();
}
void PianoRollView::mouseDrag(const MouseEvent& e)
{
    hover = e.position;
    host.pointerMoved(e.position);
    if (keysArea.contains(anchor) && std::holds_alternative<std::monostate>(gesture))
    {
        const int pitch = yToPitch(e.y);
        if (pitch != lastAudition && host.pianoKeyDragged(pitch)) lastAudition = pitch;
        return;
    }
    if (auto* split = std::get_if<gesture::LaneSplit>(&gesture))
        dragLaneSplit(*split, e);
    else if (auto* stroke = std::get_if<gesture::ControllerStroke>(&gesture))
    {
        if (!host.busy())
            paintController(*stroke, e.position);
    }
    else if (auto* velocity = std::get_if<gesture::Velocity>(&gesture))
    {
        if (!host.busy())
            paintVelocity(*velocity, e.position);
    }
    else if (auto* pan = std::get_if<gesture::Pan>(&gesture))
        applyNavigation(
            dragNavigation(pan->drag, {e.position.x, e.position.y}, navigationLimits()).view);
    else if (std::holds_alternative<gesture::Ruler>(gesture))
        repaint();
    else if (std::holds_alternative<gesture::ExportNotes>(gesture))
    {
        if (e.getDistanceFromDragStart() > 8)
        {
            host.exportSelectedNotes();
            gesture = {};
        }
    }
    else if (auto* marquee = std::get_if<gesture::Marquee>(&gesture))
        dragMarquee(*marquee, e);
    else if (auto* erase = std::get_if<gesture::Erase>(&gesture))
        eraseAt(*erase, e.position);
    else if (auto* notes = std::get_if<gesture::NoteDrag>(&gesture))
        dragNotes(*notes, e);
}
void PianoRollView::mouseUp(const MouseEvent& e)
{
    if (keysArea.contains(anchor) && host.pianoKeyUp(yToPitch(anchor.y)))
        return;
    if (auto* ruler = std::get_if<gesture::Ruler>(&gesture))
        finishRuler(*ruler, e);
    else if (std::holds_alternative<gesture::Marquee>(gesture) &&
             isPointerClick({anchor.x, anchor.y}, {e.position.x, e.position.y}) &&
             !e.mods.isCtrlDown())
    {
        host.seek(quantize(xToTime(e.x), e.mods.isAltDown()));
        follow.transportLocated();
    }
    if (std::holds_alternative<gesture::LaneSplit>(gesture))
        host.laneLayoutChanged();
    if (const auto* notes = std::get_if<gesture::NoteDrag>(&gesture))
        if (notes->edit == gesture::NoteDrag::Edit::move && e.getDistanceFromDragStart() > 4)
            host.noteGestureDropped(e.position);
    host.commitEdit();
    gesture = {};
    updateCursor(e.position, e.mods);
    host.stopAudition();
    repaint();
}
void PianoRollView::mouseDoubleClick(const MouseEvent& e)
{
    if (e.mods.isMiddleButtonDown() || e.mods.isRightButtonDown() ||
        !notesArea.contains(e.position) || host.busy() ||
        (!host.hasDocument() && !host.canCreateNote()))
        return;
    int i = hit(e.position);
    if (i >= 0 && host.note((std::size_t)i).editable)
    {
        host.beginEdit();
        host.eraseNote((std::size_t)i);
        host.editChanged();
        host.commitEdit();
    }
    else if (hit(e.position, true) < 0 && !isPaintMode())
    {
        showTool(true);
        mouseDown(e);
        showTool(false);
        host.commitEdit();
    }
    gesture = {};
    host.stopAudition();
    repaint();
}
void PianoRollView::mouseWheelMove(const MouseEvent& e, const MouseWheelDetails& d)
{
    if (e.mods.isAltDown())
    {
        if (host.busy() || !host.hasDocument())
            return;
        // The selection, or else the hovered note.
        const bool selected = host.hasSelection();
        const int hovered = selected ? -1 : hit(e.position);
        if (!selected && hovered < 0)
            return;
        host.beginEdit();
        int amount = velocityWheelDelta(d.deltaY);
        for (std::size_t i = 0; i < host.noteCount(); ++i)
        {
            const auto n = host.note(i);
            if ((selected ? host.noteSelected(i) : (int)i == hovered) && n.editable && inScope(n))
                host.setNoteVelocity(i, jlimit(1, 127, n.velocity + amount));
        }
        host.editChanged();
        host.commitEdit();
        return;
    }
    follow.userNavigated();
    applyNavigation(wheelNavigation(navigationView(), coreRect(notesArea),
                                    {e.position.x, e.position.y}, d.deltaX, d.deltaY,
                                    {e.mods.isCtrlDown() || e.mods.isCommandDown(),
                                     e.mods.isShiftDown(), e.mods.isAltDown()},
                                    navigationLimits())
                        .view);
}

// ---- Navigation, ruler and lane boundaries ----

void PianoRollView::beginPan(const MouseEvent& e)
{
    gesture::Pan pan;
    pan.drag =
        beginMiddleDrag(keysArea.contains(e.position) ? PointerRegion::keys : PointerRegion::notes,
                        {e.position.x, e.position.y}, navigationView(), coreRect(notesArea));
    follow.userNavigated();
    gesture = pan;
    setMouseCursor(keysArea.contains(e.position) ? MouseCursor::UpDownResizeCursor
                                                 : MouseCursor::DraggingHandCursor);
}
void PianoRollView::beginRuler(const MouseEvent& e)
{
    if (e.mods.isRightButtonDown())
    {
        host.clearLoop();
        repaint();
        return;
    }
    gesture::Ruler ruler;
    ruler.ruler.begin({e.position.x, e.position.y}, navigationView(), coreRect(rulerArea));
    gesture = ruler;
}
void PianoRollView::finishRuler(gesture::Ruler& ruler, const MouseEvent& e)
{
    const auto action =
        ruler.ruler.finish({e.position.x, e.position.y}, snapPolicy(e.mods.isAltDown()));
    if (action.action == RulerAction::seek)
    {
        host.seek(action.start);
        follow.transportLocated();
    }
    else if (action.action == RulerAction::setLoop)
    {
        host.setLoop(action.start, action.end);
        follow.transportLocated();
    }
}
void PianoRollView::dragLaneSplit(const gesture::LaneSplit& split, const MouseEvent& e)
{
    // Fractions are shares of the lane stack the layout reported, from the top of the
    // notes to the bottom of the last visible lane. The grab handles sit 9 px below the
    // notes (velocity) and 12 px below the velocity lane (CC), so the handle stays under
    // the mouse.
    if (host.resizeLane(split.aboveVelocity, e.y - anchor.y))
    {
        resized();
        repaint();
        return;
    }
    const float top = notesArea.getY(), usable = jmax(100.f, laneBottom - top);
    if (split.aboveVelocity)
    {
        const float gap = velocityDivider.getCentreY() - notesArea.getBottom();
        const float above = (e.y - top - gap) / usable;
        if (showCC)
            noteFraction = jlimit(.25f, .94f - velocityFraction, above);
        else
            velocityFraction = jlimit(.06f, .65f, 1.f - above);
    }
    else
    {
        const float gap = ccDivider.getCentreY() -
                          (showVelocity ? velocityArea.getBottom() : notesArea.getBottom());
        const float above = (e.y - top - gap) / usable;
        if (showVelocity)
            velocityFraction =
                jlimit(.06f, jmax(.06f, .94f - noteFraction), above - noteFraction);
        else
            noteFraction = jlimit(.25f, .94f - velocityFraction, above - velocityFraction);
    }
    resized();
    repaint();
}

// ---- Notes area: select / marquee, move, resize, copy, pencil, erase ----

bool PianoRollView::beginNoteGesture(const MouseEvent& e)
{
    const bool control = e.mods.isCtrlDown() || e.mods.isCommandDown();
    const bool focusClick = e.mods.isLeftButtonDown() && !control;
    int grab = hit(e.position, focusClick);
    if (grab >= 0 && focusClick)
        host.focusGroupOf((std::size_t)grab);
    if (e.mods.isRightButtonDown())
    {
        host.beginEdit();
        gesture = gesture::Erase{e.position};
        eraseAt(std::get<gesture::Erase>(gesture), e.position);
        return false;
    }
    if (control || (grab < 0 && !isPaintMode()))
    {
        if (grab >= 0)
        {
            const auto index = (std::size_t)grab;
            if (e.mods.isShiftDown())
            {
                if (host.noteSelected(index))
                    host.deselectNote(index);
                else
                    host.selectNote(index);
            }
            else
            {
                host.clearSelection();
                host.selectNote(index);
            }
            repaint();
            return false;
        }
        gesture::Marquee marquee;
        marquee.add = e.mods.isShiftDown();
        if (!marquee.add)
            host.clearSelection();
        marquee.box = {e.position, e.position};
        gesture = marquee;
        return false;
    }
    gesture::NoteDrag drag;
    if (grab >= 0)
    {
        const auto index = (std::size_t)grab;
        const auto grabbed = host.note(index);
        if (!grabbed.editable)
            return false;
        if (!host.noteSelected(index))
        {
            host.clearSelection();
            host.selectNote(index);
        }
        host.startSelectionCaptured();
        lastLength = grabbed.length;
        lastVelocity = grabbed.velocity;
        audition(grabbed.pitch, lastVelocity);
        host.beginEdit();
        captureBaseline();
        drag.grab = grab;
        drag.cloning = e.mods.isShiftDown();
        const auto r = noteRect(grabbed);
        drag.edit = e.position.x >= r.getRight() - jmin(7.f, r.getWidth() * .3f) && !drag.cloning
                        ? gesture::NoteDrag::Edit::resize
                        : gesture::NoteDrag::Edit::move;
        gesture = drag;
        return true;
    }
    if (!host.canCreateNote())
        return false;
    host.beginEdit();
    const auto start =
        host.roundTime(snapTime(xToTime(e.x), snapPolicy(e.mods.isAltDown(), Rounding::down)));
    const int pitch = yToPitch(e.y);
    drag.creation = true;
    drag.grab = (int)host.createNote(start, pitch, lastLength, lastVelocity);
    captureBaseline();
    drag.edit =
        e.mods.isShiftDown() ? gesture::NoteDrag::Edit::draw : gesture::NoteDrag::Edit::move;
    gesture = drag;
    host.editChanged();
    audition(pitch, lastVelocity);
    return true;
}
void PianoRollView::dragMarquee(gesture::Marquee& marquee, const MouseEvent& e)
{
    marquee.box = Rectangle<float>(anchor, e.position).getIntersection(notesArea);
    host.restoreStartSelection(marquee.add);
    forEachIntersecting(
        host.noteCount(), coreRect(marquee.box),
        [&](std::size_t i)
        {
            return coreRect(noteRect(host.note(i)));
        },
        [&](std::size_t i)
        {
            return inScope(host.note(i));
        },
        [&](std::size_t i)
        {
            host.selectNote(i);
        });
    repaint();
}
void PianoRollView::eraseAt(gesture::Erase& erase, juce::Point<float> pt)
{
    for (int i = (int)host.noteCount(); --i >= 0;)
    {
        const auto n = host.note((std::size_t)i);
        if (inScope(n) && n.editable &&
            (noteRect(n).contains(pt) || crosses(noteRect(n), Line<float>(erase.last, pt))))
        {
            host.eraseNote((std::size_t)i);
            host.editChanged();
        }
    }
    erase.last = pt;
}
void PianoRollView::dragNotes(gesture::NoteDrag& drag, const MouseEvent& e)
{
    if (host.busy() || drag.grab < 0)
        return;
    if (e.getDistanceFromDragStart() < 3)
        return;
    const auto delta = e.position - anchor;
    if (drag.cloning && !drag.cloned)
    {
        std::vector<std::size_t> indices;
        for (std::size_t i = 0; i < before.size(); ++i)
            if (host.startSelected(i) || i == (std::size_t)drag.grab)
                indices.push_back(i);
        const auto plan = makeCopyPlan(indices, before.size(), (std::size_t)drag.grab,
                                       [&](std::size_t i)
                                       {
                                           return before[i].editable;
                                       });
        if (plan.anchor == noIndex)
            return;
        host.clearSelection();
        for (const auto& entry : plan.entries)
            host.appendBaselineCopy(entry.source);
        captureBaseline();
        drag.grab = (int)plan.anchor;
        drag.cloned = true;
    }
    // The host's follow rules may end the gesture, which destroys `drag`.
    const int grab = drag.grab;
    const auto edit = drag.edit;
    const bool follows = edit == gesture::NoteDrag::Edit::move && !drag.creation;
    const bool copied = drag.cloned;
    const auto& grabbed = before[(std::size_t)grab];
    const auto bounds = selectedBounds(before);
    const auto dt =
        quantize(grabbed.start + delta.x * span / notesArea.getWidth(), e.mods.isAltDown()) -
        grabbed.start;
    int pitchDelta = roundToInt(-delta.y / row);
    if (host.pitchesFolded() && !pitchRows.empty())
    {
        const auto found = std::lower_bound(pitchRows.begin(), pitchRows.end(), grabbed.pitch);
        if (found == pitchRows.end() || *found != grabbed.pitch) return;
        const int targetRow = jlimit(0, (int)pitchRows.size() - 1,
            (int)std::distance(pitchRows.begin(), found) + pitchDelta);
        pitchDelta = pitchRows[(std::size_t)targetRow] - grabbed.pitch;
    }
    const auto constrained = constrainNoteDrag(
        {dt, pitchDelta}, shiftAtDown,
        {e.mods.isCtrlDown() || e.mods.isCommandDown(), e.mods.isShiftDown(), e.mods.isAltDown()});
    const auto move = clampMove(bounds, constrained);
    const auto lengthDelta = clampResize(
        bounds,
        quantize(edit == gesture::NoteDrag::Edit::draw && host.noteDrawEndsAtPointer()
                     ? xToTime(e.x)
                     : grabbed.start + grabbed.length + delta.x * span / notesArea.getWidth(),
                 e.mods.isAltDown()) - grabbed.start - grabbed.length,
        host.minimumNoteLength());
    if (edit == gesture::NoteDrag::Edit::resize && host.linkedNoteEnds())
    {
        std::vector<ActionNote> input;
        const auto keySwitch = host.keySwitches();
        for (std::size_t i = 0; i < before.size(); ++i)
        {
            const auto& n = before[i];
            input.push_back({n.start, n.start + n.length, n.pitch, n.velocity,
                             host.linkedBoundarySource(i), i,
                             host.noteSelected(i) ||
                                 (n.group == grabbed.group &&
                                  host.linkedBoundarySource(i) == host.linkedBoundarySource((std::size_t)grab) &&
                                  std::abs(n.start - grabbed.start - grabbed.length) <= host.linkedBoundaryTolerance()),
                             n.editable, keySwitch(i), false});
        }
        ActionRules rules;
        rules.minimumLength = host.minimumNoteLength();
        rules.maximumTime = host.maximumTime();
        auto result = moveLinkedBoundary(input, (std::size_t)grab, true,
                                         grabbed.start + grabbed.length + lengthDelta,
                                         host.linkedBoundaryTolerance(), rules);
        if (!result)
        {
            host.reportMessage(String(result.error));
            return;
        }
        for (auto n : result.notes)
            host.setNoteTiming(n.originalIndex, host.roundTime(n.start),
                               host.roundTime(n.end) - host.roundTime(n.start));
        host.editChanged();
        return;
    }
    // Only fields belonging to this gesture are written back; length, velocity,
    // channel and note identity remain untouched.
    for (std::size_t i = 0; i < host.noteCount(); ++i)
        if (host.noteSelected(i) && host.note(i).editable)
        {
            if (edit == gesture::NoteDrag::Edit::resize || edit == gesture::NoteDrag::Edit::draw)
            {
                lastLength = before[i].length + host.roundTime(lengthDelta);
                host.setNoteLength(i, lastLength);
            }
            else
                host.moveNote(i, before[i].start + host.roundTime(move.time),
                              before[i].pitch + move.pitch);
        }
    if (follows && !host.followMovedNotes(copied))
        return;
    const auto current = host.note((std::size_t)grab);
    if (edit == gesture::NoteDrag::Edit::move && current.pitch != lastAudition)
        audition(current.pitch, current.velocity);
    host.editChanged();
}

// ---- Velocity and controller lanes ----

bool PianoRollView::beginVelocityStroke(const MouseEvent& e)
{
    if (!e.mods.isLeftButtonDown())
        return false;
    host.beginEdit();
    gesture::Velocity stroke;
    const auto count = host.noteCount();
    stroke.targets.resize(count);
    for (std::size_t i = 0; i < count; ++i)
        stroke.targets[i] = host.noteSelected(i);
    stroke.limited = host.hasSelection();
    stroke.relative = e.mods.isCtrlDown() || e.mods.isCommandDown();
    if (stroke.relative)
    {
        int closest = -1;
        float dist = 6;
        for (std::size_t i = 0; i < count; ++i)
        {
            const auto n = host.note(i);
            if (inScope(n) && n.editable &&
                (!stroke.limited || stroke.targets[i]))
            {
                float d = std::abs(timeToX(n.start) - e.x);
                if (d < dist)
                {
                    dist = d;
                    closest = (int)i;
                }
            }
        }
        if (closest < 0)
        {
            host.abandonEdit();
            return false;
        }
        stroke.anchorVelocity = host.note((std::size_t)closest).velocity;
        if (!host.relativeVelocityUsesScope() &&
            (!stroke.limited || !stroke.targets[(std::size_t)closest]))
        {
            stroke.targets.assign(count, false);
            stroke.targets[(std::size_t)closest] = true;
            stroke.limited = true;
        }
    }
    stroke.last = e.position;
    gesture = std::move(stroke);
    paintVelocity(std::get<gesture::Velocity>(gesture), e.position, true);
    return true;
}
void PianoRollView::paintVelocity(gesture::Velocity& stroke, juce::Point<float> pt, bool first)
{
    for (std::size_t i = 0; i < host.noteCount(); ++i)
    {
        const auto n = host.note(i);
        if (!inScope(n) || !n.editable ||
            (stroke.limited && (i >= stroke.targets.size() || !stroke.targets[i])))
            continue;
        int value = n.velocity;
        bool hit = false;
        if (stroke.relative)
        {
            value = relativeLaneValue(before[i].velocity, stroke.anchorVelocity,
                                      laneValueAt(pt.y, coreRect(velocityArea)));
            hit = true;
        }
        else
        {
            auto sample = sampleLaneStroke(timeToX(n.start), coreRect(velocityArea),
                                           {stroke.last.x, stroke.last.y}, {pt.x, pt.y}, first);
            value = sample.value;
            hit = sample.hit;
        }
        if (hit && n.velocity != value)
        {
            host.setNoteVelocity(i, value);
            host.editChanged();
        }
    }
    stroke.last = pt;
}
void PianoRollView::beginControllerStroke(const MouseEvent& e)
{
    if (!host.canDrawController())
        return;
    host.beginEdit();
    gesture::ControllerStroke stroke;
    stroke.erase = e.mods.isRightButtonDown();
    stroke.last = e.position;
    stroke.start = stroke.end = jmax(0.0, host.roundTime(xToTime(e.x)));
    host.beginControllerStroke(stroke.start, stroke.erase);
    gesture = std::move(stroke);
    paintController(std::get<gesture::ControllerStroke>(gesture), e.position);
}
void PianoRollView::paintController(gesture::ControllerStroke& stroke, juce::Point<float> pt)
{
    if (!host.controllerStrokeOpen())
        return;
    pt.x = jlimit(ccArea.getX(), ccArea.getRight(), pt.x);
    pt.y = jlimit(ccArea.getY(), ccArea.getBottom(), pt.y);
    const double a = jlimit(0.0, host.maximumTime(), host.roundTime(xToTime(stroke.last.x))),
                 b = jlimit(0.0, host.maximumTime(), host.roundTime(xToTime(pt.x)));
    int va = jlimit(0, 127,
                    roundToInt((ccArea.getBottom() - stroke.last.y) * 127 / ccArea.getHeight())),
        vb = jlimit(0, 127, roundToInt((ccArea.getBottom() - pt.y) * 127 / ccArea.getHeight()));
    stroke.start = jmin(stroke.start, jmin(a, b));
    stroke.end = jmax(stroke.end, jmax(a, b));
    if (!stroke.erase)
    {
        auto lo = stroke.points.lower_bound(jmin(a, b)), hi = stroke.points.upper_bound(jmax(a, b));
        stroke.points.erase(lo, hi);
        stroke.points[a] = va;
        stroke.points[b] = vb;
    }
    host.updateControllerStroke(stroke.start, stroke.end, stroke.points, stroke.erase);
    stroke.last = pt;
    host.editChanged();
}

// ---- Hover and cursor ----

void PianoRollView::updateCursor(juce::Point<float> point, ModifierKeys mods)
{
    if (std::holds_alternative<gesture::Pan>(gesture))
    {
        setMouseCursor(MouseCursor::DraggingHandCursor);
        return;
    }
    if ((showVelocity && velocityDivider.contains(point)) ||
        (showCC && ccDivider.contains(point)))
    {
        setMouseCursor(MouseCursor::UpDownResizeCursor);
        return;
    }
    if (host.busy() || !host.hasDocument())
    {
        setMouseCursor(MouseCursor::NormalCursor);
        return;
    }
    if (velocityArea.contains(point) || ccArea.contains(point))
    {
        setMouseCursor(pencil);
        return;
    }
    int i = notesArea.contains(point) ? hit(point, !mods.isCtrlDown() && !mods.isCommandDown() &&
                                                       !mods.isRightButtonDown())
                                      : -1;
    if (i >= 0)
    {
        const auto n = host.note((std::size_t)i);
        const auto r = noteRect(n);
        if (!n.editable)
            setMouseCursor(MouseCursor::NormalCursor);
        else if (mods.isShiftDown())
            setMouseCursor(copyCursor);
        else if (point.x >= r.getRight() - jmin(7.f, r.getWidth() * .3f))
            setMouseCursor(MouseCursor::LeftRightResizeCursor);
        else
            setMouseCursor(MouseCursor::DraggingHandCursor);
    }
    else
        setMouseCursor(notesArea.contains(point) ? (isPaintMode()
                                                        ? pencil
                                                        : MouseCursor(MouseCursor::CrosshairCursor))
                                                 : MouseCursor(MouseCursor::NormalCursor));
}
void PianoRollView::mouseMove(const MouseEvent& e)
{
    hover = e.position;
    host.pointerMoved(e.position);
    hoverFree = e.mods.isAltDown();
    updateCursor(e.position, e.mods);
    repaint();
}
void PianoRollView::mouseExit(const MouseEvent&)
{
    hover = {-1, -1};
    host.pointerMoved(hover);
    repaint();
}
}
