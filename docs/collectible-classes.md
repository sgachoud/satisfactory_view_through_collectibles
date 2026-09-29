# Collectible category overrides

The shared resolver is `FVTCCategoryTables`, used by local discovery and the server feed.
It tries native drop-pod/dropped-item types, item descriptors, then actor class ancestry.

Add or replace mappings in `Game.ini`:

```ini
[ViewThroughCollectibles.Categories]
+Descriptor=(Path="/Game/.../Desc_Foo.Desc_Foo_C", Category="Somersloop")
+ActorClass=(Path="/Game/.../BP_Foo.BP_Foo_C", Category="Somersloop")
```

Categories: `HardDrivePod`, `PowerSlugMk1`, `PowerSlugMk2`, `PowerSlugMk3`,
`MercerSphere`, `Somersloop`, `BerylNut`, `Paleberry`, `BaconAgaric`, `DroppedItem`.
The sentinel `MAX` is rejected.

Descriptor mappings work for loaded actors; class mappings also work for unloaded
registry entries. Install matching custom mappings on server and clients when extending
the supported categories. Category enable/disable settings remain per client.

The built-in paths are in `Private/VTCCategoryTables.cpp`. WAT1 is Somersloop and
WAT2 is Mercer Sphere. Flora folder/name mappings still require in-game verification.
Native drop pods and spawnable item pickups keep their dedicated categories.