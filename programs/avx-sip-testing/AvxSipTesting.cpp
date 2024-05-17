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
    std::mt19937 rnd(clock());
    size_t str_sz = rnd() % 1000 + 1000;
    for (size_t i = 0; i < vec_sz; ++i) {
        v.push_back(get_string(str_sz));
    }
    std::sort(v.begin(), v.end(), [&](const auto& x, const auto& y) {
        return x.size() < y.size();
    });
    return v;
}

std::vector<std::string> gen_vec_rand(size_t vec_sz) {
    std::vector<std::string> v;
    std::mt19937 rnd(clock());
    size_t str_sz = rnd() % 20000 + 5;
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
    assert(x == y);
    assert(check_correctness_avx_diff(v) == check_correctness_default(v));
}

std::vector<UInt64> simple_algo(std::vector<std::string>& v) {
    auto y = clock();
    
    std::vector<UInt64> result(v.size()); 
    for (size_t i = 0; i < result.size(); ++i) {
        result[i] = SipHashAvx64(v[i].data(), v[i].size());
    }

    std::cout << "simple siphash single algo: " << clock() - y << '\n';
    return result;
}


std::vector<UInt64> algo2(std::vector<std::string>& v) {
    auto y = clock();
    
    std::vector<UInt64> result(v.size());

    for (size_t i = 0; i < v.size(); ++i) {
        if (i + 4 <= v.size()) {
            if (v[i + 3].size() == v[i].size()) {
                auto x = SipHashAvx64ArrayStr({v[i].data(), v[i + 1].data(), v[i + 2].data(), v[i + 3].data()}, v[i].size());
                result[i++] = x[0];
                result[i++] = x[1];
                result[i++] = x[2];
                result[i] = x[3];
            } else {
                auto x = SipHashAvx64ArrayStrAllLength({v[i].data(), v[i + 1].data(), v[i + 2].data(), v[i + 3].data()}, {v[i].size(), v[i + 1].size(), v[i + 2].size(), v[i + 3].size()});
                result[i++] = x[0];
                result[i++] = x[1];
                result[i++] = x[2];
                result[i] = x[3];
            }
        } else {
            result[i] = SipHashAvx64(v[i].data(), v[i].size());
        }
    }

    std::cout << "algo with avx strings with same optimize: " << clock() - y << '\n';
    return result;
}


std::vector<UInt64> algo3(std::vector<std::string>& v) {
    auto y = clock();
    
    std::vector<UInt64> result(v.size());

    for (size_t i = 0; i < v.size(); ++i) {
        if (i + 4 <= v.size()) {
            if (v[i + 3].size() == v[i].size()) {
                auto x = SipHashAvx64ArrayStr({v[i].data(), v[i + 1].data(), v[i + 2].data(), v[i + 3].data()}, v[i].size());
                result[i++] = x[0];
                result[i++] = x[1];
                result[i++] = x[2];
                result[i] = x[3];
            }
        } else {
            result[i] = SipHashAvx64(v[i].data(), v[i].size());
        }
    }

    std::cout << "algo with avx: " << clock() - y << '\n';
    return result;
}

void check_algo_speed(size_t cnt) {
    auto v = gen_vec_rand(cnt);
    std::cout << "Algo Speed check with vector of size cnt = " << cnt << '\n';
    simple_algo(v);
    algo2(v);
    algo3(v);
    std::cout << "==== Algo Speed test end ====\n";
}


int mainEntryClickHouseSipHashAVX(int argc, char ** argv) {
    (void)argc;
    (void)argv;

   {
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
    }

    {
        check_correctness(4);
        check_correctness(20);
        check_correctness(200);
        check_correctness(400);
        check_correctness(1200);
        check_correctness(4000);
        check_correctness(5000);
    }

    {
        check_algo_speed(23);
        check_algo_speed(92);
        check_algo_speed(777);
        check_algo_speed(1000);
        check_algo_speed(1234);
        check_algo_speed(1337);
        check_algo_speed(22222);
        check_algo_speed(43213);
    }

    return 0;
}
