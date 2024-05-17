#pragma once
 
/** SipHash is a fast cryptographic hash function for short strings.
  * Taken from here: https://www.131002.net/siphash/
  *
  * This is SipHash 2-4 variant.
  *
  * Two changes are made:
  * - returns also 128 bits, not only 64;
  * - done streaming (can be calculated in parts).
  *
  * On short strings (URL, search phrases) more than 3 times faster than MD5 from OpenSSL.
  * (~ 700 MB/sec, 15 million strings per second)
  */
 
 
#include <algorithm>
#include <array>
#include <bit>
#include <string>
#include <string_view>
#include <type_traits>
#include <Core/Defines.h>
#include <base/extended_types.h>
#include <base/types.h>
#include <base/unaligned.h>
#include <base/hex.h>
#include <Common/Exception.h>
#include <Common/transformEndianness.h>
 
#include <city.h>
 
#include <immintrin.h>
 
 #include <iostream>
 
 
namespace DB::ErrorCodes
{
    extern const int LOGICAL_ERROR;
}
 
#define SIPROUND                                                  \
    do                                                            \
    {                                                             \
        v0 += v1; v1 = std::rotl(v1, 13); v1 ^= v0; v0 = std::rotl(v0, 32); \
        v2 += v3; v3 = std::rotl(v3, 16); v3 ^= v2;                    \
        v0 += v3; v3 = std::rotl(v3, 21); v3 ^= v0;                    \
        v2 += v1; v1 = std::rotl(v1, 17); v1 ^= v2; v2 = std::rotl(v2, 32); \
    } while(0)
 
#define SIPROUND_AVX                                               \
    do                                                              \
    {                                                               \
        v0_avx = _mm256_add_epi64(v0_avx, v1_avx);                  \
        v1_avx = _mm256_rol_epi64(v1_avx, 13);                      \
        v1_avx = _mm256_xor_epi64(v1_avx, v0_avx);                  \
        v0_avx = _mm256_rol_epi64(v0_avx, 32);                      \
        v2_avx = _mm256_add_epi64(v2_avx, v3_avx);                  \
        v3_avx = _mm256_rol_epi64(v3_avx, 16);                      \
        v3_avx = _mm256_xor_epi64(v3_avx, v2_avx);                  \
        v0_avx = _mm256_add_epi64(v0_avx, v3_avx);                  \
        v3_avx = _mm256_rol_epi64(v3_avx, 21);                      \
        v3_avx = _mm256_xor_epi64(v3_avx, v0_avx);                  \
        v2_avx = _mm256_add_epi64(v2_avx, v1_avx);                  \
        v1_avx = _mm256_rol_epi64(v1_avx, 17);                      \
        v1_avx = _mm256_xor_epi64(v1_avx, v2_avx);                  \
        v2_avx = _mm256_rol_epi64(v2_avx, 32);                      \
    } while(0)


#define SIPROUND_ARR(i)                                                 \
    do                                                            \
    {                                                             \
        v0_arr[i] += v1_arr[i]; v1_arr[i] = std::rotl(v1_arr[i], 13); v1_arr[i] ^= v0_arr[i]; v0_arr[i] = std::rotl(v0_arr[i], 32); \
        v2_arr[i] += v3_arr[i]; v3_arr[i] = std::rotl(v3_arr[i], 16); v3_arr[i] ^= v2_arr[i];                    \
        v0_arr[i] += v3_arr[i]; v3_arr[i] = std::rotl(v3_arr[i], 21); v3_arr[i] ^= v0_arr[i];                    \
        v2_arr[i] += v1_arr[i]; v1_arr[i] = std::rotl(v1_arr[i], 17); v1_arr[i] ^= v2_arr[i]; v2_arr[i] = std::rotl(v2_arr[i], 32); \
    } while(0)

 
 
/// Define macro CURRENT_BYTES_IDX for building index used in current_bytes array
/// to ensure correct byte order on different endian machines
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define CURRENT_BYTES_IDX(i) (7 - i)
#else
#define CURRENT_BYTES_IDX(i) (i)
#endif

static constexpr UInt64 MAX64 = 0x7fffffffffffffff;

constexpr UInt64 getValWithoutByte(UInt64 byte) {
    return MAX64 ^ (15 << (8 * byte));
}
 
class SipHashAvx
{
private:
    static void print(std::string debug, __m256i res) {
        std::array<UInt64, 4> arr = {static_cast<UInt64>(_mm256_extract_epi64(res, 0)), static_cast<UInt64>(_mm256_extract_epi64(res, 1)),static_cast<UInt64>(_mm256_extract_epi64(res, 2)), static_cast<UInt64>(_mm256_extract_epi64(res, 3))};
        for (size_t i = 0; i < arr.size(); ++i) {
            std::cout << debug << ": " << arr[i] << std::endl;
        }
        return;
    }

    /// State.
    UInt64 v0;
    UInt64 v1;
    UInt64 v2;
    UInt64 v3;
 
    __m256i v0_avx;
    __m256i v1_avx;
    __m256i v2_avx;
    __m256i v3_avx;

    UInt64 v0_arr[4];
    UInt64 v1_arr[4];
    UInt64 v2_arr[4];
    UInt64 v3_arr[4];

    UInt64 cnt_arr[4];

    __m256i current_bytes_avx;
  
    /// How many bytes have been processed.
    UInt64 cnt;
 
    /// Whether it should use the reference algo for 128-bit or CH's version
    bool is_reference_128;
 
    /// The current 8 bytes of input data.
    union
    {
        UInt64 current_word;
        UInt8 current_bytes[8];
    };
    union
    {
        UInt64 current_word_1;
        UInt8 current_bytes_1[8];
    };
    union
    {
        UInt64 current_word_2;
        UInt8 current_bytes_2[8];
    };
    union
    {
        UInt64 current_word_3;
        UInt8 current_bytes_3[8];
    };


 
    ALWAYS_INLINE void finalize()
    {
        /// In the last free byte, we write the remainder of the division by 256.
        //std::cout << "current word before finalize1 : " << current_word << std::endl;
        current_bytes[CURRENT_BYTES_IDX(7)] = static_cast<UInt8>(cnt);
        //std::cout << "cnt: " << cnt << std::endl;
 
        //std::cout << "current word before finalize: " << current_word << std::endl;
        v3 ^= current_word;
        SIPROUND;
        SIPROUND;
        v0 ^= current_word;
 
        if (is_reference_128)
            v2 ^= 0xee;
        else
            v2 ^= 0xff;
 
        SIPROUND;
        SIPROUND;
        SIPROUND;
        SIPROUND;
    }

    ALWAYS_INLINE void finalize_array_all_len()
    {
        for (int i = 0; i < 4; ++i) {
            UInt8* cur_bytes_tmp;
            UInt64* cur_word_tmp = &current_word;
            if (i == 0) {
                cur_word_tmp = &current_word;
                cur_bytes_tmp = current_bytes;
            } else if (i == 1) {
                cur_word_tmp = &current_word_1;
                cur_bytes_tmp = current_bytes_1;
            } else if (i == 2) {
                cur_word_tmp = &current_word_2;
                cur_bytes_tmp = current_bytes_2;
            } else {
                cur_word_tmp = &current_word_3;
                cur_bytes_tmp = current_bytes_3;
            }
            /// In the last free byte, we write the remainder of the division by 256.
            //std::cout << "current word before finalize1 : " << current_word << std::endl;
            cur_bytes_tmp[CURRENT_BYTES_IDX(7)] = static_cast<UInt8>(cnt_arr[i]);
            //std::cout << "cnt: " << cnt << std::endl;
    
            //std::cout << "current word before finalize: " << current_word << std::endl;
            v3_arr[i] ^= *cur_word_tmp;
            SIPROUND_ARR(i);
            SIPROUND_ARR(i);
            v0_arr[i] ^= *cur_word_tmp;
    
            if (is_reference_128)
                v2_arr[i] ^= 0xee;
            else
                v2_arr[i] ^= 0xff;
    
            SIPROUND_ARR(i);
            SIPROUND_ARR(i);
            SIPROUND_ARR(i);
            SIPROUND_ARR(i);
        }
    }

    __attribute__((__target__("avx512vl,avx512f,avx512bw"))) ALWAYS_INLINE void finalize_array()
    {
        /// In the last free byte, we write the remainder of the division by 256.

        // 8 * 7 = 56
        //print("currnt_word avx in finalize", current_bytes_avx);
        //std::cout << "cnt avx: " << cnt << std::endl;

        UInt64 x = 8 * CURRENT_BYTES_IDX(7);
        UInt64 vl = MAX64 ^ (255ull << x);
        current_bytes_avx = _mm256_and_epi64(current_bytes_avx, _mm256_set_epi64x(vl, vl, vl, vl));
        current_bytes_avx = _mm256_or_epi64(current_bytes_avx, _mm256_set_epi64x(cnt << x, cnt << x, cnt << x, cnt << x));

        //print("current_bytes_avx", current_bytes_avx); 
        v3_avx = _mm256_xor_epi64(v3_avx, current_bytes_avx);
        SIPROUND_AVX;
        SIPROUND_AVX;
        v0_avx = _mm256_xor_epi64(v0_avx, current_bytes_avx);
 
        v2_avx = _mm256_xor_epi64(v2_avx, _mm256_set_epi64x(0xff, 0xff, 0xff, 0xff));
 
        SIPROUND_AVX;
        SIPROUND_AVX;
        SIPROUND_AVX;
        SIPROUND_AVX;
    }


public: 
    /// Arguments - seed.
    __attribute__((__target__("avx512vl,avx512f,avx512bw"))) SipHashAvx(UInt64 key0 = 0, UInt64 key1 = 0, bool is_reference_128_ = false) /// NOLINT
    {
        v0 = 0x736f6d6570736575ULL ^ key0;
        v1 = 0x646f72616e646f6dULL ^ key1;
        v2 = 0x6c7967656e657261ULL ^ key0;
        v3 = 0x7465646279746573ULL ^ key1;
        is_reference_128 = is_reference_128_;
 
        if (is_reference_128)
            v1 ^= 0xee;
 
        v0_avx = _mm256_set_epi64x(v0, v0, v0, v0);
        v1_avx = _mm256_set_epi64x(v1, v1, v1, v1);
        v2_avx = _mm256_set_epi64x(v2, v2, v2, v2);
        v3_avx = _mm256_set_epi64x(v3, v3, v3, v3);
  
        cnt = 0;
        current_word = 0;
        current_word_1 = 0;
        current_word_2 = 0;
        current_word_3 = 0;
        
        for (int i = 0; i < 4; ++i) {
            v0_arr[i] = v0;
            v1_arr[i] = v1;
            v2_arr[i] = v2;
            v3_arr[i] = v3;
            //v0_arr[i] = v1_arr[i] = v2_arr[i] = v3_arr[i] = cnt_arr[i] = 0;
            cnt_arr[i] = 0;
        }
        current_bytes_avx = _mm256_set_epi64x(0,0,0,0);
    }

    //union {
    //    __m256i current_words;
    //    UInt8 current_bytes_avx[32];
    //};

 
    __attribute__((__target__("avx512vl,avx512f,avx512bw"))) ALWAYS_INLINE void updateSameLength(std::array<const char*, 4> data, UInt64 size)
    {
        UInt64 inc = 0;
        // print("start update same length", v0_avx);

        /// We'll finish to process the remainder of the previous update, if any.
        
        const char* end = data[0] + size;

        if (cnt & 7)
        {
            while (cnt & 7 && inc < size)
            {
                // make current bytes avx register here and hardcode positions, make &=
                UInt8 byte = CURRENT_BYTES_IDX(cnt & 7);
                UInt64 fix_val = MAX64 ^ (255ull << (8 * byte));
                __m256i current_avx = _mm256_set_epi64x(
                    fix_val,
                    fix_val,
                    fix_val,
                    fix_val
                );
                __m256i avx_to_set = _mm256_set_epi64x(
                    static_cast<UInt64>(*data[3]++) << (8 * byte),
                    static_cast<UInt64>(*data[2]++) << (8 * byte),
                    static_cast<UInt64>(*data[1]++) << (8 * byte),
                    static_cast<UInt64>(*data[0]++) << (8 * byte)
                );
                current_bytes_avx &= current_avx;
                current_bytes_avx |= avx_to_set;
                ++cnt;
                ++inc;
            }
 
            if (cnt & 7)
                return;
 
            v3_avx = _mm256_xor_epi64(v3_avx, current_bytes_avx);
            SIPROUND_AVX;
            SIPROUND_AVX;
            v0_avx = _mm256_xor_epi64(v0_avx, current_bytes_avx);
        }
 
        cnt += size - inc;

        //print("after preprocessing", v0_avx);
        //auto start = clock();
        //UInt64 padding = 0;
        while (data[0] + 8 <= end)
        {
             current_bytes_avx = _mm256_set_epi64x(
                unalignedLoadLittleEndian<UInt64>(data[3]),
                unalignedLoadLittleEndian<UInt64>(data[2]),
                unalignedLoadLittleEndian<UInt64>(data[1]),
                unalignedLoadLittleEndian<UInt64>(data[0])
            );
            data[0] += 8;
            data[1] += 8;
            data[2] += 8;
            data[3] += 8;


            v3_avx = _mm256_xor_epi64(v3_avx, current_bytes_avx);
            SIPROUND_AVX;
            SIPROUND_AVX;
            //print("check v0_avx after iteration before XOR", v0_avx);

            v0_avx = _mm256_xor_epi64(v0_avx, current_bytes_avx);
            //print("check v0_avx after iteration", v0_avx);
            inc += 8;
            //padding += 8;
        }

       // std::cout << "dfadafds 1337 " << clock() - start << '\n';

        //for (auto& d : data) {
        //    d.data += padding;
        //}

        current_bytes_avx = _mm256_set_epi64x(0,0,0,0);

        /// Pad the remainder, which is missing up to an 8-byte word.
        switch (end - data[0])
        {
            case 7: {
                UInt64 x = 8 * CURRENT_BYTES_IDX(6);
                current_bytes_avx |= _mm256_set_epi64x(static_cast<UInt64>(data[3][6]) << x, static_cast<UInt64>(data[2][6]) << x, static_cast<UInt64>(data[1][6]) << x, static_cast<UInt64>(data[0][6]) << x);
            } [[fallthrough]];
            case 6: {
                UInt64 x = 8 * CURRENT_BYTES_IDX(5);
                current_bytes_avx |= _mm256_set_epi64x(static_cast<UInt64>(data[3][5]) << x, static_cast<UInt64>(data[2][5]) << x, static_cast<UInt64>(data[1][5]) << x, static_cast<UInt64>(data[0][5]) << x);
            } [[fallthrough]];
            case 5: {
                UInt64 x = 8 * CURRENT_BYTES_IDX(4);
                current_bytes_avx |= _mm256_set_epi64x(static_cast<UInt64>(data[3][4]) << x, static_cast<UInt64>(data[2][4]) << x, static_cast<UInt64>(data[1][4]) << x, static_cast<UInt64>(data[0][4]) << x);
            } [[fallthrough]];
            case 4: { 
                UInt64 x = 8 * CURRENT_BYTES_IDX(3);
                current_bytes_avx |= _mm256_set_epi64x(static_cast<UInt64>(data[3][3]) << x, static_cast<UInt64>(data[2][3]) << x, static_cast<UInt64>(data[1][3]) << x, static_cast<UInt64>(data[0][3]) << x);
            }[[fallthrough]];
            case 3: {
                UInt64 x = 8 * CURRENT_BYTES_IDX(2);
                current_bytes_avx |= _mm256_set_epi64x(static_cast<UInt64>(data[3][2]) << x, static_cast<UInt64>(data[2][2]) << x, static_cast<UInt64>(data[1][2]) << x, static_cast<UInt64>(data[0][2]) << x);
            }[[fallthrough]];
            case 2: {
                UInt64 x = 8 * CURRENT_BYTES_IDX(1);
                current_bytes_avx |= _mm256_set_epi64x(static_cast<UInt64>(data[3][1]) << x, static_cast<UInt64>(data[2][1]) << x, static_cast<UInt64>(data[1][1]) << x, static_cast<UInt64>(data[0][1]) << x);
            }[[fallthrough]];
            case 1: {
                UInt64 x = 8 * CURRENT_BYTES_IDX(0);
                current_bytes_avx |= _mm256_set_epi64x(static_cast<UInt64>(data[3][0]) << x, static_cast<UInt64>(data[2][0]) << x, static_cast<UInt64>(data[1][0]) << x, static_cast<UInt64>(data[0][0]) << x);

            } [[fallthrough]];
            case 0: break;
        }
    }

    __attribute__((__target__("avx512vl,avx512f,avx512bw"))) ALWAYS_INLINE bool updateAllLength(std::array<const char*, 4> data, std::array<UInt64, 4> szs)
    {
        UInt64 inc = 0;
        const char* end[4] = {};
        for (int i = 0; i < 4; ++i) {
            end[i] = data[i] + szs[i];
        }

        // assuming strings are in a sorted order
       
        // process while (min(sz{}) + 8 < )
        size_t min_sz = szs[0];
        if (min_sz < cnt) {
            return false;
        }
        
        if (cnt & 7) {
            while (cnt & 7) {
                for (size_t i = 0; i < data.size(); ++i) {
                    if (i == 0) {
                        current_bytes[CURRENT_BYTES_IDX(cnt & 7)] = *data[i];
                    } else if (i == 1) {
                        current_bytes_1[CURRENT_BYTES_IDX(cnt & 7)] = *data[i];
                    } else if (i == 2) {
                        current_bytes_2[CURRENT_BYTES_IDX(cnt & 7)] = *data[i];
                    } else {
                        current_bytes_3[CURRENT_BYTES_IDX(cnt & 7)] = *data[i];
                    }
                    ++data[i];
                    cnt_arr[i]++;
                }
                ++cnt;
                ++inc;
            }
            for (int i = 0; i < 4; ++i) {
                v3_arr[i] ^= (i == 0 ? current_word : (i == 1 ? current_word_1 : (i == 2 ? current_word_2 : current_word_3)));
                SIPROUND_ARR(i);
                SIPROUND_ARR(i);
                v0_arr[i] ^= (i == 0 ? current_word : (i == 1 ? current_word_1 : (i == 2 ? current_word_2 : current_word_3)));
            }
        }

        v0_avx = _mm256_set_epi64x(v0_arr[3], v0_arr[2], v0_arr[1], v0_arr[0]);
        v1_avx = _mm256_set_epi64x(v1_arr[3], v1_arr[2], v1_arr[1], v1_arr[0]);
        v2_avx = _mm256_set_epi64x(v2_arr[3], v2_arr[2], v2_arr[1], v2_arr[0]);
        v3_avx = _mm256_set_epi64x(v3_arr[3], v3_arr[2], v3_arr[1], v3_arr[0]);

        cnt_arr[0] = cnt_arr[1] = cnt_arr[2] = cnt_arr[3] = cnt;
        cnt_arr[0] += end[0] - data[0];
        cnt_arr[1] += end[1] - data[1];
        cnt_arr[2] += end[2] - data[2];
        cnt_arr[3] += end[3] - data[3];
        
        UInt64 padding = 0;
        while (inc + 8 <= min_sz)
        {
            current_bytes_avx = _mm256_set_epi64x(
                unalignedLoadLittleEndian<UInt64>(data[3] + padding),
                unalignedLoadLittleEndian<UInt64>(data[2] + padding),
                unalignedLoadLittleEndian<UInt64>(data[1] + padding),
                unalignedLoadLittleEndian<UInt64>(data[0] + padding)
            );
            //print("after iteration upload, current bytes", current_bytes_avx);

            v3_avx ^= current_bytes_avx;
            SIPROUND_AVX;
            SIPROUND_AVX;
            //print("after iteration SIPROUND_AVX", current_bytes_avx);
            //print("check v0_avx after iteration before XOR", v0_avx);
            v0_avx ^= current_bytes_avx;

            //print("check v0_avx after iteration", v0_avx);
            inc += 8;
            padding += 8;
        }

        
        for (auto& d : data) {
            d += padding;
        }

        current_bytes_avx = _mm256_set_epi64x(0,0,0,0);

        extractCF();
        //std::cout << "all lengths register: " << v0_arr[0] << ' ' << v0_arr[1] << ' ' << v0_arr[2] << ' ' << v0_arr[3] << std::endl; 

        for (int i = 0; i < 4; ++i) {
            while (data[i] + 8 <= end[i]) {
                current_word = unalignedLoadLittleEndian<UInt64>(data[i]);

                v3_arr[i] ^= current_word;
                SIPROUND_ARR(i);
                SIPROUND_ARR(i);
                v0_arr[i] ^= current_word;

                data[i] += 8;
            }
        }

        //std::cout << "all lengths register: " << v0_arr[0] << ' ' << v0_arr[1] << ' ' << v0_arr[2] << ' ' << v0_arr[3] << std::endl; 

        current_word = current_word_1 = current_word_2 = current_word_3 = 0;
        for (int i = 0; i < 4; ++i) {
            UInt8* tmp;
            if (i == 0) {
                tmp = current_bytes;
            } else if (i == 1) {
                tmp = current_bytes_1;
            } else if (i == 2) {
                tmp = current_bytes_2;
            } else {
                tmp = current_bytes_3;
            }
            switch (end[i] - data[i])
            {
                case 7: tmp[CURRENT_BYTES_IDX(6)] = data[i][6]; [[fallthrough]];
                case 6: tmp[CURRENT_BYTES_IDX(5)] = data[i][5]; [[fallthrough]];
                case 5: tmp[CURRENT_BYTES_IDX(4)] = data[i][4]; [[fallthrough]];
                case 4: tmp[CURRENT_BYTES_IDX(3)] = data[i][3]; [[fallthrough]];
                case 3: tmp[CURRENT_BYTES_IDX(2)] = data[i][2]; [[fallthrough]];
                case 2: tmp[CURRENT_BYTES_IDX(1)] = data[i][1]; [[fallthrough]];
                case 1: tmp[CURRENT_BYTES_IDX(0)] = data[i][0]; [[fallthrough]];
                case 0: break;
            }
        }

        return true;
    }

    ALWAYS_INLINE void update(const char * data, UInt64 size)
    {
        const char * end = data + size;

        /// We'll finish to process the remainder of the previous update, if any.
        if (cnt & 7)
        {
            while (cnt & 7 && data < end)
            {
                current_bytes[CURRENT_BYTES_IDX(cnt & 7)] = *data;
                ++data;
                ++cnt;
            }

            /// If we still do not have enough bytes to an 8-byte word.
            if (cnt & 7)
                return;

            v3 ^= current_word;
            SIPROUND;
            SIPROUND;
            v0 ^= current_word;
        }
       // std::cout << " after start update: " << v0 << std::endl;

        cnt += end - data;
        //auto start = clock();
        while (data + 8 <= end)
        {
            current_word = unalignedLoadLittleEndian<UInt64>(data);

            v3 ^= current_word;
            SIPROUND;
            SIPROUND;
      //      std::cout << " v0 after iteration before xor: " << v0 << std::endl;
            v0 ^= current_word;

       //     std::cout << " v0 after iteration after xor: " << v0 << std::endl;

            data += 8;
        }
       //std::cout << "dfadafds 228 " << clock() - start << '\n';

        /// Pad the remainder, which is missing up to an 8-byte word.
        current_word = 0;
        switch (end - data)
        {
            case 7: current_bytes[CURRENT_BYTES_IDX(6)] = data[6]; [[fallthrough]]; 
            case 6: current_bytes[CURRENT_BYTES_IDX(5)] = data[5]; [[fallthrough]];
            case 5: current_bytes[CURRENT_BYTES_IDX(4)] = data[4]; [[fallthrough]];
            case 4: current_bytes[CURRENT_BYTES_IDX(3)] = data[3]; [[fallthrough]];
            case 3: current_bytes[CURRENT_BYTES_IDX(2)] = data[2]; [[fallthrough]];
            case 2: current_bytes[CURRENT_BYTES_IDX(1)] = data[1]; [[fallthrough]];
            case 1: current_bytes[CURRENT_BYTES_IDX(0)] = data[0]; [[fallthrough]];
            case 0: break;
        }
    }
 
    template <typename Transform = void, typename T>
    ALWAYS_INLINE void update(const T & x)
    {
        if constexpr (std::endian::native == std::endian::big)
        {
            auto transformed_x = x;
            if constexpr (!std::is_same_v<Transform, void>)
                transformed_x = Transform()(x);
            else
                DB::transformEndianness<std::endian::little>(transformed_x);
 
            update(reinterpret_cast<const char *>(&transformed_x), sizeof(transformed_x)); /// NOLINT
        }
        else
            update(reinterpret_cast<const char *>(&x), sizeof(x)); /// NOLINT
    }

    template <typename Transform = void, typename T>
    ALWAYS_INLINE void updateSameLength(const std::array<T, 4> & v)
    {
        if constexpr (std::endian::native == std::endian::big)
        {
            std::array<T, 4> result = v;
            if constexpr (!std::is_same_v<Transform, void>)
                for (int i = 0; i < 4; ++i) {
                    result[i] = Transform()(v[i]);
                }
            else
                DB::transformEndianness<std::endian::little>(result);
 
            updateSameLength(result); /// NOLINT
        }
        else
            updateSameLength(v); /// NOLINT
    }

 
    ALWAYS_INLINE void update(const std::string & x) { update(x.data(), x.length()); }
    ALWAYS_INLINE void update(const std::string_view x) { update(x.data(), x.size()); }
    ALWAYS_INLINE void update(const char * s) { update(std::string_view(s)); }

    ALWAYS_INLINE void updateSameLength(std::array<std::string, 4> & x) { updateSameLength({x[0].data(), x[1].data(), x[2].data(), x[3].data()}, x[0].size()); }
    ALWAYS_INLINE void updateSameLength(std::array<std::string_view, 4>& x) { updateSameLength({x[0].data(), x[1].data(), x[2].data(), x[3].data()}, x[0].size()); }
    ALWAYS_INLINE void updateSameLength(std::array<const char*, 4>& x) { updateSameLength(x, strlen(x[0]));}


    __attribute__((__target__("avx512vl,avx512f,avx512bw"))) ALWAYS_INLINE void extractCF()
    {
        auto fill_vx_array = [](__m256i& avx_reg, UInt64* v) {
            v[0] = static_cast<UInt64>(_mm256_extract_epi64(avx_reg, 0));
            v[1] = static_cast<UInt64>(_mm256_extract_epi64(avx_reg, 1));
            v[2] = static_cast<UInt64>(_mm256_extract_epi64(avx_reg, 2));
            v[3] = static_cast<UInt64>(_mm256_extract_epi64(avx_reg, 3));
        };

        fill_vx_array(v0_avx, v0_arr);
        fill_vx_array(v1_avx, v1_arr);
        fill_vx_array(v2_avx, v2_arr);
        fill_vx_array(v3_avx, v3_arr);
    }

    ALWAYS_INLINE UInt64 get64()
    {
        finalize();
        return v0 ^ v1 ^ v2 ^ v3;
    }

    __attribute__((__target__("avx512vl,avx512f,avx512bw"))) ALWAYS_INLINE std::array<UInt64, 4> get64Array()
    {
        finalize_array();

        __m256i res = v0_avx ^ v1_avx ^ v2_avx ^ v3_avx;

        return {static_cast<UInt64>(_mm256_extract_epi64(res, 0)), static_cast<UInt64>(_mm256_extract_epi64(res, 1)),static_cast<UInt64>(_mm256_extract_epi64(res, 2)), static_cast<UInt64>(_mm256_extract_epi64(res, 3))};
    }

    __attribute__((__target__("avx512vl,avx512f,avx512bw"))) ALWAYS_INLINE std::array<UInt64, 4> get64ArrayAllLength()
    {
        finalize_array_all_len();
        std::array<UInt64, 4> result = {};
        for (int i = 0; i < 4; ++i) {
            result[i] = v0_arr[i] ^ v1_arr[i] ^ v2_arr[i] ^ v3_arr[i];
        }
        //print("before drop result", res);
        return result;
    }

 
    template <typename T>
    requires (sizeof(T) == 8)
    ALWAYS_INLINE void get128(T & lo, T & hi)
    {
        finalize();
        lo = v0 ^ v1;
        hi = v2 ^ v3;
    }
 
    ALWAYS_INLINE UInt128 get128()
    {
        UInt128 res;
        get128(res.items[UInt128::_impl::little(0)], res.items[UInt128::_impl::little(1)]);
        return res;
    }
 
    UInt128 get128Reference()
    {
        if (!is_reference_128)
            throw DB::Exception(
                DB::ErrorCodes::LOGICAL_ERROR, "Logical error: can't call get128Reference when is_reference_128 is not set");
        finalize();
        const auto lo = v0 ^ v1 ^ v2 ^ v3;
        v1 ^= 0xdd;
        SIPROUND;
        SIPROUND;
        SIPROUND;
        SIPROUND;
        const auto hi = v0 ^ v1 ^ v2 ^ v3;
 
        UInt128 res = hi;
        res <<= 64;
        res |= lo;
        return res;
    }
};
 
 
#undef ROTL
#undef SIPROUND
 
#include <cstddef>
 
inline std::array<char, 16> getSipHashAvx128AsArray(SipHashAvx & sip_hash)
{
    std::array<char, 16> arr;
    *reinterpret_cast<UInt128*>(arr.data()) = sip_hash.get128();
    return arr;
}
 
inline CityHash_v1_0_2::uint128 getSipHashAvx128AsPair(SipHashAvx & sip_hash)
{
    CityHash_v1_0_2::uint128 result;
    sip_hash.get128(result.low64, result.high64);
    return result;
}
 
inline UInt128 SipHashAvx128Keyed(UInt64 key0, UInt64 key1, const char * data, const size_t size)
{
    SipHashAvx hash(key0, key1);
    hash.update(data, size);
    return hash.get128();
}
 
inline UInt128 SipHashAvx128(const char * data, const size_t size)
{
    return SipHashAvx128Keyed(0, 0, data, size);
}
 
inline String SipHashAvx128String(const char * data, const size_t size)
{
    return getHexUIntLowercase(SipHashAvx128(data, size));
}
 
inline String SipHashAvx128String(const String & str)
{
    return SipHashAvx128String(str.data(), str.size());
}
 
inline UInt128 SipHashAvx128ReferenceKeyed(UInt64 key0, UInt64 key1, const char * data, const size_t size)
{
    SipHashAvx hash(key0, key1, true);
    hash.update(data, size);
    return hash.get128Reference();
}
 
inline UInt128 SipHashAvx128Reference(const char * data, const size_t size)
{
    return SipHashAvx128ReferenceKeyed(0, 0, data, size);
}
 
inline UInt64 SipHashAvx64Keyed(UInt64 key0, UInt64 key1, const char * data, const size_t size)
{
    SipHashAvx hash(key0, key1);
    hash.update(data, size);
    return hash.get64();
}

__attribute__((__target__("avx512vl,avx512f,avx512bw"))) inline std::array<UInt64, 4> SipHashAvx64ArrayStr(std::array<const char*, 4> data, const size_t size)
{
    //std::cout << " constructor " << std::endl;
    SipHashAvx hash;
    //std::cout << " start func " << std::endl;
    hash.updateSameLength(data, size);
    return hash.get64Array();
}

__attribute__((__target__("avx512vl,avx512f,avx512bw"))) inline std::array<UInt64, 4> SipHashAvx64ArrayStrAllLength(std::array<const char*, 4> data, std::array<size_t, 4> szs)
{
    //std::cout << " constructor " << std::endl;
    SipHashAvx hash;
    //std::cout << " start func " << std::endl;
    hash.updateAllLength(data, szs);
    return hash.get64ArrayAllLength();
}

 
inline UInt64 SipHashAvx64(const char * data, const size_t size)
{
    return SipHashAvx64Keyed(0, 0, data, size);
}
 
template <typename T>
inline UInt64 SipHashAvx64(const T & x)
{
    SipHashAvx hash;
    hash.update(x);
    return hash.get64();
}

template <typename T>
inline UInt64 SipHashAvx64Array(const std::array<T, 4> & x)
{
    SipHashAvx hash;
    hash.updateSameLength(x);
    return hash.get64();
}

 
inline UInt64 SipHashAvx64(const std::string & s)
{
    return SipHashAvx64(s.data(), s.size());
}
 
#undef CURRENT_BYTES_IDX
