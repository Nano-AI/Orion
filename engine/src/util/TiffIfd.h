/*  TIFF image file directories, little-endian, as TIFF 6.0 (Adobe, 1992) §2
 *  lays them out: a 12-byte entry per tag, ascending by tag, and a value wider
 *  than four bytes stored after the directory at an even offset. Written by
 *  the DNG writer and, off macOS, by export's TIFF and EXIF (an EXIF block is
 *  a TIFF header and its directories with no image).
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

namespace orion::util::tiff {

// Field types, TIFF 6.0 §2.
constexpr std::uint16_t kByte      = 1;
constexpr std::uint16_t kAscii     = 2;
constexpr std::uint16_t kShort     = 3;
constexpr std::uint16_t kLong      = 4;
constexpr std::uint16_t kRational  = 5;
constexpr std::uint16_t kUndefined = 7;
constexpr std::uint16_t kSRational = 10;

void put16(std::vector<std::uint8_t>& out, std::uint16_t v);
void put32(std::vector<std::uint8_t>& out, std::uint32_t v);

/// One entry, its payload already the final little-endian bytes. Whether it
/// rides inline or after the directory is decided from its size alone.
struct Entry {
    std::uint16_t tag  = 0;
    std::uint16_t type = 0;
    std::uint32_t count = 0;
    std::vector<std::uint8_t> data;
};
using Ifd = std::vector<Entry>;

Entry shortEntry(std::uint16_t tag, std::initializer_list<std::uint16_t> vs);
Entry longEntry(std::uint16_t tag, std::uint32_t v);
Entry byteEntry(std::uint16_t tag, std::initializer_list<std::uint8_t> vs);
Entry bytesEntry(std::uint16_t tag, std::uint16_t type, const std::uint8_t* data, std::size_t n);
Entry asciiEntry(std::uint16_t tag, const std::string& s);
/// Numerator and denominator pairs, written as they are.
Entry rationalEntry(std::uint16_t tag,
                    std::initializer_list<std::pair<std::uint32_t, std::uint32_t>> vs);
/// Each value times `den`, rounded; negatives clamp to zero.
Entry rationalEntry(std::uint16_t tag, const float* vs, std::size_t n, std::uint32_t den = 1000000);
/// den = 10000 is the precision Adobe's own converter writes colour matrices
/// at; LibRaw's arrive at four decimal places anyway.
Entry srationalEntry(std::uint16_t tag, const float* vs, std::size_t n, std::int32_t den = 10000);

/// The directory's own bytes: count, entries, next-IFD offset.
[[nodiscard]] std::uint32_t ifdBytes(const Ifd& ifd);
/// What its out-of-line values take, each padded to even.
[[nodiscard]] std::uint32_t externalBytes(const Ifd& ifd);

void sortByTag(Ifd& ifd);

/// Writes `ifd` at `offset` of `out` (growing it), with no next directory,
/// and its wide values straight after it.
void writeIfd(std::vector<std::uint8_t>& out, const Ifd& ifd, std::uint32_t offset);

/// A file's head: header, IFD0, and the EXIF (34665) and GPS (34853)
/// directories IFD0 points at when they are not empty, the pointers added
/// here. When IFD0 carries StripOffsets (273), with one LONG per strip, they
/// are set to follow the head (its size, rounded to even) back to back, each
/// `strips[i]` bytes long; no `strips` is one strip.
[[nodiscard]] std::vector<std::uint8_t> layout(Ifd ifd0, Ifd exif = {}, Ifd gps = {},
                                               const std::vector<std::uint32_t>& strips = {});

}  // namespace orion::util::tiff
