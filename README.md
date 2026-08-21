# BeltLine — Conveyor & Item Logistics on a Budget

Conveyor belts along splines carrying **thousands of items**, for Unreal Engine 5.8.

No actor per item. No tick per item. No component per item.

| | What most conveyors do | What BeltLine does |
|---|---|---|
| One item is | an `AActor` with a mesh component | an entry in a ring buffer: a `float` and a `uint8` |
| Moving them | one `Tick` per item | one pass over a flat float array per belt |
| Drawing them | one draw call per item | **one draw call per item *type*** |
| Placing them | `GetTransformAtDistanceAlongSpline` per item | two array lookups into a baked path table |
| A blocked output | items overlap, or disappear | the queue grows backwards at minimum spacing |

## What it costs

- **One draw call per item type.** Ten thousand crates, one instanced set. The number on the stats
  box follows how many *kinds* of thing are in play, never how many things.
- **One pass per belt.** Add the step, clamp against the item in front. That single clamp is where
  backpressure comes from — it is not a second system bolted on top.
- **No spline evaluation at runtime.** A belt's spline is baked once into an evenly spaced table of
  positions and rotations; an item's transform is a lerp between two of them. Rebaking happens when a
  belt is moved or reshaped, not when items move along it.
- **Nothing allocated in steady state.** Ring buffers are sized from the belt's length when it is
  built; instance slots are never given back. The stats box carries a **buffer growth** counter so
  that claim is something you read, not something you take on trust.
- **A hard item budget.** Not a target — a spawn past it fails and is counted. Lower it and the item
  count falls while the draw call count and the instance slot count stay exactly where they were.

## Quick start

1. Enable the plugin.
2. Drop a **Belt** actor in the level and drag its spline into the shape you want. It already runs —
   in the editor viewport, before you press Play.
3. Put something on it:
   `Get Built In Item Type (Box)` → `Spawn Items (Belt, Type, 200)`.
4. Optional: drop a **Belt Node** where two belts meet, set the first belt's **Output Node** to it and
   add the second belt to the node's **Output Belts**. That is a merge, a split and a sorter.
5. Optional: set the belt's **Belt Mesh** to tile a mesh along the spline so the conveyor itself is
   visible. The items do not need it.

Or skip all of it and type `Belt.Test 10000` in the console.

## Classes

| Class | What it is for |
|---|---|
| `ABeltActor` | one conveyor: spline, speed, gap, allowed types, ring buffer. Draggable in the editor, runs without PIE. |
| `UBeltItemType` | a data asset: mesh, material, scale, radius, weight, colour. One asset = one instance set = one draw call. |
| `ABeltNode` | where belts meet: a small buffer, a capacity, a type filter, a list of outputs. Merge, split, sort. |
| `UBeltSubsystem` | the world subsystem: belts, nodes, item types, instance sets, budget, stats. Game, PIE **and Editor**. |
| `UBeltStatics` | the whole plugin from Blueprint. |
| `UBeltSettings` | project defaults, under Project Settings → Plugins → BeltLine. |
| `ABeltHUD` | the counters box on `UCanvas`, so it survives a Shipping build. |

Four item types exist in **code**, built from engine primitives — Box, Barrel, Ore and Plate — so a
fresh project can carry something before it has authored a single asset.

## Console

```
Belt.Test [Items] [LoopRadius]   build a closed four-belt loop in front of the camera and fill it (0 removes it)
Belt.Budget [Items]              read or set the world's item ceiling
Belt.Clear                       take every item off every belt and out of every node
Belt.Stats                       print the measured counters
```

`Belt.Test 10000` is the claim, checkable in one line: ten thousand items on screen and
**Instance sets** on the stats box still reads four.

## Backpressure, in one button

Call `Set All Outputs Blocked (true)`. The front item stops on the end of the belt, the next stops a
radius and a gap behind it, and the queue packs backwards. Nothing overlaps, nothing is deleted,
`Throughput` falls to zero and `Jammed` climbs. Release it and the whole line pulls away again.

Every jammed item carries a `1` in per-instance custom data slot 1, so a material can tint the
backed-up part of the line without the CPU touching a single material instance.

## What it is not

Not a factory game. No recipes, no crafting, no machines, no inventory, no physics on the items and
no network replication. BeltLine **transports** things and tells you, in numbers, what that cost.
What a thing *is* belongs to your item system; what it is *for* belongs to your game.

---

Requires Unreal Engine **5.8**. Built and verified on Win64; Mac and Linux are enabled but untested.
