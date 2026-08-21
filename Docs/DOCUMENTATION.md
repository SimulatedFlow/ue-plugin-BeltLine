# BeltLine — Documentation

Conveyor and item logistics for **Unreal Engine 5.8**.
Plugin version 1.0.0 · one runtime module, `BeltLine`, loading phase `PreDefault`.

---

## Contents

1. [Requirements, engine and platforms](#1-requirements-engine-and-platforms)
2. [Installation](#2-installation)
3. [Quick start — a running belt in five minutes](#3-quick-start--a-running-belt-in-five-minutes)
4. [What the plugin is](#4-what-the-plugin-is)
5. [The model](#5-the-model)
6. [Setting it up](#6-setting-it-up)
7. [Class and API overview](#7-class-and-api-overview)
8. [Blueprint surface](#8-blueprint-surface)
9. [C++ code examples](#9-c-code-examples)
10. [The budget](#10-the-budget)
11. [The numbers](#11-the-numbers)
12. [Console commands](#12-console-commands)
13. [Project settings](#13-project-settings)
14. [Troubleshooting](#14-troubleshooting)
15. [Limits, honestly](#15-limits-honestly)
16. [Support](#16-support)

---

## 1. Requirements, engine and platforms

| | |
|---|---|
| **Engine version** | Unreal Engine **5.8** |
| **Project type** | C++ **or** Blueprint-only. The plugin ships precompiled; a Blueprint-only project needs no compiler to *use* it. To call the C++ API directly (section 9) the project must be a C++ project. |
| **Supported target platforms** | `Win64`, `Mac`, `Linux` — the `PlatformAllowList` in `BeltLine.uplugin` |
| **Build-verified on** | **Win64 only.** Mac and Linux are enabled and contain no platform-specific code, but have not been built or run. Treat them as untested. |
| **Configurations** | Editor Development, Game Development and Game **Shipping**. The statistics box is a real Canvas overlay, not a `DrawDebug` call, so it survives a cooked Shipping build. |
| **Module dependencies** | `Core`, `CoreUObject`, `Engine`, `DeveloperSettings` (public) and `RenderCore` (private, for the statistics box background texture) |
| **Plugin dependencies** | None. No other Marketplace/Fab plugin is required. |
| **Third-party libraries** | None |
| **Network replication** | No — see §15 |

There is **no editor module** and no editor-only code path. The belts preview in the editor through
exactly the same runtime subsystem that ships in the game.

---

## 2. Installation

### 2.1 Into a project

1. Close the Unreal Editor.
2. Copy the `BeltLine` folder into your project's `Plugins` directory, creating it if it does not
   exist:

   ```text
   MyProject/
     MyProject.uproject
     Plugins/
       BeltLine/
         BeltLine.uplugin
         Source/
         Content/
         Config/
         Resources/
   ```

3. Open the project. If you are asked to rebuild missing modules, say yes.
4. Check **Edit → Plugins → Gameplay → BeltLine** and make sure it is enabled. Restart if prompted.

### 2.2 Into the engine (all projects)

Copy the same folder to `<UE_5.8>/Engine/Plugins/Marketplace/BeltLine` instead. Everything else is
identical. Per-project installation is recommended — it keeps the plugin under your project's version
control and survives an engine reinstall.

### 2.3 Verifying the installation

Open any level and type into the console (`~`):

```text
Belt.Test 10000
```

Four belts appear in a closed loop in front of the camera, carrying ten thousand items. If the loop
runs, the plugin is installed and working. `Belt.Test 0` removes it again.

### 2.4 Using it from C++

Add the module to your own module's `Build.cs`:

```csharp
PublicDependencyModuleNames.AddRange(new string[]
{
    "Core", "CoreUObject", "Engine",
    "BeltLine",           // <- this
});
```

The public headers are `BeltActor.h`, `BeltNode.h`, `BeltItemType.h`, `BeltSubsystem.h`,
`BeltStatics.h`, `BeltSettings.h`, `BeltHUD.h`, `BeltTypes.h` and `BeltPath.h`.

### 2.5 Included content

`Content/BeltLine/` ships four item-type Data Assets (Crate, Barrel, Ore, Plate), two materials with
five instances, and a demo map. All of it is optional — the plugin is fully usable with the content
folder deleted, because the four built-in item types live in **code** (§6.2).

---

## 3. Quick start — a running belt in five minutes

**Step 1 — place a belt.** In the Place Actors panel search for **Belt** and drag one into the level.
It arrives with a two-point spline.

**Step 2 — shape it.** Select the spline component and drag its points. Alt-drag an end point to add
another. The belt rebakes itself every time you let go.

**Step 3 — put something on it.** In the Level Blueprint (or any Blueprint), on **Begin Play**:

```text
Get Built In Item Type  (World Context: self, Shape: Box)
        ↓  Return Value
Spawn Items  (Belt: <your belt>, Item Type: ←, Count: 200)
```

Press Play. Two hundred crates run along the belt.

**Step 4 — see the numbers.** Set the level's **GameMode Override → HUD Class** to **Belt HUD**
(`ABeltHUD`). A statistics box appears in the top-left: items, instance sets, throughput, jammed
items, milliseconds per update.

**Step 5 — see the backpressure.** Select the belt and tick **Output Blocked**. The queue packs
backwards from the end at minimum spacing, `Throughput` falls to zero and `Jammed` climbs. Untick it
and the whole line pulls away again. Nothing was spawned or destroyed at either point.

**Optional — make the conveyor itself visible.** Set the belt's **Belt Mesh** to a static mesh (a
stretched cube works) and **Belt Mesh Length** to that mesh's length along X. It is tiled along the
spline as scenery; the items do not need it. Raise **Item Height Offset** by the surface thickness so
the items ride on top rather than through.

**Optional — branch or merge.** Drop a **Belt Node** where two belts meet, set the first belt's
**Output Node** to it, and add the second belt to the node's **Output Belts**. That one actor is a
merge, a split and a sorter, depending on how many belts point at it and what filter it carries.

> The belt runs in the **editor viewport before you press Play**. That is deliberate: the useful
> question while laying out a line is "does this actually flow", and a static spline cannot answer it.
> Turn it off under `Project Settings → Plugins → BeltLine → Tick In Editor Worlds`.

---

## 4. What the plugin is

A belt is a spline and a ring buffer. An item is an entry in that ring buffer: a distance along the
belt and a one-byte index into the world's list of item types. That is the whole data model, and
almost everything else in this document follows from it.

An item is **not** an `AActor`, **not** a `UObject` and **not** a component. It has no `Tick`, no
transform component, no collision, no lifetime and no GC cost. Ten thousand items on belts are a few
hundred kilobytes of `float` and `uint8`, one pass per belt over contiguous memory, and one instanced
draw call per *kind* of item.

### 4.1 What it is not

Stated plainly, because the neighbouring products on the store are different things:

- **Not a factory game.** No recipes, no crafting, no machines, no production chains.
- **Not an inventory system.** An item type carries a mesh, a size and a weight. It has no stack
  size, no value and no slot.
- **No physics on the items.** They do not collide, roll, fall or push. That is the trade that buys
  ten thousand of them.
- **No network replication.** Belts are deterministic given the same speeds and the same spawns; if a
  project needs authoritative item flow, it replicates its own spawn events, not this plugin's state.

---

## 5. The model

### 5.1 The ring buffer

Each `ABeltActor` holds three parallel arrays, sized once when the belt is built:

```cpp
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

### 5.2 The movement pass

One walk from the front of the queue to the back:

```text
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

### 5.3 Hand-offs

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

### 5.4 Nodes

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

### 5.5 The baked path

`USplineComponent::GetTransformAtDistanceAlongSpline` walks a reparameterisation table to turn a
distance into a spline key, evaluates three interpolated curves and builds a matrix. Calling it once
per item per frame is what makes hand-built conveyors fall over in the low thousands — and no amount
of instancing fixes it, because the cost lands on the game thread before a single instance is written.

So the spline is sampled once, at a fixed spacing, into `FBeltPath`:

```cpp
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

### 5.6 The instance sets

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
than a claim in a description. In a material, read it with a **PerInstanceCustomData** node at index 1.

---

## 6. Setting it up

### 6.1 A belt

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
| `MaxBeltMeshSegments` | cap on spline mesh components, so a very long belt cannot quietly create hundreds. |
| `ItemHeightOffset` | how far above the spline the items ride. Add the belt surface's thickness. |

### 6.2 An item type

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

The same four shapes also ship as editable Data Assets under `Content/BeltLine/Items/`
(`DA_BeltItem_Crate`, `_Barrel`, `_Ore`, `_Plate`) if you would rather start from an asset than from
code.

### 6.3 A node

Set `Capacity` small — a node is a junction, not a warehouse. Eight is plenty to keep a merge from
stuttering, and a large buffer only moves the point at which the queue becomes visible, which is the
opposite of what a conveyor should do.

`bSplitEnabled = false` keeps the full output list but feeds only the first, so the branch drains and
the main line takes the whole flow. Turning it back on refills the branch. Nothing is spawned or
destroyed either way.

`Distribution` chooses between the outputs: **Round Robin** takes turns (a splitter), **First
Available** always fills the first output that has room and lets the later ones act as overflow lanes.

---

## 7. Class and API overview

Seven public classes. Everything else is an implementation detail.

| Class | Kind | What it is for |
|---|---|---|
| `ABeltActor` | Actor, `Blueprintable`, display name **Belt** | One conveyor: a `USplineComponent`, a speed, a gap, an allowed-type filter and the ring buffer. Draggable and reshapeable in the editor; runs without PIE. |
| `ABeltNode` | Actor, `Blueprintable`, display name **Belt Node** | Where belts meet: a small ring buffer with a capacity, an optional type filter, a list of output belts and a distribution mode. Merge, split, sorter. |
| `UBeltItemType` | `UDataAsset`, `BlueprintType` | One kind of thing on a belt: mesh, override material, scale, rotation, offset, colour, radius, weight. One asset = one instance set = one draw call. |
| `UBeltSubsystem` | `UTickableWorldSubsystem` | Owns the belts, the nodes, the item types, the instance sets, the budget and the numbers for a world. Runs in Game, PIE **and Editor** worlds. |
| `UBeltStatics` | `UBlueprintFunctionLibrary` | The whole plugin from Blueprint. Every call goes through the same subsystem the C++ side uses. |
| `UBeltSettings` | `UDeveloperSettings` (`Config = Game`) | Project defaults and ceilings, under `Project Settings → Plugins → BeltLine`. |
| `ABeltHUD` | `AHUD`, `Blueprintable`, display name **Belt HUD** | The measured counters, drawn on `UCanvas` so they survive a Shipping build. |

Supporting types in `BeltTypes.h` and `BeltPath.h`:

| Type | What it is |
|---|---|
| `EBeltBuiltInItem` | `Box`, `Barrel`, `Ore`, `Plate` — the four code item types |
| `EBeltNodeDistribution` | `RoundRobin`, `FirstAvailable` |
| `FBeltStats` | `BlueprintType` struct with every measured counter (§11) |
| `FBeltPath` | the baked position/rotation table for one belt |
| `FBeltUpdateContext` | the flat radius/weight tables handed to the movement pass. Not reflected — this is the hot path. |

### 7.1 `ABeltActor` — the members you will use

```cpp
void   SetSpeed(float NewSpeed);
float  GetSpeed() const;
void   SetOutputBlocked(bool bNewBlocked);
bool   IsOutputBlocked() const;      // the switch
bool   IsBlocked() const;            // actually backing up (see §8.1)
int32  GetItemCount() const;
int32  GetCapacity() const;
int32  GetJammedCount() const;
float  GetLength() const;            // cm, from the baked path
float  GetThroughput() const;        // items/second off this belt
float  GetLoadFactor() const;        // 0..1
void   ClearItems();
void   RebuildPath();                // rebake from the spline
bool   AllowsItemType(const UBeltItemType* ItemType) const;
```

### 7.2 `ABeltNode`

```cpp
int32  GetItemCount() const;
int32  GetCapacity() const;
bool   IsFull() const;
void   SetBlocked(bool bNewBlocked);
bool   IsBlocked() const;
void   SetSplitEnabled(bool bNewSplitEnabled);
bool   IsSplitEnabled() const;
void   ClearItems();
bool   AllowsItemType(const UBeltItemType* ItemType) const;
```

### 7.3 `UBeltSubsystem`

```cpp
static UBeltSubsystem* Get(const UObject* WorldContextObject);

// registration / layout
int32  GetBeltCount() const;
int32  GetNodeCount() const;
TArray<ABeltActor*> GetBelts() const;

// item types
UBeltItemType* GetBuiltInItemType(EBeltBuiltInItem Shape);
UBeltItemType* GetItemTypeByIndex(int32 TypeIndex) const;
int32  GetItemTypeCount() const;

// items
bool   SpawnItem(ABeltActor* Belt, UBeltItemType* ItemType);
int32  SpawnItems(ABeltActor* Belt, UBeltItemType* ItemType, int32 Count);
int32  SpawnItemsAcrossBelts(UBeltItemType* ItemType, int32 Count);
bool   TryTakeItem(ABeltActor* Belt, UBeltItemType*& OutItemType);
void   ClearAllItems();
int32  GetItemCount() const;

// budget
void   SetItemBudget(int32 NewBudget);
int32  GetItemBudget() const;
int32  GetRemainingBudget() const;

// belts, in bulk
void   SetAllBeltSpeeds(float NewSpeed);
void   ScaleAllBeltSpeeds(float Scale);
int32  SetAllOutputsBlocked(bool bNewBlocked);
int32  SetAllNodesSplitEnabled(bool bNewSplitEnabled);
float  GetAverageBeltSpeed() const;

// update rate and numbers
void   SetUpdatesPerSecond(float NewRate);
float  GetUpdatesPerSecond() const;
const FBeltStats& GetStats() const;
float  GetThroughput() const;
```

---

## 8. Blueprint surface

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
`Get Belt Stats`, `Get Belt Subsystem`.

### 8.1 `Is Blocked` means what it says

It is true when the queue is **actually backing up** — something is on the belt and the front item
cannot move — not when a switch has been set. A held belt with nothing on it is not blocking
anything, and a belt feeding a full node *is* blocking, without anyone having set a flag on it.

Use `Is Output Blocked` on the actor if what you want is the state of the switch.

### 8.2 `Set All Outputs Blocked`

Holds only the belts that actually end a line (no output node and no output belt). Holding a belt in
the middle would stop the flow too, but it would stop it where nobody is looking; the queue is meant
to grow from the end.

---

## 9. C++ code examples

Add `"BeltLine"` to your module's dependencies first (§2.4).

### 9.1 Load a belt on Begin Play

```cpp
#include "BeltSubsystem.h"
#include "BeltActor.h"
#include "BeltItemType.h"

void AMyGameMode::BeginPlay()
{
    Super::BeginPlay();

    UBeltSubsystem* Belts = UBeltSubsystem::Get(this);
    if (!Belts)
    {
        return;   // no subsystem in this world type
    }

    UBeltItemType* Crate = Belts->GetBuiltInItemType(EBeltBuiltInItem::Box);

    for (ABeltActor* Belt : Belts->GetBelts())
    {
        const int32 Placed = Belts->SpawnItems(Belt, Crate, 200);
        UE_LOG(LogTemp, Log, TEXT("%s took %d crates"), *Belt->GetName(), Placed);
    }
}
```

`SpawnItems` stops at the belt's capacity, at the world budget, or when there is no more room at the
spacing the items need — whichever comes first — and tells you how many actually went on. It never
silently drops one.

### 9.2 A machine that consumes what arrives

The other half of a conveyor: instead of the belt pushing into your system, your system asks the belt
for what reached the end.

```cpp
#include "BeltStatics.h"
#include "BeltActor.h"
#include "BeltItemType.h"

void AFurnace::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    UBeltItemType* Arrived = nullptr;
    while (InputCapacity > 0 && UBeltStatics::TryTakeItem(InputBelt, Arrived))
    {
        --InputCapacity;
        Smelt(Arrived);              // your item system takes over here
    }

    // When the furnace is full we simply stop taking. The belt backs up on its own -
    // no flag to set, no event to fire, no items destroyed.
}
```

Point the belt's `OutputNode` and `OutputBelt` at nothing so its end is a sink, and the furnace
becomes the only thing taking items off it.

### 9.3 Build a belt at runtime

This is what `Belt.Test` does internally, trimmed to one belt:

```cpp
#include "BeltActor.h"
#include "Components/SplineComponent.h"

ABeltActor* AMyDirector::SpawnBelt(const FVector& Start, const FVector& End, float Speed)
{
    ABeltActor* Belt = GetWorld()->SpawnActor<ABeltActor>(
        ABeltActor::StaticClass(), FTransform::Identity);

    if (!Belt || !Belt->Spline)
    {
        return nullptr;
    }

    Belt->Spline->ClearSplinePoints(false);
    Belt->Spline->AddSplinePoint(Start, ESplineCoordinateSpace::World, false);
    Belt->Spline->AddSplinePoint(End,   ESplineCoordinateSpace::World, false);
    Belt->Spline->SetSplinePointType(0, ESplinePointType::Linear, false);
    Belt->Spline->SetSplinePointType(1, ESplinePointType::Linear, false);
    Belt->Spline->UpdateSpline();

    // Rebake the path, then size the ring buffer from the new length. Both are allocations,
    // and both happen here rather than while the belt is running.
    Belt->RebuildPath();
    Belt->RebuildRingBuffer(nullptr);
    Belt->SetSpeed(Speed);

    return Belt;
}
```

Reshaping a belt takes its items off — the ring buffer is sized from the belt's length, and carrying
items across a reshape would leave them at distances that no longer mean what they meant. Build the
layout first, then load it.

### 9.4 Merging two belts into one

```cpp
#include "BeltNode.h"

ABeltNode* Node = GetWorld()->SpawnActor<ABeltNode>(
    ABeltNode::StaticClass(), FTransform(JunctionLocation));

Node->Capacity      = 8;               // a junction, not a warehouse
Node->Distribution  = EBeltNodeDistribution::RoundRobin;
Node->RebuildBuffer(nullptr);

LeftBelt->OutputNode  = Node;          // two belts in...
RightBelt->OutputNode = Node;
Node->OutputBelts.Add(MainLine);       // ...one belt out

// Give the node a filter and the same actor becomes a sorter:
// Node->AllowedTypes.Add(OreType);
```

### 9.5 Backpressure, and reacting to it

```cpp
void AControlRoom::UpdateAlarms()
{
    for (ABeltActor* Belt : UBeltStatics::GetAllBelts(this))
    {
        // IsBlocked is true when the queue is really backing up - a held output,
        // a full downstream belt or a full node - not merely that a switch was set.
        if (Belt->IsBlocked() && Belt->GetLoadFactor() > 0.9f)
        {
            RaiseJamAlarm(Belt);
        }
    }
}

void AControlRoom::EmergencyStop()
{
    // Holds every belt whose end feeds nothing. The queues grow backwards from
    // the ends of the line; nothing is spawned and nothing is destroyed.
    const int32 Held = UBeltStatics::SetAllOutputsBlocked(this, true);
    UE_LOG(LogTemp, Log, TEXT("Emergency stop: %d outputs held"), Held);
}
```

### 9.6 Scaling the budget to the quality setting

```cpp
void AMyGameInstance::ApplyScalability(int32 QualityLevel)
{
    const int32 Budget = (QualityLevel <= 1) ? 2000
                       : (QualityLevel == 2) ? 8000
                       :                       20000;

    UBeltStatics::SetItemBudget(this, Budget);

    // Lowering it trims from the back of the belts, so what is already halfway to
    // the end still arrives. Instance slots are not given back: the draw call count
    // does not move and raising it again allocates nothing.
}
```

### 9.7 Reading the measured numbers

```cpp
#include "BeltTypes.h"

void AMyHUD::LogBeltNumbers()
{
    const FBeltStats Stats = UBeltStatics::GetBeltStats(this);

    UE_LOG(LogTemp, Log,
        TEXT("%d items / %d budget · %d instance sets (= draw calls) · %.0f items/s · "
             "%d jammed · %.2f ms update · buffer growth this update: %d"),
        Stats.Items, Stats.ItemBudget, Stats.InstanceSets,
        Stats.ThroughputPerSecond, Stats.JammedItems,
        Stats.UpdateMilliseconds, Stats.BufferGrowthThisUpdate);
}
```

`BufferGrowthThisUpdate` is the honest one: it reads zero in steady state, which is the
"nothing is allocated once the buffers stand" claim as a number rather than a promise.

---

## 10. The budget

`MaxItems` is a **hard ceiling**, not a target. A spawn past it fails, returns false, and increments
the refused counter — the frame time does not quietly go with it.

Lowering the budget at runtime trims immediately, and it takes items off the **back** of belts — the
ones that just got on — so what is already halfway to the end still arrives and the line does not
develop holes in the middle. Node buffers are trimmed after that if the world is still over the line.

Instance slots are **not** given back when the budget falls. That is why the draw call count does not
move, why the instance slot count does not move, and why raising the budget again allocates nothing.

---

## 11. The numbers

`ABeltHUD` draws them on `UCanvas`. Canvas rather than UMG for two reasons pulling the same way:

- The box has to survive a cooked **Shipping** build. `DrawDebug` is compiled out there and a debug
  widget is usually stripped; a Canvas overlay is not.
- Anything that has to be **clicked** belongs in UMG instead. An `AHUD` hit box is tested against
  `UGameViewportClient::GetMousePosition()`, which reports nothing at all on a machine with no mouse
  attached — a capture rig, a build agent, a headless test. Widgets are not affected.

So the numbers live on Canvas, where they cost nothing and always draw, and the controls live in a
widget, where they always receive the click.

To use it: set your GameMode's **HUD Class** to `ABeltHUD`, or subclass it. `StatsBoxOrigin`,
`StatsBoxWidth` and `bShowStats` are all `EditAnywhere`/`BlueprintReadWrite`, and `ToggleStats()` is
what a **STATS** button in a widget calls.

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
claim one thing while the belts do another. The same struct is available to Blueprint and C++ through
`Get Belt Stats` (§9.7).

---

## 12. Console commands

```text
Belt.Test [Items] [LoopRadius]   build a closed four-belt loop in front of the camera and fill it
Belt.Budget [Items]              read or set the world's item ceiling
Belt.Clear                       take every item off every belt and out of every node
Belt.Stats                       print the measured counters to the log
```

`Belt.Test` builds a **closed loop**, not a line into a sink, so the items stay in the world and the
item count holds still while you watch the other numbers. One built-in type per belt, so the instance
set count reads exactly four however many items are on it. `Belt.Test 0` takes the loop away.

### 12.1 The four checks, in order

1. `Belt.Test 10000` — ten thousand items, **Instance sets** reads 4.
2. `Belt.Budget 2000` — **Items** falls to 2000, **Instance slots** does not move, **Buffer growth
   this update** stays 0.
3. `Set All Outputs Blocked (true)` on a layout that ends in a sink — the queue grows backwards,
   nothing overlaps, nothing disappears, **Throughput** goes to 0 and **Jammed** climbs.
4. `Scale All Belt Speeds (0.5)` — **Throughput** follows, measurably, within a second.

---

## 13. Project settings

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

## 14. Troubleshooting

| Symptom | Cause and fix |
|---|---|
| **Nothing moves and nothing appears.** | The belt has no items yet. `Spawn Items` returns how many went on — log it. If it returns 0, the world is at its budget or the belt's `AllowedTypes` refuses the type. |
| **Items sink into the belt mesh.** | `ItemHeightOffset` on the belt is the surface height; `MeshOffset.Z` on the item type should be about half the mesh's height. Both are needed. |
| **Items look like they are inside one another.** | The item type's `Radius` is smaller than the mesh. Radius is the **half-extent along the direction of travel** after `MeshScale`, not a collision shape. |
| **The queue never grows when I block the output.** | Check `IsBlocked` (backing up), not `IsOutputBlocked` (the switch). A belt with nothing on it is not blocking anything. Also check the belt is a real sink — `SetAllOutputsBlocked` only holds belts whose end feeds nothing. |
| **Belt does not run in the editor viewport.** | `Project Settings → Plugins → BeltLine → Tick In Editor Worlds`. Note the belt still needs items; spawn them from a construction script or press Play. |
| **Instance set count is higher than expected.** | It follows registered item *types* that hold at least one item. Two Data Assets pointing at the same mesh are still two sets, so consolidate the assets, not the meshes. |
| **The statistics box does not appear.** | The level's GameMode `HUDClass` must be `ABeltHUD` or a subclass, and `bShowStats` must be on (`bShowStatsByDefault` in the settings). |
| **Items vanish at the end of a belt.** | That belt is a sink — no `OutputNode` and no `OutputBelt`. Set one, or call `TryTakeItem` before they reach the end. |
| **Reshaping a belt emptied it.** | Expected, and documented in §9.3 and §15. Build the layout, then load it. |
| **The jam tint does not show.** | The material has to read per-instance custom data index 1. Slot 1 is written every update whether a material reads it or not. |

---

## 15. Limits, honestly

- **Items spawn at the belt's entry.** `Spawn Item` places one item at `EntryDistance`. `Spawn Items`
  fills the free stretch behind whatever is already on the belt, working backwards from the end on an
  empty belt. There is no "insert an item in the middle of a running queue" — it would need a sorted
  insertion and would move every item behind it.
- **Reshaping a belt takes its items off.** The ring buffer is sized from the belt's length; carrying
  items across a reshape would put them at distances that no longer mean what they meant.
- **255 item types per world.** The per-item type field is one byte, on purpose. The default ceiling
  is 64 (`MaxItemTypes`).
- **No replication.** See §4.1.
- **No collision on the items.** They cannot be traced against, overlapped or hit. Take an item off
  the belt with `Try Take Item` and spawn a real actor if a project needs one.
- **A belt is one-way.** Negative speed is clamped to zero rather than reversed; reversing would make
  the queue ordering — which is what the entire movement pass relies on — run the other way.
- **Mac and Linux are enabled but untested.** The plugin uses no platform-specific code; only Win64
  has actually been built and run.

---

## 16. Support

- **Documentation:** <https://github.com/SimulatedFlow/ue-plugin-BeltLine>
- **Support:** <mailto:teufelsilvan@gmail.com>

When reporting a problem, the output of `Belt.Stats` and the engine version are usually enough to
locate it.

---

Unreal Engine **5.8**. One runtime module, loading phase `PreDefault`. Dependencies: `Core`,
`CoreUObject`, `Engine`, `DeveloperSettings` and (privately) `RenderCore`.

No UMG, no Niagara, no Chaos, no `UnrealEd`. There is no editor module and no editor-only code path —
the belts preview in the editor through the same runtime subsystem that ships.

Verified with `RunUAT BuildPlugin` for Editor Development, Game Development and Game Shipping, with
adaptive unity disabled so every translation unit is compiled on its own.

Copyright 2026 Silvan Teufel. All Rights Reserved.
