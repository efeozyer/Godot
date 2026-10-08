#pragma once

#include "scene/2d/node_2d.h"

class GameRoot : public Node2D {
	GDCLASS(GameRoot, Node2D);

	Node2D *child = nullptr;
	real_t spin_speed = 1.0;

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void set_spin_speed(real_t p_speed);
	real_t get_spin_speed() const;
};
