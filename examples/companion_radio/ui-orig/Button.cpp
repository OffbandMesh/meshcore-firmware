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

// Producer side. Safe to call from an ISR: it only advances _q_head, which no other
// context writes.
void Button::pushEdge(uint32_t ms, bool pressed) {
    uint8_t head = _q_head;
    uint8_t next = (uint8_t)((head + 1) % BUTTON_EDGE_QUEUE_LEN);
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
    _q_tail = (uint8_t)((tail + 1) % BUTTON_EDGE_QUEUE_LEN);
    return true;
}

void Button::captureEdgeFromISR() {
#ifdef BUTTON_IRQ_CAPTURE
    // CHANGE gives no direction, so read the level. Contact bounce can make this
    // disagree with the edge that actually fired; that is fine -- the sequencer
    // debounces on timestamps and ignores anything that is not a transition.
    bool pressed = (digitalRead(_pin) == _activeState);
    if (pressed == _produced_level) return;   // nothing new to record
    pushEdge(millis(), pressed);
#endif
}

void Button::update() {
    uint32_t now = millis();

    // Polled producer. Under BUTTON_IRQ_CAPTURE this is a backstop: the ISR has usually
    // already published the edge and _produced_level matches, so this adds nothing. It
    // still runs, so a missed or unattachable interrupt degrades to the old sampling
    // behaviour instead of a dead button.
    if ((uint32_t)(now - _lastReadTime) >= BUTTON_READ_INTERVAL_MS) {
        _lastReadTime = now;
        bool level = readButton();
        if (level != _produced_level) {
            pushEdge(now, level);
        }
    }

    // Consumer. Drain everything captured since the last call, at the timestamps the
    // edges actually carried -- this is what makes a late loop cost latency instead of
    // dropped presses.
    Edge e;
    while (popEdge(e)) {
        if (_seq.onEdge(e.ms, e.pressed) == ButtonSequencer::EV_PRESS_EDGE) {
            triggerEvent(ANY_PRESS);
        }
    }

    // Resolve gestures whose window has now closed. Drain in a loop: one call can only
    // return a single event, and a long press followed by a pending count could produce
    // two.
    for (;;) {
        ButtonSequencer::Event ev = _seq.tick(now);
        if (ev == ButtonSequencer::EV_NONE) break;
        switch (ev) {
            case ButtonSequencer::EV_SHORT:     triggerEvent(SHORT_PRESS); break;
            case ButtonSequencer::EV_DOUBLE:    triggerEvent(DOUBLE_PRESS); break;
            case ButtonSequencer::EV_TRIPLE:    triggerEvent(TRIPLE_PRESS); break;
            case ButtonSequencer::EV_QUADRUPLE: triggerEvent(QUADRUPLE_PRESS); break;
            case ButtonSequencer::EV_LONG:      triggerEvent(LONG_PRESS); break;
            default: break;
        }
    }

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
