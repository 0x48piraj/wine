package winapi;

use strict;

sub new {
    my $proto = shift;
    my $class = ref($proto) || $proto;
    my $self  = {};
    bless ($self, $class);

    my $options = \${$self->{OPTIONS}};
    my $output = \${$self->{OUTPUT}};
    my $name = \${$self->{NAME}};

    $$options = shift;
    $$output = shift;
    $$name = shift;
    my $path = shift;

    my @files = map {
	s/^.\/(.*)$/$1/;
	$_; 
    } split(/\n/, `find $path -name \\*.api`);
  
    foreach my $file (@files) {
	my $module = $file;
	$module =~ s/.*?\/([^\/]*?)\.api$/$1/;
	$self->parse_api_file($file,$module);
    }   

    return $self;
}

sub parse_api_file {
    my $self = shift;

    my $options = \${$self->{OPTIONS}};
    my $output = \${$self->{OUTPUT}};
    my $allowed_kind = \%{$self->{ALLOWED_KIND}};
    my $allowed_modules = \%{$self->{ALLOWED_MODULES}};
    my $allowed_modules_limited = \%{$self->{ALLOWED_MODULES_LIMITED}};
    my $allowed_modules_unlimited = \%{$self->{ALLOWED_MODULES_UNLIMITED}};
    my $translate_argument = \%{$self->{TRANSLATE_ARGUMENT}};
    my $type_format = \%{$self->{TYPE_FORMAT}};

    my $file = shift;
    my $module = shift;

    my $kind;
    my $format;
    my $extension = 0;
    my $forbidden = 0;

    if($$options->progress) {
	$$output->progress("$file");
    }

    open(IN, "< $file") || die "$file: $!\n";
    $/ = "\n";
    while(<IN>) {
	s/^\s*?(.*?)\s*$/$1/; # remove whitespace at begin and end of line
	s/^(.*?)\s*#.*$/$1/;  # remove comments
	/^$/ && next;         # skip empty lines

	if(s/^%(\S+)\s*//) {
	    $kind = $1;
	    $format = undef;
	    $forbidden = 0;
	    $extension = 0;

	    $$allowed_kind{$kind} = 1;
	    if(/^--forbidden/) {
		$forbidden = 1;
	    } elsif(/^--extension/) {
		$extension = 1;
	    } elsif(/^--format=(\".*?\"|\S*)/) {
		$format = $1;
		$format =~ s/^\"(.*?)\"$/$1/;
	    }

	    if(!defined($format)) {
		if($kind eq "long") {
		    $format  = "%d|%u|%x|%X|";
		    $format .= "%hd|%hu|%hx|%hX|";
		    $format .= "%ld|%lu|%lx|%lX|";
		    $format .= "%04x|%04X|0x%04x|0x%04X|";
		    $format .= "%08x|%08X|0x%08x|0x%08X|";
                    $format .= "%08lx|%08lX|0x%08lx|0x%08lX";
		} elsif($kind eq "longlong") {
		    $format = "%lld";
		} elsif($kind eq "ptr") {
		    $format = "%p";
		} elsif($kind eq "segptr") {
		    $format = "%p";
		} elsif($kind eq "str") {
		    $format = "%p|%s";
		} elsif($kind eq "wstr") {
		    $format = "%p|%s";
		} elsif($kind eq "word") {
		    $format  = "%d|%u|%x|%X|";
		    $format .= "%hd|%hu|%hx|%hX|";
		    $format .= "%04x|%04X|0x%04x|0x%04X";
		} else {
		    $format = "<unknown>";
		}
	    }
	} elsif(defined($kind)) {
	    my $type = $_;
	    if(!$forbidden) {
		if(defined($module)) {
		    if($$allowed_modules_unlimited{$type}) {
			$$output->write("$file: type ($type) already specificed as an unlimited type\n");
		    } elsif(!$$allowed_modules{$type}{$module}) {
			$$allowed_modules{$type}{$module} = 1;
			$$allowed_modules_limited{$type} = 1;
		    } else {
			$$output->write("$file: type ($type) already specificed\n");
		    }
		} else {
		    $$allowed_modules_unlimited{$type} = 1;
		}
	    } else {
		$$allowed_modules_limited{$type} = 1;
	    }
	    if(defined($$translate_argument{$type}) && $$translate_argument{$type} ne $kind) {
		$$output->write("$file: type ($type) respecified as different kind ($kind != $$translate_argument{$type})\n");
	    } else {
		$$translate_argument{$type} = $kind;
	    }
		
	    $$type_format{$module}{$type} = $format;
	} else {
	    $$output->write("$file: file must begin with %<type> statement\n");
	    exit 1;
	}
    }
    close(IN);
}

sub get_spec_file_type {
    my $proto = shift;
    my $class = ref($proto) || $proto;

    my $file = shift;

    my $module;
    my $type;

    open(IN, "< $file") || die "$file: $!\n";
    local $/ = "\n";
    while(<IN>) {
	s/^\s*(.*?)\s*$/$1/;
	s/^(.*?)\s*#.*$/$1/;
	/^$/ && next;

	if(/^name\s*(\S*)/) { $module = $1; }
	if(/^type\s*(\w+)/) { $type = $1; }

	if(defined($module) && defined($type)) { last; }
    }
    close(IN);

    return ($type, $module);
}

sub read_spec_files {
    my $proto = shift;
    my $class = ref($proto) || $proto;

    my $modules = shift;
    my $wine_dir = shift;
    my $current_dir = shift;
    my $files = shift;
    my $win16api = shift;
    my $win32api = shift;

    foreach my $file (@$files) {
	(my $type, my $module) = 'winapi'->get_spec_file_type("$wine_dir/$file");
	$modules->spec_file_module($file, $module);
	if($type eq "win16") {
	    $win16api->parse_spec_file("$wine_dir/$file");
	} elsif($type eq "win32") {
	    $win32api->parse_spec_file("$wine_dir/$file");
	}
    }

    foreach my $self ($win16api, $win32api) {
	my $function_forward = \%{$self->{FUNCTION_FORWARD}};
	my $function_internal_name = \%{$self->{FUNCTION_INTERNAL_NAME}};
	my $function_module = \%{$self->{FUNCTION_MODULE}};
	
	foreach my $forward_name (sort(keys(%$function_forward))) {
	    $$function_forward{$forward_name} =~ /^(\S*):(\S*)\.(\S*)$/;
	    (my $from_module, my $to_module, my $external_name) = ($1, $2, $3);
	    my $internal_name = $$function_internal_name{$external_name};
	    if(defined($internal_name)) {
		$$function_module{$internal_name} .= " & $from_module";
	    }
	}
    }
}

sub read_all_spec_files {
    my $proto = shift;
    my $class = ref($proto) || $proto;

    my $modules = shift;    
    my $wine_dir = shift;
    my $current_dir = shift;
    my $file_type = shift;
    my $win16api = shift;
    my $win32api = shift;

    my @files = map {
	s/^$wine_dir\/(.*)$/$1/;
	if(&$file_type($_) eq "library") {
	    $_;
	} else {
	    ();
	}
    } split(/\n/, `find $wine_dir -name \\*.spec`);
    
    'winapi'->read_spec_files($modules, $wine_dir, $current_dir, \@files, $win16api, $win32api); 
}

sub parse_spec_file {
    my $self = shift;

    my $options = \${$self->{OPTIONS}};
    my $output = \${$self->{OUTPUT}};
    my $function_internal_arguments = \%{$self->{FUNCTION_INTERNAL_ARGUMENTS}};
    my $function_external_arguments = \%{$self->{FUNCTION_EXTERNAL_ARGUMENTS}};
    my $function_internal_ordinal = \%{$self->{FUNCTION_INTERNAL_ORDINAL}};
    my $function_external_ordinal = \%{$self->{FUNCTION_EXTERNAL_ORDINAL}};
    my $function_internal_calling_convention = \%{$self->{FUNCTION_INTERNAL_CALLING_CONVENTION}};
    my $function_external_calling_convention = \%{$self->{FUNCTION_EXTERNAL_CALLING_CONVENTION}};
    my $function_internal_name = \%{$self->{FUNCTION_INTERNAL_NAME}};
    my $function_external_name = \%{$self->{FUNCTION_EXTERNAL_NAME}};
    my $function_stub = \%{$self->{FUNCTION_STUB}};
    my $function_forward = \%{$self->{FUNCTION_FORWARD}};
    my $function_internal_module = \%{$self->{FUNCTION_INTERNAL_MODULE}};
    my $function_external_module = \%{$self->{FUNCTION_EXTERNAL_MODULE}};
    my $modules = \%{$self->{MODULES}};
    my $module_files = \%{$self->{MODULE_FILES}};

    my $file = shift;

    my %ordinals;
    my $type;
    my $module;
    my $module_file;

    if($$options->progress) {
	$$output->progress("$file");
    }

    open(IN, "< $file") || die "$file: $!\n";
    $/ = "\n";
    my $header = 1;
    my $lookahead = 0;
    while($lookahead || defined($_ = <IN>)) {
	$lookahead = 0;
	s/^\s*(.*?)\s*$/$1/;
	s/^(.*?)\s*#.*$/$1/;
	/^$/ && next;

	if($header)  {
	    if(/^name\s*(\S*)/) { $module = $1; }
	    if(/^file\s*(\S*)/) { $module_file = $1; }
	    if(/^type\s*(\w+)/) { $type = $1; }
	    if(/^\d+|@/) { $header = 0; $lookahead = 1; }
	    next;
	} 

	my $ordinal;
	if(/^(\d+|@)\s+
	   (pascal|pascal16|stdcall|cdecl|register|interrupt|varargs)
	   (?:\s+(?:-noimport|-norelay|-i386|-ret64))*\s+(\S+)\s*\(\s*(.*?)\s*\)\s*(\S+)$/x)
	{
	    my $calling_convention = $2;
	    my $external_name = $3;
	    my $arguments = $4;
	    my $internal_name = $5;
	   
	    $ordinal = $1;

	    # FIXME: Internal name existing more than once not handled properly
	    $$function_internal_name{$external_name} = $internal_name;
	    $$function_external_name{$internal_name} = $external_name;
	    $$function_internal_arguments{$internal_name} = $arguments;
	    $$function_external_arguments{$external_name} = $arguments;
	    $$function_internal_ordinal{$internal_name} = $ordinal;
	    $$function_external_ordinal{$external_name} = $ordinal;
	    $$function_internal_calling_convention{$internal_name} = $calling_convention;
	    $$function_external_calling_convention{$external_name} = $calling_convention;
	    if(!$$function_internal_module{$internal_name}) {
		$$function_internal_module{$internal_name} = "$module";
	    } elsif($$function_internal_module{$internal_name} !~ /$module/) {
		if(0) {
		    $$output->write("$file: $external_name: the internal function ($internal_name) " . 
				    "already belongs to a module ($$function_internal_module{$internal_name})\n");
		}
		$$function_internal_module{$internal_name} .= " & $module";
	    }
	    if(!$$function_external_module{$external_name}) {
		$$function_external_module{$external_name} = "$module";
	    } elsif($$function_external_module{$external_name} !~ /$module/) {
		if(0) {
		    $$output->write("$file: $internal_name: the external function ($external_name) " . 
				    "already belongs to a module ($$function_external_module{$external_name})\n");
		}
		$$function_external_module{$external_name} .= " & $module";
	    }

	    if(0 && $$options->spec_mismatch) {
		if($external_name eq "@") {
		    if($internal_name !~ /^\U$module\E_$ordinal$/) {
			$$output->write("$file: $external_name: the internal name ($internal_name) mismatch\n");
		    }
		} else {
		    my $name = $external_name;

		    my $name1 = $name;
		    $name1 =~ s/^Zw/Nt/;

		    my $name2 = $name;
		    $name2 =~ s/^(?:_|Rtl|k32|K32)//;

		    my $name3 = $name;
		    $name3 =~ s/^INT_Int[0-9a-f]{2}Handler$/BUILTIN_DefaultIntHandler/;

		    my $name4 = $name;
		    $name4 =~ s/^(VxDCall)\d$/$1/;

		    # FIXME: This special case is becuase of a very ugly kludge that should be fixed IMHO
		    my $name5 = $name;
		    $name5 =~ s/^(.*?16)_(.*?)$/$1_fn$2/;

		    if(uc($internal_name) ne uc($external_name) &&
		       $internal_name !~ /(\Q$name\E|\Q$name1\E|\Q$name2\E|\Q$name3\E|\Q$name4\E|\Q$name5\E)/)
		    {
			$$output->write("$file: $external_name: internal name ($internal_name) mismatch\n");
		    }
		}
	    }
	} elsif(/^(\d+|@)\s+stub(?:\s+(?:-noimport|-norelay|-i386|-ret64))?\s+(\S+)$/) {
	    my $external_name = $2;

	    $ordinal = $1;

	    my $internal_name;
	    if($type eq "win16") {
		if($external_name =~ /\d$/) {
		    $internal_name = $external_name . "_16";
		} else {
		    $internal_name = $external_name . "16";
		}
	    } else {
		$internal_name = $external_name;
	    }

	    # FIXME: Internal name existing more than once not handled properly
	    $$function_stub{$internal_name} = 1;
	    $$function_internal_ordinal{$internal_name} = $ordinal;
	    $$function_external_ordinal{$external_name} = $ordinal;
	    if(!$$function_internal_module{$internal_name}) {
		$$function_internal_module{$internal_name} = "$module";
	    } elsif($$function_internal_module{$internal_name} !~ /$module/) {
		$$function_internal_module{$internal_name} .= " & $module";
	    }
	    if(!$$function_external_module{$external_name}) {
		$$function_external_module{$external_name} = "$module";
	    } elsif($$function_external_module{$external_name} !~ /$module/) {
		$$function_external_module{$external_name} .= " & $module";
	    }
	} elsif(/^(\d+|@)\s+forward(?:\s+(?:-noimport|-norelay|-i386|-ret64))?\s+(\S+)\s+(\S+)\.(\S+)$/) {
	    $ordinal = $1;

	    my $external_name = $2;
	    my $forward_module = lc($3);
	    my $forward_name = $4;

	    $$function_forward{$external_name} = "$module:$forward_module.$forward_name";
	} elsif(/^(\d+|@)\s+(equate|extern|variable)/) {
	    # ignore
	} else {
	    my $next_line = <IN>;
	    if(!defined($next_line) || $next_line =~ /^\s*\d|@/) {
		die "$file: $.: syntax error: '$_'\n";
	    } else {
		$_ .= $next_line;
		$lookahead = 1;
	    }
	}
	
	if(defined($ordinal)) {
	    if($ordinal ne "@" && $ordinals{$ordinal}) {
		$$output->write("$file: ordinal redefined: $_\n");
	    }
	    $ordinals{$ordinal}++;
	}
    }
    close(IN);

    $$modules{$module}++;

    $$module_files{$module} = $file;
}

sub name {
    my $self = shift;
    my $name = \${$self->{NAME}};

    return $$name;
}

sub is_allowed_kind {
    my $self = shift;
    my $allowed_kind = \%{$self->{ALLOWED_KIND}};

    my $kind = shift;
    if(defined($kind)) {
	return $$allowed_kind{$kind};
    } else {
	return 0;
    }
}

sub is_limited_type {
    my $self = shift;
    my $allowed_modules_limited = \%{$self->{ALLOWED_MODULES_LIMITED}};

    my $type = shift;

    return $$allowed_modules_limited{$type};
}

sub allowed_type_in_module {
    my $self = shift;
    my $allowed_modules = \%{$self->{ALLOWED_MODULES}};
    my $allowed_modules_limited = \%{$self->{ALLOWED_MODULES_LIMITED}};

    my $type = shift;
    my @modules = split(/ \& /, shift);

    if(!$$allowed_modules_limited{$type}) { return 1; }

    foreach my $module (@modules) {
	if($$allowed_modules{$type}{$module}) { return 1; }
    }

    return 0;
}

sub type_used_in_module {
    my $self = shift;
    my $used_modules = \%{$self->{USED_MODULES}};

    my $type = shift;
    my @modules = split(/ \& /, shift);

    foreach my $module (@modules) {
	$$used_modules{$type}{$module} = 1;
    }

    return ();
}

sub types_not_used {
    my $self = shift;
    my $used_modules = \%{$self->{USED_MODULES}};
    my $allowed_modules = \%{$self->{ALLOWED_MODULES}};

    my $not_used;
    foreach my $type (sort(keys(%$allowed_modules))) {
	foreach my $module (sort(keys(%{$$allowed_modules{$type}}))) {
	    if(!$$used_modules{$type}{$module}) {
		$$not_used{$module}{$type} = 1;
	    }
	}
    }
    return $not_used;
}

sub types_unlimited_used_in_modules {
    my $self = shift;

    my $output = \${$self->{OUTPUT}};
    my $used_modules = \%{$self->{USED_MODULES}};
    my $allowed_modules = \%{$self->{ALLOWED_MODULES}};
    my $allowed_modules_unlimited = \%{$self->{ALLOWED_MODULES_UNLIMITED}};

    my $used_types;
    foreach my $type (sort(keys(%$allowed_modules_unlimited))) {
	my $count = 0;
	my @modules = ();
	foreach my $module (sort(keys(%{$$used_modules{$type}}))) {
	    $count++;
	    push @modules, $module;
	}
        if($count) {
	    foreach my $module (@modules) {
	      $$used_types{$type}{$module} = 1;
	    }
	}
    }
    return $used_types;
}

sub translate_argument {
    my $self = shift;
    my $translate_argument = \%{$self->{TRANSLATE_ARGUMENT}};

    my $argument = shift;

    return $$translate_argument{$argument};
}

sub all_declared_types {
    my $self = shift;
    my $translate_argument = \%{$self->{TRANSLATE_ARGUMENT}};

    return sort(keys(%$translate_argument));
}

sub found_type {
    my $self = shift;
    my $type_found = \%{$self->{TYPE_FOUND}};

    my $name = shift;

    $$type_found{$name}++;
}

sub type_found {
    my $self = shift;
    my $type_found= \%{$self->{TYPE_FOUND}};

    my $name = shift;

    return $$type_found{$name};
}

sub is_allowed_type_format {
    my $self = shift;
    my $type_format = \%{$self->{TYPE_FORMAT}};

    my $module = shift;
    my $type = shift;
    my $format = shift;

    my $formats;

    if(defined($module) && defined($type)) {
	local $_;
	foreach (split(/ & /, $module)) {
	    if(defined($formats)) { 
		$formats .= "|"; 
	    } else {
		$formats = "";
	    }	    
	    if(defined($$type_format{$_}{$type})) {
		$formats .= $$type_format{$_}{$type};
	    }
	}
    }

    if(defined($formats)) {
	local $_;
	foreach (split(/\|/, $formats)) {
	    if($_ eq $format) {
		return 1;
	    }
	}
    }

    return 0;
}

sub all_modules {
    my $self = shift;
    my $modules = \%{$self->{MODULES}};

    return sort(keys(%$modules));
}

sub is_module {
    my $self = shift;
    my $modules = \%{$self->{MODULES}};

    my $name = shift;

    return $$modules{$name};
}

sub module_file {
    my $self = shift;

    my $module = shift;

    my $module_files = \%{$self->{MODULE_FILES}};

    return $$module_files{$module};
}

sub all_internal_functions {
    my $self = shift;
    my $function_internal_calling_convention = \%{$self->{FUNCTION_INTERNAL_CALLING_CONVENTION}};

    return sort(keys(%$function_internal_calling_convention));
}

sub all_internal_functions_in_module {
    my $self = shift;
    my $function_internal_calling_convention = \%{$self->{FUNCTION_INTERNAL_CALLING_CONVENTION}};
    my $function_internal_module = \%{$self->{FUNCTION_INTERNAL_MODULE}};

    my $module = shift;

    my @names;
    foreach my $name (keys(%$function_internal_calling_convention)) {
	if($$function_internal_module{$name} eq $module) {
	    push @names, $name;
	}
    }

    return sort(@names);
}

sub all_functions_stub {
    my $self = shift;
    my $function_stub = \%{$self->{FUNCTION_STUB}};

    return sort(keys(%$function_stub));
}

sub all_internal_functions_found {
    my $self = shift;
    my $function_found = \%{$self->{FUNCTION_FOUND}};

    return sort(keys(%$function_found));
}

sub function_internal_ordinal {
    my $self = shift;
    my $function_internal_ordinal = \%{$self->{FUNCTION_INTERNAL_ORDINAL}};

    my $name = shift;

    return $$function_internal_ordinal{$name};
}

sub function_external_ordinal {
    my $self = shift;
    my $function_external_ordinal = \%{$self->{FUNCTION_EXTERNAL_ORDINAL}};

    my $name = shift;

    return $$function_external_ordinal{$name};
}

sub function_internal_calling_convention {
    my $self = shift;
    my $function_internal_calling_convention = \%{$self->{FUNCTION_INTERNAL_CALLING_CONVENTION}};

    my $name = shift;

    return $$function_internal_calling_convention{$name};
}

sub function_external_calling_convention {
    my $self = shift;
    my $function_external_calling_convention = \%{$self->{FUNCTION_EXTERNAL_CALLING_CONVENTION}};

    my $name = shift;

    return $$function_external_calling_convention{$name};
}

sub function_external_name {
    my $self = shift;
    my $function_external_name = \%{$self->{FUNCTION_EXTERNAL_NAME}};

    my $name = shift;

    return $$function_external_name{$name};
}

sub is_function {
    my $self = shift;
    my $function_internal_calling_convention = \%{$self->{FUNCTION_INTERNAL_CALLING_CONVENTION}};

    my $name = shift;

    return $$function_internal_calling_convention{$name};
}

sub is_shared_function {
    my $self = shift;
    my $function_shared = \%{$self->{FUNCTION_SHARED}};

    my $name = shift;

    return $$function_shared{$name};
}

sub found_shared_function {
    my $self = shift;
    my $function_shared = \%{$self->{FUNCTION_SHARED}};

    my $name = shift;

    $$function_shared{$name} = 1;
}

sub function_internal_arguments {
    my $self = shift;
    my $function_internal_arguments = \%{$self->{FUNCTION_INTERNAL_ARGUMENTS}};

    my $name = shift;

    return $$function_internal_arguments{$name};
}

sub function_external_arguments {
    my $self = shift;
    my $function_external_arguments = \%{$self->{FUNCTION_EXTERNAL_ARGUMENTS}};

    my $name = shift;

    return $$function_external_arguments{$name};
}

sub function_internal_module {
    my $self = shift;
    my $function_internal_module = \%{$self->{FUNCTION_INTERNAL_MODULE}};

    my $name = shift;

    return $$function_internal_module{$name};
}

sub function_external_module {
    my $self = shift;
    my $function_external_module = \%{$self->{FUNCTION_EXTERNAL_MODULE}};

    my $name = shift;

    return $$function_external_module{$name};
}

sub function_stub {
    my $self = shift;
    my $function_stub = \%{$self->{FUNCTION_STUB}};

    my $name = shift;

    return $$function_stub{$name};
}

sub found_internal_function {
    my $self = shift;
    my $function_found = \%{$self->{FUNCTION_FOUND}};

    my $name = shift;

    $$function_found{$name}++;
}

sub internal_function_found {
    my $self = shift;
    my $function_found = \%{$self->{FUNCTION_FOUND}};

    my $name = shift;

    return $$function_found{$name};
}

1;
