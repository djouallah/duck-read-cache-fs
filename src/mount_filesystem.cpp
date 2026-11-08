#include "mount_filesystem.hpp"

#include "duckdb/common/local_file_system.hpp"
#include "duckdb/common/string_util.hpp"

#include <algorithm>

namespace duckdb {

namespace {

// With forward slashes, no extended-length prefix and no trailing slash: how a path looks is not where it is.
string Clean(const string &path) {
	string cleaned = path;
	std::replace(cleaned.begin(), cleaned.end(), '\\', '/');
	if (StringUtil::StartsWith(cleaned, "//?/")) {
		cleaned = cleaned.substr(4);
	}
	while (cleaned.size() > 1 && cleaned.back() == '/') {
		cleaned.pop_back();
	}
	return cleaned;
}

string Comparable(const string &cleaned) {
#ifdef _WIN32
	return StringUtil::Lower(cleaned);
#else
	return cleaned;
#endif
}

} // namespace

void MountTable::Add(const string &local_prefix, const string &remote_prefix) {
	const auto local = Clean(local_prefix);
	string remote = remote_prefix;
	while (remote.size() > 1 && remote.back() == '/') {
		remote.pop_back();
	}
	std::lock_guard<std::mutex> lock(mu);
	for (auto &mount : mounts) {
		if (Comparable(mount.first) == Comparable(local)) {
			mount.second = remote;
			return;
		}
	}
	mounts.emplace_back(local, remote);
}

bool MountTable::Remove(const string &local_prefix) {
	const auto local = Comparable(Clean(local_prefix));
	std::lock_guard<std::mutex> lock(mu);
	const auto found = std::find_if(mounts.begin(), mounts.end(), [&](const std::pair<string, string> &mount) {
		return Comparable(mount.first) == local;
	});
	if (found == mounts.end()) {
		return false;
	}
	mounts.erase(found);
	return true;
}

bool MountTable::Resolve(const string &path, string &resolved) const {
	const auto cleaned = Clean(path);
	const auto comparable = Comparable(cleaned);
	std::lock_guard<std::mutex> lock(mu);
	for (const auto &mount : mounts) {
		const auto prefix = Comparable(mount.first);
		if (comparable.size() < prefix.size() || comparable.compare(0, prefix.size(), prefix) != 0) {
			continue;
		}
		// A whole path segment: /a/b doesn't mount /a/bc.
		if (comparable.size() > prefix.size() && comparable[prefix.size()] != '/') {
			continue;
		}
		resolved = mount.second + cleaned.substr(prefix.size());
		return true;
	}
	return false;
}

bool MountTable::FirstUse() {
	std::lock_guard<std::mutex> lock(mu);
	const bool first = !used;
	used = true;
	return first;
}

MountFileSystem::MountFileSystem(FileSystem &vfs_p, shared_ptr<MountTable> table_p)
    : vfs(vfs_p), table(std::move(table_p)), local(FileSystem::CreateLocal()) {
}

bool MountFileSystem::IsHere(const string &path) {
	try {
		return local->FileExists(path);
	} catch (const std::exception &) {
		return false;
	}
}

string MountFileSystem::Resolved(const string &path) const {
	string resolved;
	// It was mounted when this filesystem was picked, and could have gone since.
	return table->Resolve(path, resolved) ? resolved : path;
}

bool MountFileSystem::CanHandleFile(const string &fpath) {
	string resolved;
	return table->Resolve(fpath, resolved);
}

unique_ptr<FileHandle> MountFileSystem::OpenFile(const string &path, FileOpenFlags flags,
                                                 optional_ptr<FileOpener> opener) {
	if (IsHere(path)) {
		return local->OpenFile(path, flags, opener);
	}
	return vfs.OpenFile(Resolved(path), flags, opener);
}

unique_ptr<FileHandle> MountFileSystem::OpenFileExtended(const OpenFileInfo &file, FileOpenFlags flags,
                                                         optional_ptr<FileOpener> opener) {
	if (IsHere(file.path)) {
		return local->OpenFile(file.path, flags, opener);
	}
	OpenFileInfo resolved(file);
	resolved.path = Resolved(file.path);
	return vfs.OpenFile(resolved, flags, opener);
}

bool MountFileSystem::FileExists(const string &filename, optional_ptr<FileOpener> opener) {
	return IsHere(filename) || vfs.FileExists(Resolved(filename), opener);
}

bool MountFileSystem::DirectoryExists(const string &directory, optional_ptr<FileOpener> opener) {
	return vfs.DirectoryExists(Resolved(directory), opener);
}

bool MountFileSystem::ListFiles(const string &directory, const std::function<void(const string &, bool)> &callback,
                                FileOpener *opener) {
	return vfs.ListFiles(Resolved(directory), callback, opener);
}

vector<OpenFileInfo> MountFileSystem::Glob(const string &path, FileOpener *opener) {
	// A reader expands its path first and opens what comes back: a file that's here comes back as itself, or the
	// remote path it resolves to is what gets opened.
	if (!FileSystem::HasGlob(path) && IsHere(path)) {
		return {OpenFileInfo(path)};
	}
	return vfs.Glob(Resolved(path), opener);
}

optional<FileMetadata> MountFileSystem::GetStatsIfExists(const OpenFileInfo &file, optional_ptr<FileOpener> opener) {
	if (IsHere(file.path)) {
		return local->GetStatsIfExists(file, opener);
	}
	OpenFileInfo resolved(file);
	resolved.path = Resolved(file.path);
	return vfs.GetStatsIfExists(resolved, opener);
}

} // namespace duckdb
