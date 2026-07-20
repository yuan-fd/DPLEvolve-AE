namespace eval dpl_evolve_baseline {
  proc parse_options { argv defaults } {
    set options $defaults
    if { [info exists ::dpl_evolve_options] } {
      foreach {name value} $::dpl_evolve_options {
        if { ![dict exists $options $name] } {
          error "Unknown option '$name' in driver options."
        }
        dict set options $name $value
      }
    }
    set argc [llength $argv]
    set idx 0
    while { $idx < $argc } {
      set key [lindex $argv $idx]
      if { ![string match --* $key] } {
        error "Unexpected positional argument '$key'. Expected --name value pairs."
      }
      incr idx
      if { $idx >= $argc } {
        error "Missing value for option '$key'."
      }
      set name [string range $key 2 end]
      if { ![dict exists $options $name] } {
        error "Unknown option '$key'."
      }
      dict set options $name [lindex $argv $idx]
      incr idx
    }
    return $options
  }

  proc option_value { options name } {
    if { ![dict exists $options $name] } {
      error "Missing option '$name'."
    }
    return [dict get $options $name]
  }

  proc bool_value { value } {
    return [expr {$value in {1 true TRUE yes YES on ON}}]
  }

  proc flow_home { } {
    if { [info exists ::env(FLOW_HOME)] && $::env(FLOW_HOME) ne "" } {
      return [file normalize $::env(FLOW_HOME)]
    }
    return [file normalize [pwd]]
  }

  proc resolve_flow_path { path } {
    if { $path eq "" } {
      return ""
    }
    if { [file pathtype $path] eq "absolute" } {
      return [file normalize $path]
    }
    return [file normalize [file join [flow_home] $path]]
  }

  proc normalize_flow_relative { path } {
    if { $path eq "" } {
      return ""
    }
    set abs_path [resolve_flow_path $path]
    set flow_prefix "[flow_home]/"
    if { [string first $flow_prefix $abs_path] == 0 } {
      return [string range $abs_path [string length $flow_prefix] end]
    }
    return $path
  }

  proc ensure_parent_dir { path } {
    file mkdir [file dirname [resolve_flow_path $path]]
  }

  proc json_escape { value } {
    return [string map [list "\\" "\\\\" "\"" "\\\"" "\n" "\\n" "\r" "\\r" "\t" "\\t"] $value]
  }

  proc json_string { value } {
    return "\"[json_escape $value]\""
  }

  proc get_dbu_per_micron { } {
    return [[ord::get_db_tech] getDbUnitsPerMicron]
  }

  proc compute_hpwl_proxy { } {
    set db [ord::get_db]
    set chip [$db getChip]
    if { $chip == "NULL" } {
      return 0
    }

    set block [$chip getBlock]
    set total_hpwl 0

    foreach net [$block getNets] {
      set x_min 1e30
      set y_min 1e30
      set x_max -1e30
      set y_max -1e30

      foreach i_term [$net getITerms] {
        set inst [$i_term getInst]
        if { ![$inst isPlaced] } {
          continue
        }
        set inst_box [$inst getBBox]
        set x_min [expr {min($x_min, [$inst_box xMin])}]
        set y_min [expr {min($y_min, [$inst_box yMin])}]
        set x_max [expr {max($x_max, [$inst_box xMax])}]
        set y_max [expr {max($y_max, [$inst_box yMax])}]
      }

      foreach b_term [$net getBTerms] {
        foreach b_pin [$b_term getBPins] {
          set pin_box [$b_pin getBBox]
          set x_min [expr {min($x_min, [$pin_box xMin])}]
          set y_min [expr {min($y_min, [$pin_box yMin])}]
          set x_max [expr {max($x_max, [$pin_box xMax])}]
          set y_max [expr {max($y_max, [$pin_box yMax])}]
        }
      }

      if { $x_min != 1e30 && $y_min != 1e30 && $x_max != -1e30 && $y_max != -1e30 } {
        set total_hpwl [expr {$total_hpwl + $x_max - $x_min + $y_max - $y_min}]
      }
    }

    return $total_hpwl
  }

  proc snapshot_stats { } {
    set block [ord::get_db_block]
    set inst_count 0
    set placed_count 0
    set fixed_count 0

    foreach inst [$block getInsts] {
      incr inst_count
      if { [$inst isPlaced] } {
        incr placed_count
      }
      set status [$inst getPlacementStatus]
      if { $status in {LOCKED FIRM FIXED} } {
        incr fixed_count
      }
    }

    return [dict create \
      instance_count $inst_count \
      placed_instance_count $placed_count \
      fixed_instance_count $fixed_count]
  }

  proc dump_instance_snapshot { path } {
    ensure_parent_dir $path
    set fh [open [resolve_flow_path $path] w]
    puts $fh "inst\tmaster\tx\ty\torient\twidth\theight\tstatus\tis_placed\tis_fixed"

    set block [ord::get_db_block]
    foreach inst [$block getInsts] {
      set bbox [$inst getBBox]
      set master [$inst getMaster]
      set status [$inst getPlacementStatus]
      set is_fixed [expr {$status in {LOCKED FIRM FIXED} ? 1 : 0}]
      puts $fh \
        "[$inst getName]\t[$master getName]\t[$bbox xMin]\t[$bbox yMin]\t[$inst getOrient]\t[$bbox getDX]\t[$bbox getDY]\t$status\t[expr {[$inst isPlaced] ? 1 : 0}]\t$is_fixed"
    }
    close $fh
  }

  proc load_snapshot_rows { path } {
    set rows {}
    set fh [open [resolve_flow_path $path] r]
    gets $fh
    while { [gets $fh line] >= 0 } {
      if { $line eq "" } {
        continue
      }
      lassign [split $line "\t"] name master x y orient width height status is_placed is_fixed
      dict set rows $name [dict create \
        name $name \
        master $master \
        x $x \
        y $y \
        orient $orient \
        width $width \
        height $height \
        status $status \
        is_placed $is_placed \
        is_fixed $is_fixed]
    }
    close $fh
    return $rows
  }

  proc apply_snapshot_rows { path {import_orient 0} } {
    set rows [load_snapshot_rows $path]
    set block [ord::get_db_block]
    foreach inst [$block getInsts] {
      set inst_name [$inst getName]
      if { ![dict exists $rows $inst_name] } {
        continue
      }
      set row [dict get $rows $inst_name]
      if { [dict get $row is_fixed] } {
        continue
      }
      $inst setLocation [dict get $row x] [dict get $row y]
      if { $import_orient } {
        $inst setOrient [dict get $row orient]
      }
      $inst setPlacementStatus PLACED
    }
  }

  proc write_summary_json { path key_values } {
    ensure_parent_dir $path
    set fh [open [resolve_flow_path $path] w]
    puts $fh "{"
    set keys [dict keys $key_values]
    set last_idx [expr {[llength $keys] - 1}]
    set idx 0
    foreach key $keys {
      set value [dict get $key_values $key]
      if { [string is entier -strict $value] || [string is double -strict $value] } {
        set rendered $value
      } elseif { $value in {true false null} } {
        set rendered $value
      } else {
        set rendered [json_string $value]
      }
      puts -nonewline $fh "  [json_string $key]: $rendered"
      if { $idx < $last_idx } {
        puts $fh ","
      } else {
        puts $fh ""
      }
      incr idx
    }
    puts $fh "}"
    close $fh
  }
}
