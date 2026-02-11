#pragma once

#include <algorithm>
#include <vector>
#include <queue>
#include <fstream>
#include <string>
#include <sstream>

#include <networkit/graph/Graph.hpp>
#include <networkit/components/BiconnectedComponents.hpp>


typedef unsigned int NodeID;

struct Edge {
    Edge(NodeID source, NodeID target) : source(source), target(target) {}

    NodeID source;
    NodeID target;
};

class Graph {
public:
    Graph() : n(0), m(0), adj_array(), in_suff_set_cached(), use_suff(true), use_redu(true) {}

    // Read a graph from an ifstream. Each line
    // of the file has to fit the regex "\d+ \d+",
    // representing an edge.
    void operator<<(std::ifstream& stream) {
        assert(stream.is_open());
        
        std::string line;

        while (std::getline(stream, line)) {
            std::istringstream raw(line);
            NodeID first, second;
            raw >> first;
            raw >> second;

            Edge edge(first, second);
            Edge rev_edge(second, first);

            adj_array.push_back(edge);
            adj_array.push_back(rev_edge);
        }
        stream.close();

        std::sort(
            adj_array.begin(), 
            adj_array.end(),
            [] (const Edge& a, const Edge& b) { 
                return (a.source < b.source || (a.source == b.source && a.target < b.target));
            }
        );

        n = adj_array[adj_array.size() - 1].source + 1;
        m = adj_array.size() / 2;

        // Accounts for sentinel element too
        min_index_for = std::vector<size_t>(n+1, adj_array.size());    
        for (size_t i = 0; i < adj_array.size(); i++) {
            Edge edge = adj_array[i];
            min_index_for[edge.source] = std::min(min_index_for[edge.source], i); 
        }  
        // TODO: connectedness check
    }

    int deg(const NodeID node) {
        return min_index_for[node+1] - min_index_for[node];
    }

    bool adjacent(NodeID a, NodeID b) {
        if (deg(a) <= deg(b)) {
            for (int i = min_index_for[a]; i < min_index_for[a+1]; ++i) {
                if (adj_array[i].target == b) {
                    return true;
                }
            }

        } else {
            for (int i = min_index_for[b]; i < min_index_for[b+1]; ++i) {
                if (adj_array[i].target == a) {
                    return true;
                }
            }
        }
        return false;
    }

    bool dominated(NodeID u, NodeID v) {
        return adjacent(u, v) && neighborhoods_is_subset(u, v);
    }

    /*
    Calculates a bit vector "in_suff_set", where
        in_suff_set[v] = false
    if vertex v is not in the initial sufficient set. 

    Note that we only test for the first condition of 
    the notion of domination as used in Staus et al.
    This means our sufficient set might be larger, however
    the practical impact in runtime tends to be negligible.
    */
    std::vector<bool> get_in_suff_set() {
        if (!in_suff_set_cached.empty()) return in_suff_set_cached;

        if (!use_suff) {
            std::vector<bool> in_suff_set(n, true); // all vertices in suff. set
            in_suff_set_cached = in_suff_set;
            return in_suff_set;
        }

        std::vector<bool> in_suff_set(n, false);

        for (int v = 0; v < n; ++v) {
            bool v_needed = true;

            for (int w_i = min_index_for[v]; w_i < min_index_for[v+1]; ++w_i) {
                NodeID w = adj_array[w_i].target;

                if ((deg(v) < deg(w) || (deg(v) == deg(w) && v < w)) && neighborhoods_is_subset(v, w)) {
                    // v is dominated by w
                    in_suff_set[w] = true;
                    v_needed = false;
                    break;
                }
            }
            in_suff_set[v] = v_needed;
        }

        in_suff_set_cached = in_suff_set;
        return in_suff_set;
    }

    /*
    Assumes sub_vert and top_vert are neighbours.
    ignores top_vert in the neighbourhood of sub_vert.
    Otherwise, this method would always return false
    as top_vert is not in its own neighbourhood, but
    in the neighbourhood of sub_vert
    */
    bool neighborhoods_is_subset(const NodeID sub_vert, const NodeID top_vert) {
        if (deg(sub_vert) > deg(top_vert)) return false;

        auto topBeg = adj_array.begin() + min_index_for[top_vert];
        auto topEnd = adj_array.begin() + min_index_for[top_vert+1];
        auto subBeg = adj_array.begin() + min_index_for[sub_vert];
        auto subEnd = adj_array.begin() + min_index_for[sub_vert+1];
        
        for (; subBeg != subEnd; ++topBeg) {
            if (subBeg->target == top_vert) {
                subBeg++;
                topBeg--;
                continue;
            }
            if (topBeg == topEnd || subBeg->target < topBeg->target)
                return false;
            if (!(topBeg->target < subBeg->target))
                ++subBeg;
        }
        return true;
    }

    std::vector<int> get_min_dist_to(const std::vector<int>& group) {
        std::vector<int> dist(n, -1);
        std::queue<int> to_visit;

        for (int v : group) {
            to_visit.push(v);
            dist[v] = 0;
        }

        while (!to_visit.empty()) {
            int node = to_visit.front();
            to_visit.pop();

            for (size_t i = min_index_for[node]; i < min_index_for[node+1]; i++) {
                Edge edge = adj_array[i];
                int neigh = edge.target;

                if (dist[neigh] == -1) { // neigh not visited yet
                    to_visit.push(neigh);
                    dist[neigh] = dist[node] + 1;
                }
            }
        }    
        // farness = std::accumulate(dist.begin(), dist.end(), 0);

        return dist;
    }

    size_t get_num_verts() const {
        return n;
    }

    std::vector<int> compute_reduction(NetworKit::Graph& graph) {
        if (!use_redu) {
            r_n = n;
            std::vector<int> reduction(n, -1);
            std::vector<NodeID> all_node_ids(n);
            std::iota(all_node_ids.begin(), all_node_ids.end(), 0);
            translate = all_node_ids;
            rtranslate = all_node_ids;
            alpha = std::vector<int>(n, 0);
            
            std::cout << "Number of reduced vertices: 0" << std::endl;
            return reduction;
        }

        std::vector<bool> in_suff_set = get_in_suff_set();

        NetworKit::BiconnectedComponents bcc(graph);
        bcc.run();

        std::vector<std::vector<NetworKit::node>> comps = bcc.getComponents();
        std::vector<int> comp_count(graph.numberOfNodes(), 0);

        for (std::vector<NetworKit::node>& comp : comps) {
            for (NetworKit::node v : comp) {
                comp_count[v]++;
            }
        }

        std::vector<int> cut_verts;
        for (int v = 0; v < graph.numberOfNodes(); ++v) {
            if (comp_count[v] > 1) {
                cut_verts.push_back(v);
            }
        }

        int reduced = 0;
        std::vector<int> reduction(graph.numberOfNodes(), -1);

        for (int v : cut_verts) {
            for (int comp_v_id : bcc.getComponentsOfNode(v)) {
                std::vector<NetworKit::node>& comp_v = comps[comp_v_id];
                bool all_dom = true;
                
                for (NetworKit::node u: comp_v) {
                    if (u == v) continue;
                    bool u_dominated_by_v = !in_suff_set[u] && in_suff_set[v];

                    if (!u_dominated_by_v) {
                        all_dom = false;
                        break;
                    }
                }

                if (all_dom) {
                    for (NetworKit::node u: comp_v) {
                        if (u == v) continue;
                        reduction[u] = v;
                    }
                }
            }
        }

        int reduced_verts = 0;
        std::vector<int> reduced_neighs(n, 0);
        translate = std::vector<NodeID>();
        rtranslate = std::vector<NodeID>(n, -1);

        for (int v = 0; v < n; ++v) {
            if (reduction[v] != -1) {
                reduced_verts++;

                int neigh = reduction[v];
                reduced_neighs[neigh]++;
            }
            else {
                translate.push_back(v);
                rtranslate[v] = translate.size()-1;
            }
        }

        r_n = n - reduced_verts;

        alpha = std::vector<int>();
        for (int v = 0; v < r_n; ++v) {
            alpha.push_back(reduced_neighs[translate[v]]);
        }

        std::cout << "Number of reduced vertices: " << reduced_verts << std::endl;

        return reduction;
    }

    void compute_initial_distances(std::vector<int> initial_d) {
        std::vector<int> d;
        for (int v = 0; v < r_n; ++v) {
            d.push_back(initial_d[translate[v]]);
        }

        vertsAtDist = std::vector<std::vector<std::vector<NodeID>>>(r_n, std::vector<std::vector<NodeID>>());
        visited = std::vector<std::vector<bool>>(r_n, std::vector<bool>(n, false));

        for (NodeID v = 0; v < r_n; ++v) {
            vertsAtDist[v].push_back({translate[v]}); // vertsAtDist[v][0] = {translate[v]}
            visited[v][translate[v]] = true;

            for (NodeID i = 1; i <= d[v]; ++i) {
                advanceBFS(v);
            }
        }
    } 

    void advanceBFS(const NodeID u) {
        int last_layer_idx = vertsAtDist[u].size() - 1;
        std::vector<NodeID> last_layer = vertsAtDist[u][last_layer_idx];
        std::vector<NodeID> new_layer;

        for (const NodeID v : last_layer) {
            for (int i = min_index_for[v]; i < min_index_for[v+1]; ++i) {
                NodeID w = adj_array[i].target;
                if (!visited[u][w]) {
                    new_layer.push_back(w);
                    visited[u][w] = true;
                }
            }
        }
        vertsAtDist[u].push_back(new_layer);
    }

    void advanceAllBFS(const std::vector<NodeID>& verts) {
        for (const NodeID v : verts) {
            advanceBFS(v);
        }
    }

    int d(const NodeID v) {
        return vertsAtDist[v].size() - 1;
    }

    int c(const NodeID v, const int i) {
        return alpha[v]*(i+1) + i;
    }

    size_t n;  // number of vertices
    size_t m;  // number of edges
    std::vector<size_t> min_index_for;
    std::vector<Edge> adj_array;

    bool use_suff;  // compute and use the sufficient set  (false to disable)
    bool use_redu; // compute and use data reductions     (false to disable)

    size_t r_n;                        // number of vertices after reduction
    std::vector<NodeID> translate;     // for v in the reduced graph, translate[v] references v in the original graph
    std::vector<NodeID> rtranslate;    // reverse translate, -1 for vertices not in reduced graph
    std::vector<int> alpha;            // alpha[v] is the number of vertices absorbed by v

    // vertsAtDist[v][i] gives a vector of nodes at distance i from v,
    // v is the reduced graph index, the vector contains original graph node IDs
    std::vector<std::vector<std::vector<NodeID>>> vertsAtDist;

    // visited[v][w] = true iff in the bfs tree of the node v of the reduced graph,
    // node w of the original graph was already visited.
    std::vector<std::vector<bool>> visited;

    std::vector<bool> in_suff_set_cached;
};
