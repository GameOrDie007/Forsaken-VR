Forsaken VR for Quest and Steam Frame
=====================================

Forsaken (1998) on your Meta Quest or Valve Steam Frame, standalone: no PC
while you play.

Before you start: Developer mode (once)
---------------------------------------
Apps from outside the Meta store need Developer mode on the headset. It is
free and takes a minute:

1. On your phone, open the Meta Horizon app (the one you set the headset up
   with).
2. Menu, Devices, choose your headset, then Headset settings, Developer mode,
   and turn it on. If the app asks you to make a developer account or an
   "organization" first, it is free; follow its steps.
3. Restart the headset.

Install
-------
1. Unzip this folder anywhere on your PC.
2. Turn the headset on and plug it into the PC with a USB-C cable.
3. Double-click Setup.bat.
4. Put the headset on when Setup asks, and choose Allow (tick "Always allow
   from this computer").

Setup puts the game together on your PC, then installs the app on the
headset and copies the game to it:

- It downloads the game data (about 130 MB) from the ForsakenX project's own
  repository. If you already have a copy of that data set, drag its folder
  onto Setup.bat instead.
- If you own Forsaken Remastered (Steam or GOG), it takes the soundtrack and
  the intro film from your copy. Both are optional; the game plays without
  them.
- The first time, it downloads adb, Google's tool for talking to the
  headset, straight from Google (about 7 MB). If SideQuest is on the PC it
  uses SideQuest's.

The game it puts together stays on your PC (in your user folder, under
AppData\Local\Game Or Die), so the next update needs no download.

Run Setup again after an update: it copies only what changed. It never
touches your pilot, saved games or settings, and it keeps a copy of them on
the PC each time (Documents\Forsaken VR\Quest saves).

Play
----
On the headset: Library, then Unknown Sources, then Forsaken VR.

The first time you start a new game it asks how you want to aim:

  Modern   your hand aims; shots leave the ship along your controller
  Classic  the ship aims, as in 1998

Change it any time in the pause menu under VR Options.

Controls (the face buttons and stick clicks can be changed in VR Options,
Controls)
  Left stick          thrust and strafe
  Right stick         yaw and pitch
  Right grip          rise
  Left grip           sink
  Both grips          recenter
  Right trigger       fire primary weapon
  Left trigger        fire secondary weapon
  X / Y               roll left / right
  A                   nitro
  B                   drop a mine
  Left stick click    next primary weapon
  Right stick click   pull the view back behind your ship: cockpit, near, far
  Menu button         pause menu; hold it to recenter
  In menus            either stick moves, A chooses, B goes back; or hold
                      the grip of your gun hand, point at the menu and pull
                      the trigger

Also in VR Options: Fly direction (fly where you look or point), invert pitch,
turn speed, auto-level, a left-handed mode (Leftorium), swap sticks, swivel
chair, vibration and ship bob.

Steam Frame (standalone)
------------------------
The same app runs on a Valve Steam Frame with no PC, under Lepton (the
Frame's Android layer). Setup puts it in the Frame's own library:

1. On the Frame, once: Settings, System, Enable Developer Mode; then
   Settings, Developer, Set User Password. Lepton must be installed
   (Store, search Lepton).
2. On the PC, double-click "Setup Steam Frame.bat". The first time, on the
   Frame: Settings, Developer, Pair new host, then Accept. It copies the
   app and the game over your home network and adds Forsaken VR to the
   Frame's library. Run it again after an update, as on Quest.
3. On the Frame: Library, then Forsaken VR (or double-click
   "Start on Steam Frame.bat").

"Collect Steam Frame logs.bat" gathers the logs and "Uninstall Steam
Frame.bat" takes the game off the Frame. The Frame's pilot, saves and
settings are copied to Documents\Forsaken VR\Steam Frame saves each time.

Uninstall
---------
Plug the headset in and double-click Uninstall.bat. It copies your pilot,
saved games and settings onto the PC first, then removes the app from the
headset. Setup puts them back if you install again.

Something wrong?
----------------
Plug the headset in and double-click "Collect logs.bat". It puts the logs
in one zip on your desktop. Send it to us with a line on what happened.

For tools and hubs
------------------
Every Game Or Die port installs the same way. Run Setup.bat (or
files\setup-quest.ps1 with PowerShell) from the release folder:

  Setup.bat -Quiet                      install, no questions
  Setup.bat -Quiet -GamePath "<folder>" your Forsaken Remastered folder
  Setup.bat -Quiet -DataPath "<folder>" a ForsakenX data set you already have
  Setup.bat -Detect -Json               change nothing; report as JSON
  ... -Json                             any of these prints its result as
                                        JSON on stdout and nothing else
  ... -Serial <id>                      the headset to use, if several, or a
                                        Steam Frame's name

Exit codes: 0 done, 1 no game data, 2 error, 3 no headset.

License
-------
Forsaken VR is a Game Or Die project, built on ForsakenX, the community
source port of Forsaken by Probe Entertainment and Acclaim. GPL version 2:
LICENSE. The complete source is at https://github.com/GameOrDie007/Forsaken-VR

The splash picture is (c) 2026 Game Or Die, all rights reserved, and may be
used only as part of Forsaken VR. The game data Setup downloads, and anything
it copies out of Forsaken Remastered, belong to their owners and are never
redistributed here. Other parts and their licenses are listed in
THIRD-PARTY.txt.
