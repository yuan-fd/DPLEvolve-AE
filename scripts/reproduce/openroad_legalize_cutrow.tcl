proc env_or_default {name default_value} {
  if {[info exists ::env($name)] && $::env($name) ne ""} {
    return $::env($name)
  }
  return $default_value
}

proc json_escape {value} {
  return [string map [list "\\" "\\\\" "\"" "\\\"" "\n" "\\n" "\r" "\\r" "\t" "\\t"] $value]
}

proc json_value {value} {
  if {[string is entier -strict $value] || [string is double -strict $value]} {
    return $value
  }
  if {$value in {true false null}} {
    return $value
  }
  return "\"[json_escape $value]\""
}

proc write_json_dict {path payload} {
  file mkdir [file dirname $path]
  set fh [open $path w]
  puts $fh "{"
  set keys [dict keys $payload]
  set last_idx [expr {[llength $keys] - 1}]
  set idx 0
  foreach key $keys {
    puts -nonewline $fh "  \"[json_escape $key]\": [json_value [dict get $payload $key]]"
    if {$idx < $last_idx} {
      puts $fh ","
    } else {
      puts $fh ""
    }
    incr idx
  }
  puts $fh "}"
  close $fh
}

proc compute_bbox_hpwl_proxy {} {
  set db [ord::get_db]
  set chip [$db getChip]
  if {$chip == "NULL"} {
    return 0
  }
  set block [$chip getBlock]
  set total 0
  foreach net [$block getNets] {
    set x_min 1e30
    set y_min 1e30
    set x_max -1e30
    set y_max -1e30
    foreach iterm [$net getITerms] {
      set inst [$iterm getInst]
      if {![$inst isPlaced]} {
        continue
      }
      set bbox [$inst getBBox]
      set x_min [expr {min($x_min, [$bbox xMin])}]
      set y_min [expr {min($y_min, [$bbox yMin])}]
      set x_max [expr {max($x_max, [$bbox xMax])}]
      set y_max [expr {max($y_max, [$bbox yMax])}]
    }
    foreach bterm [$net getBTerms] {
      foreach bpin [$bterm getBPins] {
        set bbox [$bpin getBBox]
        set x_min [expr {min($x_min, [$bbox xMin])}]
        set y_min [expr {min($y_min, [$bbox yMin])}]
        set x_max [expr {max($x_max, [$bbox xMax])}]
        set y_max [expr {max($y_max, [$bbox yMax])}]
      }
    }
    if {$x_min != 1e30 && $y_min != 1e30 && $x_max != -1e30 && $y_max != -1e30} {
      set total [expr {$total + $x_max - $x_min + $y_max - $y_min}]
    }
  }
  return $total
}

proc dump_instance_snapshot {path} {
  file mkdir [file dirname $path]
  set fh [open $path w]
  puts $fh "inst\tmaster\tx\ty\torient\twidth\theight\tstatus\tis_placed\tis_fixed"
  set block [ord::get_db_block]
  foreach inst [$block getInsts] {
    set bbox [$inst getBBox]
    set master [$inst getMaster]
    set status [$inst getPlacementStatus]
    set is_fixed [expr {$status in {LOCKED FIRM FIXED} ? 1 : 0}]
    puts $fh "[$inst getName]\t[$master getName]\t[$bbox xMin]\t[$bbox yMin]\t[$inst getOrient]\t[$bbox getDX]\t[$bbox getDY]\t$status\t[expr {[$inst isPlaced] ? 1 : 0}]\t$is_fixed"
  }
  close $fh
}

proc row_stats {} {
  set block [ord::get_db_block]
  set rows [$block getRows]
  set total_sites 0
  set min_sites 1000000000
  set max_sites 0
  set small_50 0
  foreach row $rows {
    set sites [$row getSiteCount]
    incr total_sites $sites
    if {$sites < $min_sites} {
      set min_sites $sites
    }
    if {$sites > $max_sites} {
      set max_sites $sites
    }
    if {$sites <= 50} {
      incr small_50
    }
  }
  if {[llength $rows] == 0} {
    set min_sites 0
  }
  return [dict create row_count [llength $rows] total_sites $total_sites min_sites $min_sites max_sites $max_sites rows_le50 $small_50]
}

set flow_home [file normalize [env_or_default FLOW_HOME "."]]
set platform_dir [file normalize [env_or_default PLATFORM_DIR [file join $flow_home platforms nangate45]]]
set input_def [file normalize [env_or_default CUTROW_DEF ""]]
set input_verilog [file normalize [env_or_default CUTROW_VERILOG ""]]
set input_sdc [file normalize [env_or_default INPUT_SDC ""]]
set output_dir [file normalize [env_or_default EVAL_DIR "cut_rows/results/openroad_eval/default"]]
set line [env_or_default LEGALIZER_LINE openroad_dpl_flow]
set design_name [env_or_default DESIGN_NAME ""]
set design_nickname [env_or_default DESIGN_NICKNAME $design_name]
set platform [env_or_default PLATFORM nangate45]
set case_id [env_or_default CUTROW_CASE_ID ""]
set pattern_id [env_or_default CUTROW_PATTERN_ID ""]
set tech_lef [file normalize [env_or_default TECH_LEF [file join $platform_dir lef NangateOpenCellLibrary.tech.lef]]]
set sc_lef [file normalize [env_or_default SC_LEF [file join $platform_dir lef NangateOpenCellLibrary.macro.mod.lef]]]
set additional_lefs [env_or_default ADDITIONAL_LEFS ""]
set lib_files [env_or_default LIB_FILES [file normalize [file join $platform_dir lib NangateOpenCellLibrary_typical.lib]]]
set pdn_tcl [file normalize [env_or_default PDN_TCL [file join $platform_dir grid_strategy-M1-M4-M7.tcl]]]

proc connect_global_power_nets {} {
  add_global_connection -net {VDD} -inst_pattern {.*} -pin_pattern {^VDD$} -power
  add_global_connection -net {VDD} -inst_pattern {.*} -pin_pattern {^VDDPE$}
  add_global_connection -net {VDD} -inst_pattern {.*} -pin_pattern {^VDDCE$}
  add_global_connection -net {VSS} -inst_pattern {.*} -pin_pattern {^VSS$} -ground
  add_global_connection -net {VSS} -inst_pattern {.*} -pin_pattern {^VSSE$}
  global_connect
}
set before_snapshot [file normalize [env_or_default BEFORE_SNAPSHOT [file join $output_dir before.tsv]]]
set after_snapshot [file normalize [env_or_default AFTER_SNAPSHOT [file join $output_dir after.tsv]]]
set output_odb [file normalize [env_or_default OUTPUT_ODB [file join $output_dir legalized.odb]]]
set output_def [file normalize [env_or_default OUTPUT_DEF [file join $output_dir legalized.def]]]
set summary_json [file normalize [env_or_default LEGALIZE_SUMMARY [file join $output_dir legalize_summary.json]]]
set metrics_json [file normalize [env_or_default METRICS_JSON [file join $output_dir metrics.json]]]
set check_report [file normalize [env_or_default CHECK_REPORT [file join $output_dir check_placement_report.json]]]
set dp_report [file normalize [env_or_default DP_REPORT [file join $output_dir detailed_placement_report.json]]]

foreach required [list $input_def $input_verilog $input_sdc $tech_lef $sc_lef] {
  if {![file exists $required]} {
    error "Missing required input: $required"
  }
}

file mkdir $output_dir

set ::env(FLOW_HOME) $flow_home
set ::env(SCRIPTS_DIR) [file join $flow_home scripts]
set ::env(PLATFORM_DIR) $platform_dir
set ::env(RESULTS_DIR) $output_dir
set ::env(REPORTS_DIR) $output_dir
set ::env(LOG_DIR) $output_dir
set ::env(DESIGN_NAME) $design_name
set ::env(DESIGN_NICKNAME) $design_nickname
set ::env(PLATFORM) $platform
set ::env(TECH_LEF) $tech_lef
set ::env(SC_LEF) $sc_lef
set ::env(ADDITIONAL_LEFS) $additional_lefs
set ::env(LIB_FILES) $lib_files
set ::env(PDN_TCL) $pdn_tcl
set ::env(OPENROAD_HIERARCHICAL) [env_or_default OPENROAD_HIERARCHICAL 0]

source [file join $::env(SCRIPTS_DIR) util.tcl]
source [file join $::env(SCRIPTS_DIR) read_liberty.tcl]

log_cmd read_lef $tech_lef
log_cmd read_lef $sc_lef
foreach lef $additional_lefs {
  if {$lef ne ""} {
    log_cmd read_lef $lef
  }
}
log_cmd read_verilog $input_verilog
log_cmd link_design $design_name
connect_global_power_nets
log_cmd read_def -floorplan_initialize $input_def
log_cmd read_sdc $input_sdc
if {[file exists [file join $platform_dir derate.tcl]]} {
  log_cmd source [file join $platform_dir derate.tcl]
}
if {[file exists [file join $platform_dir setRC.tcl]]} {
  log_cmd source [file join $platform_dir setRC.tcl]
}

set dbu_per_micron [[ord::get_db_tech] getDbUnitsPerMicron]
set hpwl_before_proxy [compute_bbox_hpwl_proxy]
set before_rows [row_stats]
dump_instance_snapshot $before_snapshot

set start_ms [clock milliseconds]
set command ""
set command_rc 0
set command_error ""
set status "error"
set error_message ""
if {$line eq "openroad_dpl_flow"} {
  set command "detailed_placement"
  set command_rc [catch {log_cmd detailed_placement -report_file_name $dp_report} command_error]
} elseif {$line eq "openroad_dpl_negotiation"} {
  set command "detailed_placement -use_negotiation"
  set command_rc [catch {log_cmd detailed_placement -use_negotiation -report_file_name $dp_report} command_error]
} elseif {$line eq "evolve_default"} {
  set command "detailed_placement_evolve"
  set command_rc [catch {log_cmd detailed_placement_evolve -report_file_name $dp_report} command_error]
} else {
  error "Unsupported LEGALIZER_LINE '$line'."
}
set end_ms [clock milliseconds]
set runtime_sec [expr {($end_ms - $start_ms) / 1000.0}]

set check_status [catch {check_placement -verbose -report_file_name $check_report} check_result]
if {$check_status == 0} {
  set status "ok"
  set error_message ""
} else {
  set error_message $check_result
}
set hpwl_after_proxy [compute_bbox_hpwl_proxy]
set after_rows [row_stats]
dump_instance_snapshot $after_snapshot
catch {log_cmd write_db $output_odb}
catch {log_cmd write_def $output_def}

set summary [dict create]
foreach {key value} [list \
  status $status \
  case_id $case_id \
  pattern_id $pattern_id \
  legalizer_line $line \
  command $command \
  command_rc $command_rc \
  command_error $command_error \
  error_message $error_message \
  status_source check_placement \
  input_def $input_def \
  input_verilog $input_verilog \
  input_sdc $input_sdc \
  output_odb $output_odb \
  output_def $output_def \
  before_snapshot $before_snapshot \
  after_snapshot $after_snapshot \
  detailed_placement_report $dp_report \
  check_report $check_report \
  check_status $check_status \
  check_result $check_result \
  dbu_per_micron $dbu_per_micron \
  hpwl_before_dbu $hpwl_before_proxy \
  hpwl_after_dbu $hpwl_after_proxy \
  hpwl_delta_dbu [expr {$hpwl_after_proxy - $hpwl_before_proxy}] \
  runtime_seconds $runtime_sec] {
  dict set summary $key $value
}
foreach {key value} $before_rows {
  dict set summary "before_$key" $value
}
foreach {key value} $after_rows {
  dict set summary "after_$key" $value
}
write_json_dict $summary_json $summary

puts "OpenROAD cut-row legalization complete:"
puts "  line: $line"
puts "  status: $status"
puts "  summary: $summary_json"

if {$status ne "ok"} {
  error $error_message
}
