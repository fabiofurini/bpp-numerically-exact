#!/usr/bin/env bash
set -u

# Compare historical and refactored BPP commands on every ANI-201/ANI-402
# instance. These are the repository's 200/400-item families.
ROOT=$(cd "$(dirname "$0")/.." && pwd)
# No public defaults for either of these: point them at your own local
# copies of the ANI-201/ANI-402 instance data and the historical archive's
# build (never published, see legacy/ and .gitignore).
DATA_ROOT=${DATA_ROOT:?"set DATA_ROOT to your local ANI-201/ANI-402 data directory"}
OLD_SOLVER=${OLD_SOLVER:?"set OLD_SOLVER to your local historical-archive bpp-solve-legacy build"}
NEW_SOLVER=${NEW_SOLVER:-"$ROOT/build-cplex-soplex/bpp-solve"}
NEW_MODE=${NEW_MODE:-legacy-root-cg}
NEW_MAX_ITERATIONS=${NEW_MAX_ITERATIONS:-20}
REUSE_OLD_LOGS=${REUSE_OLD_LOGS:-0}
REUSE_NEW_LOGS=${REUSE_NEW_LOGS:-0}
# No public default: point this at your own local copy of the historical
# archive's parameter file (never published, see legacy/ and .gitignore).
OLD_PARAMS=${OLD_PARAMS:?"set OLD_PARAMS to your local historical-archive parameter file"}
TIME_LIMIT=${TIME_LIMIT:-120}
OUTPUT=${OUTPUT:-"$ROOT/tests/results/ani-comparison.csv"}
LOG_DIR=${LOG_DIR:-"$ROOT/tests/results/ani-logs"}
FAMILIES=${FAMILIES:-"ANI201 ANI402"}
MAX_INSTANCES=${MAX_INSTANCES:-0}
processed_instances=0

if [[ ! -x "$OLD_SOLVER" ]]; then
  echo "old solver not executable: $OLD_SOLVER" >&2
  echo "set OLD_SOLVER=/path/to/historical/bpp-solve-legacy (and optionally OLD_BUILD_ROOT)" >&2
  exit 2
fi
if [[ ! -x "$NEW_SOLVER" ]]; then
  echo "new solver not executable: $NEW_SOLVER" >&2
  exit 2
fi
if [[ ! -d "$DATA_ROOT/ANI201" || ! -d "$DATA_ROOT/ANI402" ]]; then
  echo "ANI data root must contain ANI201 and ANI402: $DATA_ROOT" >&2
  exit 2
fi

mkdir -p "$LOG_DIR"
printf 'family,instance,items,old_status,old_ub,old_lp_bound,old_nodes,old_columns,old_time,new_status,new_ub,new_lp_relaxation,new_lb,new_lb_valid,new_safe_bound,new_iterations,new_phase1_iterations,new_phase2_iterations,new_phase2_backend,new_columns,new_generated_columns,new_sr3_cuts,new_sr3_cuts_added,new_populate_columns,new_time,ub_equal,new_log\n' > "$OUTPUT"

extract() {
  local pattern=$1
  local file=$2
  awk -v p="$pattern" '$0 ~ p { value=$NF } END { if (value != "") print value; else print "NA" }' "$file"
}

for family in $FAMILIES; do
  for instance in $(find "$DATA_ROOT/$family" -maxdepth 1 -type f -name '*.txt' | sort); do
    if [[ "$MAX_INSTANCES" -gt 0 && "$processed_instances" -ge "$MAX_INSTANCES" ]]; then
      break 2
    fi
    processed_instances=$((processed_instances + 1))
    name=$(basename "$instance")
    stem=${name%.txt}
    old_log="$LOG_DIR/${stem}.old.log"
    new_log="$LOG_DIR/${stem}.new.log"

    if [[ "$REUSE_OLD_LOGS" == "1" && -s "$old_log" ]]; then
      old_rc=0
    else
      timeout "${TIME_LIMIT}s" "$OLD_SOLVER" "$instance" "$OLD_PARAMS" 0 0 0 0 > "$old_log" 2>&1
      old_rc=$?
    fi
    if [[ "$REUSE_NEW_LOGS" == "1" && -s "$new_log" ]]; then
      new_rc=0
      new_time="NA"
    else
      new_start=$(date +%s.%N)
      if [[ "$NEW_MODE" == "root-cg" ]]; then
        timeout "${TIME_LIMIT}s" "$NEW_SOLVER" "$instance" --root-cg "$NEW_MAX_ITERATIONS" > "$new_log" 2>&1
      elif [[ "$NEW_MODE" == "legacy-root-cg" ]]; then
        timeout "${TIME_LIMIT}s" "$NEW_SOLVER" "$instance" --legacy-root-cg "$NEW_MAX_ITERATIONS" > "$new_log" 2>&1
      else
        timeout "${TIME_LIMIT}s" "$NEW_SOLVER" "$instance" > "$new_log" 2>&1
      fi
      new_rc=$?
      new_end=$(date +%s.%N)
      new_time=$(awk -v start="$new_start" -v end="$new_end" 'BEGIN { printf "%.6f", end-start }')
    fi

    old_status=$(extract '^STATUS[[:space:]]' "$old_log")
    old_ub=$(awk '/^INCUMBENT[[:space:]]/ { value=$NF; sub("AT=", "", value); found=1 } END { if (found) print value; else print "NA" }' "$old_log")
    old_lb=$(awk '/^BP_lp[[:space:]]/ { value=$2; found=1 } END { if (found) print value; else print "NA" }' "$old_log")
    old_nodes=$(awk '/^(Nodes|NODEs)[[:space:]]/ { value=$2; found=1 } END { if (found) print value; else print "NA" }' "$old_log")
    old_columns=$(awk '/^Cols \(generated\)/ { value=$5; sub("total:", "", value); found=1 } END { if (found) print value; else { for (i=1; i<=NF; ++i) if ($i == "cols") { value=$(i+1); found=1 } if (found) print value; else print "NA" } }' "$old_log")
    old_time=$(awk '/^Total[[:space:]]+Time[[:space:]]/ { value=$NF; found=1 } END { if (found) print value; else print "NA" }' "$old_log")
    new_status=$(awk '/^status[[:space:]]/ { print $2; found=1 } END { if (!found) print "NA" }' "$new_log")
    new_ub=$(awk '/^incumbent[[:space:]]/ { print $2; found=1 } END { if (!found) print "NA" }' "$new_log")
    new_lp_relaxation=$(awk '/^lp_bound[[:space:]]/ { print $2; found=1 } END { if (!found) print "NA" }' "$new_log")
    new_safe_bound=$(awk '/^safe_bound[[:space:]]/ { print $2; found=1 } END { if (!found) print "NA" }' "$new_log")
    new_safe_duals=$(awk '/^safe_duals_feasible[[:space:]]/ { print $2; found=1 } END { if (!found) print "0" }' "$new_log")
    new_lb_valid=$( [[ "$new_status" == "converged" && "$new_safe_duals" == "1" && "$new_safe_bound" != "unavailable" ]] && echo 1 || echo 0 )
    new_lb=$( [[ "$new_lb_valid" == "1" ]] && echo "$new_safe_bound" || echo "NA" )
    new_iterations=$(awk '/^iterations[[:space:]]/ { print $2; found=1 } END { if (!found) print "NA" }' "$new_log")
    new_phase1_iterations=$(awk '/^phase1_iterations[[:space:]]/ { print $2; found=1 } END { if (!found) print "NA" }' "$new_log")
    new_phase2_iterations=$(awk '/^phase2_iterations[[:space:]]/ { print $2; found=1 } END { if (!found) print "NA" }' "$new_log")
    new_phase2_backend=$(awk '/^phase2_backend[[:space:]]/ { print $2; found=1 } END { if (!found) print "NA" }' "$new_log")
    new_columns=$(awk '/^columns[[:space:]]/ { print $2; found=1 } END { if (!found) print "NA" }' "$new_log")
    new_generated_columns=$(awk '/^generated_columns[[:space:]]/ { print $2; found=1 } END { if (!found) print "NA" }' "$new_log")
    new_sr3_cuts=$(awk '/^sr3_cuts[[:space:]]/ { print $2; found=1 } END { if (!found) print "NA" }' "$new_log")
    new_sr3_cuts_added=$(awk '/^sr3_cuts_added[[:space:]]/ { print $2; found=1 } END { if (!found) print "NA" }' "$new_log")
    new_populate_columns=$(awk '/^populate_columns[[:space:]]/ { print $2; found=1 } END { if (!found) print "NA" }' "$new_log")
    ub_equal=$( [[ "$old_ub" == "$new_ub" ]] && echo 1 || echo 0 )
    printf '%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n' \
      "$family" "$name" "${family#ANI}" "${old_rc}:${old_status}" "$old_ub" "$old_lb" "$old_nodes" "$old_columns" "$old_time" \
      "${new_rc}:${new_status}" "$new_ub" "$new_lp_relaxation" "$new_lb" "$new_lb_valid" "$new_safe_bound" \
      "$new_iterations" "$new_phase1_iterations" "$new_phase2_iterations" "$new_phase2_backend" "$new_columns" "$new_generated_columns" \
      "$new_sr3_cuts" "$new_sr3_cuts_added" "$new_populate_columns" "$new_time" \
      "$ub_equal" "$new_log" >> "$OUTPUT"
  done
done

echo "Wrote $OUTPUT"
