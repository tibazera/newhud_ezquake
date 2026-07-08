/*
 * OpenGL RmlUI renderer placeholder.
 *
 * The real implementation will derive from Rml::RenderInterface and use
 * ezQuake's OpenGL state/shader/buffer helpers.
 */

#ifndef EZQUAKE_RMLUI_RENDER_INTERFACE_GL_H
#define EZQUAKE_RMLUI_RENDER_INTERFACE_GL_H

namespace ezquake::rmlui {

class RenderInterfaceGL {
public:
	void Initialise();
	void Shutdown();
};

} // namespace ezquake::rmlui

#endif /* EZQUAKE_RMLUI_RENDER_INTERFACE_GL_H */
