package winapi_global;

use strict;
 
sub check {
    my $options = shift;
    my $output = shift;
    my $winapi = shift;
    my $nativeapi = shift;
    my $type_found = shift;

    my $winver = $winapi->name;

    if($options->argument) {
	foreach my $type ($winapi->all_declared_types) {
	    if(!$$type_found{$type} && !$winapi->is_limited_type($type) && $type ne "CONTEXT86 *") {
		$output->write("*.c: $winver: ");
		$output->write("type ($type) not used\n");
	    }
	}
    }

    if($options->argument && $options->argument_forbidden) {
	my $not_used = $winapi->types_not_used;

	foreach my $module (sort(keys(%$not_used))) {
	    foreach my $type (sort(keys(%{$$not_used{$module}}))) {
		$output->write("*.c: $module: type ($type) not used\n");
	    }
	}

	my $types_used = $winapi->types_unlimited_used_in_modules;

	foreach my $type (sort(keys(%$types_used))) {
	    $output->write("*.c: type ($type) only used in module[s] (");
	    my $count = 0;
	    foreach my $module (sort(keys(%{$$types_used{$type}}))) {
		if($count++) { $output->write(", "); }
		$output->write("$module");
	    }
	    $output->write(")\n");
	}
    }
}

1;

