Mushihime-Sama Futari Black Label - native PC recompilation
=============================================================

This is a static recompilation of the arcade game for Windows: the game's own SH-3 program was
translated ahead of time into native x86-64 code, and the CV1000 board around it (graphics blitter,
YMZ770 sound, timers, NAND) is reproduced in software. Timing follows MAME 0.289 frame for frame,
including the original slowdown.

No game data is included. You need your own copy of the MAME "futaribl" ROM set.

Source code and license: https://github.com/Gandalf4212/mushihimesama-futari-black-label-recomp
License: GPL-2.0-or-later (see LICENSE). The YMZ770 decoder is from MAME (BSD-3-Clause).


First start
-----------
Put one of these next to futaribl.exe, then start it:
  futaribl.zip   or   futaribl.7z   or   a folder named "futaribl" with the ROM files
(also found inside a "roms" folder). If none is there, the program asks you to pick one.

A ROM check screen shows each file's CRC32 and GOOD / BAD. Press Enter (or A on a pad) to go on.
The program then keeps what it needs in the "assets" folder; the ROM set is not needed after that.
assets also holds your settings and high scores. Delete it to import the ROMs again.

7z needs Windows' own tar.exe (Windows 11, recent Windows 10). If a 7z is refused, use a zip.


Controls
--------
             Player 1                         Player 2
Move         Arrow keys                       R F D G
Buttons 1-4  Z X C V  (or LCtrl LAlt Space    A S Q W
             LShift, MAME's defaults)
Start        1                                2
Coin         5                                6

Gamepad: d-pad or left stick, A B X Y = buttons 1-4, Start = start, Back = coin.
Two pads: the first is player 1, the second player 2.

All of these can be changed in F1 > CONTROLS...: pick an action and press the new key (or pad button
for the PAD rows). Each action keeps up to two keys; Delete (or X on a pad) clears one, RESET TO
DEFAULTS restores the list above. Saved in config.toml under [input].

F1           settings menu (gamepad: Guide, or Back + Start). The game pauses while it is open.
F10          performance overlay
F11          fullscreen on/off
F2 / 9       test switch / service
Esc          quit

Free play is on by default: Start inserts a credit by itself. Turn it off in the menu (or run with
--coins) to use coins.


Settings (F1 menu, saved in config.toml)
----------------------------------------
ORIENTATION      YOKO (horizontal screen), TATE CW / TATE CCW (screen turned on its side)
WINDOW SCALE     window size, AUTO or 1X-8X
SCALING          FIT (largest picture that keeps the 3:4 shape) or INTEGER (whole multiples only)
SHADER           NONE, SHARP (clean pixels at any size), CRT, CRT CURVED
BEZEL            OFF, OURS (built-in), or ARTWORK VIEW n when an artwork pack is present (Yoko only)
FULLSCREEN       uses your desktop resolution (1080p, 1440p, 4K all work)
VSYNC            tear-free presentation
UNCAPPED FPS     benchmark: draws as fast as possible; the game itself still runs at 60.024 Hz
GAME SPEED       NATIVE 60.024 HZ (the board's rate), or DISPLAY REFRESH (one frame per refresh
                 on a 60 Hz display, like MAME's -syncrefresh)
SLOWDOWN         ON = original arcade slowdown
FREE PLAY, VOLUME, ROTATE CONTROLS (for Tate), PERFORMANCE HUD, CONTROLS... (key / pad remapping)

The overlay (F10) shows game and present FPS, CPU time per game frame (EMU), CAP = game frames per
second your CPU could run, and PRES = time spent drawing and presenting (with vsync on this includes
waiting for the display; a true GPU % is not available).

Shaders need Vulkan. Without it the game still runs, with plain pixels (the overlay says BASIC).


Bezel artwork (optional, not included)
--------------------------------------
MAME artwork packs work: put the pack's files in a folder named "Bezel" next to futaribl.exe with its
layout file named default.lay (or set  artwork = "path\to\file.lay"  in config.toml). Then pick
BEZEL > ARTWORK VIEW n in the menu. A single PNG/JPG with a transparent screen area also works as
artwork = "file.png". If a pack's layout names an image file the pack does not contain, rename the
image to the name in the layout.

Tested with the "Package Overlays - Cave Shoot (Part 1)" pack from the LaunchBox forums:
  https://forums.launchbox-app.com/files/file/2263-package-overlays-cave-shoot-part-1/
It has no Black Label set; its Mushihime-Sama Futari Ver 1.5 artwork fits well. From that game's folder
copy these four files into "Bezel":
  default.lay, vert_screen_bezel.png, vert_screen_mask.png, 04.png
and rename 04.png to art1.png (the layout asks for art1.png). Use ARTWORK VIEW 1 for the full frame.


Command line
------------
--yoko --tate-cw --tate-ccw --scale N --fullscreen --no-vsync --sync-display --uncapped --integer
--shader none|sharp|crt|crt-curved --no-bezel --artwork FILE --no-slowdown --coins --freeplay
--rotate-controls --volume 0-100 --no-sound --hud --config FILE
