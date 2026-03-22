#include "imsave.hpp"
#include <cstdint>
#include <cstring>
#include <fstream>
#include <immintrin.h>
#include <string>
#include <zlib-ng.h>




struct [[gnu::packed]] IHDR {
    uint32_t width;
    uint32_t height;
    uint8_t bit_depth;
    uint8_t color_type;
    uint8_t compression_method;
    uint8_t filter_method;
    uint8_t interlace_method;
    uint32_t channels;
};




// static functions

static inline void FileInit(std::ofstream &file, IHDR &stream);

static inline uint8_t get_color_type(const uint8_t channels) noexcept;

static inline uint32_t swap_edian32bit(uint32_t x) noexcept;

static inline int abs__(int x) noexcept; // branchless absolute value

static inline int encodeEngine(void *data, std::ofstream &file) noexcept; // as its name

static inline void bdcvt(void *Dest, void *Source) noexcept; // convert the data bit depth to encode









// static constants
static constexpr unsigned char pngsig[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
static constexpr unsigned char ihdrsig[4] = {'I', 'H', 'D', 'R'};
static constexpr unsigned char IDATsig[4] = {'I', 'D', 'A', 'T'};
static constexpr unsigned char IENDsig[12] = {0, 0, 0, 0, 'I', 'E', 'N', 'D', 0xAE, 0x42, 0x60, 0x82};
const int level = 4;
const int CHUNK = uint16_t(-1); // 65536








// static variables
IHDR stream;











// main function imsave:

int png_imsave(const std::string path, void *data, const uint32_t width, const uint32_t height, const uint32_t channels, const uint8_t bit_depth) noexcept {

    if (height == 0 || width == 0 || channels == 0) { return -1; }
    switch (bit_depth) {
    case 1: break;
    case 2: break;
    case 4: break;
    case 8: break;
    case 16: break;
    default: return -1;
    }

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) { return 1; }

    memset(&stream, 0, sizeof(stream));
    stream.width = swap_edian32bit(width);
    stream.height = swap_edian32bit(height);
    stream.color_type = get_color_type(channels);
    stream.bit_depth = bit_depth;
    if (stream.color_type == uint8_t(-1)) { return -1; }
    FileInit(file, stream);
    stream.width = width;
    stream.height = height;
    stream.channels = channels;

    return encodeEngine(data, file);
}

















// static functions definition:

static inline void FileInit(std::ofstream &file, IHDR &stream) {
    uint32_t crc_ = zng_crc32(0, Z_NULL, 0);
    uint32_t data_len = swap_edian32bit(13);
    file.write(reinterpret_cast<const char *>(pngsig), 8);
    file.write(reinterpret_cast<char *>(&data_len), 4);
    file.write(reinterpret_cast<const char *>(ihdrsig), 4);
    crc_ = zng_crc32(crc_, ihdrsig, 4);
    file.write(reinterpret_cast<char *>(&stream), 13);
    crc_ = zng_crc32(crc_, reinterpret_cast<uint8_t *>(&stream), 13);
    crc_ = swap_edian32bit(crc_);
    file.write(reinterpret_cast<char *>(&crc_), 4);
}

static inline uint8_t get_color_type(const uint8_t channels) noexcept {
    uint8_t color_type;
    switch (channels) {
    case 1: {
        color_type = 0;
        break;
    }
    case 2: {
        color_type = 4;
        break;
    }
    case 3: {
        color_type = 2;
        break;
    }
    case 4: {
        color_type = 6;
        break;
    }
    default: return uint8_t(-1);
    }
    return color_type;
}

static inline uint32_t swap_edian32bit(uint32_t x) noexcept {
    uint32_t ret = 0;
    uint8_t *a = reinterpret_cast<uint8_t *>(&x) + 3;
    uint8_t *b = reinterpret_cast<uint8_t *>(&ret);
    for (uint64_t i = 0; i < 4; i++) { *b++ = *a--; }
    return ret;
}

static inline int abs__(int x) noexcept {
    int y = x >> 31;
    return ((x ^ -y) + y);
}


static inline void bdcvt(void *Dest, void *Source) noexcept {
    uint8_t *dest = reinterpret_cast<uint8_t *>(Dest);
    uint8_t *src = reinterpret_cast<uint8_t *>(Source);
    const uint64_t size = (uint64_t)(stream.height) * (uint64_t)(stream.width) * (uint64_t)(stream.channels) * (uint64_t)(1 + (stream.bit_depth == 16));

    switch (stream.bit_depth) {
    case 1: {
        __m256i mask1 = _mm256_setr_epi8(1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0);
        __m256i mask2 = _mm256_setr_epi8(0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0);
        __m256i mask3 = _mm256_setr_epi8(0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0);
        __m256i mask4 = _mm256_setr_epi8(0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0);
        __m256i mask5 = _mm256_setr_epi8(0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0);
        __m256i mask6 = _mm256_setr_epi8(0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0);
        __m256i mask7 = _mm256_setr_epi8(0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0);
        __m256i mask8 = _mm256_setr_epi8(0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1);
        __m256i extract = _mm256_setr_epi8(0, 8, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 0, 8, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1);


        __m256i z = _mm256_setzero_si256();
        uint64_t i = 0;
        for (; i + 32 <= size; i += 32) {
            __m256i in = _mm256_loadu_si256((const __m256i *)src);

            in = _mm256_and_si256(in, _mm256_set1_epi8(1)); // mask the last 1 bit of each byte

            __m256i bit1 = _mm256_slli_si256(_mm256_slli_epi64(_mm256_blendv_epi8(z, in, mask1), 7), 0);
            __m256i bit2 = _mm256_slli_si256(_mm256_slli_epi64(_mm256_blendv_epi8(z, in, mask2), 6), 1);
            __m256i bit3 = _mm256_slli_si256(_mm256_slli_epi64(_mm256_blendv_epi8(z, in, mask3), 5), 2);
            __m256i bit4 = _mm256_slli_si256(_mm256_slli_epi64(_mm256_blendv_epi8(z, in, mask4), 4), 3);
            __m256i bit5 = _mm256_slli_si256(_mm256_slli_epi64(_mm256_blendv_epi8(z, in, mask5), 3), 4);
            __m256i bit6 = _mm256_slli_si256(_mm256_slli_epi64(_mm256_blendv_epi8(z, in, mask6), 2), 5);
            __m256i bit7 = _mm256_slli_si256(_mm256_slli_epi64(_mm256_blendv_epi8(z, in, mask7), 1), 6);
            __m256i bit8 = _mm256_slli_si256(_mm256_slli_epi64(_mm256_blendv_epi8(z, in, mask8), 0), 7);

            __m256i out = _mm256_or_si256(_mm256_or_si256(_mm256_or_si256(_mm256_or_si256(_mm256_or_si256(_mm256_or_si256(_mm256_or_si256(bit1, bit2), bit3), bit4), bit5), bit6), bit7), bit8);

            out = _mm256_shuffle_epi8(out, extract);

            *(uint16_t *)dest = _mm256_extract_epi16(out, 0);
            dest += 2;
            *(uint16_t *)dest = _mm256_extract_epi16(out, 8);
            dest += 2;
            src += 32;
        }
        for (; i < size; i++) {
            *dest = 0;
            for (uint64_t j = 7; j >= 0; j++) { *dest |= (((*src++) & 1) << j); }
            dest++;
        }
        break;
    }
    case 2: {
        __m256i mask1 = _mm256_setr_epi8(1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0);
        __m256i mask2 = _mm256_setr_epi8(0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0);
        __m256i mask3 = _mm256_setr_epi8(0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0);
        __m256i mask4 = _mm256_setr_epi8(0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1);
        __m256i extract = _mm256_setr_epi8(0, 4, 8, 12, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 0, 4, 8, 12, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1);

        __m256i z = _mm256_setzero_si256();
        uint64_t i = 0;
        for (; i + 32 <= size; i += 32) {
            __m256i in = _mm256_loadu_si256((const __m256i *)src);

            in = _mm256_and_si256(in, _mm256_set1_epi8(0b11)); // mask the last 2 bit of each byte

            __m256i bit1 = _mm256_slli_si256(_mm256_slli_epi32(_mm256_blendv_epi8(z, in, mask1), 3), 0);
            __m256i bit2 = _mm256_slli_si256(_mm256_slli_epi32(_mm256_blendv_epi8(z, in, mask2), 2), 1);
            __m256i bit3 = _mm256_slli_si256(_mm256_slli_epi32(_mm256_blendv_epi8(z, in, mask3), 1), 2);
            __m256i bit4 = _mm256_slli_si256(_mm256_slli_epi32(_mm256_blendv_epi8(z, in, mask4), 0), 3);

            __m256i out = _mm256_or_si256(_mm256_or_si256(_mm256_or_si256(bit1, bit2), bit3), bit4);

            out = _mm256_shuffle_epi8(out, extract);

            *(uint32_t *)dest = _mm256_extract_epi32(out, 0);
            dest += 4;
            *(uint32_t *)dest = _mm256_extract_epi32(out, 4);
            dest += 4;
            src += 32;
        }
        for (; i < size; i++) {
            *dest = 0;
            for (uint64_t j = 3; j >= 0; j++) { *dest |= (((*src++) & 0b11) << j); }
            dest++;
        }
        break;
    }
    case 4: {
        __m256i mask1 = _mm256_setr_epi8(1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0);
        __m256i mask2 = _mm256_setr_epi8(0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1);
        __m256i extract = _mm256_setr_epi8(0, 2, 4, 6, 8, 10, 12, 14, -1, -1, -1, -1, -1, -1, -1, -1, 0, 2, 4, 6, 8, 10, 12, 14, -1, -1, -1, -1, -1, -1, -1, -1);

        __m256i z = _mm256_setzero_si256();
        uint64_t i = 0;
        for (; i + 32 <= size; i += 32) {
            __m256i in = _mm256_loadu_si256((const __m256i *)src);

            in = _mm256_and_si256(in, _mm256_set1_epi8(0b1111)); // mask the last 4 bit of each byte

            __m256i bit1 = _mm256_slli_si256(_mm256_slli_epi32(_mm256_blendv_epi8(z, in, mask1), 1), 0);
            __m256i bit2 = _mm256_slli_si256(_mm256_slli_epi32(_mm256_blendv_epi8(z, in, mask2), 0), 1);

            __m256i out = _mm256_or_si256(bit1, bit2);

            out = _mm256_shuffle_epi8(out, extract);

            *(uint64_t *)dest = _mm256_extract_epi64(out, 0);
            dest += 8;
            *(uint64_t *)dest = _mm256_extract_epi64(out, 2);
            dest += 8;
            src += 32;
        }
        for (; i < size; i++) {
            *dest = 0;
            for (uint64_t j = 1; j >= 0; j++) { *dest |= (((*src++) & 0b1111) << j); }
            dest++;
        }
        break;
    }
    case 8: {
        memcpy(Dest, Source, size);
        break;
    }
    case 16: {

        uint64_t i = 0;
        for (; i + 32 <= size; i += 32) {
            __m256i in = _mm256_loadu_si256((const __m256i *)src);

            __m256i out = _mm256_or_si256(_mm256_slli_epi16(in, 8), _mm256_srli_epi16(in, 8));

            _mm256_storeu_si256((__m256i *)dest, out);

            dest += 32;
            src += 32;
        }
        for (; i < size; i++) {
            *dest++ = *(src + 1);
            *dest++ = *(src);
            src += 2;
        }
        break;
    }
    }
}


// return 0 success
// return -1 deflate fail
// return 1 file write fail
static inline int encodeEngine(void *data, std::ofstream &file) noexcept {

    const uint64_t width = stream.width;
    const uint64_t height = stream.height;
    const uint64_t channels = stream.channels;
    const uint64_t bit_depth = stream.bit_depth;


    zng_stream strm{};
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    int ret = zng_deflateInit2(&strm, level, Z_DEFLATED, 15, 8, Z_FILTERED);
    if (ret != Z_OK) [[unlikely]] { return false; }

    const uint64_t bpp = channels * (uint64_t)(1 + (stream.bit_depth == 16)); // bytes per row
    const uint64_t bpr = (width * channels * bit_depth) / 8;                  // bytes per row,
                                                                              // it's the bpr after the bit depth conversion

    uint8_t out[CHUNK + 12];
    memcpy(out + 4, IDATsig, 4);

    uint8_t *image;
    if (stream.bit_depth == 8) {
        image = reinterpret_cast<uint8_t *>(data);
    } else if (stream.bit_depth == 16) {
        bdcvt(data, data);
        image = reinterpret_cast<uint8_t *>(data);
    } else {
        image = new uint8_t[height * bpr];
        bdcvt(image, data);
    }

    uint64_t have{}, data_len{};
    uint8_t none[bpr + 1], sub[bpr + 1], up[bpr + 1], avg[bpr + 1], paeth[bpr + 1];
    none[0] = 0;
    sub[0] = 1;
    up[0] = 2;
    avg[0] = 3;
    paeth[0] = 4;
    uint8_t filter = 1;

    strm.avail_out = CHUNK;
    for (uint64_t i = 0; i < height; i++) {
        uint64_t noneS{}, subS{}, upS{}, paethS{}, avgS{};
        if (i == 0) [[unlikely]] {
            uint8_t *raw = image;
            uint8_t *n = none + 1;
            uint8_t *s = sub + 1;
            uint8_t *u = up + 1;
            uint8_t *a = avg + 1;
            uint8_t *p = paeth + 1;
            for (uint64_t j = 0; j < bpp; j++) {
                noneS += *n++ = *raw;
                subS += *s++ = *raw;
                upS += *u++ = *raw;
                avgS += *a++ = *raw;
                paethS += *p++ = *raw;
                raw++;
            }
            for (uint64_t j = bpp; j < bpr; j++) {
                noneS += *n++ = *raw;
                subS += *s++ = *raw - *(raw - bpp);
                upS += *u++ = *raw;
                avgS += *a++ = *raw - (*(raw - bpp) >> 1);
                paethS += *p++ = *raw - *(raw - bpp);
                raw++;
            }
        } else [[likely]] {
            uint8_t *raw = image;
            uint8_t *b = image - bpr;
            uint8_t *f0 = none + 1;
            uint8_t *f1 = sub + 1;
            uint8_t *f2 = up + 1;
            uint8_t *f3 = avg + 1;
            uint8_t *f4 = paeth + 1;
            uint64_t j = 0;
            for (; j < bpp; j++) {
                noneS += *f0++ = *raw;
                subS += *f1++ = *raw;
                upS += *f2++ = *raw - *b;
                avgS += *f3++ = *raw - (*b >> 1);
                paethS += *f4++ = *raw - *b;
                b++;
                raw++;
            }
            uint8_t *a = raw - bpp;
            uint8_t *c = b - bpp;

            __m256i noneSum = _mm256_setzero_si256();
            __m256i subSum = _mm256_setzero_si256();
            __m256i upSum = _mm256_setzero_si256();
            __m256i avgSum = _mm256_setzero_si256();
            __m256i paethSum = _mm256_setzero_si256();
            __m256i zero = _mm256_setzero_si256();
            __m256i all1 = _mm256_set1_epi16(-1);


            for (; j + 32 <= bpr; j += 32) {

                __m256i r = _mm256_loadu_si256((const __m256i *)(raw));
                __m256i va = _mm256_loadu_si256((const __m256i *)(a));
                __m256i vb = _mm256_loadu_si256((const __m256i *)(b));
                __m256i vc = _mm256_loadu_si256((const __m256i *)(c));


                __m256i va_lo = _mm256_unpacklo_epi8(va, zero);
                __m256i va_hi = _mm256_unpackhi_epi8(va, zero);

                __m256i vb_lo = _mm256_unpacklo_epi8(vb, zero);
                __m256i vb_hi = _mm256_unpackhi_epi8(vb, zero);

                __m256i vc_lo = _mm256_unpacklo_epi8(vc, zero);
                __m256i vc_hi = _mm256_unpackhi_epi8(vc, zero);

                __m256i p_lo = _mm256_add_epi16(va_lo, _mm256_sub_epi16(vb_lo, vc_lo));
                __m256i p_hi = _mm256_add_epi16(va_hi, _mm256_sub_epi16(vb_hi, vc_hi));

                __m256i pa_lo = _mm256_abs_epi16(_mm256_sub_epi16(p_lo, va_lo));
                __m256i pa_hi = _mm256_abs_epi16(_mm256_sub_epi16(p_hi, va_hi));

                __m256i pb_lo = _mm256_abs_epi16(_mm256_sub_epi16(p_lo, vb_lo));
                __m256i pb_hi = _mm256_abs_epi16(_mm256_sub_epi16(p_hi, vb_hi));

                __m256i pc_lo = _mm256_abs_epi16(_mm256_sub_epi16(p_lo, vc_lo));
                __m256i pc_hi = _mm256_abs_epi16(_mm256_sub_epi16(p_hi, vc_hi));

                __m256i pa_le_pb_lo = _mm256_xor_si256(_mm256_cmpgt_epi16(pa_lo, pb_lo), all1);
                __m256i pa_le_pb_hi = _mm256_xor_si256(_mm256_cmpgt_epi16(pa_hi, pb_hi), all1);

                __m256i pa_le_pc_lo = _mm256_xor_si256(_mm256_cmpgt_epi16(pa_lo, pc_lo), all1);
                __m256i pa_le_pc_hi = _mm256_xor_si256(_mm256_cmpgt_epi16(pa_hi, pc_hi), all1);

                __m256i pb_le_pc_lo = _mm256_xor_si256(_mm256_cmpgt_epi16(pb_lo, pc_lo), all1);
                __m256i pb_le_pc_hi = _mm256_xor_si256(_mm256_cmpgt_epi16(pb_hi, pc_hi), all1);

                __m256i cond1_lo = _mm256_and_si256(pa_le_pb_lo, pa_le_pc_lo);
                __m256i cond1_hi = _mm256_and_si256(pa_le_pb_hi, pa_le_pc_hi);

                __m256i cond2_lo = _mm256_andnot_si256(cond1_lo, pb_le_pc_lo);
                __m256i cond2_hi = _mm256_andnot_si256(cond1_hi, pb_le_pc_hi);

                __m256i d_lo = _mm256_blendv_epi8(vc_lo, va_lo, cond1_lo);
                __m256i d_hi = _mm256_blendv_epi8(vc_hi, va_hi, cond1_hi);

                d_lo = _mm256_blendv_epi8(d_lo, vb_lo, cond2_lo);
                d_hi = _mm256_blendv_epi8(d_hi, vb_hi, cond2_hi);

                __m256i d = _mm256_packus_epi16(d_lo, d_hi);


                __m256i tavg_lo = _mm256_srli_epi16(_mm256_add_epi16(va_lo, vb_lo), 1);
                __m256i tavg_hi = _mm256_srli_epi16(_mm256_add_epi16(va_hi, vb_hi), 1);
                __m256i tavg = _mm256_packus_epi16(tavg_lo, tavg_hi);


                __m256i vsub = _mm256_sub_epi8(r, va);
                __m256i vup = _mm256_sub_epi8(r, vb);
                __m256i vavg = _mm256_sub_epi8(r, tavg);
                __m256i vpaeth = _mm256_sub_epi8(r, d);

                noneSum = _mm256_add_epi64(noneSum, _mm256_sad_epu8(r, zero));
                subSum = _mm256_add_epi64(subSum, _mm256_sad_epu8(r, va));
                upSum = _mm256_add_epi64(upSum, _mm256_sad_epu8(r, vb));
                avgSum = _mm256_add_epi64(avgSum, _mm256_sad_epu8(r, tavg));
                paethSum = _mm256_add_epi64(paethSum, _mm256_sad_epu8(r, d));

                _mm256_storeu_si256((__m256i *)(f0), r);
                _mm256_storeu_si256((__m256i *)(f1), vsub);
                _mm256_storeu_si256((__m256i *)(f2), vup);
                _mm256_storeu_si256((__m256i *)(f3), vavg);
                _mm256_storeu_si256((__m256i *)(f4), vpaeth);

                a += 32;
                b += 32;
                c += 32;
                f0 += 32;
                f1 += 32;
                f2 += 32;
                f3 += 32;
                f4 += 32;
                raw += 32;
            }

            alignas(32) uint64_t tmp[4]{};
            _mm256_store_si256((__m256i *)tmp, noneSum);
            for (uint64_t u = 0; u < 4; u++) { noneS += tmp[u]; }
            _mm256_store_si256((__m256i *)tmp, subSum);
            for (uint64_t u = 0; u < 4; u++) { subS += tmp[u]; }
            _mm256_store_si256((__m256i *)tmp, upSum);
            for (uint64_t u = 0; u < 4; u++) { upS += tmp[u]; }
            _mm256_store_si256((__m256i *)tmp, avgSum);
            for (uint64_t u = 0; u < 4; u++) { avgS += tmp[u]; }
            _mm256_store_si256((__m256i *)tmp, paethSum);
            for (uint64_t u = 0; u < 4; u++) { paethS += tmp[u]; }

            for (; j < bpr; j++) {
                int p = (int)*a + (int)*b - (int)*c;
                int pa = abs__(p - (int)*a);
                int pb = abs__(p - (int)*b);
                int pc = abs__(p - (int)*c);
                uint8_t d;
                if (pa <= pb && pa <= pc) {
                    d = *a;
                } else if (pb <= pc) {
                    d = *b;
                } else {
                    d = *c;
                }
                noneS += *f0++ = *raw;
                subS += *f1++ = *raw - *a;
                upS += *f2++ = *raw - *b;
                avgS += *f3++ = *raw - ((*a + *b) >> 1);
                paethS += *f4++ = *raw - d;
                a++;
                b++;
                c++;
                raw++;
            }
        }

        // uint64_t win = min__(min__(min__(min__(noneS, subS), upS), avgS), paethS);
        if (noneS <= subS && noneS <= upS && noneS <= avgS && noneS <= paethS) {
            filter = 0;
            strm.next_in = none;
        } else if (subS <= upS && subS <= avgS && subS <= paethS) {
            filter = 1;
            strm.next_in = sub;
        } else if (upS <= avgS && upS <= paethS) {
            filter = 2;
            strm.next_in = up;
        } else if (avgS <= paethS) {
            filter = 3;
            strm.next_in = avg;
        } else {
            filter = 4;
            strm.next_in = paeth;
        }

        strm.avail_in = bpr + 1;
        do {
            strm.next_out = out + 8 + have;
            ret = zng_deflate(&strm, Z_NO_FLUSH);
            if (ret != Z_OK) [[unlikely]] { return -1; }
            have = CHUNK - strm.avail_out;
            if (strm.avail_out == 0) {
                data_len = (uint32_t)(have);
                data_len = swap_edian32bit(data_len);
                memcpy(out, &data_len, 4);
                uint32_t crc_ = zng_crc32(0, Z_NULL, 0);
                crc_ = zng_crc32(crc_, out + 4, 4);
                crc_ = zng_crc32(crc_, out + 8, have);
                crc_ = swap_edian32bit(crc_);
                memcpy(out + 8 + have, &crc_, 4);
                if (!file.write(reinterpret_cast<char *>(out), 8 + have + 4)) { return 1; }
                strm.avail_out = CHUNK;
                have = 0;
            }
        } while (strm.avail_in > 0);

        image += bpr;
    }

    do {
        strm.next_out = out + 8 + have;
        ret = zng_deflate(&strm, Z_FINISH);
        if (ret != Z_OK && ret != Z_STREAM_END) [[unlikely]] { return -1; }
        have = CHUNK - strm.avail_out;
        if (strm.avail_out == 0) {
            data_len = (uint32_t)(have);
            data_len = swap_edian32bit(data_len);
            memcpy(out, &data_len, 4);
            uint32_t crc_ = zng_crc32(0, Z_NULL, 0);
            crc_ = zng_crc32(crc_, out + 4, 4 + have);
            crc_ = swap_edian32bit(crc_);
            memcpy(out + 8 + have, &crc_, 4);
            if (!file.write(reinterpret_cast<char *>(out), 8 + have + 4)) { return 1; }
            strm.avail_out = CHUNK;
            have = 0;
        }
    } while (ret != Z_STREAM_END);

    data_len = (uint32_t)(have);
    data_len = swap_edian32bit(data_len);
    memcpy(out, &data_len, 4);
    uint32_t crc_ = zng_crc32(0, Z_NULL, 0);
    crc_ = zng_crc32(crc_, out + 4, 4 + have);
    crc_ = swap_edian32bit(crc_);
    memcpy(out + 8 + have, &crc_, 4);
    if (!file.write(reinterpret_cast<char *>(out), 8 + have + 4)) { return 1; }
    zng_deflateEnd(&strm);

    if (!file.write(reinterpret_cast<const char *>(IENDsig), 12)) { return 1; }

    if (stream.bit_depth == 16) {
        bdcvt(data, data);
    } else if (stream.bit_depth != 8) {
        delete[] image;
    }

    return 0;
}

/*
                switch (filter)
                {
                case 0:
                {
                    in[0] = 0Ui8;
                    std::memcpy(in.data() + 1, image + i * bpr, in.size() - 1);
                    break;
                }
                case 1:
                {
                    in[0] = 1Ui8;
                    uint8_t* raw = image + i * bpr;
                    uint8_t* dst = in.data() + 1;
                    std::memcpy(dst, raw, bpp);
                    dst += bpp;
                    raw += bpp;
                    for (int64_t j = bpp; j < bpr; j++) {
                        *dst++ = (*raw - *(raw - bpp));
                        raw++;
                    }
                    break;
                }
                case 2:
                {
                    in[0] = 2Ui8;
                    if (i == 0) [[unlikely]] std::memcpy(in.data() + 1, image, in.size() - 1);
                    else [[likely]] {
                        uint8_t* raw = image + i * bpr;
                        uint8_t* b = image + (i - 1) * bpr;
                        uint8_t* dst = in.data() + 1;
                        for (int64_t j = 0; j < bpr; j++) *dst++ = *raw++ - *b++;
                    }
                    break;
                }
                case 3:
                {
                    in[0] = 3Ui8;
                    if (i == 0) [[unlikely]] {
                        uint8_t* raw = image;
                        uint8_t* dst = in.data() + 1;
                        std::memcpy(dst, raw, bpp);
                        dst += bpp;
                        raw += bpp;
                        for (int64_t j = bpp; j < bpr; j++) {
                            *dst++ = *raw - *(raw - bpp) / 2Ui8;
                            raw++;
                        }
                    }
                    else [[likely]] {
                        uint8_t* raw = image + i * bpr;
                        uint8_t* b = image + (i - 1) * bpr;
                        uint8_t* dst = in.data() + 1;
                        for (int64_t j = 0; j < bpp; j++) *dst++ = *raw++ - *b++ / 2;
                        uint8_t* a = raw - bpp;
                        for (int64_t j = bpp; j < bpr; j++) *dst++ = *raw++ - (*a++ + *b++) / 2;
                    }
                    break;
                }
                case 4:
                {
                    in[0] = 4Ui8;
                    if (i == 0) [[unlikely]] {
                        uint8_t* raw = image;
                        uint8_t* dst = in.data() + 1;
                        std::memcpy(dst, raw, bpp);
                        dst += bpp;
                        raw += bpp;
                        for (int64_t j = bpp; j < bpr; j++) {
                            *dst++ = *raw - *(raw - bpp);
                            raw++;
                        }
                    }
                    else [[likely]] {
                        uint8_t* raw = image + i * bpr;
                        uint8_t* b = image + (i - 1) * bpr;
                        uint8_t* dst = in.data() + 1;
                        for (int64_t j = 0; j < bpp; j++) *dst++ = *raw++ - *b++;
                        uint8_t* a = raw - bpp;
                        uint8_t* c = b - bpp;
                        int64_t j = bpp;
                        for (; j + 32i64 <= bpr; j += 32i64)
                        {
                            __m256i a8 = _mm256_loadu_si256((const __m256i*)(a));
                            __m256i b8 = _mm256_loadu_si256((const __m256i*)(b));
                            __m256i c8 = _mm256_loadu_si256((const __m256i*)(c));

                            __m256i zero = _mm256_setzero_si256();
                            __m256i all1 = _mm256_set1_epi16(-1);

                            __m256i a16_lo = _mm256_unpacklo_epi8(a8, zero);
                            __m256i a16_hi = _mm256_unpackhi_epi8(a8, zero);

                            __m256i b16_lo = _mm256_unpacklo_epi8(b8, zero);
                            __m256i b16_hi = _mm256_unpackhi_epi8(b8, zero);

                            __m256i c16_lo = _mm256_unpacklo_epi8(c8, zero);
                            __m256i c16_hi = _mm256_unpackhi_epi8(c8, zero);

                            __m256i p_lo = _mm256_add_epi16(a16_lo, _mm256_sub_epi16(b16_lo, c16_lo));
                            __m256i p_hi = _mm256_add_epi16(a16_hi, _mm256_sub_epi16(b16_hi, c16_hi));

                            __m256i pa_lo = _mm256_abs_epi16(_mm256_sub_epi16(p_lo, a16_lo));
                            __m256i pa_hi = _mm256_abs_epi16(_mm256_sub_epi16(p_hi, a16_hi));

                            __m256i pb_lo = _mm256_abs_epi16(_mm256_sub_epi16(p_lo, b16_lo));
                            __m256i pb_hi = _mm256_abs_epi16(_mm256_sub_epi16(p_hi, b16_hi));

                            __m256i pc_lo = _mm256_abs_epi16(_mm256_sub_epi16(p_lo, c16_lo));
                            __m256i pc_hi = _mm256_abs_epi16(_mm256_sub_epi16(p_hi, c16_hi));

                            __m256i pa_le_pb_lo = _mm256_or_si256(_mm256_cmpgt_epi16(pb_lo, pa_lo), _mm256_cmpeq_epi16(pa_lo, pb_lo));
                            __m256i pa_le_pc_lo = _mm256_or_si256(_mm256_cmpgt_epi16(pc_lo, pa_lo), _mm256_cmpeq_epi16(pa_lo, pc_lo));
                            __m256i cond1_lo = _mm256_and_si256(pa_le_pb_lo, pa_le_pc_lo);
                            __m256i cond2_lo = _mm256_or_si256(_mm256_cmpgt_epi16(pc_lo, pb_lo), _mm256_cmpeq_epi16(pb_lo, pc_lo));

                            __m256i pa_le_pb_hi = _mm256_or_si256(_mm256_cmpgt_epi16(pb_hi, pa_hi), _mm256_cmpeq_epi16(pa_hi, pb_hi));
                            __m256i pa_le_pc_hi = _mm256_or_si256(_mm256_cmpgt_epi16(pc_hi, pa_hi), _mm256_cmpeq_epi16(pa_hi, pc_hi));
                            __m256i cond1_hi = _mm256_and_si256(pa_le_pb_hi, pa_le_pc_hi);
                            __m256i cond2_hi = _mm256_or_si256(_mm256_cmpgt_epi16(pc_hi, pb_hi), _mm256_cmpeq_epi16(pb_hi, pc_hi));

                            __m256i da_lo = _mm256_and_si256(a16_lo, cond1_lo);
                            __m256i db_lo = _mm256_and_si256(b16_lo, _mm256_andnot_si256(cond1_lo, cond2_lo));
                            __m256i dc_lo = _mm256_and_si256(c16_lo, _mm256_and_si256(_mm256_andnot_si256(cond1_lo, all1), _mm256_andnot_si256(cond2_lo, all1)));

                            __m256i da_hi = _mm256_and_si256(a16_hi, cond1_hi);
                            __m256i db_hi = _mm256_and_si256(b16_hi, _mm256_andnot_si256(cond1_hi, cond2_hi));
                            __m256i dc_hi = _mm256_and_si256(c16_hi, _mm256_and_si256(_mm256_andnot_si256(cond1_hi, all1), _mm256_andnot_si256(cond2_hi, all1)));

                            __m256i d_lo = _mm256_or_si256(_mm256_or_si256(da_lo, db_lo), dc_lo);
                            __m256i d_hi = _mm256_or_si256(_mm256_or_si256(da_hi, db_hi), dc_hi);

                            __m256i d = _mm256_packus_epi16(d_lo, d_hi);

                            _mm256_storeu_si256((__m256i*)(dst), _mm256_sub_epi8(_mm256_loadu_si256((const __m256i*)(raw)), d));

                            a += 32;
                            b += 32;
                            c += 32;
                            dst += 32;
                            raw += 32;
                        }
                        for (; j < bpr; j++)
                        {
                            int p = (int)*a + (int)*b - (int)*c;
                            int pa = abs__(p - (int)*a);
                            int pb = abs__(p - (int)*b);
                            int pc = abs__(p - (int)*c);
                            uint8_t d;
                            if (pa <= pb && pa <= pc) d = *a;
                            else if (pb <= pc) d = *b;
                            else d = *c;
                            *dst++ = *raw++ - d;
                            a++; b++; c++;
                        }
                    }
                    break;
                }
                }
*/