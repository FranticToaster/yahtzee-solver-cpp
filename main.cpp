//A single monolithic script
#include <iostream>
#include <chrono>
#include <fstream>
#include <tuple>
#include <unordered_map>
#include <array>
#include <vector>
#include <cassert>


/**************************************************************************************
 * locals
**************************************************************************************/
enum class Category {
    ONES = 0,
    TWOS,
    THREES,
    FOURS,
    FIVES,
    SIXES,
    SMALL_STRAIGHT,
    LARGE_STRAIGHT,
    THREE_OF_A_KIND,
    FOUR_OF_A_KIND,
    FULL_HOUSE,
    CHANCE,
    YAHTZEE
};

using Dices = std::array<int, 6>;



/**************************************************************************************
 * config
**************************************************************************************/
const std::array<int, 13> constScoreCategories = {0,0,0,0,0,0,30,40,0,0,25,0,0};
const std::array<int, 6> dieWeights = {6, 5, 4, 3, 2, 1};
const int dieWeightsSum = 21; //remember to update this if dieWeights is modified!!!!!!!!
//                              ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^



//omit tests


/**************************************************************************************
 * init
**************************************************************************************/
auto precomputeDiceIndexes () {
    std::vector<Dices> idxToDices;
    idxToDices.reserve(462);

    auto generateFreq = [&idxToDices] (
        std::size_t pos,
        int remaining, //sum of remaining
        Dices& freq,
        auto&& self
    ) {
        if (pos == 5) {
            freq[5] = remaining;
            idxToDices.push_back(freq);
            return;
        }

        for (int count = 0; count <= remaining; count++) {
            freq[pos] = count;
            self(pos + 1, remaining - count, freq, self);
        }
    };
    

    Dices freq{};
    // start with sum = 5 first
    for (int i = 5; i >= 0; i--){
        generateFreq(0, i, freq, generateFreq);
    }

    return idxToDices;
}


struct rollOutcome {
    int dicesIdx;
    double probability;
};
auto precomputeRollOutcomesByIdx(std::vector<Dices> idxToDices) {
    int factorials[] = {1, 1, 2, 6, 24, 120, 720};
    using Row = std::vector<rollOutcome>;

    std::array<Row, 6> table;
    for (std::size_t dicesIdx = 0; dicesIdx < idxToDices.size(); dicesIdx++) {
        Dices dices = idxToDices[dicesIdx];
        std::size_t i = 0; for (int e: dices) {i += e;}
        
        int prod_i = 1; for (int e: dices) {prod_i *= factorials[e];}
        int permutations = factorials[i] / prod_i;

        double prod_d = 1;
        for (int val: dices) {
            prod_d *=  static_cast<double> (dieWeights[val]) / dieWeightsSum;
        }
        double probability = permutations * prod_d;

        table[i].push_back(rollOutcome{static_cast<int> (dicesIdx), probability});
    }

    return table;
}



auto precomputeAvailableRerollsByIdx(std::vector<Dices> idxToDices) {
    std::array<std::vector<Dices>, 252> table;

    std::vector<Dices> availableRerolls;

    auto generateRerolls = [&availableRerolls] (
        std::size_t idx,
        const Dices& dices,
        Dices current,
        auto&& self
    ) {
        if (idx == 6) {
            availableRerolls.push_back(current);
            return;
        }

        for (int i = 0; i <= dices[idx]; i++) {
            current[idx] = i;
            self(idx + 1, dices, current, self);
        }
    };


    for (std::size_t dicesIdx = 0; dicesIdx < 252; dicesIdx++) {
        Dices dices = idxToDices[dicesIdx];
        {
            int sum = 0; for (int e: dices) {sum += e;}
            assert(sum == 5);
        }

        Dices current{};
        generateRerolls(0, dices, current, generateRerolls);

        table[dicesIdx] = availableRerolls;
    }
    return table;
}






int main() {
	
}
