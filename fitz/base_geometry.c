#include "fitz-internal.h"

#define MAX4(a,b,c,d) fz_max(fz_max(a,b), fz_max(c,d))
#define MIN4(a,b,c,d) fz_min(fz_min(a,b), fz_min(c,d))

/*	A useful macro to add with overflow detection and clamping.

	We want to do "b = a + x", but to allow for overflow. Consider the
	top bits, and the cases in which overflow occurs:

	overflow    a   x   b ~a^x  a^b   (~a^x)&(a^b)
	   no       0   0   0   1    0          0
	   yes      0   0   1   1    1          1
	   no       0   1   0   0    0          0
	   no       0   1   1   0    1          0
	   no       1   0   0   0    1          0
	   no       1   0   1   0    0          0
	   yes      1   1   0   1    1          1
	   no       1   1   1   1    0          0
*/
#define ADD_WITH_SAT(b,a,x) \
	((b) = (a) + (x), (b) = (((~(a)^(x))&((a)^(b))) < 0 ? ((x) < 0 ? INT_MIN : INT_MAX) : (b)))

/* Matrices, points and affine transformations */

const fz_matrix fz_identity = { 1, 0, 0, 1, 0, 0 };

fz_matrix
fz_concat(fz_matrix one, fz_matrix two)
{
	fz_matrix dst;
	dst.a = one.a * two.a + one.b * two.c;
	dst.b = one.a * two.b + one.b * two.d;
	dst.c = one.c * two.a + one.d * two.c;
	dst.d = one.c * two.b + one.d * two.d;
	dst.e = one.e * two.a + one.f * two.c + two.e;
	dst.f = one.e * two.b + one.f * two.d + two.f;
	return dst;
}

fz_matrix
fz_scale(float sx, float sy)
{
	fz_matrix m;
	m.a = sx; m.b = 0;
	m.c = 0; m.d = sy;
	m.e = 0; m.f = 0;
	return m;
}

fz_matrix
fz_shear(float h, float v)
{
	fz_matrix m;
	m.a = 1; m.b = v;
	m.c = h; m.d = 1;
	m.e = 0; m.f = 0;
	return m;
}

fz_matrix
fz_rotate(float theta)
{
	fz_matrix m;
	float s;
	float c;

	while (theta < 0)
		theta += 360;
	while (theta >= 360)
		theta -= 360;

	if (fabsf(0 - theta) < FLT_EPSILON)
	{
		s = 0;
		c = 1;
	}
	else if (fabsf(90.0f - theta) < FLT_EPSILON)
	{
		s = 1;
		c = 0;
	}
	else if (fabsf(180.0f - theta) < FLT_EPSILON)
	{
		s = 0;
		c = -1;
	}
	else if (fabsf(270.0f - theta) < FLT_EPSILON)
	{
		s = -1;
		c = 0;
	}
	else
	{
		s = sinf(theta * (float)M_PI / 180);
		c = cosf(theta * (float)M_PI / 180);
	}

	m.a = c; m.b = s;
	m.c = -s; m.d = c;
	m.e = 0; m.f = 0;
	return m;
}

fz_matrix
fz_translate(float tx, float ty)
{
	fz_matrix m;
	m.a = 1; m.b = 0;
	m.c = 0; m.d = 1;
	m.e = tx; m.f = ty;
	return m;
}

fz_matrix
fz_invert_matrix(fz_matrix src)
{
	float det = src.a * src.d - src.b * src.c;
	if (det < -FLT_EPSILON || det > FLT_EPSILON)
	{
		fz_matrix dst;
		float rdet = 1 / det;
		dst.a = src.d * rdet;
		dst.b = -src.b * rdet;
		dst.c = -src.c * rdet;
		dst.d = src.a * rdet;
		dst.e = -src.e * dst.a - src.f * dst.c;
		dst.f = -src.e * dst.b - src.f * dst.d;
		return dst;
	}
	return src;
}

int
fz_is_rectilinear(fz_matrix m)
{
	return (fabsf(m.b) < FLT_EPSILON && fabsf(m.c) < FLT_EPSILON) ||
		(fabsf(m.a) < FLT_EPSILON && fabsf(m.d) < FLT_EPSILON);
}

float
fz_matrix_expansion(fz_matrix m)
{
	return sqrtf(fabsf(m.a * m.d - m.b * m.c));
}

float
fz_matrix_max_expansion(fz_matrix m)
{
	float max = fabsf(m.a);
	float x = fabsf(m.b);
	if (max < x)
		max = x;
	x = fabsf(m.c);
	if (max < x)
		max = x;
	x = fabsf(m.d);
	if (max < x)
		max = x;
	return max;
}

fz_point
fz_transform_point(fz_matrix m, fz_point p)
{
	fz_point t;
	t.x = p.x * m.a + p.y * m.c + m.e;
	t.y = p.x * m.b + p.y * m.d + m.f;
	return t;
}

fz_point
fz_transform_vector(fz_matrix m, fz_point p)
{
	fz_point t;
	t.x = p.x * m.a + p.y * m.c;
	t.y = p.x * m.b + p.y * m.d;
	return t;
}

/* Rectangles and bounding boxes */

/* biggest and smallest integers that a float can represent perfectly (i.e. 24 bits) */
#define MAX_SAFE_INT 16777216
#define MIN_SAFE_INT -16777216

const fz_rect fz_infinite_rect = { 1, 1, -1, -1 };
const fz_rect fz_empty_rect = { 0, 0, 0, 0 };
const fz_rect fz_unit_rect = { 0, 0, 1, 1 };

const fz_irect fz_infinite_irect = { 1, 1, -1, -1 };
const fz_irect fz_empty_irect = { 0, 0, 0, 0 };
const fz_irect fz_unit_irect = { 0, 0, 1, 1 };

fz_irect
fz_rect_covering_rect(fz_rect a)
{
	fz_irect b;

	a.x0 = floorf(a.x0);
	a.y0 = floorf(a.y0);
	a.x1 = ceilf(a.x1);
	a.y1 = ceilf(a.y1);

	/* check for integer overflow */
	b.x0 = fz_clamp(a.x0, MIN_SAFE_INT, MAX_SAFE_INT);
	b.y0 = fz_clamp(a.y0, MIN_SAFE_INT, MAX_SAFE_INT);
	b.x1 = fz_clamp(a.x1, MIN_SAFE_INT, MAX_SAFE_INT);
	b.y1 = fz_clamp(a.y1, MIN_SAFE_INT, MAX_SAFE_INT);

	return b;
}

fz_rect
fz_rect_from_irect(fz_irect a)
{
	fz_rect b;
	b.x0 = a.x0;
	b.y0 = a.y0;
	b.x1 = a.x1;
	b.y1 = a.y1;
	return b;
}

fz_irect
fz_round_rect(fz_rect a)
{
	fz_irect b;

	a.x0 = floorf(a.x0 + 0.001);
	a.y0 = floorf(a.y0 + 0.001);
	a.x1 = ceilf(a.x1 - 0.001);
	a.y1 = ceilf(a.y1 - 0.001);

	/* check for integer overflow */
	b.x0 = fz_clamp(a.x0, MIN_SAFE_INT, MAX_SAFE_INT);
	b.y0 = fz_clamp(a.y0, MIN_SAFE_INT, MAX_SAFE_INT);
	b.x1 = fz_clamp(a.x1, MIN_SAFE_INT, MAX_SAFE_INT);
	b.y1 = fz_clamp(a.y1, MIN_SAFE_INT, MAX_SAFE_INT);

	return b;
}

fz_rect
fz_intersect_rect(fz_rect a, fz_rect b)
{
	fz_rect r;
	/* Check for empty box before infinite box */
	if (fz_is_empty_rect(a)) return fz_empty_rect;
	if (fz_is_empty_rect(b)) return fz_empty_rect;
	if (fz_is_infinite_rect(a)) return b;
	if (fz_is_infinite_rect(b)) return a;
	r.x0 = fz_max(a.x0, b.x0);
	r.y0 = fz_max(a.y0, b.y0);
	r.x1 = fz_min(a.x1, b.x1);
	r.y1 = fz_min(a.y1, b.y1);
	return (r.x1 < r.x0 || r.y1 < r.y0) ? fz_empty_rect : r;
}

fz_irect
fz_intersect_irect(fz_irect a, fz_irect b)
{
	fz_irect r;
	/* Check for empty box before infinite box */
	if (fz_is_empty_rect(a)) return fz_empty_irect;
	if (fz_is_empty_rect(b)) return fz_empty_irect;
	if (fz_is_infinite_rect(a)) return b;
	if (fz_is_infinite_rect(b)) return a;
	r.x0 = fz_maxi(a.x0, b.x0);
	r.y0 = fz_maxi(a.y0, b.y0);
	r.x1 = fz_mini(a.x1, b.x1);
	r.y1 = fz_mini(a.y1, b.y1);
	return (r.x1 < r.x0 || r.y1 < r.y0) ? fz_empty_irect : r;
}

fz_rect
fz_union_rect(fz_rect a, fz_rect b)
{
	fz_rect r;
	/* Check for empty box before infinite box */
	if (fz_is_empty_rect(a)) return b;
	if (fz_is_empty_rect(b)) return a;
	if (fz_is_infinite_rect(a)) return a;
	if (fz_is_infinite_rect(b)) return b;
	r.x0 = fz_min(a.x0, b.x0);
	r.y0 = fz_min(a.y0, b.y0);
	r.x1 = fz_max(a.x1, b.x1);
	r.y1 = fz_max(a.y1, b.y1);
	return r;
}

fz_rect
fz_transform_rect(fz_matrix m, fz_rect r)
{
	fz_point s, t, u, v;

	if (fz_is_infinite_rect(r))
		return r;

	s.x = r.x0; s.y = r.y0;
	t.x = r.x0; t.y = r.y1;
	u.x = r.x1; u.y = r.y1;
	v.x = r.x1; v.y = r.y0;
	s = fz_transform_point(m, s);
	t = fz_transform_point(m, t);
	u = fz_transform_point(m, u);
	v = fz_transform_point(m, v);
	r.x0 = MIN4(s.x, t.x, u.x, v.x);
	r.y0 = MIN4(s.y, t.y, u.y, v.y);
	r.x1 = MAX4(s.x, t.x, u.x, v.x);
	r.y1 = MAX4(s.y, t.y, u.y, v.y);
	return r;
}

fz_rect
fz_translate_rect(fz_rect a, float xoff, float yoff)
{
	fz_rect b;
	if (fz_is_empty_rect(a)) return a;
	if (fz_is_infinite_rect(a)) return a;
	b.x0 = a.x0 + xoff;
	b.y0 = a.y0 + yoff;
	b.x1 = a.x1 + xoff;
	b.y1 = a.y1 + yoff;
	return b;
}

fz_irect
fz_translate_irect(fz_irect a, int xoff, int yoff)
{
	fz_irect b;
	if (fz_is_empty_rect(a)) return a;
	if (fz_is_infinite_rect(a)) return a;
	ADD_WITH_SAT(b.x0, a.x0, xoff);
	ADD_WITH_SAT(b.y0, a.y0, yoff);
	ADD_WITH_SAT(b.x1, a.x1, xoff);
	ADD_WITH_SAT(b.y1, a.y1, yoff);
	return b;
}

fz_rect
fz_expand_rect(fz_rect a, float expand)
{
	fz_rect b;
	if (fz_is_empty_rect(a)) return a;
	if (fz_is_infinite_rect(a)) return a;
	b.x0 = a.x0 - expand;
	b.y0 = a.y0 - expand;
	b.x1 = a.x1 + expand;
	b.y1 = a.y1 + expand;
	return b;
}
