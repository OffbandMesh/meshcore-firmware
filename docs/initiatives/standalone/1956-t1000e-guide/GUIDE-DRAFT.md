# DRAFT — Use your SenseCAP T1000-E

> Not published. Moves to `docs/user/guides/t1000e.md` (front matter
> `title: Use your SenseCAP T1000-E`, `feature: t1000e`, catalog coverage `documented`)
> when a release carries this behavior, and after the hardware checks listed under
> "Applies to" are recorded.

The SenseCAP T1000-E is a card-sized MeshCore companion radio with GPS, a buzzer, one
button and one LED, and no screen or keyboard. You use it through the Offband client on a
phone or computer. This page covers what the radio does on its own: the sounds it makes,
what its LED means, how its GPS saves power, and what to do if it goes quiet.

## Applies to

The T1000-E companion images, Bluetooth and USB (`t1000e_companion_radio_ble`,
`t1000e_companion_radio_usb`). This page describes the current development build, which
will be in the next Offband release; this page will name that release when it ships. Much
of it differs from the T1000-E image in `offband-v1.6.0-beta1`.

What has been checked, and how:

| Behavior | Checked |
|---|---|
| Start-up with a charged battery (SafeBoot) | On a T1000-E: every start read the battery at about 4.17 V and continued. |
| Sounds, send tone, tune choice from the client | Source and automated tests only. Not yet tested on a T1000-E. |
| Message-waiting LED | Source and automated tests only. Not yet tested on a T1000-E. |
| GPS power save | Source and automated tests only. Not yet tested on a T1000-E. |

## Before you start

- Charge the radio, and [download the Offband client](https://offband.app/).
- Pair the radio with the client. Every setting on this page is changed from the client;
  the radio's button only has a few fixed actions (step 3).

## Steps

### 1. Know the sounds it makes

The radio plays a tune for each kind of event. Out of the box:

| Event | Tune |
|---|---|
| A direct message arrives | Chime |
| A channel message arrives | Kerplop |
| You send a message from the client (direct or channel) | Tick |
| A message you sent is acknowledged | Tick |
| A room message, or a new contact | Silent |

A message the client sends again by itself, because the first try was not acknowledged,
does not play the send tick a second time.

You can change any of these from the client: each event, and each channel, can have its
own tune, or be silent. A channel set to silent never beeps, whatever else is set.

### 2. Choose which messages make it beep

The radio has **one notification setting for the whole radio**, separate from the
client's own notification settings:

| Setting | Channel messages | Direct messages |
|---|---|---|
| **All** (the default) | Beep | Beep |
| **Self** | Beep only when they mention you as `@[your radio's name]` | Beep |
| **None** | Silent | Silent |

Change it in the client's device settings, under the buzzer section, at **Device
notification scope**.

**This is not the per-channel All / Mentions only / Off you set on a channel in the
client.** That per-channel setting decides whether your phone or computer notifies you,
and it is not sent to the radio. Setting a channel to "Mentions only" in the client does
not quiet the radio. To quiet one channel on the radio, set that channel's tune to silent
(step 1).

### 3. Use the button

| Press | What it does |
|---|---|
| Double press | Sends an advert, so nearby radios learn about yours. |
| Triple press | Steps the notification setting All → Self → None → All, and plays a cue for the new setting: three rising notes for All, one note for Self, three falling notes for None. |
| Four presses | Turns GPS on or off. |

These are the defaults. The client can assign other actions to each press sequence.

### 4. Read the LED

The green LED blinks briefly about once a second while the radio is running. That blink
means the radio is alive, not that it is connected to anything.

While an unread message is waiting on the radio, each blink lasts longer, about half a
second instead of a tenth. It returns to the short blink once the client has collected
the waiting messages from the radio.

### 5. Save power with GPS

The T1000-E's GPS can be **Off**, **On**, or in **Power save**. Choose it in the client's
location settings.

In Power save the radio turns the GPS on to get a position, then turns it off and waits
before the next try, an hour by default (the client can change the wait, down to five
minutes). If it cannot get a position, for example
indoors, it waits longer between tries instead of searching continuously, and goes back to
its normal interval as soon as it gets a position again. You do not need to restart it
after going outside.

## Check it worked

- Send a message from the client: you hear the send tick from the radio.
- Have someone send you a direct message: you hear the direct-message tune, and the LED
  blinks longer until the client collects it.
- Change the notification setting by triple press: you hear that setting's cue.

## Troubleshooting

| What you see | What to check |
|---|---|
| It beeps for every channel message although the client is set to "Mentions only" | The client's per-channel setting does not reach the radio. Set **Device notification scope** to Self (step 2), or set the noisy channel's tune to silent (step 1). |
| No sound at all | The notification setting may be **None**: triple press until you hear the three rising notes. Check the event's tune is not set to silent in the client. |
| The radio goes quiet and won't start on a charged battery | See "Limits and recovery". |
| A factory reset or a fresh install brought the beeps back | A reset returns the notification setting to **All**, and importing a configuration exported by another app may not restore it. Set it again. |

## Limits and recovery

**The radio cannot follow the client's per-channel settings.** Per channel, the radio can
only be silent or play a tune; "beep only when mentioned" is a whole-radio setting.

**Low battery.** When the battery is too low to start safely, the radio stays off instead
of starting, and starts again once it is charged. A T1000-E has no automatic wake for
this: wake it by **connecting USB power, or pressing the button**.

**If an earlier image went dark on a charged battery.** Images up to and including
`offband-v1.6.0-beta1` could read the battery as almost 0 V at start-up and switch the
radio off with a full charge, from where only USB power or the button woke it. This is
fixed in the current build. If yours does this, reconnect USB to bring it back, then
update.

Settings survive a restart. A factory reset clears them, including the notification
setting, tunes and button assignments.
