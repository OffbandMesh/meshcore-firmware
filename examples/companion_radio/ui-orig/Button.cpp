#include "Button.h"
#include <MeshCore.h>

// -----------------------------------------------------------------------------------
// #527: edge capture
//
// Arduino's attachInterrupt() on both nRF52 and ESP32 takes a bare function pointer, so
// there is no way to hand it a `this`. The usual workaround is a fixed table of static
// trampolines, one per attachable instance, each holding the instance it belongs to.
// ui-orig creates at most two Buttons (digital + analog), and the analog one cannot use
// an interrupt at all, so two slots is one more than is reachable.
// -----------------------------------------------------------------------------------

#ifdef BUTTON_IRQ_CAPTURE

#ifndef BUTTON_IRQ_SLOTS
#define BUTTON_IRQ_SLOTS 2
#endif

// ESP32 requires ISR code to be resident in IRAM: a handler in flash can fault if the
// interrupt lands while the flash cache is disabled (during an SPI flash write). Other
// targets have no such attribute.
#ifdef ESP32
  #define BUTTON_ISR_ATTR IRAM_ATTR
#else
  #define BUTTON_ISR_ATTR
#endif

static Button* s_irq_owner[BUTTON_IRQ_SLOTS] = { nullptr, nullptr };

static void BUTTON_ISR_ATTR button_isr_0() { if (s_irq_owner[0]) s_irq_owner[0]->captureEdgeFromISR(); }
static void BUTTON_ISR_ATTR button_isr_1() { if (s_irq_owner[1]) s_irq_owner[1]->captureEdgeFromISR(); }

static void (*const s_irq_trampoline[BUTTON_IRQ_SLOTS])() = { button_isr_0, button_isr_1 };

#endif  // BUTTON_IRQ_CAPTURE

Button::Button(uint8_t pin, bool activeState)
    : _pin(pin), _activeState(activeState), _isAnalog(false), _analogThreshold(20) {
}

Button::Button(uint8_t pin, bool activeState, bool isAnalog, uint16_t analogThreshold)
    : _pin(pin), _activeState(activeState), _isAnalog(isAnalog), _analogThreshold(analogThreshold) {
}

void Button::begin() {
    _seq.reset();
    // Seed the producer's notion of the current level so a button already held at boot
    // does not synthesise a phantom press on the first poll.
    _produced_level = readButton();

#ifdef BUTTON_IRQ_CAPTURE
    // Analog "buttons" are read through the ADC and have no digital edge to interrupt on.
    if (!_isAnalog) {
        for (int8_t i = 0; i < BUTTON_IRQ_SLOTS; i++) {
            if (s_irq_owner[i] == nullptr) {
                s_irq_owner[i] = this;
                _irq_slot = i;
                attachInterrupt(digitalPinToInterrupt(_pin), s_irq_trampoline[i], CHANGE);
                MESH_DEBUG_PRINTLN("[btn] edge capture armed on pin %d (slot %d)", (int)_pin, (int)i);
                break;
            }
        }
        // SAFELANE §6: if every slot is taken, say so. The polled producer still runs,
        // so the button keeps working -- just without interrupt-grade capture.
        if (_irq_slot < 0) {
            MESH_DEBUG_PRINTLN("[btn] WARNING no IRQ slot free for pin %d, polling only", (int)_pin);
        }
    }
#endif
}

bool Button::readButton() const {
    if (_isAnalog) {
        return (analogRead(_pin) < _analogThreshold);
    }
    return (digitalRead(_pin) == _activeState);
}

// Power of two so the wrap below is a mask rather than a division -- this runs in an
// ISR on the interrupt-capture path.
static_assert((BUTTON_EDGE_QUEUE_LEN & (BUTTON_EDGE_QUEUE_LEN - 1)) == 0,
              "BUTTON_EDGE_QUEUE_LEN must be a power of two");

// Producer side. Only ever advances _q_head, which the consumer never writes. Callers
// must guarantee it is not re-entered -- the ISR owns it outright, and update()'s poll
// path masks interrupts around its call for exactly that reason.
void Button::pushEdge(uint32_t ms, bool pressed) {
    uint8_t head = _q_head;
    uint8_t next = (uint8_t)((head + 1) & (BUTTON_EDGE_QUEUE_LEN - 1));
    if (next == _q_tail) {
        _dropped++;                  // full -- record the loss rather than hide it
        return;
    }
    _queue[head].ms = ms;
    _queue[head].pressed = pressed;
    _q_head = next;                  // publish only after the slot is fully written
    _produced_level = pressed;
}

// Consumer side. Only ever called from update().
bool Button::popEdge(Edge& out) {
    uint8_t tail = _q_tail;
    if (tail == _q_head) return false;
    out.ms = _queue[tail].ms;
    out.pressed = _queue[tail].pressed;
    _q_tail = (uint8_t)((tail + 1) & (BUTTON_EDGE_QUEUE_LEN - 1));
    return true;
}

void Button::captureEdgeFromISR() {
#ifdef BUTTON_IRQ_CAPTURE
    // CHANGE gives no direction, so read the level. Contact bounce can make this
    // disagree with the edge that actually fired; that is fine -- the sequencer
    // debounces on timestamps and ignores anything that is not a transition.
    bool pressed = (digitalRead(_pin) == _activeState);
    if (pressed == _produced_level) return;   // nothing new to record
    _raw_isr++;
    pushEdge(millis(), pressed);
#endif
}

// Pull events out of the sequencer until it has nothing more to say at `at`. One call
// can only return a single event, and a deferred edge followed by a resolved count
// produces two.
void Button::drainSequencer(uint32_t at) {
    for (;;) {
        ButtonSequencer::Event ev = _seq.tick(at);
        if (ev == ButtonSequencer::EV_NONE) return;
        if (ev == ButtonSequencer::EV_PRESS_EDGE) { triggerEvent(ANY_PRESS); continue; }

        // A gesture just resolved. Report what the PRODUCERS saw during it, separately
        // from what the sequencer made of it. `isr` counts pin transitions -- two per
        // press -- so this line distinguishes the two failures that look identical from
        // the outside: isr=6 resolving to a SINGLE would be a counting bug, whereas a
        // user who pressed four times and sees isr=6 has a contact that never opened.
        // Establishing that difference took several rounds of field logs; one line per
        // gesture, only while caplog is on, is a cheap way not to repeat them.
        MESH_DEBUG_PRINTLN("[btn] capture: isr=%d poll=%d dropped=%d irq=%s",
                           (int)_raw_isr, (int)_raw_poll, (int)_dropped,
                           (_irq_slot >= 0) ? "armed" : "OFF");
        _raw_isr = 0;
        _raw_poll = 0;

        switch (ev) {
            case ButtonSequencer::EV_SHORT:     triggerEvent(SHORT_PRESS); break;
            case ButtonSequencer::EV_DOUBLE:    triggerEvent(DOUBLE_PRESS); break;
            case ButtonSequencer::EV_TRIPLE:    triggerEvent(TRIPLE_PRESS); break;
            case ButtonSequencer::EV_QUADRUPLE: triggerEvent(QUADRUPLE_PRESS); break;
            case ButtonSequencer::EV_LONG:      triggerEvent(LONG_PRESS); break;
            default: break;
        }
    }
}

void Button::update() {
    uint32_t now = millis();

    // Polled producer. Under BUTTON_IRQ_CAPTURE this is a backstop: the ISR has usually
    // already published the edge and _produced_level matches, so this adds nothing. It
    // still runs, so a missed or unattachable interrupt degrades to the old sampling
    // behaviour instead of a dead button. That matters for more than completeness: if a
    // RELEASE edge were ever lost, the sequencer would still believe the button is held
    // and fire a long press 3 s later -- which on the T1000-E is power-off. This resyncs
    // within one sample.
    if ((uint32_t)(now - _lastReadTime) >= BUTTON_READ_INTERVAL_MS) {
        _lastReadTime = now;

        // The ISR is the OTHER producer of this queue, so the two must take turns.
        // pushEdge() writes a multi-word slot and then republishes _produced_level; an
        // interrupt landing in the middle of that overwrites one of the two edges --
        // precisely the silent-input-loss bug this whole change exists to remove. Mask
        // across the entire read-check-push, not just the push: deciding on a level
        // sampled before the mask can queue an edge the pin has already moved past.
        //
        // The window is a digitalRead plus a few stores (~1 us at 64 MHz), far short of
        // anything the SoftDevice notices, and it is skipped outright when no interrupt
        // is attached -- which is every non-t1000-e env, where the poll is the only
        // producer and no masking is needed.
        const bool mask = (_irq_slot >= 0);
        if (mask) noInterrupts();
        bool level = readButton();
        if (level != _produced_level) {
            _raw_poll++;
            pushEdge(now, level);
        }
        if (mask) interrupts();
    }

    // Consumer. Drain everything captured since the last call, at the timestamps the
    // edges actually carried -- this is what makes a late loop cost latency instead of
    // dropped presses.
    //
    // Tick to each edge's own timestamp BEFORE feeding it. An edge deferred by the
    // chatter window happened before this one did, so it has to be applied first or the
    // two arrive out of order.
    Edge e;
    while (popEdge(e)) {
        drainSequencer(e.ms);
        if (_seq.onEdge(e.ms, e.pressed) == ButtonSequencer::EV_PRESS_EDGE) {
            triggerEvent(ANY_PRESS);
        }
    }

    // Resolve anything whose window has now closed.
    drainSequencer(now);

    // SAFELANE §6: a dropped edge is lost user input. Report it once per occurrence
    // rather than letting a miscounted press look like a press that never happened.
    uint32_t dropped = _dropped;
    if (dropped != _dropped_reported) {
        MESH_DEBUG_PRINTLN("[btn] WARNING dropped %d edge(s) -- queue overflow",
                           (int)(dropped - _dropped_reported));
        _dropped_reported = dropped;
    }
}

void Button::triggerEvent(EventType event) {
    _lastEvent = event;

    switch (event) {
        case ANY_PRESS:
            if (_onAnyPress) _onAnyPress();
            break;
        case SHORT_PRESS:
            if (_onShortPress) _onShortPress();
            break;
        case DOUBLE_PRESS:
            if (_onDoublePress) _onDoublePress();
            break;
        case TRIPLE_PRESS:
            if (_onTriplePress) _onTriplePress();
            break;
        case QUADRUPLE_PRESS:
            if (_onQuadruplePress) _onQuadruplePress();
            break;
        case LONG_PRESS:
            if (_onLongPress) _onLongPress();
            break;
        default:
            break;
    }
}
