// Support incline pour HW-364B (ESP8266 + OLED)
// Dimensions carte : 59 x 31 mm
// Trous : 3 mm, entraxe 54 x 25 mm
// Inclinaison : 45 degres

// === PARAMETRES MODIFIABLES ===
pcb_width = 59;           // Largeur PCB (mm)
pcb_height = 31;          // Hauteur PCB (mm)
hole_diameter = 3;        // Diametre des trous de fixation
hole_spacing_h = 54;      // Entraxe horizontal des trous
hole_spacing_v = 25;      // Entraxe vertical des trous
angle = 45;               // Angle d'inclinaison (degres)

// Parametres du support
wall_thickness = 3;       // Epaisseur des parois
pillar_diameter = 7;      // Diametre des piliers de fixation
pillar_height = 5;        // Hauteur des piliers (ecart PCB/support)
base_depth = 30;          // Profondeur de la base
margin = 5;               // Marge autour du PCB

// === CALCULS ===
support_width = pcb_width + 2 * margin;
support_height = pcb_height + 2 * margin;

// Position des trous (centre du PCB comme origine)
hole_offset_h = hole_spacing_h / 2;
hole_offset_v = hole_spacing_v / 2;

// Profondeur occupee par le plateau incline (projection horizontale)
plate_depth = cos(angle) * support_height;

// === MODULES ===

// Pilier avec teton de fixation
peg_diameter = 2.8;       // Diametre teton (legèrement < 3mm pour insertion)
peg_height = 4;           // Hauteur du teton

module pillar(h, outer_d) {
    // Base du pilier
    cylinder(h = h, d = outer_d, $fn = 32);
    // Teton
    translate([0, 0, h])
        cylinder(h = peg_height, d = peg_diameter, $fn = 32);
}

// Plateau incline avec les 4 piliers
module inclined_plate() {
    difference() {
        // Plaque de base
        translate([-support_width/2, 0, 0])
            cube([support_width, support_height, wall_thickness]);

        // Trou central traversant (juste un cadre autour des piliers)
        hole_w = hole_spacing_h - pillar_diameter;
        hole_h = hole_spacing_v - pillar_diameter;
        translate([-hole_w/2, support_height/2 - hole_h/2, -0.1])
            cube([hole_w, hole_h, wall_thickness + 0.2]);
    }

    // Les 4 piliers de fixation
    translate([-hole_offset_h, support_height/2 - hole_offset_v, wall_thickness])
        pillar(pillar_height, pillar_diameter);
    translate([hole_offset_h, support_height/2 - hole_offset_v, wall_thickness])
        pillar(pillar_height, pillar_diameter);
    translate([-hole_offset_h, support_height/2 + hole_offset_v, wall_thickness])
        pillar(pillar_height, pillar_diameter);
    translate([hole_offset_h, support_height/2 + hole_offset_v, wall_thickness])
        pillar(pillar_height, pillar_diameter);
}

// Triangle evide (avec trou central)
module hollow_triangle(depth, height, thickness, hole_margin) {
    difference() {
        // Triangle plein
        linear_extrude(thickness)
            polygon([
                [0, 0],
                [depth, 0],
                [depth, height],
                [0, 0]
            ]);

        // Trou triangulaire (plus petit)
        translate([hole_margin * 1.5, hole_margin, -0.1])
            linear_extrude(thickness + 0.2)
            polygon([
                [0, 0],
                [depth - hole_margin * 3, 0],
                [depth - hole_margin * 3, height - hole_margin * 3],
                [0, 0]
            ]);
    }
}

// Base avec renforts integres
module base_with_supports() {
    // Hauteur du point haut du plateau
    top_height = sin(angle) * support_height + wall_thickness;
    triangle_height = top_height - wall_thickness;

    // Plaque de base horizontale avec trou
    difference() {
        translate([-support_width/2, 0, 0])
            cube([support_width, base_depth, wall_thickness]);

        // Trou dans la base (juste un cadre)
        base_hole_w = support_width - wall_thickness * 2;
        base_hole_d = base_depth - wall_thickness * 2;
        translate([-base_hole_w/2, wall_thickness, -0.1])
            cube([base_hole_w, base_hole_d, wall_thickness + 0.2]);
    }

    // Renfort lateral gauche (triangle evide)
    translate([-support_width/2, 0, wall_thickness])
        rotate([90, 0, 90])
        hollow_triangle(plate_depth, triangle_height, wall_thickness, 3);

    // Renfort lateral droit (triangle evide)
    translate([support_width/2 - wall_thickness, 0, wall_thickness])
        rotate([90, 0, 90])
        hollow_triangle(plate_depth, triangle_height, wall_thickness, 3);

    // Rebord avant (liaison base-plateau)
    translate([-support_width/2, -wall_thickness, 0])
        cube([support_width, wall_thickness, wall_thickness * 2]);
}

// === ASSEMBLAGE FINAL ===
module stand() {
    // Base avec renforts lateraux
    base_with_supports();

    // Plateau incline (pose sur la base, incline vers l'arriere)
    translate([0, 0, wall_thickness])
        rotate([angle, 0, 0])
        inclined_plate();
}

// Rendu
stand();

// Apercu de la position du PCB (decommenter pour visualiser)
// %translate([0, 0, wall_thickness])
//     rotate([angle, 0, 0])
//     translate([0, support_height/2, wall_thickness + pillar_height])
//     cube([pcb_width, pcb_height, 2], center = true);
