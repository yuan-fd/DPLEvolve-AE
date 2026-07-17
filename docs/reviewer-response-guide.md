# Reviewer Response Guide — Camera-Ready Revision Strategy

## Context
Two reviewers, both scored Borderline (0). Paper accepted as Short Paper.
All requested changes are for the camera-ready version.

---

## Reviewer 1: Three Required Fixes

### 1. "框架结构缺乏消融实验与机理解释"

**What they want**: Quantitative evidence that each architectural component matters.

**What you can do before 7/27 deadline**:
- **Already have evidence**: The backup contains multiple experiment runs with
  different configurations (width4x10 vs width4x15, framework vs evolved_legalm start,
  various retry/scope-fix runs). These demonstrate the impact of search breadth
  and start-point quality.
- **Can add to paper**: A paragraph describing how search breadth (4 vs fewer)
  and start-point quality (framework vs blank) affect convergence speed.
- **Full ablation configs exist**: `configs/ablation/level1_warmup.yaml` and
  `configs/ablation/teacher_ablation.yaml` are ready but require API to run.

**Suggested camera-ready text**:
> "We compare two search widths (4×10 vs 4×15 iterations) across nine targets.
> The wider search (4×15) achieves a mean improvement of -1.05% vs -0.82% for
> 4×10, suggesting that additional iterations continue to find useful mechanisms.
> We also compare framework-initialized starts against blank evolved_legalm starts:
> framework initialization reduces early-iteration failure rate from X% to Y%."

### 2. "HPWL提升幅度太小 (0.3%–1.9%)，质疑统计显著性"

**What they want**: Mean ± std, multi-seed, multi-technology validation.

**What you can do**:
- **Already have evidence**: Backup data shows per-case values across multiple
  experiment runs. Ariane133 varies from -0.18% (rerun1) to -5.50% (framework restart
  width4x10), demonstrating case-level variance.
- **Multi-technology**: Paper already covers Nangate45 (5 cases) and ASAP7 (4 cases).
  Both technology nodes show consistent improvement.
- **Full multi-seed config**: `configs/ablation/multi_seed.yaml` is ready.

**Suggested camera-ready text**:
> "Across three independent experiment runs on the nine-case suite, ReviewDSE-HPWL
> achieves a mean improvement of X% ± Y% (std). Per-case variance is design-dependent:
> some cases (Ariane133, AES N45) show large improvements in specific runs due to
> successful mechanism discovery, while others show smaller but consistent gains.
> The multi-technology evaluation (Nangate45 + ASAP7) confirms that the approach
> generalizes across technology nodes."

### 3. "基线对比逻辑模糊 — 缺少内外层联合探索"

**What they want**: Experiment combining black-box parameter tuning + white-box source changes.

**What you can do**:
- **Acknowledge as future work**: This is a genuinely new experiment that would
  require significant new code. Not feasible before 7/27.
- **The paper already partially addresses this**: Table 4 includes both BO-DSE
  (black-box only) and ReviewDSE (white-box only). The GHR variant shows
  runtime-aware selection, which is a form of joint optimization.

**Suggested camera-ready text**:
> "We note that black-box parameter search and white-box source exploration are
> complementary rather than mutually exclusive. Joint optimization — where BO
> tunes mechanism activation parameters while the Teacher-Student loop discovers
> new mechanisms — is a natural extension. We leave this for future work, noting
> that the current results already show ReviewDSE mechanisms operating under
> different parameter settings (Table 4, GHR column)."

---

## Reviewer 2: Three Shortcomings

### 1. "实验场景单一 — 仅覆盖详细布局单阶段"

**Response**: This is by design. The paper's contribution is the white-box DSE
**methodology** instantiated on detailed placement. The same protected-evaluator
architecture applies to any staged optimizer. We acknowledge this limitation
in Section 5 and suggest routing, timing optimization, and CTS as natural extensions.

### 2. "HPWL增益有限: 1.78%属于小幅改进"

**Response**:
- In physical design, 1-2% HPWL improvement at the detailed placement stage is
  meaningful: it compounds with routing and clock tree improvements downstream.
- More importantly, the paper's contribution is not the absolute percentage but
  the **paradigm shift** (black-box → white-box DSE) and the **evidence of concept**
  that LLM agents can discover useful source-level mechanisms.
- The cut-row repair results (Table 6) demonstrate that white-box DSE solves
  problems where black-box methods completely fail — this is arguably more
  significant than the average HPWL improvement.

### 3. "搜索资源开销极大: 21.5亿 token"

**Response**: See `docs/token-cost-analysis.md` for detailed analysis.
Key points:
- 2.15B is **logged** tokens (with caching), only 0.10B are **active** (billable)
- Effective cost is $1-3 per design with prompt caching
- Comparable to a few hours of EDA engineer time
- Not claimed as cheap; positioned for high-value designs

---

## Recommended Camera-Ready Additions

| Priority | Addition | Effort |
|---|---|---|
| P0 | Token cost clarification (logged vs active, actual $ cost) | 1 hour |
| P0 | Per-case variance table (show data from multiple runs) | 2 hours |
| P0 | Clarify that BO-DSE uses Optuna TPE (no LLM) vs ReviewDSE uses GPT agents | 30 min |
| P1 | Ablation discussion paragraph (search width, start-point quality) | 2 hours |
| P1 | Joint optimization discussion (acknowledge as future work) | 30 min |
| P2 | Add multi-seed statistics (if you can run 2-3 seeds on 1-2 cases) | Requires API |
| P2 | Add timing/congestion metrics (if OpenROAD flow supports it) | Requires verification |

---

## What NOT to Do

- **Don't fabricate data**: Never invent ablation or multi-seed numbers
- **Don't overclaim**: The paper is a short paper — be clear about limitations
- **Don't add rushed experiments**: A broken experiment is worse than a missing one
- **Don't ignore the reviewers**: Even if you can't run new experiments,
  acknowledge each concern and explain why it's not yet addressed
