# Garden auto-drive offline toolkit (#80)

Rebuilds every number in `Plan & Research Documents/Balamb Garden Auto-Drive -
offline analysis.md` and in the `s_gardenParks[]` table in `src/world_garden.inl`.
Pure stdlib + numpy + scipy. Run in this order:

```
python3 ../offline/extract_wmx.py <path to world.fs> ./wmx.obj
python3 garden_grid.py            # rasterize wmx.obj -> wmgrid.npz (128u engine grid)
python3 comps.py                  # foot/garden connected components  (see analysis doc)
python3 garden_park.py            # -> park_final.json, the shipping park table
python3 garden_matrix.py          # -> the validation matrix in GARDEN_MATRIX.md
```

`garden_sim.py` holds the conservative 256u planner grid, the clearance field and
the A*; `garden_exec.py` holds the executor model that `src/world_garden.inl`
ports to C++. Keep the two in step -- if you change a steering rule in the mod,
change it here and re-run `garden_matrix.py`.

The engine facts these encode (walk mask `byte15 & 0x20`, park mask `byte15 &
0x02`, the 200-unit step gate) are documented with their disassembly addresses at
the top of `src/world_garden.inl`.
