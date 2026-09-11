#include <3ds.h>
#include <citro3d.h>
#include <citro2d.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cube_shbin.h"

#define TOP_WIDTH 400.0f
#define TOP_HEIGHT 240.0f
#define BOTTOM_WIDTH 320.0f
#define BOTTOM_HEIGHT 240.0f

#define DISPLAY_TRANSFER_FLAGS \
	(GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) | \
	GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8) | \
	GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO))

static DVLB_s* cube_dvlb = NULL;
static shaderProgram_s cube_program;
static C3D_AttrInfo cube_attr_info;
static int uLoc_projection;
static int uLoc_modelView;
static bool shader_loaded = false;

static const char* vertex_names[8] = {
	"V0", "V1", "V2", "V3",
	"V4", "V5", "V6", "V7"
};

typedef struct {
	float x, y, z;
	float r, g, b;
} Vertex;

typedef struct {
	int v0, v1;
} Edge;

typedef struct {
	float x, y, z;
} Vec3;

static Vertex cube_vertices[8] = {
	{-1.0f, -1.0f, -1.0f, 1.0f, 0.0f, 0.0f},
	{ 1.0f, -1.0f, -1.0f, 0.0f, 1.0f, 0.0f},
	{ 1.0f,  1.0f, -1.0f, 0.0f, 0.0f, 1.0f},
	{-1.0f,  1.0f, -1.0f, 1.0f, 1.0f, 0.0f},
	{-1.0f, -1.0f,  1.0f, 1.0f, 0.0f, 1.0f},
	{ 1.0f, -1.0f,  1.0f, 0.0f, 1.0f, 1.0f},
	{ 1.0f,  1.0f,  1.0f, 0.5f, 0.5f, 0.5f},
	{-1.0f,  1.0f,  1.0f, 1.0f, 0.5f, 0.0f}
};

static Edge cube_edges[12] = {
	{0, 1}, {1, 2}, {2, 3}, {3, 0},
	{4, 5}, {5, 6}, {6, 7}, {7, 4},
	{0, 4}, {1, 5}, {2, 6}, {3, 7}
};

static const u8 cube_faces[36] = {
	0, 2, 1, 0, 3, 2,
	4, 5, 6, 4, 6, 7,
	0, 1, 5, 0, 5, 4,
	3, 2, 6, 3, 6, 7,
	0, 4, 7, 0, 7, 3,
	1, 2, 6, 1, 6, 5
};

static Vec3 cube_pos = {0.0f, 0.0f, -8.0f};
static float cube_rot_x = 0.3f;
static float cube_rot_y = 0.0f;
static float cube_rot_z = 0.0f;

static int selected_vertex = -1;
static float prev_touch_x = 0.0f;
static float prev_touch_y = 0.0f;
static bool touch_active = false;

static C3D_RenderTarget* top_target = NULL;
static C3D_RenderTarget* bot_target = NULL;
static C3D_Mtx proj_top;
static C2D_TextBuf text_buf = NULL;

static Vec3 vec3_cross(Vec3 a, Vec3 b)
{
	Vec3 result = {
		a.y * b.z - a.z * b.y,
		a.z * b.x - a.x * b.z,
		a.x * b.y - a.y * b.x
	};
	return result;
}

static float vec3_length(Vec3 v)
{
	return sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
}

static Vec3 vec3_normalized(Vec3 v, Vec3 fallback)
{
	float length = vec3_length(v);
	if (length < 0.0001f) return fallback;
	v.x /= length;
	v.y /= length;
	v.z /= length;
	return v;
}

static void build_model(C3D_Mtx* model)
{
	Mtx_Identity(model);
	Mtx_Translate(model, cube_pos.x, cube_pos.y, cube_pos.z, true);
	Mtx_RotateX(model, cube_rot_x, true);
	Mtx_RotateY(model, cube_rot_y, true);
	Mtx_RotateZ(model, cube_rot_z, true);
}

static void send_vertex(float x, float y, float z, float r, float g, float b)
{
	C3D_ImmSendAttrib(x, y, z, 1.0f);
	C3D_ImmSendAttrib(r, g, b, 1.0f);
}

static void send_vertex_data(const Vertex* vertex)
{
	send_vertex(vertex->x, vertex->y, vertex->z, vertex->r, vertex->g, vertex->b);
}

static void configure_cube_state(void)
{
	C3D_BindProgram(&cube_program);
	C3D_SetAttrInfo(&cube_attr_info);
	C3D_DepthMap(true, -1.0f, 0.0f);
	C3D_DepthTest(true, GPU_GEQUAL, GPU_WRITE_ALL);
	C3D_CullFace(GPU_CULL_NONE);

	C3D_TexEnv* env = C3D_GetTexEnv(0);
	C3D_TexEnvInit(env);
	C3D_TexEnvSrc(env, C3D_Both, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
	C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);
}

static void render_cube_faces(void)
{
	C3D_Mtx model;
	build_model(&model);

	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, uLoc_modelView, &model);
	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, uLoc_projection, &proj_top);

	C3D_ImmDrawBegin(GPU_TRIANGLES);
	for (int i = 0; i < 36; i++)
		send_vertex_data(&cube_vertices[cube_faces[i]]);
	C3D_ImmDrawEnd();
}

static void render_cube_wireframe(void)
{
	C3D_Mtx model;
	C3D_Mtx inverse_model;
	build_model(&model);
	Mtx_Copy(&inverse_model, &model);
	if (fabsf(Mtx_Inverse(&inverse_model)) < 0.00001f) return;

	Vec3 view_direction = {-cube_pos.x, -cube_pos.y, -cube_pos.z};
	view_direction = vec3_normalized(view_direction, Vec3{0.0f, 0.0f, -1.0f});

	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, uLoc_modelView, &model);
	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, uLoc_projection, &proj_top);

	C3D_ImmDrawBegin(GPU_TRIANGLES);
	for (int i = 0; i < 12; i++)
	{
		Vertex* a = &cube_vertices[cube_edges[i].v0];
		Vertex* b = &cube_vertices[cube_edges[i].v1];
		Vec3 edge = {b->x - a->x, b->y - a->y, b->z - a->z};
		C3D_FVec world_edge = Mtx_MultiplyFVec4(&model, FVec4_New(edge.x, edge.y, edge.z, 0.0f));
		Vec3 world_edge_vec = {world_edge.x, world_edge.y, world_edge.z};
		float edge_len = vec3_length(world_edge_vec);
		if (edge_len < 0.00001f) continue;
		Vec3 e = {world_edge_vec.x / edge_len, world_edge_vec.y / edge_len, world_edge_vec.z / edge_len};
		Vec3 normal = vec3_cross(e, view_direction);
		if (vec3_length(normal) < 0.00001f)
		{
			Vec3 reference;
			if (fabsf(e.x) < 0.9f) reference = Vec3{1.0f, 0.0f, 0.0f};
			else reference = Vec3{0.0f, 1.0f, 0.0f};
			normal = vec3_cross(e, reference);
		}
		normal = vec3_normalized(normal, Vec3{0.0f, 1.0f, 0.0f});
		C3D_FVec local_normal = Mtx_MultiplyFVec4(&inverse_model, FVec4_New(normal.x, normal.y, normal.z, 0.0f));
		Vec3 local_normal_vec = {local_normal.x, local_normal.y, local_normal.z};
		local_normal_vec = vec3_normalized(local_normal_vec, Vec3{0.0f, 1.0f, 0.0f});

		const float thickness = 0.025f;
		send_vertex(a->x + local_normal_vec.x * thickness, a->y + local_normal_vec.y * thickness, a->z + local_normal_vec.z * thickness, a->r, a->g, a->b);
		send_vertex(b->x + local_normal_vec.x * thickness, b->y + local_normal_vec.y * thickness, b->z + local_normal_vec.z * thickness, b->r, b->g, b->b);
		send_vertex(a->x - local_normal_vec.x * thickness, a->y - local_normal_vec.y * thickness, a->z - local_normal_vec.z * thickness, a->r, a->g, a->b);
		send_vertex(b->x + local_normal_vec.x * thickness, b->y + local_normal_vec.y * thickness, b->z + local_normal_vec.z * thickness, b->r, b->g, b->b);
		send_vertex(b->x - local_normal_vec.x * thickness, b->y - local_normal_vec.y * thickness, b->z - local_normal_vec.z * thickness, b->r, b->g, b->b);
		send_vertex(a->x - local_normal_vec.x * thickness, a->y - local_normal_vec.y * thickness, a->z - local_normal_vec.z * thickness, a->r, a->g, a->b);
	}
	C3D_ImmDrawEnd();
}

static void render_cube_points(void)
{
	C3D_Mtx model;
	C3D_Mtx inverse_model;
	build_model(&model);
	Mtx_Copy(&inverse_model, &model);
	if (fabsf(Mtx_Inverse(&inverse_model)) < 0.00001f) return;

	Vec3 view_direction = {-cube_pos.x, -cube_pos.y, -cube_pos.z};
	view_direction = vec3_normalized(view_direction, Vec3{0.0f, 0.0f, -1.0f});
	C3D_FVec local_view = Mtx_MultiplyFVec4(&inverse_model, FVec4_New(view_direction.x, view_direction.y, view_direction.z, 0.0f));
	Vec3 local_view_vec = {local_view.x, local_view.y, local_view.z};

	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, uLoc_modelView, &model);
	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, uLoc_projection, &proj_top);

	C3D_ImmDrawBegin(GPU_TRIANGLES);
	for (int i = 0; i < 8; i++)
	{
		Vertex* vertex = &cube_vertices[i];
		Vec3 normal = vec3_normalized(local_view_vec, Vec3{0.0f, 0.0f, 1.0f});
		Vec3 reference = {0.0f, 0.0f, 1.0f};
		if (fabsf(normal.x * reference.x + normal.y * reference.y + normal.z * reference.z) > 0.9f)
			reference = Vec3{0.0f, 1.0f, 0.0f};
		Vec3 right = vec3_cross(normal, reference);
		right = vec3_normalized(right, Vec3{1.0f, 0.0f, 0.0f});
		Vec3 up = vec3_cross(normal, right);
		up = vec3_normalized(up, Vec3{0.0f, 1.0f, 0.0f});

		const float size = 0.055f;
		float r = vertex->r;
		float g = vertex->g;
		float b = vertex->b;
		if (i == selected_vertex)
		{
			r = 1.0f;
			g = 1.0f;
			b = 0.0f;
		}

		send_vertex(vertex->x - right.x * size - up.x * size, vertex->y - right.y * size - up.y * size, vertex->z - right.z * size - up.z * size, r, g, b);
		send_vertex(vertex->x + right.x * size - up.x * size, vertex->y + right.y * size - up.y * size, vertex->z + right.z * size - up.z * size, r, g, b);
		send_vertex(vertex->x - right.x * size + up.x * size, vertex->y - right.y * size + up.y * size, vertex->z - right.z * size + up.z * size, r, g, b);
		send_vertex(vertex->x + right.x * size - up.x * size, vertex->y + right.y * size - up.y * size, vertex->z + right.z * size - up.z * size, r, g, b);
		send_vertex(vertex->x + right.x * size + up.x * size, vertex->y + right.y * size + up.y * size, vertex->z + right.z * size + up.z * size, r, g, b);
		send_vertex(vertex->x - right.x * size + up.x * size, vertex->y - right.y * size + up.y * size, vertex->z - right.z * size + up.z * size, r, g, b);
	}
	C3D_ImmDrawEnd();
}

static bool project_vertex_clip(const Vertex* vertex, C3D_FVec* ndc, float* clip_w)
{
	C3D_Mtx model;
	build_model(&model);

	C3D_FVec world = Mtx_MultiplyFVec4(&model, FVec4_New(vertex->x, vertex->y, vertex->z, 1.0f));
	C3D_FVec clip = Mtx_MultiplyFVec4(&proj_top, world);
	if (clip.w <= 0.0001f) return false;
	if (clip_w) *clip_w = clip.w;
	*ndc = FVec4_PerspDivide(clip);
	return true;
}

static bool __attribute__((unused)) project_vertex(const Vertex* vertex, float* sx, float* sy)
{
	C3D_FVec ndc;
	if (!project_vertex_clip(vertex, &ndc, NULL)) return false;

	*sx = (ndc.x * 0.5f + 0.5f) * TOP_WIDTH;
	*sy = (0.5f - ndc.y * 0.5f) * TOP_HEIGHT;
	return true;
}

static bool __attribute__((unused)) screen_to_local(float sx, float sy, float reference_z, const C3D_Mtx* inverse_matrix, Vec3* local)
{
	float ndc_x = sx / TOP_WIDTH * 2.0f - 1.0f;
	float ndc_y = 1.0f - sy / TOP_HEIGHT * 2.0f;
	C3D_FVec clip = Mtx_MultiplyFVec4(inverse_matrix, FVec4_New(ndc_x, ndc_y, reference_z, 1.0f));
	if (fabsf(clip.w) < 0.0001f) return false;

	clip = FVec4_PerspDivide(clip);
	local->x = clip.x;
	local->y = clip.y;
	local->z = clip.z;
	return true;
}

static int find_closest_vertex(float tx, float ty)
{
	int closest = -1;
	float closest_dist_sq = 24.0f * 24.0f;
	float best_z = -2.0f;

	for (int i = 0; i < 8; i++)
	{
		float sx, sy;
		C3D_FVec ndc;
		float clip_w;
		if (!project_vertex_clip(&cube_vertices[i], &ndc, &clip_w)) continue;
		sx = (ndc.x * 0.5f + 0.5f) * TOP_WIDTH;
		sy = (0.5f - ndc.y * 0.5f) * TOP_HEIGHT;
		float dx = sx - tx;
		float dy = sy - ty;
		float dist_sq = dx * dx + dy * dy;
		if (dist_sq < closest_dist_sq)
		{
			closest_dist_sq = dist_sq;
			best_z = ndc.z;
			closest = i;
		}
		else if (dist_sq < 24.0f * 24.0f && ndc.z > best_z)
		{
			// Prefer nearer depth among ties within radius
			best_z = ndc.z;
			closest = i;
		}
	}

	return closest;
}

static void move_selected_vertex(float dx, float dy)
{
	if (selected_vertex < 0) return;

	C3D_FVec ndc;
	float clip_w;
	if (!project_vertex_clip(&cube_vertices[selected_vertex], &ndc, &clip_w)) return;

	float current_sx = (ndc.x * 0.5f + 0.5f) * TOP_WIDTH;
	float current_sy = (0.5f - ndc.y * 0.5f) * TOP_HEIGHT;
	float old_clip_z = ndc.z * clip_w;

	C3D_Mtx model;
	C3D_Mtx combined;
	C3D_Mtx inverse_combined;
	build_model(&model);
	Mtx_Multiply(&combined, &proj_top, &model);
	Mtx_Copy(&inverse_combined, &combined);
	if (fabsf(Mtx_Inverse(&inverse_combined)) < 0.0001f) return;

	float ndc_x = (current_sx + dx) / TOP_WIDTH * 2.0f - 1.0f;
	float ndc_y = 1.0f - (current_sy + dy) / TOP_HEIGHT * 2.0f;
	// Clamp to avoid extreme unprojection outside frustum
	if (ndc_x < -1.0f) ndc_x = -1.0f;
	if (ndc_x >  1.0f) ndc_x =  1.0f;
	if (ndc_y < -1.0f) ndc_y = -1.0f;
	if (ndc_y >  1.0f) ndc_y =  1.0f;

	C3D_FVec clip = FVec4_New(ndc_x * clip_w, ndc_y * clip_w, old_clip_z, clip_w);
	C3D_FVec local = Mtx_MultiplyFVec4(&inverse_combined, clip);
	if (fabsf(local.w) < 0.0001f) return;
	local = FVec4_PerspDivide(local);
	if (!isfinite(local.x) || !isfinite(local.y) || !isfinite(local.z)) return;
	// Prevent dragging behind near plane
	C3D_FVec world = Mtx_MultiplyFVec4(&model, FVec4_New(local.x, local.y, local.z, 1.0f));
	if (world.z > -0.1f) return;

	cube_vertices[selected_vertex].x = local.x;
	cube_vertices[selected_vertex].y = local.y;
	cube_vertices[selected_vertex].z = local.z;
}

static float touch_to_top_x(float x)
{
	return (x / BOTTOM_WIDTH) * TOP_WIDTH;
}

static void handle_input(u32 kDown, u32 kHeld)
{
	circlePosition circle;
	hidCircleRead(&circle);

	float move_speed = 0.1f;
	float rot_speed = 0.01f;

	if (kHeld & KEY_DLEFT)  cube_pos.x -= move_speed;
	if (kHeld & KEY_DRIGHT) cube_pos.x += move_speed;
	if (kHeld & KEY_DUP)    cube_pos.y += move_speed;
	if (kHeld & KEY_DDOWN)  cube_pos.y -= move_speed;
	if (kHeld & KEY_L)      cube_pos.z += move_speed;
	if (kHeld & KEY_R)      cube_pos.z -= move_speed;

	cube_rot_y += circle.dx * rot_speed;
	cube_rot_x += circle.dy * rot_speed;

	if (kDown & KEY_X)
	{
		cube_rot_x = 0.3f;
		cube_rot_y = 0.0f;
		cube_rot_z = 0.0f;
		cube_pos.x = 0.0f;
		cube_pos.y = 0.0f;
		cube_pos.z = -8.0f;
		selected_vertex = -1;
		touch_active = false;
	}

	if (kDown & KEY_Y)
	{
		for (int i = 0; i < 8; i++)
		{
			cube_vertices[i].r = (float)(rand() % 100) / 100.0f;
			cube_vertices[i].g = (float)(rand() % 100) / 100.0f;
			cube_vertices[i].b = (float)(rand() % 100) / 100.0f;
		}
	}

	if (kDown & KEY_TOUCH)
	{
		touchPosition touch;
		hidTouchRead(&touch);
		float tx = touch_to_top_x((float)touch.px);
		float ty = (float)touch.py;
		selected_vertex = find_closest_vertex(tx, ty);
		prev_touch_x = tx;
		prev_touch_y = ty;
		touch_active = true;
	}
	else if (touch_active && (kHeld & KEY_TOUCH))
	{
		touchPosition touch;
		hidTouchRead(&touch);
		float tx = touch_to_top_x((float)touch.px);
		float ty = (float)touch.py;
		if (selected_vertex >= 0)
		{
			float dx = tx - prev_touch_x;
			float dy = ty - prev_touch_y;
			move_selected_vertex(dx, dy);
		}
		prev_touch_x = tx;
		prev_touch_y = ty;
	}
	else if (!(kHeld & KEY_TOUCH))
	{
		touch_active = false;
	}
}

static void draw_ui_text(C2D_TextBuf buf, const char* text, float y, float scale, u32 color)
{
	C2D_Text ui_text;
	C2D_TextParse(&ui_text, buf, text);
	C2D_TextOptimize(&ui_text);
	C2D_DrawText(&ui_text, C2D_WithColor, 8.0f, y, 0.5f, scale, scale, color);
}

static void render_ui(void)
{
	C2D_TextBufClear(text_buf);

	char buf[256];

	snprintf(buf, sizeof(buf), "Cube Manipulator");
	draw_ui_text(text_buf, buf, 8.0f, 0.42f, 0xFFFFFFFF);

	snprintf(buf, sizeof(buf), "Pos: %.1f %.1f %.1f", cube_pos.x, cube_pos.y, cube_pos.z);
	draw_ui_text(text_buf, buf, 30.0f, 0.34f, 0xFFFFFFFF);

	snprintf(buf, sizeof(buf), "Rot: %.1f %.1f %.1f", cube_rot_x, cube_rot_y, cube_rot_z);
	draw_ui_text(text_buf, buf, 48.0f, 0.34f, 0xFFFFFFFF);

	snprintf(buf, sizeof(buf), "Selected: %s", selected_vertex >= 0 ? vertex_names[selected_vertex] : "None");
	draw_ui_text(text_buf, buf, 66.0f, 0.34f, 0xFFFFFFFF);

	snprintf(buf, sizeof(buf), "D-pad: Move  Circle: Rotate");
	draw_ui_text(text_buf, buf, 90.0f, 0.30f, 0xFF808080);
	snprintf(buf, sizeof(buf), "Touch: Drag Vertex  X: Reset");
	draw_ui_text(text_buf, buf, 108.0f, 0.30f, 0xFF808080);
	snprintf(buf, sizeof(buf), "Y: Random Colors");
	draw_ui_text(text_buf, buf, 126.0f, 0.30f, 0xFF808080);

	draw_ui_text(text_buf, "Vertices:", 150.0f, 0.34f, 0xFFFFFFFF);
	for (int i = 0; i < 8; i++)
	{
		float x = 8.0f + (i % 2) * 160.0f;
		float y = 168.0f + (i / 2) * 18.0f;
		u32 color = (i == selected_vertex) ? 0xFF00FFFF : 0xFF808080;
		C2D_DrawRectSolid(x, y + 2.0f, 0.5f, 7.0f, 7.0f, color);
		snprintf(buf, sizeof(buf), "%s %.1f %.1f %.1f", vertex_names[i], cube_vertices[i].x, cube_vertices[i].y, cube_vertices[i].z);
		draw_ui_text(text_buf, buf, y, 0.28f, color);
	}
}

static bool load_shaders(void)
{
	cube_dvlb = DVLB_ParseFile((u32*)cube_shbin, cube_shbin_size);
	if (!cube_dvlb) return false;

	if (R_FAILED(shaderProgramInit(&cube_program)))
	{
		DVLB_Free(cube_dvlb);
		cube_dvlb = NULL;
		return false;
	}

	shaderProgramSetVsh(&cube_program, &cube_dvlb->DVLE[0]);
	C3D_BindProgram(&cube_program);

	uLoc_projection = shaderInstanceGetUniformLocation(cube_program.vertexShader, "projection");
	uLoc_modelView  = shaderInstanceGetUniformLocation(cube_program.vertexShader, "modelView");
	if (uLoc_projection < 0 || uLoc_modelView < 0)
	{
		shaderProgramFree(&cube_program);
		DVLB_Free(cube_dvlb);
		cube_dvlb = NULL;
		return false;
	}

	AttrInfo_Init(&cube_attr_info);
	AttrInfo_AddLoader(&cube_attr_info, 0, GPU_FLOAT, 4);
	AttrInfo_AddLoader(&cube_attr_info, 1, GPU_FLOAT, 4);
	C3D_SetAttrInfo(&cube_attr_info);

	shader_loaded = true;
	return true;
}

static void unload_shaders(void)
{
	if (!shader_loaded) return;
	shaderProgramFree(&cube_program);
	shader_loaded = false;

	if (cube_dvlb)
	{
		DVLB_Free(cube_dvlb);
		cube_dvlb = NULL;
	}
}

int main(int argc, char** argv)
{
	(void)argc;
	(void)argv;
	srand((unsigned)svcGetSystemTick());

	gfxInitDefault();
	hidSetRepeatParameters(20, 10);

	bool c3d_initialized = false;
	bool c2d_initialized = false;
	bool ok = false;

	if (!C3D_Init(C3D_DEFAULT_CMDBUF_SIZE)) goto exit;
	c3d_initialized = true;

	if (!C2D_Init(C2D_DEFAULT_MAX_OBJECTS)) goto exit;
	c2d_initialized = true;
	C2D_Prepare();

	top_target = C3D_RenderTargetCreate((int)TOP_HEIGHT, (int)TOP_WIDTH, GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
	if (!top_target) goto exit;
	C3D_RenderTargetSetOutput(top_target, GFX_TOP, GFX_LEFT, DISPLAY_TRANSFER_FLAGS);

	bot_target = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
	if (!bot_target) goto exit;

	Mtx_PerspTilt(&proj_top, C3D_AngleFromDegrees(70.0f), C3D_AspectRatioTop, 0.1f, 100.0f, false);

	text_buf = C2D_TextBufNew(8192);
	if (!text_buf) goto exit;
	if (!load_shaders()) goto exit;

	while (aptMainLoop())
	{
		hidScanInput();
		u32 kDown = hidKeysDown();
		u32 kHeld = hidKeysHeld();
		if (kDown & KEY_START) break;

		handle_input(kDown, kHeld);

		if (!C3D_FrameBegin(C3D_FRAME_SYNCDRAW)) continue;

		C3D_FrameDrawOn(top_target);
		C3D_RenderTargetClear(top_target, C3D_CLEAR_ALL, 0x202020FF, 0);
		configure_cube_state();
		render_cube_faces();
		// Overlay passes: disable depth to avoid half-culled ribbons/markers and allow back-vertex picking
		C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_ALL);
		render_cube_wireframe();
		render_cube_points();

		C2D_TargetClear(bot_target, C2D_Color32(0x10, 0x10, 0x10, 0xFF));
		C2D_SceneBegin(bot_target);
		render_ui();

		C3D_FrameEnd(0);
	}
	ok = true;

exit:
	unload_shaders();
	if (text_buf) C2D_TextBufDelete(text_buf);
	if (top_target) C3D_RenderTargetDelete(top_target);
	if (bot_target) C3D_RenderTargetDelete(bot_target);
	if (c2d_initialized) C2D_Fini();
	if (c3d_initialized) C3D_Fini();
	gfxExit();
	return ok ? 0 : 1;
}
