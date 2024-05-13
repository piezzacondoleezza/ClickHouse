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


int mainEntryClickHouseSipHashAVX(int argc, char ** argv) {
    (void)argc;
    (void)argv;


    size_t sz = 49;
    std::string check1 = get_string(sz);

    std::cout << check1 << std::endl;
    
    std::array<const char*, 4> arr = {check1.data(), check1.data(), check1.data(), check1.data()};


    auto start = clock();
    auto result = SipHashAvx64ArrayStr(arr, sz);
    std::cout << clock() - start << std::endl;

    for (int i = 0; i < 4; ++i) {
        std::cout << result[i] << std::endl;
    }

    start = clock();
    UInt64 result2 = 0;
    for (int i = 0; i < 4; ++i) {
        result2 = SipHashAvx64(check1.data(), check1.size());
    }
    std::cout << clock() - start << std::endl;

    std::cout << result2 << std::endl;


    std::string one = get_string(sz - 9);
    std::string two = get_string(sz - 1);
    std::string three = get_string(sz);
    std::string four = get_string(sz + 11);

    start = clock();
    std::array<const char*, 4> arr_1 = {one.data(), two.data(), three.data(), four.data()};
    std::array<size_t, 4> szs = {one.size(), two.size(), three.size(), four.size()};
    //for (int i = 0; i < 4; ++i) std::cout << szs[i] << ' ';
    //std::cout << std::endl;

    auto result3 = SipHashAvx64ArrayStrAllLength(arr_1, szs);
    std::cout << clock() - start << std::endl;
    for (int i = 0; i < 4; ++i) {
        std::cout << result3[i] << std::endl;
    }


    return 0;
}
