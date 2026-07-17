# Task: Reproduce Main Paper Results

## Goal
Reproduce the paper's core experimental results — Level 2 multi-agent DSE
and black-box BO baseline.

## API Required
**Yes** — Requires Claude API access or compatible LLM endpoint.

## Estimated Time
Hours to days (depending on search breadth and concurrency)

## Token Budget
~2.15B tokens per design (paper claim)

---

## Pre-Flight

1. ✅ `make reproduce-baseline` must have passed
2. ✅ API credentials configured:
   ```bash
   export ANTHROPIC_API_KEY=...
   # or: configured in dpl_evolve_agent/env.sh
   ```
3. ✅ Token budget understood and accepted
4. ✅ Run `make provenance` before starting

---

## Execution Steps

### Step 1: Verify API connectivity
```bash
$DPL_EVOLVE_PYTHON -c "
import os
# Simple check — does the key exist?
key = os.environ.get('ANTHROPIC_API_KEY', '')
print(f'API key length: {len(key)} chars') if key else print('NO API KEY SET')
"
```

### Step 2: Run BO baseline (optional, for comparison)
```bash
cd $DPL_EVOLVE_AGENT_ROOT
source env.sh
bash experiments/launchers/run_bo_9case_openroad_dpl.sh
```

### Step 3: Run Level 2 DSE
```bash
cd $DPL_EVOLVE_AGENT_ROOT
source env.sh
bash experiments/launchers/run_evolve_9case_place_batch.sh
```

### Step 4: Collect and validate
```bash
bash scripts/agent/validate_run.sh --experiment main
```

### Step 5: Generate tables
```bash
make table-2
make table-3
```

---

## What to Expect

### Normal Variation
- HPWL improvement: 1.5%–2.5% (paper: 1.78%)
- BO improvement: 0.2%–0.6% (paper: 0.38%)
- Some cases may show negative improvement (regression)
- Student proposals vary significantly between runs

### Warning Signs
- All cases show identical improvement → possible metric bug
- No improvement over baseline → possible pipeline issue
- Massive token usage without convergence → possible loop bug

---

## Failure Handling

### API Errors
- Rate limiting: wait and retry with backoff
- Authentication: verify key is valid and not expired
- Context length: reduce prompt size or use a different model

### DSE Errors
- Compilation failures: check that Student patches apply cleanly
- Evaluation failures: check that the candidate binary runs
- Timeout: the DSE loop has a configurable timeout; check config

### Budget Exhaustion
- If token budget is exhausted before convergence, report partial results
- Do NOT silently increase budget

---

## Expected Output

```
results/reproduced/main/
├── evolve_9case/
│   ├── suite_results.json
│   ├── per_case_metrics.tsv
│   └── token_usage.json
├── bo_9case/
│   ├── suite_results.json
│   └── per_case_metrics.tsv
└── OUTCOME.txt
```
