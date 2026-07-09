/*
 * OpenGL RmlUI render interface for ezQuake - stub slice.
 *
 * Every method is a safe no-op so Rml::Context::Render() can run without
 * a real GL path yet. Geometry/texture handles are opaque non-zero values
 * (0 means failure in the RmlUi contract).
 */

#include "render_interface_gl.h"

namespace ezquake {
namespace rmlui {

Rml::CompiledGeometryHandle RenderInterfaceGL::CompileGeometry(
	Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices)
{
	(void)vertices;
	(void)indices;
	++geometry_compiled;
	// Non-zero opaque handle; no GPU resources are created in the stub.
	return static_cast<Rml::CompiledGeometryHandle>(geometry_compiled);
}

void RenderInterfaceGL::RenderGeometry(
	Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation, Rml::TextureHandle texture)
{
	(void)geometry;
	(void)translation;
	(void)texture;
	++geometry_rendered;
}

void RenderInterfaceGL::ReleaseGeometry(Rml::CompiledGeometryHandle geometry)
{
	(void)geometry;
}

Rml::TextureHandle RenderInterfaceGL::LoadTexture(
	Rml::Vector2i& texture_dimensions, const Rml::String& source)
{
	(void)source;
	// No texture loading yet: report failure so RmlUi skips the image.
	texture_dimensions = Rml::Vector2i(0, 0);
	return 0;
}

Rml::TextureHandle RenderInterfaceGL::GenerateTexture(
	Rml::Span<const Rml::byte> source, Rml::Vector2i source_dimensions)
{
	(void)source;
	(void)source_dimensions;
	return 0;
}

void RenderInterfaceGL::ReleaseTexture(Rml::TextureHandle texture)
{
	(void)texture;
}

void RenderInterfaceGL::EnableScissorRegion(bool enable)
{
	(void)enable;
}

void RenderInterfaceGL::SetScissorRegion(Rml::Rectanglei region)
{
	(void)region;
}

} // namespace rmlui
} // namespace ezquake
