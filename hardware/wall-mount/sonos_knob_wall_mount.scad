// ============================================================================
//  Sonos Knob -- Wall Mount  (two-part, twist-lock / bayonet)
//  Target: Elecrow CrowPanel 2.1" HMI ESP32 Rotary Display (DHE03921D)
//  Units: millimetres.  Render in OpenSCAD (https://openscad.org).
//
//  PART 1  wall plate  -> screws to the wall, has the bayonet ring + lugs
//  PART 2  cradle      -> holds the unit, has the J-slots, twists onto plate 1
//
//  HOW IT WORKS
//    * The device drops into the cradle from the BACK; a front lip stops it
//      sliding out the front.
//    * The wall plate's ring sits BEHIND the device. You push the cradle onto
//      the ring (lugs enter the axial slots), then twist ~25 deg to lock.
//    * The device is then trapped between the front lip and the ring face.
//      No screws into the device, and no need for its (undocumented) rear
//      hole pattern.
//
//  >>> BEFORE PRINTING: confirm the variables in "MEASURE THESE" with calipers.
//      Elecrow only publishes a 79 x 79 x 30 mm envelope -- everything else
//      below is a sensible default, not a verified dimension.
// ============================================================================

/* [Which part] */
part = "both";          // "cradle", "plate", or "both" (preview only -- export ONE at a time)

/* [MEASURE THESE on your actual unit] */
device_dia       = 79.0;   // outer diameter of the round body (across the back)
device_depth     = 30.0;   // front face -> back face
// VERIFY: does the very outer front rim NOT rotate when you turn the knob?
// If the entire front bezel spins, keep lip_overlap small (1-1.5) so the lip
// rides only the static outer-most edge, or it will rub. See README.
front_rim_static = true;

/* [USB-C port -- measure position] */
usb_angle_deg    = 270;    // angular position around the body (0 = +X, CCW)
usb_from_back    = 8.0;    // centre height of the port above the BACK face
usb_w            = 13.0;   // opening width  (around the circumference)
usb_h            = 8.0;    // opening height (along the axis), leave room for plug

/* [Fit / print tunables] */
clearance   = 0.6;    // radial gap around the device (loosen if it won't seat)
wall        = 3.0;    // cradle wall thickness
lip_thk     = 2.5;    // front retaining-lip thickness (axial)
lip_overlap = 3.0;    // how far the front lip reaches in over the rim (radial)
fn          = 180;    // smoothness
$fn = fn;

/* [Bayonet twist-lock] */
ring_h       = 14.0;  // height of the plate's bayonet ring (also the back stand-off)
lug_count    = 3;     // number of locking lugs (3 is plenty)
lug_z        = 9.0;   // lug centre height above the plate disc
lug_h        = 4.0;   // lug size along the axis
lug_w_deg    = 12;    // lug angular width (degrees)
lug_protrude = 2.0;   // how far the lug sticks out radially
slot_w       = lug_h + 0.8;  // axial slop of the slot around the lug
twist_deg    = 25;    // how far you rotate to lock
ring_fit     = 0.4;   // gap so the ring slides inside the cradle

/* [Wall plate] */
disc_thk    = 4.0;
screw_count = 2;      // 2 = horizontal pair; bump to 4 for a stud + anchors
screw_bc    = 60.0;   // bolt-circle diameter for the wall screws
screw_d     = 4.4;    // clearance hole for a ~#8 / M4 screw shank
screw_head  = 8.6;    // countersink head diameter (flat-head)
cable_hole  = 11.0;   // centre pass-through for the USB-C cable
side_notch  = true;   // open a slot in the disc edge for surface-run cable

// ----------------------------------------------------------------------------
//  DERIVED  (don't edit unless you know why)
// ----------------------------------------------------------------------------
cradle_ir    = device_dia/2 + clearance;        // inner radius (holds device)
cradle_or    = cradle_ir + wall;                // outer radius
barrel_len   = ring_h + device_depth + lip_thk; // total cradle length
front_open_r = device_dia/2 - lip_overlap;      // front opening radius
ring_or      = cradle_ir - ring_fit;            // bayonet ring outer radius
ring_ir      = ring_or - wall;                  // bayonet ring inner radius
plate_dia    = cradle_or * 2;

// ----------------------------------------------------------------------------
//  PRIMITIVE HELPERS
// ----------------------------------------------------------------------------
module tube(ri, ro, h) {
    difference() {
        cylinder(r = ro, h = h);
        translate([0, 0, -0.1]) cylinder(r = ri, h = h + 0.2);
    }
}

// A solid/cut arc segment. Profile is (radius, z); swept a_sweep deg from a_start.
module arc(r0, r1, z0, z1, a_start, a_sweep) {
    rotate([0, 0, a_start])
        rotate_extrude(angle = a_sweep)
            polygon([[r0, z0], [r1, z0], [r1, z1], [r0, z1]]);
}

// ----------------------------------------------------------------------------
//  PART 2 -- CRADLE
// ----------------------------------------------------------------------------
module jslot() {
    r0 = cradle_ir - 0.3;
    r1 = cradle_or + 0.3;
    slot_ang  = lug_w_deg + 4;             // a little wider than the lug
    axial_top = lug_z + slot_w/2 + 0.6;
    // axial entry, open at the back edge (z = 0)
    arc(r0, r1, -0.2, axial_top, -slot_ang/2, slot_ang);
    // circumferential lock channel
    arc(r0, r1, lug_z - slot_w/2, lug_z + slot_w/2, -slot_ang/2, slot_ang + twist_deg);
}

module usb_cut() {
    rotate([0, 0, usb_angle_deg])
        translate([cradle_ir + wall/2, 0, usb_from_back])
            minkowski() {
                cube([wall + 2, usb_w - 3, usb_h - 3], center = true);
                rotate([0, 90, 0]) cylinder(r = 1.5, h = 0.01, center = true);
            }
}

module cradle() {
    difference() {
        union() {
            tube(cradle_ir, cradle_or, barrel_len);                 // main barrel
            translate([0, 0, barrel_len - lip_thk])                 // front lip
                tube(front_open_r, cradle_ir + 0.01, lip_thk);
        }
        for (i = [0 : lug_count - 1])
            rotate([0, 0, i * 360 / lug_count]) jslot();            // bayonet slots
        usb_cut();                                                  // USB-C opening
    }
}

// ----------------------------------------------------------------------------
//  PART 1 -- WALL PLATE
// ----------------------------------------------------------------------------
module lug() {
    arc(ring_or - 0.1, ring_or + lug_protrude,
        lug_z - lug_h/2, lug_z + lug_h/2,
        -lug_w_deg/2, lug_w_deg);
}

module ring_with_lugs() {
    tube(ring_ir, ring_or, ring_h);
    for (i = [0 : lug_count - 1])
        rotate([0, 0, i * 360 / lug_count]) lug();
}

module screw_hole() {
    translate([0, 0, -0.1]) cylinder(d = screw_d, h = disc_thk + 0.2);   // shank
    translate([0, 0, disc_thk - (screw_head - screw_d)/2])               // countersink
        cylinder(d1 = screw_d, d2 = screw_head, h = (screw_head - screw_d)/2 + 0.1);
}

module wall_plate() {
    difference() {
        union() {
            cylinder(r = plate_dia/2, h = disc_thk);                 // disc
            translate([0, 0, disc_thk]) ring_with_lugs();            // bayonet ring
        }
        translate([0, 0, -0.1]) cylinder(d = cable_hole, h = disc_thk + ring_h + 1);
        for (i = [0 : screw_count - 1])
            rotate([0, 0, i * 360 / screw_count + 90])
                translate([screw_bc/2, 0, 0]) screw_hole();
        if (side_notch)
            translate([0, 0, -0.1])
                rotate([0, 0, 90])
                    translate([0, -cable_hole/2, 0])
                        cube([plate_dia/2 + 1, cable_hole, disc_thk + 0.2]);
    }
}

// ----------------------------------------------------------------------------
//  LAYOUT
// ----------------------------------------------------------------------------
if (part == "cradle") {
    cradle();
} else if (part == "plate") {
    wall_plate();
} else {
    wall_plate();
    translate([0, 0, disc_thk + ring_h + device_depth + lip_thk + 30])
        rotate([180, 0, 0]) cradle();
}
