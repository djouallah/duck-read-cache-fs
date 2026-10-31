// Opening a file with no network, from what the on-disk cache keeps.
//
// The cache keeps a file's bytes, but opening the file asks the real filesystem for its size and version first. Offline
// that request can't be answered, so what it answered is kept too, as one more cache entry for the file. An open then
// gets a handle built from those facts: a kept chunk reads as it always did, and a chunk never read is an error.

#pragma once

#include "duckdb/common/file_system.hpp"
#include "duckdb/common/string.hpp"
#include "duckdb/common/types/timestamp.hpp"

namespace duckdb {

struct InstanceConfig;

// What opening a file answered.
struct KeptFileFacts {
	int64_t file_size = 0;
	timestamp_t last_modification_time = timestamp_t::ninfinity();
	string version_tag;
};

// Answers for handles opened offline, and reads nothing: a read reaches it only when the chunk isn't kept.
class OfflineFileSystem : public FileSystem {
public:
	void Read(FileHandle &handle, void *buffer, int64_t nr_bytes, idx_t location) override;
	int64_t Read(FileHandle &handle, void *buffer, int64_t nr_bytes) override;
	int64_t GetFileSize(FileHandle &handle) override;
	timestamp_t GetLastModifiedTime(FileHandle &handle) override;
	string GetVersionTag(FileHandle &handle) override;
	FileType GetFileType(FileHandle &handle) override;
	FileMetadata Stats(FileHandle &handle) override;
	void Seek(FileHandle &handle, idx_t location) override;
	void Reset(FileHandle &handle) override;
	idx_t SeekPosition(FileHandle &handle) override;
	bool CanSeek() override {
		return true;
	}
	bool OnDiskFile(FileHandle &handle) override {
		return false;
	}
	string GetName() const override {
		return "cache_httpfs_offline";
	}
};

class OfflineFileHandle : public FileHandle {
public:
	OfflineFileHandle(OfflineFileSystem &fs, string path, FileOpenFlags flags, KeptFileFacts facts_p);
	void Close() override {
	}

	KeptFileFacts facts;
	idx_t position = 0;
};

// Thrown wherever a file, or a part of it, isn't kept.
[[noreturn]] void ThrowNotCached(const string &path);

// Keep what opening [path] answered. A failure is not reported: it only costs that file its offline read.
void StoreKeptFileFacts(const InstanceConfig &config, const string &path, const KeptFileFacts &facts);

// Returns false when nothing is kept for [path].
bool TryReadKeptFileFacts(const InstanceConfig &config, const string &path, KeptFileFacts &facts);

} // namespace duckdb
