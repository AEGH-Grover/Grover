#include <fstream>
#include <string>
#include <sstream>
#include <iostream>
#include <chrono>
#include <vector>
#include <cstring>

#include "src/graph.h"
#include "src/solution.h"
#include "src/iterative.h"

#include <networkit/auxiliary/Log.hpp>
#include <networkit/graph/Graph.hpp>
#include <networkit/graph/GraphTools.hpp>

#include <networkit/centrality/GroupCloseness.hpp>
#include <networkit/centrality/GroupClosenessGrowShrink.hpp>
#include <networkit/centrality/GroupClosenessLocalSearch.hpp>

void print_usage() {
    std::cout << "Grover - Group Closeness Centrality Maximization Solver\n\n";
    std::cout << "USAGE:\n";
    std::cout << "  grover <graph_file> <k> [OPTIONS]\n\n";
    std::cout << "ARGUMENTS:\n";
    std::cout << "  <graph_file>    Path to graph file (edge list format)\n";
    std::cout << "  <k>             Size of the group to find\n\n";
    std::cout << "OPTIONS:\n";
    std::cout << "  -h, --help              Show this help message\n";
    std::cout << "  --approx                Use fast approximation (greedy + reduction-aware LS)\n";
    std::cout << "  --no-sufficient         Disable sufficient set computation\n";
    std::cout << "  --no-reductions         Disable data reductions\n";
    std::cout << "  --no-heuristic          Disable heuristic initialization\n";
    std::cout << "  --init <file>           Use initial solution from file\n\n";
    std::cout << "EXAMPLES:\n";
    std::cout << "  grover graph.edges 10\n";
    std::cout << "  grover graph.edges 5 --no-heuristic\n";
    std::cout << "  grover graph.edges 8 --init solution.txt\n";
    std::cout << "  grover graph.edges 10 --approx\n";
}

int main(int argc, char** argv) {
    if (argc < 2 || strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
        print_usage();
        return argc < 2 ? -1 : 0;
    }

    if (argc < 3) {
        std::cerr << "Error: Missing required arguments\n\n";
        print_usage();
        return -1;
    }
    
    std::string instance_file(argv[1]);
    int k = std::stoi(argv[2]);

    bool use_suff = true;
    bool use_redu = true;
    bool use_iter_redu = true;
    bool approx_mode = false;
    std::string init_solution_file;

    for (int i = 3; i < argc; ++i) {
        std::string arg(argv[i]);
        
        if (arg == "--approx") {
            approx_mode = true;
        } else if (arg == "--no-sufficient") {
            use_suff = false;
        } else if (arg == "--no-reductions") {
            use_redu = false;
        } else if (arg == "--no-heuristic") {
            use_iter_redu = false;
        } else if (arg == "--init") {
            if (i + 1 < argc) {
                init_solution_file = argv[++i];
            } else {
                std::cerr << "Error: --init requires a file path\n";
                return -1;
            }
        } else {
            std::cerr << "Error: Unknown option '" << arg << "'\n";
            return -1;
        }
    }

    if (use_redu && !use_suff) {
        std::cerr << "Warning: Reductions require sufficient set computation. Enabling sufficient set.\n";
        use_suff = true;
    }

    std::ifstream stream(instance_file);
    if (!stream.is_open()) {
        std::cerr << "Error: Cannot open graph file: " << instance_file << "\n";
        return -1;
    }

    std::vector<int> init_group;
    if (!init_solution_file.empty()) {
        std::ifstream init_stream(init_solution_file);
        if (!init_stream.is_open()) {
            std::cerr << "Error: Cannot open initial solution file: " << init_solution_file << "\n";
            return -1;
        }
        std::string line;
        std::getline(init_stream, line);
        std::istringstream line_stream(line);
        
        int v;
        while (line_stream >> v) {
            init_group.push_back(v);
        }
        init_stream.close();

        if (init_group.size() != k) {
            std::cerr << "Error: Initial group size (" << init_group.size() 
                      << ") must match k (" << k << ")\n";
            return -1;
        }
    }
    
    std::chrono::steady_clock::time_point start_g = std::chrono::steady_clock::now();
    Graph graph;
    graph << stream;
    graph.use_suff = use_suff;
    graph.use_redu = use_redu;
    std::chrono::steady_clock::time_point end_g = std::chrono::steady_clock::now();
    double g_time_seconds = (double) std::chrono::duration_cast<std::chrono::milliseconds>(end_g - start_g).count() / 1000;
    std::cout << "Graph Time (Seconds): " << g_time_seconds << std::endl;
    
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    
    Aux::Log::setLogLevel("ERROR");
    
    if (k > graph.get_num_verts() || k <= 0) {
        std::cerr << "Error: k must be between 1 and " << graph.get_num_verts() << "\n";
        return -1;
    }

    NetworKit::Graph graphNK;
    graphNK.addNodes(graph.get_num_verts());
    for (Edge& edge : graph.adj_array) {
        graphNK.addEdge(edge.source, edge.target, 1, true);
    }

    std::cout << "Number of nodes: " << graphNK.numberOfNodes() << std::endl;
    std::cout << "Number of edges: " << graphNK.numberOfEdges() << std::endl;

    // for k == 1 return the solution of the greedy algo, which is exact for k == 1
    if (k == 1) {
        NetworKit::GroupCloseness gc(graphNK, k);
	    gc.run();
	    std::vector<NetworKit::node> result = gc.groupMaxCloseness();
        std::cout << "Exact Score: " << gc.computeFarness(result) << std::endl;
        std::cout << "Solution: {" << result[0] << "}" << std::endl;
        
        std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
        double elapsed_time_seconds = (double) std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() / 1000;
        std::cout << "Elapsed Time (Seconds): " << elapsed_time_seconds << std::endl;

        return 0;
    }

    // Approximation mode: greedy + reduction-aware local search
    if (approx_mode) {
        auto start_greedy = std::chrono::steady_clock::now();
        NetworKit::GroupCloseness greedy_algo(graphNK, k, 0);
        greedy_algo.run();
        const std::vector<NetworKit::node> greedy_result = greedy_algo.groupMaxCloseness();
        auto end_greedy = std::chrono::steady_clock::now();
        
        std::cout << "Greedy Score: " << greedy_algo.computeFarness(greedy_result) << "\n";
        std::cout << "Greedy Time (s): " 
                  << std::chrono::duration_cast<std::chrono::milliseconds>(end_greedy - start_greedy).count() / 1000.0 << "\n\n";

        auto start_ls = std::chrono::steady_clock::now();
        std::vector<int> reduction = graph.compute_reduction(graphNK);
        NetworKit::GroupClosenessLocalSearch ls(graphNK, greedy_result, reduction, false, std::numeric_limits<int>::max());
        ls.run();
        std::vector<NetworKit::node> ls_result = ls.groupMaxCloseness();
        auto end_ls = std::chrono::steady_clock::now();
        
        std::cout << "ReduLS Score: " << greedy_algo.computeFarness(ls_result) << "\n";
        std::cout << "ReduLS Time (s): " 
                  << std::chrono::duration_cast<std::chrono::milliseconds>(end_ls - start_ls).count() / 1000.0 << "\n";

        std::cout << "Solution: {";
        for (size_t i = 0; i < ls_result.size(); ++i) {
            if (i > 0) std::cout << ", ";
            std::cout << ls_result[i];
        }
        std::cout << "}\n";

        std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
        double elapsed_time_seconds = (double) std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() / 1000;
        std::cout << "Total Time (Seconds): " << elapsed_time_seconds << std::endl;

        return 0;
    }	    

    graph.compute_reduction(graphNK);

    std::vector<int> initial_d(graph.n, 2);
    Solution<int> approx;

    if (!init_group.empty()) {
        std::vector<int> dists_to_group = graph.get_min_dist_to(init_group);
        int init_score = std::accumulate(dists_to_group.begin(), dists_to_group.end(), 0);
        std::cout << "Init Score: " << init_score << std::endl;

        approx = {init_group, init_score};
        initial_d = dists_to_group;
        
        for (int i = 0; i < initial_d.size(); ++i) {
            initial_d[i] = std::max(initial_d[i] + 1, 2);
        }

    } else if (use_iter_redu) {
        std::chrono::steady_clock::time_point start_gs = std::chrono::steady_clock::now();
        
        std::vector<NetworKit::node> initial_group;
        for (int i = 0; i < k; i++) initial_group.push_back(i*(graph.n / k));
        NetworKit::GroupClosenessGrowShrink gs_algo(graphNK, initial_group);
        gs_algo.run();
        std::vector<NetworKit::node> result = gs_algo.groupMaxCloseness();
        std::vector<int> int_result;
        for (NetworKit::node node : result) int_result.push_back(node);

        std::vector<int> dists = graph.get_min_dist_to(int_result);
        int gs_farness = std::accumulate(dists.begin(), dists.end(), 0);

        std::cout << "GS Score: " << gs_farness << std::endl;
        std::chrono::steady_clock::time_point end_gs = std::chrono::steady_clock::now();
        double gr_time_seconds = (double) std::chrono::duration_cast<std::chrono::milliseconds>(end_gs - start_gs).count() / 1000;
        std::cout << "GS Time (Seconds): " << gr_time_seconds << std::endl;

        std::vector<NetworKit::node> ls_input;
        for (int v : result) ls_input.push_back(v);

        std::chrono::steady_clock::time_point start_ls = std::chrono::steady_clock::now();
        std::vector<int> empty_reduction(graph.n, -1);
        NetworKit::GroupClosenessLocalSearch ls(graphNK, ls_input, empty_reduction, false);
        ls.run();
        std::vector<NetworKit::node> ls_result = ls.groupMaxCloseness();
        std::vector<int> ls_vertex_set;
        for (NetworKit::node nnode : ls_result) {
            ls_vertex_set.push_back(nnode);
        }

        std::vector<int> ls_dists = graph.get_min_dist_to(ls_vertex_set);
        int ls_score = std::accumulate(ls_dists.begin(), ls_dists.end(), 0);
        std::cout << "LS Score: " << ls_score << std::endl;
        std::chrono::steady_clock::time_point end_ls = std::chrono::steady_clock::now();
        double ls_time_seconds = (double) std::chrono::duration_cast<std::chrono::milliseconds>(end_ls - start_ls).count() / 1000;
        std::cout << "LS Time (Seconds): " << ls_time_seconds << std::endl;

        approx = {ls_vertex_set, ls_score};
        initial_d = ls_dists;
        
        for (int i = 0; i < initial_d.size(); ++i) {
            initial_d[i] = std::max(initial_d[i] + 1, 2);
        }
    }
    
    std::chrono::steady_clock::time_point start_d = std::chrono::steady_clock::now();
    graph.compute_initial_distances(initial_d);
    std::chrono::steady_clock::time_point end_d = std::chrono::steady_clock::now();
    double d_time_seconds = (double) std::chrono::duration_cast<std::chrono::milliseconds>(end_d - start_d).count() / 1000;
    std::cout << "Initial Distance Time (Seconds): " << d_time_seconds << std::endl;

    IterativeSolver<int> iter_solver(graph, k, approx);
    Solution<int> exact_solution = iter_solver.solve();

    std::cout << "Exact Score: " << exact_solution.score << std::endl;

    std::cout << "Solution: {";
    for (auto iter = exact_solution.vertex_set.begin(); iter != exact_solution.vertex_set.end()-1; iter++) {
        std::cout << *iter << ", ";
    }
    std::cout << *(exact_solution.vertex_set.end()-1) << "}" << std::endl;

    std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
    double elapsed_time_seconds = (double) std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() / 1000;
    std::cout << "Elapsed Time (Seconds): " << elapsed_time_seconds << std::endl;

    return 0;
}
