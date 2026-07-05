//A single monolithic script
#include <cstdint>
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
#include <cmath>



std::ostringstream logs;



// score type should default to int and only use float in actual solver
using Score = double;
const Score scoreEpsilon = 1e-9;
inline bool scoreClose (Score a, Score b) {
    return std::abs(a - b) <= scoreEpsilon;
}


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
    YAHTZEE,
    NONE
};

using Dices = std::array<int, 6>;



/**************************************************************************************
 * config
**************************************************************************************/
const std::array<int, 13> constScoreCategories = {0,0,0,0,0,0,30,40,0,0,25,0,0};
const bool yahtzeeBonus = false;
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
        availableRerolls.clear();
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
        row.fill(-1);

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
    bool yahtzeeDisabled = false; // occurs when 0 is scored in yahtzee
};

struct GameWithDiceAsIndex {
    int turns_left = 13;
    int used_categories = 0;
    int upper_section_score = 0;
    int dices_idx = 255;
    int rolls_left = 3;
    bool yahtzeeDisabled = false;

    std::uint64_t hash() const {
        return (
            std::uint64_t(yahtzeeDisabled)
            | std::uint64_t(rolls_left) << 1
            | std::uint64_t(turns_left) << 3
            | std::uint64_t(upper_section_score) << 7
            | std::uint64_t(dices_idx) << 13
            | std::uint64_t(used_categories) << 21
        );
    }
};



bool moveFitsReq (
    Category category,
    Game game
) {
    auto dices = game.dices;

    if (category <= SIXES) {
        return dices[category] > 0;
    } else if (category == SMALL_STRAIGHT) {
        for (std::size_t i = 0; i < 3; i++) {
            bool fulfils = true;
            for (std::size_t j = i; j < i + 4; j++) {
                if (dices[j] == 0) {
                    fulfils = false;
                    break;
                }
            }

            if (fulfils) return true;
        }
        return false;
    } else if (category == LARGE_STRAIGHT) {
        return dices == std::array<int,6>{1,1,1,1,1,0} ||
               dices == std::array<int,6>{0,1,1,1,1,1};
    } else if (category == THREE_OF_A_KIND) {
        for (int count: dices) {
            if (count >= 3) return true;
        }
        return false;
    } else if (category == FOUR_OF_A_KIND) {
        for (int count: dices) {
            if (count >= 4) return true;
        }
        return false;
    } else if (category == FULL_HOUSE) {
        bool twoInDices = false;
        bool threeInDices = false;
        for (int val: dices) {
            if (val == 2) twoInDices = true;
            if (val == 3) threeInDices = true;
        }
        return twoInDices && threeInDices;
    } else if (category == CHANCE) {
        return true;
    } else if (category == YAHTZEE) {
        for (int count: dices) {
            if (count == 5) return true;
        }
        return false;
    } else {
        logs << "Invalid category passed to moveFitsReq: " << category << '\n';
        return false;
    }
}



int getMoveScore (
    Game game,
    Category category,
    Dices dices
) {
    if (!moveFitsReq(category, game)) {
        return 0;
    }

    const int constScore = constScoreCategories[category];
    if (constScore != 0) return constScore;

    if (category <= SIXES) {
        return dices[category] * (category + 1);
    }

    if (category == YAHTZEE) {
        return (
            (yahtzeeBonus && (game.used_categories >> YAHTZEE) & 1)
            ? 100
            : 50
        );
    }

    int sum = 0;
    for (std::size_t i = 0; i < 6; i++) {
        sum += (
            dices[i]
            * static_cast<int> (i + 1)
        );
    }
    return sum;
}



auto claimCategory (
    Game game,
    Category category
) {
    int upperSectionScore = game.upper_section_score;

    int moveScore = getMoveScore(game, category, game.dices);
    if (category <= SIXES) {
        int previousScore = upperSectionScore;
        upperSectionScore += moveScore;

        if (upperSectionScore >= 63) {
            upperSectionScore = 63;
            if (previousScore < 63) moveScore += 35;
        }
    }

    bool yahtzeeDisabled = (
        game.yahtzeeDisabled
        || (category == YAHTZEE && moveScore == 0)
    );


    int usedCategories = game.used_categories | (1 << category);
    int turnsLeft = game.turns_left - 1;
    int dicesIdx = 255;
    int rollsLeft = 3;

    return std::make_pair(
        GameWithDiceAsIndex{
            turnsLeft,
            usedCategories,
            upperSectionScore,
            dicesIdx,
            rollsLeft,
            yahtzeeDisabled
        },
        moveScore
    );
}



inline auto getLegalClaims(Game game) {
    std::vector<int> categoriesAvailable;
    for (int i = ONES; i < YAHTZEE; i++) {
        if (!(game.used_categories & (1 << i))) {
            categoriesAvailable.push_back(i);
        }
    }
    categoriesAvailable.push_back(YAHTZEE);
    return categoriesAvailable;
}



std::array<std::array<int, 6>, 462> idxToDices;
std::unordered_map<int, int> dicesHashToIdx;
std::array<std::vector<rollOutcome>, 6> rollOutcomesByIdx;
std::array<std::vector<Dices>, 252> availableRerolls;
std::array<std::array<int, 462>, 462> dicesAdditionByIdx;


int leaf_nodes_evaluated = 0;
int total_nodes_evaluated = 0;


int cache_hits = 0;
int cache_misses = 0;
std::unordered_map<std::uint64_t, Score> cache;
// might consider using .reserve later on



Score dfs(GameWithDiceAsIndex gameWithDiceAsIndex) {
    total_nodes_evaluated += 1;

    if (gameWithDiceAsIndex.turns_left == 0) {
        leaf_nodes_evaluated += 1;
        return 0;
    }

    auto gameHash = gameWithDiceAsIndex.hash();
    if (auto it = cache.find(gameHash); it != cache.end()) {
        return it->second;
    }
}



int main() {
    //init
    logs << "Precomputation started.\n";
    auto start_time = std::chrono::steady_clock::now();

    std::tie(idxToDices, dicesHashToIdx) = precomputeDiceIndexes();
    rollOutcomesByIdx = precomputeRollOutcomesByIdx(idxToDices);
    availableRerolls = precomputeAvailableRerollsByIdx(idxToDices);
    dicesAdditionByIdx = precomputeDicesAdditionByIdx(idxToDices, dicesHashToIdx);

    logs << "Precomputation complete.\n";
    auto end_time = std::chrono::steady_clock::now();
    std::chrono::duration<double> time_taken_seconds = end_time - start_time;
    logs << "Precomputation took a total of ";
    logs << std::fixed << std::setprecision(2) << time_taken_seconds.count() << 's';






}
