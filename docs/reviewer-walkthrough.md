# Reviewer walkthrough

This page is a clean-clone simulation of the artifact-evaluation process. It
separates a short confidence-building run from complete campaigns so a reviewer
does not accidentally start thousands of EDA jobs or paid model calls.

## 1. Clone and inspect

```bash
git clone https://github.com/yuan-fd/DPLEvolve-AE.git
cd DPLEvolve-AE
make help
make doctor
```

Expected outcome: Doctor identifies the host, checks required build commands,
memory and disk, and reports whether the external ORFS workspace and binaries
already exist. Warnings about an unprepared workspace are normal on a clean
clone. Doctor never installs anything.

## 2. Optional browser console

On the evaluation server:

```bash
cd DPLEvolve-AE
bash web-demo/start.sh
```

If the browser is on the same machine, open `http://127.0.0.1:8080`. For a
remote server, run this on the reviewer's laptop and keep it open:

```bash
ssh -N -L 8080:127.0.0.1:8080 USER@SERVER
```

Then open `http://127.0.0.1:8080` on the laptop. Do not bind the reviewer
console to a public interface.

The recommended Web sequence is:

1. **Reviewer Environment Doctor**;
2. **Prepare one-target reviewer environment**;
3. **Run AES default + HPWL replay**;
4. inspect the live log and export the session JSON;
5. download Table 6 data and run the one-row Table 6 check;
6. start a full campaign only after reviewing its confirmation dialog.

The console invokes fixed commands only. It cannot accept arbitrary shell
input, and it does not bypass the LLM-cost acknowledgement.

## 3. Build the pinned environment

```bash
make reviewer-prepare THREADS=8
```

This convenience target bootstraps the pinned ORFS/OpenROAD/Yosys revisions,
builds the tools, prepares AES Nangate45, and validates the protected evaluator.
Products are stored in sibling workspaces rather than committed into the
repository.

If the host is smaller than the author server, reduce `THREADS` and run one
case at a time.

## 4. Run the shortest real paper-connected path

```bash
make reviewer-aes-result THREADS=8
```

This is not a smoke-only path. It creates a paper Table 4 input, records the
complete protected trajectory, runs the same-case default, rebuilds one frozen
selected source program, and executes it with OpenROAD.

Inspect the printed `metrics.json` and replay `results.tsv` paths. A usable
result has:

- successful build and process exit;
- canonical final HPWL from the OpenROAD DPL log;
- `H_g`, `H_lg`, `H_ip`, and `H_f`;
- clean strict placement legality;
- average and maximum displacement;
- runtime and mechanism-liveness evidence.

Hash availability is not a pass/fail requirement for scientific reproduction.
Missing paper-time hashes are reported as provenance limitations; fresh legal
metrics and numerical acceptance remain the decision basis.

## 5. Reproduce complete Table 4

```bash
make prepare-paper-inputs THREADS=16
make reproduce-table4 THREADS=10
```

This is a large campaign. BO alone performs 9 × 400 placements. The final
`table4-fresh.tsv` is built from the new default metrics, BO best trials, and
both frozen-source replay tracks.

To isolate failures, run one case with `CASE=` before launching all nine.

## 6. Reproduce Table 6

```bash
make reviewer-table6-one THREADS=10
```

The one-row command is a real OpenROAD execution from retained exact
DEF/Verilog/SDC. After it passes, run the complete 27-job matrix:

```bash
make reproduce-table6 THREADS=10
```

Every fresh job uses a 7200-second cap. The output is derived from OpenROAD
status and strict `check_placement`, not from the archived Table 6 JSON.

## 7. Rebuild Figures 4 and 5

```bash
make reproduce-figures FIGURE_SOURCE=retained
```

This checks and redraws retained author-run plotting data. Figure 4 reports 96
observed points plus a JSON list of three unretained points. It never fills the
gaps. After a fresh full search, use:

```bash
make reproduce-figures FIGURE_SOURCE=fresh DSE_RUN_PREFIX=review_run_01
```

## 8. Exercise ReviewDSE without a full campaign

```bash
make plan-level1
make plan-dse-paper
make run-dse-small CASE=aes_nangate45 STUDENTS=1 ITERATIONS=1 THREADS=8
```

The plan commands inspect the configured launch without dispatching it; they
are not an alternative method path. `run-dse-small` is a real, bounded
Teacher/Student source-edit, build, evaluate, and review loop and therefore
requires model access.

Do not run the following unless the budget is explicitly authorized:

```bash
make reproduce-paper-search ACKNOWLEDGE_LLM_COST=yes THREADS=10
```

## 9. Reproduce Table 5

```bash
make check-table5-data
make reproduce-table5 THREADS=10
```

The check verifies the complete manifest for the retained LEGALM, Diamond, and
Negotiation snapshots. The replay regenerates the three inputs with Table-5-
local utilization values 70/90/60, executes all six mapped roles, and writes
`table5-fresh.tsv` under the timestamped Table 5 reproduction directory.

## 10. Preserve the evaluation record

Record:

- commit SHA and host configuration;
- exact command and thread count;
- generated input location;
- `metrics.json`, summary TSV, and log paths;
- pass/fail status and observed numerical difference.

The Web Demo can export its session JSON. Terminal users can keep command logs
and the generated summaries under `DPL_EVOLVE_STATE_ROOT`.
