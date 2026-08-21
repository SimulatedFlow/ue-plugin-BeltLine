# BeltLine — Documentation

Conveyor and item logistics for Unreal Engine 5.8.
Plugin version 1.0.0 · one runtime module, `BeltLine`, loading phase `PreDefault`.

---

## 1. What the plugin is

A belt is a spline and a ring buffer. An item is an entry in that ring buffer: a distance along the
belt and a one-byte index into the world's list of item types. That is the whole data model, and
almost everything else in this document follows from it.

An item is **not** an `AActor`, **not** a `UObject` and **not** a component. It has no `Tick`, no
transform component, no collision, no lifetime and no GC cost. Ten thousand items on belts are a few
hundred kilobytes of `float` and `uint8`, one pass per belt over contiguous memory, and one instanced
draw call per *kind* of item.

### 1.1 What it is not

Stated plainly, because the neighbouring products on the store are different things:

- **Not a factory game.** No recipes, no crafting, no machines, no production chains.
- **Not an inventory system.** An item type carries a mesh, a size and a weight. It has no stack
  size, no value and no slot.
- **No physics on the items.** They do not collide, roll, fall or push. That is the trade that buys
  ten thousand of them.
- **No network replication.** Belts are deterministic given the same speeds and the same spawns; if a
  project needs authoritative item flow, it replicates its own spawn events, not this plugin's state.

---

## 2. The model

### 2.1 The ring buffer

Each `ABeltActor` holds three parallel arrays, sized once when the belt is built:

```
TArray<float> ItemDistance;   // cm along the belt, sorted descending from RingHead
TArray<uint8> ItemType;       // index into the subsystem's item type table
TArray<uint8> ItemJammed;     // 1 while the item could not take its full step
```

Three arrays rather than an array of structs, because the movement pass touches only the distances.
Contiguous floats mean one cache line per sixteen items instead of one cache line per item; that
difference is most of why this scales.

Items join at the back (the belt's entry) and leave at the front (the belt's end), so neither end
costs a shuffle. Because a belt cannot overtake itself, the entries stay sorted by distance for free —
there is no sort anywhere in the plugin.

The buffer's capacity is `ceil(Length / MinItemPitch) + 2`, capped by `MaxItemsPerBelt`. It is
allocated when the belt is constructed and never resized while the belt runs.

### 2.2 The movement pass

One walk from the front of the queue to the back:

```
LimitBase = output can accept ? Length + Advance : Length
for each item, front to back:
    Limit  = LimitBase - (this item's radius, except for the front item)
    D      = min(D + Advance, Limit)          // and mark it jammed if it was clamped
    LimitBase = D - radius - MinGap
```

One addition, one comparison and one subtraction per item. `Advance` is `Speed × DeltaSeconds`,
computed once for the whole belt.

**Backpressure is that one clamp.** Nothing else implements it. Hold the output and the front item
stops on the end; the next stops a radius and a gap behind it; the queue packs backwards at exactly
the spacing the items need. Nothing overlaps, nothing is deleted to make room, and releasing the hold
lets the whole queue pull away in the same single pass. Spacing between two neighbours is
`RadiusA + MinGap + RadiusB`, so wide items back up sooner than narrow ones on the same belt at the
same speed.

### 2.3 Hand-offs

Whether the output will accept is asked **once**, before the pass, so every item in the queue is
clamped against the same answer. If that prediction turns out to be optimistic — a node filled up
between the question and the hand-off — the drain step parks the front item on the end and re-clamps
the queue behind it, stopping at the first item that already fits. That correction is an early-out
walk, not a second full pass, and in steady state it does not run at all.

A belt's end goes to, in order of precedence:

1. its **Output Node**, if one is set;
2. its **Output Belt**, if one is set;
3. **nothing** — the item is consumed and counted as delivered.

The third case is deliberate, not a missing branch. A belt that ends in nothing is a sink: the mouth
of a furnace, the edge of a level, the ship that took the cargo away.

### 2.4 Nodes

`ABeltNode` is a ring of type indices with a capacity, an optional type filter and a list of output
belts. Point several belts at one node and it is a merge; give one node several outputs and it is a
split; give it a filter and it is a sorter.

The buffer is what makes a merge fair. Without it, a merge would either drop items or let whichever
belt updated first win every time. With it, an arriving item is accepted only if there is a slot, so a
belt feeding a full node backs up like it should.

Nodes drain in **their own pass, after every belt has moved**. That is the reason an item can never
cross a node and half of the next belt inside one update just because the actors happened to be
registered in that order. The update order of belts is stable, but nothing depends on it.

Round-robin moves its cursor on from wherever an item actually landed, not from where it started
looking, so a stalled output cannot starve the others and a running output cannot hog the node.

### 2.5 The baked path

`USplineComponent::GetTransformAtDistanceAlongSpline` walks a reparameterisation table to turn a
distance into a spline key, evaluates three interpolated curves and builds a matrix. Calling it once
per item per frame is what makes hand-built conveyors fall over in the low thousands — and no amount
of instancing fixes it, because the cost lands on the game thread before a single instance is written.

So the spline is sampled once, at a fixed spacing, into `FBeltPath`:

```
TArray<FVector> Positions;
TArray<FQuat>   Rotations;
```

An item's transform becomes: divide by the spacing, floor, lerp, quaternion-lerp, normalise. Two
array lookups.

Two details that matter:

- The spacing is widened slightly so the samples divide the belt's length *exactly*. Otherwise the
  last segment would be a stub of arbitrary length and the uniform-spacing index arithmetic — which
  is what makes the lookup O(1) instead of a search — would drift over it.
- Neighbouring quaternions can come back on opposite hemispheres while describing nearly the same
  orientation. `FastLerp` between those two takes the long way round and an item flips end over end
  for one sample, so the table is sign-corrected once at bake time.

A hundred metres of belt at the default 50 cm spacing is 201 samples, about 11 kB, held for the life
of the belt. It does **not** grow with the number of items riding on it.

The samples are in world space, so moving a belt actor costs a rebake. The subsystem watches for that
with a transform compare per belt per update — a handful of float compares — and rebakes only when a
belt has actually been dragged.

### 2.6 The instance sets

One `UInstancedStaticMeshComponent` per registered item type, all hanging off one transient actor.
Every update:

1. Every batch's scratch arrays are `Reset()` — which keeps the allocation and moves a counter.
2. Every belt is walked and each item's transform is appended to its type's scratch array.
3. Each batch does **two** calls: `BatchUpdateInstancesTransforms` and `SetCustomData`, both with
   `bMarkRenderStateDirty = false`.

Leaving the render state alone is the point. The instance data manager streams the deltas to the GPU
scene by itself; marking the state dirty would recreate the entire primitive proxy every frame.

**Plain ISM, not HISM.** A hierarchical component rebuilds its cluster tree whenever an instance's
*translation* changes, and here every instance moves every frame — that rebuild is precisely the cost
this plugin exists to remove. GPU-scene per-instance culling does the job without a tree.

Slots that had an item last update and do not now are **collapsed**, not removed:
`RemoveInstance` swaps the last instance into the hole, so every index above it moves, and the
instance count would rise and fall with the item count. Collapsing to a ~1e-4 scale keeps the slot
count at its high-water mark, which is what makes lowering the budget cost nothing.

Per-instance custom data, five floats:

| Slot | Meaning |
|---|---|
| 0 | progress along the belt, 0..1 |
| 1 | **jam flag** — 1 while the item is being held up |
| 2, 3, 4 | the item type's colour |

Slot 1 is the useful one: a material can tint the backed-up part of a line without the CPU touching a
material instance or a component, which is how backpressure becomes something you can *see* rather
than a claim in a description.

---

## 3. Setting it up

### 3.1 A belt

Drop a **Belt** actor and drag its spline. Useful properties:

| Property | Meaning |
|---|---|
| `Speed` | cm per second. Negative is clamped to zero, not reversed. |
| `MinGap` | clear space between two neighbours' *edges*, in cm. |
| `EntryDistance` | where arriving items are placed. Usually 0. |
| `bOutputBlocked` | hold the belt at its end. This is the backpressure switch. |
| `AllowedTypes` | empty means anything. A refused type backs traffic up; it does not vanish. |
| `MaxItemWeight` | 0 means no limit. |
| `OutputNode` / `OutputBelt` | where the end goes. Neither set = a sink. |
| `BeltMesh` / `BeltMaterial` / `BeltMeshLength` / `BeltMeshScale` | optional scenery tiled along the spline. |
| `ItemHeightOffset` | how far above the spline the items ride. Add the belt surface's thickness. |

The belt runs in the **editor viewport**, before Play. That is on purpose: the useful question while
laying out a line is "does this actually flow", and you cannot answer it from a static spline. Turn it
off with `Project Settings → Plugins → BeltLine → Tick In Editor Worlds`.

### 3.2 An item type

`UBeltItemType` is a data asset. `Radius` is half the room the item needs *along the direction of
travel* — it is what makes a queue a queue, and it is not a collision shape. `MeshOffset.Z` should
usually be half the mesh's height so the item sits on the belt rather than through it.

Four types exist in **code**, from engine primitives, so a fresh project can carry something with no
content at all:

| Shape | Mesh | Size along travel | Weight |
|---|---|---|---|
| `Box` | Cube | 60 cm (radius 32) | 25 kg |
| `Barrel` | Cylinder | 60 cm wide, 80 cm tall (radius 32) | 60 kg |
| `Ore` | Sphere | 36 cm (radius 20) | 12 kg |
| `Plate` | Cube, flattened | 110 cm (radius 58) | 40 kg |

Get one with `Get Built In Item Type`. They are built on first use and kept for the life of the
world; a real project replaces them with its own assets and nothing treats these four as special.

### 3.3 A node

Set `Capacity` small — a node is a junction, not a warehouse. Eight is plenty to keep a merge from
stuttering, and a large buffer only moves the point at which the queue becomes visible, which is the
opposite of what a conveyor should do.

`bSplitEnabled = false` keeps the full output list but feeds only the first, so the branch drains and
the main line takes the whole flow. Turning it back on refills the branch. Nothing is spawned or
destroyed either way.

---

## 4. Blueprint surface

All of it is on `UBeltStatics`, and all of it goes through the same subsystem the C++ side uses.
There is no second, simplified implementation behind these calls.

**Items**

| Node | What it does |
|---|---|
| `Spawn Item` | one item at the belt's entry. False = budget full, type refused, or entry occupied. |
| `Spawn Items` | fill the free stretch of one belt. Returns how many went on. |
| `Spawn Items Across Belts` | spread items over every belt in the world. This is what a "+500 items" button calls. |
| `Try Take Item` | take the item nearest the end off a belt. The other half of a conveyor. |
| `Clear All Items` | empty every belt and node. Frees nothing; nothing regrows afterwards. |
| `Get Built In Item Type` | Box / Barrel / Ore / Plate. |

**Belts**

`Set Belt Speed`, `Get Belt Speed`, `Set All Belt Speeds`, `Scale All Belt Speeds`,
`Get Average Belt Speed`, `Set Output Blocked`, `Set All Outputs Blocked`, `Is Blocked`,
`Get Belt Item Count`, `Get Belt Jammed Count`, `Get All Belts`.

**Nodes**

`Set Node Split Enabled`, `Set All Nodes Split Enabled`, `Set Node Blocked`.

**Numbers**

`Get Throughput`, `Get Belt Throughput`, `Get Item Count`, `Set Item Budget`, `Get Item Budget`,
`Get Belt Stats`.

### 4.1 `Is Blocked` means what it says

It is true when the queue is **actually backing up** — something is on the belt and the front item
cannot move — not when a switch has been set. A held belt with nothing on it is not blocking
anything, and a belt feeding a full node *is* blocking, without anyone having set a flag on it.

### 4.2 `Set All Outputs Blocked`

Holds only the belts that actually end a line (no output node and no output belt). Holding a belt in
the middle would stop the flow too, but it would stop it where nobody is looking; the queue is meant
to grow from the end.

---

## 5. The budget

`MaxItems` is a **hard ceiling**, not a target. A spawn past it fails, returns false, and increments
the refused counter — the frame time does not quietly go with it.

Lowering the budget at runtime trims immediately, and it takes items off the **back** of belts — the
ones that just got on — so what is already halfway to the end still arrives and the line does not
develop holes in the middle. Node buffers are trimmed after that if the world is still over the line.

Instance slots are **not** given back when the budget falls. That is why the draw call count does not
move, why the instance slot count does not move, and why raising the budget again allocates nothing.

---

## 6. The numbers

`ABeltHUD` draws them on `UCanvas`. Canvas rather than UMG for two reasons pulling the same way:

- The box has to survive a cooked **Shipping** build. `DrawDebug` is compiled out there and a debug
  widget is usually stripped; a Canvas overlay is not.
- Anything that has to be **clicked** belongs in UMG instead. An `AHUD` hit box is tested against
  `UGameViewportClient::GetMousePosition()`, which reports nothing at all on a machine with no mouse
  attached — a capture rig, a build agent, a headless test. Widgets are not affected.

So the numbers live on Canvas, where they cost nothing and always draw, and the controls live in a
widget, where they always receive the click.

| Line | What it proves |
|---|---|
| `Items n / budget` | the ceiling is real and the count obeys it |
| `Instance sets` | one draw call each — follows item *types*, never item count |
| `Instance slots` | the high-water mark; it does not fall when the budget does |
| `Belts / Nodes / buffered` | the layout, and what is waiting in junctions |
| `Throughput /s` | items leaving belts, measured over a sliding one-second window |
| `Jammed` | backpressure, as a number, next to the queue you can see growing |
| `Update / advance / instances` | where the milliseconds actually go |
| `Buffer growth` | zero in steady state — the "no runtime allocation" claim, readable |
| `Buffers KB` | ring buffers, baked paths and scratch, in one number |

Everything is read from the subsystem on the frame it is drawn. Nothing is cached, so the box cannot
claim one thing while the belts do another.

---

## 7. Console

```
Belt.Test [Items] [LoopRadius]   build a closed four-belt loop in front of the camera and fill it
Belt.Budget [Items]              read or set the world's item ceiling
Belt.Clear                       take every item off every belt and out of every node
Belt.Stats                       print the measured counters to the log
```

`Belt.Test` builds a **closed loop**, not a line into a sink, so the items stay in the world and the
item count holds still while you watch the other numbers. One built-in type per belt, so the instance
set count reads exactly four however many items are on it. `Belt.Test 0` takes the loop away.

### 7.1 The four checks, in order

1. `Belt.Test 10000` — ten thousand items, **Instance sets** reads 4.
2. `Belt.Budget 2000` — **Items** falls to 2000, **Instance slots** does not move, **Buffer growth
   this update** stays 0.
3. `Set All Outputs Blocked (true)` on a layout that ends in a sink — the queue grows backwards,
   nothing overlaps, nothing disappears, **Throughput** goes to 0 and **Jammed** climbs.
4. `Scale All Belt Speeds (0.5)` — **Throughput** follows, measurably, within a second.

---

## 8. Project settings

`Project Settings → Plugins → BeltLine`, stored in `DefaultGame.ini`. Read once when a world's
subsystem comes up; the budget, the update rate and the belt speeds all move at runtime afterwards
without touching the config.

| Setting | Default | Notes |
|---|---|---|
| `MaxItems` | 20000 | the world's hard ceiling |
| `MaxItemsPerBelt` | 4000 | caps what a very long spline can ask for |
| `MinItemPitch` | 40 cm | used only to size a belt's ring buffer |
| `MaxItemTypes` | 64 | the type index stored per item is one byte |
| `UpdatesPerSecond` | 0 (every frame) | a conveyor is watched directly; stepped updates read as stutter |
| `MaxUpdateStep` | 0.1 s | a hitch must not hand the pass a step big enough to jump the queue |
| `bTickInEditorWorlds` | true | belts run in the editor viewport |
| `DefaultSpeed` | 240 cm/s | |
| `DefaultMinGap` | 12 cm | |
| `PathSampleSpacing` | 50 cm | memory against how faithfully a curve is followed |
| `InstanceBoundsScale` | 1.2 | one set covers the whole layout; a little headroom avoids late pops |
| `InstanceStartCullDistance` / `InstanceEndCullDistance` | 0 / 0 | 0 = no cut |
| `bShowStatsByDefault` | true | |
| `TestItemCount` | 10000 | what `Belt.Test` uses with no argument |

---

## 9. Limits, honestly

- **Items spawn at the belt's entry.** `Spawn Item` places one item at `EntryDistance`. `Spawn Items`
  fills the free stretch behind whatever is already on the belt, working backwards from the end on an
  empty belt. There is no "insert an item in the middle of a running queue" — it would need a sorted
  insertion and would move every item behind it.
- **Reshaping a belt takes its items off.** The ring buffer is sized from the belt's length; carrying
  items across a reshape would put them at distances that no longer mean what they meant.
- **255 item types per world.** The per-item type field is one byte, on purpose.
- **No replication.** See §1.1.
- **No collision on the items.** They cannot be traced against, overlapped or hit. Take an item off
  the belt with `Try Take Item` and spawn a real actor if a project needs one.
- **A belt is one-way.** Negative speed is clamped to zero rather than reversed; reversing would make
  the queue ordering — which is what the entire movement pass relies on — run the other way.
- **Mac and Linux are enabled but untested.** The plugin uses no platform-specific code; only Win64
  has actually been built and run.

---

## 10. Compatibility

Unreal Engine **5.8**. One runtime module, loading phase `PreDefault`. Dependencies: `Core`,
`CoreUObject`, `Engine`, `DeveloperSettings` and (privately) `RenderCore` for the statistics box
background texture.

No UMG, no Niagara, no Chaos, no `UnrealEd`. There is no editor module and no editor-only code path —
the belts preview in the editor through the same runtime subsystem that ships.

Verified with `RunUAT BuildPlugin` for Editor Development, Game Development and Game Shipping, with
adaptive unity disabled so every translation unit is compiled on its own.
