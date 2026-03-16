# FF8 Walkmesh vs. Entity Coordinates

In FF8 the field **walkmesh** (“.id” file) is a mesh of 3D triangles (each *sect_t* has 3 vertices, each vertex an (X,Y,Z) triple of 16-bit ints)【16†L174-L182】.  Entity objects store their X,Y position in the 32-bit fields at offsets 0x190 and 0x194 of the entity struct【62†L203-L211】.  Those int32 values use a fixed‑point scale (divide by 4096 to get the actual game units).  In fields where the walkmesh is essentially flat (e.g. **bgroom_1**, constant Z), the raw walkmesh X,Y values (scaled) exactly match the entity X,Y values.  But in fields with varying height (like **bg2f_1**, Z from ~484 to 10413), the entity X,Y no longer equal the raw walkmesh X,Y – they have clearly been transformed (“projected”) onto a 2D plane.

# Camera Matrix in Field Files

Each field archive contains a **.ca** (camera) file.  This holds a camera **view matrix**: three orthonormal world‐space vectors plus a camera position and a zoom factor.  In other words, the file gives an (X,Y,Z) axis‐vector for the camera’s right direction, one for up/down, and one for forward, and a camera origin.  (In FF7 this was Section 2 of the field file【83†L52-L61】.)  In PC FF8 these vectors are stored as signed 16‐bit fixed‑point (divide by 4096) and are orthonormal【83†L78-L81】.  The axis‐vectors (and camera pos) define the orientation of the camera used to render the background.

# Projecting Walkmesh into 2D

The engine uses that camera orientation to convert the 3D walkmesh into a 2D “ground” coordinate system.  In practice, when the field loads, FF8’s walkmesh loader reads each (X,Y,Z) walkmesh vertex and applies the camera’s rotation axes.  That is, for each walkmesh point **P**, the engine computes new coordinates: 
```
X' = VX·P = VX.x*P.x + VX.y*P.y + VX.z*P.z
Y' = VY·P = VY.x*P.x + VY.y*P.y + VY.z*P.z
``` 
where **VX** and **VY** are the camera’s right‑ and down‑axis vectors (from the .ca data).  These dot‑products give the point’s location in the camera’s plane (up to scale by the fixed‐point factor).  Those X',Y' (divided by 4096) become the “world” X,Y used for entities.  In other words, the engine rotates the 3D mesh by the camera’s orientation and flattens it onto 2D.  The camera’s forward axis **VZ** can give height/depth if needed (stored in entity’s Z field), but **VX** and **VY** drive the X,Y output.  

# Source of the Transform

This transform is implemented in FF8’s code (in FF8_EN.exe).  The camera axes are read from the **.ca** file (which matches the FF7 camera format【83†L52-L61】) and used as a rotation matrix.  There is no separate “2D coordinate” table in .mim/.map/.one for this – the information is derived from the camera data at load time.  (Indeed, the Qhimm reverse‑engineering shows entity.x_pos/Y_pos are exactly at offsets 0x190/0x194【62†L203-L211】, implying they are written by the loader after transforming the walkmesh.)  In summary, the walkmesh is rotated by the field’s camera matrix so that entities use the camera‑projected 2D coordinates.  This matches observations: flat scenes (no tilt) require no rotation, but tilted scenes use the camera axes to align the walkmesh to the background【62†L203-L211】【83†L52-L61】.

**Sources:** FF8 field file formats and engine structure【16†L174-L182】【62†L203-L211】, and the FF7/FF8 camera‐matrix description (3 axis vectors + position)【83†L52-L61】【83†L78-L81】, which explains how the camera axes are encoded and used for projection.