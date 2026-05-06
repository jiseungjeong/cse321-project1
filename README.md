# CSE321 Project #1 — B-Tree Index Structures

From-scratch C++17 implementation of three index structures (B-tree,
B\*-tree, B+-tree) plus benchmark harness and analysis pipeline. Submitted
for CSE321 Database Systems, Spring 2026.

The trees are implemented in `src/btree.{hpp,cpp}`,
`src/bstar_tree.{hpp,cpp}`, `src/bplus_tree.{hpp,cpp}`. **No standard-library
tree containers** (`std::map`/`set`/`multimap`/`multiset`) are used in the
implementation — verified by `grep`.

---

## Requirements

| Tool          | Version                           | Purpose             |
| ------------- | --------------------------------- | ------------------- |
| C++ compiler  | g++ ≥ 9 or clang++ ≥ 11 (C++17)   | build               |
| GNU make      | any recent                        | build orchestration |
| `student.csv` | provided dataset, 100 000 records | input               |

Optional (only for regenerating analysis figures, not for grading):

| Tool       | Version | Purpose           |
| ---------- | ------- | ----------------- |
| Python     | ≥ 3.8   | `scripts/plot.py` |
| matplotlib | ≥ 3.5   | figure rendering  |

No other dependencies. Tested on macOS (clang++ 16) and Linux (g++ 11).

---

## Quick start

```bash
make                         # build the ./run binary (release, -O2 -DNDEBUG)
./run all --csv=student.csv  # runs the full experiment matrix
                             #   3 trees × 3 orders × 4 mandatory + bonus
                             # writes results/{insert,search,range,delete}.csv
                             # ~2 seconds on a modern machine
```

For a one-line "do everything" reproducer:

```bash
bash scripts/run_all.sh
```

That wrapper builds the binary if missing, runs the full matrix, and
prints row-count sanity checks. All output CSVs land in `results/`.

---

## Layout

```
ass1/
├── student.csv             input dataset (provided by course)
├── src/                    C++ sources
│   ├── record.hpp          Record / RID typedefs
│   ├── csv_loader.{hpp,cpp}
│   ├── iindex.{hpp,cpp}    abstract IIndex interface + factory
│   ├── btree.{hpp,cpp}     B-tree (CLRS-style, bottom-up split)
│   ├── bstar_tree.{hpp,cpp} B*-tree (redistribute-first, 2-to-3 split)
│   ├── bplus_tree.{hpp,cpp} B+-tree (separator-only internals, leaf links)
│   ├── invariant.{hpp,cpp}  6 structural invariants for verification
│   ├── timer.hpp           steady_clock RAII wrapper
│   ├── experiments.{hpp,cpp} four mandatory experiment drivers
│   └── main.cpp            CLI dispatch
├── Makefile                make / make debug / make clean / make figures
├── scripts/
│   ├── run_all.sh          shell wrapper for `./run all`
│   └── plot.py             figure generator (matplotlib)
├── results/                experiment CSVs (generated; not tracked)
└── README.md               this file
```

---

## CLI reference

The single binary `./run` exposes one subcommand per experiment plus
`load`, `test`, and the umbrella `all` command.

```
./run load    --csv=PATH                                    # smoke-test loader
./run test    --tree=TYPE --d=N [--csv=...]                 # self-test (invariants
                                                            #   + insert/search/delete)
./run insert  --tree=TYPE --d=N --csv=... [--out=PATH]
./run search  --tree=TYPE --d=N --csv=... --queries=Q --seed=S [--out=PATH]
./run range   --tree=TYPE --d=N --csv=... --lo=K --hi=K [--out=PATH]
./run delete  --tree=TYPE --d=N --csv=... --ratio=R --seed=S [--out=PATH]
./run all     --csv=... [--out-dir=DIR] [--seed=S] [--queries=Q]
              [--lo=K] [--hi=K] [--ratios=A,B,...] [--ds=A,B,...]
./run range-sweep --csv=... [--d=N] [--lo=K] [--out=PATH]   # bonus
```

`TYPE` ∈ `{btree, bstar, bplus}`. Defaults match the assignment:

| flag           | default                                 |
| -------------- | --------------------------------------- |
| `--seed`       | `42`                                    |
| `--queries`    | `10000` (point-search workload)         |
| `--lo`, `--hi` | `202000000`, `202010000` (range window) |
| `--ratios`     | `0.02,0.10,0.20` (delete experiment)    |
| `--ds`         | `3,5,10` (orders)                       |
| `--out-dir`    | `results`                               |

When `--out` is omitted, individual experiment commands print one CSV
row to stdout (header included on a fresh file).

---

## Reproducing the experimental results

```bash
make                           # build the binary
./run all --csv=student.csv    # mandatory experiments (4 CSVs in results/)
./run range-sweep --csv=student.csv --d=5                          # bonus 1
./run range-sweep --csv=student.csv --d=3 --out=results/range_sweep_d3.csv
./run all --csv=student.csv \
    --ds=3,4,5,6,8,10,16,32,64,128 --out-dir=results/sweep         # bonus 2
python3 scripts/plot.py        # optional: regenerate figures (PNG + PDF)
```

The report PDF is submitted separately via Blackboard and is not
distributed through this repository.

---

## Verifying correctness

- **`./run test --tree=TYPE --d=N`** runs a self-contained test:
  inserts 100 000 records, checks all six structural invariants, deletes
  50 000 random keys, and re-checks invariants. All nine combinations
  (3 trees × 3 orders) pass.
- **Cross-tree parity**: every range-query row in `results/range.csv`
  must agree on `hits=1403`, `male_hits=712`, `avg_gpa=3.28716`,
  `avg_height=173.853`. The `./run all` command also runs a brute-force
  scan and prints the reference numbers at the end.
- **Sanitizers**: `make debug` builds with `-fsanitize=address,undefined`.
  All experiments pass clean under sanitizers.

---

## Implementation notes

### Order definition

We adopt Knuth's order $d$ = max children per node. Internally this maps
to the CLRS minimum-degree convention $t = \lceil d/2 \rceil$ with
`max_keys = 2t - 1` and `min_keys (non-root) = t - 1`. This is the only
mapping that keeps merge results within capacity for every $d \ge 3$
including odd values; see `src/btree.cpp` and the report §2.1.

### Range window interpretation

The dataset uses 9-digit Student IDs in the format
`2020xxxxx`–`2026xxxxx`, so the range query is evaluated on the
9-digit window `202000000`–`202010000`, preserving the `2020*`
prefix of the assignment's example. This is the default
`--lo`/`--hi` for `./run range` and `./run all`.

### B\*-tree details

`splits` in `TreeStats` counts both 1-to-2 and 2-to-3 splits (each grows
node count by one). Insert-time and delete-time redistributes are
aggregated into a single `redistributes` field; the implementation keeps
them separate (`redistributes_` vs `del_redists_` in `BStarTree`) and
the report uses the split.

### B+-tree separator invariant

We weakened the canonical $\textrm{sep} = \min(\textrm{right subtree})$
to $\max(\textrm{left}) < \textrm{sep} \le \min(\textrm{right})$, which
is sufficient for search correctness and avoids stale-separator
bookkeeping during deletion. Correctness is verified by leaf-link
sortedness plus a multiset equality check against the inserted keys.

---

## Submission

- Report PDF: submitted to Blackboard separately (per assignment §3.2,
  the public repository carries source code and dataset only).
- Source repository: <https://github.com/jiseungjeong/cse321-project1>
- Dataset: `student.csv` (kept in repo for reproducibility).

Contact: see the course Blackboard for the TA's email.
