#include "Arduino.h"
#ifdef PIN_BUZZER
#include "buzzer.h"

void genericBuzzer::begin() {
//    Serial.print("DBG: Setting up buzzer on pin ");
//    Serial.println(PIN_BUZZER);
    #ifdef PIN_BUZZER_EN
      pinMode(PIN_BUZZER_EN, OUTPUT);
      digitalWrite(PIN_BUZZER_EN, HIGH);
    #endif

    quiet(false);
    pinMode(PIN_BUZZER, OUTPUT);
    digitalWrite(PIN_BUZZER, LOW); // need to pull low by default to avoid extreme power draw
}

offband::TonePlay genericBuzzer::play(const char *melody, offband::TonePriority priority) {
    // #1417: a Preview must not silence an Event that is already playing. Decided by
    // the pure rule in BuzzerPriority.h so the ordering is host-tested.
    if (!offband::toneTakesBuzzer(isPlaying(), _playing, priority)) {
        return offband::TonePlay::RefusedBusy;
    }

    // The stop stays AHEAD of the quiet check, which is where it has always been. It
    // means a request arriving while quiet still stops a tune that is still sounding.
    // That is pre-existing behavior on every board, not a consequence of priorities,
    // and moving it would change what ~60 variants do -- so it is left alone here and
    // tracked on its own issue instead.
    if (isPlaying())   // interrupt existing
    {
        rtttl::stop();
    }

    if (_is_quiet) return offband::TonePlay::RefusedQuiet;

    _playing = priority;
    rtttl::begin(PIN_BUZZER,melody);
//    Serial.print("DBG: Playing melody - isQuiet: ");
//    Serial.println(isQuiet());
    return offband::TonePlay::Played;
}

bool genericBuzzer::isPlaying() {
    return rtttl::isPlaying();
}

void genericBuzzer::loop() {
    if (!rtttl::done()) rtttl::play();
}

void genericBuzzer::startup() {
    play(startup_song);
}

void genericBuzzer::shutdown() {
    play(shutdown_song);
}

void genericBuzzer::quiet(bool buzzer_state) {
    _is_quiet = buzzer_state;
#ifdef PIN_BUZZER_EN
    if (_is_quiet) {
      digitalWrite(PIN_BUZZER_EN, LOW);
    } else {
      digitalWrite(PIN_BUZZER_EN, HIGH);
    }
#endif
}

bool genericBuzzer::isQuiet() {
    return _is_quiet;
}

#endif  // ifdef PIN_BUZZER