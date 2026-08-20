#ifndef SKIPPY_XD_AABB_H
#define SKIPPY_XD_AABB_H

#include <stdbool.h>
#include <stddef.h>

/*
 * Translation-only AABB simulation for the Cosmos window layout.
 *
 * Coordinates are floating-point pixels.  A body position is its center;
 * width and height are fixed.  The world adds the same padding around every
 * body so that contact also enforces the configured window gap.
 */

typedef struct AabbWorld AabbWorld;
typedef unsigned int AabbBodyId;

#define AABB_BODY_INVALID ((AabbBodyId) -1)

typedef struct {
	float center_x;
	float center_y;
	float width;
	float height;
	/* Zero makes a body immovable.  Equal values give equal movement. */
	float inverse_mass;
} AabbBodyDef;

/*
 * Result of one fixed simulation step.  Cosmos uses max_penetration to
 * decide whether expansion is complete and max_movement to decide whether
 * contraction has settled.  The counters are useful for diagnostics.
 */
typedef struct {
	float max_penetration;
	float max_movement;
	unsigned int contacts;
	unsigned int substeps;
} AabbStepResult;

/*
 * capacity is fixed for the lifetime of the world.  coordinate_scale_x/y
 * describe the initial Cosmos extent.  Public coordinates remain pixels;
 * a backend may use the ratio internally to make collision-axis choice
 * independent of the display aspect ratio.
 */
AabbWorld *aabb_world_create(size_t capacity, float padding,
		float coordinate_scale_x, float coordinate_scale_y);
void aabb_world_destroy(AabbWorld *world);

AabbBodyId aabb_world_add_body(AabbWorld *world,
		const AabbBodyDef *definition);
size_t aabb_world_body_count(const AabbWorld *world);

bool aabb_body_get_position(const AabbWorld *world, AabbBodyId body,
		float *center_x, float *center_y);
bool aabb_body_set_position(AabbWorld *world, AabbBodyId body,
		float center_x, float center_y);

/* Drive is the desired velocity, in pixels per simulation-time unit. */
bool aabb_body_set_drive(AabbWorld *world, AabbBodyId body,
		float velocity_x, float velocity_y);
void aabb_world_clear_drives(AabbWorld *world);

/* Largest remaining padded-AABB penetration, measured in pixels. */
float aabb_world_max_penetration(const AabbWorld *world);

/* Advance by dt.  Cosmos uses a fixed dt of 1.0. */
AabbStepResult aabb_world_step(AabbWorld *world, float dt);

#endif
