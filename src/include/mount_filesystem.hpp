// A mount: paths under a local prefix are served from a remote one. A reader can then be pointed at a local folder
// that holds only part of something, with everything else read from where it really is, through the cache like any
// other remote read and under the remote path, so what's already cached is found. It's added by cache_httpfs_mount.
//
// A file that is really there under the mount is read where it is, with the local filesystem: a whole file kept on
// this machine reads at the disk's speed, with no cache in between and no request. A file is only ever put there
// whole (written beside and renamed in), and what's under the mount is never written through it.

#pragma once

#include "duckdb/common/file_system.hpp"
#include "duckdb/common/shared_ptr.hpp"
#include "duckdb/common/string.hpp"
#include "duckdb/common/vector.hpp"

#include <mutex>
#include <utility>

namespace duckdb {

class MountTable {
public:
	// Mounting a prefix again replaces what it points at.
	void Add(const string &local_prefix, const string &remote_prefix);
	// Returns whether there was such a mount.
	bool Remove(const string &local_prefix);
	// Where [path] really is. Returns false if it's under no mount.
	bool Resolve(const string &path, string &resolved) const;
	// True the first time it's called: the filesystem is registered then, so nothing changes until a mount is made.
	bool FirstUse();

private:
	mutable std::mutex mu;
	vector<std::pair<string, string>> mounts;
	bool used = false;
};

// Everything is passed on to the virtual filesystem under the resolved path, so a handle it opens belongs to whichever
// filesystem serves that path.
class MountFileSystem : public FileSystem {
public:
	MountFileSystem(FileSystem &vfs_p, shared_ptr<MountTable> table_p);

	bool CanHandleFile(const string &fpath) override;
	string GetName() const override {
		return "cache_httpfs_mount";
	}
	unique_ptr<FileHandle> OpenFile(const string &path, FileOpenFlags flags,
	                                optional_ptr<FileOpener> opener = nullptr) override;
	bool FileExists(const string &filename, optional_ptr<FileOpener> opener = nullptr) override;
	bool DirectoryExists(const string &directory, optional_ptr<FileOpener> opener = nullptr) override;
	bool ListFiles(const string &directory, const std::function<void(const string &, bool)> &callback,
	               FileOpener *opener = nullptr) override;
	vector<OpenFileInfo> Glob(const string &path, FileOpener *opener = nullptr) override;
	optional<FileMetadata> GetStatsIfExists(const OpenFileInfo &file,
	                                        optional_ptr<FileOpener> opener = nullptr) override;

protected:
	unique_ptr<FileHandle> OpenFileExtended(const OpenFileInfo &file, FileOpenFlags flags,
	                                        optional_ptr<FileOpener> opener) override;
	bool SupportsOpenFileExtended() const override {
		return true;
	}

private:
	string Resolved(const string &path) const;
	// Whether [path] is a file on this machine, read where it is.
	bool IsHere(const string &path);

	FileSystem &vfs;
	shared_ptr<MountTable> table;
	unique_ptr<FileSystem> local;
};

} // namespace duckdb
