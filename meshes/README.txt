Place 3D models here to use as note shapes instead of the default cube.

Accepted formats
----------------
  .obj  (+ optional .mtl)
  .glb / .gltf
  .iqm
  .vox
  .m3d

The model is automatically scaled to note size and centered on its origin.
A bright or white model works best — the active palette color tints it in-game.

Limit: 64 models maximum in this folder.

.obj constraint — triangulation required
-----------------------------------------
Faces with more than 4 vertices (n-gons) will cause the OBJ loader to crash.
In Blender, enable "Triangulated Mesh" at export, or add a Triangulate modifier
before exporting. The simplest and most reliable option is to export as .glb,
which is always triangulated.

Selecting a mesh in-game
--------------------------
Main menu -> Options -> "Note shape"
The list shows "Cube (default)" followed by each file in this folder.

Performance tip (low-end PC)
------------------------------
Prefer low-poly models. A cube is 12 triangles; beyond ~200 triangles,
performance may drop noticeably on dense maps.
