# Morse Trainer

Learn Morse code on your Flipper Zero, starting from absolute zero.

I made this because I wanted to actually learn Morse instead of staring at a chart. You start with just E and T, and each level adds a few more letters. Every few levels there is a challenge round where you spell real words. By the final level you know the whole alphabet and the numbers.

## Modes

- Encoding: the app shows a letter, you key it in Morse with the OK button
- Decoding: the app beeps and vibrates a letter, you pick what you heard
- Mixed: both of the above, randomly
- Practice: free keying sandbox, whatever you key gets decoded live on screen

Each mode keeps its own level progress.

## Controls

Encoding: short press OK for a dot, hold it for a dash. A small bar fills while you hold so you can see exactly where a dot turns into a dash. Stop keying for a moment and the letter commits on its own. Left clears your attempt, Up shows the letters of the level as a hint.

Decoding: Left and Right pick an answer, OK confirms, Up replays the sound, Down removes two wrong choices. You get 2 hints per level.

Press Back twice to quit a level. Progress saves to the SD card by itself.

## Extras

- Letters you keep missing start showing up more often until you fix them
- Relaxed letter spacing option for beginners (Farnsworth style)
- Sound, vibration, LED and pattern visibility can all be toggled in settings
- Stars per level, a daily streak counter and lifetime stats
- No extra hardware needed, just the Flipper itself

## Building

Run ufbt launch in the project root.
