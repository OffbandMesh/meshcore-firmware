#pragma once

#include <Arduino.h>
#include <NonBlockingRtttl.h>
#include "BuzzerPriority.h"

/* class abstracts underlying RTTTL library 

    Just a simple implementation to start.  At the moment use same
    melody for message and discovery
    Suggest enum type for different sounds
    - on message
    - on discovery

    TODO
    - make message ring tone configurable

*/

class genericBuzzer
{
    public:
        void begin();  // set up buzzer port
        // Generic play function. Says what happened: a Preview is refused while an Event
        // is playing, and a request while quiet is refused for a different reason, so a
        // caller can put the right words on screen instead of appearing to do nothing.
        // #1417: the default is Event, which is exactly the old unconditional behavior,
        // so existing callers are unchanged. They ignore the result, as they always did.
        offband::TonePlay play(const char *melody,
                               offband::TonePriority priority = offband::TonePriority::Event);
        void loop();  // loop driven-nonblocking
        // #1422: stops whatever is playing. Part of the trigger seam: a tune sent to a
        // hall of badges must be stoppable, and the payload is replaceable so it will
        // not always be under two seconds. Additive -- no existing caller stops.
        void stop();
        void startup();  // play startup sound
        void shutdown();  // play shutdown sound
        bool isPlaying();

    // #1475: would an incoming request take the buzzer, asked WITHOUT taking it.
    //
    // This exists because a custom tune has to be read from the filesystem into a shared
    // staging buffer before it can be played, and that read is destructive: if play()
    // then refused the request, the tune still sounding would be left walking a buffer
    // that had been overwritten underneath it. So the caller asks first, and only stages
    // when the answer is yes. Same pure rule play() uses -- queried, not duplicated.
    bool wouldTake(offband::TonePriority incoming) {
      return offband::toneTakesBuzzer(isPlaying(), _playing, incoming);
    }  // returns true if a sound is still playing else false
        void quiet(bool buzzer_state);  // enables or disables the buzzer
        bool isQuiet();  // get buzzer state on/off

    private:
        // gemini's picks:
        const char *startup_song = "Startup:d=4,o=5,b=160:16c6,16e6,8g6";
        const char *shutdown_song = "Shutdown:d=4,o=5,b=100:8g5,16e5,16c5";

        bool _is_quiet = true;

        // #1417: the priority of whatever is playing. Only read while rtttl reports a
        // melody in progress, so its value between tunes does not matter.
        offband::TonePriority _playing = offband::TonePriority::Event;
};
