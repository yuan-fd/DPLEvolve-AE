utl::set_metrics_stage "dpl_evolve__openroad_dpl_negotiation__{}"

source $::env(SCRIPTS_DIR)/load.tcl
source [file join [file dirname [info script]] dpl_evolve_helpers.tcl]

proc append_optional_env_args {cmd_var env_names} {
  upvar 1 $cmd_var cmd
  foreach env_name $env_names {
    if { [info exists ::env($env_name)] && $::env($env_name) ne "" } {
      foreach arg $::env($env_name) {
        lappend cmd $arg
      }
      return $env_name
    }
  }
  return ""
}

set options [dpl_evolve_baseline::parse_options $argv [dict create \
  input-odb "3_4_place_resized.odb" \
  input-sdc "2_floorplan.sdc" \
  run-dir [file join $::env(RESULTS_DIR) dpl_evolve_baseline openroad_dpl_negotiation] \
  report-dir [file join $::env(REPORTS_DIR) dpl_evolve_baseline openroad_dpl_negotiation] \
  before-snapshot [file join $::env(RESULTS_DIR) dpl_evolve_baseline openroad_dpl_negotiation before.tsv] \
  after-snapshot [file join $::env(RESULTS_DIR) dpl_evolve_baseline openroad_dpl_negotiation after.tsv] \
  output-odb [file join $::env(RESULTS_DIR) dpl_evolve_baseline openroad_dpl_negotiation legalized.odb] \
  summary-json [file join $::env(REPORTS_DIR) dpl_evolve_baseline openroad_dpl_negotiation legalize_summary.json] \
  dp-report [file join $::env(REPORTS_DIR) dpl_evolve_baseline openroad_dpl_negotiation detailed_placement_report.json] \
  track "strict"]]

set input_odb [dpl_evolve_baseline::option_value $options input-odb]
set input_sdc [dpl_evolve_baseline::option_value $options input-sdc]
set run_dir [dpl_evolve_baseline::option_value $options run-dir]
set report_dir [dpl_evolve_baseline::option_value $options report-dir]
set before_snapshot [dpl_evolve_baseline::option_value $options before-snapshot]
set after_snapshot [dpl_evolve_baseline::option_value $options after-snapshot]
set output_odb [dpl_evolve_baseline::option_value $options output-odb]
set summary_json [dpl_evolve_baseline::option_value $options summary-json]
set dp_report [dpl_evolve_baseline::option_value $options dp-report]
set track [dpl_evolve_baseline::option_value $options track]

load_design $input_odb $input_sdc
source_step_tcl PRE DETAIL_PLACE

file mkdir [dpl_evolve_baseline::resolve_flow_path $run_dir]
file mkdir [dpl_evolve_baseline::resolve_flow_path $report_dir]

set dbu_per_micron [dpl_evolve_baseline::get_dbu_per_micron]
set hpwl_before [dpl_evolve_baseline::compute_hpwl_proxy]
dpl_evolve_baseline::dump_instance_snapshot $before_snapshot

log_cmd set_placement_padding -global -left 0 -right 0
set dpl_args [list detailed_placement \
  -use_negotiation \
  -report_file_name [dpl_evolve_baseline::resolve_flow_path $dp_report]]
set detail_arg_env [append_optional_env_args dpl_args \
  {OPENROAD_DPL_DETAIL_ARGS}]
log_cmd {*}$dpl_args
set improve_args [list improve_placement]
set improve_arg_env [append_optional_env_args improve_args \
  {OPENROAD_DPL_IMPROVE_ARGS}]
if { [lsearch -exact $improve_args "-random_seed"] < 0 } {
  lappend improve_args -random_seed 1
}
log_cmd {*}$improve_args
set optimize_args [list optimize_mirroring]
set optimize_arg_env [append_optional_env_args optimize_args \
  {OPENROAD_DPL_OPTIMIZE_ARGS}]
log_cmd {*}$optimize_args

set hpwl_after [dpl_evolve_baseline::compute_hpwl_proxy]
dpl_evolve_baseline::dump_instance_snapshot $after_snapshot
log_cmd write_db [dpl_evolve_baseline::resolve_flow_path $output_odb]

set summary [dict create \
  status "ok" \
  track $track \
  legalizer_mode "openroad_dpl_negotiation" \
  input_odb $input_odb \
  input_sdc $input_sdc \
  output_odb $output_odb \
  before_snapshot $before_snapshot \
  after_snapshot $after_snapshot \
  detailed_placement_report $dp_report \
  dbu_per_micron $dbu_per_micron \
  hpwl_before_dbu $hpwl_before \
  hpwl_after_dbu $hpwl_after \
  hpwl_delta_dbu [expr {$hpwl_after - $hpwl_before}] \
  stage_sequence "detailed_placement -use_negotiation,improve_placement,optimize_mirroring" \
  place_command [join $dpl_args " "] \
  improve_command [join $improve_args " "] \
  optimize_command [join $optimize_args " "] \
  detail_args_env $detail_arg_env \
  improve_args_env $improve_arg_env \
  optimize_args_env $optimize_arg_env \
  padding_command "set_placement_padding"]

foreach {key value} [dpl_evolve_baseline::snapshot_stats] {
  dict set summary $key $value
}

dpl_evolve_baseline::write_summary_json $summary_json $summary
source_step_tcl POST DETAIL_PLACE
