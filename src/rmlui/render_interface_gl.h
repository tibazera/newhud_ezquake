/*
 * OpenGL RmlUI render interface for ezQuake.
 *
 * Derives from Rml::RenderInterface (RmlUi 6.x API). This first slice
 * implements the required interface with safe no-op stubs so the RmlUI
 * context can Update()/Render() without crashing; the actual OpenGL
 * implementation (ezQuake GL state/shader/buffer helpers) lands next.
 *
 * Intentionally engine-header free: pure C++/RmlUi.
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

	// -- Debug counters (stub phase) --
	int geometry_compiled = 0;
	int geometry_rendered = 0;
};

} // namespace rmlui
} // namespace ezquake

#endif /* EZQUAKE_RMLUI_RENDER_INTERFACE_GL_H */
