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
    std::mt19937 rnd(clock());
    std::string res;
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
        result = SipHashAvx64(arr[i]);
        (void)result;
    }
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
    std::vector<std::string> v;
    std::mt19937 rnd(time(nullptr));
    size_t str_sz = 1000;
    for (size_t i = 0; i < vec_sz; ++i) {
        v.push_back(get_string(str_sz));
    }
    std::sort(v.begin(), v.end(), [&](const auto& x, const auto& y) {
        return x.size() < y.size();
    });
    return v;
}

void check_speed_avx_same(std::vector<std::string>& v) {
    auto start = clock();
    for (size_t i = 0; i < v.size(); i += 4) {
        auto result = SipHashAvx64ArrayStr({v[i].data(), v[1 + i].data(), v[2 + i].data(), v[3 + i].data()}, v[i].size());
        (void)result;
    }
    std::cout << clock() - start << '\n';
}

void check_speed_avx_diff(std::vector<std::string>& v) {
    auto start = clock();
    for (size_t i = 0; i < v.size(); i += 4) {
        auto result = SipHashAvx64ArrayStrAllLength({v[i].data(), v[i + 1].data(), v[i + 2].data(), v[i + 3].data()}, {v[i + 0].size(), v[i + 1].size(), v[i + 2].size(), v[i + 3].size()});
        (void)result;
    }
    std::cout << clock() - start << '\n';
}

void check_speed_default(std::vector<std::string>& v) {
    auto start = clock();
    for (size_t i = 0; i < v.size(); ++i) {
        auto result = SipHashAvx64(v[i].data(), v[i].size());
        (void)result;
    }
    std::cout << clock() - start << '\n';
}

std::vector<UInt64> check_correctness_avx_same(std::vector<std::string>& v) {
    std::vector<UInt64> result(v.size()); 
    for (size_t i = 0; i < v.size(); i += 4) {
        auto x = SipHashAvx64ArrayStr({v[i].data(), v[i + 1].data(), v[i + 2].data(), v[i + 3].data()}, v[i].size());
        result[i] = x[0];
        result[i + 1] = x[1];
        result[i + 2] = x[2];
        result[i + 3] = x[3];
    }
    return result;
}

std::vector<UInt64> check_correctness_avx_diff(std::vector<std::string>& v) {
    std::vector<UInt64> result(v.size()); 
    for (size_t i = 0; i < v.size(); i += 4) {
        auto x = SipHashAvx64ArrayStrAllLength({v[i].data(), v[i + 1].data(), v[i + 2].data(), v[i + 3].data()}, {v[i].size(), v[i + 1].size(), v[i + 2].size(), v[i + 3].size()});
        result[i] = x[0];
        result[i + 1] = x[1];
        result[i + 2] = x[2];
        result[i + 3] = x[3];
    }
    return result;
}

std::vector<UInt64> check_correctness_default(std::vector<std::string>& v) {
    std::vector<UInt64> result(v.size()); 
    for (size_t i = 0; i < result.size(); ++i) {
        result[i] = SipHashAvx64(v[i].data(), v[i].size());
    }
    return result;
}

void check_speed_vec(size_t cnt) {
    auto v = gen_same_sz_vec(cnt);
    std::cout << "Speed check with vector of size cnt = " << cnt << '\n';
    check_speed_default(v);
    check_speed_avx_same(v);
    check_speed_avx_diff(v);
    std::cout << "==== Speed test end ====\n";
}


void check_correctness(size_t cnt) {
    auto v = gen_same_sz_vec(cnt);
    auto x = check_correctness_avx_same(v);
    auto y = check_correctness_default(v);
    std::cout << cnt << ' ' << v.size() << '\n';
    for (size_t i = 0; i < v.size(); ++i) {
        std::cout << x[i] << ' ' << y[i] << '\n';
    }
    assert(check_correctness_avx_same(v) == check_correctness_default(v));
    assert(check_correctness_avx_diff(v) == check_correctness_default(v));
}


int mainEntryClickHouseSipHashAVX(int argc, char ** argv) {
    (void)argc;
    (void)argv;

   /* {
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
    }


    {
        check_speed_vec(12);
        check_speed_vec(100);
        check_speed_vec(200);
        check_speed_vec(1000);
        check_speed_vec(10000);
    }*/

    {
        check_correctness(12);
        check_correctness(20);
        check_correctness(200);
        check_correctness(400);

    }

    return 0;
}
