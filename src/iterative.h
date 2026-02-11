#pragma once

#include "gurobi_c++.h"
#include <vector>
#include <string>
#include <chrono>

#include "graph.h"
#include "solution.h"


template <class T>
class IterativeSolver {
public:
    IterativeSolver(Graph& graph, size_t k, Solution<T> greedy_sol) 
    : graph(graph), k(k), n(graph.n), reduced_n(graph.r_n), greedy_sol(greedy_sol), 
      sufficient_set_is_solution(false), env(nullptr), model(nullptr) {

        setup_env();
        model_time = 0;

        in_suff_set = graph.get_in_suff_set();
        int dominated_v = 0;
        for (int v = 0; v < n; ++v) {
            dominated_v += !in_suff_set[v];
        }
        std::cout << "Dominated vertices: " << dominated_v << std::endl;

        // Check sufficient set has more than k nodes,
        // if not, the ILP does not need to be solved
        // as the sufficient set can be extended to an
        // optimum solution.
        size_t num_sufficient = 0;
        for (int v = 0; v < n; ++v) {
            if (in_suff_set[v]) {
                num_sufficient++;
            }
        }

        if (num_sufficient <= k) {
            sufficient_set_is_solution = true;

            size_t verts_to_readd = k - num_sufficient;
            size_t curr_idx = 0;

            while (verts_to_readd > 0) {
                if (!in_suff_set[curr_idx]) {
                    in_suff_set[curr_idx] = true;
                    verts_to_readd--;
                }
                curr_idx++;
            }

            sufficient_set = compute_sufficient_set();
            return;
        }
        
        // transform s.t. in_suff_set[v] returns if v (in the reduced graph!) is in the sufficient set
        std::vector<bool> new_in_suff_set;
        for (int v = 0; v < reduced_n; ++v) {
            new_in_suff_set.push_back(in_suff_set[graph.translate[v]]);
        }
        in_suff_set = new_in_suff_set;
    }

    ~IterativeSolver() {
        if (model != nullptr) delete model;
        if (env != nullptr) delete env;
    }

    Solution<T> solve() {
        if (sufficient_set_is_solution) {
            std::vector<int> dists = graph.get_min_dist_to(sufficient_set);
            int score = std::accumulate(
                dists.begin(),
                dists.end(),
                0
            );

            return { sufficient_set, score };
        }

        setup_initial_model();
        int iters = 0;

        for (;;) {
            Solution<T> solution = solve_model();

            iters++;

            if (variables_sufficient()) {
                std::cout << "ILP Iterations: " << iters << std::endl;
                std::cout << "MIP Time (Seconds): " << model_time << std::endl;
                return solution;
            }

            // variables insufficient
            add_variables();
        }
    }

private:
    void setup_env() {
        env = new GRBEnv(true);
        env->set(GRB_IntParam_OutputFlag, 0);
        env->set(GRB_IntParam_Threads, 1);
        env->start();
    }

    void setup_initial_model() {
        model = new GRBModel(*env);
        model->set(GRB_DoubleParam_MIPGap, 0);
        //model->set(GRB_IntParam_ZeroHalfCuts, 0); // disabling ZH cuts can be beneficial

        for (int v = 0; v < reduced_n; ++v) {
            int i = 0;
            if (!in_suff_set[v]) {
                // x_v_0 not needed
                i = 1;
            }

            for (; i <= graph.d(v); ++i) {
                model->addVar(0.0, 1.0, 0.0, GRB_INTEGER, name(v, i));
            }
        }

        model->update();

        if (!greedy_sol.vertex_set.empty()) {
            std::vector<int> min_dist = graph.get_min_dist_to(greedy_sol.vertex_set);
            for (int v = 0; v < reduced_n; ++v) {
                if (!in_suff_set[v]) {
                    continue;
                }

                model->getVarByName(name(v, 0)).set(GRB_DoubleAttr_Start, min_dist[graph.translate[v]] == 0);
            }
        }

        GRBLinExpr sum_verts_in_s = 0;
        for (int v = 0; v < reduced_n; ++v) {
            if (!in_suff_set[v]) continue;

            sum_verts_in_s += model->getVarByName(name(v, 0));
        }
        model->addConstr(sum_verts_in_s, GRB_LESS_EQUAL, k);

        for (int v = 0; v < reduced_n; ++v) {
            GRBLinExpr dist_active = 0;
            int i = 0;
            if (!in_suff_set[v]) {
                // x_v_0 does not exist
                i = 1;
            }

            for (; i <= graph.d(v); ++i) {
                dist_active += model->getVarByName(name(v, i));
            }
            model->addConstr(dist_active, GRB_EQUAL, 1, std::string("c_") + std::to_string(v)); 
        }

        for (int v = 0; v < reduced_n; ++v) {
            for (int i = 1; i <= graph.d(v)-1; ++i) {
                GRBLinExpr x_v_i = model->getVarByName(name(v, i));
                GRBLinExpr upper = 0;

                for (NodeID w : graph.vertsAtDist[v][i]) {
                    w = graph.rtranslate[w];
                    if (w == -1) continue;

                    if (!in_suff_set[w]) {
                        // x_w_0 does not exist
                        continue;
                    }
                    upper += model->getVarByName(name(w, 0));
                }
                model->addConstr(x_v_i - upper, GRB_LESS_EQUAL, 0);
            }
        }

        GRBLinExpr objective = 0;
        for (int v = 0; v < reduced_n; ++v) {
            int i = 0;
            if (!in_suff_set[v]) {
                // x_v_0 does not exist
                i = 1;
            }
            for (; i <= graph.d(v); ++i) {
                objective += graph.c(v, i) * model->getVarByName(name(v, i));
            }
        }
        model->setObjective(objective, GRB_MINIMIZE);
    }

    std::string name(const int v, const int d) {
        return std::to_string(v) + "_" + std::to_string(d);
    } 

    Solution<T> solve_model() {
        std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
        model->optimize();
        std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
        double elapsed_time_seconds = (double) std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() / 1000;
        model_time += elapsed_time_seconds;

        std::vector<int> solution;
        for (int v = 0; v < reduced_n; ++v) {
            float vertex_score = 0; 
            if (in_suff_set[v]) {
                vertex_score = model->getVarByName(name(v, 0)).get(GRB_DoubleAttr_X);
            }
            
            // > 0.99 protects against numerical effects (vs == 1)
            if (vertex_score > 0.99) {
                solution.push_back(graph.translate[v]);
            }
        }

        // f(solution)
        int solution_score = (int) (model->get(GRB_DoubleAttr_ObjVal) + 0.01); // +0.01 against numerical effects

        return { solution, solution_score };
    }

    bool variable_sufficient(int v) {
        bool sufficient = true;
        float x_v_dv = model->getVarByName(name(v, graph.d(v))).get(GRB_DoubleAttr_X);

        // d(v) >= ecc(v) is not checked for simplicity
        if (x_v_dv > 0.99) { 
            sufficient = false; // > 0.99 protects against numerical effects (vs == 1)
        }
        return sufficient;
    }

    bool variables_sufficient() {
        bool sufficient = true;

        for (int v = 0; v < reduced_n; ++v) {
            if(!variable_sufficient(v)) {
                sufficient = false;
                break;
            }
        }
        return sufficient;
    }

    void add_variables() {
        std::vector<NodeID> advanceBFSNodes;
        std::vector<int> d_inc(reduced_n, 0); // could use bool vector

        for (int v = 0; v < reduced_n; ++v) {
            if(!variable_sufficient(v)) {
                d_inc[v] = 1;
                advanceBFSNodes.push_back(v);
            }
        }

        graph.advanceAllBFS(advanceBFSNodes);

        // add vars and add/modify constraints

        for (int v = 0; v < reduced_n; ++v) {
            if (d_inc[v] == 0) continue;

            int c_v_dv = graph.c(v, graph.d(v));
            model->addVar(0.0, 1.0, c_v_dv, GRB_INTEGER, name(v, graph.d(v)));
        }
        model->update();

        for (int v = 0; v < reduced_n; ++v) {
            if (d_inc[v] == 0) continue;

            GRBConstr c_v = model->getConstrByName(std::string("c_") + std::to_string(v));
            model->chgCoeff(c_v, model->getVarByName(name(v, graph.d(v))), 1.0);
        }

        for (int v = 0; v < reduced_n; ++v) {
            if (d_inc[v] == 0) continue;

            GRBLinExpr x_v_dvminus1 = model->getVarByName(name(v, graph.d(v)-1));

            GRBLinExpr upper = 0;
            for (NodeID w : graph.vertsAtDist[v][graph.d(v)-1]) {
                w = graph.rtranslate[w];
                if (w == -1) continue;

                if (!in_suff_set[w]) {
                    // x_w_0 does not exist
                    continue;
                }
                upper += model->getVarByName(name(w, 0));
            }
            model->addConstr(x_v_dvminus1 - upper, GRB_LESS_EQUAL, 0);
        }        
    }

    std::vector<T> compute_sufficient_set() {
        std::vector<T> sufficient_set;
    
        for (size_t v = 0; v < graph.get_num_verts(); ++v) {
            if (in_suff_set[v]) {
                sufficient_set.push_back(v);
            }
        }
        return sufficient_set;
    }

    // use reference since copying is expensive
    Graph& graph;
    size_t k;
    size_t n;
    size_t reduced_n;

    Solution<T> greedy_sol;
    std::vector<bool> in_suff_set;
    std::vector<int> sufficient_set;
    bool sufficient_set_is_solution;

    GRBEnv* env;
    GRBModel* model;

    double model_time;
};
