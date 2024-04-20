#include <array>
#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <queue>

#include <random>


#include "SipHashAvx.h"
#include "base/types.h"


std::string get_string(size_t cnt) {
    std::string res;
    for (size_t i = 0; i < cnt; ++i) {
        res += 'a';
    }
    return res;
}


int mainEntryClickHouseSipHashAVX(int argc, char ** argv) {
    (void)argc;
    (void)argv;


    size_t sz = 20;
    std::string check1 = get_string(sz);
    
    std::array<const char*, 4> arr = {check1.data(), check1.data(), check1.data(), check1.data()};

    auto result = sipHash64ArrayStr(arr, sz);

    for (int i = 0; i < 4; ++i) {
        std::cout << result[i] << std::endl;
    }

    return 0;
}
