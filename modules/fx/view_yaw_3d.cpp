#include "view_yaw_3d.h"

#include "scene/3d/camera_3d.h"
#include "scene/main/viewport.h"

void ViewYaw3D::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			set_process_internal(true);
		} break;

		case NOTIFICATION_INTERNAL_PROCESS: {
			const Camera3D *camera = get_viewport()->get_camera_3d();
			if (!camera) {
				return;
			}

			Vector3 forward = -camera->get_global_basis().get_column(2);
			forward.y = 0;
			if (forward.is_zero_approx()) {
				return;
			}

			// The yaw is set relative to the parent so that the effect as a whole can still be turned.
			real_t yaw = Math::atan2(-forward.x, -forward.z);
			const Node3D *parent = get_parent_node_3d();
			if (parent) {
				yaw -= parent->get_global_basis().get_euler().y;
			}
			set_rotation(Vector3(0, yaw, 0));
		} break;
	}
}

void ViewYaw3D::_bind_methods() {
}
