# DPLEvolve reviewer console

The Web Demo is an optional browser frontend to the repository's fixed
environment and fresh-experiment Make targets. It does not contain separate
verification logic or accept arbitrary shell commands.

## Start locally

From the repository root:

```bash
bash web-demo/start.sh
```

Open `http://127.0.0.1:8080`.

## Open a remote server safely

Keep the service bound to loopback. On the reviewer's laptop:

```bash
ssh -N -L 8080:127.0.0.1:8080 USER@SERVER
```

Then open `http://127.0.0.1:8080` on the laptop. Do not expose this service to
the public Internet because it can launch builds and experiments.

## Recommended reviewer simulation

1. Run **Reviewer Environment Doctor**.
2. Run **Prepare one target**. This bootstraps/builds the pinned tools, prepares
   AES Nangate45, and validates its protected evaluator trajectory.
3. Run **Run AES result**. This executes the same-case default and rebuilds one
   HPWL-selected ReviewDSE source program.
4. Inspect the new `metrics.json` and replay `results.tsv` in the live log.
5. Run **Download Table 6 data**, then the one-row Ariane center-8 ReviewDSE
   task before attempting all 27 Table 6 jobs.
6. Export the session JSON for the evaluation record.

Only after the bounded path succeeds should a reviewer start complete Table 4
(3,600 BO placements), all 27 Table 6 jobs, or the available-results aggregate.

## Cost and data boundaries

- Default, BO, selected-source replay, figures, Table 6, and the Ariane
  diagnostic require no LLM.
- Full ReviewDSE search is not a one-click browser task. The terminal command
  requires explicit `ACKNOWLEDGE_LLM_COST=yes`.
- Table 5 is the only blocked reported table because its SWERV config and six
  source trees are missing.
- Figure 4 retained mode reports 96 observed points and three gaps.
- Missing paper-time hashes do not block numerical reproduction; fresh legal
  metrics and tolerance checks are the scientific criterion.

## Backend safety

The backend accepts predefined task names only. Commands are serialized,
streamed over WebSocket, and recorded in in-memory session history. SSH mode
runs those same fixed commands on the selected remote repository path.

Credentials remain in server memory while tasks exist and are not included in
status or exported session data.
