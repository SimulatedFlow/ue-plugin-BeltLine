# BeltLine — Conveyor & Item Logistics on a Budget

**Unreal Engine 5.8 · Code Plugin · one runtime module · Win64 built and verified**

---

## Short description

Conveyor belts along splines carrying thousands of items — one draw call per item type, no actor per
item, no tick per item, with real backpressure and a measured throughput readout on screen.

---

## Long description

Every hand-built conveyor starts the same way: an actor per item, a tick per item, a mesh component
per item. It looks right at fifty items, it stutters at five hundred, and somewhere in the low
thousands the frame time goes with it. The problem is never the belt. It is that a crate on a belt was
made into an object.

**In BeltLine a crate is not an object.** It is an entry in a ring buffer on its belt — a distance and
a one-byte type index. No `AActor`, no `UObject`, no component, no `Tick`, no collision, no garbage
collection. Ten thousand items are a few hundred kilobytes of contiguous floats, one pass per belt,
and **one instanced draw call per kind of item**. The number on the stats box follows how many
*kinds* of thing are in play, never how many things.

### One pass, and backpressure falls out of it

A belt update is a single walk from the front of the queue to the back: add the step, clamp against
the item in front. That one clamp *is* the backpressure. Hold the output and the front item stops on
the end of the belt, the next stops a radius and a gap behind it, and the queue packs backwards at
exactly the spacing the items need. Nothing overlaps. Nothing is deleted to make room. Nothing
disappears. The throughput counter falls to zero and the jam counter climbs, and when you let go the
whole line pulls away again in the same single pass.

Spacing comes from the two items' own radii, so wide plates back up sooner than small ore on the same
belt at the same speed. And every jammed item carries a flag in per-instance custom data, so one
material can tint the backed-up part of a line — backpressure you can *see*, not backpressure you are
asked to believe.

### No spline evaluation at runtime

`GetTransformAtDistanceAlongSpline` walks a reparameterisation table, evaluates three curves and
builds a matrix. Calling it once per item per frame is the second reason self-built conveyors fall
over, and instancing does not fix it — that cost lands on the game thread before a single instance is
written. BeltLine bakes each belt's spline once into an evenly spaced table of positions and
rotations. An item's transform is two array lookups and a lerp. Rebaking happens when a belt is
*moved*, not when items move along it.

### Numbers, not adjectives

A Canvas statistics box — one that survives a cooked Shipping build — shows items against the budget,
instance sets (one draw call each), instance slots, belts, nodes, throughput per second, jammed items,
milliseconds split across the movement pass and the instance writes, and a **buffer growth counter**
that reads zero in steady state. That last one exists so "nothing is allocated once the buffers stand"
is something you read off the screen instead of taking on trust.

Four checks, each one line:

- `Belt.Test 10000` → ten thousand items, **instance sets: 4**.
- `Belt.Budget 2000` → the item count falls, the instance slot count does not move, buffer growth
  stays at zero.
- Block the output → the queue grows backwards, throughput goes to zero, nothing overlaps or vanishes.
- Halve the belt speed → throughput follows, measurably, within a second.

### Branching and merging, and nothing more

A **Belt Node** is a small buffer with a capacity, an optional per-type filter and a list of output
belts. Point several belts at one and it is a merge. Give one several outputs and it is a split. Give
it a filter and it is a sorter. Nodes drain in their own pass after every belt has moved, so an item
can never cross two belts in one update because of the order the actors happened to be in, and
round-robin moves on from wherever an item actually landed so a stalled branch cannot starve the rest.

That is deliberately where the routing stops. Deciding *what* should go where is your game's business.

### It runs in the editor

Drop a belt, drag its spline, and it carries items in the viewport before you press Play. The useful
question while laying out a line is "does this actually flow", and you cannot answer that from a
static spline. It is the same subsystem, the same movement pass and the same instance sets that ship.

### Nothing to author first

Four item types exist in **code**, built from engine primitives — Box, Barrel, Ore, Plate. A fresh
project can put something on a belt in one Blueprint node with no content at all. Replace them with
your own data assets whenever you like; nothing treats the four as special.

---

## What it is **not**

Said plainly, because the shelf next to this one is full of different things:

- **Not a factory game.** No recipes, no crafting, no production chains, no machines.
- **Not an inventory system.** An item type has a mesh, a size and a weight — no stack size, no
  value, no slot.
- **No physics on the items.** They do not collide, roll or push. That is the trade that buys ten
  thousand of them.
- **No network replication.**

BeltLine **transports** things and tells you, in numbers, what that cost. What a thing *is* belongs to
your item system; what it is *for* belongs to your game.

---

## Features

- Spline conveyors, draggable and reshapeable in the editor, running without PIE
- Thousands of items with **no actor, no `UObject` and no tick per item**
- **One instanced draw call per item type**, however many items are moving
- Real backpressure: minimum spacing, queues that grow backwards, no overlap, no lost items
- Belt nodes for merging, splitting and per-type sorting, with capacity and buffering
- Baked path tables — no runtime spline evaluation
- A hard world item budget; lowering it trims without freeing an instance slot
- Per-instance custom data: progress, jam flag and item colour, for one material across all types
- Canvas statistics box that survives a Shipping build, including a buffer-growth counter
- Full Blueprint surface: `Spawn Item`, `Try Take Item`, `Get Throughput`, `Set Belt Speed`,
  `Is Blocked`, budget and node controls
- Four built-in code item types from engine primitives
- Console: `Belt.Test`, `Belt.Budget`, `Belt.Clear`, `Belt.Stats`
- Project settings for budget, ceilings, update rate, path spacing and instance culling

---

## Technical details

- **Modules:** one runtime module `BeltLine`, loading phase `PreDefault`
- **Dependencies:** `Core`, `CoreUObject`, `Engine`, `DeveloperSettings`, `RenderCore` (private).
  No UMG, no Niagara, no Chaos, no editor module.
- **Classes:** `ABeltActor`, `ABeltNode`, `UBeltItemType`, `UBeltSubsystem`, `UBeltStatics`,
  `UBeltSettings`, `ABeltHUD`
- **Supported engine:** 5.8
- **Platforms:** Win64 built and verified. Mac and Linux enabled but not built — the plugin contains
  no platform-specific code.
- **Network replicated:** No
- **Documentation:** included (`Docs/DOCUMENTATION.md`), plus a README with a quick start
- **Number of Blueprints:** 0 (code plugin)
- **Number of C++ classes:** 7
