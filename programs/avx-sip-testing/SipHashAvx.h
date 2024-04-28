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
 
 
#include <array>
#include <bit>
#include <string>
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
 
class SipHash
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
 
    ALWAYS_INLINE void finalize()
    {
        /// In the last free byte, we write the remainder of the division by 256.
        current_bytes[CURRENT_BYTES_IDX(7)] = static_cast<UInt8>(cnt);
 
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

    __attribute__((__target__("avx512vl,avx512f,avx512bw"))) ALWAYS_INLINE void finalize_array()
    {
        /// In the last free byte, we write the remainder of the division by 256.
        UInt64 x = 8 * CURRENT_BYTES_IDX(7);
        UInt64 vl = MAX64 ^ (255ull << x);
        current_bytes_avx &= _mm256_set_epi64x(vl, vl, vl, vl);
        current_bytes_avx |= _mm256_set_epi64x(cnt << x, cnt << x, cnt << x, cnt << x);

        v3_avx ^= current_bytes_avx;
        SIPROUND_AVX;
        SIPROUND_AVX;
        v0_avx ^= current_bytes_avx;
 
        v2_avx ^= _mm256_set_epi64x(0xff, 0xff, 0xff, 0xff);
 
        SIPROUND_AVX;
        SIPROUND_AVX;
        SIPROUND_AVX;
        SIPROUND_AVX;
    }


public:
    struct StringPtr {
        const char* data;
    };
 
    /// Arguments - seed.
    __attribute__((__target__("avx512vl,avx512f,avx512bw"))) SipHash(UInt64 key0 = 0, UInt64 key1 = 0, bool is_reference_128_ = false) /// NOLINT
    {
        /// Initialize the state with some random bytes and seed.
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
 
        print("after fill", v0_avx);
 
        cnt = 0;
        current_word = 0;
        current_bytes_avx = _mm256_set_epi64x(0,0,0,0);
    }

    //union {
    //    __m256i current_words;
    //    UInt8 current_bytes_avx[32];
    //};

 
    __attribute__((__target__("avx512vl,avx512f,avx512bw"))) ALWAYS_INLINE void updateSameLength(std::array<StringPtr, 4> data, UInt64 size)
    {
        UInt64 inc = 0;
         print("start update same length", v0_avx);

        /// We'll finish to process the remainder of the previous update, if any.

        /// попробуем тут обрабатывать втупую для каждой строчки
        
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
                    (*data[0].data++) << (8 * byte),
                    (*data[1].data++) << (8 * byte),
                    (*data[2].data++) << (8 * byte),
                    (*data[3].data++) << (8 * byte)
                );
                current_bytes_avx &= current_avx;
                current_bytes_avx |= avx_to_set;
                //current_bytes_avx = _mm256_and_epi64(current_bytes_avx, current_avx);
                //current_bytes_avx = _mm256_or_epi64(current_bytes_avx, avx_to_set);
                ++cnt;
                ++inc;
            }
 
            /// If we still do not have enough bytes to an 8-byte word.
            if (cnt & 7)
                return;
 
            v3_avx ^= current_bytes_avx;
            SIPROUND_AVX;
            SIPROUND_AVX;
            v0_avx ^= current_bytes_avx;
        }
 
        cnt += size - inc;

        print("after preprocessing", v0_avx);
 
        UInt64 padding = 0;
        while (inc + 8 <= size)
        {
            current_bytes_avx = _mm256_set_epi64x(
                unalignedLoadLittleEndian<UInt64>(data[0].data),
                unalignedLoadLittleEndian<UInt64>(data[1].data),
                unalignedLoadLittleEndian<UInt64>(data[2].data),
                unalignedLoadLittleEndian<UInt64>(data[3].data)
            );
            print("after iteration upload, current bytes", current_bytes_avx);

        v0_avx = _mm256_add_epi64(v0_avx, v1_avx);
            print("check v0_avx after iteration before XOR", v0_avx);

        v1_avx = _mm256_rol_epi64(v1_avx, 13);                      
        v1_avx = _mm256_xor_epi64(v1_avx, v0_avx);                  
        v0_avx = _mm256_rol_epi64(v0_avx, 32);     
            print("check v0_avx after iteration before XOR", v0_avx);

        v2_avx = _mm256_add_epi64(v2_avx, v3_avx);                  
        v3_avx = _mm256_rol_epi64(v3_avx, 16);                      
        v3_avx = _mm256_xor_epi64(v3_avx, v2_avx);                  
        v0_avx = _mm256_add_epi64(v0_avx, v3_avx);   
            print("check v0_avx after iteration before XOR", v0_avx);

        v3_avx = _mm256_rol_epi64(v3_avx, 21);                      
        v3_avx = _mm256_xor_epi64(v3_avx, v0_avx);                  
        v2_avx = _mm256_add_epi64(v2_avx, v1_avx);                  
        v1_avx = _mm256_rol_epi64(v1_avx, 17);                      
        v1_avx = _mm256_xor_epi64(v1_avx, v2_avx);                  
        v2_avx = _mm256_rol_epi64(v2_avx, 32);                      



//            SIPROUND_AVX;
            print("after iteration SIPROUND_AVX", current_bytes_avx);
            print("check v0_avx after iteration before XOR", v0_avx);


            v0_avx ^= current_bytes_avx;
 
            print("check v0_avx after iteration", v0_avx);
            inc += 8;
            padding += 8;
        }

        for (auto& d : data) {
            d.data += padding;
        }
 
        /// Pad the remainder, which is missing up to an 8-byte word.
        switch (size - inc)
        {
            case 7: {
                UInt64 x = 8 * CURRENT_BYTES_IDX(6);
                UInt64 vl = MAX64 ^ (255ull << x);
                current_bytes_avx &= _mm256_set_epi64x(vl, vl, vl, vl);
                current_bytes_avx |= _mm256_set_epi64x(data[0].data[6] << x, data[1].data[6] << x, data[2].data[6] << x, data[3].data[6] << x);
            } [[fallthrough]];
            case 6: {
                UInt64 x = 8 * CURRENT_BYTES_IDX(5);
                UInt64 vl = MAX64 ^ (255ull << x);
                current_bytes_avx &= _mm256_set_epi64x(vl, vl, vl, vl);
                current_bytes_avx |= _mm256_set_epi64x(data[0].data[5] << x, data[1].data[5] << x, data[2].data[5] << x, data[3].data[5] << x);

            } [[fallthrough]];
            case 5: {
                UInt64 x = 8 * CURRENT_BYTES_IDX(4);
                UInt64 vl = MAX64 ^ (255ull << x);
                current_bytes_avx &= _mm256_set_epi64x(vl, vl, vl, vl);
                current_bytes_avx |= _mm256_set_epi64x(data[0].data[4]<< x, data[1].data[4]<< x, data[2].data[4]<< x, data[3].data[4]<< x);
            } [[fallthrough]];
            case 4: { 
                UInt64 x = 8 * CURRENT_BYTES_IDX(3);
                UInt64 vl = MAX64 ^ (255ull << x);
                current_bytes_avx &= _mm256_set_epi64x(vl, vl, vl, vl);
                current_bytes_avx |= _mm256_set_epi64x(data[0].data[3]<< x, data[1].data[3]<< x, data[2].data[3]<< x, data[3].data[3]<< x);
            }[[fallthrough]];
            case 3: {
                UInt64 x = 8 * CURRENT_BYTES_IDX(2);
                UInt64 vl = MAX64 ^ (255ull << x);
                current_bytes_avx &= _mm256_set_epi64x(vl, vl, vl, vl);
                current_bytes_avx |= _mm256_set_epi64x(data[0].data[2]<< x, data[1].data[2]<< x, data[2].data[2]<< x, data[3].data[2]<< x);
            }[[fallthrough]];
            case 2: {
                UInt64 x = 8 * CURRENT_BYTES_IDX(1);
                UInt64 vl = MAX64 ^ (255ull << x);
                current_bytes_avx &= _mm256_set_epi64x(vl, vl, vl, vl);
                current_bytes_avx |= _mm256_set_epi64x(data[0].data[1]<< x, data[1].data[1]<< x, data[2].data[1]<< x, data[3].data[1]<< x);
            }[[fallthrough]];
            case 1: {
                UInt64 x = 8 * CURRENT_BYTES_IDX(0);
                UInt64 vl = MAX64 ^ (255ull << x);
                current_bytes_avx &= _mm256_set_epi64x(vl, vl, vl, vl);
                current_bytes_avx |= _mm256_set_epi64x(data[0].data[0]<< x, data[1].data[0]<< x, data[2].data[0]<< x, data[3].data[0]<< x);
            } [[fallthrough]];
            case 0: break;
        }
        print("after update current bytes avx", current_bytes_avx);
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

        cnt += end - data;

        while (data + 8 <= end)
        {
            current_word = unalignedLoadLittleEndian<UInt64>(data);

            v3 ^= current_word;
            SIPROUND;
            SIPROUND;
            v0 ^= current_word;

            data += 8;
        }

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
 
    ALWAYS_INLINE void update(const std::string & x) { update(x.data(), x.length()); }
    ALWAYS_INLINE void update(const std::string_view x) { update(x.data(), x.size()); }
    ALWAYS_INLINE void update(const char * s) { update(std::string_view(s)); }
 
    ALWAYS_INLINE UInt64 get64()
    {
        finalize();
        return v0 ^ v1 ^ v2 ^ v3;
    }

    __attribute__((__target__("avx512vl,avx512f,avx512bw"))) ALWAYS_INLINE std::array<UInt64, 4> get64Array()
    {
        print("before finalize v0_avx", v0_avx);
        print("before finalize v1_avx", v1_avx);
        print("before finalize v2_avx", v2_avx);
        print("before finalize v3_avx", v3_avx);

        finalize_array();
        print("after finalize v0_avx", v0_avx);
        print("after finalize v1_avx", v1_avx);
        print("after finalize v2_avx", v2_avx);
        print("after finalize v3_avx", v3_avx);

        __m256i res = v0_avx ^ v1_avx ^ v2_avx ^ v3_avx;

        print("before drop result", res);
        return {static_cast<UInt64>(_mm256_extract_epi64(res, 0)), static_cast<UInt64>(_mm256_extract_epi64(res, 1)),static_cast<UInt64>(_mm256_extract_epi64(res, 2)), static_cast<UInt64>(_mm256_extract_epi64(res, 3))};
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
 
inline std::array<char, 16> getSipHash128AsArray(SipHash & sip_hash)
{
    std::array<char, 16> arr;
    *reinterpret_cast<UInt128*>(arr.data()) = sip_hash.get128();
    return arr;
}
 
inline CityHash_v1_0_2::uint128 getSipHash128AsPair(SipHash & sip_hash)
{
    CityHash_v1_0_2::uint128 result;
    sip_hash.get128(result.low64, result.high64);
    return result;
}
 
inline UInt128 sipHash128Keyed(UInt64 key0, UInt64 key1, const char * data, const size_t size)
{
    SipHash hash(key0, key1);
    hash.update(data, size);
    return hash.get128();
}
 
inline UInt128 sipHash128(const char * data, const size_t size)
{
    return sipHash128Keyed(0, 0, data, size);
}
 
inline String sipHash128String(const char * data, const size_t size)
{
    return getHexUIntLowercase(sipHash128(data, size));
}
 
inline String sipHash128String(const String & str)
{
    return sipHash128String(str.data(), str.size());
}
 
inline UInt128 sipHash128ReferenceKeyed(UInt64 key0, UInt64 key1, const char * data, const size_t size)
{
    SipHash hash(key0, key1, true);
    hash.update(data, size);
    return hash.get128Reference();
}
 
inline UInt128 sipHash128Reference(const char * data, const size_t size)
{
    return sipHash128ReferenceKeyed(0, 0, data, size);
}
 
inline UInt64 sipHash64Keyed(UInt64 key0, UInt64 key1, const char * data, const size_t size)
{
    SipHash hash(key0, key1);
    hash.update(data, size);
    return hash.get64();
}

__attribute__((__target__("avx512vl,avx512f,avx512bw"))) inline std::array<UInt64, 4> sipHash64ArrayStr(std::array<const char*, 4> data, const size_t size)
{
    SipHash hash(0, 0);
    std::array<SipHash::StringPtr, 4> data1;
    for (size_t i = 0; i < 4; ++i) {
        data1[i].data = data[i];
    }
    hash.updateSameLength(data1, size);
    return hash.get64Array();
}
 
inline UInt64 sipHash64(const char * data, const size_t size)
{
    return sipHash64Keyed(0, 0, data, size);
}
 
template <typename T>
inline UInt64 sipHash64(const T & x)
{
    SipHash hash;
    hash.update(x);
    return hash.get64();
}

template <typename T>
inline UInt64 sipHash64Array(const std::array<T, 4> & x)
{
    SipHash hash;
    hash.updateSameLength(x);
    return hash.get64();
}

 
inline UInt64 sipHash64(const std::string & s)
{
    return sipHash64(s.data(), s.size());
}
 
#undef CURRENT_BYTES_IDX
