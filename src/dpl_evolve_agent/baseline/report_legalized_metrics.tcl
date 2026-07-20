utl::set_metrics_stage "detailedplace__{}"

source $::env(SCRIPTS_DIR)/load.tcl
source [file join [file dirname [info script]] dpl_evolve_helpers.tcl]

set options [dpl_evolve_baseline::parse_options $argv [dict create \
  input-odb "legalized.odb" \
  input-sdc "../../2_floorplan.sdc" \
  check-cmd "check_placement" \
  check-report [file join $::env(REPORTS_DIR) check_placement_report.json] \
  summary-json [file join $::env(REPORTS_DIR) post_metrics_summary.json] \
  metrics-json [file join $::env(LOG_DIR) 3_5_place_dp.json] \
  track "" \
  legalizer ""]]

set input_odb [dpl_evolve_baseline::option_value $options input-odb]
set input_sdc [dpl_evolve_baseline::option_value $options input-sdc]
set check_cmd [dpl_evolve_baseline::option_value $options check-cmd]
set check_report [dpl_evolve_baseline::option_value $options check-report]
set summary_json [dpl_evolve_baseline::option_value $options summary-json]
set metrics_json [dpl_evolve_baseline::option_value $options metrics-json]
set track [dpl_evolve_baseline::option_value $options track]
set legalizer [dpl_evolve_baseline::option_value $options legalizer]
set metrics_rpt [file join $::env(REPORTS_DIR) 3_detailed_place.rpt]

load_design $input_odb $input_sdc
source $::env(PLATFORM_DIR)/setRC.tcl

set placement_violations [log_cmd $check_cmd -report_file_name [dpl_evolve_baseline::resolve_flow_path $check_report]]
set resolved_check_report [dpl_evolve_baseline::resolve_flow_path $check_report]
if { [file exists $resolved_check_report] } {
  set normalized_check_report [dpl_evolve_baseline::normalize_flow_relative $check_report]
  set check_report_status "present"
} else {
  set normalized_check_report ""
  set check_report_status "absent"
}
log_cmd estimate_parasitics -placement
report_metrics 3 "detailed place" true false

set summary [dict create \
  status "ok" \
  input_odb $input_odb \
  input_sdc $input_sdc \
  check_command $check_cmd \
  check_report $normalized_check_report \
  check_report_status $check_report_status \
  placement_violations $placement_violations \
  metrics_json [dpl_evolve_baseline::normalize_flow_relative $metrics_json] \
  metrics_report [dpl_evolve_baseline::normalize_flow_relative $metrics_rpt] \
  dbu_per_micron [dpl_evolve_baseline::get_dbu_per_micron]]

if { $track ne "" } {
  dict set summary track $track
}
if { $legalizer ne "" } {
  dict set summary legalizer_mode $legalizer
}

dpl_evolve_baseline::write_summary_json $summary_json $summary
