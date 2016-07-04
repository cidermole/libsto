// -*- mode: c++; indent-tabs-mode: nil; tab-width:2  -*-
// Basic type definitions for code related to tightly packed tries
// (c) 2006-2012 Ulrich Germann

#ifndef __tpt_typedefs_h
#define __tpt_typedefs_h
#include <stdint.h>
namespace tpt
{
  typedef uint32_t      id_type;
  typedef uint8_t   offset_type;
  typedef uint32_t   count_type;
  typedef uint64_t filepos_type;
  typedef unsigned char   uchar;

  // magic = '0x%sULL' % ''.join(reversed(['%02x' % ord(c) for c in 'SaptIDX2']))
  const uint64_t INDEX_V2_MAGIC = 0x3258444974706153ULL; // magic number for encoding index file version. ASCII 'SaptIDX2'

  const uint64_t INDEX_V3_MAGIC = 0x3358444974706153ULL; // magic number for encoding index file version. ASCII 'SaptIDX3'

  /**
   * Header for corpus track.
   * This struct encodes the actual disk format and is sensitive to memory alignment (and byte order).
   *
   * If anybody ever uses this across several hosts with mixed endianness, in this time and age,
   * they shall be free to use htonl()/ntohl() calls to get this "endianed" properly.
   * For reference, the Truth shall be little endian on disk.
   */
  struct MttHeader {
    // to do: pack this.
    // Currently, the types align on 8 bytes and make us properly sized even on x64.
    // Currently, nobody uses sizeof(MttHeader), so this is fine too.

    uint64_t versionMagic;
    filepos_type startIdx;
    id_type idxSize;
    id_type totalWords;
  };

  /** Header for word alignments. */
  struct MamHeader : MttHeader {};

  /** Header for suffix arrays.
   *
   * .sfa file format:
   *
   * * TsaHeader
   * * positions: [{sid, offset}, ...]
   * * index: [positions_byte_offset, ...]
   *
   * see imTSA::save_as_mm_tsa() in ug_im_tsa.h
   */
  typedef struct __attribute__((packed)) {
    // packed: avoids padding to a multiple of 8 on x64. We use sizeof(TsaHeader) for a byte offset.

    uint64_t versionMagic;
    filepos_type idxStart; /** start of index block, byte offset from the file start */
    id_type idxSize; /** size of index block in number of entries */
  } TsaHeader;

  typedef struct __attribute__((packed)) {
    id_type sid;
    offset_type offset;
  } TsaPosition;

  const int INDEX_SEGMENT_DIGITS = 5; // model.en.seg00001.sfa
}
#endif
