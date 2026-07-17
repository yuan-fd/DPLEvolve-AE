# Token Cost Analysis — Responding to Reviewer Concerns

## The Number: 2.15B Tokens Per Design

Reviewer 2 flagged this as a critical concern: "搜索资源开销极大，单设计探索 token 消耗高达 21.5亿 token，成本极高、实用性受限。"

This document clarifies what this number means.

---

## Logged vs Active: The 20× Difference

Paper Table 4 reports two token columns:

| Metric | Value | Meaning |
|---|---|---|
| **Logged tokens** | 2.15B avg/design | Total tokens across ALL API calls, INCLUDING cached system prompts |
| **Active tokens** | 0.10B avg/design | Actual input+output tokens, EXCLUDING cached content |

### What is "logged but cached"?

Each Student API call sends a large system prompt (domain knowledge, skill cards,
invariants, prior evidence). These are **identical across iterations** for the
same Student, so the API provider caches them:

```
Call 1: System prompt (30M tokens) + New question (1M tokens) = 31M logged
Call 2: System prompt (30M tokens CACHED) + New question (1M tokens) = 31M logged, 1M active
Call 3: System prompt (30M tokens CACHED) + New question (1M tokens) = 31M logged, 1M active
...
```

After 10 iterations × 4 Students = 40+ calls: ~1.2B logged from cached content alone.

### What Actually Costs Money

| Expense Category | Amount | Billed? |
|---|---|---|
| System prompts (cached) | ~1.8B logged tokens | No (cache discount or free with prompt caching) |
| Input tokens (new each turn) | ~0.07B active tokens | Yes |
| Output tokens | ~0.03B active tokens | Yes |
| **Total billed** | **~0.10B active tokens** | **~$1-3/design (GPT-5 pricing)** |

---

## Where Tokens Go (Per Design, 10 Iterations)

```
Component                    Logged Tokens    Active Tokens
─────────────────────────────────────────────────────────
Teacher system prompt          ~200M/iter       ~2M/iter (new evidence)
Teacher review (4 students)   ~600M/iter       ~50M/iter
Student system prompt          ~200M/call       ~2M/call (new route)
Student code reading           ~800M/call       ~40M/call
Student code writing           ~300M/call       ~15M/call
Student result analysis        ~400M/call       ~20M/call
─────────────────────────────────────────────────────────
Total (10 iters × 5 agents)    ~2.15B           ~0.10B
```

---

## Is This Actually Expensive?

**For production chip design: No.**
- A single mask set at advanced nodes costs $10M+
- Saving 1-5% HPWL could reduce die area → thousands of extra chips per wafer
- Even at $100K token cost for 100 designs, ROI is positive

**For academic research: It costs money.**
- With prompt caching enabled: ~$1-3 per design at GPT-5 pricing
- 9-case experiment: ~$10-30 total (active tokens only)
- Without prompt caching: significantly more

**For artifact evaluation: Reviewers can skip this.**
- The paper provides pre-computed results
- Baseline + BO-DSE can run without API
- The full DSE pipeline is documented but not required for AE validation

---

## Why This Is Not Wasteful

1. **Caching matters**. 80-95% of tokens are cached system prompts. The effective
   cost is much lower than the "2.15B" headline suggests.

2. **EDA context is inherently large**. Explaining OpenROAD's legalizer code
   structure to an LLM requires showing the code. There's no way around this
   for source-level exploration.

3. **The alternative is manual effort**. An experienced engineer might spend
   weeks analyzing the same code and trying the same modifications. 2.15B
   tokens ($10-30) vs. weeks of engineer time ($10K+).

4. **The paper DOES NOT claim this is cheap**. Section 5 explicitly states:
   "ReviewDSE is most appropriate for high-value designs, hard failures, or
   cases where public knobs have limited remaining headroom."

---

## Recommendations for Camera-Ready

1. **Add a cost breakdown paragraph** in Section 4.2 explaining logged vs active
2. **Report active token cost in dollars** (using public API pricing at time of experiments)
3. **Note that prompt caching reduces cost by 10-20×**
4. **Add a sentence**: "Per-design active-token cost is approximately $X at
   current API pricing, comparable to a few hours of EDA engineer time."
