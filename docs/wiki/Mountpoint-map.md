# The mountpoint map

Every mountpoint a caster publishes, on one screen: **which base is near
me, and which way?** Fill in the caster and port, save, then tap **Map
mountpoints…** beside **Browse mountpoints…** on the main screen.

## What you are looking at

The map is centred on **this phone** when it has a location fix, and on
the **set position** otherwise; the header says which. Distance and
bearing from the centre are true everywhere on the screen, so the rings
mean what they say. Coastlines and borders further out stretch — the
header says that too. Pinch to zoom, drag to pan, **Recentre** to go
back.

Each mark is a mountpoint, shaped and coloured by what its sourcetable
entry says it is:

| Mark | Kind |
|---|---|
| circle | **Fixed base** — one station at one place |
| diamond | **Network service** — a VRS or nearest-base service that answers for where you are |
| triangle | **Product, no station** — an ephemeris or correction stream with no antenna behind it |
| square | **Unknown** — the entry does not say enough to tell |

The kind is the app's reading of the sourcetable, and the balloon says
why it decided that. An entry whose position cannot be used — none
given, 0,0, or out of range — is counted and **not drawn**, rather than
being put somewhere it is not.

## Tapping a mountpoint

A balloon shows its name, identifier, format, constellations, network,
its kind and the reason for it, its distance and bearing from the
centre, and whether it expects a position from you.

- **Pro**: **Select mountpoint** makes it the connection — with its
  published position and its position-reporting setting, exactly as
  picking it from the list does.
- **Free**: the balloon says what the paid edition adds. Type the name
  into the connection settings to use it.

Nothing is fetched to draw the map: the outlines come with the app
(made with Natural Earth). The mountpoint list is fetched from the
caster once, when the map opens.

## The list, sorted

**Browse mountpoints…** shows the same entries as a list, now with each
one's distance and bearing, measured from the same centre the map uses.
Sort it **Nearest** first (the default), **A–Z** or **Z–A**; the filter
narrows what is sorted. Entries without a usable position say so and
come last. The order you pick is kept while the app runs.

## When the list does not come

If the caster cannot be reached, refuses, or goes quiet, the map and
the list say so in the same sentences as a failed connection — see
[Troubleshooting](Troubleshooting). A list cut short is asked for once
more, and never shown as if it were the caster's whole list: a caster
that stopped sending after twelve of its six thousand mountpoints would
otherwise look like a caster with twelve.
