# Idle behaviour and wake

The display used to switch itself off 5 minutes after the last serial byte, with no way to turn that off. It is now a setting.

## Setting

Settings screen (swipe down on the main screen) -> **More >** -> **Idle**:

| When idle | What happens after the chosen time (1, 2, 5, 10 or 30 minutes) |
|-----------|-----------------------------------------------------------------|
| **Never sleep** (default) | Nothing. The display stays on. |
| Dim only | Backlight drops to the "dim" level (the left handle of the backlight slider). Nothing is switched off. |
| Sleep display | Panel off, backlight 0, LED off (what the old fixed timeout did). |

Stored in Preferences (`config` namespace, keys `idle_mode`, `idle_min`). Changes apply immediately.

"Idle" means **no host connected** (desktop app or web page) **and** no touch or key press for the chosen time. A connected host keeps the
display on, as before. Any touch or physical key press restarts the timer.

## Waking

- Touch the screen, or press any physical key (Key1, Key2, Key3). Keys keep being polled while the display is off.
- The press or touch that wakes a **sleeping** display is swallowed: no widget under the finger is clicked, the paw does not tap, the
  tap counter does not move, and holding that key does not then fire a long-press mood.
- From **dim**, a touch restores brightness and still works as a normal touch (the screen was visible).
- A host connecting also wakes the display.
- The light-sensor auto-dim (`autoDim`) is paused while the idle dim or sleep is active, so it cannot undo it.

## The sleepy cat is separate

The sleepy-cat picture (`animation.cpp`, 60 s without activity) is unrelated to display sleep: it only shows or hides the cat image and
never touches the backlight or panel. Tapping a key on a sleeping display does not wake the cat either, because that press is swallowed.

## Code

`sketch/src/power.cpp` holds a small state machine (active / dimmed / asleep) driven by `power_poll()` every loop pass.
The old code re-sent the sleep commands on every pass; this one switches state once.
`sketch/src/ui_ext.cpp` builds the extra settings screen in code (not in the SquareLine project) the first time it is opened.
