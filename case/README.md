# Dragon Ball Radar Case

Custom 3D-printable case for the **Waveshare ESP32-S3-LCD-1.85** (non-touch,
round 360×360 display) — the board in this project.

Designed parametrically in OpenSCAD because **no ready-made case exists** for
the non-touch 1.85" board (the community cases are for the 1.28", 1.47", 2.8"
and 7" variants, or for the *touch* 1.85" board).

## Files

| File | Role |
|---|---|
| `radar_case.scad` | Parametric source (edit any dimension at the top) |
| `radar_case_front.stl` | **Print part 1** — bezel + body + screw flange |
| `radar_case_back.stl` | **Print part 2** — lid with screw holes + vents |
| `check_components.py` | Mesh check: connected components + bounding boxes |
| `preview_*.png` | Renders (front / back / assembled) |

## Board dimensions used

| Item | Value |
|---|---|
| PCB outline | 49.95 × 48.08 mm (modelled as Ø50) |
| Display | 1.85" round, active area 45.68 mm |
| Display window | Ø46.4 mm (hides ~0.3 mm of the glass edge) |
| Internal cavity | Ø51.1 mm (≈1.15 mm radial clearance) |
| Assembled size | Ø59.1 × 22.0 mm |

## Print settings (Bambu Lab friendly)

- **Material:** PETG recommended (PLA works fine too)
- **Layer height:** 0.20 mm
- **Walls:** 3 perimeters
- **Infill:** 20 % (gyroid or grid)
- **Supports:** **none** — both parts are designed to print without them
- **Orientation:**
  - `radar_case_front` → print **bezel-face down** (flat front on the plate)
  - `radar_case_back` → print **spigot up** (flat lid on the plate)

## Assembly

1. Slide the display/board into the front part from the back; the glass sits
   behind the bezel lip.
2. Route the USB-C cable out through the side cutout.
3. Fit the lid; the spigot self-aligns it in the cavity.
4. Fasten with **3 × M2 self-tapping screws** (≈8 mm) through the lid into the
   front bosses.

## Hardware

| Item | Qty | Note |
|---|---:|---|
| M2 self-tapping screw, ~8 mm | 3 | Into the printed bosses (Ø1.7 mm pilot) |

## Design notes / open assumptions

- The USB-C cutout is placed on the **bottom edge (+Y)**. Verify which way your
  board's USB port faces before printing; if it differs, rotate the board or
  change the cutout angle in the `.scad` (search for `USB-C cutout`).
- The bezel lip retains the display by a 1.2 mm overhang. If your board's glass
  sits deeper/shallower, adjust `front_t` and `board_t` at the top of the source.
- Vent slots in the lid are included for airflow; they do not affect structure.

## Regenerating / editing

All key dimensions are variables at the top of `radar_case.scad`. After editing:

```bash
openscad --export-format binstl -D 'part="front"' -o radar_case_front.stl radar_case.scad
openscad --export-format binstl -D 'part="back"'  -o radar_case_back.stl  radar_case.scad
python3 check_components.py radar_case_front.stl radar_case_back.stl
```

Expect: `1 connected component`, `0 holes`, `0 degenerate facets` for each part.
