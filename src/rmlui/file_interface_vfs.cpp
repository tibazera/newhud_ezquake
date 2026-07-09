/*
 * RmlUi file interface backed by ezQuake's virtual filesystem.
 *
 * Each open file is either a VFS handle (game dirs + paks via FS_OpenVFS
 * with FS_ANY) or a stdio fallback (cwd-relative loose files). Seeks are
 * normalised to absolute positions so negative SEEK_CUR/SEEK_END offsets
 * work despite VFS_SEEK taking an unsigned position.
 */

/*
 * RmlUi (and the STL it pulls in) must be included BEFORE the engine
 * headers: q_shared.h defines function-style macros (min/max/bound) that
 * poison <algorithm> and friends.
 */
#include "file_interface_vfs.h"

#include <cstdio>

/* Engine headers inside extern "C"; they assume q_shared.h types. */
extern "C" {
#include "quakedef.h"
#include "fs.h"
}

namespace {

struct RmlVfsFile {
	vfsfile_t* vfs = nullptr;
	FILE* stdio = nullptr;
};

RmlVfsFile* AsOpenFile(Rml::FileHandle file)
{
	return reinterpret_cast<RmlVfsFile*>(file);
}

} // namespace

namespace ezquake {
namespace rmlui {

Rml::FileHandle FileInterfaceVFS::Open(const Rml::String& path)
{
	RmlVfsFile* handle = new RmlVfsFile();

	/* Quake filesystem first: game dirs and paks. */
	handle->vfs = FS_OpenVFS(path.c_str(), const_cast<char*>("rb"), FS_ANY);
	if (handle->vfs) {
		return reinterpret_cast<Rml::FileHandle>(handle);
	}

	/* Fallback: loose file relative to the working directory. */
	handle->stdio = fopen(path.c_str(), "rb");
	if (handle->stdio) {
		return reinterpret_cast<Rml::FileHandle>(handle);
	}

	delete handle;
	return 0;
}

void FileInterfaceVFS::Close(Rml::FileHandle file)
{
	RmlVfsFile* handle = AsOpenFile(file);
	if (!handle) {
		return;
	}
	if (handle->vfs) {
		VFS_CLOSE(handle->vfs);
	}
	if (handle->stdio) {
		fclose(handle->stdio);
	}
	delete handle;
}

size_t FileInterfaceVFS::Read(void* buffer, size_t size, Rml::FileHandle file)
{
	RmlVfsFile* handle = AsOpenFile(file);
	if (!handle || size == 0) {
		return 0;
	}
	if (handle->vfs) {
		vfserrno_t err = VFSERR_NONE;
		int bytes = VFS_READ(handle->vfs, buffer, static_cast<int>(size), &err);
		return bytes > 0 ? static_cast<size_t>(bytes) : 0;
	}
	return fread(buffer, 1, size, handle->stdio);
}

bool FileInterfaceVFS::Seek(Rml::FileHandle file, long offset, int origin)
{
	RmlVfsFile* handle = AsOpenFile(file);
	if (!handle) {
		return false;
	}
	if (handle->vfs) {
		/* Normalise to an absolute position: VFS_SEEK's position parameter
		 * is unsigned, so relative negative offsets must be resolved here. */
		long base = 0;
		switch (origin) {
			case SEEK_SET: base = 0; break;
			case SEEK_CUR: base = static_cast<long>(VFS_TELL(handle->vfs)); break;
			case SEEK_END: base = static_cast<long>(VFS_GETLEN(handle->vfs)); break;
			default: return false;
		}
		long target = base + offset;
		if (target < 0) {
			return false;
		}
		return VFS_SEEK(handle->vfs, static_cast<unsigned long>(target), SEEK_SET) == 0;
	}
	return fseek(handle->stdio, offset, origin) == 0;
}

size_t FileInterfaceVFS::Tell(Rml::FileHandle file)
{
	RmlVfsFile* handle = AsOpenFile(file);
	if (!handle) {
		return 0;
	}
	if (handle->vfs) {
		return static_cast<size_t>(VFS_TELL(handle->vfs));
	}
	long pos = ftell(handle->stdio);
	return pos > 0 ? static_cast<size_t>(pos) : 0;
}

size_t FileInterfaceVFS::Length(Rml::FileHandle file)
{
	RmlVfsFile* handle = AsOpenFile(file);
	if (!handle) {
		return 0;
	}
	if (handle->vfs) {
		return static_cast<size_t>(VFS_GETLEN(handle->vfs));
	}

	long pos = ftell(handle->stdio);
	fseek(handle->stdio, 0, SEEK_END);
	long length = ftell(handle->stdio);
	fseek(handle->stdio, pos, SEEK_SET);
	return length > 0 ? static_cast<size_t>(length) : 0;
}

} // namespace rmlui
} // namespace ezquake
