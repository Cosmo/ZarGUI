# Applied to the fetched ZArchive sources (see CMakeLists.txt). Idempotent.
# Hardens the reader for untrusted archives, fixes decoding of names >= 128 bytes,
# and makes the writer's zstd compression level configurable (it was fixed at 6).
# Usage: cmake -DSRC=<zarchive source dir> -P patch-zarchive.cmake

function(patch_file file)
  file(READ "${file}" text)
  set(orig "${text}")
  math(EXPR last "${ARGC} - 1")
  foreach(i RANGE 1 ${last} 2)
    math(EXPR j "${i} + 1")
    set(from "${ARGV${i}}")
    set(to "${ARGV${j}}")
    string(FIND "${text}" "${to}" already)
    if(already EQUAL -1)
      string(FIND "${text}" "${from}" found)
      if(found EQUAL -1)
        message(FATAL_ERROR "patch-zarchive: pattern not found in ${file}:\n${from}")
      endif()
      string(REPLACE "${from}" "${to}" text "${text}")
    endif()
  endforeach()
  if(NOT text STREQUAL orig)
    file(WRITE "${file}" "${text}")
  endif()
endfunction()

# Section range check without integer overflow.
patch_file("${SRC}/include/zarchive/zarchivecommon.h"
  "return (offset + size) <= fileSize;"
  "return offset <= fileSize && size <= fileSize - offset;")

# Public accessor for a directory entry's node handle, so callers can walk the
# tree without path lookups and detect cycles in malformed archives.
patch_file("${SRC}/include/zarchive/zarchivereader.h"
  "	bool GetDirEntry(ZArchiveNodeHandle nodeHandle, uint32_t index, DirEntry& dirEntry) const;\n"
  "	bool GetDirEntry(ZArchiveNodeHandle nodeHandle, uint32_t index, DirEntry& dirEntry) const;\n	ZArchiveNodeHandle GetDirEntryNode(ZArchiveNodeHandle nodeHandle, uint32_t index) const;\n")

patch_file("${SRC}/src/zarchivereader.cpp"
  # Name table: off-by-one bound, and the high length byte is the second header byte.
  "if (nameOffset == 0x7FFFFFFF || nameOffset > nameTable.size())"
  "if (nameOffset == 0x7FFFFFFF || nameOffset >= nameTable.size())"
  "nameLength |= ((uint16_t)nameTable[nameOffset] << 7);"
  "nameLength |= ((uint16_t)nameTable[nameOffset + 1] << 7);"
  # Compressed block range check without integer overflow.
  "if ((offset + compressedSize) > m_compressedDataSize)"
  "if (offset > m_compressedDataSize || compressedSize > m_compressedDataSize - offset)"
  # Bounds-checked child lookup (added API).
  "uint64_t ZArchiveReader::GetFileSize(ZArchiveNodeHandle nodeHandle)\n"
  "ZArchiveNodeHandle ZArchiveReader::GetDirEntryNode(ZArchiveNodeHandle nodeHandle, uint32_t index) const\n{\n	if (nodeHandle >= m_fileTree.size())\n		return ZARCHIVE_INVALID_NODE;\n	auto& dir = m_fileTree[nodeHandle];\n	if (dir.IsFile() || index >= dir.directoryRecord.count)\n		return ZARCHIVE_INVALID_NODE;\n	uint64_t child = (uint64_t)dir.directoryRecord.nodeStartIndex + index;\n	if (child >= m_fileTree.size())\n		return ZARCHIVE_INVALID_NODE;\n	return (ZArchiveNodeHandle)child;\n}\n\nuint64_t ZArchiveReader::GetFileSize(ZArchiveNodeHandle nodeHandle)\n")

patch_file("${SRC}/include/zarchive/zarchivewriter.h"
  "	void Finalize();\n"
  "	void Finalize();\n	void SetCompressionLevel(int level) { m_compressionLevel = level; }\n"
  "	std::vector<uint8_t> m_compressionBuffer;\n"
  "	std::vector<uint8_t> m_compressionBuffer;\n	int m_compressionLevel{ 6 };\n")

patch_file("${SRC}/src/zarchivewriter.cpp"
  "uncompressedData, _ZARCHIVE::COMPRESSED_BLOCK_SIZE, 6);"
  "uncompressedData, _ZARCHIVE::COMPRESSED_BLOCK_SIZE, m_compressionLevel);")
