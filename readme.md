# Grover

An exact solver for the **Group Closeness Centrality Maximization** problem.

## Problem Definition

Given a connected, undirected, unweighted graph *G = (V, E)* and a positive integer *k*, find a set *S ⊆ V* of size *k* that minimizes the group farness:

```
f(S) = Σ min{dist(v, s) : s ∈ S}
       v∈V
```

This maximizes the group closeness centrality: *c(S) = (|V| - k) / f(S)*

## Quick Start

### Build

```bash
cmake -B build
cmake --build build
```

Or use the provided script:
```bash
./build.sh
```

### Run

Exact solver:
```bash
./build/grover graph.edges 10
```

Fast approximation:
```bash
./build/grover graph.edges 10 --approx
```

## Usage

```
grover <graph_file> <k> [OPTIONS]
```

### Arguments

- `<graph_file>` - Path to graph file in edge list format
- `<k>` - Size of the group to find (1 ≤ k ≤ |V|)

### Options

| Option | Description |
|--------|-------------|
| `-h, --help` | Show help message |
| `--approx` | Use fast approximation (greedy + reduction-aware local search) |
| `--no-sufficient` | Disable sufficient set computation |
| `--no-reductions` | Disable data reductions |
| `--no-heuristic` | Disable heuristic initialization (use d(v)=2 for all v) |
| `--init <file>` | Provide initial solution from file |

## Examples

Basic usage:
```bash
./grover graph.edges 5
```

Fast approximation:
```bash
./grover graph.edges 10 --approx
```

Disable heuristic initialization:
```bash
./grover graph.edges 10 --no-heuristic
```

Use initial solution:
```bash
./grover graph.edges 8 --init solution.txt
```

Disable all optimizations:
```bash
./grover graph.edges 5 --no-sufficient --no-reductions --no-heuristic
```

## Input Format

### Graph File

Edge list format with one edge per line:
```
0 1
0 2
1 3
2 3
```

Node IDs should be consecutive integers starting from 0.

### Initial Solution File

Single line with space-separated node IDs:
```
0 20 26 45 103
```

The number of nodes must equal *k*.

## Output

The solver outputs:
- **Graph statistics** (nodes, edges, loading time)
- **Heuristic scores** (if enabled: GS Score, LS Score)
- **Exact Score** - The optimal group farness
- **Solution** - The optimal group as a set of node IDs
- **Elapsed Time** - Total computation time in seconds

Example output:
```
Graph Time (Seconds): 0.123
Number of nodes: 1000
Number of edges: 5000
GS Score: 4523
GS Time (Seconds): 0.456
LS Score: 4321
LS Time (Seconds): 0.234
Initial Distance Time (Seconds): 0.089
Exact Score: 4280
Solution: {12, 45, 78, 123, 456}
Elapsed Time (Seconds): 2.345
```

## Algorithm Features

### Exact Mode (default)

**Optimizations:**
1. **Sufficient Set Computation** - Reduces the search space by identifying vertices that cannot be in an optimal solution
2. **Data Reductions** - Applies graph reduction rules to simplify the problem
3. **Heuristic Initialization** - Uses Grow-Shrink and Local Search to find good initial bounds

All optimizations are enabled by default. Note that data reductions require sufficient set computation.

### Approximation Mode (`--approx`)

Fast approximation using:
1. **Greedy Algorithm** - NetworKit's GroupCloseness for initial solution
2. **Reduction-Aware Local Search** - Skips dominated vertices during local search

**Benefits:**
- No Gurobi dependency for this mode
- Much faster than exact solver
- Suitable for large graphs where exact solutions are infeasible

### Performance

For *k = 1*, the solver returns the exact solution using a greedy algorithm in linear time.

For *k > 1*:
- **Exact mode**: Uses iterative approach with branch-and-bound
- **Approx mode**: Greedy + local search (typically seconds vs minutes/hours)

## Dependencies

- **CMake** 3.10+
- **C++17** compiler
- **NetworKit** (downloaded and patched automatically)
- **Gurobi** (downloaded automatically, only required for exact mode)

## License

See individual source files for licensing information.

## Citation

If you use *Grover* in your research, please cite the following paper:

[Advances in Exact and Approximate Group Closeness Centrality Maximization](https://arxiv.org/abs/2603.25642)

```
@misc{Schulz2026,
  title        = {Advances in Exact and Approximate Group Closeness Centrality Maximization},
  author       = {Christian Schulz and Jakob Ternes and Henning Woydt},
  year         = {2026},
  eprint       = {2603.25642},
  archivePrefix= {arXiv},
  primaryClass = {cs.DS},
  url          = {https://arxiv.org/abs/2603.25642}
}
```
