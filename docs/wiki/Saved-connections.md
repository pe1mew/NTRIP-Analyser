# Saved connections

Pro keeps up to **sixteen** connections, and each one is a whole
connection — caster, port, mountpoint, credentials, position and
ephemeris settings — because the expensive part of setting one up is
never the mountpoint name.

## Switching

Tap the connection tile on the main screen. The list shows every saved
connection; tap one to make it current, **Edit** to change it, **Delete**
to remove it, **Add connection** for a new one.

Give them names you will recognise later — the tile shows the name if
there is one, and the mountpoint if there is not.

## The configuration file

**☰ → Load configuration** and **Save configuration** read and write the
same JSON file that the desktop tools use, so a set of connections moves
between a laptop and a phone unchanged.

A file holds a list, even when there is one connection in it. What each
program does with a list differs:

| | |
|---|---|
| This app (pro) | **Merges** the file into your saved connections, updating any it already has rather than duplicating them |
| The CLI and the Windows GUI | Use the **first** entry and say how many they ignored |
| The monitoring daemon | Uses them all |

Loading tells you what happened — how many arrived, which one is now
current, and how many were dropped if the file held more than this
edition saves.

### The passwords in that file are in the clear

Saving says so, in those words. The format is a plain-text exchange
format by necessity: the desktop tools and the daemon read the same
file, and there is no shared key between a phone and a server.

**Treat an exported configuration as a password file.** Do not put it in
a repository, a shared drive, or a support e-mail without stripping the
credentials first.

On the phone itself the credentials are stored encrypted, with the key
held in the Android Keystore. Export is the moment they become plain,
and it is a deliberate act you take rather than something the app does
in the background.

## Editing safely

Mountpoint names are case-sensitive and exact. Where the caster
publishes a sourcetable, **Browse mountpoints…** and tapping the entry
you want is quicker and cannot be mistyped — it brings the mountpoint's
published position and its NMEA setting with it.
