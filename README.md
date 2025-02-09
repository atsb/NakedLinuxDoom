# NakedLinuxDoom
Linux Doom converted to SDL2, nothing more, nothing less.

The code itself is from sdldoom 1.10, which was Linux Doom converted to SDL1.

Only things that are changed:

1. Conversion of video code to SDL2
2. Conversion of audio code to SDL2 (no music yet)
3. 64bit conversion
4. Removal of i_net functions (they are useless today)
5. Change of typedef from boolean to dboolean