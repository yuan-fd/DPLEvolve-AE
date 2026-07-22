proc env_or_default {name default_value} {
  if {[info exists ::env($name)] && $::env($name) ne ""} {
    return $::env($name)
  }
  return $default_value
}

proc json_escape {value} {
  return [string map [list "\\" "\\\\" "\"" "\\\"" "\n" "\\n" "\r" "\\r" "\t" "\\t"] $value]
}

proc json_value {value numeric} {
  if {$numeric} { return $value }
  return "\"[json_escape $value]\""
}

proc write_metrics {path payload numeric_keys} {
  file mkdir [file dirname $path]
  set fh [open $path w]
  puts $fh "{"
  set keys [dict keys $payload]
  set last [expr {[llength $keys] - 1}]
  set index 0
  foreach key $keys {
    set numeric [expr {[lsearch -exact $numeric_keys $key] >= 0}]
    puts -nonewline $fh "  \"[json_escape $key]\": [json_value [dict get $payload $key] $numeric]"
    if {$index < $last} { puts $fh "," } else { puts $fh "" }
    incr index
  }
  puts $fh "}"
  close $fh
}

proc bbox_hpwl {} {
  set block [ord::get_db_block]
  set total 0
  foreach net [$block getNets] {
    set x_min 1e30
    set y_min 1e30
    set x_max -1e30
    set y_max -1e30
    foreach iterm [$net getITerms] {
      set inst [$iterm getInst]
      if {![$inst isPlaced]} { continue }
      set box [$inst getBBox]
      set x_min [expr {min($x_min, [$box xMin])}]
      set y_min [expr {min($y_min, [$box yMin])}]
      set x_max [expr {max($x_max, [$box xMax])}]
      set y_max [expr {max($y_max, [$box yMax])}]
    }
    foreach bterm [$net getBTerms] {
      foreach bpin [$bterm getBPins] {
        set box [$bpin getBBox]
        set x_min [expr {min($x_min, [$box xMin])}]
        set y_min [expr {min($y_min, [$box yMin])}]
        set x_max [expr {max($x_max, [$box xMax])}]
        set y_max [expr {max($y_max, [$box yMax])}]
      }
    }
    if {$x_min != 1e30} {
      set total [expr {$total + $x_max - $x_min + $y_max - $y_min}]
    }
  }
  return $total
}

set flow_home [file normalize [env_or_default FLOW_HOME "."]]
set platform_dir [file normalize [env_or_default PLATFORM_DIR [file join $flow_home platforms nangate45]]]
set input_def [file normalize [env_or_default CUTROW_DEF ""]]
set input_verilog [file normalize [env_or_default CUTROW_VERILOG ""]]
set input_sdc [file normalize [env_or_default INPUT_SDC ""]]
set output_dir [file normalize [env_or_default EVAL_DIR "cutrow-eval"]]
set metrics_json [file normalize [env_or_default METRICS_JSON [file join $output_dir metrics.json]]]
set line [env_or_default LEGALIZER_LINE diamond]
set design_name [env_or_default DESIGN_NAME ""]
set design_nickname [env_or_default DESIGN_NICKNAME $design_name]
set platform [env_or_default PLATFORM nangate45]
set case_id [env_or_default CUTROW_CASE_ID ""]
set pattern_id [env_or_default CUTROW_PATTERN_ID ""]
set tech_lef [file normalize [env_or_default TECH_LEF [file join $platform_dir lef NangateOpenCellLibrary.tech.lef]]]
set sc_lef [file normalize [env_or_default SC_LEF [file join $platform_dir lef NangateOpenCellLibrary.macro.mod.lef]]]
set additional_lefs [env_or_default ADDITIONAL_LEFS ""]
set lib_files [env_or_default LIB_FILES [file join $platform_dir lib NangateOpenCellLibrary_typical.lib]]
set pdn_tcl [file normalize [env_or_default PDN_TCL [file join $platform_dir grid_strategy-M1-M4-M7.tcl]]]

foreach required [list $input_def $input_verilog $input_sdc $tech_lef $sc_lef] {
  if {![file exists $required]} { error "Missing required input: $required" }
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
  if {$lef ne ""} { log_cmd read_lef $lef }
}
log_cmd read_verilog $input_verilog
log_cmd link_design $design_name
add_global_connection -net {VDD} -inst_pattern {.*} -pin_pattern {^VDD$} -power
add_global_connection -net {VSS} -inst_pattern {.*} -pin_pattern {^VSS$} -ground
global_connect
log_cmd read_def -floorplan_initialize $input_def
log_cmd read_sdc $input_sdc
if {[file exists [file join $platform_dir derate.tcl]]} { log_cmd source [file join $platform_dir derate.tcl] }
if {[file exists [file join $platform_dir setRC.tcl]]} { log_cmd source [file join $platform_dir setRC.tcl] }

set dbu_per_micron [[ord::get_db_tech] getDbUnitsPerMicron]
set hpwl_before [bbox_hpwl]
set start_ms [clock milliseconds]
set command_rc 0
set command_error ""
if {$line eq "diamond"} {
  set command_rc [catch {log_cmd detailed_placement} command_error]
} elseif {$line eq "negotiation"} {
  set command_rc [catch {log_cmd detailed_placement -use_negotiation} command_error]
} elseif {$line eq "reviewdse"} {
  set command_rc [catch {log_cmd detailed_placement_evolve} command_error]
} else {
  error "Unsupported LEGALIZER_LINE '$line'"
}
set runtime_seconds [expr {([clock milliseconds] - $start_ms) / 1000.0}]
set check_status [catch {check_placement -verbose} check_result]
set hpwl_after [bbox_hpwl]
set status [expr {$command_rc == 0 && $check_status == 0 ? "ok" : "fail"}]

set payload [dict create \
  status $status \
  case_id $case_id \
  pattern_id $pattern_id \
  legalizer_line $line \
  command_rc $command_rc \
  command_error $command_error \
  check_status $check_status \
  check_result $check_result \
  runtime_seconds $runtime_seconds \
  dbu_per_micron $dbu_per_micron \
  hpwl_before_dbu $hpwl_before \
  hpwl_after_dbu $hpwl_after]
write_metrics $metrics_json $payload {command_rc check_status runtime_seconds dbu_per_micron hpwl_before_dbu hpwl_after_dbu}
puts "Cut-row replay: case=$case_id pattern=$pattern_id line=$line status=$status metrics=$metrics_json"
if {$status ne "ok"} { error "cut-row replay failed strict placement: $command_error $check_result" }
