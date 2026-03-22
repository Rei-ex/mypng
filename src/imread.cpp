#include "imread.hpp"
#include <cstdint>
#include <cstring>
#include <emmintrin.h>
#include <fstream>
#include <immintrin.h>
#include <string>
#include <zlib-ng.h>



// static
// functions
static inline uint32_t readuint32BigEdian(void *data) noexcept; // read the first 4 byte from data as big edian and return an uint32_t

static inline int get_channels(int color_type, int bit_depth) noexcept; // a switch to pick the image channels from color type and bit depth
                                                                        // provides full check and return 0 if something is invalid

static inline std::ifstream fileInit2(const std::string path, uint64_t *file_size, uint8_t worker[33], Image *result) noexcept; // tuned, initialize by open the file, reading the IHDR, etc

static inline void nonct3bd8decoder(Image *result, std::ifstream &file, uint64_t file_size) noexcept; // a decoder specified for bit depth 8
                                                                                                      // (optional but a little better performance)

static inline bool defilter(uint8_t *buffer); // defilter engine, using scanline[0] as the up scanline and scanline[1] as the result scanline each time

static inline void cvt8bit(Image *result) noexcept; // convert the defiltered data to 8 bit from scanline[1]
                                                    // accept all type of bit depth
                                                    // have a hidden argument cvt3flag

static inline void cvt16bit(Image *result) noexcept; // convert the defiltered data to 16 bit mod65536, from scanline[1]
                                                     // if use pls change the result buffer into uint16_t
                                                     // this function already cover the conversion from big edian to little edian
                                                     // if use for color type 3, require a different way to handle its result

static inline void cvt8bit3(Image *result) noexcept; // cvt8bit for color type 3, assume channels MUST BE 4 by design

static inline void decodeEngine(Image *result, std::ifstream &file, uint64_t file_size, uint8_t color_type, void (*cvtbit)(Image *)) noexcept; // as its name

static inline uint64_t abs__(uint64_t x) noexcept; // branchless absolute value
static inline uint64_t ceil__(double x) noexcept;  // branchless ceil






// static
// constants
static constexpr unsigned char pngsig[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
static constexpr unsigned char ihdrsig[4] = {'I', 'H', 'D', 'R'};
static constexpr unsigned char IDATsig[4] = {'I', 'D', 'A', 'T'};
static constexpr unsigned char IENDsig[4] = {'I', 'E', 'N', 'D'};
static constexpr unsigned char PLTEsig[4] = {'P', 'L', 'T', 'E'};
static constexpr unsigned char tRNSsig[4] = {'t', 'R', 'N', 'S'};






// static
// variables
static bool cvt8bitflag;
static uint64_t bpp;
static uint64_t bpr;
static uint64_t imtrker;
static uint8_t *scanline[2]; // defilter use scanline[0] as the "up scanline"
                             // defilter return the result to scanline[1]
                             // cvt8bit use scanline[1] and return to result.buffer
// scanline only hold the pointer to the object, IT DOES NOT OWN the object








/*
flag = 0, will convert data into 8 bit + upscale ( or downscale for 16bit data )

flag = 1, will convert data into 16 bit + upscale ( does not support color type 3 and will return 8 bit data only )

flag = else, for bit depth 1/2/4 it will convert to 8 bit for data-readability and no conversion for bit depth 8/16

the conversion happens because there's no uint4_t or uint1_t for you to read 4 bit or 1 bit data

NOTICE: if bit depth == 16, reinterpret_cast<uint16_t> the buffer
*/



// imread
Image png_imread(const std::string path) noexcept {
    const int flag = -1;
    Image result;
    memset(reinterpret_cast<void *>(&result), 0, sizeof(result));
    imtrker = 0;
    uint8_t worker[33];
    uint64_t file_size = 0;

    std::ifstream file = fileInit2(path, &file_size, worker, &result);
    if (result.state != 0) { return result; }

    const uint32_t width = result.width = readuint32BigEdian(worker + 16);
    const uint32_t height = result.height = readuint32BigEdian(worker + 20);
    const int bit_depth = result.bit_depth = static_cast<int>(worker[24]);
    const int color_type = static_cast<int>(worker[25]);
    const int channels = result.channels = get_channels(color_type, bit_depth);
    const int compression_method = worker[26];
    const int filter_method = worker[27];
    const int interlace_method = worker[28];

    if ((compression_method | filter_method | interlace_method) != 0 | channels == 0) {
        result.state = 2;
        return result;
    }


    if (color_type == 3) {
        bpp = 1;
        bpr = ceil__(((double)width * (double)bit_depth) / (double)8);

        result.buffer.resize(height * width * channels);

        decodeEngine(&result, file, file_size, color_type, cvt8bit3);
        result.bit_depth = 8;
        return result;
    }

    bpp = channels * (bit_depth == 16 ? 2 : 1);
    bpr = ceil__(((double)width * (double)channels * (double)bit_depth) / (double)8);

    switch (flag) {
    case 0: {
        result.buffer.resize(height * width * channels);
        switch (bit_depth) {
        case 8: {
            nonct3bd8decoder(&result, file, file_size);
            return result;
        }
        default: {
            cvt8bitflag = true;
            decodeEngine(&result, file, file_size, color_type, cvt8bit);
            result.bit_depth = 8;
            return result;
        }
        }
    }
    case 1: {
        result.buffer.resize(height * width * channels * 2);
        decodeEngine(&result, file, file_size, color_type, cvt16bit);
        result.bit_depth = 16;
        return result;
    }
    default: {
        switch (bit_depth) {
        case 8: {
            result.buffer.resize(height * width * channels);
            nonct3bd8decoder(&result, file, file_size);
            return result;
        }
        case 16: {
            result.buffer.resize(height * width * channels * 2);
            decodeEngine(&result, file, file_size, color_type, cvt16bit);
            return result;
        }
        default: {
            result.buffer.resize(height * width * channels);
            cvt8bitflag = false;
            decodeEngine(&result, file, file_size, color_type, cvt8bit);
            return result;
        }
        }
    }
    }
}







// static functions definition
static inline uint32_t readuint32BigEdian(void *data) noexcept {
    uint32_t value = 0;
    uint8_t temp[4]{};
    uint8_t *d = reinterpret_cast<uint8_t *>(data) + 3;
    for (int i = 0; i < 4; i++) { temp[i] = *d--; }
    value = *(reinterpret_cast<uint32_t *>(temp));
    return value;
}

static inline int get_channels(int color_type, int bit_depth) noexcept {
    int channels;
    switch (color_type) {
    case 0: {
        channels = 1;
        switch (bit_depth) {
        case 1: break;
        case 2: break;
        case 4: break;
        case 8: break;
        case 16: break;
        default: return 0;
        }
        break;
    }
    case 2: {
        channels = 3;
        switch (bit_depth) {
        case 8: break;
        case 16: break;
        default: return 0;
        }
        break;
    }
    case 3: {
        channels = 4;
        switch (bit_depth) {
        case 1: break;
        case 2: break;
        case 4: break;
        case 8: break;
        default: return 0;
        }
        break;
    }
    case 4: {
        channels = 2;
        switch (bit_depth) {
        case 8: break;
        case 16: break;
        default: return 0;
        }
        break;
    }
    case 6: {
        channels = 4;
        switch (bit_depth) {
        case 8: break;
        case 16: break;
        default: return 0;
        }
        break;
    }
    default: return 0;
    }
    return channels;
}

static inline std::ifstream fileInit2(const std::string path, uint64_t *file_size, uint8_t worker[33], Image *result) noexcept {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        result->state = 1;
        return file;
    }
    *file_size = file.tellg();
    file.seekg(0, std::ios::beg);
    if (!file.read(reinterpret_cast<char *>(worker), 33) || *file_size < 33 + 12) {
        result->state = 1;
        return file;
    }

    uint32_t crc_ = zng_crc32(0U, Z_NULL, 0);
    crc_ = zng_crc32(crc_, worker + 12, 4);
    crc_ = zng_crc32(crc_, worker + 16, readuint32BigEdian(worker + 8));

    if (memcmp(worker, pngsig, 8) != 0 | memcmp(worker + 12, ihdrsig, 4) != 0 | readuint32BigEdian(worker + 29) != crc_) {
        result->state = 2;
        return file;
    }

    return file;
}

static inline bool defilter(uint8_t *buffer) {
    uint8_t filter = *buffer++;
    switch (filter) {
    case 0: {
        memcpy(scanline[1], buffer, bpr);
        break;
    }
    case 1: {
        memcpy(scanline[1], buffer, bpp);
        uint8_t *dest = scanline[1] + bpp;
        uint8_t *a = dest - bpp;
        buffer += bpp;
        for (uint64_t i = bpp; i < bpr; i++) { *dest++ = uint8_t(*buffer++ + *a++); }
        break;
    }
    case 2: {
        if (imtrker == 0) [[unlikely]] {
            memcpy(scanline[1], buffer, bpr);
        } else [[likely]] {
            uint8_t *dest = scanline[1];
            uint8_t *b = scanline[0];
            uint64_t i = 0;
            for (; i + 32 <= bpr; i += 32) {
                __m256i raw = _mm256_loadu_si256((const __m256i *)buffer);
                __m256i up = _mm256_loadu_si256((const __m256i *)b);
                _mm256_storeu_si256((__m256i *)dest, _mm256_add_epi8(raw, up));
                dest += 32;
                buffer += 32;
                b += 32;
            }
            for (; i < bpr; i++) { *dest++ = uint8_t(*buffer++ + *b++); }
        }
        break;
    }
    case 3: {
        if (imtrker == 0) [[unlikely]] {
            uint8_t *dest = scanline[1];
            memcpy(dest, buffer, bpp);
            dest += bpp;
            buffer += bpp;
            uint8_t *a = dest - bpp;
            for (uint64_t i = bpp; i < bpr; i++) { *dest++ = uint8_t(*buffer++ + ((*a++) >> 1)); }
        } else [[likely]] {
            uint8_t *dest = scanline[1];
            uint8_t *b = scanline[0];
            uint64_t i = 0;
            for (; i < bpp; i++) { *dest++ = uint8_t(*buffer++ + (*b++ >> 1)); }
            uint8_t *a = dest - bpp;
            for (; i < bpr; i++) { *dest++ = uint8_t(*buffer++ + ((*a++ + *b++) >> 1)); }
        }
        break;
    }
    case 4: {
        if (imtrker == 0) [[unlikely]] {
            uint8_t *dest = scanline[1];
            memcpy(dest, buffer, bpp);
            dest += bpp;
            buffer += bpp;
            uint8_t *a = dest - bpp;
            for (uint64_t i = bpp; i < bpr; i++) { *dest++ = uint8_t(*buffer++ + *a++); }
        } else [[likely]] {
            uint8_t *dest = scanline[1];
            uint8_t *b = scanline[0];
            uint64_t i = 0;
            for (; i < bpp; i++) { *dest++ = uint8_t(*buffer++ + *b++); }
            uint8_t *a = dest - bpp;
            uint8_t *c = b - bpp;
            for (; i < bpr; i++) {
                int64_t p = (int64_t)*a + (int64_t)*b - (int64_t)*c;
                uint64_t pa = abs__(p - (int64_t)*a);
                uint64_t pb = abs__(p - (int64_t)*b);
                uint64_t pc = abs__(p - (int64_t)*c);
                uint8_t d;
                if (pa <= pb && pa <= pc) {
                    d = *a;
                } else if (pb <= pc) {
                    d = *b;
                } else {
                    d = *c;
                }
                *dest++ = uint8_t(*buffer++ + d);
                a++;
                b++;
                c++;
            }
        }
        buffer -= bpr;
        break;
    }
    default: [[unlikely]] return false;
    }

    /*
    if (100 < imtrker && imtrker < 102) {
        // buffer -= bpr;
        // for (int64_t i = 0; i < 18; i++) { printf("%04d ", buffer[i]); }
        // printf("\n");
        // for (int64_t i = 0; i < 18; i++) { printf("%04d ", scanline[0][i]); }
        // printf("\n");
        for (int64_t i = 0; i < 18; i++) { printf("%04d ", scanline[1][i]); }
        printf("\n\n");
    }
    */

    return true;
}

static inline void cvt8bit3(Image *result) noexcept {

    uint8_t *src = scanline[1];
    uint8_t *dest = result->buffer.data() + imtrker * (uint64_t)(result->width) * (uint64_t)(result->channels);

    switch (result->bit_depth) {
    case 1: {
        uint64_t i = 0;
        __m128i ones = _mm_set1_epi16(1);
        __m128i zeroes = _mm_setzero_si128();

        for (; i + 16 <= bpr; i += 16) {
            __m128i in = _mm_loadu_si128((const __m128i *)src);

            __m128i in_lo = _mm_unpacklo_epi8(in, zeroes);
            __m128i in_hi = _mm_unpackhi_epi8(in, zeroes);

            __m128i in0_lo = _mm_and_si128(_mm_srli_epi16(in_lo, 0), ones);
            __m128i in1_lo = _mm_and_si128(_mm_srli_epi16(in_lo, 1), ones);
            __m128i in2_lo = _mm_and_si128(_mm_srli_epi16(in_lo, 2), ones);
            __m128i in3_lo = _mm_and_si128(_mm_srli_epi16(in_lo, 3), ones);
            __m128i in4_lo = _mm_and_si128(_mm_srli_epi16(in_lo, 4), ones);
            __m128i in5_lo = _mm_and_si128(_mm_srli_epi16(in_lo, 5), ones);
            __m128i in6_lo = _mm_and_si128(_mm_srli_epi16(in_lo, 6), ones);
            __m128i in7_lo = _mm_and_si128(_mm_srli_epi16(in_lo, 7), ones);

            __m128i in0_hi = _mm_and_si128(_mm_srli_epi16(in_hi, 0), ones);
            __m128i in1_hi = _mm_and_si128(_mm_srli_epi16(in_hi, 1), ones);
            __m128i in2_hi = _mm_and_si128(_mm_srli_epi16(in_hi, 2), ones);
            __m128i in3_hi = _mm_and_si128(_mm_srli_epi16(in_hi, 3), ones);
            __m128i in4_hi = _mm_and_si128(_mm_srli_epi16(in_hi, 4), ones);
            __m128i in5_hi = _mm_and_si128(_mm_srli_epi16(in_hi, 5), ones);
            __m128i in6_hi = _mm_and_si128(_mm_srli_epi16(in_hi, 6), ones);
            __m128i in7_hi = _mm_and_si128(_mm_srli_epi16(in_hi, 7), ones);

            __m128i in0 = _mm_packus_epi16(in0_lo, in0_hi);
            __m128i in1 = _mm_packus_epi16(in1_lo, in1_hi);
            __m128i in2 = _mm_packus_epi16(in2_lo, in2_hi);
            __m128i in3 = _mm_packus_epi16(in3_lo, in3_hi);
            __m128i in4 = _mm_packus_epi16(in4_lo, in4_hi);
            __m128i in5 = _mm_packus_epi16(in5_lo, in5_hi);
            __m128i in6 = _mm_packus_epi16(in6_lo, in6_hi);
            __m128i in7 = _mm_packus_epi16(in7_lo, in7_hi);

            // 16 bit & 8 lanes
            //  8 lanes will interleave by pair so 16 bit 8 lanes to 32 bit 4 lanes to 64 bit 2 lanes
            //  lane is a block of bytes like a chunk, after 8 lanes - > 4 lanes we have group some elements in order as
            //  we want storing elements still working like normal 64-bit does not means an element is now 64 bit it
            //  means 1 lane/chunk is 64 bit

            __m128i a0 = _mm_unpacklo_epi8(in0, in1); // 0 x 1 lo
            __m128i a1 = _mm_unpackhi_epi8(in0, in1); // 0 x 1 hi
            __m128i a2 = _mm_unpacklo_epi8(in2, in3); // 2 x 3 lo
            __m128i a3 = _mm_unpackhi_epi8(in2, in3); // 2 x 3 hi
            __m128i a4 = _mm_unpacklo_epi8(in4, in5); // 4 x 5 lo
            __m128i a5 = _mm_unpackhi_epi8(in4, in5); // 4 x 5 hi
            __m128i a6 = _mm_unpacklo_epi8(in6, in7); // 6 x 7 lo
            __m128i a7 = _mm_unpackhi_epi8(in6, in7); // 6 x 7 hi


            __m128i b0 = _mm_unpacklo_epi16(a0, a2); // 01 lo x 23 lo p0
            __m128i b1 = _mm_unpacklo_epi16(a1, a3); // 01 hi x 23 hi p2
            __m128i b2 = _mm_unpacklo_epi16(a4, a6); // 45 lo x 67 lo p0
            __m128i b3 = _mm_unpacklo_epi16(a5, a7); // 45 hi x 67 hi p2

            __m128i b4 = _mm_unpackhi_epi16(a0, a2); // 01 lo x 23 lo p1
            __m128i b5 = _mm_unpackhi_epi16(a1, a3); // 01 hi x 23 hi p3
            __m128i b6 = _mm_unpackhi_epi16(a4, a6); // 45 lo x 67 lo p1
            __m128i b7 = _mm_unpackhi_epi16(a5, a7); // 45 hi x 67 hi p3


            __m128i c0 = _mm_unpacklo_epi32(b0, b2); // 0123 p0 x 4567 p0 pp0
            __m128i c1 = _mm_unpacklo_epi32(b4, b6); // 0123 p1 x 4567 p1 pp2
            __m128i c2 = _mm_unpacklo_epi32(b1, b3); // 0123 p2 x 4567 p2 pp4
            __m128i c3 = _mm_unpacklo_epi32(b5, b7); // 0123 p3 x 4567 p3 pp6

            __m128i c4 = _mm_unpackhi_epi32(b0, b2); // 0123 p0 x 4567 p0 pp1
            __m128i c5 = _mm_unpackhi_epi32(b4, b6); // 0123 p1 x 4567 p1 pp3
            __m128i c6 = _mm_unpackhi_epi32(b1, b3); // 0123 p2 x 4567 p2 pp5
            __m128i c7 = _mm_unpackhi_epi32(b5, b7); // 0123 p3 x 4567 p3 pp7

            //_mm_storeu_si128((__m128i *)(dest + 0 * 16), c0);
            __m128i c0_lo = _mm_unpacklo_epi8(c0, zeroes);
            __m128i c0_hi = _mm_unpackhi_epi8(c0, zeroes);

            __m128i p0 = _mm_unpacklo_epi16(c0_lo, zeroes);
            __m128i p1 = _mm_unpackhi_epi16(c0_lo, zeroes);
            __m128i p2 = _mm_unpacklo_epi16(c0_hi, zeroes);
            __m128i p3 = _mm_unpackhi_epi16(c0_hi, zeroes);

            _mm_storeu_si128((__m128i *)(dest + 0 * 16), p0);
            _mm_storeu_si128((__m128i *)(dest + 1 * 16), p1);
            _mm_storeu_si128((__m128i *)(dest + 2 * 16), p2);
            _mm_storeu_si128((__m128i *)(dest + 3 * 16), p3);

            dest += 64;

            //_mm_storeu_si128((__m128i *)(dest + 1 * 16), c4);
            __m128i c4_lo = _mm_unpacklo_epi8(c4, zeroes);
            __m128i c4_hi = _mm_unpackhi_epi8(c4, zeroes);

            p0 = _mm_unpacklo_epi16(c4_lo, zeroes);
            p1 = _mm_unpackhi_epi16(c4_lo, zeroes);
            p2 = _mm_unpacklo_epi16(c4_hi, zeroes);
            p3 = _mm_unpackhi_epi16(c4_hi, zeroes);

            _mm_storeu_si128((__m128i *)(dest + 0 * 16), p0);
            _mm_storeu_si128((__m128i *)(dest + 1 * 16), p1);
            _mm_storeu_si128((__m128i *)(dest + 2 * 16), p2);
            _mm_storeu_si128((__m128i *)(dest + 3 * 16), p3);

            dest += 64;

            //_mm_storeu_si128((__m128i *)(dest + 2 * 16), c1);
            __m128i c1_lo = _mm_unpacklo_epi8(c1, zeroes);
            __m128i c1_hi = _mm_unpackhi_epi8(c1, zeroes);

            p0 = _mm_unpacklo_epi16(c1_lo, zeroes);
            p1 = _mm_unpackhi_epi16(c1_lo, zeroes);
            p2 = _mm_unpacklo_epi16(c1_hi, zeroes);
            p3 = _mm_unpackhi_epi16(c1_hi, zeroes);

            _mm_storeu_si128((__m128i *)(dest + 0 * 16), p0);
            _mm_storeu_si128((__m128i *)(dest + 1 * 16), p1);
            _mm_storeu_si128((__m128i *)(dest + 2 * 16), p2);
            _mm_storeu_si128((__m128i *)(dest + 3 * 16), p3);

            dest += 64;

            //_mm_storeu_si128((__m128i *)(dest + 3 * 16), c5);
            __m128i c5_lo = _mm_unpacklo_epi8(c5, zeroes);
            __m128i c5_hi = _mm_unpackhi_epi8(c5, zeroes);

            p0 = _mm_unpacklo_epi16(c5_lo, zeroes);
            p1 = _mm_unpackhi_epi16(c5_lo, zeroes);
            p2 = _mm_unpacklo_epi16(c5_hi, zeroes);
            p3 = _mm_unpackhi_epi16(c5_hi, zeroes);

            _mm_storeu_si128((__m128i *)(dest + 0 * 16), p0);
            _mm_storeu_si128((__m128i *)(dest + 1 * 16), p1);
            _mm_storeu_si128((__m128i *)(dest + 2 * 16), p2);
            _mm_storeu_si128((__m128i *)(dest + 3 * 16), p3);

            dest += 64;

            //_mm_storeu_si128((__m128i *)(dest + 4 * 16), c2);
            __m128i c2_lo = _mm_unpacklo_epi8(c2, zeroes);
            __m128i c2_hi = _mm_unpackhi_epi8(c2, zeroes);

            p0 = _mm_unpacklo_epi16(c2_lo, zeroes);
            p1 = _mm_unpackhi_epi16(c2_lo, zeroes);
            p2 = _mm_unpacklo_epi16(c2_hi, zeroes);
            p3 = _mm_unpackhi_epi16(c2_hi, zeroes);

            _mm_storeu_si128((__m128i *)(dest + 0 * 16), p0);
            _mm_storeu_si128((__m128i *)(dest + 1 * 16), p1);
            _mm_storeu_si128((__m128i *)(dest + 2 * 16), p2);
            _mm_storeu_si128((__m128i *)(dest + 3 * 16), p3);

            dest += 64;

            //_mm_storeu_si128((__m128i *)(dest + 5 * 16), c6);
            __m128i c6_lo = _mm_unpacklo_epi8(c6, zeroes);
            __m128i c6_hi = _mm_unpackhi_epi8(c6, zeroes);

            p0 = _mm_unpacklo_epi16(c6_lo, zeroes);
            p1 = _mm_unpackhi_epi16(c6_lo, zeroes);
            p2 = _mm_unpacklo_epi16(c6_hi, zeroes);
            p3 = _mm_unpackhi_epi16(c6_hi, zeroes);

            _mm_storeu_si128((__m128i *)(dest + 0 * 16), p0);
            _mm_storeu_si128((__m128i *)(dest + 1 * 16), p1);
            _mm_storeu_si128((__m128i *)(dest + 2 * 16), p2);
            _mm_storeu_si128((__m128i *)(dest + 3 * 16), p3);

            dest += 64;

            //_mm_storeu_si128((__m128i *)(dest + 6 * 16), c3);
            __m128i c3_lo = _mm_unpacklo_epi8(c3, zeroes);
            __m128i c3_hi = _mm_unpackhi_epi8(c3, zeroes);

            p0 = _mm_unpacklo_epi16(c3_lo, zeroes);
            p1 = _mm_unpackhi_epi16(c3_lo, zeroes);
            p2 = _mm_unpacklo_epi16(c3_hi, zeroes);
            p3 = _mm_unpackhi_epi16(c3_hi, zeroes);

            _mm_storeu_si128((__m128i *)(dest + 0 * 16), p0);
            _mm_storeu_si128((__m128i *)(dest + 1 * 16), p1);
            _mm_storeu_si128((__m128i *)(dest + 2 * 16), p2);
            _mm_storeu_si128((__m128i *)(dest + 3 * 16), p3);

            dest += 64;

            //_mm_storeu_si128((__m128i *)(dest + 7 * 16), c7);
            __m128i c7_lo = _mm_unpacklo_epi8(c7, zeroes);
            __m128i c7_hi = _mm_unpackhi_epi8(c7, zeroes);

            p0 = _mm_unpacklo_epi16(c7_lo, zeroes);
            p1 = _mm_unpackhi_epi16(c7_lo, zeroes);
            p2 = _mm_unpacklo_epi16(c7_hi, zeroes);
            p3 = _mm_unpackhi_epi16(c7_hi, zeroes);

            _mm_storeu_si128((__m128i *)(dest + 0 * 16), p0);
            _mm_storeu_si128((__m128i *)(dest + 1 * 16), p1);
            _mm_storeu_si128((__m128i *)(dest + 2 * 16), p2);
            _mm_storeu_si128((__m128i *)(dest + 3 * 16), p3);

            dest += 64;
            src += 16;
        }
        for (; i < bpr; i++) {
            for (int j = 7; j >= 0; j--) {
                *dest = ((*src >> j) & 1);
                dest += 4;
            }
            src++;
        }
        break;
    }
    case 2: {
        uint64_t i = 0;
        __m128i ones = _mm_set1_epi16(0b11);
        __m128i zeroes = _mm_setzero_si128();

        for (; i + 16 <= bpr; i += 16) {
            __m128i in = _mm_loadu_si128((const __m128i *)src);

            __m128i in_lo = _mm_unpacklo_epi8(in, zeroes);
            __m128i in_hi = _mm_unpacklo_epi8(in, zeroes);

            __m128i in0_lo = _mm_and_si128(_mm_srli_epi16(in_lo, 0), ones);
            __m128i in0_hi = _mm_and_si128(_mm_srli_epi16(in_hi, 0), ones);
            __m128i in1_lo = _mm_and_si128(_mm_srli_epi16(in_lo, 2), ones);
            __m128i in1_hi = _mm_and_si128(_mm_srli_epi16(in_hi, 2), ones);
            __m128i in2_lo = _mm_and_si128(_mm_srli_epi16(in_lo, 4), ones);
            __m128i in2_hi = _mm_and_si128(_mm_srli_epi16(in_hi, 4), ones);
            __m128i in3_lo = _mm_and_si128(_mm_srli_epi16(in_lo, 6), ones);
            __m128i in3_hi = _mm_and_si128(_mm_srli_epi16(in_hi, 6), ones);

            __m128i in0 = _mm_packus_epi16(in0_lo, in0_hi); // 0
            __m128i in1 = _mm_packus_epi16(in1_lo, in1_hi); // 1
            __m128i in2 = _mm_packus_epi16(in2_lo, in2_hi); // 2
            __m128i in3 = _mm_packus_epi16(in3_lo, in3_hi); // 3

            __m128i a0 = _mm_unpacklo_epi8(in0, in1); // 0 lo x 1 lo p0
            __m128i a1 = _mm_unpackhi_epi8(in0, in1); // 0 hi x 1 hi p1
            __m128i a2 = _mm_unpacklo_epi8(in2, in3); // 2 lo x 3 lo p0
            __m128i a3 = _mm_unpackhi_epi8(in2, in3); // 2 hi x 3 hi p1

            __m128i b0 = _mm_unpacklo_epi16(a0, a2); // 01 p0 x 23 p0 pp0
            __m128i b1 = _mm_unpacklo_epi16(a1, a3); // 01 p1 x 23 p1 pp2
            __m128i b2 = _mm_unpackhi_epi16(a0, a2); // 01 p0 x 23 p0 pp1
            __m128i b3 = _mm_unpackhi_epi16(a1, a3); // 01 p1 x 23 p1 pp3


            //_mm_storeu_si128((__m128i *)(dest + 0 * 16), b0);
            __m128i b0_lo = _mm_unpacklo_epi8(b0, zeroes);
            __m128i b0_hi = _mm_unpackhi_epi8(b0, zeroes);

            __m128i p0 = _mm_unpacklo_epi16(b0_lo, zeroes);
            __m128i p1 = _mm_unpackhi_epi16(b0_lo, zeroes);
            __m128i p2 = _mm_unpacklo_epi16(b0_hi, zeroes);
            __m128i p3 = _mm_unpackhi_epi16(b0_hi, zeroes);

            _mm_storeu_si128((__m128i *)(dest + 0 * 16), p0);
            _mm_storeu_si128((__m128i *)(dest + 1 * 16), p1);
            _mm_storeu_si128((__m128i *)(dest + 2 * 16), p2);
            _mm_storeu_si128((__m128i *)(dest + 3 * 16), p3);

            dest += 64;

            //_mm_storeu_si128((__m128i *)(dest + 1 * 16), b2);
            __m128i b2_lo = _mm_unpacklo_epi8(b2, zeroes);
            __m128i b2_hi = _mm_unpackhi_epi8(b2, zeroes);

            p0 = _mm_unpacklo_epi16(b2_lo, zeroes);
            p1 = _mm_unpackhi_epi16(b2_lo, zeroes);
            p2 = _mm_unpacklo_epi16(b2_hi, zeroes);
            p3 = _mm_unpackhi_epi16(b2_hi, zeroes);

            _mm_storeu_si128((__m128i *)(dest + 0 * 16), p0);
            _mm_storeu_si128((__m128i *)(dest + 1 * 16), p1);
            _mm_storeu_si128((__m128i *)(dest + 2 * 16), p2);
            _mm_storeu_si128((__m128i *)(dest + 3 * 16), p3);

            dest += 64;

            //_mm_storeu_si128((__m128i *)(dest + 2 * 16), b1);
            __m128i b1_lo = _mm_unpacklo_epi8(b1, zeroes);
            __m128i b1_hi = _mm_unpackhi_epi8(b1, zeroes);

            p0 = _mm_unpacklo_epi16(b1_lo, zeroes);
            p1 = _mm_unpackhi_epi16(b1_lo, zeroes);
            p2 = _mm_unpacklo_epi16(b1_hi, zeroes);
            p3 = _mm_unpackhi_epi16(b1_hi, zeroes);

            _mm_storeu_si128((__m128i *)(dest + 0 * 16), p0);
            _mm_storeu_si128((__m128i *)(dest + 1 * 16), p1);
            _mm_storeu_si128((__m128i *)(dest + 2 * 16), p2);
            _mm_storeu_si128((__m128i *)(dest + 3 * 16), p3);

            dest += 64;

            //_mm_storeu_si128((__m128i *)(dest + 3 * 16), b3);
            __m128i b3_lo = _mm_unpacklo_epi8(b3, zeroes);
            __m128i b3_hi = _mm_unpackhi_epi8(b3, zeroes);

            p0 = _mm_unpacklo_epi16(b3_lo, zeroes);
            p1 = _mm_unpackhi_epi16(b3_lo, zeroes);
            p2 = _mm_unpacklo_epi16(b3_hi, zeroes);
            p3 = _mm_unpackhi_epi16(b3_hi, zeroes);

            _mm_storeu_si128((__m128i *)(dest + 0 * 16), p0);
            _mm_storeu_si128((__m128i *)(dest + 1 * 16), p1);
            _mm_storeu_si128((__m128i *)(dest + 2 * 16), p2);
            _mm_storeu_si128((__m128i *)(dest + 3 * 16), p3);

            dest += 64;
            src += 16;
        }
        for (; i < bpr; i++) {
            for (int j = 3; j >= 0; j--) {
                *dest++ = ((*src >> (j * 2)) & 3);
                dest += 4;
            }
            src++;
        }
        break;
    }
    case 4: {
        uint64_t i = 0;
        __m128i m0 = _mm_set1_epi8(0b11110000);
        __m128i m1 = _mm_set1_epi8(0b00001111);
        __m128i zeroes = _mm_setzero_si128();

        for (; i + 16 <= bpr; i += 16) {
            __m128i in = _mm_loadu_si128((const __m128i *)src);

            __m128i in0 = _mm_srli_epi64(_mm_and_si128(in, m0), 4);
            __m128i in1 = _mm_and_si128(in, m1);

            __m128i a0 = _mm_unpacklo_epi8(in0, in1); // 0 x 1 lo
            __m128i a1 = _mm_unpackhi_epi8(in0, in1); // 0 x 1 hi

            //_mm_storeu_si128((__m128i *)(dest + 0 * 16), a0);
            __m128i a0_lo = _mm_unpacklo_epi8(a0, zeroes);
            __m128i a0_hi = _mm_unpackhi_epi8(a0, zeroes);

            __m128i p0 = _mm_unpacklo_epi16(a0_lo, zeroes);
            __m128i p1 = _mm_unpackhi_epi16(a0_lo, zeroes);
            __m128i p2 = _mm_unpacklo_epi16(a0_hi, zeroes);
            __m128i p3 = _mm_unpackhi_epi16(a0_hi, zeroes);

            _mm_storeu_si128((__m128i *)(dest + 0 * 16), p0);
            _mm_storeu_si128((__m128i *)(dest + 1 * 16), p1);
            _mm_storeu_si128((__m128i *)(dest + 2 * 16), p2);
            _mm_storeu_si128((__m128i *)(dest + 3 * 16), p3);

            dest += 64;

            //_mm_storeu_si128((__m128i *)(dest + 1 * 16), a1);
            __m128i a1_lo = _mm_unpacklo_epi8(a1, zeroes);
            __m128i a1_hi = _mm_unpackhi_epi8(a1, zeroes);

            p0 = _mm_unpacklo_epi16(a1_lo, zeroes);
            p1 = _mm_unpackhi_epi16(a1_lo, zeroes);
            p2 = _mm_unpacklo_epi16(a1_hi, zeroes);
            p3 = _mm_unpackhi_epi16(a1_hi, zeroes);

            _mm_storeu_si128((__m128i *)(dest + 0 * 16), p0);
            _mm_storeu_si128((__m128i *)(dest + 1 * 16), p1);
            _mm_storeu_si128((__m128i *)(dest + 2 * 16), p2);
            _mm_storeu_si128((__m128i *)(dest + 3 * 16), p3);

            dest += 64;
            src += 16;
        }
        for (; i < bpr; i++) {
            for (int j = 1; j >= 0; j--) {
                *dest = ((*src >> (j * 4)) & 0b1111);
                dest += 4;
            }
            src++;
        }
        break;
    }
    case 8: {
        uint64_t i = 0;
        __m128i zeroes = _mm_setzero_si128();

        for (; i + 16 <= bpr; i += 16) {

            __m128i in = _mm_loadu_si128((const __m128i *)src);

            __m128i in_lo = _mm_unpacklo_epi8(in, zeroes);
            __m128i in_hi = _mm_unpackhi_epi8(in, zeroes);

            __m128i p0 = _mm_unpacklo_epi16(in_lo, zeroes);
            __m128i p1 = _mm_unpackhi_epi16(in_lo, zeroes);
            __m128i p2 = _mm_unpacklo_epi16(in_hi, zeroes);
            __m128i p3 = _mm_unpackhi_epi16(in_hi, zeroes);

            _mm_storeu_si128((__m128i *)(dest + 0 * 16), p0);
            _mm_storeu_si128((__m128i *)(dest + 1 * 16), p1);
            _mm_storeu_si128((__m128i *)(dest + 2 * 16), p2);
            _mm_storeu_si128((__m128i *)(dest + 3 * 16), p3);

            dest += 64;
            src += 16;
        }
        for (; i < bpr; i++) {
            *dest = *src;
            dest += 4;
            src++;
        }
        break;
    }
    }
}

static inline void cvt8bit(Image *result) noexcept {
    const bool ct3flag = cvt8bitflag; // if true this function will not scale the data into mod256, it'll only do the bit depth conversion
    // meaning the final data could be in that same bit depth mode

    uint8_t *src = scanline[1];
    uint8_t *dest = result->buffer.data() + imtrker * (uint64_t)(result->width) * (1 + (!ct3flag) * ((uint64_t)(result->channels) - 1));

    switch (result->bit_depth) {
    case 1: {
        uint64_t i = 0;
        __m128i FFs = _mm_set1_epi16(1 + (!ct3flag) * (255 - 1));
        __m128i ones = _mm_set1_epi16(1);
        __m128i zeroes = _mm_setzero_si128();

        for (; i + 16 <= bpr; i += 16) {
            __m128i in = _mm_loadu_si128((const __m128i *)src);

            __m128i in_lo = _mm_unpacklo_epi8(in, zeroes);
            __m128i in_hi = _mm_unpackhi_epi8(in, zeroes);

            __m128i in0_lo = _mm_mullo_epi16(FFs, _mm_and_si128(_mm_srli_epi16(in_lo, 0), ones));
            __m128i in1_lo = _mm_mullo_epi16(FFs, _mm_and_si128(_mm_srli_epi16(in_lo, 1), ones));
            __m128i in2_lo = _mm_mullo_epi16(FFs, _mm_and_si128(_mm_srli_epi16(in_lo, 2), ones));
            __m128i in3_lo = _mm_mullo_epi16(FFs, _mm_and_si128(_mm_srli_epi16(in_lo, 3), ones));
            __m128i in4_lo = _mm_mullo_epi16(FFs, _mm_and_si128(_mm_srli_epi16(in_lo, 4), ones));
            __m128i in5_lo = _mm_mullo_epi16(FFs, _mm_and_si128(_mm_srli_epi16(in_lo, 5), ones));
            __m128i in6_lo = _mm_mullo_epi16(FFs, _mm_and_si128(_mm_srli_epi16(in_lo, 6), ones));
            __m128i in7_lo = _mm_mullo_epi16(FFs, _mm_and_si128(_mm_srli_epi16(in_lo, 7), ones));

            __m128i in0_hi = _mm_mullo_epi16(FFs, _mm_and_si128(_mm_srli_epi16(in_hi, 0), ones));
            __m128i in1_hi = _mm_mullo_epi16(FFs, _mm_and_si128(_mm_srli_epi16(in_hi, 1), ones));
            __m128i in2_hi = _mm_mullo_epi16(FFs, _mm_and_si128(_mm_srli_epi16(in_hi, 2), ones));
            __m128i in3_hi = _mm_mullo_epi16(FFs, _mm_and_si128(_mm_srli_epi16(in_hi, 3), ones));
            __m128i in4_hi = _mm_mullo_epi16(FFs, _mm_and_si128(_mm_srli_epi16(in_hi, 4), ones));
            __m128i in5_hi = _mm_mullo_epi16(FFs, _mm_and_si128(_mm_srli_epi16(in_hi, 5), ones));
            __m128i in6_hi = _mm_mullo_epi16(FFs, _mm_and_si128(_mm_srli_epi16(in_hi, 6), ones));
            __m128i in7_hi = _mm_mullo_epi16(FFs, _mm_and_si128(_mm_srli_epi16(in_hi, 7), ones));

            __m128i in0 = _mm_packus_epi16(in0_lo, in0_hi);
            __m128i in1 = _mm_packus_epi16(in1_lo, in1_hi);
            __m128i in2 = _mm_packus_epi16(in2_lo, in2_hi);
            __m128i in3 = _mm_packus_epi16(in3_lo, in3_hi);
            __m128i in4 = _mm_packus_epi16(in4_lo, in4_hi);
            __m128i in5 = _mm_packus_epi16(in5_lo, in5_hi);
            __m128i in6 = _mm_packus_epi16(in6_lo, in6_hi);
            __m128i in7 = _mm_packus_epi16(in7_lo, in7_hi);

            // 16 bit & 8 lanes
            //  8 lanes will interleave by pair so 16 bit 8 lanes to 32 bit 4 lanes to 64 bit 2 lanes
            //  lane is a block of bytes like a chunk, after 8 lanes - > 4 lanes we have group some elements in order as
            //  we want storing elements still working like normal 64-bit does not means an element is now 64 bit it
            //  means 1 lane/chunk is 64 bit

            __m128i a0 = _mm_unpacklo_epi8(in0, in1); // 0 x 1 lo
            __m128i a1 = _mm_unpackhi_epi8(in0, in1); // 0 x 1 hi
            __m128i a2 = _mm_unpacklo_epi8(in2, in3); // 2 x 3 lo
            __m128i a3 = _mm_unpackhi_epi8(in2, in3); // 2 x 3 hi
            __m128i a4 = _mm_unpacklo_epi8(in4, in5); // 4 x 5 lo
            __m128i a5 = _mm_unpackhi_epi8(in4, in5); // 4 x 5 hi
            __m128i a6 = _mm_unpacklo_epi8(in6, in7); // 6 x 7 lo
            __m128i a7 = _mm_unpackhi_epi8(in6, in7); // 6 x 7 hi


            __m128i b0 = _mm_unpacklo_epi16(a0, a2); // 01 lo x 23 lo p0
            __m128i b1 = _mm_unpacklo_epi16(a1, a3); // 01 hi x 23 hi p2
            __m128i b2 = _mm_unpacklo_epi16(a4, a6); // 45 lo x 67 lo p0
            __m128i b3 = _mm_unpacklo_epi16(a5, a7); // 45 hi x 67 hi p2

            __m128i b4 = _mm_unpackhi_epi16(a0, a2); // 01 lo x 23 lo p1
            __m128i b5 = _mm_unpackhi_epi16(a1, a3); // 01 hi x 23 hi p3
            __m128i b6 = _mm_unpackhi_epi16(a4, a6); // 45 lo x 67 lo p1
            __m128i b7 = _mm_unpackhi_epi16(a5, a7); // 45 hi x 67 hi p3


            __m128i c0 = _mm_unpacklo_epi32(b0, b2); // 0123 p0 x 4567 p0 pp0
            __m128i c1 = _mm_unpacklo_epi32(b4, b6); // 0123 p1 x 4567 p1 pp2
            __m128i c2 = _mm_unpacklo_epi32(b1, b3); // 0123 p2 x 4567 p2 pp4
            __m128i c3 = _mm_unpacklo_epi32(b5, b7); // 0123 p3 x 4567 p3 pp6

            __m128i c4 = _mm_unpackhi_epi32(b0, b2); // 0123 p0 x 4567 p0 pp1
            __m128i c5 = _mm_unpackhi_epi32(b4, b6); // 0123 p1 x 4567 p1 pp3
            __m128i c6 = _mm_unpackhi_epi32(b1, b3); // 0123 p2 x 4567 p2 pp5
            __m128i c7 = _mm_unpackhi_epi32(b5, b7); // 0123 p3 x 4567 p3 pp7

            _mm_storeu_si128((__m128i *)(dest + 0 * 16), c0);
            _mm_storeu_si128((__m128i *)(dest + 1 * 16), c4);
            _mm_storeu_si128((__m128i *)(dest + 2 * 16), c1);
            _mm_storeu_si128((__m128i *)(dest + 3 * 16), c5);
            _mm_storeu_si128((__m128i *)(dest + 4 * 16), c2);
            _mm_storeu_si128((__m128i *)(dest + 5 * 16), c6);
            _mm_storeu_si128((__m128i *)(dest + 6 * 16), c3);
            _mm_storeu_si128((__m128i *)(dest + 7 * 16), c7);

            src += 16;
            dest += 128;
        }
        for (; i < bpr; i++) {
            for (int j = 7; j >= 0; j--) { *dest++ = ((*src >> j) & 1) * (1 + (!ct3flag) * (255 - 1)); }
            src++;
        }
        break;
    }
    case 2: {
        uint64_t i = 0;
        __m128i FFs = _mm_set1_epi16(1 + (!ct3flag) * (85 - 1));
        __m128i ones = _mm_set1_epi16(0b11);
        __m128i zeroes = _mm_setzero_si128();

        for (; i + 16 <= bpr; i += 16) {
            __m128i in = _mm_loadu_si128((const __m128i *)src);

            __m128i in_lo = _mm_unpacklo_epi8(in, zeroes);
            __m128i in_hi = _mm_unpacklo_epi8(in, zeroes);

            __m128i in0_lo = _mm_mullo_epi16(FFs, _mm_and_si128(_mm_srli_epi16(in_lo, 0), ones));
            __m128i in0_hi = _mm_mullo_epi16(FFs, _mm_and_si128(_mm_srli_epi16(in_hi, 0), ones));
            __m128i in1_lo = _mm_mullo_epi16(FFs, _mm_and_si128(_mm_srli_epi16(in_lo, 2), ones));
            __m128i in1_hi = _mm_mullo_epi16(FFs, _mm_and_si128(_mm_srli_epi16(in_hi, 2), ones));
            __m128i in2_lo = _mm_mullo_epi16(FFs, _mm_and_si128(_mm_srli_epi16(in_lo, 4), ones));
            __m128i in2_hi = _mm_mullo_epi16(FFs, _mm_and_si128(_mm_srli_epi16(in_hi, 4), ones));
            __m128i in3_lo = _mm_mullo_epi16(FFs, _mm_and_si128(_mm_srli_epi16(in_lo, 6), ones));
            __m128i in3_hi = _mm_mullo_epi16(FFs, _mm_and_si128(_mm_srli_epi16(in_hi, 6), ones));

            __m128i in0 = _mm_packus_epi16(in0_lo, in0_hi); // 0
            __m128i in1 = _mm_packus_epi16(in1_lo, in1_hi); // 1
            __m128i in2 = _mm_packus_epi16(in2_lo, in2_hi); // 2
            __m128i in3 = _mm_packus_epi16(in3_lo, in3_hi); // 3

            __m128i a0 = _mm_unpacklo_epi8(in0, in1); // 0 lo x 1 lo p0
            __m128i a1 = _mm_unpackhi_epi8(in0, in1); // 0 hi x 1 hi p1
            __m128i a2 = _mm_unpacklo_epi8(in2, in3); // 2 lo x 3 lo p0
            __m128i a3 = _mm_unpackhi_epi8(in2, in3); // 2 hi x 3 hi p1

            __m128i b0 = _mm_unpacklo_epi16(a0, a2); // 01 p0 x 23 p0 pp0
            __m128i b1 = _mm_unpacklo_epi16(a1, a3); // 01 p1 x 23 p1 pp2
            __m128i b2 = _mm_unpackhi_epi16(a0, a2); // 01 p0 x 23 p0 pp1
            __m128i b3 = _mm_unpackhi_epi16(a1, a3); // 01 p1 x 23 p1 pp3

            _mm_storeu_si128((__m128i *)(dest + 0 * 16), b0);
            _mm_storeu_si128((__m128i *)(dest + 1 * 16), b2);
            _mm_storeu_si128((__m128i *)(dest + 2 * 16), b1);
            _mm_storeu_si128((__m128i *)(dest + 3 * 16), b3);

            src += 16;
            dest += 64;
        }
        for (; i < bpr; i++) {
            for (int j = 3; j >= 0; j--) { *dest++ = ((*src >> (j * 2)) & 3) * (1 + (!ct3flag) * (85 - 1)); }
            src++;
        }
        break;
    }
    case 4: {
        uint64_t i = 0;
        __m128i FFs = _mm_set1_epi16(1 + (!ct3flag) * (17 - 1));
        __m128i ones = _mm_set1_epi16(0b1111);
        __m128i zeroes = _mm_setzero_si128();

        for (; i + 16 <= bpr; i += 16) {
            __m128i in = _mm_loadu_si128((const __m128i *)src);

            __m128i in_lo = _mm_unpacklo_epi8(in, zeroes);
            __m128i in_hi = _mm_unpackhi_epi8(in, zeroes);

            __m128i in0_lo = _mm_mullo_epi16(FFs, _mm_and_si128(_mm_srli_epi16(in_lo, 0), ones));
            __m128i in0_hi = _mm_mullo_epi16(FFs, _mm_and_si128(_mm_srli_epi16(in_hi, 0), ones));
            __m128i in1_lo = _mm_mullo_epi16(FFs, _mm_and_si128(_mm_srli_epi16(in_lo, 4), ones));
            __m128i in1_hi = _mm_mullo_epi16(FFs, _mm_and_si128(_mm_srli_epi16(in_hi, 4), ones));

            __m128i in0 = _mm_packus_epi16(in0_lo, in0_hi);
            __m128i in1 = _mm_packus_epi16(in1_lo, in1_hi);

            __m128i a0 = _mm_unpacklo_epi8(in0, in1); // 0 x 1 lo
            __m128i a1 = _mm_unpackhi_epi8(in0, in1); // 0 x 1 hi

            _mm_storeu_si128((__m128i *)(dest + 0 * 16), a0);
            _mm_storeu_si128((__m128i *)(dest + 1 * 16), a1);

            src += 16;
            dest += 32;
        }
        for (; i < bpr; i++) {
            for (int j = 1; j >= 0; j--) { *dest++ = ((*src >> (j * 4)) & 0b1111) * (1 + (!ct3flag) * (17 - 1)); }
            src++;
        }
        break;
    }
    case 8: {
        memcpy(dest, src, bpr);
        break;
    }
    case 16: {
        uint64_t i = 0;

        __m256i zz = _mm256_setzero_si256();
        __m256i mask = _mm256_setr_epi8(0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1);

        for (; i + 16 <= bpr; i += 16) {
            __m256i in = _mm256_loadu_si256((const __m256i *)src);

            __m256i out = _mm256_shuffle_epi8(in, mask); // keep the msl

            _mm_storeu_si128((__m128i *)dest, _mm256_castsi256_si128(out));

            src += 16;
            dest += 8;
        }

        for (; i < bpr; i += 2) {
            *dest++ = *src++;
            src++;
        }
        break;
    }
    }
}

static inline void cvt16bit(Image *result) noexcept {

    uint8_t *src = scanline[1];
    uint8_t *dest = result->buffer.data() + imtrker * (uint64_t)(result->width) * (uint64_t)(result->channels) * 2;

    switch (result->bit_depth) {
    case 1: {
        uint64_t i = 0;
        __m128i FFs = _mm_set1_epi16(-1);
        __m128i ones = _mm_set1_epi16(1);
        __m128i zeroes = _mm_setzero_si128();

        for (; i + 16 <= bpr; i += 16) {
            __m128i in = _mm_loadu_si128((const __m128i *)src);

            // 16 bit lane
            __m128i in_lo = _mm_unpacklo_epi8(in, zeroes);
            __m128i in_hi = _mm_unpackhi_epi8(in, zeroes);

            // 16 bit lane for each bit depth
            __m128i in0_lo = _mm_mullo_epi16(FFs, _mm_and_si128(_mm_srli_epi16(in_lo, 0), ones));
            __m128i in1_lo = _mm_mullo_epi16(FFs, _mm_and_si128(_mm_srli_epi16(in_lo, 1), ones));
            __m128i in2_lo = _mm_mullo_epi16(FFs, _mm_and_si128(_mm_srli_epi16(in_lo, 2), ones));
            __m128i in3_lo = _mm_mullo_epi16(FFs, _mm_and_si128(_mm_srli_epi16(in_lo, 3), ones));
            __m128i in4_lo = _mm_mullo_epi16(FFs, _mm_and_si128(_mm_srli_epi16(in_lo, 4), ones));
            __m128i in5_lo = _mm_mullo_epi16(FFs, _mm_and_si128(_mm_srli_epi16(in_lo, 5), ones));
            __m128i in6_lo = _mm_mullo_epi16(FFs, _mm_and_si128(_mm_srli_epi16(in_lo, 6), ones));
            __m128i in7_lo = _mm_mullo_epi16(FFs, _mm_and_si128(_mm_srli_epi16(in_lo, 7), ones));

            __m128i in0_hi = _mm_mullo_epi16(FFs, _mm_and_si128(_mm_srli_epi16(in_hi, 0), ones));
            __m128i in1_hi = _mm_mullo_epi16(FFs, _mm_and_si128(_mm_srli_epi16(in_hi, 1), ones));
            __m128i in2_hi = _mm_mullo_epi16(FFs, _mm_and_si128(_mm_srli_epi16(in_hi, 2), ones));
            __m128i in3_hi = _mm_mullo_epi16(FFs, _mm_and_si128(_mm_srli_epi16(in_hi, 3), ones));
            __m128i in4_hi = _mm_mullo_epi16(FFs, _mm_and_si128(_mm_srli_epi16(in_hi, 4), ones));
            __m128i in5_hi = _mm_mullo_epi16(FFs, _mm_and_si128(_mm_srli_epi16(in_hi, 5), ones));
            __m128i in6_hi = _mm_mullo_epi16(FFs, _mm_and_si128(_mm_srli_epi16(in_hi, 6), ones));
            __m128i in7_hi = _mm_mullo_epi16(FFs, _mm_and_si128(_mm_srli_epi16(in_hi, 7), ones));

            // 16 bit & 8 lanes
            //  8 lanes will interleave by pair so 16 bit 8 lanes to 32 bit 4 lanes to 64 bit 2 lanes
            //  lane is a block of bytes like a chunk, after 8 lanes - > 4 lanes we have group some elements in order as
            //  we want storing elements still working like normal 64-bit does not means an element is now 64 bit it
            //  means 1 lane/chunk is 64 bit

            __m128i a0_lo = _mm_unpacklo_epi16(in0_lo, in1_lo); // 0 x 1 lo
            __m128i a1_lo = _mm_unpackhi_epi16(in0_lo, in1_lo); // 0 x 1 hi
            __m128i a2_lo = _mm_unpacklo_epi16(in2_lo, in3_lo); // 2 x 3 lo
            __m128i a3_lo = _mm_unpackhi_epi16(in2_lo, in3_lo); // 2 x 3 hi
            __m128i a4_lo = _mm_unpacklo_epi16(in4_lo, in5_lo); // 4 x 5 lo
            __m128i a5_lo = _mm_unpackhi_epi16(in4_lo, in5_lo); // 4 x 5 hi
            __m128i a6_lo = _mm_unpacklo_epi16(in6_lo, in7_lo); // 6 x 7 lo
            __m128i a7_lo = _mm_unpackhi_epi16(in6_lo, in7_lo); // 6 x 7 hi


            __m128i b0_lo = _mm_unpacklo_epi32(a0_lo, a2_lo); // 01 lo x 23 lo LO p0
            __m128i b1_lo = _mm_unpacklo_epi32(a1_lo, a3_lo); // 01 hi x 23 hi LO p2
            __m128i b2_lo = _mm_unpacklo_epi32(a4_lo, a6_lo); // 45 lo x 67 lo LO p0
            __m128i b3_lo = _mm_unpacklo_epi32(a5_lo, a7_lo); // 45 hi x 67 hi LO p2

            __m128i b4_lo = _mm_unpackhi_epi32(a0_lo, a2_lo); // 01 lo x 23 lo HI p1
            __m128i b5_lo = _mm_unpackhi_epi32(a1_lo, a3_lo); // 01 hi x 23 hi HI p3
            __m128i b6_lo = _mm_unpackhi_epi32(a4_lo, a6_lo); // 45 lo x 67 lo HI p1
            __m128i b7_lo = _mm_unpackhi_epi32(a5_lo, a7_lo); // 45 lo x 67 lo HI p3


            __m128i c0_lo = _mm_unpacklo_epi64(b0_lo, b2_lo); // 0123 p0 x 4567 p0 pp0
            __m128i c1_lo = _mm_unpacklo_epi64(b4_lo, b6_lo); // 0123 p1 x 4567 p1 pp2
            __m128i c2_lo = _mm_unpacklo_epi64(b1_lo, b3_lo); // 0123 p2 x 4567 p2 pp4
            __m128i c3_lo = _mm_unpacklo_epi64(b5_lo, b7_lo); // 0123 p3 x 4567 p3 pp6

            __m128i c4_lo = _mm_unpackhi_epi64(b0_lo, b2_lo); // 0123 p0 x 4567 p0 pp1
            __m128i c5_lo = _mm_unpackhi_epi64(b4_lo, b6_lo); // 0123 p1 x 4567 p1 pp3
            __m128i c6_lo = _mm_unpackhi_epi64(b1_lo, b3_lo); // 0123 p2 x 4567 p2 pp5
            __m128i c7_lo = _mm_unpackhi_epi64(b5_lo, b7_lo); // 0123 p3 x 4567 p3 pp7

            _mm_storeu_si128((__m128i *)(dest + 0 * 16), c0_lo);
            _mm_storeu_si128((__m128i *)(dest + 1 * 16), c4_lo);
            _mm_storeu_si128((__m128i *)(dest + 2 * 16), c1_lo);
            _mm_storeu_si128((__m128i *)(dest + 3 * 16), c5_lo);
            _mm_storeu_si128((__m128i *)(dest + 4 * 16), c2_lo);
            _mm_storeu_si128((__m128i *)(dest + 5 * 16), c6_lo);
            _mm_storeu_si128((__m128i *)(dest + 6 * 16), c3_lo);
            _mm_storeu_si128((__m128i *)(dest + 7 * 16), c7_lo);

            dest += 128;

            __m128i a0_hi = _mm_unpacklo_epi16(in0_hi, in1_hi); // 0 1 lo
            __m128i a1_hi = _mm_unpackhi_epi16(in0_hi, in1_hi); // 0 1 hi
            __m128i a2_hi = _mm_unpacklo_epi16(in2_hi, in3_hi); // 2 3 lo
            __m128i a3_hi = _mm_unpackhi_epi16(in2_hi, in3_hi); // 2 3 hi
            __m128i a4_hi = _mm_unpacklo_epi16(in4_hi, in5_hi); // 4 5 lo
            __m128i a5_hi = _mm_unpackhi_epi16(in4_hi, in5_hi); // 4 5 hi
            __m128i a6_hi = _mm_unpacklo_epi16(in6_hi, in7_hi); // 6 7 lo
            __m128i a7_hi = _mm_unpackhi_epi16(in6_hi, in7_hi); // 6 7 hi


            __m128i b0_hi = _mm_unpacklo_epi32(a0_hi, a2_hi); // 01 lo x 23 lo LO p0
            __m128i b1_hi = _mm_unpacklo_epi32(a1_hi, a3_hi); // 01 hi x 23 hi LO p2
            __m128i b2_hi = _mm_unpacklo_epi32(a4_hi, a6_hi); // 45 lo x 67 lo LO p0
            __m128i b3_hi = _mm_unpacklo_epi32(a5_hi, a7_hi); // 45 hi x 67 hi LO p2

            __m128i b4_hi = _mm_unpackhi_epi32(a0_hi, a2_hi); // 01 lo x 23 lo HI p1
            __m128i b5_hi = _mm_unpackhi_epi32(a1_hi, a3_hi); // 01 hi x 23 hi HI p3
            __m128i b6_hi = _mm_unpackhi_epi32(a4_hi, a6_hi); // 45 lo x 67 lo HI p1
            __m128i b7_hi = _mm_unpackhi_epi32(a5_hi, a7_hi); // 45 lo x 67 lo HI p3


            __m128i c0_hi = _mm_unpacklo_epi64(b0_lo, b2_lo); // 0123 p0 x 4567 p0 pp0
            __m128i c1_hi = _mm_unpacklo_epi64(b4_lo, b6_lo); // 0123 p1 x 4567 p1 pp2
            __m128i c2_hi = _mm_unpacklo_epi64(b1_lo, b3_lo); // 0123 p2 x 4567 p2 pp4
            __m128i c3_hi = _mm_unpacklo_epi64(b5_lo, b7_lo); // 0123 p3 x 4567 p3 pp6

            __m128i c4_hi = _mm_unpackhi_epi64(b0_hi, b2_hi); // 0123 p0 x 4567 p0 pp1
            __m128i c5_hi = _mm_unpackhi_epi64(b4_hi, b6_hi); // 0123 p1 x 4567 p1 pp3
            __m128i c6_hi = _mm_unpackhi_epi64(b1_hi, b3_hi); // 0123 p2 x 4567 p2 pp5
            __m128i c7_hi = _mm_unpackhi_epi64(b5_hi, b7_hi); // 0123 p3 x 4567 p3 pp7

            _mm_storeu_si128((__m128i *)(dest + 0 * 16), c0_hi);
            _mm_storeu_si128((__m128i *)(dest + 1 * 16), c4_hi);
            _mm_storeu_si128((__m128i *)(dest + 2 * 16), c1_hi);
            _mm_storeu_si128((__m128i *)(dest + 3 * 16), c5_hi);
            _mm_storeu_si128((__m128i *)(dest + 4 * 16), c2_hi);
            _mm_storeu_si128((__m128i *)(dest + 5 * 16), c6_hi);
            _mm_storeu_si128((__m128i *)(dest + 6 * 16), c3_hi);
            _mm_storeu_si128((__m128i *)(dest + 7 * 16), c7_hi);

            src += 16;
            dest += 128;
        }
        uint16_t *d = reinterpret_cast<uint16_t *>(dest);
        for (; i < bpr; i++) {
            for (int j = 7; j >= 0; j--) { *d++ = uint16_t(((*src >> j) & 1)) * uint16_t(-1); }
            src++;
        }
        break;
    }
    case 2: {
        uint64_t i = 0;

        __m128i FFs = _mm_set1_epi16(21845);
        __m128i ones = _mm_set1_epi16(0b11);
        __m128i zeroes = _mm_setzero_si128();

        for (; i + 16 <= bpr; i += 16) {
            __m128i in = _mm_loadu_si128((const __m128i *)src);

            // 16 bit lane
            __m128i in_lo = _mm_unpacklo_epi8(in, zeroes);
            __m128i in_hi = _mm_unpackhi_epi8(in, zeroes);

            // 16 bit lane for each bit depth
            __m128i in0_lo = _mm_mullo_epi16(FFs, _mm_and_si128(_mm_srli_epi16(in_lo, 0), ones));
            __m128i in1_lo = _mm_mullo_epi16(FFs, _mm_and_si128(_mm_srli_epi16(in_lo, 2), ones));
            __m128i in2_lo = _mm_mullo_epi16(FFs, _mm_and_si128(_mm_srli_epi16(in_lo, 4), ones));
            __m128i in3_lo = _mm_mullo_epi16(FFs, _mm_and_si128(_mm_srli_epi16(in_lo, 6), ones));

            __m128i in0_hi = _mm_mullo_epi16(FFs, _mm_and_si128(_mm_srli_epi16(in_hi, 0), ones));
            __m128i in1_hi = _mm_mullo_epi16(FFs, _mm_and_si128(_mm_srli_epi16(in_hi, 2), ones));
            __m128i in2_hi = _mm_mullo_epi16(FFs, _mm_and_si128(_mm_srli_epi16(in_hi, 4), ones));
            __m128i in3_hi = _mm_mullo_epi16(FFs, _mm_and_si128(_mm_srli_epi16(in_hi, 6), ones));

            // 16 bit & 8 lanes
            //  8 lanes will interleave by pair so 16 bit 8 lanes to 32 bit 4 lanes to 64 bit 2 lanes
            //  lane is a block of bytes like a chunk, after 8 lanes - > 4 lanes we have group some elements in order as
            //  we want storing elements still working like normal 64-bit does not means an element is now 64 bit it
            //  means 1 lane/chunk is 64 bit

            __m128i a0_lo = _mm_unpacklo_epi16(in0_lo, in1_lo); // 0 x 1 lo
            __m128i a1_lo = _mm_unpackhi_epi16(in0_lo, in1_lo); // 0 x 1 hi
            __m128i a2_lo = _mm_unpacklo_epi16(in2_lo, in3_lo); // 2 x 3 lo
            __m128i a3_lo = _mm_unpackhi_epi16(in2_lo, in3_lo); // 2 x 3 hi


            __m128i b0_lo = _mm_unpacklo_epi32(a0_lo, a2_lo); // 01 lo x 23 lo LO p0
            __m128i b1_lo = _mm_unpacklo_epi32(a1_lo, a3_lo); // 01 hi x 23 hi LO p2

            __m128i b2_lo = _mm_unpackhi_epi32(a0_lo, a2_lo); // 01 lo x 23 lo HI p1
            __m128i b3_lo = _mm_unpackhi_epi32(a1_lo, a3_lo); // 01 hi x 23 hi HI p3

            _mm_storeu_si128((__m128i *)(dest + 0 * 16), b0_lo);
            _mm_storeu_si128((__m128i *)(dest + 1 * 16), b2_lo);
            _mm_storeu_si128((__m128i *)(dest + 2 * 16), b1_lo);
            _mm_storeu_si128((__m128i *)(dest + 3 * 16), b3_lo);

            __m128i a0_hi = _mm_unpacklo_epi16(in0_hi, in1_hi); // 0 x 1 lo
            __m128i a1_hi = _mm_unpackhi_epi16(in0_hi, in1_hi); // 0 x 1 hi
            __m128i a2_hi = _mm_unpacklo_epi16(in2_hi, in3_hi); // 2 x 3 lo
            __m128i a3_hi = _mm_unpackhi_epi16(in2_hi, in3_hi); // 2 x 3 hi


            __m128i b0_hi = _mm_unpacklo_epi32(a0_hi, a2_lo); // 01 lo x 23 lo LO p0
            __m128i b1_hi = _mm_unpacklo_epi32(a1_hi, a3_lo); // 01 hi x 23 hi LO p2

            __m128i b2_hi = _mm_unpackhi_epi32(a0_hi, a2_hi); // 01 lo x 23 lo HI p1
            __m128i b3_hi = _mm_unpackhi_epi32(a1_hi, a3_hi); // 01 hi x 23 hi HI p3

            _mm_storeu_si128((__m128i *)(dest + 4 * 16), b0_hi);
            _mm_storeu_si128((__m128i *)(dest + 5 * 16), b2_hi);
            _mm_storeu_si128((__m128i *)(dest + 6 * 16), b1_hi);
            _mm_storeu_si128((__m128i *)(dest + 7 * 16), b3_hi);

            dest += 128;
            src += 16;
        }
        uint16_t *d = reinterpret_cast<uint16_t *>(dest);
        for (; i < bpr; i++) {
            for (int j = 3; j >= 0; j--) { *d++ = uint16_t(((*src >> (j * 2)) & 0b11)) * uint16_t(21845); }
            src++;
        }
        break;
    }
    case 4: {
        uint64_t i = 0;

        __m128i FFs = _mm_set1_epi16(4369);
        __m128i ones = _mm_set1_epi16(0b1111);
        __m128i zeroes = _mm_setzero_si128();

        for (; i + 16 <= bpr; i += 16) {
            __m128i in = _mm_loadu_si128((const __m128i *)src);

            // 16 bit lane
            __m128i in_lo = _mm_unpacklo_epi8(in, zeroes);
            __m128i in_hi = _mm_unpackhi_epi8(in, zeroes);

            // 16 bit lane for each bit depth
            __m128i in0_lo = _mm_mullo_epi16(FFs, _mm_and_si128(_mm_srli_epi16(in_lo, 0), ones));
            __m128i in1_lo = _mm_mullo_epi16(FFs, _mm_and_si128(_mm_srli_epi16(in_lo, 4), ones));

            __m128i in0_hi = _mm_mullo_epi16(FFs, _mm_and_si128(_mm_srli_epi16(in_hi, 0), ones));
            __m128i in1_hi = _mm_mullo_epi16(FFs, _mm_and_si128(_mm_srli_epi16(in_hi, 4), ones));

            // 16 bit & 8 lanes
            //  8 lanes will interleave by pair so 16 bit 8 lanes to 32 bit 4 lanes to 64 bit 2 lanes
            //  lane is a block of bytes like a chunk, after 8 lanes - > 4 lanes we have group some elements in order as
            //  we want storing elements still working like normal 64-bit does not means an element is now 64 bit it
            //  means 1 lane/chunk is 64 bit

            __m128i a0_lo = _mm_unpacklo_epi16(in0_lo, in1_lo); // 0 x 1 lo
            __m128i a1_lo = _mm_unpackhi_epi16(in0_lo, in1_lo); // 0 x 1 hi

            __m128i a0_hi = _mm_unpacklo_epi16(in0_hi, in1_hi); // 0 x 1 lo
            __m128i a1_hi = _mm_unpackhi_epi16(in0_hi, in1_hi); // 0 x 1 hi

            _mm_storeu_si128((__m128i *)(dest + 0 * 16), a0_lo);
            _mm_storeu_si128((__m128i *)(dest + 1 * 16), a1_lo);
            _mm_storeu_si128((__m128i *)(dest + 2 * 16), a0_hi);
            _mm_storeu_si128((__m128i *)(dest + 3 * 16), a1_hi);

            dest += 64;
            src += 16;
        }
        uint16_t *d = reinterpret_cast<uint16_t *>(dest);
        for (; i < bpr; i++) {
            for (int j = 1; j >= 0; j--) { *d++ = uint16_t(((*src >> (j * 4)) & 0b1111)) * uint16_t(4369); }
            src++;
        }
        break;
    }
    case 8: {

        uint64_t i = 0;

        for (; i + 16 <= bpr; i += 16) {
            __m128 in = _mm_loadu_si128((const __m128i *)src);

            __m128i out_lo = _mm_unpacklo_epi8(in, in);
            __m128i out_hi = _mm_unpackhi_epi8(in, in);

            _mm_storeu_si128((__m128i *)(dest + 0 * 16), out_lo);
            _mm_storeu_si128((__m128i *)(dest + 1 * 16), out_hi);

            dest += 32;
            src += 16;
        }

        for (; i + 2 <= bpr; i += 2) {
            *dest++ = *src;
            *dest++ = *src++;
        }
        break;
    }
    case 16: {

        uint64_t i = 0;
        __m256i zeroes = _mm256_setzero_si256();

        for (; i + 32 <= bpr; i += 32) {
            __m256i in = _mm256_loadu_si256((const __m256i *)src);
            __m256i out = _mm256_or_si256(_mm256_slli_epi16(in, 8), _mm256_srli_epi16(in, 8));

            _mm256_storeu_si256((__m256i *)dest, out);

            dest += 32;
            src += 32;
        }

        for (; i < bpr; i++) {
            *dest++ = *(src + 1);
            *dest++ = *(src);
            src += 2;
        }
        break;
    }
    }
}

static inline void decodeEngine(Image *result, std::ifstream &file, uint64_t file_size, uint8_t color_type, void (*cvtbit)(Image *)) noexcept {

    uint8_t worker[12];

    uint64_t data_len{};
    int plte_check{};
    int idat_check{};
    int tRNS_check{};
    bool iend_check = false;

    std::vector<std::vector<uint8_t>> palette;

    const uint64_t CHUNK = 65536; // 2^16
    do {
        file.read(reinterpret_cast<char *>(worker), 8);
        data_len = readuint32BigEdian(worker);

        // isIDAT
        if (memcmp(worker + 4, IDATsig, 4) == 0) {
            idat_check++;
            if (idat_check > 1) [[unlikely]] {
                result->state = 2;
                return;
            }
            zng_stream strm = {0};
            strm.zalloc = Z_NULL;
            strm.zfree = Z_NULL;
            strm.opaque = Z_NULL;
            strm.avail_in = 0;
            strm.next_in = Z_NULL;
            int ret = zng_inflateInit(&strm);
            if (ret != Z_OK) [[unlikely]] {
                result->state = 3;
                return;
            }
            uint64_t ai{CHUNK};
            uint64_t ftrker{};
            uint64_t intrker{};
            uint64_t offset{};
            uint64_t have{};
            uint64_t row_produced{};
            uint32_t crc{};
            std::vector<uint8_t> out(CHUNK);
            std::vector<uint8_t> in(CHUNK);

            bool ct3flag = false;
            if (color_type == 3) { ct3flag = true; }

            std::vector<uint8_t> line[2];
            line[0].resize(bpr);
            line[1].resize(bpr);
            scanline[0] = line[0].data();
            scanline[1] = line[1].data();

            // data_len, ++12, data_len,...
            do {
                data_len = readuint32BigEdian(worker);
                if (file_size <= data_len) [[unlikely]] {
                    result->state = 1;
                    return;
                }
                crc = zng_crc32(0U, Z_NULL, 0);
                crc = zng_crc32(crc, worker + 4, 4);
                if (data_len < ai) {
                    file.read(reinterpret_cast<char *>(in.data() + intrker), data_len);
                    ai -= data_len;
                    crc = zng_crc32(crc, in.data() + intrker, data_len);
                    intrker += data_len;
                } else {
                    file.read(reinterpret_cast<char *>(in.data() + intrker), ai);
                    crc = zng_crc32(crc, in.data() + intrker, ai);
                    ftrker = data_len - ai;
                    intrker += ai;
                    ai = 0;
                    strm.avail_in = intrker;
                    strm.next_in = in.data();
                    do {
                        do {
                            strm.avail_out = out.size() - offset;
                            strm.next_out = out.data() + offset;
                            ret = zng_inflate(&strm, Z_NO_FLUSH);
                            have = out.size() - strm.avail_out;
                            if (ret != Z_OK && ret != Z_STREAM_END) [[unlikely]] {
                                result->state = 3;
                                return;
                            }
                            row_produced = (have / (bpr + 1));
                            offset = (have - row_produced * (bpr + 1));
                            for (uint64_t i = 0; i < row_produced; i++) {

                                // defilter to scanline[1] from buffer and scanline[0]
                                if (!defilter(out.data() + i * (bpr + 1))) [[unlikely]] {
                                    result->state = 2;
                                    return;
                                }

                                // cvt8bit to the result buffer from scanline[1]
                                cvtbit(result);

                                // swap scanline[0] to scanline[1] for the next process
                                uint8_t *temp = scanline[0];
                                scanline[0] = scanline[1];
                                scanline[1] = temp;

                                imtrker++;
                            }
                            memmove(out.data(), out.data() + have - offset, offset);
                        } while (strm.avail_in > 0);
                        ai = CHUNK;
                        intrker = 0;
                        if (ftrker > ai) {
                            file.read(reinterpret_cast<char *>(in.data() + intrker), ai);
                            crc = zng_crc32(crc, in.data() + intrker, ai);
                            ftrker -= ai;
                            intrker += ai;
                            strm.avail_in = intrker;
                            strm.next_in = in.data();
                            ai = 0;
                        } else {
                            file.read(reinterpret_cast<char *>(in.data() + intrker), ftrker);
                            crc = zng_crc32(crc, in.data() + intrker, ftrker);
                            intrker += ftrker;
                            ai -= (ftrker);
                            ftrker = 0;
                        }
                    } while (ftrker != 0);
                }

                if (!file.read(reinterpret_cast<char *>(worker + 8), 4)) {
                    result->state = 1;
                    return;
                }

                if (readuint32BigEdian(worker + 8) != crc) {
                    result->state = 2;
                    return;
                }

                if (!file.read(reinterpret_cast<char *>(worker), 8)) {
                    result->state = 1;
                    return;
                }
            } while (memcmp(worker + 4, IDATsig, 4) == 0);
            strm.avail_in = intrker;
            strm.next_in = in.data();
            do {
                strm.avail_out = out.size() - offset;
                strm.next_out = out.data() + offset;
                ret = zng_inflate(&strm, Z_NO_FLUSH);
                have = out.size() - strm.avail_out;
                if (ret != Z_OK && ret != Z_STREAM_END) [[unlikely]] {
                    result->state = 3;
                    return;
                }
                row_produced = (have / (bpr + 1));
                offset = have - row_produced * (bpr + 1);
                for (uint64_t i = 0; i < row_produced; i++) {

                    // defilter to scanline[1] from buffer and scanline[0]
                    if (!defilter(out.data() + i * (bpr + 1))) [[unlikely]] {
                        result->state = 2;
                        return;
                    }

                    // cvt8bit to the result buffer from scanline[1]
                    cvtbit(result);

                    // swap scanline[0] to scanline[1] for the next process
                    uint8_t *temp = scanline[0];
                    scanline[0] = scanline[1];
                    scanline[1] = temp;

                    imtrker++;
                }
                memmove(out.data(), out.data() + have - offset, offset);
            } while (ret != Z_STREAM_END);
            // if (offset != 0) throw std::runtime_error("data loss");
            zng_inflateEnd(&strm);
            file.seekg((uint64_t)(file.tellg()) - 8);
        }

        // PLTE
        else if (memcmp(worker + 4, PLTEsig, 4) == 0) {
            plte_check++;
            if (plte_check > 1) [[unlikely]] {
                result->state = 2;
                return;
            }

            if (color_type == 3) {
                std::vector<uint8_t> plte(data_len);
                file.read(reinterpret_cast<char *>(plte.data()), data_len);

                uint32_t crc_ = zng_crc32(0U, Z_NULL, 0);
                crc_ = zng_crc32(crc_, worker + 4, 4);
                crc_ = zng_crc32(crc_, plte.data(), data_len);

                file.read(reinterpret_cast<char *>(worker + 8), 4);

                if (plte_check > 1 || (data_len) % 3 != 0 || !(data_len / 3 >= 0 && data_len / 3 <= 256) || readuint32BigEdian(worker + 8) != crc_) [[unlikely]] {
                    result->state = 2;
                    return;
                }

                uint64_t entries = data_len / 3;
                palette.resize(entries, std::vector<uint8_t>(4));

                uint64_t k{};
                for (uint64_t i = 0; i < entries; i++) {
                    palette[i][0] = plte[k++];
                    palette[i][1] = plte[k++];
                    palette[i][2] = plte[k++];
                    palette[i][3] = 255;
                }
            } else {
                file.seekg((uint64_t)(file.tellg()) + data_len + 4);
            }
        }

        // tRNS
        else if (memcmp(worker + 4, tRNSsig, 4) == 0) {
            tRNS_check++;
            if (tRNS_check > 1 || plte_check == 0 || plte_check == 0 || data_len > palette.size()) [[unlikely]] {
                result->state = 2;
                return;
            }

            if (color_type == 3) {
                uint64_t entries = palette.size();

                std::vector<uint8_t> trns(data_len);
                file.read(reinterpret_cast<char *>(trns.data()), data_len);

                uint32_t crc_ = zng_crc32(0U, Z_NULL, 0);
                crc_ = zng_crc32(crc_, worker + 4, 4);
                crc_ = zng_crc32(crc_, trns.data(), data_len);

                file.read(reinterpret_cast<char *>(worker + 8), 4);
                if (readuint32BigEdian(worker + 8) != crc_) {
                    result->state = 2;
                    return;
                }

                for (uint64_t i = 0; i < data_len; i++) { palette[i][3] = trns[i]; }
            } else {
                file.seekg((uint64_t)(file.tellg()) + data_len + 4);
            }
        }

        // isIEND
        else if (memcmp(worker + 4, IENDsig, 4) == 0) {
            if (idat_check == 0) [[unlikely]] {
                result->state = 2;
                return;
            }
            iend_check = true;
            break;
        }

        // else = skip
        else {
            file.seekg((uint64_t)(file.tellg()) + data_len + 4);
        }
    } while ((uint64_t)(file.tellg()) <= (file_size - 12));

    if (!iend_check) {
        result->state = 2;
        return;
    }

    if (color_type == 3) {
        uint8_t *im = result->buffer.data();

        for (uint64_t i = 0; i < (uint64_t)(result->height) * (uint64_t)(result->width); i++) {
            uint8_t index = *im;
            *im++ = palette[index][0];
            *im++ = palette[index][1];
            *im++ = palette[index][2];
            *im++ = palette[index][3];
        }
    }

    return;
}

// non color type 3, bit depth 8, decoder
static inline void nonct3bd8decoder(Image *result, std::ifstream &file, uint64_t file_size) noexcept {

    uint8_t worker[12];

    uint64_t data_len{};
    int idat_check{};
    bool iend_check = false;

    const uint64_t CHUNK = 65536; // 2^16
    do {
        file.read(reinterpret_cast<char *>(worker), 8);
        data_len = readuint32BigEdian(worker);

        // isIDAT
        if (memcmp(worker + 4, IDATsig, 4) == 0) {
            idat_check++;
            if (idat_check > 1) {
                result->state = 2;
                return;
            }
            zng_stream strm = {0};
            strm.zalloc = Z_NULL;
            strm.zfree = Z_NULL;
            strm.opaque = Z_NULL;
            strm.avail_in = 0;
            strm.next_in = Z_NULL;
            int ret = zng_inflateInit(&strm);
            if (ret != Z_OK) {
                result->state = 3;
                return;
            }
            uint64_t ai{CHUNK};
            uint64_t ftrker{};
            uint64_t intrker{};
            uint64_t offset{};
            uint64_t have{};
            uint64_t row_produced{};
            uint32_t crc{};
            std::vector<uint8_t> out(CHUNK);
            std::vector<uint8_t> in(CHUNK);

            // data_len, ++12, data_len,...
            do {
                data_len = readuint32BigEdian(worker);
                if (file_size <= data_len) {
                    result->state = 1;
                    return;
                }
                crc = zng_crc32(0U, Z_NULL, 0);
                crc = zng_crc32(crc, worker + 4, 4);
                if (data_len < ai) {
                    file.read(reinterpret_cast<char *>(in.data() + intrker), data_len);
                    ai -= data_len;
                    crc = zng_crc32(crc, in.data() + intrker, data_len);
                    intrker += data_len;
                } else {
                    file.read(reinterpret_cast<char *>(in.data() + intrker), ai);
                    crc = zng_crc32(crc, in.data() + intrker, ai);
                    ftrker = data_len - ai;
                    intrker += ai;
                    ai = 0;
                    strm.avail_in = intrker;
                    strm.next_in = in.data();
                    do {
                        do {
                            strm.avail_out = out.size() - offset;
                            strm.next_out = out.data() + offset;
                            ret = zng_inflate(&strm, Z_NO_FLUSH);
                            have = out.size() - strm.avail_out;
                            if (ret != Z_OK && ret != Z_STREAM_END) [[unlikely]] {
                                result->state = 3;
                                return;
                            }
                            row_produced = (have / (bpr + 1));
                            offset = (have - row_produced * (bpr + 1));
                            for (uint64_t i = 0; i < row_produced; i++) {

                                scanline[0] = result->buffer.data() + (imtrker - 1) * bpr;
                                scanline[1] = result->buffer.data() + imtrker * bpr;

                                // defilter to scanline[1] from buffer and scanline[0]
                                if (!defilter(out.data() + i * (bpr + 1))) [[unlikely]] {
                                    result->state = 2;
                                    return;
                                }

                                imtrker++;
                            }
                            memmove(out.data(), out.data() + have - offset, offset);
                        } while (strm.avail_in > 0);
                        ai = CHUNK;
                        intrker = 0;
                        if (ftrker > ai) {
                            file.read(reinterpret_cast<char *>(in.data() + intrker), ai);
                            crc = zng_crc32(crc, in.data() + intrker, ai);
                            ftrker -= ai;
                            intrker += ai;
                            strm.avail_in = intrker;
                            strm.next_in = in.data();
                            ai = 0;
                        } else {
                            file.read(reinterpret_cast<char *>(in.data() + intrker), ftrker);
                            crc = zng_crc32(crc, in.data() + intrker, ftrker);
                            intrker += ftrker;
                            ai -= (ftrker);
                            ftrker = 0;
                        }
                    } while (ftrker != 0);
                }

                if (!file.read(reinterpret_cast<char *>(worker + 8), 4)) {
                    result->state = 1;
                    return;
                }

                if (readuint32BigEdian(worker + 8) != crc) {
                    result->state = 2;
                    return;
                }

                if (!file.read(reinterpret_cast<char *>(worker), 8)) {
                    result->state = 1;
                    return;
                }
            } while (memcmp(worker + 4, IDATsig, 4) == 0);
            strm.avail_in = intrker;
            strm.next_in = in.data();
            do {
                strm.avail_out = out.size() - offset;
                strm.next_out = out.data() + offset;
                ret = zng_inflate(&strm, Z_NO_FLUSH);
                have = out.size() - strm.avail_out;
                if (ret != Z_OK && ret != Z_STREAM_END) [[unlikely]] {
                    result->state = 3;
                    return;
                }
                row_produced = (have / (bpr + 1));
                offset = have - row_produced * (bpr + 1);
                for (uint64_t i = 0; i < row_produced; i++) {

                    scanline[0] = result->buffer.data() + (imtrker - 1) * bpr;
                    scanline[1] = result->buffer.data() + imtrker * bpr;

                    // defilter to scanline[1] from buffer and scanline[0]
                    if (!defilter(out.data() + i * (bpr + 1))) [[unlikely]] {
                        result->state = 2;
                        return;
                    }

                    imtrker++;
                }
                memmove(out.data(), out.data() + have - offset, offset);
            } while (ret != Z_STREAM_END);
            // if (offset != 0) throw std::runtime_error("data loss");
            zng_inflateEnd(&strm);
            file.seekg((uint64_t)(file.tellg()) - 8);
        }

        // isIEND
        else if (memcmp(worker + 4, IENDsig, 4) == 0) {
            if (idat_check == 0) {
                result->state = 2;
                return;
            }
            iend_check = true;
            break;
        }

        // else = skip
        else {
            file.seekg((uint64_t)(file.tellg()) + data_len + 4);
        }
    } while ((uint64_t)(file.tellg()) <= (file_size - 12));

    if (!iend_check) {
        result->state = 2;
        return;
    }

    return;
}

static inline uint64_t abs__(uint64_t x) noexcept {
    uint64_t y = x >> 63;
    return ((x ^ -y) + y);
}

// x must >= 0
static inline uint64_t ceil__(double x) noexcept {
    uint64_t a = uint64_t(x);
    return a + (x > a);
}