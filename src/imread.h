#include <cmath>
#include <cstdint>
#include <fstream>
#include <immintrin.h>
#include <iosfwd>
#include <memory.h>
#include <string>
#include <utility>
#include <vector>
#include <zlib-ng.h>

// image
struct Image {
    std::vector<uint8_t> buffer;
    uint32_t height;
    uint32_t width;
    uint32_t channels;
    unsigned long long state; // 0 = success
                              // 1 = file related failure
                              // 2 = invalid input file format
                              // 3 = inflate failure
};
// buffer is an object declare from the sruct so it BELONGS to the STRUCT the object Image <=> clear ownership








// static
// functions
static inline uint32_t readuint32BigEdian(void *data) noexcept;
static inline int get_channels(int color_type, int bit_depth) noexcept;
static inline std::ifstream fileInit2(const std::string path, uint64_t *file_size, std::vector<uint8_t> *worker,
                                      Image *result) noexcept;
static inline void ct3decode(Image *result, std::ifstream &file, uint64_t file_size, int bit_depth) noexcept;
static inline void bd8decode(Image *result, std::ifstream &file, uint64_t file_size) noexcept;
static inline bool defilter(uint8_t *buffer, uint64_t imtrker);
static inline void cvt8bit(Image *result, int bit_depth, uint64_t imtrker, const bool ct3flag = false) noexcept;
static inline uint64_t abs__(uint64_t x) noexcept;







// static
//  constants
static const unsigned char pngsig[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
static const unsigned char ihdrsig[4] = {'I', 'H', 'D', 'R'};
static const unsigned char IDATsig[4] = {'I', 'D', 'A', 'T'};
static const unsigned char IENDsig[4] = {'I', 'E', 'N', 'D'};
static const unsigned char PLTEsig[4] = {'P', 'L', 'T', 'E'};
static const unsigned char tRNSsig[4] = {'t', 'R', 'N', 'S'};






// static
// variables
static uint64_t bpp;
static uint64_t bpr;
static uint8_t *scanline[2];












// imread function
Image png_imread(const std::string path) noexcept;

inline Image png_imread(const std::string path) noexcept {

    Image result;
    memset(reinterpret_cast<void *>(&result), 0, sizeof(result));
    std::vector<uint8_t> worker(33);
    uint64_t file_size = 0;

    std::ifstream file = fileInit2(path, &file_size, &worker, &result);
    if (result.state != 0) { return result; }

    const uint32_t width = result.width = readuint32BigEdian(worker.data() + 16);
    const uint32_t height = result.height = readuint32BigEdian(worker.data() + 20);
    const int bit_depth = static_cast<int>(worker[24]);
    const int color_type = static_cast<int>(worker[25]);
    const int channels = result.channels = get_channels(color_type, bit_depth);
    const int compression_method = worker[26];
    const int filter_method = worker[27];
    const int interlace_method = worker[28];

    if ((compression_method | filter_method | interlace_method) != 0 | channels == 0) {
        result.state = 2;
        return result;
    }

    result.buffer.resize(height * width * channels);

    if (color_type == 3) {
        ct3decode(&result, file, file_size, bit_depth);
        return result;
    }

    if (bit_depth == 8) {
        bd8decode(&result, file, file_size);
        return result;
    }

    bpp = channels * (bit_depth == 16 ? 2 : 1);
    bpr = uint64_t(std::ceil(((double)width * (double)channels * (double)bit_depth) / (double)8));

    std::vector<uint8_t> line[2];
    line[0].resize(bpr);
    line[1].resize(bpr);

    worker.resize(12);

    uint64_t data_len{};
    int idat_check{};
    bool iend_check = false;

    const uint64_t CHUNK = 65536; // 2^16
    do {
        file.read(reinterpret_cast<char *>(worker.data()), 8);
        data_len = readuint32BigEdian(worker.data());

        // isIDAT
        if (memcmp(worker.data() + 4, IDATsig, 4) == 0) {
            idat_check++;
            if (idat_check > 1) {
                result.state = 2;
                return result;
            }
            zng_stream strm = {0};
            strm.zalloc = Z_NULL;
            strm.zfree = Z_NULL;
            strm.opaque = Z_NULL;
            strm.avail_in = 0;
            strm.next_in = Z_NULL;
            int ret = zng_inflateInit(&strm);
            if (ret != Z_OK) {
                result.state = 3;
                return result;
            }
            uint64_t ai{CHUNK};
            uint64_t ftrker{};
            uint64_t intrker{};
            uint64_t offset{};
            uint64_t have{};
            uint64_t row_produced{};
            uint32_t crc{};
            uint64_t imtrker{};
            std::vector<uint8_t> out(CHUNK);
            std::vector<uint8_t> in(CHUNK);

            // data_len, ++12, data_len,...
            do {
                data_len = readuint32BigEdian(worker.data());
                if (file_size <= data_len) {
                    result.state = 1;
                    return result;
                }
                crc = zng_crc32(0U, Z_NULL, 0);
                crc = zng_crc32(crc, worker.data() + 4, 4);
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
                                result.state = 3;
                                return result;
                            }
                            row_produced = (have / (bpr + 1));
                            offset = (have - row_produced * (bpr + 1));
                            for (uint64_t i = 0; i < row_produced; i++) {
                                scanline[0] = line[0].data();
                                scanline[1] = line[1].data();
                                // defilter to scanline[1] from buffer and scanline[0]
                                if (!defilter(out.data() + i * (bpr + 1), imtrker)) {
                                    result.state = 2;
                                    return result;
                                }
                                // cvt8bit to the result image from scanline[0]
                                cvt8bit(&result, bit_depth, imtrker);
                                // swap scanline[0] to scanlinep[1] for the next defilter
                                std::swap(line[0], line[1]);
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

                if (!file.read(reinterpret_cast<char *>(worker.data() + 8), 4)) {
                    result.state = 1;
                    return result;
                }

                if (readuint32BigEdian(worker.data() + 8) != crc) {
                    result.state = 2;
                    return result;
                }

                if (!file.read(reinterpret_cast<char *>(worker.data()), 8)) {
                    result.state = 1;
                    return result;
                }
            } while (memcmp(worker.data() + 4, IDATsig, 4) == 0);
            strm.avail_in = intrker;
            strm.next_in = in.data();
            do {
                strm.avail_out = out.size() - offset;
                strm.next_out = out.data() + offset;
                ret = zng_inflate(&strm, Z_NO_FLUSH);
                have = out.size() - strm.avail_out;
                if (ret != Z_OK && ret != Z_STREAM_END) [[unlikely]] {
                    result.state = 3;
                    return result;
                }
                row_produced = (have / (bpr + 1));
                offset = have - row_produced * (bpr + 1);
                for (uint64_t i = 0; i < row_produced; i++) {
                    scanline[0] = line[0].data();
                    scanline[1] = line[1].data();
                    if (!defilter(out.data() + i * (bpr + 1), imtrker)) {
                        result.state = 2;
                        return result;
                    }
                    cvt8bit(&result, bit_depth, imtrker);
                    std::swap(line[0], line[1]);
                    imtrker++;
                }
                memmove(out.data(), out.data() + have - offset, offset);
                // std::cout << have << " " << row_produced << " " << offset <<
                // "\n";
            } while (ret != Z_STREAM_END);
            // if (offset != 0) throw std::runtime_error("data loss");
            zng_inflateEnd(&strm);
            file.seekg((uint64_t)(file.tellg()) - 8);
        }

        // isIEND
        else if (memcmp(worker.data() + 4, IENDsig, 4) == 0) {
            if (idat_check == 0) {
                result.state = 2;
                return result;
            }
            iend_check = true;
            break;
        }

        // else
        else {
            file.seekg((uint64_t)(file.tellg()) + data_len + 4);
        }
    } while ((uint64_t)(file.tellg()) <= (file_size - 12));

    if (!iend_check) { result.state = 2; }

    return result;
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

static inline std::ifstream fileInit2(const std::string path, uint64_t *file_size, std::vector<uint8_t> *worker,
                                      Image *result) noexcept {
    uint8_t *w = (*worker).data();
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        result->state = 1;
        return file;
    }
    *file_size = file.tellg();
    file.seekg(0, std::ios::beg);
    if (!file.read(reinterpret_cast<char *>(w), 33) || *file_size < 33 + 12) {
        result->state = 1;
        return file;
    }

    uint32_t crc_ = zng_crc32(0U, Z_NULL, 0);
    crc_ = zng_crc32(crc_, w + 12, 4);
    crc_ = zng_crc32(crc_, w + 16, readuint32BigEdian(w + 8));

    if (memcmp(w, pngsig, 8) != 0 | memcmp(w + 12, ihdrsig, 4) != 0 | readuint32BigEdian(w + 29) != crc_) {
        result->state = 2;
        return file;
    }

    return file;
}

static inline bool defilter(uint8_t *buffer, uint64_t imtrker) {
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
            for (uint64_t i = bpp; i < bpr; i++) { *dest++ = uint8_t(*buffer++ + *a++ / 2); }
        } else [[likely]] {
            uint8_t *dest = scanline[1];
            uint8_t *b = scanline[0];
            uint64_t i = 0;
            for (; i < bpp; i++) { *dest++ = uint8_t(*buffer++ + *b++ / 2); }
            uint8_t *a = dest - bpp;
            for (; i < bpr; i++) { *dest++ = uint8_t(*buffer++ + (*a++ + *b++) / 2); }
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
    default: return false;
    }

    /*
    if (1 < imtrker && imtrker < 3) {
        buffer -= bpr;
        for (int64_t i = 0; i < 18; i++) { printf("%04d ", buffer[i]); }
        printf("\n");
        for (int64_t i = 0; i < 18; i++) { printf("%04d ", scanline[0][i]); }
        printf("\n");
        for (int64_t i = 0; i < 18; i++) { printf("%04d ", scanline[1][i]); }
        printf("\n\n");
    }
    */

    return true;
}

static inline void cvt8bit(Image *result, int bit_depth, uint64_t imtrker, const bool ct3flag) noexcept {
    uint8_t *src = scanline[1];
    uint8_t *dest = result->buffer.data() +
                    imtrker * (uint64_t)(result->width) * (1 + (!ct3flag) * ((uint64_t)(result->channels) - 1));
    switch (bit_depth) {
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

            __m128i a0 = _mm_unpacklo_epi8(in0, in1); // 0 1 lo
            __m128i a1 = _mm_unpackhi_epi8(in0, in1); // 0 1 hi
            __m128i a2 = _mm_unpacklo_epi8(in2, in3); // 2 3 lo
            __m128i a3 = _mm_unpackhi_epi8(in2, in3); // 2 3 hi
            __m128i a4 = _mm_unpacklo_epi8(in4, in5); // 4 5 lo
            __m128i a5 = _mm_unpackhi_epi8(in4, in5); // 4 5 hi
            __m128i a6 = _mm_unpacklo_epi8(in6, in7); // 6 7 lo
            __m128i a7 = _mm_unpackhi_epi8(in6, in7); // 6 7 hi


            __m128i b0 = _mm_unpacklo_epi16(a0, a2); // 0123 lo lo
            __m128i b1 = _mm_unpacklo_epi16(a1, a3); // 0123 hi lo
            __m128i b2 = _mm_unpacklo_epi16(a4, a6); // 4567 lo lo
            __m128i b3 = _mm_unpacklo_epi16(a5, a7); // 4567 hi lo

            __m128i b4 = _mm_unpackhi_epi16(a0, a2); // 0123 lo hi
            __m128i b5 = _mm_unpackhi_epi16(a1, a3); // 0123 hi hi
            __m128i b6 = _mm_unpackhi_epi16(a4, a6); // 4567 lo hi
            __m128i b7 = _mm_unpackhi_epi16(a5, a7); // 4567 hi hi


            __m128i c0 = _mm_unpacklo_epi32(b0, b2); // 01234567 lo lo
            __m128i c1 = _mm_unpacklo_epi32(b1, b3); // 01234567 hi lo
            __m128i c2 = _mm_unpacklo_epi32(b4, b6); // 01234567 lo lo
            __m128i c3 = _mm_unpacklo_epi32(b5, b7); // 01234567 hi lo

            __m128i c4 = _mm_unpackhi_epi32(b0, b2); // 01234567 lo hi
            __m128i c5 = _mm_unpackhi_epi32(b1, b3); // 01234567 hi hi
            __m128i c6 = _mm_unpackhi_epi32(b4, b6); // 01234567 lo hi
            __m128i c7 = _mm_unpackhi_epi32(b5, b7); // 01234567 hi hi

            _mm_storeu_si128((__m128i *)(dest + 0 * 16), c0);
            _mm_storeu_si128((__m128i *)(dest + 1 * 16), c1);
            _mm_storeu_si128((__m128i *)(dest + 2 * 16), c2);
            _mm_storeu_si128((__m128i *)(dest + 3 * 16), c3);
            _mm_storeu_si128((__m128i *)(dest + 4 * 16), c4);
            _mm_storeu_si128((__m128i *)(dest + 5 * 16), c5);
            _mm_storeu_si128((__m128i *)(dest + 6 * 16), c6);
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

            __m128i in0 = _mm_packus_epi16(in0_lo, in0_hi);
            __m128i in1 = _mm_packus_epi16(in1_lo, in1_hi);
            __m128i in2 = _mm_packus_epi16(in2_lo, in2_hi);
            __m128i in3 = _mm_packus_epi16(in3_lo, in3_hi);

            __m128i a0 = _mm_unpacklo_epi8(in0, in1); // 01 23 lo
            __m128i a1 = _mm_unpackhi_epi8(in0, in1); // 01 23 hi
            __m128i a2 = _mm_unpacklo_epi8(in2, in3); // 45 67 lo
            __m128i a3 = _mm_unpackhi_epi8(in2, in3); // 45 67 hi

            __m128i b0 = _mm_unpacklo_epi16(a0, a2); // 01234567 lo lo
            __m128i b1 = _mm_unpacklo_epi16(a1, a3); // 01234567 hi lo
            __m128i b2 = _mm_unpackhi_epi16(a0, a2); // 01234567 lo hi
            __m128i b3 = _mm_unpackhi_epi16(a1, a3); // 01234567 hi hi

            _mm_storeu_si128((__m128i *)(dest + 0 * 16), b0);
            _mm_storeu_si128((__m128i *)(dest + 1 * 16), b1);
            _mm_storeu_si128((__m128i *)(dest + 2 * 16), b2);
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

            __m128i a0 = _mm_unpacklo_epi8(in0, in1); // 01234567 lo
            __m128i a1 = _mm_unpackhi_epi8(in0, in1); // 01234567 hi

            _mm_storeu_si128((__m128i *)(dest + 0 * 16), a0);
            _mm_storeu_si128((__m128i *)(dest + 1 * 16), a1);

            src += 16;
            dest += 32;
        }
        for (; i < bpr; i++) {
            for (int j = 1; j >= 0; j--) { *dest++ = ((*src >> (j * 4)) & 15) * (1 + (!ct3flag) * (17 - 1)); }
            src++;
        }
        break;
    }
    case 8: {
        uint64_t i = 0;
        __m128i FFs = _mm_set1_epi16(1);
        __m128i ones = _mm_set1_epi16(0b11111111);
        __m128i zeroes = _mm_setzero_si128();

        for (; i + 16 <= bpr; i += 16) {
            __m128i in = _mm_loadu_si128((const __m128i *)src);

            __m128i in_lo = _mm_unpacklo_epi8(in, zeroes);
            __m128i in_hi = _mm_unpackhi_epi8(in, zeroes);

            __m128i lo = _mm_mullo_epi16(FFs, _mm_and_si128(_mm_srli_epi16(in_lo, 0), ones));
            __m128i hi = _mm_mullo_epi16(FFs, _mm_and_si128(_mm_srli_epi16(in_hi, 0), ones));

            __m128i in0 = _mm_packus_epi16(lo, hi);

            _mm_storeu_si128((__m128i *)(dest + 0 * 8), in0);

            src += 16;
            dest += 16;
        }
        for (; i < bpr; i++) {
            for (int j = 0; j >= 0; j--) { *dest++ = ((*src >> j) & 1) * 1; }
            src++;
        }
        break;
    }
    case 16: {
        uint64_t i = 0;

        __m128i ww = _mm_set1_epi16(128);
        __m128i zz = _mm_setzero_si128();

        for (; i + 16 <= bpr; i += 16) {
            __m128i in = _mm_loadu_si128((const __m128i *)src);
            in = _mm_or_si128(_mm_slli_epi16(in, 8), _mm_srli_epi16(in, 8));

            __m128i oi = _mm_srli_epi16(_mm_add_epi16(in, ww), 8);

            __m128i ou = _mm_packus_epi16(oi, zz);

            _mm_storel_epi64((__m128i *)dest, ou);

            src += 16;
            dest += 8;
        }
        for (; i < bpr; i += 2) {
            uint16_t lo = static_cast<uint16_t>(*src++);
            uint16_t hi = static_cast<uint16_t>(*src++);
            uint16_t v = (lo << 8) | hi;
            *dest++ = (v + 128) >> 8;
        }
        break;
    }
    }
}

static inline void bd8decode(Image *result, std::ifstream &file, uint64_t file_size) noexcept {
    bpp = result->channels;
    bpr = (uint64_t)(result->width) * bpp;

    std::vector<uint8_t> worker(12);

    uint64_t data_len{};
    int idat_check{};
    bool iend_check = false;

    const uint64_t CHUNK = 65536; // 2^16
    do {
        file.read(reinterpret_cast<char *>(worker.data()), 8);
        data_len = readuint32BigEdian(worker.data());

        // isIDAT
        if (memcmp(worker.data() + 4, IDATsig, 4) == 0) {
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
            uint64_t imtrker{};
            std::vector<uint8_t> out(CHUNK);
            std::vector<uint8_t> in(CHUNK);

            // data_len, ++12, data_len,...
            do {
                data_len = readuint32BigEdian(worker.data());
                if (file_size <= data_len) {
                    result->state = 1;
                    return;
                }
                crc = zng_crc32(0U, Z_NULL, 0);
                crc = zng_crc32(crc, worker.data() + 4, 4);
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
                            row_produced = have / (bpr + 1);
                            offset = have - row_produced * (bpr + 1);
                            for (uint64_t i = 0; i < row_produced; i++) {
                                scanline[0] = result->buffer.data() + (imtrker - 1) * bpr;
                                scanline[1] = result->buffer.data() + imtrker * bpr;
                                // defilter to scanline[1] from buffer and scanline[0]
                                if (!defilter(out.data() + i * (bpr + 1), imtrker)) {
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
                            ai -= ftrker;
                            ftrker = 0;
                        }
                    } while (ftrker != 0);
                }

                if (!file.read(reinterpret_cast<char *>(worker.data() + 8), 4)) {
                    result->state = 1;
                    return;
                }

                if (readuint32BigEdian(worker.data() + 8) != crc) {
                    result->state = 2;
                    return;
                }

                if (!file.read(reinterpret_cast<char *>(worker.data()), 8)) {
                    result->state = 1;
                    return;
                }
            } while (memcmp(worker.data() + 4, IDATsig, 4) == 0);
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
                row_produced = have / (bpr + 1);
                offset = have - row_produced * (bpr + 1);
                for (uint64_t i = 0; i < row_produced; i++) {
                    scanline[0] = result->buffer.data() + (imtrker - 1) * bpr;
                    scanline[1] = result->buffer.data() + imtrker * bpr;
                    // defilter to scanline[1] from buffer and scanline[0]
                    if (!defilter(out.data() + i * (bpr + 1), imtrker)) {
                        result->state = 2;
                        return;
                    }
                    imtrker++;
                }
                memmove(out.data(), out.data() + have - offset, offset);
                // std::cout << have << " " << row_produced << " " << offset <<
                // "\n";
            } while (ret != Z_STREAM_END);
            // if (offset != 0) throw std::runtime_error("data loss");
            zng_inflateEnd(&strm);
            file.seekg((uint64_t)(file.tellg()) - 8);
        }

        // isIEND
        else if (memcmp(worker.data() + 4, IENDsig, 4) == 0) {
            if (idat_check == 0) {
                result->state = 2;
                return;
            }
            iend_check = true;
            break;
        }

        // else
        else {
            file.seekg((uint64_t)(file.tellg()) + data_len + 4);
        }
    } while ((uint64_t)(file.tellg()) <= (file_size - 12));

    if (!iend_check) {
        result->state = 2;
        return;
    }
}

static inline void ct3decode(Image *result, std::ifstream &file, uint64_t file_size, int bit_depth) noexcept {
    bpp = 1;
    bpr = uint64_t(std::ceil(((double)(result->width) * (double)bit_depth) / (double)8));

    std::vector<uint8_t> index((uint64_t)(result->width) * (uint64_t)(result->height));

    std::vector<uint8_t> line[2];
    line[0].resize(bpr);
    line[1].resize(bpr);

    std::vector<uint8_t> worker(12);

    uint64_t data_len{};
    int plte_check{};
    int idat_check{};
    int tRNS_check{};
    bool iend_check = false;

    std::vector<std::vector<uint8_t>> palette;

    const uint64_t CHUNK = 65536; // 2^16
    do {
        file.read(reinterpret_cast<char *>(worker.data()), 8);
        data_len = readuint32BigEdian(worker.data());

        // IDAT
        if (memcmp(worker.data() + 4, IDATsig, 4) == 0) {
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
            uint64_t imtrker{};
            std::vector<uint8_t> out(CHUNK);
            std::vector<uint8_t> in(CHUNK);

            // data_len, ++12, data_len,...
            do {
                data_len = readuint32BigEdian(worker.data());
                if (file_size <= data_len) {
                    result->state = 1;
                    return;
                }
                crc = zng_crc32(0U, Z_NULL, 0);
                crc = zng_crc32(crc, worker.data() + 4, 4);
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
                                scanline[0] = line[0].data();
                                scanline[1] = line[1].data();
                                // defilter to scanline[1] from buffer and scanline[0]
                                if (!defilter(out.data() + i * (bpr + 1), imtrker)) {
                                    result->state = 2;
                                    return;
                                }
                                // cvt8bit to the result image from scanline[0] and give to result->buffer
                                cvt8bit(result, bit_depth, imtrker, true);
                                // swap scanline[0] to scanline[1] for the next defilter
                                std::swap(line[0], line[1]);
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

                if (!file.read(reinterpret_cast<char *>(worker.data() + 8), 4)) {
                    result->state = 1;
                    return;
                }

                if (readuint32BigEdian(worker.data() + 8) != crc) {
                    result->state = 2;
                    return;
                }

                if (!file.read(reinterpret_cast<char *>(worker.data()), 8)) {
                    result->state = 1;
                    return;
                }
            } while (memcmp(worker.data() + 4, IDATsig, 4) == 0);
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
                    scanline[0] = line[0].data();
                    scanline[1] = line[1].data();
                    if (!defilter(out.data() + i * (bpr + 1), imtrker)) {
                        result->state = 2;
                        return;
                    }
                    cvt8bit(result, bit_depth, imtrker, true);
                    std::swap(line[0], line[1]);
                    imtrker++;
                }
                memmove(out.data(), out.data() + have - offset, offset);
                // std::cout << have << " " << row_produced << " " << offset <<
                // "\n";
            } while (ret != Z_STREAM_END);
            // if (offset != 0) throw std::runtime_error("data loss");
            zng_inflateEnd(&strm);
            file.seekg((uint64_t)(file.tellg()) - 8);
        }

        // PLTE
        else if (memcmp(worker.data() + 4, PLTEsig, 4) == 0) {
            plte_check++;
            if (plte_check > 1) [[unlikely]] {
                result->state = 2;
                return;
            }

            std::vector<uint8_t> plte(data_len);
            file.read(reinterpret_cast<char *>(plte.data()), data_len);

            uint32_t crc_ = zng_crc32(0U, Z_NULL, 0);
            crc_ = zng_crc32(crc_, worker.data() + 4, 4);
            crc_ = zng_crc32(crc_, plte.data(), data_len);

            file.read(reinterpret_cast<char *>(worker.data() + 8), 4);

            if (plte_check > 1 || (data_len) % 3 != 0 || !(data_len / 3 >= 0 && data_len / 3 <= 256) ||
                readuint32BigEdian(worker.data() + 8) != crc_) [[unlikely]] {
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
        }

        // tRNS
        else if (memcmp(worker.data() + 4, tRNSsig, 4) == 0) {
            tRNS_check++;
            if (tRNS_check > 1 || plte_check == 0 || plte_check == 0 || data_len > palette.size()) [[unlikely]] {
                result->state = 2;
                return;
            }

            uint64_t entries = palette.size();

            std::vector<uint8_t> trns(data_len);
            file.read(reinterpret_cast<char *>(trns.data()), data_len);

            uint32_t crc_ = zng_crc32(0U, Z_NULL, 0);
            crc_ = zng_crc32(crc_, worker.data() + 4, 4);
            crc_ = zng_crc32(crc_, trns.data(), data_len);

            file.read(reinterpret_cast<char *>(worker.data() + 8), 4);
            if (readuint32BigEdian(worker.data() + 8) != crc_) {
                result->state = 2;
                return;
            }

            for (uint64_t i = 0; i < data_len; i++) { palette[i][3] = trns[i]; }
        }

        // IEND
        else if (memcmp(worker.data() + 4, IENDsig, 4) == 0) {
            if (idat_check == 0) {
                result->state = 2;
                return;
            }
            iend_check = true;
            break;
        }

        // else
        else {
            file.seekg((uint64_t)(file.tellg()) + data_len + 4);
        }
    } while ((uint64_t)(file.tellg()) <= (file_size - 12));

    if (!iend_check) {
        result->state = 2;
        return;
    }

    uint8_t *im = result->buffer.data();
    uint8_t *ind = index.data();
    memcpy(index.data(), result->buffer.data(), index.size());
    for (uint64_t i = 0; i < (uint64_t)(result->height) * (uint64_t)(result->width); i++) {
        for (uint64_t k = 0; k < (uint64_t)(result->channels); k++) { *im++ = palette[*ind][k]; }
        ind++;
    }
}

static inline uint64_t abs__(uint64_t x) noexcept {
    uint64_t y = x >> 63;
    return ((x ^ -y) + y);
}