/*
 * OpenGL RmlUI render interface for ezQuake - real modern-GL implementation.
 *
 * Self-contained: owns its shader, per-geometry VAO/VBO/IBO and textures,
 * and loads GL 2.0+/VAO entry points via SDL_GL_GetProcAddress. GL 1.1
 * entry points come from opengl32 (linked by the engine).
 */

#include "render_interface_gl.h"

#include <SDL_video.h>      // SDL_GL_GetProcAddress
#include <SDL_opengl.h>     // GL 1.1 + enums
#include <SDL_opengl_glext.h>

#include <cstddef>          // offsetof
#include <cstdio>
#include <cstring>

/* Engine console print, for diagnostics only. */
extern "C" void Com_Printf(const char* fmt, ...);

namespace {

// ---- GL 2.0+/VAO entry points loaded via SDL ----
PFNGLGENVERTEXARRAYSPROC        qglGenVertexArrays = nullptr;
PFNGLBINDVERTEXARRAYPROC        qglBindVertexArray = nullptr;
PFNGLDELETEVERTEXARRAYSPROC     qglDeleteVertexArrays = nullptr;
PFNGLGENBUFFERSPROC             qglGenBuffers = nullptr;
PFNGLBINDBUFFERPROC             qglBindBuffer = nullptr;
PFNGLBUFFERDATAPROC             qglBufferData = nullptr;
PFNGLDELETEBUFFERSPROC          qglDeleteBuffers = nullptr;
PFNGLENABLEVERTEXATTRIBARRAYPROC qglEnableVertexAttribArray = nullptr;
PFNGLVERTEXATTRIBPOINTERPROC    qglVertexAttribPointer = nullptr;
PFNGLCREATESHADERPROC           qglCreateShader = nullptr;
PFNGLSHADERSOURCEPROC           qglShaderSource = nullptr;
PFNGLCOMPILESHADERPROC          qglCompileShader = nullptr;
PFNGLGETSHADERIVPROC            qglGetShaderiv = nullptr;
PFNGLGETSHADERINFOLOGPROC       qglGetShaderInfoLog = nullptr;
PFNGLCREATEPROGRAMPROC          qglCreateProgram = nullptr;
PFNGLATTACHSHADERPROC           qglAttachShader = nullptr;
PFNGLLINKPROGRAMPROC            qglLinkProgram = nullptr;
PFNGLGETPROGRAMIVPROC           qglGetProgramiv = nullptr;
PFNGLGETPROGRAMINFOLOGPROC      qglGetProgramInfoLog = nullptr;
PFNGLUSEPROGRAMPROC             qglUseProgram = nullptr;
PFNGLDELETESHADERPROC           qglDeleteShader = nullptr;
PFNGLDELETEPROGRAMPROC          qglDeleteProgram = nullptr;
PFNGLGETUNIFORMLOCATIONPROC     qglGetUniformLocation = nullptr;
PFNGLUNIFORMMATRIX4FVPROC       qglUniformMatrix4fv = nullptr;
PFNGLUNIFORM2FPROC              qglUniform2f = nullptr;
PFNGLUNIFORM1IPROC              qglUniform1i = nullptr;
PFNGLACTIVETEXTUREPROC          qglActiveTexture = nullptr;

bool g_gl_loaded = false;

template <typename T>
bool Load(T& fn, const char* name)
{
	fn = reinterpret_cast<T>(SDL_GL_GetProcAddress(name));
	return fn != nullptr;
}

bool LoadGLFunctions()
{
	if (g_gl_loaded) {
		return true;
	}
	bool ok = true;
	ok &= Load(qglGenVertexArrays, "glGenVertexArrays");
	ok &= Load(qglBindVertexArray, "glBindVertexArray");
	ok &= Load(qglDeleteVertexArrays, "glDeleteVertexArrays");
	ok &= Load(qglGenBuffers, "glGenBuffers");
	ok &= Load(qglBindBuffer, "glBindBuffer");
	ok &= Load(qglBufferData, "glBufferData");
	ok &= Load(qglDeleteBuffers, "glDeleteBuffers");
	ok &= Load(qglEnableVertexAttribArray, "glEnableVertexAttribArray");
	ok &= Load(qglVertexAttribPointer, "glVertexAttribPointer");
	ok &= Load(qglCreateShader, "glCreateShader");
	ok &= Load(qglShaderSource, "glShaderSource");
	ok &= Load(qglCompileShader, "glCompileShader");
	ok &= Load(qglGetShaderiv, "glGetShaderiv");
	ok &= Load(qglGetShaderInfoLog, "glGetShaderInfoLog");
	ok &= Load(qglCreateProgram, "glCreateProgram");
	ok &= Load(qglAttachShader, "glAttachShader");
	ok &= Load(qglLinkProgram, "glLinkProgram");
	ok &= Load(qglGetProgramiv, "glGetProgramiv");
	ok &= Load(qglGetProgramInfoLog, "glGetProgramInfoLog");
	ok &= Load(qglUseProgram, "glUseProgram");
	ok &= Load(qglDeleteShader, "glDeleteShader");
	ok &= Load(qglDeleteProgram, "glDeleteProgram");
	ok &= Load(qglGetUniformLocation, "glGetUniformLocation");
	ok &= Load(qglUniformMatrix4fv, "glUniformMatrix4fv");
	ok &= Load(qglUniform2f, "glUniform2f");
	ok &= Load(qglUniform1i, "glUniform1i");
	ok &= Load(qglActiveTexture, "glActiveTexture");
	g_gl_loaded = ok;
	return ok;
}

const char* kVertexShader =
	"#version 330 core\n"
	"layout(location=0) in vec2 a_pos;\n"
	"layout(location=1) in vec4 a_color;\n"
	"layout(location=2) in vec2 a_uv;\n"
	"uniform mat4 u_projection;\n"
	"uniform vec2 u_translation;\n"
	"out vec4 v_color;\n"
	"out vec2 v_uv;\n"
	"void main(){\n"
	"    v_color = a_color;\n"
	"    v_uv = a_uv;\n"
	"    gl_Position = u_projection * vec4(a_pos + u_translation, 0.0, 1.0);\n"
	"}\n";

const char* kFragmentShader =
	"#version 330 core\n"
	"in vec4 v_color;\n"
	"in vec2 v_uv;\n"
	"uniform sampler2D u_texture;\n"
	"out vec4 frag;\n"
	"void main(){\n"
	"    frag = v_color * texture(u_texture, v_uv);\n"
	"}\n";

unsigned int CompileGLShader(unsigned int type, const char* src)
{
	unsigned int shader = qglCreateShader(type);
	qglShaderSource(shader, 1, &src, nullptr);
	qglCompileShader(shader);
	GLint ok = 0;
	qglGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		char log[512];
		qglGetShaderInfoLog(shader, sizeof(log), nullptr, log);
		Com_Printf("RmlUI GL: shader compile failed: %s\n", log);
		qglDeleteShader(shader);
		return 0;
	}
	return shader;
}

// Per-compiled-geometry GPU buffers.
struct GeometryGL {
	unsigned int vao = 0;
	unsigned int vbo = 0;
	unsigned int ibo = 0;
	int num_indices = 0;
};

} // namespace

namespace ezquake {
namespace rmlui {

bool RenderInterfaceGL::EnsureInitialised()
{
	if (initialised_) {
		return true;
	}
	if (init_failed_) {
		return false;
	}

	if (!LoadGLFunctions()) {
		Com_Printf("RmlUI GL: ERROR could not load required OpenGL entry points\n");
		init_failed_ = true;
		return false;
	}

	unsigned int vs = CompileGLShader(GL_VERTEX_SHADER, kVertexShader);
	unsigned int fs = CompileGLShader(GL_FRAGMENT_SHADER, kFragmentShader);
	if (!vs || !fs) {
		init_failed_ = true;
		return false;
	}

	program_ = qglCreateProgram();
	qglAttachShader(program_, vs);
	qglAttachShader(program_, fs);
	qglLinkProgram(program_);
	qglDeleteShader(vs);
	qglDeleteShader(fs);

	GLint linked = 0;
	qglGetProgramiv(program_, GL_LINK_STATUS, &linked);
	if (!linked) {
		char log[512];
		qglGetProgramInfoLog(program_, sizeof(log), nullptr, log);
		Com_Printf("RmlUI GL: ERROR program link failed: %s\n", log);
		qglDeleteProgram(program_);
		program_ = 0;
		init_failed_ = true;
		return false;
	}

	u_projection_ = qglGetUniformLocation(program_, "u_projection");
	u_translation_ = qglGetUniformLocation(program_, "u_translation");

	// 1x1 opaque white texture used when geometry carries no texture.
	const unsigned char white[4] = {255, 255, 255, 255};
	glGenTextures(1, &white_texture_);
	glBindTexture(GL_TEXTURE_2D, white_texture_);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glBindTexture(GL_TEXTURE_2D, 0);

	initialised_ = true;
	Com_Printf("RmlUI GL: render interface initialised\n");
	return true;
}

void RenderInterfaceGL::BeginFrame(int view_width, int view_height)
{
	if (!EnsureInitialised() || view_width <= 0 || view_height <= 0) {
		return;
	}

	// Save state we are about to clobber.
	glGetIntegerv(GL_CURRENT_PROGRAM, &saved_.program);
	glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &saved_.vertex_array);
	glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &saved_.array_buffer);
	glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &saved_.element_buffer);
	glGetIntegerv(GL_ACTIVE_TEXTURE, &saved_.active_texture);
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &saved_.texture_2d);
	glGetIntegerv(GL_VIEWPORT, saved_.viewport);
	saved_.blend = glIsEnabled(GL_BLEND);
	saved_.depth = glIsEnabled(GL_DEPTH_TEST);
	saved_.cull = glIsEnabled(GL_CULL_FACE);
	saved_.scissor = glIsEnabled(GL_SCISSOR_TEST);

	viewport_height_ = saved_.viewport[3];

	// Our 2D state.
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	glDisable(GL_SCISSOR_TEST);
	glEnable(GL_BLEND);
	glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA); // premultiplied alpha
	qglActiveTexture(GL_TEXTURE0);

	qglUseProgram(program_);

	// Orthographic projection, top-left origin, column-major.
	const float w = static_cast<float>(view_width);
	const float h = static_cast<float>(view_height);
	const float proj[16] = {
		2.0f / w, 0.0f,      0.0f, 0.0f,
		0.0f,    -2.0f / h,  0.0f, 0.0f,
		0.0f,     0.0f,     -1.0f, 0.0f,
	   -1.0f,     1.0f,      0.0f, 1.0f,
	};
	qglUniformMatrix4fv(u_projection_, 1, GL_FALSE, proj);

	in_frame_ = true;
}

void RenderInterfaceGL::EndFrame()
{
	if (!in_frame_) {
		return;
	}
	in_frame_ = false;

	// Restore prior GL state.
	glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(saved_.texture_2d));
	qglActiveTexture(static_cast<GLenum>(saved_.active_texture));
	qglBindVertexArray(static_cast<GLuint>(saved_.vertex_array));
	qglBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(saved_.array_buffer));
	qglBindBuffer(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLuint>(saved_.element_buffer));
	qglUseProgram(static_cast<GLuint>(saved_.program));

	if (saved_.depth) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
	if (saved_.cull) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
	if (saved_.blend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
	if (saved_.scissor) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
}

Rml::CompiledGeometryHandle RenderInterfaceGL::CompileGeometry(
	Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices)
{
	if (!EnsureInitialised()) {
		return 0;
	}

	GeometryGL* geo = new GeometryGL();
	geo->num_indices = static_cast<int>(indices.size());

	qglGenVertexArrays(1, &geo->vao);
	qglBindVertexArray(geo->vao);

	qglGenBuffers(1, &geo->vbo);
	qglBindBuffer(GL_ARRAY_BUFFER, geo->vbo);
	qglBufferData(GL_ARRAY_BUFFER,
		static_cast<GLsizeiptr>(vertices.size() * sizeof(Rml::Vertex)),
		vertices.data(), GL_STATIC_DRAW);

	qglGenBuffers(1, &geo->ibo);
	qglBindBuffer(GL_ELEMENT_ARRAY_BUFFER, geo->ibo);
	qglBufferData(GL_ELEMENT_ARRAY_BUFFER,
		static_cast<GLsizeiptr>(indices.size() * sizeof(int)),
		indices.data(), GL_STATIC_DRAW);

	const GLsizei stride = sizeof(Rml::Vertex);
	qglEnableVertexAttribArray(0);
	qglVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride,
		reinterpret_cast<void*>(offsetof(Rml::Vertex, position)));
	qglEnableVertexAttribArray(1);
	qglVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride,
		reinterpret_cast<void*>(offsetof(Rml::Vertex, colour)));
	qglEnableVertexAttribArray(2);
	qglVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride,
		reinterpret_cast<void*>(offsetof(Rml::Vertex, tex_coord)));

	qglBindVertexArray(0);
	return reinterpret_cast<Rml::CompiledGeometryHandle>(geo);
}

void RenderInterfaceGL::RenderGeometry(
	Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation, Rml::TextureHandle texture)
{
	if (!in_frame_ || !geometry) {
		return;
	}
	GeometryGL* geo = reinterpret_cast<GeometryGL*>(geometry);

	qglUniform2f(u_translation_, translation.x, translation.y);

	glBindTexture(GL_TEXTURE_2D,
		texture ? static_cast<GLuint>(texture) : white_texture_);

	qglBindVertexArray(geo->vao);
	glDrawElements(GL_TRIANGLES, geo->num_indices, GL_UNSIGNED_INT, nullptr);
	qglBindVertexArray(0);
}

void RenderInterfaceGL::ReleaseGeometry(Rml::CompiledGeometryHandle geometry)
{
	if (!geometry) {
		return;
	}
	GeometryGL* geo = reinterpret_cast<GeometryGL*>(geometry);
	if (g_gl_loaded) {
		qglDeleteBuffers(1, &geo->vbo);
		qglDeleteBuffers(1, &geo->ibo);
		qglDeleteVertexArrays(1, &geo->vao);
	}
	delete geo;
}

Rml::TextureHandle RenderInterfaceGL::LoadTexture(
	Rml::Vector2i& texture_dimensions, const Rml::String& source)
{
	// External image files are not wired yet (fonts/solid colours come via
	// GenerateTexture). Report failure so RmlUi skips the image.
	(void)source;
	texture_dimensions = Rml::Vector2i(0, 0);
	return 0;
}

Rml::TextureHandle RenderInterfaceGL::GenerateTexture(
	Rml::Span<const Rml::byte> source, Rml::Vector2i source_dimensions)
{
	if (!EnsureInitialised() || source_dimensions.x <= 0 || source_dimensions.y <= 0) {
		return 0;
	}

	GLuint tex = 0;
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
		source_dimensions.x, source_dimensions.y, 0,
		GL_RGBA, GL_UNSIGNED_BYTE, source.data());
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glBindTexture(GL_TEXTURE_2D, 0);

	return static_cast<Rml::TextureHandle>(tex);
}

void RenderInterfaceGL::ReleaseTexture(Rml::TextureHandle texture)
{
	if (texture) {
		GLuint tex = static_cast<GLuint>(texture);
		glDeleteTextures(1, &tex);
	}
}

void RenderInterfaceGL::EnableScissorRegion(bool enable)
{
	if (enable) {
		glEnable(GL_SCISSOR_TEST);
	}
	else {
		glDisable(GL_SCISSOR_TEST);
	}
}

void RenderInterfaceGL::SetScissorRegion(Rml::Rectanglei region)
{
	// RmlUi region is top-left origin; GL scissor is bottom-left.
	const int y = viewport_height_ - (region.Top() + region.Height());
	glScissor(region.Left(), y, region.Width(), region.Height());
}

void RenderInterfaceGL::Shutdown()
{
	if (!initialised_ || !g_gl_loaded) {
		return;
	}
	if (white_texture_) {
		glDeleteTextures(1, &white_texture_);
		white_texture_ = 0;
	}
	if (program_) {
		qglDeleteProgram(program_);
		program_ = 0;
	}
	initialised_ = false;
}

void RenderInterfaceGL::OnContextLost()
{
	/* Delete owned GL objects while the old context is still current. */
	Shutdown();

	/*
	 * Force a clean re-initialisation against the new context: entry points
	 * are reloaded (proc addresses may change between contexts) and a prior
	 * init failure no longer applies.
	 */
	g_gl_loaded = false;
	init_failed_ = false;
	in_frame_ = false;
	viewport_height_ = 0;
}

} // namespace rmlui
} // namespace ezquake
