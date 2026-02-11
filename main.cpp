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

using Clock = std::chrono::steady_clock;
using TimePoint = std::chrono::steady_clock::time_point;

struct Config {
    std::string graph_file;
    int k;
    bool use_sufficient_set = true;
    bool use_reductions = true;
    bool use_heuristic = true;
    bool approx_mode = false;
    std::string init_solution_file;
};

double elapsed_seconds(TimePoint start, TimePoint end) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() / 1000.0;
}

void print_solution(const std::vector<int>& solution) {
    std::cout << "Solution: {";
    for (size_t i = 0; i < solution.size(); ++i) {
        if (i > 0) std::cout << ", ";
        std::cout << solution[i];
    }
    std::cout << "}\n";
}

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

Config parse_args(int argc, char** argv) {
    if (argc < 2 || strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
        print_usage();
        exit(argc < 2 ? -1 : 0);
    }

    if (argc < 3) {
        std::cerr << "Error: Missing required arguments\n\n";
        print_usage();
        exit(-1);
    }

    Config config;
    config.graph_file = argv[1];
    config.k = std::stoi(argv[2]);

    for (int i = 3; i < argc; ++i) {
        std::string arg(argv[i]);
        
        if (arg == "--approx") {
            config.approx_mode = true;
        } else if (arg == "--no-sufficient") {
            config.use_sufficient_set = false;
        } else if (arg == "--no-reductions") {
            config.use_reductions = false;
        } else if (arg == "--no-heuristic") {
            config.use_heuristic = false;
        } else if (arg == "--init") {
            if (i + 1 < argc) {
                config.init_solution_file = argv[++i];
            } else {
                std::cerr << "Error: --init requires a file path\n";
                exit(-1);
            }
        } else {
            std::cerr << "Error: Unknown option '" << arg << "'\n";
            exit(-1);
        }
    }

    if (config.use_reductions && !config.use_sufficient_set) {
        std::cerr << "Warning: Reductions require sufficient set computation. Enabling sufficient set.\n";
        config.use_sufficient_set = true;
    }

    return config;
}

std::vector<int> load_initial_solution(const std::string& filename, int expected_size) {
    std::ifstream stream(filename);
    if (!stream.is_open()) {
        std::cerr << "Error: Cannot open initial solution file: " << filename << "\n";
        exit(-1);
    }

    std::vector<int> group;
    std::string line;
    std::getline(stream, line);
    std::istringstream line_stream(line);
    
    int v;
    while (line_stream >> v) {
        group.push_back(v);
    }

    if (group.size() != expected_size) {
        std::cerr << "Error: Initial group size (" << group.size() 
                  << ") must match k (" << expected_size << ")\n";
        exit(-1);
    }

    return group;
}

Graph load_graph(const std::string& filename, bool use_sufficient_set, bool use_reductions) {
    std::ifstream stream(filename);
    if (!stream.is_open()) {
        std::cerr << "Error: Cannot open graph file: " << filename << "\n";
        exit(-1);
    }

    TimePoint start = Clock::now();
    Graph graph;
    graph << stream;
    graph.use_suff = use_sufficient_set;
    graph.use_redu = use_reductions;
    TimePoint end = Clock::now();

    std::cout << "Graph Time (Seconds): " << elapsed_seconds(start, end) << "\n";
    return graph;
}

NetworKit::Graph convert_to_networkit(const Graph& graph) {
    NetworKit::Graph nk_graph;
    nk_graph.addNodes(graph.get_num_verts());
    for (const Edge& edge : graph.adj_array) {
        nk_graph.addEdge(edge.source, edge.target, 1, true);
    }
    return nk_graph;
}

void solve_k1(const NetworKit::Graph& graph, TimePoint start_time) {
    NetworKit::GroupCloseness gc(graph, 1);
    gc.run();
    std::vector<NetworKit::node> result = gc.groupMaxCloseness();
    
    std::cout << "Exact Score: " << gc.computeFarness(result) << "\n";
    std::cout << "Solution: {" << result[0] << "}\n";
    std::cout << "Elapsed Time (Seconds): " << elapsed_seconds(start_time, Clock::now()) << "\n";
}

void solve_approx(Graph& graph, NetworKit::Graph& nk_graph, int k, TimePoint start_time) {
    TimePoint greedy_start = Clock::now();
    NetworKit::GroupCloseness greedy(nk_graph, k, 0);
    greedy.run();
    std::vector<NetworKit::node> greedy_result = greedy.groupMaxCloseness();
    
    std::cout << "Greedy Score: " << greedy.computeFarness(greedy_result) << "\n";
    std::cout << "Greedy Time (s): " << elapsed_seconds(greedy_start, Clock::now()) << "\n\n";

    TimePoint ls_start = Clock::now();
    std::vector<int> reduction = graph.compute_reduction(nk_graph);
    NetworKit::GroupClosenessLocalSearch ls(nk_graph, greedy_result, reduction, false, std::numeric_limits<int>::max());
    ls.run();
    std::vector<NetworKit::node> ls_result = ls.groupMaxCloseness();
    
    std::cout << "ReduLS Score: " << greedy.computeFarness(ls_result) << "\n";
    std::cout << "ReduLS Time (s): " << elapsed_seconds(ls_start, Clock::now()) << "\n";
    
    std::vector<int> solution(ls_result.begin(), ls_result.end());
    print_solution(solution);
    std::cout << "Total Time (Seconds): " << elapsed_seconds(start_time, Clock::now()) << "\n";
}

Solution<int> compute_heuristic_solution(Graph& graph, const NetworKit::Graph& nk_graph, int k) {
    TimePoint gs_start = Clock::now();
    std::vector<NetworKit::node> initial_group;
    for (int i = 0; i < k; i++) {
        initial_group.push_back(i * (graph.n / k));
    }
    
    NetworKit::GroupClosenessGrowShrink gs(nk_graph, initial_group);
    gs.run();
    std::vector<NetworKit::node> gs_result = gs.groupMaxCloseness();
    std::vector<int> gs_group(gs_result.begin(), gs_result.end());
    
    std::vector<int> gs_dists = graph.get_min_dist_to(gs_group);
    int gs_score = std::accumulate(gs_dists.begin(), gs_dists.end(), 0);
    
    std::cout << "GS Score: " << gs_score << "\n";
    std::cout << "GS Time (Seconds): " << elapsed_seconds(gs_start, Clock::now()) << "\n";

    TimePoint ls_start = Clock::now();
    std::vector<int> empty_reduction(graph.n, -1);
    NetworKit::GroupClosenessLocalSearch ls(nk_graph, gs_result, empty_reduction, false);
    ls.run();
    std::vector<NetworKit::node> ls_result = ls.groupMaxCloseness();
    std::vector<int> ls_group(ls_result.begin(), ls_result.end());
    
    std::vector<int> ls_dists = graph.get_min_dist_to(ls_group);
    int ls_score = std::accumulate(ls_dists.begin(), ls_dists.end(), 0);
    
    std::cout << "LS Score: " << ls_score << "\n";
    std::cout << "LS Time (Seconds): " << elapsed_seconds(ls_start, Clock::now()) << "\n";

    return {ls_group, ls_score};
}

std::vector<int> compute_initial_distances(const std::vector<int>& dists) {
    std::vector<int> initial_d = dists;
    for (int& d : initial_d) {
        d = std::max(d + 1, 2);
    }
    return initial_d;
}

void solve_exact(Graph& graph, NetworKit::Graph& nk_graph, const Config& config, TimePoint start_time) {
    graph.compute_reduction(nk_graph);

    Solution<int> heuristic_solution;
    std::vector<int> initial_d(graph.n, 2);

    if (!config.init_solution_file.empty()) {
        std::vector<int> init_group = load_initial_solution(config.init_solution_file, config.k);
        std::vector<int> dists = graph.get_min_dist_to(init_group);
        int score = std::accumulate(dists.begin(), dists.end(), 0);
        
        std::cout << "Init Score: " << score << "\n";
        heuristic_solution = {init_group, score};
        initial_d = compute_initial_distances(dists);
    } else if (config.use_heuristic) {
        heuristic_solution = compute_heuristic_solution(graph, nk_graph, config.k);
        std::vector<int> dists = graph.get_min_dist_to(heuristic_solution.vertex_set);
        initial_d = compute_initial_distances(dists);
    }

    TimePoint dist_start = Clock::now();
    graph.compute_initial_distances(initial_d);
    std::cout << "Initial Distance Time (Seconds): " << elapsed_seconds(dist_start, Clock::now()) << "\n";

    IterativeSolver<int> solver(graph, config.k, heuristic_solution);
    Solution<int> exact_solution = solver.solve();

    std::cout << "Exact Score: " << exact_solution.score << "\n";
    print_solution(exact_solution.vertex_set);
    std::cout << "Elapsed Time (Seconds): " << elapsed_seconds(start_time, Clock::now()) << "\n";
}

int main(int argc, char** argv) {
    Aux::Log::setLogLevel("ERROR");
    
    Config config = parse_args(argc, argv);
    Graph graph = load_graph(config.graph_file, config.use_sufficient_set, config.use_reductions);
    
    TimePoint start_time = Clock::now();

    if (config.k <= 0 || config.k > graph.get_num_verts()) {
        std::cerr << "Error: k must be between 1 and " << graph.get_num_verts() << "\n";
        return -1;
    }

    NetworKit::Graph nk_graph = convert_to_networkit(graph);
    std::cout << "Number of nodes: " << nk_graph.numberOfNodes() << "\n";
    std::cout << "Number of edges: " << nk_graph.numberOfEdges() << "\n";

    if (config.k == 1) {
        solve_k1(nk_graph, start_time);
    } else if (config.approx_mode) {
        solve_approx(graph, nk_graph, config.k, start_time);
    } else {
        solve_exact(graph, nk_graph, config, start_time);
    }

    return 0;
}
