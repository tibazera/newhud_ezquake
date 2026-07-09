/*
 * RmlUi file interface backed by ezQuake's virtual filesystem.
 *
 * Documents, stylesheets and fonts resolve through the quake filesystem
 * (game dirs and paks, FS_ANY) first, falling back to plain stdio relative
 * to the current working directory so a loose ui/ tree next to the
 * executable keeps working during development.
 */

#ifndef EZQUAKE_RMLUI_FILE_INTERFACE_VFS_H
#define EZQUAKE_RMLUI_FILE_INTERFACE_VFS_H

#include <RmlUi/Core/FileInterface.h>

namespace ezquake {
namespace rmlui {

class FileInterfaceVFS : public Rml::FileInterface {
public:
	FileInterfaceVFS() = default;
	~FileInterfaceVFS() override = default;

	Rml::FileHandle Open(const Rml::String& path) override;
	void Close(Rml::FileHandle file) override;
	size_t Read(void* buffer, size_t size, Rml::FileHandle file) override;
	bool Seek(Rml::FileHandle file, long offset, int origin) override;
	size_t Tell(Rml::FileHandle file) override;
	size_t Length(Rml::FileHandle file) override;
};

} // namespace rmlui
} // namespace ezquake

#endif /* EZQUAKE_RMLUI_FILE_INTERFACE_VFS_H */
