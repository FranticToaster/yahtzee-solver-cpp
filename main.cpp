//A single monolithic script
#include <iostream>
#include <chrono>
#include <sstream>
#include <fstream>
#include <array>
#include <vector>
#include <cassert>
#include <utility>
#include <unordered_map>
#include <iomanip>


/**************************************************************************************
 * locals
**************************************************************************************/
enum Category {
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
auto hashDices(const Dices& dices) {
    return (
        dices[0]
        + dices[1] * 6
        + dices[2] * 36
        + dices[3] * 216
        + dices[4] * 1296
        + dices[5] * 7776
    );
}



// avoid using dicesHashToIdx where possible
auto precomputeDiceIndexes () {
    int dicesIdx = 0;
    std::array<Dices, 462> idxToDices;
    std::unordered_map<int, int> dicesHashToIdx;

    auto generateFreq = [&dicesIdx, &idxToDices, &dicesHashToIdx] (
        std::size_t pos,
        int remaining, //sum of remaining
        Dices& freq,
        auto&& self
    ) {
        if (pos == 5) {
            freq[5] = remaining;
            idxToDices[static_cast<std::size_t> (dicesIdx)] = freq;
            dicesHashToIdx[hashDices(freq)] = dicesIdx;
            dicesIdx++;
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

    return std::make_pair(idxToDices, dicesHashToIdx);
}



struct rollOutcome {
    int dicesIdx;
    double probability;
};
auto precomputeRollOutcomesByIdx (std::array<Dices, 462> idxToDices) {
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



auto precomputeAvailableRerollsByIdx (std::array<Dices, 462> idxToDices) {
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



auto precomputeDicesAdditionByIdx (
    std::array<Dices, 462> idxToDices,
    std::unordered_map<int, int> dicesHashToIdx
) {
    using Row = std::array<int, 462>;

    std::array<Row, 462> table;

    for (std::size_t i = 0; i < 462; i++) {
        Row row;

        Dices dices_i = idxToDices[i];
        for (std::size_t j = 0; j < 462; j++) {
            Dices dices_j = idxToDices[j];

            Dices newDices; int sum = 0;
            for (std::size_t k = 0; k < 6; k++) {
                newDices[k] = dices_i[k] + dices_j[k];
                sum += newDices[k];
            }

            if (sum > 5){
                continue;
            }


            row[j] = dicesHashToIdx[hashDices(newDices)];
        }

        table[i] = row;
    }

    return table;
}



/**************************************************************************************
 *game
**************************************************************************************/
struct Game {
    int turns_left = 13;
    int used_categories = 0;
    int upper_section_score = 0;
    Dices dices = {0, 0, 0, 0, 0, 0};
    int rolls_left = 3;
};

struct GameWithDiceAsIndex {
    int turns_left = 13;
    int used_categories = 0;
    int upper_section_score = 0;
    int dices_idx = 999;
    int rolls_left = 3;
};


enum accessIndexes {
    TURNS_LEFT = 0,
    USED_CATEGORIES_ = 1,
    UPPER_SECTION_SCORE = 2,
    DICES = 3,
    ROLLS_LEFT = 4
};



bool moveFitsReq (
    Category category,
    Dices dices
) {
    if (ONES <= category && category <= SIXES) {
        return dices[category] > 0;
    }
}



int main() {
    std::ostringstream logs;

    //init
    logs << "Precomputation started.\n";
    auto start_time = std::chrono::steady_clock::now();

    auto dicesIndex = precomputeDiceIndexes();
    auto idxToDices = dicesIndex.first;
    auto dicesHashToIdx = dicesIndex.second;
    auto rollOutcomesByIdx = precomputeRollOutcomesByIdx(idxToDices);
    auto availableRerolls = precomputeAvailableRerollsByIdx(idxToDices);
    auto dicesAdditionByIdx = precomputeDicesAdditionByIdx(idxToDices, dicesHashToIdx);

    logs << "Precomputation complete.\n";
    auto end_time = std::chrono::steady_clock::now();
    std::chrono::duration<double> time_taken_seconds = end_time - start_time;
    logs << "Precomputation took a total of ";
    logs << std::fixed << std::setprecision(2) << time_taken_seconds.count() << 's';






}
