// generate_addresses.cpp
// Writes addresses.txt: 700 addresses in a hot region, then 300 anywhere.
#include <cstdint>
#include <fstream>
#include <random>
using namespace std;

int main() {
    ofstream fout("addresses.txt");
    mt19937 rng(42);
    uniform_int_distribution<uint32_t> dist(0, 0xFFFFFFFF);
    // Hot region: 1 MB, which is 256 pages at 4 KB per page
    uniform_int_distribution<uint32_t> hot(0x70000000, 0x700FFFFF);
    for (int i = 0; i < 1000; i++) {
        uint32_t va = (i < 700) ? hot(rng) : dist(rng);
        fout << hex << "0x" << va << "\n";
    }
}
