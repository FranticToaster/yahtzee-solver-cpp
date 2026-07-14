#include <fstream>
#include <iostream>
#include <cereal/archives/binary.hpp>
#include <cereal/types/unordered_map.hpp>


std::unordered_map<int, double> cache;
long long leaf_nodes_evaluated, total_nodes_evaluated, cache_hits, cache_misses;
double checkpoint_runtime;


inline void load() {
    std::ifstream is("checkpoint.bin", std::ios::binary);

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
    
    
    std::cout << "Successfully loaded from checkpoint!\n";
    std::cout << "Loaded data:\n";
    std::cout << "Leaf nodes: " << leaf_nodes_evaluated << ". Total nodes: " << total_nodes_evaluated << ".\n";
    std::cout << "Cache hits: " << cache_hits << " . Cache misses: " << cache_misses << ".\n";
    std::cout << "Cache size: " << cache.size() << ".\n";
    std::cout << "Checkpoint runtime: " << checkpoint_runtime << ".\n\n";
}

int main() {
    load();
}