#pragma once

#include <Arduino.h>
#include <functional>
#include "ButtonSequencer.h"

// #527: multi-press detection reworked from polled sampling to EDGE CAPTURE.
//
// The public API below is unchanged -- UITask registers the same callbacks and calls
// update() from the same place. What changed is underneath: presses are now recorded
// with the timestamp at which they physically happened, and interpreted separately (see
// ButtonSequencer.h). The old code could only see a press if update() happened to sample
// while the contact was closed, so a fast triple-press on a busy loop was silently
// delivered as a single.
//
// BUTTON_IRQ_CAPTURE enables a pin interrupt as the edge producer. Where it is off, the
// poll loop produces the same edges at sampling resolution -- the state machine is
// identical either way, so behaviour degrades to "may miss a very fast tap" rather than
// changing shape. It is currently enabled for t1000-e only; every other env keeps the
// polled producer.

// Timing constants (debounce / click timeout / long press) live in ButtonSequencer.h so
// the state machine can be unit-tested without Arduino, and are re-exported here by
// inclusion for existing callers.

#ifndef BUTTON_READ_INTERVAL_MS
#define BUTTON_READ_INTERVAL_MS    10      // how often the polled producer samples
#endif
#ifndef BUTTON_EDGE_QUEUE_LEN
#define BUTTON_EDGE_QUEUE_LEN      16      // captured edges buffered between update() calls
#endif

class Button {
public:
    enum EventType {
        NONE,
        SHORT_PRESS,
        DOUBLE_PRESS,
        TRIPLE_PRESS,
        QUADRUPLE_PRESS,
        LONG_PRESS,
        ANY_PRESS
    };

    using EventCallback = std::function<void()>;

    Button(uint8_t pin, bool activeState = LOW);
    Button(uint8_t pin, bool activeState, bool isAnalog, uint16_t analogThreshold = 20);

    void begin();
    void update();

    // Set callbacks for different events
    void onShortPress(EventCallback callback) { _onShortPress = callback; }
    void onDoublePress(EventCallback callback) { _onDoublePress = callback; }
    void onTriplePress(EventCallback callback) { _onTriplePress = callback; }
    void onQuadruplePress(EventCallback callback) { _onQuadruplePress = callback; }
    void onLongPress(EventCallback callback) { _onLongPress = callback; }
    void onAnyPress(EventCallback callback) { _onAnyPress = callback; }

    // State getters
    bool isPressed() const { return _seq.isDown(); }
    EventType getLastEvent() const { return _lastEvent; }

    // #527 / SAFELANE §6: input loss must be VISIBLE, never silent. Non-zero means edges
    // arrived faster than update() drained them and were discarded.
    uint32_t droppedEdges() const { return _dropped; }

    // Called from the pin ISR. Public only so the static trampolines can reach it.
    void captureEdgeFromISR();

private:
    struct Edge { uint32_t ms; bool pressed; };

    uint8_t _pin;
    bool _activeState;
    bool _isAnalog;
    uint16_t _analogThreshold;

    ButtonSequencer _seq;
    EventType _lastEvent = NONE;

    // Single-producer / single-consumer queue: the ISR (or the poll sampler) writes,
    // update() reads. Indices are volatile and only ever advanced by their own side.
    volatile Edge     _queue[BUTTON_EDGE_QUEUE_LEN];
    volatile uint8_t  _q_head = 0;      // producer
    volatile uint8_t  _q_tail = 0;      // consumer
    volatile uint32_t _dropped = 0;
    volatile bool     _produced_level = false;   // last level PUSHED, by either producer

    uint32_t _lastReadTime = 0;
    uint32_t _dropped_reported = 0;
    int8_t   _irq_slot = -1;

    // Callbacks
    EventCallback _onShortPress = nullptr;
    EventCallback _onDoublePress = nullptr;
    EventCallback _onTriplePress = nullptr;
    EventCallback _onQuadruplePress = nullptr;
    EventCallback _onLongPress = nullptr;
    EventCallback _onAnyPress = nullptr;

    bool readButton() const;
    void pushEdge(uint32_t ms, bool pressed);
    bool popEdge(Edge& out);
    void drainSequencer(uint32_t at);
    void triggerEvent(EventType event);
};
