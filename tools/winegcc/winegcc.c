/*
 * MinGW wrapper: makes gcc behave like MinGW.
 *
 * Copyright 2000 Manuel Novoa III
 * Copyright 2000 Francois Gouget
 * Copyright 2002 Dimitrie O. Paun
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * DESCRIPTION
 *
 * all options for gcc start with '-' and are for the most part
 * single options (no parameters as separate argument). 
 * There are of course exceptions to this rule, so here is an 
 * exhaustive list of options that do take parameters (potentially)
 * as a separate argument:
 *
 * Compiler:
 * -x language
 * -o filename
 * -aux-info filename
 *
 * Preprocessor:
 * -D name 
 * -U name
 * -I dir
 * -MF file
 * -MT target
 * -MQ target
 * (all -i.* arg)
 * -include file 
 * -imacros file
 * -idirafter dir
 * -iwithprefix dir
 * -iwithprefixbefore dir
 * -isystem dir
 * -A predicate=answer
 *
 * Linking:
 * -l library
 * -Xlinker option
 * -u symbol
 *
 * Misc:
 * -b machine
 * -V version
 * -G num  (see NOTES below)
 *
 * NOTES
 * There is -G option for compatibility with System V that
 * takes no parameters. This makes "-G num" parsing ambiguous.
 * This option is synonymous to -shared, and as such we will
 * not support it for now.
 *
 * Special interest options 
 *
 *      Assembler Option
 *          -Wa,option
 *
 *      Linker Options
 *          object-file-name  -llibrary -nostartfiles  -nodefaultlibs
 *          -nostdlib -s  -static  -static-libgcc  -shared  -shared-libgcc
 *          -symbolic -Wl,option  -Xlinker option -u symbol
 *
 *      Directory Options
 *          -Bprefix  -Idir  -I-  -Ldir  -specs=file
 *
 *      Target Options
 *          -b machine  -V version
 *
 * Please note that the Target Options are relevant to everything:
 *   compiler, linker, assembler, preprocessor.
 * 
 */ 

#include "config.h"
#include "wine/port.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>

#include "utils.h"

static const char *app_loader_template =
    "#!/bin/sh\n"
    "\n"
    "appname=\"%s\"\n"
    "# determine the application directory\n"
    "appdir=''\n"
    "case \"$0\" in\n"
    "  */*)\n"
    "    # $0 contains a path, use it\n"
    "    appdir=`dirname \"$0\"`\n"
    "    ;;\n"
    "  *)\n"
    "    # no directory in $0, search in PATH\n"
    "    saved_ifs=$IFS\n"
    "    IFS=:\n"
    "    for d in $PATH\n"
    "    do\n"
    "      IFS=$saved_ifs\n"
    "      if [ -x \"$d/$appname\" ]; then appdir=\"$d\"; break; fi\n"
    "    done\n"
    "    ;;\n"
    "esac\n"
    "\n"
    "while true; do\n"
    "  case \"$1\" in\n"
    "    --debugmsg)\n"
    "      debugmsg=\"$1 $2\"\n"
    "      shift; shift;\n"
    "      ;;\n"
    "    --dll)\n"
    "      dll=\"$1 $2\"\n"
    "      shift; shift;\n"
    "      ;;\n"
    "    *)\n"
    "      break\n"
    "      ;;\n"
    "  esac\n"
    "done\n"
    "\n"
    "# figure out the full app path\n"
    "if [ -n \"$appdir\" ]; then\n"
    "    apppath=\"$appdir/$appname.exe.so\"\n"
    "    WINEDLLPATH=\"$appdir:$WINEDLLPATH\"\n"
    "    export WINEDLLPATH\n"
    "else\n"
    "    apppath=\"$appname.exe.so\"\n"
    "fi\n"
    "\n"
    "# determine the WINELOADER\n"
    "if [ ! -x \"$WINELOADER\" ]; then WINELOADER=\"wine\"; fi\n"
    "\n"
    "# and try to start the app\n"
    "exec \"$WINELOADER\" $debugmsg $dll -- \"$apppath\" \"$@\"\n"
;

static int keep_generated = 0;
static strarray *tmp_files;

struct options 
{
    enum { proc_cc = 0, proc_cpp = 1, proc_pp = 2} processor;
    int use_msvcrt;
    int nostdinc;
    int nostdlib;
    int nodefaultlibs;
    int noshortwchar;
    int gui_app;
    int compile_only;
    const char* output_name;
    strarray* lib_names;
    strarray* lib_dirs;
    strarray *linker_args;
    strarray *compiler_args;
    strarray* files;
};

static void clean_temp_files()
{
    int i;

    if (keep_generated) return;

    for (i = 0; i < tmp_files->size; i++)
	unlink(tmp_files->base[i]);
}

char* get_temp_file(const char* prefix, const char* suffix)
{
    char *tmp = strmake("%s-XXXXXX%s", prefix, suffix);
    int fd = mkstemps( tmp, strlen(suffix) );
    if (fd == -1)
    {
        /* could not create it in current directory, try in /tmp */
        free(tmp);
        tmp = strmake("/tmp/%s-XXXXXX%s", prefix, suffix);
        fd = mkstemps( tmp, strlen(suffix) );
        if (fd == -1) error( "could not create temp file" );
    }
    close( fd );
    strarray_add(tmp_files, tmp);

    return tmp;
}

static const char* get_translator(struct options* opts)
{
    switch(opts->processor)
    {
        case proc_pp:  return CPP;
        case proc_cc:  return CC;
        case proc_cpp: return CXX;
    }
    error("Unknown processor");
}

static void compile(struct options* opts)
{
    strarray *comp_args = strarray_alloc();
    int j, gcc_defs = 0;

    switch(opts->processor)
    {
	case proc_pp:  gcc_defs = 1; break;
#ifdef __GNUC__
	/* Note: if the C compiler is gcc we assume the C++ compiler is too */
	/* mixing different C and C++ compilers isn't supported in configure anyway */
	case proc_cc:  gcc_defs = 1; break;
	case proc_cpp: gcc_defs = 1; break;
#else
	case proc_cc:  gcc_defs = 0; break;
	case proc_cpp: gcc_defs = 0; break;
#endif
    }
    strarray_add(comp_args, get_translator(opts));

    if (opts->processor != proc_pp)
    {
#ifdef CC_FLAG_SHORT_WCHAR
	if (!opts->noshortwchar)
	{
            strarray_add(comp_args, CC_FLAG_SHORT_WCHAR);
            strarray_add(comp_args, "-DWINE_UNICODE_NATIVE");
	}
#endif
        strarray_addall(comp_args, strarray_fromstring(DLLFLAGS, " "));
    }
    if (!opts->nostdinc)
    {
        if (opts->use_msvcrt)
        {
            strarray_add(comp_args, "-I" INCLUDEDIR "/msvcrt");
            strarray_add(comp_args, "-D__MSVCRT__");
        }
        strarray_add(comp_args, "-I" INCLUDEDIR "/windows");
    }
    strarray_add(comp_args, "-DWIN32");
    strarray_add(comp_args, "-D_WIN32");
    strarray_add(comp_args, "-D__WIN32");
    strarray_add(comp_args, "-D__WIN32__");
    strarray_add(comp_args, "-D__WINNT");
    strarray_add(comp_args, "-D__WINNT__");

    if (gcc_defs)
    {
	strarray_add(comp_args, "-D__stdcall=__attribute__((__stdcall__))");
	strarray_add(comp_args, "-D__cdecl=__attribute__((__cdecl__))");
	strarray_add(comp_args, "-D__fastcall=__attribute__((__fastcall__))");
	strarray_add(comp_args, "-D_stdcall=__attribute__((__stdcall__))");
	strarray_add(comp_args, "-D_cdecl=__attribute__((__cdecl__))");
	strarray_add(comp_args, "-D_fastcall=__attribute__((__fastcall__))");
	strarray_add(comp_args, "-D__declspec(x)=__declspec_##x");
	strarray_add(comp_args, "-D__declspec_align(x)=__attribute__((aligned(x)))");
	strarray_add(comp_args, "-D__declspec_allocate(x)=__attribute__((section(x)))");
	strarray_add(comp_args, "-D__declspec_deprecated=__attribute__((deprecated))");
	strarray_add(comp_args, "-D__declspec_dllimport=__attribute__((dllimport))");
	strarray_add(comp_args, "-D__declspec_dllexport=__attribute__((dllexport))");
	strarray_add(comp_args, "-D__declspec_naked=__attribute__((naked))");
	strarray_add(comp_args, "-D__declspec_noinline=__attribute__((noinline))");
	strarray_add(comp_args, "-D__declspec_noreturn=__attribute__((noreturn))");
	strarray_add(comp_args, "-D__declspec_nothrow=__attribute__((nothrow))");
	strarray_add(comp_args, "-D__declspec_novtable=__attribute__(())"); /* ignore it */
	strarray_add(comp_args, "-D__declspec_selectany=__attribute__((weak))");
	strarray_add(comp_args, "-D__declspec_thread=__thread");
    }

    /* Wine specific defines */
    strarray_add(comp_args, "-D__WINE__");
    strarray_add(comp_args, "-D__int8=char");
    strarray_add(comp_args, "-D__int16=short");
    /* FIXME: what about 64-bit platforms? */
    strarray_add(comp_args, "-D__int32=int");
#ifdef HAVE_LONG_LONG
    strarray_add(comp_args, "-D__int64=long long");
#endif

    /* options we handle explicitly */
    if (opts->compile_only)
	strarray_add(comp_args, "-c");
    if (opts->output_name)
    {
	strarray_add(comp_args, "-o");
	strarray_add(comp_args, opts->output_name);
    }

    /* the rest of the pass-through parameters */
    for ( j = 0 ; j < opts->compiler_args->size ; j++ ) 
        strarray_add(comp_args, opts->compiler_args->base[j]);

    /* last, but not least, the files */
    for ( j = 0; j < opts->files->size; j++ )
	strarray_add(comp_args, opts->files->base[j]);

    spawn(comp_args);
}

static const char* compile_to_object(struct options* opts, const char* file)
{
    struct options copts;
    char* base_name;

    /* make a copy we so don't change any of the initial stuff */
    /* a shallow copy is exactly what we want in this case */
    base_name = get_basename(file);
    copts = *opts;
    copts.processor = proc_cc;
    copts.output_name = get_temp_file(base_name, ".o");
    copts.compile_only = 1;
    copts.files = strarray_alloc();
    strarray_add(copts.files, file);
    compile(&copts);
    strarray_free(copts.files);
    free(base_name);

    return copts.output_name;
}

static void build(struct options* opts)
{
    static const char *stdlibpath[] = { DLLDIR, LIBDIR, "/usr/lib", "/usr/local/lib" };
    strarray *so_libs, *arh_libs, *dll_libs, *lib_paths, *lib_dirs;
    strarray *res_files, *obj_files, *spec_args, *comp_args, *link_args;
    char *spec_c_name, *spec_o_name, *base_file, *base_name;
    const char* output_name;
    int j;

    so_libs = strarray_alloc();
    arh_libs = strarray_alloc();
    dll_libs = strarray_alloc();
    obj_files = strarray_alloc();
    res_files = strarray_alloc();
    lib_paths = strarray_alloc();

    output_name = opts->output_name ? opts->output_name : "a.out";

    /* get base filename by removing the .exe extension, if present */
    base_file = strdup(output_name);
    if (strendswith(base_file, ".exe.so")) base_file[strlen(base_file) - 7] = 0;
    else if (strendswith(base_file, ".exe")) base_file[strlen(base_file) - 4] = 0;
    if ((base_name = strrchr(base_file, '/'))) base_name++;
    else base_name = base_file;

    /* prepare the linking path */
    lib_dirs = strarray_dup(opts->lib_dirs);
    for ( j = 0; j < sizeof(stdlibpath)/sizeof(stdlibpath[0]);j++ )
	strarray_add(lib_dirs, stdlibpath[j]);

    for ( j = 0; j < lib_dirs->size; j++ )
	strarray_add(lib_paths, strmake("-L%s", lib_dirs->base[j]));

    /* prepare the libraries */    
    for ( j = 0; j < opts->lib_names->size; j++ )
    {
	const char* name = opts->lib_names->base[j];
	char* fullname;
	switch(get_lib_type(lib_dirs, name, &fullname))
	{
	    case file_arh:
		strarray_add(arh_libs, strdup(fullname));
		break;
	    case file_dll:
		strarray_add(dll_libs, strmake("-l%s", name));
		break;
	    case file_so:
		strarray_add(so_libs, strmake("-l%s", name));
		break;
	    default:
		fprintf(stderr, "Can't find library '%s', ignoring\n", name);
	}
	free(fullname);
    }

    if (!opts->nostdlib) 
    {
        if (opts->use_msvcrt) strarray_add(dll_libs, "-lmsvcrt");
    }

    if (!opts->nodefaultlibs) 
    {
        if (opts->gui_app) 
	{
            strarray_add(dll_libs, "-lshell32");
	    strarray_add(dll_libs, "-lcomdlg32");
	    strarray_add(dll_libs, "-lgdi32");
	}
        strarray_add(dll_libs, "-ladvapi32");
        strarray_add(dll_libs, "-luser32");
        strarray_add(dll_libs, "-lkernel32");
    }

    /* sort object file */
    for ( j = 0; j < opts->files->size; j++ )
    {
	const char* file = opts->files->base[j];
	switch(get_file_type(file))
	{
	    case file_rc:
		/* FIXME: invoke wrc to build it */
	        break;
	    case file_res:
		strarray_add(res_files, file);
		break;
	    case file_obj:
		strarray_add(obj_files, file);
		break;
	    case file_na:
		error("File does not exist: %s", file);
		break;
	    default:
		file = compile_to_object(opts, file);
		strarray_add(obj_files, file);
		break;
	}
    }

    /* run winebuild to generate the .spec.c file */
    spec_args = strarray_alloc();
    spec_c_name = get_temp_file(base_name, ".spec.c");
    strarray_add(spec_args, "winebuild");
    strarray_add(spec_args, "-o");
    strarray_add(spec_args, spec_c_name);
    strarray_add(spec_args, "--exe");
    strarray_add(spec_args, strmake("%s.exe", base_name));
    strarray_add(spec_args, opts->gui_app ? "-mgui" : "-mcui");

    for ( j = 0; j < lib_paths->size; j++ )
	strarray_add(spec_args, lib_paths->base[j]);

    for ( j = 0; j < dll_libs->size; j++ )
	strarray_add(spec_args, dll_libs->base[j]);

    for ( j = 0; j < arh_libs->size; j++)
	strarray_add(spec_args, arh_libs->base[j]);

    for ( j = 0; j < res_files->size; j++ )
	strarray_add(spec_args, res_files->base[j]);

    for ( j = 0; j < obj_files->size; j++ )
	strarray_add(spec_args, obj_files->base[j]);

    spawn(spec_args);

    /* compile the .spec.c file into a .spec.o file */
    comp_args = strarray_alloc();
    spec_o_name = get_temp_file(base_name, ".spec.o");
    strarray_add(comp_args, CC);
    strarray_addall(comp_args, strarray_fromstring(DLLFLAGS, " "));
    strarray_add(comp_args, "-o");
    strarray_add(comp_args, spec_o_name);
    strarray_add(comp_args, "-c");
    strarray_add(comp_args, spec_c_name);

    spawn(comp_args);
    
    /* link everything together now */
    link_args = strarray_alloc();
    strarray_add(link_args, get_translator(opts));
    strarray_addall(link_args, strarray_fromstring(LDDLLFLAGS, " "));

    strarray_add(link_args, "-o");
    strarray_add(link_args, strmake("%s.exe.so", base_file));

    for ( j = 0 ; j < opts->linker_args->size ; j++ ) 
        strarray_add(link_args, opts->linker_args->base[j]);

    for ( j = 0; j < lib_paths->size; j++ )
	strarray_add(link_args, lib_paths->base[j]);

    strarray_add(link_args, "-lwine");
    strarray_add(link_args, "-lm");

    for ( j = 0; j < so_libs->size; j++ )
	strarray_add(link_args, so_libs->base[j]);

    for ( j = 0; j < arh_libs->size; j++ )
	strarray_add(link_args, arh_libs->base[j]);

    for ( j = 0; j < obj_files->size; j++ )
	strarray_add(link_args, obj_files->base[j]);

    strarray_add(link_args, spec_o_name);

    spawn(link_args);

    /* create the loader script */
    create_file(base_file, app_loader_template, base_name);
    chmod(base_file, 0755);
}


static void forward(int argc, char **argv, struct options* opts)
{
    strarray *args = strarray_alloc();
    int j;

    strarray_add(args, get_translator(opts));

    for( j = 1; j < argc; j++ ) 
	strarray_add(args, argv[j]);

    spawn(args);
}

/*
 *      Linker Options
 *          object-file-name  -llibrary -nostartfiles  -nodefaultlibs
 *          -nostdlib -s  -static  -static-libgcc  -shared  -shared-libgcc
 *          -symbolic -Wl,option  -Xlinker option -u symbol
 */
static int is_linker_arg(const char* arg)
{
    static const char* link_switches[] = 
    {
	"-nostartfiles", "-nodefaultlibs", "-nostdlib", "-s", 
	"-static", "-static-libgcc", "-shared", "-shared-libgcc", "-symbolic"
    };
    int j;

    switch (arg[1]) 
    {
	case 'l': 
	case 'u':
	    return 1;
        case 'W':
            if (strncmp("-Wl,", arg, 4) == 0) return 1;
	    break;
	case 'X':
	    if (strcmp("-Xlinker", arg) == 0) return 1;
	    break;
    }

    for (j = 0; j < sizeof(link_switches)/sizeof(link_switches[0]); j++)
	if (strcmp(link_switches[j], arg) == 0) return 1;

    return 0;
}

/*
 *      Target Options
 *          -b machine  -V version
 */
static int is_target_arg(const char* arg)
{
    return arg[1] == 'b' || arg[2] == 'V';
}


/*
 *      Directory Options
 *          -Bprefix  -Idir  -I-  -Ldir  -specs=file
 */
static int is_directory_arg(const char* arg)
{
    return arg[1] == 'B' || arg[1] == 'L' || arg[1] == 'I' || strncmp("-specs=", arg, 7) == 0;
}

/*
 *      MinGW Options
 *	    -mno-cygwin -mwindows -mconsole -mthreads
 */ 
static int is_mingw_arg(const char* arg)
{
    static const char* mingw_switches[] = 
    {
	"-mno-cygwin", "-mwindows", "-mconsole", "-mthreads"
    };
    int j;

    for (j = 0; j < sizeof(mingw_switches)/sizeof(mingw_switches[0]); j++)
	if (strcmp(mingw_switches[j], arg) == 0) return 1;

    return 0;
}

int main(int argc, char **argv)
{
    int i, c, next_is_arg = 0, linking = 1;
    int raw_compiler_arg, raw_linker_arg;
    const char* option_arg;
    struct options opts;

    /* setup tmp file removal at exit */
    tmp_files = strarray_alloc();
    atexit(clean_temp_files);
    
    /* initialize options */
    memset(&opts, 0, sizeof(opts));
    opts.lib_names = strarray_alloc();
    opts.lib_dirs = strarray_alloc();
    opts.files = strarray_alloc();
    opts.linker_args = strarray_alloc();
    opts.compiler_args = strarray_alloc();

    /* determine the processor type */
    if (strendswith(argv[0], "winecpp")) opts.processor = proc_pp;
    else if (strendswith(argv[0], "++")) opts.processor = proc_cpp;
    
    /* parse options */
    for ( i = 1 ; i < argc ; i++ ) 
    {
        if (argv[i][0] == '-')  /* option */
	{
	    /* determine if tihs switch is followed by a separate argument */
	    next_is_arg = 0;
	    option_arg = 0;
	    switch(argv[i][1])
	    {
		case 'x': case 'o': case 'D': case 'U':
		case 'I': case 'A': case 'l': case 'u':
		case 'b': case 'V': case 'G':
		    if (argv[i][2]) option_arg = &argv[i][2];
		    else next_is_arg = 1;
		    break;
		case 'i':
		    next_is_arg = 1;
		    break;
		case 'a':
		    if (strcmp("-aux-info", argv[i]) == 0)
			next_is_arg = 1;
		    break;
		case 'X':
		    if (strcmp("-Xlinker", argv[i]) == 0)
			next_is_arg = 1;
		    break;
		case 'M':
		    c = argv[i][2];
		    if (c == 'F' || c == 'T' || c == 'Q')
		    {
			if (argv[i][3]) option_arg = &argv[i][3];
			else next_is_arg = 1;
		    }
		    break;
	    }
	    if (next_is_arg) option_arg = argv[i+1];

	    /* determine what options go 'as is' to the linker & the compiler */
	    raw_compiler_arg = raw_linker_arg = 0;
	    if (is_linker_arg(argv[i])) 
	    {
		raw_linker_arg = 1;
	    }
	    else 
	    {
		if (is_directory_arg(argv[i]) || is_target_arg(argv[i]))
		    raw_linker_arg = 1;
		raw_compiler_arg = !is_mingw_arg(argv[i]);
	    }

	    /* these things we handle explicitly so we don't pass them 'as is' */
	    if (argv[i][1] == 'l' || argv[i][1] == 'I' || argv[i][1] == 'L')
		raw_linker_arg = 0;
	    if (argv[i][1] == 'c' || argv[i][1] == 'L')
		raw_compiler_arg = 0;
	    if (argv[i][1] == 'o')
		raw_compiler_arg = raw_linker_arg = 0;

	    /* put the arg into the appropriate bucket */
	    if (raw_linker_arg) 
	    {
		strarray_add(opts.linker_args, argv[i]);
		if (next_is_arg && (i + 1 < argc)) 
		    strarray_add(opts.linker_args, argv[i + 1]);
	    }
	    if (raw_compiler_arg)
	    {
		strarray_add(opts.compiler_args, argv[i]);
		if (next_is_arg && (i + 1 < argc))
		    strarray_add(opts.compiler_args, argv[i + 1]);
	    }

	    /* do a bit of semantic analysis */
            switch (argv[i][1]) 
	    {
                case 'c':        /* compile or assemble */
		    if (argv[i][2] == 0) opts.compile_only = 1;
		    /* fall through */
                case 'S':        /* generate assembler code */
                case 'E':        /* preprocess only */
                    if (argv[i][2] == 0) linking = 0;
                    break;
		case 'f':
		    if (strcmp("-fno-short-wchar", argv[i]) == 0)
                        opts.noshortwchar = 1;
		    break;
		case 'l':
		    strarray_add(opts.lib_names, option_arg);
		    break;
		case 'L':
		    strarray_add(opts.lib_dirs, option_arg);
		    break;
                case 'M':        /* map file generation */
                    linking = 0;
                    break;
		case 'm':
		    if (strcmp("-mno-cygwin", argv[i]) == 0)
			opts.use_msvcrt = 1;
		    else if (strcmp("-mwindows", argv[i]) == 0)
			opts.gui_app = 1;
		    else if (strcmp("-mconsole", argv[i]) == 0)
			opts.gui_app = 0;
		    break;
                case 'n':
                    if (strcmp("-nostdinc", argv[i]) == 0)
                        opts.nostdinc = 1;
                    else if (strcmp("-nodefaultlibs", argv[i]) == 0)
                        opts.nodefaultlibs = 1;
                    else if (strcmp("-nostdlib", argv[i]) == 0)
                        opts.nostdlib = 1;
                    break;
		case 'o':
		    opts.output_name = option_arg;
		    break;
                case 's':
                    if (strcmp("-static", argv[i]) == 0) 
			linking = -1;
		    else if(strcmp("-save-temps", argv[i]) == 0)
			keep_generated = 1;
                    break;
                case 'v':
                    if (argv[i][2] == 0) verbose++;
                    break;
                case 'W':
                    if (strncmp("-Wl,", argv[i], 4) == 0)
		    {
                        if (strstr(argv[i], "-static"))
                            linking = -1;
                    }
                    break;
                case '-':
                    if (strcmp("-static", argv[i]+1) == 0)
                        linking = -1;
                    break;
            }

	    /* skip the next token if it's an argument */
	    if (next_is_arg) i++;
        }
	else
	{
	    strarray_add(opts.files, argv[i]);
	} 
    }

    if (opts.processor == proc_pp) linking = 0;
    if (linking == -1) error("Static linking is not supported.");

    if (opts.files->size == 0) forward(argc, argv, &opts);
    else if (linking) build(&opts);
    else compile(&opts);

    return 0;
}
