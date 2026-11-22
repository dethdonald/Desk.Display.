"""Waveshare ESP32-S3-Touch-AMOLED-2.16 (cased version) - CAD model from Waveshare's dimension drawing.

Units: mm. Origin: centre of the BACK face. +Z points out of the screen, +Y up (button side), +X right.
Measured (from drawing): 46 x 46 x 22.5 body, R5.8 corners, 43.3 glass, 38.99 active area (2.15 inset),
3 buttons D5.3 @ 10 mm pitch 9.6 mm behind the front face, USB-C centred 8.6 mm behind the front,
TF slot 8.3 mm behind the front (left side), back screws 37 x 34 mm (4.5 / 6.0 from edges).
Estimated (not on the drawing): button protrusion, glass corner radius, port opening sizes, mic holes.
"""
import cadquery as cq

W = H = 46.0
D = 22.5
R_CORNER = 5.8
GLASS = 43.30
GLASS_R = 4.0          # estimated
GLASS_T = 0.6          # estimated glass thickness (flush with the front)
ACTIVE = 38.99
BTN_D = 5.30
BTN_PITCH = 10.0
BTN_FROM_FRONT = 9.60
BTN_PROTRUDE = 1.2     # estimated
USB_FROM_FRONT = 8.60
USB_W, USB_H = 9.0, 3.4  # typical USB-C receptacle opening (estimated)
TF_FROM_FRONT = 8.30
TF_LEN, TF_W = 12.6, 1.4  # read from drawing proportions (estimated)
SCREW_DX, SCREW_DY = 37.0, 34.0
FRONT_Z = D

# --- case body
case = (cq.Workplane("XY").rect(W, H).extrude(D)
        .edges("|Z").fillet(R_CORNER))
try:
    case = case.faces(">Z or <Z").edges().fillet(0.8)
except Exception:
    pass

# glass pocket so the glass body sits flush
pocket = (cq.Workplane("XY").workplane(offset=FRONT_Z - GLASS_T)
          .sketch().rect(GLASS, GLASS).vertices().fillet(GLASS_R).finalize().extrude(GLASS_T + 1))
case = case.cut(pocket)

# USB-C opening (bottom face, -Y), centred in X
usb = (cq.Workplane("XZ", origin=(0, -H / 2 - 1, FRONT_Z - USB_FROM_FRONT))
       .slot2D(USB_W, USB_H, 0).extrude(-8))
case = case.cut(usb)

# TF card slot (left face, -X), long side along Y
tf = (cq.Workplane("YZ", origin=(-W / 2 - 1, 0, FRONT_Z - TF_FROM_FRONT))
      .rect(TF_LEN, TF_W).extrude(4))
case = case.cut(tf)

# button bores (top face, +Y)
for i in (-1, 0, 1):
    bore = (cq.Workplane("XZ", origin=(i * BTN_PITCH, H / 2 + 1, FRONT_Z - BTN_FROM_FRONT))
            .circle(BTN_D / 2 + 0.15).extrude(3))
    case = case.cut(bore)

# mic holes (estimated positions from the drawing)
mic1 = cq.Workplane("YZ", origin=(-W / 2 - 1, H / 2 - 10.8, FRONT_Z - TF_FROM_FRONT)).circle(0.5).extrude(3)
mic2 = cq.Workplane("XZ", origin=(W / 2 - 5.2, -H / 2 - 1, FRONT_Z - 19.4)).circle(0.5).extrude(-3)
case = case.cut(mic1).cut(mic2)

# back screws: shallow counterbores + heads
for sx in (-1, 1):
    for sy in (-1, 1):
        cb = cq.Workplane("XY", origin=(sx * SCREW_DX / 2, sy * SCREW_DY / 2, -0.01)).circle(1.9).extrude(0.5)
        case = case.cut(cb)

# --- separate bodies
glass = (cq.Workplane("XY").workplane(offset=FRONT_Z - GLASS_T)
         .sketch().rect(GLASS, GLASS).vertices().fillet(GLASS_R).finalize().extrude(GLASS_T))
active = (cq.Workplane("XY").workplane(offset=FRONT_Z - 0.05)
          .sketch().rect(ACTIVE, ACTIVE).vertices().fillet(2.0).finalize().extrude(0.05))
buttons = None
for i in (-1, 0, 1):
    b = (cq.Workplane("XZ", origin=(i * BTN_PITCH, H / 2 + BTN_PROTRUDE, FRONT_Z - BTN_FROM_FRONT))
         .circle(BTN_D / 2).extrude(BTN_PROTRUDE + 2.5))
    buttons = b if buttons is None else buttons.union(b)
screws = None
for sx in (-1, 1):
    for sy in (-1, 1):
        s = cq.Workplane("XY", origin=(sx * SCREW_DX / 2, sy * SCREW_DY / 2, 0.0)).circle(1.6).extrude(0.5)
        screws = s if screws is None else screws.union(s)

asm = cq.Assembly(name="ESP32-S3-Touch-AMOLED-2.16")
asm.add(case, name="case", color=cq.Color(0.94, 0.94, 0.94))
asm.add(glass, name="glass", color=cq.Color(0.05, 0.05, 0.06))
asm.add(active, name="active_area", color=cq.Color(0.0, 0.0, 0.0))
asm.add(buttons, name="buttons", color=cq.Color(0.9, 0.9, 0.9))
asm.add(screws, name="screws", color=cq.Color(0.6, 0.6, 0.6))

out = "/home/claude/dims/out"
import os
os.makedirs(out, exist_ok=True)
asm.save(f"{out}/Waveshare-ESP32-S3-Touch-AMOLED-2.16.step")
solid = case.union(glass).union(buttons).union(screws)
cq.exporters.export(solid, f"{out}/Waveshare-ESP32-S3-Touch-AMOLED-2.16.stl", tolerance=0.02, angularTolerance=0.1)
bb = solid.val().BoundingBox()
print("bbox mm: x %.2f..%.2f  y %.2f..%.2f  z %.2f..%.2f" % (bb.xmin, bb.xmax, bb.ymin, bb.ymax, bb.zmin, bb.zmax))
