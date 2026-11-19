# Bone animation items (ban)

Parser: `src/ovl/bone_anim.hpp`. An item holds, per bone, a position track and a rotation track. Each key
is a time in seconds and three floats.

## Rotation keys

A rotation key is a **rotation vector**: the direction of (x, y, z) is the axis and its length is the angle
in radians. It is not three Euler angles, so there is no axis order to apply. A key of (a, 0, 0) is a
turn of a radians about x. Converted to a row-vector matrix, the key equals the rest pose stored in the
bone shape on thousands of bones (the animations' first key equals the bone's rest local transform).

## Interpolation

Not confirmed. We do not know whether the engine blends between rotation keys by interpolating the vector,
the matrix or a quaternion. Playback here blends along the shortest arc between the two rotations, which is a fair match when
the turn between two keys is small, as it is in shipped animations.
