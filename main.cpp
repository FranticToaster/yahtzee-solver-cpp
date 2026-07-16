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

#include <cereal/archives/binary.hpp>
#include <cereal/types/unordered_map.hpp>



std::ostringstream logs;



// score type should default to int and only use float in actual solver
using Score = double;


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
// Increment when solver rules change so prior decision-tree checkpoints are ignored.
constexpr int solverCacheVersion = 2;


constexpr std::array<int, 13> constScoreCategories = {0,0,0,0,0,0,30,40,0,0,25,0,0};
constexpr bool yahtzeeBonus = true;
constexpr std::array<int, 6> dieWeights = {1, 1, 1, 1, 1, 1};
constexpr int dieWeightsSum = 6; //remember to update this if dieWeights is modified!!!!!!!!
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
        for (int j = 0; j < 6; j++) {
            double tmp =  static_cast<double> (dieWeights[j]) / dieWeightsSum;
            prod_d *= std::pow(tmp, dices[j]);
        }
        double probability = permutations * prod_d;

        table[i].emplace_back(static_cast<int> (dicesIdx), probability);
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

            if (sum != 5){
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

    int hash() const {
        return (
            static_cast<int>(yahtzee_disabled)
            | (rolls_left) << 1
            // turns_left can be determined using used_categories
            | (upper_section_score) << 3
            | (dices_idx) << 9
            | (used_categories) << 17
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
        if (game.yahtzee_disabled) return false;
        
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
    Dices dices,
    bool isJoker = false // joker bypasses requirement check
) {
    if (!isJoker && !moveFitsReq(category, game)) {
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
    int moveScore = getMoveScore(game, category, game.dices, isJoker);
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
    ) {
        // joker rule
        int availableUpperSectionCategory = static_cast<int>(
            std::distance(game.dices.begin(), it)
        );


        if ((game.used_categories & (1 << availableUpperSectionCategory)) == 0) {
            // prioritize upper section category
            categoriesAvailable.emplace_back(
                YAHTZEE, 
                static_cast<Category> (availableUpperSectionCategory)
            );
        } else {
            // lower section categories
            int validLowerCategoryCount = 0;
            for (int i = SMALL_STRAIGHT; i < YAHTZEE; i++) {
                if (game.used_categories & (1 << i)) continue; // already used
                validLowerCategoryCount += 1;
                categoriesAvailable.emplace_back(
                    YAHTZEE,
                    static_cast<Category> (i)
                );
            }

            // if no lowercategories available for wildcard, score 0 in any upper category
            if (validLowerCategoryCount == 0){
                for (int i = ONES; i <= SIXES; i++) {
                    if (
                        i != availableUpperSectionCategory
                        && (game.used_categories & (1 << i)) == 0
                    ) {
                        categoriesAvailable.emplace_back(
                            YAHTZEE,
                            static_cast<Category>(i)
                        );
                    }
                }
            }
        }
    }

    return categoriesAvailable;
}




inline std::string getTimeStampStr(
    std::string fmt = "%Y-%m-%d_%H-%M-%S"
) {
    auto now = std::chrono::system_clock::now();
    std::time_t time_now = std::chrono::system_clock::to_time_t(now);

    std::tm* time_info = std::localtime(&time_now);

    std::ostringstream oss;
    oss << std::put_time(time_info, fmt.c_str());
    return oss.str();
}



std::chrono::steady_clock::time_point solver_start_time;


std::array<std::array<int, 6>, 462> idxToDices;
std::unordered_map<int, int> dicesHashToIdx;
std::array<std::vector<rollOutcome>, 6> rollOutcomesByIdx;
std::array<std::vector<Dices>, 252> availableRerolls;
std::array<std::array<int, 462>, 462> dicesAdditionByIdx;


long long leaf_nodes_evaluated = 0;
long long total_nodes_evaluated = 0;


long long cache_hits = 0;
long long cache_misses = 0;
std::unordered_map<int, Score> cache;
// might consider using .reserve later on


// 0s if not loaded from checkpoint
double checkpoint_runtime = 0.0;



inline void save() {
    std::ofstream os("checkpoint-v" + std::to_string(solverCacheVersion) + ".bin", std::ios::binary);

    cereal::BinaryOutputArchive archive(os);

    auto save_time = std::chrono::steady_clock::now();
    std::chrono::duration<double> time_taken_seconds = save_time - solver_start_time;

    archive(
        cache,
        leaf_nodes_evaluated,
        total_nodes_evaluated,
        cache_hits,
        cache_misses,
        checkpoint_runtime + time_taken_seconds.count()
    );

    os.close();

    logs << "Saved checkpoint successfully at " << getTimeStampStr() << "!\n";
}


inline void load() {
    std::ifstream is("checkpoint-v" + std::to_string(solverCacheVersion) + ".bin", std::ios::binary);

    if (is.fail()) return;

    cereal::BinaryInputArchive archive(is);

    archive(
        cache,
        leaf_nodes_evaluated,
        total_nodes_evaluated,
        cache_hits,
        cache_misses,
        checkpoint_runtime
    );

    logs << "Successfully loaded from checkpoint!\n";
    logs << "Loaded data:\n";
    logs << "Leaf nodes: " << leaf_nodes_evaluated << ". Total nodes: " << total_nodes_evaluated << ".\n";
    logs << "Cache hits: " << cache_hits << " . Cache misses: " << cache_misses << ".\n";
    logs << "Checkpoint runtime: " << checkpoint_runtime << ".\n\n";
}



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


    if (cache_misses % 40'000'000 == 0) save();


    // we handle this case first since we cannot claim immediately at the start of the turn,
    // so in this case we will skip the claim best score calculation
    if (gameWithDiceAsIndex.rolls_left == 3) {
        Score score = 0;
        gameWithDiceAsIndex.rolls_left = 2;

        for (const auto& [rollResultIdx, probability]: rollOutcomesByIdx[5]) {
            gameWithDiceAsIndex.dices_idx = rollResultIdx;
            score += probability * dfs(gameWithDiceAsIndex);
        }

        cache.emplace(gameHash, score);
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
            auto [tmp, jokerBonus] = claimCategory(gameCopy, categories.second, true);
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
        cache.emplace(gameHash, best_score);
        return best_score;
    }

    for (auto reroll: availableRerolls[static_cast<std::size_t>(gameWithDiceAsIndex.dices_idx)]) {
        int sum = 0;
        for (int e: reroll) {sum += e;}
        const auto& rerollOutcomes = rollOutcomesByIdx[static_cast<std::size_t> (sum)];

        Dices remainingDices;
        for (std::size_t i = 0; i < 6; i++) {remainingDices[i] = dicesValue[i] - reroll[i];}
        int remainingDicesIdx = dicesHashToIdx[hashDices(remainingDices)];
        const auto& dicesAdditionByIdxRow = dicesAdditionByIdx[static_cast<std::size_t>(remainingDicesIdx)];

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

    cache.emplace(gameHash, best_score);
    return best_score;
}




int main() {
    load();

    //log cfg
    logs << "Config: \n";
    logs << "ConstScoreCategories: ";
    for (auto e: constScoreCategories) logs << e << " ";
    logs << "\n";
    logs << "Yahtzee Bonus: " << std::boolalpha << yahtzeeBonus << "\n";
    logs << "Die Weights: ";
    for (auto e: dieWeights) logs << e << " ";
    logs << "\n";
    logs << "Die weights sum: " << dieWeightsSum << "\n\n";



    {
        int sum = 0;
        for (int e: dieWeights) sum += e;
        assert(sum == dieWeightsSum);
    }


    //init
    {
        logs << "Precomputation started.\n";
        solver_start_time = std::chrono::steady_clock::now();

        std::tie(idxToDices, dicesHashToIdx) = precomputeDiceIndexes();
        rollOutcomesByIdx = precomputeRollOutcomesByIdx(idxToDices);
        availableRerolls = precomputeAvailableRerollsByIdx(idxToDices);
        dicesAdditionByIdx = precomputeDicesAdditionByIdx(idxToDices, dicesHashToIdx);

        auto end_time = std::chrono::steady_clock::now();
        std::chrono::duration<double> time_taken_seconds = end_time - solver_start_time;
        logs << "Precomputation took a total of ";
        logs << std::fixed << std::setprecision(5) << checkpoint_runtime + time_taken_seconds.count() << "s.\n\n";
    }




    {
        auto start_time = std::chrono::steady_clock::now();
        Score score = dfs(GameWithDiceAsIndex{});
        auto end_time = std::chrono::steady_clock::now();
        std::chrono::duration<double> time_taken_seconds = end_time - start_time;

        logs << "Searched " << leaf_nodes_evaluated << " leaf nodes and " << total_nodes_evaluated << " total nodes.\n";
        logs << "Cache hits: " << cache_hits << " . Cache misses: " << cache_misses << ".\n";
        logs << "Took " << time_taken_seconds.count() << "s.\n";
        logs << "Best score found: " << score << ".\n\n";
    }



    {
        // file creation & writing
        std::ofstream outFile(getTimeStampStr() + ".log");
        if (!outFile) {
            std::cerr << "Error opening file!" << std::endl;
            return 1;
        }

        outFile << logs.str();
        outFile.close();
    }

    return 0;
}
