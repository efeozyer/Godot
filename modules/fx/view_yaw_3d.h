#pragma once

#include "scene/3d/node_3d.h"

// Turns around the Y axis every frame so that its -Z axis follows the horizontal direction the
// current camera is looking in. This is the "view fix" of a mesh effect part
// (CFXPartMesh::Rotate): the mesh keeps its tilt but always shows the camera the same side.
class ViewYaw3D : public Node3D {
	GDCLASS(ViewYaw3D, Node3D);

protected:
	void _notification(int p_what);
	static void _bind_methods();
};
