# Archived

This repository is now archived. `sdautolock` has the problem that it
doesn't know anything about lock inhibition logic on X11, so it would
lock when watching a video or being in a video call if not disabled
explicitly. I am now using `xss-lock` with a script similar to the
`dim-screen.sh` script that it is shipped with. I recommend this setup.

If you have the same problem I had where the screen would lock without the
notifier command being run, check that the DPMS doesn't get triggered
before the screen saver timeout (using `xset q`). Also check the notes
section of the manual.

# sdautolock

This is a simple program that locks the screen based on systemd
`PrepareForSleep` and `Lock` events.  Unlike `xss-lock` and `xautolock`,
it knows nothing about Xorg.  It is connected only to the systemd-logind
dbus and relies on an external utility like `xprintidle` for idle time.

## Motive

The other aforementioned lockers weren't working exactly how I wanted them
to. `xss-lock` locks when DPMS is entered. I want my screen to blank/enter
power saving mode before locking.  `xautolock` does not inhibit sleep,
so for a brief moment the screen is *unlocked* when the computer wakes up,
which I also find problematic. This program solves both of these problems.

## Installation

To install, run:

    make install

or if you prefer to install locally, you'll probably do:

    make install PREFIX=$HOME/.local

### Dependencies

 * libsystemd -- provided by `systemd-libs` on Arch Linux in the community repo.
 * xprintidle (optional) -- use to get idle time in an Xorg session.
 * i3lock (optional) -- use to actually lock the screen.

## Example

To lock the screen with `i3lock`, using its ignore empty password
option, after 3 minutes (180 seconds) use:

    sdautolock xprintidle 180 i3lock -e

See the manual for explanation.

## Status

This project pretty much done. Only security updates and bug fixes from
this point forward. (Or maybe a *really* appealing feature request.)

## Licensing

Licensed under GPLv3.  See LICENSE.
