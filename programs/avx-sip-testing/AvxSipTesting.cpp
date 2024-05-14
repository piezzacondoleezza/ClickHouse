#include <array>
#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <queue>
#include <iomanip>
#include <random>
#include <chrono>


#include "SipHashAvx.h"
#include "base/types.h"

#include <random>


std::string get_string(size_t cnt) {
    std::string res;
    for (size_t i = 0; i < cnt; ++i) {
        if (i % 3 == 1) {
            res += 'a';
        } else if (i % 3 == 0) {
            res += 'b';
        } else {
            res += 'c';
        }
        //res += static_cast<char>( % 26 + 'a');
    }
    return res;
}


void check_speed_avx_same(size_t cnt) {
    std::string sample = get_string(cnt);
    std::array<const char*, 4> arr = {sample.data(), sample.data(), sample.data(), sample.data()};
    auto start = clock();
    auto result = SipHashAvx64ArrayStr(arr, cnt);
    (void)result;
    std::cout << clock() - start << '\n';
}

void check_speed_avx_diff(size_t cnt) {
    std::string one = get_string(cnt);
    std::string two = get_string(cnt + 1);
    std::string three = get_string(cnt + 3);
    std::string four = get_string(cnt + 4);

    std::array<const char*, 4> arr_1 = {one.data(), two.data(), three.data(), four.data()};
    std::array<size_t, 4> szs = {one.size(), two.size(), three.size(), four.size()};
    auto start = clock();
    auto result = SipHashAvx64ArrayStrAllLength(arr_1, szs);
    (void)result;
    std::cout << clock() - start << '\n';
}

void check_speed_default(size_t cnt) {
    std::string sample = get_string(cnt);
    std::array<const char*, 4> arr = {sample.data(), sample.data(), sample.data(), sample.data()};
    UInt64 result;
    auto start = clock();
    for (int i = 0; i < 4; ++i) {
        result = SipHashAvx64(arr);
    }
    (void)result;
    std::cout << clock() - start << '\n';
}


void check_speed(size_t cnt) {
    std::cout << "Speed check with cnt = " << cnt << '\n';
    check_speed_avx_same(cnt);
    check_speed_avx_diff(cnt);
    check_speed_default(cnt);
    std::cout << "==== Speed test end ====\n";
}

int mainEntryClickHouseSipHashAVX(int argc, char ** argv) {
    (void)argc;
    (void)argv;


    check_speed(20);
    check_speed(47);
    check_speed(159);
    check_speed(227);
    check_speed(777);
    check_speed(1528);
    check_speed(3152);
    check_speed(7777);
    check_speed(15999);
    check_speed(50001);
    check_speed(100007);
    check_speed(10000007);

    return 0;
}
