#include <array>
#include <cstddef>
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



std::string get_string(size_t cnt) {
    std::string res;
    std::mt19937 rnd(time(nullptr));
    for (size_t i = 0; i < cnt; ++i) {
        res += static_cast<char>(rnd() % 26 + static_cast<int>('a'));
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
    check_speed_default(cnt);
    check_speed_avx_same(cnt);
    check_speed_avx_diff(cnt);
    std::cout << "==== Speed test end ====\n";
}


std::vector<std::string> gen_same_sz_vec(size_t vec_sz) {
    std::vector<std::string> v(vec_sz);
    std::mt19937 rnd(time(nullptr));
    size_t str_sz = 1000 + rnd() % 10000;
    for (size_t i = 0; i < vec_sz; ++i) {
        v.push_back(get_string(str_sz));
    }
    return v;
}

void check_speed_avx_same(std::vector<std::string>& v) {
    auto start = clock();
    for (size_t i = 0; i < v.size(); i += 4) {
        auto result = SipHashAvx64ArrayStr({v[0].data(), v[1].data(), v[2].data(), v[3].data()}, v[0].size());
        (void)result;
    }
    std::cout << clock() - start << '\n';
}

void check_speed_avx_diff(std::vector<std::string>& v) {
    auto start = clock();
    for (size_t i = 0; i < v.size(); i += 4) {
        auto result = SipHashAvx64ArrayStrAllLength({v[0].data(), v[1].data(), v[2].data(), v[3].data()}, {v[0].size(), v[1].size(), v[2].size(), v[3].size()});
        (void)result;
    }
    std::cout << clock() - start << '\n';
}

void check_speed_default(std::vector<std::string>& v) {
    auto start = clock();
    for (int i = 0; i < 4; ++i) {
        auto result = SipHashAvx64(v[i].data(), v[i].size());
        (void)result;
    }
    std::cout << clock() - start << '\n';
}


void check_speed_vec(size_t cnt) {
    auto v = gen_same_sz_vec(cnt);
    std::cout << "Speed check with vector of size cnt = " << cnt << '\n';
    check_speed_default(v);
    check_speed_avx_same(v);
    check_speed_avx_diff(v);
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


    check_speed_vec(12);
    check_speed_vec(100);
    check_speed_vec(200);
    check_speed_vec(1000);
    return 0;
}
