#include "aabb.h"

#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef CFG_CHIPMUNK
#include <chipmunk/chipmunk.h>
#endif

typedef struct {
	float width;
	float height;
	float inverse_mass;
	float drive_x;
	float drive_y;
#ifdef CFG_CHIPMUNK
	cpBody *chipmunk_body;
	cpShape *chipmunk_shape;
	float step_start_x;
	float step_start_y;
#else
	float x;
	float y;
	float velocity_x;
	float velocity_y;
#endif
} AabbBody;

struct AabbWorld {
	AabbBody *bodies;
	size_t count;
	size_t capacity;
	float padding;
#ifdef CFG_CHIPMUNK
	cpSpace *chipmunk_space;
	float coordinate_y_scale;
#else
	float *step_start_x;
	float *step_start_y;
	/*
	 * Persistent contact orientation for each ordered body pair:
	 *   +/-1 = X contact, +/-2 = Y contact.
	 * The sign says which side the second body occupies.
	 */
	signed char *contact_axis;
#endif
};

enum {
	AABB_MAX_SUBSTEPS = 128
};

static float
half_width(const AabbWorld *world, const AabbBody *body)
{
	return (body->width + world->padding) / 2.0f;
}

static float
half_height(const AabbWorld *world, const AabbBody *body)
{
	return (body->height + world->padding) / 2.0f;
}

#ifdef CFG_CHIPMUNK

enum {
	AABB_CHIPMUNK_SOLVER_ITERATIONS = 30
};

static const float AABB_CHIPMUNK_COLLISION_SLOP = 0.001f;

static unsigned int
count_contacts(const AabbWorld *world)
{
	unsigned int count = 0;

	for (size_t a_id = 0; a_id < world->count; a_id++) {
		const AabbBody *a = &world->bodies[a_id];
		cpVect a_position = cpBodyGetPosition(a->chipmunk_body);

		for (size_t b_id = a_id + 1; b_id < world->count; b_id++) {
			const AabbBody *b = &world->bodies[b_id];
			cpVect b_position = cpBodyGetPosition(b->chipmunk_body);
			float required_x = half_width(world, a) + half_width(world, b);
			float required_y = half_height(world, a) + half_height(world, b);
			float separation_x = fabsf((float) (b_position.x - a_position.x))
				- required_x;
			float separation_y = fabsf((float) (b_position.y - a_position.y)
				/ world->coordinate_y_scale)
				- required_y;

			if (separation_x <= AABB_CHIPMUNK_COLLISION_SLOP
					&& separation_y <= AABB_CHIPMUNK_COLLISION_SLOP)
				count++;
		}
	}

	return count;
}

AabbWorld *
aabb_world_create(size_t capacity, float padding,
		float coordinate_scale_x, float coordinate_scale_y)
{
	if (capacity == 0 || capacity > UINT_MAX
			|| capacity > SIZE_MAX / sizeof(AabbBody))
		return NULL;

	AabbWorld *world = calloc(1, sizeof(*world));

	if (!world)
		return NULL;

	world->bodies = calloc(capacity, sizeof(*world->bodies));
	world->chipmunk_space = cpSpaceNew();

	if (!world->bodies || !world->chipmunk_space) {
		aabb_world_destroy(world);
		return NULL;
	}

	float y_scale = coordinate_scale_x > 0 && coordinate_scale_y > 0
		? coordinate_scale_x / coordinate_scale_y : 1.0f;
	if (!isfinite(y_scale) || y_scale <= 0)
		y_scale = 1.0f;

	world->coordinate_y_scale = y_scale;
	cpSpaceSetIterations(world->chipmunk_space,
			AABB_CHIPMUNK_SOLVER_ITERATIONS);
	cpSpaceSetCollisionSlop(world->chipmunk_space,
			AABB_CHIPMUNK_COLLISION_SLOP * fminf(1.0f, y_scale));
	world->capacity = capacity;
	world->padding = padding > 0 ? padding : 0;
	return world;
}

void
aabb_world_destroy(AabbWorld *world)
{
	if (!world)
		return;

	if (world->chipmunk_space) {
		for (size_t i = 0; i < world->count; i++) {
			AabbBody *body = &world->bodies[i];

			if (body->chipmunk_shape) {
				if (cpShapeGetSpace(body->chipmunk_shape)
						== world->chipmunk_space)
					cpSpaceRemoveShape(world->chipmunk_space,
							body->chipmunk_shape);
				cpShapeFree(body->chipmunk_shape);
			}
			if (body->chipmunk_body) {
				if (cpBodyGetSpace(body->chipmunk_body)
						== world->chipmunk_space)
					cpSpaceRemoveBody(world->chipmunk_space,
							body->chipmunk_body);
				cpBodyFree(body->chipmunk_body);
			}
		}

		cpSpaceFree(world->chipmunk_space);
	}

	free(world->bodies);
	free(world);
}

AabbBodyId
aabb_world_add_body(AabbWorld *world, const AabbBodyDef *definition)
{
	if (!world || !definition || world->count >= world->capacity
			|| definition->width <= 0 || definition->height <= 0)
		return AABB_BODY_INVALID;

	bool movable = definition->inverse_mass > 0;
	cpFloat mass = movable ? 1.0 / definition->inverse_mass : INFINITY;
	cpBody *chipmunk_body = movable
		? cpBodyNew(mass, INFINITY) : cpBodyNewStatic();

	if (!chipmunk_body)
		return AABB_BODY_INVALID;

	cpBodySetPosition(chipmunk_body,
			cpv(definition->center_x,
				definition->center_y * world->coordinate_y_scale));
	if (movable)
		cpSpaceAddBody(world->chipmunk_space, chipmunk_body);

	cpShape *shape = cpBoxShapeNew(chipmunk_body,
			definition->width + world->padding,
			(definition->height + world->padding)
				* world->coordinate_y_scale, 0);

	if (!shape) {
		if (cpBodyGetSpace(chipmunk_body) == world->chipmunk_space)
			cpSpaceRemoveBody(world->chipmunk_space, chipmunk_body);
		cpBodyFree(chipmunk_body);
		return AABB_BODY_INVALID;
	}

	cpShapeSetFriction(shape, 0);
	cpShapeSetElasticity(shape, 0);
	cpSpaceAddShape(world->chipmunk_space, shape);

	size_t id = world->count++;
	AabbBody *body = &world->bodies[id];
	body->chipmunk_body = chipmunk_body;
	body->chipmunk_shape = shape;
	body->width = definition->width;
	body->height = definition->height;
	body->inverse_mass = movable ? definition->inverse_mass : 0;

	return (AabbBodyId) id;
}

size_t
aabb_world_body_count(const AabbWorld *world)
{
	return world ? world->count : 0;
}

bool
aabb_body_get_position(const AabbWorld *world, AabbBodyId body,
		float *center_x, float *center_y)
{
	if (!world || body >= world->count || !center_x || !center_y)
		return false;

	cpVect position = cpBodyGetPosition(world->bodies[body].chipmunk_body);
	*center_x = (float) position.x;
	*center_y = (float) position.y / world->coordinate_y_scale;
	return true;
}

bool
aabb_body_set_position(AabbWorld *world, AabbBodyId body,
		float center_x, float center_y)
{
	if (!world || body >= world->count)
		return false;

	AabbBody *aabb_body = &world->bodies[body];
	cpBodySetPosition(aabb_body->chipmunk_body,
			cpv(center_x, center_y * world->coordinate_y_scale));
	cpSpaceReindexShapesForBody(world->chipmunk_space,
			aabb_body->chipmunk_body);
	return true;
}

bool
aabb_body_set_drive(AabbWorld *world, AabbBodyId body,
		float velocity_x, float velocity_y)
{
	if (!world || body >= world->count)
		return false;

	world->bodies[body].drive_x = velocity_x;
	world->bodies[body].drive_y = velocity_y;
	return true;
}

void
aabb_world_clear_drives(AabbWorld *world)
{
	if (!world)
		return;

	for (size_t i = 0; i < world->count; i++) {
		AabbBody *body = &world->bodies[i];
		body->drive_x = 0;
		body->drive_y = 0;
		if (body->inverse_mass > 0)
			cpBodySetVelocity(body->chipmunk_body, cpvzero);
	}
}

float
aabb_world_max_penetration(const AabbWorld *world)
{
	float maximum = 0;

	if (!world)
		return maximum;

	for (size_t a_id = 0; a_id < world->count; a_id++) {
		const AabbBody *a = &world->bodies[a_id];
		cpVect a_position = cpBodyGetPosition(a->chipmunk_body);

		for (size_t b_id = a_id + 1; b_id < world->count; b_id++) {
			const AabbBody *b = &world->bodies[b_id];
			cpVect b_position = cpBodyGetPosition(b->chipmunk_body);
			float required_x = half_width(world, a) + half_width(world, b);
			float required_y = half_height(world, a) + half_height(world, b);
			float overlap_x = required_x
				- fabsf((float) (b_position.x - a_position.x));
			float overlap_y = required_y
				- fabsf((float) (b_position.y - a_position.y)
					/ world->coordinate_y_scale);

			if (overlap_x > 0 && overlap_y > 0) {
				float penetration = fminf(overlap_x, overlap_y);

				if (penetration > maximum)
					maximum = penetration;
			}
		}
	}

	return maximum;
}

AabbStepResult
aabb_world_step(AabbWorld *world, float dt)
{
	AabbStepResult result = {0};

	if (!world || world->count == 0 || dt <= 0)
		return result;

	float max_speed = 0;
	float minimum_extent = FLT_MAX;

	for (size_t i = 0; i < world->count; i++) {
		AabbBody *body = &world->bodies[i];
		cpVect position = cpBodyGetPosition(body->chipmunk_body);
		body->step_start_x = (float) position.x;
		body->step_start_y = (float) position.y
			/ world->coordinate_y_scale;

		if (body->inverse_mass > 0) {
			cpBodySetVelocity(body->chipmunk_body,
					cpv(body->drive_x,
						body->drive_y * world->coordinate_y_scale));
		}

		float speed = hypotf(body->drive_x, body->drive_y);
		float extent = fminf(body->width + world->padding,
				body->height + world->padding);

		if (speed > max_speed)
			max_speed = speed;
		if (extent < minimum_extent)
			minimum_extent = extent;
	}

	float max_translation = fmaxf(1.0f, minimum_extent * 0.2f);
	unsigned int substeps = (unsigned int)
		ceilf(max_speed * dt / max_translation);

	if (substeps < 1)
		substeps = 1;
	if (substeps > AABB_MAX_SUBSTEPS)
		substeps = AABB_MAX_SUBSTEPS;

	result.substeps = substeps;
	cpFloat substep_dt = dt / (cpFloat) substeps;

	for (unsigned int step = 0; step < substeps; step++)
		cpSpaceStep(world->chipmunk_space, substep_dt);

	for (size_t i = 0; i < world->count; i++) {
		AabbBody *body = &world->bodies[i];
		cpVect position = cpBodyGetPosition(body->chipmunk_body);
		float movement = hypotf((float) position.x - body->step_start_x,
				(float) position.y / world->coordinate_y_scale
					- body->step_start_y);

		if (movement > result.max_movement)
			result.max_movement = movement;
	}

	result.max_penetration = aabb_world_max_penetration(world);
	result.contacts = count_contacts(world);
	return result;
}

#else

enum {
	AABB_SOLVER_ITERATIONS = 16
};

static const float AABB_POSITION_SLOP = 0.001f;
static const float AABB_CONTACT_RELEASE_SLOP = 0.5f;

static size_t
pair_index(const AabbWorld *world, size_t a, size_t b)
{
	return a * world->capacity + b;
}

static signed char
choose_contact_axis(const AabbWorld *world,
		size_t a_id, size_t b_id,
		float substep_dt)
{
	const AabbBody *a = &world->bodies[a_id];
	const AabbBody *b = &world->bodies[b_id];
	float required_x = half_width(world, a) + half_width(world, b);
	float required_y = half_height(world, a) + half_height(world, b);
	float dx = b->x - a->x;
	float dy = b->y - a->y;
	float dvx = b->velocity_x - a->velocity_x;
	float dvy = b->velocity_y - a->velocity_y;

	/*
	 * Predict which relationship the current drive is trying to establish.
	 * This is important for grid scatter: coincident bodies receive different
	 * drives, and the first contact records that intended row/column relation.
	 */
	float score_x = fabsf(dx + dvx * substep_dt) / required_x;
	float score_y = fabsf(dy + dvy * substep_dt) / required_y;
	bool solve_x;

	if (fabsf(score_x - score_y) > 1.0e-4f)
		solve_x = score_x > score_y;
	else {
		float penetration_x = required_x - fabsf(dx);
		float penetration_y = required_y - fabsf(dy);

		if (fabsf(penetration_x - penetration_y) > 1.0e-4f)
			solve_x = penetration_x < penetration_y;
		else
			solve_x = ((a_id ^ b_id) & 1u) == 0;
	}

	if (solve_x) {
		float direction = fabsf(dx) > 1.0e-5f ? dx : dvx;

		if (fabsf(direction) <= 1.0e-5f)
			direction = ((a_id + b_id) & 1u) ? 1.0f : -1.0f;

		return direction > 0 ? 1 : -1;
	}

	float direction = fabsf(dy) > 1.0e-5f ? dy : dvy;

	if (fabsf(direction) <= 1.0e-5f)
		direction = ((a_id + b_id) & 1u) ? -1.0f : 1.0f;

	return direction > 0 ? 2 : -2;
}

static bool
solve_contact(AabbWorld *world,
		size_t a_id, size_t b_id,
		float substep_dt)
{
	AabbBody *a = &world->bodies[a_id];
	AabbBody *b = &world->bodies[b_id];
	float required_x = half_width(world, a) + half_width(world, b);
	float required_y = half_height(world, a) + half_height(world, b);
	float dx = b->x - a->x;
	float dy = b->y - a->y;
	float overlap_x = required_x - fabsf(dx);
	float overlap_y = required_y - fabsf(dy);

	if (overlap_x <= 0 || overlap_y <= 0)
		return false;

	size_t index = pair_index(world, a_id, b_id);
	signed char axis = world->contact_axis[index];

	if (axis == 0) {
		axis = choose_contact_axis(world, a_id, b_id, substep_dt);
		world->contact_axis[index] = axis;
	}

	float inverse_mass_sum = a->inverse_mass + b->inverse_mass;

	if (inverse_mass_sum <= 0)
		return true;

	float sign = axis > 0 ? 1.0f : -1.0f;
	float penetration;
	float relative_normal_velocity;

	if (axis == 1 || axis == -1) {
		penetration = required_x - sign * dx;
		if (penetration > AABB_POSITION_SLOP) {
			float correction = penetration - AABB_POSITION_SLOP;

			a->x -= sign * correction
				* a->inverse_mass / inverse_mass_sum;
			b->x += sign * correction
				* b->inverse_mass / inverse_mass_sum;
		}

		relative_normal_velocity =
			sign * (b->velocity_x - a->velocity_x);
		if (relative_normal_velocity < 0) {
			float impulse = -relative_normal_velocity / inverse_mass_sum;

			a->velocity_x -= sign * impulse * a->inverse_mass;
			b->velocity_x += sign * impulse * b->inverse_mass;
		}
	}
	else {
		penetration = required_y - sign * dy;
		if (penetration > AABB_POSITION_SLOP) {
			float correction = penetration - AABB_POSITION_SLOP;

			a->y -= sign * correction
				* a->inverse_mass / inverse_mass_sum;
			b->y += sign * correction
				* b->inverse_mass / inverse_mass_sum;
		}

		relative_normal_velocity =
			sign * (b->velocity_y - a->velocity_y);
		if (relative_normal_velocity < 0) {
			float impulse = -relative_normal_velocity / inverse_mass_sum;

			a->velocity_y -= sign * impulse * a->inverse_mass;
			b->velocity_y += sign * impulse * b->inverse_mass;
		}
	}

	return true;
}

/*
 * Choose every new contact orientation from the same predicted state before
 * positional corrections begin.  Otherwise an early correction can move a
 * body through a third body and cause the later pair to record the reversed
 * relationship.
 */
static void
initialize_contact_axes(AabbWorld *world, float substep_dt)
{
	for (size_t a_id = 0; a_id < world->count; a_id++) {
		const AabbBody *a = &world->bodies[a_id];

		for (size_t b_id = a_id + 1; b_id < world->count; b_id++) {
			const AabbBody *b = &world->bodies[b_id];
			float required_x = half_width(world, a) + half_width(world, b);
			float required_y = half_height(world, a) + half_height(world, b);

			if (required_x - fabsf(b->x - a->x) <= 0
					|| required_y - fabsf(b->y - a->y) <= 0)
				continue;

			size_t index = pair_index(world, a_id, b_id);
			if (world->contact_axis[index] == 0)
				world->contact_axis[index] = choose_contact_axis(world,
						a_id, b_id, substep_dt);
		}
	}
}

static void
release_separated_contacts(AabbWorld *world)
{
	for (size_t a_id = 0; a_id < world->count; a_id++) {
		const AabbBody *a = &world->bodies[a_id];

		for (size_t b_id = a_id + 1; b_id < world->count; b_id++) {
			const AabbBody *b = &world->bodies[b_id];
			float required_x = half_width(world, a) + half_width(world, b);
			float required_y = half_height(world, a) + half_height(world, b);
			float separation_x = fabsf(b->x - a->x) - required_x;
			float separation_y = fabsf(b->y - a->y) - required_y;

			if (separation_x > AABB_CONTACT_RELEASE_SLOP
					|| separation_y > AABB_CONTACT_RELEASE_SLOP)
				world->contact_axis[pair_index(world, a_id, b_id)] = 0;
		}
	}
}

static unsigned int
count_contacts(const AabbWorld *world)
{
	unsigned int count = 0;

	for (size_t a_id = 0; a_id < world->count; a_id++) {
		const AabbBody *a = &world->bodies[a_id];

		for (size_t b_id = a_id + 1; b_id < world->count; b_id++) {
			const AabbBody *b = &world->bodies[b_id];
			float required_x = half_width(world, a) + half_width(world, b);
			float required_y = half_height(world, a) + half_height(world, b);
			float separation_x = fabsf(b->x - a->x) - required_x;
			float separation_y = fabsf(b->y - a->y) - required_y;

			if (separation_x <= AABB_POSITION_SLOP
					&& separation_y <= AABB_POSITION_SLOP)
				count++;
		}
	}

	return count;
}

AabbWorld *
aabb_world_create(size_t capacity, float padding,
		float coordinate_scale_x, float coordinate_scale_y)
{
	(void) coordinate_scale_x;
	(void) coordinate_scale_y;

	if (capacity == 0 || capacity > UINT_MAX
			|| capacity > SIZE_MAX / capacity)
		return NULL;

	AabbWorld *world = calloc(1, sizeof(*world));

	if (!world)
		return NULL;

	world->bodies = calloc(capacity, sizeof(*world->bodies));
	world->step_start_x = calloc(capacity, sizeof(*world->step_start_x));
	world->step_start_y = calloc(capacity, sizeof(*world->step_start_y));
	world->contact_axis = calloc(capacity * capacity,
			sizeof(*world->contact_axis));

	if (!world->bodies || !world->step_start_x || !world->step_start_y
			|| !world->contact_axis) {
		aabb_world_destroy(world);
		return NULL;
	}

	world->capacity = capacity;
	world->padding = padding > 0 ? padding : 0;
	return world;
}

void
aabb_world_destroy(AabbWorld *world)
{
	if (!world)
		return;

	free(world->contact_axis);
	free(world->step_start_y);
	free(world->step_start_x);
	free(world->bodies);
	free(world);
}

AabbBodyId
aabb_world_add_body(AabbWorld *world, const AabbBodyDef *definition)
{
	if (!world || !definition || world->count >= world->capacity
			|| definition->width <= 0 || definition->height <= 0)
		return AABB_BODY_INVALID;

	size_t id = world->count++;
	AabbBody *body = &world->bodies[id];

	body->x = definition->center_x;
	body->y = definition->center_y;
	body->width = definition->width;
	body->height = definition->height;
	body->inverse_mass = definition->inverse_mass > 0
		? definition->inverse_mass : 0;

	return (AabbBodyId) id;
}

size_t
aabb_world_body_count(const AabbWorld *world)
{
	return world ? world->count : 0;
}

bool
aabb_body_get_position(const AabbWorld *world, AabbBodyId body,
		float *center_x, float *center_y)
{
	if (!world || body >= world->count || !center_x || !center_y)
		return false;

	*center_x = world->bodies[body].x;
	*center_y = world->bodies[body].y;
	return true;
}

bool
aabb_body_set_position(AabbWorld *world, AabbBodyId body,
		float center_x, float center_y)
{
	if (!world || body >= world->count)
		return false;

	world->bodies[body].x = center_x;
	world->bodies[body].y = center_y;
	return true;
}

bool
aabb_body_set_drive(AabbWorld *world, AabbBodyId body,
		float velocity_x, float velocity_y)
{
	if (!world || body >= world->count)
		return false;

	world->bodies[body].drive_x = velocity_x;
	world->bodies[body].drive_y = velocity_y;
	return true;
}

void
aabb_world_clear_drives(AabbWorld *world)
{
	if (!world)
		return;

	for (size_t i = 0; i < world->count; i++) {
		world->bodies[i].drive_x = 0;
		world->bodies[i].drive_y = 0;
	}
}

float
aabb_world_max_penetration(const AabbWorld *world)
{
	float maximum = 0;

	if (!world)
		return maximum;

	for (size_t a_id = 0; a_id < world->count; a_id++) {
		const AabbBody *a = &world->bodies[a_id];

		for (size_t b_id = a_id + 1; b_id < world->count; b_id++) {
			const AabbBody *b = &world->bodies[b_id];
			float required_x = half_width(world, a) + half_width(world, b);
			float required_y = half_height(world, a) + half_height(world, b);
			float overlap_x = required_x - fabsf(b->x - a->x);
			float overlap_y = required_y - fabsf(b->y - a->y);

			if (overlap_x > 0 && overlap_y > 0) {
				float penetration = fminf(overlap_x, overlap_y);

				if (penetration > maximum)
					maximum = penetration;
			}
		}
	}

	return maximum;
}

AabbStepResult
aabb_world_step(AabbWorld *world, float dt)
{
	AabbStepResult result = {0};

	if (!world || world->count == 0 || dt <= 0)
		return result;

	/* Rebuild contact relationships from each new Cosmos drive field. */
	memset(world->contact_axis, 0,
			world->capacity * world->capacity
			* sizeof(*world->contact_axis));

	float max_speed = 0;
	float minimum_extent = FLT_MAX;

	for (size_t i = 0; i < world->count; i++) {
		AabbBody *body = &world->bodies[i];
		world->step_start_x[i] = body->x;
		world->step_start_y[i] = body->y;
		body->velocity_x = body->drive_x;
		body->velocity_y = body->drive_y;

		float speed = hypotf(body->velocity_x, body->velocity_y);
		float extent = fminf(body->width + world->padding,
				body->height + world->padding);

		if (speed > max_speed)
			max_speed = speed;
		if (extent < minimum_extent)
			minimum_extent = extent;
	}

	/* Keep discrete motion small enough that bodies cannot casually tunnel. */
	float max_translation = fmaxf(1.0f, minimum_extent * 0.2f);
	unsigned int substeps = (unsigned int)
		ceilf(max_speed * dt / max_translation);

	if (substeps < 1)
		substeps = 1;
	if (substeps > AABB_MAX_SUBSTEPS)
		substeps = AABB_MAX_SUBSTEPS;

	result.substeps = substeps;
	float substep_dt = dt / (float) substeps;

	for (unsigned int step = 0; step < substeps; step++) {
		for (size_t i = 0; i < world->count; i++) {
			world->bodies[i].x += world->bodies[i].velocity_x * substep_dt;
			world->bodies[i].y += world->bodies[i].velocity_y * substep_dt;
		}

		initialize_contact_axes(world, substep_dt);

		for (unsigned int iteration = 0;
				iteration < AABB_SOLVER_ITERATIONS;
				iteration++) {
			bool corrected = false;

			for (size_t a_id = 0; a_id < world->count; a_id++) {
				for (size_t b_id = a_id + 1;
						b_id < world->count; b_id++) {
					if (solve_contact(world, a_id, b_id, substep_dt))
						corrected = true;
				}
			}

			if (!corrected)
				break;
		}

		release_separated_contacts(world);
	}

	for (size_t i = 0; i < world->count; i++) {
		float movement = hypotf(world->bodies[i].x - world->step_start_x[i],
				world->bodies[i].y - world->step_start_y[i]);

		if (movement > result.max_movement)
			result.max_movement = movement;
	}

	result.max_penetration = aabb_world_max_penetration(world);
	result.contacts = count_contacts(world);

	return result;
}

#endif

bool
aabb_world_center_of_mass(const AabbWorld *world,
		float *center_x, float *center_y)
{
	if (!world || world->count == 0 || !center_x || !center_y)
		return false;

	double total_mass = 0;
	double weighted_x = 0;
	double weighted_y = 0;

	for (size_t i = 0; i < world->count; i++) {
		const AabbBody *body = &world->bodies[i];
		float x, y;

		if (body->inverse_mass <= 0
				|| !aabb_body_get_position(world, (AabbBodyId) i, &x, &y))
			continue;

		double mass = 1.0 / body->inverse_mass;
		total_mass += mass;
		weighted_x += mass * x;
		weighted_y += mass * y;
	}

	if (total_mass <= 0)
		return false;

	*center_x = (float) (weighted_x / total_mass);
	*center_y = (float) (weighted_y / total_mass);
	return true;
}

float
aabb_world_required_dilation(const AabbWorld *world, float clearance)
{
	float dilation = 1.0f;

	if (!world)
		return dilation;
	if (clearance < 0)
		clearance = 0;

	for (size_t a_id = 0; a_id < world->count; a_id++) {
		const AabbBody *a = &world->bodies[a_id];
		float ax, ay;
		if (!aabb_body_get_position(world, (AabbBodyId) a_id, &ax, &ay))
			return 1.0f;

		for (size_t b_id = a_id + 1; b_id < world->count; b_id++) {
			const AabbBody *b = &world->bodies[b_id];
			float bx, by;
			if (!aabb_body_get_position(world, (AabbBodyId) b_id, &bx, &by))
				return 1.0f;

			float distance_x = fabsf(bx - ax);
			float distance_y = fabsf(by - ay);
			float required_x = half_width(world, a)
				+ half_width(world, b) + clearance;
			float required_y = half_height(world, a)
				+ half_height(world, b) + clearance;
			float dilation_x = distance_x > 0
				? required_x / distance_x : INFINITY;
			float dilation_y = distance_y > 0
				? required_y / distance_y : INFINITY;
			float pair_dilation = fminf(dilation_x, dilation_y);

			if (pair_dilation > dilation)
				dilation = pair_dilation;
		}
	}

	return dilation;
}

bool
aabb_world_dilate(AabbWorld *world,
		float center_x, float center_y, float scale)
{
	if (!world || !isfinite(center_x) || !isfinite(center_y)
			|| !isfinite(scale) || scale <= 0)
		return false;

	for (size_t i = 0; i < world->count; i++) {
		float x, y;
		if (!aabb_body_get_position(world, (AabbBodyId) i, &x, &y)
				|| !aabb_body_set_position(world, (AabbBodyId) i,
				center_x + scale * (x - center_x),
				center_y + scale * (y - center_y)))
			return false;
	}

	aabb_world_clear_drives(world);
	return true;
}
