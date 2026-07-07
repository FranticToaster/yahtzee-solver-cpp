//A single monolithic script
#include <cstddef>
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
#include <iterator>
#include <ctime>
#include <algorithm>



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
        + dices[1] * 5
        + dices[2] * 25
        + dices[3] * 125
        + dices[4] * 625
        + dices[5] * 3125
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

    // assertion
    {
        for (int i = 0; i < idxToDices.size(); i++) {
            auto dices = idxToDices[static_cast<std::size_t>(i)];
            int sum = 0; for (auto d: dices) {sum += d;}
            assert(sum != 5 || i <= 251);
        }
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
    int turns_left;
    int used_categories;
    int upper_section_score;
    Dices dices;
    int rolls_left;
    bool yahtzee_disabled; // occurs when 0 is scored in yahtzee
};

struct GameWithDiceAsIndex {
    int turns_left = 13;
    int used_categories = 0;
    int upper_section_score = 0;
    int dices_idx = 255;
    int rolls_left = 3;
    bool yahtzee_disabled = false;

    std::uint64_t hash() const {
        return (
            static_cast<std::uint64_t>(yahtzee_disabled)
            | static_cast<std::uint64_t>(rolls_left) << 1
            | static_cast<std::uint64_t>(turns_left) << 3
            | static_cast<std::uint64_t>(upper_section_score) << 7
            | static_cast<std::uint64_t>(dices_idx) << 13
            | static_cast<std::uint64_t>(used_categories) << 21
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
    Category category,
    bool isJoker = false
) {
    int moveScore = getMoveScore(game, category, game.dices);
    if (category <= SIXES) {
        int previousScore = game.upper_section_score;
        game.upper_section_score += moveScore;

        if (game.upper_section_score >= 63) {
            game.upper_section_score = 63;
            if (previousScore < 63) moveScore += 35;
        }
    }


    if (!game.yahtzee_disabled) {
        game.yahtzee_disabled = (category == YAHTZEE && moveScore == 0);
    }
    game.used_categories |= (1 << category);
    if (!isJoker) game.turns_left -= 1;
    game.rolls_left = 3;


    return std::make_pair(game, moveScore);
}



inline auto getLegalClaims(Game game) {
    std::vector<std::pair<Category, Category>> categoriesAvailable;

    for (int i = ONES; i < YAHTZEE; i++) {
        if (!(game.used_categories & (1 << i))) {
            categoriesAvailable.emplace_back(
                static_cast<Category>(i),
                NONE
            );
        }
    }

    
    if ((game.used_categories & (1 << YAHTZEE)) == 0) {
        // first yahtzee claim
        categoriesAvailable.emplace_back(YAHTZEE, NONE);
    } else if (
        auto it = std::find(game.dices.begin(), game.dices.end(), 5);
        it != game.dices.end()
        && !game.yahtzee_disabled
    ) {
        // joker rule
        int availableUpperSectionCategory = static_cast<int>(
            std::distance(game.dices.begin(), it)
        );


        if (game.used_categories & (1 << availableUpperSectionCategory)) {
            // prioritize upper section category
            categoriesAvailable.emplace_back(
                YAHTZEE, 
                static_cast<Category> (availableUpperSectionCategory)
            );
        } else {
            // lower section categories
            for (int i = SMALL_STRAIGHT; i < YAHTZEE; i++) {
                categoriesAvailable.emplace_back(
                    YAHTZEE,
                    static_cast<Category> (i)
                );
            }
        }
    }

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



// remember to cache before return!
Score dfs(GameWithDiceAsIndex gameWithDiceAsIndex) {
    total_nodes_evaluated += 1;

    if (gameWithDiceAsIndex.turns_left == 0) {
        leaf_nodes_evaluated += 1;
        return 0;
    }


    // caching stuff
    auto gameHash = gameWithDiceAsIndex.hash();
    if (auto it = cache.find(gameHash); it != cache.end()) {
        cache_hits += 1;
        return it->second;
    }
    cache_misses += 1;


    // we handle this case first since we cannot claim immediately at the start of the turn,
    // so in this case we will skip the claim best score calculation
    if (gameWithDiceAsIndex.rolls_left == 3) {
        Score score = 0;
        gameWithDiceAsIndex.rolls_left = 2;

        for (const auto& [rollResultIdx, probability]: rollOutcomesByIdx[5]) {
            gameWithDiceAsIndex.dices_idx = rollResultIdx;
            score += probability * dfs(gameWithDiceAsIndex);
        }

        cache[gameHash] = score;
        return score;
    }

    Dices dicesValue = idxToDices[static_cast<std::size_t>(gameWithDiceAsIndex.dices_idx)];
    Game game{
        gameWithDiceAsIndex.turns_left,
        gameWithDiceAsIndex.used_categories,
        gameWithDiceAsIndex.upper_section_score,
        dicesValue,
        gameWithDiceAsIndex.rolls_left,
        gameWithDiceAsIndex.yahtzee_disabled
    };

    // calculate claim first since we can choose to claim at any point in time
    // as long as its not the start of the turn, which we already accounted for previously
    Score best_score = -1.0;
    for (auto categories: getLegalClaims(game)) {
        auto [gameCopy, claimedScore] = claimCategory(game, categories.first);
        if (categories.second != NONE) {
            auto [tmp, jokerBonus] = claimCategory(game, categories.second);
            gameCopy = tmp;
            claimedScore += jokerBonus;
        }


        Score score = claimedScore + dfs({
            gameCopy.turns_left,
            gameCopy.used_categories,
            gameCopy.upper_section_score,
            gameWithDiceAsIndex.dices_idx, // since claimCategory returns the same dices array
            gameCopy.rolls_left,
            gameCopy.yahtzee_disabled
        });

        if (score > best_score) {
            best_score = score;
        }
    }

    if (game.rolls_left == 0) {
        cache[gameHash] = best_score;
        return best_score;
    }

    for (auto reroll: availableRerolls[static_cast<std::size_t>(gameWithDiceAsIndex.dices_idx)]) {
        int sum = 0;
        for (int e: reroll) {sum += e;}
        auto rerollOutcomes = rollOutcomesByIdx[static_cast<std::size_t> (sum)];

        Dices remainingDices;
        for (std::size_t i = 0; i < 6; i++) {remainingDices[i] = dicesValue[i] + reroll[i];}
        int remainingDicesIdx = dicesHashToIdx[hashDices(remainingDices)];
        auto dicesAdditionByIdxRow = dicesAdditionByIdx[static_cast<std::size_t>(remainingDicesIdx)];

        Score score = 0.0;
        for (auto [rollResultIdx, probability]: rerollOutcomes) {
            score += probability * dfs({
                game.turns_left,
                game.used_categories,
                game.upper_section_score,
                dicesAdditionByIdxRow[rollResultIdx],
                game.rolls_left - 1,
                game.yahtzee_disabled
            });
        }

        if (score > best_score) best_score = score;
    }

    cache[gameHash] = best_score;
    return best_score;
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
    logs << std::fixed << std::setprecision(3) << time_taken_seconds.count() << "s.\n";



    start_time = std::chrono::steady_clock::now();
    Score score = dfs(GameWithDiceAsIndex{});
    end_time = std::chrono::steady_clock::now();
    time_taken_seconds = end_time - start_time;

    logs << "Searched " << leaf_nodes_evaluated << " leaf nodes and " << total_nodes_evaluated << " total nodes.\n";
    logs << "Cache hits: " << cache_hits << " . Cache misses: " << cache_misses << ".\n";
    logs << "Took " << time_taken_seconds.count() << "s.\n";
    logs << "Best score found: " << score << ".\n";



    // getting timestamp in text format
    auto now = std::chrono::system_clock::now();
    std::time_t time_now = std::chrono::system_clock::to_time_t(now);

    std::tm* time_info = std::localtime(&time_now);

    std::ostringstream oss;
    oss << std::put_time(time_info, "%Y-%m-%d %H-%M-%S");



    // file creation & writing
    std::ofstream outFile(oss.str());
    if (!outFile) {
        std::cerr << "Error opening file!" << std::endl;
        return 1;
    }

    outFile << logs.str();
    outFile.close();

    return 0;
}
