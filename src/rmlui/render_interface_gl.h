/*
 * OpenGL RmlUI render interface for ezQuake.
 *
 * Derives from Rml::RenderInterface (RmlUi 6.x). Self-contained modern-GL
 * renderer: it owns its shader program, per-geometry VAO/VBO/IBO and
 * textures, and loads the required GL entry points itself (the engine's
 * GL function pointers are static-per-file and not reusable). Only GL is
 * touched here - no ezQuake engine headers - so the bridge in
 * hud_rmlui.cpp drives BeginFrame()/EndFrame() around Context::Render().
 *
 * Vertex colours are premultiplied-alpha (RmlUi 6.x), so blending uses
 * (GL_ONE, GL_ONE_MINUS_SRC_ALPHA).
 */

#ifndef EZQUAKE_RMLUI_RENDER_INTERFACE_GL_H
#define EZQUAKE_RMLUI_RENDER_INTERFACE_GL_H

#include <RmlUi/Core/RenderInterface.h>

namespace ezquake {
namespace rmlui {

class RenderInterfaceGL : public Rml::RenderInterface {
public:
	RenderInterfaceGL() = default;
	~RenderInterfaceGL() override = default;

	// -- Frame state management (called by the HUD bridge) --
	// Sets up GL state and the orthographic projection for a view of the
	// given size (top-left origin, pixels), then restores prior GL state.
	void BeginFrame(int view_width, int view_height);
	void EndFrame();

	// Releases all GL resources. Call while the GL context is still valid.
	void Shutdown();

	// GL context is about to be destroyed (vid_restart). Deletes the owned
	// GL objects while the context is still current, then resets to the
	// uninitialised state and forces GL entry points to be reloaded on next
	// use (proc addresses may differ in the new context). Callers must have
	// already released RmlUi-owned resources (Rml::ReleaseCompiledGeometry /
	// Rml::ReleaseTextures) so no stale handles survive.
	void OnContextLost();

	// -- Required interface (RmlUi 6.x pure virtuals) --

	Rml::CompiledGeometryHandle CompileGeometry(
		Rml::Span<const Rml::Vertex> vertices,
		Rml::Span<const int> indices) override;

	void RenderGeometry(
		Rml::CompiledGeometryHandle geometry,
		Rml::Vector2f translation,
		Rml::TextureHandle texture) override;

	void ReleaseGeometry(Rml::CompiledGeometryHandle geometry) override;

	Rml::TextureHandle LoadTexture(
		Rml::Vector2i& texture_dimensions,
		const Rml::String& source) override;

	Rml::TextureHandle GenerateTexture(
		Rml::Span<const Rml::byte> source,
		Rml::Vector2i source_dimensions) override;

	void ReleaseTexture(Rml::TextureHandle texture) override;

	void EnableScissorRegion(bool enable) override;
	void SetScissorRegion(Rml::Rectanglei region) override;

private:
	bool EnsureInitialised();

	bool initialised_ = false;
	bool init_failed_ = false;
	bool in_frame_ = false;

	unsigned int program_ = 0;       // GLuint shader program
	unsigned int white_texture_ = 0; // 1x1 white, bound when geometry has no texture
	int u_translation_ = -1;
	int u_projection_ = -1;
	int viewport_height_ = 0;        // GL framebuffer height, for scissor Y-flip

	// Saved GL state between BeginFrame()/EndFrame(). Restoration must be
	// EXACT: ezQuake caches GL state (gl_state.c, opengl.rendering_state)
	// and skips redundant calls, so any raw state we leave changed behind
	// its back desyncs the cache and corrupts subsequent engine rendering.
	struct SavedState {
		int program;
		int vertex_array;
		int array_buffer;
		int element_buffer;
		int active_texture;
		int texture_2d;
		unsigned char blend, depth, cull, scissor;
		int blend_src_rgb, blend_dst_rgb, blend_src_alpha, blend_dst_alpha;
		int scissor_box[4];
		int unpack_alignment;
		int viewport[4];
	} saved_{};
};

} // namespace rmlui
} // namespace ezquake

#endif /* EZQUAKE_RMLUI_RENDER_INTERFACE_GL_H */
