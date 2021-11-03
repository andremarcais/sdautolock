# sdautolock

This is a simple program that locks the screen based on systemd
`PrepareForSleep` and `Lock` events.  Unlike `xss-lock` and `xautolock`, it
knows nothing about Xorg.  It is connected only to the systemd-logind dbus and
relies on an external utility like `xprintidle` for idle time.

## Motive

The other aforementioned lockers don't work right in my mind. `xss-lock` locks
when DPMS is entered, which I find annoying since I want my screen to
blank/enter power saving mode before locking.  `xautolock` does not inhibit
sleep, so for a brief moment the screen is *unlocked* when the computer wakes
up, which I also find annoying. This program solves both of these problems.

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

To lock the screen with `i3lock`, using its ignore empty password option, after
3 minutes (180 seconds) use:

    sdautolock xprintidle 180 i3lock -e

See the manual for explanation.

## Status

This project pretty much done. Only security updates and bug fixes from this
point forward. (Or maybe a *really* appealing feature request.)

## Licensing

Licensed under GPLv3.  See LICENSE.
