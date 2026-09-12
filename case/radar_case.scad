// ============================================================
//  Dragon Ball Radar Case
//  for Waveshare ESP32-S3-LCD-1.85 (non-touch, round 360x360)
//
//  Board: 49.95 (H) x 48.08 (V) mm -> modelled as Ø50.0 carrier
//  Display: 1.85" round, active area 45.68 mm
//
//  Two parts, printed open-side-up, no supports:
//    radar_case_front  (bezel + body + screw flange)
//    radar_case_back   (lid + USB-C / vent)
// ============================================================

$fn = 128;

// ---------- Board ----------
board_d    = 50.0;    // PCB outer diameter
board_t    = 12.0;    // PCB + display + rear component stack depth
board_fit  = 1.1;     // radial clearance around the board (FDM-friendly)

// ---------- Shell ----------
wall       = 2.4;     // nominal wall
front_t    = 2.6;     // bezel thickness (in front of the glass)
lip_t      = 1.2;     // bezel lip that overhangs the display
body_t     = board_t; // open cavity depth behind the bezel
flange_t   = 3.2;     // mating flange where front and back meet
flange_grow= 3.2;     // flange sticks out beyond the body wall
back_t     = 2.4;     // lid thickness

// ---------- Display ----------
screen_d   = 47.0;    // 1.85" round display glass
win_d      = 46.4;    // visible aperture (hides ~0.3 mm of glass edge)

// ---------- Fasteners ----------
screw_n     = 3;      // 3 x M2, evenly spaced
screw_clear = 2.3;    // M2 clearance hole
screw_pilot = 1.7;    // M2 self-tapping pilot in the front part
boss_d      = 4.6;    // screw boss outer diameter

// ---------- Ports ----------
usb_w      = 10.0;    // USB-C cutout width
usb_h      = 4.4;     // USB-C cutout height

// ---------- Derived ----------
case_id   = board_d + board_fit;
case_od   = case_id + 2 * wall;
flange_od = case_od + flange_grow;
screw_r   = (case_od + flange_od) / 4;

// ============================================================
//  FRONT: bezel + body + flange
// ============================================================
module radar_front() {
  difference() {
    union() {
      // Bezel ring (front face, full disc)
      cylinder(d = case_od, h = front_t);

      // Body wall
      translate([0, 0, front_t])
        difference() {
          cylinder(d = case_od, h = body_t);
          translate([0, 0, -0.01])
            cylinder(d = case_id, h = body_t + 0.02);
        }

      // Mating flange
      translate([0, 0, front_t + body_t])
        cylinder(d = flange_od, h = flange_t);

      // Screw bosses on the flange
      for (i = [0 : screw_n - 1])
        rotate([0, 0, i * 360 / screw_n])
          translate([screw_r, 0, front_t + body_t])
            cylinder(d = boss_d, h = flange_t);
    }

    // --- Subtractions ---

    // Display window through the bezel
    translate([0, 0, -0.01])
      cylinder(d = win_d, h = front_t + 0.02);

    // Bezel lip clearance so the glass sits recessed and is retained
    translate([0, 0, front_t - lip_t])
      cylinder(d = screen_d + 0.6, h = lip_t + 0.02);

    // Open the cavity through the flange
    translate([0, 0, front_t + body_t - 0.01])
      cylinder(d = case_id, h = flange_t + 0.02);

    // Screw pilot holes
    for (i = [0 : screw_n - 1])
      rotate([0, 0, i * 360 / screw_n])
        translate([screw_r, 0, front_t + body_t - 0.01])
          cylinder(d = screw_pilot, h = flange_t + 0.02);

    // USB-C cutout in the bottom wall
    translate([0, case_od / 2, front_t + body_t * 0.5])
      rotate([90, 0, 0])
        hull() {
          translate([-usb_w / 2 + usb_h / 2, 0, 0])
            cylinder(d = usb_h, h = wall * 3, center = true);
          translate([ usb_w / 2 - usb_h / 2, 0, 0])
            cylinder(d = usb_h, h = wall * 3, center = true);
        }
  }
}

// ============================================================
//  BACK: lid with USB relief + vents
// ============================================================
module radar_back() {
  difference() {
    union() {
      // Lid plate
      cylinder(d = flange_od, h = back_t);

      // Spigot that plugs into the front cavity (self-aligning)
      translate([0, 0, back_t])
        cylinder(d = case_id - 0.6, h = 1.8);
    }

    // --- Subtractions ---

    // Screw clearance through the lid
    for (i = [0 : screw_n - 1])
      rotate([0, 0, i * 360 / screw_n])
        translate([screw_r, 0, -0.01])
          cylinder(d = screw_clear, h = back_t + 1.82 + 0.02);

    // Vent slots (heat escape) arranged in a ring
    for (i = [0 : 5])
      rotate([0, 0, i * 60 + 30])
        translate([case_id / 2 - 4.0, 0, -0.01])
          hull() {
            translate([-2.6, 0, 0]) cylinder(d = 2.4, h = back_t + 0.02);
            translate([ 2.6, 0, 0]) cylinder(d = 2.4, h = back_t + 0.02);
          }
  }
}

// ============================================================
//  RENDER
// ============================================================
part = "both";  // "front" | "back" | "both" | "assembly"

if (part == "front" || part == "both") radar_front();
if (part == "back")  translate([0, 0, 0]) radar_back();
if (part == "both")  translate([flange_od + 6, 0, 0]) radar_back();

// Exploded assembly: lid seated on the front part.
if (part == "assembly") {
  radar_front();
  translate([0, 0, front_t + body_t + flange_t]) radar_back();
}
