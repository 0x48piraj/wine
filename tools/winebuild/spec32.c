/*
 * 32-bit spec files
 *
 * Copyright 1993 Robert J. Amstadt
 * Copyright 1995 Martin von Loewis
 * Copyright 1995, 1996, 1997 Alexandre Julliard
 * Copyright 1997 Eric Youngdale
 * Copyright 1999 Ulrich Weigand
 */

#include <assert.h>
#include <unistd.h>

#include "winbase.h"
#include "wine/exception.h"
#include "build.h"


static int name_compare( const void *name1, const void *name2 )
{
    ORDDEF *odp1 = *(ORDDEF **)name1;
    ORDDEF *odp2 = *(ORDDEF **)name2;
    return strcmp( odp1->name, odp2->name );
}

/*******************************************************************
 *         AssignOrdinals
 *
 * Assign ordinals to all entry points.
 */
static void AssignOrdinals(void)
{
    int i, ordinal;

    if ( !nb_names ) return;

    /* sort the list of names */
    qsort( Names, nb_names, sizeof(Names[0]), name_compare );

    /* check for duplicate names */
    for (i = 0; i < nb_names - 1; i++)
    {
        if (!strcmp( Names[i]->name, Names[i+1]->name ))
        {
            current_line = max( Names[i]->lineno, Names[i+1]->lineno );
            fatal_error( "'%s' redefined (previous definition at line %d)\n",
                         Names[i]->name, min( Names[i]->lineno, Names[i+1]->lineno ) );
        }
    }

    /* start assigning from Base, or from 1 if no ordinal defined yet */
    if (Base == MAX_ORDINALS) Base = 1;
    for (i = 0, ordinal = Base; i < nb_names; i++)
    {
        if (Names[i]->ordinal != -1) continue;  /* already has an ordinal */
        while (Ordinals[ordinal]) ordinal++;
        if (ordinal >= MAX_ORDINALS)
        {
            current_line = Names[i]->lineno;
            fatal_error( "Too many functions defined (max %d)\n", MAX_ORDINALS );
        }
        Names[i]->ordinal = ordinal;
        Ordinals[ordinal] = Names[i];
    }
    if (ordinal > Limit) Limit = ordinal;
}


/*******************************************************************
 *         output_exports
 *
 * Output the export table for a Win32 module.
 */
static void output_exports( FILE *outfile, int nr_exports, int nr_names, int fwd_size )
{
    int i, fwd_pos = 0;

    if (!nr_exports) return;

    fprintf( outfile, "\n\n/* exports */\n\n" );
    fprintf( outfile, "typedef void (*func_ptr)();\n" );
    fprintf( outfile, "static struct {\n" );
    fprintf( outfile, "  struct {\n" );
    fprintf( outfile, "    unsigned int    Characteristics;\n" );
    fprintf( outfile, "    unsigned int    TimeDateStamp;\n" );
    fprintf( outfile, "    unsigned short  MajorVersion;\n" );
    fprintf( outfile, "    unsigned short  MinorVersion;\n" );
    fprintf( outfile, "    const char     *Name;\n" );
    fprintf( outfile, "    unsigned int    Base;\n" );
    fprintf( outfile, "    unsigned int    NumberOfFunctions;\n" );
    fprintf( outfile, "    unsigned int    NumberOfNames;\n" );
    fprintf( outfile, "    func_ptr       *AddressOfFunctions;\n" );
    fprintf( outfile, "    const char    **AddressOfNames;\n" );
    fprintf( outfile, "    unsigned short *AddressOfNameOrdinals;\n" );
    fprintf( outfile, "    func_ptr        functions[%d];\n", nr_exports );
    if (nb_names)
    {
        fprintf( outfile, "    const char     *names[%d];\n", nb_names );
        fprintf( outfile, "    unsigned short  ordinals[%d];\n", nb_names );
        if (nb_names % 2) fprintf( outfile, "    unsigned short  pad1;\n" );
    }
    if (fwd_size)
    {
        fprintf( outfile, "    char            forwards[%d];\n", (fwd_size + 3) & ~3 );
    }
    fprintf( outfile, "  } exp;\n" );

#ifdef __i386__
    fprintf( outfile, "  struct {\n" );
    fprintf( outfile, "    unsigned char  jmp;\n" );
    fprintf( outfile, "    unsigned char  addr[4];\n" );
    fprintf( outfile, "    unsigned char  ret;\n" );
    fprintf( outfile, "    unsigned short args;\n" );
    fprintf( outfile, "    func_ptr       orig;\n" );
    fprintf( outfile, "    unsigned int   argtypes;\n" );
    fprintf( outfile, "  } relay[%d];\n", nr_exports );
#endif  /* __i386__ */

    fprintf( outfile, "} exports = {\n  {\n" );
    fprintf( outfile, "    0,\n" );                 /* Characteristics */
    fprintf( outfile, "    0,\n" );                 /* TimeDateStamp */
    fprintf( outfile, "    0,\n" );                 /* MajorVersion */
    fprintf( outfile, "    0,\n" );                 /* MinorVersion */
    fprintf( outfile, "    dllname,\n" );           /* Name */
    fprintf( outfile, "    %d,\n", Base );          /* Base */
    fprintf( outfile, "    %d,\n", nr_exports );    /* NumberOfFunctions */
    fprintf( outfile, "    %d,\n", nb_names );      /* NumberOfNames */
    fprintf( outfile, "    exports.exp.functions,\n" ); /* AddressOfFunctions */
    if (nb_names)
    {
        fprintf( outfile, "    exports.exp.names,\n" );     /* AddressOfNames */
        fprintf( outfile, "    exports.exp.ordinals,\n" );  /* AddressOfNameOrdinals */
    }
    else
    {
        fprintf( outfile, "    0,\n" );  /* AddressOfNames */
        fprintf( outfile, "    0,\n" );  /* AddressOfNameOrdinals */
    }

    /* output the function addresses */

    fprintf( outfile, "    {\n      " );
    for (i = Base; i <= Limit; i++)
    {
        ORDDEF *odp = Ordinals[i];
        if (!odp) fprintf( outfile, "0" );
        else switch(odp->type)
        {
        case TYPE_EXTERN:
            fprintf( outfile, "%s", odp->u.ext.link_name );
            break;
        case TYPE_STDCALL:
        case TYPE_STDCALL64:
        case TYPE_VARARGS:
        case TYPE_CDECL:
            fprintf( outfile, "%s", odp->u.func.link_name);
            break;
        case TYPE_STUB:
            if (odp->name[0]) fprintf( outfile, "__stub_%s", odp->name );
            else fprintf( outfile, "__stub_%d", i );
            break;
        case TYPE_REGISTER:
            fprintf( outfile, "__regs_%d", i );
            break;
        case TYPE_FORWARD:
            fprintf( outfile, "(func_ptr)&exports.exp.forwards[%d] /* %s */",
                     fwd_pos, odp->u.fwd.link_name );
            fwd_pos += strlen(odp->u.fwd.link_name) + 1;
            break;
        default:
            assert(0);
        }
        if (i < Limit) fprintf( outfile, ",\n      " );
        else fprintf( outfile, "\n    },\n" );
    }

    if (nb_names)
    {
        /* output the function names */

        fprintf( outfile, "    {\n" );
        for (i = 0; i < nb_names; i++)
        {
            if (i) fprintf( outfile, ",\n" );
            fprintf( outfile, "      \"%s\"", Names[i]->name );
        }
        fprintf( outfile, "\n    },\n" );

        /* output the function ordinals */

        fprintf( outfile, "    {\n     " );
        for (i = 0; i < nb_names; i++)
        {
            fprintf( outfile, "%4d", Names[i]->ordinal - Base );
            if (i < nb_names-1)
            {
                fputc( ',', outfile );
                if ((i % 8) == 7) fprintf( outfile, "\n     " );
            }
        }
        fprintf( outfile, "\n    },\n" );
        if (nb_names % 2) fprintf( outfile, "    0,\n" );
    }

    /* output forwards */

    if (fwd_size)
    {
        for (i = Base; i <= Limit; i++)
        {
            ORDDEF *odp = Ordinals[i];
            if (odp && odp->type == TYPE_FORWARD)
                fprintf( outfile, "    \"%s\\0\"\n", odp->u.fwd.link_name );
        }
    }

    /* output relays */

#ifdef __i386__
    fprintf( outfile, "  },\n  {\n" );
    for (i = Base; i <= Limit; i++)
    {
        ORDDEF *odp = Ordinals[i];

        if (odp && ((odp->type == TYPE_STDCALL) ||
                    (odp->type == TYPE_STDCALL64) ||
                    (odp->type == TYPE_CDECL) ||
                    (odp->type == TYPE_REGISTER)))
        {
            unsigned int j, mask = 0;
            for (j = 0; odp->u.func.arg_types[j]; j++)
            {
                if (odp->u.func.arg_types[j] == 't') mask |= 1<< (j*2);
                if (odp->u.func.arg_types[j] == 'W') mask |= 2<< (j*2);
            }

            switch(odp->type)
            {
            case TYPE_STDCALL64:
                if (j < 16) mask |= 0x80000000;
                /* fall through */
            case TYPE_STDCALL:
                fprintf( outfile, "    { 0xe9, { 0,0,0,0 }, 0xc2, 0x%04x, %s, 0x%08x }",
                         strlen(odp->u.func.arg_types) * sizeof(int),
                         odp->u.func.link_name, mask );
                break;
            case TYPE_CDECL:
                fprintf( outfile, "    { 0xe9, { 0,0,0,0 }, 0xc3, 0x%04x, %s, 0x%08x }",
                         strlen(odp->u.func.arg_types) * sizeof(int),
                         odp->u.func.link_name, mask );
                break;
            case TYPE_REGISTER:
                fprintf( outfile, "    { 0xe9, { 0,0,0,0 }, 0xc3, 0x%04x, __regs_%d, 0x%08x }",
                         0x8000 | (strlen(odp->u.func.arg_types) * sizeof(int)), i, mask );
                break;
            default:
                assert(0);
            }
        }
        else fprintf( outfile, "    { 0, { 0,0,0,0 }, 0, 0, 0, 0 }" );

        if (i < Limit) fprintf( outfile, ",\n" );
    }
#endif  /* __i386__ */

    fprintf( outfile, "  }\n};\n" );
}


/*******************************************************************
 *         BuildSpec32File
 *
 * Build a Win32 C file from a spec file.
 */
void BuildSpec32File( FILE *outfile )
{
    ORDDEF *odp;
    int i, fwd_size = 0, have_regs = FALSE;
    int nr_exports, nr_imports, nr_resources;
    int characteristics, subsystem;
    const char *init_func;
    DWORD page_size;

#ifdef HAVE_GETPAGESIZE
    page_size = getpagesize();
#else
# ifdef __svr4__
    page_size = sysconf(_SC_PAGESIZE);
# else
#   error Cannot get the page size on this platform
# endif
#endif

    AssignOrdinals();
    nr_exports = Base <= Limit ? Limit - Base + 1 : 0;

    fprintf( outfile, "/* File generated automatically from %s; do not edit! */\n\n",
             input_file_name );

    /* Reserve some space for the PE header */

    fprintf( outfile, "extern char pe_header[];\n" );
    fprintf( outfile, "asm(\".section .text\\n\\t\"\n" );
    fprintf( outfile, "    \".align %ld\\n\"\n", page_size );
    fprintf( outfile, "    \"pe_header:\\t.fill %ld,1,0\\n\\t\");\n", page_size );

    fprintf( outfile, "static const char dllname[] = \"%s\";\n\n", DLLName );

    /* Output the stub function if necessary */

    for (i = 0, odp = EntryPoints; i < nb_entry_points; i++, odp++)
    {
        if (odp->type != TYPE_STUB) continue;
        fprintf( outfile, "#ifdef __GNUC__\n" );
        fprintf( outfile, "static void __wine_unimplemented( const char *func ) __attribute__((noreturn));\n" );
        fprintf( outfile, "#endif\n" );
        fprintf( outfile, "static void __wine_unimplemented( const char *func )\n{\n" );
        fprintf( outfile, "  struct exc_record {\n" );
        fprintf( outfile, "    unsigned int code, flags;\n" );
        fprintf( outfile, "    void *rec, *addr;\n" );
        fprintf( outfile, "    unsigned int params;\n" );
        fprintf( outfile, "    const void *info[15];\n" );
        fprintf( outfile, "  } rec;\n" );
        fprintf( outfile, "  extern void RtlRaiseException( struct exc_record * );\n\n" );
        fprintf( outfile, "  rec.code    = 0x%08x;\n", EXCEPTION_WINE_STUB );
        fprintf( outfile, "  rec.flags   = %d;\n", EH_NONCONTINUABLE );
        fprintf( outfile, "  rec.rec     = 0;\n" );
        fprintf( outfile, "  rec.params  = 2;\n" );
        fprintf( outfile, "  rec.info[0] = dllname;\n" );
        fprintf( outfile, "  rec.info[1] = func;\n" );
        fprintf( outfile, "#ifdef __GNUC__\n" );
        fprintf( outfile, "  rec.addr = __builtin_return_address(1);\n" );
        fprintf( outfile, "#else\n" );
        fprintf( outfile, "  rec.addr = 0;\n" );
        fprintf( outfile, "#endif\n" );
        fprintf( outfile, "  for (;;) RtlRaiseException( &rec );\n}\n\n" );
        break;
    }

    /* Output the DLL functions prototypes */

    for (i = 0, odp = EntryPoints; i < nb_entry_points; i++, odp++)
    {
        switch(odp->type)
        {
        case TYPE_EXTERN:
            fprintf( outfile, "extern void %s();\n", odp->u.ext.link_name );
            break;
        case TYPE_STDCALL:
        case TYPE_STDCALL64:
        case TYPE_VARARGS:
        case TYPE_CDECL:
            fprintf( outfile, "extern void %s();\n", odp->u.func.link_name );
            break;
        case TYPE_FORWARD:
            fwd_size += strlen(odp->u.fwd.link_name) + 1;
            break;
        case TYPE_REGISTER:
            fprintf( outfile, "extern void __regs_%d();\n", odp->ordinal );
            have_regs = TRUE;
            break;
        case TYPE_STUB:
            if (odp->name[0])
                fprintf( outfile,
                         "static void __stub_%s() { __wine_unimplemented(\"%s\"); }\n",
                         odp->name, odp->name );
            else
                fprintf( outfile,
                         "static void __stub_%d() { __wine_unimplemented(\"%d\"); }\n",
                         odp->ordinal, odp->ordinal );
            break;
        default:
            fprintf(stderr,"build: function type %d not available for Win32\n",
                    odp->type);
            exit(1);
        }
    }

    /* Output code for all register functions */

    if ( have_regs )
    { 
        fprintf( outfile, "#ifndef __GNUC__\n" );
        fprintf( outfile, "static void __asm__dummy(void) {\n" );
        fprintf( outfile, "#endif /* !defined(__GNUC__) */\n" );
        for (i = 0, odp = EntryPoints; i < nb_entry_points; i++, odp++)
        {
            if (odp->type != TYPE_REGISTER) continue;
            fprintf( outfile,
                     "asm(\".align 4\\n\\t\"\n"
                     "    \".type " PREFIX "__regs_%d,@function\\n\\t\"\n"
                     "    \"" PREFIX "__regs_%d:\\n\\t\"\n"
                     "    \"call " PREFIX "CALL32_Regs\\n\\t\"\n"
                     "    \".long " PREFIX "%s\\n\\t\"\n"
                     "    \".byte %d,%d\");\n",
                     odp->ordinal, odp->ordinal, odp->u.func.link_name,
                     4 * strlen(odp->u.func.arg_types),
                     4 * strlen(odp->u.func.arg_types) );
        }
        fprintf( outfile, "#ifndef __GNUC__\n" );
        fprintf( outfile, "}\n" );
        fprintf( outfile, "#endif /* !defined(__GNUC__) */\n" );
    }

    /* Output the exports and relay entry points */

    output_exports( outfile, nr_exports, nb_names, fwd_size );

    /* Output the DLL imports */

    nr_imports = output_imports( outfile );

    /* Output the resources */

    nr_resources = output_resources( outfile );

    /* Output LibMain function */

    init_func = DLLInitFunc[0] ? DLLInitFunc : NULL;
    characteristics = subsystem = 0;
    switch(SpecMode)
    {
    case SPEC_MODE_DLL:
        if (init_func) fprintf( outfile, "extern void %s();\n", init_func );
        characteristics = IMAGE_FILE_DLL;
        break;
    case SPEC_MODE_GUIEXE:
        if (!init_func) init_func = "WinMain";
        fprintf( outfile,
                 "\n#include <winbase.h>\n"
                 "static void exe_main(void)\n"
                 "{\n"
                 "    extern int PASCAL %s(HINSTANCE,HINSTANCE,LPSTR,INT);\n"
                 "    STARTUPINFOA info;\n"
                 "    LPSTR cmdline = GetCommandLineA();\n"
                 "    while (*cmdline && *cmdline != ' ') cmdline++;\n"
                 "    if (*cmdline) cmdline++;\n"
                 "    GetStartupInfoA( &info );\n"
                 "    if (!(info.dwFlags & STARTF_USESHOWWINDOW)) info.wShowWindow = 1;\n"
                 "    ExitProcess( %s( GetModuleHandleA(0), 0, cmdline, info.wShowWindow ) );\n"
                 "}\n\n", init_func, init_func );
        fprintf( outfile,
                 "int main( int argc, char *argv[] )\n"
                 "{\n"
                 "    extern void PROCESS_InitWinelib( int, char ** );\n"
                 "    PROCESS_InitWinelib( argc, argv );\n"
                 "    return 1;\n"
                 "}\n\n" );
        init_func = "exe_main";
        subsystem = IMAGE_SUBSYSTEM_WINDOWS_GUI;
        break;
    case SPEC_MODE_CUIEXE:
        if (!init_func) init_func = "wine_main";
        fprintf( outfile,
                 "\n#include <winbase.h>\n"
                 "static void exe_main(void)\n"
                 "{\n"
                 "    extern int %s( int argc, char *argv[] );\n"
                 "    extern int _ARGC;\n"
                 "    extern char **_ARGV;\n"
                 "    ExitProcess( %s( _ARGC, _ARGV ) );\n"
                 "}\n\n", init_func, init_func );
        fprintf( outfile,
                 "int main( int argc, char *argv[] )\n"
                 "{\n"
                 "    extern void PROCESS_InitWinelib( int, char ** );\n"
                 "    PROCESS_InitWinelib( argc, argv );\n"
                 "    return 1;\n"
                 "}\n\n" );
        init_func = "exe_main";
        subsystem = IMAGE_SUBSYSTEM_WINDOWS_CUI;
        break;
    case SPEC_MODE_GUIEXE_NO_MAIN:
        if (init_func) fprintf( outfile, "extern void %s();\n", init_func );
        subsystem = IMAGE_SUBSYSTEM_WINDOWS_GUI;
        break;
    case SPEC_MODE_CUIEXE_NO_MAIN:
        if (init_func) fprintf( outfile, "extern void %s();\n", init_func );
        subsystem = IMAGE_SUBSYSTEM_WINDOWS_CUI;
        break;
    }

    /* Output the NT header */

    /* this is the IMAGE_NT_HEADERS structure, but we cannot include winnt.h here */
    fprintf( outfile, "static const struct image_nt_headers\n{\n" );
    fprintf( outfile, "  int Signature;\n" );
    fprintf( outfile, "  struct file_header {\n" );
    fprintf( outfile, "    short Machine;\n" );
    fprintf( outfile, "    short NumberOfSections;\n" );
    fprintf( outfile, "    int   TimeDateStamp;\n" );
    fprintf( outfile, "    void *PointerToSymbolTable;\n" );
    fprintf( outfile, "    int   NumberOfSymbols;\n" );
    fprintf( outfile, "    short SizeOfOptionalHeader;\n" );
    fprintf( outfile, "    short Characteristics;\n" );
    fprintf( outfile, "  } FileHeader;\n" );
    fprintf( outfile, "  struct opt_header {\n" );
    fprintf( outfile, "    short Magic;\n" );
    fprintf( outfile, "    char  MajorLinkerVersion, MinorLinkerVersion;\n" );
    fprintf( outfile, "    int   SizeOfCode;\n" );
    fprintf( outfile, "    int   SizeOfInitializedData;\n" );
    fprintf( outfile, "    int   SizeOfUninitializedData;\n" );
    fprintf( outfile, "    void *AddressOfEntryPoint;\n" );
    fprintf( outfile, "    void *BaseOfCode;\n" );
    fprintf( outfile, "    void *BaseOfData;\n" );
    fprintf( outfile, "    void *ImageBase;\n" );
    fprintf( outfile, "    int   SectionAlignment;\n" );
    fprintf( outfile, "    int   FileAlignment;\n" );
    fprintf( outfile, "    short MajorOperatingSystemVersion;\n" );
    fprintf( outfile, "    short MinorOperatingSystemVersion;\n" );
    fprintf( outfile, "    short MajorImageVersion;\n" );
    fprintf( outfile, "    short MinorImageVersion;\n" );
    fprintf( outfile, "    short MajorSubsystemVersion;\n" );
    fprintf( outfile, "    short MinorSubsystemVersion;\n" );
    fprintf( outfile, "    int   Win32VersionValue;\n" );
    fprintf( outfile, "    int   SizeOfImage;\n" );
    fprintf( outfile, "    int   SizeOfHeaders;\n" );
    fprintf( outfile, "    int   CheckSum;\n" );
    fprintf( outfile, "    short Subsystem;\n" );
    fprintf( outfile, "    short DllCharacteristics;\n" );
    fprintf( outfile, "    int   SizeOfStackReserve;\n" );
    fprintf( outfile, "    int   SizeOfStackCommit;\n" );
    fprintf( outfile, "    int   SizeOfHeapReserve;\n" );
    fprintf( outfile, "    int   SizeOfHeapCommit;\n" );
    fprintf( outfile, "    int   LoaderFlags;\n" );
    fprintf( outfile, "    int   NumberOfRvaAndSizes;\n" );
    fprintf( outfile, "    struct { const void *VirtualAddress; int Size; } DataDirectory[%d];\n",
             IMAGE_NUMBEROF_DIRECTORY_ENTRIES );
    fprintf( outfile, "  } OptionalHeader;\n" );
    fprintf( outfile, "} nt_header = {\n" );
    fprintf( outfile, "  0x%04x,\n", IMAGE_NT_SIGNATURE );   /* Signature */

    fprintf( outfile, "  { 0x%04x,\n", IMAGE_FILE_MACHINE_I386 );  /* Machine */
    fprintf( outfile, "    0, 0, 0, 0,\n" );
    fprintf( outfile, "    sizeof(nt_header.OptionalHeader),\n" ); /* SizeOfOptionalHeader */
    fprintf( outfile, "    0x%04x },\n", characteristics );        /* Characteristics */

    fprintf( outfile, "  { 0x%04x,\n", IMAGE_NT_OPTIONAL_HDR_MAGIC );  /* Magic */
    fprintf( outfile, "    0, 0,\n" );                   /* Major/MinorLinkerVersion */
    fprintf( outfile, "    0, 0, 0,\n" );                /* SizeOfCode/Data */
    fprintf( outfile, "    %s,\n", init_func ? init_func : "0" );  /* AddressOfEntryPoint */
    fprintf( outfile, "    0, 0,\n" );                   /* BaseOfCode/Data */
    fprintf( outfile, "    pe_header,\n" );              /* ImageBase */
    fprintf( outfile, "    %ld,\n", page_size );         /* SectionAlignment */
    fprintf( outfile, "    %ld,\n", page_size );         /* FileAlignment */
    fprintf( outfile, "    1, 0,\n" );                   /* Major/MinorOperatingSystemVersion */
    fprintf( outfile, "    0, 0,\n" );                   /* Major/MinorImageVersion */
    fprintf( outfile, "    4, 0,\n" );                   /* Major/MinorSubsystemVersion */
    fprintf( outfile, "    0,\n" );                      /* Win32VersionValue */
    fprintf( outfile, "    %ld,\n", page_size );         /* SizeOfImage */
    fprintf( outfile, "    %ld,\n", page_size );         /* SizeOfHeaders */
    fprintf( outfile, "    0,\n" );                      /* CheckSum */
    fprintf( outfile, "    0x%04x,\n", subsystem );      /* Subsystem */
    fprintf( outfile, "    0, 0, 0, 0, 0, 0,\n" );
    fprintf( outfile, "    %d,\n", IMAGE_NUMBEROF_DIRECTORY_ENTRIES );  /* NumberOfRvaAndSizes */
    fprintf( outfile, "    {\n" );
    fprintf( outfile, "      { %s, %s },\n",  /* IMAGE_DIRECTORY_ENTRY_EXPORT */
             nr_exports ? "&exports" : "0", nr_exports ? "sizeof(exports.exp)" : "0" );
    fprintf( outfile, "      { %s, %s },\n",  /* IMAGE_DIRECTORY_ENTRY_IMPORT */
             nr_imports ? "&imports" : "0", nr_imports ? "sizeof(imports)" : "0" );
    fprintf( outfile, "      { %s, %s },\n",   /* IMAGE_DIRECTORY_ENTRY_RESOURCE */
             nr_resources ? "&resources" : "0", nr_resources ? "sizeof(resources)" : "0" );
    fprintf( outfile, "    }\n  }\n};\n\n" );

    /* Output the DLL constructor */

    fprintf( outfile, "#ifdef __GNUC__\n" );
    fprintf( outfile, "static void %s_init(void) __attribute__((constructor));\n", DLLName );
    fprintf( outfile, "#else /* defined(__GNUC__) */\n" );
    fprintf( outfile, "static void __asm__dummy_dll_init(void) {\n" );
    fprintf( outfile, "asm(\"\\t.section\t.init ,\\\"ax\\\"\\n\"\n" );
    fprintf( outfile, "    \"\\tcall %s_init\\n\"\n", DLLName );
    fprintf( outfile, "    \"\\t.previous\\n\");\n" );
    fprintf( outfile, "}\n" );
    fprintf( outfile, "#endif /* defined(__GNUC__) */\n" );
    fprintf( outfile, "static void %s_init(void)\n{\n", DLLName );
    fprintf( outfile, "    extern void BUILTIN32_RegisterDLL( const struct image_nt_headers *, const char * );\n" );
    fprintf( outfile, "    BUILTIN32_RegisterDLL( &nt_header, \"%s\" );\n}\n", DLLFileName );
}
