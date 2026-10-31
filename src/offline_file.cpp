#include "offline_file.hpp"

#include "cache_filesystem_config.hpp"
#include "cache_httpfs_instance_state.hpp"
#include "disk_cache_util.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/local_file_system.hpp"
#include "duckdb/common/optional.hpp"
#include "duckdb/common/string_util.hpp"
#include "utils/include/page_aligned_data_chunk.hpp"

#include <cstring>
#include <string>

namespace duckdb {

namespace {

// A real chunk never has size 0, so the facts can't be mistaken for one, and the name still reads as a cache file's.
DiskCacheUtil::LocalCacheDestination FactsDestination(const InstanceConfig &config, const string &path,
                                                      string &cache_directory) {
	const auto path_info = DiskCacheUtil::BuildRemoteFileCachePathInfo(config.on_disk_cache_directories, path);
	cache_directory = path_info.cache_directory;
	const auto cache_file = DiskCacheUtil::GetLocalCacheFile(path_info, /*start_offset=*/0, /*bytes_to_read=*/0);
	return DiskCacheUtil::ResolveLocalCacheDestination(path_info.cache_directory, cache_file.cache_filepath, path);
}

} // namespace

OfflineFileHandle::OfflineFileHandle(OfflineFileSystem &fs, string path, FileOpenFlags flags, KeptFileFacts facts_p)
    : FileHandle(fs, std::move(path), flags), facts(std::move(facts_p)) {
}

void ThrowNotCached(const string &path) {
	throw IOException("%s is not cached on this machine, and there is no network to read it", path);
}

void OfflineFileSystem::Read(FileHandle &handle, void *buffer, int64_t nr_bytes, idx_t location) {
	ThrowNotCached(handle.GetPath());
}

int64_t OfflineFileSystem::Read(FileHandle &handle, void *buffer, int64_t nr_bytes) {
	ThrowNotCached(handle.GetPath());
}

int64_t OfflineFileSystem::GetFileSize(FileHandle &handle) {
	return handle.Cast<OfflineFileHandle>().facts.file_size;
}

timestamp_t OfflineFileSystem::GetLastModifiedTime(FileHandle &handle) {
	return handle.Cast<OfflineFileHandle>().facts.last_modification_time;
}

string OfflineFileSystem::GetVersionTag(FileHandle &handle) {
	return handle.Cast<OfflineFileHandle>().facts.version_tag;
}

FileType OfflineFileSystem::GetFileType(FileHandle &handle) {
	return FileType::FILE_TYPE_REGULAR;
}

FileMetadata OfflineFileSystem::Stats(FileHandle &handle) {
	FileMetadata metadata;
	metadata.file_size = GetFileSize(handle);
	metadata.last_modification_time = GetLastModifiedTime(handle);
	metadata.file_type = FileType::FILE_TYPE_REGULAR;
	return metadata;
}

void OfflineFileSystem::Seek(FileHandle &handle, idx_t location) {
	handle.Cast<OfflineFileHandle>().position = location;
}

void OfflineFileSystem::Reset(FileHandle &handle) {
	handle.Cast<OfflineFileHandle>().position = 0;
}

idx_t OfflineFileSystem::SeekPosition(FileHandle &handle) {
	return handle.Cast<OfflineFileHandle>().position;
}

void StoreKeptFileFacts(const InstanceConfig &config, const string &path, const KeptFileFacts &facts) {
	if (config.cache_type != *ON_DISK_CACHE_TYPE) {
		return;
	}
	string cache_directory;
	const auto cache_dest = FactsDestination(config, path, cache_directory);
	const string text = std::to_string(facts.file_size) + "\n" + std::to_string(facts.last_modification_time.value) +
	                    "\n" + facts.version_tag;
	auto content = AllocatePageAlignedChunk(text.size());
	std::memcpy(content.data(), text.data(), text.size());
	content.length = text.size();
	DiskCacheUtil::StoreLocalCacheFile(cache_directory, cache_dest, content, /*version_tag=*/"", config,
	                                   []() { return optional<string> {}; });
}

bool TryReadKeptFileFacts(const InstanceConfig &config, const string &path, KeptFileFacts &facts) {
	if (config.cache_type != *ON_DISK_CACHE_TYPE) {
		return false;
	}
	try {
		string cache_directory;
		const auto cache_dest = FactsDestination(config, path, cache_directory);
		LocalFileSystem local_filesystem {};
		auto handle = local_filesystem.OpenFile(cache_dest.dest_local_filepath, FileOpenFlags::FILE_FLAGS_READ |
		                                                                            FileOpenFlags::FILE_FLAGS_NULL_IF_NOT_EXISTS);
		if (handle == nullptr) {
			return false;
		}
		string text(static_cast<size_t>(local_filesystem.GetFileSize(*handle)), '\0');
		local_filesystem.Read(*handle, const_cast<char *>(text.data()), static_cast<int64_t>(text.size()), /*location=*/0);

		// The version tag is last, and may be empty.
		const auto size_end = text.find('\n');
		const auto time_end = size_end == string::npos ? string::npos : text.find('\n', size_end + 1);
		if (time_end == string::npos) {
			return false;
		}
		facts.file_size = std::stoll(text.substr(0, size_end));
		facts.last_modification_time = timestamp_t(std::stoll(text.substr(size_end + 1, time_end - size_end - 1)));
		facts.version_tag = text.substr(time_end + 1);
		return true;
	} catch (const std::exception &) {
		// Unreadable is not kept: the caller says the file isn't cached.
		return false;
	}
}

} // namespace duckdb
