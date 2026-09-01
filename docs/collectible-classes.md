# Collectible class paths — to verify against a real install

The mod maps a spawned actor to a collectible category by walking its class hierarchy
against a table (`UVTCOutlineSubsystem::LoadClassCategoryTable`). The default entries below
are **best-effort guesses** and must be checked against the game version you build against —
open the packaged game in [FModel](https://github.com/4sval/FModel) or browse
`FactoryGame` content in the Unreal editor.

A wrong path only makes that one category inert (nothing to outline). Fix it either in
`LoadClassCategoryTable()` or, without recompiling, via a `Game.ini` override:

```ini
[ViewThroughCollectibles.ClassCategories]
+Entry=(Path="/Game/FactoryGame/.../BP_WAT2.BP_WAT2_C", Category="Somersloop")
```

`Category` must be one of the `EVTCCollectibleCategory` names: `HardDrivePod`,
`PowerSlugMk1`, `PowerSlugMk2`, `PowerSlugMk3`, `MercerSphere`, `Somersloop`, `BerylNut`,
`Paleberry`, `BaconAgaric`, `DroppedItem`.

| Category | Common name | Default class path (VERIFY) | Native base (for the world sweep) |
|---|---|---|---|
| `HardDrivePod` | Hard drive crash-site pod | `/Game/FactoryGame/World/Benefit/DropPod/BP_DropPod.BP_DropPod_C` | `AFGDropPod` |
| `PowerSlugMk1` | Blue power slug | `/Game/FactoryGame/Resource/Environment/Crystal/BP_Crystal.BP_Crystal_C` | `AFGItemPickup` |
| `PowerSlugMk2` | Yellow power slug | `.../Crystal/BP_Crystal_mk2.BP_Crystal_mk2_C` | `AFGItemPickup` |
| `PowerSlugMk3` | Purple power slug | `.../Crystal/BP_Crystal_mk3.BP_Crystal_mk3_C` | `AFGItemPickup` |
| `MercerSphere` | Mercer Sphere | `/Game/FactoryGame/Resource/Environment/AlienArtifacts/BP_WAT1.BP_WAT1_C` | `AFGItemPickup` |
| `Somersloop` | Somersloop | `/Game/FactoryGame/Resource/Environment/AlienArtifacts/BP_WAT2.BP_WAT2_C` | `AFGItemPickup` |
| `BerylNut` | Beryl Nut | `/Game/FactoryGame/Resource/Environment/Berry/BP_Berry.BP_Berry_C` | `AFGItemPickup` |
| `Paleberry` | Paleberry | `/Game/FactoryGame/Resource/Environment/Nut/BP_Nut.BP_Nut_C` | `AFGItemPickup` |
| `BaconAgaric` | Bacon Agaric | `/Game/FactoryGame/Resource/Environment/Shroom/BP_Shroom.BP_Shroom_C` | `AFGItemPickup` |
| `DroppedItem` | Item dropped on the ground | *(none — matched by native type)* | `AFGItemPickup_Spawnable` |

Notes to resolve during verification:

- In-game names vs class names for flora are historically swapped/odd (`BP_Berry` is not
  necessarily the Beryl Nut). Match by picking one up in-game and checking the resulting
  item descriptor, or by the mesh in FModel.
- Power slug tiers may all be the same blueprint with a data-driven tier instead of three
  classes. If so, collapse `PowerSlugMk1..3` handling in `ResolveCategory` to read the
  tier/descriptor and keep the three config entries.
- `AFGItemPickup` header may be `FGItemPickup.h` or split; `AFGItemPickup_Spawnable` may be
  `FGItemPickup_Spawnable.h`. Adjust includes in `VTCOutlineSubsystem.cpp`.
- Drop pod base class could be `AFGDropPod` or a `AFGWorldScannableData`-driven actor.
