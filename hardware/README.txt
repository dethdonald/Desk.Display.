Waveshare ESP32-S3-Touch-AMOLED-2.16 (cased version) - 3D model
Units: millimetres. Origin = centre of the BACK face. Screen faces +Z, buttons on top (+Y), USB-C on the bottom (-Y), TF slot on the left (-X).

Files
  .step  CAD assembly (case, glass, active screen area, buttons, screws) - best for Fusion 360 / FreeCAD / Onshape / SolidWorks
  .stl   single solid - for slicers and quick fit checks
  .obj   same mesh as the STL
  make_model.py  CadQuery source (edit the numbers at the top and re-run)

From Waveshare's dimension drawing (exact)
  Body 46.00 x 46.00 x 22.50, vertical corners R5.80
  Front glass 43.30 x 43.30; active display 38.99 x 38.99, 2.15 in from the glass edge
  Top buttons: 3 x D5.30, 10.00 apart (middle one centred), 9.60 behind the front face
  USB-C: centred left-right on the bottom, 8.60 behind the front face
  TF card slot: left side, 8.30 behind the front face
  Back screws: 37.00 apart left-right (4.50 from the sides), 34.00 apart up-down (6.00 from top/bottom)

Estimated (not on the drawing - measure your unit if a fit is tight)
  Button protrusion 1.2, glass corner radius 4.0, 0.8 edge rounding on front/back,
  USB-C opening 9.0 x 3.4, TF slot 12.6 x 1.4, mic hole positions.
For a case/stand, leave 0.2-0.3 mm clearance per side for FDM prints.
