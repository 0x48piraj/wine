package winapi_function;

use strict;

sub new {
    my $proto = shift;
    my $class = ref($proto) || $proto;
    my $self  = {};
    bless ($self, $class);

    return $self;
}

sub file {
    my $self = shift;
    my $file = \${$self->{FILE}};

    local $_ = shift;

    if(defined($_)) { $$file = $_; }
    
    return $$file;
}

sub documentation {
    my $self = shift;
    my $documentation = \${$self->{DOCUMENTATION}};

    local $_ = shift;

    if(defined($_)) { $$documentation = $_; }
    
    return $$documentation;
}

sub documentation_line {
    my $self = shift;
    my $documentation_line = \${$self->{DOCUMENTATION_LINE}};

    local $_ = shift;

    if(defined($_)) { $$documentation_line = $_; }
    
    return $$documentation_line;
}

sub linkage {
    my $self = shift;
    my $linkage = \${$self->{LINKAGE}};

    local $_ = shift;

    if(defined($_)) { $$linkage = $_; }
    
    return $$linkage;
}

sub return_type {
    my $self = shift;
    my $return_type = \${$self->{RETURN_TYPE}};

    local $_ = shift;

    if(defined($_)) { $$return_type = $_; }
    
    return $$return_type;
}

sub calling_convention {
    my $self = shift;
    my $calling_convention = \${$self->{CALLING_CONVENTION}};

    local $_ = shift;

    if(defined($_)) { $$calling_convention = $_; }
    
    return $$calling_convention;
}

sub external_name16 {
    my $self = shift;
    my $external_name16 = \${$self->{EXTERNAL_NAME16}};

    local $_ = shift;

    if(defined($_)) { $$external_name16 = $_; }
    
    return $$external_name16;
}

sub external_name32 {
    my $self = shift;
    my $external_name32 = \${$self->{EXTERNAL_NAME32}};

    local $_ = shift;

    if(defined($_)) { $$external_name32 = $_; }
    
    return $$external_name32;
}

sub internal_name {
    my $self = shift;
    my $internal_name = \${$self->{INTERNAL_NAME}};

    local $_ = shift;

    if(defined($_)) { $$internal_name = $_; }
    
    return $$internal_name;
}

sub argument_types {
    my $self = shift;
    my $argument_types = \${$self->{ARGUMENT_TYPES}};

    local $_ = shift;

    if(defined($_)) { $$argument_types = $_; }
    
    return $$argument_types;
}

sub argument_names {
    my $self = shift;
    my $argument_names = \${$self->{ARGUMENT_NAMES}};

    local $_ = shift;

    if(defined($_)) { $$argument_names = $_; }
    
    return $$argument_names;
}

sub argument_documentations {
    my $self = shift;
    my $argument_documentations = \${$self->{ARGUMENT_DOCUMENTATIONS}};

    local $_ = shift;

    if(defined($_)) { $$argument_documentations = $_; }
    
    return $$argument_documentations;
}

sub module16 {
    my $self = shift;
    my $module16 = \${$self->{MODULE16}};

    local $_ = shift;

    if(defined($_)) { $$module16 = $_; }
    return $$module16;
}

sub module32 {
    my $self = shift;
    my $module32 = \${$self->{MODULE32}};

    local $_ = shift;
    
    if(defined($_)) { $$module32 = $_; }	
    return $$module32;
}

sub statements {
    my $self = shift;
    my $statements = \${$self->{STATEMENTS}};

    local $_ = shift;

    if(defined($_)) { $$statements = $_; }
    
    return $$statements;
}

sub module {
    my $self = shift;
    my $module16 = \${$self->{MODULE16}};
    my $module32 = \${$self->{MODULE32}};

    my $module;
    if(defined($$module16) && defined($$module32)) {
	$module = "$$module16 & $$module32";
    } elsif(defined($$module16)) {
	$module = $$module16;
    } elsif(defined($$module32)) {
	$module = $$module32;
    } else {
	$module = "";
    }
}

sub function_called {    
    my $self = shift;
    my $called_function_names = \%{$self->{CALLED_FUNCTION_NAMES}};

    my $name = shift;

    $$called_function_names{$name}++;
}

sub function_called_by { 
   my $self = shift;
   my $called_by_function_names = \%{$self->{CALLED_BY_FUNCTION_NAMES}};

   my $name = shift;

   $$called_by_function_names{$name}++;
}

sub called_function_names {    
    my $self = shift;
    my $called_function_names = \%{$self->{CALLED_FUNCTION_NAMES}};

    return sort(keys(%$called_function_names));
}

sub called_by_function_names {    
    my $self = shift;
    my $called_by_function_names = \%{$self->{CALLED_BY_FUNCTION_NAMES}};

    return sort(keys(%$called_by_function_names));
}


1;
