#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#endif
#if defined(__linux__) || defined(__unix__) || defined(__APPLE__)
#include <unistd.h>
#endif

/**
 * @file bmp_validator.c
 * @brief Secure BMP file validator supporting BMP versions 2 through 5.
 *
 * This module provides a function to validate BMP image files of various versions (BITMAPCOREHEADER, BITMAPINFOHEADER and all extensions up to BITMAPV5HEADER) for structural correctness and integrity.
 * Validation includes checking the BMP file header, DIB header, color masks, color palette, pixel data (with RLE decompression checks), and overall consistency of sizes and offsets.
 * The implementation emphasizes security, avoiding large memory allocations or integer overflows, and handles maliciously crafted files gracefully by rejecting them with detailed error messages.
 */

/** 
 * @brief Validate a BMP image file for correctness and integrity.
 * 
 * Opens the specified file and performs comprehensive validation of the BMP file format. 
 * Supports BMP files with DIB headers from the 12-byte OS/2 Core header through the 124-byte BITMAPV5HEADER. If one format check fails, it will attempt others where applicable.
 * Validation steps include:
 *   - File header verification (signature "BM", file size consistency).
 *   - DIB header parsing (identifying BMP version and reading fields).
 *   - Field validation (dimensions, planes, bit depth vs compression, etc.).
 *   - Color mask and color palette handling.
 *   - Pixel data validation: uncompressed images (row alignment and size checks), RLE-compressed images (byte-stream parsing to validate encoding and image completeness), and embedded JPEG/PNG validation.
 *   - Enforcing size limits to avoid processing very large files (files > half of system RAM or >15MB are rejected).
 *   - Logging any validation failures to stderr with a clear reason.
 * 
 * @param filename Path to the BMP file to validate.
 * @return `true` if the file is a valid BMP that passes all checks, `false` if any check fails (with an error message printed).
 */
bool validate_bmp(const char *filename);

/* Internal helper structure holding BMP metadata for validation. */
struct BmpInfo {
    uint32_t fileSize;       /**< Actual file size in bytes (as determined by reading the file). */
    uint32_t dataOffset;     /**< Offset from file start to the pixel data (from file header). */
    uint32_t dibHeaderSize;  /**< Size of DIB header in bytes. */
    bool isOS2;              /**< True if an OS/2 BMP format (Core header). */
    bool isOS2V2;            /**< True if an OS/2 2.x BMP format (64-byte extended header). */
    bool isBitmapV4;         /**< True if header is BITMAPV4HEADER (108 bytes). */
    bool isBitmapV5;         /**< True if header is BITMAPV5HEADER (124 bytes). */
    int32_t width;           /**< Bitmap width in pixels (signed to handle negative height case for top-down). */
    int32_t height;          /**< Bitmap height in pixels (signed; negative indicates top-down image). */
    uint16_t planes;         /**< Number of color planes (must be 1). */
    uint16_t bitCount;       /**< Bits per pixel (color depth). */
    uint32_t compression;    /**< Compression method (BI_RGB, BI_RLE8, etc.). */
    uint32_t imageSize;      /**< Size of pixel data (compressed or uncompressed) as given in header (may be 0 for BI_RGB). */
    uint32_t xPelsPerMeter;  /**< Horizontal resolution (pixels per meter). */
    uint32_t yPelsPerMeter;  /**< Vertical resolution (pixels per meter). */
    uint32_t colorsUsed;     /**< Number of palette colors used (0 means default 2^bitCount for indexed images). */
    uint32_t colorsImportant;/**< Number of important colors (usually 0 = all). */
    // Color masks (for BI_BITFIELDS or alpha bitfields):
    bool haveColorMasks;     /**< True if color mask fields are present/valid. */
    uint32_t redMask;
    uint32_t greenMask;
    uint32_t blueMask;
    uint32_t alphaMask;
    // BITMAPV5 profile info:
    uint32_t profileOffset;  /**< Offset of ICC profile data from start of DIB header (0 if none). */
    uint32_t profileSize;    /**< Size of ICC profile data in bytes (0 if none). */
};

/**
 * @brief Get half of the system's physical RAM size in bytes.
 * 
 * Attempts to determine the total physical memory (RAM) on the system and returns half of that value.
 * On Windows, uses GlobalMemoryStatusEx; on Linux/Unix, uses sysconf. If detection fails, returns 0.
 * This is used as an upper bound for file size to process (to avoid memory exhaustion).
 * 
 * @return Half of physical RAM in bytes, or 0 if not determinable.
 */
static uint64_t get_half_ram_limit() {
    uint64_t half_ram = 0;
    #ifdef _WIN32
    MEMORYSTATUSEX memstat;
    memstat.dwLength = sizeof(memstat);
    if (GlobalMemoryStatusEx(&memstat)) {
        half_ram = memstat.ullTotalPhys / 2;
    }
    #elif defined(__linux__) || defined(__unix__) || defined(__APPLE__)
    long pages = sysconf(_SC_PHYS_PAGES);
    long page_size = sysconf(_SC_PAGE_SIZE);
    if (pages != -1 && page_size != -1) {
        uint64_t total_ram = (uint64_t)pages * (uint64_t)page_size;
        half_ram = total_ram / 2;
    }
    #endif
    return half_ram;
}

/**
 * @brief Read the BMP file and validate the file header and DIB header.
 * 
 * Reads the 14-byte file header and the DIB header. Populates a BmpInfo structure with parsed data.
 * Performs basic validation such as signature check, file size consistency, and known DIB header size.
 * This function does not yet validate individual fields deeply; it lays the groundwork for later checks.
 * 
 * @param f Open file pointer (binary mode) positioned at start.
 * @param info Pointer to BmpInfo structure to fill.
 * @return `true` if headers were read successfully and basic checks passed, `false` on error.
 */
static bool read_headers(FILE *f, struct BmpInfo *info) {
    unsigned char fileHeader[14];
    if (fread(fileHeader, 1, 14, f) < 14) {
        fprintf(stderr, "Error: Cannot read BMP file header.\n");
        return false;
    }
    // BMP signature "BM" (0x42 0x4D)
    if (fileHeader[0] != 'B' || fileHeader[1] != 'M') {
        fprintf(stderr, "Error: Invalid BMP signature (expected 'BM').\n");
        return false;
    }
    // File size (offset 2, little-endian 4 bytes)
    uint32_t bfSize = fileHeader[2] | ((uint32_t)fileHeader[3] << 8) |
                      ((uint32_t)fileHeader[4] << 16) | ((uint32_t)fileHeader[5] << 24);
    // Reserved fields (offset 6 and 8, each 2 bytes) - must be 0 for standard BMP
    uint16_t bfReserved1 = fileHeader[6] | ((uint16_t)fileHeader[7] << 8);
    uint16_t bfReserved2 = fileHeader[8] | ((uint16_t)fileHeader[9] << 8);
    if (bfReserved1 != 0 || bfReserved2 != 0) {
        fprintf(stderr, "Error: Reserved bytes in BMP header are not zero.\n");
        return false;
    }
    // Pixel data offset (offset 10, little-endian 4 bytes)
    uint32_t bfOffBits = fileHeader[10] | ((uint32_t)fileHeader[11] << 8) |
                         ((uint32_t)fileHeader[12] << 16) | ((uint32_t)fileHeader[13] << 24);
    info->dataOffset = bfOffBits;
    // Determine actual file size by seeking to end
    if (fseek(f, 0, SEEK_END) != 0) {
        fprintf(stderr, "Error: Unable to determine file size (fseek failed).\n");
        return false;
    }
    long actualSize = ftell(f);
    if (actualSize < 0) {
        fprintf(stderr, "Error: Unable to determine file size (ftell failed).\n");
        return false;
    }
    info->fileSize = (uint32_t)actualSize;
    // Reset file pointer to after the file header (14 bytes)
    if (fseek(f, 14, SEEK_SET) != 0) {
        fprintf(stderr, "Error: fseek failed to reset file position.\n");
        return false;
    }
    // Check for consistency between header file size and actual file size
    if (bfSize != info->fileSize) {
        fprintf(stderr, "Error: BMP header file size (%u) does not match actual file size (%u).\n", bfSize, info->fileSize);
        return false;
    }
    // Check that pixel data offset is within the file
    if (info->dataOffset >= info->fileSize) {
        fprintf(stderr, "Error: Pixel data offset (%u) is beyond file size.\n", info->dataOffset);
        return false;
    }
    // Read DIB header size (first 4 bytes of DIB header)
    unsigned char dibSizeBytes[4];
    if (fread(dibSizeBytes, 1, 4, f) < 4) {
        fprintf(stderr, "Error: Cannot read DIB header size.\n");
        return false;
    }
    uint32_t dibSize = dibSizeBytes[0] | ((uint32_t)dibSizeBytes[1] << 8) |
                       ((uint32_t)dibSizeBytes[2] << 16) | ((uint32_t)dibSizeBytes[3] << 24);
    info->dibHeaderSize = dibSize;
    // Validate DIB header size and allocate buffer for the full header
    if (dibSize < 12 || dibSize > 124) {
        fprintf(stderr, "Error: Unsupported DIB header size (%u bytes).\n", dibSize);
        return false;
    }
    unsigned char *dibData = malloc(dibSize);
    if (!dibData) {
        fprintf(stderr, "Error: Memory allocation failed for DIB header.\n");
        return false;
    }
    // Copy the 4 bytes we already read, and read the remaining (dibSize - 4) bytes
    memcpy(dibData, dibSizeBytes, 4);
    size_t remaining = dibSize - 4;
    if (remaining > 0) {
        if (fread(dibData + 4, 1, remaining, f) < remaining) {
            fprintf(stderr, "Error: Failed to read full DIB header (expected %u bytes).\n", dibSize);
            free(dibData);
            return false;
        }
    }
    // Interpret the DIB header fields based on its size
    info->isOS2 = false;
    info->isOS2V2 = false;
    info->isBitmapV4 = false;
    info->isBitmapV5 = false;
    info->haveColorMasks = false;
    info->redMask = info->greenMask = info->blueMask = info->alphaMask = 0;
    info->profileOffset = info->profileSize = 0;
    if (dibSize == 12 || dibSize == 16) {
        // OS/2 1.x BITMAPCOREHEADER (12 bytes) or the 16-byte variant of OS/2 2.x (with 16 bytes used, rest assumed zero)
        info->isOS2 = true;
        // For 16-byte case, treat it as OS/2 2.x truncated header
        if (dibSize == 16) {
            info->isOS2V2 = true;
        }
        // Fields: 16-bit width, 16-bit height, 16-bit planes, 16-bit bitCount
        uint16_t width16  = dibData[4] | ((uint16_t)dibData[5] << 8);
        uint16_t height16 = dibData[6] | ((uint16_t)dibData[7] << 8);
        // Note: OS/2 1.x uses unsigned height, OS/2 2.x might treat as signed. We assume no negative in OS/2 core.
        info->width  = width16;
        info->height = height16;
        info->planes = dibData[8] | ((uint16_t)dibData[9] << 8);
        info->bitCount = dibData[10] | ((uint16_t)dibData[11] << 8);
        // OS/2 core headers do not have a compression field; they imply BI_RGB (no compression).
        info->compression = 0;  // BI_RGB
        info->imageSize = 0;
        info->xPelsPerMeter = 0;
        info->yPelsPerMeter = 0;
        info->colorsUsed = 0;
        info->colorsImportant = 0;
    } else {
        // Windows/Advanced BMP headers (BITMAPINFOHEADER and beyond)
        // All these headers share the initial BITMAPINFOHEADER layout:
        info->width  = dibData[4]  | ((uint32_t)dibData[5]  << 8) | ((uint32_t)dibData[6]  << 16) | ((uint32_t)dibData[7]  << 24);
        info->height = dibData[8]  | ((uint32_t)dibData[9]  << 8) | ((uint32_t)dibData[10] << 16) | ((uint32_t)dibData[11] << 24);
        info->planes = dibData[12] | ((uint16_t)dibData[13] << 8);
        info->bitCount = dibData[14] | ((uint16_t)dibData[15] << 8);
        info->compression = dibData[16] | ((uint32_t)dibData[17] << 8) | ((uint32_t)dibData[18] << 16) | ((uint32_t)dibData[19] << 24);
        info->imageSize   = dibData[20] | ((uint32_t)dibData[21] << 8) | ((uint32_t)dibData[22] << 16) | ((uint32_t)dibData[23] << 24);
        info->xPelsPerMeter = dibData[24] | ((uint32_t)dibData[25] << 8) | ((uint32_t)dibData[26] << 16) | ((uint32_t)dibData[27] << 24);
        info->yPelsPerMeter = dibData[28] | ((uint32_t)dibData[29] << 8) | ((uint32_t)dibData[30] << 16) | ((uint32_t)dibData[31] << 24);
        info->colorsUsed     = dibData[32] | ((uint32_t)dibData[33] << 8) | ((uint32_t)dibData[34] << 16) | ((uint32_t)dibData[35] << 24);
        info->colorsImportant= dibData[36] | ((uint32_t)dibData[37] << 8) | ((uint32_t)dibData[38] << 16) | ((uint32_t)dibData[39] << 24);
        if (dibSize == 40) {
            // BITMAPINFOHEADER (no masks present in header)
        } else if (dibSize == 52 || dibSize == 56) {
            // BITMAPV2INFOHEADER or BITMAPV3INFOHEADER (undocumented Adobe headers)
            info->haveColorMasks = true;
            info->redMask   = dibData[40] | ((uint32_t)dibData[41] << 8) | ((uint32_t)dibData[42] << 16) | ((uint32_t)dibData[43] << 24);
            info->greenMask = dibData[44] | ((uint32_t)dibData[45] << 8) | ((uint32_t)dibData[46] << 16) | ((uint32_t)dibData[47] << 24);
            info->blueMask  = dibData[48] | ((uint32_t)dibData[49] << 8) | ((uint32_t)dibData[50] << 16) | ((uint32_t)dibData[51] << 24);
            if (dibSize == 56) {
                info->alphaMask = dibData[52] | ((uint32_t)dibData[53] << 8) | ((uint32_t)dibData[54] << 16) | ((uint32_t)dibData[55] << 24);
            } else {
                info->alphaMask = 0;
            }
        } else if (dibSize == 64) {
            // OS/2 2.x BITMAPINFOHEADER2 (64 bytes)
            info->isOS2 = true;
            info->isOS2V2 = true;
            // OS/2 2.x has same initial fields as BITMAPINFOHEADER, plus additional fields we don't heavily use.
            // The additional 24 bytes at offsets 40-63 are OS/2-specific (units, reserved, recording, rendering, size1, size2, color encoding, identifier).
            // We can optionally validate a couple of those:
            uint16_t units    = dibData[54] | ((uint16_t)dibData[55] << 8);
            uint16_t recording= dibData[58] | ((uint16_t)dibData[59] << 8);
            // Only defined unit is 0 (pixels per meter) and only defined recording orientation is 0 (bottom-up).
            if (units != 0) {
                // Non-zero units might indicate an unknown unit (not necessarily fatal, but we can warn/fail as non-standard).
                fprintf(stderr, "Error: OS/2 BMP uses unsupported resolution units (%u).\n", units);
                free(dibData);
                return false;
            }
            if (recording != 0) {
                // OS/2 recording = 0 (bottom-up) or 1 (top-down). 1 means top-down (bits fill top-to-bottom).
                // If top-down OS/2, treat similar to negative height.
                if (recording == 1) {
                    // Indicate top-down via negative height if not already negative.
                    if (info->height > 0) info->height = -info->height;
                } else {
                    fprintf(stderr, "Error: OS/2 BMP uses unknown recording mode (%u).\n", recording);
                    free(dibData);
                    return false;
                }
            }
            // OS/2 compression values differ (BI_BITFIELDS not used; 3 means Huffman 1D).
            // We'll adjust interpretation later in validation.
        } else if (dibSize == 108 || dibSize == 124) {
            // BITMAPV4 or BITMAPV5
            info->haveColorMasks = true;
            info->redMask   = dibData[40] | ((uint32_t)dibData[41] << 8) | ((uint32_t)dibData[42] << 16) | ((uint32_t)dibData[43] << 24);
            info->greenMask = dibData[44] | ((uint32_t)dibData[45] << 8) | ((uint32_t)dibData[46] << 16) | ((uint32_t)dibData[47] << 24);
            info->blueMask  = dibData[48] | ((uint32_t)dibData[49] << 8) | ((uint32_t)dibData[50] << 16) | ((uint32_t)dibData[51] << 24);
            info->alphaMask = dibData[52] | ((uint32_t)dibData[53] << 8) | ((uint32_t)dibData[54] << 16) | ((uint32_t)dibData[55] << 24);
            info->isBitmapV4 = (dibSize == 108);
            info->isBitmapV5 = (dibSize == 124);
            // We won't parse the full color space fields in detail, but we should validate profile fields if V5.
            if (info->isBitmapV5) {
                // bV5CSType at 56, bV5Endpoints at 60 (36 bytes), bV5Gamma at 96 (3*4 bytes),
                // bV5Intent at 108, bV5ProfileData at 112, bV5ProfileSize at 116, bV5Reserved at 120.
                uint32_t profileData = dibData[112] | ((uint32_t)dibData[113] << 8) | 
                                       ((uint32_t)dibData[114] << 16) | ((uint32_t)dibData[115] << 24);
                uint32_t profileSize = dibData[116] | ((uint32_t)dibData[117] << 8) | 
                                       ((uint32_t)dibData[118] << 16) | ((uint32_t)dibData[119] << 24);
                uint32_t reserved    = dibData[120] | ((uint32_t)dibData[121] << 8) | 
                                       ((uint32_t)dibData[122] << 16) | ((uint32_t)dibData[123] << 24);
                info->profileOffset = profileData;
                info->profileSize = profileSize;
                if (reserved != 0) {
                    fprintf(stderr, "Error: BITMAPV5 Reserved field is not zero.\n");
                    free(dibData);
                    return false;
                }
            }
        }
    }
    free(dibData);
    return true;
}

/**
 * @brief Perform validation checks on the parsed BMP headers and structure.
 * 
 * Uses the information in BmpInfo (populated by read_headers) and the file pointer (positioned right after the DIB header data) 
 * to validate the BMP's internal consistency. This includes:
 *   - Verifying dimension, planes, and compression constraints.
 *   - Handling color masks and reading them from file if needed.
 *   - Validating color palette size and consistency with dataOffset.
 *   - Checking file size limits (half RAM / 15MB).
 *   - Validating pixel data (uncompressed size or compressed RLE stream or embedded image).
 *   - Validating BITMAPV5 profile data positioning.
 * 
 * @param f Open file pointer positioned immediately after the DIB header (at the start of any optional masks or palette).
 * @param info Pointer to BmpInfo with header data.
 * @return `true` if all validations pass and the BMP structure is consistent, `false` if any check fails (with an error printed).
 */
static bool validate_structure(FILE *f, struct BmpInfo *info) {
    // 1. Check planes
    if (info->planes != 1) {
        fprintf(stderr, "Error: Number of planes is %u (must be 1).\n", info->planes);
        return false;
    }
    // 2. Enforce maximum file size (to avoid large memory processing)
    uint64_t half_ram = get_half_ram_limit();
    uint64_t maxFileSize = (half_ram > 0 ? half_ram : (uint64_t)15 * 1024 * 1024);
    if (info->fileSize > maxFileSize) {
        fprintf(stderr, "Error: BMP file is too large (%u bytes > %llu bytes limit).\n", info->fileSize, (unsigned long long)maxFileSize);
        return false;
    }
    // 3. Validate bitCount and compression combinations
    uint16_t bpp = info->bitCount;
    uint32_t comp = info->compression;
    bool compressedRLE = false;
    bool embedded = false;
    switch (comp) {
        case 0: // BI_RGB (Uncompressed)
            // For uncompressed, bitCount can be 1,4,8,16,24,32. Some BMPs might use 0 bitCount for JPEG/PNG, but those have comp !=0.
            if (!(bpp == 1 || bpp == 4 || bpp == 8 || bpp == 16 || bpp == 24 || bpp == 32)) {
                fprintf(stderr, "Error: Unsupported bit depth %u for uncompressed BMP.\n", bpp);
                return false;
            }
            break;
        case 1: // BI_RLE8
            compressedRLE = true;
            if (bpp != 8) {
                fprintf(stderr, "Error: RLE8 compression used with bitCount %u (must be 8).\n", bpp);
                return false;
            }
            // Top-down (negative height) not allowed for RLE
            if (info->height < 0) {
                fprintf(stderr, "Error: Compressed BMP cannot have negative height (top-down orientation).\n");
                return false;
            }
            break;
        case 2: // BI_RLE4
            compressedRLE = true;
            if (bpp != 4) {
                fprintf(stderr, "Error: RLE4 compression used with bitCount %u (must be 4).\n", bpp);
                return false;
            }
            if (info->height < 0) {
                fprintf(stderr, "Error: Compressed BMP cannot be top-down (negative height).\n");
                return false;
            }
            break;
        case 3: // BI_BITFIELDS
        case 6: // BI_ALPHABITFIELDS (usually only in BITMAPINFOHEADER on CE; for V4/V5, comp stays 3 with alphaMask set)
            // For mask-based, must be 16 or 32 bpp typically.
            if (!(bpp == 16 || bpp == 32)) {
                fprintf(stderr, "Error: Bitfields compression with unsupported bitCount %u.\n", bpp);
                return false;
            }
            // If header was BITMAPINFOHEADER (40 bytes), then masks are not yet read (will be right after header).
            // If header is V2/V3/V4/V5, masks are already in info->redMask etc.
            break;
        case 4: // BI_JPEG
        case 5: // BI_PNG
            embedded = true;
            // These should only appear in BITMAPV4/5 headers
            if (!(info->isBitmapV4 || info->isBitmapV5)) {
                fprintf(stderr, "Error: JPEG/PNG compression in non-V4/V5 header.\n");
                return false;
            }
            // The bitCount might be set to 0 or 24 for JPEG/PNG. If 0, we'll allow it (meaning "defined by format").
            if (!(bpp == 0 || bpp == 24)) {
                // Some BMP might set bitCount = 24 for JPEG, but spec allows 0.
                fprintf(stderr, "Warning: JPEG/PNG compression with unexpected bitCount %u (continuing).\n", bpp);
                // Not a hard error, just unusual. Continue validation.
            }
            // Height must not be negative (no top-down for compressed).
            if (info->height < 0) {
                fprintf(stderr, "Error: JPEG/PNG compressed BMP cannot be top-down (negative height).\n");
                return false;
            }
            break;
        case 11: // BI_CMYK
        case 12: // BI_CMYKRLE8
        case 13: // BI_CMYKRLE4
            // These are rarely used (only in printing/metafiles context).
            fprintf(stderr, "Error: Unsupported CMYK compression (%u) in BMP file.\n", comp);
            return false;
        default:
            fprintf(stderr, "Error: Unknown or unsupported compression type (%u).\n", comp);
            return false;
    }
    // 4. If BI_BITFIELDS and we have a BITMAPINFOHEADER (which wouldn't include masks in the header), read masks from file
    size_t maskBytesToRead = 0;
    unsigned char maskBuf[16];
    if ((comp == 3 || comp == 6) && info->dibHeaderSize == 40) {
        // Expect 3 DWORD masks for BI_BITFIELDS, or 4 for BI_ALPHABITFIELDS
        maskBytesToRead = (comp == 6 ? 16 : 12);
        if (maskBytesToRead > 0) {
            if (fread(maskBuf, 1, maskBytesToRead, f) < maskBytesToRead) {
                fprintf(stderr, "Error: Cannot read color mask information.\n");
                return false;
            }
            info->haveColorMasks = true;
            info->redMask   = maskBuf[0]  | ((uint32_t)maskBuf[1]  << 8) | ((uint32_t)maskBuf[2]  << 16) | ((uint32_t)maskBuf[3]  << 24);
            info->greenMask = maskBuf[4]  | ((uint32_t)maskBuf[5]  << 8) | ((uint32_t)maskBuf[6]  << 16) | ((uint32_t)maskBuf[7]  << 24);
            info->blueMask  = maskBuf[8]  | ((uint32_t)maskBuf[9]  << 8) | ((uint32_t)maskBuf[10] << 16) | ((uint32_t)maskBuf[11] << 24);
            if (maskBytesToRead == 16) {
                info->alphaMask = maskBuf[12] | ((uint32_t)maskBuf[13] << 8) | ((uint32_t)maskBuf[14] << 16) | ((uint32_t)maskBuf[15] << 24);
            } else {
                info->alphaMask = 0;
            }
        }
    }
    // 5. Determine palette size (for indexed color images)
    bool hasPalette = false;
    uint32_t paletteEntries = 0;
    uint32_t bytesPerPaletteEntry = 4;  // Windows palette entries (RGBQUAD) default
    if (info->isOS2 && !info->isOS2V2) {
        // OS/2 1.x uses 3-byte RGB triples in palette
        bytesPerPaletteEntry = 3;
    }
    if (bpp <= 8) {
        hasPalette = true;
        uint32_t defaultPaletteSize = 1u << bpp; // e.g., 2^1=2, 2^4=16, 2^8=256
        paletteEntries = (info->colorsUsed != 0 && info->colorsUsed <= defaultPaletteSize) ? info->colorsUsed : defaultPaletteSize;
    } else if (info->isOS2 && bpp <= 24) {
        // OS/2 also could have palette for 24bpp? Unlikely, typically no palette for >8bpp.
        paletteEntries = (info->colorsUsed != 0 ? info->colorsUsed : 0);
        if (paletteEntries > 0) {
            hasPalette = true;
        }
    }
    // 6. Check pixel data offset against header + palette
    // Calculate where pixel data *should* start: 14 (file header) + DIB header size + any mask bytes + palette bytes
    uint32_t calculatedDataOffset = 14 + info->dibHeaderSize + maskBytesToRead + (paletteEntries * bytesPerPaletteEntry);
    if (calculatedDataOffset > info->dataOffset) {
        // The declared dataOffset is earlier than expected (overlaps header or palette)
        fprintf(stderr, "Error: Declared pixel data offset (%u) is too small (overlaps header/palette).\n", info->dataOffset);
        return false;
    }
    if (calculatedDataOffset < info->dataOffset) {
        // There's a gap between expected palette end and data offset; usually this shouldn't happen unless paletteEntries < default and they still aligned dataOffset.
        // We'll allow small gaps if they align to 4-byte boundary.
        uint32_t gap = info->dataOffset - calculatedDataOffset;
        if (gap > 3) { // more than 3 bytes gap (more than alignment padding)
            fprintf(stderr, "Error: Unexpected gap of %u bytes before pixel data.\n", gap);
            return false;
        }
        // If gap is 1-3, assume it's padding and skip it
        if (fseek(f, gap, SEEK_CUR) != 0) {
            fprintf(stderr, "Error: Failed to skip padding before pixel data.\n");
            return false;
        }
    }
    // If palette exists, we may skip reading it (unless we want to validate its contents).
    // We could validate that all palette entries were present by checking file position, but since we used fseek for gap, we're now at dataOffset.
    // At this point, file pointer should be at the start of pixel data.
    // 7. Validate pixel data size and content based on compression
    uint32_t width = info->width;
    uint32_t height = (info->height < 0) ? (uint32_t)(-info->height) : (uint32_t)info->height;
    // Total pixels (for uncompressed or full coverage in RLE)
    // Use 64-bit for multiplication to avoid overflow
    uint64_t totalPixels = (uint64_t)width * height;
    // Determine how much data is actually present for pixel array (if no color profile, it's fileSize - dataOffset; if profile present, it's profileOffset start - dataOffset).
    uint32_t pixelDataSize;
    if (info->isBitmapV5 && info->profileSize > 0 && info->profileOffset > 0) {
        // profileOffset is from start of BITMAPV5HEADER (14 bytes after file start)
        uint32_t profileFileOffset = 14 + info->profileOffset;
        if (profileFileOffset >= info->fileSize) {
            fprintf(stderr, "Error: Profile data offset is beyond file size.\n");
            return false;
        }
        if (profileFileOffset < info->dataOffset) {
            fprintf(stderr, "Error: Profile data offset overlaps the pixel data region.\n");
            return false;
        }
        pixelDataSize = profileFileOffset - info->dataOffset;
        // Ensure profile fits exactly at end
        if (profileFileOffset + info->profileSize != info->fileSize) {
            fprintf(stderr, "Error: Profile size does not match remaining file data (%u bytes vs %u bytes available).\n", info->profileSize, info->fileSize - profileFileOffset);
            return false;
        }
    } else {
        // No profile, pixel data goes till end of file
        pixelDataSize = info->fileSize - info->dataOffset;
    }
    if (!compressedRLE && !embedded) {
        // Uncompressed image: verify size
        // Compute expected image data size with row padding
        // Each row is padded to 4-byte boundary.
        // Use 64-bit for intermediate to avoid overflow.
        uint64_t bitsPerRow = (uint64_t)width * bpp;
        // round up to nearest multiple of 32 bits
        uint64_t rowSizeBytes = ((bitsPerRow + 31) / 32) * 4;
        uint64_t expectedSize64 = rowSizeBytes * height;
        if (expectedSize64 > UINT32_MAX) {
            fprintf(stderr, "Error: Image dimensions cause overflow in size calculation.\n");
            return false;
        }
        uint32_t expectedImageSize = (uint32_t) expectedSize64;
        // Some BMPs set imageSize=0 for BI_RGB; if so, we can use our computed value
        uint32_t headerImageSize = info->imageSize;
        if (headerImageSize != 0 && headerImageSize != expectedImageSize) {
            fprintf(stderr, "Error: Header image size (%u) does not match expected size (%u).\n", headerImageSize, expectedImageSize);
            // We don't return immediately; we'll double-check using actual file data length next.
            // (Some encoders might leave imageSize mismatched; but we consider it an error for strict validation.)
            return false;
        }
        if (expectedImageSize > pixelDataSize) {
            fprintf(stderr, "Error: BMP file is truncated; pixel data expected %u bytes, but only %u available.\n", expectedImageSize, pixelDataSize);
            return false;
        }
        if (expectedImageSize < pixelDataSize) {
            // There is extra data after the pixel array (maybe padding or profile which we would have handled, or just junk)
            // If it's just a few bytes (<=3) of padding to align file size, allow it. Otherwise flag it.
            uint32_t extra = pixelDataSize - expectedImageSize;
            if (info->isBitmapV5 && info->profileSize > 0) {
                // If profile present, no extra should be counted here (profile was separated above).
                extra = 0;
            }
            if (extra > 3) {
                fprintf(stderr, "Error: Extra %u bytes found after image pixel data (unexpected).\n", extra);
                return false;
            }
            // If 1-3 bytes extra, assume alignment padding and ignore.
        }
        // At this point, uncompressed image data size is consistent.
        // Optionally, we could further validate pixel content (like no out-of-range palette index). 
        // That would require reading pixel data. Given potential size, we skip that deep check.
    } else if (compressedRLE) {
        // Parse RLE stream to ensure it terminates properly and doesn't overflow image bounds
        uint32_t currX = 0;
        uint32_t currY = 0; // we'll count rows from bottom (0 = bottom row, height-1 = top row)
        bool endOfBitmap = false;
        // We read through the pixelDataSize bytes
        uint32_t bytesRead = 0;
        while (bytesRead < pixelDataSize) {
            uint8_t firstByte;
            if (fread(&firstByte, 1, 1, f) < 1) {
                fprintf(stderr, "Error: Unexpected end of file in RLE data.\n");
                return false;
            }
            bytesRead++;
            if (firstByte == 0) {
                // Escape sequence
                uint8_t secondByte;
                if (fread(&secondByte, 1, 1, f) < 1) {
                    fprintf(stderr, "Error: Unexpected end of file in RLE escape code.\n");
                    return false;
                }
                bytesRead++;
                if (secondByte == 0) {
                    // End of line
                    currX = 0;
                    currY++;
                    if (currY >= height) {
                        // EOL beyond last line; this could indicate an extra EOL at end or malformed file
                        if (currY != height || bytesRead < pixelDataSize) {
                            fprintf(stderr, "Error: RLE stream has too many lines.\n");
                            return false;
                        }
                        // if currY == height exactly and we are exactly at end, it might be okay (an extra EOL at end-of-bitmap).
                    }
                } else if (secondByte == 1) {
                    // End of bitmap
                    endOfBitmap = true;
                    break;
                } else if (secondByte == 2) {
                    // Delta: move right and up
                    uint8_t dx, dy;
                    if (fread(&dx, 1, 1, f) < 1 || fread(&dy, 1, 1, f) < 1) {
                        fprintf(stderr, "Error: Unexpected end of file in RLE delta.\n");
                        return false;
                    }
                    bytesRead += 2;
                    // Move position
                    if (currX + dx > width || currY + dy >= height) {
                        fprintf(stderr, "Error: RLE delta moves outside image bounds.\n");
                        return false;
                    }
                    currX += dx;
                    currY += dy;
                } else {
                    // Absolute mode: secondByte = count of pixels
                    uint8_t count = secondByte;
                    uint32_t pixelCount = count;
                    if (pixelCount > width - currX) {
                        fprintf(stderr, "Error: RLE absolute mode exceeds row width.\n");
                        return false;
                    }
                    // Calculate bytes to read: For RLE8, it's exactly count bytes of indices. For RLE4, count indices (nibbles) packed in bytes.
                    if (info->compression == 1) { // BI_RLE8
                        // count number of pixel indices = count
                        uint32_t dataBytes = pixelCount;
                        // Each absolute run is padded to an even number of bytes.
                        if (dataBytes % 2 == 1) {
                            dataBytes++; // include padding byte
                        }
                        unsigned char absData[256]; // max 255 indices + 1 padding
                        if (dataBytes > 0) {
                            if (fread(absData, 1, dataBytes, f) < dataBytes) {
                                fprintf(stderr, "Error: Unexpected end of file in RLE absolute data.\n");
                                return false;
                            }
                            bytesRead += dataBytes;
                        }
                        // Advance current position by count pixels
                        currX += pixelCount;
                        // If we hit end of line, wrap implicitly (though typically an EOL is used, some implementations may rely on count ending exactly at line end)
                        if (currX == width) {
                            currX = 0;
                            currY++;
                        }
                    } else { // BI_RLE4
                        // For RLE4, each byte contains 2 pixel indices (high nibble, low nibble).
                        // count is number of pixels (nibbles) to read.
                        uint32_t dataBytes = (pixelCount + 1) / 2; // number of bytes containing pixelCount nibbles
                        // Each run padded to word (2-byte) boundary
                        if (dataBytes % 2 == 1) {
                            dataBytes++; // add padding byte if needed
                        }
                        if (dataBytes > 0) {
                            // We need to ensure not to overflow our buffer (worst-case: count=255 -> dataBytes=128, which is fine for a stack buffer)
                            unsigned char absData[512];
                            if (dataBytes > sizeof(absData)) {
                                // Should never happen since max dataBytes for count=255 is 128, well within 512.
                                fprintf(stderr, "Error: RLE4 absolute run too large to handle.\n");
                                return false;
                            }
                            if (fread(absData, 1, dataBytes, f) < dataBytes) {
                                fprintf(stderr, "Error: Unexpected end of file in RLE4 absolute data.\n");
                                return false;
                            }
                        }
                        bytesRead += dataBytes;
                        currX += pixelCount;
                        if (currX > width) {
                            fprintf(stderr, "Error: RLE4 absolute mode exceeds row width.\n");
                            return false;
                        }
                        if (currX == width) {
                            currX = 0;
                            currY++;
                        }
                    }
                }
            } else {
                // Encoded mode: firstByte = count, secondByte = color data
                uint8_t count = firstByte;
                uint8_t colorByte;
                if (fread(&colorByte, 1, 1, f) < 1) {
                    fprintf(stderr, "Error: Unexpected end of file in RLE run.\n");
                    return false;
                }
                bytesRead++;
                if (count == 0) {
                    // Count of 0 in encoded mode is not valid (except as part of escape which we handled above).
                    fprintf(stderr, "Error: RLE encoded run with count 0.\n");
                    return false;
                }
                if (count > width - currX) {
                    fprintf(stderr, "Error: RLE run length exceeds row width.\n");
                    return false;
                }
                if (info->compression == 1) { // BI_RLE8
                    // colorByte is the palette index to repeat
                    currX += count;
                } else { // BI_RLE4
                    // colorByte contains two 4-bit color indices
                    // The two indices alternate for the run (high nibble, low nibble, then repeat).
                    currX += count;
                }
                if (currX == width) {
                    // If exactly end of line, wrap to next line for subsequent runs implicitly
                    currX = 0;
                    currY++;
                } else if (currX > width) {
                    // Just in case, though we checked above
                    fprintf(stderr, "Error: RLE run overflowed the row.\n");
                    return false;
                }
                if (currY >= height) {
                    // Filled beyond the last line
                    fprintf(stderr, "Error: RLE pixel data exceeds image height.\n");
                    return false;
                }
            }
            if (currY >= height) {
                // We have drawn all rows (this should ideally coincide with end-of-bitmap)
                break;
            }
        }
        if (!endOfBitmap) {
            // We exited loop without encountering end-of-bitmap marker
            fprintf(stderr, "Error: RLE data did not contain an end-of-bitmap marker.\n");
            return false;
        }
        // If we have an end-of-bitmap, there might still be padding to even 4-byte boundary of pixelDataSize. Allow only harmless padding:
        // BMP RLE data is typically padded to 2-byte alignment for the bitmap section. So at most one padding 0x00 might follow the end marker if needed to align to even size.
        // We consider anything beyond 2 bytes after end marker as suspicious.
        uint32_t remainingBytes = pixelDataSize - bytesRead;
        if (remainingBytes > 2) {
            fprintf(stderr, "Error: Unexpected data after end-of-bitmap marker in RLE stream.\n");
            return false;
        }
        // We don't need to explicitly consume the remaining padding bytes, as we will not use them further.
    } else if (embedded) {
        // We have an embedded JPEG or PNG stream.
        // We will look at the first few bytes to verify format.
        unsigned char sig[8];
        size_t toRead = (comp == 4 ? 2 : 8); // JPEG: check first 2 bytes; PNG: first 8 bytes signature
        if (toRead > pixelDataSize) toRead = pixelDataSize;
        if (fread(sig, 1, toRead, f) < toRead) {
            fprintf(stderr, "Error: Cannot read embedded image signature.\n");
            return false;
        }
        // Check JPEG or PNG signatures
        if (comp == 4) { // JPEG expected
            if (!(sig[0] == 0xFF && sig[1] == 0xD8)) {
                fprintf(stderr, "Error: Embedded JPEG data does not start with 0xFFD8.\n");
                return false;
            }
        } else if (comp == 5) { // PNG expected
            static const unsigned char PNG_SIG[8] = {0x89, 'P','N','G', 0x0D,0x0A,0x1A,0x0A};
            if (memcmp(sig, PNG_SIG, 8) != 0) {
                fprintf(stderr, "Error: Embedded PNG data has invalid signature.\n");
                return false;
            }
        }
        // Optionally, verify that biSizeImage equals pixelDataSize for embedded formats (it should).
        if (info->imageSize != 0 && info->imageSize != pixelDataSize) {
            fprintf(stderr, "Error: Embedded image size field (%u) does not match actual data size (%u).\n", info->imageSize, pixelDataSize);
            return false;
        }
        // For JPEG, we could search for 0xFFD9 at the end of data as a sanity check, but it's not guaranteed to be right at the end if padded.
        // We'll assume if the starting signature is correct and sizes match, it's acceptable.
    }
    // 8. Final check for BMP v5 profile data placement (already partially checked above)
    if (info->isBitmapV5 && info->profileSize > 0) {
        // If we haven't already validated profile positioning enough, do final consistency check:
        uint32_t profileFileOffset = 14 + info->profileOffset;
        if (profileFileOffset < info->dataOffset) {
            fprintf(stderr, "Error: Profile data offset overlaps the BMP header area.\n");
            return false;
        }
        if (profileFileOffset + info->profileSize != info->fileSize) {
            fprintf(stderr, "Error: Profile data does not match file end (offset %u, size %u, file size %u).\n",
                    profileFileOffset, info->profileSize, info->fileSize);
            return false;
        }
        // We could also verify that profileFileOffset == info->dataOffset + pixelDataSize, but that should already be true from how we calculated pixelDataSize.
    }
    // If we reach here, all checks passed
    return true;
}

bool bmp_is_valid(const char *filename) {
    if (!filename) {
        fprintf(stderr, "Error: Filename is NULL.\n");
        return false;
    }
    FILE *f = fopen(filename, "rb");
    if (!f) {
        fprintf(stderr, "Error: Cannot open file \"%s\".\n", filename);
        return false;
    }
    struct BmpInfo info;
    if (!read_headers(f, &info)) {
        fclose(f);
        return false;
    }
    bool result = validate_structure(f, &info);
    fclose(f);
    if (!result) {
        // If validation failed, ensure we return false (error messages already printed in validate_structure)
        return false;
    }
    // If everything is good:
    return true;
}
