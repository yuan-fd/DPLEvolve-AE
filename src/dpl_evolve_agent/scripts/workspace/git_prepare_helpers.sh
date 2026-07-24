#!/usr/bin/env bash

# Return success only for the exact, clean branch state left when workspace
# preparation was interrupted before any patches were applied.  This permits a
# network-failed bootstrap to resume without making --force the default.
dpl_prepare_branch_is_resumable() {
  local repo_root="$1"
  local target_branch="$2"
  local target_commit="$3"

  [[ "$(git -C "${repo_root}" symbolic-ref --quiet --short HEAD 2>/dev/null || true)" == "${target_branch}" ]] \
    && [[ "$(git -C "${repo_root}" rev-parse HEAD 2>/dev/null || true)" == "${target_commit}" ]] \
    && [[ -z "$(git -C "${repo_root}" status --short)" ]]
}
