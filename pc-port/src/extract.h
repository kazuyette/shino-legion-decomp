#ifndef EXTRACT_H
#define EXTRACT_H
#include <stddef.h>
#include <stdint.h>
/* PC: builds the assets/ folder from the player's own disc image (plain C, no SDL), so the
 * port can be distributed without game data.
 *
 * Input: a .cue sheet (FILE / TRACK / INDEX / PREGAP / POSTGAP; one or several .bin files;
 * data track MODE1/2352, MODE1/2048, MODE2/2352 or MODE2/2336; audio 2352), a .bin (its .cue
 * is used when one with the same name sits next to it, else the .bin is read as a lone
 * data track) or a plain .iso (2048-byte sectors; data track only).
 * Output (out_dir, created if needed), every file listed in asset_manifest.h:
 *   - the ISO9660 files of the data track under their disc names (root + subdirectories),
 *   - DIRECTOR.dec.bin / SHINOBI.dec.bin = DIRECTOR.PPB / SHINOBI.PPB decompressed (lz.c),
 *   - TRACK02.WAV / TRACK03.WAV = CD-DA tracks 2 and 3 (44.1 kHz 16-bit stereo LE), cut
 *     exactly like tools/rip_cdda.py: the extents of TITLE.PCM / ENDING.PCM in the ISO
 *     directory give the disc LBA and length, the cue only maps LBA -> .bin frame (the
 *     DW0536 cue's TRACK 03 INDEX 01 is 150 frames late; the ISO extents are not).
 * Each file is written to NAME.part, checked against the manifest (size + CRC-32) and then
 * renamed over NAME. A data file that does not match is not installed (bad dump / other
 * version); a WAV that does not match is installed anyway with a warning (audio read
 * offsets differ between dumping tools).
 *
 * Returns EXTRACT_OK, a warning (> 0: the game files are complete, the music is missing or
 * differs) or an error (< 0); Extract_LastError() has a one-line explanation. */

enum {
    EXTRACT_OK                 =  0,
    EXTRACT_WARN_NO_AUDIO      =  1,   /* game files OK, CD-DA track(s) missing (.iso, no audio .bin) */
    EXTRACT_WARN_AUDIO_DIFFERS =  2,   /* game files OK, a WAV differs from the reference (installed) */
    EXTRACT_ERR_ARGS           = -1,
    EXTRACT_ERR_OPEN           = -2,   /* image / cue / data .bin cannot be opened */
    EXTRACT_ERR_CUE            = -3,   /* cue sheet without a usable data track */
    EXTRACT_ERR_NOT_SATURN     = -4,   /* no Saturn header / no ISO9660 file system */
    EXTRACT_ERR_WRONG_DISC     = -5,   /* another Saturn game */
    EXTRACT_ERR_REGION         = -6,   /* Shinobi, but not the USA release (T-2301H) */
    EXTRACT_ERR_BAD_DUMP       = -7,   /* truncated image, missing file or checksum mismatch */
    EXTRACT_ERR_WRITE          = -8,   /* cannot create / write the output folder */
    EXTRACT_ERR_NOMEM          = -9
};

/* progress(msg, pct): msg = current step or file name, pct = 0..100. May be NULL. */
typedef void (*ExtractProgressFn)(const char *msg, int pct);

int         Extract_FromCue(const char *cue_or_bin, const char *out_dir, ExtractProgressFn progress);
const char *Extract_LastError(void);            /* details of the last result ("" if none) */
const char *Extract_ResultName(int code);       /* short text for a result code */

/* 1 if path ends with .cue / .bin / .iso (any case). */
int Extract_IsImagePath(const char *path);
/* Cheap check of an asset folder against the manifest (names + sizes, no checksum).
 * Returns the number of required files missing or of the wrong size (optional ones,
 * the music, are not counted); the first one is copied to first_bad when not NULL. */
int Extract_CheckAssets(const char *dir, char *first_bad, size_t n);
/* First *.cue (any case, alphabetical) in dir -> out (dir + name). 1 = found. */
int Extract_FindImage(const char *dir, char *out, size_t n);

/* Manifest access (tests). */
int         Extract_ManifestCount(void);
const char *Extract_ManifestName(int i);
int         Extract_ManifestOptional(int i);
uint32_t    Extract_Crc32(uint32_t crc, const void *data, size_t len);   /* zlib CRC-32, start with 0 */

#endif
